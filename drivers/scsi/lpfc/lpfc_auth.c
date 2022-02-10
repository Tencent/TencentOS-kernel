/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2018-2020 Broadcom. All Rights Reserved. The term *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.     *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
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
/* See Fibre Channel protocol T11 FC-SP-2 for details */
#include <linux/blkdev.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/mpi.h>
#include <crypto/hash.h>

#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"
#include "lpfc_debugfs.h"
#include "lpfc_auth.h"

struct dh_group dh_group_array[5] = {
	{
		DH_GROUP_NULL, 0,
		{
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		}
	},
	{
		DH_GROUP_1024, 128,
		{
			0xEE, 0xAF, 0x0A, 0xB9, 0xAD, 0xB3, 0x8D, 0xD6,
			0x9C, 0x33, 0xF8, 0x0A, 0xFA, 0x8F, 0xC5, 0xE8,
			0x60, 0x72, 0x61, 0x87, 0x75, 0xFF, 0x3C, 0x0B,
			0x9E, 0xA2, 0x31, 0x4C, 0x9C, 0x25, 0x65, 0x76,
			0xD6, 0x74, 0xDF, 0x74, 0x96, 0xEA, 0x81, 0xD3,
			0x38, 0x3B, 0x48, 0x13, 0xD6, 0x92, 0xC6, 0xE0,
			0xE0, 0xD5, 0xD8, 0xE2, 0x50, 0xB9, 0x8B, 0xE4,
			0x8E, 0x49, 0x5C, 0x1D, 0x60, 0x89, 0xDA, 0xD1,
			0x5D, 0xC7, 0xD7, 0xB4, 0x61, 0x54, 0xD6, 0xB6,
			0xCE, 0x8E, 0xF4, 0xAD, 0x69, 0xB1, 0x5D, 0x49,
			0x82, 0x55, 0x9B, 0x29, 0x7B, 0xCF, 0x18, 0x85,
			0xC5, 0x29, 0xF5, 0x66, 0x66, 0x0E, 0x57, 0xEC,
			0x68, 0xED, 0xBC, 0x3C, 0x05, 0x72, 0x6C, 0xC0,
			0x2F, 0xD4, 0xCB, 0xF4, 0x97, 0x6E, 0xAA, 0x9A,
			0xFD, 0x51, 0x38, 0xFE, 0x83, 0x76, 0x43, 0x5B,
			0x9F, 0xC6, 0x1D, 0x2F, 0xC0, 0xEB, 0x06, 0xE3,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		}
	},
	{
		DH_GROUP_1280, 160,
		{
			0xD7, 0x79, 0x46, 0x82, 0x6E, 0x81, 0x19, 0x14,
			0xB3, 0x94, 0x01, 0xD5, 0x6A, 0x0A, 0x78, 0x43,
			0xA8, 0xE7, 0x57, 0x5D, 0x73, 0x8C, 0x67, 0x2A,
			0x09, 0x0A, 0xB1, 0x18, 0x7D, 0x69, 0x0D, 0xC4,
			0x38, 0x72, 0xFC, 0x06, 0xA7, 0xB6, 0xA4, 0x3F,
			0x3B, 0x95, 0xBE, 0xAE, 0xC7, 0xDF, 0x04, 0xB9,
			0xD2, 0x42, 0xEB, 0xDC, 0x48, 0x11, 0x11, 0x28,
			0x32, 0x16, 0xCE, 0x81, 0x6E, 0x00, 0x4B, 0x78,
			0x6C, 0x5F, 0xCE, 0x85, 0x67, 0x80, 0xD4, 0x18,
			0x37, 0xD9, 0x5A, 0xD7, 0x87, 0xA5, 0x0B, 0xBE,
			0x90, 0xBD, 0x3A, 0x9C, 0x98, 0xAC, 0x0F, 0x5F,
			0xC0, 0xDE, 0x74, 0x4B, 0x1C, 0xDE, 0x18, 0x91,
			0x69, 0x08, 0x94, 0xBC, 0x1F, 0x65, 0xE0, 0x0D,
			0xE1, 0x5B, 0x4B, 0x2A, 0xA6, 0xD8, 0x71, 0x00,
			0xC9, 0xEC, 0xC2, 0x52, 0x7E, 0x45, 0xEB, 0x84,
			0x9D, 0xEB, 0x14, 0xBB, 0x20, 0x49, 0xB1, 0x63,
			0xEA, 0x04, 0x18, 0x7F, 0xD2, 0x7C, 0x1B, 0xD9,
			0xC7, 0x95, 0x8C, 0xD4, 0x0C, 0xE7, 0x06, 0x7A,
			0x9C, 0x02, 0x4F, 0x9B, 0x7C, 0x5A, 0x0B, 0x4F,
			0x50, 0x03, 0x68, 0x61, 0x61, 0xF0, 0x60, 0x5B,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		}
	},
	{
		DH_GROUP_1536, 192,
		{
			0x9D, 0xEF, 0x3C, 0xAF, 0xB9, 0x39, 0x27, 0x7A,
			0xB1, 0xF1, 0x2A, 0x86, 0x17, 0xA4, 0x7B, 0xBB,
			0xDB, 0xA5, 0x1D, 0xF4, 0x99, 0xAC, 0x4C, 0x80,
			0xBE, 0xEE, 0xA9, 0x61, 0x4B, 0x19, 0xCC, 0x4D,
			0x5F, 0x4F, 0x5F, 0x55, 0x6E, 0x27, 0xCB, 0xDE,
			0x51, 0xC6, 0xA9, 0x4B, 0xE4, 0x60, 0x7A, 0x29,
			0x15, 0x58, 0x90, 0x3B, 0xA0, 0xD0, 0xF8, 0x43,
			0x80, 0xB6, 0x55, 0xBB, 0x9A, 0x22, 0xE8, 0xDC,
			0xDF, 0x02, 0x8A, 0x7C, 0xEC, 0x67, 0xF0, 0xD0,
			0x81, 0x34, 0xB1, 0xC8, 0xB9, 0x79, 0x89, 0x14,
			0x9B, 0x60, 0x9E, 0x0B, 0xE3, 0xBA, 0xB6, 0x3D,
			0x47, 0x54, 0x83, 0x81, 0xDB, 0xC5, 0xB1, 0xFC,
			0x76, 0x4E, 0x3F, 0x4B, 0x53, 0xDD, 0x9D, 0xA1,
			0x15, 0x8B, 0xFD, 0x3E, 0x2B, 0x9C, 0x8C, 0xF5,
			0x6E, 0xDF, 0x01, 0x95, 0x39, 0x34, 0x96, 0x27,
			0xDB, 0x2F, 0xD5, 0x3D, 0x24, 0xB7, 0xC4, 0x86,
			0x65, 0x77, 0x2E, 0x43, 0x7D, 0x6C, 0x7F, 0x8C,
			0xE4, 0x42, 0x73, 0x4A, 0xF7, 0xCC, 0xB7, 0xAE,
			0x83, 0x7C, 0x26, 0x4A, 0xE3, 0xA9, 0xBE, 0xB8,
			0x7F, 0x8A, 0x2F, 0xE9, 0xB8, 0xB5, 0x29, 0x2E,
			0x5A, 0x02, 0x1F, 0xFF, 0x5E, 0x91, 0x47, 0x9E,
			0x8C, 0xE7, 0xA2, 0x8C, 0x24, 0x42, 0xC6, 0xF3,
			0x15, 0x18, 0x0F, 0x93, 0x49, 0x9A, 0x23, 0x4D,
			0xCF, 0x76, 0xE3, 0xFE, 0xD1, 0x35, 0xF9, 0xBB,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
		}
	},
	{
		DH_GROUP_2048, 256,
		{
			0xAC, 0x6B, 0xDB, 0x41, 0x32, 0x4A, 0x9A, 0x9B,
			0xF1, 0x66, 0xDE, 0x5E, 0x13, 0x89, 0x58, 0x2F,
			0xAF, 0x72, 0xB6, 0x65, 0x19, 0x87, 0xEE, 0x07,
			0xFC, 0x31, 0x92, 0x94, 0x3D, 0xB5, 0x60, 0x50,
			0xA3, 0x73, 0x29, 0xCB, 0xB4, 0xA0, 0x99, 0xED,
			0x81, 0x93, 0xE0, 0x75, 0x77, 0x67, 0xA1, 0x3D,
			0xD5, 0x23, 0x12, 0xAB, 0x4B, 0x03, 0x31, 0x0D,
			0xCD, 0x7F, 0x48, 0xA9, 0xDA, 0x04, 0xFD, 0x50,
			0xE8, 0x08, 0x39, 0x69, 0xED, 0xB7, 0x67, 0xB0,
			0xCF, 0x60, 0x95, 0x17, 0x9A, 0x16, 0x3A, 0xB3,
			0x66, 0x1A, 0x05, 0xFB, 0xD5, 0xFA, 0xAA, 0xE8,
			0x29, 0x18, 0xA9, 0x96, 0x2F, 0x0B, 0x93, 0xB8,
			0x55, 0xF9, 0x79, 0x93, 0xEC, 0x97, 0x5E, 0xEA,
			0xA8, 0x0D, 0x74, 0x0A, 0xDB, 0xF4, 0xFF, 0x74,
			0x73, 0x59, 0xD0, 0x41, 0xD5, 0xC3, 0x3E, 0xA7,
			0x1D, 0x28, 0x1E, 0x44, 0x6B, 0x14, 0x77, 0x3B,
			0xCA, 0x97, 0xB4, 0x3A, 0x23, 0xFB, 0x80, 0x16,
			0x76, 0xBD, 0x20, 0x7A, 0x43, 0x6C, 0x64, 0x81,
			0xF1, 0xD2, 0xB9, 0x07, 0x87, 0x17, 0x46, 0x1A,
			0x5B, 0x9D, 0x32, 0xE6, 0x88, 0xF8, 0x77, 0x48,
			0x54, 0x45, 0x23, 0xB5, 0x24, 0xB0, 0xD5, 0x7D,
			0x5E, 0xA7, 0x7A, 0x27, 0x75, 0xD2, 0xEC, 0xFA,
			0x03, 0x2C, 0xFB, 0xDB, 0xF5, 0x2F, 0xB3, 0x78,
			0x61, 0x60, 0x27, 0x90, 0x04, 0xE5, 0x7A, 0xE6,
			0xAF, 0x87, 0x4E, 0x73, 0x03, 0xCE, 0x53, 0x29,
			0x9C, 0xCC, 0x04, 0x1C, 0x7B, 0xC3, 0x08, 0xD8,
			0x2A, 0x56, 0x98, 0xF3, 0xA8, 0xD0, 0xC3, 0x82,
			0x71, 0xAE, 0x35, 0xF8, 0xE9, 0xDB, 0xFB, 0xB6,
			0x94, 0xB5, 0xC8, 0x03, 0xD8, 0x9F, 0x7A, 0xE4,
			0x35, 0xDE, 0x23, 0x6D, 0x52, 0x5F, 0x54, 0x75,
			0x9B, 0x65, 0xE3, 0x72, 0xFC, 0xD6, 0x8E, 0xF2,
			0x0F, 0xA7, 0x11, 0x1F, 0x9E, 0x4A, 0xFF, 0x73,
		}
	},
};

