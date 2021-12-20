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
 *******************************************************************/
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <asm/unaligned.h>
#include <linux/crc-t10dif.h>
#include <net/checksum.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_version.h"
#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_scsi.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"

#define LPFC_RESET_WAIT  2
#define LPFC_ABORT_WAIT  2

static char *dif_op_str[] = {
	"PROT_NORMAL",
	"PROT_READ_INSERT",
	"PROT_WRITE_STRIP",
	"PROT_READ_STRIP",
	"PROT_WRITE_INSERT",
	"PROT_READ_PASS",
	"PROT_WRITE_PASS",
};

struct scsi_dif_tuple {
	__be16 guard_tag;       /* Checksum */
	__be16 app_tag;         /* Opaque storage */
	__be32 ref_tag;         /* Target LBA or indirect LBA */
};

static struct lpfc_rport_data *
lpfc_rport_data_from_scsi_device(struct scsi_device *sdev)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)sdev->host->hostdata;

	if (vport->phba->cfg_fof)
		return ((struct lpfc_device_data *)sdev->hostdata)->rport_data;
	else
		return (struct lpfc_rport_data *)sdev->hostdata;
}

static struct lpfc_external_dif_support *
lpfc_external_dif_match(struct lpfc_vport *vport, uint64_t lun, uint32_t sid);

static int
lpfc_external_dif(struct lpfc_vport *vport, struct lpfc_io_buf *lpfc_cmd,
		  struct lpfc_nodelist *ndlp, uint8_t *cdb_ptr);
static void
lpfc_external_dif_cmpl(struct lpfc_hba *phba, struct lpfc_iocbq *pIocbIn,
		       struct lpfc_iocbq *pIocbOut);
extern int
lpfc_issue_scsi_inquiry(struct lpfc_vport *,
			struct lpfc_nodelist *, uint32_t);
static void
lpfc_release_scsi_buf_s4(struct lpfc_hba *phba, struct lpfc_io_buf *psb);
static void
lpfc_release_scsi_buf_s3(struct lpfc_hba *phba, struct lpfc_io_buf *psb);
static int
lpfc_prot_group_type(struct lpfc_hba *phba, struct scsi_cmnd *sc);

#ifdef BUILD_ASMIO
static inline unsigned
lpfc_cmd_blksize(struct scsi_cmnd *sc)
{
	return scsi_prot_interval(sc);
}

#define LPFC_CHECK_PROTECT_GUARD	1
#define LPFC_CHECK_PROTECT_REF		2
static inline unsigned
lpfc_cmd_protect(struct scsi_cmnd *sc, int flag)
{
	if (flag == LPFC_CHECK_PROTECT_GUARD)
		return (scsi_prot_flagged(sc, SCSI_PROT_GUARD_CHECK));
	if (flag == LPFC_CHECK_PROTECT_REF)
		return (scsi_prot_flagged(sc, SCSI_PROT_REF_CHECK));
	return 0;
}

static inline unsigned
lpfc_cmd_guard_csum(struct scsi_cmnd *sc)
{
	return (scsi_prot_flagged(sc, SCSI_PROT_IP_CHECKSUM));
}

#else

static inline unsigned
lpfc_cmd_blksize(struct scsi_cmnd *sc)
{
	return sc->device->sector_size;
}

#define LPFC_CHECK_PROTECT_GUARD	1
#define LPFC_CHECK_PROTECT_REF		2
static inline unsigned
lpfc_cmd_protect(struct scsi_cmnd *sc, int flag)
{
	return 1;
}

static inline unsigned
lpfc_cmd_guard_csum(struct scsi_cmnd *sc)
{
	if (lpfc_prot_group_type(NULL, sc) == LPFC_PG_TYPE_NO_DIF)
		return 0;
	if (scsi_host_get_guard(sc->device->host) == SHOST_DIX_GUARD_IP)
		return 1;
	return 0;
}
#endif

/**
 * lpfc_sli4_set_rsp_sgl_last - Set the last bit in the response sge.
 * @phba: Pointer to HBA object.
 * @lpfc_cmd: lpfc scsi command object pointer.
 *
 * This function is called from the lpfc_prep_task_mgmt_cmd function to
 * set the last bit in the response sge entry.
 **/
static void
lpfc_sli4_set_rsp_sgl_last(struct lpfc_hba *phba,
				struct lpfc_io_buf *lpfc_cmd)
{
	struct sli4_sge *sgl = (struct sli4_sge *)lpfc_cmd->dma_sgl;
	if (sgl) {
		sgl += 1;
		sgl->word2 = le32_to_cpu(sgl->word2);
		bf_set(lpfc_sli4_sge_last, sgl, 1);
		sgl->word2 = cpu_to_le32(sgl->word2);
	}
}

/**
 * lpfc_update_stats - Update statistical data for the command completion
 * @vport: The virtual port on which this call is executing.
 * @lpfc_cmd: lpfc scsi command object pointer.
 *
 * This function is called when there is a command completion and this
 * function updates the statistical data for the command completion.
 **/
static void
lpfc_update_stats(struct lpfc_vport *vport, struct lpfc_io_buf *lpfc_cmd)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *pnode;
	struct scsi_cmnd *cmd = lpfc_cmd->pCmd;
	unsigned long flags;
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	unsigned long latency;
	int i;

	if (!vport->stat_data_enabled ||
	    vport->stat_data_blocked ||
	    (cmd->result))
		return;

	latency = jiffies_to_msecs((long)jiffies - (long)lpfc_cmd->start_time);
	rdata = lpfc_cmd->rdata;
	pnode = rdata->pnode;

	spin_lock_irqsave(shost->host_lock, flags);
	if (!pnode ||
	    !pnode->lat_data ||
	    (phba->bucket_type == LPFC_NO_BUCKET)) {
		spin_unlock_irqrestore(shost->host_lock, flags);
		return;
	}

	if (phba->bucket_type == LPFC_LINEAR_BUCKET) {
		i = (latency + phba->bucket_step - 1 - phba->bucket_base)/
			phba->bucket_step;
		/* check array subscript bounds */
		if (i < 0)
			i = 0;
		else if (i >= LPFC_MAX_BUCKET_COUNT)
			i = LPFC_MAX_BUCKET_COUNT - 1;
	} else {
		for (i = 0; i < LPFC_MAX_BUCKET_COUNT-1; i++)
			if (latency <= (phba->bucket_base +
				((1<<i)*phba->bucket_step)))
				break;
	}

	pnode->lat_data[i].cmd_count++;
	spin_unlock_irqrestore(shost->host_lock, flags);
}


/**
 * lpfc_rampdown_queue_depth - Post RAMP_DOWN_QUEUE event to worker thread
 * @phba: The Hba for which this call is being executed.
 *
 * This routine is called when there is resource error in driver or firmware.
 * This routine posts WORKER_RAMP_DOWN_QUEUE event for @phba. This routine
 * posts at most 1 event each second. This routine wakes up worker thread of
 * @phba to process WORKER_RAM_DOWN_EVENT event.
 *
 * This routine should be called with no lock held.
 **/
void
lpfc_rampdown_queue_depth(struct lpfc_hba *phba)
{
	unsigned long flags;
	uint32_t evt_posted;
	unsigned long expires;

	spin_lock_irqsave(&phba->hbalock, flags);
	atomic_inc(&phba->num_rsrc_err);
	phba->last_rsrc_error_time = jiffies;

	expires = phba->last_ramp_down_time + QUEUE_RAMP_DOWN_INTERVAL;
	if (time_after(expires, jiffies)) {
		spin_unlock_irqrestore(&phba->hbalock, flags);
		return;
	}

	phba->last_ramp_down_time = jiffies;

	spin_unlock_irqrestore(&phba->hbalock, flags);

	spin_lock_irqsave(&phba->pport->work_port_lock, flags);
	evt_posted = phba->pport->work_port_events & WORKER_RAMP_DOWN_QUEUE;
	if (!evt_posted)
		phba->pport->work_port_events |= WORKER_RAMP_DOWN_QUEUE;
	spin_unlock_irqrestore(&phba->pport->work_port_lock, flags);

	if (!evt_posted)
		lpfc_worker_wake_up(phba);
	return;
}

/**
 * lpfc_ramp_down_queue_handler - WORKER_RAMP_DOWN_QUEUE event handler
 * @phba: The Hba for which this call is being executed.
 *
 * This routine is called to  process WORKER_RAMP_DOWN_QUEUE event for worker
 * thread.This routine reduces queue depth for all scsi device on each vport
 * associated with @phba.
 **/
void
lpfc_ramp_down_queue_handler(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	struct Scsi_Host  *shost;
	struct scsi_device *sdev;
	unsigned long new_queue_depth;
	unsigned long num_rsrc_err, num_cmd_success;
	int i;

	num_rsrc_err = atomic_read(&phba->num_rsrc_err);
	num_cmd_success = atomic_read(&phba->num_cmd_success);

	/*
	 * The error and success command counters are global per
	 * driver instance.  If another handler has already
	 * operated on this error event, just exit.
	 */
	if (num_rsrc_err == 0)
		return;

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			shost = lpfc_shost_from_vport(vports[i]);
			shost_for_each_device(sdev, shost) {
				new_queue_depth =
					sdev->queue_depth * num_rsrc_err /
					(num_rsrc_err + num_cmd_success);
				if (!new_queue_depth)
					new_queue_depth = sdev->queue_depth - 1;
				else
					new_queue_depth = sdev->queue_depth -
								new_queue_depth;
				scsi_change_queue_depth(sdev, new_queue_depth);
			}
		}
	lpfc_destroy_vport_work_array(phba, vports);
	atomic_set(&phba->num_rsrc_err, 0);
	atomic_set(&phba->num_cmd_success, 0);
}

/**
 * lpfc_scsi_dev_block - set all scsi hosts to block state
 * @phba: Pointer to HBA context object.
 *
 * This function walks vport list and set each SCSI host to block state
 * by invoking fc_remote_port_delete() routine. This function is invoked
 * with EEH when device's PCI slot has been permanently disabled.
 **/
void
lpfc_scsi_dev_block(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	struct lpfc_vport *vport;
	struct Scsi_Host  *shost;
	struct scsi_device *sdev;
	struct fc_rport *rport;
	struct lpfc_external_dif_support *dp;
	unsigned long flags;
	int i;

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			vport = vports[i];
			shost = lpfc_shost_from_vport(vport);
			shost_for_each_device(sdev, shost) {
				rport = starget_to_rport(scsi_target(sdev));
				fc_remote_port_delete(rport);
			}

			/*
			 * Cleanup all External DIF paths on this vport. They
			 * will need to be revalidated if the NPort comes back.
			 */
			spin_lock_irqsave(&vport->external_dif_lock, flags);
			list_for_each_entry(dp, &vport->external_dif_list,
					    listentry) {
				lpfc_edsm_set_state(vport, dp,
						    LPFC_EDSM_NEEDS_INQUIRY);
			}
			spin_unlock_irqrestore(&vport->external_dif_lock,
					       flags);

		}
	lpfc_destroy_vport_work_array(phba, vports);

}

/**
 * lpfc_new_scsi_buf_s3 - Scsi buffer allocator for HBA with SLI3 IF spec
 * @vport: The virtual port for which this call being executed.
 * @num_to_allocate: The requested number of buffers to allocate.
 *
 * This routine allocates a scsi buffer for device with SLI-3 interface spec,
 * the scsi buffer contains all the necessary information needed to initiate
 * a SCSI I/O. The non-DMAable buffer region contains information to build
 * the IOCB. The DMAable region contains memory for the FCP CMND, FCP RSP,
 * and the initial BPL. In addition to allocating memory, the FCP CMND and
 * FCP RSP BDEs are setup in the BPL and the BPL BDE is setup in the IOCB.
 *
 * Return codes:
 *   int - number of scsi buffers that were allocated.
 *   0 = failure, less than num_to_alloc is a partial failure.
 **/
static int
lpfc_new_scsi_buf_s3(struct lpfc_vport *vport, int num_to_alloc)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_io_buf *psb;
	struct ulp_bde64 *bpl;
	IOCB_t *iocb;
	dma_addr_t pdma_phys_fcp_cmd;
	dma_addr_t pdma_phys_fcp_rsp;
	dma_addr_t pdma_phys_sgl;
	uint16_t iotag;
	int bcnt, bpl_size;

	bpl_size = phba->cfg_sg_dma_buf_size -
		(sizeof(struct fcp_cmnd) + sizeof(struct fcp_rsp));

	lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
			 "9067 ALLOC %d scsi_bufs: %d (%d + %d + %d)\n",
			 num_to_alloc, phba->cfg_sg_dma_buf_size,
			 (int)sizeof(struct fcp_cmnd),
			 (int)sizeof(struct fcp_rsp), bpl_size);

	for (bcnt = 0; bcnt < num_to_alloc; bcnt++) {
		psb = kzalloc(sizeof(struct lpfc_io_buf), GFP_KERNEL);
		if (!psb)
			break;

		/*
		 * Get memory from the pci pool to map the virt space to pci
		 * bus space for an I/O.  The DMA buffer includes space for the
		 * struct fcp_cmnd, struct fcp_rsp and the number of bde's
		 * necessary to support the sg_tablesize.
		 */
		psb->data = dma_pool_zalloc(phba->lpfc_sg_dma_buf_pool,
					GFP_KERNEL, &psb->dma_handle);
		if (!psb->data) {
			kfree(psb);
			break;
		}

		/* Allocate iotag for psb->cur_iocbq. */
		iotag = lpfc_sli_next_iotag(phba, &psb->cur_iocbq);
		if (iotag == 0) {
			dma_pool_free(phba->lpfc_sg_dma_buf_pool,
				      psb->data, psb->dma_handle);
			kfree(psb);
			break;
		}
		psb->cur_iocbq.iocb_flag |= LPFC_IO_FCP;

		psb->fcp_cmnd = psb->data;
		psb->fcp_rsp = psb->data + sizeof(struct fcp_cmnd);
		psb->dma_sgl = psb->data + sizeof(struct fcp_cmnd) +
			sizeof(struct fcp_rsp);

		/* Initialize local short-hand pointers. */
		bpl = (struct ulp_bde64 *)psb->dma_sgl;
		pdma_phys_fcp_cmd = psb->dma_handle;
		pdma_phys_fcp_rsp = psb->dma_handle + sizeof(struct fcp_cmnd);
		pdma_phys_sgl = psb->dma_handle + sizeof(struct fcp_cmnd) +
			sizeof(struct fcp_rsp);

		/*
		 * The first two bdes are the FCP_CMD and FCP_RSP. The balance
		 * are sg list bdes.  Initialize the first two and leave the
		 * rest for queuecommand.
		 */
		bpl[0].addrHigh = le32_to_cpu(putPaddrHigh(pdma_phys_fcp_cmd));
		bpl[0].addrLow = le32_to_cpu(putPaddrLow(pdma_phys_fcp_cmd));
		bpl[0].tus.f.bdeSize = sizeof(struct fcp_cmnd);
		bpl[0].tus.f.bdeFlags = BUFF_TYPE_BDE_64;
		bpl[0].tus.w = le32_to_cpu(bpl[0].tus.w);

		/* Setup the physical region for the FCP RSP */
		bpl[1].addrHigh = le32_to_cpu(putPaddrHigh(pdma_phys_fcp_rsp));
		bpl[1].addrLow = le32_to_cpu(putPaddrLow(pdma_phys_fcp_rsp));
		bpl[1].tus.f.bdeSize = sizeof(struct fcp_rsp);
		bpl[1].tus.f.bdeFlags = BUFF_TYPE_BDE_64;
		bpl[1].tus.w = le32_to_cpu(bpl[1].tus.w);

		/*
		 * Since the IOCB for the FCP I/O is built into this
		 * lpfc_scsi_buf, initialize it with all known data now.
		 */
		iocb = &psb->cur_iocbq.iocb;
		iocb->un.fcpi64.bdl.ulpIoTag32 = 0;
		if ((phba->sli_rev == 3) &&
				!(phba->sli3_options & LPFC_SLI3_BG_ENABLED)) {
			/* fill in immediate fcp command BDE */
			iocb->un.fcpi64.bdl.bdeFlags = BUFF_TYPE_BDE_IMMED;
			iocb->un.fcpi64.bdl.bdeSize = sizeof(struct fcp_cmnd);
			iocb->un.fcpi64.bdl.addrLow = offsetof(IOCB_t,
					unsli3.fcp_ext.icd);
			iocb->un.fcpi64.bdl.addrHigh = 0;
			iocb->ulpBdeCount = 0;
			iocb->ulpLe = 0;
			/* fill in response BDE */
			iocb->unsli3.fcp_ext.rbde.tus.f.bdeFlags =
							BUFF_TYPE_BDE_64;
			iocb->unsli3.fcp_ext.rbde.tus.f.bdeSize =
				sizeof(struct fcp_rsp);
			iocb->unsli3.fcp_ext.rbde.addrLow =
				putPaddrLow(pdma_phys_fcp_rsp);
			iocb->unsli3.fcp_ext.rbde.addrHigh =
				putPaddrHigh(pdma_phys_fcp_rsp);
		} else {
			iocb->un.fcpi64.bdl.bdeFlags = BUFF_TYPE_BLP_64;
			iocb->un.fcpi64.bdl.bdeSize =
					(2 * sizeof(struct ulp_bde64));
			iocb->un.fcpi64.bdl.addrLow =
					putPaddrLow(pdma_phys_sgl);
			iocb->un.fcpi64.bdl.addrHigh =
					putPaddrHigh(pdma_phys_sgl);
			iocb->ulpBdeCount = 1;
			iocb->ulpLe = 1;
		}
		iocb->ulpClass = CLASS3;
		psb->status = IOSTAT_SUCCESS;
		/* Put it back into the SCSI buffer list */
		psb->cur_iocbq.context1  = psb;
		spin_lock_init(&psb->buf_lock);
		lpfc_release_scsi_buf_s3(phba, psb);

	}

	return bcnt;
}

/**
 * lpfc_sli4_vport_delete_fcp_xri_aborted -Remove all ndlp references for vport
 * @vport: pointer to lpfc vport data structure.
 *
 * This routine is invoked by the vport cleanup for deletions and the cleanup
 * for an ndlp on removal.
 **/
void
lpfc_sli4_vport_delete_fcp_xri_aborted(struct lpfc_vport *vport)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_io_buf *psb, *next_psb;
	struct lpfc_sli4_hdw_queue *qp;
	unsigned long iflag = 0;
	int idx;

	if (!(vport->cfg_enable_fc4_type & LPFC_ENABLE_FCP))
		return;

	/* may be called before queues established if hba_setup fails */
	if (!phba->sli4_hba.hdwq)
		return;

	spin_lock_irqsave(&phba->hbalock, iflag);
	for (idx = 0; idx < phba->cfg_hdw_queue; idx++) {
		qp = &phba->sli4_hba.hdwq[idx];

		spin_lock(&qp->abts_io_buf_list_lock);
		list_for_each_entry_safe(psb, next_psb,
					 &qp->lpfc_abts_io_buf_list, list) {
			if (psb->cur_iocbq.iocb_flag & LPFC_IO_NVME)
				continue;

			if (psb->rdata && psb->rdata->pnode &&
			    psb->rdata->pnode->vport == vport)
				psb->rdata = NULL;
		}
		spin_unlock(&qp->abts_io_buf_list_lock);
	}
	spin_unlock_irqrestore(&phba->hbalock, iflag);
}

/**
 * lpfc_sli4_io_xri_aborted - Fast-path process of fcp xri abort
 * @phba: pointer to lpfc hba data structure.
 * @axri: pointer to the fcp xri abort wcqe structure.
 *
 * This routine is invoked by the worker thread to process a SLI4 fast-path
 * FCP or NVME aborted xri.
 **/
void
lpfc_sli4_io_xri_aborted(struct lpfc_hba *phba,
			 struct sli4_wcqe_xri_aborted *axri, int idx)
{
	uint16_t xri = bf_get(lpfc_wcqe_xa_xri, axri);
	uint16_t rxid = bf_get(lpfc_wcqe_xa_remote_xid, axri);
	struct lpfc_io_buf *psb, *next_psb;
	struct lpfc_sli4_hdw_queue *qp;
	unsigned long iflag = 0;
	struct lpfc_iocbq *iocbq;
	int i;
	struct lpfc_nodelist *ndlp;
	int rrq_empty = 0;
	struct lpfc_sli_ring *pring = phba->sli4_hba.els_wq->pring;

	if (!(phba->cfg_enable_fc4_type & LPFC_ENABLE_FCP))
		return;

	qp = &phba->sli4_hba.hdwq[idx];
	spin_lock_irqsave(&phba->hbalock, iflag);
	spin_lock(&qp->abts_io_buf_list_lock);
	list_for_each_entry_safe(psb, next_psb,
		&qp->lpfc_abts_io_buf_list, list) {
		if (psb->cur_iocbq.sli4_xritag == xri) {
			list_del_init(&psb->list);
			psb->flags &= ~LPFC_SBUF_XBUSY;
			psb->status = IOSTAT_SUCCESS;
#if (IS_ENABLED(CONFIG_NVME_FC))
			if (psb->cur_iocbq.iocb_flag & LPFC_IO_NVME) {
				qp->abts_nvme_io_bufs--;
				spin_unlock(&qp->abts_io_buf_list_lock);
				spin_unlock_irqrestore(&phba->hbalock, iflag);
				lpfc_sli4_nvme_xri_aborted(phba, axri, psb);
				return;
			}
#endif
			qp->abts_scsi_io_bufs--;
			spin_unlock(&qp->abts_io_buf_list_lock);

			if (psb->rdata && psb->rdata->pnode)
				ndlp = psb->rdata->pnode;
			else
				ndlp = NULL;

			rrq_empty = list_empty(&phba->active_rrq_list);
			spin_unlock_irqrestore(&phba->hbalock, iflag);
			if (ndlp) {
				lpfc_set_rrq_active(phba, ndlp,
					psb->cur_iocbq.sli4_lxritag, rxid, 1);
				lpfc_sli4_abts_err_handler(phba, ndlp, axri);
			}
			lpfc_release_scsi_buf_s4(phba, psb);
			if (rrq_empty)
				lpfc_worker_wake_up(phba);
			return;
		}
	}
	spin_unlock(&qp->abts_io_buf_list_lock);
	for (i = 1; i <= phba->sli.last_iotag; i++) {
		iocbq = phba->sli.iocbq_lookup[i];

		if (!(iocbq->iocb_flag & LPFC_IO_FCP) ||
		    (iocbq->iocb_flag & LPFC_IO_LIBDFC))
			continue;
		if (iocbq->sli4_xritag != xri)
			continue;
		psb = container_of(iocbq, struct lpfc_io_buf, cur_iocbq);
		psb->flags &= ~LPFC_SBUF_XBUSY;
		spin_unlock_irqrestore(&phba->hbalock, iflag);
		if (!list_empty(&pring->txq))
			lpfc_worker_wake_up(phba);
		return;

	}
	spin_unlock_irqrestore(&phba->hbalock, iflag);
}

/**
 * lpfc_get_scsi_buf_s3 - Get a scsi buffer from lpfc_scsi_buf_list of the HBA
 * @phba: The HBA for which this call is being executed.
 *
 * This routine removes a scsi buffer from head of @phba lpfc_scsi_buf_list list
 * and returns to caller.
 *
 * Return codes:
 *   NULL - Error
 *   Pointer to lpfc_scsi_buf - Success
 **/
static struct lpfc_io_buf *
lpfc_get_scsi_buf_s3(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp,
		     struct scsi_cmnd *cmnd)
{
	struct lpfc_io_buf *lpfc_cmd = NULL;
	struct list_head *scsi_buf_list_get = &phba->lpfc_scsi_buf_list_get;
	unsigned long iflag = 0;

	spin_lock_irqsave(&phba->scsi_buf_list_get_lock, iflag);
	list_remove_head(scsi_buf_list_get, lpfc_cmd, struct lpfc_io_buf,
			 list);
	if (!lpfc_cmd) {
		spin_lock(&phba->scsi_buf_list_put_lock);
		list_splice(&phba->lpfc_scsi_buf_list_put,
			    &phba->lpfc_scsi_buf_list_get);
		INIT_LIST_HEAD(&phba->lpfc_scsi_buf_list_put);
		list_remove_head(scsi_buf_list_get, lpfc_cmd,
				 struct lpfc_io_buf, list);
		spin_unlock(&phba->scsi_buf_list_put_lock);
	}
	spin_unlock_irqrestore(&phba->scsi_buf_list_get_lock, iflag);

	if (lpfc_ndlp_check_qdepth(phba, ndlp) && lpfc_cmd) {
		atomic_inc(&ndlp->cmd_pending);
		lpfc_cmd->flags |= LPFC_SBUF_BUMP_QDEPTH;
	}
	return  lpfc_cmd;
}
/**
 * lpfc_get_scsi_buf_s4 - Get a scsi buffer from io_buf_list of the HBA
 * @phba: The HBA for which this call is being executed.
 *
 * This routine removes a scsi buffer from head of @hdwq io_buf_list
 * and returns to caller.
 *
 * Return codes:
 *   NULL - Error
 *   Pointer to lpfc_scsi_buf - Success
 **/
static struct lpfc_io_buf *
lpfc_get_scsi_buf_s4(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp,
		     struct scsi_cmnd *cmnd)
{
	struct lpfc_io_buf *lpfc_cmd;
	struct lpfc_sli4_hdw_queue *qp;
	struct sli4_sge *sgl;
	IOCB_t *iocb;
	dma_addr_t pdma_phys_fcp_rsp;
	dma_addr_t pdma_phys_fcp_cmd;
	uint32_t cpu, idx;
	int tag;
	struct fcp_cmd_rsp_buf *tmp = NULL;

	cpu = raw_smp_processor_id();

	/* Lookup Hardware Queue index based on fcp_io_sched module parameter
	 * and if SCSI mq is turned on.
	 */
#if (KERNEL_MAJOR > 4) || defined(BUILD_RHEL8)
	if (cmnd && phba->cfg_enable_scsi_mq &&
	    (phba->cfg_fcp_io_sched == LPFC_FCP_SCHED_BY_HDWQ)) {
#else
	if (cmnd && shost_use_blk_mq(cmnd->device->host) &&
	    phba->cfg_enable_scsi_mq &&
	    (phba->cfg_fcp_io_sched == LPFC_FCP_SCHED_BY_HDWQ)) {
#endif
		tag = blk_mq_unique_tag(cmnd->request);
		idx = blk_mq_unique_tag_to_hwq(tag);
	} else {
		idx = phba->sli4_hba.cpu_map[cpu].hdwq;
	}

	lpfc_cmd = lpfc_get_io_buf(phba, ndlp, idx,
				   !phba->cfg_xri_rebalancing);
	if (!lpfc_cmd) {
		qp = &phba->sli4_hba.hdwq[idx];
		qp->empty_io_bufs++;
		return NULL;
	}

	/* Setup key fields in buffer that may have been changed
	 * if other protocols used this buffer.
	 */
	lpfc_cmd->cur_iocbq.iocb_flag = LPFC_IO_FCP;
	lpfc_cmd->prot_seg_cnt = 0;
	lpfc_cmd->seg_cnt = 0;
	lpfc_cmd->timeout = 0;
	lpfc_cmd->flags = 0;
	lpfc_cmd->start_time = jiffies;
	lpfc_cmd->edifp = NULL;
	lpfc_cmd->waitq = NULL;
	lpfc_cmd->cpu = cpu;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	lpfc_cmd->prot_data_type = 0;
#endif
	tmp = lpfc_get_cmd_rsp_buf_per_hdwq(phba, lpfc_cmd);
	if (!tmp) {
		lpfc_release_io_buf(phba, lpfc_cmd, lpfc_cmd->hdwq);
		return NULL;
	}

	lpfc_cmd->fcp_cmnd = tmp->fcp_cmnd;
	lpfc_cmd->fcp_rsp = tmp->fcp_rsp;

	/*
	 * The first two SGEs are the FCP_CMD and FCP_RSP.
	 * The balance are sg list bdes. Initialize the
	 * first two and leave the rest for queuecommand.
	 */
	sgl = (struct sli4_sge *)lpfc_cmd->dma_sgl;
	pdma_phys_fcp_cmd = tmp->fcp_cmd_rsp_dma_handle;
	sgl->addr_hi = cpu_to_le32(putPaddrHigh(pdma_phys_fcp_cmd));
	sgl->addr_lo = cpu_to_le32(putPaddrLow(pdma_phys_fcp_cmd));
	sgl->word2 = le32_to_cpu(sgl->word2);
	bf_set(lpfc_sli4_sge_last, sgl, 0);
	sgl->word2 = cpu_to_le32(sgl->word2);
	sgl->sge_len = cpu_to_le32(sizeof(struct fcp_cmnd));
	sgl++;

	/* Setup the physical region for the FCP RSP */
	pdma_phys_fcp_rsp = pdma_phys_fcp_cmd + sizeof(struct fcp_cmnd);
	sgl->addr_hi = cpu_to_le32(putPaddrHigh(pdma_phys_fcp_rsp));
	sgl->addr_lo = cpu_to_le32(putPaddrLow(pdma_phys_fcp_rsp));
	sgl->word2 = le32_to_cpu(sgl->word2);
	bf_set(lpfc_sli4_sge_last, sgl, 1);
	sgl->word2 = cpu_to_le32(sgl->word2);
	sgl->sge_len = cpu_to_le32(sizeof(struct fcp_rsp));

	/*
	 * Since the IOCB for the FCP I/O is built into this
	 * lpfc_io_buf, initialize it with all known data now.
	 */
	iocb = &lpfc_cmd->cur_iocbq.iocb;
	iocb->un.fcpi64.bdl.ulpIoTag32 = 0;
	iocb->un.fcpi64.bdl.bdeFlags = BUFF_TYPE_BDE_64;
	/* setting the BLP size to 2 * sizeof BDE may not be correct.
	 * We are setting the bpl to point to out sgl. An sgl's
	 * entries are 16 bytes, a bpl entries are 12 bytes.
	 */
	iocb->un.fcpi64.bdl.bdeSize = sizeof(struct fcp_cmnd);
	iocb->un.fcpi64.bdl.addrLow = putPaddrLow(pdma_phys_fcp_cmd);
	iocb->un.fcpi64.bdl.addrHigh = putPaddrHigh(pdma_phys_fcp_cmd);
	iocb->ulpBdeCount = 1;
	iocb->ulpLe = 1;
	iocb->ulpClass = CLASS3;

	if (lpfc_ndlp_check_qdepth(phba, ndlp)) {
		atomic_inc(&ndlp->cmd_pending);
		lpfc_cmd->flags |= LPFC_SBUF_BUMP_QDEPTH;
	}
	return  lpfc_cmd;
}
/**
 * lpfc_get_scsi_buf - Get a scsi buffer from lpfc_scsi_buf_list of the HBA
 * @phba: The HBA for which this call is being executed.
 *
 * This routine removes a scsi buffer from head of @phba lpfc_scsi_buf_list list
 * and returns to caller.
 *
 * Return codes:
 *   NULL - Error
 *   Pointer to lpfc_scsi_buf - Success
 **/
static struct lpfc_io_buf*
lpfc_get_scsi_buf(struct lpfc_hba *phba, struct lpfc_nodelist *ndlp,
		  struct scsi_cmnd *cmnd)
{
	return  phba->lpfc_get_scsi_buf(phba, ndlp, cmnd);
}

/**
 * lpfc_release_scsi_buf - Return a scsi buffer back to hba scsi buf list
 * @phba: The Hba for which this call is being executed.
 * @psb: The scsi buffer which is being released.
 *
 * This routine releases @psb scsi buffer by adding it to tail of @phba
 * lpfc_scsi_buf_list list.
 **/
static void
lpfc_release_scsi_buf_s3(struct lpfc_hba *phba, struct lpfc_io_buf *psb)
{
	unsigned long iflag = 0;

	psb->edifp = NULL;
	psb->seg_cnt = 0;
	psb->prot_seg_cnt = 0;

	spin_lock_irqsave(&phba->scsi_buf_list_put_lock, iflag);
	psb->pCmd = NULL;
	psb->cur_iocbq.iocb_flag = LPFC_IO_FCP;
	list_add_tail(&psb->list, &phba->lpfc_scsi_buf_list_put);
	spin_unlock_irqrestore(&phba->scsi_buf_list_put_lock, iflag);
}

/**
 * lpfc_release_scsi_buf_s4: Return a scsi buffer back to hba scsi buf list.
 * @phba: The Hba for which this call is being executed.
 * @psb: The scsi buffer which is being released.
 *
 * This routine releases @psb scsi buffer by adding it to tail of @hdwq
 * io_buf_list list. For SLI4 XRI's are tied to the scsi buffer
 * and cannot be reused for at least RA_TOV amount of time if it was
 * aborted.
 **/
static void
lpfc_release_scsi_buf_s4(struct lpfc_hba *phba, struct lpfc_io_buf *psb)
{
	struct lpfc_sli4_hdw_queue *qp;
	unsigned long iflag = 0;

	psb->edifp = NULL;
	psb->seg_cnt = 0;
	psb->prot_seg_cnt = 0;

	qp = psb->hdwq;
	if (psb->flags & LPFC_SBUF_XBUSY) {
		spin_lock_irqsave(&qp->abts_io_buf_list_lock, iflag);
		psb->pCmd = NULL;
		list_add_tail(&psb->list, &qp->lpfc_abts_io_buf_list);
		qp->abts_scsi_io_bufs++;
		spin_unlock_irqrestore(&qp->abts_io_buf_list_lock, iflag);
	} else {
		lpfc_release_io_buf(phba, (struct lpfc_io_buf *)psb, qp);
	}
}

/**
 * lpfc_release_scsi_buf: Return a scsi buffer back to hba scsi buf list.
 * @phba: The Hba for which this call is being executed.
 * @psb: The scsi buffer which is being released.
 *
 * This routine releases @psb scsi buffer by adding it to tail of @phba
 * lpfc_scsi_buf_list list.
 **/
static void
lpfc_release_scsi_buf(struct lpfc_hba *phba, struct lpfc_io_buf *psb)
{
	if ((psb->flags & LPFC_SBUF_BUMP_QDEPTH) && psb->ndlp)
		atomic_dec(&psb->ndlp->cmd_pending);

	psb->flags &= ~(LPFC_SBUF_NORMAL_DIF | LPFC_SBUF_PASS_DIF |
			LPFC_SBUF_BUMP_QDEPTH);
	phba->lpfc_release_scsi_buf(phba, psb);
}

/**
 * lpfc_fcpcmd_to_iocb - copy the fcp_cmd data into the IOCB
 * @data: A pointer to the immediate command data portion of the IOCB.
 * @fcp_cmnd: The FCP Command that is provided by the SCSI layer.
 *
 * The routine copies the entire FCP command from @fcp_cmnd to @data while
 * byte swapping the data to big endian format for transmission on the wire.
 **/
static void
lpfc_fcpcmd_to_iocb(uint8_t *data, struct fcp_cmnd *fcp_cmnd)
{
	int i, j;
	for (i = 0, j = 0; i < sizeof(struct fcp_cmnd);
	     i += sizeof(uint32_t), j++) {
		((uint32_t *)data)[j] = cpu_to_be32(((uint32_t *)fcp_cmnd)[j]);
	}
}

/**
 * lpfc_scsi_prep_dma_buf_s3 - DMA mapping for scsi buffer to SLI3 IF spec
 * @phba: The Hba for which this call is being executed.
 * @lpfc_cmd: The scsi buffer which is going to be mapped.
 *
 * This routine does the pci dma mapping for scatter-gather list of scsi cmnd
 * field of @lpfc_cmd for device with SLI-3 interface spec. This routine scans
 * through sg elements and format the bde. This routine also initializes all
 * IOCB fields which are dependent on scsi command request buffer.
 *
 * Return codes:
 *   1 - Error
 *   0 - Success
 **/
static int
lpfc_scsi_prep_dma_buf_s3(struct lpfc_hba *phba, struct lpfc_io_buf *lpfc_cmd)
{
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct scatterlist *sgel = NULL;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	struct ulp_bde64 *bpl = (struct ulp_bde64 *)lpfc_cmd->dma_sgl;
	struct lpfc_iocbq *iocbq = &lpfc_cmd->cur_iocbq;
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	struct ulp_bde64 *data_bde = iocb_cmd->unsli3.fcp_ext.dbde;
	dma_addr_t physaddr;
	uint32_t num_bde = 0;
	int nseg, datadir = scsi_cmnd->sc_data_direction;

	/*
	 * There are three possibilities here - use scatter-gather segment, use
	 * the single mapping, or neither.  Start the lpfc command prep by
	 * bumping the bpl beyond the fcp_cmnd and fcp_rsp regions to the first
	 * data bde entry.
	 */
	bpl += 2;
	if (scsi_sg_count(scsi_cmnd)) {
		/*
		 * The driver stores the segment count returned from pci_map_sg
		 * because this a count of dma-mappings used to map the use_sg
		 * pages.  They are not guaranteed to be the same for those
		 * architectures that implement an IOMMU.
		 */

		nseg = dma_map_sg(&phba->pcidev->dev, scsi_sglist(scsi_cmnd),
				  scsi_sg_count(scsi_cmnd), datadir);
		if (unlikely(!nseg))
			return 1;

		lpfc_cmd->seg_cnt = nseg;
		if (lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt) {
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"9064 BLKGRD: %s: Too many sg segments"
					" from dma_map_sg.  Config %d, seg_cnt"
					" %d\n", __func__, phba->cfg_sg_seg_cnt,
					lpfc_cmd->seg_cnt);
			WARN_ON_ONCE(lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt);
			lpfc_cmd->seg_cnt = 0;
			scsi_dma_unmap(scsi_cmnd);
			return 2;
		}

		/*
		 * The driver established a maximum scatter-gather segment count
		 * during probe that limits the number of sg elements in any
		 * single scsi command.  Just run through the seg_cnt and format
		 * the bde's.
		 * When using SLI-3 the driver will try to fit all the BDEs into
		 * the IOCB. If it can't then the BDEs get added to a BPL as it
		 * does for SLI-2 mode.
		 */
		scsi_for_each_sg(scsi_cmnd, sgel, nseg, num_bde) {
			physaddr = sg_dma_address(sgel);
			if (phba->sli_rev == 3 &&
			    !(phba->sli3_options & LPFC_SLI3_BG_ENABLED) &&
			    !(iocbq->iocb_flag & DSS_SECURITY_OP) &&
			    nseg <= LPFC_EXT_DATA_BDE_COUNT) {
				data_bde->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
				data_bde->tus.f.bdeSize = sg_dma_len(sgel);
				data_bde->addrLow = putPaddrLow(physaddr);
				data_bde->addrHigh = putPaddrHigh(physaddr);
				data_bde++;
			} else {
				bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
				bpl->tus.f.bdeSize = sg_dma_len(sgel);
				bpl->tus.w = le32_to_cpu(bpl->tus.w);
				bpl->addrLow =
					le32_to_cpu(putPaddrLow(physaddr));
				bpl->addrHigh =
					le32_to_cpu(putPaddrHigh(physaddr));
				bpl++;
			}
		}
	}

