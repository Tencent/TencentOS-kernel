/*
 * This is the Fusion MPT base driver providing common API layer interface
 * for access to MPT (Message Passing Technology) firmware.
 *
 * This code is based on drivers/scsi/mpt2sas/mpt2_base.c
 * Copyright (c) 2007-2018  LSI Corporation
 * Copyright (c)  2013-2018 Avago Technologies 
 * Copyright (c) 2013-2018  Broadcom Inc.
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
 * _csmisas_sas_device_find_by_id - sas device search
 * @ioc: per adapter object
 * @id: target id assigned by the OS
 * @channel: channel id assigned by the OS, (but needs to be zero)
 * @sas_device: the sas_device object
 *
 * This searches for sas_device based on id, then return sas_device
 * object in parameters.
 *
 * Returns zero if successful
 */
static int
_csmisas_sas_device_find_by_id(struct MPT3SAS_ADAPTER *ioc, int id, int channel,
	struct _sas_device *sas_device)
{
	unsigned long flags;
	struct _sas_device *a;
	int rc = -ENODEV;

	if (channel != 0) /* only channel = 0 support */
		return rc;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_for_each_entry(a, &ioc->sas_device_list, list) {
		if (a->id != id)
			continue;
		memcpy(sas_device, a, sizeof(struct _sas_device));
		rc = 0;
		goto out;
	}

 out:
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	return rc;
}

/**
 * _csmisas_sas_device_find_by_handle - sas device search
 * @ioc: per adapter object
 * @handle: sas device handle (assigned by firmware)
 * @sas_device: the sas_device object
 *
 * This searches for sas_device based on handle, then return sas_device
 * object in parameters.
 *
 * Returns zero if successful
 */
static int
_csmisas_sas_device_find_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	struct _sas_device *sas_device)
{
	unsigned long flags;
	struct _sas_device *a;
	int rc = -ENODEV;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_for_each_entry(a, &ioc->sas_device_list, list) {
		if (a->handle != handle)
			continue;
		memcpy(sas_device, a, sizeof(struct _sas_device));
		rc = 0;
		goto out;
	}

 out:
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	return rc;
}

/**
 * _csmisas_scsih_sas_device_find_by_sas_address - sas device search
 * @ioc: per adapter object
 * @sas_address: sas address
 * @sas_device: the sas_device object
 *
 * This searches for sas_device based on sas address, then return sas_device
 * object in parameters.
 *
 * Returns zero if successful
 */
static int
_csmisas_scsih_sas_device_find_by_sas_address(struct MPT3SAS_ADAPTER *ioc,
	u64 sas_address, struct _sas_device *sas_device)
{
	unsigned long flags;
	struct _sas_device *a;
	int rc = -ENODEV;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_for_each_entry(a, &ioc->sas_device_list, list) {
		if (a->sas_address != sas_address)
			continue;
		memcpy(sas_device, a, sizeof(struct _sas_device));
		rc = 0;
		goto out;
	}

 out:
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	return rc;
}

/**
 * _csmisas_raid_device_find_by_handle - raid device search
 * @ioc: per adapter object
 * @handle: device handle (assigned by firmware)
 * @raid_device: the raid_device object
 *
 * This searches for raid_device based on handle, then return raid_device
 * object in parameters.
 *
 * Returns zero if successful
*/
static int
_csmisas_raid_device_find_by_handle(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	struct _raid_device *raid_device)
{
	struct _raid_device *r;
	unsigned long flags;
	int rc = -ENODEV;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	list_for_each_entry(r, &ioc->raid_device_list, list) {
		if (r->handle != handle)
			continue;
		memcpy(raid_device, r, sizeof(struct _raid_device));
		rc = 0;
		goto out;
	}
 out:
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
	return rc;
}

/**
 * _csmisas_raid_device_find_by_id - raid device search
 * @ioc: per adapter object
 * @id: target id assigned by the OS
 * @channel: channel id assigned by the OS, (but needs to be one)
 * @raid_device: the raid_device object
 *
 * This searches for raid_device based on id, then return raid_device
 * object in parameters.
 *
 * Returns zero if successful
 */
static int
_csmisas_raid_device_find_by_id(struct MPT3SAS_ADAPTER *ioc, int id,
	int channel, struct _raid_device *raid_device)
{
	struct _raid_device *r;
	unsigned long flags;
	int rc = -ENODEV;

	if (channel != 1) /* only channel = 1 support */
		return rc;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	list_for_each_entry(r, &ioc->raid_device_list, list) {
		if (r->id != id)
			continue;
		memcpy(raid_device, r, sizeof(struct _raid_device));
		rc = 0;
		goto out;
	}
 out:
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
	return rc;
}

/**
 * _csmisas_scsi_lookup_find_by_tag - pending IO lookup
 * @ioc: per adapter object
 * @queue_tag: tag assigned by the OS
 * @task_mid:  the smid that the driver assinged to this IO
 * @scmd: desired scmd
 *
 * This searches for pending IO based on tag, then return smid + scmd pointer
 * in parameters.
 *
 * Returns zero if successful
 */
static int
_csmisas_scsi_lookup_find_by_tag(struct MPT3SAS_ADAPTER *ioc, u32 queue_tag,
	u16 *task_mid, struct scsi_cmnd **scmd)
{
	int rc = 1;
	int smid;
	struct scsi_cmnd *cmd;
	struct scsiio_tracker *st;

	*task_mid = 0;
	for (smid = 1; smid <= ioc->shost->can_queue; smid++) {
		cmd = mpt3sas_scsih_scsi_lookup_get(ioc, smid);
		if (!cmd)
			continue;
		st = mpt3sas_base_scsi_cmd_priv(cmd);
		if (!st || st->smid == 0)
			continue;
		if (cmd->tag == queue_tag) {
			*task_mid = st->smid;
			*scmd = st->scmd;
			rc = 0;
			goto out;
		}
	}
 out:
	return rc;
}

/**
 * _map_sas_status_to_csmi - Conversion  for Connection Status
 * @mpi_sas_status: SAS status returned by the firmware
 *
 * Returns converted connection status
 *
 */
static u8
_map_sas_status_to_csmi(u8 mpi_sas_status)
{
	u8 csmi_connect_status;

	switch (mpi_sas_status) {

	case MPI2_SASSTATUS_SUCCESS:
		csmi_connect_status = CSMI_SAS_OPEN_ACCEPT;
		break;

	case MPI2_SASSTATUS_UTC_BAD_DEST:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_BAD_DESTINATION;
		break;

	case MPI2_SASSTATUS_UTC_CONNECT_RATE_NOT_SUPPORTED:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_RATE_NOT_SUPPORTED;
		break;

	case MPI2_SASSTATUS_UTC_PROTOCOL_NOT_SUPPORTED:
		csmi_connect_status =
		    CSMI_SAS_OPEN_REJECT_PROTOCOL_NOT_SUPPORTED;
		break;

	case MPI2_SASSTATUS_UTC_STP_RESOURCES_BUSY:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_STP_RESOURCES_BUSY;
		break;

	case MPI2_SASSTATUS_UTC_WRONG_DESTINATION:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_WRONG_DESTINATION;
		break;

	case MPI2_SASSTATUS_SDSF_NAK_RECEIVED:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_RETRY;
		break;

	case MPI2_SASSTATUS_SDSF_CONNECTION_FAILED:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_PATHWAY_BLOCKED;
		break;

	case MPI2_SASSTATUS_INITIATOR_RESPONSE_TIMEOUT:
		csmi_connect_status =  CSMI_SAS_OPEN_REJECT_NO_DESTINATION;
		break;

	case MPI2_SASSTATUS_UNKNOWN_ERROR:
	case MPI2_SASSTATUS_INVALID_FRAME:
	case MPI2_SASSTATUS_UTC_BREAK_RECEIVED:
	case MPI2_SASSTATUS_UTC_PORT_LAYER_REQUEST:
	case MPI2_SASSTATUS_SHORT_INFORMATION_UNIT:
	case MPI2_SASSTATUS_LONG_INFORMATION_UNIT:
	case MPI2_SASSTATUS_XFER_RDY_INCORRECT_WRITE_DATA:
	case MPI2_SASSTATUS_XFER_RDY_REQUEST_OFFSET_ERROR:
	case MPI2_SASSTATUS_XFER_RDY_NOT_EXPECTED:
	case MPI2_SASSTATUS_DATA_INCORRECT_DATA_LENGTH:
	case MPI2_SASSTATUS_DATA_TOO_MUCH_READ_DATA:
	case MPI2_SASSTATUS_DATA_OFFSET_ERROR:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_RESERVE_STOP;
		break;

	default:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_RESERVE_STOP;
		break;
	}

	return csmi_connect_status;
}

/**
 * _csmi_valid_phy - Verify the phy provided is a valid phy and if not
 * set the ioctl return code appropriately
 * @ioc: per adapter object
 * @IoctlHeader: pointer to the ioctl header to update if needed
 * @bPhyIdentifier: phy id to check
 *
 * Return:	0 if successful
 *		1 if not successful
 *
 */
static u8
_csmi_valid_phy(struct MPT3SAS_ADAPTER *ioc, IOCTL_HEADER *IoctlHeader,
	u8 bPhyIdentifier)
{
	u8 rc = 0;
	if (bPhyIdentifier >= ioc->sas_hba.num_phys) {
		IoctlHeader->ReturnCode = CSMI_SAS_PHY_DOES_NOT_EXIST;
		dcsmisasprintk(ioc, printk(KERN_WARNING
		    ": phy_number >= ioc->num_ports\n"));
		rc = 1;
	}
	return rc;
}

/**
 * _csmi_valid_port_for_smp_cmd - Verify the port provided is a valid phy and if
 * not set the ioctl return code appropriately.  Also verifies the phy/port
 * values are set appropriately for CSMI
 * @ioc: per adapter object
 * @IoctlHeader: pointer to the ioctl header to update if needed
 * @bPhyIdentifier: phy id to check
 * @bPortIdentifier: port id to check
 *
 * Return:	0 if successful
 *		1 if not successful
 *
 */
static u8
_csmi_valid_port_for_smp_cmd(struct MPT3SAS_ADAPTER *ioc,
	IOCTL_HEADER *IoctlHeader, u8 bPhyIdentifier,
	u8 bPortIdentifier)
{

	/* Neither a phy nor a port has been selected.
	 */
	if ((bPhyIdentifier == CSMI_SAS_USE_PORT_IDENTIFIER) &&
	    (bPortIdentifier == CSMI_SAS_IGNORE_PORT)) {
		IoctlHeader->ReturnCode = CSMI_SAS_SELECT_PHY_OR_PORT;
		dcsmisasprintk(ioc, printk(KERN_ERR
		    "%s::%s() @%d - incorrect bPhyIdentifier "
		    "and bPortIdentifier!\n",
		    __FILE__, __func__, __LINE__));
		return 1;
	}

	if ((bPhyIdentifier != CSMI_SAS_USE_PORT_IDENTIFIER) ||
	    (bPortIdentifier == CSMI_SAS_IGNORE_PORT)) {
		IoctlHeader->ReturnCode = CSMI_SAS_PHY_CANNOT_BE_SELECTED;
		dcsmisasprintk(ioc, printk(KERN_ERR
		    "%s::%s() @%d - phy selection for SMPs is not allowed!\n",
		    __FILE__, __func__, __LINE__));
		return 1;
	}

	return 0;
}

/**
 * _csmi_valid_port_for_cmd - Verify the port provided is a valid phy and if
 * not set the ioctl return code appropriately.  Also verifies the phy/port
 * values are set appropriately for CSMI
 * @ioc: per adapter object
 * @IoctlHeader: pointer to the ioctl header to update if needed
 * @bPhyIdentifier: phy id to check
 * @bPortIdentifier: port id to check
 *
 * Return:	0 if successful
 *		1 if not successful
 *
 */
static u8
_csmi_valid_port_for_cmd(struct MPT3SAS_ADAPTER *ioc, IOCTL_HEADER *IoctlHeader,
	u8 bPhyIdentifier, u8 bPortIdentifier)
{
	/* Neither a phy nor a port has been selected.
	 */
	if ((bPhyIdentifier == CSMI_SAS_USE_PORT_IDENTIFIER) &&
	    (bPortIdentifier == CSMI_SAS_IGNORE_PORT)) {
		IoctlHeader->ReturnCode = CSMI_SAS_SELECT_PHY_OR_PORT;
		dcsmisasprintk(ioc, printk(KERN_ERR
		    "%s::%s() @%d - incorrect bPhyIdentifier "
		    "and bPortIdentifier!\n",
		    __FILE__, __func__, __LINE__));
		return 1;
	}

	return _csmi_valid_phy(ioc, IoctlHeader, bPhyIdentifier);
}

