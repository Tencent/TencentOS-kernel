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
#include <linux/completion.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/semaphore.h>
#include <linux/jiffies.h>
#include <linux/delay.h>

#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hwdev.h"
#include "hinic_csr.h"
#include "hinic_hwif.h"
#include "hinic_api_cmd.h"

#define API_CMD_CHAIN_CELL_SIZE_SHIFT   6U

#define API_CMD_CELL_DESC_SIZE          8
#define API_CMD_CELL_DATA_ADDR_SIZE     8

#define API_CHAIN_NUM_CELLS             32
#define API_CHAIN_CELL_SIZE             128
#define API_CHAIN_RSP_DATA_SIZE		128

#define API_CMD_CELL_WB_ADDR_SIZE	8

#define API_CHAIN_CELL_ALIGNMENT        8

#define API_CMD_TIMEOUT			10000
#define API_CMD_STATUS_TIMEOUT		100000

#define API_CMD_BUF_SIZE		2048ULL

#define API_CMD_NODE_ALIGN_SIZE		512ULL
#define API_PAYLOAD_ALIGN_SIZE		64ULL

#define API_CHAIN_RESP_ALIGNMENT	64ULL

#define COMPLETION_TIMEOUT_DEFAULT		1000UL
#define POLLING_COMPLETION_TIMEOUT_DEFAULT	1000U

#define API_CMD_RESPONSE_DATA_PADDR(val)	be64_to_cpu(*((u64 *)(val)))

#define READ_API_CMD_PRIV_DATA(id, token)	(((id) << 16) + (token))
#define WRITE_API_CMD_PRIV_DATA(id)		(((u8)id) << 16)

#define MASKED_IDX(chain, idx)		((idx) & ((chain)->num_cells - 1))

#define SIZE_4BYTES(size)		(ALIGN((u32)(size), 4U) >> 2)
#define SIZE_8BYTES(size)		(ALIGN((u32)(size), 8U) >> 3)

enum api_cmd_data_format {
	SGL_DATA     = 1,
};

enum api_cmd_type {
	API_CMD_WRITE_TYPE = 0,
	API_CMD_READ_TYPE = 1,
};

enum api_cmd_bypass {
	NOT_BYPASS = 0,
	BYPASS = 1,
};

enum api_cmd_resp_aeq {
	NOT_TRIGGER = 0,
	TRIGGER     = 1,
};

static u8 xor_chksum_set(void *data)
{
	int idx;
	u8 checksum = 0;
	u8 *val = data;

	for (idx = 0; idx < 7; idx++)
		checksum ^= val[idx];

	return checksum;
}

static void set_prod_idx(struct hinic_api_cmd_chain *chain)
{
	enum hinic_api_cmd_chain_type chain_type = chain->chain_type;
	struct hinic_hwif *hwif = chain->hwdev->hwif;
	u32 hw_prod_idx_addr = HINIC_CSR_API_CMD_CHAIN_PI_ADDR(chain_type);
	u32 prod_idx = chain->prod_idx;

	hinic_hwif_write_reg(hwif, hw_prod_idx_addr, prod_idx);
}

static u32 get_hw_cons_idx(struct hinic_api_cmd_chain *chain)
{
	u32 addr, val;

	addr = HINIC_CSR_API_CMD_STATUS_0_ADDR(chain->chain_type);
	val  = hinic_hwif_read_reg(chain->hwdev->hwif, addr);

	return HINIC_API_CMD_STATUS_GET(val, CONS_IDX);
}

static void dump_api_chain_reg(struct hinic_api_cmd_chain *chain)
{
	void *dev = chain->hwdev->dev_hdl;
	u32 addr, val;

	addr = HINIC_CSR_API_CMD_STATUS_0_ADDR(chain->chain_type);
	val  = hinic_hwif_read_reg(chain->hwdev->hwif, addr);

	sdk_err(dev, "Chain type: 0x%x, cpld error: 0x%x, check error: 0x%x,  current fsm: 0x%x\n",
		chain->chain_type, HINIC_API_CMD_STATUS_GET(val, CPLD_ERR),
		HINIC_API_CMD_STATUS_GET(val, CHKSUM_ERR),
		HINIC_API_CMD_STATUS_GET(val, FSM));

	sdk_err(dev, "Chain hw current ci: 0x%x\n",
		HINIC_API_CMD_STATUS_GET(val, CONS_IDX));

	addr = HINIC_CSR_API_CMD_CHAIN_PI_ADDR(chain->chain_type);
	val  = hinic_hwif_read_reg(chain->hwdev->hwif, addr);
	sdk_err(dev, "Chain hw current pi: 0x%x\n", val);
}

/**
 * chain_busy - check if the chain is still processing last requests
 * @chain: chain to check
 * Return: 0 - success, negative - failure
 */
