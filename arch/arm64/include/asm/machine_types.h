/*
 *  Authors: Wang Yinfeng <wangyinfenng@phytium.com.cn>
 *
 *  Copyright (C) 2021, PHYTIUM Information Technology Co., Ltd.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#ifndef _MACHINE_TYPE_H_
#define _MACHINE_TYPE_H_

#include <asm/cputype.h>
#include <linux/types.h>

static inline bool phytium_part(u32 cpuid)
{
	return ((read_cpuid_id() & MIDR_CPU_MODEL_MASK) == cpuid);
}

#define typeof_ft1500a()	phytium_part(MIDR_FT_1500A)
#define typeof_ft2000ahk()	phytium_part(MIDR_FT_2000AHK)
#define typeof_ft2000plus()	phytium_part(MIDR_FT_2000PLUS)
#define typeof_ft2004()	    phytium_part(MIDR_FT_2004)
#define typeof_s2500()		phytium_part(MIDR_FT_2500)

#endif
