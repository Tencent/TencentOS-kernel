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

#ifndef HINIC_HW_MGMT_H_
#define HINIC_HW_MGMT_H_

/* show each drivers only such as nic_service_cap,
 * toe_service_cap structure, but not show service_cap
 */
enum hinic_service_type {
	SERVICE_T_NIC = 0,
	SERVICE_T_OVS,
	SERVICE_T_ROCE,
	SERVICE_T_TOE,
	SERVICE_T_IWARP,
	SERVICE_T_FC,
	SERVICE_T_FCOE,
	SERVICE_T_MIGRATE,
	SERVICE_T_PT,
	SERVICE_T_HWPT,
	SERVICE_T_MAX,

	/* Only used for interruption resource management,
	 * mark the request module
	 */
	SERVICE_T_INTF   = (1 << 15),
	SERVICE_T_CQM    = (1 << 16),
};

/*  NIC service capability
 *  1, The chip supports NIC RQ is 1K
 *  2, PF/VF RQ specifications:
 *   disable RSS:
 *	 disable VMDq: Each PF/VF at most 8 RQ
 *	 enable the VMDq: Each PF/VF at most 1K RQ
 *   enable the RSS:
 *	 disable VMDq: each PF at most 64 RQ, VF at most 32 RQ
 *	 enable the VMDq: Each PF/VF at most 1K RQ
 *
 *  3, The chip supports NIC SQ is 1K
 *  4, PF/VF SQ specifications:
 *   disable RSS:
 *	 disable VMDq: Each PF/VF at most 8 SQ
 *	 enable the VMDq: Each PF/VF at most 1K SQ
 *   enable the RSS:
 *	 disable VMDq: each PF at most 64 SQ, VF at most 32 SQ
 *	 enable the VMDq: Each PF/VF at most 1K SQ
 */
struct nic_service_cap {
	/* PF resources */
	u16 max_sqs;
	u16 max_rqs;

	/* VF resources, vf obtain through the MailBox mechanism from
	 * according PF
	 */
	u16 vf_max_sqs;
	u16 vf_max_rqs;
	bool lro_en;    /* LRO feature enable bit */
	u8 lro_sz;      /* LRO context space: n*16B */
	u8 tso_sz;      /* TSO context space: n*16B */

	u16 max_queue_allowed;
	u16 dynamic_qp; /* support dynamic queue */
};

struct dev_roce_svc_own_cap {
	u32 max_qps;
	u32 max_cqs;
	u32 max_srqs;
	u32 max_mpts;

	u32 vf_max_qps;
	u32 vf_max_cqs;
	u32 vf_max_srqs;
	u32 vf_max_mpts;

	u32 cmtt_cl_start;
	u32 cmtt_cl_end;
	u32 cmtt_cl_sz;

	u32 dmtt_cl_start;
	u32 dmtt_cl_end;
	u32 dmtt_cl_sz;

	u32 wqe_cl_start;
	u32 wqe_cl_end;
	u32 wqe_cl_sz;

	u32 qpc_entry_sz;
	u32 max_wqes;
	u32 max_rq_sg;
	u32 max_sq_inline_data_sz;
	u32 max_rq_desc_sz;

	u32 rdmarc_entry_sz;
	u32 max_qp_init_rdma;
	u32 max_qp_dest_rdma;

	u32 max_srq_wqes;
	u32 reserved_srqs;
	u32 max_srq_sge;
	u32 srqc_entry_sz;

	u32 max_msg_sz; /* Message size 2GB */

	u8 num_cos;
};

struct dev_iwarp_svc_own_cap {
	u32 max_qps;
	u32 max_cqs;
	u32 max_mpts;

	u32 vf_max_qps;
	u32 vf_max_cqs;
	u32 vf_max_mpts;

	u32 cmtt_cl_start;
	u32 cmtt_cl_end;
	u32 cmtt_cl_sz;

	u32 dmtt_cl_start;
	u32 dmtt_cl_end;
	u32 dmtt_cl_sz;

	u32 wqe_cl_start;
	u32 wqe_cl_end;
	u32 wqe_cl_sz;

	u32 max_rq_sg;
	u32 max_sq_inline_data_sz;
	u32 max_rq_desc_sz;

