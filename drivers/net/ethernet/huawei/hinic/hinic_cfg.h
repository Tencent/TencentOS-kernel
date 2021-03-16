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

#ifndef __CFG_MGT_H__
#define __CFG_MGT_H__

#include "hinic_ctx_def.h"

enum {
	CFG_FREE = 0,
	CFG_BUSY = 1
};

/* start position for CEQs allocation, Max number of CEQs is 32 */
enum {
	CFG_RDMA_CEQ_BASE       = 0
};

enum {
	CFG_NET_MODE_ETH = 0, /* Eth */
	CFG_NET_MODE_FIC = 1, /* FIC */
	CFG_NET_MODE_FC = 2   /* FC */
};

enum {
	SF_SVC_FT_BIT = (1 << 0),
	SF_SVC_RDMA_BIT = (1 << 1),
};

/* RDMA resource */
#define K_UNIT              BIT(10)
#define M_UNIT              BIT(20)
#define G_UNIT              BIT(30)

/* number of PFs and VFs */
#define HOST_PF_NUM         4
#define HOST_VF_NUM         0
#define HOST_OQID_MASK_VAL  2

/* L2NIC */
#define L2NIC_SQ_DEPTH      (4 * K_UNIT)
#define L2NIC_RQ_DEPTH      (4 * K_UNIT)

#define HINIC_CFG_MAX_QP	64

/* RDMA */
#define RDMA_RSVD_QPS       2
#define ROCE_MAX_WQES       (16 * K_UNIT - 1)
#define IWARP_MAX_WQES      (8 * K_UNIT)

#define RDMA_MAX_SQ_SGE     8

#define ROCE_MAX_RQ_SGE     8
#define IWARP_MAX_RQ_SGE    2

#define RDMA_MAX_SQ_DESC_SZ (1 * K_UNIT)

/* (256B(cache_line_len) - 16B(ctrl_seg_len) - 64B(max_task_seg_len)) */
#define ROCE_MAX_SQ_INLINE_DATA_SZ   192

#define IWARP_MAX_SQ_INLINE_DATA_SZ  108

#define ROCE_MAX_RQ_DESC_SZ     128
#define IWARP_MAX_RQ_DESC_SZ    64

#define IWARP_MAX_IRQ_DEPTH     1024
#define IWARP_IRQ_ENTRY_SZ      64

#define IWARP_MAX_ORQ_DEPTH     1024
#define IWARP_ORQ_ENTRY_SZ      32

#define IWARP_MAX_RTOQ_DEPTH    1024
#define IWARP_RTOQ_ENTRY_SZ     32

#define IWARP_MAX_ACKQ_DEPTH    1024
#define IWARP_ACKQ_ENTRY_SZ     16

#define ROCE_QPC_ENTRY_SZ       512
#define IWARP_QPC_ENTRY_SZ      1024

#define WQEBB_SZ                64

#define ROCE_RDMARC_ENTRY_SZ    32
#define ROCE_MAX_QP_INIT_RDMA   128
#define ROCE_MAX_QP_DEST_RDMA   128

#define ROCE_MAX_SRQ_WQES       (16 * K_UNIT - 1)
#define ROCE_RSVD_SRQS          0
#define ROCE_MAX_SRQ_SGE        7
#define ROCE_SRQC_ENTERY_SZ     64

#define RDMA_MAX_CQES       (64 * K_UNIT - 1)
#define RDMA_RSVD_CQS       0

#define RDMA_CQC_ENTRY_SZ   128

#define RDMA_CQE_SZ         32
#define RDMA_RSVD_MRWS      128
#define RDMA_MPT_ENTRY_SZ   64
#define RDMA_NUM_MTTS       (1 * G_UNIT)
#define LOG_MTT_SEG         5
#define MTT_ENTRY_SZ        8
#define LOG_RDMARC_SEG      3

#define LOCAL_ACK_DELAY     15
#define RDMA_NUM_PORTS      1
#define ROCE_MAX_MSG_SZ     (2 * G_UNIT)
#define IWARP_MAX_MSG_SZ    (1 * G_UNIT)

#define DB_PAGE_SZ          (4 * K_UNIT)
#define DWQE_SZ             256

#define NUM_PD              (128 * K_UNIT)
#define RSVD_PD             0

#define MAX_XRCDS           (64 * K_UNIT)
#define RSVD_XRCDS          0

#define MAX_GID_PER_PORT    16
#define GID_ENTRY_SZ        32
#define RSVD_LKEY           ((RDMA_RSVD_MRWS - 1) << 8)
#define NUM_COMP_VECTORS    32
#define PAGE_SZ_CAP         ((1UL << 12) | (1UL << 13) | (1UL << 14) | \
			     (1UL << 16) | (1UL << 18) | (1UL << 20) | \
			     (1UL << 22))
