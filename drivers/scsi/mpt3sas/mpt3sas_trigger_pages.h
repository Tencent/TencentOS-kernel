
/*
 * This is the Fusion MPT base driver providing common API layer interface
 * to store diag trigger values into persistent driver triggers pages
 * for MPT (Message Passing Technology) based 
 * controllers
 *
 * This code is based on drivers/scsi/mpt3sas/mpt3sas_base.h
 * Copyright (C) 2013-2020  LSI Corporation
 * Copyright (C) 2013-2020  Avago Technologies
 * Copyright (C) 2013-2020  Broadcom Inc.
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

#include "mpi/mpi2_cnfg.h"

#ifndef MPI2_TRIGGER_PAGES_H
#define MPI2_TRIGGER_PAGES_H

#define MPI2_CONFIG_EXTPAGETYPE_DRIVER_PERSISTENT_TRIGGER    (0xE0)

#define MPI26_DRIVER_TRIGGER_PAGE0_PAGEVERSION               (0x01)
typedef struct _MPI26_CONFIG_PAGE_DRIVER_TIGGER_0
{
    MPI2_CONFIG_EXTENDED_PAGE_HEADER        Header;                 /* 0x00  */
    U16                                     TriggerFlags;           /* 0x08  */
    U16                                     Reserved0xA;            /* 0x0A */
    U32                                     Reserved0xC[61];        /* 0x0C */
} MPI26_CONFIG_PAGE_DRIVER_TIGGER_0,
  MPI2_POINTER PTR_MPI26_CONFIG_PAGE_DRIVER_TIGGER_0,
  Mpi26DriverTriggerPage0_t, MPI2_POINTER pMpi26DriverTriggerPage0_t;

/* Trigger Flags */
#define  MPI26_DRIVER_TRIGGER0_FLAG_MASTER_TRIGGER_VALID       (0x0001)
#define  MPI26_DRIVER_TRIGGER0_FLAG_MPI_EVENT_TRIGGER_VALID    (0x0002)
#define  MPI26_DRIVER_TRIGGER0_FLAG_SCSI_SENSE_TRIGGER_VALID   (0x0004)
#define  MPI26_DRIVER_TRIGGER0_FLAG_LOGINFO_TRIGGER_VALID      (0x0008)


#define MPI26_DRIVER_TRIGGER_PAGE1_PAGEVERSION               (0x01)
typedef struct _MPI26_DRIVER_MASTER_TIGGER_ENTRY
{
    U32    MasterTriggerFlags;
} MPI26_DRIVER_MASTER_TIGGER_ENTRY, MPI2_POINTER PTR_MPI26_DRIVER_MASTER_TIGGER_ENTRY;

#define MPI26_MAX_MASTER_TRIGGERS                                   (1)
typedef struct _MPI26_CONFIG_PAGE_DRIVER_TIGGER_1
{
    MPI2_CONFIG_EXTENDED_PAGE_HEADER        Header;                 /* 0x00  */
    U16                                     NumMasterTrigger;       /* 0x08  */
    U16                                     Reserved0xA;            /* 0x0A */
    MPI26_DRIVER_MASTER_TIGGER_ENTRY        MasterTriggers[MPI26_MAX_MASTER_TRIGGERS];   /* 0x0C */
} MPI26_CONFIG_PAGE_DRIVER_TIGGER_1,
  MPI2_POINTER PTR_MPI26_CONFIG_PAGE_DRIVER_TIGGER_1,
  Mpi26DriverTriggerPage1_t, MPI2_POINTER pMpi26DriverTriggerPage1_t;

/* Master Trigger Flags */
#define  MPI26_DRIVER_TRIGGER1_FLAG_FW_FAULT         (0x00000001)
#define  MPI26_DRIVER_TRIGGER1_FLAG_DIAG_RESET       (0x00000002)
#define  MPI26_DRIVER_TRIGGER1_FLAG_TASK_MGMT        (0x00000004)
#define  MPI26_DRIVER_TRIGGER1_FLAG_DEVICE_REMOVAL   (0x00000008)


#define MPI26_DRIVER_TRIGGER_PAGE2_PAGEVERSION               (0x01)
typedef struct _MPI26_DRIVER_MPI_EVENT_TIGGER_ENTRY
{
    U16    MPIEventCode;            /* 0x00 */
    U16    MPIEventCodeSpecific;    /* 0x02 */
} MPI26_DRIVER_MPI_EVENT_TIGGER_ENTRY, MPI2_POINTER PTR_MPI26_DRIVER_MPI_EVENT_TIGGER_ENTRY;

