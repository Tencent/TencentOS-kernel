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
#include <linux/delay.h>
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
#include "hinic_msix_attr.h"
#include "hinic_nic_io.h"
#include "hinic_eqs.h"
#include "hinic_api_cmd.h"
#include "hinic_mgmt.h"
#include "hinic_mbox.h"
#include "hinic_wq.h"
#include "hinic_cmdq.h"
#include "hinic_nic_cfg.h"
#include "hinic_hwif.h"
#include "hinic_mgmt_interface.h"
#include "hinic_multi_host_mgmt.h"


#define HINIC_DEAULT_EQ_MSIX_PENDING_LIMIT	0
#define HINIC_DEAULT_EQ_MSIX_COALESC_TIMER_CFG	0xFF
#define HINIC_DEAULT_EQ_MSIX_RESEND_TIMER_CFG	7

#define HINIC_WAIT_IO_STATUS_TIMEOUT	100

#define HINIC_FLR_TIMEOUT		1000

#define HINIC_HT_GPA_PAGE_SIZE 4096UL

#define HINIC_PPF_HT_GPA_SET_RETRY_TIMES 10

#define HINIC_OK_FLAG_OK			0

#define HINIC_OK_FLAG_FAILED		1

#define HINIC_GET_SFP_INFO_REAL_TIME	0x1