#define ROCE_MODE           1

#define MAX_FRPL_LEN        511
#define MAX_PKEYS           1

/* FCoE */
#define FCOE_PCTX_SZ        256
#define FCOE_CCTX_SZ        256
#define FCOE_SQE_SZ         128
#define FCOE_SCQC_SZ        64
#define FCOE_SCQE_SZ        64
#define FCOE_SRQC_SZ        64
#define FCOE_SRQE_SZ        32

/* ToE */
#define TOE_PCTX_SZ         1024
#define TOE_CQC_SZ          64

/* IoE */
#define IOE_PCTX_SZ         512

/* FC */
#define FC_PCTX_SZ          256
#define FC_CCTX_SZ          256
#define FC_SQE_SZ           128
#define FC_SCQC_SZ          64
#define FC_SCQE_SZ          64
#define FC_SRQC_SZ          64
#define FC_SRQE_SZ          32

/* OVS */
#define OVS_PCTX_SZ         256
#define OVS_SCQC_SZ         64

/* ACL */
#define ACL_PCTX_SZ		512
#define ACL_SCQC_SZ		64

struct dev_sf_svc_attr {
	bool ft_en;     /* business enable flag (not include RDMA) */
	bool ft_pf_en;  /* In FPGA Test VF resource is in PF or not,
			 * 0 - VF, 1 - PF, VF doesn't need this bit.
			 */
	bool rdma_en;
	bool rdma_pf_en;/* In FPGA Test VF RDMA resource is in PF or not,
			 * 0 - VF, 1 - PF, VF doesn't need this bit.
			 */
	u8 sf_en_vf;    /* SF_EN for PPF/PF's VF */
};

struct host_shared_resource_cap {
	u32 host_pctxs; /* Parent Context max 1M, IOE and FCoE max 8K flows */
	u32 host_cctxs; /* Child Context: max 8K */
	u32 host_scqs;  /* shared CQ, chip interface module uses 1 SCQ
			 * TOE/IOE/FCoE each uses 1 SCQ
			 * RoCE/IWARP uses multiple SCQs
			 * So 6 SCQ least
			 */
	u32 host_srqs; /* SRQ number: 256K */
	u32 host_mpts; /* MR number:1M */
};

/* device capability */
struct service_cap {
	struct dev_sf_svc_attr sf_svc_attr;
	enum cfg_svc_type_en svc_type;      /* user input service type */
	enum cfg_svc_type_en chip_svc_type; /* HW supported service type */

	/* Host global resources */
	u16 host_total_function;
	u8 host_oq_id_mask_val;
	u8 host_id;
	u8 ep_id;
	/* DO NOT get interrupt_type from firmware */
	enum intr_type interrupt_type;
	u8 intr_chip_en;
	u8 max_cos_id;  /* PF/VF's max cos id */
	u8 cos_valid_bitmap;
	u8 er_id;       /* PF/VF's ER */
	u8 port_id;     /* PF/VF's physical port */
	u8 max_vf;      /* max VF number that PF supported */
	u8 force_up;
	bool sf_en;     /* stateful business status */
	u8 timer_en;    /* 0:disable, 1:enable */
	u8 bloomfilter_en; /* 0:disable, 1:enable*/
	u16 max_sqs;
	u16 max_rqs;

	/* For test */
	u32 test_qpc_num;
	u32 test_qpc_resvd_num;
	u32 test_page_size_reorder;
	bool test_xid_alloc_mode;
	bool test_gpa_check_enable;
	u8 test_qpc_alloc_mode;
	u8 test_scqc_alloc_mode;

	u32 test_max_conn_num;
	u32 test_max_cache_conn_num;
	u32 test_scqc_num;
	u32 test_mpt_num;
	u32 test_scq_resvd_num;
	u32 test_mpt_recvd_num;
	u32 test_hash_num;
	u32 test_reorder_num;

	u32 max_connect_num; /* PF/VF maximum connection number(1M) */
	/* The maximum connections which can be stick to cache memory, max 1K */
	u16 max_stick2cache_num;
	/* Starting address in cache memory for bloom filter, 64Bytes aligned */
	u16 bfilter_start_addr;
	/* Length for bloom filter, aligned on 64Bytes. The size is length*64B.
	 * Bloom filter memory size + 1 must be power of 2.
	 * The maximum memory size of bloom filter is 4M
	 */
	u16 bfilter_len;
	/* The size of hash bucket tables, align on 64 entries.
	 * Be used to AND (&) the hash value. Bucket Size +1 must be power of 2.
	 * The maximum number of hash bucket is 4M
	 */
	u16 hash_bucket_num;
	u8 net_port_mode; /* 0:ETH,1:FIC,2:4FC */

