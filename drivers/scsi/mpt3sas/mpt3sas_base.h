/*
 * This is the Fusion MPT base driver providing common API layer interface
 * for access to MPT (Message Passing Technology) firmware.
 *
 * This code is based on drivers/scsi/mpt3sas/mpt3sas_base.h
 * Copyright (C) 2013-2018  LSI Corporation
 * Copyright (C) 2013-2018  Avago Technologies
 * Copyright (C) 2013-2018  Broadcom Inc.
 *  (mailto:MPT-FusionLinux.pdl@broadcom.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * NO WARRANTY
 * THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
 * LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
 * solely responsible for determining the appropriateness of using and
 * distributing the Program and assumes all risks associated with its
 * exercise of rights under this Agreement, including but not limited to
 * the risks and costs of program errors, damage to or loss of data,
 * programs or equipment, and unavailability or interruption of operations.

 * DISCLAIMER OF LIABILITY
 * NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 * HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#ifndef MPT3SAS_BASE_H_INCLUDED
#define MPT3SAS_BASE_H_INCLUDED

#include "mpi/mpi2_type.h"
#include "mpi/mpi2.h"
#include "mpi/mpi2_ioc.h"
#include "mpi/mpi2_cnfg.h"
#include "mpi/mpi2_init.h"
#include "mpi/mpi2_image.h"
#include "mpi/mpi2_raid.h"
#include "mpi/mpi2_targ.h"
#include "mpi/mpi2_tool.h"
#include "mpi/mpi2_sas.h"
#include "mpi/mpi2_pci.h"

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_sas.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_eh.h>
#include <linux/pci.h>
#include <linux/poll.h>

#include "mpt3sas_compatibility.h"
#include "mpt3sas_debug.h"
#include "mpt3sas_trigger_diag.h"
#include "mpt3sas_trigger_pages.h"

/* mpt3sas driver versioning info */
#define MPT3SAS_DRIVER_NAME		"mpt3sas"
#define MPT3SAS_AUTHOR	"Broadcom Inc. <MPT-FusionLinux.pdl@broadcom.com>"
#define MPT3SAS_DESCRIPTION	"LSI MPT Fusion SAS 3.0 & SAS 3.5 Device Driver"
#define MPT3SAS_DRIVER_VERSION		"37.00.00.00"
#define MPT3SAS_MAJOR_VERSION		37
#define MPT3SAS_MINOR_VERSION           00
#define MPT3SAS_BUILD_VERSION		0
#define MPT3SAS_RELEASE_VERSION		0

/* mpt2sas driver versioning info */
#define MPT2SAS_DRIVER_NAME		"mpt2sas"
#define MPT2SAS_DESCRIPTION	"LSI MPT Fusion SAS 2.0 Device Driver"
#define MPT2SAS_DRIVER_VERSION		"20.00.03.00"
#define MPT2SAS_MAJOR_VERSION		20
#define MPT2SAS_MINOR_VERSION		0
#define MPT2SAS_BUILD_VERSION		3
#define MPT2SAS_RELEASE_VERSION		0

/* CoreDump: Default timeout */
#define MPT3SAS_DEFAULT_COREDUMP_TIMEOUT_SECONDS	(15)	/* 15 seconds */
#define MPT3SAS_TIMESYNC_TIMEOUT_SECONDS		(10)	/* 10 seconds */
#define MPT3SAS_TIMESYNC_UPDATE_INTERVAL		(900)   /* 15 minutes */
#define MPT3SAS_TIMESYNC_UNIT_MASK			(0x80)	/* bit 7 */
#define MPT3SAS_TIMESYNC_MASK			(0x7F)	/* 0 - 6 bits */
#define SECONDS_PER_MIN					(60)
#define SECONDS_PER_HOUR				(3600)
#define MPT3SAS_COREDUMP_LOOP_DONE			(0xFF)
#define MPI26_SET_IOC_PARAMETER_SYNC_TIMESTAMP		(0x81)

/*
 * Set MPT3SAS_SG_DEPTH value based on user input.
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25))
#define MPT_MAX_PHYS_SEGMENTS   MAX_PHYS_SEGMENTS
#elif ((defined(CONFIG_SUSE_KERNEL) && (LINUX_VERSION_CODE >= \
    KERNEL_VERSION(4,4,59))) || (LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0)))
#define MPT_MAX_PHYS_SEGMENTS  SG_CHUNK_SIZE
#else
#define MPT_MAX_PHYS_SEGMENTS   SCSI_MAX_SG_SEGMENTS
#endif
#define MPT_MIN_PHYS_SEGMENTS	16
#define MPT_KDUMP_MIN_PHYS_SEGMENTS	32

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,7,0) || (defined(CONFIG_SUSE_KERNEL) && (LINUX_VERSION_CODE >= \
											KERNEL_VERSION(4,4,59))))
#define MPT_MAX_SG_SEGMENTS   SG_MAX_SEGMENTS
#define MPT_MAX_PHYS_SEGMENTS_STRING "SG_CHUNK_SIZE"
#else
#define MPT_MAX_SG_SEGMENTS   SCSI_MAX_SG_CHAIN_SEGMENTS
#define MPT_MAX_PHYS_SEGMENTS_STRING "SCSI_MAX_SG_SEGMENTS"
#endif

#define MCPU_MAX_CHAINS_PER_IO	3

#ifdef CONFIG_SCSI_MPT3SAS_MAX_SGE
#define MPT3SAS_SG_DEPTH		CONFIG_SCSI_MPT3SAS_MAX_SGE
#else
#define MPT3SAS_SG_DEPTH		MPT_MAX_PHYS_SEGMENTS
#endif

#ifdef CONFIG_SCSI_MPT2SAS_MAX_SGE
#define MPT2SAS_SG_DEPTH		CONFIG_SCSI_MPT2SAS_MAX_SGE
#else
#define MPT2SAS_SG_DEPTH		MPT_MAX_PHYS_SEGMENTS
#endif

#if 0
#if defined(TARGET_MODE)
#include "../target/mpt3sas_stm_common.h"
#endif
#endif

#if (((defined(CONFIG_SUSE_KERNEL) || defined(RHEL_RELEASE_CODE)) && \
	LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)) || \
	LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33))
#define RAMP_UP_SUPPORT
#endif

#ifndef U32_MAX
#define U32_MAX		((u32)~0U)
#endif /* !U32_MAX */

/*
 * Generic Defines
 */
#define MPT3SAS_SATA_QUEUE_DEPTH	32
#define MPT3SAS_SAS_QUEUE_DEPTH		254
#define MPT3SAS_RAID_QUEUE_DEPTH	128
#define MPT3SAS_KDUMP_SCSI_IO_DEPTH	200
#define MPT3SAS_HOST_PAGE_SIZE_4K	12
#define MPT3SAS_NVME_QUEUE_DEPTH	128

#define MPT3SAS_RAID_MAX_SECTORS        8192

#define MPT_NAME_LENGTH			32	/* generic length of strings */
#define MPT_DRIVER_NAME_LENGTH		24
#define MPT_STRING_LENGTH		64
#define MPI_FRAME_START_OFFSET		256
#define REPLY_FREE_POOL_SIZE            512 /*(32 maxcredit *4)*(4 times)*/

#define MPT_MAX_CALLBACKS		32
#if defined(TARGET_MODE)
#undef  MPT_MAX_CALLBACKS
#define MPT_MAX_CALLBACKS		32
#endif


#define INTERNAL_CMDS_COUNT		10	/* reserved cmds */
/* reserved for issuing internally framed scsi io cmds */
#define INTERNAL_SCSIIO_CMDS_COUNT	3
#define INTERNAL_SCSIIO_FOR_IOCTL	1
#define INTERNAL_SCSIIO_FOR_DISCOVERY	2

#define MPI3_HIM_MASK			0xFFFFFFFF /* mask every bit*/

#define MPT3SAS_INVALID_DEVICE_HANDLE	0xFFFF

#define MAX_CHAIN_ELEMT_SZ		16
#define DEFAULT_NUM_FWCHAIN_ELEMTS	8

/*
 * NVMe defines
 */
#define	NVME_PRP_SIZE			8	/* PRP size */
#define	NVME_ERROR_RESPONSE_SIZE	16	/* Max NVME Error Response */
#define	NVME_PRP_PAGE_SIZE		4096	/* Page size */

#define NVME_TASK_ABORT_MIN_TIMEOUT	6
#define NVME_TASK_ABORT_MAX_TIMEOUT	60
#define NVME_TASK_MNGT_CUSTOM_MASK	(0x0010)

struct mpt3sas_nvme_cmd {
	u8	rsvd[24];
	u64	prp1;
	u64	prp2;
};

/*
 * reset phases
 */
#define MPT3_IOC_PRE_RESET		1 /* prior to host reset */
#define MPT3_IOC_AFTER_RESET		2 /* just after host reset */
#define MPT3_IOC_DONE_RESET		3 /* links re-initialized */

/*
 * logging format
 */
#define MPT3SAS_FMT			"%s: "
#define MPT3SAS_INFO_FMT		KERN_INFO MPT3SAS_FMT
#define MPT3SAS_NOTE_FMT		KERN_NOTICE MPT3SAS_FMT
#define MPT3SAS_WARN_FMT		KERN_WARNING MPT3SAS_FMT
#define MPT3SAS_ERR_FMT			KERN_ERR MPT3SAS_FMT

/*
 *  WarpDrive Specific Log codes
 */

#define MPT2_WARPDRIVE_LOGENTRY		(0x8002)
#define MPT2_WARPDRIVE_LC_SSDT		(0x41)
#define MPT2_WARPDRIVE_LC_SSDLW		(0x43)
#define MPT2_WARPDRIVE_LC_SSDLF		(0x44)
#define MPT2_WARPDRIVE_LC_BRMF		(0x4D)

/*
 * per target private data
 */
#define MPT_TARGET_FLAGS_RAID_COMPONENT	0x01
#define MPT_TARGET_FLAGS_VOLUME		0x02
#define MPT_TARGET_FLAGS_DELETED	0x04
#define MPT_TARGET_FASTPATH_IO		0x08
#define MPT_TARGET_FLAGS_PCIE_DEVICE	0x10

#define SAS2_PCI_DEVICE_B0_REVISION         (0x01)
#define SAS3_PCI_DEVICE_C0_REVISION         (0x02)

/* MSI-x ReplyPostRegister defines */
#define NUM_REPLY_POST_INDEX_REGISTERS_16	16
#define NUM_REPLY_POST_INDEX_REGISTERS_12	12
#define MAX_COMBINED_MSIX_VECTORS(gen35) ((gen35 == 1) ? 16 : 8)

/* Enable DDIO counters */
#define MPT2SAS_WD_DDIOCOUNT
#if 0 /* remove to turn on WD debug messages */
#define MPT2SAS_WD_LOGGING
#endif

#define IO_UNIT_CONTROL_SHUTDOWN_TIMEOUT 6
#define FW_IMG_HDR_READ_TIMEOUT 15

/*
 * Intel SAS2 HBA branding
 */
#define MPT2SAS_INTEL_RMS25JB080_BRANDING	\
	"Intel(R) Integrated RAID Module RMS25JB080"
#define MPT2SAS_INTEL_RMS25JB040_BRANDING	\
	"Intel(R) Integrated RAID Module RMS25JB040"
#define MPT2SAS_INTEL_RMS25KB080_BRANDING	\
	"Intel(R) Integrated RAID Module RMS25KB080"
#define MPT2SAS_INTEL_RMS25KB040_BRANDING	\
	"Intel(R) Integrated RAID Module RMS25KB040"
#define MPT2SAS_INTEL_RMS25LB040_BRANDING	\
	"Intel(R) Integrated RAID Module RMS25LB040"
#define MPT2SAS_INTEL_RMS25LB080_BRANDING	\
	"Intel(R) Integrated RAID Module RMS25LB080"
#define MPT2SAS_INTEL_RMS2LL080_BRANDING	\
	"Intel Integrated RAID Module RMS2LL080"
#define MPT2SAS_INTEL_RMS2LL040_BRANDING	\
	"Intel Integrated RAID Module RMS2LL040"
#define MPT2SAS_INTEL_RS25GB008_BRANDING	\
	"Intel(R) RAID Controller RS25GB008"
#define MPT2SAS_INTEL_SSD910_BRANDING		\
	"Intel(R) SSD 910 Series"

/*
 * Intel SAS2 HBA SSDIDs
 */
#define MPT2SAS_INTEL_RMS25JB080_SSDID         0x3516
#define MPT2SAS_INTEL_RMS25JB040_SSDID         0x3517
#define MPT2SAS_INTEL_RMS25KB080_SSDID         0x3518
#define MPT2SAS_INTEL_RMS25KB040_SSDID         0x3519
#define MPT2SAS_INTEL_RMS25LB040_SSDID         0x351A
#define MPT2SAS_INTEL_RMS25LB080_SSDID         0x351B
#define MPT2SAS_INTEL_RMS2LL080_SSDID          0x350E
#define MPT2SAS_INTEL_RMS2LL040_SSDID          0x350F
#define MPT2SAS_INTEL_RS25GB008_SSDID          0x3000
#define MPT2SAS_INTEL_SSD910_SSDID             0x3700