#define HINIC_GLB_SO_RO_CFG_SHIFT	0x0
#define HINIC_GLB_SO_RO_CFG_MASK	0x1
#define HINIC_DISABLE_ORDER		0
#define HINIC_GLB_DMA_SO_RO_GET(val, member)	\
	(((val) >> HINIC_GLB_##member##_SHIFT) & HINIC_GLB_##member##_MASK)

#define HINIC_GLB_DMA_SO_R0_CLEAR(val, member) \
	((val) & (~(HINIC_GLB_##member##_MASK << HINIC_GLB_##member##_SHIFT)))

#define HINIC_GLB_DMA_SO_R0_SET(val, member) \
	(((val) & HINIC_GLB_##member##_MASK) << HINIC_GLB_##member##_SHIFT)

#define HINIC_MGMT_CHANNEL_STATUS_SHIFT	0x0
#define HINIC_MGMT_CHANNEL_STATUS_MASK	0x1
#define HINIC_ACTIVE_STATUS_MASK 0x80000000
#define HINIC_ACTIVE_STATUS_CLEAR 0x7FFFFFFF

#define HINIC_GET_MGMT_CHANNEL_STATUS(val, member)	\
	(((val) >> HINIC_##member##_SHIFT) & HINIC_##member##_MASK)

#define HINIC_CLEAR_MGMT_CHANNEL_STATUS(val, member)	\
	((val) & (~(HINIC_##member##_MASK << HINIC_##member##_SHIFT)))

#define HINIC_SET_MGMT_CHANNEL_STATUS(val, member)	\
	(((val) & HINIC_##member##_MASK) << HINIC_##member##_SHIFT)

#define HINIC_BOARD_IS_PHY(hwdev)			\
		((hwdev)->board_info.board_type == 4 && \
		 (hwdev)->board_info.board_id == 24)

struct comm_info_ht_gpa_set {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u32 rsvd1;
	u32 rsvd2;

	u64 page_pa0;
	u64 page_pa1;
};

struct comm_info_eqm_fix {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u32 chunk_num;
	u32 search_gpa_num;
};

struct comm_info_eqm_cfg {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u32 ppf_id;
	u32 page_size;
	u32 valid;
};

struct comm_info_eqm_search_gpa {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u32 start_idx;
	u32 num;
	u32 resv0;
	u32 resv1;
	u64 gpa_hi52[0];
};

struct hinic_cons_idx_attr {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_idx;
	u8	dma_attr_off;
	u8	pending_limit;
	u8	coalescing_time;
	u8	intr_en;
	u16	intr_idx;
	u32	l2nic_sqn;
	u32	sq_id;
	u64	ci_addr;
};

struct hinic_clear_doorbell {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_idx;
	u8	ppf_idx;
	u8	rsvd1;
};

struct hinic_clear_resource {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_idx;
	u8	ppf_idx;
	u8	rsvd1;
};

struct hinic_msix_config {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	msix_index;
	u8	pending_cnt;
	u8	coalesct_timer_cnt;
	u8	lli_tmier_cnt;
	u8	lli_credit_cnt;
	u8	resend_timer_cnt;
	u8	rsvd1[3];
};

enum func_tmr_bitmap_status {
	FUNC_TMR_BITMAP_DISABLE,
	FUNC_TMR_BITMAP_ENABLE,
};

struct hinic_func_tmr_bitmap_op {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_idx;
	u8	op_id;   /* 0:start; 1:stop */
	u8	ppf_idx;
	u32	rsvd1;
};

struct hinic_ppf_tmr_op {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	ppf_idx;
	u8	op_id;   /* 0: stop timer; 1:start timer */
	u8	rsvd1[2];
	u32	rsvd2;
};

struct hinic_cmd_set_res_state {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_idx;
	u8	state;
	u8	rsvd1;
	u32	rsvd2;
};

int hinic_hw_rx_buf_size[] = {
	HINIC_RX_BUF_SIZE_32B,
	HINIC_RX_BUF_SIZE_64B,
	HINIC_RX_BUF_SIZE_96B,
	HINIC_RX_BUF_SIZE_128B,
	HINIC_RX_BUF_SIZE_192B,
	HINIC_RX_BUF_SIZE_256B,
	HINIC_RX_BUF_SIZE_384B,
	HINIC_RX_BUF_SIZE_512B,
	HINIC_RX_BUF_SIZE_768B,
	HINIC_RX_BUF_SIZE_1K,
	HINIC_RX_BUF_SIZE_1_5K,
	HINIC_RX_BUF_SIZE_2K,
	HINIC_RX_BUF_SIZE_3K,
	HINIC_RX_BUF_SIZE_4K,
	HINIC_RX_BUF_SIZE_8K,
	HINIC_RX_BUF_SIZE_16K,
};

/* vf-pf dma attr table */
struct hinic_vf_dma_attr_table {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_idx;
	u8	func_dma_entry_num;
	u8	entry_idx;
	u8	st;
	u8	at;
	u8	ph;
	u8	no_snooping;
	u8	tph_en;
	u8	resv1[3];
};

struct hinic_led_info {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	port;
	u8	type;
	u8	mode;
	u8	reset;
};

struct hinic_comm_board_info {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	struct hinic_board_info info;

	u32	rsvd1[4];
};

#define PHY_DOING_INIT_TIMEOUT	(15 * 1000)

struct hinic_phy_init_status {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	init_status;
	u8	rsvd1[3];
};

enum phy_init_status_type {
	PHY_INIT_DOING = 0,
	PHY_INIT_SUCCESS = 1,
	PHY_INIT_FAIL = 2,
	PHY_NONSUPPORT = 3,
};

struct hinic_update_active {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u32 update_flag;
	u32 update_status;
};

enum hinic_bios_cfg_op_code {
	HINIC_BIOS_CFG_GET = 0,
	HINIC_BIOS_CFG_PF_BW_LIMIT = 0x1 << 6,
};

struct hinic_bios_cfg_cmd {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u32 op_code;
	u32 signature;

	u8 rsvd1[12];
	u32 pf_bw_limit;
	u8 rsvd2[5];

	u8 func_valid;
	u8 func_idx;
	u8 rsvd3;
};

struct hinic_mgmt_watchdog_info {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u32 curr_time_h;
	u32 curr_time_l;
	u32 task_id;
	u32 rsv;

	u32 reg[13];
	u32 pc;
	u32 lr;
	u32 cpsr;

	u32 stack_top;
	u32 stack_bottom;
	u32 sp;
	u32 curr_used;
	u32 peak_used;
	u32 is_overflow;

	u32 stack_actlen;
	u8 data[1024];
};

struct hinic_fmw_act_ntc {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u32 rsvd1[5];
};

struct hinic_ppf_state {
	u8	status;
	u8	version;
	u8	rsvd0[6];
	u8	ppf_state;
	u8	rsvd1[3];
};

#define HINIC_PAGE_SIZE_HW(pg_size)	((u8)ilog2((u32)((pg_size) >> 12)))

struct hinic_wq_page_size {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_idx;
	u8	ppf_idx;
	/* real_size=4KB*2^page_size, range(0~20) must be checked by driver */
	u8	page_size;

	u32	rsvd1;
};

#define MAX_PCIE_DFX_BUF_SIZE (1024)

struct hinic_pcie_dfx_ntc {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	int len;
	u32 rsvd;
};

struct hinic_pcie_dfx_info {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u8 host_id;
	u8 last;
	u8 rsvd[2];
	u32 offset;

	u8 data[MAX_PCIE_DFX_BUF_SIZE];
};

struct hinic_hw_pf_infos_cmd {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	struct hinic_hw_pf_infos infos;
};

enum hinic_sdi_mode_ops {
	HINIC_SDI_INFO_SET		= 1U << 0,	/* 1-save, 0-read */
	HINIC_SDI_INFO_MODE		= 1U << 1,
};

struct hinic_sdi_mode_info {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	/* Op-Code:
	 * Bit0: 0 - read configuration, 1 - write configuration
	 * Bit1: 0 - ignored, 1 - get/set SDI Mode
	 */
	u32	opcode;
	u32	signature;
	u16	cur_sdi_mode;
	u16	cfg_sdi_mode;

	u32	rsvd1[29];
};

struct hinic_reg_info {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u32	reg_addr;
	u32	val_length;

	u32	data[2];
};

#define HINIC_DMA_ATTR_ENTRY_ST_SHIFT				0
#define HINIC_DMA_ATTR_ENTRY_AT_SHIFT				8
#define HINIC_DMA_ATTR_ENTRY_PH_SHIFT				10
#define HINIC_DMA_ATTR_ENTRY_NO_SNOOPING_SHIFT			12
#define HINIC_DMA_ATTR_ENTRY_TPH_EN_SHIFT			13

#define HINIC_DMA_ATTR_ENTRY_ST_MASK				0xFF
#define HINIC_DMA_ATTR_ENTRY_AT_MASK				0x3
#define HINIC_DMA_ATTR_ENTRY_PH_MASK				0x3
#define HINIC_DMA_ATTR_ENTRY_NO_SNOOPING_MASK			0x1
#define HINIC_DMA_ATTR_ENTRY_TPH_EN_MASK			0x1

#define HINIC_DMA_ATTR_ENTRY_SET(val, member)			\
		(((u32)(val) & HINIC_DMA_ATTR_ENTRY_##member##_MASK) << \
			HINIC_DMA_ATTR_ENTRY_##member##_SHIFT)

#define HINIC_DMA_ATTR_ENTRY_CLEAR(val, member)		\
		((val) & (~(HINIC_DMA_ATTR_ENTRY_##member##_MASK	\
			<< HINIC_DMA_ATTR_ENTRY_##member##_SHIFT)))

#define HINIC_PCIE_ST_DISABLE			0
#define HINIC_PCIE_AT_DISABLE			0
#define HINIC_PCIE_PH_DISABLE			0

#define PCIE_MSIX_ATTR_ENTRY			0

struct hinic_cmd_fault_event {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	struct hinic_fault_event event;
};

#define HEARTBEAT_DRV_MAGIC_ACK	0x5A5A5A5A

struct hinic_heartbeat_support {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	ppf_id;
	u8	pf_issupport;
	u8	mgmt_issupport;
	u8	rsvd1[5];
};

struct hinic_heartbeat_event {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	mgmt_init_state;
	u8	rsvd1[3];
	u32	heart;		/* increased every event */
	u32	drv_heart;
};

static void hinic_enable_mgmt_channel(void *hwdev, void *buf_out);
static void hinic_set_mgmt_channel_status(void *handle, bool state);
static inline void __set_heartbeat_ehd_detect_delay(struct hinic_hwdev *hwdev,
						    u32 delay_ms);

#define HINIC_QUEUE_MIN_DEPTH		6
#define HINIC_QUEUE_MAX_DEPTH		12
#define HINIC_MAX_RX_BUFFER_SIZE	15

#define CAP_INFO_MAC_LEN		512
#define VENDOR_MAX_LEN			17

static bool check_root_ctxt(struct hinic_hwdev *hwdev, u16 func_idx,
			    void *buf_in, u16 in_size)
{
	struct hinic_root_ctxt *root_ctxt;

	if (!hinic_mbox_check_func_id_8B(hwdev, func_idx, buf_in, in_size))
		return false;

	root_ctxt = (struct hinic_root_ctxt *)buf_in;

	if (root_ctxt->ppf_idx != HINIC_HWIF_PPF_IDX(hwdev->hwif))
		return false;

	if (root_ctxt->set_cmdq_depth) {
		if (root_ctxt->cmdq_depth >= HINIC_QUEUE_MIN_DEPTH &&
		    root_ctxt->cmdq_depth <= HINIC_QUEUE_MAX_DEPTH)
			return true;

		return false;
	}

	if (root_ctxt->rq_depth >= HINIC_QUEUE_MIN_DEPTH &&
	    root_ctxt->rq_depth <= HINIC_QUEUE_MAX_DEPTH &&
	    root_ctxt->sq_depth >= HINIC_QUEUE_MIN_DEPTH &&
	    root_ctxt->sq_depth <= HINIC_QUEUE_MAX_DEPTH &&
	    root_ctxt->rx_buf_sz <= HINIC_MAX_RX_BUFFER_SIZE)
		return true;

	if (!root_ctxt->rq_depth && !root_ctxt->sq_depth &&
	    !root_ctxt->rx_buf_sz)
		return true;

	return false;
}

static bool check_cmdq_ctxt(struct hinic_hwdev *hwdev, u16 func_idx,
			    void *buf_in, u16 in_size)
{
	if (!hinic_mbox_check_func_id_8B(hwdev, func_idx, buf_in, in_size))
		return false;

	return hinic_cmdq_check_vf_ctxt(hwdev, buf_in);
}

static bool check_set_wq_page_size(struct hinic_hwdev *hwdev, u16 func_idx,
				   void *buf_in, u16 in_size)
{
	struct hinic_wq_page_size *page_size_info = buf_in;

	if (!hinic_mbox_check_func_id_8B(hwdev, func_idx, buf_in, in_size))
		return false;

	if (page_size_info->ppf_idx != hinic_ppf_idx(hwdev))
		return false;

	if (((1U << page_size_info->page_size) * 0x1000) !=
	    HINIC_DEFAULT_WQ_PAGE_SIZE)
		return false;

	return true;
}

static bool __mbox_check_tmr_bitmap(struct hinic_hwdev *hwdev, u16 func_idx,
				    void *buf_in, u16 in_size)
{
	struct hinic_func_tmr_bitmap_op *bitmap_op = buf_in;

	if (!hinic_mbox_check_func_id_8B(hwdev, func_idx, buf_in, in_size))
		return false;

	if (bitmap_op->op_id == FUNC_TMR_BITMAP_ENABLE) {
		if (!hinic_get_ppf_status(hwdev)) {
			sdk_err(hwdev->dev_hdl, "PPF timer is not init, can't enable %d timer bitmap\n",
				func_idx);
			return false;
		}
	}

	if (bitmap_op->ppf_idx != hinic_ppf_idx(hwdev))
		return false;

	return true;
}

struct vf_cmd_check_handle hw_cmd_support_vf[] = {
	{HINIC_MGMT_CMD_START_FLR, hinic_mbox_check_func_id_8B},
	{HINIC_MGMT_CMD_DMA_ATTR_SET, hinic_mbox_check_func_id_8B},
	{HINIC_MGMT_CMD_CMDQ_CTXT_SET, check_cmdq_ctxt},
	{HINIC_MGMT_CMD_CMDQ_CTXT_GET, check_cmdq_ctxt},
	{HINIC_MGMT_CMD_VAT_SET, check_root_ctxt},
	{HINIC_MGMT_CMD_VAT_GET, check_root_ctxt},
	{HINIC_MGMT_CMD_L2NIC_SQ_CI_ATTR_SET, hinic_mbox_check_func_id_8B},
	{HINIC_MGMT_CMD_L2NIC_SQ_CI_ATTR_GET, hinic_mbox_check_func_id_8B},
	{HINIC_MGMT_CMD_RES_STATE_SET, hinic_mbox_check_func_id_8B},

	{HINIC_MGMT_CMD_CEQ_CTRL_REG_WR_BY_UP, hinic_mbox_check_func_id_8B},
	{HINIC_MGMT_CMD_MSI_CTRL_REG_WR_BY_UP, hinic_mbox_check_func_id_8B},
	{HINIC_MGMT_CMD_MSI_CTRL_REG_RD_BY_UP, hinic_mbox_check_func_id_8B},

	{HINIC_MGMT_CMD_L2NIC_RESET, hinic_mbox_check_func_id_8B},
	{HINIC_MGMT_CMD_FAST_RECYCLE_MODE_SET, hinic_mbox_check_func_id_8B},
	{HINIC_MGMT_CMD_PAGESIZE_SET, check_set_wq_page_size},
	{HINIC_MGMT_CMD_PAGESIZE_GET, hinic_mbox_check_func_id_8B},
	{HINIC_MGMT_CMD_GET_PPF_STATE, NULL},
	{HINIC_MGMT_CMD_FUNC_TMR_BITMAT_SET, __mbox_check_tmr_bitmap},
	{HINIC_MGMT_CMD_GET_BOARD_INFO, NULL},
	{HINIC_MGMT_CMD_GET_SDI_MODE, NULL},
};

struct hinic_mgmt_status_log {
	u8 status;
	const char *log;
};

struct hinic_mgmt_status_log mgmt_status_log[] = {
	{HINIC_MGMT_STATUS_ERR_PARAM, "Invalid parameter"},
	{HINIC_MGMT_STATUS_ERR_FAILED, "Operation failed"},
	{HINIC_MGMT_STATUS_ERR_PORT, "Invalid port"},
	{HINIC_MGMT_STATUS_ERR_TIMEOUT, "Operation time out"},
	{HINIC_MGMT_STATUS_ERR_NOMATCH, "Version not match"},
	{HINIC_MGMT_STATUS_ERR_EXIST, "Entry exists"},
	{HINIC_MGMT_STATUS_ERR_NOMEM, "Out of memory"},
	{HINIC_MGMT_STATUS_ERR_INIT, "Feature not initialized"},
	{HINIC_MGMT_STATUS_ERR_FAULT, "Invalid address"},
	{HINIC_MGMT_STATUS_ERR_PERM, "Operation not permitted"},
	{HINIC_MGMT_STATUS_ERR_EMPTY, "Table empty"},
	{HINIC_MGMT_STATUS_ERR_FULL, "Table full"},
	{HINIC_MGMT_STATUS_ERR_NOT_FOUND, "Not found"},
	{HINIC_MGMT_STATUS_ERR_BUSY, "Device or resource busy "},
	{HINIC_MGMT_STATUS_ERR_RESOURCE, "No resources for operation "},
	{HINIC_MGMT_STATUS_ERR_CONFIG, "Invalid configuration"},
	{HINIC_MGMT_STATUS_ERR_UNAVAIL, "Feature unavailable"},
	{HINIC_MGMT_STATUS_ERR_CRC, "CRC check failed"},
	{HINIC_MGMT_STATUS_ERR_NXIO, "No such device or address"},
	{HINIC_MGMT_STATUS_ERR_ROLLBACK, "Chip rollback fail"},
	{HINIC_MGMT_STATUS_ERR_LEN, "Length too short or too long"},
	{HINIC_MGMT_STATUS_ERR_UNSUPPORT, "Feature not supported"},
};

static void __print_status_info(struct hinic_hwdev *dev,
				enum hinic_mod_type mod, u8 cmd, int index)
{
	if (mod == HINIC_MOD_COMM) {
		sdk_err(dev->dev_hdl, "Mgmt process mod(0x%x) cmd(0x%x) fail: %s",
			mod, cmd, mgmt_status_log[index].log);
	} else if (mod == HINIC_MOD_L2NIC ||
		   mod == HINIC_MOD_HILINK) {
		if (HINIC_IS_VF(dev) && (cmd == HINIC_PORT_CMD_SET_MAC || cmd ==
		    HINIC_PORT_CMD_DEL_MAC || cmd ==
		    HINIC_PORT_CMD_UPDATE_MAC) &&
		    (mgmt_status_log[index].status == HINIC_PF_SET_VF_ALREADY))
			return;

		nic_err(dev->dev_hdl, "Mgmt process mod(0x%x) cmd(0x%x) fail: %s",
			mod, cmd, mgmt_status_log[index].log);
	}
}

static bool hinic_status_need_special_handle(enum hinic_mod_type mod,
					     u8 cmd, u8 status)
{
	if (mod == HINIC_MOD_L2NIC) {
		/* optical module isn't plugged in */
		if (((cmd == HINIC_PORT_CMD_GET_STD_SFP_INFO) ||
		     (cmd == HINIC_PORT_CMD_GET_SFP_INFO)) &&
		     (status == HINIC_MGMT_STATUS_ERR_NXIO))
			return true;

		if ((cmd == HINIC_PORT_CMD_SET_MAC ||
		     cmd == HINIC_PORT_CMD_UPDATE_MAC) &&
		     status == HINIC_MGMT_STATUS_ERR_EXIST)
			return true;
	}

	return false;
}

static void hinic_print_status_info(void *hwdev, enum hinic_mod_type mod,
				    u8 cmd, const void *buf_out)
{
	struct hinic_hwdev *dev = hwdev;
	int i, size;
	u8 status;

	if (!buf_out)
		return;

	if (mod != HINIC_MOD_COMM && mod != HINIC_MOD_L2NIC &&
	    mod != HINIC_MOD_HILINK)
		return;

	status = *(u8 *)buf_out;

	if (!status)
		return;

	if (hinic_status_need_special_handle(mod, cmd, status))
		return;

	size = sizeof(mgmt_status_log) / sizeof(mgmt_status_log[0]);
	for (i = 0; i < size; i++) {
		if (status == mgmt_status_log[i].status) {
			__print_status_info(dev, mod, cmd, i);
			return;
		}
	}

	if (mod == HINIC_MOD_COMM) {
		sdk_err(dev->dev_hdl, "Mgmt process mod(0x%x) cmd(0x%x) return driver unknown status(0x%x)\n",
			mod, cmd, status);
	} else if (mod == HINIC_MOD_L2NIC || mod == HINIC_MOD_HILINK) {
		nic_err(dev->dev_hdl, "Mgmt process mod(0x%x) cmd(0x%x) return driver unknown status(0x%x)\n",
			mod, cmd, status);
	}
}

void hinic_set_chip_present(void *hwdev)
{
	((struct hinic_hwdev *)hwdev)->chip_present_flag = HINIC_CHIP_PRESENT;
}

void hinic_set_chip_absent(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	sdk_err(dev->dev_hdl, "Card not present\n");
	dev->chip_present_flag = HINIC_CHIP_ABSENT;
}

int hinic_get_chip_present_flag(void *hwdev)
{
	int flag;

	if (!hwdev)
		return -EINVAL;
	flag = ((struct hinic_hwdev *)hwdev)->chip_present_flag;
	return flag;
}
EXPORT_SYMBOL(hinic_get_chip_present_flag);

static void hinic_set_fast_recycle_status(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	sdk_err(dev->dev_hdl, "Enter fast recycle status\n");
	dev->chip_present_flag = HINIC_CHIP_ABSENT;
}

void hinic_force_complete_all(void *hwdev)
{
	struct hinic_hwdev *dev = (struct hinic_hwdev *)hwdev;
	struct hinic_recv_msg *recv_resp_msg;

	set_bit(HINIC_HWDEV_STATE_BUSY, &dev->func_state);

	if (hinic_func_type(dev) != TYPE_VF &&
	    hinic_is_hwdev_mod_inited(dev, HINIC_HWDEV_MGMT_INITED)) {
		recv_resp_msg = &dev->pf_to_mgmt->recv_resp_msg_from_mgmt;
		if (dev->pf_to_mgmt->event_flag == SEND_EVENT_START) {
			complete(&recv_resp_msg->recv_done);
			dev->pf_to_mgmt->event_flag = SEND_EVENT_TIMEOUT;
		}
	}

	/* only flush sync cmdq to avoid blocking remove */
	if (hinic_is_hwdev_mod_inited(dev, HINIC_HWDEV_CMDQ_INITED))
		hinic_cmdq_flush_cmd(hwdev,
				     &(dev->cmdqs->cmdq[HINIC_CMDQ_SYNC]));

	clear_bit(HINIC_HWDEV_STATE_BUSY, &dev->func_state);
}

void hinic_detect_hw_present(void *hwdev)
{
	u32 addr, attr1;

	addr = HINIC_CSR_FUNC_ATTR1_ADDR;
	attr1 = hinic_hwif_read_reg(((struct hinic_hwdev *)hwdev)->hwif, addr);
	if (attr1 == HINIC_PCIE_LINK_DOWN) {
		hinic_set_chip_absent(hwdev);
		hinic_force_complete_all(hwdev);
	}
}

void hinic_record_pcie_error(void *hwdev)
{
	struct hinic_hwdev *dev = (struct hinic_hwdev *)hwdev;

	if (!hwdev)
		return;

	atomic_inc(&dev->hw_stats.fault_event_stats.pcie_fault_stats);
}

static int __func_send_mbox(struct hinic_hwdev *hwdev, enum hinic_mod_type mod,
			    u8 cmd, void *buf_in, u16 in_size, void *buf_out,
			    u16 *out_size, u32 timeout)
{
	int err;

	if (hinic_func_type(hwdev) == TYPE_VF)
		err = hinic_mbox_to_pf(hwdev, mod, cmd, buf_in,
				       in_size, buf_out,
				       out_size, timeout);
	else if (NEED_MBOX_FORWARD(hwdev))
		err = hinic_mbox_to_host_sync(hwdev, mod, cmd, buf_in,
					      in_size, buf_out, out_size,
					      timeout);
	else
		err = -EFAULT;

	return err;
}

static int __pf_to_mgmt_pre_handle(struct hinic_hwdev *hwdev,
				   enum hinic_mod_type mod, u8 cmd)
{
	if (hinic_get_mgmt_channel_status(hwdev)) {
		if (mod == HINIC_MOD_COMM || mod == HINIC_MOD_L2NIC ||
		    mod == HINIC_MOD_CFGM || mod == HINIC_MOD_HILINK)
			return HINIC_DEV_BUSY_ACTIVE_FW;
		else
			return -EBUSY;
	}

	/* Set channel invalid, don't allowed to send other cmd */
	if (mod == HINIC_MOD_COMM && cmd == HINIC_MGMT_CMD_ACTIVATE_FW) {
		hinic_set_mgmt_channel_status(hwdev, true);
		/* stop heartbeat enhanced detection temporary, and will
		 * restart in firmware active event when mgmt is resetted
		 */
		__set_heartbeat_ehd_detect_delay(hwdev,
						 HINIC_DEV_ACTIVE_FW_TIMEOUT);
	}

	return 0;
}

static void __pf_to_mgmt_after_handle(struct hinic_hwdev *hwdev,
				      enum hinic_mod_type mod, u8 cmd,
				      int sw_status, void *mgmt_status)
{
	/* if activate fw is failed, set channel valid */
	if (mod == HINIC_MOD_COMM &&
	    cmd == HINIC_MGMT_CMD_ACTIVATE_FW) {
		if (sw_status)
			hinic_set_mgmt_channel_status(hwdev, false);
		else
			hinic_enable_mgmt_channel(hwdev, mgmt_status);
	}
}

int hinic_pf_msg_to_mgmt_sync(void *hwdev, enum hinic_mod_type mod, u8 cmd,
			      void *buf_in, u16 in_size,
			      void *buf_out, u16 *out_size, u32 timeout)
{
	struct hinic_hwdev *dev = hwdev;
	int err;

	if (!hwdev)
		return -EINVAL;

	if (!((struct hinic_hwdev *)hwdev)->chip_present_flag)
		return -EPERM;

	if (NEED_MBOX_FORWARD(dev)) {
		if (!hinic_is_hwdev_mod_inited(hwdev,
					       HINIC_HWDEV_MBOX_INITED)) {
			return -EPERM;
		}

		err = __func_send_mbox(hwdev, mod, cmd, buf_in, in_size,
				       buf_out, out_size, timeout);
	} else {
		if (!hinic_is_hwdev_mod_inited(hwdev, HINIC_HWDEV_MGMT_INITED))
			return -EPERM;

		if (in_size > HINIC_MSG_TO_MGMT_MAX_LEN)
			return -EINVAL;

		err = __pf_to_mgmt_pre_handle(hwdev, mod, cmd);
		if (err)
			return err;

		err = hinic_pf_to_mgmt_sync(hwdev, mod, cmd, buf_in, in_size,
					    buf_out, out_size, timeout);
		__pf_to_mgmt_after_handle(hwdev, mod, cmd, err, buf_out);
	}

	return err;
}

static bool is_sfp_info_cmd_cached(struct hinic_hwdev *hwdev,
				   enum hinic_mod_type mod, u8 cmd,
				   void *buf_in, u16 in_size,
				   void *buf_out, u16 *out_size)
{
	struct hinic_cmd_get_sfp_qsfp_info *sfp_info = NULL;
	struct hinic_port_routine_cmd *rt_cmd = NULL;
	struct card_node *chip_node = hwdev->chip_node;

	sfp_info = buf_in;
	if (sfp_info->port_id >= HINIC_MAX_PORT_ID ||
	    *out_size < sizeof(*sfp_info))
		return false;

	if (sfp_info->version == HINIC_GET_SFP_INFO_REAL_TIME)
		return false;

	rt_cmd = &chip_node->rt_cmd[sfp_info->port_id];
	mutex_lock(&chip_node->sfp_mutex);
	memcpy(buf_out, &rt_cmd->sfp_info, sizeof(*sfp_info));
	mutex_unlock(&chip_node->sfp_mutex);

	return true;
}

static bool is_sfp_abs_cmd_cached(struct hinic_hwdev *hwdev,
				  enum hinic_mod_type mod, u8 cmd,
				  void *buf_in, u16 in_size,
				  void *buf_out, u16 *out_size)
{
	struct hinic_cmd_get_light_module_abs *abs = NULL;
	struct hinic_port_routine_cmd *rt_cmd = NULL;
	struct card_node *chip_node = hwdev->chip_node;

	abs = buf_in;
	if (abs->port_id >= HINIC_MAX_PORT_ID ||
	    *out_size < sizeof(*abs))
		return false;

	if (abs->version == HINIC_GET_SFP_INFO_REAL_TIME)
		return false;

	rt_cmd = &chip_node->rt_cmd[abs->port_id];
	mutex_lock(&chip_node->sfp_mutex);
	memcpy(buf_out, &rt_cmd->abs, sizeof(*abs));
	mutex_unlock(&chip_node->sfp_mutex);

	return true;
}

static bool driver_processed_cmd(struct hinic_hwdev *hwdev,
				 enum hinic_mod_type mod, u8 cmd,
				 void *buf_in, u16 in_size,
				 void *buf_out, u16 *out_size)
{
	struct card_node *chip_node = hwdev->chip_node;

	if (mod == HINIC_MOD_L2NIC) {
		if (cmd == HINIC_PORT_CMD_GET_SFP_INFO &&
		    chip_node->rt_cmd->up_send_sfp_info) {
			return is_sfp_info_cmd_cached(hwdev, mod, cmd, buf_in,
						      in_size, buf_out,
						      out_size);
		} else if (cmd == HINIC_PORT_CMD_GET_SFP_ABS &&
			 chip_node->rt_cmd->up_send_sfp_abs) {
			return is_sfp_abs_cmd_cached(hwdev, mod, cmd, buf_in,
						     in_size, buf_out,
						     out_size);
		}
	}

	return false;
}

int hinic_msg_to_mgmt_sync(void *hwdev, enum hinic_mod_type mod, u8 cmd,
			   void *buf_in, u16 in_size,
			   void *buf_out, u16 *out_size, u32 timeout)
{
	struct hinic_hwdev *dev = hwdev;
	unsigned long end;
	int err;

	if (!hwdev)
		return -EINVAL;

	if (!(dev->chip_present_flag))
		return -EPERM;

	end = jiffies + msecs_to_jiffies(HINIC_DEV_ACTIVE_FW_TIMEOUT);
	if (hinic_func_type(hwdev) == TYPE_VF || NEED_MBOX_FORWARD(dev)) {
		if (!hinic_is_hwdev_mod_inited(hwdev, HINIC_HWDEV_MBOX_INITED))
			return -EPERM;
		do {
			if (!hinic_get_chip_present_flag(hwdev))
				break;

			err = __func_send_mbox(hwdev, mod, cmd, buf_in, in_size,
					       buf_out, out_size, timeout);
			if (err != HINIC_MBOX_PF_BUSY_ACTIVE_FW) {
				hinic_print_status_info(hwdev, mod, cmd,
							buf_out);
				return err;
			}

			msleep(1000);
		} while (time_before(jiffies, end));

		err = __func_send_mbox(hwdev, mod, cmd, buf_in, in_size,
				       buf_out, out_size, timeout);
	} else {
		if (driver_processed_cmd(hwdev, mod, cmd, buf_in, in_size,
					 buf_out, out_size))
			return 0;

		do {
			if (!hinic_get_mgmt_channel_status(hwdev) ||
			    !hinic_get_chip_present_flag(hwdev))
				break;

			msleep(1000);
		} while (time_before(jiffies, end));
		err = hinic_pf_msg_to_mgmt_sync(hwdev, mod, cmd, buf_in,
						in_size, buf_out, out_size,
						timeout);
	}

	hinic_print_status_info(hwdev, mod, cmd, buf_out);

	return err;
}
EXPORT_SYMBOL(hinic_msg_to_mgmt_sync);

/* PF/VF send msg to uP by api cmd, and return immediately */
int hinic_msg_to_mgmt_async(void *hwdev, enum hinic_mod_type mod, u8 cmd,
			    void *buf_in, u16 in_size)
{
	int err;

	if (!hwdev)
		return -EINVAL;

	if (!(((struct hinic_hwdev *)hwdev)->chip_present_flag) ||
	    !hinic_is_hwdev_mod_inited(hwdev, HINIC_HWDEV_MGMT_INITED) ||
	    hinic_get_mgmt_channel_status(hwdev))
		return -EPERM;

	if (hinic_func_type(hwdev) == TYPE_VF) {
		err = -EFAULT;
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Mailbox don't support async cmd\n");
	} else {
		err = hinic_pf_to_mgmt_async(hwdev, mod, cmd, buf_in, in_size);
	}

	return err;
}
EXPORT_SYMBOL(hinic_msg_to_mgmt_async);

int hinic_msg_to_mgmt_no_ack(void *hwdev, enum hinic_mod_type mod, u8 cmd,
			     void *buf_in, u16 in_size)
{
	struct hinic_hwdev *dev = hwdev;
	int err;

	if (!hwdev)
		return -EINVAL;

	if (!(dev->chip_present_flag))
		return -EPERM;

	if (hinic_func_type(hwdev) == TYPE_VF || NEED_MBOX_FORWARD(dev)) {
		if (!hinic_is_hwdev_mod_inited(hwdev, HINIC_HWDEV_MBOX_INITED))
			return -EPERM;

		if (hinic_func_type(hwdev) == TYPE_VF)
			err = hinic_mbox_to_pf_no_ack(hwdev, mod, cmd, buf_in,
						      in_size);
		else
			err = hinic_mbox_to_host_no_ack(hwdev, mod, cmd, buf_in,
							in_size);
	} else {
		err = hinic_pf_to_mgmt_no_ack(hwdev, mod, cmd, buf_in, in_size);
	}

	return err;
}

int hinic_mbox_to_vf(void *hwdev,
		     enum hinic_mod_type mod, u16 vf_id, u8 cmd, void *buf_in,
		     u16 in_size, void *buf_out, u16 *out_size, u32 timeout)
{
	int err;

	if (!hwdev)
		return -EINVAL;

	err = __hinic_mbox_to_vf(hwdev, mod, vf_id, cmd, buf_in, in_size,
				 buf_out, out_size, timeout);
	if (err == MBOX_ERRCODE_UNKNOWN_DES_FUNC) {
		/* VF already in error condiction */
		sdk_warn(((struct hinic_hwdev *)hwdev)->dev_hdl, "VF%d not initialized, disconnect it\n",
			 vf_id);
		hinic_unregister_vf_msg_handler(hwdev, vf_id);
	}

	return err;
}
EXPORT_SYMBOL(hinic_mbox_to_vf);

int hinic_clp_to_mgmt(void *hwdev, enum hinic_mod_type mod, u8 cmd,
		      void *buf_in, u16 in_size,
		      void *buf_out, u16 *out_size)

{
	struct hinic_hwdev *dev = hwdev;
	int err;

	if (!dev)
		return -EINVAL;

	if (!dev->chip_present_flag)
		return -EPERM;

	if (hinic_func_type(hwdev) == TYPE_VF || NEED_MBOX_FORWARD(dev))
		return -EINVAL;

	if (!hinic_is_hwdev_mod_inited(hwdev, HINIC_HWDEV_CLP_INITED))
		return -EPERM;

	err = hinic_pf_clp_to_mgmt(dev, mod, cmd, buf_in,
				   in_size, buf_out, out_size);

	return err;
}

/**
 * hinic_cpu_to_be32 - convert data to big endian 32 bit format
 * @data: the data to convert
 * @len: length of data to convert, must be Multiple of 4B
 */
void hinic_cpu_to_be32(void *data, int len)
{
	int i, chunk_sz = sizeof(u32);
	u32 *mem = data;

	if (!data)
		return;

	len = len / chunk_sz;

	for (i = 0; i < len; i++) {
		*mem = cpu_to_be32(*mem);
		mem++;
	}
}
EXPORT_SYMBOL(hinic_cpu_to_be32);

/**
 * hinic_be32_to_cpu - convert data from big endian 32 bit format
 * @data: the data to convert
 * @len: length of data to convert
 */
void hinic_be32_to_cpu(void *data, int len)
{
	int i, chunk_sz = sizeof(u32);
	u32 *mem = data;

	if (!data)
		return;

	len = len / chunk_sz;

	for (i = 0; i < len; i++) {
		*mem = be32_to_cpu(*mem);
		mem++;
	}
}
EXPORT_SYMBOL(hinic_be32_to_cpu);

/**
 * hinic_set_sge - set dma area in scatter gather entry
 * @sge: scatter gather entry
 * @addr: dma address
 * @len: length of relevant data in the dma address
 */
void hinic_set_sge(struct hinic_sge *sge, dma_addr_t addr, u32 len)
{
	sge->hi_addr = upper_32_bits(addr);
	sge->lo_addr = lower_32_bits(addr);
	sge->len  = len;
}

/**
 * hinic_sge_to_dma - get dma address from scatter gather entry
 * @sge: scatter gather entry
 *
 * Return dma address of sg entry
 */
dma_addr_t hinic_sge_to_dma(struct hinic_sge *sge)
{
	return (dma_addr_t)((((u64)sge->hi_addr) << 32) | sge->lo_addr);
}

int hinic_set_ci_table(void *hwdev, u16 q_id, struct hinic_sq_attr *attr)
{
	struct hinic_cons_idx_attr cons_idx_attr = {0};
	u16 out_size = sizeof(cons_idx_attr);
	int err;

	if (!hwdev || !attr)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &cons_idx_attr.func_idx);
	if (err)
		return err;

	cons_idx_attr.dma_attr_off  = attr->dma_attr_off;
	cons_idx_attr.pending_limit = attr->pending_limit;
	cons_idx_attr.coalescing_time  = attr->coalescing_time;

	if (attr->intr_en) {
		cons_idx_attr.intr_en = attr->intr_en;
		cons_idx_attr.intr_idx = attr->intr_idx;
	}

	cons_idx_attr.l2nic_sqn = attr->l2nic_sqn;
	cons_idx_attr.sq_id = q_id;

	cons_idx_attr.ci_addr = attr->ci_dma_base;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_L2NIC_SQ_CI_ATTR_SET,
				     &cons_idx_attr, sizeof(cons_idx_attr),
				     &cons_idx_attr, &out_size, 0);
	if (err || !out_size || cons_idx_attr.status) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to set ci attribute table, err: %d, status: 0x%x, out_size: 0x%x\n",
			err, cons_idx_attr.status, out_size);
		return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_set_ci_table);

static int hinic_set_cmdq_depth(struct hinic_hwdev *hwdev, u16 cmdq_depth)
{
	struct hinic_root_ctxt root_ctxt = {0};
	u16 out_size = sizeof(root_ctxt);
	int err;

	err = hinic_global_func_id_get(hwdev, &root_ctxt.func_idx);
	if (err)
		return err;

	root_ctxt.ppf_idx = hinic_ppf_idx(hwdev);

	root_ctxt.set_cmdq_depth = 1;
	root_ctxt.cmdq_depth = (u8)ilog2(cmdq_depth);

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_VAT_SET,
				     &root_ctxt, sizeof(root_ctxt),
				     &root_ctxt, &out_size, 0);
	if (err || !out_size || root_ctxt.status) {
		sdk_err(hwdev->dev_hdl, "Failed to set cmdq depth, err: %d, status: 0x%x, out_size: 0x%x\n",
			err, root_ctxt.status, out_size);
		return -EFAULT;
	}

	return 0;
}

static u16 get_hw_rx_buf_size(int rx_buf_sz)
{
	u16 num_hw_types =
		sizeof(hinic_hw_rx_buf_size) /
		sizeof(hinic_hw_rx_buf_size[0]);
	u16 i;

	for (i = 0; i < num_hw_types; i++) {
		if (hinic_hw_rx_buf_size[i] == rx_buf_sz)
			return i;
	}

	pr_err("Chip can't support rx buf size of %d\n", rx_buf_sz);

	return DEFAULT_RX_BUF_SIZE;	/* default 2K */
}

int hinic_set_root_ctxt(void *hwdev, u16 rq_depth, u16 sq_depth, int rx_buf_sz)
{
	struct hinic_root_ctxt root_ctxt = {0};
	u16 out_size = sizeof(root_ctxt);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &root_ctxt.func_idx);
	if (err)
		return err;

	root_ctxt.ppf_idx = hinic_ppf_idx(hwdev);

	root_ctxt.set_cmdq_depth = 0;
	root_ctxt.cmdq_depth = 0;

	root_ctxt.lro_en = 1;

	root_ctxt.rq_depth  = (u16)ilog2(rq_depth);
	root_ctxt.rx_buf_sz = get_hw_rx_buf_size(rx_buf_sz);
	root_ctxt.sq_depth  = (u16)ilog2(sq_depth);

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_VAT_SET,
				     &root_ctxt, sizeof(root_ctxt),
				     &root_ctxt, &out_size, 0);
	if (err || !out_size || root_ctxt.status) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to set root context, err: %d, status: 0x%x, out_size: 0x%x\n",
			err, root_ctxt.status, out_size);
		return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_set_root_ctxt);

int hinic_clean_root_ctxt(void *hwdev)
{
	struct hinic_root_ctxt root_ctxt = {0};
	u16 out_size = sizeof(root_ctxt);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &root_ctxt.func_idx);
	if (err)
		return err;

	root_ctxt.ppf_idx = hinic_ppf_idx(hwdev);

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_VAT_SET,
				     &root_ctxt, sizeof(root_ctxt),
				     &root_ctxt, &out_size, 0);
	if (err || !out_size || root_ctxt.status) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to set root context, err: %d, status: 0x%x, out_size: 0x%x\n",
			err, root_ctxt.status, out_size);
		return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_clean_root_ctxt);

static int wait_for_flr_finish(struct hinic_hwif *hwif)
{
	u32 cnt = 0;
	enum hinic_pf_status status;

	while (cnt < HINIC_FLR_TIMEOUT) {
		status = hinic_get_pf_status(hwif);
		if (status == HINIC_PF_STATUS_FLR_FINISH_FLAG) {
			hinic_set_pf_status(hwif, HINIC_PF_STATUS_ACTIVE_FLAG);
			return 0;
		}

		usleep_range(9900, 10000);
		cnt++;
	}

	return -EFAULT;
}

#define HINIC_WAIT_CMDQ_IDLE_TIMEOUT		5000

static int wait_cmdq_stop(struct hinic_hwdev *hwdev)
{
	enum hinic_cmdq_type cmdq_type;
	struct hinic_cmdqs *cmdqs = hwdev->cmdqs;
	u32 cnt = 0;
	int err = 0;

	if (!(cmdqs->status & HINIC_CMDQ_ENABLE))
		return 0;

	cmdqs->status &= ~HINIC_CMDQ_ENABLE;

	while (cnt < HINIC_WAIT_CMDQ_IDLE_TIMEOUT && hwdev->chip_present_flag) {
		err = 0;
		cmdq_type = HINIC_CMDQ_SYNC;
		for (; cmdq_type < HINIC_MAX_CMDQ_TYPES; cmdq_type++) {
			if (!hinic_cmdq_idle(&cmdqs->cmdq[cmdq_type])) {
				err = -EBUSY;
				break;
			}
		}

		if (!err)
			return 0;

		usleep_range(500, 1000);
		cnt++;
	}

	cmdq_type = HINIC_CMDQ_SYNC;
	for (; cmdq_type < HINIC_MAX_CMDQ_TYPES; cmdq_type++) {
		if (!hinic_cmdq_idle(&cmdqs->cmdq[cmdq_type]))
			sdk_err(hwdev->dev_hdl, "Cmdq %d busy\n", cmdq_type);
	}

	cmdqs->status |= HINIC_CMDQ_ENABLE;

	return err;
}

static int hinic_vf_rx_tx_flush(struct hinic_hwdev *hwdev)
{
	struct hinic_clear_resource clr_res = {0};
	int err;

	err = wait_cmdq_stop(hwdev);
	if (err)
		sdk_warn(hwdev->dev_hdl, "Cmdq is still working, please check CMDQ timeout value is reasonable\n");

	err = hinic_global_func_id_get(hwdev, &clr_res.func_idx);
	if (err)
		return err;

	clr_res.ppf_idx  = HINIC_HWIF_PPF_IDX(hwdev->hwif);
	err = hinic_mbox_to_pf_no_ack(hwdev, HINIC_MOD_COMM,
				      HINIC_MGMT_CMD_START_FLR, &clr_res,
				      sizeof(clr_res));
	if (err)
		sdk_warn(hwdev->dev_hdl, "Failed to notice flush message\n");

	/* PF firstly set VF doorbell flush csr to be disabled. After PF finish
	 * VF resources flush, PF will set VF doorbell flush csr to be enabled.
	 */
	err = wait_until_doorbell_flush_states(hwdev->hwif, DISABLE_DOORBELL);
	if (err)
		sdk_warn(hwdev->dev_hdl, "Wait doorbell flush disable timeout\n");
	err = wait_until_doorbell_flush_states(hwdev->hwif, ENABLE_DOORBELL);
	if (err)
		sdk_warn(hwdev->dev_hdl, "Wait doorbell flush enable timeout\n");

	err = hinic_reinit_cmdq_ctxts(hwdev);
	if (err)
		sdk_warn(hwdev->dev_hdl, "Failed to reinit cmdq\n");

	return 0;
}

static void hinic_pf_set_vf_db_flush(struct hinic_hwdev *hwdev, u16 vf_id,
				     enum hinic_doorbell_ctrl val)
{
	u32 addr, vf_attr4;

	addr = HINIC_PF_CSR_VF_FLUSH_OFF(vf_id);
	vf_attr4 = hinic_hwif_read_reg(hwdev->hwif, addr);
	vf_attr4 = HINIC_AF4_CLEAR(vf_attr4, DOORBELL_CTRL);
	vf_attr4 |= HINIC_AF4_SET(val, DOORBELL_CTRL);
	hinic_hwif_write_reg(hwdev->hwif, addr, vf_attr4);
}

static int hinic_vf_rx_tx_flush_in_pf(struct hinic_hwdev *hwdev, u16 vf_id)
{
	struct hinic_clear_doorbell clear_db = {0};
	struct hinic_clear_resource clr_res = {0};
	u16 glb_vf_func_id;
	u16 out_size;
	int err;
	int ret = 0;

	/* disable vf doorbell flush csr */
	hinic_pf_set_vf_db_flush(hwdev, vf_id, DISABLE_DOORBELL);

	/* doorbell flush */
	out_size = sizeof(clear_db);
	glb_vf_func_id = HINIC_HWIF_GLOBAL_VF_OFFSET(hwdev->hwif) + vf_id;
	clear_db.func_idx = glb_vf_func_id;
	clear_db.ppf_idx = HINIC_HWIF_PPF_IDX(hwdev->hwif);
	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_FLUSH_DOORBELL, &clear_db,
				     sizeof(clear_db), &clear_db, &out_size, 0);
	if (err || !out_size || clear_db.status) {
		sdk_warn(hwdev->dev_hdl, "Failed to flush doorbell, err: %d, status: 0x%x, out_size: 0x%x\n",
			 err, clear_db.status, out_size);
		if (err)
			ret = err;
		else
			ret = -EFAULT;
	}

	/* wait ucode stop I/O */
	msleep(100);

	/* notice up begine vf flush */
	out_size = sizeof(clr_res);
	clr_res.func_idx = glb_vf_func_id;
	clr_res.ppf_idx = HINIC_HWIF_PPF_IDX(hwdev->hwif);
	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_START_FLR, &clr_res,
				     sizeof(clr_res), &clr_res, &out_size, 0);
	if (err || !out_size || clr_res.status) {
		sdk_warn(hwdev->dev_hdl, "Failed to flush doorbell, err: %d, status: 0x%x, out_size: 0x%x\n",
			 err, clr_res.status, out_size);
		ret = err ? err : (-EFAULT);
	}
	/* enable vf doorbell flush csr */
	hinic_pf_set_vf_db_flush(hwdev, vf_id, ENABLE_DOORBELL);

	return ret;
}

static int hinic_pf_rx_tx_flush(struct hinic_hwdev *hwdev)
{
	struct hinic_hwif *hwif = hwdev->hwif;
	struct hinic_clear_doorbell clear_db = {0};
	struct hinic_clear_resource clr_res = {0};
	u16 out_size, func_id;
	int err;
	int ret = 0;

	/* wait ucode stop I/O */
	msleep(100);

	err = wait_cmdq_stop(hwdev);
	if (err) {
		sdk_warn(hwdev->dev_hdl, "CMDQ is still working, please check CMDQ timeout value is reasonable\n");
		ret = err;
	}

	hinic_disable_doorbell(hwif);

	out_size = sizeof(clear_db);
	func_id = hinic_global_func_id_hw(hwdev);
	clear_db.func_idx = func_id;
	clear_db.ppf_idx  = HINIC_HWIF_PPF_IDX(hwif);

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_FLUSH_DOORBELL, &clear_db,
				     sizeof(clear_db), &clear_db, &out_size, 0);
	if (err || !out_size || clear_db.status) {
		sdk_warn(hwdev->dev_hdl, "Failed to flush doorbell, err: %d, status: 0x%x, out_size: 0x%x\n",
			 err, clear_db.status, out_size);
		ret = err ? err : (-EFAULT);
	}

	hinic_set_pf_status(hwif, HINIC_PF_STATUS_FLR_START_FLAG);

	clr_res.func_idx = func_id;
	clr_res.ppf_idx  = HINIC_HWIF_PPF_IDX(hwif);

	err = hinic_msg_to_mgmt_no_ack(hwdev, HINIC_MOD_COMM,
				       HINIC_MGMT_CMD_START_FLR, &clr_res,
				       sizeof(clr_res));
	if (err) {
		sdk_warn(hwdev->dev_hdl, "Failed to notice flush message\n");
		ret = err;
	}

	err = wait_for_flr_finish(hwif);
	if (err) {
		sdk_warn(hwdev->dev_hdl, "Wait firmware FLR timeout\n");
		ret = err;
	}

	hinic_enable_doorbell(hwif);

	err = hinic_reinit_cmdq_ctxts(hwdev);
	if (err) {
		sdk_warn(hwdev->dev_hdl, "Failed to reinit cmdq\n");
		ret = err;
	}

	return ret;
}

int hinic_func_rx_tx_flush(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return -EINVAL;

	if (!dev->chip_present_flag)
		return 0;

	if (HINIC_FUNC_TYPE(dev) == TYPE_VF)
		return hinic_vf_rx_tx_flush(dev);
	else
		return hinic_pf_rx_tx_flush(dev);
}
EXPORT_SYMBOL(hinic_func_rx_tx_flush);

int hinic_get_interrupt_cfg(void *hwdev,
			    struct nic_interrupt_info *interrupt_info)
{
	struct hinic_hwdev *nic_hwdev = hwdev;
	struct hinic_msix_config msix_cfg = {0};
	u16 out_size = sizeof(msix_cfg);
	int err;

	if (!hwdev || !interrupt_info)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &msix_cfg.func_id);
	if (err)
		return err;

	msix_cfg.msix_index = interrupt_info->msix_index;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_MSI_CTRL_REG_RD_BY_UP,
				     &msix_cfg, sizeof(msix_cfg),
				     &msix_cfg, &out_size, 0);
	if (err || !out_size || msix_cfg.status) {
		sdk_err(nic_hwdev->dev_hdl, "Failed to get interrupt config, err: %d, status: 0x%x, out size: 0x%x\n",
			err, msix_cfg.status, out_size);
		return -EINVAL;
	}

	interrupt_info->lli_credit_limit = msix_cfg.lli_credit_cnt;
	interrupt_info->lli_timer_cfg = msix_cfg.lli_tmier_cnt;
	interrupt_info->pending_limt = msix_cfg.pending_cnt;
	interrupt_info->coalesc_timer_cfg = msix_cfg.coalesct_timer_cnt;
	interrupt_info->resend_timer_cfg = msix_cfg.resend_timer_cnt;

	return 0;
}
EXPORT_SYMBOL(hinic_get_interrupt_cfg);

int hinic_set_interrupt_cfg_direct(void *hwdev,
				   struct nic_interrupt_info *interrupt_info)
{
	struct hinic_hwdev *nic_hwdev = hwdev;
	struct hinic_msix_config msix_cfg = {0};
	u16 out_size = sizeof(msix_cfg);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &msix_cfg.func_id);
	if (err)
		return err;

	msix_cfg.msix_index = (u16)interrupt_info->msix_index;
	msix_cfg.lli_credit_cnt = interrupt_info->lli_credit_limit;
	msix_cfg.lli_tmier_cnt = interrupt_info->lli_timer_cfg;
	msix_cfg.pending_cnt = interrupt_info->pending_limt;
	msix_cfg.coalesct_timer_cnt = interrupt_info->coalesc_timer_cfg;
	msix_cfg.resend_timer_cnt = interrupt_info->resend_timer_cfg;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_MSI_CTRL_REG_WR_BY_UP,
				     &msix_cfg, sizeof(msix_cfg),
				     &msix_cfg, &out_size, 0);
	if (err || !out_size || msix_cfg.status) {
		sdk_err(nic_hwdev->dev_hdl, "Failed to set interrupt config, err: %d, status: 0x%x, out size: 0x%x\n",
			err, msix_cfg.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int hinic_set_interrupt_cfg(void *hwdev,
			    struct nic_interrupt_info interrupt_info)
{
	struct nic_interrupt_info temp_info;
	int err;

	if (!hwdev)
		return -EINVAL;

	temp_info.msix_index = interrupt_info.msix_index;

	err = hinic_get_interrupt_cfg(hwdev, &temp_info);
	if (err)
		return -EINVAL;

	if (!interrupt_info.lli_set) {
		interrupt_info.lli_credit_limit = temp_info.lli_credit_limit;
		interrupt_info.lli_timer_cfg = temp_info.lli_timer_cfg;
	}

	if (!interrupt_info.interrupt_coalesc_set) {
		interrupt_info.pending_limt = temp_info.pending_limt;
		interrupt_info.coalesc_timer_cfg = temp_info.coalesc_timer_cfg;
		interrupt_info.resend_timer_cfg = temp_info.resend_timer_cfg;
	}

	return hinic_set_interrupt_cfg_direct(hwdev, &interrupt_info);
}
EXPORT_SYMBOL(hinic_set_interrupt_cfg);

void hinic_misx_intr_clear_resend_bit(void *hwdev, u16 msix_idx,
				      u8 clear_resend_en)
{
	struct hinic_hwif *hwif;
	u32 msix_ctrl = 0, addr;

	if (!hwdev)
		return;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	msix_ctrl = HINIC_MSIX_CNT_SET(clear_resend_en, RESEND_TIMER);

	addr = HINIC_CSR_MSIX_CNT_ADDR(msix_idx);

	hinic_hwif_write_reg(hwif, addr, msix_ctrl);
}
EXPORT_SYMBOL(hinic_misx_intr_clear_resend_bit);

static int init_aeqs_msix_attr(struct hinic_hwdev *hwdev)
{
	struct hinic_aeqs *aeqs = hwdev->aeqs;
	struct nic_interrupt_info info = {0};
	struct hinic_eq *eq;
	int q_id;
	int err;

	info.lli_set = 0;
	info.interrupt_coalesc_set = 1;
	info.pending_limt = HINIC_DEAULT_EQ_MSIX_PENDING_LIMIT;
	info.coalesc_timer_cfg = HINIC_DEAULT_EQ_MSIX_COALESC_TIMER_CFG;
	info.resend_timer_cfg = HINIC_DEAULT_EQ_MSIX_RESEND_TIMER_CFG;

	for (q_id = aeqs->num_aeqs - 1; q_id >= 0; q_id--) {
		eq = &aeqs->aeq[q_id];
		info.msix_index = eq->eq_irq.msix_entry_idx;
		err = hinic_set_interrupt_cfg_direct(hwdev, &info);
		if (err) {
			sdk_err(hwdev->dev_hdl, "Set msix attr for aeq %d failed\n",
				q_id);
			return -EFAULT;
		}
	}

	hinic_set_mbox_seg_ack_mod(hwdev, HINIC_MBOX_SEND_MSG_INT);

	return 0;
}

static int init_ceqs_msix_attr(struct hinic_hwdev *hwdev)
{
	struct hinic_ceqs *ceqs = hwdev->ceqs;
	struct nic_interrupt_info info = {0};
	struct hinic_eq *eq;
	u16 q_id;
	int err;

	info.lli_set = 0;
	info.interrupt_coalesc_set = 1;
	info.pending_limt = HINIC_DEAULT_EQ_MSIX_PENDING_LIMIT;
	info.coalesc_timer_cfg = HINIC_DEAULT_EQ_MSIX_COALESC_TIMER_CFG;
	info.resend_timer_cfg = HINIC_DEAULT_EQ_MSIX_RESEND_TIMER_CFG;

	for (q_id = 0; q_id < ceqs->num_ceqs; q_id++) {
		eq = &ceqs->ceq[q_id];
		info.msix_index = eq->eq_irq.msix_entry_idx;
		err = hinic_set_interrupt_cfg(hwdev, info);
		if (err) {
			sdk_err(hwdev->dev_hdl, "Set msix attr for ceq %d failed\n",
				q_id);
			return -EFAULT;
		}
	}

	return 0;
}

/**
 * set_pf_dma_attr_entry - set the dma attributes for entry
 * @hwdev: the pointer to hw device
 * @entry_idx: the entry index in the dma table
 * @st: PCIE TLP steering tag
 * @at: PCIE TLP AT field
 * @ph: PCIE TLP Processing Hint field
 * @no_snooping: PCIE TLP No snooping
 * @tph_en: PCIE TLP Processing Hint Enable
 */
static void set_pf_dma_attr_entry(struct hinic_hwdev *hwdev, u32 entry_idx,
				  u8 st, u8 at, u8 ph,
				enum hinic_pcie_nosnoop no_snooping,
				enum hinic_pcie_tph tph_en)
{
	u32 addr, val, dma_attr_entry;

	/* Read Modify Write */
	addr = HINIC_CSR_DMA_ATTR_TBL_ADDR(entry_idx);

	val = hinic_hwif_read_reg(hwdev->hwif, addr);
	val = HINIC_DMA_ATTR_ENTRY_CLEAR(val, ST)	&
		HINIC_DMA_ATTR_ENTRY_CLEAR(val, AT)	&
		HINIC_DMA_ATTR_ENTRY_CLEAR(val, PH)	&
		HINIC_DMA_ATTR_ENTRY_CLEAR(val, NO_SNOOPING)	&
		HINIC_DMA_ATTR_ENTRY_CLEAR(val, TPH_EN);

	dma_attr_entry = HINIC_DMA_ATTR_ENTRY_SET(st, ST)	|
			 HINIC_DMA_ATTR_ENTRY_SET(at, AT)	|
			 HINIC_DMA_ATTR_ENTRY_SET(ph, PH)	|
			 HINIC_DMA_ATTR_ENTRY_SET(no_snooping, NO_SNOOPING) |
			 HINIC_DMA_ATTR_ENTRY_SET(tph_en, TPH_EN);

	val |= dma_attr_entry;
	hinic_hwif_write_reg(hwdev->hwif, addr, val);
}

static int set_vf_dma_attr_entry(struct hinic_hwdev *hwdev, u8 entry_idx,
				 u8 st, u8 at, u8 ph,
				enum hinic_pcie_nosnoop no_snooping,
				enum hinic_pcie_tph tph_en)
{
	struct hinic_vf_dma_attr_table attr = {0};
	u16 out_size = sizeof(attr);
	int err;

	err = hinic_global_func_id_get(hwdev, &attr.func_idx);
	if (err)
		return err;

	attr.func_dma_entry_num = hinic_dma_attr_entry_num(hwdev);
	attr.entry_idx = entry_idx;
	attr.st = st;
	attr.at = at;
	attr.ph = ph;
	attr.no_snooping = no_snooping;
	attr.tph_en = tph_en;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_DMA_ATTR_SET, &attr,
				     sizeof(attr), &attr, &out_size, 0);
	if (err || !out_size || attr.status) {
		sdk_err(hwdev->dev_hdl, "Failed to set dma attribute, err: %d, status: 0x%x, out_size: 0x%x\n",
			err, attr.status, out_size);
		return -EFAULT;
	}

	return 0;
}

/**
 * dma_attr_table_init - initialize the the default dma attributes
 * @hwdev: the pointer to hw device
 * Return: 0 - success, negative - failure
 */
static int dma_attr_table_init(struct hinic_hwdev *hwdev)
{
	int err = 0;

	if (HINIC_IS_VF(hwdev))
		err = set_vf_dma_attr_entry(hwdev, PCIE_MSIX_ATTR_ENTRY,
					    HINIC_PCIE_ST_DISABLE,
					    HINIC_PCIE_AT_DISABLE,
					    HINIC_PCIE_PH_DISABLE,
					    HINIC_PCIE_SNOOP,
					    HINIC_PCIE_TPH_DISABLE);
	else
		set_pf_dma_attr_entry(hwdev, PCIE_MSIX_ATTR_ENTRY,
				      HINIC_PCIE_ST_DISABLE,
				      HINIC_PCIE_AT_DISABLE,
				      HINIC_PCIE_PH_DISABLE,
				      HINIC_PCIE_SNOOP,
				      HINIC_PCIE_TPH_DISABLE);

	return err;
}

static int resources_state_set(struct hinic_hwdev *hwdev,
			       enum hinic_res_state state)
{
	struct hinic_cmd_set_res_state res_state = {0};
	u16 out_size = sizeof(res_state);
	int err;

	err = hinic_global_func_id_get(hwdev, &res_state.func_idx);
	if (err)
		return err;

	res_state.state = state;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_RES_STATE_SET,
				     &res_state, sizeof(res_state),
				     &res_state, &out_size, 0);
	if (err || !out_size || res_state.status) {
		sdk_err(hwdev->dev_hdl, "Failed to set resources state, err: %d, status: 0x%x, out_size: 0x%x\n",
			err, res_state.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int hinic_sync_heartbeat_status(struct hinic_hwdev *hwdev,
				enum heartbeat_support_state pf_state,
				enum heartbeat_support_state *mgmt_state)
{
	struct hinic_heartbeat_support hb_support = {0};
	u16 out_size = sizeof(hb_support);
	int err;

	hb_support.ppf_id = hinic_ppf_idx(hwdev);
	hb_support.pf_issupport = pf_state;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_HEARTBEAT_SUPPORTED,
				     &hb_support, sizeof(hb_support),
				     &hb_support, &out_size, 0);
	if ((hb_support.status != HINIC_MGMT_CMD_UNSUPPORTED &&
	     hb_support.status) || err || !out_size) {
		sdk_err(hwdev->dev_hdl, "Failed to synchronize heartbeat status, err: %d, status: 0x%x, out_size: 0x%x\n",
			err, hb_support.status, out_size);
		return -EFAULT;
	}

	if (!hb_support.status)
		*mgmt_state = hb_support.mgmt_issupport;

	return hb_support.status;
}

static void comm_mgmt_msg_handler(void *hwdev, void *pri_handle, u8 cmd,
				  void *buf_in, u16 in_size, void *buf_out,
				  u16 *out_size)
{
	struct hinic_msg_pf_to_mgmt *pf_to_mgmt = pri_handle;
	u8 cmd_idx;
	u32 *mem;
	u16 i;

	for (cmd_idx = 0; cmd_idx < pf_to_mgmt->proc.cmd_num; cmd_idx++) {
		if (cmd == pf_to_mgmt->proc.info[cmd_idx].cmd) {
			if (!pf_to_mgmt->proc.info[cmd_idx].proc) {
				sdk_warn(pf_to_mgmt->hwdev->dev_hdl,
					 "PF recv up comm msg handle null, cmd(0x%x)\n",
					 cmd);
			} else {
				pf_to_mgmt->proc.info[cmd_idx].proc(hwdev,
					buf_in, in_size, buf_out, out_size);
			}

			return;
		}
	}

	sdk_warn(pf_to_mgmt->hwdev->dev_hdl, "Received mgmt cpu event: 0x%x\n",
		 cmd);

	mem = buf_in;
	for (i = 0; i < (in_size / sizeof(u32)); i++) {
		pr_info("0x%x\n", *mem);
		mem++;
	}

	*out_size = 0;
}

static int hinic_vf_get_ppf_init_state(void *handle, void *buf_out,
				       u16 *out_size)
{
	struct hinic_hwdev *hwdev = handle;
	struct hinic_ppf_state *ppf_state = buf_out;
	struct card_node *chip_node = hwdev->chip_node;

	ppf_state->ppf_state = (u8)chip_node->ppf_state;

	*out_size = sizeof(*ppf_state);

	return 0;
}

int hinic_get_sdi_mode(struct hinic_hwdev *hwdev, u16 *cur_mode)
{
	struct hinic_sdi_mode_info sdi_mode = {0};
	u16 out_size = sizeof(sdi_mode);
	int err;

	sdi_mode.opcode = HINIC_SDI_INFO_MODE & (~HINIC_SDI_INFO_SET);

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_GET_SDI_MODE, &sdi_mode,
				     sizeof(sdi_mode), &sdi_mode, &out_size, 0);
	if ((sdi_mode.status != HINIC_MGMT_CMD_UNSUPPORTED &&
	     sdi_mode.status) || err || !out_size) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to get sdi mode info, err: %d, status: 0x%x, out size: 0x%x\n",
			err, sdi_mode.status, out_size);
		return -EFAULT;
	}

	*cur_mode = sdi_mode.cur_sdi_mode;

	return sdi_mode.status;
}

int comm_pf_mbox_handler(void *handle, u16 vf_id, u8 cmd, void *buf_in,
			 u16 in_size, void *buf_out, u16 *out_size)
{
	int err = 0;
	u8 size = sizeof(hw_cmd_support_vf) / sizeof(hw_cmd_support_vf[0]);

	if (!hinic_mbox_check_cmd_valid(handle, hw_cmd_support_vf, vf_id, cmd,
					buf_in, in_size, size)) {
		sdk_err(((struct hinic_hwdev *)handle)->dev_hdl,
			"PF Receive VF(%d) common cmd(0x%x), mbox len(0x%x) is invalid\n",
			vf_id + hinic_glb_pf_vf_offset(handle), cmd, in_size);
		err = HINIC_MBOX_VF_CMD_ERROR;
		return err;
	}

	if (cmd == HINIC_MGMT_CMD_START_FLR) {
		*out_size = 0;
		err = hinic_vf_rx_tx_flush_in_pf(handle, vf_id);
	} else if (cmd == HINIC_MGMT_CMD_GET_PPF_STATE) {
		err = hinic_vf_get_ppf_init_state(handle, buf_out, out_size);
	} else {
		err = hinic_pf_msg_to_mgmt_sync(handle, HINIC_MOD_COMM, cmd,
						buf_in, in_size, buf_out,
						out_size, 0U);
		if (err && err != HINIC_DEV_BUSY_ACTIVE_FW &&
		    err != HINIC_MBOX_PF_BUSY_ACTIVE_FW)
			sdk_err(((struct hinic_hwdev *)handle)->dev_hdl,
				"PF mbox common callback handler err: %d\n",
				err);
	}

	return err;
}

static int hinic_comm_aeqs_init(struct hinic_hwdev *hwdev)
{
	struct irq_info aeq_irqs[HINIC_MAX_AEQS] = {{0} };
	u16 num_aeqs, resp_num_irq = 0, i;
	int err;

	num_aeqs = HINIC_HWIF_NUM_AEQS(hwdev->hwif);
	if (num_aeqs > HINIC_MAX_AEQS) {
		sdk_warn(hwdev->dev_hdl, "Adjust aeq num to %d\n",
			 HINIC_MAX_AEQS);
		num_aeqs = HINIC_MAX_AEQS;
	}
	err = hinic_alloc_irqs(hwdev, SERVICE_T_INTF, num_aeqs, aeq_irqs,
			       &resp_num_irq);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to alloc aeq irqs, num_aeqs: %d\n",
			num_aeqs);
		return err;
	}

	if (resp_num_irq < num_aeqs) {
		sdk_warn(hwdev->dev_hdl, "Adjust aeq num to %d\n",
			 resp_num_irq);
		num_aeqs = resp_num_irq;
	}

	err = hinic_aeqs_init(hwdev, num_aeqs, aeq_irqs);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to init aeqs\n");
		goto aeqs_init_err;
	}

	set_bit(HINIC_HWDEV_AEQ_INITED, &hwdev->func_state);

	return 0;