/**
 * lpfc_auth_cancel_tmr
 * @ptr: pointer to a node-list data structure.
 *
 * This routine cancels any pending authentication timer.
 *
 **/
void
lpfc_auth_cancel_tmr(struct lpfc_nodelist *ndlp)
{
	struct lpfc_vport *vport = ndlp->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);

	if (!(ndlp->nlp_flag & NLP_AUTH_TMO))
		return;

	spin_lock_irq(shost->host_lock);
	ndlp->nlp_flag &= ~NLP_AUTH_TMO;
	spin_unlock_irq(shost->host_lock);
	del_timer_sync(&ndlp->dhc_cfg.nlp_reauthfunc);
}

/**
 * lpfc_auth_start_tmr
 * @ndlp: pointer to a node-list data structure.
 * @value: timer value (in msecs).
 *
 * This routine cancels any pending reauthentication timer and updates
 * the timer with a new value. This timer may also be used during the
 * authentication process to check for auth_tmo.
 *
 **/
void
lpfc_auth_start_tmr(struct lpfc_nodelist *ndlp, uint32_t value)
{
	struct lpfc_vport *vport = ndlp->vport;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);

	if (ndlp->nlp_flag & NLP_AUTH_TMO)
		lpfc_auth_cancel_tmr(ndlp);

	mod_timer(&ndlp->dhc_cfg.nlp_reauthfunc,
		  jiffies + msecs_to_jiffies(value));
	spin_lock_irq(shost->host_lock);
	ndlp->nlp_flag |= NLP_AUTH_TMO;
	spin_unlock_irq(shost->host_lock);
}

/**
 * lpfc_auth_start_reauth_tmr.
 * @ndlp: pointer to a node-list data structure.
 *
 * Set-up timeout value for scheduled re-authentication.
 **/
void
lpfc_auth_start_reauth_tmr(struct lpfc_nodelist *ndlp)
{
	u32 msecs;

	msecs = ndlp->dhc_cfg.reauth_interval * 60000;
	lpfc_auth_start_tmr(ndlp, msecs);
}

/**
 * lpfc_auth_findnode - Search for a nodelist item by wwpn pair.
 * @vport: pointer to a vport structure.
 * @lwwpn: pointer to a local portname.
 * @rwwpn: pointer to a remote portname.
 *
 * This routine returns an node list pointer for authentication. The search
 * uses local and remote portnames provided, with specially accomodation for
 * the wwpn used to identify the fabric. The routine returns pointer to the
 * matching ndlp or NULL if no matching entry is found.
 **/
struct lpfc_nodelist *
lpfc_auth_findnode(struct lpfc_vport *vport, struct lpfc_name *lwwpn,
		   struct lpfc_name *rwwpn)
{
	struct lpfc_nodelist *ndlp = NULL;
	struct lpfc_name wwpn;

	wwpn.u.name = AUTH_FABRIC_WWN;
	if (!memcmp(&vport->fc_portname, lwwpn->u.wwn, 8)) {
		if (!memcmp(wwpn.u.wwn, rwwpn->u.wwn, 8))
			ndlp = lpfc_findnode_did(vport,	Fabric_DID);
		else
			ndlp = lpfc_findnode_wwpn(vport, rwwpn);
	}
	return ndlp;
}

/**
 * lpfc_auth_find_cfg_item - Search for a configuration entry by wwpn pair.
 * @phba: pointer to a host.
 * @lwwpn: pointer to a local portname.
 * @rwwpn: pointer to a remote portname.
 * @valid: address of entry valid flag.
 *
 * This routine returns a pointer to an active configuration entry matching
 * the local and remote portnames provided, or NULL if no matching entry is
 * found.
 **/
struct lpfc_auth_cfg_entry *
lpfc_auth_find_cfg_item(struct lpfc_hba *phba, uint8_t *lwwpn, uint8_t *rwwpn,
			uint8_t *valid)
{
	struct lpfc_auth_cfg *pcfg = NULL;
	struct lpfc_auth_cfg_entry *entry = NULL;
	uint8_t *ptmp = NULL;
	uint16_t entry_cnt, i;

	*valid = 0;
	pcfg = lpfc_auth_get_active_cfg_ptr(phba);
	if (!pcfg)
		return entry;

	entry_cnt = pcfg->hdr.entry_cnt;
	if (entry_cnt > MAX_AUTH_CONFIG_ENTRIES)
		entry_cnt = MAX_AUTH_CONFIG_ENTRIES;

	ptmp = (uint8_t *)&pcfg->entry[0];
	for (i = 0; i < entry_cnt; i++) {
		if (i == AUTH_CONFIG_ENTRIES_PER_PAGE) {
			ptmp = (uint8_t *)lpfc_auth_get_active_cfg_p2(phba);
			if (!ptmp)
				return entry;
		}
		if ((memcmp(ptmp + 8, rwwpn, 8)) ||
		    (memcmp(ptmp, lwwpn, 8))) {
			ptmp += sizeof(struct lpfc_auth_cfg_entry);
		} else {
			entry = (struct lpfc_auth_cfg_entry *)ptmp;
			/* match found, check validity */
			*valid = entry->auth_flags.valid;
			break;
		}
	}
	return entry;
}

/**
 * lpfc_auth_lookup_cfg - Set-up auth configuration information for an ndlp.
 * @ndlp: pointer to lpfc_nodelist instance.
 *
 * This routine will initiate a search for a configuration entry matching the
 * local wwpn and remote wwpn for the ndlp. Initial inplementation is limited
 * to authentication with the fabric. If a configuration entry is found, the
 * dh-chap configuration in the ndlp structure is populated.
 *
 * Return codes
 *   0 - Success
 *   1 - Failure
 **/
int
lpfc_auth_lookup_cfg(struct lpfc_nodelist *ndlp)
{
	struct lpfc_vport *vport = ndlp->vport;
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_auth_cfg_entry *entry = NULL;
	uint8_t rwwpn[8];
	uint8_t valid;
	int i, rc = 0;

	/* limited to authentication with the fabric */
	u64_to_wwn(AUTH_FABRIC_WWN, rwwpn);
	entry = lpfc_auth_find_cfg_item(phba, (uint8_t *)&vport->fc_portname,
					rwwpn, &valid);
	if (!entry || !valid)
		return	1;

	ndlp->dhc_cfg.auth_tmo = entry->auth_tmo;
	ndlp->dhc_cfg.auth_mode = entry->auth_mode;
	ndlp->dhc_cfg.bidirectional = entry->auth_flags.bi_direction;
	ndlp->dhc_cfg.reauth_interval = entry->reauth_interval;
	ndlp->dhc_cfg.local_passwd_len = entry->pwd.local_pw_length;
	ndlp->dhc_cfg.remote_passwd_len = entry->pwd.remote_pw_length;
	ndlp->dhc_cfg.direction = AUTH_DIRECTION_NONE;
	memcpy(ndlp->dhc_cfg.local_passwd, &entry->pwd.local_pw,
	       entry->pwd.local_pw_length);
	memcpy(ndlp->dhc_cfg.remote_passwd, &entry->pwd.remote_pw,
	       entry->pwd.remote_pw_length);
	for (i = 0; i < 4; i++) {
		switch (entry->hash_priority[i]) {
		case LPFC_HASH_MD5:
			ndlp->dhc_cfg.hash_pri[i] = HASH_MD5;
			break;
		case LPFC_HASH_SHA1:
			ndlp->dhc_cfg.hash_pri[i] = HASH_SHA1;
			break;
		default:
			ndlp->dhc_cfg.hash_pri[i] = 0;
		}
	}

	ndlp->dhc_cfg.dh_grp_cnt = 0;
	for (i = 0; i < 8; i++) {
		switch (entry->dh_grp_priority[i]) {
		case LPFC_DH_GROUP_2048:
			ndlp->dhc_cfg.dh_grp_pri[i] = DH_GROUP_2048;
			break;
		case LPFC_DH_GROUP_1536:
			ndlp->dhc_cfg.dh_grp_pri[i] = DH_GROUP_1536;
			break;
		case LPFC_DH_GROUP_1280:
			ndlp->dhc_cfg.dh_grp_pri[i] = DH_GROUP_1280;
			break;
		case LPFC_DH_GROUP_1024:
			ndlp->dhc_cfg.dh_grp_pri[i] = DH_GROUP_1024;
			break;
		case LPFC_DH_GROUP_NULL:
			ndlp->dhc_cfg.dh_grp_pri[i] = DH_GROUP_NULL;
			break;
		default:
			return rc;
		}
		ndlp->dhc_cfg.dh_grp_cnt++;
	}
	return rc;
}

/**
 * lpfc_auth_free_active_cfg_list - Free authentication config resources.
 * @phba: pointer to a host.
 *
 * This routine frees any resources allocated to hold the driver's active
 * copy of the authentication configuration object.
 **/
void
lpfc_auth_free_active_cfg_list(struct lpfc_hba *phba)
{
	struct lpfc_dmabuf *dmabuf, *next;

	if (!list_empty(&phba->lpfc_auth_active_cfg_list)) {
		list_for_each_entry_safe(dmabuf, next,
					 &phba->lpfc_auth_active_cfg_list,
					 list) {
			list_del(&dmabuf->list);
			dma_free_coherent(&phba->pcidev->dev, SLI4_PAGE_SIZE,
					  dmabuf->virt, dmabuf->phys);
			kfree(dmabuf);
		}
	}
}

/**
 * lpfc_auth_alloc_active_cfg_list - Allocate authentication config resources.
 * @phba: pointer to a host.
 *
 * This routine attempts to allocate resources to hold the driver's active
 * copy of the authentication configuration object.
 *
 * Return codes
 *   0 - Success
 *   1 - Failure
 **/
int
lpfc_auth_alloc_active_cfg_list(struct lpfc_hba *phba)
{
	struct lpfc_dmabuf *dmabuf;
	uint8_t i;
	int rc = 0;

	if (!list_empty(&phba->lpfc_auth_active_cfg_list))
		return -EINVAL;

	for (i = 0; i < 2; i++) {
		dmabuf = kzalloc(sizeof(*dmabuf), GFP_KERNEL);
		if (!dmabuf) {
			rc = -ENOMEM;
			goto out;
		}
		dmabuf->virt = dma_alloc_coherent(&phba->pcidev->dev,
						  SLI4_PAGE_SIZE,
						  &dmabuf->phys,
						  GFP_KERNEL);
		if (!dmabuf->virt) {
			kfree(dmabuf);
			rc = -ENOMEM;
			goto out;
		}
		list_add_tail(&dmabuf->list, &phba->lpfc_auth_active_cfg_list);
	}
out:
	if (rc)
		lpfc_auth_free_active_cfg_list(phba);
	return rc;
}