	u32 max_irq_depth;
	u32 irq_entry_size;    /* 64B */
	u32 max_orq_depth;
	u32 orq_entry_size;    /* 32B */
	u32 max_rtoq_depth;
	u32 rtoq_entry_size;   /* 32B */
	u32 max_ackq_depth;
	u32 ackq_entry_size;   /* 16B */

	u32 max_msg_sz;        /* Message size 1GB */

	u32 max_wqes;           /* 8K */
	u32 qpc_entry_sz;       /* 1K */

	/* true:CQM uses static allocation;
	 * false:CQM uses dynamic allocation.
	 * Currently, only consider the QPC
	 */
	bool alloc_flag;

	u8 num_cos;
};

/* RDMA service capability structure */
struct dev_rdma_svc_cap {
	/* ROCE service unique parameter structure */
	struct dev_roce_svc_own_cap  roce_own_cap;
	/* IWARP service unique parameter structure */
	struct dev_iwarp_svc_own_cap  iwarp_own_cap;
};

/* Defines the RDMA service capability flag */
enum {
	RDMA_BMME_FLAG_LOCAL_INV        = (1 << 0),
	RDMA_BMME_FLAG_REMOTE_INV       = (1 << 1),
	RDMA_BMME_FLAG_FAST_REG_WR      = (1 << 2),
	RDMA_BMME_FLAG_RESERVED_LKEY    = (1 << 3),
	RDMA_BMME_FLAG_TYPE_2_WIN       = (1 << 4),
	RDMA_BMME_FLAG_WIN_TYPE_2B      = (1 << 5),

	RDMA_DEV_CAP_FLAG_XRC           = (1 << 6),
	RDMA_DEV_CAP_FLAG_MEM_WINDOW    = (1 << 7),
	RDMA_DEV_CAP_FLAG_ATOMIC        = (1 << 8),
	RDMA_DEV_CAP_FLAG_APM           = (1 << 9),
};

/* RDMA services */
struct rdma_service_cap {
	struct dev_rdma_svc_cap     dev_rdma_cap;

	u8 log_mtt;	/* 1. the number of MTT PA must be integer power of 2
			 * 2. represented by logarithm. Each MTT table can
			 * contain 1, 2, 4, 8, and 16 PA)
			 */
	u8 log_rdmarc;	/* 1. the number of RDMArc PA must be integer power of 2
			 * 2. represented by logarithm. Each MTT table can
			 * contain 1, 2, 4, 8, and 16 PA)
			 */

	u32 reserved_qps;	/* Number of reserved QP */
	u32 max_sq_sg;		/* Maximum SGE number of SQ (8) */
	u32 max_sq_desc_sz;	/* WQE maximum size of SQ(1024B), inline maximum
				 * size if 960B(944B aligned to the 960B),
				 * 960B=>wqebb alignment=>1024B
				 */
	u32 wqebb_size;		/* Currently, the supports 64B and 128B,
				 * defined as 64Bytes
				 */

	u32 max_cqes;		/* Size of the depth of the CQ (64K-1) */
	u32 reserved_cqs;	/* Number of reserved CQ */
	u32 cqc_entry_sz;	/* Size of the CQC (64B/128B) */
	u32 cqe_size;		/* Size of CQE (32B) */

	u32 reserved_mrws;	/* Number of reserved MR/MR Window */
	u32 mpt_entry_sz;	/* MPT table size (64B) */
	u32 max_fmr_maps;	/* max MAP of FMR,
				 * (1 << (32-ilog2(num_mpt)))-1;
				 */

	u32 num_mtts;		/* Number of MTT table (4M),
				 * is actually MTT seg number
				 */
	/* MTT table number of Each MTT seg(3) */
	u32 log_mtt_seg;
	u32 mtt_entry_sz;      /* MTT table size 8B, including 1 PA(64bits) */
	u32 log_rdmarc_seg;    /* table number of each RDMArc seg(3) */

	/* Timeout time. Formula:Tr=4.096us*2(local_ca_ack_delay), [Tr,4Tr] */
	u32 local_ca_ack_delay;
	u32 num_ports;		/* Physical port number */

	u32 db_page_size;	/* Size of the DB (4KB) */
	u32 direct_wqe_size;	/* Size of the DWQE (256B) */