/*
 * Intel SAS3 HBA branding
 */
#define MPT3SAS_INTEL_RMS3JC080_BRANDING       \
	"Intel(R) Integrated RAID Module RMS3JC080"
#define MPT3SAS_INTEL_RS3GC008_BRANDING       \
        "Intel(R) RAID Controller RS3GC008"
#define MPT3SAS_INTEL_RS3FC044_BRANDING       \
        "Intel(R) RAID Controller RS3FC044"
#define MPT3SAS_INTEL_RS3UC080_BRANDING       \
        "Intel(R) RAID Controller RS3UC080"
#define MPT3SAS_INTEL_RS3PC_BRANDING       \
        "Intel(R) RAID Integrated RAID RS3PC"

/*
 * Intel SAS3 HBA SSDIDs
 */
#define MPT3SAS_INTEL_RMS3JC080_SSDID        0x3521
#define MPT3SAS_INTEL_RS3GC008_SSDID         0x3522
#define MPT3SAS_INTEL_RS3FC044_SSDID         0x3523
#define MPT3SAS_INTEL_RS3UC080_SSDID         0x3524
#define MPT3SAS_INTEL_RS3PC_SSDID            0x3527

/*
 * Dell SAS2 HBA branding
 */
#define MPT2SAS_DELL_6GBPS_SAS_HBA_BRANDING        "Dell 6Gbps SAS HBA"
#define MPT2SAS_DELL_PERC_H200_ADAPTER_BRANDING    "Dell PERC H200 Adapter"
#define MPT2SAS_DELL_PERC_H200_INTEGRATED_BRANDING "Dell PERC H200 Integrated"
#define MPT2SAS_DELL_PERC_H200_MODULAR_BRANDING    "Dell PERC H200 Modular"
#define MPT2SAS_DELL_PERC_H200_EMBEDDED_BRANDING   "Dell PERC H200 Embedded"
#define MPT2SAS_DELL_PERC_H200_BRANDING            "Dell PERC H200"
#define MPT2SAS_DELL_6GBPS_SAS_BRANDING            "Dell 6Gbps SAS"

/*
 * Dell SAS2 HBA SSDIDs
 */
#define MPT2SAS_DELL_6GBPS_SAS_HBA_SSDID           0x1F1C
#define MPT2SAS_DELL_PERC_H200_ADAPTER_SSDID       0x1F1D
#define MPT2SAS_DELL_PERC_H200_INTEGRATED_SSDID    0x1F1E
#define MPT2SAS_DELL_PERC_H200_MODULAR_SSDID       0x1F1F
#define MPT2SAS_DELL_PERC_H200_EMBEDDED_SSDID      0x1F20
#define MPT2SAS_DELL_PERC_H200_SSDID               0x1F21
#define MPT2SAS_DELL_6GBPS_SAS_SSDID               0x1F22
/*
 * Dell SAS3 HBA branding
 */
#define MPT3SAS_DELL_HBA330_ADP_BRANDING	\
	"Dell HBA330 Adp"
#define MPT3SAS_DELL_12G_HBA_BRANDING       \
        "Dell 12Gbps SAS HBA"
#define MPT3SAS_DELL_HBA330_MINI_BRANDING	\
	"Dell HBA330 Mini"

/*
 * Dell SAS3 HBA SSDIDs
 */
#define MPT3SAS_DELL_HBA330_ADP_SSDID	0x1F45
#define MPT3SAS_DELL_12G_HBA_SSDID	0x1F46
#define MPT3SAS_DELL_HBA330_MINI_SSDID	0x1F53

/*
 * Cisco SAS3 HBA branding
 */
#define MPT3SAS_CISCO_12G_8E_HBA_BRANDING       \
        "Cisco 9300-8E 12G SAS HBA"
#define MPT3SAS_CISCO_12G_8I_HBA_BRANDING       \
        "Cisco 9300-8i 12G SAS HBA"
#define MPT3SAS_CISCO_12G_AVILA_HBA_BRANDING       \
        "Cisco 12G Modular SAS Pass through Controller"
#define MPT3SAS_CISCO_12G_COLUSA_MEZZANINE_HBA_BRANDING       \
        "UCS C3X60 12G SAS Pass through Controller"
/*
 * Cisco SAS3 HBA SSSDIDs
 */
#define MPT3SAS_CISCO_12G_8E_HBA_SSDID  0x14C
#define MPT3SAS_CISCO_12G_8I_HBA_SSDID  0x154
#define MPT3SAS_CISCO_12G_AVILA_HBA_SSDID  0x155
#define MPT3SAS_CISCO_12G_COLUSA_MEZZANINE_HBA_SSDID  0x156

/*
 * HP SAS2 HBA branding
 */
#define MPT2SAS_HP_3PAR_SSVID			0x1590
#define MPT2SAS_HP_2_4_INTERNAL_BRANDING	\
	"HP H220 Host Bus Adapter"
#define MPT2SAS_HP_2_4_EXTERNAL_BRANDING	\
	"HP H221 Host Bus Adapter"
#define MPT2SAS_HP_1_4_INTERNAL_1_4_EXTERNAL_BRANDING	\
	"HP H222 Host Bus Adapter"
#define MPT2SAS_HP_EMBEDDED_2_4_INTERNAL_BRANDING	\
	"HP H220i Host Bus Adapter"
#define MPT2SAS_HP_DAUGHTER_2_4_INTERNAL_BRANDING	\
	"HP H210i Host Bus Adapter"

/*
 * HP SAS2 HBA SSDIDs
 */
#define MPT2SAS_HP_2_4_INTERNAL_SSDID			0x0041
#define MPT2SAS_HP_2_4_EXTERNAL_SSDID			0x0042
#define MPT2SAS_HP_1_4_INTERNAL_1_4_EXTERNAL_SSDID	0x0043
#define MPT2SAS_HP_EMBEDDED_2_4_INTERNAL_SSDID		0x0044
#define MPT2SAS_HP_DAUGHTER_2_4_INTERNAL_SSDID		0x0046

 /*
 * status bits for ioc->diag_buffer_status
 */
#define MPT3_DIAG_BUFFER_IS_REGISTERED	(0x01)
#define MPT3_DIAG_BUFFER_IS_RELEASED	(0x02)
#define MPT3_DIAG_BUFFER_IS_DIAG_RESET	(0x04)
#define MPT3_DIAG_BUFFER_IS_DRIVER_ALLOCATED (0x08)
#define MPT3_DIAG_BUFFER_IS_APP_OWNED (0x10)

/*
 *  End to End Data Protection Support
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27))

#define PRO_R MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP
#define PRO_W MPI2_SCSIIO_EEDPFLAGS_INSERT_OP
#define PRO_V MPI2_SCSIIO_EEDPFLAGS_INSERT_OP

/* the read capacity 16 byte parameter block - defined in SBC-3 */
struct read_cap_parameter {
	u64	logical_block_addr;
	u32	logical_block_length;
	u8	prot_en:1;
	u8	p_type:3;
	u8	reserved0:4;
	u8	logical_blocks_per_phyical_block:4;
	u8	reserved1:4;
	u16	lowest_aligned_log_block_address:14;
	u16	reserved2:2;
	u8	reserved3[16];
};
#endif

/* OEM Identifiers */
#define MFG10_OEM_ID_INVALID                   (0x00000000)
#define MFG10_OEM_ID_DELL                      (0x00000001)
#define MFG10_OEM_ID_FSC                       (0x00000002)
#define MFG10_OEM_ID_SUN                       (0x00000003)
#define MFG10_OEM_ID_IBM                       (0x00000004)

/* GENERIC Flags 0*/
#define MFG10_GF0_OCE_DISABLED                 (0x00000001)
#define MFG10_GF0_R1E_DRIVE_COUNT              (0x00000002)
#define MFG10_GF0_R10_DISPLAY                  (0x00000004)
#define MFG10_GF0_SSD_DATA_SCRUB_DISABLE       (0x00000008)
#define MFG10_GF0_SINGLE_DRIVE_R0              (0x00000010)

/* SCSI ADDITIONAL SENSE Codes */
#define FIXED_SENSE_DATA				0x70
#define SCSI_ASC_NO_SENSE				0x00
#define SCSI_ASC_PERIPHERAL_DEV_WRITE_FAULT		0x03
#define SCSI_ASC_LUN_NOT_READY				0x04
#define SCSI_ASC_WARNING				0x0B
#define SCSI_ASC_LOG_BLOCK_GUARD_CHECK_FAILED		0x10
#define SCSI_ASC_LOG_BLOCK_APPTAG_CHECK_FAILED		0x10
#define SCSI_ASC_LOG_BLOCK_REFTAG_CHECK_FAILED		0x10
#define SCSI_ASC_UNRECOVERED_READ_ERROR			0x11
#define SCSI_ASC_MISCOMPARE_DURING_VERIFY		0x1D
#define SCSI_ASC_ACCESS_DENIED_INVALID_LUN_ID		0x20
#define SCSI_ASC_ILLEGAL_COMMAND			0x20
#define SCSI_ASC_ILLEGAL_BLOCK				0x21
#define SCSI_ASC_INVALID_CDB				0x24
#define SCSI_ASC_INVALID_LUN				0x25
#define SCSI_ASC_INVALID_PARAMETER			0x26
#define SCSI_ASC_FORMAT_COMMAND_FAILED			0x31
#define SCSI_ASC_INTERNAL_TARGET_FAILURE		0x44

/* SCSI ADDITIONAL SENSE Code Qualifiers */

#define SCSI_ASCQ_CAUSE_NOT_REPORTABLE			0x00
#define SCSI_ASCQ_FORMAT_COMMAND_FAILED			0x01
#define SCSI_ASCQ_LOG_BLOCK_GUARD_CHECK_FAILED		0x01
#define SCSI_ASCQ_LOG_BLOCK_APPTAG_CHECK_FAILED		0x02
#define SCSI_ASCQ_LOG_BLOCK_REFTAG_CHECK_FAILED		0x03
#define SCSI_ASCQ_FORMAT_IN_PROGRESS			0x04
#define SCSI_ASCQ_POWER_LOSS_EXPECTED			0x08
#define SCSI_ASCQ_INVALID_LUN_ID			0x09

/* High IOPs definitions */
#define MPT3SAS_DEVICE_HIGH_IOPS_DEPTH		8
#define MPT3SAS_HIGH_IOPS_REPLY_QUEUES		8
#define MPT3SAS_HIGH_IOPS_BATCH_COUNT		16
#define MPT3SAS_GEN35_MAX_MSIX_QUEUES		128
#define RDPQ_MAX_INDEX_IN_ONE_CHUNK		16

/* Get status code type(bits 27-25) with status code(bits 24-17)
 * value from status field.
 */
#define get_nvme_sct_with_sc(nvme_status)	\
	((nvme_status & 0xFFE) >> 1)

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33))
#define UNMAP	0x42
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))

/* DataSetManagment commands attributes */
enum {
	NVME_DSMGMT_IDR		= 1 << 0,
	NVME_DSMGMT_IDW		= 1 << 1,
	NVME_DSMGMT_AD		= 1 << 2,
};

/* I/O commands */
enum nvme_opcode {
	nvme_cmd_flush		= 0x00,
	nvme_cmd_write		= 0x01,
	nvme_cmd_read		= 0x02,
	nvme_cmd_write_uncor	= 0x04,
	nvme_cmd_compare	= 0x05,
	nvme_cmd_write_zeroes	= 0x08,
	nvme_cmd_dsm		= 0x09,
	nvme_cmd_resv_register	= 0x0d,
	nvme_cmd_resv_report	= 0x0e,
	nvme_cmd_resv_acquire	= 0x11,
	nvme_cmd_resv_release	= 0x15,
};