static int chain_busy(struct hinic_api_cmd_chain *chain)
{
	void *dev = chain->hwdev->dev_hdl;
	struct hinic_api_cmd_cell_ctxt *ctxt;
	u64 resp_header;

	ctxt = &chain->cell_ctxt[chain->prod_idx];

	switch (chain->chain_type) {
	case HINIC_API_CMD_MULTI_READ:
	case HINIC_API_CMD_POLL_READ:
		resp_header = be64_to_cpu(ctxt->resp->header);
		if (ctxt->status &&
		    !HINIC_API_CMD_RESP_HEADER_VALID(resp_header)) {
			sdk_err(dev, "Context(0x%x) busy!, pi: %d, resp_header: 0x%08x%08x\n",
				ctxt->status, chain->prod_idx,
				upper_32_bits(resp_header),
				lower_32_bits(resp_header));
			dump_api_chain_reg(chain);
			return -EBUSY;
		}
		break;
	case HINIC_API_CMD_POLL_WRITE:
	case HINIC_API_CMD_WRITE_TO_MGMT_CPU:
	case HINIC_API_CMD_WRITE_ASYNC_TO_MGMT_CPU:
		chain->cons_idx = get_hw_cons_idx(chain);

		if (chain->cons_idx == MASKED_IDX(chain, chain->prod_idx + 1)) {
			sdk_err(dev, "API CMD chain %d is busy, cons_idx = %d, prod_idx = %d\n",
				chain->chain_type, chain->cons_idx,
				chain->prod_idx);
			dump_api_chain_reg(chain);
			return -EBUSY;
		}
		break;
	default:
		sdk_err(dev, "Unknown Chain type %d\n", chain->chain_type);
		return -EINVAL;
	}

	return 0;
}

/**
 * get_cell_data_size - get the data size of specific cell type
 * @type: chain type
 * @cmd_size: the command size
 * Return: cell_data_size
 */
static u16 get_cell_data_size(enum hinic_api_cmd_chain_type type, u16 cmd_size)
{
	u16 cell_data_size = 0;

	switch (type) {
	case HINIC_API_CMD_POLL_READ:
		cell_data_size = ALIGN(API_CMD_CELL_DESC_SIZE +
				    API_CMD_CELL_WB_ADDR_SIZE +
				    API_CMD_CELL_DATA_ADDR_SIZE,
				    API_CHAIN_CELL_ALIGNMENT);
		break;

	case HINIC_API_CMD_WRITE_TO_MGMT_CPU:
	case HINIC_API_CMD_POLL_WRITE:
	case HINIC_API_CMD_WRITE_ASYNC_TO_MGMT_CPU:
		cell_data_size = ALIGN(API_CMD_CELL_DESC_SIZE +
				       API_CMD_CELL_DATA_ADDR_SIZE,
				       API_CHAIN_CELL_ALIGNMENT);
		break;
	default:
		break;
	}

	return cell_data_size;
}

/**
 * prepare_cell_ctrl - prepare the ctrl of the cell for the command
 * @cell_ctrl: the control of the cell to set the control into it
 * @cell_len: the size of the cell
 */
static void prepare_cell_ctrl(u64 *cell_ctrl, u16 cell_len)
{
	u64 ctrl;
	u8 chksum;

	ctrl = HINIC_API_CMD_CELL_CTRL_SET(SIZE_8BYTES(cell_len), CELL_LEN) |
	       HINIC_API_CMD_CELL_CTRL_SET(0ULL, RD_DMA_ATTR_OFF) |
	       HINIC_API_CMD_CELL_CTRL_SET(0ULL, WR_DMA_ATTR_OFF);

	chksum = xor_chksum_set(&ctrl);

	ctrl |= HINIC_API_CMD_CELL_CTRL_SET(chksum, XOR_CHKSUM);

	/* The data in the HW should be in Big Endian Format */
	*cell_ctrl = cpu_to_be64(ctrl);
}

/**
 * prepare_api_cmd - prepare API CMD command
 * @chain: chain for the command
 * @cell: the cell of the command
 * @dest: destination node on the card that will receive the command
 * @cmd: command data
 * @cmd_size: the command size
 */
static void prepare_api_cmd(struct hinic_api_cmd_chain *chain,
			    struct hinic_api_cmd_cell *cell,
			    enum hinic_node_id dest,
			    const void *cmd, u16 cmd_size)
{
	struct hinic_api_cmd_cell_ctxt	*cell_ctxt;
	u32 priv;

	cell_ctxt = &chain->cell_ctxt[chain->prod_idx];

	switch (chain->chain_type) {
	case HINIC_API_CMD_POLL_READ:
		priv = READ_API_CMD_PRIV_DATA(chain->chain_type,
					      cell_ctxt->saved_prod_idx);
		cell->desc = HINIC_API_CMD_DESC_SET(SGL_DATA, API_TYPE)	|
			     HINIC_API_CMD_DESC_SET(API_CMD_READ_TYPE, RD_WR) |
			     HINIC_API_CMD_DESC_SET(BYPASS, MGMT_BYPASS) |
			     HINIC_API_CMD_DESC_SET(NOT_TRIGGER, RESP_AEQE_EN) |
			     HINIC_API_CMD_DESC_SET(priv, PRIV_DATA);
		break;
	case HINIC_API_CMD_POLL_WRITE:
		priv =  WRITE_API_CMD_PRIV_DATA(chain->chain_type);
		cell->desc = HINIC_API_CMD_DESC_SET(SGL_DATA, API_TYPE)	|
			     HINIC_API_CMD_DESC_SET(API_CMD_WRITE_TYPE, RD_WR) |
			     HINIC_API_CMD_DESC_SET(BYPASS, MGMT_BYPASS) |
			     HINIC_API_CMD_DESC_SET(NOT_TRIGGER, RESP_AEQE_EN) |
			     HINIC_API_CMD_DESC_SET(priv, PRIV_DATA);
		break;
	case HINIC_API_CMD_WRITE_ASYNC_TO_MGMT_CPU:
	case HINIC_API_CMD_WRITE_TO_MGMT_CPU:
		priv =  WRITE_API_CMD_PRIV_DATA(chain->chain_type);
		cell->desc = HINIC_API_CMD_DESC_SET(SGL_DATA, API_TYPE) |
			     HINIC_API_CMD_DESC_SET(API_CMD_WRITE_TYPE, RD_WR) |
			     HINIC_API_CMD_DESC_SET(NOT_BYPASS, MGMT_BYPASS) |
			     HINIC_API_CMD_DESC_SET(TRIGGER, RESP_AEQE_EN) |
			     HINIC_API_CMD_DESC_SET(priv, PRIV_DATA);
		break;
	default:
		sdk_err(chain->hwdev->dev_hdl, "Unknown Chain type: %d\n",
			chain->chain_type);
		return;
	}

	cell->desc |= HINIC_API_CMD_DESC_SET(dest, DEST) |
		      HINIC_API_CMD_DESC_SET(SIZE_4BYTES(cmd_size), SIZE);

	cell->desc |= HINIC_API_CMD_DESC_SET(xor_chksum_set(&cell->desc),
						XOR_CHKSUM);

	/* The data in the HW should be in Big Endian Format */
	cell->desc = cpu_to_be64(cell->desc);

	memcpy(cell_ctxt->api_cmd_vaddr, cmd, cmd_size);
}

