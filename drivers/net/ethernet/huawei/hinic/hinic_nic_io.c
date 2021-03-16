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
#include <linux/types.h>
#include <linux/module.h>

#include "ossl_knl.h"
#include "hinic_wq.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hw.h"
#include "hinic_hwdev.h"
#include "hinic_nic_cfg.h"
#include "hinic_mgmt_interface.h"
#include "hinic_nic_io.h"
#include "hinic_nic.h"
#include "hinic_ctx_def.h"
#include "hinic_wq.h"
#include "hinic_cmdq.h"

#define HINIC_DEAULT_TX_CI_PENDING_LIMIT	0
#define HINIC_DEAULT_TX_CI_COALESCING_TIME	0

static unsigned char tx_pending_limit = HINIC_DEAULT_TX_CI_PENDING_LIMIT;
module_param(tx_pending_limit, byte, 0444);
MODULE_PARM_DESC(tx_pending_limit, "TX CI coalescing parameter pending_limit (default=0)");

static unsigned char tx_coalescing_time = HINIC_DEAULT_TX_CI_COALESCING_TIME;
module_param(tx_coalescing_time, byte, 0444);
MODULE_PARM_DESC(tx_coalescing_time, "TX CI coalescing parameter coalescing_time (default=0)");

#define WQ_PREFETCH_MAX			4
#define WQ_PREFETCH_MIN			1
#define WQ_PREFETCH_THRESHOLD		256

struct hinic_qp_ctxt_header {
	u16	num_queues;
	u16	queue_type;
	u32	addr_offset;
};

struct hinic_sq_ctxt {
	u32	ceq_attr;

	u32	ci_owner;

	u32	wq_pfn_hi;
	u32	wq_pfn_lo;

	u32	pref_cache;
	u32	pref_owner;
	u32	pref_wq_pfn_hi_ci;
	u32	pref_wq_pfn_lo;

	u32	rsvd8;
	u32	rsvd9;

	u32	wq_block_pfn_hi;
	u32	wq_block_pfn_lo;
};

struct hinic_rq_ctxt {
	u32	ceq_attr;

	u32	pi_intr_attr;

	u32	wq_pfn_hi_ci;
	u32	wq_pfn_lo;

	u32	pref_cache;
	u32	pref_owner;

	u32	pref_wq_pfn_hi_ci;
	u32	pref_wq_pfn_lo;

	u32	pi_paddr_hi;
	u32	pi_paddr_lo;

	u32	wq_block_pfn_hi;
	u32	wq_block_pfn_lo;
};

struct hinic_sq_ctxt_block {
	struct hinic_qp_ctxt_header	cmdq_hdr;
	struct hinic_sq_ctxt		sq_ctxt[HINIC_Q_CTXT_MAX];
};

struct hinic_rq_ctxt_block {
	struct hinic_qp_ctxt_header	cmdq_hdr;
	struct hinic_rq_ctxt		rq_ctxt[HINIC_Q_CTXT_MAX];
};

struct hinic_sq_db {
	u32	db_info;
};

struct hinic_addr {
	u32	addr_hi;
	u32	addr_lo;
};

struct hinic_clean_queue_ctxt {
	struct hinic_qp_ctxt_header	cmdq_hdr;
	u32				ctxt_size;
	struct hinic_addr		cqe_dma_addr[HINIC_RQ_CQ_MAX];
};

static int init_sq(struct hinic_sq *sq, struct hinic_wq *wq, u16 q_id,
		   u16 sq_msix_idx, void *cons_idx_addr, void __iomem *db_addr)
{
	sq->wq = wq;
	sq->q_id = q_id;
	sq->owner = 1;
	sq->msix_entry_idx = sq_msix_idx;

	sq->cons_idx_addr = cons_idx_addr;
	sq->db_addr = db_addr;

	return 0;
}

static int init_rq(struct hinic_rq *rq, void *dev_hdl, struct hinic_wq *wq,
		   u16 q_id, u16 rq_msix_idx)
{
	rq->wq = wq;
	rq->q_id = q_id;
	rq->cqe_dma_addr = 0;

	rq->msix_entry_idx = rq_msix_idx;

	rq->pi_virt_addr = dma_zalloc_coherent(dev_hdl, PAGE_SIZE,
					       &rq->pi_dma_addr, GFP_KERNEL);
	if (!rq->pi_virt_addr) {
		nic_err(dev_hdl, "Failed to allocate rq pi virtual addr\n");
		return -ENOMEM;
	}

	return 0;
}

void hinic_rq_cqe_addr_set(void *hwdev, u16 qid, dma_addr_t cqe_dma_ddr)
{
	struct hinic_hwdev *dev = (struct hinic_hwdev *)hwdev;
	struct hinic_nic_io *nic_io;

	nic_io = dev->nic_io;
	nic_io->qps[qid].rq.cqe_dma_addr = cqe_dma_ddr;
}

