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

#include <linux/netdevice.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/sctp.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include <net/checksum.h>
#include <net/ip6_checksum.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>
#include <linux/u64_stats_sync.h>

#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_hw_mgmt.h"
#include "hinic_nic_io.h"
#include "hinic_nic_dev.h"
#include "hinic_qp.h"
#include "hinic_tx.h"
#include "hinic_dbg.h"

#define MIN_SKB_LEN        32
#define MAX_PAYLOAD_OFFSET 221

#define NIC_QID(q_id, nic_dev)	((q_id) & ((nic_dev)->num_qps - 1))

#define TXQ_STATS_INC(txq, field)			\
{							\
	u64_stats_update_begin(&txq->txq_stats.syncp);	\
	txq->txq_stats.field++;				\
	u64_stats_update_end(&txq->txq_stats.syncp);	\
}

void hinic_txq_get_stats(struct hinic_txq *txq,
			 struct hinic_txq_stats *stats)
{
	struct hinic_txq_stats *txq_stats = &txq->txq_stats;
	unsigned int start;

	u64_stats_update_begin(&stats->syncp);
	do {
		start = u64_stats_fetch_begin(&txq_stats->syncp);
		stats->bytes = txq_stats->bytes;
		stats->packets = txq_stats->packets;
		stats->busy = txq_stats->busy;
		stats->wake = txq_stats->wake;
		stats->dropped = txq_stats->dropped;
		stats->big_frags_pkts = txq_stats->big_frags_pkts;
		stats->big_udp_pkts = txq_stats->big_udp_pkts;
	} while (u64_stats_fetch_retry(&txq_stats->syncp, start));
	u64_stats_update_end(&stats->syncp);
}

void hinic_txq_clean_stats(struct hinic_txq_stats *txq_stats)
{
	u64_stats_update_begin(&txq_stats->syncp);
	txq_stats->bytes = 0;
	txq_stats->packets = 0;
	txq_stats->busy = 0;
	txq_stats->wake = 0;
	txq_stats->dropped = 0;
	txq_stats->big_frags_pkts = 0;
	txq_stats->big_udp_pkts = 0;

	txq_stats->ufo_pkt_unsupport = 0;
	txq_stats->ufo_linearize_err = 0;
	txq_stats->ufo_alloc_skb_err = 0;
	txq_stats->skb_pad_err = 0;
	txq_stats->frag_len_overflow = 0;
	txq_stats->offload_cow_skb_err = 0;
	txq_stats->alloc_cpy_frag_err = 0;
	txq_stats->map_cpy_frag_err = 0;
	txq_stats->map_frag_err = 0;
	txq_stats->frag_size_err = 0;
	u64_stats_update_end(&txq_stats->syncp);
}

static void txq_stats_init(struct hinic_txq *txq)
{
	struct hinic_txq_stats *txq_stats = &txq->txq_stats;

	u64_stats_init(&txq_stats->syncp);
	hinic_txq_clean_stats(txq_stats);
}

inline void hinic_set_buf_desc(struct hinic_sq_bufdesc *buf_descs,
			      dma_addr_t addr, u32 len)
{
	buf_descs->hi_addr = cpu_to_be32(upper_32_bits(addr));
	buf_descs->lo_addr = cpu_to_be32(lower_32_bits(addr));
	buf_descs->len  = cpu_to_be32(len);
}

static int tx_map_skb(struct hinic_nic_dev *nic_dev, struct sk_buff *skb,
		      struct hinic_txq *txq, struct hinic_tx_info *tx_info,
		      struct hinic_sq_bufdesc *buf_descs, u16 skb_nr_frags)
{
	struct pci_dev *pdev = nic_dev->pdev;
	struct hinic_dma_len *dma_len = tx_info->dma_len;
	skb_frag_t *frag = NULL;
	u16 base_nr_frags;
	int j, i = 0;
	int node, err = 0;
	u32 nsize, cpy_nsize = 0;
	u8 *vaddr, *cpy_buff = NULL;

	if (unlikely(skb_nr_frags > HINIC_MAX_SKB_NR_FRAGE)) {
		for (i = HINIC_MAX_SKB_NR_FRAGE; i <= skb_nr_frags; i++)
			cpy_nsize +=
				skb_frag_size(&skb_shinfo(skb)->frags[i - 1]);
		if (!cpy_nsize) {
			TXQ_STATS_INC(txq, alloc_cpy_frag_err);
			return -EINVAL;
		}

		node = dev_to_node(&nic_dev->pdev->dev);
		if (node == NUMA_NO_NODE)
			cpy_buff = kzalloc(cpy_nsize,
					   GFP_ATOMIC | __GFP_NOWARN);
		else
			cpy_buff = kzalloc_node(cpy_nsize,
						GFP_ATOMIC | __GFP_NOWARN,
						node);

		if (!cpy_buff) {
			TXQ_STATS_INC(txq, alloc_cpy_frag_err);
			return -ENOMEM;
		}

		tx_info->cpy_buff = cpy_buff;

		for (i = HINIC_MAX_SKB_NR_FRAGE; i <= skb_nr_frags; i++) {
			frag = &skb_shinfo(skb)->frags[i - 1];
			nsize = skb_frag_size(frag);

			vaddr = _kc_kmap_atomic(skb_frag_page(frag));
			memcpy(cpy_buff, vaddr + frag->bv_offset, nsize);
			_kc_kunmap_atomic(vaddr);
			cpy_buff += nsize;
		}
	}

	dma_len[0].dma = dma_map_single(&pdev->dev, skb->data,
					skb_headlen(skb), DMA_TO_DEVICE);
	if (dma_mapping_error(&pdev->dev, dma_len[0].dma)) {
		TXQ_STATS_INC(txq, map_frag_err);
		err = -EFAULT;
		goto map_single_err;
	}
	dma_len[0].len = skb_headlen(skb);
	hinic_set_buf_desc(&buf_descs[0], dma_len[0].dma,
			   dma_len[0].len);

	if (skb_nr_frags > HINIC_MAX_SKB_NR_FRAGE)
		base_nr_frags = HINIC_MAX_SKB_NR_FRAGE - 1;
	else
		base_nr_frags = skb_nr_frags;

