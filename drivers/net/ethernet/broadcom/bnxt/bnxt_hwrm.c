/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2020 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <asm/byteorder.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/errno.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/skbuff.h>

#include "bnxt_compat.h"
#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_hwrm.h"

static u64 hwrm_calc_sentinel(struct bnxt_hwrm_ctx *ctx, u16 req_type)
{
	return (((u64)ctx) + req_type) ^ BNXT_HWRM_SENTINEL;
}

int __hwrm_req_init(struct bnxt *bp, void **req, u16 req_type, u32 req_len)
{
	struct bnxt_hwrm_ctx *ctx;
	dma_addr_t dma_handle;
	u8 *req_addr;

	if (req_len > BNXT_HWRM_CTX_OFFSET)
		return -E2BIG;

	req_addr = dma_pool_alloc(bp->hwrm_dma_pool, GFP_KERNEL | __GFP_ZERO,
				  &dma_handle);
	if (!req_addr)
		return -ENOMEM;

	ctx = (struct bnxt_hwrm_ctx *)(req_addr + BNXT_HWRM_CTX_OFFSET);
	/* safety first, sentinel used to check for invalid requests */
	ctx->sentinel = hwrm_calc_sentinel(ctx, req_type);
	ctx->req_len = req_len;
	ctx->req = (struct input *)req_addr;
	ctx->resp = (struct output *)(req_addr + BNXT_HWRM_RESP_OFFSET);
	ctx->dma_handle = dma_handle;
	ctx->flags = 0; /* __GFP_ZERO, but be explicit regarding ownership */
	ctx->timeout = bp->hwrm_cmd_timeout ?: DFLT_HWRM_CMD_TIMEOUT;
	ctx->allocated = BNXT_HWRM_DMA_SIZE - BNXT_HWRM_CTX_OFFSET;
	ctx->gfp = GFP_KERNEL;
	ctx->slice_addr = NULL;

	/* initialize common request fields */
	ctx->req->req_type = cpu_to_le16(req_type);
	ctx->req->resp_addr = cpu_to_le64(dma_handle + BNXT_HWRM_RESP_OFFSET);
	ctx->req->cmpl_ring = cpu_to_le16(BNXT_HWRM_NO_CMPL_RING);
	ctx->req->target_id = cpu_to_le16(BNXT_HWRM_TARGET);
	*req = ctx->req;

	return 0;
}

static struct bnxt_hwrm_ctx *__hwrm_ctx(struct bnxt *bp, u8 *req_addr)
{
	void *ctx_addr = req_addr + BNXT_HWRM_CTX_OFFSET;
	struct input *req = (struct input *)req_addr;
	struct bnxt_hwrm_ctx *ctx = ctx_addr;
	u64 sentinel;

	if (!req) {
		/* can only be due to software bug, be loud */
		netdev_err(bp->dev, "null HWRM request");
		dump_stack();
		return NULL;
	}

	/* HWRM API has no type safety, verify sentinel to validate address */
	sentinel = hwrm_calc_sentinel(ctx, le16_to_cpu(req->req_type));
	if (ctx->sentinel != sentinel) {
		/* can only be due to software bug, be loud */
		netdev_err(bp->dev, "HWRM sentinel mismatch, req_type = %u\n",
			   (u32)le16_to_cpu(req->req_type));
		dump_stack();
		return NULL;
	}

	return ctx;
}

void hwrm_req_silence(struct bnxt *bp, void *req)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);

	if (ctx)
		ctx->flags |= BNXT_HWRM_CTX_SILENT;
}

void hwrm_req_timeout(struct bnxt *bp, void *req, int timeout)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);

	if (ctx)
		ctx->timeout = timeout;
}

void hwrm_req_alloc_flags(struct bnxt *bp, void *req, gfp_t gfp)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);

	if (ctx)
		ctx->gfp = gfp;
}

