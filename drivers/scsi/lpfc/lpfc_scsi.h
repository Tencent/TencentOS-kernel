/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2019 Broadcom. All Rights Reserved. The term *
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

#include <asm/byteorder.h>

struct lpfc_hba;
#define LPFC_FCP_CDB_LEN 16
#define CTRL_ID 0
#define LPFC_DATA_SIZE 255
#define LPFC_FCP_FB_MAX 65536

#define list_remove_head(list, entry, type, member)		\
	do {							\
	entry = NULL;						\
	if (!list_empty(list)) {				\
		entry = list_entry((list)->next, type, member);	\
		list_del_init(&entry->member);			\
	}							\
	} while(0)

#define list_get_first(list, type, member)			\
	(list_empty(list)) ? NULL :				\
	list_entry((list)->next, type, member)

/* per-port data that is allocated in the FC transport for us */
struct lpfc_rport_data {
	struct lpfc_nodelist *pnode;	/* Pointer to the node structure. */
};

struct lpfc_device_id {
	struct lpfc_name vport_wwpn;
	struct lpfc_name target_wwpn;
	uint64_t lun;
};

struct lpfc_device_data {
	struct list_head listentry;
	struct lpfc_rport_data *rport_data;
	struct lpfc_device_id device_id;
	uint8_t priority;
	bool oas_enabled;
	bool available;
};

struct fcp_rsp {
	uint32_t rspRsvd1;	/* FC Word 0, byte 0:3 */
	uint32_t rspRsvd2;	/* FC Word 1, byte 0:3 */

	uint8_t rspStatus0;	/* FCP_STATUS byte 0 (reserved) */
	uint8_t rspStatus1;	/* FCP_STATUS byte 1 (reserved) */
	uint8_t rspStatus2;	/* FCP_STATUS byte 2 field validity */
#define RSP_LEN_VALID  0x01	/* bit 0 */
#define SNS_LEN_VALID  0x02	/* bit 1 */
#define RESID_OVER     0x04	/* bit 2 */
#define RESID_UNDER    0x08	/* bit 3 */
	uint8_t rspStatus3;	/* FCP_STATUS byte 3 SCSI status byte */

	uint32_t rspResId;	/* Residual xfer if residual count field set in
				   fcpStatus2 */
	/* Received in Big Endian format */
	uint32_t rspSnsLen;	/* Length of sense data in fcpSnsInfo */
	/* Received in Big Endian format */
	uint32_t rspRspLen;	/* Length of FCP response data in fcpRspInfo */
	/* Received in Big Endian format */

	uint8_t rspInfo0;	/* FCP_RSP_INFO byte 0 (reserved) */
	uint8_t rspInfo1;	/* FCP_RSP_INFO byte 1 (reserved) */
	uint8_t rspInfo2;	/* FCP_RSP_INFO byte 2 (reserved) */
	uint8_t rspInfo3;	/* FCP_RSP_INFO RSP_CODE byte 3 */

#define RSP_NO_FAILURE       0x00
#define RSP_DATA_BURST_ERR   0x01
#define RSP_CMD_FIELD_ERR    0x02
#define RSP_RO_MISMATCH_ERR  0x03
#define RSP_TM_NOT_SUPPORTED 0x04	/* Task mgmt function not supported */
#define RSP_TM_NOT_COMPLETED 0x05	/* Task mgmt function not performed */
#define RSP_TM_INVALID_LU    0x09	/* Task mgmt function to invalid LU */

	uint32_t rspInfoRsvd;	/* FCP_RSP_INFO bytes 4-7 (reserved) */

	uint8_t rspSnsInfo[128];
#define SNS_ILLEGAL_REQ 0x05	/* sense key is byte 3 ([2]) */
#define SNSCOD_BADCMD 0x20	/* sense code is byte 13 ([12]) */
};

struct fcp_cmnd {
	struct scsi_lun  fcp_lun;

	uint8_t fcpCntl0;	/* FCP_CNTL byte 0 (reserved) */
	uint8_t fcpCntl1;	/* FCP_CNTL byte 1 task codes */
#define  SIMPLE_Q        0x00
#define  HEAD_OF_Q       0x01
#define  ORDERED_Q       0x02
#define  ACA_Q           0x04
#define  UNTAGGED        0x05
	uint8_t fcpCntl2;	/* FCP_CTL byte 2 task management codes */
#define  FCP_ABORT_TASK_SET  0x02	/* Bit 1 */
#define  FCP_CLEAR_TASK_SET  0x04	/* bit 2 */
#define  FCP_BUS_RESET       0x08	/* bit 3 */
#define  FCP_LUN_RESET       0x10	/* bit 4 */
#define  FCP_TARGET_RESET    0x20	/* bit 5 */
#define  FCP_CLEAR_ACA       0x40	/* bit 6 */
#define  FCP_TERMINATE_TASK  0x80	/* bit 7 */
	uint8_t fcpCntl3;
#define  WRITE_DATA      0x01	/* Bit 0 */
#define  READ_DATA       0x02	/* Bit 1 */

