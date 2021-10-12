/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2013, Intel Corporation. */

#ifndef _KCOMPAT_IMPL_H_
#define _KCOMPAT_IMPL_H_

/* This file contains implementations of backports from various kernels. It
 * must rely only on NEED_<FLAG> and HAVE_<FLAG> checks. It must not make any
 * checks to determine the kernel version when deciding whether to include an
 * implementation.
 *
 * All new implementations must go in this file, and legacy implementations
 * should be migrated to the new format over time.
 */

/*
 * generic network stack functions
 */

/* NEED_NET_PREFETCH
 *
 * net_prefetch was introduced by commit f468f21b7af0 ("net: Take common
 * prefetch code structure into a function")
 *
 * This function is trivial to re-implement in full.
 */
#ifdef NEED_NET_PREFETCH
static inline void net_prefetch(void *p)
{
	prefetch(p);
#if L1_CACHE_BYTES < 128
	prefetch((u8 *)p + L1_CACHE_BYTES);
#endif
}
#endif /* NEED_NET_PREFETCH */

/* NEED_SKB_FRAG_OFF_ACCESSORS
 *
 * skb_frag_off and skb_frag_off_add were added in upstream commit
 * 7240b60c98d6 ("linux: Add skb_frag_t page_offset accessors")
 *
 * Implementing the wrappers directly for older kernels which still have the
 * old implementation of skb_frag_t is trivial.
 */
#ifdef NEED_SKB_FRAG_OFF_ACCESSORS
static inline unsigned int skb_frag_off(const skb_frag_t *frag)
{
	return frag->page_offset;
}

static inline void skb_frag_off_add(skb_frag_t *frag, int delta)
{
	frag->page_offset += delta;
}
#endif

/*
 * NETIF_F_HW_L2FW_DOFFLOAD related functions
 *
 * Support for NETIF_F_HW_L2FW_DOFFLOAD was first introduced upstream by
 * commit a6cc0cfa72e0 ("net: Add layer 2 hardware acceleration operations for
 * macvlan devices")
 */
#ifdef NETIF_F_HW_L2FW_DOFFLOAD

#include <linux/if_macvlan.h>

/* NEED_MACVLAN_ACCEL_PRIV
 *
 * macvlan_accel_priv is an accessor function that replaced direct access to
 * the macvlan->fwd_priv variable. It was introduced in commit 7d775f63470c
 * ("macvlan: Rename fwd_priv to accel_priv and add accessor function")
 *
 * Implement the new wrapper name by simply accessing the older
 * macvlan->fwd_priv name.
 */
#ifdef NEED_MACVLAN_ACCEL_PRIV
static inline void *macvlan_accel_priv(struct net_device *dev)
{
	struct macvlan_dev *macvlan = netdev_priv(dev);

	return macvlan->fwd_priv;
}
#endif /* NEED_MACVLAN_ACCEL_PRIV */

/* NEED_MACVLAN_RELEASE_L2FW_OFFLOAD
 *
 * macvlan_release_l2fw_offload was introduced upstream by commit 53cd4d8e4dfb
 * ("macvlan: Provide function for interfaces to release HW offload")
 *
 * Implementing this is straight forward, but we must be careful to use
 * fwd_priv instead of accel_priv. Note that both the change to accel_priv and
 * introduction of this function happened in the same release.
 */
#ifdef NEED_MACVLAN_RELEASE_L2FW_OFFLOAD
static inline int macvlan_release_l2fw_offload(struct net_device *dev)
{
	struct macvlan_dev *macvlan = netdev_priv(dev);

	macvlan->fwd_priv = NULL;
	return dev_uc_add(macvlan->lowerdev, dev->dev_addr);
}
#endif /* NEED_MACVLAN_RELEASE_L2FW_OFFLOAD */

/* NEED_MACVLAN_SUPPORTS_DEST_FILTER
 *
 * macvlan_supports_dest_filter was introduced upstream by commit 6cb1937d4eff
 * ("macvlan: Add function to test for destination filtering support")
 *
 * The implementation doesn't rely on anything new and is trivial to backport
 * for kernels that have NETIF_F_HW_L2FW_DOFFLOAD support.
 */
