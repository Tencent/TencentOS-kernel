/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2021, Intel Corporation. */

#ifndef _KCOMPAT_UBUNTU_DEFS_H_
#define _KCOMPAT_UBUNTU_DEFS_H_

/* This file contains the definitions for the Ubuntu specific distribution of
 * the Linux kernel.
 *
 * It checks the UBUNTU_VERSION_CODE to decide which features are available in
 * the target kernel. It assumes that kcompat_std_defs.h has already been
 * processed, and will #define or #undef the relevant flags based on what
 * features were backported by Ubuntu.
 */

#if !UTS_UBUNTU_RELEASE_ABI
#error "UTS_UBUNTU_RELEASE_ABI is 0 or undefined"
#endif

#if !UBUNTU_VERSION_CODE
#error "UBUNTU_VERSION_CODE is 0 or undefined"
#endif

#ifndef UBUNTU_VERSION
#error "UBUNTU_VERSION is undefined"
#endif

#endif /* _KCOMPAT_UBUNTU_DEFS_H_ */
