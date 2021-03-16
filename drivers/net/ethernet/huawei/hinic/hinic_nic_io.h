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

#ifndef HINIC_HW_NIC_IO_H_
#define HINIC_HW_NIC_IO_H_

#include "hinic_hw_mgmt.h"
#include "hinic_qe_def.h"

#define HINIC_RX_BUF_SHIFT	11
#define HINIC_RX_BUF_LEN	2048	/* buffer len must be 2^n */

#define SQ_CTRL_SET(val, member)	((u32)val << SQ_CTRL_##member##_SHIFT)

int hinic_init_nic_hwdev(void *hwdev, u16 rx_buff_len);
void hinic_free_nic_hwdev(void *hwdev);

/* alloc qps resource */
int hinic_create_qps(void *hwdev, u16 qp_num, u16 sq_depth, u16 rq_depth,
		     struct irq_info *rq_msix_arry, int max_sq_sge);
void hinic_free_qps(void *hwdev);

/* init qps ctxt and set sq ci attr and arm all sq */
int hinic_init_qp_ctxts(void *hwdev);
void hinic_free_qp_ctxts(void *hwdev);

/* function table and root context set */
int hinic_set_parameters(void *hwdev, u8 *mac, u16 rx_buf_size, u32 mtu);
void hinic_clear_parameters(void *hwdev);

/* The function is internally invoked. set_arm_bit function */
int hinic_enable_tx_irq(void *hwdev, u16 q_id);

int hinic_rx_tx_flush(void *hwdev);

/* Obtain sq/rq number of idle wqebb */
int hinic_get_sq_free_wqebbs(void *hwdev, u16 q_id);
int hinic_get_rq_free_wqebbs(void *hwdev, u16 q_id);

u16 hinic_get_sq_local_ci(void *hwdev, u16 q_id);
u16 hinic_get_sq_hw_ci(void *hwdev, u16 q_id);

void *hinic_get_sq_wqe(void *hwdev, u16 q_id,
		       int wqebb_cnt, u16 *pi, u8 *owner);

void hinic_return_sq_wqe(void *hwdev, u16 q_id, int num_wqebbs, u8 owner);

void hinic_update_sq_pi(void *hwdev, u16 q_id, int num_wqebbs,
			u16 *pi, u8 *owner);

/* including cross-page process and press the doorbell */
void hinic_send_sq_wqe(void *hwdev, u16 q_id, void *wqe,
		       int wqebb_cnt, int cos);

void hinic_update_sq_local_ci(void *hwdev, u16 q_id, int wqebb_cnt);

/* Refreshes the rq buff */
void *hinic_get_rq_wqe(void *hwdev, u16 q_id, u16 *pi);
/* gupdate rq pi, is the latest pi, function does not need to calculate */
void hinic_return_rq_wqe(void *hwdev, u16 q_id, int num_wqebbs);

void hinic_update_rq_delta(void *hwdev, u16 q_id, int num_wqebbs);

void hinic_update_rq_hw_pi(void *hwdev, u16 q_id, u16 pi);

u16 hinic_get_rq_local_ci(void *hwdev, u16 q_id);

/* Clear rx done is not performed */
void hinic_update_rq_local_ci(void *hwdev, u16 q_id, int wqe_cnt);

struct hinic_sge {
	u32		hi_addr;
	u32		lo_addr;
	u32		len;
};

void hinic_cpu_to_be32(void *data, int len);

void hinic_be32_to_cpu(void *data, int len);

void hinic_set_sge(struct hinic_sge *sge, dma_addr_t addr, u32 len);

dma_addr_t hinic_sge_to_dma(struct hinic_sge *sge);

void hinic_rq_cqe_addr_set(void *hwdev, u16 qid, dma_addr_t cqe_dma_ddr);

#endif