static void clean_rq(struct hinic_rq *rq, void *dev_hdl)
{
	dma_free_coherent(dev_hdl, PAGE_SIZE, rq->pi_virt_addr,
			  rq->pi_dma_addr);
}

static int create_qp(struct hinic_nic_io *nic_io, struct hinic_qp *qp,
		     u16 q_id, u16 qp_msix_idx, int max_sq_sge)
{
	struct hinic_sq *sq = &qp->sq;
	struct hinic_rq *rq = &qp->rq;
	void __iomem *db_addr;
	int err;

	err = hinic_wq_allocate(&nic_io->wqs, &nic_io->sq_wq[q_id],
				HINIC_SQ_WQEBB_SIZE,
				nic_io->hwdev->wq_page_size, nic_io->sq_depth,
				MAX_WQE_SIZE(max_sq_sge, HINIC_SQ_WQEBB_SIZE));
	if (err) {
		nic_err(nic_io->hwdev->dev_hdl, "Failed to allocate WQ for SQ\n");
		return err;
	}

	err = hinic_wq_allocate(&nic_io->wqs, &nic_io->rq_wq[q_id],
				HINIC_RQ_WQE_SIZE, nic_io->hwdev->wq_page_size,
				nic_io->rq_depth, HINIC_RQ_WQE_SIZE);
	if (err) {
		nic_err(nic_io->hwdev->dev_hdl, "Failed to allocate WQ for RQ\n");
		goto rq_alloc_err;
	}

	/* we don't use direct wqe for sq */
	err = hinic_alloc_db_addr(nic_io->hwdev, &db_addr, NULL);
	if (err) {
		nic_err(nic_io->hwdev->dev_hdl, "Failed to alloc sq doorbell addr\n");
		goto alloc_db_err;
	}

	err = init_sq(sq, &nic_io->sq_wq[q_id], q_id, qp_msix_idx,
		      HINIC_CI_VADDR(nic_io->ci_vaddr_base, q_id), db_addr);
	if (err != 0) {
		nic_err(nic_io->hwdev->dev_hdl, "Failed to init sq\n");
		goto sq_init_err;
	}

	err = init_rq(rq, nic_io->hwdev->dev_hdl, &nic_io->rq_wq[q_id],
		      q_id, qp_msix_idx);
	if (err) {
		nic_err(nic_io->hwdev->dev_hdl, "Failed to init rq\n");
		goto rq_init_err;
	}

	return 0;

rq_init_err:
sq_init_err:
	hinic_free_db_addr(nic_io->hwdev, db_addr, NULL);

alloc_db_err:
	hinic_wq_free(&nic_io->wqs, &nic_io->rq_wq[q_id]);

rq_alloc_err:
	hinic_wq_free(&nic_io->wqs, &nic_io->sq_wq[q_id]);

	return err;
}

static void destroy_qp(struct hinic_nic_io *nic_io, struct hinic_qp *qp)
{
	clean_rq(&qp->rq, nic_io->hwdev->dev_hdl);

	hinic_free_db_addr(nic_io->hwdev, qp->sq.db_addr, NULL);

	hinic_wq_free(&nic_io->wqs, qp->sq.wq);
	hinic_wq_free(&nic_io->wqs, qp->rq.wq);
}

/* alloc qps and init qps ctxt */
int hinic_create_qps(void *dev, u16 num_qp, u16 sq_depth, u16 rq_depth,
		     struct irq_info *qps_msix_arry, int max_sq_sge)
{
	struct hinic_hwdev *hwdev = dev;
	struct hinic_nic_io *nic_io;
	u16 q_id, i, max_qps;
	int err;

	if (!hwdev || !qps_msix_arry)
		return -EFAULT;

	max_qps = hinic_func_max_qnum(hwdev);
	if (num_qp > max_qps) {
		nic_err(hwdev->dev_hdl, "Create number of qps: %d > max number of qps:%d\n",
			num_qp, max_qps);
		return -EINVAL;
	}

	nic_io = hwdev->nic_io;

	nic_io->max_qps = max_qps;
	nic_io->num_qps = num_qp;
	nic_io->sq_depth = sq_depth;
	nic_io->rq_depth = rq_depth;

	err = hinic_wqs_alloc(&nic_io->wqs, 2 * num_qp, hwdev->dev_hdl);
	if (err) {
		nic_err(hwdev->dev_hdl, "Failed to allocate WQS for IO\n");
		return err;
	}

	nic_io->qps = kcalloc(num_qp, sizeof(*nic_io->qps), GFP_KERNEL);
	if (!nic_io->qps) {
		err = -ENOMEM;
		goto alloc_qps_err;
	}

	nic_io->ci_vaddr_base =
		dma_zalloc_coherent(hwdev->dev_hdl,
				    CI_TABLE_SIZE(num_qp, PAGE_SIZE),
				    &nic_io->ci_dma_base, GFP_KERNEL);
	if (!nic_io->ci_vaddr_base) {
		err = -ENOMEM;
		goto ci_base_err;
	}

