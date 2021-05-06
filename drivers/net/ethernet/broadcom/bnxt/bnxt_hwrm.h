/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2020 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_HWRM_H
#define BNXT_HWRM_H

#include "bnxt_hsi.h"

enum bnxt_hwrm_ctx_flags {
	BNXT_HWRM_CTX_OWNED	= BIT(0), /* caller owns the context */
	BNXT_HWRM_CTX_SILENT	= BIT(1), /* squelch firmware errors */
	BNXT_HWRM_RESP_DIRTY	= BIT(2), /* response contains data */
};

struct bnxt_hwrm_ctx {
	u64 sentinel;
	dma_addr_t dma_handle;
	struct output *resp;
	struct input *req;
	dma_addr_t slice_handle;
	void *slice_addr;
	struct ptp_system_timestamp *sts;
	u32 slice_size;
	u32 req_len;
	enum bnxt_hwrm_ctx_flags flags;
	int timeout;
	u32 allocated;
	gfp_t gfp;
};

enum bnxt_hwrm_wait_state {
	BNXT_HWRM_PENDING,
	BNXT_HWRM_DEFERRED,
	BNXT_HWRM_COMPLETE,
	BNXT_HWRM_CANCELLED,
};

enum bnxt_hwrm_chnl { BNXT_HWRM_CHNL_CHIMP, BNXT_HWRM_CHNL_KONG };

struct bnxt_hwrm_wait_token {
	struct rcu_head rcu;
	struct hlist_node node;
	enum bnxt_hwrm_wait_state state;
	enum bnxt_hwrm_chnl dst;
	u16 seq_id;
};

void hwrm_update_token(struct bnxt *bp, u16 seq, enum bnxt_hwrm_wait_state s);

#define BNXT_HWRM_MAX_REQ_LEN		(bp->hwrm_max_req_len)
#define BNXT_HWRM_SHORT_REQ_LEN		sizeof(struct hwrm_short_input)
#define SHORT_HWRM_CMD_TIMEOUT		20
#define HWRM_CMD_TIMEOUT		(bp->hwrm_cmd_timeout)
#define HWRM_RESET_TIMEOUT		((HWRM_CMD_TIMEOUT) * 4)
#define HWRM_COREDUMP_TIMEOUT		((HWRM_CMD_TIMEOUT) * 12)
#define BNXT_HWRM_TARGET		0xffff
#define BNXT_HWRM_NO_CMPL_RING		-1
#define BNXT_HWRM_REQ_MAX_SIZE		128
#define BNXT_HWRM_DMA_SIZE		(2 * PAGE_SIZE) /* space for req+resp */
#define BNXT_HWRM_RESP_RESERVED		PAGE_SIZE
#define BNXT_HWRM_RESP_OFFSET		(BNXT_HWRM_DMA_SIZE -		\
					 BNXT_HWRM_RESP_RESERVED)
#define BNXT_HWRM_CTX_OFFSET		(BNXT_HWRM_RESP_OFFSET -	\
					 sizeof(struct bnxt_hwrm_ctx))
#define BNXT_HWRM_DMA_ALIGN		16
#define BNXT_HWRM_SENTINEL		0xb6e1f68a12e9a7eb /* arbitrary value */
#define BNXT_HWRM_REQS_PER_PAGE		(BNXT_PAGE_SIZE /	\
					 BNXT_HWRM_REQ_MAX_SIZE)
#define HWRM_SHORT_MIN_TIMEOUT		3
#define HWRM_SHORT_MAX_TIMEOUT		10
#define HWRM_SHORT_TIMEOUT_COUNTER	5

#define HWRM_MIN_TIMEOUT		25
#define HWRM_MAX_TIMEOUT		40

static inline int hwrm_total_timeout(int n)
{
	return n <= HWRM_SHORT_TIMEOUT_COUNTER ? n * HWRM_SHORT_MIN_TIMEOUT :
		HWRM_SHORT_TIMEOUT_COUNTER * HWRM_SHORT_MIN_TIMEOUT +
		(n - HWRM_SHORT_TIMEOUT_COUNTER) * HWRM_MIN_TIMEOUT;
}

