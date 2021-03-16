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

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/io-mapping.h>

#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hwdev.h"

#include "hinic_csr.h"
#include "hinic_hwif.h"
#include "hinic_eqs.h"

#define WAIT_HWIF_READY_TIMEOUT		10000

#define HINIC_SELFTEST_RESULT		0x883C

/* For UEFI driver, this function can only read BAR0 */
u32 hinic_hwif_read_reg(struct hinic_hwif *hwif, u32 reg)
{
	return be32_to_cpu(readl(hwif->cfg_regs_base + reg));
}

/* For UEFI driver, this function can only write BAR0 */
void hinic_hwif_write_reg(struct hinic_hwif *hwif, u32 reg, u32 val)
{
	writel(cpu_to_be32(val), hwif->cfg_regs_base + reg);
}

/**
 * hwif_ready - test if the HW initialization passed
 * @hwdev: the pointer to hw device
 * Return: 0 - success, negative - failure
 */
static int hwif_ready(struct hinic_hwdev *hwdev)
{
	u32 addr, attr1;

	addr   = HINIC_CSR_FUNC_ATTR1_ADDR;
	attr1  = hinic_hwif_read_reg(hwdev->hwif, addr);

	if (attr1 == HINIC_PCIE_LINK_DOWN)
		return -EBUSY;

	if (!HINIC_AF1_GET(attr1, MGMT_INIT_STATUS))
		return -EBUSY;

	if (HINIC_IS_VF(hwdev)) {
		if (!HINIC_AF1_GET(attr1, PF_INIT_STATUS))
			return -EBUSY;
	}

	return 0;
}

static int wait_hwif_ready(struct hinic_hwdev *hwdev)
{
	ulong timeout = 0;

	do {
		if (!hwif_ready(hwdev))
			return 0;

		usleep_range(999, 1000);
		timeout++;
	} while (timeout <= WAIT_HWIF_READY_TIMEOUT);

	sdk_err(hwdev->dev_hdl, "Wait for hwif timeout\n");
	return -EBUSY;
}

/**
 * set_hwif_attr - set the attributes as members in hwif
 * @hwif: the hardware interface of a pci function device
 * @attr0: the first attribute that was read from the hw
 * @attr1: the second attribute that was read from the hw
 * @attr2: the third attribute that was read from the hw
 */
static void set_hwif_attr(struct hinic_hwif *hwif, u32 attr0, u32 attr1,
			  u32 attr2)
{
	hwif->attr.func_global_idx = HINIC_AF0_GET(attr0, FUNC_GLOBAL_IDX);
	hwif->attr.port_to_port_idx = HINIC_AF0_GET(attr0, P2P_IDX);
	hwif->attr.pci_intf_idx = HINIC_AF0_GET(attr0, PCI_INTF_IDX);
	hwif->attr.vf_in_pf = HINIC_AF0_GET(attr0, VF_IN_PF);
	hwif->attr.func_type = HINIC_AF0_GET(attr0, FUNC_TYPE);

	hwif->attr.ppf_idx = HINIC_AF1_GET(attr1, PPF_IDX);

	hwif->attr.num_aeqs = BIT(HINIC_AF1_GET(attr1, AEQS_PER_FUNC));
	hwif->attr.num_ceqs = BIT(HINIC_AF1_GET(attr1, CEQS_PER_FUNC));
	hwif->attr.num_irqs = BIT(HINIC_AF1_GET(attr1, IRQS_PER_FUNC));
	hwif->attr.num_dma_attr = BIT(HINIC_AF1_GET(attr1, DMA_ATTR_PER_FUNC));

	hwif->attr.global_vf_id_of_pf = HINIC_AF2_GET(attr2,
						      GLOBAL_VF_ID_OF_PF);
}

/**
 * get_hwif_attr - read and set the attributes as members in hwif
 * @hwif: the hardware interface of a pci function device
 */
