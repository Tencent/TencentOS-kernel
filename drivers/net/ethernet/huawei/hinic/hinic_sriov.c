// SPDX-License-Identifier: GPL-2.0-only
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": [NIC]" fmt

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>

#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_nic_cfg.h"
#include "hinic_nic_dev.h"
#include "hinic_sriov.h"
#include "hinic_lld.h"

#if !(defined(HAVE_SRIOV_CONFIGURE) || defined(HAVE_RHEL6_SRIOV_CONFIGURE))
ssize_t hinic_sriov_totalvfs_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sprintf(buf, "%d\n", pci_sriov_get_totalvfs(pdev));
}

ssize_t hinic_sriov_numvfs_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return sprintf(buf, "%d\n", pci_num_vf(pdev));
}

ssize_t hinic_sriov_numvfs_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int ret;
	u16 num_vfs;
	int cur_vfs, total_vfs;

	ret = kstrtou16(buf, 0, &num_vfs);
	if (ret < 0)
		return ret;

	cur_vfs = pci_num_vf(pdev);
	total_vfs = pci_sriov_get_totalvfs(pdev);

	if (num_vfs > total_vfs)
		return -ERANGE;

	if (num_vfs == cur_vfs)
		return count;		/* no change */

	if (num_vfs == 0) {
		/* disable VFs */
		ret = hinic_pci_sriov_configure(pdev, 0);
		if (ret < 0)
			return ret;
		return count;
	}

	/* enable VFs */
	if (cur_vfs) {
		nic_warn(&pdev->dev, "%d VFs already enabled. Disable before enabling %d VFs\n",
			 cur_vfs, num_vfs);
		return -EBUSY;
	}

	ret = hinic_pci_sriov_configure(pdev, num_vfs);
	if (ret < 0)
		return ret;

	if (ret != num_vfs)
		nic_warn(&pdev->dev, "%d VFs requested; only %d enabled\n",
			 num_vfs, ret);

	return count;
}

#endif /* !(HAVE_SRIOV_CONFIGURE || HAVE_RHEL6_SRIOV_CONFIGURE) */

int hinic_pci_sriov_disable(struct pci_dev *dev)
{
#ifdef CONFIG_PCI_IOV
	struct hinic_sriov_info *sriov_info;
	u16 tmp_vfs;

	sriov_info = hinic_get_sriov_info_by_pcidev(dev);
	/* if SR-IOV is already disabled then nothing will be done */
	if (!sriov_info->sriov_enabled)
		return 0;

	if (test_and_set_bit(HINIC_SRIOV_DISABLE, &sriov_info->state)) {
		nic_err(&sriov_info->pdev->dev,
			"SR-IOV disable in process, please wait");
		return -EPERM;
	}

	/* If our VFs are assigned we cannot shut down SR-IOV
	 * without causing issues, so just leave the hardware
	 * available but disabled
	 */
	if (pci_vfs_assigned(sriov_info->pdev)) {
		clear_bit(HINIC_SRIOV_DISABLE, &sriov_info->state);
		nic_warn(&sriov_info->pdev->dev, "Unloading driver while VFs are assigned - VFs will not be deallocated\n");
		return -EPERM;
	}
	sriov_info->sriov_enabled = false;

	/* disable iov and allow time for transactions to clear */
	pci_disable_sriov(sriov_info->pdev);

	tmp_vfs = (u16)sriov_info->num_vfs;
	sriov_info->num_vfs = 0;
	hinic_deinit_vf_hw(sriov_info->hwdev, OS_VF_ID_TO_HW(0),
			   OS_VF_ID_TO_HW(tmp_vfs - 1));

	clear_bit(HINIC_SRIOV_DISABLE, &sriov_info->state);

#endif

	return 0;
}

