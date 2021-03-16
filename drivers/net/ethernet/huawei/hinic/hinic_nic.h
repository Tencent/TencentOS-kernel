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

#ifndef HINIC_NIC_H_
#define HINIC_NIC_H_

#include "hinic_wq.h"

#define SET_VPORT_MBOX_TIMEOUT	(30 * 1000)
#define SET_VPORT_MGMT_TIMEOUT	(25 * 1000)
struct hinic_sq {
	struct hinic_wq	*wq;

	u16			q_id;

	u8			owner;

	void			*cons_idx_addr;

	u8 __iomem		*db_addr;
	u16			msix_entry_idx;
};

struct hinic_rq {
	struct hinic_wq	*wq;

	u16			*pi_virt_addr;
	dma_addr_t		pi_dma_addr;

	u16			q_id;

	u32			irq_id;
	u16			msix_entry_idx;

	dma_addr_t		cqe_dma_addr;
};

struct hinic_qp {
	struct hinic_sq	sq;
	struct hinic_rq	rq;
};

struct vf_data_storage {
	u8 vf_mac_addr[ETH_ALEN];
	bool registered;
	bool pf_set_mac;
	u16 pf_vlan;
	u8 pf_qos;
	u32 max_rate;
	u32 min_rate;

	bool link_forced;
	bool link_up;		/* only valid if VF link is forced */
	bool spoofchk;
	bool trust;
};

struct hinic_nic_cfg {
	struct semaphore	cfg_lock;

	/* Valid when pfc is disable */
	bool			pause_set;
	struct nic_pause_config	nic_pause;

	u8			pfc_en;
	u8			pfc_bitmap;

	struct nic_port_info	port_info;

	/* percentage of pf link bandwidth */
	u32			pf_bw_limit;
};

struct hinic_nic_io {
	struct hinic_hwdev	*hwdev;

	u16			global_qpn;
	u8			link_status;

	struct hinic_wqs	wqs;

	struct hinic_wq		*sq_wq;
	struct hinic_wq		*rq_wq;

	u16			max_qps;
	u16			num_qps;
	u16			sq_depth;
	u16			rq_depth;
	struct hinic_qp		*qps;

	void			*ci_vaddr_base;
	dma_addr_t		ci_dma_base;

	u16			max_vfs;
	struct vf_data_storage	*vf_infos;

	struct hinic_dcb_state	dcb_state;

	struct hinic_nic_cfg	nic_cfg;
	u16			rx_buff_len;
};

#endif