static void get_hwif_attr(struct hinic_hwif *hwif)
{
	u32 addr, attr0, attr1, attr2;

	addr   = HINIC_CSR_FUNC_ATTR0_ADDR;
	attr0  = hinic_hwif_read_reg(hwif, addr);

	addr   = HINIC_CSR_FUNC_ATTR1_ADDR;
	attr1  = hinic_hwif_read_reg(hwif, addr);

	addr   = HINIC_CSR_FUNC_ATTR2_ADDR;
	attr2  = hinic_hwif_read_reg(hwif, addr);

	set_hwif_attr(hwif, attr0, attr1, attr2);
}

void hinic_set_pf_status(struct hinic_hwif *hwif, enum hinic_pf_status status)
{
	u32 attr5 = HINIC_AF5_SET(status, PF_STATUS);
	u32 addr  = HINIC_CSR_FUNC_ATTR5_ADDR;

	if (hwif->attr.func_type == TYPE_VF)
		return;

	hinic_hwif_write_reg(hwif, addr, attr5);
}

enum hinic_pf_status hinic_get_pf_status(struct hinic_hwif *hwif)
{
	u32 attr5 = hinic_hwif_read_reg(hwif, HINIC_CSR_FUNC_ATTR5_ADDR);

	return HINIC_AF5_GET(attr5, PF_STATUS);
}

enum hinic_doorbell_ctrl hinic_get_doorbell_ctrl_status(struct hinic_hwif *hwif)
{
	u32 attr4 = hinic_hwif_read_reg(hwif, HINIC_CSR_FUNC_ATTR4_ADDR);

	return HINIC_AF4_GET(attr4, DOORBELL_CTRL);
}

enum hinic_outbound_ctrl hinic_get_outbound_ctrl_status(struct hinic_hwif *hwif)
{
	u32 attr4 = hinic_hwif_read_reg(hwif, HINIC_CSR_FUNC_ATTR4_ADDR);

	return HINIC_AF4_GET(attr4, OUTBOUND_CTRL);
}

void hinic_enable_doorbell(struct hinic_hwif *hwif)
{
	u32 addr, attr4;

	addr = HINIC_CSR_FUNC_ATTR4_ADDR;
	attr4 = hinic_hwif_read_reg(hwif, addr);

	attr4 = HINIC_AF4_CLEAR(attr4, DOORBELL_CTRL);
	attr4 |= HINIC_AF4_SET(ENABLE_DOORBELL, DOORBELL_CTRL);

	hinic_hwif_write_reg(hwif, addr, attr4);
}

void hinic_disable_doorbell(struct hinic_hwif *hwif)
{
	u32 addr, attr4;

	addr = HINIC_CSR_FUNC_ATTR4_ADDR;
	attr4 = hinic_hwif_read_reg(hwif, addr);

	attr4 = HINIC_AF4_CLEAR(attr4, DOORBELL_CTRL);
	attr4 |= HINIC_AF4_SET(DISABLE_DOORBELL, DOORBELL_CTRL);

	hinic_hwif_write_reg(hwif, addr, attr4);
}

void hinic_enable_outbound(struct hinic_hwif *hwif)
{
	u32 addr, attr4;

	addr = HINIC_CSR_FUNC_ATTR4_ADDR;
	attr4 = hinic_hwif_read_reg(hwif, addr);

	attr4 = HINIC_AF4_CLEAR(attr4, OUTBOUND_CTRL);
	attr4 |= HINIC_AF4_SET(ENABLE_OUTBOUND, OUTBOUND_CTRL);

	hinic_hwif_write_reg(hwif, addr, attr4);
}

void hinic_disable_outbound(struct hinic_hwif *hwif)
{
	u32 addr, attr4;

	addr = HINIC_CSR_FUNC_ATTR4_ADDR;
	attr4 = hinic_hwif_read_reg(hwif, addr);

	attr4 = HINIC_AF4_CLEAR(attr4, OUTBOUND_CTRL);
	attr4 |= HINIC_AF4_SET(DISABLE_OUTBOUND, OUTBOUND_CTRL);

	hinic_hwif_write_reg(hwif, addr, attr4);
}