/**
 * prepare_cell - prepare cell ctrl and cmd in the current producer cell
 * @chain: chain for the command
 * @dest: destination node on the card that will receive the command
 * @cmd: command data
 * @cmd_size: the command size
 */
static void prepare_cell(struct hinic_api_cmd_chain *chain,
			 enum  hinic_node_id dest,
				void *cmd, u16 cmd_size)
{
	struct hinic_api_cmd_cell *curr_node;
	u16 cell_size;

	curr_node = chain->curr_node;

	cell_size = get_cell_data_size(chain->chain_type, cmd_size);

	prepare_cell_ctrl(&curr_node->ctrl, cell_size);
	prepare_api_cmd(chain, curr_node, dest, cmd, cmd_size);
}

static inline void cmd_chain_prod_idx_inc(struct hinic_api_cmd_chain *chain)
{
	chain->prod_idx = MASKED_IDX(chain, chain->prod_idx + 1);
}

static void issue_api_cmd(struct hinic_api_cmd_chain *chain)
{
	set_prod_idx(chain);
}

/**
 * api_cmd_status_update - update the status of the chain
 * @chain: chain to update
 */
static void api_cmd_status_update(struct hinic_api_cmd_chain *chain)
{
	struct hinic_api_cmd_status *wb_status;
	enum hinic_api_cmd_chain_type chain_type;
	u64	status_header;
	u32	buf_desc;

	wb_status = chain->wb_status;

	buf_desc = be32_to_cpu(wb_status->buf_desc);
	if (HINIC_API_CMD_STATUS_GET(buf_desc, CHKSUM_ERR))
		return;

	status_header = be64_to_cpu(wb_status->header);
	chain_type = HINIC_API_CMD_STATUS_HEADER_GET(status_header, CHAIN_ID);
	if (chain_type >= HINIC_API_CMD_MAX)
		return;

	if (chain_type != chain->chain_type)
		return;

	chain->cons_idx = HINIC_API_CMD_STATUS_GET(buf_desc, CONS_IDX);
}

/**
 * wait_for_status_poll - wait for write to mgmt command to complete
 * @chain: the chain of the command
 * Return: 0 - success, negative - failure
 */
static int wait_for_status_poll(struct hinic_api_cmd_chain *chain)
{
	int err = -ETIMEDOUT;
	u32 cnt = 0;

	while (cnt < API_CMD_STATUS_TIMEOUT &&
	       chain->hwdev->chip_present_flag) {
		api_cmd_status_update(chain);

		/* SYNC API CMD cmd should start after prev cmd finished */
		if (chain->cons_idx == chain->prod_idx) {
			err = 0;
			break;
		}

		usleep_range(50, 100);
		cnt++;
	}

	return err;
}

static void copy_resp_data(struct hinic_api_cmd_cell_ctxt *ctxt, void *ack,
			   u16 ack_size)
{
	struct hinic_api_cmd_resp_fmt *resp = ctxt->resp;

	memcpy(ack, &resp->resp_data, ack_size);
	ctxt->status = 0;
}

/**
 * prepare_cell - polling for respense data of the read api-command
 * @ctxt: pointer to api cmd cell ctxt
 *
 * Return: 0 - success, negative - failure
 */
static int wait_for_resp_polling(struct hinic_api_cmd_cell_ctxt *ctxt)
{
	u64 resp_header;
	int ret = -ETIMEDOUT;
	u32 cnt = 0;

	while (cnt < POLLING_COMPLETION_TIMEOUT_DEFAULT) {
		resp_header = be64_to_cpu(ctxt->resp->header);

		rmb(); /* read the latest header */

		if (HINIC_API_CMD_RESP_HEADER_VALID(resp_header)) {
			ret = 0;
			break;
		}
		usleep_range(100, 1000);
		cnt++;
	}

	if (ret)
		pr_err("Wait for api chain response timeout\n");

	return ret;
}

