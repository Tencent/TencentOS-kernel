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
#define pr_fmt(fmt) KBUILD_MODNAME ": [COMM]" fmt

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/semaphore.h>
#include <linux/vmalloc.h>

#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_hwdev.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hwif.h"
#include "hinic_mbox.h"
#include "hinic_cfg.h"
#include "hinic_nic_cfg.h"
#include "hinic_mgmt_interface.h"
#include "hinic_multi_host_mgmt.h"

uint g_rdma_mtts_num;
uint g_rdma_qps_num;
uint g_rdma_mpts_num;
uint g_vfs_num;
module_param(g_rdma_mtts_num, uint, 0444);
MODULE_PARM_DESC(g_rdma_mtts_num, "number of roce used mtts, use default value when pass 0");
module_param(g_rdma_qps_num, uint, 0444);
MODULE_PARM_DESC(g_rdma_qps_num, "number of roce used qps, use default value when pass 0");
module_param(g_rdma_mpts_num, uint, 0444);
MODULE_PARM_DESC(g_rdma_mpts_num, "number of roce used mpts, use default value when pass 0");
module_param(g_vfs_num, uint, 0444);
MODULE_PARM_DESC(g_vfs_num, "number of used vfs, use default value when pass 0 ");

uint intr_mode;

uint timer_enable = 1;
uint bloomfilter_enable;
uint g_test_qpc_num;
uint g_test_qpc_resvd_num;
uint g_test_pagesize_reorder;
uint g_test_xid_alloc_mode = 1;
uint g_test_gpa_check_enable = 1;
uint g_test_qpc_alloc_mode = 2;
uint g_test_scqc_alloc_mode = 2;
uint g_test_max_conn;
uint g_test_max_cache_conn;
uint g_test_scqc_num;
uint g_test_mpt_num;
uint g_test_mpt_resvd;
uint g_test_scq_resvd;
uint g_test_hash_num;
uint g_test_reorder_num;


static void set_cfg_test_param(struct cfg_mgmt_info *cfg_mgmt)
{
	cfg_mgmt->svc_cap.timer_en = (u8)timer_enable;
	cfg_mgmt->svc_cap.bloomfilter_en = (u8)bloomfilter_enable;
	cfg_mgmt->svc_cap.test_qpc_num = g_test_qpc_num;
	cfg_mgmt->svc_cap.test_qpc_resvd_num = g_test_qpc_resvd_num;
	cfg_mgmt->svc_cap.test_page_size_reorder = g_test_pagesize_reorder;
	cfg_mgmt->svc_cap.test_xid_alloc_mode = (bool)g_test_xid_alloc_mode;
	cfg_mgmt->svc_cap.test_gpa_check_enable = (bool)g_test_gpa_check_enable;
	cfg_mgmt->svc_cap.test_qpc_alloc_mode = (u8)g_test_qpc_alloc_mode;
	cfg_mgmt->svc_cap.test_scqc_alloc_mode = (u8)g_test_scqc_alloc_mode;
	cfg_mgmt->svc_cap.test_max_conn_num = g_test_max_conn;
	cfg_mgmt->svc_cap.test_max_cache_conn_num = g_test_max_cache_conn;
	cfg_mgmt->svc_cap.test_scqc_num = g_test_scqc_num;
	cfg_mgmt->svc_cap.test_mpt_num = g_test_mpt_num;
	cfg_mgmt->svc_cap.test_scq_resvd_num = g_test_scq_resvd;
	cfg_mgmt->svc_cap.test_mpt_recvd_num = g_test_mpt_resvd;
	cfg_mgmt->svc_cap.test_hash_num = g_test_hash_num;
	cfg_mgmt->svc_cap.test_reorder_num = g_test_reorder_num;
}


int hinic_sync_time(void *hwdev, u64 time)
{
	struct hinic_sync_time_info time_info = {0};
	u16 out_size = sizeof(time_info);
	int err;

	time_info.mstime = time;
	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_COMM,
				     HINIC_MGMT_CMD_SYNC_TIME, &time_info,
				     sizeof(time_info), &time_info, &out_size,
				     0);
	if (err || time_info.status || !out_size) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Failed to sync time to mgmt, err: %d, status: 0x%x, out size: 0x%x\n",
			err, time_info.status, out_size);
		return -EFAULT;
	}

	return err;
}

static void parse_sf_en_cap(struct service_cap *cap,
			    struct hinic_dev_cap *dev_cap, enum func_type type)
{
	struct dev_sf_svc_attr *attr = &cap->sf_svc_attr;

	if (type == TYPE_PPF) {
		/* For PPF's SF EN flag, we assign it in get_dynamic_res_cap().
		 * we only save its VF's flag.
		 */
		attr->sf_en_vf = dev_cap->sf_en_vf;
	} else if (type == TYPE_PF) {
		if (dev_cap->sf_en_pf)
			cap->sf_en = true;
		else
			cap->sf_en = false;

		attr->sf_en_vf = dev_cap->sf_en_vf;
	} else {
		/* VF gets SF_EN_VF from PPF/PF */
		if (dev_cap->sf_en_vf)
			cap->sf_en = true;
		else
			cap->sf_en = false;

		attr->sf_en_vf = 0;
	}
}

static void parse_pub_res_cap(struct service_cap *cap,
			      struct hinic_dev_cap *dev_cap,
			      enum func_type type)
{
	struct dev_sf_svc_attr *attr = &cap->sf_svc_attr;

	cap->svc_type = dev_cap->svc_cap_en;
	cap->chip_svc_type = cap->svc_type;

	if (dev_cap->sf_svc_attr & SF_SVC_FT_BIT)
		attr->ft_en = true;
	else
		attr->ft_en = false;

	if (dev_cap->sf_svc_attr & SF_SVC_RDMA_BIT)
		attr->rdma_en = true;
	else
		attr->rdma_en = false;

	cap->host_id = dev_cap->host_id;
	cap->ep_id = dev_cap->ep_id;

	cap->max_cos_id = dev_cap->max_cos_id;
	cap->cos_valid_bitmap = dev_cap->valid_cos_bitmap;
	cap->er_id = dev_cap->er_id;
	cap->port_id = dev_cap->port_id;
	cap->force_up = dev_cap->force_up;

	parse_sf_en_cap(cap, dev_cap, type);

	/* PF/PPF */
	if (type == TYPE_PF || type == TYPE_PPF) {
		cap->max_vf = dev_cap->max_vf;
		cap->pf_num = dev_cap->pf_num;
		cap->pf_id_start = dev_cap->pf_id_start;
		cap->vf_num = dev_cap->vf_num;
		cap->vf_id_start = dev_cap->vf_id_start;

		/* FC need max queue number, but max queue number info is in
		 * l2nic cap, we also put max queue num info in public cap, so
		 * FC can get correct max queue number info.
		 */
		cap->max_sqs = dev_cap->nic_max_sq + 1;
		cap->max_rqs = dev_cap->nic_max_rq + 1;
	} else {
		cap->max_vf = 0;
		cap->max_sqs = dev_cap->nic_max_sq;
		cap->max_rqs = dev_cap->nic_max_rq;
	}

	cap->host_total_function = dev_cap->host_total_func;
	cap->host_oq_id_mask_val = dev_cap->host_oq_id_mask_val;
	cap->max_connect_num = dev_cap->max_conn_num;
	cap->max_stick2cache_num = dev_cap->max_stick2cache_num;
	cap->bfilter_start_addr = dev_cap->max_bfilter_start_addr;
	cap->bfilter_len = dev_cap->bfilter_len;
	cap->hash_bucket_num = dev_cap->hash_bucket_num;
	cap->dev_ver_info.cfg_file_ver = dev_cap->cfg_file_ver;
	cap->net_port_mode = dev_cap->net_port_mode;

	/* FC does not use VF */
	if (cap->net_port_mode == CFG_NET_MODE_FC)
		cap->max_vf = 0;

	pr_info("Get public resource capbility, svc_cap_en: 0x%x\n",
		dev_cap->svc_cap_en);
	pr_info("Host_id=0x%x, ep_id=0x%x, max_cos_id=0x%x, cos_bitmap=0x%x, er_id=0x%x, port_id=0x%x\n",
		cap->host_id, cap->ep_id,
		cap->max_cos_id, cap->cos_valid_bitmap,
		cap->er_id, cap->port_id);
	pr_info("Host_total_function=0x%x, host_oq_id_mask_val=0x%x, net_port_mode=0x%x, max_vf=0x%x\n",
		cap->host_total_function, cap->host_oq_id_mask_val,
		cap->net_port_mode, cap->max_vf);

	pr_info("Pf_num=0x%x, pf_id_start=0x%x, vf_num=0x%x, vf_id_start=0x%x\n",
		cap->pf_num, cap->pf_id_start,
		cap->vf_num, cap->vf_id_start);

	/* Check parameters from firmware */
	if (cap->max_sqs > HINIC_CFG_MAX_QP ||
	    cap->max_rqs > HINIC_CFG_MAX_QP) {
		pr_info("Number of qp exceed limit[1-%d]: sq: %d, rq: %d\n",
			HINIC_CFG_MAX_QP, cap->max_sqs, cap->max_rqs);
		cap->max_sqs = HINIC_CFG_MAX_QP;
		cap->max_rqs = HINIC_CFG_MAX_QP;
	}
}

static void parse_dynamic_share_res_cap(struct service_cap *cap,
					struct hinic_dev_cap *dev_cap,
					enum func_type type)
{
	struct host_shared_resource_cap *shared_cap = &cap->shared_res_cap;

	shared_cap->host_pctxs = dev_cap->host_pctx_num;

	if (dev_cap->host_sf_en)
		cap->sf_en = true;
	else
		cap->sf_en = false;

	shared_cap->host_cctxs = dev_cap->host_ccxt_num;
	shared_cap->host_scqs = dev_cap->host_scq_num;
	shared_cap->host_srqs = dev_cap->host_srq_num;
	shared_cap->host_mpts = dev_cap->host_mpt_num;

	pr_info("Dynamic share resource capbility, host_pctxs=0x%x, host_cctxs=0x%x, host_scqs=0x%x, host_srqs=0x%x, host_mpts=0x%x\n",
		shared_cap->host_pctxs,
		shared_cap->host_cctxs,
		shared_cap->host_scqs,
		shared_cap->host_srqs,
		shared_cap->host_mpts);
}

static void parse_l2nic_res_cap(struct service_cap *cap,
				struct hinic_dev_cap *dev_cap,
				enum func_type type)
{
	struct nic_service_cap *nic_cap = &cap->nic_cap;

	/* PF/PPF */
	if (type == TYPE_PF || type == TYPE_PPF) {
		nic_cap->max_sqs = dev_cap->nic_max_sq + 1;
		nic_cap->max_rqs = dev_cap->nic_max_rq + 1;
		nic_cap->vf_max_sqs = dev_cap->nic_vf_max_sq + 1;
		nic_cap->vf_max_rqs = dev_cap->nic_vf_max_rq + 1;
		nic_cap->max_queue_allowed = 0;
		nic_cap->dynamic_qp = 0;
	} else {
		nic_cap->max_sqs = dev_cap->nic_max_sq;
		nic_cap->max_rqs = dev_cap->nic_max_rq;
		nic_cap->vf_max_sqs = 0;
		nic_cap->vf_max_rqs = 0;
		nic_cap->max_queue_allowed = dev_cap->max_queue_allowed;
		nic_cap->dynamic_qp = dev_cap->ovs_dq_en;
	}

