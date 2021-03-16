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

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/completion.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hwdev.h"
#include "hinic_csr.h"
#include "hinic_hwif.h"
#include "hinic_mbox.h"

#define HINIC_MBOX_INT_DST_FUNC_SHIFT				0
#define HINIC_MBOX_INT_DST_AEQN_SHIFT				10
#define HINIC_MBOX_INT_SRC_RESP_AEQN_SHIFT			12
#define HINIC_MBOX_INT_STAT_DMA_SHIFT				14
/* The size of data to be send (unit of 4 bytes) */
#define HINIC_MBOX_INT_TX_SIZE_SHIFT				20
/* SO_RO(strong order, relax order) */
#define HINIC_MBOX_INT_STAT_DMA_SO_RO_SHIFT			25
#define HINIC_MBOX_INT_WB_EN_SHIFT				28

#define HINIC_MBOX_INT_DST_FUNC_MASK				0x3FF
#define HINIC_MBOX_INT_DST_AEQN_MASK				0x3
#define HINIC_MBOX_INT_SRC_RESP_AEQN_MASK			0x3
#define HINIC_MBOX_INT_STAT_DMA_MASK				0x3F
#define HINIC_MBOX_INT_TX_SIZE_MASK				0x1F
#define HINIC_MBOX_INT_STAT_DMA_SO_RO_MASK			0x3
#define HINIC_MBOX_INT_WB_EN_MASK				0x1

