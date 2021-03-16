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

#ifndef OSSL_KNL_H
#define OSSL_KNL_H

#include "ossl_knl_linux.h"

#if defined(__WIN__) || defined(__VMWARE__)
#define __WIN_OR_VMWARE__
#endif

#if defined(__WIN__) || defined(__VMWARE__) || defined(__UEFI__)
#define __WIN_OR_VMWARE_OR_UEFI__
#endif

#if (defined(__WIN__) || defined(__VMWARE__)) && !defined(__HIFC__)
#define __WIN_OR_VMWARE_AND_NONHIFC__
#endif

#if defined(__WIN__) || defined(__UEFI__)
#define __WIN_OR_UEFI__
#endif



#define sdk_err(dev, format, ...)		\
	dev_err(dev, "[COMM]"format, ##__VA_ARGS__)
#define sdk_warn(dev, format, ...)		\
	dev_warn(dev, "[COMM]"format, ##__VA_ARGS__)
#define sdk_notice(dev, format, ...)		\
	dev_notice(dev, "[COMM]"format, ##__VA_ARGS__)
#define sdk_info(dev, format, ...)		\
	dev_info(dev, "[COMM]"format, ##__VA_ARGS__)

#define nic_err(dev, format, ...)		\
	dev_err(dev, "[NIC]"format, ##__VA_ARGS__)
#define nic_warn(dev, format, ...)		\
	dev_warn(dev, "[NIC]"format, ##__VA_ARGS__)
#define nic_notice(dev, format, ...)		\
	dev_notice(dev, "[NIC]"format, ##__VA_ARGS__)
#define nic_info(dev, format, ...)		\
	dev_info(dev, "[NIC]"format, ##__VA_ARGS__)


#endif /* OSSL_KNL_H */