/**
 * Routine for the CSMI Sas Get Driver Info command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_get_driver_info(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_DRIVER_INFO_BUFFER *karg)
{
	int rc = 0;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	/* Fill in the data and return the structure to the calling
	 * program
	 */
	memcpy(karg->Information.szName, MPT2SAS_DEV_NAME,
	    sizeof(MPT2SAS_DEV_NAME));
	sprintf(karg->Information.szDescription, "%s %s", MPT2SAS_DESCRIPTION,
	    MPT2SAS_DRIVER_VERSION);

	karg->Information.usMajorRevision = MPT2SAS_MAJOR_VERSION;
	karg->Information.usMinorRevision = MPT2SAS_MINOR_VERSION;
	karg->Information.usBuildRevision = MPT2SAS_BUILD_VERSION;
	karg->Information.usReleaseRevision = MPT2SAS_RELEASE_VERSION;

	karg->Information.usCSMIMajorRevision = CSMI_MAJOR_REVISION;
	karg->Information.usCSMIMinorRevision = CSMI_MINOR_REVISION;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI_SAS_GET_CNTLR_CONFIG command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_get_cntlr_config(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_CNTLR_CONFIG_BUFFER *karg)
{

	u32 version;
	int rc = 0;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	/* Clear the struct before filling in data. */
	memset(&karg->Configuration, 0, sizeof(CSMI_SAS_CNTLR_CONFIG));

	/* Fill in the data and return the structure to the calling program */
	karg->Configuration.uBaseIoAddress = ioc->pio_chip;
	memcpy(&karg->Configuration.BaseMemoryAddress,
	    &ioc->chip_phys, 8);

	karg->Configuration.uBoardID = (ioc->pdev->subsystem_device << 16) |
	    (ioc->pdev->subsystem_vendor);
	karg->Configuration.usSlotNumber = (ioc->ioc_pg1.PCISlotNum == 0xFF) ?
	    SLOT_NUMBER_UNKNOWN : ioc->ioc_pg1.PCISlotNum;
	karg->Configuration.bControllerClass = CSMI_SAS_CNTLR_CLASS_HBA;
	karg->Configuration.bIoBusType = CSMI_SAS_BUS_TYPE_PCI;
	karg->Configuration.BusAddress.PciAddress.bBusNumber =
	    ioc->pdev->bus->number;
	karg->Configuration.BusAddress.PciAddress.bDeviceNumber =
	    PCI_SLOT(ioc->pdev->devfn);
	karg->Configuration.BusAddress.PciAddress.bFunctionNumber =
	    PCI_FUNC(ioc->pdev->devfn);

	/* Serial number */
	memcpy(&karg->Configuration.szSerialNumber,
	    ioc->manu_pg0.BoardTracerNumber, 16);

	/* Firmware version */
	karg->Configuration.usMajorRevision =
	    (ioc->facts.FWVersion.Word & 0xFF000000) >> 24;
	karg->Configuration.usMinorRevision =
	    (ioc->facts.FWVersion.Word & 0x00FF0000) >> 16;
	karg->Configuration.usBuildRevision =
	    (ioc->facts.FWVersion.Word & 0x0000FF00) >> 8;
	karg->Configuration.usReleaseRevision =
	    (ioc->facts.FWVersion.Word & 0x000000FF);

	/* Bios version */
	version = le32_to_cpu(ioc->bios_pg3.BiosVersion);
	karg->Configuration.usBIOSMajorRevision = (version & 0xFF000000) >> 24;
	karg->Configuration.usBIOSMinorRevision = (version & 0x00FF0000) >> 16;
	karg->Configuration.usBIOSBuildRevision = (version & 0x0000FF00) >> 8;
	karg->Configuration.usBIOSReleaseRevision = (version & 0x000000FF);

	karg->Configuration.uControllerFlags =
	    CSMI_SAS_CNTLR_SAS_HBA  | CSMI_SAS_CNTLR_FWD_ONLINE |
	    CSMI_SAS_CNTLR_SATA_HBA | CSMI_SAS_CNTLR_FWD_HRESET |
	    CSMI_SAS_CNTLR_FWD_SUPPORT;

	dcsmisasprintk(ioc, printk(KERN_DEBUG "Board ID = 0x%x\n",
	    karg->Configuration.uBoardID));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "Serial Number = %s\n",
	    karg->Configuration.szSerialNumber));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "Major Revision = %d\n",
	    karg->Configuration.usMajorRevision));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "Minor Revision = %d\n",
	    karg->Configuration.usMinorRevision));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "Build Revision = %d\n",
	    karg->Configuration.usBuildRevision));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "Release Revision = %d\n",
	    karg->Configuration.usReleaseRevision));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "uControllerFlags = 0x%x\n",
	    karg->Configuration.uControllerFlags));

	/* Success */
	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI Sas Get Controller Status command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_get_cntlr_status(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_CNTLR_STATUS_BUFFER *karg)
{

	int rc = 0;
	u32 ioc_state;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	/* Fill in the data and return the structure to the calling program */
	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	switch (ioc_state & MPI2_IOC_STATE_MASK) {
	case MPI2_IOC_STATE_OPERATIONAL:
		karg->Status.uStatus =  CSMI_SAS_CNTLR_STATUS_GOOD;
		karg->Status.uOfflineReason = 0;
		break;

	case MPI2_IOC_STATE_FAULT:
	case MPI2_IOC_STATE_COREDUMP:
		karg->Status.uStatus = CSMI_SAS_CNTLR_STATUS_FAILED;
		karg->Status.uOfflineReason = 0;
		break;

	case MPI2_IOC_STATE_RESET:
	case MPI2_IOC_STATE_READY:
	default:
		karg->Status.uStatus =  CSMI_SAS_CNTLR_STATUS_OFFLINE;
		karg->Status.uOfflineReason =
			CSMI_SAS_OFFLINE_REASON_INITIALIZING;
		break;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "IOC state = 0x%x\n", ioc_state));

	/* Success */
	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI Sas Get Phy Info command.
 *
 * Outputs:     None.
 * Return:      0 if successful
 *              -EFAULT if data unavailable
 *              -ENODEV if no such device/adapter
 */