#define HINIC_MBOX_INT_SET(val, field)	\
			(((val) & HINIC_MBOX_INT_##field##_MASK) << \
			HINIC_MBOX_INT_##field##_SHIFT)

enum hinic_mbox_tx_status {
	TX_NOT_DONE = 1,
};

#define HINIC_MBOX_CTRL_TRIGGER_AEQE_SHIFT			0
/* specifies the issue request for the message data.
 * 0 - Tx request is done;
 * 1 - Tx request is in process.
 */
#define HINIC_MBOX_CTRL_TX_STATUS_SHIFT				1

#define HINIC_MBOX_CTRL_TRIGGER_AEQE_MASK			0x1
#define HINIC_MBOX_CTRL_TX_STATUS_MASK				0x1

#define HINIC_MBOX_CTRL_SET(val, field)	\
			(((val) & HINIC_MBOX_CTRL_##field##_MASK) << \
			HINIC_MBOX_CTRL_##field##_SHIFT)

#define HINIC_MBOX_HEADER_MSG_LEN_SHIFT				0
#define HINIC_MBOX_HEADER_MODULE_SHIFT				11
#define HINIC_MBOX_HEADER_SEG_LEN_SHIFT				16
#define HINIC_MBOX_HEADER_NO_ACK_SHIFT				22
#define HINIC_MBOX_HEADER_SEQID_SHIFT				24
#define HINIC_MBOX_HEADER_LAST_SHIFT				30
/* specifies the mailbox message direction
 * 0 - send
 * 1 - receive
 */
#define HINIC_MBOX_HEADER_DIRECTION_SHIFT			31
#define HINIC_MBOX_HEADER_CMD_SHIFT				32
#define HINIC_MBOX_HEADER_MSG_ID_SHIFT				40
#define HINIC_MBOX_HEADER_STATUS_SHIFT				48
#define HINIC_MBOX_HEADER_SRC_GLB_FUNC_IDX_SHIFT		54

#define HINIC_MBOX_HEADER_MSG_LEN_MASK				0x7FF
#define HINIC_MBOX_HEADER_MODULE_MASK				0x1F
#define HINIC_MBOX_HEADER_SEG_LEN_MASK				0x3F
#define HINIC_MBOX_HEADER_NO_ACK_MASK				0x1
#define HINIC_MBOX_HEADER_SEQID_MASK				0x3F
#define HINIC_MBOX_HEADER_LAST_MASK				0x1
#define HINIC_MBOX_HEADER_DIRECTION_MASK			0x1
#define HINIC_MBOX_HEADER_CMD_MASK				0xFF
#define HINIC_MBOX_HEADER_MSG_ID_MASK				0xFF
#define HINIC_MBOX_HEADER_STATUS_MASK				0x3F
#define HINIC_MBOX_HEADER_SRC_GLB_FUNC_IDX_MASK			0x3FF

#define HINIC_MBOX_HEADER_GET(val, field)	\
			(((val) >> HINIC_MBOX_HEADER_##field##_SHIFT) & \
			HINIC_MBOX_HEADER_##field##_MASK)
#define HINIC_MBOX_HEADER_SET(val, field)	\
			((u64)((val) & HINIC_MBOX_HEADER_##field##_MASK) << \
			HINIC_MBOX_HEADER_##field##_SHIFT)

#define MBOX_SEGLEN_MASK			\
		HINIC_MBOX_HEADER_SET(HINIC_MBOX_HEADER_SEG_LEN_MASK, SEG_LEN)

#define HINIC_MBOX_SEG_LEN			48
#define HINIC_MBOX_COMP_TIME			8000U
#define MBOX_MSG_POLLING_TIMEOUT		8000

#define HINIC_MBOX_DATA_SIZE			2040

#define MBOX_MAX_BUF_SZ				2048UL
#define MBOX_HEADER_SZ				8

#define MBOX_INFO_SZ				4

/* MBOX size is 64B, 8B for mbox_header, 4B reserved */
#define MBOX_SEG_LEN				48
#define MBOX_SEG_LEN_ALIGN			4
#define MBOX_WB_STATUS_LEN			16UL

/* mbox write back status is 16B, only first 4B is used */
#define MBOX_WB_STATUS_ERRCODE_MASK		0xFFFF
#define MBOX_WB_STATUS_MASK			0xFF
#define MBOX_WB_ERROR_CODE_MASK			0xFF00
#define MBOX_WB_STATUS_FINISHED_SUCCESS		0xFF
#define MBOX_WB_STATUS_FINISHED_WITH_ERR	0xFE
#define MBOX_WB_STATUS_NOT_FINISHED		0x00

#define MBOX_STATUS_FINISHED(wb)	\
	(((wb) & MBOX_WB_STATUS_MASK) != MBOX_WB_STATUS_NOT_FINISHED)
#define MBOX_STATUS_SUCCESS(wb)		\
	(((wb) & MBOX_WB_STATUS_MASK) == MBOX_WB_STATUS_FINISHED_SUCCESS)
#define MBOX_STATUS_ERRCODE(wb)		\
	((wb) & MBOX_WB_ERROR_CODE_MASK)

#define SEQ_ID_START_VAL			0
#define SEQ_ID_MAX_VAL				42

#define DST_AEQ_IDX_DEFAULT_VAL			0
#define SRC_AEQ_IDX_DEFAULT_VAL			0
#define NO_DMA_ATTRIBUTE_VAL			0

#define HINIC_MGMT_RSP_AEQN			0
#define HINIC_MBOX_RSP_AEQN			2
#define HINIC_MBOX_RECV_AEQN			0

#define MBOX_MSG_NO_DATA_LEN			1

#define MBOX_BODY_FROM_HDR(header)	((u8 *)(header) + MBOX_HEADER_SZ)
#define MBOX_AREA(hwif)			\
	((hwif)->cfg_regs_base + HINIC_FUNC_CSR_MAILBOX_DATA_OFF)

#define IS_PF_OR_PPF_SRC(src_func_idx)	((src_func_idx) < HINIC_MAX_PF_FUNCS)

#define MBOX_RESPONSE_ERROR		0x1
#define MBOX_MSG_ID_MASK		0xFF
#define MBOX_MSG_ID(func_to_func)	((func_to_func)->send_msg_id)
#define MBOX_MSG_ID_INC(func_to_func)	(MBOX_MSG_ID(func_to_func) = \
			(MBOX_MSG_ID(func_to_func) + 1) & MBOX_MSG_ID_MASK)

#define FUNC_ID_OFF_SET_8B		8
#define FUNC_ID_OFF_SET_10B		10

/* max message counter wait to process for one function */
#define HINIC_MAX_MSG_CNT_TO_PROCESS	10

enum hinic_hwif_direction_type {
	HINIC_HWIF_DIRECT_SEND	= 0,
	HINIC_HWIF_RESPONSE	= 1,
};

enum mbox_seg_type {
	NOT_LAST_SEG,
	LAST_SEG,
};

enum mbox_ordering_type {
	STRONG_ORDER,
};

enum mbox_write_back_type {
	WRITE_BACK = 1,
};

enum mbox_aeq_trig_type {
	NOT_TRIGGER,
	TRIGGER,
};

struct hinic_set_random_id {
	u8    status;
	u8    version;
	u8    rsvd0[6];

	u8    vf_in_pf;
	u8    rsvd1;
	u16    func_idx;
	u32    random_id;
};

static bool check_func_id(struct hinic_hwdev *hwdev, u16 src_func_idx,
			  const void *buf_in, u16 in_size, u16 offset)
{
	u16 func_idx;

	if (in_size < offset + sizeof(func_idx)) {
		sdk_warn(hwdev->dev_hdl,
			 "Reveice mailbox msg len: %d less than 10 Bytes is invalid\n",
			 in_size);
		return false;
	}

	func_idx = *((u16 *)((u8 *)buf_in + offset));

	if (src_func_idx != func_idx) {
		sdk_warn(hwdev->dev_hdl,
			 "Reveice mailbox function id(0x%x) not equal to msg function id(0x%x)\n",
			 src_func_idx, func_idx);
		return false;
	}

	return true;
}

bool hinic_mbox_check_func_id_8B(struct hinic_hwdev *hwdev, u16 func_idx,
				 void *buf_in, u16 in_size)
{
	return check_func_id(hwdev, func_idx, buf_in, in_size,
			     FUNC_ID_OFF_SET_8B);
}

bool hinic_mbox_check_func_id_10B(struct hinic_hwdev *hwdev, u16 func_idx,
				  void *buf_in, u16 in_size)
{
	return check_func_id(hwdev, func_idx, buf_in, in_size,
			     FUNC_ID_OFF_SET_10B);
}

static int send_mbox_to_func(struct hinic_mbox_func_to_func *func_to_func,
			     enum hinic_mod_type mod, u16 cmd, void *msg,
			     u16 msg_len, u16 dst_func,
			     enum hinic_hwif_direction_type direction,
			     enum hinic_mbox_ack_type ack_type,
			     struct mbox_msg_info *msg_info);

/**
 * hinic_register_ppf_mbox_cb - register mbox callback for ppf
 * @hwdev: the pointer to hw device
 * @mod:	specific mod that the callback will handle
 * @callback:	callback function
 * Return: 0 - success, negative - failure
 */
int hinic_register_ppf_mbox_cb(struct hinic_hwdev *hwdev,
			       enum hinic_mod_type mod,
			       hinic_ppf_mbox_cb callback)
{
	struct hinic_mbox_func_to_func *func_to_func = hwdev->func_to_func;

	if (mod >= HINIC_MOD_MAX)
		return -EFAULT;

	func_to_func->ppf_mbox_cb[mod] = callback;

	set_bit(HINIC_PPF_MBOX_CB_REG, &func_to_func->ppf_mbox_cb_state[mod]);

	return 0;
}

/**
 * hinic_register_pf_mbox_cb - register mbox callback for pf
 * @hwdev: the pointer to hw device
 * @mod:	specific mod that the callback will handle
 * @callback:	callback function
 * Return: 0 - success, negative - failure
 */
int hinic_register_pf_mbox_cb(struct hinic_hwdev *hwdev,
			      enum hinic_mod_type mod,
			      hinic_pf_mbox_cb callback)
{
	struct hinic_mbox_func_to_func *func_to_func = hwdev->func_to_func;

	if (mod >= HINIC_MOD_MAX)
		return -EFAULT;

	func_to_func->pf_mbox_cb[mod] = callback;

	set_bit(HINIC_PF_MBOX_CB_REG, &func_to_func->pf_mbox_cb_state[mod]);

	return 0;
}

/**
 * hinic_register_vf_mbox_cb - register mbox callback for vf
 * @hwdev: the pointer to hw device
 * @mod:	specific mod that the callback will handle
 * @callback:	callback function
 * Return: 0 - success, negative - failure
 */
int hinic_register_vf_mbox_cb(struct hinic_hwdev *hwdev,
			      enum hinic_mod_type mod,
			      hinic_vf_mbox_cb callback)
{
	struct hinic_mbox_func_to_func *func_to_func = hwdev->func_to_func;

	if (mod >= HINIC_MOD_MAX)
		return -EFAULT;

	func_to_func->vf_mbox_cb[mod] = callback;

	set_bit(HINIC_VF_MBOX_CB_REG, &func_to_func->vf_mbox_cb_state[mod]);

	return 0;
}

/**
 * hinic_register_ppf_to_pf_mbox_cb - register mbox callback for pf from ppf
 * @hwdev: the pointer to hw device
 * @mod: specific mod that the callback will handle
 * @callback: callback function
 * Return: 0 - success, negative - failure
 */
int hinic_register_ppf_to_pf_mbox_cb(struct hinic_hwdev *hwdev,
				     enum hinic_mod_type mod,
				     hinic_pf_recv_from_ppf_mbox_cb callback)
{
	struct hinic_mbox_func_to_func *func_to_func = hwdev->func_to_func;

	if (mod >= HINIC_MOD_MAX)
		return -EFAULT;

	func_to_func->pf_recv_from_ppf_mbox_cb[mod] = callback;

	set_bit(HINIC_PPF_TO_PF_MBOX_CB_REG,
		&func_to_func->ppf_to_pf_mbox_cb_state[mod]);

	return 0;
}

/**
 * hinic_unregister_ppf_mbox_cb - unregister the mbox callback for ppf
 * @hwdev: the pointer to hw device
 * @mod: specific mod that the callback will handle
 */
void hinic_unregister_ppf_mbox_cb(struct hinic_hwdev *hwdev,
				  enum hinic_mod_type mod)
{
	struct hinic_mbox_func_to_func *func_to_func = hwdev->func_to_func;

	clear_bit(HINIC_PPF_MBOX_CB_REG, &func_to_func->ppf_mbox_cb_state[mod]);

	while (test_bit(HINIC_PPF_MBOX_CB_RUNNING,
			&func_to_func->ppf_mbox_cb_state[mod]))
		usleep_range(900, 1000);

	func_to_func->ppf_mbox_cb[mod] = NULL;
}

/**
 * hinic_unregister_ppf_mbox_cb - unregister the mbox callback for pf
 * @hwdev: the pointer to hw device
 * @mod: specific mod that the callback will handle
 */
void hinic_unregister_pf_mbox_cb(struct hinic_hwdev *hwdev,
				 enum hinic_mod_type mod)
{
	struct hinic_mbox_func_to_func *func_to_func = hwdev->func_to_func;

	clear_bit(HINIC_PF_MBOX_CB_REG, &func_to_func->pf_mbox_cb_state[mod]);

	while (test_bit(HINIC_PF_MBOX_CB_RUNNING,
			&func_to_func->pf_mbox_cb_state[mod]))
		usleep_range(900, 1000);

	func_to_func->pf_mbox_cb[mod] = NULL;
}

/**
 * hinic_unregister_vf_mbox_cb - unregister the mbox callback for vf
 * @hwdev:the pointer to hw device
 * @mod:specific mod that the callback will handle
 */
void hinic_unregister_vf_mbox_cb(struct hinic_hwdev *hwdev,
				 enum hinic_mod_type mod)
{
	struct hinic_mbox_func_to_func *func_to_func = hwdev->func_to_func;

	clear_bit(HINIC_VF_MBOX_CB_REG, &func_to_func->vf_mbox_cb_state[mod]);

	while (test_bit(HINIC_VF_MBOX_CB_RUNNING,
			&func_to_func->vf_mbox_cb_state[mod]))
		usleep_range(900, 1000);

	func_to_func->vf_mbox_cb[mod] = NULL;
}

/**
 * hinic_unregister_ppf_mbox_cb - unregister the mbox callback for pf from ppf
 * @hwdev: the pointer to hw device
 * @mod: specific mod that the callback will handle
 */
void hinic_unregister_ppf_to_pf_mbox_cb(struct hinic_hwdev *hwdev,
					enum hinic_mod_type mod)
{
	struct hinic_mbox_func_to_func *func_to_func = hwdev->func_to_func;

	clear_bit(HINIC_PPF_TO_PF_MBOX_CB_REG,
		  &func_to_func->ppf_to_pf_mbox_cb_state[mod]);

	while (test_bit(HINIC_PPF_TO_PF_MBOX_CB_RUNNIG,
			&func_to_func->ppf_to_pf_mbox_cb_state[mod]))
		usleep_range(900, 1000);

	func_to_func->pf_recv_from_ppf_mbox_cb[mod] = NULL;
}

int vf_to_pf_handler(void *handle, u16 vf_id, u8 cmd, void *buf_in,
		     u16 in_size, void *buf_out, u16 *out_size)
{
	struct hinic_mbox_func_to_func *func_to_func = handle;

	sdk_warn(func_to_func->hwdev->dev_hdl, "Not support vf command yet/n");
	return -EFAULT;
}

static int recv_vf_mbox_handler(struct hinic_mbox_func_to_func *func_to_func,
				struct hinic_recv_mbox *recv_mbox,
				void *buf_out, u16 *out_size)
{
	hinic_vf_mbox_cb cb;
	int ret;

	if (recv_mbox->mod >= HINIC_MOD_MAX) {
		sdk_warn(func_to_func->hwdev->dev_hdl, "Receive illegal mbox message, mod = %d\n",
			 recv_mbox->mod);
		return -EINVAL;
	}

	set_bit(HINIC_VF_MBOX_CB_RUNNING,
		&func_to_func->vf_mbox_cb_state[recv_mbox->mod]);

	cb = func_to_func->vf_mbox_cb[recv_mbox->mod];
	if (cb && test_bit(HINIC_VF_MBOX_CB_REG,
			   &func_to_func->vf_mbox_cb_state[recv_mbox->mod])) {
		ret = cb(func_to_func->hwdev, recv_mbox->cmd, recv_mbox->mbox,
			 recv_mbox->mbox_len, buf_out, out_size);
	} else {
		sdk_warn(func_to_func->hwdev->dev_hdl, "VF mbox cb is not registered\n");
		ret = -EINVAL;
	}

	clear_bit(HINIC_VF_MBOX_CB_RUNNING,
		  &func_to_func->vf_mbox_cb_state[recv_mbox->mod]);

	return ret;
}

static int
recv_pf_from_ppf_handler(struct hinic_mbox_func_to_func *func_to_func,
			 struct hinic_recv_mbox *recv_mbox,
			 void *buf_out, u16 *out_size)
{
	hinic_pf_recv_from_ppf_mbox_cb	cb;
	enum hinic_mod_type mod = recv_mbox->mod;
	int ret;

	if (mod >= HINIC_MOD_MAX) {
		sdk_warn(func_to_func->hwdev->dev_hdl, "Receive illegal mbox message, mod = %d\n",
			 mod);
		return -EINVAL;
	}

	set_bit(HINIC_PPF_TO_PF_MBOX_CB_RUNNIG,
		&func_to_func->ppf_to_pf_mbox_cb_state[mod]);

	cb = func_to_func->pf_recv_from_ppf_mbox_cb[mod];
	if (cb && test_bit(HINIC_PPF_TO_PF_MBOX_CB_REG,
			   &func_to_func->ppf_to_pf_mbox_cb_state[mod])) {
		ret = cb(func_to_func->hwdev, recv_mbox->cmd,
			 recv_mbox->mbox, recv_mbox->mbox_len,
			 buf_out, out_size);
	} else {
		sdk_warn(func_to_func->hwdev->dev_hdl, "PF recvice ppf mailbox callback is not registered\n");
		ret = -EINVAL;
	}

	clear_bit(HINIC_PPF_TO_PF_MBOX_CB_RUNNIG,
		  &func_to_func->ppf_to_pf_mbox_cb_state[mod]);

	return ret;
}

static int recv_ppf_mbox_handler(struct hinic_mbox_func_to_func *func_to_func,
				 struct hinic_recv_mbox *recv_mbox,
				 u8 pf_id, void *buf_out, u16 *out_size)
{
	hinic_ppf_mbox_cb cb;
	u16 vf_id = 0;
	int ret;

	if (recv_mbox->mod >= HINIC_MOD_MAX) {
		sdk_warn(func_to_func->hwdev->dev_hdl, "Receive illegal mbox message, mod = %d\n",
			 recv_mbox->mod);
		return -EINVAL;
	}

	set_bit(HINIC_PPF_MBOX_CB_RUNNING,
		&func_to_func->ppf_mbox_cb_state[recv_mbox->mod]);

	cb = func_to_func->ppf_mbox_cb[recv_mbox->mod];
	if (cb && test_bit(HINIC_PPF_MBOX_CB_REG,
			   &func_to_func->ppf_mbox_cb_state[recv_mbox->mod])) {
		ret = cb(func_to_func->hwdev, pf_id, vf_id, recv_mbox->cmd,
			 recv_mbox->mbox, recv_mbox->mbox_len,
			 buf_out, out_size);
	} else {
		sdk_warn(func_to_func->hwdev->dev_hdl, "PPF mbox cb is not registered, mod = %d\n",
			 recv_mbox->mod);
		ret = -EINVAL;
	}

	clear_bit(HINIC_PPF_MBOX_CB_RUNNING,
		  &func_to_func->ppf_mbox_cb_state[recv_mbox->mod]);

	return ret;
}

static int
recv_pf_from_vf_mbox_handler(struct hinic_mbox_func_to_func *func_to_func,
			     struct hinic_recv_mbox *recv_mbox,
			     u16 src_func_idx, void *buf_out,
			     u16 *out_size)
{
	hinic_pf_mbox_cb cb;
	u16 vf_id = 0;
	int ret;

	if (recv_mbox->mod >= HINIC_MOD_MAX) {
		sdk_warn(func_to_func->hwdev->dev_hdl, "Receive illegal mbox message, mod = %d\n",
			 recv_mbox->mod);
		return -EINVAL;
	}

	set_bit(HINIC_PF_MBOX_CB_RUNNING,
		&func_to_func->pf_mbox_cb_state[recv_mbox->mod]);

	cb = func_to_func->pf_mbox_cb[recv_mbox->mod];
	if (cb && test_bit(HINIC_PF_MBOX_CB_REG,
			   &func_to_func->pf_mbox_cb_state[recv_mbox->mod])) {
		vf_id = src_func_idx -
			hinic_glb_pf_vf_offset(func_to_func->hwdev);
		ret = cb(func_to_func->hwdev, vf_id, recv_mbox->cmd,
			 recv_mbox->mbox, recv_mbox->mbox_len,
			 buf_out, out_size);
	} else {
		sdk_warn(func_to_func->hwdev->dev_hdl, "PF mbox mod(0x%x) cb is not registered\n",
			 recv_mbox->mod);
		ret = -EINVAL;
	}

	clear_bit(HINIC_PF_MBOX_CB_RUNNING,
		  &func_to_func->pf_mbox_cb_state[recv_mbox->mod]);

	return ret;
}

bool hinic_mbox_check_cmd_valid(struct hinic_hwdev *hwdev,
				struct vf_cmd_check_handle *cmd_handle,
				u16 vf_id, u8 cmd, void *buf_in, u16 in_size,
				u8 size)
{
	u16 src_idx = vf_id + hinic_glb_pf_vf_offset(hwdev);
	int i;

	for (i = 0; i < size; i++) {
		if (cmd == cmd_handle[i].cmd) {
			if (cmd_handle[i].check_cmd)
				return cmd_handle[i].check_cmd(hwdev, src_idx,
							       buf_in, in_size);
			else
				return true;
		}
	}

	return false;
}

static void recv_func_mbox_handler(struct hinic_mbox_func_to_func *func_to_func,
				   struct hinic_recv_mbox *recv_mbox,
				   u16 src_func_idx)
{
	struct hinic_hwdev *dev = func_to_func->hwdev;
	struct mbox_msg_info msg_info = {0};
	u16 out_size = MBOX_MAX_BUF_SZ;
	void *buf_out = recv_mbox->buf_out;
	int err = 0;

	if (HINIC_IS_VF(dev)) {
		err = recv_vf_mbox_handler(func_to_func, recv_mbox, buf_out,
					   &out_size);
	} else { /* pf/ppf process */

		if (IS_PF_OR_PPF_SRC(src_func_idx)) {
			if (HINIC_IS_PPF(dev)) {
				err = recv_ppf_mbox_handler(func_to_func,
							    recv_mbox,
							    (u8)src_func_idx,
							    buf_out, &out_size);
				if (err)
					goto out;
			} else {
				err = recv_pf_from_ppf_handler(func_to_func,
							       recv_mbox,
							       buf_out,
							       &out_size);
				if (err)
					goto out;
			}
		/* The source is neither PF nor PPF, so it is from VF */
		} else {
			err = recv_pf_from_vf_mbox_handler(func_to_func,
							   recv_mbox,
							   src_func_idx,
							   buf_out, &out_size);
		}
	}

out:
	if (recv_mbox->ack_type == MBOX_ACK) {
		msg_info.msg_id = recv_mbox->msg_info.msg_id;
		if (err == HINIC_DEV_BUSY_ACTIVE_FW ||
		    err == HINIC_MBOX_PF_BUSY_ACTIVE_FW)
			msg_info.status = HINIC_MBOX_PF_BUSY_ACTIVE_FW;
		else if (err == HINIC_MBOX_VF_CMD_ERROR)
			msg_info.status = HINIC_MBOX_VF_CMD_ERROR;
		else if (err)
			msg_info.status = HINIC_MBOX_PF_SEND_ERR;

		/* if not data need to response, set out_size to 1 */
		if (!out_size || err)
			out_size = MBOX_MSG_NO_DATA_LEN;

		send_mbox_to_func(func_to_func, recv_mbox->mod, recv_mbox->cmd,
				  buf_out, out_size, src_func_idx,
				  HINIC_HWIF_RESPONSE, MBOX_ACK,
				  &msg_info);
	}

	kfree(recv_mbox->buf_out);
	kfree(recv_mbox->mbox);
	kfree(recv_mbox);
}

static bool check_mbox_seq_id_and_seg_len(struct hinic_recv_mbox *recv_mbox,
					  u8 seq_id, u8 seg_len)
{
	if (seq_id > SEQ_ID_MAX_VAL || seg_len > MBOX_SEG_LEN)
		return false;

	if (seq_id == 0) {
		recv_mbox->seq_id = seq_id;
	} else {
		if (seq_id != recv_mbox->seq_id + 1)
			return false;
		recv_mbox->seq_id = seq_id;
	}

	return true;
}

static void resp_mbox_handler(struct hinic_mbox_func_to_func *func_to_func,
			      struct hinic_recv_mbox *recv_mbox)
{
	spin_lock(&func_to_func->mbox_lock);
	if (recv_mbox->msg_info.msg_id == func_to_func->send_msg_id &&
	    func_to_func->event_flag == EVENT_START)
		complete(&recv_mbox->recv_done);
	else
		sdk_err(func_to_func->hwdev->dev_hdl,
			"Mbox response timeout, current send msg id(0x%x), recv msg id(0x%x), status(0x%x)\n",
			func_to_func->send_msg_id, recv_mbox->msg_info.msg_id,
			recv_mbox->msg_info.status);
	spin_unlock(&func_to_func->mbox_lock);
}

static void recv_func_mbox_work_handler(struct work_struct *work)
{
	struct hinic_mbox_work *mbox_work =
			container_of(work, struct hinic_mbox_work, work);
	struct hinic_recv_mbox *recv_mbox;

	recv_func_mbox_handler(mbox_work->func_to_func, mbox_work->recv_mbox,
			       mbox_work->src_func_idx);

	recv_mbox =
		&mbox_work->func_to_func->mbox_send[mbox_work->src_func_idx];

	atomic_dec(&recv_mbox->msg_cnt);

	destroy_work(&mbox_work->work);

	kfree(mbox_work);
}

static void recv_mbox_handler(struct hinic_mbox_func_to_func *func_to_func,
			      void *header, struct hinic_recv_mbox *recv_mbox)
{
	u64 mbox_header = *((u64 *)header);
	void *mbox_body = MBOX_BODY_FROM_HDR(header);
	struct hinic_recv_mbox *rcv_mbox_temp = NULL;
	u16 src_func_idx;
	struct hinic_mbox_work *mbox_work;
	int pos;
	u8 seq_id, seg_len;

	seq_id = HINIC_MBOX_HEADER_GET(mbox_header, SEQID);
	seg_len = HINIC_MBOX_HEADER_GET(mbox_header, SEG_LEN);
	src_func_idx = HINIC_MBOX_HEADER_GET(mbox_header, SRC_GLB_FUNC_IDX);

	if (!check_mbox_seq_id_and_seg_len(recv_mbox, seq_id, seg_len)) {
		sdk_err(func_to_func->hwdev->dev_hdl,
			"Mailbox sequence and segment check fail, src func id: 0x%x, front id: 0x%x, current id: 0x%x, seg len: 0x%x\n",
			src_func_idx, recv_mbox->seq_id, seq_id, seg_len);
		recv_mbox->seq_id = SEQ_ID_MAX_VAL;
		return;
	}

	pos = seq_id * MBOX_SEG_LEN;
	memcpy((u8 *)recv_mbox->mbox + pos, mbox_body,
	       HINIC_MBOX_HEADER_GET(mbox_header, SEG_LEN));

	if (!HINIC_MBOX_HEADER_GET(mbox_header, LAST))
		return;

	recv_mbox->cmd = HINIC_MBOX_HEADER_GET(mbox_header, CMD);
	recv_mbox->mod = HINIC_MBOX_HEADER_GET(mbox_header, MODULE);
	recv_mbox->mbox_len = HINIC_MBOX_HEADER_GET(mbox_header, MSG_LEN);
	recv_mbox->ack_type = HINIC_MBOX_HEADER_GET(mbox_header, NO_ACK);
	recv_mbox->msg_info.msg_id = HINIC_MBOX_HEADER_GET(mbox_header, MSG_ID);
	recv_mbox->msg_info.status = HINIC_MBOX_HEADER_GET(mbox_header, STATUS);
	recv_mbox->seq_id = SEQ_ID_MAX_VAL;

	if (HINIC_MBOX_HEADER_GET(mbox_header, DIRECTION) ==
	    HINIC_HWIF_RESPONSE) {
		resp_mbox_handler(func_to_func, recv_mbox);
		return;
	}

	if (atomic_read(&recv_mbox->msg_cnt) > HINIC_MAX_MSG_CNT_TO_PROCESS) {
		sdk_warn(func_to_func->hwdev->dev_hdl, "This function(%u) have %d message wait to process, can't add to work queue\n",
			 src_func_idx, atomic_read(&recv_mbox->msg_cnt));
		return;
	}

	rcv_mbox_temp = kzalloc(sizeof(*rcv_mbox_temp), GFP_KERNEL);
	if (!rcv_mbox_temp) {
		sdk_err(func_to_func->hwdev->dev_hdl, "Allocate receive mbox memory failed.\n");
		return;
	}
	memcpy(rcv_mbox_temp, recv_mbox, sizeof(*rcv_mbox_temp));

	rcv_mbox_temp->mbox = kzalloc(MBOX_MAX_BUF_SZ, GFP_KERNEL);
	if (!rcv_mbox_temp->mbox) {
		sdk_err(func_to_func->hwdev->dev_hdl, "Allocate receive mbox message memory failed.\n");
		goto rcv_mbox_msg_err;
	}
	memcpy(rcv_mbox_temp->mbox, recv_mbox->mbox, MBOX_MAX_BUF_SZ);

	rcv_mbox_temp->buf_out = kzalloc(MBOX_MAX_BUF_SZ, GFP_KERNEL);
	if (!rcv_mbox_temp->buf_out) {
		sdk_err(func_to_func->hwdev->dev_hdl, "Allocate receive mbox out buffer memory failed.\n");
		goto rcv_mbox_buf_err;
	}

	mbox_work = kzalloc(sizeof(*mbox_work), GFP_KERNEL);
	if (!mbox_work) {
		sdk_err(func_to_func->hwdev->dev_hdl, "Allocate mbox work memory failed.\n");
		goto mbox_work_err;
	}

	mbox_work->func_to_func = func_to_func;
	mbox_work->recv_mbox = rcv_mbox_temp;

	mbox_work->src_func_idx = src_func_idx;

	atomic_inc(&recv_mbox->msg_cnt);
	INIT_WORK(&mbox_work->work, recv_func_mbox_work_handler);
	queue_work(func_to_func->workq, &mbox_work->work);

	return;

mbox_work_err:
	kfree(rcv_mbox_temp->buf_out);

rcv_mbox_buf_err:
	kfree(rcv_mbox_temp->mbox);

rcv_mbox_msg_err:
	kfree(rcv_mbox_temp);
}

int set_vf_mbox_random_id(struct hinic_hwdev *hwdev, u16 func_id)
{
	struct hinic_mbox_func_to_func *func_to_func = hwdev->func_to_func;
	struct hinic_set_random_id rand_info = {0};
	u16 out_size = sizeof(rand_info);
	int ret;

	rand_info.version = HINIC_CMD_VER_FUNC_ID;
	rand_info.func_idx = func_id;
	rand_info.vf_in_pf = (u8)(func_id - hinic_glb_pf_vf_offset(hwdev));
	get_random_bytes(&rand_info.random_id, sizeof(u32));

	func_to_func->vf_mbx_rand_id[func_id] = rand_info.random_id;

	ret = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_SET_VF_RANDOM_ID,
				     &rand_info, sizeof(rand_info),
				     &rand_info, &out_size, 0);
	if ((rand_info.status != HINIC_MGMT_CMD_UNSUPPORTED &&
	     rand_info.status) || !out_size || ret) {
		sdk_err(hwdev->dev_hdl, "Failed to set vf random id, err: %d, status: 0x%x, out size: 0x%x\n",
			ret, rand_info.status, out_size);
		return -EINVAL;
	}

	if (rand_info.status == HINIC_MGMT_CMD_UNSUPPORTED)
		return rand_info.status;

	func_to_func->vf_mbx_old_rand_id[func_id] =
				func_to_func->vf_mbx_rand_id[func_id];

	return 0;
}

static void update_random_id_work_handler(struct work_struct *work)
{
	struct hinic_mbox_work *mbox_work =
			container_of(work, struct hinic_mbox_work, work);
	struct hinic_mbox_func_to_func *func_to_func = mbox_work->func_to_func;
	u16 src = mbox_work->src_func_idx;
	int err;

	err = set_vf_mbox_random_id(func_to_func->hwdev, src);
	if (err)
		sdk_warn(func_to_func->hwdev->dev_hdl, "Update vf id(0x%x) random id fail\n",
			 mbox_work->src_func_idx);

	destroy_work(&mbox_work->work);

	kfree(mbox_work);
}

bool check_vf_mbox_random_id(struct hinic_mbox_func_to_func *func_to_func,
			     u8 *header)
{
	struct hinic_hwdev *hwdev = func_to_func->hwdev;
	u64 mbox_header = *((u64 *)header);
	struct hinic_mbox_work *mbox_work;
	u32 random_id;
	u16 offset, src = HINIC_MBOX_HEADER_GET(mbox_header, SRC_GLB_FUNC_IDX);
	int vf_in_pf;

	if (IS_PF_OR_PPF_SRC(src) || !func_to_func->support_vf_random)
		return true;

	if (!HINIC_IS_PPF(hwdev)) {
		offset = hinic_glb_pf_vf_offset(hwdev);
		vf_in_pf = src - offset;

		if (vf_in_pf < 1 || vf_in_pf > hinic_func_max_vf(hwdev)) {
			sdk_warn(hwdev->dev_hdl,
				 "Receive vf id(0x%x) is invalid, vf id should be from 0x%x to 0x%x\n",
				 src, (offset + 1),
				 (hinic_func_max_vf(hwdev) + offset));
			return false;
		}
	}

	random_id = be32_to_cpu(*(u32 *)(header + MBOX_SEG_LEN +
					 MBOX_HEADER_SZ));

	if (random_id == func_to_func->vf_mbx_rand_id[src] ||
	    random_id == func_to_func->vf_mbx_old_rand_id[src])
		return true;

	sdk_warn(hwdev->dev_hdl,
		 "Receive func_id(0x%x) mailbox random id(0x%x) mismatch with pf reserve(0x%x)\n",
		 src, random_id, func_to_func->vf_mbx_rand_id[src]);

	mbox_work = kzalloc(sizeof(*mbox_work), GFP_KERNEL);
	if (!mbox_work) {
		sdk_err(func_to_func->hwdev->dev_hdl, "Allocate mbox work memory failed.\n");
		return false;
	}

	mbox_work->func_to_func = func_to_func;
	mbox_work->src_func_idx = src;

	INIT_WORK(&mbox_work->work, update_random_id_work_handler);
	queue_work(func_to_func->workq, &mbox_work->work);

	return false;
}

void hinic_mbox_func_aeqe_handler(void *handle, u8 *header, u8 size)
{
	struct hinic_mbox_func_to_func *func_to_func;
	struct hinic_recv_mbox *recv_mbox;
	u64 mbox_header = *((u64 *)header);
	u64 src, dir;

	func_to_func = ((struct hinic_hwdev *)handle)->func_to_func;

	dir = HINIC_MBOX_HEADER_GET(mbox_header, DIRECTION);
	src = HINIC_MBOX_HEADER_GET(mbox_header, SRC_GLB_FUNC_IDX);

	if (src >= HINIC_MAX_FUNCTIONS) {
		sdk_err(func_to_func->hwdev->dev_hdl,
			"Mailbox source function id:%u is invalid\n", (u32)src);
		return;
	}

	if (!check_vf_mbox_random_id(func_to_func, header))
		return;

	recv_mbox = (dir == HINIC_HWIF_DIRECT_SEND) ?
		    &func_to_func->mbox_send[src] :
		    &func_to_func->mbox_resp[src];

	recv_mbox_handler(func_to_func, (u64 *)header, recv_mbox);
}

void hinic_mbox_self_aeqe_handler(void *handle, u8 *header, u8 size)
{
	struct hinic_mbox_func_to_func *func_to_func;
	struct hinic_send_mbox *send_mbox;

	func_to_func = ((struct hinic_hwdev *)handle)->func_to_func;
	send_mbox = &func_to_func->send_mbox;

	complete(&send_mbox->send_done);
}

static void clear_mbox_status(struct hinic_send_mbox *mbox)
{
	*mbox->wb_status = 0;

	/* clear mailbox write back status */
	wmb();
}


static void mbox_copy_header(struct hinic_hwdev *hwdev,
			     struct hinic_send_mbox *mbox, u64 *header)
{
	u32 *data = (u32 *)header;
	u32 i, idx_max = MBOX_HEADER_SZ / sizeof(u32);

	for (i = 0; i < idx_max; i++) {
		__raw_writel(*(data + i), mbox->data + i * sizeof(u32));
	}
}

static void mbox_copy_send_data(struct hinic_hwdev *hwdev,
				struct hinic_send_mbox *mbox, void *seg,
				u16 seg_len)
{
	u32 *data = seg;
	u32 data_len, chk_sz = sizeof(u32);
	u32 i, idx_max;
	u8 mbox_max_buf[MBOX_SEG_LEN] = {0};

	/* The mbox message should be aligned in 4 bytes. */
	if (seg_len % chk_sz) {
		memcpy(mbox_max_buf, seg, seg_len);
		data = (u32 *)mbox_max_buf;
	}

	data_len = seg_len;
	idx_max = ALIGN(data_len, chk_sz) / chk_sz;

	for (i = 0; i < idx_max; i++) {
		__raw_writel(*(data + i),
			     mbox->data + MBOX_HEADER_SZ + i * sizeof(u32));
	}
}

static void write_mbox_msg_attr(struct hinic_mbox_func_to_func *func_to_func,
				u16 dst_func, u16 dst_aeqn, u16 seg_len,
				int poll)
{
	u32 mbox_int, mbox_ctrl;

	/* msg_len - the total mbox msg len */
	u16 rsp_aeq = (dst_aeqn == 0) ? 0 : HINIC_MBOX_RSP_AEQN;

	mbox_int = HINIC_MBOX_INT_SET(dst_func, DST_FUNC) |
		   HINIC_MBOX_INT_SET(dst_aeqn, DST_AEQN) |
		   HINIC_MBOX_INT_SET(rsp_aeq, SRC_RESP_AEQN) |
		   HINIC_MBOX_INT_SET(NO_DMA_ATTRIBUTE_VAL, STAT_DMA) |
		   HINIC_MBOX_INT_SET(ALIGN(MBOX_SEG_LEN + MBOX_HEADER_SZ +
				      MBOX_INFO_SZ, MBOX_SEG_LEN_ALIGN) >> 2,
				      TX_SIZE) |
		   HINIC_MBOX_INT_SET(STRONG_ORDER, STAT_DMA_SO_RO) |
		   HINIC_MBOX_INT_SET(WRITE_BACK, WB_EN);

	hinic_hwif_write_reg(func_to_func->hwdev->hwif,
			     HINIC_FUNC_CSR_MAILBOX_INT_OFFSET_OFF, mbox_int);

	wmb();	/* writing the mbox int attributes */
	mbox_ctrl = HINIC_MBOX_CTRL_SET(TX_NOT_DONE, TX_STATUS);

	if (poll)
		mbox_ctrl |= HINIC_MBOX_CTRL_SET(NOT_TRIGGER, TRIGGER_AEQE);
	else
		mbox_ctrl |= HINIC_MBOX_CTRL_SET(TRIGGER, TRIGGER_AEQE);

	hinic_hwif_write_reg(func_to_func->hwdev->hwif,
			     HINIC_FUNC_CSR_MAILBOX_CONTROL_OFF, mbox_ctrl);
}

void dump_mox_reg(struct hinic_hwdev *hwdev)
{
	u32 val;

	val = hinic_hwif_read_reg(hwdev->hwif,
				  HINIC_FUNC_CSR_MAILBOX_CONTROL_OFF);
	sdk_err(hwdev->dev_hdl, "Mailbox control reg: 0x%x\n", val);
	val = hinic_hwif_read_reg(hwdev->hwif,
				  HINIC_FUNC_CSR_MAILBOX_INT_OFFSET_OFF);
	sdk_err(hwdev->dev_hdl, "Mailbox interrupt offset: 0x%x\n", val);
}

static u16 get_mbox_status(struct hinic_send_mbox *mbox)
{
	/* write back is 16B, but only use first 4B */
	u64 wb_val = be64_to_cpu(*mbox->wb_status);

	rmb(); /* verify reading before check */

	return (u16)(wb_val & MBOX_WB_STATUS_ERRCODE_MASK);
}

static int send_mbox_seg(struct hinic_mbox_func_to_func *func_to_func,
			 u64 header, u16 dst_func, void *seg, u16 seg_len,
			 int poll, void *msg_info)
{
	struct hinic_send_mbox *send_mbox = &func_to_func->send_mbox;
	struct hinic_hwdev *hwdev = func_to_func->hwdev;
	u8 num_aeqs = hwdev->hwif->attr.num_aeqs;
	u16 dst_aeqn, wb_status = 0, errcode;
	u16 seq_dir = HINIC_MBOX_HEADER_GET(header, DIRECTION);
	struct completion *done = &send_mbox->send_done;
	ulong jif;
	u32 cnt = 0;

	if (num_aeqs >= 4)
		dst_aeqn = (seq_dir == HINIC_HWIF_DIRECT_SEND) ?
			   HINIC_MBOX_RECV_AEQN : HINIC_MBOX_RSP_AEQN;
	else
		dst_aeqn = 0;

	if (!poll)
		init_completion(done);

	clear_mbox_status(send_mbox);

	mbox_copy_header(hwdev, send_mbox, &header);

	mbox_copy_send_data(hwdev, send_mbox, seg, seg_len);

	write_mbox_msg_attr(func_to_func, dst_func, dst_aeqn, seg_len, poll);

	wmb();	/* writing the mbox msg attributes */

	if (poll) {
		while (cnt < MBOX_MSG_POLLING_TIMEOUT) {
			wb_status = get_mbox_status(send_mbox);
			if (MBOX_STATUS_FINISHED(wb_status))
				break;

			usleep_range(900, 1000);
			cnt++;
		}

		if (cnt == MBOX_MSG_POLLING_TIMEOUT) {
			sdk_err(hwdev->dev_hdl, "Send mailbox segment timeout, wb status: 0x%x\n",
				wb_status);
			dump_mox_reg(hwdev);
			return -ETIMEDOUT;
		}
	} else {
		jif = msecs_to_jiffies(HINIC_MBOX_COMP_TIME);
		if (!wait_for_completion_timeout(done, jif)) {
			sdk_err(hwdev->dev_hdl, "Send mailbox segment timeout\n");
			dump_mox_reg(hwdev);
			destory_completion(done);
			return -ETIMEDOUT;
		}
		destory_completion(done);

		wb_status = get_mbox_status(send_mbox);
	}

	if (!MBOX_STATUS_SUCCESS(wb_status)) {
		sdk_err(hwdev->dev_hdl, "Send mailbox segment to function %d error, wb status: 0x%x\n",
			dst_func, wb_status);
		errcode = MBOX_STATUS_ERRCODE(wb_status);
		return errcode ? errcode : -EFAULT;
	}

	return 0;
}

static int send_mbox_to_func(struct hinic_mbox_func_to_func *func_to_func,
			     enum hinic_mod_type mod, u16 cmd, void *msg,
			      u16 msg_len, u16 dst_func,
			      enum hinic_hwif_direction_type direction,
			      enum hinic_mbox_ack_type ack_type,
			      struct mbox_msg_info *msg_info)
{
	struct hinic_hwdev *hwdev = func_to_func->hwdev;
	int err = 0;
	u32 seq_id = 0;
	u16 seg_len = MBOX_SEG_LEN;
	u16 left = msg_len;
	u8 *msg_seg = (u8 *)msg;
	u64 header = 0;

	down(&func_to_func->msg_send_sem);

	header = HINIC_MBOX_HEADER_SET(msg_len, MSG_LEN) |
		 HINIC_MBOX_HEADER_SET(mod, MODULE) |
		 HINIC_MBOX_HEADER_SET(seg_len, SEG_LEN) |
		 HINIC_MBOX_HEADER_SET(ack_type, NO_ACK) |
		 HINIC_MBOX_HEADER_SET(SEQ_ID_START_VAL, SEQID) |
		 HINIC_MBOX_HEADER_SET(NOT_LAST_SEG, LAST) |
		 HINIC_MBOX_HEADER_SET(direction, DIRECTION) |
		 HINIC_MBOX_HEADER_SET(cmd, CMD) |
		 /* The vf's offset to it's associated pf */
		 HINIC_MBOX_HEADER_SET(msg_info->msg_id, MSG_ID) |
		 HINIC_MBOX_HEADER_SET(msg_info->status, STATUS) |
		 HINIC_MBOX_HEADER_SET(hinic_global_func_id_hw(hwdev),
				       SRC_GLB_FUNC_IDX);

	while (!(HINIC_MBOX_HEADER_GET(header, LAST))) {
		if (left <= HINIC_MBOX_SEG_LEN) {
			header &= ~MBOX_SEGLEN_MASK;
			header |= HINIC_MBOX_HEADER_SET(left, SEG_LEN);
			header |= HINIC_MBOX_HEADER_SET(LAST_SEG, LAST);

			seg_len = left;
		}

		err = send_mbox_seg(func_to_func, header, dst_func, msg_seg,
				    seg_len, func_to_func->send_ack_mod,
				    msg_info);
		if (err) {
			sdk_err(hwdev->dev_hdl, "Failed to send mbox seg, seq_id=0x%llx\n",
				HINIC_MBOX_HEADER_GET(header, SEQID));
			goto send_err;
		}

		left -= HINIC_MBOX_SEG_LEN;
		msg_seg += HINIC_MBOX_SEG_LEN;

		seq_id++;
		header &= ~(HINIC_MBOX_HEADER_SET(HINIC_MBOX_HEADER_SEQID_MASK,
						  SEQID));
		header |= HINIC_MBOX_HEADER_SET(seq_id, SEQID);
	}

send_err:
	up(&func_to_func->msg_send_sem);

	return err;
}

static void set_mbox_to_func_event(struct hinic_mbox_func_to_func *func_to_func,
				   enum mbox_event_state event_flag)
{
	spin_lock(&func_to_func->mbox_lock);
	func_to_func->event_flag = event_flag;
	spin_unlock(&func_to_func->mbox_lock);
}

int hinic_mbox_to_func(struct hinic_mbox_func_to_func *func_to_func,
		       enum hinic_mod_type mod, u16 cmd, u16 dst_func,
		       void *buf_in, u16 in_size, void *buf_out,
		       u16 *out_size, u32 timeout)
{
	/* use mbox_resp to hole data which responsed from other function */
	struct hinic_recv_mbox *mbox_for_resp;
	struct mbox_msg_info msg_info = {0};
	ulong timeo;
	int err;

	if (!func_to_func->hwdev->chip_present_flag)
		return -EPERM;

	mbox_for_resp = &func_to_func->mbox_resp[dst_func];

	down(&func_to_func->mbox_send_sem);

	init_completion(&mbox_for_resp->recv_done);

	msg_info.msg_id = MBOX_MSG_ID_INC(func_to_func);

	set_mbox_to_func_event(func_to_func, EVENT_START);

	err = send_mbox_to_func(func_to_func, mod, cmd, buf_in, in_size,
				dst_func, HINIC_HWIF_DIRECT_SEND, MBOX_ACK,
				&msg_info);
	if (err) {
		sdk_err(func_to_func->hwdev->dev_hdl, "Send mailbox failed, msg_id: %d\n",
			msg_info.msg_id);
		set_mbox_to_func_event(func_to_func, EVENT_FAIL);
		goto send_err;
	}


	timeo = msecs_to_jiffies(timeout ? timeout : HINIC_MBOX_COMP_TIME);
	if (!wait_for_completion_timeout(&mbox_for_resp->recv_done, timeo)) {
		set_mbox_to_func_event(func_to_func, EVENT_TIMEOUT);
		sdk_err(func_to_func->hwdev->dev_hdl,
			"Send mbox msg timeout, msg_id: %d\n", msg_info.msg_id);
		err = -ETIMEDOUT;
		goto send_err;
	}

	set_mbox_to_func_event(func_to_func, EVENT_END);

	if (mbox_for_resp->msg_info.status) {
		err = mbox_for_resp->msg_info.status;
		if (err != HINIC_MBOX_PF_BUSY_ACTIVE_FW)
			sdk_err(func_to_func->hwdev->dev_hdl, "Mbox response error(0x%x)\n",
				mbox_for_resp->msg_info.status);
		goto send_err;
	}

	if (buf_out && out_size) {
		if (*out_size < mbox_for_resp->mbox_len) {
			sdk_err(func_to_func->hwdev->dev_hdl,
				"Invalid response mbox message length: %d for mod %d cmd %d, should less than: %d\n",
				mbox_for_resp->mbox_len, mod, cmd, *out_size);
			err = -EFAULT;
			goto send_err;
		}

		if (mbox_for_resp->mbox_len)
			memcpy(buf_out, mbox_for_resp->mbox,
			       mbox_for_resp->mbox_len);

		*out_size = mbox_for_resp->mbox_len;
	}

send_err:
	destory_completion(&mbox_for_resp->recv_done);
	up(&func_to_func->mbox_send_sem);

	return err;
}

static int mbox_func_params_valid(struct hinic_mbox_func_to_func *func_to_func,
				  void *buf_in, u16 in_size)
{
	if (!buf_in || !in_size)
		return -EINVAL;

	if (in_size > HINIC_MBOX_DATA_SIZE) {
		sdk_err(func_to_func->hwdev->dev_hdl,
			"Mbox msg len(%d) exceed limit(%d)\n",
			in_size, HINIC_MBOX_DATA_SIZE);
		return -EINVAL;
	}

	return 0;
}

int hinic_mbox_to_host(struct hinic_hwdev *hwdev, u16 dest_host_ppf_id,
		       enum hinic_mod_type mod, u8 cmd, void *buf_in,
		       u16 in_size, void *buf_out, u16 *out_size, u32 timeout)
{
	struct hinic_mbox_func_to_func *func_to_func = hwdev->func_to_func;
	int err;

	err = mbox_func_params_valid(func_to_func, buf_in, in_size);
	if (err)
		return err;

	if (!HINIC_IS_PPF(hwdev)) {
		sdk_err(hwdev->dev_hdl, "Params error, only ppf can send message to other host, func_type: %d\n",
			hinic_func_type(hwdev));
		return -EINVAL;
	}

	return hinic_mbox_to_func(func_to_func, mod, cmd, dest_host_ppf_id,
				  buf_in, in_size, buf_out, out_size, timeout);
}

int hinic_mbox_to_ppf(struct hinic_hwdev *hwdev,
		      enum hinic_mod_type mod, u8 cmd, void *buf_in,
		      u16 in_size, void *buf_out, u16 *out_size, u32 timeout)
{
	struct hinic_mbox_func_to_func *func_to_func = hwdev->func_to_func;
	int err = mbox_func_params_valid(func_to_func, buf_in, in_size);

	if (err)
		return err;

	if (HINIC_IS_VF(hwdev) || HINIC_IS_PPF(hwdev)) {
		sdk_err(hwdev->dev_hdl, "Params error, func_type: %d\n",
			hinic_func_type(hwdev));
		return -EINVAL;
	}

	return hinic_mbox_to_func(func_to_func, mod, cmd, hinic_ppf_idx(hwdev),
				  buf_in, in_size, buf_out, out_size, timeout);
}

int hinic_mbox_to_pf(struct hinic_hwdev *hwdev,
		     enum hinic_mod_type mod, u8 cmd, void *buf_in,
		     u16 in_size, void *buf_out, u16 *out_size, u32 timeout)
{
	struct hinic_mbox_func_to_func *func_to_func = hwdev->func_to_func;
	int err = mbox_func_params_valid(func_to_func, buf_in, in_size);

	if (err)
		return err;

	if (!HINIC_IS_VF(hwdev)) {
		sdk_err(hwdev->dev_hdl, "Params error, func_type: %d\n",
			hinic_func_type(hwdev));
		return -EINVAL;
	}

	err = hinic_func_own_get(hwdev);
	if (err)
		return err;

	/* port_to_port_idx - imply which PCIE interface PF is connected */
	err = hinic_mbox_to_func(func_to_func, mod, cmd,
				 hinic_pf_id_of_vf_hw(hwdev), buf_in, in_size,
				 buf_out, out_size, timeout);
	hinic_func_own_free(hwdev);
	return err;
}

int hinic_mbox_to_func_no_ack(struct hinic_hwdev *hwdev, u16 func_idx,
			      enum hinic_mod_type mod, u8 cmd, void *buf_in,
			      u16 in_size)
{
	struct mbox_msg_info msg_info = {0};
	int err = mbox_func_params_valid(hwdev->func_to_func, buf_in, in_size);

	if (err)
		return err;

	down(&hwdev->func_to_func->mbox_send_sem);

	err = send_mbox_to_func(hwdev->func_to_func, mod, cmd, buf_in, in_size,
				func_idx, HINIC_HWIF_DIRECT_SEND, MBOX_NO_ACK,
				&msg_info);
	if (err)
		sdk_err(hwdev->dev_hdl, "Send mailbox no ack failed\n");

	up(&hwdev->func_to_func->mbox_send_sem);

	return err;
}

int hinic_mbox_to_pf_no_ack(struct hinic_hwdev *hwdev, enum hinic_mod_type mod,
			    u8 cmd, void *buf_in, u16 in_size)
{
	int err;

	err = hinic_func_own_get(hwdev);
	if (err)
		return err;

	err = hinic_mbox_to_func_no_ack(hwdev, hinic_pf_id_of_vf_hw(hwdev),
					mod, cmd, buf_in, in_size);
	hinic_func_own_free(hwdev);
	return err;
}

int __hinic_mbox_to_vf(void *hwdev,
		       enum hinic_mod_type mod, u16 vf_id, u8 cmd, void *buf_in,
		       u16 in_size, void *buf_out, u16 *out_size, u32 timeout)
{
	struct hinic_mbox_func_to_func *func_to_func;
	int err;
	u16 dst_func_idx;

	if (!hwdev)
		return -EINVAL;

	func_to_func = ((struct hinic_hwdev *)hwdev)->func_to_func;
	err = mbox_func_params_valid(func_to_func, buf_in, in_size);
	if (err)
		return err;

	if (HINIC_IS_VF((struct hinic_hwdev *)hwdev)) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl, "Params error, func_type: %d\n",
			hinic_func_type(hwdev));
		return -EINVAL;
	}

	if (!vf_id) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"VF id(%d) error!\n", vf_id);
		return -EINVAL;
	}

	/* vf_offset_to_pf + vf_id is the vf's global function id of vf in
	 * this pf
	 */
	dst_func_idx = hinic_glb_pf_vf_offset(hwdev) + vf_id;

	return hinic_mbox_to_func(func_to_func, mod, cmd, dst_func_idx, buf_in,
				  in_size, buf_out, out_size, timeout);
}

int hinic_mbox_ppf_to_vf(void *hwdev, enum hinic_mod_type mod,
			 u16 func_id, u8 cmd, void *buf_in,
			 u16 in_size, void *buf_out, u16 *out_size,
			 u32 timeout)
{
	struct hinic_mbox_func_to_func *func_to_func;
	int err;

	if (!hwdev)
		return -EINVAL;

	func_to_func = ((struct hinic_hwdev *)hwdev)->func_to_func;
	err = mbox_func_params_valid(func_to_func, buf_in, in_size);
	if (err)
		return err;

	if (HINIC_IS_VF((struct hinic_hwdev *)hwdev)) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl, "Params error, func_type: %d\n",
			hinic_func_type(hwdev));
		return -EINVAL;
	}

	return hinic_mbox_to_func(func_to_func, mod, cmd, func_id, buf_in,
				  in_size, buf_out, out_size, timeout);
}
EXPORT_SYMBOL(hinic_mbox_ppf_to_vf);