	for (i = 0; i < base_nr_frags; ) {
		frag = &(skb_shinfo(skb)->frags[i]);
		nsize = skb_frag_size(frag);
		i++;
		dma_len[i].dma = skb_frag_dma_map(&pdev->dev, frag, 0,
						  nsize, DMA_TO_DEVICE);
		if (dma_mapping_error(&pdev->dev, dma_len[i].dma)) {
			TXQ_STATS_INC(txq, map_frag_err);
			i--;
			err = -EFAULT;
			goto frag_map_err;
		}
		dma_len[i].len = nsize;

		hinic_set_buf_desc(&buf_descs[i], dma_len[i].dma,
				   dma_len[i].len);
	}

	if (skb_nr_frags > HINIC_MAX_SKB_NR_FRAGE) {
		dma_len[HINIC_MAX_SKB_NR_FRAGE].dma =
				dma_map_single(&pdev->dev, tx_info->cpy_buff,
					       cpy_nsize, DMA_TO_DEVICE);
		if (dma_mapping_error(&pdev->dev,
				      dma_len[HINIC_MAX_SKB_NR_FRAGE].dma)) {
			TXQ_STATS_INC(txq, map_cpy_frag_err);
			err = -EFAULT;
			goto fusion_map_err;
		}

		dma_len[HINIC_MAX_SKB_NR_FRAGE].len = cpy_nsize;
		hinic_set_buf_desc(&buf_descs[HINIC_MAX_SKB_NR_FRAGE],
				   dma_len[HINIC_MAX_SKB_NR_FRAGE].dma,
				   dma_len[HINIC_MAX_SKB_NR_FRAGE].len);
	}

	return 0;

fusion_map_err:
frag_map_err:
	for (j = 0; j < i;) {
		j++;
		dma_unmap_page(&pdev->dev, dma_len[j].dma,
			       dma_len[j].len, DMA_TO_DEVICE);
	}
	dma_unmap_single(&pdev->dev, dma_len[0].dma, dma_len[0].len,
			 DMA_TO_DEVICE);

map_single_err:
	kfree(tx_info->cpy_buff);
	tx_info->cpy_buff = NULL;

	return err;
}

static inline void tx_unmap_skb(struct hinic_nic_dev *nic_dev,
				struct sk_buff *skb,
				struct hinic_dma_len *dma_len,
				u16 valid_nr_frags)
{
	struct pci_dev *pdev = nic_dev->pdev;
	int i;
	u16 nr_frags = valid_nr_frags;

	if (nr_frags > HINIC_MAX_SKB_NR_FRAGE)
		nr_frags = HINIC_MAX_SKB_NR_FRAGE;

	for (i = 0; i < nr_frags; ) {
		i++;
		dma_unmap_page(&pdev->dev,
			       dma_len[i].dma,
			       dma_len[i].len, DMA_TO_DEVICE);
	}

	dma_unmap_single(&pdev->dev, dma_len[0].dma,
			 dma_len[0].len, DMA_TO_DEVICE);
}

union hinic_ip {
	struct iphdr *v4;
	struct ipv6hdr *v6;
	unsigned char *hdr;
};

union hinic_l4 {
	struct tcphdr *tcp;
	struct udphdr *udp;
	unsigned char *hdr;
};

#define TRANSPORT_OFFSET(l4_hdr, skb)	((u32)((l4_hdr) - (skb)->data))

static void get_inner_l3_l4_type(struct sk_buff *skb, union hinic_ip *ip,
				 union hinic_l4 *l4,
				 enum tx_offload_type offload_type,
				 enum sq_l3_type *l3_type, u8 *l4_proto)
{
	unsigned char *exthdr;

	if (ip->v4->version == 4) {
		*l3_type = (offload_type == TX_OFFLOAD_CSUM) ?
		  IPV4_PKT_NO_CHKSUM_OFFLOAD : IPV4_PKT_WITH_CHKSUM_OFFLOAD;
		*l4_proto = ip->v4->protocol;

#ifdef HAVE_OUTER_IPV6_TUNNEL_OFFLOAD
		/* inner_transport_header is wrong in centos7.0 and suse12.1 */
		l4->hdr = ip->hdr + ((u8)ip->v4->ihl << 2);
#endif
	} else if (ip->v4->version == 6) {
		*l3_type = IPV6_PKT;
		exthdr = ip->hdr + sizeof(*ip->v6);
		*l4_proto = ip->v6->nexthdr;
		if (exthdr != l4->hdr) {
			__be16 frag_off = 0;
#ifndef HAVE_OUTER_IPV6_TUNNEL_OFFLOAD
			ipv6_skip_exthdr(skb, (int)(exthdr - skb->data),
					 l4_proto, &frag_off);
#else
			int pld_off = 0;

			pld_off = ipv6_skip_exthdr(skb,
						   (int)(exthdr - skb->data),
						   l4_proto, &frag_off);
			l4->hdr = skb->data + pld_off;
		} else {
			l4->hdr = exthdr;
#endif
		}
	} else {
		*l3_type = UNKNOWN_L3TYPE;
		*l4_proto = 0;
	}
}

static void get_inner_l4_info(struct sk_buff *skb, union hinic_l4 *l4,
			      enum tx_offload_type offload_type, u8 l4_proto,
			      enum sq_l4offload_type *l4_offload,
			      u32 *l4_len, u32 *offset)
{
	*offset = 0;
	*l4_len = 0;
	*l4_offload = OFFLOAD_DISABLE;

	switch (l4_proto) {
	case IPPROTO_TCP:
		*l4_offload = TCP_OFFLOAD_ENABLE;
		*l4_len = l4->tcp->doff * 4; /* doff in unit of 4B */
		/* To keep same with TSO, payload offset begins from paylaod */
		*offset = *l4_len + TRANSPORT_OFFSET(l4->hdr, skb);
		break;

	case IPPROTO_UDP:
		*l4_offload = UDP_OFFLOAD_ENABLE;
		*l4_len = sizeof(struct udphdr);
		*offset = TRANSPORT_OFFSET(l4->hdr, skb);
		break;

	case IPPROTO_SCTP:
		/* only csum offload support sctp */
		if (offload_type != TX_OFFLOAD_CSUM)
			break;

		*l4_offload = SCTP_OFFLOAD_ENABLE;
		*l4_len = sizeof(struct sctphdr);
		/* To keep same with UFO, payload offset
		 * begins from L4 header
		 */
		*offset = TRANSPORT_OFFSET(l4->hdr, skb);
		break;

	default:
		break;
	}
}