/**
 * wait_for_api_cmd_completion - wait for command to complete
 * @chain: chain for the command
 * @ctxt: pointer to api cmd cell ctxt
 * @ack: pointer to ack message
 * @ack_size: the size of ack message
 * Return: 0 - success, negative - failure
 */
static int wait_for_api_cmd_completion(struct hinic_api_cmd_chain *chain,
				       struct hinic_api_cmd_cell_ctxt *ctxt,
				       void *ack, u16 ack_size)
{
	void *dev = chain->hwdev->dev_hdl;
	int err = 0;

	switch (chain->chain_type) {
	case HINIC_API_CMD_POLL_READ:
		err = wait_for_resp_polling(ctxt);
		if (!err)
			copy_resp_data(ctxt, ack, ack_size);
		break;
	case HINIC_API_CMD_POLL_WRITE:
	case HINIC_API_CMD_WRITE_TO_MGMT_CPU:
		err = wait_for_status_poll(chain);
		if (err) {
			sdk_err(dev, "API CMD Poll status timeout, chain type: %d\n",
				chain->chain_type);
			break;
		}
		break;
	case HINIC_API_CMD_WRITE_ASYNC_TO_MGMT_CPU:
		/* No need to wait */
		break;
	default:
		sdk_err(dev, "Unknown API CMD Chain type: %d\n",
			chain->chain_type);
		err = -EINVAL;
		break;
	}

	if (err)
		dump_api_chain_reg(chain);

	return err;
}

static inline void update_api_cmd_ctxt(struct hinic_api_cmd_chain *chain,
				       struct hinic_api_cmd_cell_ctxt *ctxt)
{
	ctxt->status = 1;
	ctxt->saved_prod_idx = chain->prod_idx;
	if (ctxt->resp) {
		ctxt->resp->header = 0;

		/* make sure "header" was cleared */
		wmb();
	}
}

/**
 * api_cmd - API CMD command
 * @chain: chain for the command
 * @dest: destination node on the card that will receive the command
 * @cmd: command data
 * @cmd_size: the command size
 * @ack: the buffer for ack
 * @ack_size: the size of ack
 * Return: 0 - success, negative - failure
 */
static int api_cmd(struct hinic_api_cmd_chain *chain,
		   enum hinic_node_id dest,
		   void *cmd, u16 cmd_size, void *ack, u16 ack_size)
{
	struct hinic_api_cmd_cell_ctxt *ctxt;

	if (chain->chain_type == HINIC_API_CMD_WRITE_ASYNC_TO_MGMT_CPU)
		spin_lock(&chain->async_lock);
	else
		down(&chain->sem);
	ctxt = &chain->cell_ctxt[chain->prod_idx];
	if (chain_busy(chain)) {
		if (chain->chain_type == HINIC_API_CMD_WRITE_ASYNC_TO_MGMT_CPU)
			spin_unlock(&chain->async_lock);
		else
			up(&chain->sem);
		return -EBUSY;
	}
	update_api_cmd_ctxt(chain, ctxt);

	prepare_cell(chain, dest, cmd, cmd_size);

	cmd_chain_prod_idx_inc(chain);

	wmb();	/* issue the command */

	issue_api_cmd(chain);

	/* incremented prod idx, update ctxt */

	chain->curr_node = chain->cell_ctxt[chain->prod_idx].cell_vaddr;
	if (chain->chain_type == HINIC_API_CMD_WRITE_ASYNC_TO_MGMT_CPU)
		spin_unlock(&chain->async_lock);
	else
		up(&chain->sem);

	return wait_for_api_cmd_completion(chain, ctxt, ack, ack_size);
}

/**
 * hinic_api_cmd_write - Write API CMD command
 * @chain: chain for write command
 * @dest: destination node on the card that will receive the command
 * @cmd: command data
 * @size: the command size
 * Return: 0 - success, negative - failure
 */
int hinic_api_cmd_write(struct hinic_api_cmd_chain *chain,
			enum hinic_node_id dest, void *cmd, u16 size)
{
	/* Verify the chain type */
	return api_cmd(chain, dest, cmd, size, NULL, 0);
}

int hinic_api_cmd_read(struct hinic_api_cmd_chain *chain,
		       enum hinic_node_id dest,
			void *cmd, u16 size, void *ack, u16 ack_size)
{
	return api_cmd(chain, dest, cmd, size, ack, ack_size);
}

/**
 * api_cmd_hw_restart - restart the chain in the HW
 * @cmd_chain: the API CMD specific chain to restart
 */
static int api_cmd_hw_restart(struct hinic_api_cmd_chain *cmd_chain)
{
	struct hinic_hwif *hwif = cmd_chain->hwdev->hwif;
	u32 reg_addr, val;
	int err;
	u32 cnt = 0;

	/* Read Modify Write */
	reg_addr = HINIC_CSR_API_CMD_CHAIN_REQ_ADDR(cmd_chain->chain_type);
	val = hinic_hwif_read_reg(hwif, reg_addr);

	val = HINIC_API_CMD_CHAIN_REQ_CLEAR(val, RESTART);
	val |= HINIC_API_CMD_CHAIN_REQ_SET(1, RESTART);

	hinic_hwif_write_reg(hwif, reg_addr, val);

	err = -ETIMEDOUT;
	while (cnt < API_CMD_TIMEOUT) {
		val = hinic_hwif_read_reg(hwif, reg_addr);

		if (!HINIC_API_CMD_CHAIN_REQ_GET(val, RESTART)) {
			err = 0;
			break;
		}

		usleep_range(900, 1000);
		cnt++;
	}

	return err;
}