	/*
	 * Finish initializing those IOCB fields that are dependent on the
	 * scsi_cmnd request_buffer.  Note that for SLI-2 the bdeSize is
	 * explicitly reinitialized and for SLI-3 the extended bde count is
	 * explicitly reinitialized since all iocb memory resources are reused.
	 */
	if (phba->sli_rev == 3 &&
	    !(phba->sli3_options & LPFC_SLI3_BG_ENABLED) &&
	    !(iocbq->iocb_flag & DSS_SECURITY_OP)) {
		if (num_bde > LPFC_EXT_DATA_BDE_COUNT) {
			/*
			 * The extended IOCB format can only fit 3 BDE or a BPL.
			 * This I/O has more than 3 BDE so the 1st data bde will
			 * be a BPL that is filled in here.
			 */
			physaddr = lpfc_cmd->dma_handle;
			data_bde->tus.f.bdeFlags = BUFF_TYPE_BLP_64;
			data_bde->tus.f.bdeSize = (num_bde *
						   sizeof(struct ulp_bde64));
			physaddr += (sizeof(struct fcp_cmnd) +
				     sizeof(struct fcp_rsp) +
				     (2 * sizeof(struct ulp_bde64)));
			data_bde->addrHigh = putPaddrHigh(physaddr);
			data_bde->addrLow = putPaddrLow(physaddr);
			/* ebde count includes the response bde and data bpl */
			iocb_cmd->unsli3.fcp_ext.ebde_count = 2;
		} else {
			/* ebde count includes the response bde and data bdes */
			iocb_cmd->unsli3.fcp_ext.ebde_count = (num_bde + 1);
		}
	} else {
		iocb_cmd->un.fcpi64.bdl.bdeSize =
			((num_bde + 2) * sizeof(struct ulp_bde64));
		iocb_cmd->unsli3.fcp_ext.ebde_count = (num_bde + 1);
	}
	fcp_cmnd->fcpDl = cpu_to_be32(scsi_bufflen(scsi_cmnd));

	/*
	 * Due to difference in data length between DIF/non-DIF paths,
	 * we need to set word 4 of IOCB here
	 */
	iocb_cmd->un.fcpi.fcpi_parm = scsi_bufflen(scsi_cmnd);
	lpfc_fcpcmd_to_iocb(iocb_cmd->unsli3.fcp_ext.icd, fcp_cmnd);
	return 0;
}

/**
 * lpfc_scsi_get_prot_op - Gets the SCSI defined protection data operation
 * @sc: The SCSI Layer structure for the IO in question.
 *
 * This routine calls the SCSI Layer to get the protectio data operation
 * associated with the specified IO. Then, if this is an IO effected by an
 * External DIF device, the protection operation is adjusted accordingly.
 *
 * Returns the SCSI defined protection data operation
 **/
uint32_t
lpfc_scsi_get_prot_op(struct scsi_cmnd *sc)
{
	struct lpfc_io_buf *lpfc_cmd;
	uint32_t op = scsi_get_prot_op(sc);

	lpfc_cmd = (struct lpfc_io_buf *)sc->host_scribble;
	if (lpfc_cmd->flags & LPFC_SBUF_NORMAL_DIF) {
		if (sc->sc_data_direction == DMA_FROM_DEVICE)
			op = SCSI_PROT_READ_STRIP;
		else if (sc->sc_data_direction == DMA_TO_DEVICE)
			op = SCSI_PROT_WRITE_INSERT;
	} else if (lpfc_cmd->flags & LPFC_SBUF_PASS_DIF) {
		if (sc->sc_data_direction == DMA_FROM_DEVICE)
			op = SCSI_PROT_READ_PASS;
		else if (sc->sc_data_direction == DMA_TO_DEVICE)
			op = SCSI_PROT_WRITE_PASS;
	}
	return op;
}


#ifdef CONFIG_SCSI_LPFC_DEBUG_FS

/* Return BG_ERR_INIT if error injection is detected by Initiator */
#define BG_ERR_INIT	0x1
/* Return BG_ERR_TGT if error injection is detected by Target */
#define BG_ERR_TGT	0x2
/* Return BG_ERR_SWAP if swapping CSUM<-->CRC is required for error injection */
#define BG_ERR_SWAP	0x10
/**
 * Return BG_ERR_CHECK if disabling Guard/Ref/App checking is required for
 * error injection
 **/
#define BG_ERR_CHECK	0x20

/**
 * lpfc_bg_err_inject - Determine if we should inject an error
 * @phba: The Hba for which this call is being executed.
 * @sc: The SCSI command to examine
 * @reftag: (out) BlockGuard reference tag for transmitted data
 * @apptag: (out) BlockGuard application tag for transmitted data
 * @new_guard (in) Value to replace CRC with if needed
 *
 * Returns BG_ERR_* bit mask or 0 if request ignored
 **/
static int
lpfc_bg_err_inject(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		uint32_t *reftag, uint16_t *apptag, uint32_t new_guard)
{
	struct scatterlist *sgpe; /* s/g prot entry */
	struct lpfc_io_buf *lpfc_cmd = NULL;
	struct scsi_dif_tuple *src = NULL;
	struct lpfc_nodelist *ndlp;
	struct lpfc_rport_data *rdata;
	uint32_t op = lpfc_scsi_get_prot_op(sc);
	uint32_t blksize;
	uint32_t numblks;
	sector_t lba;
	int rc = 0;
	int blockoff = 0;

	if (op == SCSI_PROT_NORMAL)
		return 0;

	sgpe = scsi_prot_sglist(sc);
	lba = scsi_get_lba(sc);

	/* First check if we need to match the LBA */
	if (phba->lpfc_injerr_lba != LPFC_INJERR_LBA_OFF) {
		blksize = lpfc_cmd_blksize(sc);
		numblks = (scsi_bufflen(sc) + blksize - 1) / blksize;

		/* Make sure we have the right LBA if one is specified */
		if ((phba->lpfc_injerr_lba < lba) ||
			(phba->lpfc_injerr_lba >= (lba + numblks)))
			return 0;
		if (sgpe) {
			blockoff = phba->lpfc_injerr_lba - lba;
			numblks = sg_dma_len(sgpe) /
				sizeof(struct scsi_dif_tuple);
			if (numblks < blockoff)
				blockoff = numblks;
		}
	}

	/* Next check if we need to match the remote NPortID or WWPN */
	rdata = lpfc_rport_data_from_scsi_device(sc->device);
	if (rdata && rdata->pnode) {
		ndlp = rdata->pnode;

		/* Make sure we have the right NPortID if one is specified */
		if (phba->lpfc_injerr_nportid  &&
			(phba->lpfc_injerr_nportid != ndlp->nlp_DID))
			return 0;

		/*
		 * Make sure we have the right WWPN if one is specified.
		 * wwn[0] should be a non-zero NAA in a good WWPN.
		 */
		if (phba->lpfc_injerr_wwpn.u.wwn[0]  &&
			(memcmp(&ndlp->nlp_portname, &phba->lpfc_injerr_wwpn,
				sizeof(struct lpfc_name)) != 0))
			return 0;
	}

	/* Setup a ptr to the protection data if the SCSI host provides it */
	if (sgpe) {
		src = (struct scsi_dif_tuple *)sg_virt(sgpe);
		src += blockoff;
		lpfc_cmd = (struct lpfc_io_buf *)sc->host_scribble;
	}

	/* Should we change the Reference Tag */
	if (reftag) {
		if (phba->lpfc_injerr_wref_cnt) {
			switch (op) {
			case SCSI_PROT_WRITE_PASS:
				if (src) {
					/*
					 * For WRITE_PASS, force the error
					 * to be sent on the wire. It should
					 * be detected by the Target.
					 * If blockoff != 0 error will be
					 * inserted in middle of the IO.
					 */

					lpfc_printf_log(phba, KERN_ERR,
							LOG_TRACE_EVENT,
					"9076 BLKGRD: Injecting reftag error: "
					"write lba x%lx + x%x oldrefTag x%x\n",
					(unsigned long)lba, blockoff,
					be32_to_cpu(src->ref_tag));

					/*
					 * Save the old ref_tag so we can
					 * restore it on completion.
					 */
					if (lpfc_cmd) {
						lpfc_cmd->prot_data_type =
							LPFC_INJERR_REFTAG;
						lpfc_cmd->prot_data_segment =
							src;
						lpfc_cmd->prot_data =
							src->ref_tag;
					}
					src->ref_tag = cpu_to_be32(0xDEADBEEF);
					phba->lpfc_injerr_wref_cnt--;
					if (phba->lpfc_injerr_wref_cnt == 0) {
						phba->lpfc_injerr_nportid = 0;
						phba->lpfc_injerr_lba =
							LPFC_INJERR_LBA_OFF;
						memset(&phba->lpfc_injerr_wwpn,
						  0, sizeof(struct lpfc_name));
					}
					rc = BG_ERR_TGT | BG_ERR_CHECK;

					break;
				}
				/* fall through */
			case SCSI_PROT_WRITE_INSERT:
				/*
				 * For WRITE_INSERT, force the error
				 * to be sent on the wire. It should be
				 * detected by the Target.
				 */
				/* DEADBEEF will be the reftag on the wire */
				*reftag = 0xDEADBEEF;
				phba->lpfc_injerr_wref_cnt--;
				if (phba->lpfc_injerr_wref_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
					LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}
				rc = BG_ERR_TGT | BG_ERR_CHECK;

				lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"9078 BLKGRD: Injecting reftag error: "
					"write lba x%lx\n", (unsigned long)lba);
				break;
			case SCSI_PROT_WRITE_STRIP:
				/*
				 * For WRITE_STRIP and WRITE_PASS,
				 * force the error on data
				 * being copied from SLI-Host to SLI-Port.
				 */
				*reftag = 0xDEADBEEF;
				phba->lpfc_injerr_wref_cnt--;
				if (phba->lpfc_injerr_wref_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}
				rc = BG_ERR_INIT;

				lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"9077 BLKGRD: Injecting reftag error: "
					"write lba x%lx\n", (unsigned long)lba);
				break;
			}
		}
		if (phba->lpfc_injerr_rref_cnt) {
			switch (op) {
			case SCSI_PROT_READ_INSERT:
			case SCSI_PROT_READ_STRIP:
			case SCSI_PROT_READ_PASS:
				/*
				 * For READ_STRIP and READ_PASS, force the
				 * error on data being read off the wire. It
				 * should force an IO error to the driver.
				 */
				*reftag = 0xDEADBEEF;
				phba->lpfc_injerr_rref_cnt--;
				if (phba->lpfc_injerr_rref_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}
				rc = BG_ERR_INIT;

				lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"9079 BLKGRD: Injecting reftag error: "
					"read lba x%lx\n", (unsigned long)lba);
				break;
			}
		}
	}

	/* Should we change the Application Tag */
	if (apptag) {
		if (phba->lpfc_injerr_wapp_cnt) {
			switch (op) {
			case SCSI_PROT_WRITE_PASS:
				if (src) {
					/*
					 * For WRITE_PASS, force the error
					 * to be sent on the wire. It should
					 * be detected by the Target.
					 * If blockoff != 0 error will be
					 * inserted in middle of the IO.
					 */

					lpfc_printf_log(phba, KERN_ERR,
							LOG_TRACE_EVENT,
					"9080 BLKGRD: Injecting apptag error: "
					"write lba x%lx + x%x oldappTag x%x\n",
					(unsigned long)lba, blockoff,
					be16_to_cpu(src->app_tag));

					/*
					 * Save the old app_tag so we can
					 * restore it on completion.
					 */
					if (lpfc_cmd) {
						lpfc_cmd->prot_data_type =
							LPFC_INJERR_APPTAG;
						lpfc_cmd->prot_data_segment =
							src;
						lpfc_cmd->prot_data =
							src->app_tag;
					}
					src->app_tag = cpu_to_be16(0xDEAD);
					phba->lpfc_injerr_wapp_cnt--;
					if (phba->lpfc_injerr_wapp_cnt == 0) {
						phba->lpfc_injerr_nportid = 0;
						phba->lpfc_injerr_lba =
							LPFC_INJERR_LBA_OFF;
						memset(&phba->lpfc_injerr_wwpn,
						  0, sizeof(struct lpfc_name));
					}
					rc = BG_ERR_TGT | BG_ERR_CHECK;
					break;
				}
				/* fall through */
			case SCSI_PROT_WRITE_INSERT:
				/*
				 * For WRITE_INSERT, force the
				 * error to be sent on the wire. It should be
				 * detected by the Target.
				 */
				/* DEAD will be the apptag on the wire */
				*apptag = 0xDEAD;
				phba->lpfc_injerr_wapp_cnt--;
				if (phba->lpfc_injerr_wapp_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}
				rc = BG_ERR_TGT | BG_ERR_CHECK;

				lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"0813 BLKGRD: Injecting apptag error: "
					"write lba x%lx\n", (unsigned long)lba);
				break;
			case SCSI_PROT_WRITE_STRIP:
				/*
				 * For WRITE_STRIP and WRITE_PASS,
				 * force the error on data
				 * being copied from SLI-Host to SLI-Port.
				 */
				*apptag = 0xDEAD;
				phba->lpfc_injerr_wapp_cnt--;
				if (phba->lpfc_injerr_wapp_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}
				rc = BG_ERR_INIT;

				lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"0812 BLKGRD: Injecting apptag error: "
					"write lba x%lx\n", (unsigned long)lba);
				break;
			}
		}
		if (phba->lpfc_injerr_rapp_cnt) {
			switch (op) {
			case SCSI_PROT_READ_INSERT:
			case SCSI_PROT_READ_STRIP:
			case SCSI_PROT_READ_PASS:
				/*
				 * For READ_STRIP and READ_PASS, force the
				 * error on data being read off the wire. It
				 * should force an IO error to the driver.
				 */
				*apptag = 0xDEAD;
				phba->lpfc_injerr_rapp_cnt--;
				if (phba->lpfc_injerr_rapp_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}
				rc = BG_ERR_INIT;

				lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"0814 BLKGRD: Injecting apptag error: "
					"read lba x%lx\n", (unsigned long)lba);
				break;
			}
		}
	}


	/* Should we change the Guard Tag */
	if (new_guard) {
		if (phba->lpfc_injerr_wgrd_cnt) {
			switch (op) {
			case SCSI_PROT_WRITE_PASS:
				rc = BG_ERR_CHECK;
				/* fall through */

			case SCSI_PROT_WRITE_INSERT:
				/*
				 * For WRITE_INSERT, force the
				 * error to be sent on the wire. It should be
				 * detected by the Target.
				 */
				phba->lpfc_injerr_wgrd_cnt--;
				if (phba->lpfc_injerr_wgrd_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}

				rc |= BG_ERR_TGT | BG_ERR_SWAP;
				/* Signals the caller to swap CRC->CSUM */

				lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"0817 BLKGRD: Injecting guard error: "
					"write lba x%lx\n", (unsigned long)lba);
				break;
			case SCSI_PROT_WRITE_STRIP:
				/*
				 * For WRITE_STRIP and WRITE_PASS,
				 * force the error on data
				 * being copied from SLI-Host to SLI-Port.
				 */
				phba->lpfc_injerr_wgrd_cnt--;
				if (phba->lpfc_injerr_wgrd_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}

				rc = BG_ERR_INIT | BG_ERR_SWAP;
				/* Signals the caller to swap CRC->CSUM */

				lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"0816 BLKGRD: Injecting guard error: "
					"write lba x%lx\n", (unsigned long)lba);
				break;
			}
		}
		if (phba->lpfc_injerr_rgrd_cnt) {
			switch (op) {
			case SCSI_PROT_READ_INSERT:
			case SCSI_PROT_READ_STRIP:
			case SCSI_PROT_READ_PASS:
				/*
				 * For READ_STRIP and READ_PASS, force the
				 * error on data being read off the wire. It
				 * should force an IO error to the driver.
				 */
				phba->lpfc_injerr_rgrd_cnt--;
				if (phba->lpfc_injerr_rgrd_cnt == 0) {
					phba->lpfc_injerr_nportid = 0;
					phba->lpfc_injerr_lba =
						LPFC_INJERR_LBA_OFF;
					memset(&phba->lpfc_injerr_wwpn,
						0, sizeof(struct lpfc_name));
				}

				rc = BG_ERR_INIT | BG_ERR_SWAP;
				/* Signals the caller to swap CRC->CSUM */

				lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"0818 BLKGRD: Injecting guard error: "
					"read lba x%lx\n", (unsigned long)lba);
			}
		}
	}

	return rc;
}
#endif

/**
 * lpfc_sc_to_bg_opcodes - Determine the BlockGuard opcodes to be used with
 * the specified SCSI command.
 * @phba: The Hba for which this call is being executed.
 * @sc: The SCSI command to examine
 * @txopt: (out) BlockGuard operation for transmitted data
 * @rxopt: (out) BlockGuard operation for received data
 *
 * Returns: zero on success; non-zero if tx and/or rx op cannot be determined
 *
 **/
static int
lpfc_sc_to_bg_opcodes(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		uint8_t *txop, uint8_t *rxop)
{
	uint8_t ret = 0;

	if (lpfc_cmd_guard_csum(sc)) {
		switch (lpfc_scsi_get_prot_op(sc)) {
		case SCSI_PROT_READ_INSERT:
		case SCSI_PROT_WRITE_STRIP:
			*rxop = BG_OP_IN_NODIF_OUT_CSUM;
			*txop = BG_OP_IN_CSUM_OUT_NODIF;
			break;

		case SCSI_PROT_READ_STRIP:
		case SCSI_PROT_WRITE_INSERT:
			*rxop = BG_OP_IN_CRC_OUT_NODIF;
			*txop = BG_OP_IN_NODIF_OUT_CRC;
			break;

		case SCSI_PROT_READ_PASS:
		case SCSI_PROT_WRITE_PASS:
			*rxop = BG_OP_IN_CRC_OUT_CSUM;
			*txop = BG_OP_IN_CSUM_OUT_CRC;
			break;

		case SCSI_PROT_NORMAL:
		default:
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"9063 BLKGRD: Bad op/guard:%d/IP combination\n",
					lpfc_scsi_get_prot_op(sc));
			ret = 1;
			break;

		}
	} else {
		switch (lpfc_scsi_get_prot_op(sc)) {
		case SCSI_PROT_READ_STRIP:
		case SCSI_PROT_WRITE_INSERT:
			*rxop = BG_OP_IN_CRC_OUT_NODIF;
			*txop = BG_OP_IN_NODIF_OUT_CRC;
			break;

		case SCSI_PROT_READ_PASS:
		case SCSI_PROT_WRITE_PASS:
			*rxop = BG_OP_IN_CRC_OUT_CRC;
			*txop = BG_OP_IN_CRC_OUT_CRC;
			break;

		case SCSI_PROT_READ_INSERT:
		case SCSI_PROT_WRITE_STRIP:
			*rxop = BG_OP_IN_NODIF_OUT_CRC;
			*txop = BG_OP_IN_CRC_OUT_NODIF;
			break;

		case SCSI_PROT_NORMAL:
		default:
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"9075 BLKGRD: Bad op/guard:%d/CRC combination\n",
					lpfc_scsi_get_prot_op(sc));
			ret = 1;
			break;
		}
	}

	return ret;
}

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
/**
 * lpfc_bg_err_opcodes - reDetermine the BlockGuard opcodes to be used with
 * the specified SCSI command in order to force a guard tag error.
 * @phba: The Hba for which this call is being executed.
 * @sc: The SCSI command to examine
 * @txopt: (out) BlockGuard operation for transmitted data
 * @rxopt: (out) BlockGuard operation for received data
 *
 * Returns: zero on success; non-zero if tx and/or rx op cannot be determined
 *
 **/
static int
lpfc_bg_err_opcodes(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		uint8_t *txop, uint8_t *rxop)
{
	uint8_t ret = 0;

	if (lpfc_cmd_guard_csum(sc)) {
		switch (lpfc_scsi_get_prot_op(sc)) {
		case SCSI_PROT_READ_INSERT:
		case SCSI_PROT_WRITE_STRIP:
			*rxop = BG_OP_IN_NODIF_OUT_CRC;
			*txop = BG_OP_IN_CRC_OUT_NODIF;
			break;

		case SCSI_PROT_READ_STRIP:
		case SCSI_PROT_WRITE_INSERT:
			*rxop = BG_OP_IN_CSUM_OUT_NODIF;
			*txop = BG_OP_IN_NODIF_OUT_CSUM;
			break;

		case SCSI_PROT_READ_PASS:
		case SCSI_PROT_WRITE_PASS:
			*rxop = BG_OP_IN_CSUM_OUT_CRC;
			*txop = BG_OP_IN_CRC_OUT_CSUM;
			break;

		case SCSI_PROT_NORMAL:
		default:
			break;

		}
	} else {
		switch (lpfc_scsi_get_prot_op(sc)) {
		case SCSI_PROT_READ_STRIP:
		case SCSI_PROT_WRITE_INSERT:
			*rxop = BG_OP_IN_CSUM_OUT_NODIF;
			*txop = BG_OP_IN_NODIF_OUT_CSUM;
			break;

		case SCSI_PROT_READ_PASS:
		case SCSI_PROT_WRITE_PASS:
			*rxop = BG_OP_IN_CSUM_OUT_CSUM;
			*txop = BG_OP_IN_CSUM_OUT_CSUM;
			break;

		case SCSI_PROT_READ_INSERT:
		case SCSI_PROT_WRITE_STRIP:
			*rxop = BG_OP_IN_NODIF_OUT_CSUM;
			*txop = BG_OP_IN_CSUM_OUT_NODIF;
			break;

		case SCSI_PROT_NORMAL:
		default:
			break;
		}
	}

	return ret;
}
#endif

/**
 * lpfc_bg_setup_bpl - Setup BlockGuard BPL with no protection data
 * @phba: The Hba for which this call is being executed.
 * @sc: pointer to scsi command we're working on
 * @bpl: pointer to buffer list for protection groups
 * @datacnt: number of segments of data that have been dma mapped
 *
 * This function sets up BPL buffer list for protection groups of
 * type LPFC_PG_TYPE_NO_DIF
 *
 * This is usually used when the HBA is instructed to generate
 * DIFs and insert them into data stream (or strip DIF from
 * incoming data stream)
 *
 * The buffer list consists of just one protection group described
 * below:
 *                                +-------------------------+
 *   start of prot group  -->     |          PDE_5          |
 *                                +-------------------------+
 *                                |          PDE_6          |
 *                                +-------------------------+
 *                                |         Data BDE        |
 *                                +-------------------------+
 *                                |more Data BDE's ... (opt)|
 *                                +-------------------------+
 *
 *
 * Note: Data s/g buffers have been dma mapped
 *
 * Returns the number of BDEs added to the BPL.
 **/
static int
lpfc_bg_setup_bpl(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		struct ulp_bde64 *bpl, int datasegcnt)
{
	struct scatterlist *sgde = NULL; /* s/g data entry */
	struct lpfc_pde5 *pde5 = NULL;
	struct lpfc_pde6 *pde6 = NULL;
	dma_addr_t physaddr;
	int i = 0, num_bde = 0, status;
	int datadir = sc->sc_data_direction;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint32_t rc;
#endif
	uint32_t checking = 1;
	uint32_t reftag;
	uint8_t txop, rxop;

	status  = lpfc_sc_to_bg_opcodes(phba, sc, &txop, &rxop);
	if (status)
		goto out;

	/* extract some info from the scsi command for pde*/
	reftag = (uint32_t)scsi_get_lba(sc); /* Truncate LBA */

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	rc = lpfc_bg_err_inject(phba, sc, &reftag, NULL, 1);
	if (rc) {
		if (rc & BG_ERR_SWAP)
			lpfc_bg_err_opcodes(phba, sc, &txop, &rxop);
		if (rc & BG_ERR_CHECK)
			checking = 0;
	}
#endif

	/* setup PDE5 with what we have */
	pde5 = (struct lpfc_pde5 *) bpl;
	memset(pde5, 0, sizeof(struct lpfc_pde5));
	bf_set(pde5_type, pde5, LPFC_PDE5_DESCRIPTOR);

	/* Endianness conversion if necessary for PDE5 */
	pde5->word0 = cpu_to_le32(pde5->word0);
	pde5->reftag = cpu_to_le32(reftag);

	/* advance bpl and increment bde count */
	num_bde++;
	bpl++;
	pde6 = (struct lpfc_pde6 *) bpl;

	/* setup PDE6 with the rest of the info */
	memset(pde6, 0, sizeof(struct lpfc_pde6));
	bf_set(pde6_type, pde6, LPFC_PDE6_DESCRIPTOR);
	bf_set(pde6_optx, pde6, txop);
	bf_set(pde6_oprx, pde6, rxop);

	/*
	 * We only need to check the data on READs, for WRITEs
	 * protection data is automatically generated, not checked.
	 */
	if (datadir == DMA_FROM_DEVICE) {
		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_GUARD))
			bf_set(pde6_ce, pde6, checking);
		else
			bf_set(pde6_ce, pde6, 0);

		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_REF))
			bf_set(pde6_re, pde6, checking);
		else
			bf_set(pde6_re, pde6, 0);
	}
	bf_set(pde6_ai, pde6, 1);
	bf_set(pde6_ae, pde6, 0);
	bf_set(pde6_apptagval, pde6, 0);

	/* Endianness conversion if necessary for PDE6 */
	pde6->word0 = cpu_to_le32(pde6->word0);
	pde6->word1 = cpu_to_le32(pde6->word1);
	pde6->word2 = cpu_to_le32(pde6->word2);

	/* advance bpl and increment bde count */
	num_bde++;
	bpl++;

	/* assumption: caller has already run dma_map_sg on command data */
	scsi_for_each_sg(sc, sgde, datasegcnt, i) {
		physaddr = sg_dma_address(sgde);
		bpl->addrLow = le32_to_cpu(putPaddrLow(physaddr));
		bpl->addrHigh = le32_to_cpu(putPaddrHigh(physaddr));
		bpl->tus.f.bdeSize = sg_dma_len(sgde);
		if (datadir == DMA_TO_DEVICE)
			bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
		else
			bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64I;
		bpl->tus.w = le32_to_cpu(bpl->tus.w);
		bpl++;
		num_bde++;
	}

out:
	return num_bde;
}

/**
 * lpfc_bg_setup_bpl_prot - Setup BlockGuard BPL with protection data
 * @phba: The Hba for which this call is being executed.
 * @sc: pointer to scsi command we're working on
 * @bpl: pointer to buffer list for protection groups
 * @datacnt: number of segments of data that have been dma mapped
 * @protcnt: number of segment of protection data that have been dma mapped
 *
 * This function sets up BPL buffer list for protection groups of
 * type LPFC_PG_TYPE_DIF
 *
 * This is usually used when DIFs are in their own buffers,
 * separate from the data. The HBA can then by instructed
 * to place the DIFs in the outgoing stream.  For read operations,
 * The HBA could extract the DIFs and place it in DIF buffers.
 *
 * The buffer list for this type consists of one or more of the
 * protection groups described below:
 *                                    +-------------------------+
 *   start of first prot group  -->   |          PDE_5          |
 *                                    +-------------------------+
 *                                    |          PDE_6          |
 *                                    +-------------------------+
 *                                    |      PDE_7 (Prot BDE)   |
 *                                    +-------------------------+
 *                                    |        Data BDE         |
 *                                    +-------------------------+
 *                                    |more Data BDE's ... (opt)|
 *                                    +-------------------------+
 *   start of new  prot group  -->    |          PDE_5          |
 *                                    +-------------------------+
 *                                    |          ...            |
 *                                    +-------------------------+
 *
 * Note: It is assumed that both data and protection s/g buffers have been
 *       mapped for DMA
 *
 * Returns the number of BDEs added to the BPL.
 **/
static int
lpfc_bg_setup_bpl_prot(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		struct ulp_bde64 *bpl, int datacnt, int protcnt)
{
	struct scatterlist *sgde = NULL; /* s/g data entry */
	struct scatterlist *sgpe = NULL; /* s/g prot entry */
	struct lpfc_pde5 *pde5 = NULL;
	struct lpfc_pde6 *pde6 = NULL;
	struct lpfc_pde7 *pde7 = NULL;
	dma_addr_t dataphysaddr, protphysaddr;
	unsigned short curr_data = 0, curr_prot = 0;
	unsigned int split_offset;
	unsigned int protgroup_len, protgroup_offset = 0, protgroup_remainder;
	unsigned int protgrp_blks, protgrp_bytes;
	unsigned int remainder, subtotal;
	int status;
	int datadir = sc->sc_data_direction;
	unsigned char pgdone = 0, alldone = 0;
	unsigned blksize;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint32_t rc;
#endif
	uint32_t checking = 1;
	uint32_t reftag;
	uint8_t txop, rxop;
	int num_bde = 0;

	sgpe = scsi_prot_sglist(sc);
	sgde = scsi_sglist(sc);

	if (!sgpe || !sgde) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"9020 Invalid s/g entry: data=x%px prot=x%px\n",
				sgpe, sgde);
		return 0;
	}

	status = lpfc_sc_to_bg_opcodes(phba, sc, &txop, &rxop);
	if (status)
		goto out;

	/* extract some info from the scsi command */
	blksize = lpfc_cmd_blksize(sc);
	reftag = (uint32_t)scsi_get_lba(sc); /* Truncate LBA */

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	rc = lpfc_bg_err_inject(phba, sc, &reftag, NULL, 1);
	if (rc) {
		if (rc & BG_ERR_SWAP)
			lpfc_bg_err_opcodes(phba, sc, &txop, &rxop);
		if (rc & BG_ERR_CHECK)
			checking = 0;
	}
#endif

	split_offset = 0;
	do {
		/* Check to see if we ran out of space */
		if (num_bde >= (phba->cfg_total_seg_cnt - 2))
			return num_bde + 3;

		/* setup PDE5 with what we have */
		pde5 = (struct lpfc_pde5 *) bpl;
		memset(pde5, 0, sizeof(struct lpfc_pde5));
		bf_set(pde5_type, pde5, LPFC_PDE5_DESCRIPTOR);

		/* Endianness conversion if necessary for PDE5 */
		pde5->word0 = cpu_to_le32(pde5->word0);
		pde5->reftag = cpu_to_le32(reftag);

		/* advance bpl and increment bde count */
		num_bde++;
		bpl++;
		pde6 = (struct lpfc_pde6 *) bpl;

		/* setup PDE6 with the rest of the info */
		memset(pde6, 0, sizeof(struct lpfc_pde6));
		bf_set(pde6_type, pde6, LPFC_PDE6_DESCRIPTOR);
		bf_set(pde6_optx, pde6, txop);
		bf_set(pde6_oprx, pde6, rxop);

		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_GUARD))
			bf_set(pde6_ce, pde6, checking);
		else
			bf_set(pde6_ce, pde6, 0);

		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_REF))
			bf_set(pde6_re, pde6, checking);
		else
			bf_set(pde6_re, pde6, 0);

		bf_set(pde6_ai, pde6, 1);
		bf_set(pde6_ae, pde6, 0);
		bf_set(pde6_apptagval, pde6, 0);

		/* Endianness conversion if necessary for PDE6 */
		pde6->word0 = cpu_to_le32(pde6->word0);
		pde6->word1 = cpu_to_le32(pde6->word1);
		pde6->word2 = cpu_to_le32(pde6->word2);

		/* advance bpl and increment bde count */
		num_bde++;
		bpl++;

		/* setup the first BDE that points to protection buffer */
		protphysaddr = sg_dma_address(sgpe) + protgroup_offset;
		protgroup_len = sg_dma_len(sgpe) - protgroup_offset;

		/* must be integer multiple of the DIF block length */
		BUG_ON(protgroup_len % 8);

		pde7 = (struct lpfc_pde7 *) bpl;
		memset(pde7, 0, sizeof(struct lpfc_pde7));
		bf_set(pde7_type, pde7, LPFC_PDE7_DESCRIPTOR);

		pde7->addrHigh = le32_to_cpu(putPaddrHigh(protphysaddr));
		pde7->addrLow = le32_to_cpu(putPaddrLow(protphysaddr));

		protgrp_blks = protgroup_len / 8;
		protgrp_bytes = protgrp_blks * blksize;

		/* check if this pde is crossing the 4K boundary; if so split */
		if ((pde7->addrLow & 0xfff) + protgroup_len > 0x1000) {
			protgroup_remainder = 0x1000 - (pde7->addrLow & 0xfff);
			protgroup_offset += protgroup_remainder;
			protgrp_blks = protgroup_remainder / 8;
			protgrp_bytes = protgrp_blks * blksize;
		} else {
			protgroup_offset = 0;
			curr_prot++;
		}

		num_bde++;

		/* setup BDE's for data blocks associated with DIF data */
		pgdone = 0;
		subtotal = 0; /* total bytes processed for current prot grp */
		while (!pgdone) {
			/* Check to see if we ran out of space */
			if (num_bde >= phba->cfg_total_seg_cnt)
				return num_bde + 1;

			if (!sgde) {
				lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"9065 BLKGRD:%s Invalid data segment\n",
						__func__);
				return 0;
			}
			bpl++;
			dataphysaddr = sg_dma_address(sgde) + split_offset;
			bpl->addrLow = le32_to_cpu(putPaddrLow(dataphysaddr));
			bpl->addrHigh = le32_to_cpu(putPaddrHigh(dataphysaddr));

			remainder = sg_dma_len(sgde) - split_offset;

			if ((subtotal + remainder) <= protgrp_bytes) {
				/* we can use this whole buffer */
				bpl->tus.f.bdeSize = remainder;
				split_offset = 0;

				if ((subtotal + remainder) == protgrp_bytes)
					pgdone = 1;
			} else {
				/* must split this buffer with next prot grp */
				bpl->tus.f.bdeSize = protgrp_bytes - subtotal;
				split_offset += bpl->tus.f.bdeSize;
			}

			subtotal += bpl->tus.f.bdeSize;

			if (datadir == DMA_TO_DEVICE)
				bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
			else
				bpl->tus.f.bdeFlags = BUFF_TYPE_BDE_64I;
			bpl->tus.w = le32_to_cpu(bpl->tus.w);

			num_bde++;
			curr_data++;

			if (split_offset)
				break;

			/* Move to the next s/g segment if possible */
			sgde = sg_next(sgde);

		}

		if (protgroup_offset) {
			/* update the reference tag */
			reftag += protgrp_blks;
			bpl++;
			continue;
		}

		/* are we done ? */
		if (curr_prot == protcnt) {
			alldone = 1;
		} else if (curr_prot < protcnt) {
			/* advance to next prot buffer */
			sgpe = sg_next(sgpe);
			bpl++;

			/* update the reference tag */
			reftag += protgrp_blks;
		} else {
			/* if we're here, we have a bug */
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"9054 BLKGRD: bug in %s\n", __func__);
		}

	} while (!alldone);
