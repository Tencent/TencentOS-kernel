/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 * Copyright (c) 2016-2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/pci.h>
#include <linux/version.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#if !defined(NEW_FLOW_KEYS) && defined(HAVE_FLOW_KEYS)
#include <net/flow_keys.h>
#endif
#include <linux/sched.h>
#include <linux/dma-buf.h>
#ifdef HAVE_IEEE1588_SUPPORT
#include <linux/ptp_clock_kernel.h>
#endif
#if defined(HAVE_TC_FLOW_CLS_OFFLOAD) || defined(HAVE_TC_CLS_FLOWER_OFFLOAD)
#include <net/pkt_cls.h>
#endif
#ifdef HAVE_DIM
#include <linux/dim.h>
#endif
#ifdef HAVE_DEVLINK
#include <net/devlink.h>
#endif

#ifndef IS_ENABLED
#define __ARG_PLACEHOLDER_1	0,
#define config_enabled(cfg)	_config_enabled(cfg)
#define _config_enabled(value)	__config_enabled(__ARG_PLACEHOLDER_##value)
#define __config_enabled(arg1_or_junk)	___config_enabled(arg1_or_junk 1, 0)
#define ___config_enabled(__ignored, val, ...)	val
#define IS_ENABLED(option)	\
	(config_enabled(option) || config_enabled(option##_MODULE))
#endif

#if !IS_ENABLED(CONFIG_NET_DEVLINK)
#undef HAVE_DEVLINK
#undef HAVE_DEVLINK_INFO
#undef HAVE_DEVLINK_PARAM
#undef HAVE_NDO_DEVLINK_PORT
#undef HAVE_DEVLINK_PORT_PARAM
#undef HAVE_DEVLINK_FLASH_UPDATE
#undef HAVE_DEVLINK_HEALTH_REPORT
#endif

/* Reconcile all dependencies for VF reps:
 * SRIOV, Devlink, Switchdev and HW port info in metadata_dst
 */
#if defined(CONFIG_BNXT_SRIOV) && defined(HAVE_DEVLINK) && \
	defined(CONFIG_NET_SWITCHDEV) && defined(HAVE_METADATA_HW_PORT_MUX) && \
	(LINUX_VERSION_CODE >= 0x030a00)
#define CONFIG_VF_REPS		1
#endif
/* DEVLINK code has dependencies on VF reps */
#ifdef HAVE_DEVLINK_PARAM
#define CONFIG_VF_REPS		1
#endif
#ifdef CONFIG_VF_REPS
#ifndef SWITCHDEV_SET_OPS
#define SWITCHDEV_SET_OPS(netdev, ops) ((netdev)->switchdev_ops = (ops))
#endif
#endif

/* Reconcile all dependencies for TC Flower offload
 * Need the following to be defined to build TC flower offload
 * HAVE_TC_FLOW_CLS_OFFLOAD OR HAVE_TC_CLS_FLOWER_OFFLOAD
 * HAVE_RHASHTABLE
 * HAVE_FLOW_DISSECTOR_KEY_ICMP
 * CONFIG_NET_SWITCHDEV
 * HAVE_TCF_EXTS_TO_LIST (its possible to do without this but
 * the code gets a bit complicated. So, for now depend on this.)
 * HAVE_TCF_TUNNEL
 * Instead of checking for all of the above defines, enable one
 * define when all are enabled.
 */
#if (defined(HAVE_TC_FLOW_CLS_OFFLOAD) || \
	defined(HAVE_TC_CLS_FLOWER_OFFLOAD)) && \
	(defined(HAVE_TCF_EXTS_TO_LIST) || \
	defined(HAVE_TC_EXTS_FOR_ACTION)) && \
	defined(HAVE_RHASHTABLE) && defined(HAVE_FLOW_DISSECTOR_KEY_ICMP) && \
	defined(HAVE_TCF_TUNNEL) && defined(CONFIG_NET_SWITCHDEV) && \
	(LINUX_VERSION_CODE >= 0x030a00)
#define	CONFIG_BNXT_FLOWER_OFFLOAD	1
#ifndef HAVE_NDO_GET_PORT_PARENT_ID
#define netdev_port_same_parent_id(a, b) switchdev_port_same_parent_id(a, b)
#endif
#else
#undef CONFIG_BNXT_FLOWER_OFFLOAD
#endif

/* With upstream kernels >= v5.2.0, struct tc_cls_flower_offload has been
 * replaced by struct flow_cls_offload. For older kernels(< v5.2.0), rename
 * the respective definitions here.
 */
#ifndef HAVE_TC_FLOW_CLS_OFFLOAD
#ifdef HAVE_TC_CLS_FLOWER_OFFLOAD
#define flow_cls_offload		tc_cls_flower_offload
#define flow_cls_offload_flow_rule	tc_cls_flower_offload_flow_rule
#define FLOW_CLS_REPLACE		TC_CLSFLOWER_REPLACE
#define FLOW_CLS_DESTROY		TC_CLSFLOWER_DESTROY
#define FLOW_CLS_STATS			TC_CLSFLOWER_STATS
#endif
#endif

#if defined(CONFIG_HWMON) || defined(CONFIG_HWMON_MODULE)
#if defined(HAVE_NEW_HWMON_API)
#define CONFIG_BNXT_HWMON	1
#endif
#endif

#ifndef SPEED_20000
#define SPEED_20000		20000
#endif

#ifndef SPEED_25000
#define SPEED_25000		25000
#endif

#ifndef SPEED_40000
#define SPEED_40000		40000
#endif

#ifndef SPEED_50000
#define SPEED_50000		50000
#endif

#ifndef SPEED_100000
#define SPEED_100000		100000
#endif

#ifndef SPEED_200000
#define SPEED_200000		200000
#endif

#ifndef SPEED_UNKNOWN
#define SPEED_UNKNOWN		-1
#endif

#ifndef DUPLEX_UNKNOWN
#define DUPLEX_UNKNOWN		0xff
#endif

#ifndef PORT_DA
#define PORT_DA			0x05
#endif

#ifndef PORT_NONE
#define PORT_NONE		0xef
#endif

#if !defined(SUPPORTED_40000baseCR4_Full)
#define SUPPORTED_40000baseCR4_Full	(1 << 24)

#define ADVERTISED_40000baseCR4_Full	(1 << 24)
#endif

#if !defined(ETH_TEST_FL_EXTERNAL_LB)
#define ETH_TEST_FL_EXTERNAL_LB		0
#define ETH_TEST_FL_EXTERNAL_LB_DONE	0
#endif

#if !defined(IPV4_FLOW)
#define IPV4_FLOW	0x10
#endif

#if !defined(IPV6_FLOW)
#define IPV6_FLOW	0x11
#endif

#if defined(HAVE_ETH_GET_HEADLEN) || (LINUX_VERSION_CODE > 0x040900)
#define BNXT_RX_PAGE_MODE_SUPPORT	1
#endif

#if !defined(ETH_P_8021AD)
#define ETH_P_8021AD		0x88A8
#endif

#if !defined(ETH_P_ROCE)
#define ETH_P_ROCE		0x8915
#endif

#if !defined(ROCE_V2_UDP_PORT)
#define ROCE_V2_UDP_DPORT	4791
#endif

#ifndef NETIF_F_GSO_UDP_TUNNEL
#define NETIF_F_GSO_UDP_TUNNEL	0
#endif

#ifndef NETIF_F_GSO_UDP_TUNNEL_CSUM
#define NETIF_F_GSO_UDP_TUNNEL_CSUM	0
#endif

#ifndef NETIF_F_GSO_GRE
#define NETIF_F_GSO_GRE		0
#endif

#ifndef NETIF_F_GSO_GRE_CSUM
#define NETIF_F_GSO_GRE_CSUM	0
#endif

#ifndef NETIF_F_GSO_IPIP
#define NETIF_F_GSO_IPIP	0
#endif

#ifndef NETIF_F_GSO_SIT
#define NETIF_F_GSO_SIT		0
#endif

#ifndef NETIF_F_GSO_IPXIP4
#define NETIF_F_GSO_IPXIP4	(NETIF_F_GSO_IPIP | NETIF_F_GSO_SIT)
#endif

#ifndef NETIF_F_GSO_PARTIAL
#define NETIF_F_GSO_PARTIAL	0
#else
#define HAVE_GSO_PARTIAL_FEATURES	1
#endif

/* Tie rx checksum offload to tx checksum offload for older kernels. */
#ifndef NETIF_F_RXCSUM
#define NETIF_F_RXCSUM		NETIF_F_IP_CSUM
#endif

#ifndef NETIF_F_NTUPLE
#define NETIF_F_NTUPLE		0
#endif

#ifndef NETIF_F_RXHASH
#define NETIF_F_RXHASH		0
#else
#define HAVE_NETIF_F_RXHASH
#endif

#ifdef NETIF_F_GRO_HW
#define HAVE_NETIF_F_GRO_HW	1
#else
#define NETIF_F_GRO_HW		0
#endif

#ifndef HAVE_SKB_GSO_UDP_TUNNEL_CSUM
#ifndef HAVE_SKB_GSO_UDP_TUNNEL
#define SKB_GSO_UDP_TUNNEL 0
#endif
#define SKB_GSO_UDP_TUNNEL_CSUM SKB_GSO_UDP_TUNNEL
#endif

#ifndef BRIDGE_MODE_VEB
#define BRIDGE_MODE_VEB		0
#endif

#ifndef BRIDGE_MODE_VEPA
#define BRIDGE_MODE_VEPA	1
#endif

#ifndef BRIDGE_MODE_UNDEF
#define BRIDGE_MODE_UNDEF	0xffff
#endif

#ifndef DEFINE_DMA_UNMAP_ADDR
#define DEFINE_DMA_UNMAP_ADDR(mapping) DECLARE_PCI_UNMAP_ADDR(mapping)
#endif

#ifndef DEFINE_DMA_UNMAP_LEN
#define DEFINE_DMA_UNMAP_LEN(len) DECLARE_PCI_UNMAP_LEN(len)
#endif

#ifndef dma_unmap_addr_set
#define dma_unmap_addr_set pci_unmap_addr_set
#endif

#ifndef dma_unmap_addr
#define dma_unmap_addr pci_unmap_addr
#endif

#ifndef dma_unmap_len
#define dma_unmap_len pci_unmap_len
#endif

#ifdef HAVE_DMA_ATTRS_H
#define dma_map_single_attrs(dev, cpu_addr, size, dir, attrs) \
	dma_map_single_attrs(dev, cpu_addr, size, dir, NULL)

#define dma_unmap_single_attrs(dev, dma_addr, size, dir, attrs) \
	dma_unmap_single_attrs(dev, dma_addr, size, dir, NULL)

#ifdef HAVE_DMA_MAP_PAGE_ATTRS
#define dma_map_page_attrs(dev, page, offset, size, dir, attrs) \
	dma_map_page_attrs(dev, page, offset, size, dir, NULL)

#define dma_unmap_page_attrs(dev, dma_addr, size, dir, attrs) \
	dma_unmap_page_attrs(dev, dma_addr, size, dir, NULL)
#endif
#endif

#ifndef HAVE_DMA_MAP_PAGE_ATTRS
#define dma_map_page_attrs(dev, page, offset, size, dir, attrs) \
	dma_map_page(dev, page, offset, size, dir)

#define dma_unmap_page_attrs(dev, dma_addr, size, dir, attrs) \
	dma_unmap_page(dev, dma_addr, size, dir)
#endif

static inline struct dma_buf *dma_buf_export_attr(void *priv, int pg_count,
						  const struct dma_buf_ops *ops)
{
#ifdef HAVE_DMA_EXPORT
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	exp_info.ops = ops;
	exp_info.size = pg_count << PAGE_SHIFT;
	exp_info.flags = O_RDWR;
	exp_info.priv = priv;
	exp_info.exp_name = "eem_dma_buf";
	return dma_buf_export(&exp_info);
#endif

#ifdef HAVE_DMA_EXPORT_FLAG_RESV
	return dma_buf_export(priv, ops, pg_count << PAGE_SHIFT, O_RDWR, NULL);
#endif

#ifdef HAVE_DMA_EXPORT_FLAG
	return dma_buf_export(priv, ops, pg_count << PAGE_SHIFT, O_RDWR);
#endif
}

#ifndef RHEL_RELEASE_VERSION
#define RHEL_RELEASE_VERSION(a, b) 0
#endif

#if defined(RHEL_RELEASE_CODE) && (RHEL_RELEASE_CODE == RHEL_RELEASE_VERSION(6,3))
#if defined(CONFIG_X86_64) && !defined(CONFIG_NEED_DMA_MAP_STATE)
#undef DEFINE_DMA_UNMAP_ADDR
#define DEFINE_DMA_UNMAP_ADDR(ADDR_NAME)        dma_addr_t ADDR_NAME
#undef DEFINE_DMA_UNMAP_LEN
#define DEFINE_DMA_UNMAP_LEN(LEN_NAME)          __u32 LEN_NAME
#undef dma_unmap_addr
#define dma_unmap_addr(PTR, ADDR_NAME)           ((PTR)->ADDR_NAME)
#undef dma_unmap_addr_set
#define dma_unmap_addr_set(PTR, ADDR_NAME, VAL)  (((PTR)->ADDR_NAME) = (VAL))
#undef dma_unmap_len
#define dma_unmap_len(PTR, LEN_NAME)             ((PTR)->LEN_NAME)
#undef dma_unmap_len_set
#define dma_unmap_len_set(PTR, LEN_NAME, VAL)    (((PTR)->LEN_NAME) = (VAL))
#endif
#endif

#ifdef HAVE_NDO_SET_VF_VLAN_RH73
#define ndo_set_vf_vlan ndo_set_vf_vlan_rh73
#endif

#ifdef HAVE_NDO_CHANGE_MTU_RH74
#define ndo_change_mtu ndo_change_mtu_rh74
#undef HAVE_MIN_MTU
#endif

#ifdef HAVE_NDO_SETUP_TC_RH72
#define ndo_setup_tc ndo_setup_tc_rh72
#endif

#ifdef HAVE_NDO_SET_VF_TRUST_RH
#define ndo_set_vf_trust extended.ndo_set_vf_trust
#endif

#ifdef HAVE_NDO_UDP_TUNNEL_RH
#define ndo_udp_tunnel_add extended.ndo_udp_tunnel_add
#define ndo_udp_tunnel_del extended.ndo_udp_tunnel_del
#endif

#ifndef HAVE_TC_SETUP_QDISC_MQPRIO
#define TC_SETUP_QDISC_MQPRIO	TC_SETUP_MQPRIO
#endif

#if defined(HAVE_NDO_SETUP_TC_RH) || defined(HAVE_EXT_GET_PHYS_PORT_NAME) || defined(HAVE_NDO_SET_VF_TRUST_RH)
#define HAVE_NDO_SIZE	1
#endif

#ifndef HAVE_FLOW_KEYS
struct flow_keys {
	__be32 src;
	__be32 dst;
	union {
		__be32 ports;
		__be16 port16[2];
	};
	u16 thoff;
	u8 ip_proto;
};
#endif

#ifndef FLOW_RSS
#define FLOW_RSS	0x20000000
#endif

#ifndef FLOW_MAC_EXT
#define FLOW_MAC_EXT	0x40000000
#endif

#ifndef ETHTOOL_RX_FLOW_SPEC_RING
#define ETHTOOL_RX_FLOW_SPEC_RING	0x00000000FFFFFFFFLL
#define ETHTOOL_RX_FLOW_SPEC_RING_VF	0x000000FF00000000LL
#define ETHTOOL_RX_FLOW_SPEC_RING_VF_OFF 32
static inline __u64 ethtool_get_flow_spec_ring(__u64 ring_cookie)
{
	return ETHTOOL_RX_FLOW_SPEC_RING & ring_cookie;
};

static inline __u64 ethtool_get_flow_spec_ring_vf(__u64 ring_cookie)
{
	return (ETHTOOL_RX_FLOW_SPEC_RING_VF & ring_cookie) >>
				ETHTOOL_RX_FLOW_SPEC_RING_VF_OFF;
};
#endif

#ifndef ETHTOOL_GEEE
struct ethtool_eee {
	__u32	cmd;
	__u32	supported;
	__u32	advertised;
	__u32	lp_advertised;
	__u32	eee_active;
	__u32	eee_enabled;
	__u32	tx_lpi_enabled;
	__u32	tx_lpi_timer;
	__u32	reserved[2];
};
#endif

#ifndef HAVE_ETHTOOL_RESET_CRASHDUMP
enum compat_ethtool_reset_flags { ETH_RESET_CRASHDUMP = 1 << 9 };
#endif

#ifndef HAVE_SKB_FRAG_PAGE
static inline struct page *skb_frag_page(const skb_frag_t *frag)
{
	return frag->page;
}

static inline void *skb_frag_address_safe(const skb_frag_t *frag)
{
	void *ptr = page_address(skb_frag_page(frag));
	if (unlikely(!ptr))
		return NULL;

	return ptr + frag->page_offset;
}

static inline void __skb_frag_set_page(skb_frag_t *frag, struct page *page)
{
	frag->page = page;
}

#define skb_frag_dma_map(x, frag, y, len, z) \
	pci_map_page(bp->pdev, (frag)->page, \
		     (frag)->page_offset, (len), PCI_DMA_TODEVICE)
#endif

#ifndef HAVE_SKB_FRAG_ACCESSORS
static inline void skb_frag_off_add(skb_frag_t *frag, int delta)
{
	frag->page_offset += delta;
}
#endif

#ifndef HAVE_PCI_VFS_ASSIGNED
static inline int pci_vfs_assigned(struct pci_dev *dev)
{
	return 0;
}
#endif

#ifndef HAVE_PCI_NUM_VF
#include <../drivers/pci/pci.h>

static inline int pci_num_vf(struct pci_dev *dev)
{
	if (!dev->is_physfn)
		return 0;

	return dev->sriov->nr_virtfn;
}
#endif

#ifndef SKB_ALLOC_NAPI
static inline struct sk_buff *napi_alloc_skb(struct napi_struct *napi,
					     unsigned int length)
{
	struct sk_buff *skb;

	length += NET_SKB_PAD + NET_IP_ALIGN;
	skb = netdev_alloc_skb(napi->dev, length);

	if (likely(skb))
		skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);

	return skb;
}
#endif

