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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hwdev.h"
#include "hinic_hwif.h"
#include "hinic_nic_io.h"
#include "hinic_eqs.h"
#include "hinic_wq.h"
#include "hinic_cmdq.h"

#define CMDQ_CMD_TIMEOUT				5000 /* millisecond */

#define UPPER_8_BITS(data)				(((data) >> 8) & 0xFF)
#define LOWER_8_BITS(data)				((data) & 0xFF)

#define CMDQ_DB_INFO_HI_PROD_IDX_SHIFT			0
#define CMDQ_DB_INFO_QUEUE_TYPE_SHIFT			23
#define CMDQ_DB_INFO_CMDQ_TYPE_SHIFT			24
#define CMDQ_DB_INFO_SRC_TYPE_SHIFT			27

#define CMDQ_DB_INFO_HI_PROD_IDX_MASK			0xFFU
#define CMDQ_DB_INFO_QUEUE_TYPE_MASK			0x1U
#define CMDQ_DB_INFO_CMDQ_TYPE_MASK			0x7U
#define CMDQ_DB_INFO_SRC_TYPE_MASK			0x1FU

#define CMDQ_DB_INFO_SET(val, member)			\
				((val & CMDQ_DB_INFO_##member##_MASK) \
				<< CMDQ_DB_INFO_##member##_SHIFT)

#define CMDQ_CTRL_PI_SHIFT				0
#define CMDQ_CTRL_CMD_SHIFT				16
#define CMDQ_CTRL_MOD_SHIFT				24
#define CMDQ_CTRL_ACK_TYPE_SHIFT			29
#define CMDQ_CTRL_HW_BUSY_BIT_SHIFT			31

#define CMDQ_CTRL_PI_MASK				0xFFFFU
#define CMDQ_CTRL_CMD_MASK				0xFFU
#define CMDQ_CTRL_MOD_MASK				0x1FU
#define CMDQ_CTRL_ACK_TYPE_MASK				0x3U
#define CMDQ_CTRL_HW_BUSY_BIT_MASK			0x1U

#define CMDQ_CTRL_SET(val, member)			\
				(((val) & CMDQ_CTRL_##member##_MASK) \
					<< CMDQ_CTRL_##member##_SHIFT)

#define CMDQ_CTRL_GET(val, member)			\
				(((val) >> CMDQ_CTRL_##member##_SHIFT) \
					& CMDQ_CTRL_##member##_MASK)

#define CMDQ_WQE_HEADER_BUFDESC_LEN_SHIFT		0
#define CMDQ_WQE_HEADER_COMPLETE_FMT_SHIFT		15
#define CMDQ_WQE_HEADER_DATA_FMT_SHIFT			22
#define CMDQ_WQE_HEADER_COMPLETE_REQ_SHIFT		23
#define CMDQ_WQE_HEADER_COMPLETE_SECT_LEN_SHIFT		27
#define CMDQ_WQE_HEADER_CTRL_LEN_SHIFT			29
#define CMDQ_WQE_HEADER_HW_BUSY_BIT_SHIFT		31

#define CMDQ_WQE_HEADER_BUFDESC_LEN_MASK		0xFFU
#define CMDQ_WQE_HEADER_COMPLETE_FMT_MASK		0x1U
#define CMDQ_WQE_HEADER_DATA_FMT_MASK			0x1U
#define CMDQ_WQE_HEADER_COMPLETE_REQ_MASK		0x1U
#define CMDQ_WQE_HEADER_COMPLETE_SECT_LEN_MASK		0x3U
#define CMDQ_WQE_HEADER_CTRL_LEN_MASK			0x3U
#define CMDQ_WQE_HEADER_HW_BUSY_BIT_MASK		0x1U

#define CMDQ_WQE_HEADER_SET(val, member)		\
				(((val) & CMDQ_WQE_HEADER_##member##_MASK) \
					<< CMDQ_WQE_HEADER_##member##_SHIFT)

#define CMDQ_WQE_HEADER_GET(val, member)		\
				(((val) >> CMDQ_WQE_HEADER_##member##_SHIFT) \
					& CMDQ_WQE_HEADER_##member##_MASK)

#define CMDQ_CTXT_CURR_WQE_PAGE_PFN_SHIFT 0
#define CMDQ_CTXT_EQ_ID_SHIFT		56
#define CMDQ_CTXT_CEQ_ARM_SHIFT		61
#define CMDQ_CTXT_CEQ_EN_SHIFT		62
#define CMDQ_CTXT_HW_BUSY_BIT_SHIFT	63

#define CMDQ_CTXT_CURR_WQE_PAGE_PFN_MASK 0xFFFFFFFFFFFFF
#define CMDQ_CTXT_EQ_ID_MASK		0x1F
#define CMDQ_CTXT_CEQ_ARM_MASK		0x1
#define CMDQ_CTXT_CEQ_EN_MASK		0x1
#define CMDQ_CTXT_HW_BUSY_BIT_MASK	0x1

#define CMDQ_CTXT_PAGE_INFO_SET(val, member)		\
				(((u64)(val) & CMDQ_CTXT_##member##_MASK) \
				<< CMDQ_CTXT_##member##_SHIFT)

#define CMDQ_CTXT_PAGE_INFO_GET(val, member)		\
				(((u64)(val) >> CMDQ_CTXT_##member##_SHIFT) \
				 & CMDQ_CTXT_##member##_MASK)

#define CMDQ_CTXT_WQ_BLOCK_PFN_SHIFT	0
#define CMDQ_CTXT_CI_SHIFT		52

#define CMDQ_CTXT_WQ_BLOCK_PFN_MASK	0xFFFFFFFFFFFFF
#define CMDQ_CTXT_CI_MASK		0xFFF

#define CMDQ_CTXT_BLOCK_INFO_SET(val, member)		\
				(((u64)(val) & CMDQ_CTXT_##member##_MASK) \
				<< CMDQ_CTXT_##member##_SHIFT)

#define CMDQ_CTXT_BLOCK_INFO_GET(val, member)		\
				(((u64)(val) >> CMDQ_CTXT_##member##_SHIFT) \
				 & CMDQ_CTXT_##member##_MASK)

#define SAVED_DATA_ARM_SHIFT		31

#define SAVED_DATA_ARM_MASK		0x1U

#define SAVED_DATA_SET(val, member)	\
				(((val) & SAVED_DATA_##member##_MASK) \
				<< SAVED_DATA_##member##_SHIFT)

#define SAVED_DATA_CLEAR(val, member)	\
				((val) & (~(SAVED_DATA_##member##_MASK \
				<< SAVED_DATA_##member##_SHIFT)))

#define WQE_ERRCODE_VAL_SHIFT		20

#define WQE_ERRCODE_VAL_MASK		0xF

#define WQE_ERRCODE_GET(val, member)	\
		(((val) >> WQE_ERRCODE_##member##_SHIFT) & \
		WQE_ERRCODE_##member##_MASK)

#define CEQE_CMDQ_TYPE_SHIFT		0

#define CEQE_CMDQ_TYPE_MASK		0x7

#define CEQE_CMDQ_GET(val, member)	\
	(((val) >> CEQE_CMDQ_##member##_SHIFT) & CEQE_CMDQ_##member##_MASK)

#define WQE_COMPLETED(ctrl_info)	CMDQ_CTRL_GET(ctrl_info, HW_BUSY_BIT)

#define WQE_HEADER(wqe)			((struct hinic_cmdq_header *)(wqe))

#define CMDQ_DB_PI_OFF(pi)		(((u16)LOWER_8_BITS(pi)) << 3)

#define CMDQ_DB_ADDR(db_base, pi)	\
		(((u8 *)db_base + HINIC_DB_OFF) + CMDQ_DB_PI_OFF(pi))

#define CMDQ_PFN_SHIFT			12
#define CMDQ_PFN(addr)			((addr) >> CMDQ_PFN_SHIFT)

#define FIRST_DATA_TO_WRITE_LAST	sizeof(u64)

#define WQE_LCMD_SIZE		64
#define WQE_SCMD_SIZE		64

#define COMPLETE_LEN		3

#define CMDQ_WQEBB_SIZE	        64
#define CMDQ_WQE_SIZE		64

#define CMDQ_WQ_PAGE_SIZE	4096

#define WQE_NUM_WQEBBS(wqe_size, wq)	\
	 ((u16)(ALIGN((u32)(wqe_size), (wq)->wqebb_size) / (wq)->wqebb_size))

#define cmdq_to_cmdqs(cmdq)	container_of((cmdq) - (cmdq)->cmdq_type, \
				struct hinic_cmdqs, cmdq[0])

#define CMDQ_SEND_CMPT_CODE		10
#define CMDQ_COMPLETE_CMPT_CODE		11

#define HINIC_GET_CMDQ_FREE_WQEBBS(cmdq_wq)	\
				atomic_read(&cmdq_wq->delta)

enum cmdq_scmd_type {
	CMDQ_SET_ARM_CMD = 2,
};

enum cmdq_wqe_type {
	WQE_LCMD_TYPE,
	WQE_SCMD_TYPE,
};

enum ctrl_sect_len {
	CTRL_SECT_LEN = 1,
	CTRL_DIRECT_SECT_LEN = 2,
};

enum bufdesc_len {
	BUFDESC_LCMD_LEN = 2,
	BUFDESC_SCMD_LEN = 3,
};

enum data_format {
	DATA_SGE,
	DATA_DIRECT,
};

enum completion_format {
	COMPLETE_DIRECT,
	COMPLETE_SGE,
};

enum completion_request {
	CEQ_SET = 1,
};

enum cmdq_cmd_type {
	SYNC_CMD_DIRECT_RESP,
	SYNC_CMD_SGE_RESP,
	ASYNC_CMD,
};

bool hinic_cmdq_idle(struct hinic_cmdq *cmdq)
{
	struct hinic_wq *wq = cmdq->wq;

	return (atomic_read(&wq->delta) == wq->q_depth ? true : false);
}

struct hinic_cmd_buf *hinic_alloc_cmd_buf(void *hwdev)
{
	struct hinic_cmdqs *cmdqs;
	struct hinic_cmd_buf *cmd_buf;
	void *dev;

	if (!hwdev) {
		pr_err("Failed to alloc cmd buf, Invalid hwdev\n");
		return NULL;
	}

	cmdqs = ((struct hinic_hwdev *)hwdev)->cmdqs;
	dev = ((struct hinic_hwdev *)hwdev)->dev_hdl;

	cmd_buf = kzalloc(sizeof(*cmd_buf), GFP_ATOMIC);
	if (!cmd_buf)
		return NULL;

	cmd_buf->buf = pci_pool_alloc(cmdqs->cmd_buf_pool, GFP_ATOMIC,
				      &cmd_buf->dma_addr);
	if (!cmd_buf->buf) {
		sdk_err(dev, "Failed to allocate cmdq cmd buf from the pool\n");
		goto alloc_pci_buf_err;
	}

	return cmd_buf;

alloc_pci_buf_err:
	kfree(cmd_buf);
	return NULL;
}
EXPORT_SYMBOL(hinic_alloc_cmd_buf);

void hinic_free_cmd_buf(void *hwdev, struct hinic_cmd_buf *cmd_buf)
{
	struct hinic_cmdqs *cmdqs;

	if (!hwdev || !cmd_buf) {
		pr_err("Failed to free cmd buf\n");
		return;
	}

	cmdqs = ((struct hinic_hwdev *)hwdev)->cmdqs;

	pci_pool_free(cmdqs->cmd_buf_pool, cmd_buf->buf, cmd_buf->dma_addr);
	kfree(cmd_buf);
}
EXPORT_SYMBOL(hinic_free_cmd_buf);

static int cmdq_wqe_size(enum cmdq_wqe_type wqe_type)
{
	int wqe_size = 0;

	switch (wqe_type) {
	case WQE_LCMD_TYPE:
		wqe_size = WQE_LCMD_SIZE;
		break;
	case WQE_SCMD_TYPE:
		wqe_size = WQE_SCMD_SIZE;
		break;
	}

	return wqe_size;
}

static int cmdq_get_wqe_size(enum bufdesc_len len)
{
	int wqe_size = 0;

	switch (len) {
	case BUFDESC_LCMD_LEN:
		wqe_size = WQE_LCMD_SIZE;
		break;
	case BUFDESC_SCMD_LEN:
		wqe_size = WQE_SCMD_SIZE;
		break;
	}

	return wqe_size;
}

static void cmdq_set_completion(struct hinic_cmdq_completion *complete,
				struct hinic_cmd_buf *buf_out)
{
	struct hinic_sge_resp *sge_resp = &complete->sge_resp;

	hinic_set_sge(&sge_resp->sge, buf_out->dma_addr,
		      HINIC_CMDQ_BUF_SIZE);
}

static void cmdq_set_lcmd_bufdesc(struct hinic_cmdq_wqe_lcmd *wqe,
				  struct hinic_cmd_buf *buf_in)
{
	hinic_set_sge(&wqe->buf_desc.sge, buf_in->dma_addr, buf_in->size);
}

static void cmdq_set_inline_wqe_data(struct hinic_cmdq_inline_wqe *wqe,
				     const void *buf_in, u32 in_size)
{
	struct hinic_cmdq_wqe_scmd *wqe_scmd = &wqe->wqe_scmd;

	wqe_scmd->buf_desc.buf_len = in_size;
	memcpy(wqe_scmd->buf_desc.data, buf_in, in_size);
}

static void cmdq_fill_db(struct hinic_cmdq_db *db,
			 enum hinic_cmdq_type cmdq_type, u16 prod_idx)
{
	db->db_info = CMDQ_DB_INFO_SET(UPPER_8_BITS(prod_idx), HI_PROD_IDX) |
			CMDQ_DB_INFO_SET(HINIC_DB_CMDQ_TYPE, QUEUE_TYPE) |
			CMDQ_DB_INFO_SET(cmdq_type, CMDQ_TYPE)		|
			CMDQ_DB_INFO_SET(HINIC_DB_SRC_CMDQ_TYPE, SRC_TYPE);
}

static void cmdq_set_db(struct hinic_cmdq *cmdq,
			enum hinic_cmdq_type cmdq_type, u16 prod_idx)
{
	struct hinic_cmdq_db db;

	cmdq_fill_db(&db, cmdq_type, prod_idx);

	/* The data that is written to HW should be in Big Endian Format */
	db.db_info = cpu_to_be32(db.db_info);

	wmb();	/* write all before the doorbell */
	writel(db.db_info, CMDQ_DB_ADDR(cmdq->db_base, prod_idx));
}

static void cmdq_wqe_fill(void *dst, const void *src)
{
	memcpy((u8 *)dst + FIRST_DATA_TO_WRITE_LAST,
	       (u8 *)src + FIRST_DATA_TO_WRITE_LAST,
	       CMDQ_WQE_SIZE - FIRST_DATA_TO_WRITE_LAST);

	wmb();	/* The first 8 bytes should be written last */

	*(u64 *)dst = *(u64 *)src;
}

static void cmdq_prepare_wqe_ctrl(struct hinic_cmdq_wqe *wqe, int wrapped,
				  enum hinic_ack_type ack_type,
			      enum hinic_mod_type mod, u8 cmd, u16 prod_idx,
			      enum completion_format complete_format,
			      enum data_format data_format,
			      enum bufdesc_len buf_len)
{
	struct hinic_ctrl *ctrl;
	enum ctrl_sect_len ctrl_len;
	struct hinic_cmdq_wqe_lcmd *wqe_lcmd;
	struct hinic_cmdq_wqe_scmd *wqe_scmd;
	u32 saved_data = WQE_HEADER(wqe)->saved_data;

	if (data_format == DATA_SGE) {
		wqe_lcmd = &wqe->wqe_lcmd;

		wqe_lcmd->status.status_info = 0;
		ctrl = &wqe_lcmd->ctrl;
		ctrl_len = CTRL_SECT_LEN;
	} else {
		wqe_scmd = &wqe->inline_wqe.wqe_scmd;

		wqe_scmd->status.status_info = 0;
		ctrl = &wqe_scmd->ctrl;
		ctrl_len = CTRL_DIRECT_SECT_LEN;
	}

	ctrl->ctrl_info = CMDQ_CTRL_SET(prod_idx, PI)		|
			CMDQ_CTRL_SET(cmd, CMD)			|
			CMDQ_CTRL_SET(mod, MOD)			|
			CMDQ_CTRL_SET(ack_type, ACK_TYPE);

	WQE_HEADER(wqe)->header_info =
		CMDQ_WQE_HEADER_SET(buf_len, BUFDESC_LEN) |
		CMDQ_WQE_HEADER_SET(complete_format, COMPLETE_FMT) |
		CMDQ_WQE_HEADER_SET(data_format, DATA_FMT)	|
		CMDQ_WQE_HEADER_SET(CEQ_SET, COMPLETE_REQ)	|
		CMDQ_WQE_HEADER_SET(COMPLETE_LEN, COMPLETE_SECT_LEN) |
		CMDQ_WQE_HEADER_SET(ctrl_len, CTRL_LEN)	|
		CMDQ_WQE_HEADER_SET((u32)wrapped, HW_BUSY_BIT);

	if (cmd == CMDQ_SET_ARM_CMD && mod == HINIC_MOD_COMM) {
		saved_data &= SAVED_DATA_CLEAR(saved_data, ARM);
		WQE_HEADER(wqe)->saved_data = saved_data |
						SAVED_DATA_SET(1, ARM);
	} else {
		saved_data &= SAVED_DATA_CLEAR(saved_data, ARM);
		WQE_HEADER(wqe)->saved_data = saved_data;
	}
}

static void cmdq_set_lcmd_wqe(struct hinic_cmdq_wqe *wqe,
			      enum cmdq_cmd_type cmd_type,
				struct hinic_cmd_buf *buf_in,
				struct hinic_cmd_buf *buf_out, int wrapped,
				enum hinic_ack_type ack_type,
				enum hinic_mod_type mod, u8 cmd, u16 prod_idx)
{
	struct hinic_cmdq_wqe_lcmd *wqe_lcmd = &wqe->wqe_lcmd;
	enum completion_format complete_format = COMPLETE_DIRECT;

	switch (cmd_type) {
	case SYNC_CMD_SGE_RESP:
		if (buf_out) {
			complete_format = COMPLETE_SGE;
			cmdq_set_completion(&wqe_lcmd->completion, buf_out);
		}
		break;
	case SYNC_CMD_DIRECT_RESP:
		complete_format = COMPLETE_DIRECT;
		wqe_lcmd->completion.direct_resp = 0;
		break;
	case ASYNC_CMD:
		complete_format = COMPLETE_DIRECT;
		wqe_lcmd->completion.direct_resp = 0;

		wqe_lcmd->buf_desc.saved_async_buf = (u64)(buf_in);
		break;
	}

	cmdq_prepare_wqe_ctrl(wqe, wrapped, ack_type, mod, cmd,
			      prod_idx, complete_format, DATA_SGE,
			BUFDESC_LCMD_LEN);

	cmdq_set_lcmd_bufdesc(wqe_lcmd, buf_in);
}

static void cmdq_set_inline_wqe(struct hinic_cmdq_wqe *wqe,
				enum cmdq_cmd_type cmd_type,
				void *buf_in, u16 in_size,
				struct hinic_cmd_buf *buf_out, int wrapped,
				enum hinic_ack_type ack_type,
				enum hinic_mod_type mod, u8 cmd, u16 prod_idx)
{
	struct hinic_cmdq_wqe_scmd *wqe_scmd = &wqe->inline_wqe.wqe_scmd;
	enum completion_format complete_format = COMPLETE_DIRECT;

	switch (cmd_type) {
	case SYNC_CMD_SGE_RESP:
		complete_format = COMPLETE_SGE;
		cmdq_set_completion(&wqe_scmd->completion, buf_out);
		break;
	case SYNC_CMD_DIRECT_RESP:
		complete_format = COMPLETE_DIRECT;
		wqe_scmd->completion.direct_resp = 0;
		break;
	default:
		break;
	}

	cmdq_prepare_wqe_ctrl(wqe, wrapped, ack_type, mod, cmd, prod_idx,
			      complete_format, DATA_DIRECT, BUFDESC_SCMD_LEN);

	cmdq_set_inline_wqe_data(&wqe->inline_wqe, buf_in, in_size);
}

static void cmdq_update_cmd_status(struct hinic_cmdq *cmdq, u16 prod_idx,
				   struct hinic_cmdq_wqe *wqe)
{
	struct hinic_cmdq_cmd_info *cmd_info;
	struct hinic_cmdq_wqe_lcmd *wqe_lcmd;
	u32 status_info;

	wqe_lcmd = &wqe->wqe_lcmd;
	cmd_info = &cmdq->cmd_infos[prod_idx];

	if (cmd_info->errcode) {
		status_info = be32_to_cpu(wqe_lcmd->status.status_info);
		*cmd_info->errcode = WQE_ERRCODE_GET(status_info, VAL);
	}

	if (cmd_info->direct_resp &&
	    cmd_info->cmd_type == HINIC_CMD_TYPE_DIRECT_RESP)
		*cmd_info->direct_resp =
			cpu_to_be64(wqe_lcmd->completion.direct_resp);
}

static int hinic_cmdq_sync_timeout_check(struct hinic_cmdq *cmdq,
					 struct hinic_cmdq_wqe *wqe, u16 pi,
					 enum hinic_mod_type mod, u8 cmd)
{
	struct hinic_cmdq_wqe_lcmd *wqe_lcmd;
	struct hinic_ctrl *ctrl;
	u32 ctrl_info;

	wqe_lcmd = &wqe->wqe_lcmd;
	ctrl = &wqe_lcmd->ctrl;
	ctrl_info = be32_to_cpu((ctrl)->ctrl_info);
	if (!WQE_COMPLETED(ctrl_info)) {
		sdk_info(cmdq->hwdev->dev_hdl, "Cmdq sync command check busy bit not set, mod: %u, cmd: 0x%x\n",
			 mod, cmd);
		return -EFAULT;
	}

	cmdq_update_cmd_status(cmdq, pi, wqe);

	sdk_info(cmdq->hwdev->dev_hdl, "Cmdq sync command check succeed, mod: %u, cmd: 0x%x\n",
		 mod, cmd);
	return 0;
}

static void __clear_cmd_info(struct hinic_cmdq_cmd_info *cmd_info,
			     const int *errcode, struct completion *done,
			     u64 *out_param)
{
	if (cmd_info->errcode == errcode)
		cmd_info->errcode = NULL;

	if (cmd_info->done == done)
		cmd_info->done = NULL;

	if (cmd_info->direct_resp == out_param)
		cmd_info->direct_resp = NULL;
}


static int cmdq_sync_cmd_direct_resp(struct hinic_cmdq *cmdq,
				     enum hinic_ack_type ack_type,
				     enum hinic_mod_type mod, u8 cmd,
				     struct hinic_cmd_buf *buf_in,
				     u64 *out_param, u32 timeout)
{
	struct hinic_wq *wq = cmdq->wq;
	struct hinic_cmdq_wqe *curr_wqe, wqe;
	struct hinic_cmdq_cmd_info *cmd_info;
	struct completion done;
	u16 curr_prod_idx, next_prod_idx, num_wqebbs;
	int wrapped, errcode = 0, wqe_size = cmdq_wqe_size(WQE_LCMD_TYPE);
	int cmpt_code = CMDQ_SEND_CMPT_CODE;
	ulong timeo;
	u64 curr_msg_id;
	int err;

	num_wqebbs = WQE_NUM_WQEBBS(wqe_size, wq);

	/* Keep wrapped and doorbell index correct. bh - for tasklet(ceq) */
	spin_lock_bh(&cmdq->cmdq_lock);

	/* in order to save a wqebb for setting arm_bit when
	 * send cmdq commands frequently resulting in cmdq full
	 */
	if (HINIC_GET_CMDQ_FREE_WQEBBS(wq) < num_wqebbs + 1) {
		spin_unlock_bh(&cmdq->cmdq_lock);
		return -EBUSY;
	}

	/* WQE_SIZE = WQEBB_SIZE, we will get the wq element and not shadow */
	curr_wqe = hinic_get_wqe(cmdq->wq, num_wqebbs, &curr_prod_idx);
	if (!curr_wqe) {
		spin_unlock_bh(&cmdq->cmdq_lock);
		sdk_err(cmdq->hwdev->dev_hdl, "Can not get avalible wqebb, mod: %u, cmd: 0x%x\n",
			mod, cmd);
		return -EBUSY;
	}

	memset(&wqe, 0, sizeof(wqe));

	wrapped = cmdq->wrapped;

	next_prod_idx = curr_prod_idx + num_wqebbs;
	if (next_prod_idx >= wq->q_depth) {
		cmdq->wrapped = !cmdq->wrapped;
		next_prod_idx -= wq->q_depth;
	}

	cmd_info = &cmdq->cmd_infos[curr_prod_idx];

	init_completion(&done);

	cmd_info->done = &done;
	cmd_info->errcode = &errcode;
	cmd_info->direct_resp = out_param;
	cmd_info->cmpt_code = &cmpt_code;

	cmdq_set_lcmd_wqe(&wqe, SYNC_CMD_DIRECT_RESP, buf_in, NULL,
			  wrapped, ack_type, mod, cmd, curr_prod_idx);

	/* The data that is written to HW should be in Big Endian Format */
	hinic_cpu_to_be32(&wqe, wqe_size);

	/* CMDQ WQE is not shadow, therefore wqe will be written to wq */
	cmdq_wqe_fill(curr_wqe, &wqe);

	cmd_info->cmd_type = HINIC_CMD_TYPE_DIRECT_RESP;

	(cmd_info->cmdq_msg_id)++;
	curr_msg_id = cmd_info->cmdq_msg_id;

	cmdq_set_db(cmdq, HINIC_CMDQ_SYNC, next_prod_idx);

	spin_unlock_bh(&cmdq->cmdq_lock);

	timeo = msecs_to_jiffies(timeout ? timeout : CMDQ_CMD_TIMEOUT);
	if (!wait_for_completion_timeout(&done, timeo)) {
		spin_lock_bh(&cmdq->cmdq_lock);

		if (cmd_info->cmpt_code == &cmpt_code)
			cmd_info->cmpt_code = NULL;

		if (cmpt_code == CMDQ_COMPLETE_CMPT_CODE) {
			sdk_info(cmdq->hwdev->dev_hdl, "Cmdq direct sync command has been completed\n");
			spin_unlock_bh(&cmdq->cmdq_lock);
			goto timeout_check_ok;
		}

		if (curr_msg_id == cmd_info->cmdq_msg_id) {
			err = hinic_cmdq_sync_timeout_check(cmdq, curr_wqe,
							    curr_prod_idx,
							    mod, cmd);
			if (err)
				cmd_info->cmd_type = HINIC_CMD_TYPE_TIMEOUT;
			else
				cmd_info->cmd_type =
						HINIC_CMD_TYPE_FAKE_TIMEOUT;
		} else {
			err = -ETIMEDOUT;
			sdk_err(cmdq->hwdev->dev_hdl,
				"Cmdq sync command current msg id dismatch with cmd_info msg id, mod: %u, cmd: 0x%x\n",
				mod, cmd);
		}

		__clear_cmd_info(cmd_info, &errcode, &done, out_param);

		spin_unlock_bh(&cmdq->cmdq_lock);

		if (!err)
			goto timeout_check_ok;

		sdk_err(cmdq->hwdev->dev_hdl, "Cmdq sync command timeout, prod idx: 0x%x\n",
			curr_prod_idx);
		hinic_dump_ceq_info(cmdq->hwdev);
		destory_completion(&done);
		return -ETIMEDOUT;
	}

timeout_check_ok:
	destory_completion(&done);
	smp_rmb();	/* read error code after completion */

	if (errcode > 1)
		return errcode;

	return 0;
}

static int cmdq_sync_cmd_detail_resp(struct hinic_cmdq *cmdq,
				     enum hinic_ack_type ack_type,
				enum hinic_mod_type mod, u8 cmd,
				struct hinic_cmd_buf *buf_in,
				struct hinic_cmd_buf *buf_out,
				u32 timeout)
{
	struct hinic_wq *wq = cmdq->wq;
	struct hinic_cmdq_wqe *curr_wqe, wqe;
	struct hinic_cmdq_cmd_info *cmd_info;
	struct completion done;
	u16 curr_prod_idx, next_prod_idx, num_wqebbs;
	int wrapped, errcode = 0, wqe_size = cmdq_wqe_size(WQE_LCMD_TYPE);
	int cmpt_code = CMDQ_SEND_CMPT_CODE;
	ulong timeo;
	u64 curr_msg_id;
	int err;

	num_wqebbs = WQE_NUM_WQEBBS(wqe_size, wq);

	/* Keep wrapped and doorbell index correct. bh - for tasklet(ceq) */
	spin_lock_bh(&cmdq->cmdq_lock);

	/* in order to save a wqebb for setting arm_bit when
	 * send cmdq commands frequently resulting in cmdq full
	 */
	if (HINIC_GET_CMDQ_FREE_WQEBBS(wq) < num_wqebbs + 1) {
		spin_unlock_bh(&cmdq->cmdq_lock);
		return -EBUSY;
	}

	/* WQE_SIZE = WQEBB_SIZE, we will get the wq element and not shadow */
	curr_wqe = hinic_get_wqe(cmdq->wq, num_wqebbs, &curr_prod_idx);
	if (!curr_wqe) {
		spin_unlock_bh(&cmdq->cmdq_lock);
		sdk_err(cmdq->hwdev->dev_hdl, "Can not get avalible wqebb, mod: %u, cmd: 0x%x\n",
			mod, cmd);
		return -EBUSY;
	}

	memset(&wqe, 0, sizeof(wqe));

	wrapped = cmdq->wrapped;

	next_prod_idx = curr_prod_idx + num_wqebbs;
	if (next_prod_idx >= wq->q_depth) {
		cmdq->wrapped = !cmdq->wrapped;
		next_prod_idx -= wq->q_depth;
	}

	cmd_info = &cmdq->cmd_infos[curr_prod_idx];

	init_completion(&done);

	cmd_info->done = &done;
	cmd_info->errcode = &errcode;
	cmd_info->cmpt_code = &cmpt_code;

	cmdq_set_lcmd_wqe(&wqe, SYNC_CMD_SGE_RESP, buf_in, buf_out,
			  wrapped, ack_type, mod, cmd, curr_prod_idx);

	hinic_cpu_to_be32(&wqe, wqe_size);

	cmdq_wqe_fill(curr_wqe, &wqe);

	cmd_info->cmd_type = HINIC_CMD_TYPE_SGE_RESP;

	(cmd_info->cmdq_msg_id)++;
	curr_msg_id = cmd_info->cmdq_msg_id;

	cmdq_set_db(cmdq, HINIC_CMDQ_SYNC, next_prod_idx);

	spin_unlock_bh(&cmdq->cmdq_lock);

	timeo = msecs_to_jiffies(timeout ? timeout : CMDQ_CMD_TIMEOUT);
	if (!wait_for_completion_timeout(&done, timeo)) {
		spin_lock_bh(&cmdq->cmdq_lock);

		if (cmd_info->cmpt_code == &cmpt_code)
			cmd_info->cmpt_code = NULL;

		if (cmpt_code == CMDQ_COMPLETE_CMPT_CODE) {
			sdk_info(cmdq->hwdev->dev_hdl, "Cmdq detail sync command has been completed\n");
			spin_unlock_bh(&cmdq->cmdq_lock);
			goto timeout_check_ok;
		}

		if (curr_msg_id == cmd_info->cmdq_msg_id) {
			err = hinic_cmdq_sync_timeout_check(cmdq, curr_wqe,
							    curr_prod_idx,
							    mod, cmd);
			if (err)
				cmd_info->cmd_type = HINIC_CMD_TYPE_TIMEOUT;
			else
				cmd_info->cmd_type =
						HINIC_CMD_TYPE_FAKE_TIMEOUT;
		} else {
			err = -ETIMEDOUT;
			sdk_err(cmdq->hwdev->dev_hdl,
				"Cmdq sync command current msg id dismatch with cmd_info msg id, mod: %u, cmd: 0x%x\n",
				mod, cmd);
		}

		if (cmd_info->errcode == &errcode)
			cmd_info->errcode = NULL;

		if (cmd_info->done == &done)
			cmd_info->done = NULL;

		spin_unlock_bh(&cmdq->cmdq_lock);

		if (!err)
			goto timeout_check_ok;

		sdk_err(cmdq->hwdev->dev_hdl, "Cmdq sync command timeout, prod idx: 0x%x\n",
			curr_prod_idx);
		hinic_dump_ceq_info(cmdq->hwdev);
		destory_completion(&done);
		return -ETIMEDOUT;
	}

timeout_check_ok:
	destory_completion(&done);
	smp_rmb();	/* read error code after completion */

	if (errcode > 1)
		return errcode;

	return 0;
}

static int cmdq_async_cmd(struct hinic_cmdq *cmdq, enum hinic_ack_type ack_type,
			  enum hinic_mod_type mod, u8 cmd,
			  struct hinic_cmd_buf *buf_in)
{
	struct hinic_wq *wq = cmdq->wq;
	int wqe_size = cmdq_wqe_size(WQE_LCMD_TYPE);
	u16 curr_prod_idx, next_prod_idx, num_wqebbs;
	struct hinic_cmdq_wqe *curr_wqe, wqe;
	int wrapped;

	num_wqebbs = WQE_NUM_WQEBBS(wqe_size, wq);

	spin_lock_bh(&cmdq->cmdq_lock);

	/* WQE_SIZE = WQEBB_SIZE, we will get the wq element and not shadow */
	curr_wqe = hinic_get_wqe(cmdq->wq, num_wqebbs, &curr_prod_idx);
	if (!curr_wqe) {
		spin_unlock_bh(&cmdq->cmdq_lock);
		return -EBUSY;
	}

	memset(&wqe, 0, sizeof(wqe));

	wrapped = cmdq->wrapped;
	next_prod_idx = curr_prod_idx + num_wqebbs;
	if (next_prod_idx >= cmdq->wq->q_depth) {
		cmdq->wrapped = !cmdq->wrapped;
		next_prod_idx -= cmdq->wq->q_depth;
	}

	cmdq_set_lcmd_wqe(&wqe, ASYNC_CMD, buf_in, NULL, wrapped,
			  ack_type, mod, cmd, curr_prod_idx);

	/* The data that is written to HW should be in Big Endian Format */
	hinic_cpu_to_be32(&wqe, wqe_size);

	cmdq_wqe_fill(curr_wqe, &wqe);

	cmdq->cmd_infos[curr_prod_idx].cmd_type = HINIC_CMD_TYPE_ASYNC;

	cmdq_set_db(cmdq, HINIC_CMDQ_ASYNC, next_prod_idx);

	spin_unlock_bh(&cmdq->cmdq_lock);

	return 0;
}

static int cmdq_set_arm_bit(struct hinic_cmdq *cmdq, void *buf_in, u16 in_size)
{
	struct hinic_wq *wq = cmdq->wq;
	struct hinic_cmdq_wqe *curr_wqe, wqe;
	u16 curr_prod_idx, next_prod_idx, num_wqebbs;
	int wrapped, wqe_size = cmdq_wqe_size(WQE_SCMD_TYPE);

	num_wqebbs = WQE_NUM_WQEBBS(wqe_size, wq);

	/* Keep wrapped and doorbell index correct. bh - for tasklet(ceq) */
	spin_lock_bh(&cmdq->cmdq_lock);

	/* WQE_SIZE = WQEBB_SIZE, we will get the wq element and not shadow */
	curr_wqe = hinic_get_wqe(cmdq->wq, num_wqebbs, &curr_prod_idx);
	if (!curr_wqe) {
		spin_unlock_bh(&cmdq->cmdq_lock);
		sdk_err(cmdq->hwdev->dev_hdl, "Can not get avalible wqebb setting arm\n");
		return -EBUSY;
	}

	memset(&wqe, 0, sizeof(wqe));

	wrapped = cmdq->wrapped;

	next_prod_idx = curr_prod_idx + num_wqebbs;
	if (next_prod_idx >= wq->q_depth) {
		cmdq->wrapped = !cmdq->wrapped;
		next_prod_idx -= wq->q_depth;
	}

	cmdq_set_inline_wqe(&wqe, SYNC_CMD_DIRECT_RESP, buf_in, in_size, NULL,
			    wrapped, HINIC_ACK_TYPE_CMDQ, HINIC_MOD_COMM,
			CMDQ_SET_ARM_CMD, curr_prod_idx);

	/* The data that is written to HW should be in Big Endian Format */
	hinic_cpu_to_be32(&wqe, wqe_size);

	/* cmdq wqe is not shadow, therefore wqe will be written to wq */
	cmdq_wqe_fill(curr_wqe, &wqe);

	cmdq->cmd_infos[curr_prod_idx].cmd_type = HINIC_CMD_TYPE_SET_ARM;

	cmdq_set_db(cmdq, cmdq->cmdq_type, next_prod_idx);

	spin_unlock_bh(&cmdq->cmdq_lock);

	return 0;
}

static int cmdq_params_valid(void *hwdev, struct hinic_cmd_buf *buf_in)
{
	if (!buf_in || !hwdev) {
		pr_err("Invalid CMDQ buffer addr\n");
		return -EINVAL;
	}

	if (!buf_in->size || buf_in->size > HINIC_CMDQ_MAX_DATA_SIZE) {
		pr_err("Invalid CMDQ buffer size: 0x%x\n", buf_in->size);
		return -EINVAL;
	}

	return 0;
}

#define WAIT_CMDQ_ENABLE_TIMEOUT	300

static int wait_cmdqs_enable(struct hinic_cmdqs *cmdqs)
{
	unsigned long end;

	end = jiffies + msecs_to_jiffies(WAIT_CMDQ_ENABLE_TIMEOUT);
	do {
		if (cmdqs->status & HINIC_CMDQ_ENABLE)
			return 0;
	} while (time_before(jiffies, end) && cmdqs->hwdev->chip_present_flag &&
			 !cmdqs->disable_flag);

	cmdqs->disable_flag = 1;

	return -EBUSY;
}

int hinic_cmdq_direct_resp(void *hwdev, enum hinic_ack_type ack_type,
			   enum hinic_mod_type mod, u8 cmd,
			   struct hinic_cmd_buf *buf_in, u64 *out_param,
			   u32 timeout)
{
	struct hinic_cmdqs *cmdqs;
	int err = cmdq_params_valid(hwdev, buf_in);

	if (err) {
		pr_err("Invalid CMDQ parameters\n");
		return err;
	}

	cmdqs = ((struct hinic_hwdev *)hwdev)->cmdqs;

	if (!(((struct hinic_hwdev *)hwdev)->chip_present_flag) ||
	    !hinic_is_hwdev_mod_inited(hwdev, HINIC_HWDEV_CMDQ_INITED))
		return -EPERM;

	err = wait_cmdqs_enable(cmdqs);
	if (err) {
		sdk_err(cmdqs->hwdev->dev_hdl, "Cmdq is disable\n");
		return err;
	}

	err = hinic_func_own_get(hwdev);
	if (err)
		return err;

	err = cmdq_sync_cmd_direct_resp(&cmdqs->cmdq[HINIC_CMDQ_SYNC], ack_type,
					mod, cmd, buf_in, out_param, timeout);
	hinic_func_own_free(hwdev);
	if (!(((struct hinic_hwdev *)hwdev)->chip_present_flag))
		return -ETIMEDOUT;
	else
		return err;
}
EXPORT_SYMBOL(hinic_cmdq_direct_resp);

int hinic_cmdq_detail_resp(void *hwdev,
			   enum hinic_ack_type ack_type,
			   enum hinic_mod_type mod, u8 cmd,
			   struct hinic_cmd_buf *buf_in,
			   struct hinic_cmd_buf *buf_out,
			   u32 timeout)
{
	struct hinic_cmdqs *cmdqs;
	int err = cmdq_params_valid(hwdev, buf_in);

	if (err)
		return err;

	cmdqs = ((struct hinic_hwdev *)hwdev)->cmdqs;

	if (!(((struct hinic_hwdev *)hwdev)->chip_present_flag) ||
	    !hinic_is_hwdev_mod_inited(hwdev, HINIC_HWDEV_CMDQ_INITED))
		return -EPERM;

	err = wait_cmdqs_enable(cmdqs);
	if (err) {
		sdk_err(cmdqs->hwdev->dev_hdl, "Cmdq is disable\n");
		return err;
	}

	err = cmdq_sync_cmd_detail_resp(&cmdqs->cmdq[HINIC_CMDQ_SYNC], ack_type,
					mod, cmd, buf_in, buf_out, timeout);
	if (!(((struct hinic_hwdev *)hwdev)->chip_present_flag))
		return -ETIMEDOUT;
	else
		return err;
}
EXPORT_SYMBOL(hinic_cmdq_detail_resp);

int hinic_cmdq_async(void *hwdev, enum hinic_ack_type ack_type,
		     enum hinic_mod_type mod, u8 cmd,
				     struct hinic_cmd_buf *buf_in)
{
	struct hinic_cmdqs *cmdqs;
	int err = cmdq_params_valid(hwdev, buf_in);

	if (err)
		return err;

	cmdqs = ((struct hinic_hwdev *)hwdev)->cmdqs;

	if (!(((struct hinic_hwdev *)hwdev)->chip_present_flag) ||
	    !hinic_is_hwdev_mod_inited(hwdev, HINIC_HWDEV_CMDQ_INITED))
		return -EPERM;

	err = wait_cmdqs_enable(cmdqs);
	if (err) {
		sdk_err(cmdqs->hwdev->dev_hdl, "Cmdq is disable\n");
		return err;
	}

	return cmdq_async_cmd(&cmdqs->cmdq[HINIC_CMDQ_ASYNC], ack_type, mod,
			      cmd, buf_in);
}
EXPORT_SYMBOL(hinic_cmdq_async);

int hinic_set_arm_bit(void *hwdev, enum hinic_set_arm_type q_type, u16 q_id)
{
	struct hinic_cmdqs *cmdqs;
	struct hinic_cmdq *cmdq;
	struct hinic_cmdq_arm_bit arm_bit;
	enum hinic_cmdq_type cmdq_type = HINIC_CMDQ_SYNC;
	u16 in_size;
	int err;

	if (!hwdev)
		return -EINVAL;

	if (!(((struct hinic_hwdev *)hwdev)->chip_present_flag) ||
	    !hinic_is_hwdev_mod_inited(hwdev, HINIC_HWDEV_CMDQ_INITED))
		return -EPERM;

	cmdqs = ((struct hinic_hwdev *)hwdev)->cmdqs;

	if (!(cmdqs->status & HINIC_CMDQ_ENABLE))
		return -EBUSY;

	if (q_type == HINIC_SET_ARM_CMDQ) {
		if (q_id >= HINIC_MAX_CMDQ_TYPES)
			return -EFAULT;

		cmdq_type = q_id;
	}
	/* sq is using interrupt now, so we only need to set arm bit for cmdq,
	 * remove comment below if need to set sq arm bit
	 * else
	 *	cmdq_type = HINIC_CMDQ_SYNC;
	 */

	cmdq = &cmdqs->cmdq[cmdq_type];

	arm_bit.q_type = q_type;
	arm_bit.q_id   = q_id;
	in_size = sizeof(arm_bit);

	err = cmdq_set_arm_bit(cmdq, &arm_bit, in_size);
	if (err) {
		sdk_err(cmdqs->hwdev->dev_hdl,
			"Failed to set arm for q_type: %d, qid %d\n",
			q_type, q_id);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_set_arm_bit);

static void clear_wqe_complete_bit(struct hinic_cmdq *cmdq,
				   struct hinic_cmdq_wqe *wqe, u16 ci)
{
	struct hinic_cmdq_wqe_lcmd *wqe_lcmd;
	struct hinic_cmdq_inline_wqe *inline_wqe;
	struct hinic_cmdq_wqe_scmd *wqe_scmd;
	struct hinic_ctrl *ctrl;
	u32 header_info = be32_to_cpu(WQE_HEADER(wqe)->header_info);
	int buf_len = CMDQ_WQE_HEADER_GET(header_info, BUFDESC_LEN);
	int wqe_size = cmdq_get_wqe_size(buf_len);
	u16 num_wqebbs;

	if (wqe_size == WQE_LCMD_SIZE) {
		wqe_lcmd = &wqe->wqe_lcmd;
		ctrl = &wqe_lcmd->ctrl;
	} else {
		inline_wqe = &wqe->inline_wqe;
		wqe_scmd = &inline_wqe->wqe_scmd;
		ctrl = &wqe_scmd->ctrl;
	}

	/* clear HW busy bit */
	ctrl->ctrl_info = 0;
	cmdq->cmd_infos[ci].cmd_type = HINIC_CMD_TYPE_NONE;

	wmb();	/* verify wqe is clear */

	num_wqebbs = WQE_NUM_WQEBBS(wqe_size, cmdq->wq);
	hinic_put_wqe(cmdq->wq, num_wqebbs);
}

static void cmdq_sync_cmd_handler(struct hinic_cmdq *cmdq,
				  struct hinic_cmdq_wqe *wqe, u16 cons_idx)
{
	u16 prod_idx = cons_idx;

	spin_lock(&cmdq->cmdq_lock);

	cmdq_update_cmd_status(cmdq, prod_idx, wqe);

	if (cmdq->cmd_infos[prod_idx].cmpt_code) {
		*(cmdq->cmd_infos[prod_idx].cmpt_code) =
							CMDQ_COMPLETE_CMPT_CODE;
		cmdq->cmd_infos[prod_idx].cmpt_code = NULL;
	}

	/* make sure cmpt_code operation before done operation */
	smp_rmb();

	if (cmdq->cmd_infos[prod_idx].done) {
		complete(cmdq->cmd_infos[prod_idx].done);
		cmdq->cmd_infos[prod_idx].done = NULL;
	}

	spin_unlock(&cmdq->cmdq_lock);

	clear_wqe_complete_bit(cmdq, wqe, cons_idx);
}

static void cmdq_async_cmd_handler(struct hinic_hwdev *hwdev,
				   struct hinic_cmdq *cmdq,
				   struct hinic_cmdq_wqe *wqe, u16 ci)
{
	u64 buf = wqe->wqe_lcmd.buf_desc.saved_async_buf;
	int addr_sz = sizeof(u64);

	hinic_be32_to_cpu((void *)&buf, addr_sz);
	if (buf)
		hinic_free_cmd_buf(hwdev, (struct hinic_cmd_buf *)buf);

	clear_wqe_complete_bit(cmdq, wqe, ci);
}

static int cmdq_arm_ceq_handler(struct hinic_cmdq *cmdq,
				struct hinic_cmdq_wqe *wqe, u16 ci)
{
	struct hinic_cmdq_inline_wqe *inline_wqe = &wqe->inline_wqe;
	struct hinic_cmdq_wqe_scmd *wqe_scmd = &inline_wqe->wqe_scmd;
	struct hinic_ctrl *ctrl = &wqe_scmd->ctrl;
	u32 ctrl_info = be32_to_cpu((ctrl)->ctrl_info);

	if (!WQE_COMPLETED(ctrl_info))
		return -EBUSY;

	clear_wqe_complete_bit(cmdq, wqe, ci);

	return 0;
}

#define HINIC_CMDQ_WQE_HEAD_LEN		32
static void hinic_dump_cmdq_wqe_head(struct hinic_hwdev *hwdev,
				     struct hinic_cmdq_wqe *wqe)
{
	u32 i;
	u32 *data = (u32 *)wqe;

	for (i = 0; i < (HINIC_CMDQ_WQE_HEAD_LEN / sizeof(u32)); i += 4) {
		sdk_info(hwdev->dev_hdl, "wqe data: 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
			 data[i], data[i + 1], data[i + 2],
			 data[i + 3]);
	}
}

void hinic_cmdq_ceq_handler(void *handle, u32 ceqe_data)
{
	struct hinic_cmdqs *cmdqs = ((struct hinic_hwdev *)handle)->cmdqs;
	enum hinic_cmdq_type cmdq_type = CEQE_CMDQ_GET(ceqe_data, TYPE);
	struct hinic_cmdq *cmdq = &cmdqs->cmdq[cmdq_type];
	struct hinic_hwdev *hwdev = cmdqs->hwdev;
	struct hinic_cmdq_wqe *wqe;
	struct hinic_cmdq_wqe_lcmd *wqe_lcmd;
	struct hinic_ctrl *ctrl;
	struct hinic_cmdq_cmd_info *cmd_info;
	u32 ctrl_info;
	u16 ci;
	int set_arm = 1;

	while ((wqe = hinic_read_wqe(cmdq->wq, 1, &ci)) != NULL) {
		cmd_info = &cmdq->cmd_infos[ci];

		if (cmd_info->cmd_type == HINIC_CMD_TYPE_NONE) {
			set_arm = 1;
			break;
		} else if (cmd_info->cmd_type == HINIC_CMD_TYPE_TIMEOUT ||
			   cmd_info->cmd_type == HINIC_CMD_TYPE_FAKE_TIMEOUT) {
			if (cmd_info->cmd_type == HINIC_CMD_TYPE_TIMEOUT) {
				sdk_info(hwdev->dev_hdl, "Cmdq timeout, q_id: %u, ci: %u\n",
					 cmdq_type, ci);
				hinic_dump_cmdq_wqe_head(hwdev, wqe);
			}

			set_arm = 1;
			clear_wqe_complete_bit(cmdq, wqe, ci);
		} else if (cmd_info->cmd_type == HINIC_CMD_TYPE_SET_ARM) {
			/* arm_bit was set until here */
			set_arm = 0;

			if (cmdq_arm_ceq_handler(cmdq, wqe, ci))
				break;
		} else {
			set_arm = 1;

			/* only arm bit is using scmd wqe, the wqe is lcmd */
			wqe_lcmd = &wqe->wqe_lcmd;
			ctrl = &wqe_lcmd->ctrl;
			ctrl_info = be32_to_cpu((ctrl)->ctrl_info);

			if (!WQE_COMPLETED(ctrl_info))
				break;

			/* This memory barrier is needed to keep us from reading
			 * any other fields out of the cmdq wqe until we have
			 * verified the command has been processed and
			 * written back.
			 */
			dma_rmb();

			if (cmdq_type == HINIC_CMDQ_ASYNC)
				cmdq_async_cmd_handler(hwdev, cmdq, wqe, ci);
			else
				cmdq_sync_cmd_handler(cmdq, wqe, ci);
		}
	}

	if (set_arm)
		hinic_set_arm_bit(hwdev, HINIC_SET_ARM_CMDQ, cmdq_type);
}

static void cmdq_init_queue_ctxt(struct hinic_cmdq *cmdq,
				 struct hinic_cmdq_pages *cmdq_pages,
				 struct hinic_cmdq_ctxt *cmdq_ctxt)
{
	struct hinic_cmdqs *cmdqs = cmdq_to_cmdqs(cmdq);
	struct hinic_hwdev *hwdev = cmdqs->hwdev;
	struct hinic_wq *wq = cmdq->wq;
	struct hinic_cmdq_ctxt_info *ctxt_info = &cmdq_ctxt->ctxt_info;
	u64 wq_first_page_paddr, cmdq_first_block_paddr, pfn;
	u16 start_ci = (u16)wq->cons_idx;

	/* The data in the HW is in Big Endian Format */
	wq_first_page_paddr = be64_to_cpu(*wq->block_vaddr);

	pfn = CMDQ_PFN(wq_first_page_paddr);

	ctxt_info->curr_wqe_page_pfn =
		CMDQ_CTXT_PAGE_INFO_SET(1, HW_BUSY_BIT) |
		CMDQ_CTXT_PAGE_INFO_SET(1, CEQ_EN)	|
		CMDQ_CTXT_PAGE_INFO_SET(1, CEQ_ARM)	|
		CMDQ_CTXT_PAGE_INFO_SET(HINIC_CEQ_ID_CMDQ, EQ_ID) |
		CMDQ_CTXT_PAGE_INFO_SET(pfn, CURR_WQE_PAGE_PFN);

	/* If only use one page, use 0-level CLA */
	if (cmdq->wq->num_q_pages != 1) {
		cmdq_first_block_paddr = cmdq_pages->cmdq_page_paddr;
		pfn = CMDQ_PFN(cmdq_first_block_paddr);
	}

	ctxt_info->wq_block_pfn = CMDQ_CTXT_BLOCK_INFO_SET(start_ci, CI) |
				CMDQ_CTXT_BLOCK_INFO_SET(pfn, WQ_BLOCK_PFN);

	cmdq_ctxt->func_idx = hinic_global_func_id_hw(hwdev);
	cmdq_ctxt->ppf_idx  = HINIC_HWIF_PPF_IDX(hwdev->hwif);
	cmdq_ctxt->cmdq_id  = cmdq->cmdq_type;
}

bool hinic_cmdq_check_vf_ctxt(struct hinic_hwdev *hwdev,
			      struct hinic_cmdq_ctxt *cmdq_ctxt)
{
	struct hinic_cmdq_ctxt_info *ctxt_info = &cmdq_ctxt->ctxt_info;
	u64 curr_pg_pfn, wq_block_pfn;

	if (cmdq_ctxt->ppf_idx != hinic_ppf_idx(hwdev) ||
	    cmdq_ctxt->cmdq_id > HINIC_MAX_CMDQ_TYPES)
		return false;

	curr_pg_pfn = CMDQ_CTXT_PAGE_INFO_GET(ctxt_info->curr_wqe_page_pfn,
					      CURR_WQE_PAGE_PFN);
	wq_block_pfn = CMDQ_CTXT_BLOCK_INFO_GET(ctxt_info->wq_block_pfn,
						WQ_BLOCK_PFN);
	/* VF must use 0-level CLA */
	if (curr_pg_pfn != wq_block_pfn)
		return false;

	return true;
}

static int init_cmdq(struct hinic_cmdq *cmdq, struct hinic_hwdev *hwdev,
		     struct hinic_wq *wq, enum hinic_cmdq_type q_type)
{
	void __iomem *db_base;
	int err = 0;

	cmdq->wq = wq;
	cmdq->cmdq_type = q_type;
	cmdq->wrapped = 1;
	cmdq->hwdev = hwdev;

	spin_lock_init(&cmdq->cmdq_lock);

	cmdq->cmd_infos = kcalloc(wq->q_depth, sizeof(*cmdq->cmd_infos),
				  GFP_KERNEL);
	if (!cmdq->cmd_infos) {
		err = -ENOMEM;
		goto cmd_infos_err;
	}

	err = hinic_alloc_db_addr(hwdev, &db_base, NULL);
	if (err)
		goto alloc_db_err;

	cmdq->db_base = (u8 *)db_base;
	return 0;

alloc_db_err:
	kfree(cmdq->cmd_infos);

cmd_infos_err:
	spin_lock_deinit(&cmdq->cmdq_lock);

	return err;
}

static void free_cmdq(struct hinic_hwdev *hwdev, struct hinic_cmdq *cmdq)
{
	hinic_free_db_addr(hwdev, cmdq->db_base, NULL);
	kfree(cmdq->cmd_infos);
	spin_lock_deinit(&cmdq->cmdq_lock);
}

int hinic_set_cmdq_ctxts(struct hinic_hwdev *hwdev)
{
	struct hinic_cmdqs *cmdqs = hwdev->cmdqs;
	struct hinic_cmdq_ctxt *cmdq_ctxt, cmdq_ctxt_out = {0};
	enum hinic_cmdq_type cmdq_type;
	u16 in_size;
	u16 out_size = sizeof(*cmdq_ctxt);
	int err;

	cmdq_type = HINIC_CMDQ_SYNC;
	for (; cmdq_type < HINIC_MAX_CMDQ_TYPES; cmdq_type++) {
		cmdq_ctxt = &cmdqs->cmdq[cmdq_type].cmdq_ctxt;
		cmdq_ctxt->func_idx = hinic_global_func_id_hw(hwdev);
		in_size = sizeof(*cmdq_ctxt);
		err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
					     HINIC_MGMT_CMD_CMDQ_CTXT_SET,
					     cmdq_ctxt, in_size,
					     &cmdq_ctxt_out, &out_size, 0);
		if (err || !out_size || cmdq_ctxt_out.status) {
			sdk_err(hwdev->dev_hdl, "Failed to set cmdq ctxt, err: %d, status: 0x%x, out_size: 0x%x\n",
				err, cmdq_ctxt_out.status, out_size);
			return -EFAULT;
		}
	}

	cmdqs->status |= HINIC_CMDQ_ENABLE;
	cmdqs->disable_flag = 0;

	return 0;
}

void hinic_cmdq_flush_cmd(struct hinic_hwdev *hwdev,
			  struct hinic_cmdq *cmdq)
{
	struct hinic_cmdq_wqe *wqe;
	struct hinic_cmdq_cmd_info *cmdq_info;
	u16 ci, wqe_left, i;
	u64 buf;

	spin_lock_bh(&cmdq->cmdq_lock);
	wqe_left = cmdq->wq->q_depth - (u16)atomic_read(&cmdq->wq->delta);
	ci = MASKED_WQE_IDX(cmdq->wq, cmdq->wq->cons_idx);
	for (i = 0; i < wqe_left; i++, ci++) {
		ci = MASKED_WQE_IDX(cmdq->wq, ci);
		cmdq_info = &cmdq->cmd_infos[ci];

		if (cmdq_info->cmd_type == HINIC_CMD_TYPE_SET_ARM)
			continue;

		if (cmdq->cmdq_type == HINIC_CMDQ_ASYNC) {
			wqe = hinic_get_wqebb_addr(cmdq->wq, ci);
			buf = wqe->wqe_lcmd.buf_desc.saved_async_buf;
			wqe->wqe_lcmd.buf_desc.saved_async_buf = 0;

			hinic_be32_to_cpu((void *)&buf, sizeof(u64));
			if (buf)
				hinic_free_cmd_buf(hwdev,
						   (struct hinic_cmd_buf *)buf);
		} else {
			if (cmdq_info->done) {
				complete(cmdq_info->done);
				cmdq_info->done = NULL;
				cmdq_info->cmpt_code = NULL;
				cmdq_info->direct_resp = NULL;
				cmdq_info->errcode = NULL;
			}
		}
	}

	spin_unlock_bh(&cmdq->cmdq_lock);
}

int hinic_reinit_cmdq_ctxts(struct hinic_hwdev *hwdev)
{
	struct hinic_cmdqs *cmdqs = hwdev->cmdqs;
	enum hinic_cmdq_type cmdq_type;

	cmdq_type = HINIC_CMDQ_SYNC;
	for (; cmdq_type < HINIC_MAX_CMDQ_TYPES; cmdq_type++) {
		hinic_cmdq_flush_cmd(hwdev, &cmdqs->cmdq[cmdq_type]);
		cmdqs->cmdq[cmdq_type].wrapped = 1;
		hinic_wq_wqe_pg_clear(cmdqs->cmdq[cmdq_type].wq);
	}

	return hinic_set_cmdq_ctxts(hwdev);
}

int hinic_cmdqs_init(struct hinic_hwdev *hwdev)
{
	struct hinic_cmdqs *cmdqs;
	struct hinic_cmdq_ctxt *cmdq_ctxt;
	enum hinic_cmdq_type type, cmdq_type;
	size_t saved_wqs_size;
	u32 max_wqe_size;
	int err;

	cmdqs = kzalloc(sizeof(*cmdqs), GFP_KERNEL);
	if (!cmdqs)
		return -ENOMEM;

	hwdev->cmdqs = cmdqs;
	cmdqs->hwdev = hwdev;

	saved_wqs_size = HINIC_MAX_CMDQ_TYPES * sizeof(struct hinic_wq);
	cmdqs->saved_wqs = kzalloc(saved_wqs_size, GFP_KERNEL);
	if (!cmdqs->saved_wqs) {
		sdk_err(hwdev->dev_hdl, "Failed to allocate saved wqs\n");
		err = -ENOMEM;
		goto alloc_wqs_err;
	}

	cmdqs->cmd_buf_pool = dma_pool_create("hinic_cmdq", hwdev->dev_hdl,
					      HINIC_CMDQ_BUF_SIZE,
						HINIC_CMDQ_BUF_SIZE, 0ULL);
	if (!cmdqs->cmd_buf_pool) {
		sdk_err(hwdev->dev_hdl, "Failed to create cmdq buffer pool\n");
		err = -ENOMEM;
		goto pool_create_err;
	}

	max_wqe_size = (u32)cmdq_wqe_size(WQE_LCMD_TYPE);
	err = hinic_cmdq_alloc(&cmdqs->cmdq_pages, cmdqs->saved_wqs,
			       hwdev->dev_hdl, HINIC_MAX_CMDQ_TYPES,
			       hwdev->wq_page_size, CMDQ_WQEBB_SIZE,
			       HINIC_CMDQ_DEPTH, max_wqe_size);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to allocate cmdq\n");
		goto cmdq_alloc_err;
	}

	cmdq_type = HINIC_CMDQ_SYNC;
	for (; cmdq_type < HINIC_MAX_CMDQ_TYPES; cmdq_type++) {
		err = init_cmdq(&cmdqs->cmdq[cmdq_type], hwdev,
				&cmdqs->saved_wqs[cmdq_type], cmdq_type);
		if (err) {
			sdk_err(hwdev->dev_hdl, "Failed to initialize cmdq type :%d\n",
				cmdq_type);
			goto init_cmdq_err;
		}

		cmdq_ctxt = &cmdqs->cmdq[cmdq_type].cmdq_ctxt;
		cmdq_init_queue_ctxt(&cmdqs->cmdq[cmdq_type],
				     &cmdqs->cmdq_pages, cmdq_ctxt);
	}

	err = hinic_set_cmdq_ctxts(hwdev);
	if (err)
		goto init_cmdq_err;

	return 0;

init_cmdq_err:
	type = HINIC_CMDQ_SYNC;
	for (; type < cmdq_type; type++)
		free_cmdq(hwdev, &cmdqs->cmdq[type]);

	hinic_cmdq_free(&cmdqs->cmdq_pages, cmdqs->saved_wqs,
			HINIC_MAX_CMDQ_TYPES);

cmdq_alloc_err:
	dma_pool_destroy(cmdqs->cmd_buf_pool);

pool_create_err:
	kfree(cmdqs->saved_wqs);

alloc_wqs_err:
	kfree(cmdqs);

	return err;
}

void hinic_cmdqs_free(struct hinic_hwdev *hwdev)
{
	struct hinic_cmdqs *cmdqs = hwdev->cmdqs;
	enum hinic_cmdq_type cmdq_type = HINIC_CMDQ_SYNC;

	cmdqs->status &= ~HINIC_CMDQ_ENABLE;

	for (; cmdq_type < HINIC_MAX_CMDQ_TYPES; cmdq_type++) {
		hinic_cmdq_flush_cmd(hwdev, &cmdqs->cmdq[cmdq_type]);
		free_cmdq(cmdqs->hwdev, &cmdqs->cmdq[cmdq_type]);
	}

	hinic_cmdq_free(&cmdqs->cmdq_pages, cmdqs->saved_wqs,
			HINIC_MAX_CMDQ_TYPES);

	dma_pool_destroy(cmdqs->cmd_buf_pool);

	kfree(cmdqs->saved_wqs);

	kfree(cmdqs);
}