	u32 num_pds;		/* Maximum number of PD (128K) */
	u32 reserved_pds;	/* Number of reserved PD*/
	u32 max_xrcds;		/* Maximum number of xrcd (64K) */
	u32 reserved_xrcds;	/* Number of reserved xrcd */

	u32 max_gid_per_port;	/* gid number (16) of each port */
	u32 gid_entry_sz;	/* RoCE v2 GID table is 32B,
				 * compatible RoCE v1 expansion
				 */

	u32 reserved_lkey;	/* local_dma_lkey */
	u32 num_comp_vectors;	/* Number of complete vector (32) */
	u32 page_size_cap;	/* Supports 4K,8K,64K,256K,1M,4M page_size */

	u32 flags;		/* RDMA some identity */
	u32 max_frpl_len;	/* Maximum number of pages frmr registration */
	u32 max_pkeys;		/* Number of supported pkey group */
};

/* PF/VF FCoE service resource structure defined */
struct dev_fcoe_svc_cap {
	/* PF resources */
	u32 max_qps;
	u32 max_cqs;
	u32 max_srqs;

	/* Child Context(Task IO)
	 * For FCoE/IOE services, at most 8K
	 */
	u32 max_cctxs;
	u32 cctxs_id_start;

	u8 vp_id_start;
	u8 vp_id_end;
};

/* FCoE services */
struct fcoe_service_cap {
	struct dev_fcoe_svc_cap     dev_fcoe_cap;

	/* SQ */
	u32 qpc_basic_size;
	u32 childc_basic_size;
	u32 sqe_size;

	/* SCQ */
	u32 scqc_basic_size;
	u32 scqe_size;

	/* SRQ */
	u32 srqc_size;
	u32 srqe_size;
};

/* PF/VF ToE service resource structure */
struct dev_toe_svc_cap {
	/* PF resources*/
	u32 max_pctxs; /* Parent Context: max specifications 1M */
	u32 max_cqs;
	u32 max_srqs;
	u32 srq_id_start;

	u8 num_cos;
};

/* ToE services */
struct toe_service_cap {
	struct dev_toe_svc_cap      dev_toe_cap;

	bool alloc_flag;
	u32 pctx_sz;/* 1KB */
	u32 scqc_sz;/* 64B */
};

/* PF FC service resource structure defined */
struct dev_fc_svc_cap {
	/* PF Parent QPC */
	u32 max_parent_qpc_num; /* max number is 2048 */

	/* PF Child QPC */
	u32 max_child_qpc_num;  /* max number is 2048 */
	u32 child_qpc_id_start;

	/* PF SCQ */
	u32 scq_num;            /* 16 */

	/* PF supports SRQ*/
	u32 srq_num;            /* Number of SRQ is 2 */

	u8 vp_id_start;
	u8 vp_id_end;
};

/* FC services*/
struct fc_service_cap {
	struct dev_fc_svc_cap     dev_fc_cap;

	/* Parent QPC */
	u32 parent_qpc_size;    /* 256B */

	/* Child QPC */
	u32 child_qpc_size;     /* 256B */

	/* SQ */
	u32 sqe_size;           /* 128B(in linked list mode) */

	/* SCQ */
	u32 scqc_size;          /* Size of the Context 32B */
	u32 scqe_size;          /* 64B */

	/* SRQ */
	u32 srqc_size;         /* Size of SRQ Context (64B) */
	u32 srqe_size;         /* 32B */
};

/* PF OVS service resource structure defined */
struct dev_ovs_svc_cap {
	/* PF resources */
	u32 max_pctxs; /* Parent Context: max specifications 1M */
	u32 max_cqs;
	u8 dynamic_qp_en;

	/* VF resources */
	u32 vf_max_pctxs; /* Parent Context: max specifications 1M */
	u32 vf_max_cqs;
};

/* OVS services */
struct ovs_service_cap {
	struct dev_ovs_svc_cap dev_ovs_cap;

	bool alloc_flag;
	u32 pctx_sz;    /* 512B */
	u32 scqc_sz;    /* 64B */
};

