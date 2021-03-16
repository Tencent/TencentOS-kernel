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

#define pr_fmt(fmt) KBUILD_MODNAME ": [NIC]" fmt

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/skbuff.h>
#include <linux/slab.h>

#include "hinic_nic_io.h"
#include "hinic_qp.h"

#define BUF_DESC_SHIFT	1
#define BUF_DESC_SIZE(nr_descs)	 (((u32)nr_descs) << BUF_DESC_SHIFT)

void hinic_prepare_sq_ctrl(struct hinic_sq_ctrl *ctrl, u32 queue_info,
			   int nr_descs, u8 owner)
{
	u32 ctrl_size, task_size, bufdesc_size;

	ctrl_size = SIZE_8BYTES(sizeof(struct hinic_sq_ctrl));
	task_size = SIZE_8BYTES(sizeof(struct hinic_sq_task));
	bufdesc_size = BUF_DESC_SIZE(nr_descs);

	ctrl->ctrl_fmt = SQ_CTRL_SET(bufdesc_size, BUFDESC_SECT_LEN) |
			SQ_CTRL_SET(task_size, TASKSECT_LEN)	|
			SQ_CTRL_SET(SQ_NORMAL_WQE, DATA_FORMAT)	|
			SQ_CTRL_SET(ctrl_size, LEN)		|
			SQ_CTRL_SET(owner, OWNER);

	ctrl->ctrl_fmt = be32_to_cpu(ctrl->ctrl_fmt);

	ctrl->queue_info = queue_info;
	ctrl->queue_info |= SQ_CTRL_QUEUE_INFO_SET(1U, UC);

	if (!SQ_CTRL_QUEUE_INFO_GET(ctrl->queue_info, MSS)) {
		ctrl->queue_info |= SQ_CTRL_QUEUE_INFO_SET(TX_MSS_DEFAULT, MSS);
	} else if (SQ_CTRL_QUEUE_INFO_GET(ctrl->queue_info, MSS) < TX_MSS_MIN) {
		/* mss should not less than 80 */
		ctrl->queue_info = SQ_CTRL_QUEUE_INFO_CLEAR(ctrl->queue_info,
							    MSS);
		ctrl->queue_info |= SQ_CTRL_QUEUE_INFO_SET(TX_MSS_MIN, MSS);
	}
	ctrl->queue_info = be32_to_cpu(ctrl->queue_info);
}

int hinic_get_rx_done(struct hinic_rq_cqe *cqe)
{
	u32 status;
	int rx_done;

	status = be32_to_cpu(cqe->status);

	rx_done = RQ_CQE_STATUS_GET(status, RXDONE);
	if (!rx_done)
		return 0;

	return 1;
}

void hinic_clear_rx_done(struct hinic_rq_cqe *cqe, u32 status_old)
{
	u32 status;

	status = RQ_CQE_STATUS_CLEAR(status_old, RXDONE);

	cqe->status = cpu_to_be32(status);

	/* Make sure Rxdone has been set */
	wmb();
}

int hinic_get_super_cqe_en(struct hinic_rq_cqe *cqe)
{
	u32 pkt_info;
	int super_cqe_en;

	pkt_info = be32_to_cpu(cqe->pkt_info);

	super_cqe_en = RQ_CQE_SUPER_CQE_EN_GET(pkt_info, SUPER_CQE_EN);
	if (!super_cqe_en)
		return 0;

	return 1;
}

u32 hinic_get_pkt_len(struct hinic_rq_cqe *cqe)
{
	u32 vlan_len = be32_to_cpu(cqe->vlan_len);

	return RQ_CQE_SGE_GET(vlan_len, LEN);
}

u32 hinic_get_pkt_num(struct hinic_rq_cqe *cqe)
{
	u32 pkt_num = be32_to_cpu(cqe->pkt_info);

	return RQ_CQE_PKT_NUM_GET(pkt_num, NUM);
}

u32 hinic_get_pkt_len_for_super_cqe(struct hinic_rq_cqe *cqe,
				    bool last)
{
	u32 pkt_len = be32_to_cpu(cqe->pkt_info);

	if (!last)
		return RQ_CQE_PKT_LEN_GET(pkt_len, FIRST_LEN);
	else
		return RQ_CQE_PKT_LEN_GET(pkt_len, LAST_LEN);
}