static int
_csmisas_get_phy_info(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_PHY_INFO_BUFFER *karg)
{
	int rc = 0;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasIOUnitPage0_t *sas_iounit_pg0 = NULL;
	Mpi2SasPhyPage0_t sas_phy_pg0;
	Mpi2SasDevicePage0_t sas_device_pg0;
	u64 sas_address;
	u32 device_info;
	u16 ioc_status;
	u16 sz;
	int i;

	memset(&karg->Information, 0, sizeof(CSMI_SAS_PHY_INFO));

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	/* Get number of phys on this HBA */
	mpt3sas_config_get_number_hba_phys(ioc,
	    &karg->Information.bNumberOfPhys);
	if (!karg->Information.bNumberOfPhys) {
		printk(MPT3SAS_ERR_FMT "%s():%d: failure\n",
		       ioc->name, __func__, __LINE__);
		rc = -EFAULT;
		goto out;
	}

	/*
	 * Get SAS IO Unit page 0
	 */
	sz = offsetof(Mpi2SasIOUnitPage0_t, PhyData) +
	    (karg->Information.bNumberOfPhys * sizeof(Mpi2SasIOUnit0PhyData_t));
	sas_iounit_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg0) {
		printk(MPT3SAS_ERR_FMT "%s():%d: failure\n",
		       ioc->name, __func__, __LINE__);
		rc = -ENOMEM;
		goto out;
	}
	if ((mpt3sas_config_get_sas_iounit_pg0(ioc, &mpi_reply,
					       sas_iounit_pg0, sz))) {
		printk(MPT3SAS_ERR_FMT "%s():%d: failure\n",
		       ioc->name, __func__, __LINE__);
		rc = -EFAULT;
		goto kfree;
	}
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT3SAS_ERR_FMT "%s():%d: IOC status = 0x%x\n",
		       ioc->name, __func__, __LINE__, ioc_status);
		rc = -EFAULT;
		goto kfree;
	}

	/* Loop over all PHYs */
	for (i = 0; i < karg->Information.bNumberOfPhys; i++) {
		/* Dump SAS IO Unit page 0 data */
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    "---- SAS IO UNIT PAGE 0 ----\n"));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    "PHY Num = 0x%X\n", i));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    "Attached Device Handle = 0x%X\n",
		    le16_to_cpu(sas_iounit_pg0->PhyData[i].AttachedDevHandle)));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    "Controller Device Handle = 0x%X\n",
		    le16_to_cpu(sas_iounit_pg0->
		    PhyData[i].ControllerDevHandle)));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    "Port = 0x%X\n", sas_iounit_pg0->PhyData[i].Port));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    "Port Flags = 0x%X\n",
		    sas_iounit_pg0->PhyData[i].PortFlags));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    "PHY Flags = 0x%X\n", sas_iounit_pg0->PhyData[i].PhyFlags));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    "Negotiated Link Rate = 0x%X\n",
		    sas_iounit_pg0->PhyData[i].NegotiatedLinkRate));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    "Controller PHY Device Info = 0x%X\n",
		    le32_to_cpu(sas_iounit_pg0->
		    PhyData[i].ControllerPhyDeviceInfo)));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    "DiscoveryStatus = 0x%X\n",
		    le32_to_cpu(sas_iounit_pg0->PhyData[i].DiscoveryStatus)));


		/* Fill in data from SAS IO Unit page 0 */

		/* Port identifier */
		karg->Information.Phy[i].bPortIdentifier =
		    sas_iounit_pg0->PhyData[i].Port;

		/* Negotiated link rate */
		switch (sas_iounit_pg0->PhyData[i].NegotiatedLinkRate &
		    MPI2_SAS_NEG_LINK_RATE_MASK_PHYSICAL) {

		case MPI2_SAS_NEG_LINK_RATE_PHY_DISABLED:
			karg->Information.Phy[i].bNegotiatedLinkRate =
			    CSMI_SAS_PHY_DISABLED;
			break;

		case MPI2_SAS_NEG_LINK_RATE_NEGOTIATION_FAILED:
			karg->Information.Phy[i].bNegotiatedLinkRate =
			    CSMI_SAS_LINK_RATE_FAILED;
			break;

		case MPI2_SAS_NEG_LINK_RATE_SATA_OOB_COMPLETE:
			break;

		case MPI2_SAS_NEG_LINK_RATE_1_5:
			karg->Information.Phy[i].bNegotiatedLinkRate =
			    CSMI_SAS_LINK_RATE_1_5_GBPS;
			break;

		case MPI2_SAS_NEG_LINK_RATE_3_0:
			karg->Information.Phy[i].bNegotiatedLinkRate =
			    CSMI_SAS_LINK_RATE_3_0_GBPS;
			break;

		case MPI2_SAS_NEG_LINK_RATE_6_0:
			karg->Information.Phy[i].bNegotiatedLinkRate =
			    CSMI_SAS_LINK_RATE_6_0_GBPS;
			break;

		default:
			karg->Information.Phy[i].bNegotiatedLinkRate =
			    CSMI_SAS_LINK_RATE_UNKNOWN;
			break;
		}

		device_info = le32_to_cpu(sas_iounit_pg0->PhyData[i].
		    ControllerPhyDeviceInfo);

		/* Parent initiator port protocol */
		karg->Information.Phy[i].Identify.bInitiatorPortProtocol = 0;

		if (device_info & MPI2_SAS_DEVICE_INFO_SSP_INITIATOR)
			karg->Information.Phy[i].Identify.
			    bInitiatorPortProtocol |= CSMI_SAS_PROTOCOL_SSP;
		if (device_info & MPI2_SAS_DEVICE_INFO_STP_INITIATOR)
			karg->Information.Phy[i].Identify.
			    bInitiatorPortProtocol |= CSMI_SAS_PROTOCOL_STP;
		if (device_info & MPI2_SAS_DEVICE_INFO_SMP_INITIATOR)
			karg->Information.Phy[i].Identify.
			    bInitiatorPortProtocol |= CSMI_SAS_PROTOCOL_SMP;
		if (device_info & MPI2_SAS_DEVICE_INFO_SATA_HOST)
			karg->Information.Phy[i].Identify.
			    bInitiatorPortProtocol |= CSMI_SAS_PROTOCOL_SATA;

		/* Parent target port protocol */
		karg->Information.Phy[i].Identify.bTargetPortProtocol = 0;

		if (device_info & MPI2_SAS_DEVICE_INFO_SSP_TARGET)
			karg->Information.Phy[i].Identify.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_SSP;
		if (device_info & MPI2_SAS_DEVICE_INFO_STP_TARGET)
			karg->Information.Phy[i].Identify.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_STP;
		if (device_info & MPI2_SAS_DEVICE_INFO_SMP_TARGET)
			karg->Information.Phy[i].Identify.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_SMP;
		if (device_info & MPI2_SAS_DEVICE_INFO_SATA_DEVICE)
			karg->Information.Phy[i].Identify.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_SATA;

		/* Parent device type */
		switch (device_info & MPI2_SAS_DEVICE_INFO_MASK_DEVICE_TYPE) {

		case MPI2_SAS_DEVICE_INFO_NO_DEVICE:
			karg->Information.Phy[i].Identify.bDeviceType =
			    CSMI_SAS_NO_DEVICE_ATTACHED;
			break;

		case MPI2_SAS_DEVICE_INFO_END_DEVICE:
			karg->Information.Phy[i].Identify.bDeviceType =
			    CSMI_SAS_END_DEVICE;
			break;

		case MPI2_SAS_DEVICE_INFO_EDGE_EXPANDER:
			karg->Information.Phy[i].Identify.bDeviceType =
			    CSMI_SAS_EDGE_EXPANDER_DEVICE;
			break;

		case MPI2_SAS_DEVICE_INFO_FANOUT_EXPANDER:
			karg->Information.Phy[i].Identify.bDeviceType =
			    CSMI_SAS_FANOUT_EXPANDER_DEVICE;
			break;
		}


		/*
		 * Get SAS PHY page 0
		 */
		if ((mpt3sas_config_get_phy_pg0(ioc, &mpi_reply, &sas_phy_pg0,
		    i))) {
			printk(MPT3SAS_ERR_FMT "%s():%d: failure\n",
			       ioc->name, __func__, __LINE__);
			rc = -EFAULT;
			goto kfree;
		}
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT3SAS_ERR_FMT "%s():%d: IOC status = 0x%x\n",
			       ioc->name, __func__, __LINE__, ioc_status);
			rc = -EFAULT;
			goto kfree;
		}

		/* Dump SAS PHY page 0 data */
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    "---- SAS PHY PAGE 0 ----\n"));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		   "PHY Num = 0x%X\n", i));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		   "Attached Device Handle = 0x%X\n",
		   le16_to_cpu(sas_phy_pg0.AttachedDevHandle)));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		   "Attached PHY Identifier = 0x%X\n",
		   sas_phy_pg0.AttachedPhyIdentifier));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		   "Programmed Link Rate = 0x%X\n",
		   sas_phy_pg0.ProgrammedLinkRate));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		   "Hardware Link Rate = 0x%X\n", sas_phy_pg0.HwLinkRate));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		   "Change Count = 0x%X\n", sas_phy_pg0.ChangeCount));
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		   "PHY Info = 0x%X\n", le32_to_cpu(sas_phy_pg0.PhyInfo)));

		/* Get PhyChangeCount */
		karg->Information.Phy[i].bPhyChangeCount =
		   sas_phy_pg0.ChangeCount;

		/* Get AutoDiscover */
		if (sas_iounit_pg0->PhyData[i].PortFlags &
		    MPI2_SASIOUNIT0_PORTFLAGS_DISCOVERY_IN_PROGRESS)
			karg->Information.Phy[i].bAutoDiscover =
			    CSMI_SAS_DISCOVER_IN_PROGRESS;
		else if (sas_iounit_pg0->PhyData[i].DiscoveryStatus)
			karg->Information.Phy[i].bAutoDiscover =
			    CSMI_SAS_DISCOVER_ERROR;
		else
			karg->Information.Phy[i].bAutoDiscover =
			    CSMI_SAS_DISCOVER_COMPLETE;

		/* Minimum hardware link rate */
		switch (sas_phy_pg0.HwLinkRate &
		    MPI2_SAS_HWRATE_MIN_RATE_MASK) {

		case MPI2_SAS_HWRATE_MIN_RATE_1_5:
			karg->Information.Phy[i].bMinimumLinkRate =
			    CSMI_SAS_LINK_RATE_1_5_GBPS;
			break;

		case MPI2_SAS_HWRATE_MIN_RATE_3_0:
			karg->Information.Phy[i].bMinimumLinkRate =
			    CSMI_SAS_LINK_RATE_3_0_GBPS;
			break;

		case MPI2_SAS_HWRATE_MIN_RATE_6_0:
			karg->Information.Phy[i].bMinimumLinkRate =
			    CSMI_SAS_LINK_RATE_6_0_GBPS;
			break;

		default:
			karg->Information.Phy[i].bMinimumLinkRate =
			    CSMI_SAS_LINK_RATE_UNKNOWN;
			break;
		}

		/* Maximum hardware link rate */
		switch (sas_phy_pg0.HwLinkRate &
		    MPI2_SAS_HWRATE_MAX_RATE_MASK) {

		case MPI2_SAS_HWRATE_MAX_RATE_1_5:
			karg->Information.Phy[i].bMaximumLinkRate =
			    CSMI_SAS_LINK_RATE_1_5_GBPS;
			break;

		case MPI2_SAS_HWRATE_MAX_RATE_3_0:
			karg->Information.Phy[i].bMaximumLinkRate =
			    CSMI_SAS_LINK_RATE_3_0_GBPS;
			break;

		case MPI2_SAS_HWRATE_MAX_RATE_6_0:
			karg->Information.Phy[i].bMaximumLinkRate =
			    CSMI_SAS_LINK_RATE_6_0_GBPS;
			break;

		default:
			karg->Information.Phy[i].bMaximumLinkRate =
			    CSMI_SAS_LINK_RATE_UNKNOWN;
			break;
		}

		/* Minimum programmed link rate */
		switch (sas_phy_pg0.ProgrammedLinkRate &
		    MPI2_SAS_PRATE_MIN_RATE_MASK) {

		case MPI2_SAS_PRATE_MIN_RATE_1_5:
			karg->Information.Phy[i].bMinimumLinkRate |=
			    (CSMI_SAS_PROGRAMMED_LINK_RATE_1_5_GBPS << 4);
			break;

		case MPI2_SAS_PRATE_MIN_RATE_3_0:
			karg->Information.Phy[i].bMinimumLinkRate |=
			    (CSMI_SAS_PROGRAMMED_LINK_RATE_3_0_GBPS << 4);
			break;

		case MPI2_SAS_PRATE_MIN_RATE_6_0:
			karg->Information.Phy[i].bMinimumLinkRate |=
			    (CSMI_SAS_PROGRAMMED_LINK_RATE_6_0_GBPS << 4);
			break;

		default:
			karg->Information.Phy[i].bMinimumLinkRate |=
			    (CSMI_SAS_LINK_RATE_UNKNOWN << 4);
			break;
		}

		/* Maximum programmed link rate */
		switch (sas_phy_pg0.ProgrammedLinkRate &
		    MPI2_SAS_PRATE_MAX_RATE_MASK) {

		case MPI2_SAS_PRATE_MAX_RATE_1_5:
			karg->Information.Phy[i].bMaximumLinkRate |=
			    (CSMI_SAS_PROGRAMMED_LINK_RATE_1_5_GBPS << 4);
			break;

		case MPI2_SAS_PRATE_MAX_RATE_3_0:
			karg->Information.Phy[i].bMaximumLinkRate |=
			    (CSMI_SAS_PROGRAMMED_LINK_RATE_3_0_GBPS << 4);
			break;

		case MPI2_SAS_PRATE_MAX_RATE_6_0:
			karg->Information.Phy[i].bMaximumLinkRate |=
			    (CSMI_SAS_PROGRAMMED_LINK_RATE_6_0_GBPS << 4);
			break;

		default:
			karg->Information.Phy[i].bMaximumLinkRate |=
			    (CSMI_SAS_LINK_RATE_UNKNOWN << 4);
			break;
		}



		/* Get attached SAS Device page 0 */
		if (sas_phy_pg0.AttachedDevHandle) {
			if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply,
			    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_HANDLE,
			    le16_to_cpu(sas_phy_pg0.AttachedDevHandle)))) {
				printk(MPT3SAS_ERR_FMT "%s():%d: failure\n",
				    ioc->name, __func__, __LINE__);
				rc = -EFAULT;
				goto kfree;
			}
			ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
			    MPI2_IOCSTATUS_MASK;
			if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
				printk(MPT3SAS_ERR_FMT "%s():%d: IOC status = "
				    "0x%x\n", ioc->name, __func__, __LINE__,
				    ioc_status);
				rc = -EFAULT;
				goto kfree;
			}

			sas_address = le64_to_cpu(sas_device_pg0.SASAddress);

			/* Dump attached SAS Device page 0 data */
			dcsmisasprintk(ioc, printk(KERN_DEBUG
			   "---- SAS DEVICE PAGE 0 ----\n"));
			dcsmisasprintk(ioc, printk(KERN_DEBUG
			   "PHY Num = 0x%X\n", i));
			dcsmisasprintk(ioc, printk(KERN_DEBUG
			   "SAS Address = 0x%llX\n", (unsigned long long)
			   sas_address));
			dcsmisasprintk(ioc, printk(KERN_DEBUG
			   "Device Info = 0x%X\n",
			   le32_to_cpu(sas_device_pg0.DeviceInfo)));


			/* Fill in data from SAS PHY page 0 and attached SAS
			 * Device page 0 */

			device_info = le32_to_cpu(sas_device_pg0.DeviceInfo);

			/* Attached initiator port protocol */
			karg->Information.Phy[i].Attached.
			    bInitiatorPortProtocol = 0;

			if (device_info & MPI2_SAS_DEVICE_INFO_SSP_INITIATOR)
				karg->Information.Phy[i].Attached.
				bInitiatorPortProtocol |= CSMI_SAS_PROTOCOL_SSP;
			if (device_info & MPI2_SAS_DEVICE_INFO_STP_INITIATOR)
				karg->Information.Phy[i].Attached.
				bInitiatorPortProtocol |= CSMI_SAS_PROTOCOL_STP;
			if (device_info & MPI2_SAS_DEVICE_INFO_SMP_INITIATOR)
				karg->Information.Phy[i].Attached.
				bInitiatorPortProtocol |= CSMI_SAS_PROTOCOL_SMP;
			if (device_info & MPI2_SAS_DEVICE_INFO_SATA_HOST)
				karg->Information.Phy[i].Attached.
				bInitiatorPortProtocol |=
				    CSMI_SAS_PROTOCOL_SATA;

			dcsmisasprintk(ioc, printk(KERN_DEBUG
			    "Device Info Initiator Port = 0x%X\n",
			    karg->Information.Phy[i].Attached.
			    bInitiatorPortProtocol));

			/* Attached target port protocol */
			karg->Information.Phy[i].Attached.
			    bTargetPortProtocol = 0;

			if (device_info & MPI2_SAS_DEVICE_INFO_SSP_TARGET)
				karg->Information.Phy[i].Attached.
				bTargetPortProtocol |= CSMI_SAS_PROTOCOL_SSP;
			if (device_info & MPI2_SAS_DEVICE_INFO_STP_TARGET)
				karg->Information.Phy[i].Attached.
				bTargetPortProtocol |= CSMI_SAS_PROTOCOL_STP;
			if (device_info & MPI2_SAS_DEVICE_INFO_SMP_TARGET)
				karg->Information.Phy[i].Attached.
				bTargetPortProtocol |= CSMI_SAS_PROTOCOL_SMP;
			if (device_info & MPI2_SAS_DEVICE_INFO_SATA_DEVICE)
				karg->Information.Phy[i].Attached.
				bTargetPortProtocol |= CSMI_SAS_PROTOCOL_SATA;

			dcsmisasprintk(ioc, printk(KERN_DEBUG
			   "Device Info Target Port = 0x%X\n",
			   karg->Information.Phy[i].Attached.
			   bTargetPortProtocol));

			/* Attached device type */
			switch (device_info &
			    MPI2_SAS_DEVICE_INFO_MASK_DEVICE_TYPE) {

			case MPI2_SAS_DEVICE_INFO_NO_DEVICE:
				karg->Information.Phy[i].Attached.bDeviceType =
				    CSMI_SAS_NO_DEVICE_ATTACHED;
				break;

			case MPI2_SAS_DEVICE_INFO_END_DEVICE:
				karg->Information.Phy[i].Attached.bDeviceType =
				    CSMI_SAS_END_DEVICE;
				break;

			case MPI2_SAS_DEVICE_INFO_EDGE_EXPANDER:
				karg->Information.Phy[i].Attached.bDeviceType =
				    CSMI_SAS_EDGE_EXPANDER_DEVICE;
				break;

			case MPI2_SAS_DEVICE_INFO_FANOUT_EXPANDER:
				karg->Information.Phy[i].Attached.bDeviceType =
				    CSMI_SAS_FANOUT_EXPANDER_DEVICE;
				break;
			}
			dcsmisasprintk(ioc, printk(KERN_DEBUG
			   "Device Info Device Type = 0x%X\n",
			   karg->Information.Phy[i].Attached.bDeviceType));

			/* Setup the SAS address for the attached device */
			*(__be64 *)karg->Information.Phy[i].
			    Attached.bSASAddress = cpu_to_be64(sas_address);
			karg->Information.Phy[i].Attached.bPhyIdentifier =
				sas_phy_pg0.AttachedPhyIdentifier;
		}


		/* Get parent SAS Device page 0 */
		if (sas_iounit_pg0->PhyData[i].ControllerDevHandle) {
			if ((mpt3sas_config_get_sas_device_pg0(ioc, &mpi_reply,
			    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_HANDLE,
			    le16_to_cpu(sas_iounit_pg0->
			    PhyData[i].ControllerDevHandle)))) {
				printk(MPT3SAS_ERR_FMT "%s():%d: failure\n",
				    ioc->name, __func__, __LINE__);
				rc = -EFAULT;
				goto kfree;
			}
			ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
			    MPI2_IOCSTATUS_MASK;
			if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
				printk(MPT3SAS_ERR_FMT "%s():%d: IOC status = "
				    "0x%x\n", ioc->name, __func__, __LINE__,
				    ioc_status);
				rc = -EFAULT;
				goto kfree;
			}

			sas_address = le64_to_cpu(sas_device_pg0.SASAddress);

			/* Dump parent SAS Device page 0 data */
			dcsmisasprintk(ioc, printk(KERN_DEBUG
			   "---- SAS DEVICE PAGE 0 (Parent) ----\n"));
			dcsmisasprintk(ioc, printk(KERN_DEBUG
			   "PHY Num = 0x%X\n", i));
			dcsmisasprintk(ioc, printk(KERN_DEBUG
			   "SAS Address = 0x%llX\n", (unsigned long long)
			   sas_address));
			dcsmisasprintk(ioc, printk(KERN_DEBUG
			   "Device Info = 0x%X\n",
			   le32_to_cpu(sas_device_pg0.DeviceInfo)));


			/* Fill in data from parent SAS Device page 0 */

			/* Setup the SAS address for the parent device */
			*(__be64 *)karg->Information.Phy[i].Identify.
			    bSASAddress = cpu_to_be64(sas_address);
			karg->Information.Phy[i].Identify.bPhyIdentifier = i;

#if 0
			/* Dump all the data we care about */
			printk(KERN_DEBUG "\tPortId                        = %x\n",
			    karg->Information.Phy[i].bPortIdentifier);
			printk(KERN_DEBUG "\tNegotiated Linkrate           = %x\n",
			    karg->Information.Phy[i].bNegotiatedLinkRate);

			printk(KERN_DEBUG "\tIdentify Target Port Protocol = %x\n",
			    karg->Information.Phy[i].Identify.
			    bTargetPortProtocol);
			printk(KERN_DEBUG "\tIdentify Device Type          = %x\n",
			    karg->Information.Phy[i].Identify.bDeviceType);
			printk(KERN_DEBUG "\tIdentify SAS Address          = %llx\n",
			    be64_to_cpu(*(u64 *)karg->Information.Phy[i].
			    Identify.bSASAddress));

			printk(KERN_DEBUG "\tAttached Target Port Protocol = %x\n",
			    karg->Information.Phy[i].Attached.
			    bTargetPortProtocol);
			printk(KERN_DEBUG "\tAttached Device Type          = %x\n",
			    karg->Information.Phy[i].Attached.bDeviceType);
			printk(KERN_DEBUG "\tAttached SAS Address          = %llx\n",
			    be64_to_cpu(*(u64 *)karg->Information.Phy[i].
			    Attached.bSASAddress));