int hinic_pci_sriov_enable(struct pci_dev *dev, int num_vfs)
{
#ifdef CONFIG_PCI_IOV
	struct hinic_sriov_info *sriov_info;
	int err = 0;
	int pre_existing_vfs = 0;

	sriov_info = hinic_get_sriov_info_by_pcidev(dev);

	if (test_and_set_bit(HINIC_SRIOV_ENABLE, &sriov_info->state)) {
		nic_err(&sriov_info->pdev->dev,
			"SR-IOV enable in process, please wait, num_vfs %d\n",
			num_vfs);
		return -EPERM;
	}

	pre_existing_vfs = pci_num_vf(sriov_info->pdev);

	if (num_vfs > pci_sriov_get_totalvfs(sriov_info->pdev)) {
		clear_bit(HINIC_SRIOV_ENABLE, &sriov_info->state);
		return -ERANGE;
	}
	if (pre_existing_vfs && pre_existing_vfs != num_vfs) {
		err = hinic_pci_sriov_disable(sriov_info->pdev);
		if (err) {
			clear_bit(HINIC_SRIOV_ENABLE, &sriov_info->state);
			return err;
		}
	} else if (pre_existing_vfs == num_vfs) {
		clear_bit(HINIC_SRIOV_ENABLE, &sriov_info->state);
		return num_vfs;
	}

	err = hinic_init_vf_hw(sriov_info->hwdev, OS_VF_ID_TO_HW(0),
			       OS_VF_ID_TO_HW((u16)num_vfs - 1));
	if (err) {
		nic_err(&sriov_info->pdev->dev,
			"Failed to init vf in hardware before enable sriov, error %d\n",
			err);
		clear_bit(HINIC_SRIOV_ENABLE, &sriov_info->state);
		return err;
	}

	err = pci_enable_sriov(sriov_info->pdev, num_vfs);
	if (err) {
		nic_err(&sriov_info->pdev->dev,
			"Failed to enable SR-IOV, error %d\n", err);
		clear_bit(HINIC_SRIOV_ENABLE, &sriov_info->state);
		return err;
	}

	sriov_info->sriov_enabled = true;
	sriov_info->num_vfs = num_vfs;
	clear_bit(HINIC_SRIOV_ENABLE, &sriov_info->state);

	return num_vfs;
#else

	return 0;
#endif
}

static bool hinic_is_support_sriov_configure(struct pci_dev *pdev)
{
	enum hinic_init_state state = hinic_get_init_state(pdev);
	struct hinic_sriov_info *sriov_info;

	if (state < HINIC_INIT_STATE_NIC_INITED) {
		nic_err(&pdev->dev, "NIC device not initialized, don't support to configure sriov\n");
		return false;
	}

	sriov_info = hinic_get_sriov_info_by_pcidev(pdev);
	if (FUNC_SRIOV_FIX_NUM_VF(sriov_info->hwdev)) {
		nic_err(&pdev->dev, "Don't support to changed sriov configuration\n");
		return false;
	}

	return true;
}

int hinic_pci_sriov_configure(struct pci_dev *dev, int num_vfs)
{
	struct hinic_sriov_info *sriov_info;

	if (!hinic_is_support_sriov_configure(dev))
		return -EFAULT;

	sriov_info = hinic_get_sriov_info_by_pcidev(dev);

	if (test_bit(HINIC_FUNC_REMOVE, &sriov_info->state))
		return -EFAULT;

	if (!num_vfs)
		return hinic_pci_sriov_disable(dev);
	else
		return hinic_pci_sriov_enable(dev, num_vfs);
}