	u32 pf_num;
	u32 pf_id_start;
	u32 vf_num;	/* max numbers of vf in current host */
	u32 vf_id_start;

	struct host_shared_resource_cap shared_res_cap; /* shared capability */
	struct dev_version_info     dev_ver_info;       /* version */
	struct nic_service_cap      nic_cap;            /* NIC capability */
	struct rdma_service_cap     rdma_cap;           /* RDMA capability */
	struct fcoe_service_cap     fcoe_cap;           /* FCoE capability */
	struct toe_service_cap      toe_cap;            /* ToE capability */
	struct fc_service_cap       fc_cap;             /* FC capability */
	struct ovs_service_cap      ovs_cap;            /* OVS capability */
	struct acl_service_cap      acl_cap;            /* ACL capability */
};

struct cfg_eq {
	enum hinic_service_type type;
	int eqn;
	int free; /* 1 - alocated, 0- freed */
};

struct cfg_eq_info {
	struct cfg_eq *eq;

	u8 num_ceq;
	u8 num_ceq_remain;

	/* mutex used for allocate EQs */
	struct mutex eq_mutex;
};

struct irq_alloc_info_st {
	enum hinic_service_type type;
	int free;                /* 1 - alocated, 0- freed */
	struct irq_info info;
};

struct cfg_irq_info {
	struct irq_alloc_info_st *alloc_info;
	u16 num_total;
	u16 num_irq_remain;
	u16 num_irq_hw;          /* device max irq number */

	/* mutex used for allocate EQs */
	struct mutex irq_mutex;
};

#define VECTOR_THRESHOLD	2

struct cfg_mgmt_info {
	struct hinic_hwdev *hwdev;
	struct service_cap  svc_cap;
	struct cfg_eq_info  eq_info;        /* EQ */
	struct cfg_irq_info irq_param_info; /* IRQ */
	u32 func_seq_num;                   /* temporary */
};

enum cfg_sub_cmd {
	/* PPF(PF) <-> FW */
	HINIC_CFG_NIC_CAP = 0,
	CFG_FW_VERSION,
	CFG_UCODE_VERSION,
	HINIC_CFG_FUNC_CAP,
	HINIC_CFG_MBOX_CAP = 6,
};

struct hinic_dev_cap {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	/* Public resource */
	u8 sf_svc_attr;
	u8 host_id;
	u8 sf_en_pf;
	u8 sf_en_vf;

	u8 ep_id;
	u8 intr_type;
	u8 max_cos_id;
	u8 er_id;
	u8 port_id;
	u8 max_vf;
	u16 svc_cap_en;
	u16 host_total_func;
	u8 host_oq_id_mask_val;
	u8 max_vf_cos_id;

	u32 max_conn_num;
	u16 max_stick2cache_num;
	u16 max_bfilter_start_addr;
	u16 bfilter_len;
	u16 hash_bucket_num;
	u8 cfg_file_ver;
	u8 net_port_mode;
	u8 valid_cos_bitmap;	/* every bit indicate cos is valid */
	u8 force_up;
	u32 pf_num;
	u32 pf_id_start;
	u32 vf_num;
	u32 vf_id_start;

	/* shared resource */
	u32 host_pctx_num;
	u8 host_sf_en;
	u8 rsvd2[3];
	u32 host_ccxt_num;
	u32 host_scq_num;
	u32 host_srq_num;
	u32 host_mpt_num;

	/* l2nic */
	u16 nic_max_sq;
	u16 nic_max_rq;
	u16 nic_vf_max_sq;
	u16 nic_vf_max_rq;
	u8 nic_lro_en;
	u8 nic_lro_sz;
	u8 nic_tso_sz;
	u8 max_queue_allowed;

	/* RoCE */
	u32 roce_max_qp;
	u32 roce_max_cq;
	u32 roce_max_srq;
	u32 roce_max_mpt;

	u32 roce_vf_max_qp;
	u32 roce_vf_max_cq;
	u32 roce_vf_max_srq;
	u32 roce_vf_max_mpt;

	u32 roce_cmtt_cl_start;
	u32 roce_cmtt_cl_end;
	u32 roce_cmtt_cl_size;

	u32 roce_dmtt_cl_start;
	u32 roce_dmtt_cl_end;
	u32 roce_dmtt_cl_size;

	u32 roce_wqe_cl_start;
	u32 roce_wqe_cl_end;
	u32 roce_wqe_cl_size;

	/* IWARP */
	u32 iwarp_max_qp;
	u32 iwarp_max_cq;
	u32 iwarp_max_mpt;

