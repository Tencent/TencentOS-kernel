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
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/ethtool.h>
#include <linux/vmalloc.h>

#include "ossl_knl.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hw.h"
#include "hinic_nic_cfg.h"
#include "hinic_nic_dev.h"
#include "hinic_dfx_def.h"
#include "hinic_tx.h"
#include "hinic_rx.h"
#include "hinic_qp.h"

#ifndef SET_ETHTOOL_OPS
#define SET_ETHTOOL_OPS(netdev, ops) \
	((netdev)->ethtool_ops = (ops))
#endif

struct hinic_stats {
	char name[ETH_GSTRING_LEN];
	u32 size;
	int offset;
};

#define ARRAY_LEN(arr)	((int)((int)sizeof(arr) / (int)sizeof(arr[0])))

#define HINIC_NETDEV_STAT(_stat_item) { \
	.name = #_stat_item, \
	.size = FIELD_SIZEOF(struct rtnl_link_stats64, _stat_item), \
	.offset = offsetof(struct rtnl_link_stats64, _stat_item) \
}

static struct hinic_stats hinic_netdev_stats[] = {
	HINIC_NETDEV_STAT(rx_packets),
	HINIC_NETDEV_STAT(tx_packets),
	HINIC_NETDEV_STAT(rx_bytes),
	HINIC_NETDEV_STAT(tx_bytes),
	HINIC_NETDEV_STAT(rx_errors),
	HINIC_NETDEV_STAT(tx_errors),
	HINIC_NETDEV_STAT(rx_dropped),
	HINIC_NETDEV_STAT(tx_dropped),
	HINIC_NETDEV_STAT(multicast),
	HINIC_NETDEV_STAT(collisions),
	HINIC_NETDEV_STAT(rx_length_errors),
	HINIC_NETDEV_STAT(rx_over_errors),
	HINIC_NETDEV_STAT(rx_crc_errors),
	HINIC_NETDEV_STAT(rx_frame_errors),
	HINIC_NETDEV_STAT(rx_fifo_errors),
	HINIC_NETDEV_STAT(rx_missed_errors),
	HINIC_NETDEV_STAT(tx_aborted_errors),
	HINIC_NETDEV_STAT(tx_carrier_errors),
	HINIC_NETDEV_STAT(tx_fifo_errors),
	HINIC_NETDEV_STAT(tx_heartbeat_errors),
};

#define HINIC_NIC_STAT(_stat_item) { \
	.name = #_stat_item, \
	.size = FIELD_SIZEOF(struct hinic_nic_stats, _stat_item), \
	.offset = offsetof(struct hinic_nic_stats, _stat_item) \
}

static struct hinic_stats hinic_nic_dev_stats[] = {
	HINIC_NIC_STAT(netdev_tx_timeout),
};

static struct hinic_stats hinic_nic_dev_stats_extern[] = {
	HINIC_NIC_STAT(tx_carrier_off_drop),
	HINIC_NIC_STAT(tx_invalid_qid),
};

#define HINIC_RXQ_STAT(_stat_item) { \
	.name = "rxq%d_"#_stat_item, \
	.size = FIELD_SIZEOF(struct hinic_rxq_stats, _stat_item), \
	.offset = offsetof(struct hinic_rxq_stats, _stat_item) \
}

#define HINIC_TXQ_STAT(_stat_item) { \
	.name = "txq%d_"#_stat_item, \
	.size = FIELD_SIZEOF(struct hinic_txq_stats, _stat_item), \
	.offset = offsetof(struct hinic_txq_stats, _stat_item) \
}

static struct hinic_stats hinic_rx_queue_stats[] = {
	HINIC_RXQ_STAT(packets),
	HINIC_RXQ_STAT(bytes),
	HINIC_RXQ_STAT(errors),
	HINIC_RXQ_STAT(csum_errors),
	HINIC_RXQ_STAT(other_errors),
	HINIC_RXQ_STAT(dropped),
};

static struct hinic_stats hinic_rx_queue_stats_extern[] = {
	HINIC_RXQ_STAT(alloc_skb_err),
};

static struct hinic_stats hinic_tx_queue_stats[] = {
	HINIC_TXQ_STAT(packets),
	HINIC_TXQ_STAT(bytes),
	HINIC_TXQ_STAT(busy),
	HINIC_TXQ_STAT(wake),
	HINIC_TXQ_STAT(dropped),
	HINIC_TXQ_STAT(big_frags_pkts),
	HINIC_TXQ_STAT(big_udp_pkts),
};

static struct hinic_stats hinic_tx_queue_stats_extern[] = {
	HINIC_TXQ_STAT(ufo_pkt_unsupport),
	HINIC_TXQ_STAT(ufo_linearize_err),
	HINIC_TXQ_STAT(ufo_alloc_skb_err),
	HINIC_TXQ_STAT(skb_pad_err),
	HINIC_TXQ_STAT(frag_len_overflow),
	HINIC_TXQ_STAT(offload_cow_skb_err),
	HINIC_TXQ_STAT(alloc_cpy_frag_err),
	HINIC_TXQ_STAT(map_cpy_frag_err),
	HINIC_TXQ_STAT(map_frag_err),
	HINIC_TXQ_STAT(frag_size_err),
};

#define HINIC_FUNC_STAT(_stat_item) {	\
	.name = #_stat_item, \
	.size = FIELD_SIZEOF(struct hinic_vport_stats, _stat_item), \
	.offset = offsetof(struct hinic_vport_stats, _stat_item) \
}

static struct hinic_stats hinic_function_stats[] = {
	HINIC_FUNC_STAT(tx_unicast_pkts_vport),
	HINIC_FUNC_STAT(tx_unicast_bytes_vport),
	HINIC_FUNC_STAT(tx_multicast_pkts_vport),
	HINIC_FUNC_STAT(tx_multicast_bytes_vport),
	HINIC_FUNC_STAT(tx_broadcast_pkts_vport),
	HINIC_FUNC_STAT(tx_broadcast_bytes_vport),

	HINIC_FUNC_STAT(rx_unicast_pkts_vport),
	HINIC_FUNC_STAT(rx_unicast_bytes_vport),
	HINIC_FUNC_STAT(rx_multicast_pkts_vport),
	HINIC_FUNC_STAT(rx_multicast_bytes_vport),
	HINIC_FUNC_STAT(rx_broadcast_pkts_vport),
	HINIC_FUNC_STAT(rx_broadcast_bytes_vport),

	HINIC_FUNC_STAT(tx_discard_vport),
	HINIC_FUNC_STAT(rx_discard_vport),
	HINIC_FUNC_STAT(tx_err_vport),
	HINIC_FUNC_STAT(rx_err_vport),
};

#define HINIC_PORT_STAT(_stat_item) { \
	.name = #_stat_item, \
	.size = FIELD_SIZEOF(struct hinic_phy_port_stats, _stat_item), \
	.offset = offsetof(struct hinic_phy_port_stats, _stat_item) \
}

static struct hinic_stats hinic_port_stats[] = {
	HINIC_PORT_STAT(mac_rx_total_pkt_num),
	HINIC_PORT_STAT(mac_rx_total_oct_num),
	HINIC_PORT_STAT(mac_rx_bad_pkt_num),
	HINIC_PORT_STAT(mac_rx_bad_oct_num),
	HINIC_PORT_STAT(mac_rx_good_pkt_num),
	HINIC_PORT_STAT(mac_rx_good_oct_num),
	HINIC_PORT_STAT(mac_rx_uni_pkt_num),
	HINIC_PORT_STAT(mac_rx_multi_pkt_num),
	HINIC_PORT_STAT(mac_rx_broad_pkt_num),
	HINIC_PORT_STAT(mac_tx_total_pkt_num),
	HINIC_PORT_STAT(mac_tx_total_oct_num),
	HINIC_PORT_STAT(mac_tx_bad_pkt_num),
	HINIC_PORT_STAT(mac_tx_bad_oct_num),
	HINIC_PORT_STAT(mac_tx_good_pkt_num),
	HINIC_PORT_STAT(mac_tx_good_oct_num),
	HINIC_PORT_STAT(mac_tx_uni_pkt_num),
	HINIC_PORT_STAT(mac_tx_multi_pkt_num),
	HINIC_PORT_STAT(mac_tx_broad_pkt_num),
	HINIC_PORT_STAT(mac_rx_fragment_pkt_num),
	HINIC_PORT_STAT(mac_rx_undersize_pkt_num),
	HINIC_PORT_STAT(mac_rx_undermin_pkt_num),
	HINIC_PORT_STAT(mac_rx_64_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_65_127_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_128_255_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_256_511_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_512_1023_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_1024_1518_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_1519_2047_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_2048_4095_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_4096_8191_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_8192_9216_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_9217_12287_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_12288_16383_oct_pkt_num),
	HINIC_PORT_STAT(mac_rx_1519_max_good_pkt_num),
	HINIC_PORT_STAT(mac_rx_1519_max_bad_pkt_num),
	HINIC_PORT_STAT(mac_rx_oversize_pkt_num),
	HINIC_PORT_STAT(mac_rx_jabber_pkt_num),
	HINIC_PORT_STAT(mac_rx_pause_num),
	HINIC_PORT_STAT(mac_rx_pfc_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri0_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri1_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri2_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri3_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri4_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri5_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri6_pkt_num),
	HINIC_PORT_STAT(mac_rx_pfc_pri7_pkt_num),
	HINIC_PORT_STAT(mac_rx_control_pkt_num),
	HINIC_PORT_STAT(mac_rx_sym_err_pkt_num),
	HINIC_PORT_STAT(mac_rx_fcs_err_pkt_num),
	HINIC_PORT_STAT(mac_rx_send_app_good_pkt_num),
	HINIC_PORT_STAT(mac_rx_send_app_bad_pkt_num),
	HINIC_PORT_STAT(mac_tx_fragment_pkt_num),
	HINIC_PORT_STAT(mac_tx_undersize_pkt_num),
	HINIC_PORT_STAT(mac_tx_undermin_pkt_num),
	HINIC_PORT_STAT(mac_tx_64_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_65_127_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_128_255_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_256_511_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_512_1023_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_1024_1518_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_1519_2047_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_2048_4095_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_4096_8191_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_8192_9216_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_9217_12287_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_12288_16383_oct_pkt_num),
	HINIC_PORT_STAT(mac_tx_1519_max_good_pkt_num),
	HINIC_PORT_STAT(mac_tx_1519_max_bad_pkt_num),
	HINIC_PORT_STAT(mac_tx_oversize_pkt_num),
	HINIC_PORT_STAT(mac_tx_jabber_pkt_num),
	HINIC_PORT_STAT(mac_tx_pause_num),
	HINIC_PORT_STAT(mac_tx_pfc_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri0_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri1_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri2_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri3_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri4_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri5_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri6_pkt_num),
	HINIC_PORT_STAT(mac_tx_pfc_pri7_pkt_num),
	HINIC_PORT_STAT(mac_tx_control_pkt_num),
	HINIC_PORT_STAT(mac_tx_err_all_pkt_num),
	HINIC_PORT_STAT(mac_tx_from_app_good_pkt_num),
	HINIC_PORT_STAT(mac_tx_from_app_bad_pkt_num),
};

u32 hinic_get_io_stats_size(struct hinic_nic_dev *nic_dev)
{
	return ARRAY_LEN(hinic_nic_dev_stats) +
	       ARRAY_LEN(hinic_nic_dev_stats_extern) +
	       (ARRAY_LEN(hinic_tx_queue_stats) +
		ARRAY_LEN(hinic_tx_queue_stats_extern) +
		ARRAY_LEN(hinic_rx_queue_stats) +
		ARRAY_LEN(hinic_rx_queue_stats_extern)) * nic_dev->max_qps;
}

#define GET_VALUE_OF_PTR(size, ptr) (				\
	(size) == sizeof(u64) ? *(u64 *)(ptr) :			\
	(size) == sizeof(u32) ? *(u32 *)(ptr) :			\
	(size) == sizeof(u16) ? *(u16 *)(ptr) : *(u8 *)(ptr)	\
)

#define DEV_STATS_PACK(items, item_idx, array, stats_ptr) do {		\
	int j;								\
	for (j = 0; j < ARRAY_LEN(array); j++) {			\
		memcpy((items)[item_idx].name, (array)[j].name,		\
		       HINIC_SHOW_ITEM_LEN);				\
		(items)[item_idx].hexadecimal = 0;			\
		(items)[item_idx].value =				\
			GET_VALUE_OF_PTR((array)[j].size,		\
			(char *)(stats_ptr) + (array)[j].offset);	\
		item_idx++;						\
	}								\
} while (0)

