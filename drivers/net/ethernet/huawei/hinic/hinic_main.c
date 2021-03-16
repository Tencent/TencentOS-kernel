// SPDX-License-Identifier: GPL-2.0-only
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

#define pr_fmt(fmt) KBUILD_MODNAME ": [NIC]" fmt
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/ethtool.h>
#include <linux/dcbnl.h>
#include <linux/tcp.h>
#include <linux/ip.h>
#include <linux/debugfs.h>

#include "ossl_knl.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hw.h"
#include "hinic_dbg.h"
#include "hinic_nic_cfg.h"
#include "hinic_nic_dev.h"
#include "hinic_tx.h"
#include "hinic_rx.h"
#include "hinic_qp.h"
#include "hinic_dcb.h"
#include "hinic_lld.h"
#include "hinic_sriov.h"
#include "hinic_pci_id_tbl.h"

static u16 num_qps;
module_param(num_qps, ushort, 0444);
MODULE_PARM_DESC(num_qps, "Number of Queue Pairs (default unset)");

static u16 ovs_num_qps = 16;
module_param(ovs_num_qps, ushort, 0444);
MODULE_PARM_DESC(ovs_num_qps, "Number of Queue Pairs in ovs mode (default=16)");

#define DEFAULT_POLL_WEIGHT	64
static unsigned int poll_weight = DEFAULT_POLL_WEIGHT;
module_param(poll_weight, uint, 0444);
MODULE_PARM_DESC(poll_weight, "Number packets for NAPI budget (default=64)");

#define HINIC_DEAULT_TXRX_MSIX_PENDING_LIMIT		2
#define HINIC_DEAULT_TXRX_MSIX_COALESC_TIMER_CFG	32
#define HINIC_DEAULT_TXRX_MSIX_RESEND_TIMER_CFG		7

/* suit for sdi3.0 vm mode, change this define for test best performance */
#define SDI_VM_PENDING_LIMT		2
#define SDI_VM_COALESCE_TIMER_CFG	16
#define SDI_VM_RX_PKT_RATE_HIGH		1000000
#define SDI_VM_RX_PKT_RATE_LOW		30000
#define SDI_VM_RX_USECS_HIGH		56
#define SDI_VM_RX_PENDING_LIMT_HIGH	20
#define SDI_VM_RX_USECS_LOW		16
#define SDI_VM_RX_PENDING_LIMT_LOW	2

/* if qp_coalesc_use_drv_params_switch !=0, use user setting params */
static unsigned char qp_coalesc_use_drv_params_switch;
module_param(qp_coalesc_use_drv_params_switch, byte, 0444);
MODULE_PARM_DESC(qp_coalesc_use_drv_params_switch, "QP MSI-X Interrupt coalescing parameter switch (default=0, not use drv parameter)");

static unsigned char qp_pending_limit = HINIC_DEAULT_TXRX_MSIX_PENDING_LIMIT;
module_param(qp_pending_limit, byte, 0444);
MODULE_PARM_DESC(qp_pending_limit, "QP MSI-X Interrupt coalescing parameter pending_limit (default=2)");

static unsigned char qp_coalesc_timer_cfg =
		HINIC_DEAULT_TXRX_MSIX_COALESC_TIMER_CFG;
module_param(qp_coalesc_timer_cfg, byte, 0444);
MODULE_PARM_DESC(qp_coalesc_timer_cfg, "QP MSI-X Interrupt coalescing parameter coalesc_timer_cfg (default=32)");

/* For arm64 server, the best known configuration of lro max wqe number
 * is 4 (8K), for x86_64 server, it is 8 (16K). You can also
 * configure these values by hinicadm.
 */
static unsigned char set_max_wqe_num;
module_param(set_max_wqe_num, byte, 0444);
MODULE_PARM_DESC(set_max_wqe_num, "Set lro max wqe number, valid range is 1 - 32, default is 4(arm) / 8(x86)");

#define DEFAULT_RX_BUFF_LEN	2
u16 rx_buff = DEFAULT_RX_BUFF_LEN;
module_param(rx_buff, ushort, 0444);
MODULE_PARM_DESC(rx_buff, "Set rx_buff size, buffer len must be 2^n. 2 - 16, default is 2KB");

static u32 set_lro_timer;
module_param(set_lro_timer, uint, 0444);
MODULE_PARM_DESC(set_lro_timer, "Set lro timer in micro second, valid range is 1 - 1024, default is 16");

static unsigned char set_link_status_follow = HINIC_LINK_FOLLOW_STATUS_MAX;
module_param(set_link_status_follow, byte, 0444);
MODULE_PARM_DESC(set_link_status_follow, "Set link status follow port status. 0 - default, 1 - follow, 2 - separate, other - unset. (default unset)");


static unsigned int lro_replenish_thld = 256;
module_param(lro_replenish_thld, uint, 0444);
MODULE_PARM_DESC(lro_replenish_thld, "Number wqe for lro replenish buffer (default=256)");


static bool l2nic_interrupt_switch = true;
module_param(l2nic_interrupt_switch, bool, 0644);
MODULE_PARM_DESC(l2nic_interrupt_switch, "Control whether execute l2nic io interrupt switch or not, default is true");

static unsigned char lro_en_status = HINIC_LRO_STATUS_UNSET;
module_param(lro_en_status, byte, 0444);
MODULE_PARM_DESC(lro_en_status, "lro enable status. 0 - disable, 1 - enable, other - unset. (default unset)");

static unsigned char qp_pending_limit_low = HINIC_RX_PENDING_LIMIT_LOW;
module_param(qp_pending_limit_low, byte, 0444);
MODULE_PARM_DESC(qp_pending_limit_low, "MSI-X adaptive low coalesce pending limit, range is 0 - 255");

static unsigned char qp_coalesc_timer_low = HINIC_RX_COAL_TIME_LOW;
module_param(qp_coalesc_timer_low, byte, 0444);
MODULE_PARM_DESC(qp_coalesc_timer_low, "MSI-X adaptive low coalesce time, range is 0 - 255");

static unsigned char qp_pending_limit_high = HINIC_RX_PENDING_LIMIT_HIGH;
module_param(qp_pending_limit_high, byte, 0444);
MODULE_PARM_DESC(qp_pending_limit_high, "MSI-X adaptive high coalesce pending limit, range is 0 - 255");

static unsigned char qp_coalesc_timer_high = HINIC_RX_COAL_TIME_HIGH;
module_param(qp_coalesc_timer_high, byte, 0444);
MODULE_PARM_DESC(qp_coalesc_timer_high, "MSI-X adaptive high coalesce time, range is 0 - 255");

static unsigned int enable_bp = 0;

static unsigned int bp_lower_thd = HINIC_RX_BP_LOWER_THD;
static unsigned int bp_upper_thd = HINIC_RX_BP_UPPER_THD;

#define HINIC_NIC_DEV_WQ_NAME		"hinic_nic_dev_wq"

#define DEFAULT_MSG_ENABLE		(NETIF_MSG_DRV | NETIF_MSG_LINK)

#define QID_MASKED(q_id, nic_dev)	(q_id & ((nic_dev)->num_qps - 1))

#define VLAN_BITMAP_BYTE_SIZE(nic_dev)	(sizeof(*(nic_dev)->vlan_bitmap))

#define VLAN_BITMAP_BITS_SIZE(nic_dev)	(VLAN_BITMAP_BYTE_SIZE(nic_dev) * 8)

#define VLAN_NUM_BITMAPS(nic_dev)	(VLAN_N_VID / \
					VLAN_BITMAP_BITS_SIZE(nic_dev))

#define VLAN_BITMAP_SIZE(nic_dev)	(VLAN_N_VID / \
					VLAN_BITMAP_BYTE_SIZE(nic_dev))

#define VID_LINE(nic_dev, vid)	((vid) / VLAN_BITMAP_BITS_SIZE(nic_dev))
#define VID_COL(nic_dev, vid)	((vid) & (VLAN_BITMAP_BITS_SIZE(nic_dev) - 1))

enum hinic_rx_mod {
	HINIC_RX_MODE_UC = 1 << 0,
	HINIC_RX_MODE_MC = 1 << 1,
	HINIC_RX_MODE_BC = 1 << 2,
	HINIC_RX_MODE_MC_ALL = 1 << 3,
	HINIC_RX_MODE_PROMISC = 1 << 4,
};

enum hinic_rx_buff_len {
	RX_BUFF_VALID_2KB		= 2,
	RX_BUFF_VALID_4KB		= 4,
	RX_BUFF_VALID_8KB		= 8,
	RX_BUFF_VALID_16KB		= 16,
};

#define HINIC_AVG_PKT_SMALL		256U
#define HINIC_MODERATONE_DELAY		HZ
#define CONVERT_UNIT			1024

#ifdef HAVE_MULTI_VLAN_OFFLOAD_EN
int hinic_netdev_event(struct notifier_block *notifier,
		       unsigned long event, void *ptr);

/* used for netdev notifier register/unregister */
DEFINE_MUTEX(g_hinic_netdev_notifiers_mutex);
static int hinic_netdev_notifiers_ref_cnt;
static struct notifier_block hinic_netdev_notifier = {
	.notifier_call = hinic_netdev_event,
};

static void hinic_register_notifier(struct hinic_nic_dev *nic_dev)
{
	int err;

	mutex_lock(&g_hinic_netdev_notifiers_mutex);
	hinic_netdev_notifiers_ref_cnt++;
	if (hinic_netdev_notifiers_ref_cnt == 1) {
		err = register_netdevice_notifier(&hinic_netdev_notifier);
		if (err) {
			hinic_info(nic_dev, drv, "Register netdevice notifier failed, err: %d\n",
				   err);
			hinic_netdev_notifiers_ref_cnt--;
		}
	}
	mutex_unlock(&g_hinic_netdev_notifiers_mutex);
}

static void hinic_unregister_notifier(struct hinic_nic_dev *nic_dev)
{
	mutex_lock(&g_hinic_netdev_notifiers_mutex);
	if (hinic_netdev_notifiers_ref_cnt == 1)
		unregister_netdevice_notifier(&hinic_netdev_notifier);

	if (hinic_netdev_notifiers_ref_cnt)
		hinic_netdev_notifiers_ref_cnt--;
	mutex_unlock(&g_hinic_netdev_notifiers_mutex);
}

#define HINIC_MAX_VLAN_DEPTH_OFFLOAD_SUPPORT	2
#define HINIC_VLAN_CLEAR_OFFLOAD	(NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM | \
					 NETIF_F_SCTP_CRC | NETIF_F_RXCSUM | \
					 NETIF_F_ALL_TSO)

int hinic_netdev_event(struct notifier_block *notifier,
		       unsigned long event, void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct net_device *real_dev, *ret;
	struct hinic_nic_dev *nic_dev;
	u16 vlan_depth;

	if (!is_vlan_dev(ndev))
		return NOTIFY_DONE;

	dev_hold(ndev);

	switch (event) {
	case NETDEV_REGISTER:
		real_dev = vlan_dev_real_dev(ndev);
		nic_dev = hinic_get_uld_dev_by_ifname(real_dev->name,
						      SERVICE_T_NIC);
		if (!nic_dev)
			goto out;

		vlan_depth = 1;
		ret = vlan_dev_priv(ndev)->real_dev;
		while (is_vlan_dev(ret)) {
			ret = vlan_dev_priv(ret)->real_dev;
			vlan_depth++;
		}

		if (vlan_depth == HINIC_MAX_VLAN_DEPTH_OFFLOAD_SUPPORT) {
			ndev->vlan_features &= (~HINIC_VLAN_CLEAR_OFFLOAD);
		} else if (vlan_depth > HINIC_MAX_VLAN_DEPTH_OFFLOAD_SUPPORT) {
#ifdef HAVE_NDO_SET_FEATURES
#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
			set_netdev_hw_features(ndev,
					       get_netdev_hw_features(ndev) &
					       (~HINIC_VLAN_CLEAR_OFFLOAD));
#else
			ndev->hw_features &= (~HINIC_VLAN_CLEAR_OFFLOAD);
#endif
#endif
			ndev->features &= (~HINIC_VLAN_CLEAR_OFFLOAD);
		}

		break;

	default:
		break;
	};

out:
	dev_put(ndev);

	return NOTIFY_DONE;
}
#endif

void hinic_link_status_change(struct hinic_nic_dev *nic_dev, bool status)
{
	struct net_device *netdev = nic_dev->netdev;

	if (!test_bit(HINIC_INTF_UP, &nic_dev->flags) ||
	    test_bit(HINIC_LP_TEST, &nic_dev->flags))
		return;

	if (status) {
		if (netif_carrier_ok(netdev))
			return;

		nic_dev->link_status = status;
		netif_carrier_on(netdev);
		nicif_info(nic_dev, link, netdev, "Link is up\n");
	} else {
		if (!netif_carrier_ok(netdev))
			return;

		nic_dev->link_status = status;
		netif_carrier_off(netdev);
		nicif_info(nic_dev, link, netdev, "Link is down\n");
	}
}

static void hinic_heart_lost(struct hinic_nic_dev *nic_dev)
{
	nic_dev->heart_status = false;
}

static int hinic_setup_qps_resources(struct hinic_nic_dev *nic_dev)
{
	struct net_device *netdev = nic_dev->netdev;
	int err;

	err = hinic_setup_all_tx_resources(netdev);
	if (err) {
		nicif_err(nic_dev, drv, netdev,
			  "Failed to create Tx queues\n");
		return err;
	}

	err = hinic_setup_all_rx_resources(netdev, nic_dev->qps_irq_info);
	if (err) {
		nicif_err(nic_dev, drv, netdev,
			  "Failed to create Rx queues\n");
		goto create_rxqs_err;
	}

	return 0;

create_rxqs_err:
	hinic_free_all_tx_resources(netdev);

	return err;
}

static int hinic_configure(struct hinic_nic_dev *nic_dev)
{
	struct net_device *netdev = nic_dev->netdev;
	int err;

	/* rx rss init */
	err = hinic_rx_configure(netdev);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to configure rx\n");
		return err;
	}

	return 0;
}

static void hinic_remove_configure(struct hinic_nic_dev *nic_dev)
{
	hinic_rx_remove_configure(nic_dev->netdev);
}

static void hinic_setup_dcb_qps(struct hinic_nic_dev *nic_dev, u16 max_qps)
{
	struct net_device *netdev = nic_dev->netdev;
	u16 num_rss;
	u8 num_tcs;
	u8 i;

	if (!test_bit(HINIC_DCB_ENABLE, &nic_dev->flags) ||
	    !test_bit(HINIC_RSS_ENABLE, &nic_dev->flags))
		return;

	num_tcs = (u8)netdev_get_num_tc(netdev);
	/* For now, we don't support to change num_tcs */
	if (num_tcs != nic_dev->max_cos || max_qps < num_tcs) {
		nicif_err(nic_dev, drv, netdev, "Invalid num_tcs: %d or num_qps: %d, disable DCB\n",
			  num_tcs, max_qps);
		netdev_reset_tc(netdev);
		clear_bit(HINIC_DCB_ENABLE, &nic_dev->flags);
		/* if we can't enable rss or get enough num_qps,
		 * need to sync default configure to hw
		 */
		hinic_configure_dcb(netdev);
	} else {
		/* We bind sq with cos but not tc */
		num_rss = (u16)(max_qps / nic_dev->max_cos);
		num_rss = min_t(u16, num_rss, nic_dev->rss_limit);
		for (i = 0; i < nic_dev->max_cos; i++)
			netdev_set_tc_queue(netdev, i, num_rss,
					    (u16)(num_rss * i));

		nic_dev->num_rss = num_rss;
		nic_dev->num_qps = (u16)(num_tcs * num_rss);
	}
}

