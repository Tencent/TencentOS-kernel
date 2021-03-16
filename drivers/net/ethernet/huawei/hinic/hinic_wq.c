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
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/vmalloc.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hwif.h"
#include "hinic_wq.h"
#include "hinic_qe_def.h"

#define	WQS_MAX_NUM_BLOCKS		128
#define WQS_FREE_BLOCKS_SIZE(wqs)	(WQS_MAX_NUM_BLOCKS * \
					sizeof((wqs)->free_blocks[0]))

static int wqs_next_block(struct hinic_wqs *wqs, u32 *page_idx,
			  u32 *block_idx);

static void wqs_return_block(struct hinic_wqs *wqs, u32 page_idx,
			     u32 block_idx);

static int queue_alloc_page(void *handle, u64 **vaddr, u64 *paddr,
			    u64 **shadow_vaddr, u64 page_sz)
{
	dma_addr_t dma_addr = 0;

	*vaddr = dma_zalloc_coherent(handle, page_sz, &dma_addr,
				     GFP_KERNEL);
	if (!*vaddr) {
		sdk_err(handle, "Failed to allocate dma to wqs page\n");
		return -ENOMEM;
	}

	if (!ADDR_4K_ALIGNED(dma_addr)) {
		sdk_err(handle, "Cla is not 4k aligned!\n");
		goto shadow_vaddr_err;
	}

	*paddr = (u64)dma_addr;

	/* use vzalloc for big mem, shadow_vaddr only used at initialization */
	*shadow_vaddr = vzalloc(page_sz);
	if (!*shadow_vaddr) {
		sdk_err(handle, "Failed to allocate shadow page vaddr\n");
		goto shadow_vaddr_err;
	}

	return 0;

shadow_vaddr_err:
	dma_free_coherent(handle, page_sz, *vaddr, dma_addr);
	return -ENOMEM;
}

static int wqs_allocate_page(struct hinic_wqs *wqs, u32 page_idx)
{
	return queue_alloc_page(wqs->dev_hdl, &wqs->page_vaddr[page_idx],
				&wqs->page_paddr[page_idx],
				&wqs->shadow_page_vaddr[page_idx],
				WQS_PAGE_SIZE);
}

static void wqs_free_page(struct hinic_wqs *wqs, u32 page_idx)
{
	dma_free_coherent(wqs->dev_hdl, WQS_PAGE_SIZE,
			  wqs->page_vaddr[page_idx],
			(dma_addr_t)wqs->page_paddr[page_idx]);
	vfree(wqs->shadow_page_vaddr[page_idx]);
}

static int cmdq_allocate_page(struct hinic_cmdq_pages *cmdq_pages)
{
	return queue_alloc_page(cmdq_pages->dev_hdl,
				&cmdq_pages->cmdq_page_vaddr,
				&cmdq_pages->cmdq_page_paddr,
				&cmdq_pages->cmdq_shadow_page_vaddr,
				CMDQ_PAGE_SIZE);
}

static void cmdq_free_page(struct hinic_cmdq_pages *cmdq_pages)
{
	dma_free_coherent(cmdq_pages->dev_hdl, CMDQ_PAGE_SIZE,
			  cmdq_pages->cmdq_page_vaddr,
			(dma_addr_t)cmdq_pages->cmdq_page_paddr);
	vfree(cmdq_pages->cmdq_shadow_page_vaddr);
}

static int alloc_wqes_shadow(struct hinic_wq *wq)
{
	u64 size;

	/* if wq->max_wqe_size == 0, we don't need to alloc shadow */
	if (wq->max_wqe_size <= wq->wqebb_size)
		return 0;

	size = (u64)wq->num_q_pages * wq->max_wqe_size;
	wq->shadow_wqe = kzalloc(size, GFP_KERNEL);
	if (!wq->shadow_wqe) {
		pr_err("Failed to allocate shadow wqe\n");
		return -ENOMEM;
	}

	size = wq->num_q_pages * sizeof(wq->prod_idx);
	wq->shadow_idx = kzalloc(size, GFP_KERNEL);
	if (!wq->shadow_idx) {
		pr_err("Failed to allocate shadow index\n");
		goto shadow_idx_err;
	}

	return 0;

shadow_idx_err:
	kfree(wq->shadow_wqe);
	return -ENOMEM;
}

