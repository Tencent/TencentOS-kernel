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

#ifndef HINIC_TX_H
#define HINIC_TX_H

enum tx_offload_type {
	TX_OFFLOAD_TSO = BIT(0),
	TX_OFFLOAD_CSUM = BIT(1),
	TX_OFFLOAD_VLAN = BIT(2),
	TX_OFFLOAD_INVALID = BIT(3),
};

struct hinic_txq_stats {
	u64	packets;
	u64	bytes;
	u64	busy;
	u64	wake;
	u64	dropped;
	u64	big_frags_pkts;
	u64	big_udp_pkts;

	/* Subdivision statistics show in private tool */
	u64	ufo_pkt_unsupport;
	u64	ufo_linearize_err;
	u64	ufo_alloc_skb_err;
	u64	skb_pad_err;
	u64	frag_len_overflow;
	u64	offload_cow_skb_err;
	u64	alloc_cpy_frag_err;
	u64	map_cpy_frag_err;
	u64	map_frag_err;
	u64	frag_size_err;

#ifdef HAVE_NDO_GET_STATS64
	struct u64_stats_sync	syncp;
#else
	struct u64_stats_sync_empty syncp;
#endif
};

struct hinic_dma_len {
	dma_addr_t dma;
	u32 len;
};

#define MAX_SGE_NUM_PER_WQE	17

struct hinic_tx_info {
	struct sk_buff		*skb;

	int			wqebb_cnt;

	int			num_sge;
	void			*wqe;
	u8			*cpy_buff;
	u16			valid_nr_frags;
	u16			num_pkts;
	u64			num_bytes;
	struct hinic_dma_len	dma_len[MAX_SGE_NUM_PER_WQE];
};

struct hinic_txq {
	struct net_device	*netdev;

	u16			q_id;
	u16			q_depth;
	u16			q_mask;
	struct hinic_txq_stats	txq_stats;
	u64			last_moder_packets;
	u64			last_moder_bytes;
	struct hinic_tx_info	*tx_info;
};

enum hinic_tx_xmit_status {
	HINIC_TX_OK = 0,
	HINIC_TX_DROPED = 1,
	HINIC_TX_BUSY =	2,
};

enum hinic_tx_avd_type {
	HINIC_TX_NON_AVD = 0,
	HINIC_TX_UFO_AVD = 1,
};

void hinic_txq_clean_stats(struct hinic_txq_stats *txq_stats);

void hinic_txq_get_stats(struct hinic_txq *txq,
			 struct hinic_txq_stats *stats);

netdev_tx_t hinic_lb_xmit_frame(struct sk_buff *skb,
				struct net_device *netdev);

netdev_tx_t hinic_xmit_frame(struct sk_buff *skb, struct net_device *netdev);

int hinic_setup_all_tx_resources(struct net_device *netdev);

void hinic_free_all_tx_resources(struct net_device *netdev);

void hinic_set_sq_default_cos(struct net_device *netdev, u8 cos_id);

int hinic_sq_cos_mapping(struct net_device *netdev);

int hinic_alloc_txqs(struct net_device *netdev);

void hinic_free_txqs(struct net_device *netdev);

int hinic_tx_poll(struct hinic_txq *txq, int budget);

u8 hinic_get_vlan_pri(struct sk_buff *skb);

void hinic_flush_txqs(struct net_device *netdev);

#endif