out:

	return num_bde;
}

/**
 * lpfc_bg_setup_sgl - Setup BlockGuard SGL with no protection data
 * @phba: The Hba for which this call is being executed.
 * @sc: pointer to scsi command we're working on
 * @sgl: pointer to buffer list for protection groups
 * @datacnt: number of segments of data that have been dma mapped
 *
 * This function sets up SGL buffer list for protection groups of
 * type LPFC_PG_TYPE_NO_DIF
 *
 * This is usually used when the HBA is instructed to generate
 * DIFs and insert them into data stream (or strip DIF from
 * incoming data stream)
 *
 * The buffer list consists of just one protection group described
 * below:
 *                                +-------------------------+
 *   start of prot group  -->     |         DI_SEED         |
 *                                +-------------------------+
 *                                |         Data SGE        |
 *                                +-------------------------+
 *                                |more Data SGE's ... (opt)|
 *                                +-------------------------+
 *
 *
 * Note: Data s/g buffers have been dma mapped
 *
 * Returns the number of SGEs added to the SGL.
 **/
static int
lpfc_bg_setup_sgl(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		struct sli4_sge *sgl, int datasegcnt,
		struct lpfc_io_buf *lpfc_cmd)
{
	struct scatterlist *sgde = NULL; /* s/g data entry */
	struct sli4_sge_diseed *diseed = NULL;
	dma_addr_t physaddr;
	int i = 0, num_sge = 0, status;
	uint32_t reftag;
	uint8_t txop, rxop;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint32_t rc;
#endif
	uint32_t checking = 1;
	uint32_t dma_len;
	uint32_t dma_offset = 0;
	struct sli4_hybrid_sgl *sgl_xtra = NULL;
	int j;
	bool lsp_just_set = false;

	status  = lpfc_sc_to_bg_opcodes(phba, sc, &txop, &rxop);
	if (status)
		goto out;

	/* extract some info from the scsi command for pde*/
	reftag = (uint32_t)scsi_get_lba(sc); /* Truncate LBA */

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	rc = lpfc_bg_err_inject(phba, sc, &reftag, NULL, 1);
	if (rc) {
		if (rc & BG_ERR_SWAP)
			lpfc_bg_err_opcodes(phba, sc, &txop, &rxop);
		if (rc & BG_ERR_CHECK)
			checking = 0;
	}
#endif

	/* setup DISEED with what we have */
	diseed = (struct sli4_sge_diseed *) sgl;
	memset(diseed, 0, sizeof(struct sli4_sge_diseed));
	bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_DISEED);

	/* Endianness conversion if necessary */
	diseed->ref_tag = cpu_to_le32(reftag);
	diseed->ref_tag_tran = diseed->ref_tag;

	/*
	 * We only need to check the data on READs, for WRITEs
	 * protection data is automatically generated, not checked.
	 */
	if (sc->sc_data_direction == DMA_FROM_DEVICE) {
		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_GUARD))
			bf_set(lpfc_sli4_sge_dif_ce, diseed, checking);
		else
			bf_set(lpfc_sli4_sge_dif_ce, diseed, 0);

		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_REF))
			bf_set(lpfc_sli4_sge_dif_re, diseed, checking);
		else
			bf_set(lpfc_sli4_sge_dif_re, diseed, 0);
	}

	/* setup DISEED with the rest of the info */
	bf_set(lpfc_sli4_sge_dif_optx, diseed, txop);
	bf_set(lpfc_sli4_sge_dif_oprx, diseed, rxop);

	bf_set(lpfc_sli4_sge_dif_ai, diseed, 1);
	bf_set(lpfc_sli4_sge_dif_me, diseed, 0);

	/* Endianness conversion if necessary for DISEED */
	diseed->word2 = cpu_to_le32(diseed->word2);
	diseed->word3 = cpu_to_le32(diseed->word3);

	/* advance bpl and increment sge count */
	num_sge++;
	sgl++;

	/* assumption: caller has already run dma_map_sg on command data */
	sgde = scsi_sglist(sc);
	j = 3;
	for (i = 0; i < datasegcnt; i++) {
		/* clear it */
		sgl->word2 = 0;

		/* do we need to expand the segment */
		if (!lsp_just_set && !((j + 1) % phba->border_sge_num) &&
		    ((datasegcnt - 1) != i)) {
			/* set LSP type */
			bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_LSP);

			sgl_xtra = lpfc_get_sgl_per_hdwq(phba, lpfc_cmd);

			if (unlikely(!sgl_xtra)) {
				lpfc_cmd->seg_cnt = 0;
				return 0;
			}
			sgl->addr_lo = cpu_to_le32(putPaddrLow(
						sgl_xtra->dma_phys_sgl));
			sgl->addr_hi = cpu_to_le32(putPaddrHigh(
						sgl_xtra->dma_phys_sgl));

		} else {
			bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_DATA);
		}

		if (!(bf_get(lpfc_sli4_sge_type, sgl) & LPFC_SGE_TYPE_LSP)) {
			if ((datasegcnt - 1) == i)
				bf_set(lpfc_sli4_sge_last, sgl, 1);
			physaddr = sg_dma_address(sgde);
			dma_len = sg_dma_len(sgde);
			sgl->addr_lo = cpu_to_le32(putPaddrLow(physaddr));
			sgl->addr_hi = cpu_to_le32(putPaddrHigh(physaddr));

			bf_set(lpfc_sli4_sge_offset, sgl, dma_offset);
			sgl->word2 = cpu_to_le32(sgl->word2);
			sgl->sge_len = cpu_to_le32(dma_len);

			dma_offset += dma_len;
			sgde = sg_next(sgde);

			sgl++;
			num_sge++;
			lsp_just_set = false;

		} else {
			sgl->word2 = cpu_to_le32(sgl->word2);
			sgl->sge_len = cpu_to_le32(phba->cfg_sg_dma_buf_size);

			sgl = (struct sli4_sge *)sgl_xtra->dma_sgl;
			i = i - 1;

			lsp_just_set = true;
		}

		j++;

	}

out:
	return num_sge;
}

/**
 * lpfc_bg_setup_sgl_prot - Setup BlockGuard SGL with protection data
 * @phba: The Hba for which this call is being executed.
 * @sc: pointer to scsi command we're working on
 * @sgl: pointer to buffer list for protection groups
 * @datacnt: number of segments of data that have been dma mapped
 * @protcnt: number of segment of protection data that have been dma mapped
 *
 * This function sets up SGL buffer list for protection groups of
 * type LPFC_PG_TYPE_DIF
 *
 * This is usually used when DIFs are in their own buffers,
 * separate from the data. The HBA can then by instructed
 * to place the DIFs in the outgoing stream.  For read operations,
 * The HBA could extract the DIFs and place it in DIF buffers.
 *
 * The buffer list for this type consists of one or more of the
 * protection groups described below:
 *                                    +-------------------------+
 *   start of first prot group  -->   |         DISEED          |
 *                                    +-------------------------+
 *                                    |      DIF (Prot SGE)     |
 *                                    +-------------------------+
 *                                    |        Data SGE         |
 *                                    +-------------------------+
 *                                    |more Data SGE's ... (opt)|
 *                                    +-------------------------+
 *   start of new  prot group  -->    |         DISEED          |
 *                                    +-------------------------+
 *                                    |          ...            |
 *                                    +-------------------------+
 *
 * Note: It is assumed that both data and protection s/g buffers have been
 *       mapped for DMA
 *
 * Returns the number of SGEs added to the SGL.
 **/
static int
lpfc_bg_setup_sgl_prot(struct lpfc_hba *phba, struct scsi_cmnd *sc,
		struct sli4_sge *sgl, int datacnt, int protcnt,
		struct lpfc_io_buf *lpfc_cmd)
{
	struct scatterlist *sgde = NULL; /* s/g data entry */
	struct scatterlist *sgpe = NULL; /* s/g prot entry */
	struct sli4_sge_diseed *diseed = NULL;
	dma_addr_t dataphysaddr, protphysaddr;
	unsigned short curr_data = 0, curr_prot = 0;
	unsigned int split_offset;
	unsigned int protgroup_len, protgroup_offset = 0, protgroup_remainder;
	unsigned int protgrp_blks, protgrp_bytes;
	unsigned int remainder, subtotal;
	int status;
	unsigned char pgdone = 0, alldone = 0;
	unsigned blksize;
	uint32_t reftag;
	uint8_t txop, rxop;
	uint32_t dma_len;
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	uint32_t rc;
#endif
	uint32_t checking = 1;
	uint32_t dma_offset = 0;
	int num_sge = 0, j = 2;
	struct sli4_hybrid_sgl *sgl_xtra = NULL;

	sgpe = scsi_prot_sglist(sc);
	sgde = scsi_sglist(sc);

	if (!sgpe || !sgde) {
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"9082 Invalid s/g entry: data=x%px prot=x%px\n",
				sgpe, sgde);
		return 0;
	}

	status = lpfc_sc_to_bg_opcodes(phba, sc, &txop, &rxop);
	if (status)
		goto out;

	/* extract some info from the scsi command */
	blksize = lpfc_cmd_blksize(sc);
	reftag = (uint32_t)scsi_get_lba(sc); /* Truncate LBA */

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	rc = lpfc_bg_err_inject(phba, sc, &reftag, NULL, 1);
	if (rc) {
		if (rc & BG_ERR_SWAP)
			lpfc_bg_err_opcodes(phba, sc, &txop, &rxop);
		if (rc & BG_ERR_CHECK)
			checking = 0;
	}
#endif

	split_offset = 0;
	do {
		/* Check to see if we ran out of space */
		if ((num_sge >= (phba->cfg_total_seg_cnt - 2)) &&
		    !(phba->cfg_xpsgl))
			return num_sge + 3;

		/* DISEED and DIF have to be together */
		if (!((j + 1) % phba->border_sge_num) ||
		    !((j + 2) % phba->border_sge_num) ||
		    !((j + 3) % phba->border_sge_num)) {
			sgl->word2 = 0;

			/* set LSP type */
			bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_LSP);

			sgl_xtra = lpfc_get_sgl_per_hdwq(phba, lpfc_cmd);

			if (unlikely(!sgl_xtra)) {
				goto out;
			} else {
				sgl->addr_lo = cpu_to_le32(putPaddrLow(
						sgl_xtra->dma_phys_sgl));
				sgl->addr_hi = cpu_to_le32(putPaddrHigh(
						       sgl_xtra->dma_phys_sgl));
			}

			sgl->word2 = cpu_to_le32(sgl->word2);
			sgl->sge_len = cpu_to_le32(phba->cfg_sg_dma_buf_size);

			sgl = (struct sli4_sge *)sgl_xtra->dma_sgl;
			j = 0;
		}

		/* setup DISEED with what we have */
		diseed = (struct sli4_sge_diseed *) sgl;
		memset(diseed, 0, sizeof(struct sli4_sge_diseed));
		bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_DISEED);

		/* Endianness conversion if necessary */
		diseed->ref_tag = cpu_to_le32(reftag);
		diseed->ref_tag_tran = diseed->ref_tag;

		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_GUARD)) {
			bf_set(lpfc_sli4_sge_dif_ce, diseed, checking);

		} else {
			bf_set(lpfc_sli4_sge_dif_ce, diseed, 0);
			/*
			 * When in this mode, the hardware will replace
			 * the guard tag from the host with a
			 * newly generated good CRC for the wire.
			 * Switch to raw mode here to avoid this
			 * behavior. What the host sends gets put on the wire.
			 */
			if (txop == BG_OP_IN_CRC_OUT_CRC) {
				txop = BG_OP_RAW_MODE;
				rxop = BG_OP_RAW_MODE;
			}
		}


		if (lpfc_cmd_protect(sc, LPFC_CHECK_PROTECT_REF))
			bf_set(lpfc_sli4_sge_dif_re, diseed, checking);
		else
			bf_set(lpfc_sli4_sge_dif_re, diseed, 0);

		/* setup DISEED with the rest of the info */
		bf_set(lpfc_sli4_sge_dif_optx, diseed, txop);
		bf_set(lpfc_sli4_sge_dif_oprx, diseed, rxop);

		bf_set(lpfc_sli4_sge_dif_ai, diseed, 1);
		bf_set(lpfc_sli4_sge_dif_me, diseed, 0);

		/* Endianness conversion if necessary for DISEED */
		diseed->word2 = cpu_to_le32(diseed->word2);
		diseed->word3 = cpu_to_le32(diseed->word3);

		/* advance sgl and increment bde count */
		num_sge++;

		sgl++;
		j++;

		/* setup the first BDE that points to protection buffer */
		protphysaddr = sg_dma_address(sgpe) + protgroup_offset;
		protgroup_len = sg_dma_len(sgpe) - protgroup_offset;

		/* must be integer multiple of the DIF block length */
		BUG_ON(protgroup_len % 8);

		/* Now setup DIF SGE */
		sgl->word2 = 0;
		bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_DIF);
		sgl->addr_hi = le32_to_cpu(putPaddrHigh(protphysaddr));
		sgl->addr_lo = le32_to_cpu(putPaddrLow(protphysaddr));
		sgl->word2 = cpu_to_le32(sgl->word2);
		sgl->sge_len = 0;

		protgrp_blks = protgroup_len / 8;
		protgrp_bytes = protgrp_blks * blksize;

		/* check if DIF SGE is crossing the 4K boundary; if so split */
		if ((sgl->addr_lo & 0xfff) + protgroup_len > 0x1000) {
			protgroup_remainder = 0x1000 - (sgl->addr_lo & 0xfff);
			protgroup_offset += protgroup_remainder;
			protgrp_blks = protgroup_remainder / 8;
			protgrp_bytes = protgrp_blks * blksize;
		} else {
			protgroup_offset = 0;
			curr_prot++;
		}

		num_sge++;

		/* setup SGE's for data blocks associated with DIF data */
		pgdone = 0;
		subtotal = 0; /* total bytes processed for current prot grp */

		sgl++;
		j++;

		while (!pgdone) {
			/* Check to see if we ran out of space */
			if ((num_sge >= phba->cfg_total_seg_cnt) &&
			    !phba->cfg_xpsgl)
				return num_sge + 1;

			if (!sgde) {
				lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"9086 BLKGRD:%s Invalid data segment\n",
						__func__);
				return 0;
			}

			if (!((j + 1) % phba->border_sge_num)) {
				sgl->word2 = 0;

				/* set LSP type */
				bf_set(lpfc_sli4_sge_type, sgl,
				       LPFC_SGE_TYPE_LSP);

				sgl_xtra = lpfc_get_sgl_per_hdwq(phba,
								 lpfc_cmd);

				if (unlikely(!sgl_xtra)) {
					goto out;
				} else {
					sgl->addr_lo = cpu_to_le32(
					  putPaddrLow(sgl_xtra->dma_phys_sgl));
					sgl->addr_hi = cpu_to_le32(
					  putPaddrHigh(sgl_xtra->dma_phys_sgl));
				}

				sgl->word2 = cpu_to_le32(sgl->word2);
				sgl->sge_len = cpu_to_le32(
						     phba->cfg_sg_dma_buf_size);

				sgl = (struct sli4_sge *)sgl_xtra->dma_sgl;
			} else {
				dataphysaddr = sg_dma_address(sgde) +
								   split_offset;

				remainder = sg_dma_len(sgde) - split_offset;

				if ((subtotal + remainder) <= protgrp_bytes) {
					/* we can use this whole buffer */
					dma_len = remainder;
					split_offset = 0;

					if ((subtotal + remainder) ==
								  protgrp_bytes)
						pgdone = 1;
				} else {
					/* must split this buffer with next
					 * prot grp
					 */
					dma_len = protgrp_bytes - subtotal;
					split_offset += dma_len;
				}

				subtotal += dma_len;

				sgl->word2 = 0;
				sgl->addr_lo = cpu_to_le32(putPaddrLow(
								 dataphysaddr));
				sgl->addr_hi = cpu_to_le32(putPaddrHigh(
								 dataphysaddr));
				bf_set(lpfc_sli4_sge_last, sgl, 0);
				bf_set(lpfc_sli4_sge_offset, sgl, dma_offset);
				bf_set(lpfc_sli4_sge_type, sgl,
				       LPFC_SGE_TYPE_DATA);

				sgl->sge_len = cpu_to_le32(dma_len);
				dma_offset += dma_len;

				num_sge++;
				curr_data++;

				if (split_offset) {
					sgl++;
					j++;
					break;
				}

				/* Move to the next s/g segment if possible */
				sgde = sg_next(sgde);

				sgl++;
			}

			j++;
		}

		if (protgroup_offset) {
			/* update the reference tag */
			reftag += protgrp_blks;
			continue;
		}

		/* are we done ? */
		if (curr_prot == protcnt) {
			/* mark the last SGL */
			sgl--;
			bf_set(lpfc_sli4_sge_last, sgl, 1);
			alldone = 1;
		} else if (curr_prot < protcnt) {
			/* advance to next prot buffer */
			sgpe = sg_next(sgpe);

			/* update the reference tag */
			reftag += protgrp_blks;
		} else {
			/* if we're here, we have a bug */
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"9085 BLKGRD: bug in %s\n", __func__);
		}

	} while (!alldone);

out:

	return num_sge;
}

/**
 * lpfc_prot_group_type - Get prtotection group type of SCSI command
 * @phba: The Hba for which this call is being executed.
 * @sc: pointer to scsi command we're working on
 *
 * Given a SCSI command that supports DIF, determine composition of protection
 * groups involved in setting up buffer lists
 *
 * Returns: Protection group type (with or without DIF)
 *
 **/
static int
lpfc_prot_group_type(struct lpfc_hba *phba, struct scsi_cmnd *sc)
{
	int ret = LPFC_PG_TYPE_INVALID;
	unsigned char op = lpfc_scsi_get_prot_op(sc);

	switch (op) {
	case SCSI_PROT_READ_STRIP:
	case SCSI_PROT_WRITE_INSERT:
		ret = LPFC_PG_TYPE_NO_DIF;
		break;
	case SCSI_PROT_READ_INSERT:
	case SCSI_PROT_WRITE_STRIP:
	case SCSI_PROT_READ_PASS:
	case SCSI_PROT_WRITE_PASS:
		ret = LPFC_PG_TYPE_DIF_BUF;
		break;
	default:
		if (phba)
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"9021 Unsupported protection op:%d\n",
					op);
		break;
	}
	return ret;
}

/**
 * lpfc_bg_scsi_adjust_dl - Adjust SCSI data length for BlockGuard
 * @phba: The Hba for which this call is being executed.
 * @lpfc_cmd: The scsi buffer which is going to be adjusted.
 *
 * Adjust the data length to account for how much data
 * is actually on the wire.
 *
 * returns the adjusted data length
 **/
static int
lpfc_bg_scsi_adjust_dl(struct lpfc_hba *phba,
		       struct lpfc_io_buf *lpfc_cmd)
{
	struct scsi_cmnd *sc = lpfc_cmd->pCmd;
	int fcpdl;

	fcpdl = scsi_bufflen(sc);

	/* Check if there is protection data on the wire */
	if (sc->sc_data_direction == DMA_FROM_DEVICE) {
		/* Read check for protection data */
		if (lpfc_scsi_get_prot_op(sc) ==  SCSI_PROT_READ_INSERT)
			return fcpdl;

	} else {
		/* Write check for protection data */
		if (lpfc_scsi_get_prot_op(sc) ==  SCSI_PROT_WRITE_STRIP)
			return fcpdl;
	}

	/*
	 * If we are in DIF Type 1 mode every data block has a 8 byte
	 * DIF (trailer) attached to it. Must ajust FCP data length
	 * to account for the protection data.
	 */
	fcpdl += (fcpdl / lpfc_cmd_blksize(sc)) * 8;

	return fcpdl;
}

/**
 * lpfc_bg_scsi_prep_dma_buf_s3 - DMA mapping for scsi buffer to SLI3 IF spec
 * @phba: The Hba for which this call is being executed.
 * @lpfc_cmd: The scsi buffer which is going to be prep'ed.
 *
 * This is the protection/DIF aware version of
 * lpfc_scsi_prep_dma_buf(). It may be a good idea to combine the
 * two functions eventually, but for now, it's here.
 * RETURNS 0 - SUCCESS,
 *         1 - Failed DMA map, retry.
 *         2 - Invalid scsi cmd or prot-type. Do not rety.
 **/
static int
lpfc_bg_scsi_prep_dma_buf_s3(struct lpfc_hba *phba,
		struct lpfc_io_buf *lpfc_cmd)
{
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	struct ulp_bde64 *bpl = (struct ulp_bde64 *)lpfc_cmd->dma_sgl;
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	uint32_t num_bde = 0;
	int datasegcnt, protsegcnt, datadir = scsi_cmnd->sc_data_direction;
	int prot_group_type = 0;
	int fcpdl;
	int ret = 1;

	/*
	 * Start the lpfc command prep by bumping the bpl beyond fcp_cmnd
	 *  fcp_rsp regions to the first data bde entry
	 */
	bpl += 2;
	if (scsi_sg_count(scsi_cmnd)) {
		/*
		 * The driver stores the segment count returned from pci_map_sg
		 * because this a count of dma-mappings used to map the use_sg
		 * pages.  They are not guaranteed to be the same for those
		 * architectures that implement an IOMMU.
		 */
		datasegcnt = dma_map_sg(&phba->pcidev->dev,
					scsi_sglist(scsi_cmnd),
					scsi_sg_count(scsi_cmnd), datadir);
		if (unlikely(!datasegcnt))
			return 1;

		lpfc_cmd->seg_cnt = datasegcnt;

		/* First check if data segment count from SCSI Layer is good */
		if (lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt) {
			WARN_ON_ONCE(lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt);
			ret = 2;
			goto err;
		}

		prot_group_type = lpfc_prot_group_type(phba, scsi_cmnd);

		switch (prot_group_type) {
		case LPFC_PG_TYPE_NO_DIF:

			/* Here we need to add a PDE5 and PDE6 to the count */
			if ((lpfc_cmd->seg_cnt + 2) > phba->cfg_total_seg_cnt) {
				ret = 2;
				goto err;
			}

			num_bde = lpfc_bg_setup_bpl(phba, scsi_cmnd, bpl,
					datasegcnt);
			/* we should have 2 or more entries in buffer list */
			if (num_bde < 2) {
				ret = 2;
				goto err;
			}
			break;

		case LPFC_PG_TYPE_DIF_BUF:
			/*
			 * This type indicates that protection buffers are
			 * passed to the driver, so that needs to be prepared
			 * for DMA
			 */
			protsegcnt = dma_map_sg(&phba->pcidev->dev,
					scsi_prot_sglist(scsi_cmnd),
					scsi_prot_sg_count(scsi_cmnd), datadir);
			if (unlikely(!protsegcnt)) {
				scsi_dma_unmap(scsi_cmnd);
				return 1;
			}

			lpfc_cmd->prot_seg_cnt = protsegcnt;

			/*
			 * There is a minimun of 4 BPLs used for every
			 * protection data segment.
			 */
			if ((lpfc_cmd->prot_seg_cnt * 4) >
			    (phba->cfg_total_seg_cnt - 2)) {
				ret = 2;
				goto err;
			}

			num_bde = lpfc_bg_setup_bpl_prot(phba, scsi_cmnd, bpl,
					datasegcnt, protsegcnt);
			/* we should have 3 or more entries in buffer list */
			if ((num_bde < 3) ||
			    (num_bde > phba->cfg_total_seg_cnt)) {
				ret = 2;
				goto err;
			}
			break;

		case LPFC_PG_TYPE_INVALID:
		default:
			scsi_dma_unmap(scsi_cmnd);
			lpfc_cmd->seg_cnt = 0;

			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"9022 Unexpected protection group %i\n",
					prot_group_type);
			return 2;
		}
	}

	/*
	 * Finish initializing those IOCB fields that are dependent on the
	 * scsi_cmnd request_buffer.  Note that the bdeSize is explicitly
	 * reinitialized since all iocb memory resources are used many times
	 * for transmit, receive, and continuation bpl's.
	 */
	iocb_cmd->un.fcpi64.bdl.bdeSize = (2 * sizeof(struct ulp_bde64));
	iocb_cmd->un.fcpi64.bdl.bdeSize += (num_bde * sizeof(struct ulp_bde64));
	iocb_cmd->ulpBdeCount = 1;
	iocb_cmd->ulpLe = 1;

	fcpdl = lpfc_bg_scsi_adjust_dl(phba, lpfc_cmd);
	fcp_cmnd->fcpDl = be32_to_cpu(fcpdl);

	/*
	 * Due to difference in data length between DIF/non-DIF paths,
	 * we need to set word 4 of IOCB here
	 */
	iocb_cmd->un.fcpi.fcpi_parm = fcpdl;

	/*
	 * For First burst, we may need to adjust the initial transfer
	 * length for DIF
	 */
	if (iocb_cmd->un.fcpi.fcpi_XRdy &&
	    (fcpdl < lpfc_cmd->ndlp->first_burst))
		iocb_cmd->un.fcpi.fcpi_XRdy = fcpdl;

	return 0;
err:
	if (lpfc_cmd->seg_cnt)
		scsi_dma_unmap(scsi_cmnd);
	if (lpfc_cmd->prot_seg_cnt)
		dma_unmap_sg(&phba->pcidev->dev, scsi_prot_sglist(scsi_cmnd),
			     scsi_prot_sg_count(scsi_cmnd),
			     scsi_cmnd->sc_data_direction);

	lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
			"9023 Cannot setup S/G List for HBA"
			"IO segs %d/%d BPL %d SCSI %d: %d %d\n",
			lpfc_cmd->seg_cnt, lpfc_cmd->prot_seg_cnt,
			phba->cfg_total_seg_cnt, phba->cfg_sg_seg_cnt,
			prot_group_type, num_bde);

	lpfc_cmd->seg_cnt = 0;
	lpfc_cmd->prot_seg_cnt = 0;
	return ret;
}

/*
 * This function calcuates the T10 DIF guard tag
 * on the specified data using a CRC algorithmn
 * using crc_t10dif.
 */
static uint16_t
lpfc_bg_crc(uint8_t *data, int count)
{
	uint16_t crc = 0;
	uint16_t x;

#if defined(BUILD_CITRIX_XS)
	uint16_t poly = 0x8BB7L;
	unsigned int poly_length = 16;
	unsigned int i, j, fb;

	for (i = 0; i < count; i += 2) {

		x = (data[i] << 8) | data[i+1];

		/* serial shift register implementation */
		for (j = 0; j < poly_length; j++) {
			fb = ((x & 0x8000L) == 0x8000L) ^ ((crc &
			      0x8000L) == 0x8000L);
			x <<= 1;
			crc <<= 1;
			if (fb)
				crc ^= poly;
		}
	}

#else
	crc = crc_t10dif(data, count);
#endif
	x = cpu_to_be16(crc);
	return x;
}

/*
 * This function calcuates the T10 DIF guard tag
 * on the specified data using a CSUM algorithmn
 * using ip_compute_csum.
 */
static uint16_t
lpfc_bg_csum(uint8_t *data, int count)
{
	uint16_t ret;

	ret = ip_compute_csum(data, count);
	return ret;
}

/*
 * This function examines the protection data to try to determine
 * what type of T10-DIF error occurred.
 */
static void
lpfc_calc_bg_err(struct lpfc_hba *phba, struct lpfc_io_buf *lpfc_cmd)
{
	struct scatterlist *sgpe; /* s/g prot entry */
	struct scatterlist *sgde; /* s/g data entry */
	struct scsi_cmnd *cmd = lpfc_cmd->pCmd;
	struct scsi_dif_tuple *src = NULL;
	uint8_t *data_src = NULL;
	uint16_t guard_tag;
	uint16_t start_app_tag, app_tag;
	uint32_t start_ref_tag, ref_tag;
	int prot, protsegcnt;
	int err_type, len, data_len;
	int chk_ref, chk_app, chk_guard;
	uint16_t sum;
	unsigned blksize;

	err_type = BGS_GUARD_ERR_MASK;
	sum = 0;
	guard_tag = 0;

	/* First check to see if there is protection data to examine */
	prot = lpfc_scsi_get_prot_op(cmd);
	if ((prot == SCSI_PROT_READ_STRIP) ||
	    (prot == SCSI_PROT_WRITE_INSERT) ||
	    (prot == SCSI_PROT_NORMAL))
		goto out;

	/* Currently the driver just supports ref_tag and guard_tag checking */
	chk_ref = 1;
	chk_app = 0;
	chk_guard = 0;

	/* Setup a ptr to the protection data provided by the SCSI host */
	sgpe = scsi_prot_sglist(cmd);
	protsegcnt = lpfc_cmd->prot_seg_cnt;

	if (sgpe && protsegcnt) {

		/*
		 * We will only try to verify guard tag if the segment
		 * data length is a multiple of the blksize.
		 */
		sgde = scsi_sglist(cmd);
		blksize = lpfc_cmd_blksize(cmd);
		data_src = (uint8_t *)sg_virt(sgde);
		data_len = sgde->length;
		if ((data_len & (blksize - 1)) == 0)
			chk_guard = 1;

		src = (struct scsi_dif_tuple *)sg_virt(sgpe);
		start_ref_tag = (uint32_t)scsi_get_lba(cmd); /* Truncate LBA */
		start_app_tag = src->app_tag;
		len = sgpe->length;
		while (src && protsegcnt) {
			while (len) {

				/*
				 * First check to see if a protection data
				 * check is valid
				 */
				if ((src->ref_tag == 0xffffffff) ||
				    (src->app_tag == 0xffff)) {
					start_ref_tag++;
					goto skipit;
				}

				/* First Guard Tag checking */
				if (chk_guard) {
					guard_tag = src->guard_tag;
					if (lpfc_cmd_guard_csum(cmd))
						sum = lpfc_bg_csum(data_src,
								   blksize);
					else
						sum = lpfc_bg_crc(data_src,
								  blksize);
					if ((guard_tag != sum)) {
						err_type = BGS_GUARD_ERR_MASK;
						goto out;
					}
				}

				/* Reference Tag checking */
				ref_tag = be32_to_cpu(src->ref_tag);
				if (chk_ref && (ref_tag != start_ref_tag)) {
					err_type = BGS_REFTAG_ERR_MASK;
					goto out;
				}
				start_ref_tag++;

				/* App Tag checking */
				app_tag = src->app_tag;
				if (chk_app && (app_tag != start_app_tag)) {
					err_type = BGS_APPTAG_ERR_MASK;
					goto out;
				}
skipit:
				len -= sizeof(struct scsi_dif_tuple);
				if (len < 0)
					len = 0;
				src++;

				data_src += blksize;
				data_len -= blksize;

				/*
				 * Are we at the end of the Data segment?
				 * The data segment is only used for Guard
				 * tag checking.
				 */
				if (chk_guard && (data_len == 0)) {
					chk_guard = 0;
					sgde = sg_next(sgde);
					if (!sgde)
						goto out;

					data_src = (uint8_t *)sg_virt(sgde);
					data_len = sgde->length;
					if ((data_len & (blksize - 1)) == 0)
						chk_guard = 1;
				}
			}

			/* Goto the next Protection data segment */
			sgpe = sg_next(sgpe);
			if (sgpe) {
				src = (struct scsi_dif_tuple *)sg_virt(sgpe);
				len = sgpe->length;
			} else {
				src = NULL;
			}
			protsegcnt--;
		}
	}
out:
	if (err_type == BGS_GUARD_ERR_MASK) {
		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
					0x10, 0x1);
		cmd->result = DRIVER_SENSE << 24 | DID_ABORT << 16 |
			      SAM_STAT_CHECK_CONDITION;
		phba->bg_guard_err_cnt++;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9069 BLKGRD: LBA %lx grd_tag error %x != %x\n",
				(unsigned long)scsi_get_lba(cmd),
				sum, guard_tag);

	} else if (err_type == BGS_REFTAG_ERR_MASK) {
		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
					0x10, 0x3);
		cmd->result = DRIVER_SENSE << 24 | DID_ABORT << 16 |
			      SAM_STAT_CHECK_CONDITION;

		phba->bg_reftag_err_cnt++;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9066 BLKGRD: LBA %lx ref_tag error %x != %x\n",
				(unsigned long)scsi_get_lba(cmd),
				ref_tag, start_ref_tag);

	} else if (err_type == BGS_APPTAG_ERR_MASK) {
		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
					0x10, 0x2);
		cmd->result = DRIVER_SENSE << 24 | DID_ABORT << 16 |
			      SAM_STAT_CHECK_CONDITION;

		phba->bg_apptag_err_cnt++;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9041 BLKGRD: LBA %lx app_tag error %x != %x\n",
				(unsigned long)scsi_get_lba(cmd),
				app_tag, start_app_tag);
	}
}


/*
 * This function checks for BlockGuard errors detected by
 * the HBA.  In case of errors, the ASC/ASCQ fields in the
 * sense buffer will be set accordingly, paired with
 * ILLEGAL_REQUEST to signal to the kernel that the HBA
 * detected corruption.
 *
 * Returns:
 *  0 - No error found
 *  1 - BlockGuard error found
 * -1 - Internal error (bad profile, ...etc)
 */
static int
lpfc_parse_bg_err(struct lpfc_hba *phba, struct lpfc_io_buf *lpfc_cmd,
		  struct lpfc_iocbq *pIocbOut)
{
	struct scsi_cmnd *cmd = lpfc_cmd->pCmd;
	struct sli3_bg_fields *bgf = &pIocbOut->iocb.unsli3.sli3_bg;
	int ret = 0;
	uint32_t bghm = bgf->bghm;
	uint32_t bgstat = bgf->bgstat;
	uint64_t failing_sector = 0;

