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

#ifndef __CHIPIF_SM_LT_H__
#define __CHIPIF_SM_LT_H__

#define SM_LT_LOAD                     (0x12)
#define SM_LT_STORE                    (0x14)

#define SM_LT_NUM_OFFSET            13
#define SM_LT_ABUF_FLG_OFFSET       12
#define SM_LT_BC_OFFSET             11

#define SM_LT_ENTRY_16B             16
#define SM_LT_ENTRY_32B             32
#define SM_LT_ENTRY_48B             48
#define SM_LT_ENTRY_64B             64

#define TBL_LT_OFFSET_DEFAULT       0

#define SM_CACHE_LINE_SHFT          4   /* log2(16) */
#define SM_CACHE_LINE_SIZE          16  /* the size of cache line */

#define MAX_SM_LT_READ_LINE_NUM     4
#define MAX_SM_LT_WRITE_LINE_NUM    3

#define SM_LT_FULL_BYTEENB          0xFFFF

#define TBL_GET_ENB3_MASK(bitmask)  (u16)(((bitmask) >> 32) & 0xFFFF)
#define TBL_GET_ENB2_MASK(bitmask)  (u16)(((bitmask) >> 16) & 0xFFFF)
#define TBL_GET_ENB1_MASK(bitmask)  (u16)((bitmask) & 0xFFFF)

enum {
	SM_LT_NUM_0 = 0,            /* lt num = 0, load/store 16B */
	SM_LT_NUM_1,                /* lt num = 1, load/store 32B */
	SM_LT_NUM_2,                /* lt num = 2, load/store 48B */
	SM_LT_NUM_3                 /* lt num = 3, load 64B */
};

/* lt load request */
typedef union {
	struct {
		u32 offset:8;
		u32 pad:3;
		u32 bc:1;
		u32 abuf_flg:1;
		u32 num:2;
		u32 ack:1;
		u32 op_id:5;
		u32 instance:6;
		u32 src:5;
	} bs;

	u32 value;
} sml_lt_req_head_u;

typedef struct {
	u32 extra;
	sml_lt_req_head_u head;
	u32 index;
	u32 pad0;
	u32 pad1;
} sml_lt_load_req_s;

typedef struct {
	u32 extra;
	sml_lt_req_head_u head;
	u32 index;
	u32 byte_enb[2];
	u8  write_data[48];
} sml_lt_store_req_s;

enum {
	SM_LT_OFFSET_1 = 1,
	SM_LT_OFFSET_2,
	SM_LT_OFFSET_3,
	SM_LT_OFFSET_4,
	SM_LT_OFFSET_5,
	SM_LT_OFFSET_6,
	SM_LT_OFFSET_7,
	SM_LT_OFFSET_8,
	SM_LT_OFFSET_9,
	SM_LT_OFFSET_10,
	SM_LT_OFFSET_11,
	SM_LT_OFFSET_12,
	SM_LT_OFFSET_13,
	SM_LT_OFFSET_14,
	SM_LT_OFFSET_15
};

static inline void sml_lt_store_memcpy(u32 *dst, u32 *src, u8 num)
{
	switch (num) {
	case SM_LT_NUM_2:
		*(dst + SM_LT_OFFSET_11) = *(src + SM_LT_OFFSET_11);
		*(dst + SM_LT_OFFSET_10) = *(src + SM_LT_OFFSET_10);
		*(dst + SM_LT_OFFSET_9)  = *(src + SM_LT_OFFSET_9);
		*(dst + SM_LT_OFFSET_8)  = *(src + SM_LT_OFFSET_8);
	case SM_LT_NUM_1:
		*(dst + SM_LT_OFFSET_7) = *(src + SM_LT_OFFSET_7);
		*(dst + SM_LT_OFFSET_6) = *(src + SM_LT_OFFSET_6);
		*(dst + SM_LT_OFFSET_5) = *(src + SM_LT_OFFSET_5);
		*(dst + SM_LT_OFFSET_4) = *(src + SM_LT_OFFSET_4);
	case SM_LT_NUM_0:
		*(dst + SM_LT_OFFSET_3) = *(src + SM_LT_OFFSET_3);
		*(dst + SM_LT_OFFSET_2) = *(src + SM_LT_OFFSET_2);
		*(dst + SM_LT_OFFSET_1) = *(src + SM_LT_OFFSET_1);
		*dst = *src;
		break;
	default:
		break;
	}
}

