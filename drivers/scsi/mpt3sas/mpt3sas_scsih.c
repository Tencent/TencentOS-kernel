/*
 * Scsi Host Layer for MPT (Message Passing Technology) based controllers
 *
 * This code is based on drivers/scsi/mpt3sas/mpt3sas_scsih.c
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

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/pci.h>
#if !((defined(RHEL_MAJOR) && (RHEL_MAJOR == 8) && (RHEL_MINOR > 2)) || \
	(LINUX_VERSION_CODE > KERNEL_VERSION(5,4,0)) || \
	(LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)))
#include <linux/pci-aspm.h>
#endif
#include <linux/interrupt.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0))
#include <linux/nvme.h>
#endif
#include <asm/unaligned.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19))
#include <linux/aer.h>
#endif
#include <linux/raid_class.h>

#include "mpt3sas_base.h"

#define RAID_CHANNEL 1

#define PCIE_CHANNEL 2

/* forward proto's */
static void _scsih_expander_node_remove(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_node *sas_expander);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
static void _firmware_event_work(struct work_struct *work);
static void _firmware_event_work_delayed(struct work_struct *work);
#else
static void _firmware_event_work(void *arg);
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27))
static enum device_responsive_state
_scsih_read_capacity_16(struct MPT3SAS_ADAPTER *ioc, u16 handle, u32 lun,
	void *data, u32 data_length);
#endif

static enum device_responsive_state
_scsih_inquiry_vpd_sn(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u8 **serial_number);
static enum device_responsive_state
_scsih_inquiry_vpd_supported_pages(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u32 lun, void *data, u32 data_length);
static enum device_responsive_state
_scsih_ata_pass_thru_idd(struct MPT3SAS_ADAPTER *ioc, u16 handle, u8 *is_ssd_device,
	u8 tr_timeout, u8 tr_method);
static enum device_responsive_state
_scsih_wait_for_target_to_become_ready(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u8 retry_count, u8 is_pd, u8 tr_timeout, u8 tr_method);
static enum device_responsive_state
_scsih_wait_for_device_to_become_ready(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u8 retry_count, u8 is_pd, int lun, u8 tr_timeout, u8 tr_method);
static void _scsih_remove_device(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_device *sas_device);
static int _scsih_add_device(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u8 retry_count, u8 is_pd);
static int _scsih_pcie_add_device(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u8 retry_count);
static void _scsih_pcie_device_remove_from_sml(struct MPT3SAS_ADAPTER *ioc,
	struct _pcie_device *pcie_device);

static u8 _scsih_check_for_pending_tm(struct MPT3SAS_ADAPTER *ioc, u16 smid);
static void
_scsih_send_event_to_turn_on_pfa_led(struct MPT3SAS_ADAPTER *ioc, u16 handle);

void _scsih_log_entry_add_event(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataLogEntryAdded_t *log_entry);
static u16
_scsih_determine_hba_mpi_version(struct pci_dev *pdev);

/* global parameters */
LIST_HEAD(mpt3sas_ioc_list);
/* global ioc lock for list operations */
DEFINE_SPINLOCK(gioc_lock);

MODULE_AUTHOR(MPT3SAS_AUTHOR);
MODULE_DESCRIPTION(MPT3SAS_DESCRIPTION);
MODULE_LICENSE("GPL");
MODULE_VERSION(MPT3SAS_DRIVER_VERSION);
MODULE_ALIAS("mpt2sas");

/* local parameters */
static u8 scsi_io_cb_idx = -1;
static u8 tm_cb_idx = -1;
static u8 ctl_cb_idx = -1;
static u8 ctl_tm_cb_idx = -1;
static u8 ctl_diag_cb_idx = -1;
static u8 base_cb_idx = -1;
static u8 port_enable_cb_idx = -1;
static u8 transport_cb_idx = -1;
static u8 scsih_cb_idx = -1;
static u8 config_cb_idx = -1;
static int mpt2_ids;
static int mpt3_ids;

static u8 tm_tr_cb_idx = -1 ;
static u8 tm_tr_volume_cb_idx = -1 ;
static u8 tm_tr_internal_cb_idx =-1;
static u8 tm_sas_control_cb_idx = -1;

/* command line options */
static u32 logging_level;
MODULE_PARM_DESC(logging_level, " bits for enabling additional logging info "
	"(default=0)");

static bool enable_sdev_max_qd;
module_param(enable_sdev_max_qd, bool, 0444);
MODULE_PARM_DESC(enable_sdev_max_qd,
	"Enable sdev max qd as can_queue, def=disabled(0)");

static ushort max_sectors = 0xFFFF;
module_param(max_sectors, ushort, 0444);
MODULE_PARM_DESC(max_sectors, "max sectors, range 64 to 32767  default=32767");

static int command_retry_count = 144;
module_param(command_retry_count, int, 0444);
MODULE_PARM_DESC(command_retry_count, " Device discovery TUR command retry "
	"count: (default=144)");

static int missing_delay[2] = {-1, -1};
module_param_array(missing_delay, int, NULL, 0444);
MODULE_PARM_DESC(missing_delay, " device missing delay , io missing delay");

static int host_lock_mode = 0;
module_param(host_lock_mode, int, 0444);
MODULE_PARM_DESC(host_lock_mode, "Enable SCSI host lock if set to 1"
	"(default=0)");

/* scsi-mid layer global parmeter is max_report_luns, which is 511 */
#define MPT3SAS_MAX_LUN (16895)
static int max_lun = MPT3SAS_MAX_LUN;
module_param(max_lun, int, 0444);
MODULE_PARM_DESC(max_lun, " max lun, default=16895 ");

/* hbas_to_enumerate is set to -1 by default.
 * This allows to enumerate all sas2, sas3 & above generation HBAs
 * only on kernel version > 4.4.
 * On Lower kernel versions < 4.4. this enumerates only sas3 driver.
 * Module Param 0 will enumerate all sas2, sas3 & above generation HBAs
 * on all kernels.
 */
static int hbas_to_enumerate = -1;
module_param(hbas_to_enumerate, int, 0444);
MODULE_PARM_DESC(hbas_to_enumerate,
               " 0 - enumerates all SAS 2.0, PCIe HBA, SAS3.0 & above generation HBAs\n \
                 1 - enumerates only PCIe HBA & SAS 2.0 generation HBAs\n \
                 2 - enumerates PCIe HBA, SAS 3.0 & above generation HBAs (default=-1,"
               " Enumerates all SAS 2.0, PCIe HBA, SAS 3.0 & above generation HBAs else"
               " PCIe HBA, SAS 3.0 & above generation HBAs only)");

static int multipath_on_hba = -1;
module_param(multipath_on_hba, int, 0444);
MODULE_PARM_DESC(multipath_on_hba,
		" Multipath support to add same target device\n \
		as many times as it is visible to HBA from various paths\n \
		(by default: \n \
			SAS 2.0,SAS 3.0 HBA & SAS3.5 HBA-"
			"This will be disabled)");

/* Enable or disable EEDP support */
static int disable_eedp = 0;
module_param(disable_eedp, uint, 0444);
MODULE_PARM_DESC(disable_eedp, " disable EEDP support: (default=0)");

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
/* diag_buffer_enable is bitwise
 * bit 0 set = TRACE
 * bit 1 set = SNAPSHOT
 * bit 2 set = EXTENDED
 *
 * Either bit can be set, or both
 */
static int diag_buffer_enable = -1;
module_param(diag_buffer_enable, int, 0444);
MODULE_PARM_DESC(diag_buffer_enable, " post diag buffers "
	"(TRACE=1/SNAPSHOT=2/EXTENDED=4/default=0)");
static int disable_discovery = -1;
module_param(disable_discovery, int, 0444);
MODULE_PARM_DESC(disable_discovery, " disable discovery ");
#else
extern int disable_discovery;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22))
static unsigned int allow_drive_spindown = 1;
module_param(allow_drive_spindown, uint, 0444);
MODULE_PARM_DESC(allow_drive_spindown, " allow host driver to "
	"issue START STOP UNIT(STOP) command to spindown the drive before shut down or driver unload, default=1, \n"
	" \t\tDont spindown any SATA drives =0 /	Spindown SSD but not HDD = 1/ 	Spindown HDD but not SSD =2/  Spindown all SATA drives =3");
#endif

/**
permit overriding the host protection capabilities mask (EEDP/T10 PI).
Ref tag errors are observed on kernels < 3.18 on issuing IOs on 4K
drive. This is a kernel Bug.
Disabled DIX support by default.
*/
int prot_mask = 0x07;
module_param(prot_mask, int, 0444);
MODULE_PARM_DESC(prot_mask, " host protection capabilities mask, def=0x07");

#ifndef SCSI_MPT2SAS
/* permit overriding the host protection algorithm mask (T10 CRC/ IP Checksum) */
static int protection_guard_mask = 3;
module_param(protection_guard_mask, int, 0444);
MODULE_PARM_DESC(protection_guard_mask, " host protection algorithm mask, def=3 ");
#endif

/* permit overriding the SCSI command issuing capability of
	the driver to bring the drive to READY state*/
static int issue_scsi_cmd_to_bringup_drive = 1;
module_param(issue_scsi_cmd_to_bringup_drive, int, 0444);
MODULE_PARM_DESC(issue_scsi_cmd_to_bringup_drive, " allow host driver to "
	"issue SCSI commands to bring the drive to READY state, default=1 ");
static int sata_smart_polling = 0;
module_param(sata_smart_polling, uint, 0444);
MODULE_PARM_DESC(sata_smart_polling, " poll for smart errors on SATA drives: (default=0)");
#if (defined(CONFIG_SUSE_KERNEL) && defined(scsi_is_sas_phy_local)) || \
	LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
#define MPT_WIDE_PORT_API	1
#define MPT_WIDE_PORT_API_PLUS	1
#endif

/* raid transport support */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
static struct raid_template *mpt3sas_raid_template;
static struct raid_template *mpt2sas_raid_template;
#endif

/**
 * enum device_responsive_state - responsive state
 * @DEVICE_READY: device is ready to be added
 * @DEVICE_RETRY: device can be retried later
 * @DEVICE_RETRY_UA: retry unit attentions
 * @DEVICE_START_UNIT: requires start unit
 * @DEVICE_STOP_UNIT: requires stop unit
 * @DEVICE_ERROR: device reported some fatal error
 *
 * Look at _scsih_wait_for_target_to_become_ready()
 *
 */
enum device_responsive_state {
	DEVICE_READY,
	DEVICE_RETRY,
	DEVICE_RETRY_UA,
	DEVICE_START_UNIT,
	DEVICE_STOP_UNIT,
	DEVICE_ERROR,
};

/**
 * struct sense_info - common structure for obtaining sense keys
 * @skey: sense key
 * @asc: additional sense code
 * @ascq: additional sense code qualifier
 */
struct sense_info {
	u8 skey;
	u8 asc;
	u8 ascq;
};

#define MPT3SAS_PROCESS_TRIGGER_DIAG (0xFFFB)
#define MPT3SAS_TURN_ON_PFA_LED (0xFFFC)
#define MPT3SAS_PORT_ENABLE_COMPLETE (0xFFFD)
#define MPT3SAS_ABRT_TASK_SET (0xFFFE)
#define MPT3SAS_REMOVE_UNRESPONDING_DEVICES (0xFFFF)
/**
 * struct fw_event_work - firmware event struct
 * @list: link list framework
 * @work: work object (ioc->fault_reset_work_q)
 * @cancel_pending_work: flag set during reset handling
 * @ioc: per adapter object
 * @device_handle: device handle
 * @VF_ID: virtual function id
 * @VP_ID: virtual port id
 * @ignore: flag meaning this event has been marked to ignore
 * @event: firmware event MPI2_EVENT_XXX defined in mpt2_ioc.h
 * @refcount: reference count for fw_event_work
 * @event_data: reply event data payload follows
 * @retries: number of times this event has been retried(for each device)
 *
 * This object stored on ioc->fw_event_list.
 */
struct fw_event_work {
	struct list_head	list;
	struct work_struct	work;
	u8			cancel_pending_work;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	struct delayed_work	delayed_work;
	u8			delayed_work_active;
#endif
	struct MPT3SAS_ADAPTER *ioc;
	u16			device_handle;
	u8			VF_ID;
	u8			VP_ID;
	u8			ignore;
	u16			event;
	struct kref		refcount;
	void			*event_data;
	u8			*retries;
};

/**
 * fw_event_work_free - Free the firmware event work structure
 * @r : kref  object
 *
 * Free the firmware event work structure. This will be called
 * corresponding firmware event refernce count reaches to zero.
 */
static void fw_event_work_free(struct kref *r)
{
	struct fw_event_work *fw_work;
	fw_work = container_of(r, struct fw_event_work, refcount);
	kfree(fw_work->event_data);
	kfree(fw_work->retries);
	kfree(fw_work);
}

/** fw_event_work_get - Increment the firmware event work's
 *			reference count.
 * @fw_work: firmware event work's object
 *
 * Increments the firmware event work's reference count.
 */
static void fw_event_work_get(struct fw_event_work *fw_work)
{
	kref_get(&fw_work->refcount);
}

/** fw_event_work_put - Decrement the firmware event work's
 *			reference count.
 * @fw_work: firmware event work's object
 *
 * Decrements the firmware event work's reference count.
 * When reference count reaches to zero it will indirectly
 * call fw_event_work_free() to free the firmware event work's
 * object.
 */
static void fw_event_work_put(struct fw_event_work *fw_work)
{
	kref_put(&fw_work->refcount, fw_event_work_free);
}

/** alloc_fw_event_work - allocate's memory for firmware event work
 * @len: extra memory length
 *
 * allocate's memory for firmware event works and also initlialize
 * firmware event work's reference count.
 */
static struct fw_event_work *alloc_fw_event_work(int len)
{
	struct fw_event_work *fw_event;

	fw_event = kzalloc(sizeof(*fw_event) + len, GFP_ATOMIC);
	if (!fw_event)
		return NULL;

	kref_init(&fw_event->refcount);
	return fw_event;
}

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)))
static inline unsigned int mpt3sas_scsi_prot_interval(struct scsi_cmnd *scmd)
{
        return scmd->device->sector_size;
}

static inline u32 mpt3sas_scsi_prot_ref_tag(struct scsi_cmnd *scmd)
{
      return blk_rq_pos(scmd->request) >>
               (ilog2(mpt3sas_scsi_prot_interval(scmd)) - 9) & 0xffffffff;
}
#endif

/**
 * struct _scsi_io_transfer - scsi io transfer
 * @handle: sas device handle (assigned by firmware)
 * @is_raid: flag set for hidden raid components
 * @dir: DMA_TO_DEVICE, DMA_FROM_DEVICE,
 * @data_length: data transfer length
 * @data_dma: dma pointer to data
 * @sense: sense data
 * @lun: lun number
 * @cdb_length: cdb length
 * @cdb: cdb contents
 * @timeout: timeout for this command
 * @VF_ID: virtual function id
 * @VP_ID: virtual port id
 * @valid_reply: flag set for reply message
 * @sense_length: sense length
 * @ioc_status: ioc status
 * @scsi_state: scsi state
 * @scsi_status: scsi staus
 * @log_info: log information
 * @transfer_length: data length transfer when there is a reply message
 *
 * Used for sending internal scsi commands to devices within this module.
 * Refer to _scsi_send_scsi_io().
 */
struct _scsi_io_transfer {
	u16	handle;
	u8	is_raid;
	enum dma_data_direction dir;
	u32	data_length;
	dma_addr_t data_dma;
	u8	sense[SCSI_SENSE_BUFFERSIZE];
	u32	lun;
	u8	cdb_length;
	u8	cdb[32];
	u8	timeout;
	u8	VF_ID;
	u8	VP_ID;
	u8	valid_reply;
  /* the following bits are only valid when 'valid_reply = 1' */
	u32	sense_length;
	u16	ioc_status;
	u8	scsi_state;
	u8	scsi_status;
	u32	log_info;
	u32	transfer_length;
};

/**
 * _scsih_set_debug_level - global setting of ioc->logging_level.
 *
 * Note: The logging levels are defined in mpt3sas_debug.h.
 */
static int
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0))
_scsih_set_debug_level(const char *val, const struct kernel_param *kp)
#else
_scsih_set_debug_level(const char *val, struct kernel_param *kp)
#endif
{
	int ret = param_set_int(val, kp);
	struct MPT3SAS_ADAPTER *ioc;

	if (ret)
		return ret;

	printk(KERN_INFO "setting logging_level(0x%08x)\n", logging_level);
	spin_lock(&gioc_lock);
	list_for_each_entry(ioc, &mpt3sas_ioc_list, list)
		ioc->logging_level = logging_level;
	spin_unlock(&gioc_lock);
	return 0;
}
module_param_call(logging_level, _scsih_set_debug_level, param_get_int,
	&logging_level, 0644);

/**
 * _scsih_srch_boot_sas_address - search based on sas_address
 * @sas_address: sas address
 * @boot_device: boot device object from bios page 2
 *
 * Returns 1 when there's a match, 0 means no match.
 */
static inline int
_scsih_srch_boot_sas_address(u64 sas_address,
	Mpi2BootDeviceSasWwid_t *boot_device)
{
	return (sas_address == le64_to_cpu(boot_device->SASAddress)) ?  1 : 0;
}

/**
 * _scsih_srch_boot_device_name - search based on device name
 * @device_name: device name specified in INDENTIFY fram
 * @boot_device: boot device object from bios page 2
 *
 * Returns 1 when there's a match, 0 means no match.
 */
static inline int
_scsih_srch_boot_device_name(u64 device_name,
	Mpi2BootDeviceDeviceName_t *boot_device)
{
	return (device_name == le64_to_cpu(boot_device->DeviceName)) ? 1 : 0;
}

/**
 * _scsih_srch_boot_encl_slot - search based on enclosure_logical_id/slot
 * @enclosure_logical_id: enclosure logical id
 * @slot_number: slot number
 * @boot_device: boot device object from bios page 2
 *
 * Returns 1 when there's a match, 0 means no match.
 */
static inline int
_scsih_srch_boot_encl_slot(u64 enclosure_logical_id, u16 slot_number,
	Mpi2BootDeviceEnclosureSlot_t *boot_device)
{
	return (enclosure_logical_id == le64_to_cpu(boot_device->
	    EnclosureLogicalID) && slot_number == le16_to_cpu(boot_device->
	    SlotNumber)) ? 1 : 0;
}

static void
_scsih_display_enclosure_chassis_info(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_device *sas_device, struct scsi_device *sdev,
	struct scsi_target *starget)
{
	if(sdev) {
		if(sas_device->enclosure_handle != 0)
			sdev_printk(KERN_INFO, sdev, "enclosure logical id"
				    "(0x%016llx), slot(%d) \n",
				    (unsigned long long)
				    sas_device->enclosure_logical_id,
				    sas_device->slot);
		if(sas_device->connector_name[0] != '\0')
			sdev_printk(KERN_INFO, sdev, "enclosure level"
				    "(0x%04x), connector name( %s)\n",
				    sas_device->enclosure_level,
				    sas_device->connector_name);
		if (sas_device->is_chassis_slot_valid)
			sdev_printk(KERN_INFO, sdev, "chassis slot(0x%04x)\n",
			    	    sas_device->chassis_slot);
	}
	else if(starget) {
		if(sas_device->enclosure_handle != 0)
			starget_printk(KERN_INFO, starget, "enclosure"
				       "logical id(0x%016llx), slot(%d) \n",
				       (unsigned long long)
				       sas_device->enclosure_logical_id,
				       sas_device->slot);
		if(sas_device->connector_name[0] != '\0')
			starget_printk(KERN_INFO, starget, "enclosure level"
				       "(0x%04x), connector name( %s)\n",
				       sas_device->enclosure_level,
				       sas_device->connector_name);
		if (sas_device->is_chassis_slot_valid)
			starget_printk(KERN_INFO, starget, "chassis slot"
				       "(0x%04x)\n",
				       sas_device->chassis_slot);
	}
	else {

		if(sas_device->enclosure_handle != 0)
			printk(MPT3SAS_INFO_FMT "enclosure logical id"
			       "(0x%016llx), slot(%d) \n",ioc->name,
			       (unsigned long long)
			       sas_device->enclosure_logical_id,
			       sas_device->slot);
		if(sas_device->connector_name[0] != '\0')
			printk(MPT3SAS_INFO_FMT "enclosure level(0x%04x),"
			       "connector name( %s)\n",ioc->name,
			       sas_device->enclosure_level,
			       sas_device->connector_name);
		if(sas_device->is_chassis_slot_valid)
			printk(MPT3SAS_INFO_FMT "chassis slot(0x%04x)\n",
			       ioc->name, sas_device->chassis_slot);
	}

}

/**
 * mpt3sas_get_port_by_id - get port entry corresponding to provided
 *			  port number from port list
 * @ioc: per adapter object
 * @port: port number
 *
 * Search for port entry corresponding to provided port number,
 * if available return port address otherwise return NULL.
 */
struct hba_port *
mpt3sas_get_port_by_id(struct MPT3SAS_ADAPTER *ioc, u8 port_id,
	u8 skip_dirty_flag)
{

	struct hba_port *port, *port_next;

	if (!ioc->multipath_on_hba)
		port_id = MULTIPATH_DISABLED_PORT_ID;

	list_for_each_entry_safe (port, port_next,
	    &ioc->port_table_list, list) {
		if (port->port_id != port_id)
			continue;
		if (port->flags & HBA_PORT_FLAG_DIRTY_PORT)
			continue;
		return port;
	}

	if (skip_dirty_flag) {
		port = port_next = NULL;
		list_for_each_entry_safe (port, port_next,
					&ioc->port_table_list, list) {
			if (port->port_id != port_id)
				continue;
			return port;
		}
	}

	if (unlikely(!ioc->multipath_on_hba)) {
		port = kzalloc(sizeof(struct hba_port), GFP_KERNEL);
		if (!port) {
			printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			return NULL;
		}
		port->port_id = MULTIPATH_DISABLED_PORT_ID;
		printk(MPT3SAS_INFO_FMT
		   "hba_port entry: %p, port: %d is added to hba_port list\n",
		   ioc->name, port, port->port_id);
		list_add_tail(&port->list, &ioc->port_table_list);

		return port;
	}

	return NULL;
}

/**
 * mpt3sas_get_vphy_by_phy - get virtual_phy object corresponding to phy number
 * @ioc: per adapter object
 * @port: hba_port object
 * @phy: phy number
 *
 * Return virtual_phy object corresponding to phy number.
 */
struct virtual_phy *
mpt3sas_get_vphy_by_phy(struct MPT3SAS_ADAPTER *ioc, struct hba_port *port, u32 phy)
{
	struct virtual_phy *vphy, *vphy_next;

	if (!port->vphys_mask)
		return NULL;

	list_for_each_entry_safe(vphy, vphy_next, &port->vphys_list, list) {
		if (vphy->phy_mask & ( 1 << phy))
			return vphy;
	}
	return NULL;
}

/**
 * _scsih_is_boot_device - search for matching boot device.
 * @sas_address: sas address or WWID if PCIe device
 * @device_name: device name specified in INDENTIFY fram
 * @enclosure_logical_id: enclosure logical id
 * @slot_number: slot number
 * @form: specifies boot device form
 * @boot_device: boot device object from bios page 2
 *
 * Returns 1 when there's a match, 0 means no match.
 */
static int
_scsih_is_boot_device(u64 sas_address, u64 device_name,
	u64 enclosure_logical_id, u16 slot, u8 form,
	Mpi2BiosPage2BootDevice_t *boot_device)
{
	int rc = 0;

	switch (form) {
	case MPI2_BIOSPAGE2_FORM_SAS_WWID:
		if (!sas_address)
			break;
		rc = _scsih_srch_boot_sas_address(
		    sas_address, &boot_device->SasWwid);
		break;
	case MPI2_BIOSPAGE2_FORM_ENCLOSURE_SLOT:
		if (!enclosure_logical_id)
			break;
		rc = _scsih_srch_boot_encl_slot(
		    enclosure_logical_id,
		    slot, &boot_device->EnclosureSlot);
		break;
	case MPI2_BIOSPAGE2_FORM_DEVICE_NAME:
		if (!device_name)
			break;
		rc = _scsih_srch_boot_device_name(
		    device_name, &boot_device->DeviceName);
		break;
	case MPI2_BIOSPAGE2_FORM_NO_DEVICE_SPECIFIED:
		break;
	}

	return rc;
}

/**
 * _scsih_get_sas_address - set the sas_address for given device handle
 * @handle: device handle
 * @sas_address: sas address
 *
 * Returns 0 success, non-zero when failure
 */
static int
_scsih_get_sas_address(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u64 *sas_address)
{
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u32 ioc_status;

	*sas_address = 0;

	if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle))) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n", ioc->name,
		__FILE__, __LINE__, __func__);
		return -ENXIO;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
		/* For HBA vSES don't return hba sas address instead return
		 * vSES's sas address.
		 */
		if ((handle <= ioc->sas_hba.num_phys) &&
		    (!(le32_to_cpu(sas_device_pg0.DeviceInfo) &
		     MPI2_SAS_DEVICE_INFO_SEP)))
			*sas_address = ioc->sas_hba.sas_address;
		else
			*sas_address = le64_to_cpu(sas_device_pg0.SASAddress);
		return 0;
	}

	/* we hit this becuase the given parent handle doesn't exist */
	if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
		return -ENXIO;

	/* else error case */
	printk(MPT3SAS_ERR_FMT "handle(0x%04x), ioc_status(0x%04x), "
	    "failure at %s:%d/%s()!\n", ioc->name, handle, ioc_status,
	     __FILE__, __LINE__, __func__);
	return -EIO;
}

/**
 * _scsih_determine_boot_device - determine boot device.
 * @ioc: per adapter object
 * @device: sas_device or pcie_device object
 * @channel: SAS(0), raid(1) or PCIe(2) channel
 *
 * Determines whether this device should be first reported device to
 * to scsi-ml or sas transport, this purpose is for persistent boot device.
 * There are primary, alternate, and current entries in bios page 2. The order
 * priority is primary, alternate, then current.  This routine saves
 * the corresponding device object.
 * The saved data to be used later in _scsih_probe_boot_devices().
 */
static void
_scsih_determine_boot_device(struct MPT3SAS_ADAPTER *ioc, void *device,
    u32 channel)
{
	struct _sas_device *sas_device;
	struct _pcie_device *pcie_device;
	struct _raid_device *raid_device;
	u64 sas_address;
	u64 device_name;
	u64 enclosure_logical_id;
	u16 slot;

	 /* only process this function when driver loads */
	if (!ioc->is_driver_loading)
		return;

	 /* no Bios, return immediately */
	if (!ioc->bios_pg3.BiosVersion)
		return;

	if (channel == RAID_CHANNEL) {
		raid_device = device;
		sas_address = raid_device->wwid;
		device_name = 0;
		enclosure_logical_id = 0;
		slot = 0;
	} else if (channel == PCIE_CHANNEL) {
		pcie_device = device;
		sas_address = pcie_device->wwid;
		device_name = 0;
		enclosure_logical_id = 0;
		slot = 0;
	} else {
		sas_device = device;
		sas_address = sas_device->sas_address;
		device_name = sas_device->device_name;
		enclosure_logical_id = sas_device->enclosure_logical_id;
		slot = sas_device->slot;
	}

	if (!ioc->req_boot_device.device) {
		if (_scsih_is_boot_device(sas_address, device_name,
		    enclosure_logical_id, slot,
		    (ioc->bios_pg2.ReqBootDeviceForm &
		    MPI2_BIOSPAGE2_FORM_MASK),
		    &ioc->bios_pg2.RequestedBootDevice)) {
			dinitprintk(ioc, printk(MPT3SAS_INFO_FMT
			   "%s: req_boot_device(0x%016llx)\n",
			    ioc->name, __func__,
			    (unsigned long long)sas_address));
			ioc->req_boot_device.device = device;
			ioc->req_boot_device.channel = channel;
		}
	}

	if (!ioc->req_alt_boot_device.device) {
		if (_scsih_is_boot_device(sas_address, device_name,
		    enclosure_logical_id, slot,
		    (ioc->bios_pg2.ReqAltBootDeviceForm &
		    MPI2_BIOSPAGE2_FORM_MASK),
		    &ioc->bios_pg2.RequestedAltBootDevice)) {
			dinitprintk(ioc, printk(MPT3SAS_INFO_FMT
			   "%s: req_alt_boot_device(0x%016llx)\n",
			    ioc->name, __func__,
			    (unsigned long long)sas_address));
			ioc->req_alt_boot_device.device = device;
			ioc->req_alt_boot_device.channel = channel;
		}
	}

	if (!ioc->current_boot_device.device) {
		if (_scsih_is_boot_device(sas_address, device_name,
		    enclosure_logical_id, slot,
		    (ioc->bios_pg2.CurrentBootDeviceForm &
		    MPI2_BIOSPAGE2_FORM_MASK),
		    &ioc->bios_pg2.CurrentBootDevice)) {
			dinitprintk(ioc, printk(MPT3SAS_INFO_FMT
			   "%s: current_boot_device(0x%016llx)\n",
			    ioc->name, __func__,
			    (unsigned long long)sas_address));
			ioc->current_boot_device.device = device;
			ioc->current_boot_device.channel = channel;
		}
	}
}

static struct _sas_device *
__mpt3sas_get_sdev_from_target(struct MPT3SAS_ADAPTER *ioc,
	struct MPT3SAS_TARGET *tgt_priv)
{
	struct _sas_device *ret;

	assert_spin_locked(&ioc->sas_device_lock);

	ret = tgt_priv->sas_dev;
	if (ret)
		sas_device_get(ret);

	return ret;
}

/**
 * mpt3sas_get_sdev_from_target - sas device search
 * @ioc: per adapter object
 * @tgt_priv: starget private object
 *
 * Context: This function will acquire ioc->sas_device_lock and will release
 * before returning the sas_device object.
 *
 * This searches for sas_device from target, then return sas_device
 * object.
 */
struct _sas_device *
mpt3sas_get_sdev_from_target(struct MPT3SAS_ADAPTER *ioc,
	struct MPT3SAS_TARGET *tgt_priv)
{
	struct _sas_device *ret;
	unsigned long flags;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	ret = __mpt3sas_get_sdev_from_target(ioc, tgt_priv);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	return ret;
}

static struct _pcie_device *
__mpt3sas_get_pdev_from_target(struct MPT3SAS_ADAPTER *ioc,
	struct MPT3SAS_TARGET *tgt_priv)
{
	struct _pcie_device *ret;

	assert_spin_locked(&ioc->pcie_device_lock);

	ret = tgt_priv->pcie_dev;
	if (ret)
		pcie_device_get(ret);

	return ret;
}

/**
 * mpt3sas_get_pdev_from_target - pcie device search
 * @ioc: per adapter object
 * @tgt_priv: starget private object
 *
 * Context: This function will acquire ioc->pcie_device_lock and will release
 * before returning the pcie_device object.
 *
 * This searches for pcie_device from target, then return pcie_device object.
 */
struct _pcie_device *
mpt3sas_get_pdev_from_target(struct MPT3SAS_ADAPTER *ioc,
	struct MPT3SAS_TARGET *tgt_priv)
{
	struct _pcie_device *ret;
	unsigned long flags;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	ret = __mpt3sas_get_pdev_from_target(ioc, tgt_priv);
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	return ret;
}

/**
 * __mpt3sas_get_sdev_by_addr - sas device search
 * @ioc: per adapter object
 * @sas_address: sas address
 * @port: hba port entry
 *
 * Context: This function will acquire ioc->sas_device_lock and will release
 * before returning the sas_device object.
 *
 * This searches for sas_device from sas adress and port number
 * then return sas_device object.
 */
struct _sas_device *
__mpt3sas_get_sdev_by_addr(struct MPT3SAS_ADAPTER *ioc,
	u64 sas_address, struct hba_port *port)
{
	struct _sas_device *sas_device;

	if (!port)
		return NULL;

	assert_spin_locked(&ioc->sas_device_lock);

	list_for_each_entry(sas_device, &ioc->sas_device_list, list)
		if (sas_device->sas_address == sas_address &&
		    sas_device->port == port)
			goto found_device;

	list_for_each_entry(sas_device, &ioc->sas_device_init_list, list)
		if (sas_device->sas_address == sas_address &&
		    sas_device->port == port)
			goto found_device;

	return NULL;

found_device:
	sas_device_get(sas_device);
	return sas_device;
}

/**
 * __mpt3sas_get_sdev_by_addr_on_rphy - sas device search
 * @ioc: per adapter object
 * @sas_address: sas address
 * @rphy: sas_rphy pointer
 *
 * Context: This function will acquire ioc->sas_device_lock and will release
 * before returning the sas_device object.
 *
 * This searches for sas_device from sas adress and rphy pointer
 * then return sas_device object.
 */
struct _sas_device *
__mpt3sas_get_sdev_by_addr_and_rphy(struct MPT3SAS_ADAPTER *ioc,
	u64 sas_address, struct sas_rphy *rphy)
{
	struct _sas_device *sas_device;

	assert_spin_locked(&ioc->sas_device_lock);

	list_for_each_entry(sas_device, &ioc->sas_device_list, list)
		if (sas_device->sas_address == sas_address &&
		   (sas_device->rphy == rphy))
			goto found_device;

	list_for_each_entry(sas_device, &ioc->sas_device_init_list, list)
		if (sas_device->sas_address == sas_address &&
		   (sas_device->rphy == rphy))
			goto found_device;

	return NULL;

found_device:
	sas_device_get(sas_device);
	return sas_device;
}

/**
 * mpt3sas_get_sdev_by_addr - sas device search
 * @ioc: per adapter object
 * @sas_address: sas address
 * @port: hba port entry
 *
 * Context: This function will acquire ioc->sas_device_lock and will release
 * before returning the sas_device object.
 *
 * This searches for sas_device based on sas_address & port number,
 * then return sas_device object.
 */
struct _sas_device *
mpt3sas_get_sdev_by_addr(struct MPT3SAS_ADAPTER *ioc,
	u64 sas_address, struct hba_port *port)
{
	struct _sas_device *sas_device = NULL;
	unsigned long flags;

	if(!port)
		return sas_device;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_addr(ioc,
			sas_address, port);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	return sas_device;
}

static struct _sas_device *
__mpt3sas_get_sdev_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_device *sas_device;

	assert_spin_locked(&ioc->sas_device_lock);

	list_for_each_entry(sas_device, &ioc->sas_device_list, list)
		if (sas_device->handle == handle)
			goto found_device;

	list_for_each_entry(sas_device, &ioc->sas_device_init_list, list)
		if (sas_device->handle == handle)
			goto found_device;

	return NULL;

found_device:
	sas_device_get(sas_device);
	return sas_device;
}

/**
 * mpt3sas_get_sdev_by_handle - sas device search
 * @ioc: per adapter object
 * @handle: sas device handle (assigned by firmware)
 *
 * Context: This function will acquire ioc->sas_device_lock and will release
 * before returning the sas_device object.
 *
 * This searches for sas_device based on sas_address, then return sas_device
 * object.
 */
struct _sas_device *
mpt3sas_get_sdev_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_device *sas_device;
	unsigned long flags;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_handle(ioc, handle);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	return sas_device;
}

/**
 * _scsih_sas_device_remove - remove sas_device from list.
 * @ioc: per adapter object
 * @sas_device: the sas_device object
 * Context: This function will acquire ioc->sas_device_lock.
 *
 * If sas_device is on the list, remove it and decrement its reference count.
 */
void
_scsih_sas_device_remove(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_device *sas_device)
{
	unsigned long flags;
	int was_on_sas_device_list = 0;

	if (!sas_device)
		return;
	printk(MPT3SAS_INFO_FMT "%s: removing handle(0x%04x), sas_addr"
	    "(0x%016llx)\n", ioc->name, __func__, sas_device->handle,
	    (unsigned long long) sas_device->sas_address);

	_scsih_display_enclosure_chassis_info(ioc, sas_device, NULL, NULL);

	/*
	 * The lock serializes access to the list, but we still need to verify
	 * that nobody removed the entry while we were waiting on the lock.
	 */

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	if (!list_empty(&sas_device->list)) {
		list_del_init(&sas_device->list);
		was_on_sas_device_list = 1;
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (was_on_sas_device_list) {
		kfree(sas_device->serial_number);
		sas_device_put(sas_device);
	}
}

/**
 * _scsih_device_remove_by_handle - removing device object by handle
 * @ioc: per adapter object
 * @handle: device handle
 *
 * Return nothing.
 */
static void
_scsih_device_remove_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_device *sas_device;
	unsigned long flags;
	int was_on_sas_device_list = 0;

	if (ioc->shost_recovery)
		return;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_handle(ioc, handle);
	if (sas_device) {
		if (!list_empty(&sas_device->list)) {
			list_del_init(&sas_device->list);
        		was_on_sas_device_list = 1;
			sas_device_put(sas_device);
		}
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (was_on_sas_device_list) {
		_scsih_remove_device(ioc, sas_device);
		sas_device_put(sas_device);
	}
}

/**
 * mpt3sas_device_remove_by_sas_address - removing device object by
 *				 sas address & port number
 * @ioc: per adapter object
 * @sas_address: device sas_address
 * @port: hba port entry
 *
 * Return nothing.
 */
void
mpt3sas_device_remove_by_sas_address(struct MPT3SAS_ADAPTER *ioc,
	u64 sas_address, struct hba_port *port)
{
	struct _sas_device *sas_device;
	unsigned long flags;
	int was_on_sas_device_list = 0;

	if (ioc->shost_recovery)
		return;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_addr(ioc,
			 sas_address, port);
	if (sas_device) {
		if (!list_empty(&sas_device->list)) {
			list_del_init(&sas_device->list);
			was_on_sas_device_list = 1;
			sas_device_put(sas_device);
		}
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (was_on_sas_device_list) {
		_scsih_remove_device(ioc, sas_device);
		sas_device_put(sas_device);
	}
}

/**
 * _scsih_sas_device_add - insert sas_device to the list.
 * @ioc: per adapter object
 * @sas_device: the sas_device object
 * Context: This function will acquire ioc->sas_device_lock.
 *
 * Adding new object to the ioc->sas_device_list.
 */
static void
_scsih_sas_device_add(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_device *sas_device)
{
	unsigned long flags;

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: handle"
	    "(0x%04x), sas_addr(0x%016llx)\n", ioc->name, __func__,
	    sas_device->handle, (unsigned long long)sas_device->sas_address));

	dewtprintk(ioc,	_scsih_display_enclosure_chassis_info(ioc, sas_device,
	    NULL, NULL));

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device_get(sas_device);
	list_add_tail(&sas_device->list, &ioc->sas_device_list);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	if (ioc->hide_drives) {
		clear_bit(sas_device->handle, ioc->pend_os_device_add);
		return;
	}

	if (!mpt3sas_transport_port_add(ioc, sas_device->handle,
	     sas_device->sas_address_parent, sas_device->port)) {
		_scsih_sas_device_remove(ioc, sas_device);
	} else if (!sas_device->starget) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
		/* CQ 206770:
		 * When asyn scanning is enabled, its not possible to remove
		 * devices while scanning is turned on due to an oops in
		 * scsi_sysfs_add_sdev()->add_device()->sysfs_addrm_start()
		 */
		if (!ioc->is_driver_loading) {
#endif
			mpt3sas_transport_port_remove(ioc,
			    sas_device->sas_address,
			    sas_device->sas_address_parent,
			    sas_device->port);
			_scsih_sas_device_remove(ioc, sas_device);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
		}
#endif
	} else
		clear_bit(sas_device->handle, ioc->pend_os_device_add);
}

/**
 * _scsih_sas_device_init_add - insert sas_device to the list.
 * @ioc: per adapter object
 * @sas_device: the sas_device object
 * Context: This function will acquire ioc->sas_device_lock.
 *
 * Adding new object at driver load time to the ioc->sas_device_init_list.
 */
static void
_scsih_sas_device_init_add(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_device *sas_device)
{
	unsigned long flags;

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: handle"
	    "(0x%04x), sas_addr(0x%016llx)\n", ioc->name, __func__,
	    sas_device->handle, (unsigned long long)sas_device->sas_address));

	dewtprintk(ioc,	_scsih_display_enclosure_chassis_info(ioc, sas_device,
	    NULL, NULL));

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device_get(sas_device);
	list_add_tail(&sas_device->list, &ioc->sas_device_init_list);
	_scsih_determine_boot_device(ioc, sas_device, 0);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
}



struct _pcie_device *
__mpt3sas_get_pdev_by_wwid(struct MPT3SAS_ADAPTER *ioc, u64 wwid)
{
	struct _pcie_device *pcie_device;

	assert_spin_locked(&ioc->pcie_device_lock);

	list_for_each_entry(pcie_device, &ioc->pcie_device_list, list)
		if (pcie_device->wwid == wwid)
			goto found_device;

	list_for_each_entry(pcie_device, &ioc->pcie_device_init_list, list)
		if (pcie_device->wwid == wwid)
			goto found_device;

	return NULL;

found_device:
	pcie_device_get(pcie_device);
	return pcie_device;
}


/**
 * mpt3sas_get_pdev_by_wwid - pcie device search
 * @ioc: per adapter object
 * @wwid: wwid
 *
 * Context: This function will acquire ioc->pcie_device_lock and will release
 * before returning the pcie_device object.
 *
 * This searches for pcie_device based on wwid, then return pcie_device object.
 */
struct _pcie_device *
mpt3sas_get_pdev_by_wwid(struct MPT3SAS_ADAPTER *ioc, u64 wwid)
{
	struct _pcie_device *pcie_device;
	unsigned long flags;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	pcie_device = __mpt3sas_get_pdev_by_wwid(ioc, wwid);
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	return pcie_device;
}


struct _pcie_device *
__mpt3sas_get_pdev_by_idchannel(struct MPT3SAS_ADAPTER *ioc, int id,
	int channel)
{
	struct _pcie_device *pcie_device;

	assert_spin_locked(&ioc->pcie_device_lock);

	list_for_each_entry(pcie_device, &ioc->pcie_device_list, list)
		if (pcie_device->id == id && pcie_device->channel == channel)
			goto found_device;

	list_for_each_entry(pcie_device, &ioc->pcie_device_init_list, list)
		if (pcie_device->id == id && pcie_device->channel == channel)
			goto found_device;

	return NULL;

found_device:
	pcie_device_get(pcie_device);
	return pcie_device;
}


/**
 * mpt3sas_get_pdev_by_idchannel - pcie device search
 * @ioc: per adapter object
 * @id: Target ID
 * @channel: Channel ID
 *
 * Context: This function will acquire ioc->pcie_device_lock and will release
 * before returning the pcie_device object.
 *
 * This searches for pcie_device based on id and channel, then return
 * pcie_device object.
 */
struct _pcie_device *
mpt3sas_get_pdev_by_idchannel(struct MPT3SAS_ADAPTER *ioc, int id, int channel)
{
	struct _pcie_device *pcie_device;
	unsigned long flags;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	pcie_device = __mpt3sas_get_pdev_by_idchannel(ioc, id, channel);
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	return pcie_device;
}


struct _pcie_device *
__mpt3sas_get_pdev_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _pcie_device *pcie_device;

	assert_spin_locked(&ioc->pcie_device_lock);

	list_for_each_entry(pcie_device, &ioc->pcie_device_list, list)
		if (pcie_device->handle == handle)
			goto found_device;

	list_for_each_entry(pcie_device, &ioc->pcie_device_init_list, list)
		if (pcie_device->handle == handle)
			goto found_device;

	return NULL;

found_device:
	pcie_device_get(pcie_device);
	return pcie_device;
}


/**
 * mpt3sas_get_pdev_by_handle - pcie device search
 * @ioc: per adapter object
 * @handle: Firmware device handle
 *
 * Context: This function will acquire ioc->pcie_device_lock and will release
 * before returning the pcie_device object.
 *
 * This searches for pcie_device based on handle, then return pcie_device
 * object.
 */
struct _pcie_device *
mpt3sas_get_pdev_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _pcie_device *pcie_device;
	unsigned long flags;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	pcie_device = __mpt3sas_get_pdev_by_handle(ioc, handle);
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	return pcie_device;
}

/**
 * _scsih_set_nvme_max_shutdown_latency - Update max_shutdown_latency.
 * @ioc: per adapter object
 * Context: This function will acquire ioc->pcie_device_lock
 *
 * Update ioc->max_shutdown_latency by checking available devices.
 */
static void
_scsih_set_nvme_max_shutdown_latency(struct MPT3SAS_ADAPTER *ioc)
{
	struct _pcie_device *pcie_device;
	unsigned long flags;
	u16 shutdown_latency = IO_UNIT_CONTROL_SHUTDOWN_TIMEOUT;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	list_for_each_entry(pcie_device, &ioc->pcie_device_list, list) {
		if (pcie_device->shutdown_latency) {
			if (shutdown_latency < pcie_device->shutdown_latency)
				shutdown_latency =
					pcie_device->shutdown_latency;
		}
	}
	ioc->max_shutdown_latency = shutdown_latency;
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
}

/**
 * _scsih_pcie_device_remove - remove pcie_device from list.
 * @ioc: per adapter object
 * @pcie_device: the pcie_device object
 * Context: This function will acquire ioc->pcie_device_lock.
 *
 * If pcie_device is on the list, remove it and decrement its reference count.
 */
static void
_scsih_pcie_device_remove(struct MPT3SAS_ADAPTER *ioc,
       struct _pcie_device *pcie_device)
{
	unsigned long flags;
	int was_on_pcie_device_list = 0;
	u8 update_latency = 0;

	if (!pcie_device)
		return;
	printk(MPT3SAS_INFO_FMT "removing handle(0x%04x), wwid"
		"(0x%016llx)\n", ioc->name, pcie_device->handle,
		(unsigned long long) pcie_device->wwid);
	if(pcie_device->enclosure_handle != 0)
		printk(MPT3SAS_INFO_FMT "removing "
			"enclosure logical id(0x%016llx), slot(%d) \n",
			ioc->name, (unsigned long long)pcie_device->enclosure_logical_id,
			pcie_device->slot);
	if(pcie_device->connector_name[0] != '\0')
		printk(MPT3SAS_INFO_FMT "removing  "
			"enclosure level(0x%04x), connector name( %s)\n",
			ioc->name, pcie_device->enclosure_level,
			pcie_device->connector_name);

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	if (!list_empty(&pcie_device->list)) {
		list_del_init(&pcie_device->list);
		was_on_pcie_device_list = 1;
	}

	if (pcie_device->shutdown_latency == ioc->max_shutdown_latency)
		update_latency = 1;

	if (was_on_pcie_device_list) {
		kfree(pcie_device->serial_number);
		pcie_device_put(pcie_device);
	}
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	if (update_latency)
		_scsih_set_nvme_max_shutdown_latency(ioc);

}

/**
 * _scsih_pcie_device_remove_by_handle - removing pcie device object by handle
 * @ioc: per adapter object
 * @handle: device handle
 *
 * Return nothing.
 */
static void
_scsih_pcie_device_remove_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _pcie_device *pcie_device;
	unsigned long flags;
	int was_on_pcie_device_list = 0;
	u8 update_latency = 0;

	if (ioc->shost_recovery)
		return;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	pcie_device = __mpt3sas_get_pdev_by_handle(ioc, handle);
	if (pcie_device) {
		if (!list_empty(&pcie_device->list)) {
			list_del_init(&pcie_device->list);
			was_on_pcie_device_list = 1;
			pcie_device_put(pcie_device);
		}
		if (pcie_device->shutdown_latency == ioc->max_shutdown_latency)
			update_latency = 1;
	}
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
	if (was_on_pcie_device_list) {
		_scsih_pcie_device_remove_from_sml(ioc, pcie_device);
		pcie_device_put(pcie_device);
	}

	/* If max shutwown latency is from this device, then update
	 * ioc->max_shutdown_latency by iterating over the list to
	 * find max value after removing the device from list.
	 */
	if (update_latency)
		_scsih_set_nvme_max_shutdown_latency(ioc);
}

/**
 * _scsih_pcie_device_remove_by_wwid - removing device object by
 * wwid
 * @ioc: per adapter object
 * @wwid: pcie device wwid
 *
 * Return nothing.
 */
static void
_scsih_pcie_device_remove_by_wwid(struct MPT3SAS_ADAPTER *ioc, u64 wwid)
{
	struct _pcie_device *pcie_device;
	unsigned long flags;
	int was_on_pcie_device_list = 0;
	u8 update_latency = 0;

	if (ioc->shost_recovery)
		return;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	pcie_device = __mpt3sas_get_pdev_by_wwid(ioc, wwid);
	if (pcie_device) {
		if (!list_empty(&pcie_device->list)) {
			list_del_init(&pcie_device->list);
			was_on_pcie_device_list = 1;
			pcie_device_put(pcie_device);
		}
		if (pcie_device->shutdown_latency == ioc->max_shutdown_latency)
			update_latency = 1;
	}
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
	if (was_on_pcie_device_list) {
		_scsih_pcie_device_remove_from_sml(ioc, pcie_device);
		pcie_device_put(pcie_device);
	}
	if (update_latency)
		_scsih_set_nvme_max_shutdown_latency(ioc);
}

/**
 * _scsih_pcie_device_add - add pcie_device object
 * @ioc: per adapter object
 * @pcie_device: pcie_device object
 *
 * This is added to the pcie_device_list link list.
 */
static void
_scsih_pcie_device_add(struct MPT3SAS_ADAPTER *ioc,
	struct _pcie_device *pcie_device)
{
	unsigned long flags;

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: handle"
		"(0x%04x), wwid(0x%016llx)\n", ioc->name, __func__,
		pcie_device->handle, (unsigned long long)pcie_device->wwid));
	if(pcie_device->enclosure_handle != 0)
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
			"%s: enclosure logical id(0x%016llx), slot( %d)\n", ioc->name,
			__func__, (unsigned long long)pcie_device->enclosure_logical_id,
			pcie_device->slot));
	if(pcie_device->connector_name[0] != '\0')
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: enclosure level(0x%04x), "
			"connector name( %s)\n", ioc->name, __func__,
			pcie_device->enclosure_level, pcie_device->connector_name));

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	pcie_device_get(pcie_device);
	list_add_tail(&pcie_device->list, &ioc->pcie_device_list);
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
	
	if (pcie_device->access_status ==
			MPI26_PCIEDEV0_ASTATUS_DEVICE_BLOCKED) {
		clear_bit(pcie_device->handle, ioc->pend_os_device_add);
		return;
	}
	if (scsi_add_device(ioc->shost, PCIE_CHANNEL, pcie_device->id, 0)) {
		_scsih_pcie_device_remove(ioc, pcie_device);
	} else if (!pcie_device->starget) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
		if (!ioc->is_driver_loading) {
#endif
/*TODO-- Need to find out whether this condition will occur or not*/
			clear_bit(pcie_device->handle, ioc->pend_os_device_add);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
		}
#endif
	} else
		clear_bit(pcie_device->handle, ioc->pend_os_device_add);
}

/**
* _scsih_pcie_device_init_add - insert pcie_device to the init list.
* @ioc: per adapter object
* @pcie_device: the pcie_device object
* Context: This function will acquire ioc->pcie_device_lock.
*
* Adding new object at driver load time to the ioc->pcie_device_init_list.
*/
static void
_scsih_pcie_device_init_add(struct MPT3SAS_ADAPTER *ioc,
    struct _pcie_device *pcie_device)
{
	unsigned long flags;

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: handle"
		"(0x%04x), wwid(0x%016llx)\n", ioc->name, __func__,
		pcie_device->handle, (unsigned long long)pcie_device->wwid));
	if(pcie_device->enclosure_handle != 0)
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
			"%s: enclosure logical id(0x%016llx), slot( %d)\n", ioc->name,
			__func__, (unsigned long long)pcie_device->enclosure_logical_id,
			pcie_device->slot));
	if(pcie_device->connector_name[0] != '\0')
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: enclosure level(0x%04x), "
			"connector name( %s)\n", ioc->name, __func__,
			pcie_device->enclosure_level, pcie_device->connector_name));

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	pcie_device_get(pcie_device);
	list_add_tail(&pcie_device->list, &ioc->pcie_device_init_list);
	if (pcie_device->access_status !=
			MPI26_PCIEDEV0_ASTATUS_DEVICE_BLOCKED)
		_scsih_determine_boot_device(ioc, pcie_device, PCIE_CHANNEL);
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
}

/**
 * _scsih_raid_device_find_by_id - raid device search
 * @ioc: per adapter object
 * @id: sas device target id
 * @channel: sas device channel
 * Context: Calling function should acquire ioc->raid_device_lock
 *
 * This searches for raid_device based on target id, then return raid_device
 * object.
 */
static struct _raid_device *
_scsih_raid_device_find_by_id(struct MPT3SAS_ADAPTER *ioc, int id, int channel)
{
	struct _raid_device *raid_device, *r;

	r = NULL;
	list_for_each_entry(raid_device, &ioc->raid_device_list, list) {
		if (raid_device->id == id && raid_device->channel == channel) {
			r = raid_device;
			goto out;
		}
	}

 out:
	return r;
}

/**
 * mpt3sas_raid_device_find_by_handle - raid device search
 * @ioc: per adapter object
 * @handle: sas device handle (assigned by firmware)
 * Context: Calling function should acquire ioc->raid_device_lock
 *
 * This searches for raid_device based on handle, then return raid_device
 * object.
 */
struct _raid_device *
mpt3sas_raid_device_find_by_handle(struct MPT3SAS_ADAPTER *ioc, u16
handle)
{
	struct _raid_device *raid_device, *r;

	r = NULL;
	list_for_each_entry(raid_device, &ioc->raid_device_list, list) {
		if (raid_device->handle != handle)
			continue;
		r = raid_device;
		goto out;
	}

 out:
	return r;
}

/**
 * _scsih_raid_device_find_by_wwid - raid device search
 * @ioc: per adapter object
 * @handle: sas device handle (assigned by firmware)
 * Context: Calling function should acquire ioc->raid_device_lock
 *
 * This searches for raid_device based on wwid, then return raid_device
 * object.
 */
static struct _raid_device *
_scsih_raid_device_find_by_wwid(struct MPT3SAS_ADAPTER *ioc, u64 wwid)
{
	struct _raid_device *raid_device, *r;

	r = NULL;
	list_for_each_entry(raid_device, &ioc->raid_device_list, list) {
		if (raid_device->wwid != wwid)
			continue;
		r = raid_device;
		goto out;
	}

 out:
	return r;
}

/**
 * _scsih_raid_device_add - add raid_device object
 * @ioc: per adapter object
 * @raid_device: raid_device object
 *
 * This is added to the raid_device_list link list.
 */
static void
_scsih_raid_device_add(struct MPT3SAS_ADAPTER *ioc,
	struct _raid_device *raid_device)
{
	unsigned long flags;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
	u8 protection_mask;
#endif
	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: handle"
	    "(0x%04x), wwid(0x%016llx)\n", ioc->name, __func__,
	    raid_device->handle, (unsigned long long)raid_device->wwid));

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	list_add_tail(&raid_device->list, &ioc->raid_device_list);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
	if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
		if (!ioc->disable_eedp_support) {
		/* Disable DIX0 protection capability */
			protection_mask = scsi_host_get_prot(ioc->shost);
			if (protection_mask & SHOST_DIX_TYPE0_PROTECTION) {
				scsi_host_set_prot(ioc->shost, protection_mask & 0x77);
				printk(MPT3SAS_INFO_FMT ": Disabling DIX0 prot capability because HBA does"
					"not support DIX0 operation on volumes\n",ioc->name);
			}
		}
	}
#endif

	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
}

/**
 * _scsih_raid_device_remove - delete raid_device object
 * @ioc: per adapter object
 * @raid_device: raid_device object
 *
 */
static void
_scsih_raid_device_remove(struct MPT3SAS_ADAPTER *ioc,
	struct _raid_device *raid_device)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	list_del(&raid_device->list);
	kfree(raid_device);
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
}

/**
 * mpt3sas_scsih_expander_find_by_handle - expander device search
 * @ioc: per adapter object
 * @handle: expander handle (assigned by firmware)
 * Context: Calling function should acquire ioc->sas_device_lock
 *
 * This searches for expander device based on handle, then returns the
 * sas_node object.
 */
struct _sas_node *
mpt3sas_scsih_expander_find_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_node *sas_expander, *r;

	r = NULL;
	list_for_each_entry(sas_expander, &ioc->sas_expander_list, list) {
		if (sas_expander->handle != handle)
			continue;
		r = sas_expander;
		goto out;
	}
 out:
	return r;
}

/**
 * mpt3sas_scsih_enclosure_find_by_handle - exclosure device search
 * @ioc: per adapter object
 * @handle: enclosure handle (assigned by firmware)
 * Context: Calling function should acquire ioc->sas_device_lock
 *
 * This searches for enclosure device based on handle, then returns the
 * enclosure object.
 */
struct _enclosure_node *
mpt3sas_scsih_enclosure_find_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _enclosure_node *enclosure_dev, *r;
	r = NULL;

	list_for_each_entry(enclosure_dev, &ioc->enclosure_list, list) {
		if (le16_to_cpu(enclosure_dev->pg0.EnclosureHandle) != handle)
			continue;
		r = enclosure_dev;
		goto out;
	}
out:
	return r;
}

/**
 * TODO: search for pcie switch
 * mpt3sas_scsih_switch_find_by_handle - pcie switch search
 * @ioc: per adapter object
 * @handle: switch handle (assigned by firmware)
 * Context: Calling function should acquire ioc->sas_node_lock
 *
 * This searches for switch device based on handle, then returns the
 * sas_node object.
 */
struct _sas_node *
mpt3sas_scsih_switch_find_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
       printk(MPT3SAS_ERR_FMT "%s is not yet implemented",
              ioc->name, __func__);
       return NULL;
}

/**
 * mpt3sas_scsih_expander_find_by_sas_address - expander device search
 * @ioc: per adapter object
 * @sas_address: sas address
 * @port: hba port entry
 * Context: Calling function should acquire ioc->sas_node_lock.
 *
 * This searches for expander device based on sas_address & port number,
 * then returns the sas_node object.
 */
struct _sas_node *
mpt3sas_scsih_expander_find_by_sas_address(struct MPT3SAS_ADAPTER *ioc,
	u64 sas_address, struct hba_port *port)
{
	struct _sas_node *sas_expander, *r;
	r = NULL;
	if (!port)
		return r;

	list_for_each_entry(sas_expander, &ioc->sas_expander_list, list) {
		if (sas_expander->sas_address != sas_address ||
					 sas_expander->port != port)
			continue;
		r = sas_expander;
		goto out;
	}
 out:
	return r;
}

/**
 * _scsih_expander_node_add - insert expander device to the list.
 * @ioc: per adapter object
 * @sas_expander: the sas_device object
 * Context: This function will acquire ioc->sas_node_lock.
 *
 * Adding new object to the ioc->sas_expander_list.
 *
 * Return nothing.
 */
static void
_scsih_expander_node_add(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_node *sas_expander)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	list_add_tail(&sas_expander->list, &ioc->sas_expander_list);
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
}

/**
 * _scsih_is_sas_end_device - determines if device is an end device
 * @device_info: bitfield providing information about the device.
 * Context: none
 *
 * Returns 1 if the device is SAS/SATA/STP end device.
 */
static int
_scsih_is_sas_end_device(u32 device_info)
{
	if (device_info & MPI2_SAS_DEVICE_INFO_END_DEVICE &&
		((device_info & MPI2_SAS_DEVICE_INFO_SSP_TARGET) |
		(device_info & MPI2_SAS_DEVICE_INFO_STP_TARGET) |
		(device_info & MPI2_SAS_DEVICE_INFO_SATA_DEVICE)))
		return 1;
	else
		return 0;
}

/**
 * _scsih_is_nvme_pciescsi_device - determines if device is an pcie nvme/scsi device
 * @device_info: bitfield providing information about the device.
 * Context: none
 *
 * Returns 1 if device is pcie device type nvme/scsi.
 */
static int
_scsih_is_nvme_pciescsi_device(u32 device_info)
{
	if (((device_info & MPI26_PCIE_DEVINFO_MASK_DEVICE_TYPE)
		== MPI26_PCIE_DEVINFO_NVME) || 
		((device_info & MPI26_PCIE_DEVINFO_MASK_DEVICE_TYPE)
		 == MPI26_PCIE_DEVINFO_SCSI))
		return 1;
	else
		return 0;
}

/**
 * _scsih_scsi_lookup_find_by_target - search for matching channel:id
 * @ioc: per adapter object
 * @id: target id
 * @channel: channel
 * Context: This function will acquire ioc->scsi_lookup_lock.
 *
 * This will search for a matching channel:id in the scsi_lookup array,
 * returning 1 if found.
 */
static u8
_scsih_scsi_lookup_find_by_target(struct MPT3SAS_ADAPTER *ioc, int id,
	int channel)
{
	int smid;
	struct scsi_cmnd *scmd;

	for (smid = 1;
	     smid <= ioc->shost->can_queue; smid++) {
		scmd = mpt3sas_scsih_scsi_lookup_get(ioc, smid);
		if (!scmd)
			continue;
		if (scmd->device->id == id &&
		    scmd->device->channel == channel)
			return 1;
	}
	return 0;
}

/**
 * _scsih_scsi_lookup_find_by_lun - search for matching channel:id:lun
 * @ioc: per adapter object
 * @id: target id
 * @lun: lun number
 * @channel: channel
 * Context: This function will acquire ioc->scsi_lookup_lock.
 *
 * This will search for a matching channel:id:lun in the scsi_lookup array,
 * returning 1 if found.
 */
static u8
_scsih_scsi_lookup_find_by_lun(struct MPT3SAS_ADAPTER *ioc, int id,
	unsigned int lun, int channel)
{
	int smid;
	struct scsi_cmnd *scmd;

	for (smid = 1; smid <= ioc->shost->can_queue; smid++) {

		scmd = mpt3sas_scsih_scsi_lookup_get(ioc, smid);
		if (!scmd)
			continue;
		if (scmd->device->id == id &&
		    scmd->device->channel == channel &&
		    scmd->device->lun == lun)
			return 1;
	}
	return 0;
}

/**
 * mpt3sas_scsih_scsi_lookup_get - returns scmd entry
 * @ioc: per adapter object
 * @smid: system request message index

 * Returns the smid stored scmd pointer.
 * Then will dereference the stored scmd pointer.
 */
struct scsi_cmnd *
mpt3sas_scsih_scsi_lookup_get(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	struct scsi_cmnd *scmd = NULL;
	struct scsiio_tracker *st;
	Mpi25SCSIIORequest_t *mpi_request;

	if (smid > 0  &&
		smid <= ioc->shost->can_queue) {
		mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
		/*
		 * If SCSI IO request is outstanding at driver level then
		 * DevHandle filed must be non-zero. If DevHandle is zero
		 * then it means that this smid is free at driver level,
		 * so return NULL.
		 */
		if (!mpi_request->DevHandle)
			return scmd;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19))
		scmd = scsi_host_find_tag(ioc->shost, smid - 1);
#else

		scmd = ioc->scsi_lookup[smid -1].scmd;
#endif
		if (scmd) {
			st = mpt3sas_base_scsi_cmd_priv(scmd);
			if ((!st) || (st->cb_idx == 0xFF) || (st->smid == 0))
				scmd = NULL;
		}
	}
	return scmd;
}


static void
_scsih_display_sdev_qd(struct scsi_device *sdev)
{
	if (sdev->inquiry_len <= 7)
		return;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0))
	sdev_printk(KERN_INFO, sdev,
	    "qdepth(%d), tagged(%d), simple(%d), ordered(%d), scsi_level(%d), cmd_que(%d)\n",
	    sdev->queue_depth, ((sdev->inquiry[7] & 2) >> 1), sdev->simple_tags,
	    sdev->ordered_tags, sdev->scsi_level, (sdev->inquiry[7] & 2) >> 1);
#else
	sdev_printk(KERN_INFO, sdev,
	    "qdepth(%d), tagged(%d), scsi_level(%d), cmd_que(%d)\n",
	    sdev->queue_depth, sdev->tagged_supported,
	    sdev->scsi_level, ((sdev->inquiry[7] & 2) >> 1));
#endif
}


#if (defined(RAMP_UP_SUPPORT) && (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)))
static void
_scsih_adjust_queue_depth(struct scsi_device *sdev, int qdepth)
{
#else
static int
_scsih_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
#endif

	struct Scsi_Host *shost = sdev->host;
	int max_depth;
	struct MPT3SAS_ADAPTER *ioc = shost_private(shost);
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
	unsigned long flags;

	max_depth = shost->can_queue;

	/*
	 * limit max device queue for SATA to 32 if enable_sdev_max_qd
	 * is disabled.
	 */
	if (ioc->enable_sdev_max_qd)
		goto not_sata;

	sas_device_priv_data = sdev->hostdata;
	if (!sas_device_priv_data)
		goto not_sata;
	sas_target_priv_data = sas_device_priv_data->sas_target;
	if (!sas_target_priv_data)
		goto not_sata;
	if ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME))
		goto not_sata;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_from_target(ioc, sas_target_priv_data);
	
	if (sas_device) {
		if (sas_device->device_info & MPI2_SAS_DEVICE_INFO_SATA_DEVICE)
			max_depth = MPT3SAS_SATA_QUEUE_DEPTH;

		sas_device_put(sas_device);
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

 not_sata:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0))
	/*
	 * if sdev INQUIRY data shows that CmdQue is zero then set sdev QD as one.
	 */
	if (!sdev->tagged_supported ||
	    !((sdev->scsi_level >= SCSI_2) && (sdev->inquiry_len > 7) &&
				(sdev->inquiry[7] & 2)))
		max_depth = 1;
#else
	if (!sdev->tagged_supported)
		max_depth = 1;
#endif
	if (qdepth > max_depth)
		qdepth = max_depth;
#if (defined(RAMP_UP_SUPPORT) && (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)))
	scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev), qdepth);
	_scsih_display_sdev_qd(sdev);
#elif (!defined(RAMP_UP_SUPPORT) && (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)))
	scsi_adjust_queue_depth(sdev, ((qdepth == 1) ? 0 : MSG_SIMPLE_TAG),
								     qdepth);
	_scsih_display_sdev_qd(sdev);
	return sdev->queue_depth;
#else
	scsi_change_queue_depth(sdev, qdepth);
	_scsih_display_sdev_qd(sdev);
	return sdev->queue_depth;
#endif
}

#if (defined(RAMP_UP_SUPPORT) && (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)))
/**
 * _scsih_change_queue_depth - setting device queue depth
 * @sdev: scsi device struct
 * @qdepth: requested queue depth
 * @reason: SCSI_QDEPTH_DEFAULT/SCSI_QDEPTH_QFULL/SCSI_QDEPTH_RAMP_UP
 * (see include/scsi/scsi_host.h for definition)
 *
 * Returns queue depth.
 */
static int
_scsih_change_queue_depth(struct scsi_device *sdev, int qdepth, int reason)
{
	if (reason == SCSI_QDEPTH_DEFAULT || reason == SCSI_QDEPTH_RAMP_UP)
		_scsih_adjust_queue_depth(sdev, qdepth);
	else if (reason == SCSI_QDEPTH_QFULL)
		scsi_track_queue_full(sdev, qdepth);
	else
		return -EOPNOTSUPP;

	return sdev->queue_depth;
}
#endif

/**
 * mpt3sas_scsih_change_queue_depth - setting device queue depth
 * @sdev: scsi device struct
 * @qdepth: requested queue depth
 *
 * Returns nothing.
 */
void
mpt3sas_scsih_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
	struct Scsi_Host *shost = sdev->host;
	struct MPT3SAS_ADAPTER *ioc = shost_private(shost);

	if (ioc->enable_sdev_max_qd)
		qdepth = shost->can_queue;

#if (defined(RAMP_UP_SUPPORT) && (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0)))
	_scsih_change_queue_depth(sdev, qdepth, SCSI_QDEPTH_DEFAULT);
#else
	_scsih_change_queue_depth(sdev, qdepth);
#endif
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0))
/**
 * _scsih_change_queue_type - changing device queue tag type
 * @sdev: scsi device struct
 * @tag_type: requested tag type
 *
 * Returns queue tag type.
 */
static int
_scsih_change_queue_type(struct scsi_device *sdev, int tag_type)
{
	if (sdev->tagged_supported) {
		scsi_set_tag_type(sdev, tag_type);
		if (tag_type)
			scsi_activate_tcq(sdev, sdev->queue_depth);
		else
			scsi_deactivate_tcq(sdev, sdev->queue_depth);
	} else
		tag_type = 0;

	return tag_type;
}
#endif

/**
 * _scsih_target_alloc - target add routine
 * @starget: scsi target struct
 *
 * Returns 0 if ok. Any other return is assumed to be an error and
 * the device is ignored.
 */
static int
_scsih_target_alloc(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct MPT3SAS_ADAPTER *ioc = shost_private(shost);
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
	struct _raid_device *raid_device;
	struct _pcie_device *pcie_device;
	unsigned long flags;
	struct sas_rphy *rphy;

	sas_target_priv_data = kzalloc(sizeof(struct MPT3SAS_TARGET), GFP_KERNEL);
	if (!sas_target_priv_data)
		return -ENOMEM;

	starget->hostdata = sas_target_priv_data;
	sas_target_priv_data->starget = starget;
	sas_target_priv_data->handle = MPT3SAS_INVALID_DEVICE_HANDLE;

	/* RAID volumes */
	if (starget->channel == RAID_CHANNEL) {
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_id(ioc, starget->id,
		    starget->channel);
		if (raid_device) {
			sas_target_priv_data->handle = raid_device->handle;
			sas_target_priv_data->sas_address = raid_device->wwid;
			sas_target_priv_data->flags |= MPT_TARGET_FLAGS_VOLUME;
			if (ioc->is_warpdrive)
				sas_target_priv_data->raid_device = raid_device;
			raid_device->starget = starget;
		}
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
		return 0;
	}

	/* PCIe devices */
	if (starget->channel == PCIE_CHANNEL) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = __mpt3sas_get_pdev_by_idchannel(ioc, starget->id,
			starget->channel);
		if (pcie_device) {
			sas_target_priv_data->handle = pcie_device->handle;
			sas_target_priv_data->sas_address = pcie_device->wwid;
			sas_target_priv_data->port = NULL;
			sas_target_priv_data->pcie_dev = pcie_device;
			pcie_device->starget = starget;
			pcie_device->id = starget->id;
			pcie_device->channel = starget->channel;
			sas_target_priv_data->flags |=
				MPT_TARGET_FLAGS_PCIE_DEVICE;
			if (pcie_device->fast_path)
				sas_target_priv_data->flags |=
					MPT_TARGET_FASTPATH_IO;
		}
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		return 0;
	}

	/* sas/sata devices */
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	rphy = dev_to_rphy(starget->dev.parent);
	sas_device = __mpt3sas_get_sdev_by_addr_and_rphy(ioc,
	   rphy->identify.sas_address, rphy);

	if (sas_device) {
		sas_target_priv_data->handle = sas_device->handle;
		sas_target_priv_data->sas_address = sas_device->sas_address;
		sas_target_priv_data->port = sas_device->port;
		sas_target_priv_data->sas_dev = sas_device;
		sas_device->starget = starget;
		sas_device->id = starget->id;
		sas_device->channel = starget->channel;
		if (test_bit(sas_device->handle, ioc->pd_handles))
			sas_target_priv_data->flags |=
			    MPT_TARGET_FLAGS_RAID_COMPONENT;
		if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
			if (sas_device->fast_path)
				sas_target_priv_data->flags |= MPT_TARGET_FASTPATH_IO;
		}
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	return 0;
}

/**
 * _scsih_target_destroy - target destroy routine
 * @starget: scsi target struct
 *
 * Returns nothing.
 */
static void
_scsih_target_destroy(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct MPT3SAS_ADAPTER *ioc = shost_private(shost);
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
	struct _raid_device *raid_device;
	struct _pcie_device *pcie_device;
	unsigned long flags;

	sas_target_priv_data = starget->hostdata;
	if (!sas_target_priv_data)
		return;

	if (starget->channel == RAID_CHANNEL) {
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_id(ioc, starget->id,
		    starget->channel);
		if (raid_device) {
			raid_device->starget = NULL;
			raid_device->sdev = NULL;
		}
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
		goto out;
	}

	if (starget->channel == PCIE_CHANNEL) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = __mpt3sas_get_pdev_from_target(ioc,
							sas_target_priv_data);
		if (pcie_device && (pcie_device->starget == starget) &&
			(pcie_device->id == starget->id) &&
			(pcie_device->channel == starget->channel))
			pcie_device->starget = NULL;

		if (pcie_device) {
			/*
			* Corresponding get() is in _scsih_target_alloc()
			*/
			sas_target_priv_data->pcie_dev = NULL;
			pcie_device_put(pcie_device);
			pcie_device_put(pcie_device);
		}
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		goto out;
	}

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_from_target(ioc, sas_target_priv_data);
	if (sas_device && (sas_device->starget == starget) &&
	    (sas_device->id == starget->id) &&
	    (sas_device->channel == starget->channel))
		sas_device->starget = NULL;

	if (sas_device) {
		/*
		 * Corresponding get() is in _scsih_target_alloc()
		 */
		sas_target_priv_data->sas_dev = NULL;
		sas_device_put(sas_device);
		sas_device_put(sas_device);
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

 out:
	kfree(sas_target_priv_data);
	starget->hostdata = NULL;
}

/**
 * _scsih_slave_alloc - device add routine
 * @sdev: scsi device struct
 *
 * Returns 0 if ok. Any other return is assumed to be an error and
 * the device is ignored.
 */
static int
_scsih_slave_alloc(struct scsi_device *sdev)
{
	struct Scsi_Host *shost;
	struct MPT3SAS_ADAPTER *ioc;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_target *starget;
	struct _raid_device *raid_device;
	struct _sas_device *sas_device;
	struct _pcie_device *pcie_device;
	unsigned long flags;

	sas_device_priv_data = kzalloc(sizeof(*sas_device_priv_data), GFP_KERNEL);
	if (!sas_device_priv_data)
		return -ENOMEM;

	sas_device_priv_data->lun = sdev->lun;
	sas_device_priv_data->flags = MPT_DEVICE_FLAGS_INIT;

	starget = scsi_target(sdev);
	sas_target_priv_data = starget->hostdata;
	sas_target_priv_data->num_luns++;
	sas_device_priv_data->sas_target = sas_target_priv_data;
	sdev->hostdata = sas_device_priv_data;
	if ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_RAID_COMPONENT))
		sdev->no_uld_attach = 1;

	shost = dev_to_shost(&starget->dev);
	ioc = shost_private(shost);
	if (starget->channel == RAID_CHANNEL) {
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_id(ioc,
		    starget->id, starget->channel);
		if (raid_device)
			raid_device->sdev = sdev; /* raid is single lun */
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
	}
	if (starget->channel == PCIE_CHANNEL) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = __mpt3sas_get_pdev_by_wwid(ioc,
 				sas_target_priv_data->sas_address);
		if (pcie_device && (pcie_device->starget == NULL)) {
			sdev_printk(KERN_INFO, sdev,
				"%s : pcie_device->starget set to starget @ %d\n", __func__,
				__LINE__);
			pcie_device->starget = starget;
		}

		if (pcie_device)
			pcie_device_put(pcie_device);
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	} else if (!(sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME)) {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = __mpt3sas_get_sdev_by_addr(ioc,
 				sas_target_priv_data->sas_address,
				sas_target_priv_data->port);
		if (sas_device && (sas_device->starget == NULL)) {
			sdev_printk(KERN_INFO, sdev,
				"%s : sas_device->starget set to starget @ %d\n", __func__,
				__LINE__);
			sas_device->starget = starget;
		}

		if (sas_device)
			sas_device_put(sas_device);

		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0))
	sdev->tagged_supported = 1;
	scsi_activate_tcq(sdev, sdev->queue_depth);
#endif
	return 0;
}

/**
 * _scsih_slave_destroy - device destroy routine
 * @sdev: scsi device struct
 *
 * Returns nothing.
 */
static void
_scsih_slave_destroy(struct scsi_device *sdev)
{
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct scsi_target *starget;
	struct Scsi_Host *shost;
	struct MPT3SAS_ADAPTER *ioc;
	struct _sas_device *sas_device;
	struct _pcie_device *pcie_device;
	unsigned long flags;

	if (!sdev->hostdata)
		return;

	starget = scsi_target(sdev);
	sas_target_priv_data = starget->hostdata;
	sas_target_priv_data->num_luns--;

	shost = dev_to_shost(&starget->dev);
	ioc = shost_private(shost);

	if ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_PCIE_DEVICE)) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = __mpt3sas_get_pdev_from_target(ioc,
				sas_target_priv_data);
		if (pcie_device && !sas_target_priv_data->num_luns)
			pcie_device->starget = NULL;

		if (pcie_device)
			pcie_device_put(pcie_device);

		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	} else if (!(sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME)) {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = __mpt3sas_get_sdev_from_target(ioc,
				sas_target_priv_data);
		if (sas_device && !sas_target_priv_data->num_luns)
			sas_device->starget = NULL;

		if (sas_device)
			sas_device_put(sas_device);

		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	}

	kfree(sdev->hostdata);
	sdev->hostdata = NULL;
}

/**
 * _scsih_display_sata_capabilities - sata capabilities
 * @ioc: per adapter object
 * @handle: device handle
 * @sdev: scsi device struct
 */
static void
_scsih_display_sata_capabilities(struct MPT3SAS_ADAPTER *ioc,
	u16 handle, struct scsi_device *sdev)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	u32 ioc_status;
	u16 flags;
	u32 device_info;

	if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle))) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	flags = le16_to_cpu(sas_device_pg0.Flags);
	device_info = le32_to_cpu(sas_device_pg0.DeviceInfo);

	sdev_printk(KERN_INFO, sdev,
	    "atapi(%s), ncq(%s), asyn_notify(%s), smart(%s), fua(%s), "
	    "sw_preserve(%s)\n",
	    (device_info & MPI2_SAS_DEVICE_INFO_ATAPI_DEVICE) ? "y" : "n",
	    (flags & MPI2_SAS_DEVICE0_FLAGS_SATA_NCQ_SUPPORTED) ? "y" : "n",
	    (flags & MPI2_SAS_DEVICE0_FLAGS_SATA_ASYNCHRONOUS_NOTIFY) ? "y" :
	    "n",
	    (flags & MPI2_SAS_DEVICE0_FLAGS_SATA_SMART_SUPPORTED) ? "y" : "n",
	    (flags & MPI2_SAS_DEVICE0_FLAGS_SATA_FUA_SUPPORTED) ? "y" : "n",
	    (flags & MPI2_SAS_DEVICE0_FLAGS_SATA_SW_PRESERVE) ? "y" : "n");
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
/*
 * raid transport support -
 * Enabled for SLES11 and newer, in older kernels the driver will panic when
 * unloading the driver followed by a load - I beleive that the subroutine
 * raid_class_release() is not cleaning up properly.
 */

/**
 * _scsih_is_raid - return boolean indicating device is raid volume
 * @dev the device struct object
 */
static int
_scsih_is_raid(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct MPT3SAS_ADAPTER *ioc = shost_priv(sdev->host);

	if (ioc->is_warpdrive)
		return 0;
	return (sdev->channel == RAID_CHANNEL) ? 1 : 0;
}

/**
 * _scsih_get_resync - get raid volume resync percent complete
 * @dev the device struct object
 */
static void
_scsih_get_resync(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct MPT3SAS_ADAPTER *ioc = shost_private(sdev->host);
	static struct _raid_device *raid_device;
	unsigned long flags;
	Mpi2RaidVolPage0_t vol_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u32 volume_status_flags;
	u8 percent_complete;
	u16 handle;

	percent_complete = 0;
	handle = 0;
	if (ioc->is_warpdrive)
		return;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	raid_device = _scsih_raid_device_find_by_id(ioc, sdev->id,
	    sdev->channel);
	if (raid_device) {
		handle = raid_device->handle;
		percent_complete = raid_device->percent_complete;
	}
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);

	if (!handle)
		goto out;

	if (mpt3sas_config_get_raid_volume_pg0(ioc, &mpi_reply, &vol_pg0,
	     MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, handle,
	     sizeof(Mpi2RaidVolPage0_t))) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		percent_complete = 0;
		goto out;
	}

	volume_status_flags = le32_to_cpu(vol_pg0.VolumeStatusFlags);
	if (!(volume_status_flags &
	    MPI2_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS))
		percent_complete = 0;

 out:

	switch (ioc->hba_mpi_version_belonged) {
	case MPI2_VERSION:
		raid_set_resync(mpt2sas_raid_template, dev, percent_complete);
		break;
	case MPI25_VERSION:
	case MPI26_VERSION:
		raid_set_resync(mpt3sas_raid_template, dev, percent_complete);
		break;
	}

}

/**
 * _scsih_get_state - get raid volume level
 * @dev the device struct object
 */
static void
_scsih_get_state(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct MPT3SAS_ADAPTER *ioc = shost_private(sdev->host);
	static struct _raid_device *raid_device;
	unsigned long flags;
	Mpi2RaidVolPage0_t vol_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u32 volstate;
	enum raid_state state = RAID_STATE_UNKNOWN;
	u16 handle = 0;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	raid_device = _scsih_raid_device_find_by_id(ioc, sdev->id,
	    sdev->channel);
	if (raid_device)
		handle = raid_device->handle;
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);

	if (!raid_device)
		goto out;

	if (mpt3sas_config_get_raid_volume_pg0(ioc, &mpi_reply, &vol_pg0,
	     MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, handle,
	     sizeof(Mpi2RaidVolPage0_t))) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}

	volstate = le32_to_cpu(vol_pg0.VolumeStatusFlags);
	if (volstate & MPI2_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS) {
		state = RAID_STATE_RESYNCING;
		goto out;
	}

	switch (vol_pg0.VolumeState) {
	case MPI2_RAID_VOL_STATE_OPTIMAL:
	case MPI2_RAID_VOL_STATE_ONLINE:
		state = RAID_STATE_ACTIVE;
		break;
	case  MPI2_RAID_VOL_STATE_DEGRADED:
		state = RAID_STATE_DEGRADED;
		break;
	case MPI2_RAID_VOL_STATE_FAILED:
	case MPI2_RAID_VOL_STATE_MISSING:
		state = RAID_STATE_OFFLINE;
		break;
	}
 out:
	switch (ioc->hba_mpi_version_belonged) {
	case MPI2_VERSION:
		raid_set_state(mpt2sas_raid_template, dev, state);
		break;
	case MPI25_VERSION:
	case MPI26_VERSION:
		raid_set_state(mpt3sas_raid_template, dev, state);
		break;
	}
}

/**
 * _scsih_set_level - set raid level
 * @sdev: scsi device struct
 * @volume_type: volume type
 */
static void
_scsih_set_level(struct MPT3SAS_ADAPTER *ioc,
	struct scsi_device *sdev, u8 volume_type)
{
	enum raid_level level = RAID_LEVEL_UNKNOWN;

	switch (volume_type) {
	case MPI2_RAID_VOL_TYPE_RAID0:
		level = RAID_LEVEL_0;
		break;
	case MPI2_RAID_VOL_TYPE_RAID10:
	case MPI2_RAID_VOL_TYPE_RAID1E:
		level = RAID_LEVEL_10;
		break;
	case MPI2_RAID_VOL_TYPE_RAID1:
		level = RAID_LEVEL_1;
		break;
	}

	switch (ioc->hba_mpi_version_belonged) {
	case MPI2_VERSION:
		raid_set_level(mpt2sas_raid_template,
				&sdev->sdev_gendev, level);
		break;
	case MPI25_VERSION:
	case MPI26_VERSION:
		raid_set_level(mpt3sas_raid_template,
				&sdev->sdev_gendev, level);
		break;
	}
}
#endif /* raid transport support - (2.6.27 and newer) */

/**
 * _scsih_get_volume_capabilities - volume capabilities
 * @ioc: per adapter object
 * @sas_device: the raid_device object
 *
 * Returns 0 for success, else 1
 */
static int
_scsih_get_volume_capabilities(struct MPT3SAS_ADAPTER *ioc,
	struct _raid_device *raid_device)
{
	Mpi2RaidVolPage0_t *vol_pg0;
	Mpi2RaidPhysDiskPage0_t pd_pg0;
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 sz;
	u8 num_pds;

	if ((mpt3sas_config_get_number_pds(ioc, raid_device->handle,
	    &num_pds)) || !num_pds) {
		dfailprintk(ioc, printk(MPT3SAS_WARN_FMT
		    "failure at %s:%d/%s()!\n", ioc->name, __FILE__, __LINE__,
		    __func__));
		return 1;
	}

	raid_device->num_pds = num_pds;
	sz = offsetof(Mpi2RaidVolPage0_t, PhysDisk) + (num_pds *
	    sizeof(Mpi2RaidVol0PhysDisk_t));
	vol_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!vol_pg0) {
		dfailprintk(ioc, printk(MPT3SAS_WARN_FMT
		    "failure at %s:%d/%s()!\n", ioc->name, __FILE__, __LINE__,
		    __func__));
		return 1;
	}

	if ((mpt3sas_config_get_raid_volume_pg0(ioc, &mpi_reply, vol_pg0,
	     MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, raid_device->handle, sz))) {
		dfailprintk(ioc, printk(MPT3SAS_WARN_FMT
		    "failure at %s:%d/%s()!\n", ioc->name, __FILE__, __LINE__,
		    __func__));
		kfree(vol_pg0);
		return 1;
	}

	raid_device->volume_type = vol_pg0->VolumeType;

	/* figure out what the underlying devices are by
	 * obtaining the device_info bits for the 1st device
	 */
	if (!(mpt3sas_config_get_phys_disk_pg0(ioc, &mpi_reply,
	    &pd_pg0, MPI2_PHYSDISK_PGAD_FORM_PHYSDISKNUM,
	    vol_pg0->PhysDisk[0].PhysDiskNum))) {
		if (!(mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply,
		    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_HANDLE,
		    le16_to_cpu(pd_pg0.DevHandle)))) {
			raid_device->device_info =
			    le32_to_cpu(sas_device_pg0.DeviceInfo);
		}
	}

	kfree(vol_pg0);
	return 0;
}

/**
 * _scsih_enable_tlr - setting TLR flags
 * @ioc: per adapter object
 * @sdev: scsi device struct
 *
 * Enabling Transaction Layer Retries for tape devices when
 * vpd page 0x90 is present
 *
 */
static void
_scsih_enable_tlr(struct MPT3SAS_ADAPTER *ioc, struct scsi_device *sdev)
{
	u8 data[30];
	u8 page_len, ii;
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;

	/* only for TAPE */
	if (sdev->type != TYPE_TAPE)
		return;

	if (!(ioc->facts.IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_TLR))
		return;

	sas_device_priv_data = sdev->hostdata;
	if (!sas_device_priv_data)
		return;
	sas_target_priv_data = sas_device_priv_data->sas_target;
	if (!sas_target_priv_data)
		return;

	/* is Protocol-specific logical unit information (0x90) present ?? */
	if (_scsih_inquiry_vpd_supported_pages(ioc,
	    sas_target_priv_data->handle, sdev->lun, data,
	    sizeof(data)) != DEVICE_READY) {
	    sas_device = mpt3sas_get_sdev_by_addr(ioc,
		   sas_target_priv_data->sas_address,
		   sas_target_priv_data->port);
		if (sas_device) {
			sdev_printk(KERN_INFO, sdev, "%s: DEVICE NOT READY: handle(0x%04x), "
				"sas_addr(0x%016llx), phy(%d), device_name(0x%016llx) \n",
				__func__, sas_device->handle,
				(unsigned long long)sas_device->sas_address, sas_device->phy,
				(unsigned long long)sas_device->device_name);

			_scsih_display_enclosure_chassis_info(NULL, sas_device, sdev, NULL);

			sas_device_put(sas_device);
		}
		return;
	}
	page_len = data[3];
	for (ii = 4; ii < page_len + 4; ii++) {
		if (data[ii] == 0x90) {
			sas_device_priv_data->flags |= MPT_DEVICE_TLR_ON;
			return;
		}
	}
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22))
/**
 * _scsih_enable_ssu_on_sata - Enable drive spin down/up
 * @sas_device: sas device object
 * @sdev: scsi device struct
 *
 * Enables START_STOP command for SATA drives (sata drive spin down/up)
 *
 */
static void
_scsih_enable_ssu_on_sata(struct _sas_device *sas_device,
		struct scsi_device *sdev)
{
	if (!(sas_device->device_info & MPI2_SAS_DEVICE_INFO_SATA_DEVICE))
		return;

	switch (allow_drive_spindown) {

	case 1:
		if (sas_device->ssd_device)
			sdev->manage_start_stop = 1;
		break;
	case 2:
		if (!sas_device->ssd_device)
			sdev->manage_start_stop = 1;
		break;
	case 3:
		sdev->manage_start_stop = 1;
		break;
	}

}
#endif

/**
 * _scsih_set_queue_flag - Sets a queue flag. 
 * @flag: flag to be set
 * @q: request queue
 *
 * Sets queue flag based on flag(QUEUE_FLAG_NOMERGES/QUEUE_FLAG_SG_GAPS) 
 *
 */
static void
_scsih_set_queue_flag(unsigned int flag, struct request_queue *q)
{
	set_bit(flag, &q->queue_flags);
}

/**
 * _scsih_slave_configure - device configure routine.
 * @sdev: scsi device struct
 *
 * Returns 0 if ok. Any other return is assumed to be an error and
 * the device is ignored.
 */
static int
_scsih_slave_configure(struct scsi_device *sdev)
{
	struct Scsi_Host *shost = sdev->host;
	struct MPT3SAS_ADAPTER *ioc = shost_private(shost);
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
	struct _pcie_device *pcie_device;
	struct _raid_device *raid_device;
	unsigned long flags;
	int qdepth;
	u8 ssp_target = 0;
	char *ds = "";
	char *r_level = "";
	u16 handle, volume_handle = 0;
	u64 volume_wwid = 0;
	u8 *serial_number = NULL;
	enum device_responsive_state retval;
	u8 count=0;

	qdepth = 1;
	sas_device_priv_data = sdev->hostdata;
	sas_device_priv_data->configured_lun = 1;
	sas_device_priv_data->flags &= ~MPT_DEVICE_FLAGS_INIT;
	sas_target_priv_data = sas_device_priv_data->sas_target;
	handle = sas_target_priv_data->handle;

	/* raid volume handling */
	if (sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME) {

		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = mpt3sas_raid_device_find_by_handle(ioc, handle);
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
		if (!raid_device) {
			dfailprintk(ioc, printk(MPT3SAS_WARN_FMT
			    "failure at %s:%d/%s()!\n", ioc->name, __FILE__,
			    __LINE__, __func__));
			return 1;
		}

		if (_scsih_get_volume_capabilities(ioc, raid_device)) {
			dfailprintk(ioc, printk(MPT3SAS_WARN_FMT
			    "failure at %s:%d/%s()!\n", ioc->name, __FILE__,
			    __LINE__, __func__));
			return 1;
		}

		mpt3sas_init_warpdrive_properties(ioc, raid_device);

		/* RAID Queue Depth Support
		 * IS volume = underlying qdepth of drive type, either
		 *    MPT3SAS_SAS_QUEUE_DEPTH or MPT3SAS_SATA_QUEUE_DEPTH
		 * IM/IME/R10 = 128 (MPT3SAS_RAID_QUEUE_DEPTH)
		 */
		if (raid_device->device_info &
		    MPI2_SAS_DEVICE_INFO_SSP_TARGET) {
			qdepth = MPT3SAS_SAS_QUEUE_DEPTH;
			ds = "SSP";
		} else {
			qdepth = MPT3SAS_SATA_QUEUE_DEPTH;
			 if (raid_device->device_info &
			    MPI2_SAS_DEVICE_INFO_SATA_DEVICE)
				ds = "SATA";
			else
				ds = "STP";
		}

		switch (raid_device->volume_type) {
		case MPI2_RAID_VOL_TYPE_RAID0:
			r_level = "RAID0";
			break;
		case MPI2_RAID_VOL_TYPE_RAID1E:
			qdepth = MPT3SAS_RAID_QUEUE_DEPTH;
			if (ioc->manu_pg10.OEMIdentifier &&
			    (le32_to_cpu(ioc->manu_pg10.GenericFlags0) &
			    MFG10_GF0_R10_DISPLAY) &&
			    !(raid_device->num_pds % 2))
				r_level = "RAID10";
			else
				r_level = "RAID1E";
			break;
		case MPI2_RAID_VOL_TYPE_RAID1:
			qdepth = MPT3SAS_RAID_QUEUE_DEPTH;
			r_level = "RAID1";
			break;
		case MPI2_RAID_VOL_TYPE_RAID10:
			qdepth = MPT3SAS_RAID_QUEUE_DEPTH;
			r_level = "RAID10";
			break;
		case MPI2_RAID_VOL_TYPE_UNKNOWN:
		default:
			qdepth = MPT3SAS_RAID_QUEUE_DEPTH;
			r_level = "RAIDX";
			break;
		}

		if (!ioc->warpdrive_msg)
			sdev_printk(KERN_INFO, sdev, "%s: handle(0x%04x), "
			    "wwid(0x%016llx), pd_count(%d), type(%s)\n",
			    r_level, raid_device->handle,
			    (unsigned long long)raid_device->wwid,
			    raid_device->num_pds, ds);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
		if (shost->max_sectors > MPT3SAS_RAID_MAX_SECTORS) {
			blk_queue_max_hw_sectors(sdev->request_queue,
						MPT3SAS_RAID_MAX_SECTORS);
			sdev_printk(KERN_INFO, sdev,
				"Set queue's max_sector to: %u\n",
				MPT3SAS_RAID_MAX_SECTORS);
		}
#endif

		mpt3sas_scsih_change_queue_depth(sdev, qdepth);

/* raid transport support */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
		if (!ioc->is_warpdrive)
			_scsih_set_level(ioc, sdev, raid_device->volume_type);
#endif
		return 0;
	}

	/* non-raid handling */
	if (sas_target_priv_data->flags & MPT_TARGET_FLAGS_RAID_COMPONENT) {
		if (mpt3sas_config_get_volume_handle(ioc, handle,
		    &volume_handle)) {
			dfailprintk(ioc, printk(MPT3SAS_WARN_FMT
			    "failure at %s:%d/%s()!\n", ioc->name,
			    __FILE__, __LINE__, __func__));
			return 1;
		}
		if (volume_handle && mpt3sas_config_get_volume_wwid(ioc,
		    volume_handle, &volume_wwid)) {
			dfailprintk(ioc, printk(MPT3SAS_WARN_FMT
			    "failure at %s:%d/%s()!\n", ioc->name,
			    __FILE__, __LINE__, __func__));
			return 1;
		}
	}

	_scsih_inquiry_vpd_sn(ioc, handle, &serial_number);

	/* PCIe handling */
	if (sas_target_priv_data->flags & MPT_TARGET_FLAGS_PCIE_DEVICE) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = __mpt3sas_get_pdev_by_wwid(ioc,
						sas_device_priv_data->sas_target->sas_address);
		if (!pcie_device) {
			spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
			dfailprintk(ioc, printk(MPT3SAS_WARN_FMT
				"failure at %s:%d/%s()!\n", ioc->name, __FILE__,
				__LINE__, __func__));
			kfree(serial_number);
			return 1;
		}
		pcie_device->serial_number = serial_number;
		/*TODO-right Queue Depth?*/
		qdepth = MPT3SAS_NVME_QUEUE_DEPTH;
		ds = "NVMe";
		/*TODO-Add device name when defined*/
		sdev_printk(KERN_INFO, sdev, "%s: handle(0x%04x), wwid(0x%016llx), "
			"port(%d)\n", ds, handle, (unsigned long long)pcie_device->wwid,
			pcie_device->port_num);
		if(pcie_device->enclosure_handle != 0)
			sdev_printk(KERN_INFO, sdev, "%s: enclosure logical id(0x%016llx),"
				" slot(%d)\n", ds, (unsigned long long)
				pcie_device->enclosure_logical_id, pcie_device->slot);
		if(pcie_device->connector_name[0] != '\0')
			sdev_printk(KERN_INFO, sdev, "%s: enclosure level(0x%04x), "
				"connector name( %s)\n", ds, pcie_device->enclosure_level,
				pcie_device->connector_name);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
		if (pcie_device->nvme_mdts) {
			blk_queue_max_hw_sectors(sdev->request_queue,
						pcie_device->nvme_mdts/512);
		}
#endif
		pcie_device_put(pcie_device);
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		if (serial_number)
			sdev_printk(KERN_INFO, sdev, "serial_number(%s)\n", serial_number);

		mpt3sas_scsih_change_queue_depth(sdev, qdepth);

		/* Enable QUEUE_FLAG_NOMERGES flag, so that IOs won't be
		 * merged and can eliminate holes created during merging
		 * operation.
		 */
		_scsih_set_queue_flag(QUEUE_FLAG_NOMERGES,
					 sdev->request_queue);

#if ((defined(RHEL_MAJOR) && (RHEL_MAJOR == 7) && (RHEL_MINOR >= 3)) || \
     (LINUX_VERSION_CODE >= KERNEL_VERSION(4,3,0)))
		blk_queue_virt_boundary(sdev->request_queue,
					 ioc->page_size - 1);
#else
#ifdef QUEUE_FLAG_SG_GAPS
		/* Enable QUEUE_FLAG_SG_GAPS flag, So that kernel won't
		 * issue any IO's to the driver which has SG GAPs (holes).
		 */
		_scsih_set_queue_flag(QUEUE_FLAG_SG_GAPS,
					 sdev->request_queue);
#endif
#endif
		/*TODO -- Do we need to support EEDP for NVMe devices*/
		return 0;
	}

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_addr(ioc,
	   sas_device_priv_data->sas_target->sas_address,
	   sas_device_priv_data->sas_target->port);
	if (!sas_device) {
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		dfailprintk(ioc, printk(MPT3SAS_WARN_FMT
		    "failure at %s:%d/%s()!\n", ioc->name, __FILE__, __LINE__,
		    __func__));
		kfree(serial_number);
		return 1;
	}

	sas_device->volume_handle = volume_handle;
	sas_device->volume_wwid = volume_wwid;
	sas_device->serial_number = serial_number;
	if (sas_device->device_info & MPI2_SAS_DEVICE_INFO_SSP_TARGET) {
		qdepth = MPT3SAS_SAS_QUEUE_DEPTH;
		ssp_target = 1;
		if (sas_device->device_info & MPI2_SAS_DEVICE_INFO_SEP) {
			sdev_printk(KERN_WARNING, sdev,
			"set ignore_delay_remove for handle(0x%04x)\n",
			sas_device_priv_data->sas_target->handle);
			sas_device_priv_data->ignore_delay_remove = 1;
			ds = "SES";
		} else
			ds = "SSP";
	} else {
		qdepth = MPT3SAS_SATA_QUEUE_DEPTH;
		if (sas_device->device_info & MPI2_SAS_DEVICE_INFO_STP_TARGET)
			ds = "STP";
		else if (sas_device->device_info &
		    MPI2_SAS_DEVICE_INFO_SATA_DEVICE)
			ds = "SATA";
	}

	sdev_printk(KERN_INFO, sdev, "%s: handle(0x%04x), "
	    "sas_addr(0x%016llx), phy(%d), device_name(0x%016llx)\n",
	    ds, handle, (unsigned long long)sas_device->sas_address,
	    sas_device->phy, (unsigned long long)sas_device->device_name);

	_scsih_display_enclosure_chassis_info(NULL, sas_device, sdev, NULL);

	sas_device_put(sas_device);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	if (!ssp_target) {
		_scsih_display_sata_capabilities(ioc, handle, sdev);

		do {
			retval = _scsih_ata_pass_thru_idd(ioc,handle,
				    &sas_device->ssd_device, 30, 0);
		} while ((retval == DEVICE_RETRY ||retval == DEVICE_RETRY_UA)
			&& count++ < 3);
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22))
	/* Enable spin-down/up based on module param allow_drive_spindown */
	_scsih_enable_ssu_on_sata(sas_device, sdev);
#endif
	if (serial_number)
		sdev_printk(KERN_INFO, sdev, "serial_number(%s)\n",
		    serial_number);

	mpt3sas_scsih_change_queue_depth(sdev, qdepth);

	if (ssp_target) {
		sas_read_port_mode_page(sdev);
		_scsih_enable_tlr(ioc, sdev);
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27))
	if (!ioc->disable_eedp_support) {
		if (ssp_target && (!(sas_target_priv_data->flags &
		    MPT_TARGET_FLAGS_RAID_COMPONENT))) {
			struct read_cap_parameter data;
			enum device_responsive_state retcode;
			u8 retry_count = 0;

			if (!(sdev->inquiry[5] & 1))
				goto out;
retry:
			/* issue one retry to handle UA's */
			memset(&data, 0, sizeof(struct read_cap_parameter));
			retcode = _scsih_read_capacity_16(ioc,
			    sas_target_priv_data->handle, sdev->lun, &data,
			    sizeof(struct read_cap_parameter));

			if ((retcode == DEVICE_RETRY || retcode == DEVICE_RETRY_UA)
			    && (!retry_count++))
				goto retry;
			if (retcode != DEVICE_READY) {
				sdev_printk(KERN_INFO, sdev, "%s: DEVICE NOT READY: handle(0x%04x), "
				    "sas_addr(0x%016llx), phy(%d), device_name(0x%016llx)\n",
				    ds, handle, (unsigned long long)sas_device->sas_address,
					sas_device->phy, (unsigned long long)sas_device->device_name);

				_scsih_display_enclosure_chassis_info(NULL, sas_device, sdev, NULL);

				goto out;
			}
			if (!data.prot_en)
				goto out;
			sas_device_priv_data->eedp_type = data.p_type + 1;

			if (sas_device_priv_data->eedp_type == 2) {
				sdev_printk(KERN_INFO, sdev, "formatted with "
				    "DIF Type 2 protection which is currently "
				    "unsupported.\n");
				goto out;
			}

			sas_device_priv_data->eedp_enable = 1;
			sdev_printk(KERN_INFO, sdev, "Enabling DIF Type %d "
			    "protection\n", sas_device_priv_data->eedp_type);
		}
out:
		return 0;
	}
#endif
	return 0;
}

/**
 * _scsih_bios_param - fetch head, sector, cylinder info for a disk
 * @sdev: scsi device struct
 * @bdev: pointer to block device context
 * @capacity: device size (in 512 byte sectors)
 * @params: three element array to place output:
 *              params[0] number of heads (max 255)
 *              params[1] number of sectors (max 63)
 *              params[2] number of cylinders
 *
 * Return nothing.
 */
static int
_scsih_bios_param(struct scsi_device *sdev, struct block_device *bdev,
	sector_t capacity, int params[])
{
	int		heads;
	int		sectors;
	sector_t	cylinders;
	ulong		dummy;

	heads = 64;
	sectors = 32;

	dummy = heads * sectors;
	cylinders = capacity;
	sector_div(cylinders, dummy);

	/*
	 * Handle extended translation size for logical drives
	 * > 1Gb
	 */
	if ((ulong)capacity >= 0x200000) {
		heads = 255;
		sectors = 63;
		dummy = heads * sectors;
		cylinders = capacity;
		sector_div(cylinders, dummy);
	}

	/* return result */
	params[0] = heads;
	params[1] = sectors;
	params[2] = cylinders;

	return 0;
}

/**
 * _scsih_response_code - translation of device response code
 * @ioc: per adapter object
 * @response_code: response code returned by the device
 *
 * Return nothing.
 */
static void
_scsih_response_code(struct MPT3SAS_ADAPTER *ioc, u8 response_code)
{
	char *desc;

	switch (response_code) {
	case MPI2_SCSITASKMGMT_RSP_TM_COMPLETE:
		desc = "task management request completed";
		break;
	case MPI2_SCSITASKMGMT_RSP_INVALID_FRAME:
		desc = "invalid frame";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_NOT_SUPPORTED:
		desc = "task management request not supported";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_FAILED:
		desc = "task management request failed";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_SUCCEEDED:
		desc = "task management request succeeded";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_INVALID_LUN:
		desc = "invalid lun";
		break;
	case 0xA:
		desc = "overlapped tag attempted";
		break;
	case MPI2_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC:
		desc = "task queued, however not sent to target";
		break;
	default:
		desc = "unknown";
		break;
	}
	printk(MPT3SAS_WARN_FMT "response_code(0x%01x): %s\n",
		ioc->name, response_code, desc);
}

/**
 * _scsih_tm_done - tm completion routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: none.
 *
 * The callback handler when using scsih_issue_tm.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_tm_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index, u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;

	if (ioc->tm_cmds.status == MPT3_CMD_NOT_USED)
		return 1;
	if (ioc->tm_cmds.smid != smid)
		return 1;
	ioc->tm_cmds.status |= MPT3_CMD_COMPLETE;
	mpi_reply =  mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (mpi_reply) {
		memcpy(ioc->tm_cmds.reply, mpi_reply, mpi_reply->MsgLength*4);
		ioc->tm_cmds.status |= MPT3_CMD_REPLY_VALID;
	}
	ioc->tm_cmds.status &= ~MPT3_CMD_PENDING;
	complete(&ioc->tm_cmds.done);
	return 1;
}

/**
 * mpt3sas_scsih_set_tm_flag - set per target tm_busy
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During taskmangement request, we need to freeze the device queue.
 */
void
mpt3sas_scsih_set_tm_flag(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;
	u8 skip = 0;

	shost_for_each_device(sdev, ioc->shost) {
		if (skip)
			continue;
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->sas_target->handle == handle) {
			sas_device_priv_data->sas_target->tm_busy = 1;
			skip = 1;
			ioc->ignore_loginfos = 1;
		}
	}
}

/**
 * mpt3sas_scsih_clear_tm_flag - clear per target tm_busy
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During taskmangement request, we need to freeze the device queue.
 */
void
mpt3sas_scsih_clear_tm_flag(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;
	u8 skip = 0;

	shost_for_each_device(sdev, ioc->shost) {
		if (skip)
			continue;
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->sas_target->handle == handle) {
			sas_device_priv_data->sas_target->tm_busy = 0;
			skip = 1;
			ioc->ignore_loginfos = 0;
		}
	}
}

/**
 * scsih_tm_cmd_map_status - map the target reset & LUN reset TM status
 * @ioc - per adapter object
 * @channel - the channel assigned by the OS 
 * @id: the id assigned by the OS
 * @lun: lun number
 * @type: MPI2_SCSITASKMGMT_TASKTYPE__XXX (defined in mpi2_init.h)
 * @smid_task: smid assigned to the task
 *
 * Look whether TM has aborted the timed out SCSI command, if
 * TM has aborted the IO then return SUCCESS else return FAILED.
 */
int
scsih_tm_cmd_map_status(struct MPT3SAS_ADAPTER *ioc, uint channel,
	uint id, uint lun, u8 type, u16 smid_task)
{

	if (smid_task <= ioc->shost->can_queue) {
		switch (type) {
		case MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET:

		        if (!(_scsih_scsi_lookup_find_by_target(ioc, id, channel)))
				return SUCCESS;
			break;
		case MPI2_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET:
		case MPI2_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET:
			if (!(_scsih_scsi_lookup_find_by_lun(ioc, id, lun, channel)))
				return SUCCESS;
			break;
		default:
			return SUCCESS;
		}
	} else if (smid_task == ioc->scsih_cmds.smid) {
		if ((ioc->scsih_cmds.status & MPT3_CMD_COMPLETE) ||
		    (ioc->scsih_cmds.status & MPT3_CMD_NOT_USED))
			return SUCCESS;
	} else if (smid_task == ioc->ctl_cmds.smid) {
    		if ((ioc->ctl_cmds.status & MPT3_CMD_COMPLETE) ||
		    (ioc->ctl_cmds.status & MPT3_CMD_NOT_USED))
			return SUCCESS;
	}

	return FAILED;
}

/**
 * scsih_tm_post_processing - post proceesing of target & LUN reset 
 * @ioc - per adapter object
 * @handle: device handle
 * @channel - the channel assigned by the OS 
 * @id: the id assigned by the OS
 * @lun: lun number
 * @type: MPI2_SCSITASKMGMT_TASKTYPE__XXX (defined in mpi2_init.h)
 * @smid_task: smid assigned to the task
 *
 * Post processing of target & LUN reset. Due to interrupt latency
 * issue it possible that interrupt for aborted IO might not
 * received yet. So before returning failure status poll the
 * reply descriptor pools for the reply of timed out SCSI command.
 * Return FAILED status if reply for timed out is not received
 * otherwise return SUCCESS.
 */
int
scsih_tm_post_processing(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	uint channel, uint id, uint lun, u8 type, u16 smid_task)
{
	int rc;

	rc = scsih_tm_cmd_map_status(ioc, channel, id, lun, type, smid_task); 
	if (rc == SUCCESS)
		return rc;

	printk(MPT3SAS_INFO_FMT
	    "Poll ReplyDescriptor queues for completion of"
	    " smid(%d), task_type(0x%02x), handle(0x%04x)\n",
	    ioc->name, smid_task, type, handle);

	/*
	 * Due to interrupt latency issues, driver may receive interrupt for
	 * TM first and then for aborted SCSI IO command. So, poll all the
	 * ReplyDescriptor pools before returning the FAILED status to SML.
	 */
	mpt3sas_base_mask_interrupts(ioc);
	mpt3sas_base_sync_reply_irqs(ioc, 1);
	mpt3sas_base_unmask_interrupts(ioc);

	return scsih_tm_cmd_map_status(ioc, channel, id, lun, type, smid_task);
}

/**
 * mpt3sas_scsih_issue_tm - main routine for sending tm requests
 * @ioc: per adapter struct
 * @device_handle: device handle
 * @channel: the channel assigned by the OS
 * @id: the id assigned by the OS
 * @lun: lun number
 * @type: MPI2_SCSITASKMGMT_TASKTYPE__XXX (defined in mpi2_init.h)
 * @smid_task: smid assigned to the task
 * @timeout: timeout in seconds
 * @tr_method: Target Reset Method
 * Context: user
 *
 * A generic API for sending task management requests to firmware.
 *
 * The callback index is set inside `ioc->tm_cb_idx`.
 *
 * Return SUCCESS or FAILED.
 */
int
mpt3sas_scsih_issue_tm(struct MPT3SAS_ADAPTER *ioc, u16 handle, uint channel,
	uint id, uint lun, u8 type, u16 smid_task, u8 timeout,
	u8 tr_method)
{
	Mpi2SCSITaskManagementRequest_t *mpi_request;
	Mpi2SCSITaskManagementReply_t *mpi_reply;
	Mpi25SCSIIORequest_t *request;
	u16 smid = 0;
	u32 ioc_state;
	struct scsiio_tracker *scsi_lookup = NULL;
	int rc;
	u16 msix_task = 0;
	u8 issue_reset = 0;
	lockdep_assert_held(&ioc->tm_cmds.mutex);

	if (ioc->tm_cmds.status != MPT3_CMD_NOT_USED) {
		printk(MPT3SAS_INFO_FMT "%s: tm_cmd busy!!!\n",
		    __func__, ioc->name);
		return FAILED;
	}

	if (ioc->shost_recovery || ioc->remove_host ||
	    				ioc->pci_error_recovery) {
		printk(MPT3SAS_INFO_FMT "%s: host reset in progress!\n",
		    __func__, ioc->name);
		return FAILED;
	}

	ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
	if (ioc_state & MPI2_DOORBELL_USED) {
		printk(MPT3SAS_INFO_FMT "unexpected doorbell active!\n",
		    ioc->name);
		rc = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
		return (!rc) ? SUCCESS : FAILED;
	}

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
		mpt3sas_print_fault_code(ioc, ioc_state &
		    MPI2_DOORBELL_DATA_MASK);
		rc = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
		return (!rc) ? SUCCESS : FAILED;
	}
	else if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_COREDUMP) {
		mpt3sas_base_coredump_info(ioc, ioc_state &
		    MPI2_DOORBELL_DATA_MASK);
		rc = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
		return (!rc) ? SUCCESS : FAILED;
	}

	smid = mpt3sas_base_get_smid_hpr(ioc, ioc->tm_cb_idx);
	if (!smid) {
		printk(MPT3SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		return FAILED;
	}

	if (type == MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK)
		scsi_lookup = mpt3sas_get_st_from_smid(ioc, smid_task);
	dtmprintk(ioc, printk(MPT3SAS_INFO_FMT "sending tm: handle(0x%04x),"
	    " task_type(0x%02x), timeout(%d) tr_method(0x%x) smid(%d)\n", ioc->name, handle, type,
	    timeout, tr_method, smid_task));
	ioc->tm_cmds.status = MPT3_CMD_PENDING;
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->tm_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2SCSITaskManagementRequest_t));
	memset(ioc->tm_cmds.reply, 0, sizeof(Mpi2SCSITaskManagementReply_t));
	mpi_request->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
	mpi_request->DevHandle = cpu_to_le16(handle);
	mpi_request->TaskType = type;
	mpi_request->MsgFlags = tr_method;
	if (type == MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK ||
	    type == MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK)
		mpi_request->TaskMID = cpu_to_le16(smid_task);
	int_to_scsilun(lun, (struct scsi_lun *)mpi_request->LUN);
	mpt3sas_scsih_set_tm_flag(ioc, handle);
	init_completion(&ioc->tm_cmds.done);
	if ((type == MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK) &&
	    (scsi_lookup && (scsi_lookup->msix_io < ioc->reply_queue_count)))
		msix_task = scsi_lookup->msix_io;
	else
		msix_task = 0;
	ioc->put_smid_hi_priority(ioc, smid, msix_task);
	wait_for_completion_timeout(&ioc->tm_cmds.done, timeout*HZ);
	if (!(ioc->tm_cmds.status & MPT3_CMD_COMPLETE)) {
		mpt3sas_check_cmd_timeout(ioc,
			ioc->tm_cmds.status, mpi_request,
			sizeof(Mpi2SCSITaskManagementRequest_t)/4, issue_reset);
	       if (issue_reset)	{
			rc = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
			rc = (!rc) ? SUCCESS : FAILED;
			goto out;
		}
	}

	mpt3sas_base_sync_reply_irqs(ioc, 0);

	if (ioc->tm_cmds.status & MPT3_CMD_REPLY_VALID) {
		mpt3sas_trigger_master(ioc, MASTER_TRIGGER_TASK_MANAGMENT);
		mpi_reply = ioc->tm_cmds.reply;
		dtmprintk(ioc, printk(MPT3SAS_INFO_FMT "complete tm: "
		    "ioc_status(0x%04x), loginfo(0x%08x), term_count(0x%08x)\n",
		    ioc->name, le16_to_cpu(mpi_reply->IOCStatus),
		    le32_to_cpu(mpi_reply->IOCLogInfo),
		    le32_to_cpu(mpi_reply->TerminationCount)));
		if (ioc->logging_level & MPT_DEBUG_TM) {
			_scsih_response_code(ioc, mpi_reply->ResponseCode);
			if (mpi_reply->IOCStatus)
				_debug_dump_mf(mpi_request,
				    sizeof(Mpi2SCSITaskManagementRequest_t)/4);
		}
	}

	switch (type) {
	case MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK:
		rc = SUCCESS;
		/*
		 * If DevHandle filed in smid_task's entry of request pool
		 * doesn't matches with device handle on which this task abort
		 * TM is received then it means that TM has successfully
		 * aborted the timed out command. Since smid_task's entry in
		 * request pool will be memset to zero once the timed out
		 * command is returned to the SML. If the command is not
		 * aborted then smid_task’s entry won’t be cleared and it will
		 * have same DevHandle value on which this task abort TM is
		 * received and driver will return the TM status as FAILED.
		 */
		request = mpt3sas_base_get_msg_frame(ioc, smid_task);
		if (le16_to_cpu(request->DevHandle) != handle)
			break;

		printk(MPT3SAS_INFO_FMT
		    "Task abort tm failed: handle(0x%04x), timeout(%d) tr_method(0x%x) smid(%d) msix_index(%d)\n",
		    ioc->name, handle, timeout, tr_method, smid_task, msix_task);
		rc = FAILED;
		break;

	case MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET:
	case MPI2_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET:
	case MPI2_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET:
		rc = scsih_tm_post_processing(ioc, handle, channel, id, lun,
		    type, smid_task);
		break;
	case MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK:
		rc = SUCCESS;
		break;
	default:
		rc = FAILED;
		break;
	}

out:
	mpt3sas_scsih_clear_tm_flag(ioc, handle);
	ioc->tm_cmds.status = MPT3_CMD_NOT_USED;

	return rc;
}

/**
 * mpt3sas_scsih_issue_locked_tm - calling function for mpt3sas_scsih_issue_tm()
 * @ioc: per adapter struct
 * @handle: device handle
 * @channel: the channel assigned by the OS
 * @id: the id assigned by the OS
 * @lun: lun number
 * @type: MPI2_SCSITASKMGMT_TASKTYPE__XXX (defined in mpi2_init.h)
 * @smid_task: smid assigned to the task
 * @timeout: timeout in seconds
 * @tr_method: Target Reset Method
 * Context: user
 *
 * This function acquires mutex before calling mpt3sas_scsih_issue_tm() and
 * releases mutex after sending tm requests via mpt3sas_scsih_issue_tm().
 *
 * Return SUCCESS or FAILED.
 */
int
mpt3sas_scsih_issue_locked_tm(struct MPT3SAS_ADAPTER *ioc, u16 handle, uint channel,
	uint id, uint lun, u8 type, u16 smid_task, u8 timeout,
	u8 tr_method)
{
       int ret;

       mutex_lock(&ioc->tm_cmds.mutex);
       ret = mpt3sas_scsih_issue_tm(ioc, handle, channel, id, lun, type,
                       smid_task, timeout, tr_method);
       mutex_unlock(&ioc->tm_cmds.mutex);

       return ret;
}

/**
 * _scsih_tm_display_info - displays info about the device
 * @ioc: per adapter struct
 * @scmd: pointer to scsi command object
 *
 * Called by task management callback handlers.
 */
static void
_scsih_tm_display_info(struct MPT3SAS_ADAPTER *ioc, struct scsi_cmnd *scmd)
{
	struct scsi_target *starget = scmd->device->sdev_target;
	struct MPT3SAS_TARGET *priv_target = starget->hostdata;
	struct _sas_device *sas_device = NULL;
	struct _pcie_device *pcie_device = NULL;
	unsigned long flags;
	char *device_str = NULL;

	if (!priv_target)
		return;

	if (ioc->warpdrive_msg)
		device_str = "WarpDrive";
	else
		device_str = "volume";

	scsi_print_command(scmd);
	if (priv_target->flags & MPT_TARGET_FLAGS_VOLUME) {
		starget_printk(KERN_INFO, starget, "%s handle(0x%04x), "
		    "%s wwid(0x%016llx)\n", device_str, priv_target->handle,
		    device_str, (unsigned long long)priv_target->sas_address);

	} else if (priv_target->flags & MPT_TARGET_FLAGS_PCIE_DEVICE) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = __mpt3sas_get_pdev_from_target(ioc, priv_target);
		if (pcie_device) {
			starget_printk(KERN_INFO, starget,
				"handle(0x%04x), wwid(0x%016llx), port(%d)\n",
				pcie_device->handle,
				(unsigned long long)pcie_device->wwid,
				pcie_device->port_num);
			if(pcie_device->enclosure_handle != 0)
				starget_printk(KERN_INFO, starget,
					"enclosure logical id(0x%016llx), slot(%d)\n",
					(unsigned long long)pcie_device->enclosure_logical_id,
					pcie_device->slot);
			if(pcie_device->connector_name[0] != '\0')
				starget_printk(KERN_INFO, starget,
					"enclosure level(0x%04x), connector name( %s)\n",
					pcie_device->enclosure_level, pcie_device->connector_name);
			pcie_device_put(pcie_device);
		}
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	} else {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = __mpt3sas_get_sdev_from_target(ioc, priv_target);
		if (sas_device) {
			if (priv_target->flags &
			    MPT_TARGET_FLAGS_RAID_COMPONENT) {
				starget_printk(KERN_INFO, starget,
				    "volume handle(0x%04x), "
				    "volume wwid(0x%016llx)\n",
				    sas_device->volume_handle,
				   (unsigned long long)sas_device->volume_wwid);
			}
			starget_printk(KERN_INFO, starget,
				"%s: handle(0x%04x), sas_address(0x%016llx), phy(%d)\n",
				__func__, sas_device->handle,
				(unsigned long long)sas_device->sas_address,
				sas_device->phy);

			_scsih_display_enclosure_chassis_info(NULL, sas_device, NULL, starget);

			sas_device_put(sas_device);
		}
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	}
}

/**
 * _scsih_abort - eh threads main abort routine
 * @scmd: pointer to scsi command object
 *
 * Returns SUCCESS if command aborted else FAILED
 */
static int
_scsih_abort(struct scsi_cmnd *scmd)
{
	struct MPT3SAS_ADAPTER *ioc = shost_private(scmd->device->host);
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	u16 handle;
	int r;
	struct scsiio_tracker *st = mpt3sas_base_scsi_cmd_priv(scmd);
	u8 timeout = 30;
	struct _pcie_device *pcie_device = NULL;

	sdev_printk(KERN_INFO, scmd->device,
	    "attempting task abort! scmd(0x%p), outstanding for %u ms & timeout %u ms\n",
	    scmd, jiffies_to_msecs(jiffies - scmd->jiffies_at_alloc),
	    (scmd->request->timeout / HZ) * 1000);
	_scsih_tm_display_info(ioc, scmd);

	if (mpt3sas_base_pci_device_is_unplugged(ioc) || ioc->remove_host) {
		sdev_printk(KERN_INFO, scmd->device, "%s scmd(0x%p)\n",
		    ((ioc->remove_host)?("shost is getting removed!"):
		    ("pci device been removed!")), scmd);
		if (st && st->smid)
			mpt3sas_base_free_smid(ioc, st->smid);
		scmd->result = DID_NO_CONNECT << 16;
		r = mpt3sas_determine_failed_or_fast_io_fail_status();
		goto out;
	}

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target) {
		sdev_printk(KERN_INFO, scmd->device, "device been deleted! "
		    "scmd(0x%p)\n", scmd);
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		r = SUCCESS;
		goto out;
	}

	/* check for completed command */
	if (st == NULL || st->cb_idx == 0xFF) {
		sdev_printk(KERN_INFO, scmd->device,
		    "No reference found at driver, assuming scmd(0x%p) might have completed\n",
		    scmd);
		scmd->result = DID_RESET << 16;
		r = SUCCESS;
		goto out;
	}

	/* for hidden raid components and volumes this is not supported */
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT ||
	    sas_device_priv_data->sas_target->flags & MPT_TARGET_FLAGS_VOLUME) {
		scmd->result = DID_RESET << 16;
		r = FAILED;
		goto out;
	}

	mpt3sas_halt_firmware(ioc, 0);

	handle = sas_device_priv_data->sas_target->handle;
	pcie_device = mpt3sas_get_pdev_by_handle(ioc, handle);
	if (pcie_device && (!ioc->tm_custom_handling) &&
		(!(mpt3sas_scsih_is_pcie_scsi_device(pcie_device->device_info))))
		timeout = ioc->nvme_abort_timeout;
	r = mpt3sas_scsih_issue_locked_tm(ioc, handle, scmd->device->channel,
	    scmd->device->id, scmd->device->lun,
	    MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK, st->smid, timeout,
	    0);

 out:
	sdev_printk(KERN_INFO, scmd->device, "task abort: %s scmd(0x%p)\n",
	    ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);
	if (pcie_device)
		pcie_device_put(pcie_device);
	return r;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26))
/**
 * _scsih_dev_reset - eh threads main device reset routine
 * @scmd: pointer to scsi command object
 *
 * Returns SUCCESS if command aborted else FAILED
 */
static int
_scsih_dev_reset(struct scsi_cmnd *scmd)
{
	struct MPT3SAS_ADAPTER *ioc = shost_private(scmd->device->host);
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct _sas_device *sas_device = NULL;
	struct _pcie_device *pcie_device = NULL;
	u16	handle;
	u8	tr_method = 0;
	u8	tr_timeout = 30;
	int r;
	struct scsi_target *starget = scmd->device->sdev_target;
	struct MPT3SAS_TARGET *target_priv_data = starget->hostdata;

	if (ioc->is_warpdrive) {
		r = FAILED;
		goto out;
	}

	sdev_printk(KERN_INFO, scmd->device, "attempting device reset! "
	    "scmd(0x%p)\n", scmd);
	_scsih_tm_display_info(ioc, scmd);

	if (mpt3sas_base_pci_device_is_unplugged(ioc) || ioc->remove_host) {
		sdev_printk(KERN_INFO, scmd->device, "%s scmd(0x%p)\n",
		    ((ioc->remove_host)?("shost is getting removed!"):
		    ("pci device been removed!")), scmd);
		scmd->result = DID_NO_CONNECT << 16;
		r = mpt3sas_determine_failed_or_fast_io_fail_status();
		goto out;
	}

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target) {
		sdev_printk(KERN_INFO, scmd->device, "device been deleted! "
		    "scmd(0x%p)\n", scmd);
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		r = SUCCESS;
		goto out;
	}

	/* for hidden raid components obtain the volume_handle */
	handle = 0;
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT) {
		sas_device = mpt3sas_get_sdev_from_target(ioc,
				target_priv_data);
		if (sas_device)
			handle = sas_device->volume_handle;
	} else
		handle = sas_device_priv_data->sas_target->handle;

	if (!handle) {
		scmd->result = DID_RESET << 16;
		r = FAILED;
		goto out;
	}

	pcie_device = mpt3sas_get_pdev_by_handle(ioc, handle);

	if (pcie_device && (!ioc->tm_custom_handling) &&
		(!(mpt3sas_scsih_is_pcie_scsi_device(pcie_device->device_info)))) {
		tr_timeout = pcie_device->reset_timeout;
		tr_method = MPI26_SCSITASKMGMT_MSGFLAGS_PROTOCOL_LVL_RST_PCIE;
	}
	else
		tr_method = MPI2_SCSITASKMGMT_MSGFLAGS_LINK_RESET;

	r = mpt3sas_scsih_issue_locked_tm(ioc, handle, scmd->device->channel,
		scmd->device->id, scmd->device->lun,
		MPI2_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET, 0, tr_timeout, tr_method);
 out:
	sdev_printk(KERN_INFO, scmd->device, "device reset: %s scmd(0x%p)\n",
	    ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);

	if (sas_device)
		sas_device_put(sas_device);
	if (pcie_device)
		pcie_device_put(pcie_device);

	return r;
}

/**
 * _scsih_target_reset - eh threads main target reset routine
 * @scmd: pointer to scsi command object
 *
 * Returns SUCCESS if command aborted else FAILED
 */
static int
_scsih_target_reset(struct scsi_cmnd *scmd)
{
	struct MPT3SAS_ADAPTER *ioc = shost_private(scmd->device->host);
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct _sas_device *sas_device = NULL;
	struct _pcie_device *pcie_device = NULL;
	u16	handle;
	u8	tr_method = 0;
	u8	tr_timeout = 30;
	int r;
	struct scsi_target *starget = scmd->device->sdev_target;
	struct MPT3SAS_TARGET *target_priv_data = starget->hostdata;

	starget_printk(KERN_INFO, starget, "attempting target reset! "
	    "scmd(0x%p)\n", scmd);
	_scsih_tm_display_info(ioc, scmd);

	if (mpt3sas_base_pci_device_is_unplugged(ioc) || ioc->remove_host) {
		sdev_printk(KERN_INFO, scmd->device, "%s scmd(0x%p)\n",
		    ((ioc->remove_host)?("shost is getting removed!"):
		    ("pci device been removed!")), scmd);
		scmd->result = DID_NO_CONNECT << 16;
		r = mpt3sas_determine_failed_or_fast_io_fail_status();
		goto out;
	}

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target) {
		starget_printk(KERN_INFO, starget, "target been deleted! "
		    "scmd(0x%p)\n", scmd);
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		r = SUCCESS;
		goto out;
	}

	/* for hidden raid components obtain the volume_handle */
	handle = 0;
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT) {
		sas_device = mpt3sas_get_sdev_from_target(ioc,
				target_priv_data);
		if (sas_device)
			handle = sas_device->volume_handle;
	} else
		handle = sas_device_priv_data->sas_target->handle;

	if (!handle) {
		scmd->result = DID_RESET << 16;
		r = FAILED;
		goto out;
	}

	pcie_device = mpt3sas_get_pdev_by_handle(ioc, handle);

	if (pcie_device && (!ioc->tm_custom_handling) &&
		(!(mpt3sas_scsih_is_pcie_scsi_device(pcie_device->device_info)))) {
		tr_timeout = pcie_device->reset_timeout;
		tr_method = MPI26_SCSITASKMGMT_MSGFLAGS_PROTOCOL_LVL_RST_PCIE;
	}
	else
		tr_method = MPI2_SCSITASKMGMT_MSGFLAGS_LINK_RESET;

	r = mpt3sas_scsih_issue_locked_tm(ioc, handle, scmd->device->channel,
	    scmd->device->id, 0, MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET, 0,
	    tr_timeout, tr_method);

 out:
	starget_printk(KERN_INFO, starget, "target reset: %s scmd(0x%p)\n",
	    ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);

	if (sas_device)
		sas_device_put(sas_device);
	if (pcie_device)
		pcie_device_put(pcie_device);

	return r;
}

#else /* prior to 2.6.26 kernel */

/**
 * _scsih_dev_reset - eh threads main device reset routine
 * @scmd: pointer to scsi command object
 *
 * Returns SUCCESS if command aborted else FAILED
 */
int
_scsih_dev_reset(struct scsi_cmnd *scmd)
{
	struct MPT3SAS_ADAPTER *ioc = shost_private(scmd->device->host);
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct _sas_device *sas_device = NULL;
	unsigned long flags;
	u16	handle;
	int r;
	struct scsi_target *starget = scmd->device->sdev_target;
	struct _pcie_device *pcie_device = NULL;
	u8 tr_timeout = 30;
	u8 tr_method = 0;

	starget_printk(KERN_INFO, starget, "attempting target reset! "
	    "scmd(0x%p)\n", scmd);
	_scsih_tm_display_info(ioc, scmd);

	if (mpt3sas_base_pci_device_is_unplugged(ioc)) {
		sdev_printk(KERN_INFO, scmd->device,
		    "pci device been removed! scmd(0x%p)\n", scmd);
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		r = SUCCESS;
		goto out;
	}

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target) {
		starget_printk(KERN_INFO, starget, "target been deleted! "
		    "scmd(0x%p)\n", scmd);
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		r = SUCCESS;
		goto out;
	}

	/* for hidden raid components obtain the volume_handle */
	handle = 0;
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT) {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = __mpt3sas_get_sdev_by_handle(ioc,
		   sas_device_priv_data->sas_target->handle);
		if (sas_device)
			handle = sas_device->volume_handle;
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	} else
		handle = sas_device_priv_data->sas_target->handle;

	if (!handle) {
		scmd->result = DID_RESET << 16;
		r = FAILED;
		goto out;
	}

	pcie_device = mpt3sas_get_pdev_by_handle(ioc, handle);

	if (pcie_device && (!ioc->tm_custom_handling) &&
		(!(mpt3sas_scsih_is_pcie_scsi_device(pcie_device->device_info)))) {
		tr_timeout = pcie_device->reset_timeout;
		tr_method = MPI26_SCSITASKMGMT_MSGFLAGS_PROTOCOL_LVL_RST_PCIE;
	}
	else
		tr_method = MPI2_SCSITASKMGMT_MSGFLAGS_LINK_RESET;

	r = mpt3sas_scsih_issue_locked_tm(ioc, handle, scmd->device->channel,
	    scmd->device->id, 0, MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET, 0,
	    tr_timeout, tr_method);

 out:
	starget_printk(KERN_INFO, starget, "target reset: %s scmd(0x%p)\n",
	    ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);
	if (sas_device)
		sas_device_put(sas_device);
	if (pcie_device)
		pcie_device_put(pcie_device);
	return r;
}
#endif

/**
 * _scsih_host_reset - eh threads main host reset routine
 * @scmd: pointer to scsi command object
 *
 * Returns SUCCESS if command aborted else FAILED
 */
static int
_scsih_host_reset(struct scsi_cmnd *scmd)
{
	struct MPT3SAS_ADAPTER *ioc = shost_private(scmd->device->host);
	int r, retval;

	printk(MPT3SAS_INFO_FMT "attempting host reset! scmd(0x%p)\n",
	    ioc->name, scmd);
	scsi_print_command(scmd);

	if (ioc->is_driver_loading || ioc->remove_host){
                printk(MPT3SAS_INFO_FMT "Blocking the host reset\n",
		    ioc->name);
                r = FAILED;
                goto out;
	}

	retval = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	r = (retval < 0) ? FAILED : SUCCESS;

 out:
	printk(MPT3SAS_INFO_FMT "host reset: %s scmd(0x%p)\n",
	    ioc->name, ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);

	return r;
}

/**
 * _scsih_fw_event_add - insert and queue up fw_event
 * @ioc: per adapter object
 * @fw_event: object describing the event
 * Context: This function will acquire ioc->fw_event_lock.
 *
 * This adds the firmware event object into link list, then queues it up to
 * be processed from user context.
 *
 * Return nothing.
 */
static void
_scsih_fw_event_add(struct MPT3SAS_ADAPTER *ioc, struct fw_event_work *fw_event)
{
	unsigned long flags;

	if (ioc->firmware_event_thread == NULL)
		return;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	fw_event_work_get(fw_event);
	INIT_LIST_HEAD(&fw_event->list);
	list_add_tail(&fw_event->list, &ioc->fw_event_list);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	INIT_WORK(&fw_event->work, _firmware_event_work);
#else
	INIT_WORK(&fw_event->work, _firmware_event_work, (void *)fw_event);
#endif
	fw_event_work_get(fw_event);
	queue_work(ioc->firmware_event_thread, &fw_event->work);
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/**
 * _scsih_fw_event_del_from_list - delete fw_event from the list
 * @ioc: per adapter object
 * @fw_event: object describing the event
 * Context: This function will acquire ioc->fw_event_lock.
 *
 * If the fw_event is on the fw_event_list, remove it and do a put.
 *
 * Return nothing.
 */
static void
_scsih_fw_event_del_from_list(struct MPT3SAS_ADAPTER *ioc, struct fw_event_work
	*fw_event)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	if (!list_empty(&fw_event->list)) {
		list_del_init(&fw_event->list);
		fw_event_work_put(fw_event);
	}
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/**
 * _scsih_fw_event_requeue - requeue an event
 * @ioc: per adapter object
 * @fw_event: object describing the event
 * Context: This function will acquire ioc->fw_event_lock.
 *
 * Return nothing.
 */
static void
_scsih_fw_event_requeue(struct MPT3SAS_ADAPTER *ioc, struct fw_event_work
	*fw_event, unsigned long delay)
{
	unsigned long flags;

	if (ioc->firmware_event_thread == NULL)
		return;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	fw_event_work_get(fw_event);
	list_add_tail(&fw_event->list, &ioc->fw_event_list);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	if (!fw_event->delayed_work_active) {
		fw_event->delayed_work_active = 1;
		INIT_DELAYED_WORK(&fw_event->delayed_work,
		    _firmware_event_work_delayed);
	}
	queue_delayed_work(ioc->firmware_event_thread, &fw_event->delayed_work,
	    msecs_to_jiffies(delay));
#else
	queue_delayed_work(ioc->firmware_event_thread, &fw_event->work,
	    msecs_to_jiffies(delay));
#endif
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

 /**
 * mpt3sas_send_trigger_data_event - send event for processing trigger data
 * @ioc: per adapter object
 * @event_data: trigger event data
 *
 * Return nothing.
 */
void
mpt3sas_send_trigger_data_event(struct MPT3SAS_ADAPTER *ioc,
	struct SL_WH_TRIGGERS_EVENT_DATA_T *event_data)
{
	struct fw_event_work *fw_event;

	if (ioc->is_driver_loading)
		return;
	fw_event = alloc_fw_event_work(sizeof(*event_data));
	if (!fw_event)
		return;
	fw_event->event_data = kzalloc(sizeof(*event_data), GFP_ATOMIC);
	if (!fw_event->event_data) {
		fw_event_work_put(fw_event);
		return;
	}
	fw_event->event = MPT3SAS_PROCESS_TRIGGER_DIAG;
	fw_event->ioc = ioc;
	memcpy(fw_event->event_data, event_data, sizeof(*event_data));
	_scsih_fw_event_add(ioc, fw_event);
	fw_event_work_put(fw_event);
}

/**
 * _scsih_error_recovery_delete_devices - remove devices not responding
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_scsih_error_recovery_delete_devices(struct MPT3SAS_ADAPTER *ioc)
{
	struct fw_event_work *fw_event;

	if (ioc->is_driver_loading)
		return;
	fw_event = alloc_fw_event_work(0);
	if (!fw_event)
		return;
	fw_event->event = MPT3SAS_REMOVE_UNRESPONDING_DEVICES;
	fw_event->ioc = ioc;
	_scsih_fw_event_add(ioc, fw_event);
	fw_event_work_put(fw_event);
}

/**
 * mpt3sas_port_enable_complete - port enable completed (fake event)
 * @ioc: per adapter object
 *
 * Return nothing.
 */
void
mpt3sas_port_enable_complete(struct MPT3SAS_ADAPTER *ioc)
{
	struct fw_event_work *fw_event;

	fw_event = alloc_fw_event_work(0);
	if (!fw_event)
		return;
	fw_event->event = MPT3SAS_PORT_ENABLE_COMPLETE;
	fw_event->ioc = ioc;
	_scsih_fw_event_add(ioc, fw_event);
	fw_event_work_put(fw_event);
}

/**
 * dequeue_next_fw_event  - Dequeue the firmware event from list
 * @ioc: per adapter object
 *
 * dequeue the firmware event from firmware event list
 *
 * Return firmware event.
 */
static struct fw_event_work *dequeue_next_fw_event(struct MPT3SAS_ADAPTER *ioc)
{
	unsigned long flags;
	struct fw_event_work *fw_event = NULL;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	if (!list_empty(&ioc->fw_event_list)) {
		fw_event = list_first_entry(&ioc->fw_event_list,
				struct fw_event_work, list);
		list_del_init(&fw_event->list);
	}
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);

	return fw_event;
}

/**
 * _scsih_fw_event_cleanup_queue - cleanup event queue
 * @ioc: per adapter object
 *
 * Walk the firmware event queue, either killing timers, or waiting
 * for outstanding events to complete
 *
 * Return nothing.
 */
static void
_scsih_fw_event_cleanup_queue(struct MPT3SAS_ADAPTER *ioc)
{
	struct fw_event_work *fw_event;

	if ((list_empty(&ioc->fw_event_list) && !ioc->current_event) ||
	     !ioc->firmware_event_thread || in_interrupt())
		return;

	ioc->fw_events_cleanup = 1;

	while ((fw_event = dequeue_next_fw_event(ioc)) ||
	     (fw_event = ioc->current_event)) {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
		if (fw_event->delayed_work_active)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23))
			cancel_delayed_work_sync(&fw_event->delayed_work);
#else
			cancel_delayed_work(&fw_event->delayed_work);
#endif
		else
			cancel_work_sync(&fw_event->work);

		fw_event_work_put(fw_event);

#else
		cancel_delayed_work(&fw_event->work);
		fw_event_work_put(fw_event);

#endif
		fw_event_work_put(fw_event);
	}

	ioc->fw_events_cleanup = 0;
}

 /**
 * _scsih_internal_device_block - block the sdev device
 * @sdev: per device object
 * @sas_device_priv_data : per device driver private data
 *
 * make sure device is blocked without error, if not
 * print an error
 */
static void
_scsih_internal_device_block(struct scsi_device *sdev,
			struct MPT3SAS_DEVICE *sas_device_priv_data)
{
	int r = 0;

	sdev_printk(KERN_INFO, sdev, "device_block, handle(0x%04x)\n",
	    sas_device_priv_data->sas_target->handle);
	sas_device_priv_data->block = 1;

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0)) || \
	(defined(RHEL_MAJOR) && (RHEL_MAJOR == 7) && (RHEL_MINOR > 3)) || \
	(defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,14)))
	r = scsi_internal_device_block_nowait(sdev);
#elif ((defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,59)) || LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0))
	r = scsi_internal_device_block(sdev, false);
#else
	r = scsi_internal_device_block(sdev);
#endif

	if (r == -EINVAL)
		sdev_printk(KERN_WARNING, sdev,
		    "device_block failed with return(%d) for handle(0x%04x)\n",
		    r, sas_device_priv_data->sas_target->handle);
}

/**
 * _scsih_internal_device_unblock - unblock the sdev device
 * @sdev: per device object
 * @sas_device_priv_data : per device driver private data
 * make sure device is unblocked without error, if not retry
 * by blocking and then unblocking
 */

static void
_scsih_internal_device_unblock(struct scsi_device *sdev,
			struct MPT3SAS_DEVICE *sas_device_priv_data)
{
	int r = 0;

	sdev_printk(KERN_WARNING, sdev, "device_unblock and setting to running, "
	    "handle(0x%04x)\n", sas_device_priv_data->sas_target->handle);
	sas_device_priv_data->block = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0) || \
	(defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,14)))
	r = scsi_internal_device_unblock_nowait(sdev, SDEV_RUNNING);
#elif ((defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,70)) || LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
	r = scsi_internal_device_unblock(sdev, SDEV_RUNNING);
#else
	r = scsi_internal_device_unblock(sdev);
#endif

	if (r == -EINVAL) {
		/* The device has been set to SDEV_RUNNING by SD layer during
		 * device addition but the request queue is still stopped by
		 * our earlier block call. We need to perform a block again
		 * to get the device to SDEV_BLOCK and then to SDEV_RUNNING */

		sdev_printk(KERN_WARNING, sdev,
		    "device_unblock failed with return(%d) for handle(0x%04x) "
		    "performing a block followed by an unblock\n",
		    r, sas_device_priv_data->sas_target->handle);
		sas_device_priv_data->block = 1;
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0)) || \
	(defined(RHEL_MAJOR) && (RHEL_MAJOR == 7) && (RHEL_MINOR > 3)) || \
	(defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,14)))
		r = scsi_internal_device_block_nowait(sdev);
#elif ((defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,59)) || LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0))
		r = scsi_internal_device_block(sdev, false);
#else
		r = scsi_internal_device_block(sdev);
#endif
		if (r)
			sdev_printk(KERN_WARNING, sdev, "retried device_block "
			    "failed with return(%d) for handle(0x%04x)\n",
			    r, sas_device_priv_data->sas_target->handle);

		sas_device_priv_data->block = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0) || \
    (defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,14)))
		r = scsi_internal_device_unblock_nowait(sdev, SDEV_RUNNING);
#elif ((defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,70)) || LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
		r = scsi_internal_device_unblock(sdev, SDEV_RUNNING);
#else
		r = scsi_internal_device_unblock(sdev);
#endif
		if (r)
			sdev_printk(KERN_WARNING, sdev, "retried device_unblock"
			    " failed with return(%d) for handle(0x%04x)\n",
			    r, sas_device_priv_data->sas_target->handle);
	}
}

/**
 * _scsih_ublock_io_all_device - unblock every device
 * @ioc: per adapter object
 * @no_turs: don't issue TEST_UNIT_READY
 *
 * make sure device is reponsponding before unblocking
 */
static void
_scsih_ublock_io_all_device(struct MPT3SAS_ADAPTER *ioc, u8 no_turs)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct MPT3SAS_TARGET *sas_target;
	enum device_responsive_state rc;
	struct scsi_device *sdev;
	struct _sas_device *sas_device = NULL;
	struct _pcie_device *pcie_device = NULL;
	int count;
	u8 tr_timeout = 30;
	u8 tr_method = 0;

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		sas_target = sas_device_priv_data->sas_target;
		if (!sas_target || sas_target->deleted)
			continue;
		if (!sas_device_priv_data->block)
			continue;
		count = 0;
		if ((no_turs) || (!issue_scsi_cmd_to_bringup_drive)) {
			sdev_printk(KERN_WARNING, sdev, "device_unblocked, "
			    "handle(0x%04x)\n",
			    sas_device_priv_data->sas_target->handle);
			_scsih_internal_device_unblock(sdev, sas_device_priv_data);
			continue;
		}

		do {
			pcie_device = mpt3sas_get_pdev_by_handle(ioc, sas_target->handle);
			if (pcie_device && (!ioc->tm_custom_handling) &&
				(!(mpt3sas_scsih_is_pcie_scsi_device(pcie_device->device_info)))) {
				tr_timeout = pcie_device->reset_timeout;
				tr_method = MPI26_SCSITASKMGMT_MSGFLAGS_PROTOCOL_LVL_RST_PCIE;
			}
			rc = _scsih_wait_for_device_to_become_ready(ioc,
			    sas_target->handle, 0, (sas_target->flags &
			    MPT_TARGET_FLAGS_RAID_COMPONENT), sdev->lun, tr_timeout, tr_method);
			if (rc == DEVICE_RETRY || rc == DEVICE_START_UNIT ||
			    rc == DEVICE_STOP_UNIT ||rc == DEVICE_RETRY_UA)
				ssleep(1);
			if (pcie_device)
				pcie_device_put(pcie_device);
		} while ((rc == DEVICE_RETRY || rc == DEVICE_START_UNIT ||
		    rc == DEVICE_STOP_UNIT ||rc == DEVICE_RETRY_UA)
			&& count++ < command_retry_count);
		sas_device_priv_data->block = 0;
		if (rc != DEVICE_READY)
			sas_device_priv_data->deleted = 1;

		_scsih_internal_device_unblock(sdev, sas_device_priv_data);

		if (rc != DEVICE_READY) {
			sdev_printk(KERN_WARNING, sdev, "%s: device_offlined, "
			    "handle(0x%04x)\n",
			    __func__, sas_device_priv_data->sas_target->handle);
			scsi_device_set_state(sdev, SDEV_OFFLINE);
			sas_device = mpt3sas_get_sdev_by_addr(ioc,
					sas_device_priv_data->sas_target->sas_address,
					sas_device_priv_data->sas_target->port);
			if (sas_device) {
				_scsih_display_enclosure_chassis_info(NULL, sas_device, sdev, NULL);
				sas_device_put(sas_device);
			} else {
				pcie_device = mpt3sas_get_pdev_by_wwid(ioc,
								sas_device_priv_data->sas_target->sas_address);
				if (pcie_device) {
					if(pcie_device->enclosure_handle != 0)
						sdev_printk(KERN_INFO, sdev, "enclosure logical id"
							"(0x%016llx), slot(%d) \n", (unsigned long long)
							pcie_device->enclosure_logical_id,
							pcie_device->slot);
					if(pcie_device->connector_name[0] != '\0')
						sdev_printk(KERN_INFO, sdev, "enclosure level(0x%04x),"
							" connector name( %s)\n",
							pcie_device->enclosure_level,
							pcie_device->connector_name);
					pcie_device_put(pcie_device);
				}
			}
		} else
			sdev_printk(KERN_WARNING, sdev, "device_unblocked, "
			    "handle(0x%04x)\n",
			    sas_device_priv_data->sas_target->handle);
	}
}

/**
 * _scsih_ublock_io_device_wait - unblock IO for target
 * @ioc: per adapter object
 * @sas_addr: sas address
 * @port: hba port entry
 *
 * make sure device is reponsponding before unblocking
 */
static void
_scsih_ublock_io_device_wait(struct MPT3SAS_ADAPTER *ioc, u64 sas_address,
			     struct hba_port *port)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct MPT3SAS_TARGET *sas_target;
	enum device_responsive_state rc;
	struct scsi_device *sdev;
	int count, host_reset_completion_count;
	struct _sas_device *sas_device;
	struct _pcie_device *pcie_device;
	u8 tr_timeout = 30;
	u8 tr_method = 0;

	/* moving devices from SDEV_OFFLINE to SDEV_BLOCK */
	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		sas_target = sas_device_priv_data->sas_target;
		if (!sas_target)
			continue;
		if (sas_target->sas_address != sas_address ||
		    sas_target->port != port)
			continue;
		if (sdev->sdev_state == SDEV_OFFLINE) {
			sas_device_priv_data->block = 1;
			sas_device_priv_data->deleted = 0;
			scsi_device_set_state(sdev, SDEV_RUNNING);
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0)) || \
	(defined(RHEL_MAJOR) && (RHEL_MAJOR == 7) && (RHEL_MINOR > 3)) || \
	(defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,14)))
			scsi_internal_device_block_nowait(sdev);
#elif ((defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,59)) || LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0))
			scsi_internal_device_block(sdev, false);
#else
			scsi_internal_device_block(sdev);
#endif
		}
	}

	/* moving devices from SDEV_BLOCK to SDEV_RUNNING state */
	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		sas_target = sas_device_priv_data->sas_target;
		if (!sas_target)
			continue;
		if (sas_target->sas_address != sas_address ||
		    sas_target->port != port)
			continue;
		if (!sas_device_priv_data->block)
			continue;
		count = 0;
		do {
			host_reset_completion_count = 0;
			pcie_device = mpt3sas_get_pdev_by_handle(ioc, sas_target->handle);
			if (pcie_device && (!ioc->tm_custom_handling) &&
				(!(mpt3sas_scsih_is_pcie_scsi_device(pcie_device->device_info)))) {
				tr_timeout = pcie_device->reset_timeout;
				tr_method = MPI26_SCSITASKMGMT_MSGFLAGS_PROTOCOL_LVL_RST_PCIE;
			}
			rc = _scsih_wait_for_device_to_become_ready(ioc,
			      sas_target->handle, 0, (sas_target->flags &
			      MPT_TARGET_FLAGS_RAID_COMPONENT), sdev->lun, tr_timeout, tr_method);
			if (rc == DEVICE_RETRY || rc == DEVICE_START_UNIT ||
			    rc == DEVICE_STOP_UNIT ||rc == DEVICE_RETRY_UA) {
				do {
					msleep(500);
					host_reset_completion_count++;
				} while (rc == DEVICE_RETRY &&
							ioc->shost_recovery);
				if (host_reset_completion_count > 1) {
					rc = _scsih_wait_for_device_to_become_ready(ioc,
						sas_target->handle, 0, (sas_target->flags &
						MPT_TARGET_FLAGS_RAID_COMPONENT), sdev->lun,
						tr_timeout, tr_method);
					if (rc == DEVICE_RETRY || rc == DEVICE_START_UNIT ||
		                            rc == DEVICE_STOP_UNIT ||rc == DEVICE_RETRY_UA)
						msleep(500);
				}
				continue;
			}
			if (pcie_device)
				pcie_device_put(pcie_device);
		} while ((rc == DEVICE_RETRY || rc == DEVICE_START_UNIT ||
		    rc == DEVICE_STOP_UNIT ||rc == DEVICE_RETRY_UA)
			&& count++ <= command_retry_count);

		sas_device_priv_data->block = 0;
		if (rc != DEVICE_READY)
			sas_device_priv_data->deleted = 1;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0) || \
    (defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,14)))
		scsi_internal_device_unblock_nowait(sdev, SDEV_RUNNING);
#elif ((defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,70)) || LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
		scsi_internal_device_unblock(sdev, SDEV_RUNNING);
#else
		scsi_internal_device_unblock(sdev);
#endif

		if (rc != DEVICE_READY) {
			sdev_printk(KERN_WARNING, sdev,
			    "%s: device_offlined, handle(0x%04x)\n",
			    __func__, sas_device_priv_data->sas_target->handle);

			sas_device = mpt3sas_get_sdev_by_handle(ioc,
				sas_device_priv_data->sas_target->handle);
			if (sas_device) {
				_scsih_display_enclosure_chassis_info(NULL, sas_device, sdev, NULL);
				sas_device_put(sas_device);
			} else {
				pcie_device = mpt3sas_get_pdev_by_handle(ioc,
								sas_device_priv_data->sas_target->handle);
				if (pcie_device) {
					if(pcie_device->enclosure_handle != 0)
						sdev_printk(KERN_INFO, sdev,
							"device_offlined, enclosure logical id(0x%016llx),"
							" slot(%d)\n", (unsigned long long)
							pcie_device->enclosure_logical_id,
							pcie_device->slot);
					if(pcie_device->connector_name[0] != '\0')
						sdev_printk(KERN_WARNING, sdev,
							"device_offlined, enclosure level(0x%04x), "
							"connector name( %s)\n",
							pcie_device->enclosure_level,
							pcie_device->connector_name);
					pcie_device_put(pcie_device);
				}
			}
			scsi_device_set_state(sdev, SDEV_OFFLINE);
		} else {
			sdev_printk(KERN_WARNING, sdev,
				"device_unblocked, handle(0x%04x)\n",
				sas_device_priv_data->sas_target->handle);
		}
	}
}

/**
 * _scsih_ublock_io_device - prepare device to be deleted
 * @ioc: per adapter object
 * @sas_addr: sas address
 * @port: hba port entry
 *
 * unblock then put device in offline state
 */
static void
_scsih_ublock_io_device(struct MPT3SAS_ADAPTER *ioc, u64 sas_address,
			struct hba_port *port)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->sas_target->sas_address
		    != sas_address ||
		    sas_device_priv_data->sas_target->port
		    != port)
			continue;
		if (sas_device_priv_data->block) {
			_scsih_internal_device_unblock(sdev, sas_device_priv_data);
		}
		scsi_device_set_state(sdev, SDEV_OFFLINE);
	}
}

/**
 * _scsih_ublock_io_device_to_running - set the device state to SDEV_RUNNING
 * @ioc: per adapter object
 * @sas_addr: sas address
 * @port: hab port entry
 *
 * unblock the device to receive IO during device addition. Device
 * responsiveness is not checked before unblocking
 */
static void
_scsih_ublock_io_device_to_running(struct MPT3SAS_ADAPTER *ioc, u64 sas_address,
				   struct hba_port *port)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->sas_target->sas_address
		     != sas_address ||
		     sas_device_priv_data->sas_target->port
		     != port)
			continue;
		if (sas_device_priv_data->block) {
			sas_device_priv_data->block = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0) || \
    (defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(4,12,14)))
			scsi_internal_device_unblock_nowait(sdev, SDEV_RUNNING);
#elif ((defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,70)) || LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
			scsi_internal_device_unblock(sdev, SDEV_RUNNING);
#else
			scsi_internal_device_unblock(sdev);
#endif
			sdev_printk(KERN_WARNING, sdev,
				"device_unblocked, handle(0x%04x)\n",
				sas_device_priv_data->sas_target->handle);
		}
	}
}

/**
 * _scsih_block_io_all_device - set the device state to SDEV_BLOCK
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During device pull we need to appropiately set the sdev state.
 */
static void
_scsih_block_io_all_device(struct MPT3SAS_ADAPTER *ioc)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->block)
			continue;
		if (sas_device_priv_data->ignore_delay_remove) {
			sdev_printk(KERN_INFO, sdev,
			"%s skip device_block for SES handle(0x%04x)\n",
			__func__, sas_device_priv_data->sas_target->handle);
			continue;
		}
		_scsih_internal_device_block(sdev, sas_device_priv_data);
	}
}

/**
 * _scsih_block_io_device - set the device state to SDEV_BLOCK
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During device pull we need to appropiately set the sdev state.
 */
static void
_scsih_block_io_device(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;
	struct _sas_device *sas_device;

	sas_device = mpt3sas_get_sdev_by_handle(ioc, handle);

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->sas_target->handle != handle)
			continue;
		if (sas_device_priv_data->block)
			continue;
		if (sas_device && sas_device->pend_sas_rphy_add)
			continue;
		if (sas_device_priv_data->ignore_delay_remove) {
			sdev_printk(KERN_INFO, sdev,
			"%s skip device_block for SES handle(0x%04x)\n",
			__func__, sas_device_priv_data->sas_target->handle);
			continue;
		}
		_scsih_internal_device_block(sdev, sas_device_priv_data);
	}
	if (sas_device)
		sas_device_put(sas_device);
}

/**
 * _scsih_block_io_to_children_attached_to_ex
 * @ioc: per adapter object
 * @sas_expander: the sas_device object
 *
 * This routine set sdev state to SDEV_BLOCK for all devices
 * attached to this expander. This function called when expander is
 * pulled.
 */
static void
_scsih_block_io_to_children_attached_to_ex(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_node *sas_expander)
{
	struct _sas_port *mpt3sas_port;
	struct _sas_device *sas_device;
	struct _sas_node *expander_sibling;
	unsigned long flags;

	if (!sas_expander)
		return;

	list_for_each_entry(mpt3sas_port,
	   &sas_expander->sas_port_list, port_list) {
		if (mpt3sas_port->remote_identify.device_type ==
		    SAS_END_DEVICE) {
			spin_lock_irqsave(&ioc->sas_device_lock, flags);
			sas_device = __mpt3sas_get_sdev_by_addr(ioc,
				mpt3sas_port->remote_identify.sas_address,
				mpt3sas_port->hba_port);
			if (sas_device) {
				set_bit(sas_device->handle,
				    ioc->blocking_handles);
				sas_device_put(sas_device);
			}
			spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		}
	}

	list_for_each_entry(mpt3sas_port,
	   &sas_expander->sas_port_list, port_list) {

		if (mpt3sas_port->remote_identify.device_type ==
		    SAS_EDGE_EXPANDER_DEVICE ||
		    mpt3sas_port->remote_identify.device_type ==
		    SAS_FANOUT_EXPANDER_DEVICE) {
			expander_sibling =
			    mpt3sas_scsih_expander_find_by_sas_address(
			    ioc, mpt3sas_port->remote_identify.sas_address,
			    mpt3sas_port->hba_port);
			_scsih_block_io_to_children_attached_to_ex(ioc,
			    expander_sibling);
		}
	}
}

/**
 * _scsih_block_io_to_children_attached_directly
 * @ioc: per adapter object
 * @event_data: topology change event data
 *
 * This routine set sdev state to SDEV_BLOCK for all devices
 * direct attached during device pull/reconnect.
 */
static void
_scsih_block_io_to_children_attached_directly(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataSasTopologyChangeList_t *event_data)
{
	int i;
	u16 handle;
	u16 reason_code;

	for (i = 0; i < event_data->NumEntries; i++) {
		handle = le16_to_cpu(event_data->PHY[i].AttachedDevHandle);
		if (!handle)
			continue;
		reason_code = event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_RC_MASK;
		if (reason_code == MPI2_EVENT_SAS_TOPO_RC_DELAY_NOT_RESPONDING)
			_scsih_block_io_device(ioc, handle);
	}
}

/**
 * _scsih_block_io_to_pcie_children_attached_directly
 * @ioc: per adapter object
 * @event_data: topology change event data
 *
 * This routine set sdev state to SDEV_BLOCK for all devices
 * direct attached during device pull/reconnect.
 */
static void
_scsih_block_io_to_pcie_children_attached_directly( struct MPT3SAS_ADAPTER *ioc,
	Mpi26EventDataPCIeTopologyChangeList_t *event_data)
{
	int i;
	u16 handle;
	u16 reason_code;

	for (i = 0; i < event_data->NumEntries; i++) {
		handle = le16_to_cpu(event_data->PortEntry[i].AttachedDevHandle);
		if (!handle)
			continue;
		reason_code = event_data->PortEntry[i].PortStatus;
		if (reason_code == MPI26_EVENT_PCIE_TOPO_PS_DELAY_NOT_RESPONDING)
			_scsih_block_io_device(ioc, handle);
	}
}
/**
 * _scsih_tm_tr_send - send task management request
 * @ioc: per adapter object
 * @handle: device handle
 * Context: interrupt time.
 *
 * This code is to initiate the device removal handshake protocol
 * with controller firmware.  This function will issue target reset
 * using high priority request queue.  It will send a sas iounit
 * control request (MPI2_SAS_OP_REMOVE_DEVICE) from this completion.
 *
 * This is designed to send muliple task management request at the same
 * time to the fifo. If the fifo is full, we will append the request,
 * and process it in a future completion.
 */
static void
_scsih_tm_tr_send(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	Mpi2SCSITaskManagementRequest_t *mpi_request;
	u16 smid;
	struct _sas_device *sas_device = NULL;
	struct _pcie_device *pcie_device = NULL;
	struct MPT3SAS_TARGET *sas_target_priv_data = NULL;
	u64 sas_address = 0;
	unsigned long flags;
	struct _tr_list *delayed_tr;
	u32 ioc_state;
	struct hba_port *port = NULL;
	u8 tr_method = 0;

	if (ioc->pci_error_recovery) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: host in pci "
		    "error recovery: handle(0x%04x)\n", __func__, ioc->name,
		    handle));
		return;
	}
	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	if (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: host is not "
		   "operational: handle(0x%04x)\n", __func__, ioc->name,
		   handle));
		return;
	}

	/* if PD, then return */
	if (test_bit(handle, ioc->pd_handles))
		return;

	clear_bit(handle, ioc->pend_os_device_add);

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_handle(ioc, handle);
	if (sas_device && sas_device->starget &&
	    sas_device->starget->hostdata) {
		sas_target_priv_data = sas_device->starget->hostdata;
		sas_target_priv_data->deleted = 1;
		sas_address = sas_device->sas_address;
		port = sas_device->port;
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (!sas_device) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = __mpt3sas_get_pdev_by_handle(ioc, handle);
		if (pcie_device && pcie_device->starget &&
			pcie_device->starget->hostdata) {
			sas_target_priv_data = pcie_device->starget->hostdata;
			sas_target_priv_data->deleted = 1;
			sas_address =pcie_device->wwid;
		}
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		if (pcie_device && (!ioc->tm_custom_handling) &&
			(!(mpt3sas_scsih_is_pcie_scsi_device(pcie_device->device_info))))
			tr_method = MPI26_SCSITASKMGMT_MSGFLAGS_PROTOCOL_LVL_RST_PCIE;
		else
			tr_method = MPI2_SCSITASKMGMT_MSGFLAGS_LINK_RESET;
	}
	if (sas_target_priv_data) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: setting delete flag: "
			"handle(0x%04x), sas_addr(0x%016llx)\n", ioc->name, __func__,
			handle, (unsigned long long)sas_address));
		if (sas_device) {
			dewtprintk(ioc,	_scsih_display_enclosure_chassis_info(ioc, sas_device,
	    			NULL, NULL));
		} else if (pcie_device) {
			if (pcie_device->enclosure_handle != 0)
				dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "setting delete flag: "
					"enclosure logical id(0x%016llx), slot(%d) \n",ioc->name,
					(unsigned long long)pcie_device->enclosure_logical_id,
					pcie_device->slot));
			if(pcie_device->connector_name[0] != '\0')
				dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "setting delete flag: "
					"enclosure level(0x%04x), connector name( %s)\n", ioc->name,
					pcie_device->enclosure_level, pcie_device->connector_name));
		}
		_scsih_ublock_io_device(ioc, sas_address, port);
		sas_target_priv_data->handle = MPT3SAS_INVALID_DEVICE_HANDLE;
	}

	smid = mpt3sas_base_get_smid_hpr(ioc, ioc->tm_tr_cb_idx);
	if (!smid) {
		delayed_tr = kzalloc(sizeof(*delayed_tr), GFP_ATOMIC);
		if (!delayed_tr)
			goto out;
		INIT_LIST_HEAD(&delayed_tr->list);
		delayed_tr->handle = handle;
		list_add_tail(&delayed_tr->list, &ioc->delayed_tr_list);
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
		    "DELAYED:tr:handle(0x%04x), (open)\n",
		    ioc->name, handle));
		goto out;
	}

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "tr_send:handle(0x%04x), "
	    "(open), smid(%d), cb(%d)\n", ioc->name, handle, smid,
	    ioc->tm_tr_cb_idx));
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, sizeof(Mpi2SCSITaskManagementRequest_t));
	mpi_request->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
	mpi_request->DevHandle = cpu_to_le16(handle);
	mpi_request->TaskType = MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	mpi_request->MsgFlags = tr_method;
	set_bit(handle, ioc->device_remove_in_progress);
	ioc->put_smid_hi_priority(ioc, smid, 0);
	mpt3sas_trigger_master(ioc, MASTER_TRIGGER_DEVICE_REMOVAL);
out:
	if (sas_device)
		sas_device_put(sas_device);
	if (pcie_device)
		pcie_device_put(pcie_device);
}

/**
 * _scsih_tm_tr_complete -
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: interrupt time.
 *
 * This is the target reset completion routine.
 * This code is part of the code to initiate the device removal
 * handshake protocol with controller firmware.
 * It will send a sas iounit control request (MPI2_SAS_OP_REMOVE_DEVICE) or
 * iounit control request (MPI26_CTRL_OP_REMOVE_DEVICE) based on controller's
 * MPI version
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_tm_tr_complete(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply)
{
	u16 handle;
	Mpi2SCSITaskManagementRequest_t *mpi_request_tm;
	Mpi2SCSITaskManagementReply_t *mpi_reply =
	    mpt3sas_base_get_reply_virt_addr(ioc, reply);
	Mpi2SasIoUnitControlRequest_t *mpi_request;
	u16 smid_sas_ctrl;
	u32 ioc_state;
	struct _sc_list *delayed_sc;

	if (ioc->pci_error_recovery) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: host in pci "
		    "error recovery\n", __func__, ioc->name));
		return 1;
	}
	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	if (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: host is not "
		    "operational\n", __func__, ioc->name));
		return 1;
	}
	if (unlikely(!mpi_reply)) {
		printk(MPT3SAS_ERR_FMT "mpi_reply not valid at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 1;
	}
	mpi_request_tm = mpt3sas_base_get_msg_frame(ioc, smid);
	handle = le16_to_cpu(mpi_request_tm->DevHandle);
	if (handle != le16_to_cpu(mpi_reply->DevHandle)) {
		dewtprintk(ioc, printk(MPT3SAS_ERR_FMT "spurious interrupt: "
		    "handle(0x%04x:0x%04x), smid(%d)!!!\n", ioc->name, handle,
		    le16_to_cpu(mpi_reply->DevHandle), smid));
		return 0;
	}

	mpt3sas_trigger_master(ioc, MASTER_TRIGGER_TASK_MANAGMENT);
	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
	    "tr_complete:handle(0x%04x), (open) smid(%d), ioc_status(0x%04x), "
	    "loginfo(0x%08x), completed(%d)\n", ioc->name,
	    handle, smid, le16_to_cpu(mpi_reply->IOCStatus),
	    le32_to_cpu(mpi_reply->IOCLogInfo),
	    le32_to_cpu(mpi_reply->TerminationCount)));

	smid_sas_ctrl = mpt3sas_base_get_smid(ioc, ioc->tm_sas_control_cb_idx);
	if (!smid_sas_ctrl) {
		delayed_sc = kzalloc(sizeof(*delayed_sc), GFP_ATOMIC);
		if (!delayed_sc)
			return _scsih_check_for_pending_tm(ioc, smid);
		INIT_LIST_HEAD(&delayed_sc->list);
		delayed_sc->handle = le16_to_cpu(mpi_request_tm->DevHandle);
		list_add_tail(&delayed_sc->list, &ioc->delayed_sc_list);
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
		   "DELAYED:sc:handle(0x%04x), (open)\n",
		   ioc->name, handle));
		return _scsih_check_for_pending_tm(ioc, smid);
	}

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "sc_send:handle(0x%04x), "
	    "(open), smid(%d), cb(%d)\n", ioc->name, handle, smid_sas_ctrl,
	    ioc->tm_sas_control_cb_idx));
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid_sas_ctrl);
	if (ioc->hba_mpi_version_belonged < MPI26_VERSION ) {
		memset(mpi_request, 0, sizeof(Mpi2SasIoUnitControlRequest_t));
		mpi_request->Function = MPI2_FUNCTION_SAS_IO_UNIT_CONTROL;
		mpi_request->Operation = MPI2_SAS_OP_REMOVE_DEVICE;
	} else {
		memset(mpi_request, 0, sizeof(Mpi26IoUnitControlRequest_t));
		mpi_request->Function = MPI2_FUNCTION_IO_UNIT_CONTROL;
		mpi_request->Operation = MPI26_CTRL_OP_REMOVE_DEVICE;
	}
	mpi_request->DevHandle = mpi_request_tm->DevHandle;
	ioc->put_smid_default(ioc, smid_sas_ctrl);

	return _scsih_check_for_pending_tm(ioc, smid);
}

/** _scsih_allow_scmd_to_device - check whether scmd needs to
 *				 issue to IOC or not.
 * @ioc: per adapter object
 * @scmd: pointer to scsi command object
 *
 * Returns true if scmd can be issued to IOC otherwise returns false.
 */
inline bool _scsih_allow_scmd_to_device(struct MPT3SAS_ADAPTER *ioc,
	struct scsi_cmnd *scmd)
{

	if (ioc->pci_error_recovery)
		return false;

	if (ioc->hba_mpi_version_belonged == MPI2_VERSION) {
		if (ioc->remove_host)
			return false;

		return true;
	}

	if (ioc->adapter_over_temp)
		return false;

	if (ioc->remove_host) {
		if (mpt3sas_base_pci_device_is_unplugged(ioc))
			return false;

		switch (scmd->cmnd[0]) {
		case SYNCHRONIZE_CACHE:
		case START_STOP:
			return true;
		default:
			return false;
		}
	}

	return true;
}

/**
 * _scsih_sas_control_complete - completion routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: interrupt time.
 *
 * This is the sas iounit / iounit control completion routine.
 * This code is part of the code to initiate the device removal
 * handshake protocol with controller firmware.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_sas_control_complete(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u8 msix_index, u32 reply)
{
	MPI2DefaultReply_t *mpi_reply =
		mpt3sas_base_get_reply_virt_addr(ioc, reply);
	u16 dev_handle;


	if (likely(mpi_reply)) {
		if (ioc->hba_mpi_version_belonged < MPI26_VERSION ) {
			dev_handle = ((Mpi2SasIoUnitControlReply_t *)mpi_reply)->DevHandle;
		} else {
			dev_handle = ((Mpi26IoUnitControlReply_t *)mpi_reply)->DevHandle;
		}

		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
			"sc_complete:handle(0x%04x), (open) "
			"smid(%d), ioc_status(0x%04x), loginfo(0x%08x)\n",
		ioc->name, le16_to_cpu(dev_handle), smid,
		le16_to_cpu(mpi_reply->IOCStatus),
		le32_to_cpu(mpi_reply->IOCLogInfo)));
		if (le16_to_cpu(mpi_reply->IOCStatus) == MPI2_IOCSTATUS_SUCCESS) {
			clear_bit(le16_to_cpu(dev_handle),
			    ioc->device_remove_in_progress);
			ioc->tm_tr_retry[le16_to_cpu(dev_handle)] = 0;
		} else if (ioc->tm_tr_retry[le16_to_cpu(dev_handle)] < 3){
			dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
			    "re-initiating tm_tr_send:handle(0x%04x)\n",
			    ioc->name, le16_to_cpu(dev_handle)));
			ioc->tm_tr_retry[le16_to_cpu(dev_handle)]++;
			_scsih_tm_tr_send(ioc, le16_to_cpu(dev_handle));
		} else {
			dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
			    "Exiting out of tm_tr_send retries:handle(0x%04x)\n",
			    ioc->name, le16_to_cpu(dev_handle)));
			ioc->tm_tr_retry[le16_to_cpu(dev_handle)] = 0;
			clear_bit(le16_to_cpu(dev_handle),
			    ioc->device_remove_in_progress);
		}
	} else {
		printk(MPT3SAS_ERR_FMT "mpi_reply not valid at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
	}
	return mpt3sas_check_for_pending_internal_cmds(ioc, smid);
}

/**
 * _scsih_tm_tr_volume_send - send target reset request for volumes
 * @ioc: per adapter object
 * @handle: device handle
 * Context: interrupt time.
 *
 * This is designed to send muliple task management request at the same
 * time to the fifo. If the fifo is full, we will append the request,
 * and process it in a future completion.
 */
static void
_scsih_tm_tr_volume_send(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	Mpi2SCSITaskManagementRequest_t *mpi_request;
	u16 smid;
	struct _tr_list *delayed_tr;

	if (ioc->pci_error_recovery) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: host reset in "
		   "progress!\n", __func__, ioc->name));
		return;
	}

	smid = mpt3sas_base_get_smid_hpr(ioc, ioc->tm_tr_volume_cb_idx);
	if (!smid) {
		delayed_tr = kzalloc(sizeof(*delayed_tr), GFP_ATOMIC);
		if (!delayed_tr)
			return;
		INIT_LIST_HEAD(&delayed_tr->list);
		delayed_tr->handle = handle;
		list_add_tail(&delayed_tr->list, &ioc->delayed_tr_volume_list);
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
		    "DELAYED:tr:handle(0x%04x), (open)\n",
		    ioc->name, handle));
		return;
	}

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "tr_send:handle(0x%04x), "
	    "(open), smid(%d), cb(%d)\n", ioc->name, handle, smid,
	    ioc->tm_tr_volume_cb_idx));
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, sizeof(Mpi2SCSITaskManagementRequest_t));
	mpi_request->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
	mpi_request->DevHandle = cpu_to_le16(handle);
	mpi_request->TaskType = MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	ioc->put_smid_hi_priority(ioc, smid, 0);
}

/**
 * _scsih_tm_volume_tr_complete - target reset completion
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: interrupt time.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_tm_volume_tr_complete(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u8 msix_index, u32 reply)
{
	u16 handle;
	Mpi2SCSITaskManagementRequest_t *mpi_request_tm;
	Mpi2SCSITaskManagementReply_t *mpi_reply =
	    mpt3sas_base_get_reply_virt_addr(ioc, reply);

	if (ioc->shost_recovery || ioc->pci_error_recovery) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: host reset in "
		   "progress!\n", __func__, ioc->name));
		return 1;
	}
	if (unlikely(!mpi_reply)) {
		printk(MPT3SAS_ERR_FMT "mpi_reply not valid at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 1;
	}

	mpi_request_tm = mpt3sas_base_get_msg_frame(ioc, smid);
	handle = le16_to_cpu(mpi_request_tm->DevHandle);
	if (handle != le16_to_cpu(mpi_reply->DevHandle)) {
		dewtprintk(ioc, printk(MPT3SAS_ERR_FMT "spurious interrupt: "
		    "handle(0x%04x:0x%04x), smid(%d)!!!\n", ioc->name, handle,
		    le16_to_cpu(mpi_reply->DevHandle), smid));
		return 0;
	}

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
	    "tr_complete:handle(0x%04x), (open) smid(%d), ioc_status(0x%04x), "
	    "loginfo(0x%08x), completed(%d)\n", ioc->name,
	    handle, smid, le16_to_cpu(mpi_reply->IOCStatus),
	    le32_to_cpu(mpi_reply->IOCLogInfo),
	    le32_to_cpu(mpi_reply->TerminationCount)));

	return _scsih_check_for_pending_tm(ioc, smid);
}

/**
 * _scsih_tm_internal_tr_send - send target reset request
 * @ioc: per adapter object
 * @handle: device handle
 * Context: interrupt time.
 *
 * This is designed to send multiple task management request (TR)at the same
 * time to the fifo. If the fifo is full, we will append the request,
 * and process it in a future completion.
 */
static void
_scsih_tm_internal_tr_send(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _tr_list *delayed_tr;
	Mpi2SCSITaskManagementRequest_t *mpi_request;
	u16 smid;
	struct _pcie_device *pcie_device;
	u8 tr_method = MPI2_SCSITASKMGMT_MSGFLAGS_LINK_RESET;

	smid = mpt3sas_base_get_smid_hpr(ioc, ioc->tm_tr_internal_cb_idx);
	if (!smid) {
		delayed_tr = kzalloc(sizeof(*delayed_tr), GFP_ATOMIC);
		if (!delayed_tr)
			return;
		INIT_LIST_HEAD(&delayed_tr->list);
		delayed_tr->handle = handle;
		list_add_tail(&delayed_tr->list, &ioc->delayed_internal_tm_list);
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
		    "DELAYED:tr:handle(0x%04x), (open)\n",
		    ioc->name, handle));
		return;
	}

	pcie_device = mpt3sas_get_pdev_by_handle(ioc, handle);
	if (pcie_device && (!ioc->tm_custom_handling) &&
		 (!(mpt3sas_scsih_is_pcie_scsi_device(pcie_device->device_info))))
		tr_method = MPI26_SCSITASKMGMT_MSGFLAGS_PROTOCOL_LVL_RST_PCIE;

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "tr_send:handle(0x%04x), "
	    "(open), smid(%d), cb(%d)\n", ioc->name, handle, smid,
	    ioc->tm_tr_internal_cb_idx));
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, sizeof(Mpi2SCSITaskManagementRequest_t));
	mpi_request->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
	mpi_request->DevHandle = cpu_to_le16(handle);
	mpi_request->TaskType = MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	mpi_request->MsgFlags = tr_method;
	ioc->put_smid_hi_priority(ioc, smid, 0);
	mpt3sas_trigger_master(ioc, MASTER_TRIGGER_TASK_MANAGMENT);
}

/**
 * _scsih_tm_internal_tr_complete - internal target reset completion
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: interrupt time.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_tm_internal_tr_complete(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u8 msix_index, u32 reply)
{
	Mpi2SCSITaskManagementReply_t *mpi_reply =
	mpt3sas_base_get_reply_virt_addr(ioc, reply);


	if (likely(mpi_reply)) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
		"tr_complete:handle(0x%04x), (open) "
		"smid(%d), ioc_status(0x%04x), loginfo(0x%08x)\n",
		ioc->name, le16_to_cpu(mpi_reply->DevHandle), smid,
		le16_to_cpu(mpi_reply->IOCStatus),
		le32_to_cpu(mpi_reply->IOCLogInfo)));
	} else {
		printk(MPT3SAS_ERR_FMT "mpi_reply not valid at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 1;
	}
	return _scsih_check_for_pending_tm(ioc, smid);;
}

/**
 * _scsih_issue_delayed_event_ack - issue delayed Event ACK messages
 * @ioc: per adapter object
 * @smid: system request message index
 * @event: Event ID
 * @event_context: used to track events uniquely
 *
 * Context - processed in interrupt context.
 */
static void
_scsih_issue_delayed_event_ack(struct MPT3SAS_ADAPTER *ioc, u16 smid, U16 event,
				U32 event_context)
{
	Mpi2EventAckRequest_t *ack_request;
	int i = smid - ioc->internal_smid;
	unsigned long flags;

	/* Without releasing the smid just update the
	 * call back index and reuse the same smid for
	 * processing this delayed request
	 */
	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	ioc->internal_lookup[i].cb_idx = ioc->base_cb_idx;
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "EVENT ACK: event(0x%04x), "
	    "smid(%d), cb(%d)\n", ioc->name, le16_to_cpu(event), smid,
	    ioc->base_cb_idx));
	ack_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(ack_request, 0, sizeof(Mpi2EventAckRequest_t));
	ack_request->Function = MPI2_FUNCTION_EVENT_ACK;
	ack_request->Event = event;
	ack_request->EventContext = event_context;
	ack_request->VF_ID = 0;  /* TODO */
	ack_request->VP_ID = 0;
	ioc->put_smid_default(ioc, smid);
}

/**
 * _scsih_issue_delayed_sas_io_unit_ctrl - issue delayed sas_io_unit_ctrl messages
 * @ioc: per adapter object
 * @smid: system request message index
 * @handle: device handle
 *
 * Context - processed in interrupt context.
 */
static void
_scsih_issue_delayed_sas_io_unit_ctrl(struct MPT3SAS_ADAPTER *ioc, u16 smid, u16 handle)
{
	Mpi2SasIoUnitControlRequest_t *mpi_request;
	u32 ioc_state;
	int i = smid - ioc->internal_smid;
	unsigned long flags;

	if (ioc->remove_host) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: host has been "
		   "removed\n", __func__, ioc->name));
		return;
	} else if (ioc->pci_error_recovery) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: host in pci "
		    "error recovery\n", __func__, ioc->name));
		return;
	}
	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	if (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: host is not "
		    "operational\n", __func__, ioc->name));
		return;
	}

	/* Without releasing the smid just update the
	 * call back index and reuse the same smid for
	 * processing this delayed request
	 */
	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	ioc->internal_lookup[i].cb_idx = ioc->tm_sas_control_cb_idx;
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "sc_send:handle(0x%04x), "
	    "(open), smid(%d), cb(%d)\n", ioc->name, handle, smid,
	    ioc->tm_sas_control_cb_idx));
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	if (ioc->hba_mpi_version_belonged < MPI26_VERSION ) {
		memset(mpi_request, 0, sizeof(Mpi2SasIoUnitControlRequest_t));
		mpi_request->Function = MPI2_FUNCTION_SAS_IO_UNIT_CONTROL;
		mpi_request->Operation = MPI2_SAS_OP_REMOVE_DEVICE;
	} else {
		memset(mpi_request, 0, sizeof(Mpi26IoUnitControlRequest_t));
		mpi_request->Function = MPI2_FUNCTION_IO_UNIT_CONTROL;
		mpi_request->Operation = MPI26_CTRL_OP_REMOVE_DEVICE;
	}
	mpi_request->DevHandle = cpu_to_le16(handle);
	ioc->put_smid_default(ioc, smid);
}

/**
 * _scsih_check_for_pending_internal_cmds - check for pending internal messages
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Context: Executed in interrupt context
 *
 * This will check delayed internal messages list, and process the
 * next request.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
u8
mpt3sas_check_for_pending_internal_cmds(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	struct _sc_list *delayed_sc;
	struct _event_ack_list *delayed_event_ack;

	if (!list_empty(&ioc->delayed_event_ack_list)) {
		delayed_event_ack = list_entry(ioc->delayed_event_ack_list.next,
						struct _event_ack_list, list);
		_scsih_issue_delayed_event_ack(ioc, smid,
		  delayed_event_ack->Event, delayed_event_ack->EventContext);
		list_del(&delayed_event_ack->list);
		kfree(delayed_event_ack);
		return 0;
	}

	if (!list_empty(&ioc->delayed_sc_list)) {
		delayed_sc= list_entry(ioc->delayed_sc_list.next,
						struct _sc_list, list);
		_scsih_issue_delayed_sas_io_unit_ctrl(ioc, smid,
						 delayed_sc->handle);
		list_del(&delayed_sc->list);
		kfree(delayed_sc);
		return 0;
	}
	return 1;
}

/**
 * _scsih_check_for_pending_tm - check for pending task management
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * This will check delayed target reset list, and feed the
 * next reqeust.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_check_for_pending_tm(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	struct _tr_list *delayed_tr;

	if (!list_empty(&ioc->delayed_tr_volume_list)) {
		delayed_tr = list_entry(ioc->delayed_tr_volume_list.next,
		    struct _tr_list, list);
		mpt3sas_base_free_smid(ioc, smid);
		_scsih_tm_tr_volume_send(ioc, delayed_tr->handle);
		list_del(&delayed_tr->list);
		kfree(delayed_tr);
		return 0;
	}

	if (!list_empty(&ioc->delayed_tr_list)) {
		delayed_tr = list_entry(ioc->delayed_tr_list.next,
		    struct _tr_list, list);
		mpt3sas_base_free_smid(ioc, smid);
		_scsih_tm_tr_send(ioc, delayed_tr->handle);
		list_del(&delayed_tr->list);
		kfree(delayed_tr);
		return 0;
	}

	if (!list_empty(&ioc->delayed_internal_tm_list)) {
		delayed_tr = list_entry(ioc->delayed_internal_tm_list.next,
		    struct _tr_list, list);
		mpt3sas_base_free_smid(ioc, smid);
		_scsih_tm_internal_tr_send(ioc, delayed_tr->handle);
		list_del(&delayed_tr->list);
		kfree(delayed_tr);
		return 0;
	}

	return 1;
}

/**
 * _scsih_check_topo_delete_events - sanity check on topo events
 * @ioc: per adapter object
 * @event_data: the event data payload
 *
 * This routine added to better handle cable breaker.
 *
 * This handles the case where driver receives multiple expander
 * add and delete events in a single shot.  When there is a delete event
 * the routine will void any pending add events waiting in the event queue.
 *
 * Return nothing.
 */
static void
_scsih_check_topo_delete_events(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataSasTopologyChangeList_t *event_data)
{
	struct fw_event_work *fw_event;
	Mpi2EventDataSasTopologyChangeList_t *local_event_data;
	u16 expander_handle;
	struct _sas_node *sas_expander;
	unsigned long flags;
	int i, reason_code;
	u16 handle;

	for (i = 0 ; i < event_data->NumEntries; i++) {
		handle = le16_to_cpu(event_data->PHY[i].AttachedDevHandle);
		if (!handle)
			continue;
		reason_code = event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_RC_MASK;
		if (reason_code == MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING)
			_scsih_tm_tr_send(ioc, handle);
	}

	expander_handle = le16_to_cpu(event_data->ExpanderDevHandle);
	if (expander_handle < ioc->sas_hba.num_phys) {
		_scsih_block_io_to_children_attached_directly(ioc, event_data);
		return;
	}
	if (event_data->ExpStatus ==
	    MPI2_EVENT_SAS_TOPO_ES_DELAY_NOT_RESPONDING) {
		/* put expander attached devices into blocking state */
		spin_lock_irqsave(&ioc->sas_node_lock, flags);
		sas_expander = mpt3sas_scsih_expander_find_by_handle(ioc,
		    expander_handle);
		_scsih_block_io_to_children_attached_to_ex(ioc, sas_expander);
		spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
		do {
			handle = find_first_bit(ioc->blocking_handles,
			    ioc->facts.MaxDevHandle);
			if (handle < ioc->facts.MaxDevHandle)
				_scsih_block_io_device(ioc, handle);
		} while (test_and_clear_bit(handle, ioc->blocking_handles));
	} else if (event_data->ExpStatus == MPI2_EVENT_SAS_TOPO_ES_RESPONDING)
		_scsih_block_io_to_children_attached_directly(ioc, event_data);

	if (event_data->ExpStatus != MPI2_EVENT_SAS_TOPO_ES_NOT_RESPONDING)
		return;

	/* mark ignore flag for pending events */
	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	list_for_each_entry(fw_event, &ioc->fw_event_list, list) {
		if (fw_event->event != MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST ||
		    fw_event->ignore)
			continue;
		local_event_data = fw_event->event_data;
		if (local_event_data->ExpStatus ==
		    MPI2_EVENT_SAS_TOPO_ES_ADDED ||
		    local_event_data->ExpStatus ==
		    MPI2_EVENT_SAS_TOPO_ES_RESPONDING) {
			if (le16_to_cpu(local_event_data->ExpanderDevHandle) ==
			    expander_handle) {
				dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
				    "setting ignoring flag\n", ioc->name));
				fw_event->ignore = 1;
			}
		}
	}
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/**
 * _scsih_check_pcie_topo_remove_events - sanity check on topo
 * events
 * @ioc: per adapter object
 * @event_data: the event data payload
 *
 * This handles the case where driver receives multiple switch
 * or device add and delete events in a single shot.  When there
 * is a delete event the routine will void any pending add
 * events waiting in the event queue.
 *
 * Return nothing.
 */
static void
_scsih_check_pcie_topo_remove_events(struct MPT3SAS_ADAPTER *ioc,
	Mpi26EventDataPCIeTopologyChangeList_t *event_data)
{
	struct fw_event_work *fw_event;
	Mpi26EventDataPCIeTopologyChangeList_t *local_event_data;
	unsigned long flags;
	int i, reason_code;
	u16 handle, switch_handle;

	for (i = 0 ; i < event_data->NumEntries; i++) {
		handle = le16_to_cpu(event_data->PortEntry[i].AttachedDevHandle);
		if (!handle)
			continue;
		reason_code = event_data->PortEntry[i].PortStatus;
		if (reason_code == MPI26_EVENT_PCIE_TOPO_PS_NOT_RESPONDING)
			_scsih_tm_tr_send(ioc, handle);
	}

	switch_handle = le16_to_cpu(event_data->SwitchDevHandle);
	if (!switch_handle) {
		_scsih_block_io_to_pcie_children_attached_directly(ioc, event_data);
		return;
	}
    /* TODO We are not supporting cascaded PCIe Switch removal yet*/
    if ((event_data->SwitchStatus
		== MPI26_EVENT_PCIE_TOPO_SS_DELAY_NOT_RESPONDING) ||
		(event_data->SwitchStatus == MPI26_EVENT_PCIE_TOPO_SS_RESPONDING))
		_scsih_block_io_to_pcie_children_attached_directly(ioc, event_data);

	if (event_data->SwitchStatus != MPI2_EVENT_SAS_TOPO_ES_NOT_RESPONDING)
		return;

	/* mark ignore flag for pending events */
	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	list_for_each_entry(fw_event, &ioc->fw_event_list, list) {
		if (fw_event->event != MPI2_EVENT_PCIE_TOPOLOGY_CHANGE_LIST ||
			fw_event->ignore)
			continue;
		local_event_data = fw_event->event_data;
		if (local_event_data->SwitchStatus ==
		    MPI2_EVENT_SAS_TOPO_ES_ADDED ||
		    local_event_data->SwitchStatus ==
		    MPI2_EVENT_SAS_TOPO_ES_RESPONDING) {
			if (le16_to_cpu(local_event_data->SwitchDevHandle) ==
				switch_handle) {
				dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
					"setting ignoring flag for switch event\n", ioc->name));
				fw_event->ignore = 1;
			}
		}
	}
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/**
 * _scsih_set_volume_delete_flag - setting volume delete flag
 * @ioc: per adapter object
 * @handle: device handle
 *
 * This returns nothing.
 */
static void
_scsih_set_volume_delete_flag(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _raid_device *raid_device;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	unsigned long flags;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	raid_device = mpt3sas_raid_device_find_by_handle(ioc, handle);
	if (raid_device && raid_device->starget &&
	    raid_device->starget->hostdata) {
		sas_target_priv_data =
		    raid_device->starget->hostdata;
		sas_target_priv_data->deleted = 1;
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
		    "setting delete flag: handle(0x%04x), "
		    "wwid(0x%016llx)\n", ioc->name, handle,
		    (unsigned long long) raid_device->wwid));
	}
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
}

/**
 * _scsih_set_volume_handle_for_tr - set handle for target reset to volume
 * @handle: input handle
 * @a: handle for volume a
 * @b: handle for volume b
 *
 * IR firmware only supports two raid volumes.  The purpose of this
 * routine is to set the volume handle in either a or b. When the given
 * input handle is non-zero, or when a and b have not been set before.
 */
static void
_scsih_set_volume_handle_for_tr(u16 handle, u16 *a, u16 *b)
{
	if (!handle || handle == *a || handle == *b)
		return;
	if (!*a)
		*a = handle;
	else if (!*b)
		*b = handle;
}

/**
 * _scsih_check_ir_config_unhide_events - check for UNHIDE events
 * @ioc: per adapter object
 * @event_data: the event data payload
 * Context: interrupt time.
 *
 * This routine will send target reset to volume, followed by target
 * resets to the PDs. This is called when a PD has been removed, or
 * volume has been deleted or removed. When the target reset is sent
 * to volume, the PD target resets need to be queued to start upon
 * completion of the volume target reset.
 *
 * Return nothing.
 */
static void
_scsih_check_ir_config_unhide_events(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataIrConfigChangeList_t *event_data)
{
	Mpi2EventIrConfigElement_t *element;
	int i;
	u16 handle, volume_handle, a, b;
	struct _tr_list *delayed_tr;

	a = 0;
	b = 0;

	if (ioc->is_warpdrive)
		return;

	/* Volume Resets for Deleted or Removed */
	element = (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];
	for (i = 0; i < event_data->NumElements; i++, element++) {
		if (le32_to_cpu(event_data->Flags) &
		    MPI2_EVENT_IR_CHANGE_FLAGS_FOREIGN_CONFIG)
			continue;
		if (element->ReasonCode ==
		    MPI2_EVENT_IR_CHANGE_RC_VOLUME_DELETED ||
		    element->ReasonCode ==
		    MPI2_EVENT_IR_CHANGE_RC_REMOVED) {
			volume_handle = le16_to_cpu(element->VolDevHandle);
			_scsih_set_volume_delete_flag(ioc, volume_handle);
			_scsih_set_volume_handle_for_tr(volume_handle, &a, &b);
		}
	}

	/* Volume Resets for UNHIDE events */
	element = (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];
	for (i = 0; i < event_data->NumElements; i++, element++) {
		if (le32_to_cpu(event_data->Flags) &
		    MPI2_EVENT_IR_CHANGE_FLAGS_FOREIGN_CONFIG)
			continue;
		if (element->ReasonCode == MPI2_EVENT_IR_CHANGE_RC_UNHIDE) {
			volume_handle = le16_to_cpu(element->VolDevHandle);
			_scsih_set_volume_handle_for_tr(volume_handle, &a, &b);
		}
	}

	if (a)
		_scsih_tm_tr_volume_send(ioc, a);
	if (b)
		_scsih_tm_tr_volume_send(ioc, b);

	/* PD target resets */
	element = (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];
	for (i = 0; i < event_data->NumElements; i++, element++) {
		if (element->ReasonCode != MPI2_EVENT_IR_CHANGE_RC_UNHIDE)
			continue;
		handle = le16_to_cpu(element->PhysDiskDevHandle);
		volume_handle = le16_to_cpu(element->VolDevHandle);
		clear_bit(handle, ioc->pd_handles);
		if (!volume_handle)
			_scsih_tm_tr_send(ioc, handle);
		else if (volume_handle == a || volume_handle == b) {
			delayed_tr = kzalloc(sizeof(*delayed_tr), GFP_ATOMIC);
			BUG_ON(!delayed_tr);
			INIT_LIST_HEAD(&delayed_tr->list);
			delayed_tr->handle = handle;
			list_add_tail(&delayed_tr->list, &ioc->delayed_tr_list);
			dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
			    "DELAYED:tr:handle(0x%04x), (open)\n", ioc->name,
			    handle));
		} else
			_scsih_tm_tr_send(ioc, handle);
	}
}


/**
 * _scsih_check_volume_delete_events - set delete flag for volumes
 * @ioc: per adapter object
 * @event_data: the event data payload
 * Context: interrupt time.
 *
 * This will handle the case when the cable connected to entire volume is
 * pulled. We will take care of setting the deleted flag so normal IO will
 * not be sent.
 *
 * Return nothing.
 */
static void
_scsih_check_volume_delete_events(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataIrVolume_t *event_data)
{
	u32 state;

	if (event_data->ReasonCode != MPI2_EVENT_IR_VOLUME_RC_STATE_CHANGED)
		return;
	state = le32_to_cpu(event_data->NewValue);
	if (state == MPI2_RAID_VOL_STATE_MISSING || state ==
	    MPI2_RAID_VOL_STATE_FAILED)
		_scsih_set_volume_delete_flag(ioc,
		    le16_to_cpu(event_data->VolDevHandle));
}

/**
 * _scsih_temp_threshold_events - display temperature threshold exceeded events
 * @ioc: per adapter object
 * @event_data: the temp threshold event data
 * Context: interrupt time.
 *
 * Return nothing.
 */
static void
_scsih_temp_threshold_events(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataTemperature_t *event_data)
{
	u32 doorbell;

	if (ioc->temp_sensors_count >= event_data->SensorNum) {
		printk(MPT3SAS_ERR_FMT "Temperature Threshold flags %s%s%s%s"
			"exceeded for Sensor: %d !!!\n", ioc->name,
			((event_data->Status & 0x1) == 1) ? "0 " : " ",
			((event_data->Status & 0x2) == 2) ? "1 " : " ",
			((event_data->Status & 0x4) == 4) ? "2 " : " ",
			((event_data->Status & 0x8) == 8) ? "3 " : " ",
			event_data->SensorNum);
		printk(MPT3SAS_ERR_FMT "Current Temp In Celsius: %d\n",
			ioc->name, event_data->CurrentTemperature);

		if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
			doorbell = mpt3sas_base_get_iocstate(ioc, 0);
			if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
				mpt3sas_print_fault_code(ioc, doorbell &
				MPI2_DOORBELL_DATA_MASK);
			} else if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_COREDUMP)
				mpt3sas_base_coredump_info(ioc, doorbell &
				MPI2_DOORBELL_DATA_MASK);
		}
	}
}

/**
 * _scsih_set_satl_pending - sets variable as per bool,
 * indicating ATA command is pending or not.
 *
 * @scmd: pointer to scsi command object
 * @pending: boolean(true or false)
 *
 * Returns 0 else 1 if ATA command is pending.
 */
static int _scsih_set_satl_pending(struct scsi_cmnd *scmd, bool pending)
{
	struct MPT3SAS_DEVICE *priv = scmd->device->hostdata;

	if  (scmd->cmnd[0] != ATA_12 && scmd->cmnd[0] != ATA_16)
		return 0;

	if (pending)
		return test_and_set_bit(CMND_PENDING_BIT, &priv->ata_command_pending);

	clear_bit(CMND_PENDING_BIT, &priv->ata_command_pending);
	return 0;
}

/**
 * mpt3sas_scsih_flush_running_cmds - completing outstanding commands.
 * @ioc: per adapter object
 *
 * The flushing out of all pending scmd commands following host reset,
 * where all IO is dropped to the floor.
 *
 * Return nothing.
 */
void
mpt3sas_scsih_flush_running_cmds(struct MPT3SAS_ADAPTER *ioc)
{
	struct scsi_cmnd *scmd;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23))
	Mpi25SCSIIORequest_t *mpi_request;
#endif
	struct scsiio_tracker *st;
	u16 smid;
	u16 count = 0;

	for (smid = 1; smid <= ioc->shost->can_queue; smid++) {
		scmd = mpt3sas_scsih_scsi_lookup_get(ioc, smid);
		if (!scmd)
			continue;
		count++;

		st = mpt3sas_base_scsi_cmd_priv(scmd);
		/* It may be possible that SCSI scmd got prepared by SML
		but it has not issued to the driver, for these type of
		scmd's don't do anything" */
		if (st && st->smid == 0)
			continue;
		_scsih_set_satl_pending(scmd, false);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23))
		mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
		if (scmd->use_sg) {
			pci_unmap_sg(ioc->pdev,
			    (struct scatterlist *) scmd->request_buffer,
			    scmd->use_sg, scmd->sc_data_direction);
		} else if (scmd->request_bufflen) {
			pci_unmap_single(ioc->pdev,
			    scmd->SCp.dma_handle, scmd->request_bufflen,
			    scmd->sc_data_direction);
		}
#else
		if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
			if (mpi_request->DMAFlags == MPI25_TA_DMAFLAGS_OP_D_H_D_D)
			{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
				dma_unmap_sg(scmd->device->host->dma_dev, scsi_prot_sglist(scmd),
				scsi_prot_sg_count(scmd), scmd->sc_data_direction);
#else
				dma_unmap_sg(scmd->device->host->shost_gendev.parent, scsi_prot_sglist(scmd),
				scsi_prot_sg_count(scmd), scmd->sc_data_direction);
#endif
			}
		}
		scsi_dma_unmap(scmd);
#endif
		mpt3sas_base_clear_st(ioc, st);

		if ((!mpt3sas_base_pci_device_is_available(ioc)) ||
				(ioc->ioc_reset_status != 0)
				|| ioc->adapter_over_temp
				|| ioc->remove_host)
			scmd->result = DID_NO_CONNECT << 16;
		else
			mpt3sas_set_requeue_or_reset(scmd);

		scmd->scsi_done(scmd);
	}
	dtmprintk(ioc, printk(MPT3SAS_INFO_FMT "completing %d cmds\n",
	    ioc->name, count));
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27))
static u8 opcode_protection[256] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, PRO_R, 0, PRO_W, 0, 0, 0, PRO_W, PRO_V,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, PRO_W, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, PRO_R, 0, PRO_W, 0, 0, 0, PRO_W, PRO_V,
	0, 0, 0, PRO_W, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, PRO_R, 0, PRO_W, 0, 0, 0, PRO_W, PRO_V,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
#endif
/**
 * _scsih_setup_eedp - setup MPI request for EEDP transfer
 * @ioc: per adapter object
 * @scmd: pointer to scsi command object
 * @mpi_request: pointer to the SCSI_IO reqest message frame
 *
 * Supporting protection 1 and 3.
 *
 * Returns nothing
 */
static void
_scsih_setup_eedp(struct MPT3SAS_ADAPTER *ioc, struct scsi_cmnd *scmd,
	Mpi25SCSIIORequest_t *mpi_request)
{
	u16 eedp_flags;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27))
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	u8 scsi_opcode;

	sas_device_priv_data = scmd->device->hostdata;

	if (!sas_device_priv_data->eedp_enable)
		return;

	/* check whether scsi opcode supports eedp transfer */
	scsi_opcode = scmd->cmnd[0];
	eedp_flags = opcode_protection[scsi_opcode];
	if (!eedp_flags)
		return;

	/* set RDPROTECT, WRPROTECT, VRPROTECT bits to (001b) */
	scmd->cmnd[1] = (scmd->cmnd[1] & 0x1F) | 0x20;

	switch (sas_device_priv_data->eedp_type) {
	case 1: /* type 1 */

		/*
		* enable ref/guard checking
		* auto increment ref tag
		*/
		eedp_flags |= MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG |
			MPI2_SCSIIO_EEDPFLAGS_CHECK_REFTAG |
			MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD;
		if (ioc->is_gen35_ioc)
			eedp_flags |= MPI25_SCSIIO_EEDPFLAGS_APPTAG_DISABLE_MODE;
		mpi_request->CDB.EEDP32.PrimaryReferenceTag =
		    cpu_to_be32(scsi_get_lba(scmd));

		break;

	case 3: /* type 3 */

		/*
		* enable guard checking
		*/
		eedp_flags |= MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD;
		if (ioc->is_gen35_ioc)
			eedp_flags |= MPI25_SCSIIO_EEDPFLAGS_APPTAG_DISABLE_MODE;
		break;
	}

#else /* sles11 and newer */

	unsigned char prot_op = scsi_get_prot_op(scmd);
	unsigned char prot_type = scsi_get_prot_type(scmd);

	if (ioc->hba_mpi_version_belonged == MPI2_VERSION) {
		if (prot_type == SCSI_PROT_DIF_TYPE0 || prot_op == SCSI_PROT_NORMAL)
			return;

		if (prot_op ==  SCSI_PROT_READ_STRIP)
			eedp_flags = MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP;
		else if (prot_op ==  SCSI_PROT_WRITE_INSERT)
			eedp_flags = MPI2_SCSIIO_EEDPFLAGS_INSERT_OP;
		else
			return;
	}
	else {

	if (prot_op == SCSI_PROT_NORMAL)
		return;

	/* Translate SCSI opcode to a protection opcode */
	switch (prot_op) {
// The prints are added only for testing purpose. Will be removed before GCA
	case SCSI_PROT_READ_INSERT:
		eedp_flags = MPI2_SCSIIO_EEDPFLAGS_INSERT_OP;
		mpi_request->DMAFlags = MPI25_TA_DMAFLAGS_OP_D_H_D_D;

		break;

	case SCSI_PROT_WRITE_STRIP:
		eedp_flags = MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP;
		mpi_request->DMAFlags = MPI25_TA_DMAFLAGS_OP_D_H_D_D;
		break;

	case SCSI_PROT_READ_STRIP:
		eedp_flags = MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP;
		mpi_request->DMAFlags = MPI25_TA_DMAFLAGS_OP_D_D_D_D;
		break;


	case SCSI_PROT_WRITE_INSERT:
		eedp_flags = MPI2_SCSIIO_EEDPFLAGS_INSERT_OP;
		mpi_request->DMAFlags = MPI25_TA_DMAFLAGS_OP_D_D_D_D;
		break;


	case SCSI_PROT_READ_PASS:
		eedp_flags = MPI2_SCSIIO_EEDPFLAGS_CHECK_OP| MPI25_TA_EEDPFLAGS_CHECK_REFTAG |MPI25_TA_EEDPFLAGS_CHECK_APPTAG |MPI25_TA_EEDPFLAGS_CHECK_GUARD;
		mpi_request->DMAFlags = MPI25_TA_DMAFLAGS_OP_D_H_D_D;
			break;

	case SCSI_PROT_WRITE_PASS:
		if(scsi_host_get_guard(scmd->device->host)  & SHOST_DIX_GUARD_IP){
			eedp_flags = MPI2_SCSIIO_EEDPFLAGS_CHECK_REGEN_OP|MPI25_TA_EEDPFLAGS_CHECK_APPTAG |MPI25_TA_EEDPFLAGS_CHECK_GUARD|MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG;;
			mpi_request->DMAFlags = MPI25_TA_DMAFLAGS_OP_D_H_D_D;
			mpi_request->ApplicationTagTranslationMask = 0xffff;

		} else {

			eedp_flags = MPI2_SCSIIO_EEDPFLAGS_CHECK_OP| MPI25_TA_EEDPFLAGS_CHECK_REFTAG |MPI25_TA_EEDPFLAGS_CHECK_APPTAG |MPI25_TA_EEDPFLAGS_CHECK_GUARD;
			mpi_request->DMAFlags = MPI25_TA_DMAFLAGS_OP_D_H_D_D;

		}
		break;

	default :
		eedp_flags = MPI2_SCSIIO_EEDPFLAGS_NOOP_OP;
		mpi_request->DMAFlags = MPI25_TA_DMAFLAGS_OP_D_D_D_D;
		break;
	}

	/* In case of T10 CRC algorithm, bits 5:4 of eedp_flags should be set to 0
	 * if(scsi_host_get_guard(scmd->device->host) == SHOST_DIX_GUARD_CRC)
	 *	eedp_flags |=	(0<<4);
	 */
	if(scsi_host_get_guard(scmd->device->host)  & SHOST_DIX_GUARD_IP)
		eedp_flags |=	(1<<4);
	}
	switch (prot_type) {
	case SCSI_PROT_DIF_TYPE0:
		if (ioc->hba_mpi_version_belonged != MPI2_VERSION){
			eedp_flags |= MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG;
			mpi_request->CDB.EEDP32.PrimaryReferenceTag =
			cpu_to_be32(scsi_get_lba(scmd));
		}
		break;

	case SCSI_PROT_DIF_TYPE1:
	case SCSI_PROT_DIF_TYPE2:

		eedp_flags |= MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG;
		mpi_request->CDB.EEDP32.PrimaryReferenceTag =
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(4,19,0)) || ((defined(RHEL_MAJOR) && (RHEL_MAJOR == 8))) \
	|| (defined(CONFIG_SUSE_KERNEL) && ((CONFIG_SUSE_VERSION == 15) && (CONFIG_SUSE_PATCHLEVEL >= 1))) \
	|| (defined(CONFIG_SUSE_KERNEL) && ((CONFIG_SUSE_VERSION == 12) && (CONFIG_SUSE_PATCHLEVEL >= 5))))

			cpu_to_be32(t10_pi_ref_tag(scmd->request));
#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)))
			cpu_to_be32(mpt3sas_scsi_prot_ref_tag(scmd));
#else
			cpu_to_be32(scsi_get_lba(scmd));
#endif
		eedp_flags |= MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG |
		    MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD;
		if (ioc->is_gen35_ioc)
			eedp_flags |= MPI25_SCSIIO_EEDPFLAGS_APPTAG_DISABLE_MODE;
		break;

	case SCSI_PROT_DIF_TYPE3:
		/*
		* enable guard checking
		*/
		eedp_flags |= MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD;
		if (ioc->is_gen35_ioc)
			eedp_flags |= MPI25_SCSIIO_EEDPFLAGS_APPTAG_DISABLE_MODE;
		break;
	}

#endif
	mpi_request->EEDPBlockSize =
	    cpu_to_le32(scmd->device->sector_size);
	mpi_request->EEDPFlags = cpu_to_le16(eedp_flags);

}

/**
 * _scsih_eedp_error_handling - return sense code for EEDP errors
 * @scmd: pointer to scsi command object
 * @ioc_status: ioc status
 *
 * Returns nothing
 */
static void
_scsih_eedp_error_handling(struct scsi_cmnd *scmd, u16 ioc_status)
{
	u8 ascq;

	switch (ioc_status) {
	case MPI2_IOCSTATUS_EEDP_GUARD_ERROR:
		ascq = 0x01;
		break;
	case MPI2_IOCSTATUS_EEDP_APP_TAG_ERROR:
		ascq = 0x02;
		break;
	case MPI2_IOCSTATUS_EEDP_REF_TAG_ERROR:
		ascq = 0x03;
		break;
	default:
		ascq = 0x00;
		break;
	}
	mpt_scsi_build_sense_buffer(0, scmd->sense_buffer, ILLEGAL_REQUEST, 0x10,
	    ascq);
	scmd->result = DRIVER_SENSE << 24 | (DID_ABORT << 16) |
	    SAM_STAT_CHECK_CONDITION;
}


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26))
/** _scsih_build_nvme_unmap - Build Native NVMe DSM command equivalent
 *			      to SCSI Unmap.
 * Return 0 - for success,
 *        1 - to immediately return back the command with success status to SML
 *        negative value - to fallback to firmware path i.e. issue scsi unmap
 *			   to FW without any translation.
 */
int _scsih_build_nvme_unmap(struct MPT3SAS_ADAPTER *ioc, struct scsi_cmnd *scmd,
					u16 handle, u64 lun, u32 nvme_mdts)
{
	Mpi26NVMeEncapsulatedRequest_t *nvme_encap_request = NULL;
	struct scsi_unmap_parm_list *plist;
	struct nvme_dsm_range *nvme_dsm_ranges = NULL;
	struct nvme_command *c;
	int i, res=0;
	u16 ndesc, list_len, data_length, smid;
	dma_addr_t nvme_dsm_ranges_dma_handle = 0;

	list_len = get_unaligned_be16(&scmd->cmnd[7]);
	if (!list_len) {
		pr_warn(MPT3SAS_FMT
		    "%s: CDB received with zero parameter length\n",
		    ioc->name, __func__);
		scsi_print_command(scmd);
		scmd->result = DID_OK << 16;
		scmd->scsi_done(scmd);
		return 1;
	}

	if (list_len < 24) {
		pr_warn(MPT3SAS_FMT
		    "%s: CDB received with invalid param_len: %d\n",
		    ioc->name, __func__, list_len);
		scsi_print_command(scmd);
		scmd->result = (DRIVER_SENSE << 24) |
		    SAM_STAT_CHECK_CONDITION;
		scsi_build_sense_buffer(0, scmd->sense_buffer, ILLEGAL_REQUEST,
		    0x1A, 0);
		scmd->scsi_done(scmd);
		return 1;
	}

	if (list_len != scsi_bufflen(scmd)) {
		pr_warn(MPT3SAS_FMT
		    "%s: CDB received with param_len: %d bufflen: %d\n",
		    ioc->name, __func__, list_len, scsi_bufflen(scmd));
		scsi_print_command(scmd);
		scmd->result = (DRIVER_SENSE << 24) |
		    SAM_STAT_CHECK_CONDITION;
		scsi_build_sense_buffer(0, scmd->sense_buffer, ILLEGAL_REQUEST,
		    0x1A, 0);
		scmd->scsi_done(scmd);
		return 1;
	}

	plist = kzalloc(list_len, GFP_KERNEL);
	if (!plist)
		return -ENOMEM;

	/* Copy SCSI unmap data to a local buffer */
	scsi_sg_copy_to_buffer(scmd, plist, list_len);

	/* return back the unmap command to SML with success status,
 	 * if number of descripts is zero.
 	 */
	ndesc = be16_to_cpu(plist->unmap_blk_desc_data_len) >> 4;
	if (ndesc == 0 || ndesc > 256) {
		res = -EINVAL;
		goto out;
	}

	data_length = ndesc * sizeof(*nvme_dsm_ranges);
	if ((nvme_mdts && (data_length > nvme_mdts)) ||
	    (data_length > (list_len - 8))) {
		res = -EINVAL;
		goto out;
	}

	smid = mpt3sas_base_get_smid_scsiio(ioc, ioc->scsi_io_cb_idx, scmd);
	if (!smid) {
		printk(MPT3SAS_ERR_FMT "%s: failed obtaining a smid\n",
							ioc->name, __func__);
		res = -ENOMEM;
		goto out;
	}

	nvme_dsm_ranges =
		(struct nvme_dsm_range *)mpt3sas_base_get_pcie_sgl(ioc, smid);
	nvme_dsm_ranges_dma_handle =
		(dma_addr_t)mpt3sas_base_get_pcie_sgl_dma(ioc, smid);

	memset(nvme_dsm_ranges, 0, data_length);

	/* Convert SCSI unmap's descriptor data to NVMe DSM specific Range data
	 * for each descriptors contained in SCSI UNMAP data.
	 */
	for (i = 0; i < ndesc; i++) {
		nvme_dsm_ranges[i].nlb = cpu_to_le32(be32_to_cpu(plist->desc[i].nlb));
		nvme_dsm_ranges[i].slba = cpu_to_le64(be64_to_cpu(plist->desc[i].slba));
		nvme_dsm_ranges[i].cattr = 0;
	}

	/* Build MPI2.6's NVMe Encapsulated Request Message */
	nvme_encap_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(nvme_encap_request, 0, sizeof(Mpi26NVMeEncapsulatedRequest_t));

	nvme_encap_request->Function = MPI2_FUNCTION_NVME_ENCAPSULATED;
	nvme_encap_request->ErrorResponseBaseAddress =
		cpu_to_le64(mpt3sas_base_get_sense_buffer_dma_64(ioc, smid));
	nvme_encap_request->ErrorResponseAllocationLength =
				cpu_to_le16(sizeof(struct nvme_completion));
	nvme_encap_request->EncapsulatedCommandLength =
				cpu_to_le16(sizeof(struct nvme_command));
	nvme_encap_request->DataLength = cpu_to_le32(data_length);
	nvme_encap_request->DevHandle = cpu_to_le16(handle);
	nvme_encap_request->Flags = MPI26_NVME_FLAGS_WRITE;

	/* Build NVMe DSM command */
	c = (struct nvme_command *) nvme_encap_request->NVMe_Command;
	c->dsm.opcode = nvme_cmd_dsm;
	c->dsm.nsid = cpu_to_le32(lun + 1);
	c->dsm.nr = cpu_to_le32(ndesc - 1);
	c->dsm.attributes = cpu_to_le32(NVME_DSMGMT_AD);

	ioc->build_nvme_prp(ioc, smid, nvme_encap_request,
			    nvme_dsm_ranges_dma_handle, data_length, 0, 0);

	ioc->put_smid_nvme_encap(ioc, smid);
out:
	kfree(plist);
	return res;
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0))
static inline u8 scsih_is_io_belongs_to_RT_class(struct scsi_cmnd *scmd)
{
	struct request *rq = scmd->request;
	return (IOPRIO_PRIO_CLASS(req_get_ioprio(rq)) == IOPRIO_CLASS_RT);
}
#endif

/**
 * scsih_qcmd - main scsi request entry point
 * @scmd: pointer to scsi command object
 * @done: function pointer to be invoked on completion
 *
 * The callback index is set inside `ioc->scsi_io_cb_idx`.
 *
 * Returns 0 on success.  If there's a failure, return either:
 * SCSI_MLQUEUE_DEVICE_BUSY if the device queue is full, or
 * SCSI_MLQUEUE_HOST_BUSY if the entire host queue is full
 */
static int
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37))
scsih_qcmd(struct scsi_cmnd *scmd, void (*done)(struct scsi_cmnd *))
#else
scsih_qcmd(struct Scsi_Host *shost, struct scsi_cmnd *scmd)
#endif
{
	struct MPT3SAS_ADAPTER *ioc = shost_private(scmd->device->host);
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct _raid_device *raid_device;
	Mpi25SCSIIORequest_t *mpi_request;
	struct _pcie_device *pcie_device = NULL;
	u32 mpi_control;
	u16 smid;
	u16 handle;
	int rc = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	unsigned long irq_flags = 0;
#endif

	if (ioc->logging_level & MPT_DEBUG_SCSI)
		scsi_print_command(scmd);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	if(host_lock_mode)
	{
		spin_lock_irqsave(shost->host_lock, irq_flags);
	}
#else
	scmd->scsi_done = done;
#endif

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		goto out;
	}

	if (!(_scsih_allow_scmd_to_device(ioc, scmd))) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		goto out;
 	}

	sas_target_priv_data = sas_device_priv_data->sas_target;

	/* invalid device handle */
	handle = sas_target_priv_data->handle;
	if (handle == MPT3SAS_INVALID_DEVICE_HANDLE) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		goto out;
	}

	/*
	 * Avoid error handling escallation when blocked
	 */
	if (sas_device_priv_data->block &&
	    scmd->device->host->shost_state == SHOST_RECOVERY &&
	    scmd->cmnd[0] == TEST_UNIT_READY) {
		scmd->result = (DRIVER_SENSE << 24) |
		    SAM_STAT_CHECK_CONDITION;
		scmd->sense_buffer[0] = 0x70;
		scmd->sense_buffer[2] = UNIT_ATTENTION;
		scmd->sense_buffer[12] = 0x29;
		/* ASCQ = I_T NEXUS LOSS OCCURRED */
		scmd->sense_buffer[13] = 0x07;
		scmd->scsi_done(scmd);
		goto out;
	}

	/* host recovery or link resets sent via IOCTLs */
	if (ioc->shost_recovery || ioc->ioc_link_reset_in_progress) {
		rc =  SCSI_MLQUEUE_HOST_BUSY;
		goto out;
	}
	/* device has been deleted */
	else if (sas_target_priv_data->deleted ||
	    sas_device_priv_data->deleted) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		goto out;
	/* device busy with task managment */
	} else if (sas_target_priv_data->tm_busy ||
	    sas_device_priv_data->block) {
		rc = SCSI_MLQUEUE_DEVICE_BUSY;
		goto out;
	}

	/*
	 * Bug work around for firmware SATL handling.  The loop
	 * is based on atomic operations and ensures consistency
	 * since we're lockless at this point
	 */
	do {
		if (test_bit(CMND_PENDING_BIT,
				&sas_device_priv_data->ata_command_pending)) {
			rc = SCSI_MLQUEUE_DEVICE_BUSY;
			goto out;
		}
	} while (_scsih_set_satl_pending(scmd, true));

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26))
	/* For NVME device type other than SCSI Devic issue UNMAP command
 	 * directly to NVME drives by constructing equivalent native NVMe
 	 * DataSetManagement command. 
	 * For pcie SCSI Device fallback to firmware
	 * path to issue scsi unmap without any translation
	 */
	if ((sas_target_priv_data->pcie_dev && scmd->cmnd[0] == UNMAP) &&
		(!(mpt3sas_scsih_is_pcie_scsi_device(
		sas_target_priv_data->pcie_dev->device_info)))) {
		pcie_device = sas_target_priv_data->pcie_dev;
		rc = _scsih_build_nvme_unmap(ioc, scmd, handle, sas_device_priv_data->lun,
						pcie_device->nvme_mdts);
		if (rc == 1) { /* return command back to SML */
			rc = 0;
			goto out;
		} else if (!rc) { /* Issued NVMe Encapsulated Request Message */
			goto out;
		} else /* Issue a normal scsi UNMAP command to FW */
			rc = 0;
	}
#endif

	if (scmd->sc_data_direction == DMA_FROM_DEVICE)
		mpi_control = MPI2_SCSIIO_CONTROL_READ;
	else if (scmd->sc_data_direction == DMA_TO_DEVICE)
		mpi_control = MPI2_SCSIIO_CONTROL_WRITE;
	else
		mpi_control = MPI2_SCSIIO_CONTROL_NODATATRANSFER;

	/* set tags */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0))
	if (!(sas_device_priv_data->flags & MPT_DEVICE_FLAGS_INIT)) {
		if (scmd->device->tagged_supported) {
			if (scmd->device->ordered_tags)
				mpi_control |= MPI2_SCSIIO_CONTROL_ORDEREDQ;
			else
				mpi_control |= MPI2_SCSIIO_CONTROL_SIMPLEQ;
		} else
			mpi_control |= MPI2_SCSIIO_CONTROL_SIMPLEQ;
	} else
		mpi_control |= MPI2_SCSIIO_CONTROL_SIMPLEQ;
#else
	mpi_control |= MPI2_SCSIIO_CONTROL_SIMPLEQ;
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0))
	/* NCQ Prio supported, make sure control indicated high priority */
	if (sas_device_priv_data->ncq_prio_enable) {
		if (scsih_is_io_belongs_to_RT_class(scmd))
			mpi_control |= 1 << MPI2_SCSIIO_CONTROL_CMDPRI_SHIFT;
	}
#endif

	if ((sas_device_priv_data->flags & MPT_DEVICE_TLR_ON) &&
	    scmd->cmd_len != 32)
		mpi_control |= MPI2_SCSIIO_CONTROL_TLR_ON;

	smid = mpt3sas_base_get_smid_scsiio(ioc, ioc->scsi_io_cb_idx, scmd);
	if (!smid) {
		printk(MPT3SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		rc = SCSI_MLQUEUE_HOST_BUSY;
		_scsih_set_satl_pending(scmd, false);
		goto out;
	}
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	/*  the message frame allocated will be of sizeof(MPT3SAS_SCSIIORequest2SGLS_t) */
	if (!ioc->disable_eedp_support) {
		 _scsih_setup_eedp(ioc, scmd, mpi_request);

	}
	if (scmd->cmd_len == 32)
		mpi_control |= 4 << MPI2_SCSIIO_CONTROL_ADDCDBLEN_SHIFT;
	mpi_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT)
		mpi_request->Function = MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH;
	else
		mpi_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
	mpi_request->DevHandle = cpu_to_le16(handle);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	mpi_request->DataLength = cpu_to_le32(scmd->request_bufflen);
#else
	mpi_request->DataLength = cpu_to_le32(scsi_bufflen(scmd));
#endif
	mpi_request->Control = cpu_to_le32(mpi_control);
	mpi_request->IoFlags = cpu_to_le16(scmd->cmd_len);
	mpi_request->MsgFlags = MPI2_SCSIIO_MSGFLAGS_SYSTEM_SENSE_ADDR;
	mpi_request->SenseBufferLength = SCSI_SENSE_BUFFERSIZE;
	mpi_request->SenseBufferLowAddress =
	    mpt3sas_base_get_sense_buffer_dma(ioc, smid);
	mpi_request->SGLOffset0 = offsetof(Mpi25SCSIIORequest_t, SGL) / 4;
	int_to_scsilun(sas_device_priv_data->lun, (struct scsi_lun *)
	    mpi_request->LUN);
	memcpy(mpi_request->CDB.CDB32, scmd->cmnd, scmd->cmd_len);

	if (mpi_request->DataLength) {
		pcie_device = sas_target_priv_data->pcie_dev;
		if (ioc->build_sg_scmd(ioc, scmd, smid, pcie_device)) {
			mpt3sas_base_free_smid(ioc, smid);
			rc = SCSI_MLQUEUE_HOST_BUSY;
			_scsih_set_satl_pending(scmd, false);
			goto out;
		}
	} else
		ioc->build_zero_len_sge(ioc, &mpi_request->SGL);

	if (ioc->hba_mpi_version_belonged == MPI2_VERSION) {
		raid_device = sas_target_priv_data->raid_device;
		if (raid_device && raid_device->direct_io_enabled)
			mpt3sas_setup_direct_io(ioc, scmd, raid_device,
						 mpi_request);
	}
	if (likely(mpi_request->Function == MPI2_FUNCTION_SCSI_IO_REQUEST)) {
		if ((ioc->hba_mpi_version_belonged != MPI2_VERSION) &&
			(sas_target_priv_data->flags & MPT_TARGET_FASTPATH_IO)) {
			mpi_request->IoFlags = cpu_to_le16(scmd->cmd_len |
			    MPI25_SCSIIO_IOFLAGS_FAST_PATH);
			ioc->put_smid_fast_path(ioc, smid, handle);
		} else
			ioc->put_smid_scsi_io(ioc, smid,
			    le16_to_cpu(mpi_request->DevHandle));
	} else
		ioc->put_smid_default(ioc, smid);

 out:
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	if(host_lock_mode)
		spin_unlock_irqrestore(shost->host_lock, irq_flags);
	#endif
	return rc;

}

/**
 * _scsih_normalize_sense - normalize descriptor and fixed format sense data
 * @sense_buffer: sense data returned by target
 * @data: normalized skey/asc/ascq
 *
 * Return nothing.
 */
static void
_scsih_normalize_sense(char *sense_buffer, struct sense_info *data)
{
	if ((sense_buffer[0] & 0x7F) >= 0x72) {
		/* descriptor format */
		data->skey = sense_buffer[1] & 0x0F;
		data->asc = sense_buffer[2];
		data->ascq = sense_buffer[3];
	} else {
		/* fixed format */
		data->skey = sense_buffer[2] & 0x0F;
		data->asc = sense_buffer[12];
		data->ascq = sense_buffer[13];
	}
}

/**
 * _scsih_scsi_ioc_info - translated non-succesfull SCSI_IO request
 * @ioc: per adapter object
 * @scmd: pointer to scsi command object
 * @mpi_reply: reply mf payload returned from firmware
 *
 * scsi_status - SCSI Status code returned from target device
 * scsi_state - state info associated with SCSI_IO determined by ioc
 * ioc_status - ioc supplied status info
 *
 * Return nothing.
 */
static void
_scsih_scsi_ioc_info(struct MPT3SAS_ADAPTER *ioc, struct scsi_cmnd *scmd,
	Mpi2SCSIIOReply_t *mpi_reply, u16 smid, u8 scsi_status,
	u16 error_response_count)
{
	u32 response_info;
	u8 *response_bytes;
	u16 ioc_status = le16_to_cpu(mpi_reply->IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	u8 scsi_state = mpi_reply->SCSIState;
	char *desc_ioc_state = NULL;
	char *desc_scsi_status = NULL;
	char *desc_scsi_state = ioc->tmp_string;
	u32 log_info = le32_to_cpu(mpi_reply->IOCLogInfo);
	struct _sas_device *sas_device = NULL;
	struct _pcie_device *pcie_device = NULL;
	struct scsi_target *starget = scmd->device->sdev_target;
	struct MPT3SAS_TARGET *priv_target = starget->hostdata;
	char *device_str = NULL;
	u8 function = mpi_reply->Function;

	if (!priv_target)
		return;

	if (ioc->warpdrive_msg)
		device_str = "WarpDrive";
	else
		device_str = "volume";

	if (log_info == 0x31170000)
		return;

	switch (ioc_status) {
	case MPI2_IOCSTATUS_SUCCESS:
		desc_ioc_state = "success";
		break;
	case MPI2_IOCSTATUS_INVALID_FUNCTION:
		desc_ioc_state = "invalid function";
		break;
	case MPI2_IOCSTATUS_SCSI_RECOVERED_ERROR:
		desc_ioc_state = "scsi recovered error";
		break;
	case MPI2_IOCSTATUS_SCSI_INVALID_DEVHANDLE:
		desc_ioc_state = "scsi invalid dev handle";
		break;
	case MPI2_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
		desc_ioc_state = "scsi device not there";
		break;
	case MPI2_IOCSTATUS_SCSI_DATA_OVERRUN:
		desc_ioc_state = "scsi data overrun";
		break;
	case MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN:
		desc_ioc_state = "scsi data underrun";
		break;
	case MPI2_IOCSTATUS_SCSI_IO_DATA_ERROR:
		desc_ioc_state = "scsi io data error";
		break;
	case MPI2_IOCSTATUS_SCSI_PROTOCOL_ERROR:
		desc_ioc_state = "scsi protocol error";
		break;
	case MPI2_IOCSTATUS_SCSI_TASK_TERMINATED:
		desc_ioc_state = "scsi task terminated";
		break;
	case MPI2_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:
		desc_ioc_state = "scsi residual mismatch";
		break;
	case MPI2_IOCSTATUS_SCSI_TASK_MGMT_FAILED:
		desc_ioc_state = "scsi task mgmt failed";
		break;
	case MPI2_IOCSTATUS_SCSI_IOC_TERMINATED:
		desc_ioc_state = "scsi ioc terminated";
		break;
	case MPI2_IOCSTATUS_SCSI_EXT_TERMINATED:
		desc_ioc_state = "scsi ext terminated";
		break;
	case MPI2_IOCSTATUS_EEDP_GUARD_ERROR:
		if (!ioc->disable_eedp_support) {
			desc_ioc_state = "eedp guard error";
			break;
		}
		/* fall through */
	case MPI2_IOCSTATUS_EEDP_REF_TAG_ERROR:
		if (!ioc->disable_eedp_support) {
			desc_ioc_state = "eedp ref tag error";
			break;
		}
		/* fall through */
	case MPI2_IOCSTATUS_EEDP_APP_TAG_ERROR:
		if (!ioc->disable_eedp_support) {
			desc_ioc_state = "eedp app tag error";
			break;
		}
		/* fall through */
	case MPI2_IOCSTATUS_INSUFFICIENT_POWER:
               	desc_ioc_state = "insufficient power";
               	break;
	default:
		desc_ioc_state = "unknown";
		break;
	}

	switch (scsi_status) {
	case MPI2_SCSI_STATUS_GOOD:
		desc_scsi_status = "good";
		break;
	case MPI2_SCSI_STATUS_CHECK_CONDITION:
		desc_scsi_status = "check condition";
		break;
	case MPI2_SCSI_STATUS_CONDITION_MET:
		desc_scsi_status = "condition met";
		break;
	case MPI2_SCSI_STATUS_BUSY:
		desc_scsi_status = "busy";
		break;
	case MPI2_SCSI_STATUS_INTERMEDIATE:
		desc_scsi_status = "intermediate";
		break;
	case MPI2_SCSI_STATUS_INTERMEDIATE_CONDMET:
		desc_scsi_status = "intermediate condmet";
		break;
	case MPI2_SCSI_STATUS_RESERVATION_CONFLICT:
		desc_scsi_status = "reservation conflict";
		break;
	case MPI2_SCSI_STATUS_COMMAND_TERMINATED:
		desc_scsi_status = "command terminated";
		break;
	case MPI2_SCSI_STATUS_TASK_SET_FULL:
		desc_scsi_status = "task set full";
		break;
	case MPI2_SCSI_STATUS_ACA_ACTIVE:
		desc_scsi_status = "aca active";
		break;
	case MPI2_SCSI_STATUS_TASK_ABORTED:
		desc_scsi_status = "task aborted";
		break;
	default:
		desc_scsi_status = "unknown";
		break;
	}

	desc_scsi_state[0] = '\0';
	if (!scsi_state)
		desc_scsi_state = " ";
	if (scsi_state & MPI2_SCSI_STATE_RESPONSE_INFO_VALID)
		strcat(desc_scsi_state, "response info ");
	if (scsi_state & MPI2_SCSI_STATE_TERMINATED)
		strcat(desc_scsi_state, "state terminated ");
	if (scsi_state & MPI2_SCSI_STATE_NO_SCSI_STATUS)
		strcat(desc_scsi_state, "no status ");
	if (scsi_state & MPI2_SCSI_STATE_AUTOSENSE_FAILED)
		strcat(desc_scsi_state, "autosense failed ");
	if (scsi_state & MPI2_SCSI_STATE_AUTOSENSE_VALID)
		strcat(desc_scsi_state, "autosense valid ");

	scsi_print_command(scmd);

	if (priv_target->flags & MPT_TARGET_FLAGS_VOLUME) {
		printk(MPT3SAS_WARN_FMT "\t%s wwid(0x%016llx)\n", ioc->name,
		    device_str, (unsigned long long)priv_target->sas_address);
	} else if (priv_target->flags & MPT_TARGET_FLAGS_PCIE_DEVICE){
		pcie_device = mpt3sas_get_pdev_from_target(ioc, priv_target);
		if (pcie_device) {
			printk(MPT3SAS_WARN_FMT "\twwid(0x%016llx), port(%d)\n", ioc->name,
				(unsigned long long)pcie_device->wwid, pcie_device->port_num);
			if(pcie_device->enclosure_handle != 0)
				printk(MPT3SAS_WARN_FMT "\tenclosure logical id(0x%016llx), "
					"slot(%d)\n", ioc->name,
					(unsigned long long)pcie_device->enclosure_logical_id,
					pcie_device->slot);
			if (pcie_device->connector_name[0])
				printk(MPT3SAS_WARN_FMT
					"\tenclosure level(0x%04x), connector name( %s)\n",
					ioc->name, pcie_device->enclosure_level,
					pcie_device->connector_name);
			pcie_device_put(pcie_device);
		}
	} else {
		sas_device = mpt3sas_get_sdev_from_target(ioc, priv_target);
		if (sas_device) {
			printk(MPT3SAS_WARN_FMT "\t%s: sas_address(0x%016llx), "
			    "phy(%d)\n", ioc->name, __func__, (unsigned long long)
			    sas_device->sas_address, sas_device->phy);
			_scsih_display_enclosure_chassis_info(ioc, sas_device, NULL, NULL);
			sas_device_put(sas_device);
		}
	}

	printk(MPT3SAS_WARN_FMT "\thandle(0x%04x), ioc_status(%s)(0x%04x), "
	    "smid(%d)\n", ioc->name, le16_to_cpu(mpi_reply->DevHandle),
	    desc_ioc_state, ioc_status, smid);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	printk(MPT3SAS_WARN_FMT "\trequest_len(%d), underflow(%d), "
	    "resid(%d)\n", ioc->name, scmd->request_bufflen, scmd->underflow,
	    scmd->resid);
#else
	printk(MPT3SAS_WARN_FMT "\trequest_len(%d), underflow(%d), "
	    "resid(%d)\n", ioc->name, scsi_bufflen(scmd), scmd->underflow,
	    scsi_get_resid(scmd));
#endif
	if (function == MPI2_FUNCTION_NVME_ENCAPSULATED) {
		printk(MPT3SAS_WARN_FMT "\tsc->result(0x%08x)\n", ioc->name,
		    scmd->result);
	} else {
		printk(MPT3SAS_WARN_FMT "\ttag(%d), transfer_count(%d), "
		    "sc->result(0x%08x)\n", ioc->name,
		    le16_to_cpu(mpi_reply->TaskTag),
		    le32_to_cpu(mpi_reply->TransferCount), scmd->result);
	}

	printk(MPT3SAS_WARN_FMT "\tscsi_status(%s)(0x%02x), "
	    "scsi_state(%s)(0x%02x)\n", ioc->name, desc_scsi_status,
	    scsi_status, desc_scsi_state, scsi_state);

	if (scsi_state & MPI2_SCSI_STATE_AUTOSENSE_VALID) {
		struct sense_info data;
		_scsih_normalize_sense(scmd->sense_buffer, &data);
		printk(MPT3SAS_WARN_FMT "\t[sense_key,asc,ascq]: "
		    "[0x%02x,0x%02x,0x%02x], count(%d)\n", ioc->name, data.skey,
		    data.asc, data.ascq, le32_to_cpu(mpi_reply->SenseCount));
	}

	if (function == MPI2_FUNCTION_NVME_ENCAPSULATED) {
		if(error_response_count) {
			struct sense_info data;
			_scsih_normalize_sense(scmd->sense_buffer, &data);
			printk(MPT3SAS_WARN_FMT "\t[sense_key,asc,ascq]: "
			    "[0x%02x,0x%02x,0x%02x], count(%d)\n", ioc->name,
			    data.skey, data.asc, data.ascq,
			    error_response_count);
		}
	}

	if (scsi_state & MPI2_SCSI_STATE_RESPONSE_INFO_VALID) {
		response_info = le32_to_cpu(mpi_reply->ResponseInfo);
		response_bytes = (u8 *)&response_info;
		_scsih_response_code(ioc, response_bytes[0]);
	}
}

/**
 * _scsih_turn_on_pfa_led - illuminate PFA LED
 * @ioc: per adapter object
 * @handle: device handle
 * Context: process
 *
 * Return nothing.
 */
static void
_scsih_turn_on_pfa_led(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	Mpi2SepReply_t mpi_reply;
	Mpi2SepRequest_t mpi_request;
	struct _sas_device *sas_device;

	sas_device = mpt3sas_get_sdev_by_handle(ioc, handle);
	if (!sas_device)
		return;

	memset(&mpi_request, 0, sizeof(Mpi2SepRequest_t));
	mpi_request.Function = MPI2_FUNCTION_SCSI_ENCLOSURE_PROCESSOR;
	mpi_request.Action = MPI2_SEP_REQ_ACTION_WRITE_STATUS;
	mpi_request.SlotStatus =
	    cpu_to_le32(MPI2_SEP_REQ_SLOTSTATUS_PREDICTED_FAULT);
	mpi_request.DevHandle = cpu_to_le16(handle);
	mpi_request.Flags = MPI2_SEP_REQ_FLAGS_DEVHANDLE_ADDRESS;
	if ((mpt3sas_base_scsi_enclosure_processor(ioc, &mpi_reply,
	    &mpi_request)) != 0) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n", ioc->name,
		__FILE__, __LINE__, __func__);
		goto out;
	}
	sas_device->pfa_led_on = 1;

	if (mpi_reply.IOCStatus || mpi_reply.IOCLogInfo) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "enclosure_processor: "
		    "ioc_status (0x%04x), loginfo(0x%08x)\n", ioc->name,
		    le16_to_cpu(mpi_reply.IOCStatus),
		    le32_to_cpu(mpi_reply.IOCLogInfo)));
		goto out;
	}
out:
	sas_device_put(sas_device);
}
/**
 * _scsih_turn_off_pfa_led - turn off PFA LED
 * @ioc: per adapter object
 * @sas_device: sas device whose PFA LED has to turned off
 * Context: process
 *
 * Return nothing.
 */
/*TODO -- No Support for Turning of PFA LED yet*/
static void
_scsih_turn_off_pfa_led(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_device *sas_device)
{
	Mpi2SepReply_t mpi_reply;
	Mpi2SepRequest_t mpi_request;

	memset(&mpi_request, 0, sizeof(Mpi2SepRequest_t));
	mpi_request.Function = MPI2_FUNCTION_SCSI_ENCLOSURE_PROCESSOR;
	mpi_request.Action = MPI2_SEP_REQ_ACTION_WRITE_STATUS;
	mpi_request.SlotStatus = 0;
	mpi_request.Slot = cpu_to_le16(sas_device->slot);
	mpi_request.DevHandle = 0;
	mpi_request.EnclosureHandle = cpu_to_le16(sas_device->enclosure_handle);
	mpi_request.Flags = MPI2_SEP_REQ_FLAGS_ENCLOSURE_SLOT_ADDRESS;
	if ((mpt3sas_base_scsi_enclosure_processor(ioc, &mpi_reply,
	    &mpi_request)) != 0) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n", ioc->name,
		__FILE__, __LINE__, __func__);
		return;
	}

	if (mpi_reply.IOCStatus || mpi_reply.IOCLogInfo) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "enclosure_processor: "
		    "ioc_status (0x%04x), loginfo(0x%08x)\n", ioc->name,
		    le16_to_cpu(mpi_reply.IOCStatus),
		    le32_to_cpu(mpi_reply.IOCLogInfo)));
		return;
	}
}
/**
 * _scsih_send_event_to_turn_on_pfa_led - fire delayed event
 * @ioc: per adapter object
 * @handle: device handle
 * Context: interrupt.
 *
 * Return nothing.
 */
static void
_scsih_send_event_to_turn_on_pfa_led(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct fw_event_work *fw_event;

	fw_event = alloc_fw_event_work(0);
	if (!fw_event)
		return;
	fw_event->event = MPT3SAS_TURN_ON_PFA_LED;
	fw_event->device_handle = handle;
	fw_event->ioc = ioc;
	_scsih_fw_event_add(ioc, fw_event);
	fw_event_work_put(fw_event);
}

/**
 * _scsih_smart_predicted_fault - process smart errors
 * @ioc: per adapter object
 * @handle: device handle
 * Context: interrupt.
 *
 * Return nothing.
 */
static void
_scsih_smart_predicted_fault(struct MPT3SAS_ADAPTER *ioc, u16 handle,
						 u8 from_sata_smart_polling)
{
	struct scsi_target *starget;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	Mpi2EventNotificationReply_t *event_reply;
	Mpi2EventDataSasDeviceStatusChange_t *event_data;
	struct _sas_device *sas_device;
	ssize_t sz;
	unsigned long flags;

	/* only handle non-raid devices */
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_handle(ioc, handle);
	if (!sas_device) {
		goto out_unlock;
	}
	starget = sas_device->starget;
	sas_target_priv_data = starget->hostdata;

	if ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_RAID_COMPONENT) ||
	   ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME)))
		goto out_unlock;
	_scsih_display_enclosure_chassis_info(NULL, sas_device, NULL, starget);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	/* Issue SEP request message to turn on the PFA LED whenever this
 	 * function is invoked from SATA SMART polling thread or when subsystem
 	 * sub system vendor is IBM */
	if (from_sata_smart_polling ||
	    ioc->pdev->subsystem_vendor == PCI_VENDOR_ID_IBM)
		_scsih_send_event_to_turn_on_pfa_led(ioc, handle);

	/* insert into event log */
	sz = offsetof(Mpi2EventNotificationReply_t, EventData) +
	     sizeof(Mpi2EventDataSasDeviceStatusChange_t);
	event_reply = kzalloc(sz, GFP_ATOMIC);
	if (!event_reply) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}

	event_reply->Function = MPI2_FUNCTION_EVENT_NOTIFICATION;
	event_reply->Event =
	    cpu_to_le16(MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE);
	event_reply->MsgLength = sz/4;
	event_reply->EventDataLength =
	    cpu_to_le16(sizeof(Mpi2EventDataSasDeviceStatusChange_t)/4);
	event_data = (Mpi2EventDataSasDeviceStatusChange_t *)
	    event_reply->EventData;
	event_data->ReasonCode = MPI2_EVENT_SAS_DEV_STAT_RC_SMART_DATA;
	event_data->ASC = 0x5D;
	event_data->DevHandle = cpu_to_le16(handle);
	event_data->SASAddress = cpu_to_le64(sas_target_priv_data->sas_address);
	mpt3sas_ctl_add_to_event_log(ioc, event_reply);
	kfree(event_reply);
out:
	if (sas_device)
		sas_device_put(sas_device);
	return;

out_unlock:
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	goto out;
}

/** _scsih_nvme_trans_status_code
 *
 * Convert Native NVMe command error status to
 * equivalent SCSI error status.
 *
 * Returns appropriate scsi_status
 */
static u8 _scsih_nvme_trans_status_code(u16 nvme_status, struct sense_info *data)
{
	u8 status = MPI2_SCSI_STATUS_GOOD;

	switch (get_nvme_sct_with_sc(nvme_status)) {
	/* Generic Command Status */
	case NVME_SC_SUCCESS:
		status = MPI2_SCSI_STATUS_GOOD;
		data->skey = NO_SENSE;
		data->asc = SCSI_ASC_NO_SENSE;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_INVALID_OPCODE:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = ILLEGAL_REQUEST;
		data->asc = SCSI_ASC_ILLEGAL_COMMAND;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_INVALID_FIELD:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = ILLEGAL_REQUEST;
		data->asc = SCSI_ASC_INVALID_CDB;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_DATA_XFER_ERROR:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = MEDIUM_ERROR;
		data->asc = SCSI_ASC_NO_SENSE;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_POWER_LOSS:
		status = MPI2_SCSI_STATUS_TASK_ABORTED;
		data->skey = ABORTED_COMMAND;
		data->asc = SCSI_ASC_WARNING;
		data->ascq = SCSI_ASCQ_POWER_LOSS_EXPECTED;
		break;
	case NVME_SC_INTERNAL:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = HARDWARE_ERROR;
		data->asc = SCSI_ASC_INTERNAL_TARGET_FAILURE;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_ABORT_REQ:
		status = MPI2_SCSI_STATUS_TASK_ABORTED;
		data->skey = ABORTED_COMMAND;
		data->asc = SCSI_ASC_NO_SENSE;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_ABORT_QUEUE:
		status = MPI2_SCSI_STATUS_TASK_ABORTED;
		data->skey = ABORTED_COMMAND;
		data->asc = SCSI_ASC_NO_SENSE;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_FUSED_FAIL:
		status = MPI2_SCSI_STATUS_TASK_ABORTED;
		data->skey = ABORTED_COMMAND;
		data->asc = SCSI_ASC_NO_SENSE;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_FUSED_MISSING:
		status = MPI2_SCSI_STATUS_TASK_ABORTED;
		data->skey = ABORTED_COMMAND;
		data->asc = SCSI_ASC_NO_SENSE;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_INVALID_NS:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = ILLEGAL_REQUEST;
		data->asc = SCSI_ASC_ACCESS_DENIED_INVALID_LUN_ID;
		data->ascq = SCSI_ASCQ_INVALID_LUN_ID;
		break;
	case NVME_SC_LBA_RANGE:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = ILLEGAL_REQUEST;
		data->asc = SCSI_ASC_ILLEGAL_BLOCK;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_CAP_EXCEEDED:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = MEDIUM_ERROR;
		data->asc = SCSI_ASC_NO_SENSE;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_NS_NOT_READY:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = NOT_READY;
		data->asc = SCSI_ASC_LUN_NOT_READY;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;

	/* Command Specific Status */
	case NVME_SC_INVALID_FORMAT:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = ILLEGAL_REQUEST;
		data->asc = SCSI_ASC_FORMAT_COMMAND_FAILED;
		data->ascq = SCSI_ASCQ_FORMAT_COMMAND_FAILED;
		break;
	case NVME_SC_BAD_ATTRIBUTES:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = ILLEGAL_REQUEST;
		data->asc = SCSI_ASC_INVALID_CDB;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;

	/* Media Errors */
	case NVME_SC_WRITE_FAULT:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = MEDIUM_ERROR;
		data->asc = SCSI_ASC_PERIPHERAL_DEV_WRITE_FAULT;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_READ_ERROR:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = MEDIUM_ERROR;
		data->asc = SCSI_ASC_UNRECOVERED_READ_ERROR;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_GUARD_CHECK:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = MEDIUM_ERROR;
		data->asc = SCSI_ASC_LOG_BLOCK_GUARD_CHECK_FAILED;
		data->ascq = SCSI_ASCQ_LOG_BLOCK_GUARD_CHECK_FAILED;
		break;
	case NVME_SC_APPTAG_CHECK:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = MEDIUM_ERROR;
		data->asc = SCSI_ASC_LOG_BLOCK_APPTAG_CHECK_FAILED;
		data->ascq = SCSI_ASCQ_LOG_BLOCK_APPTAG_CHECK_FAILED;
		break;
	case NVME_SC_REFTAG_CHECK:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = MEDIUM_ERROR;
		data->asc = SCSI_ASC_LOG_BLOCK_REFTAG_CHECK_FAILED;
		data->ascq = SCSI_ASCQ_LOG_BLOCK_REFTAG_CHECK_FAILED;
		break;
	case NVME_SC_COMPARE_FAILED:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = MISCOMPARE;
		data->asc = SCSI_ASC_MISCOMPARE_DURING_VERIFY;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	case NVME_SC_ACCESS_DENIED:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = ILLEGAL_REQUEST;
		data->asc = SCSI_ASC_ACCESS_DENIED_INVALID_LUN_ID;
		data->ascq = SCSI_ASCQ_INVALID_LUN_ID;
		break;

	/* Unspecified/Default */
	case NVME_SC_CMDID_CONFLICT:
	case NVME_SC_CMD_SEQ_ERROR:
	case NVME_SC_CQ_INVALID:
	case NVME_SC_QID_INVALID:
	case NVME_SC_QUEUE_SIZE:
	case NVME_SC_ABORT_LIMIT:
	case NVME_SC_ABORT_MISSING:
	case NVME_SC_ASYNC_LIMIT:
	case NVME_SC_FIRMWARE_SLOT:
	case NVME_SC_FIRMWARE_IMAGE:
	case NVME_SC_INVALID_VECTOR:
	case NVME_SC_INVALID_LOG_PAGE:
	default:
		status = MPI2_SCSI_STATUS_CHECK_CONDITION;
		data->skey = ILLEGAL_REQUEST;
		data->asc = SCSI_ASC_NO_SENSE;
		data->ascq = SCSI_ASCQ_CAUSE_NOT_REPORTABLE;
		break;
	}

	return status;
}

/** _scsih_complete_nvme_unmap
 *
 * Complete native NVMe command issued using NVMe Encapsulated
 * Request Message.
 */
static u8
_scsih_complete_nvme_unmap(struct MPT3SAS_ADAPTER *ioc, u16 smid,
		struct scsi_cmnd *scmd, u32 reply, u16 *error_response_count)
{
	Mpi26NVMeEncapsulatedErrorReply_t *mpi_reply;
	struct nvme_completion *nvme_completion = NULL;
	u8 scsi_status = MPI2_SCSI_STATUS_GOOD;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	*error_response_count = le16_to_cpu(mpi_reply->ErrorResponseCount);
	if (*error_response_count) {
		struct sense_info data;
		nvme_completion =
		 (struct nvme_completion *) mpt3sas_base_get_sense_buffer(ioc,
									 smid);
		scsi_status = _scsih_nvme_trans_status_code(
				le16_to_cpu(nvme_completion->status), &data);

		scmd->sense_buffer[0] = FIXED_SENSE_DATA;
		scmd->sense_buffer[2] = data.skey;
		scmd->sense_buffer[12] = data.asc;
		scmd->sense_buffer[13] = data.ascq;
	}

	return scsi_status;
}

/**
 * _scsih_io_done - scsi request callback
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Callback handler when using scsih_qcmd.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_io_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index, u32 reply)
{
	Mpi25SCSIIORequest_t *mpi_request;
	Mpi2SCSIIOReply_t *mpi_reply;
	struct scsi_cmnd *scmd;
	u16 ioc_status, error_response_count=0;
	u32 xfer_cnt;
	u8 scsi_state;
	u8 scsi_status;
	u32 log_info;
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	u32 response_code = 0;
	struct scsiio_tracker *st;

	scmd = mpt3sas_scsih_scsi_lookup_get(ioc, smid);
	if (scmd == NULL)
		return 1;

	_scsih_set_satl_pending(scmd, false);

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (mpi_reply == NULL) {
		scmd->result = DID_OK << 16;
		goto out;
	}

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target ||
	     sas_device_priv_data->sas_target->deleted) {
		scmd->result = DID_NO_CONNECT << 16;
		goto out;
	}
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus);

	/*
	 * WARPDRIVE: If direct_io is set then it is directIO,
	 * the failed direct I/O should be redirected to volume
	 */
	st = mpt3sas_base_scsi_cmd_priv(scmd);
	if (st->direct_io &&
	    ((ioc_status & MPI2_IOCSTATUS_MASK)
	    != MPI2_IOCSTATUS_SCSI_TASK_TERMINATED)) {
#ifdef MPT2SAS_WD_DDIOCOUNT
		ioc->ddio_err_count++;
#endif
#ifdef MPT2SAS_WD_LOGGING
		if (ioc->logging_level & MPT_DEBUG_SCSI) {
			printk(MPT3SAS_INFO_FMT "scmd(%p) failed when issued"
			    "as direct IO, retrying\n", ioc->name, scmd);
			scsi_print_command(scmd);
		}
#endif
		st->scmd = scmd;
		st->direct_io = 0;
		memcpy(mpi_request->CDB.CDB32, scmd->cmnd, scmd->cmd_len);
		mpi_request->DevHandle =
		    cpu_to_le16(sas_device_priv_data->sas_target->handle);
		ioc->put_smid_scsi_io(ioc, smid,
		    sas_device_priv_data->sas_target->handle);
		return 0;
	}

	/* turning off TLR */
	scsi_state = mpi_reply->SCSIState;
	if (scsi_state & MPI2_SCSI_STATE_RESPONSE_INFO_VALID)
		response_code =
		    le32_to_cpu(mpi_reply->ResponseInfo) & 0xFF;
	if (!sas_device_priv_data->tlr_snoop_check) {
		sas_device_priv_data->tlr_snoop_check++;
		if ((sas_device_priv_data->flags & MPT_DEVICE_TLR_ON) &&
		    response_code == MPI2_SCSITASKMGMT_RSP_INVALID_FRAME)
			sas_device_priv_data->flags &=
			    ~MPT_DEVICE_TLR_ON;
	}

	if (ioc_status & MPI2_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE)
		log_info =  le32_to_cpu(mpi_reply->IOCLogInfo);
	else
		log_info = 0;
	ioc_status &= MPI2_IOCSTATUS_MASK;
	scsi_status = mpi_reply->SCSIStatus;

	if (mpi_request->Function == MPI2_FUNCTION_NVME_ENCAPSULATED &&
		sas_device_priv_data->sas_target->pcie_dev &&
			scmd->cmnd[0] == UNMAP) {
		scsi_status =
			_scsih_complete_nvme_unmap(ioc, smid, scmd, reply,
					&error_response_count);
		xfer_cnt = le32_to_cpu(mpi_request->DataLength);
	} else
		xfer_cnt = le32_to_cpu(mpi_reply->TransferCount);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	scmd->resid = scmd->request_bufflen - xfer_cnt;
#else
	scsi_set_resid(scmd, scsi_bufflen(scmd) - xfer_cnt);
#endif

	if (ioc_status == MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN && xfer_cnt == 0 &&
	    (scsi_status == MPI2_SCSI_STATUS_BUSY ||
	     scsi_status == MPI2_SCSI_STATUS_RESERVATION_CONFLICT ||
	     scsi_status == MPI2_SCSI_STATUS_TASK_SET_FULL)) {
		ioc_status = MPI2_IOCSTATUS_SUCCESS;
	}

	if (scsi_state & MPI2_SCSI_STATE_AUTOSENSE_VALID) {
		struct sense_info data;
		const void *sense_data = mpt3sas_base_get_sense_buffer(ioc,
		    smid);
		u32 sz = min_t(u32, SCSI_SENSE_BUFFERSIZE,
		    le32_to_cpu(mpi_reply->SenseCount));
		memcpy(scmd->sense_buffer, sense_data, sz);
		_scsih_normalize_sense(scmd->sense_buffer, &data);
		/* failure prediction threshold exceeded */
		if (data.asc == 0x5D)
			_scsih_smart_predicted_fault(ioc,
			    le16_to_cpu(mpi_reply->DevHandle), 0);
		mpt3sas_trigger_scsi(ioc, data.skey, data.asc, data.ascq);

	}
	switch (ioc_status) {
	case MPI2_IOCSTATUS_BUSY:
	case MPI2_IOCSTATUS_INSUFFICIENT_RESOURCES:
		scmd->result = SAM_STAT_BUSY;
		break;

	case MPI2_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
		scmd->result = DID_NO_CONNECT << 16;
		break;

	case MPI2_IOCSTATUS_SCSI_IOC_TERMINATED:
		if (sas_device_priv_data->block) {
			scmd->result = DID_TRANSPORT_DISRUPTED << 16;
			goto out;
		}
		if (log_info == 0x31110630) {
			if (scmd->retries > 2) {
				scmd->result = DID_NO_CONNECT << 16;
				scsi_device_set_state(scmd->device,
				    SDEV_OFFLINE);
			} else {
				scmd->result = DID_SOFT_ERROR << 16;
				scmd->device->expecting_cc_ua = 1;
			}
			break;
		} else if(log_info == 0x32010081) {
			mpt3sas_set_requeue_or_reset(scmd);
			break;
		} else if ((scmd->device->channel == RAID_CHANNEL) &&
			   (scsi_state == (MPI2_SCSI_STATE_TERMINATED |
			    MPI2_SCSI_STATE_NO_SCSI_STATUS))) {
			mpt3sas_set_requeue_or_reset(scmd);
			break;
		}
		scmd->result = DID_SOFT_ERROR << 16;
		break;
	case MPI2_IOCSTATUS_SCSI_TASK_TERMINATED:
	case MPI2_IOCSTATUS_SCSI_EXT_TERMINATED:
		mpt3sas_set_requeue_or_reset(scmd);
		break;

	case MPI2_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:
		if ((xfer_cnt == 0) || (scmd->underflow > xfer_cnt))
			scmd->result = DID_SOFT_ERROR << 16;
		else
			scmd->result = (DID_OK << 16) | scsi_status;
		break;

	case MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN:
		scmd->result = (DID_OK << 16) | scsi_status;

		if ((scsi_state & MPI2_SCSI_STATE_AUTOSENSE_VALID))
			break;

		if (xfer_cnt < scmd->underflow) {
			if (scsi_status == SAM_STAT_BUSY)
				scmd->result = SAM_STAT_BUSY;
			else
				scmd->result = DID_SOFT_ERROR << 16;
		} else if (scsi_state & (MPI2_SCSI_STATE_AUTOSENSE_FAILED |
		     MPI2_SCSI_STATE_NO_SCSI_STATUS))
			scmd->result = DID_SOFT_ERROR << 16;
		else if (scsi_state & MPI2_SCSI_STATE_TERMINATED)
			mpt3sas_set_requeue_or_reset(scmd);
		else if (!xfer_cnt && scmd->cmnd[0] == REPORT_LUNS) {
			mpi_reply->SCSIState = MPI2_SCSI_STATE_AUTOSENSE_VALID;
			mpi_reply->SCSIStatus = SAM_STAT_CHECK_CONDITION;
			scmd->result = (DRIVER_SENSE << 24) |
			    SAM_STAT_CHECK_CONDITION;
			scmd->sense_buffer[0] = 0x70;
			scmd->sense_buffer[2] = ILLEGAL_REQUEST;
			scmd->sense_buffer[12] = 0x20;
			scmd->sense_buffer[13] = 0;
		}
		break;

	case MPI2_IOCSTATUS_SCSI_DATA_OVERRUN:
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
		scmd->resid = 0;
#else
		scsi_set_resid(scmd, 0);
#endif
		/* fall through */
	case MPI2_IOCSTATUS_SCSI_RECOVERED_ERROR:
	case MPI2_IOCSTATUS_SUCCESS:
		scmd->result = (DID_OK << 16) | scsi_status;
		if (response_code ==
		    MPI2_SCSITASKMGMT_RSP_INVALID_FRAME ||
		    (scsi_state & (MPI2_SCSI_STATE_AUTOSENSE_FAILED |
		     MPI2_SCSI_STATE_NO_SCSI_STATUS)))
			scmd->result = DID_SOFT_ERROR << 16;
		else if (scsi_state & MPI2_SCSI_STATE_TERMINATED)
			mpt3sas_set_requeue_or_reset(scmd);
		break;

	case MPI2_IOCSTATUS_EEDP_GUARD_ERROR:
	case MPI2_IOCSTATUS_EEDP_REF_TAG_ERROR:
		/* fall through */
	case MPI2_IOCSTATUS_EEDP_APP_TAG_ERROR:
		if (!ioc->disable_eedp_support) {
			_scsih_eedp_error_handling(scmd, ioc_status);
			break;
		}
		/* fall through */
	case MPI2_IOCSTATUS_SCSI_PROTOCOL_ERROR:
	case MPI2_IOCSTATUS_INVALID_FUNCTION:
	case MPI2_IOCSTATUS_INVALID_SGL:
	case MPI2_IOCSTATUS_INTERNAL_ERROR:
	case MPI2_IOCSTATUS_INVALID_FIELD:
	case MPI2_IOCSTATUS_INVALID_STATE:
	case MPI2_IOCSTATUS_SCSI_IO_DATA_ERROR:
	case MPI2_IOCSTATUS_SCSI_TASK_MGMT_FAILED:
	case MPI2_IOCSTATUS_INSUFFICIENT_POWER:
	default:
		scmd->result = DID_SOFT_ERROR << 16;
		break;

	}

	if (scmd->result && (ioc->logging_level & MPT_DEBUG_REPLY))
		_scsih_scsi_ioc_info(ioc , scmd, mpi_reply, smid, scsi_status,
				     error_response_count);

 out:

#if defined(CRACK_MONKEY_EEDP)
	if (!ioc->disable_eedp_support) {
		if (scmd->cmnd[0] == INQUIRY && scmd->host_scribble) {
			char *some_data = scmd->host_scribble;
			char inq_str[16];

			memset(inq_str, 0, 16);
			strncpy(inq_str, &some_data[16], 10);
			if (!strcmp(inq_str, "Harpy Disk"))
				some_data[5] |= 1;
			scmd->host_scribble = NULL;
		}
	}
#endif /* CRACK_MONKEY_EEDP */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	if (scmd->use_sg)
		pci_unmap_sg(ioc->pdev, (struct scatterlist *)
		    scmd->request_buffer, scmd->use_sg,
		    scmd->sc_data_direction);
	else if (scmd->request_bufflen)
		pci_unmap_single(ioc->pdev, scmd->SCp.dma_handle,
		    scmd->request_bufflen, scmd->sc_data_direction);
#else
	scsi_dma_unmap(scmd);

	if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
		if (mpi_request->DMAFlags == MPI25_TA_DMAFLAGS_OP_D_H_D_D)
		{
			dma_unmap_sg(&ioc->pdev->dev, scsi_prot_sglist(scmd),
		    	scsi_prot_sg_count(scmd), scmd->sc_data_direction);
		}
	}
#endif

	mpt3sas_base_free_smid(ioc, smid);
	scmd->scsi_done(scmd);
	return 0;
}

/**
 * _scsih_update_vphys_after_reset - update the Port's vphys_list after reset
 * @ioc: per adapter object
 *
 * Returns nothing.
 */
static void
_scsih_update_vphys_after_reset(struct MPT3SAS_ADAPTER *ioc)
{
	u16 sz, ioc_status;
	int i;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasIOUnitPage0_t *sas_iounit_pg0 = NULL;
	u16 attached_handle;
	u64 attached_sas_addr;
	u8 found=0, port_id;
	Mpi2SasPhyPage0_t phy_pg0;
	struct hba_port *port, *port_next, *mport;
	struct virtual_phy *vphy, *vphy_next;
	struct _sas_device *sas_device;

	/*
	 * Mark all the vphys objects as dirty.
	 */
	list_for_each_entry_safe(port, port_next, &ioc->port_table_list, list) {
		if (!port->vphys_mask)
			continue;
		list_for_each_entry_safe(vphy, vphy_next, &port->vphys_list, list) {
			vphy->flags |= MPT_VPHY_FLAG_DIRTY_PHY; 
		}
	}

	/*
	 * Read SASIOUnitPage0 to get each HBA Phy's data.
	 */
	sz = offsetof(Mpi2SasIOUnitPage0_t, PhyData) + (ioc->sas_hba.num_phys
	    * sizeof(Mpi2SasIOUnit0PhyData_t));
	sas_iounit_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg0) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}
	if ((mpt3sas_config_get_sas_iounit_pg0(ioc, &mpi_reply,
	    sas_iounit_pg0, sz)) != 0)
		goto out;
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
		goto out;

	/*
	 * Loop over each HBA Phy.
	 */
	for (i = 0; i < ioc->sas_hba.num_phys; i++) {

		/*
		 * Check whether Phy's Negotiation Link Rate is > 1.5G or not.
		 */
		if ((sas_iounit_pg0->PhyData[i].NegotiatedLinkRate >> 4) <
						 MPI2_SAS_NEG_LINK_RATE_1_5)
			continue;

		/*
		 * Check whether Phy is connected to SEP device or not,
		 * if it is SEP device then read the Phy's SASPHYPage0 data to
		 * determine whether Phy is a virtual Phy or not. if it is
		 * virtual phy then it is conformed that the attached remote
		 * device is a HBA's vSES device.
		 */
		if (!(le32_to_cpu(sas_iounit_pg0->PhyData[i].ControllerPhyDeviceInfo) &
                    MPI2_SAS_DEVICE_INFO_SEP))
			continue;
		 if ((mpt3sas_config_get_phy_pg0(ioc, &mpi_reply, &phy_pg0,
		    i))) {
			printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			continue;
		}
		if (!(le32_to_cpu(phy_pg0.PhyInfo) & MPI2_SAS_PHYINFO_VIRTUAL_PHY))
			continue;

		/*
		 * Get the vSES device's SAS Address.
		 */
		attached_handle = le16_to_cpu(sas_iounit_pg0->PhyData[i].
								AttachedDevHandle);
		if (_scsih_get_sas_address(ioc, attached_handle, &attached_sas_addr)
			!= 0) {
                	printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
                	    ioc->name, __FILE__, __LINE__, __func__);
			continue;
        	}
	
		found = 0;
		port = port_next = NULL;

		/*
		 * Loop over each virtual_phy object from each port's vphys_list.
		 */	
		list_for_each_entry_safe(port, port_next, &ioc->port_table_list, list) {
			if (!port->vphys_mask)
				continue;
			list_for_each_entry_safe(vphy, vphy_next, &port->vphys_list, list) {
				
				/*
				 * Continue with next virtual_phy object
				 * if the object is not marked as dirty.
				 */
				if (!(vphy->flags & MPT_VPHY_FLAG_DIRTY_PHY))
					continue;

				/*
				 * Continue with next virtual_phy object
				 * if the object's SAS Address is not equals
				 * to current Phy's vSES device SAS Address.
				 */
				if (vphy->sas_address != attached_sas_addr)
					continue;

				/*
				 * Enable current Phy number bit in object's
				 * phy_mask field.
				 */
				if (!(vphy->phy_mask & (1 << i)))
					vphy->phy_mask = (1 << i);

				/*
				 * Get hba_port object from hba_port table
				 * corresponding to current phy's Port ID.
				 * if there is no hba_port object corresponding
				 * to Phy's Port ID then create a new hba_port
				 * object & add to hba_port table.
				 */
				port_id = sas_iounit_pg0->PhyData[i].Port;
				mport = mpt3sas_get_port_by_id(ioc, port_id, 1);
				if (!mport) {
					mport = kzalloc(sizeof(struct hba_port), GFP_KERNEL);
					if (!mport) {
						printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
						ioc->name, __FILE__, __LINE__, __func__);
						break;
					}
					mport->port_id = port_id;
					printk(MPT3SAS_INFO_FMT "%s: hba_port entry: %p,"
					" port: %d is added to hba_port list\n",
					ioc->name, __func__, mport, mport->port_id);
					list_add_tail(&mport->list, &ioc->port_table_list);
				}

				/* 
				 * If mport & port pointers are not pointing to
				 * same hba_port object then it means that vSES
				 * device's Port ID got changed after reset and
				 * hence move current virtual_phy object from
				 * port's vphys_list to mport's vphys_list.
				 */
				if (port != mport) {
					if (!mport->vphys_mask)
						INIT_LIST_HEAD(&mport->vphys_list);

					mport->vphys_mask |= (1 << i);
					port->vphys_mask &= ~(1 << i);
					list_move(&vphy->list, &mport->vphys_list);

					sas_device = mpt3sas_get_sdev_by_addr(ioc,
					    attached_sas_addr, port);
					if (sas_device)
						sas_device->port = mport;
				}

				/*
				 * Earlier while updating the hba_port table,
				 * it is determined that there is no other
				 * direct attached device with mport's Port ID,
				 * Hence mport was marked as dirty. Only vSES
				 * device has this Port ID, so unmark the mport
				 * as dirt.
				 */
				if (mport->flags & HBA_PORT_FLAG_DIRTY_PORT) {
					mport->sas_address = 0;
					mport->phy_mask = 0;
					mport->flags &= ~HBA_PORT_FLAG_DIRTY_PORT;
				}

				/*
				 * Unmark current virtual_phy object as dirty.
				 */
				vphy->flags &= ~MPT_VPHY_FLAG_DIRTY_PHY;
				found = 1;
				break;
			}
			if (found)
				break;
		}
	}
out:
	kfree(sas_iounit_pg0);
}

/**
 * _scsih_get_port_table_after_reset - Construct temporary port table
 * @ioc: per adapter object
 * @port_table: address where port table needs to be constructed
 *
 * return number of HBA port entries available after reset.
 */
static u8
_scsih_get_port_table_after_reset(struct MPT3SAS_ADAPTER *ioc,
	struct hba_port *port_table)
{
	u16 sz, ioc_status;
	int i, j;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasIOUnitPage0_t *sas_iounit_pg0 = NULL;
	u16 attached_handle;
	u64 attached_sas_addr;
	u8 found=0, port_count=0, port_id;

	sz = offsetof(Mpi2SasIOUnitPage0_t, PhyData) + (ioc->sas_hba.num_phys
	    * sizeof(Mpi2SasIOUnit0PhyData_t));
	sas_iounit_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg0) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return port_count;
	}

	if ((mpt3sas_config_get_sas_iounit_pg0(ioc, &mpi_reply,
	    sas_iounit_pg0, sz)) != 0)
		goto out;
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
		goto out;
	for (i = 0; i < ioc->sas_hba.num_phys; i++) {
		found = 0;
		if ((sas_iounit_pg0->PhyData[i].NegotiatedLinkRate >> 4) <
						 MPI2_SAS_NEG_LINK_RATE_1_5)
			continue;
		attached_handle = le16_to_cpu(sas_iounit_pg0->PhyData[i].
								AttachedDevHandle);
		if (_scsih_get_sas_address(ioc, attached_handle, &attached_sas_addr)
			!= 0) {
                	printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
                	    ioc->name, __FILE__, __LINE__, __func__);
			continue;
        	}

		for (j = 0; j < port_count; j++) {
			port_id = sas_iounit_pg0->PhyData[i].Port;
			if ((port_table[j].port_id == port_id) &&
			    (port_table[j].sas_address == attached_sas_addr)) {
				port_table[j].phy_mask |= (1 << i);
				found = 1;
				break;
			}
		}

		if (found)
			continue;

		port_id = sas_iounit_pg0->PhyData[i].Port;
		port_table[port_count].port_id = port_id;
		port_table[port_count].phy_mask = (1 << i);
		port_table[port_count].sas_address = attached_sas_addr;
		port_count++;
	}
out:
	kfree(sas_iounit_pg0);
	return port_count;
}

enum hba_port_matched_codes {
	NOT_MATCHED = 0,
	MATCHED_WITH_ADDR_AND_PHYMASK,
	MATCHED_WITH_ADDR_SUBPHYMASK_AND_PORT,
	MATCHED_WITH_ADDR_AND_SUBPHYMASK,
	MATCHED_WITH_ADDR,
};

/**
 * _scsih_look_and_get_matched_port_entry - Get matched port entry
 *					    from HBA port table
 * @ioc: per adapter object
 * @port_entry - port entry from temporary port table which needs to be
 *		searched for matched entry in the HBA port table
 * @matched_port_entry - save matched port entry here
 * @count - count of matched entries
 *
 * return type of matched entry found.
 */
static int
_scsih_look_and_get_matched_port_entry(struct MPT3SAS_ADAPTER *ioc,
		struct hba_port *port_entry,
		struct hba_port **matched_port_entry, int *count)
{
	struct hba_port *port_table_entry, *matched_port = NULL;
	enum hba_port_matched_codes matched_code = NOT_MATCHED;
	int lcount=0;
	*matched_port_entry = NULL;

	list_for_each_entry(port_table_entry, &ioc->port_table_list, list) {
		if (!(port_table_entry->flags & HBA_PORT_FLAG_DIRTY_PORT))
			continue;

		if((port_table_entry->sas_address == port_entry->sas_address) &&
		   (port_table_entry->phy_mask == port_entry->phy_mask)) {
			matched_code = MATCHED_WITH_ADDR_AND_PHYMASK;
			matched_port = port_table_entry;
			break;
		}

		if((port_table_entry->sas_address == port_entry->sas_address) &&
		   (port_table_entry->phy_mask & port_entry->phy_mask) &&
		   (port_table_entry->port_id == port_entry->port_id)) {
			matched_code = MATCHED_WITH_ADDR_SUBPHYMASK_AND_PORT;
			matched_port = port_table_entry;
			continue;
		}

		if((port_table_entry->sas_address == port_entry->sas_address) &&
		   (port_table_entry->phy_mask & port_entry->phy_mask)) {
			if (matched_code ==
			    MATCHED_WITH_ADDR_SUBPHYMASK_AND_PORT)
				continue;
			matched_code = MATCHED_WITH_ADDR_AND_SUBPHYMASK;
			matched_port = port_table_entry;
			continue;
		}

		if(port_table_entry->sas_address == port_entry->sas_address) {
			if (matched_code ==
			    MATCHED_WITH_ADDR_SUBPHYMASK_AND_PORT)
				continue;
			if (matched_code == MATCHED_WITH_ADDR_AND_SUBPHYMASK)
				continue;
			matched_code = MATCHED_WITH_ADDR;
			matched_port = port_table_entry;
			lcount++;
		}
	}

	*matched_port_entry = matched_port;
	if (matched_code ==  MATCHED_WITH_ADDR)
		*count = lcount;
	return matched_code;
}

/**
 * _scsih_del_phy_part_of_anther_port - remove phy if it
 *				 is a part of anther port
 *@ioc: per adapter object
 *@port_table: port table after reset
 *@index: port entry index
 *@port_count: number of ports available after host reset
 *@offset: HBA phy bit offset
 *
 */
static void
_scsih_del_phy_part_of_anther_port(struct MPT3SAS_ADAPTER *ioc,
	struct hba_port *port_table,
	int index, u8 port_count, int offset)
{
	struct _sas_node *sas_node = &ioc->sas_hba;
	u32 i, found=0;

	for (i=0; i < port_count; i++)
	{
		if (i == index)
			continue;

		if (port_table[i].phy_mask & (1 << offset)) {
			mpt3sas_transport_del_phy_from_an_existing_port(
			    ioc, sas_node, &sas_node->phy[offset]);
			found = 1;
			break;
		}
	}
	if (!found)
		port_table[index].phy_mask |= (1 << offset);
}

/**
 * _scsih_add_or_del_phys_from_existing_port - add/remove phy to/from
 *					        right port
 *@ioc: per adapter object
 *@hba_port_entry: hba port table entry
 *@port_table: temporary port table
 *@index: port entry index
 *@port_count: number of ports available after host reset
 *
 */
static void
_scsih_add_or_del_phys_from_existing_port(struct MPT3SAS_ADAPTER *ioc,
	struct hba_port *hba_port_entry, struct hba_port *port_table,
	int index, u8 port_count)
{
	u32 phy_mask, offset=0;
	struct _sas_node *sas_node = &ioc->sas_hba;
	phy_mask = hba_port_entry->phy_mask ^ port_table[index].phy_mask;
	for (offset=0; offset < ioc->sas_hba.num_phys; offset++) {
		if (phy_mask & (1 << offset)) {
			if (!(port_table[index].phy_mask & (1 << offset))) {
				_scsih_del_phy_part_of_anther_port(
				 ioc, port_table, index, port_count, offset);
			} else {
				#if defined(MPT_WIDE_PORT_API)
				if (sas_node->phy[offset].phy_belongs_to_port)
					mpt3sas_transport_del_phy_from_an_existing_port(
					 ioc, sas_node, &sas_node->phy[offset]);
				mpt3sas_transport_add_phy_to_an_existing_port(
				  ioc, sas_node, &sas_node->phy[offset],
				  hba_port_entry->sas_address,
				  hba_port_entry);
				#endif
			}
		}
	}
}

/**
 * _scsih_del_dirty_vphy - delete virtual_phy objects marked as dirty.
 * @ioc: per adapter object
 *
 * Returns nothing.
 */
static void
_scsih_del_dirty_vphy(struct MPT3SAS_ADAPTER *ioc)
{
	struct hba_port *port, *port_next;
	struct virtual_phy *vphy, *vphy_next;

	list_for_each_entry_safe(port, port_next, &ioc->port_table_list, list) {
		if (!port->vphys_mask)
			continue;
		list_for_each_entry_safe(vphy, vphy_next, &port->vphys_list, list) {
			if (vphy->flags & MPT_VPHY_FLAG_DIRTY_PHY) {
				drsprintk(ioc, printk(MPT3SAS_INFO_FMT
				    "Deleting vphy %p entry from"
				    " port id: %d\t, Phy_mask 0x%08x\n",
				    ioc->name, vphy, port->port_id,
				    vphy->phy_mask));
				port->vphys_mask &= ~vphy->phy_mask;
				list_del(&vphy->list);
				kfree(vphy);
			}
		}
		if (!port->vphys_mask && !port->sas_address)
			port->flags |= HBA_PORT_FLAG_DIRTY_PORT;
	}
}

/**
 * _scsih_del_dirty_port_entries - delete dirty port entries from port list
 * 				   after host reset
 *@ioc: per adapter object
 *
 */
static void
_scsih_del_dirty_port_entries(struct MPT3SAS_ADAPTER *ioc)
{
	struct hba_port *port, *port_next;
	list_for_each_entry_safe(port, port_next,
					 &ioc->port_table_list, list) {
		if (!(port->flags & HBA_PORT_FLAG_DIRTY_PORT) ||
		    port->flags & HBA_PORT_FLAG_NEW_PORT)
			continue;
		drsprintk(ioc, printk(MPT3SAS_INFO_FMT
		   "Deleting port table entry %p having"
		   " Port id: %d\t, Phy_mask 0x%08x\n", ioc->name,
		   port, port->port_id, port->phy_mask));
		list_del(&port->list);
		kfree(port);
	}
}

/**
 * _scsih_sas_port_refresh - Update HBA port table after host reset
 *@ioc: per adapter object
 */
static void
_scsih_sas_port_refresh(struct MPT3SAS_ADAPTER *ioc)
{
	u8 port_count=0;
	struct hba_port *port_table;
	struct hba_port *port_table_entry;
	struct hba_port *port_entry = NULL;
	int i, j, ret, count=0, lcount=0;
	u64 sas_addr;

	drsprintk(ioc, printk(MPT3SAS_INFO_FMT
	    "updating ports for sas_host(0x%016llx)\n",
	    ioc->name, (unsigned long long)ioc->sas_hba.sas_address));

	port_table = kcalloc(ioc->sas_hba.num_phys,
	    sizeof(struct hba_port), GFP_KERNEL);
	if (!port_table)
		return;

	port_count = _scsih_get_port_table_after_reset(ioc, port_table);
	if (!port_count)
		return;

	drsprintk(ioc, printk(MPT3SAS_INFO_FMT "New Port table\n", ioc->name));
	for (j = 0; j < port_count; j++)
		drsprintk(ioc, printk(MPT3SAS_INFO_FMT
		  "Port: %d\t Phy_mask 0x%08x\t sas_addr(0x%016llx)\n",
		  ioc->name, port_table[j].port_id,
		  port_table[j].phy_mask, port_table[j].sas_address));

	list_for_each_entry(port_table_entry, &ioc->port_table_list, list) {
		port_table_entry->flags |= HBA_PORT_FLAG_DIRTY_PORT;
	}

	drsprintk(ioc, printk(MPT3SAS_INFO_FMT "Old Port table\n", ioc->name));
	port_table_entry = NULL;
	list_for_each_entry(port_table_entry, &ioc->port_table_list, list) {
		drsprintk(ioc, printk(MPT3SAS_INFO_FMT
		  "Port: %d\t Phy_mask 0x%08x\t sas_addr(0x%016llx)\n",
		  ioc->name, port_table_entry->port_id,
		  port_table_entry->phy_mask,
		  port_table_entry->sas_address));
	}

	for ( j = 0; j < port_count; j++) {

		ret = _scsih_look_and_get_matched_port_entry(ioc,
					 &port_table[j], &port_entry, &count);
		if(!port_entry) {
			drsprintk(ioc, printk(MPT3SAS_INFO_FMT
			 "No Matched entry for sas_addr(0x%16llx), Port:%d\n",
			 ioc->name, port_table[j].sas_address,
			 port_table[j].port_id));
			continue;
		}

		switch (ret) {
		case MATCHED_WITH_ADDR_SUBPHYMASK_AND_PORT:
		case MATCHED_WITH_ADDR_AND_SUBPHYMASK:
			_scsih_add_or_del_phys_from_existing_port(ioc,
			     port_entry, port_table, j, port_count);
			break;
		case MATCHED_WITH_ADDR:
			sas_addr = port_table[j].sas_address;
			for (i = 0; i < port_count; i++) {
				if (port_table[i].sas_address == sas_addr)
					lcount++;
			}

			if ((count > 1) || (lcount > 1))
				port_entry = NULL;
			else
				_scsih_add_or_del_phys_from_existing_port(ioc,
				    port_entry, port_table, j, port_count);
		}

		if (!port_entry)
			continue;

		if (port_entry->port_id != port_table[j].port_id)
			port_entry->port_id = port_table[j].port_id;
		port_entry->flags &= ~HBA_PORT_FLAG_DIRTY_PORT;
		port_entry->phy_mask = port_table[j].phy_mask;
	}

	port_table_entry = NULL;

}

/**
 * _scsih_alloc_vphy - allocate virtual_phy object
 * @ioc: per adapter object
 * @port_id: Port ID number
 * @phy_num: HBA Phy number
 *
 * Returns allocated virtual_phy object.
 */
static struct virtual_phy *
_scsih_alloc_vphy(struct MPT3SAS_ADAPTER *ioc, u8 port_id, u8 phy_num)
{
	struct virtual_phy *vphy;
	struct hba_port *port;

	port = mpt3sas_get_port_by_id(ioc, port_id, 0);
	if (!port)
		return NULL;

	vphy = mpt3sas_get_vphy_by_phy(ioc, port, phy_num);
	if (!vphy) {
		vphy = kzalloc(sizeof(struct virtual_phy), GFP_KERNEL);
		if (!vphy) {
			printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			return NULL;
		}

		/*
		 * Enable bit corresponding to HBA phy number on it's
		 * parent hba_port object's vphys_mask field.
		 */
		port->vphys_mask |= (1 << phy_num);
		vphy->phy_mask |= (1 << phy_num);

		INIT_LIST_HEAD(&port->vphys_list);
		list_add_tail(&vphy->list, &port->vphys_list);

		printk(MPT3SAS_INFO_FMT "vphy entry: %p,"
		   " port id: %d, phy:%d is added to port's vphys_list\n",
		   ioc->name, vphy, port->port_id, phy_num);
	}
	return vphy;
}

/**
 * _scsih_sas_host_refresh - refreshing sas host object contents
 * @ioc: per adapter object
 * Context: user
 *
 * During port enable, fw will send topology events for every device. Its
 * possible that the handles may change from the previous setting, so this
 * code keeping handles updating if changed.
 *
 * Return nothing.
 */
static void
_scsih_sas_host_refresh(struct MPT3SAS_ADAPTER *ioc)
{
	u16 sz;
	u16 ioc_status;
	int i;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasIOUnitPage0_t *sas_iounit_pg0 = NULL;
	u16 attached_handle;
	u8 link_rate, port_id;
	struct hba_port *port;
	Mpi2SasPhyPage0_t phy_pg0;

	dtmprintk(ioc, printk(MPT3SAS_INFO_FMT
	    "updating handles for sas_host(0x%016llx)\n",
	    ioc->name, (unsigned long long)ioc->sas_hba.sas_address));

	sz = offsetof(Mpi2SasIOUnitPage0_t, PhyData) + (ioc->sas_hba.num_phys
	    * sizeof(Mpi2SasIOUnit0PhyData_t));
	sas_iounit_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg0) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	if ((mpt3sas_config_get_sas_iounit_pg0(ioc, &mpi_reply,
	    sas_iounit_pg0, sz)) != 0)
		goto out;
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
		goto out;
	for (i = 0; i < ioc->sas_hba.num_phys ; i++) {
		link_rate = sas_iounit_pg0->PhyData[i].NegotiatedLinkRate >> 4;
		if (i == 0)
			ioc->sas_hba.handle = le16_to_cpu(sas_iounit_pg0->
			    PhyData[0].ControllerDevHandle);
		port_id = sas_iounit_pg0->PhyData[i].Port;
		if (!(mpt3sas_get_port_by_id(ioc, port_id, 0))) {
			port = kzalloc(sizeof(struct hba_port),
							GFP_KERNEL);
			if (!port) {
				printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
				    ioc->name, __FILE__, __LINE__, __func__);
				goto out;
			}
			port->port_id = port_id;
			printk(MPT3SAS_INFO_FMT "hba_port entry: %p,"
			   " port: %d is added to hba_port list\n",
			   ioc->name, port, port->port_id);
			if (ioc->shost_recovery)
				port->flags = HBA_PORT_FLAG_NEW_PORT;
			list_add_tail(&port->list, &ioc->port_table_list);
		}

		/*
		 * Check whether current Phy belongs to HBA vSES device or not.
		 */
		if (le32_to_cpu(sas_iounit_pg0->PhyData[i].ControllerPhyDeviceInfo) &
		    MPI2_SAS_DEVICE_INFO_SEP && 
		    (link_rate >=  MPI2_SAS_NEG_LINK_RATE_1_5)) {

			if ((mpt3sas_config_get_phy_pg0(ioc, &mpi_reply, &phy_pg0,
			    i))) {
				printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
				    ioc->name, __FILE__, __LINE__, __func__);
				goto out;
			}

			if (!(le32_to_cpu(phy_pg0.PhyInfo) & MPI2_SAS_PHYINFO_VIRTUAL_PHY))
			        continue;
			
			/*
			 * Allocate a virtual_phy object for vSES device, if
			 * this vSES device is hot added.
			 */
			if (!_scsih_alloc_vphy(ioc, port_id, i))
				goto out;
			ioc->sas_hba.phy[i].hba_vphy = 1;
		}

		ioc->sas_hba.phy[i].handle = ioc->sas_hba.handle;
		attached_handle = le16_to_cpu(sas_iounit_pg0->PhyData[i].
		    AttachedDevHandle);
		if (attached_handle && link_rate < MPI2_SAS_NEG_LINK_RATE_1_5)
			link_rate = MPI2_SAS_NEG_LINK_RATE_1_5;
		ioc->sas_hba.phy[i].port = mpt3sas_get_port_by_id(ioc, port_id, 0);
		mpt3sas_transport_update_links(ioc, ioc->sas_hba.sas_address,
		    attached_handle, i, link_rate, ioc->sas_hba.phy[i].port);
	}
 out:
	kfree(sas_iounit_pg0);
}

/**
 * _scsih_sas_host_add - create sas host object
 * @ioc: per adapter object
 *
 * Creating host side data object, stored in ioc->sas_hba
 *
 * Return nothing.
 */
static void
_scsih_sas_host_add(struct MPT3SAS_ADAPTER *ioc)
{
	int i;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasIOUnitPage0_t *sas_iounit_pg0 = NULL;
	Mpi2SasIOUnitPage1_t *sas_iounit_pg1 = NULL;
	Mpi2SasPhyPage0_t phy_pg0;
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2SasEnclosurePage0_t enclosure_pg0;
	u16 ioc_status;
	u16 sz;
	u8 device_missing_delay;
	u8 num_phys, port_id;
	struct hba_port *port;

	mpt3sas_config_get_number_hba_phys(ioc, &num_phys);
	if (!num_phys) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	ioc->sas_hba.phy = kcalloc(num_phys, sizeof(struct _sas_phy), GFP_KERNEL);
	if (!ioc->sas_hba.phy) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	ioc->sas_hba.num_phys = num_phys;

	/* sas_iounit page 0 */
	sz = offsetof(Mpi2SasIOUnitPage0_t, PhyData) + (ioc->sas_hba.num_phys *
	    sizeof(Mpi2SasIOUnit0PhyData_t));
	sas_iounit_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg0) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}
	if ((mpt3sas_config_get_sas_iounit_pg0(ioc, &mpi_reply,
	    sas_iounit_pg0, sz))) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}

	/* sas_iounit page 1 */
	sz = offsetof(Mpi2SasIOUnitPage1_t, PhyData) + (ioc->sas_hba.num_phys *
	    sizeof(Mpi2SasIOUnit1PhyData_t));
	sas_iounit_pg1 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg1) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	if ((mpt3sas_config_get_sas_iounit_pg1(ioc, &mpi_reply,
	    sas_iounit_pg1, sz))) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}

	ioc->io_missing_delay =
	    sas_iounit_pg1->IODeviceMissingDelay;
	device_missing_delay =
	    sas_iounit_pg1->ReportDeviceMissingDelay;
	if (device_missing_delay & MPI2_SASIOUNIT1_REPORT_MISSING_UNIT_16)
		ioc->device_missing_delay = (device_missing_delay &
		    MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK) * 16;
	else
		ioc->device_missing_delay = device_missing_delay &
		    MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK;

	ioc->sas_hba.parent_dev = &ioc->shost->shost_gendev;

	for (i = 0; i < ioc->sas_hba.num_phys ; i++) {
		if ((mpt3sas_config_get_phy_pg0(ioc, &mpi_reply, &phy_pg0,
		    i))) {
			printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			goto out;
		}
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			goto out;
		}

		if (i == 0)
			ioc->sas_hba.handle = le16_to_cpu(sas_iounit_pg0->
			    PhyData[0].ControllerDevHandle);

		port_id = sas_iounit_pg0->PhyData[i].Port;
		if (!(mpt3sas_get_port_by_id(ioc, port_id, 0))) {
			port = kzalloc(sizeof(struct hba_port), GFP_KERNEL);
			if (!port) {
				printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
				    ioc->name, __FILE__, __LINE__, __func__);
				goto out;
			}
			port->port_id = port_id;
			printk(MPT3SAS_INFO_FMT "hba_port entry: %p,"
			   " port: %d is added to hba_port list\n",
			   ioc->name, port, port->port_id);
			list_add_tail(&port->list, &ioc->port_table_list);
		}

		/*
		 * Check whether current Phy belongs to HBA vSES device or not.
		 */
		if ((le32_to_cpu(phy_pg0.PhyInfo) & MPI2_SAS_PHYINFO_VIRTUAL_PHY) &&
		    (phy_pg0.NegotiatedLinkRate >> 4) >=  MPI2_SAS_NEG_LINK_RATE_1_5) {

			/*
			 * Allocate a virtual_phy object for vSES device.
			 */
			if (!_scsih_alloc_vphy(ioc, port_id, i))
				goto out;
			ioc->sas_hba.phy[i].hba_vphy = 1;
		}

		ioc->sas_hba.phy[i].handle = ioc->sas_hba.handle;
		ioc->sas_hba.phy[i].phy_id = i;
		ioc->sas_hba.phy[i].port = mpt3sas_get_port_by_id(ioc, port_id, 0);
		mpt3sas_transport_add_host_phy(ioc, &ioc->sas_hba.phy[i],
		    phy_pg0, ioc->sas_hba.parent_dev);
	}
	if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, ioc->sas_hba.handle))) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	ioc->sas_hba.enclosure_handle =
	    le16_to_cpu(sas_device_pg0.EnclosureHandle);
	ioc->sas_hba.sas_address = le64_to_cpu(sas_device_pg0.SASAddress);
	printk(MPT3SAS_INFO_FMT "host_add: handle(0x%04x), "
	    "sas_addr(0x%016llx), phys(%d)\n", ioc->name, ioc->sas_hba.handle,
	    (unsigned long long) ioc->sas_hba.sas_address,
	    ioc->sas_hba.num_phys);

	if (ioc->sas_hba.enclosure_handle) {
		if (!(mpt3sas_config_get_enclosure_pg0(ioc, &mpi_reply,
				&enclosure_pg0, MPI2_SAS_ENCLOS_PGAD_FORM_HANDLE,
				ioc->sas_hba.enclosure_handle)))
			ioc->sas_hba.enclosure_logical_id =
				le64_to_cpu(enclosure_pg0.EnclosureLogicalID);
	}

 out:
	kfree(sas_iounit_pg1);
	kfree(sas_iounit_pg0);
}

/**
 * _scsih_expander_add -  creating expander object
 * @ioc: per adapter object
 * @handle: expander handle
 *
 * Creating expander object, stored in ioc->sas_expander_list.
 *
 * Return 0 for success, else error.
 */
static int
_scsih_expander_add(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_node *sas_expander;
	struct _enclosure_node *enclosure_dev;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2ExpanderPage0_t expander_pg0;
	Mpi2ExpanderPage1_t expander_pg1;
	u32 ioc_status;
	u16 parent_handle;
	u64 sas_address, sas_address_parent = 0;
	int i;
	unsigned long flags;
	u8 port_id;
	struct _sas_port *mpt3sas_port = NULL;

	int rc = 0;

	if (!handle)
		return -1;

	if (ioc->shost_recovery || ioc->pci_error_recovery)
		return -1;

	if ((mpt3sas_config_get_expander_pg0(ioc, &mpi_reply, &expander_pg0,
	    MPI2_SAS_EXPAND_PGAD_FORM_HNDL, handle))) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	/* handle out of order topology events */
	parent_handle = le16_to_cpu(expander_pg0.ParentDevHandle);
	if (_scsih_get_sas_address(ioc, parent_handle, &sas_address_parent)
	    != 0) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	port_id = expander_pg0.PhysicalPort;
	if (sas_address_parent != ioc->sas_hba.sas_address) {
		spin_lock_irqsave(&ioc->sas_node_lock, flags);
		sas_expander =
		   mpt3sas_scsih_expander_find_by_sas_address(ioc,
		    sas_address_parent, mpt3sas_get_port_by_id(ioc, port_id, 0));
		spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
		if (!sas_expander) {
			rc = _scsih_expander_add(ioc, parent_handle);
			if (rc != 0)
				return rc;
		}
	}

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	sas_address = le64_to_cpu(expander_pg0.SASAddress);
	sas_expander = mpt3sas_scsih_expander_find_by_sas_address(ioc,
	    			sas_address, mpt3sas_get_port_by_id(ioc, port_id, 0));
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);

	if (sas_expander)
		return 0;

	sas_expander = kzalloc(sizeof(struct _sas_node),
	    GFP_KERNEL);
	if (!sas_expander) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	sas_expander->handle = handle;
	sas_expander->num_phys = expander_pg0.NumPhys;
	sas_expander->sas_address_parent = sas_address_parent;
	sas_expander->sas_address = sas_address;
	sas_expander->port = mpt3sas_get_port_by_id(ioc, port_id, 0);
	if (!sas_expander->port) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -1;
		goto out_fail;
	}

	printk(MPT3SAS_INFO_FMT "expander_add: handle(0x%04x),"
	    " parent(0x%04x), sas_addr(0x%016llx), phys(%d)\n", ioc->name,
	    handle, parent_handle, (unsigned long long)
	    sas_expander->sas_address, sas_expander->num_phys);

	if (!sas_expander->num_phys)
		goto out_fail;
	sas_expander->phy = kcalloc(sas_expander->num_phys,
	    sizeof(struct _sas_phy), GFP_KERNEL);
	if (!sas_expander->phy) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -1;
		goto out_fail;
	}

	INIT_LIST_HEAD(&sas_expander->sas_port_list);
	mpt3sas_port = mpt3sas_transport_port_add(ioc, handle,
	    sas_address_parent, sas_expander->port);
	if (!mpt3sas_port) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -1;
		goto out_fail;
	}
	sas_expander->parent_dev = &mpt3sas_port->rphy->dev;
	sas_expander->rphy = mpt3sas_port->rphy;

	for (i = 0 ; i < sas_expander->num_phys ; i++) {
		if ((mpt3sas_config_get_expander_pg1(ioc, &mpi_reply,
		    &expander_pg1, i, handle))) {
			printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			rc = -1;
			goto out_fail;
		}
		sas_expander->phy[i].handle = handle;
		sas_expander->phy[i].phy_id = i;
		sas_expander->phy[i].port = mpt3sas_get_port_by_id(ioc, port_id, 0);

		if ((mpt3sas_transport_add_expander_phy(ioc,
		    &sas_expander->phy[i], expander_pg1,
		    sas_expander->parent_dev))) {
			printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			rc = -1;
			goto out_fail;
		}
	}

	if (sas_expander->enclosure_handle) {
		enclosure_dev =
			mpt3sas_scsih_enclosure_find_by_handle(ioc,
						sas_expander->enclosure_handle);
		if (enclosure_dev)
			sas_expander->enclosure_logical_id =
			    le64_to_cpu(enclosure_dev->pg0.EnclosureLogicalID);
	}

	_scsih_expander_node_add(ioc, sas_expander);
	 return 0;

 out_fail:

	if (mpt3sas_port)
		mpt3sas_transport_port_remove(ioc,
		    sas_expander->sas_address,
		    sas_address_parent, sas_expander->port);
	kfree(sas_expander);
	return rc;
}

/**
 * mpt3sas_expander_remove - removing expander object
 * @ioc: per adapter object
 * @sas_address: expander sas_address
 *
 * Return nothing.
 */
void
mpt3sas_expander_remove(struct MPT3SAS_ADAPTER *ioc,
	 u64 sas_address, struct hba_port *port)
{
	struct _sas_node *sas_expander;
	unsigned long flags;

	if (ioc->shost_recovery)
		return;

	if (!port)
		return;

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	sas_expander = mpt3sas_scsih_expander_find_by_sas_address(ioc,
							sas_address, port);
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
	if (sas_expander)
		_scsih_expander_node_remove(ioc, sas_expander);
}

/**
 * _scsih_done -  internal SCSI_IO callback handler.
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Callback handler when sending internal generated SCSI_IO.
 * The callback index passed is `ioc->scsih_cb_idx`
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index, u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;

	mpi_reply =  mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (ioc->scsih_cmds.status == MPT3_CMD_NOT_USED)
		return 1;
	if (ioc->scsih_cmds.smid != smid)
		return 1;
	ioc->scsih_cmds.status |= MPT3_CMD_COMPLETE;
	if (mpi_reply) {
		memcpy(ioc->scsih_cmds.reply, mpi_reply,
		    mpi_reply->MsgLength*4);
		ioc->scsih_cmds.status |= MPT3_CMD_REPLY_VALID;
	}
	ioc->scsih_cmds.status &= ~MPT3_CMD_PENDING;
	complete(&ioc->scsih_cmds.done);
	return 1;
}

/**
 * _scsi_send_scsi_io - send internal SCSI_IO to target
 * @ioc: per adapter object
 * @transfer_packet: packet describing the transfer
 * @tr_timeout: Target Reset Timeout
 * @tr_method: Target Reset Method
 * Context: user
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_scsi_send_scsi_io(struct MPT3SAS_ADAPTER *ioc, struct _scsi_io_transfer
	*transfer_packet, u8 tr_timeout, u8 tr_method)
{
	Mpi2SCSIIOReply_t *mpi_reply;
	Mpi2SCSIIORequest_t *mpi_request;
	u16 smid;
	unsigned long timeleft;
	u8 issue_reset = 0;
	int rc;
	void *priv_sense;
	u32 mpi_control;
	void *psge;
	dma_addr_t data_out_dma = 0;
	dma_addr_t data_in_dma = 0;
	size_t data_in_sz = 0;
	size_t data_out_sz = 0;
	u16 handle;
	u8 retry_count = 0, host_reset_count = 0;
	int tm_return_code;

	if (ioc->pci_error_recovery) {
		printk(MPT3SAS_INFO_FMT "%s: pci error recovery in progress!\n",
		    ioc->name, __func__);
		return -EFAULT;
	}

	if (ioc->shost_recovery) {
		printk(MPT3SAS_INFO_FMT "%s: host recovery in progress!\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}

	handle = transfer_packet->handle;
	if (handle == MPT3SAS_INVALID_DEVICE_HANDLE) {
		printk(MPT3SAS_INFO_FMT "%s: no device!\n",
		    __func__, ioc->name);
		return -EFAULT;
	}

	mutex_lock(&ioc->scsih_cmds.mutex);

	if (ioc->scsih_cmds.status != MPT3_CMD_NOT_USED) {
		printk(MPT3SAS_ERR_FMT "%s: scsih_cmd in use\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

 retry_loop:
	if (test_bit(handle, ioc->device_remove_in_progress)) {
		printk(MPT3SAS_INFO_FMT "%s: device removal in progress\n",
		       ioc->name, __func__);
		rc = -EFAULT;
		goto out;
	}

	ioc->scsih_cmds.status = MPT3_CMD_PENDING;

	rc = mpt3sas_wait_for_ioc_to_operational(ioc, 10);
	if (rc)
		goto out;

	/* Use second reserved smid for discovery related IOs */
	smid = ioc->shost->can_queue + INTERNAL_SCSIIO_FOR_DISCOVERY;

	rc = 0;
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->scsih_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2SCSIIORequest_t));
	if (transfer_packet->is_raid)
		mpi_request->Function = MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH;
	else
		mpi_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
	mpi_request->DevHandle = cpu_to_le16(handle);

	switch (transfer_packet->dir) {
	case DMA_TO_DEVICE:
		mpi_control = MPI2_SCSIIO_CONTROL_WRITE;
		data_out_dma = transfer_packet->data_dma;
		data_out_sz = transfer_packet->data_length;
		break;
	case DMA_FROM_DEVICE:
		mpi_control = MPI2_SCSIIO_CONTROL_READ;
		data_in_dma = transfer_packet->data_dma;
		data_in_sz = transfer_packet->data_length;
		break;
	case DMA_BIDIRECTIONAL:
		mpi_control = MPI2_SCSIIO_CONTROL_BIDIRECTIONAL;
		/* TODO - is BIDI support needed ?? */
		BUG();
		break;
	default:
	case DMA_NONE:
		mpi_control = MPI2_SCSIIO_CONTROL_NODATATRANSFER;
		break;
	}

	psge = &mpi_request->SGL;
	ioc->build_sg(ioc, psge, data_out_dma, data_out_sz, data_in_dma,
	    data_in_sz);

	mpi_request->Control = cpu_to_le32(mpi_control |
	    MPI2_SCSIIO_CONTROL_SIMPLEQ);
	mpi_request->DataLength = cpu_to_le32(transfer_packet->data_length);
	mpi_request->MsgFlags = MPI2_SCSIIO_MSGFLAGS_SYSTEM_SENSE_ADDR;
	mpi_request->SenseBufferLength = SCSI_SENSE_BUFFERSIZE;
	mpi_request->SenseBufferLowAddress =
	    mpt3sas_base_get_sense_buffer_dma(ioc, smid);
	priv_sense = mpt3sas_base_get_sense_buffer(ioc, smid);
	mpi_request->SGLOffset0 = offsetof(Mpi2SCSIIORequest_t, SGL) / 4;
	mpi_request->IoFlags = cpu_to_le16(transfer_packet->cdb_length);
	int_to_scsilun(transfer_packet->lun, (struct scsi_lun *)
	    mpi_request->LUN);
	memcpy(mpi_request->CDB.CDB32, transfer_packet->cdb,
	    transfer_packet->cdb_length);
	init_completion(&ioc->scsih_cmds.done);
	if (likely(mpi_request->Function == MPI2_FUNCTION_SCSI_IO_REQUEST))
		ioc->put_smid_scsi_io(ioc, smid, handle);
	else
		ioc->put_smid_default(ioc, smid);
	timeleft = wait_for_completion_timeout(&ioc->scsih_cmds.done,
	    transfer_packet->timeout*HZ);
	if (!(ioc->scsih_cmds.status & MPT3_CMD_COMPLETE)) {
		mpt3sas_check_cmd_timeout(ioc,
		    ioc->scsih_cmds.status, mpi_request,
		    sizeof(Mpi2SCSIIORequest_t)/4, issue_reset);
		goto issue_target_reset;
	}
	if (ioc->scsih_cmds.status & MPT3_CMD_REPLY_VALID) {
		transfer_packet->valid_reply = 1;
		mpi_reply = ioc->scsih_cmds.reply;
		transfer_packet->sense_length =
		   le32_to_cpu(mpi_reply->SenseCount);
		if (transfer_packet->sense_length)
			memcpy(transfer_packet->sense, priv_sense,
			    transfer_packet->sense_length);
		transfer_packet->transfer_length =
		    le32_to_cpu(mpi_reply->TransferCount);
		transfer_packet->ioc_status =
		    le16_to_cpu(mpi_reply->IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		transfer_packet->scsi_state = mpi_reply->SCSIState;
		transfer_packet->scsi_status = mpi_reply->SCSIStatus;
		transfer_packet->log_info =
		    le32_to_cpu(mpi_reply->IOCLogInfo);
	}
	goto out;

 issue_target_reset:
	if (issue_reset) {
		printk(MPT3SAS_INFO_FMT "issue target reset: handle"
		    "(0x%04x)\n", ioc->name, handle);
		tm_return_code =
			mpt3sas_scsih_issue_locked_tm(ioc, handle,
				0xFFFFFFFF, 0xFFFFFFFF, 0,
				MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET, smid,
				tr_timeout, tr_method);

		if (tm_return_code == SUCCESS) {
			printk(MPT3SAS_INFO_FMT "target reset completed: handle"
			    "(0x%04x)\n", ioc->name, handle);
			/* If the command is successfully aborted due to
			 * target reset TM then do up to three retries else
			 * command will be terminated by the host reset TM and
			 * hence retry once.
			 */
			if (((ioc->scsih_cmds.status & MPT3_CMD_COMPLETE) &&
			    retry_count++ < 3) ||
			    ((ioc->scsih_cmds.status & MPT3_CMD_RESET) &&
			    host_reset_count++ == 0)) {
				printk(MPT3SAS_INFO_FMT "issue retry: "
				    "handle (0x%04x)\n", ioc->name, handle);
				goto retry_loop;
			}
		} else
			printk(MPT3SAS_INFO_FMT "target reset didn't complete:"
			    " handle(0x%04x)\n", ioc->name, handle);
		rc = -EFAULT;
	} else
		rc = -EAGAIN;

 out:
	ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
	mutex_unlock(&ioc->scsih_cmds.mutex);
	return rc;
}

/**
 * _scsih_determine_disposition -
 * @ioc: per adapter object
 * @transfer_packet: packet describing the transfer
 * Context: user
 *
 * Determines if an internal generated scsi_io is good data, or
 * whether it needs to be retried or treated as an error.
 *
 * Returns device_responsive_state
 */
static enum device_responsive_state
_scsih_determine_disposition(struct MPT3SAS_ADAPTER *ioc,
	struct _scsi_io_transfer *transfer_packet)
{
	static enum device_responsive_state rc;
	struct sense_info sense_info = {0, 0, 0};
	u8 check_sense = 0;
	char *desc = NULL;

	if (!transfer_packet->valid_reply)
		return DEVICE_READY;

	switch (transfer_packet->ioc_status) {
	case MPI2_IOCSTATUS_BUSY:
	case MPI2_IOCSTATUS_INSUFFICIENT_RESOURCES:
	case MPI2_IOCSTATUS_SCSI_TASK_TERMINATED:
	case MPI2_IOCSTATUS_SCSI_IO_DATA_ERROR:
	case MPI2_IOCSTATUS_SCSI_EXT_TERMINATED:
		rc = DEVICE_RETRY;
		break;
	case MPI2_IOCSTATUS_SCSI_IOC_TERMINATED:
		if (transfer_packet->log_info ==  0x31170000) {
			rc = DEVICE_RETRY;
			break;
		}
		if (transfer_packet->cdb[0] == REPORT_LUNS)
			rc = DEVICE_READY;
		else
			rc = DEVICE_RETRY;
		break;
	case MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN:
	case MPI2_IOCSTATUS_SCSI_RECOVERED_ERROR:
	case MPI2_IOCSTATUS_SUCCESS:
		if (!transfer_packet->scsi_state &&
		    !transfer_packet->scsi_status) {
			rc = DEVICE_READY;
			break;
		}
		if (transfer_packet->scsi_state &
		    MPI2_SCSI_STATE_AUTOSENSE_VALID) {
			rc = DEVICE_ERROR;
			check_sense = 1;
			break;
		}
		if (transfer_packet->scsi_state &
		    (MPI2_SCSI_STATE_AUTOSENSE_FAILED |
		    MPI2_SCSI_STATE_NO_SCSI_STATUS |
		    MPI2_SCSI_STATE_TERMINATED)) {
			rc = DEVICE_RETRY;
			break;
		}
		if (transfer_packet->scsi_status >=
		    MPI2_SCSI_STATUS_BUSY) {
			rc = DEVICE_RETRY;
			break;
		}
		rc = DEVICE_READY;
		break;
	case MPI2_IOCSTATUS_SCSI_PROTOCOL_ERROR:
		if (transfer_packet->scsi_state &
		    MPI2_SCSI_STATE_TERMINATED)
			rc = DEVICE_RETRY;
		else
			rc = DEVICE_ERROR;
		break;
	case MPI2_IOCSTATUS_INSUFFICIENT_POWER:
	default:
		rc = DEVICE_ERROR;
		break;
	}

	if (check_sense) {
		_scsih_normalize_sense(transfer_packet->sense, &sense_info);
		if (sense_info.skey == UNIT_ATTENTION)
			rc = DEVICE_RETRY_UA;
		else if (sense_info.skey == NOT_READY) {
			/* medium isn't present */
			if (sense_info.asc == 0x3a)
				rc = DEVICE_READY;
			/* LOGICAL UNIT NOT READY */
			else if (sense_info.asc == 0x04) {
				if (sense_info.ascq == 0x03 ||
				   sense_info.ascq == 0x0b ||
				   sense_info.ascq == 0x0c) {
					rc = DEVICE_ERROR;
				} else
					rc = DEVICE_START_UNIT;
			}
			/* LOGICAL UNIT HAS NOT SELF-CONFIGURED YET */
			else if (sense_info.asc == 0x3e && !sense_info.ascq)
				rc = DEVICE_START_UNIT;
		} else if (sense_info.skey == ILLEGAL_REQUEST &&
		    transfer_packet->cdb[0] == REPORT_LUNS) {
			rc = DEVICE_READY;
		} else if (sense_info.skey == MEDIUM_ERROR) {

			/* medium is corrupt, lets add the device so
			 * users can collect some info as needed
			 */

			if (sense_info.asc == 0x31)
				rc = DEVICE_READY;
		} else if (sense_info.skey == HARDWARE_ERROR) {
			/* Defect List Error, still add the device */
			if (sense_info.asc == 0x19)
				rc = DEVICE_READY;
		}
	}

	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK) {
		switch (rc) {
		case DEVICE_READY:
			desc = "ready";
			break;
		case DEVICE_RETRY:
			desc = "retry";
			break;
		case DEVICE_RETRY_UA:
			desc = "retry_ua";
			break;
		case DEVICE_START_UNIT:
			desc = "start_unit";
			break;
		case DEVICE_STOP_UNIT:
			desc = "stop_unit";
			break;
		case DEVICE_ERROR:
			desc = "error";
			break;
		}

		printk(MPT3SAS_INFO_FMT "\tioc_status(0x%04x), "
		    "loginfo(0x%08x), scsi_status(0x%02x), "
		    "scsi_state(0x%02x), rc(%s)\n",
		    ioc->name, transfer_packet->ioc_status,
		    transfer_packet->log_info, transfer_packet->scsi_status,
		    transfer_packet->scsi_state, desc);

		if (check_sense)
			printk(MPT3SAS_INFO_FMT "\t[sense_key,asc,ascq]: "
			    "[0x%02x,0x%02x,0x%02x]\n", ioc->name,
			    sense_info.skey, sense_info.asc, sense_info.ascq);
	}
	return rc;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27))
/**
 * _scsih_read_capacity_16 - send READ_CAPACITY_16 to target
 * @ioc: per adapter object
 * @handle: expander handle
 * @data: report luns data payload
 * @data_length: length of data in bytes
 * Context: user
 *
 * Returns device_responsive_state
 */
static enum device_responsive_state
_scsih_read_capacity_16(struct MPT3SAS_ADAPTER *ioc, u16 handle, u32 lun,
	void *data, u32 data_length)
{
	struct _scsi_io_transfer *transfer_packet;
	enum device_responsive_state rc;
	void *parameter_data;
	int return_code;

	parameter_data = NULL;
	transfer_packet = kzalloc(sizeof(struct _scsi_io_transfer), GFP_KERNEL);
	if (!transfer_packet) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_RETRY;
		goto out;
	}

	parameter_data = pci_alloc_consistent(ioc->pdev, data_length,
		&transfer_packet->data_dma);
	if (!parameter_data) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_RETRY;
		goto out;
	}

	rc = DEVICE_READY;
	memset(parameter_data, 0, data_length);
	transfer_packet->handle = handle;
	transfer_packet->lun = lun;
	transfer_packet->dir = DMA_FROM_DEVICE;
	transfer_packet->data_length = data_length;
	transfer_packet->cdb_length = 16;
	transfer_packet->cdb[0] = SERVICE_ACTION_IN;
	transfer_packet->cdb[1] = 0x10;
	transfer_packet->cdb[13] = data_length;
	transfer_packet->timeout = 10;

	return_code = _scsi_send_scsi_io(ioc, transfer_packet, 30, 0);
	switch (return_code) {
	case 0:
		rc = _scsih_determine_disposition(ioc, transfer_packet);
		if (rc == DEVICE_READY)
			memcpy(data, parameter_data, data_length);
		break;
	case -EAGAIN:
		rc = DEVICE_RETRY;
		break;
	case -EFAULT:
	default:
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_ERROR;
		break;
	}

 out:
	if (parameter_data)
		pci_free_consistent(ioc->pdev, data_length, parameter_data,
		    transfer_packet->data_dma);
	kfree(transfer_packet);
	return rc;
}
#endif

/**
 * _scsih_inquiry_vpd_sn - obtain device serial number
 * @ioc: per adapter object
 * @handle: device handle
 * @serial_number: returns pointer to serial_number
 * Context: user
 *
 * Returns device_responsive_state
 */
static enum device_responsive_state
_scsih_inquiry_vpd_sn(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u8 **serial_number)
{
	struct _scsi_io_transfer *transfer_packet;
	enum device_responsive_state rc;
	u8 *inq_data;
	int return_code;
	u32 data_length;
	u8 len;
	struct _pcie_device *pcie_device = NULL;
	u8 tr_timeout = 30;
	u8 tr_method = 0;

	inq_data = NULL;
	transfer_packet = kzalloc(sizeof(struct _scsi_io_transfer), GFP_KERNEL);
	if (!transfer_packet) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_RETRY;
		goto out;
	}

	data_length = 252;
	inq_data = pci_alloc_consistent(ioc->pdev, data_length,
		&transfer_packet->data_dma);
	if (!inq_data) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_RETRY;
		goto out;
	}

	rc = DEVICE_READY;
	memset(inq_data, 0, data_length);
	transfer_packet->handle = handle;
	transfer_packet->dir = DMA_FROM_DEVICE;
	transfer_packet->data_length = data_length;
	transfer_packet->cdb_length = 6;
	transfer_packet->cdb[0] = INQUIRY;
	transfer_packet->cdb[1] = 1;
	transfer_packet->cdb[2] = 0x80;
	transfer_packet->cdb[4] = data_length;
	transfer_packet->timeout = 5;

	pcie_device = mpt3sas_get_pdev_by_handle(ioc, handle);

	if (pcie_device && (!ioc->tm_custom_handling) &&
		(!(mpt3sas_scsih_is_pcie_scsi_device(pcie_device->device_info)))) {
		tr_timeout = pcie_device->reset_timeout;
		tr_method = MPI26_SCSITASKMGMT_MSGFLAGS_PROTOCOL_LVL_RST_PCIE;
	}
	else
		tr_method = MPI2_SCSITASKMGMT_MSGFLAGS_LINK_RESET;

	return_code = _scsi_send_scsi_io(ioc, transfer_packet, tr_timeout, tr_method);
	switch (return_code) {
	case 0:
		rc = _scsih_determine_disposition(ioc, transfer_packet);
		if (rc == DEVICE_READY) {
			len = strlen(&inq_data[4]) + 1;
			*serial_number = kmalloc(len, GFP_KERNEL);
			if (*serial_number)
				strncpy(*serial_number, &inq_data[4], len);
		}
		break;
	case -EAGAIN:
		rc = DEVICE_RETRY;
		break;
	case -EFAULT:
	default:
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_ERROR;
		break;
	}

 out:
	if (pcie_device)
		pcie_device_put(pcie_device);
	if (inq_data)
		pci_free_consistent(ioc->pdev, data_length, inq_data,
		    transfer_packet->data_dma);
	kfree(transfer_packet);
	return rc;
}

/**
 * _scsih_inquiry_vpd_supported_pages - get supported pages
 * @ioc: per adapter object
 * @handle: device handle
 * @data: report luns data payload
 * @data_length: length of data in bytes
 * Context: user
 *
 * Returns device_responsive_state
 */
static enum device_responsive_state
_scsih_inquiry_vpd_supported_pages(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u32 lun, void *data, u32 data_length)
{
	struct _scsi_io_transfer *transfer_packet;
	enum device_responsive_state rc;
	void *inq_data;
	int return_code;

	inq_data = NULL;
	transfer_packet = kzalloc(sizeof(struct _scsi_io_transfer), GFP_KERNEL);
	if (!transfer_packet) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_RETRY;
		goto out;
	}

	inq_data = pci_alloc_consistent(ioc->pdev, data_length,
		&transfer_packet->data_dma);
	if (!inq_data) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_RETRY;
		goto out;
	}

	rc = DEVICE_READY;
	memset(inq_data, 0, data_length);
	transfer_packet->handle = handle;
	transfer_packet->dir = DMA_FROM_DEVICE;
	transfer_packet->data_length = data_length;
	transfer_packet->cdb_length = 6;
	transfer_packet->lun = lun;
	transfer_packet->cdb[0] = INQUIRY;
	transfer_packet->cdb[1] = 1;
	transfer_packet->cdb[4] = data_length;
	transfer_packet->timeout = 5;

	return_code = _scsi_send_scsi_io(ioc, transfer_packet, 30, 0);
	switch (return_code) {
	case 0:
		rc = _scsih_determine_disposition(ioc, transfer_packet);
		if (rc == DEVICE_READY)
			memcpy(data, inq_data, data_length);
		break;
	case -EAGAIN:
		rc = DEVICE_RETRY;
		break;
	case -EFAULT:
	default:
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_ERROR;
		break;
	}

 out:
	if (inq_data)
		pci_free_consistent(ioc->pdev, data_length, inq_data,
		    transfer_packet->data_dma);
	kfree(transfer_packet);
	return rc;
}

/**
 * _scsih_report_luns - send REPORT_LUNS to target
 * @ioc: per adapter object
 * @handle: expander handle
 * @data: report luns data payload
 * @data_length: length of data in bytes
 * @is_pd: is this hidden raid component
 * @tr_timeout: Target Reset Timeout
 * @tr_method: Target Reset Method
 * Context: user
 *
 * Returns device_responsive_state
 */
static enum device_responsive_state
_scsih_report_luns(struct MPT3SAS_ADAPTER *ioc, u16 handle, void *data,
	u32 data_length, u8 is_pd, u8 tr_timeout, u8 tr_method)
{
	struct _scsi_io_transfer *transfer_packet;
	enum device_responsive_state rc;
	void *lun_data;
	int return_code;
	int retries;

	lun_data = NULL;
	transfer_packet = kzalloc(sizeof(struct _scsi_io_transfer), GFP_KERNEL);
	if (!transfer_packet) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_RETRY;
		goto out;
	}

	lun_data = pci_alloc_consistent(ioc->pdev, data_length,
		&transfer_packet->data_dma);
	if (!lun_data) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_RETRY;
		goto out;
	}

	for (retries = 0; retries < 4; retries++) {
		rc = DEVICE_ERROR;
		printk(MPT3SAS_INFO_FMT "REPORT_LUNS: handle(0x%04x), "
		    "retries(%d)\n", ioc->name, handle, retries);
		memset(lun_data, 0, data_length);
		transfer_packet->handle = handle;
		transfer_packet->dir = DMA_FROM_DEVICE;
		transfer_packet->data_length = data_length;
		transfer_packet->cdb_length = 12;
		transfer_packet->cdb[0] = REPORT_LUNS;
		transfer_packet->cdb[6] = (data_length >> 24) & 0xFF;
		transfer_packet->cdb[7] = (data_length >> 16) & 0xFF;
		transfer_packet->cdb[8] = (data_length >>  8) & 0xFF;
		transfer_packet->cdb[9] = data_length & 0xFF;
		transfer_packet->timeout = 5;
		transfer_packet->is_raid = is_pd;

		return_code = _scsi_send_scsi_io(ioc, transfer_packet, tr_timeout, tr_method);
		switch (return_code) {
		case 0:
			rc = _scsih_determine_disposition(ioc, transfer_packet);
			if (rc == DEVICE_READY) {
				memcpy(data, lun_data, data_length);
				goto out;
			} else if (rc == DEVICE_ERROR)
				goto out;
			break;
		case -EAGAIN:
			break;
		case -EFAULT:
		default:
			printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			goto out;
			break;
		}
	}
 out:

	if (rc ==  DEVICE_RETRY) {
		rc = DEVICE_ERROR;
	}
	if (lun_data)
		pci_free_consistent(ioc->pdev, data_length, lun_data,
		    transfer_packet->data_dma);
	kfree(transfer_packet);
	return rc;
}

/**
 * _scsih_issue_logsense - obtain page 0x2f info by issuing Log Sense CMD
 * @ioc: per adapter object
 * @handle: device handle
 * @lun:    lun number
 * Context: User.
 *
 * Returns device_responsive_state
 */
static enum device_responsive_state
_scsih_issue_logsense(struct MPT3SAS_ADAPTER *ioc, u16 handle, u32 lun)
{
	struct _scsi_io_transfer *transfer_packet;
	enum device_responsive_state rc;
	u8 *log_sense_data;
	int return_code;
	u32 data_length;

	log_sense_data = NULL;
	transfer_packet = kzalloc(sizeof(struct _scsi_io_transfer), GFP_KERNEL);
	if (!transfer_packet) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_RETRY;
		goto out;
	}

	/* Allocate 16 byte DMA buffer to recieve the LOG Sense Data*/
	data_length = 16;
	log_sense_data = pci_alloc_consistent(ioc->pdev, data_length,
		&transfer_packet->data_dma);
	if (!log_sense_data) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_RETRY;
		goto out;
	}

	rc = DEVICE_READY;
	memset(log_sense_data, 0, data_length);
	transfer_packet->handle = handle;
	transfer_packet->lun = lun;
	transfer_packet->dir = DMA_FROM_DEVICE;
	transfer_packet->data_length = data_length;
	transfer_packet->cdb_length = 10;
	transfer_packet->cdb[0] = LOG_SENSE;   /* Operation Code: 0x4D */
	transfer_packet->cdb[2] = 0x6F;        /* Page Code: 0x2F & PC: 0x1 */
	transfer_packet->cdb[8] = data_length; /* Allocation Length:16 bytes */
	transfer_packet->timeout = 5;

	return_code = _scsi_send_scsi_io(ioc, transfer_packet, 30, 0);
	switch (return_code) {
	case 0:
		rc = _scsih_determine_disposition(ioc, transfer_packet);
		if (rc == DEVICE_READY) {
			/* Check for ASC field value in the Log Sense Data,
 			 * if this value is 0x5D then issue SEP message to
 			 * turn on PFA LED for this drive */
			if (log_sense_data[8] == 0x5D)
				 _scsih_smart_predicted_fault(ioc, handle, 1);
		}
		break;
	case -EAGAIN:
		rc = DEVICE_RETRY;
		break;
	case -EFAULT:
	default:
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_ERROR;
		break;
	}
out:
	if (transfer_packet) {
		if (log_sense_data)
			pci_free_consistent(ioc->pdev, data_length,
			    log_sense_data, transfer_packet->data_dma);
		kfree(transfer_packet);
	}
	return rc;
}

/**
 * _scsih_start_unit - send START_UNIT to target
 * @ioc: per adapter object
 * @handle: expander handle
 * @lun: lun number
 * @is_pd: is this hidden raid component
 * @tr_timeout: Target Reset Timeout
 * @tr_method: Target Reset Method
 * Context: user
 *
 * Returns device_responsive_state
 */
static enum device_responsive_state
_scsih_start_unit(struct MPT3SAS_ADAPTER *ioc, u16 handle, u32 lun, u8 is_pd,
	u8 tr_timeout, u8 tr_method)
{
	struct _scsi_io_transfer *transfer_packet;
	enum device_responsive_state rc;
	int return_code;

	transfer_packet = kzalloc(sizeof(struct _scsi_io_transfer), GFP_KERNEL);
	if (!transfer_packet) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_RETRY;
		goto out;
	}

	rc = DEVICE_READY;
	transfer_packet->handle = handle;
	transfer_packet->dir = DMA_NONE;
	transfer_packet->lun = lun;
	transfer_packet->cdb_length = 6;
	transfer_packet->cdb[0] = START_STOP;
	transfer_packet->cdb[1] = 1;
	transfer_packet->cdb[4] = 1;
	transfer_packet->timeout = 15;
	transfer_packet->is_raid = is_pd;

	printk(MPT3SAS_INFO_FMT "START_UNIT: handle(0x%04x), "
	    "lun(%d)\n", ioc->name, handle, lun);

	return_code = _scsi_send_scsi_io(ioc, transfer_packet, tr_timeout, tr_method);
	switch (return_code) {
	case 0:
		rc = _scsih_determine_disposition(ioc, transfer_packet);
		break;
	case -EAGAIN:
		rc = DEVICE_RETRY;
		break;
	case -EFAULT:
	default:
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_ERROR;
		break;
	}
 out:
	kfree(transfer_packet);
	return rc;
}

/**
 * mpt3sas_scsih_sata_smart_polling - loop every device attached to host
 * 				      and issue the LOG SENSE command
 * 				      to SATA drives.
 * @ioc - per adapter object
 * Context:
 */
void
mpt3sas_scsih_sata_smart_polling(struct MPT3SAS_ADAPTER *ioc)
{
	struct scsi_device *sdev;
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct _sas_device *sas_device;
	static u32 reset_count;
	u32 reset_occured = 0;

	if (reset_count < ioc->ioc_reset_count) {
		reset_occured = 1;
		reset_count = ioc->ioc_reset_count;
	}

	__shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;

		if (!sas_device_priv_data->sas_target->port)
			continue;

		sas_device = mpt3sas_get_sdev_by_addr(ioc,
				sas_device_priv_data->sas_target->sas_address,
				sas_device_priv_data->sas_target->port);

		/* Don't send Log Sense CMD,
 		 * if device doesn't supports SATA SMART */
		if (!sas_device)
			continue;

		if (!sas_device->supports_sata_smart) {
			sas_device_put(sas_device);
			continue;
		}

		/* Clear the pfa_led_on flag, when polling first time
 		 * after host reset */
		if (reset_occured)
			sas_device->pfa_led_on = 0;
		else if (sas_device->pfa_led_on) {
			sas_device_put(sas_device);
			continue;
		}

		sas_device_put(sas_device);

		/* Don't send Log Sense CMD, if device is part of a volume or RAID
 		 * compenent */
		if ((sas_device_priv_data->sas_target->flags &
					 MPT_TARGET_FLAGS_RAID_COMPONENT) ||
		    (sas_device_priv_data->sas_target->flags &
						 MPT_TARGET_FLAGS_VOLUME))
			continue;

		if (sas_device_priv_data->block)
			continue;
		if (ioc->hba_mpi_version_belonged != MPI2_VERSION)
			_scsih_issue_logsense(ioc, sas_device_priv_data->sas_target->handle, sdev->lun);
	}
}

/**
 * _scsih_test_unit_ready - send TUR to target
 * @ioc: per adapter object
 * @handle: expander handle
 * @lun: lun number
 * @is_pd: is this hidden raid component
 * @tr_timeout: Target Reset timeout value for Pcie devie
 * @tr_method: pcie device Target reset method
 * Context: user
 *
 * Returns device_responsive_state
 */
static enum device_responsive_state
_scsih_test_unit_ready(struct MPT3SAS_ADAPTER *ioc, u16 handle, u32 lun,
	u8 is_pd, u8 tr_timeout, u8 tr_method)
{
	struct _scsi_io_transfer *transfer_packet;
	enum device_responsive_state rc;
	int return_code;
	int sata_init_failure = 0;

	transfer_packet = kzalloc(sizeof(struct _scsi_io_transfer), GFP_KERNEL);
	if (!transfer_packet) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_RETRY;
		goto out;
	}

	rc = DEVICE_READY;
	transfer_packet->handle = handle;
	transfer_packet->dir = DMA_NONE;
	transfer_packet->lun = lun;
	transfer_packet->cdb_length = 6;
	transfer_packet->cdb[0] = TEST_UNIT_READY;
	transfer_packet->timeout = 10;
	transfer_packet->is_raid = is_pd;

 sata_init_retry:
	printk(MPT3SAS_INFO_FMT "TEST_UNIT_READY: handle(0x%04x), "
	    "lun(%d)\n", ioc->name, handle, lun);

	return_code = _scsi_send_scsi_io(ioc, transfer_packet, tr_timeout, tr_method);
	switch (return_code) {
	case 0:
		rc = _scsih_determine_disposition(ioc, transfer_packet);
		if (rc == DEVICE_RETRY &&
		    transfer_packet->log_info == 0x31111000) {
			if (!sata_init_failure++) {
				printk(MPT3SAS_INFO_FMT
				    "SATA Initialization Timeout,"
				    "sending a retry\n", ioc->name);
				rc = DEVICE_READY;
				goto sata_init_retry;
			} else {
				printk(MPT3SAS_ERR_FMT
				    "SATA Initialization Failed\n", ioc->name);
				rc = DEVICE_ERROR;
			}
		}
		break;
	case -EAGAIN:
		rc = DEVICE_RETRY;
		break;
	case -EFAULT:
	default:
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_ERROR;
		break;
	}
 out:
	kfree(transfer_packet);
	return rc;
}

/**
 * _scsih_ata_pass_thru_idd - obtain SATA device Identify Device Data
 * @ioc: per adapter object
 * @handle: device handle
 * @is_ssd_device : is this SATA SSD device
 * @tr_timeout: Target Reset Timeout
 * @tr_method: Target Reset Method
 * Context: user
 *
 * Returns device_responsive_state
 */
static enum device_responsive_state
_scsih_ata_pass_thru_idd(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u8 *is_ssd_device, u8 tr_timeout, u8 tr_method)
{
	struct _scsi_io_transfer *transfer_packet;
	enum device_responsive_state rc;
	u16 *idd_data;
	int return_code;
	u32 data_length;

	idd_data = NULL;
	transfer_packet = kzalloc(sizeof(struct _scsi_io_transfer), GFP_KERNEL);
	if (!transfer_packet) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_RETRY;
		goto out;
	}
	data_length = 512;
	idd_data = pci_alloc_consistent(ioc->pdev, data_length,
		&transfer_packet->data_dma);
	if (!idd_data) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_RETRY;
		goto out;
	}
	rc = DEVICE_READY;
	memset(idd_data, 0, data_length);
	transfer_packet->handle = handle;
	transfer_packet->dir = DMA_FROM_DEVICE;
	transfer_packet->data_length = data_length;
	transfer_packet->cdb_length = 12;
	transfer_packet->cdb[0] = ATA_12;
	transfer_packet->cdb[1] = 0x8;
	transfer_packet->cdb[2] = 0xd;
	transfer_packet->cdb[3] = 0x1;
	transfer_packet->cdb[9] = 0xec;
	transfer_packet->timeout = 5;

	return_code = _scsi_send_scsi_io(ioc, transfer_packet, 30, 0);
	switch (return_code) {
	case 0:
		rc = _scsih_determine_disposition(ioc, transfer_packet);
		if (rc == DEVICE_READY) {
			// Check if nominal media rotation rate is set to 1 i.e. SSD device
			if(le16_to_cpu(idd_data[217]) == 1)
				*is_ssd_device = 1;
		}
		break;
	case -EAGAIN:
		rc = DEVICE_RETRY;
		break;
	case -EFAULT:
	default:
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = DEVICE_ERROR;
		break;
	}

 out:
	if (idd_data) {
		pci_free_consistent(ioc->pdev, data_length, idd_data,
		    transfer_packet->data_dma);
	}
	kfree(transfer_packet);
	return rc;
}

#define MPT3_MAX_LUNS (255)

/**
 * _scsih_wait_for_device_to_become_ready - handle busy devices
 * @ioc: per adapter object
 * @handle: expander handle
 * @retry_count: number of times this event has been retried
 * @is_pd: is this hidden raid component
 * @lun: lun number
 * @tr_timeout: Target Reset Timeout
 * @tr_method: Target Reset Method
 *
 * Some devices spend too much time in busy state, queue event later
 *
 * Return the device_responsive_state.
 */
static enum device_responsive_state
_scsih_wait_for_device_to_become_ready(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u8 retry_count, u8 is_pd, int lun, u8 tr_timeout, u8 tr_method)
{
	enum device_responsive_state rc;

	if (ioc->pci_error_recovery)
		return DEVICE_ERROR;

	if (ioc->shost_recovery)
		return DEVICE_RETRY;

	rc = _scsih_test_unit_ready(ioc, handle, lun, is_pd, tr_timeout, tr_method);
	if (rc == DEVICE_READY || rc == DEVICE_ERROR)
		return rc;
	else if (rc == DEVICE_START_UNIT) {
		rc = _scsih_start_unit(ioc, handle, lun, is_pd, tr_timeout, tr_method);
		if (rc == DEVICE_ERROR)
			return rc;
		rc = _scsih_test_unit_ready(ioc, handle, lun, is_pd, tr_timeout, tr_method);
	}

	if ((rc == DEVICE_RETRY || rc == DEVICE_START_UNIT ||
	    rc == DEVICE_RETRY_UA) && retry_count >= command_retry_count)
		rc = DEVICE_ERROR;
	return rc;
}

/**
 * _scsih_wait_for_target_to_become_ready - handle busy devices
 * @ioc: per adapter object
 * @handle: expander handle
 * @retry_count: number of times this event has been retried
 * @is_pd: is this hidden raid component
 * @tr_timeout: Target Reset timeout value
 * @tr_method: Target Reset method Hot/Protocol level.
 *
 * Some devices spend too much time in busy state, queue event later
 *
 * Return the device_responsive_state.
 */
static enum device_responsive_state
_scsih_wait_for_target_to_become_ready(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	u8 retry_count, u8 is_pd, u8 tr_timeout, u8 tr_method)
{
	enum device_responsive_state rc;
	struct scsi_lun *lun_data;
	u32 length, num_luns;
	u8 *data;
	int lun;
	struct scsi_lun *lunp;

	lun_data = kcalloc(MPT3_MAX_LUNS, sizeof(struct scsi_lun), GFP_KERNEL);
	if (!lun_data) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return DEVICE_RETRY;
	}

	rc = _scsih_report_luns(ioc, handle, lun_data,
	    MPT3_MAX_LUNS * sizeof(struct scsi_lun), is_pd, tr_timeout, tr_method);

	if (rc != DEVICE_READY)
		goto out;

	/* some debug bits*/
	data = (u8 *)lun_data;
	length = ((data[0] << 24) | (data[1] << 16) |
		(data[2] << 8) | (data[3] << 0));

	num_luns = (length / sizeof(struct scsi_lun));
#if 0 /* debug */
	if (num_luns) {
		struct scsi_lun *lunp;
		for (lunp = &lun_data[1]; lunp <= &lun_data[num_luns];
		    lunp++)
			printk(KERN_INFO "%x\n", mpt_scsilun_to_int(lunp));
	}
#endif
	lunp = &lun_data[1];
	lun = (num_luns) ? mpt_scsilun_to_int(&lun_data[1]) : 0;
	rc = _scsih_wait_for_device_to_become_ready(ioc, handle, retry_count,
	    is_pd, lun, tr_timeout, tr_method);

	if (rc == DEVICE_ERROR)
	{
		struct scsi_lun *lunq;
		for (lunq = lunp++; lunq <= &lun_data[num_luns]; lunq++)
		{
			rc = _scsih_wait_for_device_to_become_ready(ioc, handle,
				retry_count, is_pd, mpt_scsilun_to_int(lunq), tr_timeout, tr_method);
			if (rc != DEVICE_ERROR)
				goto out;
		}
	}
out:
	kfree(lun_data);
	return rc;
}

/**
 * _scsih_check_access_status - check access flags
 * @ioc: per adapter object
 * @sas_address: sas address
 * @handle: sas device handle
 * @access_flags: errors returned during discovery of the device
 *
 * Return 0 for success, else failure
 */
static u8
_scsih_check_access_status(struct MPT3SAS_ADAPTER *ioc, u64 sas_address,
	u16 handle, u8 access_status)
{
	u8 rc = 1;
	char *desc = NULL;

	switch (access_status) {
	case MPI2_SAS_DEVICE0_ASTATUS_NO_ERRORS:
	case MPI2_SAS_DEVICE0_ASTATUS_SATA_NEEDS_INITIALIZATION:
		rc = 0;
		break;
	case MPI2_SAS_DEVICE0_ASTATUS_SATA_CAPABILITY_FAILED:
		desc = "sata capability failed";
		break;
	case MPI2_SAS_DEVICE0_ASTATUS_SATA_AFFILIATION_CONFLICT:
		desc = "sata affiliation conflict";
		break;
	case MPI2_SAS_DEVICE0_ASTATUS_ROUTE_NOT_ADDRESSABLE:
		desc = "route not addressable";
		break;
	case MPI2_SAS_DEVICE0_ASTATUS_SMP_ERROR_NOT_ADDRESSABLE:
		desc = "smp error not addressable";
		break;
	case MPI2_SAS_DEVICE0_ASTATUS_DEVICE_BLOCKED:
		desc = "device blocked";
		break;
	case MPI2_SAS_DEVICE0_ASTATUS_SATA_INIT_FAILED:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_UNKNOWN:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_AFFILIATION_CONFLICT:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_DIAG:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_IDENTIFICATION:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_CHECK_POWER:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_PIO_SN:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_MDMA_SN:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_UDMA_SN:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_ZONING_VIOLATION:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_NOT_ADDRESSABLE:
	case MPI2_SAS_DEVICE0_ASTATUS_SIF_MAX:
		desc = "sata initialization failed";
		break;
	default:
		desc = "unknown";
		break;
	}

	if (!rc)
		return 0;

	printk(MPT3SAS_ERR_FMT "discovery errors(%s): sas_address(0x%016llx), "
	    "handle(0x%04x)\n", ioc->name, desc,
	    (unsigned long long)sas_address, handle);
	return rc;
}

/**
 * _scsih_check_device - checking device responsiveness
 * @ioc: per adapter object
 * @parent_sas_address: sas address of parent expander or sas host
 * @handle: attached device handle
 * @phy_numberv: phy number
 * @link_rate: new link rate
 *
 * Returns nothing.
 */
static void
_scsih_check_device(struct MPT3SAS_ADAPTER *ioc,
	u64 parent_sas_address, u16 handle, u8 phy_number, u8 link_rate)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	struct _sas_device *sas_device = NULL;
	struct _enclosure_node *enclosure_dev = NULL;
	u32 ioc_status;
	unsigned long flags;
	u64 sas_address;
	struct scsi_target *starget;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	u32 device_info;
	u8 *serial_number = NULL;
	u8 *original_serial_number = NULL;
	int rc;
	struct hba_port *port;

	if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle)))
		return;

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
		return;

	/* wide port handling ~ we need only handle device once for the phy that
	 * is matched in sas device page zero
	 */
	if (phy_number != sas_device_pg0.PhyNum)
		return;

	/* check if this is end device */
	device_info = le32_to_cpu(sas_device_pg0.DeviceInfo);
	if (!(_scsih_is_sas_end_device(device_info)))
		return;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_address = le64_to_cpu(sas_device_pg0.SASAddress);
	port = mpt3sas_get_port_by_id(ioc, sas_device_pg0.PhysicalPort, 0);
	if (!port)
		goto out_unlock;

	sas_device = __mpt3sas_get_sdev_by_addr(ioc, sas_address, port);

	if (!sas_device)
		goto out_unlock;

	if (unlikely(sas_device->handle != handle)) {
		starget = sas_device->starget;
		sas_target_priv_data = starget->hostdata;
		starget_printk(KERN_INFO, starget, "handle changed from(0x%04x)"
		   " to (0x%04x)!!!\n", sas_device->handle, handle);
		sas_target_priv_data->handle = handle;
		sas_device->handle = handle;
		if ((le16_to_cpu(sas_device_pg0.Flags) & MPI2_SAS_DEVICE0_FLAGS_ENCL_LEVEL_VALID)
		    && (ioc->hba_mpi_version_belonged != MPI2_VERSION)) {
			sas_device->enclosure_level = sas_device_pg0.EnclosureLevel;
			memcpy(sas_device->connector_name,  sas_device_pg0.ConnectorName, 4);
			sas_device->connector_name[4] = '\0';
		}
		else {
			sas_device->enclosure_level = 0;
			sas_device->connector_name[0] = '\0';
		}

		sas_device->enclosure_handle =
					le16_to_cpu(sas_device_pg0.EnclosureHandle);
		sas_device->is_chassis_slot_valid = 0;
		enclosure_dev = mpt3sas_scsih_enclosure_find_by_handle(ioc,
							sas_device->enclosure_handle);
		if (enclosure_dev) {
			sas_device->enclosure_logical_id =
				le64_to_cpu(enclosure_dev->pg0.EnclosureLogicalID);
			if (le16_to_cpu(enclosure_dev->pg0.Flags) &
			    MPI2_SAS_ENCLS0_FLAGS_CHASSIS_SLOT_VALID) {
				sas_device->is_chassis_slot_valid = 1;
				sas_device->chassis_slot =
						enclosure_dev->pg0.ChassisSlot;
			}
		}
	}

	/* check if device is present */
	if (!(le16_to_cpu(sas_device_pg0.Flags) &
	    MPI2_SAS_DEVICE0_FLAGS_DEVICE_PRESENT)) {
		printk(MPT3SAS_ERR_FMT "device is not present "
		    "handle(0x%04x), flags!!!\n", ioc->name, handle);
		goto out_unlock;
	}

	/* check if there were any issues with discovery */
	if (_scsih_check_access_status(ioc, sas_address, handle,
	    sas_device_pg0.AccessStatus))
		goto out_unlock;

	original_serial_number = sas_device->serial_number;
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (issue_scsi_cmd_to_bringup_drive)
		_scsih_ublock_io_device_wait(ioc, sas_address, port);
	else
		_scsih_ublock_io_device_to_running(ioc, sas_address, port);

	/* check to see if serial number still the same, if not, delete
	 * and re-add new device
	 */
	if (!original_serial_number)
		goto out;

	if (_scsih_inquiry_vpd_sn(ioc, handle, &serial_number) == DEVICE_READY
	    && serial_number)  {
		rc = strcmp(original_serial_number, serial_number);
		kfree(serial_number);
		if (!rc) {
		 	if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {

			/* Turn on the PFA LED for a SATA SMART drive on
			 * enclosure, whoes PFA LED is turned on before it went
			 * to blocked state */
				if (sata_smart_polling) {
					if (sas_device->pfa_led_on && sas_device->supports_sata_smart)
						_scsih_send_event_to_turn_on_pfa_led(ioc, handle);
				}
			}
			goto out;
		}
		mpt3sas_device_remove_by_sas_address(ioc,
					    sas_address, port);
		mpt3sas_transport_update_links(ioc, parent_sas_address,
				      handle, phy_number, link_rate, port);
		_scsih_add_device(ioc, handle, 0, 0);
	}
	goto out;
out_unlock:
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
out:
	if (sas_device)
		sas_device_put(sas_device);
}

/**
 * _scsih_add_device -  creating sas device object
 * @ioc: per adapter object
 * @handle: sas device handle
 * @retry_count: number of times this event has been retried
 * @is_pd: is this hidden raid component
 *
 * Creating end device object, stored in ioc->sas_device_list.
 *
 * Return 1 means queue the event later, 0 means complete the event
 */
static int
_scsih_add_device(struct MPT3SAS_ADAPTER *ioc, u16 handle, u8 retry_count,
	u8 is_pd)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	struct _sas_device *sas_device;
	struct _enclosure_node *enclosure_dev = NULL;
	u32 ioc_status;
	u64 sas_address;
	u32 device_info;
	enum device_responsive_state rc;
	u8   connector_name[5], port_id;


	if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle))) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 0;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 0;
	}

	/* check if this is end device */
	device_info = le32_to_cpu(sas_device_pg0.DeviceInfo);
	if (!(_scsih_is_sas_end_device(device_info)))
		return 0;
	set_bit(handle, ioc->pend_os_device_add);
	sas_address = le64_to_cpu(sas_device_pg0.SASAddress);

	/* check if device is present */
	if (!(le16_to_cpu(sas_device_pg0.Flags) &
	    MPI2_SAS_DEVICE0_FLAGS_DEVICE_PRESENT)) {
		printk(MPT3SAS_ERR_FMT "device is not present "
		    "handle(0x04%x)!!!\n", ioc->name, handle);
		return 0;
	}

	/* check if there were any issues with discovery */
	if (_scsih_check_access_status(ioc, sas_address, handle,
	    sas_device_pg0.AccessStatus))
		return 0;
	port_id = sas_device_pg0.PhysicalPort;
	sas_device = mpt3sas_get_sdev_by_addr(ioc,
		     sas_address,
		     mpt3sas_get_port_by_id(ioc, port_id, 0));

	if (sas_device) {
		clear_bit(handle, ioc->pend_os_device_add);
		sas_device_put(sas_device);
		return 0;
	}

	if (le16_to_cpu(sas_device_pg0.EnclosureHandle)) {
		enclosure_dev =
			mpt3sas_scsih_enclosure_find_by_handle(ioc,
		    le16_to_cpu(sas_device_pg0.EnclosureHandle));
		if(enclosure_dev == NULL)
			printk(MPT3SAS_INFO_FMT "Enclosure handle(0x%04x)"
			       "doesn't match with enclosure device!\n",
			       ioc->name, le16_to_cpu(sas_device_pg0.EnclosureHandle));
	}

	/*
	 * Wait for device that is becoming ready
	 * queue request later if device is busy.
	 */
	if ((!ioc->wait_for_discovery_to_complete) &&
		(issue_scsi_cmd_to_bringup_drive)) {
		printk(MPT3SAS_INFO_FMT "detecting: handle(0x%04x), "
		    "sas_address(0x%016llx), phy(%d)\n", ioc->name, handle,
		    (unsigned long long)sas_address, sas_device_pg0.PhyNum);
		rc = _scsih_wait_for_target_to_become_ready(ioc, handle,
		    retry_count, is_pd, 30, 0);
		if (rc != DEVICE_READY) {
			if (le16_to_cpu(sas_device_pg0.EnclosureHandle) != 0)
				dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: "
				    "device not ready: slot(%d) \n", ioc->name,
				    __func__,
				    le16_to_cpu(sas_device_pg0.Slot)));
			if ((le16_to_cpu(sas_device_pg0.Flags) &
			    MPI2_SAS_DEVICE0_FLAGS_ENCL_LEVEL_VALID) &&
			    (ioc->hba_mpi_version_belonged != MPI2_VERSION)) {
				memcpy(connector_name,
					sas_device_pg0.ConnectorName, 4);
				connector_name[4] = '\0';
				dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: "
				    "device not ready: "
				    "enclosure level(0x%04x), "
				    "connector name( %s)\n", ioc->name,
				    __func__, sas_device_pg0.EnclosureLevel,
				    connector_name));
			}

			if ((enclosure_dev) && (le16_to_cpu(enclosure_dev->pg0.Flags) &
			    MPI2_SAS_ENCLS0_FLAGS_CHASSIS_SLOT_VALID))
				printk(MPT3SAS_INFO_FMT "chassis slot(0x%04x)\n",
				       ioc->name, enclosure_dev->pg0.ChassisSlot);

			if (rc == DEVICE_RETRY || rc == DEVICE_START_UNIT ||
			    rc == DEVICE_STOP_UNIT || rc == DEVICE_RETRY_UA)
				return 1;
			else if (rc == DEVICE_ERROR)
				return 0;
		}
	}
	sas_device = kzalloc(sizeof(struct _sas_device),
	    GFP_KERNEL);
	if (!sas_device) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 0;
	}

	kref_init(&sas_device->refcount);
	sas_device->handle = handle;
	if (_scsih_get_sas_address(ioc,
	    le16_to_cpu(sas_device_pg0.ParentDevHandle),
	    &sas_device->sas_address_parent) != 0)
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n", ioc->name,
		    __FILE__, __LINE__, __func__);
	sas_device->enclosure_handle =
	    le16_to_cpu(sas_device_pg0.EnclosureHandle);
	if (sas_device->enclosure_handle != 0)
		sas_device->slot = le16_to_cpu(sas_device_pg0.Slot);
	sas_device->device_info = device_info;
	sas_device->sas_address = sas_address;
	sas_device->port = mpt3sas_get_port_by_id(ioc, port_id, 0);
	if (!sas_device->port) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	sas_device->phy = sas_device_pg0.PhyNum;
	if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
		sas_device->fast_path = (le16_to_cpu(sas_device_pg0.Flags) &
		    MPI25_SAS_DEVICE0_FLAGS_FAST_PATH_CAPABLE) ? 1 : 0;

		sas_device->supports_sata_smart =
		    (le16_to_cpu(sas_device_pg0.Flags) &
		        MPI2_SAS_DEVICE0_FLAGS_SATA_SMART_SUPPORTED);
	}

	if ((le16_to_cpu(sas_device_pg0.Flags) &
	    MPI2_SAS_DEVICE0_FLAGS_ENCL_LEVEL_VALID)
	    && (ioc->hba_mpi_version_belonged != MPI2_VERSION)) {
		sas_device->enclosure_level = sas_device_pg0.EnclosureLevel;
		memcpy(sas_device->connector_name,
			sas_device_pg0.ConnectorName, 4);
		sas_device->connector_name[4] = '\0';
	} else {
		sas_device->enclosure_level = 0;
		sas_device->connector_name[0] = '\0';
	}
	/* get enclosure_logical_id & chassis_slot*/
	sas_device->is_chassis_slot_valid = 0;
	if (enclosure_dev) {
		sas_device->enclosure_logical_id =
		    le64_to_cpu(enclosure_dev->pg0.EnclosureLogicalID);
		if (le16_to_cpu(enclosure_dev->pg0.Flags) &
		    MPI2_SAS_ENCLS0_FLAGS_CHASSIS_SLOT_VALID) {
			sas_device->is_chassis_slot_valid = 1;
			sas_device->chassis_slot =
					enclosure_dev->pg0.ChassisSlot;
		}
	}
	/* get device name */
	sas_device->device_name = le64_to_cpu(sas_device_pg0.DeviceName);

	if (ioc->wait_for_discovery_to_complete)
		_scsih_sas_device_init_add(ioc, sas_device);
	else
		_scsih_sas_device_add(ioc, sas_device);

out:
	sas_device_put(sas_device);
	return 0;
}

/**
 * _scsih_remove_device -  removing sas device object
 * @ioc: per adapter object
 * @sas_device_delete: the sas_device object
 *
 * Return nothing.
 */
static void
_scsih_remove_device(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_device *sas_device)
{
	struct MPT3SAS_TARGET *sas_target_priv_data;

	if (((ioc->pdev->subsystem_vendor == PCI_VENDOR_ID_IBM)
		|| ((ioc->hba_mpi_version_belonged != MPI2_VERSION) && sata_smart_polling))
		&& (sas_device->pfa_led_on)) {
		_scsih_turn_off_pfa_led(ioc, sas_device);
		sas_device->pfa_led_on = 0;
	}

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: enter: "
	    "handle(0x%04x), sas_addr(0x%016llx)\n", ioc->name, __func__,
	    sas_device->handle, (unsigned long long)sas_device->sas_address));
	dewtprintk(ioc,	_scsih_display_enclosure_chassis_info(ioc, sas_device,
	    NULL, NULL));

	if (sas_device->starget && sas_device->starget->hostdata) {
		sas_target_priv_data = sas_device->starget->hostdata;
		sas_target_priv_data->deleted = 1;
		_scsih_ublock_io_device(ioc, sas_device->sas_address,
						sas_device->port);
		sas_target_priv_data->handle =
		     MPT3SAS_INVALID_DEVICE_HANDLE;
	}

	if (!ioc->hide_drives)
		mpt3sas_transport_port_remove(ioc,
		    sas_device->sas_address,
		    sas_device->sas_address_parent,
		    sas_device->port);

	printk(MPT3SAS_INFO_FMT "removing handle(0x%04x), sas_addr"
	    "(0x%016llx)\n", ioc->name, sas_device->handle,
	    (unsigned long long) sas_device->sas_address);
	_scsih_display_enclosure_chassis_info(ioc, sas_device, NULL, NULL);

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: exit: "
	    "handle(0x%04x), sas_addr(0x%016llx)\n", ioc->name, __func__,
	    sas_device->handle, (unsigned long long)
	    sas_device->sas_address));
	dewtprintk(ioc,	_scsih_display_enclosure_chassis_info(ioc, sas_device,
	    NULL, NULL));

	kfree(sas_device->serial_number);


}

/**
 * _scsih_sas_topology_change_event_debug - debug for topology event
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 */
static void
_scsih_sas_topology_change_event_debug(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataSasTopologyChangeList_t *event_data)
{
	int i;
	u16 handle;
	u16 reason_code;
	u8 phy_number;
	char *status_str = NULL;
	u8 link_rate, prev_link_rate;

	switch (event_data->ExpStatus) {
	case MPI2_EVENT_SAS_TOPO_ES_ADDED:
		status_str = "add";
		break;
	case MPI2_EVENT_SAS_TOPO_ES_NOT_RESPONDING:
		status_str = "remove";
		break;
	case MPI2_EVENT_SAS_TOPO_ES_RESPONDING:
	case 0:
		status_str =  "responding";
		break;
	case MPI2_EVENT_SAS_TOPO_ES_DELAY_NOT_RESPONDING:
		status_str = "remove delay";
		break;
	default:
		status_str = "unknown status";
		break;
	}
	printk(MPT3SAS_INFO_FMT "sas topology change: (%s)\n",
	    ioc->name, status_str);
	printk(KERN_INFO "\thandle(0x%04x), enclosure_handle(0x%04x) "
	    "start_phy(%02d), count(%d)\n",
	    le16_to_cpu(event_data->ExpanderDevHandle),
	    le16_to_cpu(event_data->EnclosureHandle),
	    event_data->StartPhyNum, event_data->NumEntries);
	for (i = 0; i < event_data->NumEntries; i++) {
		handle = le16_to_cpu(event_data->PHY[i].AttachedDevHandle);
		if (!handle)
			continue;
		phy_number = event_data->StartPhyNum + i;
		reason_code = event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_RC_MASK;
		switch (reason_code) {
		case MPI2_EVENT_SAS_TOPO_RC_TARG_ADDED:
			status_str = "target add";
			break;
		case MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING:
			status_str = "target remove";
			break;
		case MPI2_EVENT_SAS_TOPO_RC_DELAY_NOT_RESPONDING:
			status_str = "delay target remove";
			break;
		case MPI2_EVENT_SAS_TOPO_RC_PHY_CHANGED:
			status_str = "link rate change";
			break;
		case MPI2_EVENT_SAS_TOPO_RC_NO_CHANGE:
			status_str = "target responding";
			break;
		default:
			status_str = "unknown";
			break;
		}
		link_rate = event_data->PHY[i].LinkRate >> 4;
		prev_link_rate = event_data->PHY[i].LinkRate & 0xF;
		printk(KERN_INFO "\tphy(%02d), attached_handle(0x%04x): %s:"
		    " link rate: new(0x%02x), old(0x%02x)\n", phy_number,
		    handle, status_str, link_rate, prev_link_rate);

	}
}

/**
 * _scsih_sas_topology_change_event - handle topology changes
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 */
static int
_scsih_sas_topology_change_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	int i;
	u16 parent_handle, handle;
	u16 reason_code;
	u8 phy_number, max_phys;
	struct _sas_node *sas_expander;
	struct _sas_device *sas_device;
	u64 sas_address;
	unsigned long flags;
	u8 link_rate, prev_link_rate;
	int rc;
	int requeue_event;
	struct hba_port *port;
	Mpi2EventDataSasTopologyChangeList_t *event_data = fw_event->event_data;

	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_sas_topology_change_event_debug(ioc, event_data);

	if (ioc->shost_recovery || ioc->remove_host || ioc->pci_error_recovery)
		return 0;

	if (!ioc->sas_hba.num_phys)
		_scsih_sas_host_add(ioc);
	else
		_scsih_sas_host_refresh(ioc);

	if (fw_event->ignore) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "ignoring expander "
		    "event\n", ioc->name));
		return 0;
	}

	parent_handle = le16_to_cpu(event_data->ExpanderDevHandle);
	port = mpt3sas_get_port_by_id(ioc, event_data->PhysicalPort, 0);

	/* handle expander add */
	if (event_data->ExpStatus == MPI2_EVENT_SAS_TOPO_ES_ADDED)
		if (_scsih_expander_add(ioc, parent_handle) != 0)
			return 0;

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	sas_expander = mpt3sas_scsih_expander_find_by_handle(ioc,
	    						parent_handle);
	if (sas_expander) {
		sas_address = sas_expander->sas_address;
		max_phys = sas_expander->num_phys;
		port = sas_expander->port;
	} else if (parent_handle < ioc->sas_hba.num_phys) {
		sas_address = ioc->sas_hba.sas_address;
		max_phys = ioc->sas_hba.num_phys;
	} else {
		spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);

	/* handle siblings events */
	for (i = 0, requeue_event = 0; i < event_data->NumEntries; i++) {
		if (fw_event->ignore) {
			dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "ignoring "
			    "expander event\n", ioc->name));
			return 0;
		}
		if (ioc->remove_host || ioc->pci_error_recovery)
			return 0;
		phy_number = event_data->StartPhyNum + i;
		if (phy_number >= max_phys)
			continue;
		reason_code = event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_RC_MASK;
		if ((event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_PHYSTATUS_VACANT) && (reason_code !=
		    MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING))
				continue;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
		if (fw_event->delayed_work_active && (reason_code ==
		    MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING)) {
			dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "ignoring "
			    "Targ not responding event phy in re-queued event "
			    "processing\n", ioc->name));
			continue;
		}
#endif
		handle = le16_to_cpu(event_data->PHY[i].AttachedDevHandle);
		if (!handle)
			continue;
		link_rate = event_data->PHY[i].LinkRate >> 4;
		prev_link_rate = event_data->PHY[i].LinkRate & 0xF;
		switch (reason_code) {
		case MPI2_EVENT_SAS_TOPO_RC_PHY_CHANGED:

			if (ioc->shost_recovery)
				break;

			if (link_rate == prev_link_rate)
				break;

			mpt3sas_transport_update_links(ioc, sas_address,
			    handle, phy_number, link_rate, port);

			if (link_rate < MPI2_SAS_NEG_LINK_RATE_1_5)
				break;

			_scsih_check_device(ioc, sas_address, handle,
			    phy_number, link_rate);

			/* This code after this point handles the test case
			 * where a device has been added, however its returning
			 * BUSY for sometime.  Then before the Device Missing
			 * Delay expires and the device becomes READY, the
			 * device is removed and added back.
			 */
			spin_lock_irqsave(&ioc->sas_device_lock, flags);
			sas_device = __mpt3sas_get_sdev_by_handle(ioc,
			    handle);
			spin_unlock_irqrestore(&ioc->sas_device_lock,
			    flags);

			if (sas_device) {
				sas_device_put(sas_device);
				break;
			}

			if (!test_bit(handle, ioc->pend_os_device_add))
				break;

			dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
			    "handle(0x%04x) device not found: convert "
			    "event to a device add\n", ioc->name,
			    handle));
			event_data->PHY[i].PhyStatus &= 0xF0;
			event_data->PHY[i].PhyStatus |=
			    MPI2_EVENT_SAS_TOPO_RC_TARG_ADDED;
			
			/* fall through */

		case MPI2_EVENT_SAS_TOPO_RC_TARG_ADDED:

			if (ioc->shost_recovery)
				break;

			mpt3sas_transport_update_links(ioc, sas_address,
			    handle, phy_number, link_rate, port);

			if (link_rate < MPI2_SAS_NEG_LINK_RATE_1_5)
				break;

			rc = _scsih_add_device(ioc, handle,
			    fw_event->retries[i], 0);
			if (rc) {/* retry due to busy device */
				fw_event->retries[i]++;
				requeue_event = 1;
			} else {/* mark entry vacant */
				event_data->PHY[i].PhyStatus |=
			    MPI2_EVENT_SAS_TOPO_PHYSTATUS_VACANT;
			}
			break;
		case MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING:

			_scsih_device_remove_by_handle(ioc, handle);
			break;
		}
	}

	/* handle expander removal */
	if (event_data->ExpStatus == MPI2_EVENT_SAS_TOPO_ES_NOT_RESPONDING &&
	    sas_expander)
		mpt3sas_expander_remove(ioc, sas_address, port);

	return requeue_event;
}

/**
 * _scsih_sas_device_status_change_event_debug - debug for device event
 * @event_data: event data payload
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_device_status_change_event_debug(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataSasDeviceStatusChange_t *event_data)
{
	char *reason_str = NULL;

	switch (event_data->ReasonCode) {
	case MPI2_EVENT_SAS_DEV_STAT_RC_SMART_DATA:
		reason_str = "smart data";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_UNSUPPORTED:
		reason_str = "unsupported device discovered";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET:
		reason_str = "internal device reset";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_TASK_ABORT_INTERNAL:
		reason_str = "internal task abort";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_ABORT_TASK_SET_INTERNAL:
		reason_str = "internal task abort set";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_CLEAR_TASK_SET_INTERNAL:
		reason_str = "internal clear task set";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_QUERY_TASK_INTERNAL:
		reason_str = "internal query task";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_SATA_INIT_FAILURE:
		reason_str = "sata init failure";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_CMP_INTERNAL_DEV_RESET:
		reason_str = "internal device reset complete";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_CMP_TASK_ABORT_INTERNAL:
		reason_str = "internal task abort complete";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_ASYNC_NOTIFICATION:
		reason_str = "internal async notification";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_EXPANDER_REDUCED_FUNCTIONALITY:
		reason_str = "expander reduced functionality";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_CMP_EXPANDER_REDUCED_FUNCTIONALITY:
		reason_str = "expander reduced functionality complete";
		break;
	default:
		reason_str = "unknown reason";
		break;
	}
	printk(MPT3SAS_INFO_FMT "device status change: (%s)\n"
	    "\thandle(0x%04x), sas address(0x%016llx), tag(%d)",
	    ioc->name, reason_str, le16_to_cpu(event_data->DevHandle),
	    (unsigned long long)le64_to_cpu(event_data->SASAddress),
	    le16_to_cpu(event_data->TaskTag));
	if (event_data->ReasonCode == MPI2_EVENT_SAS_DEV_STAT_RC_SMART_DATA)
		printk(MPT3SAS_INFO_FMT ", ASC(0x%x), ASCQ(0x%x)\n", ioc->name,
		    event_data->ASC, event_data->ASCQ);
	printk(KERN_INFO "\n");
}


/**
 * _scsih_sas_device_status_change_event - handle device status change
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_device_status_change_event(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataSasDeviceStatusChange_t *event_data)
{
	struct MPT3SAS_TARGET *target_priv_data;
	struct _sas_device *sas_device;
	u64 sas_address;
	unsigned long flags;

	/* In MPI Revision K (0xC), the internal device reset complete was
	 * implemented, so avoid setting tm_busy flag for older firmware.
	 */
	if ((ioc->facts.HeaderVersion >> 8) < 0xC)
		return;

	if (event_data->ReasonCode !=
	    MPI2_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET &&
	   event_data->ReasonCode !=
	    MPI2_EVENT_SAS_DEV_STAT_RC_CMP_INTERNAL_DEV_RESET)
		return;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_address = le64_to_cpu(event_data->SASAddress);
	sas_device = __mpt3sas_get_sdev_by_addr(ioc, sas_address,
			mpt3sas_get_port_by_id(ioc, event_data->PhysicalPort, 0));

	if (!sas_device || !sas_device->starget)
		goto out;

	target_priv_data = sas_device->starget->hostdata;
	if (!target_priv_data)
		goto out;

	if (event_data->ReasonCode ==
	    MPI2_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET)
		target_priv_data->tm_busy = 1;
	else
		target_priv_data->tm_busy = 0;

	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)	
		printk(MPT3SAS_INFO_FMT "%s tm_busy flag for handle(0x%04x)\n",
		    ioc->name,
		    (target_priv_data->tm_busy == 1) ? "Enable" : "Disable",
		    target_priv_data->handle);
out:
	if (sas_device)
		sas_device_put(sas_device);

	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
}


/**
 * _scsih_check_pcie_access_status - check access flags
 * @ioc: per adapter object
 * @wwid: wwid
 * @handle: sas device handle
 * @access_flags: errors returned during discovery of the device
 *
 * Return 0 for success, else failure
 */
static u8
_scsih_check_pcie_access_status(struct MPT3SAS_ADAPTER *ioc, u64 wwid,
	u16 handle, u8 access_status)
{
	u8 rc = 1;
	char *desc = NULL;

	switch (access_status) {
	case MPI26_PCIEDEV0_ASTATUS_NO_ERRORS:
	case MPI26_PCIEDEV0_ASTATUS_NEEDS_INITIALIZATION:
		rc = 0;
		break;
	case MPI26_PCIEDEV0_ASTATUS_CAPABILITY_FAILED:
		desc = "PCIe device capability failed";
		break;
	case MPI26_PCIEDEV0_ASTATUS_DEVICE_BLOCKED:
		desc = "PCIe device blocked";
		printk(MPT3SAS_INFO_FMT "Device with Access Status (%s): wwid(0x%016llx), "
			"handle(0x%04x)\n ll be added to the internal list",
			ioc->name, desc, (unsigned long long)wwid, handle);
		rc = 0;
		break;
	case MPI26_PCIEDEV0_ASTATUS_MEMORY_SPACE_ACCESS_FAILED:
		desc = "PCIe device mem space access failed";
		break;
	case MPI26_PCIEDEV0_ASTATUS_UNSUPPORTED_DEVICE:
		desc = "PCIe device unsupported";
		break;
	case MPI26_PCIEDEV0_ASTATUS_MSIX_REQUIRED:
		desc = "PCIe device MSIx Required";
		break;
	case MPI26_PCIEDEV0_ASTATUS_INIT_FAIL_MAX:
		desc = "PCIe device init fail max";
		break;
	case MPI26_PCIEDEV0_ASTATUS_UNKNOWN:
		desc = "PCIe device status unknown";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_READY_TIMEOUT:
		desc = "nvme ready timeout";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_DEVCFG_UNSUPPORTED:
		desc = "nvme device configuration unsupported";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_IDENTIFY_FAILED:
		desc = "nvme identify failed";
		break;
    case MPI26_PCIEDEV0_ASTATUS_NVME_QCONFIG_FAILED:
		desc = "nvme qconfig failed";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_QCREATION_FAILED:
		desc = "nvme qcreation failed";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_EVENTCFG_FAILED:
		desc = "nvme eventcfg failed";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_GET_FEATURE_STAT_FAILED:
		desc = "nvme get feature stat failed";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_IDLE_TIMEOUT:
		desc = "nvme idle timeout";
		break;
	case MPI26_PCIEDEV0_ASTATUS_NVME_FAILURE_STATUS:
		desc = "nvme failure status";
		break;
	default:
		printk(MPT3SAS_ERR_FMT "NVMe discovery error(0x%02x): wwid(0x%016llx),"
			" handle(0x%04x)\n", ioc->name, access_status,
			(unsigned long long)wwid, handle);
		return rc;
	}

	if (!rc)
		return rc;

	printk(MPT3SAS_ERR_FMT "NVMe discovery error(%s): wwid(0x%016llx), "
		"handle(0x%04x)\n", ioc->name, desc, (unsigned long long)wwid, handle);
	return rc;
}

/**
 * _scsih_pcie_device_remove_from_sml -  removing pcie device
 * from SML and free up associated memory
 * @ioc: per adapter object
 * @pcie_device: the pcie_device object
 *
 * Return nothing.
 */
static void
_scsih_pcie_device_remove_from_sml(struct MPT3SAS_ADAPTER *ioc,
	struct _pcie_device *pcie_device)
{
	struct MPT3SAS_TARGET *sas_target_priv_data;

#if 0
	if (((ioc->pdev->subsystem_vendor == PCI_VENDOR_ID_IBM) ||
	     (sata_smart_polling)) && (pcie_device->pfa_led_on)) {
		_scsih_turn_off_pfa_led(ioc, pcie_device);
		pcie_device->pfa_led_on = 0;
	}
#endif

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: enter: "
	    "handle(0x%04x), wwid(0x%016llx)\n", ioc->name, __func__,
	    pcie_device->handle, (unsigned long long)
	    pcie_device->wwid));
	if(pcie_device->enclosure_handle != 0)
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: enter: "
			"enclosure logical id(0x%016llx), slot(%d) \n",
			ioc->name, __func__,
			(unsigned long long)pcie_device->enclosure_logical_id,
			pcie_device->slot));
	if(pcie_device->connector_name[0] != '\0')
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: enter: "
			"enclosure level(0x%04x), connector name( %s)\n",
			ioc->name, __func__,
			pcie_device->enclosure_level, pcie_device->connector_name));

	if (pcie_device->starget && pcie_device->starget->hostdata) {
		sas_target_priv_data = pcie_device->starget->hostdata;
		sas_target_priv_data->deleted = 1;
		_scsih_ublock_io_device(ioc, pcie_device->wwid, NULL);
		sas_target_priv_data->handle = MPT3SAS_INVALID_DEVICE_HANDLE;
	}

	printk(MPT3SAS_INFO_FMT "removing handle(0x%04x), wwid"
		"(0x%016llx)\n", ioc->name, pcie_device->handle,
		(unsigned long long) pcie_device->wwid);
	if(pcie_device->enclosure_handle != 0)
		printk(MPT3SAS_INFO_FMT "removing :"
			"enclosure logical id(0x%016llx), slot(%d) \n",
			ioc->name, (unsigned long long)pcie_device->enclosure_logical_id,
			pcie_device->slot);
	if(pcie_device->connector_name[0] != '\0')
		printk(MPT3SAS_INFO_FMT "removing :"
			"enclosure level(0x%04x), connector name( %s)\n",
			ioc->name, pcie_device->enclosure_level,
			pcie_device->connector_name);

	if (pcie_device->starget &&
		(pcie_device->access_status != MPI26_PCIEDEV0_ASTATUS_DEVICE_BLOCKED))
		scsi_remove_target(&pcie_device->starget->dev);
	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: exit: "
		"handle(0x%04x), wwid(0x%016llx)\n", ioc->name, __func__,
		pcie_device->handle, (unsigned long long)
		pcie_device->wwid));
	if(pcie_device->enclosure_handle != 0)
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: exit: "
			"enclosure logical id(0x%016llx), slot(%d) \n",
			ioc->name, __func__,
			(unsigned long long)pcie_device->enclosure_logical_id,
			pcie_device->slot));
	if(pcie_device->connector_name[0] != '\0')
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: exit: "
			"enclosure level(0x%04x), connector name( %s)\n",
			ioc->name, __func__, pcie_device->enclosure_level,
			pcie_device->connector_name));

	kfree(pcie_device->serial_number);
}


/**
 * _scsih_pcie_check_device - checking device responsiveness
 * @ioc: per adapter object
 * @handle: attached device handle
 *
 * Returns nothing.
 */
static void
_scsih_pcie_check_device(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi26PCIeDevicePage0_t pcie_device_pg0;
	u32 ioc_status;
	struct _pcie_device *pcie_device;
	u64 wwid;
	unsigned long flags;
	struct scsi_target *starget;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	u32 device_info;
	u8 *serial_number = NULL;
	u8 *original_serial_number = NULL;
	int rc;

	if ((mpt3sas_config_get_pcie_device_pg0(ioc, &mpi_reply, &pcie_device_pg0,
		MPI26_PCIE_DEVICE_PGAD_FORM_HANDLE, handle)))
		return;

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
		return;

	/* check if this is end device */
	device_info = le32_to_cpu(pcie_device_pg0.DeviceInfo);
	if (!(_scsih_is_nvme_pciescsi_device(device_info)))
		return;

	wwid = le64_to_cpu(pcie_device_pg0.WWID);
	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	pcie_device = __mpt3sas_get_pdev_by_wwid(ioc, wwid);

	if (!pcie_device) {
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		return;
	}

	if (unlikely(pcie_device->handle != handle)) {
		starget = pcie_device->starget;
		sas_target_priv_data = starget->hostdata;
		pcie_device->access_status = pcie_device_pg0.AccessStatus;
		starget_printk(KERN_INFO, starget, "handle changed from(0x%04x)"
		   " to (0x%04x)!!!\n", pcie_device->handle, handle);
		sas_target_priv_data->handle = handle;
		pcie_device->handle = handle;

		if (le32_to_cpu(pcie_device_pg0.Flags) &
		    MPI26_PCIEDEV0_FLAGS_ENCL_LEVEL_VALID) {
			pcie_device->enclosure_level =
			    pcie_device_pg0.EnclosureLevel;
			memcpy(pcie_device->connector_name,
			    pcie_device_pg0.ConnectorName, 4);
			pcie_device->connector_name[4] = '\0';
		} else {
			pcie_device->enclosure_level = 0;
			pcie_device->connector_name[0] = '\0';
		}
	}

	/* check if device is present */
	if (!(le32_to_cpu(pcie_device_pg0.Flags) &
	    MPI26_PCIEDEV0_FLAGS_DEVICE_PRESENT)) {
		printk(MPT3SAS_ERR_FMT "device is not present "
		    "handle(0x%04x), flags!!!\n", ioc->name, handle);
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		pcie_device_put(pcie_device);
		return;
	}

	/* check if there were any issues with discovery */
	if (_scsih_check_pcie_access_status(ioc, wwid, handle,
	    pcie_device_pg0.AccessStatus)) {
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		pcie_device_put(pcie_device);
		return;
	}

	original_serial_number = pcie_device->serial_number;
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
	pcie_device_put(pcie_device);
	if (issue_scsi_cmd_to_bringup_drive)
		_scsih_ublock_io_device_wait(ioc, wwid, NULL);
	else
		_scsih_ublock_io_device_to_running(ioc, wwid, NULL);

	/* check to see if serial number still the same, if not, delete
	 * and re-add new device
	 */
	if (!original_serial_number)
		return;
	
	if (_scsih_inquiry_vpd_sn(ioc, handle, &serial_number) == DEVICE_READY
	    && serial_number)  {
		rc = strcmp(original_serial_number, serial_number);
		kfree(serial_number);
		if (!rc) {
			return;
		}
		_scsih_pcie_device_remove_by_wwid(ioc, wwid);
		_scsih_pcie_add_device(ioc, handle, 0);
	}
	return;
}

/**
 * _scsih_pcie_add_device -  creating pcie device object
 * @ioc: per adapter object
 * @handle: pcie device handle
 * @retry_count: number of times this event has been retried
 *
 * Creating end device object, stored in ioc->pcie_device_list.
 *
 * Return 1 means queue the event later, 0 means complete the event
 */
static int
_scsih_pcie_add_device(struct MPT3SAS_ADAPTER *ioc, u16 handle, u8 retry_count)
{
	Mpi26PCIeDevicePage0_t pcie_device_pg0;
	Mpi26PCIeDevicePage2_t pcie_device_pg2;
	Mpi2ConfigReply_t mpi_reply;
	struct _pcie_device *pcie_device;
	struct _enclosure_node *enclosure_dev;
	u32 pcie_device_type;
	u32 ioc_status;
	u64 wwid;
	enum device_responsive_state rc;
	u8 connector_name[5];
	u8 tr_timeout = 30;
	u8 tr_method = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
	u8 protection_mask;
#endif

	if ((mpt3sas_config_get_pcie_device_pg0(ioc, &mpi_reply,
	    &pcie_device_pg0, MPI26_PCIE_DEVICE_PGAD_FORM_HANDLE, handle))) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 0;
	}
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 0;
	}

	set_bit(handle, ioc->pend_os_device_add);
	wwid = le64_to_cpu(pcie_device_pg0.WWID);

	/* check if device is present */
	if (!(le32_to_cpu(pcie_device_pg0.Flags) &
		MPI26_PCIEDEV0_FLAGS_DEVICE_PRESENT)) {
		printk(MPT3SAS_ERR_FMT "device is not present "
		    "handle(0x04%x)!!!\n", ioc->name, handle);
		return 0;
	}

	/* check if there were any issues with discovery */
	if (_scsih_check_pcie_access_status(ioc, wwid, handle,
	    pcie_device_pg0.AccessStatus))
		return 0;

	if (!(_scsih_is_nvme_pciescsi_device(le32_to_cpu(pcie_device_pg0.DeviceInfo))))
		return 0;

	pcie_device = mpt3sas_get_pdev_by_wwid(ioc, wwid);
	if (pcie_device) {
		clear_bit(handle, ioc->pend_os_device_add);
		pcie_device_put(pcie_device);
		return 0;
	}

	/* PCIe Device Page 2 contains read-only information about a
	 * specific NVMe device; therefore, this page is only
	 * valid for NVMe devices and skip for pcie devices of type scsi.
	 */
	if (!(mpt3sas_scsih_is_pcie_scsi_device(
		le32_to_cpu(pcie_device_pg0.DeviceInfo)))) {
		if (mpt3sas_config_get_pcie_device_pg2(ioc, &mpi_reply, &pcie_device_pg2,
					MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle)) {
			printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n", ioc->name, __FILE__,
					__LINE__, __func__);
			return 0;
		}

		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
					ioc->name, __FILE__, __LINE__, __func__);
			return 0;
		}

		if (!ioc->tm_custom_handling) {
			tr_method = MPI26_SCSITASKMGMT_MSGFLAGS_PROTOCOL_LVL_RST_PCIE;
			if (pcie_device_pg2.ControllerResetTO)
				tr_timeout = pcie_device_pg2.ControllerResetTO;

		}
	}

	/*
	* Wait for device that is becoming ready
	* queue request later if device is busy.
	*/
	if ((!ioc->wait_for_discovery_to_complete) &&
		(issue_scsi_cmd_to_bringup_drive) &&
		(pcie_device_pg0.AccessStatus !=
			MPI26_PCIEDEV0_ASTATUS_DEVICE_BLOCKED)) {
		printk(MPT3SAS_INFO_FMT "detecting: handle(0x%04x), "
		    "wwid(0x%016llx), port(%d)\n", ioc->name, handle,
		    (unsigned long long)wwid, pcie_device_pg0.PortNum);

		rc = _scsih_wait_for_target_to_become_ready(ioc, handle,
		    retry_count, 0, tr_timeout, tr_method);
		if (rc != DEVICE_READY) {
			if (le16_to_cpu(pcie_device_pg0.EnclosureHandle) != 0)
				dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: "
				    "device not ready: slot(%d) \n", ioc->name,
				    __func__,
				    le16_to_cpu(pcie_device_pg0.Slot)));

			if (le32_to_cpu(pcie_device_pg0.Flags) &
			    MPI26_PCIEDEV0_FLAGS_ENCL_LEVEL_VALID) {
				memcpy(connector_name,
				    pcie_device_pg0.ConnectorName, 4);
				connector_name[4] = '\0';
				dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: "
				    "device not ready: enclosure "
				    "level(0x%04x), connector name( %s)\n",
				    ioc->name, __func__,
				    pcie_device_pg0.EnclosureLevel,
				    connector_name));
			}

			if (rc == DEVICE_RETRY || rc == DEVICE_START_UNIT ||
				rc == DEVICE_STOP_UNIT ||rc == DEVICE_RETRY_UA)
				return 1;
			else if (rc == DEVICE_ERROR)
				return 0;
		}
	}

	pcie_device = kzalloc(sizeof(struct _pcie_device), GFP_KERNEL);
	if (!pcie_device) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 0;
	}

	kref_init(&pcie_device->refcount);
	pcie_device->id = ioc->pcie_target_id++;
	pcie_device->channel = PCIE_CHANNEL;
	pcie_device->handle = handle;
	pcie_device->access_status = pcie_device_pg0.AccessStatus;
	pcie_device->device_info = le32_to_cpu(pcie_device_pg0.DeviceInfo);
	pcie_device->wwid = wwid;
	pcie_device->port_num = pcie_device_pg0.PortNum;
	pcie_device->fast_path = (le32_to_cpu(pcie_device_pg0.Flags) &
	    MPI26_PCIEDEV0_FLAGS_FAST_PATH_CAPABLE) ? 1 : 0;
	pcie_device_type = pcie_device->device_info &
	    MPI26_PCIE_DEVINFO_MASK_DEVICE_TYPE;

	pcie_device->enclosure_handle =
	    le16_to_cpu(pcie_device_pg0.EnclosureHandle);
	if (pcie_device->enclosure_handle != 0)
		pcie_device->slot = le16_to_cpu(pcie_device_pg0.Slot);

	if (le32_to_cpu(pcie_device_pg0.Flags) &
	    MPI26_PCIEDEV0_FLAGS_ENCL_LEVEL_VALID) {
		pcie_device->enclosure_level = pcie_device_pg0.EnclosureLevel;
		memcpy(pcie_device->connector_name,
		    pcie_device_pg0.ConnectorName, 4);
		pcie_device->connector_name[4] = '\0';
	} else {
		pcie_device->enclosure_level = 0;
		pcie_device->connector_name[0] = '\0';
	}

	/* get enclosure_logical_id */
	if (pcie_device->enclosure_handle) {
		enclosure_dev =
			mpt3sas_scsih_enclosure_find_by_handle(ioc,
						pcie_device->enclosure_handle);
		if (enclosure_dev)
			pcie_device->enclosure_logical_id =
			    le64_to_cpu(enclosure_dev->pg0.EnclosureLogicalID);
	}
    /*TODO -- Add device name once FW supports it*/
#if 0
	/* get device name */
	pcie_device->device_name = le64_to_cpu(pcie_device_pg0.DeviceName);
#endif
	if (!(mpt3sas_scsih_is_pcie_scsi_device(
		le32_to_cpu(pcie_device_pg0.DeviceInfo)))) {
		pcie_device->nvme_mdts =
			le32_to_cpu(pcie_device_pg2.MaximumDataTransferSize);
		pcie_device->shutdown_latency =
			le16_to_cpu(pcie_device_pg2.ShutdownLatency);
		if (pcie_device->shutdown_latency > ioc->max_shutdown_latency)
			ioc->max_shutdown_latency =
				pcie_device->shutdown_latency;
		if (pcie_device_pg2.ControllerResetTO)
			pcie_device->reset_timeout =
			pcie_device_pg2.ControllerResetTO;
		else
			pcie_device->reset_timeout = 30;
	}
	else 
		pcie_device->reset_timeout = 30;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
	if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
		if (!ioc->disable_eedp_support) {
		/* Disable DIX0 protection capability */
			protection_mask = scsi_host_get_prot(ioc->shost);
			if (protection_mask & SHOST_DIX_TYPE0_PROTECTION) {
				scsi_host_set_prot(ioc->shost, protection_mask & 0x77);
				printk(MPT3SAS_INFO_FMT ": Disabling DIX0 prot capability because HBA does"
					"not support DIX0 operation on NVME drives\n",ioc->name);
			}
		}
	}
#endif

	if (ioc->wait_for_discovery_to_complete)
		_scsih_pcie_device_init_add(ioc, pcie_device);
	else
		_scsih_pcie_device_add(ioc, pcie_device);

	pcie_device_put(pcie_device);
	return 0;
}

/**
 * _scsih_pcie_topology_change_event_debug - debug for topology
 * event
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 */
static void
_scsih_pcie_topology_change_event_debug(struct MPT3SAS_ADAPTER *ioc,
	Mpi26EventDataPCIeTopologyChangeList_t *event_data)
{
	int i;
	u16 handle;
	u16 reason_code;
	u8 port_number;
	char *status_str = NULL;
	u8 link_rate, prev_link_rate;

	switch (event_data->SwitchStatus) {
	case MPI26_EVENT_PCIE_TOPO_SS_ADDED:
		status_str = "add";
		break;
	case MPI26_EVENT_PCIE_TOPO_SS_NOT_RESPONDING:
		status_str = "remove";
		break;
	case MPI26_EVENT_PCIE_TOPO_SS_RESPONDING:
	case 0:
		status_str =  "responding";
		break;
	case MPI26_EVENT_PCIE_TOPO_SS_DELAY_NOT_RESPONDING:
		status_str = "remove delay";
		break;
	default:
		status_str = "unknown status";
		break;
	}
	printk(MPT3SAS_INFO_FMT "pcie topology change: (%s)\n",
		ioc->name, status_str);
	printk(KERN_INFO "\tswitch_handle(0x%04x), enclosure_handle(0x%04x) "
		"start_port(%02d), count(%d)\n",
		le16_to_cpu(event_data->SwitchDevHandle),
		le16_to_cpu(event_data->EnclosureHandle),
		event_data->StartPortNum, event_data->NumEntries);
	for (i = 0; i < event_data->NumEntries; i++) {
		handle = le16_to_cpu(event_data->PortEntry[i].AttachedDevHandle);
		if (!handle)
			continue;
		port_number = event_data->StartPortNum + i;
		reason_code = event_data->PortEntry[i].PortStatus;
		switch (reason_code) {
		case MPI26_EVENT_PCIE_TOPO_PS_DEV_ADDED:
			status_str = "target add";
			break;
		case MPI26_EVENT_PCIE_TOPO_PS_NOT_RESPONDING:
			status_str = "target remove";
			break;
		case MPI26_EVENT_PCIE_TOPO_PS_DELAY_NOT_RESPONDING:
			status_str = "delay target remove";
			break;
		case MPI26_EVENT_PCIE_TOPO_PS_PORT_CHANGED:
			status_str = "link rate change";
			break;
		case MPI26_EVENT_PCIE_TOPO_PS_NO_CHANGE:
			status_str = "target responding";
			break;
		default:
			status_str = "unknown";
			break;
		}
		link_rate = event_data->PortEntry[i].CurrentPortInfo &
			MPI26_EVENT_PCIE_TOPO_PI_RATE_MASK;
		prev_link_rate = event_data->PortEntry[i].PreviousPortInfo &
			MPI26_EVENT_PCIE_TOPO_PI_RATE_MASK;
		printk(KERN_INFO "\tport(%02d), attached_handle(0x%04x): %s:"
			" link rate: new(0x%02x), old(0x%02x)\n", port_number,
			handle, status_str, link_rate, prev_link_rate);
	}
}

/**
 * _scsih_pcie_topology_change_event - handle PCIe topology
 *  changes
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 */
static int
_scsih_pcie_topology_change_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	int i;
	u16 handle;
	u16 reason_code;
	u8 link_rate, prev_link_rate;
	unsigned long flags;
	int rc;
	int requeue_event;
	Mpi26EventDataPCIeTopologyChangeList_t *event_data = fw_event->event_data;
	struct _pcie_device *pcie_device;

	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_pcie_topology_change_event_debug(ioc, event_data);

	if (ioc->shost_recovery || ioc->remove_host || ioc->pci_error_recovery)
		return 0;

	if (fw_event->ignore) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "ignoring switch event\n",
			ioc->name));
		return 0;
	}

	/* handle siblings events */
	for (i = 0, requeue_event = 0; i < event_data->NumEntries; i++) {
		if (fw_event->ignore) {
			dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "ignoring switch event\n",
				ioc->name));
			return 0;
		}
		if (ioc->remove_host || ioc->pci_error_recovery)
			return 0;
		reason_code = event_data->PortEntry[i].PortStatus;
		handle = le16_to_cpu(event_data->PortEntry[i].AttachedDevHandle);
		if (!handle)
			continue;

		link_rate = event_data->PortEntry[i].CurrentPortInfo
			& MPI26_EVENT_PCIE_TOPO_PI_RATE_MASK;
		prev_link_rate = event_data->PortEntry[i].PreviousPortInfo
			& MPI26_EVENT_PCIE_TOPO_PI_RATE_MASK;

		switch (reason_code) {
		case MPI26_EVENT_PCIE_TOPO_PS_PORT_CHANGED:
			if (ioc->shost_recovery)
				break;
			if (link_rate == prev_link_rate)
				break;
			if (link_rate < MPI26_EVENT_PCIE_TOPO_PI_RATE_2_5)
				break;

			_scsih_pcie_check_device(ioc, handle);

			/* This code after this point handles the test case
			 * where a device has been added, however its returning
			 * BUSY for sometime.  Then before the Device Missing
			 * Delay expires and the device becomes READY, the
			 * device is removed and added back.
			 */
			spin_lock_irqsave(&ioc->pcie_device_lock, flags);
			pcie_device = __mpt3sas_get_pdev_by_handle(ioc, handle);
			spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

			if (pcie_device) {
				pcie_device_put(pcie_device);
				break;
			}

			if (!test_bit(handle, ioc->pend_os_device_add))
				break;

			dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
				"handle(0x%04x) device not found: convert "
				"event to a device add\n", ioc->name, handle));
			event_data->PortEntry[i].PortStatus &= 0xF0;
			event_data->PortEntry[i].PortStatus |=
				MPI26_EVENT_PCIE_TOPO_PS_DEV_ADDED;
			/* fall through */
		case MPI26_EVENT_PCIE_TOPO_PS_DEV_ADDED:
			if (ioc->shost_recovery)
				break;
			if (link_rate < MPI26_EVENT_PCIE_TOPO_PI_RATE_2_5)
				break;

			rc = _scsih_pcie_add_device(ioc, handle, fw_event->retries[i]);
			if (rc) {/* retry due to busy device */
				fw_event->retries[i]++;
				requeue_event = 1;
			} else {/* mark entry vacant */
			/* TODO This needs to be reviewed and fixed, we dont have an entry
			 * to make an event void like vacant
			 */
				event_data->PortEntry[i].PortStatus |=
					MPI26_EVENT_PCIE_TOPO_PS_NO_CHANGE;
			}
			break;
        case MPI26_EVENT_PCIE_TOPO_PS_NOT_RESPONDING:
			_scsih_pcie_device_remove_by_handle(ioc, handle);
			break;
		}
	}
	return requeue_event;
}


/**
 * _scsih_pcie_device_status_change_event_debug - debug for
 * device event
 * @event_data: event data payload
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_pcie_device_status_change_event_debug(struct MPT3SAS_ADAPTER *ioc,
	Mpi26EventDataPCIeDeviceStatusChange_t *event_data)
{
	char *reason_str = NULL;

	switch (event_data->ReasonCode) {
	case MPI26_EVENT_PCIDEV_STAT_RC_SMART_DATA:
		reason_str = "smart data";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_UNSUPPORTED:
		reason_str = "unsupported device discovered";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_INTERNAL_DEVICE_RESET:
		reason_str = "internal device reset";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_TASK_ABORT_INTERNAL:
		reason_str = "internal task abort";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_ABORT_TASK_SET_INTERNAL:
		reason_str = "internal task abort set";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_CLEAR_TASK_SET_INTERNAL:
		reason_str = "internal clear task set";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_QUERY_TASK_INTERNAL:
		reason_str = "internal query task";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_DEV_INIT_FAILURE:
		reason_str = "device init failure";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_CMP_INTERNAL_DEV_RESET:
		reason_str = "internal device reset complete";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_CMP_TASK_ABORT_INTERNAL:
		reason_str = "internal task abort complete";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_ASYNC_NOTIFICATION:
		reason_str = "internal async notification";
		break;
	case MPI26_EVENT_PCIDEV_STAT_RC_PCIE_HOT_RESET_FAILED:
		reason_str = "pcie hot reset failed";
		break;
	default:
		reason_str = "unknown reason";
		break;
	}

	printk(MPT3SAS_INFO_FMT "PCIE device status change: (%s)\n"
		"\thandle(0x%04x), WWID(0x%016llx), tag(%d)",
		ioc->name, reason_str, le16_to_cpu(event_data->DevHandle),
		(unsigned long long)le64_to_cpu(event_data->WWID),
		le16_to_cpu(event_data->TaskTag));
	if (event_data->ReasonCode == MPI26_EVENT_PCIDEV_STAT_RC_SMART_DATA)
		printk(MPT3SAS_INFO_FMT ", ASC(0x%x), ASCQ(0x%x)\n", ioc->name,
			event_data->ASC, event_data->ASCQ);
	printk(KERN_INFO "\n");
}

/**
 * _scsih_pcie_device_status_change_event - handle device status
 * change
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_pcie_device_status_change_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	struct MPT3SAS_TARGET *target_priv_data;
	struct _pcie_device *pcie_device;
	u64 wwid;
	unsigned long flags;
	Mpi26EventDataPCIeDeviceStatusChange_t *event_data = fw_event->event_data;


	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_pcie_device_status_change_event_debug(ioc,
			event_data);

	if (event_data->ReasonCode !=
		MPI26_EVENT_PCIDEV_STAT_RC_INTERNAL_DEVICE_RESET &&
		event_data->ReasonCode !=
		MPI26_EVENT_PCIDEV_STAT_RC_CMP_INTERNAL_DEV_RESET)
		return;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	wwid = le64_to_cpu(event_data->WWID);
	pcie_device = __mpt3sas_get_pdev_by_wwid(ioc, wwid);

	if (!pcie_device || !pcie_device->starget) {
		goto out;
	}

	target_priv_data = pcie_device->starget->hostdata;
	if (!target_priv_data) {
		goto out;
	}

	if (event_data->ReasonCode ==
		MPI26_EVENT_PCIDEV_STAT_RC_INTERNAL_DEVICE_RESET)
		target_priv_data->tm_busy = 1;
	else
		target_priv_data->tm_busy = 0;
out:
	if (pcie_device)
		pcie_device_put(pcie_device);

	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
}


/**
 * _scsih_sas_enclosure_dev_status_change_event_debug - debug for enclosure
 * event
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_enclosure_dev_status_change_event_debug(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataSasEnclDevStatusChange_t *event_data)
{
	char *reason_str = NULL;

	switch (event_data->ReasonCode) {
	case MPI2_EVENT_SAS_ENCL_RC_ADDED:
		reason_str = "enclosure add";
		break;
	case MPI2_EVENT_SAS_ENCL_RC_NOT_RESPONDING:
		reason_str = "enclosure remove";
		break;
	default:
		reason_str = "unknown reason";
		break;
	}

	printk(MPT3SAS_INFO_FMT "enclosure status change: (%s)\n"
	    "\thandle(0x%04x), enclosure logical id(0x%016llx)"
	    " number slots(%d)\n", ioc->name, reason_str,
	    le16_to_cpu(event_data->EnclosureHandle),
	    (unsigned long long)le64_to_cpu(event_data->EnclosureLogicalID),
	    le16_to_cpu(event_data->StartSlot));
}

/**
 * _scsih_sas_enclosure_dev_status_change_event - handle enclosure events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_enclosure_dev_status_change_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	Mpi2ConfigReply_t mpi_reply;
	struct _enclosure_node *enclosure_dev = NULL;
	Mpi2EventDataSasEnclDevStatusChange_t *event_data = fw_event->event_data;
	int rc;

	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_sas_enclosure_dev_status_change_event_debug(ioc,
							fw_event->event_data);

	if (ioc->shost_recovery)
		return;

	event_data->EnclosureHandle = le16_to_cpu(event_data->EnclosureHandle);

	if (event_data->EnclosureHandle)
		enclosure_dev =
			mpt3sas_scsih_enclosure_find_by_handle(ioc,
						event_data->EnclosureHandle);
	switch (event_data->ReasonCode) {
	case MPI2_EVENT_SAS_ENCL_RC_ADDED:
		if (!enclosure_dev) {
			enclosure_dev =
				kzalloc(sizeof(struct _enclosure_node), GFP_KERNEL);
			if (!enclosure_dev) {
				printk(MPT3SAS_ERR_FMT
					"failure at %s:%d/%s()!\n", ioc->name,
					__FILE__, __LINE__, __func__);
				return;
			}
			rc = mpt3sas_config_get_enclosure_pg0(ioc, &mpi_reply,
				&enclosure_dev->pg0, MPI2_SAS_ENCLOS_PGAD_FORM_HANDLE,
				event_data->EnclosureHandle);

			if (rc || (le16_to_cpu(mpi_reply.IOCStatus) &
                                                        MPI2_IOCSTATUS_MASK)) {
				kfree(enclosure_dev);
				return;
			}

			list_add_tail(&enclosure_dev->list,
							&ioc->enclosure_list);
		}
		break;
	case MPI2_EVENT_SAS_ENCL_RC_NOT_RESPONDING:
		if (enclosure_dev) {
			list_del(&enclosure_dev->list);
			kfree(enclosure_dev);
		}
		break;
	default:
		break;
	}
}

/**
 * _scsih_sas_broadcast_primitive_event - handle broadcast events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_broadcast_primitive_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	struct scsi_cmnd *scmd;
	struct scsi_device *sdev;
	u16 smid, handle;
	u32 lun;
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	u32 termination_count;
	u32 query_count;
	Mpi2SCSITaskManagementReply_t *mpi_reply;
	Mpi2EventDataSasBroadcastPrimitive_t *event_data = fw_event->event_data;
	u16 ioc_status;
	unsigned long flags;
	int r;
	u8 max_retries = 0;
	u8 task_abort_retries;
	struct scsiio_tracker *st;

	mutex_lock(&ioc->tm_cmds.mutex);
	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: enter: phy number(%d), "
	    "width(%d)\n", ioc->name, __func__, event_data->PhyNum,
	     event_data->PortWidth));

	  _scsih_block_io_all_device(ioc);

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	mpi_reply = ioc->tm_cmds.reply;
 broadcast_aen_retry:

	/* sanity checks for retrying this loop */
	if (max_retries++ == 5) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: giving up\n",
		    ioc->name, __func__));
		goto out;
	} else if (max_retries > 1)
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: %d retry\n",
		    ioc->name, __func__, max_retries - 1));

	termination_count = 0;
	query_count = 0;
	for (smid = 1; smid <= ioc->shost->can_queue; smid++) {
		if (ioc->shost_recovery)
			goto out;

		scmd = mpt3sas_scsih_scsi_lookup_get(ioc, smid);
		if (!scmd)
			continue;
		st = mpt3sas_base_scsi_cmd_priv(scmd);
		if (!st || st->smid == 0)
			continue;
		sdev = scmd->device;
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data || !sas_device_priv_data->sas_target)
			continue;
		 /* skip hidden raid components */
		if (sas_device_priv_data->sas_target->flags &
		    MPT_TARGET_FLAGS_RAID_COMPONENT)
			continue;
		 /* skip volumes */
		if (sas_device_priv_data->sas_target->flags &
		    MPT_TARGET_FLAGS_VOLUME)
			continue;
		 /* skip PCIe devices */
		if (sas_device_priv_data->sas_target->flags &
		    MPT_TARGET_FLAGS_PCIE_DEVICE)
			continue;
		
		handle = sas_device_priv_data->sas_target->handle;
		lun = sas_device_priv_data->lun;
		query_count++;

		if (ioc->shost_recovery)
			goto out;

		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		r = mpt3sas_scsih_issue_tm(ioc, handle, 0, 0, lun,
		    MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK, st->smid, 30, 0);
		if (r == FAILED) {
			sdev_printk(KERN_WARNING, sdev,
			    "mpt3sas_scsih_issue_tm: FAILED when sending "
			    "QUERY_TASK: scmd(%p)\n", scmd);
			spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
			goto broadcast_aen_retry;
		}
		ioc_status = le16_to_cpu(mpi_reply->IOCStatus)
		    & MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			sdev_printk(KERN_WARNING, sdev, "query task: FAILED "
			    "with IOCSTATUS(0x%04x), scmd(%p)\n", ioc_status,
			    scmd);
			spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
			goto broadcast_aen_retry;
		}

		/* see if IO is still owned by IOC and target */
		if (mpi_reply->ResponseCode ==
		     MPI2_SCSITASKMGMT_RSP_TM_SUCCEEDED ||
		     mpi_reply->ResponseCode ==
		     MPI2_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC) {
			spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
			continue;
		}
		task_abort_retries = 0;
 tm_retry:
		if (task_abort_retries++ == 60) {
			dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
			    "%s: ABORT_TASK: giving up\n", ioc->name,
			    __func__));
			spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
			goto broadcast_aen_retry;
		}

		if (ioc->shost_recovery)
			goto out_no_lock;

		r = mpt3sas_scsih_issue_tm(ioc, handle, sdev->channel, sdev->id,
		    sdev->lun, MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK, st->smid, 30,
		    0);
		if (r == FAILED) {
			sdev_printk(KERN_WARNING, sdev,
			    "mpt3sas_scsih_issue_tm: ABORT_TASK: FAILED : "
			    "scmd(%p)\n", scmd);
			goto tm_retry;
		}

		if (task_abort_retries > 1)
			sdev_printk(KERN_WARNING, sdev,
			    "mpt3sas_scsih_issue_tm: ABORT_TASK: RETRIES (%d):"
			    " scmd(%p)\n",
			    task_abort_retries - 1, scmd);

		termination_count += le32_to_cpu(mpi_reply->TerminationCount);
		spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	}

	if (ioc->broadcast_aen_pending) {
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: loop back due to"
		     " pending AEN\n", ioc->name, __func__));
		 ioc->broadcast_aen_pending = 0;
		 goto broadcast_aen_retry;
	}

 out:
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
 out_no_lock:

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
	    "%s - exit, query_count = %d termination_count = %d\n",
	    ioc->name, __func__, query_count, termination_count));

	ioc->broadcast_aen_busy = 0;
	if (!ioc->shost_recovery)
		_scsih_ublock_io_all_device(ioc, 1);
	mutex_unlock(&ioc->tm_cmds.mutex);
}

/**
 * _scsih_sas_discovery_event - handle discovery events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_discovery_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	Mpi2EventDataSasDiscovery_t *event_data = fw_event->event_data;

	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK) {
		printk(MPT3SAS_INFO_FMT "sas discovery event: (%s)", ioc->name,
		    (event_data->ReasonCode == MPI2_EVENT_SAS_DISC_RC_STARTED) ?
		    "start" : "stop");
	if (event_data->DiscoveryStatus)
		printk("discovery_status(0x%08x)",
		    le32_to_cpu(event_data->DiscoveryStatus));
	printk("\n");
	}

	if (event_data->ReasonCode == MPI2_EVENT_SAS_DISC_RC_STARTED &&
	    !ioc->sas_hba.num_phys) {
		if (disable_discovery > 0 && ioc->shost_recovery) {
			/* Wait for the reset to complete */
			while (ioc->shost_recovery)
				ssleep(1);
		}
		_scsih_sas_host_add(ioc);
	}
}

/**
 * _scsih_sas_device_discovery_error_event - display SAS device discovery error events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_device_discovery_error_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	Mpi25EventDataSasDeviceDiscoveryError_t *event_data = fw_event->event_data;

	switch (event_data->ReasonCode) {

	case MPI25_EVENT_SAS_DISC_ERR_SMP_FAILED:
		printk(MPT3SAS_WARN_FMT "SMP command sent to the expander" \
					"(handle:0x%04x, sas_address:0x%016llx," \
					"physical_port:0x%02x) has failed\n", \
					ioc->name, le16_to_cpu(event_data->DevHandle),
					(unsigned long long)le64_to_cpu(event_data->SASAddress),
					event_data->PhysicalPort);
		break;

	case MPI25_EVENT_SAS_DISC_ERR_SMP_TIMEOUT:
		printk(MPT3SAS_WARN_FMT "SMP command sent to the expander" \
					"(handle:0x%04x, sas_address:0x%016llx," \
					"physical_port:0x%02x) has timed out\n", \
					ioc->name, le16_to_cpu(event_data->DevHandle),
					(unsigned long long)le64_to_cpu(event_data->SASAddress),
					event_data->PhysicalPort);
		break;
	default:
		break;
	}
}

/**
 * _scsih_pcie_enumeration_event - handle enumeration events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_pcie_enumeration_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	Mpi26EventDataPCIeEnumeration_t *event_data = fw_event->event_data;

	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK) {
		printk(MPT3SAS_INFO_FMT "pcie enumeration event: (%s) Flag 0x%02x",
			ioc->name,
			((event_data->ReasonCode == MPI26_EVENT_PCIE_ENUM_RC_STARTED) ?
			 "started" : "completed"), event_data->Flags);
	if (event_data->EnumerationStatus)
		printk("enumeration_status(0x%08x)",
		    le32_to_cpu(event_data->EnumerationStatus));
	printk("\n");
	}
}

/**
 * _scsih_ir_fastpath - turn on fastpath for IR physdisk
 * @ioc: per adapter object
 * @handle: device handle for physical disk
 * @phys_disk_num: physical disk number
 *
 * Return 0 for success, else failure.
 */
static int
_scsih_ir_fastpath(struct MPT3SAS_ADAPTER *ioc, u16 handle, u8 phys_disk_num)
{
	Mpi2RaidActionRequest_t *mpi_request;
	Mpi2RaidActionReply_t *mpi_reply;
	u16 smid;
	u8 issue_reset = 0;
	int rc = 0;
	u16 ioc_status;
	u32 log_info;

	mutex_lock(&ioc->scsih_cmds.mutex);

	if (ioc->scsih_cmds.status != MPT3_CMD_NOT_USED) {
		printk(MPT3SAS_ERR_FMT "%s: scsih_cmd in use\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}
	ioc->scsih_cmds.status = MPT3_CMD_PENDING;

	smid = mpt3sas_base_get_smid(ioc, ioc->scsih_cb_idx);
	if (!smid) {
		printk(MPT3SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
		rc = -EAGAIN;
		goto out;
	}

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->scsih_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2RaidActionRequest_t));

	mpi_request->Function = MPI2_FUNCTION_RAID_ACTION;
	mpi_request->Action = MPI2_RAID_ACTION_PHYSDISK_HIDDEN;
	mpi_request->PhysDiskNum = phys_disk_num;

	dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "IR RAID_ACTION: turning fast "
	    "path on for handle(0x%04x), phys_disk_num (0x%02x)\n", ioc->name,
	    handle, phys_disk_num));

	init_completion(&ioc->scsih_cmds.done);
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->scsih_cmds.done, 10*HZ);
	if (!(ioc->scsih_cmds.status & MPT3_CMD_COMPLETE)) {
		mpt3sas_check_cmd_timeout(ioc,
		    ioc->scsih_cmds.status, mpi_request,
		    sizeof(Mpi2RaidActionRequest_t)/4, issue_reset);
		rc = -EFAULT;
		goto out;
	}

	if (ioc->scsih_cmds.status & MPT3_CMD_REPLY_VALID) {

		mpi_reply = ioc->scsih_cmds.reply;
		ioc_status = le16_to_cpu(mpi_reply->IOCStatus);
		if (ioc_status & MPI2_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE)
			log_info =  le32_to_cpu(mpi_reply->IOCLogInfo);
		else
			log_info = 0;
		ioc_status &= MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
			    "IR RAID_ACTION: failed: ioc_status(0x%04x), "
			    "loginfo(0x%08x)!!!\n", ioc->name, ioc_status,
			    log_info));
			rc = -EFAULT;
		} else
			dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
			    "IR RAID_ACTION: completed successfully\n",
			    ioc->name));
	}

 out:
	ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
	mutex_unlock(&ioc->scsih_cmds.mutex);

	if (issue_reset)
		mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	return rc;
}

/**
 * _scsih_reprobe_lun - reprobing lun
 * @sdev: scsi device struct
 * @no_uld_attach: sdev->no_uld_attach flag setting
 *
 **/
static void
_scsih_reprobe_lun(struct scsi_device *sdev, void *no_uld_attach)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
	int rc;
#endif
	sdev->no_uld_attach = no_uld_attach ? 1 : 0;
	sdev_printk(KERN_INFO, sdev, "%s raid component\n",
	    sdev->no_uld_attach ? "hiding" : "exposing");
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
	rc = scsi_device_reprobe(sdev);
#else
	scsi_device_reprobe(sdev);
#endif
}

/**
 * _scsih_sas_volume_add - add new volume
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_volume_add(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventIrConfigElement_t *element)
{
	struct _raid_device *raid_device;
	unsigned long flags;
	u64 wwid;
	u16 handle = le16_to_cpu(element->VolDevHandle);
	int rc;

	mpt3sas_config_get_volume_wwid(ioc, handle, &wwid);
	if (!wwid) {
		printk(MPT3SAS_ERR_FMT
		    "failure at %s:%d/%s()!\n", ioc->name,
		    __FILE__, __LINE__, __func__);
		return;
	}

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	raid_device = _scsih_raid_device_find_by_wwid(ioc, wwid);
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);

	if (raid_device)
		return;

	raid_device = kzalloc(sizeof(struct _raid_device), GFP_KERNEL);
	if (!raid_device) {
		printk(MPT3SAS_ERR_FMT
		    "failure at %s:%d/%s()!\n", ioc->name,
		    __FILE__, __LINE__, __func__);
		return;
	}

	raid_device->id = ioc->sas_id++;
	raid_device->channel = RAID_CHANNEL;
	raid_device->handle = handle;
	raid_device->wwid = wwid;
	_scsih_raid_device_add(ioc, raid_device);
	if (!ioc->wait_for_discovery_to_complete) {
		rc = scsi_add_device(ioc->shost, RAID_CHANNEL,
		    raid_device->id, 0);
		if (rc)
			_scsih_raid_device_remove(ioc, raid_device);
	} else {
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		_scsih_determine_boot_device(ioc, raid_device, RAID_CHANNEL);
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
	}
}

/**
 * _scsih_sas_volume_delete - delete volume
 * @ioc: per adapter object
 * @handle: volume device handle
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_volume_delete(struct MPT3SAS_ADAPTER *ioc, u16 handle)
{
	struct _raid_device *raid_device;
	unsigned long flags;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct scsi_target *starget = NULL;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
	u8 protection_mask;
#endif
	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	raid_device = mpt3sas_raid_device_find_by_handle(ioc, handle);
	if (raid_device) {
		if (raid_device->starget) {
			starget = raid_device->starget;
			sas_target_priv_data = starget->hostdata;
			sas_target_priv_data->deleted = 1;
		}
		printk(MPT3SAS_INFO_FMT "removing handle(0x%04x), wwid"
		    "(0x%016llx)\n", ioc->name,  raid_device->handle,
		    (unsigned long long) raid_device->wwid);
		list_del(&raid_device->list);
		kfree(raid_device);
	}
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
	if (starget)
		scsi_remove_target(&starget->dev);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
	if ((!ioc->disable_eedp_support)  &&  (prot_mask & SHOST_DIX_TYPE0_PROTECTION)
		&& (ioc->hba_mpi_version_belonged != MPI2_VERSION)){
		/* are there any volumes ? */
		if (list_empty(&ioc->raid_device_list)){
		/* Enabling DIX0 protection capability  */
			protection_mask = scsi_host_get_prot(ioc->shost);
			if (!(protection_mask & SHOST_DIX_TYPE0_PROTECTION)) {
				scsi_host_set_prot(ioc->shost, protection_mask | 8);
				printk(MPT3SAS_INFO_FMT ": Enabling DIX0 prot capability \n",
		       	ioc->name);
			}
		}
	}
#endif
}

/**
 * _scsih_sas_pd_expose - expose pd component to /dev/sdX
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_pd_expose(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventIrConfigElement_t *element)
{
	struct _sas_device *sas_device;
	struct scsi_target *starget = NULL;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	unsigned long flags;
	u16 handle = le16_to_cpu(element->PhysDiskDevHandle);

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_handle(ioc, handle);
	if (sas_device) {
		sas_device->volume_handle = 0;
		sas_device->volume_wwid = 0;
		clear_bit(handle, ioc->pd_handles);
		if (sas_device->starget && sas_device->starget->hostdata) {
			starget = sas_device->starget;
			sas_target_priv_data = starget->hostdata;
			sas_target_priv_data->flags &=
			    ~MPT_TARGET_FLAGS_RAID_COMPONENT;
			sas_device->pfa_led_on = 0;
			sas_device_put(sas_device);
		}
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (!sas_device)
		return;

	/* exposing raid component */
	if (starget)
		starget_for_each_device(starget, NULL, _scsih_reprobe_lun);
}

/**
 * _scsih_sas_pd_hide - hide pd component from /dev/sdX
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_pd_hide(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventIrConfigElement_t *element)
{
	struct _sas_device *sas_device;
	struct scsi_target *starget = NULL;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	unsigned long flags;
	u16 handle = le16_to_cpu(element->PhysDiskDevHandle);
	u16 volume_handle = 0;
	u64 volume_wwid = 0;

	mpt3sas_config_get_volume_handle(ioc, handle, &volume_handle);
	if (volume_handle)
		mpt3sas_config_get_volume_wwid(ioc, volume_handle,
		    &volume_wwid);

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = __mpt3sas_get_sdev_by_handle(ioc, handle);
	if (sas_device) {
		set_bit(handle, ioc->pd_handles);
		if (sas_device->starget && sas_device->starget->hostdata) {
			starget = sas_device->starget;
			sas_target_priv_data = starget->hostdata;
			sas_target_priv_data->flags |=
			    MPT_TARGET_FLAGS_RAID_COMPONENT;
			sas_device->volume_handle = volume_handle;
			sas_device->volume_wwid = volume_wwid;
			sas_device_put(sas_device);
		}
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (!sas_device)
		return;
	/* hiding raid component */
	if(ioc->hba_mpi_version_belonged != MPI2_VERSION)
		_scsih_ir_fastpath(ioc, handle, element->PhysDiskNum);

	if (starget)
		starget_for_each_device(starget, (void *)1, _scsih_reprobe_lun);

}

/**
 * _scsih_sas_pd_delete - delete pd component
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_pd_delete(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventIrConfigElement_t *element)
{
	u16 handle = le16_to_cpu(element->PhysDiskDevHandle);

	_scsih_device_remove_by_handle(ioc, handle);
}

/**
 * _scsih_sas_pd_add - remove pd component
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_pd_add(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventIrConfigElement_t *element)
{
	struct _sas_device *sas_device;
	u16 handle = le16_to_cpu(element->PhysDiskDevHandle);
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	u32 ioc_status;
	u64 sas_address;
	u16 parent_handle;

	set_bit(handle, ioc->pd_handles);
	sas_device = mpt3sas_get_sdev_by_handle(ioc, handle);
	if (sas_device) {
		if(ioc->hba_mpi_version_belonged != MPI2_VERSION)
			_scsih_ir_fastpath(ioc, handle, element->PhysDiskNum);
		sas_device_put(sas_device);
		return;
	}

	if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle))) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	parent_handle = le16_to_cpu(sas_device_pg0.ParentDevHandle);
	if (!_scsih_get_sas_address(ioc, parent_handle, &sas_address))
		mpt3sas_transport_update_links(ioc, sas_address, handle,
		    sas_device_pg0.PhyNum, MPI2_SAS_NEG_LINK_RATE_1_5,
		    mpt3sas_get_port_by_id(ioc, sas_device_pg0.PhysicalPort, 0));
	if(ioc->hba_mpi_version_belonged != MPI2_VERSION)
		_scsih_ir_fastpath(ioc, handle, element->PhysDiskNum);
	_scsih_add_device(ioc, handle, 0, 1);
}

/**
 * _scsih_sas_ir_config_change_event_debug - debug for IR Config Change events
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_config_change_event_debug(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataIrConfigChangeList_t *event_data)
{
	Mpi2EventIrConfigElement_t *element;
	u8 element_type;
	int i;
	char *reason_str = NULL, *element_str = NULL;

	element = (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];

	printk(MPT3SAS_INFO_FMT "raid config change: (%s), elements(%d)\n",
	    ioc->name, (le32_to_cpu(event_data->Flags) &
	    MPI2_EVENT_IR_CHANGE_FLAGS_FOREIGN_CONFIG) ?
	    "foreign" : "native", event_data->NumElements);
	for (i = 0; i < event_data->NumElements; i++, element++) {
		switch (element->ReasonCode) {
		case MPI2_EVENT_IR_CHANGE_RC_ADDED:
			reason_str = "add";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_REMOVED:
			reason_str = "remove";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_NO_CHANGE:
			reason_str = "no change";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_HIDE:
			reason_str = "hide";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_UNHIDE:
			reason_str = "unhide";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_VOLUME_CREATED:
			reason_str = "volume_created";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_VOLUME_DELETED:
			reason_str = "volume_deleted";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_PD_CREATED:
			reason_str = "pd_created";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_PD_DELETED:
			reason_str = "pd_deleted";
			break;
		default:
			reason_str = "unknown reason";
			break;
		}
		element_type = le16_to_cpu(element->ElementFlags) &
		    MPI2_EVENT_IR_CHANGE_EFLAGS_ELEMENT_TYPE_MASK;
		switch (element_type) {
		case MPI2_EVENT_IR_CHANGE_EFLAGS_VOLUME_ELEMENT:
			element_str = "volume";
			break;
		case MPI2_EVENT_IR_CHANGE_EFLAGS_VOLPHYSDISK_ELEMENT:
			element_str = "phys disk";
			break;
		case MPI2_EVENT_IR_CHANGE_EFLAGS_HOTSPARE_ELEMENT:
			element_str = "hot spare";
			break;
		default:
			element_str = "unknown element";
			break;
		}
		printk(KERN_INFO "\t(%s:%s), vol handle(0x%04x), "
		    "pd handle(0x%04x), pd num(0x%02x)\n", element_str,
		    reason_str, le16_to_cpu(element->VolDevHandle),
		    le16_to_cpu(element->PhysDiskDevHandle),
		    element->PhysDiskNum);
	}
}

/**
 * _scsih_sas_ir_config_change_event - handle ir configuration change events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_config_change_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	Mpi2EventIrConfigElement_t *element;
	int i;
	u8 foreign_config;
	Mpi2EventDataIrConfigChangeList_t *event_data = fw_event->event_data;

	if ((ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
	    && !ioc->warpdrive_msg)
		_scsih_sas_ir_config_change_event_debug(ioc, event_data);

	foreign_config = (le32_to_cpu(event_data->Flags) &
	    MPI2_EVENT_IR_CHANGE_FLAGS_FOREIGN_CONFIG) ? 1 : 0;

	element = (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];
	if (ioc->shost_recovery &&
			ioc->hba_mpi_version_belonged != MPI2_VERSION) {
		for (i = 0; i < event_data->NumElements; i++, element++) {
			if(element->ReasonCode == MPI2_EVENT_IR_CHANGE_RC_HIDE)
				_scsih_ir_fastpath(ioc, le16_to_cpu(element->PhysDiskDevHandle), element->PhysDiskNum);
		}
		return;
	}
	for (i = 0; i < event_data->NumElements; i++, element++) {

		switch (element->ReasonCode) {
		case MPI2_EVENT_IR_CHANGE_RC_VOLUME_CREATED:
		case MPI2_EVENT_IR_CHANGE_RC_ADDED:
			if (!foreign_config)
				_scsih_sas_volume_add(ioc, element);
			break;
		case MPI2_EVENT_IR_CHANGE_RC_VOLUME_DELETED:
		case MPI2_EVENT_IR_CHANGE_RC_REMOVED:
			if (!foreign_config)
				_scsih_sas_volume_delete(ioc,
				    le16_to_cpu(element->VolDevHandle));
			break;
		case MPI2_EVENT_IR_CHANGE_RC_PD_CREATED:
			if (!ioc->is_warpdrive)
				_scsih_sas_pd_hide(ioc, element);
			break;
		case MPI2_EVENT_IR_CHANGE_RC_PD_DELETED:
			if (!ioc->is_warpdrive)
				_scsih_sas_pd_expose(ioc, element);
			break;
		case MPI2_EVENT_IR_CHANGE_RC_HIDE:
			if (!ioc->is_warpdrive)
				_scsih_sas_pd_add(ioc, element);
			break;
		case MPI2_EVENT_IR_CHANGE_RC_UNHIDE:
			if (!ioc->is_warpdrive)
				_scsih_sas_pd_delete(ioc, element);
			break;
		}
	}
}

/**
 * _scsih_sas_ir_volume_event - IR volume event
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_volume_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	u64 wwid;
	unsigned long flags;
	struct _raid_device *raid_device;
	u16 handle;
	u32 state;
	int rc;
	Mpi2EventDataIrVolume_t *event_data = fw_event->event_data;

	if (ioc->shost_recovery)
		return;

	if (event_data->ReasonCode != MPI2_EVENT_IR_VOLUME_RC_STATE_CHANGED)
		return;

	handle = le16_to_cpu(event_data->VolDevHandle);
	state = le32_to_cpu(event_data->NewValue);
	if (!ioc->warpdrive_msg)
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: handle(0x%04x), "
		    "old(0x%08x), new(0x%08x)\n", ioc->name, __func__,  handle,
		    le32_to_cpu(event_data->PreviousValue), state));
	switch (state) {
	case MPI2_RAID_VOL_STATE_MISSING:
	case MPI2_RAID_VOL_STATE_FAILED:
		_scsih_sas_volume_delete(ioc, handle);
		break;

	case MPI2_RAID_VOL_STATE_ONLINE:
	case MPI2_RAID_VOL_STATE_DEGRADED:
	case MPI2_RAID_VOL_STATE_OPTIMAL:

		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = mpt3sas_raid_device_find_by_handle(ioc, handle);
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);

		if (raid_device)
			break;

		mpt3sas_config_get_volume_wwid(ioc, handle, &wwid);
		if (!wwid) {
			printk(MPT3SAS_ERR_FMT
			    "failure at %s:%d/%s()!\n", ioc->name,
			    __FILE__, __LINE__, __func__);
			break;
		}

		raid_device = kzalloc(sizeof(struct _raid_device), GFP_KERNEL);
		if (!raid_device) {
			printk(MPT3SAS_ERR_FMT
			    "failure at %s:%d/%s()!\n", ioc->name,
			    __FILE__, __LINE__, __func__);
			break;
		}

		raid_device->id = ioc->sas_id++;
		raid_device->channel = RAID_CHANNEL;
		raid_device->handle = handle;
		raid_device->wwid = wwid;
		_scsih_raid_device_add(ioc, raid_device);
		rc = scsi_add_device(ioc->shost, RAID_CHANNEL,
		    raid_device->id, 0);
		if (rc)
			_scsih_raid_device_remove(ioc, raid_device);
		break;

	case MPI2_RAID_VOL_STATE_INITIALIZING:
	default:
		break;
	}
}

/**
 * _scsih_sas_ir_physical_disk_event - PD event
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_physical_disk_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	u16 handle, parent_handle;
	u32 state;
	struct _sas_device *sas_device;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	u32 ioc_status;
	Mpi2EventDataIrPhysicalDisk_t *event_data = fw_event->event_data;
	u64 sas_address;

	if (ioc->shost_recovery)
		return;

	if (event_data->ReasonCode != MPI2_EVENT_IR_PHYSDISK_RC_STATE_CHANGED)
		return;

	handle = le16_to_cpu(event_data->PhysDiskDevHandle);
	state = le32_to_cpu(event_data->NewValue);

	if (!ioc->warpdrive_msg)
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: handle(0x%04x), "
		    "old(0x%08x), new(0x%08x)\n", ioc->name, __func__,  handle,
		    le32_to_cpu(event_data->PreviousValue), state));
	switch (state) {
	case MPI2_RAID_PD_STATE_ONLINE:
	case MPI2_RAID_PD_STATE_DEGRADED:
	case MPI2_RAID_PD_STATE_REBUILDING:
	case MPI2_RAID_PD_STATE_OPTIMAL:
	case MPI2_RAID_PD_STATE_HOT_SPARE:

		if (!ioc->is_warpdrive)
			set_bit(handle, ioc->pd_handles);

		sas_device = mpt3sas_get_sdev_by_handle(ioc, handle);

		if (sas_device) {
			sas_device_put(sas_device);
			return;
		}
		if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply,
		    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_HANDLE,
		    handle))) {
			printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			return;
		}

		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			return;
		}

		parent_handle = le16_to_cpu(sas_device_pg0.ParentDevHandle);
		if (!_scsih_get_sas_address(ioc, parent_handle, &sas_address))
			mpt3sas_transport_update_links(ioc, sas_address,
			    handle, sas_device_pg0.PhyNum,
			    MPI2_SAS_NEG_LINK_RATE_1_5,
			    mpt3sas_get_port_by_id(ioc, sas_device_pg0.PhysicalPort, 0));

		_scsih_add_device(ioc, handle, 0, 1);

		break;

	case MPI2_RAID_PD_STATE_OFFLINE:
	case MPI2_RAID_PD_STATE_NOT_CONFIGURED:
	case MPI2_RAID_PD_STATE_NOT_COMPATIBLE:
	default:
		break;
	}
}

/**
 * _scsih_sas_ir_operation_status_event_debug - debug for IR op event
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_operation_status_event_debug(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventDataIrOperationStatus_t *event_data)
{
	char *reason_str = NULL;

	switch (event_data->RAIDOperation) {
	case MPI2_EVENT_IR_RAIDOP_RESYNC:
		reason_str = "resync";
		break;
	case MPI2_EVENT_IR_RAIDOP_ONLINE_CAP_EXPANSION:
		reason_str = "online capacity expansion";
		break;
	case MPI2_EVENT_IR_RAIDOP_CONSISTENCY_CHECK:
		reason_str = "consistency check";
		break;
	case MPI2_EVENT_IR_RAIDOP_BACKGROUND_INIT:
		reason_str = "background init";
		break;
	case MPI2_EVENT_IR_RAIDOP_MAKE_DATA_CONSISTENT:
		reason_str = "make data consistent";
		break;
	}

	if (!reason_str)
		return;

	printk(MPT3SAS_INFO_FMT "raid operational status: (%s)"
	    "\thandle(0x%04x), percent complete(%d)\n",
	    ioc->name, reason_str,
	    le16_to_cpu(event_data->VolDevHandle),
	    event_data->PercentComplete);
}

/**
 * _scsih_sas_ir_operation_status_event - handle RAID operation events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_operation_status_event(struct MPT3SAS_ADAPTER *ioc,
	struct fw_event_work *fw_event)
{
	Mpi2EventDataIrOperationStatus_t *event_data = fw_event->event_data;
	static struct _raid_device *raid_device;
	unsigned long flags;
	u16 handle;

	if ((ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
	     && !ioc->warpdrive_msg)
		_scsih_sas_ir_operation_status_event_debug(ioc,
		     event_data);

	/* code added for raid transport support */
	if (event_data->RAIDOperation == MPI2_EVENT_IR_RAIDOP_RESYNC) {

		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		handle = le16_to_cpu(event_data->VolDevHandle);
		raid_device = mpt3sas_raid_device_find_by_handle(ioc, handle);
		if (raid_device)
			raid_device->percent_complete =
			    event_data->PercentComplete;
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
	}
}

/**
 * _scsih_prep_device_scan - initialize parameters prior to device scan
 * @ioc: per adapter object
 *
 * Set the deleted flag prior to device scan.  If the device is found during
 * the scan, then we clear the deleted flag.
 */
static void
_scsih_prep_device_scan(struct MPT3SAS_ADAPTER *ioc)
{
	struct MPT3SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (sas_device_priv_data && sas_device_priv_data->sas_target)
			sas_device_priv_data->sas_target->deleted = 1;
	}
}

/**
 * _scsih_mark_responding_sas_device - mark a sas_devices as responding
 * @ioc: per adapter object
 * @sas_device_pg0: SAS Device page 0
 *
 * After host reset, find out whether devices are still responding.
 * Used in _scsih_remove_unresponsive_sas_devices.
 *
 * Return nothing.
 */
static void
_scsih_mark_responding_sas_device(struct MPT3SAS_ADAPTER *ioc,
Mpi2SasDevicePage0_t *sas_device_pg0)
{
	struct MPT3SAS_TARGET *sas_target_priv_data = NULL;
	struct scsi_target *starget;
	struct _sas_device *sas_device;
	struct _enclosure_node *enclosure_dev = NULL;
	unsigned long flags;
	struct hba_port *port;

	port = mpt3sas_get_port_by_id(ioc, sas_device_pg0->PhysicalPort, 0);

	if (sas_device_pg0->EnclosureHandle) {
		enclosure_dev =
			mpt3sas_scsih_enclosure_find_by_handle(ioc,
				le16_to_cpu(sas_device_pg0->EnclosureHandle));
		if(enclosure_dev == NULL)
			printk(MPT3SAS_INFO_FMT "Enclosure handle(0x%04x)"
			       "doesn't match with enclosure device!\n",
			       ioc->name, sas_device_pg0->EnclosureHandle);
	}

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_for_each_entry(sas_device, &ioc->sas_device_list, list) {
		if ((sas_device->sas_address == le64_to_cpu(
		    sas_device_pg0->SASAddress)) && (sas_device->slot ==
		    le16_to_cpu(sas_device_pg0->Slot)) &&
		    (sas_device->port == port)) {
			sas_device->responding = 1;
			starget = sas_device->starget;
			if (starget && starget->hostdata) {
				sas_target_priv_data = starget->hostdata;
				sas_target_priv_data->tm_busy = 0;
				sas_target_priv_data->deleted = 0;
			} else
				sas_target_priv_data = NULL;
			if (starget) {
				starget_printk(KERN_INFO, starget,
				   "handle(0x%04x), sas_address(0x%016llx),"
				   " port: %d\n", sas_device->handle,
				   (unsigned long long)sas_device->sas_address,
				   sas_device->port->port_id);
				if (sas_device->enclosure_handle != 0)
					starget_printk(KERN_INFO, starget,
					    "enclosure logical id(0x%016llx), slot(%d)\n",
					    (unsigned long long)sas_device->enclosure_logical_id,
					    sas_device->slot);
			}

			if (le16_to_cpu(sas_device_pg0->Flags) &
 			    MPI2_SAS_DEVICE0_FLAGS_ENCL_LEVEL_VALID &&
			    (ioc->hba_mpi_version_belonged != MPI2_VERSION)) {
				sas_device->enclosure_level = sas_device_pg0->EnclosureLevel;
				memcpy(sas_device->connector_name, sas_device_pg0->ConnectorName, 4);
				sas_device->connector_name[4] = '\0';
			} else {
				sas_device->enclosure_level = 0;
				sas_device->connector_name[0] = '\0';
			}

			sas_device->enclosure_handle =
				le16_to_cpu(sas_device_pg0->EnclosureHandle);
			sas_device->is_chassis_slot_valid = 0;
			if (enclosure_dev) {
				sas_device->enclosure_logical_id =
					le64_to_cpu(enclosure_dev->pg0.EnclosureLogicalID);
				if (le16_to_cpu(enclosure_dev->pg0.Flags) &
		    		    MPI2_SAS_ENCLS0_FLAGS_CHASSIS_SLOT_VALID) {
					sas_device->is_chassis_slot_valid = 1;
					sas_device->chassis_slot =
							enclosure_dev->pg0.ChassisSlot;
				}
			}

			if (sas_device->handle == le16_to_cpu(
			    sas_device_pg0->DevHandle))
				goto out;
			printk(KERN_INFO "\thandle changed from(0x%04x)!!!\n",
			    sas_device->handle);
			sas_device->handle = le16_to_cpu(
			    sas_device_pg0->DevHandle);
			if (sas_target_priv_data)
 				sas_target_priv_data->handle =
				    le16_to_cpu(sas_device_pg0->DevHandle);
			goto out;
		}
	}
 out:
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
}

/**
 * _scsih_create_enclosure_list_after_reset - Free Existing list,
 *	And create enclosure list by scanning all Enclosure Page(0)s
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_scsih_create_enclosure_list_after_reset(struct MPT3SAS_ADAPTER *ioc)
{
	struct _enclosure_node *enclosure_dev;
	Mpi2ConfigReply_t mpi_reply;
	u16 enclosure_handle;
	int rc;

	/* Free existing enclosure list */
	mpt3sas_free_enclosure_list(ioc);

	/* Re constructing enclosure list after reset*/
	enclosure_handle = 0xFFFF;
	do {
		enclosure_dev =
			kzalloc(sizeof(struct _enclosure_node), GFP_KERNEL);
		if (!enclosure_dev) {
			printk(MPT3SAS_ERR_FMT
				"failure at %s:%d/%s()!\n", ioc->name,
				__FILE__, __LINE__, __func__);
			return;
		}
		rc = mpt3sas_config_get_enclosure_pg0(ioc, &mpi_reply,
				&enclosure_dev->pg0,
				MPI2_SAS_ENCLOS_PGAD_FORM_GET_NEXT_HANDLE,
				enclosure_handle);

		if (rc || (le16_to_cpu(mpi_reply.IOCStatus) &
                                                MPI2_IOCSTATUS_MASK)) {
			kfree(enclosure_dev);
			return;
		}
		list_add_tail(&enclosure_dev->list,
						&ioc->enclosure_list);
		enclosure_handle =
			le16_to_cpu(enclosure_dev->pg0.EnclosureHandle);
	} while(1);
}

/**
 * _scsih_search_responding_sas_devices -
 * @ioc: per adapter object
 *
 * After host reset, find out whether devices are still responding.
 * If not remove.
 *
 * Return nothing.
 */
static void
_scsih_search_responding_sas_devices(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 ioc_status;
	u16 handle;
	u32 device_info;

	printk(MPT3SAS_INFO_FMT "search for end-devices: start\n", ioc->name);

	if (list_empty(&ioc->sas_device_list))
		goto out;

	handle = 0xFFFF;
	while (!(mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply,
	    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE,
	    handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT3SAS_INFO_FMT "\tbreak from %s: "
			    "ioc_status(0x%04x), loginfo(0x%08x)\n",
			       ioc->name, __func__, ioc_status,
			    le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		handle = le16_to_cpu(sas_device_pg0.DevHandle);
		device_info = le32_to_cpu(sas_device_pg0.DeviceInfo);
		if (!(_scsih_is_sas_end_device(device_info)))
			continue;
		_scsih_mark_responding_sas_device(ioc, &sas_device_pg0);
	}

 out:
	printk(MPT3SAS_INFO_FMT "search for end-devices: complete\n",
	    ioc->name);
}

/**
 * _scsih_mark_responding_pcie_device - mark a pcie_device as responding
 * @ioc: per adapter object
 * @pcie_device_pg0: PCIe Device page 0
 *
 * After host reset, find out whether devices are still responding.
 * Used in _scsih_remove_unresponding_devices.
 *
 * Return nothing.
 */
static void
_scsih_mark_responding_pcie_device(struct MPT3SAS_ADAPTER *ioc,
    Mpi26PCIeDevicePage0_t *pcie_device_pg0)
{
	struct MPT3SAS_TARGET *sas_target_priv_data = NULL;
	struct scsi_target *starget;
	struct _pcie_device *pcie_device;
	unsigned long flags;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	list_for_each_entry(pcie_device, &ioc->pcie_device_list, list) {
		if ((pcie_device->wwid == le64_to_cpu(pcie_device_pg0->WWID))
		    && (pcie_device->slot == le16_to_cpu(
		    pcie_device_pg0->Slot))) {
			pcie_device->access_status = pcie_device_pg0->AccessStatus;
			pcie_device->responding = 1;
			starget = pcie_device->starget;
			if (starget && starget->hostdata) {
				sas_target_priv_data = starget->hostdata;
				sas_target_priv_data->tm_busy = 0;
				sas_target_priv_data->deleted = 0;
			} else
				sas_target_priv_data = NULL;
			if (starget) {
				starget_printk(KERN_INFO, starget,
				    "handle(0x%04x), wwid(0x%016llx) ",
				    pcie_device->handle,
				    (unsigned long long)pcie_device->wwid);
				if (pcie_device->enclosure_handle != 0)
					starget_printk(KERN_INFO, starget,
					    "enclosure logical id(0x%016llx), "
					    "slot(%d)\n",
					    (unsigned long long)
					    pcie_device->enclosure_logical_id,
					    pcie_device->slot);
			}

			if ((le32_to_cpu(pcie_device_pg0->Flags) &
			    MPI26_PCIEDEV0_FLAGS_ENCL_LEVEL_VALID) &&
			    (ioc->hba_mpi_version_belonged != MPI2_VERSION)) {
				pcie_device->enclosure_level =
				    pcie_device_pg0->EnclosureLevel;
				memcpy(pcie_device->connector_name,
				    pcie_device_pg0->ConnectorName, 4);
				pcie_device->connector_name[4] = '\0';
			} else {
				pcie_device->enclosure_level = 0;
				pcie_device->connector_name[0] = '\0';
			}

			if (pcie_device->handle == le16_to_cpu(
			    pcie_device_pg0->DevHandle))
				goto out;
			printk(KERN_INFO "\thandle changed from(0x%04x)!!!\n",
			    pcie_device->handle);
			pcie_device->handle = le16_to_cpu(
			    pcie_device_pg0->DevHandle);
			if (sas_target_priv_data)
				sas_target_priv_data->handle =
				    le16_to_cpu(pcie_device_pg0->DevHandle);
			goto out;
		}
	}

 out:
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
}

/**
 * _scsih_search_responding_pcie_devices -
 * @ioc: per adapter object
 *
 * After host reset, find out whether devices are still responding.
 * If not remove.
 *
 * Return nothing.
 */
static void
_scsih_search_responding_pcie_devices(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi26PCIeDevicePage0_t pcie_device_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 ioc_status;
	u16 handle;
	u32 device_info;

	printk(MPT3SAS_INFO_FMT "search for end-devices: start\n", ioc->name);

	if (list_empty(&ioc->pcie_device_list))
		goto out;

	handle = 0xFFFF;
	while (!(mpt3sas_config_get_pcie_device_pg0(ioc, &mpi_reply,
		&pcie_device_pg0, MPI26_PCIE_DEVICE_PGAD_FORM_GET_NEXT_HANDLE,
		handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT3SAS_INFO_FMT "\tbreak from %s: "
			    "ioc_status(0x%04x), loginfo(0x%08x)\n", ioc->name,
			    __func__, ioc_status,
			    le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		handle = le16_to_cpu(pcie_device_pg0.DevHandle);
		device_info = le32_to_cpu(pcie_device_pg0.DeviceInfo);
		if (!(_scsih_is_nvme_pciescsi_device(device_info)))
			continue;
		_scsih_mark_responding_pcie_device(ioc, &pcie_device_pg0);
	}
out:
	printk(MPT3SAS_INFO_FMT "search for PCIe end-devices: complete\n",
	    ioc->name);
}

/**
 * _scsih_mark_responding_raid_device - mark a raid_device as responding
 * @ioc: per adapter object
 * @wwid: world wide identifier for raid volume
 * @handle: device handle
 *
 * After host reset, find out whether devices are still responding.
 * Used in _scsih_remove_unresponding_devices.
 *
 * Return nothing.
 */
static void
_scsih_mark_responding_raid_device(struct MPT3SAS_ADAPTER *ioc, u64 wwid,
	u16 handle)
{
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct scsi_target *starget;
	struct _raid_device *raid_device;
	unsigned long flags;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	list_for_each_entry(raid_device, &ioc->raid_device_list, list) {
		if (raid_device->wwid == wwid && raid_device->starget) {
			starget = raid_device->starget;
			if (starget && starget->hostdata) {
				sas_target_priv_data = starget->hostdata;
				sas_target_priv_data->deleted = 0;
			} else
				sas_target_priv_data = NULL;
			raid_device->responding = 1;
			spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
			starget_printk(KERN_INFO, raid_device->starget,
			    "handle(0x%04x), wwid(0x%016llx)\n", handle,
			    (unsigned long long)raid_device->wwid);

			/*
			 * WARPDRIVE: The handles of the PDs might have changed
			 * across the host reset so re-initialize the
			 * required data for Direct IO
			 */
			if (ioc->hba_mpi_version_belonged == MPI2_VERSION)
				mpt3sas_init_warpdrive_properties(ioc, raid_device);

			spin_lock_irqsave(&ioc->raid_device_lock, flags);
			if (raid_device->handle == handle) {
				spin_unlock_irqrestore(&ioc->raid_device_lock,
				    flags);
				return;
			}
			printk(KERN_INFO "\thandle changed from(0x%04x)!!!\n",
			    raid_device->handle);
			raid_device->handle = handle;
			if (sas_target_priv_data)
				sas_target_priv_data->handle = handle;
			spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
			return;
		}
	}
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
}

/**
 * _scsih_search_responding_raid_devices -
 * @ioc: per adapter object
 *
 * After host reset, find out whether devices are still responding.
 * If not remove.
 *
 * Return nothing.
 */
static void
_scsih_search_responding_raid_devices(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2RaidVolPage1_t volume_pg1;
	Mpi2RaidVolPage0_t volume_pg0;
	Mpi2RaidPhysDiskPage0_t pd_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 ioc_status;
	u16 handle;
	u8 phys_disk_num;

	if (!ioc->ir_firmware)
		return;

	printk(MPT3SAS_INFO_FMT "search for raid volumes: start\n",
	    ioc->name);

	if (list_empty(&ioc->raid_device_list))
		goto out;

	handle = 0xFFFF;
	while (!(mpt3sas_config_get_raid_volume_pg1(ioc, &mpi_reply,
	    &volume_pg1, MPI2_RAID_VOLUME_PGAD_FORM_GET_NEXT_HANDLE, handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT3SAS_INFO_FMT "\tbreak from %s: "
			    "ioc_status(0x%04x), loginfo(0x%08x)\n",
			       ioc->name, __func__, ioc_status,
			    le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		handle = le16_to_cpu(volume_pg1.DevHandle);

		if (mpt3sas_config_get_raid_volume_pg0(ioc, &mpi_reply,
		    &volume_pg0, MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, handle,
		     sizeof(Mpi2RaidVolPage0_t)))
			continue;

		if (volume_pg0.VolumeState == MPI2_RAID_VOL_STATE_OPTIMAL ||
		    volume_pg0.VolumeState == MPI2_RAID_VOL_STATE_ONLINE ||
		    volume_pg0.VolumeState == MPI2_RAID_VOL_STATE_DEGRADED)
			_scsih_mark_responding_raid_device(ioc,
			    le64_to_cpu(volume_pg1.WWID), handle);
	}

	/* refresh the pd_handles */
	if (!ioc->is_warpdrive) {
		phys_disk_num = 0xFF;
		memset(ioc->pd_handles, 0, ioc->pd_handles_sz);
		while (!(mpt3sas_config_get_phys_disk_pg0(ioc, &mpi_reply,
		    &pd_pg0, MPI2_PHYSDISK_PGAD_FORM_GET_NEXT_PHYSDISKNUM,
		    phys_disk_num))) {
			ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
			    MPI2_IOCSTATUS_MASK;
			if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
				printk(MPT3SAS_INFO_FMT "\tbreak from %s: "
				       "ioc_status(0x%04x), loginfo(0x%08x)\n",
				       ioc->name, __func__, ioc_status,
				       le32_to_cpu(mpi_reply.IOCLogInfo));
				break;
			}
			phys_disk_num = pd_pg0.PhysDiskNum;
			handle = le16_to_cpu(pd_pg0.DevHandle);
			set_bit(handle, ioc->pd_handles);
		}
	}
 out:
	printk(MPT3SAS_INFO_FMT "search for responding raid volumes: "
	    "complete\n", ioc->name);
}

/**
 * _scsih_mark_responding_expander - mark a expander as responding
 * @ioc: per adapter object
 * @expander_pg0: SAS Expander Config Page0
 *
 * After host reset, find out whether devices are still responding.
 * Used in _scsih_remove_unresponding_devices.
 *
 * Return nothing.
 */
static void
_scsih_mark_responding_expander(struct MPT3SAS_ADAPTER *ioc,
		Mpi2ExpanderPage0_t *expander_pg0)
{
	struct _sas_node *sas_expander;
	unsigned long flags;
	int i;
	u8 port_id = expander_pg0->PhysicalPort;
	struct hba_port *port = mpt3sas_get_port_by_id(ioc, port_id, 0);
	struct _enclosure_node *enclosure_dev = NULL;
	u16 handle = le16_to_cpu(expander_pg0->DevHandle);
	u16 enclosure_handle = le16_to_cpu(expander_pg0->EnclosureHandle);
	u64 sas_address = le64_to_cpu(expander_pg0->SASAddress);

	if (enclosure_handle)
		enclosure_dev =
			mpt3sas_scsih_enclosure_find_by_handle(ioc,
							enclosure_handle);

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	list_for_each_entry(sas_expander, &ioc->sas_expander_list, list) {
		if (sas_expander->sas_address != sas_address ||
		    (sas_expander->port != port))
			continue;
		sas_expander->responding = 1;

		if (enclosure_dev) {
			sas_expander->enclosure_logical_id =
				le64_to_cpu(enclosure_dev->pg0.EnclosureLogicalID);
			sas_expander->enclosure_handle =
				le16_to_cpu(expander_pg0->EnclosureHandle);
		}

		if (sas_expander->handle == handle)
			goto out;
		printk(KERN_INFO "\texpander(0x%016llx): handle changed"
		    " from(0x%04x) to (0x%04x)!!!\n",
		    (unsigned long long)sas_expander->sas_address,
		    sas_expander->handle, handle);
		sas_expander->handle = handle;
		for (i = 0 ; i < sas_expander->num_phys ; i++)
			sas_expander->phy[i].handle = handle;
		goto out;
	}
 out:
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
}

/**
 * _scsih_search_responding_expanders -
 * @ioc: per adapter object
 *
 * After host reset, find out whether devices are still responding.
 * If not remove.
 *
 * Return nothing.
 */
static void
_scsih_search_responding_expanders(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2ExpanderPage0_t expander_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 ioc_status;
	u64 sas_address;
	u16 handle;
	u8 port;

	printk(MPT3SAS_INFO_FMT "search for expanders: start\n", ioc->name);

	if (list_empty(&ioc->sas_expander_list))
		goto out;

	handle = 0xFFFF;
	while (!(mpt3sas_config_get_expander_pg0(ioc, &mpi_reply, &expander_pg0,
	    MPI2_SAS_EXPAND_PGAD_FORM_GET_NEXT_HNDL, handle))) {

		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT3SAS_INFO_FMT "\tbreak from %s: "
			    "ioc_status(0x%04x), loginfo(0x%08x)\n",
			       ioc->name, __func__, ioc_status,
			    le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}

		handle = le16_to_cpu(expander_pg0.DevHandle);
		sas_address = le64_to_cpu(expander_pg0.SASAddress);
		port = expander_pg0.PhysicalPort;
		printk(KERN_INFO "\texpander present: handle(0x%04x), "
		    "sas_addr(0x%016llx), port:%d\n", handle,
		    (unsigned long long)sas_address,
		    ((ioc->multipath_on_hba)?
		     (port):(MULTIPATH_DISABLED_PORT_ID)));
		_scsih_mark_responding_expander(ioc, &expander_pg0);
	}

 out:
	printk(MPT3SAS_INFO_FMT "search for expanders: complete\n", ioc->name);
}

/**
 * _scsih_remove_unresponding_devices - removing unresponding devices
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_scsih_remove_unresponding_devices(struct MPT3SAS_ADAPTER *ioc)
{
	struct _sas_device *sas_device, *sas_device_next;
	struct _sas_node *sas_expander, *sas_expander_next;
	struct _raid_device *raid_device, *raid_device_next;
	struct _pcie_device *pcie_device, *pcie_device_next;
	struct list_head tmp_list;
	unsigned long flags;
	LIST_HEAD(head);


	printk(MPT3SAS_INFO_FMT "removing unresponding devices: start\n",
		ioc->name);

	/* removing unresponding end devices */
	printk(MPT3SAS_INFO_FMT "removing unresponding devices: sas end-devices\n",
		ioc->name);

	/*
	 * Iterate, pulling off devices marked as non-responding. We become the
	 * owner for the reference the list had on any object we prune.
	 */
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_for_each_entry_safe(sas_device, sas_device_next,
				 &ioc->sas_device_list, list) {
		if (!sas_device->responding)
			list_move_tail(&sas_device->list, &head);
		else
			sas_device->responding = 0;
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	/*
	 * Now, uninitialize and remove the unresponding devices we pruned.
	 */
	list_for_each_entry_safe(sas_device, sas_device_next, &head, list) {
		_scsih_remove_device(ioc, sas_device);
		list_del_init(&sas_device->list);
		sas_device_put(sas_device);
	}

	printk(MPT3SAS_INFO_FMT "removing unresponding devices: pcie end-devices\n"
		,ioc->name);
	INIT_LIST_HEAD(&head);
	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	list_for_each_entry_safe(pcie_device, pcie_device_next,
	    &ioc->pcie_device_list, list) {
		if (!pcie_device->responding)
			list_move_tail(&pcie_device->list, &head);
		else
			pcie_device->responding = 0;
	}
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	list_for_each_entry_safe(pcie_device, pcie_device_next, &head, list) {
		_scsih_pcie_device_remove_from_sml(ioc, pcie_device);
		list_del_init(&pcie_device->list);
		pcie_device_put(pcie_device);
	}

	/* removing unresponding volumes */
	if (ioc->ir_firmware) {
		printk(MPT3SAS_INFO_FMT "removing unresponding devices: "
		    "volumes\n", ioc->name);
		list_for_each_entry_safe(raid_device, raid_device_next,
		    &ioc->raid_device_list, list) {
			if (!raid_device->responding)
				_scsih_sas_volume_delete(ioc,
				    raid_device->handle);
			else
				raid_device->responding = 0;
		}
	}

	/* removing unresponding expanders */
	printk(MPT3SAS_INFO_FMT "removing unresponding devices: expanders\n",
	    ioc->name);
	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	INIT_LIST_HEAD(&tmp_list);
	list_for_each_entry_safe(sas_expander, sas_expander_next,
	    &ioc->sas_expander_list, list) {
		if (!sas_expander->responding)
			list_move_tail(&sas_expander->list, &tmp_list);
		else
			sas_expander->responding = 0;
	}
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
	list_for_each_entry_safe(sas_expander, sas_expander_next, &tmp_list,
	    list) {
		_scsih_expander_node_remove(ioc, sas_expander);
	}

	printk(MPT3SAS_INFO_FMT "removing unresponding devices: complete\n",
	    ioc->name);

	/* unblock devices */
	_scsih_ublock_io_all_device(ioc, 0);
}

static void
_scsih_refresh_expander_links(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_node *sas_expander, u16 handle)
{
	Mpi2ExpanderPage1_t expander_pg1;
	Mpi2ConfigReply_t mpi_reply;
	int i;

	for (i = 0 ; i < sas_expander->num_phys ; i++) {
		if ((mpt3sas_config_get_expander_pg1(ioc, &mpi_reply,
		    &expander_pg1, i, handle))) {
			printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			return;
		}

		mpt3sas_transport_update_links(ioc,
		    sas_expander->sas_address,
		    le16_to_cpu(expander_pg1.AttachedDevHandle), i,
		    expander_pg1.NegotiatedLinkRate >> 4,
		    sas_expander->port);
	}
}

/**
 * _scsih_scan_for_devices_after_reset - scan for devices after host reset
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_scsih_scan_for_devices_after_reset(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2ExpanderPage0_t expander_pg0;
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi26PCIeDevicePage0_t pcie_device_pg0;
	Mpi2RaidVolPage1_t volume_pg1;
	Mpi2RaidVolPage0_t volume_pg0;
	Mpi2RaidPhysDiskPage0_t pd_pg0;
	Mpi2EventIrConfigElement_t element;
	Mpi2ConfigReply_t mpi_reply;
	u8 phys_disk_num, port_id;
	u16 ioc_status;
	u16 handle, parent_handle;
	u64 sas_address;
	struct _sas_device *sas_device;
	struct _pcie_device *pcie_device;
	struct _sas_node *expander_device;
	static struct _raid_device *raid_device;
	u8 retry_count;
	unsigned long flags;

	printk(MPT3SAS_INFO_FMT "scan devices: start\n", ioc->name);

	_scsih_sas_host_refresh(ioc);

	printk(MPT3SAS_INFO_FMT "\tscan devices: expanders start\n", ioc->name);

	/* expanders */
	handle = 0xFFFF;
	while (!(mpt3sas_config_get_expander_pg0(ioc, &mpi_reply, &expander_pg0,
	    MPI2_SAS_EXPAND_PGAD_FORM_GET_NEXT_HNDL, handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT3SAS_INFO_FMT "\tbreak from expander scan: "
			    "ioc_status(0x%04x), loginfo(0x%08x)\n",
			    ioc->name, ioc_status,
			    le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		handle = le16_to_cpu(expander_pg0.DevHandle);
		spin_lock_irqsave(&ioc->sas_node_lock, flags);
		port_id = expander_pg0.PhysicalPort;
		expander_device =
		    mpt3sas_scsih_expander_find_by_sas_address(
		       ioc, le64_to_cpu(expander_pg0.SASAddress),
		       mpt3sas_get_port_by_id(ioc, port_id, 0));
		spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
		if (expander_device)
			_scsih_refresh_expander_links(ioc, expander_device,
			    handle);
		else {
			printk(MPT3SAS_INFO_FMT "\tBEFORE adding expander: "
			    "handle (0x%04x), sas_addr(0x%016llx)\n", ioc->name,
			    handle, (unsigned long long)
			    le64_to_cpu(expander_pg0.SASAddress));
			_scsih_expander_add(ioc, handle);
			printk(MPT3SAS_INFO_FMT "\tAFTER adding expander: "
			    "handle (0x%04x), sas_addr(0x%016llx)\n", ioc->name,
			    handle, (unsigned long long)
			    le64_to_cpu(expander_pg0.SASAddress));
		}
	}

	printk(MPT3SAS_INFO_FMT "\tscan devices: expanders complete\n",
	    ioc->name);

	if (!ioc->ir_firmware)
		goto skip_to_sas;

	printk(MPT3SAS_INFO_FMT "\tscan devices: phys disk start\n", ioc->name);

	/* phys disk */
	phys_disk_num = 0xFF;
	while (!(mpt3sas_config_get_phys_disk_pg0(ioc, &mpi_reply,
	    &pd_pg0, MPI2_PHYSDISK_PGAD_FORM_GET_NEXT_PHYSDISKNUM,
	    phys_disk_num))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT3SAS_INFO_FMT "\tbreak from phys disk scan: "
			    "ioc_status(0x%04x), loginfo(0x%08x)\n",
			    ioc->name, ioc_status,
			    le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		phys_disk_num = pd_pg0.PhysDiskNum;
		handle = le16_to_cpu(pd_pg0.DevHandle);
		sas_device = mpt3sas_get_sdev_by_handle(ioc, handle);
		if (sas_device) {
			sas_device_put(sas_device);
			continue;
		}
		if (mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply,
		    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_HANDLE,
		    handle) != 0)
			continue;
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT3SAS_INFO_FMT "\tbreak from phys disk scan "
			    "ioc_status(0x%04x), loginfo(0x%08x)\n",
			    ioc->name, ioc_status,
			    le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		parent_handle = le16_to_cpu(sas_device_pg0.ParentDevHandle);
		if (!_scsih_get_sas_address(ioc, parent_handle,
		    &sas_address)) {
			printk(MPT3SAS_INFO_FMT "\tBEFORE adding phys disk: "
			    " handle (0x%04x), sas_addr(0x%016llx)\n",
			    ioc->name, handle, (unsigned long long)
			    le64_to_cpu(sas_device_pg0.SASAddress));
			port_id = sas_device_pg0.PhysicalPort;
			mpt3sas_transport_update_links(ioc, sas_address,
			    handle, sas_device_pg0.PhyNum,
			    MPI2_SAS_NEG_LINK_RATE_1_5,
			    mpt3sas_get_port_by_id(ioc, port_id, 0));
			set_bit(handle, ioc->pd_handles);
			retry_count = 0;
			/* This will retry adding the end device.
			 * _scsih_add_device() will decide on retries and
			 * return "1" when it should be retried
			 */
			while(_scsih_add_device(ioc, handle, retry_count++,
			    1)) {
				ssleep(1);
			}
			printk(MPT3SAS_INFO_FMT "\tAFTER adding phys disk: "
			    " handle (0x%04x), sas_addr(0x%016llx)\n",
			    ioc->name, handle, (unsigned long long)
			    le64_to_cpu(sas_device_pg0.SASAddress));
		}
	}

	printk(MPT3SAS_INFO_FMT "\tscan devices: phys disk complete\n",
	    ioc->name);

	printk(MPT3SAS_INFO_FMT "\tscan devices: volumes start\n", ioc->name);

	/* volumes */
	handle = 0xFFFF;
	while (!(mpt3sas_config_get_raid_volume_pg1(ioc, &mpi_reply,
	    &volume_pg1, MPI2_RAID_VOLUME_PGAD_FORM_GET_NEXT_HANDLE, handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT3SAS_INFO_FMT "\tbreak from volume scan: "
			    "ioc_status(0x%04x), loginfo(0x%08x)\n",
			    ioc->name, ioc_status,
			    le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		handle = le16_to_cpu(volume_pg1.DevHandle);
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_wwid(ioc,
		    le64_to_cpu(volume_pg1.WWID));
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
		if (raid_device)
			continue;
		if (mpt3sas_config_get_raid_volume_pg0(ioc, &mpi_reply,
		    &volume_pg0, MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, handle,
		     sizeof(Mpi2RaidVolPage0_t)))
			continue;
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT3SAS_INFO_FMT "\tbreak from volume scan: "
			    "ioc_status(0x%04x), loginfo(0x%08x)\n",
			    ioc->name, ioc_status,
			    le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		if (volume_pg0.VolumeState == MPI2_RAID_VOL_STATE_OPTIMAL ||
		    volume_pg0.VolumeState == MPI2_RAID_VOL_STATE_ONLINE ||
		    volume_pg0.VolumeState == MPI2_RAID_VOL_STATE_DEGRADED) {
			memset(&element, 0, sizeof(Mpi2EventIrConfigElement_t));
			element.ReasonCode = MPI2_EVENT_IR_CHANGE_RC_ADDED;
			element.VolDevHandle = volume_pg1.DevHandle;
			printk(MPT3SAS_INFO_FMT "\tBEFORE adding volume: "
			    " handle (0x%04x)\n", ioc->name,
			    volume_pg1.DevHandle);
			_scsih_sas_volume_add(ioc, &element);
			printk(MPT3SAS_INFO_FMT "\tAFTER adding volume: "
			    " handle (0x%04x)\n", ioc->name,
			    volume_pg1.DevHandle);
		}
	}

	printk(MPT3SAS_INFO_FMT "\tscan devices: volumes complete\n",
	    ioc->name);

 skip_to_sas:

	printk(MPT3SAS_INFO_FMT "\tscan devices: sas end devices start\n",
	    ioc->name);

	/* sas devices */
	handle = 0xFFFF;
	while (!(mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply,
	    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE,
	    handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT3SAS_INFO_FMT "\tbreak from sas end device scan:"
			    " ioc_status(0x%04x), loginfo(0x%08x)\n",
			    ioc->name, ioc_status,
			    le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		handle = le16_to_cpu(sas_device_pg0.DevHandle);
		if (!(_scsih_is_sas_end_device(
		    le32_to_cpu(sas_device_pg0.DeviceInfo))))
			continue;
		port_id = sas_device_pg0.PhysicalPort;
		sas_device = mpt3sas_get_sdev_by_addr(ioc,
		    le64_to_cpu(sas_device_pg0.SASAddress),
		    mpt3sas_get_port_by_id(ioc, port_id, 0));
		if (sas_device) {
			sas_device_put(sas_device);
			continue;
		}
		parent_handle = le16_to_cpu(sas_device_pg0.ParentDevHandle);
		if (!_scsih_get_sas_address(ioc, parent_handle, &sas_address)) {
			printk(MPT3SAS_INFO_FMT "\tBEFORE adding sas end device: "
			    "handle (0x%04x), sas_addr(0x%016llx)\n", ioc->name,
			    handle, (unsigned long long)
			    le64_to_cpu(sas_device_pg0.SASAddress));
			mpt3sas_transport_update_links(ioc, sas_address,
			    handle, sas_device_pg0.PhyNum,
			    MPI2_SAS_NEG_LINK_RATE_1_5,
			    mpt3sas_get_port_by_id(ioc, port_id, 0));
			retry_count = 0;
			/* This will retry adding the end device.
			 * _scsih_add_device() will decide on retries and
			 * return "1" when it should be retried
			 */
			while(_scsih_add_device(ioc, handle, retry_count++,
			    0)) {
				ssleep(1);
			}
			printk(MPT3SAS_INFO_FMT "\tAFTER adding sas end device: "
			    "handle (0x%04x), sas_addr(0x%016llx)\n", ioc->name,
			    handle, (unsigned long long)
			    le64_to_cpu(sas_device_pg0.SASAddress));
		}
	}
	printk(MPT3SAS_INFO_FMT "\tscan devices: sas end devices complete\n",
	    ioc->name);

    printk(MPT3SAS_INFO_FMT "\tscan devices: pcie end devices start\n",
	    ioc->name);

	/* pcie devices */
	handle = 0xFFFF;
	while (!(mpt3sas_config_get_pcie_device_pg0(ioc, &mpi_reply,
		&pcie_device_pg0, MPI26_PCIE_DEVICE_PGAD_FORM_GET_NEXT_HANDLE,
		handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT3SAS_INFO_FMT "\tbreak from pcie end device scan:"
				" ioc_status(0x%04x), loginfo(0x%08x)\n", ioc->name,
				ioc_status, le32_to_cpu(mpi_reply.IOCLogInfo));
			break;
		}
		handle = le16_to_cpu(pcie_device_pg0.DevHandle);
		if (!(_scsih_is_nvme_pciescsi_device(le32_to_cpu(pcie_device_pg0.DeviceInfo))))
			continue;
		pcie_device = mpt3sas_get_pdev_by_wwid(ioc,
							le64_to_cpu(pcie_device_pg0.WWID));
		if (pcie_device) {
			pcie_device_put(pcie_device);
			continue;
		}
		retry_count = 0;
		parent_handle = le16_to_cpu(pcie_device_pg0.ParentDevHandle);
		while(_scsih_pcie_add_device(ioc, handle, retry_count++)) {
			ssleep(1);
		}
		printk(MPT3SAS_INFO_FMT "\tAFTER adding pcie end device: "
			"handle (0x%04x), wwid(0x%016llx)\n", ioc->name,
			handle, (unsigned long long) le64_to_cpu(pcie_device_pg0.WWID));
    }
	printk(MPT3SAS_INFO_FMT "\tpcie devices: pcie end devices complete\n",
		ioc->name);

	printk(MPT3SAS_INFO_FMT "scan devices: complete\n", ioc->name);
}

void
mpt3sas_scsih_clear_outstanding_scsi_tm_commands(struct MPT3SAS_ADAPTER *ioc)
{
	struct _internal_qcmd *scsih_qcmd, *scsih_qcmd_next;
	unsigned long flags;

	if (ioc->scsih_cmds.status & MPT3_CMD_PENDING) {
		ioc->scsih_cmds.status |= MPT3_CMD_RESET;
		mpt3sas_base_free_smid(ioc, ioc->scsih_cmds.smid);
		complete(&ioc->scsih_cmds.done);
	}
	if (ioc->tm_cmds.status & MPT3_CMD_PENDING) {
		ioc->tm_cmds.status |= MPT3_CMD_RESET;
		mpt3sas_base_free_smid(ioc, ioc->tm_cmds.smid);
		complete(&ioc->tm_cmds.done);
	}

	spin_lock_irqsave(&ioc->scsih_q_internal_lock, flags);
	list_for_each_entry_safe(scsih_qcmd, scsih_qcmd_next, &ioc->scsih_q_intenal_cmds, list) {
		scsih_qcmd->status |= MPT3_CMD_RESET;
		mpt3sas_base_free_smid(ioc, scsih_qcmd->smid);
	}
	spin_unlock_irqrestore(&ioc->scsih_q_internal_lock, flags);

	memset(ioc->pend_os_device_add, 0, ioc->pend_os_device_add_sz);
	memset(ioc->device_remove_in_progress, 0,
	       ioc->device_remove_in_progress_sz);
	memset(ioc->tm_tr_retry, 0, ioc->tm_tr_retry_sz);
#ifdef MPT2SAS_WD_DDIOCOUNT
	ioc->ddio_count = 0;
	ioc->ddio_err_count = 0;
#endif
	_scsih_fw_event_cleanup_queue(ioc);
	mpt3sas_scsih_flush_running_cmds(ioc);
}

/**
 * mpt3sas_scsih_reset_handler - reset callback handler (for scsih)
 * @ioc: per adapter object
 * @reset_phase: phase
 *
 * The handler for doing any required cleanup or initialization.
 *
 * The reset phase can be MPT3_IOC_PRE_RESET, MPT3_IOC_AFTER_RESET,
 * MPT3_IOC_DONE_RESET
 *
 * Return nothing.
 */
void
mpt3sas_scsih_reset_handler(struct MPT3SAS_ADAPTER *ioc, int reset_phase)
{

	switch (reset_phase) {
	case MPT3_IOC_PRE_RESET:
		dtmprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: "
		    "MPT3_IOC_PRE_RESET\n", ioc->name, __func__));
		break;
	case MPT3_IOC_AFTER_RESET:
		dtmprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: "
		    "MPT3_IOC_AFTER_RESET\n", ioc->name, __func__));
		mpt3sas_scsih_clear_outstanding_scsi_tm_commands(ioc);
		break;
	case MPT3_IOC_DONE_RESET:
		dtmprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: "
		    "MPT3_IOC_DONE_RESET\n", ioc->name, __func__));
		if ((!ioc->is_driver_loading) && !(disable_discovery > 0 &&
		    !ioc->sas_hba.num_phys)) {
			if (ioc->multipath_on_hba) {
				_scsih_sas_port_refresh(ioc);
				_scsih_update_vphys_after_reset(ioc);
			}
			_scsih_prep_device_scan(ioc);
			_scsih_create_enclosure_list_after_reset(ioc);
			_scsih_search_responding_sas_devices(ioc);
			_scsih_search_responding_pcie_devices(ioc);
			_scsih_search_responding_raid_devices(ioc);
			_scsih_search_responding_expanders(ioc);
			_scsih_error_recovery_delete_devices(ioc);
		}
		break;
	}
}

/**
 * _mpt3sas_fw_work - delayed task for processing firmware events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_mpt3sas_fw_work(struct MPT3SAS_ADAPTER *ioc, struct fw_event_work *fw_event)
{
	ioc->current_event = fw_event;
	_scsih_fw_event_del_from_list(ioc, fw_event);

	/* the queue is being flushed so ignore this event */
	if (ioc->remove_host || ioc->pci_error_recovery) {
		fw_event_work_put(fw_event);
		ioc->current_event = NULL;
		return;
	}

	switch (fw_event->event) {
	case MPT3SAS_PROCESS_TRIGGER_DIAG:
		mpt3sas_process_trigger_data(ioc, fw_event->event_data);
		break;
	case MPT3SAS_REMOVE_UNRESPONDING_DEVICES:
		while (scsi_host_in_recovery(ioc->shost) || ioc->shost_recovery)
		{
			/*
			 * If we're unloading or cancelling the work, bail.
			 * Otherwise, this can become an infinite loop.
			 */
			if (ioc->remove_host || ioc->fw_events_cleanup)
				goto out;

			ssleep(1);
		}
		_scsih_remove_unresponding_devices(ioc);
		_scsih_del_dirty_vphy(ioc);
		_scsih_del_dirty_port_entries(ioc);
		_scsih_scan_for_devices_after_reset(ioc);
		_scsih_set_nvme_max_shutdown_latency(ioc);
		if(ioc->hba_mpi_version_belonged == MPI2_VERSION)
			_scsih_hide_unhide_sas_devices(ioc);
		break;
	case MPT3SAS_PORT_ENABLE_COMPLETE:
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
		ioc->start_scan = 0;
#else
		complete(&ioc->port_enable_cmds.done);
#endif
		if (missing_delay[0] != -1 && missing_delay[1] != -1)
			mpt3sas_base_update_missing_delay(ioc, missing_delay[0],
			    missing_delay[1]);

		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT "port enable: complete "
		    "from worker thread\n", ioc->name));
		break;
	case MPT3SAS_TURN_ON_PFA_LED:
		_scsih_turn_on_pfa_led(ioc, fw_event->device_handle);
		break;
	case MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
		if (_scsih_sas_topology_change_event(ioc, fw_event)) {
			_scsih_fw_event_requeue(ioc, fw_event, 1000);
			ioc->current_event = NULL;
			return;
		}
		break;
	case MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE:
		if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
			_scsih_sas_device_status_change_event_debug(ioc,
			    (Mpi2EventDataSasDeviceStatusChange_t *)
			    fw_event->event_data);
		break;
	case MPI2_EVENT_SAS_DISCOVERY:
		_scsih_sas_discovery_event(ioc, fw_event);
		break;
	case MPI2_EVENT_SAS_DEVICE_DISCOVERY_ERROR:
		_scsih_sas_device_discovery_error_event(ioc, fw_event);
		break;
	case MPI2_EVENT_SAS_BROADCAST_PRIMITIVE:
		_scsih_sas_broadcast_primitive_event(ioc, fw_event);
		break;
	case MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE:
		_scsih_sas_enclosure_dev_status_change_event(ioc, fw_event);
		break;
	case MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST:
		_scsih_sas_ir_config_change_event(ioc, fw_event);
		break;
	case MPI2_EVENT_IR_VOLUME:
		_scsih_sas_ir_volume_event(ioc, fw_event);
		break;
	case MPI2_EVENT_IR_PHYSICAL_DISK:
		_scsih_sas_ir_physical_disk_event(ioc, fw_event);
		break;
	case MPI2_EVENT_IR_OPERATION_STATUS:
		_scsih_sas_ir_operation_status_event(ioc, fw_event);
		break;
	case MPI2_EVENT_PCIE_DEVICE_STATUS_CHANGE:
		_scsih_pcie_device_status_change_event(ioc, fw_event);
		break;
	case MPI2_EVENT_PCIE_ENUMERATION:
		_scsih_pcie_enumeration_event(ioc, fw_event);
		break;
	case MPI2_EVENT_PCIE_TOPOLOGY_CHANGE_LIST:
		if (_scsih_pcie_topology_change_event(ioc, fw_event)) {
			_scsih_fw_event_requeue(ioc, fw_event, 1000);
			ioc->current_event = NULL;
			return;
		}
	break;
	}
out:
	fw_event_work_put(fw_event);
	ioc->current_event = NULL;
}

/**
 * _firmware_event_work and _firmware_event_work_delayed
 * @ioc: per adapter object
 * @work: The fw_event_work object
 * Context: user.
 *
 * wrappers for the work thread handling firmware events
 *
 * Return nothing.
 */

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
static void
_firmware_event_work(struct work_struct *work)
{
	struct fw_event_work *fw_event = container_of(work,
	    struct fw_event_work, work);

	_mpt3sas_fw_work(fw_event->ioc, fw_event);
}
static void
_firmware_event_work_delayed(struct work_struct *work)
{
	struct fw_event_work *fw_event = container_of(work,
	    struct fw_event_work, delayed_work.work);

	_mpt3sas_fw_work(fw_event->ioc, fw_event);
}
#else
static void
_firmware_event_work(void *arg)
{
	struct fw_event_work *fw_event = (struct fw_event_work *)arg;

	_mpt3sas_fw_work(fw_event->ioc, fw_event);
}
#endif

/**
 * mpt3sas_scsih_event_callback - firmware event handler (called at ISR time)
 * @ioc: per adapter object
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: interrupt.
 *
 * This function merely adds a new work task into ioc->firmware_event_thread.
 * The tasks are worked from _firmware_event_work in user context.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
u8
mpt3sas_scsih_event_callback(struct MPT3SAS_ADAPTER *ioc, u8 msix_index,
	u32 reply)
{
	struct fw_event_work *fw_event;
	Mpi2EventNotificationReply_t *mpi_reply;
	u16 event;
	u16 sz;
	Mpi26EventDataActiveCableExcept_t *ActiveCableEventData;

	/* events turned off due to host reset*/
	if (ioc->pci_error_recovery)
		return 1;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);

	if (unlikely(!mpi_reply)) {
		printk(MPT3SAS_ERR_FMT "mpi_reply not valid at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 1;
	}

	event = le16_to_cpu(mpi_reply->Event);

	if (event != MPI2_EVENT_LOG_ENTRY_ADDED)
		mpt3sas_trigger_event(ioc, event, 0);

	switch (event) {
	/* handle these */
	case MPI2_EVENT_SAS_BROADCAST_PRIMITIVE:
	{
		Mpi2EventDataSasBroadcastPrimitive_t *baen_data =
		    (Mpi2EventDataSasBroadcastPrimitive_t *)
		    mpi_reply->EventData;

		if (baen_data->Primitive !=
		    MPI2_EVENT_PRIMITIVE_ASYNCHRONOUS_EVENT)
			return 1;

		if (ioc->broadcast_aen_busy) {
			ioc->broadcast_aen_pending++;
			return 1;
		} else
			ioc->broadcast_aen_busy = 1;
		break;
	}

	case MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
		_scsih_check_topo_delete_events(ioc,
		    (Mpi2EventDataSasTopologyChangeList_t *)
		    mpi_reply->EventData);
		break;
	case MPI2_EVENT_PCIE_TOPOLOGY_CHANGE_LIST:
	_scsih_check_pcie_topo_remove_events(ioc,
		    (Mpi26EventDataPCIeTopologyChangeList_t *)
		    mpi_reply->EventData);
		break;
	case MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST:
		_scsih_check_ir_config_unhide_events(ioc,
		    (Mpi2EventDataIrConfigChangeList_t *)
		    mpi_reply->EventData);
		break;
	case MPI2_EVENT_IR_VOLUME:
		_scsih_check_volume_delete_events(ioc,
		    (Mpi2EventDataIrVolume_t *)
		    mpi_reply->EventData);
		break;

	case MPI2_EVENT_LOG_ENTRY_ADDED:
		if(ioc->hba_mpi_version_belonged == MPI2_VERSION) {
			if (!ioc->is_warpdrive)
				break;
			_scsih_log_entry_add_event(ioc,
		    		(Mpi2EventDataLogEntryAdded_t *)
			mpi_reply->EventData);
			break;
		}
		/* fall through */
	case MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE:
		_scsih_sas_device_status_change_event(ioc,
		    (Mpi2EventDataSasDeviceStatusChange_t *)
		    mpi_reply->EventData);
		break;
	case MPI2_EVENT_IR_OPERATION_STATUS:
	case MPI2_EVENT_SAS_DISCOVERY:
	case MPI2_EVENT_SAS_DEVICE_DISCOVERY_ERROR:
	case MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE:
	case MPI2_EVENT_IR_PHYSICAL_DISK:
	case MPI2_EVENT_PCIE_ENUMERATION:
	case MPI2_EVENT_PCIE_DEVICE_STATUS_CHANGE:
		break;

	case MPI2_EVENT_TEMP_THRESHOLD:
		_scsih_temp_threshold_events(ioc,
			(Mpi2EventDataTemperature_t *)
			mpi_reply->EventData);
		break;

	case MPI2_EVENT_ACTIVE_CABLE_EXCEPTION:
		ActiveCableEventData =
		    (Mpi26EventDataActiveCableExcept_t*) mpi_reply->EventData;
		switch (ActiveCableEventData->ReasonCode) {

		case MPI26_EVENT_ACTIVE_CABLE_INSUFFICIENT_POWER:
			printk(MPT3SAS_INFO_FMT "Currently an active cable with ReceptacleID %d\n"
				,ioc->name, ActiveCableEventData->ReceptacleID);
			printk(KERN_INFO "cannot be powered and devices connected to this active cable\n");
			printk(KERN_INFO "will not be seen. This active cable\n");
			printk(KERN_INFO "requires %d mW of power\n", ActiveCableEventData->ActiveCablePowerRequirement);
			break;

		case MPI26_EVENT_ACTIVE_CABLE_DEGRADED:
			printk(MPT3SAS_INFO_FMT "Currently a cable with ReceptacleID %d" \
						" is not running at optimal speed(12 Gb/s rate)\n",
						ioc->name, ActiveCableEventData->ReceptacleID);
			break;
		}
		break;

	default: /* ignore the rest */
		return 1;
	}

	fw_event = alloc_fw_event_work(0);
	if (!fw_event) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 1;
	}
	sz = le16_to_cpu(mpi_reply->EventDataLength) * 4;
	fw_event->event_data = kzalloc(sz, GFP_ATOMIC);
	if (!fw_event->event_data) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		fw_event_work_put(fw_event);
		return 1;
	}

	if (event == MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST) {
		Mpi2EventDataSasTopologyChangeList_t *topo_event_data =
		    (Mpi2EventDataSasTopologyChangeList_t *)
		    mpi_reply->EventData;
		fw_event->retries = kzalloc(topo_event_data->NumEntries,
		    GFP_ATOMIC);
		if (!fw_event->retries) {
			printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			kfree(fw_event->event_data);
			fw_event_work_put(fw_event);
			return 1;
		}
	}

	if (event == MPI2_EVENT_PCIE_TOPOLOGY_CHANGE_LIST) {
		Mpi26EventDataPCIeTopologyChangeList_t *topo_event_data =
			(Mpi26EventDataPCIeTopologyChangeList_t *) mpi_reply->EventData;
		fw_event->retries = kzalloc(topo_event_data->NumEntries,
			GFP_ATOMIC);
		if (!fw_event->retries) {
			printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
				ioc->name, __FILE__, __LINE__, __func__);
			kfree(fw_event->event_data);
			fw_event_work_put(fw_event);
			return 1;
		}
	}

	memcpy(fw_event->event_data, mpi_reply->EventData, sz);
	fw_event->ioc = ioc;
	fw_event->VF_ID = mpi_reply->VF_ID;
	fw_event->VP_ID = mpi_reply->VP_ID;
	fw_event->event = event;
	_scsih_fw_event_add(ioc, fw_event);
	fw_event_work_put(fw_event);
	return 1;
}

/**
 * _scsih_expander_node_remove - removing expander device from list.
 * @ioc: per adapter object
 * @sas_expander: the sas_device object
 *
 * Removing object and freeing associated memory from the
 * ioc->sas_expander_list.
 *
 * Return nothing.
 */
static void
_scsih_expander_node_remove(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_node *sas_expander)
{
	struct _sas_port *mpt3sas_port, *next;
	unsigned long flags;

	/* remove sibling ports attached to this expander */
	list_for_each_entry_safe(mpt3sas_port, next,
	   &sas_expander->sas_port_list, port_list) {
		if (ioc->shost_recovery)
			return;
		if (mpt3sas_port->remote_identify.device_type ==
		    SAS_END_DEVICE)
			mpt3sas_device_remove_by_sas_address(ioc,
			    mpt3sas_port->remote_identify.sas_address,
			    mpt3sas_port->hba_port);
		else if (mpt3sas_port->remote_identify.device_type ==
		    SAS_EDGE_EXPANDER_DEVICE ||
		    mpt3sas_port->remote_identify.device_type ==
		    SAS_FANOUT_EXPANDER_DEVICE)
			mpt3sas_expander_remove(ioc,
			    mpt3sas_port->remote_identify.sas_address,
			    mpt3sas_port->hba_port);
	}

	mpt3sas_transport_port_remove(ioc, sas_expander->sas_address,
	    sas_expander->sas_address_parent, sas_expander->port);

	printk(MPT3SAS_INFO_FMT "expander_remove: handle"
	   "(0x%04x), sas_addr(0x%016llx), port:%d\n", ioc->name,
	    sas_expander->handle, (unsigned long long)
	    sas_expander->sas_address, sas_expander->port->port_id);

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	list_del(&sas_expander->list);
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);

	kfree(sas_expander->phy);
	kfree(sas_expander);
}

static void
_scsih_nvme_shutdown(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi26IoUnitControlRequest_t *mpi_request;
	Mpi26IoUnitControlReply_t *mpi_reply;
	u16 smid;

	/* are there any NVMe devices ? */
	if (list_empty(&ioc->pcie_device_list))
		return;

	mutex_lock(&ioc->scsih_cmds.mutex);

	if (ioc->scsih_cmds.status != MPT3_CMD_NOT_USED) {
		printk(MPT3SAS_ERR_FMT "%s: scsih_cmd in use\n",
		    ioc->name, __func__);
		goto out;
	}

	ioc->scsih_cmds.status = MPT3_CMD_PENDING;

	smid = mpt3sas_base_get_smid(ioc, ioc->scsih_cb_idx);
	if (!smid) {
		printk(MPT3SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
		goto out;
	}

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->scsih_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi26IoUnitControlRequest_t));
	mpi_request->Function = MPI2_FUNCTION_IO_UNIT_CONTROL;
	mpi_request->Operation = MPI26_CTRL_OP_SHUTDOWN;

	init_completion(&ioc->scsih_cmds.done);
	ioc->put_smid_default(ioc, smid);
	/* Wait for max_shutdown_latency seconds */
	printk(MPT3SAS_INFO_FMT
		"Io Unit Control shutdown (sending), Shutdown latency %d sec\n",
		ioc->name, ioc->max_shutdown_latency);
	wait_for_completion_timeout(&ioc->scsih_cmds.done,
			ioc->max_shutdown_latency*HZ);

	if (!(ioc->scsih_cmds.status & MPT3_CMD_COMPLETE)) {
		printk(MPT3SAS_ERR_FMT "%s: timeout\n",
		    ioc->name, __func__);
		goto out;
	}

	if (ioc->scsih_cmds.status & MPT3_CMD_REPLY_VALID) {
		mpi_reply = ioc->scsih_cmds.reply;
		printk(MPT3SAS_INFO_FMT "Io Unit Control shutdown (complete): "
			"ioc_status(0x%04x), loginfo(0x%08x)\n",
			ioc->name, le16_to_cpu(mpi_reply->IOCStatus),
			le32_to_cpu(mpi_reply->IOCLogInfo));
	}

 out:
	ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
	mutex_unlock(&ioc->scsih_cmds.mutex);
}

/**
 * _scsih_ir_shutdown - IR shutdown notification
 * @ioc: per adapter object
 *
 * Sending RAID Action to alert the Integrated RAID subsystem of the IOC that
 * the host system is shutting down.
 *
 * Return nothing.
 */
static void
_scsih_ir_shutdown(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2RaidActionRequest_t *mpi_request;
	Mpi2RaidActionReply_t *mpi_reply;
	u16 smid;

	/* is IR firmware build loaded ? */
	if (!ioc->ir_firmware)
		return;

	/* are there any volumes ? */
	if (list_empty(&ioc->raid_device_list))
		return;

	if (mpt3sas_base_pci_device_is_unplugged(ioc))
		return;

	mutex_lock(&ioc->scsih_cmds.mutex);

	if (ioc->scsih_cmds.status != MPT3_CMD_NOT_USED) {
		printk(MPT3SAS_ERR_FMT "%s: scsih_cmd in use\n",
		    ioc->name, __func__);
		goto out;
	}
	ioc->scsih_cmds.status = MPT3_CMD_PENDING;

	smid = mpt3sas_base_get_smid(ioc, ioc->scsih_cb_idx);
	if (!smid) {
		printk(MPT3SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
		goto out;
	}

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->scsih_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2RaidActionRequest_t));

	mpi_request->Function = MPI2_FUNCTION_RAID_ACTION;
	mpi_request->Action = MPI2_RAID_ACTION_SYSTEM_SHUTDOWN_INITIATED;

	if (!ioc->warpdrive_msg)
		printk(MPT3SAS_INFO_FMT "IR shutdown (sending)\n", ioc->name);
	init_completion(&ioc->scsih_cmds.done);
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->scsih_cmds.done, 10*HZ);

	if (!(ioc->scsih_cmds.status & MPT3_CMD_COMPLETE)) {
		printk(MPT3SAS_ERR_FMT "%s: timeout\n",
		    ioc->name, __func__);
		goto out;
	}

	if (ioc->scsih_cmds.status & MPT3_CMD_REPLY_VALID) {
		mpi_reply = ioc->scsih_cmds.reply;
		if (!ioc->warpdrive_msg)
			printk(MPT3SAS_INFO_FMT "IR shutdown (complete): "
			    "ioc_status(0x%04x), loginfo(0x%08x)\n",
			    ioc->name, le16_to_cpu(mpi_reply->IOCStatus),
			    le32_to_cpu(mpi_reply->IOCLogInfo));
	}

 out:
	ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
	mutex_unlock(&ioc->scsih_cmds.mutex);
}

/**
 * _scsih_get_shost_and_ioc - get shost and ioc
 *			and verify whether they are NULL or not
 * @pdev: PCI device struct
 * @shost: address of scsi host pointer
 * @ioc: address of HBA adapter pointer
 *
 * Return zero if *shost and *ioc are not NULL otherwise return error number.
 */
static int
_scsih_get_shost_and_ioc(struct pci_dev *pdev,
	struct Scsi_Host **shost, struct MPT3SAS_ADAPTER **ioc)
{
	*shost = pci_get_drvdata(pdev);
	if (*shost == NULL) {
		dev_err(&pdev->dev,"pdev's driver data is null\n");
		return -ENXIO;
	}

	*ioc = shost_private(*shost);
	if (*ioc == NULL) {
		dev_err(&pdev->dev,"shost's private data is null\n");
		return -ENXIO;
	}

	return 0;
}


/**
 * scsih_remove - detach and remove add host
 * @pdev: PCI device struct
 *
 * Routine called when unloading the driver.
 * Return nothing.
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0))
static void
#else
static void __devexit
#endif
scsih_remove(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = NULL;
	struct MPT3SAS_ADAPTER *ioc = NULL;
	struct _sas_port *mpt3sas_port, *next_port;
	struct _raid_device *raid_device, *next;
	struct MPT3SAS_TARGET *sas_target_priv_data;
	struct _pcie_device *pcie_device, *pcienext;
	struct workqueue_struct	*wq;
	unsigned long flags;
	struct hba_port *port, *port_next;
	struct virtual_phy *vphy, *vphy_next;
	Mpi2ConfigReply_t mpi_reply;

	if (_scsih_get_shost_and_ioc(pdev, &shost, &ioc)) {
		dev_err(&pdev->dev, "unable to remove device\n");
		return;
	}

/* Kashyap - don't port this to upstream
 * This is a fix for CQ 186643
 *
 * The reason for this fix is becuase Fast Load CQ 176830
 * This is due to Asnyc Scanning implementation.
 * This allows the driver to load before the initialization has completed.
 * Under Red Hat, the rmmod is not doing reference count checking, so it
 * will allow driver unload even though async scanning is still active.
 * This issue doesn't occur with SuSE.
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
	while (ioc->is_driver_loading)
		ssleep(1);
#endif
	ioc->remove_host = 1;

	mpt3sas_wait_for_commands_to_complete(ioc);
	if ((ioc->hba_mpi_version_belonged != MPI2_VERSION) &&
		(sata_smart_polling))
		mpt3sas_base_stop_smart_polling(ioc);

	spin_lock_irqsave(&ioc->hba_hot_unplug_lock, flags);
	if (mpt3sas_base_pci_device_is_unplugged(ioc))
		mpt3sas_scsih_flush_running_cmds(ioc);
	_scsih_fw_event_cleanup_queue(ioc);
	spin_unlock_irqrestore(&ioc->hba_hot_unplug_lock, flags);

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	wq = ioc->firmware_event_thread;
	ioc->firmware_event_thread = NULL;
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
	if (wq)
		destroy_workqueue(wq);

	/*
	 * Copy back the unmodified ioc page1. so that on next driver load,
	 * current modified changes on ioc page1 won't take effect.
	 */
	if (ioc->is_aero_ioc)
		mpt3sas_config_set_ioc_pg1(ioc, &mpi_reply,
		    &ioc->ioc_pg1_copy);

	/* release all the volumes */
	_scsih_ir_shutdown(ioc);

	mpt3sas_destroy_debugfs(ioc);
	sas_remove_host(shost);
	scsi_remove_host(shost);

	list_for_each_entry_safe(raid_device, next, &ioc->raid_device_list,
	    list) {
		if (raid_device->starget) {
			sas_target_priv_data =
			    raid_device->starget->hostdata;
			sas_target_priv_data->deleted = 1;
			scsi_remove_target(&raid_device->starget->dev);
		}
		printk(MPT3SAS_INFO_FMT "removing handle(0x%04x), wwid"
		    "(0x%016llx)\n", ioc->name,  raid_device->handle,
		    (unsigned long long) raid_device->wwid);
		_scsih_raid_device_remove(ioc, raid_device);
	}
	list_for_each_entry_safe(pcie_device, pcienext, &ioc->pcie_device_list,
		list) {
		_scsih_pcie_device_remove_from_sml(ioc, pcie_device);
		list_del_init(&pcie_device->list);
		pcie_device_put(pcie_device);
	}

	/* free ports attached to the sas_host */
	list_for_each_entry_safe(mpt3sas_port, next_port,
	   &ioc->sas_hba.sas_port_list, port_list) {
		if (mpt3sas_port->remote_identify.device_type ==
		    SAS_END_DEVICE)
			mpt3sas_device_remove_by_sas_address(ioc,
			    mpt3sas_port->remote_identify.sas_address,
			    mpt3sas_port->hba_port);
		else if (mpt3sas_port->remote_identify.device_type ==
		    SAS_EDGE_EXPANDER_DEVICE ||
		    mpt3sas_port->remote_identify.device_type ==
		    SAS_FANOUT_EXPANDER_DEVICE)
			mpt3sas_expander_remove(ioc,
			    mpt3sas_port->remote_identify.sas_address,
			    mpt3sas_port->hba_port);
	}

	list_for_each_entry_safe(port,
	    port_next, &ioc->port_table_list, list) {
		if (port->vphys_mask) {
			list_for_each_entry_safe(vphy, vphy_next,
			    &port->vphys_list, list) {
				list_del(&vphy->list);
				kfree(vphy);
			}
		}
		list_del(&port->list);
		kfree(port);
	}

	/* free phys attached to the sas_host */
	if (ioc->sas_hba.num_phys) {
		kfree(ioc->sas_hba.phy);
		ioc->sas_hba.phy = NULL;
		ioc->sas_hba.num_phys = 0;
	}

	mpt3sas_base_detach(ioc);
	spin_lock(&gioc_lock);
	list_del(&ioc->list);
	spin_unlock(&gioc_lock);
	scsi_host_put(shost);
}

/**
 * scsih_shutdown - routine call during system shutdown
 * @pdev: PCI device struct
 *
 * Return nothing.
 */
static void
scsih_shutdown(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = NULL;
	struct MPT3SAS_ADAPTER *ioc = NULL;
	struct workqueue_struct	*wq;
	unsigned long flags;
	Mpi2ConfigReply_t mpi_reply;

	if (_scsih_get_shost_and_ioc(pdev, &shost, &ioc)) {
		dev_err(&pdev->dev, "unable to shutdown device\n");
		return;
	}

	ioc->remove_host = 1;

	mpt3sas_wait_for_commands_to_complete(ioc);

	_scsih_fw_event_cleanup_queue(ioc);

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	wq = ioc->firmware_event_thread;
	ioc->firmware_event_thread = NULL;
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
	if (wq)
		destroy_workqueue(wq);

	/*
	 * Copy back the unmodified ioc page1. so that on next driver load,
	 * current modified changes on ioc page1 won't take effect.
	 */
	if (ioc->is_aero_ioc)
		mpt3sas_config_set_ioc_pg1(ioc, &mpi_reply,
		    &ioc->ioc_pg1_copy);

	_scsih_ir_shutdown(ioc);
	_scsih_nvme_shutdown(ioc);
	mpt3sas_base_mask_interrupts(ioc);
	ioc->shost_recovery = 1;
	mpt3sas_base_make_ioc_ready(ioc, SOFT_RESET);
	ioc->shost_recovery = 0;
	mpt3sas_base_free_irq(ioc);
	mpt3sas_base_disable_msix(ioc);
}

/**
 * _scsih_probe_boot_devices - reports 1st device
 * @ioc: per adapter object
 *
 * If specified in bios page 2, this routine reports the 1st
 * device scsi-ml or sas transport for persistent boot device
 * purposes.  Please refer to function _scsih_determine_boot_device()
 */
static void
_scsih_probe_boot_devices(struct MPT3SAS_ADAPTER *ioc)
{
	u32 channel;
	void *device;
	struct _sas_device *sas_device;
	struct _raid_device *raid_device;
	struct _pcie_device *pcie_device;
	u16 handle;
	u64 sas_address_parent;
	u64 sas_address;
	unsigned long flags;
	int rc;
	int tid;
	struct hba_port *port;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
	u8 protection_mask;
#endif

	 /* no Bios, return immediately */
	if (!ioc->bios_pg3.BiosVersion)
		return;

	device = NULL;
	if (ioc->req_boot_device.device) {
		device =  ioc->req_boot_device.device;
		channel = ioc->req_boot_device.channel;
	} else if (ioc->req_alt_boot_device.device) {
		device =  ioc->req_alt_boot_device.device;
		channel = ioc->req_alt_boot_device.channel;
	} else if (ioc->current_boot_device.device) {
		device =  ioc->current_boot_device.device;
		channel = ioc->current_boot_device.channel;
	}

	if (!device)
		return;

	if (channel == RAID_CHANNEL) {
		raid_device = device;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
		if ((!ioc->disable_eedp_support) &&
			(ioc->hba_mpi_version_belonged != MPI2_VERSION)) {
		/* Disable DIX0 protection capability */
		protection_mask = scsi_host_get_prot(ioc->shost);
			if (protection_mask & SHOST_DIX_TYPE0_PROTECTION) {
				scsi_host_set_prot(ioc->shost, protection_mask & 0x77);
				printk(MPT3SAS_INFO_FMT ": Disabling DIX0 prot capability "
					"because HBA does not support DIX0 operation on volumes\n",
					ioc->name);
			}
		}
#endif
		rc = scsi_add_device(ioc->shost, RAID_CHANNEL,
		    raid_device->id, 0);
		if (rc)
			_scsih_raid_device_remove(ioc, raid_device);
	} else if (channel == PCIE_CHANNEL) {
		spin_lock_irqsave(&ioc->pcie_device_lock, flags);
		pcie_device = device;
		tid = pcie_device->id;
		list_move_tail(&pcie_device->list, &ioc->pcie_device_list);
		spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
		rc = scsi_add_device(ioc->shost, PCIE_CHANNEL, tid, 0);
		if (rc)
			_scsih_pcie_device_remove(ioc, pcie_device);
		else if (!pcie_device->starget) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
			/* CQ 206770:
			 * When asyn scanning is enabled, its not possible to
			 * remove devices while scanning is turned on due to an
			 * oops in scsi_sysfs_add_sdev()->add_device()
			 * ->sysfs_addrm_start()
			 */
			if (!ioc->is_driver_loading) {
#endif
				/*TODO-- Need to find out whether this condition will occur
				  or not*/
				//_scsih_pcie_device_remove(ioc, pcie_device);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
			}
#endif
		}
	} else {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = device;
		handle = sas_device->handle;
		sas_address_parent = sas_device->sas_address_parent;
		sas_address = sas_device->sas_address;
		port = sas_device->port;
		list_move_tail(&sas_device->list, &ioc->sas_device_list);
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

		if (!port)
			return;

		if (ioc->hide_drives)
			return;

		if (!mpt3sas_transport_port_add(ioc, handle,
		     sas_address_parent, port)) {
			_scsih_sas_device_remove(ioc, sas_device);
		} else if (!sas_device->starget) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
			/* CQ 206770:
			 * When asyn scanning is enabled, its not possible to
			 * remove devices while scanning is turned on due to an
			 * oops in scsi_sysfs_add_sdev()->add_device()
			 * ->sysfs_addrm_start()
			 */
			if (!ioc->is_driver_loading) {
#endif
				mpt3sas_transport_port_remove(ioc,
				    sas_address, sas_address_parent, port);
				_scsih_sas_device_remove(ioc, sas_device);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
			}
#endif
		}
	}
}

/**
 * _scsih_probe_raid - reporting raid volumes to scsi-ml
 * @ioc: per adapter object
 *
 * Called during initial loading of the driver.
 */
static void
_scsih_probe_raid(struct MPT3SAS_ADAPTER *ioc)
{
	struct _raid_device *raid_device, *raid_next;
	int rc;

	list_for_each_entry_safe(raid_device, raid_next,
	    &ioc->raid_device_list, list) {
		if (raid_device->starget)
			continue;
		rc = scsi_add_device(ioc->shost, RAID_CHANNEL,
		    raid_device->id, 0);
		if (rc)
			_scsih_raid_device_remove(ioc, raid_device);
	}
}

/**
 * get_next_sas_device - Get the next sas device
 * @ioc: per adapter object
 *
 * Get the next sas device from sas_device_init_list list.
 *
 * Returns sas device structure if sas_device_init_list list is
 * not empty otherwise returns NULL
 */
static struct _sas_device *get_next_sas_device(struct MPT3SAS_ADAPTER *ioc)
{
	struct _sas_device *sas_device = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	if (!list_empty(&ioc->sas_device_init_list)) {
		sas_device = list_first_entry(&ioc->sas_device_init_list,
				struct _sas_device, list);
		sas_device_get(sas_device);
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	return sas_device;
}

/**
 * sas_device_make_active - Add sas device to sas_device_list list
 * @ioc: per adapter object
 * @sas_device: sas device object
 *
 * Add the sas device which has registered with SCSI Transport Later
 * to sas_device_list list
 */
static void sas_device_make_active(struct MPT3SAS_ADAPTER *ioc,
		struct _sas_device *sas_device)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);

	/*
	 * Since we dropped the lock during the call to port_add(), we need to
	 * be careful here that somebody else didn't move or delete this item
	 * while we were busy with other things.
	 *
	 * If it was on the list, we need a put() for the reference the list
	 * had. Either way, we need a get() for the destination list.
	 */
	if (!list_empty(&sas_device->list)) {
		list_del_init(&sas_device->list);
		sas_device_put(sas_device);
	}

	sas_device_get(sas_device);
	list_add_tail(&sas_device->list, &ioc->sas_device_list);

	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
}

/**
 * _scsih_probe_sas - reporting sas devices to sas transport
 * @ioc: per adapter object
 *
 * Called during initial loading of the driver.
 */
static void
_scsih_probe_sas(struct MPT3SAS_ADAPTER *ioc)
{
	struct _sas_device *sas_device;

	while ((sas_device = get_next_sas_device(ioc))) {
		if (ioc->hide_drives) {
			sas_device_make_active(ioc, sas_device);
			sas_device_put(sas_device);
			continue;
		}

		if (!mpt3sas_transport_port_add(ioc, sas_device->handle,
		    sas_device->sas_address_parent,
		    sas_device->port)) {
			_scsih_sas_device_remove(ioc, sas_device);
			sas_device_put(sas_device);
			continue;
		} else if (!sas_device->starget) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
			/* CQ 206770:
			 * When asyn scanning is enabled, its not possible to
			 * remove devices while scanning is turned on due to an
			 * oops in scsi_sysfs_add_sdev()->add_device()->
			 * sysfs_addrm_start()
			 */
			if (!ioc->is_driver_loading) {
#endif
				mpt3sas_transport_port_remove(ioc,
				    sas_device->sas_address,
				    sas_device->sas_address_parent,
				    sas_device->port);
				_scsih_sas_device_remove(ioc, sas_device);
				sas_device_put(sas_device);
				continue;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
			}
#endif
		}

		sas_device_make_active(ioc, sas_device);
		sas_device_put(sas_device);
	}
}

/**
 * get_next_pcie_device - Get the next pcie device
 * @ioc: per adapter object
 *
 * Get the next pcie device from pcie_device_init_list list.
 *
 * Returns pcie device structure if pcie_device_init_list list is not empty
 * otherwise returns NULL
 */
static struct _pcie_device *get_next_pcie_device(struct MPT3SAS_ADAPTER *ioc)
{
	struct _pcie_device *pcie_device = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);
	if (!list_empty(&ioc->pcie_device_init_list)) {
		pcie_device = list_first_entry(&ioc->pcie_device_init_list,
				struct _pcie_device, list);
		pcie_device_get(pcie_device);
	}
	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);

	return pcie_device;
}

/**
 * pcie_device_make_active - Add pcie device to pcie_device_list list
 * @ioc: per adapter object
 * @pcie_device: pcie device object
 *
 * Add the pcie device which has registered with SCSI Transport Later to
 * pcie_device_list list
 */
static void pcie_device_make_active(struct MPT3SAS_ADAPTER *ioc,
		struct _pcie_device *pcie_device)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->pcie_device_lock, flags);

	if (!list_empty(&pcie_device->list)) {
		list_del_init(&pcie_device->list);
		pcie_device_put(pcie_device);
	}
	pcie_device_get(pcie_device);
	list_add_tail(&pcie_device->list, &ioc->pcie_device_list);

	spin_unlock_irqrestore(&ioc->pcie_device_lock, flags);
}

/**
* _scsih_probe_pcie - reporting PCIe devices to scsi-ml
* @ioc: per adapter object
*
* Called during initial loading of the driver.
*/
static void
_scsih_probe_pcie(struct MPT3SAS_ADAPTER *ioc)
{
	int rc;
	struct _pcie_device *pcie_device;
	
	while ((pcie_device = get_next_pcie_device(ioc))) {
		if (pcie_device->starget) {
			pcie_device_put(pcie_device);
			continue;
		}
		if (pcie_device->access_status ==
				MPI26_PCIEDEV0_ASTATUS_DEVICE_BLOCKED) {
			pcie_device_make_active(ioc, pcie_device);
			pcie_device_put(pcie_device);
			continue;
		}
		rc = scsi_add_device(ioc->shost, PCIE_CHANNEL,
				pcie_device->id, 0);
		if (rc) {
			_scsih_pcie_device_remove(ioc, pcie_device);
			pcie_device_put(pcie_device);
			continue;
		} else if (!pcie_device->starget) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
			/* CQ 206770:
			 * When asyn scanning is enabled, its not possible to
			 * remove devices while scanning is turned on due to an
			 * oops in scsi_sysfs_add_sdev()->add_device()->
			 * sysfs_addrm_start()
			 */
			if (!ioc->is_driver_loading) {
#endif
				/*TODO-- Need to find out whether this condition will occur
				  or not*/
				_scsih_pcie_device_remove(ioc, pcie_device);
				pcie_device_put(pcie_device);
				continue;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
			}
#endif
		}
		pcie_device_make_active(ioc, pcie_device);
		pcie_device_put(pcie_device);
	}
}

/**
 * _scsih_probe_devices - probing for devices
 * @ioc: per adapter object
 *
 * Called during initial loading of the driver.
 */
static void
_scsih_probe_devices(struct MPT3SAS_ADAPTER *ioc)
{
	u16 volume_mapping_flags;

	if (!(ioc->facts.ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_SCSI_INITIATOR))
		return;  /* return when IOC doesn't support initiator mode */

	_scsih_probe_boot_devices(ioc);

	if (ioc->ir_firmware) {
		volume_mapping_flags =
		    le16_to_cpu(ioc->ioc_pg8.IRVolumeMappingFlags) &
		    MPI2_IOCPAGE8_IRFLAGS_MASK_VOLUME_MAPPING_MODE;
		if (volume_mapping_flags ==
		    MPI2_IOCPAGE8_IRFLAGS_LOW_VOLUME_MAPPING) {
			_scsih_probe_raid(ioc);
			_scsih_probe_sas(ioc);
		} else {
			_scsih_probe_sas(ioc);
			_scsih_probe_raid(ioc);
		}
	} else {
		_scsih_probe_sas(ioc);
		_scsih_probe_pcie(ioc);
	}
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
/**
 * _scsih_scan_start - scsi lld callback for .scan_start
 * @shost: SCSI host pointer
 *
 * The shost has the ability to discover targets on its own instead
 * of scanning the entire bus.  In our implemention, we will kick off
 * firmware discovery.
 */
static void
_scsih_scan_start(struct Scsi_Host *shost)
{
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);
	int rc;

	if (ioc->is_warpdrive)
		mpt3sas_enable_diag_buffer(ioc, 1);
	else if (diag_buffer_enable != -1 && diag_buffer_enable != 0)
		mpt3sas_enable_diag_buffer(ioc, diag_buffer_enable);
	else if (ioc->manu_pg11.HostTraceBufferMaxSizeKB != 0)
		mpt3sas_enable_diag_buffer(ioc, 1);

	if (disable_discovery > 0)
		return;

	ioc->start_scan = 1;
	rc = mpt3sas_port_enable(ioc);

	if (rc != 0)
		printk(MPT3SAS_INFO_FMT "port enable: FAILED\n", ioc->name);
}

/**
 * _scsih_scan_finished - scsi lld callback for .scan_finished
 * @shost: SCSI host pointer
 * @time: elapsed time of the scan in jiffies
 *
 * This function will be called periodicallyn until it returns 1 with the
 * scsi_host and the elapsed time of the scan in jiffies. In our implemention,
 * we wait for firmware discovery to complete, then return 1.
 */
static int
_scsih_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	struct MPT3SAS_ADAPTER *ioc = shost_priv(shost);

	if (disable_discovery > 0) {
		ioc->is_driver_loading = 0;
		ioc->wait_for_discovery_to_complete = 0;
		goto out;
	}

	if (time >= (300 * HZ)) {
		ioc->port_enable_cmds.status = MPT3_CMD_NOT_USED;
		printk(MPT3SAS_INFO_FMT "port enable: FAILED with timeout "
		    "(timeout=300s)\n", ioc->name);
		ioc->is_driver_loading = 0;
		goto out;
	}

	if (ioc->start_scan)
		return 0;

	if (ioc->start_scan_failed) {
		printk(MPT3SAS_INFO_FMT "port enable: FAILED with "
		    "(ioc_status=0x%08x)\n", ioc->name, ioc->start_scan_failed);
		ioc->is_driver_loading = 0;
		ioc->wait_for_discovery_to_complete = 0;
		ioc->remove_host = 1;
		goto out;
	}

	printk(MPT3SAS_INFO_FMT "port enable: SUCCESS\n", ioc->name);
	ioc->port_enable_cmds.status = MPT3_CMD_NOT_USED;

	if (ioc->wait_for_discovery_to_complete) {
		ioc->wait_for_discovery_to_complete = 0;
		_scsih_probe_devices(ioc);
	}
	mpt3sas_base_start_watchdog(ioc);
	ioc->is_driver_loading = 0;
	if ((ioc->hba_mpi_version_belonged != MPI2_VERSION)
		&& (sata_smart_polling))
			mpt3sas_base_start_smart_polling(ioc);
out:
	return 1;
}
#endif

/* shost template for SAS 2.0 HBA devices */
static struct scsi_host_template mpt2sas_driver_template = {
	.module                         = THIS_MODULE,
	.name                           = "Fusion MPT SAS Host",
	.proc_name                      = MPT2SAS_DRIVER_NAME,
	.queuecommand                   = scsih_qcmd,
	.target_alloc                   = _scsih_target_alloc,
	.slave_alloc                    = _scsih_slave_alloc,
	.slave_configure                = _scsih_slave_configure,
	.target_destroy                 = _scsih_target_destroy,
	.slave_destroy                  = _scsih_slave_destroy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
	.scan_finished                  = _scsih_scan_finished,
	.scan_start                     = _scsih_scan_start,
#endif
	.change_queue_depth             = _scsih_change_queue_depth,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0))
	.change_queue_type              = _scsih_change_queue_type,
#endif
	.eh_abort_handler               = _scsih_abort,
	.eh_device_reset_handler        = _scsih_dev_reset,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26))
	.eh_target_reset_handler        = _scsih_target_reset,
#endif
	.eh_host_reset_handler          = _scsih_host_reset,
	.bios_param                     = _scsih_bios_param,
	.can_queue                      = 1,
	.this_id                        = -1,
	.sg_tablesize                   = MPT2SAS_SG_DEPTH,
	.max_sectors                    = 32767,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
	.max_segment_size		= 0xffffffff,
#endif
	.cmd_per_lun                    = 7,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0))
	.use_clustering                 = ENABLE_CLUSTERING,
#endif
	.shost_attrs                    = mpt3sas_host_attrs,
	.sdev_attrs                     = mpt3sas_dev_attrs,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0))
	.track_queue_depth              = 1,
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0))
	.cmd_size           = sizeof(struct scsiio_tracker),
#endif
};

/* raid transport support for SAS 2.0 HBA devices */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
static struct raid_function_template mpt2sas_raid_functions = {
	.cookie         = &mpt2sas_driver_template,
	.is_raid        = _scsih_is_raid,
	.get_resync     = _scsih_get_resync,
	.get_state      = _scsih_get_state,
};
#endif

/* shost template for SAS 3.0 HBA devices */
static struct scsi_host_template mpt3sas_driver_template = {
	.module                         = THIS_MODULE,
	.name                           = "Fusion MPT SAS Host",
	.proc_name                      = MPT3SAS_DRIVER_NAME,
	.queuecommand                   = scsih_qcmd,
	.target_alloc                   = _scsih_target_alloc,
	.slave_alloc                    = _scsih_slave_alloc,
	.slave_configure                = _scsih_slave_configure,
	.target_destroy                 = _scsih_target_destroy,
	.slave_destroy                  = _scsih_slave_destroy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
	.scan_finished                  = _scsih_scan_finished,
	.scan_start                     = _scsih_scan_start,
#endif
	.change_queue_depth             = _scsih_change_queue_depth,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,19,0))
	.change_queue_type              = _scsih_change_queue_type,
#endif
	.eh_abort_handler               = _scsih_abort,
	.eh_device_reset_handler        = _scsih_dev_reset,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26))
        .eh_target_reset_handler        = _scsih_target_reset,
#endif
	.eh_host_reset_handler          = _scsih_host_reset,
	.bios_param                     = _scsih_bios_param,
	.can_queue                      = 1,
	.this_id                        = -1,
	.sg_tablesize                   = MPT3SAS_SG_DEPTH,
	.max_sectors                    = 32767,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
	.max_segment_size		= 0xffffffff,
#endif
	.cmd_per_lun                    = 7,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0))
	.use_clustering                 = ENABLE_CLUSTERING,
#endif
	.shost_attrs                    = mpt3sas_host_attrs,
	.sdev_attrs                     = mpt3sas_dev_attrs,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0))
	.track_queue_depth              = 1,
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0))
	.cmd_size           = sizeof(struct scsiio_tracker),
#endif
};

/* raid transport support for SAS 3.0 HBA devices */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
 static struct raid_function_template mpt3sas_raid_functions = {
	.cookie         = &mpt3sas_driver_template,
	.is_raid        = _scsih_is_raid,
	.get_resync     = _scsih_get_resync,
	.get_state      = _scsih_get_state,
};
#endif

/**
 * _scsih_determine_hba_mpi_version - determine in which MPI version class
 *                                     this device belongs to.
 * @pdev: PCI device struct
 *
 * return MPI2_VERSION for SAS 2.0 HBA devices,
 *     MPI25_VERSION for SAS 3.0 HBA devices.
 */

static u16
_scsih_determine_hba_mpi_version(struct pci_dev *pdev) {

	switch (pdev->device) {
	case MPI2_MFGPAGE_DEVID_SSS6200:
	case MPI2_MFGPAGE_DEVID_SAS2004:
	case MPI2_MFGPAGE_DEVID_SAS2008:
	case MPI2_MFGPAGE_DEVID_SAS2108_1:
	case MPI2_MFGPAGE_DEVID_SAS2108_2:
	case MPI2_MFGPAGE_DEVID_SAS2108_3:
	case MPI2_MFGPAGE_DEVID_SAS2116_1:
	case MPI2_MFGPAGE_DEVID_SAS2116_2:
	case MPI2_MFGPAGE_DEVID_SAS2208_1:
	case MPI2_MFGPAGE_DEVID_SAS2208_2:
	case MPI2_MFGPAGE_DEVID_SAS2208_3:
	case MPI2_MFGPAGE_DEVID_SAS2208_4:
	case MPI2_MFGPAGE_DEVID_SAS2208_5:
	case MPI2_MFGPAGE_DEVID_SAS2208_6:
	case MPI2_MFGPAGE_DEVID_SAS2308_1:
	case MPI2_MFGPAGE_DEVID_SAS2308_2:
	case MPI2_MFGPAGE_DEVID_SAS2308_3:
	case MPI26_MFGPAGE_DEVID_SWCH_MPI_EP:
	case MPI26_MFGPAGE_DEVID_SWCH_MPI_EP_1:
		return MPI2_VERSION;
	case MPI25_MFGPAGE_DEVID_SAS3004:
	case MPI25_MFGPAGE_DEVID_SAS3008:
	case MPI25_MFGPAGE_DEVID_SAS3108_1:
	case MPI25_MFGPAGE_DEVID_SAS3108_2:
	case MPI25_MFGPAGE_DEVID_SAS3108_5:
	case MPI25_MFGPAGE_DEVID_SAS3108_6:
		return MPI25_VERSION;
	case MPI26_MFGPAGE_DEVID_SAS3216:
	case MPI26_MFGPAGE_DEVID_SAS3224:
	case MPI26_MFGPAGE_DEVID_SAS3316_1:
	case MPI26_MFGPAGE_DEVID_SAS3316_2:
	case MPI26_MFGPAGE_DEVID_SAS3316_3:
	case MPI26_MFGPAGE_DEVID_SAS3316_4:
	case MPI26_MFGPAGE_DEVID_SAS3324_1:
	case MPI26_MFGPAGE_DEVID_SAS3324_2:
	case MPI26_MFGPAGE_DEVID_SAS3324_3:
	case MPI26_MFGPAGE_DEVID_SAS3324_4:
	case MPI26_MFGPAGE_DEVID_SAS3508_1:
	case MPI26_MFGPAGE_DEVID_SAS3408:
	case MPI26_MFGPAGE_DEVID_SAS3516_1:
	case MPI26_MFGPAGE_DEVID_SAS3416:
	case MPI26_MFGPAGE_DEVID_SAS3716:
	case MPI26_MFGPAGE_DEVID_SAS3616:
	case MPI26_MFGPAGE_DEVID_PEX88000:
	case MPI26_MFGPAGE_DEVID_INVALID0_3816:
	case MPI26_MFGPAGE_DEVID_CFG_SEC_3816:
	case MPI26_MFGPAGE_DEVID_HARD_SEC_3816:
	case MPI26_MFGPAGE_DEVID_INVALID1_3816:
	case MPI26_MFGPAGE_DEVID_INVALID0_3916:
	case MPI26_MFGPAGE_DEVID_CFG_SEC_3916:
	case MPI26_MFGPAGE_DEVID_HARD_SEC_3916:
	case MPI26_MFGPAGE_DEVID_INVALID1_3916:
		return MPI26_VERSION;
	}
	return 0;

}

/**
 * _scsih_probe - attach and add scsi host
 * @pdev: PCI device struct
 * @id: pci device id
 *
 * Returns 0 success, anything else error.
 */
static int
_scsih_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct MPT3SAS_ADAPTER *ioc;
	struct Scsi_Host *shost = NULL;
	int rv;
	u16 hba_mpi_version;
	u8 revision;
	int enumerate_hba = 0;

	/* Determine in which MPI version class this pci device belongs */
	hba_mpi_version = _scsih_determine_hba_mpi_version(pdev);

	if (hba_mpi_version == 0)
		return -ENODEV;

	if (hbas_to_enumerate == -1) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0))
		enumerate_hba = 0;
#else
		enumerate_hba = 2;
#endif
	}
	else
		enumerate_hba = hbas_to_enumerate;

	/* Enumerate only SAS 2.0 HBA's if hbas_to_enumerate is one,
	 * for other generation HBA's return with -ENODEV
	 */
	if ((enumerate_hba == 1) && (hba_mpi_version !=  MPI2_VERSION))
		return -ENODEV;

	/* Enumerate PCIe HBA (MPI Endpoint), SAS 3.0 & above generation HBA's
	 * if hbas_to_enumerate is two, for other HBA's return with -ENODEV.
	 */
	if (enumerate_hba == 2) {
		switch (hba_mpi_version) {
		case MPI2_VERSION:
			if (pdev->device == MPI26_MFGPAGE_DEVID_SWCH_MPI_EP ||
			    pdev->device == MPI26_MFGPAGE_DEVID_SWCH_MPI_EP_1)
				break;
			else
				return -ENODEV;
		case MPI25_VERSION:
		case MPI26_VERSION:
			break;
		default:
			return -ENODEV;
		}
	}

	switch (hba_mpi_version) {
	case MPI2_VERSION:
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26))
		pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S |
			PCIE_LINK_STATE_L1 | PCIE_LINK_STATE_CLKPM);
#endif
		/* Use mpt2sas driver host template for SAS 2.0 HBA's */
		shost = scsi_host_alloc(&mpt2sas_driver_template,
			sizeof(struct MPT3SAS_ADAPTER));
		if (!shost)
			return -ENODEV;
		ioc = shost_priv(shost);
		memset(ioc, 0, sizeof(struct MPT3SAS_ADAPTER));
		ioc->hba_mpi_version_belonged = hba_mpi_version;
		ioc->id = mpt2_ids++;
		sprintf(ioc->driver_name, "%s", MPT2SAS_DRIVER_NAME);
		if (pdev->device == MPI2_MFGPAGE_DEVID_SSS6200) {
			ioc->is_warpdrive = 1;
			ioc->hide_ir_msg = 1;
		}
		else if (pdev->device == MPI26_MFGPAGE_DEVID_SWCH_MPI_EP ||
		    pdev->device == MPI26_MFGPAGE_DEVID_SWCH_MPI_EP_1)
			ioc->is_mcpu_endpoint = 1;
		else
			ioc->mfg_pg10_hide_flag = MFG_PAGE10_EXPOSE_ALL_DISKS;

		if(multipath_on_hba == -1 || multipath_on_hba == 0)
			ioc->multipath_on_hba = 0;
		else
			ioc->multipath_on_hba = 1;

		break;
	case MPI25_VERSION:
	case MPI26_VERSION:
		/* Use mpt3sas driver host template for SAS 3.0 HBA's */
		shost = scsi_host_alloc(&mpt3sas_driver_template,
				sizeof(struct MPT3SAS_ADAPTER));
		if (!shost)
			return -ENODEV;

		ioc = shost_priv(shost);
		memset(ioc, 0, sizeof(struct MPT3SAS_ADAPTER));
		ioc->hba_mpi_version_belonged = hba_mpi_version;
		ioc->id = mpt3_ids++;
		sprintf(ioc->driver_name, "%s", MPT3SAS_DRIVER_NAME);

		switch (pdev->device) {
		case MPI26_MFGPAGE_DEVID_SAS3508_1:
		case MPI26_MFGPAGE_DEVID_SAS3408:
		case MPI26_MFGPAGE_DEVID_SAS3516_1:
		case MPI26_MFGPAGE_DEVID_SAS3416:
		case MPI26_MFGPAGE_DEVID_SAS3716:
		case MPI26_MFGPAGE_DEVID_SAS3616:
		case MPI26_MFGPAGE_DEVID_PEX88000:
			ioc->is_gen35_ioc = 1;
			break;
		case MPI26_MFGPAGE_DEVID_INVALID0_3816:
		case MPI26_MFGPAGE_DEVID_INVALID0_3916:
			dev_printk(KERN_ERR, &pdev->dev,
			    "HBA with DeviceId 0x%04x, subsystem VendorId 0x%04x,"
			    " subsystem DeviceId 0x%04x is Invalid",
			    pdev->device, pdev->subsystem_vendor, pdev->subsystem_device);
			return 1;
		case MPI26_MFGPAGE_DEVID_INVALID1_3816:
		case MPI26_MFGPAGE_DEVID_INVALID1_3916:
			dev_printk(KERN_ERR, &pdev->dev,
			    "HBA with DeviceId 0x%04x, subsystem VendorId 0x%04x,"
			    " subsystem DeviceId 0x%04x is Tampered",
			    pdev->device, pdev->subsystem_vendor, pdev->subsystem_device);
			return 1;
		case MPI26_MFGPAGE_DEVID_CFG_SEC_3816:
		case MPI26_MFGPAGE_DEVID_CFG_SEC_3916:
			dev_printk(KERN_ERR, &pdev->dev,
			    "HBA with DeviceId 0x%04x, subsystem VendorId 0x%04x,"
			    " subsystem DeviceId 0x%04x is in Configurable Secured Mode",
			    pdev->device, pdev->subsystem_vendor, pdev->subsystem_device);
			/* fall through */
		case MPI26_MFGPAGE_DEVID_HARD_SEC_3816:
		case MPI26_MFGPAGE_DEVID_HARD_SEC_3916:
			ioc->is_aero_ioc = ioc->is_gen35_ioc = 1;
			break;
		default:
			ioc->is_gen35_ioc = ioc->is_aero_ioc = 0;
		}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
		pci_read_config_byte(ioc->pdev, PCI_CLASS_REVISION, &revision);
#else
		revision = pdev->revision;
#endif

		if ((revision >= SAS3_PCI_DEVICE_C0_REVISION) ||
				(ioc->hba_mpi_version_belonged == MPI26_VERSION)) {
			ioc->combined_reply_queue = 1;
			if (ioc->is_gen35_ioc) {
				ioc->nc_reply_index_count = NUM_REPLY_POST_INDEX_REGISTERS_16;
			} else {
				ioc->nc_reply_index_count = NUM_REPLY_POST_INDEX_REGISTERS_12;
			}
		}

		switch (ioc->is_gen35_ioc) {
		case 0:
			if(multipath_on_hba == -1 || multipath_on_hba == 0)
				ioc->multipath_on_hba = 0;
			else
				ioc->multipath_on_hba = 1;
			break;
		case 1:
			if(multipath_on_hba == -1 || multipath_on_hba > 0)
				ioc->multipath_on_hba = 1;
			else
				ioc->multipath_on_hba = 0;
		default:
			break;
		}

		break;
	default:
		return -ENODEV;
	}

	/* init local params */
	ioc = shost_private(shost);
	INIT_LIST_HEAD(&ioc->list);
	spin_lock(&gioc_lock);
	list_add_tail(&ioc->list, &mpt3sas_ioc_list);
	spin_unlock(&gioc_lock);
	ioc->shost = shost;
	ioc->pdev = pdev;
	ioc->scsi_io_cb_idx = scsi_io_cb_idx;
	ioc->tm_cb_idx = tm_cb_idx;
	ioc->ctl_cb_idx = ctl_cb_idx;
	ioc->ctl_tm_cb_idx = ctl_tm_cb_idx;
	ioc->ctl_diag_cb_idx = ctl_diag_cb_idx;
	ioc->base_cb_idx = base_cb_idx;
	ioc->port_enable_cb_idx = port_enable_cb_idx;
	ioc->transport_cb_idx = transport_cb_idx;
	ioc->scsih_cb_idx = scsih_cb_idx;
	ioc->config_cb_idx = config_cb_idx;
	ioc->tm_tr_cb_idx = tm_tr_cb_idx;
	ioc->tm_tr_volume_cb_idx = tm_tr_volume_cb_idx;
	ioc->tm_tr_internal_cb_idx = tm_tr_internal_cb_idx;
	ioc->tm_sas_control_cb_idx = tm_sas_control_cb_idx;
	ioc->logging_level = logging_level;
	ioc->schedule_dead_ioc_flush_running_cmds = &mpt3sas_scsih_flush_running_cmds;

	ioc->enable_sdev_max_qd = enable_sdev_max_qd;
	
	/* Host waits for six seconds */
	ioc->max_shutdown_latency = IO_UNIT_CONTROL_SHUTDOWN_TIMEOUT;
	/*
	 * Enable MEMORY MOVE support flag.
	 */
	ioc->drv_support_bitmap |= MPT_DRV_SUPPORT_BITMAP_MEMMOVE;
	/* Enable ADDITIONAL QUERY support flag. */
	ioc->drv_support_bitmap |= MPT_DRV_SUPPORT_BITMAP_ADDNLQUERY;

	/* misc semaphores and spin locks */
	mutex_init(&ioc->reset_in_progress_mutex);
	/* initializing pci_access_mutex lock */
	mutex_init(&ioc->pci_access_mutex);
	spin_lock_init(&ioc->ioc_reset_in_progress_lock);
	spin_lock_init(&ioc->scsi_lookup_lock);
	spin_lock_init(&ioc->sas_device_lock);
	spin_lock_init(&ioc->sas_node_lock);
	spin_lock_init(&ioc->fw_event_lock);
	spin_lock_init(&ioc->pcie_device_lock);
	spin_lock_init(&ioc->raid_device_lock);
	spin_lock_init(&ioc->diag_trigger_lock);
	spin_lock_init(&ioc->scsih_q_internal_lock);

	INIT_LIST_HEAD(&ioc->sas_device_list);
	INIT_LIST_HEAD(&ioc->port_table_list);
	INIT_LIST_HEAD(&ioc->sas_device_init_list);
	INIT_LIST_HEAD(&ioc->sas_expander_list);
	INIT_LIST_HEAD(&ioc->enclosure_list);
	INIT_LIST_HEAD(&ioc->pcie_device_list);
	INIT_LIST_HEAD(&ioc->pcie_device_init_list);
	INIT_LIST_HEAD(&ioc->fw_event_list);
	INIT_LIST_HEAD(&ioc->raid_device_list);
	INIT_LIST_HEAD(&ioc->sas_hba.sas_port_list);
	INIT_LIST_HEAD(&ioc->delayed_tr_list);
	INIT_LIST_HEAD(&ioc->delayed_sc_list);
	INIT_LIST_HEAD(&ioc->delayed_event_ack_list);
	INIT_LIST_HEAD(&ioc->delayed_tr_volume_list);
	INIT_LIST_HEAD(&ioc->delayed_internal_tm_list);
	INIT_LIST_HEAD(&ioc->scsih_q_intenal_cmds);
	INIT_LIST_HEAD(&ioc->reply_queue_list);

	sprintf(ioc->name, "%s_cm%d", ioc->driver_name, ioc->id);
	/* init shost parameters */
	shost->max_cmd_len = 32;
	shost->max_lun = max_lun;
	shost->transportt = mpt3sas_transport_template;
	shost->unique_id = ioc->id;

#if ((defined(RHEL_MAJOR) && (RHEL_MAJOR == 7) && (RHEL_MINOR == 2)) || \
((LINUX_VERSION_CODE >= KERNEL_VERSION(3,17,0)) && \
(LINUX_VERSION_CODE < KERNEL_VERSION(4,3,0))))
	/* Disable blk mq support on these kernels.
	 * Refer CQ# 1351060 for more details.
	 */
	if (shost->use_blk_mq) {
		shost_printk(KERN_INFO, shost, "Disabled use_blk_mq flag");
		shost->use_blk_mq = 0;
	}
#endif

#if ((defined(RHEL_MAJOR) && (RHEL_MAJOR >= 8)) || \
        (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)))
        ioc->drv_internal_flags |= MPT_DRV_INTERNAL_BITMAP_BLK_MQ;
#elif ((defined(RHEL_MAJOR) && (RHEL_MAJOR == 7) && (RHEL_MINOR >= 2)) ||  \
        (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)))
        if(shost->use_blk_mq)
        	ioc->drv_internal_flags |= MPT_DRV_INTERNAL_BITMAP_BLK_MQ;
#endif


#ifdef RHEL_MAJOR
#if (RHEL_MAJOR == 6 && RHEL_MINOR >= 2)
	if(!host_lock_mode)
        	shost->hostt->lockless = 1;
#endif
#endif

	if (max_sectors != 0xFFFF) {
		if (max_sectors < 64) {
			shost->max_sectors = 64;
			printk(MPT3SAS_WARN_FMT "Invalid value %d passed "
			    "for max_sectors, range is 64 to 32767. Assigning "
			    "value of 64.\n", ioc->name, max_sectors);
		} else if (max_sectors > 32767) {
			shost->max_sectors = 32767;
			printk(MPT3SAS_WARN_FMT "Invalid value %d passed "
			    "for max_sectors, range is 64 to 32767. Assigning "
			    "default value of 32767.\n", ioc->name,
			    max_sectors);
		} else {
			shost->max_sectors = max_sectors & 0xFFFE;
			printk(MPT3SAS_INFO_FMT "The max_sectors value is "
			    "set to %d\n", ioc->name, shost->max_sectors);
		}
	}

	if(ioc->is_mcpu_endpoint) {
		/* mCPU MPI support 64K max IO */
		shost->max_sectors = 128;
		printk(MPT3SAS_INFO_FMT "The max_sectors value is "
		    "set to %d\n", ioc->name, shost->max_sectors);
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27))
	rv = scsi_add_host(shost, &pdev->dev);
	if(rv) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		spin_lock(&gioc_lock);
		list_del(&ioc->list);
		spin_unlock(&gioc_lock);
		goto out_add_shost_fail;
	}
#endif

	ioc->disable_eedp_support = disable_eedp;


	/* event thread */
	snprintf(ioc->firmware_event_name, sizeof(ioc->firmware_event_name),
		"fw_event_%s%d", ioc->driver_name, ioc->id);
#if defined(alloc_ordered_workqueue)
	ioc->firmware_event_thread = alloc_ordered_workqueue(
	    ioc->firmware_event_name, 0);
#else
	ioc->firmware_event_thread = create_singlethread_workqueue(
	    ioc->firmware_event_name);
#endif
	if (!ioc->firmware_event_thread) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rv = -ENODEV;
		goto out_thread_fail;
	}

	ioc->is_driver_loading = 1;
	if ((mpt3sas_base_attach(ioc))) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rv = -ENODEV;
		goto out_attach_fail;
	}

	if (ioc->is_warpdrive) {
		if (ioc->mfg_pg10_hide_flag ==  MFG_PAGE10_EXPOSE_ALL_DISKS)
			ioc->hide_drives = 0;
		else if (ioc->mfg_pg10_hide_flag ==  MFG_PAGE10_HIDE_ALL_DISKS)
			ioc->hide_drives = 1;
		else {
			if (mpt3sas_get_num_volumes(ioc))
				ioc->hide_drives = 1;
			else
				ioc->hide_drives = 0;
		}
	} else
		ioc->hide_drives = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
	if (!ioc->disable_eedp_support) {
		/* register EEDP capabilities with SCSI layer */

		switch (ioc->hba_mpi_version_belonged) {
		case MPI2_VERSION:
			if (prot_mask)
				scsi_host_set_prot(shost, prot_mask);
			else
				scsi_host_set_prot(shost, SHOST_DIF_TYPE1_PROTECTION
					| SHOST_DIF_TYPE2_PROTECTION | SHOST_DIF_TYPE3_PROTECTION);
			scsi_host_set_guard(shost, SHOST_DIX_GUARD_CRC);
			break;
		case MPI25_VERSION:
		case MPI26_VERSION:
			if (prot_mask) {
				if (list_empty(&ioc->raid_device_list))
					scsi_host_set_prot(shost, (prot_mask & 0x7f));
				else
					scsi_host_set_prot(shost, (prot_mask & 0x77));

				printk(MPT3SAS_INFO_FMT
					": host protection capabilities enabled %s%s%s%s%s%s%s\n",
					ioc->name,
					(prot_mask & SHOST_DIF_TYPE1_PROTECTION) ? " DIF1" : "",
					(prot_mask & SHOST_DIF_TYPE2_PROTECTION) ? " DIF2" : "",
					(prot_mask & SHOST_DIF_TYPE3_PROTECTION) ? " DIF3" : "",
					(prot_mask & SHOST_DIX_TYPE0_PROTECTION) ? " DIX0" : "",
					(prot_mask & SHOST_DIX_TYPE1_PROTECTION) ? " DIX1" : "",
					(prot_mask & SHOST_DIX_TYPE2_PROTECTION) ? " DIX2" : "",
					(prot_mask & SHOST_DIX_TYPE3_PROTECTION) ? " DIX3" : "");
				if (protection_guard_mask)
					scsi_host_set_guard(shost, (protection_guard_mask & 3));
				else
					scsi_host_set_guard(shost, SHOST_DIX_GUARD_CRC);
			}
			break;
		}
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,4,0))
       rv = scsi_init_shared_tag_map(shost, shost->can_queue);
       if (rv) {
               pr_err(MPT3SAS_FMT "failure at %s:%d/%s()!\n",
                               ioc->name, __FILE__, __LINE__, __func__);
               goto out_add_shost_fail;
       }
#endif
	rv = scsi_add_host(shost, &pdev->dev);
	if(rv) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			ioc->name, __FILE__, __LINE__, __func__);
		spin_lock(&gioc_lock);
		list_del(&ioc->list);
		spin_unlock(&gioc_lock);
		goto out_add_shost_fail;
	}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
	scsi_scan_host(shost);
#else
	ioc->wait_for_discovery_to_complete = 0;
	_scsih_probe_devices(ioc);
	mpt3sas_base_start_watchdog(ioc);
	ioc->is_driver_loading = 0;

	if ((ioc->hba_mpi_version_belonged != MPI2_VERSION)
		&& (sata_smart_polling))
		mpt3sas_base_start_smart_polling(ioc);
#endif

	mpt3sas_setup_debugfs(ioc);
	return 0;

out_add_shost_fail:
	mpt3sas_base_detach(ioc);
out_attach_fail:
	destroy_workqueue(ioc->firmware_event_thread);
out_thread_fail:
	spin_lock(&gioc_lock);
	list_del(&ioc->list);
	spin_unlock(&gioc_lock);
	scsi_host_put(shost);
	return rv;
}

#ifdef CONFIG_PM
/**
 * scsih_suspend - power management suspend main entry point
 * @pdev: PCI device struct
 * @state: PM state change to (usually PCI_D3)
 *
 * Returns 0 success, anything else error.
 */
static int
scsih_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct Scsi_Host *shost = NULL;
	struct MPT3SAS_ADAPTER *ioc = NULL;
	pci_power_t device_state;
	int rc;

	rc = _scsih_get_shost_and_ioc(pdev, &shost, &ioc);
	if (rc) {
		dev_err(&pdev->dev, "unable to suspend device\n");
		return rc;
	}

	if ((ioc->hba_mpi_version_belonged != MPI2_VERSION) && (sata_smart_polling))
			mpt3sas_base_stop_smart_polling(ioc);
	mpt3sas_base_stop_watchdog(ioc);
	mpt3sas_base_stop_hba_unplug_watchdog(ioc);
	flush_scheduled_work();
	scsi_block_requests(shost);
	device_state = pci_choose_state(pdev, state);
	_scsih_ir_shutdown(ioc);
	_scsih_nvme_shutdown(ioc);

	printk(MPT3SAS_INFO_FMT "pdev=0x%p, slot=%s, entering "
	    "operating state [D%d]\n", ioc->name, pdev,
	    pci_name(pdev), device_state);

	pci_save_state(pdev);
	mpt3sas_base_free_resources(ioc);
	pci_set_power_state(pdev, device_state);
	return 0;
}

/**
 * scsih_resume - power management resume main entry point
 * @pdev: PCI device struct
 *
 * Returns 0 success, anything else error.
 */
static int
scsih_resume(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = NULL;
	struct MPT3SAS_ADAPTER *ioc = NULL;
	pci_power_t device_state = pdev->current_state;
	int r;

	r = _scsih_get_shost_and_ioc(pdev, &shost, &ioc);
	if (r) {
		dev_err(&pdev->dev, "unable to resume device\n");
		return r;
	}

	printk(MPT3SAS_INFO_FMT "pdev=0x%p, slot=%s, previous "
	    "operating state [D%d]\n", ioc->name, pdev,
	    pci_name(pdev), device_state);

	pci_set_power_state(pdev, PCI_D0);
	pci_enable_wake(pdev, PCI_D0, 0);
	pci_restore_state(pdev);
	ioc->pdev = pdev;
	r = mpt3sas_base_map_resources(ioc);
	if (r)
		return r;

	printk(MPT3SAS_INFO_FMT "issuing hard reset as part of OS resume\n",
	    ioc->name);
	mpt3sas_base_hard_reset_handler(ioc, SOFT_RESET);
	scsi_unblock_requests(shost);
	mpt3sas_base_start_watchdog(ioc);
	mpt3sas_base_start_hba_unplug_watchdog(ioc);
	if ((ioc->hba_mpi_version_belonged != MPI2_VERSION) && (sata_smart_polling))
			mpt3sas_base_start_smart_polling(ioc);
	return 0;
}
#endif /* CONFIG_PM */

/**
 * scsih_pci_error_detected - Called when a PCI error is detected.
 * @pdev: PCI device struct
 * @state: PCI channel state
 *
 * Description: Called when a PCI error is detected.
 *
 * Return value:
 *      PCI_ERS_RESULT_NEED_RESET or PCI_ERS_RESULT_DISCONNECT
 */
static pci_ers_result_t
scsih_pci_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct Scsi_Host *shost = NULL;
	struct MPT3SAS_ADAPTER *ioc = NULL;

	if (_scsih_get_shost_and_ioc(pdev, &shost, &ioc)) {
		dev_err(&pdev->dev, "device unavailable\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	printk(MPT3SAS_INFO_FMT "PCI error: detected callback, state(%d)!!\n",
	    ioc->name, state);

	switch (state) {
	case pci_channel_io_normal:
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		/* Fatal error, prepare for slot reset */
		ioc->pci_error_recovery = 1;
		scsi_block_requests(ioc->shost);
		if ((ioc->hba_mpi_version_belonged != MPI2_VERSION) && (sata_smart_polling))
				mpt3sas_base_stop_smart_polling(ioc);
		mpt3sas_base_stop_watchdog(ioc);
		mpt3sas_base_stop_hba_unplug_watchdog(ioc);
		mpt3sas_base_free_resources(ioc);
		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		/* Permanent error, prepare for device removal */
		ioc->pci_error_recovery = 1;
		if ((ioc->hba_mpi_version_belonged != MPI2_VERSION) && (sata_smart_polling))
				mpt3sas_base_stop_smart_polling(ioc);
		mpt3sas_base_stop_watchdog(ioc);
		mpt3sas_base_stop_hba_unplug_watchdog(ioc);
		mpt3sas_scsih_flush_running_cmds(ioc);
		return PCI_ERS_RESULT_DISCONNECT;
	}
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * scsih_pci_slot_reset - Called when PCI slot has been reset.
 * @pdev: PCI device struct
 *
 * Description: This routine is called by the pci error recovery
 * code after the PCI slot has been reset, just before we
 * should resume normal operations.
 */
static pci_ers_result_t
scsih_pci_slot_reset(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = NULL;
	struct MPT3SAS_ADAPTER *ioc = NULL;
	int rc;

	if (_scsih_get_shost_and_ioc(pdev, &shost, &ioc)) {
		dev_err(&pdev->dev, "unable to perform slot reset\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	printk(MPT3SAS_INFO_FMT "PCI error: slot reset callback!!\n",
	     ioc->name);

	ioc->pci_error_recovery = 0;
	ioc->pdev = pdev;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19))
	pci_restore_state(pdev);
#endif
	rc = mpt3sas_base_map_resources(ioc);
	if (rc)
		return PCI_ERS_RESULT_DISCONNECT;
	else {
		if(ioc->is_warpdrive)
			ioc->pci_error_recovery = 0;
	}

	printk(MPT3SAS_INFO_FMT
	     "issuing hard reset as part of PCI slot reset\n", ioc->name);
	rc = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);

	printk(MPT3SAS_WARN_FMT "hard reset: %s\n", ioc->name,
	    (rc == 0) ? "success" : "failed");

	if (!rc)
		return PCI_ERS_RESULT_RECOVERED;
	else
		return PCI_ERS_RESULT_DISCONNECT;
}

/**
 * scsih_pci_resume() - resume normal ops after PCI reset
 * @pdev: pointer to PCI device
 *
 * Called when the error recovery driver tells us that its
 * OK to resume normal operation. Use completion to allow
 * halted scsi ops to resume.
 */
static void
scsih_pci_resume(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = NULL;
	struct MPT3SAS_ADAPTER *ioc = NULL;

	if (_scsih_get_shost_and_ioc(pdev, &shost, &ioc)) {
		dev_err(&pdev->dev, "unable to resume device\n");
		return;
	}

	printk(MPT3SAS_INFO_FMT "PCI error: resume callback!!\n", ioc->name);

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(5, 7, 0)) || \
	(defined(RHEL_MAJOR) && (RHEL_MAJOR == 8) && (RHEL_MINOR >= 3)) \
	|| (defined(CONFIG_SUSE_KERNEL) && ((CONFIG_SUSE_VERSION == 15) \
	&& (CONFIG_SUSE_PATCHLEVEL >= 2))))
	pci_aer_clear_nonfatal_status(pdev);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19) 	
	pci_cleanup_aer_uncorrect_error_status(pdev);
#endif
	mpt3sas_base_start_watchdog(ioc);
	mpt3sas_base_start_hba_unplug_watchdog(ioc);
	scsi_unblock_requests(ioc->shost);
	if ((ioc->hba_mpi_version_belonged != MPI2_VERSION) && (sata_smart_polling))
		mpt3sas_base_start_smart_polling(ioc);
}

/**
 * scsih_pci_mmio_enabled - Enable MMIO and dump debug registers
 * @pdev: pointer to PCI device
 */
static pci_ers_result_t
scsih_pci_mmio_enabled(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = NULL;
	struct MPT3SAS_ADAPTER *ioc = NULL;

	if (_scsih_get_shost_and_ioc(pdev, &shost, &ioc)) {
		dev_err(&pdev->dev, "unable to enable mmio\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	printk(MPT3SAS_INFO_FMT "PCI error: mmio enabled callback!!\n",
	    ioc->name);

	/* TODO - dump whatever for debugging purposes */

	/* Driver ready to resume IOs. */
	return PCI_ERS_RESULT_RECOVERED;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0))
/**
 * mpt3sas_scsih_ncq_prio_supp - Check for NCQ command priority support
 * @sdev: scsi device struct
 *
 * This is called when a user indicates they would like to enable
 * ncq command priorities. This works only on SATA devices.
 */
u8 mpt3sas_scsih_ncq_prio_supp(struct scsi_device *sdev)
{
	unsigned char *buf;
	u8 ncq_prio_supp = 0;

	if (!scsi_device_supports_vpd(sdev))
		return ncq_prio_supp;

	buf = kmalloc(SCSI_VPD_PG_LEN, GFP_KERNEL);
	if (!buf)
		return ncq_prio_supp;

	if (!scsi_get_vpd_page(sdev, 0x89, buf, SCSI_VPD_PG_LEN))
		ncq_prio_supp = (buf[213] >> 4) & 1;

	kfree(buf);
	return ncq_prio_supp;
}
#endif

/*
 * The pci device ids are defined in mpi/mpi2_cnfg.h.
 */
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4,7,0)))
static DEFINE_PCI_DEVICE_TABLE(mpt3sas_pci_table) = {
#else
static const struct pci_device_id mpt3sas_pci_table[] = {
#endif
	/* Spitfire ~ 2004 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2004,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Falcon ~ 2008 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2008,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Liberator ~ 2108 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2108_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2108_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2108_3,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Meteor ~ 2116 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2116_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2116_2,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Thunderbolt ~ 2208 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_3,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_4,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_5,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_6,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Mustang ~ 2308 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2308_3,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SWCH_MPI_EP,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SWCH_MPI_EP_1,
		PCI_ANY_ID, PCI_ANY_ID },
	/* SSS6200 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SSS6200,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Fury ~ 3004 and 3008 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI25_MFGPAGE_DEVID_SAS3004,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI25_MFGPAGE_DEVID_SAS3008,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Invader ~ 3108 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI25_MFGPAGE_DEVID_SAS3108_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI25_MFGPAGE_DEVID_SAS3108_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI25_MFGPAGE_DEVID_SAS3108_5,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI25_MFGPAGE_DEVID_SAS3108_6,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Intruder & Cutlass ~ 3316 and 3324 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3216,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3224,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3316_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3316_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3316_3,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3316_4,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3324_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3324_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3324_3,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3324_4,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Ventura, Crusader, Harpoon & Tomcat ~ 3516, 3416, 3508 & 3408*/
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3508_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3408,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3516_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3416,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Marlin, Mercator ~ 3716, 3616 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3716,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_SAS3616,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Atlas PCIe Switch Management Port*/
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_PEX88000,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Aero SI –> 0x00E0 Invalid 0x00E1 Configurable Secure
	   0x00E2 Hard Secure 0x00E3 Tampered */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_INVALID0_3916,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_CFG_SEC_3916,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_HARD_SEC_3916,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_INVALID1_3916,
		PCI_ANY_ID, PCI_ANY_ID },

	/* Sea SI –> 0x00E4 Invalid, 0x00E5 Configurable Secure
	   0x00E6 Hard Secure 0x00E7 Tampered */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_INVALID0_3816,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_CFG_SEC_3816,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_HARD_SEC_3816,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI26_MFGPAGE_DEVID_INVALID1_3816,
		PCI_ANY_ID, PCI_ANY_ID },

	{0}     /* Terminating entry */
};

MODULE_DEVICE_TABLE(pci, mpt3sas_pci_table);

static struct pci_error_handlers _mpt3sas_err_handler = {
	.error_detected = scsih_pci_error_detected,
	.mmio_enabled   = scsih_pci_mmio_enabled,
	.slot_reset     = scsih_pci_slot_reset,
	.resume         = scsih_pci_resume,
};

static struct pci_driver mpt3sas_driver = {
	.name           = MPT3SAS_DRIVER_NAME,
	.id_table       = mpt3sas_pci_table,
	.probe          = _scsih_probe,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0))
	.remove         = scsih_remove,
#else
	.remove         = __devexit_p(scsih_remove),
#endif
	.shutdown       = scsih_shutdown,
	.err_handler    = &_mpt3sas_err_handler,
#ifdef CONFIG_PM
	.suspend        = scsih_suspend,
	.resume         = scsih_resume,
#endif
};

/**
 * scsih_init - main entry point for this driver.
 *
 * Returns 0 success, anything else error.
 */
static int
scsih_init(void)
{
	mpt2_ids = 0;
	mpt3_ids = 0;

	mpt3sas_base_initialize_callback_handler();

	 /* queuecommand callback hander */
	scsi_io_cb_idx = mpt3sas_base_register_callback_handler(_scsih_io_done);

	/* task managment callback handler */
	tm_cb_idx = mpt3sas_base_register_callback_handler(_scsih_tm_done);

	/* base internal commands callback handler */
	base_cb_idx = mpt3sas_base_register_callback_handler(mpt3sas_base_done);
	port_enable_cb_idx = mpt3sas_base_register_callback_handler(
	    mpt3sas_port_enable_done);

	/* transport internal commands callback handler */
	transport_cb_idx = mpt3sas_base_register_callback_handler(
	    mpt3sas_transport_done);

	/* scsih internal commands callback handler */
	scsih_cb_idx = mpt3sas_base_register_callback_handler(_scsih_done);

	/* configuration page API internal commands callback handler */
	config_cb_idx = mpt3sas_base_register_callback_handler(
	    mpt3sas_config_done);

	/* ctl module callback handler */
	ctl_cb_idx = mpt3sas_base_register_callback_handler(mpt3sas_ctl_done);
	ctl_tm_cb_idx = mpt3sas_base_register_callback_handler(
	    mpt3sas_ctl_tm_done);
	ctl_diag_cb_idx = mpt3sas_base_register_callback_handler(
	    mpt3sas_ctl_diag_done);

	tm_tr_cb_idx = mpt3sas_base_register_callback_handler(
	    _scsih_tm_tr_complete);

	tm_tr_volume_cb_idx = mpt3sas_base_register_callback_handler(
	    _scsih_tm_volume_tr_complete);

	tm_tr_internal_cb_idx = mpt3sas_base_register_callback_handler(
	    _scsih_tm_internal_tr_complete);

	tm_sas_control_cb_idx = mpt3sas_base_register_callback_handler(
	   _scsih_sas_control_complete);

#if defined(TARGET_MODE)
	mpt3sas_base_stm_initialize_callback_handler();
#endif

	mpt3sas_init_debugfs();

	return 0;
}

/**
 * scsih_exit - exit point for this driver (when it is a module).
 *
 * Returns 0 success, anything else error.
 */
static void
scsih_exit(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
	int enumerate_hba = 0;
#endif
#if defined(TARGET_MODE)
	mpt3sas_base_stm_release_callback_handler();
#endif

	mpt3sas_base_release_callback_handler(scsi_io_cb_idx);
	mpt3sas_base_release_callback_handler(tm_cb_idx);
	mpt3sas_base_release_callback_handler(base_cb_idx);
	mpt3sas_base_release_callback_handler(port_enable_cb_idx);
	mpt3sas_base_release_callback_handler(transport_cb_idx);
	mpt3sas_base_release_callback_handler(scsih_cb_idx);
	mpt3sas_base_release_callback_handler(config_cb_idx);
	mpt3sas_base_release_callback_handler(ctl_cb_idx);
	mpt3sas_base_release_callback_handler(ctl_tm_cb_idx);
	mpt3sas_base_release_callback_handler(ctl_diag_cb_idx);

	mpt3sas_base_release_callback_handler(tm_tr_cb_idx);
	mpt3sas_base_release_callback_handler(tm_tr_volume_cb_idx);
	mpt3sas_base_release_callback_handler(tm_tr_internal_cb_idx);
	mpt3sas_base_release_callback_handler(tm_sas_control_cb_idx);

/* raid transport support */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
	if (hbas_to_enumerate == -1) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0))
		enumerate_hba = 0;
#else
		enumerate_hba = 2;
#endif
	}
	else
		enumerate_hba = hbas_to_enumerate;

	if (enumerate_hba != 1)
		raid_class_release(mpt3sas_raid_template);
	if (enumerate_hba != 2)
		raid_class_release(mpt2sas_raid_template);
#endif
	sas_release_transport(mpt3sas_transport_template);
	mpt3sas_exit_debugfs();
}

/**
 * _mpt3sas_init - main entry point for this driver.
 *
 * Returns 0 success, anything else error.
 */
static int __init
_mpt3sas_init(void)
{
	int error;
	int enumerate_hba = 0;
	pr_info("%s version %s loaded\n", MPT3SAS_DRIVER_NAME,
                                       MPT3SAS_DRIVER_VERSION);
	mpt3sas_transport_template =
		sas_attach_transport(&mpt3sas_transport_functions);
	if (!mpt3sas_transport_template)
		return -ENODEV;
	/* No need attach mpt3sas raid functions template
	* if hbas_to_enumarate value is one.
	*/
	if (hbas_to_enumerate == -1) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0))
		enumerate_hba = 0;
#else
		enumerate_hba = 2;
#endif
	}
	else
		enumerate_hba = hbas_to_enumerate;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
	if (enumerate_hba != 1) {
		mpt3sas_raid_template =
			raid_class_attach(&mpt3sas_raid_functions);
		if (!mpt3sas_raid_template) {
			sas_release_transport(mpt3sas_transport_template);
			return -ENODEV;
		}
	}
	/* No need to attach mpt2sas raid functions template
     * if hbas_to_enumarate value is two
     */
	if (enumerate_hba != 2) {
		mpt2sas_raid_template =
			raid_class_attach(&mpt2sas_raid_functions);
		if (!mpt2sas_raid_template) {
			sas_release_transport(mpt3sas_transport_template);
			return -ENODEV;
		}
	}
#endif
	error = scsih_init();
	if (error) {
		scsih_exit();
		return error;
	}

	mpt3sas_ctl_init(enumerate_hba);

	error = pci_register_driver(&mpt3sas_driver);
	if (error)
		scsih_exit();
	return error;
}

/**
 * _mpt3sas_exit - exit point for this driver (when it is a module).
 *
 */
static void __exit
_mpt3sas_exit(void)
{
	int enumerate_hba = 0;
	pr_info("mpt3sas version %s unloading\n", MPT3SAS_DRIVER_VERSION);

	if (hbas_to_enumerate == -1) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0))
		enumerate_hba = 0;
#else
		enumerate_hba = 2;
#endif
	}
	else
		enumerate_hba = hbas_to_enumerate;

	mpt3sas_ctl_exit(enumerate_hba);
	pci_unregister_driver(&mpt3sas_driver);

	scsih_exit();
}

module_init(_mpt3sas_init);
module_exit(_mpt3sas_exit);