	if (lpfc_bgs_get_invalid_prof(bgstat)) {
		cmd->result = DID_ERROR << 16;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9072 BLKGRD: Invalid BG Profile in cmd"
				" 0x%x lba 0x%llx blk cnt 0x%x "
				"bgstat=x%x bghm=x%x\n", cmd->cmnd[0],
				(unsigned long long)scsi_get_lba(cmd),
				blk_rq_sectors(cmd->request), bgstat, bghm);
		ret = (-1);
		goto out;
	}

	if (lpfc_bgs_get_uninit_dif_block(bgstat)) {
		cmd->result = DID_ERROR << 16;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9073 BLKGRD: Invalid BG PDIF Block in cmd"
				" 0x%x lba 0x%llx blk cnt 0x%x "
				"bgstat=x%x bghm=x%x\n", cmd->cmnd[0],
				(unsigned long long)scsi_get_lba(cmd),
				blk_rq_sectors(cmd->request), bgstat, bghm);
		ret = (-1);
		goto out;
	}

	if (lpfc_bgs_get_guard_err(bgstat)) {
		ret = 1;

		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
				0x10, 0x1);
		cmd->result = DRIVER_SENSE << 24 | DID_ABORT << 16 |
			      SAM_STAT_CHECK_CONDITION;
		phba->bg_guard_err_cnt++;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9055 BLKGRD: Guard Tag error in cmd"
				" 0x%x lba 0x%llx blk cnt 0x%x "
				"bgstat=x%x bghm=x%x\n", cmd->cmnd[0],
				(unsigned long long)scsi_get_lba(cmd),
				blk_rq_sectors(cmd->request), bgstat, bghm);
	}

	if (lpfc_bgs_get_reftag_err(bgstat)) {
		ret = 1;

		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
				0x10, 0x3);
		cmd->result = DRIVER_SENSE << 24 | DID_ABORT << 16 |
			      SAM_STAT_CHECK_CONDITION;

		phba->bg_reftag_err_cnt++;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9056 BLKGRD: Ref Tag error in cmd"
				" 0x%x lba 0x%llx blk cnt 0x%x "
				"bgstat=x%x bghm=x%x\n", cmd->cmnd[0],
				(unsigned long long)scsi_get_lba(cmd),
				blk_rq_sectors(cmd->request), bgstat, bghm);
	}

	if (lpfc_bgs_get_apptag_err(bgstat)) {
		ret = 1;

		scsi_build_sense_buffer(1, cmd->sense_buffer, ILLEGAL_REQUEST,
				0x10, 0x2);
		cmd->result = DRIVER_SENSE << 24 | DID_ABORT << 16 |
			      SAM_STAT_CHECK_CONDITION;

		phba->bg_apptag_err_cnt++;
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9061 BLKGRD: App Tag error in cmd"
				" 0x%x lba 0x%llx blk cnt 0x%x "
				"bgstat=x%x bghm=x%x\n", cmd->cmnd[0],
				(unsigned long long)scsi_get_lba(cmd),
				blk_rq_sectors(cmd->request), bgstat, bghm);
	}

	if (lpfc_bgs_get_hi_water_mark_present(bgstat)) {
		/*
		 * setup sense data descriptor 0 per SPC-4 as an information
		 * field, and put the failing LBA in it.
		 * This code assumes there was also a guard/app/ref tag error
		 * indication.
		 */
		cmd->sense_buffer[7] = 0xc;   /* Additional sense length */
		cmd->sense_buffer[8] = 0;     /* Information descriptor type */
		cmd->sense_buffer[9] = 0xa;   /* Additional descriptor length */
		cmd->sense_buffer[10] = 0x80; /* Validity bit */

		/* bghm is a "on the wire" FC frame based count */
		switch (lpfc_scsi_get_prot_op(cmd)) {
		case SCSI_PROT_READ_INSERT:
		case SCSI_PROT_WRITE_STRIP:
			bghm /= cmd->device->sector_size;
			break;
		case SCSI_PROT_READ_STRIP:
		case SCSI_PROT_WRITE_INSERT:
		case SCSI_PROT_READ_PASS:
		case SCSI_PROT_WRITE_PASS:
			bghm /= (cmd->device->sector_size +
				sizeof(struct scsi_dif_tuple));
			break;
		}

		failing_sector = scsi_get_lba(cmd);
		failing_sector += bghm;

		/* Descriptor Information */
		put_unaligned_be64(failing_sector, &cmd->sense_buffer[12]);
	}

	if (!ret) {
		/* No error was reported - problem in FW? */
		lpfc_printf_log(phba, KERN_WARNING, LOG_FCP | LOG_BG,
				"9057 BLKGRD: Unknown error in cmd"
				" 0x%x lba 0x%llx blk cnt 0x%x "
				"bgstat=x%x bghm=x%x\n", cmd->cmnd[0],
				(unsigned long long)scsi_get_lba(cmd),
				blk_rq_sectors(cmd->request), bgstat, bghm);

		/* Calcuate what type of error it was */
		lpfc_calc_bg_err(phba, lpfc_cmd);
	}
out:
	return ret;
}

/**
 * lpfc_scsi_prep_dma_buf_s4 - DMA mapping for scsi buffer to SLI4 IF spec
 * @phba: The Hba for which this call is being executed.
 * @lpfc_cmd: The scsi buffer which is going to be mapped.
 *
 * This routine does the pci dma mapping for scatter-gather list of scsi cmnd
 * field of @lpfc_cmd for device with SLI-4 interface spec.
 *
 * Return codes:
 *	2 - Error - Do not retry
 *	1 - Error - Retry
 *	0 - Success
 **/
static int
lpfc_scsi_prep_dma_buf_s4(struct lpfc_hba *phba, struct lpfc_io_buf *lpfc_cmd)
{
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct scatterlist *sgel = NULL;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	struct sli4_sge *sgl = (struct sli4_sge *)lpfc_cmd->dma_sgl;
	struct sli4_sge *first_data_sgl;
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	dma_addr_t physaddr;
	uint32_t num_bde = 0;
	uint32_t dma_len;
	uint32_t dma_offset = 0;
	int nseg, i, j;
	struct ulp_bde64 *bde;
	bool lsp_just_set = false;
	struct sli4_hybrid_sgl *sgl_xtra = NULL;

	/*
	 * There are three possibilities here - use scatter-gather segment, use
	 * the single mapping, or neither.  Start the lpfc command prep by
	 * bumping the bpl beyond the fcp_cmnd and fcp_rsp regions to the first
	 * data bde entry.
	 */
	if (scsi_sg_count(scsi_cmnd)) {
		/*
		 * The driver stores the segment count returned from pci_map_sg
		 * because this a count of dma-mappings used to map the use_sg
		 * pages.  They are not guaranteed to be the same for those
		 * architectures that implement an IOMMU.
		 */

		nseg = scsi_dma_map(scsi_cmnd);
		if (unlikely(nseg <= 0))
			return 1;
		sgl += 1;
		/* clear the last flag in the fcp_rsp map entry */
		sgl->word2 = le32_to_cpu(sgl->word2);
		bf_set(lpfc_sli4_sge_last, sgl, 0);
		sgl->word2 = cpu_to_le32(sgl->word2);
		sgl += 1;
		first_data_sgl = sgl;
		lpfc_cmd->seg_cnt = nseg;
		if (!phba->cfg_xpsgl &&
		    lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt) {
			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"9074 BLKGRD:"
					" %s: Too many sg segments from "
					"dma_map_sg.  Config %d, seg_cnt %d\n",
					__func__, phba->cfg_sg_seg_cnt,
					lpfc_cmd->seg_cnt);
			WARN_ON_ONCE(lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt);
			lpfc_cmd->seg_cnt = 0;
			scsi_dma_unmap(scsi_cmnd);
			return 2;
		}

		/*
		 * The driver established a maximum scatter-gather segment count
		 * during probe that limits the number of sg elements in any
		 * single scsi command.  Just run through the seg_cnt and format
		 * the sge's.
		 * When using SLI-3 the driver will try to fit all the BDEs into
		 * the IOCB. If it can't then the BDEs get added to a BPL as it
		 * does for SLI-2 mode.
		 */

		/* for tracking segment boundaries */
		sgel = scsi_sglist(scsi_cmnd);
		j = 2;
		for (i = 0; i < nseg; i++) {
			sgl->word2 = 0;
			if ((num_bde + 1) == nseg) {
				bf_set(lpfc_sli4_sge_last, sgl, 1);
				bf_set(lpfc_sli4_sge_type, sgl,
				       LPFC_SGE_TYPE_DATA);
			} else {
				bf_set(lpfc_sli4_sge_last, sgl, 0);

				/* do we need to expand the segment */
				if (!lsp_just_set &&
				    !((j + 1) % phba->border_sge_num) &&
				    ((nseg - 1) != i)) {
					/* set LSP type */
					bf_set(lpfc_sli4_sge_type, sgl,
					       LPFC_SGE_TYPE_LSP);

					sgl_xtra = lpfc_get_sgl_per_hdwq(
							phba, lpfc_cmd);

					if (unlikely(!sgl_xtra)) {
						lpfc_cmd->seg_cnt = 0;
						scsi_dma_unmap(scsi_cmnd);
						return 1;
					}
					sgl->addr_lo = cpu_to_le32(putPaddrLow(
						       sgl_xtra->dma_phys_sgl));
					sgl->addr_hi = cpu_to_le32(putPaddrHigh(
						       sgl_xtra->dma_phys_sgl));

				} else {
					bf_set(lpfc_sli4_sge_type, sgl,
					       LPFC_SGE_TYPE_DATA);
				}
			}

			if (!(bf_get(lpfc_sli4_sge_type, sgl) &
				     LPFC_SGE_TYPE_LSP)) {
				if ((nseg - 1) == i)
					bf_set(lpfc_sli4_sge_last, sgl, 1);

				physaddr = sg_dma_address(sgel);
				dma_len = sg_dma_len(sgel);
				sgl->addr_lo = cpu_to_le32(putPaddrLow(
							   physaddr));
				sgl->addr_hi = cpu_to_le32(putPaddrHigh(
							   physaddr));

				bf_set(lpfc_sli4_sge_offset, sgl, dma_offset);
				sgl->word2 = cpu_to_le32(sgl->word2);
				sgl->sge_len = cpu_to_le32(dma_len);

				dma_offset += dma_len;
				sgel = sg_next(sgel);

				sgl++;
				lsp_just_set = false;

			} else {
				sgl->word2 = cpu_to_le32(sgl->word2);
				sgl->sge_len = cpu_to_le32(
						     phba->cfg_sg_dma_buf_size);

				sgl = (struct sli4_sge *)sgl_xtra->dma_sgl;
				i = i - 1;

				lsp_just_set = true;
			}

			j++;
		}
		/*
		 * Setup the first Payload BDE. For FCoE we just key off
		 * Performance Hints, for FC we use lpfc_enable_pbde.
		 * We populate words 13-15 of IOCB/WQE.
		 */
		if ((phba->sli3_options & LPFC_SLI4_PERFH_ENABLED) ||
		    phba->cfg_enable_pbde) {
			bde = (struct ulp_bde64 *)
				&(iocb_cmd->unsli3.sli3Words[5]);
			bde->addrLow = first_data_sgl->addr_lo;
			bde->addrHigh = first_data_sgl->addr_hi;
			bde->tus.f.bdeSize =
					le32_to_cpu(first_data_sgl->sge_len);
			bde->tus.f.bdeFlags = BUFF_TYPE_BDE_64;
			bde->tus.w = cpu_to_le32(bde->tus.w);
		}
	} else {
		sgl += 1;
		/* clear the last flag in the fcp_rsp map entry */
		sgl->word2 = le32_to_cpu(sgl->word2);
		bf_set(lpfc_sli4_sge_last, sgl, 1);
		sgl->word2 = cpu_to_le32(sgl->word2);

		if ((phba->sli3_options & LPFC_SLI4_PERFH_ENABLED) ||
		    phba->cfg_enable_pbde) {
			bde = (struct ulp_bde64 *)
				&(iocb_cmd->unsli3.sli3Words[5]);
			memset(bde, 0, (sizeof(uint32_t) * 3));
		}
	}

	/*
	 * Finish initializing those IOCB fields that are dependent on the
	 * scsi_cmnd request_buffer.  Note that for SLI-2 the bdeSize is
	 * explicitly reinitialized.
	 * all iocb memory resources are reused.
	 */
	fcp_cmnd->fcpDl = cpu_to_be32(scsi_bufflen(scsi_cmnd));

	/*
	 * Due to difference in data length between DIF/non-DIF paths,
	 * we need to set word 4 of IOCB here
	 */
	iocb_cmd->un.fcpi.fcpi_parm = scsi_bufflen(scsi_cmnd);

	/*
	 * If the OAS driver feature is enabled and the lun is enabled for
	 * OAS, set the oas iocb related flags.
	 */
	if ((phba->cfg_fof) && ((struct lpfc_device_data *)
		scsi_cmnd->device->hostdata)->oas_enabled) {
		lpfc_cmd->cur_iocbq.iocb_flag |= (LPFC_IO_OAS | LPFC_IO_FOF);
		lpfc_cmd->cur_iocbq.priority = ((struct lpfc_device_data *)
			scsi_cmnd->device->hostdata)->priority;
	}

	return 0;
}

/**
 * lpfc_bg_scsi_prep_dma_buf_s4 - DMA mapping for scsi buffer to SLI4 IF spec
 * @phba: The Hba for which this call is being executed.
 * @lpfc_cmd: The scsi buffer which is going to be mapped.
 *
 * This is the protection/DIF aware version of
 * lpfc_scsi_prep_dma_buf(). It may be a good idea to combine the
 * two functions eventually, but for now, it's here
 * Return codes:
 *	2 - Error - Do not retry
 *	1 - Error - Retry
 *	0 - Success
 **/
static int
lpfc_bg_scsi_prep_dma_buf_s4(struct lpfc_hba *phba,
		struct lpfc_io_buf *lpfc_cmd)
{
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	struct sli4_sge *sgl = (struct sli4_sge *)(lpfc_cmd->dma_sgl);
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	uint32_t num_sge = 0;
	int datasegcnt, protsegcnt, datadir = scsi_cmnd->sc_data_direction;
	int prot_group_type = 0;
	int fcpdl;
	int ret = 1;

	/*
	 * Start the lpfc command prep by bumping the sgl beyond fcp_cmnd
	 *  fcp_rsp regions to the first data sge entry
	 */
	if (scsi_sg_count(scsi_cmnd)) {
		/*
		 * The driver stores the segment count returned from pci_map_sg
		 * because this a count of dma-mappings used to map the use_sg
		 * pages.  They are not guaranteed to be the same for those
		 * architectures that implement an IOMMU.
		 */
		datasegcnt = dma_map_sg(&phba->pcidev->dev,
					scsi_sglist(scsi_cmnd),
					scsi_sg_count(scsi_cmnd), datadir);
		if (unlikely(!datasegcnt))
			return 1;

		sgl += 1;
		/* clear the last flag in the fcp_rsp map entry */
		sgl->word2 = le32_to_cpu(sgl->word2);
		bf_set(lpfc_sli4_sge_last, sgl, 0);
		sgl->word2 = cpu_to_le32(sgl->word2);

		sgl += 1;
		lpfc_cmd->seg_cnt = datasegcnt;

		/* First check if data segment count from SCSI Layer is good */
		if (lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt &&
		    !phba->cfg_xpsgl) {
			WARN_ON_ONCE(lpfc_cmd->seg_cnt > phba->cfg_sg_seg_cnt);
			ret = 2;
			goto err;
		}

		prot_group_type = lpfc_prot_group_type(phba, scsi_cmnd);

		switch (prot_group_type) {
		case LPFC_PG_TYPE_NO_DIF:
			/* Here we need to add a DISEED to the count */
			if (((lpfc_cmd->seg_cnt + 1) >
					phba->cfg_total_seg_cnt) &&
			    !phba->cfg_xpsgl) {
				ret = 2;
				goto err;
			}

			num_sge = lpfc_bg_setup_sgl(phba, scsi_cmnd, sgl,
					datasegcnt, lpfc_cmd);

			/* we should have 2 or more entries in buffer list */
			if (num_sge < 2) {
				ret = 2;
				goto err;
			}
			break;

		case LPFC_PG_TYPE_DIF_BUF:
			/*
			 * This type indicates that protection buffers are
			 * passed to the driver, so that needs to be prepared
			 * for DMA
			 */
			protsegcnt = dma_map_sg(&phba->pcidev->dev,
					scsi_prot_sglist(scsi_cmnd),
					scsi_prot_sg_count(scsi_cmnd), datadir);
			if (unlikely(!protsegcnt)) {
				scsi_dma_unmap(scsi_cmnd);
				return 1;
			}

			lpfc_cmd->prot_seg_cnt = protsegcnt;
			/*
			 * There is a minimun of 3 SGEs used for every
			 * protection data segment.
			 */
			if (((lpfc_cmd->prot_seg_cnt * 3) >
					(phba->cfg_total_seg_cnt - 2)) &&
			    !phba->cfg_xpsgl) {
				ret = 2;
				goto err;
			}

			num_sge = lpfc_bg_setup_sgl_prot(phba, scsi_cmnd, sgl,
					datasegcnt, protsegcnt, lpfc_cmd);

			/* we should have 3 or more entries in buffer list */
			if (num_sge < 3 ||
			    (num_sge > phba->cfg_total_seg_cnt &&
			     !phba->cfg_xpsgl)) {
				ret = 2;
				goto err;
			}
			break;

		case LPFC_PG_TYPE_INVALID:
		default:
			scsi_dma_unmap(scsi_cmnd);
			lpfc_cmd->seg_cnt = 0;

			lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
					"9083 Unexpected protection group %i\n",
					prot_group_type);
			return 2;
		}
	}

	switch (lpfc_scsi_get_prot_op(scsi_cmnd)) {
	case SCSI_PROT_WRITE_STRIP:
	case SCSI_PROT_READ_STRIP:
		lpfc_cmd->cur_iocbq.iocb_flag |= LPFC_IO_DIF_STRIP;
		break;
	case SCSI_PROT_WRITE_INSERT:
	case SCSI_PROT_READ_INSERT:
		lpfc_cmd->cur_iocbq.iocb_flag |= LPFC_IO_DIF_INSERT;
		break;
	case SCSI_PROT_WRITE_PASS:
	case SCSI_PROT_READ_PASS:
		lpfc_cmd->cur_iocbq.iocb_flag |= LPFC_IO_DIF_PASS;
		break;
	}

	fcpdl = lpfc_bg_scsi_adjust_dl(phba, lpfc_cmd);
	fcp_cmnd->fcpDl = be32_to_cpu(fcpdl);

	/*
	 * Due to difference in data length between DIF/non-DIF paths,
	 * we need to set word 4 of IOCB here
	 */
	iocb_cmd->un.fcpi.fcpi_parm = fcpdl;

	/*
	 * For First burst, we may need to adjust the initial transfer
	 * length for DIF
	 */
	if (iocb_cmd->un.fcpi.fcpi_XRdy &&
	    (fcpdl < lpfc_cmd->ndlp->first_burst))
		iocb_cmd->un.fcpi.fcpi_XRdy = fcpdl;

	/*
	 * If the OAS driver feature is enabled and the lun is enabled for
	 * OAS, set the oas iocb related flags.
	 */
	if ((phba->cfg_fof) && ((struct lpfc_device_data *)
		scsi_cmnd->device->hostdata)->oas_enabled)
		lpfc_cmd->cur_iocbq.iocb_flag |= (LPFC_IO_OAS | LPFC_IO_FOF);

	return 0;
err:
	if (lpfc_cmd->seg_cnt)
		scsi_dma_unmap(scsi_cmnd);
	if (lpfc_cmd->prot_seg_cnt)
		dma_unmap_sg(&phba->pcidev->dev, scsi_prot_sglist(scsi_cmnd),
			     scsi_prot_sg_count(scsi_cmnd),
			     scsi_cmnd->sc_data_direction);

	lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
			"9084 Cannot setup S/G List for HBA"
			"IO segs %d/%d SGL %d SCSI %d: %d %d\n",
			lpfc_cmd->seg_cnt, lpfc_cmd->prot_seg_cnt,
			phba->cfg_total_seg_cnt, phba->cfg_sg_seg_cnt,
			prot_group_type, num_sge);

	lpfc_cmd->seg_cnt = 0;
	lpfc_cmd->prot_seg_cnt = 0;
	return ret;
}

/**
 * lpfc_scsi_prep_dma_buf - Wrapper function for DMA mapping of scsi buffer
 * @phba: The Hba for which this call is being executed.
 * @lpfc_cmd: The scsi buffer which is going to be mapped.
 *
 * This routine wraps the actual DMA mapping function pointer from the
 * lpfc_hba struct.
 *
 * Return codes:
 *	1 - Error
 *	0 - Success
 **/
static inline int
lpfc_scsi_prep_dma_buf(struct lpfc_hba *phba, struct lpfc_io_buf *lpfc_cmd)
{
	return phba->lpfc_scsi_prep_dma_buf(phba, lpfc_cmd);
}

/**
 * lpfc_bg_scsi_prep_dma_buf - Wrapper function for DMA mapping of scsi buffer
 * using BlockGuard.
 * @phba: The Hba for which this call is being executed.
 * @lpfc_cmd: The scsi buffer which is going to be mapped.
 *
 * This routine wraps the actual DMA mapping function pointer from the
 * lpfc_hba struct.
 *
 * Return codes:
 *	1 - Error
 *	0 - Success
 **/
static inline int
lpfc_bg_scsi_prep_dma_buf(struct lpfc_hba *phba, struct lpfc_io_buf *lpfc_cmd)
{
	return phba->lpfc_bg_scsi_prep_dma_buf(phba, lpfc_cmd);
}

/**
 * lpfc_send_scsi_error_event - Posts an event when there is SCSI error
 * @phba: Pointer to hba context object.
 * @vport: Pointer to vport object.
 * @lpfc_cmd: Pointer to lpfc scsi command which reported the error.
 * @rsp_iocb: Pointer to response iocb object which reported error.
 *
 * This function posts an event when there is a SCSI command reporting
 * error from the scsi device.
 **/
static void
lpfc_send_scsi_error_event(struct lpfc_hba *phba, struct lpfc_vport *vport,
		struct lpfc_io_buf *lpfc_cmd, struct lpfc_iocbq *rsp_iocb) {
	struct scsi_cmnd *cmnd = lpfc_cmd->pCmd;
	struct fcp_rsp *fcprsp = lpfc_cmd->fcp_rsp;
	uint32_t resp_info = fcprsp->rspStatus2;
	uint32_t scsi_status = fcprsp->rspStatus3;
	uint32_t fcpi_parm = rsp_iocb->iocb.un.fcpi.fcpi_parm;
	struct lpfc_fast_path_event *fast_path_evt = NULL;
	struct lpfc_nodelist *pnode = lpfc_cmd->rdata->pnode;
	unsigned long flags;

	if (!pnode || !NLP_CHK_NODE_ACT(pnode))
		return;

	/* If there is queuefull or busy condition send a scsi event */
	if ((cmnd->result == SAM_STAT_TASK_SET_FULL) ||
		(cmnd->result == SAM_STAT_BUSY)) {
		fast_path_evt = lpfc_alloc_fast_evt(phba);
		if (!fast_path_evt)
			return;
		fast_path_evt->un.scsi_evt.event_type =
			FC_REG_SCSI_EVENT;
		fast_path_evt->un.scsi_evt.subcategory =
		(cmnd->result == SAM_STAT_TASK_SET_FULL) ?
		LPFC_EVENT_QFULL : LPFC_EVENT_DEVBSY;
		fast_path_evt->un.scsi_evt.lun = cmnd->device->lun;
		memcpy(&fast_path_evt->un.scsi_evt.wwpn,
			&pnode->nlp_portname, sizeof(struct lpfc_name));
		memcpy(&fast_path_evt->un.scsi_evt.wwnn,
			&pnode->nlp_nodename, sizeof(struct lpfc_name));
	} else if ((resp_info & SNS_LEN_VALID) && fcprsp->rspSnsLen &&
		((cmnd->cmnd[0] == READ_10) || (cmnd->cmnd[0] == WRITE_10))) {
		fast_path_evt = lpfc_alloc_fast_evt(phba);
		if (!fast_path_evt)
			return;
		fast_path_evt->un.check_cond_evt.scsi_event.event_type =
			FC_REG_SCSI_EVENT;
		fast_path_evt->un.check_cond_evt.scsi_event.subcategory =
			LPFC_EVENT_CHECK_COND;
		fast_path_evt->un.check_cond_evt.scsi_event.lun =
			cmnd->device->lun;
		memcpy(&fast_path_evt->un.check_cond_evt.scsi_event.wwpn,
			&pnode->nlp_portname, sizeof(struct lpfc_name));
		memcpy(&fast_path_evt->un.check_cond_evt.scsi_event.wwnn,
			&pnode->nlp_nodename, sizeof(struct lpfc_name));
		fast_path_evt->un.check_cond_evt.sense_key =
			cmnd->sense_buffer[2] & 0xf;
		fast_path_evt->un.check_cond_evt.asc = cmnd->sense_buffer[12];
		fast_path_evt->un.check_cond_evt.ascq = cmnd->sense_buffer[13];
	} else if ((cmnd->sc_data_direction == DMA_FROM_DEVICE) &&
		     fcpi_parm &&
		     ((be32_to_cpu(fcprsp->rspResId) != fcpi_parm) ||
			((scsi_status == SAM_STAT_GOOD) &&
			!(resp_info & (RESID_UNDER | RESID_OVER))))) {
		/*
		 * If status is good or resid does not match with fcp_param and
		 * there is valid fcpi_parm, then there is a read_check error
		 */
		fast_path_evt = lpfc_alloc_fast_evt(phba);
		if (!fast_path_evt)
			return;
		fast_path_evt->un.read_check_error.header.event_type =
			FC_REG_FABRIC_EVENT;
		fast_path_evt->un.read_check_error.header.subcategory =
			LPFC_EVENT_FCPRDCHKERR;
		memcpy(&fast_path_evt->un.read_check_error.header.wwpn,
			&pnode->nlp_portname, sizeof(struct lpfc_name));
		memcpy(&fast_path_evt->un.read_check_error.header.wwnn,
			&pnode->nlp_nodename, sizeof(struct lpfc_name));
		fast_path_evt->un.read_check_error.lun = cmnd->device->lun;
		fast_path_evt->un.read_check_error.opcode = cmnd->cmnd[0];
		fast_path_evt->un.read_check_error.fcpiparam =
			fcpi_parm;
	} else
		return;

	fast_path_evt->vport = vport;
	spin_lock_irqsave(&phba->hbalock, flags);
	list_add_tail(&fast_path_evt->work_evt.evt_listp, &phba->work_list);
	spin_unlock_irqrestore(&phba->hbalock, flags);
	lpfc_worker_wake_up(phba);
	return;
}

/**
 * lpfc_scsi_unprep_dma_buf - Un-map DMA mapping of SG-list for dev
 * @phba: The HBA for which this call is being executed.
 * @psb: The scsi buffer which is going to be un-mapped.
 *
 * This routine does DMA un-mapping of scatter gather list of scsi command
 * field of @lpfc_cmd for device with SLI-3 interface spec.
 **/
static void
lpfc_scsi_unprep_dma_buf(struct lpfc_hba *phba, struct lpfc_io_buf *psb)
{
	/*
	 * There are only two special cases to consider.  (1) the scsi command
	 * requested scatter-gather usage or (2) the scsi command allocated
	 * a request buffer, but did not request use_sg.  There is a third
	 * case, but it does not require resource deallocation.
	 */
	if (psb->seg_cnt > 0)
		scsi_dma_unmap(psb->pCmd);
	if (psb->prot_seg_cnt > 0)
		dma_unmap_sg(&phba->pcidev->dev, scsi_prot_sglist(psb->pCmd),
				scsi_prot_sg_count(psb->pCmd),
				psb->pCmd->sc_data_direction);
}

uint32_t
lpfc_edsm_process_inq_data_chg(struct lpfc_vport *vport,
			       struct lpfc_io_buf *lpfc_cmd)
{
	struct scsi_cmnd *cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcpcmd = lpfc_cmd->fcp_cmnd;
	struct lpfc_external_dif_support *dp = lpfc_cmd->edifp;
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *pnode;
	unsigned long flags;
	int rc;

	rdata = lpfc_cmd->rdata;
	pnode = rdata->pnode;

	spin_lock_irqsave(&vport->external_dif_lock, flags);
	if (dp == NULL) {
		dp = lpfc_external_dif_match(vport, cmnd->device->lun,
					     pnode->nlp_sid);
		if (dp == NULL) {
			spin_unlock_irqrestore(&vport->external_dif_lock,
					       flags);
			return DID_OK;
		}
	}
	rc = lpfc_issue_scsi_inquiry(vport, pnode, dp->lun);
	if (rc == 0)
		lpfc_edsm_set_state(vport, dp, LPFC_EDSM_PATH_CHECK);
	else
		lpfc_edsm_set_state(vport, dp, LPFC_EDSM_NEEDS_INQUIRY);
	spin_unlock_irqrestore(&vport->external_dif_lock, flags);
	dp->err3f_cnt++;

	lpfc_printf_vlog(vport, KERN_INFO, LOG_EDIF,
			 "0913 INQ Data Change: Data: "
			 "%x %x %x",
			 cmnd->cmnd[0],
			 be32_to_cpu(fcpcmd->fcpDl),
			 lpfc_cmd->cur_iocbq.sli4_xritag);

	/* Convert to retryable err */
	return DID_ERROR;
}

uint32_t
lpfc_edsm_process_data_phase(struct lpfc_vport *vport,
			     struct lpfc_io_buf *lpfc_cmd)
{
	struct scsi_cmnd *cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcpcmd = lpfc_cmd->fcp_cmnd;
	struct lpfc_external_dif_support *dp = lpfc_cmd->edifp;
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *pnode;
	unsigned long flags;
	int rc;

	rdata = lpfc_cmd->rdata;
	pnode = rdata->pnode;

	spin_lock_irqsave(&vport->external_dif_lock, flags);
	if (dp == NULL) {
		dp = lpfc_external_dif_match(vport, cmnd->device->lun,
					     pnode->nlp_sid);
		if (dp == NULL) {
			spin_unlock_irqrestore(&vport->external_dif_lock,
					       flags);
			return DID_OK;
		}
	}
	/*
	 * Extra check in case we dropped the
	 * LPFC_ASC_TARGET_CHANGED error.
	 */
	if (dp->state != LPFC_EDSM_PATH_CHECK &&
	    dp->state != LPFC_EDSM_PATH_STANDBY) {
		rc = lpfc_issue_scsi_inquiry(vport, pnode, dp->lun);
		if (rc == 0)
			lpfc_edsm_set_state(vport, dp, LPFC_EDSM_PATH_CHECK);
		else
			lpfc_edsm_set_state(vport, dp, LPFC_EDSM_NEEDS_INQUIRY);
	}
	spin_unlock_irqrestore(&vport->external_dif_lock, flags);
	dp->err4b_cnt++;

	lpfc_printf_vlog(vport, KERN_INFO, LOG_EDIF,
			 "0914 Data Phase Err: Data: "
			 "%x %x %x",
			 cmnd->cmnd[0],
			 be32_to_cpu(fcpcmd->fcpDl),
			 lpfc_cmd->cur_iocbq.sli4_xritag);

	/* Convert to retryable err */
	return DID_ERROR;
}

/**
 * lpfc_unblock_requests: allow further commands from being queued.
 * For single vport, just call scsi_unblock_requests on physical port.
 * For multiple vports sent scsi_unblock_requests for all the vports.
 */
void
lpfc_unblock_requests(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	struct Scsi_Host  *shost;
	int i;

	if (phba->sli_rev == LPFC_SLI_REV4 &&
	    !phba->sli4_hba.max_cfg_param.vpi_used) {
		shost = lpfc_shost_from_vport(phba->pport);
		scsi_unblock_requests(shost);
		return;
	}

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			shost = lpfc_shost_from_vport(vports[i]);
			scsi_unblock_requests(shost);
		}
	lpfc_destroy_vport_work_array(phba, vports);
}

/**
 * lpfc_block_requests: prevent further	commands from being queued.
 * For single vport, just call scsi_block_requests on physical port.
 * For multiple vports sent scsi_block_requests for all the vports.
 */
void
lpfc_block_requests(struct lpfc_hba *phba)
{
	struct lpfc_vport **vports;
	struct Scsi_Host  *shost;
	int i;

	if (atomic_read(&phba->cmf_stop_io))
		return;

	if (phba->sli_rev == LPFC_SLI_REV4 &&
	    !phba->sli4_hba.max_cfg_param.vpi_used) {
		shost = lpfc_shost_from_vport(phba->pport);
		scsi_block_requests(shost);
		return;
	}

	vports = lpfc_create_vport_work_array(phba);
	if (vports != NULL)
		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			shost = lpfc_shost_from_vport(vports[i]);
			scsi_block_requests(shost);
		}
	lpfc_destroy_vport_work_array(phba, vports);
}

/**
 * lpfc_update_cmf_cmpl - Adjust CMF counters for IO completion
 * @phba: The HBA for which this call is being executed.
 * @time: The latency of the IO that completed (in ns)
 * @size: The size of the IO that completed
 * @shost: SCSI host the IO completed on (NULL for a NVME IO)
 *
 * The routine adjusts the various Burst and Bandwidth counters used in
 * Congestion management and E2E. If time is set to LPFC_CGN_NOT_SENT,
 * that means the IO was never issued to the HBA, so this routine is
 * just being called to cleanup the counter from a previous
 * lpfc_update_cmf_cmd call.
 */
int
lpfc_update_cmf_cmpl(struct lpfc_hba *phba,
		     uint64_t time, uint32_t size, struct Scsi_Host *shost)
{
	struct lpfc_cgn_stat *cgs;

	if (time != LPFC_CGN_NOT_SENT) {
		/* lat is ns coming in, save latency in us */
		if (time < 1000)
			time = 1;
		else
			time = (time + 500) / 1000; /* round it */

		cgs = this_cpu_ptr(phba->cmf_stat);
		atomic64_add(size, &cgs->rcv_bytes);
		atomic64_add(time, &cgs->rx_latency);
		atomic_inc(&cgs->rx_io_cnt);
	}
	return 0;
}

/**
 * lpfc_update_cmf_cmd - Adjust CMF counters for IO submission
 * @phba: The HBA for which this call is being executed.
 * @size: The size of the IO that will be issued
 *
 * The routine adjusts the various Burst and Bandwidth counters used in
 * Congestion management and E2E.
 */
int
lpfc_update_cmf_cmd(struct lpfc_hba *phba, uint32_t size)
{
	uint64_t total;
	struct lpfc_cgn_stat *cgs;
	int cpu;

	/* At this point we are either LPFC_CFG_MANAGED or LPFC_CFG_MONITOR */
	if (phba->cmf_active_mode == LPFC_CFG_MANAGED &&
	    phba->cmf_max_bytes_per_interval) {
		total = 0;
		for_each_present_cpu(cpu) {
			cgs = per_cpu_ptr(phba->cmf_stat, cpu);
			total += atomic64_read(&cgs->total_bytes);
		}
		if (total >= phba->cmf_max_bytes_per_interval) {
			if (!atomic_xchg(&phba->cmf_bw_wait, 1)) {
				lpfc_block_requests(phba);
				phba->cmf_last_ts =
					lpfc_calc_cmf_latency(phba);
			}
			atomic_inc(&phba->cmf_busy);
			return -EBUSY;
		}
		if (size > atomic_read(&phba->rx_max_read_cnt))
			atomic_set(&phba->rx_max_read_cnt, size);
	}

	cgs = this_cpu_ptr(phba->cmf_stat);
	atomic64_add(size, &cgs->total_bytes);
	return 0;
}

/**
 * lpfc_handler_fcp_err - FCP response handler
 * @vport: The virtual port for which this call is being executed.
 * @lpfc_cmd: Pointer to lpfc_io_buf data structure.
 * @rsp_iocb: The response IOCB which contains FCP error.
 *
 * This routine is called to process response IOCB with status field
 * IOSTAT_FCP_RSP_ERROR. This routine sets result field of scsi command
 * based upon SCSI and FCP error.
 **/
static void
lpfc_handle_fcp_err(struct lpfc_vport *vport, struct lpfc_io_buf *lpfc_cmd,
		    struct lpfc_iocbq *rsp_iocb)
{
	struct lpfc_hba *phba = vport->phba;
	struct scsi_cmnd *cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcpcmd = lpfc_cmd->fcp_cmnd;
	struct fcp_rsp *fcprsp = lpfc_cmd->fcp_rsp;
	uint32_t fcpi_parm = rsp_iocb->iocb.un.fcpi.fcpi_parm;
	uint32_t resp_info = fcprsp->rspStatus2;
	uint32_t scsi_status = fcprsp->rspStatus3;
	struct lpfc_external_dif_support *dp;
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *pnode;
	uint32_t *lp;
	uint32_t host_status = DID_OK;
	uint32_t rsplen = 0;
	uint32_t fcpDl;
	uint32_t logit = LOG_FCP | LOG_FCP_ERROR;
	uint8_t  asc, ascq;

