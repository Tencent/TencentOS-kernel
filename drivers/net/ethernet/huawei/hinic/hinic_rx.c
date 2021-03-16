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
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/u64_stats_sync.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/sctp.h>
#include <linux/pkt_sched.h>
#include <linux/ipv6.h>

#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_hw_mgmt.h"
#include "hinic_nic_io.h"
#include "hinic_nic_cfg.h"
#include "hinic_nic_dev.h"
#include "hinic_qp.h"
#include "hinic_rx.h"

static void hinic_clear_rss_config_user(struct hinic_nic_dev *nic_dev);

#define HINIC_RX_HDR_SIZE			256
#define HINIC_RX_IPV6_PKT			7
#define HINIC_RX_VXLAN_PKT			0xb

#define RXQ_STATS_INC(rxq, field)			\
{							\
	u64_stats_update_begin(&rxq->rxq_stats.syncp);	\
	rxq->rxq_stats.field++;				\
	u64_stats_update_end(&rxq->rxq_stats.syncp);	\
}

static bool rx_alloc_mapped_page(struct hinic_rxq *rxq,
				 struct hinic_rx_info *rx_info)
{
	struct net_device *netdev = rxq->netdev;
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct pci_dev *pdev = nic_dev->pdev;

	struct page *page = rx_info->page;
	dma_addr_t dma = rx_info->buf_dma_addr;

	if (likely(dma))
		return true;

	/* alloc new page for storage */
	page = alloc_pages_node(NUMA_NO_NODE, GFP_ATOMIC | __GFP_COLD |
				__GFP_COMP, nic_dev->page_order);
	if (unlikely(!page)) {
		nicif_err(nic_dev, drv, netdev, "Alloc rxq: %d page failed\n",
			  rxq->q_id);
		return false;
	}

	/* map page for use */
	dma = dma_map_page(&pdev->dev, page, 0, rxq->dma_rx_buff_size,
			   DMA_FROM_DEVICE);

	/* if mapping failed free memory back to system since
	 * there isn't much point in holding memory we can't use
	 */
	if (unlikely(dma_mapping_error(&pdev->dev, dma))) {
		nicif_err(nic_dev, drv, netdev, "Failed to map page to rx buffer\n");
		__free_pages(page, nic_dev->page_order);
		return false;
	}

	rx_info->page = page;
	rx_info->buf_dma_addr = dma;
	rx_info->page_offset = 0;

	return true;
}

static int hinic_rx_fill_wqe(struct hinic_rxq *rxq)
{
	struct net_device *netdev = rxq->netdev;
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_rq_wqe *rq_wqe;
	struct hinic_rx_info *rx_info;
	dma_addr_t dma_addr = 0;
	u16 pi = 0;
	int rq_wqe_len;
	int i;

	for (i = 0; i < rxq->q_depth; i++) {
		rx_info = &rxq->rx_info[i];

		rq_wqe = hinic_get_rq_wqe(nic_dev->hwdev, rxq->q_id, &pi);
		if (!rq_wqe) {
			nicif_err(nic_dev, drv, netdev, "Failed to get rq wqe, rxq id: %d, wqe id: %d\n",
				  rxq->q_id, i);
			break;
		}

		hinic_prepare_rq_wqe(rq_wqe, pi, dma_addr, rx_info->cqe_dma);

		rq_wqe_len = sizeof(struct hinic_rq_wqe);
		hinic_cpu_to_be32(rq_wqe, rq_wqe_len);
		rx_info->rq_wqe = rq_wqe;
	}

	hinic_return_rq_wqe(nic_dev->hwdev, rxq->q_id, rxq->q_depth);

	return i;
}

static int hinic_rx_fill_buffers(struct hinic_rxq *rxq)
{
	struct net_device *netdev = rxq->netdev;
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_rq_wqe *rq_wqe;
	struct hinic_rx_info *rx_info;
	dma_addr_t dma_addr;
	int i;
	int free_wqebbs = rxq->delta - 1;

	for (i = 0; i < free_wqebbs; i++) {
		rx_info = &rxq->rx_info[rxq->next_to_update];

		if (unlikely(!rx_alloc_mapped_page(rxq, rx_info)))
			break;

		dma_addr = rx_info->buf_dma_addr + rx_info->page_offset;

		rq_wqe = rx_info->rq_wqe;

		rq_wqe->buf_desc.addr_high =
				cpu_to_be32(upper_32_bits(dma_addr));
		rq_wqe->buf_desc.addr_low =
				cpu_to_be32(lower_32_bits(dma_addr));
		rxq->next_to_update = (rxq->next_to_update + 1) & rxq->q_mask;
	}

	if (likely(i)) {
		/* Write all the wqes before pi update */
		wmb();

		hinic_update_rq_hw_pi(nic_dev->hwdev, rxq->q_id,
				      rxq->next_to_update);
		rxq->delta -= i;
		rxq->next_to_alloc = rxq->next_to_update;
	} else {
		nicif_err(nic_dev, drv, netdev, "Failed to allocate rx buffers, rxq id: %d\n",
			  rxq->q_id);
	}

	return i;
}