/**
 * api_cmd_ctrl_init - set the control register of a chain
 * @chain: the API CMD specific chain to set control register for
 */
static void api_cmd_ctrl_init(struct hinic_api_cmd_chain *chain)
{
	struct hinic_hwif *hwif = chain->hwdev->hwif;
	u32 reg_addr, ctrl;
	u32 size;

	/* Read Modify Write */
	reg_addr = HINIC_CSR_API_CMD_CHAIN_CTRL_ADDR(chain->chain_type);

	size = (u32)ilog2(chain->cell_size >> API_CMD_CHAIN_CELL_SIZE_SHIFT);

	ctrl = hinic_hwif_read_reg(hwif, reg_addr);

	ctrl = HINIC_API_CMD_CHAIN_CTRL_CLEAR(ctrl, AEQE_EN) &
		HINIC_API_CMD_CHAIN_CTRL_CLEAR(ctrl, CELL_SIZE);

	ctrl |= HINIC_API_CMD_CHAIN_CTRL_SET(0, AEQE_EN) |
		HINIC_API_CMD_CHAIN_CTRL_SET(size, CELL_SIZE);

	hinic_hwif_write_reg(hwif, reg_addr, ctrl);
}

/**
 * api_cmd_set_status_addr - set the status address of a chain in the HW
 * @chain: the API CMD specific chain to set status address for
 */
static void api_cmd_set_status_addr(struct hinic_api_cmd_chain *chain)
{
	struct hinic_hwif *hwif = chain->hwdev->hwif;
	u32 addr, val;

	addr = HINIC_CSR_API_CMD_STATUS_HI_ADDR(chain->chain_type);
	val = upper_32_bits(chain->wb_status_paddr);
	hinic_hwif_write_reg(hwif, addr, val);

	addr = HINIC_CSR_API_CMD_STATUS_LO_ADDR(chain->chain_type);
	val = lower_32_bits(chain->wb_status_paddr);
	hinic_hwif_write_reg(hwif, addr, val);
}

/**
 * api_cmd_set_num_cells - set the number cells of a chain in the HW
 * @chain: the API CMD specific chain to set the number of cells for
 */
static void api_cmd_set_num_cells(struct hinic_api_cmd_chain *chain)
{
	struct hinic_hwif *hwif = chain->hwdev->hwif;
	u32 addr, val;

	addr = HINIC_CSR_API_CMD_CHAIN_NUM_CELLS_ADDR(chain->chain_type);
	val  = chain->num_cells;
	hinic_hwif_write_reg(hwif, addr, val);
}

/**
 * api_cmd_head_init - set the head cell of a chain in the HW
 * @chain: the API CMD specific chain to set the head for
 */
static void api_cmd_head_init(struct hinic_api_cmd_chain *chain)
{
	struct hinic_hwif *hwif = chain->hwdev->hwif;
	u32 addr, val;

	addr = HINIC_CSR_API_CMD_CHAIN_HEAD_HI_ADDR(chain->chain_type);
	val = upper_32_bits(chain->head_cell_paddr);
	hinic_hwif_write_reg(hwif, addr, val);

	addr = HINIC_CSR_API_CMD_CHAIN_HEAD_LO_ADDR(chain->chain_type);
	val = lower_32_bits(chain->head_cell_paddr);
	hinic_hwif_write_reg(hwif, addr, val);
}

/**
 * wait_for_ready_chain - wait for the chain to be ready
 * @chain: the API CMD specific chain to wait for
 * Return: 0 - success, negative - failure
 */
static int wait_for_ready_chain(struct hinic_api_cmd_chain *chain)
{
	struct hinic_hwif *hwif = chain->hwdev->hwif;
	u32 addr, val;
	u32 hw_cons_idx;
	u32 cnt = 0;
	int err;

	addr = HINIC_CSR_API_CMD_STATUS_0_ADDR(chain->chain_type);
	err = -ETIMEDOUT;
	while (cnt < API_CMD_TIMEOUT) {
		val = hinic_hwif_read_reg(hwif, addr);
		hw_cons_idx = HINIC_API_CMD_STATUS_GET(val, CONS_IDX);

		/* wait for HW cons idx to be updated */
		if (hw_cons_idx == chain->cons_idx) {
			err = 0;
			break;
		}

		usleep_range(900, 1000);
		cnt++;
	}

	return err;
}

/**
 * api_cmd_chain_hw_clean - clean the HW
 * @chain: the API CMD specific chain
 */
static void api_cmd_chain_hw_clean(struct hinic_api_cmd_chain *chain)
{
	struct hinic_hwif *hwif = chain->hwdev->hwif;
	u32 addr, ctrl;

	addr = HINIC_CSR_API_CMD_CHAIN_CTRL_ADDR(chain->chain_type);

	ctrl = hinic_hwif_read_reg(hwif, addr);
	ctrl = HINIC_API_CMD_CHAIN_CTRL_CLEAR(ctrl, RESTART_EN) &
	       HINIC_API_CMD_CHAIN_CTRL_CLEAR(ctrl, XOR_ERR)    &
	       HINIC_API_CMD_CHAIN_CTRL_CLEAR(ctrl, AEQE_EN)    &
	       HINIC_API_CMD_CHAIN_CTRL_CLEAR(ctrl, XOR_CHK_EN) &
	       HINIC_API_CMD_CHAIN_CTRL_CLEAR(ctrl, CELL_SIZE);

	hinic_hwif_write_reg(hwif, addr, ctrl);
}