	/*
	 *  If this is a task management command, there is no
	 *  scsi packet associated with this lpfc_cmd.  The driver
	 *  consumes it.
	 */
	if (fcpcmd->fcpCntl2) {
		scsi_status = 0;
		goto out;
	}

	if (resp_info & RSP_LEN_VALID) {
		rsplen = be32_to_cpu(fcprsp->rspRspLen);
		if (rsplen != 0 && rsplen != 4 && rsplen != 8) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
					 "2719 Invalid response length: "
					 "tgt x%x lun x%llx cmnd x%x rsplen "
					 "x%x\n", cmnd->device->id,
					 cmnd->device->lun, cmnd->cmnd[0],
					 rsplen);
			host_status = DID_ERROR;
			goto out;
		}
		if (fcprsp->rspInfo3 != RSP_NO_FAILURE) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "2757 Protocol failure detected during "
				 "processing of FCP I/O op: "
				 "tgt x%x lun x%llx cmnd x%x rspInfo3 x%x\n",
				 cmnd->device->id,
				 cmnd->device->lun, cmnd->cmnd[0],
				 fcprsp->rspInfo3);
			host_status = DID_ERROR;
			goto out;
		}
	}

	if ((resp_info & SNS_LEN_VALID) && fcprsp->rspSnsLen) {
		uint32_t snslen = be32_to_cpu(fcprsp->rspSnsLen);
		if (snslen > SCSI_SENSE_BUFFERSIZE)
			snslen = SCSI_SENSE_BUFFERSIZE;

		if (resp_info & RSP_LEN_VALID)
		  rsplen = be32_to_cpu(fcprsp->rspRspLen);
		memcpy(cmnd->sense_buffer, &fcprsp->rspInfo0 + rsplen, snslen);
	}
	lp = (uint32_t *)cmnd->sense_buffer;

	/* special handling for under run conditions */
	if (!scsi_status && (resp_info & RESID_UNDER)) {
		/* don't log under runs if fcp set... */
		if (vport->cfg_log_verbose & LOG_FCP)
			logit = LOG_FCP_ERROR;
		/* unless operator says so */
		if (vport->cfg_log_verbose & LOG_FCP_UNDER)
			logit = LOG_FCP_UNDER;
	}

	lpfc_printf_vlog(vport, KERN_WARNING, logit,
			 "9024 FCP command x%x failed: x%x SNS x%x x%x "
			 "Data: x%x x%x x%x x%x x%x\n",
			 cmnd->cmnd[0], scsi_status,
			 be32_to_cpu(*lp), be32_to_cpu(*(lp + 3)), resp_info,
			 be32_to_cpu(fcprsp->rspResId),
			 be32_to_cpu(fcprsp->rspSnsLen),
			 be32_to_cpu(fcprsp->rspRspLen),
			 fcprsp->rspInfo3);

	scsi_set_resid(cmnd, 0);
	fcpDl = be32_to_cpu(fcpcmd->fcpDl);
	if (resp_info & RESID_UNDER) {
		scsi_set_resid(cmnd, be32_to_cpu(fcprsp->rspResId));

		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP_UNDER,
				 "9025 FCP Underrun, expected %d, "
				 "residual %d Data: x%x x%x x%x\n",
				 fcpDl,
				 scsi_get_resid(cmnd), fcpi_parm, cmnd->cmnd[0],
				 cmnd->underflow);

		/*
		 * If there is an under run, check if under run reported by
		 * storage array is same as the under run reported by HBA.
		 * If this is not same, there is a dropped frame.
		 */
		if (fcpi_parm && (scsi_get_resid(cmnd) != fcpi_parm)) {
			lpfc_printf_vlog(vport, KERN_WARNING,
					 LOG_FCP | LOG_FCP_ERROR,
					 "9026 FCP Read Check Error "
					 "and Underrun Data: x%x x%x x%x x%x\n",
					 fcpDl,
					 scsi_get_resid(cmnd), fcpi_parm,
					 cmnd->cmnd[0]);
			scsi_set_resid(cmnd, scsi_bufflen(cmnd));
			host_status = DID_ERROR;
		}
		/*
		 * The cmnd->underflow is the minimum number of bytes that must
		 * be transferred for this command.  Provided a sense condition
		 * is not present, make sure the actual amount transferred is at
		 * least the underflow value or fail.
		 */
		if (!(resp_info & SNS_LEN_VALID) &&
		    (scsi_status == SAM_STAT_GOOD) &&
		    (scsi_bufflen(cmnd) - scsi_get_resid(cmnd)
		     < cmnd->underflow)) {
			lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
					 "9027 FCP command x%x residual "
					 "underrun converted to error "
					 "Data: x%x x%x x%x\n",
					 cmnd->cmnd[0], scsi_bufflen(cmnd),
					 scsi_get_resid(cmnd), cmnd->underflow);
			host_status = DID_ERROR;
		}
	} else if (resp_info & RESID_OVER) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
				 "9028 FCP command x%x residual overrun error. "
				 "Data: x%x x%x\n", cmnd->cmnd[0],
				 scsi_bufflen(cmnd), scsi_get_resid(cmnd));
		host_status = DID_ERROR;

	/*
	 * Check SLI validation that all the transfer was actually done
	 * (fcpi_parm should be zero). Apply check only to reads.
	 */
	} else if (fcpi_parm) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP | LOG_FCP_ERROR,
				 "9029 FCP %s Check Error xri x%x  Data: "
				 "x%x x%x x%x x%x x%x\n",
				 ((cmnd->sc_data_direction == DMA_FROM_DEVICE) ?
				 "Read" : "Write"),
				 ((phba->sli_rev == LPFC_SLI_REV4) ?
				 lpfc_cmd->cur_iocbq.sli4_xritag :
				 rsp_iocb->iocb.ulpContext),
				 fcpDl, be32_to_cpu(fcprsp->rspResId),
				 fcpi_parm, cmnd->cmnd[0], scsi_status);

		/* There is some issue with the LPe12000 that causes it
		 * to miscalculate the fcpi_parm and falsely trip this
		 * recovery logic.  Detect this case and don't error when true.
		 */
		if (fcpi_parm > fcpDl)
			goto out;

		switch (scsi_status) {
		case SAM_STAT_GOOD:
		case SAM_STAT_CHECK_CONDITION:
			/* Fabric dropped a data frame. Fail any successful
			 * command in which we detected dropped frames.
			 * A status of good or some check conditions could
			 * be considered a successful command.
			 */
			host_status = DID_ERROR;
			break;
		}
		scsi_set_resid(cmnd, scsi_bufflen(cmnd));
	}

out:
	rdata = lpfc_cmd->rdata;
	pnode = rdata->pnode;
	if (pnode && (pnode->nlp_flag & NLP_EXTERNAL_DIF)) {
		/* Is this an External DIF array? */
		dp = lpfc_cmd->edifp;
		asc = cmnd->sense_buffer[12];
		ascq = cmnd->sense_buffer[13];
		/* Check for LOGICAL BLOCK GUARD CHECK / REF TAG failed */
		if (scsi_status == SAM_STAT_CHECK_CONDITION) {
			if (dp &&
			    (asc == LPFC_ASC_DIF_ERR) &&
			    ((ascq == LPFC_ASCQ_GUARD_CHECK) ||
			    (ascq == LPFC_ASCQ_REFTAG_CHECK))) {
				host_status = DID_ERROR;
				scsi_status = SAM_STAT_GOOD;
				/* Convert to retryable err */
			}

			/*
			 * A Operation Condition Change error on an External DIF
			 * device indicates its in transition from one mode to
			 * another.
			 */
			if ((asc == LPFC_ASC_TARGET_CHANGED) &&
			    (ascq == LPFC_ASCQ_INQUIRY_DATA)) {
				host_status =
					lpfc_edsm_process_inq_data_chg(
						vport, lpfc_cmd);
				if (host_status == DID_ERROR)
					scsi_status = SAM_STAT_GOOD;
			}
			/*
			 * A Data Phase error on External DIF device indicates
			 * the IO was sent in the wrong mode.
			 */
			if ((asc == LPFC_ASC_DATA_PHASE_ERR) &&
			    (ascq == LPFC_ASCQ_EDIF_MODE)) {
				host_status =
					lpfc_edsm_process_data_phase(
						vport, lpfc_cmd);
				if (host_status == DID_ERROR)
					scsi_status = SAM_STAT_GOOD;
			}

			lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
					 "3031 FCP command x%x force ERROR "
					 "asc/ascq:x%x/x%x "
					 "xri x%x lba x%llx stat %d:%d\n",
					 cmnd->cmnd[0], asc, ascq,
					 lpfc_cmd->cur_iocbq.sli4_xritag,
					 (unsigned long long)scsi_get_lba(cmnd),
					 host_status, scsi_status);
			/*
			 * If the scsi_status has been changed to SAM_STAT_GOOD,
			 * we need to massage the cmnd and remove all trace
			 * of the check condition.
			 */
			if (scsi_status == SAM_STAT_GOOD) {
				/* Cleanup FCP response */
				fcprsp->rspStatus2 &=
					~(SNS_LEN_VALID | RESID_UNDER);
				fcprsp->rspSnsLen = 0;

				/* resid must be 0 */
				scsi_set_resid(cmnd, 0);

				/* Get rid of any unwanted sense data */
				memset(cmnd->sense_buffer, 0,
				       SCSI_SENSE_BUFFERSIZE);
			}

		}
	}
	cmnd->result = host_status << 16 | scsi_status;
	lpfc_send_scsi_error_event(vport->phba, vport, lpfc_cmd, rsp_iocb);
}

/**
 * lpfc_scsi_cmd_iocb_cmpl - Scsi cmnd IOCB completion routine
 * @phba: The Hba for which this call is being executed.
 * @pIocbIn: The command IOCBQ for the scsi cmnd.
 * @pIocbOut: The response IOCBQ for the scsi cmnd.
 *
 * This routine assigns scsi command result by looking into response IOCB
 * status field appropriately. This routine handles QUEUE FULL condition as
 * well by ramping down device queue depth.
 **/
static void
lpfc_scsi_cmd_iocb_cmpl(struct lpfc_hba *phba, struct lpfc_iocbq *pIocbIn,
			struct lpfc_iocbq *pIocbOut)
{
	struct lpfc_io_buf *lpfc_cmd =
		(struct lpfc_io_buf *) pIocbIn->context1;
	struct lpfc_vport      *vport = pIocbIn->vport;
	struct lpfc_rport_data *rdata = lpfc_cmd->rdata;
	struct lpfc_nodelist *pnode = rdata->pnode;
	struct scsi_cmnd *cmd;
	unsigned long flags;
	struct lpfc_fast_path_event *fast_path_evt;
	struct Scsi_Host *shost;
	int idx;
	uint32_t logit = LOG_FCP;
	uint32_t lat;

	/* Guard against abort handler being called at same time */
	spin_lock(&lpfc_cmd->buf_lock);

	/* Sanity check on return of outstanding command */
	cmd = lpfc_cmd->pCmd;
	if (!cmd || !phba) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "2621 IO completion: Not an active IO\n");
		spin_unlock(&lpfc_cmd->buf_lock);
		return;
	}

	idx = lpfc_cmd->cur_iocbq.hba_wqidx;
	if (phba->sli4_hba.hdwq)
		phba->sli4_hba.hdwq[idx].scsi_cstat.io_cmpls++;

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (unlikely(phba->hdwqstat_on & LPFC_CHECK_SCSI_IO))
		this_cpu_inc(phba->sli4_hba.c_stat->cmpl_io);
#endif
	shost = cmd->device->host;

	lpfc_cmd->result = (pIocbOut->iocb.un.ulpWord[4] & IOERR_PARAM_MASK);
	lpfc_cmd->status = pIocbOut->iocb.ulpStatus;
	/* pick up SLI4 exhange busy status from HBA */
	if (pIocbOut->iocb_flag & LPFC_EXCHANGE_BUSY)
		lpfc_cmd->flags |= LPFC_SBUF_XBUSY;
	else
		lpfc_cmd->flags &= ~LPFC_SBUF_XBUSY;

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (lpfc_cmd->prot_data_type) {
		struct scsi_dif_tuple *src = NULL;

		src =  (struct scsi_dif_tuple *)lpfc_cmd->prot_data_segment;
		/*
		 * Used to restore any changes to protection
		 * data for error injection.
		 */
		switch (lpfc_cmd->prot_data_type) {
		case LPFC_INJERR_REFTAG:
			src->ref_tag =
				lpfc_cmd->prot_data;
			break;
		case LPFC_INJERR_APPTAG:
			src->app_tag =
				(uint16_t)lpfc_cmd->prot_data;
			break;
		case LPFC_INJERR_GUARD:
			src->guard_tag =
				(uint16_t)lpfc_cmd->prot_data;
			break;
		default:
			break;
		}

		lpfc_cmd->prot_data = 0;
		lpfc_cmd->prot_data_type = 0;
		lpfc_cmd->prot_data_segment = NULL;
	}
#endif

	if (unlikely(lpfc_cmd->status)) {
		if (lpfc_cmd->status == IOSTAT_LOCAL_REJECT &&
		    (lpfc_cmd->result & IOERR_DRVR_MASK))
			lpfc_cmd->status = IOSTAT_DRIVER_REJECT;
		else if (lpfc_cmd->status >= IOSTAT_CNT)
			lpfc_cmd->status = IOSTAT_DEFAULT;
		if (lpfc_cmd->status == IOSTAT_FCP_RSP_ERROR &&
		    !lpfc_cmd->fcp_rsp->rspStatus3 &&
		    (lpfc_cmd->fcp_rsp->rspStatus2 & RESID_UNDER) &&
		    !(vport->cfg_log_verbose & LOG_FCP_UNDER))
			logit = 0;
		else
			logit = LOG_FCP | LOG_FCP_UNDER;
		lpfc_printf_vlog(vport, KERN_WARNING, logit,
			 "9030 FCP cmd x%x failed <%d/%lld> "
			 "status: x%x result: x%x "
			 "sid: x%x did: x%x oxid: x%x "
			 "Data: x%x x%x\n",
			 cmd->cmnd[0],
			 cmd->device ? cmd->device->id : 0xffff,
			 cmd->device ? cmd->device->lun : 0xffff,
			 lpfc_cmd->status, lpfc_cmd->result,
			 vport->fc_myDID,
			 (pnode) ? pnode->nlp_DID : 0,
			 phba->sli_rev == LPFC_SLI_REV4 ?
			     lpfc_cmd->cur_iocbq.sli4_xritag : 0xffff,
			 pIocbOut->iocb.ulpContext,
			 lpfc_cmd->cur_iocbq.iocb.ulpIoTag);

		switch (lpfc_cmd->status) {
		case IOSTAT_FCP_RSP_ERROR:
			/* Call FCP RSP handler to determine result */
			lpfc_handle_fcp_err(vport, lpfc_cmd, pIocbOut);
			break;
		case IOSTAT_NPORT_BSY:
		case IOSTAT_FABRIC_BSY:
			cmd->result = DID_TRANSPORT_DISRUPTED << 16;
			fast_path_evt = lpfc_alloc_fast_evt(phba);
			if (!fast_path_evt)
				break;
			fast_path_evt->un.fabric_evt.event_type =
				FC_REG_FABRIC_EVENT;
			fast_path_evt->un.fabric_evt.subcategory =
				(lpfc_cmd->status == IOSTAT_NPORT_BSY) ?
				LPFC_EVENT_PORT_BUSY : LPFC_EVENT_FABRIC_BUSY;
			if (pnode && NLP_CHK_NODE_ACT(pnode)) {
				memcpy(&fast_path_evt->un.fabric_evt.wwpn,
					&pnode->nlp_portname,
					sizeof(struct lpfc_name));
				memcpy(&fast_path_evt->un.fabric_evt.wwnn,
					&pnode->nlp_nodename,
					sizeof(struct lpfc_name));
			}
			fast_path_evt->vport = vport;
			fast_path_evt->work_evt.evt =
				LPFC_EVT_FASTPATH_MGMT_EVT;
			spin_lock_irqsave(&phba->hbalock, flags);
			list_add_tail(&fast_path_evt->work_evt.evt_listp,
				&phba->work_list);
			spin_unlock_irqrestore(&phba->hbalock, flags);
			lpfc_worker_wake_up(phba);
			break;
		case IOSTAT_LOCAL_REJECT:
		case IOSTAT_REMOTE_STOP:
			if (lpfc_cmd->result == IOERR_ELXSEC_KEY_UNWRAP_ERROR ||
			    lpfc_cmd->result ==
					IOERR_ELXSEC_KEY_UNWRAP_COMPARE_ERROR ||
			    lpfc_cmd->result == IOERR_ELXSEC_CRYPTO_ERROR ||
			    lpfc_cmd->result ==
					IOERR_ELXSEC_CRYPTO_COMPARE_ERROR) {
				cmd->result = DID_NO_CONNECT << 16;
				break;
			}
			if (lpfc_cmd->result == IOERR_INVALID_RPI ||
			    lpfc_cmd->result == IOERR_NO_RESOURCES ||
			    lpfc_cmd->result == IOERR_ABORT_REQUESTED ||
			    lpfc_cmd->result == IOERR_SLER_CMD_RCV_FAILURE) {
				cmd->result = DID_REQUEUE << 16;
				break;
			}
			if ((lpfc_cmd->result == IOERR_RX_DMA_FAILED ||
			     lpfc_cmd->result == IOERR_TX_DMA_FAILED) &&
			     pIocbOut->iocb.unsli3.sli3_bg.bgstat) {
				if (lpfc_scsi_get_prot_op(cmd) !=
				    SCSI_PROT_NORMAL) {
					/*
					 * This is a response for a BG enabled
					 * cmd. Parse BG error
					 */
					lpfc_parse_bg_err(phba, lpfc_cmd,
							pIocbOut);
					break;
				} else {
					lpfc_printf_vlog(vport, KERN_WARNING,
							LOG_BG,
							"9031 non-zero BGSTAT "
							"on unprotected cmd\n");
				}
			}
			if ((lpfc_cmd->status == IOSTAT_REMOTE_STOP)
				&& (phba->sli_rev == LPFC_SLI_REV4)
				&& (pnode && NLP_CHK_NODE_ACT(pnode))) {
				/* This IO was aborted by the target, we don't
				 * know the rxid and because we did not send the
				 * ABTS we cannot generate and RRQ.
				 */
				lpfc_set_rrq_active(phba, pnode,
					lpfc_cmd->cur_iocbq.sli4_lxritag,
					0, 0);
			}
			/* fall through */
		default:
			cmd->result = DID_ERROR << 16;
			break;
		}

		if (!pnode || !NLP_CHK_NODE_ACT(pnode)
		    || (pnode->nlp_state != NLP_STE_MAPPED_NODE))
			cmd->result = DID_TRANSPORT_DISRUPTED << 16 |
				      SAM_STAT_BUSY;
	} else
		cmd->result = DID_OK << 16;

	if (cmd->result || lpfc_cmd->fcp_rsp->rspSnsLen) {
		uint32_t *lp = (uint32_t *)cmd->sense_buffer;

		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
				 "0710 Iodone <%d/%llu> cmd x%px, error "
				 "x%x SNS x%x x%x Data: x%x x%x\n",
				 cmd->device->id, cmd->device->lun, cmd,
				 cmd->result, *lp, *(lp + 3), cmd->retries,
				 scsi_get_resid(cmd));
	}

	lpfc_update_stats(vport, lpfc_cmd);

	if (vport->cfg_max_scsicmpl_time &&
	   time_after(jiffies, lpfc_cmd->start_time +
		msecs_to_jiffies(vport->cfg_max_scsicmpl_time))) {
		spin_lock_irqsave(shost->host_lock, flags);
		if (pnode && NLP_CHK_NODE_ACT(pnode)) {
			if (pnode->cmd_qdepth >
				atomic_read(&pnode->cmd_pending) &&
				(atomic_read(&pnode->cmd_pending) >
				LPFC_MIN_TGT_QDEPTH) &&
				((cmd->cmnd[0] == READ_10) ||
				(cmd->cmnd[0] == WRITE_10)))
				pnode->cmd_qdepth =
					atomic_read(&pnode->cmd_pending);

			pnode->last_change_time = jiffies;
		}
		spin_unlock_irqrestore(shost->host_lock, flags);
	}
	lpfc_scsi_unprep_dma_buf(phba, lpfc_cmd);

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (lpfc_cmd->ts_cmd_start) {
		lpfc_cmd->ts_isr_cmpl = pIocbIn->isr_timestamp;
		lpfc_cmd->ts_data_io = ktime_get_ns();
		phba->ktime_last_cmd = lpfc_cmd->ts_data_io;
		lpfc_io_ktime(phba, lpfc_cmd);
	}
#endif
	lpfc_cmd->pCmd = NULL;
	spin_unlock(&lpfc_cmd->buf_lock);

	/* Check if IO qualified for CMF */
	if (phba->cmf_active_mode != LPFC_CFG_OFF &&
	    cmd->sc_data_direction == DMA_FROM_DEVICE &&
	    (scsi_sg_count(cmd))) {
		/* Used when calculating average latency */
		lat = ktime_get_ns() - lpfc_cmd->rx_cmd_start;
		lpfc_update_cmf_cmpl(phba, lat, scsi_bufflen(cmd), shost);
	}

	/* The sdev is not guaranteed to be valid post scsi_done upcall. */
	cmd->scsi_done(cmd);

	/*
	 * If there is an abort thread waiting for command completion
	 * wake up the thread.
	 */
	spin_lock(&lpfc_cmd->buf_lock);
	lpfc_cmd->cur_iocbq.iocb_flag &= ~LPFC_DRIVER_ABORTED;
	if (lpfc_cmd->waitq)
		wake_up(lpfc_cmd->waitq);
	spin_unlock(&lpfc_cmd->buf_lock);

	lpfc_release_scsi_buf(phba, lpfc_cmd);
}

/**
 * lpfc_scsi_prep_cmnd - Wrapper func for convert scsi cmnd to FCP info unit
 * @vport: The virtual port for which this call is being executed.
 * @lpfc_cmd: The scsi command which needs to send.
 * @pnode: Pointer to lpfc_nodelist.
 *
 * This routine initializes fcp_cmnd and iocb data structure from scsi command
 * to transfer for device with SLI3 interface spec.
 **/
static int
lpfc_scsi_prep_cmnd(struct lpfc_vport *vport, struct lpfc_io_buf *lpfc_cmd,
		    struct lpfc_nodelist *pnode)
{
	struct lpfc_hba *phba = vport->phba;
	struct scsi_cmnd *scsi_cmnd = lpfc_cmd->pCmd;
	struct fcp_cmnd *fcp_cmnd = lpfc_cmd->fcp_cmnd;
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	struct lpfc_iocbq *piocbq = &(lpfc_cmd->cur_iocbq);
	struct lpfc_sli4_hdw_queue *hdwq = NULL;
	int datadir = scsi_cmnd->sc_data_direction;
	int rc, idx;
	uint8_t *ptr;
	bool sli4;
	bool scan_io;
	uint8_t tmo;
	uint32_t fcpdl;
#ifndef NO_APEX
	uint8_t fcp_priority = 0;
	uint8_t pri;
	uint32_t lun_index;
#endif

	if (!pnode || !NLP_CHK_NODE_ACT(pnode))
		return 0;

	lpfc_cmd->fcp_rsp->rspSnsLen = 0;
	/* clear task management bits */
	lpfc_cmd->fcp_cmnd->fcpCntl2 = 0;

	int_to_scsilun(lpfc_cmd->pCmd->device->lun,
			&lpfc_cmd->fcp_cmnd->fcp_lun);

	ptr = &fcp_cmnd->fcpCdb[0];
	memcpy(ptr, scsi_cmnd->cmnd, scsi_cmnd->cmd_len);
	if (scsi_cmnd->cmd_len < LPFC_FCP_CDB_LEN) {
		ptr += scsi_cmnd->cmd_len;
		memset(ptr, 0, (LPFC_FCP_CDB_LEN - scsi_cmnd->cmd_len));
	}

	/* Check if we want to make this IO an External DIF device */
	if (vport->phba->cfg_external_dif &&
	    !(pnode->nlp_flag & NLP_SKIP_EXT_DIF)) {
		rc = lpfc_external_dif(vport, lpfc_cmd, pnode,
				       &fcp_cmnd->fcpCdb[0]);
		if (rc)
			return rc;
	}

	scan_io = (scsi_cmnd->cmnd[0] == 0 || scsi_cmnd->cmnd[0] == 0x12 ||
		   scsi_cmnd->cmnd[0]  == 0xa0);
	fcp_cmnd->fcpCntl1 = SIMPLE_Q;
#ifndef NO_APEX
	/*
	 * Get the fcp priority from the table and unpack it. Each byte
	 * contains the priority for two luns. Low nibble has the even lun.
	 * Upper nibble has the odd lun.
	 */
	if (vport->phba->cfg_enable_fcp_priority &&
	    (scsi_cmnd->device->lun < MAX_FCP_PRI_LUN)) {
		lun_index = scsi_cmnd->device->lun >> 1;
		pri = pnode->fcp_priority[lun_index];
		if (scsi_cmnd->device->lun & 1)
			fcp_priority = (pri & 0xf0) >> 4;
		else
			fcp_priority = pri & 0xf;
	} else {
		fcp_priority = 0;
	}
	fcp_cmnd->fcpCntl1  |= (fcp_priority & 0xf) << 3;
#endif

	sli4 = (phba->sli_rev == LPFC_SLI_REV4);
	piocbq->iocb.un.fcpi.fcpi_XRdy = 0;
	idx = lpfc_cmd->hdwq_no;
	if (phba->sli4_hba.hdwq)
		hdwq = &phba->sli4_hba.hdwq[idx];

	/*
	 * There are three possibilities here - use scatter-gather segment, use
	 * the single mapping, or neither.  Start the lpfc command prep by
	 * bumping the bpl beyond the fcp_cmnd and fcp_rsp regions to the first
	 * data bde entry.
	 */
	if (scsi_sg_count(scsi_cmnd)) {
		if (datadir == DMA_TO_DEVICE) {
			iocb_cmd->ulpCommand = CMD_FCP_IWRITE64_CR;
			iocb_cmd->ulpPU = PARM_READ_CHECK;
			if (pnode->first_burst &&
			    (pnode->nlp_flag & NLP_FIRSTBURST)) {
				u32 xrdy_len;
				fcpdl = scsi_bufflen(scsi_cmnd);
				xrdy_len = min(fcpdl, pnode->first_burst);
				piocbq->iocb.un.fcpi.fcpi_XRdy = xrdy_len;
			}
			fcp_cmnd->fcpCntl3 = WRITE_DATA;
			if (hdwq)
				hdwq->scsi_cstat.output_requests++;
		} else {
			iocb_cmd->ulpCommand = CMD_FCP_IREAD64_CR;
			iocb_cmd->ulpPU = PARM_READ_CHECK;
			fcp_cmnd->fcpCntl3 = READ_DATA;
			if (hdwq)
				hdwq->scsi_cstat.input_requests++;
		}
	} else {
		iocb_cmd->ulpCommand = CMD_FCP_ICMND64_CR;
		iocb_cmd->un.fcpi.fcpi_parm = 0;
		iocb_cmd->ulpPU = 0;
		fcp_cmnd->fcpCntl3 = 0;
		if (hdwq)
			hdwq->scsi_cstat.control_requests++;
	}
	/*
	 * Finish initializing those IOCB fields that are independent
	 * of the scsi_cmnd request_buffer
	 */
	piocbq->iocb.ulpContext = pnode->nlp_rpi;
	if (sli4)
		piocbq->iocb.ulpContext =
		  phba->sli4_hba.rpi_ids[pnode->nlp_rpi];
	if (pnode->nlp_fcp_info & NLP_FCP_2_DEVICE)
		piocbq->iocb.ulpFCP2Rcvy = 1;
	else
		piocbq->iocb.ulpFCP2Rcvy = 0;

	piocbq->iocb.ulpClass = (pnode->nlp_fcp_info & 0x0f);
	piocbq->context1  = lpfc_cmd;
	if (piocbq->iocb_cmpl == NULL)
		piocbq->iocb_cmpl = lpfc_scsi_cmd_iocb_cmpl;
	if (scan_io) {
		/*
		 * timeout the command at the firmware level.
		 * The driver will not get the XRI back if
		 * this is a tur from the SCAN.
		 */
		if (scsi_cmnd->request->timeout > 0) {
			tmo  = (uint8_t)
				(scsi_cmnd->request->timeout / 1000) + 2;
			if (tmo == 0  || tmo > 60)
				tmo = 30;
		} else {
			tmo = 30;
		}
		piocbq->iocb.ulpTimeout = tmo;
	} else {
		piocbq->iocb.ulpTimeout = lpfc_cmd->timeout;
	}
	piocbq->vport = vport;
	return 0;
}

/**
 * lpfc_scsi_prep_task_mgmt_cmd - Convert SLI3 scsi TM cmd to FCP info unit
 * @vport: The virtual port for which this call is being executed.
 * @lpfc_cmd: Pointer to lpfc_io_buf data structure.
 * @lun: Logical unit number.
 * @task_mgmt_cmd: SCSI task management command.
 *
 * This routine creates FCP information unit corresponding to @task_mgmt_cmd
 * for device with SLI-3 interface spec.
 *
 * Return codes:
 *   0 - Error
 *   1 - Success
 **/
static int
lpfc_scsi_prep_task_mgmt_cmd(struct lpfc_vport *vport,
			     struct lpfc_io_buf *lpfc_cmd,
			     uint64_t lun,
			     uint8_t task_mgmt_cmd)
{
	struct lpfc_iocbq *piocbq;
	IOCB_t *piocb;
	struct fcp_cmnd *fcp_cmnd;
	struct lpfc_rport_data *rdata = lpfc_cmd->rdata;
	struct lpfc_nodelist *ndlp = rdata->pnode;

	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp) ||
	    ndlp->nlp_state != NLP_STE_MAPPED_NODE)
		return 0;

	piocbq = &(lpfc_cmd->cur_iocbq);
	piocbq->vport = vport;

	piocb = &piocbq->iocb;

	fcp_cmnd = lpfc_cmd->fcp_cmnd;
	/* Clear out any old data in the FCP command area */
	memset(fcp_cmnd, 0, sizeof(struct fcp_cmnd));
	int_to_scsilun(lun, &fcp_cmnd->fcp_lun);
	fcp_cmnd->fcpCntl2 = task_mgmt_cmd;
	if (vport->phba->sli_rev == 3 &&
	    !(vport->phba->sli3_options & LPFC_SLI3_BG_ENABLED))
		lpfc_fcpcmd_to_iocb(piocb->unsli3.fcp_ext.icd, fcp_cmnd);
	piocb->ulpCommand = CMD_FCP_ICMND64_CR;
	piocb->ulpContext = ndlp->nlp_rpi;
	if (vport->phba->sli_rev == LPFC_SLI_REV4) {
		piocb->ulpContext =
		  vport->phba->sli4_hba.rpi_ids[ndlp->nlp_rpi];
	}
	piocb->ulpFCP2Rcvy = (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE) ? 1 : 0;
	piocb->ulpClass = (ndlp->nlp_fcp_info & 0x0f);
	piocb->ulpPU = 0;
	piocb->un.fcpi.fcpi_parm = 0;

	/* ulpTimeout is only one byte */
	if (lpfc_cmd->timeout > 0xff) {
		/*
		 * Do not timeout the command at the firmware level.
		 * The driver will provide the timeout mechanism.
		 */
		piocb->ulpTimeout = 0;
	} else
		piocb->ulpTimeout = lpfc_cmd->timeout;

	if (vport->phba->sli_rev == LPFC_SLI_REV4)
		lpfc_sli4_set_rsp_sgl_last(vport->phba, lpfc_cmd);

	return 1;
}

/**
 * lpfc_scsi_api_table_setup - Set up scsi api function jump table
 * @phba: The hba struct for which this call is being executed.
 * @dev_grp: The HBA PCI-Device group number.
 *
 * This routine sets up the SCSI interface API function jump table in @phba
 * struct.
 * Returns: 0 - success, -ENODEV - failure.
 **/
int
lpfc_scsi_api_table_setup(struct lpfc_hba *phba, uint8_t dev_grp)
{

	phba->lpfc_scsi_unprep_dma_buf = lpfc_scsi_unprep_dma_buf;
	phba->lpfc_scsi_prep_cmnd = lpfc_scsi_prep_cmnd;

	switch (dev_grp) {
	case LPFC_PCI_DEV_LP:
		phba->lpfc_scsi_prep_dma_buf = lpfc_scsi_prep_dma_buf_s3;
		phba->lpfc_bg_scsi_prep_dma_buf = lpfc_bg_scsi_prep_dma_buf_s3;
		phba->lpfc_release_scsi_buf = lpfc_release_scsi_buf_s3;
		phba->lpfc_get_scsi_buf = lpfc_get_scsi_buf_s3;
		break;
	case LPFC_PCI_DEV_OC:
		phba->lpfc_scsi_prep_dma_buf = lpfc_scsi_prep_dma_buf_s4;
		phba->lpfc_bg_scsi_prep_dma_buf = lpfc_bg_scsi_prep_dma_buf_s4;
		phba->lpfc_release_scsi_buf = lpfc_release_scsi_buf_s4;
		phba->lpfc_get_scsi_buf = lpfc_get_scsi_buf_s4;
		break;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"1418 Invalid HBA PCI-device group: 0x%x\n",
				dev_grp);
		return -ENODEV;
		break;
	}
	phba->lpfc_rampdown_queue_depth = lpfc_rampdown_queue_depth;
	phba->lpfc_scsi_cmd_iocb_cmpl = lpfc_scsi_cmd_iocb_cmpl;
	return 0;
}

/**
 * lpfc_taskmgmt_def_cmpl - IOCB completion routine for task management command
 * @phba: The Hba for which this call is being executed.
 * @cmdiocbq: Pointer to lpfc_iocbq data structure.
 * @rspiocbq: Pointer to lpfc_iocbq data structure.
 *
 * This routine is IOCB completion routine for device reset and target reset
 * routine. This routine release scsi buffer associated with lpfc_cmd.
 **/
static void
lpfc_tskmgmt_def_cmpl(struct lpfc_hba *phba,
			struct lpfc_iocbq *cmdiocbq,
			struct lpfc_iocbq *rspiocbq)
{
	struct lpfc_io_buf *lpfc_cmd =
		(struct lpfc_io_buf *) cmdiocbq->context1;
	if (lpfc_cmd)
		lpfc_release_scsi_buf(phba, lpfc_cmd);
	return;
}

/**
 * lpfc_check_pci_resettable - Walks list of devices on pci_dev's bus to check
 *                             if issuing a pci_bus_reset is possibly unsafe
 * @phba: lpfc_hba pointer.
 *
 * Description:
 * Walks the bus_list to ensure only PCI devices with Emulex
 * vendor id, device ids that support hot reset, and only one occurrence
 * of function 0.
 *
 * Returns:
 * -EBADSLT,  detected invalid device
 *      0,    successful
 */
int
lpfc_check_pci_resettable(struct lpfc_hba *phba)
{
	const struct pci_dev *pdev = phba->pcidev;
	struct pci_dev *ptr = NULL;
	u8 counter = 0;

	/* Walk the list of devices on the pci_dev's bus */
	list_for_each_entry(ptr, &pdev->bus->devices, bus_list) {
		/* Check for Emulex Vendor ID */
		if (ptr->vendor != PCI_VENDOR_ID_EMULEX) {
			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"8346 Non-Emulex vendor found: "
					"0x%04x\n", ptr->vendor);
			return -EBADSLT;
		}

		/* Check for valid Emulex Device ID */
		switch (ptr->device) {
		case PCI_DEVICE_ID_LANCER_FC:
		case PCI_DEVICE_ID_LANCER_G6_FC:
		case PCI_DEVICE_ID_LANCER_G7_FC:
			break;
		default:
			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"8347 Invalid device found: "
					"0x%04x\n", ptr->device);
			return -EBADSLT;
		}

		/* Check for only one function 0 ID to ensure only one HBA on
		 * secondary bus
		 */
		if (ptr->devfn == 0) {
			if (++counter > 1) {
				lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
						"8348 More than one device on "
						"secondary bus found\n");
				return -EBADSLT;
			}
		}
	}

	return 0;
}

