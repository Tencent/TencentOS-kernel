/* SPDX-License-Identifier: GPL-2.0-only */
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

#ifndef HINIC_SRIOV_H
#define HINIC_SRIOV_H

#if !(defined(HAVE_SRIOV_CONFIGURE) || defined(HAVE_RHEL6_SRIOV_CONFIGURE))
ssize_t hinic_sriov_totalvfs_show(struct device *dev,
				  struct device_attribute *attr, char *buf);
ssize_t hinic_sriov_numvfs_show(struct device *dev,
				struct device_attribute *attr, char *buf);
ssize_t hinic_sriov_numvfs_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count);
#endif /* !(HAVE_SRIOV_CONFIGURE || HAVE_RHEL6_SRIOV_CONFIGURE) */

enum hinic_sriov_state {
	HINIC_SRIOV_DISABLE,
	HINIC_SRIOV_ENABLE,
	HINIC_FUNC_REMOVE,
};

struct hinic_sriov_info {
	struct pci_dev *pdev;
	void *hwdev;
	bool sriov_enabled;
	unsigned int num_vfs;
	unsigned long state;
};

int hinic_pci_sriov_disable(struct pci_dev *dev);
int hinic_pci_sriov_enable(struct pci_dev *dev, int num_vfs);
int hinic_pci_sriov_configure(struct pci_dev *dev, int num_vfs);
#ifdef IFLA_VF_MAX
int hinic_ndo_set_vf_mac(struct net_device *netdev, int vf, u8 *mac);
#ifdef IFLA_VF_VLAN_INFO_MAX
int hinic_ndo_set_vf_vlan(struct net_device *netdev, int vf, u16 vlan, u8 qos,
			  __be16 vlan_proto);
#else
int hinic_ndo_set_vf_vlan(struct net_device *netdev, int vf, u16 vlan, u8 qos);
#endif

int hinic_ndo_get_vf_config(struct net_device *netdev, int vf,
			    struct ifla_vf_info *ivi);

#ifdef HAVE_VF_SPOOFCHK_CONFIGURE
int hinic_ndo_set_vf_spoofchk(struct net_device *netdev, int vf, bool setting);
#endif

#ifdef HAVE_NDO_SET_VF_TRUST
int hinic_ndo_set_vf_trust(struct net_device *netdev, int vf, bool setting);
#endif

int hinic_ndo_set_vf_link_state(struct net_device *netdev, int vf_id, int link);

#ifdef HAVE_NDO_SET_VF_MIN_MAX_TX_RATE
int hinic_ndo_set_vf_bw(struct net_device *netdev,
			int vf, int min_tx_rate, int max_tx_rate);
#else
int hinic_ndo_set_vf_bw(struct net_device *netdev, int vf, int max_tx_rate);
#endif /* HAVE_NDO_SET_VF_MIN_MAX_TX_RATE */
#endif
#endif