/**
 * api_cmd_chain_hw_init - initialize the chain in the HW
 * @chain: the API CMD specific chain to initialize in HW
 * Return: 0 - success, negative - failure
 */
static int api_cmd_chain_hw_init(struct hinic_api_cmd_chain *chain)
{
	api_cmd_chain_hw_clean(chain);

	api_cmd_set_status_addr(chain);

	if (api_cmd_hw_restart(chain)) {
		sdk_err(chain->hwdev->dev_hdl, "Failed to restart api_cmd_hw\n");
		return -EBUSY;
	}

	api_cmd_ctrl_init(chain);
	api_cmd_set_num_cells(chain);
	api_cmd_head_init(chain);

	return wait_for_ready_chain(chain);
}

/**
 * alloc_cmd_buf - allocate a dma buffer for API CMD command
 * @chain: the API CMD specific chain for the cmd
 * @cell: the cell in the HW for the cmd
 * @cell_idx: the index of the cell
 * Return: 0 - success, negative - failure
 */
static int alloc_cmd_buf(struct hinic_api_cmd_chain *chain,
			 struct hinic_api_cmd_cell *cell, u32 cell_idx)
{
	struct hinic_api_cmd_cell_ctxt *cell_ctxt;
	void *dev = chain->hwdev->dev_hdl;
	void *buf_vaddr;
	u64 buf_paddr;
	int err = 0;

	buf_vaddr = (u8 *)((u64)chain->buf_vaddr_base +
		chain->buf_size_align * cell_idx);
	buf_paddr = chain->buf_paddr_base +
		chain->buf_size_align * cell_idx;

	cell_ctxt = &chain->cell_ctxt[cell_idx];

	cell_ctxt->api_cmd_vaddr = buf_vaddr;

	/* set the cmd DMA address in the cell */
	switch (chain->chain_type) {
	case HINIC_API_CMD_POLL_READ:
		cell->read.hw_cmd_paddr = cpu_to_be64(buf_paddr);
		break;
	case HINIC_API_CMD_WRITE_TO_MGMT_CPU:
	case HINIC_API_CMD_POLL_WRITE:
	case HINIC_API_CMD_WRITE_ASYNC_TO_MGMT_CPU:
		/* The data in the HW should be in Big Endian Format */
		cell->write.hw_cmd_paddr = cpu_to_be64(buf_paddr);
		break;
	default:
		sdk_err(dev, "Unknown API CMD Chain type: %d\n",
			chain->chain_type);
		err = -EINVAL;
		break;
	}

	return err;
}

static void alloc_resp_buf(struct hinic_api_cmd_chain *chain,
			   struct hinic_api_cmd_cell *cell, u32 cell_idx)
{
	struct hinic_api_cmd_cell_ctxt *cell_ctxt;
	void *resp_vaddr;
	u64 resp_paddr;

	resp_vaddr = (u8 *)((u64)chain->rsp_vaddr_base +
		chain->rsp_size_align * cell_idx);
	resp_paddr = chain->rsp_paddr_base +
		chain->rsp_size_align * cell_idx;

	cell_ctxt = &chain->cell_ctxt[cell_idx];

	cell_ctxt->resp = resp_vaddr;
	cell->read.hw_wb_resp_paddr = cpu_to_be64(resp_paddr);
}

static int hinic_alloc_api_cmd_cell_buf(struct hinic_api_cmd_chain *chain,
					u32 cell_idx,
					struct hinic_api_cmd_cell *node)
{
	void *dev = chain->hwdev->dev_hdl;
	int err;

	/* For read chain, we should allocate buffer for the response data */
	if (chain->chain_type == HINIC_API_CMD_MULTI_READ ||
	    chain->chain_type == HINIC_API_CMD_POLL_READ)
		alloc_resp_buf(chain, node, cell_idx);

	switch (chain->chain_type) {
	case HINIC_API_CMD_WRITE_TO_MGMT_CPU:
	case HINIC_API_CMD_POLL_WRITE:
	case HINIC_API_CMD_POLL_READ:
	case HINIC_API_CMD_WRITE_ASYNC_TO_MGMT_CPU:
		err = alloc_cmd_buf(chain, node, cell_idx);
		if (err) {
			sdk_err(dev, "Failed to allocate cmd buffer\n");
			goto alloc_cmd_buf_err;
		}
		break;
	/* For api command write and api command read, the data section
	 * is directly inserted in the cell, so no need to allocate.
	 */
	case HINIC_API_CMD_MULTI_READ:
		chain->cell_ctxt[cell_idx].api_cmd_vaddr =
			&node->read.hw_cmd_paddr;
		break;
	default:
		sdk_err(dev, "Unsupported API CMD chain type\n");
		err = -EINVAL;
		goto alloc_cmd_buf_err;
	}

	return 0;

alloc_cmd_buf_err:

	return err;
}

/**
 * api_cmd_create_cell - create API CMD cell of specific chain
 * @chain: the API CMD specific chain to create its cell
 * @cell_idx: the cell index to create
 * @pre_node: previous cell
 * @node_vaddr: the virt addr of the cell
 * Return: 0 - success, negative - failure
 */
