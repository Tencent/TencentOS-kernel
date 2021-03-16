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

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/module.h>

#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_hwdev.h"
#include "hinic_hw_mgmt.h"
#include "hinic_mbox.h"
#include "hinic_nic_io.h"
#include "hinic_nic_cfg.h"
#include "hinic_nic.h"
#include "hinic_mgmt_interface.h"
#include "hinic_hwif.h"

static unsigned char set_vf_link_state;
module_param(set_vf_link_state, byte, 0444);
MODULE_PARM_DESC(set_vf_link_state, "Set vf link state, 0 represents link auto, 1 represents link always up, 2 represents link always down. - default is 0.");

#define l2nic_msg_to_mgmt_sync(hwdev, cmd, buf_in, in_size, buf_out, out_size)\
	hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_L2NIC, cmd, \
			       buf_in, in_size, \
			       buf_out, out_size, 0)

#define l2nic_msg_to_mgmt_async(hwdev, cmd, buf_in, in_size)	\
	hinic_msg_to_mgmt_async(hwdev, HINIC_MOD_L2NIC, cmd, buf_in, in_size)

#define CPATH_FUNC_ID_VALID_LIMIT	2
#define CHECK_IPSU_15BIT	0X8000

static int hinic_set_rx_lro_timer(void *hwdev, u32 timer_value);

static bool check_func_table(struct hinic_hwdev *hwdev, u16 func_idx,
			     void *buf_in, u16 in_size)
{
	struct hinic_function_table *function_table;

	if (!hinic_mbox_check_func_id_8B(hwdev, func_idx, buf_in, in_size))
		return false;

	function_table = (struct hinic_function_table *)buf_in;

	if (!function_table->rx_wqe_buf_size)
		return false;

	return true;
}

struct vf_cmd_check_handle nic_cmd_support_vf[] = {
	{HINIC_PORT_CMD_VF_REGISTER, NULL},
	{HINIC_PORT_CMD_VF_UNREGISTER, NULL},

	{HINIC_PORT_CMD_CHANGE_MTU, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_ADD_VLAN, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_DEL_VLAN, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_SET_MAC, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_MAC, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_DEL_MAC, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_SET_RX_MODE, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_GET_PAUSE_INFO, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_GET_LINK_STATE, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_LRO, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_RX_CSUM, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_RX_VLAN_OFFLOAD, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_GET_VPORT_STAT, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_CLEAN_VPORT_STAT, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_GET_RSS_TEMPLATE_INDIR_TBL,
	 hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_RSS_TEMPLATE_INDIR_TBL,
	 hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_RSS_TEMPLATE_TBL, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_RSS_TEMPLATE_TBL, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_RSS_HASH_ENGINE, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_RSS_HASH_ENGINE, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_RSS_CTX_TBL, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_RSS_CTX_TBL, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_RSS_TEMP_MGR, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_RSS_CFG, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_INIT_FUNC, check_func_table},
	{HINIC_PORT_CMD_SET_LLI_PRI, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_GET_MGMT_VERSION, NULL},
	{HINIC_PORT_CMD_GET_BOOT_VERSION, NULL},
	{HINIC_PORT_CMD_GET_MICROCODE_VERSION, NULL},

	{HINIC_PORT_CMD_GET_VPORT_ENABLE, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_VPORT_ENABLE, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_GET_LRO, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_GET_GLOBAL_QPN, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_SET_TSO, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_RQ_IQ_MAP, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_LINK_STATUS_REPORT, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_UPDATE_MAC, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_GET_PORT_INFO, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_SET_IPSU_MAC, hinic_mbox_check_func_id_10B},
	{HINIC_PORT_CMD_GET_IPSU_MAC, hinic_mbox_check_func_id_10B},

	{HINIC_PORT_CMD_GET_LINK_MODE, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_CLEAR_SQ_RES, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_SET_SUPER_CQE, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_GET_VF_COS, NULL},
	{HINIC_PORT_CMD_FORCE_PKT_DROP, NULL},
	{HINIC_PORT_CMD_SET_VHD_CFG, hinic_mbox_check_func_id_8B},

	{HINIC_PORT_CMD_SET_VLAN_FILTER, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_Q_FILTER, hinic_mbox_check_func_id_8B},
	{HINIC_PORT_CMD_TCAM_FILTER, NULL},
};

int hinic_init_function_table(void *hwdev, u16 rx_buf_sz)
{
	struct hinic_function_table function_table = {0};
	u16 out_size = sizeof(function_table);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &function_table.func_id);
	if (err)
		return err;

	function_table.version = HINIC_CMD_VER_FUNC_ID;
	function_table.mtu = 0x3FFF;	/* default, max mtu */
	function_table.rx_wqe_buf_size = rx_buf_sz;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_L2NIC,
				     HINIC_PORT_CMD_INIT_FUNC,
				     &function_table, sizeof(function_table),
				     &function_table, &out_size, 0);
	if (err || function_table.status || !out_size) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to init func table, err: %d, status: 0x%x, out size: 0x%x\n",
			err, function_table.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int hinic_get_base_qpn(void *hwdev, u16 *global_qpn)
{
	struct hinic_cmd_qpn cmd_qpn = {0};
	u16 out_size = sizeof(cmd_qpn);
	int err;

	if (!hwdev || !global_qpn)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &cmd_qpn.func_id);
	if (err)
		return err;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_L2NIC,
				     HINIC_PORT_CMD_GET_GLOBAL_QPN,
				     &cmd_qpn, sizeof(cmd_qpn), &cmd_qpn,
				     &out_size, 0);
	if (err || !out_size || cmd_qpn.status) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to get base qpn, err: %d, status: 0x%x, out size: 0x%x\n",
			err, cmd_qpn.status, out_size);
		return -EINVAL;
	}

	*global_qpn = cmd_qpn.base_qpn;

	return 0;
}

int hinic_get_fw_support_func(void *hwdev)
{
	struct fw_support_func support_flag = {0};
	struct hinic_hwdev *dev = hwdev;
	u16 out_size = sizeof(support_flag);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_L2NIC,
				     HINIC_PORT_CMD_GET_FW_SUPPORT_FLAG,
				     &support_flag, sizeof(support_flag),
				     &support_flag, &out_size, 0);
	if (support_flag.status == HINIC_MGMT_CMD_UNSUPPORTED) {
		nic_info(dev->dev_hdl, "Current firmware doesn't support to get mac reuse flag\n");
		support_flag.flag = 0;
	} else if (support_flag.status || err || !out_size) {
		nic_err(dev->dev_hdl, "Failed to get mac reuse flag, err: %d, status: 0x%x, out size: 0x%x\n",
			err, support_flag.status, out_size);
		return -EFAULT;
	}

	dev->fw_support_func_flag = support_flag.flag;

	return 0;
}

#define HINIC_ADD_VLAN_IN_MAC	0x8000
#define HINIC_VLAN_ID_MASK	0x7FFF

int hinic_set_mac(void *hwdev, const u8 *mac_addr, u16 vlan_id, u16 func_id)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_port_mac_set mac_info = {0};
	u16 out_size = sizeof(mac_info);
	int err;

	if (!hwdev || !mac_addr)
		return -EINVAL;

	if ((vlan_id & HINIC_VLAN_ID_MASK) >= VLAN_N_VID) {
		nic_err(nic_hwdev->dev_hdl, "Invalid VLAN number: %d\n",
			(vlan_id & HINIC_VLAN_ID_MASK));
		return -EINVAL;
	}

	mac_info.func_id = func_id;
	mac_info.vlan_id = vlan_id;
	memcpy(mac_info.mac, mac_addr, ETH_ALEN);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_MAC, &mac_info,
				     sizeof(mac_info), &mac_info, &out_size);
	if (err || !out_size ||
	    (mac_info.status && mac_info.status != HINIC_MGMT_STATUS_EXIST &&
	    mac_info.status != HINIC_PF_SET_VF_ALREADY) ||
	    (mac_info.vlan_id & CHECK_IPSU_15BIT &&
	    mac_info.status == HINIC_MGMT_STATUS_EXIST)) {
		nic_err(nic_hwdev->dev_hdl,
			"Failed to update MAC, err: %d, status: 0x%x, out size: 0x%x\n",
			err, mac_info.status, out_size);
		return -EINVAL;
	}

	if (mac_info.status == HINIC_PF_SET_VF_ALREADY) {
		nic_warn(nic_hwdev->dev_hdl, "PF has already set VF mac, Ignore set operation\n");
		return HINIC_PF_SET_VF_ALREADY;
	}

	if (mac_info.status == HINIC_MGMT_STATUS_EXIST) {
		nic_warn(nic_hwdev->dev_hdl, "MAC is repeated. Ignore update operation\n");
		return 0;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_set_mac);

int hinic_del_mac(void *hwdev, const u8 *mac_addr, u16 vlan_id, u16 func_id)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_port_mac_set mac_info = {0};
	u16 out_size = sizeof(mac_info);
	int err;

	if (!hwdev || !mac_addr)
		return -EINVAL;

	if ((vlan_id & HINIC_VLAN_ID_MASK) >= VLAN_N_VID) {
		nic_err(nic_hwdev->dev_hdl, "Invalid VLAN number: %d\n",
			(vlan_id & HINIC_VLAN_ID_MASK));
		return -EINVAL;
	}

	mac_info.func_id = func_id;
	mac_info.vlan_id = vlan_id;
	memcpy(mac_info.mac, mac_addr, ETH_ALEN);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_DEL_MAC, &mac_info,
				     sizeof(mac_info), &mac_info, &out_size);
	if (err || !out_size ||
	    (mac_info.status && mac_info.status != HINIC_PF_SET_VF_ALREADY)) {
		nic_err(nic_hwdev->dev_hdl,
			"Failed to delete MAC, err: %d, status: 0x%x, out size: 0x%x\n",
			err, mac_info.status, out_size);
		return -EINVAL;
	}
	if (mac_info.status == HINIC_PF_SET_VF_ALREADY) {
		nic_warn(nic_hwdev->dev_hdl, "PF has already set VF mac, Ignore delete operation.\n");
		return HINIC_PF_SET_VF_ALREADY;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_del_mac);

int hinic_update_mac(void *hwdev, u8 *old_mac, u8 *new_mac, u16 vlan_id,
		     u16 func_id)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_port_mac_update mac_info = {0};
	u16 out_size = sizeof(mac_info);
	int err;

	if (!hwdev || !old_mac || !new_mac)
		return -EINVAL;

	if ((vlan_id & HINIC_VLAN_ID_MASK) >= VLAN_N_VID) {
		nic_err(nic_hwdev->dev_hdl, "Invalid VLAN number: %d\n",
			(vlan_id & HINIC_VLAN_ID_MASK));
		return -EINVAL;
	}

	mac_info.func_id = func_id;
	mac_info.vlan_id = vlan_id;
	memcpy(mac_info.old_mac, old_mac, ETH_ALEN);
	memcpy(mac_info.new_mac, new_mac, ETH_ALEN);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_UPDATE_MAC,
				     &mac_info, sizeof(mac_info),
				     &mac_info, &out_size);
	if (err || !out_size ||
	    (mac_info.status && mac_info.status != HINIC_MGMT_STATUS_EXIST &&
	    mac_info.status != HINIC_PF_SET_VF_ALREADY) ||
	    (mac_info.vlan_id & CHECK_IPSU_15BIT &&
	    mac_info.status == HINIC_MGMT_STATUS_EXIST)) {
		nic_err(nic_hwdev->dev_hdl,
			"Failed to update MAC, err: %d, status: 0x%x, out size: 0x%x\n",
			err, mac_info.status, out_size);
		return -EINVAL;
	}

	if (mac_info.status == HINIC_PF_SET_VF_ALREADY) {
		nic_warn(nic_hwdev->dev_hdl, "PF has already set VF MAC. Ignore update operation\n");
		return HINIC_PF_SET_VF_ALREADY;
	}

	if (mac_info.status == HINIC_MGMT_STATUS_EXIST) {
		nic_warn(nic_hwdev->dev_hdl, "MAC is repeated. Ignore update operation\n");
		return 0;
	}

	return 0;
}

int hinic_update_mac_vlan(void *hwdev, u16 old_vlan, u16 new_vlan, int vf_id)
{
	struct hinic_hwdev *dev = hwdev;
	struct vf_data_storage *vf_info;
	u16 func_id, vlan_id;
	int err;

	if (!hwdev || old_vlan >= VLAN_N_VID || new_vlan >= VLAN_N_VID)
		return -EINVAL;

	vf_info = dev->nic_io->vf_infos + HW_VF_ID_TO_OS(vf_id);
	if (!vf_info->pf_set_mac)
		return 0;

	if (!FW_SUPPORT_MAC_REUSE_FUNC(dev)) {
		nic_info(dev->dev_hdl, "Current firmware doesn't support mac reuse\n");
		return 0;
	}

	func_id = hinic_glb_pf_vf_offset(dev) + (u16)vf_id;
	vlan_id = old_vlan;
	if (vlan_id)
		vlan_id |= HINIC_ADD_VLAN_IN_MAC;
	err = hinic_del_mac(dev, vf_info->vf_mac_addr, vlan_id,
			    func_id);
	if (err) {
		nic_err(dev->dev_hdl, "Failed to delete VF %d MAC %pM vlan %d\n",
			HW_VF_ID_TO_OS(vf_id), vf_info->vf_mac_addr, vlan_id);
		return err;
	}

	vlan_id = new_vlan;
	if (vlan_id)
		vlan_id |= HINIC_ADD_VLAN_IN_MAC;
	err = hinic_set_mac(dev, vf_info->vf_mac_addr, vlan_id,
			    func_id);
	if (err) {
		nic_err(dev->dev_hdl, "Failed to add VF %d MAC %pM vlan %d\n",
			HW_VF_ID_TO_OS(vf_id), vf_info->vf_mac_addr, vlan_id);
		goto out;
	}

	return 0;

out:
	vlan_id = old_vlan;
	if (vlan_id)
		vlan_id |= HINIC_ADD_VLAN_IN_MAC;
	hinic_set_mac(dev, vf_info->vf_mac_addr, vlan_id,
		      func_id);

	return err;
}

int hinic_get_default_mac(void *hwdev, u8 *mac_addr)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_port_mac_set mac_info = {0};
	u16 out_size = sizeof(mac_info);
	int err;

	if (!hwdev || !mac_addr)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &mac_info.func_id);
	if (err)
		return err;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_MAC,
				     &mac_info, sizeof(mac_info),
				     &mac_info, &out_size);
	if (err || !out_size || mac_info.status) {
		nic_err(nic_hwdev->dev_hdl,
			"Failed to get mac, err: %d, status: 0x%x, out size: 0x%x\n",
			err, mac_info.status, out_size);
		return -EINVAL;
	}

	memcpy(mac_addr, mac_info.mac, ETH_ALEN);

	return 0;
}

int hinic_set_port_mtu(void *hwdev, u32 new_mtu)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_mtu mtu_info = {0};
	u16 out_size = sizeof(mtu_info);
	int err;

	if (!hwdev)
		return -EINVAL;

	if (new_mtu < HINIC_MIN_MTU_SIZE) {
		nic_err(nic_hwdev->dev_hdl,
			"Invalid mtu size, mtu size < 256bytes");
		return -EINVAL;
	}

	if (new_mtu > HINIC_MAX_JUMBO_FRAME_SIZE) {
		nic_err(nic_hwdev->dev_hdl, "Invalid mtu size, mtu size > 9600bytes");
		return -EINVAL;
	}

	err = hinic_global_func_id_get(hwdev, &mtu_info.func_id);
	if (err)
		return err;

	mtu_info.mtu = new_mtu;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_CHANGE_MTU,
				     &mtu_info, sizeof(mtu_info),
				     &mtu_info, &out_size);
	if (err || !out_size || mtu_info.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set mtu, err: %d, status: 0x%x, out size: 0x%x\n",
			err, mtu_info.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_hiovs_set_cpath_vlan(void *hwdev, u16 vlan_id, u16 pf_id)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct cmd_cpath_vlan cpath_vlan_info = {0};
	u16 out_size = sizeof(cpath_vlan_info);
	int err;

	if (!hwdev)
		return -EINVAL;

	cpath_vlan_info.pf_id = pf_id;
	cpath_vlan_info.vlan_id = vlan_id;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_OVS, OVS_SET_CPATH_VLAN,
				     &cpath_vlan_info, sizeof(cpath_vlan_info),
				     &cpath_vlan_info, &out_size, 0);

	if (err || !out_size || cpath_vlan_info.status) {
		sdk_err(nic_hwdev->dev_hdl, "Failed to set cpath vlan, err: %d, status: 0x%x, out_size: 0x%0x\n",
			err, cpath_vlan_info.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int hinic_hiovs_del_cpath_vlan(void *hwdev, u16 vlan_id, u16 pf_id)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct cmd_cpath_vlan cpath_vlan_info = {0};
	u16 out_size = sizeof(cpath_vlan_info);
	int err;

	if (!hwdev)
		return -EINVAL;

	cpath_vlan_info.pf_id = pf_id;
	cpath_vlan_info.vlan_id = vlan_id;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_OVS, OVS_DEL_CPATH_VLAN,
				     &cpath_vlan_info, sizeof(cpath_vlan_info),
				     &cpath_vlan_info, &out_size, 0);

	if (err || !out_size || cpath_vlan_info.status) {
		sdk_err(nic_hwdev->dev_hdl, "Failed to delte cpath vlan, err: %d, status: 0x%x, out_size: 0x%0x\n",
			err, cpath_vlan_info.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int hinic_enable_netq(void *hwdev, u8 en)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_netq_cfg_msg netq_cfg = {0};
	u16 out_size = sizeof(netq_cfg);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &netq_cfg.func_id);
	if (err)
		return err;

	netq_cfg.netq_en = en;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_NETQ,
				     &netq_cfg, sizeof(netq_cfg),
				     &netq_cfg, &out_size);
	if (netq_cfg.status == HINIC_MGMT_CMD_UNSUPPORTED) {
		err = HINIC_MGMT_CMD_UNSUPPORTED;
		nic_warn(nic_hwdev->dev_hdl, "Not support enable netq\n");
	} else if (err || !out_size || netq_cfg.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to enable netq, err: %d, status: 0x%x, out size: 0x%x\n",
			err, netq_cfg.status, out_size);
	}

	return err;
}

