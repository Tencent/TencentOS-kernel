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

#ifndef HINIC_MSIX_ATTR_H_
#define HINIC_MSIX_ATTR_H_

#define	HINIC_MSIX_PENDING_LIMIT_SHIFT			0
#define	HINIC_MSIX_COALESC_TIMER_SHIFT			8
#define	HINIC_MSIX_LLI_TIMER_SHIFT			16
#define	HINIC_MSIX_LLI_CREDIT_SHIFT			24
#define	HINIC_MSIX_RESEND_TIMER_SHIFT			29

#define	HINIC_MSIX_PENDING_LIMIT_MASK			0xFFU
#define	HINIC_MSIX_COALESC_TIMER_MASK			0xFFU
#define	HINIC_MSIX_LLI_TIMER_MASK			0xFFU
#define	HINIC_MSIX_LLI_CREDIT_MASK			0x1FU
#define	HINIC_MSIX_RESEND_TIMER_MASK			0x7U

#define HINIC_MSIX_ATTR_GET(val, member)		\
	     (((val) >> HINIC_MSIX_##member##_SHIFT)	\
	     & HINIC_MSIX_##member##_MASK)

#define HINIC_MSIX_ATTR_SET(val, member)		\
	     (((val) & HINIC_MSIX_##member##_MASK)	\
	     << HINIC_MSIX_##member##_SHIFT)

#define	HINIC_MSIX_CNT_LLI_TIMER_SHIFT			0
#define	HINIC_MSIX_CNT_LLI_CREDIT_SHIFT			8
#define	HINIC_MSIX_CNT_COALESC_TIMER_SHIFT		8
#define	HINIC_MSIX_CNT_PENDING_SHIFT			8
#define	HINIC_MSIX_CNT_RESEND_TIMER_SHIFT		29

#define	HINIC_MSIX_CNT_LLI_TIMER_MASK			0xFFU
#define	HINIC_MSIX_CNT_LLI_CREDIT_MASK			0xFFU
#define	HINIC_MSIX_CNT_COALESC_TIMER_MASK		0xFFU
#define	HINIC_MSIX_CNT_PENDING_MASK			0x1FU
#define	HINIC_MSIX_CNT_RESEND_TIMER_MASK		0x7U

#define HINIC_MSIX_CNT_SET(val, member)		\
		(((val) & HINIC_MSIX_CNT_##member##_MASK) << \
		HINIC_MSIX_CNT_##member##_SHIFT)

int hinic_msix_attr_set(struct hinic_hwif *hwif, u16 msix_index,
			u8 pending_limit, u8 coalesc_timer,
			 u8 lli_timer_cfg, u8 lli_credit_limit,
			 u8 resend_timer);

int hinic_msix_attr_get(struct hinic_hwif *hwif, u16 msix_index,
			u8 *pending_limit, u8 *coalesc_timer_cfg,
			 u8 *lli_timer_cfg, u8 *lli_credit_limit,
			 u8 *resend_timer_cfg);

int hinic_msix_attr_cnt_set(struct hinic_hwif *hwif, u16 msix_index,
			    u8 pending_limit, u8 coalesc_timer,
			     u8 lli_timer_cfg, u8 lli_credit_limit,
			     u8 resend_timer);
#endif