void hinic_rx_free_buffers(struct hinic_rxq *rxq)
{
	u16 i;
	struct hinic_nic_dev *nic_dev = netdev_priv(rxq->netdev);
	struct hinic_rx_info *rx_info;

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rxq->q_depth; i++) {
		rx_info = &rxq->rx_info[i];

		if (rx_info->buf_dma_addr) {
			dma_unmap_page(rxq->dev, rx_info->buf_dma_addr,
				       rxq->dma_rx_buff_size,
				       DMA_FROM_DEVICE);
			rx_info->buf_dma_addr = 0;
		}

		if (rx_info->page) {
			__free_pages(rx_info->page, nic_dev->page_order);
			rx_info->page = NULL;
		}
	}
}

static void hinic_reuse_rx_page(struct hinic_rxq *rxq,
				struct hinic_rx_info *old_rx_info)
{
	struct hinic_rx_info *new_rx_info;
	u16 nta = rxq->next_to_alloc;

	new_rx_info = &rxq->rx_info[nta];

	/* update, and store next to alloc */
	nta++;
	rxq->next_to_alloc = (nta < rxq->q_depth) ? nta : 0;

	new_rx_info->page = old_rx_info->page;
	new_rx_info->page_offset = old_rx_info->page_offset;
	new_rx_info->buf_dma_addr = old_rx_info->buf_dma_addr;

	/* sync the buffer for use by the device */
	dma_sync_single_range_for_device(rxq->dev, new_rx_info->buf_dma_addr,
					 new_rx_info->page_offset,
					 rxq->buf_len,
					 DMA_FROM_DEVICE);
}

static bool hinic_add_rx_frag(struct hinic_rxq *rxq,
			      struct hinic_rx_info *rx_info,
			      struct sk_buff *skb, u32 size)
{
	struct page *page;
	u8 *va;

	page = rx_info->page;
	va = (u8 *)page_address(page) + rx_info->page_offset;
	prefetch(va);
#if L1_CACHE_BYTES < 128
	prefetch(va + L1_CACHE_BYTES);
#endif

	dma_sync_single_range_for_cpu(rxq->dev,
				      rx_info->buf_dma_addr,
				      rx_info->page_offset,
				      rxq->buf_len,
				      DMA_FROM_DEVICE);

	if (size <= HINIC_RX_HDR_SIZE && !skb_is_nonlinear(skb)) {
		memcpy(__skb_put(skb, size), va,
		       ALIGN(size, sizeof(long)));

		/* page is not reserved, we can reuse buffer as-is */
		if (likely(page_to_nid(page) == numa_node_id()))
			return true;

		/* this page cannot be reused so discard it */
		put_page(page);
		return false;
	}

	skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, page,
			(int)rx_info->page_offset, (int)size, rxq->buf_len);

	/* avoid re-using remote pages */
	if (unlikely(page_to_nid(page) != numa_node_id()))
		return false;

	/* if we are only owner of page we can reuse it */
	if (unlikely(page_count(page) != 1))
		return false;

	/* flip page offset to other buffer */
	rx_info->page_offset ^= rxq->buf_len;

#ifdef HAVE_PAGE_COUNT
	atomic_add(1, &page->_count);
#else
	page_ref_inc(page);
#endif

	return true;
}

static void __packaging_skb(struct hinic_rxq *rxq, struct sk_buff *head_skb,
			    u8 sge_num, u32 pkt_len)
{
	struct hinic_rx_info *rx_info;
	struct sk_buff *skb;
	u8 frag_num = 0;
	u32 size;
	u16 sw_ci;

	sw_ci = ((u32)rxq->cons_idx) & rxq->q_mask;
	skb = head_skb;
	while (sge_num) {
		rx_info = &rxq->rx_info[sw_ci];
		sw_ci = (sw_ci + 1) & rxq->q_mask;
		if (unlikely(pkt_len > rxq->buf_len)) {
			size = rxq->buf_len;
			pkt_len -= rxq->buf_len;
		} else {
			size = pkt_len;
		}

		if (unlikely(frag_num == MAX_SKB_FRAGS)) {
			frag_num = 0;
			if (skb == head_skb)
				skb = skb_shinfo(skb)->frag_list;
			else
				skb = skb->next;
		}

		if (unlikely(skb != head_skb)) {
			head_skb->len += size;
			head_skb->data_len += size;
			head_skb->truesize += rxq->buf_len;
		}

		if (likely(hinic_add_rx_frag(rxq, rx_info, skb, size))) {
			hinic_reuse_rx_page(rxq, rx_info);
		} else {
			/* we are not reusing the buffer so unmap it */
			dma_unmap_page(rxq->dev, rx_info->buf_dma_addr,
				       rxq->dma_rx_buff_size, DMA_FROM_DEVICE);
		}
		/* clear contents of buffer_info */
		rx_info->buf_dma_addr = 0;
		rx_info->page = NULL;
		sge_num--;
		frag_num++;
	}
}

static struct sk_buff *hinic_fetch_rx_buffer(struct hinic_rxq *rxq, u32 pkt_len)
{
	struct sk_buff *head_skb, *cur_skb, *skb = NULL;
	struct net_device *netdev = rxq->netdev;
	u8 sge_num, skb_num;
	u16 wqebb_cnt = 0;

	head_skb = netdev_alloc_skb_ip_align(netdev, HINIC_RX_HDR_SIZE);
	if (unlikely(!head_skb))
		return NULL;

