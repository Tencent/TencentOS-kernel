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
#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_hwdev.h"
#include "hinic_sml_counter.h"

#ifndef HTONL
#define HTONL(x) \
	((((x) & 0x000000ff) << 24) \
	| (((x) & 0x0000ff00) << 8) \
	| (((x) & 0x00ff0000) >> 8) \
	| (((x) & 0xff000000) >> 24))
#endif

static void sml_ctr_htonl_n(u32 *node, u32 ulLen)
{
	u32 i;

	for (i = 0; i < ulLen; i++) {
		*node = HTONL(*node);
		node++;
	}
}

static void hinic_sml_ctr_read_build_req(chipif_sml_ctr_rd_req_s *msg,
					 u8 instance_id, u8 op_id,
					 u8 ack, u32 ctr_id, u32 init_val)
{
	msg->head.value = 0;
	msg->head.bs.instance = instance_id;
	msg->head.bs.op_id = op_id;
	msg->head.bs.ack = ack;
	msg->head.value = HTONL(msg->head.value);

	msg->ctr_id = ctr_id;
	msg->ctr_id = HTONL(msg->ctr_id);

	msg->initial = init_val;
}

static void hinic_sml_ctr_write_build_req(chipif_sml_ctr_wr_req_s *msg,
					  u8 instance_id, u8 op_id,
					  u8 ack, u32 ctr_id,
					  u64 val1, u64 val2)
{
	msg->head.value = 0;
	msg->head.bs.instance = instance_id;
	msg->head.bs.op_id = op_id;
	msg->head.bs.ack = ack;
	msg->head.value = HTONL(msg->head.value);

	msg->ctr_id = ctr_id;
	msg->ctr_id = HTONL(msg->ctr_id);

	msg->value1_h = val1 >> 32;
	msg->value1_l = val1 & 0xFFFFFFFF;

	msg->value2_h = val2 >> 32;
	msg->value2_l = val2 & 0xFFFFFFFF;
}

/**
 * hinic_sm_ctr_rd32 - small single 32 counter read
 * @hwdev: the pointer to hw device
 * @node: the node id
 * @instance: instance value
 * @ctr_id: counter id
 * @value: read counter value ptr
 * Return: 0 - success, negative - failure
 */
int hinic_sm_ctr_rd32(void *hwdev, u8 node, u8 instance, u32 ctr_id, u32 *value)
{
	chipif_sml_ctr_rd_req_s req;
	ctr_rd_rsp_u rsp;
	int ret;

	if (!hwdev || !value)
		return -EFAULT;

	hinic_sml_ctr_read_build_req(&req, instance, CHIPIF_SM_CTR_OP_READ,
				     CHIPIF_ACK, ctr_id, 0);

	ret = hinic_api_cmd_read_ack(hwdev, node, (u8 *)&req,
				     (unsigned short)sizeof(req),
				     (void *)&rsp, (unsigned short)sizeof(rsp));
	if (ret) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Sm 32bit counter read fail, err(%d)\n", ret);
		return ret;
	}
	sml_ctr_htonl_n((u32 *)&rsp, 4);
	*value = rsp.bs_ss32_rsp.value1;

	return 0;
}
EXPORT_SYMBOL(hinic_sm_ctr_rd32);

/**
 * hinic_sm_ctr_rd32_clear - small single 32 counter read and clear to zero
 * @hwdev: the pointer to hw device
 * @node: the node id
 * @instance: instance value
 * @ctr_id: counter id
 * @value: read counter value ptr
 * Return: 0 - success, negative - failure
 * according to ACN error code (ERR_OK, ERR_PARAM, ERR_FAILED...etc)
 */
int hinic_sm_ctr_rd32_clear(void *hwdev, u8 node, u8 instance,
			    u32 ctr_id, u32 *value)
{
	chipif_sml_ctr_rd_req_s req;
	ctr_rd_rsp_u rsp;
	int ret;

	if (!hwdev || !value)
		return -EFAULT;

	hinic_sml_ctr_read_build_req(&req, instance,
				     CHIPIF_SM_CTR_OP_READ_CLEAR,
				     CHIPIF_ACK, ctr_id, 0);

	ret = hinic_api_cmd_read_ack(hwdev, node, (u8 *)&req,
				     (unsigned short)sizeof(req),
				     (void *)&rsp, (unsigned short)sizeof(rsp));

	if (ret) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Sm 32bit counter clear fail, err(%d)\n", ret);
		return ret;
	}
	sml_ctr_htonl_n((u32 *)&rsp, 4);
	*value = rsp.bs_ss32_rsp.value1;

	return 0;
}
EXPORT_SYMBOL(hinic_sm_ctr_rd32_clear);

/**
 * hinic_sm_ctr_wr32 - small single 32 counter write
 * @hwdev: the pointer to hw device
 * @node: the node id
 * @instance: instance value
 * @ctr_id: counter id
 * @value: write counter value
 * Return: 0 - success, negative - failure
 */
int hinic_sm_ctr_wr32(void *hwdev, u8 node, u8 instance, u32 ctr_id, u32 value)
{
	chipif_sml_ctr_wr_req_s req;
	chipif_sml_ctr_wr_rsp_s rsp;

	if (!hwdev)
		return -EFAULT;

	hinic_sml_ctr_write_build_req(&req, instance, CHIPIF_SM_CTR_OP_WRITE,
				      CHIPIF_NOACK, ctr_id, (u64)value, 0ULL);

	return hinic_api_cmd_read_ack(hwdev, node, (u8 *)&req,
				      (unsigned short)sizeof(req), (void *)&rsp,
				      (unsigned short)sizeof(rsp));
}