/* determin num_qps from rss_tmpl_id/irq_num/dcb_en */
static int hinic_setup_num_qps(struct hinic_nic_dev *nic_dev)
{
	struct net_device *netdev = nic_dev->netdev;
	u32 irq_size;
	u16 resp_irq_num, i;
	int err;

	if (test_bit(HINIC_RSS_ENABLE, &nic_dev->flags)) {
		nic_dev->num_rss = nic_dev->rss_limit;
		nic_dev->num_qps = nic_dev->rss_limit;
	} else {
		nic_dev->num_rss = 0;
		nic_dev->num_qps = 1;
	}

	hinic_setup_dcb_qps(nic_dev, nic_dev->max_qps);

	irq_size = sizeof(*nic_dev->qps_irq_info) * nic_dev->num_qps;
	if (!irq_size) {
		nicif_err(nic_dev, drv, netdev, "Cannot allocate zero size entries\n");
		return -EINVAL;
	}
	nic_dev->qps_irq_info = kzalloc(irq_size, GFP_KERNEL);
	if (!nic_dev->qps_irq_info) {
		nicif_err(nic_dev, drv, netdev, "Failed to alloc msix entries\n");
		return -ENOMEM;
	}

	err = hinic_alloc_irqs(nic_dev->hwdev, SERVICE_T_NIC, nic_dev->num_qps,
			       nic_dev->qps_irq_info, &resp_irq_num);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to alloc irqs\n");
		kfree(nic_dev->qps_irq_info);
		return err;
	}

	/* available irq number is less than rq numbers, adjust rq numbers */
	if (resp_irq_num < nic_dev->num_qps) {
		nic_dev->num_qps = resp_irq_num;
		nic_dev->num_rss = nic_dev->num_qps;
		hinic_setup_dcb_qps(nic_dev, nic_dev->num_qps);
		nicif_warn(nic_dev, drv, netdev,
			   "Can not get enough irqs, adjust num_qps to %d\n",
			   nic_dev->num_qps);
		/* after adjust num_qps, free the remaind irq */
		for (i = nic_dev->num_qps; i < resp_irq_num; i++)
			hinic_free_irq(nic_dev->hwdev, SERVICE_T_NIC,
				       nic_dev->qps_irq_info[i].irq_id);
	}

	nicif_info(nic_dev, drv, netdev, "Finally num_qps: %d, num_rss: %d\n",
		   nic_dev->num_qps, nic_dev->num_rss);

	return 0;
}

static void hinic_destroy_num_qps(struct hinic_nic_dev *nic_dev)
{
	u16 i;

	for (i = 0; i < nic_dev->num_qps; i++)
		hinic_free_irq(nic_dev->hwdev, SERVICE_T_NIC,
			       nic_dev->qps_irq_info[i].irq_id);

	kfree(nic_dev->qps_irq_info);
}

static int hinic_poll(struct napi_struct *napi, int budget)
{
	int tx_pkts, rx_pkts;
	struct hinic_irq *irq_cfg = container_of(napi, struct hinic_irq, napi);
	struct hinic_nic_dev *nic_dev = netdev_priv(irq_cfg->netdev);

	rx_pkts = hinic_rx_poll(irq_cfg->rxq, budget);

	tx_pkts = hinic_tx_poll(irq_cfg->txq, budget);

	if (tx_pkts >= budget || rx_pkts >= budget)
		return budget;

	set_bit(HINIC_RESEND_ON, &irq_cfg->intr_flag);
	rx_pkts += hinic_rx_poll(irq_cfg->rxq, budget - rx_pkts);
	tx_pkts += hinic_tx_poll(irq_cfg->txq, budget - tx_pkts);
	if (rx_pkts >= budget || tx_pkts >= budget) {
		clear_bit(HINIC_RESEND_ON, &irq_cfg->intr_flag);
		return budget;
	}

	napi_complete(napi);

	if (!test_and_set_bit(HINIC_INTR_ON, &irq_cfg->intr_flag)) {
		if (!HINIC_FUNC_IS_VF(nic_dev->hwdev))
			hinic_set_msix_state(nic_dev->hwdev,
					     irq_cfg->msix_entry_idx,
					     HINIC_MSIX_ENABLE);
		else if (!nic_dev->in_vm &&
			 (hinic_get_func_mode(nic_dev->hwdev) ==
			  FUNC_MOD_NORMAL_HOST))
			enable_irq(irq_cfg->irq_id);
	}

	return max(tx_pkts, rx_pkts);
}

static void qp_add_napi(struct hinic_irq *irq_cfg)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(irq_cfg->netdev);

	netif_napi_add(nic_dev->netdev, &irq_cfg->napi,
		       hinic_poll, nic_dev->poll_weight);
	napi_enable(&irq_cfg->napi);
}

static void qp_del_napi(struct hinic_irq *irq_cfg)
{
	napi_disable(&irq_cfg->napi);
	netif_napi_del(&irq_cfg->napi);
}

static irqreturn_t qp_irq(int irq, void *data)
{
	struct hinic_irq *irq_cfg = (struct hinic_irq *)data;
	struct hinic_nic_dev *nic_dev = netdev_priv(irq_cfg->netdev);
	u16 msix_entry_idx = irq_cfg->msix_entry_idx;

	if (napi_schedule_prep(&irq_cfg->napi)) {
		if (l2nic_interrupt_switch) {
			/* Disable the interrupt until napi will be completed */
			if (!HINIC_FUNC_IS_VF(nic_dev->hwdev)) {
				hinic_set_msix_state(nic_dev->hwdev,
						     msix_entry_idx,
						     HINIC_MSIX_DISABLE);
			} else if (!nic_dev->in_vm &&
				   (hinic_get_func_mode(nic_dev->hwdev) ==
				    FUNC_MOD_NORMAL_HOST)) {
				disable_irq_nosync(irq_cfg->irq_id);
			}

			clear_bit(HINIC_INTR_ON, &irq_cfg->intr_flag);
		}

		hinic_misx_intr_clear_resend_bit(nic_dev->hwdev,
						 msix_entry_idx, 1);

		clear_bit(HINIC_RESEND_ON, &irq_cfg->intr_flag);

		__napi_schedule(&irq_cfg->napi);
	} else if (!test_bit(HINIC_RESEND_ON, &irq_cfg->intr_flag)) {
		hinic_misx_intr_clear_resend_bit(nic_dev->hwdev, msix_entry_idx,
						 1);
	}

	return IRQ_HANDLED;
}

static int hinic_request_irq(struct hinic_irq *irq_cfg, u16 q_id)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(irq_cfg->netdev);
	struct nic_interrupt_info info = {0};
	int err;

	qp_add_napi(irq_cfg);

	info.msix_index = irq_cfg->msix_entry_idx;
	info.lli_set = 0;
	info.interrupt_coalesc_set = 1;
	info.pending_limt = nic_dev->intr_coalesce[q_id].pending_limt;
	info.coalesc_timer_cfg =
		nic_dev->intr_coalesce[q_id].coalesce_timer_cfg;
	info.resend_timer_cfg = nic_dev->intr_coalesce[q_id].resend_timer_cfg;
	nic_dev->rxqs[q_id].last_coalesc_timer_cfg =
			nic_dev->intr_coalesce[q_id].coalesce_timer_cfg;
	nic_dev->rxqs[q_id].last_pending_limt =
			nic_dev->intr_coalesce[q_id].pending_limt;
	err = hinic_set_interrupt_cfg(nic_dev->hwdev, info);
	if (err) {
		nicif_err(nic_dev, drv, irq_cfg->netdev,
			  "Failed to set RX interrupt coalescing attribute.\n");
		qp_del_napi(irq_cfg);
		return err;
	}

	err = request_irq(irq_cfg->irq_id, &qp_irq, 0,
			  irq_cfg->irq_name, irq_cfg);
	if (err) {
		nicif_err(nic_dev, drv, irq_cfg->netdev, "Failed to request Rx irq\n");
		qp_del_napi(irq_cfg);
		return err;
	}

	/* assign the mask for this irq */
	irq_set_affinity_hint(irq_cfg->irq_id, &irq_cfg->affinity_mask);

	return 0;
}

static int set_interrupt_moder(struct hinic_nic_dev *nic_dev, u16 q_id,
			       u8 coalesc_timer_cfg, u8 pending_limt)
{
	struct nic_interrupt_info interrupt_info = {0};
	int err;

	if (coalesc_timer_cfg == nic_dev->rxqs[q_id].last_coalesc_timer_cfg &&
	    pending_limt == nic_dev->rxqs[q_id].last_pending_limt)
		return 0;

	/* netdev not running or qp not in using,
	 * don't need to set coalesce to hw
	 */
	if (!test_bit(HINIC_INTF_UP, &nic_dev->flags) ||
	    q_id >= nic_dev->num_qps)
		return 0;

	interrupt_info.lli_set = 0;
	interrupt_info.interrupt_coalesc_set = 1;
	interrupt_info.coalesc_timer_cfg = coalesc_timer_cfg;
	interrupt_info.pending_limt = pending_limt;
	interrupt_info.msix_index = nic_dev->irq_cfg[q_id].msix_entry_idx;
	interrupt_info.resend_timer_cfg =
			nic_dev->intr_coalesce[q_id].resend_timer_cfg;

	err = hinic_set_interrupt_cfg(nic_dev->hwdev, interrupt_info);
	if (err) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Failed modifying moderation for Queue: %d\n", q_id);
	} else {
		nic_dev->rxqs[q_id].last_coalesc_timer_cfg = coalesc_timer_cfg;
		nic_dev->rxqs[q_id].last_pending_limt = pending_limt;
	}

	return err;
}

static void __calc_coal_para(struct hinic_nic_dev *nic_dev,
			     struct hinic_intr_coal_info *q_coal, u64 rate,
			     u8 *coalesc_timer_cfg, u8 *pending_limt)
{
	if (rate < q_coal->pkt_rate_low) {
		*coalesc_timer_cfg = q_coal->rx_usecs_low;
		*pending_limt = q_coal->rx_pending_limt_low;
	} else if (rate > q_coal->pkt_rate_high) {
		*coalesc_timer_cfg = q_coal->rx_usecs_high;
		*pending_limt = q_coal->rx_pending_limt_high;
	} else {
		*coalesc_timer_cfg =
			(u8)((rate - q_coal->pkt_rate_low) *
			(q_coal->rx_usecs_high -
			q_coal->rx_usecs_low) /
			(q_coal->pkt_rate_high -
			q_coal->pkt_rate_low) +
			q_coal->rx_usecs_low);
		if (nic_dev->in_vm)
			*pending_limt = (u8)((rate - q_coal->pkt_rate_low) *
				(q_coal->rx_pending_limt_high -
				q_coal->rx_pending_limt_low) /
				(q_coal->pkt_rate_high -
				q_coal->pkt_rate_low) +
				q_coal->rx_pending_limt_low);
		else
			*pending_limt = q_coal->rx_pending_limt_low;
	}
}

static void update_queue_coal(struct hinic_nic_dev *nic_dev, u16 qid,
			      u64 rate, u64 avg_pkt_size, u64 tx_rate)
{
	struct hinic_intr_coal_info *q_coal;
	u8 coalesc_timer_cfg, pending_limt;

	q_coal = &nic_dev->intr_coalesce[qid];

	if ((rate > HINIC_RX_RATE_THRESH &&
	     avg_pkt_size > HINIC_AVG_PKT_SMALL) ||
	    (nic_dev->in_vm && rate > HINIC_RX_RATE_THRESH)) {
		__calc_coal_para(nic_dev, q_coal, rate,
				 &coalesc_timer_cfg, &pending_limt);
	} else {
		coalesc_timer_cfg = HINIC_LOWEST_LATENCY;
		pending_limt = q_coal->rx_pending_limt_low;
	}

	set_interrupt_moder(nic_dev, qid, coalesc_timer_cfg,
			    pending_limt);
}

#define SDI_VM_PPS_3W		30000
#define SDI_VM_PPS_5W		50000

#define SDI_VM_BPS_100MB	12500000
#define SDI_VM_BPS_1GB		125000000

static void update_queue_coal_sdi_vm(struct hinic_nic_dev *nic_dev,
				     u16 qid, u64 rx_pps, u64 rx_bps,
				     u64 tx_pps, u64 tx_bps)
{
	struct hinic_intr_coal_info *q_coal = NULL;
	u8 coalesc_timer_cfg, pending_limt;

	q_coal = &nic_dev->intr_coalesce[qid];
	if (qp_coalesc_use_drv_params_switch == 0) {
		if (rx_pps < SDI_VM_PPS_3W &&
		    tx_pps < SDI_VM_PPS_3W &&
		    rx_bps < SDI_VM_BPS_100MB &&
		    tx_bps < SDI_VM_BPS_100MB) {
			set_interrupt_moder(nic_dev, qid, 0, 0);
		} else if (tx_pps > SDI_VM_PPS_3W &&
			   tx_pps < SDI_VM_PPS_5W &&
			   tx_bps > SDI_VM_BPS_1GB) {
			set_interrupt_moder(nic_dev, qid, 7, 7);
		} else {
			__calc_coal_para(nic_dev, q_coal, rx_pps,
					 &coalesc_timer_cfg,
					 &pending_limt);
			set_interrupt_moder(nic_dev, qid,
					    coalesc_timer_cfg,
					    pending_limt);
		}
	} else {
		__calc_coal_para(nic_dev, q_coal, rx_pps,
				 &coalesc_timer_cfg,
				 &pending_limt);
		set_interrupt_moder(nic_dev, qid, coalesc_timer_cfg,
				    pending_limt);
	}
}

static void hinic_auto_moderation_work(struct work_struct *work)
{
	struct delayed_work *delay = to_delayed_work(work);
	struct hinic_nic_dev *nic_dev = container_of(delay,
						     struct hinic_nic_dev,
						     moderation_task);
	unsigned long period = (unsigned long)(jiffies -
			nic_dev->last_moder_jiffies);
	u64 rx_packets, rx_bytes, rx_pkt_diff, rate, avg_pkt_size;
	u64 tx_packets, tx_bytes, tx_pkt_diff, tx_rate, rx_bps, tx_bps;
	u16 qid;

	if (!test_bit(HINIC_INTF_UP, &nic_dev->flags))
		return;

	queue_delayed_work(nic_dev->workq, &nic_dev->moderation_task,
			   HINIC_MODERATONE_DELAY);

	if (!nic_dev->adaptive_rx_coal || !period)
		return;

	for (qid = 0; qid < nic_dev->num_qps; qid++) {
		rx_packets = nic_dev->rxqs[qid].rxq_stats.packets;
		rx_bytes = nic_dev->rxqs[qid].rxq_stats.bytes;
		tx_packets = nic_dev->txqs[qid].txq_stats.packets;
		tx_bytes = nic_dev->txqs[qid].txq_stats.bytes;

		rx_pkt_diff =
			rx_packets - nic_dev->rxqs[qid].last_moder_packets;
		avg_pkt_size = rx_pkt_diff ?
			((unsigned long)(rx_bytes -
			 nic_dev->rxqs[qid].last_moder_bytes)) /
			 rx_pkt_diff : 0;

		rate = rx_pkt_diff * HZ / period;
		tx_pkt_diff =
			tx_packets - nic_dev->txqs[qid].last_moder_packets;
		tx_rate = tx_pkt_diff * HZ / period;

		rx_bps = (unsigned long)(rx_bytes -
			 nic_dev->rxqs[qid].last_moder_bytes)
			 * HZ / period;
		tx_bps = (unsigned long)(tx_bytes -
			 nic_dev->txqs[qid].last_moder_bytes)
			 * HZ / period;
		if ((nic_dev->is_vm_slave && nic_dev->in_vm) ||
		    nic_dev->is_bm_slave) {
			update_queue_coal_sdi_vm(nic_dev, qid, rate, rx_bps,
						 tx_rate, tx_bps);
		} else {
			update_queue_coal(nic_dev, qid, rate, avg_pkt_size,
					  tx_rate);
		}

		nic_dev->rxqs[qid].last_moder_packets = rx_packets;
		nic_dev->rxqs[qid].last_moder_bytes = rx_bytes;
		nic_dev->txqs[qid].last_moder_packets = tx_packets;
		nic_dev->txqs[qid].last_moder_bytes = tx_bytes;
	}

	nic_dev->last_moder_jiffies = jiffies;
}