/**
 * set_ppf - try to set hwif as ppf and set the type of hwif in this case
 * @hwif: the hardware interface of a pci function device
 */
static void set_ppf(struct hinic_hwif *hwif)
{
	struct hinic_func_attr *attr = &hwif->attr;
	u32 addr, val, ppf_election;

	/* Read Modify Write */
	addr  = HINIC_CSR_PPF_ELECTION_ADDR;

	val = hinic_hwif_read_reg(hwif, addr);
	val = HINIC_PPF_ELECTION_CLEAR(val, IDX);

	ppf_election =  HINIC_PPF_ELECTION_SET(attr->func_global_idx, IDX);
	val |= ppf_election;

	hinic_hwif_write_reg(hwif, addr, val);

	/* Check PPF */
	val = hinic_hwif_read_reg(hwif, addr);

	attr->ppf_idx = HINIC_PPF_ELECTION_GET(val, IDX);
	if (attr->ppf_idx == attr->func_global_idx)
		attr->func_type = TYPE_PPF;
}

/**
 * get_mpf - get the mpf index into the hwif
 * @hwif: the hardware interface of a pci function device
 */
static void get_mpf(struct hinic_hwif *hwif)
{
	struct hinic_func_attr *attr = &hwif->attr;
	u32 mpf_election, addr;

	addr = HINIC_CSR_GLOBAL_MPF_ELECTION_ADDR;

	mpf_election = hinic_hwif_read_reg(hwif, addr);
	attr->mpf_idx = HINIC_MPF_ELECTION_GET(mpf_election, IDX);
}

/**
 * set_mpf - try to set hwif as mpf and set the mpf idx in hwif
 * @hwif: the hardware interface of a pci function device
 */
static void set_mpf(struct hinic_hwif *hwif)
{
	struct hinic_func_attr *attr = &hwif->attr;
	u32 addr, val, mpf_election;

	/* Read Modify Write */
	addr  = HINIC_CSR_GLOBAL_MPF_ELECTION_ADDR;

	val = hinic_hwif_read_reg(hwif, addr);

	val = HINIC_MPF_ELECTION_CLEAR(val, IDX);
	mpf_election = HINIC_MPF_ELECTION_SET(attr->func_global_idx, IDX);

	val |= mpf_election;
	hinic_hwif_write_reg(hwif, addr, val);
}

static void init_db_area_idx(struct hinic_hwif *hwif)
{
	struct hinic_free_db_area *free_db_area;
	u32 db_max_areas;
	u32 i;

	free_db_area = &hwif->free_db_area;
	db_max_areas = hwif->db_size / HINIC_DB_PAGE_SIZE;

	for (i = 0; i < db_max_areas; i++)
		free_db_area->db_idx[i] = i;

	free_db_area->num_free = db_max_areas;

	spin_lock_init(&free_db_area->idx_lock);
}

static int get_db_idx(struct hinic_hwif *hwif, u32 *idx)
{
	struct hinic_free_db_area *free_db_area = &hwif->free_db_area;
	u32 db_max_areas = hwif->db_size / HINIC_DB_PAGE_SIZE;
	u32 pos;
	u32 pg_idx;

	spin_lock(&free_db_area->idx_lock);

retry:
	if (free_db_area->num_free == 0) {
		spin_unlock(&free_db_area->idx_lock);
		return -ENOMEM;
	}

	free_db_area->num_free--;

	pos = free_db_area->alloc_pos++;
	pos &= db_max_areas - 1;

	pg_idx = free_db_area->db_idx[pos];

	free_db_area->db_idx[pos] = 0xFFFFFFFF;

	/* pg_idx out of range */
	if (pg_idx >= db_max_areas)
		goto retry;

	spin_unlock(&free_db_area->idx_lock);

	*idx = pg_idx;

	return 0;
}