/* NVMe commmands status */
enum {
	NVME_SC_SUCCESS			= 0x0,
	NVME_SC_INVALID_OPCODE		= 0x1,
	NVME_SC_INVALID_FIELD		= 0x2,
	NVME_SC_CMDID_CONFLICT		= 0x3,
	NVME_SC_DATA_XFER_ERROR		= 0x4,
	NVME_SC_POWER_LOSS		= 0x5,
	NVME_SC_INTERNAL		= 0x6,
	NVME_SC_ABORT_REQ		= 0x7,
	NVME_SC_ABORT_QUEUE		= 0x8,
	NVME_SC_FUSED_FAIL		= 0x9,
	NVME_SC_FUSED_MISSING		= 0xa,
	NVME_SC_INVALID_NS		= 0xb,
	NVME_SC_CMD_SEQ_ERROR		= 0xc,
	NVME_SC_SGL_INVALID_LAST	= 0xd,
	NVME_SC_SGL_INVALID_COUNT	= 0xe,
	NVME_SC_SGL_INVALID_DATA	= 0xf,
	NVME_SC_SGL_INVALID_METADATA	= 0x10,
	NVME_SC_SGL_INVALID_TYPE	= 0x11,
	NVME_SC_LBA_RANGE		= 0x80,
	NVME_SC_CAP_EXCEEDED		= 0x81,
	NVME_SC_NS_NOT_READY		= 0x82,
	NVME_SC_RESERVATION_CONFLICT	= 0x83,
	NVME_SC_CQ_INVALID		= 0x100,
	NVME_SC_QID_INVALID		= 0x101,
	NVME_SC_QUEUE_SIZE		= 0x102,
	NVME_SC_ABORT_LIMIT		= 0x103,
	NVME_SC_ABORT_MISSING		= 0x104,
	NVME_SC_ASYNC_LIMIT		= 0x105,
	NVME_SC_FIRMWARE_SLOT		= 0x106,
	NVME_SC_FIRMWARE_IMAGE		= 0x107,
	NVME_SC_INVALID_VECTOR		= 0x108,
	NVME_SC_INVALID_LOG_PAGE	= 0x109,
	NVME_SC_INVALID_FORMAT		= 0x10a,
	NVME_SC_FIRMWARE_NEEDS_RESET	= 0x10b,
	NVME_SC_INVALID_QUEUE		= 0x10c,
	NVME_SC_FEATURE_NOT_SAVEABLE	= 0x10d,
	NVME_SC_FEATURE_NOT_CHANGEABLE	= 0x10e,
	NVME_SC_FEATURE_NOT_PER_NS	= 0x10f,
	NVME_SC_FW_NEEDS_RESET_SUBSYS	= 0x110,
	NVME_SC_BAD_ATTRIBUTES		= 0x180,
	NVME_SC_INVALID_PI		= 0x181,
	NVME_SC_READ_ONLY		= 0x182,
	NVME_SC_WRITE_FAULT		= 0x280,
	NVME_SC_READ_ERROR		= 0x281,
	NVME_SC_GUARD_CHECK		= 0x282,
	NVME_SC_APPTAG_CHECK		= 0x283,
	NVME_SC_REFTAG_CHECK		= 0x284,
	NVME_SC_COMPARE_FAILED		= 0x285,
	NVME_SC_ACCESS_DENIED		= 0x286,
	NVME_SC_DNR			= 0x4000,
};

/* DSM range Definition */
struct nvme_dsm_range {
	__le32			cattr;
	__le32			nlb;
	__le64			slba;
};

/* DataSetManagement command */
struct nvme_dsm_cmd {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__le32			nsid;
	__u64			rsvd2[2];
	__le64			prp1;
	__le64			prp2;
	__le32			nr;
	__le32			attributes;
	__u32			rsvd12[4];
};

/* Generic NVMe command */
struct nvme_command {
	union {
		struct nvme_dsm_cmd dsm;
	};
};

/* NVMe completion queue entry */
struct nvme_completion {
	__le32	result;		/* Used by admin commands to return data */
	__u32	rsvd;
	__le16	sq_head;	/* how much of this queue may be reclaimed */
	__le16	sq_id;		/* submission queue that generated this entry */
	__u16	command_id;	/* of the command which completed */
	__le16	status;		/* did the command fail, and if so, why? */
};
#endif

/* SCSI UNMAP block descriptor structure */
struct scsi_unmap_blk_desc {
	__be64  slba;
	__be32  nlb;
	u32     resv;
};

/* SCSI UNMAP command's data */
struct scsi_unmap_parm_list {
	__be16  unmap_data_len;
	__be16  unmap_blk_desc_data_len;
	u32     resv;
	struct scsi_unmap_blk_desc desc[0];
};

#ifndef SCSI_MPT2SAS
#define IFAULT_IOP_OVER_TEMP_THRESHOLD_EXCEEDED        (0x2810) /* Over temp threshold has exceeded. */
#endif

/* OEM Specific Flags will come from OEM specific header files */
struct Mpi2ManufacturingPage10_t {
	MPI2_CONFIG_PAGE_HEADER	Header;		/* 00h */
	U8	OEMIdentifier;			/* 04h */
	U8	Reserved1;			/* 05h */
	U16	Reserved2;			/* 08h */
	U32	Reserved3;			/* 0Ch */
	U32	GenericFlags0;			/* 10h */
	U32	GenericFlags1;			/* 14h */
	U32	Reserved4;			/* 18h */
	U32	OEMSpecificFlags0;		/* 1Ch */
	U32	OEMSpecificFlags1;		/* 20h */
	U32	Reserved5[18];			/* 24h - 60h*/
};

/* Miscellaneous options */
struct Mpi2ManufacturingPage11_t {
	MPI2_CONFIG_PAGE_HEADER Header;		/* 00h */
	__le32	Reserved1;			/* 04h */
	u8	Reserved2;			/* 08h */
	u8	EEDPTagMode;			/* 09h */
	u8	Reserved3;			/* 0Ah */
	u8	Reserved4;			/* 0Bh */
	__le32	Reserved5[8];			/* 0Ch-2Ch */
	u16	AddlFlags2;			/* 2Ch */
	u8	AddlFlags3;			/* 2Eh */
	u8	Reserved6;			/* 2Fh */
	__le32	Reserved7[7];			/* 30h - 4Bh */
	u8	NVMeAbortTO;			/* 4Ch */
	u8	NumPerDevEvents;		/* 4Dh */
	u8	HostTraceBufferDecrementSizeKB;	/* 4Eh */
	u8	HostTraceBufferFlags;		/* 4Fh */
	u16	HostTraceBufferMaxSizeKB;	/* 50h */
	u16	HostTraceBufferMinSizeKB;	/* 52h */
	u8	CoreDumpTOSec;			/* 54h */
	u8	TimeSyncInterval;		/* 55h */
	u16 Reserved9;                  /* 56h */
	__le32	Reserved10;	            /* 58h */
};

/**
 * struct MPT3SAS_TARGET - starget private hostdata
 * @starget: starget object
 * @sas_address: target sas address
 * @handle: device handle
 * @num_luns: number luns
 * @flags: MPT_TARGET_FLAGS_XXX flags
 * @deleted: target flaged for deletion
 * @tm_busy: target is busy with TM request.
 * @port: hba port entry
 * @sdev: The sas_device associated with this target
 */
struct MPT3SAS_TARGET {
	struct scsi_target *starget;
	u64	sas_address;
	struct _raid_device *raid_device;
	u16	handle;
	int	num_luns;
	u32	flags;
	u8	deleted;
	u8	tm_busy;
	struct hba_port *port;
	struct	_sas_device *sas_dev;
	struct	_pcie_device *pcie_dev;
};

/*
 * per device private data
 */
#define MPT_DEVICE_FLAGS_INIT		0x01
#define MPT_DEVICE_TLR_ON		0x02

/**
 * struct MPT3SAS_DEVICE - sdev private hostdata
 * @sas_target: starget private hostdata
 * @lun: lun number
 * @flags: MPT_DEVICE_XXX flags
 * @configured_lun: lun is configured
 * @block: device is in SDEV_BLOCK state
 * @tlr_snoop_check: flag used in determining whether to disable TLR
 * @eedp_enable: eedp support enable bit
 * @eedp_type: 0(type_1), 1(type_2), 2(type_3)
 * @eedp_block_length: block size
 * @ata_command_pending: SATL passthrough outstanding for device
 */
struct MPT3SAS_DEVICE {
	struct MPT3SAS_TARGET *sas_target;
	unsigned int	lun;
	u32	flags;
	u8	configured_lun;
	u8	block;
	u8	deleted;
	u8	tlr_snoop_check;
	u8	ignore_delay_remove;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27))
	u8	eedp_enable;
	u8	eedp_type;
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0))
 /* Iopriority Command Handling */
	u8	ncq_prio_enable;
#endif
/*
* Bug workaround for SATL handling: the mpt2/3sas firmware
* doesn't return BUSY or TASK_SET_FULL for subsequent
* commands while a SATL pass through is in operation as the
* spec requires, it simply does nothing with them until the
* pass through completes, causing them possibly to timeout if
* the passthrough is a long executing command (like format or
* secure erase).  This variable allows us to do the right
* thing while a SATL command is pending.
*/
	unsigned long ata_command_pending;
};

/* Bit indicates ATA command pending or not */
#define CMND_PENDING_BIT 0

#define MPT3_CMD_NOT_USED	0x8000	/* free */
#define MPT3_CMD_COMPLETE	0x0001	/* completed */
#define MPT3_CMD_PENDING	0x0002	/* pending */
#define MPT3_CMD_REPLY_VALID	0x0004	/* reply is valid */
#define MPT3_CMD_RESET		0x0008	/* host reset dropped the command */

/**
 * struct _internal_cmd - internal commands struct
 * @mutex: mutex
 * @done: completion
 * @reply: reply message pointer
 * @sense: sense data
 * @status: MPT3_CMD_XXX status
 * @smid: system message id
 */
struct _internal_cmd {
	struct mutex mutex;
	struct completion done;
	void	*reply;
	void	*sense;
	u16	status;
	u16	smid;
};
/**
 * struct _internal_qcmd - internal q commands struct
 * @list: list of internal q cmds
 * @request: request message pointer
 * @reply: reply message pointer
 * @sense: sense data
 * @status: MPT2_CMD_XXX status
 * @smid: system message id
 * @transfer_packet: SCSI IO transfer packet pointer
 */
struct _internal_qcmd {
	struct list_head list;
	void *request;
	void	*reply;
	void	*sense;
	u16	status;
	u16	smid;
	struct _scsi_io_transfer *transfer_packet;
};
#if (defined(CONFIG_SUSE_KERNEL) && defined(scsi_is_sas_phy_local)) || \
	LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
#define MPT_WIDE_PORT_API	1
#define MPT_WIDE_PORT_API_PLUS	1
#endif

#define MFG_PAGE10_HIDE_SSDS_MASK	(0x00000003)
#define MFG_PAGE10_HIDE_ALL_DISKS	(0x00)
#define MFG_PAGE10_EXPOSE_ALL_DISKS	(0x01)
#define MFG_PAGE10_HIDE_IF_VOL_PRESENT	(0x02)

/**
 * struct _sas_device - attached device information
 * @list: sas device list
 * @starget: starget object
 * @sas_address: device sas address
 * @device_name: retrieved from the SAS IDENTIFY frame.
 * @handle: device handle
 * @sas_address_parent: sas address of parent expander or sas host
 * @enclosure_handle: enclosure handle
 * @enclosure_logical_id: enclosure logical identifier
 * @volume_handle: volume handle (valid when hidden raid member)
 * @volume_wwid: volume unique identifier
 * @device_info: bitfield provides detailed info about the device
 * @id: target id
 * @channel: target channel
 * @slot: number number
 * @phy: phy identifier provided in sas device page 0
 * @responding: used in _scsih_sas_device_mark_responding
 * @fast_path: fast path feature enable bit
 * @pfa_led_on: flag for PFA LED status
 * @pend_sas_rphy_add: flag to check if device is in sas_rphy_add()
 *     addition routine
 * @enclosure_level: used for enclosure services
 * @chassis_slot: chassis slot
 * @is_chassis_slot_valid: chassis slot valid or not
 * @connector_name: ASCII value from pg0.ConnectorName
 * @refcount: reference count for deletion
 * @port: hba port entry
 * @rphy: device's sas_rphy address used to identify this device structure in
 *	  target_alloc callback function
 */
struct _sas_device {
	struct list_head list;
	struct scsi_target *starget;
	u64	sas_address;
	u64	device_name;
	u16	handle;
	u64	sas_address_parent;
	u16	enclosure_handle;
	u64	enclosure_logical_id;
	u16	volume_handle;
	u64	volume_wwid;
	u32	device_info;
	int	id;
	int	channel;
	u16	slot;
	u8	phy;
	u8	responding;
	u8	fast_path;
	u8	pfa_led_on;
	struct kref refcount;
	u8	*serial_number;
	u8      pend_sas_rphy_add;
	u8	enclosure_level;
	u8	chassis_slot;
	u8	is_chassis_slot_valid;
	u8	connector_name[5];
	u8	ssd_device;
#ifndef SCSI_MPT2SAS
	u8	supports_sata_smart;
#endif
	struct hba_port *port;
	struct sas_rphy *rphy;
};

/**
 * sas_device_get - Increment the sas device reference count
 * @s : sas_device object
 *
 * When ever this function called it will increment the
 * reference count of the sas device for which this function called.
 *
 */
static inline void sas_device_get(struct _sas_device *s)
{
	kref_get(&s->refcount);
}