static void free_wqes_shadow(struct hinic_wq *wq)
{
	if (wq->max_wqe_size <= wq->wqebb_size)
		return;

	kfree(wq->shadow_idx);
	kfree(wq->shadow_wqe);
}

static void free_wq_pages(void *handle, struct hinic_wq *wq,
			  u32 num_q_pages)
{
	u32 i;

	for (i = 0; i < num_q_pages; i++)
		hinic_dma_free_coherent_align(handle, &wq->mem_align[i]);

	free_wqes_shadow(wq);

	wq->block_vaddr = NULL;
	wq->shadow_block_vaddr = NULL;

	kfree(wq->mem_align);
}

static int alloc_wq_pages(void *dev_hdl, struct hinic_wq *wq)
{
	struct hinic_dma_addr_align *mem_align;
	u64 *vaddr, *paddr;
	u32 i, num_q_pages;
	int err;

	vaddr = wq->shadow_block_vaddr;
	paddr = wq->block_vaddr;

	num_q_pages = ALIGN(WQ_SIZE(wq), wq->wq_page_size) / wq->wq_page_size;
	if (num_q_pages > WQ_MAX_PAGES) {
		sdk_err(dev_hdl, "Number(%d) wq pages exceeds the limit\n",
			num_q_pages);
		return -EINVAL;
	}

	if (num_q_pages & (num_q_pages - 1)) {
		sdk_err(dev_hdl, "Wq num(%d) q pages must be power of 2\n",
			num_q_pages);
		return -EINVAL;
	}

	wq->num_q_pages = num_q_pages;

	err = alloc_wqes_shadow(wq);
	if (err) {
		sdk_err(dev_hdl, "Failed to allocate wqe shadow\n");
		return err;
	}

	wq->mem_align = kcalloc(wq->num_q_pages, sizeof(*wq->mem_align),
				GFP_KERNEL);
	if (!wq->mem_align) {
		sdk_err(dev_hdl, "Failed to allocate mem_align\n");
		free_wqes_shadow(wq);
		return -ENOMEM;
	}

	for (i = 0; i < num_q_pages; i++) {
		mem_align = &wq->mem_align[i];
		err = hinic_dma_zalloc_coherent_align(dev_hdl, wq->wq_page_size,
						      wq->wq_page_size,
						      GFP_KERNEL, mem_align);
		if (err) {
			sdk_err(dev_hdl, "Failed to allocate wq page\n");
			goto alloc_wq_pages_err;
		}

		*paddr = cpu_to_be64(mem_align->align_paddr);
		*vaddr = (u64)mem_align->align_vaddr;

		paddr++;
		vaddr++;
	}

	return 0;

alloc_wq_pages_err:
	free_wq_pages(dev_hdl, wq, i);

	return -ENOMEM;
}

