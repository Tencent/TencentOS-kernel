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
#include "hinic_hw_mgmt.h"
#include "hinic_hw.h"
#include "hinic_hwdev.h"
#include "hinic_hwif.h"
#include "hinic_wq.h"
#include "hinic_nic_cfg.h"
#include "hinic_mgmt_interface.h"
#include "hinic_nic_io.h"
#include "hinic_nic.h"
#include "hinic_dbg.h"

#define INVALID_PI (0xFFFF)

u16 hinic_dbg_get_qp_num(void *hwdev)
{
	struct hinic_nic_io *nic_io;

	if (!hwdev)
		return 0;

	nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	if (!nic_io)
		return 0;

	return nic_io->num_qps;
}

void *hinic_dbg_get_qp_handle(void *hwdev, u16 q_id)
{
	struct hinic_nic_io *nic_io;

	if (!hwdev)
		return NULL;

	nic_io = ((struct hinic_hwdev *)hwdev)->nic_io;
	if (!nic_io)
		return NULL;

	if (q_id >= nic_io->num_qps)
		return NULL;

	return &nic_io->qps[q_id];
}

void *hinic_dbg_get_sq_wq_handle(void *hwdev, u16 q_id)
{
	struct hinic_qp	*qp = hinic_dbg_get_qp_handle(hwdev, q_id);

	if (!qp)
		return NULL;

	return qp->sq.wq;
}

void *hinic_dbg_get_rq_wq_handle(void *hwdev, u16 q_id)
{
	struct hinic_qp	*qp = hinic_dbg_get_qp_handle(hwdev, q_id);

	if (!qp)
		return NULL;

	return qp->rq.wq;
}

u16 hinic_dbg_get_sq_pi(void *hwdev, u16 q_id)
{
	struct hinic_wq *wq = hinic_dbg_get_sq_wq_handle(hwdev, q_id);

	if (!wq)
		return 0;

	return ((u16)wq->prod_idx) & wq->mask;
}

u16 hinic_dbg_get_rq_hw_pi(void *hwdev, u16 q_id)
{
	struct hinic_qp *qp = hinic_dbg_get_qp_handle(hwdev, q_id);

	if (qp)
		return cpu_to_be16(*qp->rq.pi_virt_addr);

	nic_err(((struct hinic_hwdev *)hwdev)->dev_hdl, "Get rq hw pi failed!\n");

	return INVALID_PI;
}

u16 hinic_dbg_get_rq_sw_pi(void *hwdev, u16 q_id)
{
	struct hinic_wq *wq = hinic_dbg_get_rq_wq_handle(hwdev, q_id);

	if (!wq)
		return 0;

	return ((u16)wq->prod_idx) & wq->mask;
}

void *hinic_dbg_get_sq_ci_addr(void *hwdev, u16 q_id)
{
	struct hinic_qp	*qp = hinic_dbg_get_qp_handle(hwdev, q_id);

	if (!qp)
		return NULL;

	return qp->sq.cons_idx_addr;
}

u64 hinic_dbg_get_sq_cla_addr(void *hwdev, u16 q_id)
{
	struct hinic_qp	*qp = hinic_dbg_get_qp_handle(hwdev, q_id);

	if (!qp)
		return 0;

	return qp->sq.wq->block_paddr;
}

u64 hinic_dbg_get_rq_cla_addr(void *hwdev, u16 q_id)
{
	struct hinic_qp	*qp = hinic_dbg_get_qp_handle(hwdev, q_id);

	if (!qp)
		return 0;

	return qp->rq.wq->block_paddr;
}

int hinic_dbg_get_sq_db_addr(void *hwdev, u16 q_id, u64 **map_addr,
			     u64 *phy_addr, u32 *pg_idx)
{
	struct hinic_qp	*qp;
	struct hinic_hwif *hwif;

	qp = hinic_dbg_get_qp_handle(hwdev, q_id);
	if (!qp)
		return -EFAULT;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	*map_addr = (u64 *)qp->sq.db_addr;
	*pg_idx = DB_IDX(qp->sq.db_addr, hwif->db_base);
	*phy_addr = hwif->db_base_phy + (*pg_idx) * HINIC_DB_PAGE_SIZE;

	return 0;
}