/* PF ACL service resource structure */
struct dev_acl_svc_cap {
	/* PF resources */
	u32 max_pctxs; /* Parent Context: max specifications 1M */
	u32 max_cqs;

	/* VF resources */
	u32 vf_max_pctxs; /* Parent Context: max specifications 1M */
	u32 vf_max_cqs;
};

/* ACL services */
struct acl_service_cap {
	struct dev_acl_svc_cap    dev_acl_cap;

	bool alloc_flag;
	u32 pctx_sz;    /* 512B */
	u32 scqc_sz;    /* 64B */
};

enum hinic_chip_mode {
	CHIP_MODE_NORMAL,
	CHIP_MODE_BMGW,
	CHIP_MODE_VMGW,
};

bool hinic_support_nic(void *hwdev, struct nic_service_cap *cap);
bool hinic_support_roce(void *hwdev, struct rdma_service_cap *cap);
bool hinic_support_fcoe(void *hwdev, struct fcoe_service_cap *cap);
/* PPF support,PF not support */
bool hinic_support_toe(void *hwdev, struct toe_service_cap *cap);
bool hinic_support_iwarp(void *hwdev, struct rdma_service_cap *cap);
bool hinic_support_fc(void *hwdev, struct fc_service_cap *cap);
bool hinic_support_fic(void *hwdev);
bool hinic_support_ovs(void *hwdev, struct ovs_service_cap *cap);
bool hinic_support_acl(void *hwdev, struct acl_service_cap *cap);
bool hinic_support_rdma(void *hwdev, struct rdma_service_cap *cap);
bool hinic_support_ft(void *hwdev);
bool hinic_func_for_mgmt(void *hwdev);
bool hinic_support_dynamic_q(void *hwdev);

int hinic_set_toe_enable(void *hwdev, bool enable);
bool hinic_get_toe_enable(void *hwdev);
int hinic_set_fcoe_enable(void *hwdev, bool enable);
bool hinic_get_fcoe_enable(void *hwdev);
bool hinic_get_stateful_enable(void *hwdev);

/* Service interface for obtaining service_cap public fields */
/* Obtain service_cap.host_oq_id_mask_val */
u8 hinic_host_oq_id_mask(void *hwdev);
u8 hinic_host_id(void *hwdev);/* Obtain service_cap.host_id */
/* Obtain service_cap.host_total_function */
u16 hinic_host_total_func(void *hwdev);
/* Obtain service_cap.nic_cap.dev_nic_cap.max_sqs */
u16 hinic_func_max_nic_qnum(void *hwdev);
/* Obtain service_cap.dev_cap.max_sqs */
u16 hinic_func_max_qnum(void *hwdev);
u8 hinic_ep_id(void *hwdev);/* Obtain service_cap.ep_id */
u8 hinic_er_id(void *hwdev);/* Obtain service_cap.er_id */
u8 hinic_physical_port_id(void *hwdev);/* Obtain service_cap.port_id */
u8 hinic_func_max_vf(void *hwdev);/* Obtain service_cap.max_vf */
u32 hinic_func_pf_num(void *hwdev);/* Obtain service_cap.pf_num */
u8 hinic_max_num_cos(void *hwdev);
u8 hinic_cos_valid_bitmap(void *hwdev);
u8 hinic_net_port_mode(void *hwdev);/* Obtain service_cap.net_port_mode */

/* The following information is obtained from the bar space
 * which is recorded by SDK layer.
 * Here provide parameter query interface for service
 */
/* func_attr.glb_func_idx, global function index */
u16 hinic_global_func_id(void *hwdev);
/* func_attr.intr_num, MSI-X table entry in function */
u16 hinic_intr_num(void *hwdev);
enum intr_type {
	INTR_TYPE_MSIX,
	INTR_TYPE_MSI,
	INTR_TYPE_INT,
	INTR_TYPE_NONE,
	/* PXE,OVS need single thread processing,
	 * synchronization messages must use poll wait mechanism interface
	 */
};

enum intr_type hinic_intr_type(void *hwdev);

u8 hinic_pf_id_of_vf(void *hwdev); /* func_attr.p2p_idx, belongs to which pf */
u8 hinic_pcie_itf_id(void *hwdev); /* func_attr.itf_idx, pcie interface index */
u8 hinic_vf_in_pf(void *hwdev); /* func_attr.vf_in_pf, the vf offser in pf */
enum func_type {
	TYPE_PF,
	TYPE_VF,
	TYPE_PPF,
	TYPE_UNKNOWN,
};