#endif
		}
	}

	/* Success */
	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

 kfree:
	kfree(sas_iounit_pg0);
 out:
	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI SAS Set PHY Info command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_set_phy_info(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_SET_PHY_INFO_BUFFER *karg)
{
	CSMI_SAS_SET_PHY_INFO *info = &karg->Information;
	Mpi2SasIOUnitPage1_t *sas_iounit_pg1 = NULL;
	Mpi2SasPhyPage0_t phy_pg0;
	Mpi2ConfigReply_t mpi_reply;
	MPI2_SAS_IO_UNIT1_PHY_DATA *phy_ptr = NULL;
	u16 ioc_status;
	int rc = 0;
	u16 sz;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	if (_csmi_valid_phy(ioc, &karg->IoctlHeader, info->bPhyIdentifier))
		goto out;

	sz = offsetof(Mpi2SasIOUnitPage1_t, PhyData) + (ioc->sas_hba.num_phys *
	    sizeof(Mpi2SasIOUnit1PhyData_t));
	sas_iounit_pg1 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg1) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -ENOMEM;
		goto out;
	}
	if ((mpt3sas_config_get_sas_iounit_pg1(ioc, &mpi_reply,
	    sas_iounit_pg1, sz))) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -ENXIO;
		goto out;
	}
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -EIO;
		goto out;
	}

	if (mpt3sas_config_get_phy_pg0(ioc, &mpi_reply, &phy_pg0,
	    info->bPhyIdentifier)) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -ENXIO;
		goto out;
	}
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -EIO;
		goto out;
	}

	phy_ptr = &sas_iounit_pg1->PhyData[info->bPhyIdentifier];

	switch (info->bNegotiatedLinkRate) {
	case CSMI_SAS_LINK_RATE_NEGOTIATE:
		phy_ptr->PhyFlags &= ~MPI2_SASIOUNIT1_PHYFLAGS_PHY_DISABLE;
		break;
	case CSMI_SAS_LINK_RATE_PHY_DISABLED:
		phy_ptr->PhyFlags |= MPI2_SASIOUNIT1_PHYFLAGS_PHY_DISABLE;
		break;
	default:
		printk(MPT3SAS_ERR_FMT
		    "%s():%d: bad negotiated link rate: %X\n",
		    ioc->name, __func__, __LINE__, info->bNegotiatedLinkRate);
		rc = -EFAULT;
		goto out;
		break;
	}

	switch (info->bProgrammedMinimumLinkRate) {
	case CSMI_SAS_PROGRAMMED_LINK_RATE_UNCHANGED:
		break;
	case CSMI_SAS_PROGRAMMED_LINK_RATE_1_5_GBPS:
		phy_ptr->MaxMinLinkRate =
		    (phy_ptr->MaxMinLinkRate & MPI2_SASIOUNIT1_MAX_RATE_MASK) |
		     MPI2_SASIOUNIT1_MIN_RATE_1_5;
		break;
	case CSMI_SAS_PROGRAMMED_LINK_RATE_3_0_GBPS:
		phy_ptr->MaxMinLinkRate =
		    (phy_ptr->MaxMinLinkRate & MPI2_SASIOUNIT1_MAX_RATE_MASK) |
		     MPI2_SASIOUNIT1_MIN_RATE_3_0;
		break;
	case CSMI_SAS_PROGRAMMED_LINK_RATE_6_0_GBPS:
		phy_ptr->MaxMinLinkRate =
		    (phy_ptr->MaxMinLinkRate & MPI2_SASIOUNIT1_MAX_RATE_MASK) |
		     MPI2_SASIOUNIT1_MIN_RATE_6_0;
		break;
	case CSMI_SAS_PROGRAMMED_LINK_RATE_12_0_GBPS:
	default:
		karg->IoctlHeader.ReturnCode = CSMI_SAS_LINK_RATE_OUT_OF_RANGE;
		printk(MPT3SAS_ERR_FMT
		    "%s():%d: bad programmed minimum link rate: %X\n",
		    ioc->name, __func__, __LINE__,
		    info->bProgrammedMinimumLinkRate);
		rc = -EFAULT;
		goto out;
		break;
	}

	switch (info->bProgrammedMaximumLinkRate) {
	case CSMI_SAS_PROGRAMMED_LINK_RATE_UNCHANGED:
		break;
	case CSMI_SAS_PROGRAMMED_LINK_RATE_1_5_GBPS:
		phy_ptr->MaxMinLinkRate =
		    (phy_ptr->MaxMinLinkRate & MPI2_SASIOUNIT1_MIN_RATE_MASK) |
		     MPI2_SASIOUNIT1_MAX_RATE_1_5;
		break;
	case CSMI_SAS_PROGRAMMED_LINK_RATE_3_0_GBPS:
		phy_ptr->MaxMinLinkRate =
		    (phy_ptr->MaxMinLinkRate & MPI2_SASIOUNIT1_MIN_RATE_MASK) |
		     MPI2_SASIOUNIT1_MAX_RATE_3_0;
		break;
	case CSMI_SAS_PROGRAMMED_LINK_RATE_6_0_GBPS:
		phy_ptr->MaxMinLinkRate =
		    (phy_ptr->MaxMinLinkRate & MPI2_SASIOUNIT1_MIN_RATE_MASK) |
		     MPI2_SASIOUNIT1_MAX_RATE_6_0;
		break;
	case CSMI_SAS_PROGRAMMED_LINK_RATE_12_0_GBPS:
	default:
		karg->IoctlHeader.ReturnCode = CSMI_SAS_LINK_RATE_OUT_OF_RANGE;
		printk(MPT3SAS_ERR_FMT
		    "%s():%d: bad programmed maximum link rate: %X\n",
		    ioc->name, __func__, __LINE__,
		    info->bProgrammedMaximumLinkRate);
		rc = -EFAULT;
		goto out;
		break;
	}

	if (info->bSignalClass != CSMI_SAS_SIGNAL_CLASS_UNKNOWN) {
		printk(MPT3SAS_ERR_FMT
		    "%s():%d: signal class is not supported: %X\n",
		    ioc->name, __func__, __LINE__,
		    info->bSignalClass);
		rc = -EFAULT;
		goto out;
	}

	/* Validate the new settings before committing them
	 * min <= max, min >= hwmin, max <= hwmax
	 */
	if ((phy_ptr->MaxMinLinkRate & MPI2_SASIOUNIT1_MIN_RATE_MASK) >
	    (phy_ptr->MaxMinLinkRate & MPI2_SASIOUNIT1_MAX_RATE_MASK)>>4 ||
	    (phy_ptr->MaxMinLinkRate & MPI2_SASIOUNIT1_MIN_RATE_MASK) <
	    (phy_pg0.HwLinkRate & MPI2_SAS_HWRATE_MIN_RATE_MASK) ||
	    (phy_ptr->MaxMinLinkRate & MPI2_SASIOUNIT1_MAX_RATE_MASK) >
	    (phy_pg0.HwLinkRate & MPI2_SAS_HWRATE_MAX_RATE_MASK)) {
		printk(MPT3SAS_ERR_FMT
		    "%s():%d: Rates invalid or exceed hardware limits\n",
		    ioc->name, __func__, __LINE__);
		rc = -EFAULT;
		goto out;
	}


	if (mpt3sas_config_set_sas_iounit_pg1(ioc, &mpi_reply, sas_iounit_pg1,
	    sz)) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -ENXIO;
		goto out;
	}


 out:
	kfree(sas_iounit_pg1);
	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI Sas Get SCSI Address command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_get_scsi_address(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_GET_SCSI_ADDRESS_BUFFER *karg)
{
	int rc = 0;
	struct _sas_device sas_device;
	u64 sas_address;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	karg->bTargetId = 0;
	karg->bPathId = 0;
	karg->bLun = 0;

	/* Find the target SAS device */
	sas_address = be64_to_cpu(*(__be64 *)karg->bSASAddress);
	rc = _csmisas_scsih_sas_device_find_by_sas_address(ioc,
	    sas_address, &sas_device);
	if (rc == 0) {
		karg->bHostIndex = ioc->shost->host_no;
		karg->bTargetId = sas_device.id;
		karg->bPathId = sas_device.channel;
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
	} else
		karg->IoctlHeader.ReturnCode = CSMI_SAS_NO_SCSI_ADDRESS;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI Sas Get SATA Signature
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_get_sata_signature(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_SATA_SIGNATURE_BUFFER *karg)
{
	int rc = 0;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasPhyPage0_t phy_pg0_request;
	Mpi2SasDevicePage1_t sas_device_pg1;
	struct _sas_device sas_device;
	u32 handle;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_NOT_AN_END_DEVICE;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	if (_csmi_valid_phy(ioc, &karg->IoctlHeader,
	    karg->Signature.bPhyIdentifier))
		goto out;

	if (mpt3sas_config_get_phy_pg0(ioc, &mpi_reply, &phy_pg0_request,
	    karg->Signature.bPhyIdentifier)) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}

	handle = le16_to_cpu(phy_pg0_request.AttachedDevHandle);
	if (!handle) {
		dcsmisasprintk(ioc, printk(KERN_WARNING
		    ": NOT AN END DEVICE\n"));
		goto out;
	}
	rc = _csmisas_sas_device_find_by_handle(ioc, handle, &sas_device);
	if (rc != 0)
		goto out;
	if (!(sas_device.device_info & MPI2_SAS_DEVICE_INFO_SATA_DEVICE)) {
		dcsmisasprintk(ioc, printk(KERN_WARNING
		    ": NOT A SATA DEVICE\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_NO_SATA_DEVICE;
		goto out;
	}

	if (mpt3sas_config_get_sas_device_pg1(ioc, &mpi_reply, &sas_device_pg1,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle)) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}

	memcpy(karg->Signature.bSignatureFIS,
	    sas_device_pg1.InitialRegDeviceFIS, 20);
	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

 out:
	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI Sas Get Device Address
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_get_device_address(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER *karg)
{
	int rc = 0;
	struct _sas_device sas_device;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_NO_DEVICE_ADDRESS;
	memset(karg->bSASAddress, 0, sizeof(u64));
	memset(karg->bSASLun, 0, sizeof(karg->bSASLun));

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	if (karg->bHostIndex != ioc->shost->host_no)
		goto out;

	/* search for device based on bus/target */
	rc = _csmisas_sas_device_find_by_id(ioc, karg->bTargetId,
	    karg->bPathId, &sas_device);
	if (!rc) {
		*(__be64 *)karg->bSASAddress =
		    cpu_to_be64(sas_device.sas_address);
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
	}

 out:
	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI Sas Get Link Errors command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_get_link_errors(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_LINK_ERRORS_BUFFER *karg)
{
	int rc = 0;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasPhyPage1_t phy_pg1;
	Mpi2SasIoUnitControlRequest_t sas_iounit_request;
	Mpi2SasIoUnitControlReply_t sas_iounit_reply;
	u32 phy_number;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	phy_number = karg->Information.bPhyIdentifier;
	if (_csmi_valid_phy(ioc, &karg->IoctlHeader, phy_number))
		goto out;

	/* get hba phy error logs */
	if ((mpt3sas_config_get_phy_pg1(ioc, &mpi_reply, &phy_pg1,
	    phy_number))) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -ENXIO;
		goto out;
	}

	if (mpi_reply.IOCStatus || mpi_reply.IOCLogInfo)
		printk(MPT3SAS_INFO_FMT "phy(%d), ioc_status"
		    "(0x%04x), loginfo(0x%08x)\n", ioc->name,
		    phy_number, le16_to_cpu(mpi_reply.IOCStatus),
		    le32_to_cpu(mpi_reply.IOCLogInfo));

/* EDM : dump PHY Page 1 data*/
	dcsmisasprintk(ioc, printk(KERN_DEBUG
	    "---- SAS PHY PAGE 1 ------------\n"));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "Invalid Dword Count=0x%x\n",
	    phy_pg1.InvalidDwordCount));
	dcsmisasprintk(ioc, printk(KERN_DEBUG
	    "Running Disparity Error Count=0x%x\n",
	    phy_pg1.RunningDisparityErrorCount));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "Loss Dword Synch Count=0x%x\n",
	    phy_pg1.LossDwordSynchCount));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "PHY Reset Problem Count=0x%x\n",
	    phy_pg1.PhyResetProblemCount));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "\n\n"));