	sge_num = (u8)(pkt_len >> rxq->rx_buff_shift) +
			((pkt_len & (rxq->buf_len - 1)) ? 1 : 0);
	if (likely(sge_num <= MAX_SKB_FRAGS))
		skb_num = 1;
	else
		skb_num = (sge_num / MAX_SKB_FRAGS) +
			((sge_num % MAX_SKB_FRAGS) ? 1 : 0);

	while (unlikely(skb_num > 1)) {
		cur_skb = netdev_alloc_skb_ip_align(netdev, HINIC_RX_HDR_SIZE);
		if (unlikely(!cur_skb))
			goto alloc_skb_fail;

		if (!skb) {
			skb_shinfo(head_skb)->frag_list = cur_skb;
			skb = cur_skb;
		} else {
			skb->next = cur_skb;
			skb = cur_skb;
		}

		skb_num--;
	}

	prefetchw(head_skb->data);
	wqebb_cnt = sge_num;

	__packaging_skb(rxq, head_skb, sge_num, pkt_len);

	rxq->cons_idx += wqebb_cnt;
	rxq->delta += wqebb_cnt;

	return head_skb;

alloc_skb_fail:
	dev_kfree_skb_any(head_skb);
	return NULL;
}

void hinic_rxq_get_stats(struct hinic_rxq *rxq,
			 struct hinic_rxq_stats *stats)
{
	struct hinic_rxq_stats *rxq_stats = &rxq->rxq_stats;
	unsigned int start;

	u64_stats_update_begin(&stats->syncp);
	do {
		start = u64_stats_fetch_begin(&rxq_stats->syncp);
		stats->bytes = rxq_stats->bytes;
		stats->packets = rxq_stats->packets;
		stats->errors = rxq_stats->csum_errors +
				rxq_stats->other_errors;
		stats->csum_errors = rxq_stats->csum_errors;
		stats->other_errors = rxq_stats->other_errors;
		stats->unlock_bp = rxq_stats->unlock_bp;
		stats->dropped = rxq_stats->dropped;
	} while (u64_stats_fetch_retry(&rxq_stats->syncp, start));
	u64_stats_update_end(&stats->syncp);
}

void hinic_rxq_clean_stats(struct hinic_rxq_stats *rxq_stats)
{
	u64_stats_update_begin(&rxq_stats->syncp);
	rxq_stats->bytes = 0;
	rxq_stats->packets = 0;
	rxq_stats->errors = 0;
	rxq_stats->csum_errors = 0;
	rxq_stats->unlock_bp = 0;
	rxq_stats->other_errors = 0;
	rxq_stats->dropped = 0;

	rxq_stats->alloc_skb_err = 0;
	u64_stats_update_end(&rxq_stats->syncp);
}

static void rxq_stats_init(struct hinic_rxq *rxq)
{
	struct hinic_rxq_stats *rxq_stats = &rxq->rxq_stats;

	u64_stats_init(&rxq_stats->syncp);
	hinic_rxq_clean_stats(rxq_stats);
}

static void hinic_pull_tail(struct sk_buff *skb)
{
	skb_frag_t *frag = &skb_shinfo(skb)->frags[0];
	unsigned char *va;

	/* it is valid to use page_address instead of kmap since we are
	 * working with pages allocated out of the lomem pool per
	 * alloc_page(GFP_ATOMIC)
	 */
	va = skb_frag_address(frag);

	/* align pull length to size of long to optimize memcpy performance */
	skb_copy_to_linear_data(skb, va, HINIC_RX_HDR_SIZE);

	/* update all of the pointers */
	skb_frag_size_sub(frag, HINIC_RX_HDR_SIZE);
	frag->bv_offset += HINIC_RX_HDR_SIZE;
	skb->data_len -= HINIC_RX_HDR_SIZE;
	skb->tail += HINIC_RX_HDR_SIZE;
}

static void hinic_rx_csum(struct hinic_rxq *rxq, u32 status,
			  struct sk_buff *skb)
{
	struct net_device *netdev = rxq->netdev;
	u32 csum_err;

	csum_err = HINIC_GET_RX_CSUM_ERR(status);

	if (unlikely(csum_err == HINIC_RX_CSUM_IPSU_OTHER_ERR))
		rxq->rxq_stats.other_errors++;

	if (!(netdev->features & NETIF_F_RXCSUM))
		return;

	if (!csum_err) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else {
		/* pkt type is recognized by HW, and csum is err */
		if (!(csum_err & (HINIC_RX_CSUM_HW_CHECK_NONE |
		    HINIC_RX_CSUM_IPSU_OTHER_ERR)))
			rxq->rxq_stats.csum_errors++;

		skb->ip_summed = CHECKSUM_NONE;
	}
}

#ifdef HAVE_SKBUFF_CSUM_LEVEL
static void hinic_rx_gro(struct hinic_rxq *rxq, u32 offload_type,
			 struct sk_buff *skb)
{
	struct net_device *netdev = rxq->netdev;
	bool l2_tunnel;

	if (!(netdev->features & NETIF_F_GRO))
		return;

	l2_tunnel = HINIC_GET_RX_PKT_TYPE(offload_type) == HINIC_RX_VXLAN_PKT ?
			1 : 0;

