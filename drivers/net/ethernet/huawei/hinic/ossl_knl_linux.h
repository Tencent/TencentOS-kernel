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

#ifndef OSSL_KNL_LINUX_H_
#define OSSL_KNL_LINUX_H_

#include <linux/string.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/ethtool.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <net/checksum.h>
#include <net/ipv6.h>
#include <linux/if_vlan.h>
#include <linux/udp.h>
#include <linux/highmem.h>

/* UTS_RELEASE is in a different header starting in kernel 2.6.18 */
#ifndef UTS_RELEASE
/* utsrelease.h changed locations in 2.6.33 */
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33) )
#include <linux/utsrelease.h>
#else
#include <generated/utsrelease.h>
#endif
#endif

#ifndef NETIF_F_SCTP_CSUM
#define NETIF_F_SCTP_CSUM 0
#endif

#ifndef __GFP_COLD
#define __GFP_COLD 0
#endif

#ifndef __GFP_COMP
#define __GFP_COMP 0
#endif

#ifndef SUPPORTED_100000baseKR4_Full
#define SUPPORTED_100000baseKR4_Full	0
#define ADVERTISED_100000baseKR4_Full	0
#endif
#ifndef SUPPORTED_100000baseCR4_Full
#define SUPPORTED_100000baseCR4_Full	0
#define ADVERTISED_100000baseCR4_Full	0
#endif

#ifndef SUPPORTED_40000baseKR4_Full
#define SUPPORTED_40000baseKR4_Full	0
#define ADVERTISED_40000baseKR4_Full	0
#endif
#ifndef SUPPORTED_40000baseCR4_Full
#define SUPPORTED_40000baseCR4_Full	0
#define ADVERTISED_40000baseCR4_Full	0
#endif

#ifndef SUPPORTED_25000baseKR_Full
#define	SUPPORTED_25000baseKR_Full	0
#define ADVERTISED_25000baseKR_Full	0
#endif
#ifndef SUPPORTED_25000baseCR_Full
#define SUPPORTED_25000baseCR_Full	0
#define	ADVERTISED_25000baseCR_Full	0
#endif

#ifndef ETHTOOL_GLINKSETTINGS
enum ethtool_link_mode_bit_indices {
	ETHTOOL_LINK_MODE_1000baseKX_Full_BIT = 17,
	ETHTOOL_LINK_MODE_10000baseKR_Full_BIT = 19,
	ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT = 23,
	ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT = 24,
	ETHTOOL_LINK_MODE_25000baseCR_Full_BIT = 31,
	ETHTOOL_LINK_MODE_25000baseKR_Full_BIT = 32,
	ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT = 36,
	ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT = 38,
};
#endif

#ifndef RHEL_RELEASE_VERSION
#define RHEL_RELEASE_VERSION(a, b) (((a) << 8) + (b))
#endif
#ifndef AX_RELEASE_VERSION
#define AX_RELEASE_VERSION(a, b) (((a) << 8) + (b))
#endif

#ifndef AX_RELEASE_CODE
#define AX_RELEASE_CODE 0
#endif

#if (AX_RELEASE_CODE && AX_RELEASE_CODE == AX_RELEASE_VERSION(3, 0))
#define RHEL_RELEASE_CODE RHEL_RELEASE_VERSION(5, 0)
#elif (AX_RELEASE_CODE && AX_RELEASE_CODE == AX_RELEASE_VERSION(3, 1))
#define RHEL_RELEASE_CODE RHEL_RELEASE_VERSION(5, 1)
#elif (AX_RELEASE_CODE && AX_RELEASE_CODE == AX_RELEASE_VERSION(3, 2))
#define RHEL_RELEASE_CODE RHEL_RELEASE_VERSION(5, 3)
#endif

#ifndef RHEL_RELEASE_CODE
/* NOTE: RHEL_RELEASE_* introduced in RHEL4.5. */
#define RHEL_RELEASE_CODE 0
#endif

/* RHEL 7 didn't backport the parameter change in
 * create_singlethread_workqueue.
 * If/when RH corrects this we will want to tighten up the version check.
 */
#if (RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 0))
#undef create_singlethread_workqueue
#define create_singlethread_workqueue(name)	\
	alloc_ordered_workqueue("%s", WQ_MEM_RECLAIM, name)
#endif

/* Ubuntu Release ABI is the 4th digit of their kernel version. You can find
 * it in /usr/src/linux/$(uname -r)/include/generated/utsrelease.h for new
 * enough versions of Ubuntu. Otherwise you can simply see it in the output of
 * uname as the 4th digit of the kernel. The UTS_UBUNTU_RELEASE_ABI is not in
 * the linux-source package, but in the linux-headers package. It begins to
 * appear in later releases of 14.04 and 14.10.
 *
 * Ex:
 * <Ubuntu 14.04.1>
 *  $uname -r
 *  3.13.0-45-generic
 * ABI is 45
 *
 * <Ubuntu 14.10>
 *  $uname -r
 *  3.16.0-23-generic
 * ABI is 23.
 */
#ifndef UTS_UBUNTU_RELEASE_ABI
#define UTS_UBUNTU_RELEASE_ABI 0
#define UBUNTU_VERSION_CODE 0
#else

#if UTS_UBUNTU_RELEASE_ABI > 255
#error UTS_UBUNTU_RELEASE_ABI is too large...
#endif /* UTS_UBUNTU_RELEASE_ABI > 255 */

#if (KERNEL_VERSION(3, 0, 0) >= LINUX_VERSION_CODE)
/* Our version code scheme does not make sense for non 3.x or newer kernels,
 * and we have no support in kcompat for this scenario. Thus, treat this as a
 * non-Ubuntu kernel. Possibly might be better to error here.
 */
#define UTS_UBUNTU_RELEASE_ABI 0
#define UBUNTU_VERSION_CODE 0
#endif
#endif

/* Note that the 3rd digit is always zero, and will be ignored. This is
 * because Ubuntu kernels are based on x.y.0-ABI values, and while their linux
 * version codes are 3 digit, this 3rd digit is superseded by the ABI value.
 */
#define UBUNTU_VERSION(a, b, c, d) ((KERNEL_VERSION(a, b, 0) << 8) + (d))

#ifndef DEEPIN_PRODUCT_VERSION
#define DEEPIN_PRODUCT_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#endif

#ifdef CONFIG_DEEPIN_KERNEL
#if (KERNEL_VERSION(4, 4, 102) == LINUX_VERSION_CODE)
#define DEEPIN_VERSION_CODE DEEPIN_PRODUCT_VERSION(15, 2, 0)
#endif
#endif

#ifndef DEEPIN_VERSION_CODE
#define DEEPIN_VERSION_CODE 0
#endif

/* SuSE version macros are the same as Linux kernel version macro. */
#ifndef SLE_VERSION
#define SLE_VERSION(a, b, c)	KERNEL_VERSION(a, b, c)
#endif
#define SLE_LOCALVERSION(a, b, c)	KERNEL_VERSION(a, b, c)
#ifdef CONFIG_SUSE_KERNEL
#if (KERNEL_VERSION(2, 6, 27) == LINUX_VERSION_CODE)
/* SLES11 GA is 2.6.27 based. */
#define SLE_VERSION_CODE SLE_VERSION(11, 0, 0)
#elif (KERNEL_VERSION(2, 6, 32) == LINUX_VERSION_CODE)
/* SLES11 SP1 is 2.6.32 based. */
#define SLE_VERSION_CODE SLE_VERSION(11, 1, 0)
#elif (KERNEL_VERSION(3, 0, 13) == LINUX_VERSION_CODE)
/* SLES11 SP2 GA is 3.0.13-0.27. */
#define SLE_VERSION_CODE SLE_VERSION(11, 2, 0)
#elif (KERNEL_VERSION(3, 0, 76) == LINUX_VERSION_CODE)
/* SLES11 SP3 GA is 3.0.76-0.11. */
#define SLE_VERSION_CODE SLE_VERSION(11, 3, 0)
#elif (KERNEL_VERSION(3, 0, 101) == LINUX_VERSION_CODE)
/* SLES11 SP4 GA (3.0.101-63) and update kernels 3.0.101-63+ */
#define SLE_VERSION_CODE SLE_VERSION(11, 4, 0)
#elif (KERNEL_VERSION(3, 12, 28) == LINUX_VERSION_CODE)
/*
 * SLES12 GA is 3.12.28-4
 * kernel updates 3.12.xx-<33 through 52>[.yy].
 */
#define SLE_VERSION_CODE SLE_VERSION(12, 0, 0)
#elif (KERNEL_VERSION(3, 12, 49) == LINUX_VERSION_CODE)
/*
 * SLES12 SP1 GA is 3.12.49-11
 * updates 3.12.xx-60.yy where xx={51..}
 */
#define SLE_VERSION_CODE SLE_VERSION(12, 1, 0)
#elif (LINUX_VERSION_CODE == KERNEL_VERSION(4, 4, 21))
/*
 * SLES12 SP2 GA is 4.4.21-69.
 * SLES12 SP2 updates before SLES12 SP3 are: 4.4.{21,38,49,59}
 * SLES12 SP2 updates after SLES12 SP3 are: 4.4.{74,90,103,114,120}
 * but they all use a SLE_LOCALVERSION_CODE matching 92.nn.y
 */
#define SLE_VERSION_CODE SLE_VERSION(12, 2, 0)
#elif (LINUX_VERSION_CODE == KERNEL_VERSION(4, 4, 73))
/* SLES12 SP3 GM is 4.4.73-5 and update kernels are 4.4.82-6.3.
 * SLES12 SP3 updates not conflicting with SP2 are: 4.4.{82,92}
 * SLES12 SP3 updates conflicting with SP2 are:
 *   - 4.4.103-6.33.1, 4.4.103-6.38.1
 *   - 4.4.{114,120}-94.nn.y */