/**
 * lpfc_auth_get_active_cfg_ptr - Returns the base address of the
 *				  authentication config.
 * @phba: pointer to a host.
 *
 * This routine returns a pointer to the first page of the driver's active copy
 * of the authentication configuration for the port. The routine will return
 * NULL if there is no active configuration.
 *
 **/
struct lpfc_auth_cfg *
lpfc_auth_get_active_cfg_ptr(struct lpfc_hba *phba)
{
	struct lpfc_dmabuf *dmabuf;
	struct lpfc_auth_cfg *p = NULL;

	if (!list_empty(&phba->lpfc_auth_active_cfg_list)) {
		dmabuf = list_first_entry(&phba->lpfc_auth_active_cfg_list,
					  struct lpfc_dmabuf, list);

		p = (struct lpfc_auth_cfg *)dmabuf->virt;
	}
	return p;
}

/**
 * lpfc_auth_get_active_cfg_p2 - Returns the base address for the second page
 *				 of the active authentication config.
 * @phba: pointer to a host.
 *
 * This routine returns a pointer to the second page of the driver's active copy
 * of the authentication configuration for the port. The routine will return
 * NULL if there is no active configuation.
 *
 **/
struct lpfc_auth_cfg_entry *
lpfc_auth_get_active_cfg_p2(struct lpfc_hba *phba)
{
	struct lpfc_dmabuf *dmabuf;
	struct lpfc_auth_cfg_entry *p = NULL;

	if (!list_empty(&phba->lpfc_auth_active_cfg_list)) {
		dmabuf = list_entry((&phba->lpfc_auth_active_cfg_list)->prev,
				    struct lpfc_dmabuf, list);

		p = (struct lpfc_auth_cfg_entry *)dmabuf->virt;
	}
	return p;
}

/**
 * lpfc_auth_cmpl_rd_object, completion for read object mbx cmd.
 * @phba: pointer to a host.
 * @mboxq: pointer to lpfc mailbox command context.
 *
 * This routine handles the completion of a read object mailbox command
 * issued to populate the driver's active copy of the authentication
 * configuration.
 **/
void
lpfc_auth_cmpl_rd_object(struct lpfc_hba *phba, LPFC_MBOXQ_t *mboxq)
{
	struct lpfc_mbx_auth_rd_object *rd_object;
	union lpfc_sli4_cfg_shdr *shdr;
	uint32_t shdr_status, shdr_add_status;
	int mb_sts = mboxq->u.mb.mbxStatus;

	rd_object = (struct lpfc_mbx_auth_rd_object *)mboxq->sge_array->addr[0];
	shdr = &rd_object->cfg_shdr;
	shdr_status = bf_get(lpfc_mbox_hdr_status, &shdr->response);
	shdr_add_status = bf_get(lpfc_mbox_hdr_add_status, &shdr->response);

	if (shdr_status || shdr_add_status || mb_sts) {
		lpfc_printf_log(phba, KERN_INFO, LOG_MBOX | LOG_AUTH,
				"3040 Read Object mailbox cmd failed with "
				"status x%x add_status x%x, mbx status x%x\n",
				shdr_status, shdr_add_status, mb_sts);

		if (mboxq->ctx_buf == &phba->lpfc_auth_active_cfg_list)
			lpfc_auth_free_active_cfg_list(phba);
	}

	lpfc_sli4_mbox_cmd_free(phba, mboxq);
}

/**
 * lpfc_auth_read_cfg_object, to read object.
 * @phba: pointer to a host.
 *
 * This routine issues a read object mailbox command to populate the driver's
 * active copy of the authentication configuration.
 *
 * Return codes
 *   0 - Success
 *   1 - Failure
 **/
int
lpfc_auth_read_cfg_object(struct lpfc_hba *phba)
{
	uint32_t dma_size;
	uint32_t offset;
	char *obj_str = {"/driver/auth.cfg"};
	int rc = 0;

	if (list_empty(&phba->lpfc_auth_active_cfg_list))
		rc = lpfc_auth_alloc_active_cfg_list(phba);
	if (rc)
		return rc;

	/* 8k per port */
	dma_size = SLI4_PAGE_SIZE * 2;
	offset = phba->sli4_hba.lnk_info.lnk_no * dma_size;
	rc = lpfc_auth_issue_rd_object(phba, &phba->lpfc_auth_active_cfg_list,
				       dma_size, &offset, obj_str);
	if (rc) {
		/* MBX_BUSY is acceptable, no reason to alarm caller */
		if (rc == MBX_BUSY)
			rc = MBX_SUCCESS;
		else
			lpfc_printf_log(phba, KERN_INFO, LOG_MBOX | LOG_AUTH,
					"3041 Read Object abnormal exit:%x.\n",
					rc);
	}
	if (rc != MBX_SUCCESS)
		lpfc_auth_free_active_cfg_list(phba);

	return rc;
}

/**
 * lpfc_auth_compute_hash - Calculate the hash
 * @vport: pointer to a host virtual N_Port data structure.
 *
 * Return value
 *   Pointer to hash
 **/
static uint8_t *
lpfc_auth_compute_hash(uint32_t hash_id,
		       uint8_t *val1,
		       uint32_t val1_len,
		       uint8_t *val2,
		       uint32_t val2_len,
		       uint8_t *val3,
		       uint32_t val3_len)
{
	struct crypto_shash *tfm = NULL;
	struct shash_desc *desc = NULL;
	uint8_t *digest = NULL;
	uint32_t chal_len = 0;
	int rc = 0;

	switch (hash_id) {
	case HASH_MD5:
		tfm = crypto_alloc_shash("md5", 0, 0);
		chal_len = MD5_CHAL_LEN;
		break;
	case HASH_SHA1:
		tfm = crypto_alloc_shash("sha1", 0, 0);
		chal_len = SHA1_CHAL_LEN;
		break;
	default:
		return NULL;
	}

	if (!tfm)
		return NULL;

	digest = kzalloc(chal_len, GFP_KERNEL);
	if (!digest)
		goto out;

	desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm),
		       GFP_KERNEL);
	if (!desc) {
		rc = 1;
		goto out;
	}
	desc->tfm = tfm;
#if (KERNEL_MAJOR < 5)
	desc->flags = 0;
#endif

	rc = crypto_shash_init(desc);
	if (rc < 0)
		goto out;
	if (val1 && val1_len) {
		rc = crypto_shash_update(desc, val1, val1_len);
		if (rc < 0)
			goto out;
	}
	if (val2 && val2_len) {
		rc = crypto_shash_update(desc, val2, val2_len);
		if (rc < 0)
			goto out;
	}
	if (val3 && val3_len) {
		rc = crypto_shash_update(desc, val3, val3_len);
		if (rc < 0)
			goto out;
	}
	rc = crypto_shash_final(desc, digest);

out:
	if (rc) {
		kfree(digest);
		digest = NULL;
	}
	kfree(desc);
	crypto_free_shash(tfm);
	return digest;
}

/**
 * lpfc_auth_compute_dhkey - Calculate the dhkey
 * @base: base value
 * @base_len: base value length
 * @expo: exponent value
 * @expo_len: exponent value length
 * @mod: modulo value
 * @mod_len: modulo value length
 * @key: resulting key value
 * @key_len: resulting key value length
 *
 * Return value
 *   Success / Failure
 **/
static int
lpfc_auth_compute_dhkey(uint8_t *base,
			uint32_t base_len,
			uint8_t *expo,
			uint32_t expo_len,
			uint8_t *mod,
			uint32_t mod_len,
			uint8_t *key,
			uint32_t key_len)
{
	int i, rc;
	uint8_t *temp = NULL;
	uint32_t temp_len = 0;
	MPI k, b, e, m;

	/*
	 * key = ((base)^expo modulo mod)
	 */
	b = mpi_read_raw_data(base, base_len);
	e = mpi_read_raw_data(expo, expo_len);
	m = mpi_read_raw_data(mod, mod_len);

	k = mpi_alloc(mpi_get_nlimbs(m) * 2);

	rc = mpi_powm(k, b, e, m);
	if (!rc) {
		temp = mpi_get_buffer(k, &temp_len, NULL);
		if (!temp) {
			rc = 1;
			goto out;
		}

		if (key_len < temp_len) {
			rc = 1;
			goto out;
		}

		/* Pad the key with leading zeros */
		memset(key, 0, key_len);
		i = key_len - temp_len;
		memcpy(key + i, temp, temp_len);
	}

out:
	mpi_free(b);
	mpi_free(e);
	mpi_free(m);
	mpi_free(k);
	kfree(temp);
	return rc;
}

/**
 * lpfc_auth_cmpl_negotiate - Completion callback routine
 * @phba: pointer to lpfc hba data structure.
 * @cmdiocb: pointer to lpfc command iocb data structure.
 * @rspiocb: pointer to lpfc response iocb data structure.
 *
 **/
static void
lpfc_auth_cmpl_negotiate(struct lpfc_hba *phba, struct lpfc_iocbq *cmdiocb,
			 struct lpfc_iocbq *rspiocb)
{
	struct lpfc_vport *vport = phba->pport;
	IOCB_t *irsp = &rspiocb->iocb;
	struct lpfc_nodelist *ndlp = cmdiocb->context1;

	if (irsp->ulpStatus)
		lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS | LOG_AUTH,
				 "3043 ELS_AUTH cmpl, ulpstatus=x%x/%x\n",
				 irsp->ulpStatus, irsp->un.ulpWord[4]);
	lpfc_els_free_iocb(phba, cmdiocb);

	if ((irsp->ulpStatus == IOSTAT_LS_RJT) &&
	    ndlp && NLP_CHK_NODE_ACT(ndlp) &&
	    (ndlp->dhc_cfg.auth_mode == LPFC_AUTH_MODE_ACTIVE)) {
		ndlp->dhc_cfg.state = LPFC_AUTH_UNKNOWN;
		/* logout should be sent on LS REJECT */
		lpfc_issue_els_logo(vport, ndlp, 0);
	}
}

/**
 * lpfc_auth_issue_negotiate - Issue AUTH_NEGOTIATE command
 * @vport: pointer to a host virtual N_Port data structure.
 * @ndlp: pointer to a node-list data structure.
 *
 * This routine issues AUTH_NEGOTIATE ELS command to the remote
 * port as the authentication initiator.
 *
 * Return codes
 *   0 - Success
 *   1 - Failure
 **/
