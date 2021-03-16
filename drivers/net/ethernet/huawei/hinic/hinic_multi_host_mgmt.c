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

#define pr_fmt(fmt) KBUILD_MODNAME ": [COMM]" fmt

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/semaphore.h>
#include <linux/interrupt.h>

#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hwdev.h"
#include "hinic_csr.h"
#include "hinic_hwif.h"
#include "hinic_nic_io.h"
#include "hinic_api_cmd.h"
#include "hinic_mgmt.h"
#include "hinic_mbox.h"
#include "hinic_nic_cfg.h"
#include "hinic_hwif.h"
#include "hinic_mgmt_interface.h"
#include "hinic_multi_host_mgmt.h"

#define SLAVE_HOST_STATUS_CLEAR(host_id, val)	\
			((val) & (~(1U << (host_id))))
#define SLAVE_HOST_STATUS_SET(host_id, enable)	\
			(((u8)(enable) & 1U) << (host_id))
#define SLAVE_HOST_STATUS_GET(host_id, val)	(!!((val) & (1U << (host_id))))

#define MULTI_HOST_PPF_GET(host_id, val) (((val) >> ((host_id) * 4 + 16)) & 0xf)

static inline u8 get_master_host_ppf_idx(struct hinic_hwdev *hwdev)
{
	u32 reg_val;

	reg_val = hinic_hwif_read_reg(hwdev->hwif,
				      HINIC_MULT_HOST_SLAVE_STATUS_ADDR);
	/* master host sets host_id to 0 */
	return MULTI_HOST_PPF_GET(0, reg_val);
}

void set_slave_host_enable(struct hinic_hwdev *hwdev, u8 host_id, bool enable)
{
	u32 reg_val;

	if (HINIC_FUNC_TYPE(hwdev) != TYPE_PPF)
		return;

	reg_val = hinic_hwif_read_reg(hwdev->hwif,
				      HINIC_MULT_HOST_SLAVE_STATUS_ADDR);

	reg_val = SLAVE_HOST_STATUS_CLEAR(host_id, reg_val);
	reg_val |= SLAVE_HOST_STATUS_SET(host_id, enable);
	hinic_hwif_write_reg(hwdev->hwif, HINIC_MULT_HOST_SLAVE_STATUS_ADDR,
			     reg_val);

	sdk_info(hwdev->dev_hdl, "Set slave host %d status %d, reg value: 0x%x\n",
		 host_id, enable, reg_val);
}

bool hinic_get_slave_host_enable(void *hwdev, u8 host_id)
{
	u32 reg_val;
	struct hinic_hwdev *dev = hwdev;

	if (HINIC_FUNC_TYPE(dev) != TYPE_PPF)
		return false;

	reg_val = hinic_hwif_read_reg(dev->hwif,
				      HINIC_MULT_HOST_SLAVE_STATUS_ADDR);

	return SLAVE_HOST_STATUS_GET(host_id, reg_val);
}
EXPORT_SYMBOL(hinic_get_slave_host_enable);

void set_master_host_mbox_enable(struct hinic_hwdev *hwdev, bool enable)
{
	u32 reg_val;

	if (!IS_MASTER_HOST(hwdev) || HINIC_FUNC_TYPE(hwdev) != TYPE_PPF)
		return;

	reg_val = hinic_hwif_read_reg(hwdev->hwif, HINIC_HOST_MODE_ADDR);
	reg_val = MULTI_HOST_REG_CLEAR(reg_val, MASTER_MBX_STS);
	reg_val |= MULTI_HOST_REG_SET((u8)enable, MASTER_MBX_STS);
	hinic_hwif_write_reg(hwdev->hwif, HINIC_HOST_MODE_ADDR, reg_val);

	sdk_info(hwdev->dev_hdl, "multi-host status %d, reg value: 0x%x\n",
		 enable, reg_val);
}

bool hinic_get_master_host_mbox_enable(void *hwdev)
{
	u32 reg_val;
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	if (!IS_SLAVE_HOST(dev) || HINIC_FUNC_TYPE(dev) == TYPE_VF)
		return true;

	reg_val = hinic_hwif_read_reg(dev->hwif, HINIC_HOST_MODE_ADDR);

	return !!MULTI_HOST_REG_GET(reg_val, MASTER_MBX_STS);
}

