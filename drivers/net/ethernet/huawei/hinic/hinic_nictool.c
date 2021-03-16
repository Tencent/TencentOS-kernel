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

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <net/sock.h>

#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_hw_mgmt.h"
#include "hinic_lld.h"
#include "hinic_nic_dev.h"
#include "hinic_dbg.h"
#include "hinic_nictool.h"
#include "hinic_qp.h"
#include "hinic_dcb.h"
#include "hinic_dbgtool_knl.h"

#define HIADM_DEV_PATH		"/dev/nictool_dev"
#define HIADM_DEV_CLASS		"nictool_class"
#define HIADM_DEV_NAME		"nictool_dev"

#define MAJOR_DEV_NUM 921
#define	HINIC_CMDQ_BUF_MAX_SIZE		2048U
#define MSG_MAX_IN_SIZE	(2048 * 1024)
#define MSG_MAX_OUT_SIZE	(2048 * 1024)

static dev_t g_dev_id = {0};
static struct class *g_nictool_class;
static struct cdev g_nictool_cdev;

static int g_nictool_init_flag;
static int g_nictool_ref_cnt;

typedef int (*nic_driv_module)(struct hinic_nic_dev *nic_dev, void *buf_in,
			   u32 in_size, void *buf_out, u32 *out_size);
struct nic_drv_module_handle {
	enum driver_cmd_type	driv_cmd_name;
	nic_driv_module		driv_func;
};

typedef int (*hw_driv_module)(void *hwdev, void *buf_in,
			   u32 in_size, void *buf_out, u32 *out_size);
struct hw_drv_module_handle {
	enum driver_cmd_type	driv_cmd_name;
	hw_driv_module		driv_func;
};

static void free_buff_in(void *hwdev, struct msg_module *nt_msg, void *buf_in)
{
	if (!buf_in)
		return;

	if (nt_msg->module == SEND_TO_UCODE)
		hinic_free_cmd_buf(hwdev, buf_in);
	else
		kfree(buf_in);
}

static int alloc_buff_in(void *hwdev, struct msg_module *nt_msg,
			 u32 in_size, void **buf_in)
{
	void *msg_buf;

	if (!in_size)
		return 0;

	if (nt_msg->module == SEND_TO_UCODE) {
		struct hinic_cmd_buf *cmd_buf;

		if (in_size > HINIC_CMDQ_BUF_MAX_SIZE) {
			pr_err("Cmdq in size(%u) more than 2KB\n", in_size);
			return -ENOMEM;
		}

		cmd_buf = hinic_alloc_cmd_buf(hwdev);
		if (!cmd_buf) {
			pr_err("Alloc cmdq cmd buffer failed in %s\n",
			       __func__);
			return -ENOMEM;
		}
		msg_buf = cmd_buf->buf;
		*buf_in = (void *)cmd_buf;
		cmd_buf->size = (u16)in_size;
	} else {
		if (in_size > MSG_MAX_IN_SIZE) {
			pr_err("In size(%u) more than 2M\n", in_size);
			return -ENOMEM;
		}
		msg_buf = kzalloc(in_size, GFP_KERNEL);
		*buf_in = msg_buf;
	}
	if (!(*buf_in)) {
		pr_err("Alloc buffer in failed\n");
		return -ENOMEM;
	}

	if (copy_from_user(msg_buf, nt_msg->in_buff, in_size)) {
		pr_err("%s:%d: Copy from user failed\n",
		       __func__, __LINE__);
		free_buff_in(hwdev, nt_msg, *buf_in);
		return -EFAULT;
	}

	return 0;
}

static void free_buff_out(void *hwdev, struct msg_module *nt_msg,
			  void *buf_out)
{
	if (!buf_out)
		return;

	if (nt_msg->module == SEND_TO_UCODE &&
	    !nt_msg->ucode_cmd.ucode_db.ucode_imm)
		hinic_free_cmd_buf(hwdev, buf_out);
	else
		kfree(buf_out);
}

static int alloc_buff_out(void *hwdev, struct msg_module *nt_msg,
			  u32 out_size, void **buf_out)
{
	if (!out_size)
		return 0;

	if (nt_msg->module == SEND_TO_UCODE &&
	    !nt_msg->ucode_cmd.ucode_db.ucode_imm) {
		struct hinic_cmd_buf *cmd_buf;

		if (out_size > HINIC_CMDQ_BUF_MAX_SIZE) {
			pr_err("Cmdq out size(%u) more than 2KB\n", out_size);
			return -ENOMEM;
		}

		cmd_buf = hinic_alloc_cmd_buf(hwdev);
		*buf_out = (void *)cmd_buf;
	} else {
		if (out_size > MSG_MAX_OUT_SIZE) {
			pr_err("out size(%u) more than 2M\n", out_size);
			return -ENOMEM;
		}
		*buf_out = kzalloc(out_size, GFP_KERNEL);
	}
	if (!(*buf_out)) {
		pr_err("Alloc buffer out failed\n");
		return -ENOMEM;
	}

	return 0;
}

static int copy_buf_out_to_user(struct msg_module *nt_msg,
				u32 out_size, void *buf_out)
{
	int ret = 0;
	void *msg_out;

	if (nt_msg->module == SEND_TO_UCODE &&
	    !nt_msg->ucode_cmd.ucode_db.ucode_imm)
		msg_out = ((struct hinic_cmd_buf *)buf_out)->buf;
	else
		msg_out = buf_out;

	if (copy_to_user(nt_msg->out_buf, msg_out, out_size))
		ret = -EFAULT;

	return ret;
}

static int hinic_dbg_get_sq_info(struct hinic_nic_dev *nic_dev, u16 q_id,
				 struct hinic_dbg_sq_info *sq_info,
				 u32 *msg_size);
static int hinic_dbg_get_rq_info(struct hinic_nic_dev *nic_dev, u16 q_id,
				 struct hinic_dbg_rq_info *rq_info,
				 u32 *msg_size);

static int get_tx_info(struct hinic_nic_dev *nic_dev, void *buf_in, u32 in_size,
		       void *buf_out, u32 *out_size)
{
	u16 q_id;
	int err;

	if (!test_bit(HINIC_INTF_UP, &nic_dev->flags)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Netdev is down, can't get tx info\n");
		return -EFAULT;
	}

	if (!buf_in || !buf_out ||  in_size != sizeof(int))
		return -EINVAL;

	q_id = *((u16 *)buf_in);

	err = hinic_dbg_get_sq_info(nic_dev, q_id, buf_out, out_size);

	return err;
}

static int get_q_num(struct hinic_nic_dev *nic_dev, void *buf_in, u32 in_size,
		     void *buf_out, u32 *out_size)
{
	u16 num_qp;

	if (!test_bit(HINIC_INTF_UP, &nic_dev->flags)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Netdev is down, can't get queue number\n");
		return -EFAULT;
	}

	if (!buf_out)
		return -EFAULT;

	num_qp = hinic_dbg_get_qp_num(nic_dev->hwdev);
	if (!num_qp)
		return -EFAULT;

	if (*out_size != sizeof(u16)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect out buf size from user: %d, expect: %lu\n",
			  *out_size, sizeof(u16));
		return -EFAULT;
	}
	*((u16 *)buf_out) = num_qp;

	return 0;
}

static int get_tx_wqe_info(struct hinic_nic_dev *nic_dev,
			   void *buf_in, u32 in_size,
			   void *buf_out, u32 *out_size)
{
	struct hinic_wqe_info *info = buf_in;
	u16 q_id = 0;
	u16 idx = 0, wqebb_cnt = 1;
	int err;

	if (!test_bit(HINIC_INTF_UP, &nic_dev->flags)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Netdev is down, can't get tx wqe info\n");
		return -EFAULT;
	}

	if (!info || !buf_out || in_size != sizeof(*info))
		return -EFAULT;

	q_id = (u16)info->q_id;
	idx = (u16)info->wqe_id;

	err = hinic_dbg_get_sq_wqe_info(nic_dev->hwdev, q_id,
					idx, wqebb_cnt,
					buf_out, (u16 *)out_size);

	return err;
}

static int get_rx_info(struct hinic_nic_dev *nic_dev, void *buf_in, u32 in_size,
		       void *buf_out, u32 *out_size)
{
	u16 q_id;
	int err;

	if (!test_bit(HINIC_INTF_UP, &nic_dev->flags)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Netdev is down, can't get rx info\n");
		return -EFAULT;
	}

	if (!buf_in || !buf_out || in_size != sizeof(int))
		return -EINVAL;

	q_id = *((u16 *)buf_in);

	err = hinic_dbg_get_rq_info(nic_dev, q_id, buf_out, out_size);

	for (q_id = 0; q_id < nic_dev->num_qps; q_id++) {
		nicif_info(nic_dev, drv, nic_dev->netdev,
			   "qid: %u, coalesc_timer:0x%x, pending_limit: 0x%x\n",
			   q_id, nic_dev->rxqs[q_id].last_coalesc_timer_cfg,
			   nic_dev->rxqs[q_id].last_pending_limt);
	}

	return err;
}

static int get_rx_wqe_info(struct hinic_nic_dev *nic_dev, void *buf_in,
			   u32 in_size, void *buf_out, u32 *out_size)
{
	struct hinic_wqe_info *info = buf_in;
	u16 q_id = 0;
	u16 idx = 0, wqebb_cnt = 1;
	int err;

	if (!test_bit(HINIC_INTF_UP, &nic_dev->flags)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Netdev is down, can't get rx wqe info\n");
		return -EFAULT;
	}

	if (!info || !buf_out || in_size != sizeof(*info))
		return -EFAULT;

	q_id = (u16)info->q_id;
	idx = (u16)info->wqe_id;

	err = hinic_dbg_get_rq_wqe_info(nic_dev->hwdev, q_id,
					idx, wqebb_cnt,
					buf_out, (u16 *)out_size);

	return err;
}