static int
lpfc_auth_issue_negotiate(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_iocbq *elsiocb;
	uint8_t *pcmd;
	struct lpfc_auth_hdr *hdr;
	struct lpfc_auth_negotiate_cmn *negotiate;
	struct lpfc_dhchap_param_cmn *dhchap_param;
	struct lpfc_auth_param_hdr *param_hdr;
	uint32_t param_len;
	uint32_t *hash;
	uint32_t *dh_group;
	uint16_t cmdsize;
	uint8_t i, dh_grp_cnt;
	int rc;

	/* Support 2 Hash Functions and 5 DH-Groups */
	cmdsize = sizeof(struct lpfc_auth_hdr) +
		sizeof(struct lpfc_auth_negotiate_cmn) +
		sizeof(struct lpfc_dhchap_param_cmn) +
		sizeof(struct lpfc_auth_param_hdr) + 2 * sizeof(uint32_t) +
		sizeof(struct lpfc_auth_param_hdr) + 5 * sizeof(uint32_t);
	elsiocb = lpfc_prep_els_iocb(vport, 1, cmdsize, 0, ndlp,
				     ndlp->nlp_DID, ELS_CMD_AUTH);

	if (!elsiocb)
		return 1;

	/* Save the auth details for later use */
	if (ndlp->dhc_cfg.state != LPFC_AUTH_SUCCESS)
		ndlp->dhc_cfg.state = LPFC_AUTH_UNKNOWN;
	ndlp->dhc_cfg.msg = LPFC_AUTH_NEGOTIATE;

	pcmd = (uint8_t *)(((struct lpfc_dmabuf *)elsiocb->context2)->virt);
	ndlp->dhc_cfg.tran_id += 1;

	/* Build the AUTH_NEGOTIATE header */
	hdr = (struct lpfc_auth_hdr *)pcmd;
	hdr->auth_els_code = (uint8_t) ELS_CMD_AUTH;
	hdr->auth_els_flags = 0;
	hdr->auth_msg_code = AUTH_NEGOTIATE;
	hdr->protocol_ver = AUTH_PROTOCOL_VER_1;
	hdr->msg_len = be32_to_cpu(cmdsize - sizeof(struct lpfc_auth_hdr));
	hdr->tran_id = be32_to_cpu(ndlp->dhc_cfg.tran_id);
	pcmd += sizeof(struct lpfc_auth_hdr);

	/* Set up AUTH_NEGOTIATE common data */
	negotiate = (struct lpfc_auth_negotiate_cmn *)pcmd;
	negotiate->name_tag = be16_to_cpu(AUTH_NAME_TAG);
	negotiate->name_len = be16_to_cpu(sizeof(struct lpfc_name));
	memcpy(&negotiate->port_name, &vport->fc_portname,
	       sizeof(struct lpfc_name));
	negotiate->num_protocol = be32_to_cpu(1); /* Only support DHCHAP */
	pcmd += sizeof(struct lpfc_auth_negotiate_cmn);

	/* Set up DH-CHAP parameters common data */
	dh_grp_cnt = ndlp->dhc_cfg.dh_grp_cnt;
	if (dh_grp_cnt > 5)
		dh_grp_cnt = 5;
	dhchap_param = (struct lpfc_dhchap_param_cmn *)pcmd;
	param_len = sizeof(uint32_t) +
		sizeof(struct lpfc_auth_param_hdr) + 2 * sizeof(uint32_t) +
		sizeof(struct lpfc_auth_param_hdr) +
		       dh_grp_cnt * sizeof(uint32_t);
	dhchap_param->param_len = be32_to_cpu(param_len);
	dhchap_param->protocol_id = be32_to_cpu(PROTOCOL_DHCHAP);
	pcmd += sizeof(struct lpfc_dhchap_param_cmn);

	/* Set up Hash Functions */
	param_hdr = (struct lpfc_auth_param_hdr *)pcmd;
	param_hdr->tag = be16_to_cpu(DHCHAP_TAG_HASHLIST);
	param_hdr->wcnt = be16_to_cpu(2); /* 2 HASH Functions */
	pcmd += sizeof(struct lpfc_auth_param_hdr);
	hash = (uint32_t *)pcmd;
	hash[0] = be32_to_cpu(ndlp->dhc_cfg.hash_pri[0]);
	hash[1] = be32_to_cpu(ndlp->dhc_cfg.hash_pri[1]);
	pcmd += 2 * sizeof(uint32_t);

	/* Setup DH Groups */
	param_hdr = (struct lpfc_auth_param_hdr *)pcmd;
	param_hdr->tag = be16_to_cpu(DHCHAP_TAG_GRP_IDLIST);
	param_hdr->wcnt = be16_to_cpu(dh_grp_cnt); /* max 5 DH Groups */
	pcmd += sizeof(struct lpfc_auth_param_hdr);
	dh_group = (uint32_t *)pcmd;
	for (i = 0; i < dh_grp_cnt; i++)
		dh_group[i] = be32_to_cpu(ndlp->dhc_cfg.dh_grp_pri[i]);

	elsiocb->iocb_cmpl = lpfc_auth_cmpl_negotiate;
	rc = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, elsiocb, 0);

	if (rc == IOCB_ERROR) {
		lpfc_els_free_iocb(phba, elsiocb);
		return 1;
	}
	/* Set up authentication timer */
	if (ndlp->dhc_cfg.auth_tmo)
		lpfc_auth_start_tmr(ndlp, ndlp->dhc_cfg.auth_tmo * 1000);

	return 0;
}

/**
 * lpfc_auth_issue_dhchap_challenge - Issue DHCHAP_CHALLENGE command
 * @vport: pointer to a host virtual N_Port data structure.
 * @ndlp: pointer to a node-list data structure.
 *
 * This routine issues DHCHAP_CHALLENGE ELS command to the remote
 * port as the authentication responder.
 *
 * Return codes
 *   0 - Success
 *   1 - Failure
 **/
static int
lpfc_auth_issue_dhchap_challenge(struct lpfc_vport *vport,
				 struct lpfc_nodelist *ndlp)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_auth_hdr *hdr;
	struct lpfc_auth_challenge *challenge;
	uint8_t *pcmd;
	uint16_t cmdsize;
	uint32_t chal_len;
	uint32_t hash_id = ndlp->dhc_cfg.hash_id;
	uint32_t dh_grp_id = ndlp->dhc_cfg.dh_grp_id;
	uint32_t dh_val_len = dh_group_array[dh_grp_id].length;
	uint8_t nonce[16], key[256];
	uint8_t *rand_num = NULL;
	uint32_t key_len;
	uint8_t key_base = 2;
	int rc = 0;

	switch (hash_id) {
	case HASH_MD5:
		chal_len = MD5_CHAL_LEN;
		break;
	case HASH_SHA1:
		chal_len = SHA1_CHAL_LEN;
		break;
	default:
		return 1;
	}

	rand_num = kzalloc(chal_len, GFP_KERNEL);
	if (!rand_num) {
		rc = 1;
		goto out;
	}
	get_random_bytes(rand_num, chal_len);

	cmdsize = sizeof(struct lpfc_auth_hdr) +
		sizeof(struct lpfc_auth_challenge) +
		chal_len + sizeof(uint32_t) + dh_val_len;
	elsiocb = lpfc_prep_els_iocb(vport, 1, cmdsize, 0, ndlp,
				     ndlp->nlp_DID, ELS_CMD_AUTH);

	if (!elsiocb) {
		rc = 1;
		goto out;
	}

	pcmd = (uint8_t *)(((struct lpfc_dmabuf *)elsiocb->context2)->virt);

	/* Build the DHCHAP_REPLY payload */
	hdr = (struct lpfc_auth_hdr *)pcmd;
	hdr->auth_els_code = (uint8_t) ELS_CMD_AUTH;
	hdr->auth_els_flags = 0;
	hdr->auth_msg_code = DHCHAP_CHALLENGE;
	hdr->protocol_ver = AUTH_PROTOCOL_VER_1;
	hdr->msg_len = be32_to_cpu(cmdsize - sizeof(struct lpfc_auth_hdr));
	hdr->tran_id = be32_to_cpu(ndlp->dhc_cfg.tran_id);
	pcmd += sizeof(struct lpfc_auth_hdr);

	/* Set up DHCHAP_CHALLENGE payload */
	challenge = (struct lpfc_auth_challenge *)pcmd;
	challenge->name_tag = be16_to_cpu(AUTH_NAME_TAG);
	challenge->name_len = be16_to_cpu(sizeof(struct lpfc_name));
	memcpy(&challenge->port_name, &vport->fc_portname,
	       sizeof(struct lpfc_name));
	challenge->hash_id = be32_to_cpu(ndlp->dhc_cfg.hash_id);
	challenge->dh_grp_id = be32_to_cpu(ndlp->dhc_cfg.dh_grp_id);
	challenge->chal_len = be32_to_cpu(chal_len);
	pcmd += sizeof(struct lpfc_auth_challenge);
	memcpy(pcmd, rand_num, chal_len);
	pcmd += chal_len;
	*((uint32_t *)pcmd) = be32_to_cpu(dh_val_len);
	pcmd += sizeof(uint32_t);

	/* Save the challenge */
	memcpy(ndlp->dhc_cfg.rand_num, rand_num, chal_len);

	if (dh_grp_id) {
		key_len = dh_group_array[dh_grp_id].length;
		get_random_bytes(nonce, sizeof(nonce));

		rc = lpfc_auth_compute_dhkey(&key_base, sizeof(key_base),
					     nonce, sizeof(nonce),
					     dh_group_array[dh_grp_id].value,
					     dh_group_array[dh_grp_id].length,
					     key, key_len);
		if (rc)
			goto out;

		memcpy(pcmd, key, key_len);

		/* Save the nonce */
		memcpy(ndlp->dhc_cfg.nonce, nonce, sizeof(nonce));
	}

	elsiocb->iocb_cmpl = lpfc_auth_cmpl_negotiate;
	rc = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, elsiocb, 0);
	if (rc == IOCB_ERROR)
		lpfc_els_free_iocb(phba, elsiocb);

out:
	kfree(rand_num);
	return rc;
}

/**
 * lpfc_auth_issue_dhchap_reply - Issue DHCHAP_REPLY cmd
 * @vport: pointer to a host virtual N_Port data structure.
 * @ndlp: pointer to a node-list data structure.
 *
 * Return code
 *   0 -
 *   1 -
 **/