	if (l2_tunnel && skb->ip_summed == CHECKSUM_UNNECESSARY)
		/* If we checked the outer header let the stack know */
		skb->csum_level = 1;
}
#endif /* HAVE_SKBUFF_CSUM_LEVEL */

#define HINIC_RX_BP_THD		128

static void hinic_unlock_bp(struct hinic_rxq *rxq, bool bp_en, bool force_en)
{
	struct net_device *netdev = rxq->netdev;
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	int free_wqebbs, err;

	if (bp_en)
		set_bit(HINIC_RX_STATUS_BP_EN, &rxq->status);

	free_wqebbs = rxq->delta - 1;
	if (test_bit(HINIC_RX_STATUS_BP_EN, &rxq->status) &&
	    (nic_dev->rq_depth - free_wqebbs) >= nic_dev->bp_upper_thd &&
		(rxq->bp_cnt >= HINIC_RX_BP_THD || force_en)) {
		err = hinic_set_iq_enable_mgmt(nic_dev->hwdev, rxq->q_id,
					       nic_dev->bp_lower_thd,
					       rxq->next_to_update);
		if (!err) {
			clear_bit(HINIC_RX_STATUS_BP_EN, &rxq->status);
			rxq->bp_cnt = 0;
			rxq->rxq_stats.unlock_bp++;
		} else {
			nicif_err(nic_dev, drv, netdev, "Failed to set iq enable\n");
		}
	}
}

static void hinic_copy_lp_data(struct hinic_nic_dev *nic_dev,
			       struct sk_buff *skb)
{
	struct net_device *netdev = nic_dev->netdev;
	u8 *lb_buf = nic_dev->lb_test_rx_buf;
	void *frag_data;
	int lb_len = nic_dev->lb_pkt_len;
	int pkt_offset, frag_len, i;

	if (nic_dev->lb_test_rx_idx == LP_PKT_CNT) {
		nic_dev->lb_test_rx_idx = 0;
		nicif_warn(nic_dev, drv, netdev, "Loopback test warning, recive too more test pkt\n");
	}

	if (skb->len != nic_dev->lb_pkt_len) {
		nicif_warn(nic_dev, drv, netdev, "Wrong packet length\n");
		nic_dev->lb_test_rx_idx++;
		return;
	}

	pkt_offset = nic_dev->lb_test_rx_idx * lb_len;
	frag_len = (int)skb_headlen(skb);
	memcpy((lb_buf + pkt_offset), skb->data, frag_len);
	pkt_offset += frag_len;
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		frag_data = skb_frag_address(&skb_shinfo(skb)->frags[i]);
		frag_len = (int)skb_frag_size(&skb_shinfo(skb)->frags[i]);
		memcpy((lb_buf + pkt_offset), frag_data, frag_len);
		pkt_offset += frag_len;
	}
	nic_dev->lb_test_rx_idx++;
}

int recv_one_pkt(struct hinic_rxq *rxq, struct hinic_rq_cqe *rx_cqe,
		 u32 pkt_len, u32 vlan_len, u32 status)
{
	struct sk_buff *skb;
	struct net_device *netdev = rxq->netdev;
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u32 offload_type;

	skb = hinic_fetch_rx_buffer(rxq, pkt_len);
	if (unlikely(!skb)) {
		RXQ_STATS_INC(rxq, alloc_skb_err);
		return -ENOMEM;
	}

	/* place header in linear portion of buffer */
	if (skb_is_nonlinear(skb))
		hinic_pull_tail(skb);

	hinic_rx_csum(rxq, status, skb);

	offload_type = be32_to_cpu(rx_cqe->offload_type);
#ifdef HAVE_SKBUFF_CSUM_LEVEL
	hinic_rx_gro(rxq, offload_type, skb);
#endif

#if defined(NETIF_F_HW_VLAN_CTAG_RX)
	if ((netdev->features & NETIF_F_HW_VLAN_CTAG_RX) &&
	    HINIC_GET_RX_VLAN_OFFLOAD_EN(offload_type)) {
#else
	if ((netdev->features & NETIF_F_HW_VLAN_RX) &&
	    HINIC_GET_RX_VLAN_OFFLOAD_EN(offload_type)) {
#endif
		u16 vid = HINIC_GET_RX_VLAN_TAG(vlan_len);

		/* if the packet is a vlan pkt, the vid may be 0 */
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vid);
	}

	if (unlikely(test_bit(HINIC_LP_TEST, &nic_dev->flags)))
		hinic_copy_lp_data(nic_dev, skb);

	skb_record_rx_queue(skb, rxq->q_id);
	skb->protocol = eth_type_trans(skb, netdev);

	if (skb_has_frag_list(skb)) {
#ifdef HAVE_NAPI_GRO_FLUSH_OLD
		napi_gro_flush(&rxq->irq_cfg->napi, false);
#else
		napi_gro_flush(&rxq->irq_cfg->napi);
#endif
		netif_receive_skb(skb);
	} else {
		napi_gro_receive(&rxq->irq_cfg->napi, skb);
	}

	return 0;
}

void rx_pass_super_cqe(struct hinic_rxq *rxq, u32 index, u32 pkt_num,
		       struct hinic_rq_cqe *cqe)
{
	u8 sge_num = 0;
	u32 pkt_len;

