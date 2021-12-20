/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2020 Broadcom. All Rights Reserved. The term *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.     *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
 * Portions Copyright (C) 2004-2005 Christoph Hellwig              *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 ********************************************************************/

#if !defined(BUILD_NVME)
#define	FC_TYPE_NVME			0x28
#endif

#define LPFC_NVME_MIN_SEGS		16
#define LPFC_NVME_DEFAULT_SEGS		66	/* 256K IOs - 64 + 2 */
#define LPFC_NVME_MAX_SEGS		510
#define LPFC_NVMET_MIN_POSTBUF		16
#define LPFC_NVMET_DEFAULT_POSTBUF	1024
#define LPFC_NVMET_MAX_POSTBUF		4096

#define LPFC_NVME_ERSP_LEN		0x20

#define LPFC_NVME_WAIT_TMO              10
#define LPFC_NVME_EXPEDITE_XRICNT	8
#define LPFC_NVME_FB_SHIFT		9
#define LPFC_NVME_MAX_FB		(1 << 20)	/* 1M */

#define lpfc_ndlp_get_nrport(ndlp)					\
	((!ndlp->nrport || (ndlp->upcall_flags & NLP_WAIT_FOR_UNREG))	\
	? NULL : ndlp->nrport)

struct lpfc_nvme_qhandle {
	struct list_head list;	/* list entry to maintain qhandle list */
	uint32_t index;		/* WQ index to use */
	uint32_t qidx;		/* queue index passed to create */
	uint32_t cpu_id;	/* current cpu id at time of create */
};

/* Declare nvme-based local and remote port definitions. */
struct lpfc_nvme_lport {
	struct lpfc_vport *vport;
	struct completion *lport_unreg_cmp;
	/* Add stats counters here */
	atomic_t fc4NvmeLsRequests;
	atomic_t fc4NvmeLsCmpls;
	atomic_t xmt_fcp_noxri;
	atomic_t xmt_fcp_bad_ndlp;
	atomic_t xmt_fcp_qdepth;
	atomic_t xmt_fcp_wqerr;
	atomic_t xmt_fcp_err;
	atomic_t xmt_fcp_abort;
	atomic_t xmt_ls_abort;
	atomic_t xmt_ls_err;
	atomic_t cmpl_fcp_xb;
	atomic_t cmpl_fcp_err;
	atomic_t cmpl_ls_xb;
	atomic_t cmpl_ls_err;
	/* Maintaining qhandle list */
	struct list_head qhandle_list;
	spinlock_t qhandle_list_lock;
};

struct lpfc_nvme_rport {
	struct lpfc_nvme_lport *lport;
	struct nvme_fc_remote_port *remoteport;
	struct lpfc_nodelist *ndlp;
	struct completion rport_unreg_done;
};

struct lpfc_nvme_fcpreq_priv {
	struct lpfc_io_buf *nvme_buf;

	/* Used for pending IO */
	struct list_head nvme_pend_list;
	struct list_head nvme_abts_pend_list;
	struct nvme_fc_local_port *pnvme_lport;
	struct nvme_fc_remote_port *pnvme_rport;
	void *hw_queue_handle;
	struct nvmefc_fcp_req *pnvme_fcreq;
	uint32_t status;
};