static int
lpfc_auth_issue_dhchap_reply(struct lpfc_vport *vport,
			     struct lpfc_nodelist *ndlp,
			     uint32_t tran_id, uint32_t dh_grp_id,
			     uint32_t hash_id, uint8_t *hash_value,
			     uint8_t *dh_value, uint32_t dh_val_len)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_auth_hdr *hdr;
	uint8_t *pcmd;
	uint16_t cmdsize;
	uint32_t chal_len = 0;
	uint8_t *rand_num = NULL;
	int rc = 0;

	switch (hash_id) {
	case HASH_MD5:
		/* Check for bi_directional support */
		if (ndlp->dhc_cfg.bidirectional)
			chal_len = MD5_CHAL_LEN;
		cmdsize = sizeof(struct lpfc_auth_hdr) +
			sizeof(uint32_t) + MD5_CHAL_LEN +
			sizeof(uint32_t) + dh_val_len +
			sizeof(uint32_t) + chal_len;
		break;
	case HASH_SHA1:
		/* Check for bi_directional support */
		if (ndlp->dhc_cfg.bidirectional)
			chal_len = SHA1_CHAL_LEN;
		cmdsize = sizeof(struct lpfc_auth_hdr) +
			sizeof(uint32_t) + SHA1_CHAL_LEN +
			sizeof(uint32_t) + dh_val_len +
			sizeof(uint32_t) + chal_len;
		break;
	default:
		return 1;
	}

	ndlp->dhc_cfg.msg = LPFC_DHCHAP_REPLY;

	/* Check for bi_directional support before allocating rand_num */
	if (ndlp->dhc_cfg.bidirectional) {
		rand_num = kzalloc(chal_len, GFP_KERNEL);
		if (!rand_num) {
			rc = 1;
			goto out;
		}
		get_random_bytes(rand_num, chal_len);
	}

	elsiocb = lpfc_prep_els_iocb(vport, 1, cmdsize, 0, ndlp,
				     ndlp->nlp_DID, ELS_CMD_AUTH);

	if (!elsiocb) {
		rc = 1;
		goto out;
	}

	pcmd = (uint8_t *)(((struct lpfc_dmabuf *)elsiocb->context2)->virt);

	/* Build the DHCHAP_REPLY payload */
	hdr = (struct lpfc_auth_hdr *)pcmd;
	hdr->auth_els_code = (uint8_t) ELS_CMD_AUTH;
	hdr->auth_els_flags = 0;
	hdr->auth_msg_code = DHCHAP_REPLY;
	hdr->protocol_ver = AUTH_PROTOCOL_VER_1;
	hdr->msg_len = be32_to_cpu(cmdsize - sizeof(struct lpfc_auth_hdr));
	hdr->tran_id = be32_to_cpu(tran_id);

	pcmd += sizeof(struct lpfc_auth_hdr);
	if (hash_id == HASH_MD5) {
		*((uint32_t *)pcmd) = be32_to_cpu(MD5_CHAL_LEN);
		pcmd += sizeof(uint32_t);
		memcpy(pcmd, hash_value, MD5_CHAL_LEN);
		pcmd += MD5_CHAL_LEN;
	} else if (hash_id == HASH_SHA1) {
		*((uint32_t *)pcmd) = be32_to_cpu(SHA1_CHAL_LEN);
		pcmd += sizeof(uint32_t);
		memcpy(pcmd, hash_value, SHA1_CHAL_LEN);
		pcmd += SHA1_CHAL_LEN;
	}

	*((uint32_t *)pcmd) = be32_to_cpu(dh_val_len);
	pcmd += sizeof(uint32_t);
	if (dh_val_len) {
		memcpy(pcmd, dh_value, dh_val_len);
		pcmd += dh_val_len;
	}
	*((uint32_t *)pcmd) = be32_to_cpu(chal_len);
	if (chal_len) {
		pcmd += sizeof(uint32_t);
		memcpy(pcmd, rand_num, chal_len);
	}

	/* Save the auth details for later use */
	ndlp->dhc_cfg.hash_id = hash_id;
	ndlp->dhc_cfg.tran_id = tran_id;
	ndlp->dhc_cfg.dh_grp_id = dh_grp_id;
	memcpy(ndlp->dhc_cfg.rand_num, rand_num, chal_len);

	elsiocb->iocb_cmpl = lpfc_auth_cmpl_negotiate;
	rc = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, elsiocb, 0);
	if (rc == IOCB_ERROR)
		lpfc_els_free_iocb(phba, elsiocb);

	/* Set up authentication timer */
	if (ndlp->dhc_cfg.auth_tmo)
		lpfc_auth_start_tmr(ndlp, ndlp->dhc_cfg.auth_tmo * 1000);
out:
	kfree(rand_num);
	return rc;
}

/**
 * lpfc_auth_issue_dhchap_success - Issue DHCHAP_SUCCESS cmd
 * @vport: pointer to a host virtual N_Port data structure.
 * @ndlp: pointer to a node-list data structure.
 *
 * Return code
 *   0 -
 *   1 -
 **/
static int
lpfc_auth_issue_dhchap_success(struct lpfc_vport *vport,
			       struct lpfc_nodelist *ndlp,
			       uint8_t *hash_value)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_auth_hdr *hdr;
	uint8_t *pcmd;
	uint16_t cmdsize;
	uint32_t chal_len = 0;
	int rc = 0;

	if (ndlp->dhc_cfg.hash_id == HASH_MD5)
		chal_len = MD5_CHAL_LEN;
	else if (ndlp->dhc_cfg.hash_id == HASH_SHA1)
		chal_len = SHA1_CHAL_LEN;

	cmdsize = sizeof(struct lpfc_auth_hdr) + sizeof(uint32_t);
	if (hash_value)
		cmdsize += chal_len;

	elsiocb = lpfc_prep_els_iocb(vport, 1, cmdsize, 0, ndlp,
				     ndlp->nlp_DID, ELS_CMD_AUTH);

	if (!elsiocb) {
		rc = 1;
		goto out;
	}

	pcmd = (uint8_t *)(((struct lpfc_dmabuf *)elsiocb->context2)->virt);

	/* Build the DHCHAP_SUCCESS payload */
	hdr = (struct lpfc_auth_hdr *)pcmd;
	hdr->auth_els_code = (uint8_t) ELS_CMD_AUTH;
	hdr->auth_els_flags = 0;
	hdr->auth_msg_code = DHCHAP_SUCCESS;
	hdr->protocol_ver = AUTH_PROTOCOL_VER_1;
	hdr->msg_len = be32_to_cpu(cmdsize - sizeof(struct lpfc_auth_hdr));
	hdr->tran_id = be32_to_cpu(ndlp->dhc_cfg.tran_id);
	pcmd += sizeof(struct lpfc_auth_hdr);

	if (hash_value) {
		*((uint32_t *)pcmd) = be32_to_cpu(chal_len);
		pcmd += sizeof(uint32_t);
		memcpy(pcmd, hash_value, chal_len);
	} else {
		*((uint32_t *)pcmd) = 0;
	}

	elsiocb->iocb_cmpl = lpfc_auth_cmpl_negotiate;
	rc = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, elsiocb, 0);
	if (rc == IOCB_ERROR)
		lpfc_els_free_iocb(phba, elsiocb);

	ndlp->dhc_cfg.state = LPFC_AUTH_SUCCESS;
	ndlp->dhc_cfg.msg = LPFC_DHCHAP_SUCCESS_REPLY;
	ndlp->dhc_cfg.direction = AUTH_DIRECTION_LOCAL;
	ndlp->dhc_cfg.last_auth = jiffies;
out:
	return rc;
}

/**
 * lpfc_auth_issue_reject - Issue AUTH_REJECT cmd
 * @vport: pointer to a host virtual N_Port data structure.
 * @ndlp: pointer to a node-list data structure.
 * @tran_id: transaction id
 * @rsn_code: reason code
 * @rsn_expl: reason code explanation
 *
 * Return code
 *   0 -
 *   1 -
 **/
static int
lpfc_auth_issue_reject(struct lpfc_vport *vport,
		       struct lpfc_nodelist *ndlp,
		       uint32_t tran_id,
		       uint8_t rsn_code,
		       uint8_t rsn_expl)
{
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_iocbq *elsiocb;
	struct lpfc_auth_hdr *hdr;
	struct lpfc_auth_reject *rjt;
	uint8_t *pcmd;
	uint16_t cmdsize;
	int rc = 0;

	cmdsize = sizeof(struct lpfc_auth_hdr) + sizeof(uint32_t);
	elsiocb = lpfc_prep_els_iocb(vport, 1, cmdsize, 0, ndlp,
				     ndlp->nlp_DID, ELS_CMD_AUTH);

	if (!elsiocb) {
		rc = 1;
		goto out;
	}

	pcmd = (uint8_t *)(((struct lpfc_dmabuf *)elsiocb->context2)->virt);

	/* Build the AUTH_REJECT payload */
	hdr = (struct lpfc_auth_hdr *)pcmd;
	hdr->auth_els_code = (uint8_t) ELS_CMD_AUTH;
	hdr->auth_els_flags = 0;
	hdr->auth_msg_code = AUTH_REJECT;
	hdr->protocol_ver = AUTH_PROTOCOL_VER_1;
	hdr->msg_len = be32_to_cpu(cmdsize - sizeof(struct lpfc_auth_hdr));
	hdr->tran_id = be32_to_cpu(tran_id);

	rjt = (struct lpfc_auth_reject *)(pcmd + sizeof(struct lpfc_auth_hdr));
	rjt->rsn_code = rsn_code;
	rjt->rsn_expl = rsn_expl;

	elsiocb->iocb_cmpl = lpfc_auth_cmpl_negotiate;
	rc = lpfc_sli_issue_iocb(phba, LPFC_ELS_RING, elsiocb, 0);
	if (rc == IOCB_ERROR)
		lpfc_els_free_iocb(phba, elsiocb);

	ndlp->dhc_cfg.msg = LPFC_AUTH_REJECT;
	ndlp->dhc_cfg.direction = AUTH_DIRECTION_NONE;
out:
	return rc;
}

/**
 * lpfc_auth_handle_reject - Handle AUTH Reject for a vport
 * @vport: pointer to a host virtual N_Port data structure.
 * @payload: pointer to AUTH Reject cmd payload.
 * @ndlp: pointer to a node-list data structure.
 *
 * Return code
 *   0 -
 *   1 -
 **/
static int
lpfc_auth_handle_reject(struct lpfc_vport *vport, uint8_t *payload,
			struct lpfc_nodelist *ndlp)
{
	struct lpfc_auth_reject *rjt;
	uint8_t rsn_code;
	uint8_t rsn_expl;

	rjt = (struct lpfc_auth_reject *)
		(payload + sizeof(struct lpfc_auth_hdr));
	rsn_code = rjt->rsn_code;
	rsn_expl = rjt->rsn_expl;

	if (rsn_code == AUTHRJT_LOGICAL_ERR &&
	    rsn_expl == AUTHEXPL_RESTART_AUTH) {
		/* Restart authentication */
		lpfc_auth_issue_negotiate(vport, ndlp);
	} else {
		ndlp->dhc_cfg.state = LPFC_AUTH_FAIL;
		ndlp->dhc_cfg.msg = LPFC_AUTH_REJECT;
	}
	ndlp->dhc_cfg.direction = AUTH_DIRECTION_NONE;

	return 0;
}

/**
 * lpfc_auth_handle_negotiate - Handle AUTH Negotiate for a vport
 * @vport: pointer to a host virtual N_Port data structure.
 * @payload: pointer to AUTH Negotiate cmd payload.
 * @ndlp: pointer to a node-list data structure.
 *
 * Return code
 *   0 -
 *   1 -
 **/