aeqs_init_err:
	for (i = 0; i < num_aeqs; i++)
		hinic_free_irq(hwdev, SERVICE_T_INTF, aeq_irqs[i].irq_id);

	return err;
}

static void hinic_comm_aeqs_free(struct hinic_hwdev *hwdev)
{
	struct irq_info aeq_irqs[HINIC_MAX_AEQS] = {{0} };
	u16 num_irqs, i;

	clear_bit(HINIC_HWDEV_AEQ_INITED, &hwdev->func_state);

	hinic_get_aeq_irqs(hwdev, aeq_irqs, &num_irqs);
	hinic_aeqs_free(hwdev);
	for (i = 0; i < num_irqs; i++)
		hinic_free_irq(hwdev, SERVICE_T_INTF, aeq_irqs[i].irq_id);
}

static int hinic_comm_ceqs_init(struct hinic_hwdev *hwdev)
{
	struct irq_info ceq_irqs[HINIC_MAX_CEQS] = {{0} };
	u16 num_ceqs, resp_num_irq = 0, i;
	int err;

	num_ceqs = HINIC_HWIF_NUM_CEQS(hwdev->hwif);
	if (num_ceqs > HINIC_MAX_CEQS) {
		sdk_warn(hwdev->dev_hdl, "Adjust ceq num to %d\n",
			 HINIC_MAX_CEQS);
		num_ceqs = HINIC_MAX_CEQS;
	}

	err = hinic_alloc_irqs(hwdev, SERVICE_T_INTF, num_ceqs, ceq_irqs,
			       &resp_num_irq);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to alloc ceq irqs, num_ceqs: %d\n",
			num_ceqs);
		return err;
	}

	if (resp_num_irq < num_ceqs) {
		sdk_warn(hwdev->dev_hdl, "Adjust ceq num to %d\n",
			 resp_num_irq);
		num_ceqs = resp_num_irq;
	}

	err = hinic_ceqs_init(hwdev, num_ceqs, ceq_irqs);
	if (err) {
		sdk_err(hwdev->dev_hdl,
			"Failed to init ceqs, err:%d\n", err);
		goto ceqs_init_err;
	}

	return 0;