int hinic_add_hw_rqfilter(void *hwdev, struct hinic_rq_filter_info *filter_info)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_rq_filter_msg filter_msg = {0};
	u16 out_size = sizeof(filter_msg);
	int err;

	if (!hwdev || !filter_info)
		return -EINVAL;

	switch (filter_info->filter_type) {
	case HINIC_RQ_FILTER_TYPE_MAC_ONLY:
		memcpy(filter_msg.mac, filter_info->mac, ETH_ALEN);
		break;
	case HINIC_RQ_FILTER_TYPE_VXLAN:
		memcpy(filter_msg.mac, filter_info->mac, ETH_ALEN);
		memcpy(filter_msg.vxlan.inner_mac,
		       filter_info->vxlan.inner_mac, ETH_ALEN);
		filter_msg.vxlan.vni = filter_info->vxlan.vni;
		break;
	default:
		nic_warn(nic_hwdev->dev_hdl, "No support filter type: 0x%x\n",
			 filter_info->filter_type);
		return -EINVAL;
	}

	err = hinic_global_func_id_get(hwdev, &filter_msg.func_id);
	if (err)
		return err;

	filter_msg.filter_type = filter_info->filter_type;
	filter_msg.qid = filter_info->qid;
	filter_msg.qflag = filter_info->qflag;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_ADD_RQ_FILTER,
				     &filter_msg, sizeof(filter_msg),
				     &filter_msg, &out_size);
	if (filter_msg.status == HINIC_MGMT_CMD_UNSUPPORTED) {
		err = HINIC_MGMT_CMD_UNSUPPORTED;
		nic_warn(nic_hwdev->dev_hdl, "Not support add rxq filter\n");
	} else if (err || !out_size || filter_msg.status) {
		nic_err(nic_hwdev->dev_hdl,
			"Failed to add RX qfilter, err: %d, status: 0x%x, out size: 0x%x\n",
			err, filter_msg.status, out_size);
		return -EINVAL;
	}

	return err;
}

int hinic_del_hw_rqfilter(void *hwdev, struct hinic_rq_filter_info *filter_info)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_rq_filter_msg filter_msg = {0};
	u16 out_size = sizeof(filter_msg);
	int err;

	if (!hwdev || !filter_info)
		return -EINVAL;

	switch (filter_info->filter_type) {
	case HINIC_RQ_FILTER_TYPE_MAC_ONLY:
		memcpy(filter_msg.mac, filter_info->mac, ETH_ALEN);
		break;
	case HINIC_RQ_FILTER_TYPE_VXLAN:
		memcpy(filter_msg.mac, filter_info->mac, ETH_ALEN);
		memcpy(filter_msg.vxlan.inner_mac,
		       filter_info->vxlan.inner_mac, ETH_ALEN);
		filter_msg.vxlan.vni = filter_info->vxlan.vni;
		break;
	default:
		nic_warn(nic_hwdev->dev_hdl, "No support filter type: 0x%x\n",
			 filter_info->filter_type);
		return -EINVAL;
	}

	err = hinic_global_func_id_get(hwdev, &filter_msg.func_id);
	if (err)
		return err;

	filter_msg.filter_type = filter_info->filter_type;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_DEL_RQ_FILTER,
				     &filter_msg, sizeof(filter_msg),
					&filter_msg, &out_size);
	if (filter_msg.status == HINIC_MGMT_CMD_UNSUPPORTED) {
		err = HINIC_MGMT_CMD_UNSUPPORTED;
		nic_warn(nic_hwdev->dev_hdl, "Not support del rxq filter\n");
	} else if (err || !out_size || filter_msg.status) {
		nic_err(nic_hwdev->dev_hdl,
			"Failed to delte RX qfilter, err: %d, status: 0x%x, out size: 0x%x\n",
			err, filter_msg.status, out_size);
		return -EINVAL;
	}

	return err;
}

int hinic_add_vlan(void *hwdev, u16 vlan_id, u16 func_id)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_vlan_config vlan_info = {0};
	u16 out_size = sizeof(vlan_info);
	int err;

	if (!hwdev)
		return -EINVAL;

	vlan_info.func_id = func_id;
	vlan_info.vlan_id = vlan_id;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_ADD_VLAN,
				     &vlan_info, sizeof(vlan_info),
				     &vlan_info, &out_size);
	if (err || !out_size || vlan_info.status) {
		nic_err(nic_hwdev->dev_hdl,
			"Failed to add vlan, err: %d, status: 0x%x, out size: 0x%x\n",
			err, vlan_info.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_del_vlan(void *hwdev, u16 vlan_id, u16 func_id)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_vlan_config vlan_info = {0};
	u16 out_size = sizeof(vlan_info);
	int err;

	if (!hwdev)
		return -EINVAL;

	vlan_info.func_id = func_id;
	vlan_info.vlan_id = vlan_id;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_DEL_VLAN,
				     &vlan_info, sizeof(vlan_info),
					&vlan_info, &out_size);
	if (err || !out_size || vlan_info.status) {
		nic_err(nic_hwdev->dev_hdl,
			"Failed to delte vlan, err: %d, status: 0x%x, out size: 0x%x\n",
			err, vlan_info.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_set_vlan_fliter(void *hwdev, u32 vlan_filter_ctrl)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_vlan_filter vlan_filter = {0};
	u16 out_size = sizeof(vlan_filter);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &vlan_filter.func_id);
	if (err)
		return err;
	vlan_filter.vlan_filter_ctrl = vlan_filter_ctrl;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_VLAN_FILTER,
				     &vlan_filter, sizeof(vlan_filter),
				     &vlan_filter, &out_size);
	if (vlan_filter.status == HINIC_MGMT_CMD_UNSUPPORTED) {
		err = HINIC_MGMT_CMD_UNSUPPORTED;
	} else if ((err == HINIC_MBOX_VF_CMD_ERROR) &&
		   HINIC_IS_VF(nic_hwdev)) {
		err = HINIC_MGMT_CMD_UNSUPPORTED;
	} else if (err || !out_size || vlan_filter.status) {
		nic_err(nic_hwdev->dev_hdl,
			"Failed to set vlan fliter, err: %d, status: 0x%x, out size: 0x%x\n",
			err, vlan_filter.status, out_size);
		err = -EINVAL;
	}

	return err;
}

int hinic_get_port_info(void *hwdev, struct nic_port_info *port_info)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_port_info port_msg = {0};
	u16 out_size = sizeof(port_msg);
	int err;

	if (!hwdev || !port_info)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &port_msg.func_id);
	if (err)
		return err;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_PORT_INFO,
				     &port_msg, sizeof(port_msg),
				     &port_msg, &out_size);
	if (err || !out_size || port_msg.status) {
		nic_err(nic_hwdev->dev_hdl,
			"Failed to get port info, err: %d, status: 0x%x, out size: 0x%x\n",
			err, port_msg.status, out_size);
		return -EINVAL;
	}

	port_info->autoneg_cap = port_msg.autoneg_cap;
	port_info->autoneg_state = port_msg.autoneg_state;
	port_info->duplex = port_msg.duplex;
	port_info->port_type = port_msg.port_type;
	port_info->speed = port_msg.speed;

	return 0;
}
EXPORT_SYMBOL(hinic_get_port_info);

int hinic_set_autoneg(void *hwdev, bool enable)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_set_autoneg_cmd autoneg = {0};
	u16 out_size = sizeof(autoneg);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &autoneg.func_id);
	if (err)
		return err;

	autoneg.enable = enable;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_AUTONEG,
				     &autoneg, sizeof(autoneg),
				     &autoneg, &out_size);
	if (err || !out_size || autoneg.status) {
		nic_err(dev->dev_hdl, "Failed to %s autoneg, err: %d, status: 0x%x, out size: 0x%x\n",
			enable ? "enable" : "disable", err, autoneg.status,
			out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_force_port_relink(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;
	int err;

	/* Force port link down and link up */
	err = hinic_set_port_link_status(hwdev, false);
	if (err) {
		nic_err(dev->dev_hdl, "Failed to set port link down\n");
		return -EFAULT;
	}

	err = hinic_set_port_link_status(hwdev, true);
	if (err) {
		nic_err(dev->dev_hdl, "Failed to set port link up\n");
		return -EFAULT;
	}

	return 0;
}

int hinic_get_link_mode(void *hwdev, enum hinic_link_mode *supported,
			enum hinic_link_mode *advertised)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_link_mode_cmd link_mode = {0};
	u16 out_size = sizeof(link_mode);
	int err;

	if (!hwdev || !supported || !advertised)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &link_mode.func_id);
	if (err)
		return err;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_LINK_MODE,
				     &link_mode, sizeof(link_mode),
				     &link_mode, &out_size);
	if (err || !out_size || link_mode.status) {
		nic_err(dev->dev_hdl,
			"Failed to get link mode, err: %d, status: 0x%x, out size: 0x%x\n",
			err, link_mode.status, out_size);
		return -EINVAL;
	}

	*supported = link_mode.supported;
	*advertised = link_mode.advertised;

	return 0;
}

int hinic_set_port_link_status(void *hwdev, bool enable)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_set_link_status link_status = {0};
	u16 out_size = sizeof(link_status);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &link_status.func_id);
	if (err)
		return err;

	link_status.enable = enable;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_PORT_LINK_STATUS,
				     &link_status, sizeof(link_status),
				     &link_status, &out_size);
	if (err || !out_size || link_status.status) {
		nic_err(dev->dev_hdl, "Failed to %s port link status, err: %d, status: 0x%x, out size: 0x%x\n",
			enable ? "Enable" : "Disable", err, link_status.status,
			out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_set_speed(void *hwdev, enum nic_speed_level speed)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_speed_cmd speed_info = {0};
	u16 out_size = sizeof(speed_info);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &speed_info.func_id);
	if (err)
		return err;

	speed_info.speed = speed;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_SPEED,
				     &speed_info, sizeof(speed_info),
				     &speed_info, &out_size);
	if (err || !out_size || speed_info.status) {
		nic_err(dev->dev_hdl,
			"Failed to set speed, err: %d, status: 0x%x, out size: 0x%x\n",
			err, speed_info.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_get_speed(void *hwdev, enum nic_speed_level *speed)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_speed_cmd speed_info = {0};
	u16 out_size = sizeof(speed_info);
	int err;

	if (!hwdev || !speed)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &speed_info.func_id);
	if (err)
		return err;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_SPEED,
				     &speed_info, sizeof(speed_info),
				     &speed_info, &out_size);
	if (err || !out_size || speed_info.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to get speed, err: %d, status: 0x%x, out size: 0x%x\n",
			err, speed_info.status, out_size);
		return -EINVAL;
	}

	*speed = speed_info.speed;

	return 0;
}
EXPORT_SYMBOL(hinic_get_speed);

int hinic_get_link_state(void *hwdev, u8 *link_state)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_get_link get_link = {0};
	u16 out_size = sizeof(get_link);
	int err;

	if (!hwdev || !link_state)
		return -EINVAL;

	if (FUNC_FORCE_LINK_UP(hwdev)) {
		*link_state = 1;
		return 0;
	}

	err = hinic_global_func_id_get(hwdev, &get_link.func_id);
	if (err)
		return err;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_LINK_STATE,
				     &get_link, sizeof(get_link),
				     &get_link, &out_size);
	if (err || !out_size || get_link.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to get link state, err: %d, status: 0x%x, out size: 0x%x\n",
			err, get_link.status, out_size);
		return -EINVAL;
	}

	*link_state = get_link.link_status;

	return 0;
}

static int hinic_set_hw_pause_info(void *hwdev,
				   struct nic_pause_config nic_pause)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_pause_config pause_info = {0};
	u16 out_size = sizeof(pause_info);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &pause_info.func_id);
	if (err)
		return err;

	pause_info.auto_neg = nic_pause.auto_neg;
	pause_info.rx_pause = nic_pause.rx_pause;
	pause_info.tx_pause = nic_pause.tx_pause;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_PAUSE_INFO,
				     &pause_info, sizeof(pause_info),
				     &pause_info, &out_size);
	if (err || !out_size || pause_info.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set pause info, err: %d, status: 0x%x, out size: 0x%x\n",
			err, pause_info.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_set_pause_info(void *hwdev, struct nic_pause_config nic_pause)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_nic_cfg *nic_cfg;
	int err;

	if (!hwdev)
		return -EINVAL;

	nic_cfg = &nic_hwdev->nic_io->nic_cfg;

	down(&nic_cfg->cfg_lock);

	err = hinic_set_hw_pause_info(hwdev, nic_pause);
	if (err) {
		up(&nic_cfg->cfg_lock);
		return err;
	}

	nic_cfg->pfc_en = 0;
	nic_cfg->pfc_bitmap = 0;
	nic_cfg->pause_set = true;
	nic_cfg->nic_pause.auto_neg = nic_pause.auto_neg;
	nic_cfg->nic_pause.rx_pause = nic_pause.rx_pause;
	nic_cfg->nic_pause.tx_pause = nic_pause.tx_pause;

	up(&nic_cfg->cfg_lock);

	return 0;
}

int hinic_get_hw_pause_info(void *hwdev, struct nic_pause_config *nic_pause)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_pause_config pause_info = {0};
	u16 out_size = sizeof(pause_info);
	int err;

	if (!hwdev || !nic_pause)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &pause_info.func_id);
	if (err)
		return err;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_PAUSE_INFO,
				     &pause_info, sizeof(pause_info),
				     &pause_info, &out_size);
	if (err || !out_size || pause_info.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to get pause info, err: %d, status: 0x%x, out size: 0x%x\n",
			err, pause_info.status, out_size);
		return -EINVAL;
	}

	nic_pause->auto_neg = pause_info.auto_neg;
	nic_pause->rx_pause = pause_info.rx_pause;
	nic_pause->tx_pause = pause_info.tx_pause;

	return 0;
}

int hinic_get_pause_info(void *hwdev, struct nic_pause_config *nic_pause)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_nic_cfg *nic_cfg = &nic_hwdev->nic_io->nic_cfg;
	int err = 0;

	err = hinic_get_hw_pause_info(hwdev, nic_pause);
	if (err)
		return err;

	if (nic_cfg->pause_set || !nic_pause->auto_neg) {
		nic_pause->rx_pause = nic_cfg->nic_pause.rx_pause;
		nic_pause->tx_pause = nic_cfg->nic_pause.tx_pause;
	}

	return 0;
}

int hinic_set_rx_mode(void *hwdev, u32 enable)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_rx_mode_config rx_mode_cfg = {0};
	u16 out_size = sizeof(rx_mode_cfg);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &rx_mode_cfg.func_id);
	if (err)
		return err;

	rx_mode_cfg.rx_mode = enable;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_RX_MODE,
				     &rx_mode_cfg, sizeof(rx_mode_cfg),
				     &rx_mode_cfg, &out_size);
	if (err || !out_size || rx_mode_cfg.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set rx mode, err: %d, status: 0x%x, out size: 0x%x\n",
			err, rx_mode_cfg.status, out_size);
		return -EINVAL;
	}

	return 0;
}