int hinic_wq_allocate(struct hinic_wqs *wqs, struct hinic_wq *wq,
		      u32 wqebb_size, u32 wq_page_size, u16 q_depth,
		      u32 max_wqe_size)
{
	u32 num_wqebbs_per_page;
	int err;

	if (wqebb_size == 0) {
		sdk_err(wqs->dev_hdl, "Wqebb_size must be >0\n");
		return -EINVAL;
	}

	if (q_depth & (q_depth - 1)) {
		sdk_err(wqs->dev_hdl, "Wq q_depth(%d) isn't power of 2\n",
			q_depth);
		return -EINVAL;
	}

	if (wq_page_size & (wq_page_size - 1)) {
		sdk_err(wqs->dev_hdl, "Wq page_size(%d) isn't power of 2\n",
			wq_page_size);
		return -EINVAL;
	}

	num_wqebbs_per_page = ALIGN(wq_page_size, wqebb_size) / wqebb_size;

	if (num_wqebbs_per_page & (num_wqebbs_per_page - 1)) {
		sdk_err(wqs->dev_hdl, "Num(%d) wqebbs per page isn't power of 2\n",
			num_wqebbs_per_page);
		return -EINVAL;
	}

	err = wqs_next_block(wqs, &wq->page_idx, &wq->block_idx);
	if (err) {
		sdk_err(wqs->dev_hdl, "Failed to get free wqs next block\n");
		return err;
	}

	wq->wqebb_size = wqebb_size;
	wq->wq_page_size = wq_page_size;
	wq->q_depth = q_depth;
	wq->max_wqe_size = max_wqe_size;
	wq->num_wqebbs_per_page = num_wqebbs_per_page;

	wq->wqebbs_per_page_shift = (u32)ilog2(num_wqebbs_per_page);

	wq->block_vaddr = WQ_BASE_VADDR(wqs, wq);
	wq->shadow_block_vaddr = WQ_BASE_ADDR(wqs, wq);
	wq->block_paddr = WQ_BASE_PADDR(wqs, wq);

	err = alloc_wq_pages(wqs->dev_hdl, wq);
	if (err) {
		sdk_err(wqs->dev_hdl, "Failed to allocate wq pages\n");
		goto alloc_wq_pages_err;
	}

	atomic_set(&wq->delta, q_depth);
	wq->cons_idx = 0;
	wq->prod_idx = 0;
	wq->mask = q_depth - 1;

	return 0;

alloc_wq_pages_err:
	wqs_return_block(wqs, wq->page_idx, wq->block_idx);
	return err;
}

void hinic_wq_free(struct hinic_wqs *wqs, struct hinic_wq *wq)
{
	free_wq_pages(wqs->dev_hdl, wq, wq->num_q_pages);

	wqs_return_block(wqs, wq->page_idx, wq->block_idx);
}

static int wqs_next_block(struct hinic_wqs *wqs, u32 *page_idx,
			  u32 *block_idx)
{
	u32 pos;

	spin_lock(&wqs->alloc_blocks_lock);

	if (wqs->num_free_blks <= 0) {
		spin_unlock(&wqs->alloc_blocks_lock);
		return -ENOMEM;
	}
	wqs->num_free_blks--;

	pos = wqs->alloc_blk_pos++;
	pos &= WQS_MAX_NUM_BLOCKS - 1;

	*page_idx = wqs->free_blocks[pos].page_idx;
	*block_idx = wqs->free_blocks[pos].block_idx;

	wqs->free_blocks[pos].page_idx = 0xFFFFFFFF;
	wqs->free_blocks[pos].block_idx = 0xFFFFFFFF;

	spin_unlock(&wqs->alloc_blocks_lock);

	return 0;
}

static void wqs_return_block(struct hinic_wqs *wqs, u32 page_idx,
			     u32 block_idx)
{
	u32 pos;

	spin_lock(&wqs->alloc_blocks_lock);

	wqs->num_free_blks++;

	pos = wqs->return_blk_pos++;
	pos &= WQS_MAX_NUM_BLOCKS - 1;

	wqs->free_blocks[pos].page_idx = page_idx;
	wqs->free_blocks[pos].block_idx = block_idx;

	spin_unlock(&wqs->alloc_blocks_lock);
}

static void init_wqs_blocks_arr(struct hinic_wqs *wqs)
{
	u32 page_idx, blk_idx, pos = 0;

	for (page_idx = 0; page_idx < wqs->num_pages; page_idx++) {
		for (blk_idx = 0; blk_idx < WQS_BLOCKS_PER_PAGE; blk_idx++) {
			wqs->free_blocks[pos].page_idx = page_idx;
			wqs->free_blocks[pos].block_idx = blk_idx;
			pos++;
		}
	}

	wqs->alloc_blk_pos = 0;
	wqs->return_blk_pos = 0;
	wqs->num_free_blks = WQS_MAX_NUM_BLOCKS;
	spin_lock_init(&wqs->alloc_blocks_lock);
}

