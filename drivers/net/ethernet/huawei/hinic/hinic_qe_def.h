/* SPDX-License-Identifier: GPL-2.0-only */
/* Huawei HiNIC PCI Express Linux driver
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

#ifndef __HINIC_QE_DEF_H__
#define __HINIC_QE_DEF_H__

#ifdef __cplusplus
    #if __cplusplus
extern "C"{
    #endif
#endif /* __cplusplus */

#define HINIC_SQ_WQEBB_SIZE		64
#define HINIC_RQ_WQE_SIZE		32
#define HINIC_SQ_WQEBB_SHIFT		6
#define HINIC_RQ_WQEBB_SHIFT		5

#define HINIC_MAX_QUEUE_DEPTH		4096
#define HINIC_MIN_QUEUE_DEPTH		128
#define HINIC_TXD_ALIGN                 1
#define HINIC_RXD_ALIGN                 1

#define HINIC_SQ_DEPTH			1024
#define HINIC_RQ_DEPTH			1024

#define HINIC_RQ_WQE_MAX_SIZE		32

#define SIZE_8BYTES(size)	(ALIGN((u32)(size), 8) >> 3)

/************** SQ_CTRL ***************/
#define SQ_CTRL_BUFDESC_SECT_LEN_SHIFT			0
#define SQ_CTRL_TASKSECT_LEN_SHIFT			16
#define SQ_CTRL_DATA_FORMAT_SHIFT			22
#define SQ_CTRL_LEN_SHIFT				29
#define SQ_CTRL_OWNER_SHIFT				31

#define SQ_CTRL_BUFDESC_SECT_LEN_MASK			0xFFU
#define SQ_CTRL_TASKSECT_LEN_MASK			0x1FU
#define SQ_CTRL_DATA_FORMAT_MASK			0x1U
#define SQ_CTRL_LEN_MASK				0x3U
#define SQ_CTRL_OWNER_MASK				0x1U

#define SQ_CTRL_GET(val, member)	(((val) >> SQ_CTRL_##member##_SHIFT) \
					& SQ_CTRL_##member##_MASK)