/* offload feature */
int hinic_set_rx_vlan_offload(void *hwdev, u8 en)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_vlan_offload vlan_cfg = {0};
	u16 out_size = sizeof(vlan_cfg);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &vlan_cfg.func_id);
	if (err)
		return err;

	vlan_cfg.vlan_rx_offload = en;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_RX_VLAN_OFFLOAD,
				     &vlan_cfg, sizeof(vlan_cfg),
				     &vlan_cfg, &out_size);
	if (err || !out_size || vlan_cfg.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set rx vlan offload, err: %d, status: 0x%x, out size: 0x%x\n",
			err, vlan_cfg.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_set_rx_csum_offload(void *hwdev, u32 en)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_checksum_offload rx_csum_cfg = {0};
	u16 out_size = sizeof(rx_csum_cfg);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &rx_csum_cfg.func_id);
	if (err)
		return err;

	rx_csum_cfg.rx_csum_offload = en;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_RX_CSUM,
				     &rx_csum_cfg, sizeof(rx_csum_cfg),
				     &rx_csum_cfg, &out_size);
	if (err || !out_size || rx_csum_cfg.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set rx csum offload, err: %d, status: 0x%x, out size: 0x%x\n",
			err, rx_csum_cfg.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_set_tx_tso(void *hwdev, u8 tso_en)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_tso_config tso_cfg = {0};
	u16 out_size = sizeof(tso_cfg);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &tso_cfg.func_id);
	if (err)
		return err;

	tso_cfg.tso_en = tso_en;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_TSO,
				     &tso_cfg, sizeof(tso_cfg),
				     &tso_cfg, &out_size);
	if (err || !out_size || tso_cfg.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set tso, err: %d, status: 0x%x, out size: 0x%x\n",
			err, tso_cfg.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_set_rx_lro_state(void *hwdev, u8 lro_en, u32 lro_timer, u32 wqe_num)
{
	struct hinic_hwdev *nic_hwdev = hwdev;
	u8 ipv4_en = 0, ipv6_en = 0;
	int err;

	if (!hwdev)
		return -EINVAL;

	ipv4_en = lro_en ? 1 : 0;
	ipv6_en = lro_en ? 1 : 0;

	nic_info(nic_hwdev->dev_hdl, "Set LRO max wqe number to %u\n", wqe_num);

	err = hinic_set_rx_lro(hwdev, ipv4_en, ipv6_en, (u8)wqe_num);
	if (err)
		return err;

	/* we don't set LRO timer for VF */
	if (hinic_func_type(hwdev) == TYPE_VF)
		return 0;

	nic_info(nic_hwdev->dev_hdl, "Set LRO timer to %u\n", lro_timer);

	return hinic_set_rx_lro_timer(hwdev, lro_timer);
}

static int hinic_set_rx_lro_timer(void *hwdev, u32 timer_value)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_lro_timer lro_timer = {0};
	u16 out_size = sizeof(lro_timer);
	int err;

	if (!hwdev)
		return -EINVAL;

	lro_timer.status = 0;
	lro_timer.type = 0;
	lro_timer.enable = 1;
	lro_timer.timer = timer_value;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_LRO_TIMER,
				     &lro_timer, sizeof(lro_timer),
				     &lro_timer, &out_size);
	if (lro_timer.status == 0xFF) {
		/* For this case, we think status (0xFF) is OK */
		lro_timer.status = 0;
		nic_err(nic_hwdev->dev_hdl, "Set lro timer not supported by the current FW version, it will be 1ms default\n");
	}

	if (err || !out_size || lro_timer.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set lro timer, err: %d, status: 0x%x, out size: 0x%x\n",
			err, lro_timer.status, out_size);

		return -EINVAL;
	}

	return 0;
}

int hinic_set_rx_lro(void *hwdev, u8 ipv4_en, u8 ipv6_en, u8 max_wqe_num)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_lro_config lro_cfg = {0};
	u16 out_size = sizeof(lro_cfg);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &lro_cfg.func_id);
	if (err)
		return err;

	lro_cfg.lro_ipv4_en = ipv4_en;
	lro_cfg.lro_ipv6_en = ipv6_en;
	lro_cfg.lro_max_wqe_num = max_wqe_num;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_LRO,
				     &lro_cfg, sizeof(lro_cfg),
				     &lro_cfg, &out_size);
	if (err || !out_size || lro_cfg.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set lro offload, err: %d, status: 0x%x, out size: 0x%x\n",
			err, lro_cfg.status, out_size);
		return -EINVAL;
	}

	return 0;
}

static int hinic_dcb_set_hw_pfc(void *hwdev, u8 pfc_en, u8 pfc_bitmap)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_set_pfc pfc = {0};
	u16 out_size = sizeof(pfc);
	int err;

	err = hinic_global_func_id_get(hwdev, &pfc.func_id);
	if (err)
		return err;

	pfc.pfc_bitmap = pfc_bitmap;
	pfc.pfc_en = pfc_en;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_PFC,
				     &pfc, sizeof(pfc), &pfc, &out_size);
	if (err || pfc.status || !out_size) {
		nic_err(dev->dev_hdl, "Failed to set pfc, err: %d, status: 0x%x, out size: 0x%x\n",
			err, pfc.status, out_size);
		return -EINVAL;
	}

	return 0;
}

/* dcbtool */
int hinic_dcb_set_pfc(void *hwdev, u8 pfc_en, u8 pfc_bitmap)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_nic_cfg *nic_cfg = &dev->nic_io->nic_cfg;
	int err;

	down(&nic_cfg->cfg_lock);

	err = hinic_dcb_set_hw_pfc(hwdev, pfc_en, pfc_bitmap);
	if (err) {
		up(&nic_cfg->cfg_lock);
		return err;
	}

	nic_cfg->pfc_en = pfc_en;
	nic_cfg->pfc_bitmap = pfc_bitmap;

	/* pause settings is opposite from pfc */
	nic_cfg->nic_pause.rx_pause = pfc_en ? 0 : 1;
	nic_cfg->nic_pause.tx_pause = pfc_en ? 0 : 1;

	up(&nic_cfg->cfg_lock);

	return 0;
}

int hinic_dcb_get_pfc(void *hwdev, u8 *pfc_en_bitmap)
{
	return 0;
}

int hinic_dcb_set_ets(void *hwdev, u8 *up_tc, u8 *pg_bw, u8 *pgid, u8 *up_bw,
		      u8 *prio)
{
	struct hinic_up_ets_cfg ets = {0};
	u16 out_size = sizeof(ets);
	u16 up_bw_t = 0;
	u8 pg_bw_t = 0;
	int i, err;

	for (i = 0; i < HINIC_DCB_TC_MAX; i++) {
		up_bw_t += *(up_bw + i);
		pg_bw_t += *(pg_bw + i);

		if (*(up_tc + i) > HINIC_DCB_TC_MAX) {
			nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
				"Invalid up %d mapping tc: %d\n",
				i, *(up_tc + i));
			return -EINVAL;
		}
	}

	if (pg_bw_t != 100 || (up_bw_t % 100) != 0) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Invalid pg_bw: %d or up_bw: %d\n", pg_bw_t, up_bw_t);
		return -EINVAL;
	}

	ets.port_id = 0;    /* reserved */
	memcpy(ets.up_tc, up_tc, HINIC_DCB_TC_MAX);
	memcpy(ets.pg_bw, pg_bw, HINIC_DCB_UP_MAX);
	memcpy(ets.pgid, pgid, HINIC_DCB_UP_MAX);
	memcpy(ets.up_bw, up_bw, HINIC_DCB_UP_MAX);
	memcpy(ets.prio, prio, HINIC_DCB_UP_MAX);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_ETS,
				     &ets, sizeof(ets), &ets, &out_size);
	if (err || ets.status || !out_size) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to set ets, err: %d, status: 0x%x, out size: 0x%x\n",
			err, ets.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_dcb_get_ets(void *hwdev, u8 *up_tc, u8 *pg_bw, u8 *pgid, u8 *up_bw,
		      u8 *prio)
{
	return 0;
}

int hinic_dcb_set_cos_up_map(void *hwdev, u8 cos_valid_bitmap, u8 *cos_up)
{
	struct hinic_cos_up_map map = {0};
	u16 out_size = sizeof(map);
	int err;

	if (!hwdev || !cos_up)
		return -EINVAL;

	map.port_id = hinic_physical_port_id(hwdev);
	map.cos_valid_mask = cos_valid_bitmap;
	memcpy(map.map, cos_up, sizeof(map.map));

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_COS_UP_MAP,
				     &map, sizeof(map), &map, &out_size);
	if (err || map.status || !out_size) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to set cos2up map, err: %d, status: 0x%x, out size: 0x%x\n",
			err, map.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int hinic_dcb_set_rq_iq_mapping(void *hwdev, u32 num_rqs, u8 *map)
{
	struct hinic_hwdev *dev;
	struct hinic_nic_io *nic_io;
	struct hinic_set_rq_iq_mapping rq_iq_mapping = {0};
	u16 out_size = sizeof(rq_iq_mapping);
	int err;

	if (!hwdev || !map || num_rqs > HINIC_MAX_NUM_RQ)
		return -EINVAL;

	dev = hwdev;
	nic_io = dev->nic_io;

	hinic_qps_num_set(dev, nic_io->num_qps);

	err = hinic_global_func_id_get(hwdev, &rq_iq_mapping.func_id);
	if (err)
		return err;

	rq_iq_mapping.num_rqs = num_rqs;
	rq_iq_mapping.rq_depth = (u16)ilog2(nic_io->rq_depth);

	memcpy(rq_iq_mapping.map, map, num_rqs);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_RQ_IQ_MAP,
				     &rq_iq_mapping, sizeof(rq_iq_mapping),
				     &rq_iq_mapping, &out_size);
	if (err || !out_size || rq_iq_mapping.status) {
		nic_err(dev->dev_hdl, "Failed to set rq cos mapping, err: %d, status: 0x%x, out size: 0x%x\n",
			err, rq_iq_mapping.status, out_size);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_dcb_set_rq_iq_mapping);

int hinic_set_pfc_threshold(void *hwdev, u16 op_type, u16 threshold)
{
	struct hinic_pfc_thd pfc_thd = {0};
	u16 out_size = sizeof(pfc_thd);
	int err;

	if (op_type == HINIC_PFC_SET_FUNC_THD)
		pfc_thd.func_thd = threshold;
	else if (op_type == HINIC_PFC_SET_GLB_THD)
		pfc_thd.glb_thd = threshold;
	else
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &pfc_thd.func_id);
	if (err)
		return err;

	pfc_thd.op_type = op_type;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_PFC_THD,
				     &pfc_thd, sizeof(pfc_thd),
				     &pfc_thd, &out_size);
	if (err || !out_size || pfc_thd.status) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to set pfc threshold, err: %d, status: 0x%x, out size: 0x%x\n",
			err, pfc_thd.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int hinic_set_bp_thd(void *hwdev, u16 threshold)
{
	int err;

	err = hinic_set_pfc_threshold(hwdev, HINIC_PFC_SET_GLB_THD, threshold);
	if (err) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to set global threshold\n");
		return -EFAULT;
	}

	err = hinic_set_pfc_threshold(hwdev, HINIC_PFC_SET_FUNC_THD, threshold);
	if (err) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to set function threshold\n");
		return -EFAULT;
	}

	return 0;
}

int hinic_disable_fw_bp(void *hwdev)
{
	int err;

	err = hinic_set_pfc_threshold(hwdev, HINIC_PFC_SET_FUNC_THD, 0);
	if (err) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to disable ucode backpressure\n");
		return -EFAULT;
	}

	return 0;
}

int hinic_set_iq_enable(void *hwdev, u16 q_id, u16 lower_thd, u16 prod_idx)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_cmd_enable_iq *iq_info;
	struct hinic_cmd_buf *cmd_buf;
	int err;

	cmd_buf = hinic_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		nic_err(dev->dev_hdl, "Failed to allocate cmd buf\n");
		return -ENOMEM;
	}

	iq_info = cmd_buf->buf;
	cmd_buf->size = sizeof(*iq_info);

	iq_info->force_en = 0;
	iq_info->rq_depth = (u8)ilog2(dev->nic_io->rq_depth);
	iq_info->num_rq = (u8)dev->nic_io->max_qps;
	/* num_qps will not lager than 64 */
	iq_info->glb_rq_id = dev->nic_io->global_qpn + q_id;
	iq_info->q_id = q_id;
	iq_info->lower_thd = lower_thd;
	iq_info->prod_idx = prod_idx;
	hinic_cpu_to_be32(iq_info, sizeof(*iq_info));

	err = hinic_cmdq_async(hwdev, HINIC_ACK_TYPE_CMDQ, HINIC_MOD_L2NIC,
			       HINIC_UCODE_CMD_SET_IQ_ENABLE, cmd_buf);
	if (err) {
		hinic_free_cmd_buf(hwdev, cmd_buf);
		nic_err(dev->dev_hdl, "Failed to set iq enable, err:%d\n", err);
		return -EFAULT;
	}

	return 0;
}

int hinic_set_iq_enable_mgmt(void *hwdev, u16 q_id, u16 lower_thd, u16 prod_idx)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_cmd_enable_iq_mgmt iq_info = {0};
	int err;

	iq_info.force_en = 0;

	iq_info.rq_depth = (u8)ilog2(dev->nic_io->rq_depth);
	iq_info.num_rq = (u8)dev->nic_io->max_qps;
	/* num_qps will not lager than 64 */
	iq_info.glb_rq_id = dev->nic_io->global_qpn + q_id;
	iq_info.q_id = q_id;
	iq_info.lower_thd = lower_thd;
	iq_info.prod_idx = prod_idx;

	err = l2nic_msg_to_mgmt_async(hwdev, HINIC_PORT_CMD_SET_IQ_ENABLE,
				      &iq_info, sizeof(iq_info));
	if (err || iq_info.status) {
		nic_err(dev->dev_hdl, "Failed to set iq enable for rq:%d, err: %d, status: 0x%x\n",
			q_id, err, iq_info.status);
		return -EFAULT;
	}

	return 0;
}

/* nictool */
int hinic_set_lro_aging_timer(void *hwdev, u8 timer_en, u32 period)
{
	return 0;
}

int hinic_get_rx_lro(void *hwdev, struct nic_lro_info *cfg)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_lro_config lro_cfg = {0};
	u16 out_size = sizeof(lro_cfg);
	int err;

	if (!hwdev || !cfg)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &lro_cfg.func_id);
	if (err)
		return err;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_LRO,
				     &lro_cfg, sizeof(lro_cfg),
				     &lro_cfg, &out_size);
	if (err || !out_size || lro_cfg.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set lro offload, err: %d, status: 0x%x, out size: 0x%x\n",
			err, lro_cfg.status, out_size);
		return -EINVAL;
	}

	cfg->func_id = lro_cfg.func_id;
	cfg->lro_ipv4_en = lro_cfg.lro_ipv4_en;
	cfg->lro_ipv6_en = lro_cfg.lro_ipv6_en;
	cfg->lro_max_wqe_num = lro_cfg.lro_max_wqe_num;
	return 0;
}

int hinic_get_jumbo_frame_size(void *hwdev, u32 *jumbo_size)
{
	return 0;
}

int hinic_set_jumbo_frame_size(void *hwdev, u32 jumbo_size)
{
	return 0;
}

int hinic_set_loopback_mode_ex(void *hwdev, u32 mode, u32 enable)
{
	struct hinic_port_loopback lb = {0};
	u16 out_size = sizeof(lb);
	int err;

	lb.mode = mode;
	lb.en = enable;

	if ((mode < LOOP_MODE_MIN) || (mode > LOOP_MODE_MAX)) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Invalid loopback mode %d to set\n", mode);
		return -EINVAL;
	}

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_LOOPBACK_MODE,
				     &lb, sizeof(lb), &lb, &out_size);
	if (err || !out_size || lb.status) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to set loopback mode %d en %d, err: %d, status: 0x%x, out size: 0x%x\n",
			mode, enable, err, lb.status, out_size);
		return -EINVAL;
	}

	nic_info(((struct hinic_hwdev *)hwdev)->dev_hdl,
		 "Set loopback mode %d en %d succeed\n", mode, enable);

	return 0;
}

int hinic_get_loopback_mode_ex(void *hwdev, u32 *mode, u32 *enable)
{
	struct hinic_port_loopback lb = {0};
	u16 out_size = sizeof(lb);
	int err;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_LOOPBACK_MODE,
				     &lb, sizeof(lb), &lb, &out_size);
	if (err || !out_size || lb.status) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to get loopback mode, err: %d, status: 0x%x, out size: 0x%x\n",
			err, lb.status, out_size);
		return -EINVAL;
	}

	*mode = lb.mode;
	*enable = lb.en;
	return 0;
}