int hwrm_req_replace(struct bnxt *bp, void *req, void *new_req, u32 len)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);
	struct input *internal_req = req;
	u16 req_type;

	if (!ctx)
		return -EINVAL;

	if (len > BNXT_HWRM_CTX_OFFSET)
		return -E2BIG;

	/* free any existing slices */
	ctx->allocated = BNXT_HWRM_DMA_SIZE - BNXT_HWRM_CTX_OFFSET;
	if (ctx->slice_addr) {
		dma_free_coherent(&bp->pdev->dev, ctx->slice_size,
				  ctx->slice_addr, ctx->slice_handle);
		ctx->slice_addr = NULL;
	}
	ctx->gfp = GFP_KERNEL;

	if ((bp->fw_cap & BNXT_FW_CAP_SHORT_CMD) || len > BNXT_HWRM_MAX_REQ_LEN) {
		memcpy(internal_req, new_req, len);
	} else {
		internal_req->req_type = ((struct input *)new_req)->req_type;
		ctx->req = new_req;
	}

	ctx->req_len = len;
	ctx->req->resp_addr = cpu_to_le64(ctx->dma_handle +
					  BNXT_HWRM_RESP_OFFSET);

	/* update sentinel for potentially new request type */
	req_type = le16_to_cpu(internal_req->req_type);
	ctx->sentinel = hwrm_calc_sentinel(ctx, req_type);

	return 0;
}

void hwrm_req_capture_ts(struct bnxt *bp, void *req,
			 struct ptp_system_timestamp *sts)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);

	if (ctx)
		ctx->sts = sts;
}

void *hwrm_req_hold(struct bnxt *bp, void *req)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);
	struct input *input = (struct input *)req;

	if (!ctx)
		return NULL;

	if (ctx->flags & BNXT_HWRM_CTX_OWNED) {
		/* can only be due to software bug, be loud */
		netdev_err(bp->dev, "HWRM context already owned, req_type = %u\n",
			   (u32)le16_to_cpu(input->req_type));
		dump_stack();
		return NULL;
	}

	ctx->flags |= BNXT_HWRM_CTX_OWNED;
	return ((u8 *)req) + BNXT_HWRM_RESP_OFFSET;
}

static void __hwrm_ctx_drop(struct bnxt *bp, struct bnxt_hwrm_ctx *ctx)
{
	void *addr = ((u8 *)ctx) - BNXT_HWRM_CTX_OFFSET;
	dma_addr_t dma_handle = ctx->dma_handle; /* save before invalidate */

	/* unmap any auxiliary DMA slice */
	if (ctx->slice_addr)
		dma_free_coherent(&bp->pdev->dev, ctx->slice_size,
				  ctx->slice_addr, ctx->slice_handle);

	/* invalidate, ensure ownership, sentinel and dma_handle are cleared */
	memset(ctx, 0, sizeof(struct bnxt_hwrm_ctx));

	/* return the buffer to the DMA pool */
	if (dma_handle)
		dma_pool_free(bp->hwrm_dma_pool, addr, dma_handle);
}

/* void because failure is irrecoverable */
void hwrm_req_drop(struct bnxt *bp, void *req)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);

	if (ctx)
		__hwrm_ctx_drop(bp, ctx);
}

static int __hwrm_to_stderr(u32 hwrm_err)
{
	switch (hwrm_err) {
	case HWRM_ERR_CODE_SUCCESS:
		return 0;
	case HWRM_ERR_CODE_RESOURCE_ACCESS_DENIED:
		return -EACCES;
	case HWRM_ERR_CODE_RESOURCE_ALLOC_ERROR:
		return -ENOSPC;
	case HWRM_ERR_CODE_INVALID_PARAMS:
	case HWRM_ERR_CODE_INVALID_FLAGS:
	case HWRM_ERR_CODE_INVALID_ENABLES:
	case HWRM_ERR_CODE_UNSUPPORTED_TLV:
	case HWRM_ERR_CODE_UNSUPPORTED_OPTION_ERR:
		return -EINVAL;
	case HWRM_ERR_CODE_NO_BUFFER:
		return -ENOMEM;
	case HWRM_ERR_CODE_HOT_RESET_PROGRESS:
	case HWRM_ERR_CODE_BUSY:
		return -EAGAIN;
	case HWRM_ERR_CODE_CMD_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	default:
		return -EIO;
	}
}

