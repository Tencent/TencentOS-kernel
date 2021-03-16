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

#ifndef __HINIC_CTX_DEF_H__
#define __HINIC_CTX_DEF_H__

#ifdef __cplusplus
    #if __cplusplus
extern "C"{
    #endif
#endif /* __cplusplus */

#define MASKED_SQ_IDX(sq, idx)			((idx) & (sq)->wq->mask)

#define HINIC_CEQE_QN_MASK			0x3FFU

#define HINIC_Q_CTXT_MAX				42

#define HINIC_RQ_CQ_MAX					128

#define MAX_WQE_SIZE(max_sge, wqebb_size)	\
			((max_sge <= 2) ? (wqebb_size) : \
			((ALIGN(((max_sge) - 2), 4) / 4 + 1) * (wqebb_size)))

/* performance: ci addr RTE_CACHE_SIZE(64B) alignment */
#define HINIC_CI_Q_ADDR_SIZE			(64)

#define CI_TABLE_SIZE(num_qps, pg_sz)	\
			(ALIGN((num_qps) * HINIC_CI_Q_ADDR_SIZE, pg_sz))

#define HINIC_CI_VADDR(base_addr, q_id)		((u8 *)(base_addr) + \
						(q_id) * HINIC_CI_Q_ADDR_SIZE)

#define HINIC_CI_PADDR(base_paddr, q_id)	((base_paddr) + \
						(q_id) * HINIC_CI_Q_ADDR_SIZE)

#define Q_CTXT_SIZE						48
#define TSO_LRO_CTXT_SIZE				240

#define SQ_CTXT_OFFSET(max_sqs, max_rqs, q_id)	\
			(((max_rqs) + (max_sqs)) * TSO_LRO_CTXT_SIZE \
			+ (q_id) * Q_CTXT_SIZE)

#define RQ_CTXT_OFFSET(max_sqs, max_rqs, q_id)	\
			(((max_rqs) + (max_sqs)) * TSO_LRO_CTXT_SIZE \
			+ (max_sqs) * Q_CTXT_SIZE + (q_id) * Q_CTXT_SIZE)

#define SQ_CTXT_SIZE(num_sqs)	((u16)(sizeof(struct hinic_qp_ctxt_header) \
				+ (num_sqs) * sizeof(struct hinic_sq_ctxt)))

#define RQ_CTXT_SIZE(num_rqs)	((u16)(sizeof(struct hinic_qp_ctxt_header) \
				+ (num_rqs) * sizeof(struct hinic_rq_ctxt)))

#define SQ_CTXT_CEQ_ATTR_CEQ_ID_SHIFT			8
#define SQ_CTXT_CEQ_ATTR_GLOBAL_SQ_ID_SHIFT		13
#define SQ_CTXT_CEQ_ATTR_EN_SHIFT				23
#define SQ_CTXT_CEQ_ATTR_ARM_SHIFT				31

#define SQ_CTXT_CEQ_ATTR_CEQ_ID_MASK			0x1FU
#define SQ_CTXT_CEQ_ATTR_GLOBAL_SQ_ID_MASK		0x3FFU
#define SQ_CTXT_CEQ_ATTR_EN_MASK					0x1U
#define SQ_CTXT_CEQ_ATTR_ARM_MASK				0x1U

#define SQ_CTXT_CEQ_ATTR_SET(val, member)		(((val) & \
					SQ_CTXT_CEQ_ATTR_##member##_MASK) \
					<< SQ_CTXT_CEQ_ATTR_##member##_SHIFT)

#define SQ_CTXT_CI_IDX_SHIFT						11
#define SQ_CTXT_CI_OWNER_SHIFT					23

#define SQ_CTXT_CI_IDX_MASK						0xFFFU
#define SQ_CTXT_CI_OWNER_MASK					0x1U

#define SQ_CTXT_CI_SET(val, member)			(((val) & \
					SQ_CTXT_CI_##member##_MASK) \
					<< SQ_CTXT_CI_##member##_SHIFT)

#define SQ_CTXT_WQ_PAGE_HI_PFN_SHIFT				0
#define SQ_CTXT_WQ_PAGE_PI_SHIFT					20

#define SQ_CTXT_WQ_PAGE_HI_PFN_MASK				0xFFFFFU
#define SQ_CTXT_WQ_PAGE_PI_MASK					0xFFFU

#define SQ_CTXT_WQ_PAGE_SET(val, member)		(((val) & \
					SQ_CTXT_WQ_PAGE_##member##_MASK) \
					<< SQ_CTXT_WQ_PAGE_##member##_SHIFT)