static int get_inter_num(struct hinic_nic_dev *nic_dev, void *buf_in,
			 u32 in_size, void *buf_out, u32 *out_size)
{
	u16 intr_num;

	intr_num = hinic_intr_num(nic_dev->hwdev);

	if (*out_size != sizeof(u16)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect out buf size from user :%d, expect: %lu\n",
			  *out_size, sizeof(u16));
		return -EFAULT;
	}
	*(u16 *)buf_out = intr_num;

	*out_size = sizeof(u16);

	return 0;
}

static void clean_nicdev_stats(struct hinic_nic_dev *nic_dev)
{
	u64_stats_update_begin(&nic_dev->stats.syncp);
	nic_dev->stats.netdev_tx_timeout = 0;
	nic_dev->stats.tx_carrier_off_drop = 0;
	nic_dev->stats.tx_invalid_qid = 0;
	u64_stats_update_end(&nic_dev->stats.syncp);
}

static int clear_func_static(struct hinic_nic_dev *nic_dev, void *buf_in,
			     u32 in_size, void *buf_out, u32 *out_size)
{
	int i;

	*out_size = 0;
#ifndef HAVE_NETDEV_STATS_IN_NETDEV
	memset(&nic_dev->net_stats, 0, sizeof(nic_dev->net_stats));
#endif
	clean_nicdev_stats(nic_dev);
	for (i = 0; i < nic_dev->max_qps; i++) {
		hinic_rxq_clean_stats(&nic_dev->rxqs[i].rxq_stats);
		hinic_txq_clean_stats(&nic_dev->txqs[i].txq_stats);
	}

	return 0;
}

static int get_num_cos(struct hinic_nic_dev *nic_dev, void *buf_in,
		       u32 in_size, void *buf_out, u32 *out_size)
{
	u8 *num_cos = buf_out;

	if (!buf_out || !out_size)
		return -EINVAL;

	if (*out_size != sizeof(*num_cos)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect out buf size from user :%d, expect: %lu\n",
			  *out_size, sizeof(*num_cos));
		return -EFAULT;
	}

	return hinic_get_num_cos(nic_dev, num_cos);
}

static int get_dcb_cos_up_map(struct hinic_nic_dev *nic_dev, void *buf_in,
			      u32 in_size, void *buf_out, u32 *out_size)
{
	struct hinic_cos_up_map *map = buf_out;

	if (!buf_out || !out_size)
		return -EINVAL;

	if (*out_size != sizeof(*map)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect out buf size from user :%d, expect: %lu\n",
			  *out_size, sizeof(*map));
		return -EFAULT;
	}

	return hinic_get_cos_up_map(nic_dev, &map->num_cos, map->cos_up);
}

static int set_dcb_cos_up_map(struct hinic_nic_dev *nic_dev, void *buf_in,
			      u32 in_size, void *buf_out, u32 *out_size)
{
	struct hinic_cos_up_map *map = buf_in;

	if (!buf_in || !out_size || in_size != sizeof(*map))
		return -EINVAL;

	*out_size = sizeof(*map);

	return hinic_set_cos_up_map(nic_dev, map->cos_up);
}

static int get_rx_cqe_info(struct hinic_nic_dev *nic_dev, void *buf_in,
			   u32 in_size, void *buf_out, u32 *out_size)
{
	struct hinic_wqe_info *info = buf_in;
	u16 q_id = 0;
	u16 idx = 0;

	if (!test_bit(HINIC_INTF_UP, &nic_dev->flags)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Netdev is down, can't get rx cqe info\n");
		return -EFAULT;
	}

	if (!info || !buf_out || in_size != sizeof(*info))
		return -EFAULT;

	if (*out_size != sizeof(struct hinic_rq_cqe)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect out buf size from user :%d, expect: %lu\n",
			  *out_size, sizeof(struct hinic_rq_cqe));
		return -EFAULT;
	}
	q_id = (u16)info->q_id;
	idx = (u16)info->wqe_id;

	if (q_id >= nic_dev->num_qps || idx >= nic_dev->rxqs[q_id].q_depth)
		return -EFAULT;

	memcpy(buf_out, nic_dev->rxqs[q_id].rx_info[idx].cqe,
	       sizeof(struct hinic_rq_cqe));

	return 0;
}

static int hinic_dbg_get_sq_info(struct hinic_nic_dev *nic_dev, u16 q_id,
				 struct hinic_dbg_sq_info *sq_info,
				 u32 *msg_size)
{
	int err;

	if (!nic_dev)
		return -EINVAL;

	if (!test_bit(HINIC_INTF_UP, &nic_dev->flags)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Netdev is down, can't get sq info\n");
		return -EFAULT;
	}

	if (q_id >= nic_dev->num_qps) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Input queue id is larger than the actual queue number\n");
		return -EINVAL;
	}

	if (*msg_size != sizeof(*sq_info)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect out buf size from user :%d, expect: %lu\n",
			  *msg_size, sizeof(*sq_info));
		return -EFAULT;
	}
	sq_info->q_id = q_id;
	sq_info->pi = hinic_dbg_get_sq_pi(nic_dev->hwdev, q_id);
	sq_info->ci = hinic_get_sq_local_ci(nic_dev->hwdev, q_id);
	sq_info->fi = hinic_get_sq_hw_ci(nic_dev->hwdev, q_id);

	sq_info->q_depth = nic_dev->txqs[q_id].q_depth;
	/* pi_reverse */

	sq_info->weqbb_size = HINIC_SQ_WQEBB_SIZE;
	/* priority */

	sq_info->ci_addr = hinic_dbg_get_sq_ci_addr(nic_dev->hwdev, q_id);

	sq_info->cla_addr = hinic_dbg_get_sq_cla_addr(nic_dev->hwdev, q_id);
	sq_info->slq_handle = hinic_dbg_get_sq_wq_handle(nic_dev->hwdev, q_id);

	/* direct wqe */

	err = hinic_dbg_get_sq_db_addr(nic_dev->hwdev,
				       q_id, &sq_info->db_addr.map_addr,
				       &sq_info->db_addr.phy_addr,
				       &sq_info->pg_idx);

	sq_info->glb_sq_id = hinic_dbg_get_global_qpn(nic_dev->hwdev) + q_id;

	return err;
}

static int hinic_dbg_get_rq_info(struct hinic_nic_dev *nic_dev, u16 q_id,
				 struct hinic_dbg_rq_info *rq_info,
				 u32 *msg_size)
{
	if (!nic_dev)
		return -EINVAL;

	if (!test_bit(HINIC_INTF_UP, &nic_dev->flags)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Netdev is down, can't get rq info\n");
		return -EFAULT;
	}

	if (q_id >= nic_dev->num_qps) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Input queue id is larger than the actual queue number\n");
		return -EINVAL;
	}
	if (*msg_size != sizeof(*rq_info)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect out buf size from user: %d, expect: %lu\n",
			  *msg_size, sizeof(*rq_info));
		return -EFAULT;
	}

	rq_info->q_id = q_id;
	rq_info->glb_rq_id = hinic_dbg_get_global_qpn(nic_dev->hwdev) + q_id;

	rq_info->hw_pi = hinic_dbg_get_rq_hw_pi(nic_dev->hwdev, q_id);
	rq_info->ci = (u16)nic_dev->rxqs[q_id].cons_idx &
		      nic_dev->rxqs[q_id].q_mask;

	rq_info->sw_pi = nic_dev->rxqs[q_id].next_to_update;

	rq_info->wqebb_size = HINIC_RQ_WQE_SIZE;
	rq_info->q_depth = nic_dev->rxqs[q_id].q_depth;

	rq_info->buf_len = nic_dev->rxqs[q_id].buf_len;

	rq_info->slq_handle = hinic_dbg_get_rq_wq_handle(nic_dev->hwdev, q_id);
	if (!rq_info->slq_handle) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Get rq slq handle null\n");
		return -EFAULT;
	}
	rq_info->ci_wqe_page_addr =
		hinic_slq_get_first_pageaddr(rq_info->slq_handle);
	rq_info->ci_cla_tbl_addr =
		hinic_dbg_get_rq_cla_addr(nic_dev->hwdev, q_id);

	rq_info->msix_idx = nic_dev->rxqs[q_id].msix_entry_idx;
	rq_info->msix_vector = nic_dev->rxqs[q_id].irq_id;

	return 0;
}

static int get_loopback_mode(struct hinic_nic_dev *nic_dev, void *buf_in,
			     u32 in_size, void *buf_out, u32 *out_size)
{
	struct hinic_nic_loop_mode *mode = buf_out;
	int err;

	if (!out_size || !mode)
		return -EFAULT;

	if (*out_size != sizeof(*mode)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect out buf size from user :%d, expect: %lu\n",
			  *out_size, sizeof(*mode));
		return -EFAULT;
	}
	err = hinic_get_loopback_mode_ex(nic_dev->hwdev, &mode->loop_mode,
					 &mode->loop_ctrl);
	return err;
}

static int set_loopback_mode(struct hinic_nic_dev *nic_dev, void *buf_in,
			     u32 in_size, void *buf_out, u32 *out_size)
{
	struct hinic_nic_loop_mode *mode = buf_in;
	int err;

	if (!test_bit(HINIC_INTF_UP, &nic_dev->flags)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Netdev is down, can't set loopback mode\n");
		return -EFAULT;
	}

	if (!mode || !out_size || in_size != sizeof(*mode))
		return -EFAULT;

	err = hinic_set_loopback_mode_ex(nic_dev->hwdev, mode->loop_mode,
					 mode->loop_ctrl);
	if (err)
		return err;

	*out_size = sizeof(*mode);
	return 0;
}