static struct bnxt_hwrm_wait_token *
__hwrm_acquire_token(struct bnxt *bp, enum bnxt_hwrm_chnl dst)
	__acquires(&bp->hwrm_cmd_lock)
{
	struct bnxt_hwrm_wait_token *token;

	token = kzalloc(sizeof(*token), GFP_KERNEL);
	if (!token)
		return NULL;

	mutex_lock(&bp->hwrm_cmd_lock);

	token->dst = dst;
	token->state = BNXT_HWRM_PENDING;
	if (dst == BNXT_HWRM_CHNL_CHIMP) {
		token->seq_id = bp->hwrm_cmd_seq++;
		hlist_add_head_rcu(&token->node, &bp->hwrm_pending_list);
	} else {
		token->seq_id = bp->hwrm_cmd_kong_seq++;
	}

	return token;
}

static void
__hwrm_release_token(struct bnxt *bp, struct bnxt_hwrm_wait_token *token)
	__releases(&bp->hwrm_cmd_lock)
{
	if (token->dst == BNXT_HWRM_CHNL_CHIMP) {
		hlist_del_rcu(&token->node);
		kfree_rcu(token, rcu);
	} else {
		kfree(token);
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
}

void
hwrm_update_token(struct bnxt *bp, u16 seq_id, enum bnxt_hwrm_wait_state state)
{
	struct hlist_node __maybe_unused *dummy;
	struct bnxt_hwrm_wait_token *token;

	rcu_read_lock();
	__hlist_for_each_entry_rcu(token, dummy, &bp->hwrm_pending_list, node) {
		if (token->seq_id == seq_id) {
			WRITE_ONCE(token->state, state);
			rcu_read_unlock();
			return;
		}
	}
	rcu_read_unlock();
	netdev_err(bp->dev, "Invalid hwrm seq id %d\n", seq_id);
}

static int __hwrm_send(struct bnxt *bp, struct bnxt_hwrm_ctx *ctx)
{
	u32 doorbell_offset = BNXT_GRCPF_REG_CHIMP_COMM_TRIGGER;
	enum bnxt_hwrm_chnl dst = BNXT_HWRM_CHNL_CHIMP;
	u32 bar_offset = BNXT_GRCPF_REG_CHIMP_COMM;
	struct bnxt_hwrm_wait_token *token = NULL;
	struct hwrm_short_input short_input = {0};
	u16 max_req_len = BNXT_HWRM_MAX_REQ_LEN;
	int i, timeout, tmo_count, rc = -EBUSY;
	u32 *data = (u32 *)ctx->req;
	u32 msg_len = ctx->req_len;
	u16 len = 0;
	u8 *valid;

#ifndef HSI_DBG_DISABLE
	decode_hwrm_req(ctx->req);
#endif

	if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state))
		goto exit;

	if (msg_len > BNXT_HWRM_MAX_REQ_LEN &&
	    msg_len > bp->hwrm_max_ext_req_len) {
		rc = -E2BIG;
		goto exit;
	}

	if (hwrm_req_kong(bp, ctx->req)) {
		dst = BNXT_HWRM_CHNL_KONG;
		bar_offset = BNXT_GRCPF_REG_KONG_COMM;
		doorbell_offset = BNXT_GRCPF_REG_KONG_COMM_TRIGGER;
		if (le16_to_cpu(ctx->req->cmpl_ring) != INVALID_HW_RING_ID) {
			netdev_err(bp->dev, "Ring completions not supported for KONG commands, req_type = %d\n",
				   (u32)le16_to_cpu(ctx->req->req_type));
			rc = -EINVAL;
			goto exit;
		}
	}

	if (ctx->flags & BNXT_HWRM_RESP_DIRTY)
		memset(ctx->resp, 0, PAGE_SIZE);

	token = __hwrm_acquire_token(bp, dst);
	if (!token) {
		rc = -ENOMEM;
		goto exit;
	}
	ctx->req->seq_id = cpu_to_le16(token->seq_id);

	if ((bp->fw_cap & BNXT_FW_CAP_SHORT_CMD) ||
	    msg_len > BNXT_HWRM_MAX_REQ_LEN) {
		short_input.req_type = ctx->req->req_type;
		short_input.signature =
				cpu_to_le16(SHORT_REQ_SIGNATURE_SHORT_CMD);
		short_input.size = cpu_to_le16(msg_len);
		short_input.req_addr = cpu_to_le64(ctx->dma_handle);

		data = (u32 *)&short_input;
		msg_len = sizeof(short_input);

		max_req_len = BNXT_HWRM_SHORT_REQ_LEN;
	}

	/* Ensure any associated DMA buffers are written before doorbell */
	wmb();

	/* Write request msg to hwrm channel */
	__iowrite32_copy(bp->bar0 + bar_offset, data, msg_len / 4);

	for (i = msg_len; i < max_req_len; i += 4)
		writel(0, bp->bar0 + bar_offset + i);

	ptp_read_system_prets(ctx->sts);

	/* Ring channel doorbell */
	writel(1, bp->bar0 + doorbell_offset);

	if (!pci_is_enabled(bp->pdev)) {
		rc = 0; /* don't wait for response during error recovery */
		goto exit;
	}

	/* convert timeout to usec */
	timeout = ctx->timeout * 1000;

	i = 0;
	/* Short timeout for the first few iterations:
	 * number of loops = number of loops for short timeout +
	 * number of loops for standard timeout.
	 */
	tmo_count = HWRM_SHORT_TIMEOUT_COUNTER;
	timeout = timeout - HWRM_SHORT_MIN_TIMEOUT * HWRM_SHORT_TIMEOUT_COUNTER;
	tmo_count += DIV_ROUND_UP(timeout, HWRM_MIN_TIMEOUT);

	if (le16_to_cpu(ctx->req->cmpl_ring) != INVALID_HW_RING_ID) {
		/* Wait until hwrm response cmpl interrupt is processed */
		while (READ_ONCE(token->state) < BNXT_HWRM_COMPLETE &&
		       i++ < tmo_count) {
			/* Abort the wait for completion if the FW health
			 * check has failed.
			 */
			if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state))
				goto exit;
			/* on first few passes, just barely sleep */
			if (i < HWRM_SHORT_TIMEOUT_COUNTER)
				usleep_range(HWRM_SHORT_MIN_TIMEOUT,
					     HWRM_SHORT_MAX_TIMEOUT);
			else
				usleep_range(HWRM_MIN_TIMEOUT,
					     HWRM_MAX_TIMEOUT);
		}

		if (READ_ONCE(token->state) != BNXT_HWRM_COMPLETE) {
			if (!(ctx->flags & BNXT_HWRM_CTX_SILENT))
				netdev_err(bp->dev, "Resp cmpl intr err msg: 0x%x\n",
					   le16_to_cpu(ctx->req->req_type));
			goto exit;
		}
		len = le16_to_cpu(ctx->resp->resp_len);
		valid = ((u8 *)ctx->resp) + len - 1;
	} else {
		int j;

		/* Check if response len is updated */
		for (i = 0; i < tmo_count; i++) {
			/* Abort the wait for completion if the FW health
			 * check has failed.
			 */
			if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state))
				goto exit;

			if (token &&
			    READ_ONCE(token->state) == BNXT_HWRM_DEFERRED) {
				__hwrm_release_token(bp, token);
				token = NULL;
			}

			len = le16_to_cpu(ctx->resp->resp_len);
			if (len)
				break;
			/* on first few passes, just barely sleep */
			if (i < HWRM_SHORT_TIMEOUT_COUNTER)
				usleep_range(HWRM_SHORT_MIN_TIMEOUT,
					     HWRM_SHORT_MAX_TIMEOUT);
			else
				usleep_range(HWRM_MIN_TIMEOUT,
					     HWRM_MAX_TIMEOUT);
		}

		if (i >= tmo_count) {
			if (!(ctx->flags & BNXT_HWRM_CTX_SILENT))
				netdev_err(bp->dev, "Error (timeout: %d) msg {0x%x 0x%x} len:%d\n",
					   hwrm_total_timeout(i),
					   le16_to_cpu(ctx->req->req_type),
					   le16_to_cpu(ctx->req->seq_id), len);
			goto exit;
		}

		/* Last byte of resp contains valid bit */
		valid = ((u8 *)ctx->resp) + len - 1;
		for (j = 0; j < HWRM_VALID_BIT_DELAY_USEC; j++) {
			/* make sure we read from updated DMA memory */
			dma_rmb();
			if (*valid)
				break;
			usleep_range(1, 5);
		}

		if (j >= HWRM_VALID_BIT_DELAY_USEC) {
			if (!(ctx->flags & BNXT_HWRM_CTX_SILENT))
				netdev_err(bp->dev, "Error (timeout: %d) msg {0x%x 0x%x} len:%d v:%d\n",
					   hwrm_total_timeout(i),
					   le16_to_cpu(ctx->req->req_type),
					   le16_to_cpu(ctx->req->seq_id), len,
					   *valid);
			goto exit;
		}
	}

	/* Zero valid bit for compatibility.  Valid bit in an older spec
	 * may become a new field in a newer spec.  We must make sure that
	 * a new field not implemented by old spec will read zero.
	 */
	*valid = 0;
	rc = le16_to_cpu(ctx->resp->error_code);
	if (rc && !(ctx->flags & BNXT_HWRM_CTX_SILENT)) {
		if (rc == HWRM_ERR_CODE_BUSY)
			netdev_warn(bp->dev, "FW returned busy, hwrm req_type 0x%x\n",
				    le16_to_cpu(ctx->resp->req_type));
		else
			netdev_err(bp->dev, "hwrm req_type 0x%x seq id 0x%x error 0x%x\n",
				   le16_to_cpu(ctx->resp->req_type),
				   le16_to_cpu(ctx->resp->seq_id), rc);
	}