static int hinic_tx_csum(struct hinic_sq_task *task, u32 *queue_info,
			 struct sk_buff *skb)
{
	union hinic_ip ip;
	union hinic_l4 l4;
	enum sq_l3_type l3_type;
	enum sq_l4offload_type l4_offload;
	u32 network_hdr_len;
	u32 offset, l4_len;
	u8 l4_proto;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

#if (KERNEL_VERSION(3, 8, 0) <= LINUX_VERSION_CODE)
	if (skb->encapsulation) {
		u32 l4_tunnel_len;
		u32 tunnel_type = TUNNEL_UDP_NO_CSUM;

		ip.hdr = skb_network_header(skb);

		if (ip.v4->version == 4) {
			l3_type = IPV4_PKT_NO_CHKSUM_OFFLOAD;
			l4_proto = ip.v4->protocol;
		} else if (ip.v4->version == 6) {
			unsigned char *exthdr;
			__be16 frag_off;

			l3_type = IPV6_PKT;
#ifdef HAVE_OUTER_IPV6_TUNNEL_OFFLOAD
			tunnel_type = TUNNEL_UDP_CSUM;
#endif
			exthdr = ip.hdr + sizeof(*ip.v6);
			l4_proto = ip.v6->nexthdr;
			l4.hdr = skb_transport_header(skb);
			if (l4.hdr != exthdr)
				ipv6_skip_exthdr(skb, exthdr - skb->data,
						 &l4_proto, &frag_off);
		} else {
			l3_type = UNKNOWN_L3TYPE;
			l4_proto = IPPROTO_RAW;
		}

		hinic_task_set_outter_l3(task, l3_type,
					 skb_network_header_len(skb));

		if (l4_proto == IPPROTO_UDP || l4_proto == IPPROTO_GRE) {
			l4_tunnel_len = skb_inner_network_offset(skb) -
					skb_transport_offset(skb);
			ip.hdr = skb_inner_network_header(skb);
			l4.hdr = skb_inner_transport_header(skb);
			network_hdr_len = skb_inner_network_header_len(skb);
		} else {
			tunnel_type = NOT_TUNNEL;
			l4_tunnel_len = 0;

			ip.hdr = skb_inner_network_header(skb);
			l4.hdr = skb_transport_header(skb);
			network_hdr_len = skb_network_header_len(skb);
		}

		hinic_task_set_tunnel_l4(task, tunnel_type, l4_tunnel_len);
	} else {
		ip.hdr = skb_network_header(skb);
		l4.hdr = skb_transport_header(skb);
		network_hdr_len = skb_network_header_len(skb);
	}
#else
	ip.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);
	network_hdr_len = skb_network_header_len(skb);
#endif
	get_inner_l3_l4_type(skb, &ip, &l4, TX_OFFLOAD_CSUM,
			     &l3_type, &l4_proto);

	get_inner_l4_info(skb, &l4, TX_OFFLOAD_CSUM, l4_proto,
			  &l4_offload, &l4_len, &offset);

#ifdef HAVE_OUTER_IPV6_TUNNEL_OFFLOAD
	/* get wrong network header length using skb_network_header_len */
	if (unlikely(l3_type == UNKNOWN_L3TYPE))
		network_hdr_len = 0;
	else
		network_hdr_len = l4.hdr - ip.hdr;

	/* payload offset must be setted */
	if (unlikely(!offset)) {
		if (l3_type == UNKNOWN_L3TYPE)
			offset = ip.hdr - skb->data;
		else if (l4_offload == OFFLOAD_DISABLE)
			offset = ip.hdr - skb->data + network_hdr_len;
	}
#endif

	hinic_task_set_inner_l3(task, l3_type, network_hdr_len);

	hinic_set_cs_inner_l4(task, queue_info, l4_offload, l4_len, offset);

	return 1;
}

static __sum16 csum_magic(union hinic_ip *ip, unsigned short proto)
{
	return (ip->v4->version == 4) ?
		csum_tcpudp_magic(ip->v4->saddr, ip->v4->daddr, 0, proto, 0) :
		csum_ipv6_magic(&ip->v6->saddr, &ip->v6->daddr, 0, proto, 0);
}

static int hinic_tso(struct hinic_sq_task *task, u32 *queue_info,
		     struct sk_buff *skb)
{
	union hinic_ip ip;
	union hinic_l4 l4;
	enum sq_l3_type l3_type;
	enum sq_l4offload_type l4_offload;
	u32 network_hdr_len;
	u32 offset, l4_len;
	u32 ip_identify = 0;
	u8 l4_proto;
	int err;

	if (!skb_is_gso(skb))
		return 0;

	err = skb_cow_head(skb, 0);
	if (err < 0)
		return err;

#if (KERNEL_VERSION(3, 8, 0) <= LINUX_VERSION_CODE)
	if (skb->encapsulation) {
		u32 l4_tunnel_len;
		u32 tunnel_type = 0;
		u32 gso_type = skb_shinfo(skb)->gso_type;

		ip.hdr = skb_network_header(skb);
		l4.hdr = skb_transport_header(skb);
		network_hdr_len = skb_inner_network_header_len(skb);

		if (ip.v4->version == 4)
			l3_type = IPV4_PKT_WITH_CHKSUM_OFFLOAD;
		else if (ip.v4->version == 6)
			l3_type = IPV6_PKT;
		else
			l3_type = 0;

		hinic_task_set_outter_l3(task, l3_type,
					 skb_network_header_len(skb));

		if (gso_type & SKB_GSO_UDP_TUNNEL_CSUM) {
			l4.udp->check = ~csum_magic(&ip, IPPROTO_UDP);
			tunnel_type = TUNNEL_UDP_CSUM;
		} else if (gso_type & SKB_GSO_UDP_TUNNEL) {
#ifdef HAVE_OUTER_IPV6_TUNNEL_OFFLOAD
			if (l3_type == IPV6_PKT) {
				tunnel_type = TUNNEL_UDP_CSUM;
				l4.udp->check = ~csum_magic(&ip, IPPROTO_UDP);
			} else {
				tunnel_type = TUNNEL_UDP_NO_CSUM;
			}
#else
			tunnel_type = TUNNEL_UDP_NO_CSUM;
#endif
		}

		l4_tunnel_len = skb_inner_network_offset(skb) -
				skb_transport_offset(skb);
		hinic_task_set_tunnel_l4(task, tunnel_type, l4_tunnel_len);

		ip.hdr = skb_inner_network_header(skb);
		l4.hdr = skb_inner_transport_header(skb);
	} else {
		ip.hdr = skb_network_header(skb);
		l4.hdr = skb_transport_header(skb);
		network_hdr_len = skb_network_header_len(skb);
	}
#else
	ip.hdr = skb_network_header(skb);
	l4.hdr = skb_transport_header(skb);
	network_hdr_len = skb_network_header_len(skb);
#endif