	nic_io->sq_wq = kcalloc(num_qp, sizeof(*nic_io->sq_wq), GFP_KERNEL);
	if (!nic_io->sq_wq) {
		err = -ENOMEM;
		goto sq_wq_err;
	}

	nic_io->rq_wq = kcalloc(num_qp, sizeof(*nic_io->rq_wq), GFP_KERNEL);
	if (!nic_io->rq_wq) {
		err = -ENOMEM;
		goto rq_wq_err;
	}

	for (q_id = 0; q_id < num_qp; q_id++) {
		err = create_qp(nic_io, &nic_io->qps[q_id], q_id,
				qps_msix_arry[q_id].msix_entry_idx, max_sq_sge);
		if (err) {
			nic_err(hwdev->dev_hdl,
				"Failed to allocate qp %d, err: %d\n",
				q_id, err);
			goto create_qp_err;
		}
	}

	return 0;

create_qp_err:
	for (i = 0; i < q_id; i++)
		destroy_qp(nic_io, &nic_io->qps[i]);

	kfree(nic_io->rq_wq);

rq_wq_err:
	kfree(nic_io->sq_wq);

sq_wq_err:
	dma_free_coherent(hwdev->dev_hdl, CI_TABLE_SIZE(num_qp, PAGE_SIZE),
			  nic_io->ci_vaddr_base, nic_io->ci_dma_base);

ci_base_err:
	kfree(nic_io->qps);

alloc_qps_err:
	hinic_wqs_free(&nic_io->wqs);

	return err;
}
EXPORT_SYMBOL(hinic_create_qps);

void hinic_free_qps(void *dev)
{
	struct hinic_hwdev *hwdev = dev;
	struct hinic_nic_io *nic_io;
	u16 i;

	if (!hwdev)
		return;

	nic_io = hwdev->nic_io;

	for (i = 0; i < nic_io->num_qps; i++)
		destroy_qp(nic_io, &nic_io->qps[i]);

	kfree(nic_io->rq_wq);
	kfree(nic_io->sq_wq);

	dma_free_coherent(hwdev->dev_hdl,
			  CI_TABLE_SIZE(nic_io->num_qps, PAGE_SIZE),
			  nic_io->ci_vaddr_base, nic_io->ci_dma_base);

	kfree(nic_io->qps);

	hinic_wqs_free(&nic_io->wqs);
}
EXPORT_SYMBOL(hinic_free_qps);

void hinic_qp_prepare_cmdq_header(struct hinic_qp_ctxt_header *qp_ctxt_hdr,
				  enum hinic_qp_ctxt_type ctxt_type,
				  u16 num_queues, u16 max_queues, u16 q_id)
{
	qp_ctxt_hdr->queue_type = ctxt_type;
	qp_ctxt_hdr->num_queues = num_queues;

	if (ctxt_type == HINIC_QP_CTXT_TYPE_SQ)
		qp_ctxt_hdr->addr_offset =
				SQ_CTXT_OFFSET(max_queues, max_queues, q_id);
	else
		qp_ctxt_hdr->addr_offset =
				RQ_CTXT_OFFSET(max_queues, max_queues, q_id);

	qp_ctxt_hdr->addr_offset = SIZE_16BYTES(qp_ctxt_hdr->addr_offset);

	hinic_cpu_to_be32(qp_ctxt_hdr, sizeof(*qp_ctxt_hdr));
}

void hinic_sq_prepare_ctxt(struct hinic_sq *sq, u16 global_qpn,
			   struct hinic_sq_ctxt *sq_ctxt)
{
	struct hinic_wq *wq = sq->wq;
	u64 wq_page_addr;
	u64 wq_page_pfn, wq_block_pfn;
	u32 wq_page_pfn_hi, wq_page_pfn_lo;
	u32 wq_block_pfn_hi, wq_block_pfn_lo;
	u16 pi_start, ci_start;

	ci_start = (u16)wq->cons_idx;
	pi_start = (u16)wq->prod_idx;

	/* read the first page from the HW table */
	wq_page_addr = be64_to_cpu(*wq->block_vaddr);

	wq_page_pfn = WQ_PAGE_PFN(wq_page_addr);
	wq_page_pfn_hi = upper_32_bits(wq_page_pfn);
	wq_page_pfn_lo = lower_32_bits(wq_page_pfn);

	/* If only one page, use 0-level CLA */
	if (wq->num_q_pages == 1)
		wq_block_pfn = WQ_BLOCK_PFN(wq_page_addr);
	else
		wq_block_pfn = WQ_BLOCK_PFN(wq->block_paddr);

	wq_block_pfn_hi = upper_32_bits(wq_block_pfn);
	wq_block_pfn_lo = lower_32_bits(wq_block_pfn);

	sq_ctxt->ceq_attr = SQ_CTXT_CEQ_ATTR_SET(global_qpn, GLOBAL_SQ_ID) |
				SQ_CTXT_CEQ_ATTR_SET(0, EN);