#ifndef HSI_DBG_DISABLE
	decode_hwrm_resp(ctx->resp);
#endif
	rc = __hwrm_to_stderr(rc);
exit:
	ptp_read_system_postts(ctx->sts);
	if (token)
		__hwrm_release_token(bp, token);
	if (ctx->flags & BNXT_HWRM_CTX_OWNED)
		ctx->flags |= BNXT_HWRM_RESP_DIRTY;
	else
		__hwrm_ctx_drop(bp, ctx);
	return rc;
}

int hwrm_req_send(struct bnxt *bp, void *req)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);

	if (!ctx)
		return -EINVAL;

	return __hwrm_send(bp, ctx);
}

int hwrm_req_send_silent(struct bnxt *bp, void *req)
{
	hwrm_req_silence(bp, req);
	return hwrm_req_send(bp, req);
}

void *
hwrm_req_dma_slice(struct bnxt *bp, void *req, u32 size, dma_addr_t *dma_handle)
{
	struct bnxt_hwrm_ctx *ctx = __hwrm_ctx(bp, req);
	u8 *end = ((u8 *)req) + BNXT_HWRM_DMA_SIZE;
	struct input *input = req;
	u8 *addr, *req_addr = req;
	u32 max_offset, offset;

	if (!ctx)
		return NULL;

	max_offset = BNXT_HWRM_DMA_SIZE - ctx->allocated;
	offset = max_offset - size;
	offset = ALIGN_DOWN(offset, BNXT_HWRM_DMA_ALIGN);
	addr = req_addr + offset;

	if (addr < req_addr + max_offset && req_addr + ctx->req_len <= addr) {
		ctx->allocated = end - addr;
		*dma_handle = ctx->dma_handle + offset;
		return addr;
	}

	/* could not suballocate from ctx buffer, try create a new mapping */
	if (ctx->slice_addr) {
		/* if one exists, can only be due to software bug, be loud */
		netdev_err(bp->dev, "HWRM refusing to reallocate DMA slice, req_type = %u\n",
			   (u32)le16_to_cpu(input->req_type));
		dump_stack();
		return NULL;
	}

	addr = dma_alloc_coherent(&bp->pdev->dev, size, dma_handle, ctx->gfp);

	if (!addr)
		return NULL;

	ctx->slice_addr = addr;
	ctx->slice_size = size;
	ctx->slice_handle = *dma_handle;

	return addr;
}
