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

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "ossl_knl.h"
#include "hinic_sm_lt.h"
#include "hinic_hw.h"
#include "hinic_hwdev.h"
#include "hinic_dbg.h"

#define ACK 1
#define NOACK 0

#define LT_LOAD16_API_SIZE (16 + 4)
#define LT_STORE16_API_SIZE (32 + 4)

#ifndef HTONL
#define HTONL(x) \
	((((x) & 0x000000ff) << 24) \
	| (((x) & 0x0000ff00) << 8) \
	| (((x) & 0x00ff0000) >> 8) \
	| (((x) & 0xff000000) >> 24))
#endif

static inline void sm_lt_build_head(sml_lt_req_head_u *head,
				    u8 instance_id,
				    u8 op_id, u8 ack,
				    u8 offset, u8 num)
{
	head->value = 0;
	head->bs.instance = instance_id;
	head->bs.op_id = op_id;
	head->bs.ack = ack;
	head->bs.num = num;
	head->bs.abuf_flg = 0;
	head->bs.bc = 1;
	head->bs.offset = offset;
	head->value = HTONL((head->value));
}

static inline void sm_lt_load_build_req(sml_lt_load_req_s *req,
					u8 instance_id,
					u8 op_id, u8 ack,
					u32 lt_index,
					u8 offset, u8 num)
{
	sm_lt_build_head(&req->head, instance_id, op_id, ack, offset, num);
	req->extra = 0;
	req->index = lt_index;
	req->index = HTONL(req->index);
}

static inline void sm_lt_store_build_req(sml_lt_store_req_s *req,
					 u8 instance_id,
					 u8 op_id, u8 ack,
					 u32 lt_index,
					 u8 offset,
					 u8 num,
					 u16 byte_enb3,
					 u16 byte_enb2,
					 u16 byte_enb1,
					 u8 *data)
{
	sm_lt_build_head(&req->head, instance_id, op_id, ack, offset, num);
	req->index     = lt_index;
	req->index     = HTONL(req->index);
	req->extra = 0;
	req->byte_enb[0] = (u32)(byte_enb3);
	req->byte_enb[0] = HTONL(req->byte_enb[0]);
	req->byte_enb[1] = HTONL((((u32)byte_enb2) << 16) | byte_enb1);
	sml_lt_store_memcpy((u32 *)req->write_data, (u32 *)(void *)data, num);
}

int hinic_dbg_lt_rd_16byte(void *hwdev, u8 dest, u8 instance,
			   u32 lt_index, u8 *data)
{
	sml_lt_load_req_s req;
	int ret;

	if (!hwdev)
		return -EFAULT;

	sm_lt_load_build_req(&req, instance, SM_LT_LOAD, ACK, lt_index, 0, 0);

	ret = hinic_api_cmd_read_ack(hwdev, dest, &req,
				     LT_LOAD16_API_SIZE, (void *)data, 16);
	if (ret) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Read linear table 16byte fail, err: %d\n", ret);
		return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_dbg_lt_rd_16byte);

int hinic_dbg_lt_wr_16byte_mask(void *hwdev, u8 dest, u8 instance,
				u32 lt_index, u8 *data, u16 mask)
{
	sml_lt_store_req_s req;
	int ret;

	if (!hwdev || !data)
		return -EFAULT;

	sm_lt_store_build_req(&req, instance, SM_LT_STORE, NOACK, lt_index,
			      0, 0, 0, 0, mask, data);

	ret = hinic_api_cmd_write_nack(hwdev, dest, &req, LT_STORE16_API_SIZE);
	if (ret) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Write linear table 16byte fail, err: %d\n", ret);
		return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_dbg_lt_wr_16byte_mask);

int hinic_api_csr_rd32(void *hwdev, u8 dest, u32 addr, u32 *val)
{
	struct hinic_csr_request_api_data api_data = {0};
	u32 csr_val = 0;
	u16 in_size = sizeof(api_data);
	int ret;

	if (!hwdev || !val)
		return -EFAULT;

	memset(&api_data, 0, sizeof(struct hinic_csr_request_api_data));
	api_data.dw0 = 0;
	api_data.dw1.bits.operation_id = HINIC_CSR_OPERATION_READ_CSR;
	api_data.dw1.bits.need_response = HINIC_CSR_NEED_RESP_DATA;
	api_data.dw1.bits.data_size = HINIC_CSR_DATA_SZ_32;
	api_data.dw1.val32 = cpu_to_be32(api_data.dw1.val32);
	api_data.dw2.bits.csr_addr = addr;
	api_data.dw2.val32 = cpu_to_be32(api_data.dw2.val32);

	ret = hinic_api_cmd_read_ack(hwdev, dest, (u8 *)(&api_data),
				     in_size, &csr_val, 4);
	if (ret) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Read 32 bit csr fail, dest %d addr 0x%x, ret: 0x%x\n",
			dest, addr, ret);
		return ret;
	}

	*val = csr_val;

	return 0;
}
EXPORT_SYMBOL(hinic_api_csr_rd32);

int hinic_api_csr_wr32(void *hwdev, u8 dest, u32 addr, u32 val)
{
	struct hinic_csr_request_api_data api_data;
	u16 in_size = sizeof(api_data);
	int ret;

	if (!hwdev)
		return -EFAULT;

	memset(&api_data, 0, sizeof(struct hinic_csr_request_api_data));
	api_data.dw1.bits.operation_id = HINIC_CSR_OPERATION_WRITE_CSR;
	api_data.dw1.bits.need_response = HINIC_CSR_NO_RESP_DATA;
	api_data.dw1.bits.data_size = HINIC_CSR_DATA_SZ_32;
	api_data.dw1.val32 = cpu_to_be32(api_data.dw1.val32);
	api_data.dw2.bits.csr_addr = addr;
	api_data.dw2.val32 = cpu_to_be32(api_data.dw2.val32);
	api_data.csr_write_data_h = 0xffffffff;
	api_data.csr_write_data_l = val;

	ret = hinic_api_cmd_write_nack(hwdev, dest, (u8 *)(&api_data), in_size);
	if (ret) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Write 32 bit csr fail! dest %d addr 0x%x val 0x%x\n",
			dest, addr, val);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(hinic_api_csr_wr32);

int hinic_api_csr_rd64(void *hwdev, u8 dest, u32 addr, u64 *val)
{
	struct hinic_csr_request_api_data api_data = {0};
	u64 csr_val = 0;
	u16 in_size = sizeof(api_data);
	int ret;

	if (!hwdev || !val)
		return -EFAULT;

	memset(&api_data, 0, sizeof(struct hinic_csr_request_api_data));
	api_data.dw0 = 0;
	api_data.dw1.bits.operation_id = HINIC_CSR_OPERATION_READ_CSR;
	api_data.dw1.bits.need_response = HINIC_CSR_NEED_RESP_DATA;
	api_data.dw1.bits.data_size = HINIC_CSR_DATA_SZ_64;
	api_data.dw1.val32 = cpu_to_be32(api_data.dw1.val32);
	api_data.dw2.bits.csr_addr = addr;
	api_data.dw2.val32 = cpu_to_be32(api_data.dw2.val32);

	ret = hinic_api_cmd_read_ack(hwdev, dest, (u8 *)(&api_data),
				     in_size, &csr_val, 8);
	if (ret) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Read 64 bit csr fail, dest %d addr 0x%x\n",
			dest, addr);
		return ret;
	}

	*val = csr_val;

	return 0;
}
EXPORT_SYMBOL(hinic_api_csr_rd64);