void set_func_host_mode(struct hinic_hwdev *hwdev, enum hinic_func_mode mode)
{
	switch (mode) {
	case FUNC_MOD_MULTI_BM_MASTER:
		sdk_info(hwdev->dev_hdl, "Detect multi-host BM master host\n");
		hwdev->func_mode = FUNC_MOD_MULTI_BM_MASTER;
		hwdev->feature_cap = HINIC_MULTI_BM_MASTER;
		break;
	case FUNC_MOD_MULTI_BM_SLAVE:
		sdk_info(hwdev->dev_hdl, "Detect multi-host BM slave host\n");
		hwdev->func_mode = FUNC_MOD_MULTI_BM_SLAVE;
		hwdev->feature_cap = HINIC_MULTI_BM_SLAVE;
		break;
	case FUNC_MOD_MULTI_VM_MASTER:
		sdk_info(hwdev->dev_hdl, "Detect multi-host VM master host\n");
		hwdev->func_mode = FUNC_MOD_MULTI_VM_MASTER;
		hwdev->feature_cap = HINIC_MULTI_VM_MASTER;
		break;
	case FUNC_MOD_MULTI_VM_SLAVE:
		sdk_info(hwdev->dev_hdl, "Detect multi-host VM slave host\n");
		hwdev->func_mode = FUNC_MOD_MULTI_VM_SLAVE;
		hwdev->feature_cap = HINIC_MULTI_VM_SLAVE;
		break;
	default:
		hwdev->func_mode = FUNC_MOD_NORMAL_HOST;
		hwdev->feature_cap = HINIC_NORMAL_HOST_CAP;
		break;
	}
}

bool is_multi_vm_slave(void *hwdev)
{
	struct hinic_hwdev *hw_dev = hwdev;

	if (!hwdev)
		return false;

	return (hw_dev->func_mode == FUNC_MOD_MULTI_VM_SLAVE) ? true : false;
}

bool is_multi_bm_slave(void *hwdev)
{
	struct hinic_hwdev *hw_dev = hwdev;

	if (!hwdev)
		return false;

	return (hw_dev->func_mode == FUNC_MOD_MULTI_BM_SLAVE) ? true : false;
}

int rectify_host_mode(struct hinic_hwdev *hwdev)
{
	u16 cur_sdi_mode;
	int err;

	if (hwdev->board_info.board_type !=
	    HINIC_BOARD_TYPE_MULTI_HOST_ETH_25GE)
		return 0;

	sdk_info(hwdev->dev_hdl, "Rectify host mode, host_id: %d\n",
		 hinic_pcie_itf_id(hwdev));

	err = hinic_get_sdi_mode(hwdev, &cur_sdi_mode);
	if (err == HINIC_MGMT_CMD_UNSUPPORTED)
		cur_sdi_mode = HINIC_SDI_MODE_BM;
	else if (err)
		return err;

	switch (cur_sdi_mode) {
	case HINIC_SDI_MODE_BM:
		if (hinic_pcie_itf_id(hwdev) == 0)
			set_func_host_mode(hwdev, FUNC_MOD_MULTI_BM_MASTER);
		else
			set_func_host_mode(hwdev, FUNC_MOD_MULTI_BM_SLAVE);
		break;
	case HINIC_SDI_MODE_VM:
		if (hinic_pcie_itf_id(hwdev) == 0)
			set_func_host_mode(hwdev, FUNC_MOD_MULTI_VM_MASTER);
		else
			set_func_host_mode(hwdev, FUNC_MOD_MULTI_VM_SLAVE);
		break;
	default:
		sdk_warn(hwdev->dev_hdl, "Unknown sdi mode %d\n", cur_sdi_mode);
		break;
	}

	return 0;
}

void detect_host_mode_pre(struct hinic_hwdev *hwdev)
{
	enum hinic_chip_mode chip_mode;

	/* all pf can set HOST_MODE REG, so don't trust HOST_MODE REG for host0,
	 * get chip mode from mgmt cpu for host0
	 * VF have not right to read HOST_MODE REG, detect mode from board info
	 */
	if (hinic_pcie_itf_id(hwdev) == 0 ||
	    HINIC_FUNC_TYPE(hwdev) == TYPE_VF) {
		set_func_host_mode(hwdev, FUNC_MOD_NORMAL_HOST);
		return;
	}

	chip_mode = hinic_hwif_read_reg(hwdev->hwif, HINIC_HOST_MODE_ADDR);
	switch (MULTI_HOST_REG_GET(chip_mode, CHIP_MODE)) {
	case CHIP_MODE_VMGW:
		set_func_host_mode(hwdev, FUNC_MOD_MULTI_VM_SLAVE);
		/* mbox has not initialized, set slave host disable */
		set_slave_host_enable(hwdev, hinic_pcie_itf_id(hwdev), false);
		break;
	case CHIP_MODE_BMGW:
		set_func_host_mode(hwdev, FUNC_MOD_MULTI_BM_SLAVE);
		/* mbox has not initialized, set slave host disable */
		set_slave_host_enable(hwdev, hinic_pcie_itf_id(hwdev), false);
		break;

	default:
		set_func_host_mode(hwdev, FUNC_MOD_NORMAL_HOST);
		break;
	}
}