	sq_ctxt->ci_owner = SQ_CTXT_CI_SET(ci_start, IDX) |
				SQ_CTXT_CI_SET(1, OWNER);

	sq_ctxt->wq_pfn_hi =
			SQ_CTXT_WQ_PAGE_SET(wq_page_pfn_hi, HI_PFN) |
			SQ_CTXT_WQ_PAGE_SET(pi_start, PI);

	sq_ctxt->wq_pfn_lo = wq_page_pfn_lo;

	sq_ctxt->pref_cache =
		SQ_CTXT_PREF_SET(WQ_PREFETCH_MIN, CACHE_MIN) |
		SQ_CTXT_PREF_SET(WQ_PREFETCH_MAX, CACHE_MAX) |
		SQ_CTXT_PREF_SET(WQ_PREFETCH_THRESHOLD, CACHE_THRESHOLD);

	sq_ctxt->pref_owner = 1;

	sq_ctxt->pref_wq_pfn_hi_ci =
		SQ_CTXT_PREF_SET(ci_start, CI) |
		SQ_CTXT_PREF_SET(wq_page_pfn_hi, WQ_PFN_HI);

	sq_ctxt->pref_wq_pfn_lo = wq_page_pfn_lo;

	sq_ctxt->wq_block_pfn_hi =
		SQ_CTXT_WQ_BLOCK_SET(wq_block_pfn_hi, PFN_HI);

	sq_ctxt->wq_block_pfn_lo = wq_block_pfn_lo;

	hinic_cpu_to_be32(sq_ctxt, sizeof(*sq_ctxt));
}

void hinic_rq_prepare_ctxt(struct hinic_rq *rq, struct hinic_rq_ctxt *rq_ctxt)
{
	struct hinic_wq *wq = rq->wq;
	u64 wq_page_addr;
	u64 wq_page_pfn, wq_block_pfn;
	u32 wq_page_pfn_hi, wq_page_pfn_lo;
	u32 wq_block_pfn_hi, wq_block_pfn_lo;
	u16 pi_start, ci_start;

	ci_start = (u16)wq->cons_idx;
	pi_start = (u16)wq->prod_idx;
	pi_start = pi_start & wq->mask;

	/* read the first page from the HW table */
	wq_page_addr = be64_to_cpu(*wq->block_vaddr);

	wq_page_pfn = WQ_PAGE_PFN(wq_page_addr);
	wq_page_pfn_hi = upper_32_bits(wq_page_pfn);
	wq_page_pfn_lo = lower_32_bits(wq_page_pfn);

	if (wq->num_q_pages == 1)
		wq_block_pfn = WQ_BLOCK_PFN(wq_page_addr);
	else
		wq_block_pfn = WQ_BLOCK_PFN(wq->block_paddr);

	wq_block_pfn_hi = upper_32_bits(wq_block_pfn);
	wq_block_pfn_lo = lower_32_bits(wq_block_pfn);

	rq_ctxt->ceq_attr = RQ_CTXT_CEQ_ATTR_SET(0, EN) |
			    RQ_CTXT_CEQ_ATTR_SET(1, OWNER);

	rq_ctxt->pi_intr_attr = RQ_CTXT_PI_SET(pi_start, IDX) |
				RQ_CTXT_PI_SET(rq->msix_entry_idx, INTR);

	rq_ctxt->wq_pfn_hi_ci = RQ_CTXT_WQ_PAGE_SET(wq_page_pfn_hi, HI_PFN) |
				RQ_CTXT_WQ_PAGE_SET(ci_start, CI);

	rq_ctxt->wq_pfn_lo = wq_page_pfn_lo;

	rq_ctxt->pref_cache =
		RQ_CTXT_PREF_SET(WQ_PREFETCH_MIN, CACHE_MIN) |
		RQ_CTXT_PREF_SET(WQ_PREFETCH_MAX, CACHE_MAX) |
		RQ_CTXT_PREF_SET(WQ_PREFETCH_THRESHOLD, CACHE_THRESHOLD);

	rq_ctxt->pref_owner = 1;

	rq_ctxt->pref_wq_pfn_hi_ci =
		RQ_CTXT_PREF_SET(wq_page_pfn_hi, WQ_PFN_HI) |
		RQ_CTXT_PREF_SET(ci_start, CI);

	rq_ctxt->pref_wq_pfn_lo = wq_page_pfn_lo;

	rq_ctxt->pi_paddr_hi = upper_32_bits(rq->pi_dma_addr);
	rq_ctxt->pi_paddr_lo = lower_32_bits(rq->pi_dma_addr);

	rq_ctxt->wq_block_pfn_hi =
		RQ_CTXT_WQ_BLOCK_SET(wq_block_pfn_hi, PFN_HI);

	rq_ctxt->wq_block_pfn_lo = wq_block_pfn_lo;

	hinic_cpu_to_be32(rq_ctxt, sizeof(*rq_ctxt));
}