#define SQ_CTXT_PREF_CACHE_THRESHOLD_SHIFT		0
#define SQ_CTXT_PREF_CACHE_MAX_SHIFT				14
#define SQ_CTXT_PREF_CACHE_MIN_SHIFT				25

#define SQ_CTXT_PREF_CACHE_THRESHOLD_MASK		0x3FFFU
#define SQ_CTXT_PREF_CACHE_MAX_MASK				0x7FFU
#define SQ_CTXT_PREF_CACHE_MIN_MASK				0x7FU

#define SQ_CTXT_PREF_WQ_PFN_HI_SHIFT				0
#define SQ_CTXT_PREF_CI_SHIFT						20

#define SQ_CTXT_PREF_WQ_PFN_HI_MASK				0xFFFFFU
#define SQ_CTXT_PREF_CI_MASK						0xFFFU

#define SQ_CTXT_PREF_SET(val, member)			(((val) & \
					SQ_CTXT_PREF_##member##_MASK) \
					<< SQ_CTXT_PREF_##member##_SHIFT)

#define SQ_CTXT_WQ_BLOCK_PFN_HI_SHIFT			0

#define SQ_CTXT_WQ_BLOCK_PFN_HI_MASK			0x7FFFFFU

#define SQ_CTXT_WQ_BLOCK_SET(val, member)	(((val) & \
					SQ_CTXT_WQ_BLOCK_##member##_MASK) \
					<< SQ_CTXT_WQ_BLOCK_##member##_SHIFT)

#define RQ_CTXT_CEQ_ATTR_EN_SHIFT				0
#define RQ_CTXT_CEQ_ATTR_OWNER_SHIFT			1

#define RQ_CTXT_CEQ_ATTR_EN_MASK				0x1U
#define RQ_CTXT_CEQ_ATTR_OWNER_MASK			0x1U

#define RQ_CTXT_CEQ_ATTR_SET(val, member)		(((val) & \
					RQ_CTXT_CEQ_ATTR_##member##_MASK) \
					<< RQ_CTXT_CEQ_ATTR_##member##_SHIFT)

#define RQ_CTXT_PI_IDX_SHIFT						0
#define RQ_CTXT_PI_INTR_SHIFT						22
#define RQ_CTXT_PI_CEQ_ARM_SHIFT					31

#define RQ_CTXT_PI_IDX_MASK						0xFFFU
#define RQ_CTXT_PI_INTR_MASK						0x3FFU
#define RQ_CTXT_PI_CEQ_ARM_MASK					0x1U

#define RQ_CTXT_PI_SET(val, member)			(((val) & \
					RQ_CTXT_PI_##member##_MASK) << \
					RQ_CTXT_PI_##member##_SHIFT)

#define RQ_CTXT_WQ_PAGE_HI_PFN_SHIFT			0
#define RQ_CTXT_WQ_PAGE_CI_SHIFT					20

#define RQ_CTXT_WQ_PAGE_HI_PFN_MASK				0xFFFFFU
#define RQ_CTXT_WQ_PAGE_CI_MASK					0xFFFU

#define RQ_CTXT_WQ_PAGE_SET(val, member)		(((val) & \
					RQ_CTXT_WQ_PAGE_##member##_MASK) << \
					RQ_CTXT_WQ_PAGE_##member##_SHIFT)

#define RQ_CTXT_PREF_CACHE_THRESHOLD_SHIFT		0
#define RQ_CTXT_PREF_CACHE_MAX_SHIFT				14
#define RQ_CTXT_PREF_CACHE_MIN_SHIFT				25

#define RQ_CTXT_PREF_CACHE_THRESHOLD_MASK		0x3FFFU
#define RQ_CTXT_PREF_CACHE_MAX_MASK				0x7FFU
#define RQ_CTXT_PREF_CACHE_MIN_MASK				0x7FU

#define RQ_CTXT_PREF_WQ_PFN_HI_SHIFT				0
#define RQ_CTXT_PREF_CI_SHIFT						20

#define RQ_CTXT_PREF_WQ_PFN_HI_MASK				0xFFFFFU
#define RQ_CTXT_PREF_CI_MASK						0xFFFU

#define RQ_CTXT_PREF_SET(val, member)			(((val) & \
					RQ_CTXT_PREF_##member##_MASK) << \
					RQ_CTXT_PREF_##member##_SHIFT)

#define RQ_CTXT_WQ_BLOCK_PFN_HI_SHIFT			0

#define RQ_CTXT_WQ_BLOCK_PFN_HI_MASK			0x7FFFFFU

