/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2013, Intel Corporation. */

#ifndef _KCOMPAT_SLES_DEFS_H_
#define _KCOMPAT_SLES_DEFS_H_

/* This is the SUSE Linux Enterprise distribution specific definitions file.
 * It defines what features need backports for a given version of the SUSE
 * Linux Enterprise kernel.
 *
 * It checks a combination of the LINUX_VERSION code and the
 * SLE_LOCALVERSION_CODE to determine what support the kernel has.
 *
 * It assumes that kcompat_std_defs.h has already been processed, and will
 * #define or #undef any flags that have changed based on backports done by
 * SUSE.
 */

#ifndef LINUX_VERSION_CODE
#error "LINUX_VERSION_CODE is undefined"
#endif

#ifndef KERNEL_VERSION
#error "KERNEL_VERSION is undefined"
#endif

#if !SLE_KERNEL_REVISION
#error "SLE_KERNEL_REVISION is 0 or undefined"
#endif

#if SLE_KERNEL_REVISION > 65535
#error "SLE_KERNEL_REVISION is unexpectedly large"
#endif

/* SLE kernel versions are a combination of the LINUX_VERSION_CODE along with
 * an extra digit that indicates the SUSE specific revision of that kernel.
 * This value is found in the CONFIG_LOCALVERSION of the SUSE kernel, which is
 * extracted by common.mk and placed into SLE_KERNEL_REVISION_CODE.
 *
 * We combine the value of SLE_KERNEL_REVISION along with the LINUX_VERSION code
 * to generate the useful value that determines what specific kernel we're
 * dealing with.
 *
 * Just in case the SLE_KERNEL_REVISION ever goes above 255, we reserve 16 bits
 * instead of 8 for this value.
 */
#define SLE_KERNEL_CODE ((LINUX_VERSION_CODE << 16) + SLE_KERNEL_REVISION)
#define SLE_KERNEL_VERSION(a,b,c,d) ((KERNEL_VERSION(a,b,c) << 16) + (d))

/* Unlike RHEL, SUSE kernels are not always tied to a single service pack. For
 * example, 4.12.14 was used as the base for SLE 15 SP1, SLE 12 SP4, and SLE 12
 * SP5.
 *
 * You can find the patches that SUSE applied to the kernel tree at
 * https://github.com/SUSE/kernel-source.
 *
 * You can find the correct kernel version for a check by using steps similar
 * to the following
 *
 * 1) download the kernel-source repo
 * 2) checkout the relevant branch, i.e SLE15-SP3
 * 3) find the relevant backport you're interested in the patches.suse
 *    directory
 * 4) git log <patch file> to locate the commit that introduced the backport
 * 5) git describe --contains to find the relevant tag that includes that
 *    commit, i.e. rpm-5.3.18-37
 * 6) those digits represent the SLE kernel that introduced that backport.
 *
 * Try to keep the checks in SLE_KERNEL_CODE order and condense where
 * possible.
 */

/*****************************************************************************/
#if (SLE_KERNEL_CODE > SLE_KERNEL_VERSION(4,12,14,23) && \
     SLE_KERNEL_CODE < SLE_KERNEL_VERSION(4,12,14,94))
/*
 * 4.12.14 is used as the base for SLE 12 SP4, SLE 12 SP5, SLE 15, and SLE 15
 * SP1. Unfortunately the revision codes do not line up cleanly. SLE 15
 * launched with 4.12.14-23. It appears that SLE 12 SP4 and SLE 15 SP1 both
 * diverged from this point, with SLE 12 SP4 kernels starting around
 * 4.12.14-94. A few backports for SLE 15 SP1 landed in some alpha and beta
 * kernels tagged between 4.12.14-25 up to 4.12.14-32. These changes did not
 * make it into SLE 12 SP4. This was cleaned up with SLE 12 SP5 by an apparent
 * merge in 4.12.14-111. The official launch of SLE 15 SP1 ended up with
 * version 4.12.14-195.
 *
 * Because of this inconsistency and because all of these kernels appear to be
 * alpha or beta kernel releases for SLE 15 SP1, we do not rely on version
 * checks between this range. Issue a warning to indicate that we do not
 * support these.
 */
#warning "SLE kernel versions between 4.12.14-23 and 4.12.14-94 are not supported"
#endif

/*****************************************************************************/
#if (SLE_KERNEL_CODE < SLE_KERNEL_VERSION(4,12,14,100))
#else /* >= 4.12.14-100 */
#undef HAVE_TCF_EXTS_TO_LIST
#define HAVE_TCF_EXTS_FOR_EACH_ACTION
#endif /* 4.12.14-100 */

/*****************************************************************************/
#if (SLE_KERNEL_CODE < SLE_KERNEL_VERSION(4,12,14,111))
#else /* >= 4.12.14-111 */
#define HAVE_DEVLINK_PORT_ATTRS_SET_PORT_FLAVOUR
#undef NEED_MACVLAN_ACCEL_PRIV
#undef NEED_MACVLAN_RELEASE_L2FW_OFFLOAD
#undef NEED_MACVLAN_SUPPORTS_DEST_FILTER
#endif /* 4.12.14-111 */

/*****************************************************************************/
#if (SLE_KERNEL_CODE < SLE_KERNEL_VERSION(4,12,14,120))
#else /* >= 4.12.14-120 */
#define HAVE_NDO_SELECT_QUEUE_SB_DEV
#define HAVE_TCF_MIRRED_DEV
#define HAVE_TCF_BLOCK
#define HAVE_TC_CB_AND_SETUP_QDISC_MQPRIO
#define HAVE_TCF_BLOCK_CB_REGISTER_EXTACK
#undef NEED_TC_SETUP_QDISC_MQPRIO
#undef NEED_TC_CLS_CAN_OFFLOAD_AND_CHAIN0
#endif /* 4.12.14-120 */

/*****************************************************************************/
#if (SLE_KERNEL_CODE < SLE_KERNEL_VERSION(5,3,8,2))
#else /* >= 5.3.8-2 */
#undef NEED_FLOW_INDR_BLOCK_CB_REGISTER
#undef NEED_SKB_FRAG_OFF_ACCESSORS
#endif /* 5.3.8-2 */

/*****************************************************************************/
#if (SLE_KERNEL_CODE < SLE_KERNEL_VERSION(5,3,18,34))
#else /* >= 5.3.18-34 */
#undef NEED_DEVLINK_REGION_CREATE_OPS
#undef NEED_DEVLINK_PORT_ATTRS_SET_STRUCT
#endif /* 5.3.18-34 */

/*****************************************************************************/
#if (SLE_KERNEL_CODE < SLE_KERNEL_VERSION(5,3,18,37))
#else /* >= 5.3.18-37 */
#undef NEED_NET_PREFETCH
#endif /* 5.3.18-37 */

/*****************************************************************************/
#if (SLE_KERNEL_CODE < SLE_KERNEL_VERSION(5,3,18,38))
#else /* >= 5.3.18-38 */
#undef NEED_DEVLINK_FLASH_UPDATE_TIMEOUT_NOTIFY
#endif /* 5.3.18-38 */

#endif /* _KCOMPAT_SLES_DEFS_H_ */