int __mbox_to_host(struct hinic_hwdev *hwdev, enum hinic_mod_type mod,
		   u8 cmd, void *buf_in, u16 in_size, void *buf_out,
		   u16 *out_size, u32 timeout,
		   enum hinic_mbox_ack_type ack_type)
{
	struct hinic_hwdev *mbox_hwdev = hwdev;
	u8 dst_host_func_idx;
	int err;

	if (!IS_MULTI_HOST(hwdev) || HINIC_IS_VF(hwdev))
		return -EPERM;

	if (hinic_func_type(hwdev) == TYPE_PF) {
		down(&hwdev->ppf_sem);
		mbox_hwdev = hwdev->ppf_hwdev;
		if (!mbox_hwdev) {
			err = -EINVAL;
			goto release_lock;
		}

		if (!hinic_is_hwdev_mod_inited(mbox_hwdev,
					       HINIC_HWDEV_MBOX_INITED)) {
			err = -EPERM;
			goto release_lock;
		}
	}

	if (!mbox_hwdev->chip_present_flag) {
		err = -EPERM;
		goto release_lock;
	}

	if (!hinic_get_master_host_mbox_enable(hwdev)) {
		sdk_err(hwdev->dev_hdl, "Master host not initialized\n");
		err = -EFAULT;
		goto release_lock;
	}

	if (!mbox_hwdev->mhost_mgmt) {
		/* send to master host in default */
		dst_host_func_idx = get_master_host_ppf_idx(hwdev);
	} else {
		dst_host_func_idx = IS_MASTER_HOST(hwdev) ?
				mbox_hwdev->mhost_mgmt->shost_ppf_idx :
				mbox_hwdev->mhost_mgmt->mhost_ppf_idx;
	}

	if (ack_type == MBOX_ACK)
		err = hinic_mbox_to_host(mbox_hwdev, dst_host_func_idx,
					 mod, cmd, buf_in, in_size,
					 buf_out, out_size,
					 timeout);
	else
		err = hinic_mbox_to_func_no_ack(mbox_hwdev, dst_host_func_idx,
						mod, cmd, buf_in, in_size);

release_lock:
	if (hinic_func_type(hwdev) == TYPE_PF)
		up(&hwdev->ppf_sem);

	return err;
}

int hinic_mbox_to_host_sync(void *hwdev, enum hinic_mod_type mod,
			    u8 cmd, void *buf_in, u16 in_size, void *buf_out,
			    u16 *out_size, u32 timeout)
{
	if (!hwdev)
		return -EINVAL;

	return __mbox_to_host((struct hinic_hwdev *)hwdev, mod, cmd, buf_in,
			in_size, buf_out, out_size, timeout, MBOX_ACK);
}
EXPORT_SYMBOL(hinic_mbox_to_host_sync);

int hinic_mbox_to_host_no_ack(struct hinic_hwdev *hwdev,
			      enum hinic_mod_type mod, u8 cmd, void *buf_in,
			      u16 in_size)
{
	return __mbox_to_host(hwdev, mod, cmd, buf_in, in_size, NULL, NULL,
			      0, MBOX_NO_ACK);
}

static int __get_func_nic_state_from_pf(struct hinic_hwdev *hwdev,
					u16 glb_func_idx, u8 *en);

int sw_func_pf_mbox_handler(void *handle, u16 vf_id, u8 cmd, void *buf_in,
			    u16 in_size, void *buf_out, u16 *out_size)
{
	struct hinic_hwdev *hwdev = handle;
	struct hinic_slave_func_nic_state *nic_state, *out_state;
	int err;

	switch (cmd) {
	case HINIC_SW_CMD_GET_SLAVE_FUNC_NIC_STATE:
		nic_state = buf_in;
		out_state = buf_out;
		*out_size = sizeof(*nic_state);

		/* find nic state in ppf func_nic_en bitmap */
		err = __get_func_nic_state_from_pf(hwdev, nic_state->func_idx,
						   &out_state->enable);
		if (err)
			out_state->status = 1;
		else
			out_state->status = 0;

		break;
	default:
		break;
	}

	return 0;
}