int hinic_set_loopback_mode(void *hwdev, bool enable)
{
	return hinic_set_loopback_mode_ex(hwdev, HINIC_INTERNAL_LP_MODE,
					  enable);
}

int hinic_get_port_enable_state(void *hwdev, bool *enable)
{
	return 0;
}

int hinic_get_vport_enable_state(void *hwdev, bool *enable)
{
	return 0;
}

int hinic_set_lli_state(void *hwdev, u8 lli_state)
{
	return 0;
}

int hinic_set_vport_enable(void *hwdev, bool enable)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_vport_state en_state = {0};
	u16 out_size = sizeof(en_state);
	int err;
	u32 timeout;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &en_state.func_id);
	if (err)
		return err;

	en_state.state = enable ? 1 : 0;

	if (HINIC_IS_VF(nic_hwdev))
		timeout = SET_VPORT_MBOX_TIMEOUT;
	else
		timeout = SET_VPORT_MGMT_TIMEOUT;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_L2NIC,
				     HINIC_PORT_CMD_SET_VPORT_ENABLE,
				     &en_state, sizeof(en_state), &en_state,
				     &out_size, timeout);

	if (err || !out_size || en_state.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set vport state, err: %d, status: 0x%x, out size: 0x%x\n",
			err, en_state.status, out_size);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_set_vport_enable);

#define NIC_PORT_DISABLE		0x0
#define NIC_PORT_ENABLE			0x3
int hinic_set_port_enable(void *hwdev, bool enable)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_port_state en_state = {0};
	u16 out_size = sizeof(en_state);
	int err;

	if (!hwdev)
		return -EINVAL;

	if (HINIC_IS_VF(nic_hwdev))
		return 0;

	err = hinic_global_func_id_get(hwdev, &en_state.func_id);
	if (err)
		return err;

	en_state.version = HINIC_CMD_VER_FUNC_ID;
	en_state.state = enable ? NIC_PORT_ENABLE : NIC_PORT_DISABLE;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_PORT_ENABLE,
				     &en_state, sizeof(en_state), &en_state,
				     &out_size);
	if (err || !out_size || en_state.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set port state, err: %d, status: 0x%x, out size: 0x%x\n",
			err, en_state.status, out_size);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_set_port_enable);

/* rss */
int hinic_set_rss_type(void *hwdev, u32 tmpl_idx, struct nic_rss_type rss_type)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct nic_rss_context_tbl *ctx_tbl;
	struct hinic_cmd_buf *cmd_buf;
	u32 ctx = 0;
	u64 out_param;
	int err;

	if (!hwdev)
		return -EINVAL;

	cmd_buf = hinic_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		nic_err(nic_hwdev->dev_hdl, "Failed to allocate cmd buf\n");
		return -ENOMEM;
	}

	ctx |= HINIC_RSS_TYPE_SET(1, VALID) |
		HINIC_RSS_TYPE_SET(rss_type.ipv4, IPV4) |
		HINIC_RSS_TYPE_SET(rss_type.ipv6, IPV6) |
		HINIC_RSS_TYPE_SET(rss_type.ipv6_ext, IPV6_EXT) |
		HINIC_RSS_TYPE_SET(rss_type.tcp_ipv4, TCP_IPV4) |
		HINIC_RSS_TYPE_SET(rss_type.tcp_ipv6, TCP_IPV6) |
		HINIC_RSS_TYPE_SET(rss_type.tcp_ipv6_ext, TCP_IPV6_EXT) |
		HINIC_RSS_TYPE_SET(rss_type.udp_ipv4, UDP_IPV4) |
		HINIC_RSS_TYPE_SET(rss_type.udp_ipv6, UDP_IPV6);

	cmd_buf->size = sizeof(struct nic_rss_context_tbl);

	ctx_tbl = (struct nic_rss_context_tbl *)cmd_buf->buf;
	ctx_tbl->group_index = cpu_to_be32(tmpl_idx);
	ctx_tbl->offset = 0;
	ctx_tbl->size = sizeof(u32);
	ctx_tbl->size = cpu_to_be32(ctx_tbl->size);
	ctx_tbl->rsvd = 0;
	ctx_tbl->ctx = cpu_to_be32(ctx);

	/* cfg the rss context table by command queue */
	err = hinic_cmdq_direct_resp(hwdev, HINIC_ACK_TYPE_CMDQ,
				     HINIC_MOD_L2NIC,
				     HINIC_UCODE_CMD_SET_RSS_CONTEXT_TABLE,
				     cmd_buf, &out_param, 0);

	hinic_free_cmd_buf(hwdev, cmd_buf);

	if (err || out_param != 0) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set rss context table, err: %d\n",
			err);
		return -EFAULT;
	}

	return 0;
}

int hinic_get_rss_type(void *hwdev, u32 tmpl_idx, struct nic_rss_type *rss_type)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_rss_context_table ctx_tbl = {0};
	u16 out_size = sizeof(ctx_tbl);
	int err;

	if (!hwdev || !rss_type)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &ctx_tbl.func_id);
	if (err)
		return err;

	ctx_tbl.template_id = (u8)tmpl_idx;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_RSS_CTX_TBL,
				     &ctx_tbl, sizeof(ctx_tbl),
				     &ctx_tbl, &out_size);
	if (err || !out_size || ctx_tbl.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to get hash type, err: %d, status: 0x%x, out size: 0x%x\n",
			err, ctx_tbl.status, out_size);
		return -EINVAL;
	}

	rss_type->ipv4		= HINIC_RSS_TYPE_GET(ctx_tbl.context, IPV4);
	rss_type->ipv6		= HINIC_RSS_TYPE_GET(ctx_tbl.context, IPV6);
	rss_type->ipv6_ext	= HINIC_RSS_TYPE_GET(ctx_tbl.context, IPV6_EXT);
	rss_type->tcp_ipv4	= HINIC_RSS_TYPE_GET(ctx_tbl.context, TCP_IPV4);
	rss_type->tcp_ipv6	= HINIC_RSS_TYPE_GET(ctx_tbl.context, TCP_IPV6);
	rss_type->tcp_ipv6_ext	= HINIC_RSS_TYPE_GET(ctx_tbl.context,
						     TCP_IPV6_EXT);
	rss_type->udp_ipv4	= HINIC_RSS_TYPE_GET(ctx_tbl.context, UDP_IPV4);
	rss_type->udp_ipv6	= HINIC_RSS_TYPE_GET(ctx_tbl.context, UDP_IPV6);

	return 0;
}

int hinic_rss_set_template_tbl(void *hwdev, u32 tmpl_idx, const u8 *temp)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_rss_template_key temp_key = {0};
	u16 out_size = sizeof(temp_key);
	int err;

	if (!hwdev || !temp)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &temp_key.func_id);
	if (err)
		return err;

	temp_key.template_id = (u8)tmpl_idx;
	memcpy(temp_key.key, temp, HINIC_RSS_KEY_SIZE);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_RSS_TEMPLATE_TBL,
				     &temp_key, sizeof(temp_key),
				     &temp_key, &out_size);
	if (err || !out_size || temp_key.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set hash key, err: %d, status: 0x%x, out size: 0x%x\n",
			err, temp_key.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_rss_get_template_tbl(void *hwdev, u32 tmpl_idx, u8 *temp)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_rss_template_key temp_key = {0};
	u16 out_size = sizeof(temp_key);
	int err;

	if (!hwdev || !temp)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &temp_key.func_id);
	if (err)
		return err;

	temp_key.template_id = (u8)tmpl_idx;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_RSS_TEMPLATE_TBL,
				     &temp_key, sizeof(temp_key),
				     &temp_key, &out_size);
	if (err || !out_size || temp_key.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to get hash key, err: %d, status: 0x%x, out size: 0x%x\n",
			err, temp_key.status, out_size);
		return -EINVAL;
	}

	memcpy(temp, temp_key.key, HINIC_RSS_KEY_SIZE);

	return 0;
}

int hinic_rss_get_hash_engine(void *hwdev, u8 tmpl_idx, u8 *type)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_rss_engine_type hash_type = {0};
	u16 out_size = sizeof(hash_type);
	int err;

	if (!hwdev || !type)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &hash_type.func_id);
	if (err)
		return err;

	hash_type.template_id = tmpl_idx;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_RSS_HASH_ENGINE,
				     &hash_type, sizeof(hash_type),
				     &hash_type, &out_size);
	if (err || !out_size || hash_type.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to get hash engine, err: %d, status: 0x%x, out size: 0x%x\n",
			err, hash_type.status, out_size);
		return -EINVAL;
	}

	*type = hash_type.hash_engine;
	return 0;
}

int hinic_rss_set_hash_engine(void *hwdev, u8 tmpl_idx, u8 type)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_rss_engine_type hash_type = {0};
	u16 out_size = sizeof(hash_type);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &hash_type.func_id);
	if (err)
		return err;

	hash_type.hash_engine = type;
	hash_type.template_id = tmpl_idx;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_RSS_HASH_ENGINE,
				     &hash_type, sizeof(hash_type),
				     &hash_type, &out_size);
	if (err || !out_size || hash_type.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set hash engine, err: %d, status: 0x%x, out size: 0x%x\n",
			err, hash_type.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_rss_set_indir_tbl(void *hwdev, u32 tmpl_idx, const u32 *indir_table)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct nic_rss_indirect_tbl *indir_tbl;
	struct hinic_cmd_buf *cmd_buf;
	u32 i;
	u32 *temp;
	u32 indir_size;
	u64 out_param;
	int err;

	if (!hwdev || !indir_table)
		return -EINVAL;

	cmd_buf = hinic_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		nic_err(nic_hwdev->dev_hdl, "Failed to allocate cmd buf\n");
		return -ENOMEM;
	}

	cmd_buf->size = sizeof(struct nic_rss_indirect_tbl);

	indir_tbl = (struct nic_rss_indirect_tbl *)cmd_buf->buf;
	indir_tbl->group_index = cpu_to_be32(tmpl_idx);

	for (i = 0; i < HINIC_RSS_INDIR_SIZE; i++) {
		indir_tbl->entry[i] = (u8)(*(indir_table + i));

		if (0x3 == (i & 0x3)) {
			temp = (u32 *)&indir_tbl->entry[i - 3];
			*temp = cpu_to_be32(*temp);
		}
	}

	/* cfg the rss indirect table by command queue */
	indir_size = HINIC_RSS_INDIR_SIZE / 2;
	indir_tbl->offset = 0;
	indir_tbl->size = cpu_to_be32(indir_size);

	err = hinic_cmdq_direct_resp(hwdev, HINIC_ACK_TYPE_CMDQ,
				     HINIC_MOD_L2NIC,
				     HINIC_UCODE_CMD_SET_RSS_INDIR_TABLE,
				     cmd_buf, &out_param, 0);
	if (err || out_param != 0) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set rss indir table\n");
		err = -EFAULT;
		goto free_buf;
	}

	indir_tbl->offset = cpu_to_be32(indir_size);
	indir_tbl->size = cpu_to_be32(indir_size);
	memcpy(&indir_tbl->entry[0], &indir_tbl->entry[indir_size], indir_size);

	err = hinic_cmdq_direct_resp(hwdev, HINIC_ACK_TYPE_CMDQ,
				     HINIC_MOD_L2NIC,
				     HINIC_UCODE_CMD_SET_RSS_INDIR_TABLE,
				     cmd_buf, &out_param, 0);
	if (err || out_param != 0) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set rss indir table\n");
		err = -EFAULT;
	}

free_buf:
	hinic_free_cmd_buf(hwdev, cmd_buf);

	return err;
}

int hinic_rss_get_indir_tbl(void *hwdev, u32 tmpl_idx, u32 *indir_table)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_rss_indir_table rss_cfg = {0};
	u16 out_size = sizeof(rss_cfg);
	int err = 0, i;

	err = hinic_global_func_id_get(hwdev, &rss_cfg.func_id);
	if (err)
		return err;

	rss_cfg.template_id = (u8)tmpl_idx;

	err = l2nic_msg_to_mgmt_sync(hwdev,
				     HINIC_PORT_CMD_GET_RSS_TEMPLATE_INDIR_TBL,
				     &rss_cfg, sizeof(rss_cfg), &rss_cfg,
				     &out_size);
	if (err || !out_size || rss_cfg.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to get indir table, err: %d, status: 0x%x, out size: 0x%x\n",
			err, rss_cfg.status, out_size);
		return -EINVAL;
	}

	hinic_be32_to_cpu(rss_cfg.indir, HINIC_RSS_INDIR_SIZE);
	for (i = 0; i < HINIC_RSS_INDIR_SIZE; i++)
		indir_table[i] = rss_cfg.indir[i];

	return 0;
}

int hinic_rss_cfg(void *hwdev, u8 rss_en, u8 tmpl_idx, u8 tc_num, u8 *prio_tc)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_rss_config rss_cfg = {0};
	u16 out_size = sizeof(rss_cfg);
	int err;

	/* micro code required: number of TC should be power of 2 */
	if (!hwdev || !prio_tc || (tc_num & (tc_num - 1)))
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &rss_cfg.func_id);
	if (err)
		return err;

	rss_cfg.rss_en = rss_en;
	rss_cfg.template_id = tmpl_idx;
	rss_cfg.rq_priority_number = tc_num ? (u8)ilog2(tc_num) : 0;

	memcpy(rss_cfg.prio_tc, prio_tc, HINIC_DCB_UP_MAX);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_RSS_CFG,
				     &rss_cfg, sizeof(rss_cfg),
				     &rss_cfg, &out_size);
	if (err || !out_size || rss_cfg.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to set rss cfg, err: %d, status: 0x%x, out size: 0x%x\n",
			err, rss_cfg.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_get_vport_stats(void *hwdev, struct hinic_vport_stats *stats)
{
	struct hinic_port_stats_info stats_info = {0};
	struct hinic_cmd_vport_stats vport_stats = {0};
	u16 out_size = sizeof(vport_stats);
	int err;

	err = hinic_global_func_id_get(hwdev, &stats_info.func_id);
	if (err)
		return err;

	stats_info.stats_version = HINIC_PORT_STATS_VERSION;
	stats_info.stats_size = sizeof(vport_stats);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_VPORT_STAT,
				     &stats_info, sizeof(stats_info),
				     &vport_stats, &out_size);
	if (err || !out_size || vport_stats.status) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to get function statistics, err: %d, status: 0x%x, out size: 0x%x\n",
			err, vport_stats.status, out_size);
		return -EFAULT;
	}

	memcpy(stats, &vport_stats.stats, sizeof(*stats));

	return 0;
}

int hinic_get_phy_port_stats(void *hwdev, struct hinic_phy_port_stats *stats)
{
	struct hinic_port_stats *port_stats;
	struct hinic_port_stats_info stats_info = {0};
	u16 out_size = sizeof(*port_stats);
	int err;

	port_stats = kzalloc(sizeof(*port_stats), GFP_KERNEL);
	if (!port_stats)
		return -ENOMEM;

	stats_info.stats_version = HINIC_PORT_STATS_VERSION;
	stats_info.stats_size = sizeof(*port_stats);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_PORT_STATISTICS,
				     &stats_info, sizeof(stats_info),
				     port_stats, &out_size);
	if (err || !out_size || port_stats->status) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to get port statistics, err: %d, status: 0x%x, out size: 0x%x\n",
			err, port_stats->status, out_size);
		err = -EINVAL;
		goto out;
	}

	memcpy(stats, &port_stats->stats, sizeof(*stats));

out:
	kfree(port_stats);

	return err;
}


int hinic_get_mgmt_version(void *hwdev, u8 *mgmt_ver)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_version_info up_ver = {0};
	u16 out_size;
	int err;

	out_size = sizeof(up_ver);
	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_MGMT_VERSION,
				     &up_ver, sizeof(up_ver), &up_ver,
				     &out_size);
	if (err || !out_size || up_ver.status) {
		nic_err(dev->dev_hdl, "Failed to get mgmt version, err: %d, status: 0x%x, out size: 0x%x\n",
			err, up_ver.status, out_size);
		return -EINVAL;
	}

	err = snprintf(mgmt_ver, HINIC_MGMT_VERSION_MAX_LEN, "%s", up_ver.ver);
	if (err <= 0 || err >= HINIC_MGMT_VERSION_MAX_LEN) {
		nic_err(dev->dev_hdl,
			"Failed snprintf fw version, function return(%d) and dest_len(%d)\n",
			err, HINIC_MGMT_VERSION_MAX_LEN);
		return -EINVAL;
	}

	return 0;
}