static int set_link_mode(struct hinic_nic_dev *nic_dev, void *buf_in,
			 u32 in_size, void *buf_out, u32 *out_size)
{
	enum hinic_nic_link_mode *link = buf_in;
	u8 link_status;

	if (!test_bit(HINIC_INTF_UP, &nic_dev->flags)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Netdev is down, can't set link mode\n");
		return -EFAULT;
	}

	if (!link || !out_size || in_size != sizeof(*link))
		return -EFAULT;

	switch (*link) {
	case HINIC_LINK_MODE_AUTO:
		if (hinic_get_link_state(nic_dev->hwdev, &link_status))
			link_status = false;
		hinic_link_status_change(nic_dev, (bool)link_status);
		nicif_info(nic_dev, drv, nic_dev->netdev,
			   "Set link mode: auto succeed, now is link %s\n",
			   (link_status ? "up" : "down"));
		break;
	case HINIC_LINK_MODE_UP:
		hinic_link_status_change(nic_dev, true);
		nicif_info(nic_dev, drv, nic_dev->netdev,
			   "Set link mode: up succeed\n");
		break;
	case HINIC_LINK_MODE_DOWN:
		hinic_link_status_change(nic_dev, false);
		nicif_info(nic_dev, drv, nic_dev->netdev,
			   "Set link mode: down succeed\n");
		break;
	default:
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Invalid link mode %d to set\n", *link);
		return  -EINVAL;
	}

	*out_size = sizeof(*link);
	return 0;
}

static int set_dcb_cfg(struct hinic_nic_dev *nic_dev, void *buf_in,
		       u32 in_size, void *buf_out, u32 *out_size)
{
	union _dcb_ctl dcb_ctl = {.data = 0};
	int err;

	if (!buf_in || !buf_out || *out_size != sizeof(u32) ||
	    in_size != sizeof(u32))
		return -EINVAL;

	dcb_ctl.data = *((u32 *)buf_in);

	err = hinic_setup_dcb_tool(nic_dev->netdev,
				   &dcb_ctl.dcb_data.dcb_en,
				   !!dcb_ctl.dcb_data.wr_flag);
	if (err) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Failed to setup dcb state to %d\n",
			  !!dcb_ctl.dcb_data.dcb_en);
		err = EINVAL;
	}
	dcb_ctl.dcb_data.err = (u8)err;
	*((u32 *)buf_out) = (u32)dcb_ctl.data;
	*out_size = sizeof(u32);

	return 0;
}

int get_pfc_info(struct hinic_nic_dev *nic_dev, void *buf_in,
		 u32 in_size, void *buf_out, u32 *out_size)
{
	union _pfc pfc = {.data = 0};

	if (!buf_in || !buf_out || *out_size != sizeof(u32) ||
	    in_size != sizeof(u32))
		return -EINVAL;

	pfc.data = *((u32 *)buf_in);

	hinic_dcbnl_set_pfc_en_tool(nic_dev->netdev,
				    &pfc.pfc_data.pfc_en, false);
	hinic_dcbnl_get_pfc_cfg_tool(nic_dev->netdev,
				     &pfc.pfc_data.pfc_priority);
	hinic_dcbnl_get_tc_num_tool(nic_dev->netdev,
				    &pfc.pfc_data.num_of_tc);
	*((u32 *)buf_out) = (u32)pfc.data;
	*out_size = sizeof(u32);

	return 0;
}

int set_pfc_control(struct hinic_nic_dev *nic_dev, void *buf_in,
		    u32 in_size, void *buf_out, u32 *out_size)
{
	u8 pfc_en = 0;
	u8 err = 0;

	if (!buf_in || !buf_out || *out_size != sizeof(u8) ||
	    in_size != sizeof(u8))
		return -EINVAL;

	pfc_en = *((u8 *)buf_in);
	if (!(test_bit(HINIC_DCB_ENABLE, &nic_dev->flags))) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Need to enable dcb first.\n");
		err = 0xff;
		goto exit;
	}

	hinic_dcbnl_set_pfc_en_tool(nic_dev->netdev, &pfc_en, true);
	err = hinic_dcbnl_set_pfc_tool(nic_dev->netdev);
	if (err) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Failed to set pfc to %s\n",
			  pfc_en ? "enable" : "disable");
	}

exit:
	*((u8 *)buf_out) = (u8)err;
	*out_size = sizeof(u8);
	return 0;
}

int set_ets(struct hinic_nic_dev *nic_dev, void *buf_in,
	    u32 in_size, void *buf_out, u32 *out_size)
{
	struct _ets ets =  {0};
	u8 err = 0;
	u8 i;
	u8 support_tc = nic_dev->max_cos;

	if (!buf_in || !buf_out || *out_size != sizeof(u8) ||
	    in_size != sizeof(struct _ets))
		return -EINVAL;

	memcpy(&ets, buf_in, sizeof(struct _ets));

	if (!(test_bit(HINIC_DCB_ENABLE, &nic_dev->flags))) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Need to enable dcb first.\n");
		err = 0xff;
		goto exit;
	}
	if (ets.flag_com.ets_flag.flag_ets_enable) {
		hinic_dcbnl_set_ets_en_tool(nic_dev->netdev, &ets.ets_en, true);

		if (!ets.ets_en)
			goto exit;
	}

	if (!(test_bit(HINIC_ETS_ENABLE, &nic_dev->flags))) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Need to enable ets first.\n");
		err = 0xff;
		goto exit;
	}
	if (ets.flag_com.ets_flag.flag_ets_cos)
		hinic_dcbnl_set_ets_tc_tool(nic_dev->netdev, ets.tc, true);

	if (ets.flag_com.ets_flag.flag_ets_percent) {
		for (i = support_tc; i < HINIC_DCB_TC_MAX; i++) {
			if (ets.ets_percent[i]) {
				nicif_err(nic_dev, drv, nic_dev->netdev,
					  "ETS setting out of range\n");
				break;
			}
		}

		hinic_dcbnl_set_ets_pecent_tool(nic_dev->netdev,
						ets.ets_percent, true);
	}

	if (ets.flag_com.ets_flag.flag_ets_strict)
		hinic_dcbnl_set_ets_strict_tool(nic_dev->netdev,
						&ets.strict, true);

	err = hinic_dcbnl_set_ets_tool(nic_dev->netdev);
	if (err) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Failed to set ets [%d].\n", err);
	}
exit:
	*((u8 *)buf_out) = err;
	*out_size = sizeof(err);
	return 0;
}

int get_support_up(struct hinic_nic_dev *nic_dev, void *buf_in,
		   u32 in_size, void *buf_out, u32 *out_size)
{
	u8 *up_num = buf_out;
	u8 support_up = 0;
	u8 i;
	u8 up_valid_bitmap = nic_dev->up_valid_bitmap;

	if (!buf_in || !buf_out || !out_size)
		return -EINVAL;

	if (*out_size != sizeof(*up_num)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect out buf size from user: %d, expect: %lu\n",
			  *out_size, sizeof(*up_num));
		return -EFAULT;
	}

	for (i = 0; i < HINIC_DCB_UP_MAX; i++) {
		if (up_valid_bitmap & BIT(i))
			support_up++;
	}

	*up_num = support_up;

	return 0;
}

int get_support_tc(struct hinic_nic_dev *nic_dev, void *buf_in,
		   u32 in_size, void *buf_out, u32 *out_size)
{
	u8 *tc_num = buf_out;

	if (!buf_in || !buf_out || !out_size)
		return -EINVAL;

	if (*out_size != sizeof(*tc_num)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect out buf size from user :%d, expect:	%lu\n",
			  *out_size, sizeof(*tc_num));
		return -EFAULT;
	}

	hinic_dcbnl_get_tc_num_tool(nic_dev->netdev, tc_num);

	return 0;
}

int get_ets_info(struct hinic_nic_dev *nic_dev, void *buf_in,
		 u32 in_size, void *buf_out, u32 *out_size)
{
	struct _ets *ets = buf_out;

	if (!buf_in || !buf_out || *out_size != sizeof(*ets))
		return -EINVAL;

	hinic_dcbnl_set_ets_pecent_tool(nic_dev->netdev,
					ets->ets_percent, false);
	hinic_dcbnl_set_ets_tc_tool(nic_dev->netdev, ets->tc, false);
	hinic_dcbnl_set_ets_en_tool(nic_dev->netdev, &ets->ets_en, false);
	hinic_dcbnl_set_ets_strict_tool(nic_dev->netdev, &ets->strict, false);
	ets->err = 0;

	*out_size = sizeof(*ets);
	return 0;
}

int set_pfc_priority(struct hinic_nic_dev *nic_dev, void *buf_in,
		     u32 in_size, void *buf_out, u32 *out_size)
{
	u8 pfc_prority = 0;
	u8 err = 0;

	if (!buf_in || !buf_out || *out_size != sizeof(u8) ||
	    in_size != sizeof(u8))
		return -EINVAL;

	pfc_prority = *((u8 *)buf_in);
	if (!((test_bit(HINIC_DCB_ENABLE, &nic_dev->flags)) &&
	      nic_dev->tmp_dcb_cfg.pfc_state)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Need to enable pfc first.\n");
		err = 0xff;
		goto exit;
	}

	hinic_dcbnl_set_pfc_cfg_tool(nic_dev->netdev, pfc_prority);

	err = hinic_dcbnl_set_pfc_tool(nic_dev->netdev);
	if (err) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Failed to set pfc to %x priority\n",
			  pfc_prority);
	}
exit:
	*((u8 *)buf_out) = (u8)err;
	*out_size = sizeof(u8);

	return 0;
}

static int set_pf_bw_limit(struct hinic_nic_dev *nic_dev, void *buf_in,
			   u32 in_size, void *buf_out, u32 *out_size)
{
	u32 pf_bw_limit = 0;
	int err;

	if (hinic_func_type(nic_dev->hwdev) == TYPE_VF) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "To set VF bandwidth rate, please use ip link cmd\n");
		return -EINVAL;
	}

	if (!buf_in || !buf_out || in_size != sizeof(u32) ||
	    *out_size != sizeof(u8))
		return -EINVAL;

	pf_bw_limit = *((u32 *)buf_in);

	err = hinic_set_pf_bw_limit(nic_dev->hwdev, pf_bw_limit);
	if (err) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Failed to set pf bandwidth limit to %d%%\n",
			  pf_bw_limit);
		if (err < 0)
			return err;
	}

	*((u8 *)buf_out) = (u8)err;
	*out_size = sizeof(u8);

	return 0;
}