static void free_db_idx(struct hinic_hwif *hwif, u32 idx)
{
	struct hinic_free_db_area *free_db_area = &hwif->free_db_area;
	u32 db_max_areas = hwif->db_size / HINIC_DB_PAGE_SIZE;
	u32 pos;

	if (idx >= db_max_areas)
		return;

	spin_lock(&free_db_area->idx_lock);

	pos = free_db_area->return_pos++;
	pos &= db_max_areas - 1;

	free_db_area->db_idx[pos] = idx;

	free_db_area->num_free++;

	spin_unlock(&free_db_area->idx_lock);
}

void hinic_free_db_addr(void *hwdev, void __iomem *db_base,
			void __iomem *dwqe_base)
{
	struct hinic_hwif *hwif;
	u32 idx;

	if (!hwdev || !db_base)
		return;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;
	idx = DB_IDX(db_base, hwif->db_base);

#if defined(__aarch64__)
	/* No need to unmap */
#else
	if (dwqe_base && (hwif->chip_mode == CHIP_MODE_NORMAL))
		io_mapping_unmap(dwqe_base);
#endif

	free_db_idx(hwif, idx);
}
EXPORT_SYMBOL(hinic_free_db_addr);

int hinic_alloc_db_addr(void *hwdev, void __iomem **db_base,
			void __iomem **dwqe_base)
{
	struct hinic_hwif *hwif;
	u64 offset;
	u32 idx;
	int err;

	if (!hwdev || !db_base)
		return -EINVAL;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	err = get_db_idx(hwif, &idx);
	if (err)
		return -EFAULT;

	*db_base = hwif->db_base + idx * HINIC_DB_PAGE_SIZE;

	if (!dwqe_base || hwif->chip_mode != CHIP_MODE_NORMAL)
		return 0;

	offset = ((u64)idx) << PAGE_SHIFT;

#if defined(__aarch64__)
	*dwqe_base = hwif->dwqe_mapping + offset;
#else
#ifdef HAVE_IO_MAP_WC_SIZE
	*dwqe_base = io_mapping_map_wc(hwif->dwqe_mapping, offset,
				       HINIC_DB_PAGE_SIZE);
#else
	*dwqe_base = io_mapping_map_wc(hwif->dwqe_mapping, offset);
#endif /* end of HAVE_IO_MAP_WC_SIZE */
#endif /* end of "defined(__aarch64__)" */

	if (!(*dwqe_base)) {
		hinic_free_db_addr(hwdev, *db_base, NULL);
		return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_alloc_db_addr);

void hinic_free_db_phy_addr(void *hwdev, u64 db_base, u64 dwqe_base)
{
	struct hinic_hwif *hwif;
	u32 idx;

	if (!hwdev)
		return;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;
	idx = DB_IDX(db_base, hwif->db_base_phy);

	free_db_idx(hwif, idx);
}
EXPORT_SYMBOL(hinic_free_db_phy_addr);

int hinic_alloc_db_phy_addr(void *hwdev, u64 *db_base, u64 *dwqe_base)
{
	struct hinic_hwif *hwif;
	u32 idx;
	int err;

	if (!hwdev || !db_base || !dwqe_base)
		return -EINVAL;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	err = get_db_idx(hwif, &idx);
	if (err)
		return -EFAULT;

	*db_base = hwif->db_base_phy + idx * HINIC_DB_PAGE_SIZE;

	if (hwif->chip_mode == CHIP_MODE_NORMAL)
		*dwqe_base = *db_base + HINIC_DB_DWQE_SIZE;

	return 0;
}
EXPORT_SYMBOL(hinic_alloc_db_phy_addr);

void hinic_set_msix_state(void *hwdev, u16 msix_idx, enum hinic_msix_state flag)
{
	struct hinic_hwif *hwif;
	u32 offset = msix_idx * HINIC_PCI_MSIX_ENTRY_SIZE +
		     HINIC_PCI_MSIX_ENTRY_VECTOR_CTRL;
	u32 mask_bits;

	if (!hwdev)
		return;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	mask_bits = readl(hwif->intr_regs_base + offset);
	mask_bits &= ~HINIC_PCI_MSIX_ENTRY_CTRL_MASKBIT;
	if (flag)
		mask_bits |= HINIC_PCI_MSIX_ENTRY_CTRL_MASKBIT;

	writel(mask_bits, hwif->intr_regs_base + offset);
}
EXPORT_SYMBOL(hinic_set_msix_state);

static void disable_all_msix(struct hinic_hwdev *hwdev)
{
	u16 num_irqs = hwdev->hwif->attr.num_irqs;
	u16 i;

	for (i = 0; i < num_irqs; i++)
		hinic_set_msix_state(hwdev, i, HINIC_MSIX_DISABLE);
}

int wait_until_doorbell_flush_states(struct hinic_hwif *hwif,
				     enum hinic_doorbell_ctrl states)
{
	enum hinic_doorbell_ctrl db_ctrl;
	u32 cnt = 0;

	if (!hwif)
		return -EFAULT;

	while (cnt < HINIC_WAIT_DOORBELL_AND_OUTBOUND_TIMEOUT) {
		db_ctrl = hinic_get_doorbell_ctrl_status(hwif);
		if (db_ctrl == states)
			return 0;

		usleep_range(900, 1000);
		cnt++;
	}

	return -EFAULT;
}
EXPORT_SYMBOL(wait_until_doorbell_flush_states);

static int wait_until_doorbell_and_outbound_enabled(struct hinic_hwif *hwif)
{
	enum hinic_doorbell_ctrl db_ctrl;
	enum hinic_outbound_ctrl outbound_ctrl;
	u32 cnt = 0;

	while (cnt < HINIC_WAIT_DOORBELL_AND_OUTBOUND_TIMEOUT) {
		db_ctrl = hinic_get_doorbell_ctrl_status(hwif);
		outbound_ctrl = hinic_get_outbound_ctrl_status(hwif);

		if (outbound_ctrl == ENABLE_OUTBOUND &&
		    db_ctrl == ENABLE_DOORBELL)
			return 0;

		usleep_range(900, 1000);
		cnt++;
	}

	return -EFAULT;
}

static void __print_selftest_reg(struct hinic_hwdev *hwdev)
{
	u32 addr, attr0, attr1;

	addr   = HINIC_CSR_FUNC_ATTR1_ADDR;
	attr1  = hinic_hwif_read_reg(hwdev->hwif, addr);

	if (attr1 == HINIC_PCIE_LINK_DOWN) {
		sdk_err(hwdev->dev_hdl, "PCIE is link down\n");
		return;
	}

	addr   = HINIC_CSR_FUNC_ATTR0_ADDR;
	attr0  = hinic_hwif_read_reg(hwdev->hwif, addr);
	if (HINIC_AF0_GET(attr0, FUNC_TYPE) != TYPE_VF &&
	    !HINIC_AF0_GET(attr0, PCI_INTF_IDX))
		sdk_err(hwdev->dev_hdl, "Selftest reg: 0x%08x\n",
			hinic_hwif_read_reg(hwdev->hwif,
					    HINIC_SELFTEST_RESULT));
}

/**
 * hinic_init_hwif - initialize the hw interface
 * @hwdev: the pointer to hw device
 * @cfg_reg_base: configuration base address
 * Return: 0 - success, negative - failure
 */
int hinic_init_hwif(struct hinic_hwdev *hwdev, void *cfg_reg_base,
		    void *intr_reg_base, u64 db_base_phy,
		    void *db_base, void *dwqe_mapping)
{
	struct hinic_hwif *hwif;
	int err;

	hwif = kzalloc(sizeof(*hwif), GFP_KERNEL);
	if (!hwif)
		return -ENOMEM;

	hwdev->hwif = hwif;
	hwif->pdev = hwdev->pcidev_hdl;

	hwif->cfg_regs_base = cfg_reg_base;
	hwif->intr_regs_base = intr_reg_base;

	hwif->db_base_phy = db_base_phy;
	hwif->db_base = db_base;
	hwif->dwqe_mapping = dwqe_mapping;

	hwif->db_size = hinic_get_db_size(cfg_reg_base, &hwif->chip_mode);

	sdk_info(hwdev->dev_hdl, "Doorbell size: 0x%x, chip mode: %d\n",
		 hwif->db_size, hwif->chip_mode);

	init_db_area_idx(hwif);

	err = wait_hwif_ready(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Chip status is not ready\n");
		__print_selftest_reg(hwdev);
		goto hwif_ready_err;
	}

	get_hwif_attr(hwif);

	err = wait_until_doorbell_and_outbound_enabled(hwif);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Hw doorbell/outbound is disabled\n");
		goto hwif_ready_err;
	}

	if (!HINIC_IS_VF(hwdev)) {
		set_ppf(hwif);

		if (HINIC_IS_PPF(hwdev))
			set_mpf(hwif);

		get_mpf(hwif);
	}

	disable_all_msix(hwdev);
	/* disable mgmt cpu report any event */
	hinic_set_pf_status(hwdev->hwif, HINIC_PF_STATUS_INIT);

	pr_info("global_func_idx: %d, func_type: %d, host_id: %d, ppf: %d, mpf: %d\n",
		hwif->attr.func_global_idx, hwif->attr.func_type,
		hwif->attr.pci_intf_idx, hwif->attr.ppf_idx,
		hwif->attr.mpf_idx);

	return 0;

hwif_ready_err:
	spin_lock_deinit(&hwif->free_db_area.idx_lock);
	kfree(hwif);

	return err;
}