static void hinic_release_irq(struct hinic_irq *irq_cfg)
{
	irq_set_affinity_hint(irq_cfg->irq_id, NULL);
	synchronize_irq(irq_cfg->irq_id);
	free_irq(irq_cfg->irq_id, irq_cfg);
	qp_del_napi(irq_cfg);
}

static int hinic_qps_irq_init(struct hinic_nic_dev *nic_dev)
{
	struct pci_dev *pdev = nic_dev->pdev;
	struct irq_info *qp_irq_info;
	struct hinic_irq *irq_cfg;
	u16 q_id, i;
	u32 local_cpu;
	int err;

	nic_dev->irq_cfg = kcalloc(nic_dev->num_qps, sizeof(*nic_dev->irq_cfg),
				   GFP_KERNEL);
	if (!nic_dev->irq_cfg) {
		nic_err(&pdev->dev, "Failed to alloc irq cfg\n");
		return -ENOMEM;
	}

	for (q_id = 0; q_id < nic_dev->num_qps; q_id++) {
		qp_irq_info = &nic_dev->qps_irq_info[q_id];
		irq_cfg = &nic_dev->irq_cfg[q_id];

		irq_cfg->irq_id = qp_irq_info->irq_id;
		irq_cfg->msix_entry_idx = qp_irq_info->msix_entry_idx;
		irq_cfg->netdev = nic_dev->netdev;
		irq_cfg->txq = &nic_dev->txqs[q_id];
		irq_cfg->rxq = &nic_dev->rxqs[q_id];
		nic_dev->rxqs[q_id].irq_cfg = irq_cfg;

		if (nic_dev->force_affinity) {
			irq_cfg->affinity_mask = nic_dev->affinity_mask;
		} else {
			local_cpu =
				cpumask_local_spread(q_id,
						     dev_to_node(&pdev->dev));
			cpumask_set_cpu(local_cpu, &irq_cfg->affinity_mask);
		}

		err = snprintf(irq_cfg->irq_name, sizeof(irq_cfg->irq_name),
			       "%s_qp%d", nic_dev->netdev->name, q_id);
		if (err <= 0 || err >= (int)sizeof(irq_cfg->irq_name)) {
			nic_err(&pdev->dev,
				"Failed snprintf irq_name, function return(%d) and dest_len(%d)\n",
				err, (int)sizeof(irq_cfg->irq_name));
			goto req_tx_irq_err;
		}

		set_bit(HINIC_INTR_ON, &irq_cfg->intr_flag);

		err = hinic_request_irq(irq_cfg, q_id);
		if (err) {
			nicif_err(nic_dev, drv, nic_dev->netdev, "Failed to request Rx irq\n");
			goto req_tx_irq_err;
		}

		hinic_set_msix_state(nic_dev->hwdev,
				     irq_cfg->msix_entry_idx,
				     HINIC_MSIX_ENABLE);
	}

	INIT_DELAYED_WORK(&nic_dev->moderation_task,
			  hinic_auto_moderation_work);

	return 0;

req_tx_irq_err:
	for (i = 0; i < q_id; i++) {
		hinic_set_msix_state(nic_dev->hwdev,
				     nic_dev->irq_cfg[i].msix_entry_idx,
				     HINIC_MSIX_DISABLE);
		hinic_release_irq(&nic_dev->irq_cfg[i]);
	}

	kfree(nic_dev->irq_cfg);

	return err;
}

static void hinic_qps_irq_deinit(struct hinic_nic_dev *nic_dev)
{
	u16 q_id;

	for (q_id = 0; q_id < nic_dev->num_qps; q_id++) {
		hinic_set_msix_state(nic_dev->hwdev,
				     nic_dev->irq_cfg[q_id].msix_entry_idx,
				     HINIC_MSIX_DISABLE);
		hinic_release_irq(&nic_dev->irq_cfg[q_id]);
	}

	kfree(nic_dev->irq_cfg);
}

int hinic_force_port_disable(struct hinic_nic_dev *nic_dev)
{
	int err;

	down(&nic_dev->port_state_sem);

	err = hinic_set_port_enable(nic_dev->hwdev, false);
	if (!err)
		nic_dev->force_port_disable = true;

	up(&nic_dev->port_state_sem);

	return err;
}

int hinic_force_set_port_state(struct hinic_nic_dev *nic_dev, bool enable)
{
	int err = 0;

	down(&nic_dev->port_state_sem);

	nic_dev->force_port_disable = false;
	err = hinic_set_port_enable(nic_dev->hwdev, enable);

	up(&nic_dev->port_state_sem);

	return err;
}

int hinic_maybe_set_port_state(struct hinic_nic_dev *nic_dev, bool enable)
{
	int err;

	down(&nic_dev->port_state_sem);

	/* Do nothing when force disable
	 * Port will disable when call force port disable
	 * and should not enable port when in force mode
	 */
	if (nic_dev->force_port_disable) {
		up(&nic_dev->port_state_sem);
		return 0;
	}

	err = hinic_set_port_enable(nic_dev->hwdev, enable);

	up(&nic_dev->port_state_sem);

	return err;
}

static void hinic_print_link_message(struct hinic_nic_dev *nic_dev,
				     u8 link_status)
{
	if (nic_dev->link_status == link_status)
		return;

	nic_dev->link_status = link_status;

	nicif_info(nic_dev, link, nic_dev->netdev, "Link is %s\n",
		   (link_status ? "up" : "down"));
}

int hinic_open(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u8 link_status = 0;
	int err;

	if (test_bit(HINIC_INTF_UP, &nic_dev->flags)) {
		nicif_info(nic_dev, drv, netdev, "Netdev already open, do nothing\n");
		return 0;
	}

	err = hinic_setup_num_qps(nic_dev);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to setup num_qps\n");
		return err;
	}

	err = hinic_create_qps(nic_dev->hwdev, nic_dev->num_qps,
			       nic_dev->sq_depth, nic_dev->rq_depth,
			       nic_dev->qps_irq_info, HINIC_MAX_SQ_BUFDESCS);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to create queue pairs\n");
		goto create_qps_err;
	}

	err = hinic_setup_qps_resources(nic_dev);
	if (err)
		goto setup_qps_resources_err;

	err = hinic_init_qp_ctxts(nic_dev->hwdev);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to init qp ctxts\n");
		goto init_qp_ctxts_err;
	}

	err = hinic_set_port_mtu(nic_dev->hwdev, netdev->mtu);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to set mtu\n");
		goto mtu_err;
	}

	err = hinic_configure(nic_dev);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to configure txrx\n");
		goto cfg_err;
	}

	err = hinic_qps_irq_init(nic_dev);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to qps irq init\n");
		goto qps_irqs_init_err;
	}

	err = hinic_set_vport_enable(nic_dev->hwdev, true);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to enable vport\n");
		goto vport_enable_err;
	}

	err = hinic_maybe_set_port_state(nic_dev, true);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to enable port\n");
		goto port_enable_err;
	}

	set_bit(HINIC_INTF_UP, &nic_dev->flags);

	netif_set_real_num_tx_queues(netdev, nic_dev->num_qps);
	netif_set_real_num_rx_queues(netdev, nic_dev->num_qps);
	netif_tx_wake_all_queues(netdev);

	queue_delayed_work(nic_dev->workq, &nic_dev->moderation_task,
			   HINIC_MODERATONE_DELAY);

	err = hinic_get_link_state(nic_dev->hwdev, &link_status);
	if (!err && link_status) {
		hinic_update_pf_bw(nic_dev->hwdev);
		netif_carrier_on(netdev);
	}

	hinic_print_link_message(nic_dev, link_status);

	if (!HINIC_FUNC_IS_VF(nic_dev->hwdev))
		hinic_notify_all_vfs_link_changed(nic_dev->hwdev, link_status);

	nicif_info(nic_dev, drv, nic_dev->netdev, "Netdev is up\n");

	return 0;

port_enable_err:
	hinic_set_vport_enable(nic_dev->hwdev, false);

vport_enable_err:
	hinic_flush_sq_res(nic_dev->hwdev);
	/* After set vport disable 100ms, no packets will be send to host */
	msleep(100);
	hinic_qps_irq_deinit(nic_dev);

qps_irqs_init_err:
	hinic_remove_configure(nic_dev);

cfg_err:
mtu_err:
	hinic_free_qp_ctxts(nic_dev->hwdev);

init_qp_ctxts_err:
	hinic_free_all_rx_resources(netdev);
	hinic_free_all_tx_resources(netdev);

setup_qps_resources_err:
	hinic_free_qps(nic_dev->hwdev);

create_qps_err:
	hinic_destroy_num_qps(nic_dev);

	return err;
}

int hinic_close(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (!test_and_clear_bit(HINIC_INTF_UP, &nic_dev->flags)) {
		nicif_info(nic_dev, drv, netdev, "Netdev already close, do nothing\n");
		return 0;
	}

	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	cancel_delayed_work_sync(&nic_dev->moderation_task);

	if (hinic_get_chip_present_flag(nic_dev->hwdev)) {
		if (!HINIC_FUNC_IS_VF(nic_dev->hwdev))
			hinic_notify_all_vfs_link_changed(nic_dev->hwdev, 0);

		hinic_maybe_set_port_state(nic_dev, false);

		hinic_set_vport_enable(nic_dev->hwdev, false);

		hinic_flush_txqs(netdev);
		hinic_flush_sq_res(nic_dev->hwdev);
		/* After set vport disable 100ms,
		 * no packets will be send to host
		 */
		msleep(100);
	}

	hinic_qps_irq_deinit(nic_dev);
	hinic_remove_configure(nic_dev);

	if (hinic_get_chip_present_flag(nic_dev->hwdev))
		hinic_free_qp_ctxts(nic_dev->hwdev);

	mutex_lock(&nic_dev->nic_mutex);
	hinic_free_all_rx_resources(netdev);

	hinic_free_all_tx_resources(netdev);

	hinic_free_qps(nic_dev->hwdev);

	hinic_destroy_num_qps(nic_dev);
	mutex_unlock(&nic_dev->nic_mutex);

	nicif_info(nic_dev, drv, nic_dev->netdev, "Netdev is down\n");

	return 0;
}

static inline u32 calc_toeplitz_rss(u32 sip, u32 dip, u32 sport, u32 dport,
				    const u32 *rss_key)
{
	u32 i, port, rss = 0;

	port = (sport << 16) | dport;

	/* The key - SIP, DIP, SPORT, DPORT */
	for (i = 0; i < 32; i++)
		if (sip & ((u32)1 << (u32)(31 - i)))
			rss ^= (rss_key[0] << i) |
			(u32)((u64)rss_key[1] >> (32 - i));

	for (i = 0; i < 32; i++)
		if (dip & ((u32)1 << (u32)(31 - i)))
			rss ^= (rss_key[1] << i) |
			(u32)((u64)rss_key[2] >> (32 - i));

	for (i = 0; i < 32; i++)
		if (port & ((u32)1 << (u32)(31 - i)))
			rss ^= (rss_key[2] << i) |
			(u32)((u64)rss_key[3] >> (32 - i));

	return rss;
}

static u16 select_queue_by_toeplitz(struct net_device *dev,
				    struct sk_buff *skb,
				    unsigned int num_tx_queues)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(dev);
	struct tcphdr *tcphdr;
	struct iphdr *iphdr;
	u32 hash = 0;

	if (skb_rx_queue_recorded(skb)) {
		hash = skb_get_rx_queue(skb);
		while (unlikely(hash >= num_tx_queues))
			hash -= num_tx_queues;
		return (u16)hash;
	}

	if (vlan_get_protocol(skb) == htons(ETH_P_IP)) {
		iphdr = ip_hdr(skb);
		if (iphdr->protocol == IPPROTO_UDP ||
		    iphdr->protocol == IPPROTO_TCP) {
			tcphdr = tcp_hdr(skb);
			hash = calc_toeplitz_rss(ntohl(iphdr->daddr),
						 ntohl(iphdr->saddr),
						 ntohs(tcphdr->dest),
						 ntohs(tcphdr->source),
						 nic_dev->rss_hkey_user_be);
		}
	}

	return (u16)nic_dev->rss_indir_user[hash & 0xFF];
}

#if defined(HAVE_NDO_SELECT_QUEUE_SB_DEV_ONLY)
static u16 hinic_select_queue(struct net_device *netdev, struct sk_buff *skb,
				struct net_device *sb_dev)
#elif defined(HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK)
#if defined(HAVE_NDO_SELECT_QUEUE_SB_DEV)
static u16 hinic_select_queue(struct net_device *netdev, struct sk_buff *skb,
			      struct net_device *sb_dev,
			      select_queue_fallback_t fallback)
#else
static u16 hinic_select_queue(struct net_device *netdev, struct sk_buff *skb,
			      __always_unused void *accel,
			      select_queue_fallback_t fallback)
#endif
#elif defined(HAVE_NDO_SELECT_QUEUE_ACCEL)
static u16 hinic_select_queue(struct net_device *netdev, struct sk_buff *skb,
			      __always_unused void *accel)
#else
static u16 hinic_select_queue(struct net_device *netdev, struct sk_buff *skb)
#endif /* end of HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK */
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	skb->priority = skb->vlan_tci >> VLAN_PRIO_SHIFT;

	if (netdev_get_num_tc(netdev) || !nic_dev->rss_hkey_user_be)
		goto fall_back;

	if ((nic_dev->rss_hash_engine == HINIC_RSS_HASH_ENGINE_TYPE_TOEP) &&
	    test_bit(HINIC_SAME_RXTX, &nic_dev->flags))
		return select_queue_by_toeplitz(netdev, skb,
						netdev->real_num_tx_queues);

fall_back:
#if defined(HAVE_NDO_SELECT_QUEUE_SB_DEV_ONLY)
	return netdev_pick_tx(netdev, skb, NULL);
#elif defined(HAVE_NDO_SELECT_QUEUE_ACCEL_FALLBACK)
#ifdef HAVE_NDO_SELECT_QUEUE_SB_DEV
	return fallback(netdev, skb, sb_dev);
#else
	return fallback(netdev, skb);
#endif
#else
	return skb_tx_hash(netdev, skb);
#endif
}

#ifdef HAVE_NDO_GET_STATS64
#ifdef HAVE_VOID_NDO_GET_STATS64
static void hinic_get_stats64(struct net_device *netdev,
			      struct rtnl_link_stats64 *stats)
#else
static struct rtnl_link_stats64
	*hinic_get_stats64(struct net_device *netdev,
			   struct rtnl_link_stats64 *stats)
#endif

#else /* !HAVE_NDO_GET_STATS64 */
static struct net_device_stats *hinic_get_stats(struct net_device *netdev)
#endif
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
#ifndef HAVE_NDO_GET_STATS64
#ifdef HAVE_NETDEV_STATS_IN_NETDEV
	struct net_device_stats *stats = &netdev->stats;
#else
	struct net_device_stats *stats = &nic_dev->net_stats;