	while (index < pkt_num) {
		pkt_len = hinic_get_pkt_len_for_super_cqe
				(cqe, index == (pkt_num - 1));
		sge_num += (u8)(pkt_len >> rxq->rx_buff_shift) +
				((pkt_len & (rxq->buf_len - 1)) ? 1 : 0);
		index++;
	}

	rxq->cons_idx += sge_num;
	rxq->delta += sge_num;
}

static inline int __recv_supper_cqe(struct hinic_rxq *rxq,
				    struct hinic_rq_cqe *rx_cqe, u32 pkt_info,
				    u32 vlan_len, u32 status, int *pkts,
				    u64 *rx_bytes, u32 *dropped)
{
	u32 pkt_len;
	int i, pkt_num = 0;

	pkt_num = HINIC_GET_RQ_CQE_PKT_NUM(pkt_info);
	i = 0;
	while (i < pkt_num) {
		pkt_len = ((i == (pkt_num - 1)) ?
		    RQ_CQE_PKT_LEN_GET(pkt_info, LAST_LEN) :
		    RQ_CQE_PKT_LEN_GET(pkt_info, FIRST_LEN));
		if (unlikely(recv_one_pkt(rxq, rx_cqe, pkt_len,
					  vlan_len, status))) {
			if (i) {
				rx_pass_super_cqe(rxq, i,
						  pkt_num,
						  rx_cqe);
				*dropped += (pkt_num - i);
			}
			break;
		}

		*rx_bytes += pkt_len;
		(*pkts)++;
		i++;
	}

	if (!i)
		return -EFAULT;

	return 0;
}


#define LRO_PKT_HDR_LEN_IPV4		66
#define LRO_PKT_HDR_LEN_IPV6		86
#define LRO_PKT_HDR_LEN(cqe)		\
	(HINIC_GET_RX_PKT_TYPE(be32_to_cpu((cqe)->offload_type)) == \
	 HINIC_RX_IPV6_PKT ? LRO_PKT_HDR_LEN_IPV6 : LRO_PKT_HDR_LEN_IPV4)

int hinic_rx_poll(struct hinic_rxq *rxq, int budget)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(rxq->netdev);
	u32 status, pkt_len, vlan_len, pkt_info, dropped = 0;
	struct hinic_rq_cqe *rx_cqe;
	u64 rx_bytes = 0;
	u16 sw_ci, num_lro;
	int pkts = 0, nr_pkts = 0;
	bool bp_en = false;
	u16 num_wqe = 0;

	while (likely(pkts < budget)) {
		sw_ci = ((u32)rxq->cons_idx) & rxq->q_mask;
		rx_cqe = rxq->rx_info[sw_ci].cqe;
		status = be32_to_cpu(rx_cqe->status);

		if (!HINIC_GET_RX_DONE(status))
			break;

		/* make sure we read rx_done before packet length */
		rmb();

		vlan_len = be32_to_cpu(rx_cqe->vlan_len);
		pkt_info = be32_to_cpu(rx_cqe->pkt_info);
		pkt_len = HINIC_GET_RX_PKT_LEN(vlan_len);


		if (unlikely(HINIC_GET_SUPER_CQE_EN(pkt_info))) {
			if (unlikely(__recv_supper_cqe(rxq, rx_cqe, pkt_info,
						       vlan_len, status, &pkts,
						       &rx_bytes, &dropped)))
				break;
			nr_pkts += (int)HINIC_GET_RQ_CQE_PKT_NUM(pkt_info);
		} else {
			if (recv_one_pkt(rxq, rx_cqe, pkt_len,
					 vlan_len, status))
				break;
			rx_bytes += pkt_len;
			pkts++;
			nr_pkts++;

			num_lro = HINIC_GET_RX_NUM_LRO(status);
			if (num_lro) {
				rx_bytes += ((num_lro - 1) *
					    LRO_PKT_HDR_LEN(rx_cqe));

				num_wqe +=
				(u16)(pkt_len >> rxq->rx_buff_shift) +
				((pkt_len & (rxq->buf_len - 1)) ? 1 : 0);
			}
		}
		if (unlikely(HINIC_GET_RX_BP_EN(status))) {
			rxq->bp_cnt++;
			bp_en = true;
		}

		rx_cqe->status = 0;

		if (num_wqe >= nic_dev->lro_replenish_thld)
			break;
	}

	if (rxq->delta >= HINIC_RX_BUFFER_WRITE)
		hinic_rx_fill_buffers(rxq);

	if (unlikely(bp_en || test_bit(HINIC_RX_STATUS_BP_EN, &rxq->status)))
		hinic_unlock_bp(rxq, bp_en, pkts < budget);

	u64_stats_update_begin(&rxq->rxq_stats.syncp);
	rxq->rxq_stats.packets += nr_pkts;
	rxq->rxq_stats.bytes += rx_bytes;
	rxq->rxq_stats.dropped += dropped;
	u64_stats_update_end(&rxq->rxq_stats.syncp);
	return pkts;
}