#ifndef HAVE_SKB_HASH_TYPE

enum pkt_hash_types {
	PKT_HASH_TYPE_NONE,	/* Undefined type */
	PKT_HASH_TYPE_L2,	/* Input: src_MAC, dest_MAC */
	PKT_HASH_TYPE_L3,	/* Input: src_IP, dst_IP */
	PKT_HASH_TYPE_L4,	/* Input: src_IP, dst_IP, src_port, dst_port */
};

static inline void
skb_set_hash(struct sk_buff *skb, __u32 hash, enum pkt_hash_types type)
{
#ifdef HAVE_NETIF_F_RXHASH
	skb->rxhash = hash;
#endif
}

#endif

#define GET_NET_STATS(x) (unsigned long)le64_to_cpu(x)

#if !defined(NETDEV_RX_FLOW_STEER) || !defined(HAVE_FLOW_KEYS) || (LINUX_VERSION_CODE < 0x030300) || defined(NO_NETDEV_CPU_RMAP)
#undef CONFIG_RFS_ACCEL
#endif

#ifdef CONFIG_RFS_ACCEL
#if !defined(HAVE_FLOW_HASH_FROM_KEYS) && !defined(HAVE_GET_HASH_RAW)
static inline __u32 skb_get_hash_raw(const struct sk_buff *skb)
{
	return skb->rxhash;
}
#endif
#endif /* CONFIG_RFS_ACCEL */

