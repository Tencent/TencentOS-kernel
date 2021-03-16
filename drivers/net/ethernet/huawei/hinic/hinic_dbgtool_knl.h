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

#ifndef __DBGTOOL_KNL_H__
#define __DBGTOOL_KNL_H__

#define DBG_TOOL_MAGIC 'w'

/* dbgtool command type */
/* You can add the required dbgtool through these commands
 * can invoke all X86 kernel mode driver interface
 */
enum dbgtool_cmd {
	DBGTOOL_CMD_API_RD = 0,
	DBGTOOL_CMD_API_WR,

	DBGTOOL_CMD_FFM_RD,
	DBGTOOL_CMD_FFM_CLR,

	DBGTOOL_CMD_PF_DEV_INFO_GET,

	DBGTOOL_CMD_MSG_2_UP,

	DBGTOOL_CMD_FREE_MEM,
	DBGTOOL_CMD_NUM
};

struct api_cmd_rd {
	u32 pf_id;
	u8  dest;
	u8 *cmd;
	u16 size;
	void *ack;
	u16 ack_size;
};

struct api_cmd_wr {
	u32 pf_id;
	u8  dest;
	u8 *cmd;
	u16 size;
};

struct pf_dev_info {
	u64 bar0_size;
	u8 bus;
	u8 slot;
	u8 func;
	u64 phy_addr;
};

/* Interrupt at most records, interrupt will be recorded in the FFM */
#define FFM_RECORD_NUM_MAX 64

struct ffm_intr_tm_info {
	u8 node_id;
	/* error level of the interrupt source */
	u8 err_level;
	/* Classification by interrupt source properties */
	u16 err_type;
	u32 err_csr_addr;
	u32 err_csr_value;

	u8 sec;		/* second*/
	u8 min;		/* minute */
	u8 hour;	/* hour */
	u8 mday;	/* day */
	u8 mon;		/* month */
	u16 year;	/* year */
};

struct ffm_record_info {
	u32 ffm_num;
	struct ffm_intr_tm_info ffm[FFM_RECORD_NUM_MAX];
};

struct msg_2_up {
	u8 pf_id;   /* which pf sends messages to the up */
	u8 mod;
	u8 cmd;
	void *buf_in;
	u16 in_size;
	void *buf_out;
	u16 *out_size;
};

struct dbgtool_param {
	union {
		struct api_cmd_rd api_rd;
		struct api_cmd_wr api_wr;
		struct pf_dev_info *dev_info;
		struct ffm_record_info *ffm_rd;
		struct msg_2_up msg2up;
	} param;
	char chip_name[16];
};

#ifndef MAX_CARD_NUM
#define MAX_CARD_NUM 64
#endif
#define DBGTOOL_PAGE_ORDER 10

int dbgtool_knl_init(void *vhwdev, void *chip_node);
void dbgtool_knl_deinit(void *vhwdev, void *chip_node);
int hinic_mem_mmap(struct file *filp, struct vm_area_struct *vma);
void chipif_get_all_pf_dev_info(struct pf_dev_info *dev_info, int card_id,
				void **g_func_handle_array);
long dbgtool_knl_free_mem(int id);

#endif