static int
lpfc_auth_handle_negotiate(struct lpfc_vport *vport, uint8_t *payload,
			   struct lpfc_nodelist *ndlp)
{
	struct lpfc_auth_hdr *hdr;
	struct lpfc_auth_negotiate_cmn *negotiate;
	struct lpfc_dhchap_param_cmn *dhchap_param;
	struct lpfc_auth_param_hdr *param_hdr;
	uint32_t protocol_id, hash = 0, dh_group = 0;
	uint8_t found = 0;
	int i;
	int rc = 0;

	hdr = (struct lpfc_auth_hdr *)payload;

	/* Nx_Port with the higher port_name shall remain the initiator */
	if (memcmp(&vport->fc_portname, &ndlp->nlp_portname,
		   sizeof(struct lpfc_name)) > 0) {
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_LOGICAL_ERR,
				       AUTHEXPL_AUTH_STARTED);
		return 0;
	}

	/* Sanity check the AUTH message header */
	if (hdr->protocol_ver != AUTH_PROTOCOL_VER_1) {
		rc = 1;
		goto out;
	}

	/* Sanity check the Negotiate payload */
	payload += sizeof(struct lpfc_auth_hdr);
	negotiate = (struct lpfc_auth_negotiate_cmn *)payload;
	if (be16_to_cpu(negotiate->name_tag) != AUTH_NAME_TAG ||
	    be16_to_cpu(negotiate->name_len) != sizeof(struct lpfc_name) ||
	    be32_to_cpu(negotiate->num_protocol) == 0) {
		rc = 1;
		goto out;
	}

	payload += sizeof(struct lpfc_auth_negotiate_cmn);

	/* Scan the payload for Protocol support */
	for (i = 0; i < be32_to_cpu(negotiate->num_protocol); i++) {
		dhchap_param = (struct lpfc_dhchap_param_cmn *)payload;
		protocol_id = be32_to_cpu(dhchap_param->protocol_id);

		if (protocol_id == PROTOCOL_DHCHAP) {
			found = 1;
			break;
		}

		payload += be32_to_cpu(dhchap_param->param_len);
	}

	if (!found) {
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_LOGICAL_ERR,
				       AUTHEXPL_MECH_UNUSABLE);
		return 0;
	}

	payload += sizeof(struct lpfc_dhchap_param_cmn);
	param_hdr = (struct lpfc_auth_param_hdr *)payload;
	payload += sizeof(struct lpfc_auth_param_hdr);

	/* Scan the payload for Hash support */
	found = 0;
	for (i = 0; i < be32_to_cpu(param_hdr->wcnt); i++) {
		hash = be32_to_cpu(*((uint32_t *)payload));

		if (hash ==  HASH_MD5 || hash == HASH_SHA1) {
			found = 1;
			break;
		}

		payload += sizeof(uint32_t);
	}

	if (!found) {
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_LOGICAL_ERR,
				       AUTHEXPL_HASH_UNUSABLE);
		return 0;
	}

	param_hdr = (struct lpfc_auth_param_hdr *)payload;
	payload += sizeof(struct lpfc_auth_param_hdr);

	/* Scan the payload for DH Group support */
	found = 0;
	for (i = 0; i < be32_to_cpu(param_hdr->wcnt); i++) {
		dh_group = be32_to_cpu(*((uint32_t *)payload));

		if (dh_group >= DH_GROUP_NULL && dh_group <= DH_GROUP_2048) {
			found = 1;
			break;
		}

		payload += sizeof(uint32_t);
	}

	if (!found) {
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_LOGICAL_ERR,
				       AUTHEXPL_DHGRP_UNUSABLE);
		return 0;
	}

	/* Save the Authentication settings for this node */
	ndlp->dhc_cfg.tran_id = be32_to_cpu(hdr->tran_id);
	ndlp->dhc_cfg.hash_id = hash;
	ndlp->dhc_cfg.dh_grp_id = dh_group;

	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS | LOG_AUTH,
			 "6041 AUTH_NEGOTIATE "
			 "tran_id: x%x hash_id: x%x dh_grp: x%x\n",
			 be32_to_cpu(hdr->tran_id), hash, dh_group);

	/* Issue a DH-CHAP Challenge to the initiator */
	lpfc_auth_issue_dhchap_challenge(vport, ndlp);
	return 0;

out:
	if (rc)
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_FAILURE, AUTHEXPL_BAD_PAYLOAD);
	return 0;
}

/**
 * lpfc_auth_handle_dhchap_chal - Handle DHCHAP Challenge for a vport
 * @vport: pointer to a host virtual N_Port data structure.
 * @payload: pointer to DHCHAP Challenge cmd payload.
 * @ndlp: pointer to a node-list data structure.
 *
 * Return code
 *   0 -
 *   1 -
 **/
static int
lpfc_auth_handle_dhchap_chal(struct lpfc_vport *vport, uint8_t *payload,
			     struct lpfc_nodelist *ndlp)
{
	struct lpfc_auth_hdr *hdr;
	struct lpfc_auth_challenge *chal;
	uint8_t *hash_value = NULL;
	uint32_t dh_val_len = 0;
	uint8_t *dh_value = NULL;
	uint8_t *aug_chal = NULL;
	uint8_t challenge[20];
	uint8_t tran_id;
	uint32_t hash_id;
	uint32_t chal_len;
	uint32_t dh_grp_id;
	uint8_t nonce[16], key[256], rsp_key[256];
	uint32_t key_len;
	uint32_t rsp_key_len;
	uint8_t rsp_key_base = 2;
	char *passwd = ndlp->dhc_cfg.local_passwd;
	uint32_t passwd_len = ndlp->dhc_cfg.local_passwd_len;
	int rc;

	hdr = (struct lpfc_auth_hdr *)payload;
	/* Sanity check the AUTH message header */
	if (hdr->protocol_ver != AUTH_PROTOCOL_VER_1 ||
	    be32_to_cpu(hdr->tran_id) != ndlp->dhc_cfg.tran_id) {
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_FAILURE, AUTHEXPL_BAD_PAYLOAD);
		return 0;
	}

	payload += sizeof(struct lpfc_auth_hdr);
	chal = (struct lpfc_auth_challenge *)payload;

	tran_id = be32_to_cpu(hdr->tran_id) & 0xff;
	hash_id = be32_to_cpu(chal->hash_id);
	chal_len = be32_to_cpu(chal->chal_len);
	dh_grp_id = be32_to_cpu(chal->dh_grp_id);

	payload += sizeof(struct lpfc_auth_challenge);

	ndlp->dhc_cfg.msg = LPFC_DHCHAP_CHALLENGE;

	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS | LOG_AUTH,
			 "3044 AUTH_CHALLENGE "
			 "tran_id:x%x hash_id:x%x chal_len=x%x dh_grp=x%x\n",
			 tran_id, hash_id, chal_len, dh_grp_id);

	if (hash_id == HASH_MD5 && chal_len == MD5_CHAL_LEN) {
		memcpy(challenge, payload, MD5_CHAL_LEN);
	} else if (hash_id == HASH_SHA1 && chal_len == SHA1_CHAL_LEN) {
		memcpy(challenge, payload, SHA1_CHAL_LEN);
	} else {
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_FAILURE, AUTHEXPL_BAD_PAYLOAD);
		return 0;
	}

	payload += chal_len;

	switch (dh_grp_id) {
	case DH_GROUP_2048:
	case DH_GROUP_1536:
	case DH_GROUP_1280:
	case DH_GROUP_1024:
		key_len = dh_group_array[dh_grp_id].length;
		rsp_key_len = dh_group_array[dh_grp_id].length;
		/* Save the dh value */
		dh_val_len = be32_to_cpu(*((uint32_t *)payload));
		payload += sizeof(uint32_t);

		dh_value = kzalloc(dh_val_len, GFP_KERNEL);
		if (!dh_value)
			goto out;
		memcpy(dh_value, payload, dh_val_len);

		/* Compute Ephemeral DH Key */
		get_random_bytes(nonce, sizeof(nonce));

		rc = lpfc_auth_compute_dhkey(dh_value, dh_val_len, nonce,
					     sizeof(nonce),
					     dh_group_array[dh_grp_id].value,
					     dh_group_array[dh_grp_id].length,
					     key, key_len);
		if (rc)
			break;

		/* Save this key, (((g)^x)^y mod p) */
		memcpy(ndlp->dhc_cfg.dhkey, key, key_len);
		ndlp->dhc_cfg.dhkey_len = key_len;

		rc = lpfc_auth_compute_dhkey(&rsp_key_base,
					     sizeof(rsp_key_base),
					     nonce, sizeof(nonce),
					     dh_group_array[dh_grp_id].value,
					     dh_group_array[dh_grp_id].length,
					     rsp_key, rsp_key_len);
		if (rc)
			break;

		/*
		 * Compute the augmented challenge
		 * aug_chal = hash(Challenge || Ephemeral DH Key)
		 */
		aug_chal = lpfc_auth_compute_hash(hash_id, challenge, chal_len,
						  key, key_len, 0, 0);
		if (!aug_chal) {
			lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS | LOG_AUTH,
					 "3045 Null aug_chal\n");
			break;
		}

		/* Calculate the hash response */
		hash_value = lpfc_auth_compute_hash(hash_id,
						    &tran_id,
						    sizeof(tran_id),
						    passwd,
						    passwd_len,
						    aug_chal,
						    chal_len);
		break;
	case DH_GROUP_NULL:
		/* Calculate the hash response */
		hash_value = lpfc_auth_compute_hash(hash_id,
						    &tran_id,
						    sizeof(tran_id),
						    passwd,
						    passwd_len,
						    challenge,
						    chal_len);
		rsp_key_len = 0;
		break;
	default:
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_FAILURE,
				       AUTHEXPL_BAD_PAYLOAD);
		return 0;
	}

	if (!hash_value) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS | LOG_AUTH,
				 "3046 Null hash_value\n");
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_FAILURE, AUTHEXPL_AUTH_FAILED);
		goto out;
	}

	/* Issue DHCHAP reply */
	lpfc_auth_issue_dhchap_reply(vport, ndlp, be32_to_cpu(hdr->tran_id),
				     dh_grp_id, hash_id, hash_value, rsp_key,
				     rsp_key_len);

out:
	kfree(dh_value);
	kfree(aug_chal);
	kfree(hash_value);
	return 0;
}

/**
 * lpfc_auth_handle_dhchap_reply - Handle DHCHAP Reply for a vport
 * @vport: pointer to a host virtual N_Port data structure.
 * @payload: pointer to DHCHAP Challenge cmd payload.
 * @ndlp: pointer to a node-list data structure.
 *
 * Return code
 *   0 -
 *   1 -
 **/