void hinic_wq_wqe_pg_clear(struct hinic_wq *wq)
{
	u64 *block_vaddr;
	u32 pg_idx;

	block_vaddr = wq->shadow_block_vaddr;

	atomic_set(&wq->delta, wq->q_depth);
	wq->cons_idx = 0;
	wq->prod_idx = 0;

	for (pg_idx = 0; pg_idx < wq->num_q_pages; pg_idx++)
		memset((void *)(*(block_vaddr + pg_idx)), 0, wq->wq_page_size);
}

int hinic_cmdq_alloc(struct hinic_cmdq_pages *cmdq_pages,
		     struct hinic_wq *wq, void *dev_hdl,
			int cmdq_blocks, u32 wq_page_size, u32 wqebb_size,
			u16 q_depth, u32 max_wqe_size)
{
	int i, j, err = -ENOMEM;

	if (q_depth & (q_depth - 1)) {
		sdk_err(dev_hdl, "Cmdq q_depth(%d) isn't power of 2\n",
			q_depth);
		return -EINVAL;
	}

	cmdq_pages->dev_hdl = dev_hdl;

	err = cmdq_allocate_page(cmdq_pages);
	if (err) {
		sdk_err(dev_hdl, "Failed to allocate CMDQ page\n");
		return err;
	}

	for (i = 0; i < cmdq_blocks; i++) {
		wq[i].page_idx = 0;
		wq[i].block_idx = (u32)i;
		wq[i].wqebb_size = wqebb_size;
		wq[i].wq_page_size = wq_page_size;
		wq[i].q_depth = q_depth;
		wq[i].max_wqe_size = max_wqe_size;
		wq[i].num_wqebbs_per_page =
				ALIGN(wq_page_size, wqebb_size) / wqebb_size;

		wq[i].wqebbs_per_page_shift =
			(u32)ilog2(wq[i].num_wqebbs_per_page);

		wq[i].block_vaddr = CMDQ_BASE_VADDR(cmdq_pages, &wq[i]);
		wq[i].shadow_block_vaddr = CMDQ_BASE_ADDR(cmdq_pages, &wq[i]);
		wq[i].block_paddr = CMDQ_BASE_PADDR(cmdq_pages, &wq[i]);

		err = alloc_wq_pages(cmdq_pages->dev_hdl, &wq[i]);
		if (err) {
			sdk_err(dev_hdl, "Failed to alloc CMDQ blocks\n");
			goto cmdq_block_err;
		}

		atomic_set(&wq[i].delta, q_depth);
		wq[i].cons_idx = 0;
		wq[i].prod_idx = 0;
		wq[i].mask = q_depth - 1;
	}

	return 0;

cmdq_block_err:
	for (j = 0; j < i; j++)
		free_wq_pages(cmdq_pages->dev_hdl, &wq[j], wq[j].num_q_pages);

	cmdq_free_page(cmdq_pages);
	return err;
}

void hinic_cmdq_free(struct hinic_cmdq_pages *cmdq_pages,
		     struct hinic_wq *wq, int cmdq_blocks)
{
	int i;

	for (i = 0; i < cmdq_blocks; i++)
		free_wq_pages(cmdq_pages->dev_hdl, &wq[i], wq[i].num_q_pages);

	cmdq_free_page(cmdq_pages);
}

static int alloc_page_addr(struct hinic_wqs *wqs)
{
	u64 size = wqs->num_pages * sizeof(*wqs->page_paddr);

	wqs->page_paddr = kzalloc(size, GFP_KERNEL);
	if (!wqs->page_paddr)
		return -ENOMEM;

	size = wqs->num_pages * sizeof(*wqs->page_vaddr);
	wqs->page_vaddr = kzalloc(size, GFP_KERNEL);
	if (!wqs->page_vaddr)
		goto page_vaddr_err;

	size = wqs->num_pages * sizeof(*wqs->shadow_page_vaddr);
	wqs->shadow_page_vaddr = kzalloc(size, GFP_KERNEL);
	if (!wqs->shadow_page_vaddr)
		goto page_shadow_vaddr_err;

	return 0;

page_shadow_vaddr_err:
	kfree(wqs->page_vaddr);

page_vaddr_err:
	kfree(wqs->page_paddr);
	return -ENOMEM;
}

