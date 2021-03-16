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

#ifndef __HINIC_DFX_DEF_H__
#define __HINIC_DFX_DEF_H__

#ifdef __cplusplus
    #if __cplusplus
extern "C"{
    #endif
#endif /* __cplusplus */

enum module_name {
	SEND_TO_NIC_DRIVER = 1,
	SEND_TO_HW_DRIVER,
	SEND_TO_UCODE,
	SEND_TO_UP,
	SEND_TO_SM,

	HINICADM_OVS_DRIVER = 6,
	HINICADM_ROCE_DRIVER,
	HINICADM_TOE_DRIVER,
	HINICADM_IWAP_DRIVER,
	HINICADM_FC_DRIVER,
	HINICADM_FCOE_DRIVER,
};

enum driver_cmd_type {
	TX_INFO = 1,
	Q_NUM,
	TX_WQE_INFO,
	TX_MAPPING,
	RX_INFO,
	RX_WQE_INFO,
	RX_CQE_INFO,
	UPRINT_FUNC_EN,
	UPRINT_FUNC_RESET,
	UPRINT_SET_PATH,
	UPRINT_GET_STATISTICS,
	FUNC_TYPE,
	GET_FUNC_IDX,
	GET_INTER_NUM,
	CLOSE_TX_STREAM,
	GET_DRV_VERSION,
	CLEAR_FUNC_STASTIC,
	GET_HW_STATS,
	CLEAR_HW_STATS,
	GET_SELF_TEST_RES,
	GET_CHIP_FAULT_STATS,
	GET_NUM_COS,
	SET_COS_UP_MAP,
	GET_COS_UP_MAP,
	GET_CHIP_ID,
	GET_SINGLE_CARD_INFO,
	GET_FIRMWARE_ACTIVE_STATUS,
	ROCE_DFX_FUNC,
	GET_DEVICE_ID,
	GET_PF_DEV_INFO,
	CMD_FREE_MEM,
	GET_LOOPBACK_MODE = 32,
	SET_LOOPBACK_MODE,
	SET_LINK_MODE,
	SET_PF_BW_LIMIT,
	GET_PF_BW_LIMIT,
	ROCE_CMD,
	GET_POLL_WEIGHT,
	SET_POLL_WEIGHT,
	GET_HOMOLOGUE,
	SET_HOMOLOGUE,
	GET_SSET_COUNT,
	GET_SSET_ITEMS,
	IS_DRV_IN_VM,
	LRO_ADPT_MGMT,
	SET_INTER_COAL_PARAM,
	GET_INTER_COAL_PARAM,
	GET_CHIP_INFO,
	GET_NIC_STATS_LEN,
	GET_NIC_STATS_STRING,
	GET_NIC_STATS_INFO,
	GET_PF_ID,
	SET_DCB_CFG,
	SET_PFC_PRIORITY,
	GET_PFC_INFO,
	SET_PFC_CONTROL,
	SET_ETS,
	GET_ETS_INFO,
	GET_SUPPORT_UP,
	GET_SUPPORT_TC,

	RSS_CFG = 0x40,
	RSS_INDIR,
	PORT_ID,

	GET_WIN_STAT = 0x60,
	WIN_CSR_READ = 0x61,
	WIN_CSR_WRITE = 0x62,
	WIN_API_CMD_RD = 0x63,

	VM_COMPAT_TEST = 0xFF
};

enum hinic_nic_link_mode {
	HINIC_LINK_MODE_AUTO = 0,
	HINIC_LINK_MODE_UP,
	HINIC_LINK_MODE_DOWN,
	HINIC_LINK_MODE_MAX
};

enum api_chain_cmd_type {
	API_CSR_READ,
	API_CSR_WRITE
};

enum sm_cmd_type {
	SM_CTR_RD32 = 1,
	SM_CTR_RD64_PAIR,
	SM_CTR_RD64
};

enum hinic_show_set {
	HINIC_SHOW_SSET_IO_STATS = 1,
};

#define HINIC_SHOW_ITEM_LEN	32
struct hinic_show_item {
	char	name[HINIC_SHOW_ITEM_LEN];
	u8	hexadecimal;	/* 0: decimal , 1: Hexadecimal */
	u8	rsvd[7];
	u64	value;
};

#define UP_UPDATEFW_TIME_OUT_VAL		20000U
#define UCODE_COMP_TIME_OUT_VAL		0xFF00000
#define NIC_TOOL_MAGIC					'x'

#ifdef __cplusplus
    #if __cplusplus
}
    #endif
#endif /* __cplusplus */
#endif /* __HINIC_DFX_DEF_H__ */
