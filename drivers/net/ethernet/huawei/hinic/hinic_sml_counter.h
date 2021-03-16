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

#ifndef __CHIPIF_SML_COUNTER_H__
#define __CHIPIF_SML_COUNTER_H__

#define CHIPIF_FUNC_PF  0
#define CHIPIF_FUNC_VF  1
#define CHIPIF_FUNC_PPF 2

#define CHIPIF_ACK 1
#define CHIPIF_NOACK 0

#define CHIPIF_SM_CTR_OP_READ 0x2
#define CHIPIF_SM_CTR_OP_READ_CLEAR 0x6
#define CHIPIF_SM_CTR_OP_WRITE 0x3

#define SMALL_CNT_READ_RSP_SIZE 16

/* request head */
typedef union {
	struct {
		u32  pad:15;
		u32  ack:1;
		u32  op_id:5;
		u32  instance:6;
		u32  src:5;
	} bs;

	u32 value;
} chipif_sml_ctr_req_head_u;
/* counter read request struct */
typedef struct {
	u32 extra;
	chipif_sml_ctr_req_head_u head;
	u32 ctr_id;
	u32 initial;
	u32 pad;
} chipif_sml_ctr_rd_req_s;

/* counter read response union */
typedef union {
	struct {
		u32 value1:16;
		u32 pad0:16;
		u32 pad1[3];
	} bs_ss16_rsp;

	struct {
		u32 value1;
		u32 pad[3];
	} bs_ss32_rsp;

	struct {
		u32 value1:20;
		u32 pad0:12;
		u32 value2:12;
		u32 pad1:20;
		u32 pad2[2];
	} bs_sp_rsp;

	struct {
		u32 value1;
		u32 value2;
		u32 pad[2];
	} bs_bs64_rsp;

	struct {
		u32 val1_h;
		u32 val1_l;
		u32 val2_h;
		u32 val2_l;
	} bs_bp64_rsp;

} ctr_rd_rsp_u;

/* resopnse head */
typedef union {
	struct {
		u32 pad:30; /* reserve */
		u32 code:2;  /* error code */
	} bs;

	u32 value;
} sml_ctr_rsp_head_u;

/* counter write request struct */
typedef struct {
	u32 extra;
	chipif_sml_ctr_req_head_u head;
	u32 ctr_id;
	u32 rsv1;
	u32 rsv2;
	u32 value1_h;
	u32 value1_l;
	u32 value2_h;
	u32 value2_l;
} chipif_sml_ctr_wr_req_s;

/* counter write response struct */
typedef struct {
	sml_ctr_rsp_head_u head;
	u32 pad[3];
} chipif_sml_ctr_wr_rsp_s;

#endif