#if !defined(IEEE_8021QAZ_APP_SEL_DGRAM) || !defined(CONFIG_DCB) || !defined(HAVE_IEEE_DELAPP)
#undef CONFIG_BNXT_DCB
#endif

#ifdef CONFIG_BNXT_DCB
#ifndef IEEE_8021QAZ_APP_SEL_DSCP
#define IEEE_8021QAZ_APP_SEL_DSCP	5
#endif
#endif

#ifndef NETDEV_HW_FEATURES
#define hw_features features
#endif

#ifndef HAVE_NETDEV_FEATURES_T
#ifdef HAVE_NDO_FIX_FEATURES
typedef u32 netdev_features_t;
#else
typedef unsigned long netdev_features_t;
#endif
#endif

#if !defined(IFF_UNICAST_FLT)
#define IFF_UNICAST_FLT 0
#endif

#ifndef HAVE_NEW_BUILD_SKB
#define build_skb(data, frag) build_skb(data)
#endif

#ifndef __rcu
#define __rcu
#endif

#ifndef rcu_dereference_protected
#define rcu_dereference_protected(p, c)	\
	rcu_dereference((p))
#endif

#ifndef rcu_access_pointer
#define rcu_access_pointer rcu_dereference
#endif

#ifndef rtnl_dereference
#define rtnl_dereference(p)		\
	rcu_dereference_protected(p, lockdep_rtnl_is_held())
#endif

#ifndef RCU_INIT_POINTER
#define RCU_INIT_POINTER(p, v)	\
	p = (typeof(*v) __force __rcu *)(v)
#endif

#ifdef HAVE_OLD_HLIST
#define __hlist_for_each_entry_rcu(f, n, h, m) \
	hlist_for_each_entry_rcu(f, n, h, m)
#define __hlist_for_each_entry_safe(f, n, t, h, m) \
	hlist_for_each_entry_safe(f, n, t, h, m)
#else
#define __hlist_for_each_entry_rcu(f, n, h, m) \
	hlist_for_each_entry_rcu(f, h, m)
#define __hlist_for_each_entry_safe(f, n, t, h, m) \
	hlist_for_each_entry_safe(f, t, h, m)
#endif

#ifndef VLAN_PRIO_SHIFT
#define VLAN_PRIO_SHIFT		13
#endif

#ifndef NETIF_F_HW_VLAN_CTAG_TX
#define NETIF_F_HW_VLAN_CTAG_TX NETIF_F_HW_VLAN_TX
#define NETIF_F_HW_VLAN_CTAG_RX NETIF_F_HW_VLAN_RX
/* 802.1AD not supported on older kernels */
#define NETIF_F_HW_VLAN_STAG_TX 0
#define NETIF_F_HW_VLAN_STAG_RX 0

#define __vlan_hwaccel_put_tag(skb, proto, tag) \
	if (proto == ntohs(ETH_P_8021Q))	\
		__vlan_hwaccel_put_tag(skb, tag)

#define vlan_proto protocol

#if defined(HAVE_VLAN_RX_REGISTER)
#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
#define OLD_VLAN	1
#define OLD_VLAN_VALID	(1 << 31)
#endif
#endif

#endif

#ifndef HAVE_NETDEV_NOTIFIER_INFO_TO_DEV
#ifndef netdev_notifier_info_to_dev
static inline struct net_device *
netdev_notifier_info_to_dev(void *ptr)
{
	return (struct net_device *)ptr;
}
#endif
#endif