ceqs_init_err:
	for (i = 0; i < num_ceqs; i++)
		hinic_free_irq(hwdev, SERVICE_T_INTF, ceq_irqs[i].irq_id);

	return err;
}

static void hinic_comm_ceqs_free(struct hinic_hwdev *hwdev)
{
	struct irq_info ceq_irqs[HINIC_MAX_CEQS] = {{0} };
	u16 num_irqs;
	int i;

	hinic_get_ceq_irqs(hwdev, ceq_irqs, &num_irqs);
	hinic_ceqs_free(hwdev);
	for (i = 0; i < num_irqs; i++)
		hinic_free_irq(hwdev, SERVICE_T_INTF, ceq_irqs[i].irq_id);
}

static int hinic_comm_func_to_func_init(struct hinic_hwdev *hwdev)
{
	int err;

	err = hinic_func_to_func_init(hwdev);
	if (err)
		return err;

	hinic_aeq_register_hw_cb(hwdev, HINIC_MBX_FROM_FUNC,
				 hinic_mbox_func_aeqe_handler);
	hinic_aeq_register_hw_cb(hwdev, HINIC_MBX_SEND_RSLT,
				 hinic_mbox_self_aeqe_handler);

	if (!HINIC_IS_VF(hwdev)) {
		hinic_register_pf_mbox_cb(hwdev, HINIC_MOD_COMM,
					  comm_pf_mbox_handler);
		hinic_register_pf_mbox_cb(hwdev, HINIC_MOD_SW_FUNC,
					  sw_func_pf_mbox_handler);
	} else {
		hinic_register_pf_mbox_cb(hwdev, HINIC_MOD_COMM,
					  vf_to_pf_handler);
	}

	set_bit(HINIC_HWDEV_MBOX_INITED, &hwdev->func_state);

	return 0;
}

static void hinic_comm_func_to_func_free(struct hinic_hwdev *hwdev)
{
	hinic_aeq_unregister_hw_cb(hwdev, HINIC_MBX_FROM_FUNC);
	hinic_aeq_unregister_hw_cb(hwdev, HINIC_MBX_SEND_RSLT);

	hinic_unregister_pf_mbox_cb(hwdev, HINIC_MOD_COMM);
	hinic_unregister_pf_mbox_cb(hwdev, HINIC_MOD_SW_FUNC);

	hinic_func_to_func_free(hwdev);
}

static int hinic_comm_pf_to_mgmt_init(struct hinic_hwdev *hwdev)
{
	int err;

	if (hinic_func_type(hwdev) == TYPE_VF ||
	    !FUNC_SUPPORT_MGMT(hwdev))
		return 0; /* VF do not support send msg to mgmt directly */

	err = hinic_pf_to_mgmt_init(hwdev);
	if (err)
		return err;

	hinic_aeq_register_hw_cb(hwdev, HINIC_MSG_FROM_MGMT_CPU,
				 hinic_mgmt_msg_aeqe_handler);

	hinic_register_mgmt_msg_cb(hwdev, HINIC_MOD_COMM,
				   hwdev->pf_to_mgmt, comm_mgmt_msg_handler);

	set_bit(HINIC_HWDEV_MGMT_INITED, &hwdev->func_state);

	return 0;
}

static void hinic_comm_pf_to_mgmt_free(struct hinic_hwdev *hwdev)
{
	if (hinic_func_type(hwdev) == TYPE_VF ||
	    !FUNC_SUPPORT_MGMT(hwdev))
		return;	/* VF do not support send msg to mgmt directly */

	hinic_unregister_mgmt_msg_cb(hwdev, HINIC_MOD_COMM);

	hinic_aeq_unregister_hw_cb(hwdev, HINIC_MSG_FROM_MGMT_CPU);

	hinic_pf_to_mgmt_free(hwdev);
}

static int hinic_comm_clp_to_mgmt_init(struct hinic_hwdev *hwdev)
{
	int err;

	if (hinic_func_type(hwdev) == TYPE_VF ||
	    !FUNC_SUPPORT_MGMT(hwdev))
		return 0;

	err = hinic_clp_pf_to_mgmt_init(hwdev);
	if (err)
		return err;

	set_bit(HINIC_HWDEV_CLP_INITED, &hwdev->func_state);

	return 0;
}

static void hinic_comm_clp_to_mgmt_free(struct hinic_hwdev *hwdev)
{
	if (hinic_func_type(hwdev) == TYPE_VF ||
	    !FUNC_SUPPORT_MGMT(hwdev))
		return;

	clear_bit(HINIC_HWDEV_CLP_INITED, &hwdev->func_state);
	hinic_clp_pf_to_mgmt_free(hwdev);
}

static int hinic_comm_cmdqs_init(struct hinic_hwdev *hwdev)
{
	int err;

	err = hinic_cmdqs_init(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to init cmd queues\n");
		return err;
	}

	hinic_ceq_register_cb(hwdev, HINIC_CMDQ, hinic_cmdq_ceq_handler);

	err = hinic_set_cmdq_depth(hwdev, HINIC_CMDQ_DEPTH);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to set cmdq depth\n");
		goto set_cmdq_depth_err;
	}

	return 0;

set_cmdq_depth_err:
	hinic_cmdqs_free(hwdev);

	return err;
}

static void hinic_comm_cmdqs_free(struct hinic_hwdev *hwdev)
{
	hinic_ceq_unregister_cb(hwdev, HINIC_CMDQ);
	hinic_cmdqs_free(hwdev);
}

static inline void __set_heartbeat_ehd_detect_delay(struct hinic_hwdev *hwdev,
						    u32 delay_ms)
{
	hwdev->heartbeat_ehd.start_detect_jiffies =
					jiffies + msecs_to_jiffies(delay_ms);
}

static int hinic_sync_mgmt_func_state(struct hinic_hwdev *hwdev)
{
	int err;

	hinic_set_pf_status(hwdev->hwif, HINIC_PF_STATUS_ACTIVE_FLAG);

	err = resources_state_set(hwdev, HINIC_RES_ACTIVE);
	if (err) {
		sdk_err(hwdev->dev_hdl,
			"Failed to set function resources state\n");
		goto resources_state_set_err;
	}

	hwdev->heartbeat_ehd.en = false;
	if (HINIC_FUNC_TYPE(hwdev) == TYPE_PPF) {
		/* heartbeat synchronize must be after set pf active status */
		hinic_comm_recv_mgmt_self_cmd_reg(
			hwdev, HINIC_MGMT_CMD_HEARTBEAT_EVENT,
			mgmt_heartbeat_event_handler);
	}

	return 0;

resources_state_set_err:
	hinic_set_pf_status(hwdev->hwif, HINIC_PF_STATUS_INIT);

	return err;
}

static void hinic_unsync_mgmt_func_state(struct hinic_hwdev *hwdev)
{

	hinic_set_pf_status(hwdev->hwif, HINIC_PF_STATUS_INIT);

	hwdev->heartbeat_ehd.en = false;
	if (HINIC_FUNC_TYPE(hwdev) == TYPE_PPF) {
		hinic_comm_recv_up_self_cmd_unreg(
			hwdev, HINIC_MGMT_CMD_HEARTBEAT_EVENT);
	}

	resources_state_set(hwdev, HINIC_RES_CLEAN);
}

int hinic_l2nic_reset_base(struct hinic_hwdev *hwdev, u16 reset_flag)
{
	struct hinic_l2nic_reset l2nic_reset = {0};
	u16 out_size = sizeof(l2nic_reset);
	int err = 0;

	err = hinic_set_vport_enable(hwdev, false);
	if (err)
		return err;

	msleep(100);

	sdk_info(hwdev->dev_hdl, "L2nic reset flag 0x%x\n", reset_flag);

	err = hinic_global_func_id_get(hwdev, &l2nic_reset.func_id);
	if (err)
		return err;

	l2nic_reset.reset_flag = reset_flag;
	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_L2NIC_RESET, &l2nic_reset,
				     sizeof(l2nic_reset), &l2nic_reset,
				     &out_size, 0);
	if (err || !out_size || l2nic_reset.status) {
		sdk_err(hwdev->dev_hdl, "Failed to reset L2NIC resources, err: %d, status: 0x%x, out_size: 0x%x\n",
			err, l2nic_reset.status, out_size);
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_l2nic_reset_base);

static int hinic_l2nic_reset(struct hinic_hwdev *hwdev)
{
	return hinic_l2nic_reset_base(hwdev, 0);
}

static int __get_func_misc_info(struct hinic_hwdev *hwdev)
{
	int err;

	err = hinic_get_board_info(hwdev, &hwdev->board_info);
	if (err) {
		/* For the pf/vf of slave host, return error */
		if (hinic_pcie_itf_id(hwdev))
			return err;

		/* VF can't get board info in early version */
		if (!HINIC_IS_VF(hwdev)) {
			sdk_err(hwdev->dev_hdl, "Get board info failed\n");
			return err;
		}

		memset(&hwdev->board_info, 0xff,
		       sizeof(struct hinic_board_info));
	}

	err = hinic_get_mgmt_version(hwdev, hwdev->mgmt_ver);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Get mgmt cpu version failed\n");
		return err;
	}

	return 0;
}


/* initialize communication channel */
int hinic_init_comm_ch(struct hinic_hwdev *hwdev)
{
	int err;
	u16 func_id;

	if (IS_BMGW_SLAVE_HOST(hwdev) &&
	    (!hinic_get_master_host_mbox_enable(hwdev))) {
		sdk_err(hwdev->dev_hdl, "Master host not initialized\n");
		return -EFAULT;
	}

	err = hinic_comm_clp_to_mgmt_init(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to init clp\n");
		return err;
	}

	err = hinic_comm_aeqs_init(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to init async event queues\n");
		goto aeqs_init_err;
	}

	err = hinic_comm_pf_to_mgmt_init(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to init msg\n");
		goto msg_init_err;
	}

	err = hinic_comm_func_to_func_init(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to init mailbox\n");
		goto func_to_func_init_err;
	}

	err = init_aeqs_msix_attr(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to init aeqs msix attr\n");
		goto aeqs_msix_attr_init_err;
	}

	err = __get_func_misc_info(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to get function msic information\n");
		goto get_func_info_err;
	}

	/* detect master host chip mode according board type and host id */
	err = rectify_host_mode(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to rectify host mode\n");
		goto rectify_mode_err;
	}

	err = hinic_l2nic_reset(hwdev);
	if (err)
		goto l2nic_reset_err;

	if (IS_MULTI_HOST(hwdev)) {
		err = hinic_multi_host_mgmt_init(hwdev);
		if (err)
			goto multi_host_mgmt_init_err;
	}

	dma_attr_table_init(hwdev);

	err = hinic_comm_ceqs_init(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to init completion event queues\n");
		goto ceqs_init_err;
	}

	err = init_ceqs_msix_attr(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to init ceqs msix attr\n");
		goto init_eqs_msix_err;
	}

	/* set default wq page_size */
	hwdev->wq_page_size = HINIC_DEFAULT_WQ_PAGE_SIZE;

	err = hinic_global_func_id_get(hwdev, &func_id);
	if (err)
		goto get_func_id_err;

	err = hinic_set_wq_page_size(hwdev, func_id, hwdev->wq_page_size);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to set wq page size\n");
		goto init_wq_pg_size_err;
	}

	err = hinic_comm_cmdqs_init(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to init cmd queues\n");
		goto cmdq_init_err;
	}

	set_bit(HINIC_HWDEV_CMDQ_INITED, &hwdev->func_state);

	err = hinic_sync_mgmt_func_state(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to synchronize mgmt function state\n");
		goto sync_mgmt_func_err;
	}

	err = hinic_aeq_register_swe_cb(hwdev, HINIC_STATELESS_EVENT,
					hinic_nic_sw_aeqe_handler);
	if (err) {
		sdk_err(hwdev->dev_hdl,
			"Failed to register ucode aeqe handler\n");
		goto register_ucode_aeqe_err;
	}

	set_bit(HINIC_HWDEV_COMM_CH_INITED, &hwdev->func_state);

	return 0;

register_ucode_aeqe_err:
	hinic_unsync_mgmt_func_state(hwdev);
sync_mgmt_func_err:
	return err;

cmdq_init_err:
	if (HINIC_FUNC_TYPE(hwdev) != TYPE_VF)
		hinic_set_wq_page_size(hwdev, func_id, HINIC_HW_WQ_PAGE_SIZE);
init_wq_pg_size_err:
get_func_id_err:
init_eqs_msix_err:
	hinic_comm_ceqs_free(hwdev);

ceqs_init_err:
	if (IS_MULTI_HOST(hwdev))
		hinic_multi_host_mgmt_free(hwdev);
multi_host_mgmt_init_err:
l2nic_reset_err:
rectify_mode_err:
get_func_info_err:
aeqs_msix_attr_init_err:
func_to_func_init_err:
	return err;

msg_init_err:
	hinic_comm_aeqs_free(hwdev);

aeqs_init_err:
	hinic_comm_clp_to_mgmt_free(hwdev);

	return err;
}

static void __uninit_comm_module(struct hinic_hwdev *hwdev,
				 enum hinic_hwdev_init_state init_state)
{
	u16 func_id;

	switch (init_state) {
	case HINIC_HWDEV_COMM_CH_INITED:
		hinic_aeq_unregister_swe_cb(hwdev,
					    HINIC_STATELESS_EVENT);
		hinic_unsync_mgmt_func_state(hwdev);
		break;
	case HINIC_HWDEV_CMDQ_INITED:
		hinic_comm_cmdqs_free(hwdev);
		/* VF can set page size of 256K only, any other value
		 * will return error in pf, pf will set all vf's page
		 * size to 4K when disable sriov
		 */
		if (HINIC_FUNC_TYPE(hwdev) != TYPE_VF) {
			func_id = hinic_global_func_id_hw(hwdev);
			hinic_set_wq_page_size(hwdev, func_id,
					       HINIC_HW_WQ_PAGE_SIZE);
		}

		hinic_comm_ceqs_free(hwdev);

		if (IS_MULTI_HOST(hwdev))
			hinic_multi_host_mgmt_free(hwdev);
		break;
	case HINIC_HWDEV_MBOX_INITED:
		hinic_comm_func_to_func_free(hwdev);
		break;
	case HINIC_HWDEV_MGMT_INITED:
		hinic_comm_pf_to_mgmt_free(hwdev);
		break;
	case HINIC_HWDEV_AEQ_INITED:
		hinic_comm_aeqs_free(hwdev);
		break;
	case HINIC_HWDEV_CLP_INITED:
		hinic_comm_clp_to_mgmt_free(hwdev);
		break;
	default:
		break;
	}
}

#define HINIC_FUNC_STATE_BUSY_TIMEOUT	300
void hinic_uninit_comm_ch(struct hinic_hwdev *hwdev)
{
	enum hinic_hwdev_init_state init_state = HINIC_HWDEV_COMM_CH_INITED;
	int cnt;

	while (init_state > HINIC_HWDEV_NONE_INITED) {
		if (!test_bit(init_state, &hwdev->func_state)) {
			init_state--;
			continue;
		}
		clear_bit(init_state, &hwdev->func_state);

		cnt = 0;
		while (test_bit(HINIC_HWDEV_STATE_BUSY, &hwdev->func_state) &&
		       cnt++ <= HINIC_FUNC_STATE_BUSY_TIMEOUT)
			usleep_range(900, 1000);

		__uninit_comm_module(hwdev, init_state);

		init_state--;
	}
}

int hinic_slq_init(void *dev, int num_wqs)
{
	struct hinic_hwdev *hwdev = dev;
	int err;

	if (!dev)
		return -EINVAL;

	hwdev->wqs = kzalloc(sizeof(*hwdev->wqs), GFP_KERNEL);
	if (!hwdev->wqs)
		return -ENOMEM;

	err = hinic_wqs_alloc(hwdev->wqs, num_wqs, hwdev->dev_hdl);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to alloc wqs\n");
		kfree(hwdev->wqs);
		hwdev->wqs = NULL;
	}

	return err;
}
EXPORT_SYMBOL(hinic_slq_init);

void hinic_slq_uninit(void *dev)
{
	struct hinic_hwdev *hwdev = dev;

	if (!hwdev)
		return;

	hinic_wqs_free(hwdev->wqs);

	kfree(hwdev->wqs);
}
EXPORT_SYMBOL(hinic_slq_uninit);

int hinic_slq_alloc(void *dev, u16 wqebb_size, u16 q_depth, u16 page_size,
		    u64 *cla_addr, void **handle)
{
	struct hinic_hwdev *hwdev = dev;
	struct hinic_wq *wq;
	int err;

	if (!dev || !cla_addr || !handle)
		return -EINVAL;

	wq = kzalloc(sizeof(*wq), GFP_KERNEL);
	if (!wq)
		return -ENOMEM;

	err = hinic_wq_allocate(hwdev->wqs, wq, wqebb_size, hwdev->wq_page_size,
				q_depth, 0);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to alloc wq\n");
		kfree(wq);
		return -EFAULT;
	}

	*cla_addr = wq->block_paddr;
	*handle = wq;

	return 0;
}
EXPORT_SYMBOL(hinic_slq_alloc);

void hinic_slq_free(void *dev, void *handle)
{
	struct hinic_hwdev *hwdev = dev;

	if (!hwdev || !handle)
		return;

	hinic_wq_free(hwdev->wqs, handle);
	kfree(handle);
}
EXPORT_SYMBOL(hinic_slq_free);

u64 hinic_slq_get_addr(void *handle, u16 index)
{
	if (!handle)
		return 0;	/* NULL of wqe addr */

	return (u64)hinic_get_wqebb_addr(handle, index);
}
EXPORT_SYMBOL(hinic_slq_get_addr);

u64 hinic_slq_get_first_pageaddr(void *handle)
{
	struct hinic_wq *wq = handle;

	if (!handle)
		return 0;	/* NULL of wqe addr */

	return hinic_get_first_wqe_page_addr(wq);
}
EXPORT_SYMBOL(hinic_slq_get_first_pageaddr);

int hinic_func_tmr_bitmap_set(void *hwdev, bool en)
{
	struct hinic_func_tmr_bitmap_op bitmap_op = {0};
	u16 out_size = sizeof(bitmap_op);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &bitmap_op.func_idx);
	if (err)
		return err;

	bitmap_op.ppf_idx = hinic_ppf_idx(hwdev);
	if (en)
		bitmap_op.op_id = FUNC_TMR_BITMAP_ENABLE;
	else
		bitmap_op.op_id = FUNC_TMR_BITMAP_DISABLE;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_FUNC_TMR_BITMAT_SET,
				     &bitmap_op, sizeof(bitmap_op),
				     &bitmap_op, &out_size, 0);
	if (err || !out_size || bitmap_op.status) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to set timer bitmap, err: %d, status: 0x%x, out_size: 0x%x\n",
			err, bitmap_op.status, out_size);
		return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_func_tmr_bitmap_set);

int ppf_ht_gpa_set(struct hinic_hwdev *hwdev, struct hinic_page_addr *pg0,
		   struct hinic_page_addr *pg1)
{
	struct comm_info_ht_gpa_set ht_gpa_set = {0};
	u16 out_size = sizeof(ht_gpa_set);
	int ret;

	pg0->virt_addr = dma_zalloc_coherent(hwdev->dev_hdl,
					     HINIC_HT_GPA_PAGE_SIZE,
					     &pg0->phys_addr, GFP_KERNEL);
	if (!pg0->virt_addr) {
		sdk_err(hwdev->dev_hdl, "Alloc pg0 page addr failed\n");
		return -EFAULT;
	}

	pg1->virt_addr = dma_zalloc_coherent(hwdev->dev_hdl,
					     HINIC_HT_GPA_PAGE_SIZE,
					     &pg1->phys_addr, GFP_KERNEL);
	if (!pg1->virt_addr) {
		sdk_err(hwdev->dev_hdl, "Alloc pg1 page addr failed\n");
		return -EFAULT;
	}

	ht_gpa_set.page_pa0 = pg0->phys_addr;
	ht_gpa_set.page_pa1 = pg1->phys_addr;
	sdk_info(hwdev->dev_hdl, "PPF ht gpa set: page_addr0.pa=0x%llx, page_addr1.pa=0x%llx\n",
		 pg0->phys_addr, pg1->phys_addr);
	ret = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_PPF_HT_GPA_SET,
				     &ht_gpa_set, sizeof(ht_gpa_set),
				     &ht_gpa_set, &out_size, 0);
	if (ret || !out_size || ht_gpa_set.status) {
		sdk_warn(hwdev->dev_hdl, "PPF ht gpa set failed, ret: %d, status: 0x%x, out_size: 0x%x\n",
			 ret, ht_gpa_set.status, out_size);
		return -EFAULT;
	}

	hwdev->page_pa0.phys_addr = pg0->phys_addr;
	hwdev->page_pa0.virt_addr = pg0->virt_addr;

	hwdev->page_pa1.phys_addr = pg1->phys_addr;
	hwdev->page_pa1.virt_addr = pg1->virt_addr;

	return 0;
}

