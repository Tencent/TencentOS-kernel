/*
 * Compatiblity Header for compilation working across multiple kernels
 *
 * This code is based on drivers/scsi/mpt3sas/mpt3sas_base.c
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

#ifndef FUSION_LINUX_COMPAT_H
#define FUSION_LINUX_COMPAT_H

#include <linux/version.h>

#if ((defined(RHEL_MAJOR) && (RHEL_MAJOR == 7) && (RHEL_MINOR >= 3)) || \
    (defined(CONFIG_SUSE_KERNEL) && (LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,21))) || \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0)))
#include <linux/irq_poll.h>
#define MPT3SAS_ENABLE_IRQ_POLL
#endif

#if ((defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,59) && LINUX_VERSION_CODE < KERNEL_VERSION(4,12,14)))
extern int scsi_internal_device_block(struct scsi_device *sdev, bool wait);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(4,11,0))
extern int scsi_internal_device_block(struct scsi_device *sdev);
#endif

#if ((defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,70)) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0) \
&& LINUX_VERSION_CODE < KERNEL_VERSION(4,11,0)))
extern int scsi_internal_device_unblock(struct scsi_device *sdev,
					enum scsi_device_state new_state);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))
extern int scsi_internal_device_unblock(struct scsi_device *sdev);
#endif

#ifndef DID_TRANSPORT_DISRUPTED
#define DID_TRANSPORT_DISRUPTED DID_BUS_BUSY
#endif

#ifndef ULLONG_MAX
#define ULLONG_MAX      (~0ULL)
#endif

#ifndef USHORT_MAX
#define USHORT_MAX      ((u16)(~0U))
#endif

#ifndef UINT_MAX
#define UINT_MAX        (~0U)
#endif

/*
 * TODO Need to change 'shost_private' back to 'shost_priv' when suppying patchs
 * upstream.  Since Red Hat decided to backport this to rhel5.2 (2.6.18-92.el5)
 * from the 2.6.23 kernel, it will make it difficult for us to add the proper
 * glue in our driver.
 */
static inline void *shost_private(struct Scsi_Host *shost)
{
	return (void *)shost->hostdata;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27))
static inline sector_t scsi_get_lba(struct scsi_cmnd *scmd)
{
        return scmd->request->sector;
}
#endif

/**
 * mpt_scsi_build_sense_buffer - build sense data in a buffer
 * @desc:	Sense format (non zero == descriptor format,
 * 		0 == fixed format)
 * @buf:	Where to build sense data
 * @key:	Sense key
 * @asc:	Additional sense code
 * @ascq:	Additional sense code qualifier
 *
 * Note: scsi_build_sense_buffer was added in the 2.6.26 kernel
 * It was backported in RHEL5.3 2.6.18-128.el5
 **/
static inline void mpt_scsi_build_sense_buffer(int desc, u8 *buf, u8 key,
    u8 asc, u8 ascq)
{
	if (desc) {
		buf[0] = 0x72;	/* descriptor, current */
		buf[1] = key;
		buf[2] = asc;
		buf[3] = ascq;
		buf[7] = 0;
	} else {
		buf[0] = 0x70;	/* fixed, current */
		buf[2] = key;
		buf[7] = 0xa;
		buf[12] = asc;
		buf[13] = ascq;
	}
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
/**
 * mpt_scsilun_to_int: convert a scsi_lun to an int
 * @scsilun:    struct scsi_lun to be converted.
 *
 * Description:
 *     Convert @scsilun from a struct scsi_lun to a four byte host byte-ordered
 *     integer, and return the result. The caller must check for
 *     truncation before using this function.
 *
 * Notes:
 *     The struct scsi_lun is assumed to be four levels, with each level
 *     effectively containing a SCSI byte-ordered (big endian) short; the
 *     addressing bits of each level are ignored (the highest two bits).
 *     For a description of the LUN format, post SCSI-3 see the SCSI
 *     Architecture Model, for SCSI-3 see the SCSI Controller Commands.
 *
 *     Given a struct scsi_lun of: 0a 04 0b 03 00 00 00 00, this function
 *     returns the integer: 0x0b030a04
 **/
static inline int mpt_scsilun_to_int(struct scsi_lun *scsilun)
{
	int i;
	unsigned int lun;

	lun = 0;
	for (i = 0; i < sizeof(lun); i += 2)
		lun = lun | (((scsilun->scsi_lun[i] << 8) |
			scsilun->scsi_lun[i + 1]) << (i * 8));
	return lun;
}
#else
static inline int mpt_scsilun_to_int(struct scsi_lun *scsilun)
{
	return scsilun_to_int(scsilun);
}
#endif

/**
 * mpt3sas_set_requeue_or_reset -
 * @scmd: pointer to scsi command object
 *
 * Tells whether scmd needs retried by using DID_RESET / DID_REQUEUE
 */
static inline void mpt3sas_set_requeue_or_reset(struct scsi_cmnd *scmd)
{
#if ((defined(RHEL_MAJOR) && (RHEL_MAJOR == 7) && (RHEL_MINOR == 3)) || \
((LINUX_VERSION_CODE >= KERNEL_VERSION(3,17,0)) && \
(LINUX_VERSION_CODE < KERNEL_VERSION(4,4,14))))
	scmd->result = DID_REQUEUE << 16;
#else
	scmd->result = DID_RESET << 16;
#endif
}

/**
 * mpt3sas_determine_failed_or_fast_io_fail_status -
 *
 * Return FAST_IO_FAIL status if kernel supports fast io fail feature
 * otherwise return FAILED status.
 */
static inline int
mpt3sas_determine_failed_or_fast_io_fail_status(void)
{
#ifdef FAST_IO_FAIL
	return FAST_IO_FAIL;
#else
	return FAILED;
#endif
}

#endif /* FUSION_LINUX_COMPAT_H */