void hinic_prepare_rq_wqe(void *wqe, u16 pi, dma_addr_t buf_addr,
			  dma_addr_t cqe_dma)
{
	struct hinic_rq_wqe *rq_wqe = (struct hinic_rq_wqe *)wqe;
	struct hinic_rq_ctrl *ctrl = &rq_wqe->ctrl;
	struct hinic_rq_cqe_sect *cqe_sect = &rq_wqe->cqe_sect;
	struct hinic_rq_bufdesc *buf_desc = &rq_wqe->buf_desc;
	u32 rq_ceq_len = sizeof(struct hinic_rq_cqe);

	ctrl->ctrl_fmt =
		RQ_CTRL_SET(SIZE_8BYTES(sizeof(*ctrl)),  LEN) |
		RQ_CTRL_SET(SIZE_8BYTES(sizeof(*cqe_sect)), COMPLETE_LEN) |
		RQ_CTRL_SET(SIZE_8BYTES(sizeof(*buf_desc)), BUFDESC_SECT_LEN) |
		RQ_CTRL_SET(RQ_COMPLETE_SGE, COMPLETE_FORMAT);

	hinic_set_sge(&cqe_sect->sge, cqe_dma, rq_ceq_len);

	buf_desc->addr_high = upper_32_bits(buf_addr);
	buf_desc->addr_low = lower_32_bits(buf_addr);
}

void hinic_set_cs_inner_l4(struct hinic_sq_task *task,
			   u32 *queue_info,
			   enum sq_l4offload_type l4_offload,
			   u32 l4_len, u32 offset)
{
	u32 tcp_udp_cs = 0, sctp = 0;
	u32 mss = TX_MSS_DEFAULT;

	/* tcp_udp_cs should be setted to calculate outter checksum when vxlan
	 * packets without inner l3 and l4
	 */
	if (unlikely(l4_offload == SCTP_OFFLOAD_ENABLE))
		sctp = 1;
	else
		tcp_udp_cs = 1;

	task->pkt_info0 |= SQ_TASK_INFO0_SET(l4_offload, L4OFFLOAD);
	task->pkt_info1 |= SQ_TASK_INFO1_SET(l4_len, INNER_L4LEN);

	*queue_info |= SQ_CTRL_QUEUE_INFO_SET(offset, PLDOFF) |
			SQ_CTRL_QUEUE_INFO_SET(tcp_udp_cs, TCPUDP_CS) |
			SQ_CTRL_QUEUE_INFO_SET(sctp, SCTP);

	*queue_info = SQ_CTRL_QUEUE_INFO_CLEAR(*queue_info, MSS);
	*queue_info |= SQ_CTRL_QUEUE_INFO_SET(mss, MSS);
}

void hinic_set_tso_inner_l4(struct hinic_sq_task *task,
			    u32 *queue_info,
			    enum sq_l4offload_type l4_offload,
			    u32 l4_len,
			    u32 offset, u32 ip_ident, u32 mss)
{
	u32 tso = 0, ufo = 0;

	if (l4_offload == TCP_OFFLOAD_ENABLE)
		tso = 1;
	else if (l4_offload == UDP_OFFLOAD_ENABLE)
		ufo = 1;

	task->ufo_v6_identify = be32_to_cpu(ip_ident);
	/* just keep the same code style here */

	task->pkt_info0 |= SQ_TASK_INFO0_SET(l4_offload, L4OFFLOAD);
	task->pkt_info0 |= SQ_TASK_INFO0_SET(tso || ufo, TSO_UFO);
	task->pkt_info1 |= SQ_TASK_INFO1_SET(l4_len, INNER_L4LEN);

	*queue_info |= SQ_CTRL_QUEUE_INFO_SET(offset, PLDOFF) |
			SQ_CTRL_QUEUE_INFO_SET(tso, TSO) |
			SQ_CTRL_QUEUE_INFO_SET(ufo, UFO) |
			SQ_CTRL_QUEUE_INFO_SET(!!l4_offload, TCPUDP_CS);
	/* cs must be calculate by hw if tso is enable */

	*queue_info = SQ_CTRL_QUEUE_INFO_CLEAR(*queue_info, MSS);
	/* qsf was initialized in prepare_sq_wqe */
	*queue_info |= SQ_CTRL_QUEUE_INFO_SET(mss, MSS);
}

void hinic_set_vlan_tx_offload(struct hinic_sq_task *task,
			       u32 *queue_info,
			       u16 vlan_tag, u16 vlan_pri)
{
	task->pkt_info0 |= SQ_TASK_INFO0_SET(vlan_tag, VLAN_TAG) |
				SQ_TASK_INFO0_SET(1U, VLAN_OFFLOAD);

	*queue_info |= SQ_CTRL_QUEUE_INFO_SET(vlan_pri, PRI);
}

void hinic_task_set_tx_offload_valid(struct hinic_sq_task *task, u32 l2hdr_len)
{
	task->pkt_info0 |= SQ_TASK_INFO0_SET(l2hdr_len, L2HDR_LEN);
}