static int rx_alloc_cqe(struct hinic_rxq *rxq)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(rxq->netdev);
	struct pci_dev *pdev = nic_dev->pdev;
	struct hinic_rx_info *rx_info;
	struct hinic_rq_cqe *cqe_va;
	dma_addr_t cqe_pa;
	u32 cqe_mem_size;
	int idx;

	cqe_mem_size = sizeof(*rx_info->cqe) * rxq->q_depth;
	rxq->cqe_start_vaddr = dma_zalloc_coherent(&pdev->dev, cqe_mem_size,
						   &rxq->cqe_start_paddr,
						   GFP_KERNEL);
	if (!rxq->cqe_start_vaddr) {
		nicif_err(nic_dev, drv, rxq->netdev, "Failed to allocate cqe dma\n");
		return -ENOMEM;
	}

	cqe_va = (struct hinic_rq_cqe *)rxq->cqe_start_vaddr;
	cqe_pa = rxq->cqe_start_paddr;

	for (idx = 0; idx < rxq->q_depth; idx++) {
		rx_info = &rxq->rx_info[idx];
		rx_info->cqe = cqe_va;
		rx_info->cqe_dma = cqe_pa;

		cqe_va++;
		cqe_pa += sizeof(*rx_info->cqe);
	}

	hinic_rq_cqe_addr_set(nic_dev->hwdev, rxq->q_id, rxq->cqe_start_paddr);
	return 0;
}

static void rx_free_cqe(struct hinic_rxq *rxq)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(rxq->netdev);
	struct pci_dev *pdev = nic_dev->pdev;
	u32 cqe_mem_size;

	cqe_mem_size = sizeof(struct hinic_rq_cqe) * rxq->q_depth;

	dma_free_coherent(&pdev->dev, cqe_mem_size,
			  rxq->cqe_start_vaddr, rxq->cqe_start_paddr);
}

static int hinic_setup_rx_resources(struct hinic_rxq *rxq,
				    struct net_device *netdev,
				    struct irq_info *entry)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(rxq->netdev);
	u64 rx_info_sz;
	int err, pkts;

	rxq->irq_id = entry->irq_id;
	rxq->msix_entry_idx = entry->msix_entry_idx;
	rxq->next_to_alloc = 0;
	rxq->next_to_update = 0;
	rxq->delta = rxq->q_depth;
	rxq->q_mask = rxq->q_depth - 1;
	rxq->cons_idx = 0;

	rx_info_sz = rxq->q_depth * sizeof(*rxq->rx_info);
	if (!rx_info_sz) {
		nicif_err(nic_dev, drv, netdev, "Cannot allocate zero size rx info\n");
		return -EINVAL;
	}

	rxq->rx_info = kzalloc(rx_info_sz, GFP_KERNEL);
	if (!rxq->rx_info)
		return -ENOMEM;

	err = rx_alloc_cqe(rxq);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to allocate Rx cqe\n");
		goto rx_cqe_err;
	}

	pkts = hinic_rx_fill_wqe(rxq);
	if (pkts != rxq->q_depth) {
		nicif_err(nic_dev, drv, netdev, "Failed to fill rx wqe\n");
		err = -ENOMEM;
		goto rx_pkts_err;
	}
	pkts = hinic_rx_fill_buffers(rxq);
	if (!pkts) {
		nicif_err(nic_dev, drv, netdev, "Failed to allocate Rx buffer\n");
		err = -ENOMEM;
		goto rx_pkts_err;
	}

	return 0;

rx_pkts_err:
	rx_free_cqe(rxq);

rx_cqe_err:
	kfree(rxq->rx_info);

	return err;
}

static void hinic_free_rx_resources(struct hinic_rxq *rxq)
{
	hinic_rx_free_buffers(rxq);
	rx_free_cqe(rxq);
	kfree(rxq->rx_info);
}

int hinic_setup_all_rx_resources(struct net_device *netdev,
				 struct irq_info *msix_entires)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u16 i, q_id;
	int err;

	for (q_id = 0; q_id < nic_dev->num_qps; q_id++) {
		err = hinic_setup_rx_resources(&nic_dev->rxqs[q_id],
					       nic_dev->netdev,
					       &msix_entires[q_id]);
		if (err) {
			nicif_err(nic_dev, drv, netdev, "Failed to set up rxq resource\n");
			goto init_rxq_err;
		}
	}

	return 0;

init_rxq_err:
	for (i = 0; i < q_id; i++)
		hinic_free_rx_resources(&nic_dev->rxqs[i]);

	return err;
}

void hinic_free_all_rx_resources(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u16 q_id;

	for (q_id = 0; q_id < nic_dev->num_qps; q_id++)
		hinic_free_rx_resources(&nic_dev->rxqs[q_id]);
}

int hinic_alloc_rxqs(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct pci_dev *pdev = nic_dev->pdev;
	struct hinic_rxq *rxq;
	u16 num_rxqs = nic_dev->max_qps;
	u16 q_id;
	u64 rxq_size;

	rxq_size = num_rxqs * sizeof(*nic_dev->rxqs);
	if (!rxq_size) {
		nic_err(&pdev->dev, "Cannot allocate zero size rxqs\n");
		return -EINVAL;
	}

	nic_dev->rxqs = kzalloc(rxq_size, GFP_KERNEL);
	if (!nic_dev->rxqs) {
		nic_err(&pdev->dev, "Failed to allocate rxqs\n");
		return -ENOMEM;
	}

	for (q_id = 0; q_id < num_rxqs; q_id++) {
		rxq = &nic_dev->rxqs[q_id];
		rxq->netdev = netdev;
		rxq->dev = &pdev->dev;
		rxq->q_id = q_id;
		rxq->buf_len = nic_dev->rx_buff_len;
		rxq->rx_buff_shift = ilog2(nic_dev->rx_buff_len);
		rxq->dma_rx_buff_size = RX_BUFF_NUM_PER_PAGE *
					nic_dev->rx_buff_len;
		rxq->q_depth = nic_dev->rq_depth;
		rxq->q_mask = nic_dev->rq_depth - 1;


		rxq_stats_init(rxq);
	}

	return 0;
}