int hinic_get_fw_version(void *hwdev, struct hinic_fw_version *fw_ver)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_version_info ver_info = {0};
	u16 out_size = sizeof(ver_info);
	int err;

	if (!hwdev || !fw_ver)
		return -EINVAL;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_MGMT_VERSION,
				     &ver_info, sizeof(ver_info), &ver_info,
				     &out_size);
	if (err || !out_size || ver_info.status) {
		nic_err(dev->dev_hdl, "Failed to get mgmt version, err: %d, status: 0x%x, out size: 0x%x\n",
			err, ver_info.status, out_size);
		return -EINVAL;
	}

	memcpy(fw_ver->mgmt_ver, ver_info.ver, HINIC_FW_VERSION_NAME);

	out_size = sizeof(ver_info);
	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_BOOT_VERSION,
				     &ver_info, sizeof(ver_info), &ver_info,
				     &out_size);
	if (err || !out_size || ver_info.status) {
		nic_err(dev->dev_hdl, "Failed to get boot versionerr: %d, status: 0x%x, out size: 0x%x\n",
			err, ver_info.status, out_size);
		return -EINVAL;
	}

	memcpy(fw_ver->boot_ver, ver_info.ver, HINIC_FW_VERSION_NAME);

	out_size = sizeof(ver_info);
	err = l2nic_msg_to_mgmt_sync(hwdev,
				     HINIC_PORT_CMD_GET_MICROCODE_VERSION,
				     &ver_info, sizeof(ver_info), &ver_info,
				     &out_size);
	if (err || !out_size || ver_info.status) {
		nic_err(dev->dev_hdl, "Failed to get microcode version, err: %d, status: 0x%x, out size: 0x%x\n",
			err, ver_info.status, out_size);
		return -EINVAL;
	}

	memcpy(fw_ver->microcode_ver, ver_info.ver, HINIC_FW_VERSION_NAME);

	return 0;
}
EXPORT_SYMBOL(hinic_get_fw_version);

int hinic_rss_template_alloc(void *hwdev, u8 *tmpl_idx)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_rss_template_mgmt template_mgmt = {0};
	u16 out_size = sizeof(template_mgmt);
	int err;

	if (!hwdev || !tmpl_idx)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &template_mgmt.func_id);
	if (err)
		return err;

	template_mgmt.cmd = NIC_RSS_CMD_TEMP_ALLOC;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_RSS_TEMP_MGR,
				     &template_mgmt, sizeof(template_mgmt),
				     &template_mgmt, &out_size);
	if (err || !out_size || template_mgmt.status) {
		if (template_mgmt.status == HINIC_MGMT_STATUS_ERR_FULL) {
			nic_warn(nic_hwdev->dev_hdl, "Failed to alloc rss template, table is full\n");
			return -ENOSPC;
		}
		nic_err(nic_hwdev->dev_hdl, "Failed to alloc rss template, err: %d, status: 0x%x, out size: 0x%x\n",
			err, template_mgmt.status, out_size);
		return -EINVAL;
	}

	*tmpl_idx = template_mgmt.template_id;

	return 0;
}

int hinic_rss_template_free(void *hwdev, u8 tmpl_idx)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_rss_template_mgmt template_mgmt = {0};
	u16 out_size = sizeof(template_mgmt);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &template_mgmt.func_id);
	if (err)
		return err;

	template_mgmt.template_id = tmpl_idx;
	template_mgmt.cmd = NIC_RSS_CMD_TEMP_FREE;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_RSS_TEMP_MGR,
				     &template_mgmt, sizeof(template_mgmt),
				     &template_mgmt, &out_size);
	if (err || !out_size || template_mgmt.status) {
		nic_err(nic_hwdev->dev_hdl, "Failed to free rss template, err: %d, status: 0x%x, out size: 0x%x\n",
			err, template_mgmt.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_set_port_funcs_state(void *hwdev, bool enable)
{
	struct hinic_hwdev *dev = (struct hinic_hwdev *)hwdev;
	struct hinic_port_funcs_state state = {0};
	u16 out_size = sizeof(state);
	int err = 0;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &state.func_id);
	if (err)
		return err;

	state.drop_en = enable ? 0 : 1;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_PORT_FUNCS_STATE,
				     &state, sizeof(state), &state, &out_size);
	if (err || !out_size || state.status) {
		nic_err(dev->dev_hdl, "Failed to %s all functions in port, err: %d, status: 0x%x, out size: 0x%x\n",
			enable ? "enable" : "disable", err, state.status,
			out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_reset_port_link_cfg(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_reset_link_cfg reset_cfg = {0};
	u16 out_size = sizeof(reset_cfg);
	int err;

	err = hinic_global_func_id_get(hwdev, &reset_cfg.func_id);
	if (err)
		return err;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_RESET_LINK_CFG,
				     &reset_cfg, sizeof(reset_cfg),
				     &reset_cfg, &out_size);
	if (err || !out_size || reset_cfg.status) {
		nic_err(dev->dev_hdl, "Failed to reset port link configure, err: %d, status: 0x%x, out size: 0x%x\n",
			err, reset_cfg.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int hinic_save_vf_mac(void *hwdev, u16 vf_id, u8 *mac)
{
	struct hinic_nic_io *nic_io;

	if (!hwdev || !mac)
		return -EINVAL;

	nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	memcpy(nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].vf_mac_addr, mac,
	       ETH_ALEN);

	return 0;
}

static int hinic_change_vf_mtu_msg_handler(struct hinic_hwdev *hwdev, u16 vf_id,
					   void *buf_in, u16 in_size,
					   void *buf_out, u16 *out_size)
{
	int err;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_L2NIC,
				     HINIC_PORT_CMD_CHANGE_MTU, buf_in, in_size,
				     buf_out, out_size, 0);
	if (err) {
		nic_err(hwdev->dev_hdl, "Failed to set VF %u mtu\n", vf_id);
		return err;
	}

	return 0;
}

static bool is_ether_addr_zero(const u8 *addr)
{
	return !(addr[0] | addr[1] | addr[2] | addr[3] | addr[4] | addr[5]);
}

static int hinic_get_vf_mac_msg_handler(struct hinic_nic_io *nic_io, u16 vf,
					void *buf_in, u16 in_size,
					void *buf_out, u16 *out_size)
{
	struct vf_data_storage *vf_info = nic_io->vf_infos + HW_VF_ID_TO_OS(vf);
	struct hinic_port_mac_set *mac_info = buf_out;
	int err;

	if ((nic_io->hwdev->func_mode == FUNC_MOD_MULTI_BM_SLAVE) ||
	    (nic_io->hwdev->func_mode == FUNC_MOD_MULTI_VM_SLAVE) ||
	    (hinic_support_ovs(nic_io->hwdev, NULL))) {
		err = hinic_pf_msg_to_mgmt_sync(nic_io->hwdev, HINIC_MOD_L2NIC,
						HINIC_PORT_CMD_GET_MAC, buf_in,
						in_size, buf_out, out_size, 0);

		if (!err) {
			if (is_ether_addr_zero(&mac_info->mac[0]))
				memcpy(mac_info->mac,
				       vf_info->vf_mac_addr, ETH_ALEN);
		}
		return err;
	}

	memcpy(mac_info->mac, vf_info->vf_mac_addr, ETH_ALEN);
	mac_info->status = 0;
	*out_size = sizeof(*mac_info);

	return 0;
}

static int hinic_set_vf_mac_msg_handler(struct hinic_nic_io *nic_io, u16 vf,
					void *buf_in, u16 in_size,
					void *buf_out, u16 *out_size)
{
	struct vf_data_storage *vf_info = nic_io->vf_infos + HW_VF_ID_TO_OS(vf);
	struct hinic_port_mac_set *mac_in = buf_in;
	struct hinic_port_mac_set *mac_out = buf_out;
	int err;

	if (vf_info->pf_set_mac && !(vf_info->trust) &&
	    is_valid_ether_addr(mac_in->mac)) {
		nic_warn(nic_io->hwdev->dev_hdl, "PF has already set VF %d MAC address\n",
			 HW_VF_ID_TO_OS(vf));
		mac_out->status = HINIC_PF_SET_VF_ALREADY;
		*out_size = sizeof(*mac_out);
		return 0;
	}

	err = hinic_pf_msg_to_mgmt_sync(nic_io->hwdev, HINIC_MOD_L2NIC,
					HINIC_PORT_CMD_SET_MAC, buf_in, in_size,
					buf_out, out_size, 0);
	if ((err && err != HINIC_DEV_BUSY_ACTIVE_FW &&
	     err != HINIC_MBOX_PF_BUSY_ACTIVE_FW) || !(*out_size)) {
		nic_err(nic_io->hwdev->dev_hdl, "Failed to set VF %d MAC address, err: %d, status: 0x%x, out size: 0x%x\n",
			HW_VF_ID_TO_OS(vf), err, mac_out->status, *out_size);
		return -EFAULT;
	}

	return err;
}

static int hinic_del_vf_mac_msg_handler(struct hinic_nic_io *nic_io, u16 vf,
					void *buf_in, u16 in_size,
					void *buf_out, u16 *out_size)
{
	struct vf_data_storage *vf_info = nic_io->vf_infos + HW_VF_ID_TO_OS(vf);
	struct hinic_port_mac_set *mac_in = buf_in;
	struct hinic_port_mac_set *mac_out = buf_out;
	int err;

	if (vf_info->pf_set_mac && !(vf_info->trust) &&
	    is_valid_ether_addr(mac_in->mac) &&
	    !memcmp(vf_info->vf_mac_addr, mac_in->mac, ETH_ALEN)) {
		nic_warn(nic_io->hwdev->dev_hdl, "PF has already set VF mac.\n");
		mac_out->status = HINIC_PF_SET_VF_ALREADY;
		*out_size = sizeof(*mac_out);
		return 0;
	}

	err = hinic_pf_msg_to_mgmt_sync(nic_io->hwdev, HINIC_MOD_L2NIC,
					HINIC_PORT_CMD_DEL_MAC, buf_in, in_size,
					buf_out, out_size, 0);
	if ((err && err != HINIC_DEV_BUSY_ACTIVE_FW &&
	     err != HINIC_MBOX_PF_BUSY_ACTIVE_FW) || !(*out_size)) {
		nic_err(nic_io->hwdev->dev_hdl, "Failed to delete VF %d MAC, err: %d, status: 0x%x, out size: 0x%x\n",
			HW_VF_ID_TO_OS(vf), err, mac_out->status, *out_size);
		return -EFAULT;
	}

	return err;
}

static int hinic_update_vf_mac_msg_handler(struct hinic_nic_io *nic_io, u16 vf,
					   void *buf_in, u16 in_size,
					   void *buf_out, u16 *out_size)
{
	struct vf_data_storage *vf_info = nic_io->vf_infos + HW_VF_ID_TO_OS(vf);
	struct hinic_port_mac_update *mac_in = buf_in;
	struct hinic_port_mac_update *mac_out = buf_out;
	int err;

	if (!is_valid_ether_addr(mac_in->new_mac)) {
		nic_err(nic_io->hwdev->dev_hdl, "Update VF MAC is invalid.\n");
		return -EINVAL;
	}

	if (vf_info->pf_set_mac && !(vf_info->trust)) {
		nic_warn(nic_io->hwdev->dev_hdl, "PF has already set VF mac.\n");
		mac_out->status = HINIC_PF_SET_VF_ALREADY;
		*out_size = sizeof(*mac_out);
		return 0;
	}

	err = hinic_pf_msg_to_mgmt_sync(nic_io->hwdev, HINIC_MOD_L2NIC,
					HINIC_PORT_CMD_UPDATE_MAC, buf_in,
					in_size, buf_out, out_size, 0);
	if ((err && err != HINIC_DEV_BUSY_ACTIVE_FW &&
	     err != HINIC_MBOX_PF_BUSY_ACTIVE_FW) || !(*out_size)) {
		nic_warn(nic_io->hwdev->dev_hdl, "Failed to update VF %d MAC, err: %d, status: 0x%x, out size: 0x%x\n",
			 HW_VF_ID_TO_OS(vf), err, mac_out->status, *out_size);
		return -EFAULT;
	}

	return err;
}

static int hinic_set_vf_vlan(struct hinic_hwdev *hwdev, bool add, u16 vid,
			     u8 qos, int vf_id)
{
	struct hinic_vf_vlan_config vf_vlan = {0};
	u8 cmd;
	u16 out_size = sizeof(vf_vlan);
	int err;

	/* VLAN 0 is a special case, don't allow it to be removed */
	if (!vid && !add)
		return 0;

	vf_vlan.func_id = hinic_glb_pf_vf_offset(hwdev) + vf_id;
	vf_vlan.vlan_id = vid;
	vf_vlan.qos = qos;

	if (add)
		cmd = HINIC_PORT_CMD_SET_VF_VLAN;
	else
		cmd = HINIC_PORT_CMD_CLR_VF_VLAN;

	err = l2nic_msg_to_mgmt_sync(hwdev, cmd, &vf_vlan, sizeof(vf_vlan),
				     &vf_vlan, &out_size);
	if (err || !out_size || vf_vlan.status) {
		nic_err(hwdev->dev_hdl, "Failed to set VF %d vlan, err: %d, status: 0x%x, out size: 0x%x\n",
			HW_VF_ID_TO_OS(vf_id), err, vf_vlan.status, out_size);
		return -EFAULT;
	}

	return 0;
}

static int hinic_init_vf_config(struct hinic_hwdev *hwdev, u16 vf_id)
{
	struct vf_data_storage *vf_info;
	u16 func_id, vlan_id;
	int err = 0;

	vf_info = hwdev->nic_io->vf_infos + HW_VF_ID_TO_OS(vf_id);
	if (vf_info->pf_set_mac) {
		func_id = hinic_glb_pf_vf_offset(hwdev) + vf_id;
		if (FW_SUPPORT_MAC_REUSE_FUNC(hwdev)) {
			vlan_id = vf_info->pf_vlan;
			if (vlan_id)
				vlan_id |= HINIC_ADD_VLAN_IN_MAC;
		} else {
			vlan_id = 0;
		}

		err = hinic_set_mac(hwdev, vf_info->vf_mac_addr, vlan_id,
				    func_id);
		if (err) {
			nic_err(hwdev->dev_hdl, "Failed to set VF %d MAC\n",
				HW_VF_ID_TO_OS(vf_id));
			return err;
		}
	}
	if (hinic_vf_info_vlanprio(hwdev, vf_id)) {
		err = hinic_set_vf_vlan(hwdev, true, vf_info->pf_vlan,
					vf_info->pf_qos, vf_id);
		if (err) {
			nic_err(hwdev->dev_hdl, "Failed to add VF %d VLAN_QOS\n",
				HW_VF_ID_TO_OS(vf_id));
			return err;
		}
	}

	if (vf_info->max_rate) {
		err = hinic_set_vf_tx_rate(hwdev, vf_id, vf_info->max_rate,
					   vf_info->min_rate);
		if (err) {
			nic_err(hwdev->dev_hdl, "Failed to set VF %d max rate %d, min rate %d\n",
				HW_VF_ID_TO_OS(vf_id), vf_info->max_rate,
				vf_info->min_rate);
			return err;
		}
	}

	return 0;
}

static int hinic_register_vf_msg_handler(void *hwdev, u16 vf_id,
					 void *buf_out, u16 *out_size)
{
	struct hinic_hwdev *hw_dev = hwdev;
	struct hinic_nic_io *nic_io = hw_dev->nic_io;
	struct hinic_register_vf *register_info = buf_out;
	int err;

	if (vf_id > nic_io->max_vfs) {
		nic_err(hw_dev->dev_hdl, "Register VF id %d exceed limit[0-%d]\n",
			HW_VF_ID_TO_OS(vf_id), HW_VF_ID_TO_OS(nic_io->max_vfs));
		register_info->status = EFAULT;
		return -EFAULT;
	}

	*out_size = sizeof(*register_info);
	err = hinic_init_vf_config(hw_dev, vf_id);
	if (err) {
		register_info->status = EFAULT;
		return err;
	}

	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].registered = true;

	return 0;
}

void hinic_unregister_vf_msg_handler(void *hwdev, u16 vf_id)
{
	struct hinic_hwdev *hw_dev = (struct hinic_hwdev *)hwdev;
	struct hinic_nic_io *nic_io = hw_dev->nic_io;

	if (vf_id > nic_io->max_vfs)
		return;

	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].registered = false;
}

static void hinic_get_vf_link_status_msg_handler(struct hinic_nic_io *nic_io,
						 u16 vf_id, void *buf_out,
						 u16 *out_size)
{
	struct vf_data_storage *vf_infos = nic_io->vf_infos;
	struct hinic_get_link *get_link = buf_out;
	bool link_forced, link_up;

	link_forced = vf_infos[HW_VF_ID_TO_OS(vf_id)].link_forced;
	link_up = vf_infos[HW_VF_ID_TO_OS(vf_id)].link_up;

	if (link_forced)
		get_link->link_status = link_up ?
					HINIC_LINK_UP : HINIC_LINK_DOWN;
	else
		get_link->link_status = nic_io->link_status;

	get_link->status = 0;
	*out_size = sizeof(*get_link);
}

static void hinic_get_vf_cos_msg_handler(struct hinic_nic_io *nic_io,
					 u16 vf_id, void *buf_out,
					 u16 *out_size)
{
	struct hinic_vf_dcb_state *dcb_state = buf_out;

	memcpy(&dcb_state->state, &nic_io->dcb_state,
	       sizeof(nic_io->dcb_state));

	dcb_state->status = 0;
	*out_size = sizeof(*dcb_state);
}

/* pf receive message from vf */
int nic_pf_mbox_handler(void *hwdev, u16 vf_id, u8 cmd, void *buf_in,
			u16 in_size, void *buf_out, u16 *out_size)
{
	u8 size = sizeof(nic_cmd_support_vf) / sizeof(nic_cmd_support_vf[0]);
	struct hinic_nic_io *nic_io;
	int err = 0;
	u32 timeout = 0;

	if (!hwdev)
		return -EFAULT;

	if (!hinic_mbox_check_cmd_valid(hwdev, nic_cmd_support_vf, vf_id, cmd,
					buf_in, in_size, size)) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"PF Receive VF nic cmd(0x%x), mbox len(0x%x) is invalid\n",
			cmd, in_size);
		err = HINIC_MBOX_VF_CMD_ERROR;
		return err;
	}

	nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;

	switch (cmd) {
	case HINIC_PORT_CMD_VF_REGISTER:
		err = hinic_register_vf_msg_handler(hwdev, vf_id, buf_out,
						    out_size);
		break;

	case HINIC_PORT_CMD_VF_UNREGISTER:
		*out_size = 0;
		hinic_unregister_vf_msg_handler(hwdev, vf_id);
		break;

	case HINIC_PORT_CMD_CHANGE_MTU:
		err = hinic_change_vf_mtu_msg_handler(hwdev, vf_id, buf_in,
						      in_size, buf_out,
						      out_size);
		break;

	case HINIC_PORT_CMD_GET_MAC:
		hinic_get_vf_mac_msg_handler(nic_io, vf_id, buf_in,
					     in_size, buf_out, out_size);
		break;

	case HINIC_PORT_CMD_SET_MAC:
		err = hinic_set_vf_mac_msg_handler(nic_io, vf_id, buf_in,
						   in_size, buf_out, out_size);
		break;

	case HINIC_PORT_CMD_DEL_MAC:
		err = hinic_del_vf_mac_msg_handler(nic_io, vf_id, buf_in,
						   in_size, buf_out, out_size);
		break;

	case HINIC_PORT_CMD_UPDATE_MAC:
		err = hinic_update_vf_mac_msg_handler(nic_io, vf_id, buf_in,
						      in_size, buf_out,
						      out_size);
		break;

	case HINIC_PORT_CMD_GET_LINK_STATE:
		hinic_get_vf_link_status_msg_handler(nic_io, vf_id, buf_out,
						     out_size);
		break;

	case HINIC_PORT_CMD_GET_VF_COS:
		hinic_get_vf_cos_msg_handler(nic_io, vf_id, buf_out, out_size);
		break;

	default:
		/* pass through */
		if (cmd == HINIC_PORT_CMD_SET_VPORT_ENABLE)
			timeout = SET_VPORT_MGMT_TIMEOUT;

		err = hinic_pf_msg_to_mgmt_sync(nic_io->hwdev, HINIC_MOD_L2NIC,
						cmd, buf_in, in_size,
						buf_out, out_size, timeout);

		break;
	}

	if (err && err != HINIC_DEV_BUSY_ACTIVE_FW &&
	    err != HINIC_MBOX_PF_BUSY_ACTIVE_FW)
		nic_err(nic_io->hwdev->dev_hdl, "PF receive VF L2NIC cmd: %d process error, err:%d\n",
			cmd, err);
	return err;
}