static int get_pf_bw_limit(struct hinic_nic_dev *nic_dev, void *buf_in,
			   u32 in_size, void *buf_out, u32 *out_size)
{
	u32 pf_bw_limit = 0;
	int err;

	if (hinic_func_type(nic_dev->hwdev) == TYPE_VF) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "To get VF bandwidth rate, please use ip link cmd\n");
		return -EINVAL;
	}

	if (!buf_out || *out_size != sizeof(u32)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect out buf size from user :%d, expect: %lu\n",
			  *out_size, sizeof(u32));
		return -EFAULT;
	}
	err = hinic_dbg_get_pf_bw_limit(nic_dev->hwdev, &pf_bw_limit);
	if (err)
		return err;

	*((u32 *)buf_out) = pf_bw_limit;

	return 0;
}

static int get_poll_weight(struct hinic_nic_dev *nic_dev, void *buf_in,
			   u32 in_size, void *buf_out, u32 *out_size)
{
	struct hinic_nic_poll_weight *weight_info = buf_out;

	if (!buf_out || *out_size != sizeof(*weight_info)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect out buf size from user :%d, expect: %lu\n",
			  *out_size, sizeof(*weight_info));
		return -EFAULT;
	}
	weight_info->poll_weight = nic_dev->poll_weight;
	return 0;
}

static int set_poll_weight(struct hinic_nic_dev *nic_dev, void *buf_in,
			   u32 in_size, void *buf_out, u32 *out_size)
{
	struct hinic_nic_poll_weight *weight_info = buf_in;

	if (!buf_in || in_size != sizeof(*weight_info)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect in buf size from user :%u, expect: %lu\n",
			  *out_size, sizeof(*weight_info));
		return -EFAULT;
	}

	nic_dev->poll_weight = weight_info->poll_weight;
	*out_size = sizeof(u32);
	return 0;
}

static int get_homologue(struct hinic_nic_dev *nic_dev, void *buf_in,
			 u32 in_size, void *buf_out, u32 *out_size)
{
	struct hinic_homologues *homo = buf_out;

	if (!buf_out || *out_size != sizeof(*homo)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect out buf size from user :%d, expect: %lu\n",
			  *out_size, sizeof(*homo));
		return -EFAULT;
	}

	if (test_bit(HINIC_SAME_RXTX, &nic_dev->flags))
		homo->homo_state = HINIC_HOMOLOGUES_ON;
	else
		homo->homo_state = HINIC_HOMOLOGUES_OFF;

	*out_size = sizeof(*homo);

	return 0;
}

static int set_homologue(struct hinic_nic_dev *nic_dev, void *buf_in,
			 u32 in_size, void *buf_out, u32 *out_size)
{
	struct hinic_homologues *homo = buf_in;

	if (!buf_in || in_size != sizeof(*homo)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect in buf size from user :%d, expect: %lu\n",
			  *out_size, sizeof(*homo));
		return -EFAULT;
	}

	if (homo->homo_state == HINIC_HOMOLOGUES_ON) {
		set_bit(HINIC_SAME_RXTX, &nic_dev->flags);
	} else if (homo->homo_state == HINIC_HOMOLOGUES_OFF) {
		clear_bit(HINIC_SAME_RXTX, &nic_dev->flags);
	} else {
		pr_err("Invalid parameters.\n");
		return -EFAULT;
	}

	*out_size = sizeof(*homo);

	return 0;
}

static int get_sset_count(struct hinic_nic_dev *nic_dev, void *buf_in,
			  u32 in_size, void *buf_out, u32 *out_size)
{
	u32 count;

	if (!buf_in || !buf_out || in_size != sizeof(u32) ||
	    *out_size != sizeof(u32)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Invalid parameters.\n");
		return -EINVAL;
	}

	switch (*((u32 *)buf_in)) {
	case HINIC_SHOW_SSET_IO_STATS:
		count = hinic_get_io_stats_size(nic_dev);
		break;

	default:
		count = 0;
		break;
	}

	*((u32 *)buf_out) = count;

	return 0;
}

static int get_sset_stats(struct hinic_nic_dev *nic_dev, void *buf_in,
			  u32 in_size, void *buf_out, u32 *out_size)
{
	struct hinic_show_item *items = buf_out;
	u32 sset, count, size;
	int err;

	if (!buf_in || in_size != sizeof(u32) || !out_size || !buf_out)
		return -EINVAL;

	size = sizeof(u32);
	err = get_sset_count(nic_dev, buf_in, in_size, &count, &size);
	if (err)
		return -EINVAL;

	if (count * sizeof(*items) != *out_size) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unexpect out buf size from user :%d, expect: %lu\n",
			  *out_size, count * sizeof(*items));
		return -EINVAL;
	}

	sset = *((u32 *)buf_in);

	switch (sset) {
	case HINIC_SHOW_SSET_IO_STATS:
		hinic_get_io_stats(nic_dev, items);
		break;

	default:
		nicif_err(nic_dev, drv, nic_dev->netdev, "Unknown %d to get stats\n",
			  sset);
		err = -EINVAL;
		break;
	}

	return err;
}

static int get_func_type(void *hwdev, void *buf_in, u32 in_size,
			 void *buf_out, u32 *out_size)
{
	u16 func_typ;

	func_typ = hinic_func_type(hwdev);
	if (!buf_out || *out_size != sizeof(u16)) {
		pr_err("Unexpect out buf size from user :%d, expect: %lu\n",
		       *out_size, sizeof(u16));
		return -EFAULT;
	}
	*(u16 *)buf_out = func_typ;
	return 0;
}

static int get_func_id(void *hwdev, void *buf_in, u32 in_size,
		       void *buf_out, u32 *out_size)
{
	u16 func_id;

	if (!buf_out || *out_size != sizeof(u16)) {
		pr_err("Unexpect out buf size from user :%d, expect: %lu\n",
		       *out_size, sizeof(u16));
		return -EFAULT;
	}

	func_id = hinic_global_func_id_hw(hwdev);
	*(u16 *)buf_out = func_id;
	*out_size = sizeof(u16);
	return 0;
}

static int get_chip_faults_stats(void *hwdev, void *buf_in, u32 in_size,
				 void *buf_out, u32 *out_size)
{
	int offset = 0;
	struct chip_fault_stats *fault_info;

	if (!buf_in || !buf_out || *out_size != sizeof(*fault_info) ||
	    in_size != sizeof(*fault_info)) {
		pr_err("Unexpect out buf size from user :%d, expect: %lu\n",
		       *out_size, sizeof(*fault_info));
		return -EFAULT;
	}
	fault_info = (struct chip_fault_stats *)buf_in;
	offset = fault_info->offset;
	fault_info = (struct chip_fault_stats *)buf_out;
	hinic_get_chip_fault_stats(hwdev, fault_info->chip_faults, offset);

	return 0;
}

static int get_hw_stats(void *hwdev, void *buf_in, u32 in_size,
			void *buf_out, u32 *out_size)
{
	return hinic_dbg_get_hw_stats(hwdev, buf_out, (u16 *)out_size);
}

static int clear_hw_stats(void *hwdev, void *buf_in, u32 in_size,
			  void *buf_out, u32 *out_size)
{
	*out_size = hinic_dbg_clear_hw_stats(hwdev);
	return 0;
}

static int get_drv_version(void *hwdev, void *buf_in, u32 in_size,
			   void *buf_out, u32 *out_size)
{
	struct drv_version_info *ver_info;
	char ver_str[MAX_VER_INFO_LEN] = {0};
	int err;

	if (*out_size != sizeof(*ver_info)) {
		pr_err("Unexpect out buf size from user :%d, expect: %lu\n",
		       *out_size, sizeof(*ver_info));
		return -EFAULT;
	}
	err = snprintf(ver_str, sizeof(ver_str),
		       "%s  [compiled with the kernel]", HINIC_DRV_VERSION);
	if (err <= 0 || err >= MAX_VER_INFO_LEN) {
		pr_err("Failed snprintf driver version, function return(%d) and dest_len(%d)\n",
		       err, MAX_VER_INFO_LEN);
		return -EFAULT;
	}
	ver_info = (struct drv_version_info *)buf_out;
	memcpy(ver_info->ver, ver_str, sizeof(ver_str));

	return 0;
}

static int get_self_test(void *hwdev, void *buf_in, u32 in_size,
			 void *buf_out, u32 *out_size)
{
	return 0;
}

static int get_chip_id_test(void *hwdev, void *buf_in, u32 in_size,
			    void *buf_out, u32 *out_size)
{
	return 0;
}

static int get_single_card_info(void *hwdev, void *buf_in, u32 in_size,
				void *buf_out, u32 *out_size)
{
	if (!buf_in || !buf_out || in_size != sizeof(struct card_info) ||
	    *out_size != sizeof(struct card_info)) {
		pr_err("Unexpect out buf size from user :%d, expect: %lu\n",
		       *out_size, sizeof(struct card_info));
		return -EFAULT;
	}

	hinic_get_card_info(hwdev, buf_out);
	*out_size = in_size;

	return 0;
}

static int get_device_id(void *hwdev, void *buf_in, u32 in_size,
			 void *buf_out, u32 *out_size)
{
	u16 dev_id;
	int err;

	if (!buf_out || !buf_in || *out_size != sizeof(u16) ||
	    in_size != sizeof(u16)) {
		pr_err("Unexpect out buf size from user :%d, expect: %lu\n",
		       *out_size, sizeof(u16));
		return -EFAULT;
	}

	err = hinic_get_device_id(hwdev, &dev_id);
	if (err)
		return err;

	*((u32 *)buf_out) = dev_id;
	*out_size = in_size;

	return 0;
}