static inline void sml_lt_load_memcpy(u32 *dst, u32 *src, u8 num)
{
	switch (num) {
	case SM_LT_NUM_3:
		*(dst + SM_LT_OFFSET_15) = *(src + SM_LT_OFFSET_15);
		*(dst + SM_LT_OFFSET_14) = *(src + SM_LT_OFFSET_14);
		*(dst + SM_LT_OFFSET_13) = *(src + SM_LT_OFFSET_13);
		*(dst + SM_LT_OFFSET_12) = *(src + SM_LT_OFFSET_12);
	case SM_LT_NUM_2:
		*(dst + SM_LT_OFFSET_11) = *(src + SM_LT_OFFSET_11);
		*(dst + SM_LT_OFFSET_10) = *(src + SM_LT_OFFSET_10);
		*(dst + SM_LT_OFFSET_9)  = *(src + SM_LT_OFFSET_9);
		*(dst + SM_LT_OFFSET_8)  = *(src + SM_LT_OFFSET_8);
	case SM_LT_NUM_1:
		*(dst + SM_LT_OFFSET_7) = *(src + SM_LT_OFFSET_7);
		*(dst + SM_LT_OFFSET_6) = *(src + SM_LT_OFFSET_6);
		*(dst + SM_LT_OFFSET_5) = *(src + SM_LT_OFFSET_5);
		*(dst + SM_LT_OFFSET_4) = *(src + SM_LT_OFFSET_4);
	case SM_LT_NUM_0:
		*(dst + SM_LT_OFFSET_3) = *(src + SM_LT_OFFSET_3);
		*(dst + SM_LT_OFFSET_2) = *(src + SM_LT_OFFSET_2);
		*(dst + SM_LT_OFFSET_1) = *(src + SM_LT_OFFSET_1);
		*dst = *src;
		break;
	default:
		break;
	}
}

enum HINIC_CSR_API_DATA_OPERATION_ID {
	HINIC_CSR_OPERATION_WRITE_CSR = 0x1E,
	HINIC_CSR_OPERATION_READ_CSR = 0x1F
};

enum HINIC_CSR_API_DATA_NEED_RESPONSE_DATA {
	HINIC_CSR_NO_RESP_DATA = 0,
	HINIC_CSR_NEED_RESP_DATA = 1
};

enum HINIC_CSR_API_DATA_DATA_SIZE {
	HINIC_CSR_DATA_SZ_32 = 0,
	HINIC_CSR_DATA_SZ_64 = 1
};

struct hinic_csr_request_api_data {
	u32 dw0;

	union {
		struct {
			u32 reserved1:13;
			/* this field indicates the write/read data size:
			 * 2'b00: 32 bits
			 * 2'b01: 64 bits
			 * 2'b10~2'b11:reserved
			 */
			u32 data_size:2;
			/* this field indicates that requestor expect receive a
			 * response data or not.
			 * 1'b0: expect not to receive a response data.
			 * 1'b1: expect to receive a response data.
			 */
			u32 need_response:1;
			/* this field indicates the operation that the requestor
			 *  expected.
			 * 5'b1_1110: write value to csr space.
			 * 5'b1_1111: read register from csr space.
			 */
			u32 operation_id:5;
			u32 reserved2:6;
			/* this field specifies the Src node ID for this API
			 * request message.
			 */
			u32 src_node_id:5;
		} bits;

		u32 val32;
	} dw1;

	union {
		struct {
			/* it specifies the CSR address. */
			u32 csr_addr:26;
			u32 reserved3:6;
		} bits;

		u32 val32;
	} dw2;

	/* if data_size=2'b01, it is high 32 bits of write data. else, it is
	 * 32'hFFFF_FFFF.
	 */
	u32 csr_write_data_h;
	/* the low 32 bits of write data. */
	u32 csr_write_data_l;
};

#endif