/**
 * hinic_sm_ctr_rd64 - big counter 64 read
 * @hwdev: the pointer to hw device
 * @node: the node id
 * @instance: instance value
 * @ctr_id: counter id
 * @value: read counter value ptr
 * Return: 0 - success, negative - failure
 */
int hinic_sm_ctr_rd64(void *hwdev, u8 node, u8 instance, u32 ctr_id, u64 *value)
{
	chipif_sml_ctr_rd_req_s req;
	ctr_rd_rsp_u rsp;
	int ret;

	if (!hwdev || !value)
		return -EFAULT;

	hinic_sml_ctr_read_build_req(&req, instance, CHIPIF_SM_CTR_OP_READ,
				     CHIPIF_ACK, ctr_id, 0);

	ret = hinic_api_cmd_read_ack(hwdev, node, (u8 *)&req,
				     (unsigned short)sizeof(req), (void *)&rsp,
				     (unsigned short)sizeof(rsp));
	if (ret) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Sm 64bit counter read fail err(%d)\n", ret);
		return ret;
	}
	sml_ctr_htonl_n((u32 *)&rsp, 4);
	*value = ((u64)rsp.bs_bs64_rsp.value1 << 32) | rsp.bs_bs64_rsp.value2;

	return 0;
}

/**
 * hinic_sm_ctr_wr64 - big single 64 counter write
 * @hwdev: the pointer to hw device
 * @node: the node id
 * @instance: instance value
 * @ctr_id: counter id
 * @value: write counter value
 * Return: 0 - success, negative - failure
 */
int hinic_sm_ctr_wr64(void *hwdev, u8 node, u8 instance, u32 ctr_id, u64 value)
{
	chipif_sml_ctr_wr_req_s req;
	chipif_sml_ctr_wr_rsp_s rsp;

	if (!hwdev)
		return -EFAULT;

	hinic_sml_ctr_write_build_req(&req, instance, CHIPIF_SM_CTR_OP_WRITE,
				      CHIPIF_NOACK, ctr_id, value, 0ULL);

	return hinic_api_cmd_read_ack(hwdev, node, (u8 *)&req,
				      (unsigned short)sizeof(req), (void *)&rsp,
				      (unsigned short)sizeof(rsp));
}

/**
 * hinic_sm_ctr_rd64_pair - big pair 128 counter read
 * @hwdev: the pointer to hw device
 * @node: the node id
 * @instance: instance value
 * @ctr_id: counter id
 * @value1: read counter value ptr
 * @value2: read counter value ptr
 * Return: 0 - success, negative - failure
 */
int hinic_sm_ctr_rd64_pair(void *hwdev, u8 node, u8 instance,
			   u32 ctr_id, u64 *value1, u64 *value2)
{
	chipif_sml_ctr_rd_req_s req;
	ctr_rd_rsp_u rsp;
	int ret;

	if (!value1) {
		pr_err("value1 is NULL for read 64 bit pair\n");
		return -EFAULT;
	}

	if (!value2) {
		pr_err("value2 is NULL for read 64 bit pair\n");
		return -EFAULT;
	}

	if (!hwdev || (0 != (ctr_id & 0x1))) {
		pr_err("Hwdev is NULL or ctr_id(%d) is odd number for read 64 bit pair\n",
		       ctr_id);
		return -EFAULT;
	}

	hinic_sml_ctr_read_build_req(&req, instance, CHIPIF_SM_CTR_OP_READ,
				     CHIPIF_ACK, ctr_id, 0);

	ret = hinic_api_cmd_read_ack(hwdev, node, (u8 *)&req,
				     (unsigned short)sizeof(req), (void *)&rsp,
				     (unsigned short)sizeof(rsp));
	if (ret) {
		sdk_err(((struct hinic_hwdev *)hwdev)->dev_hdl,
			"Sm 64 bit rd pair ret(%d)\n", ret);
		return ret;
	}
	sml_ctr_htonl_n((u32 *)&rsp, 4);
	*value1 = ((u64)rsp.bs_bp64_rsp.val1_h << 32) | rsp.bs_bp64_rsp.val1_l;
	*value2 = ((u64)rsp.bs_bp64_rsp.val2_h << 32) | rsp.bs_bp64_rsp.val2_l;

	return 0;
}

/**
 * hinic_sm_ctr_wr64_pair - big pair 128 counter write
 * @hwdev: the pointer to hw device
 * @node: the node id
 * @instance: instance value
 * @ctr_id: counter id
 * @value1: write counter value
 * @value2: write counter value
 * Return: 0 - success, negative - failure
 */
int hinic_sm_ctr_wr64_pair(void *hwdev, u8 node, u8 instance,
			   u32 ctr_id, u64 value1, u64 value2)
{
	chipif_sml_ctr_wr_req_s req;
	chipif_sml_ctr_wr_rsp_s rsp;

	/* pair pattern ctr_id must be even number */
	if (!hwdev || (0 != (ctr_id & 0x1))) {
		pr_err("Handle is NULL or ctr_id(%d) is odd number for write 64 bit pair\n",
		       ctr_id);
		return -EFAULT;
	}

	hinic_sml_ctr_write_build_req(&req, instance, CHIPIF_SM_CTR_OP_WRITE,
				      CHIPIF_NOACK, ctr_id, value1, value2);
	return hinic_api_cmd_read_ack(hwdev, node, (u8 *)&req,
				      (unsigned short)sizeof(req), (void *)&rsp,
				      (unsigned short)sizeof(rsp));
}
