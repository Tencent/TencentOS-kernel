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

#ifndef HINIC_RX_H
#define HINIC_RX_H

/* rx cqe checksum err */
#define HINIC_RX_CSUM_IP_CSUM_ERR	BIT(0)
#define HINIC_RX_CSUM_TCP_CSUM_ERR	BIT(1)
#define HINIC_RX_CSUM_UDP_CSUM_ERR	BIT(2)
#define HINIC_RX_CSUM_IGMP_CSUM_ERR	BIT(3)
#define HINIC_RX_CSUM_ICMPv4_CSUM_ERR	BIT(4)
#define HINIC_RX_CSUM_ICMPv6_CSUM_ERR	BIT(5)
#define HINIC_RX_CSUM_SCTP_CRC_ERR	BIT(6)
#define HINIC_RX_CSUM_HW_CHECK_NONE	BIT(7)
#define HINIC_RX_CSUM_IPSU_OTHER_ERR	BIT(8)

#define HINIC_RX_CSUM_OFFLOAD_EN	0xFFF

#define HINIC_RX_BP_LOWER_THD		200
#define HINIC_RX_BP_UPPER_THD		400

#define HINIC_SUPPORT_LRO_ADAP_QPS_MAX	16
#define HINIC_RX_BUFFER_WRITE			16

enum {
	HINIC_RX_STATUS_BP_EN,
};

struct hinic_rxq_stats {
	u64	packets;
	u64	bytes;
	u64	errors;
	u64	csum_errors;
	u64	other_errors;
	u64	unlock_bp;
	u64	dropped;

	u64	alloc_skb_err;
#ifdef HAVE_NDO_GET_STATS64
	struct u64_stats_sync		syncp;
#else
	struct u64_stats_sync_empty	syncp;
#endif
};

struct hinic_rx_info {
	dma_addr_t		buf_dma_addr;

	struct hinic_rq_cqe	*cqe;
	dma_addr_t		cqe_dma;
	struct page		*page;
	u32			page_offset;
	struct hinic_rq_wqe	*rq_wqe;
};


struct hinic_rxq {
	struct net_device	*netdev;

	u16			q_id;
	u16			q_depth;
	u16			q_mask;

	u16			buf_len;
	u32			rx_buff_shift;
	u32			dma_rx_buff_size;

	struct hinic_rxq_stats rxq_stats;
	int			cons_idx;
	int			delta;

	u32			irq_id;
	u16			msix_entry_idx;

	struct hinic_rx_info	*rx_info;

	struct hinic_irq	*irq_cfg;
	u16			next_to_alloc;
	u16			next_to_update;
	struct device		*dev;		/* device for DMA mapping */

	u32			bp_cnt;
	unsigned long		status;
	dma_addr_t		cqe_start_paddr;
	void			*cqe_start_vaddr;
	u64			last_moder_packets;
	u64			last_moder_bytes;
	u8			last_coalesc_timer_cfg;
	u8			last_pending_limt;

};

void hinic_rxq_clean_stats(struct hinic_rxq_stats *rxq_stats);

void hinic_rxq_get_stats(struct hinic_rxq *rxq,
			 struct hinic_rxq_stats *stats);

int hinic_alloc_rxqs(struct net_device *netdev);

void hinic_free_rxqs(struct net_device *netdev);

void hinic_init_rss_parameters(struct net_device *netdev);

void hinic_set_default_rss_indir(struct net_device *netdev);

int hinic_setup_all_rx_resources(struct net_device *netdev,
				 struct irq_info *msix_entires);

void hinic_free_all_rx_resources(struct net_device *netdev);

void hinic_rx_remove_configure(struct net_device *netdev);

int hinic_rx_configure(struct net_device *netdev);

int hinic_set_hw_rss_parameters(struct net_device *netdev, u8 rss_en, u8 num_tc,
				u8 *prio_tc);

int hinic_update_hw_tc_map(struct net_device *netdev, u8 num_tc, u8 *prio_tc);

int hinic_rx_poll(struct hinic_rxq *rxq, int budget);


#endif