#define QUEUE_STATS_PACK(items, item_idx, array, stats_ptr, qid) do {	\
	int j, err;							\
	for (j = 0; j < ARRAY_LEN(array); j++) {			\
		memcpy((items)[item_idx].name, (array)[j].name,		\
		       HINIC_SHOW_ITEM_LEN);				\
		err = snprintf((items)[item_idx].name, HINIC_SHOW_ITEM_LEN,\
			 (array)[j].name, (qid));			\
		if (err <= 0 || err >= HINIC_SHOW_ITEM_LEN)		\
			pr_err("Failed snprintf: func_ret(%d), dest_len(%d)\n",\
				err, HINIC_SHOW_ITEM_LEN);		\
		(items)[item_idx].hexadecimal = 0;			\
		(items)[item_idx].value =				\
			GET_VALUE_OF_PTR((array)[j].size,		\
			(char *)(stats_ptr) + (array)[j].offset);	\
		item_idx++;						\
	}								\
} while (0)

void hinic_get_io_stats(struct hinic_nic_dev *nic_dev,
			struct hinic_show_item *items)
{
	int item_idx = 0;
	u16 qid;

	DEV_STATS_PACK(items, item_idx, hinic_nic_dev_stats, &nic_dev->stats);
	DEV_STATS_PACK(items, item_idx, hinic_nic_dev_stats_extern,
		       &nic_dev->stats);

	for (qid = 0; qid < nic_dev->max_qps; qid++) {
		QUEUE_STATS_PACK(items, item_idx, hinic_tx_queue_stats,
				 &nic_dev->txqs[qid].txq_stats, qid);
		QUEUE_STATS_PACK(items, item_idx, hinic_tx_queue_stats_extern,
				 &nic_dev->txqs[qid].txq_stats, qid);
	}

	for (qid = 0; qid < nic_dev->max_qps; qid++) {
		QUEUE_STATS_PACK(items, item_idx, hinic_rx_queue_stats,
				 &nic_dev->rxqs[qid].rxq_stats, qid);
		QUEUE_STATS_PACK(items, item_idx, hinic_rx_queue_stats_extern,
				 &nic_dev->rxqs[qid].rxq_stats, qid);
	}
}

#define LP_DEFAULT_TIME                 (5) /* seconds */
#define LP_PKT_LEN                      (1514)
#define OBJ_STR_MAX_LEN			(32)
#define SET_LINK_STR_MAX_LEN		(128)

#define PORT_DOWN_ERR_IDX  0
enum diag_test_index {
	INTERNAL_LP_TEST = 0,
	EXTERNAL_LP_TEST = 1,
	DIAG_TEST_MAX = 2,
};

static char hinic_test_strings[][ETH_GSTRING_LEN] = {
	"Internal lb test  (on/offline)",
	"External lb test (external_lb)",
};

struct hw2ethtool_link_mode {
	u32 supported;
	u32 advertised;
	enum ethtool_link_mode_bit_indices link_mode_bit;
	u32 speed;
	enum hinic_link_mode hw_link_mode;
};

static struct hw2ethtool_link_mode
	hw_to_ethtool_link_mode_table[HINIC_LINK_MODE_NUMBERS] = {
	{
		.supported = SUPPORTED_10000baseKR_Full,
		.advertised = ADVERTISED_10000baseKR_Full,
		.link_mode_bit = ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
		.speed = SPEED_10000,
		.hw_link_mode = HINIC_10GE_BASE_KR,
	},
	{
		.supported = SUPPORTED_40000baseKR4_Full,
		.advertised = ADVERTISED_40000baseKR4_Full,
		.link_mode_bit = ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT,
		.speed = SPEED_40000,
		.hw_link_mode = HINIC_40GE_BASE_KR4,
	},
	{
		.supported = SUPPORTED_40000baseCR4_Full,
		.advertised = ADVERTISED_40000baseCR4_Full,
		.link_mode_bit = ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT,
		.speed = SPEED_40000,
		.hw_link_mode = HINIC_40GE_BASE_CR4,
	},
	{
		.supported = SUPPORTED_100000baseKR4_Full,
		.advertised = ADVERTISED_100000baseKR4_Full,
		.link_mode_bit = ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT,
		.speed = SPEED_100000,
		.hw_link_mode = HINIC_100GE_BASE_KR4,
	},
	{
		.supported = SUPPORTED_100000baseCR4_Full,
		.advertised = ADVERTISED_100000baseCR4_Full,
		.link_mode_bit = ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT,
		.speed = SPEED_100000,
		.hw_link_mode = HINIC_100GE_BASE_CR4,
	},
	{
		.supported = SUPPORTED_25000baseKR_Full,
		.advertised = ADVERTISED_25000baseKR_Full,
		.link_mode_bit = ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
		.speed = SPEED_25000,
		.hw_link_mode = HINIC_25GE_BASE_KR_S,
	},
	{
		.supported = SUPPORTED_25000baseCR_Full,
		.advertised = ADVERTISED_25000baseCR_Full,
		.link_mode_bit = ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
		.speed = SPEED_25000,
		.hw_link_mode = HINIC_25GE_BASE_CR_S,
	},
	{
		.supported = SUPPORTED_25000baseKR_Full,
		.advertised = ADVERTISED_25000baseKR_Full,
		.link_mode_bit = ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
		.speed = SPEED_25000,
		.hw_link_mode = HINIC_25GE_BASE_KR,
	},
	{
		.supported = SUPPORTED_25000baseCR_Full,
		.advertised = ADVERTISED_25000baseCR_Full,
		.link_mode_bit = ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
		.speed = SPEED_25000,
		.hw_link_mode = HINIC_25GE_BASE_CR,
	},
	{
		.supported = SUPPORTED_1000baseKX_Full,
		.advertised = ADVERTISED_1000baseKX_Full,
		.link_mode_bit = ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
		.speed = SPEED_1000,
		.hw_link_mode = HINIC_GE_BASE_KX,
	},
};

u32 hw_to_ethtool_speed[LINK_SPEED_LEVELS] = {
	SPEED_10, SPEED_100,
	SPEED_1000, SPEED_10000,
	SPEED_25000, SPEED_40000,
	SPEED_100000
};

static int hinic_ethtool_to_hw_speed_level(u32 speed)
{
	int i;

	for (i = 0; i < LINK_SPEED_LEVELS; i++) {
		if (hw_to_ethtool_speed[i] == speed)
			break;
	}

	return i;
}

static int hinic_get_link_mode_index(enum hinic_link_mode link_mode)
{
	int i = 0;

	for (i = 0; i < HINIC_LINK_MODE_NUMBERS; i++) {
		if (link_mode == hw_to_ethtool_link_mode_table[i].hw_link_mode)
			break;
	}

	return i;
}

static int hinic_is_support_speed(enum hinic_link_mode supported_link,
				  u32 speed)
{
	enum hinic_link_mode link_mode;
	int idx;

	for (link_mode = 0; link_mode < HINIC_LINK_MODE_NUMBERS; link_mode++) {
		if (!(supported_link & ((u32)1 << link_mode)))
			continue;

		idx = hinic_get_link_mode_index(link_mode);
		if (idx >= HINIC_LINK_MODE_NUMBERS)
			continue;

		if (hw_to_ethtool_link_mode_table[idx].speed == speed)
			return 1;
	}

	return 0;
}

#define GET_SUPPORTED_MODE	0
#define GET_ADVERTISED_MODE	1

struct cmd_link_settings {
	u64	supported;
	u64	advertising;

	u32	speed;
	u8	duplex;
	u8	port;
	u8	autoneg;
};

#define ETHTOOL_ADD_SUPPORTED_SPEED_LINK_MODE(ecmd, mode)	\
		((ecmd)->supported |=	\
		(1UL << hw_to_ethtool_link_mode_table[mode].link_mode_bit))
#define ETHTOOL_ADD_ADVERTISED_SPEED_LINK_MODE(ecmd, mode)	\
		((ecmd)->advertising |=	\
		(1UL << hw_to_ethtool_link_mode_table[mode].link_mode_bit))

