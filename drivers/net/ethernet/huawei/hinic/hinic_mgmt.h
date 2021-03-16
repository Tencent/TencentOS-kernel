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

#ifndef HINIC_MGMT_H_
#define HINIC_MGMT_H_

#define HINIC_MSG_HEADER_MSG_LEN_SHIFT				0
#define HINIC_MSG_HEADER_MODULE_SHIFT				11
#define HINIC_MSG_HEADER_SEG_LEN_SHIFT				16
#define HINIC_MSG_HEADER_NO_ACK_SHIFT				22
#define HINIC_MSG_HEADER_ASYNC_MGMT_TO_PF_SHIFT			23
#define HINIC_MSG_HEADER_SEQID_SHIFT				24
#define HINIC_MSG_HEADER_LAST_SHIFT				30
#define HINIC_MSG_HEADER_DIRECTION_SHIFT			31
#define HINIC_MSG_HEADER_CMD_SHIFT				32
#define HINIC_MSG_HEADER_PCI_INTF_IDX_SHIFT			48
#define HINIC_MSG_HEADER_P2P_IDX_SHIFT				50
#define HINIC_MSG_HEADER_MSG_ID_SHIFT				54

#define HINIC_MSG_HEADER_MSG_LEN_MASK				0x7FF
#define HINIC_MSG_HEADER_MODULE_MASK				0x1F
#define HINIC_MSG_HEADER_SEG_LEN_MASK				0x3F
#define HINIC_MSG_HEADER_NO_ACK_MASK				0x1
#define HINIC_MSG_HEADER_ASYNC_MGMT_TO_PF_MASK			0x1
#define HINIC_MSG_HEADER_SEQID_MASK				0x3F
#define HINIC_MSG_HEADER_LAST_MASK				0x1
#define HINIC_MSG_HEADER_DIRECTION_MASK				0x1
#define HINIC_MSG_HEADER_CMD_MASK				0xFF
#define HINIC_MSG_HEADER_PCI_INTF_IDX_MASK			0x3
#define HINIC_MSG_HEADER_P2P_IDX_MASK				0xF
#define HINIC_MSG_HEADER_MSG_ID_MASK				0x3FF