static int hinic_init_vf_infos(struct hinic_nic_io *nic_io, u16 vf_id)
{
	struct vf_data_storage *vf_infos = nic_io->vf_infos;
	u8 vf_link_state;

	if (set_vf_link_state > HINIC_IFLA_VF_LINK_STATE_DISABLE) {
		nic_warn(nic_io->hwdev->dev_hdl, "Module Parameter set_vf_link_state value %d is out of range, resetting to %d\n",
			 set_vf_link_state, HINIC_IFLA_VF_LINK_STATE_AUTO);
		set_vf_link_state = HINIC_IFLA_VF_LINK_STATE_AUTO;
	}

	vf_link_state = hinic_support_ovs(nic_io->hwdev, NULL) ?
			HINIC_IFLA_VF_LINK_STATE_ENABLE : set_vf_link_state;

	if (FUNC_FORCE_LINK_UP(nic_io->hwdev))
		vf_link_state = HINIC_IFLA_VF_LINK_STATE_ENABLE;

	switch (vf_link_state) {
	case HINIC_IFLA_VF_LINK_STATE_AUTO:
		vf_infos[vf_id].link_forced = false;
		break;
	case HINIC_IFLA_VF_LINK_STATE_ENABLE:
		vf_infos[vf_id].link_forced = true;
		vf_infos[vf_id].link_up = true;
		break;
	case HINIC_IFLA_VF_LINK_STATE_DISABLE:
		vf_infos[vf_id].link_forced = true;
		vf_infos[vf_id].link_up = false;
		break;
	default:
		nic_err(nic_io->hwdev->dev_hdl, "Input parameter set_vf_link_state error: %d\n",
			vf_link_state);
		return -EINVAL;
	}

	return 0;
}

int hinic_init_vf_hw(void *hwdev, u16 start_vf_id, u16 end_vf_id)
{
	u16 i, func_idx;
	int err;

	/* vf use 256K as default wq page size, and can't change it */
	for (i = start_vf_id; i <= end_vf_id; i++) {
		func_idx = hinic_glb_pf_vf_offset(hwdev) + i;
		err = hinic_set_wq_page_size(hwdev, func_idx,
					     HINIC_DEFAULT_WQ_PAGE_SIZE);
		if (err)
			return err;
	}

	return 0;
}

int hinic_deinit_vf_hw(void *hwdev, u16 start_vf_id, u16 end_vf_id)
{
	u16 func_idx, idx;

	for (idx = start_vf_id; idx <= end_vf_id; idx++) {
		func_idx = hinic_glb_pf_vf_offset(hwdev) + idx;
		hinic_set_wq_page_size(hwdev, func_idx, HINIC_HW_WQ_PAGE_SIZE);

		hinic_clear_vf_infos(hwdev, idx);
	}

	return 0;
}

int hinic_vf_func_init(struct hinic_hwdev *hwdev)
{
	struct hinic_nic_io *nic_io;
	int err = 0;
	struct hinic_register_vf register_info = {0};
	u32 size;
	u16 i, out_size = sizeof(register_info);

	hwdev->nic_io = kzalloc(sizeof(*hwdev->nic_io), GFP_KERNEL);
	if (!hwdev->nic_io)
		return -ENOMEM;

	nic_io = hwdev->nic_io;
	nic_io->hwdev = hwdev;

	sema_init(&nic_io->nic_cfg.cfg_lock, 1);

	if (hinic_func_type(hwdev) == TYPE_VF) {
		err = hinic_mbox_to_pf(hwdev, HINIC_MOD_L2NIC,
				       HINIC_PORT_CMD_VF_REGISTER,
				       &register_info, sizeof(register_info),
				       &register_info, &out_size, 0);
		if (err || register_info.status || !out_size) {
			nic_err(hwdev->dev_hdl, "Failed to register VF, err: %d, status: 0x%x, out size: 0x%x\n",
				err, register_info.status, out_size);
			hinic_unregister_vf_mbox_cb(hwdev, HINIC_MOD_L2NIC);
			err = -EIO;
			goto out_free_nic_io;
		}
	} else {
		nic_io->max_vfs = hinic_func_max_vf(hwdev);
		size = sizeof(*nic_io->vf_infos) * nic_io->max_vfs;
		if (size != 0) {
			nic_io->vf_infos = kzalloc(size, GFP_KERNEL);
			if (!nic_io->vf_infos) {
				err = -ENOMEM;
				goto out_free_nic_io;
			}

			for (i = 0; i < nic_io->max_vfs; i++) {
				err = hinic_init_vf_infos(nic_io, i);
				if (err)
					goto init_vf_infos_err;
			}

			err = hinic_register_pf_mbox_cb(hwdev, HINIC_MOD_L2NIC,
							nic_pf_mbox_handler);
			if (err)
				goto register_pf_mbox_cb_err;
		}
	}

	return 0;

register_pf_mbox_cb_err:
init_vf_infos_err:
	kfree(nic_io->vf_infos);

out_free_nic_io:
	sema_deinit(&hwdev->nic_io->nic_cfg.cfg_lock);
	kfree(hwdev->nic_io);
	hwdev->nic_io = NULL;

	return err;
}

void hinic_vf_func_free(struct hinic_hwdev *hwdev)
{
	struct hinic_register_vf unregister = {0};
	u16 out_size = sizeof(unregister);
	int err;

	if (hinic_func_type(hwdev) == TYPE_VF) {
		err = hinic_mbox_to_pf(hwdev, HINIC_MOD_L2NIC,
				       HINIC_PORT_CMD_VF_UNREGISTER,
				       &unregister, sizeof(unregister),
				       &unregister, &out_size, 0);
		if (err || !out_size || unregister.status)
			nic_err(hwdev->dev_hdl, "Failed to unregister VF, err: %d, status: 0x%x, out_size: 0x%x\n",
				err, unregister.status, out_size);
	} else {
		if (hwdev->nic_io->vf_infos) {
			hinic_unregister_pf_mbox_cb(hwdev, HINIC_MOD_L2NIC);
			kfree(hwdev->nic_io->vf_infos);
		}
	}

	sema_deinit(&hwdev->nic_io->nic_cfg.cfg_lock);

	kfree(hwdev->nic_io);
	hwdev->nic_io = NULL;
}

/* this function just be called by hinic_ndo_set_vf_mac, others are
 * not permitted
 */
int hinic_set_vf_mac(void *hwdev, int vf, unsigned char *mac_addr)
{
	struct hinic_hwdev *hw_dev = (struct hinic_hwdev *)hwdev;
	struct hinic_nic_io *nic_io = hw_dev->nic_io;
	struct vf_data_storage *vf_info = nic_io->vf_infos + HW_VF_ID_TO_OS(vf);
	u16 func_id;
	int err;

	/* duplicate request, so just return success */
	if (vf_info->pf_set_mac &&
	    !memcmp(vf_info->vf_mac_addr, mac_addr, ETH_ALEN))
		return 0;

	vf_info->pf_set_mac = true;

	func_id = hinic_glb_pf_vf_offset(hw_dev) + vf;
	err = hinic_update_mac(hw_dev, vf_info->vf_mac_addr,
			       mac_addr, 0, func_id);
	if (err) {
		vf_info->pf_set_mac = false;
		return err;
	}

	memcpy(vf_info->vf_mac_addr, mac_addr, ETH_ALEN);

	return 0;
}

int hinic_add_vf_vlan(void *hwdev, int vf_id, u16 vlan, u8 qos)
{
	struct hinic_hwdev *hw_dev = (struct hinic_hwdev *)hwdev;
	struct hinic_nic_io *nic_io = hw_dev->nic_io;
	int err;

	err = hinic_set_vf_vlan(hw_dev, true, vlan, qos, vf_id);
	if (err)
		return err;

	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_vlan = vlan;
	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_qos = qos;

	nic_info(hw_dev->dev_hdl, "Setting VLAN %d, QOS 0x%x on VF %d\n",
		 vlan, qos, HW_VF_ID_TO_OS(vf_id));
	return 0;
}

int hinic_kill_vf_vlan(void *hwdev, int vf_id)
{
	struct hinic_hwdev *hw_dev = (struct hinic_hwdev *)hwdev;
	struct hinic_nic_io *nic_io = hw_dev->nic_io;
	int err;

	err = hinic_set_vf_vlan(hw_dev, false,
				nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_vlan,
				nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_qos,
				vf_id);
	if (err)
		return err;

	nic_info(hw_dev->dev_hdl, "Remove VLAN %d on VF %d\n",
		 nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_vlan,
		 HW_VF_ID_TO_OS(vf_id));

	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_vlan = 0;
	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_qos = 0;

	return 0;
}

u16 hinic_vf_info_vlanprio(void *hwdev, int vf_id)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	u16 pf_vlan = nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_vlan;
	u8 pf_qos = nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].pf_qos;
	u16 vlanprio = pf_vlan | pf_qos << HINIC_VLAN_PRIORITY_SHIFT;

	return vlanprio;
}

bool hinic_vf_is_registered(void *hwdev, u16 vf_id)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;

	return nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].registered;
}

void hinic_get_vf_config(void *hwdev, u16 vf_id, struct ifla_vf_info *ivi)
{
	struct hinic_hwdev *hw_dev = (struct hinic_hwdev *)hwdev;
	struct vf_data_storage *vfinfo;

	vfinfo = hw_dev->nic_io->vf_infos + HW_VF_ID_TO_OS(vf_id);

	ivi->vf = HW_VF_ID_TO_OS(vf_id);
	memcpy(ivi->mac, vfinfo->vf_mac_addr, ETH_ALEN);
	ivi->vlan = vfinfo->pf_vlan;
	ivi->qos = vfinfo->pf_qos;

#ifdef HAVE_VF_SPOOFCHK_CONFIGURE
	ivi->spoofchk = vfinfo->spoofchk;
#endif

#ifdef HAVE_NDO_SET_VF_TRUST
	ivi->trusted = vfinfo->trust;
#endif

#ifdef HAVE_NDO_SET_VF_MIN_MAX_TX_RATE
	ivi->max_tx_rate = vfinfo->max_rate;
	ivi->min_tx_rate = vfinfo->min_rate;
#else
	ivi->tx_rate = vfinfo->max_rate;
#endif /* HAVE_NDO_SET_VF_MIN_MAX_TX_RATE */

#ifdef HAVE_NDO_SET_VF_LINK_STATE
	if (!vfinfo->link_forced)
		ivi->linkstate = IFLA_VF_LINK_STATE_AUTO;
	else if (vfinfo->link_up)
		ivi->linkstate = IFLA_VF_LINK_STATE_ENABLE;
	else
		ivi->linkstate = IFLA_VF_LINK_STATE_DISABLE;
#endif
}

void hinic_clear_vf_infos(void *hwdev, u16 vf_id)
{
	struct hinic_hwdev *hw_dev = (struct hinic_hwdev *)hwdev;
	struct vf_data_storage *vf_infos;
	u16 func_id;

	func_id = hinic_glb_pf_vf_offset(hwdev) + vf_id;
	vf_infos = hw_dev->nic_io->vf_infos + HW_VF_ID_TO_OS(vf_id);
	if (vf_infos->pf_set_mac)
		hinic_del_mac(hwdev, vf_infos->vf_mac_addr, 0, func_id);

	if (hinic_vf_info_vlanprio(hwdev, vf_id))
		hinic_kill_vf_vlan(hwdev, vf_id);

	if (vf_infos->max_rate)
		hinic_set_vf_tx_rate(hwdev, vf_id, 0, 0);

#ifdef HAVE_VF_SPOOFCHK_CONFIGURE
	if (vf_infos->spoofchk)
		hinic_set_vf_spoofchk(hwdev, vf_id, false);
#endif

#ifdef HAVE_NDO_SET_VF_TRUST
	if (vf_infos->trust)
		hinic_set_vf_trust(hwdev, vf_id, false);
#endif

	memset(vf_infos, 0, sizeof(*vf_infos));
	/* set vf_infos to default */
	hinic_init_vf_infos(hw_dev->nic_io, HW_VF_ID_TO_OS(vf_id));
}