/* func_attr.func_type, 0-PF 1-VF 2-PPF */
enum func_type hinic_func_type(void *hwdev);

u8 hinic_ceq_num(void *hwdev); /* func_attr.ceq_num, ceq num in one function */
/* func_attr.dma_attr_entry_num, dma attribute entry num */
u8 hinic_dma_attr_entry_num(void *hwdev);
/* The PF func_attr.glb_pf_vf_offset,
 * PF use only
 */
u16 hinic_glb_pf_vf_offset(void *hwdev);
/* func_attr.mpf_idx, mpf global function index,
 * This value is valid only when it is PF
 */
u8 hinic_mpf_idx(void *hwdev);
u8 hinic_ppf_idx(void *hwdev);

enum hinic_msix_state {
	HINIC_MSIX_ENABLE,
	HINIC_MSIX_DISABLE,
};

void hinic_set_msix_state(void *hwdev, u16 msix_idx,
			  enum hinic_msix_state flag);

/* Define the version information structure */
struct dev_version_info {
	u8 up_ver;	/* uP version, directly read from uP
			 * is not configured to file
			 */
	u8 ucode_ver;	/* The microcode version,
			 * read through the CMDq from microcode
			 */
	u8 cfg_file_ver;/* uP configuration file version */
	u8 sdk_ver;	/* SDK driver version */
	u8 hw_ver;	/* Hardware version */
};

/* Obtain service_cap.dev_version_info */
int hinic_dev_ver_info(void *hwdev, struct dev_version_info *ver);

int hinic_vector_to_eqn(void *hwdev, enum hinic_service_type type, int vector);

/* Defines the IRQ information structure */
struct irq_info {
	u16 msix_entry_idx; /* IRQ corresponding index number */
	u32 irq_id;         /* the IRQ number from OS */
};

int hinic_alloc_irqs(void *hwdev, enum hinic_service_type type, u16 req_num,
		     struct irq_info *irq_info_array, u16 *resp_num);
void hinic_free_irq(void *hwdev, enum hinic_service_type type, u32 irq_id);
int hinic_alloc_ceqs(void *hwdev, enum hinic_service_type type, int req_num,
		     int *ceq_id_array, int *resp_num);
void hinic_free_ceq(void *hwdev, enum hinic_service_type type, int ceq_id);
int hinic_sync_time(void *hwdev, u64 time);

struct hinic_micro_log_info {
	int (*init)(void *hwdev);
	void (*deinit)(void *hwdev);
};

int hinic_register_micro_log(struct hinic_micro_log_info *micro_log_info);
void hinic_unregister_micro_log(struct hinic_micro_log_info *micro_log_info);

void hinic_disable_mgmt_msg_report(void *hwdev);
void hinic_set_func_deinit_flag(void *hwdev);
void hinic_flush_mgmt_workq(void *hwdev);


enum func_nic_state {
	HINIC_FUNC_NIC_DEL,
	HINIC_FUNC_NIC_ADD,
};

struct hinic_func_nic_state {
	u8 state;
	u8 rsvd0;
	u16 func_idx;

	u8 rsvd1[16];
};

int hinic_set_func_nic_state(void *hwdev, struct hinic_func_nic_state *state);
int hinic_get_func_nic_enable(void *hwdev, u16 glb_func_idx, bool *en);
bool hinic_get_master_host_mbox_enable(void *hwdev);
bool hinic_get_slave_host_enable(void *hwdev, u8 host_id);
int hinic_func_own_get(void *hwdev);
void hinic_func_own_free(void *hwdev);
int hinic_global_func_id_get(void *hwdev, u16 *func_id);
u16 hinic_pf_id_of_vf_hw(void *hwdev);
u16 hinic_global_func_id_hw(void *hwdev);
bool hinic_func_for_pt(void *hwdev);
bool hinic_func_for_hwpt(void *hwdev);
u32 hinic_get_db_size(void *cfg_reg_base, enum hinic_chip_mode *chip_mode);
#endif