/* EDM : debug data */

	karg->Information.uInvalidDwordCount =
		le32_to_cpu(phy_pg1.InvalidDwordCount);
	karg->Information.uRunningDisparityErrorCount =
		le32_to_cpu(phy_pg1.RunningDisparityErrorCount);
	karg->Information.uLossOfDwordSyncCount =
		le32_to_cpu(phy_pg1.LossDwordSynchCount);
	karg->Information.uPhyResetProblemCount =
		le32_to_cpu(phy_pg1.PhyResetProblemCount);

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	if (karg->Information.bResetCounts ==
	    CSMI_SAS_LINK_ERROR_DONT_RESET_COUNTS)
		goto out;

	memset(&sas_iounit_request, 0, sizeof(Mpi2SasIoUnitControlRequest_t));
	memset(&sas_iounit_reply, 0, sizeof(Mpi2SasIoUnitControlReply_t));
	sas_iounit_request.Function = MPI2_FUNCTION_SAS_IO_UNIT_CONTROL;
	sas_iounit_request.PhyNum = phy_number;
	sas_iounit_request.Operation = MPI2_SAS_OP_PHY_CLEAR_ERROR_LOG;

	if (mpt3sas_base_sas_iounit_control(ioc, &sas_iounit_reply,
	    &sas_iounit_request)) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
	}

	if (sas_iounit_reply.IOCStatus || sas_iounit_reply.IOCLogInfo)
		printk(MPT3SAS_INFO_FMT "phy(%d), ioc_status"
		    "(0x%04x), loginfo(0x%08x)\n", ioc->name,
		    phy_number, le16_to_cpu(sas_iounit_reply.IOCStatus),
		    le32_to_cpu(sas_iounit_reply.IOCLogInfo));

 out:
	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI SAS SMP Passthru command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_smp_passthru(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_SMP_PASSTHRU_BUFFER *karg)
{
	Mpi2SmpPassthroughRequest_t *mpi_request;
	Mpi2SmpPassthroughReply_t *mpi_reply;
	int rc = 0;
	u16 smid;
	unsigned long timeleft;
	void *psge;
	u32 sgl_flags;
	void *data_addr = NULL;
	dma_addr_t data_dma;
	u32 data_out_sz = 0, data_in_sz = 0;
	u16 response_sz;
	u16 ioc_status;
	u8 issue_host_reset = 0;
	u64 sas_address;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	if (_csmi_valid_port_for_smp_cmd(ioc, &karg->IoctlHeader,
	    karg->Parameters.bPhyIdentifier, karg->Parameters.bPortIdentifier))
		goto out;

	/* MPI doesn't support SMP commands across specific phy's at specific
	 * rates so a port must be specified
	 */
	if (karg->Parameters.bConnectionRate != CSMI_SAS_LINK_RATE_NEGOTIATE) {
		dcsmisasprintk(ioc, printk(KERN_ERR
		    "%s::%s() @%d - must use CSMI_SAS_LINK_RATE_NEGOTIATE\n",
		    __FILE__, __func__, __LINE__));
		goto out;
	}

	/*
	 * The following is taken from _transport_smp_handler and
	 * _transport_expander_report_manufacture in mpt3sas_transport.c.
	 * More or less.
	 */

	if (ioc->shost_recovery) {
		printk(MPT3SAS_INFO_FMT "%s():%d: host reset in progress!\n",
		       ioc->name, __func__, __LINE__);
		rc = -EFAULT;
		goto out;
	}

	mutex_lock(&ioc->transport_cmds.mutex);

	if (ioc->transport_cmds.status != MPT3_CMD_NOT_USED) {
		printk(MPT3SAS_ERR_FMT "%s():%d: status = 0x%x\n",
		       ioc->name, __func__, __LINE__,
		       ioc->transport_cmds.status);
		rc = -EFAULT;
		goto unlock;
	}
	ioc->transport_cmds.status = MPT3_CMD_PENDING;

	rc = mpt3sas_wait_for_ioc_to_operational(ioc, 10);
	if (rc)
		goto out;

	smid = mpt3sas_base_get_smid(ioc, ioc->transport_cb_idx);
	if (!smid) {
		printk(MPT3SAS_ERR_FMT "%s():%d: smid = 0\n",
		       ioc->name, __func__, __LINE__);
		rc = -EFAULT;
		goto cmd_not_used;
	}

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->transport_cmds.smid = smid;

	/* Request and response sizes */
	data_out_sz = karg->Parameters.uRequestLength;
	data_in_sz = sizeof(CSMI_SAS_SMP_RESPONSE);

	/* Allocate DMA memory. Use a single buffer for both request and
	 * response data. */
	data_addr = pci_alloc_consistent(ioc->pdev, data_out_sz + data_in_sz,
					 &data_dma);
	if (!data_addr) {
		printk(MPT3SAS_ERR_FMT "%s():%d: data addr = NULL\n",
		       ioc->name, __func__, __LINE__);
		rc = -ENOMEM;
		mpt3sas_base_free_smid(ioc, smid);
		goto cmd_not_used;
	}

	/* Copy the SMP request to the DMA buffer */
	memcpy(data_addr, &karg->Parameters.Request, data_out_sz);

	/* Setup the MPI request frame */
	memset(mpi_request, 0, sizeof(Mpi2SmpPassthroughRequest_t));
	mpi_request->Function = MPI2_FUNCTION_SMP_PASSTHROUGH;
	mpi_request->PhysicalPort = karg->Parameters.bPortIdentifier;
	sas_address = be64_to_cpu(
	    *(__be64 *)karg->Parameters.bDestinationSASAddress);
	mpi_request->SASAddress = cpu_to_le64(sas_address);
	mpi_request->RequestDataLength = cpu_to_le16(data_out_sz);
	psge = &mpi_request->SGL;

	/* Request (write) sgel first */
	sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
	     MPI2_SGE_FLAGS_END_OF_BUFFER | MPI2_SGE_FLAGS_HOST_TO_IOC);
	sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
	ioc->base_add_sg_single(psge, sgl_flags | data_out_sz, data_dma);

	/* incr sgel */
	psge += ioc->sge_size;

	/* Response (read) sgel last */
	sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
	     MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
	     MPI2_SGE_FLAGS_END_OF_LIST);
	sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
	ioc->base_add_sg_single(psge, sgl_flags | data_in_sz, data_dma +
	    data_out_sz);

	/* Send the request */
	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): sending request "
	   "(target addr = 0x%llx, function = 0x%x timeout = %d sec)\n",
	   ioc->name, __func__, (unsigned long long)sas_address,
	   karg->Parameters.Request.bFunction, karg->IoctlHeader.Timeout));
	init_completion(&ioc->transport_cmds.done);
	ioc->put_smid_default(ioc, smid);
	timeleft = wait_for_completion_timeout(&ioc->transport_cmds.done,
	    karg->IoctlHeader.Timeout*HZ);

	/* Check the command status */
	if (!(ioc->transport_cmds.status & MPT3_CMD_COMPLETE)) {
		printk(MPT3SAS_ERR_FMT "%s(): failure (target addr = 0x%llx, "
		    "function = 0x%x, status = 0x%0x, timeleft = %lu)\n",
		    ioc->name, __func__, (unsigned long long)
		    sas_address, karg->Parameters.Request.bFunction,
		    ioc->transport_cmds.status, timeleft);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2SmpPassthroughRequest_t)/4);
		rc = -EFAULT;
		if (!(ioc->scsih_cmds.status & MPT3_CMD_RESET))
			issue_host_reset = 1;
		goto pci_free;
	}

	/* Check if the response is valid */
	if (!(ioc->transport_cmds.status & MPT3_CMD_REPLY_VALID)) {
		printk(MPT3SAS_ERR_FMT "%s(): target addr = 0x%llx, "
		    "function = 0x%x, status = 0x%x\n",  ioc->name, __func__,
		    (unsigned long long)sas_address,
		    karg->Parameters.Request.bFunction,
		    ioc->transport_cmds.status);
		rc = -EFAULT;
		goto pci_free;
	}

	/* Process the response */
	mpi_reply = ioc->transport_cmds.reply;

	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	if ((ioc_status != MPI2_IOCSTATUS_SUCCESS) &&
	    (ioc_status != MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN)) {
		dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): "
		   "target addr = 0x%llx, function = 0x%x,  IOC Status = 0x%x, "
		   "IOC LogInfo = 0x%x\n", ioc->name, __func__,
		   (unsigned long long)sas_address,
		   karg->Parameters.Request.bFunction,
		   le16_to_cpu(mpi_reply->IOCStatus),
		   le32_to_cpu(mpi_reply->IOCLogInfo)));
		rc = -EFAULT;
		goto pci_free;
	}

	karg->Parameters.bConnectionStatus =
	    _map_sas_status_to_csmi(mpi_reply->SASStatus);

	/* Copy the SMP response data from the DMA buffer */
	response_sz = le16_to_cpu(mpi_reply->ResponseDataLength);
	if (response_sz > sizeof(CSMI_SAS_SMP_RESPONSE)) {
		printk(MPT3SAS_ERR_FMT "%s(): response size(0x%X) larger than"
		    "available memory(0x%lX)\n",  ioc->name, __func__,
		    response_sz, sizeof(CSMI_SAS_SMP_RESPONSE));
		rc = -ENOMEM;
		goto pci_free;
	} else if (response_sz) {
		karg->Parameters.uResponseBytes = response_sz;
		memcpy(&karg->Parameters.Response, data_addr + data_out_sz,
		       response_sz);
	}

	/* Success */
	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

pci_free:
	pci_free_consistent(ioc->pdev, data_out_sz + data_in_sz, data_addr,
	    data_dma);
cmd_not_used:
	ioc->transport_cmds.status = MPT3_CMD_NOT_USED;
unlock:
	mutex_unlock(&ioc->transport_cmds.mutex);
out:
	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	if (issue_host_reset)
		mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	return rc;
}