static inline int bnxt_en_register_netdevice_notifier(struct notifier_block *nb)
{
	int rc;
#ifdef HAVE_REGISTER_NETDEVICE_NOTIFIER_RH
	rc = register_netdevice_notifier_rh(nb);
#else
	rc = register_netdevice_notifier(nb);
#endif
	return rc;
}

static inline int bnxt_en_unregister_netdevice_notifier(struct notifier_block *nb)
{
	int rc;
#ifdef HAVE_REGISTER_NETDEVICE_NOTIFIER_RH
	rc = unregister_netdevice_notifier_rh(nb);
#else
	rc = unregister_netdevice_notifier(nb);
#endif
	return rc;
}

#ifndef HAVE_NETDEV_UPDATE_FEATURES
static inline void netdev_update_features(struct net_device *dev)
{
	/* Do nothing, since we can't set default VLAN on these old kernels. */
}
#endif

#if !defined(netdev_printk) && (LINUX_VERSION_CODE < 0x020624)

#ifndef HAVE_NETDEV_NAME
static inline const char *netdev_name(const struct net_device *dev)
{
	if (dev->reg_state != NETREG_REGISTERED)
		return "(unregistered net_device)";
	return dev->name;
}
#endif

#define NET_PARENT_DEV(netdev)  ((netdev)->dev.parent)