#ifdef NEED_MACVLAN_SUPPORTS_DEST_FILTER
static inline bool macvlan_supports_dest_filter(struct net_device *dev)
{
	struct macvlan_dev *macvlan = netdev_priv(dev);

	return macvlan->mode == MACVLAN_MODE_PRIVATE ||
	       macvlan->mode == MACVLAN_MODE_VEPA ||
	       macvlan->mode == MACVLAN_MODE_BRIDGE;
}
#endif /* NEED_MACVLAN_SUPPORTS_DEST_FILTER */

#endif /* NETIF_F_HW_L2FW_DOFFLOAD */

/*
 * tc functions
 */

/* NEED_FLOW_INDR_BLOCK_CB_REGISTER
 *
 * __flow_indr_block_cb_register and __flow_indr_block_cb_unregister were
 * added in upstream commit 4e481908c51b ("flow_offload: move tc indirect
 * block to flow offload")
 *
 * This was a simple rename so we can just translate from the old
 * naming scheme with a macro.
 */
#ifdef NEED_FLOW_INDR_BLOCK_CB_REGISTER
#define __flow_indr_block_cb_register __tc_indr_block_cb_register
#define __flow_indr_block_cb_unregister __tc_indr_block_cb_unregister
#endif

/*
 * devlink support
 */
#if IS_ENABLED(CONFIG_NET_DEVLINK)

#include <net/devlink.h>

#ifdef HAVE_DEVLINK_REGIONS
/* NEED_DEVLINK_REGION_CREATE_OPS
 *
 * The ops parameter to devlink_region_create was added by commit e8937681797c
 * ("devlink: prepare to support region operations")
 *
 * For older kernels, define _kc_devlink_region_create that takes an ops
 * parameter, and calls the old implementation function by extracting the name
 * from the structure.
 */
#ifdef NEED_DEVLINK_REGION_CREATE_OPS
struct devlink_region_ops {
	const char *name;
	void (*destructor)(const void *data);
};

static inline struct devlink_region *
_kc_devlink_region_create(struct devlink *devlink,
			  const struct devlink_region_ops *ops,
			  u32 region_max_snapshots, u64 region_size)
{
	return devlink_region_create(devlink, ops->name, region_max_snapshots,
				     region_size);
}

#define devlink_region_create _kc_devlink_region_create
#endif /* NEED_DEVLINK_REGION_CREATE_OPS */
#endif /* HAVE_DEVLINK_REGIONS */

/* NEED_DEVLINK_FLASH_UPDATE_STATUS_NOTIFY
 *
 * devlink_flash_update_status_notify, _begin_notify, and _end_notify were
 * added by upstream commit 191ed2024de9 ("devlink: allow driver to update
 * progress of flash update")
 *
 * For older kernels that lack the netlink messages, convert the functions
 * into no-ops.
 */
#ifdef NEED_DEVLINK_FLASH_UPDATE_STATUS_NOTIFY
static inline void
devlink_flash_update_begin_notify(struct devlink __always_unused *devlink)
{
}

static inline void
devlink_flash_update_end_notify(struct devlink __always_unused *devlink)
{
}

static inline void
devlink_flash_update_status_notify(struct devlink __always_unused *devlink,
				   const char __always_unused *status_msg,
				   const char __always_unused *component,
				   unsigned long __always_unused done,
				   unsigned long __always_unused total)
{
}
#endif /* NEED_DEVLINK_FLASH_UPDATE_STATUS_NOTIFY */

/* NEED_DEVLINK_FLASH_UPDATE_TIMEOUT_NOTIFY
 *
 * devlink_flash_update_timeout_notify was added by upstream commit
 * f92970c694b3 ("devlink: add timeout information to status_notify").
 *
 * For older kernels, just convert timeout notifications into regular status
 * notification messages without timeout information.
 */