static int api_cmd_create_cell(struct hinic_api_cmd_chain *chain, u32 cell_idx,
			       struct hinic_api_cmd_cell *pre_node,
			       struct hinic_api_cmd_cell **node_vaddr)
{
	struct hinic_api_cmd_cell_ctxt *cell_ctxt;
	struct hinic_api_cmd_cell *node;
	void *cell_vaddr;
	u64 cell_paddr;
	int err;

	cell_vaddr = (void *)((u64)chain->cell_vaddr_base +
		chain->cell_size_align * cell_idx);
	cell_paddr = chain->cell_paddr_base +
		chain->cell_size_align * cell_idx;

	cell_ctxt = &chain->cell_ctxt[cell_idx];
	cell_ctxt->cell_vaddr = cell_vaddr;
	node = cell_ctxt->cell_vaddr;

	if (!pre_node) {
		chain->head_node = cell_vaddr;
		chain->head_cell_paddr = cell_paddr;
	} else {
		/* The data in the HW should be in Big Endian Format */
		pre_node->next_cell_paddr = cpu_to_be64(cell_paddr);
	}

	/* Driver software should make sure that there is an empty API
	 * command cell at the end the chain
	 */
	node->next_cell_paddr = 0;

	err = hinic_alloc_api_cmd_cell_buf(chain, cell_idx, node);
	if (err)
		return err;

	*node_vaddr = node;

	return 0;
}

/**
 * api_cmd_create_cells - create API CMD cells for specific chain
 * @chain: the API CMD specific chain
 * Return: 0 - success, negative - failure
 */
static int api_cmd_create_cells(struct hinic_api_cmd_chain *chain)
{
	struct hinic_api_cmd_cell *node = NULL, *pre_node = NULL;
	void *dev = chain->hwdev->dev_hdl;
	u32 cell_idx;
	int err;

	for (cell_idx = 0; cell_idx < chain->num_cells; cell_idx++) {
		err = api_cmd_create_cell(chain, cell_idx, pre_node, &node);
		if (err) {
			sdk_err(dev, "Failed to create API CMD cell\n");
			return err;
		}

		pre_node = node;
	}

	if (!node)
		return -EFAULT;

	/* set the Final node to point on the start */
	node->next_cell_paddr = cpu_to_be64(chain->head_cell_paddr);

	/* set the current node to be the head */
	chain->curr_node = chain->head_node;
	return 0;
}

/**
 * api_chain_init - initialize API CMD specific chain
 * @chain: the API CMD specific chain to initialize
 * @attr: attributes to set in the chain
 * Return: 0 - success, negative - failure
 */
static int api_chain_init(struct hinic_api_cmd_chain *chain,
			  struct hinic_api_cmd_chain_attr *attr)
{
	void *dev = chain->hwdev->dev_hdl;
	size_t cell_ctxt_size;
	size_t cells_buf_size;
	int err;

	chain->chain_type  = attr->chain_type;
	chain->num_cells = attr->num_cells;
	chain->cell_size = attr->cell_size;
	chain->rsp_size = attr->rsp_size;

	chain->prod_idx  = 0;
	chain->cons_idx  = 0;

	if (chain->chain_type == HINIC_API_CMD_WRITE_ASYNC_TO_MGMT_CPU)
		spin_lock_init(&chain->async_lock);
	else
		sema_init(&chain->sem, 1);

	cell_ctxt_size = chain->num_cells * sizeof(*chain->cell_ctxt);
	if (!cell_ctxt_size) {
		sdk_err(dev, "Api chain cell size cannot be zero\n");
		err = -EINVAL;
		goto alloc_cell_ctxt_err;
	}

	chain->cell_ctxt = kzalloc(cell_ctxt_size, GFP_KERNEL);
	if (!chain->cell_ctxt) {
		sdk_err(dev, "Failed to allocate cell contexts for a chain\n");
		err = -ENOMEM;
		goto alloc_cell_ctxt_err;
	}

	chain->wb_status = dma_zalloc_coherent(dev,
					       sizeof(*chain->wb_status),
					       &chain->wb_status_paddr,
					       GFP_KERNEL);
	if (!chain->wb_status) {
		sdk_err(dev, "Failed to allocate DMA wb status\n");
		err = -ENOMEM;
		goto alloc_wb_status_err;
	}

	chain->cell_size_align = ALIGN((u64)chain->cell_size,
				       API_CMD_NODE_ALIGN_SIZE);
	chain->rsp_size_align = ALIGN((u64)chain->rsp_size,
				      API_CHAIN_RESP_ALIGNMENT);
	chain->buf_size_align = ALIGN(API_CMD_BUF_SIZE, API_PAYLOAD_ALIGN_SIZE);

	cells_buf_size = (chain->cell_size_align + chain->rsp_size_align +
			  chain->buf_size_align) * chain->num_cells;

	err = hinic_dma_zalloc_coherent_align(dev, cells_buf_size,
					      API_CMD_NODE_ALIGN_SIZE,
					      GFP_KERNEL,
					      &chain->cells_addr);
	if (err) {
		sdk_err(dev, "Failed to allocate API CMD cells buffer\n");
		goto alloc_cells_buf_err;
	}

	chain->cell_vaddr_base = chain->cells_addr.align_vaddr;
	chain->cell_paddr_base = chain->cells_addr.align_paddr;