static void free_page_addr(struct hinic_wqs *wqs)
{
	kfree(wqs->shadow_page_vaddr);
	kfree(wqs->page_vaddr);
	kfree(wqs->page_paddr);
}

int hinic_wqs_alloc(struct hinic_wqs *wqs, int num_wqs, void *dev_hdl)
{
	u32 i, page_idx;
	int err;

	wqs->dev_hdl = dev_hdl;
	wqs->num_pages = WQ_NUM_PAGES(num_wqs);

	if (alloc_page_addr(wqs)) {
		sdk_err(dev_hdl, "Failed to allocate mem for page addresses\n");
		return -ENOMEM;
	}

	for (page_idx = 0; page_idx < wqs->num_pages; page_idx++) {
		err = wqs_allocate_page(wqs, page_idx);
		if (err) {
			sdk_err(dev_hdl, "Failed wq page allocation\n");
			goto wq_allocate_page_err;
		}
	}

	wqs->free_blocks = kzalloc(WQS_FREE_BLOCKS_SIZE(wqs), GFP_KERNEL);
	if (!wqs->free_blocks) {
		err = -ENOMEM;
		goto alloc_blocks_err;
	}

	init_wqs_blocks_arr(wqs);
	return 0;

alloc_blocks_err:
wq_allocate_page_err:
	for (i = 0; i < page_idx; i++)
		wqs_free_page(wqs, i);

	free_page_addr(wqs);
	return err;
}

void hinic_wqs_free(struct hinic_wqs *wqs)
{
	u32 page_idx;

	spin_lock_deinit(&wqs->alloc_blocks_lock);

	for (page_idx = 0; page_idx < wqs->num_pages; page_idx++)
		wqs_free_page(wqs, page_idx);

	free_page_addr(wqs);
	kfree(wqs->free_blocks);
}

static void copy_wqe_to_shadow(struct hinic_wq *wq, void *shadow_addr,
			       int num_wqebbs, u16 prod_idx)
{
	u8 *shadow_wqebb_addr, *wqe_page_addr, *wqebb_addr;
	u32 i, offset;
	u16 idx;

	for (i = 0; i < (u32)num_wqebbs; i++) {
		offset = i * wq->wqebb_size;
		shadow_wqebb_addr = (u8 *)shadow_addr + offset;

		idx = MASKED_WQE_IDX(wq, prod_idx + i);
		wqe_page_addr = WQ_PAGE_ADDR(wq, idx);
		wqebb_addr = wqe_page_addr +
				WQE_PAGE_OFF(wq, MASKED_WQE_IDX(wq, idx));

		memcpy(shadow_wqebb_addr, wqebb_addr, wq->wqebb_size);
	}
}

static void copy_wqe_from_shadow(struct hinic_wq *wq, void *shadow_addr,
				 int num_wqebbs, u16 prod_idx)
{
	u8 *shadow_wqebb_addr, *wqe_page_addr, *wqebb_addr;
	u32 i, offset;
	u16 idx;

	for (i = 0; i < (u32)num_wqebbs; i++) {
		offset = i * wq->wqebb_size;
		shadow_wqebb_addr = (u8 *)shadow_addr + offset;

		idx = MASKED_WQE_IDX(wq, prod_idx + i);
		wqe_page_addr = WQ_PAGE_ADDR(wq, idx);
		wqebb_addr = wqe_page_addr +
				WQE_PAGE_OFF(wq, MASKED_WQE_IDX(wq, idx));

		memcpy(wqebb_addr, shadow_wqebb_addr, wq->wqebb_size);
	}
}

void *hinic_get_wqebb_addr(struct hinic_wq *wq, u16 index)
{
	return WQ_PAGE_ADDR(wq, index) + WQE_PAGE_OFF(wq, index);
}