	get_inner_l3_l4_type(skb, &ip, &l4, TX_OFFLOAD_TSO,
			     &l3_type, &l4_proto);

	if (l4_proto == IPPROTO_TCP)
		l4.tcp->check = ~csum_magic(&ip, IPPROTO_TCP);
#ifdef HAVE_IP6_FRAG_ID_ENABLE_UFO
	else if (l4_proto == IPPROTO_UDP && ip.v4->version == 6)
		ip_identify = be32_to_cpu(skb_shinfo(skb)->ip6_frag_id);
	/* changed to big endiant is just to keep the same code style here */
#endif

	get_inner_l4_info(skb, &l4, TX_OFFLOAD_TSO, l4_proto,
			  &l4_offload, &l4_len, &offset);

#ifdef HAVE_OUTER_IPV6_TUNNEL_OFFLOAD
	if (unlikely(l3_type == UNKNOWN_L3TYPE))
		network_hdr_len = 0;
	else
		network_hdr_len = l4.hdr - ip.hdr;

	if (unlikely(!offset)) {
		if (l3_type == UNKNOWN_L3TYPE)
			offset = ip.hdr - skb->data;
		else if (l4_offload == OFFLOAD_DISABLE)
			offset = ip.hdr - skb->data + network_hdr_len;
	}
#endif

	hinic_task_set_inner_l3(task, l3_type, network_hdr_len);

	hinic_set_tso_inner_l4(task, queue_info, l4_offload, l4_len,
			       offset, ip_identify, skb_shinfo(skb)->gso_size);

	return 1;
}

static enum tx_offload_type hinic_tx_offload(struct sk_buff *skb,
					     struct hinic_sq_task *task,
					     u32 *queue_info, u8 avd_flag)
{
	enum tx_offload_type offload = 0;
	int tso_cs_en;
	u16 vlan_tag;

	task->pkt_info0 = 0;
	task->pkt_info1 = 0;
	task->pkt_info2 = 0;

	tso_cs_en = hinic_tso(task, queue_info, skb);
	if (tso_cs_en < 0) {
		offload = TX_OFFLOAD_INVALID;
		return offload;
	} else if (tso_cs_en) {
		offload |= TX_OFFLOAD_TSO;
	} else {
		tso_cs_en = hinic_tx_csum(task, queue_info, skb);
		if (tso_cs_en)
			offload |= TX_OFFLOAD_CSUM;
	}

	if (unlikely(skb_vlan_tag_present(skb))) {
		vlan_tag = skb_vlan_tag_get(skb);
		hinic_set_vlan_tx_offload(task, queue_info, vlan_tag,
					  vlan_tag >> VLAN_PRIO_SHIFT);
		offload |= TX_OFFLOAD_VLAN;
	}

	if (unlikely(SQ_CTRL_QUEUE_INFO_GET(*queue_info, PLDOFF) >
		     MAX_PAYLOAD_OFFSET)) {
		offload = TX_OFFLOAD_INVALID;
		return offload;
	}

	if (avd_flag == HINIC_TX_UFO_AVD)
		task->pkt_info0 |= SQ_TASK_INFO0_SET(1, UFO_AVD);

	if (offload) {
		hinic_task_set_tx_offload_valid(task, skb_network_offset(skb));
		task->pkt_info0 = be32_to_cpu(task->pkt_info0);
		task->pkt_info1 = be32_to_cpu(task->pkt_info1);
		task->pkt_info2 = be32_to_cpu(task->pkt_info2);
	}

	return offload;
}

static inline void __get_pkt_stats(struct hinic_tx_info *tx_info,
				   struct sk_buff *skb)
{
	u32 ihs, hdr_len;

	if (skb_is_gso(skb)) {
#if (KERNEL_VERSION(3, 8, 0) <= LINUX_VERSION_CODE)
#if (defined(HAVE_SKB_INNER_TRANSPORT_HEADER) && \
	defined(HAVE_SK_BUFF_ENCAPSULATION))
		if (skb->encapsulation) {
#ifdef HAVE_SKB_INNER_TRANSPORT_OFFSET
			ihs = skb_inner_transport_offset(skb) +
			      inner_tcp_hdrlen(skb);
#else
			ihs = (skb_inner_transport_header(skb) - skb->data) +
			      inner_tcp_hdrlen(skb);
#endif
		} else {
#endif
#endif
			ihs = skb_transport_offset(skb) + tcp_hdrlen(skb);
#if (KERNEL_VERSION(3, 8, 0) <= LINUX_VERSION_CODE)
#if (defined(HAVE_SKB_INNER_TRANSPORT_HEADER) && \
	defined(HAVE_SK_BUFF_ENCAPSULATION))
		}
#endif
#endif
		hdr_len = (skb_shinfo(skb)->gso_segs - 1) * ihs;
		tx_info->num_bytes = skb->len + (u64)hdr_len;

	} else {
		tx_info->num_bytes = skb->len > ETH_ZLEN ? skb->len : ETH_ZLEN;
	}

	tx_info->num_pkts = 1;
}

inline u8 hinic_get_vlan_pri(struct sk_buff *skb)
{
	u16 vlan_tci = 0;
	int err;

	err = vlan_get_tag(skb, &vlan_tci);
	if (err)
		return 0;

	return (vlan_tci & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
}

static void *__try_to_get_wqe(struct net_device *netdev, u16 q_id,
			      int wqebb_cnt, u16 *pi, u8 *owner)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	void *wqe = NULL;

	netif_stop_subqueue(netdev, q_id);
	/* We need to check again in a case another CPU has just
	 * made room available.
	 */
	if (unlikely(hinic_get_sq_free_wqebbs(nic_dev->hwdev, q_id) >=
					      wqebb_cnt)) {
		netif_start_subqueue(netdev, q_id);
		/* there have enough wqebbs after queue is wake up */
		wqe = hinic_get_sq_wqe(nic_dev->hwdev, q_id,
				       wqebb_cnt, pi, owner);
	}

	return wqe;
}