static int init_sq_ctxts(struct hinic_nic_io *nic_io)
{
	struct hinic_hwdev *hwdev = nic_io->hwdev;
	struct hinic_sq_ctxt_block *sq_ctxt_block;
	struct hinic_sq_ctxt *sq_ctxt;
	struct hinic_cmd_buf *cmd_buf;
	struct hinic_qp *qp;
	u64 out_param = 0;
	u16 q_id, curr_id, global_qpn, max_ctxts, i;
	int err = 0;

	cmd_buf = hinic_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		nic_err(hwdev->dev_hdl, "Failed to allocate cmd buf\n");
		return -ENOMEM;
	}

	q_id = 0;
	while (q_id < nic_io->num_qps) {
		sq_ctxt_block = cmd_buf->buf;
		sq_ctxt = sq_ctxt_block->sq_ctxt;

		max_ctxts = (nic_io->num_qps - q_id) > HINIC_Q_CTXT_MAX ?
			     HINIC_Q_CTXT_MAX : (nic_io->num_qps - q_id);

		hinic_qp_prepare_cmdq_header(&sq_ctxt_block->cmdq_hdr,
					     HINIC_QP_CTXT_TYPE_SQ, max_ctxts,
					     nic_io->max_qps, q_id);

		for (i = 0; i < max_ctxts; i++) {
			curr_id = q_id + i;
			qp = &nic_io->qps[curr_id];
			global_qpn = nic_io->global_qpn + curr_id;

			hinic_sq_prepare_ctxt(&qp->sq, global_qpn, &sq_ctxt[i]);
		}

		cmd_buf->size = SQ_CTXT_SIZE(max_ctxts);

		err = hinic_cmdq_direct_resp(hwdev, HINIC_ACK_TYPE_CMDQ,
					     HINIC_MOD_L2NIC,
					HINIC_UCODE_CMD_MODIFY_QUEUE_CONTEXT,
					     cmd_buf, &out_param, 0);
		if (err || out_param != 0) {
			nic_err(hwdev->dev_hdl, "Failed to set SQ ctxts, err: %d, out_param: 0x%llx\n",
				err, out_param);
			err = -EFAULT;
			break;
		}

		q_id += max_ctxts;
	}

	hinic_free_cmd_buf(hwdev, cmd_buf);

	return err;
}

static int init_rq_ctxts(struct hinic_nic_io *nic_io)
{
	struct hinic_hwdev *hwdev = nic_io->hwdev;
	struct hinic_rq_ctxt_block *rq_ctxt_block;
	struct hinic_rq_ctxt *rq_ctxt;
	struct hinic_cmd_buf *cmd_buf;
	struct hinic_qp *qp;
	u64 out_param = 0;
	u16 q_id, curr_id, max_ctxts, i;
	int err = 0;

	cmd_buf = hinic_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		nic_err(hwdev->dev_hdl, "Failed to allocate cmd buf\n");
		return -ENOMEM;
	}

	q_id = 0;
	while (q_id < nic_io->num_qps) {
		rq_ctxt_block = cmd_buf->buf;
		rq_ctxt = rq_ctxt_block->rq_ctxt;

		max_ctxts = (nic_io->num_qps - q_id) > HINIC_Q_CTXT_MAX ?
				HINIC_Q_CTXT_MAX : (nic_io->num_qps - q_id);

		hinic_qp_prepare_cmdq_header(&rq_ctxt_block->cmdq_hdr,
					     HINIC_QP_CTXT_TYPE_RQ, max_ctxts,
						nic_io->max_qps, q_id);

		for (i = 0; i < max_ctxts; i++) {
			curr_id = q_id + i;
			qp = &nic_io->qps[curr_id];

			hinic_rq_prepare_ctxt(&qp->rq, &rq_ctxt[i]);
		}

		cmd_buf->size = RQ_CTXT_SIZE(max_ctxts);

		err = hinic_cmdq_direct_resp(hwdev, HINIC_ACK_TYPE_CMDQ,
					     HINIC_MOD_L2NIC,
					   HINIC_UCODE_CMD_MODIFY_QUEUE_CONTEXT,
					     cmd_buf, &out_param, 0);

		if (err || out_param != 0) {
			nic_err(hwdev->dev_hdl, "Failed to set RQ ctxts, err: %d, out_param: 0x%llx\n",
				err, out_param);
			err = -EFAULT;
			break;
		}

		q_id += max_ctxts;
	}

	hinic_free_cmd_buf(hwdev, cmd_buf);

	return err;
}

static int init_qp_ctxts(struct hinic_nic_io *nic_io)
{
	int err;

	err = init_sq_ctxts(nic_io);
	if (err)
		return err;

	err = init_rq_ctxts(nic_io);
	if (err)
		return err;

	return 0;
}

static int clean_queue_offload_ctxt(struct hinic_nic_io *nic_io,
				    enum hinic_qp_ctxt_type ctxt_type)
{
	struct hinic_hwdev *hwdev = nic_io->hwdev;
	struct hinic_clean_queue_ctxt *ctxt_block;
	struct hinic_cmd_buf *cmd_buf;
	dma_addr_t cqe_dma_addr;
	struct hinic_addr *addr;
	u64 out_param = 0;
	int i, err;