int hinic_mbox_ppf_to_pf(struct hinic_hwdev *hwdev,
			 enum hinic_mod_type mod, u16 dst_pf_id, u8 cmd,
			 void *buf_in, u16 in_size, void *buf_out,
			 u16 *out_size, u32 timeout)
{
	struct hinic_mbox_func_to_func *func_to_func = hwdev->func_to_func;
	int err;

	err = mbox_func_params_valid(func_to_func, buf_in, in_size);
	if (err)
		return err;

	if (!HINIC_IS_PPF(hwdev)) {
		sdk_err(hwdev->dev_hdl, "Params error, func_type: %d\n",
			hinic_func_type(hwdev));
		return -EINVAL;
	}

	if (hinic_ppf_idx(hwdev) == dst_pf_id) {
		sdk_err(hwdev->dev_hdl,
			"Params error, dst_pf_id(0x%x) is ppf\n", dst_pf_id);
		return -EINVAL;
	}

	return hinic_mbox_to_func(func_to_func, mod, cmd, dst_pf_id, buf_in,
				  in_size, buf_out, out_size, timeout);
}

static int init_mbox_info(struct hinic_recv_mbox *mbox_info)
{
	int err;

	mbox_info->seq_id = SEQ_ID_MAX_VAL;

	mbox_info->mbox = kzalloc(MBOX_MAX_BUF_SZ, GFP_KERNEL);
	if (!mbox_info->mbox)
		return -ENOMEM;

	mbox_info->buf_out = kzalloc(MBOX_MAX_BUF_SZ, GFP_KERNEL);
	if (!mbox_info->buf_out) {
		err = -ENOMEM;
		goto alloc_buf_out_err;
	}

	atomic_set(&mbox_info->msg_cnt, 0);

	return 0;

alloc_buf_out_err:
	kfree(mbox_info->mbox);

	return err;
}