int hinic_ppf_ht_gpa_init(struct hinic_hwdev *hwdev)
{
	int ret;
	int i;
	int j;
	int size;

	struct hinic_page_addr page_addr0[HINIC_PPF_HT_GPA_SET_RETRY_TIMES];
	struct hinic_page_addr page_addr1[HINIC_PPF_HT_GPA_SET_RETRY_TIMES];

	size = HINIC_PPF_HT_GPA_SET_RETRY_TIMES * sizeof(page_addr0[0]);
	memset(page_addr0, 0, size);
	memset(page_addr1, 0, size);

	for (i = 0; i < HINIC_PPF_HT_GPA_SET_RETRY_TIMES; i++) {
		ret = ppf_ht_gpa_set(hwdev, &page_addr0[i], &page_addr1[i]);
		if (!ret)
			break;
	}

	for (j = 0; j < i; j++) {
		if (page_addr0[j].virt_addr) {
			dma_free_coherent(hwdev->dev_hdl,
					  HINIC_HT_GPA_PAGE_SIZE,
					  page_addr0[j].virt_addr,
					  page_addr0[j].phys_addr);
			page_addr0[j].virt_addr = NULL;
		}
		if (page_addr1[j].virt_addr) {
			dma_free_coherent(hwdev->dev_hdl,
					  HINIC_HT_GPA_PAGE_SIZE,
					  page_addr1[j].virt_addr,
					  page_addr1[j].phys_addr);
			page_addr1[j].virt_addr = NULL;
		}
	}

	if (i >= HINIC_PPF_HT_GPA_SET_RETRY_TIMES) {
		sdk_err(hwdev->dev_hdl, "PPF ht gpa init failed, retry times: %d\n",
			i);
		return -EFAULT;
	}

	return 0;
}

void hinic_ppf_ht_gpa_deinit(struct hinic_hwdev *hwdev)
{
	if (hwdev->page_pa0.virt_addr) {
		dma_free_coherent(hwdev->dev_hdl, HINIC_HT_GPA_PAGE_SIZE,
				  hwdev->page_pa0.virt_addr,
				  hwdev->page_pa0.phys_addr);
		hwdev->page_pa0.virt_addr = NULL;
	}

	if (hwdev->page_pa1.virt_addr) {
		dma_free_coherent(hwdev->dev_hdl, HINIC_HT_GPA_PAGE_SIZE,
				  hwdev->page_pa1.virt_addr,
				  hwdev->page_pa1.phys_addr);
		hwdev->page_pa1.virt_addr = NULL;
	}
}

static int set_ppf_tmr_status(struct hinic_hwdev *hwdev,
			      enum ppf_tmr_status status)
{
	struct hinic_ppf_tmr_op op = {0};
	u16 out_size = sizeof(op);
	int err = 0;

	if (!hwdev)
		return -EINVAL;

	if (hinic_func_type(hwdev) != TYPE_PPF)
		return -EFAULT;

	if (status == HINIC_PPF_TMR_FLAG_START) {
		err = hinic_ppf_ht_gpa_init(hwdev);
		if (err) {
			sdk_err(hwdev->dev_hdl, "PPF ht gpa init fail!\n");
			return -EFAULT;
		}
	} else {
		hinic_ppf_ht_gpa_deinit(hwdev);
	}

	op.op_id = status;
	op.ppf_idx = hinic_ppf_idx(hwdev);

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_PPF_TMR_SET, &op,
				     sizeof(op), &op, &out_size, 0);
	if (err || !out_size || op.status) {
		sdk_err(hwdev->dev_hdl, "Failed to set ppf timer, err: %d, status: 0x%x, out_size: 0x%x\n",
			err, op.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int hinic_ppf_tmr_start(void *hwdev)
{
	if (!hwdev) {
		pr_err("Hwdev pointer is NULL for starting ppf timer\n");
		return -EINVAL;
	}

	return set_ppf_tmr_status(hwdev, HINIC_PPF_TMR_FLAG_START);
}
EXPORT_SYMBOL(hinic_ppf_tmr_start);

int hinic_ppf_tmr_stop(void *hwdev)
{
	if (!hwdev) {
		pr_err("Hwdev pointer is NULL for stop ppf timer\n");
		return -EINVAL;
	}

	return set_ppf_tmr_status(hwdev, HINIC_PPF_TMR_FLAG_STOP);
}
EXPORT_SYMBOL(hinic_ppf_tmr_stop);

int mqm_eqm_try_alloc_mem(struct hinic_hwdev *hwdev, u32 page_size,
			  u32 page_num)
{
	struct hinic_page_addr *page_addr = hwdev->mqm_att.brm_srch_page_addr;
	u32 valid_num = 0;
	u32 flag = 1;
	u32 i = 0;

	for (i = 0; i < page_num; i++) {
		page_addr->virt_addr =
			dma_zalloc_coherent(hwdev->dev_hdl, page_size,
					    &page_addr->phys_addr, GFP_KERNEL);
		if (!page_addr->virt_addr) {
			flag = 0;
			break;
		}
		valid_num++;
		page_addr++;
	}

	if (flag == 1) {
		hwdev->mqm_att.page_size = page_size;
		hwdev->mqm_att.page_num = page_num;
	} else {
		page_addr = hwdev->mqm_att.brm_srch_page_addr;
		for (i = 0; i < valid_num; i++) {
			dma_free_coherent(hwdev->dev_hdl, page_size,
					  page_addr->virt_addr,
					  page_addr->phys_addr);
			page_addr++;
		}
		return -EFAULT;
	}

	return 0;
}

int mqm_eqm_alloc_page_mem(struct hinic_hwdev *hwdev)
{
	int ret = 0;

	/* apply for 64KB page, number is chunk_num/16 */
	ret = mqm_eqm_try_alloc_mem(hwdev, 64 * 1024,
				    hwdev->mqm_att.chunk_num >> 4);
	if (!ret)
		return 0;

	/* apply for 8KB page, number is chunk_num/2 */
	ret = mqm_eqm_try_alloc_mem(hwdev, 8 * 1024,
				    hwdev->mqm_att.chunk_num >> 1);
	if (!ret)
		return 0;

	/* apply for 4KB page, number is chunk_num */
	ret = mqm_eqm_try_alloc_mem(hwdev, 4 * 1024,
				    hwdev->mqm_att.chunk_num);
	if (!ret)
		return 0;

	return ret;
}

void mqm_eqm_free_page_mem(struct hinic_hwdev *hwdev)
{
	u32 i;
	struct hinic_page_addr *page_addr;
	u32 page_size;

	page_size = hwdev->mqm_att.page_size;
	page_addr = hwdev->mqm_att.brm_srch_page_addr;

	for (i = 0; i < hwdev->mqm_att.page_num; i++) {
		dma_free_coherent(hwdev->dev_hdl, page_size,
				  page_addr->virt_addr, page_addr->phys_addr);
		page_addr++;
	}
}

int mqm_eqm_set_cfg_2_hw(struct hinic_hwdev *hwdev, u32 valid)
{
	struct comm_info_eqm_cfg info_eqm_cfg = {0};
	u16 out_size = sizeof(info_eqm_cfg);
	int err;

	info_eqm_cfg.ppf_id = hinic_global_func_id_hw(hwdev);
	info_eqm_cfg.page_size = hwdev->mqm_att.page_size;
	info_eqm_cfg.valid     = valid;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_MQM_CFG_INFO_SET,
				     &info_eqm_cfg, sizeof(info_eqm_cfg),
				     &info_eqm_cfg, &out_size, 0);
	if (err || !out_size || info_eqm_cfg.status) {
		sdk_err(hwdev->dev_hdl, "Failed to init func table, err: %d, status: 0x%x, out_size: 0x%x\n",
			err, info_eqm_cfg.status, out_size);
		return -EFAULT;
	}

	return 0;
}

#define EQM_DATA_BUF_SIZE	1024

int mqm_eqm_set_page_2_hw(struct hinic_hwdev *hwdev)
{
	struct comm_info_eqm_search_gpa *info;
	struct hinic_page_addr *page_addr;
	void *send_buf;
	u16 send_buf_size;
	u32 i;
	u64 *gpa_hi52;
	u64 gpa;
	u32 num;
	u32 start_idx;
	int err = 0;
	u32 valid_page_num;
	u16 out_size;

	send_buf_size = sizeof(struct comm_info_eqm_search_gpa) +
			EQM_DATA_BUF_SIZE;
	send_buf = kzalloc(send_buf_size, GFP_KERNEL);
	if (!send_buf) {
		sdk_err(hwdev->dev_hdl, "Alloc virtual mem failed\r\n");
		return -EFAULT;
	}

	page_addr = hwdev->mqm_att.brm_srch_page_addr;
	info = (struct comm_info_eqm_search_gpa *)send_buf;
	valid_page_num = 0;

	gpa_hi52 = info->gpa_hi52;
	num = 0;
	start_idx = 0;
	for (i = 0; i < hwdev->mqm_att.page_num; i++) {
		gpa = page_addr->phys_addr >> 12;
		gpa_hi52[num] = gpa;
		num++;
		if (num == 128) {
			info->num = num;
			info->start_idx = start_idx;
			out_size = send_buf_size;
			err = hinic_msg_to_mgmt_sync(
				hwdev, HINIC_MOD_COMM,
				HINIC_MGMT_CMD_MQM_SRCH_GPA_SET,
				info, (u16)send_buf_size, info,
				&out_size, 0);
			if (err || !out_size || info->status) {
				sdk_err(hwdev->dev_hdl, "Set mqm srch gpa fail, err: %d, status: 0x%x, out_size: 0x%x\n",
					err, info->status, out_size);
				err = -EFAULT;
				goto set_page_2_hw_end;
			}

			gpa_hi52 = info->gpa_hi52;
			num = 0;
			start_idx = i + 1;
		}
		page_addr++;
		valid_page_num++;
	}

	if (0 != (valid_page_num & 0x7f)) {
		info->num = num;
		info->start_idx = start_idx;
		out_size = send_buf_size;
		err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
					     HINIC_MGMT_CMD_MQM_SRCH_GPA_SET,
					     info, (u16)send_buf_size,
					     info, &out_size, 0);
		if (err || !out_size || info->status) {
			sdk_err(hwdev->dev_hdl, "Set mqm srch gpa fail, err: %d, status: 0x%x, out_size: 0x%x\n",
				err, info->status, out_size);
			err = -EFAULT;
			goto set_page_2_hw_end;
		}
	}

set_page_2_hw_end:
	kfree(send_buf);
	return err;
}

int mqm_eqm_init(struct hinic_hwdev *hwdev)
{
	struct comm_info_eqm_fix info_eqm_fix = {0};
	u16 len = sizeof(info_eqm_fix);
	int ret;

	if (hwdev->hwif->attr.func_type != TYPE_PPF)
		return 0;

	ret = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_MQM_FIX_INFO_GET,
				     &info_eqm_fix, sizeof(info_eqm_fix),
				     &info_eqm_fix, &len, 0);
	if (ret || !len || info_eqm_fix.status) {
		sdk_err(hwdev->dev_hdl, "Get mqm fix info fail,err: %d, status: 0x%x, out_size: 0x%x\n",
			ret, info_eqm_fix.status, len);
		return -EFAULT;
	}
	if (!(info_eqm_fix.chunk_num))
		return 0;

	hwdev->mqm_att.chunk_num = info_eqm_fix.chunk_num;
	hwdev->mqm_att.search_gpa_num = info_eqm_fix.search_gpa_num;
	hwdev->mqm_att.page_size = 0;
	hwdev->mqm_att.page_num  = 0;

	hwdev->mqm_att.brm_srch_page_addr =
		kcalloc(hwdev->mqm_att.chunk_num,
			sizeof(struct hinic_page_addr), GFP_KERNEL);
	if (!(hwdev->mqm_att.brm_srch_page_addr)) {
		sdk_err(hwdev->dev_hdl, "Alloc virtual mem failed\r\n");
		return -EFAULT;
	}

	ret = mqm_eqm_alloc_page_mem(hwdev);
	if (ret) {
		sdk_err(hwdev->dev_hdl, "Alloc eqm page mem failed\r\n");
		goto err_page;
	}

	ret = mqm_eqm_set_page_2_hw(hwdev);
	if (ret) {
		sdk_err(hwdev->dev_hdl, "Set page to hw failed\r\n");
		goto err_ecmd;
	}

	ret = mqm_eqm_set_cfg_2_hw(hwdev, 1);
	if (ret) {
		sdk_err(hwdev->dev_hdl, "Set page to hw failed\r\n");
		goto err_ecmd;
	}

	return 0;

err_ecmd:
	mqm_eqm_free_page_mem(hwdev);

err_page:
	kfree(hwdev->mqm_att.brm_srch_page_addr);

	return ret;
}

void mqm_eqm_deinit(struct hinic_hwdev *hwdev)
{
	int ret;

	if (hwdev->hwif->attr.func_type != TYPE_PPF)
		return;

	if (!(hwdev->mqm_att.chunk_num))
		return;

	mqm_eqm_free_page_mem(hwdev);
	kfree(hwdev->mqm_att.brm_srch_page_addr);

	ret = mqm_eqm_set_cfg_2_hw(hwdev, 0);
	if (ret) {
		sdk_err(hwdev->dev_hdl, "Set mqm eqm cfg to chip fail! err: %d\n",
			ret);
		return;
	}

	hwdev->mqm_att.chunk_num = 0;
	hwdev->mqm_att.search_gpa_num = 0;
	hwdev->mqm_att.page_num = 0;
	hwdev->mqm_att.page_size = 0;
}

int hinic_ppf_ext_db_init(void *dev)
{
	struct hinic_hwdev *hwdev = dev;
	int ret;

	if (!dev)
		return -EINVAL;

	ret = mqm_eqm_init(hwdev);
	if (ret) {
		sdk_err(hwdev->dev_hdl, "MQM eqm init fail!\n");
		return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_ppf_ext_db_init);

int hinic_ppf_ext_db_deinit(void *dev)
{
	struct hinic_hwdev *hwdev = dev;

	if (!dev)
		return -EINVAL;

	if (hwdev->hwif->attr.func_type != TYPE_PPF)
		return -EFAULT;

	mqm_eqm_deinit(hwdev);

	return 0;
}
EXPORT_SYMBOL(hinic_ppf_ext_db_deinit);

int hinic_set_wq_page_size(struct hinic_hwdev *hwdev, u16 func_idx,
			   u32 page_size)
{
	struct hinic_wq_page_size page_size_info = {0};
	u16 out_size = sizeof(page_size_info);
	int err;

	page_size_info.func_idx = func_idx;
	page_size_info.ppf_idx = hinic_ppf_idx(hwdev);
	page_size_info.page_size = HINIC_PAGE_SIZE_HW(page_size);

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_PAGESIZE_SET,
				     &page_size_info, sizeof(page_size_info),
				     &page_size_info, &out_size, 0);
	if (err || !out_size || page_size_info.status) {
		sdk_err(hwdev->dev_hdl, "Failed to set wq page size, err: %d, status: 0x%x, out_size: 0x%0x\n",
			err, page_size_info.status, out_size);
		return -EFAULT;
	}

	return 0;
}

enum hinic_event_cmd {
	/* hilink event */
	HINIC_EVENT_LINK_STATUS_CHANGE = 1,
	HINIC_EVENT_LINK_ERR,
	HINIC_EVENT_CABLE_PLUG,
	HINIC_EVENT_HILINK_INFO,
	/* reserved for hilink */

	/* driver event, pf & vf communicate */
	HINIC_EVENT_HEARTBEAT_LOST = 31,
	HINIC_EVENT_SET_VF_COS,

	/* mgmt event */
	HINIC_EVENT_MGMT_FAULT = 61,
	HINIC_EVENT_MGMT_WATCHDOG,
	HINIC_EVENT_MGMT_FMW_ACT_NTC,
	HINIC_EVENT_MGMT_RESET,
	HINIC_EVENT_MGMT_PCIE_DFX,
	HINIC_EVENT_MCTP_HOST_INFO,
	HINIC_EVENT_MGMT_HEARTBEAT_EHD,
	HINIC_EVENT_SFP_INFO_REPORT,
	HINIC_EVENT_SFP_ABS_REPORT,

	HINIC_EVENT_MAX_TYPE,
};

struct hinic_event_convert {
	u8	mod;
	u8	cmd;

	enum hinic_event_cmd event;
};

static struct hinic_event_convert __event_convert[] = {
	/* hilink event */
	{
		.mod	= HINIC_MOD_L2NIC,
		.cmd	= HINIC_PORT_CMD_LINK_STATUS_REPORT,
		.event	= HINIC_EVENT_LINK_STATUS_CHANGE,
	},
	{
		.mod	= HINIC_MOD_L2NIC,
		.cmd	= HINIC_PORT_CMD_LINK_ERR_EVENT,
		.event	= HINIC_EVENT_LINK_ERR,
	},
	{
		.mod	= HINIC_MOD_L2NIC,
		.cmd	= HINIC_PORT_CMD_CABLE_PLUG_EVENT,
		.event	= HINIC_EVENT_CABLE_PLUG,
	},
	{
		.mod	= HINIC_MOD_HILINK,
		.cmd	= HINIC_HILINK_CMD_GET_LINK_INFO,
		.event	= HINIC_EVENT_HILINK_INFO,
	},

	/* driver triggered event */
	{
		.mod	= HINIC_MOD_L2NIC,
		.cmd	= HINIC_MGMT_CMD_HEART_LOST_REPORT,
		.event	= HINIC_EVENT_HEARTBEAT_LOST,
	},
	{
		.mod	= HINIC_MOD_L2NIC,
		.cmd	= HINIC_PORT_CMD_SET_VF_COS,
		.event	= HINIC_EVENT_SET_VF_COS,
	},

	/* mgmt event */
	{
		.mod	= HINIC_MOD_COMM,
		.cmd	= HINIC_MGMT_CMD_FAULT_REPORT,
		.event	= HINIC_EVENT_MGMT_FAULT,
	},
	{
		.mod	= HINIC_MOD_COMM,
		.cmd	= HINIC_MGMT_CMD_WATCHDOG_INFO,
		.event	= HINIC_EVENT_MGMT_WATCHDOG,
	},
	{
		.mod	= HINIC_MOD_COMM,
		.cmd	= HINIC_MGMT_CMD_FMW_ACT_NTC,
		.event	= HINIC_EVENT_MGMT_FMW_ACT_NTC,
	},
	{
		.mod	= HINIC_MOD_L2NIC,
		.cmd	= HINIC_PORT_CMD_MGMT_RESET,
		.event	= HINIC_EVENT_MGMT_RESET,
	},
	{
		.mod	= HINIC_MOD_COMM,
		.cmd	= HINIC_MGMT_CMD_PCIE_DFX_NTC,
		.event	= HINIC_EVENT_MGMT_PCIE_DFX,
	},
	{
		.mod	= HINIC_MOD_COMM,
		.cmd	= HINIC_MGMT_CMD_GET_HOST_INFO,
		.event	= HINIC_EVENT_MCTP_HOST_INFO,
	},
	{
		.mod	= HINIC_MOD_COMM,
		.cmd	= HINIC_MGMT_CMD_HEARTBEAT_EVENT,
		.event	= HINIC_EVENT_MGMT_HEARTBEAT_EHD,
	},
	{
		.mod	= HINIC_MOD_L2NIC,
		.cmd	= HINIC_PORT_CMD_GET_SFP_INFO,
		.event	= HINIC_EVENT_SFP_INFO_REPORT,
	},
	{
		.mod	= HINIC_MOD_L2NIC,
		.cmd	= HINIC_PORT_CMD_GET_SFP_ABS,
		.event	= HINIC_EVENT_SFP_ABS_REPORT,
	},
};

static enum hinic_event_cmd __get_event_type(u8 mod, u8 cmd)
{
	int idx;
	int arr_size = sizeof(__event_convert) / sizeof(__event_convert[0]);

	for (idx = 0; idx < arr_size; idx++) {
		if (__event_convert[idx].mod == mod &&
		    __event_convert[idx].cmd == cmd)
			return __event_convert[idx].event;
	}

	return HINIC_EVENT_MAX_TYPE;
}

bool hinic_mgmt_event_ack_first(u8 mod, u8 cmd)
{
	if ((mod == HINIC_MOD_COMM && cmd == HINIC_MGMT_CMD_GET_HOST_INFO) ||
	    (mod == HINIC_MOD_COMM && cmd == HINIC_MGMT_CMD_HEARTBEAT_EVENT))
		return false;

	if (mod == HINIC_MOD_COMM || mod == HINIC_MOD_L2NIC ||
	    mod == HINIC_MOD_HILINK)
		return true;

	return false;
}

#define FAULT_SHOW_STR_LEN 16
static void fault_report_show(struct hinic_hwdev *hwdev,
			      struct hinic_fault_event *event)
{
	char fault_type[FAULT_TYPE_MAX][FAULT_SHOW_STR_LEN + 1] = {
		"chip", "ucode", "mem rd timeout", "mem wr timeout",
		"reg rd timeout", "reg wr timeout", "phy fault"};
	char fault_level[FAULT_LEVEL_MAX][FAULT_SHOW_STR_LEN + 1] = {
		"fatal", "reset", "flr", "general", "suggestion"};
	char type_str[FAULT_SHOW_STR_LEN + 1];
	char level_str[FAULT_SHOW_STR_LEN + 1];
	u8 level;
	u32 pos, base;
	struct hinic_fault_event_stats *fault;
	u8 node_id;

	sdk_err(hwdev->dev_hdl, "Fault event report received, func_id: %d.\n",
		hinic_global_func_id(hwdev));

	memset(type_str, 0, FAULT_SHOW_STR_LEN + 1);
	if (event->type < FAULT_TYPE_MAX)
		strncpy(type_str, fault_type[event->type], FAULT_SHOW_STR_LEN);
	else
		strncpy(type_str, "Unknown", FAULT_SHOW_STR_LEN);

	sdk_err(hwdev->dev_hdl, "Fault type: %d [%s]\n", event->type, type_str);
	sdk_err(hwdev->dev_hdl, "Fault val[0]: 0x%08x, val[1]: 0x%08x, val[2]: 0x%08x, val[3]: 0x%08x\n",
		event->event.val[0], event->event.val[1], event->event.val[2],
		event->event.val[3]);

	fault = &hwdev->hw_stats.fault_event_stats;