#define netdev_printk(level, netdev, format, args...)		\
	dev_printk(level, NET_PARENT_DEV(netdev),		\
		   "%s: " format,				\
		   netdev_name(netdev), ##args)

#endif

#ifndef netdev_err
#define netdev_err(dev, format, args...)			\
	netdev_printk(KERN_ERR, dev, format, ##args)
#endif

#ifndef netdev_info
#define netdev_info(dev, format, args...)			\
	netdev_printk(KERN_INFO, dev, format, ##args)
#endif

#ifndef netdev_warn
#define netdev_warn(dev, format, args...)			\
	netdev_printk(KERN_WARNING, dev, format, ##args)
#endif

#ifndef dev_warn_ratelimited
#define dev_warn_ratelimited(dev, format, args...)		\
	dev_warn(dev, format, ##args)
#endif

#ifndef netdev_level_once
#define netdev_level_once(level, dev, fmt, ...)			\
do {								\
	static bool __print_once __read_mostly;			\
								\
	if (!__print_once) {					\
		__print_once = true;				\
		netdev_printk(level, dev, fmt, ##__VA_ARGS__);	\
	}							\
} while (0)

#define netdev_info_once(dev, fmt, ...) \
	netdev_level_once(KERN_INFO, dev, fmt, ##__VA_ARGS__)
#define netdev_warn_once(dev, fmt, ...) \
	netdev_level_once(KERN_WARNING, dev, fmt, ##__VA_ARGS__)
#endif

#ifndef netdev_uc_count
#define netdev_uc_count(dev)	((dev)->uc.count)
#endif

#ifndef netdev_for_each_uc_addr
#define netdev_for_each_uc_addr(ha, dev) \
	list_for_each_entry(ha, &dev->uc.list, list)
#endif

#ifndef netdev_for_each_mc_addr
#define netdev_for_each_mc_addr(mclist, dev) \
	for (mclist = dev->mc_list; mclist; mclist = mclist->next)
#endif

#ifndef smp_mb__before_atomic
#define smp_mb__before_atomic()	smp_mb()
#endif

#ifndef smp_mb__after_atomic
#define smp_mb__after_atomic()	smp_mb()
#endif

#ifndef dma_rmb
#define dma_rmb() rmb()
#endif

#ifndef writel_relaxed
#define writel_relaxed(v, a)	writel(v, a)
#endif

#ifndef writeq_relaxed
#define writeq_relaxed(v, a)	writeq(v, a)
#endif

#ifndef WRITE_ONCE
#define WRITE_ONCE(x, val)	(x = val)
#endif

#ifndef READ_ONCE
#define READ_ONCE(val)		(val)
#endif

#ifdef CONFIG_NET_RX_BUSY_POLL
#include <net/busy_poll.h>
#if defined(HAVE_NAPI_HASH_ADD) && defined(NETDEV_BUSY_POLL)
#define BNXT_PRIV_RX_BUSY_POLL	1
#endif
#endif

#if defined(HAVE_NETPOLL_POLL_DEV)
#undef CONFIG_NET_POLL_CONTROLLER
#endif

#if !defined(CONFIG_PTP_1588_CLOCK) && !defined(CONFIG_PTP_1588_CLOCK_MODULE)
#undef HAVE_IEEE1588_SUPPORT
#endif

#if !defined(HAVE_PTP_GETTIMEX64)
struct ptp_system_timestamp {
	struct timespec64 pre_ts;
	struct timespec64 post_ts;
};

static inline void ptp_read_system_prets(struct ptp_system_timestamp *sts)
{
}

static inline void ptp_read_system_postts(struct ptp_system_timestamp *sts)
{
}
#endif

#if defined(RHEL_RELEASE_CODE) && (RHEL_RELEASE_CODE == RHEL_RELEASE_VERSION(7,0))
#undef CONFIG_CRASH_DUMP
#endif

#ifndef skb_vlan_tag_present
#define skb_vlan_tag_present(skb) vlan_tx_tag_present(skb)
#define skb_vlan_tag_get(skb) vlan_tx_tag_get(skb)
#endif

#if !defined(HAVE_NAPI_HASH_DEL)
static inline void napi_hash_del(struct napi_struct *napi)
{
}
#endif

#if !defined(LL_FLUSH_FAILED) || !defined(HAVE_NAPI_HASH_ADD)
static inline void napi_hash_add(struct napi_struct *napi)
{
}
#endif

#ifndef HAVE_SET_COHERENT_MASK
static inline int dma_set_coherent_mask(struct device *dev, u64 mask)
{
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);

	return pci_set_consistent_dma_mask(pdev, mask);
}
#endif

#ifndef HAVE_SET_MASK_AND_COHERENT
static inline int dma_set_mask_and_coherent(struct device *dev, u64 mask)
{
	int rc = dma_set_mask(dev, mask);
	if (rc == 0)
		dma_set_coherent_mask(dev, mask);
	return rc;
}
#endif

#ifndef HAVE_DMA_ZALLOC_COHERENT
static inline void *dma_zalloc_coherent(struct device *dev, size_t size,
					dma_addr_t *dma_handle, gfp_t flag)
{
	void *ret = dma_alloc_coherent(dev, size, dma_handle,
				       flag | __GFP_ZERO);
	return ret;
}
#endif

#ifndef HAVE_IFLA_TX_RATE
#define ndo_set_vf_rate ndo_set_vf_tx_rate
#endif

#ifndef HAVE_PRANDOM_BYTES
#define prandom_bytes get_random_bytes
#endif

#ifndef rounddown
#define rounddown(x, y) (				\
{							\
	typeof(x) __x = (x);				\
	__x - (__x % (y));				\
}							\
)
#endif

#ifdef NO_SKB_FRAG_SIZE
static inline unsigned int skb_frag_size(const skb_frag_t *frag)
{
	return frag->size;
}
#endif

#ifdef NO_ETH_RESET_AP
#define ETH_RESET_AP (1<<8)
#endif

#ifndef HAVE_SKB_CHECKSUM_NONE_ASSERT
static inline void skb_checksum_none_assert(struct sk_buff *skb)
{
	skb->ip_summed = CHECKSUM_NONE;
}
#endif

#ifndef HAVE_NEW_FLOW_DISSECTOR_WITH_FLAGS
#define skb_flow_dissect_flow_keys(skb, fkeys, flags)	\
	skb_flow_dissect_flow_keys(skb, fkeys)
#endif

#ifndef HAVE_ETHER_ADDR_EQUAL
static inline bool ether_addr_equal(const u8 *addr1, const u8 *addr2)
{
	return !compare_ether_addr(addr1, addr2);
}
#endif

#ifndef HAVE_ETHER_ADDR_COPY
static inline void ether_addr_copy(u8 *dst, const u8 *src)
{
	memcpy(dst, src, ETH_ALEN);
}
#endif

#ifndef HAVE_ETH_BROADCAST_ADDR
static inline void eth_broadcast_addr(u8 *addr)
{
	memset(addr, 0xff, ETH_ALEN);
}
#endif

#ifndef HAVE_ETH_HW_ADDR_RANDOM
static inline void eth_hw_addr_random(struct net_device *dev)
{
#if defined(NET_ADDR_RANDOM)
	dev->addr_assign_type = NET_ADDR_RANDOM;
#endif
	random_ether_addr(dev->dev_addr);
}
#endif

#ifndef HAVE_NETDEV_TX_QUEUE_CTRL
static inline void netdev_tx_sent_queue(struct netdev_queue *dev_queue,
				unsigned int bytes)
{
}

static inline void netdev_tx_completed_queue(struct netdev_queue *dev_queue,
				unsigned int pkts, unsigned int bytes)
{
}

static inline void netdev_tx_reset_queue(struct netdev_queue *q)
{
}
#endif

#ifndef HAVE_NETIF_SET_REAL_NUM_RX
static inline int netif_set_real_num_rx_queues(struct net_device *dev,
				unsigned int rxq)
{
	return 0;
}
#endif

#ifndef HAVE_NETIF_SET_REAL_NUM_TX
static inline void netif_set_real_num_tx_queues(struct net_device *dev,
						unsigned int txq)
{
	dev->real_num_tx_queues = txq;
}
#endif

#ifndef HAVE_NETIF_GET_DEFAULT_RSS
static inline int netif_get_num_default_rss_queues(void)
{
	return min_t(int, 8, num_online_cpus());
}
#endif

#ifndef IFF_RXFH_CONFIGURED
#define IFF_RXFH_CONFIGURED	0
#undef HAVE_SET_RXFH
static inline bool netif_is_rxfh_configured(const struct net_device *dev)
{
	return false;
}
#endif

#if !defined(HAVE_TCP_V6_CHECK)
static __inline__ __sum16 tcp_v6_check(int len,
				const struct in6_addr *saddr,
				const struct in6_addr *daddr,
				__wsum base)
{
	return csum_ipv6_magic(saddr, daddr, len, IPPROTO_TCP, base);
}
#endif

#ifndef HAVE_USLEEP_RANGE
static inline void usleep_range(unsigned long min, unsigned long max)
{
	if (min < 1000)
		udelay(min);
	else
		msleep(min / 1000);
}
#endif

#ifndef HAVE_GET_NUM_TC
static inline int netdev_get_num_tc(struct net_device *dev)
{
	return 0;
}

static inline void netdev_reset_tc(struct net_device *dev)
{
}

static inline int netdev_set_tc_queue(struct net_device *devi, u8 tc,
				      u16 count, u16 offset)
{
	return 0;
}
#endif

#ifndef HAVE_VZALLOC
static inline void *vzalloc(size_t size)
{
	void *ret = vmalloc(size);
	if (ret)
		memset(ret, 0, size);
	return ret;
}
#endif

#ifndef HAVE_KMALLOC_ARRAY
static inline void *kmalloc_array(unsigned n, size_t s, gfp_t gfp)
{
	return kmalloc(n * s, gfp);
}
#endif

#ifndef ETH_MODULE_SFF_8436
#define ETH_MODULE_SFF_8436             0x4
#endif

#ifndef ETH_MODULE_SFF_8436_LEN
#define ETH_MODULE_SFF_8436_LEN         256
#endif

#ifndef ETH_MODULE_SFF_8636
#define ETH_MODULE_SFF_8636             0x3
#endif

#ifndef ETH_MODULE_SFF_8636_LEN
#define ETH_MODULE_SFF_8636_LEN         256
#endif

#ifndef HAVE_MSIX_RANGE
static inline int
pci_enable_msix_range(struct pci_dev *dev, struct msix_entry *entries,
		      int minvec, int maxvec)
{
	int rc;

	while (maxvec >= minvec) {
		rc = pci_enable_msix(dev, entries, maxvec);
		if (!rc || rc < 0)
			return rc;
		maxvec = rc;
	}
}
#endif /* HAVE_MSIX_RANGE */

#ifndef HAVE_PCI_PHYSFN
static inline struct pci_dev *pci_physfn(struct pci_dev *dev)
{
#ifdef CONFIG_PCI_IOV
	if (dev->is_virtfn)
		dev = dev->physfn;
#endif

	return dev;
}
#endif

#ifndef HAVE_PCI_PRINT_LINK_STATUS
#ifndef HAVE_PCI_LINK_WIDTH
enum pcie_link_width {
	PCIE_LNK_WIDTH_UNKNOWN		= 0xFF,
};
#endif

#ifndef HAVE_PCIE_BUS_SPEED
enum pci_bus_speed {
	PCIE_SPEED_2_5GT		= 0x14,
	PCIE_SPEED_5_0GT		= 0x15,
	PCIE_SPEED_8_0GT		= 0x16,
#ifndef PCIE_SPEED_16_0GT
	PCIE_SPEED_16_0GT		= 0x17,
#endif
	PCI_SPEED_UNKNOWN		= 0xFF,
};
#endif

#ifndef PCIE_SPEED_16_0GT
#define PCIE_SPEED_16_0GT	0x17
#endif

static const unsigned char pcie_link_speed[] = {
	PCI_SPEED_UNKNOWN,		/* 0 */
	PCIE_SPEED_2_5GT,		/* 1 */
	PCIE_SPEED_5_0GT,		/* 2 */
	PCIE_SPEED_8_0GT,		/* 3 */
	PCIE_SPEED_16_0GT,		/* 4 */
	PCI_SPEED_UNKNOWN,		/* 5 */
	PCI_SPEED_UNKNOWN,		/* 6 */
	PCI_SPEED_UNKNOWN,		/* 7 */
	PCI_SPEED_UNKNOWN,		/* 8 */
	PCI_SPEED_UNKNOWN,		/* 9 */
	PCI_SPEED_UNKNOWN,		/* A */
	PCI_SPEED_UNKNOWN,		/* B */
	PCI_SPEED_UNKNOWN,		/* C */
	PCI_SPEED_UNKNOWN,		/* D */
	PCI_SPEED_UNKNOWN,		/* E */
	PCI_SPEED_UNKNOWN		/* F */
};

#ifndef PCI_EXP_LNKSTA_NLW_SHIFT
#define PCI_EXP_LNKSTA_NLW_SHIFT	4
#endif

#ifndef PCI_EXP_LNKCAP2
#define PCI_EXP_LNKCAP2		44	/* Link Capabilities 2 */
#endif

#ifndef PCI_EXP_LNKCAP2_SLS_2_5GB
#define PCI_EXP_LNKCAP2_SLS_2_5GB	0x00000002 /* Supported Speed 2.5GT/s */
#define PCI_EXP_LNKCAP2_SLS_5_0GB	0x00000004 /* Supported Speed 5GT/s */
#define PCI_EXP_LNKCAP2_SLS_8_0GB	0x00000008 /* Supported Speed 8GT/s */
#endif

#ifndef PCI_EXP_LNKCAP2_SLS_16_0GB
#define PCI_EXP_LNKCAP2_SLS_16_0GB	0x00000010 /* Supported Speed 16GT/s */
#endif

#ifndef PCI_EXP_LNKCAP_SLS_2_5GB
#define PCI_EXP_LNKCAP_SLS_2_5GB	0x00000001 /* LNKCAP2 SLS Vector bit 0*/
#define PCI_EXP_LNKCAP_SLS_5_0GB	0x00000002 /* LNKCAP2 SLS Vector bit 1*/
#endif

#ifndef PCI_EXP_LNKCAP_SLS_8_0GB
#define PCI_EXP_LNKCAP_SLS_8_0GB	0x00000003 /* LNKCAP2 SLS Vector bit2 */
#endif

#ifndef PCI_EXP_LNKCAP_SLS_16_0GB
#define PCI_EXP_LNKCAP_SLS_16_0GB	0x00000004 /* LNKCAP2 SLS Vector bit 3 */
#endif

#ifndef PCIE_SPEED2STR
/* PCIe link information */
#define PCIE_SPEED2STR(speed) \
	((speed) == PCIE_SPEED_16_0GT ? "16 GT/s" : \
	 (speed) == PCIE_SPEED_8_0GT ? "8 GT/s" : \
	 (speed) == PCIE_SPEED_5_0GT ? "5 GT/s" : \
	 (speed) == PCIE_SPEED_2_5GT ? "2.5 GT/s" : \
	 "Unknown speed")

/* PCIe speed to Mb/s reduced by encoding overhead */
#define PCIE_SPEED2MBS_ENC(speed) \
	((speed) == PCIE_SPEED_16_0GT ? 16000 * 128 / 130 : \
	 (speed) == PCIE_SPEED_8_0GT  ?  8000 * 128 / 130 : \
	 (speed) == PCIE_SPEED_5_0GT  ?  5000 * 8 / 10 : \
	 (speed) == PCIE_SPEED_2_5GT  ?  2500 * 8 / 10 : \
	 0)
#endif /* PCIE_SPEED2STR */

#define BNXT_PCIE_CAP		0xAC
#ifndef HAVE_PCI_UPSTREAM_BRIDGE
static inline struct pci_dev *pci_upstream_bridge(struct pci_dev *dev)
{
	dev = pci_physfn(dev);
	if (pci_is_root_bus(dev->bus))
		return NULL;

	return dev->bus->self;
}
#endif

static u32
pcie_bandwidth_available(struct pci_dev *dev, struct pci_dev **limiting_dev,
			 enum pci_bus_speed *speed, enum pcie_link_width *width)
{
	enum pcie_link_width next_width;
	enum pci_bus_speed next_speed;
	u32 bw, next_bw;
	u16 lnksta;

	if (speed)
		*speed = PCI_SPEED_UNKNOWN;
	if (width)
		*width = PCIE_LNK_WIDTH_UNKNOWN;

	bw = 0;
#ifdef HAVE_PCIE_CAPABILITY_READ_WORD
	while (dev) {
		pcie_capability_read_word(dev, PCI_EXP_LNKSTA, &lnksta);

		next_speed = pcie_link_speed[lnksta & PCI_EXP_LNKSTA_CLS];
		next_width = (lnksta & PCI_EXP_LNKSTA_NLW) >>
			PCI_EXP_LNKSTA_NLW_SHIFT;

		next_bw = next_width * PCIE_SPEED2MBS_ENC(next_speed);

		/* Check if current device limits the total bandwidth */
		if (!bw || next_bw <= bw) {
			bw = next_bw;

			if (limiting_dev)
				*limiting_dev = dev;
			if (speed)
				*speed = next_speed;
			if (width)
				*width = next_width;
		}

		dev = pci_upstream_bridge(dev);
	}
#else
	pci_read_config_word(dev, BNXT_PCIE_CAP + PCI_EXP_LNKSTA, &lnksta);
	next_speed = pcie_link_speed[lnksta & PCI_EXP_LNKSTA_CLS];
	next_width = (lnksta & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT;
	next_bw = next_width * PCIE_SPEED2MBS_ENC(next_speed);

	if (limiting_dev)
		*limiting_dev = dev;
	if (speed)
		*speed = next_speed;
	if (width)
		*width = next_width;
#endif /* HAVE_PCIE_CAPABILITY_READ_WORD */

	return bw;
}

static enum pci_bus_speed pcie_get_speed_cap(struct pci_dev *dev)
{
	/*
	 * PCIe r4.0 sec 7.5.3.18 recommends using the Supported Link
	 * Speeds Vector in Link Capabilities 2 when supported, falling
	 * back to Max Link Speed in Link Capabilities otherwise.
	 */
#ifdef HAVE_PCIE_CAPABILITY_READ_WORD
	u32 lnkcap2, lnkcap;

	pcie_capability_read_dword(dev, PCI_EXP_LNKCAP2, &lnkcap2);
#else
	u16 lnkcap2, lnkcap;

	pci_read_config_word(dev, BNXT_PCIE_CAP + PCI_EXP_LNKCAP2, &lnkcap2);
#endif
	if (lnkcap2) { /* PCIe r3.0-compliant */
		if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_16_0GB)
			return PCIE_SPEED_16_0GT;
		else if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_8_0GB)
			return PCIE_SPEED_8_0GT;
		else if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_5_0GB)
			return PCIE_SPEED_5_0GT;
		else if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_2_5GB)
			return PCIE_SPEED_2_5GT;
		return PCI_SPEED_UNKNOWN;
	}

#ifdef HAVE_PCIE_CAPABILITY_READ_WORD
	pcie_capability_read_dword(dev, PCI_EXP_LNKCAP, &lnkcap);
#else
	pci_read_config_word(dev, BNXT_PCIE_CAP + PCI_EXP_LNKCAP, &lnkcap);
#endif
	if (lnkcap) {
		if (lnkcap & PCI_EXP_LNKCAP_SLS_16_0GB)
			return PCIE_SPEED_16_0GT;
		else if (lnkcap & PCI_EXP_LNKCAP_SLS_8_0GB)
			return PCIE_SPEED_8_0GT;
		else if (lnkcap & PCI_EXP_LNKCAP_SLS_5_0GB)
			return PCIE_SPEED_5_0GT;
		else if (lnkcap & PCI_EXP_LNKCAP_SLS_2_5GB)
			return PCIE_SPEED_2_5GT;
	}

	return PCI_SPEED_UNKNOWN;
}

static enum pcie_link_width pcie_get_width_cap(struct pci_dev *dev)
{
#ifdef HAVE_PCIE_CAPABILITY_READ_WORD
	u32 lnkcap;

	pcie_capability_read_dword(dev, PCI_EXP_LNKCAP, &lnkcap);
#else
	u16 lnkcap;

	pci_read_config_word(dev, BNXT_PCIE_CAP + PCI_EXP_LNKCAP, &lnkcap);
#endif
	if (lnkcap)
		return (lnkcap & PCI_EXP_LNKCAP_MLW) >> 4;

	return PCIE_LNK_WIDTH_UNKNOWN;
}

static u32
pcie_bandwidth_capable(struct pci_dev *dev, enum pci_bus_speed *speed,
		       enum pcie_link_width *width)
{
	*speed = pcie_get_speed_cap(dev);
	*width = pcie_get_width_cap(dev);

	if (*speed == PCI_SPEED_UNKNOWN || *width == PCIE_LNK_WIDTH_UNKNOWN)
		return 0;

	return *width * PCIE_SPEED2MBS_ENC(*speed);
}

static inline void pcie_print_link_status(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	enum pcie_link_width width, width_cap;
	enum pci_bus_speed speed, speed_cap;
	struct pci_dev *limiting_dev = NULL;
	u32 bw_avail, bw_cap;

	bw_cap = pcie_bandwidth_capable(pdev, &speed_cap, &width_cap);
	bw_avail = pcie_bandwidth_available(pdev, &limiting_dev, &speed,
					    &width);

	if (bw_avail >= bw_cap)
		netdev_info(dev, "%u.%03u Gb/s available PCIe bandwidth (%s x%d link)\n",
			    bw_cap / 1000, bw_cap % 1000,
			    PCIE_SPEED2STR(speed_cap), width_cap);
	else
		netdev_info(dev, "%u.%03u Gb/s available PCIe bandwidth, limited by %s x%d link at %s (capable of %u.%03u Gb/s with %s x%d link)\n",
			    bw_avail / 1000, bw_avail % 1000,
			    PCIE_SPEED2STR(speed), width,
			    limiting_dev ? pci_name(limiting_dev) : "<unknown>",
			    bw_cap / 1000, bw_cap % 1000,
			    PCIE_SPEED2STR(speed_cap), width_cap);
}
#endif /* HAVE_PCI_PRINT_LINK_STATUS */

#ifndef HAVE_PCI_IS_BRIDGE
static inline bool pci_is_bridge(struct pci_dev *dev)
{
	return dev->hdr_type == PCI_HEADER_TYPE_BRIDGE ||
		dev->hdr_type == PCI_HEADER_TYPE_CARDBUS;
}
#endif

#ifndef HAVE_PCI_GET_DSN
static inline u64 pci_get_dsn(struct pci_dev *dev)
{
	u32 dword;
	u64 dsn;
	int pos;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_DSN);
	if (!pos)
		return 0;

	/*
	 * The Device Serial Number is two dwords offset 4 bytes from the
	 * capability position. The specification says that the first dword is
	 * the lower half, and the second dword is the upper half.
	 */
	pos += 4;
	pci_read_config_dword(dev, pos, &dword);
	dsn = (u64)dword;
	pci_read_config_dword(dev, pos + 4, &dword);
	dsn |= ((u64)dword) << 32;

	return dsn;
}
#endif

#ifndef HAVE_NDO_XDP
struct netdev_bpf;
struct xdp_buff;
#elif !defined(HAVE_NDO_BPF)
#define ndo_bpf		ndo_xdp
#define netdev_bpf	netdev_xdp
#endif

#ifndef XDP_PACKET_HEADROOM
#define XDP_PACKET_HEADROOM	0
#endif

#ifndef HAVE_XDP_FRAME
#define xdp_do_flush_map()
#ifndef HAVE_XDP_REDIRECT
struct bpf_prog;
static inline int xdp_do_redirect(struct net_device *dev, struct xdp_buff *xdp,
				  struct bpf_prog *prog)
{
	return 0;
}
#endif
#endif

#ifndef HAVE_XDP_ACTION
enum xdp_action {
        XDP_ABORTED = 0,
        XDP_DROP,
        XDP_PASS,
        XDP_TX,
#ifndef HAVE_XDP_REDIRECT
        XDP_REDIRECT,
#endif
};
#else
#ifndef HAVE_XDP_REDIRECT
#define XDP_REDIRECT	4
#endif
#endif

#ifndef HAVE_BPF_TRACE
#define trace_xdp_exception(dev, xdp_prog, act)
#define bpf_warn_invalid_xdp_action(act)
#endif

#ifndef HAVE_XDP_SET_DATA_META_INVALID
#define xdp_set_data_meta_invalid(xdp)
#endif

#ifdef HAVE_XDP_RXQ_INFO
#ifndef HAVE_XDP_RXQ_INFO_IS_REG

#define REG_STATE_REGISTERED	0x1

static inline bool xdp_rxq_info_is_reg(struct xdp_rxq_info *xdp_rxq)
{
	return (xdp_rxq->reg_state == REG_STATE_REGISTERED);
}
#endif
#else
struct xdp_rxq_info {
	struct net_device *dev;
	u32 queue_index;
	u32 reg_state;
};
#endif

#ifndef HAVE_XDP_MEM_TYPE
enum xdp_mem_type {
	MEM_TYPE_PAGE_SHARED = 0, /* Split-page refcnt based model */
	MEM_TYPE_PAGE_ORDER0,     /* Orig XDP full page model */
	MEM_TYPE_PAGE_POOL,
	MEM_TYPE_ZERO_COPY,
	MEM_TYPE_MAX,
};
static inline int xdp_rxq_info_reg_mem_model(struct xdp_rxq_info *xdp_rxq,
			       enum xdp_mem_type type, void *allocator)
{
	return 0;
}
#endif

#if !defined(CONFIG_PAGE_POOL) || !defined(HAVE_PAGE_POOL_RELEASE_PAGE)
#define page_pool_release_page(page_pool, page)
#endif

#ifndef HAVE_TCF_EXTS_HAS_ACTIONS
#define tcf_exts_has_actions(x)			(!tc_no_actions(x))
#endif

#if defined(CONFIG_BNXT_FLOWER_OFFLOAD) && !defined(HAVE_TCF_STATS_UPDATE)
static inline void
tcf_exts_stats_update(const struct tcf_exts *exts,
		      u64 bytes, u64 packets, u64 lastuse)
{
#ifdef CONFIG_NET_CLS_ACT
	int i;

	preempt_disable();

	for (i = 0; i < exts->nr_actions; i++) {
		struct tc_action *a = exts->actions[i];

		tcf_action_stats_update(a, bytes, packets, lastuse);
	}

	preempt_enable();
#endif
}
#endif

#ifndef HAVE_TC_CB_REG_EXTACK
#define tcf_block_cb_register(block, cb, cb_ident, cb_priv, extack)	\
	tcf_block_cb_register(block, cb, cb_ident, cb_priv)
#endif

#ifdef CONFIG_BNXT_FLOWER_OFFLOAD
#if !defined(HAVE_TC_CLS_CAN_OFFLOAD_AND_CHAIN0) && defined(HAVE_TC_SETUP_BLOCK)
static inline bool
tc_cls_can_offload_and_chain0(const struct net_device *dev,
			      struct tc_cls_common_offload *common)
{
	if (!tc_can_offload(dev))
		return false;
	if (common->chain_index)
		return false;
	return true;
}
#endif

#ifdef HAVE_TC_CB_EGDEV

static inline void bnxt_reg_egdev(const struct net_device *dev,
				  void *cb, void *cb_priv, int vf_idx)
{
	if (tc_setup_cb_egdev_register(dev, (tc_setup_cb_t *)cb, cb_priv))
		netdev_warn(dev,
			    "Failed to register egdev for VF-Rep: %d", vf_idx);
}

static inline void bnxt_unreg_egdev(const struct net_device *dev,
				    void *cb, void *cb_priv)
{
	tc_setup_cb_egdev_unregister(dev, (tc_setup_cb_t *)cb, cb_priv);
}

#else

static inline void bnxt_reg_egdev(const struct net_device *dev,
				  void *cb, void *cb_priv, int vf_idx)
{
}

static inline void bnxt_unreg_egdev(const struct net_device *dev,
				    void *cb, void *cb_priv)
{
}

#endif /* HAVE_TC_CB_EGDEV */

#ifdef HAVE_TC_SETUP_BLOCK
#ifndef HAVE_SETUP_TC_BLOCK_HELPER

static inline int
flow_block_cb_setup_simple(struct tc_block_offload *f,
			   struct list_head *driver_block_list,
			   tc_setup_cb_t *cb, void *cb_ident, void *cb_priv,
			   bool ingress_only)
{
	if (ingress_only &&
	    f->binder_type != TCF_BLOCK_BINDER_TYPE_CLSACT_INGRESS)
		return -EOPNOTSUPP;

	switch (f->command) {
	case TC_BLOCK_BIND:
		return tcf_block_cb_register(f->block, cb, cb_ident, cb_priv,
					     f->extack);
	case TC_BLOCK_UNBIND:
		tcf_block_cb_unregister(f->block, cb, cb_ident);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

#endif /* !HAVE_SETUP_TC_BLOCK_HELPER */
#endif /* HAVE_TC_SETUP_BLOCK */
#endif /* CONFIG_BNXT_FLOWER_OFFLOAD */

#ifndef BIT_ULL
#define BIT_ULL(nr)		(1ULL << (nr))
#endif

#ifndef HAVE_SIMPLE_OPEN
static inline int simple_open(struct inode *inode, struct file *file)
{
	if (inode->i_private)
		file->private_data = inode->i_private;
	return 0;
}
#endif

#if !defined(HAVE_DEVLINK_PARAM_PUBLISH) && defined(HAVE_DEVLINK_PARAM)
static inline void devlink_params_publish(struct devlink *devlink)
{
}
#endif

#ifdef HAVE_DEVLINK_HEALTH_REPORT
#ifndef HAVE_DEVLINK_HEALTH_REPORTER_STATE_UPDATE

#define DEVLINK_HEALTH_REPORTER_STATE_HEALTHY	0
#define DEVLINK_HEALTH_REPORTER_STATE_ERROR	1

static inline void
devlink_health_reporter_state_update(struct devlink_health_reporter *reporter,
				     int state)
{
}
#endif

#ifndef HAVE_DEVLINK_HEALTH_REPORTER_RECOVERY_DONE
static inline void
devlink_health_reporter_recovery_done(struct devlink_health_reporter *reporter)
{
}
#endif

#ifndef HAVE_DEVLINK_HEALTH_REPORT_EXTACK
#define bnxt_fw_reporter_diagnose(reporter, priv_ctx, extack)	\
	bnxt_fw_reporter_diagnose(reporter, priv_ctx)
#define bnxt_fw_reset_recover(reporter, priv_ctx, extack)	\
	bnxt_fw_reset_recover(reporter, priv_ctx)
#define bnxt_fw_fatal_recover(reporter, priv_ctx, extack)	\
	bnxt_fw_fatal_recover(reporter, priv_ctx)
#endif /* HAVE_DEVLINK_HEALTH_REPORT_EXTACK */
#endif /* HAVE_DEVLINK_HEALTH_REPORT */

#ifndef mmiowb
#define mmiowb() do {} while (0)
#endif

#ifndef HAVE_ETH_GET_HEADLEN_NEW
#define eth_get_headlen(dev, data, len)	eth_get_headlen(data, len)
#endif

#ifndef HAVE_NETDEV_XMIT_MORE
#define netdev_xmit_more()	skb->xmit_more
#endif

#ifndef HAVE_NDO_TX_TIMEOUT_QUEUE
#define bnxt_tx_timeout(dev, queue)	bnxt_tx_timeout(dev)
#endif

#ifdef HAVE_NDO_ADD_VXLAN
#ifndef HAVE_VXLAN_GET_RX_PORT
static inline void vxlan_get_rx_port(struct net_device *netdev)
{
}
#endif
#endif

#ifndef HAVE_DEVLINK_VALIDATE_NEW
#define bnxt_dl_msix_validate(dl, id, val, extack)	\
	bnxt_dl_msix_validate(dl, id, val)
#endif

#ifndef kfree_rcu
#define kfree_rcu(ptr, rcu_head)			\
	do {						\
		synchronize_rcu();			\
		kfree(ptr);				\
	} while (0)
#endif

#if defined(HAVE_DEVLINK_FLASH_UPDATE) &&	\
	!defined(HAVE_DEVLINK_FLASH_UPDATE_STATUS)
static inline void devlink_flash_update_begin_notify(struct devlink *devlink)
{
}

static inline void devlink_flash_update_end_notify(struct devlink *devlink)
{
}

static inline void devlink_flash_update_status_notify(struct devlink *devlink,
						      const char *status_msg,
						      const char *component,
						      unsigned long done,
						      unsigned long total)
{
}
#endif

#ifdef HAVE_DEVLINK_INFO
#ifndef DEVLINK_INFO_VERSION_GENERIC_ASIC_ID
#define DEVLINK_INFO_VERSION_GENERIC_ASIC_ID	"asic.id"
#define DEVLINK_INFO_VERSION_GENERIC_ASIC_REV	"asic.rev"
#define DEVLINK_INFO_VERSION_GENERIC_FW		"fw"
#endif
#ifndef DEVLINK_INFO_VERSION_GENERIC_FW_PSID
#define DEVLINK_INFO_VERSION_GENERIC_FW_PSID	"fw.psid"
#endif
#ifndef DEVLINK_INFO_VERSION_GENERIC_FW_ROCE
#define DEVLINK_INFO_VERSION_GENERIC_FW_ROCE	"fw.roce"
#endif
#ifndef DEVLINK_INFO_VERSION_GENERIC_FW_MGMT_API
#define DEVLINK_INFO_VERSION_GENERIC_FW_MGMT_API	"fw.mgmt.api"
#endif

#ifndef HAVE_DEVLINK_INFO_BSN_PUT
static inline int devlink_info_board_serial_number_put(struct devlink_info_req *req,
						       const char *bsn)
{
	return 0;
}
#endif
#endif /* HAVE_DEVLINK_INFO */

#ifndef HAVE_PCIE_FLR
static inline int pcie_flr(struct pci_dev *dev)
{
	pcie_capability_set_word(dev, PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_BCR_FLR);

	msleep(100);

	return 0;
}
#endif /* pcie_flr */

#if defined(HAVE_FLOW_OFFLOAD_H) &&	\
	!defined(HAVE_FLOW_ACTION_BASIC_HW_STATS_CHECK)
static inline bool
flow_action_basic_hw_stats_check(const struct flow_action *action,
				 struct netlink_ext_ack *extack)
{
	return true;
}

#define flow_stats_update(flow_stats, bytes, pkts, last_used, used_hw_stats) \
	flow_stats_update(flow_stats, bytes, pkts, last_used)
#endif

#ifndef fallthrough
#define fallthrough do {} while (0)  /* fall through */
#endif

#ifndef __ALIGN_KERNEL_MASK
#define __ALIGN_KERNEL_MASK(x, mask)	(((x) + (mask)) & ~(mask))
#endif
#ifndef __ALIGN_KERNEL
#define __ALIGN_KERNEL(x, a)	__ALIGN_KERNEL_MASK(x, (typeof(x))(a) - 1)
#endif
#ifndef ALIGN_DOWN
#define ALIGN_DOWN(x, a)	__ALIGN_KERNEL((x) - ((a) - 1), (a))
#endif

struct bnxt_compat_dma_pool {
	struct dma_pool *pool;
	size_t size;
};

static inline struct bnxt_compat_dma_pool*
bnxt_compat_dma_pool_create(const char *name, struct device *dev, size_t size,
			    size_t align, size_t allocation)
{
	struct bnxt_compat_dma_pool *wrapper;

	wrapper = kmalloc_node(sizeof(*wrapper), GFP_KERNEL, dev_to_node(dev));
	if (!wrapper)
		return NULL;

	wrapper->pool = dma_pool_create(name, dev, size, align, allocation);
	wrapper->size = size;

	return wrapper;
}

static inline void
bnxt_compat_dma_pool_destroy(struct bnxt_compat_dma_pool *wrapper)
{
	dma_pool_destroy(wrapper->pool);
	kfree(wrapper);
}

static inline void *
bnxt_compat_dma_pool_alloc(struct bnxt_compat_dma_pool *wrapper,
			   gfp_t mem_flags, dma_addr_t *handle)
{
	void *mem;

	mem = dma_pool_alloc(wrapper->pool, mem_flags & ~__GFP_ZERO, handle);
	if (mem_flags & __GFP_ZERO)
		memset(mem, 0, wrapper->size);
	return mem;
}

static inline void
bnxt_compat_dma_pool_free(struct bnxt_compat_dma_pool *wrapper, void *vaddr,
			  dma_addr_t addr)
{
	dma_pool_free(wrapper->pool, vaddr, addr);
}

#define dma_pool_create bnxt_compat_dma_pool_create
#define dma_pool_destroy bnxt_compat_dma_pool_destroy
#define dma_pool_alloc bnxt_compat_dma_pool_alloc
#define dma_pool_free bnxt_compat_dma_pool_free
#define dma_pool bnxt_compat_dma_pool