/**
 * hinic_free_hwif - free the hw interface
 * @hwdev: the pointer to hw device
 */
void hinic_free_hwif(struct hinic_hwdev *hwdev)
{
	spin_lock_deinit(&hwdev->hwif->free_db_area.idx_lock);
	kfree(hwdev->hwif);
}

int hinic_dma_zalloc_coherent_align(void *dev_hdl, u64 size, u64 align,
				    unsigned flag,
				    struct hinic_dma_addr_align *mem_align)
{
	void *vaddr, *align_vaddr;
	dma_addr_t paddr, align_paddr;
	u64 real_size = size;

	vaddr = dma_zalloc_coherent(dev_hdl, real_size, &paddr, flag);
	if (!vaddr)
		return -ENOMEM;

	align_paddr = ALIGN(paddr, align);
	/* align */
	if (align_paddr == paddr) {
		align_vaddr = vaddr;
		goto out;
	}

	dma_free_coherent(dev_hdl, real_size, vaddr, paddr);

	/* realloc memory for align */
	real_size = size + align;
	vaddr = dma_zalloc_coherent(dev_hdl, real_size, &paddr, flag);
	if (!vaddr)
		return -ENOMEM;

	align_paddr = ALIGN(paddr, align);
	align_vaddr = (void *)((u64)vaddr + (align_paddr - paddr));

out:
	mem_align->real_size = (u32)real_size;
	mem_align->ori_vaddr = vaddr;
	mem_align->ori_paddr = paddr;
	mem_align->align_vaddr = align_vaddr;
	mem_align->align_paddr = align_paddr;