static void clean_mbox_info(struct hinic_recv_mbox *mbox_info)
{
	kfree(mbox_info->buf_out);
	kfree(mbox_info->mbox);
}

static int alloc_mbox_info(struct hinic_recv_mbox *mbox_info)
{
	u16 func_idx, i;
	int err;

	for (func_idx = 0; func_idx < HINIC_MAX_FUNCTIONS; func_idx++) {
		err = init_mbox_info(&mbox_info[func_idx]);
		if (err) {
			pr_err("Failed to init mbox info\n");
			goto init_mbox_info_err;
		}
	}

	return 0;

init_mbox_info_err:
	for (i = 0; i < func_idx; i++)
		clean_mbox_info(&mbox_info[i]);

	return err;
}

static void free_mbox_info(struct hinic_recv_mbox *mbox_info)
{
	u16 func_idx;

	for (func_idx = 0; func_idx < HINIC_MAX_FUNCTIONS; func_idx++)
		clean_mbox_info(&mbox_info[func_idx]);
}

static void prepare_send_mbox(struct hinic_mbox_func_to_func *func_to_func)
{
	struct hinic_send_mbox *send_mbox = &func_to_func->send_mbox;

	send_mbox->data = MBOX_AREA(func_to_func->hwdev->hwif);
}

static int alloc_mbox_wb_status(struct hinic_mbox_func_to_func *func_to_func)
{
	struct hinic_send_mbox *send_mbox = &func_to_func->send_mbox;
	struct hinic_hwdev *hwdev = func_to_func->hwdev;
	u32 addr_h, addr_l;

	send_mbox->wb_vaddr = dma_zalloc_coherent(hwdev->dev_hdl,
						  MBOX_WB_STATUS_LEN,
						  &send_mbox->wb_paddr,
						  GFP_KERNEL);
	if (!send_mbox->wb_vaddr)
		return -ENOMEM;

	send_mbox->wb_status = send_mbox->wb_vaddr;

	addr_h = upper_32_bits(send_mbox->wb_paddr);
	addr_l = lower_32_bits(send_mbox->wb_paddr);

	hinic_hwif_write_reg(hwdev->hwif, HINIC_FUNC_CSR_MAILBOX_RESULT_H_OFF,
			     addr_h);
	hinic_hwif_write_reg(hwdev->hwif, HINIC_FUNC_CSR_MAILBOX_RESULT_L_OFF,
			     addr_l);

	return 0;
}