u64 hinic_get_first_wqe_page_addr(struct hinic_wq *wq)
{
	return be64_to_cpu(*wq->block_vaddr);
}

void *hinic_get_wqe(struct hinic_wq *wq, int num_wqebbs, u16 *prod_idx)
{
	u32 curr_pg, end_pg;
	u16 curr_prod_idx, end_prod_idx;

	if (atomic_sub_return(num_wqebbs, &wq->delta) < 0) {
		atomic_add(num_wqebbs, &wq->delta);
		return NULL;
	}

	/* use original cur_pi and end_pi, no need queue depth mask as
	 * WQE_PAGE_NUM will do num_queue_pages mask
	 */
	curr_prod_idx = (u16)wq->prod_idx;
	wq->prod_idx += num_wqebbs;

	/* end prod index should points to the last wqebb of wqe,
	 * therefore minus 1
	 */
	end_prod_idx = (u16)wq->prod_idx - 1;

	curr_pg = WQE_PAGE_NUM(wq, curr_prod_idx);
	end_pg = WQE_PAGE_NUM(wq, end_prod_idx);

	*prod_idx = MASKED_WQE_IDX(wq, curr_prod_idx);

	/* If we only have one page, still need to get shadown wqe when
	 * wqe rolling-over page
	 */
	if (curr_pg != end_pg || MASKED_WQE_IDX(wq, end_prod_idx) < *prod_idx) {
		u32 offset = curr_pg * wq->max_wqe_size;
		u8 *shadow_addr = wq->shadow_wqe + offset;

		wq->shadow_idx[curr_pg] = *prod_idx;
		return shadow_addr;
	}

	return WQ_PAGE_ADDR(wq, *prod_idx) + WQE_PAGE_OFF(wq, *prod_idx);
}

void hinic_put_wqe(struct hinic_wq *wq, int num_wqebbs)
{
	atomic_add(num_wqebbs, &wq->delta);
	wq->cons_idx += num_wqebbs;
}

void *hinic_read_wqe(struct hinic_wq *wq, int num_wqebbs, u16 *cons_idx)
{
	u32 curr_pg, end_pg;
	u16 curr_cons_idx, end_cons_idx;

	if ((atomic_read(&wq->delta) + num_wqebbs) > wq->q_depth)
		return NULL;

	curr_cons_idx = (u16)wq->cons_idx;

	curr_cons_idx = MASKED_WQE_IDX(wq, curr_cons_idx);
	end_cons_idx = MASKED_WQE_IDX(wq, curr_cons_idx + num_wqebbs - 1);

	curr_pg = WQE_PAGE_NUM(wq, curr_cons_idx);
	end_pg = WQE_PAGE_NUM(wq, end_cons_idx);

	*cons_idx = curr_cons_idx;

	if (curr_pg != end_pg) {
		u32 offset = curr_pg * wq->max_wqe_size;
		u8 *shadow_addr = wq->shadow_wqe + offset;

		copy_wqe_to_shadow(wq, shadow_addr, num_wqebbs, *cons_idx);

		return shadow_addr;
	}

	return WQ_PAGE_ADDR(wq, *cons_idx) + WQE_PAGE_OFF(wq, *cons_idx);
}

static inline int wqe_shadow(struct hinic_wq *wq, const void *wqe)
{
	void *end_wqe_shadow_addr;
	u32 wqe_shadow_size = wq->num_q_pages * wq->max_wqe_size;

	end_wqe_shadow_addr = &wq->shadow_wqe[wqe_shadow_size];

	return WQE_IN_RANGE(wqe, wq->shadow_wqe, end_wqe_shadow_addr);
}

void hinic_write_wqe(struct hinic_wq *wq, void *wqe, int num_wqebbs)
{
	u16 curr_pg;
	u16 prod_idx;

	if (wqe_shadow(wq, wqe)) {
		curr_pg = WQE_SHADOW_PAGE(wq, wqe);
		prod_idx = wq->shadow_idx[curr_pg];

		copy_wqe_from_shadow(wq, wqe, num_wqebbs, prod_idx);
	}
}
