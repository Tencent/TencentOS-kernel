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
#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hwif.h"
#include "hinic_csr.h"
#include "hinic_msix_attr.h"

#define VALID_MSIX_IDX(attr, msix_index) ((msix_index) < (attr)->num_irqs)

/**
 * hinic_msix_attr_set - set message attribute of msix entry
 * @hwif: the hardware interface of a pci function device
 * @msix_index: msix_index
 * @pending_limit: the maximum pending interrupt events (unit 8)
 * @coalesc_timer: coalesc period for interrupt (unit 8 us)
 * @lli_timer_cfg: replenishing period for low latency credit (unit 8 us)
 * @lli_credit_limit: maximum credits for low latency msix messages (unit 8)
 * @resend_timer: maximum wait for resending msix message
 *                    (unit coalesc period)
 * Return: 0 - success, negative - failure
 */
int hinic_msix_attr_set(struct hinic_hwif *hwif, u16 msix_index,
			u8 pending_limit, u8 coalesc_timer,
			u8 lli_timer_cfg, u8 lli_credit_limit,
			u8 resend_timer)
{
	u32 msix_ctrl, addr;

	if (!VALID_MSIX_IDX(&hwif->attr, msix_index))
		return -EINVAL;

	msix_ctrl = HINIC_MSIX_ATTR_SET(pending_limit, PENDING_LIMIT) |
		     HINIC_MSIX_ATTR_SET(coalesc_timer, COALESC_TIMER) |
		     HINIC_MSIX_ATTR_SET(lli_timer_cfg, LLI_TIMER)	|
		     HINIC_MSIX_ATTR_SET(lli_credit_limit, LLI_CREDIT) |
		     HINIC_MSIX_ATTR_SET(resend_timer, RESEND_TIMER);

	addr = HINIC_CSR_MSIX_CTRL_ADDR(msix_index);

	hinic_hwif_write_reg(hwif, addr, msix_ctrl);

	return 0;
}

/**
 * hinic_msix_attr_get - get message attribute of msix entry
 * @hwif: the hardware interface of a pci function device
 * @msix_index: msix_index
 * @pending_limit: the maximum pending interrupt events (unit 8)
 * @coalesc_timer_cfg: coalesc period for interrupt (unit 8 us)
 * @lli_timer_cfg: replenishing period for low latency credit (unit 8 us)
 * @lli_credit_limit: maximum credits for low latency msix messages (unit 8)
 * @resend_timer_cfg: maximum wait for resending msix message
 *                    (unit coalesc period)
 * Return: 0 - success, negative - failure
 */
int hinic_msix_attr_get(struct hinic_hwif *hwif, u16 msix_index,
			u8 *pending_limit, u8 *coalesc_timer_cfg,
			 u8 *lli_timer_cfg, u8 *lli_credit_limit,
			 u8 *resend_timer_cfg)
{
	u32 addr, val;

	if (!VALID_MSIX_IDX(&hwif->attr, msix_index))
		return -EINVAL;

	addr = HINIC_CSR_MSIX_CTRL_ADDR(msix_index);
	val  = hinic_hwif_read_reg(hwif, addr);

	*pending_limit		= HINIC_MSIX_ATTR_GET(val, PENDING_LIMIT);
	*coalesc_timer_cfg	= HINIC_MSIX_ATTR_GET(val, COALESC_TIMER);
	*lli_timer_cfg		= HINIC_MSIX_ATTR_GET(val, LLI_TIMER);
	*lli_credit_limit	= HINIC_MSIX_ATTR_GET(val, LLI_CREDIT);
	*resend_timer_cfg	= HINIC_MSIX_ATTR_GET(val, RESEND_TIMER);

	return 0;
}

/**
 * hinic_msix_attr_cnt_set - set message attribute counters of msix entry
 * @hwif: the hardware interface of a pci function device
 * @msix_index: msix_index
 * @lli_timer_cnt: replenishing period for low latency interrupt (unit 8 us)
 * @lli_credit_cnt: maximum credits for low latency msix messages (unit 8)
 * @coalesc_timer_cnt: coalesc period for interrupt (unit 8 us)
 * @pending_cnt: the maximum pending interrupt events (unit 8)
 * @resend_timer_cnt: maximum wait for resending msix message
 *                    (unit coalesc period)
 * Return: 0 - success, negative - failure
 */
int hinic_msix_attr_cnt_set(struct hinic_hwif *hwif, u16 msix_index,
			    u8 lli_timer_cnt, u8 lli_credit_cnt,
			     u8 coalesc_timer_cnt, u8 pending_cnt,
			     u8 resend_timer_cnt)
{
	u32 msix_ctrl, addr;

	if (!VALID_MSIX_IDX(&hwif->attr, msix_index))
		return -EINVAL;

	msix_ctrl = HINIC_MSIX_CNT_SET(lli_timer_cnt, LLI_TIMER)	   |
		     HINIC_MSIX_CNT_SET(lli_credit_cnt, LLI_CREDIT)	   |
		     HINIC_MSIX_CNT_SET(coalesc_timer_cnt, COALESC_TIMER)  |
		     HINIC_MSIX_CNT_SET(pending_cnt, PENDING)		   |
		     HINIC_MSIX_CNT_SET(resend_timer_cnt, RESEND_TIMER);

	addr = HINIC_CSR_MSIX_CNT_ADDR(msix_index);

	hinic_hwif_write_reg(hwif, addr, msix_ctrl);

	return 0;
}