	cmd_buf = hinic_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		nic_err(hwdev->dev_hdl, "Failed to allocate cmd buf\n");
		return -ENOMEM;
	}

	ctxt_block = cmd_buf->buf;
	ctxt_block->cmdq_hdr.num_queues = nic_io->max_qps;
	ctxt_block->cmdq_hdr.queue_type = ctxt_type;
	ctxt_block->cmdq_hdr.addr_offset = 0;

	/* TSO/LRO ctxt size: 0x0:0B; 0x1:160B; 0x2:200B; 0x3:240B */
	ctxt_block->ctxt_size = 0x3;
	if ((hinic_func_type(hwdev) == TYPE_VF) &&
	    ctxt_type == HINIC_QP_CTXT_TYPE_RQ) {
		addr = ctxt_block->cqe_dma_addr;
		for (i = 0; i < nic_io->max_qps; i++) {
			cqe_dma_addr = nic_io->qps[i].rq.cqe_dma_addr;
			addr[i].addr_hi = upper_32_bits(cqe_dma_addr);
			addr[i].addr_lo = lower_32_bits(cqe_dma_addr);
		}
	}

	hinic_cpu_to_be32(ctxt_block, sizeof(*ctxt_block));

	cmd_buf->size = sizeof(*ctxt_block);

	err = hinic_cmdq_direct_resp(hwdev, HINIC_ACK_TYPE_CMDQ,
				     HINIC_MOD_L2NIC,
					HINIC_UCODE_CMD_CLEAN_QUEUE_CONTEXT,
					cmd_buf, &out_param, 0);

	if ((err) || (out_param)) {
		nic_err(hwdev->dev_hdl, "Failed to clean queue offload ctxts, err: %d, out_param: 0x%llx\n",
			err, out_param);
		err = -EFAULT;
	}

	hinic_free_cmd_buf(hwdev, cmd_buf);

	return err;
}

static int clean_qp_offload_ctxt(struct hinic_nic_io *nic_io)
{
	/* clean LRO/TSO context space */
	return (clean_queue_offload_ctxt(nic_io, HINIC_QP_CTXT_TYPE_SQ) ||
		clean_queue_offload_ctxt(nic_io, HINIC_QP_CTXT_TYPE_RQ));
}

/* init qps ctxt and set sq ci attr and arm all sq */
int hinic_init_qp_ctxts(void *dev)
{
	struct hinic_hwdev *hwdev = dev;
	struct hinic_nic_io *nic_io;
	struct hinic_sq_attr sq_attr;
	u16 q_id;
	int err;

	if (!hwdev)
		return -EINVAL;

	nic_io = hwdev->nic_io;

	err = init_qp_ctxts(nic_io);
	if (err) {
		nic_err(hwdev->dev_hdl, "Failed to init QP ctxts\n");
		return err;
	}

	/* clean LRO/TSO context space */
	err = clean_qp_offload_ctxt(nic_io);
	if (err) {
		nic_err(hwdev->dev_hdl, "Failed to clean qp offload ctxts\n");
		return err;
	}

	err = hinic_set_root_ctxt(hwdev, nic_io->rq_depth,
				  nic_io->sq_depth, nic_io->rx_buff_len);
	if (err) {
		nic_err(hwdev->dev_hdl, "Failed to set root context\n");
		return err;
	}

	for (q_id = 0; q_id < nic_io->num_qps; q_id++) {
		sq_attr.ci_dma_base =
			HINIC_CI_PADDR(nic_io->ci_dma_base, q_id) >> 2;
		sq_attr.pending_limit = tx_pending_limit;
		sq_attr.coalescing_time = tx_coalescing_time;
		sq_attr.intr_en = 1;
		sq_attr.intr_idx = nic_io->qps[q_id].sq.msix_entry_idx;
		sq_attr.l2nic_sqn = q_id;
		sq_attr.dma_attr_off = 0;
		err = hinic_set_ci_table(hwdev, q_id, &sq_attr);
		if (err) {
			nic_err(hwdev->dev_hdl, "Failed to set ci table\n");
			goto set_cons_idx_table_err;
		}
	}

	return 0;

set_cons_idx_table_err:
	hinic_clean_root_ctxt(hwdev);

	return err;
}
EXPORT_SYMBOL(hinic_init_qp_ctxts);

void hinic_free_qp_ctxts(void *hwdev)
{
	int err;

	if (!hwdev)
		return;

	hinic_qps_num_set(hwdev, 0);

	err = hinic_clean_root_ctxt(hwdev);
	if (err)
		nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to clean root ctxt\n");
}
EXPORT_SYMBOL(hinic_free_qp_ctxts);