#define HWRM_VALID_BIT_DELAY_USEC	150

static inline bool hwrm_req_type_cfa(u16 req_type)
{
	switch (req_type) {
	case HWRM_CFA_ENCAP_RECORD_ALLOC:
	case HWRM_CFA_ENCAP_RECORD_FREE:
	case HWRM_CFA_DECAP_FILTER_ALLOC:
	case HWRM_CFA_DECAP_FILTER_FREE:
	case HWRM_CFA_EM_FLOW_ALLOC:
	case HWRM_CFA_EM_FLOW_FREE:
	case HWRM_CFA_EM_FLOW_CFG:
	case HWRM_CFA_FLOW_ALLOC:
	case HWRM_CFA_FLOW_FREE:
	case HWRM_CFA_FLOW_INFO:
	case HWRM_CFA_FLOW_FLUSH:
	case HWRM_CFA_FLOW_STATS:
	case HWRM_CFA_METER_PROFILE_ALLOC:
	case HWRM_CFA_METER_PROFILE_FREE:
	case HWRM_CFA_METER_PROFILE_CFG:
	case HWRM_CFA_METER_INSTANCE_ALLOC:
	case HWRM_CFA_METER_INSTANCE_FREE:
	case HWRM_CFA_EEM_QCAPS:
	case HWRM_CFA_EEM_OP:
	case HWRM_CFA_EEM_CFG:
	case HWRM_CFA_EEM_QCFG:
	case HWRM_CFA_CTX_MEM_RGTR:
	case HWRM_CFA_CTX_MEM_UNRGTR:
		return true;
	default:
		return false;
	}
}

static inline bool hwrm_req_kong(struct bnxt *bp, struct input *req)
{
	return (bp->fw_cap & BNXT_FW_CAP_KONG_MB_CHNL &&
		(hwrm_req_type_cfa(le16_to_cpu(req->req_type)) ||
		 le16_to_cpu(req->target_id) == HWRM_TARGET_ID_KONG));
}

/**
 * __hwrm_req_init() - Initialize an HWRM request.
 * @bp: The driver context.
 * @req: A pointer to the request pointer to initialize.
 * @req_type: The request type. This will be converted to the little endian
 *	before being written to the req_type field of the returned request.
 * @req_len: The length of the request to be allocated.
 *
 * Allocate DMA resources and initialize a new HWRM request object of the
 * given type. The response address field in the request is configured with
 * the DMA bus address that has been mapped for the response and the passed
 * request is pointed to kernel virtual memory mapped for the request (such
 * that short_input indirection can be accomplished without copying). The
 * request’s target and completion ring are initialized to default values and
 * can be overridden by writing to the returned request object directly.
 *
 * The initialized request can be further customized by writing to its fields
 * directly, taking care to covert such fields to little endian. The request
 * object will be consumed (and all its associated resources release) upon
 * passing it to hwrm_req_send() unless ownership of the request has been
 * claimed by the caller via a call to hwrm_req_hold(). If the request is not
 * consumed, either because it is never sent or because ownership has been
 * claimed, then it must be released by a call to hwrm_req_drop().
 *
 * Return: zero on success, negative error code otherwise:
 *	E2BIG: the type of request pointer is too large to fit.
 *	ENOMEM: an allocation failure occurred.
 */
int __hwrm_req_init(struct bnxt *bp, void **req, u16 req_type, u32 req_len);
#define hwrm_req_init(bp, req, req_type) \
	__hwrm_req_init((bp), (void **)&(req), (req_type), sizeof(*(req)))

