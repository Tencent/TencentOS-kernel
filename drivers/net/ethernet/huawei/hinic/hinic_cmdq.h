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

#ifndef HINIC_CMDQ_H_
#define HINIC_CMDQ_H_

#define HINIC_DB_OFF			0x00000800

#define HINIC_SCMD_DATA_LEN		16

#define	HINIC_CMDQ_DEPTH		4096

#define	HINIC_CMDQ_BUF_SIZE		2048U
#define HINIC_CMDQ_BUF_HW_RSVD		8
#define HINIC_CMDQ_MAX_DATA_SIZE	\
		(HINIC_CMDQ_BUF_SIZE - HINIC_CMDQ_BUF_HW_RSVD)

enum hinic_cmdq_type {
	HINIC_CMDQ_SYNC,
	HINIC_CMDQ_ASYNC,
	HINIC_MAX_CMDQ_TYPES,
};

enum hinic_db_src_type {
	HINIC_DB_SRC_CMDQ_TYPE,
	HINIC_DB_SRC_L2NIC_SQ_TYPE,
};

enum hinic_cmdq_db_type {
	HINIC_DB_SQ_RQ_TYPE,
	HINIC_DB_CMDQ_TYPE,
};

/* CMDQ WQE CTRLS */
struct hinic_cmdq_header {
	u32	header_info;
	u32	saved_data;
};

struct hinic_scmd_bufdesc {
	u32	buf_len;
	u32	rsvd;
	u8	data[HINIC_SCMD_DATA_LEN];
};

struct hinic_lcmd_bufdesc {
	struct hinic_sge	sge;
	u32			rsvd1;
	u64			saved_async_buf;
	u64			rsvd3;
};

struct hinic_cmdq_db {
	u32	db_info;
	u32	rsvd;
};

struct hinic_status {
	u32 status_info;
};

struct hinic_ctrl {
	u32 ctrl_info;
};

struct hinic_sge_resp {
	struct hinic_sge sge;
	u32		rsvd;
};

struct hinic_cmdq_completion {
	/* HW Format */
	union {
		struct hinic_sge_resp	sge_resp;
		u64			direct_resp;
	};
};

struct hinic_cmdq_wqe_scmd {
	struct hinic_cmdq_header	header;
	struct hinic_cmdq_db		db;
	struct hinic_status		status;
	struct hinic_ctrl		ctrl;
	struct hinic_cmdq_completion	completion;
	struct hinic_scmd_bufdesc	buf_desc;
};

struct hinic_cmdq_wqe_lcmd {
	struct hinic_cmdq_header	header;
	struct hinic_status		status;
	struct hinic_ctrl		ctrl;
	struct hinic_cmdq_completion	completion;
	struct hinic_lcmd_bufdesc	buf_desc;
};

struct hinic_cmdq_inline_wqe {
	struct hinic_cmdq_wqe_scmd	wqe_scmd;
};

struct hinic_cmdq_wqe {
	/* HW Format */
	union {
		struct hinic_cmdq_inline_wqe	inline_wqe;
		struct hinic_cmdq_wqe_lcmd	wqe_lcmd;
	};
};

struct hinic_cmdq_arm_bit {
	u32	q_type;
	u32	q_id;
};

struct hinic_cmdq_ctxt_info {
	u64	curr_wqe_page_pfn;
	u64	wq_block_pfn;
};

struct hinic_cmdq_ctxt {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_idx;
	u8	cmdq_id;
	u8	ppf_idx;

	u8	rsvd1[4];

	struct hinic_cmdq_ctxt_info ctxt_info;
};

enum hinic_cmdq_status {
	HINIC_CMDQ_ENABLE = BIT(0),
};

enum hinic_cmdq_cmd_type {
	HINIC_CMD_TYPE_NONE,
	HINIC_CMD_TYPE_SET_ARM,
	HINIC_CMD_TYPE_DIRECT_RESP,
	HINIC_CMD_TYPE_SGE_RESP,
	HINIC_CMD_TYPE_ASYNC,
	HINIC_CMD_TYPE_TIMEOUT,
	HINIC_CMD_TYPE_FAKE_TIMEOUT,
};

struct hinic_cmdq_cmd_info {
	enum hinic_cmdq_cmd_type	cmd_type;

	struct completion		*done;
	int				*errcode;
	int				*cmpt_code;
	u64				*direct_resp;
	u64				cmdq_msg_id;
};

struct hinic_cmdq {
	struct hinic_wq			*wq;

	enum hinic_cmdq_type		cmdq_type;
	int				wrapped;

	/* spinlock for send cmdq commands */
	spinlock_t			cmdq_lock;

	/* doorbell area */
	u8 __iomem			*db_base;

	struct hinic_cmdq_ctxt		cmdq_ctxt;

	struct hinic_cmdq_cmd_info	*cmd_infos;

	struct hinic_hwdev		*hwdev;
};

struct hinic_cmdqs {
	struct hinic_hwdev		*hwdev;

	struct pci_pool			*cmd_buf_pool;

	struct hinic_wq			*saved_wqs;

	struct hinic_cmdq_pages		cmdq_pages;
	struct hinic_cmdq		cmdq[HINIC_MAX_CMDQ_TYPES];

	u32				status;
	u32				disable_flag;
};

void hinic_cmdq_ceq_handler(void *hwdev, u32 ceqe_data);

int hinic_reinit_cmdq_ctxts(struct hinic_hwdev *hwdev);

bool hinic_cmdq_idle(struct hinic_cmdq *cmdq);

int hinic_cmdqs_init(struct hinic_hwdev *hwdev);

void hinic_cmdqs_free(struct hinic_hwdev *hwdev);

bool hinic_cmdq_check_vf_ctxt(struct hinic_hwdev *hwdev,
			      struct hinic_cmdq_ctxt *cmdq_ctxt);

void hinic_cmdq_flush_cmd(struct hinic_hwdev *hwdev,
			  struct hinic_cmdq *cmdq);

#endif