/**
 * Prototype Routine for the CSMI SAS SSP Passthru command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_ssp_passthru(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_SSP_PASSTHRU_BUFFER *karg)
{
	Mpi2SCSIIORequest_t *mpi_request;
	Mpi2SCSIIOReply_t *mpi_reply = NULL;
	int rc = 0;
	u16 smid;
	unsigned long timeleft;
	void *priv_sense;
	u32 mpi_control;
	u32 sgl_flags;
	void *data_addr = NULL;
	dma_addr_t data_dma = -1;
	u32 data_sz = 0;
	u64 sas_address;
	struct _sas_device sas_device;
	u16 ioc_status;
	int ii;
	u8 response_length;
	u16 wait_state_count;
	u32 ioc_state;
	u8 issue_tm = 0;
	u8 issue_host_reset = 0;
	u32 in_size;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	if (_csmi_valid_port_for_cmd(ioc, &karg->IoctlHeader,
	    karg->Parameters.bPhyIdentifier, karg->Parameters.bPortIdentifier))
		goto out;

	/* MPI doesn't support SSP commands across specific phy's at a specific
	 * rate so a port must be used
	 */
	if (karg->Parameters.bConnectionRate != CSMI_SAS_LINK_RATE_NEGOTIATE) {
		dcsmisasprintk(ioc, printk(KERN_ERR
		    "%s::%s() @%d - must use CSMI_SAS_LINK_RATE_NEGOTIATE\n",
		    __FILE__, __func__, __LINE__));
		goto out;
	}

	/* Find the target SAS device */
	sas_address = be64_to_cpu(*(__be64 *)karg->Parameters.
				  bDestinationSASAddress);
	if (_csmisas_scsih_sas_device_find_by_sas_address(ioc, sas_address,
	    &sas_device) != 0) {
		printk(MPT3SAS_ERR_FMT "%s():%d: device with addr 0x%llx not "
		   "found!\n", ioc->name, __func__, __LINE__,
		   (unsigned long long)sas_address);
		rc = -EFAULT;
		goto out;
	}

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT
	    "%s(): karg->IoctlHeader.Length = %d, "
	    "karg->Parameters.uDataLength = %d\n",
	    ioc->name, __func__, karg->IoctlHeader.Length,
	    karg->Parameters.uDataLength));

	/* data buffer bounds checking */
	in_size = offsetof(CSMI_SAS_SSP_PASSTHRU_BUFFER, bDataBuffer) -
	    sizeof(IOCTL_HEADER) + karg->Parameters.uDataLength;
	if (in_size > karg->IoctlHeader.Length) {
		printk(MPT3SAS_ERR_FMT "%s():%d: data buffer too small "
		   "IoctlHeader.Length = %d, Parameters.uDataLength = %d !\n",
		   ioc->name, __func__, __LINE__, karg->IoctlHeader.Length,
		   karg->Parameters.uDataLength);
		karg->IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		goto out;
	}

	/*
	 * The following is taken from _scsi_send_scsi_io in mpt3sas_scsih.c.
	 * More or less.
	 */

	if (ioc->shost_recovery) {
		printk(MPT3SAS_INFO_FMT "%s():%d: host reset in progress!\n",
		    ioc->name, __func__, __LINE__);
		rc = -EFAULT;
		goto out;
	}

	mutex_lock(&ioc->scsih_cmds.mutex);

	if (ioc->scsih_cmds.status != MPT3_CMD_NOT_USED) {
		printk(MPT3SAS_ERR_FMT "%s():%d: status = 0x%x\n", ioc->name,
		    __func__, __LINE__, ioc->scsih_cmds.status);
		rc = -EFAULT;
		goto unlock;
	}
	ioc->scsih_cmds.status = MPT3_CMD_PENDING;

	wait_state_count = 0;
	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	while (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		if (wait_state_count++ == 10) {
			printk(MPT3SAS_ERR_FMT  "%s: failed due to ioc not "
			    "operational\n", ioc->name, __func__);
			rc = -EFAULT;
			goto out;
		}
		ssleep(1);
		ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
		printk(MPT3SAS_INFO_FMT "%s: waiting for operational "
		    "state(count=%d)\n", ioc->name, __func__, wait_state_count);
	}
	if (wait_state_count)
		printk(MPT3SAS_INFO_FMT "%s: ioc is operational\n", ioc->name,
		    __func__);

	/* Use first reserved smid for passthrough ioctls */
	smid = ioc->shost->can_queue + INTERNAL_SCSIIO_FOR_IOCTL;

	/* Allocate DMA memory and copy the SSP write payload from user memory
	 * to the DMA buffer */
	data_sz = karg->Parameters.uDataLength;
	if (data_sz) {
		data_addr = pci_alloc_consistent(ioc->pdev, data_sz, &data_dma);
		if (!data_addr) {
			printk(MPT3SAS_ERR_FMT "%s():%d: data addr = NULL\n",
			    ioc->name, __func__, __LINE__);
			rc = -ENOMEM;
			goto set_cmd_status;
		}
		if (data_sz && (karg->IoctlHeader.Direction ==
		    CSMI_SAS_DATA_WRITE))
			memcpy(data_addr, karg->bDataBuffer, data_sz);
	}

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->scsih_cmds.smid = smid;

	/* Setup the MPI request frame */
	memset(mpi_request, 0, sizeof(Mpi2SCSIIORequest_t));
	mpi_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
	mpi_request->DevHandle = cpu_to_le16(sas_device.handle);

	/* Set scatter gather flags */
	sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
	    MPI2_SGE_FLAGS_END_OF_LIST);
	if (karg->IoctlHeader.Direction == CSMI_SAS_DATA_WRITE)
		sgl_flags |= MPI2_SGE_FLAGS_HOST_TO_IOC;
	sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;

	/* Direction */
	if (!data_sz) /* handle case when uFlags is incorrectly set */ {
		mpi_control = MPI2_SCSIIO_CONTROL_NODATATRANSFER;
		ioc->build_zero_len_sge(ioc, &mpi_request->SGL);
	} else if (karg->IoctlHeader.Direction == CSMI_SAS_DATA_WRITE) {
		ioc->base_add_sg_single(&mpi_request->SGL, sgl_flags | data_sz,
		    data_dma);
		mpi_control = MPI2_SCSIIO_CONTROL_WRITE;

	} else if (karg->IoctlHeader.Direction == CSMI_SAS_DATA_READ) {
		ioc->base_add_sg_single(&mpi_request->SGL, sgl_flags | data_sz,
		    data_dma);
		mpi_control = MPI2_SCSIIO_CONTROL_READ;

	} else {
		mpi_control = MPI2_SCSIIO_CONTROL_NODATATRANSFER;
		ioc->build_zero_len_sge(ioc, &mpi_request->SGL);
	}

	mpi_request->Control = cpu_to_le32(mpi_control |
	    MPI2_SCSIIO_CONTROL_SIMPLEQ);
	mpi_request->DataLength = cpu_to_le32(data_sz);
	mpi_request->MsgFlags = MPI2_SCSIIO_MSGFLAGS_SYSTEM_SENSE_ADDR;
	mpi_request->SenseBufferLength = SCSI_SENSE_BUFFERSIZE;
	mpi_request->SenseBufferLowAddress =
	    mpt3sas_base_get_sense_buffer_dma(ioc, smid);
	priv_sense = mpt3sas_base_get_sense_buffer(ioc, smid);
	mpi_request->SGLOffset0 = offsetof(Mpi2SCSIIORequest_t, SGL) / 4;
	mpi_request->SGLFlags = cpu_to_le16(MPI2_SCSIIO_SGLFLAGS_TYPE_MPI +
	    MPI2_SCSIIO_SGLFLAGS_SYSTEM_ADDR);
	mpi_request->IoFlags = cpu_to_le16(karg->Parameters.bCDBLength);
	memcpy(mpi_request->LUN, karg->Parameters.bLun, 8);
	memcpy(mpi_request->CDB.CDB32, karg->Parameters.bCDB,
	    karg->Parameters.bCDBLength);

	/* Send the request */
	if (ioc->logging_level & MPT_DEBUG_CSMISAS) {
		printk(MPT3SAS_INFO_FMT "%s(): sending request "
		    "(target addr = 0x%llx, SCSI cmd = {",
		    ioc->name, __func__, (unsigned long long)sas_address);
		for (ii = 0; ii < karg->Parameters.bCDBLength; ii++) {
			printk("%02x", karg->Parameters.bCDB[ii]);
			if (ii < karg->Parameters.bCDBLength - 1)
				printk(" ");
		}
		printk("}, timeout = %d sec)\n", karg->IoctlHeader.Timeout);
	}

	init_completion(&ioc->scsih_cmds.done);
	ioc->put_smid_scsi_io(ioc, smid, sas_device.handle);
	timeleft = wait_for_completion_timeout(&ioc->scsih_cmds.done,
	    karg->IoctlHeader.Timeout*HZ);

	/* Check the command status */
	if (!(ioc->scsih_cmds.status & MPT3_CMD_COMPLETE)) {
		printk(MPT3SAS_ERR_FMT "%s(): failure (target addr = 0x%llx, "
		    "SCSI cmd = 0x%x, status = 0x%x, timeleft = %lu)\n",
		    ioc->name, __func__, (unsigned long long)sas_address,
		    mpi_request->CDB.CDB32[0], ioc->scsih_cmds.status,
		    timeleft);
		_debug_dump_mf(mpi_request, sizeof(Mpi2SCSIIORequest_t)/4);
		rc = -EFAULT;
		if (!(ioc->scsih_cmds.status & MPT3_CMD_RESET))
			issue_tm = 1;
		goto issue_target_reset;
	}

	/* Initialize the CSMI reply */
	memset(&karg->Status, 0, sizeof(CSMI_SAS_SSP_PASSTHRU_STATUS));
	karg->Status.bConnectionStatus = CSMI_SAS_OPEN_ACCEPT;
	karg->Status.bDataPresent = CSMI_SAS_SSP_NO_DATA_PRESENT;
	karg->Status.bSSPStatus = CSMI_SAS_SSP_STATUS_COMPLETED;
	karg->Status.bStatus = GOOD;
	karg->Status.uDataBytes = data_sz;

	/* Process the respone */
	if (ioc->scsih_cmds.status & MPT3_CMD_REPLY_VALID) {
		mpi_reply = ioc->scsih_cmds.reply;
		karg->Status.bStatus = mpi_reply->SCSIStatus;
		karg->Status.uDataBytes = le32_to_cpu(mpi_reply->TransferCount);

		ioc_status = le16_to_cpu(mpi_reply->IOCStatus) &
		    MPI2_IOCSTATUS_MASK;

		if (mpi_reply->SCSIState == MPI2_SCSI_STATE_AUTOSENSE_VALID) {
			response_length =
			    le32_to_cpu(mpi_reply->SenseCount) & 0xff;
			karg->Status.bDataPresent =
			    CSMI_SAS_SSP_SENSE_DATA_PRESENT;
			karg->Status.bResponseLength[0] = response_length;
			memcpy(karg->Status.bResponse, priv_sense,
			    response_length);
			dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): "
			   "[sense_key,asc,asq]: [0x%02x,0x%02x,0x%02x]\n",
			   ioc->name, __func__, ((u8 *)priv_sense)[2] & 0xf,
			   ((u8 *)priv_sense)[12], ((u8 *)priv_sense)[13]));

		} else if (mpi_reply->SCSIState ==
			   MPI2_SCSI_STATE_RESPONSE_INFO_VALID) {
			response_length =
			    sizeof(mpi_reply->ResponseInfo) & 0xff;
			karg->Status.bDataPresent =
			    CSMI_SAS_SSP_RESPONSE_DATA_PRESENT;
			karg->Status.bResponseLength[0] = response_length;
			for (ii = 0; ii < response_length; ii++) {
				karg->Status.bResponse[ii] =
				    ((u8 *)&mpi_reply->
				    ResponseInfo)[response_length-1-ii];
			}

		} else if ((ioc_status != MPI2_IOCSTATUS_SUCCESS) &&
			   (ioc_status !=
				MPI2_IOCSTATUS_SCSI_RECOVERED_ERROR) &&
			   (ioc_status != MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN)) {
			printk(MPT3SAS_WARN_FMT "%s(): target addr = 0x%llx, "
			    "SCSI cmd = 0x%x, IOC Status = 0x%x, IOC LogInfo = "
			    "0x%x\n", ioc->name, __func__,
			       (unsigned long long)sas_address,
			       mpi_request->CDB.CDB32[0],
			       le16_to_cpu(mpi_reply->IOCStatus),
			       le32_to_cpu(mpi_reply->IOCLogInfo));
		}
	}

#if 0
	/* Dump the data buffer */
	printk(KERN_DEBUG "arg = %p\n", arg);
	printk(KERN_DEBUG "off = %lu\n",
	       offsetof(CSMI_SAS_SSP_PASSTHRU_BUFFER, bDataBuffer));
	printk(KERN_DEBUG "arg + off = %p\n", arg +
	       offsetof(CSMI_SAS_SSP_PASSTHRU_BUFFER, bDataBuffer));
	printk(KERN_DEBUG "sof = %lu\n",
	       sizeof(CSMI_SAS_SSP_PASSTHRU_BUFFER));
	printk(KERN_DEBUG "arg + sof = %p\n",
	       arg + sizeof(CSMI_SAS_SSP_PASSTHRU_BUFFER));
	_debug_dump_mf(data_addr, data_sz / 4);
#endif

	/* Copy the SSP read payload from the DMA buffer to user memory */
	if (data_sz && (karg->IoctlHeader.Direction == CSMI_SAS_DATA_READ)) {
		karg->Status.uDataBytes =
		    min(le32_to_cpu(mpi_reply->TransferCount), data_sz);
		memcpy(karg->bDataBuffer, data_addr, data_sz);
	}

	/* Success */
	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

 issue_target_reset:
	if (issue_tm) {
		printk(MPT3SAS_INFO_FMT "issue target reset: handle"
		    "(0x%04x)\n", ioc->name, sas_device.handle);
		mpt3sas_scsih_issue_locked_tm(ioc, sas_device.handle, 0, 0, 0,
		    MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET, smid, 30, 0);
		if (ioc->scsih_cmds.status & MPT3_CMD_COMPLETE) {
			printk(MPT3SAS_INFO_FMT "target reset completed: handle"
			    "(0x%04x)\n", ioc->name, sas_device.handle);
			rc = -EAGAIN;
		} else {
			printk(MPT3SAS_INFO_FMT "target reset didn't complete:"
			    " handle(0x%04x)\n", ioc->name, sas_device.handle);
			issue_host_reset = 1;
		}
		karg->Status.bSSPStatus = CSMI_SAS_SSP_STATUS_RETRY;
	}
	pci_free_consistent(ioc->pdev, data_sz, data_addr, data_dma);
 set_cmd_status:
	ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
 unlock:
	mutex_unlock(&ioc->scsih_cmds.mutex);
 out:
	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	if (issue_host_reset)
		mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	return rc;
}