	if (dev_cap->nic_lro_en)
		nic_cap->lro_en = true;
	else
		nic_cap->lro_en = false;

	nic_cap->lro_sz = dev_cap->nic_lro_sz;
	nic_cap->tso_sz = dev_cap->nic_tso_sz;

	pr_info("L2nic resource capbility, max_sqs=0x%x, max_rqs=0x%x, vf_max_sqs=0x%x, vf_max_rqs=0x%x, max_queue_allowed=0x%x\n",
		nic_cap->max_sqs,
		nic_cap->max_rqs,
		nic_cap->vf_max_sqs,
		nic_cap->vf_max_rqs,
		nic_cap->max_queue_allowed);

	/* Check parameters from firmware */
	if (nic_cap->max_sqs > HINIC_CFG_MAX_QP ||
	    nic_cap->max_rqs > HINIC_CFG_MAX_QP) {
		pr_info("Number of qp exceed limit[1-%d]: sq: %d, rq: %d\n",
			HINIC_CFG_MAX_QP, nic_cap->max_sqs, nic_cap->max_rqs);
		nic_cap->max_sqs = HINIC_CFG_MAX_QP;
		nic_cap->max_rqs = HINIC_CFG_MAX_QP;
	}
}

static void parse_roce_res_cap(struct service_cap *cap,
			       struct hinic_dev_cap *dev_cap,
			       enum func_type type)
{
	struct dev_roce_svc_own_cap *roce_cap =
		&cap->rdma_cap.dev_rdma_cap.roce_own_cap;

	roce_cap->max_qps = dev_cap->roce_max_qp;
	roce_cap->max_cqs = dev_cap->roce_max_cq;
	roce_cap->max_srqs = dev_cap->roce_max_srq;
	roce_cap->max_mpts = dev_cap->roce_max_mpt;
	roce_cap->num_cos = dev_cap->max_cos_id + 1;

	/* PF/PPF */
	if (type == TYPE_PF || type == TYPE_PPF) {
		roce_cap->vf_max_qps = dev_cap->roce_vf_max_qp;
		roce_cap->vf_max_cqs = dev_cap->roce_vf_max_cq;
		roce_cap->vf_max_srqs = dev_cap->roce_vf_max_srq;
		roce_cap->vf_max_mpts = dev_cap->roce_vf_max_mpt;
	} else {
		roce_cap->vf_max_qps = 0;
		roce_cap->vf_max_cqs = 0;
		roce_cap->vf_max_srqs = 0;
		roce_cap->vf_max_mpts = 0;
	}

	roce_cap->cmtt_cl_start = dev_cap->roce_cmtt_cl_start;
	roce_cap->cmtt_cl_end = dev_cap->roce_cmtt_cl_end;
	roce_cap->cmtt_cl_sz = dev_cap->roce_cmtt_cl_size;

	roce_cap->dmtt_cl_start = dev_cap->roce_dmtt_cl_start;
	roce_cap->dmtt_cl_end = dev_cap->roce_dmtt_cl_end;
	roce_cap->dmtt_cl_sz = dev_cap->roce_dmtt_cl_size;

	roce_cap->wqe_cl_start = dev_cap->roce_wqe_cl_start;
	roce_cap->wqe_cl_end = dev_cap->roce_wqe_cl_end;
	roce_cap->wqe_cl_sz = dev_cap->roce_wqe_cl_size;

	pr_info("Get roce resource capbility\n");
	pr_info("Max_qps=0x%x, max_cqs=0x%x, max_srqs=0x%x, max_mpts=0x%x\n",
		roce_cap->max_qps, roce_cap->max_cqs,
		roce_cap->max_srqs, roce_cap->max_mpts);

	pr_info("Vf_max_qps=0x%x, vf_max_cqs=0x%x, vf_max_srqs= 0x%x, vf_max_mpts= 0x%x\n",
		roce_cap->vf_max_qps, roce_cap->vf_max_cqs,
		roce_cap->vf_max_srqs, roce_cap->vf_max_mpts);

	pr_info("Cmtt_start=0x%x, cmtt_end=0x%x, cmtt_sz=0x%x\n",
		roce_cap->cmtt_cl_start, roce_cap->cmtt_cl_end,
		roce_cap->cmtt_cl_sz);

	pr_info("Dmtt_start=0x%x, dmtt_end=0x%x, dmtt_sz=0x%x\n",
		roce_cap->dmtt_cl_start, roce_cap->dmtt_cl_end,
		roce_cap->dmtt_cl_sz);

	pr_info("Wqe_start=0x%x, wqe_end=0x%x, wqe_sz=0x%x\n",
		roce_cap->wqe_cl_start, roce_cap->wqe_cl_end,
		roce_cap->wqe_cl_sz);

	if (roce_cap->max_qps == 0) {
		roce_cap->max_qps = 1024;
		roce_cap->max_cqs = 2048;
		roce_cap->max_srqs = 1024;
		roce_cap->max_mpts = 1024;

		if (type == TYPE_PF || type == TYPE_PPF) {
			roce_cap->vf_max_qps = 512;
			roce_cap->vf_max_cqs = 1024;
			roce_cap->vf_max_srqs = 512;
			roce_cap->vf_max_mpts = 512;
		}
	}
}

static void parse_iwarp_res_cap(struct service_cap *cap,
				struct hinic_dev_cap *dev_cap,
				enum func_type type)

{
	struct dev_iwarp_svc_own_cap *iwarp_cap =
		&cap->rdma_cap.dev_rdma_cap.iwarp_own_cap;

	iwarp_cap->max_qps = dev_cap->iwarp_max_qp;
	iwarp_cap->max_cqs = dev_cap->iwarp_max_cq;
	iwarp_cap->max_mpts = dev_cap->iwarp_max_mpt;
	iwarp_cap->num_cos = dev_cap->max_cos_id + 1;

	/* PF/PPF */
	if (type == TYPE_PF || type == TYPE_PPF) {
		iwarp_cap->vf_max_qps = dev_cap->iwarp_vf_max_qp;
		iwarp_cap->vf_max_cqs = dev_cap->iwarp_vf_max_cq;
		iwarp_cap->vf_max_mpts = dev_cap->iwarp_vf_max_mpt;
	} else {
		iwarp_cap->vf_max_qps = 0;
		iwarp_cap->vf_max_cqs = 0;
		iwarp_cap->vf_max_mpts = 0;
	}

	iwarp_cap->cmtt_cl_start = dev_cap->iwarp_cmtt_cl_start;
	iwarp_cap->cmtt_cl_end = dev_cap->iwarp_cmtt_cl_end;
	iwarp_cap->cmtt_cl_sz = dev_cap->iwarp_cmtt_cl_size;

	iwarp_cap->dmtt_cl_start = dev_cap->iwarp_dmtt_cl_start;
	iwarp_cap->dmtt_cl_end = dev_cap->iwarp_dmtt_cl_end;
	iwarp_cap->dmtt_cl_sz = dev_cap->iwarp_dmtt_cl_size;

	iwarp_cap->wqe_cl_start = dev_cap->iwarp_wqe_cl_start;
	iwarp_cap->wqe_cl_end = dev_cap->iwarp_wqe_cl_end;
	iwarp_cap->wqe_cl_sz = dev_cap->iwarp_wqe_cl_size;

	pr_info("Get iwrap resource capbility\n");
	pr_info("Max_qps=0x%x, max_cqs=0x%x, max_mpts=0x%x\n",
		iwarp_cap->max_qps, iwarp_cap->max_cqs,
		iwarp_cap->max_mpts);
	pr_info("Vf_max_qps=0x%x, vf_max_cqs=0x%x, vf_max_mpts=0x%x\n",
		iwarp_cap->vf_max_qps, iwarp_cap->vf_max_cqs,
		iwarp_cap->vf_max_mpts);

	pr_info("Cmtt_start=0x%x, cmtt_end=0x%x, cmtt_sz=0x%x\n",
		iwarp_cap->cmtt_cl_start, iwarp_cap->cmtt_cl_end,
		iwarp_cap->cmtt_cl_sz);

	pr_info("Dmtt_start=0x%x, dmtt_end=0x%x, dmtt_sz=0x%x\n",
		iwarp_cap->dmtt_cl_start, iwarp_cap->dmtt_cl_end,
		iwarp_cap->dmtt_cl_sz);

	pr_info("Wqe_start=0x%x, wqe_end=0x%x, wqe_sz=0x%x\n",
		iwarp_cap->wqe_cl_start, iwarp_cap->wqe_cl_end,
		iwarp_cap->wqe_cl_sz);

	if (iwarp_cap->max_qps == 0) {
		iwarp_cap->max_qps = 8;
		iwarp_cap->max_cqs = 16;
		iwarp_cap->max_mpts = 8;

		if (type == TYPE_PF || type == TYPE_PPF) {
			iwarp_cap->vf_max_qps = 8;
			iwarp_cap->vf_max_cqs = 16;
			iwarp_cap->vf_max_mpts = 8;
		}
	}
}

static void parse_fcoe_res_cap(struct service_cap *cap,
			       struct hinic_dev_cap *dev_cap,
			       enum func_type type)
{
	struct dev_fcoe_svc_cap *fcoe_cap = &cap->fcoe_cap.dev_fcoe_cap;

	fcoe_cap->max_qps = dev_cap->fcoe_max_qp;
	fcoe_cap->max_cqs = dev_cap->fcoe_max_cq;
	fcoe_cap->max_srqs = dev_cap->fcoe_max_srq;
	fcoe_cap->max_cctxs = dev_cap->fcoe_max_cctx;
	fcoe_cap->cctxs_id_start = dev_cap->fcoe_cctx_id_start;
	fcoe_cap->vp_id_start = dev_cap->fcoe_vp_id_start;
	fcoe_cap->vp_id_end = dev_cap->fcoe_vp_id_end;

	pr_info("Get fcoe resource capbility\n");
	pr_info("Max_qps=0x%x, max_cqs=0x%x, max_srqs=0x%x, max_cctxs=0x%x, cctxs_id_start=0x%x\n",
		fcoe_cap->max_qps, fcoe_cap->max_cqs, fcoe_cap->max_srqs,
		fcoe_cap->max_cctxs, fcoe_cap->cctxs_id_start);
	pr_info("Vp_id_start=0x%x, vp_id_end=0x%x\n",
		fcoe_cap->vp_id_start, fcoe_cap->vp_id_end);
}

static void parse_toe_res_cap(struct service_cap *cap,
			      struct hinic_dev_cap *dev_cap,
			      enum func_type type)
{
	struct dev_toe_svc_cap *toe_cap = &cap->toe_cap.dev_toe_cap;