int hinic_init_nic_hwdev(void *hwdev, u16 rx_buff_len)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_nic_io *nic_io;
	u16 global_qpn;
	int err;

	if (!hwdev)
		return -EINVAL;

	if (is_multi_bm_slave(hwdev) && hinic_support_dynamic_q(hwdev)) {
		err = hinic_reinit_cmdq_ctxts(dev);
		if (err) {
			nic_err(dev->dev_hdl, "Failed to reinit cmdq\n");
			return err;
		}
	}

	nic_io = dev->nic_io;

	err = hinic_get_base_qpn(hwdev, &global_qpn);
	if (err) {
		nic_err(dev->dev_hdl, "Failed to get base qpn\n");
		return err;
	}

	nic_io->global_qpn = global_qpn;
	nic_io->rx_buff_len = rx_buff_len;
	err = hinic_init_function_table(hwdev, nic_io->rx_buff_len);
	if (err) {
		nic_err(dev->dev_hdl, "Failed to init function table\n");
		return err;
	}

	err = hinic_enable_fast_recycle(hwdev, false);
	if (err) {
		nic_err(dev->dev_hdl, "Failed to disable fast recycle\n");
		return err;
	}

	/* get default pf bandwidth from firmware witch setted by bios */
	err = hinic_get_bios_pf_bw_limit(hwdev, &nic_io->nic_cfg.pf_bw_limit);
	if (err) {
		nic_err(dev->dev_hdl, "Failed to get pf bandwidth limit\n");
		return err;
	}

	if ((dev->func_mode == FUNC_MOD_MULTI_BM_MASTER) ||
	    (dev->func_mode == FUNC_MOD_MULTI_VM_MASTER) ||
	    (dev->board_info.service_mode == HINIC_WORK_MODE_OVS_SP)) {
		if (hinic_func_type(dev) != TYPE_VF) {
			err = hinic_disable_tx_promisc(dev);
			if (err) {
				nic_err(dev->dev_hdl, "Failed to set tx promisc\n");
				return err;
			}
		}
	}
	
	/* VFs don't set port routine command report */
	if (hinic_func_type(dev) != TYPE_VF) {
		/* Get the fw support mac reuse flag */
		err = hinic_get_fw_support_func(hwdev);
		if (err) {
			nic_err(dev->dev_hdl, "Failed to get mac reuse flag\n");
			return err;
		}

		/* Inform mgmt to send sfp's information to driver */
		err = hinic_set_port_routine_cmd_report(hwdev, true);
	}

	return err;
}
EXPORT_SYMBOL(hinic_init_nic_hwdev);

void hinic_free_nic_hwdev(void *hwdev)
{
	if (hinic_func_type(hwdev) != TYPE_VF)
		hinic_set_port_routine_cmd_report(hwdev, false);
}
EXPORT_SYMBOL(hinic_free_nic_hwdev);

int hinic_enable_tx_irq(void *hwdev, u16 q_id)
{
	return hinic_set_arm_bit(hwdev, HINIC_SET_ARM_SQ, q_id);
}
EXPORT_SYMBOL(hinic_enable_tx_irq);

int hinic_rx_tx_flush(void *hwdev)
{
	return hinic_func_rx_tx_flush(hwdev);
}
EXPORT_SYMBOL(hinic_rx_tx_flush);

int hinic_get_sq_free_wqebbs(void *hwdev, u16 q_id)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	struct hinic_wq *wq = &nic_io->sq_wq[q_id];

	return atomic_read(&wq->delta) - 1;
}
EXPORT_SYMBOL(hinic_get_sq_free_wqebbs);

int hinic_get_rq_free_wqebbs(void *hwdev, u16 q_id)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;

	return nic_io->rq_wq[q_id].delta.counter - 1;
}
EXPORT_SYMBOL(hinic_get_rq_free_wqebbs);

u16 hinic_get_sq_local_ci(void *hwdev, u16 q_id)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;

	return (u16)(nic_io->sq_wq[q_id].cons_idx & nic_io->sq_wq[q_id].mask);
}
EXPORT_SYMBOL(hinic_get_sq_local_ci);

u16 hinic_get_sq_hw_ci(void *hwdev, u16 q_id)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	struct hinic_sq *sq = &nic_io->qps[q_id].sq;

	return MASKED_SQ_IDX(sq, be16_to_cpu(*(u16 *)(sq->cons_idx_addr)));
}
EXPORT_SYMBOL(hinic_get_sq_hw_ci);

void *hinic_get_sq_wqe(void *hwdev, u16 q_id, int wqebb_cnt, u16 *pi, u8 *owner)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	struct hinic_sq *sq = &nic_io->qps[q_id].sq;
	void *wqe;

	wqe = hinic_get_wqe(sq->wq, wqebb_cnt, pi);
	if (wqe) {
		*owner = sq->owner;
		if ((*pi + wqebb_cnt) >= nic_io->sq_depth)
			sq->owner = !sq->owner;
	}

	return wqe;
}
EXPORT_SYMBOL(hinic_get_sq_wqe);

