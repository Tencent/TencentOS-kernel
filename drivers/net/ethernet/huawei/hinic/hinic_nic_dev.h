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

#ifndef HINIC_NIC_DEV_H
#define	HINIC_NIC_DEV_H

#include <linux/netdevice.h>
#include <linux/semaphore.h>
#include <linux/types.h>
#include <linux/bitops.h>

#include "ossl_knl.h"
#include "hinic_nic_io.h"
#include "hinic_nic_cfg.h"
#include "hinic_tx.h"
#include "hinic_rx.h"

#define HINIC_DRV_NAME		"hinic"
#define HINIC_CHIP_NAME		"hinic"

#define HINIC_DRV_VERSION	"2.3.2.14"
struct vf_data_storage;

#define HINIC_FUNC_IS_VF(hwdev)	(hinic_func_type(hwdev) == TYPE_VF)

enum hinic_flags {
	HINIC_INTF_UP,
	HINIC_MAC_FILTER_CHANGED,
	HINIC_LP_TEST,
	HINIC_RSS_ENABLE,
	HINIC_DCB_ENABLE,
	HINIC_BP_ENABLE,
	HINIC_SAME_RXTX,
	HINIC_INTR_ADAPT,
	HINIC_UPDATE_MAC_FILTER,
	HINIC_ETS_ENABLE,
};

#define RX_BUFF_NUM_PER_PAGE	2
#define HINIC_MAX_MAC_NUM	3
#define LP_PKT_CNT		64

struct hinic_mac_addr {
	u8 addr[ETH_ALEN];
	u16 state;
};

enum hinic_rx_mode_state {
	HINIC_HW_PROMISC_ON,
	HINIC_HW_ALLMULTI_ON,
	HINIC_PROMISC_FORCE_ON,
	HINIC_ALLMULTI_FORCE_ON,
};

enum mac_filter_state {
	HINIC_MAC_WAIT_HW_SYNC,
	HINIC_MAC_HW_SYNCED,
	HINIC_MAC_WAIT_HW_UNSYNC,
	HINIC_MAC_HW_UNSYNCED,
};

struct hinic_mac_filter {
	struct list_head list;
	u8 addr[ETH_ALEN];
	unsigned long state;
};

/* TC bandwidth allocation per direction */
struct hinic_tc_attr {
	u8 pg_id;		/* Priority Group(PG) ID */
	u8 bw_pct;		/* % of PG's bandwidth */
	u8 up_map;		/* User Priority to Traffic Class mapping */
	u8 prio_type;
};

/* User priority configuration */
struct hinic_tc_cfg {
	struct hinic_tc_attr path[2]; /* One each for Tx/Rx */

	bool pfc_en;
};

struct hinic_dcb_config {
	u8 pg_tcs;
	u8 pfc_tcs;

	bool pfc_state;

	struct hinic_tc_cfg tc_cfg[HINIC_DCB_TC_MAX];
	u8     bw_pct[2][HINIC_DCB_PG_MAX]; /* One each for Tx/Rx */
};

enum hinic_intr_flags {
	HINIC_INTR_ON,
	HINIC_RESEND_ON,
};

struct hinic_irq {
	struct net_device	*netdev;
	/* IRQ corresponding index number */
	u16			msix_entry_idx;
	u32			irq_id;         /* The IRQ number from OS */
	char			irq_name[IFNAMSIZ + 16];
	struct napi_struct	napi;
	cpumask_t		affinity_mask;
	struct hinic_txq	*txq;
	struct hinic_rxq	*rxq;
	unsigned long		intr_flag;
};

struct hinic_intr_coal_info {
	u8	pending_limt;
	u8	coalesce_timer_cfg;
	u8	resend_timer_cfg;

	u64	pkt_rate_low;
	u8	rx_usecs_low;
	u8	rx_pending_limt_low;
	u64	pkt_rate_high;
	u8	rx_usecs_high;
	u8	rx_pending_limt_high;

	u8	user_set_intr_coal_flag;
};

#define HINIC_NIC_STATS_INC(nic_dev, field)		\
{							\
	u64_stats_update_begin(&nic_dev->stats.syncp);	\
	nic_dev->stats.field++;				\
	u64_stats_update_end(&nic_dev->stats.syncp);	\
}

struct hinic_nic_stats {
	u64	netdev_tx_timeout;

	/* Subdivision statistics show in private tool */
	u64	tx_carrier_off_drop;
	u64	tx_invalid_qid;

#ifdef HAVE_NDO_GET_STATS64
	struct u64_stats_sync	syncp;
#else
	struct u64_stats_sync_empty syncp;
#endif
};

struct hinic_nic_dev {
	struct pci_dev		*pdev;
	struct net_device	*netdev;
	void			*hwdev;

	int			poll_weight;

	unsigned long		*vlan_bitmap;

	u16			num_qps;
	u16			max_qps;