	toe_cap->max_pctxs = dev_cap->toe_max_pctx;
	toe_cap->max_cqs = dev_cap->toe_max_cq;
	toe_cap->max_srqs = dev_cap->toe_max_srq;
	toe_cap->srq_id_start = dev_cap->toe_srq_id_start;
	toe_cap->num_cos = dev_cap->max_cos_id + 1;

	pr_info("Get toe resource capbility, max_pctxs=0x%x, max_cqs=0x%x, max_srqs=0x%x, srq_id_start=0x%x\n",
		toe_cap->max_pctxs, toe_cap->max_cqs, toe_cap->max_srqs,
		toe_cap->srq_id_start);
}

static void parse_fc_res_cap(struct service_cap *cap,
			     struct hinic_dev_cap *dev_cap,
			     enum func_type type)
{
	struct dev_fc_svc_cap *fc_cap = &cap->fc_cap.dev_fc_cap;

	fc_cap->max_parent_qpc_num = dev_cap->fc_max_pctx;
	fc_cap->scq_num = dev_cap->fc_max_scq;
	fc_cap->srq_num = dev_cap->fc_max_srq;
	fc_cap->max_child_qpc_num = dev_cap->fc_max_cctx;
	fc_cap->child_qpc_id_start = dev_cap->fc_cctx_id_start;
	fc_cap->vp_id_start = dev_cap->fc_vp_id_start;
	fc_cap->vp_id_end = dev_cap->fc_vp_id_end;

	pr_info("Get fc resource capbility\n");
	pr_info("Max_parent_qpc_num=0x%x, scq_num=0x%x, srq_num=0x%x, max_child_qpc_num=0x%x, child_qpc_id_start=0x%x\n",
		fc_cap->max_parent_qpc_num, fc_cap->scq_num, fc_cap->srq_num,
		fc_cap->max_child_qpc_num, fc_cap->child_qpc_id_start);
	pr_info("Vp_id_start=0x%x, vp_id_end=0x%x\n",
		fc_cap->vp_id_start, fc_cap->vp_id_end);
}

static void parse_ovs_res_cap(struct service_cap *cap,
			      struct hinic_dev_cap *dev_cap,
			      enum func_type type)
{
	struct ovs_service_cap *ovs_cap = &cap->ovs_cap;

	ovs_cap->dev_ovs_cap.max_pctxs = dev_cap->ovs_max_qpc;
	ovs_cap->dev_ovs_cap.max_cqs = 0;

	if (type == TYPE_PF || type == TYPE_PPF)
		ovs_cap->dev_ovs_cap.dynamic_qp_en = dev_cap->ovs_dq_en;

	pr_info("Get ovs resource capbility, max_qpc: 0x%x\n",
		ovs_cap->dev_ovs_cap.max_pctxs);
}

static void parse_acl_res_cap(struct service_cap *cap,
			      struct hinic_dev_cap *dev_cap,
			      enum func_type type)
{
	struct acl_service_cap *acl_cap = &cap->acl_cap;

	acl_cap->dev_acl_cap.max_pctxs = 1024 * 1024;
	acl_cap->dev_acl_cap.max_cqs = 8;
}

static void parse_dev_cap(struct hinic_hwdev *dev,
			  struct hinic_dev_cap *dev_cap, enum func_type type)
{
	struct service_cap *cap = &dev->cfg_mgmt->svc_cap;

	/* Public resource */
	parse_pub_res_cap(cap, dev_cap, type);

	/* PPF managed dynamic resource */
	if (type == TYPE_PPF)
		parse_dynamic_share_res_cap(cap, dev_cap, type);

	/* L2 NIC resource */
	if (IS_NIC_TYPE(dev))
		parse_l2nic_res_cap(cap, dev_cap, type);


	/* FCoE/IOE/TOE/FC without virtulization */
	if (type == TYPE_PF || type == TYPE_PPF) {
		if (IS_FC_TYPE(dev))
			parse_fc_res_cap(cap, dev_cap, type);


		if (IS_FCOE_TYPE(dev))
			parse_fcoe_res_cap(cap, dev_cap, type);

		if (IS_TOE_TYPE(dev))
			parse_toe_res_cap(cap, dev_cap, type);
	}

	/* RoCE resource */
	if (IS_ROCE_TYPE(dev))
		parse_roce_res_cap(cap, dev_cap, type);

	/* iWARP resource */
	if (IS_IWARP_TYPE(dev))
		parse_iwarp_res_cap(cap, dev_cap, type);

	if (IS_OVS_TYPE(dev))
		parse_ovs_res_cap(cap, dev_cap, type);

	if (IS_ACL_TYPE(dev))
		parse_acl_res_cap(cap, dev_cap, type);
}

static int get_cap_from_fw(struct hinic_hwdev *dev, enum func_type type)
{
	struct hinic_dev_cap dev_cap = {0};
	u16 out_len = sizeof(dev_cap);
	int err;

	dev_cap.version = HINIC_CMD_VER_FUNC_ID;
	err = hinic_global_func_id_get(dev, &dev_cap.func_id);
	if (err)
		return err;

	sdk_info(dev->dev_hdl, "Get cap from fw, func_idx: %d\n",
		 dev_cap.func_id);

	err = hinic_msg_to_mgmt_sync(dev, HINIC_MOD_CFGM, HINIC_CFG_NIC_CAP,
				     &dev_cap, sizeof(dev_cap),
				     &dev_cap, &out_len, 0);
	if (err || dev_cap.status || !out_len) {
		sdk_err(dev->dev_hdl,
			"Failed to get capability from FW, err: %d, status: 0x%x, out size: 0x%x\n",
			err, dev_cap.status, out_len);
		return -EFAULT;
	}

	parse_dev_cap(dev, &dev_cap, type);
	return 0;
}

static int get_cap_from_pf(struct hinic_hwdev *dev, enum func_type type)
{
	struct hinic_dev_cap dev_cap = {0};
	u16 in_len, out_len;
	int err;

	in_len = sizeof(dev_cap);
	out_len = in_len;

	err = hinic_msg_to_mgmt_sync(dev, HINIC_MOD_CFGM, HINIC_CFG_MBOX_CAP,
				     &dev_cap, in_len, &dev_cap, &out_len, 0);
	if (err || dev_cap.status || !out_len) {
		sdk_err(dev->dev_hdl, "Failed to get capability from PF,  err: %d, status: 0x%x, out size: 0x%x\n",
			err, dev_cap.status, out_len);
		return -EFAULT;
	}

	parse_dev_cap(dev, &dev_cap, type);
	return 0;
}

static int get_dev_cap(struct hinic_hwdev *dev)
{
	int err;
	enum func_type type = HINIC_FUNC_TYPE(dev);

	switch (type) {
	case TYPE_PF:
	case TYPE_PPF:
		err = get_cap_from_fw(dev, type);
		if (err) {
			sdk_err(dev->dev_hdl, "Failed to get PF/PPF capability\n");
			return err;
		}
		break;
	case TYPE_VF:
		err = get_cap_from_pf(dev, type);
		if (err) {
			sdk_err(dev->dev_hdl, "Failed to get VF capability\n");
			return err;
		}
		break;
	default:
		sdk_err(dev->dev_hdl, "Unsupported PCI Function type: %d\n",
			type);
		return -EINVAL;
	}

	return 0;
}

static void nic_param_fix(struct hinic_hwdev *dev)
{
	struct nic_service_cap *nic_cap = &dev->cfg_mgmt->svc_cap.nic_cap;

	if ((hinic_func_type(dev) == TYPE_VF) && (nic_cap->max_queue_allowed != 0)) {
		nic_cap->max_rqs = nic_cap->max_queue_allowed;
		nic_cap->max_sqs = nic_cap->max_queue_allowed;
	}

}

static void rdma_param_fix(struct hinic_hwdev *dev)
{
	struct service_cap *cap = &dev->cfg_mgmt->svc_cap;
	struct rdma_service_cap *rdma_cap = &cap->rdma_cap;
	struct dev_roce_svc_own_cap *roce_cap =
		&rdma_cap->dev_rdma_cap.roce_own_cap;
	struct dev_iwarp_svc_own_cap *iwarp_cap =
		&rdma_cap->dev_rdma_cap.iwarp_own_cap;

	rdma_cap->log_mtt = LOG_MTT_SEG;
	rdma_cap->log_rdmarc = LOG_RDMARC_SEG;
	rdma_cap->reserved_qps = RDMA_RSVD_QPS;
	rdma_cap->max_sq_sg = RDMA_MAX_SQ_SGE;

	/* RoCE */
	if (IS_ROCE_TYPE(dev)) {
		roce_cap->qpc_entry_sz = ROCE_QPC_ENTRY_SZ;
		roce_cap->max_wqes = ROCE_MAX_WQES;
		roce_cap->max_rq_sg = ROCE_MAX_RQ_SGE;
		roce_cap->max_sq_inline_data_sz = ROCE_MAX_SQ_INLINE_DATA_SZ;
		roce_cap->max_rq_desc_sz = ROCE_MAX_RQ_DESC_SZ;
		roce_cap->rdmarc_entry_sz = ROCE_RDMARC_ENTRY_SZ;
		roce_cap->max_qp_init_rdma = ROCE_MAX_QP_INIT_RDMA;
		roce_cap->max_qp_dest_rdma = ROCE_MAX_QP_DEST_RDMA;
		roce_cap->max_srq_wqes = ROCE_MAX_SRQ_WQES;
		roce_cap->reserved_srqs = ROCE_RSVD_SRQS;
		roce_cap->max_srq_sge = ROCE_MAX_SRQ_SGE;
		roce_cap->srqc_entry_sz = ROCE_SRQC_ENTERY_SZ;
		roce_cap->max_msg_sz = ROCE_MAX_MSG_SZ;
	} else {
		iwarp_cap->qpc_entry_sz = IWARP_QPC_ENTRY_SZ;
		iwarp_cap->max_wqes = IWARP_MAX_WQES;
		iwarp_cap->max_rq_sg = IWARP_MAX_RQ_SGE;
		iwarp_cap->max_sq_inline_data_sz = IWARP_MAX_SQ_INLINE_DATA_SZ;
		iwarp_cap->max_rq_desc_sz = IWARP_MAX_RQ_DESC_SZ;
		iwarp_cap->max_irq_depth = IWARP_MAX_IRQ_DEPTH;
		iwarp_cap->irq_entry_size = IWARP_IRQ_ENTRY_SZ;
		iwarp_cap->max_orq_depth = IWARP_MAX_ORQ_DEPTH;
		iwarp_cap->orq_entry_size = IWARP_ORQ_ENTRY_SZ;
		iwarp_cap->max_rtoq_depth = IWARP_MAX_RTOQ_DEPTH;
		iwarp_cap->rtoq_entry_size = IWARP_RTOQ_ENTRY_SZ;
		iwarp_cap->max_ackq_depth = IWARP_MAX_ACKQ_DEPTH;
		iwarp_cap->ackq_entry_size = IWARP_ACKQ_ENTRY_SZ;
		iwarp_cap->max_msg_sz = IWARP_MAX_MSG_SZ;
	}

	rdma_cap->max_sq_desc_sz = RDMA_MAX_SQ_DESC_SZ;
	rdma_cap->wqebb_size = WQEBB_SZ;
	rdma_cap->max_cqes = RDMA_MAX_CQES;
	rdma_cap->reserved_cqs = RDMA_RSVD_CQS;
	rdma_cap->cqc_entry_sz = RDMA_CQC_ENTRY_SZ;
	rdma_cap->cqe_size = RDMA_CQE_SZ;
	rdma_cap->reserved_mrws = RDMA_RSVD_MRWS;
	rdma_cap->mpt_entry_sz = RDMA_MPT_ENTRY_SZ;

	/* 2^8 - 1
	 *	+------------------------+-----------+
	 *	|   4B   |      1M(20b)  | Key(8b)   |
	 *	+------------------------+-----------+
	 * key = 8bit key + 24bit index,
	 * now Lkey of SGE uses 2bit(bit31 and bit30), so key only have 10bit,
	 * we use original 8bits directly for simpilification
	 */
	rdma_cap->max_fmr_maps = 255;
	rdma_cap->num_mtts = g_rdma_mtts_num > 0 ?
			      g_rdma_mtts_num : RDMA_NUM_MTTS;
	rdma_cap->log_mtt_seg = LOG_MTT_SEG;
	rdma_cap->mtt_entry_sz = MTT_ENTRY_SZ;
	rdma_cap->log_rdmarc_seg = LOG_RDMARC_SEG;
	rdma_cap->local_ca_ack_delay = LOCAL_ACK_DELAY;
	rdma_cap->num_ports = RDMA_NUM_PORTS;
	rdma_cap->db_page_size = DB_PAGE_SZ;
	rdma_cap->direct_wqe_size = DWQE_SZ;
	rdma_cap->num_pds = NUM_PD;
	rdma_cap->reserved_pds = RSVD_PD;
	rdma_cap->max_xrcds = MAX_XRCDS;
	rdma_cap->reserved_xrcds = RSVD_XRCDS;
	rdma_cap->max_gid_per_port = MAX_GID_PER_PORT;
	rdma_cap->gid_entry_sz = GID_ENTRY_SZ;
	rdma_cap->reserved_lkey = RSVD_LKEY;
	rdma_cap->num_comp_vectors = (u32)dev->cfg_mgmt->eq_info.num_ceq;
	rdma_cap->page_size_cap = PAGE_SZ_CAP;
	rdma_cap->flags = (RDMA_BMME_FLAG_LOCAL_INV |
			   RDMA_BMME_FLAG_REMOTE_INV |
			   RDMA_BMME_FLAG_FAST_REG_WR |
			   RDMA_DEV_CAP_FLAG_XRC |
			   RDMA_DEV_CAP_FLAG_MEM_WINDOW |
			   RDMA_BMME_FLAG_TYPE_2_WIN |
			   RDMA_BMME_FLAG_WIN_TYPE_2B |
			   RDMA_DEV_CAP_FLAG_ATOMIC);
	rdma_cap->max_frpl_len = MAX_FRPL_LEN;
	rdma_cap->max_pkeys = MAX_PKEYS;
}