#define SQ_CTRL_CLEAR(val, member)	((val) & \
					(~(SQ_CTRL_##member##_MASK << \
					SQ_CTRL_##member##_SHIFT)))

#define SQ_CTRL_QUEUE_INFO_PLDOFF_SHIFT			2
#define SQ_CTRL_QUEUE_INFO_UFO_SHIFT			10
#define SQ_CTRL_QUEUE_INFO_TSO_SHIFT			11
#define SQ_CTRL_QUEUE_INFO_TCPUDP_CS_SHIFT		12
#define SQ_CTRL_QUEUE_INFO_MSS_SHIFT			13
#define SQ_CTRL_QUEUE_INFO_SCTP_SHIFT			27
#define SQ_CTRL_QUEUE_INFO_UC_SHIFT			28
#define SQ_CTRL_QUEUE_INFO_PRI_SHIFT			29

#define SQ_CTRL_QUEUE_INFO_PLDOFF_MASK			0xFFU
#define SQ_CTRL_QUEUE_INFO_UFO_MASK			0x1U
#define SQ_CTRL_QUEUE_INFO_TSO_MASK			0x1U
#define SQ_CTRL_QUEUE_INFO_TCPUDP_CS_MASK		0x1U
#define SQ_CTRL_QUEUE_INFO_MSS_MASK			0x3FFFU
#define SQ_CTRL_QUEUE_INFO_SCTP_MASK			0x1U
#define SQ_CTRL_QUEUE_INFO_UC_MASK			0x1U
#define SQ_CTRL_QUEUE_INFO_PRI_MASK			0x7U

#define SQ_CTRL_QUEUE_INFO_SET(val, member)	\
	(((u32)(val) & SQ_CTRL_QUEUE_INFO_##member##_MASK) \
	<< SQ_CTRL_QUEUE_INFO_##member##_SHIFT)

#define SQ_CTRL_QUEUE_INFO_GET(val, member)	\
	(((val) >> SQ_CTRL_QUEUE_INFO_##member##_SHIFT) \
	& SQ_CTRL_QUEUE_INFO_##member##_MASK)

#define SQ_CTRL_QUEUE_INFO_CLEAR(val, member)	\
	((val) & (~(SQ_CTRL_QUEUE_INFO_##member##_MASK << \
	SQ_CTRL_QUEUE_INFO_##member##_SHIFT)))

#define	SQ_TASK_INFO0_L2HDR_LEN_SHIFT		0
#define	SQ_TASK_INFO0_L4OFFLOAD_SHIFT		8
#define	SQ_TASK_INFO0_INNER_L3TYPE_SHIFT	10
#define	SQ_TASK_INFO0_VLAN_OFFLOAD_SHIFT	12
#define	SQ_TASK_INFO0_PARSE_FLAG_SHIFT		13
#define	SQ_TASK_INFO0_UFO_AVD_SHIFT		14
#define	SQ_TASK_INFO0_TSO_UFO_SHIFT		15
#define SQ_TASK_INFO0_VLAN_TAG_SHIFT		16

#define	SQ_TASK_INFO0_L2HDR_LEN_MASK		0xFFU
#define	SQ_TASK_INFO0_L4OFFLOAD_MASK		0x3U
#define	SQ_TASK_INFO0_INNER_L3TYPE_MASK		0x3U
#define	SQ_TASK_INFO0_VLAN_OFFLOAD_MASK		0x1U
#define	SQ_TASK_INFO0_PARSE_FLAG_MASK		0x1U
#define	SQ_TASK_INFO0_UFO_AVD_MASK		0x1U
#define SQ_TASK_INFO0_TSO_UFO_MASK		0x1U
#define SQ_TASK_INFO0_VLAN_TAG_MASK		0xFFFFU

#define SQ_TASK_INFO0_SET(val, member)			\
		(((u32)(val) & SQ_TASK_INFO0_##member##_MASK) <<	\
		SQ_TASK_INFO0_##member##_SHIFT)
#define SQ_TASK_INFO0_GET(val, member)			\
		(((val) >> SQ_TASK_INFO0_##member##_SHIFT) &	\
		SQ_TASK_INFO0_##member##_MASK)

#define	SQ_TASK_INFO1_MD_TYPE_SHIFT		8
#define SQ_TASK_INFO1_INNER_L4LEN_SHIFT		16
#define SQ_TASK_INFO1_INNER_L3LEN_SHIFT		24

#define	SQ_TASK_INFO1_MD_TYPE_MASK		0xFFU
#define SQ_TASK_INFO1_INNER_L4LEN_MASK		0xFFU
#define SQ_TASK_INFO1_INNER_L3LEN_MASK		0xFFU

#define SQ_TASK_INFO1_SET(val, member)			\
		(((val) & SQ_TASK_INFO1_##member##_MASK) <<	\
		SQ_TASK_INFO1_##member##_SHIFT)
#define SQ_TASK_INFO1_GET(val, member)			\
		(((val) >> SQ_TASK_INFO1_##member##_SHIFT) &	\
		SQ_TASK_INFO1_##member##_MASK)

#define SQ_TASK_INFO2_TUNNEL_L4LEN_SHIFT	0
#define SQ_TASK_INFO2_OUTER_L3LEN_SHIFT		8
#define SQ_TASK_INFO2_TUNNEL_L4TYPE_SHIFT	16
#define SQ_TASK_INFO2_OUTER_L3TYPE_SHIFT	24

#define SQ_TASK_INFO2_TUNNEL_L4LEN_MASK		0xFFU
#define SQ_TASK_INFO2_OUTER_L3LEN_MASK		0xFFU
#define SQ_TASK_INFO2_TUNNEL_L4TYPE_MASK	0x7U
#define SQ_TASK_INFO2_OUTER_L3TYPE_MASK		0x3U

#define SQ_TASK_INFO2_SET(val, member)			\
		(((val) & SQ_TASK_INFO2_##member##_MASK) <<	\
		SQ_TASK_INFO2_##member##_SHIFT)
#define SQ_TASK_INFO2_GET(val, member)			\
		(((val) >> SQ_TASK_INFO2_##member##_SHIFT) &	\
		SQ_TASK_INFO2_##member##_MASK)

#define	SQ_TASK_INFO4_L2TYPE_SHIFT			31

#define	SQ_TASK_INFO4_L2TYPE_MASK			0x1U

#define SQ_TASK_INFO4_SET(val, member)		\
		(((u32)(val) & SQ_TASK_INFO4_##member##_MASK) << \
		SQ_TASK_INFO4_##member##_SHIFT)

/********************* SQ_DB *********************/
#define SQ_DB_OFF						0x00000800
#define SQ_DB_INFO_HI_PI_SHIFT					0
#define SQ_DB_INFO_QID_SHIFT					8
#define SQ_DB_INFO_CFLAG_SHIFT					23
#define SQ_DB_INFO_COS_SHIFT					24
#define SQ_DB_INFO_TYPE_SHIFT					27
#define SQ_DB_INFO_HI_PI_MASK					0xFFU
#define SQ_DB_INFO_QID_MASK					0x3FFU
#define SQ_DB_INFO_CFLAG_MASK					0x1U
#define SQ_DB_INFO_COS_MASK					0x7U
#define SQ_DB_INFO_TYPE_MASK					0x1FU
#define SQ_DB_INFO_SET(val, member)			(((u32)(val) & \
					SQ_DB_INFO_##member##_MASK) << \
					SQ_DB_INFO_##member##_SHIFT)

#define SQ_DB_PI_LOW_MASK			0xFF
#define SQ_DB_PI_LOW(pi)			((pi) & SQ_DB_PI_LOW_MASK)
#define SQ_DB_PI_HI_SHIFT			8
#define SQ_DB_PI_HIGH(pi)			((pi) >> SQ_DB_PI_HI_SHIFT)
#define SQ_DB_ADDR(sq, pi)	\
	((u64 *)((sq)->db_addr + SQ_DB_OFF) + SQ_DB_PI_LOW(pi))
#define SQ_DB					1
#define SQ_CFLAG_DP				0	/* CFLAG_DATA_PATH */

/*********************** RQ_CTRL ******************/
#define	RQ_CTRL_BUFDESC_SECT_LEN_SHIFT		0
#define	RQ_CTRL_COMPLETE_FORMAT_SHIFT		15
#define RQ_CTRL_COMPLETE_LEN_SHIFT		27
#define RQ_CTRL_LEN_SHIFT			29

#define	RQ_CTRL_BUFDESC_SECT_LEN_MASK		0xFFU
#define	RQ_CTRL_COMPLETE_FORMAT_MASK		0x1U
#define RQ_CTRL_COMPLETE_LEN_MASK		0x3U
#define RQ_CTRL_LEN_MASK			0x3U

#define RQ_CTRL_SET(val, member)			(((val) & \
					RQ_CTRL_##member##_MASK) << \
					RQ_CTRL_##member##_SHIFT)

#define RQ_CTRL_GET(val, member)			(((val) >> \
					RQ_CTRL_##member##_SHIFT) & \
					RQ_CTRL_##member##_MASK)

#define RQ_CTRL_CLEAR(val, member)			((val) & \
					(~(RQ_CTRL_##member##_MASK << \
					RQ_CTRL_##member##_SHIFT)))

#define RQ_CQE_STATUS_CSUM_ERR_SHIFT		0
#define RQ_CQE_STATUS_NUM_LRO_SHIFT		16
#define RQ_CQE_STATUS_LRO_PUSH_SHIFT		25
#define RQ_CQE_STATUS_LRO_ENTER_SHIFT		26
#define RQ_CQE_STATUS_LRO_INTR_SHIFT		27

#define RQ_CQE_STATUS_BP_EN_SHIFT		30
#define RQ_CQE_STATUS_RXDONE_SHIFT		31
#define RQ_CQE_STATUS_FLUSH_SHIFT		28

#define RQ_CQE_STATUS_CSUM_ERR_MASK		0xFFFFU
#define RQ_CQE_STATUS_NUM_LRO_MASK		0xFFU
#define RQ_CQE_STATUS_LRO_PUSH_MASK		0X1U
#define RQ_CQE_STATUS_LRO_ENTER_MASK		0X1U
#define RQ_CQE_STATUS_LRO_INTR_MASK		0X1U
#define RQ_CQE_STATUS_BP_EN_MASK		0X1U
#define RQ_CQE_STATUS_RXDONE_MASK		0x1U
#define RQ_CQE_STATUS_FLUSH_MASK		0x1U

#define RQ_CQE_STATUS_GET(val, member)			(((val) >> \
					RQ_CQE_STATUS_##member##_SHIFT) & \
					RQ_CQE_STATUS_##member##_MASK)

#define RQ_CQE_STATUS_CLEAR(val, member)		((val) & \
					(~(RQ_CQE_STATUS_##member##_MASK << \
					RQ_CQE_STATUS_##member##_SHIFT)))

#define RQ_CQE_SGE_VLAN_SHIFT					0
#define RQ_CQE_SGE_LEN_SHIFT					16

#define RQ_CQE_SGE_VLAN_MASK					0xFFFFU
#define RQ_CQE_SGE_LEN_MASK					0xFFFFU

#define RQ_CQE_SGE_GET(val, member)			(((val) >> \
					RQ_CQE_SGE_##member##_SHIFT) & \
					RQ_CQE_SGE_##member##_MASK)

#define RQ_CQE_PKT_NUM_SHIFT				1
#define RQ_CQE_PKT_FIRST_LEN_SHIFT			19
#define RQ_CQE_PKT_LAST_LEN_SHIFT			6
#define RQ_CQE_SUPER_CQE_EN_SHIFT			0

#define RQ_CQE_PKT_FIRST_LEN_MASK			0x1FFFU
#define RQ_CQE_PKT_LAST_LEN_MASK			0x1FFFU
#define RQ_CQE_PKT_NUM_MASK				0x1FU
#define RQ_CQE_SUPER_CQE_EN_MASK			0x1

#define RQ_CQE_PKT_NUM_GET(val, member)			(((val) >> \
					RQ_CQE_PKT_##member##_SHIFT) & \
					RQ_CQE_PKT_##member##_MASK)
#define HINIC_GET_RQ_CQE_PKT_NUM(pkt_info) RQ_CQE_PKT_NUM_GET(pkt_info, NUM)

#define RQ_CQE_SUPER_CQE_EN_GET(val, member)	(((val) >> \
					RQ_CQE_##member##_SHIFT) & \
					RQ_CQE_##member##_MASK)
#define HINIC_GET_SUPER_CQE_EN(pkt_info)	\
	RQ_CQE_SUPER_CQE_EN_GET(pkt_info, SUPER_CQE_EN)

#define HINIC_GET_SUPER_CQE_EN_BE(pkt_info)	((pkt_info) & 0x1000000U)
#define RQ_CQE_PKT_LEN_GET(val, member)			(((val) >> \
						RQ_CQE_PKT_##member##_SHIFT) & \
						RQ_CQE_PKT_##member##_MASK)

#define RQ_CQE_OFFOLAD_TYPE_VLAN_EN_SHIFT		21
#define RQ_CQE_OFFOLAD_TYPE_VLAN_EN_MASK		0x1U

#define RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_SHIFT		0
#define RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_MASK		0xFFFU

#define RQ_CQE_OFFOLAD_TYPE_PKT_UMBCAST_SHIFT		19
#define RQ_CQE_OFFOLAD_TYPE_PKT_UMBCAST_MASK		0x3U

#define RQ_CQE_OFFOLAD_TYPE_RSS_TYPE_SHIFT		24
#define RQ_CQE_OFFOLAD_TYPE_RSS_TYPE_MASK		0xFFU

#define RQ_CQE_OFFOLAD_TYPE_GET(val, member)		(((val) >> \
				RQ_CQE_OFFOLAD_TYPE_##member##_SHIFT) & \
				RQ_CQE_OFFOLAD_TYPE_##member##_MASK)

#define RQ_CQE_PKT_TYPES_NON_L2_MASK				0x800U
#define RQ_CQE_PKT_TYPES_L2_MASK				0x7FU

#define RQ_CQE_STATUS_CSUM_BYPASS_VAL				0x80
#define RQ_CQE_STATUS_CSUM_ERR_IP_MASK				0x31U
#define RQ_CQE_STATUS_CSUM_ERR_L4_MASK				0x4EU

#define SECT_SIZE_BYTES(size)	((size) << 3)

#define HINIC_PF_SET_VF_ALREADY					0x4
#define HINIC_MGMT_STATUS_EXIST					0x6

#define WQS_BLOCKS_PER_PAGE		4

#define WQ_SIZE(wq)		(u32)((u64)(wq)->q_depth * (wq)->wqebb_size)

#define	WQE_PAGE_NUM(wq, idx)	(((idx) >> ((wq)->wqebbs_per_page_shift)) & \
				((wq)->num_q_pages - 1))

#define	WQE_PAGE_OFF(wq, idx)	((u64)((wq)->wqebb_size) * \
				((idx) & ((wq)->num_wqebbs_per_page - 1)))

#define WQ_PAGE_ADDR_SIZE		sizeof(u64)
#define WQ_PAGE_ADDR_SIZE_SHIFT 3
#define WQ_PAGE_ADDR(wq, idx)		\
		(u8 *)(*(u64 *)((u64)((wq)->shadow_block_vaddr) + \
		(WQE_PAGE_NUM(wq, idx) << WQ_PAGE_ADDR_SIZE_SHIFT)))

#define WQ_BLOCK_SIZE	4096UL
#define WQS_PAGE_SIZE	(WQS_BLOCKS_PER_PAGE * WQ_BLOCK_SIZE)
#define WQ_MAX_PAGES	(WQ_BLOCK_SIZE >> WQ_PAGE_ADDR_SIZE_SHIFT)

#define CMDQ_BLOCKS_PER_PAGE	8
#define CMDQ_BLOCK_SIZE		512UL
#define CMDQ_PAGE_SIZE		ALIGN((CMDQ_BLOCKS_PER_PAGE * \
					CMDQ_BLOCK_SIZE), PAGE_SIZE)

#define ADDR_4K_ALIGNED(addr)		(0 == (addr & 0xfff))
#define ADDR_256K_ALIGNED(addr)		(0 == (addr & 0x3ffff))

#define WQ_BASE_VADDR(wqs, wq)		\
		(u64 *)(((u64)((wqs)->page_vaddr[(wq)->page_idx])) \
				+ (wq)->block_idx * WQ_BLOCK_SIZE)

#define WQ_BASE_PADDR(wqs, wq)	(((wqs)->page_paddr[(wq)->page_idx]) \
				+ (u64)(wq)->block_idx * WQ_BLOCK_SIZE)

#define WQ_BASE_ADDR(wqs, wq)		\
		(u64 *)(((u64)((wqs)->shadow_page_vaddr[(wq)->page_idx])) \
				+ (wq)->block_idx * WQ_BLOCK_SIZE)

#define CMDQ_BASE_VADDR(cmdq_pages, wq)	\
			(u64 *)(((u64)((cmdq_pages)->cmdq_page_vaddr)) \
				+ (wq)->block_idx * CMDQ_BLOCK_SIZE)

#define CMDQ_BASE_PADDR(cmdq_pages, wq)	\
			(((u64)((cmdq_pages)->cmdq_page_paddr)) \
				+ (u64)(wq)->block_idx * CMDQ_BLOCK_SIZE)

#define CMDQ_BASE_ADDR(cmdq_pages, wq)	\
			(u64 *)(((u64)((cmdq_pages)->cmdq_shadow_page_vaddr)) \
				+ (wq)->block_idx * CMDQ_BLOCK_SIZE)

#define MASKED_WQE_IDX(wq, idx)	((idx) & (wq)->mask)

#define WQE_SHADOW_PAGE(wq, wqe)	\
		(u16)(((ulong)(wqe) - (ulong)(wq)->shadow_wqe) \
		/ (wq)->max_wqe_size)

#define WQE_IN_RANGE(wqe, start, end)	\
		(((ulong)(wqe) >= (ulong)(start)) && \
		((ulong)(wqe) < (ulong)(end)))

#define WQ_NUM_PAGES(num_wqs)	\
	(ALIGN((u32)num_wqs, WQS_BLOCKS_PER_PAGE) / WQS_BLOCKS_PER_PAGE)

/* Qe buffer relates define */

enum hinic_rx_buf_size {
	HINIC_RX_BUF_SIZE_32B = 0x20,
	HINIC_RX_BUF_SIZE_64B = 0x40,
	HINIC_RX_BUF_SIZE_96B = 0x60,
	HINIC_RX_BUF_SIZE_128B = 0x80,
	HINIC_RX_BUF_SIZE_192B = 0xC0,
	HINIC_RX_BUF_SIZE_256B = 0x100,
	HINIC_RX_BUF_SIZE_384B = 0x180,
	HINIC_RX_BUF_SIZE_512B = 0x200,
	HINIC_RX_BUF_SIZE_768B = 0x300,
	HINIC_RX_BUF_SIZE_1K = 0x400,
	HINIC_RX_BUF_SIZE_1_5K = 0x600,
	HINIC_RX_BUF_SIZE_2K = 0x800,
	HINIC_RX_BUF_SIZE_3K = 0xC00,
	HINIC_RX_BUF_SIZE_4K = 0x1000,
	HINIC_RX_BUF_SIZE_8K = 0x2000,
	HINIC_RX_BUF_SIZE_16K = 0x4000,
};

enum ppf_tmr_status {
	HINIC_PPF_TMR_FLAG_STOP,
	HINIC_PPF_TMR_FLAG_START,
};

enum hinic_res_state {
	HINIC_RES_CLEAN = 0,
	HINIC_RES_ACTIVE = 1,
};

#define DEFAULT_RX_BUF_SIZE	((u16)0xB)

#define BUF_DESC_SIZE_SHIFT			4

#define HINIC_SQ_WQE_SIZE(num_sge)		\
		(sizeof(struct hinic_sq_ctrl) + \
		sizeof(struct hinic_sq_task) +  \
		(u32)((num_sge) << BUF_DESC_SIZE_SHIFT))

#define HINIC_SQ_WQEBB_CNT(num_sge)	\
		(int)(ALIGN(HINIC_SQ_WQE_SIZE((u32)num_sge), \
			    HINIC_SQ_WQEBB_SIZE) >> HINIC_SQ_WQEBB_SHIFT)

#define HINIC_GET_RX_VLAN_OFFLOAD_EN(offload_type)	\
		RQ_CQE_OFFOLAD_TYPE_GET(offload_type, VLAN_EN)

#define HINIC_GET_RSS_TYPES(offload_type)	\
		RQ_CQE_OFFOLAD_TYPE_GET(offload_type, RSS_TYPE)

#define HINIC_GET_PKT_TYPES(offload_type)	\
		RQ_CQE_OFFOLAD_TYPE_GET(offload_type, PKT_TYPE)

#define HINIC_GET_RX_PKT_TYPE(offload_type)	\
		RQ_CQE_OFFOLAD_TYPE_GET(offload_type, PKT_TYPE)

#define HINIC_GET_RX_PKT_UMBCAST(offload_type)	\
		RQ_CQE_OFFOLAD_TYPE_GET(offload_type, PKT_UMBCAST)

#define HINIC_GET_RX_VLAN_TAG(vlan_len)	\
		RQ_CQE_SGE_GET(vlan_len, VLAN)

#define HINIC_GET_RX_PKT_LEN(vlan_len)	\
		RQ_CQE_SGE_GET(vlan_len, LEN)

#define HINIC_GET_RX_CSUM_ERR(status)	\
		RQ_CQE_STATUS_GET(status, CSUM_ERR)

#define HINIC_GET_RX_DONE(status)	\
		RQ_CQE_STATUS_GET(status, RXDONE)

#define HINIC_GET_RX_FLUSH(status)	\
		RQ_CQE_STATUS_GET(status, FLUSH)

#define HINIC_GET_RX_BP_EN(status)	\
		RQ_CQE_STATUS_GET(status, BP_EN)

#define HINIC_GET_RX_NUM_LRO(status)	\
		RQ_CQE_STATUS_GET(status, NUM_LRO)

#define HINIC_PKT_TYPES_UNKNOWN(pkt_types)	 \
	(pkt_types & RQ_CQE_PKT_TYPES_NON_L2_MASK)

#define HINIC_PKT_TYPES_L2(pkt_types)	 \
	(pkt_types & RQ_CQE_PKT_TYPES_L2_MASK)

#define HINIC_CSUM_ERR_BYPASSED(csum_err)	 \
	(csum_err == RQ_CQE_STATUS_CSUM_BYPASS_VAL)

#define HINIC_CSUM_ERR_IP(csum_err)	 \
	(csum_err & RQ_CQE_STATUS_CSUM_ERR_IP_MASK)

#define HINIC_CSUM_ERR_L4(csum_err)	 \
	(csum_err & RQ_CQE_STATUS_CSUM_ERR_L4_MASK)

#define TX_MSS_DEFAULT		0x3E00
#define TX_MSS_MIN		0x50

enum sq_wqe_type {
	SQ_NORMAL_WQE = 0,
};

enum rq_completion_fmt {
	RQ_COMPLETE_SGE = 1
};

#ifdef __cplusplus
    #if __cplusplus
}
    #endif
#endif /* __cplusplus */
#endif /* __HINIC_QE_DEF_H__ */