	u32 iwarp_vf_max_qp;
	u32 iwarp_vf_max_cq;
	u32 iwarp_vf_max_mpt;

	u32 iwarp_cmtt_cl_start;
	u32 iwarp_cmtt_cl_end;
	u32 iwarp_cmtt_cl_size;

	u32 iwarp_dmtt_cl_start;
	u32 iwarp_dmtt_cl_end;
	u32 iwarp_dmtt_cl_size;

	u32 iwarp_wqe_cl_start;
	u32 iwarp_wqe_cl_end;
	u32 iwarp_wqe_cl_size;

	/* FCoE */
	u32 fcoe_max_qp;
	u32 fcoe_max_cq;
	u32 fcoe_max_srq;

	u32 fcoe_max_cctx;
	u32 fcoe_cctx_id_start;

	u8 fcoe_vp_id_start;
	u8 fcoe_vp_id_end;
	u8 rsvd4[2];

	/* OVS */
	u32 ovs_max_qpc;
	u8  ovs_dq_en;
	u8  rsvd5[3];

	/* ToE */
	u32 toe_max_pctx;
	u32 toe_max_cq;
	u32 toe_max_srq;
	u32 toe_srq_id_start;

	/* FC */
	u32 fc_max_pctx;
	u32 fc_max_scq;
	u32 fc_max_srq;

	u32 fc_max_cctx;
	u32 fc_cctx_id_start;

	u8 fc_vp_id_start;
	u8 fc_vp_id_end;
	u16 func_id;
};

#define VSW_UP_CFG_TIMEOUT      (0xFF00000)

#define VSW_SET_STATEFUL_BITS_TOE(flag)       \
	((flag) << VSW_STATEFUL_TOE_EN)
#define VSW_SET_STATEFUL_BITS_FCOE(flag)      \
	((flag) << VSW_STATEFUL_FCOE_EN)
#define VSW_SET_STATEFUL_BITS_IWARP(flag)     \
	((flag) << VSW_STATEFUL_IWARP_EN)
#define VSW_SET_STATEFUL_BITS_ROCE(flag)      \
	((flag) << VSW_STATEFUL_ROCE_EN)

#define VSW_GET_STATEFUL_BITS_TOE(flag)       \
	((bool)(((flag) >> VSW_STATEFUL_TOE_EN) & 0x1U))
#define VSW_GET_STATEFUL_BITS_FCOE(flag)      \
	((bool)(((flag) >> VSW_STATEFUL_FCOE_EN) & 0x1U))
#define VSW_GET_STATEFUL_BITS_IWARP(flag)     \
	((bool)(((flag) >> VSW_STATEFUL_IWARP_EN) & 0x1U))
#define VSW_GET_STATEFUL_BITS_ROCE(flag)      \
	((bool)(((flag) >> VSW_STATEFUL_ROCE_EN) & 0x1U))

enum tag_vsw_major_cmd {
	VSW_MAJOR_MISC = 10,    /* 0~9 reserved for driver */
	VSW_MAJOR_L2SWITCH,
	VSW_MAJOR_L2MULTICAST,
	VSW_MAJOR_QOS,
	VSW_MAJOR_PKTSUPS,
	VSW_MAJOR_VLANFILTER,
	VSW_MAJOR_MACFILTER,
	VSW_MAJOR_IPFILTER,
	VSW_MAJOR_VLANMAPPING,
	VSW_MAJOR_ETHTRUNK,
	VSW_MAJOR_MIRROR,
	VSW_MAJOR_DFX,
	VSW_MAJOR_ACL,
};

enum tag_vsw_minor_misc_cmd {
	VSW_MINOR_MISC_INIT_FUNC = 0,
	VSW_MINOR_MISC_SET_FUNC_SF_ENBITS,
	VSW_MINOR_MISC_GET_FUNC_SF_ENBITS,
	VSW_MINOR_MISC_CMD_MAX,
};

/* vswitch eth-trunk sub-command */
enum tag_nic_stateful_enbits {
	VSW_STATEFUL_TOE_EN   = 0,
	VSW_STATEFUL_FCOE_EN  = 1,
	VSW_STATEFUL_IWARP_EN = 2,
	VSW_STATEFUL_ROCE_EN  = 3,
};

/* function stateful enable parameters */
struct nic_misc_func_sf_enbits {
	u8  status;
	u8  version;
	u8  rsvd0[6];
	u32 function_id;
	u32 stateful_enbits; /* b0:toe, b1:fcoe, b2:iwarp, b3:roce */
	u32 stateful_enmask; /* b0:toe, b1:fcoe, b2:iwarp, b3:roce */
};

#endif