#ifdef HAVE_IP6_FRAG_ID_ENABLE_UFO
static int hinic_ufo_avoidance(struct sk_buff *skb, struct sk_buff **ufo_skb,
			       struct net_device *netdev, struct hinic_txq *txq)
{
	__be32 ip6_frag_id = skb_shinfo(skb)->ip6_frag_id;
	u16 gso_size = skb_shinfo(skb)->gso_size;
	struct frag_hdr ipv6_fhdr = {0};
	union hinic_ip ip;
	u32 l2_l3_hlen, l3_payload_len, extra_len;
	u32 skb_len, nsize, frag_data_len = 0;
	u16 ipv6_fhl_ext = sizeof(struct frag_hdr);
	__wsum frag_data_csum = 0;
	u16 frag_offset = 0;
	u8 *tmp = NULL;
	int err;

	ip.hdr = skb_network_header(skb);
	if (ip.v6->version == 6 && ip.v6->nexthdr != NEXTHDR_UDP) {
		TXQ_STATS_INC(txq, ufo_pkt_unsupport);
		return -EPROTONOSUPPORT;
	}

	/* linearize the big ufo packet */
	err = skb_linearize(skb);
	if (err) {
		TXQ_STATS_INC(txq, ufo_linearize_err);
		return err;
	}

	extra_len = skb->len - HINIC_GSO_MAX_SIZE;
	l2_l3_hlen = (u32)(skb_transport_header(skb) - skb_mac_header(skb));
	l3_payload_len = skb->len - l2_l3_hlen;
	frag_data_len += extra_len +
			((l3_payload_len - extra_len) % gso_size);
	skb_len = skb->len - frag_data_len;

	/* ipv6 need external frag header */
	if (ip.v6->version == 6)
		nsize = frag_data_len + l2_l3_hlen + ipv6_fhl_ext;
	else
		nsize = frag_data_len + l2_l3_hlen;

	*ufo_skb = netdev_alloc_skb_ip_align(netdev, nsize);
	if (!*ufo_skb) {
		TXQ_STATS_INC(txq, ufo_alloc_skb_err);
		return -ENOMEM;
	}

	/* copy l2_l3 layer header from original skb to ufo_skb */
	skb_copy_from_linear_data_offset(skb, 0, skb_put(*ufo_skb, l2_l3_hlen),
					 l2_l3_hlen);

	/* reserve ipv6 external frag header for ufo_skb */
	if (ip.v6->version == 6) {
		ipv6_fhdr.nexthdr = NEXTHDR_UDP;
		ipv6_fhdr.reserved = 0;
		frag_offset = (u16)(l3_payload_len - frag_data_len);
		ipv6_fhdr.frag_off = htons(frag_offset);
		ipv6_fhdr.identification = ip6_frag_id;
		tmp = skb_put(*ufo_skb, ipv6_fhl_ext);
		memcpy(tmp, &ipv6_fhdr, ipv6_fhl_ext);
	}

	/* split original one skb to two parts: skb and ufo_skb */
	skb_split(skb, (*ufo_skb), skb_len);

	/* modify skb ip total len */
	ip.hdr = skb_network_header(skb);
	if (ip.v4->version == 4)
		ip.v4->tot_len = htons(ntohs(ip.v4->tot_len) -
							(u16)frag_data_len);
	else
		ip.v6->payload_len = htons(ntohs(ip.v6->payload_len) -
							(u16)frag_data_len);

	/* set ufo_skb network header */
	skb_set_network_header(*ufo_skb, skb_network_offset(skb));

	/* set vlan offload feature */
	(*ufo_skb)->vlan_tci = skb->vlan_tci;

	/* modify ufo_skb ip total len, flag, frag_offset and compute csum */
	ip.hdr = skb_network_header(*ufo_skb);
	if (ip.v4->version == 4) {
		/* compute ufo_skb data csum and put into skb udp csum */
		tmp = (*ufo_skb)->data + l2_l3_hlen;
		frag_data_csum = csum_partial(tmp, (int)frag_data_len, 0);
		udp_hdr(skb)->check =
				(__sum16)csum_add(~csum_fold(frag_data_csum),
						  udp_hdr(skb)->check);

		ip.v4->tot_len = htons((u16)(skb_network_header_len(skb) +
								frag_data_len));
		ip.v4->frag_off = 0;
		frag_offset = (u16)((l3_payload_len - frag_data_len) >> 3);
		ip.v4->frag_off = htons(frag_offset);
		ip_send_check(ip.v4);
	} else {
		/* compute ufo_skb data csum and put into skb udp csum */
		tmp = (*ufo_skb)->data + l2_l3_hlen + ipv6_fhl_ext;
		frag_data_csum = csum_partial(tmp, (int)frag_data_len, 0);
		udp_hdr(skb)->check =
				(__sum16)csum_add(~csum_fold(frag_data_csum),
						  udp_hdr(skb)->check);

		ip.v6->payload_len = htons(ipv6_fhl_ext + (u16)frag_data_len);
		ip.v6->nexthdr = NEXTHDR_FRAGMENT;
	}

	return 0;
}
#endif

#define HINIC_FRAG_STATUS_OK		0
#define HINIC_FRAG_STATUS_IGNORE	1