#define MPI26_MAX_MPI_EVENT_TRIGGERS                            (20)
typedef struct _MPI26_CONFIG_PAGE_DRIVER_TIGGER_2
{
    MPI2_CONFIG_EXTENDED_PAGE_HEADER        Header;                 /* 0x00  */
    U16                                     NumMPIEventTrigger;     /* 0x08  */
    U16                                     Reserved0xA;            /* 0x0A */
    MPI26_DRIVER_MPI_EVENT_TIGGER_ENTRY     MPIEventTriggers[MPI26_MAX_MPI_EVENT_TRIGGERS]; /* 0x0C */
} MPI26_CONFIG_PAGE_DRIVER_TIGGER_2,
  MPI2_POINTER PTR_MPI26_CONFIG_PAGE_DRIVER_TIGGER_2,
  Mpi26DriverTriggerPage2_t, MPI2_POINTER pMpi26DriverTriggerPage2_t;


#define MPI26_DRIVER_TRIGGER_PAGE3_PAGEVERSION               (0x01)
typedef struct _MPI26_DRIVER_SCSI_SENSE_TIGGER_ENTRY
{
    U8     ASCQ;      /* 0x00 */
    U8     ASC;       /* 0x01 */
    U8     SenseKey;       /* 0x02 */
    U8     Reserved;  /* 0x03 */
} MPI26_DRIVER_SCSI_SENSE_TIGGER_ENTRY, MPI2_POINTER PTR_MPI26_DRIVER_SCSI_SENSE_TIGGER_ENTRY;

#define MPI26_MAX_SCSI_SENSE_TRIGGERS                            (20)
typedef struct _MPI26_CONFIG_PAGE_DRIVER_TIGGER_3
{
    MPI2_CONFIG_EXTENDED_PAGE_HEADER        Header;                  /* 0x00  */
    U16                                     NumSCSISenseTrigger;     /* 0x08  */
    U16                                     Reserved0xA;             /* 0x0A */
    MPI26_DRIVER_SCSI_SENSE_TIGGER_ENTRY    SCSISenseTriggers[MPI26_MAX_SCSI_SENSE_TRIGGERS];    /* 0x0C */
} MPI26_CONFIG_PAGE_DRIVER_TIGGER_3,
  MPI2_POINTER PTR_MPI26_CONFIG_PAGE_DRIVER_TIGGER_3,
  Mpi26DriverTriggerPage3_t, MPI2_POINTER pMpi26DriverTriggerPage3_t;

#define MPI26_DRIVER_TRIGGER_PAGE4_PAGEVERSION               (0x01)
typedef struct _MPI26_DRIVER_IOCSTATUS_LOGINFO_TIGGER_ENTRY
{
    U16        IOCStatus;      /* 0x00 */
    U16        Reserved;       /* 0x02 */
    U32        LogInfo;        /* 0x04 */
} MPI26_DRIVER_IOCSTATUS_LOGINFO_TIGGER_ENTRY, MPI2_POINTER PTR_MPI26_DRIVER_IOCSTATUS_LOGINFO_TIGGER_ENTRY;

#define MPI26_MAX_LOGINFO_TRIGGERS                            (20)
typedef struct _MPI26_CONFIG_PAGE_DRIVER_TIGGER_4
{
    MPI2_CONFIG_EXTENDED_PAGE_HEADER               Header;                      /* 0x00  */
    U16                                            NumIOCStatusLogInfoTrigger;  /* 0x08  */
    U16                                            Reserved0xA;                 /* 0x0A */
    MPI26_DRIVER_IOCSTATUS_LOGINFO_TIGGER_ENTRY    IOCStatusLoginfoTriggers[MPI26_MAX_LOGINFO_TRIGGERS]; /* 0x0C */
} MPI26_CONFIG_PAGE_DRIVER_TIGGER_4,
  MPI2_POINTER PTR_MPI26_CONFIG_PAGE_DRIVER_TIGGER_4,
  Mpi26DriverTriggerPage4_t, MPI2_POINTER pMpi26DriverTriggerPage4_t;

#endif