/**
 * Prototype Routine for the CSMI SAS STP Passthru command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_stp_passthru(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_STP_PASSTHRU_BUFFER *karg)
{
	int rc = 0;
	u64 sas_address;
	struct _sas_device sas_device;
	Mpi2SataPassthroughRequest_t *mpi_request;
	Mpi2SataPassthroughReply_t *mpi_reply;
	u16 smid;
	u16 wait_state_count;
	u32 ioc_state;
	u16 ioc_status;
	u32 sgl_flags;
	void *data_addr = NULL;
	dma_addr_t data_dma = -1;
	u32 data_sz = 0;
	unsigned long timeleft;
	u8 issue_tm = 0;
	u8 issue_host_reset = 0;
	u32 in_size;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	if (ioc->shost_recovery) {
		printk(MPT3SAS_INFO_FMT "%s():%d: host reset in progress!\n",
		    ioc->name, __func__, __LINE__);
		rc = -EFAULT;
		goto out;
	}

	if (_csmi_valid_port_for_cmd(ioc, &karg->IoctlHeader,
	    karg->Parameters.bPhyIdentifier, karg->Parameters.bPortIdentifier))
		goto out;

	/* data buffer bounds checking */
	in_size = offsetof(CSMI_SAS_STP_PASSTHRU_BUFFER, bDataBuffer) -
	    sizeof(IOCTL_HEADER) + karg->Parameters.uDataLength;
	if (in_size > karg->IoctlHeader.Length) {
		printk(MPT3SAS_ERR_FMT "%s():%d: data buffer too small "
		   "IoctlHeader.Length = %d, Parameters.uDataLength = %d !\n",
		   ioc->name, __func__, __LINE__, karg->IoctlHeader.Length,
		   karg->Parameters.uDataLength);
		karg->IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		goto out;
	}

	data_sz = karg->Parameters.uDataLength;

	sas_address =
	    be64_to_cpu(*(__be64 *)karg->Parameters.bDestinationSASAddress);
	rc = _csmisas_scsih_sas_device_find_by_sas_address(ioc, sas_address,
	    &sas_device);

	/* check that this is an STP or SATA target device
	 */
	if (rc != 0 || (!(sas_device.device_info &
	     MPI2_SAS_DEVICE_INFO_SATA_DEVICE))) {
		dcsmisasprintk(ioc, printk(KERN_WARNING
		    ": NOT A SATA DEVICE\n"));
		karg->IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		goto out;
	}

	mutex_lock(&ioc->scsih_cmds.mutex);

	if (ioc->scsih_cmds.status != MPT3_CMD_NOT_USED) {
		printk(MPT3SAS_ERR_FMT "%s():%d: status = 0x%x\n", ioc->name,
		    __func__, __LINE__, ioc->scsih_cmds.status);
		rc = -EFAULT;
		goto unlock;
	}
	ioc->scsih_cmds.status = MPT3_CMD_PENDING;

	wait_state_count = 0;
	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	while (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {
		if (wait_state_count++ == 10) {
			printk(MPT3SAS_ERR_FMT "%s: failed due to ioc not "
			    "operational\n", ioc->name, __func__);
			rc = -EFAULT;
			goto set_cmd_status;
		}
		ssleep(1);
		ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
		printk(MPT3SAS_INFO_FMT "%s: waiting for operational "
		    "state(count=%d)\n", ioc->name, __func__, wait_state_count);
	}
	if (wait_state_count)
		printk(MPT3SAS_INFO_FMT "%s: ioc is operational\n",  ioc->name,
		    __func__);

	smid = mpt3sas_base_get_smid_scsiio(ioc, ioc->scsih_cb_idx, NULL);
	if (!smid) {
		printk(MPT3SAS_ERR_FMT "%s():%d: smid = 0\n", ioc->name,
		    __func__, __LINE__);
		rc = -EFAULT;
		goto set_cmd_status;
	}

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);

	memset(mpi_request, 0, sizeof(Mpi2SataPassthroughRequest_t));
	mpi_request->Function = MPI2_FUNCTION_SATA_PASSTHROUGH;
	mpi_request->PassthroughFlags = cpu_to_le16(karg->Parameters.uFlags);
	mpi_request->DevHandle = cpu_to_le16(sas_device.handle);
	mpi_request->DataLength = cpu_to_le32(data_sz);
	memcpy(mpi_request->CommandFIS, karg->Parameters.bCommandFIS, 20);

	sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
	    MPI2_SGE_FLAGS_END_OF_LIST);
	if (karg->IoctlHeader.Direction == CSMI_SAS_DATA_WRITE)
		sgl_flags |= MPI2_SGE_FLAGS_HOST_TO_IOC;
	sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;

	sgl_flags |= data_sz;
	if (data_sz > 0) {
		data_addr = pci_alloc_consistent(ioc->pdev, data_sz, &data_dma);

		if (data_addr == NULL) {
			dcsmisasprintk(ioc, printk(KERN_ERR
			    ": pci_alloc_consistent: FAILED\n"));
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			mpt3sas_base_free_smid(ioc, smid);
			goto set_cmd_status;
		}

		ioc->base_add_sg_single(&mpi_request->SGL, sgl_flags, data_dma);
		if (karg->IoctlHeader.Direction == CSMI_SAS_DATA_WRITE)
			memcpy(data_addr, karg->bDataBuffer, data_sz);
	} else
		ioc->build_zero_len_sge(ioc, &mpi_request->SGL);

	init_completion(&ioc->scsih_cmds.done);
	ioc->scsih_cmds.smid = smid;
	ioc->put_smid_scsi_io(ioc, smid, sas_device.handle);
	timeleft = wait_for_completion_timeout(&ioc->scsih_cmds.done,
	    karg->IoctlHeader.Timeout * HZ);

	/* Check the command status */
	if (!(ioc->scsih_cmds.status & MPT3_CMD_COMPLETE)) {
		printk(MPT3SAS_ERR_FMT "%s(): failure (target addr = 0x%llx, "
		    "timeleft = %lu)\n", ioc->name, __func__,
		    (unsigned long long)sas_address, timeleft);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2SataPassthroughRequest_t)/4);
		rc = -EFAULT;
		if (!(ioc->scsih_cmds.status & MPT3_CMD_RESET))
			issue_tm = 1;
		goto issue_target_reset;
	}

	memset(&karg->Status, 0, sizeof(CSMI_SAS_STP_PASSTHRU_STATUS));

	if ((ioc->scsih_cmds.status & MPT3_CMD_REPLY_VALID) == 0) {
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    ": STP Passthru: oh no, there is no reply!!"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto set_cmd_status;
	}

	mpi_reply = ioc->scsih_cmds.reply;
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;

	if (ioc_status != MPI2_IOCSTATUS_SUCCESS &&
	    ioc_status != MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		dcsmisasprintk(ioc, printk(KERN_DEBUG ": STP Passthru: "));
		dcsmisasprintk(ioc, printk("IOCStatus=0x%X IOCLogInfo=0x%X "
		    "SASStatus=0x%X\n",  le16_to_cpu(mpi_reply->IOCStatus),
		    le32_to_cpu(mpi_reply->IOCLogInfo),  mpi_reply->SASStatus));
	}

	karg->Status.bConnectionStatus =
	    _map_sas_status_to_csmi(mpi_reply->SASStatus);
	memcpy(karg->Status.bStatusFIS, mpi_reply->StatusFIS, 20);

	memset(karg->Status.uSCR, 0, 64);
	karg->Status.uSCR[0] = le32_to_cpu(mpi_reply->StatusControlRegisters);

	if (data_sz && (karg->IoctlHeader.Direction == CSMI_SAS_DATA_READ)) {
		karg->Status.uDataBytes =
		    min(le32_to_cpu(mpi_reply->TransferCount), data_sz);
		memcpy(karg->bDataBuffer, data_addr, data_sz);
	}

	if (data_addr)
		pci_free_consistent(ioc->pdev, data_sz, (u8 *)data_addr,
		    data_dma);

	/* Success */
	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

 issue_target_reset:
	if (issue_tm) {
		printk(MPT3SAS_INFO_FMT "issue target reset: handle"
		    "(0x%04x)\n", ioc->name, sas_device.handle);
		mpt3sas_scsih_issue_locked_tm(ioc, sas_device.handle, 0, 0, 0,
		    MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET, 0, 30, 0);
		if (ioc->scsih_cmds.status & MPT3_CMD_COMPLETE) {
			printk(MPT3SAS_INFO_FMT "target reset completed: handle"
			    "(0x%04x)\n", ioc->name, sas_device.handle);
			rc = -EAGAIN;
		} else {
			printk(MPT3SAS_INFO_FMT "target reset didn't complete:"
			    " handle(0x%04x)\n", ioc->name, sas_device.handle);
			issue_host_reset = 1;
		}
	}

 set_cmd_status:
	ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
 unlock:
	mutex_unlock(&ioc->scsih_cmds.mutex);
 out:
	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	if (issue_host_reset)
		mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	return rc;
}

/**
 * Prototype Routine for the CSMI SAS Firmware Download command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_firmware_download(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER *karg)
{
	int rc = 0;
	int retval;
	pMpi2FWImageHeader_t pFwHeader = NULL;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
	karg->Information.usStatus = CSMI_SAS_FWD_REJECT;
	karg->Information.usSeverity = CSMI_SAS_FWD_ERROR;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	/* check the incoming frame */
	if ((karg->Information.uBufferLength +
	    sizeof(CSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER)) >
	    karg->IoctlHeader.Length) {
		karg->IoctlHeader.ReturnCode =
			CSMI_SAS_STATUS_INVALID_PARAMETER;
		goto out;
	}

	/* return if user specific soft reset or to validate fw */
	if (karg->Information.uDownloadFlags &
	    (CSMI_SAS_FWD_SOFT_RESET | CSMI_SAS_FWD_VALIDATE)) {
		karg->IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		goto out;
	}

	/* get a pointer to our bDataBuffer */
	pFwHeader = (pMpi2FWImageHeader_t)karg->bDataBuffer;

	/* verify the fimware signature */
	if (!((pFwHeader->Signature0 == MPI2_FW_HEADER_SIGNATURE0) &&
	    (pFwHeader->Signature1 == MPI2_FW_HEADER_SIGNATURE1) &&
	    (pFwHeader->Signature2 == MPI2_FW_HEADER_SIGNATURE2)))
		goto out;

	if (_ctl_do_fw_download(ioc, karg->bDataBuffer,
	    karg->Information.uBufferLength) != 0) {
		printk(KERN_ERR "_ctl_do_fw_download failed\n");
		karg->Information.usSeverity = CSMI_SAS_FWD_FATAL;
		karg->Information.usStatus = CSMI_SAS_FWD_FAILED;
		goto out;
	} else {
		printk(KERN_ERR "_ctl_do_fw_download succeeded\n");
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
		karg->Information.usSeverity = CSMI_SAS_FWD_INFORMATION;
		karg->Information.usStatus = CSMI_SAS_FWD_SUCCESS;
	}

	/* reset HBA to load new fw if desired*/
	if (karg->Information.uDownloadFlags & CSMI_SAS_FWD_HARD_RESET) {
		scsi_block_requests(ioc->shost);
		retval = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
		scsi_unblock_requests(ioc->shost);
		printk(MPT3SAS_INFO_FMT "%s: host reset %s.\n", ioc->name,
		   __func__, (retval < 0) ? "failed" : "succeeded");
		if (retval < 0) {
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			karg->Information.usSeverity = CSMI_SAS_FWD_FATAL;
			karg->Information.usStatus = CSMI_SAS_FWD_FAILED;
		}
		goto out;
	}

 out:
	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI SAS Get RAID Info command.
 * Note: This function is required for the SAS agent (cmasasd)
 * to correctly process physically removed drives from DotHill
 * enclosures due to the behaviour of the DH FW. The function
 * is required even if the controller or FW don't support RAID.
 *
 * Outputs:     None.
 * Return:      0 if successful
 *              -EFAULT if data unavailable
 *              -ENODEV if no such device/adapter
 */
static int
_csmisas_get_raid_info(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_RAID_INFO_BUFFER *karg)
{
	int rc = 0;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	/* Don't need to return any data so just clear the struct */
	memset(&karg->Information, 0, sizeof(CSMI_SAS_RAID_INFO));

	/* Success */
	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI SAS Get RAID Config command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_get_raid_config(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_RAID_CONFIG_BUFFER *karg)
{
	int rc = 0;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));


	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI SAS Get RAID Features command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_get_raid_features(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_RAID_FEATURES_BUFFER *karg)
{
	int rc = 0;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));


	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI SAS Set RAID Control command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_set_raid_control(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_RAID_CONTROL_BUFFER *karg)
{
	int rc = 0;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));


	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI SAS Get Raid Element.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_get_raid_element(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_RAID_ELEMENT_BUFFER *karg)
{
	int rc = 0;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI SAS Set Raid Operation
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_set_raid_operation(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_RAID_SET_OPERATION_BUFFER *karg)
{
	int rc = 0;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));


	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}