static int is_driver_in_vm(void *hwdev, void *buf_in, u32 in_size,
			   void *buf_out, u32 *out_size)
{
	bool in_host;

	if (!buf_out || (*out_size != sizeof(u8)))
		return -EINVAL;

	in_host = hinic_is_in_host();
	if (in_host)
		*((u8 *)buf_out) = 0;
	else
		*((u8 *)buf_out) = 1;

	return 0;
}

static int get_pf_id(void *hwdev, void *buf_in, u32 in_size,
		     void *buf_out, u32 *out_size)
{
	struct hinic_pf_info *pf_info;
	u32 port_id = 0;
	int err;

	if (!buf_out || (*out_size != sizeof(*pf_info)) ||
	    !buf_in || in_size != sizeof(u32))
		return -EINVAL;

	port_id = *((u32 *)buf_in);
	pf_info = (struct hinic_pf_info *)buf_out;
	err = hinic_get_pf_id(hwdev, port_id, &pf_info->pf_id,
			      &pf_info->isvalid);
	if (err)
		return err;

	*out_size = sizeof(*pf_info);

	return 0;
}

static int __get_card_usr_api_chain_mem(int card_idx)
{
	unsigned char *tmp;
	int i;

	mutex_lock(&g_addr_lock);
	card_id = card_idx;
	if (!g_card_vir_addr[card_idx]) {
		g_card_vir_addr[card_idx] =
			(void *)__get_free_pages(GFP_KERNEL,
						 DBGTOOL_PAGE_ORDER);
		if (!g_card_vir_addr[card_idx]) {
			pr_err("Alloc api chain memory fail for card %d.\n",
			       card_idx);
			mutex_unlock(&g_addr_lock);
			return -EFAULT;
		}

		memset(g_card_vir_addr[card_idx], 0,
		       PAGE_SIZE * (1 << DBGTOOL_PAGE_ORDER));

		g_card_phy_addr[card_idx] =
			virt_to_phys(g_card_vir_addr[card_idx]);
		if (!g_card_phy_addr[card_idx]) {
			pr_err("phy addr for card %d is 0.\n", card_idx);
			free_pages((unsigned long)g_card_vir_addr[card_idx],
				   DBGTOOL_PAGE_ORDER);
			g_card_vir_addr[card_idx] = NULL;
			mutex_unlock(&g_addr_lock);
			return -EFAULT;
		}

		tmp = g_card_vir_addr[card_idx];
		for (i = 0; i < (1 << DBGTOOL_PAGE_ORDER); i++) {
			SetPageReserved(virt_to_page(tmp));
			tmp += PAGE_SIZE;
		}
	}
	mutex_unlock(&g_addr_lock);

	return 0;
}

static int get_pf_dev_info(char *dev_name, struct msg_module *nt_msg)
{
	struct pf_dev_info dev_info[16] = { {0} };
	struct card_node *card_info = NULL;
	int i;
	int err;

	if (nt_msg->lenInfo.outBuffLen != (sizeof(dev_info) * 16) ||
	    nt_msg->lenInfo.inBuffLen != (sizeof(dev_info) * 16)) {
		pr_err("Invalid out_buf_size %d or Invalid in_buf_size %d, expect %lu\n",
		       nt_msg->lenInfo.outBuffLen, nt_msg->lenInfo.inBuffLen,
		       (sizeof(dev_info) * 16));
		return -EINVAL;
	}

	for (i = 0; i < MAX_CARD_NUM; i++) {
		card_info = (struct card_node *)g_card_node_array[i];
		if (!card_info)
			continue;
		if (!strncmp(dev_name, card_info->chip_name, IFNAMSIZ))
			break;
	}

	if (i == MAX_CARD_NUM || !card_info) {
		pr_err("Can't find this card %s\n", dev_name);
		return -EFAULT;
	}

	err = __get_card_usr_api_chain_mem(i);
	if (err) {
		pr_err("Faile to get api chain memory for userspace %s\n",
		       dev_name);
		return -EFAULT;
	}

	chipif_get_all_pf_dev_info(dev_info, i,
				   card_info->func_handle_array);

	/* Copy the dev_info to user mode */
	if (copy_to_user(nt_msg->out_buf, dev_info, sizeof(dev_info))) {
		pr_err("Copy dev_info to user fail\n");
		return -EFAULT;
	}

	return 0;
}

static int knl_free_mem(char *dev_name, struct msg_module *nt_msg)
{
	struct card_node *card_info = NULL;
	int i;

	for (i = 0; i < MAX_CARD_NUM; i++) {
		card_info = (struct card_node *)g_card_node_array[i];
		if (!card_info)
			continue;
		if (!strncmp(dev_name, card_info->chip_name, IFNAMSIZ))
			break;
	}

	if (i == MAX_CARD_NUM || !card_info) {
		pr_err("Can't find this card %s\n", dev_name);
		return -EFAULT;
	}

	dbgtool_knl_free_mem(i);

	return 0;
}

extern void hinic_get_card_func_info_by_card_name(
	const char *chip_name, struct hinic_card_func_info *card_func);

static int get_card_func_info(char *dev_name, struct msg_module *nt_msg)
{
	struct hinic_card_func_info card_func_info = {0};
	int id, err;

	if (nt_msg->lenInfo.outBuffLen != sizeof(card_func_info) ||
	    nt_msg->lenInfo.inBuffLen != sizeof(card_func_info)) {
		pr_err("Invalid out_buf_size %d or Invalid in_buf_size %d, expect %lu\n",
		       nt_msg->lenInfo.outBuffLen, nt_msg->lenInfo.inBuffLen,
		       sizeof(card_func_info));
		return -EINVAL;
	}

	err = memcmp(dev_name, HINIC_CHIP_NAME, strlen(HINIC_CHIP_NAME));
	if (err) {
		pr_err("Invalid chip name %s\n", dev_name);
		return err;
	}

	err = sscanf(dev_name, HINIC_CHIP_NAME "%d", &id);
	if (err <= 0) {
		pr_err("Failed to get hinic id\n");
		return err;
	}

	if (id >= MAX_CARD_NUM) {
		pr_err("chip id %d exceed limit[0-%d]\n", id, MAX_CARD_NUM - 1);
		return -EINVAL;
	}

	hinic_get_card_func_info_by_card_name(dev_name, &card_func_info);

	if (!card_func_info.num_pf) {
		pr_err("None function found for %s\n", dev_name);
		return -EFAULT;
	}

	err = __get_card_usr_api_chain_mem(id);
	if (err) {
		pr_err("Faile to get api chain memory for userspace %s\n",
		       dev_name);
		return -EFAULT;
	}

	card_func_info.usr_api_phy_addr = g_card_phy_addr[id];

	/* Copy the dev_info to user mode */
	if (copy_to_user(nt_msg->out_buf, &card_func_info,
			 sizeof(card_func_info))) {
		pr_err("Copy dev_info to user fail\n");
		return -EFAULT;
	}

	return 0;
}

#define GET_FIRMWARE_ACTIVE_STATUS_TIMEOUT	30
static int get_firmware_active_status(void *hwdev, void *buf_in, u32 in_size,
				      void *buf_out, u32 *out_size)
{
	u32 loop_cnt = 0;

	while (loop_cnt < GET_FIRMWARE_ACTIVE_STATUS_TIMEOUT) {
		if (!hinic_get_mgmt_channel_status(hwdev))
			return 0;

		msleep(1000);
		loop_cnt++;
	}
	if (loop_cnt == GET_FIRMWARE_ACTIVE_STATUS_TIMEOUT)
		return -ETIMEDOUT;

	return 0;
}


struct nic_drv_module_handle nic_driv_module_cmd_handle[] = {
	{TX_INFO,		get_tx_info},
	{Q_NUM,			get_q_num},
	{TX_WQE_INFO,		get_tx_wqe_info},
	{RX_INFO,		get_rx_info},
	{RX_WQE_INFO,		get_rx_wqe_info},
	{RX_CQE_INFO,		get_rx_cqe_info},
	{GET_INTER_NUM,		get_inter_num},
	{CLEAR_FUNC_STASTIC,	clear_func_static},
	{GET_NUM_COS,		get_num_cos},
	{GET_COS_UP_MAP,	get_dcb_cos_up_map},
	{SET_COS_UP_MAP,	set_dcb_cos_up_map},
	{GET_LOOPBACK_MODE,	get_loopback_mode},
	{SET_LOOPBACK_MODE,	set_loopback_mode},
	{SET_LINK_MODE,		set_link_mode},
	{SET_PF_BW_LIMIT,	set_pf_bw_limit},
	{GET_PF_BW_LIMIT,	get_pf_bw_limit},
	{GET_POLL_WEIGHT,       get_poll_weight},
	{SET_POLL_WEIGHT,       set_poll_weight},
	{GET_HOMOLOGUE,         get_homologue},
	{SET_HOMOLOGUE,         set_homologue},
	{GET_SSET_COUNT,	get_sset_count},
	{GET_SSET_ITEMS,	get_sset_stats},
	{SET_PFC_CONTROL,	set_pfc_control},
	{SET_ETS,		set_ets},
	{GET_ETS_INFO,		get_ets_info},
	{SET_PFC_PRIORITY,	set_pfc_priority},
	{SET_DCB_CFG,		set_dcb_cfg},
	{GET_PFC_INFO,		get_pfc_info},
	{GET_SUPPORT_UP,	get_support_up},
	{GET_SUPPORT_TC,	get_support_tc},
};