void hinic_free_rxqs(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	hinic_clear_rss_config_user(nic_dev);
	kfree(nic_dev->rxqs);
}

void hinic_init_rss_parameters(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	nic_dev->rss_hash_engine = HINIC_RSS_HASH_ENGINE_TYPE_XOR;

	nic_dev->rss_type.tcp_ipv6_ext = 1;
	nic_dev->rss_type.ipv6_ext = 1;
	nic_dev->rss_type.tcp_ipv6 = 1;
	nic_dev->rss_type.ipv6 = 1;
	nic_dev->rss_type.tcp_ipv4 = 1;
	nic_dev->rss_type.ipv4 = 1;
	nic_dev->rss_type.udp_ipv6 = 1;
	nic_dev->rss_type.udp_ipv4 = 1;
}

void hinic_set_default_rss_indir(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (!nic_dev->rss_indir_user)
		return;

	nicif_info(nic_dev, drv, netdev,
		   "Discard user configured Rx flow hash indirection\n");

	kfree(nic_dev->rss_indir_user);
	nic_dev->rss_indir_user = NULL;
}

static void hinic_maybe_reconfig_rss_indir(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	int i;

	if (!nic_dev->rss_indir_user)
		return;

	if (test_bit(HINIC_DCB_ENABLE, &nic_dev->flags))
		goto discard_user_rss_indir;

	for (i = 0; i < HINIC_RSS_INDIR_SIZE; i++) {
		if (nic_dev->rss_indir_user[i] >= nic_dev->num_qps)
			goto discard_user_rss_indir;
	}

	return;

discard_user_rss_indir:
	hinic_set_default_rss_indir(netdev);
}

static void hinic_clear_rss_config_user(struct hinic_nic_dev *nic_dev)
{
	kfree(nic_dev->rss_hkey_user);

	nic_dev->rss_hkey_user_be = NULL;
	nic_dev->rss_hkey_user    = NULL;

	kfree(nic_dev->rss_indir_user);
	nic_dev->rss_indir_user = NULL;
}

static void hinic_fillout_indir_tbl(struct hinic_nic_dev *nic_dev,
				    u8 num_tcs, u32 *indir)
{
	u16 num_rss, tc_group_size;
	int i;

	if (num_tcs)
		tc_group_size = HINIC_RSS_INDIR_SIZE / num_tcs;
	else
		tc_group_size = HINIC_RSS_INDIR_SIZE;

	num_rss = nic_dev->num_rss;
	for (i = 0; i < HINIC_RSS_INDIR_SIZE; i++)
		indir[i] = (i / tc_group_size) * num_rss + i % num_rss;
}

static void hinic_rss_deinit(struct hinic_nic_dev *nic_dev)
{
	u8 prio_tc[HINIC_DCB_UP_MAX] = {0};

	hinic_rss_cfg(nic_dev->hwdev, 0, nic_dev->rss_tmpl_idx, 0, prio_tc);
}

/* In rx, iq means cos */
static u8 hinic_get_iqmap_by_tc(u8 *prio_tc, u8 num_iq, u8 tc)
{
	u8 i, map = 0;

	for (i = 0; i < num_iq; i++) {
		if (prio_tc[i] == tc)
			map |= (u8)(1U << ((num_iq - 1) - i));
	}

	return map;
}

static u8 hinic_get_tcid_by_rq(u32 *indir_tbl, u8 num_tcs, u16 rq_id)
{
	u16 tc_group_size;
	int i;

	tc_group_size = HINIC_RSS_INDIR_SIZE / num_tcs;
	for (i = 0; i < HINIC_RSS_INDIR_SIZE; i++) {
		if (indir_tbl[i] == rq_id)
			return (u8)(i / tc_group_size);
	}

	return 0xFF;	/* Invalid TC */
}

#define HINIC_NUM_IQ_PER_FUNC	8
static void hinic_get_rq2iq_map(struct hinic_nic_dev *nic_dev,
				u16 num_rq, u8 num_tcs, u8 *prio_tc,
				u32 *indir_tbl, u8 *map)
{
	u16 qid;
	u8 tc_id;

	if (!num_tcs)
		num_tcs = 1;

	for (qid = 0; qid < num_rq; qid++) {
		tc_id = hinic_get_tcid_by_rq(indir_tbl, num_tcs, qid);
		map[qid] = hinic_get_iqmap_by_tc(prio_tc,
						 HINIC_NUM_IQ_PER_FUNC, tc_id);
	}
}

