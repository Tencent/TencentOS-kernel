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

#ifndef HINIC_HWIF_H
#define HINIC_HWIF_H

#include "hinic_hwdev.h"

#define HINIC_WAIT_DOORBELL_AND_OUTBOUND_TIMEOUT	60000

struct hinic_free_db_area {
	u32		db_idx[HINIC_DB_MAX_AREAS];

	u32		num_free;

	u32		alloc_pos;
	u32		return_pos;

	/* spinlock for allocating doorbell area */
	spinlock_t	idx_lock;
};

struct hinic_func_attr {
	u16			func_global_idx;
	u8			port_to_port_idx;
	u8			pci_intf_idx;
	u8			vf_in_pf;
	enum func_type		func_type;

	u8			mpf_idx;

	u8			ppf_idx;

	u16			num_irqs;		/* max: 2 ^ 15 */
	u8			num_aeqs;		/* max: 2 ^ 3 */
	u8			num_ceqs;		/* max: 2 ^ 7 */

	u8			num_dma_attr;		/* max: 2 ^ 6 */

	u16			global_vf_id_of_pf;
};

struct hinic_hwif {
	u8 __iomem			*cfg_regs_base;
	u8 __iomem			*intr_regs_base;
	u64				db_base_phy;
	u8 __iomem			*db_base;

#if defined(__aarch64__)
	void __iomem			*dwqe_mapping;
#else
	struct io_mapping		*dwqe_mapping;
#endif
	struct hinic_free_db_area	free_db_area;

	struct hinic_func_attr		attr;

	void				*pdev;
	enum hinic_chip_mode		chip_mode;
	u32				db_size;
};

struct hinic_dma_addr_align {
	u32		real_size;

	void		*ori_vaddr;
	dma_addr_t	ori_paddr;

	void		*align_vaddr;
	dma_addr_t	align_paddr;
};

u32 hinic_hwif_read_reg(struct hinic_hwif *hwif, u32 reg);

void hinic_hwif_write_reg(struct hinic_hwif *hwif, u32 reg, u32 val);

void hinic_set_pf_status(struct hinic_hwif *hwif, enum hinic_pf_status status);

enum hinic_pf_status hinic_get_pf_status(struct hinic_hwif *hwif);

enum hinic_doorbell_ctrl
	hinic_get_doorbell_ctrl_status(struct hinic_hwif *hwif);

enum hinic_outbound_ctrl
	hinic_get_outbound_ctrl_status(struct hinic_hwif *hwif);

void hinic_enable_doorbell(struct hinic_hwif *hwif);

void hinic_disable_doorbell(struct hinic_hwif *hwif);

void hinic_enable_outbound(struct hinic_hwif *hwif);

void hinic_disable_outbound(struct hinic_hwif *hwif);

int hinic_init_hwif(struct hinic_hwdev *hwdev, void *cfg_reg_base,
		    void *intr_reg_base, u64 db_base_phy,
		    void *db_base, void *dwqe_mapping);

void hinic_free_hwif(struct hinic_hwdev *hwdev);

int wait_until_doorbell_flush_states(struct hinic_hwif *hwif,
				     enum hinic_doorbell_ctrl states);

int hinic_dma_zalloc_coherent_align(void *dev_hdl, u64 size, u64 align,
				    unsigned flag,
				    struct hinic_dma_addr_align *mem_align);

void hinic_dma_free_coherent_align(void *dev_hdl,
				   struct hinic_dma_addr_align *mem_align);

#endif