#define ETHTOOL_ADD_SUPPORTED_LINK_MODE(ecmd, mode)	\
			((ecmd)->supported |= SUPPORTED_##mode)
#define ETHTOOL_ADD_ADVERTISED_LINK_MODE(ecmd, mode)	\
			((ecmd)->advertising |= ADVERTISED_##mode)
#define ETHTOOL_TEST_LINK_MODE_SUPPORTED(ecmd, mode)	\
			((ecmd)->supported & SUPPORTED_##Autoneg)

static void hinic_link_port_type(struct cmd_link_settings *link_settings,
				 enum hinic_port_type port_type)
{
	switch (port_type) {
	case HINIC_PORT_ELEC:
	case HINIC_PORT_TP:
		ETHTOOL_ADD_SUPPORTED_LINK_MODE(link_settings, TP);
		ETHTOOL_ADD_ADVERTISED_LINK_MODE(link_settings, TP);
		link_settings->port = PORT_TP;
		break;

	case HINIC_PORT_AOC:
	case HINIC_PORT_FIBRE:
		ETHTOOL_ADD_SUPPORTED_LINK_MODE(link_settings, FIBRE);
		ETHTOOL_ADD_ADVERTISED_LINK_MODE(link_settings, FIBRE);
		link_settings->port = PORT_FIBRE;
		break;

	case HINIC_PORT_COPPER:
		ETHTOOL_ADD_SUPPORTED_LINK_MODE(link_settings, FIBRE);
		ETHTOOL_ADD_ADVERTISED_LINK_MODE(link_settings, FIBRE);
		link_settings->port = PORT_DA;
		break;

	case HINIC_PORT_BACKPLANE:
		ETHTOOL_ADD_SUPPORTED_LINK_MODE(link_settings, Backplane);
		ETHTOOL_ADD_ADVERTISED_LINK_MODE(link_settings, Backplane);
		link_settings->port = PORT_NONE;
		break;

	default:
		link_settings->port = PORT_OTHER;
		break;
	}
}

static void hinic_add_ethtool_link_mode(struct cmd_link_settings *link_settings,
					enum hinic_link_mode hw_link_mode,
					u32 name)
{
	enum hinic_link_mode link_mode;
	int idx = 0;

	for (link_mode = 0; link_mode < HINIC_LINK_MODE_NUMBERS; link_mode++) {
		if (hw_link_mode & ((u32)1 << link_mode)) {
			idx = hinic_get_link_mode_index(link_mode);
			if (idx >= HINIC_LINK_MODE_NUMBERS)
				continue;

			if (name == GET_SUPPORTED_MODE)
				ETHTOOL_ADD_SUPPORTED_SPEED_LINK_MODE
					(link_settings, idx);
			else
				ETHTOOL_ADD_ADVERTISED_SPEED_LINK_MODE
					(link_settings, idx);
		}
	}
}

static int hinic_link_speed_set(struct hinic_nic_dev *nic_dev,
				struct cmd_link_settings *link_settings,
				struct nic_port_info *port_info)
{
	struct net_device *netdev = nic_dev->netdev;
	enum hinic_link_mode supported_link = 0, advertised_link = 0;
	u8 link_state = 0;
	int err;

	err = hinic_get_link_mode(nic_dev->hwdev,
				  &supported_link, &advertised_link);
	if (err || supported_link == HINIC_SUPPORTED_UNKNOWN ||
	    advertised_link == HINIC_SUPPORTED_UNKNOWN) {
		nicif_err(nic_dev, drv, netdev, "Failed to get supported link modes\n");
		return err;
	}

	hinic_add_ethtool_link_mode(link_settings, supported_link,
				    GET_SUPPORTED_MODE);
	hinic_add_ethtool_link_mode(link_settings, advertised_link,
				    GET_ADVERTISED_MODE);

	err = hinic_get_link_state(nic_dev->hwdev, &link_state);
	if (!err && link_state) {
		link_settings->speed = port_info->speed < LINK_SPEED_LEVELS ?
					hw_to_ethtool_speed[port_info->speed] :
					(u32)SPEED_UNKNOWN;

		link_settings->duplex = port_info->duplex;
	} else {
		link_settings->speed = (u32)SPEED_UNKNOWN;
		link_settings->duplex = DUPLEX_UNKNOWN;
	}

	return 0;
}

static int get_link_settings(struct net_device *netdev,
			     struct cmd_link_settings *link_settings)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct nic_port_info port_info = {0};
	struct nic_pause_config nic_pause = {0};
	int err;

	err = hinic_get_port_info(nic_dev->hwdev, &port_info);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to get port info\n");
		return err;
	}

	err = hinic_link_speed_set(nic_dev, link_settings, &port_info);
	if (err)
		return err;

	hinic_link_port_type(link_settings, port_info.port_type);

	link_settings->autoneg = port_info.autoneg_state;
	if (port_info.autoneg_cap)
		ETHTOOL_ADD_SUPPORTED_LINK_MODE(link_settings, Autoneg);
	if (port_info.autoneg_state)
		ETHTOOL_ADD_ADVERTISED_LINK_MODE(link_settings, Autoneg);

	if (!HINIC_FUNC_IS_VF(nic_dev->hwdev)) {
		err = hinic_get_pause_info(nic_dev->hwdev, &nic_pause);
		if (err) {
			nicif_err(nic_dev, drv, netdev,
				  "Failed to get pauseparam from hw\n");
			return err;
		}

		ETHTOOL_ADD_SUPPORTED_LINK_MODE(link_settings, Pause);
		if (nic_pause.rx_pause && nic_pause.tx_pause) {
			ETHTOOL_ADD_ADVERTISED_LINK_MODE(link_settings, Pause);
		} else if (nic_pause.tx_pause) {
			ETHTOOL_ADD_ADVERTISED_LINK_MODE(link_settings,
							 Asym_Pause);
		} else if (nic_pause.rx_pause) {
			ETHTOOL_ADD_ADVERTISED_LINK_MODE(link_settings, Pause);
			ETHTOOL_ADD_ADVERTISED_LINK_MODE(link_settings,
							 Asym_Pause);
		}
	}

	return 0;
}

#ifndef HAVE_NEW_ETHTOOL_LINK_SETTINGS_ONLY
static int hinic_get_settings(struct net_device *netdev,
			      struct ethtool_cmd *ep)
{
	struct cmd_link_settings settings = {0};
	int err;

	err = get_link_settings(netdev, &settings);
	if (err)
		return err;

	ep->supported = settings.supported & ((u32)~0);
	ep->advertising = settings.advertising & ((u32)~0);

	ep->autoneg = settings.autoneg;
	ethtool_cmd_speed_set(ep, settings.speed);
	ep->duplex = settings.duplex;
	ep->port = settings.port;
	ep->transceiver = XCVR_INTERNAL;

	return 0;
}
#endif

#ifdef ETHTOOL_GLINKSETTINGS
static int hinic_get_link_ksettings(
	struct net_device *netdev,
	struct ethtool_link_ksettings *link_settings)
{
	struct cmd_link_settings settings = {0};
	struct ethtool_link_settings *base = &link_settings->base;
	int err;

	ethtool_link_ksettings_zero_link_mode(link_settings, supported);
	ethtool_link_ksettings_zero_link_mode(link_settings, advertising);

	err = get_link_settings(netdev, &settings);
	if (err)
		return err;

	bitmap_copy(link_settings->link_modes.supported,
		    (unsigned long *)&settings.supported,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);
	bitmap_copy(link_settings->link_modes.advertising,
		    (unsigned long *)&settings.advertising,
		    __ETHTOOL_LINK_MODE_MASK_NBITS);

	base->autoneg = settings.autoneg;
	base->speed = settings.speed;
	base->duplex = settings.duplex;
	base->port = settings.port;

	return 0;
}
#endif

static int hinic_is_speed_legal(struct hinic_nic_dev *nic_dev, u32 speed)
{
	struct net_device *netdev = nic_dev->netdev;
	enum hinic_link_mode supported_link = 0, advertised_link = 0;
	enum nic_speed_level speed_level = 0;
	int err;

	err = hinic_get_link_mode(nic_dev->hwdev,
				  &supported_link, &advertised_link);
	if (err || supported_link == HINIC_SUPPORTED_UNKNOWN ||
	    advertised_link == HINIC_SUPPORTED_UNKNOWN) {
		nicif_err(nic_dev, drv, netdev,
			  "Failed to get supported link modes\n");
		return -EAGAIN;
	}

	speed_level = hinic_ethtool_to_hw_speed_level(speed);
	if (speed_level >= LINK_SPEED_LEVELS ||
	    !hinic_is_support_speed(supported_link, speed)) {
		nicif_err(nic_dev, drv, netdev,
			  "Not supported speed: %d\n", speed);
		return -EINVAL;
	}

	return 0;
}

static int hinic_set_settings_to_hw(struct hinic_nic_dev *nic_dev,
				    u32 set_settings, u8 autoneg, u32 speed)
{
	struct net_device *netdev = nic_dev->netdev;
	struct hinic_link_ksettings settings = {0};
	enum nic_speed_level speed_level = 0;
	char set_link_str[SET_LINK_STR_MAX_LEN] = {0};
	int err = 0;

	err = snprintf(set_link_str, sizeof(set_link_str), "%s",
		       (set_settings & HILINK_LINK_SET_AUTONEG) ?
		       (autoneg ? "autong enable " : "autong disable ") : "");
	if (err < 0 || err >= SET_LINK_STR_MAX_LEN) {
		nicif_err(nic_dev, drv, netdev,
			  "Failed snprintf link state, function return(%d) and dest_len(%d)\n",
			  err, SET_LINK_STR_MAX_LEN);
		return -EFAULT;
	}
	if (set_settings & HILINK_LINK_SET_SPEED) {
		speed_level = hinic_ethtool_to_hw_speed_level(speed);
		err = snprintf(set_link_str, sizeof(set_link_str),
			       "%sspeed %d ", set_link_str, speed);
		if (err <= 0 || err >= SET_LINK_STR_MAX_LEN) {
			nicif_err(nic_dev, drv, netdev,
				  "Failed snprintf link speed, function return(%d) and dest_len(%d)\n",
				  err, SET_LINK_STR_MAX_LEN);
			return -EFAULT;
		}
	}

	settings.valid_bitmap = set_settings;
	settings.autoneg = autoneg;
	settings.speed = speed_level;

	err = hinic_set_link_settings(nic_dev->hwdev, &settings);
	if (err != HINIC_MGMT_CMD_UNSUPPORTED) {
		if (err)
			nicif_err(nic_dev, drv, netdev, "Set %sfailed\n",
				  set_link_str);
		else
			nicif_info(nic_dev, drv, netdev, "Set %ssuccess\n",
				   set_link_str);

		return err;
	}

	if (set_settings & HILINK_LINK_SET_AUTONEG) {
		err = hinic_set_autoneg(nic_dev->hwdev,
					(autoneg == AUTONEG_ENABLE));
		if (err)
			nicif_err(nic_dev, drv, netdev, "%s autoneg failed\n",
				  (autoneg == AUTONEG_ENABLE) ?
				  "Enable" : "Disable");
		else
			nicif_info(nic_dev, drv, netdev, "%s autoneg success\n",
				   (autoneg == AUTONEG_ENABLE) ?
				   "Enable" : "Disable");
	}

	if (!err && (set_settings & HILINK_LINK_SET_SPEED)) {
		err = hinic_set_speed(nic_dev->hwdev, speed_level);
		if (err)
			nicif_err(nic_dev, drv, netdev, "Set speed %d failed\n",
				  speed);
		else
			nicif_info(nic_dev, drv, netdev, "Set speed %d success\n",
				   speed);
	}

	return err;
}

static int set_link_settings(struct net_device *netdev, u8 autoneg, u32 speed)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct nic_port_info port_info = {0};
	u32 set_settings = 0;
	int err = 0;

	if (!FUNC_SUPPORT_PORT_SETTING(nic_dev->hwdev)) {
		nicif_err(nic_dev, drv, netdev, "Not support set link settings\n");
		return -EOPNOTSUPP;
	}

	err = hinic_get_port_info(nic_dev->hwdev, &port_info);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to get current settings\n");
		return -EAGAIN;
	}

	/* Alwayse set autonegation */
	if (port_info.autoneg_cap)
		set_settings |= HILINK_LINK_SET_AUTONEG;

	if (autoneg == AUTONEG_ENABLE) {
		if (!port_info.autoneg_cap) {
			nicif_err(nic_dev, drv, netdev, "Not support autoneg\n");
			return -EOPNOTSUPP;
		}
	} else if (speed != (u32)SPEED_UNKNOWN) {
		/* Set speed only when autoneg is disable */
		err = hinic_is_speed_legal(nic_dev, speed);
		if (err)
			return err;

		set_settings |= HILINK_LINK_SET_SPEED;
	} else {
		nicif_err(nic_dev, drv, netdev, "Need to set speed when autoneg is off\n");
		return -EOPNOTSUPP;
	}

	if (set_settings)
		err = hinic_set_settings_to_hw(nic_dev, set_settings,
					       autoneg, speed);
	else
		nicif_info(nic_dev, drv, netdev, "Nothing changed, exiting without setting anything\n");

	return err;
}

#ifndef HAVE_NEW_ETHTOOL_LINK_SETTINGS_ONLY
static int hinic_set_settings(struct net_device *netdev,
			      struct ethtool_cmd *link_settings)
{
	/* Only support to set autoneg and speed */
	return set_link_settings(netdev, link_settings->autoneg,
				 ethtool_cmd_speed(link_settings));
}
#endif

#ifdef ETHTOOL_GLINKSETTINGS
static int hinic_set_link_ksettings(
	struct net_device *netdev,
	const struct ethtool_link_ksettings *link_settings)
{
	/* Only support to set autoneg and speed */
	return set_link_settings(netdev, link_settings->base.autoneg,
				 link_settings->base.speed);
}
#endif

static void hinic_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *info)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct pci_dev *pdev = nic_dev->pdev;
	u8 mgmt_ver[HINIC_MGMT_VERSION_MAX_LEN] = {0};
	int err;

	strlcpy(info->driver, HINIC_DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, HINIC_DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(pdev), sizeof(info->bus_info));

	err = hinic_get_mgmt_version(nic_dev->hwdev, mgmt_ver);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to get fw version\n");
		return;
	}

	err = snprintf(info->fw_version, sizeof(info->fw_version),
		       "%s", mgmt_ver);
	if (err <= 0 || err >= (int)sizeof(info->fw_version))
		nicif_err(nic_dev, drv, netdev,
			  "Failed snprintf fw_version, function return(%d) and dest_len(%d)\n",
			  err, (int)sizeof(info->fw_version));
}

static u32 hinic_get_msglevel(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	return nic_dev->msg_enable;
}

static void hinic_set_msglevel(struct net_device *netdev, u32 data)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	nic_dev->msg_enable = data;

	nicif_info(nic_dev, drv, netdev, "Set message level: 0x%x\n", data);
}

static int hinic_nway_reset(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct nic_port_info port_info = {0};
	int err;

	if (!FUNC_SUPPORT_PORT_SETTING(nic_dev->hwdev)) {
		nicif_err(nic_dev, drv, netdev, "Current function don't support to restart autoneg\n");
		return -EOPNOTSUPP;
	}

	err = hinic_get_port_info(nic_dev->hwdev, &port_info);
	if (err) {
		nicif_err(nic_dev, drv, netdev,
			  "Get autonegotiation state failed\n");
		return  -EFAULT;
	}

	if (!port_info.autoneg_state) {
		nicif_err(nic_dev, drv, netdev,
			  "Autonegotiation is off, don't support to restart it\n");
		return  -EINVAL;
	}

	err = hinic_set_autoneg(nic_dev->hwdev, true);
	if (err) {
		nicif_err(nic_dev, drv, netdev,
			  "Restart autonegotiation failed\n");
		return -EFAULT;
	}

	nicif_info(nic_dev, drv, netdev, "Restart autonegotiation success\n");

	return 0;
}

static void hinic_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	ring->rx_max_pending = HINIC_MAX_QUEUE_DEPTH;
	ring->tx_max_pending = HINIC_MAX_QUEUE_DEPTH;
	ring->rx_pending = nic_dev->rxqs[0].q_depth;
	ring->tx_pending = nic_dev->txqs[0].q_depth;
}

static void hinic_update_qp_depth(struct hinic_nic_dev *nic_dev,
				  u16 sq_depth, u16 rq_depth)
{
	u16 i;

	nic_dev->sq_depth = sq_depth;
	nic_dev->rq_depth = rq_depth;
	for (i = 0; i < nic_dev->max_qps; i++) {
		nic_dev->txqs[i].q_depth = sq_depth;
		nic_dev->txqs[i].q_mask = sq_depth - 1;
		nic_dev->rxqs[i].q_depth = rq_depth;
		nic_dev->rxqs[i].q_mask = rq_depth - 1;
	}
}