static void free_mbox_wb_status(struct hinic_mbox_func_to_func *func_to_func)
{
	struct hinic_send_mbox *send_mbox = &func_to_func->send_mbox;
	struct hinic_hwdev *hwdev = func_to_func->hwdev;

	hinic_hwif_write_reg(hwdev->hwif, HINIC_FUNC_CSR_MAILBOX_RESULT_H_OFF,
			     0);
	hinic_hwif_write_reg(hwdev->hwif, HINIC_FUNC_CSR_MAILBOX_RESULT_L_OFF,
			     0);

	dma_free_coherent(hwdev->dev_hdl, MBOX_WB_STATUS_LEN,
			  send_mbox->wb_vaddr,
			  send_mbox->wb_paddr);
}

int hinic_vf_mbox_random_id_init(struct hinic_hwdev *hwdev)
{
	u8 vf_in_pf;
	int err = 0;

	if (hinic_func_type(hwdev) == TYPE_VF)
		return 0;

	for (vf_in_pf = 1; vf_in_pf <= hinic_func_max_vf(hwdev); vf_in_pf++) {
		err = set_vf_mbox_random_id(
			hwdev, (hinic_glb_pf_vf_offset(hwdev) + vf_in_pf));
		if (err)
			break;
	}

	if (err == HINIC_MGMT_CMD_UNSUPPORTED) {
		hwdev->func_to_func->support_vf_random = false;
		err = 0;
		sdk_warn(hwdev->dev_hdl, "Mgmt unsupport set vf random id\n");
	} else if (!err) {
		hwdev->func_to_func->support_vf_random = true;
		sdk_info(hwdev->dev_hdl, "PF Set vf random id success\n");
	}

	return err;
}