/**
 * hwrm_req_hold() - Claim ownership of the request's resources.
 * @bp: The driver context.
 * @req: A pointer to the request to own. The request will no longer be
 *	consumed by calls to hwrm_req_send().
 *
 * Take ownership of the request. Ownership places responsibility on the
 * caller to free the resources associated with the request via a call to
 * hwrm_req_drop(). The caller taking ownership implies that a subsequent
 * call to hwrm_req_send() will not consume the request (ie. sending will
 * not free the associated resources if the request is owned by the caller).
 * Taking ownership returns a reference to the response. Retaining and
 * accessing the response data is the most common reason to take ownership
 * of the request. Ownership can also be acquired in order to reuse the same
 * request object across multiple invocations of hwrm_req_send().
 *
 * Return: A pointer to the response object.
 *
 * The resources associated with the response will remain available to the
 * caller until ownership of the request is relinquished via a call to
 * hwrm_req_drop(). It is not possible for hwrm_req_hold() to return NULL if
 * a valid request is provided. A returned NULL value would imply a driver
 * bug and the implementation will complain loudly in the logs to aid in
 * detection. It should not be necessary to check the result for NULL.
 */
void *hwrm_req_hold(struct bnxt *bp, void *req);

/**
 * hwrm_req_drop() - Release all resources associated with the request.
 * @bp: The driver context.
 * @req: The request to consume, releasing the associated resources. The
 *	request object, any slices, and its associated response are no
 *	longer valid.
 *
 * It is legal to call hwrm_req_drop() on an unowned request, provided it
 * has not already been consumed by hwrm_req_send() (for example, to release
 * an aborted request). A given request should not be dropped more than once,
 * nor should it be dropped after having been consumed by hwrm_req_send(). To
 * do so is an error (the context will not be found and a stack trace will be
 * rendered in the kernel log).
 */
void hwrm_req_drop(struct bnxt *bp, void *req);

/**
 * hwrm_req_silence() - Do not log HWRM errors.
 * @bp: The driver context.
 * @req: The request to silence.
 *
 * Do not print HWRM errors to the kernel log for the associated request
 * during a subsequent call to hwrm_req_send(). Some requests are expected
 * to fail in certain scenarios and, as such, errors should be suppressed.
 */
void hwrm_req_silence(struct bnxt *bp, void *req);

/**
 * hwrm_req_timeout() - Set the completion timeout for the request.
 * @bp: The driver context.
 * @req: The request to set the timeout.
 * @timeout: The timeout in milliseconds.
 *
 * Set the timeout associated with the request for subsequent calls to
 * hwrm_req_send(). Some requests are long running and require a different
 * timeout than the default.
 */
void hwrm_req_timeout(struct bnxt *bp, void *req, int timeout);

/**
 * hwrm_req_send() - Execute an HWRM command.
 * @bp: The driver context.
 * @req: A pointer to the request to send. The DMA resources associated with
 *	the request will be released (ie. the request will be consumed) unless
 *	ownership of the request has been assumed by the caller via a call to
 *	hwrm_req_hold().
 *
 * Send an HWRM request to the device and wait for a response. The request is
 * consumed if it is not owned by the caller. This function will block until
 * the request has either completed or times out due to an error.
 *
 * Return: A result code.
 *
 * The result is zero on success, otherwise the negative error code indicates
 * one of the following errors:
 *	E2BIG: The request was too large.
 *	EBUSY: The firmware is in a fatal state or the request timed out
 *	EACCESS: HWRM access denied.
 *	ENOSPC: HWRM resource allocation error.
 *	EINVAL: Request parameters are invalid.
 *	ENOMEM: HWRM has no buffers.
 *	EAGAIN: HWRM busy or reset in progress.
 *	EOPNOTSUPP: Invalid request type.
 *	EIO: Any other error.
 * Error handling is orthogonal to request ownership. An unowned request will
 * still be consumed on error. If the caller owns the request, then the caller
 * is responsible for releasing the resources. Otherwise, hwrm_req_send() will
 * always consume the request.
 */
int hwrm_req_send(struct bnxt *bp, void *req);

/**
 * hwrm_req_send_silent() - A silent version of hwrm_req_send().
 * @bp: The driver context.
 * @req: The request to send without logging.
 *
 * The same as hwrm_req_send(), except that the request is silenced using
 * hwrm_req_silence() prior the call. This version of the function is
 * provided solely to preserve the legacy API’s flavor for this functionality.
 *
 * Return: A result code, see hwrm_req_send().
 */
int hwrm_req_send_silent(struct bnxt *bp, void *req);