static void fcoe_param_fix(struct hinic_hwdev *dev)
{
	struct service_cap *cap = &dev->cfg_mgmt->svc_cap;
	struct fcoe_service_cap *fcoe_cap = &cap->fcoe_cap;

	fcoe_cap->qpc_basic_size = FCOE_PCTX_SZ;
	fcoe_cap->childc_basic_size = FCOE_CCTX_SZ;
	fcoe_cap->sqe_size = FCOE_SQE_SZ;

	fcoe_cap->scqc_basic_size = FCOE_SCQC_SZ;
	fcoe_cap->scqe_size = FCOE_SCQE_SZ;

	fcoe_cap->srqc_size = FCOE_SRQC_SZ;
	fcoe_cap->srqe_size = FCOE_SRQE_SZ;
}

static void toe_param_fix(struct hinic_hwdev *dev)
{
	struct service_cap *cap = &dev->cfg_mgmt->svc_cap;
	struct toe_service_cap *toe_cap = &cap->toe_cap;

	toe_cap->pctx_sz = TOE_PCTX_SZ;
	toe_cap->scqc_sz = TOE_CQC_SZ;
}

static void fc_param_fix(struct hinic_hwdev *dev)
{
	struct service_cap *cap = &dev->cfg_mgmt->svc_cap;
	struct fc_service_cap *fc_cap = &cap->fc_cap;

	fc_cap->parent_qpc_size = FC_PCTX_SZ;
	fc_cap->child_qpc_size = FC_CCTX_SZ;
	fc_cap->sqe_size = FC_SQE_SZ;

	fc_cap->scqc_size = FC_SCQC_SZ;
	fc_cap->scqe_size = FC_SCQE_SZ;

	fc_cap->srqc_size = FC_SRQC_SZ;
	fc_cap->srqe_size = FC_SRQE_SZ;
}

static void ovs_param_fix(struct hinic_hwdev *dev)
{
	struct service_cap *cap = &dev->cfg_mgmt->svc_cap;
	struct ovs_service_cap *ovs_cap = &cap->ovs_cap;

	ovs_cap->pctx_sz = OVS_PCTX_SZ;
	ovs_cap->scqc_sz = OVS_SCQC_SZ;
}

static void acl_param_fix(struct hinic_hwdev *dev)
{
	struct service_cap *cap = &dev->cfg_mgmt->svc_cap;
	struct acl_service_cap *acl_cap = &cap->acl_cap;

	acl_cap->pctx_sz = ACL_PCTX_SZ;
	acl_cap->scqc_sz = ACL_SCQC_SZ;
}

static void init_service_param(struct hinic_hwdev *dev)
{
	if (IS_NIC_TYPE(dev))
		nic_param_fix(dev);

	if (IS_RDMA_TYPE(dev))
		rdma_param_fix(dev);

	if (IS_FCOE_TYPE(dev))
		fcoe_param_fix(dev);

	if (IS_TOE_TYPE(dev))
		toe_param_fix(dev);

	if (IS_FC_TYPE(dev))
		fc_param_fix(dev);

	if (IS_OVS_TYPE(dev))
		ovs_param_fix(dev);

	if (IS_ACL_TYPE(dev))
		acl_param_fix(dev);
}

static void cfg_get_eq_num(struct hinic_hwdev *dev)
{
	struct cfg_eq_info *eq_info = &dev->cfg_mgmt->eq_info;

	eq_info->num_ceq = dev->hwif->attr.num_ceqs;
	eq_info->num_ceq_remain = eq_info->num_ceq;
}

static int cfg_init_eq(struct hinic_hwdev *dev)
{
	struct cfg_mgmt_info *cfg_mgmt = dev->cfg_mgmt;
	struct cfg_eq *eq;
	u8 num_ceq, i = 0;

	cfg_get_eq_num(dev);
	num_ceq = cfg_mgmt->eq_info.num_ceq;

	sdk_info(dev->dev_hdl, "Cfg mgmt: ceqs=0x%x, remain=0x%x\n",
		 cfg_mgmt->eq_info.num_ceq, cfg_mgmt->eq_info.num_ceq_remain);

	if (!num_ceq) {
		sdk_err(dev->dev_hdl, "Ceq num cfg in fw is zero\n");
		return -EFAULT;
	}
	eq = kcalloc(num_ceq, sizeof(*eq), GFP_KERNEL);
	if (!eq)
		return -ENOMEM;

	for (i = 0; i < num_ceq; ++i) {
		eq[i].eqn = i;
		eq[i].free = CFG_FREE;
		eq[i].type = SERVICE_T_MAX;
	}

	cfg_mgmt->eq_info.eq = eq;
	mutex_init(&cfg_mgmt->eq_info.eq_mutex);

	return 0;
}

int hinic_dev_ver_info(void *hwdev, struct dev_version_info *ver)
{
	struct hinic_hwdev *dev = hwdev;
	struct cfg_mgmt_info *cfg_mgmt;

	if (!hwdev || !ver)
		return -EINVAL;

	cfg_mgmt = dev->cfg_mgmt;

	memcpy(ver, &cfg_mgmt->svc_cap.dev_ver_info, sizeof(*ver));

	return 0;
}
EXPORT_SYMBOL(hinic_dev_ver_info);

int hinic_vector_to_eqn(void *hwdev, enum hinic_service_type type, int vector)
{
	struct hinic_hwdev *dev = hwdev;
	struct cfg_mgmt_info *cfg_mgmt;
	struct cfg_eq *eq;
	int eqn = -EINVAL;

	if (!hwdev || vector < 0)
		return -EINVAL;

	if (type != SERVICE_T_ROCE && type != SERVICE_T_IWARP) {
		sdk_err(dev->dev_hdl,
			"Service type :%d, only RDMA service could get eqn by vector.\n",
			type);
		return -EINVAL;
	}

	cfg_mgmt = dev->cfg_mgmt;
	vector = (vector % cfg_mgmt->eq_info.num_ceq) + CFG_RDMA_CEQ_BASE;

	eq = cfg_mgmt->eq_info.eq;
	if ((eq[vector].type == SERVICE_T_ROCE ||
	     eq[vector].type == SERVICE_T_IWARP) &&
	     eq[vector].free == CFG_BUSY)
		eqn = eq[vector].eqn;

	return eqn;
}
EXPORT_SYMBOL(hinic_vector_to_eqn);

static int cfg_init_interrupt(struct hinic_hwdev *dev)
{
	struct cfg_mgmt_info *cfg_mgmt = dev->cfg_mgmt;
	struct cfg_irq_info *irq_info = &cfg_mgmt->irq_param_info;
	u16 intr_num = dev->hwif->attr.num_irqs;

	if (!intr_num) {
		sdk_err(dev->dev_hdl, "Irq num cfg in fw is zero\n");
		return -EFAULT;
	}
	irq_info->alloc_info = kcalloc(intr_num, sizeof(*irq_info->alloc_info),
				       GFP_KERNEL);
	if (!irq_info->alloc_info)
		return -ENOMEM;

	irq_info->num_irq_hw = intr_num;

	/* Production requires VF only surppots MSI-X */
	if (HINIC_FUNC_TYPE(dev) == TYPE_VF)
		cfg_mgmt->svc_cap.interrupt_type = INTR_TYPE_MSIX;
	else
		cfg_mgmt->svc_cap.interrupt_type = intr_mode;
	mutex_init(&irq_info->irq_mutex);
	return 0;
}