void hinic_set_mbox_seg_ack_mod(struct hinic_hwdev *hwdev,
				enum hinic_mbox_send_mod mod)
{
	if (!hwdev || !hwdev->func_to_func)
		return;

	hwdev->func_to_func->send_ack_mod = mod;
}

int hinic_func_to_func_init(struct hinic_hwdev *hwdev)
{
	struct hinic_mbox_func_to_func *func_to_func;
	struct card_node *chip_node;
	int err;

	func_to_func = kzalloc(sizeof(*func_to_func), GFP_KERNEL);
	if (!func_to_func)
		return -ENOMEM;

	hwdev->func_to_func = func_to_func;
	func_to_func->hwdev = hwdev;
	chip_node = hwdev->chip_node;
	func_to_func->vf_mbx_rand_id = chip_node->vf_mbx_rand_id;
	func_to_func->vf_mbx_old_rand_id = chip_node->vf_mbx_old_rand_id;
	sema_init(&func_to_func->mbox_send_sem, 1);
	sema_init(&func_to_func->msg_send_sem, 1);
	spin_lock_init(&func_to_func->mbox_lock);
	func_to_func->workq = create_singlethread_workqueue(HINIC_MBOX_WQ_NAME);
	if (!func_to_func->workq) {
		sdk_err(hwdev->dev_hdl, "Failed to initialize MBOX workqueue\n");
		err = -ENOMEM;
		goto create_mbox_workq_err;
	}

	err = alloc_mbox_info(func_to_func->mbox_send);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Alloc mem for mbox_active fail\n");
		goto alloc_mbox_for_send_err;
	}

	err = alloc_mbox_info(func_to_func->mbox_resp);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Alloc mem for mbox_passive fail\n");
		goto alloc_mbox_for_resp_err;
	}

	err = alloc_mbox_wb_status(func_to_func);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to alloc mbox write back status\n");
		goto alloc_wb_status_err;
	}

	prepare_send_mbox(func_to_func);

	func_to_func->send_ack_mod = HINIC_MBOX_SEND_MSG_POLL;

	return 0;

alloc_wb_status_err:
	free_mbox_info(func_to_func->mbox_resp);

alloc_mbox_for_resp_err:
	free_mbox_info(func_to_func->mbox_send);

alloc_mbox_for_send_err:
	destroy_workqueue(func_to_func->workq);

create_mbox_workq_err:
	spin_lock_deinit(&func_to_func->mbox_lock);
	sema_deinit(&func_to_func->msg_send_sem);
	sema_deinit(&func_to_func->mbox_send_sem);
	kfree(func_to_func);

	return err;
}

void hinic_func_to_func_free(struct hinic_hwdev *hwdev)
{
	struct hinic_mbox_func_to_func *func_to_func = hwdev->func_to_func;

	/* destroy workqueue before free related mbox resources in case of
	 * illegal resource access
	 */
	destroy_workqueue(func_to_func->workq);

	free_mbox_wb_status(func_to_func);
	free_mbox_info(func_to_func->mbox_resp);
	free_mbox_info(func_to_func->mbox_send);
	spin_lock_deinit(&func_to_func->mbox_lock);
	sema_deinit(&func_to_func->mbox_send_sem);
	sema_deinit(&func_to_func->msg_send_sem);

	kfree(func_to_func);
}