	uint8_t fcpCdb[LPFC_FCP_CDB_LEN]; /* SRB cdb field is copied here */
	uint32_t fcpDl;		/* Total transfer length */

};

struct lpfc_scsicmd_bkt {
	uint32_t cmd_count;
};

struct lpfc_scsi_io_req {
	u32 dev_id;
	u32 cmd_id;
	void (*cmd_cmpl)(struct lpfc_hba *phba, struct lpfc_iocbq *p_io_in,
			 struct lpfc_iocbq *p_io_out);
};

struct lpfc_first_burst_page {
	u8 page_code;
	u8 page_len;
	u8 buf_full_r;
	u8 buf_mt_r;
	__be16 bus_inact_lmt;
	__be16 disc_tmo;
	__be16 conn_tmo;
	__be16 max_burst;
	u8 byte12;
	u8 rsvd;
	__be16 first_burst;
};

#define LPFC_RETRY_PAUSE       300
#define LPFC_SCSI_DMA_EXT_SIZE	264
#define LPFC_BPL_SIZE		1024
#define MDAC_DIRECT_CMD		0x22

#define FIND_FIRST_OAS_LUN	0
#define NO_MORE_OAS_LUN		-1
#define NOT_OAS_ENABLED_LUN	NO_MORE_OAS_LUN

#ifndef WRITE_SAME_16
#define WRITE_SAME_16 0x93
#endif

#ifndef VERIFY_12
#define VERIFY_12 0xaf
#endif

/* ASC/ASCQ codes used in driver */
#define LPFC_ASC_DIF_ERR	0x10
#define LPFC_ASCQ_GUARD_CHECK	0x1
#define LPFC_ASCQ_REFTAG_CHECK	0x3
#define LPFC_ASC_TARGET_CHANGED	0x3f
#define LPFC_ASCQ_INQUIRY_DATA	0x3
#define LPFC_ASC_DATA_PHASE_ERR	0x4b
#define LPFC_ASCQ_EDIF_MODE	0x82

/* This macro is available from RHEL 7.1 onwards. */
#ifndef FC_PORTSPEED_32GBIT
#define FC_PORTSPEED_32GBIT     0x40
#endif

#ifndef FC_PORTSPEED_64GBIT
#define FC_PORTSPEED_64GBIT     0x1000
#endif

#ifndef FC_PORTSPEED_25GBIT
#define FC_PORTSPEED_25GBIT	0x800
#endif

#ifndef FC_PORTSPEED_40GBIT
#define FC_PORTSPEED_40GBIT	0x100
#endif

#ifndef FC_PORTSPEED_100GBIT
#define FC_PORTSPEED_100GBIT	0x400
#endif

#ifndef FC_PORTSPEED_128GBIT
#define FC_PORTSPEED_128GBIT	0x2000
#endif

#define TXRDY_PAYLOAD_LEN	12

/* Safe command codes for scanning, excluding group code */
#define LPFC_INQUIRY_CMD_CODE			(INQUIRY & 0x1f)
#define LPFC_LOG_SELECT_CMD_CODE		(LOG_SELECT & 0x1f)
#define LPFC_LOG_SENSE_CMD_CODE			(LOG_SENSE & 0x1f)
#define LPFC_MODE_SELECT_CMD_CODE		(MODE_SELECT & 0x1f)
#define LPFC_MODE_SENSE_CMD_CODE		(MODE_SENSE & 0x1f)
#define LPFC_REPORT_LUNS_CMD_CODE		(REPORT_LUNS & 0x1f)
#define LPFC_SEND_DIAGNOSTIC_CMD_CODE		(SEND_DIAGNOSTIC & 0x1f)
#define LPFC_MAINTENANCE_IN_CMD_CODE		(MAINTENANCE_IN & 0x1f)
#define LPFC_MAINTENANCE_OUT_CMD_CODE		(MAINTENANCE_OUT & 0x1f)
#define LPFC_PERSISTENT_RESERVE_IN_CMD_CODE	(PERSISTENT_RESERVE_IN & 0x1f)
#define LPFC_PERSISTENT_RESERVE_OUT_CMD_CODE	(PERSISTENT_RESERVE_OUT & 0x1f)
#define LPFC_READ_BUFFER_CMD_CODE		(READ_BUFFER & 0x1f)
#define LPFC_WRITE_BUFFER_CMD_CODE		(WRITE_BUFFER & 0x1f)

/* For sysfs/debugfs tmp string max len */
#define LPFC_MAX_SCSI_INFO_TMP_LEN	79