static int hinic_set_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u16 new_sq_depth, new_rq_depth;
	int err;

	if (ring->rx_jumbo_pending || ring->rx_mini_pending) {
		nicif_err(nic_dev, drv, netdev,
			  "Unsupported rx_jumbo_pending/rx_mini_pending\n");
		return -EINVAL;
	}

	if (ring->tx_pending > HINIC_MAX_QUEUE_DEPTH ||
	    ring->tx_pending < HINIC_MIN_QUEUE_DEPTH ||
	    ring->rx_pending > HINIC_MAX_QUEUE_DEPTH ||
	    ring->rx_pending < HINIC_MIN_QUEUE_DEPTH) {
		nicif_err(nic_dev, drv, netdev,
			  "Queue depth out of rang [%d-%d]\n",
			  HINIC_MIN_QUEUE_DEPTH, HINIC_MAX_QUEUE_DEPTH);
		return -EINVAL;
	}

	new_sq_depth = (u16)(1U << (u16)ilog2(ring->tx_pending));
	new_rq_depth = (u16)(1U << (u16)ilog2(ring->rx_pending));

	if (new_sq_depth == nic_dev->sq_depth &&
	    new_rq_depth == nic_dev->rq_depth)
		return 0;

	if (test_bit(HINIC_BP_ENABLE, &nic_dev->flags) &&
	    new_rq_depth <= nic_dev->bp_upper_thd) {
		nicif_err(nic_dev, drv, netdev,
			  "BP is enable, rq_depth must be larger than upper threshold: %d\n",
			  nic_dev->bp_upper_thd);
		return -EINVAL;
	}

	nicif_info(nic_dev, drv, netdev,
		   "Change Tx/Rx ring depth from %d/%d to %d/%d\n",
		   nic_dev->sq_depth, nic_dev->rq_depth,
		   new_sq_depth, new_rq_depth);

	if (!netif_running(netdev)) {
		hinic_update_qp_depth(nic_dev, new_sq_depth, new_rq_depth);
	} else {
		nicif_info(nic_dev, drv, netdev, "Restarting netdev\n");
		err = hinic_close(netdev);
		if (err) {
			nicif_err(nic_dev, drv, netdev,
				  "Failed to close netdev\n");
			return -EFAULT;
		}

		hinic_update_qp_depth(nic_dev, new_sq_depth, new_rq_depth);

		err = hinic_open(netdev);
		if (err) {
			nicif_err(nic_dev, drv, netdev,
				  "Failed to open netdev\n");
			return -EFAULT;
		}
	}

	return 0;
}

static u16 hinic_max_channels(struct hinic_nic_dev *nic_dev)
{
	u8 tcs = (u8)netdev_get_num_tc(nic_dev->netdev);

	return tcs ? nic_dev->max_qps / tcs : nic_dev->max_qps;
}

static u16 hinic_curr_channels(struct hinic_nic_dev *nic_dev)
{
	if (netif_running(nic_dev->netdev))
		return nic_dev->num_rss ? nic_dev->num_rss : 1;
	else
		return min_t(u16, hinic_max_channels(nic_dev),
			     nic_dev->rss_limit);
}

static void hinic_get_channels(struct net_device *netdev,
			       struct ethtool_channels *channels)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	channels->max_rx = 0;
	channels->max_tx = 0;
	channels->max_other = 0;
	channels->max_combined = hinic_max_channels(nic_dev);
	channels->rx_count = 0;
	channels->tx_count = 0;
	channels->other_count = 0;
	channels->combined_count = hinic_curr_channels(nic_dev);
}

void hinic_update_num_qps(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u16 num_qps;
	u8 tcs;

	/* change num_qps to change counter in ethtool -S */
	tcs = (u8)netdev_get_num_tc(nic_dev->netdev);
	num_qps = (u16)(nic_dev->rss_limit * (tcs ? tcs : 1));
	nic_dev->num_qps = min_t(u16, nic_dev->max_qps, num_qps);
}

static int hinic_set_channels(struct net_device *netdev,
			      struct ethtool_channels *channels)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	unsigned int count = channels->combined_count;
	int err;

	if (!count) {
		nicif_err(nic_dev, drv, netdev,
			  "Unsupported combined_count=0\n");
		return -EINVAL;
	}

	if (channels->tx_count || channels->rx_count || channels->other_count) {
		nicif_err(nic_dev, drv, netdev,
			  "Setting rx/tx/other count not supported\n");
		return -EINVAL;
	}

	if (!test_bit(HINIC_RSS_ENABLE, &nic_dev->flags)) {
		nicif_err(nic_dev, drv, netdev,
			  "This function don't support RSS, only support 1 queue pair\n");
		return -EOPNOTSUPP;
	}

	if (count > hinic_max_channels(nic_dev)) {
		nicif_err(nic_dev, drv, netdev,
			  "Combined count %d exceed limit %d\n",
			  count, hinic_max_channels(nic_dev));
		return -EINVAL;
	}

	nicif_info(nic_dev, drv, netdev, "Set max combined queue number from %d to %d\n",
		   nic_dev->rss_limit, count);
	nic_dev->rss_limit = (u16)count;

	if (netif_running(netdev)) {
		nicif_info(nic_dev, drv, netdev, "Restarting netdev\n");
		err = hinic_close(netdev);
		if (err) {
			nicif_err(nic_dev, drv, netdev,
				  "Failed to close netdev\n");
			return -EFAULT;
		}
		/* Discard user configured rss */
		hinic_set_default_rss_indir(netdev);

		err = hinic_open(netdev);
		if (err) {
			nicif_err(nic_dev, drv, netdev,
				  "Failed to open netdev\n");
			return -EFAULT;
		}
	} else {
		/* Discard user configured rss */
		hinic_set_default_rss_indir(netdev);

		hinic_update_num_qps(netdev);
	}

	return 0;
}

static int hinic_get_sset_count(struct net_device *netdev, int sset)
{
	int count = 0, q_num = 0;
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	switch (sset) {
	case ETH_SS_TEST:
		return ARRAY_LEN(hinic_test_strings);
	case ETH_SS_STATS:
		q_num = nic_dev->num_qps;
		count = ARRAY_LEN(hinic_netdev_stats) +
			ARRAY_LEN(hinic_nic_dev_stats) +
			(ARRAY_LEN(hinic_tx_queue_stats) +
			ARRAY_LEN(hinic_rx_queue_stats)) * q_num;

		if (!HINIC_FUNC_IS_VF(nic_dev->hwdev))
			count += ARRAY_LEN(hinic_function_stats);

		if (!HINIC_FUNC_IS_VF(nic_dev->hwdev) &&
		    FUNC_SUPPORT_PORT_SETTING(nic_dev->hwdev))
			count += ARRAY_LEN(hinic_port_stats);

		return count;
	default:
		return -EOPNOTSUPP;
	}
}

#define COALESCE_ALL_QUEUE		0xFFFF
#define COALESCE_MAX_PENDING_LIMIT	(255 * COALESCE_PENDING_LIMIT_UNIT)
#define COALESCE_MAX_TIMER_CFG		(255 * COALESCE_TIMER_CFG_UNIT)
#define COALESCE_PENDING_LIMIT_UNIT	8
#define	COALESCE_TIMER_CFG_UNIT		9

static int __hinic_get_coalesce(struct net_device *netdev,
				struct ethtool_coalesce *coal, u16 queue)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_intr_coal_info *interrupt_info;

	if (queue == COALESCE_ALL_QUEUE) {
		/* get tx/rx irq0 as default parameters */
		interrupt_info = &nic_dev->intr_coalesce[0];
	} else {
		if (queue >= nic_dev->num_qps) {
			nicif_err(nic_dev, drv, netdev,
				  "Invalid queue_id: %d\n", queue);
			return -EINVAL;
		}
		interrupt_info = &nic_dev->intr_coalesce[queue];
	}

	/* coalescs_timer is in unit of 9us */
	coal->rx_coalesce_usecs = interrupt_info->coalesce_timer_cfg *
			COALESCE_TIMER_CFG_UNIT;
	/* coalescs_frams is in unit of 8 */
	coal->rx_max_coalesced_frames = interrupt_info->pending_limt *
			COALESCE_PENDING_LIMIT_UNIT;

	/* tx/rx use the same interrupt */
	coal->tx_coalesce_usecs = coal->rx_coalesce_usecs;
	coal->tx_max_coalesced_frames = coal->rx_max_coalesced_frames;
	coal->use_adaptive_rx_coalesce = nic_dev->adaptive_rx_coal;

	coal->pkt_rate_high = (u32)interrupt_info->pkt_rate_high;
	coal->rx_coalesce_usecs_high = interrupt_info->rx_usecs_high *
				       COALESCE_TIMER_CFG_UNIT;
	coal->rx_max_coalesced_frames_high =
					interrupt_info->rx_pending_limt_high *
					COALESCE_PENDING_LIMIT_UNIT;

	coal->pkt_rate_low = (u32)interrupt_info->pkt_rate_low;
	coal->rx_coalesce_usecs_low = interrupt_info->rx_usecs_low *
				      COALESCE_TIMER_CFG_UNIT;
	coal->rx_max_coalesced_frames_low =
					interrupt_info->rx_pending_limt_low *
					COALESCE_PENDING_LIMIT_UNIT;

	return 0;
}

static int set_queue_coalesce(struct hinic_nic_dev *nic_dev, u16 q_id,
			      struct hinic_intr_coal_info *coal)
{
	struct hinic_intr_coal_info *intr_coal;
	struct nic_interrupt_info interrupt_info = {0};
	struct net_device *netdev = nic_dev->netdev;
	int err;

	intr_coal = &nic_dev->intr_coalesce[q_id];
	if ((intr_coal->coalesce_timer_cfg != coal->coalesce_timer_cfg) ||
	    (intr_coal->pending_limt != coal->pending_limt))
		intr_coal->user_set_intr_coal_flag = 1;

	intr_coal->coalesce_timer_cfg = coal->coalesce_timer_cfg;
	intr_coal->pending_limt = coal->pending_limt;
	intr_coal->pkt_rate_low = coal->pkt_rate_low;
	intr_coal->rx_usecs_low = coal->rx_usecs_low;
	intr_coal->rx_pending_limt_low = coal->rx_pending_limt_low;
	intr_coal->pkt_rate_high = coal->pkt_rate_high;
	intr_coal->rx_usecs_high = coal->rx_usecs_high;
	intr_coal->rx_pending_limt_high = coal->rx_pending_limt_high;

	/* netdev not running or qp not in using,
	 * don't need to set coalesce to hw
	 */
	if (!test_bit(HINIC_INTF_UP, &nic_dev->flags) ||
	    q_id >= nic_dev->num_qps || nic_dev->adaptive_rx_coal)
		return 0;

	interrupt_info.msix_index = nic_dev->irq_cfg[q_id].msix_entry_idx;
	interrupt_info.lli_set = 0;
	interrupt_info.interrupt_coalesc_set = 1;
	interrupt_info.coalesc_timer_cfg = intr_coal->coalesce_timer_cfg;
	interrupt_info.pending_limt = intr_coal->pending_limt;
	interrupt_info.resend_timer_cfg = intr_coal->resend_timer_cfg;
	nic_dev->rxqs[q_id].last_coalesc_timer_cfg =
					intr_coal->coalesce_timer_cfg;
	nic_dev->rxqs[q_id].last_pending_limt = intr_coal->pending_limt;
	err = hinic_set_interrupt_cfg(nic_dev->hwdev, interrupt_info);
	if (err)
		nicif_warn(nic_dev, drv, netdev,
			   "Failed to set queue%d coalesce", q_id);

	return err;
}