static int cfg_enable_interrupt(struct hinic_hwdev *dev)
{
	struct cfg_mgmt_info *cfg_mgmt = dev->cfg_mgmt;
	u16 nreq = cfg_mgmt->irq_param_info.num_irq_hw;

	void *pcidev = dev->pcidev_hdl;
	struct irq_alloc_info_st *irq_info;
	struct msix_entry *entry;
	u16 i = 0;
	int actual_irq;

	irq_info = cfg_mgmt->irq_param_info.alloc_info;

	sdk_info(dev->dev_hdl, "Interrupt type: %d, irq num: %d.\n",
		 cfg_mgmt->svc_cap.interrupt_type, nreq);

	switch (cfg_mgmt->svc_cap.interrupt_type) {
	case INTR_TYPE_MSIX:
		if (!nreq) {
			sdk_err(dev->dev_hdl, "Interrupt number cannot be zero\n");
			return -EINVAL;
		}
		entry = kcalloc(nreq, sizeof(*entry), GFP_KERNEL);
		if (!entry)
			return -ENOMEM;

		for (i = 0; i < nreq; i++)
			entry[i].entry = i;

		actual_irq = pci_enable_msix_range(pcidev, entry,
						   VECTOR_THRESHOLD, nreq);
		if (actual_irq < 0) {
			sdk_err(dev->dev_hdl, "Alloc msix entries with threshold 2 failed.\n");
			kfree(entry);
			return -ENOMEM;
		}

		nreq = (u16)actual_irq;
		cfg_mgmt->irq_param_info.num_total = nreq;
		cfg_mgmt->irq_param_info.num_irq_remain = nreq;
		sdk_info(dev->dev_hdl, "Request %d msix vector success.\n",
			 nreq);

		for (i = 0; i < nreq; ++i) {
			/* u16 driver uses to specify entry, OS writes */
			irq_info[i].info.msix_entry_idx = entry[i].entry;
			/* u32 kernel uses to write allocated vector */
			irq_info[i].info.irq_id = entry[i].vector;
			irq_info[i].type = SERVICE_T_MAX;
			irq_info[i].free = CFG_FREE;
		}

		kfree(entry);

		break;

	default:
		sdk_err(dev->dev_hdl, "Unsupport interrupt type %d\n",
			cfg_mgmt->svc_cap.interrupt_type);
		break;
	}

	return 0;
}

int hinic_alloc_irqs(void *hwdev, enum hinic_service_type type, u16 num,
		     struct irq_info *irq_info_array, u16 *act_num)
{
	struct hinic_hwdev *dev = hwdev;
	struct cfg_mgmt_info *cfg_mgmt;
	struct cfg_irq_info *irq_info;
	struct irq_alloc_info_st *alloc_info;
	int max_num_irq;
	u16 free_num_irq;
	int i, j;

	if (!hwdev || !irq_info_array || !act_num)
		return -EINVAL;

	cfg_mgmt = dev->cfg_mgmt;
	irq_info = &cfg_mgmt->irq_param_info;
	alloc_info = irq_info->alloc_info;
	max_num_irq = irq_info->num_total;
	free_num_irq = irq_info->num_irq_remain;

	mutex_lock(&irq_info->irq_mutex);

	if (num > free_num_irq) {
		if (free_num_irq == 0) {
			sdk_err(dev->dev_hdl,
				"no free irq resource in cfg mgmt.\n");
			mutex_unlock(&irq_info->irq_mutex);
			return -ENOMEM;
		}

		sdk_warn(dev->dev_hdl, "only %d irq resource in cfg mgmt.\n",
			 free_num_irq);
		num = free_num_irq;
	}

	*act_num = 0;

	for (i = 0; i < num; i++) {
		for (j = 0; j < max_num_irq; j++) {
			if (alloc_info[j].free == CFG_FREE) {
				if (irq_info->num_irq_remain == 0) {
					sdk_err(dev->dev_hdl, "No free irq resource in cfg mgmt\n");
					mutex_unlock(&irq_info->irq_mutex);
					return -EINVAL;
				}
				alloc_info[j].type = type;
				alloc_info[j].free = CFG_BUSY;

				irq_info_array[i].msix_entry_idx =
					alloc_info[j].info.msix_entry_idx;
				irq_info_array[i].irq_id =
					alloc_info[j].info.irq_id;
				(*act_num)++;
				irq_info->num_irq_remain--;

				break;
			}
		}
	}

	mutex_unlock(&irq_info->irq_mutex);
	return 0;
}
EXPORT_SYMBOL(hinic_alloc_irqs);

void hinic_free_irq(void *hwdev, enum hinic_service_type type, u32 irq_id)
{
	struct hinic_hwdev *dev = hwdev;
	struct cfg_mgmt_info *cfg_mgmt;
	struct cfg_irq_info *irq_info;
	struct irq_alloc_info_st *alloc_info;
	int max_num_irq;
	int i;

	if (!hwdev)
		return;

	cfg_mgmt = dev->cfg_mgmt;
	irq_info = &cfg_mgmt->irq_param_info;
	alloc_info = irq_info->alloc_info;
	max_num_irq = irq_info->num_total;

	mutex_lock(&irq_info->irq_mutex);

	for (i = 0; i < max_num_irq; i++) {
		if (irq_id == alloc_info[i].info.irq_id &&
		    type == alloc_info[i].type) {
			if (alloc_info[i].free == CFG_BUSY) {
				alloc_info[i].free = CFG_FREE;
				irq_info->num_irq_remain++;
				if (irq_info->num_irq_remain > max_num_irq) {
					sdk_err(dev->dev_hdl, "Find target,but over range\n");
					mutex_unlock(&irq_info->irq_mutex);
					return;
				}
				break;
			}
		}
	}

	if (i >= max_num_irq)
		sdk_warn(dev->dev_hdl, "Irq %d don`t need to free\n", irq_id);

	mutex_unlock(&irq_info->irq_mutex);
}
EXPORT_SYMBOL(hinic_free_irq);

int hinic_vector_to_irq(void *hwdev, enum hinic_service_type type, int vector)
{
	struct hinic_hwdev *dev = hwdev;
	struct cfg_mgmt_info *cfg_mgmt;
	struct irq_alloc_info_st *irq_info;
	int irq = -EINVAL;

	if (!hwdev)
		return -EINVAL;

	cfg_mgmt = dev->cfg_mgmt;
	if (type != SERVICE_T_ROCE && type != SERVICE_T_IWARP) {
		sdk_err(dev->dev_hdl,
			"Service type: %u, only RDMA service could get eqn by vector\n",
			type);
		return -EINVAL;
	}

	/* Current RDMA CEQ are 2 - 31, will change in the future */
	vector = ((vector % cfg_mgmt->eq_info.num_ceq) + CFG_RDMA_CEQ_BASE);

	irq_info = cfg_mgmt->irq_param_info.alloc_info;
	if (irq_info[vector].type == SERVICE_T_ROCE ||
	    irq_info[vector].type == SERVICE_T_IWARP)
		if (irq_info[vector].free == CFG_BUSY)
			irq = (int)irq_info[vector].info.irq_id;

	return irq;
}
EXPORT_SYMBOL(hinic_vector_to_irq);

int hinic_alloc_ceqs(void *hwdev, enum hinic_service_type type, int num,
		     int *ceq_id_array, int *act_num)
{
	struct hinic_hwdev *dev = hwdev;
	struct cfg_mgmt_info *cfg_mgmt;
	struct cfg_eq_info *eq;
	int free_ceq;
	int i, j;

	if (!hwdev || !ceq_id_array || !act_num)
		return -EINVAL;

	cfg_mgmt = dev->cfg_mgmt;
	eq = &cfg_mgmt->eq_info;
	free_ceq = eq->num_ceq_remain;

	mutex_lock(&eq->eq_mutex);

	if (num > free_ceq) {
		if (free_ceq <= 0) {
			sdk_err(dev->dev_hdl, "No free ceq resource in cfg mgmt\n");
			mutex_unlock(&eq->eq_mutex);
			return -ENOMEM;
		}

		sdk_warn(dev->dev_hdl, "Only %d ceq resource in cfg mgmt\n",
			 free_ceq);
	}

	*act_num = 0;

	num = min(num, eq->num_ceq - CFG_RDMA_CEQ_BASE);
	for (i = 0; i < num; i++) {
		if (eq->num_ceq_remain == 0) {
			sdk_warn(dev->dev_hdl, "Alloc %d ceqs, less than required %d ceqs\n",
				 *act_num, num);
			mutex_unlock(&eq->eq_mutex);
			return 0;
		}

		for (j = CFG_RDMA_CEQ_BASE; j < eq->num_ceq; j++) {
			if (eq->eq[j].free == CFG_FREE) {
				eq->eq[j].type = type;
				eq->eq[j].free = CFG_BUSY;
				eq->num_ceq_remain--;
				ceq_id_array[i] = eq->eq[j].eqn;
				(*act_num)++;
				break;
			}
		}
	}

	mutex_unlock(&eq->eq_mutex);
	return 0;
}
EXPORT_SYMBOL(hinic_alloc_ceqs);

void hinic_free_ceq(void *hwdev, enum hinic_service_type type, int ceq_id)
{
	struct hinic_hwdev *dev = hwdev;
	struct cfg_mgmt_info *cfg_mgmt;
	struct cfg_eq_info *eq;
	u8 num_ceq;
	u8 i = 0;

	if (!hwdev)
		return;

	cfg_mgmt = dev->cfg_mgmt;
	eq = &cfg_mgmt->eq_info;
	num_ceq = eq->num_ceq;

	mutex_lock(&eq->eq_mutex);

	for (i = 0; i < num_ceq; i++) {
		if (ceq_id == eq->eq[i].eqn &&
		    type == cfg_mgmt->eq_info.eq[i].type) {
			if (eq->eq[i].free == CFG_BUSY) {
				eq->eq[i].free = CFG_FREE;
				eq->num_ceq_remain++;
				if (eq->num_ceq_remain > num_ceq)
					eq->num_ceq_remain %= num_ceq;

				mutex_unlock(&eq->eq_mutex);
				return;
			}
		}
	}

	if (i >= num_ceq)
		sdk_warn(dev->dev_hdl, "ceq %d don`t need to free.\n", ceq_id);

	mutex_unlock(&eq->eq_mutex);
}
EXPORT_SYMBOL(hinic_free_ceq);