static netdev_tx_t hinic_send_one_skb(struct sk_buff *skb,
				      struct net_device *netdev,
				      struct hinic_txq *txq,
				      u8 *flag, u8 avd_flag)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_tx_info *tx_info;
	struct hinic_sq_wqe *wqe = NULL;
	enum tx_offload_type offload = 0;
	u16 q_id = txq->q_id;
	u32 queue_info = 0;
	u8 owner = 0;
	u16 pi = 0;
	int err, wqebb_cnt;
	u16 num_sge = 0;
	u16 original_nr_frags;
	u16 new_nr_frags;
	u16 i;
	int frag_err = HINIC_FRAG_STATUS_OK;

	/* skb->dev will not initialized when calling netdev_alloc_skb_ip_align
	 * and parameter of length is largger then PAGE_SIZE(under redhat7.3),
	 * but skb->dev will be used in vlan_get_tag or somewhere
	 */
	if (unlikely(!skb->dev))
		skb->dev = netdev;

	if (unlikely(skb->len < MIN_SKB_LEN)) {
		if (skb_pad(skb, (int)(MIN_SKB_LEN - skb->len))) {
			TXQ_STATS_INC(txq, skb_pad_err);
			goto tx_skb_pad_err;
		}

		skb->len = MIN_SKB_LEN;
	}

	original_nr_frags = skb_shinfo(skb)->nr_frags;
	new_nr_frags = original_nr_frags;

	/* If size of lastest frags are all zero, should ignore this frags.
	 * If size of some frag in the middle is zero, should drop this skb.
	 */
	for (i = 0; i < original_nr_frags; i++) {
		if ((skb_frag_size(&skb_shinfo(skb)->frags[i])) &&
		    frag_err == HINIC_FRAG_STATUS_OK)
			continue;

		if ((!skb_frag_size(&skb_shinfo(skb)->frags[i])) &&
		    frag_err == HINIC_FRAG_STATUS_OK) {
			frag_err = HINIC_FRAG_STATUS_IGNORE;
			new_nr_frags = i + 1;
			continue;
		}

		if ((!skb_frag_size(&skb_shinfo(skb)->frags[i])) &&
		    frag_err == HINIC_FRAG_STATUS_IGNORE)
			continue;

		if ((skb_frag_size(&skb_shinfo(skb)->frags[i])) &&
		    frag_err == HINIC_FRAG_STATUS_IGNORE) {
			TXQ_STATS_INC(txq, frag_size_err);
			goto tx_drop_pkts;
		}
	}

	num_sge = new_nr_frags + 1;

	/* if skb->len is more than 65536B but num_sge is 1,
	 * driver will drop it
	 */
	if (unlikely(skb->len > HINIC_GSO_MAX_SIZE && num_sge == 1)) {
		TXQ_STATS_INC(txq, frag_len_overflow);
		goto tx_drop_pkts;
	}

	/* if sge number more than 17, driver will set 17 sges */
	if (unlikely(num_sge > HINIC_MAX_SQ_SGE)) {
		TXQ_STATS_INC(txq, big_frags_pkts);
		num_sge = HINIC_MAX_SQ_SGE;
	}

	wqebb_cnt = HINIC_SQ_WQEBB_CNT(num_sge);
	if (likely(hinic_get_sq_free_wqebbs(nic_dev->hwdev, q_id) >=
			wqebb_cnt)) {
		if (likely(wqebb_cnt == 1)) {
			hinic_update_sq_pi(nic_dev->hwdev, q_id,
					   wqebb_cnt, &pi, &owner);
			wqe = txq->tx_info[pi].wqe;
		} else {
			wqe = hinic_get_sq_wqe(nic_dev->hwdev, q_id,
					       wqebb_cnt, &pi, &owner);
		}

	} else {
		wqe = __try_to_get_wqe(netdev, q_id, wqebb_cnt, &pi, &owner);
		if (likely(!wqe)) {
			TXQ_STATS_INC(txq, busy);
			return NETDEV_TX_BUSY;
		}
	}

	tx_info = &txq->tx_info[pi];
	tx_info->skb = skb;
	tx_info->wqebb_cnt = wqebb_cnt;
	tx_info->valid_nr_frags = new_nr_frags;

	__get_pkt_stats(tx_info, skb);

	offload = hinic_tx_offload(skb, &wqe->task, &queue_info, avd_flag);
	if (unlikely(offload == TX_OFFLOAD_INVALID)) {
		hinic_return_sq_wqe(nic_dev->hwdev, q_id, wqebb_cnt, owner);
		TXQ_STATS_INC(txq, offload_cow_skb_err);
		goto tx_drop_pkts;
	}

	err = tx_map_skb(nic_dev, skb, txq, tx_info, wqe->buf_descs,
			 new_nr_frags);
	if (err) {
		hinic_return_sq_wqe(nic_dev->hwdev, q_id, wqebb_cnt, owner);
		goto tx_drop_pkts;
	}

	hinic_prepare_sq_ctrl(&wqe->ctrl, queue_info, num_sge, owner);

	hinic_send_sq_wqe(nic_dev->hwdev, q_id, wqe, wqebb_cnt,
			  nic_dev->sq_cos_mapping[hinic_get_vlan_pri(skb)]);

	return NETDEV_TX_OK;

tx_drop_pkts:
	dev_kfree_skb_any(skb);

tx_skb_pad_err:
	TXQ_STATS_INC(txq, dropped);

	*flag = HINIC_TX_DROPED;
	return NETDEV_TX_OK;
}

netdev_tx_t hinic_lb_xmit_frame(struct sk_buff *skb,
				struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u16 q_id = skb_get_queue_mapping(skb);
	struct hinic_txq *txq;
	u8 flag = 0;

	if (unlikely(!nic_dev->heart_status)) {
		dev_kfree_skb_any(skb);
		HINIC_NIC_STATS_INC(nic_dev, tx_carrier_off_drop);
		return NETDEV_TX_OK;
	}

	txq = &nic_dev->txqs[q_id];

	return hinic_send_one_skb(skb, netdev, txq, &flag, HINIC_TX_NON_AVD);
}

netdev_tx_t hinic_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u16 q_id = skb_get_queue_mapping(skb);
	struct hinic_txq *txq;
	u8 flag = 0;
#ifdef HAVE_IP6_FRAG_ID_ENABLE_UFO
	struct sk_buff *ufo_skb;
	int err;
#endif

	if (unlikely(!netif_carrier_ok(netdev) ||
		     !nic_dev->heart_status)) {
		dev_kfree_skb_any(skb);
		HINIC_NIC_STATS_INC(nic_dev, tx_carrier_off_drop);
		return NETDEV_TX_OK;
	}

	if (unlikely(q_id >= nic_dev->num_qps)) {
		txq = &nic_dev->txqs[0];
		HINIC_NIC_STATS_INC(nic_dev, tx_invalid_qid);
		goto tx_drop_pkts;
	}
	txq = &nic_dev->txqs[q_id];