int hinic_ndo_set_vf_mac(struct net_device *netdev, int vf, u8 *mac)
{
	struct hinic_nic_dev *adapter = netdev_priv(netdev);
	struct hinic_sriov_info *sriov_info;
	int err;

	if (!FUNC_SUPPORT_SET_VF_MAC_VLAN(adapter->hwdev)) {
		nicif_err(adapter, drv, netdev,
			  "Current function don't support to set vf mac\n");
		return -EOPNOTSUPP;
	}

	sriov_info = hinic_get_sriov_info_by_pcidev(adapter->pdev);
	if (!is_valid_ether_addr(mac) ||
	    vf >= sriov_info->num_vfs)
		return -EINVAL;

	err = hinic_set_vf_mac(sriov_info->hwdev, OS_VF_ID_TO_HW(vf), mac);
	if (err)
		return err;

	nic_info(&sriov_info->pdev->dev, "Setting MAC %pM on VF %d\n", mac, vf);
	nic_info(&sriov_info->pdev->dev, "Reload the VF driver to make this change effective.");

	return 0;
}

#ifdef IFLA_VF_MAX
static int set_hw_vf_vlan(struct hinic_sriov_info *sriov_info,
			  u16 cur_vlanprio, int vf, u16 vlan, u8 qos)
{
	int err = 0;
	u16 old_vlan = cur_vlanprio & VLAN_VID_MASK;

	if (vlan || qos) {
		if (cur_vlanprio) {
			err = hinic_kill_vf_vlan(sriov_info->hwdev,
						 OS_VF_ID_TO_HW(vf));
			if (err) {
				nic_err(&sriov_info->pdev->dev, "Failed to delete vf %d old vlan %d\n",
					vf, old_vlan);
				return err;
			}
		}
		err = hinic_add_vf_vlan(sriov_info->hwdev,
					OS_VF_ID_TO_HW(vf), vlan, qos);
		if (err) {
			nic_err(&sriov_info->pdev->dev, "Failed to add vf %d new vlan %d\n",
				vf, vlan);
			return err;
		}
	} else {
		err = hinic_kill_vf_vlan(sriov_info->hwdev, OS_VF_ID_TO_HW(vf));
		if (err) {
			nic_err(&sriov_info->pdev->dev, "Failed to delete vf %d vlan %d\n",
				vf, old_vlan);
			return err;
		}
	}

	return hinic_update_mac_vlan(sriov_info->hwdev, old_vlan, vlan,
					    OS_VF_ID_TO_HW(vf));
}

#ifdef IFLA_VF_VLAN_INFO_MAX
int hinic_ndo_set_vf_vlan(struct net_device *netdev, int vf, u16 vlan, u8 qos,
			  __be16 vlan_proto)
#else
int hinic_ndo_set_vf_vlan(struct net_device *netdev, int vf, u16 vlan, u8 qos)
#endif
{
	struct hinic_nic_dev *adapter = netdev_priv(netdev);
	struct hinic_sriov_info *sriov_info;
	u16 vlanprio, cur_vlanprio;

	if (!FUNC_SUPPORT_SET_VF_MAC_VLAN(adapter->hwdev)) {
		nicif_err(adapter, drv, netdev,
			  "Current function don't support to set vf vlan\n");
		return -EOPNOTSUPP;
	}

	sriov_info = hinic_get_sriov_info_by_pcidev(adapter->pdev);
	if (vf >= sriov_info->num_vfs || vlan > 4095 || qos > 7)
		return -EINVAL;
#ifdef IFLA_VF_VLAN_INFO_MAX
	if (vlan_proto != htons(ETH_P_8021Q))
		return -EPROTONOSUPPORT;
#endif
	vlanprio = vlan | qos << HINIC_VLAN_PRIORITY_SHIFT;
	cur_vlanprio = hinic_vf_info_vlanprio(sriov_info->hwdev,
					      OS_VF_ID_TO_HW(vf));
	/* duplicate request, so just return success */
	if (vlanprio == cur_vlanprio)
		return 0;

	return set_hw_vf_vlan(sriov_info, cur_vlanprio, vf, vlan, qos);
}
#endif