static int cfg_mbx_pf_proc_vf_msg(void *hwdev, u16 vf_id, u8 cmd, void *buf_in,
				  u16 in_size, void *buf_out, u16 *out_size)
{
	struct hinic_hwdev *dev = hwdev;
	struct hinic_dev_cap *dev_cap = buf_out;
	struct service_cap *cap = &dev->cfg_mgmt->svc_cap;
	struct nic_service_cap *nic_cap = &cap->nic_cap;
	struct dev_roce_svc_own_cap *roce_cap =
		&cap->rdma_cap.dev_rdma_cap.roce_own_cap;
	struct dev_iwarp_svc_own_cap *iwarp_cap =
		&cap->rdma_cap.dev_rdma_cap.iwarp_own_cap;
	struct dev_ovs_svc_cap *ovs_cap = &cap->ovs_cap.dev_ovs_cap;
	struct hinic_dev_cap dev_cap_tmp = {0};
	u16 out_len = 0;
	u16 func_id;
	int err;

	memset(dev_cap, 0, sizeof(*dev_cap));

	if (cap->sf_svc_attr.ft_en)
		dev_cap->sf_svc_attr |= SF_SVC_FT_BIT;
	else
		dev_cap->sf_svc_attr &= ~SF_SVC_FT_BIT;

	if (cap->sf_svc_attr.rdma_en)
		dev_cap->sf_svc_attr |= SF_SVC_RDMA_BIT;
	else
		dev_cap->sf_svc_attr &= ~SF_SVC_RDMA_BIT;

	dev_cap->sf_en_vf = cap->sf_svc_attr.sf_en_vf;

	dev_cap->host_id = cap->host_id;
	dev_cap->ep_id = cap->ep_id;
	dev_cap->intr_type = cap->interrupt_type;
	dev_cap->max_cos_id = cap->max_cos_id;
	dev_cap->er_id = cap->er_id;
	dev_cap->port_id = cap->port_id;
	dev_cap->max_vf = cap->max_vf;
	dev_cap->svc_cap_en = cap->chip_svc_type;
	dev_cap->host_total_func = cap->host_total_function;
	dev_cap->host_oq_id_mask_val = cap->host_oq_id_mask_val;
	dev_cap->net_port_mode = cap->net_port_mode;

	/* Parameters below is uninitialized because NIC and ROCE not use it
	 * max_connect_num
	 * max_stick2cache_num
	 * bfilter_start_addr
	 * bfilter_len
	 * hash_bucket_num
	 * cfg_file_ver
	 */

	/* NIC VF resources */
	dev_cap->nic_max_sq = nic_cap->vf_max_sqs;
	dev_cap->nic_max_rq = nic_cap->vf_max_rqs;

	/* ROCE VF resources */
	dev_cap->roce_max_qp = roce_cap->vf_max_qps;
	dev_cap->roce_max_cq = roce_cap->vf_max_cqs;
	dev_cap->roce_max_srq = roce_cap->vf_max_srqs;
	dev_cap->roce_max_mpt = roce_cap->vf_max_mpts;

	dev_cap->roce_cmtt_cl_start = roce_cap->cmtt_cl_start;
	dev_cap->roce_cmtt_cl_end = roce_cap->cmtt_cl_end;
	dev_cap->roce_cmtt_cl_size = roce_cap->cmtt_cl_sz;

	dev_cap->roce_dmtt_cl_start = roce_cap->dmtt_cl_start;
	dev_cap->roce_dmtt_cl_end = roce_cap->dmtt_cl_end;
	dev_cap->roce_dmtt_cl_size = roce_cap->dmtt_cl_sz;

	dev_cap->roce_wqe_cl_start = roce_cap->wqe_cl_start;
	dev_cap->roce_wqe_cl_end = roce_cap->wqe_cl_end;
	dev_cap->roce_wqe_cl_size = roce_cap->wqe_cl_sz;

	/* Iwarp VF resources */
	dev_cap->iwarp_max_qp = iwarp_cap->vf_max_qps;
	dev_cap->iwarp_max_cq = iwarp_cap->vf_max_cqs;
	dev_cap->iwarp_max_mpt = iwarp_cap->vf_max_mpts;

	/* OVS VF resources */
	dev_cap->ovs_max_qpc = ovs_cap->max_pctxs;
	dev_cap->ovs_dq_en = ovs_cap->dynamic_qp_en;

	*out_size = sizeof(*dev_cap);

	out_len = sizeof(dev_cap_tmp);
	/* fixed qnum in ovs mode */
	func_id = vf_id + hinic_glb_pf_vf_offset(hwdev);
	dev_cap_tmp.func_id = func_id;
	err = hinic_pf_msg_to_mgmt_sync(dev, HINIC_MOD_CFGM, HINIC_CFG_FUNC_CAP,
					&dev_cap_tmp, sizeof(dev_cap_tmp),
					&dev_cap_tmp, &out_len, 0);
	if (err && err != HINIC_DEV_BUSY_ACTIVE_FW &&
	    err != HINIC_MBOX_PF_BUSY_ACTIVE_FW) {
		sdk_err(dev->dev_hdl,
			"Get func_id: %u capability from FW failed, err: %d, status: 0x%x, out_size: 0x%x\n",
			func_id, err, dev_cap_tmp.status, out_len);
		return -EFAULT;
	} else if (err) {
		return err;
	}

	dev_cap->nic_max_sq = dev_cap_tmp.nic_max_sq + 1;
	dev_cap->nic_max_rq = dev_cap_tmp.nic_max_rq + 1;
	dev_cap->max_queue_allowed = dev_cap_tmp.max_queue_allowed;

	sdk_info(dev->dev_hdl, "func_id(%u) %s qnum %u max_queue_allowed %u\n",
		 func_id, (ovs_cap->dynamic_qp_en ? "dynamic" : "fixed"),
		 dev_cap->nic_max_sq, dev_cap->max_queue_allowed);

	return 0;
}

static int cfg_mbx_ppf_proc_msg(void *hwdev, u16 pf_id, u16 vf_id, u8 cmd,
				void *buf_in, u16 in_size, void *buf_out,
				u16 *out_size)
{
	struct hinic_hwdev *dev = hwdev;

	sdk_info(dev->dev_hdl, "ppf receive other pf cfgmgmt cmd %d mbox msg\n",
		 cmd);

	return hinic_ppf_process_mbox_msg(hwdev, pf_id, vf_id, HINIC_MOD_CFGM,
					  cmd, buf_in, in_size, buf_out,
					  out_size);
}

static int cfg_mbx_vf_proc_msg(void *hwdev, u8 cmd, void *buf_in, u16 in_size,
			       void *buf_out, u16 *out_size)
{
	struct hinic_hwdev *dev = hwdev;

	*out_size = 0;
	sdk_err(dev->dev_hdl, "VF msg callback not supported\n");

	return -EOPNOTSUPP;
}

static int cfg_mbx_init(struct hinic_hwdev *dev, struct cfg_mgmt_info *cfg_mgmt)
{
	int err;
	enum func_type type = dev->hwif->attr.func_type;

	if (type == TYPE_PF) {
		err = hinic_register_pf_mbox_cb(dev, HINIC_MOD_CFGM,
						cfg_mbx_pf_proc_vf_msg);
		if (err) {
			sdk_err(dev->dev_hdl,
				"PF: Register PF mailbox callback failed\n");
			return err;
		}
	} else if (type == TYPE_PPF) {
		err = hinic_register_ppf_mbox_cb(dev, HINIC_MOD_CFGM,
						 cfg_mbx_ppf_proc_msg);
		if (err) {
			sdk_err(dev->dev_hdl,
				"PPF: Register PPF mailbox callback failed\n");
			return err;
		}

		err = hinic_register_pf_mbox_cb(dev, HINIC_MOD_CFGM,
						cfg_mbx_pf_proc_vf_msg);
		if (err) {
			sdk_err(dev->dev_hdl,
				"PPF: Register PF mailbox callback failed\n");
			hinic_unregister_ppf_mbox_cb(dev, HINIC_MOD_CFGM);
			return err;
		}
	} else if (type == TYPE_VF) {
		err = hinic_register_vf_mbox_cb(dev, HINIC_MOD_CFGM,
						cfg_mbx_vf_proc_msg);
		if (err) {
			sdk_err(dev->dev_hdl,
				"VF: Register VF mailbox callback failed\n");
			return err;
		}
	} else {
		sdk_err(dev->dev_hdl, "Invalid func_type: %d, not supported\n",
			type);
		return -EINVAL;
	}

	return 0;
}

static void cfg_mbx_cleanup(struct hinic_hwdev *dev)
{
	hinic_unregister_ppf_mbox_cb(dev, HINIC_MOD_CFGM);
	hinic_unregister_pf_mbox_cb(dev, HINIC_MOD_CFGM);
	hinic_unregister_vf_mbox_cb(dev, HINIC_MOD_CFGM);
}

int init_cfg_mgmt(struct hinic_hwdev *dev)
{
	int err;
	struct cfg_mgmt_info *cfg_mgmt;

	cfg_mgmt = kzalloc(sizeof(*cfg_mgmt), GFP_KERNEL);
	if (!cfg_mgmt)
		return -ENOMEM;

	dev->cfg_mgmt = cfg_mgmt;
	cfg_mgmt->hwdev = dev;

	err = cfg_init_eq(dev);
	if (err) {
		sdk_err(dev->dev_hdl, "Failed to init cfg event queue, err: %d\n",
			err);
		goto free_mgmt_mem;
	}

	err = cfg_init_interrupt(dev);
	if (err) {
		sdk_err(dev->dev_hdl, "Failed to init cfg interrupt, err: %d\n",
			err);
		goto free_eq_mem;
	}

	err = cfg_enable_interrupt(dev);
	if (err) {
		sdk_err(dev->dev_hdl, "Failed to enable cfg interrupt, err: %d\n",
			err);
		goto free_interrupt_mem;
	}

	return 0;

free_interrupt_mem:
	kfree(cfg_mgmt->irq_param_info.alloc_info);
	mutex_deinit(&((cfg_mgmt->irq_param_info).irq_mutex));
	cfg_mgmt->irq_param_info.alloc_info = NULL;

free_eq_mem:
	kfree(cfg_mgmt->eq_info.eq);
	mutex_deinit(&cfg_mgmt->eq_info.eq_mutex);
	cfg_mgmt->eq_info.eq = NULL;

free_mgmt_mem:
	kfree(cfg_mgmt);
	return err;
}

void free_cfg_mgmt(struct hinic_hwdev *dev)
{
	struct cfg_mgmt_info *cfg_mgmt = dev->cfg_mgmt;

	/* if the allocated resource were recycled */
	if (cfg_mgmt->irq_param_info.num_irq_remain !=
	    cfg_mgmt->irq_param_info.num_total ||
	    cfg_mgmt->eq_info.num_ceq_remain != cfg_mgmt->eq_info.num_ceq)
		sdk_err(dev->dev_hdl, "Can't reclaim all irq and event queue, please check\n");

	switch (cfg_mgmt->svc_cap.interrupt_type) {
	case INTR_TYPE_MSIX:
		pci_disable_msix(dev->pcidev_hdl);
		break;

	case INTR_TYPE_MSI:
		pci_disable_msi(dev->pcidev_hdl);
		break;

	case INTR_TYPE_INT:
	default:
		break;
	}

	kfree(cfg_mgmt->irq_param_info.alloc_info);
	cfg_mgmt->irq_param_info.alloc_info = NULL;
	mutex_deinit(&((cfg_mgmt->irq_param_info).irq_mutex));

	kfree(cfg_mgmt->eq_info.eq);
	cfg_mgmt->eq_info.eq = NULL;
	mutex_deinit(&cfg_mgmt->eq_info.eq_mutex);

	kfree(cfg_mgmt);
}

int init_capability(struct hinic_hwdev *dev)
{
	int err;
	struct cfg_mgmt_info *cfg_mgmt = dev->cfg_mgmt;

	set_cfg_test_param(cfg_mgmt);

	err = cfg_mbx_init(dev, cfg_mgmt);
	if (err) {
		sdk_err(dev->dev_hdl, "Configure mailbox init failed, err: %d\n",
			err);
		return err;
	}

	cfg_mgmt->svc_cap.sf_svc_attr.ft_pf_en = false;
	cfg_mgmt->svc_cap.sf_svc_attr.rdma_pf_en = false;

	err = get_dev_cap(dev);
	if (err) {
		cfg_mbx_cleanup(dev);
		return err;
	}

	init_service_param(dev);

	sdk_info(dev->dev_hdl, "Init capability success\n");
	return 0;
}

void free_capability(struct hinic_hwdev *dev)
{
	cfg_mbx_cleanup(dev);
	sdk_info(dev->dev_hdl, "Free capability success");
}