/**
 * sas_device_free - Release the sas device object
 * @r - kref object
 *
 * Free's the sas device object. It will be called
 * when reference count reaches to zero.
 */
static inline void sas_device_free(struct kref *r)
{
	kfree(container_of(r, struct _sas_device, refcount));
}

/**
 * sas_device_put - Decrement the sas device reference count
 * @s : sas_device object
 *
 * When ever this function called it will decrement the
 * reference count of the sas device for which this function called.
 *
 * When refernce count reaches to Zero, this will call sas_device_free
 * to the sas_device object.
 */
static inline void sas_device_put(struct _sas_device *s)
{
	kref_put(&s->refcount, sas_device_free);
}

/**
* struct _pcie_device - attached PCIe device information
* @list: pcie device list
* @starget: starget object
* @wwid: device WWID
* @handle: device handle
* @device_info: bitfield provides detailed info about the device
* @id: target id
* @channel: target channel
* @slot: slot number
* @port_num: port number
* @responding: used in _scsih_pcie_device_mark_responding
* @fast_path: fast path feature enable bit
* @nvme_mdts: MaximumDataTransferSize from PCIe Device Page 2 for NVMe device only
* @enclosure_handle: enclosure handle
* @enclosure_logical_id: enclosure logical identifier
* @enclosure_level: The level of device's enclosure from the controller
* @connector_name: ASCII value of the Connector's name
* @serial_number: pointer of serial number string allocated runtime
* @access_status: Device's Access Status
* @refcount: reference count for deletion
*/
struct _pcie_device {
	struct list_head list;
	struct scsi_target *starget;
	u64	wwid;
	u16	handle;
	u32	device_info;
	int	id;
	int	channel;
	u16	slot;
	u8	port_num;
	u8	responding;
	u8	fast_path;
	u32	nvme_mdts;
	u16	enclosure_handle;
	u64	enclosure_logical_id;
	u8	enclosure_level;
	u8	connector_name[5];
	u8	*serial_number;
	u8	reset_timeout;
	u8	access_status;
	u16	shutdown_latency;
	struct kref refcount;
};

/**
 * pcie_device_get - Increment the pcie device reference count
 *
 * @p: pcie_device object
 *
 * When ever this function called it will increment the
 * reference count of the pcie device for which this function called.
 *
 */
static inline void pcie_device_get(struct _pcie_device *p)
{
	kref_get(&p->refcount);
}

/**
 * pcie_device_free - Release the pcie device object
 * @r - kref object
 *
 * Free's the pcie device object. It will be called when reference count
 * reaches to zero.
 */
static inline void pcie_device_free(struct kref *r)
{
	kfree(container_of(r, struct _pcie_device, refcount));
}

/**
 * pcie_device_put - Decrement the pcie device reference count
 *
 * @p: pcie_device object
 *
 * When ever this function called it will decrement the
 * reference count of the pcie device for which this function called.
 *
 * When refernce count reaches to Zero, this will call pcie_device_free to the
 * pcie_device object.
 */
static inline void pcie_device_put(struct _pcie_device *p)
{
	kref_put(&p->refcount, pcie_device_free);
}



/**
 * struct _raid_device - raid volume link list
 * @list: sas device list
 * @starget: starget object
 * @sdev: scsi device struct (volumes are single lun)
 * @wwid: unique identifier for the volume
 * @handle: device handle
 * @id: target id
 * @channel: target channel
 * @volume_type: the raid level
 * @device_info: bitfield provides detailed info about the hidden components
 * @num_pds: number of hidden raid components
 * @responding: used in _scsih_raid_device_mark_responding
 * @percent_complete: resync percent complete
 * @direct_io_enabled: Whether direct io to PDs are allowed or not
 * @stripe_exponent: X where 2powX is the stripe sz in blocks
 * @block_exponent: X where 2powX is the block sz in bytes
 * @max_lba: Maximum number of LBA in the volume
 * @stripe_sz: Stripe Size of the volume
 * @device_info: Device info of the volume member disk
 * @pd_handle: Array of handles of the physical drives for direct I/O in le16
 */
#define MPT_MAX_WARPDRIVE_PDS		8
struct _raid_device {
	struct list_head list;
	struct scsi_target *starget;
	struct scsi_device *sdev;
	u64	wwid;
	u16	handle;
	u16	block_sz;
	int	id;
	int	channel;
	u8	volume_type;
	u8	num_pds;
	u8	responding;
	u8	percent_complete;
	u8	direct_io_enabled;
	u8	stripe_exponent;
	u8	block_exponent;
	u64	max_lba;
	u32	stripe_sz;
	u32	device_info;
	u16	pd_handle[MPT_MAX_WARPDRIVE_PDS];
};

/**
 * struct _boot_device - boot device info
 *
 * @channel: sas, raid, or pcie channel
 * @device: holds pointer for struct _sas_device, struct _raid_device or
 *     struct _pcie_device
 */
struct _boot_device {
	int channel;
	void *device;
};

/**
 * struct _sas_port - wide/narrow sas port information
 * @port_list: list of ports belonging to expander
 * @num_phys: number of phys belonging to this port
 * @hba_port: hba port entry
 * @remote_identify: attached device identification
 * @rphy: sas transport rphy object
 * @port: sas transport wide/narrow port object
 * @phy_list: _sas_phy list objects belonging to this port
 */
struct _sas_port {
	struct list_head port_list;
	u8	num_phys;
	struct hba_port *hba_port;
	struct sas_identify remote_identify;
	struct sas_rphy *rphy;
#if defined(MPT_WIDE_PORT_API)
	struct sas_port *port;
#endif
	struct list_head phy_list;
};

/**
 * struct _sas_phy - phy information
 * @port_siblings: list of phys belonging to a port
 * @identify: phy identification
 * @remote_identify: attached device identification
 * @phy: sas transport phy object
 * @phy_id: unique phy id
 * @handle: device handle for this phy
 * @attached_handle: device handle for attached device
 * @phy_belongs_to_port: port has been created for this phy
 * @hba_vphy: flag to identify HBA vSES device phy
 * @port: hba port entry
 */
struct _sas_phy {
	struct list_head port_siblings;
	struct sas_identify identify;
	struct sas_identify remote_identify;
	struct sas_phy *phy;
	u8	phy_id;
	u16	handle;
	u16	attached_handle;
	u8	phy_belongs_to_port;
	u8	hba_vphy;
	struct hba_port *port;
};

/**
 * struct _sas_node - sas_host/expander information
 * @list: list of expanders
 * @parent_dev: parent device class
 * @num_phys: number phys belonging to this sas_host/expander
 * @sas_address: sas address of this sas_host/expander
 * @handle: handle for this sas_host/expander
 * @sas_address_parent: sas address of parent expander or sas host
 * @enclosure_handle: handle for this a member of an enclosure
 * @device_info: bitwise defining capabilities of this sas_host/expander
 * @responding: used in _scsih_expander_device_mark_responding
 * @port: hba port entry
 * @phy: a list of phys that make up this sas_host/expander
 * @sas_port_list: list of ports attached to this sas_host/expander
 * @rphy: sas_rphy object of this expander
 */
struct _sas_node {
	struct list_head list;
	struct device *parent_dev;
	u8	num_phys;
	u64	sas_address;
	u16	handle;
	u64	sas_address_parent;
	u16	enclosure_handle;
	u64	enclosure_logical_id;
	u8	responding;
	struct hba_port *port;
	struct	_sas_phy *phy;
	struct list_head sas_port_list;
	struct sas_rphy *rphy;
};


/**
 * struct _enclosure_node - enclosure information
 * @list: list of enclosures
 * @pg0: enclosure pg0;
 */
struct _enclosure_node {
	struct list_head list;
	Mpi2SasEnclosurePage0_t pg0;
};

/**
 * enum reset_type - reset state
 * @FORCE_BIG_HAMMER: issue diagnostic reset
 * @SOFT_RESET: issue message_unit_reset, if fails to to big hammer
 */
enum reset_type {
	FORCE_BIG_HAMMER,
	SOFT_RESET,
};

/*
 * struct pcie_sg_list - PCIe SGL buffer (contiguous per I/O)
 * @pcie_sgl: PCIe native SGL for NVMe devices
 * @pcie_sgl_dma: physical address
 */
struct pcie_sg_list {
	void            *pcie_sgl;
	dma_addr_t      pcie_sgl_dma;
};

/**
 * struct chain_tracker - firmware chain tracker
 * @chain_buffer: chain buffer
 * @chain_buffer_dma: physical address
 * @tracker_list: list of free request (ioc->free_chain_list)
 */
struct chain_tracker {
	void *chain_buffer;
	dma_addr_t chain_buffer_dma;
};

struct chain_lookup {
	struct chain_tracker *chains_per_smid;
	atomic_t	chain_offset;
};

/**
 * struct scsiio_tracker - scsi mf request tracker
 * @smid: system message id
 * @scmd: scsi request pointer
 * @cb_idx: callback index
 * @direct_io: To indicate whether I/O is direct (WARPDRIVE)
 * @pcie_sg_list: PCIe native SGL in contiguous memory
 * @chain_list: list of chains for this I/O
 * @tracker_list: list of free request (ioc->free_list)
 * @msix_io: IO's msix
 */
struct scsiio_tracker {
	u16	smid;
	struct scsi_cmnd *scmd;
	u8	cb_idx;
	u8	direct_io;
	struct list_head chain_list;
	u16     msix_io;
};

/**
 * struct request_tracker - firmware request tracker
 * @smid: system message id
 * @cb_idx: callback index
 * @tracker_list: list of free request (ioc->free_list)
 */
struct request_tracker {
	u16	smid;
	u8	cb_idx;
	struct list_head tracker_list;
};

/**
 * struct _tr_list - target reset list
 * @handle: device handle
 * @state: state machine
 */
struct _tr_list {
	struct list_head list;
	u16	handle;
	u16	state;
};

/**
 * struct _sc_list - delayed SAS_IO_UNIT_CONTROL message list
 * @handle: device handle
 */
struct _sc_list {
	struct list_head list;
	u16     handle;
};

/**
 * struct _event_ack_list - delayed event acknowledgement list
 * @Event: Event ID
 * @EventContext: used to track the event uniquely
 */
struct _event_ack_list {
	struct list_head list;
	U16     Event;
	U32     EventContext;
};

/**
 * struct adapter_reply_queue - the reply queue struct
 * @ioc: per adapter object
 * @msix_index: msix index into vector table
 * @vector: irq vector
 * @reply_post_host_index: head index in the pool where FW completes IO
 * @reply_post_free: reply post base virt address
 * @name: the name registered to request_irq()
 * @busy: isr is actively processing replies on another cpu
 * @os_irq: irq number
 * @irqpoll: irq_poll object
 * @irq_poll_scheduled: Tells whether irq poll is scheduled or not
 * @list: this list
*/
struct adapter_reply_queue {
	struct MPT3SAS_ADAPTER	*ioc;
	u8			msix_index;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0))
	unsigned int		vector;
#endif
	u32			reply_post_host_index;
	Mpi2ReplyDescriptorsUnion_t *reply_post_free;
	char			name[MPT_NAME_LENGTH];
	atomic_t		busy;
#if ((defined(RHEL_MAJOR) && (RHEL_MAJOR == 6)) || ((LINUX_VERSION_CODE >= \
     KERNEL_VERSION(2,6,36))))
	cpumask_var_t		affinity_hint;
#endif
#if defined(MPT3SAS_ENABLE_IRQ_POLL)
	u32			os_irq;
	struct irq_poll         irqpoll;
	bool 			irq_poll_scheduled;
	bool			irq_line_enable;
#endif
	struct list_head	list;
};

/* IOC Facts and Port Facts converted from little endian to cpu */
union mpi3_version_union {
	MPI2_VERSION_STRUCT		Struct;
	u32				Word;
};


typedef void (*MPT_ADD_SGE)(void *paddr, u32 flags_length, dma_addr_t dma_addr);

/* SAS3.0 support */
typedef int (*MPT_BUILD_SG_SCMD)(struct MPT3SAS_ADAPTER *ioc,
	struct scsi_cmnd *scmd, u16 smid, struct _pcie_device *pcie_device);
typedef void (*MPT_BUILD_SG)(struct MPT3SAS_ADAPTER *ioc, void *psge,
	dma_addr_t data_out_dma, size_t data_out_sz, dma_addr_t data_in_dma,
	size_t data_in_sz);
typedef void (*MPT_BUILD_ZERO_LEN_SGE)(struct MPT3SAS_ADAPTER *ioc,
	void *paddr);

/* SAS3.5 support */
typedef void (*NVME_BUILD_PRP)(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	Mpi26NVMeEncapsulatedRequest_t *nvme_encap_request,
	dma_addr_t data_out_dma, size_t data_out_sz, dma_addr_t data_in_dma,
	size_t data_in_sz);