#define RQ_CTXT_WQ_BLOCK_SET(val, member)		(((val) & \
					RQ_CTXT_WQ_BLOCK_##member##_MASK) << \
					RQ_CTXT_WQ_BLOCK_##member##_SHIFT)

#define SIZE_16BYTES(size)		(ALIGN((size), 16) >> 4)

#define	WQ_PAGE_PFN_SHIFT						12
#define	WQ_BLOCK_PFN_SHIFT						9

#define WQ_PAGE_PFN(page_addr)		((page_addr) >> WQ_PAGE_PFN_SHIFT)
#define WQ_BLOCK_PFN(page_addr)		((page_addr) >> WQ_BLOCK_PFN_SHIFT)

enum sq_cflag {
	CFLAG_DATA_PATH = 0,
};

enum hinic_qp_ctxt_type {
	HINIC_QP_CTXT_TYPE_SQ,
	HINIC_QP_CTXT_TYPE_RQ,
};

/* service type relates define */
enum cfg_svc_type_en {
	CFG_SVC_NIC_BIT0    = (1 << 0),
	CFG_SVC_ROCE_BIT1   = (1 << 1),
	CFG_SVC_FCOE_BIT2   = (1 << 2),
	CFG_SVC_TOE_BIT3    = (1 << 3),
	CFG_SVC_IWARP_BIT4  = (1 << 4),
	CFG_SVC_FC_BIT5     = (1 << 5),

	CFG_SVC_FIC_BIT6    = (1 << 6),
	CFG_SVC_OVS_BIT7    = (1 << 7),
	CFG_SVC_ACL_BIT8    = (1 << 8), /* used for 8PF ovs in up */
	CFG_SVC_IOE_BIT9    = (1 << 9),
	CFG_SVC_HWPT_BIT10  = (1 << 10),

	CFG_SVC_FT_EN       = (CFG_SVC_FCOE_BIT2 | CFG_SVC_TOE_BIT3 |
			       CFG_SVC_FC_BIT5 | CFG_SVC_IOE_BIT9),
	CFG_SVC_RDMA_EN     = (CFG_SVC_ROCE_BIT1 | CFG_SVC_IWARP_BIT4)
};

#define IS_NIC_TYPE(dev) \
	((dev)->cfg_mgmt->svc_cap.chip_svc_type & CFG_SVC_NIC_BIT0)
#define IS_ROCE_TYPE(dev) \
	((dev)->cfg_mgmt->svc_cap.chip_svc_type & CFG_SVC_ROCE_BIT1)
#define IS_FCOE_TYPE(dev) \
	((dev)->cfg_mgmt->svc_cap.chip_svc_type & CFG_SVC_FCOE_BIT2)
#define IS_TOE_TYPE(dev) \
	((dev)->cfg_mgmt->svc_cap.chip_svc_type & CFG_SVC_TOE_BIT3)
#define IS_IWARP_TYPE(dev) \
	((dev)->cfg_mgmt->svc_cap.chip_svc_type & CFG_SVC_IWARP_BIT4)
#define IS_FC_TYPE(dev) \
	((dev)->cfg_mgmt->svc_cap.chip_svc_type & CFG_SVC_FC_BIT5)
#define IS_FIC_TYPE(dev) \
	((dev)->cfg_mgmt->svc_cap.chip_svc_type & CFG_SVC_FIC_BIT6)
#define IS_OVS_TYPE(dev) \
	((dev)->cfg_mgmt->svc_cap.chip_svc_type & CFG_SVC_OVS_BIT7)
#define IS_ACL_TYPE(dev) \
	((dev)->cfg_mgmt->svc_cap.chip_svc_type & CFG_SVC_ACL_BIT8)
#define IS_IOE_TYPE(dev) \
	((dev)->cfg_mgmt->svc_cap.chip_svc_type & CFG_SVC_IOE_BIT9)
#define IS_FT_TYPE(dev) \
	((dev)->cfg_mgmt->svc_cap.chip_svc_type & CFG_SVC_FT_EN)
#define IS_RDMA_TYPE(dev) \
	((dev)->cfg_mgmt->svc_cap.chip_svc_type & CFG_SVC_RDMA_EN)
#define IS_HWPT_TYPE(dev) \
	((dev)->cfg_mgmt->svc_cap.chip_svc_type & CFG_SVC_HWPT_BIT10)

#ifdef __cplusplus
    #if __cplusplus
}
    #endif
#endif /* __cplusplus */
#endif /* __HINIC_CTX_DEF_H__ */
