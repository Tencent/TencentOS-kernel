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

#ifndef HINIC_NICTOOL_H_
#define HINIC_NICTOOL_H_

#include "hinic_dfx_def.h"
#ifndef IFNAMSIZ
#define IFNAMSIZ    16
#endif
/* completion timeout interval, unit is jiffies*/
#define UP_COMP_TIME_OUT_VAL		10000U

struct sm_in_st {
	int node;
	int id;
	int instance;
};

struct sm_out_st {
	u64 val1;
	u64 val2;
};

struct up_log_msg_st {
	u32 rd_len;
	u32 addr;
};

struct csr_write_st {
	u32 rd_len;
	u32 addr;
	u8 *data;
};

struct ipsurx_stats_info {
	u32 addr;
	u32 rd_cnt;
};

struct ucode_cmd_st {
	union {
		struct {
			u32 comm_mod_type:8;
			u32 ucode_cmd_type:4;
			u32 cmdq_ack_type:3;
			u32 ucode_imm:1;
			u32 len:16;
		} ucode_db;
		u32 value;
	};
};

struct up_cmd_st {
	union {
		struct {
			u32 comm_mod_type:8;
			u32 chipif_cmd:8;
			u32 up_api_type:16;
		} up_db;
		u32 value;
	};
};

struct _dcb_data {
	u8 wr_flag;
	u8 dcb_en;
	u8 err;
	u8 rsvd;
};

union _dcb_ctl {
	struct _dcb_data dcb_data;
	u32 data;
};

struct _pfc_data {
	u8 pfc_en;
	u8 pfc_priority;
	u8 num_of_tc;
	u8 err;
};

union _pfc {
	struct _pfc_data pfc_data;
	u32 data;
};

union _flag_com {
	struct _ets_flag {
		u8 flag_ets_enable:1;
		u8 flag_ets_percent:1;
		u8 flag_ets_cos:1;
		u8 flag_ets_strict:1;
		u8 rev:4;
	} ets_flag;
	u8 data;
};

struct _ets {
	u8 ets_en;
	u8 err;
	u8 strict;
	u8 tc[8];
	u8 ets_percent[8];
	union _flag_com flag_com;
};

#define API_CMD 0x1
#define API_CHAIN 0x2
#define API_CLP 0x3

struct msg_module {
	char device_name[IFNAMSIZ];
	unsigned int module;
	union {
		u32 msg_formate;
		struct ucode_cmd_st ucode_cmd;
		struct up_cmd_st up_cmd;
	};

	struct {
		u32 inBuffLen;
		u32 outBuffLen;
	} lenInfo;
	u32 res;
	void *in_buff;
	void *out_buf;
};

#define MAX_VER_INFO_LEN 128
struct drv_version_info {
	char ver[MAX_VER_INFO_LEN];
};

struct chip_fault_stats {
	int offset;
	u8 chip_faults[MAX_DRV_BUF_SIZE];
};

struct hinic_wqe_info {
	int q_id;
	void *slq_handle;
	unsigned int wqe_id;
};

struct hinic_cos_up_map {
	u8 cos_up[HINIC_DCB_UP_MAX];
	u8 num_cos;
};

struct hinic_tx_hw_page {
	u64     phy_addr;
	u64	*map_addr;
};

struct hinic_dbg_sq_info {
	u16	q_id;
	u16	pi;
	u16	ci;/* sw_ci */
	u16	fi;/* hw_ci */

	u32	q_depth;
	u16	pi_reverse;/* TODO: what is this? */
	u16	weqbb_size;

	u8	priority;
	u16	*ci_addr;
	u64	cla_addr;

	void	*slq_handle;

	/* TODO: NIC don't use direct wqe */
	struct hinic_tx_hw_page	direct_wqe;
	struct hinic_tx_hw_page	db_addr;
	u32	pg_idx;

	u32	glb_sq_id;
};

struct hinic_dbg_rq_info {
	u16	q_id;
	u16	glb_rq_id;
	u16	hw_pi;
	u16	ci;	/* sw_ci */
	u16	sw_pi;
	u16	wqebb_size;
	u16	q_depth;
	u16	buf_len;

	void	*slq_handle;
	u64	ci_wqe_page_addr;
	u64	ci_cla_tbl_addr;

	u16	msix_idx;
	u32	msix_vector;
};

#ifndef BUSINFO_LEN
#define BUSINFO_LEN (32)
#endif
struct pf_info {
	char name[IFNAMSIZ];
	char bus_info[BUSINFO_LEN];
	u32 pf_type;
};

#ifndef MAX_SIZE
#define MAX_SIZE (16)
#endif
struct card_info {
	struct pf_info pf[MAX_SIZE];
	u32 pf_num;
};

struct nic_card_id {
	u32 id[MAX_SIZE];
	u32 num;
};

struct func_pdev_info {
	u64 bar0_phy_addr;
	u64 bar0_size;
	u64 rsvd1[4];
};

struct hinic_card_func_info {
	u32 num_pf;
	u32 rsvd0;
	u64 usr_api_phy_addr;
	struct func_pdev_info pdev_info[MAX_SIZE];
};

#ifndef NIC_UP_CMD_UPDATE_FW
#define NIC_UP_CMD_UPDATE_FW (114)
#endif

#ifndef MAX_CARD_NUM
#define MAX_CARD_NUM (64)
#endif
extern void *g_card_node_array[MAX_CARD_NUM];
extern void *g_card_vir_addr[MAX_CARD_NUM];
extern u64 g_card_phy_addr[MAX_CARD_NUM];
extern struct mutex	g_addr_lock;
extern int card_id;

struct hinic_nic_loop_mode {
	u32 loop_mode;
	u32 loop_ctrl;
};

struct hinic_nic_poll_weight {
	int poll_weight;
};

enum hinic_homologues_state {
	HINIC_HOMOLOGUES_OFF = 0,
	HINIC_HOMOLOGUES_ON  = 1,
};

struct hinic_homologues {
	enum hinic_homologues_state homo_state;
};

struct hinic_pf_info {
	u32 isvalid;
	u32 pf_id;
};

int nictool_k_init(void);
void nictool_k_uninit(void);

extern u32 hinic_get_io_stats_size(struct hinic_nic_dev *nic_dev);
extern void hinic_get_io_stats(struct hinic_nic_dev *nic_dev,
			       struct hinic_show_item *items);

#define TOOL_COUNTER_MAX_LEN			512

#endif