static int is_coalesce_legal(struct net_device *netdev,
			     const struct ethtool_coalesce *coal)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct ethtool_coalesce tmp_coal = {0};

	if (coal->rx_coalesce_usecs != coal->tx_coalesce_usecs) {
		nicif_err(nic_dev, drv, netdev,
			  "tx-usecs must be equal to rx-usecs\n");
		return -EINVAL;
	}

	if (coal->rx_max_coalesced_frames != coal->tx_max_coalesced_frames) {
		nicif_err(nic_dev, drv, netdev,
			  "tx-frames must be equal to rx-frames\n");
		return -EINVAL;
	}

	tmp_coal.cmd = coal->cmd;
	tmp_coal.rx_coalesce_usecs = coal->rx_coalesce_usecs;
	tmp_coal.rx_max_coalesced_frames = coal->rx_max_coalesced_frames;
	tmp_coal.tx_coalesce_usecs = coal->tx_coalesce_usecs;
	tmp_coal.tx_max_coalesced_frames = coal->tx_max_coalesced_frames;
	tmp_coal.use_adaptive_rx_coalesce = coal->use_adaptive_rx_coalesce;

	tmp_coal.pkt_rate_low = coal->pkt_rate_low;
	tmp_coal.rx_coalesce_usecs_low = coal->rx_coalesce_usecs_low;
	tmp_coal.rx_max_coalesced_frames_low =
					coal->rx_max_coalesced_frames_low;

	tmp_coal.pkt_rate_high = coal->pkt_rate_high;
	tmp_coal.rx_coalesce_usecs_high = coal->rx_coalesce_usecs_high;
	tmp_coal.rx_max_coalesced_frames_high =
					coal->rx_max_coalesced_frames_high;

	if (memcmp(coal, &tmp_coal, sizeof(struct ethtool_coalesce))) {
		nicif_err(nic_dev, drv, netdev,
			  "Only support to change rx/tx-usecs and rx/tx-frames\n");
		return -EOPNOTSUPP;
	}

	if (coal->rx_coalesce_usecs > COALESCE_MAX_TIMER_CFG) {
		nicif_err(nic_dev, drv, netdev,
			  "rx_coalesce_usecs out of range[%d-%d]\n", 0,
			  COALESCE_MAX_TIMER_CFG);
		return -EOPNOTSUPP;
	}

	if (coal->rx_max_coalesced_frames > COALESCE_MAX_PENDING_LIMIT) {
		nicif_err(nic_dev, drv, netdev,
			  "rx_max_coalesced_frames out of range[%d-%d]\n", 0,
			  COALESCE_MAX_PENDING_LIMIT);
		return -EOPNOTSUPP;
	}

	if (coal->rx_coalesce_usecs_low > COALESCE_MAX_TIMER_CFG) {
		nicif_err(nic_dev, drv, netdev,
			  "rx_coalesce_usecs_low out of range[%d-%d]\n", 0,
			  COALESCE_MAX_TIMER_CFG);
		return -EOPNOTSUPP;
	}

	if (coal->rx_max_coalesced_frames_low > COALESCE_MAX_PENDING_LIMIT) {
		nicif_err(nic_dev, drv, netdev,
			  "rx_max_coalesced_frames_low out of range[%d-%d]\n",
			  0, COALESCE_MAX_PENDING_LIMIT);
		return -EOPNOTSUPP;
	}

	if (coal->rx_coalesce_usecs_high > COALESCE_MAX_TIMER_CFG) {
		nicif_err(nic_dev, drv, netdev,
			  "rx_coalesce_usecs_high out of range[%d-%d]\n", 0,
			  COALESCE_MAX_TIMER_CFG);
		return -EOPNOTSUPP;
	}

	if (coal->rx_max_coalesced_frames_high > COALESCE_MAX_PENDING_LIMIT) {
		nicif_err(nic_dev, drv, netdev,
			  "rx_max_coalesced_frames_high out of range[%d-%d]\n",
			  0, COALESCE_MAX_PENDING_LIMIT);
		return -EOPNOTSUPP;
	}

	if (coal->rx_coalesce_usecs_low / COALESCE_TIMER_CFG_UNIT >=
	    coal->rx_coalesce_usecs_high / COALESCE_TIMER_CFG_UNIT) {
		nicif_err(nic_dev, drv, netdev,
			  "coalesce_usecs_high(%u) must more than coalesce_usecs_low(%u), after dividing %d usecs unit\n",
			  coal->rx_coalesce_usecs_high,
			  coal->rx_coalesce_usecs_low,
			  COALESCE_TIMER_CFG_UNIT);
		return -EOPNOTSUPP;
	}

	if (coal->rx_max_coalesced_frames_low / COALESCE_PENDING_LIMIT_UNIT >=
	    coal->rx_max_coalesced_frames_high / COALESCE_PENDING_LIMIT_UNIT) {
		nicif_err(nic_dev, drv, netdev,
			  "coalesced_frames_high(%u) must more than coalesced_frames_low(%u), after dividing %d frames unit\n",
			  coal->rx_max_coalesced_frames_high,
			  coal->rx_max_coalesced_frames_low,
			  COALESCE_PENDING_LIMIT_UNIT);
		return -EOPNOTSUPP;
	}

	if (coal->pkt_rate_low >= coal->pkt_rate_high) {
		nicif_err(nic_dev, drv, netdev,
			  "pkt_rate_high(%u) must more than pkt_rate_low(%u)\n",
			  coal->pkt_rate_high,
			  coal->pkt_rate_low);
		return -EOPNOTSUPP;
	}

	return 0;
}

#define CHECK_COALESCE_ALIGN(coal, item, unit)				\
do {									\
	if (coal->item % (unit))					\
		nicif_warn(nic_dev, drv, netdev,			\
			   "%s in %d units, change to %d\n",		\
			   #item, (unit), ALIGN_DOWN(coal->item, unit));\
} while (0)

#define CHECK_COALESCE_CHANGED(coal, item, unit, ori_val, obj_str)	\
do {									\
	if ((coal->item / (unit)) != (ori_val))				\
		nicif_info(nic_dev, drv, netdev,			\
			   "Change %s from %d to %d %s\n",		\
			   #item, (ori_val) * (unit),			\
			   ALIGN_DOWN(coal->item, unit), (obj_str));	\
} while (0)

#define CHECK_PKT_RATE_CHANGED(coal, item, ori_val, obj_str)		\
do {									\
	if (coal->item != (ori_val))					\
		nicif_info(nic_dev, drv, netdev,			\
			   "Change %s from %llu to %u %s\n",		\
			   #item, (ori_val), coal->item, (obj_str));	\
} while (0)

static int __hinic_set_coalesce(struct net_device *netdev,
				struct ethtool_coalesce *coal, u16 queue)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_intr_coal_info intr_coal = {0};
	struct hinic_intr_coal_info *ori_intr_coal;
	char obj_str[OBJ_STR_MAX_LEN] = {0};
	u16 i;
	int err = 0;

	err = is_coalesce_legal(netdev, coal);
	if (err)
		return err;

	CHECK_COALESCE_ALIGN(coal, rx_coalesce_usecs, COALESCE_TIMER_CFG_UNIT);
	CHECK_COALESCE_ALIGN(coal, rx_max_coalesced_frames,
			     COALESCE_PENDING_LIMIT_UNIT);
	CHECK_COALESCE_ALIGN(coal, rx_coalesce_usecs_high,
			     COALESCE_TIMER_CFG_UNIT);
	CHECK_COALESCE_ALIGN(coal, rx_max_coalesced_frames_high,
			     COALESCE_PENDING_LIMIT_UNIT);
	CHECK_COALESCE_ALIGN(coal, rx_coalesce_usecs_low,
			     COALESCE_TIMER_CFG_UNIT);
	CHECK_COALESCE_ALIGN(coal, rx_max_coalesced_frames_low,
			     COALESCE_PENDING_LIMIT_UNIT);

	if (queue == COALESCE_ALL_QUEUE) {
		ori_intr_coal = &nic_dev->intr_coalesce[0];
		err = snprintf(obj_str, sizeof(obj_str), "for netdev");
		if (err <= 0 || err >= OBJ_STR_MAX_LEN) {
			nicif_err(nic_dev, drv, netdev,
				  "Failed snprintf string, function return(%d) and dest_len(%d)\n",
				  err, OBJ_STR_MAX_LEN);
			return -EFAULT;
		}
	} else {
		ori_intr_coal = &nic_dev->intr_coalesce[queue];
		err = snprintf(obj_str, sizeof(obj_str), "for queue %d", queue);
		if (err <= 0 || err >= OBJ_STR_MAX_LEN) {
			nicif_err(nic_dev, drv, netdev,
				  "Failed snprintf string, function return(%d) and dest_len(%d)\n",
				  err, OBJ_STR_MAX_LEN);
			return -EFAULT;
		}
	}
	CHECK_COALESCE_CHANGED(coal, rx_coalesce_usecs, COALESCE_TIMER_CFG_UNIT,
			       ori_intr_coal->coalesce_timer_cfg, obj_str);
	CHECK_COALESCE_CHANGED(coal, rx_max_coalesced_frames,
			       COALESCE_PENDING_LIMIT_UNIT,
			       ori_intr_coal->pending_limt, obj_str);
	CHECK_PKT_RATE_CHANGED(coal, pkt_rate_high,
			       ori_intr_coal->pkt_rate_high, obj_str);
	CHECK_COALESCE_CHANGED(coal, rx_coalesce_usecs_high,
			       COALESCE_TIMER_CFG_UNIT,
			       ori_intr_coal->rx_usecs_high, obj_str);
	CHECK_COALESCE_CHANGED(coal, rx_max_coalesced_frames_high,
			       COALESCE_PENDING_LIMIT_UNIT,
			       ori_intr_coal->rx_pending_limt_high, obj_str);
	CHECK_PKT_RATE_CHANGED(coal, pkt_rate_low,
			       ori_intr_coal->pkt_rate_low, obj_str);
	CHECK_COALESCE_CHANGED(coal, rx_coalesce_usecs_low,
			       COALESCE_TIMER_CFG_UNIT,
			       ori_intr_coal->rx_usecs_low, obj_str);
	CHECK_COALESCE_CHANGED(coal, rx_max_coalesced_frames_low,
			       COALESCE_PENDING_LIMIT_UNIT,
			       ori_intr_coal->rx_pending_limt_low, obj_str);

	intr_coal.coalesce_timer_cfg =
		(u8)(coal->rx_coalesce_usecs / COALESCE_TIMER_CFG_UNIT);
	intr_coal.pending_limt = (u8)(coal->rx_max_coalesced_frames /
				      COALESCE_PENDING_LIMIT_UNIT);

	nic_dev->adaptive_rx_coal = coal->use_adaptive_rx_coalesce;

	intr_coal.pkt_rate_high = coal->pkt_rate_high;
	intr_coal.rx_usecs_high =
		(u8)(coal->rx_coalesce_usecs_high / COALESCE_TIMER_CFG_UNIT);
	intr_coal.rx_pending_limt_high =
		(u8)(coal->rx_max_coalesced_frames_high /
		     COALESCE_PENDING_LIMIT_UNIT);

	intr_coal.pkt_rate_low = coal->pkt_rate_low;
	intr_coal.rx_usecs_low =
		(u8)(coal->rx_coalesce_usecs_low / COALESCE_TIMER_CFG_UNIT);
	intr_coal.rx_pending_limt_low =
		(u8)(coal->rx_max_coalesced_frames_low /
		     COALESCE_PENDING_LIMIT_UNIT);

	/* coalesce timer or pending set to zero will disable coalesce */
	if (!nic_dev->adaptive_rx_coal &&
	    (!intr_coal.coalesce_timer_cfg || !intr_coal.pending_limt))
		nicif_warn(nic_dev, drv, netdev, "Coalesce will be disabled\n");

	if (queue == COALESCE_ALL_QUEUE) {
		for (i = 0; i < nic_dev->max_qps; i++)
			set_queue_coalesce(nic_dev, i, &intr_coal);
	} else {
		if (queue >= nic_dev->num_qps) {
			nicif_err(nic_dev, drv, netdev,
				  "Invalid queue_id: %d\n", queue);
			return -EINVAL;
		}
		set_queue_coalesce(nic_dev, queue, &intr_coal);
	}

	return 0;
}

static int hinic_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coal)
{
	return __hinic_get_coalesce(netdev, coal, COALESCE_ALL_QUEUE);
}

static int hinic_set_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *coal)
{
	return __hinic_set_coalesce(netdev, coal, COALESCE_ALL_QUEUE);
}

#if defined(ETHTOOL_PERQUEUE) && defined(ETHTOOL_GCOALESCE)
static int hinic_get_per_queue_coalesce(struct net_device *netdev, u32 queue,
					struct ethtool_coalesce *coal)
{
	return __hinic_get_coalesce(netdev, coal, queue);
}

static int hinic_set_per_queue_coalesce(struct net_device *netdev, u32 queue,
					struct ethtool_coalesce *coal)
{
	return __hinic_set_coalesce(netdev, coal, queue);
}
#endif

