/*
 * Scsi Host Layer for MPT (Message Passing Technology) based controllers
 *
 * Copyright (C) 2012-2018  LSI Corporation
 * Copyright (C) 2013-2018 Avago Technologies
 * Copyright (C) 2013-2018 Broadcom Inc.
 *  (mailto: MPT-FusionLinux.pdl@broadcom.com)
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

#include "csmi/csmisas.h"
static int _csmisas_get_driver_info(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_DRIVER_INFO_BUFFER *karg);
static int _csmisas_get_cntlr_status(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_CNTLR_STATUS_BUFFER *karg);
static int _csmisas_get_cntlr_config(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_CNTLR_CONFIG_BUFFER *karg);
static int _csmisas_get_phy_info(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_PHY_INFO_BUFFER *karg);
static int _csmisas_get_scsi_address(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_GET_SCSI_ADDRESS_BUFFER *karg);
static int _csmisas_get_link_errors(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_LINK_ERRORS_BUFFER *karg);
static int _csmisas_smp_passthru(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_SMP_PASSTHRU_BUFFER *karg);
static int _csmisas_firmware_download(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER *karg);
static int _csmisas_get_raid_info(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_RAID_INFO_BUFFER *karg);
static int _csmisas_get_raid_config(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_RAID_CONFIG_BUFFER *karg);
static int _csmisas_get_raid_features(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_RAID_FEATURES_BUFFER *karg);
static int _csmisas_set_raid_control(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_RAID_CONTROL_BUFFER *karg);
static int _csmisas_get_raid_element(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_RAID_ELEMENT_BUFFER *karg);
static int _csmisas_set_raid_operation(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_RAID_SET_OPERATION_BUFFER *karg);
static int _csmisas_set_phy_info(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_SET_PHY_INFO_BUFFER *karg);
static int _csmisas_ssp_passthru(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_SSP_PASSTHRU_BUFFER *karg);
static int _csmisas_stp_passthru(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_STP_PASSTHRU_BUFFER *karg);
static int _csmisas_get_sata_signature(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_SATA_SIGNATURE_BUFFER *karg);
static int _csmisas_get_device_address(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER *karg);
static int _csmisas_task_managment(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_SSP_TASK_IU_BUFFER *karg);
static int _csmisas_phy_control(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_PHY_CONTROL_BUFFER *karg);
static int _csmisas_get_connector_info(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_CONNECTOR_INFO_BUFFER *karg);
static int _csmisas_get_location(struct MPT3SAS_ADAPTER *ioc,
	CSMI_SAS_GET_LOCATION_BUFFER *karg);

#define MPT2SAS_HP_3PAR_SSVID				0x1590
#define MPT2SAS_HP_2_4_INTERNAL_SSDID			0x0041
#define MPT2SAS_HP_2_4_EXTERNAL_SSDID			0x0042
#define MPT2SAS_HP_1_4_INTERNAL_1_4_EXTERNAL_SSDID	0x0043
#define MPT2SAS_HP_EMBEDDED_2_4_INTERNAL_SSDID		0x0044
#define MPT2SAS_HP_DAUGHTER_2_4_INTERNAL_SSDID		0x0046

/**
 * _ctl_check_for_hp_branded_controllers - customer branding check
 * @ioc: per adapter object
 *
 * HP controllers are only allowed to do CSMI IOCTL's
 *
 * Returns;  "1" if HP controller, else "0"
 */