/**
 * lpfc_info - Info entry point of scsi_host_template data structure
 * @host: The scsi host for which this call is being executed.
 *
 * This routine provides module information about hba.
 *
 * Reutrn code:
 *   Pointer to char - Success.
 **/
const char *
lpfc_info(struct Scsi_Host *host)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) host->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int link_speed = 0;
	static char lpfcinfobuf[384];
	char tmp[384] = {0};

	memset(lpfcinfobuf, 0, sizeof(lpfcinfobuf));
	if (phba && phba->pcidev){
		/* Model Description */
		scnprintf(tmp, sizeof(tmp), phba->ModelDesc);
		if (strlcat(lpfcinfobuf, tmp, sizeof(lpfcinfobuf)) >=
		    sizeof(lpfcinfobuf))
			goto buffer_done;

		/* PCI Info */
		scnprintf(tmp, sizeof(tmp),
			  " on PCI bus %02x device %02x irq %d",
			  phba->pcidev->bus->number, phba->pcidev->devfn,
			  phba->pcidev->irq);
		if (strlcat(lpfcinfobuf, tmp, sizeof(lpfcinfobuf)) >=
		    sizeof(lpfcinfobuf))
			goto buffer_done;

		/* Port Number */
		if (phba->Port[0]) {
			scnprintf(tmp, sizeof(tmp), " port %s", phba->Port);
			if (strlcat(lpfcinfobuf, tmp, sizeof(lpfcinfobuf)) >=
			    sizeof(lpfcinfobuf))
				goto buffer_done;
		}

		/* Link Speed */
		link_speed = lpfc_sli_port_speed_get(phba);
		if (link_speed != 0) {
			scnprintf(tmp, sizeof(tmp),
				  " Logical Link Speed: %d Mbps", link_speed);
			if (strlcat(lpfcinfobuf, tmp, sizeof(lpfcinfobuf)) >=
			    sizeof(lpfcinfobuf))
				goto buffer_done;
		}

		/* PCI resettable */
		if (!lpfc_check_pci_resettable(phba)) {
			scnprintf(tmp, sizeof(tmp), " PCI resettable");
			strlcat(lpfcinfobuf, tmp, sizeof(lpfcinfobuf));
		}
	}

buffer_done:
	return lpfcinfobuf;
}

/**
 * lpfc_poll_rearm_time - Routine to modify fcp_poll timer of hba
 * @phba: The Hba for which this call is being executed.
 *
 * This routine modifies fcp_poll_timer  field of @phba by cfg_poll_tmo.
 * The default value of cfg_poll_tmo is 10 milliseconds.
 **/
static __inline__ void lpfc_poll_rearm_timer(struct lpfc_hba * phba)
{
	unsigned long  poll_tmo_expires =
		(jiffies + msecs_to_jiffies(phba->cfg_poll_tmo));

	if (!list_empty(&phba->sli.sli3_ring[LPFC_FCP_RING].txcmplq))
		mod_timer(&phba->fcp_poll_timer,
			  poll_tmo_expires);
}

/**
 * lpfc_poll_start_timer - Routine to start fcp_poll_timer of HBA
 * @phba: The Hba for which this call is being executed.
 *
 * This routine starts the fcp_poll_timer of @phba.
 **/
void lpfc_poll_start_timer(struct lpfc_hba * phba)
{
	lpfc_poll_rearm_timer(phba);
}

/**
 * lpfc_poll_timeout - Restart polling timer
 * @ptr: Map to lpfc_hba data structure pointer.
 *
 * This routine restarts fcp_poll timer, when FCP ring  polling is enable
 * and FCP Ring interrupt is disable.
 **/
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
void lpfc_poll_timeout(unsigned long ptr)
#else
void lpfc_poll_timeout(struct timer_list *t)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
	struct lpfc_hba *phba = (struct lpfc_hba *) ptr;
#else
	struct lpfc_hba *phba = from_timer(phba, t, fcp_poll_timer);
#endif

	if (phba->cfg_poll & ENABLE_FCP_RING_POLLING) {
		lpfc_sli_handle_fast_ring_event(phba,
			&phba->sli.sli3_ring[LPFC_FCP_RING], HA_R0RE_REQ);

		if (phba->cfg_poll & DISABLE_FCP_RING_INT)
			lpfc_poll_rearm_timer(phba);
	}
}

/*
 * External DIF State (EDSM) routines
 *
 * STATE Descriptions:
 * LPFC_EDSM_INIT
 *	Used when LUN path is initialized and External DIF is configured for
 *	this HBA. This logic assumes the SCSI Layer will be sending a standard
 *	INQUIRY, page 0 / evpd 0, after device initialization is complete
 * LPFC_EDSM_NEEDS_INQUIRY
 *	On a Link Up, or RSCN, previously discovered paths imay need to be checked
 *	for External DIF capability by issuing an INQUIRY P0
 * LPFC_EDSM_PATH_STANDBY
 *	Used when the LUN path points to a External DIF array; however, External
 *	DIF is NOT enabled at this time.
 * LPFC_EDSM_PATH_READY
 *	Used when the LUN path points to a External DIF array and External DIF
 *	is enabled.
 * LPFC_EDSM_PATH_CHECK
 *	Used when the External DIF state of the device has changed.
 */
void
lpfc_edsm_set_state(struct lpfc_vport *vport,
		    struct lpfc_external_dif_support *dp,
		    uint32_t state)
{
	lpfc_printf_vlog(vport, KERN_INFO, LOG_EDIF,
			 "0918 EDSM: Device(%d/%lld) State Change %d to %d ",
			 dp->sid, dp->lun, dp->state, state);

	dp->state = state;
}

/**
 * lpfc_edsm_process_inq - Process a SCSI Layer Inquiry
 * @vport: The virtual port for which this call being executed.
 * @lpfc_cmd: lpfc scsi command object pointer.
 *
 * Piggy back on a SCSI Layer Inquiry to check for External DIF support
 */
static uint32_t
lpfc_edsm_process_inq(struct lpfc_vport *vport, struct lpfc_io_buf *lpfc_cmd,
		      uint8_t *cdb_ptr)
{
	struct lpfc_iocbq *piocbq = &(lpfc_cmd->cur_iocbq);

	/* We are only interested in EVPD=0 &&  page 0 */
	if ((cdb_ptr[1] & 0x1) || cdb_ptr[2])
		return 0;
	/* Setup to intercept INQUIRY completion */
	piocbq->iocb_cmpl = lpfc_external_dif_cmpl;
	return 1;
}

/**
 * lpfc_edsm_rw_path_rdy - Process a SCSI read/write command
 * @vport: The virtual port for which this call being executed.
 * @lpfc_cmd: lpfc scsi command object pointer.
 *
 * Process a SCSI read/write command on a External DIF path
 */
static void
lpfc_edsm_rw_path_rdy(struct lpfc_vport *vport, struct lpfc_io_buf *lpfc_cmd,
		      uint8_t *cdb_ptr, uint32_t op)
{
	cdb_ptr[1] |= LPFC_FDIF_CDB_PROTECT; /* Set PROTECT = 001 */

	switch (op) {
	case SCSI_PROT_NORMAL:
		lpfc_cmd->flags |= LPFC_SBUF_NORMAL_DIF;
		break;
	case SCSI_PROT_READ_INSERT:
	case SCSI_PROT_WRITE_STRIP:
		lpfc_cmd->flags |= LPFC_SBUF_PASS_DIF;
		break;
	}
}

/**
 * lpfc_edsm_vaiidate_target - Check a remote NPort for External DIF support
 * @vport: The virtual port for which this call being executed.
 * @ndlp - Pointer to lpfc_nodelist instance.
 *
 * Step thru all paths to a remote NPort to see if External DIF needs
 * to be validated.
 */
void
lpfc_edsm_validate_target(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	struct lpfc_external_dif_support *dp;
	unsigned long flags;
	int rc;

	lpfc_printf_vlog(vport, KERN_INFO, LOG_EDIF,
			"0920 External DIF: Validate Target %06x: sid %d",
			ndlp->nlp_DID, ndlp->nlp_sid);

	spin_lock_irqsave(&vport->external_dif_lock, flags);
	list_for_each_entry(dp, &vport->external_dif_list, listentry) {
		if (memcmp(&ndlp->nlp_portname, (uint8_t *)&dp->portName,
			   sizeof(struct lpfc_name)) == 0) {
			if (dp->state !=  LPFC_EDSM_PATH_READY) {
				rc = lpfc_issue_scsi_inquiry(vport, ndlp,
							     dp->lun);
				if (rc == 0) {
					lpfc_edsm_set_state(
						vport, dp,
						LPFC_EDSM_PATH_CHECK);
				} else {
					lpfc_edsm_set_state(
						vport, dp,
						LPFC_EDSM_NEEDS_INQUIRY);
				}
			}
		}
	}
	spin_unlock_irqrestore(&vport->external_dif_lock, flags);
}

/**
 * lpfc_issue_scsi_inquiry - Send a driver initiated SCSI INQUIRY
 * @vport: The virtual port for which this call being executed.
 * @ndlp - Pointer to lpfc_nodelist instance.
 *
 * This function is called when the driver want to issue a SCSI
 * INQUIRY Page 0 evpd 0. This is typically called when the driver
 * wants to verify if a device supports External DIF.
 **/
int
lpfc_issue_scsi_inquiry(struct lpfc_vport *vport,
			struct lpfc_nodelist *ndlp, uint32_t lunId)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_io_buf *lpfc_cmd = NULL;
	struct sli4_sge *sgl;
	dma_addr_t pdma_phys_data;
	IOCB_t *iocb_cmd;
	uint32_t err;

	/* Check if there is a path to the target */
	if (!NLP_CHK_NODE_ACT(ndlp))
		return EINVAL;
	if (ndlp->nlp_state != NLP_STE_MAPPED_NODE)
		return EINVAL;

	lpfc_cmd = lpfc_get_scsi_buf(phba, ndlp, NULL);
	if (!lpfc_cmd)
		return EIO;

	/*
	 * lpfc_cmd is partially setup for the IO.
	 * We will use 4 SGEs in the SGL, immediately follwed by
	 * 256 bytes for the INQUIRY data.
	 * The lpfc_cmd data (virt) / dma_handle (phys) point to the SGL.
	 * The first SGE, fcp_cmnd is already setup
	 * The next SGE fcp_rsp is already setup.
	 * Next setup the SGE for the data.
	 */

	sgl = (struct sli4_sge *)lpfc_cmd->dma_sgl;
	sgl++;
	bf_set(lpfc_sli4_sge_last, sgl, 0); /* FCP_RSP is no longer last */
	sgl++;
	pdma_phys_data = lpfc_cmd->dma_handle + (4 * sizeof(struct sli4_sge));
	sgl->addr_hi = cpu_to_le32(putPaddrHigh(pdma_phys_data));
	sgl->addr_lo = cpu_to_le32(putPaddrLow(pdma_phys_data));
	sgl->word2 = le32_to_cpu(sgl->word2);
	bf_set(lpfc_sli4_sge_last, sgl, 1);
	bf_set(lpfc_sli4_sge_offset, sgl, 0);
	bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_DATA);
	sgl->word2 = cpu_to_le32(sgl->word2);
	sgl->sge_len = cpu_to_le32(256);
	sgl++;
	sgl->addr_hi = 0;
	sgl->addr_lo = 0;
	sgl->word2 = 0;
	sgl->sge_len = 0;

	/*
	 * The lpfc_cmd->data / lpfc_cmd->dma_handle should now point to
	 * the following construct:
	 *
	 * FCP_CMD SGE		sizeof(sli4_sge)
	 * FCP_RSP SGE		sizeof(sli4_sge)
	 * INQUIRY DATA SGE	sizeof(sli4_sge)
	 * Zero'ed SGE		sizeof(sli4_sge)
	 * INQUIRY Data		256 bytes
	 * Filler
	 * FCP_CMD Data		sizeof(struct fcp_cmd)
	 * FCP_RSP_Data		sizeof(struct fcp_rsp)
	 */

	/* Setup the FCP_CMD */
	memset(lpfc_cmd->fcp_cmnd, 0, sizeof(struct fcp_cmnd));
	lpfc_cmd->fcp_cmnd->fcpCdb[0] = INQUIRY;
	lpfc_cmd->fcp_cmnd->fcpCdb[4] = 0xff;
	lpfc_cmd->fcp_cmnd->fcpCntl3 = READ_DATA;
	int_to_scsilun(lunId, &lpfc_cmd->fcp_cmnd->fcp_lun);
	lpfc_cmd->fcp_cmnd->fcpDl = cpu_to_be32(0xff);

	/* Zero the FCP_RSP */
	memset((void *)lpfc_cmd->fcp_rsp, 0, sizeof(struct fcp_rsp));

	/*
	 * Setup the lpfc_cmd / IOCB
	 * set LPFC_IO_DRVR_INIT in iocb_flag field to indicate driver use of
	 * iocb from scsi buf list for a driver initiated SCSI IO.
	 */
	lpfc_cmd->cur_iocbq.iocb_flag |= LPFC_IO_DRVR_INIT;
	lpfc_cmd->cur_iocbq.iocb_cmpl = lpfc_external_dif_cmpl;
	lpfc_cmd->cur_iocbq.context1 = lpfc_cmd;
	lpfc_cmd->cur_iocbq.context2 = (void *)(unsigned long)lunId;
	lpfc_cmd->cur_iocbq.vport = vport;
	lpfc_cmd->timeout = 30;
	lpfc_cmd->pCmd = NULL;
	lpfc_cmd->seg_cnt = 1;
	lpfc_cmd->rdata = (void *)ndlp; /* Save ndlp in rdata */
	lpfc_cmd->ndlp = ndlp;

	iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	iocb_cmd->ulpCommand = CMD_FCP_IREAD64_CR;
	iocb_cmd->ulpPU = PARM_READ_CHECK;
	iocb_cmd->un.fcpi.fcpi_parm  = 0xff;
	iocb_cmd->ulpContext = ndlp->nlp_rpi;
	iocb_cmd->ulpContext = phba->sli4_hba.rpi_ids[ndlp->nlp_rpi];
	if (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE)
		iocb_cmd->ulpFCP2Rcvy = 1;
	iocb_cmd->ulpClass = (ndlp->nlp_fcp_info & 0x0f);
	iocb_cmd->ulpTimeout = lpfc_cmd->timeout;

	err = lpfc_sli_issue_iocb(phba, LPFC_FCP_RING,
				  &lpfc_cmd->cur_iocbq, SLI_IOCB_RET_IOCB);

	lpfc_printf_vlog(vport, KERN_INFO, LOG_EDIF,
			 "0919 External DIF: Issue INQUIRY (%d/%d) ret %d "
			 "flag x%x iotag x%x xri x%x",
			 ndlp->nlp_sid, lunId, err,
			 lpfc_cmd->cur_iocbq.iocb_flag,
			 lpfc_cmd->cur_iocbq.iotag,
			 lpfc_cmd->cur_iocbq.sli4_xritag);

	if (err) {
		lpfc_cmd->cur_iocbq.iocb_flag &= ~LPFC_IO_DRVR_INIT;
		lpfc_cmd->rdata = NULL;
		lpfc_release_scsi_buf(phba, lpfc_cmd);
		return EIO;
	}

	if (phba->cfg_xri_rebalancing)
		lpfc_keep_pvt_pool_above_lowwm(phba, lpfc_cmd->hdwq_no);

	return 0;
}

/**
 * lpfc_external_dif_match - Look up a specific External DIF device
 * @vport: The virtual port for which this call is being executed.
 * @lun: lun id used to specify the desired External DIF device
 * @sid: SCSI id used to specify the desired External DIF device
 *
 * This routine scans the discovered External DIF devices for the vport
 * for a match using the lun/sid criteria.
 * MUST hold external_dif_lock BEFORE calling this routine
 *
 * Return code :
 *   NULL - device not found
 *   dp   - struct lpfc_external_dif_support of matching device
 **/
static struct lpfc_external_dif_support *
lpfc_external_dif_match(struct lpfc_vport *vport, uint64_t lun, uint32_t sid)
{
	struct lpfc_external_dif_support *dp;

	list_for_each_entry(dp, &vport->external_dif_list, listentry) {
		if ((dp->sid == sid) && (dp->lun == lun)) {
			return dp;
		}
	}
	return NULL;
}

/**
 * lpfc_retry_scsi_inquiry - Send a driver initiated SCSI INQUIRY
 * @vport: The virtual port for which this call being executed.
 * @ndlp - Pointer to lpfc_nodelist instance.
 *
 * This function is called when the driver want to retry a SCSI
 * INQUIRY Page 0 evpd 0. This is typically called when the driver
 * wants to verify if a device supports External DIF and a previous
 * driver initiated INQUIRY has failed.
 **/
int
lpfc_retry_scsi_inquiry(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
			uint32_t lunId, uint32_t status)
{
	struct lpfc_external_dif_support *dp;
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&vport->external_dif_lock, flags);
	dp = lpfc_external_dif_match(vport, lunId, ndlp->nlp_sid);
	if (dp) {
		dp->inq_retry++;
		if (dp->inq_retry < LPFC_EDSM_MAX_RETRY) {
			lpfc_printf_vlog(vport, KERN_INFO, LOG_EDIF,
					 "0921 External DIF: Inquiry retry %d "
					 "NPort %06x: (%d/%d) err x%x",
					 dp->inq_retry, ndlp->nlp_DID,
					 ndlp->nlp_sid, lunId, status);
			rc = lpfc_issue_scsi_inquiry(vport, ndlp, dp->lun);
		} else {
			lpfc_printf_vlog(vport, KERN_INFO, LOG_EDIF,
					 "0922 External DIF: Inquiry retry "
					 "exceeded NPort %06x: (%d/%d) err x%x",
					 ndlp->nlp_DID, ndlp->nlp_sid,
					 lunId, status);
			rc = ENXIO;
		}
	}
	spin_unlock_irqrestore(&vport->external_dif_lock, flags);
	return rc;
}

/**
 * lpfc_external_dif_cmpl - IOCB completion routine for a External DIF IO
 * @phba: The Hba for which this call is being executed.
 * @pIocbIn: The command IOCBQ for the scsi cmnd.
 * @pIocbOut: The response IOCBQ for the scsi cmnd.
 *
 * This routine processes the External DIF SCSi command cmpl before calling the
 * normal SCSI cmpl routine (lpfc_scsi_cmd_iocb_cmpl). There are 2 types of
 * External DIF completions, INQUIRY and READ/WRITE SCSI commands.
 *
 * We use INQUIRY to discover External DIF devices. An External DIF device does
 * not advertise itself as T10-DIF capable using standard bits in the INQUIRY
 * and READ_CAPACITY commands. Instead, it uses some vendor specfic information
 * in the standard INQUIRY command to turn on this feature.
 *
 * For READ/WRITE IOs we convert the IO back into a normal IO so it can be
 * completed to the SCSI layer. The SCSI layer is unaware the IO was actually
 * transmitted on the wire in T10 DIF Type 1 format.
 **/
static void
lpfc_external_dif_cmpl(struct lpfc_hba *phba, struct lpfc_iocbq *pIocbIn,
		       struct lpfc_iocbq *pIocbOut)
{
	struct lpfc_io_buf *lpfc_cmd =
		(struct lpfc_io_buf *)pIocbIn->context1;
	struct lpfc_vport *vport = pIocbIn->vport;
	struct fcp_rsp *fcprsp = lpfc_cmd->fcp_rsp;
	struct lpfc_external_dif_support *dp;
	uint32_t resp_info = fcprsp->rspStatus2;
	struct scsi_cmnd *cmnd = lpfc_cmd->pCmd;
	uint32_t status = pIocbOut->iocb.ulpStatus;
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *ndlp = NULL;
	struct lpfc_vendor_dif *vendor_dif_infop;
	struct fcp_cmnd *fcpcmd;
	struct scatterlist *sgde;
	unsigned long flags;
	uint8_t *data_inq, *buf = NULL, *ptr;
	uint8_t *name;
	uint32_t cnt, cmd, lun, resid, len;

	if (status && cmnd) {
		if ((status != IOSTAT_FCP_RSP_ERROR) ||
		    !(resp_info & RESID_UNDER))
			goto out;
	}
	fcpcmd = lpfc_cmd->fcp_cmnd;
	cmd = fcpcmd->fcpCdb[0];

	/* Only success and RESID_UNDER make it here */
	switch (cmd) {
	case INQUIRY:

		if (cmnd) {
			/* Inquiry issued by SCSI Layer */
			rdata = lpfc_cmd->rdata;
			ndlp = rdata->pnode;

			sgde = scsi_sglist(cmnd);
			buf = kmalloc(scsi_bufflen(cmnd), GFP_ATOMIC);
			len = scsi_bufflen(cmnd);
			ptr = buf;
			while (sgde && len) {
#if (KERNEL_MAJOR < 3) || ((KERNEL_MAJOR == 3) && (KERNEL_MINOR < 4))
				data_inq = kmap_atomic(sg_page(sgde), KM_IRQ0)
				    + sgde->offset;
#else
				data_inq = kmap_atomic(sg_page(sgde))
				    + sgde->offset;
#endif
				memcpy(ptr, data_inq, sgde->length);
				len -= sgde->length;
				ptr += sgde->length;
#if (KERNEL_MAJOR < 3) || ((KERNEL_MAJOR == 3) && (KERNEL_MINOR < 4))
				kunmap_atomic(data_inq - sgde->offset, KM_IRQ0);
#else
				kunmap_atomic(data_inq - sgde->offset);
#endif
				sgde = sg_next(sgde);
			}

			data_inq = buf;
			lun = cmnd->device->lun;
		} else {
			/* Inquiry issued by driver */
			ndlp = (void *)lpfc_cmd->rdata;
			lpfc_cmd->rdata = NULL;

			data_inq = (uint8_t *)lpfc_cmd->dma_sgl +
				(4 * sizeof(struct sli4_sge));
			lun = (uint32_t)(unsigned long)(pIocbIn->context2);

			if ((status != IOSTAT_FCP_RSP_ERROR) ||
			    !(resp_info & RESID_UNDER)) {
				/* If its an unexpected error, retry it */
				lpfc_retry_scsi_inquiry(vport, ndlp, lun,
							status);
				break;
			}
		}

		/* Jump to T10 Vendor Identification field */
		data_inq += LPFC_INQ_VID_OFFSET;
		if ((memcmp(data_inq, LPFC_INQ_FDIF_VENDOR,
			    sizeof(LPFC_INQ_FDIF_VENDOR) != 0))) {
			ndlp->nlp_flag &= ~NLP_EXTERNAL_DIF;
			ndlp->nlp_flag |= NLP_SKIP_EXT_DIF;
			break;
		}

		/* Jump to Vendor specific DIF info */
		vendor_dif_infop = (struct lpfc_vendor_dif *)(data_inq +
			(LPFC_INQ_VDIF_OFFSET - LPFC_INQ_VID_OFFSET));

		name = (uint8_t *)&ndlp->nlp_portname;

		spin_lock_irqsave(&vport->external_dif_lock, flags);
		dp = lpfc_external_dif_match(vport, lun, ndlp->nlp_sid);
		if (dp) {
			dp->inq_cnt++;
			dp->inq_retry = 0;
			ndlp->nlp_flag |= NLP_EXTERNAL_DIF;

			if (dp->state == LPFC_EDSM_PATH_READY) {
				spin_unlock_irqrestore(
					&vport->external_dif_lock, flags);
				break; /* device already exists */
			}
		} else {

			/* New External DIF device found */
			dp = kmalloc(sizeof(struct lpfc_external_dif_support),
				     GFP_ATOMIC);
			if (!dp) {
				spin_unlock_irqrestore(
					&vport->external_dif_lock, flags);
				break;
			}
			memset(dp, 0x0,
			       sizeof(struct lpfc_external_dif_support));
			dp->lun = lun;
			dp->sid = ndlp->nlp_sid;
			dp->dif_info = vendor_dif_infop->dif_info;
			lpfc_edsm_set_state(vport, dp, LPFC_EDSM_INIT);
			memcpy(&dp->portName, name, sizeof(struct lpfc_name));
			dp->inq_cnt = 1;
			dp->err3f_cnt = 0;
			dp->err4b_cnt = 0;
			ndlp->nlp_flag |= NLP_EXTERNAL_DIF;

			list_add_tail(&dp->listentry,
				      &vport->external_dif_list);
		}

		/* Make sure the INQUIRY payload has our
		 * vendor specfic info included.
		 */
		if (status == IOSTAT_FCP_RSP_ERROR)
			resid = be32_to_cpu(fcprsp->rspResId);
		else
			resid = 0;
		cnt = be32_to_cpu(fcpcmd->fcpDl) - resid;
		if (cnt < LPFC_INQ_FDIF_SZ) {
			lpfc_edsm_set_state(vport, dp, LPFC_EDSM_IGNORE);
			spin_unlock_irqrestore(&vport->external_dif_lock, flags);
			lpfc_printf_vlog(vport, KERN_INFO, LOG_EDIF,
					"0917 External DIF Vendor size error "
					"Data: %02x %02x %02x\n",
					be32_to_cpu(fcpcmd->fcpDl),
					be32_to_cpu(fcprsp->rspResId),
					vendor_dif_infop->length);
			ndlp->nlp_flag &= ~NLP_EXTERNAL_DIF;
			break;
		}

		/* Check to see if External DIF protection is enabled and we
		 * are version 1.
		 */
		if ((vendor_dif_infop->length != LPFC_INQ_FDIF_SIZE) ||
		    (vendor_dif_infop->version != LPFC_INQ_FDIF_VERSION)) {
			lpfc_edsm_set_state(vport, dp, LPFC_EDSM_IGNORE);
			spin_unlock_irqrestore(&vport->external_dif_lock, flags);
			lpfc_printf_vlog(vport, KERN_ERR, LOG_EDIF,
					"0709 Possible External DIF INQUIRY "
					"info error: "
					"lun %d xri x%x fcpdl %d resid %d "
					"Data: %02x %02x %02x\n",
					lun, lpfc_cmd->cur_iocbq.sli4_xritag,
					be32_to_cpu(fcpcmd->fcpDl), resid,
					vendor_dif_infop->length,
					vendor_dif_infop->version,
					vendor_dif_infop->dif_info);
			dp->dif_info = 0;
			ndlp->nlp_flag &= ~NLP_EXTERNAL_DIF;
			break;
		}

		dp->dif_info = vendor_dif_infop->dif_info;

		/* Currently we only support DIF Type 1 * (GRD_CHK / REF_CHK) */
		if (vendor_dif_infop->dif_info != (LPFC_FDIF_PROTECT |
		    LPFC_FDIF_REFCHK |  LPFC_FDIF_GRDCHK)) {
			lpfc_edsm_set_state(vport, dp, LPFC_EDSM_PATH_STANDBY);
			spin_unlock_irqrestore(&vport->external_dif_lock, flags);
			lpfc_printf_vlog(vport, KERN_INFO, LOG_EDIF,
					"0701 External DIF INQUIRY protection "
					"off: xri x%x fcpdl %d resid %d "
					"Data: %02x %02x %02x\n",
					lpfc_cmd->cur_iocbq.sli4_xritag,
					be32_to_cpu(fcpcmd->fcpDl),
					be32_to_cpu(fcprsp->rspResId),
					vendor_dif_infop->length,
					vendor_dif_infop->version,
					vendor_dif_infop->dif_info);
			break;
		}

		lpfc_edsm_set_state(vport, dp, LPFC_EDSM_PATH_READY);
		spin_unlock_irqrestore(&vport->external_dif_lock, flags);

		lpfc_printf_vlog(vport, KERN_WARNING, LOG_EDIF,
				"0712 Discovered External DIF device NPortId "
				"x%x: scsi_id x%x: lun_id x%llx: WWPN "
				"%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
				ndlp->nlp_DID, dp->sid, dp->lun,
				*name, *(name+1), *(name+2), *(name+3),
				*(name+4), *(name+5), *(name+6), *(name+7));
		break;
	default:
		break;
	}
out:
	kfree(buf);
	if (lpfc_cmd->cur_iocbq.iocb_flag & LPFC_IO_DRVR_INIT) {
		if (ndlp == NULL) {
			ndlp = (void *)lpfc_cmd->rdata;
			lpfc_cmd->rdata = NULL;
		}
		pIocbIn->context2 = NULL;
		lpfc_cmd->cur_iocbq.iocb_flag &= ~LPFC_IO_DRVR_INIT;
		lpfc_release_scsi_buf(phba, lpfc_cmd);
		return;
	}

	lpfc_scsi_cmd_iocb_cmpl(phba, pIocbIn, pIocbOut);
}

/**
 * lpfc_external_dif - Check to see if we want to process this IO as a External DIF
 * @vport: The virtual port for which this call is being executed.
 * @lpfc_cmd: Pointer to lpfc_io_buf data structure.
 *
 * This routine will selectively force normal IOs to be processed as a
 * READ_STRIP / WRITE_INSERT T10-DIF IO. The upper SCSI Layer will be unaware
 * that the IO is going to be transmitted on the wire with T10-DIF protection
 * data. This routine also diverts INQUIRY command cmpletions so they can be
 * used to scan for External DIF devices.
 **/
static int
lpfc_external_dif(struct lpfc_vport *vport, struct lpfc_io_buf *lpfc_cmd,
		  struct lpfc_nodelist *ndlp, uint8_t *cdb_ptr)
{
	struct scsi_cmnd *cmnd = lpfc_cmd->pCmd;
	uint32_t op = scsi_get_prot_op(cmnd);
	struct lpfc_external_dif_support *dp;
	struct scsi_device *sdev;
	unsigned long flags;
	int rc;

	switch (op) {
	case SCSI_PROT_NORMAL:
	case SCSI_PROT_READ_INSERT:
	case SCSI_PROT_WRITE_STRIP:
		break;
	default:
		return 0;
	}

	switch (cdb_ptr[0]) {
	case INQUIRY:
		lpfc_edsm_process_inq(vport, lpfc_cmd, cdb_ptr);
		break;
	case READ_10:
	case READ_12:
	case READ_16:
	case WRITE_10:
	case WRITE_12:
	case WRITE_16:
	case WRITE_SAME:
	case WRITE_SAME_16:
	case WRITE_VERIFY:
	case VERIFY:
	case VERIFY_12:
	case VERIFY_16:

		/* Is this an External DIF device */
		sdev = cmnd->device;
		spin_lock_irqsave(&vport->external_dif_lock, flags);
		dp = lpfc_external_dif_match(vport, sdev->lun, sdev->id);
		if (dp == NULL) {
			spin_unlock_irqrestore(&vport->external_dif_lock,
					       flags);
			break;
		}
		switch (dp->state) {
		case LPFC_EDSM_PATH_STANDBY:
			/* IO is sent normally */
			break;

		case LPFC_EDSM_NEEDS_INQUIRY:
			rc = lpfc_issue_scsi_inquiry(vport, ndlp, dp->lun);
			if (rc == 0)
				lpfc_edsm_set_state(vport, dp,
						    LPFC_EDSM_PATH_CHECK);
			else
				lpfc_edsm_set_state(vport, dp,
						    LPFC_EDSM_NEEDS_INQUIRY);
			/* fall through */

		case LPFC_EDSM_PATH_CHECK:
			/* In process of checking path, need to retry the IO */
			spin_unlock_irqrestore(&vport->external_dif_lock,
					       flags);
			lpfc_printf_vlog(vport, KERN_INFO, LOG_EDIF,
					 "0923 External DIF: In process of "
					 "checking path, retry IO\n");
			return 1;

		case LPFC_EDSM_PATH_READY:
			/* modify CDB as needed */
			lpfc_edsm_rw_path_rdy(vport, lpfc_cmd, cdb_ptr, op);
			lpfc_cmd->edifp = (void *)dp;

		default:
			break;
		}
		spin_unlock_irqrestore(&vport->external_dif_lock, flags);
		break;
	default:
		break;
	}
	return 0;
}

/**
 * lpfc_queuecommand - scsi_host_template queuecommand entry point
 * @cmnd: Pointer to scsi_cmnd data structure.
 * @done: Pointer to done routine.
 *
 * Driver registers this routine to scsi midlayer to submit a @cmd to process.
 * This routine prepares an IOCB from scsi command and provides to firmware.
 * The @done callback is invoked after driver finished processing the command.
 *
 * Return value :
 *   0 - Success
 *   SCSI_MLQUEUE_HOST_BUSY - Block all devices served by this host temporarily.
 **/
static int
#ifdef DEF_SCSI_QCMD
lpfc_queuecommand(struct Scsi_Host *shost, struct scsi_cmnd *cmnd)
#else
lpfc_queuecommand(struct scsi_cmnd *cmnd, void (*done) (struct scsi_cmnd *))
#endif
{
#ifndef DEF_SCSI_QCMD
	struct Scsi_Host  *shost = cmnd->device->host;
#endif
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *ndlp;
	struct lpfc_io_buf *lpfc_cmd;
	struct fc_rport *rport = starget_to_rport(scsi_target(cmnd->device));
	int err, idx;
	uint64_t start;

	start = ktime_get_ns();
	rdata = lpfc_rport_data_from_scsi_device(cmnd->device);

	/* sanity check on references */
	if (unlikely(!rdata) || unlikely(!rport))
		goto out_fail_command;

#ifndef DEF_SCSI_QCMD
	spin_unlock_irq(shost->host_lock);
#endif

	err = fc_remote_port_chkready(rport);
	if (err) {
		cmnd->result = err;
		goto out_fail_command;
	}
	/*
	 * Do not let the mid-layer retry I/O too fast. If an I/O is retried
	 * without waiting a bit then indicate that the device is busy.
	 */
	if (cmnd->retries &&
	    time_before(jiffies, (cmnd->jiffies_at_alloc +
				  msecs_to_jiffies(LPFC_RETRY_PAUSE *
						   cmnd->retries)))) {
#ifndef DEF_SCSI_QCMD
		spin_lock_irq(shost->host_lock);
#endif
		return SCSI_MLQUEUE_DEVICE_BUSY;
	}
	ndlp = rdata->pnode;

	if ((scsi_get_prot_op(cmnd) != SCSI_PROT_NORMAL) &&
		(!(phba->sli3_options & LPFC_SLI3_BG_ENABLED))) {

		lpfc_printf_log(phba, KERN_ERR, LOG_TRACE_EVENT,
				"9058 BLKGRD: ERROR: rcvd protected cmd:%02x"
				" op:%02x str=%s without registering for"
				" BlockGuard - Rejecting command\n",
				cmnd->cmnd[0], scsi_get_prot_op(cmnd),
				dif_op_str[scsi_get_prot_op(cmnd)]);
		goto out_fail_command;
	}

	/*
	 * Catch race where our node has transitioned, but the
	 * transport is still transitioning.
	 */
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp))
		goto out_tgt_busy1;

	/* Check if IO qualifies for CMF */
	if (phba->cmf_active_mode != LPFC_CFG_OFF &&
	    cmnd->sc_data_direction == DMA_FROM_DEVICE &&
	    (scsi_sg_count(cmnd))) {
		/* Latency start time saved in rx_cmd_start later in routine */
		err = lpfc_update_cmf_cmd(phba, scsi_bufflen(cmnd));
		if (err)
			goto out_tgt_busy1;
	}

	if (lpfc_ndlp_check_qdepth(phba, ndlp)) {
		if (atomic_read(&ndlp->cmd_pending) >= ndlp->cmd_qdepth) {
			lpfc_throttle_vlog(ndlp->vport, &ndlp->log,
					   LOG_FCP_ERROR,
					   "3377 Target Queue Full, scsi Id:%d"
					   " Qdepth:%d Pending command:%d"
					   " WWNN:%02x:%02x:%02x:%02x:"
					   "%02x:%02x:%02x:%02x, "
					   " WWPN:%02x:%02x:%02x:%02x"
					   ":%02x:%02x:%02x:%02x",
					   ndlp->nlp_sid, ndlp->cmd_qdepth,
					   atomic_read(&ndlp->cmd_pending),
					   ndlp->nlp_nodename.u.wwn[0],
					   ndlp->nlp_nodename.u.wwn[1],
					   ndlp->nlp_nodename.u.wwn[2],
					   ndlp->nlp_nodename.u.wwn[3],
					   ndlp->nlp_nodename.u.wwn[4],
					   ndlp->nlp_nodename.u.wwn[5],
					   ndlp->nlp_nodename.u.wwn[6],
					   ndlp->nlp_nodename.u.wwn[7],
					   ndlp->nlp_portname.u.wwn[0],
					   ndlp->nlp_portname.u.wwn[1],
					   ndlp->nlp_portname.u.wwn[2],
					   ndlp->nlp_portname.u.wwn[3],
					   ndlp->nlp_portname.u.wwn[4],
					   ndlp->nlp_portname.u.wwn[5],
					   ndlp->nlp_portname.u.wwn[6],
					   ndlp->nlp_portname.u.wwn[7]);
			goto out_tgt_busy2;
		}
	}

	lpfc_cmd = lpfc_get_scsi_buf(phba, ndlp, cmnd);
	if (lpfc_cmd == NULL) {
		lpfc_rampdown_queue_depth(phba);

		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP_ERROR,
				 "0707 driver's buffer pool is empty, "
				 "IO busied\n");
		goto out_host_busy;
	}
	lpfc_cmd->rx_cmd_start = start;

	/*
	 * Store the midlayer's command structure for the completion phase
	 * and complete the command initialization.
	 */
	lpfc_cmd->pCmd  = cmnd;
	lpfc_cmd->rdata = rdata;
	lpfc_cmd->ndlp = ndlp;
	lpfc_cmd->cur_iocbq.iocb_cmpl = NULL;
	cmnd->host_scribble = (unsigned char *)lpfc_cmd;