#define SLE_VERSION_CODE SLE_VERSION(12, 3, 0)
#elif (KERNEL_VERSION(4, 12, 14) <= LINUX_VERSION_CODE)
/* SLES15 Beta1 is 4.12.14-2.
 * SLES12 SP4 will also use 4.12.14-nn.xx.y */
#include <linux/suse_version.h>
/*
 * new SLES kernels must be added here with >= based on kernel
 * the idea is to order from newest to oldest and just catch all
 * of them using the >=
 */
#endif /* LINUX_VERSION_CODE == KERNEL VERSION(x,y,z) */
#endif /* CONFIG_SUSE_KERNEL */
#ifndef SLE_VERSION_CODE
#define SLE_VERSION_CODE 0
#endif /* SLE_VERSION_CODE */
#ifndef SUSE_PRODUCT_CODE
#define SUSE_PRODUCT_CODE 0
#endif /* SUSE_PRODUCT_CODE */
#ifndef SUSE_PRODUCT
#define SUSE_PRODUCT(product, version, patchlevel, auxrelease) \
	(((product) << 24) + ((version) << 16) + \
	((patchlevel) << 8) + (auxrelease))
#endif /* SUSE_PRODUCT */

#ifndef ALIGN_DOWN
#ifndef __ALIGN_KERNEL
#define __ALIGN_KERNEL(x, a)	__ALIGN_MASK(x, (typeof(x))(a) - 1)
#endif
#define ALIGN_DOWN(x, a)	__ALIGN_KERNEL((x) - ((a) - 1), (a))
#endif

/*****************************************************************************/
#if (KERNEL_VERSION(2, 6, 22) > LINUX_VERSION_CODE)
#define tcp_hdr(skb) ((skb)->h.th)
#define tcp_hdrlen(skb) ((skb)->h.th->doff << 2)
#define skb_transport_offset(skb) ((skb)->h.raw - (skb)->data)
#define skb_transport_header(skb) ((skb)->h.raw)
#define ipv6_hdr(skb) ((skb)->nh.ipv6h)
#define ip_hdr(skb) ((skb)->nh.iph)
#define skb_network_offset(skb) ((skb)->nh.raw - (skb)->data)
#define skb_network_header(skb) ((skb)->nh.raw)
#define skb_tail_pointer(skb) ((skb)->tail)
#define skb_reset_tail_pointer(skb) \
	do { \
		skb->tail = skb->data; \
	} while (0)
#define skb_set_tail_pointer(skb, offset) \
	do { \
		skb->tail = skb->data + offset; \
	} while (0)
#define skb_copy_to_linear_data(skb, from, len) \
				memcpy(skb->data, from, len)
#define skb_copy_to_linear_data_offset(skb, offset, from, len) \
				memcpy(skb->data + offset, from, len)
#define skb_network_header_len(skb) ((skb)->h.raw - (skb)->nh.raw)
#define pci_register_driver pci_module_init
#define skb_mac_header(skb) ((skb)->mac.raw)

#ifdef NETIF_F_MULTI_QUEUE
#ifndef alloc_etherdev_mq
#define alloc_etherdev_mq(_a, _b) alloc_etherdev(_a)
#endif
#endif /* NETIF_F_MULTI_QUEUE */

#ifndef ETH_FCS_LEN
#define ETH_FCS_LEN 4
#endif
#define cancel_work_sync(x) flush_scheduled_work()
#ifndef udp_hdr
#define udp_hdr _udp_hdr
static inline struct udphdr *_udp_hdr(const struct sk_buff *skb)
{
	return (struct udphdr *)skb_transport_header(skb);
}
#endif

#ifdef cpu_to_be16
#undef cpu_to_be16
#endif
#define cpu_to_be16(x) __constant_htons(x)

#if (!(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(5, 1)))
enum {
	DUMP_PREFIX_NONE,
	DUMP_PREFIX_ADDRESS,
	DUMP_PREFIX_OFFSET,
};
#endif /* !(RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(5,1)) */
#ifndef hex_asc
#define hex_asc(x)	"0123456789abcdef"[x]
#endif
#ifndef ADVERTISED_2500baseX_Full
#define ADVERTISED_2500baseX_Full BIT(15)
#endif
#ifndef SUPPORTED_2500baseX_Full
#define SUPPORTED_2500baseX_Full BIT(15)
#endif

#ifndef ETH_P_PAUSE
#define ETH_P_PAUSE 0x8808
#endif

static inline int compound_order(struct page *page)
{
	return 0;
}

#ifndef SKB_WITH_OVERHEAD
#define SKB_WITH_OVERHEAD(X) \
	((X) - SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
#endif
#else /* 2.6.22 */
#define ETH_TYPE_TRANS_SETS_DEV
#define HAVE_NETDEV_STATS_IN_NETDEV
#endif /* < 2.6.22 */

/*****************************************************************************/
#if (KERNEL_VERSION(2, 6, 32) > LINUX_VERSION_CODE)
#undef netdev_tx_t
#define netdev_tx_t int
#else /* < 2.6.32 */

#if (RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_VERSION(6, 2) <= RHEL_RELEASE_CODE) && \
	(RHEL_RELEASE_VERSION(7, 0) > RHEL_RELEASE_CODE))
#define HAVE_RHEL6_NET_DEVICE_EXTENDED
#endif /* RHEL >= 6.2 && RHEL < 7.0 */
#if (RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_VERSION(6, 6) <= RHEL_RELEASE_CODE) && \
	(RHEL_RELEASE_VERSION(7, 0) > RHEL_RELEASE_CODE))
#define HAVE_RHEL6_NET_DEVICE_OPS_EXT
#define HAVE_NDO_SET_FEATURES
#endif /* RHEL >= 6.6 && RHEL < 7.0 */
#endif /* < 2.6.32 */

/*****************************************************************************/
#if (KERNEL_VERSION(2, 6, 33) > LINUX_VERSION_CODE)
#ifndef IPV4_FLOW
#define IPV4_FLOW 0x10
#endif /* IPV4_FLOW */
#ifndef IPV6_FLOW
#define IPV6_FLOW 0x11
#endif /* IPV6_FLOW */

#ifndef __percpu
#define __percpu
#endif /* __percpu */

#ifndef PORT_DA
#define PORT_DA PORT_OTHER
#endif /* PORT_DA */
#ifndef PORT_NONE
#define PORT_NONE PORT_OTHER
#endif

#if ((RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_VERSION(6, 3) <= RHEL_RELEASE_CODE) && \
	(RHEL_RELEASE_VERSION(7, 0) > RHEL_RELEASE_CODE)))
#if !defined(CONFIG_X86_32) && !defined(CONFIG_NEED_DMA_MAP_STATE)
#undef DEFINE_DMA_UNMAP_ADDR
#define DEFINE_DMA_UNMAP_ADDR(ADDR_NAME)	dma_addr_t ADDR_NAME
#undef DEFINE_DMA_UNMAP_LEN
#define DEFINE_DMA_UNMAP_LEN(LEN_NAME)		__u32 LEN_NAME
#undef dma_unmap_addr
#define dma_unmap_addr(PTR, ADDR_NAME)		((PTR)->ADDR_NAME)
#undef dma_unmap_addr_set
#define dma_unmap_addr_set(PTR, ADDR_NAME, VAL)	(((PTR)->ADDR_NAME) = (VAL))
#undef dma_unmap_len
#define dma_unmap_len(PTR, LEN_NAME)		((PTR)->LEN_NAME)
#undef dma_unmap_len_set
#define dma_unmap_len_set(PTR, LEN_NAME, VAL)	(((PTR)->LEN_NAME) = (VAL))
#endif /* CONFIG_X86_64 && !CONFIG_NEED_DMA_MAP_STATE */
#endif /* RHEL_RELEASE_CODE */

#if (!(RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_VERSION(6, 2) <= RHEL_RELEASE_CODE)))
#define sk_tx_queue_get(_sk) (-1)
#define sk_tx_queue_set(_sk, _tx_queue) do {} while (0)
#endif /* !(RHEL >= 6.2) */

#if (RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_VERSION(6, 4) <= RHEL_RELEASE_CODE) && \
	(RHEL_RELEASE_VERSION(7, 0) > RHEL_RELEASE_CODE))
#define HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT
#define HAVE_ETHTOOL_GRXFHINDIR_SIZE
#define HAVE_ETHTOOL_SET_PHYS_ID
#define HAVE_ETHTOOL_GET_TS_INFO
#if (RHEL_RELEASE_VERSION(6, 5) < RHEL_RELEASE_CODE)
#define HAVE_ETHTOOL_GSRSSH
#define HAVE_RHEL6_SRIOV_CONFIGURE
#define HAVE_RXFH_NONCONST
#endif /* RHEL > 6.5 */
#endif /* RHEL >= 6.4 && RHEL < 7.0 */

#endif /* < 2.6.33 */

/*****************************************************************************/
#if (KERNEL_VERSION(2, 6, 34) > LINUX_VERSION_CODE)
#if (RHEL_RELEASE_VERSION(6, 0) > RHEL_RELEASE_CODE)
#ifndef pci_num_vf
#define pci_num_vf(pdev) _kc_pci_num_vf(pdev)
extern int _kc_pci_num_vf(struct pci_dev *dev);
#endif
#endif /* RHEL_RELEASE_CODE */

#ifndef ETH_FLAG_NTUPLE
#define ETH_FLAG_NTUPLE NETIF_F_NTUPLE
#endif

#ifndef netdev_mc_count
#define netdev_mc_count(dev) ((dev)->mc_count)
#endif
#ifndef netdev_mc_empty
#define netdev_mc_empty(dev) (netdev_mc_count(dev) == 0)
#endif
#ifndef netdev_for_each_mc_addr
#define netdev_for_each_mc_addr(mclist, dev) \
	for (mclist = dev->mc_list; mclist; mclist = mclist->next)