#ifdef HAVE_VF_SPOOFCHK_CONFIGURE
int hinic_ndo_set_vf_spoofchk(struct net_device *netdev, int vf, bool setting)
{
	struct hinic_nic_dev *adapter = netdev_priv(netdev);
	struct hinic_sriov_info *sriov_info;
	int err = 0;
	bool cur_spoofchk;

	sriov_info = hinic_get_sriov_info_by_pcidev(adapter->pdev);
	if (vf >= sriov_info->num_vfs)
		return -EINVAL;

	cur_spoofchk = hinic_vf_info_spoofchk(sriov_info->hwdev,
					      OS_VF_ID_TO_HW(vf));
	/* same request, so just return success */
	if ((setting && cur_spoofchk) || (!setting && !cur_spoofchk))
		return 0;

	err = hinic_set_vf_spoofchk(sriov_info->hwdev,
				    OS_VF_ID_TO_HW(vf), setting);

	if (!err) {
		nicif_info(adapter, drv, netdev, "Set VF %d spoofchk %s succeed\n",
			   vf, setting ? "on" : "off");
	} else if (err == HINIC_MGMT_CMD_UNSUPPORTED) {
		nicif_err(adapter, drv, netdev,
			  "Current firmware doesn't support to set vf spoofchk, need to upgrade latest firmware version\n");
		err = -EOPNOTSUPP;
	}

	return err;
}
#endif

#ifdef HAVE_NDO_SET_VF_TRUST
int hinic_ndo_set_vf_trust(struct net_device *netdev, int vf, bool setting)
{
	struct hinic_nic_dev *adapter = netdev_priv(netdev);
	struct hinic_sriov_info *sriov_info;
	int err = 0;
	bool cur_trust;

	sriov_info = hinic_get_sriov_info_by_pcidev(adapter->pdev);
	if (vf >= sriov_info->num_vfs)
		return -EINVAL;

	cur_trust = hinic_vf_info_trust(sriov_info->hwdev,
					OS_VF_ID_TO_HW(vf));
	/* same request, so just return success */
	if ((setting && cur_trust) || (!setting && !cur_trust))
		return 0;

	err = hinic_set_vf_trust(sriov_info->hwdev,
				 OS_VF_ID_TO_HW(vf), setting);
	if (!err)
		nicif_info(adapter, drv, netdev, "Set VF %d trusted %s succeed\n",
			   vf, setting ? "on" : "off");
	else
		nicif_err(adapter, drv, netdev, "Failed set VF %d trusted %s\n",
			  vf, setting ? "on" : "off");

	return err;
}
#endif

int hinic_ndo_get_vf_config(struct net_device *netdev,
			    int vf, struct ifla_vf_info *ivi)
{
	struct hinic_nic_dev *adapter = netdev_priv(netdev);
	struct hinic_sriov_info *sriov_info;

	sriov_info = hinic_get_sriov_info_by_pcidev(adapter->pdev);
	if (vf >= sriov_info->num_vfs)
		return -EINVAL;

	hinic_get_vf_config(sriov_info->hwdev, OS_VF_ID_TO_HW(vf), ivi);

	return 0;
}

/**
 * hinic_ndo_set_vf_link_state
 * @netdev: network interface device structure
 * @vf_id: VF identifier
 * @link: required link state
 * Return: 0 - success, negative - failure
 * Set the link state of a specified VF, regardless of physical link state
 */
int hinic_ndo_set_vf_link_state(struct net_device *netdev, int vf_id, int link)
{
	struct hinic_nic_dev *adapter = netdev_priv(netdev);
	struct hinic_sriov_info *sriov_info;
	static const char * const vf_link[] = {"auto", "enable", "disable"};
	int err;

	if (FUNC_FORCE_LINK_UP(adapter->hwdev)) {
		nicif_err(adapter, drv, netdev,
			  "Current function don't support to set vf link state\n");
		return -EOPNOTSUPP;
	}

	sriov_info = hinic_get_sriov_info_by_pcidev(adapter->pdev);
	/* validate the request */
	if (vf_id >= sriov_info->num_vfs) {
		nicif_err(adapter, drv, netdev,
			  "Invalid VF Identifier %d\n", vf_id);
		return -EINVAL;
	}

	err = hinic_set_vf_link_state(sriov_info->hwdev,
				      OS_VF_ID_TO_HW(vf_id), link);

	if (!err)
		nicif_info(adapter, drv, netdev, "Set VF %d link state: %s\n",
			   vf_id, vf_link[link]);

	return err;
}