#ifndef DEF_SCSI_QCMD
	cmnd->scsi_done = done;
#endif

	err = lpfc_scsi_prep_cmnd(vport, lpfc_cmd, ndlp);
	if (err)
		goto out_host_busy_release_buf;

	if (lpfc_scsi_get_prot_op(cmnd) != SCSI_PROT_NORMAL) {
		if (phba->sli3_options & LPFC_SLI3_BG_ENABLED) {
			lpfc_printf_vlog(vport,
					 KERN_INFO, LOG_SCSI_CMD,
					 "9033 BLKGRD: rcvd %s cmd:x%x "
					 "sector x%llx cnt %u pt %x\n",
					 dif_op_str[scsi_get_prot_op(cmnd)],
					 cmnd->cmnd[0],
					 (unsigned long long)scsi_get_lba(cmnd),
					 blk_rq_sectors(cmnd->request),
					 (cmnd->cmnd[1]>>5));
		}
		err = lpfc_bg_scsi_prep_dma_buf(phba, lpfc_cmd);
	} else {
		if (phba->sli3_options & LPFC_SLI3_BG_ENABLED) {
			lpfc_printf_vlog(vport,
					 KERN_INFO, LOG_SCSI_CMD,
					 "9038 BLKGRD: rcvd PROT_NORMAL cmd: "
					 "x%x sector x%llx cnt %u pt %x\n",
					 cmnd->cmnd[0],
					 (unsigned long long)scsi_get_lba(cmnd),
					 blk_rq_sectors(cmnd->request),
					 (cmnd->cmnd[1]>>5));
		}
		err = lpfc_scsi_prep_dma_buf(phba, lpfc_cmd);
	}

	if (unlikely(err)) {
		if (err == 2) {
			cmnd->result = ScsiResult(DID_ERROR, 0);
			goto out_fail_command_release_buf;
		}
		goto out_host_busy_free_buf;
	}

	if (cmnd->cmnd[0] == 0 || cmnd->cmnd[0] == 0x12 ||
	    cmnd->cmnd[0] == 0xa0 || cmnd->cmnd[0] == 0x11) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
				 "3322 FCP cmd x%x <%d/%lld> "
				 "sid: x%x did: x%x oxid: x%x "
				 "Data: x%x x%x x%x x%x %x\n",
				 cmnd->cmnd[0],
				 cmnd->device ? cmnd->device->id : 0xffff,
				 cmnd->device ? cmnd->device->lun : (u64) -1,
				 vport->fc_myDID, ndlp->nlp_DID,
				 phba->sli_rev == LPFC_SLI_REV4 ?
				 lpfc_cmd->cur_iocbq.sli4_xritag : 0xffff,
				 lpfc_cmd->cur_iocbq.iocb.ulpContext,
				 lpfc_cmd->cur_iocbq.iocb.ulpIoTag,
				 lpfc_cmd->cur_iocbq.iocb.ulpTimeout,
				 (uint32_t)
				  (cmnd->request->timeout / 1000),
				 lpfc_cmd->cur_iocbq.iocb.ulpCommand);
	}

#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (unlikely(phba->hdwqstat_on & LPFC_CHECK_SCSI_IO))
		this_cpu_inc(phba->sli4_hba.c_stat->xmt_io);
#endif
	err = lpfc_sli_issue_iocb(phba, LPFC_FCP_RING,
				  &lpfc_cmd->cur_iocbq, SLI_IOCB_RET_IOCB);
#ifdef CONFIG_SCSI_LPFC_DEBUG_FS
	if (phba->ktime_on) {
		lpfc_cmd->ts_cmd_start = start;
		lpfc_cmd->ts_last_cmd = phba->ktime_last_cmd;
		lpfc_cmd->ts_cmd_wqput = ktime_get_ns();
	} else {
		lpfc_cmd->ts_cmd_start = 0;
	}
#endif
	if (err) {
		lpfc_throttle_vlog(vport, &vport->log, LOG_FCP,
				   "3376 FCP could not issue IOCB err %x"
				   "FCP cmd x%x <%d/%llu> "
				   "sid: x%x did: x%x oxid: x%x "
				   "Data: x%x x%x x%x x%x\n",
				   err, cmnd->cmnd[0],
				   cmnd->device ? cmnd->device->id : 0xffff,
				   cmnd->device ? cmnd->device->lun : (u64) -1,
				   vport->fc_myDID, ndlp->nlp_DID,
				   phba->sli_rev == LPFC_SLI_REV4 ?
				   lpfc_cmd->cur_iocbq.sli4_xritag : 0xffff,
				   lpfc_cmd->cur_iocbq.iocb.ulpContext,
				   lpfc_cmd->cur_iocbq.iocb.ulpIoTag,
				   lpfc_cmd->cur_iocbq.iocb.ulpTimeout,
				   (uint32_t)
				   (cmnd->request->timeout / 1000));

		goto out_host_busy_free_buf;
	}

	if (phba->cfg_poll & ENABLE_FCP_RING_POLLING) {
		lpfc_sli_handle_fast_ring_event(phba,
			&phba->sli.sli3_ring[LPFC_FCP_RING], HA_R0RE_REQ);

		if (phba->cfg_poll & DISABLE_FCP_RING_INT)
			lpfc_poll_rearm_timer(phba);
	}

	if (phba->cfg_xri_rebalancing)
		lpfc_keep_pvt_pool_above_lowwm(phba, lpfc_cmd->hdwq_no);

#ifndef DEF_SCSI_QCMD
	spin_lock_irq(shost->host_lock);
#endif
	return 0;

 out_host_busy_free_buf:
	idx = lpfc_cmd->hdwq_no;
	lpfc_scsi_unprep_dma_buf(phba, lpfc_cmd);
	if (phba->sli4_hba.hdwq) {
		switch (lpfc_cmd->fcp_cmnd->fcpCntl3) {
		case WRITE_DATA:
			phba->sli4_hba.hdwq[idx].scsi_cstat.output_requests--;
			break;
		case READ_DATA:
			phba->sli4_hba.hdwq[idx].scsi_cstat.input_requests--;
			break;
		default:
			phba->sli4_hba.hdwq[idx].scsi_cstat.control_requests--;
		}
	}
 out_host_busy_release_buf:
	lpfc_release_scsi_buf(phba, lpfc_cmd);
 out_host_busy:
	lpfc_update_cmf_cmpl(phba, LPFC_CGN_NOT_SENT, scsi_bufflen(cmnd),
			     shost);
#ifndef DEF_SCSI_QCMD
	spin_lock_irq(shost->host_lock);
#endif
	return SCSI_MLQUEUE_HOST_BUSY;

 out_tgt_busy2:
	lpfc_update_cmf_cmpl(phba, LPFC_CGN_NOT_SENT, scsi_bufflen(cmnd),
			     shost);
 out_tgt_busy1:
#ifndef DEF_SCSI_QCMD
	spin_lock_irq(shost->host_lock);
#endif
	return SCSI_MLQUEUE_TARGET_BUSY;

 out_fail_command_release_buf:
	lpfc_release_scsi_buf(phba, lpfc_cmd);
	lpfc_update_cmf_cmpl(phba, LPFC_CGN_NOT_SENT, scsi_bufflen(cmnd),
			     shost);

 out_fail_command:
#ifdef DEF_SCSI_QCMD
	cmnd->scsi_done(cmnd);
#else
	spin_lock_irq(shost->host_lock);
	done(cmnd);
#endif
	return 0;
}

/* lpfc_mode_sense_cmpl.
 *
 * This is the completion routine for a driver-issued mode sense
 * used to determine if an FCP target supports first_burst.  This
 * routine could be used for other mode sense data values.
 */
void
lpfc_mode_sense_cmpl(struct lpfc_hba *phba, struct lpfc_iocbq *p_io_in,
		       struct lpfc_iocbq *p_io_out)
{
	struct lpfc_io_buf *lpfc_cmd = p_io_in->context1;
	u32 resp_info = lpfc_cmd->fcp_rsp->rspStatus2;
	struct sli4_sge *sgl;
	struct lpfc_nodelist *ndlp = lpfc_cmd->ndlp;
	u32 status = p_io_out->iocb.ulpStatus;
	u32 result = p_io_out->iocb.un.ulpWord[4] & IOERR_PARAM_MASK;
	u32 fb = 0;
	int ret_val;
	struct lpfc_first_burst_page *p_data;

	/* Make sure the driver's remoteport state is still valid. If the
	 * reference put results in an invalid remoteport, just exit.
	 */
	ret_val = lpfc_nlp_put(ndlp);
	if (ret_val) {
		ret_val = -ENOMEM;
		goto out;
	}

	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp) ||
	    ndlp->nlp_state != NLP_STE_MAPPED_NODE) {
		ret_val = -ENXIO;
		goto out;
	}

	/* Only accept a successful IO or an IO with an underflow status
	 * because any other FCP RSP error indicates the IO had some other
	 * error.
	 */
	if (status &&
	    !(status == IOSTAT_FCP_RSP_ERROR && resp_info & RESID_UNDER)) {
		ret_val = -EIO;
		goto out;
	}

	/* No errors.  clear the return value. */
	ret_val = 0;

	/* Skip over the SGEs and headers to get the response data region. */
	sgl = (struct sli4_sge *)lpfc_cmd->dma_sgl;
	sgl += 4;
	p_data = (struct lpfc_first_burst_page *)(((u32 *)sgl) + 3);

	/* Validate the correct page code response and the number of bytes
	 * available before reading the first burst size. Initialize
	 * the first_burst in case the response doesn't have a value.
	 */
	ndlp->first_burst = 0;
	if (p_data->page_code == 0x2 && p_data->page_len == 0xe) {
		fb = be16_to_cpu(p_data->first_burst);
		ndlp->first_burst = min((u32)(fb * 512), (u32)LPFC_FCP_FB_MAX);
	} else {
		lpfc_printf_vlog(phba->pport, KERN_INFO,
				 LOG_FCP | LOG_FCP_ERROR,
				 "6332 Clear first burst value "
				 "Code x%x Len x%x\n", p_data->page_code,
				 p_data->page_len);
	}

	lpfc_printf_vlog(phba->pport, KERN_INFO,
			 LOG_FCP | LOG_FCP_ERROR,
			 "6336 FB Rsp: DID x%x Data: x%x x%x x%x x%x x%x x%x "
			 "x%x\n", ndlp->nlp_DID, fb, ndlp->first_burst,
			 status, result, resp_info,
			 lpfc_cmd->cur_iocbq.iocb_flag, ret_val);

	/* Pick up SLI4 exhange busy status from HBA */
	if (p_io_out->iocb_flag & LPFC_EXCHANGE_BUSY)
		lpfc_cmd->flags |= LPFC_SBUF_XBUSY;
	else
		lpfc_cmd->flags &= ~LPFC_SBUF_XBUSY;

 out:
	/* Push a driver message only if the IO completion is in error. */
	if (ret_val)
		lpfc_printf_vlog(phba->pport, KERN_INFO,
				 LOG_FCP_UNDER | LOG_FCP_ERROR,
				 "6331 FB err %d  <x%x/x%x> resp_info x%x\n",
				 ret_val, status, result, resp_info);

	p_io_in->context2 = NULL;
	lpfc_cmd->cur_iocbq.iocb_flag &= ~LPFC_IO_DRVR_INIT;
	lpfc_release_scsi_buf(phba, lpfc_cmd);
}

/**
 * lpfc_init_scsi_cmd - Initiatize SCSI command-specific fields.
 * @vport: The virtual port for which this call being executed.
 * @lpfc_cmd: The lpfc_io_buf containing this command.
 * @dev_id - device identifier for this command
 * @cmd_id:  SCSI command used to format the driver io and CDB
 * @data_len: Number of bytes available for data.
 *
 * This routine initializes an lpfc_io_buf for the specified @cmd_id to
 * the specified @dev_id for @data_len bytes.
 *
 * Returns:
 *   0 - on success
 *   A negative error value in the form -Exxxx otherwise.
 *
 **/
static int
lpfc_init_scsi_cmd(struct lpfc_vport *vport, struct lpfc_io_buf *lpfc_cmd,
		   struct lpfc_scsi_io_req *cmd_req, u32 data_len)
{
	int ret = 0;
	IOCB_t *iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;

	/* Set command-independent values. */
	lpfc_cmd->fcp_cmnd->fcpCdb[0] = cmd_req->cmd_id;
	lpfc_cmd->fcp_cmnd->fcpCdb[4] = data_len;
	lpfc_cmd->cur_iocbq.iocb_cmpl = cmd_req->cmd_cmpl;
	lpfc_cmd->fcp_cmnd->fcpDl = cpu_to_be32(data_len);
	int_to_scsilun(cmd_req->dev_id, &lpfc_cmd->fcp_cmnd->fcp_lun);

	/* SCSI Command specific initialization. */
	switch (cmd_req->cmd_id) {
	case MODE_SENSE:
		/* Send the DISCONNECT-RECONNECT page code */
		lpfc_cmd->fcp_cmnd->fcpCdb[2] = 0x2;
		lpfc_cmd->fcp_cmnd->fcpCntl3 = READ_DATA;
		lpfc_cmd->cur_iocbq.context2 = NULL;
		iocb_cmd->ulpCommand = CMD_FCP_IREAD64_CR;
		iocb_cmd->ulpPU = PARM_READ_CHECK;
		break;
	default:
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "6335 Unsupported CMD x%x\n", cmd_req->cmd_id);
		ret = -EINVAL;
		break;
	}
	return ret;
}

/**
 * lpfc_issue_scsi_cmd - Send a driver initiated SCSI cmd
 * @vport: The virtual port for which this call being executed.
 * @ndlp - Pointer to lpfc_nodelist instance.
 * @scsi_io_req - pointer to a job structure with the command, device ids,
 *                and the associated completion routine.
 *
 * The driver calls this function to dynamically determine if an FCP
 * target suppports a particular feature.  The caller needs to specify
 * what command is needed to what device.
 *
 * Returns:
 *   0 - on success
 *   A negative error value in the form -Exxxx otherwise.
 *
 **/
int
lpfc_issue_scsi_cmd(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp,
		    struct lpfc_scsi_io_req *cmd_req)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_io_buf *lpfc_cmd = NULL;
	struct sli4_sge *sgl;
	dma_addr_t pdma_phys_data;
	IOCB_t *iocb_cmd;
	int err = 0;

	/* Requirements must be met. */
	if (phba->sli_rev != LPFC_SLI_REV4 ||
	    phba->hba_flag == HBA_FCOE_MODE ||
	    !phba->pport->cfg_first_burst_size)
		return -EPERM;

	/* Because the driver potentially sends an FCP and NVME PRLI, the
	 * caller is required to ensure an FCP PRLI has been successfully
	 * completed.
	 */
	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp))
		return -EINVAL;

	lpfc_cmd = lpfc_get_scsi_buf(phba, ndlp, NULL);
	if (!lpfc_cmd)
		return -EIO;

	/*
	 * Every lpfc_io_buf is partially setup for the IO - an FCP Cmd SGE
	 * and an FCP RSP SGE.  This routine configures two more - one for
	 * this scsi command and an empty SGE. A 256 byte data area follows
	 * for the command payload data.
	 */
	sgl = (struct sli4_sge *)lpfc_cmd->dma_sgl;
	sgl++;
	bf_set(lpfc_sli4_sge_last, sgl, 0);
	sgl++;

	/* SGE for the SCSI command */
	pdma_phys_data = lpfc_cmd->dma_handle + (4 * sizeof(struct sli4_sge));
	sgl->addr_hi = cpu_to_le32(putPaddrHigh(pdma_phys_data));
	sgl->addr_lo = cpu_to_le32(putPaddrLow(pdma_phys_data));
	sgl->word2 = le32_to_cpu(sgl->word2);
	bf_set(lpfc_sli4_sge_last, sgl, 1);
	bf_set(lpfc_sli4_sge_offset, sgl, 0);
	bf_set(lpfc_sli4_sge_type, sgl, LPFC_SGE_TYPE_DATA);
	sgl->word2 = cpu_to_le32(sgl->word2);
	sgl->sge_len = cpu_to_le32(LPFC_DATA_SIZE);
	sgl++;

	/* Empty SGE.  Clear to prevent stale data errors. */
	sgl->addr_hi = 0;
	sgl->addr_lo = 0;
	sgl->word2 = 0;
	sgl->sge_len = 0;

	/* Finish prepping the command, response and scsi command. */
	memset(lpfc_cmd->fcp_cmnd, 0, sizeof(struct fcp_cmnd));
	memset((void *)lpfc_cmd->fcp_rsp, 0, sizeof(struct fcp_rsp));
	err = lpfc_init_scsi_cmd(vport, lpfc_cmd, cmd_req, LPFC_DATA_SIZE);
	if (err) {
		err = -EPERM;
		goto out_err;
	}

	/* This is an FCP IO the driver tracks so don't let it stay
	 * outstanding forever - provide a generous 16 sec TMO.
	 */
	lpfc_cmd->cur_iocbq.iocb_flag |= LPFC_IO_FCP;
	lpfc_cmd->cur_iocbq.context1 = lpfc_cmd;
	lpfc_cmd->cur_iocbq.vport = vport;
	lpfc_cmd->timeout = LPFC_DRVR_TIMEOUT;
	lpfc_cmd->pCmd = NULL;
	lpfc_cmd->seg_cnt = 1;

	/* Get an ndlp reference.  Ignore rdata as this IO is driver owned. */
	lpfc_cmd->ndlp = lpfc_nlp_get(ndlp);
	if (!lpfc_cmd->ndlp) {
		err = -EINVAL;
		goto out_err;
	}

	iocb_cmd = &lpfc_cmd->cur_iocbq.iocb;
	iocb_cmd->un.fcpi.fcpi_parm = LPFC_DATA_SIZE;
	iocb_cmd->ulpContext = phba->sli4_hba.rpi_ids[ndlp->nlp_rpi];
	if (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE)
		iocb_cmd->ulpFCP2Rcvy = 1;
	iocb_cmd->ulpClass = (ndlp->nlp_fcp_info & 0x0f);
	iocb_cmd->ulpTimeout = lpfc_cmd->timeout;

	err = lpfc_sli_issue_iocb(phba, LPFC_FCP_RING, &lpfc_cmd->cur_iocbq,
				  SLI_IOCB_RET_IOCB);
	if (err) {
		err = -EIO;
		lpfc_nlp_put(ndlp);
		goto out_err;
	}

	lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP | LOG_FCP_ERROR,
			 "6333 Issue cmd x%x to 0x%06x status %d "
			 "flag x%x iotag x%x xri x%x\n",
			 cmd_req->cmd_id, ndlp->nlp_DID, err,
			 lpfc_cmd->cur_iocbq.iocb_flag,
			 lpfc_cmd->cur_iocbq.iotag,
			 lpfc_cmd->cur_iocbq.sli4_xritag);

	if (phba->cfg_xri_rebalancing)
		lpfc_keep_pvt_pool_above_lowwm(phba, lpfc_cmd->hdwq_no);
	return 0;

 out_err:
	lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP | LOG_FCP_ERROR,
			 "6337 Could not issue cmd x%x err %d\n",
			 cmd_req->cmd_id, err);
	lpfc_cmd->cur_iocbq.iocb_flag &= ~LPFC_IO_DRVR_INIT;
	lpfc_cmd->rdata = NULL;
	lpfc_release_scsi_buf(phba, lpfc_cmd);
	return err;
}

/**
 * lpfc_abort_handler - scsi_host_template eh_abort_handler entry point
 * @cmnd: Pointer to scsi_cmnd data structure.
 *
 * This routine aborts @cmnd pending in base driver.
 *
 * Return code :
 *   0x2003 - Error
 *   0x2002 - Success
 **/
static int
lpfc_abort_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host  *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_iocbq *iocb;
	struct lpfc_iocbq *abtsiocb;
	struct lpfc_io_buf *lpfc_cmd;
	IOCB_t *cmd, *icmd;
	int ret = SUCCESS, status = 0;
	struct lpfc_sli_ring *pring_s4 = NULL;
	int ret_val;
	unsigned long flags;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(waitq);

	status = fc_block_scsi_eh(cmnd);
	if (status != 0 && status != SUCCESS)
		return status;

	lpfc_cmd = (struct lpfc_io_buf *)cmnd->host_scribble;
	if (!lpfc_cmd)
		return ret;

	spin_lock_irqsave(&phba->hbalock, flags);
	/* driver queued commands are in process of being flushed */
	if (phba->hba_flag & HBA_IOQ_FLUSH) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
			"3168 SCSI Layer abort requested I/O has been "
			"flushed by LLD.\n");
		ret = FAILED;
		goto out_unlock;
	}

	/* Guard against IO completion being called at same time */
	spin_lock(&lpfc_cmd->buf_lock);

	if (!lpfc_cmd->pCmd) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
			 "2873 SCSI Layer I/O Abort Request IO CMPL Status "
			 "x%x ID %d LUN %llu\n",
			 SUCCESS, cmnd->device->id, cmnd->device->lun);
		goto out_unlock_buf;
	}

	iocb = &lpfc_cmd->cur_iocbq;
	if (phba->sli_rev == LPFC_SLI_REV4) {
		pring_s4 = phba->sli4_hba.hdwq[iocb->hba_wqidx].io_wq->pring;
		if (!pring_s4) {
			ret = FAILED;
			goto out_unlock_buf;
		}
		spin_lock(&pring_s4->ring_lock);
	}
	/* the command is in process of being cancelled */
	if (!(iocb->iocb_flag & LPFC_IO_ON_TXCMPLQ)) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
			"3169 SCSI Layer abort requested I/O has been "
			"cancelled by LLD.\n");
		ret = FAILED;
		goto out_unlock_ring;
	}
	/*
	 * If pCmd field of the corresponding lpfc_io_buf structure
	 * points to a different SCSI command, then the driver has
	 * already completed this command, but the midlayer did not
	 * see the completion before the eh fired. Just return SUCCESS.
	 */
	if (lpfc_cmd->pCmd != cmnd) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
			"3170 SCSI Layer abort requested I/O has been "
			"completed by LLD.\n");
		goto out_unlock_ring;
	}

	BUG_ON(iocb->context1 != lpfc_cmd);

	/* abort issued in recovery is still in progress */
	if (iocb->iocb_flag & LPFC_DRIVER_ABORTED) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
			 "3389 SCSI Layer I/O Abort Request is pending\n");
		if (phba->sli_rev == LPFC_SLI_REV4)
			spin_unlock(&pring_s4->ring_lock);
		spin_unlock(&lpfc_cmd->buf_lock);
		spin_unlock_irqrestore(&phba->hbalock, flags);
		goto wait_for_cmpl;
	}

	abtsiocb = __lpfc_sli_get_iocbq(phba);
	if (abtsiocb == NULL) {
		ret = FAILED;
		goto out_unlock_ring;
	}

	/* Indicate the IO is being aborted by the driver. */
	iocb->iocb_flag |= LPFC_DRIVER_ABORTED;

	/*
	 * The scsi command can not be in txq and it is in flight because the
	 * pCmd is still pointig at the SCSI command we have to abort. There
	 * is no need to search the txcmplq. Just send an abort to the FW.
	 */

	cmd = &iocb->iocb;
	icmd = &abtsiocb->iocb;
	icmd->un.acxri.abortType = ABORT_TYPE_ABTS;
	icmd->un.acxri.abortContextTag = cmd->ulpContext;
	if (phba->sli_rev == LPFC_SLI_REV4)
		icmd->un.acxri.abortIoTag = iocb->sli4_xritag;
	else
		icmd->un.acxri.abortIoTag = cmd->ulpIoTag;

	icmd->ulpLe = 1;
	icmd->ulpClass = cmd->ulpClass;

	/* ABTS WQE must go to the same WQ as the WQE to be aborted */
	abtsiocb->hba_wqidx = iocb->hba_wqidx;
	abtsiocb->iocb_flag |= LPFC_USE_FCPWQIDX;
	if (iocb->iocb_flag & LPFC_IO_FOF)
		abtsiocb->iocb_flag |= LPFC_IO_FOF;

	if (lpfc_is_link_up(phba))
		icmd->ulpCommand = CMD_ABORT_XRI_CN;
	else
		icmd->ulpCommand = CMD_CLOSE_XRI_CN;

	abtsiocb->iocb_cmpl = lpfc_sli_abort_fcp_cmpl;
	abtsiocb->vport = vport;
	lpfc_cmd->waitq = &waitq;
	if (phba->sli_rev == LPFC_SLI_REV4) {
		/* Note: both hbalock and ring_lock must be set here */
		ret_val = __lpfc_sli_issue_iocb(phba, pring_s4->ringno,
						abtsiocb, 0);
		spin_unlock(&pring_s4->ring_lock);
	} else {
		ret_val = __lpfc_sli_issue_iocb(phba, LPFC_FCP_RING,
						abtsiocb, 0);
	}

	/* Make sure HBA is alive */
	lpfc_issue_hb_tmo(phba);

	if (ret_val == IOCB_ERROR) {
		/* Indicate the IO is not being aborted by the driver. */
		iocb->iocb_flag &= ~LPFC_DRIVER_ABORTED;
		lpfc_cmd->waitq = NULL;
		spin_unlock(&lpfc_cmd->buf_lock);
		spin_unlock_irqrestore(&phba->hbalock, flags);
		lpfc_sli_release_iocbq(phba, abtsiocb);
		ret = FAILED;
		goto out;
	}

	/* no longer need the lock after this point */
	spin_unlock(&lpfc_cmd->buf_lock);
	spin_unlock_irqrestore(&phba->hbalock, flags);

	if (phba->cfg_poll & DISABLE_FCP_RING_INT)
		lpfc_sli_handle_fast_ring_event(phba,
			&phba->sli.sli3_ring[LPFC_FCP_RING], HA_R0RE_REQ);

wait_for_cmpl:
	/*
	 * iocb_flag is set to LPFC_DRIVER_ABORTED before we wait
	 * for abort to complete.
	 */
	wait_event_timeout(waitq,
			  (lpfc_cmd->pCmd != cmnd),
			   msecs_to_jiffies(2*vport->cfg_devloss_tmo*1000));

	spin_lock(&lpfc_cmd->buf_lock);

	if (lpfc_cmd->pCmd == cmnd) {
		ret = FAILED;
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "0748 abort handler timed out waiting "
				 "for aborting I/O (xri:x%x) to complete: "
				 "ret %#x, ID %d, LUN %llu\n",
				 iocb->sli4_xritag, ret,
				 cmnd->device->id, cmnd->device->lun);
	}

	lpfc_cmd->waitq = NULL;

	spin_unlock(&lpfc_cmd->buf_lock);
	goto out;

out_unlock_ring:
	if (phba->sli_rev == LPFC_SLI_REV4)
		spin_unlock(&pring_s4->ring_lock);
out_unlock_buf:
	spin_unlock(&lpfc_cmd->buf_lock);
out_unlock:
	spin_unlock_irqrestore(&phba->hbalock, flags);
out:
	lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
			 "0749 SCSI Layer I/O Abort Request Status x%x ID %d "
			 "LUN %llu\n", ret, cmnd->device->id,
			 cmnd->device->lun);
	return ret;
}

static char *
lpfc_taskmgmt_name(uint8_t task_mgmt_cmd)
{
	switch (task_mgmt_cmd) {
	case FCP_ABORT_TASK_SET:
		return "ABORT_TASK_SET";
	case FCP_CLEAR_TASK_SET:
		return "FCP_CLEAR_TASK_SET";
	case FCP_BUS_RESET:
		return "FCP_BUS_RESET";
	case FCP_LUN_RESET:
		return "FCP_LUN_RESET";
	case FCP_TARGET_RESET:
		return "FCP_TARGET_RESET";
	case FCP_CLEAR_ACA:
		return "FCP_CLEAR_ACA";
	case FCP_TERMINATE_TASK:
		return "FCP_TERMINATE_TASK";
	default:
		return "unknown";
	}
}


/**
 * lpfc_check_fcp_rsp - check the returned fcp_rsp to see if task failed
 * @vport: The virtual port for which this call is being executed.
 * @lpfc_cmd: Pointer to lpfc_io_buf data structure.
 *
 * This routine checks the FCP RSP INFO to see if the tsk mgmt command succeded
 *
 * Return code :
 *   0x2003 - Error
 *   0x2002 - Success
 **/
static int
lpfc_check_fcp_rsp(struct lpfc_vport *vport, struct lpfc_io_buf *lpfc_cmd)
{
	struct fcp_rsp *fcprsp = lpfc_cmd->fcp_rsp;
	uint32_t rsp_info;
	uint32_t rsp_len;
	uint8_t  rsp_info_code;
	int ret = FAILED;


	if (fcprsp == NULL)
		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
				 "0703 fcp_rsp is missing\n");
	else {
		rsp_info = fcprsp->rspStatus2;
		rsp_len = be32_to_cpu(fcprsp->rspRspLen);
		rsp_info_code = fcprsp->rspInfo3;


		lpfc_optioned_vlog(vport, &vport->log,
				KERN_INFO, LOG_FCP,
				 "0706 fcp_rsp valid 0x%x,"
				 " rsp len=%d code 0x%x\n",
				 rsp_info,
				 rsp_len, rsp_info_code);

		/* If FCP_RSP_LEN_VALID bit is one, then the FCP_RSP_LEN
		 * field specifies the number of valid bytes of FCP_RSP_INFO.
		 * The FCP_RSP_LEN field shall be set to 0x04 or 0x08
		 */
		if ((fcprsp->rspStatus2 & RSP_LEN_VALID) &&
		    ((rsp_len == 8) || (rsp_len == 4))) {
			switch (rsp_info_code) {
			case RSP_NO_FAILURE:
				lpfc_optioned_vlog(vport, &vport->log,
						   KERN_INFO, LOG_FCP,
						   "0715 Task Mgmt No "
						   "Failure\n");
				ret = SUCCESS;
				break;
			case RSP_TM_NOT_SUPPORTED: /* TM rejected */
				lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
						 "0716 Task Mgmt Target "
						"reject\n");
				break;
			case RSP_TM_NOT_COMPLETED: /* TM failed */
				lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
						 "0717 Task Mgmt Target "
						"failed TM\n");
				break;
			case RSP_TM_INVALID_LU: /* TM to invalid LU! */
				lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
						 "0718 Task Mgmt to invalid "
						"LUN\n");
				break;
			}
		}
	}
	return ret;
}


/**
 * lpfc_send_taskmgmt - Generic SCSI Task Mgmt Handler
 * @vport: The virtual port for which this call is being executed.
 * @rdata: Pointer to remote port local data
 * @tgt_id: Target ID of remote device.
 * @lun_id: Lun number for the TMF
 * @task_mgmt_cmd: type of TMF to send
 *
 * This routine builds and sends a TMF (SCSI Task Mgmt Function) to
 * a remote port.
 *
 * Return Code:
 *   0x2003 - Error
 *   0x2002 - Success.
 **/
static int
lpfc_send_taskmgmt(struct lpfc_vport *vport, struct scsi_cmnd *cmnd,
		   unsigned  tgt_id, uint64_t lun_id,
		   uint8_t task_mgmt_cmd)
{
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_io_buf *lpfc_cmd;
	struct lpfc_iocbq *iocbq;
	struct lpfc_iocbq *iocbqrsp;
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *pnode;
	int ret;
	int status;

	rdata = lpfc_rport_data_from_scsi_device(cmnd->device);
	pnode = rdata->pnode;
	if (!pnode || !NLP_CHK_NODE_ACT(pnode))
		return FAILED;

	lpfc_cmd = lpfc_get_scsi_buf(phba, rdata->pnode, NULL);
	if (lpfc_cmd == NULL)
		return FAILED;
	lpfc_cmd->timeout = phba->cfg_task_mgmt_tmo;
	lpfc_cmd->rdata = rdata;
	lpfc_cmd->pCmd = cmnd;
	lpfc_cmd->ndlp = pnode;

	status = lpfc_scsi_prep_task_mgmt_cmd(vport, lpfc_cmd, lun_id,
					   task_mgmt_cmd);
	if (!status) {
		lpfc_release_scsi_buf(phba, lpfc_cmd);
		return FAILED;
	}

	iocbq = &lpfc_cmd->cur_iocbq;
	iocbqrsp = lpfc_sli_get_iocbq(phba);
	if (iocbqrsp == NULL) {
		lpfc_release_scsi_buf(phba, lpfc_cmd);
		return FAILED;
	}
	iocbq->iocb_cmpl = lpfc_tskmgmt_def_cmpl;

	lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
			 "0702 Issue %s to TGT %d LUN %llu "
			 "rpi x%x nlp_flag x%x Data: x%x x%x\n",
			 lpfc_taskmgmt_name(task_mgmt_cmd), tgt_id, lun_id,
			 pnode->nlp_rpi, pnode->nlp_flag, iocbq->sli4_xritag,
			 iocbq->iocb_flag);

	status = lpfc_sli_issue_iocb_wait(phba, LPFC_FCP_RING,
					  iocbq, iocbqrsp, lpfc_cmd->timeout);
	if ((status != IOCB_SUCCESS) ||
	    (iocbqrsp->iocb.ulpStatus != IOSTAT_SUCCESS)) {
		if (status != IOCB_SUCCESS ||
			iocbqrsp->iocb.ulpStatus != IOSTAT_FCP_RSP_ERROR)
			lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
					 "0727 TMF %s to TGT %d LUN %llu "
					 "failed (%d, %d) iocb_flag x%x\n",
					 lpfc_taskmgmt_name(task_mgmt_cmd),
					 tgt_id, lun_id,
					 iocbqrsp->iocb.ulpStatus,
					 iocbqrsp->iocb.un.ulpWord[4],
					 iocbq->iocb_flag);
		/* if ulpStatus != IOCB_SUCCESS, then status == IOCB_SUCCESS */
		if (status == IOCB_SUCCESS) {
			if (iocbqrsp->iocb.ulpStatus == IOSTAT_FCP_RSP_ERROR)
				/* Something in the FCP_RSP was invalid.
				 * Check conditions */
				ret = lpfc_check_fcp_rsp(vport, lpfc_cmd);
			else
				ret = FAILED;
		} else if ((status == IOCB_TIMEDOUT) ||
			   (status == IOCB_ABORTED)) {
			ret = TIMEOUT_ERROR;
		} else {
			ret = FAILED;
		}
	} else
		ret = SUCCESS;

	lpfc_sli_release_iocbq(phba, iocbqrsp);

	if (status != IOCB_TIMEDOUT)
		lpfc_release_scsi_buf(phba, lpfc_cmd);

	return ret;
}

