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
 * Statement:
 * It must include "ossl_knl.h" or "ossl_user.h" before include "ossl_types.h"
 */

#ifndef _OSSL_TYPES_H
#define _OSSL_TYPES_H

#undef NULL
#if defined(__cplusplus)
#define NULL 0
#else
#define NULL ((void *)0)
#endif



#define uda_handle   void *

#define UDA_TRUE  1
#define UDA_FALSE 0

#ifndef UINT8_MAX
#define UINT8_MAX          (u8)(~((u8)0))	/* 0xFF               */
#define UINT16_MAX         (u16)(~((u16)0))	/* 0xFFFF             */
#define UINT32_MAX         (u32)(~((u32)0))	/* 0xFFFFFFFF         */
#define UINT64_MAX         (u64)(~((u64)0))	/* 0xFFFFFFFFFFFFFFFF */
#define ASCII_MAX          (0x7F)
#endif

#endif /* OSSL_TYPES_H */