	switch (event->type) {
	case FAULT_TYPE_CHIP:
		memset(level_str, 0, FAULT_SHOW_STR_LEN + 1);
		level = event->event.chip.err_level;
		if (level < FAULT_LEVEL_MAX)
			strncpy(level_str, fault_level[level],
				FAULT_SHOW_STR_LEN);
		else
			strncpy(level_str, "Unknown", FAULT_SHOW_STR_LEN);

		if (level == FAULT_LEVEL_SERIOUS_FLR) {
			sdk_err(hwdev->dev_hdl, "err_level: %d [%s], flr func_id: %d\n",
				level, level_str, event->event.chip.func_id);
			atomic_inc(&fault->fault_type_stat[event->type]);
		}
		sdk_err(hwdev->dev_hdl, "module_id: 0x%x, err_type: 0x%x, err_level: %d[%s], err_csr_addr: 0x%08x, err_csr_value: 0x%08x\n",
			event->event.chip.node_id,
			event->event.chip.err_type, level, level_str,
			event->event.chip.err_csr_addr,
			event->event.chip.err_csr_value);

		node_id = event->event.chip.node_id;
		atomic_inc(&fault->chip_fault_stats[node_id][level]);

		base = event->event.chip.node_id * FAULT_LEVEL_MAX *
		       HINIC_CHIP_ERROR_TYPE_MAX;
		pos = base + HINIC_CHIP_ERROR_TYPE_MAX * level +
		      event->event.chip.err_type;
		if (pos < HINIC_CHIP_FAULT_SIZE)
			hwdev->chip_fault_stats[pos]++;
		break;
	case FAULT_TYPE_UCODE:
		atomic_inc(&fault->fault_type_stat[event->type]);

		sdk_err(hwdev->dev_hdl, "cause_id: %d, core_id: %d, c_id: %d, epc: 0x%08x\n",
			event->event.ucode.cause_id, event->event.ucode.core_id,
			event->event.ucode.c_id, event->event.ucode.epc);
		break;
	case FAULT_TYPE_MEM_RD_TIMEOUT:
	case FAULT_TYPE_MEM_WR_TIMEOUT:
		atomic_inc(&fault->fault_type_stat[event->type]);

		sdk_err(hwdev->dev_hdl, "err_csr_ctrl: 0x%08x, err_csr_data: 0x%08x, ctrl_tab: 0x%08x, mem_index: 0x%08x\n",
			event->event.mem_timeout.err_csr_ctrl,
			event->event.mem_timeout.err_csr_data,
			event->event.mem_timeout.ctrl_tab,
			event->event.mem_timeout.mem_index);
		break;
	case FAULT_TYPE_REG_RD_TIMEOUT:
	case FAULT_TYPE_REG_WR_TIMEOUT:
		atomic_inc(&fault->fault_type_stat[event->type]);
		sdk_err(hwdev->dev_hdl, "err_csr:       0x%08x\n",
			event->event.reg_timeout.err_csr);
		break;
	case FAULT_TYPE_PHY_FAULT:
		atomic_inc(&fault->fault_type_stat[event->type]);
		sdk_err(hwdev->dev_hdl, "op_type: %u, port_id: %u, dev_ad: %u, csr_addr: 0x%08x, op_data: 0x%08x\n",
			event->event.phy_fault.op_type,
			event->event.phy_fault.port_id,
			event->event.phy_fault.dev_ad,
			event->event.phy_fault.csr_addr,
			event->event.phy_fault.op_data);
		break;
	default:
		break;
	}
}

static void hinic_refresh_history_fault(struct hinic_hwdev *hwdev,
					struct hinic_fault_recover_info *info)
{
	if (!hwdev->history_fault_flag) {
		hwdev->history_fault_flag = true;
		memcpy(&hwdev->history_fault, info,
		       sizeof(struct hinic_fault_recover_info));
	} else {
		if (hwdev->history_fault.fault_lev >= info->fault_lev)
			memcpy(&hwdev->history_fault, info,
			       sizeof(struct hinic_fault_recover_info));
	}
}

void hinic_migrate_report(void *dev)
{
	struct hinic_hwdev *hwdev = (struct hinic_hwdev *)dev;
	struct hinic_event_info event_info = {0};

	if (!dev)
		return;

	event_info.type = HINIC_EVENT_INIT_MIGRATE_PF;
	if (hwdev->event_callback)
		hwdev->event_callback(hwdev->event_pri_handle, &event_info);
}
EXPORT_SYMBOL(hinic_migrate_report);

static void fault_event_handler(struct hinic_hwdev *hwdev, void *buf_in,
				u16 in_size, void *buf_out, u16 *out_size)
{
	struct hinic_cmd_fault_event *fault_event;
	struct hinic_event_info event_info;
	struct hinic_fault_info_node *fault_node;

	if (in_size != sizeof(*fault_event)) {
		sdk_err(hwdev->dev_hdl, "Invalid fault event report, length: %d, should be %ld.\n",
			in_size, sizeof(*fault_event));
		return;
	}

	fault_event = buf_in;
	fault_report_show(hwdev, &fault_event->event);

	if (hwdev->event_callback) {
		event_info.type = HINIC_EVENT_FAULT;
		memcpy(&event_info.info, &fault_event->event,
		       sizeof(event_info.info));

		hwdev->event_callback(hwdev->event_pri_handle, &event_info);
	}

	/* refresh history fault info */
	fault_node = kzalloc(sizeof(*fault_node), GFP_KERNEL);
	if (!fault_node) {
		sdk_err(hwdev->dev_hdl, "Malloc fault node memory failed\n");
		return;
	}

	if (fault_event->event.type <= FAULT_TYPE_REG_WR_TIMEOUT)
		fault_node->info.fault_src = fault_event->event.type;
	else if (fault_event->event.type == FAULT_TYPE_PHY_FAULT)
		fault_node->info.fault_src = HINIC_FAULT_SRC_HW_PHY_FAULT;

	if (fault_node->info.fault_src == HINIC_FAULT_SRC_HW_MGMT_CHIP)
		fault_node->info.fault_lev =
					fault_event->event.event.chip.err_level;
	else
		fault_node->info.fault_lev = FAULT_LEVEL_FATAL;

	memcpy(&fault_node->info.fault_data.hw_mgmt, &fault_event->event.event,
	       sizeof(union hinic_fault_hw_mgmt));
	hinic_refresh_history_fault(hwdev, &fault_node->info);

	down(&hwdev->fault_list_sem);
	kfree(fault_node);
	up(&hwdev->fault_list_sem);

	queue_work(hwdev->workq, &hwdev->fault_work);
}

static void heartbeat_lost_event_handler(struct hinic_hwdev *hwdev)
{
	struct hinic_fault_info_node *fault_node;
	struct hinic_event_info event_info = {0};

	atomic_inc(&hwdev->hw_stats.heart_lost_stats);
	sdk_err(hwdev->dev_hdl, "Heart lost report received, func_id: %d\n",
		hinic_global_func_id(hwdev));

	if (hwdev->event_callback) {
		event_info.type = HINIC_EVENT_HEART_LOST;
		hwdev->event_callback(hwdev->event_pri_handle, &event_info);
	}

	/* refresh history fault info */
	fault_node = kzalloc(sizeof(*fault_node), GFP_KERNEL);
	if (!fault_node) {
		sdk_err(hwdev->dev_hdl, "Malloc fault node memory failed\n");
		return;
	}

	fault_node->info.fault_src = HINIC_FAULT_SRC_HOST_HEARTBEAT_LOST;
	fault_node->info.fault_lev = FAULT_LEVEL_FATAL;
	hinic_refresh_history_fault(hwdev, &fault_node->info);

	down(&hwdev->fault_list_sem);
	kfree(fault_node);
	up(&hwdev->fault_list_sem);

	queue_work(hwdev->workq, &hwdev->fault_work);
}

static void link_status_event_handler(struct hinic_hwdev *hwdev, void *buf_in,
				      u16 in_size, void *buf_out, u16 *out_size)
{
	struct hinic_port_link_status *link_status, *ret_link_status;
	struct hinic_event_info event_info = {0};
	struct hinic_event_link_info *link_info = &event_info.link_info;
	struct nic_port_info port_info = {0};
	int err;

	/* Ignore link change event */
	if (FUNC_FORCE_LINK_UP(hwdev))
		return;

	link_status = buf_in;
	sdk_info(hwdev->dev_hdl, "Link status report received, func_id: %d, status: %d\n",
		 hinic_global_func_id(hwdev), link_status->link);

	if (link_status->link)
		atomic_inc(&hwdev->hw_stats.link_event_stats.link_up_stats);
	else
		atomic_inc(&hwdev->hw_stats.link_event_stats.link_down_stats);

	/* link event reported only after set vport enable */
	if (hinic_func_type(hwdev) != TYPE_VF &&
	    link_status->link == HINIC_EVENT_LINK_UP) {
		err = hinic_get_port_info(hwdev, &port_info);
		if (err) {
			nic_warn(hwdev->dev_hdl, "Failed to get port info\n");
		} else {
			link_info->valid = 1;
			link_info->port_type = port_info.port_type;
			link_info->autoneg_cap = port_info.autoneg_cap;
			link_info->autoneg_state = port_info.autoneg_state;
			link_info->duplex = port_info.duplex;
			link_info->speed = port_info.speed;
			hinic_refresh_nic_cfg(hwdev, &port_info);
		}
	}

	if (!hwdev->event_callback)
		return;

	event_info.type = link_status->link ?
			HINIC_EVENT_LINK_UP : HINIC_EVENT_LINK_DOWN;

	hwdev->event_callback(hwdev->event_pri_handle, &event_info);
	if (hinic_func_type(hwdev) != TYPE_VF) {
		hinic_notify_all_vfs_link_changed(hwdev, link_status->link);
		ret_link_status = buf_out;
		ret_link_status->status = 0;
		*out_size = sizeof(*ret_link_status);
	}
}

static void module_status_event(struct hinic_hwdev *hwdev,
				enum hinic_event_cmd cmd, void *buf_in,
				u16 in_size, void *buf_out, u16 *out_size)
{
	struct hinic_cable_plug_event *plug_event;
	struct hinic_link_err_event *link_err;
	struct hinic_event_info event_info = {0};
	struct hinic_port_routine_cmd *rt_cmd;
	struct card_node *chip_node = hwdev->chip_node;

	event_info.type = HINIC_EVENT_PORT_MODULE_EVENT;

	if (cmd == HINIC_EVENT_CABLE_PLUG) {
		plug_event = buf_in;

		if (plug_event->port_id < HINIC_MAX_PORT_ID) {
			rt_cmd = &chip_node->rt_cmd[plug_event->port_id];
			mutex_lock(&chip_node->sfp_mutex);
			rt_cmd->up_send_sfp_abs = false;
			rt_cmd->up_send_sfp_info = false;
			mutex_unlock(&chip_node->sfp_mutex);
		}

		event_info.module_event.type = plug_event->plugged ?
					HINIC_PORT_MODULE_CABLE_PLUGGED :
					HINIC_PORT_MODULE_CABLE_UNPLUGGED;

		*out_size = sizeof(*plug_event);
		plug_event = buf_out;
		plug_event->status = 0;
	} else if (cmd == HINIC_EVENT_LINK_ERR) {
		link_err = buf_in;

		event_info.module_event.type = HINIC_PORT_MODULE_LINK_ERR;
		event_info.module_event.err_type = link_err->err_type;

		*out_size = sizeof(*link_err);
		link_err = buf_out;
		link_err->status = 0;
	} else {
		sdk_warn(hwdev->dev_hdl, "Unknown module event: %d\n", cmd);
		return;
	}

	if (!hwdev->event_callback)
		return;

	hwdev->event_callback(hwdev->event_pri_handle, &event_info);
}

void hinic_notify_dcb_state_event(struct hinic_hwdev *hwdev,
				  struct hinic_dcb_state *dcb_state)
{
	struct hinic_event_info event_info = {0};

	sdk_info(hwdev->dev_hdl, "DCB %s, default cos %d, up2cos %d%d%d%d%d%d%d%d\n",
		 dcb_state->dcb_on ? "on" : "off", dcb_state->default_cos,
		 dcb_state->up_cos[0], dcb_state->up_cos[1],
		 dcb_state->up_cos[2], dcb_state->up_cos[3],
		 dcb_state->up_cos[4], dcb_state->up_cos[5],
		 dcb_state->up_cos[6], dcb_state->up_cos[7]);

	/* Saved in sdk for statefull module */
	hinic_save_dcb_state(hwdev, dcb_state);

	if (!hwdev->event_callback)
		return;

	event_info.type = HINIC_EVENT_DCB_STATE_CHANGE;
	memcpy(&event_info.dcb_state, dcb_state, sizeof(event_info.dcb_state));
	hwdev->event_callback(hwdev->event_pri_handle, &event_info);
}

static void sw_watchdog_timeout_info_show(struct hinic_hwdev *hwdev,
					  void *buf_in, u16 in_size,
					  void *buf_out, u16 *out_size)
{
	struct hinic_mgmt_watchdog_info *watchdog_info;
	u32 *dump_addr, *reg, stack_len, i, j;

	if (in_size != sizeof(*watchdog_info)) {
		sdk_err(hwdev->dev_hdl, "Invalid mgmt watchdog report, length: %d, should be %ld.\n",
			in_size, sizeof(*watchdog_info));
		return;
	}

	watchdog_info = buf_in;

	sdk_err(hwdev->dev_hdl, "Mgmt deadloop time: 0x%x 0x%x, task id: 0x%x, sp: 0x%x\n",
		watchdog_info->curr_time_h, watchdog_info->curr_time_l,
		watchdog_info->task_id, watchdog_info->sp);
	sdk_err(hwdev->dev_hdl, "Stack current used: 0x%x, peak used: 0x%x, overflow flag: 0x%x, top: 0x%x, bottom: 0x%x\n",
		watchdog_info->curr_used, watchdog_info->peak_used,
		watchdog_info->is_overflow, watchdog_info->stack_top,
		watchdog_info->stack_bottom);

	sdk_err(hwdev->dev_hdl, "Mgmt pc: 0x%08x, lr: 0x%08x, cpsr:0x%08x\n",
		watchdog_info->pc, watchdog_info->lr, watchdog_info->cpsr);

	sdk_err(hwdev->dev_hdl, "Mgmt register info\n");

	for (i = 0; i < 3; i++) {
		reg = watchdog_info->reg + (u64)(u32)(4 * i);
		sdk_err(hwdev->dev_hdl, "0x%08x 0x%08x 0x%08x 0x%08x\n",
			*(reg), *(reg + 1), *(reg + 2), *(reg + 3));
	}

	sdk_err(hwdev->dev_hdl, "0x%08x\n", watchdog_info->reg[12]);

	if (watchdog_info->stack_actlen <= 1024) {
		stack_len = watchdog_info->stack_actlen;
	} else {
		sdk_err(hwdev->dev_hdl, "Oops stack length: 0x%x is wrong\n",
			watchdog_info->stack_actlen);
		stack_len = 1024;
	}

	sdk_err(hwdev->dev_hdl, "Mgmt dump stack, 16Bytes per line(start from sp)\n");
	for (i = 0; i < (stack_len / 16); i++) {
		dump_addr = (u32 *)(watchdog_info->data + ((u64)(u32)(i * 16)));
		sdk_err(hwdev->dev_hdl, "0x%08x 0x%08x 0x%08x 0x%08x\n",
			*dump_addr, *(dump_addr + 1), *(dump_addr + 2),
			*(dump_addr + 3));
	}

	for (j = 0; j < ((stack_len % 16) / 4); j++) {
		dump_addr = (u32 *)(watchdog_info->data +
			    ((u64)(u32)(i * 16 + j * 4)));
		sdk_err(hwdev->dev_hdl, "0x%08x ", *dump_addr);
	}

	*out_size = sizeof(*watchdog_info);
	watchdog_info = buf_out;
	watchdog_info->status = 0;
}

static void mgmt_watchdog_timeout_event_handler(struct hinic_hwdev *hwdev,
						void *buf_in, u16 in_size,
						void *buf_out, u16 *out_size)
{
	struct hinic_fault_info_node *fault_node;

	sw_watchdog_timeout_info_show(hwdev, buf_in, in_size,
				      buf_out, out_size);

	/* refresh history fault info */
	fault_node = kzalloc(sizeof(*fault_node), GFP_KERNEL);
	if (!fault_node) {
		sdk_err(hwdev->dev_hdl, "Malloc fault node memory failed\n");
		return;
	}

	fault_node->info.fault_src = HINIC_FAULT_SRC_MGMT_WATCHDOG;
	fault_node->info.fault_lev = FAULT_LEVEL_FATAL;
	hinic_refresh_history_fault(hwdev, &fault_node->info);

	down(&hwdev->fault_list_sem);
	kfree(fault_node);
	up(&hwdev->fault_list_sem);

	queue_work(hwdev->workq, &hwdev->fault_work);
}

static void port_sfp_info_event(struct hinic_hwdev *hwdev, void *buf_in,
				u16 in_size, void *buf_out, u16 *out_size)
{
	struct hinic_cmd_get_sfp_qsfp_info *sfp_info = buf_in;
	struct hinic_port_routine_cmd *rt_cmd;
	struct card_node *chip_node = hwdev->chip_node;

	if (in_size != sizeof(*sfp_info)) {
		sdk_err(hwdev->dev_hdl, "Invalid sfp info cmd, length: %d, should be %ld\n",
			in_size, sizeof(*sfp_info));
		return;
	}

	if (sfp_info->port_id >= HINIC_MAX_PORT_ID) {
		sdk_err(hwdev->dev_hdl, "Invalid sfp port id: %d, max port is %d\n",
			sfp_info->port_id, HINIC_MAX_PORT_ID - 1);
		return;
	}

	rt_cmd = &chip_node->rt_cmd[sfp_info->port_id];
	mutex_lock(&chip_node->sfp_mutex);
	memcpy(&rt_cmd->sfp_info, sfp_info, sizeof(rt_cmd->sfp_info));
	rt_cmd->up_send_sfp_info = true;
	mutex_unlock(&chip_node->sfp_mutex);
}

static void port_sfp_abs_event(struct hinic_hwdev *hwdev, void *buf_in,
			       u16 in_size, void *buf_out, u16 *out_size)
{
	struct hinic_cmd_get_light_module_abs *sfp_abs = buf_in;
	struct hinic_port_routine_cmd *rt_cmd;
	struct card_node *chip_node = hwdev->chip_node;

	if (in_size != sizeof(*sfp_abs)) {
		sdk_err(hwdev->dev_hdl, "Invalid sfp absent cmd, length: %d, should be %ld\n",
			in_size, sizeof(*sfp_abs));
		return;
	}

	if (sfp_abs->port_id >= HINIC_MAX_PORT_ID) {
		sdk_err(hwdev->dev_hdl, "Invalid sfp port id: %d, max port is %d\n",
			sfp_abs->port_id, HINIC_MAX_PORT_ID - 1);
		return;
	}

	rt_cmd = &chip_node->rt_cmd[sfp_abs->port_id];
	mutex_lock(&chip_node->sfp_mutex);
	memcpy(&rt_cmd->abs, sfp_abs, sizeof(rt_cmd->abs));
	rt_cmd->up_send_sfp_abs = true;
	mutex_unlock(&chip_node->sfp_mutex);
}

static void mgmt_reset_event_handler(struct hinic_hwdev *hwdev)
{
	sdk_info(hwdev->dev_hdl, "Mgmt is reset\n");

	/* mgmt reset only occurred when hot update or Mgmt deadloop,
	 * if Mgmt deadloop, mgmt will report an event with
	 * mod=0, cmd=0x56, and will reported fault to os,
	 * so mgmt reset event don't need to report fault
	 */
}

static void hinic_fmw_act_ntc_handler(struct hinic_hwdev *hwdev,
				      void *buf_in, u16 in_size,
				      void *buf_out, u16 *out_size)
{
	struct hinic_event_info event_info = {0};
	struct hinic_fmw_act_ntc *notice_info;

	if (in_size != sizeof(*notice_info)) {
		sdk_err(hwdev->dev_hdl, "Invalid mgmt firmware active notice, length: %d, should be %ld.\n",
			in_size, sizeof(*notice_info));
		return;
	}

	/* mgmt is activated now, restart heartbeat enhanced detection */
	__set_heartbeat_ehd_detect_delay(hwdev, 0);

	if (!hwdev->event_callback)
		return;

	event_info.type = HINIC_EVENT_FMW_ACT_NTC;
	hwdev->event_callback(hwdev->event_pri_handle, &event_info);

	*out_size = sizeof(*notice_info);
	notice_info = buf_out;
	notice_info->status = 0;
}

static void hinic_pcie_dfx_event_handler(struct hinic_hwdev *hwdev,
					 void *buf_in, u16 in_size,
					 void *buf_out, u16 *out_size)
{
	struct hinic_pcie_dfx_ntc *notice_info = buf_in;
	struct hinic_pcie_dfx_info *dfx_info;
	u16 size = 0;
	u16 cnt = 0;
	u32 num = 0;
	u32 i, j;
	int err;
	u32 *reg;

	if (in_size != sizeof(*notice_info)) {
		sdk_err(hwdev->dev_hdl, "Invalid mgmt firmware active notice, length: %d, should be %ld.\n",
			in_size, sizeof(*notice_info));
		return;
	}

	dfx_info = kzalloc(sizeof(*dfx_info), GFP_KERNEL);
	if (!dfx_info) {
		sdk_err(hwdev->dev_hdl, "Malloc dfx_info memory failed\n");
		return;
	}

	((struct hinic_pcie_dfx_ntc *)buf_out)->status = 0;
	*out_size = sizeof(*notice_info);
	num = (u32)(notice_info->len / 1024);
	sdk_info(hwdev->dev_hdl, "INFO LEN: %d\n", notice_info->len);
	sdk_info(hwdev->dev_hdl, "PCIE DFX:\n");
	dfx_info->host_id = 0;
	for (i = 0; i < num; i++) {
		dfx_info->offset = i * MAX_PCIE_DFX_BUF_SIZE;
		if (i == (num - 1))
			dfx_info->last = 1;
		size = sizeof(*dfx_info);
		err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
					     HINIC_MGMT_CMD_PCIE_DFX_GET,
					     dfx_info, sizeof(*dfx_info),
					     dfx_info, &size, 0);
		if (err || dfx_info->status || !size) {
			sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
				"Failed to get pcie dfx info, err: %d, status: 0x%x, out size: 0x%x\n",
				err, dfx_info->status, size);
			kfree(dfx_info);
			return;
		}

		reg = (u32 *)dfx_info->data;
		for (j = 0; j < 256; j = j + 8) {
			sdk_info(hwdev->dev_hdl, "0x%04x: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
				 cnt, reg[j], reg[(u32)(j + 1)],
				 reg[(u32)(j + 2)], reg[(u32)(j + 3)],
				 reg[(u32)(j + 4)], reg[(u32)(j + 5)],
				 reg[(u32)(j + 6)], reg[(u32)(j + 7)]);
			cnt = cnt + 32;
		}
		memset(dfx_info->data, 0, MAX_PCIE_DFX_BUF_SIZE);
	}
	kfree(dfx_info);
}

struct hinic_mctp_get_host_info {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u8 huawei_cmd;
	u8 sub_cmd;
	u8 rsvd[2];

	u32 actual_len;

	u8 data[1024];
};