static void get_drv_queue_stats(struct hinic_nic_dev *nic_dev, u64 *data)
{
	struct hinic_txq_stats txq_stats;
	struct hinic_rxq_stats rxq_stats;
	u16 i = 0, j = 0, qid = 0;
	char *p;

	for (qid = 0; qid < nic_dev->num_qps; qid++) {
		if (!nic_dev->txqs)
			break;

		hinic_txq_get_stats(&nic_dev->txqs[qid], &txq_stats);
		for (j = 0; j < ARRAY_LEN(hinic_tx_queue_stats); j++, i++) {
			p = (char *)(&txq_stats) +
				hinic_tx_queue_stats[j].offset;
			data[i] = (hinic_tx_queue_stats[j].size ==
					sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
		}
	}

	for (qid = 0; qid < nic_dev->num_qps; qid++) {
		if (!nic_dev->rxqs)
			break;

		hinic_rxq_get_stats(&nic_dev->rxqs[qid], &rxq_stats);
		for (j = 0; j < ARRAY_LEN(hinic_rx_queue_stats); j++, i++) {
			p = (char *)(&rxq_stats) +
				hinic_rx_queue_stats[j].offset;
			data[i] = (hinic_rx_queue_stats[j].size ==
					sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
		}
	}
}

static void hinic_get_ethtool_stats(struct net_device *netdev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
#ifdef HAVE_NDO_GET_STATS64
	struct rtnl_link_stats64 temp;
	const struct rtnl_link_stats64 *net_stats;
#else
	const struct net_device_stats *net_stats;
#endif
	struct hinic_phy_port_stats *port_stats;
	struct hinic_nic_stats *nic_stats;
	struct hinic_vport_stats vport_stats = {0};
	u16 i = 0, j = 0;
	char *p;
	int err;

#ifdef HAVE_NDO_GET_STATS64
	net_stats = dev_get_stats(netdev, &temp);
#else
	net_stats = dev_get_stats(netdev);
#endif
	for (j = 0; j < ARRAY_LEN(hinic_netdev_stats); j++, i++) {
		p = (char *)(net_stats) + hinic_netdev_stats[j].offset;
		data[i] = (hinic_netdev_stats[j].size ==
				sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}

	nic_stats = &nic_dev->stats;
	for (j = 0; j < ARRAY_LEN(hinic_nic_dev_stats); j++, i++) {
		p = (char *)(nic_stats) + hinic_nic_dev_stats[j].offset;
		data[i] = (hinic_nic_dev_stats[j].size ==
				sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}

	if (!HINIC_FUNC_IS_VF(nic_dev->hwdev)) {
		err = hinic_get_vport_stats(nic_dev->hwdev, &vport_stats);
		if (err)
			nicif_err(nic_dev, drv, netdev,
				  "Failed to get function stats from fw\n");

		for (j = 0; j < ARRAY_LEN(hinic_function_stats); j++, i++) {
			p = (char *)(&vport_stats) +
					hinic_function_stats[j].offset;
			data[i] = (hinic_function_stats[j].size ==
					sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
		}
	}

	if (!HINIC_FUNC_IS_VF(nic_dev->hwdev) &&
	    FUNC_SUPPORT_PORT_SETTING(nic_dev->hwdev)) {
		port_stats = kzalloc(sizeof(*port_stats), GFP_KERNEL);
		if (!port_stats) {
			nicif_err(nic_dev, drv, netdev,
				  "Failed to malloc port stats\n");
			memset(&data[i], 0,
			       ARRAY_LEN(hinic_port_stats) * sizeof(*data));
			i += ARRAY_LEN(hinic_port_stats);
			goto get_drv_stats;
		}

		err = hinic_get_phy_port_stats(nic_dev->hwdev, port_stats);
		if (err)
			nicif_err(nic_dev, drv, netdev,
				  "Failed to get port stats from fw\n");

		for (j = 0; j < ARRAY_LEN(hinic_port_stats); j++, i++) {
			p = (char *)(port_stats) + hinic_port_stats[j].offset;
			data[i] = (hinic_port_stats[j].size ==
					sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
		}

		kfree(port_stats);
	}

get_drv_stats:
	get_drv_queue_stats(nic_dev, data + i);
}

static void hinic_get_strings(struct net_device *netdev,
			      u32 stringset, u8 *data)
{
	u16 i = 0, j = 0;
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	char *p = (char *)data;

	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, *hinic_test_strings, sizeof(hinic_test_strings));
		return;
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_LEN(hinic_netdev_stats); i++) {
			memcpy(p, hinic_netdev_stats[i].name,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < ARRAY_LEN(hinic_nic_dev_stats); i++) {
			memcpy(p, hinic_nic_dev_stats[i].name, ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		if (!HINIC_FUNC_IS_VF(nic_dev->hwdev)) {
			for (i = 0; i < ARRAY_LEN(hinic_function_stats); i++) {
				memcpy(p, hinic_function_stats[i].name,
				       ETH_GSTRING_LEN);
				p += ETH_GSTRING_LEN;
			}
		}

		if (!HINIC_FUNC_IS_VF(nic_dev->hwdev) &&
		    FUNC_SUPPORT_PORT_SETTING(nic_dev->hwdev)) {
			for (i = 0; i < ARRAY_LEN(hinic_port_stats); i++) {
				memcpy(p, hinic_port_stats[i].name,
				       ETH_GSTRING_LEN);
				p += ETH_GSTRING_LEN;
			}
		}

		for (i = 0; i < nic_dev->num_qps; i++) {
			for (j = 0; j < ARRAY_LEN(hinic_tx_queue_stats); j++) {
				sprintf(p, hinic_tx_queue_stats[j].name, i);
				p += ETH_GSTRING_LEN;
			}
		}

		for (i = 0; i < nic_dev->num_qps; i++) {
			for (j = 0; j < ARRAY_LEN(hinic_rx_queue_stats); j++) {
				sprintf(p, hinic_rx_queue_stats[j].name, i);
				p += ETH_GSTRING_LEN;
			}
		}

		return;
	default:
		nicif_err(nic_dev, drv, netdev,
			  "Invalid string set %d.", stringset);
		return;
	}
}

#ifdef HAVE_ETHTOOL_SET_PHYS_ID
static int hinic_set_phys_id(struct net_device *netdev,
			     enum ethtool_phys_id_state state)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u8 port;
	int err;

	if (!FUNC_SUPPORT_PORT_SETTING(nic_dev->hwdev)) {
		nicif_err(nic_dev, drv, netdev, "Current function don't support to set LED status\n");
		return -EOPNOTSUPP;
	}

	port = hinic_physical_port_id(nic_dev->hwdev);

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		err = hinic_set_led_status(nic_dev->hwdev, port,
					   HINIC_LED_TYPE_LINK,
					   HINIC_LED_MODE_FORCE_2HZ);
		if (err)
			nicif_err(nic_dev, drv, netdev,
				  "Set LED blinking in 2HZ failed\n");
		else
			nicif_info(nic_dev, drv, netdev,
				   "Set LED blinking in 2HZ success\n");
		break;

	case ETHTOOL_ID_INACTIVE:
		err = hinic_reset_led_status(nic_dev->hwdev, port);
		if (err)
			nicif_err(nic_dev, drv, netdev,
				  "Reset LED to original status failed\n");
		else
			nicif_info(nic_dev, drv, netdev,
				   "Reset LED to original status success\n");

		break;

	default:
		return -EOPNOTSUPP;
	}

	if (err)
		return -EFAULT;
	else
		return 0;
}
#else
/* TODO: implement */
static int hinic_phys_id(struct net_device *netdev, u32 data)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	nicif_err(nic_dev, drv, netdev, "Not support to set phys id\n");

	return -EOPNOTSUPP;
}
#endif

static void hinic_get_pauseparam(struct net_device *netdev,
				 struct ethtool_pauseparam *pause)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct nic_pause_config nic_pause = {0};
	int err;

	err = hinic_get_pause_info(nic_dev->hwdev, &nic_pause);
	if (err) {
		nicif_err(nic_dev, drv, netdev,
			  "Failed to get pauseparam from hw\n");
	} else {
		pause->autoneg = nic_pause.auto_neg;
		pause->rx_pause = nic_pause.rx_pause;
		pause->tx_pause = nic_pause.tx_pause;
	}
}

static int hinic_set_pauseparam(struct net_device *netdev,
				struct ethtool_pauseparam *pause)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct nic_pause_config nic_pause = {0};
	struct nic_port_info port_info = {0};
	int err;

	if (!FUNC_SUPPORT_PORT_SETTING(nic_dev->hwdev)) {
		nicif_err(nic_dev, drv, netdev, "Not support to set pause parameters\n");
		return -EOPNOTSUPP;
	}

	if (test_bit(HINIC_BP_ENABLE, &nic_dev->flags) && pause->tx_pause &&
	    nic_dev->rq_depth <= nic_dev->bp_upper_thd) {
		nicif_err(nic_dev, drv, netdev,
			  "Can not set tx pause enable, rq depth is less than bp upper threshold: %d\n",
			  nic_dev->bp_upper_thd);
		return -EINVAL;
	}

	err = hinic_get_port_info(nic_dev->hwdev, &port_info);
	if (err) {
		nicif_err(nic_dev, drv, netdev,
			  "Failed to get auto-negotiation state\n");
		return -EFAULT;
	}

	if (pause->autoneg != port_info.autoneg_state) {
		nicif_err(nic_dev, drv, netdev,
			  "To change autoneg please use: ethtool -s <dev> autoneg <on|off>\n");
		return -EOPNOTSUPP;
	}

	nic_pause.auto_neg = pause->autoneg;
	nic_pause.rx_pause = pause->rx_pause;
	nic_pause.tx_pause = pause->tx_pause;

	err = hinic_set_pause_info(nic_dev->hwdev, nic_pause);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to set pauseparam\n");
		return -EFAULT;
	}

	nicif_info(nic_dev, drv, netdev, "Set pause options, tx: %s, rx: %s\n",
		   pause->tx_pause ? "on" : "off",
		   pause->rx_pause ? "on" : "off");

	return 0;
}

static int hinic_run_lp_test(struct hinic_nic_dev *nic_dev, u32 test_time)
{
	u32 i;
	u8 j;
	u32 cnt = test_time * 5;
	struct sk_buff *skb = NULL;
	struct sk_buff *skb_tmp = NULL;
	u8 *test_data = NULL;
	u8 *lb_test_rx_buf = nic_dev->lb_test_rx_buf;
	struct net_device *netdev = nic_dev->netdev;

	skb_tmp = alloc_skb(LP_PKT_LEN, GFP_ATOMIC);
	if (!skb_tmp) {
		nicif_err(nic_dev, drv, netdev,
			  "Alloc xmit skb template failed for loopback test\n");
		return -ENOMEM;
	}

	test_data = __skb_put(skb_tmp, LP_PKT_LEN);

	memset(test_data, 0xFF, (2 * ETH_ALEN));
	test_data[ETH_ALEN] = 0xFE;
	test_data[2 * ETH_ALEN] = 0x08;
	test_data[2 * ETH_ALEN + 1] = 0x0;

	for (i = ETH_HLEN; i < LP_PKT_LEN; i++)
		test_data[i] = i & 0xFF;

	skb_tmp->queue_mapping = 0;
	skb_tmp->ip_summed = CHECKSUM_COMPLETE;
	skb_tmp->dev = netdev;

	for (i = 0; i < cnt; i++) {
		nic_dev->lb_test_rx_idx = 0;
		memset(lb_test_rx_buf, 0, (LP_PKT_CNT * LP_PKT_LEN));

		for (j = 0; j < LP_PKT_CNT; j++) {
			skb = pskb_copy(skb_tmp, GFP_ATOMIC);
			if (!skb) {
				dev_kfree_skb_any(skb_tmp);
				nicif_err(nic_dev, drv, netdev,
					  "Copy skb failed for loopback test\n");
				return -ENOMEM;
			}

			/* mark index for every pkt */
			skb->data[LP_PKT_LEN - 1] = j;

			if (hinic_lb_xmit_frame(skb, netdev)) {
				dev_kfree_skb_any(skb);
				dev_kfree_skb_any(skb_tmp);
				nicif_err(nic_dev, drv, netdev,
					  "Xmit pkt failed for loopback test\n");
				return -EBUSY;
			}
		}

		/* wait till all pkt received to rx buffer */
		msleep(200);

		for (j = 0; j < LP_PKT_CNT; j++) {
			if (memcmp((lb_test_rx_buf + (j * LP_PKT_LEN)),
				   skb_tmp->data, (LP_PKT_LEN - 1)) ||
				   (*(lb_test_rx_buf + ((j * LP_PKT_LEN) +
				   (LP_PKT_LEN - 1))) != j)) {
				dev_kfree_skb_any(skb_tmp);
				nicif_err(nic_dev, drv, netdev,
					  "Compare pkt failed in loopback test(index=0x%02x, data[%d]=0x%02x)\n",
					  (j + (i * LP_PKT_CNT)),
					  (LP_PKT_LEN - 1),
					  *(lb_test_rx_buf +
					  (((j * LP_PKT_LEN) +
					  (LP_PKT_LEN - 1)))));
				return -EIO;
			}
		}
	}

	dev_kfree_skb_any(skb_tmp);
	nicif_info(nic_dev, drv, netdev, "Loopback test succeed.\n");
	return 0;
}