/* To support atomic and non atomic descriptors*/
typedef void (*PUT_SMID_IO_FP_HIP_TA) (struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u16 funcdep);
typedef void (*PUT_SMID_DEF_NVME) (struct MPT3SAS_ADAPTER *ioc, u16 smid);

typedef u32 (*BASE_READ_REG) (const volatile void __iomem *addr);

 /*
 * To get high iops reply queue's msix index when high iops mode is enabled
 * else get the msix index of general reply queues.
 */
typedef u8 (*GET_MSIX_INDEX) (struct MPT3SAS_ADAPTER *ioc, struct scsi_cmnd *scmd);

struct mpt3sas_facts {
	u16			MsgVersion;
	u16			HeaderVersion;
	u8			IOCNumber;
	u8			VP_ID;
	u8			VF_ID;
	u16			IOCExceptions;
	u16			IOCStatus;
	u32			IOCLogInfo;
	u8			MaxChainDepth;
	u8			WhoInit;
	u8			NumberOfPorts;
	u8			MaxMSIxVectors;
	u16			RequestCredit;
	u16			ProductID;
	u32			IOCCapabilities;
	union mpi3_version_union	FWVersion;
	u16			IOCRequestFrameSize;
#ifndef SCSI_MPT2SAS
	u16			IOCMaxChainSegmentSize;
#else
    u16			Reserved3;
#endif
	u16			MaxInitiators;
	u16			MaxTargets;
	u16			MaxSasExpanders;
	u16			MaxEnclosures;
	u16			ProtocolFlags;
	u16			HighPriorityCredit;
	u16			MaxReplyDescriptorPostQueueDepth;
	u8			ReplyFrameSize;
	u8			MaxVolumes;
	u16			MaxDevHandle;
	u16			MaxPersistentEntries;
	u16			MinDevHandle;
	u8			CurrentHostPageSize;
};

struct mpt3sas_port_facts {
	u8			PortNumber;
	u8			VP_ID;
	u8			VF_ID;
	u8			PortType;
	u16			MaxPostedCmdBuffers;
};

struct reply_post_struct {
	Mpi2ReplyDescriptorsUnion_t 	*reply_post_free;
	dma_addr_t			reply_post_free_dma;
};

/**
 * struct virtual_phy - vSES phy structure
 * sas_address: SAS Address of vSES device
 * phy_mask: vSES device's phy number
 * flags: flags used to manage this structure
 */
struct virtual_phy {
	struct	list_head list;
	u64	sas_address;
	u32	phy_mask;
	u8	flags;
};

#define MPT_VPHY_FLAG_DIRTY_PHY	0x01

 /**
 * struct hba_port - Saves each HBA's Wide/Narrow port info
 * @port_id: port number
 * @sas_address: sas address of this wide/narrow port's attached device
 * @phy_mask: HBA PHY's belonging to this port
 * @flags: hba port flags
 * @vphys_mask : mask of vSES devices Phy number
 * @vphys_list : list containing vSES device structures
 */
struct hba_port {
	struct list_head list;
	u64	sas_address;
	u32	phy_mask;
	u8	port_id;
	u8	flags;
	u32	vphys_mask;
	struct list_head vphys_list;
};

/**
 * struct htb_rel_query - diagnostic buffer release reason
 * @unique_id - unique id associated with this buffer.
 * @buffer_rel_condition - Release condition ioctl/sysfs/reset 
 * @reserved
 * @trigger_type - Master/Event/scsi/MPI
 * @trigger_info_dwords - Data Correspondig to trigger type
 */
struct htb_rel_query
{
	u16	buffer_rel_condition;
	u16	reserved;
	u32	trigger_type;
	u32	trigger_info_dwords[2];
};

/* Buffer_rel_condition bit fields */

/* Bit 0 - Diag Buffer not Released */
#define MPT3_DIAG_BUFFER_NOT_RELEASED	(0x00)
/* Bit 0 - Diag Buffer Released */
#define MPT3_DIAG_BUFFER_RELEASED	(0x01)

/*
 * Bit 1 - Diag Buffer Released by IOCTL,
 * This bit is valid only if Bit 0 is one
 */
#define MPT3_DIAG_BUFFER_REL_IOCTL	(0x02 | MPT3_DIAG_BUFFER_RELEASED)

/*
 * Bit 2 - Diag Buffer Released by Trigger,
 * This bit is valid only if Bit 0 is one
 */
#define MPT3_DIAG_BUFFER_REL_TRIGGER	(0x04 | MPT3_DIAG_BUFFER_RELEASED)

/*
 * Bit 3 - Diag Buffer Released by SysFs,
 * This bit is valid only if Bit 0 is one
 */
#define MPT3_DIAG_BUFFER_REL_SYSFS	(0x08 | MPT3_DIAG_BUFFER_RELEASED)

/* DIAG RESET Master trigger flags */
#define MPT_DIAG_RESET_ISSUED_BY_DRIVER 0x00000000
#define MPT_DIAG_RESET_ISSUED_BY_USER	0x00000001

/* hba port flags */
#define HBA_PORT_FLAG_DIRTY_PORT	0x01
#define HBA_PORT_FLAG_NEW_PORT		0x02

#define MULTIPATH_DISABLED_PORT_ID	0xFF

typedef void (*MPT3SAS_FLUSH_RUNNING_CMDS)(struct MPT3SAS_ADAPTER *ioc);
/**
 * struct MPT3SAS_ADAPTER - per adapter struct
 * @list: ioc_list
 * @shost: shost object
 * @id: unique adapter id
 * @cpu_count: number online cpus
 * @name: generic ioc string
 * @tmp_string: tmp string used for logging
 * @pdev: pci pdev object
 * @pio_chip: physical io register space
 * @chip: memory mapped register space
 * @chip_phys: physical addrss prior to mapping
 * @logging_level: see mpt3sas_debug.h
 * @fwfault_debug: debuging FW timeouts
 * @ir_firmware: IR firmware present
 * @bars: bitmask of BAR's that must be configured
 * @mask_interrupts: ignore interrupt
 * @pci_access_mutex: Mutex to synchronize ioctl,sysfs show path and
 * 						 pci resource handling
 * @fault_reset_work_q_name: fw fault work queue
 * @fault_reset_work_q: ""
 * @fault_reset_work: ""
 * @firmware_event_name: fw event work queue
 * @firmware_event_thread: ""
 * @fw_event_lock:
 * @fw_event_list: list of fw events
 * @current_evet: current processing firmware event
 * @fw_event_cleanup: set to one while cleaning up the fw events
 * @aen_event_read_flag: event log was read
 * @broadcast_aen_busy: broadcast aen waiting to be serviced
 * @shost_recovery: host reset in progress
 * @ioc_reset_in_progress_lock:
 * @ioc_link_reset_in_progress: phy/hard reset in progress
 * @ignore_loginfos: ignore loginfos during task managment
 * @remove_host: flag for when driver unloads, to avoid sending dev resets
 * @pci_error_recovery: flag to prevent ioc access until slot reset completes
 * @wait_for_discovery_to_complete: flag set at driver load time when
 *                                               waiting on reporting devices
 * @is_driver_loading: flag set at driver load time
 * @port_enable_failed: flag set when port enable has failed
 * @start_scan: flag set from scan_start callback, cleared from _mpt3sas_fw_work
 * @start_scan_failed: means port enable failed, return's the ioc_status
 * @adapter_over_temp: adapter reached temperature threshold 3
 * @msix_enable: flag indicating msix is enabled
 * @msix_table: virt address to the msix table
 * @cpu_msix_table: table for mapping cpus to msix index
 * @cpu_msix_table_sz: table size
 * @multipath_on_hba: flag to determine multipath on hba is enabled or not
 * @total_io_cnt: Gives total IO count, used to load balance the interrupts
 * @high_iops_outstanding: used to load balance the interrupts within high iops reply queues
 * @high_iops_queues: high iops reply queues count
 * @drv_internal_flags: Bit map internal to driver
 * @drv_support_bitmap: driver's supported feature bit map
 * @msix_load_balance: Enables load balancing of interrupts across the multiple MSIXs
 * @thresh_hold: Max number of reply descriptors processed before updating Host Index
 * @schedule_dead_ioc_flush_running_cmds: callback to flush pending commands
 * @timestamp_update_count: Counter to fire timeSync command
 * time_sync_interval: Time sync interval read from man page 11
 * @scsi_io_cb_idx: shost generated commands
 * @tm_cb_idx: task management commands
 * @scsih_cb_idx: scsih internal commands
 * @ctl_cb_idx: ctl internal commands
 * @ctl_tm_cb_idx: ctl task management commands
 * @base_cb_idx: base internal commands
 * @config_cb_idx: base internal commands
 * @tm_tr_cb_idx : device removal target reset handshake
 * @tm_tr_volume_cb_idx : volume removal target reset
 * @tm_tr_internal_cb_idx : internal task managemnet commands queue
 * @base_cmds:
 * @transport_cmds:
 * @scsih_cmds:
 * @tm_cmds:
 * @ctl_cmds:
 * @config_cmds:
 * @scsih_q_intenal_cmds: base internal queue commands list
 * @scsih_q_internal_lock: internal queue commands lock
 * @base_add_sg_single: handler for either 32/64 bit sgl's
 * @event_type: bits indicating which events to log
 * @event_context: unique id for each logged event
 * @event_log: event log pointer
 * @event_masks: events that are masked
 * @facts: static facts data
 * @pfacts: static port facts data
 * @manu_pg0: static manufacturing page 0
 * @manu_pg10: static manufacturing page 10
 * @manu_pg11: static manufacturing page 11
 * @bios_pg2: static bios page 2
 * @bios_pg3: static bios page 3
 * @ioc_pg8: static ioc page 8
 * @iounit_pg0: static iounit page 0
 * @iounit_pg1: static iounit page 1
 * @iounit_pg8: static iounit page 8
 * @sas_hba: sas host object
 * @sas_expander_list: expander object list
 * @enclosure_list: enclosure object list
 * @sas_node_lock:
 * @sas_device_list: sas device object list
 * @sas_device_init_list: sas device object list (used only at init time)
 * @sas_device_lock:
 * @pcie_device_list: pcie device object list
 * @pcie_device_init_list: pcie device object list (used only at init time)
 * @pcie_device_lock:
 * @io_missing_delay: time for IO completed by fw when PDR enabled
 * @device_missing_delay: time for device missing by fw when PDR enabled
 * @sas_id : used for setting volume target IDs
 * @blocking_handles: bitmask used to identify which devices need blocking
 * @pd_handles : bitmask for PD handles
 * @pd_handles_sz : size of pd_handle bitmask
 * @config_page_sz: config page size
 * @config_page: reserve memory for config page payload
 * @config_page_dma:
 * @hba_queue_depth: hba request queue depth
 * @sge_size: sg element size for either 32/64 bit
 * @scsiio_depth: SCSI_IO queue depth
 * @request_sz: per request frame size
 * @request: pool of request frames
 * @request_dma:
 * @request_dma_sz:
 * @scsi_lookup: firmware request tracker list
 * @scsi_lookup_lock:
 * @free_list: free list of request
 * @pending_io_count:
 * @reset_wq:
 * @pending_tm_count: pending task mangement request
 * @terminated_tm_count: terminated request
 * @pending_tm_wq: wait queue
 * @out_of_frames
 * @no_frames_tm_wq
 * @chain: pool of chains
 * @chain_dma:
 * @max_sges_in_main_message: number sg elements in main message
 * @max_sges_in_chain_message: number sg elements per chain
 * @chains_needed_per_io: max chains per io
 * @chain_segment_sz: givesthe max number of SGEs accomodate on single
 * 		      chain buffer
 * @chains_per_prp_buffer: number of chain segments can fit in a PRP buffer
 * @hi_priority_smid:
 * @hi_priority:
 * @hi_priority_dma:
 * @hi_priority_depth:
 * @hpr_lookup:
 * @hpr_free_list:
 * @internal_smid:
 * @internal:
 * @internal_dma:
 * @internal_depth:
 * @internal_lookup:
 * @internal_free_list:
 * @sense: pool of sense
 * @sense_dma:
 * @sense_dma_pool:
 * @reply_depth: hba reply queue depth:
 * @reply_sz: per reply frame size:
 * @reply: pool of replys:
 * @reply_dma:
 * @reply_dma_pool:
 * @reply_free_queue_depth: reply free depth
 * @reply_free: pool for reply free queue (32 bit addr)
 * @reply_free_dma:
 * @reply_free_dma_pool:
 * @reply_post_free_dma_pool_mod_rdpq_set:
 * @reply_post_free_dma_pool_mod_rdpq_set_align:
 * @reply_free_host_index: tail index in pool to insert free replys
 * @reply_post_queue_depth: reply post queue depth
 * @reply_post_struct: struct for reply_post_free physical & virt address
 * @rdpq_array_capable: FW supports multiple reply queue addresses in ioc_init
 * @rdpq_array_enable: rdpq_array support is enabled in the driver
 * @rdpq_array_enable_assigned: this ensures that rdpq_array_enable flag
 *				is assigned only ones
 * @reply_queue_count: number of reply queue's
 * @reply_queue_list: link list contaning the reply queue info
 * @reply_post_host_index: head index in the pool where FW completes IO
 * @combined_reply_queue: non combined reply queue support
 * @smp_affinity_enable: sets the smp affinity for enabled IRQs
 * @replyPostRegisterIndex: index of next position in Reply Desc Post Queue
 * @delayed_tr_list: target reset link list
 * @delayed_tr_volume_list: volume target reset link list
 * @delayed_internal_tm_list: internal tmf link list
 * @temp_sensors_count: flag to carry the number of temperature sensors
 * @port_table_list: list containing HBA's wide/narrow port's info
 */