/**
 * Prototype Routine for the CSMI SAS Task Managment Config command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_task_managment(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_SSP_TASK_IU_BUFFER *karg)
{
	struct _sas_device sas_device;
	int rc = 0;
	u16 handle = 0;
	u8 task_type;
	u16 task_mid = 0;
	struct scsi_cmnd *scmd = NULL;

	memset(&karg->Status, 0, sizeof(CSMI_SAS_SSP_PASSTHRU_STATUS));
	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	switch (karg->Parameters.uInformation) {
	case CSMI_SAS_SSP_TEST:
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    "TM request for test purposes\n"));
		break;
	case CSMI_SAS_SSP_EXCEEDED:
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    "TM request due to timeout\n"));
		break;
	case CSMI_SAS_SSP_DEMAND:
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    "TM request demanded by app\n"));
		break;
	case CSMI_SAS_SSP_TRIGGER:
		dcsmisasprintk(ioc, printk(KERN_DEBUG
		    "TM request sent to trigger event\n"));
		break;
	}

	if (karg->Parameters.bHostIndex != ioc->shost->host_no) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_NO_DEVICE_ADDRESS;
		goto out;
	}

	/* search for device based on bus/target */
	rc = _csmisas_sas_device_find_by_id(ioc, karg->Parameters.bTargetId,
	    karg->Parameters.bPathId, &sas_device);
	if (rc != 0) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_NO_DEVICE_ADDRESS;
		goto out;
	}

	handle = sas_device.handle;

	/* try to catch an error
	 */
	if ((karg->Parameters.uFlags & CSMI_SAS_TASK_IU) &&
	    (karg->Parameters.uFlags & CSMI_SAS_HARD_RESET_SEQUENCE))
		goto out;

	if (karg->Parameters.uFlags & CSMI_SAS_TASK_IU) {
		switch (karg->Parameters.bTaskManagementFunction) {

		case CSMI_SAS_SSP_ABORT_TASK:
			if (_csmisas_scsi_lookup_find_by_tag(ioc,
			    karg->Parameters.uQueueTag, &task_mid,
			    &scmd)) {
				karg->IoctlHeader.ReturnCode =
				    CSMI_SAS_STATUS_SUCCESS;
				karg->Status.bSSPStatus =
				    CSMI_SAS_SSP_STATUS_NO_TAG;
				goto out;
			}
			task_type = MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK;
			break;
		case CSMI_SAS_SSP_ABORT_TASK_SET:
			task_type = MPI2_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET;
			break;
		case CSMI_SAS_SSP_CLEAR_TASK_SET:
			task_type = MPI2_SCSITASKMGMT_TASKTYPE_CLEAR_TASK_SET;
			break;
		case CSMI_SAS_SSP_LOGICAL_UNIT_RESET:
			task_type =
			    MPI2_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET;
			break;
		case CSMI_SAS_SSP_CLEAR_ACA:
			task_type =
			    MPI2_SCSITASKMGMT_TASKTYPE_CLR_ACA;
			break;
		case CSMI_SAS_SSP_QUERY_TASK:
			if (_csmisas_scsi_lookup_find_by_tag(ioc,
			    karg->Parameters.uQueueTag, &task_mid,
			    &scmd)) {
				karg->IoctlHeader.ReturnCode =
				    CSMI_SAS_STATUS_SUCCESS;
				karg->Status.bSSPStatus =
				    CSMI_SAS_SSP_STATUS_NO_TAG;
				goto out;
			}
			task_type =
			    MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK;
			break;
		default:
			goto out;
		}
	} else if (karg->Parameters.uFlags & CSMI_SAS_HARD_RESET_SEQUENCE)
		task_type = MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	else
		goto out;

	if (mpt3sas_scsih_issue_locked_tm(ioc, handle, karg->Parameters.bPathId,
	    karg->Parameters.bTargetId, karg->Parameters.bLun, task_type,
	    task_mid, karg->IoctlHeader.Timeout,
	    0)
	    == SUCCESS) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
		karg->Status.bSSPStatus = CSMI_SAS_SSP_STATUS_COMPLETED;
	} else {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		karg->Status.bSSPStatus = CSMI_SAS_SSP_STATUS_FATAL_ERROR;
	}

 out:
	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/** Prototype Routine for the CSMI SAS Phy Control command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_phy_control(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_PHY_CONTROL_BUFFER *karg)
{
	int rc = 0;
	int sz;
	Mpi2SasIoUnitControlReply_t sas_iounit_ctr_reply;
	Mpi2SasIoUnitControlRequest_t sas_iounit_ctr;
	Mpi2SasIOUnitPage1_t *sas_iounit_pg1 = NULL;
	Mpi2ConfigReply_t sas_iounit_pg1_reply;
	u32 ioc_status;
	u8 phy_number;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
	phy_number = karg->bPhyIdentifier;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	if (_csmi_valid_phy(ioc, &karg->IoctlHeader, phy_number))
		goto out;

	sz = offsetof(Mpi2SasIOUnitPage1_t, PhyData) + (ioc->sas_hba.num_phys *
	    sizeof(Mpi2SasIOUnit1PhyData_t));
	sas_iounit_pg1 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg1) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -ENOMEM;
		goto out;
	}

	if ((mpt3sas_config_get_sas_iounit_pg1(ioc, &sas_iounit_pg1_reply,
	    sas_iounit_pg1, sz))) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -ENXIO;
		goto out;
	}

	ioc_status = le16_to_cpu(sas_iounit_pg1_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT3SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -EIO;
		goto out;
	}

	switch (karg->uFunction) {
	case CSMI_SAS_PC_LINK_RESET:
	case CSMI_SAS_PC_HARD_RESET:
		if ((karg->uLinkFlags & CSMI_SAS_PHY_ACTIVATE_CONTROL) &&
		    (karg->usLengthOfControl >= sizeof(CSMI_SAS_PHY_CONTROL)) &&
		    (karg->bNumberOfControls > 0)) {
			if (karg->Control[0].bRate ==
			   CSMI_SAS_LINK_RATE_1_5_GBPS) {
				sas_iounit_pg1->PhyData[phy_number].
				    MaxMinLinkRate =
				    MPI2_SASIOUNIT1_MAX_RATE_1_5 |
				    MPI2_SASIOUNIT1_MIN_RATE_1_5;
			} else if (karg->Control[0].bRate ==
			   CSMI_SAS_LINK_RATE_3_0_GBPS) {
				sas_iounit_pg1->PhyData[phy_number].
				    MaxMinLinkRate =
				    MPI2_SASIOUNIT1_MAX_RATE_3_0 |
				    MPI2_SASIOUNIT1_MIN_RATE_3_0;
			} else if (karg->Control[0].bRate ==
			   CSMI_SAS_LINK_RATE_6_0_GBPS) {
				sas_iounit_pg1->PhyData[phy_number].
				    MaxMinLinkRate =
				    MPI2_SASIOUNIT1_MAX_RATE_6_0 |
				    MPI2_SASIOUNIT1_MIN_RATE_6_0;
			} else {
				printk(MPT3SAS_ERR_FMT
				    "failure at %s:%d/%s()! Unsupported "
				    "Link Rate\n", ioc->name,
				    __FILE__, __LINE__, __func__);
				goto out;
			}

			sas_iounit_pg1->PhyData[phy_number].PhyFlags &=
			    ~MPI2_SASIOUNIT1_PHYFLAGS_PHY_DISABLE;

			mpt3sas_config_set_sas_iounit_pg1(ioc,
			    &sas_iounit_pg1_reply, sas_iounit_pg1, sz);

			memset(&sas_iounit_ctr, 0,
			    sizeof(Mpi2SasIoUnitControlRequest_t));
			sas_iounit_ctr.Function =
			    MPI2_FUNCTION_SAS_IO_UNIT_CONTROL;
			sas_iounit_ctr.Operation = karg->uFunction ==
			    CSMI_SAS_PC_LINK_RESET ? MPI2_SAS_OP_PHY_LINK_RESET
			    : MPI2_SAS_OP_PHY_HARD_RESET;
			sas_iounit_ctr.PhyNum = phy_number;

			if ((mpt3sas_base_sas_iounit_control(ioc,
			    &sas_iounit_ctr_reply, &sas_iounit_ctr))) {
				printk(MPT3SAS_ERR_FMT
				    "failure at %s:%d/%s()!\n", ioc->name,
				    __FILE__, __LINE__, __func__);
				goto out;
			}
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
		}
		break;
	case CSMI_SAS_PC_PHY_DISABLE:
		if (karg->usLengthOfControl || karg->bNumberOfControls) {
			karg->IoctlHeader.ReturnCode =
			    CSMI_SAS_STATUS_INVALID_PARAMETER;
			break;
		}
		sas_iounit_pg1->PhyData[phy_number].PhyFlags |=
		    MPI2_SASIOUNIT1_PHYFLAGS_PHY_DISABLE;

		mpt3sas_config_set_sas_iounit_pg1(ioc,
		    &sas_iounit_pg1_reply, sas_iounit_pg1, sz);

		memset(&sas_iounit_ctr, 0,
		    sizeof(Mpi2SasIoUnitControlRequest_t));
		sas_iounit_ctr.Function =
		    MPI2_FUNCTION_SAS_IO_UNIT_CONTROL;
		sas_iounit_ctr.Operation = MPI2_SAS_OP_PHY_HARD_RESET;
		sas_iounit_ctr.PhyNum = phy_number;

		if ((mpt3sas_base_sas_iounit_control(ioc,
		    &sas_iounit_ctr_reply, &sas_iounit_ctr))) {
			printk(MPT3SAS_ERR_FMT
			    "failure at %s:%d/%s()!\n", ioc->name,
			    __FILE__, __LINE__, __func__);
			goto out;
		}
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
		break;
	case CSMI_SAS_PC_GET_PHY_SETTINGS:
		if (karg->usLengthOfControl || karg->bNumberOfControls) {
			karg->IoctlHeader.ReturnCode =
			    CSMI_SAS_STATUS_INVALID_PARAMETER;
			break;
		}
		if (karg->IoctlHeader.Length
		    < offsetof(CSMI_SAS_PHY_CONTROL_BUFFER, Control) +
		    (4 * sizeof(CSMI_SAS_PHY_CONTROL))) {
			karg->IoctlHeader.ReturnCode =
			    CSMI_SAS_STATUS_INVALID_PARAMETER;
			break;
		}
		karg->usLengthOfControl = sizeof(CSMI_SAS_PHY_CONTROL);
		karg->bNumberOfControls = 6;
		karg->Control[0].bType = CSMI_SAS_SAS;
		karg->Control[0].bRate = CSMI_SAS_LINK_RATE_1_5_GBPS;
		karg->Control[1].bType = CSMI_SAS_SAS;
		karg->Control[1].bRate = CSMI_SAS_LINK_RATE_3_0_GBPS;
		karg->Control[2].bType = CSMI_SAS_SAS;
		karg->Control[2].bRate = CSMI_SAS_LINK_RATE_6_0_GBPS;
		karg->Control[3].bType = CSMI_SAS_SATA;
		karg->Control[3].bRate = CSMI_SAS_LINK_RATE_1_5_GBPS;
		karg->Control[4].bType = CSMI_SAS_SATA;
		karg->Control[4].bRate = CSMI_SAS_LINK_RATE_3_0_GBPS;
		karg->Control[5].bType = CSMI_SAS_SATA;
		karg->Control[5].bRate = CSMI_SAS_LINK_RATE_6_0_GBPS;
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
		break;
	default:
		break;
	}

 out:
	kfree(sas_iounit_pg1);
	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/**
 * Prototype Routine for the CSMI SAS Get Connector info command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_get_connector_info(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_CONNECTOR_INFO_BUFFER *karg)
{
	int rc = 0;
	int i;
	u16 sz;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2ManufacturingPage7_t *manu_pg7;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	sz = offsetof(Mpi2ManufacturingPage7_t, ConnectorInfo) +
	    (ioc->sas_hba.num_phys * sizeof(MPI2_MANPAGE7_CONNECTOR_INFO));

	manu_pg7 = kzalloc(sz, GFP_KERNEL);
	if (!manu_pg7) {
		printk(KERN_ERR "%s@%d::%s() - Unable to alloc @ %p\n",
		    __FILE__, __LINE__, __func__, manu_pg7);
		goto out;
	}

	/* initialize the array */
	for (i = 0; i < 32; i++) {
		karg->Reference[i].uPinout = CSMI_SAS_CON_UNKNOWN;
		strcpy(karg->Reference[i].bConnector, "");
		karg->Reference[i].bLocation = CSMI_SAS_CON_UNKNOWN;
	}

	if (!mpt3sas_config_get_manufacturing_pg7(ioc, &mpi_reply, manu_pg7,
	    sz)) {
		for (i = 0; i < ioc->sas_hba.num_phys; i++) {
			karg->Reference[i].uPinout =
			    le32_to_cpu(manu_pg7->ConnectorInfo[i].Pinout);
			strncpy(karg->Reference[i].bConnector,
			    manu_pg7->ConnectorInfo[i].Connector, 16);
			karg->Reference[i].bLocation =
			    manu_pg7->ConnectorInfo[i].Location;
		}
	}

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
	kfree(manu_pg7);
 out:
	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}

/* supporting routines for CSMI SAS Get location command. */
static void
_csmisas_fill_location_data(struct MPT3SAS_ADAPTER *ioc,
	struct _sas_device *sas_device, u8 opcode,
	CSMI_SAS_LOCATION_IDENTIFIER *location_ident)
{

	location_ident->bLocationFlags |= CSMI_SAS_LOCATE_SAS_ADDRESS_VALID;
	*(__be64 *)location_ident->bSASAddress =
	    cpu_to_be64(sas_device->sas_address);

	location_ident->bLocationFlags |= CSMI_SAS_LOCATE_SAS_LUN_VALID;
	memset(location_ident->bSASLun, 0, sizeof(location_ident->bSASLun));

	location_ident->bLocationFlags |=
	    CSMI_SAS_LOCATE_ENCLOSURE_IDENTIFIER_VALID;
	if (sas_device->enclosure_logical_id)
		*(__be64 *)location_ident->bEnclosureIdentifier =
		    cpu_to_be64(sas_device->enclosure_logical_id);
	else
		memcpy(location_ident->bEnclosureIdentifier, "Internal", 8);

	location_ident->bLocationFlags |= CSMI_SAS_LOCATE_ENCLOSURE_NAME_VALID;
	strcpy(location_ident->bEnclosureName, "Not Supported");

	location_ident->bLocationFlags |= CSMI_SAS_LOCATE_LOCATION_STATE_VALID;
	location_ident->bLocationState = CSMI_SAS_LOCATE_UNKNOWN;

	location_ident->bLocationFlags |= CSMI_SAS_LOCATE_BAY_IDENTIFIER_VALID;
	location_ident->bBayIdentifier = sas_device->slot;
}

/* supporting routines for CSMI SAS Get location command. */
static int
_csmisas_fill_location_data_raid(struct MPT3SAS_ADAPTER *ioc, u16 handle,
	CSMI_SAS_GET_LOCATION_BUFFER *karg)
{
	struct _raid_device raid_device;
	struct _sas_device *sas_device;
	unsigned long flags;
	int rc;

	rc = _csmisas_raid_device_find_by_handle(ioc, handle, &raid_device);
	if (rc)
		return -ENODEV;

	karg->bNumberOfLocationIdentifiers = 0;
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_for_each_entry(sas_device, &ioc->sas_device_list, list) {
		if (sas_device->volume_wwid != raid_device.wwid)
			continue;
		_csmisas_fill_location_data(ioc, sas_device, karg->bIdentify,
		    &karg->Location[karg->bNumberOfLocationIdentifiers]);
		karg->bNumberOfLocationIdentifiers++;
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	return 0;
}

/**
 * Prototype Routine for the CSMI SAS Get location command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
_csmisas_get_location(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_GET_LOCATION_BUFFER *karg)
{
	int rc = 0;
	struct _sas_device sas_device;
	struct _raid_device raid_device;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): enter\n", ioc->name,
	   __func__));

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_INVALID_PARAMETER;
	if (karg->bLengthOfLocationIdentifier !=
	    sizeof(CSMI_SAS_LOCATION_IDENTIFIER))
		goto out;

	/* invalid for channel > 1 */
	if (karg->bPathId > 1) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_NO_DEVICE_ADDRESS;
		goto out;
	}

	if (karg->bHostIndex != ioc->shost->host_no) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_NO_DEVICE_ADDRESS;
		goto out;
	}

	/* volumes are on channel = 1 */
	if (karg->bPathId == 1) {
		rc = _csmisas_raid_device_find_by_id(ioc, karg->bTargetId,
		    karg->bPathId, &raid_device);
		if (rc != 0) {
			karg->IoctlHeader.ReturnCode =
			    CSMI_SAS_NO_DEVICE_ADDRESS;
			goto out;
		}
		if (_csmisas_fill_location_data_raid(ioc, raid_device.handle,
		    karg) == 0)
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
		else
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto out;
	}

	/* bare drives are on channel = 0 */

	rc = _csmisas_sas_device_find_by_id(ioc, karg->bTargetId, karg->bPathId,
	    &sas_device);
	if (rc != 0) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_NO_DEVICE_ADDRESS;
		goto out;
	}

	/* make sure there's enough room to populate the Location[] struct */
	if ((karg->IoctlHeader.Length - offsetof(CSMI_SAS_GET_LOCATION_BUFFER,
	    Location)) < sizeof(CSMI_SAS_LOCATION_IDENTIFIER))
		goto out;

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
	karg->bNumberOfLocationIdentifiers = 1;
	karg->Location[0].bLocationFlags = 0;
	_csmisas_fill_location_data(ioc, &sas_device, karg->bIdentify,
	    &karg->Location[0]);

 out:
	dcsmisasprintk(ioc, printk(MPT3SAS_INFO_FMT "%s(): exit\n", ioc->name,
	   __func__));
	return rc;
}