#define HINIC_MSG_HEADER_GET(val, member)			\
		(((val) >> HINIC_MSG_HEADER_##member##_SHIFT) & \
		HINIC_MSG_HEADER_##member##_MASK)

#define HINIC_MSG_HEADER_SET(val, member)			\
		((u64)((val) & HINIC_MSG_HEADER_##member##_MASK) << \
		HINIC_MSG_HEADER_##member##_SHIFT)

#define HINIC_MGMT_WQ_NAME "hinic_mgmt"

enum clp_data_type {
	HINIC_CLP_REQ_HOST = 0,
	HINIC_CLP_RSP_HOST = 1
};

enum clp_reg_type {
	HINIC_CLP_BA_HOST = 0,
	HINIC_CLP_SIZE_HOST = 1,
	HINIC_CLP_LEN_HOST = 2,
	HINIC_CLP_START_REQ_HOST = 3,
	HINIC_CLP_READY_RSP_HOST = 4
};

#define HINIC_CLP_REG_GAP			0x20
#define HINIC_CLP_INPUT_BUFFER_LEN_HOST		4096UL
#define HINIC_CLP_DATA_UNIT_HOST		4UL

#define HINIC_BAR01_GLOABAL_CTL_OFFSET		0x4000
#define HINIC_BAR01_CLP_OFFSET			0x5000

#define HINIC_CLP_SRAM_SIZE_REG		(HINIC_BAR01_GLOABAL_CTL_OFFSET + 0x220)
#define HINIC_CLP_REQ_SRAM_BA_REG	(HINIC_BAR01_GLOABAL_CTL_OFFSET + 0x224)
#define HINIC_CLP_RSP_SRAM_BA_REG	(HINIC_BAR01_GLOABAL_CTL_OFFSET + 0x228)
#define HINIC_CLP_REQ_REG		(HINIC_BAR01_GLOABAL_CTL_OFFSET + 0x22c)
#define HINIC_CLP_RSP_REG		(HINIC_BAR01_GLOABAL_CTL_OFFSET + 0x230)
#define HINIC_CLP_REG(member)		(HINIC_CLP_##member##_REG)

#define HINIC_CLP_REQ_DATA			(HINIC_BAR01_CLP_OFFSET)
#define HINIC_CLP_RSP_DATA		(HINIC_BAR01_CLP_OFFSET + 0x1000)
#define HINIC_CLP_DATA(member)			(HINIC_CLP_##member##_DATA)

#define HINIC_CLP_SRAM_SIZE_OFFSET		16
#define HINIC_CLP_SRAM_BASE_OFFSET		0
#define HINIC_CLP_LEN_OFFSET			0
#define HINIC_CLP_START_OFFSET			31
#define HINIC_CLP_READY_OFFSET			31
#define HINIC_CLP_OFFSET(member)		(HINIC_CLP_##member##_OFFSET)

#define HINIC_CLP_SRAM_SIZE_BIT_LEN		0x7ffUL
#define HINIC_CLP_SRAM_BASE_BIT_LEN		0x7ffffffUL
#define HINIC_CLP_LEN_BIT_LEN			0x7ffUL
#define HINIC_CLP_START_BIT_LEN			0x1UL
#define HINIC_CLP_READY_BIT_LEN			0x1UL
#define HINIC_CLP_MASK(member)			(HINIC_CLP_##member##_BIT_LEN)

#define HINIC_CLP_DELAY_CNT_MAX			200UL
#define HINIC_CLP_SRAM_SIZE_REG_MAX		0x3ff
#define HINIC_CLP_SRAM_BASE_REG_MAX		0x7ffffff
#define HINIC_CLP_LEN_REG_MAX			0x3ff
#define HINIC_CLP_START_OR_READY_REG_MAX	0x1

enum hinic_msg_direction_type {
	HINIC_MSG_DIRECT_SEND	= 0,
	HINIC_MSG_RESPONSE	= 1
};

enum hinic_msg_segment_type {
	NOT_LAST_SEGMENT = 0,
	LAST_SEGMENT	 = 1,
};

enum hinic_mgmt_msg_type {
	ASYNC_MGMT_MSG  = 0,
	SYNC_MGMT_MSG	= 1,
};

enum hinic_msg_ack_type {
	HINIC_MSG_ACK = 0,
	HINIC_MSG_NO_ACK = 1,
};

struct hinic_recv_msg {
	void			*msg;

	struct completion	recv_done;

	u16			msg_len;
	enum hinic_mod_type	mod;
	u8			cmd;
	u8			seq_id;
	u16			msg_id;
	int			async_mgmt_to_pf;
};

#define HINIC_COMM_SELF_CMD_MAX 8

struct comm_up_self_msg_sub_info {
	u8 cmd;
	comm_up_self_msg_proc proc;
};

struct comm_up_self_msg_info {
	u8 cmd_num;
	struct comm_up_self_msg_sub_info info[HINIC_COMM_SELF_CMD_MAX];
};

enum comm_pf_to_mgmt_event_state {
	SEND_EVENT_UNINIT = 0,
	SEND_EVENT_START,
	SEND_EVENT_FAIL,
	SEND_EVENT_TIMEOUT,
	SEND_EVENT_END,
};

enum hinic_mgmt_msg_cb_state {
	HINIC_MGMT_MSG_CB_REG = 0,
	HINIC_MGMT_MSG_CB_RUNNING,
};

struct hinic_clp_pf_to_mgmt {
	struct semaphore	clp_msg_lock;
	void			*clp_msg_buf;
};

struct hinic_msg_pf_to_mgmt {
	struct hinic_hwdev		*hwdev;

	/* Async cmd can not be scheduling */
	spinlock_t			async_msg_lock;
	struct semaphore		sync_msg_lock;

	struct workqueue_struct		*workq;

	void				*async_msg_buf;
	void				*sync_msg_buf;
	void				*mgmt_ack_buf;

	struct hinic_recv_msg		recv_msg_from_mgmt;
	struct hinic_recv_msg		recv_resp_msg_from_mgmt;

	u16				async_msg_id;
	u16				sync_msg_id;

	struct hinic_api_cmd_chain	*cmd_chain[HINIC_API_CMD_MAX];

	hinic_mgmt_msg_cb		recv_mgmt_msg_cb[HINIC_MOD_HW_MAX];
	void				*recv_mgmt_msg_data[HINIC_MOD_HW_MAX];
	unsigned long			mgmt_msg_cb_state[HINIC_MOD_HW_MAX];

	void	(*async_msg_cb[HINIC_MOD_HW_MAX])(void *handle,
						  enum hinic_mgmt_cmd cmd,
						  void *priv_data, u32 msg_id,
						  void *buf_out, u32 out_size);

	void	*async_msg_cb_data[HINIC_MOD_HW_MAX];

	struct comm_up_self_msg_info	proc;

	/* lock when sending msg */
	spinlock_t		sync_event_lock;
	enum comm_pf_to_mgmt_event_state event_flag;
};

struct hinic_mgmt_msg_handle_work {
	struct work_struct work;
	struct hinic_msg_pf_to_mgmt *pf_to_mgmt;

	void			*msg;
	u16			msg_len;

	enum hinic_mod_type	mod;
	u8			cmd;
	u16			msg_id;

	int			async_mgmt_to_pf;
};

int hinic_pf_to_mgmt_no_ack(void *hwdev, enum hinic_mod_type mod, u8 cmd,
			    void *buf_in, u16 in_size);

void hinic_mgmt_msg_aeqe_handler(void *handle, u8 *header, u8 size);

int hinic_pf_to_mgmt_init(struct hinic_hwdev *hwdev);

void hinic_pf_to_mgmt_free(struct hinic_hwdev *hwdev);

int hinic_pf_to_mgmt_sync(void *hwdev, enum hinic_mod_type mod, u8 cmd,
			  void *buf_in, u16 in_size, void *buf_out,
			  u16 *out_size, u32 timeout);

int hinic_pf_to_mgmt_async(void *hwdev, enum hinic_mod_type mod,
			   u8 cmd, void *buf_in, u16 in_size);

int hinic_pf_clp_to_mgmt(void *hwdev, enum hinic_mod_type mod, u8 cmd,
			 const void *buf_in, u16 in_size,
			 void *buf_out, u16 *out_size);

int hinic_clp_pf_to_mgmt_init(struct hinic_hwdev *hwdev);
void hinic_clp_pf_to_mgmt_free(struct hinic_hwdev *hwdev);

#endif
