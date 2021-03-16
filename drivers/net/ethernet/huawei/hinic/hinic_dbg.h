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

#ifndef HINIC_NIC_DBG_H_
#define HINIC_NIC_DBG_H_

u16 hinic_dbg_get_qp_num(void *hwdev);

void *hinic_dbg_get_qp_handle(void *hwdev, u16 q_id);

void *hinic_dbg_get_sq_wq_handle(void *hwdev, u16 q_id);

void *hinic_dbg_get_rq_wq_handle(void *hwdev, u16 q_id);

u16 hinic_dbg_get_sq_pi(void *hwdev, u16 q_id);

u16 hinic_dbg_get_rq_hw_pi(void *hwdev, u16 q_id);

u16 hinic_dbg_get_rq_sw_pi(void *hwdev, u16 q_id);

void *hinic_dbg_get_sq_ci_addr(void *hwdev, u16 q_id);

u64 hinic_dbg_get_sq_cla_addr(void *hwdev, u16 q_id);

u64 hinic_dbg_get_rq_cla_addr(void *hwdev, u16 q_id);

int hinic_dbg_get_sq_db_addr(void *hwdev, u16 q_id, u64 **map_addr,
			     u64 *phy_addr, u32 *pg_idx);

u16 hinic_dbg_get_global_qpn(const void *hwdev);

int hinic_dbg_get_sq_wqe_info(void *hwdev, u16 q_id, u16 idx, u16 wqebb_cnt,
			      u8 *wqe, u16 *wqe_size);

int hinic_dbg_get_rq_wqe_info(void *hwdev, u16 q_id, u16 idx, u16 wqebb_cnt,
			      u8 *wqe, u16 *wqe_size);

int hinic_dbg_lt_rd_16byte(void *hwdev, u8 dest, u8 instance,
			   u32 lt_index, u8 *data);

int hinic_dbg_lt_wr_16byte_mask(void *hwdev, u8 dest, u8 instance,
				u32 lt_index, u8 *data, u16 mask);

int hinic_sm_ctr_rd32(void *hwdev, u8 node, u8 instance,
		      u32 ctr_id, u32 *value);

int hinic_sm_ctr_rd32_clear(void *hwdev, u8 node, u8 instance,
			    u32 ctr_id, u32 *value);

int hinic_sm_ctr_wr32(void *hwdev, u8 node, u8 instance, u32 ctr_id, u32 value);

int hinic_sm_ctr_rd64(void *hwdev, u8 node, u8 instance,
		      u32 ctr_id, u64 *value);

int hinic_sm_ctr_wr64(void *hwdev, u8 node, u8 instance,
		      u32 ctr_id, u64 value);

int hinic_sm_ctr_rd64_pair(void *hwdev, u8 node, u8 instance, u32 ctr_id,
			   u64 *value1, u64 *value2);

int hinic_sm_ctr_wr64_pair(void *hwdev, u8 node, u8 instance, u32 ctr_id,
			   u64 value1, u64 value2);

int hinic_api_csr_rd32(void *hwdev, u8 dest, u32 addr, u32 *val);

int hinic_api_csr_wr32(void *hwdev, u8 dest, u32 addr, u32 val);

int hinic_api_csr_rd64(void *hwdev, u8 dest, u32 addr, u64 *val);

int hinic_dbg_get_hw_stats(const void *hwdev, u8 *hw_stats, u16 *out_size);

u16 hinic_dbg_clear_hw_stats(void *hwdev);

void hinic_get_chip_fault_stats(const void *hwdev,
				u8 *chip_fault_stats, int offset);

int hinic_dbg_get_pf_bw_limit(void *hwdev, u32 *pf_bw_limit);

#endif