#endif
#ifndef netdev_uc_count
#define netdev_uc_count(dev) ((dev)->uc.count)
#endif
#ifndef netdev_uc_empty
#define netdev_uc_empty(dev) (netdev_uc_count(dev) == 0)
#endif
#ifndef netdev_for_each_uc_addr
#define netdev_for_each_uc_addr(ha, dev) \
	list_for_each_entry(ha, &dev->uc.list, list)
#endif
#ifndef dma_set_coherent_mask
#define dma_set_coherent_mask(dev, mask) \
	pci_set_consistent_dma_mask(to_pci_dev(dev), (mask))
#endif

/* netdev logging taken from include/linux/netdevice.h */
#ifndef netdev_name
static inline const char *_kc_netdev_name(const struct net_device *dev)
{
	if (dev->reg_state != NETREG_REGISTERED)
		return "(unregistered net_device)";
	return dev->name;
}

#define netdev_name(netdev)	_kc_netdev_name(netdev)
#endif /* netdev_name */

#undef netdev_printk
#if (KERNEL_VERSION(2, 6, 0) > LINUX_VERSION_CODE)
#define netdev_printk(level, netdev, format, args...)		\
do {								\
	struct pci_dev *pdev = _kc_netdev_to_pdev(netdev);	\
	printk(level "%s: " format, pci_name(pdev), ##args);	\
} while (0)
#elif (KERNEL_VERSION(2, 6, 21) > LINUX_VERSION_CODE)
#define netdev_printk(level, netdev, format, args...)		\
do {								\
	struct pci_dev *pdev = _kc_netdev_to_pdev(netdev);	\
	struct device *dev = pci_dev_to_dev(pdev);		\
	dev_printk(level, dev, "%s: " format,			\
		   netdev_name(netdev), ##args);		\
} while (0)
#else /* 2.6.21 => 2.6.34 */
#define netdev_printk(level, netdev, format, args...)		\
	dev_printk(level, (netdev)->dev.parent,			\
		   "%s: " format,				\
		   netdev_name(netdev), ##args)
#endif /* <2.6.0 <2.6.21 <2.6.34 */
#undef netdev_emerg
#define netdev_emerg(dev, format, args...)			\
	netdev_printk(KERN_EMERG, dev, format, ##args)
#undef netdev_alert
#define netdev_alert(dev, format, args...)			\
	netdev_printk(KERN_ALERT, dev, format, ##args)
#undef netdev_crit
#define netdev_crit(dev, format, args...)			\
	netdev_printk(KERN_CRIT, dev, format, ##args)
#undef netdev_err
#define netdev_err(dev, format, args...)			\
	netdev_printk(KERN_ERR, dev, format, ##args)
#undef netdev_warn
#define netdev_warn(dev, format, args...)			\
	netdev_printk(KERN_WARNING, dev, format, ##args)
#undef netdev_notice
#define netdev_notice(dev, format, args...)			\
	netdev_printk(KERN_NOTICE, dev, format, ##args)
#undef netdev_info
#define netdev_info(dev, format, args...)			\
	netdev_printk(KERN_INFO, dev, format, ##args)
#undef netdev_dbg
#if defined(DEBUG)
#define netdev_dbg(__dev, format, args...)			\
	netdev_printk(KERN_DEBUG, __dev, format, ##args)
#elif defined(CONFIG_DYNAMIC_DEBUG)
#define netdev_dbg(__dev, format, args...)			\
do {								\
	dynamic_dev_dbg((__dev)->dev.parent, "%s: " format,	\
			netdev_name(__dev), ##args);		\
} while (0)
#else /* DEBUG */
#define netdev_dbg(__dev, format, args...)			\
({								\
	if (0)							\
		netdev_printk(KERN_DEBUG, __dev, format, ##args); \
	0;							\
})
#endif /* DEBUG */

#undef netif_printk
#define netif_printk(priv, type, level, dev, fmt, args...)	\
do {								\
	if (netif_msg_##type(priv))				\
		netdev_printk(level, (dev), fmt, ##args);	\
} while (0)

#undef netif_emerg
#define netif_emerg(priv, type, dev, fmt, args...)		\
	netif_level(emerg, priv, type, dev, fmt, ##args)
#undef netif_alert
#define netif_alert(priv, type, dev, fmt, args...)		\
	netif_level(alert, priv, type, dev, fmt, ##args)
#undef netif_crit
#define netif_crit(priv, type, dev, fmt, args...)		\
	netif_level(crit, priv, type, dev, fmt, ##args)
#undef netif_err
#define netif_err(priv, type, dev, fmt, args...)		\
	netif_level(err, priv, type, dev, fmt, ##args)
#undef netif_warn
#define netif_warn(priv, type, dev, fmt, args...)		\
	netif_level(warn, priv, type, dev, fmt, ##args)
#undef netif_notice
#define netif_notice(priv, type, dev, fmt, args...)		\
	netif_level(notice, priv, type, dev, fmt, ##args)
#undef netif_info
#define netif_info(priv, type, dev, fmt, args...)		\
	netif_level(info, priv, type, dev, fmt, ##args)
#undef netif_dbg
#define netif_dbg(priv, type, dev, fmt, args...)		\
	netif_level(dbg, priv, type, dev, fmt, ##args)

#ifndef for_each_set_bit
#define for_each_set_bit(bit, addr, size) \
	for ((bit) = find_first_bit((addr), (size)); \
		(bit) < (size); \
		(bit) = find_next_bit((addr), (size), (bit) + 1))
#endif /* for_each_set_bit */

#ifndef DEFINE_DMA_UNMAP_ADDR
#define DEFINE_DMA_UNMAP_ADDR DECLARE_PCI_UNMAP_ADDR
#define DEFINE_DMA_UNMAP_LEN DECLARE_PCI_UNMAP_LEN
#define dma_unmap_addr pci_unmap_addr
#define dma_unmap_addr_set pci_unmap_addr_set
#define dma_unmap_len pci_unmap_len
#define dma_unmap_len_set pci_unmap_len_set
#endif /* DEFINE_DMA_UNMAP_ADDR */

#ifndef pci_bus_speed
/* override pci_bus_speed introduced in 2.6.19 with an expanded enum type */
enum _kc_pci_bus_speed {
	_KC_PCIE_SPEED_2_5GT		= 0x14,
	_KC_PCIE_SPEED_5_0GT		= 0x15,
	_KC_PCIE_SPEED_8_0GT		= 0x16,
	_KC_PCI_SPEED_UNKNOWN		= 0xff,
};

#define pci_bus_speed		_kc_pci_bus_speed
#define PCIE_SPEED_2_5GT	_KC_PCIE_SPEED_2_5GT
#define PCIE_SPEED_5_0GT	_KC_PCIE_SPEED_5_0GT
#define PCIE_SPEED_8_0GT	_KC_PCIE_SPEED_8_0GT
#define PCI_SPEED_UNKNOWN	_KC_PCI_SPEED_UNKNOWN
#endif /* pci_bus_speed */

#else /* < 2.6.34 */
#ifndef HAVE_SET_RX_MODE
#define HAVE_SET_RX_MODE
#endif
#define HAVE_INET6_IFADDR_LIST
#endif /* < 2.6.34 */

/*****************************************************************************/
#if (KERNEL_VERSION(2, 6, 36) > LINUX_VERSION_CODE)
#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
#ifdef NET_IP_ALIGN
#undef NET_IP_ALIGN
#endif
#define NET_IP_ALIGN 0
#endif /* CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS */

#ifdef NET_SKB_PAD
#undef NET_SKB_PAD
#endif

#if (L1_CACHE_BYTES > 32)
#define NET_SKB_PAD L1_CACHE_BYTES
#else
#define NET_SKB_PAD 32
#endif

static inline struct sk_buff *_kc_netdev_alloc_skb_ip_align
	(struct net_device *dev, unsigned int length)
{
	struct sk_buff *skb;

	skb = alloc_skb(length + NET_SKB_PAD + NET_IP_ALIGN, GFP_ATOMIC);
	if (skb) {
#if (NET_IP_ALIGN + NET_SKB_PAD)
		skb_reserve(skb, NET_IP_ALIGN + NET_SKB_PAD);
#endif
		skb->dev = dev;
	}

	return skb;
}

#ifdef netdev_alloc_skb_ip_align
#undef netdev_alloc_skb_ip_align
#endif
#define netdev_alloc_skb_ip_align(n, l) _kc_netdev_alloc_skb_ip_align(n, l)

#undef netif_level
#define netif_level(level, priv, type, dev, fmt, args...)	\
do {								\
	if (netif_msg_##type(priv))				\
		netdev_##level(dev, fmt, ##args);		\
} while (0)

#if (!(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6, 3)))
#undef usleep_range
#define usleep_range(min, max)	msleep(DIV_ROUND_UP(min, 1000))
#endif

#define u64_stats_update_begin(a) do { } while (0)
#define u64_stats_update_end(a) do { } while (0)
#define u64_stats_fetch_retry(a, b) (0)
#define u64_stats_fetch_begin(a) (0)
#define u64_stats_fetch_retry_bh(a, b) (0)
#define u64_stats_fetch_begin_bh(a) (0)
struct u64_stats_sync_empty {
};

#if (RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6, 1))
#define HAVE_8021P_SUPPORT
#endif

/* RHEL6.4 and SLES11sp2 backported skb_tx_timestamp */
/* RHEL6.4 and SLES11sp2 backported skb_tx_timestamp */
#if (!(RHEL_RELEASE_VERSION(6, 4) <= RHEL_RELEASE_CODE) && \
	!(SLE_VERSION(11, 2, 0) <= SLE_VERSION_CODE))
static inline void skb_tx_timestamp(struct sk_buff __always_unused *skb)
{
}
#endif

#else /* < 2.6.36 */

#define HAVE_NDO_GET_STATS64
#endif /* < 2.6.36 */

/*****************************************************************************/
#if (KERNEL_VERSION(2, 6, 39) > LINUX_VERSION_CODE)

#ifndef TC_BITMASK
#define TC_BITMASK 15
#endif

#ifndef NETIF_F_RXCSUM
#define NETIF_F_RXCSUM		BIT(29)
#endif

#ifndef skb_queue_reverse_walk_safe
#define skb_queue_reverse_walk_safe(queue, skb, tmp)			\
		for (skb = (queue)->prev, tmp = skb->prev;		\
		     skb != (struct sk_buff *)(queue);			\
		     skb = tmp, tmp = skb->prev)
#endif

#if (!(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6, 4)))
#define kstrtoul(a, b, c)  ((*(c)) = simple_strtoul((a), NULL, (b)), 0)
#define kstrtouint(a, b, c)  ((*(c)) = simple_strtoul((a), NULL, (b)), 0)
#define kstrtou32(a, b, c)  ((*(c)) = simple_strtoul((a), NULL, (b)), 0)
#endif /* !(RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6,4)) */
#if (!(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(6, 0)))
#define netdev_set_tc_queue(dev, tc, cnt, off) do {} while (0)
#define netdev_set_prio_tc_map(dev, up, tc) do {} while (0)
#else /* RHEL6.1 or greater */
#ifndef HAVE_MQPRIO
#define HAVE_MQPRIO
#endif /* HAVE_MQPRIO */

#endif /* !(RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(6,0)) */

#ifndef udp_csum
#define udp_csum __kc_udp_csum
static inline __wsum __kc_udp_csum(struct sk_buff *skb)
{
	__wsum csum = csum_partial(skb_transport_header(skb),
				   sizeof(struct udphdr), skb->csum);

	for (skb = skb_shinfo(skb)->frag_list; skb; skb = skb->next)
		csum = csum_add(csum, skb->csum);

	return csum;
}
#endif /* udp_csum */

#else /* < 2.6.39 */

#ifndef HAVE_MQPRIO
#define HAVE_MQPRIO
#endif
#ifndef HAVE_SETUP_TC
#define HAVE_SETUP_TC
#endif

#ifndef HAVE_NDO_SET_FEATURES
#define HAVE_NDO_SET_FEATURES
#endif
#define HAVE_IRQ_AFFINITY_NOTIFY
#endif /* < 2.6.39 */

/*****************************************************************************/
#if (KERNEL_VERSION(2, 6, 40) > LINUX_VERSION_CODE)

#else
#define HAVE_ETHTOOL_SET_PHYS_ID
#endif /* < 2.6.40 */

/*****************************************************************************/
#if (KERNEL_VERSION(3, 0, 0) > LINUX_VERSION_CODE)

#else
#define HAVE_NETDEV_WANTED_FEAUTES
#endif

/*****************************************************************************/
#if (KERNEL_VERSION(3, 2, 0) > LINUX_VERSION_CODE)
#ifndef dma_zalloc_coherent
#define dma_zalloc_coherent(d, s, h, f) _kc_dma_zalloc_coherent(d, s, h, f)
static inline void *_kc_dma_zalloc_coherent(struct device *dev, size_t size,
					    dma_addr_t *dma_handle, gfp_t flag)
{
	void *ret = dma_alloc_coherent(dev, size, dma_handle, flag);

	if (ret)
		memset(ret, 0, size);

	return ret;
}
#endif

#ifndef skb_frag_size
#define skb_frag_size(frag)	_kc_skb_frag_size(frag)
static inline unsigned int _kc_skb_frag_size(const skb_frag_t *frag)
{
	return frag->size;
}
#endif /* skb_frag_size */

#ifndef skb_frag_size_sub
#define skb_frag_size_sub(frag, delta)	_kc_skb_frag_size_sub(frag, delta)
static inline void _kc_skb_frag_size_sub(skb_frag_t *frag, int delta)
{
	frag->size -= delta;
}
#endif /* skb_frag_size_sub */

#ifndef skb_frag_page
#define skb_frag_page(frag)	_kc_skb_frag_page(frag)
static inline struct page *_kc_skb_frag_page(const skb_frag_t *frag)
{
	return frag->page;
}
#endif /* skb_frag_page */

#ifndef skb_frag_address
#define skb_frag_address(frag)	_kc_skb_frag_address(frag)
static inline void *_kc_skb_frag_address(const skb_frag_t *frag)
{
	return page_address(skb_frag_page(frag)) + frag->page_offset;
}
#endif /* skb_frag_address */

#ifndef skb_frag_dma_map
#if (KERNEL_VERSION(2, 6, 0) <= LINUX_VERSION_CODE)
#include <linux/dma-mapping.h>
#endif
#define skb_frag_dma_map(dev, frag, offset, size, dir) \
		_kc_skb_frag_dma_map(dev, frag, offset, size, dir)
static inline dma_addr_t _kc_skb_frag_dma_map(struct device *dev,
					      const skb_frag_t *frag,
					      size_t offset, size_t size,
					      enum dma_data_direction dir)
{
	return dma_map_page(dev, skb_frag_page(frag),
			    frag->page_offset + offset, size, dir);
}
#endif /* skb_frag_dma_map */

#ifndef __skb_frag_unref
#define __skb_frag_unref(frag) __kc_skb_frag_unref(frag)
static inline void __kc_skb_frag_unref(skb_frag_t *frag)
{
	put_page(skb_frag_page(frag));
}
#endif /* __skb_frag_unref */

#ifndef SPEED_UNKNOWN
#define SPEED_UNKNOWN	-1
#endif
#ifndef DUPLEX_UNKNOWN
#define DUPLEX_UNKNOWN	0xff
#endif
#if ((RHEL_RELEASE_VERSION(6, 3) <= RHEL_RELEASE_CODE) ||\
	(SLE_VERSION_CODE && SLE_VERSION(11, 3, 0) <= SLE_VERSION_CODE))
#ifndef HAVE_PCI_DEV_FLAGS_ASSIGNED
#define HAVE_PCI_DEV_FLAGS_ASSIGNED
#endif
#endif
#else /* < 3.2.0 */
#ifndef HAVE_PCI_DEV_FLAGS_ASSIGNED
#define HAVE_PCI_DEV_FLAGS_ASSIGNED
#define HAVE_VF_SPOOFCHK_CONFIGURE
#endif
#ifndef HAVE_SKB_L4_RXHASH
#define HAVE_SKB_L4_RXHASH
#endif
#endif /* < 3.2.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(3, 3, 0) > LINUX_VERSION_CODE)
#if (SLE_VERSION_CODE && (SLE_VERSION(11, 2, 0) >= SLE_VERSION_CODE))
#define NOT_HAVE_GET_RXFH_INDIR_SIZE
#endif

/*
 * NOTE: the order of parameters to _kc_alloc_workqueue() is different than
 * alloc_workqueue() to avoid compiler warning from -Wvarargs.
 */
static inline struct workqueue_struct *__attribute__ ((format(printf, 3, 4)))
_kc_alloc_workqueue(__maybe_unused int flags, __maybe_unused int max_active,
		    const char *fmt, ...)
{
	struct workqueue_struct *wq;
	va_list args, temp;
	unsigned int len;
	char *p;

	va_start(args, fmt);
	va_copy(temp, args);
	len = vsnprintf(NULL, 0, fmt, temp);
	va_end(temp);

	p = kmalloc(len + 1, GFP_KERNEL);
	if (!p) {
		va_end(args);
		return NULL;
	}

	vsnprintf(p, len + 1, fmt, args);
	va_end(args);
#if (KERNEL_VERSION(2, 6, 36) > LINUX_VERSION_CODE)
	wq = create_workqueue(p);
#else
	wq = alloc_workqueue(p, flags, max_active);
#endif
	kfree(p);

	return wq;
}

#ifdef alloc_workqueue
#undef alloc_workqueue
#endif
#define alloc_workqueue(fmt, flags, max_active, args...) \
	_kc_alloc_workqueue(flags, max_active, fmt, ##args)

#if !(RHEL_RELEASE_VERSION(6, 5) <= RHEL_RELEASE_CODE)
typedef u32 netdev_features_t;
#endif
#undef PCI_EXP_TYPE_RC_EC
#define  PCI_EXP_TYPE_RC_EC	0xa	/* Root Complex Event Collector */
#ifndef CONFIG_BQL
#define netdev_tx_completed_queue(_q, _p, _b) do {} while (0)
#define netdev_completed_queue(_n, _p, _b) do {} while (0)
#define netdev_tx_sent_queue(_q, _b) do {} while (0)
#define netdev_sent_queue(_n, _b) do {} while (0)
#define netdev_tx_reset_queue(_q) do {} while (0)
#define netdev_reset_queue(_n) do {} while (0)
#endif
#if (SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(11, 3, 0))
#define HAVE_ETHTOOL_GRXFHINDIR_SIZE
#endif /* SLE_VERSION(11,3,0) */
#define netif_xmit_stopped(_q) netif_tx_queue_stopped(_q)
#if !(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(11, 4, 0))
static inline int __kc_ipv6_skip_exthdr(const struct sk_buff *skb, int start,
					u8 *nexthdrp,
					__be16 __always_unused *frag_offp)
{
	return ipv6_skip_exthdr(skb, start, nexthdrp);
}

#undef ipv6_skip_exthdr
#define ipv6_skip_exthdr(a, b, c, d) __kc_ipv6_skip_exthdr((a), (b), (c), (d))
#endif /* !SLES11sp4 or greater */

#else /* ! < 3.3.0 */
#define HAVE_ETHTOOL_GRXFHINDIR_SIZE
#define HAVE_INT_NDO_VLAN_RX_ADD_VID
#ifdef ETHTOOL_SRXNTUPLE
#undef ETHTOOL_SRXNTUPLE
#endif
#endif /* < 3.3.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(3, 4, 0) > LINUX_VERSION_CODE)
#ifndef NETIF_F_RXFCS
#define NETIF_F_RXFCS	0
#endif /* NETIF_F_RXFCS */
#ifndef NETIF_F_RXALL
#define NETIF_F_RXALL	0
#endif /* NETIF_F_RXALL */

#if !(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(11, 3, 0))
#define NUMTCS_RETURNS_U8
#endif /* !(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(11,3,0)) */

#ifndef skb_add_rx_frag
#define skb_add_rx_frag _kc_skb_add_rx_frag
extern void _kc_skb_add_rx_frag(struct sk_buff *, int, struct page *,
				int, int, unsigned int);
#endif
#ifdef NET_ADDR_RANDOM
#define eth_hw_addr_random(N) do { \
	eth_random_addr(N->dev_addr); \
	N->addr_assign_type |= NET_ADDR_RANDOM; \
	} while (0)
#else /* NET_ADDR_RANDOM */
#define eth_hw_addr_random(N) eth_random_addr(N->dev_addr)
#endif /* NET_ADDR_RANDOM */

#ifndef for_each_set_bit_from
#define for_each_set_bit_from(bit, addr, size) \
	for ((bit) = find_next_bit((addr), (size), (bit)); \
			(bit) < (size); \
			(bit) = find_next_bit((addr), (size), (bit) + 1))
#endif /* for_each_set_bit_from */

#if (RHEL_RELEASE_VERSION(7,0) > RHEL_RELEASE_CODE)
#define _kc_kmap_atomic(page)	kmap_atomic(page, KM_SKB_DATA_SOFTIRQ)
#define _kc_kunmap_atomic(addr)	kunmap_atomic(addr, KM_SKB_DATA_SOFTIRQ)
#else
#define _kc_kmap_atomic(page)	__kmap_atomic(page)
#define _kc_kunmap_atomic(addr)	__kunmap_atomic(addr)
#endif

#else /* < 3.4.0 */
#include <linux/kconfig.h>

#define _kc_kmap_atomic(page)	kmap_atomic(page)
#define _kc_kunmap_atomic(addr)	kunmap_atomic(addr)
#endif /* >= 3.4.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(3, 5, 0) > LINUX_VERSION_CODE)

#ifndef BITS_PER_LONG_LONG
#define BITS_PER_LONG_LONG 64
#endif

#ifndef ether_addr_equal
static inline bool __kc_ether_addr_equal(const u8 *addr1, const u8 *addr2)
{
	return !compare_ether_addr(addr1, addr2);
}

#define ether_addr_equal(_addr1, _addr2) \
	__kc_ether_addr_equal((_addr1), (_addr2))
#endif

/* Definitions for !CONFIG_OF_NET are introduced in 3.10 */
#ifdef CONFIG_OF_NET
static inline int of_get_phy_mode(struct device_node __always_unused *np)
{
	return -ENODEV;
}

static inline const void *
of_get_mac_address(struct device_node __always_unused *np)
{
	return NULL;
}
#endif
#else
#include <linux/of_net.h>
#define HAVE_FDB_OPS
#define HAVE_ETHTOOL_GET_TS_INFO
#endif /* < 3.5.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(3, 6, 0) > LINUX_VERSION_CODE)
#ifndef eth_random_addr
#define eth_random_addr _kc_eth_random_addr
static inline void _kc_eth_random_addr(u8 *addr)
{
        get_random_bytes(addr, ETH_ALEN);
        addr[0] &= 0xfe; /* clear multicast */
        addr[0] |= 0x02; /* set local assignment */
}
#endif /* eth_random_addr */
#endif /* < 3.6.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(3, 7, 0) > LINUX_VERSION_CODE)
#if (RHEL_RELEASE_CODE && RHEL_RELEASE_VERSION(6, 8) <= RHEL_RELEASE_CODE)
#define HAVE_NAPI_GRO_FLUSH_OLD
#endif

#else /* >= 3.7.0 */
#define HAVE_NAPI_GRO_FLUSH_OLD
#endif /* < 3.7.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(3, 8, 0) > LINUX_VERSION_CODE)
#if (SLE_VERSION_CODE && SLE_VERSION(11, 4, 0) <= SLE_VERSION_CODE)
#define HAVE_SRIOV_CONFIGURE
#endif
#else /* >= 3.8.0 */
#ifndef HAVE_SRIOV_CONFIGURE
#define HAVE_SRIOV_CONFIGURE
#endif
#endif /* < 3.8.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(3, 10, 0) > LINUX_VERSION_CODE)
#ifndef NAPI_POLL_WEIGHT
#define NAPI_POLL_WEIGHT 64
#endif
#ifdef CONFIG_PCI_IOV
extern int __kc_pci_vfs_assigned(struct pci_dev *dev);
#else
static inline int __kc_pci_vfs_assigned(struct pci_dev __always_unused *dev)
{
	return 0;
}
#endif
#define pci_vfs_assigned(dev) __kc_pci_vfs_assigned(dev)

#ifndef list_first_entry_or_null
#define list_first_entry_or_null(ptr, type, member) \
	(!list_empty(ptr) ? list_first_entry(ptr, type, member) : NULL)
#endif

#ifndef VLAN_TX_COOKIE_MAGIC
static inline struct sk_buff *__kc__vlan_hwaccel_put_tag(struct sk_buff *skb,
							 u16 vlan_tci)
{
#ifdef VLAN_TAG_PRESENT
	vlan_tci |= VLAN_TAG_PRESENT;
#endif
	skb->vlan_tci = vlan_tci;

	return skb;
}

#define __vlan_hwaccel_put_tag(skb, vlan_proto, vlan_tci) \
	__kc__vlan_hwaccel_put_tag(skb, vlan_tci)
#endif

#ifndef PCI_DEVID
#define PCI_DEVID(bus, devfn)  ((((u16)(bus)) << 8) | (devfn))
#endif

#else /* >= 3.10.0 */
#define HAVE_ENCAP_TSO_OFFLOAD
#define HAVE_SKB_INNER_NETWORK_HEADER
#if (RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_VERSION(7, 0) <= RHEL_RELEASE_CODE) && \
	(RHEL_RELEASE_VERSION(8, 0) > RHEL_RELEASE_CODE))
#define HAVE_RHEL7_PCI_DRIVER_RH
#if (RHEL_RELEASE_VERSION(7, 2) <= RHEL_RELEASE_CODE)
#define HAVE_RHEL7_PCI_RESET_NOTIFY
#endif /* RHEL >= 7.2 */
#if (RHEL_RELEASE_VERSION(7, 3) <= RHEL_RELEASE_CODE)
#define HAVE_GENEVE_RX_OFFLOAD
#if !defined(HAVE_UDP_ENC_TUNNEL) && IS_ENABLED(CONFIG_GENEVE)
#define HAVE_UDP_ENC_TUNNEL
#endif
#ifdef ETHTOOL_GLINKSETTINGS
/* pay attention pangea platform when use this micro */
#define HAVE_ETHTOOL_25G_BITS
#endif /* ETHTOOL_GLINKSETTINGS */
#endif /* RHEL >= 7.3 */

/* new hooks added to net_device_ops_extended in RHEL7.4 */
#if (RHEL_RELEASE_VERSION(7, 4) <= RHEL_RELEASE_CODE)
#define HAVE_RHEL7_NETDEV_OPS_EXT_NDO_UDP_TUNNEL
#define HAVE_UDP_ENC_RX_OFFLOAD
#endif /* RHEL >= 7.4 */

#if (RHEL_RELEASE_VERSION(7, 5) <= RHEL_RELEASE_CODE)
#define HAVE_NDO_SETUP_TC_REMOVE_TC_TO_NETDEV
#endif /* RHEL > 7.5 */

#endif /* RHEL >= 7.0 && RHEL < 8.0 */
#endif /* >= 3.10.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(3, 11, 0) > LINUX_VERSION_CODE)
#define netdev_notifier_info_to_dev(ptr) ptr
#if ((RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(6, 6)) ||\
	(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(11, 4, 0)))
#define HAVE_NDO_SET_VF_LINK_STATE
#endif
#if RHEL_RELEASE_CODE && (RHEL_RELEASE_VERSION(7, 2) < RHEL_RELEASE_CODE)
#define HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK
#endif
#if (RHEL_RELEASE_VERSION(7, 3) <= RHEL_RELEASE_CODE)
#define HAVE_RHEL7_NET_DEVICE_OPS_EXT
#endif
#if (RHEL_RELEASE_VERSION(7, 4) <= RHEL_RELEASE_CODE)
#define HAVE_RHEL7_NETDEV_OPS_EXT_NDO_SET_VF_VLAN
#endif
#if (RHEL_RELEASE_VERSION(7, 5) <= RHEL_RELEASE_CODE)
#define HAVE_RHEL7_NETDEV_OPS_EXT_NDO_SETUP_TC
#define HAVE_RHEL7_NETDEV_OPS_EXT_NDO_CHANGE_MTU
#endif

#else /* >= 3.11.0 */
#define HAVE_NDO_SET_VF_LINK_STATE
#define HAVE_SKB_INNER_PROTOCOL
#define HAVE_MPLS_FEATURES
#endif /* >= 3.11.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(3, 12, 0) <= LINUX_VERSION_CODE)
#if (SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12, 0, 0))
#define HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK
#endif
#if (KERNEL_VERSION(4, 8, 0) > LINUX_VERSION_CODE)
#define HAVE_VXLAN_RX_OFFLOAD
#if !defined(HAVE_UDP_ENC_TUNNEL) && IS_ENABLED(CONFIG_VXLAN)
#define HAVE_UDP_ENC_TUNNEL
#endif
#endif /* < 4.8.0 */
#define HAVE_NDO_GET_PHYS_PORT_ID
#define HAVE_NETIF_SET_XPS_QUEUE_CONST_MASK
#endif /* >= 3.12.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(3, 13, 0) > LINUX_VERSION_CODE)
#define dma_set_mask_and_coherent(_p, _m) __kc_dma_set_mask_and_coherent(_p, _m)
extern int __kc_dma_set_mask_and_coherent(struct device *dev, u64 mask);
#ifndef u64_stats_init
#define u64_stats_init(a) do { } while (0)
#endif
#ifndef BIT_ULL
#define BIT_ULL(n) (1ULL << (n))
#endif

#if (SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12, 1, 0))
#undef HAVE_STRUCT_PAGE_PFMEMALLOC
#define HAVE_DCBNL_OPS_SETAPP_RETURN_INT
#endif
#ifndef list_next_entry
#define list_next_entry(pos, member) \
	list_entry((pos)->member.next, typeof(*(pos)), member)
#endif
#ifndef list_prev_entry
#define list_prev_entry(pos, member) \
	list_entry((pos)->member.prev, typeof(*(pos)), member)
#endif

#if (KERNEL_VERSION(2, 6, 20) < LINUX_VERSION_CODE)
#define devm_kcalloc(dev, cnt, size, flags) \
	devm_kzalloc(dev, cnt * size, flags)
#endif /* > 2.6.20 */

#else /* >= 3.13.0 */
#define HAVE_VXLAN_CHECKS
#if (UBUNTU_VERSION_CODE && UBUNTU_VERSION_CODE >= UBUNTU_VERSION(3, 13, 0, 24))
#define HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK
#else
#define HAVE_NDO_SELECT_QUEUE_ACCEL
#endif
#define HAVE_NET_GET_RANDOM_ONCE
#define HAVE_HWMON_DEVICE_REGISTER_WITH_GROUPS
#endif /* 3.13.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(3, 14, 0) > LINUX_VERSION_CODE)

#ifndef U16_MAX
#define U16_MAX ((u16)~0U)
#endif

#ifndef U32_MAX
#define U32_MAX ((u32)~0U)
#endif

#if (!(RHEL_RELEASE_CODE && \
	RHEL_RELEASE_VERSION(7, 0) <= RHEL_RELEASE_CODE) && \
	!(SLE_VERSION_CODE && SLE_VERSION_CODE >= SLE_VERSION(12, 0, 0)))

/* it isn't expected that this would be a #define unless we made it so */
#ifndef skb_set_hash

#define PKT_HASH_TYPE_NONE	0
#define PKT_HASH_TYPE_L2	1
#define PKT_HASH_TYPE_L3	2
#define PKT_HASH_TYPE_L4	3

enum _kc_pkt_hash_types {
	_KC_PKT_HASH_TYPE_NONE = PKT_HASH_TYPE_NONE,
	_KC_PKT_HASH_TYPE_L2 = PKT_HASH_TYPE_L2,
	_KC_PKT_HASH_TYPE_L3 = PKT_HASH_TYPE_L3,
	_KC_PKT_HASH_TYPE_L4 = PKT_HASH_TYPE_L4,
};

#define pkt_hash_types         _kc_pkt_hash_types

#define skb_set_hash __kc_skb_set_hash
static inline void __kc_skb_set_hash(struct sk_buff __maybe_unused *skb,
				     u32 __maybe_unused hash,
				     int __maybe_unused type)
{
#ifdef HAVE_SKB_L4_RXHASH
	skb->l4_rxhash = (type == PKT_HASH_TYPE_L4);
#endif
#ifdef NETIF_F_RXHASH
	skb->rxhash = hash;
#endif
}
#endif /* !skb_set_hash */

#else /* RHEL_RELEASE_CODE >= 7.0 || SLE_VERSION_CODE >= 12.0 */

#ifndef HAVE_VXLAN_RX_OFFLOAD
#define HAVE_VXLAN_RX_OFFLOAD
#endif /* HAVE_VXLAN_RX_OFFLOAD */

#if !defined(HAVE_UDP_ENC_TUNNEL) && IS_ENABLED(CONFIG_VXLAN)
#define HAVE_UDP_ENC_TUNNEL
#endif

#ifndef HAVE_VXLAN_CHECKS
#define HAVE_VXLAN_CHECKS
#endif /* HAVE_VXLAN_CHECKS */
#endif /* !(RHEL_RELEASE_CODE >= 7.0 && SLE_VERSION_CODE >= 12.0) */

#ifndef pci_enable_msix_range
extern int __kc_pci_enable_msix_range(struct pci_dev *dev,
				      struct msix_entry *entries,
				      int minvec, int maxvec);
#define pci_enable_msix_range __kc_pci_enable_msix_range
#endif

#ifndef ether_addr_copy
#define ether_addr_copy __kc_ether_addr_copy
static inline void __kc_ether_addr_copy(u8 *dst, const u8 *src)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	*(u32 *)dst = *(const u32 *)src;
	*(u16 *)(dst + 4) = *(const u16 *)(src + 4);
#else
	u16 *a = (u16 *)dst;
	const u16 *b = (const u16 *)src;

	a[0] = b[0];
	a[1] = b[1];
	a[2] = b[2];
#endif
}
#endif /* ether_addr_copy */
#endif /* 3.14.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(3, 16, 0) > LINUX_VERSION_CODE)
#ifndef __dev_uc_sync
#ifdef HAVE_SET_RX_MODE
#ifdef NETDEV_HW_ADDR_T_UNICAST
int __kc_hw_addr_sync_dev(struct netdev_hw_addr_list *list,
			  struct net_device *dev,
		int (*sync)(struct net_device *, const unsigned char *),
		int (*unsync)(struct net_device *, const unsigned char *));
void __kc_hw_addr_unsync_dev(struct netdev_hw_addr_list *list,
			     struct net_device *dev,
		int (*unsync)(struct net_device *, const unsigned char *));
#endif
#ifndef NETDEV_HW_ADDR_T_MULTICAST
int __kc_dev_addr_sync_dev(struct dev_addr_list **list, int *count,
			   struct net_device *dev,
		int (*sync)(struct net_device *, const unsigned char *),
		int (*unsync)(struct net_device *, const unsigned char *));
void __kc_dev_addr_unsync_dev(struct dev_addr_list **list, int *count,
			      struct net_device *dev,
		int (*unsync)(struct net_device *, const unsigned char *));
#endif
#endif /* HAVE_SET_RX_MODE */

static inline int __kc_dev_uc_sync
	(struct net_device __maybe_unused *dev,
	int __maybe_unused (*sync)(struct net_device *, const unsigned char *),
	int __maybe_unused (*unsync)(struct net_device *,
				     const unsigned char *))
{
#ifdef NETDEV_HW_ADDR_T_UNICAST
	return __kc_hw_addr_sync_dev(&dev->uc, dev, sync, unsync);
#elif defined(HAVE_SET_RX_MODE)
	return __kc_dev_addr_sync_dev(&dev->uc_list, &dev->uc_count,
				      dev, sync, unsync);
#else
	return 0;
#endif
}

#define __dev_uc_sync __kc_dev_uc_sync

static inline void __kc_dev_uc_unsync
	(struct net_device __maybe_unused *dev,
	int __maybe_unused (*unsync)(struct net_device *,
				     const unsigned char *))
{
#ifdef HAVE_SET_RX_MODE
#ifdef NETDEV_HW_ADDR_T_UNICAST
	__kc_hw_addr_unsync_dev(&dev->uc, dev, unsync);
#else /* NETDEV_HW_ADDR_T_MULTICAST */
	__kc_dev_addr_unsync_dev(&dev->uc_list, &dev->uc_count, dev, unsync);
#endif /* NETDEV_HW_ADDR_T_UNICAST */
#endif /* HAVE_SET_RX_MODE */
}

#define __dev_uc_unsync __kc_dev_uc_unsync

static inline int __kc_dev_mc_sync
	(struct net_device __maybe_unused *dev,
	int __maybe_unused (*sync)(struct net_device *, const unsigned char *),
	int __maybe_unused (*unsync)(struct net_device *,
				     const unsigned char *))
{
#ifdef NETDEV_HW_ADDR_T_MULTICAST
	return __kc_hw_addr_sync_dev(&dev->mc, dev, sync, unsync);
#elif defined(HAVE_SET_RX_MODE)
	return __kc_dev_addr_sync_dev(&dev->mc_list, &dev->mc_count,
				      dev, sync, unsync);
#else
	return 0;
#endif
}

#define __dev_mc_sync __kc_dev_mc_sync

static inline void __kc_dev_mc_unsync
	(struct net_device __maybe_unused *dev,
	int __maybe_unused (*unsync)(struct net_device *,
				     const unsigned char *))
{
#ifdef HAVE_SET_RX_MODE
#ifdef NETDEV_HW_ADDR_T_MULTICAST
	__kc_hw_addr_unsync_dev(&dev->mc, dev, unsync);
#else /* NETDEV_HW_ADDR_T_MULTICAST */
	__kc_dev_addr_unsync_dev(&dev->mc_list, &dev->mc_count, dev, unsync);
#endif /* NETDEV_HW_ADDR_T_MULTICAST */
#endif /* HAVE_SET_RX_MODE */
}

#define __dev_mc_unsync __kc_dev_mc_unsync
#endif /* __dev_uc_sync */

#if RHEL_RELEASE_CODE && (RHEL_RELEASE_VERSION(7, 1) < RHEL_RELEASE_CODE)
#define HAVE_NDO_SET_VF_MIN_MAX_TX_RATE
#endif

#ifndef NETIF_F_GSO_UDP_TUNNEL_CSUM
/*
 * if someone backports this, hopefully they backport as a #define.
 * declare it as zero on older kernels so that if it get's or'd in
 * it won't effect anything, therefore preventing core driver changes.
 */
#define NETIF_F_GSO_UDP_TUNNEL_CSUM 0
#define SKB_GSO_UDP_TUNNEL_CSUM 0
#endif

#else
#define HAVE_NDO_SET_VF_MIN_MAX_TX_RATE
#endif /* 3.16.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(3, 18, 0) > LINUX_VERSION_CODE)
#if RHEL_RELEASE_CODE && (RHEL_RELEASE_VERSION(7, 1) < RHEL_RELEASE_CODE)
#define HAVE_MULTI_VLAN_OFFLOAD_EN
#endif

/* RHEL 7.1 backported csum_level, but SLES 12 and 12-SP1 did not */
#if RHEL_RELEASE_CODE && (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,1))
#define HAVE_SKBUFF_CSUM_LEVEL
#endif /* >= RH 7.1 */

#else
#define HAVE_SKBUFF_CSUM_LEVEL
#define HAVE_MULTI_VLAN_OFFLOAD_EN
#endif	/* 3.18.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(3, 19, 0) > LINUX_VERSION_CODE)
/* netdev_phys_port_id renamed to netdev_phys_item_id */
#define netdev_phys_item_id netdev_phys_port_id

static inline void _kc_napi_complete_done(struct napi_struct *napi,
					  int __always_unused work_done) {
	napi_complete(napi);
}

#define napi_complete_done _kc_napi_complete_done

#ifndef NETDEV_RSS_KEY_LEN
#define NETDEV_RSS_KEY_LEN (13 * 4)
#endif
#if (!(RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_VERSION(6, 7) <= RHEL_RELEASE_CODE && \
	(RHEL_RELEASE_VERSION(7, 0) > RHEL_RELEASE_CODE))))
#define netdev_rss_key_fill(buffer, len) __kc_netdev_rss_key_fill(buffer, len)
#endif /* RHEL_RELEASE_CODE */
extern void __kc_netdev_rss_key_fill(void *buffer, size_t len);
#define SPEED_20000 20000
#define SPEED_40000 40000
#ifndef dma_rmb
#define dma_rmb() rmb()
#endif
#ifndef dev_alloc_pages
#define dev_alloc_pages(_order) alloc_pages_node(NUMA_NO_NODE, (GFP_ATOMIC | \
						 __GFP_COLD | __GFP_COMP |   \
						 __GFP_MEMALLOC), (_order))
#endif
#ifndef dev_alloc_page
#define dev_alloc_page() dev_alloc_pages(0)
#endif
#if !defined(eth_skb_pad) && !defined(skb_put_padto)
/*
 *     __kc_skb_put_padto - increase size and pad an skbuff up to a minimal size
 *     @skb: buffer to pad
 *     @len: minimal length
 *
 *     Pads up a buffer to ensure the trailing bytes exist and are
 *     blanked. If the buffer already contains sufficient data it
 *     is untouched. Otherwise it is extended. Returns zero on
 *     success. The skb is freed on error.
 */
static inline int __kc_skb_put_padto(struct sk_buff *skb, unsigned int len)
{
	unsigned int size = skb->len;

	if (unlikely(size < len)) {
		len -= size;
		if (skb_pad(skb, len))
			return -ENOMEM;
		__skb_put(skb, len);
	}
	return 0;
}

#define skb_put_padto(skb, len) __kc_skb_put_padto(skb, len)

static inline int __kc_eth_skb_pad(struct sk_buff *skb)
{
	return __kc_skb_put_padto(skb, ETH_ZLEN);
}

#define eth_skb_pad(skb) __kc_eth_skb_pad(skb)
#endif /* eth_skb_pad && skb_put_padto */

#ifndef SKB_ALLOC_NAPI
/* RHEL 7.2 backported napi_alloc_skb and friends */
static inline struct sk_buff *__kc_napi_alloc_skb(struct napi_struct *napi,
						  unsigned int length)
{
	return netdev_alloc_skb_ip_align(napi->dev, length);
}

#define napi_alloc_skb(napi, len) __kc_napi_alloc_skb(napi, len)
#define __napi_alloc_skb(napi, len, mask) __kc_napi_alloc_skb(napi, len)
#endif /* SKB_ALLOC_NAPI */
#define HAVE_CONFIG_PM_RUNTIME

#if defined(RHEL_RELEASE_CODE)
#if ((RHEL_RELEASE_VERSION(6, 7) < RHEL_RELEASE_CODE) && \
	(RHEL_RELEASE_VERSION(7, 0) > RHEL_RELEASE_CODE))
#define HAVE_RXFH_HASHFUNC
#endif /* 6.7 < RHEL < 7.0 */
#if (RHEL_RELEASE_VERSION(7, 1) < RHEL_RELEASE_CODE)
#define HAVE_RXFH_HASHFUNC
#endif /* RHEL > 7.1 */
#endif /* RHEL_RELEASE_CODE */

#ifndef napi_schedule_irqoff
#define napi_schedule_irqoff	napi_schedule
#endif
#ifndef READ_ONCE
#define READ_ONCE(_x) ACCESS_ONCE(_x)
#endif
#else /* 3.19.0 */
#define HAVE_RXFH_HASHFUNC
#endif /* 3.19.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(4, 0, 0) > LINUX_VERSION_CODE)
#ifndef skb_vlan_tag_present
#define skb_vlan_tag_present(__skb)	vlan_tx_tag_present(__skb)
#define skb_vlan_tag_get(__skb)		vlan_tx_tag_get(__skb)
#define skb_vlan_tag_get_id(__skb)	vlan_tx_tag_get_id(__skb)
#endif
#endif /* 4.0.0 */

/****************************************************************/
#if (KERNEL_VERSION(4, 2, 0) > LINUX_VERSION_CODE)
#define SPEED_25000	25000
#define SPEED_100000	100000
#endif /* 4.2.0 */

/****************************************************************/
#if (KERNEL_VERSION(4, 4, 0) > LINUX_VERSION_CODE)
#if (RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7,3))
#define HAVE_NDO_SET_VF_TRUST
#endif /* (RHEL_RELEASE >= 7.3) */

#else /* < 4.4.0 */
#define HAVE_NDO_SET_VF_TRUST
#endif /* 4.4.0 */

/****************************************************************/
#if (KERNEL_VERSION(4, 5, 0) > LINUX_VERSION_CODE)
#ifndef NETIF_F_SCTP_CRC
#define NETIF_F_SCTP_CRC NETIF_F_SCTP_CSUM
#endif /* NETIF_F_SCTP_CRC */
#endif /* 4.5.0 */

/****************************************************************/
#if (KERNEL_VERSION(4, 6, 0) > LINUX_VERSION_CODE)
#if !(SLE_VERSION_CODE && (SLE_VERSION_CODE >= SLE_VERSION(12,3,0))) && \
	!(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 4)) && \
	!(DEEPIN_VERSION_CODE && (DEEPIN_VERSION_CODE == DEEPIN_PRODUCT_VERSION(15,2,0)))
static inline void csum_replace_by_diff(__sum16 *sum, __wsum diff)
{
	*sum = csum_fold(csum_add(diff, ~csum_unfold(*sum)));
}
#endif
#endif /* 4.6.0 */

/****************************************************************/
#if (KERNEL_VERSION(4, 7, 0) > LINUX_VERSION_CODE)
#define HAVE_PAGE_COUNT

#ifdef ETHTOOL_GLINKSETTINGS
#if (UBUNTU_VERSION_CODE && UBUNTU_VERSION_CODE >= UBUNTU_VERSION(4, 4, 0, 62))
#define ETHTOOL_LINK_MODE_25000baseCR_Full_BIT  31
#define ETHTOOL_LINK_MODE_25000baseKR_Full_BIT  32
#define ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT  36
#define ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT  38
#endif
#endif

#endif /* 4.7.0 */

/****************************************************************/
#ifdef ETHTOOL_GMODULEEEPROM
#ifndef ETH_MODULE_SFF_8472
#define ETH_MODULE_SFF_8472		0x2
#endif
#ifndef ETH_MODULE_SFF_8636
#define ETH_MODULE_SFF_8636		0x3
#endif
#ifndef ETH_MODULE_SFF_8436
#define ETH_MODULE_SFF_8436		0x4
#endif
#ifndef ETH_MODULE_SFF_8472_LEN
#define ETH_MODULE_SFF_8472_LEN 	512
#endif
#ifndef ETH_MODULE_SFF_8636_MAX_LEN
#define ETH_MODULE_SFF_8636_MAX_LEN	640
#endif
#ifndef ETH_MODULE_SFF_8436_MAX_LEN
#define ETH_MODULE_SFF_8436_MAX_LEN	640
#endif
#endif

/****************************************************************/
#if (KERNEL_VERSION(4, 8, 0) > LINUX_VERSION_CODE)
#if (RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 4)) || \
	(SLE_VERSION_CODE && (SLE_VERSION_CODE >= SLE_VERSION(12, 3, 0)))
#define HAVE_IO_MAP_WC_SIZE
#endif

#else	/* > 4.8.0 */
#define HAVE_IO_MAP_WC_SIZE
#endif	/* 4.8.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(4, 10, 0) > LINUX_VERSION_CODE)
#if (SLE_VERSION_CODE && (SLE_VERSION_CODE >= SLE_VERSION(12, 3, 0))) || \
	(DEEPIN_VERSION_CODE && (DEEPIN_VERSION_CODE == DEEPIN_PRODUCT_VERSION(15,2,0)))
#define HAVE_NETDEVICE_MIN_MAX_MTU
#endif
#if (RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 5))
#define HAVE_NETDEVICE_EXTENDED_MIN_MAX_MTU
#endif
#else	/* >= 4.10.0 */
#define HAVE_NETDEVICE_MIN_MAX_MTU
#endif	/* 4.10.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE)
#define HAVE_STRUCT_CURRENT
#if (SLE_VERSION_CODE && (SLE_VERSION(12, 3, 0) <= SLE_VERSION_CODE)) || \
	(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(7, 5)) || \
	(DEEPIN_VERSION_CODE && (DEEPIN_VERSION_CODE == DEEPIN_PRODUCT_VERSION(15,2,0)))
#define HAVE_VOID_NDO_GET_STATS64
#endif
#ifdef CONFIG_NET_RX_BUSY_POLL
#define HAVE_NDO_BUSY_POLL
#endif
#else /* > 4.11 */
#define HAVE_VOID_NDO_GET_STATS64
#define HAVE_VM_OPS_FAULT_NO_VMA
#endif /* 4.11.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(4, 13, 0) > LINUX_VERSION_CODE)
#else /* > 4.13 */
#define HAVE_HWTSTAMP_FILTER_NTP_ALL
#define HAVE_NDO_SETUP_TC_CHAIN_INDEX
#define HAVE_PCI_ERROR_HANDLER_RESET_PREPARE
#define HAVE_PTP_CLOCK_DO_AUX_WORK
#endif /* 4.13.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(4, 14, 0) > LINUX_VERSION_CODE)
#if (SUSE_PRODUCT_CODE && (SUSE_PRODUCT_CODE >= SUSE_PRODUCT(1, 12, 4, 0)))
#define HAVE_NDO_SETUP_TC_REMOVE_TC_TO_NETDEV
#endif

#else /* > 4.14 */
#define HAVE_NDO_SETUP_TC_REMOVE_TC_TO_NETDEV
#endif /* 4.14.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(4, 15, 0) > LINUX_VERSION_CODE)
#if ((KERNEL_VERSION(3, 10, 0) == LINUX_VERSION_CODE) && \
     RHEL_RELEASE_CODE && (RHEL_RELEASE_VERSION(7, 6) <= RHEL_RELEASE_CODE )) || \
     (SUSE_PRODUCT_CODE && (SUSE_PRODUCT_CODE == SUSE_PRODUCT(1, 15, 1, 0))) || \
     (SUSE_PRODUCT_CODE && (SUSE_PRODUCT_CODE == SUSE_PRODUCT(1, 12, 5, 0)))
#else
#ifndef KEYLIN_V7UPDATE6
#define  TC_SETUP_QDISC_MQPRIO  TC_SETUP_MQPRIO
#endif
#endif
#if (KERNEL_VERSION(4, 12, 0) <= LINUX_VERSION_CODE)
#if (SUSE_PRODUCT_CODE && (SUSE_PRODUCT_CODE >= SUSE_PRODUCT(1, 12, 4, 0))) || \
	(RHEL_RELEASE_CODE && RHEL_RELEASE_VERSION(7, 5) <= RHEL_RELEASE_CODE)
#else /* 4.12-4.15*/
#define HAVE_IP6_FRAG_ID_ENABLE_UFO
#endif
#else /* < 4.12.0 */
#define HAVE_IP6_FRAG_ID_ENABLE_UFO
#endif
#else /* >= 4.15.0 */
#define HAVE_TIMER_SETUP
#endif /* 4.15.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE)
#if (RHEL_RELEASE_CODE && (RHEL_RELEASE_VERSION(8, 0) <= RHEL_RELEASE_CODE )) || \
	(SUSE_PRODUCT_CODE && (SUSE_PRODUCT_CODE == SUSE_PRODUCT(1, 15, 1, 0))) || \
	(SUSE_PRODUCT_CODE && (SUSE_PRODUCT_CODE == SUSE_PRODUCT(1, 12, 5, 0)))
#define HAVE_NDO_SELECT_QUEUE_SB_DEV
#endif

#endif /* 4.19.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(5, 0, 0) > LINUX_VERSION_CODE)
#if (RHEL_RELEASE_CODE && RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8, 0))
#define dev_open(x) dev_open(x, NULL)
#endif
#else /* >= 5.0.0 */
#define dev_open(x) dev_open(x, NULL)
#define HAVE_NEW_ETHTOOL_LINK_SETTINGS_ONLY

#ifndef dma_zalloc_coherent
#define dma_zalloc_coherent(d, s, h, f) _hinic_dma_zalloc_coherent(d, s, h, f)
static inline void *_hinic_dma_zalloc_coherent(struct device *dev,  size_t size,
					    dma_addr_t *dma_handle, gfp_t gfp)
{
	/* Above kernel 5.0, fixed up all remaining architectures
	 * to zero the memory in dma_alloc_coherent, and made
	 * dma_zalloc_coherent a no-op wrapper around dma_alloc_coherent,
	 * which fixes all of the above issues.
	 */
	return dma_alloc_coherent(dev, size, dma_handle, gfp);
}
#endif

#ifndef do_gettimeofday
#define do_gettimeofday(time) _kc_do_gettimeofday(time)
static inline void _kc_do_gettimeofday(struct timeval *tv)
{
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / NSEC_PER_USEC;
}
#endif
#endif /* 5.0.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(5, 2, 0) > LINUX_VERSION_CODE)
#else /* >= 5.2.0 */
#define HAVE_NDO_SELECT_QUEUE_SB_DEV_ONLY
#endif /* 5.2.0 */

/*****************************************************************************/
#if (KERNEL_VERSION(5, 3, 0) > LINUX_VERSION_CODE)
#if (KERNEL_VERSION(3, 14, 0) <= LINUX_VERSION_CODE)
#define HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK
#endif
#if (KERNEL_VERSION(4, 19, 0) <= LINUX_VERSION_CODE)
#define HAVE_NDO_SELECT_QUEUE_SB_DEV
#endif
#endif /* 5.3.0 */


/*****************************************************************************/
/* vxlan outer udp checksum will offload and skb->inner_transport_header is wrong */
#if (SLE_VERSION_CODE && ((SLE_VERSION(12, 1, 0) == SLE_VERSION_CODE) || \
	(SLE_VERSION(12, 0, 0) == SLE_VERSION_CODE))) || \
	(RHEL_RELEASE_CODE && (RHEL_RELEASE_VERSION(7, 0) == RHEL_RELEASE_CODE))
#define HAVE_OUTER_IPV6_TUNNEL_OFFLOAD
#endif

#if (KERNEL_VERSION(3, 10, 0) <= LINUX_VERSION_CODE)
#define HAVE_ENCAPSULATION_TSO
#endif

#if (KERNEL_VERSION(3, 8, 0) <= LINUX_VERSION_CODE)
#define HAVE_ENCAPSULATION_CSUM
#endif

#ifndef eth_zero_addr
static inline void __kc_eth_zero_addr(u8 *addr)
{
	memset(addr, 0x00, ETH_ALEN);
}

#define eth_zero_addr(_addr) __kc_eth_zero_addr(_addr)
#endif

#ifndef netdev_hw_addr_list_for_each
#define netdev_hw_addr_list_for_each(ha, l) \
	list_for_each_entry(ha, &(l)->list, list)
#endif

#if (KERNEL_VERSION(3, 8, 0) > LINUX_VERSION_CODE)
int pci_sriov_get_totalvfs(struct pci_dev *dev);
#endif
#if (KERNEL_VERSION(4, 1, 0) > LINUX_VERSION_CODE)
unsigned int cpumask_local_spread(unsigned int i, int node);
#endif
#define spin_lock_deinit(lock)

struct file *file_creat(const char *file_name);

struct file *file_open(const char *file_name);

void file_close(struct file *file_handle);

u32 get_file_size(struct file *file_handle);

void set_file_position(struct file *file_handle, u32 position);

int file_read(struct file *file_handle, char *log_buffer,
	      u32 rd_length, u32 *file_pos);

u32 file_write(struct file *file_handle, char *log_buffer, u32 wr_length);

struct sdk_thread_info {
	struct task_struct *thread_obj;
	char			*name;
	void			(*thread_fn)(void *x);
	void			*thread_event;
	void			*data;
};

int creat_thread(struct sdk_thread_info *thread_info);

void stop_thread(struct sdk_thread_info *thread_info);

#define destroy_work(work)
void utctime_to_localtime(u64 utctime, u64 *localtime);
#ifndef HAVE_TIMER_SETUP
void initialize_timer(void *adapter_hdl, struct timer_list *timer);
#endif
void add_to_timer(struct timer_list *timer, long period);
void stop_timer(struct timer_list *timer);
void delete_timer(struct timer_list *timer);

int local_atoi(const char *name);

#define nicif_err(priv, type, dev, fmt, args...)		\
	netif_level(err, priv, type, dev, "[NIC]"fmt, ##args)
#define nicif_warn(priv, type, dev, fmt, args...)		\
	netif_level(warn, priv, type, dev, "[NIC]"fmt, ##args)
#define nicif_notice(priv, type, dev, fmt, args...)		\
	netif_level(notice, priv, type, dev, "[NIC]"fmt, ##args)
#define nicif_info(priv, type, dev, fmt, args...)		\
	netif_level(info, priv, type, dev, "[NIC]"fmt, ##args)
#define nicif_dbg(priv, type, dev, fmt, args...)		\
	netif_level(dbg, priv, type, dev, "[NIC]"fmt, ##args)

#define destory_completion(completion)
#define sema_deinit(lock)
#define mutex_deinit(lock)
#define rwlock_deinit(lock)

#define tasklet_state(tasklet) ((tasklet)->state)

#endif
/*****************************************************************************/

