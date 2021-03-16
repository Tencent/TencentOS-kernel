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

#ifndef HINIC_WQ_H
#define HINIC_WQ_H

struct hinic_free_block {
	u32	page_idx;
	u32	block_idx;
};

struct hinic_wq {
	/* The addresses are 64 bit in the HW */
	u64		block_paddr;
	u64		*shadow_block_vaddr;
	u64		*block_vaddr;

	u32		wqebb_size;
	u32		wq_page_size;
	u16		q_depth;
	u32		max_wqe_size;
	u32		num_wqebbs_per_page;

	/* performance: replace mul/div as shift;
	 * num_wqebbs_per_page must be power of 2
	 */
	u32		wqebbs_per_page_shift;
	u32		page_idx;
	u32		block_idx;

	u32		num_q_pages;

	struct hinic_dma_addr_align *mem_align;

	int		cons_idx;
	int		prod_idx;

	atomic_t	delta;
	u16		mask;

	u8		*shadow_wqe;
	u16		*shadow_idx;
};

struct hinic_cmdq_pages {
	/* The addresses are 64 bit in the HW */
	u64	cmdq_page_paddr;
	u64	*cmdq_page_vaddr;
	u64	*cmdq_shadow_page_vaddr;

	void	*dev_hdl;
};

struct hinic_wqs {
	/* The addresses are 64 bit in the HW */
	u64				*page_paddr;
	u64				**page_vaddr;
	u64				**shadow_page_vaddr;

	struct hinic_free_block	*free_blocks;
	u32				alloc_blk_pos;
	u32				return_blk_pos;
	int				num_free_blks;

	/* for allocate blocks */
	spinlock_t			alloc_blocks_lock;

	u32				num_pages;

	void				*dev_hdl;
};

void hinic_wq_wqe_pg_clear(struct hinic_wq *wq);

int hinic_cmdq_alloc(struct hinic_cmdq_pages *cmdq_pages,
		     struct hinic_wq *wq, void *dev_hdl,
			int cmdq_blocks, u32 wq_page_size, u32 wqebb_size,
			u16 q_depth, u32 max_wqe_size);

void hinic_cmdq_free(struct hinic_cmdq_pages *cmdq_pages,
		     struct hinic_wq *wq, int cmdq_blocks);

int hinic_wqs_alloc(struct hinic_wqs *wqs, int num_wqs, void *dev_hdl);

void hinic_wqs_free(struct hinic_wqs *wqs);

int hinic_wq_allocate(struct hinic_wqs *wqs, struct hinic_wq *wq,
		      u32 wqebb_size, u32 wq_page_size, u16 q_depth,
			u32 max_wqe_size);

void hinic_wq_free(struct hinic_wqs *wqs, struct hinic_wq *wq);

void *hinic_get_wqebb_addr(struct hinic_wq *wq, u16 index);

u64 hinic_get_first_wqe_page_addr(struct hinic_wq *wq);

void *hinic_get_wqe(struct hinic_wq *wq, int num_wqebbs, u16 *prod_idx);

void hinic_put_wqe(struct hinic_wq *wq, int num_wqebbs);

void *hinic_read_wqe(struct hinic_wq *wq, int num_wqebbs, u16 *cons_idx);

void hinic_write_wqe(struct hinic_wq *wq, void *wqe, int num_wqebbs);

#endif