/* 0 - MSIx, 1 - MSI, 2 - INTx */
enum intr_type hinic_intr_type(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return INTR_TYPE_NONE;

	return dev->cfg_mgmt->svc_cap.interrupt_type;
}
EXPORT_SYMBOL(hinic_intr_type);

bool hinic_support_nic(void *hwdev, struct nic_service_cap *cap)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	if (!IS_NIC_TYPE(dev))
		return false;

	if (cap)
		memcpy(cap, &dev->cfg_mgmt->svc_cap.nic_cap, sizeof(*cap));

	return true;
}
EXPORT_SYMBOL(hinic_support_nic);

bool hinic_support_roce(void *hwdev, struct rdma_service_cap *cap)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	if (!IS_ROCE_TYPE(dev))
		return false;

	if (cap)
		memcpy(cap, &dev->cfg_mgmt->svc_cap.rdma_cap, sizeof(*cap));

	return true;
}
EXPORT_SYMBOL(hinic_support_roce);

bool hinic_support_fcoe(void *hwdev, struct fcoe_service_cap *cap)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	if (!IS_FCOE_TYPE(dev))
		return false;

	if (cap)
		memcpy(cap, &dev->cfg_mgmt->svc_cap.fcoe_cap, sizeof(*cap));

	return true;
}
EXPORT_SYMBOL(hinic_support_fcoe);

/* Only PPF support it, PF is not */
bool hinic_support_toe(void *hwdev, struct toe_service_cap *cap)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	if (!IS_TOE_TYPE(dev))
		return false;

	if (cap)
		memcpy(cap, &dev->cfg_mgmt->svc_cap.toe_cap, sizeof(*cap));

	return true;
}
EXPORT_SYMBOL(hinic_support_toe);

bool hinic_support_iwarp(void *hwdev, struct rdma_service_cap *cap)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	if (!IS_IWARP_TYPE(dev))
		return false;

	if (cap)
		memcpy(cap, &dev->cfg_mgmt->svc_cap.rdma_cap, sizeof(*cap));

	return true;
}
EXPORT_SYMBOL(hinic_support_iwarp);

bool hinic_support_fc(void *hwdev, struct fc_service_cap *cap)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	if (!IS_FC_TYPE(dev))
		return false;

	if (cap)
		memcpy(cap, &dev->cfg_mgmt->svc_cap.fc_cap, sizeof(*cap));

	return true;
}
EXPORT_SYMBOL(hinic_support_fc);

bool hinic_support_fic(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	if (!IS_FIC_TYPE(dev))
		return false;

	return true;
}
EXPORT_SYMBOL(hinic_support_fic);

bool hinic_support_ovs(void *hwdev, struct ovs_service_cap *cap)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	if (!IS_OVS_TYPE(dev))
		return false;

	if (cap)
		memcpy(cap, &dev->cfg_mgmt->svc_cap.ovs_cap, sizeof(*cap));

	return true;
}
EXPORT_SYMBOL(hinic_support_ovs);

bool hinic_support_acl(void *hwdev, struct acl_service_cap *cap)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	if (!IS_ACL_TYPE(dev))
		return false;

	if (cap)
		memcpy(cap, &dev->cfg_mgmt->svc_cap.acl_cap, sizeof(*cap));

	return true;
}
EXPORT_SYMBOL(hinic_support_acl);

bool hinic_support_rdma(void *hwdev, struct rdma_service_cap *cap)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	if (!IS_RDMA_TYPE(dev))
		return false;

	if (cap)
		memcpy(cap, &dev->cfg_mgmt->svc_cap.rdma_cap, sizeof(*cap));

	return true;
}
EXPORT_SYMBOL(hinic_support_rdma);

bool hinic_support_ft(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	if (!IS_FT_TYPE(dev))
		return false;

	return true;
}
EXPORT_SYMBOL(hinic_support_ft);

bool hinic_support_dynamic_q(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	return dev->cfg_mgmt->svc_cap.nic_cap.dynamic_qp ? true : false;
}

bool hinic_func_for_mgmt(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	if (dev->cfg_mgmt->svc_cap.chip_svc_type >= CFG_SVC_NIC_BIT0)
		return false;
	else
		return true;
}

bool hinic_func_for_hwpt(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	if (IS_HWPT_TYPE(dev))
		return true;
	else
		return false;
}

bool hinic_func_for_pt(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	if (dev->cfg_mgmt->svc_cap.force_up)
		return true;
	else
		return false;
}

int cfg_set_func_sf_en(void *hwdev, u32 enbits, u32 enmask)
{
	struct hinic_hwdev *dev = hwdev;
	struct nic_misc_func_sf_enbits *func_sf_enbits;
	u16 out_size = sizeof(*func_sf_enbits);
	u16 glb_func_idx;
	u16 api_info_len;
	int err;

	api_info_len = sizeof(struct nic_misc_func_sf_enbits);
	func_sf_enbits = kzalloc(api_info_len, GFP_KERNEL);
	if (!func_sf_enbits) {
		sdk_err(dev->dev_hdl, "Alloc cfg api info failed\n");
		return -ENOMEM;
	}

	err = hinic_global_func_id_get(dev, &glb_func_idx);
	if (err) {
		kfree(func_sf_enbits);
		return err;
	}

	func_sf_enbits->stateful_enbits = enbits;
	func_sf_enbits->stateful_enmask = enmask;
	func_sf_enbits->function_id = glb_func_idx;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_L2NIC,
				     HINIC_MISC_SET_FUNC_SF_ENBITS,
				     (void *)func_sf_enbits, api_info_len,
				     (void *)func_sf_enbits, &out_size,
				     VSW_UP_CFG_TIMEOUT);
	if (err || !out_size || func_sf_enbits->status) {
		sdk_err(dev->dev_hdl,
			"Failed to set stateful enable, err: %d, status: 0x%x, out_size: 0x%x\n",
			err, func_sf_enbits->status, out_size);
		kfree(func_sf_enbits);
		return -EFAULT;
	}

	kfree(func_sf_enbits);
	return 0;
}

int cfg_get_func_sf_en(void *hwdev, u32 *enbits)
{
	struct nic_misc_func_sf_enbits *func_sf_enbits;
	struct hinic_hwdev *dev = hwdev;
	u16 out_size = sizeof(*func_sf_enbits);
	u16 glb_func_idx;
	u16 api_info_len;
	int err;

	api_info_len   = sizeof(struct nic_misc_func_sf_enbits);
	func_sf_enbits = kzalloc(api_info_len, GFP_KERNEL);
	if (!func_sf_enbits) {
		sdk_err(dev->dev_hdl, "Alloc cfg api info failed\n");
		return -ENOMEM;
	}

	err = hinic_global_func_id_get(dev, &glb_func_idx);
	if (err) {
		kfree(func_sf_enbits);
		return err;
	}

	func_sf_enbits->function_id = glb_func_idx;

	err = hinic_msg_to_mgmt_sync(hwdev, HINIC_MOD_L2NIC,
				     HINIC_MISC_GET_FUNC_SF_ENBITS,
				     (void *)func_sf_enbits, api_info_len,
				     (void *)func_sf_enbits, &out_size,
				     VSW_UP_CFG_TIMEOUT);
	if (err || !out_size || func_sf_enbits->status) {
		sdk_err(dev->dev_hdl, "Failed to get stateful enable, err: %d, status: 0x%x, out_size: 0x%x\n",
			err, func_sf_enbits->status, out_size);
		kfree(func_sf_enbits);
		return -EFAULT;
	}

	*enbits = func_sf_enbits->stateful_enbits;

	kfree(func_sf_enbits);
	return 0;
}

int hinic_set_toe_enable(void *hwdev, bool enable)
{
	u32 enbits;
	u32 enmask;

	if (!hwdev)
		return -EINVAL;

	enbits = VSW_SET_STATEFUL_BITS_TOE((u16)enable);
	enmask = VSW_SET_STATEFUL_BITS_TOE(0x1U);

	return cfg_set_func_sf_en(hwdev, enbits, enmask);
}
EXPORT_SYMBOL(hinic_set_toe_enable);

bool hinic_get_toe_enable(void *hwdev)
{
	int err;
	u32 enbits;

	if (!hwdev)
		return false;

	err = cfg_get_func_sf_en(hwdev, &enbits);
	if (err)
		return false;

	return VSW_GET_STATEFUL_BITS_TOE(enbits);
}
EXPORT_SYMBOL(hinic_get_toe_enable);

int hinic_set_fcoe_enable(void *hwdev, bool enable)
{
	u32 enbits;
	u32 enmask;

	if (!hwdev)
		return -EINVAL;

	enbits = VSW_SET_STATEFUL_BITS_FCOE((u16)enable);
	enmask = VSW_SET_STATEFUL_BITS_FCOE(0x1U);

	return cfg_set_func_sf_en(hwdev, enbits, enmask);
}
EXPORT_SYMBOL(hinic_set_fcoe_enable);

bool hinic_get_fcoe_enable(void *hwdev)
{
	int err;
	u32 enbits;

	if (!hwdev)
		return false;

	err = cfg_get_func_sf_en(hwdev, &enbits);
	if (err)
		return false;

	return VSW_GET_STATEFUL_BITS_FCOE(enbits);
}
EXPORT_SYMBOL(hinic_get_fcoe_enable);

bool hinic_get_stateful_enable(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return false;

	return dev->cfg_mgmt->svc_cap.sf_en;
}
EXPORT_SYMBOL(hinic_get_stateful_enable);

u8 hinic_host_oq_id_mask(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting host oq id mask\n");
		return 0;
	}
	return dev->cfg_mgmt->svc_cap.host_oq_id_mask_val;
}
EXPORT_SYMBOL(hinic_host_oq_id_mask);

u8 hinic_host_id(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting host id\n");
		return 0;
	}
	return dev->cfg_mgmt->svc_cap.host_id;
}
EXPORT_SYMBOL(hinic_host_id);

u16 hinic_host_total_func(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting host total function number\n");
		return 0;
	}
	return dev->cfg_mgmt->svc_cap.host_total_function;
}
EXPORT_SYMBOL(hinic_host_total_func);

u16 hinic_func_max_qnum(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting function max queue number\n");
		return 0;
	}
	return dev->cfg_mgmt->svc_cap.max_sqs;
}
EXPORT_SYMBOL(hinic_func_max_qnum);

u16 hinic_func_max_nic_qnum(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting function max queue number\n");
		return 0;
	}
	return dev->cfg_mgmt->svc_cap.nic_cap.max_sqs;
}
EXPORT_SYMBOL(hinic_func_max_nic_qnum);

u8 hinic_ep_id(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting ep id\n");
		return 0;
	}
	return dev->cfg_mgmt->svc_cap.ep_id;
}
EXPORT_SYMBOL(hinic_ep_id);

u8 hinic_er_id(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting er id\n");
		return 0;
	}
	return dev->cfg_mgmt->svc_cap.er_id;
}
EXPORT_SYMBOL(hinic_er_id);