#ifdef HAVE_IP6_FRAG_ID_ENABLE_UFO
	/* UFO avoidance */
	if (unlikely((skb->len > HINIC_GSO_MAX_SIZE) &&
		     (skb_shinfo(skb)->gso_type & SKB_GSO_UDP))) {
		TXQ_STATS_INC(txq, big_udp_pkts);

		err = hinic_ufo_avoidance(skb, &ufo_skb, netdev, txq);
		if (err)
			goto tx_drop_pkts;

		err = hinic_send_one_skb(skb, netdev, txq, &flag,
					 HINIC_TX_UFO_AVD);
		if (err == NETDEV_TX_BUSY) {
			dev_kfree_skb_any(ufo_skb);
			return NETDEV_TX_BUSY;
		}

		if (flag == HINIC_TX_DROPED) {
			nicif_err(nic_dev, drv, netdev, "Send first skb failed for HINIC_TX_SKB_DROPED\n");
			dev_kfree_skb_any(ufo_skb);
			return NETDEV_TX_OK;
		}

		err = hinic_send_one_skb(ufo_skb, netdev, txq, &flag,
					 HINIC_TX_NON_AVD);
		if (err == NETDEV_TX_BUSY) {
			nicif_err(nic_dev, drv, netdev, "Send second skb failed for NETDEV_TX_BUSY\n");
			dev_kfree_skb_any(ufo_skb);
			return NETDEV_TX_OK;
		}

		return NETDEV_TX_OK;
	}
#endif

	return hinic_send_one_skb(skb, netdev, txq, &flag, HINIC_TX_NON_AVD);

tx_drop_pkts:
	dev_kfree_skb_any(skb);
	u64_stats_update_begin(&txq->txq_stats.syncp);
	txq->txq_stats.dropped++;
	u64_stats_update_end(&txq->txq_stats.syncp);

	return NETDEV_TX_OK;
}

static inline void tx_free_skb(struct hinic_nic_dev *nic_dev,
			       struct sk_buff *skb,
			       struct hinic_tx_info *tx_info)
{
	tx_unmap_skb(nic_dev, skb, tx_info->dma_len, tx_info->valid_nr_frags);

	kfree(tx_info->cpy_buff);
	tx_info->cpy_buff = NULL;
	dev_kfree_skb_any(skb);
}

static void free_all_tx_skbs(struct hinic_txq *txq)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(txq->netdev);
	struct hinic_tx_info *tx_info;
	u16 ci;
	int free_wqebbs = hinic_get_sq_free_wqebbs(nic_dev->hwdev,
						   txq->q_id) + 1;

	while (free_wqebbs < txq->q_depth) {
		ci = hinic_get_sq_local_ci(nic_dev->hwdev, txq->q_id);

		tx_info = &txq->tx_info[ci];

		tx_free_skb(nic_dev, tx_info->skb, tx_info);

		hinic_update_sq_local_ci(nic_dev->hwdev, txq->q_id,
					 tx_info->wqebb_cnt);

		free_wqebbs += tx_info->wqebb_cnt;
	}
}

int hinic_tx_poll(struct hinic_txq *txq, int budget)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(txq->netdev);
	struct sk_buff *skb;
	struct hinic_tx_info *tx_info;
	u64 tx_bytes = 0, wake = 0;
	int pkts = 0, nr_pkts = 0, wqebb_cnt = 0;
	u16 hw_ci, sw_ci = 0, q_id = txq->q_id;

	hw_ci = hinic_get_sq_hw_ci(nic_dev->hwdev, q_id);
	dma_rmb();
	sw_ci = hinic_get_sq_local_ci(nic_dev->hwdev, q_id);

	do {
		tx_info = &txq->tx_info[sw_ci];

		/* Whether all of the wqebb of this wqe is completed */
		if (hw_ci == sw_ci || ((hw_ci - sw_ci) &
		    txq->q_mask) < tx_info->wqebb_cnt) {
			break;
		}

		sw_ci = (u16)(sw_ci + tx_info->wqebb_cnt) & txq->q_mask;
		prefetch(&txq->tx_info[sw_ci]);

		wqebb_cnt += tx_info->wqebb_cnt;

		skb = tx_info->skb;
		tx_bytes += tx_info->num_bytes;
		nr_pkts += tx_info->num_pkts;
		pkts++;

		tx_free_skb(nic_dev, skb, tx_info);

	} while (likely(pkts < budget));

	hinic_update_sq_local_ci(nic_dev->hwdev, q_id, wqebb_cnt);

	if (unlikely(__netif_subqueue_stopped(nic_dev->netdev, q_id) &&
		     hinic_get_sq_free_wqebbs(nic_dev->hwdev, q_id) >= 1 &&
		     test_bit(HINIC_INTF_UP, &nic_dev->flags))) {
		struct netdev_queue *netdev_txq =
				netdev_get_tx_queue(txq->netdev, q_id);

		__netif_tx_lock(netdev_txq, smp_processor_id());
		/* To avoid re-waking subqueue with xmit_frame */
		if (__netif_subqueue_stopped(nic_dev->netdev, q_id)) {
			netif_wake_subqueue(nic_dev->netdev, q_id);
			wake++;
		}
		__netif_tx_unlock(netdev_txq);
	}

	u64_stats_update_begin(&txq->txq_stats.syncp);
	txq->txq_stats.bytes += tx_bytes;
	txq->txq_stats.packets += nr_pkts;
	txq->txq_stats.wake += wake;
	u64_stats_update_end(&txq->txq_stats.syncp);

	return pkts;
}

int hinic_setup_tx_wqe(struct hinic_txq *txq)
{
	struct net_device *netdev = txq->netdev;
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_sq_wqe *wqe;
	struct hinic_tx_info *tx_info;
	u16 pi = 0;
	int i;
	u8 owner = 0;

	for (i = 0; i < txq->q_depth; i++) {
		tx_info = &txq->tx_info[i];

		wqe = hinic_get_sq_wqe(nic_dev->hwdev, txq->q_id,
				       1, &pi, &owner);
		if (!wqe) {
			nicif_err(nic_dev, drv, netdev, "Failed to get SQ wqe\n");
			break;
		}

		tx_info->wqe = wqe;
	}

	hinic_return_sq_wqe(nic_dev->hwdev, txq->q_id, txq->q_depth, owner);

	return i;
}