static void hinic_mctp_get_host_info_event_handler(struct hinic_hwdev *hwdev,
						   void *buf_in, u16 in_size,
						   void *buf_out, u16 *out_size)
{
	struct hinic_event_info event_info = {0};
	struct hinic_mctp_get_host_info *mctp_out, *mctp_in;
	struct hinic_mctp_host_info *host_info;

	if (in_size != sizeof(*mctp_in)) {
		sdk_err(hwdev->dev_hdl, "Invalid mgmt mctp info, length: %d, should be %ld\n",
			in_size, sizeof(*mctp_in));
		return;
	}

	*out_size = sizeof(*mctp_out);
	mctp_out = buf_out;
	mctp_out->status = 0;

	if (!hwdev->event_callback) {
		mctp_out->status = HINIC_MGMT_STATUS_ERR_INIT;
		return;
	}

	mctp_in = buf_in;
	host_info = &event_info.mctp_info;
	host_info->major_cmd = mctp_in->huawei_cmd;
	host_info->sub_cmd = mctp_in->sub_cmd;
	host_info->data = mctp_out->data;

	event_info.type = HINIC_EVENT_MCTP_GET_HOST_INFO;
	hwdev->event_callback(hwdev->event_pri_handle, &event_info);

	mctp_out->actual_len = host_info->data_len;
}

char *__hw_to_char_fec[HILINK_FEC_MAX_TYPE] = {"RS-FEC", "BASE-FEC", "NO-FEC"};

char *__hw_to_char_port_type[LINK_PORT_MAX_TYPE] = {
	"Unknown", "Fibre", "Electric", "Direct Attach Copper", "AOC",
	"Back plane", "BaseT"
};

static void __print_cable_info(struct hinic_hwdev *hwdev,
			       struct hinic_link_info *info)
{
	char tmp_str[CAP_INFO_MAC_LEN] = {0};
	char tmp_vendor[VENDOR_MAX_LEN] = {0};
	char *port_type = "Unknown port type";
	int i;
	int err = 0;

	if (info->cable_absent) {
		sdk_info(hwdev->dev_hdl, "Cable unpresent\n");
		return;
	}

	if (info->port_type < LINK_PORT_MAX_TYPE)
		port_type = __hw_to_char_port_type[info->port_type];
	else
		sdk_info(hwdev->dev_hdl, "Unknown port type: %u\n",
			 info->port_type);
	if (info->port_type == LINK_PORT_FIBRE) {
		if (info->port_sub_type == FIBRE_SUBTYPE_SR)
			port_type = "Fibre-SR";
		else if (info->port_sub_type == FIBRE_SUBTYPE_LR)
			port_type = "Fibre-LR";
	}

	for (i = sizeof(info->vendor_name) - 1; i >= 0; i--) {
		if (info->vendor_name[i] == ' ')
			info->vendor_name[i] = '\0';
		else
			break;
	}

	memcpy(tmp_vendor, info->vendor_name,
	       sizeof(info->vendor_name));
	err = snprintf(tmp_str, sizeof(tmp_str),
		       "Vendor: %s, %s, length: %um, max_speed: %uGbps",
		       tmp_vendor, port_type, info->cable_length,
		       info->cable_max_speed);
	if (err <= 0 || err >= CAP_INFO_MAC_LEN) {
		sdk_err(hwdev->dev_hdl,
			"Failed snprintf cable vendor info, function return(%d) and dest_len(%d)\n",
			err, CAP_INFO_MAC_LEN);
		return;
	}

	if (info->port_type == LINK_PORT_FIBRE ||
	    info->port_type == LINK_PORT_AOC) {
		err = snprintf(tmp_str, sizeof(tmp_str),
			       "%s, %s, Temperature: %u", tmp_str,
			       info->sfp_type ? "SFP" : "QSFP",
			       info->cable_temp);
		if (err <= 0 || err >= CAP_INFO_MAC_LEN) {
			sdk_err(hwdev->dev_hdl,
				"Failed snprintf cable Temp, function return(%d) and dest_len(%d)\n",
				err, CAP_INFO_MAC_LEN);
			return;
		}

		if (info->sfp_type) {
			err = snprintf(tmp_str, sizeof(tmp_str),
				       "%s, rx power: %uuW, tx power: %uuW",
				       tmp_str, info->power[0], info->power[1]);
		} else {
			err = snprintf(tmp_str, sizeof(tmp_str),
				       "%s, rx power: %uuw %uuW %uuW %uuW",
				       tmp_str, info->power[0], info->power[1],
				       info->power[2], info->power[3]);
		}
		if (err <= 0 || err >= CAP_INFO_MAC_LEN) {
			sdk_err(hwdev->dev_hdl,
				"Failed snprintf power info, function return(%d) and dest_len(%d)\n",
				err, CAP_INFO_MAC_LEN);
			return;
		}
	}

	sdk_info(hwdev->dev_hdl, "Cable information: %s\n",
		 tmp_str);
}

static void __hi30_lane_info(struct hinic_hwdev *hwdev,
			     struct hilink_lane *lane)
{
	struct hi30_ffe_data *ffe_data;
	struct hi30_ctle_data *ctle_data;

	ffe_data = (struct hi30_ffe_data *)lane->hi30_ffe;
	ctle_data = (struct hi30_ctle_data *)lane->hi30_ctle;

	sdk_info(hwdev->dev_hdl, "TX_FFE: PRE1=%s%d; PRE2=%s%d; MAIN=%d; POST1=%s%d; POST1X=%s%d\n",
		 (ffe_data->PRE1 & 0x10) ? "-" : "",
		 (int)(ffe_data->PRE1 & 0xf),
		 (ffe_data->PRE2 & 0x10) ? "-" : "",
		 (int)(ffe_data->PRE2 & 0xf),
		 (int)ffe_data->MAIN,
		 (ffe_data->POST1 & 0x10) ? "-" : "",
		 (int)(ffe_data->POST1 & 0xf),
		 (ffe_data->POST2 & 0x10) ? "-" : "",
		 (int)(ffe_data->POST2 & 0xf));
	sdk_info(hwdev->dev_hdl, "RX_CTLE: Gain1~3=%u %u %u; Boost1~3=%u %u %u; Zero1~3=%u %u %u; Squelch1~3=%u %u %u\n",
		 ctle_data->ctlebst[0], ctle_data->ctlebst[1],
		 ctle_data->ctlebst[2], ctle_data->ctlecmband[0],
		 ctle_data->ctlecmband[1], ctle_data->ctlecmband[2],
		 ctle_data->ctlermband[0], ctle_data->ctlermband[1],
		 ctle_data->ctlermband[2], ctle_data->ctleza[0],
		 ctle_data->ctleza[1], ctle_data->ctleza[2]);
}

static void __print_hi30_status(struct hinic_hwdev *hwdev,
				struct hinic_link_info *info)
{
	struct hilink_lane *lane;
	int lane_used_num = 0, i;

	for (i = 0; i < HILINK_MAX_LANE; i++) {
		lane = (struct hilink_lane *)(info->lane2 + i * sizeof(*lane));
		if (!lane->lane_used)
			continue;

		__hi30_lane_info(hwdev, lane);
		lane_used_num++;
	}

	/* in new firmware, all lane info setted in lane2 */
	if (lane_used_num)
		return;

	/* compatible old firmware */
	__hi30_lane_info(hwdev, (struct hilink_lane *)info->lane1);
}

static void __print_link_info(struct hinic_hwdev *hwdev,
			      struct hinic_link_info *info,
			      enum hilink_info_print_event type)
{
	char *fec = "None";

	if (info->fec < HILINK_FEC_MAX_TYPE)
		fec = __hw_to_char_fec[info->fec];
	else
		sdk_info(hwdev->dev_hdl, "Unknown fec type: %u\n",
			 info->fec);

	if (type == HILINK_EVENT_LINK_UP || !info->an_state) {
		sdk_info(hwdev->dev_hdl, "Link information: speed %dGbps, %s, autoneg %s\n",
			 info->speed, fec, info->an_state ? "on" : "off");
	} else {
		sdk_info(hwdev->dev_hdl, "Link information: antoneg: %s\n",
			 info->an_state ? "on" : "off");
	}
}

static char *hilink_info_report_type[HILINK_EVENT_MAX_TYPE] = {
	"", "link up", "link down", "cable plugged"
};

void print_hilink_info(struct hinic_hwdev *hwdev,
		       enum hilink_info_print_event type,
		       struct hinic_link_info *info)
{
	__print_cable_info(hwdev, info);

	__print_link_info(hwdev, info, type);

	__print_hi30_status(hwdev, info);

	if (type == HILINK_EVENT_LINK_UP)
		return;

	if (type == HILINK_EVENT_CABLE_PLUGGED) {
		sdk_info(hwdev->dev_hdl, "alos: %u, rx_los: %u\n",
			 info->alos, info->rx_los);
		return;
	}

	sdk_info(hwdev->dev_hdl, "PMA ctrl: %s, MAC tx %s, MAC rx %s, PMA debug info reg: 0x%x, PMA signal ok reg: 0x%x, RF/LF status reg: 0x%x\n",
		 info->pma_status == 1 ? "off" : "on",
		 info->mac_tx_en ? "enable" : "disable",
		 info->mac_rx_en ? "enable" : "disable", info->pma_dbg_info_reg,
		 info->pma_signal_ok_reg, info->rf_lf_status_reg);
	sdk_info(hwdev->dev_hdl, "alos: %u, rx_los: %u, PCS block counter reg: 0x%x, PCS link: 0x%x, MAC link: 0x%x PCS_err_cnt: 0x%x\n",
		 info->alos, info->rx_los, info->pcs_err_blk_cnt_reg,
		 info->pcs_link_reg, info->mac_link_reg, info->pcs_err_cnt);
}

static int hinic_print_hilink_info(struct hinic_hwdev *hwdev, void *buf_in,
				   u16 in_size, void *buf_out, u16 *out_size)
{
	struct hinic_hilink_link_info *hilink_info = buf_in;
	struct hinic_link_info *info;
	enum hilink_info_print_event type;

	if (in_size != sizeof(*hilink_info)) {
		sdk_err(hwdev->dev_hdl, "Invalid hilink info message size %d, should be %ld\n",
			in_size, sizeof(*hilink_info));
		return -EINVAL;
	}

	((struct hinic_hilink_link_info *)buf_out)->status = 0;
	*out_size = sizeof(*hilink_info);

	info = &hilink_info->info;
	type = hilink_info->info_type;

	if (type < HILINK_EVENT_LINK_UP || type >= HILINK_EVENT_MAX_TYPE) {
		sdk_info(hwdev->dev_hdl, "Invalid hilink info report, type: %d\n",
			 type);
		return -EINVAL;
	}

	sdk_info(hwdev->dev_hdl, "Hilink info report after %s\n",
		 hilink_info_report_type[type]);

	print_hilink_info(hwdev, type, info);

	return 0;
}

int hinic_hilink_info_show(struct hinic_hwdev *hwdev)
{
	struct hinic_link_info hilink_info = { {0} };
	int err;

	err = hinic_get_hilink_link_info(hwdev, &hilink_info);
	if (err) {
		if (err == HINIC_MGMT_CMD_UNSUPPORTED)
			sdk_info(hwdev->dev_hdl, "Unsupport to get hilink info\n");
		return err;
	}

	if (hilink_info.cable_absent) {
		sdk_info(hwdev->dev_hdl, "Cable unpresent\n");
		return 0;
	}

	sdk_info(hwdev->dev_hdl, "Current state of hilink info:\n");
	print_hilink_info(hwdev, HILINK_EVENT_MAX_TYPE, &hilink_info);

	return 0;
}

static void mgmt_heartbeat_enhanced_event(struct hinic_hwdev *hwdev,
					  void *buf_in, u16 in_size,
					  void *buf_out, u16 *out_size)
{
	struct hinic_heartbeat_event *hb_event = buf_in;
	struct hinic_heartbeat_event *hb_event_out = buf_out;
	struct hinic_hwdev *dev = hwdev;

	if (in_size != sizeof(*hb_event)) {
		sdk_err(dev->dev_hdl, "Invalid data size from mgmt for heartbeat event: %d\n",
			in_size);
		return;
	}

	if (dev->heartbeat_ehd.last_heartbeat != hb_event->heart) {
		dev->heartbeat_ehd.last_update_jiffies = jiffies;
		dev->heartbeat_ehd.last_heartbeat = hb_event->heart;
	}

	hb_event_out->drv_heart = HEARTBEAT_DRV_MAGIC_ACK;

	hb_event_out->status = 0;
	*out_size = sizeof(*hb_event_out);
}

/* public process for this event:
 * pf link change event
 * pf heart lost event ,TBD
 * pf fault report event
 * vf link change event
 * vf heart lost event, TBD
 * vf fault report event, TBD
 */
static void _event_handler(struct hinic_hwdev *hwdev, enum hinic_event_cmd cmd,
			   void *buf_in, u16 in_size,
			   void *buf_out, u16 *out_size)
{
	struct hinic_vf_dcb_state *vf_dcb;

	if (!hwdev)
		return;

	*out_size = 0;

	switch (cmd) {
	case HINIC_EVENT_LINK_STATUS_CHANGE:
		link_status_event_handler(hwdev, buf_in, in_size, buf_out,
					  out_size);
		break;

	case HINIC_EVENT_CABLE_PLUG:
	case HINIC_EVENT_LINK_ERR:
		module_status_event(hwdev, cmd, buf_in, in_size, buf_out,
				    out_size);
		break;

	case HINIC_EVENT_HILINK_INFO:
		hinic_print_hilink_info(hwdev, buf_in, in_size, buf_out,
					out_size);
		break;

	case HINIC_EVENT_MGMT_FAULT:
		fault_event_handler(hwdev, buf_in, in_size, buf_out, out_size);
		break;

	case HINIC_EVENT_HEARTBEAT_LOST:
		heartbeat_lost_event_handler(hwdev);
		break;

	case HINIC_EVENT_SET_VF_COS:
		vf_dcb = buf_in;
		if (!vf_dcb)
			break;

		hinic_notify_dcb_state_event(hwdev, &vf_dcb->state);

		break;

	case HINIC_EVENT_MGMT_WATCHDOG:
		mgmt_watchdog_timeout_event_handler(hwdev, buf_in, in_size,
						    buf_out, out_size);
		break;

	case HINIC_EVENT_MGMT_RESET:
		mgmt_reset_event_handler(hwdev);
		break;

	case HINIC_EVENT_MGMT_FMW_ACT_NTC:
		hinic_fmw_act_ntc_handler(hwdev, buf_in, in_size, buf_out,
					  out_size);

		break;

	case HINIC_EVENT_MGMT_PCIE_DFX:
		hinic_pcie_dfx_event_handler(hwdev, buf_in, in_size, buf_out,
					     out_size);
		break;

	case HINIC_EVENT_MCTP_HOST_INFO:
		hinic_mctp_get_host_info_event_handler(hwdev, buf_in, in_size,
						       buf_out, out_size);
		break;

	case HINIC_EVENT_MGMT_HEARTBEAT_EHD:
		mgmt_heartbeat_enhanced_event(hwdev, buf_in, in_size,
					      buf_out, out_size);
		break;

	case HINIC_EVENT_SFP_INFO_REPORT:
		port_sfp_info_event(hwdev, buf_in, in_size, buf_out, out_size);
		break;

	case HINIC_EVENT_SFP_ABS_REPORT:
		port_sfp_abs_event(hwdev, buf_in, in_size, buf_out, out_size);
		break;

	default:
		sdk_warn(hwdev->dev_hdl, "Unsupported event %d to process\n",
			 cmd);
		break;
	}
}

/* vf link change event
 * vf fault report event, TBD
 */
static int vf_nic_event_handler(void *hwdev, u8 cmd, void *buf_in,
				u16 in_size, void *buf_out, u16 *out_size)

{
	enum hinic_event_cmd type = __get_event_type(HINIC_MOD_L2NIC, cmd);

	if (type == HINIC_EVENT_MAX_TYPE) {
		sdk_warn(((struct hinic_hwdev *)hwdev)->dev_hdl,
			 "Unsupport L2NIC event: cmd %d\n", cmd);
		*out_size = 0;
		return -EINVAL;
	}

	_event_handler(hwdev, type, buf_in, in_size, buf_out, out_size);

	return 0;
}

static int vf_comm_event_handler(void *hwdev, u8 cmd, void *buf_in,
				 u16 in_size, void *buf_out, u16 *out_size)

{
	enum hinic_event_cmd type = __get_event_type(HINIC_MOD_COMM, cmd);

	if (type == HINIC_EVENT_MAX_TYPE) {
		sdk_warn(((struct hinic_hwdev *)hwdev)->dev_hdl,
			 "Unsupport COMM event: cmd %d\n", cmd);
		*out_size = 0;
		return -EFAULT;
	}

	_event_handler(hwdev, type, buf_in, in_size, buf_out, out_size);

	return 0;
}

/* pf link change event */
static void pf_nic_event_handler(void *hwdev, void *pri_handle, u8 cmd,
				 void *buf_in, u16 in_size,
				 void *buf_out, u16 *out_size)
{
	enum hinic_event_cmd type = __get_event_type(HINIC_MOD_L2NIC, cmd);

	if (type == HINIC_EVENT_MAX_TYPE) {
		sdk_warn(((struct hinic_hwdev *)hwdev)->dev_hdl,
			 "Unsupport L2NIC event: cmd %d\n", cmd);
		*out_size = 0;
		return;
	}

	_event_handler(hwdev, type, buf_in, in_size, buf_out, out_size);
}

static void pf_hilink_event_handler(void *hwdev, void *pri_handle, u8 cmd,
				    void *buf_in, u16 in_size,
				    void *buf_out, u16 *out_size)
{
	enum hinic_event_cmd type = __get_event_type(HINIC_MOD_HILINK, cmd);

	if (type == HINIC_EVENT_MAX_TYPE) {
		sdk_warn(((struct hinic_hwdev *)hwdev)->dev_hdl,
			 "Unsupport HILINK event: cmd %d\n", cmd);
		*out_size = 0;
		return;
	}

	_event_handler(hwdev, type, buf_in, in_size, buf_out, out_size);
}

/* pf fault report event */
void pf_fault_event_handler(void *hwdev,
			    void *buf_in, u16 in_size,
		   void *buf_out, u16 *out_size)
{
	_event_handler(hwdev, HINIC_EVENT_MGMT_FAULT, buf_in,
		       in_size, buf_out, out_size);
}

void mgmt_watchdog_event_handler(void *hwdev, void *buf_in, u16 in_size,
				 void *buf_out, u16 *out_size)
{
	_event_handler(hwdev, HINIC_EVENT_MGMT_WATCHDOG, buf_in,
		       in_size, buf_out, out_size);
}

void mgmt_fmw_act_event_handler(void *hwdev, void *buf_in, u16 in_size,
				void *buf_out, u16 *out_size)
{
	_event_handler(hwdev, HINIC_EVENT_MGMT_FMW_ACT_NTC, buf_in,
		       in_size, buf_out, out_size);
}

void mgmt_pcie_dfx_event_handler(void *hwdev, void *buf_in, u16 in_size,
				 void *buf_out, u16 *out_size)
{
	_event_handler(hwdev, HINIC_EVENT_MGMT_PCIE_DFX, buf_in,
		       in_size, buf_out, out_size);
}

void mgmt_get_mctp_event_handler(void *hwdev, void *buf_in, u16 in_size,
				 void *buf_out, u16 *out_size)
{
	_event_handler(hwdev, HINIC_EVENT_MCTP_HOST_INFO, buf_in,
		       in_size, buf_out, out_size);
}

void mgmt_heartbeat_event_handler(void *hwdev, void *buf_in, u16 in_size,
				  void *buf_out, u16 *out_size)
{
	_event_handler(hwdev, HINIC_EVENT_MGMT_HEARTBEAT_EHD, buf_in,
		       in_size, buf_out, out_size);
}

static void pf_event_register(struct hinic_hwdev *hwdev)
{
	if (hinic_is_hwdev_mod_inited(hwdev, HINIC_HWDEV_MGMT_INITED)) {
		hinic_register_mgmt_msg_cb(hwdev, HINIC_MOD_L2NIC,
					   hwdev, pf_nic_event_handler);
		hinic_register_mgmt_msg_cb(hwdev, HINIC_MOD_HILINK,
					   hwdev,
					   pf_hilink_event_handler);
		hinic_comm_recv_mgmt_self_cmd_reg(hwdev,
						  HINIC_MGMT_CMD_FAULT_REPORT,
						  pf_fault_event_handler);

		hinic_comm_recv_mgmt_self_cmd_reg(hwdev,
						  HINIC_MGMT_CMD_WATCHDOG_INFO,
						  mgmt_watchdog_event_handler);

		hinic_comm_recv_mgmt_self_cmd_reg(hwdev,
						  HINIC_MGMT_CMD_FMW_ACT_NTC,
						  mgmt_fmw_act_event_handler);
		hinic_comm_recv_mgmt_self_cmd_reg(hwdev,
						  HINIC_MGMT_CMD_PCIE_DFX_NTC,
						  mgmt_pcie_dfx_event_handler);
		hinic_comm_recv_mgmt_self_cmd_reg(hwdev,
						  HINIC_MGMT_CMD_GET_HOST_INFO,
						  mgmt_get_mctp_event_handler);
	}
}

void hinic_event_register(void *dev, void *pri_handle,
			  hinic_event_handler callback)
{
	struct hinic_hwdev *hwdev = dev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for register event\n");
		return;
	}

	hwdev->event_callback = callback;
	hwdev->event_pri_handle = pri_handle;

	if (hinic_func_type(hwdev) != TYPE_VF) {
		pf_event_register(hwdev);
	} else {
		hinic_register_vf_mbox_cb(hwdev, HINIC_MOD_L2NIC,
					  vf_nic_event_handler);
		hinic_register_vf_mbox_cb(hwdev, HINIC_MOD_COMM,
					  vf_comm_event_handler);
	}
}

void hinic_event_unregister(void *dev)
{
	struct hinic_hwdev *hwdev = dev;

	hwdev->event_callback = NULL;
	hwdev->event_pri_handle = NULL;

	if (hinic_func_type(hwdev) != TYPE_VF) {
		hinic_unregister_mgmt_msg_cb(hwdev, HINIC_MOD_L2NIC);
		hinic_unregister_mgmt_msg_cb(hwdev, HINIC_MOD_HILINK);
		hinic_comm_recv_up_self_cmd_unreg(hwdev,
						  HINIC_MGMT_CMD_FAULT_REPORT);
		hinic_comm_recv_up_self_cmd_unreg(hwdev,
						  HINIC_MGMT_CMD_WATCHDOG_INFO);
		hinic_comm_recv_up_self_cmd_unreg(hwdev,
						  HINIC_MGMT_CMD_FMW_ACT_NTC);
		hinic_comm_recv_up_self_cmd_unreg(hwdev,
						  HINIC_MGMT_CMD_PCIE_DFX_NTC);
		hinic_comm_recv_up_self_cmd_unreg(hwdev,
						  HINIC_MGMT_CMD_GET_HOST_INFO);
	} else {
		hinic_unregister_vf_mbox_cb(hwdev, HINIC_MOD_L2NIC);
		hinic_unregister_vf_mbox_cb(hwdev, HINIC_MOD_COMM);
	}
}

/* 0 - heartbeat lost, 1 - normal */
static u8 hinic_get_heartbeat_status(struct hinic_hwdev *hwdev)
{
	struct hinic_hwif *hwif = hwdev->hwif;
	u32 attr1;

	/* suprise remove should be set 1 */
	if (!hinic_get_chip_present_flag(hwdev))
		return 1;

	attr1 = hinic_hwif_read_reg(hwif, HINIC_CSR_FUNC_ATTR1_ADDR);
	if (attr1 == HINIC_PCIE_LINK_DOWN) {
		sdk_err(hwdev->dev_hdl, "Detect pcie is link down\n");
		hinic_set_chip_absent(hwdev);
		hinic_force_complete_all(hwdev);
	/* should notify chiperr to pangea when detecting pcie link down */
		return 1;
	}

	return HINIC_AF1_GET(attr1, MGMT_INIT_STATUS);
}