struct hw_drv_module_handle hw_driv_module_cmd_handle[] = {
	{FUNC_TYPE,		get_func_type},
	{GET_FUNC_IDX,		get_func_id},
	{GET_DRV_VERSION,	get_drv_version},
	{GET_HW_STATS,		get_hw_stats},
	{CLEAR_HW_STATS,	clear_hw_stats},
	{GET_SELF_TEST_RES,	get_self_test},
	{GET_CHIP_FAULT_STATS,	get_chip_faults_stats},
	{GET_CHIP_ID,		get_chip_id_test},
	{GET_SINGLE_CARD_INFO,	get_single_card_info},
	{GET_FIRMWARE_ACTIVE_STATUS, get_firmware_active_status},
	{GET_DEVICE_ID,		get_device_id},
	{IS_DRV_IN_VM,		is_driver_in_vm},
	{GET_PF_ID,		get_pf_id},
};

static int send_to_nic_driver(struct hinic_nic_dev *nic_dev,
			      u32 cmd, void *buf_in,
			      u32 in_size, void *buf_out, u32 *out_size)
{
	int index, num_cmds = sizeof(nic_driv_module_cmd_handle) /
				sizeof(nic_driv_module_cmd_handle[0]);
	enum driver_cmd_type cmd_type = (enum driver_cmd_type)cmd;
	int err = 0;

	mutex_lock(&nic_dev->nic_mutex);
	for (index = 0; index < num_cmds; index++) {
		if (cmd_type ==
			nic_driv_module_cmd_handle[index].driv_cmd_name) {
			err = nic_driv_module_cmd_handle[index].driv_func
					(nic_dev, buf_in,
					 in_size, buf_out, out_size);
			break;
		}
	}
	mutex_unlock(&nic_dev->nic_mutex);

	if (index == num_cmds)
		return -EINVAL;
	return err;
}

static int send_to_hw_driver(void *hwdev, struct msg_module *nt_msg,
			     void *buf_in, u32 in_size, void *buf_out,
			     u32 *out_size)
{
	int index, num_cmds = sizeof(hw_driv_module_cmd_handle) /
				sizeof(hw_driv_module_cmd_handle[0]);
	enum driver_cmd_type cmd_type =
				(enum driver_cmd_type)(nt_msg->msg_formate);
	int err = 0;

	for (index = 0; index < num_cmds; index++) {
		if (cmd_type ==
			hw_driv_module_cmd_handle[index].driv_cmd_name) {
			err = hw_driv_module_cmd_handle[index].driv_func
					(hwdev, buf_in,
					 in_size, buf_out, out_size);
			break;
		}
	}

	if (index == num_cmds)
		return -EINVAL;

	return err;
}

static int send_to_ucode(void *hwdev, struct msg_module *nt_msg,
			 void *buf_in, u32 in_size, void *buf_out,
			 u32 *out_size)
{
	int ret = 0;

	if (nt_msg->ucode_cmd.ucode_db.ucode_imm) {
		ret = hinic_cmdq_direct_resp
			(hwdev, nt_msg->ucode_cmd.ucode_db.cmdq_ack_type,
			 nt_msg->ucode_cmd.ucode_db.comm_mod_type,
			 nt_msg->ucode_cmd.ucode_db.ucode_cmd_type,
			 buf_in, buf_out, 0);
		if (ret)
			pr_err("Send direct cmdq err: %d!\n", ret);
	} else {
		ret = hinic_cmdq_detail_resp
			(hwdev, nt_msg->ucode_cmd.ucode_db.cmdq_ack_type,
			 nt_msg->ucode_cmd.ucode_db.comm_mod_type,
			 nt_msg->ucode_cmd.ucode_db.ucode_cmd_type,
			 buf_in, buf_out, 0);
		if (ret)
			pr_err("Send detail cmdq err: %d!\n", ret);
	}

	return ret;
}

static int api_csr_read(void *hwdev, struct msg_module *nt_msg,
			void *buf_in, u32 in_size, void *buf_out, u32 *out_size)
{
	struct up_log_msg_st *up_log_msg = (struct up_log_msg_st *)buf_in;
	int ret = 0;
	u32 rd_len;
	u32 rd_addr;
	u32 rd_cnt = 0;
	u32 offset = 0;
	u8 node_id;
	u32 i;

	if (!buf_in || !buf_out || in_size != sizeof(*up_log_msg) ||
	    *out_size != up_log_msg->rd_len)
		return -EINVAL;

	rd_len = up_log_msg->rd_len;
	rd_addr = up_log_msg->addr;
	node_id = (u8)nt_msg->up_cmd.up_db.comm_mod_type;

	rd_cnt = rd_len / 4;

	if (rd_len % 4)
		rd_cnt++;

	for (i = 0; i < rd_cnt; i++) {
		ret = hinic_api_csr_rd32(hwdev, node_id,
					 rd_addr + offset,
					 (u32 *)(((u8 *)buf_out) + offset));
		if (ret) {
			pr_err("Csr rd fail, err: %d, node_id: %d, csr addr: 0x%08x\n",
			       ret, node_id, rd_addr + offset);
			return ret;
		}
		offset += 4;
	}
	*out_size = rd_len;

	return ret;
}

static int api_csr_write(void *hwdev, struct msg_module *nt_msg,
			 void *buf_in, u32 in_size, void *buf_out,
			 u32 *out_size)
{
	struct csr_write_st *csr_write_msg = (struct csr_write_st *)buf_in;
	int ret = 0;
	u32 rd_len;
	u32 rd_addr;
	u32 rd_cnt = 0;
	u32 offset = 0;
	u8 node_id;
	u32 i;
	u8 *data;

	if (!buf_in || in_size != sizeof(*csr_write_msg))
		return -EINVAL;

	rd_len = csr_write_msg->rd_len;
	rd_addr = csr_write_msg->addr;
	node_id = (u8)nt_msg->up_cmd.up_db.comm_mod_type;

	if (rd_len % 4) {
		pr_err("Csr length must be a multiple of 4\n");
		return -EFAULT;
	}

	rd_cnt = rd_len / 4;
	data = kzalloc(rd_len, GFP_KERNEL);
	if (!data) {
		pr_err("No more memory\n");
		return -EFAULT;
	}
	if (copy_from_user(data, (void *)csr_write_msg->data, rd_len)) {
		pr_err("Copy information from user failed\n");
		kfree(data);
		return -EFAULT;
	}

	for (i = 0; i < rd_cnt; i++) {
		ret = hinic_api_csr_wr32(hwdev, node_id,
					 rd_addr + offset,
					 *((u32 *)(data + offset)));
		if (ret) {
			pr_err("Csr wr fail, ret: %d, node_id: %d, csr addr: 0x%08x\n",
			       ret, rd_addr + offset, node_id);
			kfree(data);
			return ret;
		}
		offset += 4;
	}

	*out_size = 0;
	kfree(data);
	return ret;
}

static u32 get_up_timeout_val(enum hinic_mod_type mod, u8 cmd)
{
	if (mod == HINIC_MOD_L2NIC && cmd == NIC_UP_CMD_UPDATE_FW)
		return UP_UPDATEFW_TIME_OUT_VAL;
	else
		return UP_COMP_TIME_OUT_VAL;
}

static int check_useparam_valid(struct msg_module *nt_msg, void *buf_in)
{
	struct csr_write_st *csr_write_msg = (struct csr_write_st *)buf_in;
	u32 rd_len = csr_write_msg->rd_len;

	if (rd_len > TOOL_COUNTER_MAX_LEN) {
		pr_err("Csr read or write len is invalid!\n");
		return -EINVAL;
	}

	return 0;
}

static int send_to_up(void *hwdev, struct msg_module *nt_msg,
		      void *buf_in, u32 in_size, void *buf_out, u32 *out_size)
{
	int ret = 0;

	if ((nt_msg->up_cmd.up_db.up_api_type == API_CMD) ||
	    (nt_msg->up_cmd.up_db.up_api_type == API_CLP)) {
		enum hinic_mod_type mod;
		u8 cmd;
		u32 timeout;

		mod = (enum hinic_mod_type)nt_msg->up_cmd.up_db.comm_mod_type;
		cmd = nt_msg->up_cmd.up_db.chipif_cmd;

		timeout = get_up_timeout_val(mod, cmd);

		if (nt_msg->up_cmd.up_db.up_api_type == API_CMD)
			ret = hinic_msg_to_mgmt_sync(hwdev, mod, cmd,
						     buf_in, (u16)in_size,
						     buf_out, (u16 *)out_size,
						     timeout);
		else
			ret = hinic_clp_to_mgmt(hwdev, mod, cmd,
						buf_in, (u16)in_size,
						buf_out, (u16 *)out_size);
		if (ret) {
			pr_err("Message to mgmt cpu return fail, mod: %d, cmd: %d\n",
			       mod, cmd);
			return ret;
		}

	} else if (nt_msg->up_cmd.up_db.up_api_type == API_CHAIN) {
		if (check_useparam_valid(nt_msg, buf_in))
			return -EINVAL;

		if (nt_msg->up_cmd.up_db.chipif_cmd == API_CSR_WRITE) {
			ret = api_csr_write(hwdev, nt_msg, buf_in,
					    in_size, buf_out, out_size);
			return ret;
		}

		ret = api_csr_read(hwdev, nt_msg, buf_in,
				   in_size, buf_out, out_size);
	}

	return ret;
}

static int sm_rd32(void *hwdev, u32 id, u8 instance,
		   u8 node, struct sm_out_st *buf_out)
{
	u32 val1;
	int ret;

	ret = hinic_sm_ctr_rd32(hwdev, node, instance, id, &val1);
	if (ret) {
		pr_err("Get sm ctr information (32 bits)failed!\n");
		val1 = 0xffffffff;
	}

	buf_out->val1 = val1;

	return ret;
}