	u32			msg_enable;
	unsigned long		flags;

	u16			sq_depth;
	u16			rq_depth;

	/* mapping from priority */
	u8			sq_cos_mapping[HINIC_DCB_UP_MAX];
	u8			default_cos_id;
	struct hinic_txq	*txqs;
	struct hinic_rxq	*rxqs;

	struct nic_service_cap	nic_cap;

	struct irq_info		*qps_irq_info;
	struct hinic_irq	*irq_cfg;
	struct work_struct	rx_mode_work;
	struct delayed_work	moderation_task;
	struct workqueue_struct *workq;

	struct list_head	uc_filter_list;
	struct list_head	mc_filter_list;
	unsigned long		rx_mod_state;
	int			netdev_uc_cnt;
	int			netdev_mc_cnt;
	int			lb_test_rx_idx;
	int			lb_pkt_len;
	u8			*lb_test_rx_buf;

	u8			rss_tmpl_idx;
	u16			num_rss;
	u16			rss_limit;
	u8			rss_hash_engine;
	struct nic_rss_type	rss_type;
	u8			*rss_hkey_user;
	/* hkey in big endian */
	u32			*rss_hkey_user_be;
	u32			*rss_indir_user;

	u8			dcbx_cap;
	u32			dcb_changes;
	u8			max_cos;
	u8			up_valid_bitmap;
	u8			up_cos[HINIC_DCB_UP_MAX];
	struct ieee_ets		hinic_ieee_ets_default;
	struct ieee_ets		hinic_ieee_ets;
	struct ieee_pfc		hinic_ieee_pfc;
	struct hinic_dcb_config	dcb_cfg;
	struct hinic_dcb_config	tmp_dcb_cfg;
	struct hinic_dcb_config	save_dcb_cfg;
	unsigned long		dcb_flags;
	int			disable_port_cnt;
	/* lock for disable or enable traffic flow */
	struct semaphore	dcb_sem;

	u16			bp_lower_thd;
	u16			bp_upper_thd;
	bool			heart_status;

	struct hinic_intr_coal_info *intr_coalesce;
	unsigned long		last_moder_jiffies;
	u32			adaptive_rx_coal;
	u8			intr_coal_set_flag;
	u32			his_link_speed;
	/* interrupt coalesce must be different in virtual machine */
	bool			in_vm;
	bool			is_vm_slave;
	int			is_bm_slave;
#ifndef HAVE_NETDEV_STATS_IN_NETDEV
	struct net_device_stats net_stats;
#endif
	struct hinic_nic_stats	stats;
	/* lock for nic resource */
	struct mutex		nic_mutex;
	bool			force_port_disable;
	struct semaphore	port_state_sem;
	u8			link_status;

	struct hinic_environment_info env_info;
	struct hinic_adaptive_cfg adaptive_cfg;

	/* pangea cpu affinity setting */
	bool			force_affinity;
	cpumask_t		affinity_mask;

	u32			lro_replenish_thld;
	u16			rx_buff_len;
	u32			page_order;
};

extern struct hinic_uld_info nic_uld_info;

int hinic_open(struct net_device *netdev);
int hinic_close(struct net_device *netdev);
void hinic_set_ethtool_ops(struct net_device *netdev);
void hinicvf_set_ethtool_ops(struct net_device *netdev);
void hinic_update_num_qps(struct net_device *netdev);
int nic_ioctl(void *uld_dev, u32 cmd, void *buf_in,
	      u32 in_size, void *buf_out, u32 *out_size);

int hinic_force_port_disable(struct hinic_nic_dev *nic_dev);
int hinic_force_set_port_state(struct hinic_nic_dev *nic_dev, bool enable);
int hinic_maybe_set_port_state(struct hinic_nic_dev *nic_dev, bool enable);
void hinic_link_status_change(struct hinic_nic_dev *nic_dev, bool status);

int hinic_disable_func_rss(struct hinic_nic_dev *nic_dev);
int hinic_enable_func_rss(struct hinic_nic_dev *nic_dev);

#define hinic_msg(level, nic_dev, msglvl, format, arg...)	\
do {								\
	if (nic_dev->netdev && nic_dev->netdev->reg_state	\
	    == NETREG_REGISTERED)				\
		nicif_##level(nic_dev, msglvl, nic_dev->netdev,	\
			      format, ## arg);			\
	else							\
		nic_##level(&nic_dev->pdev->dev,		\
			    format, ## arg);			\
} while (0)

#define hinic_info(nic_dev, msglvl, format, arg...)	\
	hinic_msg(info, nic_dev, msglvl, format, ## arg)

#define hinic_warn(nic_dev, msglvl, format, arg...)	\
	hinic_msg(warn, nic_dev, msglvl, format, ## arg)

#define hinic_err(nic_dev, msglvl, format, arg...)	\
	hinic_msg(err, nic_dev, msglvl, format, ## arg)

#endif