int hinic_set_hw_rss_parameters(struct net_device *netdev, u8 rss_en, u8 num_tc,
				u8 *prio_tc)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u8 tmpl_idx = 0xFF;
	u8 default_rss_key[HINIC_RSS_KEY_SIZE] = {
			 0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
			 0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
			 0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
			 0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
			 0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa};
	u32 *indir_tbl;
	u8 *hkey;
	int err;

	tmpl_idx = nic_dev->rss_tmpl_idx;

	/* RSS key */
	if (nic_dev->rss_hkey_user)
		hkey = nic_dev->rss_hkey_user;
	else
		hkey = default_rss_key;
	err = hinic_rss_set_template_tbl(nic_dev->hwdev, tmpl_idx, hkey);
	if (err)
		return err;

	hinic_maybe_reconfig_rss_indir(netdev);
	indir_tbl = kzalloc(sizeof(u32) * HINIC_RSS_INDIR_SIZE, GFP_KERNEL);
	if (!indir_tbl) {
		nicif_err(nic_dev, drv, netdev, "Failed to allocate set hw rss indir_tbl\n");
		return -ENOMEM;
	}

	if (nic_dev->rss_indir_user)
		memcpy(indir_tbl, nic_dev->rss_indir_user,
		       sizeof(u32) * HINIC_RSS_INDIR_SIZE);
	else
		hinic_fillout_indir_tbl(nic_dev, num_tc, indir_tbl);

	err = hinic_rss_set_indir_tbl(nic_dev->hwdev, tmpl_idx, indir_tbl);
	if (err)
		goto out;

	err = hinic_set_rss_type(nic_dev->hwdev, tmpl_idx, nic_dev->rss_type);
	if (err)
		goto out;

	err = hinic_rss_set_hash_engine(nic_dev->hwdev, tmpl_idx,
					nic_dev->rss_hash_engine);
	if (err)
		goto out;

	err = hinic_rss_cfg(nic_dev->hwdev, rss_en, tmpl_idx, num_tc, prio_tc);
	if (err)
		goto out;

	kfree(indir_tbl);
	return 0;

out:
	kfree(indir_tbl);
	return err;
}

static int hinic_rss_init(struct hinic_nic_dev *nic_dev, u8 *rq2iq_map)
{
	struct net_device *netdev = nic_dev->netdev;
	u32 *indir_tbl;
	u8 cos, num_tc = 0;
	u8 prio_tc[HINIC_DCB_UP_MAX] = {0};
	int err;

	if (test_bit(HINIC_DCB_ENABLE, &nic_dev->flags)) {
		num_tc = nic_dev->max_cos;
		for (cos = 0; cos < HINIC_DCB_COS_MAX; cos++) {
			if (cos < HINIC_DCB_COS_MAX - nic_dev->max_cos)
				prio_tc[cos] = nic_dev->max_cos - 1;
			else
				prio_tc[cos] = (HINIC_DCB_COS_MAX - 1) - cos;
		}
	} else {
		num_tc = 0;
	}

	indir_tbl = kzalloc(sizeof(u32) * HINIC_RSS_INDIR_SIZE, GFP_KERNEL);
	if (!indir_tbl) {
		nicif_err(nic_dev, drv, netdev, "Failed to allocate rss init indir_tbl\n");
		return -ENOMEM;
	}

	if (nic_dev->rss_indir_user)
		memcpy(indir_tbl, nic_dev->rss_indir_user,
		       sizeof(u32) * HINIC_RSS_INDIR_SIZE);
	else
		hinic_fillout_indir_tbl(nic_dev, num_tc, indir_tbl);
	err = hinic_set_hw_rss_parameters(netdev, 1, num_tc, prio_tc);
	if (err) {
		kfree(indir_tbl);
		return err;
	}

	hinic_get_rq2iq_map(nic_dev, nic_dev->num_qps, num_tc,
			    prio_tc, indir_tbl, rq2iq_map);

	kfree(indir_tbl);
	return 0;
}

int hinic_update_hw_tc_map(struct net_device *netdev, u8 num_tc, u8 *prio_tc)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u8 tmpl_idx = nic_dev->rss_tmpl_idx;

	/* RSS must be enable when dcb is enabled */
	return hinic_rss_cfg(nic_dev->hwdev, 1, tmpl_idx, num_tc, prio_tc);
}

int hinic_rx_configure(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u8 rq2iq_map[HINIC_MAX_NUM_RQ];
	int err;

	/* Set all rq mapping to all iq in default */
	memset(rq2iq_map, 0xFF, sizeof(rq2iq_map));
	if (test_bit(HINIC_RSS_ENABLE, &nic_dev->flags)) {
		err = hinic_rss_init(nic_dev, rq2iq_map);
		if (err) {
			nicif_err(nic_dev, drv, netdev, "Failed to init rss\n");
			return -EFAULT;
		}
	}

	err = hinic_dcb_set_rq_iq_mapping(nic_dev->hwdev,
					  hinic_func_max_qnum(nic_dev->hwdev),
					  rq2iq_map);
	if (err) {
		nicif_err(nic_dev, drv, netdev, "Failed to set rq_iq mapping\n");
		goto set_rq_cos_mapping_err;
	}

	return 0;

set_rq_cos_mapping_err:
	if (test_bit(HINIC_RSS_ENABLE, &nic_dev->flags))
		hinic_rss_deinit(nic_dev);

	return err;
}

void hinic_rx_remove_configure(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (test_bit(HINIC_RSS_ENABLE, &nic_dev->flags))
		hinic_rss_deinit(nic_dev);
}