static int sm_rd64_pair(void *hwdev, u32 id, u8 instance,
			u8 node, struct sm_out_st *buf_out)
{
	u64 val1 = 0, val2 = 0;
	int ret;

	ret = hinic_sm_ctr_rd64_pair(hwdev, node, instance, id, &val1, &val2);
	if (ret) {
		pr_err("Get sm ctr information (64 bits pair)failed!\n");
		val1 = 0xffffffff;
	}

	buf_out->val1 = val1;
	buf_out->val2 = val2;

	return ret;
}

static int sm_rd64(void *hwdev, u32 id, u8 instance,
		   u8 node, struct sm_out_st *buf_out)
{
	u64 val1;
	int ret;

	ret = hinic_sm_ctr_rd64(hwdev, node, instance, id, &val1);
	if (ret) {
		pr_err("Get sm ctr information (64 bits)failed!\n");
		val1 = 0xffffffff;
	}
	buf_out->val1 = val1;

	return ret;
}

typedef int (*sm_module)(void *hwdev, u32 id, u8 instance,
			 u8 node, struct sm_out_st *buf_out);

struct sm_module_handle {
	enum sm_cmd_type	smCmdName;
	sm_module		smFunc;
};

struct sm_module_handle sm_module_cmd_handle[] = {
	{SM_CTR_RD32,		sm_rd32},
	{SM_CTR_RD64_PAIR,	sm_rd64_pair},
	{SM_CTR_RD64,		sm_rd64}
};

static int send_to_sm(void *hwdev, struct msg_module *nt_msg,
		      void *buf_in, u32 in_size, void *buf_out, u32 *out_size)
{
	struct sm_in_st *sm_in = buf_in;
	struct sm_out_st *sm_out = buf_out;
	u32 msg_formate = nt_msg->msg_formate;
	int index, num_cmds = sizeof(sm_module_cmd_handle) /
				sizeof(sm_module_cmd_handle[0]);
	int ret = 0;

	if (!buf_in || !buf_out || in_size != sizeof(*sm_in) ||
	    *out_size != sizeof(*sm_out))
		return -EINVAL;

	for (index = 0; index < num_cmds; index++) {
		if (msg_formate == sm_module_cmd_handle[index].smCmdName)
			ret = sm_module_cmd_handle[index].smFunc(hwdev,
						(u32)sm_in->id,
						(u8)sm_in->instance,
						(u8)sm_in->node, sm_out);
	}

	if (ret)
		pr_err("Get sm information fail!\n");

	*out_size = sizeof(struct sm_out_st);

	return ret;
}

static bool is_hwdev_cmd_support(unsigned int mod,
				 char *ifname, u32 up_api_type)
{
	void *hwdev;

	hwdev = hinic_get_hwdev_by_ifname(ifname);
	if (!hwdev) {
		pr_err("Can not get the device %s correctly\n", ifname);
		return false;
	}

	switch (mod) {
	case SEND_TO_UP:
	case SEND_TO_SM:
		if (FUNC_SUPPORT_MGMT(hwdev)) {
			if (up_api_type == API_CLP) {
				if (!hinic_is_hwdev_mod_inited
					(hwdev, HINIC_HWDEV_CLP_INITED)) {
					pr_err("CLP have not initialized\n");
					return false;
				}
			} else if (!hinic_is_hwdev_mod_inited
					(hwdev, HINIC_HWDEV_MGMT_INITED)) {
				pr_err("MGMT have not initialized\n");
				return false;
			}
		} else if (!hinic_is_hwdev_mod_inited
				(hwdev, HINIC_HWDEV_MBOX_INITED)) {
			pr_err("MBOX have not initialized\n");
			return false;
		}

		if (mod == SEND_TO_SM &&
		    ((hinic_func_type(hwdev) == TYPE_VF) ||
		     (!hinic_is_hwdev_mod_inited(hwdev,
						 HINIC_HWDEV_MGMT_INITED)))) {
			pr_err("Current function do not support this cmd\n");
			return false;
		}
		break;

	case SEND_TO_UCODE:
		if (!hinic_is_hwdev_mod_inited(hwdev,
					       HINIC_HWDEV_CMDQ_INITED)) {
			pr_err("CMDQ have not initialized\n");
			return false;
		}
		break;

	default:
		return false;
	}

	return true;
}

static bool nictool_k_is_cmd_support(unsigned int mod,
				     char *ifname, u32 up_api_type)
{
	enum hinic_init_state init_state =
			hinic_get_init_state_by_ifname(ifname);
	bool support = true;

	if (init_state == HINIC_INIT_STATE_NONE)
		return false;

	if (mod == SEND_TO_NIC_DRIVER) {
		if (init_state < HINIC_INIT_STATE_NIC_INITED) {
			pr_err("NIC driver have not initialized\n");
			return false;
		}
	} else if (mod >= SEND_TO_UCODE && mod <= SEND_TO_SM) {
		return is_hwdev_cmd_support(mod, ifname, up_api_type);
	} else if ((mod >= HINICADM_OVS_DRIVER &&
		   mod <= HINICADM_FCOE_DRIVER) ||
		   mod == SEND_TO_HW_DRIVER) {
		if (init_state < HINIC_INIT_STATE_HWDEV_INITED) {
			pr_err("Hwdev have not initialized\n");
			return false;
		}
	} else {
		pr_err("Unsupport mod %d\n", mod);
		support = false;
	}

	return support;
}

static int alloc_tmp_buf(void *hwdev, struct msg_module *nt_msg, u32 in_size,
			 void **buf_in, u32 out_size, void **buf_out)
{
	int ret;

	ret = alloc_buff_in(hwdev, nt_msg, in_size, buf_in);
	if (ret) {
		pr_err("Alloc tool cmd buff in failed\n");
		return ret;
	}

	ret = alloc_buff_out(hwdev, nt_msg, out_size, buf_out);
	if (ret) {
		pr_err("Alloc tool cmd buff out failed\n");
		goto out_free_buf_in;
	}

	return 0;

out_free_buf_in:
	free_buff_in(hwdev, nt_msg, *buf_in);

	return ret;
}

static void free_tmp_buf(void *hwdev, struct msg_module *nt_msg,
			 void *buf_in, void *buf_out)
{
	free_buff_out(hwdev, nt_msg, buf_out);
	free_buff_in(hwdev, nt_msg, buf_in);
}

static int get_self_test_cmd(struct msg_module *nt_msg)
{
	int ret;
	u32 res = 0;

	ret = hinic_get_self_test_result(nt_msg->device_name, &res);
	if (ret) {
		pr_err("Get self test result failed!\n");
		return -EFAULT;
	}

	ret = copy_buf_out_to_user(nt_msg, sizeof(res), &res);
	if (ret)
		pr_err("%s:%d:: Copy to user failed\n", __func__, __LINE__);

	return ret;
}

static int get_all_chip_id_cmd(struct msg_module *nt_msg)
{
	struct nic_card_id card_id;

	memset(&card_id, 0, sizeof(card_id));

	hinic_get_all_chip_id((void *)&card_id);

	if (copy_to_user(nt_msg->out_buf, &card_id, sizeof(card_id))) {
		pr_err("Copy chip id to user failed\n");
		return -EFAULT;
	}

	return 0;
}

int nic_ioctl(void *uld_dev, u32 cmd, void *buf_in,
	      u32 in_size, void *buf_out, u32 *out_size)
{
	return send_to_nic_driver(uld_dev, cmd, buf_in,
				  in_size, buf_out, out_size);
}

static void *__get_dev_support_nic_cmd(struct msg_module *nt_msg,
				       enum hinic_service_type type)
{
	void *uld_dev = NULL;

	/* set/get qos must use chip_name(hinic0) */
	switch (nt_msg->msg_formate) {
	case GET_COS_UP_MAP:
	case SET_COS_UP_MAP:
	case GET_NUM_COS:
		uld_dev = hinic_get_uld_by_chip_name(nt_msg->device_name, type);
		if (!uld_dev)
			pr_err("Get/set cos_up must use chip_name(hinic0)\n");

		return uld_dev;

	default:
		break;
	}

	uld_dev = hinic_get_uld_dev_by_ifname(nt_msg->device_name, type);
	if (!uld_dev)
		pr_err("Can not get the uld dev correctly: %s, nic driver may be not register\n",
		       nt_msg->device_name);

	return uld_dev;
}

static void *get_support_uld_dev(struct msg_module *nt_msg,
				 enum hinic_service_type type)
{
	char *service_name[SERVICE_T_MAX] = {"NIC", "OVS", "ROCE", "TOE",
					     "IWARP", "FC", "FCOE"};
	void *hwdev = NULL;
	void *uld_dev = NULL;

	switch (nt_msg->module) {
	case SEND_TO_NIC_DRIVER:
		hwdev = hinic_get_hwdev_by_ifname(nt_msg->device_name);
		if (!hinic_support_nic(hwdev, NULL)) {
			pr_err("Current function don't support NIC\n");
			return NULL;
		}
		return __get_dev_support_nic_cmd(nt_msg, type);
	default:
		break;
	}

	uld_dev = hinic_get_uld_dev_by_ifname(nt_msg->device_name, type);
	if (!uld_dev)
		pr_err("Can not get the uld dev correctly: %s, %s driver may be not register\n",
		       nt_msg->device_name, service_name[type]);

	return uld_dev;
}

static int get_service_drv_version(void *hwdev, struct msg_module *nt_msg,
				   void *buf_in, u32 in_size, void *buf_out,
				   u32 *out_size)
{
	enum hinic_service_type type;
	int ret = 0;

	type = nt_msg->module - SEND_TO_SM;
	*out_size = sizeof(struct drv_version_info);

	if (!g_uld_info[type].ioctl)
		return ret;

	ret = g_uld_info[type].ioctl(NULL, nt_msg->msg_formate, buf_in, in_size,
				     buf_out, out_size);
	if (ret)
		return ret;

	if (copy_to_user(nt_msg->out_buf, buf_out, *out_size))
		return -EFAULT;

	return ret;
}