#endif /* HAVE_NETDEV_STATS_IN_NETDEV */
#endif /* HAVE_NDO_GET_STATS64 */
	struct hinic_txq_stats *txq_stats;
	struct hinic_rxq_stats *rxq_stats;
	struct hinic_txq *txq;
	struct hinic_rxq *rxq;
	u64 bytes, packets, dropped, errors;
	unsigned int start;
	int i;

	bytes = 0;
	packets = 0;
	dropped = 0;
	for (i = 0; i < nic_dev->max_qps; i++) {
		if (!nic_dev->txqs)
			break;

		txq = &nic_dev->txqs[i];
		txq_stats = &txq->txq_stats;
		do {
			start = u64_stats_fetch_begin(&txq_stats->syncp);
			bytes += txq_stats->bytes;
			packets += txq_stats->packets;
			dropped += txq_stats->dropped;
		} while (u64_stats_fetch_retry(&txq_stats->syncp, start));
	}
	stats->tx_packets = packets;
	stats->tx_bytes   = bytes;
	stats->tx_dropped = dropped;

	bytes = 0;
	packets = 0;
	errors = 0;
	dropped = 0;
	for (i = 0; i < nic_dev->max_qps; i++) {
		if (!nic_dev->rxqs)
			break;

		rxq = &nic_dev->rxqs[i];
		rxq_stats = &rxq->rxq_stats;
		do {
			start = u64_stats_fetch_begin(&rxq_stats->syncp);
			bytes += rxq_stats->bytes;
			packets += rxq_stats->packets;
			errors += rxq_stats->csum_errors +
				rxq_stats->other_errors;
			dropped += rxq_stats->dropped;
		} while (u64_stats_fetch_retry(&rxq_stats->syncp, start));
	}
	stats->rx_packets = packets;
	stats->rx_bytes   = bytes;
	stats->rx_errors  = errors;
	stats->rx_dropped = dropped;

#ifndef HAVE_VOID_NDO_GET_STATS64
	return stats;
#endif
}

static void hinic_tx_timeout(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u8 q_id = 0;

	HINIC_NIC_STATS_INC(nic_dev, netdev_tx_timeout);
	nicif_err(nic_dev, drv, netdev, "Tx timeout\n");

	for (q_id = 0; q_id < nic_dev->num_qps; q_id++) {
		if (!netif_xmit_stopped(netdev_get_tx_queue(netdev, q_id)))
			continue;

		nicif_info(nic_dev, drv, netdev,
			   "txq%d: sw_pi: %d, hw_ci: %d, sw_ci: %d, napi->state: 0x%lx\n",
			   q_id, hinic_dbg_get_sq_pi(nic_dev->hwdev, q_id),
			   hinic_get_sq_hw_ci(nic_dev->hwdev, q_id),
			   hinic_get_sq_local_ci(nic_dev->hwdev, q_id),
			   nic_dev->irq_cfg[q_id].napi.state);
	}
}

static int hinic_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u32 mtu = (u32)new_mtu;
	int err = 0;

	err = hinic_set_port_mtu(nic_dev->hwdev, mtu);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to change port mtu to %d\n",
			  new_mtu);
	} else {
		nicif_info(nic_dev, drv, nic_dev->netdev, "Change mtu from %d to %d\n",
			   netdev->mtu, new_mtu);
		netdev->mtu = mtu;
	}

	return err;
}

static int hinic_set_mac_addr(struct net_device *netdev, void *addr)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct sockaddr *saddr = addr;
	u16 func_id;
	int err;

	if (!FUNC_SUPPORT_CHANGE_MAC(nic_dev->hwdev)) {
		nicif_warn(nic_dev, drv, netdev,
			   "Current function don't support to set mac\n");
		return -EOPNOTSUPP;
	}

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	if (ether_addr_equal(netdev->dev_addr, saddr->sa_data)) {
		nicif_info(nic_dev, drv, netdev,
			   "Already using mac address %pM\n",
			   saddr->sa_data);
		return 0;
	}

	err = hinic_global_func_id_get(nic_dev->hwdev, &func_id);
	if (err)
		return err;

	err = hinic_update_mac(nic_dev->hwdev, netdev->dev_addr, saddr->sa_data,
			       0, func_id);
	if (err)
		return err;

	memcpy(netdev->dev_addr, saddr->sa_data, ETH_ALEN);

	nicif_info(nic_dev, drv, netdev, "Set new mac address %pM\n",
		   saddr->sa_data);

	/* TODO: vlan mac address of the device must be modified of kernel
	 * larger than 4.7 (not modified mac address of vlan)
	 */

	return 0;
}

#if (KERNEL_VERSION(3, 3, 0) > LINUX_VERSION_CODE)
static void
#else
static int
#endif
hinic_vlan_rx_add_vid(struct net_device *netdev,
		      #if (KERNEL_VERSION(3, 10, 0) <= LINUX_VERSION_CODE)
		      __always_unused __be16 proto,
		      #endif
		      u16 vid)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	unsigned long *vlan_bitmap = nic_dev->vlan_bitmap;
	u16 func_id;
	u32 col, line;
	int err;

	col = VID_COL(nic_dev, vid);
	line = VID_LINE(nic_dev, vid);

	err = hinic_global_func_id_get(nic_dev->hwdev, &func_id);
	if (err)
		goto end;

	err = hinic_add_vlan(nic_dev->hwdev, vid, func_id);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to add vlan%d\n", vid);
		goto end;
	}

	set_bit(col, &vlan_bitmap[line]);

	nicif_info(nic_dev, drv, netdev, "Add vlan %d\n", vid);

end:
#if (KERNEL_VERSION(3, 3, 0) <= LINUX_VERSION_CODE)
	return err;
#else
	return;
#endif
}

#if (KERNEL_VERSION(3, 3, 0) > LINUX_VERSION_CODE)
static void
#else
static int
#endif
hinic_vlan_rx_kill_vid(struct net_device *netdev,
		       #if (KERNEL_VERSION(3, 10, 0) <= LINUX_VERSION_CODE)
		       __always_unused __be16 proto,
		       #endif
		       u16 vid)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	unsigned long *vlan_bitmap = nic_dev->vlan_bitmap;
	u16 func_id;
	int err, col, line;

	col  = VID_COL(nic_dev, vid);
	line = VID_LINE(nic_dev, vid);

	err = hinic_global_func_id_get(nic_dev->hwdev, &func_id);
	if (err)
		goto end;

	err = hinic_del_vlan(nic_dev->hwdev, vid, func_id);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to delete vlan\n");
		goto end;
	}

	clear_bit(col, &vlan_bitmap[line]);

	nicif_info(nic_dev, drv, netdev, "Remove vlan %d\n", vid);

end:
#if (KERNEL_VERSION(3, 3, 0) <= LINUX_VERSION_CODE)
	return err;
#else
	return;
#endif
}

static int set_features(struct hinic_nic_dev *nic_dev,
			netdev_features_t pre_features,
			netdev_features_t features, bool force_change)
{
	netdev_features_t changed = force_change ? ~0 : pre_features ^ features;
#ifdef NETIF_F_HW_VLAN_CTAG_RX
	u8 rxvlan_changed = !!(changed & NETIF_F_HW_VLAN_CTAG_RX);
	u8 rxvlan_en = !!(features & NETIF_F_HW_VLAN_CTAG_RX);
#else
	u8 rxvlan_changed = !!(changed & NETIF_F_HW_VLAN_RX);
	u8 rxvlan_en = !!(features & NETIF_F_HW_VLAN_RX);
#endif
	u32 lro_timer, lro_buf_size;
	int err = 0;


	if (changed & NETIF_F_TSO) {
		err = hinic_set_tx_tso(nic_dev->hwdev,
				       !!(features & NETIF_F_TSO));
		hinic_info(nic_dev, drv, "%s tso %s\n",
			   (features & NETIF_F_TSO) ? "Enable" : "Disable",
			   err ? "failed" : "success");
	}

	if (rxvlan_changed) {
		err = hinic_set_rx_vlan_offload(nic_dev->hwdev, rxvlan_en);
		hinic_info(nic_dev, drv, "%s rxvlan %s\n",
			   rxvlan_en ? "Enable" : "Disable",
			   err ? "failed" : "success");
	}

	if (changed & NETIF_F_RXCSUM) {
		/* hw should always enable rx csum */
		u32 csum_en = HINIC_RX_CSUM_OFFLOAD_EN;

		err = hinic_set_rx_csum_offload(nic_dev->hwdev, csum_en);
		hinic_info(nic_dev, drv, "%s rx csum %s\n",
			   (features & NETIF_F_RXCSUM) ? "Enable" : "Disable",
			   err ? "failed" : "success");
	}

	if (changed & NETIF_F_LRO) {
		lro_timer = nic_dev->adaptive_cfg.lro.timer;
		lro_buf_size = nic_dev->adaptive_cfg.lro.buffer_size;

		err = hinic_set_rx_lro_state(nic_dev->hwdev,
					     !!(features & NETIF_F_LRO),
					     lro_timer,
					     lro_buf_size /
					     nic_dev->rx_buff_len);
		hinic_info(nic_dev, drv, "%s lro %s\n",
			   (features & NETIF_F_LRO) ? "Enable" : "Disable",
			   err ? "failed" : "success");
	}

	return err;
}

#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
static int hinic_set_features(struct net_device *netdev, u32 features)
#else
static int hinic_set_features(struct net_device *netdev,
			      netdev_features_t features)
#endif
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	return set_features(nic_dev, nic_dev->netdev->features,
			    features, false);
}

#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
static u32 hinic_fix_features(struct net_device *netdev, u32 features)
#else
static netdev_features_t hinic_fix_features(struct net_device *netdev,
					    netdev_features_t features)
#endif
{
	/* If Rx checksum is disabled, then LRO should also be disabled */
	if (!(features & NETIF_F_RXCSUM))
		features &= ~NETIF_F_LRO;

	return features;
}

static int hinic_set_default_hw_feature(struct hinic_nic_dev *nic_dev)
{
	int err;

	if (!HINIC_FUNC_IS_VF(nic_dev->hwdev)) {
		if (FUNC_SUPPORT_DCB(nic_dev->hwdev)) {
			err = hinic_dcb_reset_hw_config(nic_dev);
			if (err) {
				nic_err(&nic_dev->pdev->dev, "Failed to reset hw dcb configuration\n");
				return -EFAULT;
			}
		}

		if (FUNC_SUPPORT_PORT_SETTING(nic_dev->hwdev)) {
			err = hinic_reset_port_link_cfg(nic_dev->hwdev);
			if (err)
				return -EFAULT;
		}

		if (enable_bp) {
			nic_dev->bp_upper_thd = (u16)bp_upper_thd;
			nic_dev->bp_lower_thd = (u16)bp_lower_thd;
			err = hinic_set_bp_thd(nic_dev->hwdev,
					       nic_dev->bp_lower_thd);
			if (err) {
				nic_err(&nic_dev->pdev->dev,
					"Failed to set bp lower threshold\n");
				return -EFAULT;
			}

			set_bit(HINIC_BP_ENABLE, &nic_dev->flags);
		} else {
			err = hinic_disable_fw_bp(nic_dev->hwdev);
			if (err)
				return -EFAULT;

			clear_bit(HINIC_BP_ENABLE, &nic_dev->flags);
		}

		hinic_set_anti_attack(nic_dev->hwdev, true);

		if (set_link_status_follow < HINIC_LINK_FOLLOW_STATUS_MAX &&
		    FUNC_SUPPORT_PORT_SETTING(nic_dev->hwdev)) {
			err = hinic_set_link_status_follow(
				nic_dev->hwdev, set_link_status_follow);
			if (err == HINIC_MGMT_CMD_UNSUPPORTED)
				nic_warn(&nic_dev->pdev->dev,
					 "Current version of firmware don't support to set link status follow port status\n");
		}
	}

	/* enable all hw features in netdev->features */
	return set_features(nic_dev, 0, nic_dev->netdev->features, true);
}

#ifdef NETIF_F_HW_TC
#ifdef TC_MQPRIO_HW_OFFLOAD_MAX
static int hinic_setup_tc_mqprio(struct net_device *dev,
				 struct tc_mqprio_qopt *mqprio)
{
	mqprio->hw = TC_MQPRIO_HW_OFFLOAD_TCS;
	return hinic_setup_tc(dev, mqprio->num_tc);
}
#endif /* TC_MQPRIO_HW_OFFLOAD_MAX */

#if defined(HAVE_NDO_SETUP_TC_REMOVE_TC_TO_NETDEV)
static int __hinic_setup_tc(struct net_device *dev, enum tc_setup_type type,
			    void *type_data)
#elif defined(HAVE_NDO_SETUP_TC_CHAIN_INDEX)
static int __hinic_setup_tc(struct net_device *dev, __always_unused u32 handle,
			    u32 chain_index, __always_unused __be16 proto,
			    struct tc_to_netdev *tc)
#else
static int __hinic_setup_tc(struct net_device *dev, __always_unused u32 handle,
			    __always_unused __be16 proto,
			    struct tc_to_netdev *tc)
#endif
{
#ifndef HAVE_NDO_SETUP_TC_REMOVE_TC_TO_NETDEV
	unsigned int type = tc->type;

#ifdef HAVE_NDO_SETUP_TC_CHAIN_INDEX
	if (chain_index)
		return -EOPNOTSUPP;

#endif
#endif
	switch (type) {
	case TC_SETUP_QDISC_MQPRIO:
#if defined(HAVE_NDO_SETUP_TC_REMOVE_TC_TO_NETDEV)
		return hinic_setup_tc_mqprio(dev, type_data);
#elif defined(TC_MQPRIO_HW_OFFLOAD_MAX)
		return hinic_setup_tc_mqprio(dev, tc->mqprio);
#else
		return hinic_setup_tc(dev, tc->tc);
#endif
	default:
		return -EOPNOTSUPP;
	}
}
#endif /* NETIF_F_HW_TC */

#ifdef CONFIG_NET_POLL_CONTROLLER
static void hinic_netpoll(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u16 i;

	for (i = 0; i < nic_dev->num_qps; i++)
		napi_schedule(&nic_dev->irq_cfg[i].napi);
}
#endif /* CONFIG_NET_POLL_CONTROLLER */

static int hinic_uc_sync(struct net_device *netdev, u8 *addr)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u16 func_id;
	int err;

	err = hinic_global_func_id_get(nic_dev->hwdev, &func_id);
	if (err)
		return err;

	err = hinic_set_mac(nic_dev->hwdev, addr, 0, func_id);
	return err;
}

static int hinic_uc_unsync(struct net_device *netdev, u8 *addr)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u16 func_id;
	int err;

	/* The addr is in use */
	if (ether_addr_equal(addr, netdev->dev_addr))
		return 0;

	err = hinic_global_func_id_get(nic_dev->hwdev, &func_id);
	if (err)
		return err;

	err = hinic_del_mac(nic_dev->hwdev, addr, 0, func_id);
	return err;
}

static void hinic_clean_mac_list_filter(struct hinic_nic_dev *nic_dev)
{
	struct net_device *netdev = nic_dev->netdev;
	struct hinic_mac_filter *f, *ftmp;

	list_for_each_entry_safe(f, ftmp, &nic_dev->uc_filter_list, list) {
		if (f->state == HINIC_MAC_HW_SYNCED)
			hinic_uc_unsync(netdev, f->addr);
		list_del(&f->list);
		kfree(f);
	}

	list_for_each_entry_safe(f, ftmp, &nic_dev->mc_filter_list, list) {
		if (f->state == HINIC_MAC_HW_SYNCED)
			hinic_uc_unsync(netdev, f->addr);
		list_del(&f->list);
		kfree(f);
	}
}

static struct hinic_mac_filter *hinic_find_mac(struct list_head *filter_list,
					       u8 *addr)
{
	struct hinic_mac_filter *f;

	list_for_each_entry(f, filter_list, list) {
		if (ether_addr_equal(addr, f->addr))
			return f;
	}
	return NULL;
}

static struct hinic_mac_filter
	*hinic_add_filter(struct hinic_nic_dev *nic_dev,
			  struct list_head *mac_filter_list, u8 *addr)
{
	struct hinic_mac_filter *f;

	f = kzalloc(sizeof(*f), GFP_ATOMIC);
	if (!f)
		goto out;

	memcpy(f->addr, addr, ETH_ALEN);

	INIT_LIST_HEAD(&f->list);
	list_add_tail(&f->list, mac_filter_list);

	f->state = HINIC_MAC_WAIT_HW_SYNC;
	set_bit(HINIC_MAC_FILTER_CHANGED, &nic_dev->flags);

out:
	return f;
}