static int
lpfc_auth_handle_dhchap_reply(struct lpfc_vport *vport, uint8_t *payload,
			      struct lpfc_nodelist *ndlp)
{
	struct lpfc_auth_hdr *hdr;
	uint8_t tran_id = ndlp->dhc_cfg.tran_id & 0xff;
	uint32_t hash_id = ndlp->dhc_cfg.hash_id;
	uint32_t dh_grp_id = ndlp->dhc_cfg.dh_grp_id;
	uint32_t dh_val_len;
	uint8_t dh_value[256], key[256];
	uint32_t resp_len;
	uint32_t key_len = 0;
	uint8_t resp_value[20];
	uint8_t *aug_chal = NULL;
	uint8_t *hash_value = NULL;
	uint8_t challenge[20];
	uint32_t chal_len;
	char *local_passwd = ndlp->dhc_cfg.local_passwd;
	uint32_t local_passwd_len = ndlp->dhc_cfg.local_passwd_len;
	char *remote_passwd = ndlp->dhc_cfg.remote_passwd;
	uint32_t remote_passwd_len = ndlp->dhc_cfg.remote_passwd_len;
	int rc = 0;

	hdr = (struct lpfc_auth_hdr *)payload;
	/* Sanity check the AUTH message header */
	if (hdr->protocol_ver != AUTH_PROTOCOL_VER_1 ||
	    be32_to_cpu(hdr->tran_id) != ndlp->dhc_cfg.tran_id) {
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_FAILURE,
				       AUTHEXPL_BAD_PAYLOAD);
		return 0;
	}

	payload += sizeof(struct lpfc_auth_hdr);
	resp_len = be32_to_cpu(*(uint32_t *)payload);
	payload += sizeof(uint32_t);

	if (hash_id == HASH_MD5 && resp_len == MD5_CHAL_LEN) {
		memcpy(resp_value, payload, MD5_CHAL_LEN);
	} else if (hash_id == HASH_SHA1 && resp_len == SHA1_CHAL_LEN) {
		memcpy(resp_value, payload, SHA1_CHAL_LEN);
	} else {
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_FAILURE, AUTHEXPL_BAD_PAYLOAD);
		return 0;
	}

	payload += resp_len;
	dh_val_len = be32_to_cpu(*(uint32_t *)payload);
	payload += sizeof(uint32_t);

	if (dh_val_len != dh_group_array[dh_grp_id].length) {
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_FAILURE, AUTHEXPL_BAD_PAYLOAD);
		return 0;
	}

	memcpy(dh_value, payload, dh_val_len);
	payload += dh_val_len;

	/* Check for bidirectional challenge */
	chal_len = be32_to_cpu(*(uint32_t *)payload);
	payload += sizeof(uint32_t);
	if (chal_len) {
		if (chal_len != resp_len) {
			lpfc_auth_issue_reject(vport, ndlp,
					       be32_to_cpu(hdr->tran_id),
					       AUTHRJT_FAILURE,
					       AUTHEXPL_BAD_PAYLOAD);
			return 0;
		}

		memcpy(challenge, payload, chal_len);
		/* compare this challenge with what was issued */
		if (!memcmp(challenge, ndlp->dhc_cfg.rand_num, chal_len)) {
			lpfc_auth_issue_reject(vport, ndlp,
					       be32_to_cpu(hdr->tran_id),
					       AUTHRJT_FAILURE,
					       AUTHEXPL_BAD_PAYLOAD);
			return 0;
		}
	}

	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS | LOG_AUTH,
			 "6043 AUTH_REPLY tran_id: x%x hash_id: x%x "
			 "resp_len: x%x chal_len: x%x dh_grp: x%x\n",
			 tran_id, hash_id, resp_len, chal_len, dh_grp_id);

	switch (dh_grp_id) {
	case DH_GROUP_2048:
	case DH_GROUP_1536:
	case DH_GROUP_1280:
	case DH_GROUP_1024:
		key_len = dh_group_array[dh_grp_id].length;

		/* Compute Ephemeral DH Key */
		rc = lpfc_auth_compute_dhkey(dh_value, dh_val_len,
					     ndlp->dhc_cfg.nonce,
					     sizeof(ndlp->dhc_cfg.nonce),
					     dh_group_array[dh_grp_id].value,
					     dh_group_array[dh_grp_id].length,
					     key, key_len);
		if (rc)
			break;

		/*
		 * Compute the augmented challenge
		 * aug_chal = hash(Challenge || Ephemeral DH Key)
		 */
		aug_chal = lpfc_auth_compute_hash(hash_id,
						  ndlp->dhc_cfg.rand_num,
						  resp_len, key, key_len,
						  0, 0);
		if (!aug_chal) {
			rc = 1;
			goto out;
		}

		/* Calculate the hash response */
		hash_value = lpfc_auth_compute_hash(hash_id,
						    &tran_id,
						    sizeof(tran_id),
						    remote_passwd,
						    remote_passwd_len,
						    aug_chal,
						    resp_len);
		break;
	case DH_GROUP_NULL:
		hash_value = lpfc_auth_compute_hash(hash_id,
						    &tran_id, sizeof(tran_id),
						    remote_passwd,
						    remote_passwd_len,
						    ndlp->dhc_cfg.rand_num,
						    resp_len);
		break;
	default:
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_FAILURE, AUTHEXPL_BAD_PAYLOAD);
		return 0;
	}

	if (!hash_value) {
		rc = 1;
		goto out;
	}

	/* compare the hash with hash received from the remote port */
	if (memcmp(hash_value, resp_value, resp_len)) {
		lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS | LOG_AUTH,
				 "3047 Hash verification failed\n");
		rc = 1;
		goto out;
	}

	kfree(aug_chal);
	aug_chal = NULL;
	kfree(hash_value);

	/* Compute the response hash */
	if (dh_grp_id) {
		/*
		 * Compute the augmented challenge
		 * aug_chal = hash(Challenge || Ephemeral DH Key)
		 */
		aug_chal = lpfc_auth_compute_hash(hash_id,
						  challenge, chal_len,
						  key, key_len, 0, 0);

		if (!aug_chal) {
			rc = 1;
			goto out;
		}
		hash_value = lpfc_auth_compute_hash(hash_id,
						    &tran_id,
						    sizeof(tran_id),
						    local_passwd,
						    local_passwd_len,
						    aug_chal,
						    chal_len);
	} else {
		hash_value = lpfc_auth_compute_hash(hash_id,
						    &tran_id,
						    sizeof(tran_id),
						    local_passwd,
						    local_passwd_len,
						    challenge,
						    chal_len);
	}

	if (!hash_value) {
		rc = 1;
		goto out;
	}

	lpfc_auth_issue_dhchap_success(vport, ndlp, hash_value);

out:
	kfree(aug_chal);
	kfree(hash_value);
	if (rc)
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_FAILURE, AUTHEXPL_AUTH_FAILED);
	return 0;
}

/**
 * lpfc_auth_handle_dhchap_success - Handle DHCHAP Success for a vport
 * @vport: pointer to a host virtual N_Port data structure.
 * @payload: pointer to DHCHAP Challenge cmd payload.
 * @ndlp: pointer to a node-list data structure.
 *
 * Return code
 *   0 -
 *   1 -
 **/
static int
lpfc_auth_handle_dhchap_success(struct lpfc_vport *vport, uint8_t *payload,
				struct lpfc_nodelist *ndlp)
{
	struct lpfc_auth_hdr *hdr;
	uint32_t resp_len;
	uint8_t *hash_value = NULL;
	uint8_t *resp_value = NULL;
	uint8_t *aug_chal = NULL;
	uint8_t tran_id;
	uint32_t hash_id;
	uint32_t dh_grp_id;
	uint32_t chal_len = 0;
	char *local_passwd = ndlp->dhc_cfg.local_passwd;
	uint32_t local_passwd_len = ndlp->dhc_cfg.local_passwd_len;
	char *remote_passwd = ndlp->dhc_cfg.remote_passwd;
	uint32_t remote_passwd_len = ndlp->dhc_cfg.remote_passwd_len;
	int rc = 0;

	hdr = (struct lpfc_auth_hdr *)payload;
	/* Sanity check the AUTH message header */
	if (hdr->protocol_ver != AUTH_PROTOCOL_VER_1 ||
	    be32_to_cpu(hdr->tran_id) != ndlp->dhc_cfg.tran_id) {
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_FAILURE, AUTHEXPL_BAD_PAYLOAD);
		return 0;
	}
	payload += sizeof(struct lpfc_auth_hdr);

	tran_id = be32_to_cpu(hdr->tran_id) & 0xff;
	resp_len = be32_to_cpu(*((uint32_t *)payload));
	resp_value = payload + sizeof(uint32_t);

	hash_id = ndlp->dhc_cfg.hash_id;
	if (hash_id == HASH_MD5) {
		chal_len = MD5_CHAL_LEN;
	} else if (hash_id == HASH_SHA1) {
		chal_len = SHA1_CHAL_LEN;
	} else {
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_FAILURE, AUTHEXPL_BAD_PAYLOAD);
		return 0;
	}

	dh_grp_id = ndlp->dhc_cfg.dh_grp_id;
	lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS | LOG_AUTH,
			 "3048 DHCHAP_SUCCESS resp_len = x%x, tran_id=x%x "
			 "hash_id=x%x\n",
			 resp_len, tran_id, hash_id);
	if (resp_len) {
		if (resp_len != chal_len) {
			lpfc_auth_issue_reject(vport, ndlp,
					       be32_to_cpu(hdr->tran_id),
					       AUTHRJT_FAILURE,
					       AUTHEXPL_BAD_PAYLOAD);
			return 0;
		}

		if (dh_grp_id) {
			/*
			 * Compute the augmented challenge
			 * aug_chal = hash(Challenge || Ephemeral DH Key)
			 */
			aug_chal =
				lpfc_auth_compute_hash(hash_id,
						       ndlp->dhc_cfg.rand_num,
						       chal_len,
						       ndlp->dhc_cfg.dhkey,
						       ndlp->dhc_cfg.dhkey_len,
						       0, 0);
			if (!aug_chal) {
				rc = 1;
				goto out;
			}

			/* Calculate the hash response */
			hash_value = lpfc_auth_compute_hash(hash_id,
							    &tran_id,
							    sizeof(tran_id),
							    remote_passwd,
							    remote_passwd_len,
							    aug_chal,
							    chal_len);
		} else {
			/* Calculate the hash response */
			hash_value =
				lpfc_auth_compute_hash(hash_id,
						       &tran_id,
						       sizeof(tran_id),
						       remote_passwd,
						       remote_passwd_len,
						       ndlp->dhc_cfg.rand_num,
						       chal_len);
		}

		if (!hash_value) {
			lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS | LOG_AUTH,
					 "3049 Null hash_value\n");
			rc = 1;
			goto out;
		}

		/* compare the hash with hash received from the remote port */
		if (memcmp(hash_value, resp_value, resp_len)) {
			lpfc_printf_vlog(vport, KERN_INFO, LOG_ELS | LOG_AUTH,
					 "3100 Hash verification failed\n");
			rc = 1;
			goto out;
		}

		if (!dh_grp_id) {
			/* test for identical local and remote secrets */
			hash_value =
				lpfc_auth_compute_hash(hash_id,
						       &tran_id,
						       sizeof(tran_id),
						       local_passwd,
						       local_passwd_len,
						       ndlp->dhc_cfg.rand_num,
						       chal_len);
			if (!hash_value) {
				lpfc_printf_vlog(vport, KERN_INFO,
						 LOG_ELS | LOG_AUTH,
						 "3014 Null hash_value\n");
				rc = 1;
				goto out;
			}
			if (!memcmp(hash_value, resp_value, resp_len)) {
				lpfc_printf_vlog(vport, KERN_INFO,
						 LOG_ELS | LOG_AUTH,
						 "3101 Local and Remote "
						 "secrets must differ.\n");
				rc = 1;
				goto out;
			}
		}

		lpfc_auth_issue_dhchap_success(vport, ndlp, NULL);

	} else {
		ndlp->dhc_cfg.state = LPFC_AUTH_SUCCESS;
		ndlp->dhc_cfg.last_auth = jiffies;
	}
	ndlp->dhc_cfg.msg = LPFC_DHCHAP_SUCCESS;
	ndlp->dhc_cfg.direction |= AUTH_DIRECTION_REMOTE;