static void hinic_notify_vf_link_status(struct hinic_hwdev *hwdev, u16 vf_id,
					u8 link_status)
{
	struct hinic_port_link_status link = {0};
	struct vf_data_storage *vf_infos = hwdev->nic_io->vf_infos;
	u16 out_size = sizeof(link);
	int err;

	if (vf_infos[HW_VF_ID_TO_OS(vf_id)].registered) {
		link.link = link_status;
		link.func_id = hinic_glb_pf_vf_offset(hwdev) + vf_id;
		err = hinic_mbox_to_vf(hwdev, HINIC_MOD_L2NIC,
				       vf_id, HINIC_PORT_CMD_LINK_STATUS_REPORT,
				       &link, sizeof(link),
				       &link, &out_size, 0);
		if (err || !out_size || link.status)
			nic_err(hwdev->dev_hdl,
				"Send link change event to VF %d failed, err: %d, status: 0x%x, out_size: 0x%x\n",
				HW_VF_ID_TO_OS(vf_id), err,
				link.status, out_size);
	}
}

/* send link change event mbox msg to active vfs under the pf */
void hinic_notify_all_vfs_link_changed(void *hwdev, u8 link_status)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	u16 i;

	nic_io->link_status = link_status;
	for (i = 1; i <= nic_io->max_vfs; i++) {
		if (!nic_io->vf_infos[HW_VF_ID_TO_OS(i)].link_forced)
			hinic_notify_vf_link_status(nic_io->hwdev, i,
						    link_status);
	}
}

void hinic_save_pf_link_status(void *hwdev, u8 link_status)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;

	nic_io->link_status = link_status;
}

int hinic_set_vf_link_state(void *hwdev, u16 vf_id, int link)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	struct vf_data_storage *vf_infos = nic_io->vf_infos;
	u8 link_status = 0;

	switch (link) {
	case HINIC_IFLA_VF_LINK_STATE_AUTO:
		vf_infos[HW_VF_ID_TO_OS(vf_id)].link_forced = false;
		vf_infos[HW_VF_ID_TO_OS(vf_id)].link_up = nic_io->link_status ?
			true : false;
		link_status = nic_io->link_status;
		break;
	case HINIC_IFLA_VF_LINK_STATE_ENABLE:
		vf_infos[HW_VF_ID_TO_OS(vf_id)].link_forced = true;
		vf_infos[HW_VF_ID_TO_OS(vf_id)].link_up = true;
		link_status = HINIC_LINK_UP;
		break;
	case HINIC_IFLA_VF_LINK_STATE_DISABLE:
		vf_infos[HW_VF_ID_TO_OS(vf_id)].link_forced = true;
		vf_infos[HW_VF_ID_TO_OS(vf_id)].link_up = false;
		link_status = HINIC_LINK_DOWN;
		break;
	default:
		return -EINVAL;
	}

	/* Notify the VF of its new link state */
	hinic_notify_vf_link_status(hwdev, vf_id, link_status);

	return 0;
}

#ifdef HAVE_VF_SPOOFCHK_CONFIGURE
int hinic_set_vf_spoofchk(void *hwdev, u16 vf_id, bool spoofchk)
{
	struct hinic_hwdev *hw_dev = hwdev;
	struct hinic_nic_io *nic_io = NULL;
	struct hinic_spoofchk_set spoofchk_cfg = {0};
	struct vf_data_storage *vf_infos = NULL;
	u16 out_size = sizeof(spoofchk_cfg);
	int err = 0;

	if (!hwdev)
		return -EINVAL;

	nic_io = hw_dev->nic_io;
	vf_infos = nic_io->vf_infos;

	spoofchk_cfg.func_id = hinic_glb_pf_vf_offset(hwdev) + vf_id;
	spoofchk_cfg.state = spoofchk ? 1 : 0;
	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_L2NIC,
				     HINIC_PORT_CMD_ENABLE_SPOOFCHK,
				     &spoofchk_cfg,
				     sizeof(spoofchk_cfg), &spoofchk_cfg,
				     &out_size, 0);
	if (spoofchk_cfg.status == HINIC_MGMT_CMD_UNSUPPORTED) {
		err = HINIC_MGMT_CMD_UNSUPPORTED;
	} else if (err || !out_size || spoofchk_cfg.status) {
		nic_err(hw_dev->dev_hdl, "Failed to set VF(%d) spoofchk, err: %d, status: 0x%x, out size: 0x%x\n",
			HW_VF_ID_TO_OS(vf_id), err, spoofchk_cfg.status,
			out_size);
		err = -EINVAL;
	}

	vf_infos[HW_VF_ID_TO_OS(vf_id)].spoofchk = spoofchk;

	return err;
}
#endif

#ifdef HAVE_NDO_SET_VF_TRUST
int hinic_set_vf_trust(void *hwdev, u16 vf_id, bool trust)
{
	struct hinic_hwdev *hw_dev = hwdev;
	struct hinic_nic_io *nic_io = NULL;
	struct vf_data_storage *vf_infos = NULL;

	if (!hwdev)
		return -EINVAL;

	nic_io = hw_dev->nic_io;
	vf_infos = nic_io->vf_infos;
	vf_infos[HW_VF_ID_TO_OS(vf_id)].trust = trust;

	return 0;
}
#endif

#ifdef HAVE_VF_SPOOFCHK_CONFIGURE
bool hinic_vf_info_spoofchk(void *hwdev, int vf_id)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	bool spoofchk = nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].spoofchk;

	return spoofchk;
}
#endif

#ifdef HAVE_NDO_SET_VF_TRUST
bool hinic_vf_info_trust(void *hwdev, int vf_id)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	bool trust = nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].trust;

	return trust;
}
#endif

static int hinic_set_vf_rate_limit(void *hwdev, u16 vf_id, u32 tx_rate)
{
	struct hinic_hwdev *hw_dev = hwdev;
	struct hinic_nic_io *nic_io = hw_dev->nic_io;
	struct hinic_tx_rate_cfg rate_cfg = {0};
	u16 out_size = sizeof(rate_cfg);
	int err;

	rate_cfg.func_id = hinic_glb_pf_vf_offset(hwdev) + vf_id;
	rate_cfg.tx_rate = tx_rate;
	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_L2NIC,
				     HINIC_PORT_CMD_SET_VF_RATE, &rate_cfg,
				     sizeof(rate_cfg), &rate_cfg,
				     &out_size, 0);
	if (err || !out_size || rate_cfg.status) {
		nic_err(hw_dev->dev_hdl, "Failed to set VF(%d) rate(%d), err: %d, status: 0x%x, out size: 0x%x\n",
			HW_VF_ID_TO_OS(vf_id), tx_rate, err, rate_cfg.status,
			out_size);
		if (rate_cfg.status)
			return rate_cfg.status;

		return -EIO;
	}

	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].max_rate = tx_rate;
	nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].min_rate = 0;

	return 0;
}

static int hinic_set_vf_tx_rate_max_min(void *hwdev, u16 vf_id,
					u32 max_rate, u32 min_rate)
{
	struct hinic_hwdev *hw_dev = hwdev;
	struct hinic_nic_io *nic_io = hw_dev->nic_io;
	struct hinic_tx_rate_cfg_max_min rate_cfg = {0};
	u16 out_size = sizeof(rate_cfg);
	int err;

	rate_cfg.func_id = hinic_glb_pf_vf_offset(hwdev) + vf_id;
	rate_cfg.max_rate = max_rate;
	rate_cfg.min_rate = min_rate;
	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_L2NIC,
				     HINIC_PORT_CMD_SET_VF_MAX_MIN_RATE,
				     &rate_cfg, sizeof(rate_cfg), &rate_cfg,
				     &out_size, 0);
	if ((rate_cfg.status != HINIC_MGMT_CMD_UNSUPPORTED &&
	     rate_cfg.status) || err || !out_size) {
		nic_err(hw_dev->dev_hdl, "Failed to set VF(%d) max rate(%d), min rate(%d), err: %d, status: 0x%x, out size: 0x%x\n",
			HW_VF_ID_TO_OS(vf_id), max_rate, min_rate, err,
			rate_cfg.status, out_size);
		return -EIO;
	}

	if (!rate_cfg.status) {
		nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].max_rate = max_rate;
		nic_io->vf_infos[HW_VF_ID_TO_OS(vf_id)].min_rate = min_rate;
	}

	return rate_cfg.status;
}

int hinic_set_vf_tx_rate(void *hwdev, u16 vf_id, u32 max_rate, u32 min_rate)
{
	struct hinic_hwdev *hw_dev = hwdev;
	int err;

	err = hinic_set_vf_tx_rate_max_min(hwdev, vf_id, max_rate, min_rate);
	if (err != HINIC_MGMT_CMD_UNSUPPORTED)
		return err;

	if (min_rate) {
		nic_err(hw_dev->dev_hdl, "Current firmware don't support to set min tx rate\n");
		return -EINVAL;
	}

	nic_info(hw_dev->dev_hdl, "Current firmware don't support to set min tx rate, force min_tx_rate = max_tx_rate\n");

	return hinic_set_vf_rate_limit(hwdev, vf_id, max_rate);
}

int hinic_set_dcb_state(void *hwdev, struct hinic_dcb_state *dcb_state)
{
	struct hinic_hwdev *hw_dev = hwdev;
	struct hinic_nic_io *nic_io;
	struct vf_data_storage *vf_infos;
	struct hinic_vf_dcb_state vf_dcb = {0};
	u16 vf_id, out_size = 0;
	int err;

	if (!hwdev || !dcb_state || !hw_dev->nic_io)
		return -EINVAL;

	nic_io = hw_dev->nic_io;
	if (!memcmp(&nic_io->dcb_state, dcb_state, sizeof(nic_io->dcb_state)))
		return 0;

	memcpy(&vf_dcb.state, dcb_state, sizeof(vf_dcb.state));
	/* save in sdk, vf will get dcb state when probing */
	hinic_save_dcb_state(hwdev, dcb_state);

	/* notify statefull in pf, than notify all vf */
	hinic_notify_dcb_state_event(hwdev, dcb_state);

	/* not vf supported, don't need to notify vf */
	if (!nic_io->vf_infos)
		return 0;

	vf_infos = nic_io->vf_infos;
	for (vf_id = 0; vf_id < nic_io->max_vfs; vf_id++) {
		if (vf_infos[vf_id].registered) {
			vf_dcb.status = 0;
			out_size = sizeof(vf_dcb);
			err = hinic_mbox_to_vf(hwdev, HINIC_MOD_L2NIC,
					       OS_VF_ID_TO_HW(vf_id),
					       HINIC_PORT_CMD_SET_VF_COS,
					       &vf_dcb, sizeof(vf_dcb), &vf_dcb,
					       &out_size, 0);
			if (err || vf_dcb.status || !out_size)
				nic_err(hw_dev->dev_hdl,
					"Failed to notify dcb state to VF %d, err: %d, status: 0x%x, out size: 0x%x\n",
					vf_id, err, vf_dcb.status, out_size);
		}
	}

	return 0;
}

int hinic_get_dcb_state(void *hwdev, struct hinic_dcb_state *dcb_state)
{
	struct hinic_hwdev *hw_dev = hwdev;
	struct hinic_nic_io *nic_io;

	if (!hwdev || !dcb_state)
		return -EINVAL;

	nic_io = hw_dev->nic_io;
	memcpy(dcb_state, &nic_io->dcb_state, sizeof(*dcb_state));

	return 0;
}
EXPORT_SYMBOL(hinic_get_dcb_state);

int hinic_save_dcb_state(struct hinic_hwdev *hwdev,
			 struct hinic_dcb_state *dcb_state)
{
	struct hinic_nic_io *nic_io;

	if (!hwdev || !dcb_state)
		return -EINVAL;

	if (!hwdev->nic_io)
		return -EINVAL;

	nic_io = hwdev->nic_io;
	memcpy(&nic_io->dcb_state, dcb_state, sizeof(*dcb_state));

	return 0;
}

int hinic_get_pf_dcb_state(void *hwdev, struct hinic_dcb_state *dcb_state)
{
	struct hinic_hwdev *hw_dev = hwdev;
	struct hinic_vf_dcb_state vf_dcb = {0};
	u16 out_size = sizeof(vf_dcb);
	int err;

	if (!hwdev || !dcb_state)
		return -EINVAL;

	if (hinic_func_type(hwdev) != TYPE_VF) {
		nic_err(hw_dev->dev_hdl, "Only vf need to get pf dcb state\n");
		return -EINVAL;
	}

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_L2NIC,
				     HINIC_PORT_CMD_GET_VF_COS, &vf_dcb,
				     sizeof(vf_dcb), &vf_dcb,
				     &out_size, 0);
	if (err || !out_size || vf_dcb.status) {
		nic_err(hw_dev->dev_hdl, "Failed to get vf default cos, err: %d, status: 0x%x, out size: 0x%x\n",
			err, vf_dcb.status, out_size);
		return -EFAULT;
	}

	memcpy(dcb_state, &vf_dcb.state, sizeof(*dcb_state));
	/* Save dcb_state in hw for statefull module */
	hinic_save_dcb_state(hwdev, dcb_state);

	return 0;
}
EXPORT_SYMBOL(hinic_get_pf_dcb_state);

int hinic_set_ipsu_mac(void *hwdev, u16 index, u8 *mac_addr, u16 vlan_id,
		       u16 func_id)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_port_ipsu_mac mac_info = {0};
	u16 out_size = sizeof(mac_info);
	int err;

	if (!hwdev || !mac_addr)
		return -EINVAL;

	mac_info.index = index;
	mac_info.func_id = func_id;
	mac_info.vlan_id = vlan_id;
	memcpy(mac_info.mac, mac_addr, ETH_ALEN);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_IPSU_MAC,
				     &mac_info, sizeof(mac_info), &mac_info,
				     &out_size);
	if (err || !out_size || mac_info.status) {
		nic_err(nic_hwdev->dev_hdl,
			"Failed to set IPSU MAC(index %d), err: %d, status: 0x%x, out size: 0x%x\n",
			index, err, mac_info.status, out_size);
		return -EINVAL;
	}

	return 0;
}

int hinic_get_ipsu_mac(void *hwdev, u16 index, u8 *mac_addr, u16 *vlan_id,
		       u16 *func_id)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_port_ipsu_mac mac_info = {0};
	u16 out_size = sizeof(mac_info);
	int err;

	if (!hwdev || !mac_addr)
		return -EINVAL;

	mac_info.index = index;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_IPSU_MAC,
				     &mac_info, sizeof(mac_info), &mac_info,
				     &out_size);
	if (err || !out_size || mac_info.status) {
		nic_err(nic_hwdev->dev_hdl,
			"Failed to get IPSU MAC(index %d), err: %d, status: 0x%x, out size: 0x%x\n",
			index, err, mac_info.status, out_size);
		return -EINVAL;
	}
	*func_id = mac_info.func_id;
	*vlan_id = mac_info.vlan_id;
	memcpy(mac_addr, mac_info.mac, ETH_ALEN);

	return 0;
}

int hinic_set_anti_attack(void *hwdev, bool enable)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_port_anti_attack_rate rate = {0};
	u16 out_size = sizeof(rate);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &rate.func_id);
	if (err)
		return err;

	rate.enable = enable;
	rate.cir = ANTI_ATTACK_DEFAULT_CIR;
	rate.xir = ANTI_ATTACK_DEFAULT_XIR;
	rate.cbs = ANTI_ATTACK_DEFAULT_CBS;
	rate.xbs = ANTI_ATTACK_DEFAULT_XBS;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_ANTI_ATTACK_RATE,
				     &rate, sizeof(rate), &rate,
				     &out_size);
	if (err || !out_size || rate.status) {
		nic_err(nic_hwdev->dev_hdl, "Can`t %s port Anti-Attack rate limit err: %d, status: 0x%x, out size: 0x%x\n",
			(enable ? "enable" : "disable"), err, rate.status,
			out_size);
		return -EINVAL;
	}

	nic_info(nic_hwdev->dev_hdl, "%s port Anti-Attack rate limit succeed.\n",
		 (enable ? "Enable" : "Disable"));

	return 0;
}

int hinic_flush_sq_res(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_clear_sq_resource sq_res = {0};
	u16 out_size = sizeof(sq_res);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &sq_res.func_id);
	if (err)
		return err;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_CLEAR_SQ_RES,
				     &sq_res, sizeof(sq_res), &sq_res,
				     &out_size);
	if (err || !out_size || sq_res.status) {
		nic_err(dev->dev_hdl, "Failed to clear sq resources, err: %d, status: 0x%x, out size: 0x%x\n",
			err, sq_res.status, out_size);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_flush_sq_res);

static int __set_pf_bw(struct hinic_hwdev *hwdev, u8 speed_level);