static int
_ctl_check_for_hp_branded_controllers(struct MPT3SAS_ADAPTER *ioc)
{
	int rc = 0;

	if (ioc->pdev->subsystem_vendor != MPT2SAS_HP_3PAR_SSVID)
		goto out;

	switch (ioc->pdev->device) {
	case MPI2_MFGPAGE_DEVID_SAS2004:
		switch (ioc->pdev->subsystem_device) {
		case MPT2SAS_HP_DAUGHTER_2_4_INTERNAL_SSDID:
			rc = 1;
			break;
		default:
			break;
		}
		break;
	case MPI2_MFGPAGE_DEVID_SAS2308_2:
		switch (ioc->pdev->subsystem_device) {
		case MPT2SAS_HP_2_4_INTERNAL_SSDID:
		case MPT2SAS_HP_2_4_EXTERNAL_SSDID:
		case MPT2SAS_HP_1_4_INTERNAL_1_4_EXTERNAL_SSDID:
		case MPT2SAS_HP_EMBEDDED_2_4_INTERNAL_SSDID:
			rc = 1;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

 out:
	return rc;
}

long
_ctl_ioctl_csmi(struct MPT3SAS_ADAPTER *ioc, unsigned int cmd, void __user *arg)
{
	IOCTL_HEADER karg;
	void *payload;
	u32 payload_sz;
	int payload_pages;
	long ret = -EINVAL;

	if (copy_from_user(&karg, arg, sizeof(IOCTL_HEADER))) {
		printk(KERN_ERR "failure at %s:%d/%s()!\n",
		    __FILE__, __LINE__, __func__);
		return -EFAULT;
	}

	if (_ctl_check_for_hp_branded_controllers(ioc) != 1)
		return -EPERM;

	payload_sz = karg.Length + sizeof(IOCTL_HEADER);
	payload_pages = get_order(payload_sz);
	payload = (void *)__get_free_pages(GFP_KERNEL, payload_pages);
	if (!payload)
		goto out;

	if (copy_from_user(payload, arg, payload_sz)) {
		printk(KERN_ERR "%s():%d: failure\n", __func__, __LINE__);
		ret = -EFAULT;
		goto out_free_pages;
	}

	switch (cmd) {
	case CC_CSMI_SAS_GET_DRIVER_INFO:
		ret = _csmisas_get_driver_info(ioc, payload);
		break;
	case CC_CSMI_SAS_GET_CNTLR_STATUS:
		ret = _csmisas_get_cntlr_status(ioc, payload);
		break;
	case CC_CSMI_SAS_GET_SCSI_ADDRESS:
		ret = _csmisas_get_scsi_address(ioc, payload);
		break;
	case CC_CSMI_SAS_GET_DEVICE_ADDRESS:
		ret = _csmisas_get_device_address(ioc, payload);
		break;
	case CC_CSMI_SAS_GET_CNTLR_CONFIG:
		ret = _csmisas_get_cntlr_config(ioc, payload);
		break;
	case CC_CSMI_SAS_GET_PHY_INFO:
		ret = _csmisas_get_phy_info(ioc, payload);
		break;
	case CC_CSMI_SAS_GET_SATA_SIGNATURE:
		ret = _csmisas_get_sata_signature(ioc, payload);
		break;
	case CC_CSMI_SAS_GET_LINK_ERRORS:
		ret = _csmisas_get_link_errors(ioc, payload);
		break;
	case CC_CSMI_SAS_SMP_PASSTHRU:
		ret = _csmisas_smp_passthru(ioc, payload);
		break;
	case CC_CSMI_SAS_SSP_PASSTHRU:
		ret = _csmisas_ssp_passthru(ioc, payload);
		break;
	case CC_CSMI_SAS_FIRMWARE_DOWNLOAD:
		ret = _csmisas_firmware_download(ioc, payload);
		break;
	case CC_CSMI_SAS_GET_RAID_INFO:
		ret = _csmisas_get_raid_info(ioc, payload);
		break;
	case CC_CSMI_SAS_GET_RAID_CONFIG:
		ret = _csmisas_get_raid_config(ioc, payload);
		break;
	case CC_CSMI_SAS_GET_RAID_FEATURES:
		ret = _csmisas_get_raid_features(ioc, payload);
		break;
	case CC_CSMI_SAS_SET_RAID_CONTROL:
		ret = _csmisas_set_raid_control(ioc, payload);
		break;
	case CC_CSMI_SAS_GET_RAID_ELEMENT:
		ret = _csmisas_get_raid_element(ioc, payload);
		break;
	case CC_CSMI_SAS_SET_RAID_OPERATION:
		ret = _csmisas_set_raid_operation(ioc, payload);
		break;
	case CC_CSMI_SAS_SET_PHY_INFO:
		ret = _csmisas_set_phy_info(ioc, payload);
		break;
	case CC_CSMI_SAS_STP_PASSTHRU:
		ret = _csmisas_stp_passthru(ioc, payload);
		break;
	case CC_CSMI_SAS_TASK_MANAGEMENT:
		ret = _csmisas_task_managment(ioc, payload);
		break;
	case CC_CSMI_SAS_PHY_CONTROL:
		ret = _csmisas_phy_control(ioc, payload);
		break;
	case CC_CSMI_SAS_GET_CONNECTOR_INFO:
		ret = _csmisas_get_connector_info(ioc, payload);
		break;
	case CC_CSMI_SAS_GET_LOCATION:
		ret = _csmisas_get_location(ioc, payload);
		break;
	}

	if (copy_to_user(arg, payload, payload_sz)) {
		printk(KERN_ERR "%s():%d: failure\n",
		       __func__, __LINE__);
		ret = -EFAULT;
	}

 out_free_pages:
	free_pages((unsigned long)payload, payload_pages);
 out:
	return ret;
}

/**
 * _ctl_do_fw_download - Download fw to HBA
 * @ioc - pointer to ioc structure
 * @fwbuf - the fw buffer to flash
 * @fwlen - the size of the firmware
 *
 * This is the fw download engine for ioctls
 */
static int
_ctl_do_fw_download(struct MPT3SAS_ADAPTER *ioc, char *fwbuf, size_t fwlen)
{
	MPI2RequestHeader_t *mpi_request = NULL;
	MPI2DefaultReply_t *mpi_reply;
	Mpi2FWDownloadRequest *dlmsg;
	Mpi2FWDownloadTCSGE_t *tcsge;
	u16 ioc_status;
	u16 smid;
	unsigned long timeout, timeleft;
	u8 issue_reset;
	void *psge;
	void *data_out = NULL;
	dma_addr_t data_out_dma = 0;
	size_t data_out_sz = 0;
	u32 sgl_flags;
	long ret;
	u32 chunk_sz = 0;
	u32 cur_offset = 0;
	u32 remaining_bytes = (u32)fwlen;

	issue_reset = 0;

	if (ioc->ctl_cmds.status != MPT3_CMD_NOT_USED) {
		printk(MPT3SAS_ERR_FMT "%s: ctl_cmd in use\n",
		    ioc->name, __func__);
		ret = -EAGAIN;
		goto out;
	}

	ret = mpt3sas_wait_for_ioc_to_operational(ioc, 10);
	if (ret)
		goto out;

 again:
	if (remaining_bytes > FW_DL_CHUNK_SIZE)
		chunk_sz = FW_DL_CHUNK_SIZE;
	else
		chunk_sz = remaining_bytes;

	smid = mpt3sas_base_get_smid(ioc, ioc->ctl_cb_idx);
	if (!smid) {
		printk(MPT3SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		ret = -EAGAIN;
		goto out;
	}
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, sizeof(*mpi_request));
	/*
	 * Construct f/w download request
	 */
	dlmsg = (Mpi2FWDownloadRequest *)mpi_request;
	dlmsg->ImageType = MPI2_FW_DOWNLOAD_ITYPE_FW;
	dlmsg->Function = MPI2_FUNCTION_FW_DOWNLOAD;
	dlmsg->TotalImageSize = cpu_to_le32(fwlen);

	if (remaining_bytes == chunk_sz)
		dlmsg->MsgFlags = MPI2_FW_DOWNLOAD_MSGFLGS_LAST_SEGMENT;

	/* Construct TrasactionContext Element */
	tcsge = (Mpi2FWDownloadTCSGE_t *)&dlmsg->SGL;
	memset(tcsge, 0, sizeof(Mpi2FWDownloadTCSGE_t));

	/* spec defines 12 or size of element. which is better */
	tcsge->DetailsLength = offsetof(Mpi2FWDownloadTCSGE_t, ImageSize);
	tcsge->Flags = MPI2_SGE_FLAGS_TRANSACTION_ELEMENT;
	tcsge->ImageSize = cpu_to_le32(chunk_sz);
	tcsge->ImageOffset = cpu_to_le32(cur_offset);

	ret = 0;
	ioc->ctl_cmds.status = MPT3_CMD_PENDING;
	memset(ioc->ctl_cmds.reply, 0, ioc->reply_sz);
	ioc->ctl_cmds.smid = smid;
	data_out_sz = chunk_sz;

	/* obtain dma-able memory for data transfer */
	if (!data_out) {
		data_out = pci_alloc_consistent(ioc->pdev, data_out_sz,
		    &data_out_dma);
	}
	if (!data_out) {
		printk(KERN_ERR "failure at %s:%d/%s()!\n", __FILE__,
		    __LINE__, __func__);
		ret = -ENOMEM;
		mpt3sas_base_free_smid(ioc, smid);
		goto out;
	}
	memcpy(data_out, fwbuf+cur_offset, data_out_sz);

	/* add scatter gather elements */
	psge = (void *)mpi_request + sizeof(Mpi2FWDownloadRequest) -
	    sizeof(MPI2_MPI_SGE_UNION) + sizeof(Mpi2FWDownloadTCSGE_t);

	sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
	    MPI2_SGE_FLAGS_END_OF_LIST | MPI2_SGE_FLAGS_HOST_TO_IOC);
	sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
	ioc->base_add_sg_single(psge, sgl_flags |
	    data_out_sz, data_out_dma);

	/* send command to firmware */
	_ctl_display_some_debug(ioc, smid, "ctl_request", NULL);

	init_completion(&ioc->ctl_cmds.done);
	ioc->put_smid_default(ioc, smid);

	timeout = MPT3_IOCTL_DEFAULT_TIMEOUT;
	timeleft = wait_for_completion_timeout(&ioc->ctl_cmds.done,
	    timeout*HZ);

	if (!(ioc->ctl_cmds.status & MPT3_CMD_COMPLETE)) {
		printk(MPT3SAS_ERR_FMT "%s: timeout\n", ioc->name,
		    __func__);
		_debug_dump_mf(mpi_request, 0);
		if (!(ioc->ctl_cmds.status & MPT3_CMD_RESET))
			issue_reset = 1;
		goto issue_host_reset;
	}

	mpi_reply = ioc->ctl_cmds.reply;
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;

	cur_offset += chunk_sz;
	remaining_bytes -= chunk_sz;
	if (remaining_bytes > 0)
		goto again;

	goto out;
 issue_host_reset:
	/* Reset is only here for error conditions */
	mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
 out:

	/* free memory associated with sg buffers */
	if (data_out) {
		pci_free_consistent(ioc->pdev, data_out_sz, data_out,
		    data_out_dma);
		data_out = NULL;
	}

	ioc->ctl_cmds.status = MPT3_CMD_NOT_USED;
	return ret;
}
#include "csmi/csmisas.c"
