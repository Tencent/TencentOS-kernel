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

#ifndef __HINIC_MULTI_HOST_MGMT_H_
#define __HINIC_MULTI_HOST_MGMT_H_

#define IS_BMGW_MASTER_HOST(hwdev)	\
		((hwdev)->func_mode == FUNC_MOD_MULTI_BM_MASTER)
#define IS_BMGW_SLAVE_HOST(hwdev)	\
		((hwdev)->func_mode == FUNC_MOD_MULTI_BM_SLAVE)
#define IS_VM_MASTER_HOST(hwdev)	\
		((hwdev)->func_mode == FUNC_MOD_MULTI_VM_MASTER)
#define IS_VM_SLAVE_HOST(hwdev)		\
		((hwdev)->func_mode == FUNC_MOD_MULTI_VM_SLAVE)

#define IS_MASTER_HOST(hwdev)		\
		(IS_BMGW_MASTER_HOST(hwdev) || IS_VM_MASTER_HOST(hwdev))

#define IS_SLAVE_HOST(hwdev)		\
		(IS_BMGW_SLAVE_HOST(hwdev) || IS_VM_SLAVE_HOST(hwdev))

#define IS_MULTI_HOST(hwdev)		\
		(IS_BMGW_MASTER_HOST(hwdev) || IS_BMGW_SLAVE_HOST(hwdev) || \
		 IS_VM_MASTER_HOST(hwdev) || IS_VM_SLAVE_HOST(hwdev))

#define NEED_MBOX_FORWARD(hwdev)	IS_BMGW_SLAVE_HOST(hwdev)

struct hinic_multi_host_mgmt {
	struct hinic_hwdev *hwdev;

	/* slave host registered */
	bool	shost_registered;
	u8	shost_host_idx;
	u8	shost_ppf_idx;

	/* slave host functios support nic enable */
	DECLARE_BITMAP(func_nic_en, HINIC_MAX_FUNCTIONS);

	u8	mhost_ppf_idx;

	struct hinic_hw_pf_infos pf_infos;
};

struct hinic_host_fwd_head {
	unsigned short dst_glb_func_idx;
	unsigned char dst_itf_idx;
	unsigned char mod;

	unsigned char cmd;
	unsigned char rsv[3];
};

int hinic_multi_host_mgmt_init(struct hinic_hwdev *hwdev);
int hinic_multi_host_mgmt_free(struct hinic_hwdev *hwdev);
int hinic_mbox_to_host_no_ack(struct hinic_hwdev *hwdev,
			      enum hinic_mod_type mod, u8 cmd, void *buf_in,
			      u16 in_size);

struct register_slave_host {
	u8 status;
	u8 version;
	u8 rsvd[6];

	u8 host_id;
	u8 ppf_idx;
	u8 rsvd2[6];

	/* for max 512 functions */
	u64 funcs_nic_en[8];

	u64 rsvd3[8];
};

struct hinic_slave_func_nic_state {
	u8 status;
	u8 version;
	u8 rsvd[6];

	u16 func_idx;
	u8 enable;
	u8 rsvd1;

	u32 rsvd2[2];
};

void set_master_host_mbox_enable(struct hinic_hwdev *hwdev, bool enable);
void set_slave_host_enable(struct hinic_hwdev *hwdev, u8 host_id, bool enable);
void set_func_host_mode(struct hinic_hwdev *hwdev, enum hinic_func_mode mode);
int rectify_host_mode(struct hinic_hwdev *hwdev);
void detect_host_mode_pre(struct hinic_hwdev *hwdev);

int sw_func_pf_mbox_handler(void *handle, u16 vf_id, u8 cmd, void *buf_in,
			    u16 in_size, void *buf_out, u16 *out_size);

#endif