	return 0;
}

void hinic_dma_free_coherent_align(void *dev_hdl,
				   struct hinic_dma_addr_align *mem_align)
{
	dma_free_coherent(dev_hdl, mem_align->real_size,
			  mem_align->ori_vaddr, mem_align->ori_paddr);
}

u16 hinic_global_func_id(void *hwdev)
{
	struct hinic_hwif *hwif;

	if (!hwdev)
		return 0;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	return hwif->attr.func_global_idx;
}
EXPORT_SYMBOL(hinic_global_func_id);

/**
 * get function id from register,used by sriov hot migration process
 * @hwdev: the pointer to hw device
 */
u16 hinic_global_func_id_hw(void *hwdev)
{
	u32 addr, attr0;
	struct hinic_hwdev *dev;

	dev = (struct hinic_hwdev *)hwdev;
	addr   = HINIC_CSR_FUNC_ATTR0_ADDR;
	attr0  = hinic_hwif_read_reg(dev->hwif, addr);

	return HINIC_AF0_GET(attr0, FUNC_GLOBAL_IDX);
}

static int func_busy_state_check(struct hinic_hwdev *hwdev)
{
	u32 func_state;
	int cycle;

	/* set BUSY before src vm suspend and clear it before dst vm resume */
	cycle = PIPE_CYCLE_MAX;
	func_state = hinic_func_busy_state_get(hwdev);
	while (func_state && cycle) {
		usleep_range(800, 1000);
		cycle--;
		if (!cycle) {
			sdk_err(hwdev->dev_hdl, "busy_state suspend timeout");
			return -ETIMEDOUT;
		}

		func_state = hinic_func_busy_state_get(hwdev);
	}

	return 0;
}