void hinic_lp_test(struct net_device *netdev, struct ethtool_test *eth_test,
		   u64 *data, u32 test_time)
{
	int err = 0;
	u8 link_status = 0;
	u8 *lb_test_rx_buf = NULL;
	struct ethtool_test test = {0};
	enum diag_test_index test_index = 0;
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	memset(data, 0, (DIAG_TEST_MAX * sizeof(u64)));

	/* Do not support loopback test when netdev is closed. */
	if (!test_bit(HINIC_INTF_UP, &nic_dev->flags)) {
		nicif_err(nic_dev, drv, netdev,
			  "Do not support loopback test when netdev is closed.\n");
		eth_test->flags |= ETH_TEST_FL_FAILED;
		data[PORT_DOWN_ERR_IDX] = 1;
		return;
	}

	test.flags = eth_test->flags;

	if (test_time == 0)
		test_time = LP_DEFAULT_TIME;

	netif_carrier_off(netdev);

	if (!(test.flags & ETH_TEST_FL_EXTERNAL_LB)) {
		test_index = INTERNAL_LP_TEST;

		if (hinic_set_loopback_mode(nic_dev->hwdev, true)) {
			nicif_err(nic_dev, drv, netdev,
				  "Failed to set port loopback mode before loopback test\n");
			err = 1;
			goto resume_link;
		}
	} else {
		test_index = EXTERNAL_LP_TEST;
	}

	lb_test_rx_buf = vmalloc(LP_PKT_CNT * LP_PKT_LEN);
	if (!lb_test_rx_buf) {
		nicif_err(nic_dev, drv, netdev,
			  "Failed to alloc rx buffer for loopback test.\n");
		err = 1;
	} else {
		nic_dev->lb_test_rx_buf = lb_test_rx_buf;
		nic_dev->lb_pkt_len = LP_PKT_LEN;
		set_bit(HINIC_LP_TEST, &nic_dev->flags);

		if (hinic_run_lp_test(nic_dev, test_time))
			err = 1;

		clear_bit(HINIC_LP_TEST, &nic_dev->flags);
		msleep(100);
		vfree(lb_test_rx_buf);
		nic_dev->lb_test_rx_buf = NULL;
	}

	if (!(test.flags & ETH_TEST_FL_EXTERNAL_LB)) {
		if (hinic_set_loopback_mode(nic_dev->hwdev, false)) {
			nicif_err(nic_dev, drv, netdev,
				  "Failed to cancel port loopback mode after loopback test.\n");
			err = 1;

			goto resume_link;
		}
	}

resume_link:
	if (err) {
		eth_test->flags |= ETH_TEST_FL_FAILED;
		data[test_index] = 1;
	}

	err = hinic_get_link_state(nic_dev->hwdev, &link_status);
	if (!err && link_status)
		netif_carrier_on(netdev);
}

static void hinic_diag_test(struct net_device *netdev,
			    struct ethtool_test *eth_test, u64 *data)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (!FUNC_SUPPORT_PORT_SETTING(nic_dev->hwdev)) {
		nicif_err(nic_dev, drv, netdev, "Current function don't support self test\n");
		return;
	}

	hinic_lp_test(netdev, eth_test, data, 0);
}

#ifdef ETHTOOL_GMODULEEEPROM
static int hinic_get_module_info(struct net_device *netdev,
				 struct ethtool_modinfo *modinfo)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u8 sfp_type;
	u8 sfp_type_ext;
	int err;

	err = hinic_get_sfp_type(nic_dev->hwdev, &sfp_type, &sfp_type_ext);
	if (err)
		return err;

	switch (sfp_type) {
	case MODULE_TYPE_SFP:
		modinfo->type = ETH_MODULE_SFF_8472;
		modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		break;
	case MODULE_TYPE_QSFP:
		modinfo->type = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_MAX_LEN;
		break;
	case MODULE_TYPE_QSFP_PLUS:
		if (sfp_type_ext >= 0x3) {
			modinfo->type = ETH_MODULE_SFF_8636;
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_MAX_LEN;

		} else {
			modinfo->type = ETH_MODULE_SFF_8436;
			modinfo->eeprom_len = ETH_MODULE_SFF_8436_MAX_LEN;
		}
		break;
	case MODULE_TYPE_QSFP28:
		modinfo->type = ETH_MODULE_SFF_8636;
		modinfo->eeprom_len = ETH_MODULE_SFF_8636_MAX_LEN;
		break;
	default:
		nicif_warn(nic_dev, drv, netdev,
			   "Optical module unknown: 0x%x\n", sfp_type);
		return -EINVAL;
	}

	return 0;
}

static int hinic_get_module_eeprom(struct net_device *netdev,
				   struct ethtool_eeprom *ee, u8 *data)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u8 sfp_data[STD_SFP_INFO_MAX_SIZE];
	u16 len;
	int err;

	if (!ee->len || ((ee->len + ee->offset) > STD_SFP_INFO_MAX_SIZE))
		return -EINVAL;

	memset(data, 0, ee->len);

	err = hinic_get_sfp_eeprom(nic_dev->hwdev, sfp_data, &len);
	if (err)
		return err;

	memcpy(data, sfp_data + ee->offset, ee->len);

	return 0;
}
#endif /* ETHTOOL_GMODULEEEPROM */

static int set_l4_rss_hash_ops(struct ethtool_rxnfc *cmd,
			       struct nic_rss_type *rss_type)
{
	u8 rss_l4_en = 0;

	switch (cmd->data & (RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
	case 0:
		rss_l4_en = 0;
		break;
	case (RXH_L4_B_0_1 | RXH_L4_B_2_3):
		rss_l4_en = 1;
		break;
	default:
		return -EINVAL;
	}

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		rss_type->tcp_ipv4 = rss_l4_en;
		break;
	case TCP_V6_FLOW:
		rss_type->tcp_ipv6 = rss_l4_en;
		break;
	case UDP_V4_FLOW:
		rss_type->udp_ipv4 = rss_l4_en;
		break;
	case UDP_V6_FLOW:
		rss_type->udp_ipv6 = rss_l4_en;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int hinic_set_rss_hash_opts(struct hinic_nic_dev *nic_dev,
				   struct ethtool_rxnfc *cmd)
{
	struct nic_rss_type *rss_type = &nic_dev->rss_type;
	int err;

	if (!test_bit(HINIC_RSS_ENABLE, &nic_dev->flags)) {
		cmd->data = 0;
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "RSS is disable, not support to set flow-hash\n");
		return -EOPNOTSUPP;
	}

	/* RSS does not support anything other than hashing
	 * to queues on src and dst IPs and ports
	 */
	if (cmd->data & ~(RXH_IP_SRC | RXH_IP_DST | RXH_L4_B_0_1 |
	    RXH_L4_B_2_3))
		return -EINVAL;

	/* We need at least the IP SRC and DEST fields for hashing */
	if (!(cmd->data & RXH_IP_SRC) || !(cmd->data & RXH_IP_DST))
		return -EINVAL;

	err = hinic_get_rss_type(nic_dev->hwdev,
				 nic_dev->rss_tmpl_idx, rss_type);
	if (err) {
		nicif_err(nic_dev, drv, nic_dev->netdev, "Failed to get rss type\n");
		return -EFAULT;
	}

	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
	case UDP_V4_FLOW:
	case UDP_V6_FLOW:
		err = set_l4_rss_hash_ops(cmd, rss_type);
		if (err)
			return err;

		break;
	case IPV4_FLOW:
		rss_type->ipv4 = 1;
		break;
	case IPV6_FLOW:
		rss_type->ipv6 = 1;
		break;
	default:
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unsupported flow type\n");
		return -EINVAL;
	}

	err = hinic_set_rss_type(nic_dev->hwdev, nic_dev->rss_tmpl_idx,
				 *rss_type);
	if (err) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Failed to set rss type\n");
		return -EFAULT;
	}

	nicif_info(nic_dev, drv, nic_dev->netdev, "Set rss hash options success\n");

	return 0;
}

static int hinic_get_rss_hash_opts(struct hinic_nic_dev *nic_dev,
				   struct ethtool_rxnfc *cmd)
{
	struct nic_rss_type rss_type = {0};
	int err;

	cmd->data = 0;

	if (!test_bit(HINIC_RSS_ENABLE, &nic_dev->flags))
		return 0;

	err = hinic_get_rss_type(nic_dev->hwdev, nic_dev->rss_tmpl_idx,
				 &rss_type);
	if (err) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Failed to get rss type\n");
		return err;
	}

	cmd->data = RXH_IP_SRC | RXH_IP_DST;
	switch (cmd->flow_type) {
	case TCP_V4_FLOW:
		if (rss_type.tcp_ipv4)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case TCP_V6_FLOW:
		if (rss_type.tcp_ipv6)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case UDP_V4_FLOW:
		if (rss_type.udp_ipv4)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case UDP_V6_FLOW:
		if (rss_type.udp_ipv6)
			cmd->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
		break;
	case IPV4_FLOW:
	case IPV6_FLOW:
		break;
	default:
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Unsupported flow type\n");
		cmd->data = 0;
		return -EINVAL;
	}

	return 0;
}

#if (KERNEL_VERSION(3, 4, 24) > LINUX_VERSION_CODE)
	static int hinic_get_rxnfc(struct net_device *netdev,
				   struct ethtool_rxnfc *cmd, void *rule_locs)
#else
	static int hinic_get_rxnfc(struct net_device *netdev,
				   struct ethtool_rxnfc *cmd, u32 *rule_locs)
#endif
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	int err = 0;

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = nic_dev->num_qps;
		break;
	case ETHTOOL_GRXFH:
		err = hinic_get_rss_hash_opts(nic_dev, cmd);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int hinic_set_rxnfc(struct net_device *netdev, struct ethtool_rxnfc *cmd)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	int err = 0;

	switch (cmd->cmd) {
	case ETHTOOL_SRXFH:
		err = hinic_set_rss_hash_opts(nic_dev, cmd);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

#ifndef NOT_HAVE_GET_RXFH_INDIR_SIZE
static u32 hinic_get_rxfh_indir_size(struct net_device *netdev)
{
	return HINIC_RSS_INDIR_SIZE;
}
#endif

static int __set_rss_rxfh(struct net_device *netdev,
			  const u32 *indir, const u8 *key)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	int err, i;

	if (indir) {
		if (!nic_dev->rss_indir_user) {
			nic_dev->rss_indir_user =
				kzalloc(sizeof(u32) * HINIC_RSS_INDIR_SIZE,
					GFP_KERNEL);
			if (!nic_dev->rss_indir_user) {
				nicif_err(nic_dev, drv, netdev,
					  "Failed to alloc memory for rss_indir_usr\n");
				return -ENOMEM;
			}
		}

		memcpy(nic_dev->rss_indir_user, indir,
		       sizeof(u32) * HINIC_RSS_INDIR_SIZE);

		err = hinic_rss_set_indir_tbl(nic_dev->hwdev,
					      nic_dev->rss_tmpl_idx, indir);
		if (err) {
			nicif_err(nic_dev, drv, netdev,
				  "Failed to set rss indir table\n");
			return -EFAULT;
		}

		nicif_info(nic_dev, drv, netdev, "Change rss indir success\n");
	}

	if (key) {
		if (!nic_dev->rss_hkey_user) {
			/* We request double spaces for the hash key,
			 * the second one holds the key of Big Edian
			 * format.
			 */
			nic_dev->rss_hkey_user =
				kzalloc(HINIC_RSS_KEY_SIZE * 2, GFP_KERNEL);

			if (!nic_dev->rss_hkey_user) {
				nicif_err(nic_dev, drv, netdev,
					  "Failed to alloc memory for rss_hkey_user\n");
				return -ENOMEM;
			}

			/* The second space is for big edian hash key */
			nic_dev->rss_hkey_user_be =
				(u32 *)(nic_dev->rss_hkey_user +
				HINIC_RSS_KEY_SIZE);
		}

		memcpy(nic_dev->rss_hkey_user, key, HINIC_RSS_KEY_SIZE);

		/* make a copy of the key, and convert it to Big Endian */
		memcpy(nic_dev->rss_hkey_user_be, key, HINIC_RSS_KEY_SIZE);
		for (i = 0; i < HINIC_RSS_KEY_SIZE / 4; i++)
			nic_dev->rss_hkey_user_be[i] =
				cpu_to_be32(nic_dev->rss_hkey_user_be[i]);

		err = hinic_rss_set_template_tbl(nic_dev->hwdev,
						 nic_dev->rss_tmpl_idx, key);
		if (err) {
			nicif_err(nic_dev, drv, netdev, "Failed to set rss key\n");
			return -EFAULT;
		}

		nicif_info(nic_dev, drv, netdev, "Change rss key success\n");
	}

	return 0;
}

#if defined(ETHTOOL_GRSSH) && defined(ETHTOOL_SRSSH)
static u32 hinic_get_rxfh_key_size(struct net_device *netdev)
{
	return HINIC_RSS_KEY_SIZE;
}

#ifdef HAVE_RXFH_HASHFUNC
static int hinic_get_rxfh(struct net_device *netdev,
			  u32 *indir, u8 *key, u8 *hfunc)
#else
static int hinic_get_rxfh(struct net_device *netdev, u32 *indir, u8 *key)
#endif
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	int err = 0;

	if (!test_bit(HINIC_RSS_ENABLE, &nic_dev->flags))
		return -EOPNOTSUPP;