#ifdef NEED_DEVLINK_FLASH_UPDATE_TIMEOUT_NOTIFY
static inline void
devlink_flash_update_timeout_notify(struct devlink *devlink,
				    const char *status_msg,
				    const char *component,
				    unsigned long __always_unused timeout)
{
	devlink_flash_update_status_notify(devlink, status_msg, component, 0, 0);
}
#endif /* NEED_DEVLINK_FLASH_UPDATE_TIMEOUT_NOTIFY */

/*
 * NEED_DEVLINK_PORT_ATTRS_SET_STRUCT
 *
 * HAVE_DEVLINK_PORT_ATTRS_SET_PORT_FLAVOUR
 * HAVE_DEVLINK_PORT_ATTRS_SET_SWITCH_ID
 *
 * devlink_port_attrs_set was introduced by commit b9ffcbaf56d3 ("devlink:
 * introduce devlink_port_attrs_set")
 *
 * It's function signature has changed multiple times over several kernel
 * releases:
 *
 * commit 5ec1380a21bb ("devlink: extend attrs_set for setting port
 * flavours") added the ability to set port flavour. (Note that there is no
 * official kernel release with devlink_port_attrs_set without the flavour
 * argument, as they were introduced in the same series.)
 *
 * commit bec5267cded2 ("net: devlink: extend port attrs for switch ID") added
 * the ability to set the switch ID (HAVE_DEVLINK_PORT_ATTRS_SET_SWITCH_ID)
 *
 * Finally commit 71ad8d55f8e5 ("devlink: Replace devlink_port_attrs_set
 * parameters with a struct") refactored to pass devlink_port_attrs struct
 * instead of individual parameters. (!NEED_DEVLINK_PORT_ATTRS_SET_STRUCT)
 *
 * We want core drivers to just use the latest form that takes
 * a devlink_port_attrs structure. Note that this structure did exist as part
 * of <net/devlink.h> but was never used directly by driver code prior to the
 * function parameter change. For this reason, the implementation always
 * relies on _kc_devlink_port_attrs instead of what was defined in the kernel.
 */
#ifdef NEED_DEVLINK_PORT_ATTRS_SET_STRUCT

#ifndef HAVE_DEVLINK_PORT_ATTRS_SET_PORT_FLAVOUR
enum devlink_port_flavour {
	DEVLINK_PORT_FLAVOUR_PHYSICAL,
	DEVLINK_PORT_FLAVOUR_CPU,
	DEVLINK_PORT_FLAVOUR_DSA,
	DEVLINK_PORT_FLAVOUR_PCI_PF,
	DEVLINK_PORT_FLAVOUR_PCI_VF,
};
#endif

struct _kc_devlink_port_phys_attrs {
	u32 port_number;
	u32 split_subport_number;
};

struct _kc_devlink_port_pci_pf_attrs {
	u16 pf;
};

struct _kc_devlink_port_pci_vf_attrs {
	u16 pf;
	u16 vf;
};

struct _kc_devlink_port_attrs {
	u8 split:1,
	   splittable:1;
	u32 lanes;
	enum devlink_port_flavour flavour;
	struct netdev_phys_item_id switch_id;
	union {
		struct _kc_devlink_port_phys_attrs phys;
		struct _kc_devlink_port_pci_pf_attrs pci_pf;
		struct _kc_devlink_port_pci_vf_attrs pci_vf;
	};
};

#define devlink_port_attrs _kc_devlink_port_attrs

static inline void
_kc_devlink_port_attrs_set(struct devlink_port *devlink_port,
			   struct _kc_devlink_port_attrs *attrs)
{
#if defined(HAVE_DEVLINK_PORT_ATTRS_SET_SWITCH_ID)
	devlink_port_attrs_set(devlink_port, attrs->flavour, attrs->phys.port_number,
			       attrs->split, attrs->phys.split_subport_number,
			       attrs->switch_id.id, attrs->switch_id.id_len);
#elif defined(HAVE_DEVLINK_PORT_ATTRS_SET_PORT_FLAVOUR)
	devlink_port_attrs_set(devlink_port, attrs->flavour, attrs->phys.port_number,
			       attrs->split, attrs->phys.split_subport_number);
#else
	if (attrs->split)
		devlink_port_split_set(devlink_port, attrs->phys.port_number);
#endif
}