int hinic_func_own_get(void *hwdev)
{
	struct hinic_hwdev *dev = (struct hinic_hwdev *)hwdev;
	u32 func_state;
	int err;

	if (!HINIC_IS_VF(dev))
		return 0;

restart:
	down(&dev->func_sem);

	dev->func_ref++;
	hinic_func_own_bit_set(dev, 1);

	func_state = hinic_func_busy_state_get(hwdev);
	if (func_state) {
		dev->func_ref--;
		if (dev->func_ref == 0)
			hinic_func_own_bit_set(dev, 0);

		up(&dev->func_sem);
		err = func_busy_state_check(dev);
		if (err)
			return err;
		goto restart;
	}

	up(&dev->func_sem);
	return 0;
}

void hinic_func_own_free(void *hwdev)
{
	struct hinic_hwdev *dev = (struct hinic_hwdev *)hwdev;

	if (!HINIC_IS_VF(dev))
		return;

	down(&dev->func_sem);
	dev->func_ref--;
	if (dev->func_ref == 0)
		hinic_func_own_bit_set(dev, 0);

	up(&dev->func_sem);
}

/**
 * get function id, used by sriov hot migratition process.
 * @hwdev: the pointer to hw device
 * @func_id: function id
 */
int hinic_global_func_id_get(void *hwdev, u16 *func_id)
{
	struct hinic_hwdev *dev = (struct hinic_hwdev *)hwdev;
	int err;

	/* only vf get func_id from chip reg for sriov migrate */
	if (!HINIC_IS_VF(dev)) {
		*func_id = hinic_global_func_id(hwdev);
		return 0;
	}

	err = func_busy_state_check(dev);
	if (err)
		return err;

	*func_id = hinic_global_func_id_hw(dev);
	return 0;
}

u16 hinic_intr_num(void *hwdev)
{
	struct hinic_hwif *hwif;

	if (!hwdev)
		return 0;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	return hwif->attr.num_irqs;
}
EXPORT_SYMBOL(hinic_intr_num);

u8 hinic_pf_id_of_vf(void *hwdev)
{
	struct hinic_hwif *hwif;

	if (!hwdev)
		return 0;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	return hwif->attr.port_to_port_idx;
}
EXPORT_SYMBOL(hinic_pf_id_of_vf);

u16 hinic_pf_id_of_vf_hw(void *hwdev)
{
	u32 addr, attr0;
	struct hinic_hwdev *dev;

	dev = (struct hinic_hwdev *)hwdev;
	addr   = HINIC_CSR_FUNC_ATTR0_ADDR;
	attr0  = hinic_hwif_read_reg(dev->hwif, addr);

	return HINIC_AF0_GET(attr0, P2P_IDX);
}