/**
 * hwrm_req_replace() - Replace request data.
 * @bp: The driver context.
 * @req: The request to modify. A call to hwrm_req_replace() is conceptually
 *	an assignment of new_req to req. Subsequent calls to HWRM API functions,
 *	such as hwrm_req_send(), should thus use req and not new_req (in fact,
 *	calls to HWRM API functions will fail if non-managed request objects
 *	are passed).
 * @len: The length of new_req.
 * @new_req: The pre-built request to copy or reference.
 *
 * Replaces the request data in req with that of new_req. This is useful in
 * scenarios where a request object has already been constructed by a third
 * party prior to creating a resource managed request using hwrm_req_init().
 * Depending on the length, hwrm_req_replace() will either copy the new
 * request data into the DMA memory allocated for req, or it will simply
 * reference the new request and use it in lieu of req during subsequent
 * calls to hwrm_req_send(). The resource management is associated with
 * req and is independent of and does not apply to new_req. The caller must
 * ensure that the lifetime of new_req is least as long as req. Any slices
 * that may have been associated with the original request are released.
 *
 * Return: zero on success, negative error code otherwise:
 *     E2BIG: Request is too large.
 *     EINVAL: Invalid request to modify.
 */
int hwrm_req_replace(struct bnxt *bp, void *req, void *new_req, u32 len);

/**
 * hwrm_req_alloc_flags() - Sets GFP allocation flags for slices.
 * @bp: The driver context.
 * @req: The request for which calls to hwrm_req_dma_slice() will have altered
 *	allocation flags.
 * @flags: A bitmask of GFP flags. These flags are passed to
 *	dma_alloc_coherent() whenever it is used to allocate backing memory
 *	for slices. Note that calls to hwrm_req_dma_slice() will not always
 *	result in new allocations, however, memory suballocated from the
 *	request buffer is already __GFP_ZERO.
 *
 * Sets the GFP allocation flags associated with the request for subsequent
 * calls to hwrm_req_dma_slice(). This can be useful for specifying __GFP_ZERO
 * for slice allocations.
 */
void hwrm_req_alloc_flags(struct bnxt *bp, void *req, gfp_t flags);

/**
 * hwrm_req_dma_slice() - Allocate a slice of DMA mapped memory.
 * @bp: The driver context.
 * @req: The request for which indirect data will be associated.
 * @size: The size of the allocation.
 * @dma: The bus address associated with the allocation. The HWRM API has no
 *	knowledge about the type of the request and so cannot infer how the
 *	caller intends to use the indirect data. Thus, the caller is
 *	responsible for configuring the request object appropriately to
 *	point to the associated indirect memory. Note, DMA handle has the
 *	same definition as it does in dma_alloc_coherent(), the caller is
 *	responsible for endian conversions via cpu_to_le64() before assigning
 *	this address.
 *
 * Allocates DMA mapped memory for indirect data related to a request. The
 * lifetime of the DMA resources will be bound to that of the request (ie.
 * they will be automatically released when the request is either consumed by
 * hwrm_req_send() or dropped by hwrm_req_drop()). Small allocations are
 * efficiently suballocated out of the request buffer space, hence the name
 * slice, while larger requests are satisfied via an underlying call to
 * dma_alloc_coherent(). Multiple suballocations are supported, however, only
 * one externally mapped region is.
 *
 * Return: The kernel virtual address of the DMA mapping.
 */
void *hwrm_req_dma_slice(struct bnxt *bp, void *req, u32 size, dma_addr_t *dma);

/**
 * hwrm_req_capture_ts() - Get system time into PTP provided time structure
 * @bp: The driver context.
 * @req: The request to query free running timer of the NIC
 * @sts: System provided ptp_system_timestamp structure
 *
 * PTP's gettimex64() optionally provides a system timestamp structure that
 * driver should update. This function sets the sts member of bnxt_hwrm_ctx
 * associated with the request. The __hwrm_send() will eventually update the
 * pre and post system time in this structure when executing the gettimex64()
 */
void hwrm_req_capture_ts(struct bnxt *bp, void *req, struct ptp_system_timestamp *sts);
#endif