struct MPT3SAS_ADAPTER {
	struct list_head list;
	struct Scsi_Host *shost;
	u8		id;
	u8		IOCNumber;
	int		cpu_count;
	char		name[MPT_NAME_LENGTH];
	char		driver_name[MPT_DRIVER_NAME_LENGTH];
	char		tmp_string[MPT_STRING_LENGTH];
	struct pci_dev	*pdev;
	Mpi2SystemInterfaceRegs_t __iomem *chip;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18))
#if defined(CPQ_CIM)
	resource_size_t pio_chip;
#endif
	phys_addr_t	chip_phys;
#else
#if defined(CPQ_CIM)
	u64		 pio_chip;
#endif
	phys_addr_t	chip_phys;
#endif
	int		logging_level;
	int		fwfault_debug;
	u8		ir_firmware;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25))
	int		bars;
#endif
	u8		mask_interrupts;

	struct mutex pci_access_mutex;

	/* fw fault handler */
	char		fault_reset_work_q_name[20];
	char		hba_hot_unplug_work_q_name[20];
	struct workqueue_struct *fault_reset_work_q;
	struct workqueue_struct *hba_hot_unplug_work_q;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	struct delayed_work fault_reset_work;
	struct delayed_work hba_hot_unplug_work;
#else
	struct work_struct fault_reset_work;
	struct work_struct hba_hot_unplug_work;
#endif

#ifndef SCSI_MPT2SAS
	/* SATA SMART pooling handler */
	char		smart_poll_work_q_name[20];
	struct workqueue_struct *smart_poll_work_q;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	struct delayed_work smart_poll_work;
#else
	struct work_struct smart_poll_work;
#endif
	u8 		adapter_over_temp;
#endif /*End of not defined SCSI_MPT2SAS */

	/* fw event handler */
	char		firmware_event_name[20];
	struct workqueue_struct	*firmware_event_thread;
	spinlock_t	fw_event_lock;
	struct list_head fw_event_list;
	struct fw_event_work *current_event;
	u8		fw_events_cleanup;

	 /* misc flags */
	int		aen_event_read_flag;
	u8		broadcast_aen_busy;
	u16		broadcast_aen_pending;
	u8		shost_recovery;
	u16		hba_mpi_version_belonged;
	u8		got_task_abort_from_ioctl;
	u8		got_task_abort_from_sysfs;

	struct mutex	reset_in_progress_mutex;
	spinlock_t	ioc_reset_in_progress_lock;
	spinlock_t	hba_hot_unplug_lock;
	u8		ioc_link_reset_in_progress;
	int		ioc_reset_status;

	u8		ignore_loginfos;
	u8		remove_host;
	u8		pci_error_recovery;
	u8		wait_for_discovery_to_complete;
	u8		is_driver_loading;
	u8		port_enable_failed;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
	u8		start_scan;
	u16		start_scan_failed;
#endif
	u8		msix_enable;
	u8		*cpu_msix_table;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18))
	resource_size_t	**reply_post_host_index;
#else
	u64		**reply_post_host_index;
#endif
	u16		cpu_msix_table_sz;
	u32		ioc_reset_count;
	MPT3SAS_FLUSH_RUNNING_CMDS schedule_dead_ioc_flush_running_cmds;
	u32             non_operational_loop;
	u8		ioc_coredump_loop;
	u32		timestamp_update_count;
	u32		time_sync_interval;
	u8		multipath_on_hba;
	atomic64_t	total_io_cnt;
	atomic64_t	high_iops_outstanding;
	bool 		msix_load_balance;
	u16		thresh_hold;
	u8		high_iops_queues;
	u32		drv_internal_flags;
	u32		drv_support_bitmap;
	u32		dma_mask;
	bool		enable_sdev_max_qd;
	bool		use_32bit_dma;

	/* internal commands, callback index */
	u8		scsi_io_cb_idx;
	u8		tm_cb_idx;
	u8		transport_cb_idx;
	u8		scsih_cb_idx;
	u8		ctl_cb_idx;
	u8		ctl_tm_cb_idx;
	u8		ctl_diag_cb_idx;
	u8		base_cb_idx;
	u8		port_enable_cb_idx;
	u8		config_cb_idx;
	u8		tm_tr_cb_idx;
	u8		tm_tr_volume_cb_idx;
	u8		tm_tr_internal_cb_idx;
	u8		tm_sas_control_cb_idx;
	struct _internal_cmd base_cmds;
	struct _internal_cmd port_enable_cmds;
	struct _internal_cmd transport_cmds;
	struct _internal_cmd scsih_cmds;
	struct _internal_cmd tm_cmds;
	struct _internal_cmd ctl_cmds;
	struct _internal_cmd ctl_diag_cmds;
	struct _internal_cmd config_cmds;

	struct list_head scsih_q_intenal_cmds;
	spinlock_t	scsih_q_internal_lock;

	MPT_ADD_SGE	base_add_sg_single;

	/* function ptr for either IEEE or MPI sg elements */
	MPT_BUILD_SG_SCMD build_sg_scmd;
	MPT_BUILD_SG    build_sg;
	MPT_BUILD_ZERO_LEN_SGE build_zero_len_sge;
	u16             sge_size_ieee;

	/* function ptr for MPI sg elements only */
	MPT_BUILD_SG    build_sg_mpi;
	MPT_BUILD_ZERO_LEN_SGE build_zero_len_sge_mpi;

	/* function ptr for NVMe PRP elements only */
	NVME_BUILD_PRP  build_nvme_prp;

	/* event log */
	u32		event_type[MPI2_EVENT_NOTIFY_EVENTMASK_WORDS];
	u32		event_context;
	void		*event_log;
	u32		event_masks[MPI2_EVENT_NOTIFY_EVENTMASK_WORDS];

	u8		disable_eedp_support;
	u8		tm_custom_handling;
	u8		nvme_abort_timeout;
	u16		max_shutdown_latency;

	/* static config pages */
	struct mpt3sas_facts facts;
	struct mpt3sas_facts prev_fw_facts;
	struct mpt3sas_port_facts *pfacts;
	Mpi2ManufacturingPage0_t manu_pg0;
	struct Mpi2ManufacturingPage10_t manu_pg10;
	struct Mpi2ManufacturingPage11_t manu_pg11;
	Mpi2BiosPage2_t	bios_pg2;
	Mpi2BiosPage3_t	bios_pg3;
	Mpi2IOCPage8_t ioc_pg8;
	Mpi2IOUnitPage0_t iounit_pg0;
	Mpi2IOUnitPage1_t iounit_pg1;
	Mpi2IOUnitPage8_t iounit_pg8;
#if defined(CPQ_CIM)
	Mpi2IOCPage1_t ioc_pg1;
#endif
	Mpi2IOCPage1_t ioc_pg1_copy;

	struct _boot_device req_boot_device;
	struct _boot_device req_alt_boot_device;
	struct _boot_device current_boot_device;

	/* sas hba, expander, and device list */
	struct _sas_node sas_hba;
	struct list_head sas_expander_list;
	struct list_head enclosure_list;
	spinlock_t	sas_node_lock;
	struct list_head sas_device_list;
	struct list_head sas_device_init_list;
	spinlock_t	sas_device_lock;
	struct list_head pcie_device_list;
	struct list_head pcie_device_init_list;
	spinlock_t      pcie_device_lock;

	struct list_head raid_device_list;
	spinlock_t	raid_device_lock;
	u8		io_missing_delay;
	u16		device_missing_delay;
	int		sas_id;
	int		pcie_target_id;

	void		*blocking_handles;
	void		*pd_handles;
	u16		pd_handles_sz;

	void		*pend_os_device_add;
	u16		pend_os_device_add_sz;

	/* config page */
	u16		config_page_sz;
	void		*config_page;
	dma_addr_t	config_page_dma;
	void 		*config_vaddr;

	/* scsiio request */
	u16		hba_queue_depth;
	u16		sge_size;
	u16		scsiio_depth;
	u16		request_sz;
	u8		*request;
	dma_addr_t	request_dma;
	u32		request_dma_sz;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0))
	struct scsiio_tracker *scsi_lookup;
	ulong		scsi_lookup_pages;
#endif
	struct pcie_sg_list *pcie_sg_lookup;
	spinlock_t	scsi_lookup_lock;
	int		pending_io_count;
	wait_queue_head_t reset_wq;
	int		pending_tm_count;
	u32		terminated_tm_count;
	wait_queue_head_t pending_tm_wq;
	u8		  out_of_frames;
	wait_queue_head_t no_frames_tm_wq;

	/* PCIe SGL */
	struct dma_pool *pcie_sgl_dma_pool;

	/* Host Page Size */
	u32		page_size;

	/* chain */
	struct chain_lookup *chain_lookup;
	struct list_head free_chain_list;
	struct dma_pool *chain_dma_pool;
	u16		max_sges_in_main_message;
	u16		max_sges_in_chain_message;
	u16		chains_needed_per_io;
	u16		chain_segment_sz;
	u16		chains_per_prp_buffer;

	/* hi-priority queue */
	u16		hi_priority_smid;
	u8		*hi_priority;
	dma_addr_t	hi_priority_dma;
	u16		hi_priority_depth;
	struct request_tracker *hpr_lookup;
	struct list_head hpr_free_list;

	/* internal queue */
	u16		internal_smid;
	u8		*internal;
	dma_addr_t	internal_dma;
	u16		internal_depth;
	struct request_tracker *internal_lookup;
	struct list_head internal_free_list;

	/* sense */
	u8		*sense;
	dma_addr_t	sense_dma;
	struct dma_pool *sense_dma_pool;

	/* reply */
	u16		reply_sz;
	u8		*reply;
	dma_addr_t	reply_dma;
	u32		reply_dma_max_address;
	u32		reply_dma_min_address;
	struct dma_pool *reply_dma_pool;

	/* reply free queue */
	u16		reply_free_queue_depth;
	__le32		*reply_free;
	dma_addr_t	reply_free_dma;
	struct dma_pool *reply_free_dma_pool;
	u32		reply_free_host_index;

	/* reply post queue */
	u16		reply_post_queue_depth;
	struct reply_post_struct *reply_post;
	struct dma_pool *reply_post_free_dma_pool;
	struct dma_pool *reply_post_free_array_dma_pool;
	Mpi2IOCInitRDPQArrayEntry *reply_post_free_array;
	dma_addr_t reply_post_free_array_dma;
	u8		reply_queue_count;
	struct list_head reply_queue_list;
	u8		rdpq_array_capable;
	u8              rdpq_array_enable;
	u8              rdpq_array_enable_assigned;

	u8 		combined_reply_queue;
	u8 		nc_reply_index_count;
	u8		smp_affinity_enable;
	/* reply post register index */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18))
	resource_size_t	**replyPostRegisterIndex;
#else
	u64		**replyPostRegisterIndex;
#endif

	struct list_head delayed_tr_list;
	struct list_head delayed_tr_volume_list;
	struct list_head delayed_internal_tm_list;
	struct list_head delayed_sc_list;
	struct list_head delayed_event_ack_list;

	/* diag buffer support */
	u8		*diag_buffer[MPI2_DIAG_BUF_TYPE_COUNT];
	u32		diag_buffer_sz[MPI2_DIAG_BUF_TYPE_COUNT];
	dma_addr_t	diag_buffer_dma[MPI2_DIAG_BUF_TYPE_COUNT];
	u8		diag_buffer_status[MPI2_DIAG_BUF_TYPE_COUNT];
	u32		unique_id[MPI2_DIAG_BUF_TYPE_COUNT];
	u32		product_specific[MPI2_DIAG_BUF_TYPE_COUNT][23];
	u32		diagnostic_flags[MPI2_DIAG_BUF_TYPE_COUNT];
	u32		ring_buffer_offset;
	u32		ring_buffer_sz;
	struct	htb_rel_query htb_rel;
	u8		reset_from_user;