u8 hinic_pcie_itf_id(void *hwdev)
{
	struct hinic_hwif *hwif;

	if (!hwdev)
		return 0;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	return hwif->attr.pci_intf_idx;
}
EXPORT_SYMBOL(hinic_pcie_itf_id);

u8 hinic_vf_in_pf(void *hwdev)
{
	struct hinic_hwif *hwif;

	if (!hwdev)
		return 0;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	return hwif->attr.vf_in_pf;
}
EXPORT_SYMBOL(hinic_vf_in_pf);

enum func_type hinic_func_type(void *hwdev)
{
	struct hinic_hwif *hwif;

	if (!hwdev)
		return 0;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	return hwif->attr.func_type;
}
EXPORT_SYMBOL(hinic_func_type);

u8 hinic_ceq_num(void *hwdev)
{
	struct hinic_hwif *hwif;

	if (!hwdev)
		return 0;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	return hwif->attr.num_ceqs;
}
EXPORT_SYMBOL(hinic_ceq_num);

u8 hinic_dma_attr_entry_num(void *hwdev)
{
	struct hinic_hwif *hwif;

	if (!hwdev)
		return 0;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	return hwif->attr.num_dma_attr;
}
EXPORT_SYMBOL(hinic_dma_attr_entry_num);

u16 hinic_glb_pf_vf_offset(void *hwdev)
{
	struct hinic_hwif *hwif;

	if (!hwdev)
		return 0;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	return hwif->attr.global_vf_id_of_pf;
}
EXPORT_SYMBOL(hinic_glb_pf_vf_offset);

u8 hinic_mpf_idx(void *hwdev)
{
	struct hinic_hwif *hwif;

	if (!hwdev)
		return 0;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	return hwif->attr.mpf_idx;
}
EXPORT_SYMBOL(hinic_mpf_idx);

u8 hinic_ppf_idx(void *hwdev)
{
	struct hinic_hwif *hwif;

	if (!hwdev)
		return 0;

	hwif = ((struct hinic_hwdev *)hwdev)->hwif;

	return hwif->attr.ppf_idx;
}
EXPORT_SYMBOL(hinic_ppf_idx);

#define CEQ_CTRL_0_CHIP_MODE_SHIFT		26
#define CEQ_CTRL_0_CHIP_MODE_MASK		0xFU
#define CEQ_CTRL_0_GET(val, member)				\
		(((val) >> CEQ_CTRL_0_##member##_SHIFT) &	\
			CEQ_CTRL_0_##member##_MASK)

/**
 * hinic_get_db_size - get db size ceq ctrl: bit26~29: uP write vf mode is
 * normal(0x0), bmgw(0x1) or vmgw(0x2) and normal mode db size is 512k,
 * bmgw or vmgw mode db size is 256k
 * @cfg_reg_base: pointer to cfg_reg_base
 * @chip_mode: pointer to chip_mode
 */
u32 hinic_get_db_size(void *cfg_reg_base, enum hinic_chip_mode *chip_mode)
{
	u32 attr0, ctrl0;

	attr0 = be32_to_cpu(readl((u8 __iomem *)cfg_reg_base +
				  HINIC_CSR_FUNC_ATTR0_ADDR));

	/* PF is always normal mode & db size is 512K */
	if (HINIC_AF0_GET(attr0, FUNC_TYPE) != TYPE_VF) {
		*chip_mode = CHIP_MODE_NORMAL;
		return HINIC_DB_DWQE_SIZE;
	}

	ctrl0 = be32_to_cpu(readl((u8 __iomem *)cfg_reg_base +
				  HINIC_CSR_CEQ_CTRL_0_ADDR(0)));

	*chip_mode = CEQ_CTRL_0_GET(ctrl0, CHIP_MODE);

	switch (*chip_mode) {
	case CHIP_MODE_VMGW:
	case CHIP_MODE_BMGW:
		return HINIC_GW_VF_DB_SIZE;
	default:
		return HINIC_DB_DWQE_SIZE;
	}
}