static void hinic_del_filter(struct hinic_nic_dev *nic_dev,
			     struct hinic_mac_filter *f)
{
	set_bit(HINIC_MAC_FILTER_CHANGED, &nic_dev->flags);

	if (f->state == HINIC_MAC_WAIT_HW_SYNC) {
		/* have not added to hw, delete it directly */
		list_del(&f->list);
		kfree(f);
		return;
	}

	f->state = HINIC_MAC_WAIT_HW_UNSYNC;
}

static struct hinic_mac_filter
	*hinic_mac_filter_entry_clone(struct hinic_mac_filter *src)
{
	struct hinic_mac_filter *f;

	f = kzalloc(sizeof(*f), GFP_ATOMIC);
	if (!f)
		return NULL;

	*f = *src;
	INIT_LIST_HEAD(&f->list);

	return f;
}

static void hinic_undo_del_filter_entries(struct list_head *filter_list,
					  struct list_head *from)
{
	struct hinic_mac_filter *f, *ftmp;

	list_for_each_entry_safe(f, ftmp, from, list) {
		if (hinic_find_mac(filter_list, f->addr))
			continue;

		if (f->state == HINIC_MAC_HW_SYNCED)
			f->state = HINIC_MAC_WAIT_HW_UNSYNC;

		list_move_tail(&f->list, filter_list);
	}
}

static void hinic_undo_add_filter_entries(struct list_head *filter_list,
					  struct list_head *from)
{
	struct hinic_mac_filter *f, *ftmp, *tmp;

	list_for_each_entry_safe(f, ftmp, from, list) {
		tmp = hinic_find_mac(filter_list, f->addr);
		if (tmp && tmp->state == HINIC_MAC_HW_SYNCED)
			tmp->state = HINIC_MAC_WAIT_HW_SYNC;
	}
}

static void hinic_cleanup_filter_list(struct list_head *head)
{
	struct hinic_mac_filter *f, *ftmp;

	list_for_each_entry_safe(f, ftmp, head, list) {
		list_del(&f->list);
		kfree(f);
	}
}

static int hinic_mac_filter_sync_hw(struct hinic_nic_dev *nic_dev,
				    struct list_head *del_list,
				    struct list_head *add_list)
{
	struct net_device *netdev = nic_dev->netdev;
	struct hinic_mac_filter *f, *ftmp;
	int err = 0, add_count = 0;

	if (!list_empty(del_list)) {
		list_for_each_entry_safe(f, ftmp, del_list, list) {
			err = hinic_uc_unsync(netdev, f->addr);
			if (err) { /* ignore errors when delete mac */
				nic_err(&nic_dev->pdev->dev, "Failed to delete mac\n");
			}

			list_del(&f->list);
			kfree(f);
		}
	}

	if (!list_empty(add_list)) {
		list_for_each_entry_safe(f, ftmp, add_list, list) {
			err = hinic_uc_sync(netdev, f->addr);
			if (err) {
				nic_err(&nic_dev->pdev->dev, "Failed to add mac\n");
				return err;
			}

			add_count++;
			list_del(&f->list);
			kfree(f);
		}
	}

	return add_count;
}

static int hinic_mac_filter_sync(struct hinic_nic_dev *nic_dev,
				 struct list_head *mac_filter_list, bool uc)
{
	struct net_device *netdev = nic_dev->netdev;
	struct list_head tmp_del_list, tmp_add_list;
	struct hinic_mac_filter *f, *ftmp, *fclone;
	int err = 0, add_count = 0;

	INIT_LIST_HEAD(&tmp_del_list);
	INIT_LIST_HEAD(&tmp_add_list);

	list_for_each_entry_safe(f, ftmp, mac_filter_list, list) {
		if (f->state != HINIC_MAC_WAIT_HW_UNSYNC)
			continue;

		f->state = HINIC_MAC_HW_UNSYNCED;
		list_move_tail(&f->list, &tmp_del_list);
	}

	list_for_each_entry_safe(f, ftmp, mac_filter_list, list) {
		if (f->state != HINIC_MAC_WAIT_HW_SYNC)
			continue;

		fclone = hinic_mac_filter_entry_clone(f);
		if (!fclone) {
			err = -ENOMEM;
			break;
		}

		f->state = HINIC_MAC_HW_SYNCED;
		list_add_tail(&fclone->list, &tmp_add_list);
	}

	if (err) {
		hinic_undo_del_filter_entries(mac_filter_list, &tmp_del_list);
		hinic_undo_add_filter_entries(mac_filter_list, &tmp_add_list);
		nicif_err(nic_dev, drv, netdev, "Failed to clone mac_filter_entry\n");
	}

	if (err) {
		hinic_cleanup_filter_list(&tmp_del_list);
		hinic_cleanup_filter_list(&tmp_add_list);
		return -ENOMEM;
	}

	add_count =
		hinic_mac_filter_sync_hw(nic_dev, &tmp_del_list, &tmp_add_list);
	if (list_empty(&tmp_add_list))
		return add_count;

	/* there are errors when add mac to hw, delete all mac in hw */
	hinic_undo_add_filter_entries(mac_filter_list, &tmp_add_list);
	/* VF don't support to enter promisc mode,
	 * so we can't delete any other uc mac
	 */
	if (!HINIC_FUNC_IS_VF(nic_dev->hwdev) || !uc) {
		list_for_each_entry_safe(f, ftmp, mac_filter_list, list) {
			if (f->state != HINIC_MAC_HW_SYNCED)
				continue;

			fclone = hinic_mac_filter_entry_clone(f);
			if (!fclone)
				break;

			f->state = HINIC_MAC_WAIT_HW_SYNC;
			list_add_tail(&fclone->list, &tmp_del_list);
		}
	}

	hinic_cleanup_filter_list(&tmp_add_list);
	hinic_mac_filter_sync_hw(nic_dev, &tmp_del_list, &tmp_add_list);

	/* need to enter promisc/allmulti mode */
	return -ENOMEM;
}

static void hinic_mac_filter_sync_all(struct hinic_nic_dev *nic_dev)
{
	struct net_device *netdev = nic_dev->netdev;
	int add_count;

	if (test_bit(HINIC_MAC_FILTER_CHANGED, &nic_dev->flags)) {
		clear_bit(HINIC_MAC_FILTER_CHANGED, &nic_dev->flags);
		add_count = hinic_mac_filter_sync(nic_dev,
						  &nic_dev->uc_filter_list,
						  true);
		if (add_count < 0 && !HINIC_FUNC_IS_VF(nic_dev->hwdev)) {
			set_bit(HINIC_PROMISC_FORCE_ON, &nic_dev->rx_mod_state);
			nicif_info(nic_dev, drv, netdev, "Promisc mode forced on\n");
		} else if (add_count) {
			clear_bit(HINIC_PROMISC_FORCE_ON,
				  &nic_dev->rx_mod_state);
		}

		add_count = hinic_mac_filter_sync(nic_dev,
						  &nic_dev->mc_filter_list,
						  false);
		if (add_count < 0) {
			set_bit(HINIC_ALLMULTI_FORCE_ON,
				&nic_dev->rx_mod_state);
			nicif_info(nic_dev, drv, netdev, "All multicast mode forced on\n");
		} else if (add_count) {
			clear_bit(HINIC_ALLMULTI_FORCE_ON,
				  &nic_dev->rx_mod_state);
		}
	}
}

#define HINIC_DEFAULT_RX_MODE	(HINIC_RX_MODE_UC | HINIC_RX_MODE_MC | \
				HINIC_RX_MODE_BC)

static void hinic_update_mac_filter(struct hinic_nic_dev *nic_dev,
				    struct netdev_hw_addr_list *src_list,
				    struct list_head *filter_list)
{
	struct netdev_hw_addr *ha;
	struct hinic_mac_filter *f, *ftmp, *filter;

	/* add addr if not already in the filter list */
	netif_addr_lock_bh(nic_dev->netdev);
	netdev_hw_addr_list_for_each(ha, src_list) {
		filter = hinic_find_mac(filter_list, ha->addr);
		if (!filter)
			hinic_add_filter(nic_dev, filter_list, ha->addr);
		else if (filter->state == HINIC_MAC_WAIT_HW_UNSYNC)
			filter->state = HINIC_MAC_HW_SYNCED;
	}
	netif_addr_unlock_bh(nic_dev->netdev);

	/* delete addr if not in netdev list */
	list_for_each_entry_safe(f, ftmp, filter_list, list) {
		bool found = false;

		netif_addr_lock_bh(nic_dev->netdev);
		netdev_hw_addr_list_for_each(ha, src_list)
			if (ether_addr_equal(ha->addr, f->addr)) {
				found = true;
				break;
			}
		netif_addr_unlock_bh(nic_dev->netdev);

		if (found)
			continue;

		hinic_del_filter(nic_dev, f);
	}
}

#ifndef NETDEV_HW_ADDR_T_MULTICAST
static void hinic_update_mc_filter(struct hinic_nic_dev *nic_dev,
				   struct list_head *filter_list)
{
	struct dev_mc_list *ha;
	struct hinic_mac_filter *f, *ftmp, *filter;

	/* add addr if not already in the filter list */
	netif_addr_lock_bh(nic_dev->netdev);
	netdev_for_each_mc_addr(ha, nic_dev->netdev) {
		filter = hinic_find_mac(filter_list, ha->da_addr);
		if (!filter)
			hinic_add_filter(nic_dev, filter_list, ha->da_addr);
		else if (filter->state == HINIC_MAC_WAIT_HW_UNSYNC)
			filter->state = HINIC_MAC_HW_SYNCED;
	}
	netif_addr_unlock_bh(nic_dev->netdev);
	/* delete addr if not in netdev list */
	list_for_each_entry_safe(f, ftmp, filter_list, list) {
		bool found = false;

		netif_addr_lock_bh(nic_dev->netdev);
		netdev_for_each_mc_addr(ha, nic_dev->netdev)
			if (ether_addr_equal(ha->da_addr, f->addr)) {
				found = true;
				break;
			}
		netif_addr_unlock_bh(nic_dev->netdev);

		if (found)
			continue;

		hinic_del_filter(nic_dev, f);
	}
}
#endif

static void __update_mac_filter(struct hinic_nic_dev *nic_dev)
{
	struct net_device *netdev = nic_dev->netdev;

	if (test_and_clear_bit(HINIC_UPDATE_MAC_FILTER, &nic_dev->flags)) {
		hinic_update_mac_filter(nic_dev, &netdev->uc,
					&nic_dev->uc_filter_list);
#ifdef NETDEV_HW_ADDR_T_MULTICAST
		hinic_update_mac_filter(nic_dev, &netdev->mc,
					&nic_dev->mc_filter_list);
#else
		hinic_update_mc_filter(nic_dev, &nic_dev->mc_filter_list);
#endif
	}
}

static void hinic_set_rx_mode_work(struct work_struct *work)
{
	struct hinic_nic_dev *nic_dev =
			container_of(work, struct hinic_nic_dev, rx_mode_work);
	struct net_device *netdev = nic_dev->netdev;
	int promisc_en = 0, allmulti_en = 0;
	int err = 0;

	__update_mac_filter(nic_dev);

	hinic_mac_filter_sync_all(nic_dev);

	/* VF don't support to enter promisc mode */
	if (!HINIC_FUNC_IS_VF(nic_dev->hwdev)) {
		promisc_en = !!(netdev->flags & IFF_PROMISC) ||
			test_bit(HINIC_PROMISC_FORCE_ON,
				 &nic_dev->rx_mod_state);
	}

	allmulti_en = !!(netdev->flags & IFF_ALLMULTI) ||
		test_bit(HINIC_ALLMULTI_FORCE_ON, &nic_dev->rx_mod_state);

	if (promisc_en !=
	    test_bit(HINIC_HW_PROMISC_ON, &nic_dev->rx_mod_state) ||
	    allmulti_en !=
	    test_bit(HINIC_HW_ALLMULTI_ON, &nic_dev->rx_mod_state)) {
		enum hinic_rx_mod rx_mod = HINIC_DEFAULT_RX_MODE;

		rx_mod |= (promisc_en ? HINIC_RX_MODE_PROMISC : 0);
		rx_mod |= (allmulti_en ? HINIC_RX_MODE_MC_ALL : 0);

		/* FOR DEBUG */
		if (promisc_en !=
		    test_bit(HINIC_HW_PROMISC_ON, &nic_dev->rx_mod_state))
			nicif_info(nic_dev, drv, netdev,
				   "%s promisc mode\n",
				   promisc_en ? "Enter" : "Left");
		if (allmulti_en !=
		    test_bit(HINIC_HW_ALLMULTI_ON, &nic_dev->rx_mod_state))
			nicif_info(nic_dev, drv, netdev,
				   "%s all_multi mode\n",
				   allmulti_en ? "Enter" : "Left");

		err = hinic_set_rx_mode(nic_dev->hwdev, rx_mod);
		if (!err) {
			promisc_en ?
			set_bit(HINIC_HW_PROMISC_ON, &nic_dev->rx_mod_state) :
			clear_bit(HINIC_HW_PROMISC_ON, &nic_dev->rx_mod_state);

			allmulti_en ?
			set_bit(HINIC_HW_ALLMULTI_ON, &nic_dev->rx_mod_state) :
			clear_bit(HINIC_HW_ALLMULTI_ON, &nic_dev->rx_mod_state);
		} else {
			nicif_err(nic_dev, drv, netdev, "Failed to set rx_mode\n");
		}
	}
}

static void hinic_nic_set_rx_mode(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (netdev_uc_count(netdev) != nic_dev->netdev_uc_cnt ||
	    netdev_mc_count(netdev) != nic_dev->netdev_mc_cnt) {
		set_bit(HINIC_UPDATE_MAC_FILTER, &nic_dev->flags);
		nic_dev->netdev_uc_cnt = netdev_uc_count(netdev);
		nic_dev->netdev_mc_cnt = netdev_mc_count(netdev);
	}

	if (FUNC_SUPPORT_RX_MODE(nic_dev->hwdev))
		queue_work(nic_dev->workq, &nic_dev->rx_mode_work);
}

static const struct net_device_ops hinic_netdev_ops = {
	.ndo_open = hinic_open,
	.ndo_stop = hinic_close,
	.ndo_start_xmit = hinic_xmit_frame,

#ifdef HAVE_NDO_GET_STATS64
	.ndo_get_stats64 =  hinic_get_stats64,
#else
	.ndo_get_stats = hinic_get_stats,
#endif /* HAVE_NDO_GET_STATS64 */

	.ndo_tx_timeout = hinic_tx_timeout,
	.ndo_select_queue = hinic_select_queue,
#ifdef HAVE_RHEL7_NETDEV_OPS_EXT_NDO_CHANGE_MTU
	.extended.ndo_change_mtu = hinic_change_mtu,
#else
	.ndo_change_mtu = hinic_change_mtu,
#endif
	.ndo_set_mac_address = hinic_set_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
#if defined(NETIF_F_HW_VLAN_TX) || defined(NETIF_F_HW_VLAN_CTAG_TX)
	.ndo_vlan_rx_add_vid = hinic_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = hinic_vlan_rx_kill_vid,
#endif
#ifdef HAVE_RHEL7_NET_DEVICE_OPS_EXT
	/* RHEL7 requires this to be defined to enable extended ops.  RHEL7
	 * uses the function get_ndo_ext to retrieve offsets for extended
	 * fields from with the net_device_ops struct and ndo_size is checked
	 * to determine whether or not the offset is valid.
	 */
	.ndo_size		= sizeof(const struct net_device_ops),
#endif
#ifdef IFLA_VF_MAX
	.ndo_set_vf_mac		= hinic_ndo_set_vf_mac,
#ifdef HAVE_RHEL7_NETDEV_OPS_EXT_NDO_SET_VF_VLAN
	.extended.ndo_set_vf_vlan = hinic_ndo_set_vf_vlan,
#else
	.ndo_set_vf_vlan	= hinic_ndo_set_vf_vlan,
#endif
#ifdef HAVE_NDO_SET_VF_MIN_MAX_TX_RATE
	.ndo_set_vf_rate	= hinic_ndo_set_vf_bw,
#else
	.ndo_set_vf_tx_rate	= hinic_ndo_set_vf_bw,
#endif /* HAVE_NDO_SET_VF_MIN_MAX_TX_RATE */
#ifdef HAVE_VF_SPOOFCHK_CONFIGURE
	.ndo_set_vf_spoofchk	= hinic_ndo_set_vf_spoofchk,
#endif
#ifdef HAVE_NDO_SET_VF_TRUST
#ifdef HAVE_RHEL7_NET_DEVICE_OPS_EXT
	.extended.ndo_set_vf_trust = hinic_ndo_set_vf_trust,
#else
	.ndo_set_vf_trust	= hinic_ndo_set_vf_trust,
#endif /* HAVE_RHEL7_NET_DEVICE_OPS_EXT */
#endif /* HAVE_NDO_SET_VF_TRUST */

	.ndo_get_vf_config	= hinic_ndo_get_vf_config,
#endif

#ifdef HAVE_RHEL7_NETDEV_OPS_EXT_NDO_SETUP_TC
	.extended.ndo_setup_tc_rh	= __hinic_setup_tc,
#else
#ifdef HAVE_SETUP_TC
#ifdef NETIF_F_HW_TC
	.ndo_setup_tc		= __hinic_setup_tc,
#else
	.ndo_setup_tc		= hinic_setup_tc,
#endif /* NETIF_F_HW_TC */
#endif /* HAVE_SETUP_TC */
#endif

#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = hinic_netpoll,
#endif /* CONFIG_NET_POLL_CONTROLLER */

	.ndo_set_rx_mode = hinic_nic_set_rx_mode,

#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
};