#if defined(TARGET_MODE)
	char		stm_name[MPT_NAME_LENGTH];
	u8		stm_io_cb_idx; /* normal io */
	u8		stm_tm_cb_idx; /* task managment */
	u8		stm_tm_imm_cb_idx; /* immediate TM request */
	u8		stm_post_cb_idx; /* post all buffers */
	struct _internal_cmd stm_tm_cmds; /* TM requests */
	struct _internal_cmd stm_post_cmds; /* post cmd buffer request */
	void		*priv;
#endif
	u8		is_warpdrive;
	u8		is_mcpu_endpoint;
	u8		hide_ir_msg;
	u8		warpdrive_msg;
	u8		mfg_pg10_hide_flag;
	u8		hide_drives;

	u8		atomic_desc_capable;
	BASE_READ_REG	base_readl; 
	PUT_SMID_IO_FP_HIP_TA put_smid_scsi_io;
	PUT_SMID_IO_FP_HIP_TA put_smid_fast_path;
	PUT_SMID_IO_FP_HIP_TA put_smid_hi_priority;
#if defined(TARGET_MODE)
	PUT_SMID_IO_FP_HIP_TA put_smid_target_assist;
#endif
	PUT_SMID_DEF_NVME put_smid_default;
	PUT_SMID_DEF_NVME put_smid_nvme_encap;
	GET_MSIX_INDEX get_msix_index_for_smlio;
#ifdef MPT2SAS_WD_DDIOCOUNT
	u64		ddio_count;
	u64		ddio_err_count;
#endif
	spinlock_t	diag_trigger_lock;
	u8		diag_trigger_active;
	struct SL_WH_MASTER_TRIGGER_T diag_trigger_master;
	struct SL_WH_EVENT_TRIGGERS_T diag_trigger_event;
	struct SL_WH_SCSI_TRIGGERS_T diag_trigger_scsi;
	struct SL_WH_MPI_TRIGGERS_T diag_trigger_mpi;
	u8		supports_trigger_pages;
	void		*device_remove_in_progress;
	u16		device_remove_in_progress_sz;
	u8		*tm_tr_retry;
	u32		tm_tr_retry_sz;
	u8		temp_sensors_count;
	u8		is_gen35_ioc;
	u8		is_aero_ioc;
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_root;
	struct dentry *ioc_dump;
#endif
	struct list_head port_table_list;
};

struct mpt3sas_debugfs_buffer {
        void *buf;
        u32 len;
};

#define MPT_DRV_SUPPORT_BITMAP_MEMMOVE		0x00000001
#define MPT_DRV_SUPPORT_BITMAP_ADDNLQUERY	0x00000002

#define MPT_DRV_INTERNAL_BITMAP_BLK_MQ 0x00000001

typedef u8 (*MPT_CALLBACK)(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply);

#if defined(TARGET_MODE)
typedef  void (*STM_CALLBACK_WITH_IOC)(struct MPT3SAS_ADAPTER *ioc);
typedef  void (*STM_CALLBACK_FOR_TGT_CMD)(struct MPT3SAS_ADAPTER *ioc,
    Mpi2TargetCommandBufferReplyDescriptor_t *rpf,   u8 msix_index);
typedef  void (*STM_CALLBACK_FOR_TGT_ASSIST)(struct MPT3SAS_ADAPTER *ioc,
    Mpi2TargetAssistSuccessReplyDescriptor_t *rpf);
typedef  u8 (*STM_CALLBACK_FOR_SMID)(struct MPT3SAS_ADAPTER *ioc,
    u8 msix_index, u32 reply);
typedef  void (*STM_CALLBACK_FOR_RESET)(struct MPT3SAS_ADAPTER *ioc,
    int reset_phase);

struct STM_CALLBACK {
	STM_CALLBACK_WITH_IOC  watchdog;
	STM_CALLBACK_FOR_TGT_CMD target_command;
	STM_CALLBACK_FOR_TGT_ASSIST target_assist;
	STM_CALLBACK_FOR_SMID smid_handler;
	STM_CALLBACK_FOR_RESET reset_handler;
};
#endif

/* base shared API */
extern struct list_head mpt3sas_ioc_list;
extern spinlock_t gioc_lock;
void mpt3sas_base_start_watchdog(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_base_stop_watchdog(struct MPT3SAS_ADAPTER *ioc);
#ifndef SCSI_MPT2SAS
void mpt3sas_base_start_smart_polling(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_base_stop_smart_polling(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_scsih_sata_smart_polling(struct MPT3SAS_ADAPTER *ioc);
#endif

int mpt3sas_base_attach(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_base_detach(struct MPT3SAS_ADAPTER *ioc);
int mpt3sas_base_map_resources(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_base_free_resources(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_free_enclosure_list(struct MPT3SAS_ADAPTER *ioc);
int mpt3sas_base_hard_reset_handler(struct MPT3SAS_ADAPTER *ioc, enum reset_type type);

void *mpt3sas_base_get_msg_frame(struct MPT3SAS_ADAPTER *ioc, u16 smid);
void *mpt3sas_base_get_sense_buffer(struct MPT3SAS_ADAPTER *ioc, u16 smid);
__le32 mpt3sas_base_get_sense_buffer_dma(struct MPT3SAS_ADAPTER *ioc,
	u16 smid);
__le64 mpt3sas_base_get_sense_buffer_dma_64(struct MPT3SAS_ADAPTER *ioc,
	u16 smid);
void *mpt3sas_base_get_pcie_sgl(struct MPT3SAS_ADAPTER *ioc, u16 smid);
dma_addr_t mpt3sas_base_get_pcie_sgl_dma(struct MPT3SAS_ADAPTER *ioc, u16 smid);
void mpt3sas_base_sync_reply_irqs(struct MPT3SAS_ADAPTER *ioc, u8 poll);

/* hi-priority queue */
u16 mpt3sas_base_get_smid_hpr(struct MPT3SAS_ADAPTER *ioc, u8 cb_idx);
u16 mpt3sas_base_get_smid_scsiio(struct MPT3SAS_ADAPTER *ioc, u8 cb_idx,
	struct scsi_cmnd *scmd);

u16 mpt3sas_base_get_smid(struct MPT3SAS_ADAPTER *ioc, u8 cb_idx);
void mpt3sas_base_free_smid(struct MPT3SAS_ADAPTER *ioc, u16 smid);
void mpt3sas_base_initialize_callback_handler(void);
u8 mpt3sas_base_register_callback_handler(MPT_CALLBACK cb_func);
void mpt3sas_base_release_callback_handler(u8 cb_idx);

u8 mpt3sas_base_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply);
u8 mpt3sas_port_enable_done(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u8 msix_index, u32 reply);
void *mpt3sas_base_get_reply_virt_addr(struct MPT3SAS_ADAPTER *ioc,
	u32 phys_addr);

u32 mpt3sas_base_get_iocstate(struct MPT3SAS_ADAPTER *ioc, int cooked);
int _base_check_and_get_msix_vectors(struct pci_dev *pdev);

void mpt3sas_base_fault_info(struct MPT3SAS_ADAPTER *ioc , u16 fault_code);
#define mpt3sas_print_fault_code(ioc, fault_code) \
    printk(MPT3SAS_ERR_FMT "fault info from func: %s\n", ioc->name, __func__); \
    mpt3sas_base_fault_info (ioc, fault_code)

void mpt3sas_base_coredump_info(struct MPT3SAS_ADAPTER *ioc , u16 fault_code);
int mpt3sas_base_wait_for_coredump_completion(struct MPT3SAS_ADAPTER *ioc,
		const char *caller);
int mpt3sas_base_sas_iounit_control(struct MPT3SAS_ADAPTER *ioc,
	Mpi2SasIoUnitControlReply_t *mpi_reply,
	Mpi2SasIoUnitControlRequest_t *mpi_request);
int mpt3sas_base_scsi_enclosure_processor(struct MPT3SAS_ADAPTER *ioc,
	Mpi2SepReply_t *mpi_reply, Mpi2SepRequest_t *mpi_request);

void mpt3sas_base_validate_event_type(struct MPT3SAS_ADAPTER *ioc,
	u32 *event_type);

void mpt3sas_halt_firmware(struct MPT3SAS_ADAPTER *ioc, u8 set_fault);

void mpt3sas_base_update_missing_delay(struct MPT3SAS_ADAPTER *ioc,
	u16 device_missing_delay, u8 io_missing_delay);

struct scsiio_tracker * mpt3sas_get_st_from_smid(struct MPT3SAS_ADAPTER *ioc,
	u16 smid);
void mpt3sas_base_clear_st(struct MPT3SAS_ADAPTER *ioc,
	struct scsiio_tracker *st);
struct scsiio_tracker * mpt3sas_base_scsi_cmd_priv(struct scsi_cmnd *scmd);


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
int mpt3sas_port_enable(struct MPT3SAS_ADAPTER *ioc);
#endif
u8 mpt3sas_base_pci_device_is_unplugged(struct MPT3SAS_ADAPTER *ioc);
u8 mpt3sas_base_pci_device_is_available(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_base_free_irq(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_base_disable_msix(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_wait_for_commands_to_complete(struct MPT3SAS_ADAPTER *ioc);
u8 mpt3sas_base_check_cmd_timeout(struct MPT3SAS_ADAPTER *ioc,
	u8 status, void *mpi_request, int sz);
#define mpt3sas_check_cmd_timeout(ioc, status, mpi_request, sz, issue_reset) \
	printk(MPT3SAS_ERR_FMT "In func: %s\n", ioc->name, __func__); \
	issue_reset = mpt3sas_base_check_cmd_timeout(ioc, status, mpi_request, sz)

int mpt3sas_wait_for_ioc_to_operational(struct MPT3SAS_ADAPTER *ioc,
	int wait_count);
void mpt3sas_base_start_hba_unplug_watchdog(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_base_stop_hba_unplug_watchdog(struct MPT3SAS_ADAPTER *ioc);
int mpt3sas_base_make_ioc_ready(struct MPT3SAS_ADAPTER *ioc, enum reset_type type);
void mpt3sas_base_mask_interrupts(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_base_unmask_interrupts(struct MPT3SAS_ADAPTER *ioc);

/* scsih shared API */
extern char driver_name[MPT_NAME_LENGTH];
extern int prot_mask;
struct scsi_cmnd *mpt3sas_scsih_scsi_lookup_get(struct MPT3SAS_ADAPTER *ioc,
	u16 smid);
u8 mpt3sas_scsih_event_callback(struct MPT3SAS_ADAPTER *ioc, u8 msix_index,
	u32 reply);
void mpt3sas_scsih_reset_handler(struct MPT3SAS_ADAPTER *ioc, int reset_phase);
int mpt3sas_scsih_issue_tm(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	uint channel, uint id, uint lun, u8 type, u16 smid_task,
	u8 timeout, u8 tr_method);
int mpt3sas_scsih_issue_locked_tm(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	uint channel, uint id, uint lun, u8 type, u16 smid_task,
	u8 timeout, u8 tr_method);
void mpt3sas_scsih_set_tm_flag(struct MPT3SAS_ADAPTER *ioc, u16 handle);
void mpt3sas_scsih_clear_tm_flag(struct MPT3SAS_ADAPTER *ioc, u16 handle);
void mpt3sas_expander_remove(struct MPT3SAS_ADAPTER *ioc,
	 u64 sas_address, struct hba_port *port);
void mpt3sas_device_remove_by_sas_address(struct MPT3SAS_ADAPTER *ioc,
	u64 sas_address, struct hba_port *port);
u8 mpt3sas_check_for_pending_internal_cmds(struct MPT3SAS_ADAPTER *ioc,
	 u16 smid);
struct hba_port *
mpt3sas_get_port_by_id(struct MPT3SAS_ADAPTER *ioc, u8 port, u8 skip_dirty_flag);
struct virtual_phy *
mpt3sas_get_vphy_by_phy(struct MPT3SAS_ADAPTER *ioc, struct hba_port *port, u32 phy);

struct _sas_node *mpt3sas_scsih_expander_find_by_handle(
	struct MPT3SAS_ADAPTER *ioc, u16 handle);
struct _sas_node *mpt3sas_scsih_expander_find_by_sas_address(
	struct MPT3SAS_ADAPTER *ioc, u64 sas_address,
	struct hba_port *port);
struct _sas_device *__mpt3sas_get_sdev_by_addr_and_rphy(
	struct MPT3SAS_ADAPTER *ioc, u64 sas_address, struct sas_rphy *rphy);
struct _sas_device *mpt3sas_get_sdev_by_addr(
	struct MPT3SAS_ADAPTER *ioc, u64 sas_address,
	struct hba_port *port);
struct _sas_device *mpt3sas_get_sdev_by_handle(struct MPT3SAS_ADAPTER *ioc,
	u16 handle);
struct _pcie_device *mpt3sas_get_pdev_by_handle(struct MPT3SAS_ADAPTER *ioc,
	u16 handle);

void mpt3sas_scsih_flush_running_cmds(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_port_enable_complete(struct MPT3SAS_ADAPTER *ioc);
struct _raid_device *
mpt3sas_raid_device_find_by_handle(struct MPT3SAS_ADAPTER *ioc, u16
	handle);
void
_scsih_sas_device_remove(struct MPT3SAS_ADAPTER *ioc,
        struct _sas_device *sas_device);
void
mpt3sas_scsih_clear_outstanding_scsi_tm_commands(struct MPT3SAS_ADAPTER *ioc);
u32 base_mod64(u64 dividend, u32 divisor);
void
mpt3sas_scsih_change_queue_depth(struct scsi_device *sdev, int qdepth);

/**
 * _scsih_is_pcie_scsi_device - determines if device is an pcie scsi device
 * @device_info: bitfield providing information about the device.
 * Context: none
 *
 * Returns 1 if scsi device.
 */
static inline int
mpt3sas_scsih_is_pcie_scsi_device(u32 device_info)
{
	if ((device_info &
		MPI26_PCIE_DEVINFO_MASK_DEVICE_TYPE) == MPI26_PCIE_DEVINFO_SCSI)
		return 1;
	else
		return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0))
/* NCQ Prio Handling Check */
u8 mpt3sas_scsih_ncq_prio_supp(struct scsi_device *sdev);
#endif

/* config shared API */
u8 mpt3sas_config_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply);
int mpt3sas_config_get_number_hba_phys(struct MPT3SAS_ADAPTER *ioc,
	u8 *num_phys);
int mpt3sas_config_get_manufacturing_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2ManufacturingPage0_t *config_page);
int mpt3sas_config_get_manufacturing_pg7(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2ManufacturingPage7_t *config_page,
	u16 sz);
int mpt3sas_config_get_manufacturing_pg10(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply,
	struct Mpi2ManufacturingPage10_t *config_page);
int mpt3sas_config_get_manufacturing_pg11(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply,
	struct Mpi2ManufacturingPage11_t  *config_page);
int mpt3sas_config_set_manufacturing_pg11(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply,
	struct Mpi2ManufacturingPage11_t *config_page);
int mpt3sas_config_get_bios_pg2(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2BiosPage2_t *config_page);
int mpt3sas_config_get_bios_pg3(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2BiosPage3_t *config_page);
int mpt3sas_config_get_iounit_pg0(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2IOUnitPage0_t *config_page);
int mpt3sas_config_get_sas_device_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasDevicePage0_t *config_page,
	u32 form, u32 handle);
int mpt3sas_config_get_sas_device_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasDevicePage1_t *config_page,
	u32 form, u32 handle);
int mpt3sas_config_get_pcie_device_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi26PCIeDevicePage0_t *config_page,
	u32 form, u32 handle);
int mpt3sas_config_get_pcie_device_pg2(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi26PCIeDevicePage2_t *config_page,
	u32 form, u32 handle);
int mpt3sas_config_get_sas_iounit_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasIOUnitPage0_t *config_page,
	u16 sz);
int mpt3sas_config_get_iounit_pg1(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2IOUnitPage1_t *config_page);
int mpt3sas_config_set_iounit_pg1(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2IOUnitPage1_t *config_page);
int mpt3sas_config_get_iounit_pg8(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2IOUnitPage8_t *config_page);
int mpt3sas_config_get_sas_iounit_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasIOUnitPage1_t *config_page,
	u16 sz);
int mpt3sas_config_set_sas_iounit_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasIOUnitPage1_t *config_page,
	u16 sz);
int mpt3sas_config_get_ioc_pg1(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2IOCPage1_t *config_page);
int mpt3sas_config_set_ioc_pg1(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2IOCPage1_t *config_page);
int mpt3sas_config_get_ioc_pg8(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2IOCPage8_t *config_page);
int mpt3sas_config_get_expander_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2ExpanderPage0_t *config_page,
	u32 form, u32 handle);
int mpt3sas_config_get_expander_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2ExpanderPage1_t *config_page,
	u32 phy_number, u16 handle);