static void hinic_heartbeat_event_handler(struct work_struct *work)
{
	struct hinic_hwdev *hwdev =
			container_of(work, struct hinic_hwdev, timer_work);
	u16 out = 0;

	_event_handler(hwdev, HINIC_EVENT_HEARTBEAT_LOST,
		       NULL, 0, &out, &out);
}

static bool __detect_heartbeat_ehd_lost(struct hinic_hwdev *hwdev)
{
	struct hinic_heartbeat_enhanced *hb_ehd = &hwdev->heartbeat_ehd;
	u64 update_time;
	bool hb_ehd_lost = false;

	if (!hb_ehd->en)
		return false;

	if (time_after(jiffies, hb_ehd->start_detect_jiffies)) {
		update_time = jiffies_to_msecs(jiffies -
					       hb_ehd->last_update_jiffies);
		if (update_time > HINIC_HEARBEAT_ENHANCED_LOST) {
			sdk_warn(hwdev->dev_hdl, "Heartbeat enhanced lost for %d millisecond\n",
				 (u32)update_time);
			hb_ehd_lost = true;
		}
	} else {
		/* mgmt may not report heartbeart enhanced event and won't
		 * update last_update_jiffies
		 */
		hb_ehd->last_update_jiffies = jiffies;
	}

	return hb_ehd_lost;
}

#ifdef HAVE_TIMER_SETUP
static void hinic_heartbeat_timer_handler(struct timer_list *t)
#else
static void hinic_heartbeat_timer_handler(unsigned long data)
#endif
{
#ifdef HAVE_TIMER_SETUP
	struct hinic_hwdev *hwdev = from_timer(hwdev, t, heartbeat_timer);
#else
	struct hinic_hwdev *hwdev = (struct hinic_hwdev *)data;
#endif

	if (__detect_heartbeat_ehd_lost(hwdev) ||
	    !hinic_get_heartbeat_status(hwdev)) {
		hwdev->heartbeat_lost = 1;
		stop_timer(&hwdev->heartbeat_timer);
		queue_work(hwdev->workq, &hwdev->timer_work);
	} else {
		mod_timer(&hwdev->heartbeat_timer,
			  jiffies + msecs_to_jiffies(HINIC_HEARTBEAT_PERIOD));
	}
}

void hinic_init_heartbeat(struct hinic_hwdev *hwdev)
{
#ifdef HAVE_TIMER_SETUP
	timer_setup(&hwdev->heartbeat_timer, hinic_heartbeat_timer_handler, 0);
#else
	initialize_timer(hwdev->adapter_hdl, &hwdev->heartbeat_timer);
	hwdev->heartbeat_timer.data = (unsigned long)hwdev;
	hwdev->heartbeat_timer.function = hinic_heartbeat_timer_handler;
#endif
	hwdev->heartbeat_timer.expires =
		jiffies + msecs_to_jiffies(HINIC_HEARTBEAT_START_EXPIRE);

	add_to_timer(&hwdev->heartbeat_timer, HINIC_HEARTBEAT_PERIOD);

	INIT_WORK(&hwdev->timer_work, hinic_heartbeat_event_handler);
}

void hinic_destroy_heartbeat(struct hinic_hwdev *hwdev)
{
	destroy_work(&hwdev->timer_work);
	stop_timer(&hwdev->heartbeat_timer);
	delete_timer(&hwdev->heartbeat_timer);
}

u8 hinic_nic_sw_aeqe_handler(void *handle, u8 event, u64 data)
{
	struct hinic_hwdev *hwdev =  (struct hinic_hwdev *)handle;
	u8 event_level = FAULT_LEVEL_MAX;

	switch (event) {
	case HINIC_INTERNAL_TSO_FATAL_ERROR:
	case HINIC_INTERNAL_LRO_FATAL_ERROR:
	case HINIC_INTERNAL_TX_FATAL_ERROR:
	case HINIC_INTERNAL_RX_FATAL_ERROR:
	case HINIC_INTERNAL_OTHER_FATAL_ERROR:
		atomic_inc(&hwdev->hw_stats.nic_ucode_event_stats[event]);
		sdk_err(hwdev->dev_hdl, "SW aeqe event type: 0x%x, data: 0x%llx\n",
			event, data);
		event_level = FAULT_LEVEL_FATAL;
		break;
	default:
		sdk_err(hwdev->dev_hdl, "Unsupported sw event %d to process.\n",
			event);
	}

	return event_level;
}

struct hinic_fast_recycled_mode {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	fast_recycled_mode;	/* 1: enable fast recycle, available
					 *    in dpdk mode,
					 * 0: normal mode, available in kernel
					 *    nic mode
					 */
	u8	rsvd1;
};

int hinic_enable_fast_recycle(void *hwdev, bool enable)
{
	struct hinic_fast_recycled_mode fast_recycled_mode = {0};
	u16 out_size = sizeof(fast_recycled_mode);
	int err;

	if (!hwdev)
		return -EINVAL;

	err = hinic_global_func_id_get(hwdev, &fast_recycled_mode.func_id);
	if (err)
		return err;

	fast_recycled_mode.fast_recycled_mode = enable ? 1 : 0;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_FAST_RECYCLE_MODE_SET,
				     &fast_recycled_mode,
				     sizeof(fast_recycled_mode),
				     &fast_recycled_mode, &out_size, 0);
	if (err || fast_recycled_mode.status || !out_size) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to set recycle mode, err: %d, status: 0x%x, out size: 0x%x\n",
			err, fast_recycled_mode.status, out_size);
		return -EFAULT;
	}

	return 0;
}

void hinic_set_pcie_order_cfg(void *handle)
{
	struct hinic_hwdev *hwdev = handle;
	u32 val;

	if (!hwdev)
		return;

	val = hinic_hwif_read_reg(hwdev->hwif,
				  HINIC_GLB_DMA_SO_RO_REPLACE_ADDR);

	if (HINIC_GLB_DMA_SO_RO_GET(val, SO_RO_CFG)) {
		val = HINIC_GLB_DMA_SO_R0_CLEAR(val, SO_RO_CFG);
		val |= HINIC_GLB_DMA_SO_R0_SET(HINIC_DISABLE_ORDER, SO_RO_CFG);
		hinic_hwif_write_reg(hwdev->hwif,
				     HINIC_GLB_DMA_SO_RO_REPLACE_ADDR, val);
	}
}

int _set_led_status(struct hinic_hwdev *hwdev, u8 port,
		    enum hinic_led_type type,
		    enum hinic_led_mode mode, u8 reset)
{
	struct hinic_led_info led_info = {0};
	u16 out_size = sizeof(led_info);
	int err;

	led_info.port = port;
	led_info.reset = reset;

	led_info.type = type;
	led_info.mode = mode;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_SET_LED_STATUS,
				     &led_info, sizeof(led_info),
				     &led_info, &out_size, 0);
	if (err || led_info.status || !out_size) {
		sdk_err(hwdev->dev_hdl, "Failed to set led status, err: %d, status: 0x%x, out size: 0x%x\n",
			err, led_info.status, out_size);
		return -EFAULT;
	}

	return 0;
}

int hinic_set_led_status(void *hwdev, u8 port, enum hinic_led_type type,
			 enum hinic_led_mode mode)
{
	int err;

	if (!hwdev)
		return -EFAULT;

	err = _set_led_status(hwdev, port, type, mode, 0);
	if (err)
		return err;

	return 0;
}

int hinic_reset_led_status(void *hwdev, u8 port)
{
	int err;

	if (!hwdev)
		return -EFAULT;

	err = _set_led_status(hwdev, port, HINIC_LED_TYPE_INVALID,
			      HINIC_LED_MODE_INVALID, 1);
	if (err) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to reset led status\n");
		return err;
	}

	return 0;
}

int hinic_get_board_info(void *hwdev, struct hinic_board_info *info)
{
	struct hinic_comm_board_info board_info = {0};
	u16 out_size = sizeof(board_info);
	int err;

	if (!hwdev || !info)
		return -EINVAL;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_GET_BOARD_INFO,
				     &board_info, sizeof(board_info),
				     &board_info, &out_size, 0);
	if (err || board_info.status || !out_size) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to get board info, err: %d, status: 0x%x, out size: 0x%x\n",
			err, board_info.status, out_size);
		return -EFAULT;
	}

	memcpy(info, &board_info.info, sizeof(*info));

	return 0;
}
EXPORT_SYMBOL(hinic_get_board_info);

int hinic_get_phy_init_status(void *hwdev,
			      enum phy_init_status_type *init_status)
{
	struct hinic_phy_init_status phy_info = {0};
	u16 out_size = sizeof(phy_info);
	int err;

	if (!hwdev || !init_status)
		return -EINVAL;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_GET_PHY_INIT_STATUS,
				     &phy_info, sizeof(phy_info),
				     &phy_info, &out_size, 0);
	if ((phy_info.status != HINIC_MGMT_CMD_UNSUPPORTED &&
	     phy_info.status) || err || !out_size) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to get phy info, err: %d, status: 0x%x, out size: 0x%x\n",
			err, phy_info.status, out_size);
		return -EFAULT;
	}

	*init_status = phy_info.init_status;

	return phy_info.status;
}

int hinic_phy_init_status_judge(void *hwdev)
{
	enum phy_init_status_type init_status;
	int ret;
	unsigned long end;

	/* It's not a phy, so don't judge phy status */
	if (!HINIC_BOARD_IS_PHY((struct hinic_hwdev *)hwdev))
		return 0;

	end = jiffies + msecs_to_jiffies(PHY_DOING_INIT_TIMEOUT);
	do {
		ret = hinic_get_phy_init_status(hwdev, &init_status);
		if (ret == HINIC_MGMT_CMD_UNSUPPORTED)
			return 0;
		else if (ret)
			return -EFAULT;

		switch (init_status) {
		case PHY_INIT_SUCCESS:
			sdk_info(((struct hinic_hwdev *)hwdev)->dev_hdl,
				 "Phy init is success\n");
			return 0;
		case PHY_NONSUPPORT:
			sdk_info(((struct hinic_hwdev *)hwdev)->dev_hdl,
				 "Phy init is nonsupport\n");
			return 0;
		case PHY_INIT_FAIL:
			sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
				"Phy init is failed\n");
			return -EIO;
		case PHY_INIT_DOING:
			msleep(250);
			break;
		default:
			sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
				"Phy init is invalid, init_status: %d\n",
				init_status);
			return -EINVAL;
		}
	} while (time_before(jiffies, end));

	sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
		"Phy init is timeout\n");

	return -ETIMEDOUT;
}

static void hinic_set_mgmt_channel_status(void *handle, bool state)
{
	struct hinic_hwdev *hwdev = handle;
	u32 val;

	if (!hwdev || hinic_func_type(hwdev) == TYPE_VF ||
	    !(hwdev->feature_cap & HINIC_FUNC_SUPP_DFX_REG))
		return;

	val = hinic_hwif_read_reg(hwdev->hwif, HINIC_ICPL_RESERVD_ADDR);
	val = HINIC_CLEAR_MGMT_CHANNEL_STATUS(val, MGMT_CHANNEL_STATUS);
	val |= HINIC_SET_MGMT_CHANNEL_STATUS((u32)state, MGMT_CHANNEL_STATUS);

	hinic_hwif_write_reg(hwdev->hwif, HINIC_ICPL_RESERVD_ADDR, val);
}

int hinic_get_mgmt_channel_status(void *handle)
{
	struct hinic_hwdev *hwdev = handle;
	u32 val;

	if (!hwdev)
		return true;

	if (hinic_func_type(hwdev) == TYPE_VF ||
	    !(hwdev->feature_cap & HINIC_FUNC_SUPP_DFX_REG))
		return false;

	val = hinic_hwif_read_reg(hwdev->hwif, HINIC_ICPL_RESERVD_ADDR);

	return HINIC_GET_MGMT_CHANNEL_STATUS(val, MGMT_CHANNEL_STATUS);
}

static void hinic_enable_mgmt_channel(void *hwdev, void *buf_out)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_update_active *active_info = buf_out;

	if (!active_info || hinic_func_type(hwdev) == TYPE_VF ||
	    !(dev->feature_cap & HINIC_FUNC_SUPP_DFX_REG))
		return;

	if ((!active_info->status) &&
	    (active_info->update_status & HINIC_ACTIVE_STATUS_MASK)) {
		active_info->update_status &= HINIC_ACTIVE_STATUS_CLEAR;
		return;
	}

	hinic_set_mgmt_channel_status(hwdev, false);
}

int hinic_get_bios_pf_bw_limit(void *hwdev, u32 *pf_bw_limit)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_bios_cfg_cmd cfg = {0};
	u16 out_size = sizeof(cfg);
	u16 func_id;
	int err;

	if (!hwdev || !pf_bw_limit)
		return -EINVAL;

	if (HINIC_FUNC_TYPE(dev) == TYPE_VF ||
	    !FUNC_SUPPORT_RATE_LIMIT(hwdev))
		return 0;

	err = hinic_global_func_id_get(hwdev, &func_id);
	if (err)
		return err;

	cfg.func_valid = 1;
	cfg.func_idx = (u8)func_id;

	cfg.op_code = HINIC_BIOS_CFG_GET | HINIC_BIOS_CFG_PF_BW_LIMIT;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_BIOS_NV_DATA_MGMT,
				     &cfg, sizeof(cfg),
				     &cfg, &out_size, 0);
	if (err || cfg.status || !out_size) {
		sdk_err(dev->dev_hdl, "Failed to get bios pf bandwidth limit, err: %d, status: 0x%x, out size: 0x%x\n",
			err, cfg.status, out_size);
		return -EIO;
	}

	/* Check data is valid or not */
	if (cfg.signature != 0x19e51822) {
		sdk_err(dev->dev_hdl, "Invalid bios configureration data, signature: 0x%x\n",
			cfg.signature);
		return -EINVAL;
	}

	if (cfg.pf_bw_limit > 100) {
		sdk_err(dev->dev_hdl, "Invalid bios cfg pf bandwidth limit: %d\n",
			cfg.pf_bw_limit);
		return -EINVAL;
	}

	*pf_bw_limit = cfg.pf_bw_limit;

	return 0;
}

bool hinic_get_ppf_status(void *hwdev)
{
	struct hinic_ppf_state ppf_state = {0};
	struct hinic_hwdev *dev = hwdev;
	struct card_node *chip_node;
	u16 out_size = sizeof(ppf_state);
	int err;

	if (!hwdev)
		return false;

	chip_node = (struct card_node *)dev->chip_node;

	if (!HINIC_IS_VF(dev))
		return chip_node->ppf_state;

	err = hinic_mbox_to_pf(hwdev, HINIC_MOD_COMM,
			       HINIC_MGMT_CMD_GET_PPF_STATE,
			       &ppf_state, sizeof(ppf_state),
			       &ppf_state, &out_size, 0);
	if (err || ppf_state.status || !out_size) {
		sdk_err(dev->dev_hdl, "Failed to get ppf state, err: %d, status: 0x%x, out size: 0x%x\n",
			err, ppf_state.status, out_size);
		return false;
	}

	return (bool)ppf_state.ppf_state;
}

#define HINIC_RED_REG_TIME_OUT	3000

int hinic_read_reg(void *hwdev, u32 reg_addr, u32 *val)
{
	struct hinic_reg_info reg_info = {0};
	u16 out_size = sizeof(reg_info);
	int err;

	if (!hwdev || !val)
		return -EINVAL;

	reg_info.reg_addr = reg_addr;
	reg_info.val_length = sizeof(u32);

	err = hinic_pf_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
					HINIC_MGMT_CMD_REG_READ,
					&reg_info, sizeof(reg_info),
					&reg_info, &out_size,
					HINIC_RED_REG_TIME_OUT);
	if (reg_info.status || err || !out_size) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to read reg, err: %d, status: 0x%x, out size: 0x%x\n",
			err, reg_info.status, out_size);
		return -EFAULT;
	}

	*val = reg_info.data[0];

	return 0;
}



static void hinic_exec_recover_cb(struct hinic_hwdev *hwdev,
				  struct hinic_fault_recover_info *info)
{

	sdk_info(hwdev->dev_hdl, "Enter %s\n", __func__);

	if (!hinic_get_chip_present_flag(hwdev)) {
		sdk_err(hwdev->dev_hdl, "Device surprised removed, abort recover\n");
		return;
	}

	if (info->fault_lev >= FAULT_LEVEL_MAX) {
		sdk_err(hwdev->dev_hdl, "Invalid fault level\n");
		return;
	}


	down(&hwdev->recover_sem);
	if (hwdev->recover_cb) {
		if (info->fault_lev <= FAULT_LEVEL_SERIOUS_FLR)
			hinic_set_fast_recycle_status(hwdev);

		hwdev->recover_cb(hwdev->recover_pri_hd, *info);
	}
	up(&hwdev->recover_sem);
}

void hinic_fault_work_handler(struct work_struct *work)
{
	struct hinic_hwdev *hwdev =
			container_of(work, struct hinic_hwdev, fault_work);

	down(&hwdev->fault_list_sem);
	up(&hwdev->fault_list_sem);
}

void hinic_swe_fault_handler(struct hinic_hwdev *hwdev, u8 level,
			     u8 event, u64 val)
{
	struct hinic_fault_info_node *fault_node;

	if (level < FAULT_LEVEL_MAX) {
		fault_node = kzalloc(sizeof(*fault_node), GFP_KERNEL);
		if (!fault_node) {
			sdk_err(hwdev->dev_hdl, "Malloc fault node memory failed\n");
			return;
		}

		fault_node->info.fault_src = HINIC_FAULT_SRC_SW_MGMT_UCODE;
		fault_node->info.fault_lev = level;
		fault_node->info.fault_data.sw_mgmt.event_id = event;
		fault_node->info.fault_data.sw_mgmt.event_data = val;
		hinic_refresh_history_fault(hwdev, &fault_node->info);

		down(&hwdev->fault_list_sem);
		kfree(fault_node);
		up(&hwdev->fault_list_sem);

		queue_work(hwdev->workq, &hwdev->fault_work);
	}
}

int hinic_register_fault_recover(void *hwdev, void *pri_handle,
				 hinic_fault_recover_handler cb)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev || !pri_handle || !cb) {
		pr_err("Invalid input parameters when register fault recover handler\n");
		return -EINVAL;
	}

	down(&dev->recover_sem);
	dev->recover_pri_hd = pri_handle;
	dev->recover_cb = cb;
	up(&dev->recover_sem);

	if (dev->history_fault_flag)
		hinic_exec_recover_cb(dev, &dev->history_fault);

	return 0;
}
EXPORT_SYMBOL(hinic_register_fault_recover);

int hinic_unregister_fault_recover(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev) {
		pr_err("Invalid input parameters when unregister fault recover handler\n");
		return -EINVAL;
	}

	down(&dev->recover_sem);
	dev->recover_pri_hd = NULL;
	dev->recover_cb = NULL;
	up(&dev->recover_sem);

	return 0;
}
EXPORT_SYMBOL(hinic_unregister_fault_recover);

void hinic_set_func_deinit_flag(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	set_bit(HINIC_HWDEV_FUNC_DEINIT, &dev->func_state);
}

int hinic_get_hw_pf_infos(void *hwdev, struct hinic_hw_pf_infos *infos)
{
	struct hinic_hw_pf_infos_cmd pf_infos = {0};
	u16 out_size = sizeof(pf_infos);
	int err;

	if (!hwdev || !infos)
		return -EINVAL;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_GET_HW_PF_INFOS,
				     &pf_infos, sizeof(pf_infos),
				     &pf_infos, &out_size, 0);
	if ((pf_infos.status != HINIC_MGMT_CMD_UNSUPPORTED &&
	     pf_infos.status) || err || !out_size) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to get hw pf informations, err: %d, status: 0x%x, out size: 0x%x\n",
			err, pf_infos.status, out_size);
		return -EFAULT;
	}

	if (!pf_infos.status)
		memcpy(infos, &pf_infos.infos, sizeof(*infos));

	return pf_infos.status;
}
EXPORT_SYMBOL(hinic_get_hw_pf_infos);

int hinic_set_ip_check(void *hwdev, bool ip_check_ctl)
{
	u32 val = 0;
	int ret;
	int i;

	if (!hwdev)
		return -EINVAL;

	if (hinic_func_type(hwdev) == TYPE_VF)
		return 0;

	for (i = 0; i <= HINIC_IPSU_CHANNEL_NUM; i++) {
		ret = hinic_api_csr_rd32(hwdev, HINIC_NODE_ID_IPSU,
					 (HINIC_IPSU_CHANNEL0_ADDR +
					 i * HINIC_IPSU_CHANNEL_OFFSET), &val);
		if (ret)
			return ret;

		val = be32_to_cpu(val);
		if (ip_check_ctl)
			val |= HINIC_IPSU_DIP_SIP_MASK;
		else
			val &= (~HINIC_IPSU_DIP_SIP_MASK);

		val = cpu_to_be32(val);
		ret = hinic_api_csr_wr32(hwdev, HINIC_NODE_ID_IPSU,
					 (HINIC_IPSU_CHANNEL0_ADDR +
					 i * HINIC_IPSU_CHANNEL_OFFSET), val);
		if (ret)
			return ret;
	}
	return 0;
}

int hinic_get_card_present_state(void *hwdev, bool *card_present_state)
{
	u32 addr, attr1;

	if (!hwdev || !card_present_state)
		return -EINVAL;

	addr = HINIC_CSR_FUNC_ATTR1_ADDR;
	attr1 = hinic_hwif_read_reg(((struct hinic_hwdev *)hwdev)->hwif, addr);
	if (attr1 == HINIC_PCIE_LINK_DOWN) {
		sdk_warn(((struct hinic_hwdev *)hwdev)->dev_hdl, "Card is not present\n");
		*card_present_state = (bool)0;
	} else {
		*card_present_state = (bool)1;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_get_card_present_state);

void hinic_disable_mgmt_msg_report(void *hwdev)
{
	struct hinic_hwdev *hw_dev = (struct hinic_hwdev *)hwdev;

	hinic_set_pf_status(hw_dev->hwif, HINIC_PF_STATUS_INIT);
}

int hinic_set_vxlan_udp_dport(void *hwdev, u32 udp_port)
{
	u32 val = 0;
	int ret;

	if (!hwdev)
		return -EINVAL;

	if (hinic_func_type(hwdev) == TYPE_VF)
		return 0;

	ret = hinic_api_csr_rd32(hwdev, HINIC_NODE_ID_IPSU,
				 HINIC_IPSURX_VXLAN_DPORT_ADDR, &val);
	if (ret)
		return ret;

	nic_info(((struct hinic_hwdev *)hwdev)->dev_hdl,
		 "Update VxLAN UDP dest port: cur port:%u, new port:%u",
		 be32_to_cpu(val), udp_port);

	if (be32_to_cpu(val) == udp_port)
		return 0;

	udp_port = cpu_to_be32(udp_port);
	ret = hinic_api_csr_wr32(hwdev, HINIC_NODE_ID_IPSU,
				 HINIC_IPSURX_VXLAN_DPORT_ADDR, udp_port);
	if (ret)
		return ret;

	return 0;
}