/**
 * lpfc_chk_tgt_mapped -
 * @vport: The virtual port to check on
 * @cmnd: Pointer to scsi_cmnd data structure.
 *
 * This routine delays until the scsi target (aka rport) for the
 * command exists (is present and logged in) or we declare it non-existent.
 *
 * Return code :
 *  0x2003 - Error
 *  0x2002 - Success
 **/
static int
lpfc_chk_tgt_mapped(struct lpfc_vport *vport, struct scsi_cmnd *cmnd)
{
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *pnode;
	unsigned long later;

	rdata = lpfc_rport_data_from_scsi_device(cmnd->device);
	if (!rdata) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_FCP,
			"0797 Tgt Map rport failure: rdata x%px\n", rdata);
		return FAILED;
	}
	pnode = rdata->pnode;
	/*
	 * If target is not in a MAPPED state, delay until
	 * target is rediscovered or devloss timeout expires.
	 */
	later = msecs_to_jiffies(2 * vport->cfg_devloss_tmo * 1000) + jiffies;
	while (time_after(later, jiffies)) {
		if (!pnode || !NLP_CHK_NODE_ACT(pnode))
			return FAILED;
		if (pnode->nlp_state == NLP_STE_MAPPED_NODE)
			return SUCCESS;
		schedule_timeout_uninterruptible(msecs_to_jiffies(500));
		rdata = lpfc_rport_data_from_scsi_device(cmnd->device);
		if (!rdata)
			return FAILED;
		pnode = rdata->pnode;
	}
	if (!pnode || !NLP_CHK_NODE_ACT(pnode) ||
	    (pnode->nlp_state != NLP_STE_MAPPED_NODE))
		return FAILED;
	return SUCCESS;
}

/**
 * lpfc_reset_flush_io_context -
 * @vport: The virtual port (scsi_host) for the flush context
 * @tgt_id: If aborting by Target contect - specifies the target id
 * @lun_id: If aborting by Lun context - specifies the lun id
 * @context: specifies the context level to flush at.
 *
 * After a reset condition via TMF, we need to flush orphaned i/o
 * contexts from the adapter. This routine aborts any contexts
 * outstanding, then waits for their completions. The wait is
 * bounded by devloss_tmo though.
 *
 * Return code :
 *  0x2003 - Error
 *  0x2002 - Success
 **/
static int
lpfc_reset_flush_io_context(struct lpfc_vport *vport, uint16_t tgt_id,
			uint64_t lun_id, lpfc_ctx_cmd context)
{
	struct lpfc_hba   *phba = vport->phba;
	unsigned long later;
	int cnt;

	cnt = lpfc_sli_sum_iocb(vport, tgt_id, lun_id, context);
	if (cnt)
		lpfc_sli_abort_taskmgmt(vport,
					&phba->sli.sli3_ring[LPFC_FCP_RING],
					tgt_id, lun_id, context);
	later = msecs_to_jiffies(2 * vport->cfg_devloss_tmo * 1000) + jiffies;
	while (time_after(later, jiffies) && cnt) {
		schedule_timeout_uninterruptible(msecs_to_jiffies(20));
		cnt = lpfc_sli_sum_iocb(vport, tgt_id, lun_id, context);
	}
	if (cnt) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
			"0724 I/O flush failure for context %s : cnt x%x\n",
			((context == LPFC_CTX_LUN) ? "LUN" :
			 ((context == LPFC_CTX_TGT) ? "TGT" :
			  ((context == LPFC_CTX_HOST) ? "HOST" : "Unknown"))),
			cnt);
		return FAILED;
	}
	return SUCCESS;
}

/**
 * lpfc_device_reset_handler - scsi_host_template eh_device_reset entry point
 * @cmnd: Pointer to scsi_cmnd data structure.
 *
 * This routine does a device reset by sending a LUN_RESET task management
 * command.
 *
 * Return code :
 *  0x2003 - Error
 *  0x2002 - Success
 **/
static int
lpfc_device_reset_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host  *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *pnode;
	unsigned tgt_id = cmnd->device->id;
	uint64_t lun_id = cmnd->device->lun;
	struct lpfc_scsi_event_header scsi_event;
	int status = 0;
	u32 logit = LOG_FCP;

	rdata = lpfc_rport_data_from_scsi_device(cmnd->device);
	if (!rdata || !rdata->pnode) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "0798 Device Reset rdata failure: rdata x%px\n",
				 rdata);
		return FAILED;
	}
	pnode = rdata->pnode;
	status = fc_block_scsi_eh(cmnd);
	if (status != 0 && status != SUCCESS)
		return status;

	status = lpfc_chk_tgt_mapped(vport, cmnd);
	if (status == FAILED) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
			"0721 Device Reset rport failure: rdata x%px\n", rdata);
		return FAILED;
	}

	scsi_event.event_type = FC_REG_SCSI_EVENT;
	scsi_event.subcategory = LPFC_EVENT_LUNRESET;
	scsi_event.lun = lun_id;
	memcpy(scsi_event.wwpn, &pnode->nlp_portname, sizeof(struct lpfc_name));
	memcpy(scsi_event.wwnn, &pnode->nlp_nodename, sizeof(struct lpfc_name));

	fc_host_post_vendor_event(shost, fc_get_event_number(),
		sizeof(scsi_event), (char *)&scsi_event, LPFC_NL_VENDOR_ID);

	status = lpfc_send_taskmgmt(vport, cmnd, tgt_id, lun_id,
						FCP_LUN_RESET);
	if (status != SUCCESS)
		logit =  LOG_TRACE_EVENT;

	lpfc_printf_vlog(vport, KERN_ERR, logit,
			 "0713 SCSI layer issued Device Reset (%d, %llu) "
			 "return x%x\n", tgt_id, lun_id, status);

	/*
	 * We have to clean up i/o as : they may be orphaned by the TMF;
	 * or if the TMF failed, they may be in an indeterminate state.
	 * So, continue on.
	 * We will report success if all the i/o aborts successfully.
	 */
	if (status == SUCCESS)
		status = lpfc_reset_flush_io_context(vport, tgt_id, lun_id,
						LPFC_CTX_LUN);

	return status;
}

/**
 * lpfc_target_reset_handler - scsi_host_template eh_target_reset entry point
 * @cmnd: Pointer to scsi_cmnd data structure.
 *
 * This routine does a target reset by sending a TARGET_RESET task management
 * command.
 *
 * Return code :
 *  0x2003 - Error
 *  0x2002 - Success
 **/
static int
lpfc_target_reset_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host  *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_rport_data *rdata;
	struct lpfc_nodelist *pnode;
	unsigned tgt_id = cmnd->device->id;
	uint64_t lun_id = cmnd->device->lun;
	struct lpfc_scsi_event_header scsi_event;
	int status = 0;
	u32 logit = LOG_FCP;
	u32 dev_loss_tmo = vport->cfg_devloss_tmo;
	unsigned long flags;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(waitq);

	rdata = lpfc_rport_data_from_scsi_device(cmnd->device);
	if (!rdata || !rdata->pnode) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
				 "0799 Target Reset rdata failure: rdata x%px\n",
				 rdata);
		return FAILED;
	}
	pnode = rdata->pnode;
	status = fc_block_scsi_eh(cmnd);
	if (status != 0 && status != SUCCESS)
		return status;

	status = lpfc_chk_tgt_mapped(vport, cmnd);
	if (status == FAILED) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
			"0722 Target Reset rport failure: rdata x%px\n", rdata);
		if (pnode) {
			spin_lock_irqsave(shost->host_lock, flags);
			pnode->nlp_flag &= ~NLP_NPR_ADISC;
			pnode->nlp_fcp_info &= ~NLP_FCP_2_DEVICE;
			spin_unlock_irqrestore(shost->host_lock, flags);
		}
		lpfc_reset_flush_io_context(vport, tgt_id, lun_id,
					  LPFC_CTX_TGT);
		return FAST_IO_FAIL;
	}

	scsi_event.event_type = FC_REG_SCSI_EVENT;
	scsi_event.subcategory = LPFC_EVENT_TGTRESET;
	scsi_event.lun = 0;
	memcpy(scsi_event.wwpn, &pnode->nlp_portname, sizeof(struct lpfc_name));
	memcpy(scsi_event.wwnn, &pnode->nlp_nodename, sizeof(struct lpfc_name));

	fc_host_post_vendor_event(shost, fc_get_event_number(),
		sizeof(scsi_event), (char *)&scsi_event, LPFC_NL_VENDOR_ID);

	status = lpfc_send_taskmgmt(vport, cmnd, tgt_id, lun_id,
					FCP_TARGET_RESET);
	if (status != SUCCESS) {
		logit = LOG_TRACE_EVENT;

		/* Issue LOGO, if no LOGO is outstanding */
		spin_lock_irqsave(shost->host_lock, flags);
		if (!(pnode->upcall_flags & NLP_WAIT_FOR_LOGO) &&
		    !pnode->logo_waitq) {
			pnode->logo_waitq = &waitq;
			pnode->nlp_fcp_info &= ~NLP_FCP_2_DEVICE;
			pnode->nlp_flag |= NLP_ISSUE_LOGO;
			pnode->upcall_flags |= NLP_WAIT_FOR_LOGO;
			spin_unlock_irqrestore(shost->host_lock, flags);
			lpfc_unreg_rpi(vport, pnode);
			wait_event_timeout(waitq,
					   (!(pnode->upcall_flags &
					      NLP_WAIT_FOR_LOGO)),
					   msecs_to_jiffies(dev_loss_tmo *
							    1000));

			if (pnode->upcall_flags & NLP_WAIT_FOR_LOGO) {
				lpfc_printf_vlog(vport, KERN_ERR, logit,
						 "0725 SCSI layer TGTRST failed"
						 " & LOGO TMO (%d, %llu) "
						 "return x%x\n",
						 tgt_id, lun_id, status);
				spin_lock_irqsave(shost->host_lock, flags);
				pnode->upcall_flags &= ~NLP_WAIT_FOR_LOGO;
			} else {
				spin_lock_irqsave(shost->host_lock, flags);
			}
			pnode->logo_waitq = NULL;
			spin_unlock_irqrestore(shost->host_lock, flags);
			status = SUCCESS;
		} else {

			spin_unlock_irqrestore(shost->host_lock, flags);
			status = FAILED;
 		}
	}

	lpfc_printf_vlog(vport, KERN_ERR, logit,
			 "0723 SCSI layer issued Target Reset (%d, %llu) "
			 "return x%x\n", tgt_id, lun_id, status);

	/*
	 * We have to clean up i/o as : they may be orphaned by the TMF;
	 * or if the TMF failed, they may be in an indeterminate state.
	 * So, continue on.
	 * We will report success if all the i/o aborts successfully.
	 */
	if (status == SUCCESS)
		status = lpfc_reset_flush_io_context(vport, tgt_id, lun_id,
					  LPFC_CTX_TGT);
	return status;
}

/**
 * lpfc_bus_reset_handler - scsi_host_template eh_bus_reset_handler entry point
 * @cmnd: Pointer to scsi_cmnd data structure.
 *
 * This routine does target reset to all targets on @cmnd->device->host.
 * This emulates Parallel SCSI Bus Reset Semantics.
 *
 * Return code :
 *  0x2003 - Error
 *  0x2002 - Success
 **/
static int
lpfc_bus_reset_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host  *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_nodelist *ndlp = NULL;
	struct lpfc_scsi_event_header scsi_event;
	int match;
	int ret = SUCCESS, status = SUCCESS, i;
	u32 logit = LOG_FCP;

	scsi_event.event_type = FC_REG_SCSI_EVENT;
	scsi_event.subcategory = LPFC_EVENT_BUSRESET;
	scsi_event.lun = 0;
	memcpy(scsi_event.wwpn, &vport->fc_portname, sizeof(struct lpfc_name));
	memcpy(scsi_event.wwnn, &vport->fc_nodename, sizeof(struct lpfc_name));

	fc_host_post_vendor_event(shost, fc_get_event_number(),
		sizeof(scsi_event), (char *)&scsi_event, LPFC_NL_VENDOR_ID);

	status = fc_block_scsi_eh(cmnd);
	if (status != 0 && status != SUCCESS)
		return status;

	/*
	 * Since the driver manages a single bus device, reset all
	 * targets known to the driver.  Should any target reset
	 * fail, this routine returns failure to the midlayer.
	 */
	for (i = 0; i < LPFC_MAX_TARGET; i++) {
		/* Search for mapped node by target ID */
		match = 0;
		spin_lock_irq(shost->host_lock);
		list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
			if (!NLP_CHK_NODE_ACT(ndlp))
				continue;
			if (vport->phba->cfg_fcp2_no_tgt_reset &&
			    (ndlp->nlp_fcp_info & NLP_FCP_2_DEVICE))
				continue;
			if (ndlp->nlp_state == NLP_STE_MAPPED_NODE &&
			    ndlp->nlp_sid == i &&
			    ndlp->rport &&
			    ndlp->nlp_type & NLP_FCP_TARGET) {
				match = 1;
				break;
			}
		}
		spin_unlock_irq(shost->host_lock);
		if (!match)
			continue;

		status = lpfc_send_taskmgmt(vport, cmnd,
					i, 0, FCP_TARGET_RESET);

		if (status != SUCCESS) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
					 "0700 Bus Reset on target %d failed\n",
					 i);
			ret = FAILED;
		}
	}
	/*
	 * We have to clean up i/o as : they may be orphaned by the TMFs
	 * above; or if any of the TMFs failed, they may be in an
	 * indeterminate state.
	 * We will report success if all the i/o aborts successfully.
	 */

	status = lpfc_reset_flush_io_context(vport, 0, 0, LPFC_CTX_HOST);
	if (status != SUCCESS)
		ret = FAILED;
	if (ret == FAILED)
		logit =  LOG_TRACE_EVENT;

	lpfc_printf_vlog(vport, KERN_ERR, logit,
			 "0714 SCSI layer issued Bus Reset Data: x%x\n", ret);
	return ret;
}

/**
 * lpfc_host_reset_handler - scsi_host_template eh_host_reset_handler entry pt
 * @cmnd: Pointer to scsi_cmnd data structure.
 *
 * This routine does host reset to the adaptor port. It brings the HBA
 * offline, performs a board restart, and then brings the board back online.
 * The lpfc_offline calls lpfc_sli_hba_down which will abort and local
 * reject all outstanding SCSI commands to the host and error returned
 * back to SCSI mid-level. As this will be SCSI mid-level's last resort
 * of error handling, it will only return error if resetting of the adapter
 * is not successful; in all other cases, will return success.
 *
 * Return code :
 *  0x2003 - Error
 *  0x2002 - Success
 **/
static int
lpfc_host_reset_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host *shost = cmnd->device->host;
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	int rc, ret = SUCCESS;

	lpfc_printf_vlog(vport, KERN_ERR, LOG_FCP,
			 "3172 SCSI layer issued Host Reset Data:\n");

	lpfc_offline_prep(phba, LPFC_MBX_WAIT);
	lpfc_offline(phba);
	rc = lpfc_sli_brdrestart(phba);
	if (rc)
		goto error;

	rc = lpfc_online(phba);
	if (rc)
		goto error;

	lpfc_unblock_mgmt_io(phba);

	return ret;
error:
	lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
			 "3323 Failed host reset\n");
	lpfc_unblock_mgmt_io(phba);
	return FAILED;
}

/**
 * lpfc_slave_alloc - scsi_host_template slave_alloc entry point
 * @sdev: Pointer to scsi_device.
 *
 * This routine populates the cmds_per_lun count + 2 scsi_bufs into  this host's
 * globally available list of scsi buffers. This routine also makes sure scsi
 * buffer is not allocated more than HBA limit conveyed to midlayer. This list
 * of scsi buffer exists for the lifetime of the driver.
 *
 * Return codes:
 *   non-0 - Error
 *   0 - Success
 **/
static int
lpfc_slave_alloc(struct scsi_device *sdev)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) sdev->host->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));
	uint32_t total = 0;
	uint32_t num_to_alloc = 0;
	int num_allocated = 0;
	uint32_t sdev_cnt;
	struct lpfc_device_data *device_data;
	unsigned long flags;
	struct lpfc_name target_wwpn;

	if (!rport || fc_remote_port_chkready(rport))
		return -ENXIO;

	if (phba->cfg_fof) {

		/*
		 * Check to see if the device data structure for the lun
		 * exists.  If not, create one.
		 */

		u64_to_wwn(rport->port_name, target_wwpn.u.wwn);
		spin_lock_irqsave(&phba->devicelock, flags);
		device_data = __lpfc_get_device_data(phba,
						     &phba->luns,
						     &vport->fc_portname,
						     &target_wwpn,
						     sdev->lun);
		if (!device_data) {
			spin_unlock_irqrestore(&phba->devicelock, flags);
			device_data = lpfc_create_device_data(phba,
							&vport->fc_portname,
							&target_wwpn,
							sdev->lun,
							phba->cfg_XLanePriority,
							true);
			if (!device_data)
				return -ENOMEM;
			spin_lock_irqsave(&phba->devicelock, flags);
			list_add_tail(&device_data->listentry, &phba->luns);
		}
		device_data->rport_data = rport->dd_data;
		device_data->available = true;
		spin_unlock_irqrestore(&phba->devicelock, flags);
		sdev->hostdata = device_data;
	} else {
		sdev->hostdata = rport->dd_data;
	}
	sdev_cnt = atomic_inc_return(&phba->sdev_cnt);

	/* For SLI4, all IO buffers are pre-allocated */
	if (phba->sli_rev == LPFC_SLI_REV4)
		return 0;

	/* This code path is now ONLY for SLI3 adapters */

	/*
	 * Populate the cmds_per_lun count scsi_bufs into this host's globally
	 * available list of scsi buffers.  Don't allocate more than the
	 * HBA limit conveyed to the midlayer via the host structure.  The
	 * formula accounts for the lun_queue_depth + error handlers + 1
	 * extra.  This list of scsi bufs exists for the lifetime of the driver.
	 */
	total = phba->total_scsi_bufs;
	num_to_alloc = vport->cfg_lun_queue_depth + 2;

	/* If allocated buffers are enough do nothing */
	if ((sdev_cnt * (vport->cfg_lun_queue_depth + 2)) < total)
		return 0;

	/* Allow some exchanges to be available always to complete discovery */
	if (total >= phba->cfg_hba_queue_depth - LPFC_DISC_IOCB_BUFF_COUNT ) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
				 "0704 At limitation of %d preallocated "
				 "command buffers\n", total);
		return 0;
	/* Allow some exchanges to be available always to complete discovery */
	} else if (total + num_to_alloc >
		phba->cfg_hba_queue_depth - LPFC_DISC_IOCB_BUFF_COUNT ) {
		lpfc_printf_vlog(vport, KERN_WARNING, LOG_FCP,
				 "0705 Allocation request of %d "
				 "command buffers will exceed max of %d.  "
				 "Reducing allocation request to %d.\n",
				 num_to_alloc, phba->cfg_hba_queue_depth,
				 (phba->cfg_hba_queue_depth - total));
		num_to_alloc = phba->cfg_hba_queue_depth - total;
	}
	num_allocated = lpfc_new_scsi_buf_s3(vport, num_to_alloc);
	if (num_to_alloc != num_allocated) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_TRACE_EVENT,
					 "0708 Allocation request of %d "
					 "command buffers did not succeed.  "
					 "Allocated %d buffers.\n",
					 num_to_alloc, num_allocated);
	}
	if (num_allocated > 0)
		phba->total_scsi_bufs += num_allocated;
	return 0;
}

/**
 * lpfc_slave_configure - scsi_host_template slave_configure entry point
 * @sdev: Pointer to scsi_device.
 *
 * This routine configures following items
 *   - Tag command queuing support for @sdev if supported.
 *   - Dev loss time out value of fc_rport.
 *   - Enable SLI polling for fcp ring if ENABLE_FCP_RING_POLLING flag is set.
 *
 * Return codes:
 *   0 - Success
 **/
static int
lpfc_slave_configure(struct scsi_device *sdev)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) sdev->host->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct fc_rport   *rport = starget_to_rport(sdev->sdev_target);

	/*
	 * Initialize the fc transport attributes for the target
	 * containing this scsi device.  Also note that the driver's
	 * target pointer is stored in the starget_data for the
	 * driver's sysfs entry point functions.
	 */
	rport->dev_loss_tmo = vport->cfg_devloss_tmo;

	scsi_change_queue_depth(sdev, vport->cfg_lun_queue_depth);
	if (phba->cfg_poll & ENABLE_FCP_RING_POLLING) {
		lpfc_sli_handle_fast_ring_event(phba,
			&phba->sli.sli3_ring[LPFC_FCP_RING], HA_R0RE_REQ);
		if (phba->cfg_poll & DISABLE_FCP_RING_INT)
			lpfc_poll_rearm_timer(phba);
	}

	return 0;
}

/**
 * lpfc_slave_destroy - slave_destroy entry point of SHT data structure
 * @sdev: Pointer to scsi_device.
 *
 * This routine sets @sdev hostatdata filed to null.
 **/
static void
lpfc_slave_destroy(struct scsi_device *sdev)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) sdev->host->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	unsigned long flags;
	struct lpfc_device_data *device_data = sdev->hostdata;

	atomic_dec(&phba->sdev_cnt);
	if ((phba->cfg_fof) && (device_data)) {
		spin_lock_irqsave(&phba->devicelock, flags);
		device_data->available = false;
		if (!device_data->oas_enabled)
			lpfc_delete_device_data(phba, device_data);
		spin_unlock_irqrestore(&phba->devicelock, flags);
	}
	sdev->hostdata = NULL;
	return;
}

/**
 * lpfc_create_device_data - creates and initializes device data structure for OAS
 * @pha: Pointer to host bus adapter structure.
 * @vport_wwpn: Pointer to vport's wwpn information
 * @target_wwpn: Pointer to target's wwpn information
 * @lun: Lun on target
 * @atomic_create: Flag to indicate if memory should be allocated using the
 *		  GFP_ATOMIC flag or not.
 *
 * This routine creates a device data structure which will contain identifying
 * information for the device (host wwpn, target wwpn, lun), state of OAS,
 * whether or not the corresponding lun is available by the system,
 * and pointer to the rport data.
 *
 * Return codes:
 *   NULL - Error
 *   Pointer to lpfc_device_data - Success
 **/
struct lpfc_device_data*
lpfc_create_device_data(struct lpfc_hba *phba, struct lpfc_name *vport_wwpn,
			struct lpfc_name *target_wwpn, uint64_t lun,
			uint32_t pri, bool atomic_create)
{

	struct lpfc_device_data *lun_info;
	int memory_flags;

	if (unlikely(!phba) || !vport_wwpn || !target_wwpn  ||
	    !(phba->cfg_fof))
		return NULL;

	/* Attempt to create the device data to contain lun info */

	if (atomic_create)
		memory_flags = GFP_ATOMIC;
	else
		memory_flags = GFP_KERNEL;
	lun_info = mempool_alloc(phba->device_data_mem_pool, memory_flags);
	if (!lun_info)
		return NULL;
	INIT_LIST_HEAD(&lun_info->listentry);
	lun_info->rport_data  = NULL;
	memcpy(&lun_info->device_id.vport_wwpn, vport_wwpn,
	       sizeof(struct lpfc_name));
	memcpy(&lun_info->device_id.target_wwpn, target_wwpn,
	       sizeof(struct lpfc_name));
	lun_info->device_id.lun = lun;
	lun_info->oas_enabled = false;
	lun_info->priority = pri;
	lun_info->available = false;
	return lun_info;
}

/**
 * lpfc_delete_device_data - frees a device data structure for OAS
 * @pha: Pointer to host bus adapter structure.
 * @lun_info: Pointer to device data structure to free.
 *
 * This routine frees the previously allocated device data structure passed.
 *
 **/
void
lpfc_delete_device_data(struct lpfc_hba *phba,
			struct lpfc_device_data *lun_info)
{

	if (unlikely(!phba) || !lun_info  ||
	    !(phba->cfg_fof))
		return;

	if (!list_empty(&lun_info->listentry))
		list_del(&lun_info->listentry);
	mempool_free(lun_info, phba->device_data_mem_pool);
	return;
}

/**
 * __lpfc_get_device_data - returns the device data for the specified lun
 * @pha: Pointer to host bus adapter structure.
 * @list: Point to list to search.
 * @vport_wwpn: Pointer to vport's wwpn information
 * @target_wwpn: Pointer to target's wwpn information
 * @lun: Lun on target
 *
 * This routine searches the list passed for the specified lun's device data.
 * This function does not hold locks, it is the responsibility of the caller
 * to ensure the proper lock is held before calling the function.
 *
 * Return codes:
 *   NULL - Error
 *   Pointer to lpfc_device_data - Success
 **/
struct lpfc_device_data*
__lpfc_get_device_data(struct lpfc_hba *phba, struct list_head *list,
		       struct lpfc_name *vport_wwpn,
		       struct lpfc_name *target_wwpn, uint64_t lun)
{

	struct lpfc_device_data *lun_info;

	if (unlikely(!phba) || !list || !vport_wwpn || !target_wwpn ||
	    !phba->cfg_fof)
		return NULL;

	/* Check to see if the lun is already enabled for OAS. */

	list_for_each_entry(lun_info, list, listentry) {
		if ((memcmp(&lun_info->device_id.vport_wwpn, vport_wwpn,
			    sizeof(struct lpfc_name)) == 0) &&
		    (memcmp(&lun_info->device_id.target_wwpn, target_wwpn,
			    sizeof(struct lpfc_name)) == 0) &&
		    (lun_info->device_id.lun == lun))
			return lun_info;
	}

	return NULL;
}

/**
 * lpfc_find_next_oas_lun - searches for the next oas lun
 * @pha: Pointer to host bus adapter structure.
 * @vport_wwpn: Pointer to vport's wwpn information
 * @target_wwpn: Pointer to target's wwpn information
 * @starting_lun: Pointer to the lun to start searching for
 * @found_vport_wwpn: Pointer to the found lun's vport wwpn information
 * @found_target_wwpn: Pointer to the found lun's target wwpn information
 * @found_lun: Pointer to the found lun.
 * @found_lun_status: Pointer to status of the found lun.
 *
 * This routine searches the luns list for the specified lun
 * or the first lun for the vport/target.  If the vport wwpn contains
 * a zero value then a specific vport is not specified. In this case
 * any vport which contains the lun will be considered a match.  If the
 * target wwpn contains a zero value then a specific target is not specified.
 * In this case any target which contains the lun will be considered a
 * match.  If the lun is found, the lun, vport wwpn, target wwpn and lun status
 * are returned.  The function will also return the next lun if available.
 * If the next lun is not found, starting_lun parameter will be set to
 * NO_MORE_OAS_LUN.
 *
 * Return codes:
 *   non-0 - Error
 *   0 - Success
 **/
bool
lpfc_find_next_oas_lun(struct lpfc_hba *phba, struct lpfc_name *vport_wwpn,
		       struct lpfc_name *target_wwpn, uint64_t *starting_lun,
		       struct lpfc_name *found_vport_wwpn,
		       struct lpfc_name *found_target_wwpn,
		       uint64_t *found_lun,
		       uint32_t *found_lun_status,
		       uint32_t *found_lun_pri)
{

	unsigned long flags;
	struct lpfc_device_data *lun_info;
	struct lpfc_device_id *device_id;
	uint64_t lun;
	bool found = false;

	if (unlikely(!phba) || !vport_wwpn || !target_wwpn ||
	    !starting_lun || !found_vport_wwpn ||
	    !found_target_wwpn || !found_lun || !found_lun_status ||
	    (*starting_lun == NO_MORE_OAS_LUN) ||
	    !phba->cfg_fof)
		return false;

	lun = *starting_lun;
	*found_lun = NO_MORE_OAS_LUN;
	*starting_lun = NO_MORE_OAS_LUN;

	/* Search for lun or the lun closet in value */

	spin_lock_irqsave(&phba->devicelock, flags);
	list_for_each_entry(lun_info, &phba->luns, listentry) {
		if (((wwn_to_u64(vport_wwpn->u.wwn) == 0) ||
		     (memcmp(&lun_info->device_id.vport_wwpn, vport_wwpn,
			    sizeof(struct lpfc_name)) == 0)) &&
		    ((wwn_to_u64(target_wwpn->u.wwn) == 0) ||
		     (memcmp(&lun_info->device_id.target_wwpn, target_wwpn,
			    sizeof(struct lpfc_name)) == 0)) &&
		    (lun_info->oas_enabled)) {
			device_id = &lun_info->device_id;
			if ((!found) &&
			    ((lun == FIND_FIRST_OAS_LUN) ||
			     (device_id->lun == lun))) {
				*found_lun = device_id->lun;
				memcpy(found_vport_wwpn,
				       &device_id->vport_wwpn,
				       sizeof(struct lpfc_name));
				memcpy(found_target_wwpn,
				       &device_id->target_wwpn,
				       sizeof(struct lpfc_name));
				if (lun_info->available)
					*found_lun_status =
						OAS_LUN_STATUS_EXISTS;
				else
					*found_lun_status = 0;
				*found_lun_pri = lun_info->priority;
				if (phba->cfg_oas_flags & OAS_FIND_ANY_VPORT)
					memset(vport_wwpn, 0x0,
					       sizeof(struct lpfc_name));
				if (phba->cfg_oas_flags & OAS_FIND_ANY_TARGET)
					memset(target_wwpn, 0x0,
					       sizeof(struct lpfc_name));
				found = true;
			} else if (found) {
				*starting_lun = device_id->lun;
				memcpy(vport_wwpn, &device_id->vport_wwpn,
				       sizeof(struct lpfc_name));
				memcpy(target_wwpn, &device_id->target_wwpn,
				       sizeof(struct lpfc_name));
				break;
			}
		}
	}
	spin_unlock_irqrestore(&phba->devicelock, flags);
	return found;
}

/**
 * lpfc_enable_oas_lun - enables a lun for OAS operations
 * @pha: Pointer to host bus adapter structure.
 * @vport_wwpn: Pointer to vport's wwpn information
 * @target_wwpn: Pointer to target's wwpn information
 * @lun: Lun
 *
 * This routine enables a lun for oas operations.  The routines does so by
 * doing the following :
 *
 *   1) Checks to see if the device data for the lun has been created.
 *   2) If found, sets the OAS enabled flag if not set and returns.
 *   3) Otherwise, creates a device data structure.
 *   4) If successfully created, indicates the device data is for an OAS lun,
 *   indicates the lun is not available and add to the list of luns.
 *
 * Return codes:
 *   false - Error
 *   true - Success
 **/
bool
lpfc_enable_oas_lun(struct lpfc_hba *phba, struct lpfc_name *vport_wwpn,
		    struct lpfc_name *target_wwpn, uint64_t lun, uint8_t pri)
{

	struct lpfc_device_data *lun_info;
	unsigned long flags;

	if (unlikely(!phba) || !vport_wwpn || !target_wwpn ||
	    !phba->cfg_fof)
		return false;

	spin_lock_irqsave(&phba->devicelock, flags);

	/* Check to see if the device data for the lun has been created */
	lun_info = __lpfc_get_device_data(phba, &phba->luns, vport_wwpn,
					  target_wwpn, lun);
	if (lun_info) {
		if (!lun_info->oas_enabled)
			lun_info->oas_enabled = true;
		lun_info->priority = pri;
		spin_unlock_irqrestore(&phba->devicelock, flags);
		return true;
	}

	/* Create an lun info structure and add to list of luns */
	lun_info = lpfc_create_device_data(phba, vport_wwpn, target_wwpn, lun,
					   pri, true);
	if (lun_info) {
		lun_info->oas_enabled = true;
		lun_info->priority = pri;
		lun_info->available = false;
		list_add_tail(&lun_info->listentry, &phba->luns);
		spin_unlock_irqrestore(&phba->devicelock, flags);
		return true;
	}
	spin_unlock_irqrestore(&phba->devicelock, flags);
	return false;
}

/**
 * lpfc_disable_oas_lun - disables a lun for OAS operations
 * @pha: Pointer to host bus adapter structure.
 * @vport_wwpn: Pointer to vport's wwpn information
 * @target_wwpn: Pointer to target's wwpn information
 * @lun: Lun
 *
 * This routine disables a lun for oas operations.  The routines does so by
 * doing the following :
 *
 *   1) Checks to see if the device data for the lun is created.
 *   2) If present, clears the flag indicating this lun is for OAS.
 *   3) If the lun is not available by the system, the device data is
 *   freed.
 *
 * Return codes:
 *   false - Error
 *   true - Success
 **/
bool
lpfc_disable_oas_lun(struct lpfc_hba *phba, struct lpfc_name *vport_wwpn,
		     struct lpfc_name *target_wwpn, uint64_t lun, uint8_t pri)
{

	struct lpfc_device_data *lun_info;
	unsigned long flags;

	if (unlikely(!phba) || !vport_wwpn || !target_wwpn ||
	    !phba->cfg_fof)
		return false;

	spin_lock_irqsave(&phba->devicelock, flags);

	/* Check to see if the lun is available. */
	lun_info = __lpfc_get_device_data(phba,
					  &phba->luns, vport_wwpn,
					  target_wwpn, lun);
	if (lun_info) {
		lun_info->oas_enabled = false;
		lun_info->priority = pri;
		if (!lun_info->available)
			lpfc_delete_device_data(phba, lun_info);
		spin_unlock_irqrestore(&phba->devicelock, flags);
		return true;
	}

	spin_unlock_irqrestore(&phba->devicelock, flags);
	return false;
}

static int
lpfc_no_command(struct Scsi_Host *shost, struct scsi_cmnd *cmnd)
{
	return SCSI_MLQUEUE_HOST_BUSY;
}

static int
lpfc_no_handler(struct scsi_cmnd *cmnd)
{
	return FAILED;
}

static int
lpfc_no_slave(struct scsi_device *sdev)
{
	return -ENODEV;
}

struct scsi_host_template lpfc_template_nvme = {
	.module			= THIS_MODULE,
	.name			= LPFC_DRIVER_NAME,
	.proc_name		= LPFC_DRIVER_NAME,
	.info			= lpfc_info,
	.queuecommand		= lpfc_no_command,
	.eh_abort_handler	= lpfc_no_handler,
	.eh_device_reset_handler = lpfc_no_handler,
	.eh_target_reset_handler = lpfc_no_handler,
	.eh_bus_reset_handler	= lpfc_no_handler,
	.eh_host_reset_handler  = lpfc_no_handler,
	.slave_alloc		= lpfc_no_slave,
	.slave_configure	= lpfc_no_slave,
	.scan_finished		= lpfc_scan_finished,
	.this_id		= -1,
	.sg_tablesize		= 1,
	.cmd_per_lun		= 1,
#if (KERNEL_MAJOR < 5)
	.use_clustering		= ENABLE_CLUSTERING,
#endif
	.shost_attrs		= lpfc_hba_attrs,
	.max_sectors		= 0xFFFF,
	.vendor_id		= LPFC_NL_VENDOR_ID,
	.track_queue_depth	= 0,
};

struct scsi_host_template lpfc_template = {
	.module			= THIS_MODULE,
	.name			= LPFC_DRIVER_NAME,
	.proc_name		= LPFC_DRIVER_NAME,
	.info			= lpfc_info,
	.queuecommand		= lpfc_queuecommand,
	.eh_abort_handler	= lpfc_abort_handler,
	.eh_device_reset_handler = lpfc_device_reset_handler,
	.eh_target_reset_handler = lpfc_target_reset_handler,
	.eh_bus_reset_handler	= lpfc_bus_reset_handler,
	.eh_host_reset_handler  = lpfc_host_reset_handler,
	.slave_alloc		= lpfc_slave_alloc,
	.slave_configure	= lpfc_slave_configure,
	.slave_destroy		= lpfc_slave_destroy,
	.scan_finished		= lpfc_scan_finished,
	.this_id		= -1,
	.sg_tablesize		= LPFC_DEFAULT_SG_SEG_CNT,
	.cmd_per_lun		= LPFC_CMD_PER_LUN,
#if (KERNEL_MAJOR < 5)
	.use_clustering		= ENABLE_CLUSTERING,
#endif
	.shost_attrs		= lpfc_hba_attrs,
	.max_sectors		= 0xFFFFFFFF,
	.vendor_id		= LPFC_NL_VENDOR_ID,
	.change_queue_depth	= scsi_change_queue_depth,
	.track_queue_depth	= 1,
};