int mpt3sas_config_get_enclosure_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2SasEnclosurePage0_t *config_page,
	u32 form, u32 handle);
int mpt3sas_config_get_phy_pg0(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2SasPhyPage0_t *config_page, u32 phy_number);
int mpt3sas_config_get_phy_pg1(struct MPT3SAS_ADAPTER *ioc, Mpi2ConfigReply_t
	*mpi_reply, Mpi2SasPhyPage1_t *config_page, u32 phy_number);
int mpt3sas_config_get_raid_volume_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2RaidVolPage1_t *config_page, u32 form,
	u32 handle);
int mpt3sas_config_get_number_pds(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u8 *num_pds);
int mpt3sas_config_get_raid_volume_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2RaidVolPage0_t *config_page, u32 form,
	u32 handle, u16 sz);
int mpt3sas_config_get_phys_disk_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2RaidPhysDiskPage0_t *config_page,
	u32 form, u32 form_specific);
int mpt3sas_config_get_volume_handle(struct MPT3SAS_ADAPTER *ioc, u16 pd_handle,
	u16 *volume_handle);
int mpt3sas_config_get_volume_wwid(struct MPT3SAS_ADAPTER *ioc,
	u16 volume_handle, u64 *wwid);
#if defined(CPQ_CIM)
int mpt3sas_config_get_ioc_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi2IOCPage1_t *config_page);
#endif
int
mpt3sas_config_get_iounit_pg3(struct MPT3SAS_ADAPTER *ioc,
        Mpi2ConfigReply_t *mpi_reply, Mpi2IOUnitPage3_t *config_page, u16 sz);
int
mpt3sas_config_get_driver_trigger_pg0(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi26DriverTriggerPage0_t *config_page);
int
mpt3sas_config_get_driver_trigger_pg1(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi26DriverTriggerPage1_t *config_page);
int
mpt3sas_config_get_driver_trigger_pg2(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi26DriverTriggerPage2_t *config_page);
int
mpt3sas_config_get_driver_trigger_pg3(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi26DriverTriggerPage3_t *config_page);
int
mpt3sas_config_get_driver_trigger_pg4(struct MPT3SAS_ADAPTER *ioc,
	Mpi2ConfigReply_t *mpi_reply, Mpi26DriverTriggerPage4_t *config_page);
int
mpt3sas_config_update_driver_trigger_pg1(struct MPT3SAS_ADAPTER *ioc,
	struct SL_WH_MASTER_TRIGGER_T *master_tg, bool set);
int
mpt3sas_config_update_driver_trigger_pg2(struct MPT3SAS_ADAPTER *ioc,
	struct SL_WH_EVENT_TRIGGERS_T *event_tg, bool set);
int
mpt3sas_config_update_driver_trigger_pg3(struct MPT3SAS_ADAPTER *ioc,
	struct SL_WH_SCSI_TRIGGERS_T *scsi_tg, bool set);
int
mpt3sas_config_update_driver_trigger_pg4(struct MPT3SAS_ADAPTER *ioc,
	struct SL_WH_MPI_TRIGGERS_T *mpi_tg, bool set);

/* ctl shared API */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,25))
extern struct device_attribute *mpt3sas_host_attrs[];
#else
extern struct class_device_attribute *mpt3sas_host_attrs[];
#endif
extern struct device_attribute *mpt3sas_dev_attrs[];
void mpt3sas_ctl_init(int enumerate_hba);
void mpt3sas_ctl_exit(int enumerate_hba);
u8 mpt3sas_ctl_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply);
u8 mpt3sas_ctl_tm_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply);
u8 mpt3sas_ctl_diag_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply);
void mpt3sas_ctl_reset_handler(struct MPT3SAS_ADAPTER *ioc, int reset_phase);
u8 mpt3sas_ctl_event_callback(struct MPT3SAS_ADAPTER *ioc,
	u8 msix_index, u32 reply);
void mpt3sas_ctl_add_to_event_log(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventNotificationReply_t *mpi_reply);

void mpt3sas_enable_diag_buffer(struct MPT3SAS_ADAPTER *ioc,
	u8 bits_to_regsiter);
int mpt3sas_send_diag_release(struct MPT3SAS_ADAPTER *ioc, u8 buffer_type,
	u8 *issue_reset);
void mpt3sas_ctl_clear_outstanding_ioctls(struct MPT3SAS_ADAPTER *ioc);

int ctl_release(struct inode *inode, struct file *filep);
void ctl_init(void);
void ctl_exit(void);

#if defined(CPQ_CIM)
long
_ctl_ioctl_csmi(struct MPT3SAS_ADAPTER *ioc, unsigned int cmd, void __user *arg);
#endif

/* transport shared API */
u8 mpt3sas_transport_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply);
struct _sas_port *mpt3sas_transport_port_add(struct MPT3SAS_ADAPTER *ioc,
	u16 handle, u64 sas_address, struct hba_port *port);
void mpt3sas_transport_port_remove(struct MPT3SAS_ADAPTER *ioc, u64 sas_address,
	u64 sas_address_parent, struct hba_port *port);
int mpt3sas_transport_add_host_phy(struct MPT3SAS_ADAPTER *ioc, struct _sas_phy
	*mpt3sas_phy, Mpi2SasPhyPage0_t phy_pg0, struct device *parent_dev);
int mpt3sas_transport_add_expander_phy(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_phy *mpt3sas_phy, Mpi2ExpanderPage1_t expander_pg1,
	struct device *parent_dev);
void mpt3sas_transport_update_links(struct MPT3SAS_ADAPTER *ioc,
	u64 sas_address, u16 handle, u8 phy_number, u8 link_rate,
	struct hba_port *port);
extern struct sas_function_template mpt3sas_transport_functions;
extern struct scsi_transport_template *mpt3sas_transport_template;
void
mpt3sas_transport_del_phy_from_an_existing_port(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_node *sas_node, struct _sas_phy *mpt3sas_phy);
#if defined(MPT_WIDE_PORT_API)
void
mpt3sas_transport_add_phy_to_an_existing_port(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_node *sas_node, struct _sas_phy *mpt3sas_phy,
	u64 sas_address, struct hba_port *port);
#endif

#if defined(TARGET_MODE)
void mpt3sas_base_stm_initialize_callback_handler(void);
void mpt3sas_base_stm_release_callback_handler(void);
#if defined(STM_RING_BUFFER)
extern void sysfs_dump_kernel_thread_state(struct MPT_STM_PRIV *priv);
extern void sysfs_dump_ring_buffer(struct MPT_STM_PRIV *priv);
#endif /* STM_RING_BUFFER */
#endif

/* trigger data externs */
void mpt3sas_send_trigger_data_event(struct MPT3SAS_ADAPTER *ioc,
	struct SL_WH_TRIGGERS_EVENT_DATA_T *event_data);
void mpt3sas_process_trigger_data(struct MPT3SAS_ADAPTER *ioc,
	struct SL_WH_TRIGGERS_EVENT_DATA_T *event_data);
void mpt3sas_trigger_master(struct MPT3SAS_ADAPTER *ioc,
	u32 tigger_bitmask);
void mpt3sas_trigger_event(struct MPT3SAS_ADAPTER *ioc, u16 event,
	u16 log_entry_qualifier);
void mpt3sas_trigger_scsi(struct MPT3SAS_ADAPTER *ioc, u8 sense_key,
	u8 asc, u8 ascq);
void mpt3sas_trigger_mpi(struct MPT3SAS_ADAPTER *ioc, u16 ioc_status,
	u32 loginfo);

/* warpdrive APIs */
u8 mpt3sas_get_num_volumes(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_init_warpdrive_properties(struct MPT3SAS_ADAPTER *ioc,
       	struct _raid_device *raid_device);
void
mpt3sas_setup_direct_io(struct MPT3SAS_ADAPTER *ioc, struct scsi_cmnd *scmd,
	struct _raid_device *raid_device, Mpi25SCSIIORequest_t *mpi_request);

void _scsih_hide_unhide_sas_devices(struct MPT3SAS_ADAPTER *ioc);

void mpt3sas_setup_debugfs(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_destroy_debugfs(struct MPT3SAS_ADAPTER *ioc);
void mpt3sas_init_debugfs(void);
void mpt3sas_exit_debugfs(void);

#ifdef MPT2SAS_WD_DDIOCOUNT
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,25))
ssize_t
_ctl_ioc_ddio_count_show(struct device *cdev, struct device_attribute *attr,
        char *buf);
ssize_t
_ctl_BRM_status_show(struct device *cdev, struct device_attribute *attr,
        char *buf);
ssize_t
_ctl_ioc_ddio_err_count_show(struct device *cdev, struct device_attribute *attr,
        char *buf);
#else
ssize_t
_ctl_ioc_ddio_count_show(struct class_device *cdev, char *buf);
ssize_t
_ctl_ioc_ddio_err_count_show(struct class_device *cdev, char *buf);
ssize_t
_ctl_BRM_status_show(struct class_device *cdev, char *buf);
#endif
#endif
#endif /* MPT3SAS_BASE_H_INCLUDED */
