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

#ifndef HINIC_QP_H
#define HINIC_QP_H

#include "hinic_qe_def.h"
#include "hinic_port_cmd.h"

/* frags and linner */
#define HINIC_MAX_SQ_BUFDESCS			(MAX_SKB_FRAGS + 1)
#define HINIC_MAX_SQ_SGE			17
#define HINIC_MAX_SKB_NR_FRAGE			(HINIC_MAX_SQ_SGE - 1)
#define HINIC_GSO_MAX_SIZE			65536

struct hinic_sq_ctrl {
	u32	ctrl_fmt;
	u32	queue_info;
};

struct hinic_sq_task {
	u32	pkt_info0;
	u32	pkt_info1;
	u32	pkt_info2;
	u32	ufo_v6_identify;
	u32	pkt_info4;
	u32	rsvd5;
};

struct hinic_sq_bufdesc {
	u32	hi_addr;
	u32	lo_addr;
	u32	len;
	u32	rsvd;
};

struct hinic_sq_wqe {
	struct hinic_sq_ctrl		ctrl;
	struct hinic_sq_task		task;
	struct hinic_sq_bufdesc		buf_descs[HINIC_MAX_SQ_BUFDESCS];
};

struct hinic_rq_ctrl {
	u32	ctrl_fmt;
};

struct hinic_rq_cqe {
	u32	status;
	u32	vlan_len;

	u32	offload_type;
	u32	hash_val;
	u32	rsvd4;
	u32	rsvd5;
	u32	rsvd6;
	u32	pkt_info;
};

struct hinic_rq_cqe_sect {
	struct hinic_sge	sge;
	u32			rsvd;
};

struct hinic_rq_bufdesc {
	u32	addr_high;
	u32	addr_low;
};

struct hinic_rq_wqe {
	struct hinic_rq_ctrl		ctrl;
	u32				rsvd;
	struct hinic_rq_cqe_sect	cqe_sect;
	struct hinic_rq_bufdesc		buf_desc;
};

void hinic_prepare_sq_ctrl(struct hinic_sq_ctrl *ctrl, u32 queue_info,
			   int nr_descs, u8 owner);

u32 hinic_get_pkt_len(struct hinic_rq_cqe *cqe);

int hinic_get_super_cqe_en(struct hinic_rq_cqe *cqe);

u32 hinic_get_pkt_len_for_super_cqe(struct hinic_rq_cqe *cqe, bool last);

u32 hinic_get_pkt_num(struct hinic_rq_cqe *cqe);

int hinic_get_rx_done(struct hinic_rq_cqe *cqe);

void hinic_clear_rx_done(struct hinic_rq_cqe *cqe, u32 status_old);

void hinic_prepare_rq_wqe(void *wqe, u16 pi, dma_addr_t buf_addr,
			  dma_addr_t cqe_dma);

#ifdef static
#undef static
#define LLT_STATIC_DEF_SAVED
#endif
static inline void hinic_task_set_outter_l3(struct hinic_sq_task *task,
					    enum sq_l3_type l3_type,
					    u32 network_len)
{
	task->pkt_info2 |= SQ_TASK_INFO2_SET(l3_type, OUTER_L3TYPE) |
			SQ_TASK_INFO2_SET(network_len, OUTER_L3LEN);
}

static inline void hinic_task_set_tunnel_l4(struct hinic_sq_task *task,
					    enum sq_tunnel_l4_type l4_type,
					    u32 tunnel_len)
{
	task->pkt_info2 |= SQ_TASK_INFO2_SET(l4_type, TUNNEL_L4TYPE) |
			SQ_TASK_INFO2_SET(tunnel_len, TUNNEL_L4LEN);
}

static inline void hinic_task_set_inner_l3(struct hinic_sq_task *task,
					   enum sq_l3_type l3_type,
					   u32 network_len)
{
	task->pkt_info0 |= SQ_TASK_INFO0_SET(l3_type, INNER_L3TYPE);
	task->pkt_info1 |= SQ_TASK_INFO1_SET(network_len, INNER_L3LEN);
}

#ifdef LLT_STATIC_DEF_SAVED
#define static
#undef LLT_STATIC_DEF_SAVED
#endif

void hinic_set_cs_inner_l4(struct hinic_sq_task *task, u32 *queue_info,
			   enum sq_l4offload_type l4_offload,
			   u32 l4_len, u32 offset);

void hinic_set_tso_inner_l4(struct hinic_sq_task *task, u32 *queue_info,
			    enum sq_l4offload_type l4_offload, u32 l4_len,
			    u32 offset, u32 ip_ident, u32 mss);

void hinic_set_vlan_tx_offload(struct hinic_sq_task *task, u32 *queue_info,
			       u16 vlan_tag, u16 vlan_pri);

void hinic_task_set_tx_offload_valid(struct hinic_sq_task *task, u32 l2hdr_len);

#endif