u8 hinic_physical_port_id(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting physical port id\n");
		return 0;
	}
	return dev->cfg_mgmt->svc_cap.port_id;
}
EXPORT_SYMBOL(hinic_physical_port_id);

u8 hinic_func_max_vf(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting max vf number\n");
		return 0;
	}
	return dev->cfg_mgmt->svc_cap.max_vf;
}
EXPORT_SYMBOL(hinic_func_max_vf);

u8 hinic_max_num_cos(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting max cos number\n");
		return 0;
	}
	return (u8)(dev->cfg_mgmt->svc_cap.max_cos_id + 1);
}
EXPORT_SYMBOL(hinic_max_num_cos);

u8 hinic_cos_valid_bitmap(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting cos valid bitmap\n");
		return 0;
	}
	return (u8)(dev->cfg_mgmt->svc_cap.cos_valid_bitmap);
}
EXPORT_SYMBOL(hinic_cos_valid_bitmap);

u8 hinic_net_port_mode(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting net port mode\n");
		return 0;
	}
	return dev->cfg_mgmt->svc_cap.net_port_mode;
}
EXPORT_SYMBOL(hinic_net_port_mode);


bool hinic_is_hwdev_mod_inited(void *hwdev, enum hinic_hwdev_init_state state)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev || state >= HINIC_HWDEV_MAX_INVAL_INITED)
		return false;

	return !!test_bit(state, &dev->func_state);
}

static int hinic_os_dep_init(struct hinic_hwdev *hwdev)
{

	hwdev->workq = create_singlethread_workqueue(HINIC_HW_WQ_NAME);
	if (!hwdev->workq) {
		sdk_err(hwdev->dev_hdl, "Failed to initialize hardware workqueue\n");
		return -EFAULT;
	}

	sema_init(&hwdev->recover_sem, 1);
	sema_init(&hwdev->fault_list_sem, 1);

	INIT_WORK(&hwdev->fault_work, hinic_fault_work_handler);

	return 0;
}

static void hinic_os_dep_deinit(struct hinic_hwdev *hwdev)
{

	destroy_work(&hwdev->fault_work);

	destroy_workqueue(hwdev->workq);

	down(&hwdev->fault_list_sem);


	up(&hwdev->fault_list_sem);

	sema_deinit(&hwdev->fault_list_sem);
	sema_deinit(&hwdev->recover_sem);
}

void hinic_ppf_hwdev_unreg(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return;

	down(&dev->ppf_sem);
	dev->ppf_hwdev = NULL;
	up(&dev->ppf_sem);

	sdk_info(dev->dev_hdl, "Unregister PPF hwdev\n");
}

void hinic_ppf_hwdev_reg(void *hwdev, void *ppf_hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return;

	down(&dev->ppf_sem);
	dev->ppf_hwdev = ppf_hwdev;
	up(&dev->ppf_sem);

	sdk_info(dev->dev_hdl, "Register PPF hwdev\n");
}

static int __vf_func_init(struct hinic_hwdev *hwdev)
{
	int err;

	err = hinic_vf_mbox_random_id_init(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to init vf mbox random id\n");
		return err;
	}
	err = hinic_vf_func_init(hwdev);
	if (err)
		nic_err(hwdev->dev_hdl, "Failed to init nic mbox\n");

	return err;
}

static int __hilink_phy_init(struct hinic_hwdev *hwdev)
{
	int err;

	if (!HINIC_IS_VF(hwdev)) {
		err = hinic_phy_init_status_judge(hwdev);
		if (err) {
			sdk_info(hwdev->dev_hdl, "Phy init failed\n");
			return err;
		}

		if (hinic_support_nic(hwdev, NULL))
			hinic_hilink_info_show(hwdev);
	}

	return 0;
}

/* Return:
 * 0: all success
 * >0: partitial success
 * <0: all failed
 */
int hinic_init_hwdev(struct hinic_init_para *para)
{
	struct hinic_hwdev *hwdev;
	int err;

	if (!(*para->hwdev)) {
		hwdev = kzalloc(sizeof(*hwdev), GFP_KERNEL);
		if (!hwdev)
			return -ENOMEM;

		*para->hwdev = hwdev;
		hwdev->adapter_hdl = para->adapter_hdl;
		hwdev->pcidev_hdl = para->pcidev_hdl;
		hwdev->dev_hdl = para->dev_hdl;
		hwdev->chip_node = para->chip_node;
		hwdev->ppf_hwdev = para->ppf_hwdev;
		sema_init(&hwdev->ppf_sem, 1);
		sema_init(&hwdev->func_sem, 1);
		hwdev->func_ref = 0;

		hwdev->chip_fault_stats = vzalloc(HINIC_CHIP_FAULT_SIZE);
		if (!hwdev->chip_fault_stats)
			goto alloc_chip_fault_stats_err;

		err = hinic_init_hwif(hwdev, para->cfg_reg_base,
				      para->intr_reg_base,
				      para->db_base_phy, para->db_base,
				      para->dwqe_mapping);
		if (err) {
			sdk_err(hwdev->dev_hdl, "Failed to init hwif\n");
			goto init_hwif_err;
		}
	} else {
		hwdev = *para->hwdev;
	}

	/* detect slave host according to BAR reg */
	detect_host_mode_pre(hwdev);

	if (IS_BMGW_SLAVE_HOST(hwdev) &&
	    (!hinic_get_master_host_mbox_enable(hwdev))) {
		set_bit(HINIC_HWDEV_NONE_INITED, &hwdev->func_state);
		sdk_info(hwdev->dev_hdl, "Master host not ready, init hwdev later\n");
		return (1 << HINIC_HWDEV_ALL_INITED);
	}

	err = hinic_os_dep_init(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to init os dependent\n");
		goto os_dep_init_err;
	}

	hinic_set_chip_present(hwdev);
	hinic_init_heartbeat(hwdev);

	err = init_cfg_mgmt(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to init config mgmt\n");
		goto init_cfg_mgmt_err;
	}

	err = hinic_init_comm_ch(hwdev);
	if (err) {
		if (!(hwdev->func_state & HINIC_HWDEV_INIT_MODES_MASK)) {
			sdk_err(hwdev->dev_hdl, "Failed to init communication channel\n");
			goto init_comm_ch_err;
		} else {
			sdk_err(hwdev->dev_hdl, "Init communication channel partitail failed\n");
			return hwdev->func_state & HINIC_HWDEV_INIT_MODES_MASK;
		}
	}

	err = init_capability(hwdev);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to init capability\n");
		goto init_cap_err;
	}

	if (hwdev->board_info.service_mode == HINIC_WORK_MODE_OVS_SP)
		hwdev->feature_cap &= ~HINIC_FUNC_SUPP_RATE_LIMIT;

	if (hwdev->cfg_mgmt->svc_cap.force_up)
		hwdev->feature_cap |= HINIC_FUNC_FORCE_LINK_UP;

	err = __vf_func_init(hwdev);
	if (err)
		goto vf_func_init_err;


	err = __hilink_phy_init(hwdev);
	if (err)
		goto hilink_phy_init_err;

	set_bit(HINIC_HWDEV_ALL_INITED, &hwdev->func_state);

	sdk_info(hwdev->dev_hdl, "Init hwdev success\n");

	return 0;

hilink_phy_init_err:

	hinic_vf_func_free(hwdev);
vf_func_init_err:
	free_capability(hwdev);
init_cap_err:
	return (hwdev->func_state & HINIC_HWDEV_INIT_MODES_MASK);

init_comm_ch_err:
	free_cfg_mgmt(hwdev);

init_cfg_mgmt_err:
	hinic_destroy_heartbeat(hwdev);
	hinic_os_dep_deinit(hwdev);

os_dep_init_err:
	hinic_free_hwif(hwdev);

init_hwif_err:
	vfree(hwdev->chip_fault_stats);

alloc_chip_fault_stats_err:
	sema_deinit(&hwdev->func_sem);
	sema_deinit(&hwdev->ppf_sem);
	kfree(hwdev);
	*para->hwdev = NULL;

	return -EFAULT;
}

/**
 * hinic_set_vf_dev_cap - Set max queue num for VF
 * @hwdev: the HW device for VF
 */
int hinic_set_vf_dev_cap(void *hwdev)
{
	int err;
	struct hinic_hwdev *dev;
	enum func_type type;

	if (!hwdev)
		return EFAULT;

	dev = (struct hinic_hwdev *)hwdev;
	type = HINIC_FUNC_TYPE(dev);
	if (type != TYPE_VF)
		return EPERM;

	err = get_dev_cap(dev);
	if (err)
		return err;

	nic_param_fix(dev);

	return 0;
}

void hinic_free_hwdev(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;
	enum hinic_hwdev_init_state state = HINIC_HWDEV_ALL_INITED;
	int flag = 0;

	if (!hwdev)
		return;

	if (test_bit(HINIC_HWDEV_ALL_INITED, &dev->func_state)) {
		clear_bit(HINIC_HWDEV_ALL_INITED, &dev->func_state);

		/* BM slave function not need to exec rx_tx_flush */
		if (dev->func_mode != FUNC_MOD_MULTI_BM_SLAVE)
			hinic_func_rx_tx_flush(hwdev);


		hinic_vf_func_free(hwdev);

		free_capability(dev);
	}
	while (state > HINIC_HWDEV_NONE_INITED) {
		if (test_bit(state, &dev->func_state)) {
			flag = 1;
			break;
		}
		state--;
	}
	if (flag) {
		hinic_uninit_comm_ch(dev);
		free_cfg_mgmt(dev);
		hinic_destroy_heartbeat(dev);
		hinic_os_dep_deinit(dev);
	}
	clear_bit(HINIC_HWDEV_NONE_INITED, &dev->func_state);
	hinic_free_hwif(dev);
	vfree(dev->chip_fault_stats);
	sema_deinit(&dev->func_sem);
	sema_deinit(&dev->ppf_sem);
	kfree(dev);
}

void hinic_set_api_stop(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return;

	dev->chip_present_flag = HINIC_CHIP_ABSENT;
	sdk_info(dev->dev_hdl, "Set card absent\n");
	hinic_force_complete_all(dev);
	sdk_info(dev->dev_hdl, "All messages interacting with the chip will stop\n");
}

void hinic_shutdown_hwdev(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!hwdev)
		return;

	if (IS_SLAVE_HOST(dev))
		set_slave_host_enable(hwdev, hinic_pcie_itf_id(hwdev), false);
}

u32 hinic_func_pf_num(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting pf number capability\n");
		return 0;
	}

	return dev->cfg_mgmt->svc_cap.pf_num;
}

u64 hinic_get_func_feature_cap(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting function feature capability\n");
		return 0;
	}

	return dev->feature_cap;
}

enum hinic_func_mode hinic_get_func_mode(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting function mode\n");
		return 0;
	}

	return dev->func_mode;
}
EXPORT_SYMBOL(hinic_get_func_mode);

enum hinic_service_mode hinic_get_service_mode(void *hwdev)
{
	struct hinic_hwdev *dev = hwdev;

	if (!dev) {
		pr_err("Hwdev pointer is NULL for getting service mode\n");
		return HINIC_WORK_MODE_INVALID;
	}

	return dev->board_info.service_mode;
}