int hinic_refresh_nic_cfg(void *hwdev, struct nic_port_info *port_info)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_nic_cfg *nic_cfg = &dev->nic_io->nic_cfg;
	int err = 0;

	down(&nic_cfg->cfg_lock);

	/* Enable PFC will disable pause */
	if (nic_cfg->pfc_en) {
		err = hinic_dcb_set_hw_pfc(hwdev, nic_cfg->pfc_en,
					   nic_cfg->pfc_bitmap);
		if (err)
			nic_err(dev->dev_hdl, "Failed to set pfc\n");

	} else if (!port_info->autoneg_state || nic_cfg->pause_set) {
		nic_cfg->nic_pause.auto_neg = port_info->autoneg_state;
		err = hinic_set_hw_pause_info(hwdev, nic_cfg->nic_pause);
		if (err)
			nic_err(dev->dev_hdl, "Failed to set pause\n");
	}

	if (FUNC_SUPPORT_RATE_LIMIT(hwdev)) {
		err = __set_pf_bw(hwdev, port_info->speed);
		if (err)
			nic_err(dev->dev_hdl, "Failed to set pf bandwidth limit\n");
	}

	up(&nic_cfg->cfg_lock);

	return err;
}

int hinic_set_super_cqe_state(void *hwdev, bool enable)
{
	struct hinic_hwdev *nic_hwdev = (struct hinic_hwdev *)hwdev;
	struct hinic_super_cqe super_cqe = {0};
	u16 out_size = sizeof(super_cqe);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &super_cqe.func_id);
	if (err)
		return err;

	super_cqe.super_cqe_en = enable;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_SUPER_CQE,
				     &super_cqe, sizeof(super_cqe), &super_cqe,
				     &out_size);
	if (err || !out_size || super_cqe.status) {
		nic_err(nic_hwdev->dev_hdl, "Can`t %s surper cqe, err: %d, status: 0x%x, out size: 0x%x\n",
			(enable ? "enable" : "disable"), err, super_cqe.status,
			out_size);
		return -EINVAL;
	}

	nic_info(nic_hwdev->dev_hdl, "%s super cqe succeed.\n",
		 (enable ? "Enable" : "Disable"));

	return 0;
}

int hinic_set_port_routine_cmd_report(void *hwdev, bool enable)
{
	struct hinic_port_rt_cmd rt_cmd = { 0 };
	struct hinic_hwdev *dev = hwdev;
	u16 out_size = sizeof(rt_cmd);
	int err;

	if (!hwdev)
		return -EINVAL;

	rt_cmd.pf_id = (u8)hinic_global_func_id(hwdev);
	rt_cmd.enable = enable;

	err = l2nic_msg_to_mgmt_sync(hwdev,
				     HINIC_PORT_CMD_SET_PORT_REPORT,
				     &rt_cmd, sizeof(rt_cmd), &rt_cmd,
				     &out_size);
	if (rt_cmd.status == HINIC_MGMT_CMD_UNSUPPORTED) {
		nic_info(dev->dev_hdl, "Current firmware doesn't support to set port routine command report\n");
	} else if (rt_cmd.status || err || !out_size) {
		nic_err(dev->dev_hdl,
			"Failed to set port routine command report, err: %d, status: 0x%x, out size: 0x%x\n",
			err, rt_cmd.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int hinic_set_func_capture_en(void *hwdev, u16 func_id, bool cap_en)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_capture_info cap_info = {0};
	u16 out_size = sizeof(cap_info);
	int err;

	if (!hwdev)
		return -EINVAL;

	cap_info.op_type = 2;	/* function capture */
	cap_info.is_en_trx = cap_en;
	cap_info.func_id = func_id;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_UCAPTURE_OPT,
				     &cap_info, sizeof(cap_info),
				     &cap_info, &out_size);
	if (err || !out_size || cap_info.status) {
		nic_err(dev->dev_hdl,
			"Failed to set function capture attr, err: %d, status: 0x%x, out size: 0x%x\n",
			err, cap_info.status, out_size);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_set_func_capture_en);

int hinic_force_drop_tx_pkt(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_force_pkt_drop pkt_drop = {0};
	u16 out_size = sizeof(pkt_drop);
	int err;

	if (!hwdev)
		return -EINVAL;

	pkt_drop.port = hinic_physical_port_id(hwdev);

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_FORCE_PKT_DROP,
				     &pkt_drop, sizeof(pkt_drop),
				     &pkt_drop, &out_size);
	if ((pkt_drop.status != HINIC_MGMT_CMD_UNSUPPORTED &&
	     pkt_drop.status) || err || !out_size) {
		nic_err(dev->dev_hdl,
			"Failed to set force tx packets drop, err: %d, status: 0x%x, out size: 0x%x\n",
			err, pkt_drop.status, out_size);
		return -EFAULT;
	}

	return pkt_drop.status;
}

u32 hw_speed_convert[LINK_SPEED_LEVELS] = {
	10, 100, 1000, 10000,
	25000, 40000, 100000
};

static int __set_pf_bw(struct hinic_hwdev *hwdev, u8 speed_level)
{
	struct hinic_nic_cfg *nic_cfg = &hwdev->nic_io->nic_cfg;
	struct hinic_tx_rate_cfg rate_cfg = {0};
	u32 pf_bw = 0;
	u16 out_size = sizeof(rate_cfg);
	int err;

	if (speed_level >= LINK_SPEED_LEVELS) {
		nic_err(hwdev->dev_hdl, "Invalid speed level: %d\n",
			speed_level);
		return -EINVAL;
	}

	if (nic_cfg->pf_bw_limit == 100) {
		pf_bw = 0;	/* unlimit bandwidth */
	} else {
		pf_bw = (hw_speed_convert[speed_level] / 100) *
			nic_cfg->pf_bw_limit;
		/* bandwidth limit is very small but not unlimit in this case */
		if (pf_bw == 0)
			pf_bw = 1;
	}

	err = hinic_global_func_id_get(hwdev, &rate_cfg.func_id);
	if (err)
		return err;

	rate_cfg.tx_rate = pf_bw;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_L2NIC,
				     HINIC_PORT_CMD_SET_VF_RATE, &rate_cfg,
				     sizeof(rate_cfg), &rate_cfg,
				     &out_size, 0);
	if (err || !out_size || rate_cfg.status) {
		nic_err(hwdev->dev_hdl, "Failed to set rate(%d), err: %d, status: 0x%x, out size: 0x%x\n",
			pf_bw, err, rate_cfg.status, out_size);
		if (rate_cfg.status)
			return rate_cfg.status;

		return -EIO;
	}

	return 0;
}

int hinic_update_pf_bw(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;
	struct nic_port_info port_info = {0};
	int err;

	if (hinic_func_type(hwdev) == TYPE_VF ||
	    !(FUNC_SUPPORT_RATE_LIMIT(hwdev)))
		return 0;

	err = hinic_get_port_info(hwdev, &port_info);
	if (err) {
		nic_err(dev->dev_hdl, "Failed to get port info\n");
		return -EIO;
	}

	err = __set_pf_bw(hwdev, port_info.speed);
	if (err) {
		nic_err(dev->dev_hdl, "Failed to set pf bandwidth\n");
		return err;
	}

	return 0;
}

int hinic_set_pf_bw_limit(void *hwdev, u32 bw_limit)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_nic_cfg *nic_cfg;
	u32 old_bw_limit;
	u8 link_state = 0;
	int err;

	if (!hwdev)
		return -EINVAL;

	if (hinic_func_type(hwdev) == TYPE_VF)
		return 0;

	if (bw_limit > 100) {
		nic_err(dev->dev_hdl, "Invalid bandwidth: %d\n", bw_limit);
		return -EINVAL;
	}

	err = hinic_get_link_state(hwdev, &link_state);
	if (err) {
		nic_err(dev->dev_hdl, "Failed to get link state\n");
		return -EIO;
	}

	if (!link_state) {
		nic_err(dev->dev_hdl, "Link status must be up when set pf tx rate\n");
		return -EINVAL;
	}

	nic_cfg = &dev->nic_io->nic_cfg;
	old_bw_limit = nic_cfg->pf_bw_limit;
	nic_cfg->pf_bw_limit = bw_limit;

	err = hinic_update_pf_bw(hwdev);
	if (err) {
		nic_cfg->pf_bw_limit = old_bw_limit;
		return err;
	}

	return 0;
}

/* Set link status follow port status */
int hinic_set_link_status_follow(void *hwdev,
				 enum hinic_link_follow_status status)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_set_link_follow follow = {0};
	u16 out_size = sizeof(follow);
	int err;

	if (!hwdev)
		return -EINVAL;

	if (status >= HINIC_LINK_FOLLOW_STATUS_MAX) {
		nic_err(dev->dev_hdl,
			"Invalid link follow status: %d\n", status);
		return -EINVAL;
	}

	err = hinic_global_func_id_get(hwdev, &follow.func_id);
	if (err)
		return err;

	follow.follow_status = status;

	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_SET_LINK_FOLLOW,
				     &follow, sizeof(follow), &follow,
				     &out_size);
	if ((follow.status != HINIC_MGMT_CMD_UNSUPPORTED &&
	     follow.status) || err || !out_size) {
		nic_err(dev->dev_hdl,
			"Failed to set link status follow port status, err: %d, status: 0x%x, out size: 0x%x\n",
			err, follow.status, out_size);
		return -EFAULT;
	}

	return follow.status;
}
EXPORT_SYMBOL(hinic_set_link_status_follow);

/* HILINK module */

#define HINIC_MGMT_DEFAULT_SIZE		1

static int __hilink_msg_to_mgmt_sync(void *hwdev, u8 cmd, void *buf_in,
				     u16 in_size, void *buf_out, u16 *out_size,
				     u32 timeout)
{
	int err;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_HILINK, cmd, buf_in,
				     in_size, buf_out, out_size, timeout);
	if (err)
		return err;

	if (*out_size == HINIC_MGMT_DEFAULT_SIZE && buf_out)
		*((u8 *)(buf_out)) = HINIC_MGMT_CMD_UNSUPPORTED;

	return 0;
}

int hinic_get_hilink_link_info(void *hwdev, struct hinic_link_info *info)
{
	struct hinic_hilink_link_info link_info = {0};
	u16 out_size = sizeof(link_info);
	int err;

	link_info.port_id = hinic_physical_port_id(hwdev);

	err = __hilink_msg_to_mgmt_sync(hwdev, HINIC_HILINK_CMD_GET_LINK_INFO,
					&link_info, sizeof(link_info),
					&link_info, &out_size, 0);
	if ((link_info.status != HINIC_MGMT_CMD_UNSUPPORTED &&
	     link_info.status) || err || !out_size) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to get hilink info, err: %d, status: 0x%x, out size: 0x%x\n",
			err, link_info.status, out_size);
		return -EFAULT;
	}

	if (!link_info.status)
		memcpy(info, &link_info.info, sizeof(*info));
	else if (link_info.status == HINIC_MGMT_CMD_UNSUPPORTED)
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Unsupported command: mod: %d, cmd: %d\n",
			HINIC_MOD_HILINK, HINIC_HILINK_CMD_GET_LINK_INFO);

	return link_info.status;
}

int hinic_set_link_settings(void *hwdev, struct hinic_link_ksettings *settings)
{
	struct hinic_link_ksettings_info info = {0};
	u16 out_size = sizeof(info);
	int err;

	err = hinic_global_func_id_get(hwdev, &info.func_id);
	if (err)
		return err;

	info.valid_bitmap = settings->valid_bitmap;
	info.autoneg = settings->autoneg;
	info.speed = settings->speed;
	info.fec = settings->fec;

	err = __hilink_msg_to_mgmt_sync(hwdev,
					HINIC_HILINK_CMD_SET_LINK_SETTINGS,
					&info, sizeof(info),
					&info, &out_size, 0);
	if ((info.status != HINIC_MGMT_CMD_UNSUPPORTED &&
	     info.status) || err || !out_size) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to set link settings, err: %d, status: 0x%x, out size: 0x%x\n",
			err, info.status, out_size);
		return -EFAULT;
	}

	return info.status;
}

int hinic_disable_tx_promisc(void *hwdev)
{
	struct hinic_promsic_info info = {0};
	u16 out_size = sizeof(info);
	int err;

	err = hinic_global_func_id_get(hwdev, &info.func_id);
	if (err)
		return err;

	info.cfg = HINIC_TX_PROMISC_DISABLE;
	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_L2NIC,
				     HINIC_PORT_CMD_DISABLE_PROMISIC, &info,
				     sizeof(info), &info, &out_size, 0);
	if (err || !out_size || info.status) {
		if (info.status == HINIC_MGMT_CMD_UNSUPPORTED) {
			nic_info(((struct hinic_hwdev *)hwdev)->dev_hdl,
				 "Unsupported to disable TX promisic\n");
			return 0;
		}
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to disable multihost promisic, err: %d, status: 0x%x, out size: 0x%x\n",
			err, info.status, out_size);
		return -EFAULT;
	}
	return  0;
}


static bool hinic_if_sfp_absent(void *hwdev)
{
	struct card_node *chip_node = ((struct hinic_hwdev *)hwdev)->chip_node;
	struct hinic_port_routine_cmd *rt_cmd;
	struct hinic_cmd_get_light_module_abs sfp_abs = {0};
	u8 port_id = hinic_physical_port_id(hwdev);
	u16 out_size = sizeof(sfp_abs);
	int err;
	bool sfp_abs_vaild;
	bool sfp_abs_status;

	rt_cmd = &chip_node->rt_cmd[port_id];
	mutex_lock(&chip_node->sfp_mutex);
	sfp_abs_vaild = rt_cmd->up_send_sfp_abs;
	sfp_abs_status = (bool)rt_cmd->abs.abs_status;
	if (sfp_abs_vaild) {
		mutex_unlock(&chip_node->sfp_mutex);
		return sfp_abs_status;
	}
	mutex_unlock(&chip_node->sfp_mutex);

	sfp_abs.port_id = port_id;
	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_SFP_ABS,
				     &sfp_abs, sizeof(sfp_abs), &sfp_abs,
				     &out_size);
	if (sfp_abs.status || err || !out_size) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to get port%d sfp absent status, err: %d, status: 0x%x, out size: 0x%x\n",
			port_id, err, sfp_abs.status, out_size);
		return true;
	}

	return ((sfp_abs.abs_status == 0) ? false : true);
}

int hinic_get_sfp_eeprom(void *hwdev, u8 *data, u16 *len)
{
	struct hinic_cmd_get_std_sfp_info sfp_info = {0};
	u8 port_id;
	u16 out_size = sizeof(sfp_info);
	int err;

	if (!hwdev || !data || !len)
		return -EINVAL;

	port_id = hinic_physical_port_id(hwdev);
	if (port_id >= HINIC_MAX_PORT_ID)
		return -EINVAL;

	if (hinic_if_sfp_absent(hwdev))
		return -ENXIO;

	sfp_info.port_id = port_id;
	err = l2nic_msg_to_mgmt_sync(hwdev, HINIC_PORT_CMD_GET_STD_SFP_INFO,
				     &sfp_info, sizeof(sfp_info), &sfp_info,
				     &out_size);
	if (sfp_info.status || err || !out_size) {
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to get port%d sfp eeprom infomation, err: %d, status: 0x%x, out size: 0x%x\n",
			port_id, err, sfp_info.status, out_size);
		return -EIO;
	}

	*len = min_t(u16, sfp_info.eeprom_len, STD_SFP_INFO_MAX_SIZE);
	memcpy(data, sfp_info.sfp_info, STD_SFP_INFO_MAX_SIZE);

	return  0;
}

int hinic_get_sfp_type(void *hwdev, u8 *data0, u8 *data1)
{
	struct card_node *chip_node = NULL;
	struct hinic_port_routine_cmd *rt_cmd;
	u8 sfp_data[STD_SFP_INFO_MAX_SIZE];
	u16 len;
	u8 port_id;
	int err;

	if (!hwdev || !data0 || !data1)
		return -EINVAL;

	port_id = hinic_physical_port_id(hwdev);
	if (port_id >= HINIC_MAX_PORT_ID)
		return -EINVAL;

	if (hinic_if_sfp_absent(hwdev))
		return -ENXIO;

	chip_node = ((struct hinic_hwdev *)hwdev)->chip_node;
	rt_cmd = &chip_node->rt_cmd[port_id];
	mutex_lock(&chip_node->sfp_mutex);
	if (rt_cmd->up_send_sfp_info) {
		*data0 = rt_cmd->sfp_info.sfp_qsfp_info[0];
		*data1 = rt_cmd->sfp_info.sfp_qsfp_info[1];
		mutex_unlock(&chip_node->sfp_mutex);
		return 0;
	}
	mutex_unlock(&chip_node->sfp_mutex);

	err = hinic_get_sfp_eeprom(hwdev, sfp_data, &len);
	if (err)
		return err;

	*data0 = sfp_data[0];
	*data1 = sfp_data[1];

	return 0;
}