#define devlink_port_attrs_set _kc_devlink_port_attrs_set

#endif /* NEED_DEVLINK_PORT_ATTRS_SET_STRUCT */

#endif /* CONFIG_NET_DEVLINK */

/*
 * dev_printk implementations
 */

/* NEED_DEV_PRINTK_ONCE
 *
 * The dev_*_once family of printk functions was introduced by commit
 * e135303bd5be ("device: Add dev_<level>_once variants")
 *
 * The implementation is very straight forward so we will just implement them
 * as-is here.
 */
#ifdef NEED_DEV_PRINTK_ONCE
#ifdef CONFIG_PRINTK
#define dev_level_once(dev_level, dev, fmt, ...)			\
do {									\
	static bool __print_once __read_mostly;				\
									\
	if (!__print_once) {						\
		__print_once = true;					\
		dev_level(dev, fmt, ##__VA_ARGS__);			\
	}								\
} while (0)
#else
#define dev_level_once(dev_level, dev, fmt, ...)			\
do {									\
	if (0)								\
		dev_level(dev, fmt, ##__VA_ARGS__);			\
} while (0)
#endif

#define dev_emerg_once(dev, fmt, ...)					\
	dev_level_once(dev_emerg, dev, fmt, ##__VA_ARGS__)
#define dev_alert_once(dev, fmt, ...)					\
	dev_level_once(dev_alert, dev, fmt, ##__VA_ARGS__)
#define dev_crit_once(dev, fmt, ...)					\
	dev_level_once(dev_crit, dev, fmt, ##__VA_ARGS__)
#define dev_err_once(dev, fmt, ...)					\
	dev_level_once(dev_err, dev, fmt, ##__VA_ARGS__)
#define dev_warn_once(dev, fmt, ...)					\
	dev_level_once(dev_warn, dev, fmt, ##__VA_ARGS__)
#define dev_notice_once(dev, fmt, ...)					\
	dev_level_once(dev_notice, dev, fmt, ##__VA_ARGS__)
#define dev_info_once(dev, fmt, ...)					\
	dev_level_once(dev_info, dev, fmt, ##__VA_ARGS__)
#define dev_dbg_once(dev, fmt, ...)					\
	dev_level_once(dev_dbg, dev, fmt, ##__VA_ARGS__)
#endif /* NEED_DEV_PRINTK_ONCE */

#ifdef HAVE_TC_CB_AND_SETUP_QDISC_MQPRIO

/* NEED_TC_CLS_CAN_OFFLOAD_AND_CHAIN0
 *
 * tc_cls_can_offload_and_chain0 was added by upstream commit
 * 878db9f0f26d ("pkt_cls: add new tc cls helper to check offload flag and
 * chain index").
 *
 * This patch backports this function for older kernels by calling
 * tc_can_offload() directly.
 */
#ifdef NEED_TC_CLS_CAN_OFFLOAD_AND_CHAIN0
#include <net/pkt_cls.h>
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
#endif /* NEED_TC_CLS_CAN_OFFLOAD_AND_CHAIN0 */
#endif /* HAVE_TC_CB_AND_SETUP_QDISC_MQPRIO */

/* NEED_TC_SETUP_QDISC_MQPRIO
 *
 * TC_SETUP_QDISC_MQPRIO was added by upstream commit
 * 575ed7d39e2f ("net_sch: mqprio: Change TC_SETUP_MQPRIO to
 * TC_SETUP_QDISC_MQPRIO").
 *
 * For older kernels which are using TC_SETUP_MQPRIO
 */
#ifdef NEED_TC_SETUP_QDISC_MQPRIO
#define TC_SETUP_QDISC_MQPRIO TC_SETUP_MQPRIO
#endif /* NEED_TC_SETUP_QDISC_MQPRIO */

#endif /* _KCOMPAT_IMPL_H_ */