u16 hinic_dbg_get_global_qpn(const void *hwdev)
{
	if (!hwdev)
		return 0;

	return ((struct hinic_hwdev *)hwdev)->nic_io->global_qpn;
}

static int get_wqe_info(struct hinic_wq *wq, u16 idx, u16 wqebb_cnt,
			u8 *wqe, u16 *wqe_size)
{
	void *src_wqe;
	u32 offset;
	u16 i;

	if (idx + wqebb_cnt > wq->q_depth)
		return -EFAULT;

	if (*wqe_size != (u16)(wq->wqebb_size * wqebb_cnt)) {
		pr_err("Unexpect out buf size from user :%d, expect: %d\n",
		       *wqe_size, (u16)(wq->wqebb_size * wqebb_cnt));
		return -EFAULT;
	}

	for (i = 0; i < wqebb_cnt; i++) {
		src_wqe = (void *)hinic_slq_get_addr(wq, idx + i);
		offset = i * wq->wqebb_size;
		memcpy(wqe + offset, src_wqe, wq->wqebb_size);
	}

	return 0;
}

int hinic_dbg_get_sq_wqe_info(void *hwdev, u16 q_id, u16 idx, u16 wqebb_cnt,
			      u8 *wqe, u16 *wqe_size)
{
	struct hinic_wq *wq;
	int err;

	wq = hinic_dbg_get_sq_wq_handle(hwdev, q_id);
	if (!wq)
		return -EFAULT;

	err = get_wqe_info(wq, idx, wqebb_cnt, wqe, wqe_size);

	return err;
}

int hinic_dbg_get_rq_wqe_info(void *hwdev, u16 q_id, u16 idx, u16 wqebb_cnt,
			      u8 *wqe, u16 *wqe_size)
{
	struct hinic_wq *wq;
	int err;

	wq = hinic_dbg_get_rq_wq_handle(hwdev, q_id);
	if (!wq)
		return -EFAULT;

	err = get_wqe_info(wq, idx, wqebb_cnt, wqe, wqe_size);

	return err;
}

int hinic_dbg_get_hw_stats(const void *hwdev, u8 *hw_stats, u16 *out_size)
{
	if (!hw_stats || *out_size != sizeof(struct hinic_hw_stats)) {
		pr_err("Unexpect out buf size from user :%d, expect: %lu\n",
		       *out_size, sizeof(struct hinic_hw_stats));
		return -EFAULT;
	}

	memcpy(hw_stats, &((struct hinic_hwdev *)hwdev)->hw_stats,
	       sizeof(struct hinic_hw_stats));
	return 0;
}

u16 hinic_dbg_clear_hw_stats(void *hwdev)
{
	memset((void *)&((struct hinic_hwdev *)hwdev)->hw_stats, 0,
	       sizeof(struct hinic_hw_stats));
	memset((void *)((struct hinic_hwdev *)hwdev)->chip_fault_stats, 0,
	       HINIC_CHIP_FAULT_SIZE);
	return sizeof(struct hinic_hw_stats);
}

void hinic_get_chip_fault_stats(const void *hwdev,
				u8 *chip_fault_stats, int offset)
{
	int copy_len = HINIC_CHIP_FAULT_SIZE - offset;

	if (offset < 0 || offset > HINIC_CHIP_FAULT_SIZE) {
		pr_err("Invalid chip offset value: %d\n", offset);
		return;
	}

	if (offset + MAX_DRV_BUF_SIZE <= HINIC_CHIP_FAULT_SIZE)
		memcpy(chip_fault_stats,
		       ((struct hinic_hwdev *)hwdev)->chip_fault_stats + offset,
		       MAX_DRV_BUF_SIZE);
	else
		memcpy(chip_fault_stats,
		       ((struct hinic_hwdev *)hwdev)->chip_fault_stats + offset,
		       copy_len);
}

int hinic_dbg_get_pf_bw_limit(void *hwdev, u32 *pf_bw_limit)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_nic_cfg *nic_cfg;

	if (!hwdev)
		return -EINVAL;

	if (!dev->nic_io)
		return -EINVAL;

	nic_cfg = &dev->nic_io->nic_cfg;

	*pf_bw_limit = nic_cfg->pf_bw_limit;

	return 0;
}