static int __master_host_sw_func_handler(struct hinic_hwdev *hwdev, u16 pf_idx,
					 u8 cmd, void *buf_in, u16 in_size,
					 void *buf_out, u16 *out_size)
{
	struct hinic_multi_host_mgmt *mhost_mgmt = hwdev->mhost_mgmt;
	struct register_slave_host *slave_host, *out_shost;
	int err = 0;

	if (!mhost_mgmt)
		return -ENXIO;

	switch (cmd) {
	case HINIC_SW_CMD_SLAVE_HOST_PPF_REGISTER:
		slave_host = buf_in;
		out_shost = buf_out;
		*out_size = sizeof(*slave_host);
		mhost_mgmt->shost_registered = true;
		mhost_mgmt->shost_host_idx = slave_host->host_id;
		mhost_mgmt->shost_ppf_idx = slave_host->ppf_idx;

		bitmap_copy((ulong *)out_shost->funcs_nic_en,
			    mhost_mgmt->func_nic_en, HINIC_MAX_FUNCTIONS);
		sdk_info(hwdev->dev_hdl, "slave host register ppf, host_id: %d, ppf_idx: %d\n",
			 slave_host->host_id, slave_host->ppf_idx);

		out_shost->status = 0;
		break;
	case HINIC_SW_CMD_SLAVE_HOST_PPF_UNREGISTER:
		slave_host = buf_in;
		mhost_mgmt->shost_registered = false;
		sdk_info(hwdev->dev_hdl, "slave host unregister ppf, host_id: %d, ppf_idx: %d\n",
			 slave_host->host_id, slave_host->ppf_idx);

		*out_size = sizeof(*slave_host);
		((struct register_slave_host *)buf_out)->status = 0;
		break;

	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static int __event_set_func_nic_state(struct hinic_hwdev *hwdev, void *buf_in,
				      u16 in_size, void *buf_out, u16 *out_size)
{
	struct hinic_event_info event_info = {0};
	struct hinic_mhost_nic_func_state nic_state = {0};
	struct hinic_slave_func_nic_state *out_state, *func_nic_state = buf_in;

	event_info.type = HINIC_EVENT_MULTI_HOST_MGMT;
	event_info.mhost_mgmt.sub_cmd = HINIC_MHOST_NIC_STATE_CHANGE;
	event_info.mhost_mgmt.data = &nic_state;

	nic_state.func_idx = func_nic_state->func_idx;
	nic_state.enable = func_nic_state->enable;

	if (!hwdev->event_callback)
		return -EFAULT;

	hwdev->event_callback(hwdev->event_pri_handle, &event_info);

	*out_size = sizeof(*out_state);
	out_state = buf_out;
	out_state->status = nic_state.status;

	return nic_state.status;
}

static int multi_host_event_handler(struct hinic_hwdev *hwdev,
				    u8 cmd, void *buf_in,
				    u16 in_size, void *buf_out, u16 *out_size)
{
	int err;

	switch (cmd) {
	case HINIC_SW_CMD_SET_SLAVE_FUNC_NIC_STATE:
		err = __event_set_func_nic_state(hwdev, buf_in, in_size,
						 buf_out, out_size);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int sw_fwd_msg_to_vf(struct hinic_hwdev *hwdev,
			    void *buf_in, u16 in_size,
			    void *buf_out, u16 *out_size)
{
	struct hinic_host_fwd_head *fwd_head;
	u16 fwd_head_len;
	void *msg;
	int err;

	fwd_head = buf_in;
	fwd_head_len = sizeof(struct hinic_host_fwd_head);
	msg = (void *)((u8 *)buf_in + fwd_head_len);
	err = hinic_mbox_ppf_to_vf(hwdev, fwd_head->mod,
				   fwd_head->dst_glb_func_idx, fwd_head->cmd,
				   msg, (in_size - fwd_head_len),
				   buf_out, out_size, 0);
	if (err)
		nic_err(hwdev->dev_hdl,
			"Fwd msg to func %u failed, err: %d\n",
			fwd_head->dst_glb_func_idx, err);

	return err;
}

static int __slave_host_sw_func_handler(struct hinic_hwdev *hwdev, u16 pf_idx,
					u8 cmd, void *buf_in, u16 in_size,
					void *buf_out, u16 *out_size)
{
	struct hinic_multi_host_mgmt *mhost_mgmt = hwdev->mhost_mgmt;
	struct hinic_slave_func_nic_state *nic_state;
	int err = 0;

	if (!mhost_mgmt)
		return -ENXIO;

	switch (cmd) {
	case HINIC_SW_CMD_SET_SLAVE_FUNC_NIC_STATE:
		nic_state = buf_in;

		*out_size = sizeof(*nic_state);
		((struct hinic_slave_func_nic_state *)buf_out)->status = 0;

		sdk_info(hwdev->dev_hdl, "slave func %d %s nic\n",
			 nic_state->func_idx,
			 nic_state->enable ? "register" : "unregister");

		if (nic_state->enable)
			set_bit(nic_state->func_idx, mhost_mgmt->func_nic_en);
		else
			clear_bit(nic_state->func_idx, mhost_mgmt->func_nic_en);

		multi_host_event_handler(hwdev, cmd, buf_in, in_size, buf_out,
					 out_size);

		break;

	case HINIC_SW_CMD_SEND_MSG_TO_VF:
		err = sw_fwd_msg_to_vf(hwdev, buf_in, in_size,
				       buf_out, out_size);
		break;

	case HINIC_SW_CMD_MIGRATE_READY:
		hinic_migrate_report(hwdev);
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

int sw_func_ppf_mbox_handler(void *handle, u16 pf_idx, u16 vf_id, u8 cmd,
			     void *buf_in, u16 in_size, void *buf_out,
			     u16 *out_size)
{
	struct hinic_hwdev *hwdev = handle;
	int err;

	if (IS_MASTER_HOST(hwdev))
		err = __master_host_sw_func_handler(hwdev, pf_idx, cmd, buf_in,
						    in_size, buf_out, out_size);
	else if (IS_SLAVE_HOST(hwdev))
		err = __slave_host_sw_func_handler(hwdev, pf_idx, cmd, buf_in,
						   in_size, buf_out, out_size);
	else
		err = -EINVAL;

	if (err)
		sdk_err(hwdev->dev_hdl, "PPF process sw funcs cmd %d failed, err: %d\n",
			cmd, err);

	return err;
}

int __ppf_process_mbox_msg(struct hinic_hwdev *hwdev, u16 pf_idx, u16 vf_id,
			   enum hinic_mod_type mod, u8 cmd, void *buf_in,
			   u16 in_size, void *buf_out, u16 *out_size)
{
	int err;

	if (IS_SLAVE_HOST(hwdev)) {
		err = hinic_mbox_to_host_sync(hwdev, mod, cmd,
					      buf_in, in_size,
					      buf_out, out_size, 0);
		if (err)
			sdk_err(hwdev->dev_hdl, "send to mpf failed, err: %d\n",
				err);
	} else if (IS_MASTER_HOST(hwdev)) {
		if (mod == HINIC_MOD_COMM && cmd == HINIC_MGMT_CMD_START_FLR)
			err = hinic_pf_to_mgmt_no_ack(hwdev, mod, cmd, buf_in,
						      in_size);
		else
			err = hinic_pf_msg_to_mgmt_sync(hwdev, mod, cmd, buf_in,
							in_size, buf_out,
							out_size, 0U);
		if (err && err != HINIC_DEV_BUSY_ACTIVE_FW &&
		    err != HINIC_MBOX_PF_BUSY_ACTIVE_FW)
			sdk_err(hwdev->dev_hdl, "PF mbox common callback handler err: %d\n",
				err);
	} else {
		/* not support */
		err = -EFAULT;
	}

	return err;
}

int hinic_ppf_process_mbox_msg(struct hinic_hwdev *hwdev, u16 pf_idx, u16 vf_id,
			       enum hinic_mod_type mod, u8 cmd, void *buf_in,
			       u16 in_size, void *buf_out, u16 *out_size)
{
	bool same_host = false;
	int err = -EFAULT;

	/* TODO: receive message from other host? get host id from pf_id */
	/* modify same_host according to hinic_get_hw_pf_infos */

	switch (hwdev->func_mode) {
	case FUNC_MOD_MULTI_VM_MASTER:
	case FUNC_MOD_MULTI_BM_MASTER:
		if (!same_host)
			err = __ppf_process_mbox_msg(hwdev, pf_idx, vf_id,
						     mod, cmd, buf_in, in_size,
						     buf_out, out_size);
		else
			sdk_warn(hwdev->dev_hdl, "Don't support ppf mbox message in BM master\n");

		break;
	case FUNC_MOD_MULTI_VM_SLAVE:
	case FUNC_MOD_MULTI_BM_SLAVE:
		same_host = true;
		if (same_host)
			err = __ppf_process_mbox_msg(hwdev, pf_idx, vf_id,
						     mod, cmd, buf_in, in_size,
						     buf_out, out_size);
		else
			sdk_warn(hwdev->dev_hdl, "Receive control message from BM master, don't support for now\n");

		break;
	default:
		sdk_warn(hwdev->dev_hdl, "Don't support ppf mbox message\n");

		break;
	}

	return err;
}

int comm_ppf_mbox_handler(void *handle, u16 pf_idx, u16 vf_id, u8 cmd,
			  void *buf_in, u16 in_size, void *buf_out,
			  u16 *out_size)
{
	return hinic_ppf_process_mbox_msg(handle, pf_idx, vf_id, HINIC_MOD_COMM,
					  cmd, buf_in, in_size, buf_out,
					  out_size);
}

void comm_ppf_to_pf_handler(void *handle, u8 cmd,
			    void *buf_in, u16 in_size,
			    void *buf_out, u16 *out_size)
{
	struct hinic_hwdev *hwdev = handle;

	sdk_err(hwdev->dev_hdl, "pf receive ppf common mbox msg, don't supported for now\n");
}

int hilink_ppf_mbox_handler(void *handle, u16 pf_idx, u16 vf_id, u8 cmd,
			    void *buf_in, u16 in_size, void *buf_out,
			    u16 *out_size)
{
	return hinic_ppf_process_mbox_msg(handle, pf_idx, vf_id,
					  HINIC_MOD_HILINK, cmd, buf_in,
					  in_size, buf_out, out_size);
}

int hinic_nic_ppf_mbox_handler(void *handle, u16 pf_idx, u16 vf_id, u8 cmd,
			       void *buf_in, u16 in_size, void *buf_out,
			       u16 *out_size)
{
	return hinic_ppf_process_mbox_msg(handle, pf_idx, vf_id,
					  HINIC_MOD_L2NIC, cmd, buf_in, in_size,
					  buf_out, out_size);
}

void hinic_nic_ppf_to_pf_handler(void *handle, u8 cmd,
				 void *buf_in, u16 in_size,
				 void *buf_out, u16 *out_size)
{
	struct hinic_hwdev *hwdev = handle;

	sdk_err(hwdev->dev_hdl, "ppf receive other pf l2nic mbox msg, don't supported for now\n");
}

int hinic_register_slave_ppf(struct hinic_hwdev *hwdev, bool registered)
{
	struct register_slave_host host_info = {0};
	u16 out_size = sizeof(host_info);
	u8 cmd;
	int err;

	if (!IS_SLAVE_HOST(hwdev))
		return -EINVAL;

	cmd = registered ? HINIC_SW_CMD_SLAVE_HOST_PPF_REGISTER :
		HINIC_SW_CMD_SLAVE_HOST_PPF_UNREGISTER;

	host_info.host_id = hinic_pcie_itf_id(hwdev);
	host_info.ppf_idx = hinic_ppf_idx(hwdev);

	err = hinic_mbox_to_host_sync(hwdev, HINIC_MOD_SW_FUNC, cmd,
				      &host_info, sizeof(host_info), &host_info,
				      &out_size, 0);
	if (err || !out_size || host_info.status) {
		sdk_err(hwdev->dev_hdl, "Failed to %s slave host, err: %d, out_size: 0x%x, status: 0x%x\n",
			registered ? "register" : "unregister", err, out_size,
			host_info.status);
		return -EFAULT;
	}
	bitmap_copy(hwdev->mhost_mgmt->func_nic_en,
		    (ulong *)host_info.funcs_nic_en,
		    HINIC_MAX_FUNCTIONS);
	return 0;
}

static int get_host_id_by_func_id(struct hinic_hwdev *hwdev, u16 func_idx,
				  u8 *host_id)
{
	struct hinic_hw_pf_infos *pf_infos;
	u16 vf_id_start, vf_id_end;
	int i;

	if (!hwdev || !host_id || !hwdev->mhost_mgmt)
		return -EINVAL;

	pf_infos = &hwdev->mhost_mgmt->pf_infos;

	for (i = 0; i < pf_infos->num_pfs; i++) {
		if (func_idx == pf_infos->infos[i].glb_func_idx) {
			*host_id = pf_infos->infos[i].itf_idx;
			return 0;
		}

		vf_id_start = pf_infos->infos[i].glb_pf_vf_offset + 1;
		vf_id_end = pf_infos->infos[i].glb_pf_vf_offset +
				pf_infos->infos[i].max_vfs;
		if (func_idx >= vf_id_start && func_idx <= vf_id_end) {
			*host_id = pf_infos->infos[i].itf_idx;
			return 0;
		}
	}

	return -EFAULT;
}

int set_slave_func_nic_state(struct hinic_hwdev *hwdev, u16 func_idx, u8 en)
{
	struct hinic_slave_func_nic_state nic_state = {0};
	u16 out_size = sizeof(nic_state);
	int err;

	nic_state.func_idx = func_idx;
	nic_state.enable = en;

	err = hinic_mbox_to_host_sync(hwdev, HINIC_MOD_SW_FUNC,
				      HINIC_SW_CMD_SET_SLAVE_FUNC_NIC_STATE,
				      &nic_state, sizeof(nic_state), &nic_state,
				      &out_size, 0);
	if (err == MBOX_ERRCODE_UNKNOWN_DES_FUNC) {
		sdk_warn(hwdev->dev_hdl, "Can not notify func %d nic state because slave host not initialized\n",
			 func_idx);
	} else if (err || !out_size || nic_state.status) {
		sdk_err(hwdev->dev_hdl, "Failed to set slave host functions nic state, err: %d, out_size: 0x%x, status: 0x%x\n",
			err, out_size, nic_state.status);
		return -EFAULT;
	}

	return 0;
}

int hinic_set_func_nic_state(void *hwdev, struct hinic_func_nic_state *state)
{
	struct hinic_hwdev *ppf_hwdev = hwdev;
	struct hinic_multi_host_mgmt *mhost_mgmt;
	u8 host_id = 0;
	bool host_enable;
	int err;
	int old_state;

	if (!hwdev || !state)
		return -EINVAL;

	if (hinic_func_type(hwdev) != TYPE_PPF)
		ppf_hwdev = ((struct hinic_hwdev *)hwdev)->ppf_hwdev;

	if (!ppf_hwdev || !IS_MASTER_HOST(ppf_hwdev))
		return -EINVAL;

	mhost_mgmt = ppf_hwdev->mhost_mgmt;
	if (!mhost_mgmt || state->func_idx >= HINIC_MAX_FUNCTIONS)
		return -EINVAL;

	old_state = test_bit(state->func_idx, mhost_mgmt->func_nic_en) ? 1 : 0;
	if (state->state == HINIC_FUNC_NIC_DEL)
		clear_bit(state->func_idx, mhost_mgmt->func_nic_en);
	else if (state->state == HINIC_FUNC_NIC_ADD)
		set_bit(state->func_idx, mhost_mgmt->func_nic_en);
	else
		return -EINVAL;

	err = get_host_id_by_func_id(ppf_hwdev, state->func_idx, &host_id);
	if (err) {
		sdk_err(ppf_hwdev->dev_hdl, "Failed to get function %d host id, err: %d\n",
			state->func_idx, err);
		old_state ? set_bit(state->func_idx, mhost_mgmt->func_nic_en) :
			clear_bit(state->func_idx, mhost_mgmt->func_nic_en);
		return -EFAULT;
	}

	host_enable = hinic_get_slave_host_enable(hwdev, host_id);
	sdk_info(ppf_hwdev->dev_hdl, "Set slave host %d(status: %d) func %d %s nic\n",
		 host_id, host_enable,
		 state->func_idx, state->state ? "enable" : "disable");

	if (!host_enable)
		return 0;

	/* notify slave host */
	err = set_slave_func_nic_state(hwdev, state->func_idx, state->state);
	if (err) {
		old_state ? set_bit(state->func_idx, mhost_mgmt->func_nic_en) :
			clear_bit(state->func_idx, mhost_mgmt->func_nic_en);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_set_func_nic_state);

static int __get_func_nic_state_from_pf(struct hinic_hwdev *hwdev,
					u16 glb_func_idx, u8 *en)
{
	struct hinic_multi_host_mgmt *mhost_mgmt;
	struct hinic_hwdev *ppf_hwdev = hwdev;

	if (hinic_func_type(hwdev) != TYPE_PPF)
		ppf_hwdev = ((struct hinic_hwdev *)hwdev)->ppf_hwdev;

	if (!ppf_hwdev || !ppf_hwdev->mhost_mgmt)
		return -EFAULT;

	mhost_mgmt = ppf_hwdev->mhost_mgmt;
	*en = !!(test_bit(glb_func_idx, mhost_mgmt->func_nic_en));

	sdk_info(ppf_hwdev->dev_hdl, "slave host func %d nic %d\n",
		 glb_func_idx, *en);

	return 0;
}

int hinic_get_func_nic_enable(void *hwdev, u16 glb_func_idx, bool *en)
{
	struct hinic_slave_func_nic_state nic_state = {0};
	u16 out_size = sizeof(nic_state);
	u8 nic_en;
	int err;

	if (!hwdev || !en)
		return -EINVAL;
	/*if card mode is OVS, VFs donot need attach_uld, so return false.*/
	if (!IS_SLAVE_HOST((struct hinic_hwdev *)hwdev)) {
		if (hinic_func_type(hwdev) == TYPE_VF &&
		    hinic_support_ovs(hwdev, NULL)) {
			*en = false;
		} else {
			*en = true;
		}
		return 0;
	}

	if (hinic_func_type(hwdev) == TYPE_VF) {
		nic_state.func_idx = glb_func_idx;
		err = hinic_msg_to_mgmt_sync(
			hwdev, HINIC_MOD_SW_FUNC,
			HINIC_SW_CMD_GET_SLAVE_FUNC_NIC_STATE,
			&nic_state, sizeof(nic_state),
			&nic_state, &out_size, 0);
		if (err || !out_size || nic_state.status) {
			sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl, "Failed to get func %d nic state, err: %d, out_size: 0x%x, status: 0x%x\n",
				glb_func_idx, err, out_size, nic_state.status);
			return -EFAULT;
		}

		*en = !!nic_state.enable;

		return 0;
	}

	/* pf in slave host should be probe in CHIP_MODE_VMGW
	 * mode for pxe install
	 */
	if (IS_VM_SLAVE_HOST((struct hinic_hwdev *)hwdev)) {
		*en = true;
		return 0;
	}

	/* pf/ppf get function nic state in sdk diretly */
	err = __get_func_nic_state_from_pf(hwdev, glb_func_idx, &nic_en);
	if (err)
		return err;

	*en = !!nic_en;

	return 0;
}

int hinic_multi_host_mgmt_init(struct hinic_hwdev *hwdev)
{
	int err;

	if (!IS_MULTI_HOST(hwdev) || !HINIC_IS_PPF(hwdev))
		return 0;

	hwdev->mhost_mgmt = kzalloc(sizeof(*hwdev->mhost_mgmt), GFP_KERNEL);
	if (!hwdev->mhost_mgmt) {
		sdk_err(hwdev->dev_hdl, "Failed to alloc multi-host mgmt memory\n");
		return -ENOMEM;
	}

	hwdev->mhost_mgmt->mhost_ppf_idx = get_master_host_ppf_idx(hwdev);
	hwdev->mhost_mgmt->shost_ppf_idx = 0;
	hwdev->mhost_mgmt->shost_host_idx = 2;

	err = hinic_get_hw_pf_infos(hwdev, &hwdev->mhost_mgmt->pf_infos);
	if (err)
		goto out_free_mhost_mgmt;

	hinic_register_ppf_mbox_cb(hwdev, HINIC_MOD_COMM,
				   comm_ppf_mbox_handler);
	hinic_register_ppf_mbox_cb(hwdev, HINIC_MOD_L2NIC,
				   hinic_nic_ppf_mbox_handler);
	hinic_register_ppf_mbox_cb(hwdev, HINIC_MOD_HILINK,
				   hilink_ppf_mbox_handler);
	hinic_register_ppf_mbox_cb(hwdev, HINIC_MOD_SW_FUNC,
				   sw_func_ppf_mbox_handler);

	bitmap_zero(hwdev->mhost_mgmt->func_nic_en, HINIC_MAX_FUNCTIONS);

	/* Slave host:
	 * register slave host ppf functions
	 * Get function's nic state
	 */
	if (IS_SLAVE_HOST(hwdev)) {
		/* PXE don't support to receive mbox from master host */
		set_slave_host_enable(hwdev, hinic_pcie_itf_id(hwdev), true);
		if ((IS_VM_SLAVE_HOST(hwdev) &&
		     hinic_get_master_host_mbox_enable(hwdev)) ||
		     IS_BMGW_SLAVE_HOST(hwdev)) {
			err = hinic_register_slave_ppf(hwdev, true);
			if (err) {
				set_slave_host_enable(hwdev,
						      hinic_pcie_itf_id(hwdev),
						      false);
				goto out_free_mhost_mgmt;
			}
		}
	} else {
		/* slave host can send message to mgmt cpu after setup master
		 * mbox
		 */
		set_master_host_mbox_enable(hwdev, true);
	}

	return 0;

out_free_mhost_mgmt:
	kfree(hwdev->mhost_mgmt);
	hwdev->mhost_mgmt = NULL;

	return err;
}

int hinic_multi_host_mgmt_free(struct hinic_hwdev *hwdev)
{
	if (!IS_MULTI_HOST(hwdev) || !HINIC_IS_PPF(hwdev))
		return 0;

	if (IS_SLAVE_HOST(hwdev)) {
		hinic_register_slave_ppf(hwdev, false);

		set_slave_host_enable(hwdev, hinic_pcie_itf_id(hwdev), false);
	} else {
		set_master_host_mbox_enable(hwdev, false);
	}

	hinic_unregister_ppf_mbox_cb(hwdev, HINIC_MOD_COMM);
	hinic_unregister_ppf_mbox_cb(hwdev, HINIC_MOD_L2NIC);
	hinic_unregister_ppf_mbox_cb(hwdev, HINIC_MOD_HILINK);
	hinic_unregister_ppf_mbox_cb(hwdev, HINIC_MOD_SW_FUNC);

	kfree(hwdev->mhost_mgmt);
	hwdev->mhost_mgmt = NULL;

	return 0;
}