/* RHEL6 keeps these operations in a separate structure */
static const struct net_device_ops_ext hinic_netdev_ops_ext = {
	.size = sizeof(struct net_device_ops_ext),
#endif /* HAVE_RHEL6_NET_DEVICE_OPS_EXT */

#ifdef HAVE_NDO_SET_VF_LINK_STATE
	.ndo_set_vf_link_state	= hinic_ndo_set_vf_link_state,
#endif

#ifdef HAVE_NDO_SET_FEATURES
	.ndo_fix_features = hinic_fix_features,
	.ndo_set_features = hinic_set_features,
#endif /* HAVE_NDO_SET_FEATURES */
};

static const struct net_device_ops hinicvf_netdev_ops = {
	.ndo_open = hinic_open,
	.ndo_stop = hinic_close,
	.ndo_start_xmit = hinic_xmit_frame,

#ifdef HAVE_NDO_GET_STATS64
	.ndo_get_stats64 =  hinic_get_stats64,
#else
	.ndo_get_stats = hinic_get_stats,
#endif /* HAVE_NDO_GET_STATS64 */

	.ndo_tx_timeout = hinic_tx_timeout,
	.ndo_select_queue = hinic_select_queue,

#ifdef HAVE_RHEL7_NET_DEVICE_OPS_EXT
	/* RHEL7 requires this to be defined to enable extended ops.  RHEL7
	 * uses the function get_ndo_ext to retrieve offsets for extended
	 * fields from with the net_device_ops struct and ndo_size is checked
	 * to determine whether or not the offset is valid.
	 */
	 .ndo_size = sizeof(const struct net_device_ops),
#endif

#ifdef HAVE_RHEL7_NETDEV_OPS_EXT_NDO_CHANGE_MTU
	.extended.ndo_change_mtu = hinic_change_mtu,
#else
	.ndo_change_mtu = hinic_change_mtu,
#endif
	.ndo_set_mac_address = hinic_set_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
#if defined(NETIF_F_HW_VLAN_TX) || defined(NETIF_F_HW_VLAN_CTAG_TX)
	.ndo_vlan_rx_add_vid = hinic_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = hinic_vlan_rx_kill_vid,
#endif

#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = hinic_netpoll,
#endif /* CONFIG_NET_POLL_CONTROLLER */

	.ndo_set_rx_mode = hinic_nic_set_rx_mode,

#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
};

/* RHEL6 keeps these operations in a separate structure */
static const struct net_device_ops_ext hinicvf_netdev_ops_ext = {
	.size = sizeof(struct net_device_ops_ext),
#endif /* HAVE_RHEL6_NET_DEVICE_OPS_EXT */

#ifdef HAVE_NDO_SET_FEATURES
	.ndo_fix_features = hinic_fix_features,
	.ndo_set_features = hinic_set_features,
#endif /* HAVE_NDO_SET_FEATURES */
};

static void netdev_feature_init(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

#ifdef HAVE_NDO_SET_FEATURES
#ifndef HAVE_RHEL6_NET_DEVICE_OPS_EXT
	netdev_features_t hw_features;
#else
	u32 hw_features;
#endif
#endif

	netdev->features = NETIF_F_SG | NETIF_F_HIGHDMA |
			   NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
			   NETIF_F_TSO |
			   NETIF_F_TSO6 | NETIF_F_RXCSUM;

	if (FUNC_SUPPORT_SCTP_CRC(nic_dev->hwdev))
		netdev->features |= NETIF_F_SCTP_CRC;

	netdev->vlan_features = netdev->features;

	if (FUNC_SUPPORT_ENCAP_TSO_CSUM(nic_dev->hwdev)) {
#ifdef HAVE_ENCAPSULATION_TSO
		netdev->features |= NETIF_F_GSO_UDP_TUNNEL |
				    NETIF_F_GSO_UDP_TUNNEL_CSUM;
#endif /* HAVE_ENCAPSULATION_TSO */
	}

	if (FUNC_SUPPORT_HW_VLAN(nic_dev->hwdev)) {
#if defined(NETIF_F_HW_VLAN_CTAG_TX)
		netdev->features |= NETIF_F_HW_VLAN_CTAG_TX;
#elif defined(NETIF_F_HW_VLAN_TX)
		netdev->features |= NETIF_F_HW_VLAN_TX;
#endif

#if defined(NETIF_F_HW_VLAN_CTAG_RX)
		netdev->features |= NETIF_F_HW_VLAN_CTAG_RX;
#elif defined(NETIF_F_HW_VLAN_RX)
		netdev->features |= NETIF_F_HW_VLAN_RX;
#endif
	}

#ifdef HAVE_NDO_SET_FEATURES
	/* copy netdev features into list of user selectable features */
#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
	hw_features = get_netdev_hw_features(netdev);
#else
	hw_features = netdev->hw_features;
#endif
	hw_features |= netdev->features;
#endif
	if (FUNC_SUPPORT_LRO(nic_dev->hwdev)) {
		/* LRO is disable in default, only set hw features */
		hw_features |= NETIF_F_LRO;

		/* Enable LRO */
		if (nic_dev->adaptive_cfg.lro.enable &&
		    !HINIC_FUNC_IS_VF(nic_dev->hwdev))
			netdev->features |= NETIF_F_LRO;
	}


#ifdef HAVE_NDO_SET_FEATURES
#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
	set_netdev_hw_features(netdev, hw_features);
#else
	netdev->hw_features = hw_features;
#endif
#endif

/* Set after hw_features because this could not be part of hw_features */
#if defined(NETIF_F_HW_VLAN_CTAG_FILTER)
	netdev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;
#elif defined(NETIF_F_HW_VLAN_FILTER)
	netdev->features |= NETIF_F_HW_VLAN_FILTER;
#endif

#ifdef IFF_UNICAST_FLT
	netdev->priv_flags |= IFF_UNICAST_FLT;
#endif

	if (FUNC_SUPPORT_ENCAP_TSO_CSUM(nic_dev->hwdev)) {
#ifdef HAVE_ENCAPSULATION_CSUM
		netdev->hw_enc_features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM
			       | NETIF_F_SCTP_CRC | NETIF_F_SG;
#ifdef HAVE_ENCAPSULATION_TSO
		netdev->hw_enc_features |= NETIF_F_TSO | NETIF_F_TSO6
			       | NETIF_F_TSO_ECN
			       | NETIF_F_GSO_UDP_TUNNEL_CSUM
			       | NETIF_F_GSO_UDP_TUNNEL;
#endif /* HAVE_ENCAPSULATION_TSO */
#endif /* HAVE_ENCAPSULATION_CSUM */
	}
}

#define MOD_PARA_VALIDATE_NUM_QPS(nic_dev, num_qps, out_qps)	{	\
	if ((num_qps) > nic_dev->max_qps)				\
		nic_warn(&nic_dev->pdev->dev,				\
			 "Module Parameter %s value %d is out of range, "\
			 "Maximum value for the device: %d, using %d\n",\
			 #num_qps, num_qps, nic_dev->max_qps,		\
			 nic_dev->max_qps);				\
	if (!(num_qps) || (num_qps) > nic_dev->max_qps)			\
		out_qps = nic_dev->max_qps;				\
	else								\
		out_qps = num_qps;					\
}

static void hinic_try_to_enable_rss(struct hinic_nic_dev *nic_dev)
{
	u8 prio_tc[HINIC_DCB_UP_MAX] = {0};
	int i, node, err = 0;
	u16 num_cpus = 0;
	enum hinic_service_mode service_mode =
					hinic_get_service_mode(nic_dev->hwdev);

	nic_dev->max_qps = hinic_func_max_nic_qnum(nic_dev->hwdev);
	if (nic_dev->max_qps <= 1) {
		clear_bit(HINIC_RSS_ENABLE, &nic_dev->flags);
		nic_dev->rss_limit = nic_dev->max_qps;
		nic_dev->num_qps = nic_dev->max_qps;
		nic_dev->num_rss = nic_dev->max_qps;

		return;
	}

	err = hinic_rss_template_alloc(nic_dev->hwdev, &nic_dev->rss_tmpl_idx);
	if (err) {
		if (err == -ENOSPC)
			nic_warn(&nic_dev->pdev->dev,
				 "Failed to alloc tmpl_idx for rss, table is full\n");
		else
			nic_err(&nic_dev->pdev->dev,
				"Failed to alloc tmpl_idx for rss, can't enable rss for this function\n");
		clear_bit(HINIC_RSS_ENABLE, &nic_dev->flags);
		nic_dev->max_qps = 1;
		nic_dev->rss_limit = nic_dev->max_qps;
		nic_dev->num_qps = nic_dev->max_qps;
		nic_dev->num_rss = nic_dev->max_qps;

		return;
	}

	set_bit(HINIC_RSS_ENABLE, &nic_dev->flags);

	nic_dev->max_qps = hinic_func_max_nic_qnum(nic_dev->hwdev);

	MOD_PARA_VALIDATE_NUM_QPS(nic_dev, num_qps, nic_dev->num_qps);

	/* To reduce memory footprint in ovs mode.
	 * VF can't get board info correctly with early pf driver.
	 */
	if ((hinic_get_func_mode(nic_dev->hwdev) == FUNC_MOD_NORMAL_HOST) &&
	    service_mode == HINIC_WORK_MODE_OVS &&
	    hinic_func_type(nic_dev->hwdev) != TYPE_VF)
		MOD_PARA_VALIDATE_NUM_QPS(nic_dev, ovs_num_qps,
					  nic_dev->num_qps);

	for (i = 0; i < (int)num_online_cpus(); i++) {
		node = (int)cpu_to_node(i);
		if (node == dev_to_node(&nic_dev->pdev->dev))
			num_cpus++;
	}

	if (!num_cpus)
		num_cpus = (u16)num_online_cpus();

	nic_dev->num_qps = min_t(u16, nic_dev->num_qps, num_cpus);


	nic_dev->rss_limit = nic_dev->num_qps;
	nic_dev->num_rss = nic_dev->num_qps;

	hinic_init_rss_parameters(nic_dev->netdev);
	hinic_set_hw_rss_parameters(nic_dev->netdev, 0, 0, prio_tc);
}

static int hinic_sw_init(struct hinic_nic_dev *adapter)
{
	struct net_device *netdev = adapter->netdev;
	u16 func_id;
	int err = 0;

	sema_init(&adapter->port_state_sem, 1);

	err = hinic_dcb_init(adapter);
	if (err) {
		nic_err(&adapter->pdev->dev, "Failed to init dcb\n");
		return -EFAULT;
	}

	if (HINIC_FUNC_IS_VF(adapter->hwdev)) {
		err = hinic_sq_cos_mapping(netdev);
		if (err) {
			nic_err(&adapter->pdev->dev, "Failed to set sq_cos_mapping\n");
			return -EFAULT;
		}
	}

	adapter->sq_depth = HINIC_SQ_DEPTH;
	adapter->rq_depth = HINIC_RQ_DEPTH;

	hinic_try_to_enable_rss(adapter);


	err = hinic_get_default_mac(adapter->hwdev, netdev->dev_addr);
	if (err) {
		nic_err(&adapter->pdev->dev, "Failed to get MAC address\n");
		goto get_mac_err;
	}

	if (!is_valid_ether_addr(netdev->dev_addr)) {
		if (!HINIC_FUNC_IS_VF(adapter->hwdev)) {
			nic_err(&adapter->pdev->dev, "Invalid MAC address\n");
			err = -EIO;
			goto err_mac;
		}

		nic_info(&adapter->pdev->dev, "Invalid MAC address %pM, using random\n",
			 netdev->dev_addr);
		eth_hw_addr_random(netdev);
	}

	err = hinic_global_func_id_get(adapter->hwdev, &func_id);
	if (err)
		goto func_id_err;

	err = hinic_set_mac(adapter->hwdev, netdev->dev_addr, 0, func_id);
	/* When this is VF driver, we must consider that PF has already set VF
	 * MAC, and we can't consider this condition is error status during
	 * driver probe procedure.
	 */
	if (err && err != HINIC_PF_SET_VF_ALREADY) {
		nic_err(&adapter->pdev->dev, "Failed to set default MAC\n");
		goto set_mac_err;
	}

	/* MTU range: 256 - 9600 */
#ifdef HAVE_NETDEVICE_MIN_MAX_MTU
	netdev->min_mtu = HINIC_MIN_MTU_SIZE;
	netdev->max_mtu = HINIC_MAX_JUMBO_FRAME_SIZE;
#endif

#ifdef HAVE_NETDEVICE_EXTENDED_MIN_MAX_MTU
	netdev->extended->min_mtu = HINIC_MIN_MTU_SIZE;
	netdev->extended->max_mtu = HINIC_MAX_JUMBO_FRAME_SIZE;
#endif
	return 0;

set_mac_err:
func_id_err:
err_mac:
get_mac_err:
	if (test_bit(HINIC_RSS_ENABLE, &adapter->flags))
		hinic_rss_template_free(adapter->hwdev, adapter->rss_tmpl_idx);

	return err;
}

static void hinic_assign_netdev_ops(struct hinic_nic_dev *adapter)
{
	if (!HINIC_FUNC_IS_VF(adapter->hwdev)) {
		adapter->netdev->netdev_ops = &hinic_netdev_ops;
#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
		set_netdev_ops_ext(adapter->netdev, &hinic_netdev_ops_ext);
#endif /* HAVE_RHEL6_NET_DEVICE_OPS_EXT */
		if (FUNC_SUPPORT_DCB(adapter->hwdev))
			adapter->netdev->dcbnl_ops = &hinic_dcbnl_ops;
		hinic_set_ethtool_ops(adapter->netdev);
	} else {
		adapter->netdev->netdev_ops = &hinicvf_netdev_ops;
#ifdef HAVE_RHEL6_NET_DEVICE_OPS_EXT
		set_netdev_ops_ext(adapter->netdev, &hinicvf_netdev_ops_ext);
#endif /* HAVE_RHEL6_NET_DEVICE_OPS_EXT */
		hinicvf_set_ethtool_ops(adapter->netdev);
	}
	adapter->netdev->watchdog_timeo = 5 * HZ;
}