int hinic_setup_all_tx_resources(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_txq *txq;
	u64 tx_info_sz;
	u16 i, q_id;
	int err;

	for (q_id = 0; q_id < nic_dev->num_qps; q_id++) {
		txq = &nic_dev->txqs[q_id];
		tx_info_sz = txq->q_depth * sizeof(*txq->tx_info);
		if (!tx_info_sz) {
			nicif_err(nic_dev, drv, netdev, "Cannot allocate zero size tx%d info\n",
				  q_id);
			err = -EINVAL;
			goto init_txq_err;
		}

		txq->tx_info = kzalloc(tx_info_sz, GFP_KERNEL);
		if (!txq->tx_info) {
			nicif_err(nic_dev, drv, netdev, "Failed to allocate Tx:%d info\n",
				  q_id);
			err = -ENOMEM;
			goto init_txq_err;
		}

		err = hinic_setup_tx_wqe(txq);
		if (err != txq->q_depth) {
			nicif_err(nic_dev, drv, netdev, "Failed to setup Tx:%d wqe\n",
				  q_id);
			q_id++;
			goto init_txq_err;
		}
	}

	return 0;

init_txq_err:
	for (i = 0; i < q_id; i++) {
		txq = &nic_dev->txqs[i];
		kfree(txq->tx_info);
	}

	return err;
}

void hinic_free_all_tx_resources(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_txq *txq;
	u16 q_id;

	for (q_id = 0; q_id < nic_dev->num_qps; q_id++) {
		txq = &nic_dev->txqs[q_id];
		free_all_tx_skbs(txq);
		kfree(txq->tx_info);
	}
}

void hinic_set_sq_default_cos(struct net_device *netdev, u8 cos_id)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	int up;

	for (up = HINIC_DCB_UP_MAX - 1; up >= 0; up--)
		nic_dev->sq_cos_mapping[up] = nic_dev->default_cos_id;
}

int hinic_sq_cos_mapping(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_dcb_state dcb_state = {0};
	u8 default_cos = 0;
	int err;

	if (HINIC_FUNC_IS_VF(nic_dev->hwdev)) {
		err = hinic_get_pf_dcb_state(nic_dev->hwdev, &dcb_state);
		if (err) {
			hinic_info(nic_dev, drv, "Failed to get vf default cos\n");
			return err;
		}

		default_cos = dcb_state.default_cos;
		nic_dev->default_cos_id = default_cos;
		hinic_set_sq_default_cos(nic_dev->netdev, default_cos);
	} else {
		default_cos = nic_dev->default_cos_id;
		if (test_bit(HINIC_DCB_ENABLE, &nic_dev->flags))
			memcpy(nic_dev->sq_cos_mapping, nic_dev->up_cos,
			       sizeof(nic_dev->sq_cos_mapping));
		else
			hinic_set_sq_default_cos(nic_dev->netdev, default_cos);

		dcb_state.dcb_on = !!test_bit(HINIC_DCB_ENABLE,
					      &nic_dev->flags);
		dcb_state.default_cos = default_cos;
		memcpy(dcb_state.up_cos, nic_dev->sq_cos_mapping,
		       sizeof(dcb_state.up_cos));

		err = hinic_set_dcb_state(nic_dev->hwdev, &dcb_state);
		if (err)
			hinic_info(nic_dev, drv, "Failed to set vf default cos\n");
	}

	return err;
}

int hinic_alloc_txqs(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct pci_dev *pdev = nic_dev->pdev;
	struct hinic_txq *txq;
	u16 q_id, num_txqs = nic_dev->max_qps;
	u64 txq_size;

	txq_size = num_txqs * sizeof(*nic_dev->txqs);
	if (!txq_size) {
		nic_err(&pdev->dev, "Cannot allocate zero size txqs\n");
		return -EINVAL;
	}

	nic_dev->txqs = kzalloc(txq_size, GFP_KERNEL);

	if (!nic_dev->txqs) {
		nic_err(&pdev->dev, "Failed to allocate txqs\n");
		return -ENOMEM;
	}

	for (q_id = 0; q_id < num_txqs; q_id++) {
		txq = &nic_dev->txqs[q_id];
		txq->netdev = netdev;
		txq->q_id = q_id;
		txq->q_depth = nic_dev->sq_depth;
		txq->q_mask = nic_dev->sq_depth - 1;

		txq_stats_init(txq);
	}

	return 0;
}

void hinic_free_txqs(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	kfree(nic_dev->txqs);
}

/* should stop transmit any packets before calling this function */
#define HINIC_FLUSH_QUEUE_TIMEOUT	1000

bool hinic_get_hw_handle_status(void *hwdev, u16 q_id)
{
	u16 sw_pi = 0, hw_ci = 0;

	sw_pi = hinic_dbg_get_sq_pi(hwdev, q_id);
	hw_ci = hinic_get_sq_hw_ci(hwdev, q_id);

	return sw_pi == hw_ci;
}

int hinic_stop_sq(struct hinic_txq *txq)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(txq->netdev);
	unsigned long timeout;
	int err;

	timeout = msecs_to_jiffies(HINIC_FLUSH_QUEUE_TIMEOUT) + jiffies;
	do {
		if (hinic_get_hw_handle_status(nic_dev->hwdev, txq->q_id))
			return 0;

		usleep_range(900, 1000);
	} while (time_before(jiffies, timeout));

	/* force hardware to drop packets */
	timeout = msecs_to_jiffies(HINIC_FLUSH_QUEUE_TIMEOUT) + jiffies;
	do {
		if (hinic_get_hw_handle_status(nic_dev->hwdev, txq->q_id))
			return 0;

		err = hinic_force_drop_tx_pkt(nic_dev->hwdev);
		if (err)
			break;

		usleep_range(9900, 10000);
	} while (time_before(jiffies, timeout));

	/* Avoid msleep takes too long and get a fake result */
	if (hinic_get_hw_handle_status(nic_dev->hwdev, txq->q_id))
		return 0;

	return -EFAULT;
}

void hinic_flush_txqs(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u16 qid;
	int err;

	for (qid = 0; qid < nic_dev->num_qps; qid++) {
		err = hinic_stop_sq(&nic_dev->txqs[qid]);
		if (err)
			nicif_err(nic_dev, drv, netdev,
				  "Failed to stop sq%d\n", qid);
	}
}