#ifdef HAVE_RXFH_HASHFUNC
	if (hfunc) {
		u8 hash_engine_type = 0;

		err = hinic_rss_get_hash_engine(nic_dev->hwdev,
						nic_dev->rss_tmpl_idx,
						&hash_engine_type);
		if (err)
			return -EFAULT;

		*hfunc = hash_engine_type ? ETH_RSS_HASH_TOP : ETH_RSS_HASH_XOR;
	}
#endif

	if (indir) {
		err = hinic_rss_get_indir_tbl(nic_dev->hwdev,
					      nic_dev->rss_tmpl_idx, indir);
		if (err)
			return -EFAULT;
	}

	if (key)
		err = hinic_rss_get_template_tbl(nic_dev->hwdev,
						 nic_dev->rss_tmpl_idx, key);

	return err;
}

#ifdef HAVE_RXFH_HASHFUNC
static int hinic_set_rxfh(struct net_device *netdev, const u32 *indir,
			  const u8 *key, const u8 hfunc)
#else
#ifdef HAVE_RXFH_NONCONST
static int hinic_set_rxfh(struct net_device *netdev, u32 *indir, u8 *key)
#else
static int hinic_set_rxfh(struct net_device *netdev, const u32 *indir,
			  const u8 *key)
#endif
#endif /* HAVE_RXFH_HASHFUNC */
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	int err = 0;

	if (!test_bit(HINIC_RSS_ENABLE, &nic_dev->flags)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Not support to set rss parameters when rss is disable\n");
		return -EOPNOTSUPP;
	}

	if (test_bit(HINIC_DCB_ENABLE, &nic_dev->flags) && indir) {
		nicif_err(nic_dev, drv, netdev,
			  "Not support to set indir when DCB is enabled\n");
		return -EOPNOTSUPP;
	}

#ifdef HAVE_RXFH_HASHFUNC
	if (hfunc != ETH_RSS_HASH_NO_CHANGE) {
		if (hfunc != ETH_RSS_HASH_TOP && hfunc != ETH_RSS_HASH_XOR) {
			nicif_err(nic_dev, drv, netdev,
				  "Not support to set hfunc type except TOP and XOR\n");
			return -EOPNOTSUPP;
		}

		nic_dev->rss_hash_engine = (hfunc == ETH_RSS_HASH_XOR) ?
			HINIC_RSS_HASH_ENGINE_TYPE_XOR :
			HINIC_RSS_HASH_ENGINE_TYPE_TOEP;
		err = hinic_rss_set_hash_engine
			(nic_dev->hwdev, nic_dev->rss_tmpl_idx,
			nic_dev->rss_hash_engine);
		if (err)
			return -EFAULT;

		nicif_info(nic_dev, drv, netdev,
			   "Change hfunc to RSS_HASH_%s success\n",
			   (hfunc == ETH_RSS_HASH_XOR) ? "XOR" : "TOP");
	}
#endif

	err = __set_rss_rxfh(netdev, indir, key);

	return err;
}

#else /* !(defined(ETHTOOL_GRSSH) && defined(ETHTOOL_SRSSH)) */

#ifdef NOT_HAVE_GET_RXFH_INDIR_SIZE
static int hinic_get_rxfh_indir(struct net_device *netdev,
				struct ethtool_rxfh_indir *indir1)
#else
static int hinic_get_rxfh_indir(struct net_device *netdev, u32 *indir)
#endif
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	int err = 0;
#ifdef NOT_HAVE_GET_RXFH_INDIR_SIZE
	u32 *indir;

	/* In a low version kernel(eg:suse 11.2), call the interface twice.
	 * First call to get the size value,
	 * and second call to get the rxfh indir according to the size value.
	 */
	if (indir1->size == 0) {
		indir1->size = HINIC_RSS_INDIR_SIZE;
		return 0;
	}

	if (indir1->size < HINIC_RSS_INDIR_SIZE) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Failed to get rss indir, hinic rss size(%d) is more than system rss size(%u).\n",
			  HINIC_RSS_INDIR_SIZE, indir1->size);
		return -EINVAL;
	}

	indir = indir1->ring_index;
#endif
	if (!test_bit(HINIC_RSS_ENABLE, &nic_dev->flags))
		return -EOPNOTSUPP;

	if (indir)
		err = hinic_rss_get_indir_tbl(nic_dev->hwdev,
					      nic_dev->rss_tmpl_idx, indir);

	return err;
}

#ifdef NOT_HAVE_GET_RXFH_INDIR_SIZE
static int hinic_set_rxfh_indir(struct net_device *netdev,
				const struct ethtool_rxfh_indir *indir1)
#else
static int hinic_set_rxfh_indir(struct net_device *netdev, const u32 *indir)
#endif
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
#ifdef NOT_HAVE_GET_RXFH_INDIR_SIZE
	const u32 *indir;

	if (indir1->size != HINIC_RSS_INDIR_SIZE) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Failed to set rss indir, hinic rss size(%d) is more than system rss size(%u).\n",
			  HINIC_RSS_INDIR_SIZE, indir1->size);
		return -EINVAL;
	}

	indir = indir1->ring_index;
#endif

	if (!test_bit(HINIC_RSS_ENABLE, &nic_dev->flags)) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Not support to set rss indir when rss is disable\n");
		return -EOPNOTSUPP;
	}

	if (test_bit(HINIC_DCB_ENABLE, &nic_dev->flags) && indir) {
		nicif_err(nic_dev, drv, netdev,
			  "Not support to set indir when DCB is enabled\n");
		return -EOPNOTSUPP;
	}

	return __set_rss_rxfh(netdev, indir, NULL);
}
#endif /* defined(ETHTOOL_GRSSH) && defined(ETHTOOL_SRSSH) */

static const struct ethtool_ops hinic_ethtool_ops = {
#ifdef ETHTOOL_GLINKSETTINGS
	.get_link_ksettings = hinic_get_link_ksettings,
	.set_link_ksettings = hinic_set_link_ksettings,
#endif
#ifndef HAVE_NEW_ETHTOOL_LINK_SETTINGS_ONLY
	.get_settings = hinic_get_settings,
	.set_settings = hinic_set_settings,
#endif
        .get_drvinfo = hinic_get_drvinfo,
	.get_msglevel = hinic_get_msglevel,
	.set_msglevel = hinic_set_msglevel,
	.nway_reset = hinic_nway_reset,
	.get_link = ethtool_op_get_link,
	.get_ringparam = hinic_get_ringparam,
	.set_ringparam = hinic_set_ringparam,
	.get_pauseparam = hinic_get_pauseparam,
	.set_pauseparam = hinic_set_pauseparam,
	.get_sset_count = hinic_get_sset_count,
	.get_coalesce = hinic_get_coalesce,
	.set_coalesce = hinic_set_coalesce,
#if defined(ETHTOOL_PERQUEUE) && defined(ETHTOOL_GCOALESCE)
	.get_per_queue_coalesce = hinic_get_per_queue_coalesce,
	.set_per_queue_coalesce = hinic_set_per_queue_coalesce,
#endif
	.get_ethtool_stats = hinic_get_ethtool_stats,
	.get_strings = hinic_get_strings,
#ifndef HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT
#ifdef HAVE_ETHTOOL_SET_PHYS_ID
	.set_phys_id = hinic_set_phys_id,
#else
	.phys_id = hinic_phys_id,
#endif
#endif
	.self_test = hinic_diag_test,
	.get_rxnfc = hinic_get_rxnfc,
	.set_rxnfc = hinic_set_rxnfc,

#ifndef HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT
	.get_channels = hinic_get_channels,
	.set_channels = hinic_set_channels,
#ifdef ETHTOOL_GMODULEEEPROM
	.get_module_info = hinic_get_module_info,
	.get_module_eeprom = hinic_get_module_eeprom,
#endif
#ifndef NOT_HAVE_GET_RXFH_INDIR_SIZE
	.get_rxfh_indir_size = hinic_get_rxfh_indir_size,
#endif
#if defined(ETHTOOL_GRSSH) && defined(ETHTOOL_SRSSH)
	.get_rxfh_key_size = hinic_get_rxfh_key_size,
	.get_rxfh = hinic_get_rxfh,
	.set_rxfh = hinic_set_rxfh,
#else
	.get_rxfh_indir = hinic_get_rxfh_indir,
	.set_rxfh_indir = hinic_set_rxfh_indir,
#endif
#endif /* HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT */
};

#ifdef HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT
static const struct ethtool_ops_ext hinic_ethtool_ops_ext = {
	.size	= sizeof(struct ethtool_ops_ext),
	.set_phys_id = hinic_set_phys_id,
	.get_channels = hinic_get_channels,
	.set_channels = hinic_set_channels,
#ifdef ETHTOOL_GMODULEEEPROM
	.get_module_info = hinic_get_module_info,
	.get_module_eeprom = hinic_get_module_eeprom,
#endif

#ifndef NOT_HAVE_GET_RXFH_INDIR_SIZE
	.get_rxfh_indir_size = hinic_get_rxfh_indir_size,
#endif
#if defined(ETHTOOL_GRSSH) && defined(ETHTOOL_SRSSH)
	.get_rxfh_key_size = hinic_get_rxfh_key_size,
	.get_rxfh = hinic_get_rxfh,
	.set_rxfh = hinic_set_rxfh,
#else
	.get_rxfh_indir = hinic_get_rxfh_indir,
	.set_rxfh_indir = hinic_set_rxfh_indir,
#endif
};
#endif /* HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT */

static const struct ethtool_ops hinicvf_ethtool_ops = {
#ifdef ETHTOOL_GLINKSETTINGS
	.get_link_ksettings = hinic_get_link_ksettings,
#else
	.get_settings = hinic_get_settings,
#endif
	.get_drvinfo = hinic_get_drvinfo,
	.get_msglevel = hinic_get_msglevel,
	.set_msglevel = hinic_set_msglevel,
	.get_link = ethtool_op_get_link,
	.get_ringparam = hinic_get_ringparam,
	.set_ringparam = hinic_set_ringparam,
	.get_sset_count = hinic_get_sset_count,
	.get_coalesce = hinic_get_coalesce,
	.set_coalesce = hinic_set_coalesce,
#if defined(ETHTOOL_PERQUEUE) && defined(ETHTOOL_GCOALESCE)
	.get_per_queue_coalesce = hinic_get_per_queue_coalesce,
	.set_per_queue_coalesce = hinic_set_per_queue_coalesce,
#endif
	.get_ethtool_stats = hinic_get_ethtool_stats,
	.get_strings = hinic_get_strings,
	.get_rxnfc = hinic_get_rxnfc,
	.set_rxnfc = hinic_set_rxnfc,

#ifndef HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT
	.get_channels = hinic_get_channels,
	.set_channels = hinic_set_channels,
#ifndef NOT_HAVE_GET_RXFH_INDIR_SIZE
	.get_rxfh_indir_size = hinic_get_rxfh_indir_size,
#endif
#if defined(ETHTOOL_GRSSH) && defined(ETHTOOL_SRSSH)
	.get_rxfh_key_size = hinic_get_rxfh_key_size,
	.get_rxfh = hinic_get_rxfh,
	.set_rxfh = hinic_set_rxfh,
#else
	.get_rxfh_indir = hinic_get_rxfh_indir,
	.set_rxfh_indir = hinic_set_rxfh_indir,
#endif
#endif /* HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT */
};

#ifdef HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT
static const struct ethtool_ops_ext hinicvf_ethtool_ops_ext = {
	.size	= sizeof(struct ethtool_ops_ext),
	.get_channels = hinic_get_channels,
	.set_channels = hinic_set_channels,
#ifndef NOT_HAVE_GET_RXFH_INDIR_SIZE
	.get_rxfh_indir_size = hinic_get_rxfh_indir_size,
#endif
#if defined(ETHTOOL_GRSSH) && defined(ETHTOOL_SRSSH)
	.get_rxfh_key_size = hinic_get_rxfh_key_size,
	.get_rxfh = hinic_get_rxfh,
	.set_rxfh = hinic_set_rxfh,
#else
	.get_rxfh_indir = hinic_get_rxfh_indir,
	.set_rxfh_indir = hinic_set_rxfh_indir,
#endif
};
#endif /* HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT */

void hinic_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &hinic_ethtool_ops);
#ifdef HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT
	set_ethtool_ops_ext(netdev, &hinic_ethtool_ops_ext);
#endif /* HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT */
}

void hinicvf_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &hinicvf_ethtool_ops);
#ifdef HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT
	set_ethtool_ops_ext(netdev, &hinicvf_ethtool_ops_ext);
#endif /* HAVE_RHEL6_ETHTOOL_OPS_EXT_STRUCT */
}