#define HINIC_DFT_PG_10GE_TXRX_MSIX_PENDING_LIMIT	1
#define HINIC_DFT_PG_10GE_TXRX_MSIX_COALESC_TIMER	1
#define HINIC_DFT_PG_25GE_TXRX_MSIX_PENDING_LIMIT	2
#define HINIC_DFT_PG_25GE_TXRX_MSIX_COALESC_TIMER	2
#define HINIC_DFT_PG_ARM_25GE_TXRX_MSIX_COALESC_TIMER	3
#define HINIC_DFT_PG_100GE_TXRX_MSIX_PENDING_LIMIT	2
#define HINIC_DFT_PG_100GE_TXRX_MSIX_COALESC_TIMER	2
#define HINIC_DFT_PG_ARM_100GE_TXRX_MSIX_COALESC_TIMER	3

static void update_queue_coal_param(struct hinic_nic_dev *nic_dev,
				    struct pci_device_id *id, u16 qid)
{
	struct hinic_intr_coal_info *info = NULL;

	info = &nic_dev->intr_coalesce[qid];
	if (!nic_dev->intr_coal_set_flag) {
		switch (id->driver_data) {
		case HINIC_BOARD_PG_TP_10GE:
			info->pending_limt =
			HINIC_DFT_PG_10GE_TXRX_MSIX_PENDING_LIMIT;
			info->coalesce_timer_cfg =
			HINIC_DFT_PG_10GE_TXRX_MSIX_COALESC_TIMER;
			break;
		case HINIC_BOARD_PG_SM_25GE:
			info->pending_limt =
			HINIC_DFT_PG_25GE_TXRX_MSIX_PENDING_LIMIT;
#if   defined(__aarch64__)
			info->coalesce_timer_cfg =
			HINIC_DFT_PG_ARM_25GE_TXRX_MSIX_COALESC_TIMER;
#else
			info->coalesce_timer_cfg =
			HINIC_DFT_PG_25GE_TXRX_MSIX_COALESC_TIMER;
#endif
			break;
		case HINIC_BOARD_PG_100GE:
			info->pending_limt =
			HINIC_DFT_PG_100GE_TXRX_MSIX_PENDING_LIMIT;
#if   defined(__aarch64__)
			info->coalesce_timer_cfg =
			HINIC_DFT_PG_ARM_100GE_TXRX_MSIX_COALESC_TIMER;
#else
			info->coalesce_timer_cfg =
			HINIC_DFT_PG_100GE_TXRX_MSIX_COALESC_TIMER;
#endif
			break;
		default:
			info->pending_limt = qp_pending_limit;
			info->coalesce_timer_cfg = qp_coalesc_timer_cfg;
			break;
		}
	}

	info->resend_timer_cfg = HINIC_DEAULT_TXRX_MSIX_RESEND_TIMER_CFG;
	info->pkt_rate_high = HINIC_RX_RATE_HIGH;
	info->rx_usecs_high = qp_coalesc_timer_high;
	info->rx_pending_limt_high = qp_pending_limit_high;
	info->pkt_rate_low = HINIC_RX_RATE_LOW;
	info->rx_usecs_low = qp_coalesc_timer_low;
	info->rx_pending_limt_low = qp_pending_limit_low;

	if (nic_dev->in_vm) {
		if (qp_pending_limit_high == HINIC_RX_PENDING_LIMIT_HIGH)
			qp_pending_limit_high = HINIC_RX_PENDING_LIMIT_HIGH_VM;
		info->pkt_rate_low = HINIC_RX_RATE_LOW_VM;
		info->rx_pending_limt_high = qp_pending_limit_high;
	}

	/* suit for sdi3.0 vm mode vf drv or bm mode pf/vf drv */
	if ((nic_dev->is_vm_slave && nic_dev->in_vm) ||
	    nic_dev->is_bm_slave) {
		info->pkt_rate_high = SDI_VM_RX_PKT_RATE_HIGH;
		info->pkt_rate_low = SDI_VM_RX_PKT_RATE_LOW;

		if (qp_coalesc_use_drv_params_switch == 0) {
			/* if arm server, maybe need to change this value
			 * again
			 */
			info->pending_limt = SDI_VM_PENDING_LIMT;
			info->coalesce_timer_cfg = SDI_VM_COALESCE_TIMER_CFG;
			info->rx_usecs_high = SDI_VM_RX_USECS_HIGH;
			info->rx_pending_limt_high =
				SDI_VM_RX_PENDING_LIMT_HIGH;
			info->rx_usecs_low = SDI_VM_RX_USECS_LOW;
			info->rx_pending_limt_low = SDI_VM_RX_PENDING_LIMT_LOW;
		} else {
			info->rx_usecs_high = qp_coalesc_timer_high;
			info->rx_pending_limt_high = qp_pending_limit_high;
			info->rx_usecs_low = qp_coalesc_timer_low;
			info->rx_pending_limt_low = qp_pending_limit_low;
		}
	}
}

static void init_intr_coal_param(struct hinic_nic_dev *nic_dev)
{
	struct pci_device_id *id;
	u16 i;

	id = hinic_get_pci_device_id(nic_dev->pdev);
	switch (id->driver_data) {
	case HINIC_BOARD_10GE:
	case HINIC_BOARD_PG_TP_10GE:
		nic_dev->his_link_speed = SPEED_10000;
		break;
	case HINIC_BOARD_25GE:
	case HINIC_BOARD_PG_SM_25GE:
		nic_dev->his_link_speed = SPEED_25000;
		break;
	case HINIC_BOARD_40GE:
		nic_dev->his_link_speed = SPEED_40000;
		break;
	case HINIC_BOARD_100GE:
	case HINIC_BOARD_PG_100GE:
		nic_dev->his_link_speed = SPEED_100000;
		break;
	default:
		break;
	}

	for (i = 0; i < nic_dev->max_qps; i++)
		update_queue_coal_param(nic_dev, id, i);
}

static int hinic_init_intr_coalesce(struct hinic_nic_dev *nic_dev)
{
	u64 size;

	if ((qp_pending_limit != HINIC_DEAULT_TXRX_MSIX_PENDING_LIMIT) ||
	    (qp_coalesc_timer_cfg != HINIC_DEAULT_TXRX_MSIX_COALESC_TIMER_CFG))
		nic_dev->intr_coal_set_flag = 1;
	else
		nic_dev->intr_coal_set_flag = 0;

	size = sizeof(*nic_dev->intr_coalesce) * nic_dev->max_qps;
	if (!size) {
		nic_err(&nic_dev->pdev->dev, "Cannot allocate zero size intr coalesce\n");
		return -EINVAL;
	}
	nic_dev->intr_coalesce = kzalloc(size, GFP_KERNEL);
	if (!nic_dev->intr_coalesce) {
		nic_err(&nic_dev->pdev->dev, "Failed to alloc intr coalesce\n");
		return -ENOMEM;
	}

	init_intr_coal_param(nic_dev);

	if (test_bit(HINIC_INTR_ADAPT, &nic_dev->flags))
		nic_dev->adaptive_rx_coal = 1;
	else
		nic_dev->adaptive_rx_coal = 0;

	return 0;
}

static void hinic_free_intr_coalesce(struct hinic_nic_dev *nic_dev)
{
	kfree(nic_dev->intr_coalesce);
}

static int hinic_alloc_qps(struct hinic_nic_dev *nic_dev)
{
	struct net_device *netdev = nic_dev->netdev;
	int err;

	err = hinic_alloc_txqs(netdev);
	if (err) {
		nic_err(&nic_dev->pdev->dev, "Failed to alloc txqs\n");
		return err;
	}

	err = hinic_alloc_rxqs(netdev);
	if (err) {
		nic_err(&nic_dev->pdev->dev, "Failed to alloc rxqs\n");
		goto alloc_rxqs_err;
	}

	err = hinic_init_intr_coalesce(nic_dev);
	if (err) {
		nic_err(&nic_dev->pdev->dev, "Failed to init_intr_coalesce\n");
		goto init_intr_err;
	}

	return 0;

init_intr_err:
	hinic_free_rxqs(netdev);

alloc_rxqs_err:
	hinic_free_txqs(netdev);

	return err;
}

static void hinic_destroy_qps(struct hinic_nic_dev *nic_dev)
{
	hinic_free_intr_coalesce(nic_dev);
	hinic_free_rxqs(nic_dev->netdev);
	hinic_free_txqs(nic_dev->netdev);
}

static int hinic_validate_parameters(struct hinic_lld_dev *lld_dev)
{
	struct pci_dev *pdev = lld_dev->pdev;

	if (bp_upper_thd < bp_lower_thd || bp_lower_thd == 0) {
		nic_warn(&pdev->dev, "Module Parameter bp_upper_thd: %d, bp_lower_thd: %d is invalid, resetting to default\n",
			 bp_upper_thd, bp_lower_thd);
		bp_lower_thd = HINIC_RX_BP_LOWER_THD;
		bp_upper_thd = HINIC_RX_BP_UPPER_THD;
	}

	if (!poll_weight) {
		nic_warn(&pdev->dev, "Module Parameter poll_weight can not be 0, resetting to %d\n",
			 DEFAULT_POLL_WEIGHT);
		poll_weight = DEFAULT_POLL_WEIGHT;
	}

	/* check rx_buff value, default rx_buff is 2KB.
	 * Invalid rx_buff include 2KB/4KB/8KB/16KB.
	 */
	if ((rx_buff != RX_BUFF_VALID_2KB) && (rx_buff != RX_BUFF_VALID_4KB) &&
	    (rx_buff != RX_BUFF_VALID_8KB) && (rx_buff != RX_BUFF_VALID_16KB)) {
		nic_warn(&pdev->dev, "Module Parameter rx_buff value %d is out of range, must be 2^n. Valid range is 2 - 16, resetting to %dKB",
			 rx_buff, DEFAULT_RX_BUFF_LEN);
		rx_buff = DEFAULT_RX_BUFF_LEN;
	}

	if (qp_coalesc_timer_high <= qp_coalesc_timer_low) {
		nic_warn(&pdev->dev, "Module Parameter qp_coalesc_timer_high: %d, qp_coalesc_timer_low: %d is invalid, resetting to default\n",
			 qp_coalesc_timer_high, qp_coalesc_timer_low);
		qp_coalesc_timer_high = HINIC_RX_COAL_TIME_HIGH;
		qp_coalesc_timer_low = HINIC_RX_COAL_TIME_LOW;
	}

	if (qp_pending_limit_high <= qp_pending_limit_low) {
		nic_warn(&pdev->dev, "Module Parameter qp_pending_limit_high: %d, qp_pending_limit_low: %d is invalid, resetting to default\n",
			 qp_pending_limit_high, qp_pending_limit_low);
		qp_pending_limit_high = HINIC_RX_PENDING_LIMIT_HIGH;
		qp_pending_limit_low = HINIC_RX_PENDING_LIMIT_LOW;
	}

	return 0;
}

static void check_lro_module_param(struct hinic_nic_dev *nic_dev)
{
	struct hinic_lro_cfg *lro = &nic_dev->adaptive_cfg.lro;

	/* Use module parameters first. */
	if (set_lro_timer != 0 &&
	    set_lro_timer >= HINIC_LRO_RX_TIMER_LOWER &&
	    set_lro_timer <= HINIC_LRO_RX_TIMER_UPPER)
		lro->timer = set_lro_timer;

	/* Use module parameters first. */
	if (set_max_wqe_num != 0 &&
	    set_max_wqe_num <= HINIC_LRO_MAX_WQE_NUM_UPPER &&
	    set_max_wqe_num >= HINIC_LRO_MAX_WQE_NUM_LOWER)
		lro->buffer_size = set_max_wqe_num * nic_dev->rx_buff_len;
}

static void decide_rss_cfg(struct hinic_nic_dev *nic_dev)
{
	struct hinic_environment_info *info = &nic_dev->env_info;

	switch (info->cpu) {
	case HINIC_CPU_ARM_GENERIC:
		set_bit(HINIC_SAME_RXTX, &nic_dev->flags);

		break;
	case HINIC_CPU_X86_GENERIC:
		clear_bit(HINIC_SAME_RXTX, &nic_dev->flags);

		break;

	default:
		clear_bit(HINIC_SAME_RXTX, &nic_dev->flags);
		break;
	}
}

static void decide_lro_cfg(struct hinic_nic_dev *nic_dev)
{
	struct hinic_environment_info *info = &nic_dev->env_info;
	struct hinic_lro_cfg *lro = &nic_dev->adaptive_cfg.lro;

	if (lro_en_status < HINIC_LRO_STATUS_UNSET) {
		lro->enable = lro_en_status;
	} else {
		/* LRO will be opened in all Huawei OS */
		switch (info->os) {
		case HINIC_OS_HUAWEI:
			lro->enable = 1;
			break;
		case HINIC_OS_NON_HUAWEI:
			lro->enable = 0;
			break;
		default:
			lro->enable = 0;
			break;
		}
	}

	switch (info->board) {
	case HINIC_BOARD_25GE:
		lro->timer = HINIC_LRO_RX_TIMER_DEFAULT_25GE;
		break;
	case HINIC_BOARD_100GE:
		lro->timer = HINIC_LRO_RX_TIMER_DEFAULT_100GE;
		break;
	case HINIC_BOARD_PG_TP_10GE:
		lro->timer = HINIC_LRO_RX_TIMER_DEFAULT_PG_10GE;
		break;
	case HINIC_BOARD_PG_SM_25GE:
		lro->timer = HINIC_LRO_RX_TIMER_DEFAULT;
		break;
	case HINIC_BOARD_PG_100GE:
		lro->timer = HINIC_LRO_RX_TIMER_DEFAULT_PG_100GE;
		break;
	default:
		lro->timer = HINIC_LRO_RX_TIMER_DEFAULT;
		break;
	}

	/* Use module parameters first. */
	switch (info->cpu) {
	case HINIC_CPU_ARM_GENERIC:
		lro->buffer_size =
			HINIC_LRO_MAX_WQE_NUM_DEFAULT_ARM *
			nic_dev->rx_buff_len;
		break;
	case HINIC_CPU_X86_GENERIC:
		lro->buffer_size =
			HINIC_LRO_MAX_WQE_NUM_DEFAULT_X86 *
			nic_dev->rx_buff_len;
		break;
	default:
		lro->buffer_size =
			HINIC_LRO_MAX_WQE_NUM_DEFAULT *
			nic_dev->rx_buff_len;
		break;
	}

	/* lro buffer_size need modify according board type */
	switch (info->board) {
	case HINIC_BOARD_PG_TP_10GE:
	case HINIC_BOARD_PG_SM_25GE:
	case HINIC_BOARD_PG_100GE:
		lro->buffer_size =
			HINIC_LRO_WQE_NUM_PANGEA_DEFAULT * nic_dev->rx_buff_len;
		break;
	default:
		break;
	}

	check_lro_module_param(nic_dev);

	nic_info(&nic_dev->pdev->dev,
		 "LRO default configuration: enable %u, timer %u, buffer size %u\n",
		 lro->enable, lro->timer, lro->buffer_size);
}

static void decide_intr_cfg(struct hinic_nic_dev *nic_dev)
{
	struct pci_device_id *id;

	id = hinic_get_pci_device_id(nic_dev->pdev);
	switch (id->driver_data) {
	case HINIC_BOARD_PG_TP_10GE:
	case HINIC_BOARD_PG_SM_25GE:
	case HINIC_BOARD_PG_100GE:
		clear_bit(HINIC_INTR_ADAPT, &nic_dev->flags);
		break;
	default:
		set_bit(HINIC_INTR_ADAPT, &nic_dev->flags);
		break;
	}
}