int send_to_service_driver(struct msg_module *nt_msg, void *buf_in,
			   u32 in_size, void *buf_out, u32 *out_size)
{
	enum hinic_service_type type;
	void *uld_dev;
	int ret = -EINVAL;

	if (nt_msg->module == SEND_TO_NIC_DRIVER)
		type = SERVICE_T_NIC;
	else
		type = nt_msg->module - SEND_TO_SM;

	if (type < SERVICE_T_MAX) {
		uld_dev = get_support_uld_dev(nt_msg, type);
		if (!uld_dev)
			return -EINVAL;

		if (g_uld_info[type].ioctl)
			ret = g_uld_info[type].ioctl(uld_dev,
						     nt_msg->msg_formate,
						     buf_in, in_size, buf_out,
						     out_size);
	} else {
		pr_err("Ioctl input module id: %d is incorrectly\n",
		       nt_msg->module);
	}

	return ret;
}

static int nictool_exec_cmd(void *hwdev, struct msg_module *nt_msg,
			    void *buf_in, u32 in_size, void *buf_out,
			    u32 *out_size)
{
	int ret;

	switch (nt_msg->module) {
	case SEND_TO_HW_DRIVER:
		ret = send_to_hw_driver(hwdev, nt_msg, buf_in,
					in_size, buf_out, out_size);
		break;
	case SEND_TO_UP:
		ret = send_to_up(hwdev, nt_msg, buf_in,
				 in_size, buf_out, out_size);
		break;
	case SEND_TO_UCODE:
		ret = send_to_ucode(hwdev, nt_msg, buf_in,
				    in_size, buf_out, out_size);
		break;
	case SEND_TO_SM:
		ret = send_to_sm(hwdev, nt_msg, buf_in,
				 in_size, buf_out, out_size);
		break;
	default:
		ret = send_to_service_driver(nt_msg, buf_in, in_size, buf_out,
					     out_size);
		break;
	}

	return ret;
}

static bool hinic_is_special_handling_cmd(struct msg_module *nt_msg, int *ret)
{
	unsigned int cmd_raw = nt_msg->module;

	/* Get self test result directly whatever driver probe success or not */
	if (cmd_raw == SEND_TO_HW_DRIVER &&
	    nt_msg->msg_formate == GET_SELF_TEST_RES) {
		*ret = get_self_test_cmd(nt_msg);
		return true;
	}

	if (cmd_raw == SEND_TO_HW_DRIVER &&
	    nt_msg->msg_formate == GET_CHIP_ID) {
		*ret = get_all_chip_id_cmd(nt_msg);
		return true;
	}

	if (cmd_raw == SEND_TO_HW_DRIVER &&
	    nt_msg->msg_formate == GET_PF_DEV_INFO) {
		*ret = get_pf_dev_info(nt_msg->device_name, nt_msg);
		return true;
	}

	if (cmd_raw == SEND_TO_HW_DRIVER &&
	    nt_msg->msg_formate == CMD_FREE_MEM) {
		*ret = knl_free_mem(nt_msg->device_name, nt_msg);
		return true;
	}

	if (cmd_raw == SEND_TO_HW_DRIVER &&
	    nt_msg->msg_formate == GET_CHIP_INFO) {
		*ret = get_card_func_info(nt_msg->device_name, nt_msg);
		return true;
	}

	return false;
}

static long nictool_k_unlocked_ioctl(struct file *pfile,
				     unsigned int cmd, unsigned long arg)
{
	void *hwdev;
	struct msg_module nt_msg;
	void *buf_out = NULL;
	void *buf_in = NULL;
	u32 out_size_expect = 0;
	u32 out_size = 0;
	u32 in_size = 0;
	unsigned int cmd_raw = 0;
	int ret = 0;

	memset(&nt_msg, 0, sizeof(nt_msg));

	if (copy_from_user(&nt_msg, (void *)arg, sizeof(nt_msg))) {
		pr_err("Copy information from user failed\n");
		return -EFAULT;
	}

	/* end with '\0' */
	nt_msg.device_name[IFNAMSIZ - 1] = '\0';

	cmd_raw = nt_msg.module;

	out_size_expect = nt_msg.lenInfo.outBuffLen;
	in_size = nt_msg.lenInfo.inBuffLen;

	hinic_tool_cnt_inc();

	if (hinic_is_special_handling_cmd(&nt_msg, &ret))
		goto out_free_lock;

	if (cmd_raw == HINICADM_FC_DRIVER &&
	    nt_msg.msg_formate == GET_CHIP_ID)
		get_fc_devname(nt_msg.device_name);

	if (!nictool_k_is_cmd_support(cmd_raw, nt_msg.device_name,
				      nt_msg.up_cmd.up_db.up_api_type)) {
		ret = -EFAULT;
		goto out_free_lock;
	}

	/* get the netdevice */
	hwdev = hinic_get_hwdev_by_ifname(nt_msg.device_name);
	if (!hwdev) {
		pr_err("Can not get the device %s correctly\n",
		       nt_msg.device_name);
		ret = -ENODEV;
		goto out_free_lock;
	}

	ret = alloc_tmp_buf(hwdev, &nt_msg, in_size,
			    &buf_in, out_size_expect, &buf_out);
	if (ret) {
		pr_err("Alloc tmp buff failed\n");
		goto out_free_lock;
	}

	out_size = out_size_expect;

	if (nt_msg.msg_formate == GET_DRV_VERSION &&
	    (cmd_raw == HINICADM_FC_DRIVER || cmd_raw == HINICADM_TOE_DRIVER)) {
		ret = get_service_drv_version(hwdev, &nt_msg, buf_in,
					      in_size, buf_out, &out_size);
		goto out_free_buf;
	}

	ret = nictool_exec_cmd(hwdev, &nt_msg, buf_in,
			       in_size, buf_out, &out_size);
	if (ret)
		goto out_free_buf;

	ret = copy_buf_out_to_user(&nt_msg, out_size_expect, buf_out);
	if (ret)
		pr_err("Copy information to user failed\n");

out_free_buf:
	free_tmp_buf(hwdev, &nt_msg, buf_in, buf_out);

out_free_lock:
	hinic_tool_cnt_dec();

	return (long)ret;
}

static int nictool_k_open(struct inode *pnode, struct file *pfile)
{
	return 0;
}

static ssize_t nictool_k_read(struct file *pfile, char __user *ubuf,
			      size_t size, loff_t *ppos)
{
	return 0;
}

static ssize_t nictool_k_write(struct file *pfile, const char __user *ubuf,
			       size_t size, loff_t *ppos)
{
	return 0;
}

static const struct file_operations fifo_operations = {
	.owner = THIS_MODULE,
	.open = nictool_k_open,
	.read = nictool_k_read,
	.write = nictool_k_write,
	.unlocked_ioctl = nictool_k_unlocked_ioctl,
	.mmap = hinic_mem_mmap,
};

int if_nictool_exist(void)
{
	struct file *fp = NULL;
	int exist = 0;

	fp = filp_open(HIADM_DEV_PATH, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		exist = 0;
	} else {
		(void)filp_close(fp, NULL);
		exist = 1;
	}

	return exist;
}

/**
 * nictool_k_init - initialize the hw interface
 */
int nictool_k_init(void)
{
	int ret;
	struct device *pdevice;

	if (g_nictool_init_flag) {
		g_nictool_ref_cnt++;
		/* already initialized */
		return 0;
	}

	if (if_nictool_exist()) {
		pr_err("Nictool device exists\n");
		return 0;
	}

	/* Device ID: primary device ID (12bit) |
	 * secondary device number (20bit)
	 */
	g_dev_id = MKDEV(MAJOR_DEV_NUM, 0);

	/* Static device registration number */
	ret = register_chrdev_region(g_dev_id, 1, HIADM_DEV_NAME);
	if (ret < 0) {
		ret = alloc_chrdev_region(&g_dev_id, 0, 1, HIADM_DEV_NAME);
		if (ret < 0) {
			pr_err("Register nictool_dev fail(0x%x)\n", ret);
			return ret;
		}
	}

	/* Create equipment */
	g_nictool_class = class_create(THIS_MODULE, HIADM_DEV_CLASS);
	if (IS_ERR(g_nictool_class)) {
		pr_err("Create nictool_class fail\n");
		ret = -EFAULT;
		goto class_create_err;
	}

	/* Initializing the character device */
	cdev_init(&g_nictool_cdev, &fifo_operations);

	/* Add devices to the operating system */
	ret = cdev_add(&g_nictool_cdev, g_dev_id, 1);
	if (ret < 0) {
		pr_err("Add nictool_dev to operating system fail(0x%x)\n", ret);
		goto cdev_add_err;
	}

	/* Export device information to user space
	 * (/sys/class/class name/device name)
	 */
	pdevice = device_create(g_nictool_class, NULL,
				g_dev_id, NULL, HIADM_DEV_NAME);
	if (IS_ERR(pdevice)) {
		pr_err("Export nictool device information to user space fail\n");
		ret = -EFAULT;
		goto device_create_err;
	}

	g_nictool_init_flag = 1;
	g_nictool_ref_cnt = 1;

	pr_info("Register nictool_dev to system succeed\n");

	return 0;

device_create_err:
	cdev_del(&g_nictool_cdev);

cdev_add_err:
	class_destroy(g_nictool_class);

class_create_err:
	g_nictool_class = NULL;
	unregister_chrdev_region(g_dev_id, 1);

	return ret;
}

void nictool_k_uninit(void)
{
	if (g_nictool_init_flag) {
		if ((--g_nictool_ref_cnt))
			return;
	}

	g_nictool_init_flag = 0;

	if (!g_nictool_class || IS_ERR(g_nictool_class))
		return;

	cdev_del(&g_nictool_cdev);
	device_destroy(g_nictool_class, g_dev_id);
	class_destroy(g_nictool_class);
	g_nictool_class = NULL;

	unregister_chrdev_region(g_dev_id, 1);

	pr_info("Unregister nictool_dev succeed\n");
}