out:
	kfree(aug_chal);
	kfree(hash_value);
	if (rc) {
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_FAILURE, AUTHEXPL_AUTH_FAILED);
		lpfc_issue_els_logo(vport, ndlp, 0);
	}
	return 0;
}

/**
 * lpfc_auth_handle_cmd - Handle unsolicited auth cmd for a vport
 * @vport: pointer to a host virtual N_Port data structure.
 * @cmdiocb: pointer to lpfc command iocb data structure.
 * @ndlp: pointer to a node-list data structure.
 *
 * Return code
 *   0 -
 *   1 -
 **/
int
lpfc_auth_handle_cmd(struct lpfc_vport *vport, struct lpfc_iocbq *cmdiocb,
		     struct lpfc_nodelist *ndlp)
{
	struct lpfc_hba *phba = vport->phba;
	uint8_t *pcmd = NULL;
	uint32_t size, size1 = 0, size2 = 0;
	struct lpfc_auth_hdr *hdr;
	struct ulp_bde64 *pbde;
	int rc = 0;

	size = cmdiocb->iocb.unsli3.rcvsli3.acc_len;
	pcmd = kzalloc(size, GFP_KERNEL);
	if (!pcmd)
		goto out;

	size1 = cmdiocb->iocb.un.cont64[0].tus.f.bdeSize;
	memcpy(pcmd, ((struct lpfc_dmabuf *)cmdiocb->context2)->virt, size1);
	if (cmdiocb->iocb.ulpBdeCount == 2) {
		pbde = (struct ulp_bde64 *)&cmdiocb->iocb.unsli3.sli3Words[4];
		size2 = pbde->tus.f.bdeSize;
		memcpy(pcmd + size1,
		       ((struct lpfc_dmabuf *)cmdiocb->context3)->virt, size2);
	}

	lpfc_auth_cancel_tmr(ndlp);
	hdr = (struct lpfc_auth_hdr *)pcmd;

	switch (hdr->auth_msg_code) {
	case AUTH_REJECT:
		rc = lpfc_auth_handle_reject(vport, pcmd, ndlp);
		break;
	case AUTH_NEGOTIATE:
		rc = lpfc_auth_handle_negotiate(vport, pcmd, ndlp);
		break;
	case AUTH_DONE:
		/* Nothing to do */
		break;
	case DHCHAP_CHALLENGE:
		rc = lpfc_auth_handle_dhchap_chal(vport, pcmd, ndlp);
		break;
	case DHCHAP_REPLY:
		rc = lpfc_auth_handle_dhchap_reply(vport, pcmd, ndlp);
		break;
	case DHCHAP_SUCCESS:
		rc = lpfc_auth_handle_dhchap_success(vport, pcmd, ndlp);
		if (!rc) {
			/* Authentication complete */
			if (vport->port_state < LPFC_VPORT_READY) {
				if (vport->port_state != LPFC_FDISC)
					lpfc_start_fdiscs(phba);
				lpfc_do_scr_ns_plogi(phba, vport);
			}

			/* Set up reauthentication timer */
			if (ndlp->dhc_cfg.reauth_interval)
				lpfc_auth_start_reauth_tmr(ndlp);
		}
		break;
	default:
		lpfc_auth_issue_reject(vport, ndlp, be32_to_cpu(hdr->tran_id),
				       AUTHRJT_FAILURE, AUTHEXPL_BAD_PAYLOAD);
		break;
	}

out:
	kfree(pcmd);
	return rc;
}

/**
 * lpfc_auth_start - Start DH-CHAP authentication
 * @vport: pointer to a host virtual N_Port data structure.
 * @ndlp: pointer to a node-list data structure.
 *
 * This routine initiates authentication on the @vport.
 *
 * Return codes
 *   0 - Authentication required before logins continue
 *   1 - No Authentication will be done
 **/
int
lpfc_auth_start(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	struct lpfc_hba *phba = vport->phba;
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	uint8_t fcsp_en;
	int rc = 0;

	if (!phba->cfg_enable_auth)
		return 1;

	if (phba->sli4_hba.fawwpn_in_use)
		return 1;

	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp))
		return 1;

	/*
	 * If FC_DISC_DELAYED is set, delay the authentication.
	 */
	spin_lock_irq(shost->host_lock);
	if (vport->fc_flag & FC_DISC_DELAYED) {
		spin_unlock_irq(shost->host_lock);
		lpfc_printf_log(phba, KERN_ERR, LOG_DISCOVERY,
				"3435 Delay authentication for %d seconds\n",
				phba->fc_ratov);
		mod_timer(&vport->delayed_disc_tmo,
			  jiffies + msecs_to_jiffies(1000 * phba->fc_ratov));
		return 0;
	}
	spin_unlock_irq(shost->host_lock);

	fcsp_en = (ndlp->nlp_DID == Fabric_DID) ? phba->fc_fabparam.cmn.fcsp :
		   ndlp->fc_sparam.cmn.fcsp;

	/* Determine the dh-chap configuration for port pair */
	rc = lpfc_auth_lookup_cfg(ndlp);
	if (rc) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_AUTH,
				 "3042 No Authentication configuration found "
				 "for request.\n");

		if (fcsp_en) {
			ndlp->dhc_cfg.state = LPFC_AUTH_FAIL;
			lpfc_issue_els_logo(vport, ndlp, 0);
			return 0;
		}
		return 1;
	}

	switch (ndlp->dhc_cfg.auth_mode) {
	case LPFC_AUTH_MODE_PASSIVE:
		if (!fcsp_en)
			return 1;
		/* Fall Thru */
	case LPFC_AUTH_MODE_ACTIVE:
		rc = lpfc_auth_issue_negotiate(vport, ndlp);
		if (rc)
			ndlp->dhc_cfg.state = LPFC_AUTH_UNKNOWN;
		break;
	case LPFC_AUTH_MODE_DISABLE:
		if (!fcsp_en)
			return 1;
		/* fall through */
	default:
		ndlp->dhc_cfg.state = LPFC_AUTH_UNKNOWN;
		/* logout should be sent */
		lpfc_issue_els_logo(vport, ndlp, 0);
	}

	return rc;
}

/**
 * lpfc_auth_reauth - Restart DH-CHAP authentication
 * @ptr: pointer to a node-list data structure.
 *
 * This routine initiates authentication on the @vport.
 *
 **/
void
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
lpfc_auth_reauth(unsigned long ptr)
#else
lpfc_auth_reauth(struct timer_list *t)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 15, 0)
	struct lpfc_nodelist *ndlp = (struct lpfc_nodelist *)ptr;
#else
	struct lpfc_nodelist *ndlp = from_timer(ndlp, t,
						dhc_cfg.nlp_reauthfunc);
#endif
	struct lpfc_vport *vport = ndlp->vport;
	struct lpfc_hba   *phba = vport->phba;
	unsigned long flags;
	struct lpfc_work_evt  *evtp = &ndlp->reauth_evt;

	spin_lock_irqsave(&phba->hbalock, flags);
	if (!list_empty(&evtp->evt_listp)) {
		spin_unlock_irqrestore(&phba->hbalock, flags);
		return;
	}

	/* We need to hold the node by incrementing the reference
	 * count until the queued work is done
	 */
	evtp->evt_arg1  = lpfc_nlp_get(ndlp);
	if (evtp->evt_arg1) {
		evtp->evt = LPFC_EVT_REAUTH;
		list_add_tail(&evtp->evt_listp, &phba->work_list);
		lpfc_worker_wake_up(phba);
	}
	spin_unlock_irqrestore(&phba->hbalock, flags);

	lpfc_printf_vlog(vport, KERN_INFO, LOG_AUTH,
			 "3102 Start Reauthentication\n");
}

/**
 * lpfc_auth_timeout_handler - process authentication timer expiry
 * @vport: pointer to a host virtual N_Port data structure.
 * @ndlp: pointer to a node-list data structure.
 *
 * This routine authentication will clean up an incomplete authentication
 * or start reauthentication.
 *
 **/
void
lpfc_auth_timeout_handler(struct lpfc_vport *vport, struct lpfc_nodelist *ndlp)
{
	struct Scsi_Host  *shost = lpfc_shost_from_vport(vport);
	struct lpfc_work_evt *evtp;

	/* determine whether previous authentication completed */
	if (ndlp->dhc_cfg.msg == LPFC_DHCHAP_SUCCESS) {
		lpfc_auth_start(vport, ndlp);
	} else {
		ndlp->dhc_cfg.state = LPFC_AUTH_FAIL;
		ndlp->dhc_cfg.msg = LPFC_AUTH_REJECT;
		ndlp->dhc_cfg.direction = AUTH_DIRECTION_NONE;

		if (!(ndlp->nlp_flag & NLP_DELAY_TMO))
			return;
		spin_lock_irq(shost->host_lock);
		ndlp->nlp_flag &= ~NLP_DELAY_TMO;
		spin_unlock_irq(shost->host_lock);
		del_timer_sync(&ndlp->nlp_delayfunc);
		ndlp->nlp_last_elscmd = 0;
		if (!list_empty(&ndlp->els_retry_evt.evt_listp)) {
			list_del_init(&ndlp->els_retry_evt.evt_listp);
			/* Decrement ndlp ref count held for delayed retry */
			evtp = &ndlp->els_retry_evt;
			lpfc_nlp_put((struct lpfc_nodelist *)evtp->evt_arg1);
		}
	}
}