static void adaptive_configuration_init(struct hinic_nic_dev *nic_dev)
{
	struct pci_device_id *id;

	id = hinic_get_pci_device_id(nic_dev->pdev);
	if (id)
		nic_dev->env_info.board = id->driver_data;
	else
		nic_dev->env_info.board = HINIC_BOARD_UKNOWN;

	nic_dev->env_info.os = HINIC_OS_HUAWEI;

#if   defined(__aarch64__)
	nic_dev->env_info.cpu = HINIC_CPU_ARM_GENERIC;
#elif defined(__x86_64__)
	nic_dev->env_info.cpu = HINIC_CPU_X86_GENERIC;
#else
	nic_dev->env_info.cpu = HINIC_CPU_UKNOWN;
#endif

	nic_info(&nic_dev->pdev->dev,
		 "Board type %u, OS type %u, CPU type %u\n",
		 nic_dev->env_info.board, nic_dev->env_info.os,
		 nic_dev->env_info.cpu);

	decide_lro_cfg(nic_dev);
	decide_rss_cfg(nic_dev);
	decide_intr_cfg(nic_dev);
}

static int nic_probe(struct hinic_lld_dev *lld_dev, void **uld_dev,
		     char *uld_dev_name)
{
	struct pci_dev *pdev = lld_dev->pdev;
	struct hinic_nic_dev *nic_dev;
	struct net_device *netdev;
	u16 max_qps;
	u32 page_num;
	int err;

	/* *uld_dev should always no be NULL */
	*uld_dev = lld_dev;

	if (!hinic_support_nic(lld_dev->hwdev, NULL)) {
		nic_info(&pdev->dev, "Hw don't support nic\n");
		return 0;
	}

	err = hinic_validate_parameters(lld_dev);
	if (err)
		return -EINVAL;

	max_qps = hinic_func_max_nic_qnum(lld_dev->hwdev);
	netdev = alloc_etherdev_mq(sizeof(*nic_dev), max_qps);
	if (!netdev) {
		nic_err(&pdev->dev, "Failed to allocate ETH device\n");
		return -ENOMEM;
	}


	SET_NETDEV_DEV(netdev, &pdev->dev);
	nic_dev = (struct hinic_nic_dev *)netdev_priv(netdev);
	nic_dev->hwdev = lld_dev->hwdev;
	nic_dev->pdev = pdev;
	nic_dev->poll_weight = (int)poll_weight;
	nic_dev->msg_enable = DEFAULT_MSG_ENABLE;
	nic_dev->heart_status = true;
	nic_dev->in_vm = !hinic_is_in_host();
	nic_dev->is_vm_slave = is_multi_vm_slave(lld_dev->hwdev);
	nic_dev->is_bm_slave = is_multi_bm_slave(lld_dev->hwdev);
	nic_dev->lro_replenish_thld = lro_replenish_thld;
	nic_dev->rx_buff_len = (u16)(rx_buff * CONVERT_UNIT);
	page_num = (RX_BUFF_NUM_PER_PAGE * nic_dev->rx_buff_len) / PAGE_SIZE;
	nic_dev->page_order = page_num > 0 ? ilog2(page_num) : 0;


	mutex_init(&nic_dev->nic_mutex);

	adaptive_configuration_init(nic_dev);

	nic_dev->vlan_bitmap = kzalloc(VLAN_BITMAP_SIZE(nic_dev), GFP_KERNEL);
	if (!nic_dev->vlan_bitmap) {
		nic_err(&pdev->dev, "Failed to allocate vlan bitmap\n");
		err = -ENOMEM;
		goto vlan_bitmap_err;
	}
	nic_dev->netdev = netdev;
	hinic_assign_netdev_ops(nic_dev);
	netdev_feature_init(netdev);
	/* get nic cap from hw */
	hinic_support_nic(lld_dev->hwdev, &nic_dev->nic_cap);

	err = hinic_init_nic_hwdev(nic_dev->hwdev, nic_dev->rx_buff_len);
	if (err) {
		nic_err(&pdev->dev, "Failed to init nic hwdev\n");
		goto init_nic_hwdev_err;
	}

	err = hinic_set_super_cqe_state(nic_dev->hwdev, true);
	if (err) {
		nic_err(&pdev->dev, "Failed to set super cqe\n");
		goto set_supper_cqe_err;
	}

	err = hinic_sw_init(nic_dev);
	if (err)
		goto sw_init_err;

	err = hinic_alloc_qps(nic_dev);
	if (err) {
		nic_err(&pdev->dev, "Failed to alloc qps\n");
		goto alloc_qps_err;
	}

	nic_dev->workq = create_singlethread_workqueue(HINIC_NIC_DEV_WQ_NAME);
	if (!nic_dev->workq) {
		nic_err(&pdev->dev, "Failed to initialize AEQ workqueue\n");
		err = -ENOMEM;
		goto create_workq_err;
	}

	INIT_LIST_HEAD(&nic_dev->uc_filter_list);
	INIT_LIST_HEAD(&nic_dev->mc_filter_list);
	INIT_WORK(&nic_dev->rx_mode_work, hinic_set_rx_mode_work);

	err = hinic_set_default_hw_feature(nic_dev);
	if (err)
		goto set_features_err;

#ifdef HAVE_MULTI_VLAN_OFFLOAD_EN
	hinic_register_notifier(nic_dev);
#endif
	err = register_netdev(netdev);
	if (err) {
		nic_err(&pdev->dev, "Failed to register netdev\n");
		err = -ENOMEM;
		goto netdev_err;
	}

	netif_carrier_off(netdev);

	*uld_dev = nic_dev;
	nicif_info(nic_dev, probe, netdev, "Register netdev succeed\n");

	return 0;

netdev_err:
#ifdef HAVE_MULTI_VLAN_OFFLOAD_EN
	hinic_unregister_notifier(nic_dev);
#endif

set_features_err:
	destroy_workqueue(nic_dev->workq);

create_workq_err:
	hinic_destroy_qps(nic_dev);

alloc_qps_err:
	hinic_del_mac(nic_dev->hwdev, netdev->dev_addr, 0,
		      hinic_global_func_id_hw(nic_dev->hwdev));

sw_init_err:
	(void)hinic_set_super_cqe_state(nic_dev->hwdev, false);

set_supper_cqe_err:
	hinic_free_nic_hwdev(nic_dev->hwdev);

init_nic_hwdev_err:
	kfree(nic_dev->vlan_bitmap);

vlan_bitmap_err:
	free_netdev(netdev);

	return err;
}

static void nic_remove(struct hinic_lld_dev *lld_dev, void *adapter)
{
	struct hinic_nic_dev *nic_dev = adapter;
	struct net_device *netdev;

	if (!nic_dev || !hinic_support_nic(lld_dev->hwdev, NULL))
		return;

	netdev = nic_dev->netdev;

	unregister_netdev(netdev);
#ifdef HAVE_MULTI_VLAN_OFFLOAD_EN
	hinic_unregister_notifier(nic_dev);

#endif
	cancel_work_sync(&nic_dev->rx_mode_work);
	destroy_workqueue(nic_dev->workq);

	hinic_destroy_qps(nic_dev);

	hinic_clean_mac_list_filter(nic_dev);
	hinic_del_mac(nic_dev->hwdev, netdev->dev_addr, 0,
		      hinic_global_func_id_hw(nic_dev->hwdev));
	if (test_bit(HINIC_RSS_ENABLE, &nic_dev->flags))
		hinic_rss_template_free(nic_dev->hwdev, nic_dev->rss_tmpl_idx);

	(void)hinic_set_super_cqe_state(nic_dev->hwdev, false);

	hinic_free_nic_hwdev(nic_dev->hwdev);

	kfree(nic_dev->vlan_bitmap);

	free_netdev(netdev);
}

int hinic_disable_func_rss(struct hinic_nic_dev *nic_dev)
{
	struct net_device *netdev = nic_dev->netdev;
	int err, err_netdev = 0;

	nicif_info(nic_dev, drv, netdev, "Start to disable RSS\n");

	if (!test_bit(HINIC_RSS_ENABLE, &nic_dev->flags)) {
		nicif_info(nic_dev, drv, netdev, "RSS not enabled, do nothing\n");
		return 0;
	}

	if (netif_running(netdev)) {
		err_netdev = hinic_close(netdev);
		if (err_netdev) {
			nicif_err(nic_dev, drv, netdev,
				  "Failed to close netdev\n");
			return -EFAULT;
		}
	}

	/* free rss template */
	err = hinic_rss_template_free(nic_dev->hwdev, nic_dev->rss_tmpl_idx);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to free RSS template\n");
	} else {
		nicif_info(nic_dev, drv, netdev, "Success to free RSS template\n");
		clear_bit(HINIC_RSS_ENABLE, &nic_dev->flags);
	}

	if (netif_running(netdev)) {
		err_netdev = hinic_open(netdev);
		if (err_netdev)
			nicif_err(nic_dev, drv, netdev,
				  "Failed to open netdev\n");
	}

	return err ? err : err_netdev;
}

int hinic_enable_func_rss(struct hinic_nic_dev *nic_dev)
{
	struct net_device *netdev = nic_dev->netdev;
	int err, err_netdev = 0;

	nicif_info(nic_dev, drv, netdev, "Start to enable RSS\n");

	if (test_bit(HINIC_RSS_ENABLE, &nic_dev->flags)) {
		nicif_info(nic_dev, drv, netdev, "RSS already enabled, do nothing\n");
		return 0;
	}

	if (netif_running(netdev)) {
		err_netdev = hinic_close(netdev);
		if (err_netdev) {
			nicif_err(nic_dev, drv, netdev,
				  "Failed to close netdev\n");
			return -EFAULT;
		}
	}

	err = hinic_rss_template_alloc(nic_dev->hwdev, &nic_dev->rss_tmpl_idx);
	if (err) {
		if (err == -ENOSPC)
			nicif_warn(nic_dev, drv, netdev,
				   "Failed to alloc RSS template,table is full\n");
		else
			nicif_err(nic_dev, drv, netdev,
				  "Failed to alloc RSS template\n");
	} else {
		set_bit(HINIC_RSS_ENABLE, &nic_dev->flags);
		nicif_info(nic_dev, drv, netdev, "Success to alloc RSS template\n");
	}

	if (netif_running(netdev)) {
		err_netdev = hinic_open(netdev);
		if (err_netdev)
			nicif_err(nic_dev, drv, netdev,
				  "Failed to open netdev\n");
	}

	return err ? err : err_netdev;
}

static const char *hinic_module_link_err[LINK_ERR_NUM] = {
	"Unrecognized module",
};

static void hinic_port_module_event_handler(struct hinic_nic_dev *nic_dev,
					    struct hinic_event_info *event)
{
	enum port_module_event_type type = event->module_event.type;
	enum link_err_type err_type = event->module_event.err_type;

	switch (type) {
	case HINIC_PORT_MODULE_CABLE_PLUGGED:
	case HINIC_PORT_MODULE_CABLE_UNPLUGGED:
		nicif_info(nic_dev, link, nic_dev->netdev,
			   "Port module event: Cable %s\n",
			   type == HINIC_PORT_MODULE_CABLE_PLUGGED ?
			   "plugged" : "unplugged");
		break;
	case HINIC_PORT_MODULE_LINK_ERR:
		if (err_type >= LINK_ERR_NUM) {
			nicif_info(nic_dev, link, nic_dev->netdev,
				   "Link failed, Unknown error type: 0x%x\n",
				   err_type);
		} else {
			nicif_info(nic_dev, link, nic_dev->netdev,
				   "Link failed, error type: 0x%x: %s\n",
				   err_type, hinic_module_link_err[err_type]);
		}
		break;
	default:
		nicif_err(nic_dev, link, nic_dev->netdev,
			  "Unknown port module type %d\n", type);
		break;
	}
}

static void hinic_intr_coalesc_change(struct hinic_nic_dev *nic_dev,
				      struct hinic_event_info *event)
{
	u32 hw_to_os_speed[LINK_SPEED_LEVELS] = {SPEED_10, SPEED_100,
						 SPEED_1000, SPEED_10000,
						 SPEED_25000, SPEED_40000,
						 SPEED_100000};
	u8 qid, coalesc_timer_cfg, pending_limt;
	struct pci_device_id *id;
	u32 speed;
	int err;

	if (nic_dev->adaptive_rx_coal)
		return;

	speed = hw_to_os_speed[event->link_info.speed];
	if (speed == nic_dev->his_link_speed)
		return;

	id = hinic_get_pci_device_id(nic_dev->pdev);
	switch (id->driver_data) {
	case HINIC_BOARD_PG_TP_10GE:
		return;
	case HINIC_BOARD_PG_SM_25GE:
		if (speed == SPEED_10000) {
			pending_limt =
				HINIC_DFT_PG_10GE_TXRX_MSIX_PENDING_LIMIT;
			coalesc_timer_cfg =
				HINIC_DFT_PG_10GE_TXRX_MSIX_COALESC_TIMER;
		} else if (speed == SPEED_25000) {
			pending_limt =
				HINIC_DFT_PG_25GE_TXRX_MSIX_PENDING_LIMIT;
#if   defined(__aarch64__)
			coalesc_timer_cfg =
				HINIC_DFT_PG_ARM_25GE_TXRX_MSIX_COALESC_TIMER;
#else
			coalesc_timer_cfg =
				HINIC_DFT_PG_25GE_TXRX_MSIX_COALESC_TIMER;
#endif
		} else {
			pending_limt =
				HINIC_DFT_PG_25GE_TXRX_MSIX_PENDING_LIMIT;
			coalesc_timer_cfg =
				HINIC_DFT_PG_25GE_TXRX_MSIX_COALESC_TIMER;
		}
		break;
	case HINIC_BOARD_PG_100GE:
		return;
	default:
		return;
	}

	for (qid = 0; qid < nic_dev->num_qps; qid++) {
		if (!nic_dev->intr_coalesce[qid].user_set_intr_coal_flag) {
			err = set_interrupt_moder(nic_dev, qid,
						  coalesc_timer_cfg,
						  pending_limt);
			if (!err) {
				nic_dev->intr_coalesce[qid].pending_limt =
								pending_limt;
				nic_dev->intr_coalesce[qid].coalesce_timer_cfg =
							coalesc_timer_cfg;
			}
		}
	}

	nic_dev->his_link_speed = speed;
}

void nic_event(struct hinic_lld_dev *lld_dev, void *adapter,
	       struct hinic_event_info *event)
{
	struct hinic_nic_dev *nic_dev = adapter;
	struct net_device *netdev;
	enum hinic_event_type type;

	if (!nic_dev || !event || !hinic_support_nic(lld_dev->hwdev, NULL))
		return;

	netdev = nic_dev->netdev;
	type = event->type;

	switch (type) {
	case HINIC_EVENT_LINK_DOWN:
		hinic_link_status_change(nic_dev, false);
		break;
	case HINIC_EVENT_LINK_UP:
		hinic_link_status_change(nic_dev, true);
		hinic_intr_coalesc_change(nic_dev, event);
		break;
	case HINIC_EVENT_HEART_LOST:
		hinic_heart_lost(nic_dev);
		break;
	case HINIC_EVENT_FAULT:
		break;
	case HINIC_EVENT_DCB_STATE_CHANGE:
		if (nic_dev->default_cos_id == event->dcb_state.default_cos)
			break;

		/* PF notify to vf, don't need to handle this event */
		if (!HINIC_FUNC_IS_VF(nic_dev->hwdev))
			break;

		nicif_info(nic_dev, drv, netdev, "Change default cos %d to %d\n",
			   nic_dev->default_cos_id,
			   event->dcb_state.default_cos);

		nic_dev->default_cos_id = event->dcb_state.default_cos;
		hinic_set_sq_default_cos(netdev, nic_dev->default_cos_id);
		break;
	case HINIC_EVENT_PORT_MODULE_EVENT:
		hinic_port_module_event_handler(nic_dev, event);
		break;
	default:
		break;
	}
}

struct hinic_uld_info nic_uld_info = {
	.probe = nic_probe,
	.remove = nic_remove,
	.suspend = NULL,
	.resume = NULL,
	.event = nic_event,
	.ioctl = nic_ioctl,
};