#define HINIC_TX_RATE_TABLE_FULL	12

#ifdef HAVE_NDO_SET_VF_MIN_MAX_TX_RATE
int hinic_ndo_set_vf_bw(struct net_device *netdev,
			int vf, int min_tx_rate, int max_tx_rate)
#else
int hinic_ndo_set_vf_bw(struct net_device *netdev, int vf, int max_tx_rate)
#endif /* HAVE_NDO_SET_VF_MIN_MAX_TX_RATE */
{
	struct hinic_nic_dev *adapter = netdev_priv(netdev);
	struct nic_port_info port_info = {0};
	struct hinic_sriov_info *sriov_info;
#ifndef HAVE_NDO_SET_VF_MIN_MAX_TX_RATE
	int min_tx_rate = 0;
#endif
	u8 link_status = 0;
	u32 speeds[] = {SPEED_10, SPEED_100, SPEED_1000, SPEED_10000,
			SPEED_25000, SPEED_40000, SPEED_100000};
	int err = 0;

	if (!FUNC_SUPPORT_RATE_LIMIT(adapter->hwdev)) {
		nicif_err(adapter, drv, netdev,
			  "Current function don't support to set vf rate limit\n");
		return -EOPNOTSUPP;
	}

	sriov_info = hinic_get_sriov_info_by_pcidev(adapter->pdev);

	/* verify VF is active */
	if (vf >= sriov_info->num_vfs) {
		nicif_err(adapter, drv, netdev, "VF number must be less than %d\n",
			  sriov_info->num_vfs);
		return -EINVAL;
	}

	if (max_tx_rate < min_tx_rate) {
		nicif_err(adapter, drv, netdev, "Invalid rate, max rate %d must greater than min rate %d\n",
			  max_tx_rate, min_tx_rate);
		return -EINVAL;
	}

	err = hinic_get_link_state(adapter->hwdev, &link_status);
	if (err) {
		nicif_err(adapter, drv, netdev,
			  "Get link status failed when set vf tx rate\n");
		return -EIO;
	}

	if (!link_status) {
		nicif_err(adapter, drv, netdev,
			  "Link status must be up when set vf tx rate\n");
		return -EINVAL;
	}

	err = hinic_get_port_info(adapter->hwdev, &port_info);
	if (err || port_info.speed > LINK_SPEED_100GB)
		return -EIO;

	/* rate limit cannot be less than 0 and greater than link speed */
	if (max_tx_rate < 0 || max_tx_rate > speeds[port_info.speed]) {
		nicif_err(adapter, drv, netdev, "Set vf max tx rate must be in [0 - %d]\n",
			  speeds[port_info.speed]);
		return -EINVAL;
	}

	err = hinic_set_vf_tx_rate(adapter->hwdev, OS_VF_ID_TO_HW(vf),
				   max_tx_rate, min_tx_rate);
	if (err) {
		nicif_err(adapter, drv, netdev,
			  "Unable to set VF %d max rate %d min rate %d%s\n",
			  vf, max_tx_rate, min_tx_rate,
			  err == HINIC_TX_RATE_TABLE_FULL ?
			  ", tx rate profile is full" : "");
		return -EIO;
	}

#ifdef HAVE_NDO_SET_VF_MIN_MAX_TX_RATE
	nicif_info(adapter, drv, netdev,
		   "Set VF %d max tx rate %d min tx rate %d successfully\n",
		   vf, max_tx_rate, min_tx_rate);
#else
	nicif_info(adapter, drv, netdev,
		   "Set VF %d tx rate %d successfully\n",
		   vf, max_tx_rate);
#endif

	return 0;
}