void hinic_return_sq_wqe(void *hwdev, u16 q_id, int num_wqebbs, u8 owner)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	struct hinic_sq *sq = &nic_io->qps[q_id].sq;

	if (owner != sq->owner)
		sq->owner = owner;

	atomic_add(num_wqebbs, &sq->wq->delta);
	sq->wq->prod_idx -= num_wqebbs;
}
EXPORT_SYMBOL(hinic_return_sq_wqe);

void hinic_update_sq_pi(void *hwdev, u16 q_id, int num_wqebbs, u16 *pi,
			u8 *owner)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	struct hinic_sq *sq = &nic_io->qps[q_id].sq;

	*pi = MASKED_WQE_IDX(sq->wq, sq->wq->prod_idx);

	atomic_sub(num_wqebbs, &sq->wq->delta);
	sq->wq->prod_idx += num_wqebbs;

	*owner = sq->owner;
	if ((*pi + num_wqebbs) >= nic_io->sq_depth)
		sq->owner = !sq->owner;
}
EXPORT_SYMBOL(hinic_update_sq_pi);

static void sq_prepare_db(struct hinic_sq *sq, struct hinic_sq_db *db,
			  u16 prod_idx, int cos)
{
	u32 hi_prod_idx = SQ_DB_PI_HIGH(MASKED_SQ_IDX(sq, prod_idx));

	db->db_info = SQ_DB_INFO_SET(hi_prod_idx, HI_PI) |
			SQ_DB_INFO_SET(SQ_DB, TYPE) |
			SQ_DB_INFO_SET(CFLAG_DATA_PATH, CFLAG) |
			SQ_DB_INFO_SET(cos, COS) |
			SQ_DB_INFO_SET(sq->q_id, QID);
}

static void sq_write_db(struct hinic_sq *sq, u16 prod_idx, int cos)
{
	struct hinic_sq_db sq_db;

	sq_prepare_db(sq, &sq_db, prod_idx, cos);

	/* Data should be written to HW in Big Endian Format */
	sq_db.db_info = cpu_to_be32(sq_db.db_info);

	wmb();	/* Write all before the doorbell */

	writel(sq_db.db_info, SQ_DB_ADDR(sq, prod_idx));
}

void hinic_send_sq_wqe(void *hwdev, u16 q_id, void *wqe, int wqebb_cnt, int cos)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	struct hinic_sq *sq = &nic_io->qps[q_id].sq;

	if (wqebb_cnt != 1)
		hinic_write_wqe(sq->wq, wqe, wqebb_cnt);

	sq_write_db(sq, MASKED_SQ_IDX(sq, sq->wq->prod_idx), cos);
}
EXPORT_SYMBOL(hinic_send_sq_wqe);

void hinic_update_sq_local_ci(void *hwdev, u16 q_id, int wqebb_cnt)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	struct hinic_sq *sq = &nic_io->qps[q_id].sq;

	sq->wq->cons_idx += wqebb_cnt;
	atomic_add(wqebb_cnt, &sq->wq->delta);
}
EXPORT_SYMBOL(hinic_update_sq_local_ci);

void *hinic_get_rq_wqe(void *hwdev, u16 q_id, u16 *pi)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	struct hinic_rq *rq = &nic_io->qps[q_id].rq;

	return hinic_get_wqe(rq->wq, 1, pi);
}
EXPORT_SYMBOL(hinic_get_rq_wqe);

void hinic_return_rq_wqe(void *hwdev, u16 q_id, int num_wqebbs)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	struct hinic_rq *rq = &nic_io->qps[q_id].rq;

	atomic_add(num_wqebbs, &rq->wq->delta);
	rq->wq->prod_idx -= num_wqebbs;
}
EXPORT_SYMBOL(hinic_return_rq_wqe);

void hinic_update_rq_delta(void *hwdev, u16 q_id, int num_wqebbs)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;

	nic_io->qps[q_id].rq.wq->delta.counter -= num_wqebbs;
}
EXPORT_SYMBOL(hinic_update_rq_delta);

void hinic_update_rq_hw_pi(void *hwdev, u16 q_id, u16 pi)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	struct hinic_rq *rq = &nic_io->qps[q_id].rq;

	*rq->pi_virt_addr = cpu_to_be16(pi & rq->wq->mask);
}
EXPORT_SYMBOL(hinic_update_rq_hw_pi);

u16 hinic_get_rq_local_ci(void *hwdev, u16 q_id)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;

	return (u16)(nic_io->rq_wq[q_id].cons_idx & nic_io->rq_wq[q_id].mask);
}
EXPORT_SYMBOL(hinic_get_rq_local_ci);

void hinic_update_rq_local_ci(void *hwdev, u16 q_id, int wqe_cnt)
{
	struct hinic_nic_io *nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;

	nic_io->qps[q_id].rq.wq->cons_idx += wqe_cnt;
	nic_io->qps[q_id].rq.wq->delta.counter += wqe_cnt;
}
EXPORT_SYMBOL(hinic_update_rq_local_ci);