	chain->rsp_vaddr_base = (u8 *)((u64)chain->cell_vaddr_base +
		chain->cell_size_align * chain->num_cells);
	chain->rsp_paddr_base = chain->cell_paddr_base +
		chain->cell_size_align * chain->num_cells;

	chain->buf_vaddr_base = (u8 *)((u64)chain->rsp_vaddr_base +
		chain->rsp_size_align * chain->num_cells);
	chain->buf_paddr_base = chain->rsp_paddr_base +
		chain->rsp_size_align * chain->num_cells;

	return 0;

alloc_cells_buf_err:
	dma_free_coherent(dev, sizeof(*chain->wb_status),
			  chain->wb_status, chain->wb_status_paddr);

alloc_wb_status_err:
	kfree(chain->cell_ctxt);

alloc_cell_ctxt_err:
	if (chain->chain_type == HINIC_API_CMD_WRITE_ASYNC_TO_MGMT_CPU)
		spin_lock_deinit(&chain->async_lock);
	else
		sema_deinit(&chain->sem);
	return err;
}

/**
 * api_chain_free - free API CMD specific chain
 * @chain: the API CMD specific chain to free
 */
static void api_chain_free(struct hinic_api_cmd_chain *chain)
{
	void *dev = chain->hwdev->dev_hdl;

	hinic_dma_free_coherent_align(dev, &chain->cells_addr);

	dma_free_coherent(dev, sizeof(*chain->wb_status),
			  chain->wb_status, chain->wb_status_paddr);
	kfree(chain->cell_ctxt);

	if (chain->chain_type == HINIC_API_CMD_WRITE_ASYNC_TO_MGMT_CPU)
		spin_lock_deinit(&chain->async_lock);
	else
		sema_deinit(&chain->sem);
}

/**
 * api_cmd_create_chain - create API CMD specific chain
 * @chain: the API CMD specific chain to create
 * @attr: attributes to set in the chain
 * Return: 0 - success, negative - failure
 */
static int api_cmd_create_chain(struct hinic_api_cmd_chain **cmd_chain,
				struct hinic_api_cmd_chain_attr *attr)
{
	struct hinic_hwdev *hwdev = attr->hwdev;
	struct hinic_api_cmd_chain *chain;
	int err;

	if (attr->num_cells & (attr->num_cells - 1)) {
		sdk_err(hwdev->dev_hdl, "Invalid number of cells, must be power of 2\n");
		return -EINVAL;
	}

	chain = kzalloc(sizeof(*chain), GFP_KERNEL);
	if (!chain)
		return -ENOMEM;

	chain->hwdev = hwdev;

	err = api_chain_init(chain, attr);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to initialize chain\n");
		goto chain_init_err;
	}

	err = api_cmd_create_cells(chain);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to create cells for API CMD chain\n");
		goto create_cells_err;
	}

	err = api_cmd_chain_hw_init(chain);
	if (err) {
		sdk_err(hwdev->dev_hdl, "Failed to initialize chain HW\n");
		goto chain_hw_init_err;
	}

	*cmd_chain = chain;
	return 0;

chain_hw_init_err:
create_cells_err:
	api_chain_free(chain);

chain_init_err:
	kfree(chain);
	return err;
}

/**
 * api_cmd_destroy_chain - destroy API CMD specific chain
 * @chain: the API CMD specific chain to destroy
 */
static void api_cmd_destroy_chain(struct hinic_api_cmd_chain *chain)
{
	api_chain_free(chain);
	kfree(chain);
}

/**
 * hinic_api_cmd_init - Initialize all the API CMD chains
 * @hwdev: the pointer to hw device
 * @chain: the API CMD chains that will be initialized
 * Return: 0 - success, negative - failure
 */
int hinic_api_cmd_init(struct hinic_hwdev *hwdev,
		       struct hinic_api_cmd_chain **chain)
{
	void *dev = hwdev->dev_hdl;
	struct hinic_api_cmd_chain_attr attr;
	enum hinic_api_cmd_chain_type chain_type, i;
	int err;

	attr.hwdev = hwdev;
	attr.num_cells  = API_CHAIN_NUM_CELLS;
	attr.cell_size  = API_CHAIN_CELL_SIZE;
	attr.rsp_size	= API_CHAIN_RSP_DATA_SIZE;

	chain_type = HINIC_API_CMD_WRITE_TO_MGMT_CPU;
	for (; chain_type < HINIC_API_CMD_MAX; chain_type++) {
		attr.chain_type = chain_type;

		err = api_cmd_create_chain(&chain[chain_type], &attr);
		if (err) {
			sdk_err(dev, "Failed to create chain %d\n", chain_type);
			goto create_chain_err;
		}
	}

	return 0;

create_chain_err:
	i = HINIC_API_CMD_WRITE_TO_MGMT_CPU;
	for (; i < chain_type; i++)
		api_cmd_destroy_chain(chain[i]);

	return err;
}

/**
 * hinic_api_cmd_free - free the API CMD chains
 * @chain: the API CMD chains that will be freed
 */
void hinic_api_cmd_free(struct hinic_api_cmd_chain **chain)
{
	enum hinic_api_cmd_chain_type chain_type;

	chain_type = HINIC_API_CMD_WRITE_TO_MGMT_CPU;

	for (; chain_type < HINIC_API_CMD_MAX; chain_type++)
		api_cmd_destroy_chain(chain[chain_type]);
}
