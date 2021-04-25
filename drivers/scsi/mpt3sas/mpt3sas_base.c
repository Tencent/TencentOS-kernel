/*
 * This is the Fusion MPT base driver providing common API layer interface
 * for access to MPT (Message Passing Technology) firmware.
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

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/kthread.h>
#include <asm/page.h>        /* To get host page size per arch */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19))
#include <linux/aer.h>
#endif

#include "mpt3sas_base.h"

static MPT_CALLBACK	mpt_callbacks[MPT_MAX_CALLBACKS];

#if defined(TARGET_MODE)
static struct STM_CALLBACK stm_callbacks;
EXPORT_SYMBOL(mpt3sas_ioc_list);
#endif

#define FAULT_POLLING_INTERVAL 1000 /* in milliseconds */
#define HBA_HOTUNPLUG_POLLING_INTERVAL 1000 /* in milliseconds */
#define SATA_SMART_POLLING_INTERVAL 300 /* in seconds */

 /* maximum controller queue depth */
#define MAX_HBA_QUEUE_DEPTH	30000
#define MAX_CHAIN_DEPTH		100000
static int max_queue_depth = -1;
module_param(max_queue_depth, int, 0444);
MODULE_PARM_DESC(max_queue_depth, " max controller queue depth ");

static int max_sgl_entries = -1;
module_param(max_sgl_entries, int, 0444);
MODULE_PARM_DESC(max_sgl_entries, " max sg entries ");

static int msix_disable = -1;
module_param(msix_disable, int, 0444);
MODULE_PARM_DESC(msix_disable, " disable msix routed interrupts (default=0)");

#if ((defined(RHEL_MAJOR) && (RHEL_MAJOR == 6)) || LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36))
static int smp_affinity_enable = 1;
module_param(smp_affinity_enable, int, 0444);
MODULE_PARM_DESC(smp_affinity_enable, "SMP affinity feature enable/disbale Default: enable(1)");
#endif

static int max_msix_vectors = -1;
module_param(max_msix_vectors, int, 0444);
MODULE_PARM_DESC(max_msix_vectors, " max msix vectors");

static int irqpoll_weight = -1;
module_param(irqpoll_weight, int, 0444);
MODULE_PARM_DESC(irqpoll_weight,
    "irq poll weight (default= one fourth of HBA queue depth)");

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
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

int disable_discovery = -1;
module_param(disable_discovery, int, 0444);
MODULE_PARM_DESC(disable_discovery, " disable discovery ");
#endif

static int mpt3sas_fwfault_debug;
MODULE_PARM_DESC(mpt3sas_fwfault_debug, " enable detection of firmware fault "
	"and halt firmware - (default=0)");

static int perf_mode = -1;
module_param(perf_mode, int, 0444);
MODULE_PARM_DESC(perf_mode,
    "Performance mode (only for Aero/Sea Generation), options:\n\t\t"
    "0 - balanced: high iops mode is enabled &"
    " interrupt coalescing is enabled only on high iops queues,\n\t\t"
    "1 - iops: high iops mode is disabled &"
    " interrupt coalescing is enabled on all queues,\n\t\t"
    "2 - latency: high iops mode is disabled &"
    " interrupt coalescing is enabled on all queues with timeout value 0xA,\n"
    "\t\tdefault - default perf_mode is 'balanced'.");

enum mpt3sas_perf_mode {
	MPT_PERF_MODE_DEFAULT	= -1,
	MPT_PERF_MODE_BALANCED	= 0,
	MPT_PERF_MODE_IOPS	= 1,
	MPT_PERF_MODE_LATENCY	= 2,
};

static void
_base_clear_outstanding_mpt_commands(struct MPT3SAS_ADAPTER *ioc);

static int
_base_wait_on_iocstate(struct MPT3SAS_ADAPTER *ioc,
		u32 ioc_state, int timeout);

void
mpt3sas_base_unmask_interrupts(struct MPT3SAS_ADAPTER *ioc);

/**
 * _scsih_set_fwfault_debug - global setting of ioc->fwfault_debug.
 *
 */
static int
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0))
_scsih_set_fwfault_debug(const char *val, const struct kernel_param *kp)
#else
_scsih_set_fwfault_debug(const char *val, struct kernel_param *kp)
#endif
{
	int ret = param_set_int(val, kp);
	struct MPT3SAS_ADAPTER *ioc;

	if (ret)
		return ret;

	printk(KERN_INFO "setting fwfault_debug(%d)\n", mpt3sas_fwfault_debug);
	/* global ioc spinlock to protect controller list on list operations */
	spin_lock(&gioc_lock);
	list_for_each_entry(ioc, &mpt3sas_ioc_list, list)
		ioc->fwfault_debug = mpt3sas_fwfault_debug;
	spin_unlock(&gioc_lock);
	return 0;
}
module_param_call(mpt3sas_fwfault_debug, _scsih_set_fwfault_debug,
	param_get_int, &mpt3sas_fwfault_debug, 0644);

/**
 * _base_readl_aero - retry the readl for max three times
 * @addr - MPT Fusion system interface register address
 *
 * Retry the readl() for max three times it gets zero value
 * while reading the system interface registers.
 */
static inline u32
_base_readl_aero(const volatile void __iomem *addr)
{
	u32 i = 0, ret_val;

	do {
		ret_val = readl(addr);
		i++;
	} while (ret_val == 0 && i < 3);

	return ret_val;
}

static inline u32
_base_readl(const volatile void __iomem *addr)
{
	return readl(addr);
}


/**
 * mpt3sas_base_check_cmd_timeout - Function
 *		to check timeout and command termination due
 *		to Host reset.
 *
 * @ioc:	per adapter object.
 * @status:	Status of issued command.	
 * @mpi_request:mf request pointer.
 * @sz:		size of buffer. 
 *
 * @Returns - 1/0 Reset to be done or Not
 */
u8 
mpt3sas_base_check_cmd_timeout(struct MPT3SAS_ADAPTER *ioc,
		U8 status, void *mpi_request, int sz)
{
	u8 issue_reset = 0;

	if (!(status & MPT3_CMD_RESET))
		issue_reset = 1;

	printk(MPT3SAS_ERR_FMT "Command %s\n", ioc->name,
	    ((issue_reset == 0) ? "terminated due to Host Reset" : "Timeout"));	
	_debug_dump_mf(mpi_request, sz);

	return issue_reset;
}

/**
 * _base_clone_reply_to_sys_mem - copies reply to reply free iomem
 *				  in BAR0 space.
 *
 * @ioc: per adapter object
 * @reply: reply message frame(lower 32bit addr)
 * @index: System request message index. 
 *
 * @Returns - Nothing
 */
static void
_base_clone_reply_to_sys_mem(struct MPT3SAS_ADAPTER *ioc, u32 reply,
		u32 index)
{
	/*256 is offset within sys register. 
	 256 offset MPI frame starts. Max MPI frame supported is 32.
	 32 * 128 = 4K. From here, Clone of reply free for mcpu starts*/
	u16 cmd_credit = ioc->facts.RequestCredit + 1;
	void *reply_free_iomem = (void*)ioc->chip + MPI_FRAME_START_OFFSET + 
		(cmd_credit * ioc->request_sz) + (index * sizeof(u32));
	writel(reply, reply_free_iomem);     
}

/**
 * _base_clone_mpi_to_sys_mem - Writes/copies MPI frames 
 *                              to system/BAR0 region.
 *
 * @dst_iomem: Pointer to the destinaltion location in BAR0 space.
 * @src: Pointer to the Source data.
 * @size: Size of data to be copied.
 */
static void
_base_clone_mpi_to_sys_mem(void *dst_iomem, void *src, u32 size)
{
	int i;
	u32 *src_virt_mem = (u32 *)src;

	for (i = 0; i < size/4; i++)
		writel((u32)src_virt_mem[i], dst_iomem + (i * 4));
}

/**
 * _base_clone_to_sys_mem - Writes/copies data to system/BAR0 region
 * 
 * @dst_iomem: Pointer to the destinaltion location in BAR0 space.
 * @src: Pointer to the Source data.
 * @size: Size of data to be copied.
 */
static void
_base_clone_to_sys_mem( void *dst_iomem, void *src, u32 size)
{
	int i;
	u32 *src_virt_mem = (u32 *)(src);

	for ( i = 0; i < size/4; i++ )
		writel((u32)src_virt_mem[i], dst_iomem + (i * 4));
}

/**
 * _base_get_chain - Calculates and Returns virtual chain address
 *			 for the provided smid in BAR0 space.
 *
 * @ioc: per adapter object
 * @smid: system request message index
 * @sge_chain_count: Scatter gather chain count.
 *
 * @Return: chain address.
 */
static inline void *
_base_get_chain(struct MPT3SAS_ADAPTER *ioc, u16 smid,
		u8 sge_chain_count)
{
	void *base_chain, *chain_virt;
	u16 cmd_credit = ioc->facts.RequestCredit + 1;
	base_chain  = (void *)ioc->chip + MPI_FRAME_START_OFFSET +
			(cmd_credit * ioc->request_sz) +
			(cmd_credit * 4 * sizeof(U32));
	chain_virt = base_chain + (smid * ioc->facts.MaxChainDepth *
			ioc->request_sz) + (sge_chain_count * ioc->request_sz);
	return chain_virt;
}

/**
 * _base_get_chain_phys - Calculates and Returns physical address in BAR0 for 
 *			  scatter gather chains, for the provided smid.
 *
 * @ioc: per adapter object
 * @smid: system request message index
 * @sge_chain_count: Scatter gather chain count.
 *
 * @Return - Physical chain address.
 */
static inline phys_addr_t
_base_get_chain_phys(struct MPT3SAS_ADAPTER *ioc, u16 smid,
		u8 sge_chain_count)
{
	phys_addr_t base_chain_phys, chain_phys;
	u16 cmd_credit = ioc->facts.RequestCredit + 1;

	base_chain_phys  = ioc->chip_phys + MPI_FRAME_START_OFFSET +
				(cmd_credit * ioc->request_sz) +
				REPLY_FREE_POOL_SIZE;
	chain_phys = base_chain_phys + (smid * ioc->facts.MaxChainDepth *
			ioc->request_sz) + (sge_chain_count * ioc->request_sz);
	return chain_phys;
}

/**
 * _base_get_buffer_bar0 - Calculates and Returns BAR0 mapped Host buffer address
 *			   for the provided smid.
 *			   (Each smid can have 64K starts from 17024)
 *			 
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * @Returns - Pointer to buffer location in BAR0.
 */

static void *
_base_get_buffer_bar0(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	u16 cmd_credit = ioc->facts.RequestCredit + 1;
	// Added extra 1 to reach end of chain.
	void *chain_end = _base_get_chain(ioc,
				cmd_credit + 1,
				ioc->facts.MaxChainDepth);
	return chain_end + (smid * 64 * 1024);
}

/**
 * _base_get_buffer_phys_bar0 - Calculates and Returns BAR0 mapped Host buffer 
 *				Physical address for the provided smid.
 *				(Each smid can have 64K starts from 17024)
 *			 
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * @Returns - Pointer to buffer location in BAR0.
 */
static phys_addr_t
_base_get_buffer_phys_bar0(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	u16 cmd_credit = ioc->facts.RequestCredit + 1;
	phys_addr_t chain_end_phys = _base_get_chain_phys(ioc,
			cmd_credit + 1,
			ioc->facts.MaxChainDepth);
	return chain_end_phys + (smid * 64 * 1024);
}

/**
 * _base_get_chain_buffer_dma_to_chain_buffer - Iterates chain lookup list and Provides 
 *				chain_buffer address for the matching dma address.
 *				(Each smid can have 64K starts from 17024)
 *			 
 * @ioc: per adapter object
 * @chain_buffer_dma: Chain buffer dma address. 
 *
 * @Returns - Pointer to chain buffer. Or Null on Failure.
 */
static void *
_base_get_chain_buffer_dma_to_chain_buffer(struct MPT3SAS_ADAPTER *ioc,
		dma_addr_t chain_buffer_dma)
{
	u16 index, j;
	struct chain_tracker *ct;
	
	for (index = 0; index < ioc->scsiio_depth; index++) {
		for (j = 0; j < ioc->chains_needed_per_io; j++) {
			ct = &ioc->chain_lookup[index].chains_per_smid[j];
			if (ct && ct->chain_buffer_dma == chain_buffer_dma)
				return ct->chain_buffer;
			else
				continue; 
		}
	}
	printk(MPT3SAS_ERR_FMT "Provided chain_buffer_dma"
			" address is not in the lookup list \n", ioc->name);
	return NULL;
}

/**
 * _clone_sg_entries -	MPI EP's scsiio and config requests
 *			are handled here. Base function for
 *			double buffering, before submitting
 *			the requests.
 *
 * @ioc: per adapter object.
 * @mpi_request: mf request pointer.
 * @smid: system request message index.
 *
 * @Returns: Nothing.
 */
static void _clone_sg_entries(struct MPT3SAS_ADAPTER *ioc,
		void *mpi_request, u16 smid)
{
	Mpi2SGESimple32_t *sgel, *sgel_next;
	u32  sgl_flags, sge_chain_count = 0;
	bool is_write = 0;
	u16 i = 0;
	void __iomem *buffer_iomem;
	phys_addr_t buffer_iomem_phys;
	void __iomem *buff_ptr;
	phys_addr_t buff_ptr_phys;
	void __iomem *dst_chain_addr[MCPU_MAX_CHAINS_PER_IO];
	void *src_chain_addr[MCPU_MAX_CHAINS_PER_IO];
	phys_addr_t dst_addr_phys;
	MPI2RequestHeader_t *request_hdr;
	struct scsi_cmnd *scmd;
	struct scatterlist *sg_scmd = NULL;
	int is_scsiio_req = 0;

	request_hdr = (MPI2RequestHeader_t *) mpi_request;

	if (request_hdr->Function == MPI2_FUNCTION_SCSI_IO_REQUEST) {
		Mpi25SCSIIORequest_t *scsiio_request =
			(Mpi25SCSIIORequest_t *)mpi_request;
		sgel = (Mpi2SGESimple32_t *) &scsiio_request->SGL;
		is_scsiio_req = 1;
	} else if (request_hdr->Function == MPI2_FUNCTION_CONFIG) {
		Mpi2ConfigRequest_t  *config_req =
			(Mpi2ConfigRequest_t *)mpi_request;
		sgel = (Mpi2SGESimple32_t *) &config_req->PageBufferSGE;
	} else
		return;

	/* From smid we can get scsi_cmd, once we have sg_scmd,
	 * we just need to get sg_virt and sg_next to get virual
	 * address associated with sgel->Address.
	 */

	if (is_scsiio_req) 
	{
		/* Get scsi_cmd using smid */
		scmd = mpt3sas_scsih_scsi_lookup_get(ioc, smid);
		if(scmd == NULL) {
			printk(MPT3SAS_ERR_FMT "scmd is NULL\n", ioc->name);
			return;
		}

		/* Get sg_scmd from scmd provided */
		sg_scmd = scsi_sglist(scmd);
	}

	/*
	 * 0 - 255	System register
	 * 256 - 4352	MPI Frame. (This is based on maxCredit 32)
	 * 4352 - 4864	Reply_free pool (512 byte is reserved considering
	 * 		maxCredit 32. Reply need extra room, for mCPU case kept
	 *		four times of maxCredit).
	 * 4864 - 17152	SGE chain element. (32cmd * 3 chain of 128 byte size = 12288)
	 * 17152 - x	Host buffer mapped with smid.
	 *		(Each smid can have 64K Max IO.)
	 * BAR0+Last 1K MSIX Addr and Data
	 * Total size in use 2113664 bytes of 4MB BAR0
	 */

	buffer_iomem = _base_get_buffer_bar0(ioc, smid);
	buffer_iomem_phys = _base_get_buffer_phys_bar0(ioc, smid);

	buff_ptr = buffer_iomem;
	buff_ptr_phys = buffer_iomem_phys;
	WARN_ON(buff_ptr_phys > U32_MAX);

	if (le32_to_cpu(sgel->FlagsLength) &
			(MPI2_SGE_FLAGS_HOST_TO_IOC << MPI2_SGE_FLAGS_SHIFT))
		is_write = 1;

	for (i = 0; i < MPT_MIN_PHYS_SEGMENTS + ioc->facts.MaxChainDepth; i++) {

		sgl_flags =
		    (le32_to_cpu(sgel->FlagsLength) >> MPI2_SGE_FLAGS_SHIFT);

		switch (sgl_flags & MPI2_SGE_FLAGS_ELEMENT_MASK) {
		case MPI2_SGE_FLAGS_CHAIN_ELEMENT:
			/*
			 * Helper function which on passing
			 * chain_buffer_dma returns chain_buffer. Get
			 * the virtual address for sgel->Address
			 */
			sgel_next =
				_base_get_chain_buffer_dma_to_chain_buffer(ioc,
						le32_to_cpu(sgel->Address));
			if (sgel_next == NULL)
				return;
			/*
			 * This is coping 128 byte chain
			 * frame (not a host buffer)
			 */
			dst_chain_addr[sge_chain_count] =
				_base_get_chain(ioc,
					smid, sge_chain_count);
			src_chain_addr[sge_chain_count] =
						(void *) sgel_next;
			dst_addr_phys = _base_get_chain_phys(ioc,
						smid, sge_chain_count);
			WARN_ON(dst_addr_phys > U32_MAX);
			sgel->Address =
				cpu_to_le32(lower_32_bits(dst_addr_phys));
			sgel = sgel_next;
			sge_chain_count++;
			break;
		case MPI2_SGE_FLAGS_SIMPLE_ELEMENT:
			if (is_write) {
				if (is_scsiio_req) {
					_base_clone_to_sys_mem(buff_ptr,
					    sg_virt(sg_scmd),
					    (le32_to_cpu(sgel->FlagsLength) &
					    0x00ffffff));
					/*
					 * FIXME: this relies on a a zero
					 * PCI mem_offset.
					 */
					sgel->Address =
					    cpu_to_le32((u32)buff_ptr_phys);
				} else {
					_base_clone_to_sys_mem(buff_ptr,
					    ioc->config_vaddr,
					    (le32_to_cpu(sgel->FlagsLength) &
					    0x00ffffff));
					sgel->Address =
					    cpu_to_le32((u32)buff_ptr_phys);
				}
			}
			buff_ptr += (le32_to_cpu(sgel->FlagsLength) &
			    0x00ffffff);
			buff_ptr_phys += (le32_to_cpu(sgel->FlagsLength) &
			    0x00ffffff);
			if ((le32_to_cpu(sgel->FlagsLength) &
			    (MPI2_SGE_FLAGS_END_OF_BUFFER
					<< MPI2_SGE_FLAGS_SHIFT)))
				goto eob_clone_chain;
			else {
				/*
				 * Every single element in MPT will have
				 * associated sg_next. Better to sanity that
				 * sg_next is not NULL, but it will be a bug
				 * if it is null.
				 */
				if (is_scsiio_req) {
					sg_scmd = sg_next(sg_scmd);
					if (sg_scmd)
						sgel++;
					else
						goto eob_clone_chain;
				}
			}
			break;
		}
	}

eob_clone_chain:
	for (i = 0; i < sge_chain_count; i++) {
		if (is_scsiio_req)
			_base_clone_to_sys_mem(dst_chain_addr[i],
				src_chain_addr[i], ioc->request_sz);
	}
}

/**
 *  mpt3sas_remove_dead_ioc_func - kthread context to remove dead ioc
 * @arg: input argument, used to derive ioc
 *
 * Return 0 if controller is removed from pci subsystem.
 * Return -1 for other case.
 */
static int mpt3sas_remove_dead_ioc_func(void *arg)
{
	struct MPT3SAS_ADAPTER *ioc = (struct MPT3SAS_ADAPTER *)arg;
	struct pci_dev *pdev;

	if ((ioc == NULL))
		return -1;

	pdev = ioc->pdev;
	if ((pdev == NULL))
		return -1;

#if defined(DISABLE_RESET_SUPPORT)
	ssleep(2);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,3))
	pci_stop_and_remove_bus_device(pdev);
#else
	pci_remove_bus_device(pdev);
#endif
	return 0;
}

/**
 * mpt3sas_base_pci_device_is_unplugged - Check whether HBA device is
 *				 hot unplugged or not
 * @ioc: per adapter object 
 *
 * Return 1 if the HBA device is hot unplugged else return 0.
 */
u8
mpt3sas_base_pci_device_is_unplugged(struct MPT3SAS_ADAPTER *ioc)
{
	struct pci_dev *pdev = ioc->pdev;
	struct pci_bus *bus = pdev->bus;
	int devfn = pdev->devfn;
	u32 vendor_id;

	if (pci_bus_read_config_dword(bus, devfn, PCI_VENDOR_ID, &vendor_id))
		return 1;

	/* some broken boards return 0 or ~0 if a slot is empty: */
	if (vendor_id == 0xffffffff || vendor_id == 0x00000000 ||
	    vendor_id == 0x0000ffff || vendor_id == 0xffff0000)
		return 1;

	/*
	 * Configuration Request Retry Status.  Some root ports return the
	 * actual device ID instead of the synthetic ID (0xFFFF) required
	 * by the PCIe spec.  Ignore the device ID and only check for
	 * (vendor id == 1).
	 */

	if ((vendor_id & 0xffff) == 0x0001)
		return 1;

	return 0;	
}

/**
 * mpt3sas_base_pci_device_is_available - check whether pci device is
 * 				 available for any transactions with FW
 *
 * @ioc: per adapter object 
 *
 * Return 1 if pci device state is up and running else return 0.
 */
u8
mpt3sas_base_pci_device_is_available(struct MPT3SAS_ADAPTER *ioc)
{
	if (ioc->pci_error_recovery || mpt3sas_base_pci_device_is_unplugged(ioc))
		return 0;
	return 1;
}

/**
 * _base_sync_drv_fw_timestamp - Sync Drive-Fw TimeStamp.
 *
 * @ioc: Per Adapter Object 
 *
 * Return nothing.
 */
static void _base_sync_drv_fw_timestamp(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi26IoUnitControlRequest_t *mpi_request;
	Mpi26IoUnitControlReply_t *mpi_reply;
	u16 smid;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0))
	ktime_t current_time;
#else
	struct timeval current_time;
#endif
	u64 TimeStamp = 0;
	u8 issue_reset = 0;

	mutex_lock(&ioc->scsih_cmds.mutex);
	if (ioc->scsih_cmds.status != MPT3_CMD_NOT_USED) {
		pr_err("%s: scsih_cmd in use %s\n", ioc->name,  __func__);
		goto out;
	}
	ioc->scsih_cmds.status = MPT3_CMD_PENDING;
	smid = mpt3sas_base_get_smid(ioc, ioc->scsih_cb_idx);
	if (!smid) {
		pr_err("%s: failed obtaining a smid %s\n", ioc->name,
		    __func__);
		ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
		goto out;
	}
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->scsih_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi26IoUnitControlRequest_t));
	mpi_request->Function = MPI2_FUNCTION_IO_UNIT_CONTROL;
	mpi_request->Operation = MPI26_CTRL_OP_SET_IOC_PARAMETER;
	mpi_request->IOCParameter = MPI26_SET_IOC_PARAMETER_SYNC_TIMESTAMP;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0))
	current_time = ktime_get_real();
	TimeStamp = ktime_to_ms(current_time);
#else
	do_gettimeofday(&current_time);
	TimeStamp = (u64) (current_time.tv_sec * 1000) +
			(current_time.tv_usec / 1000);
#endif
	mpi_request->Reserved7 = cpu_to_le32(TimeStamp & 0xFFFFFFFF);
	mpi_request->IOCParameterValue = cpu_to_le32(TimeStamp >> 32);
	init_completion(&ioc->scsih_cmds.done);
	ioc->put_smid_default(ioc, smid);
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT
	    "Io Unit Control Sync TimeStamp (sending), @time %lld ms\n",
	    ioc->name, TimeStamp));
	wait_for_completion_timeout(&ioc->scsih_cmds.done,
		MPT3SAS_TIMESYNC_TIMEOUT_SECONDS*HZ);
	if (!(ioc->scsih_cmds.status & MPT3_CMD_COMPLETE)) {
		mpt3sas_check_cmd_timeout(ioc,
		    ioc->scsih_cmds.status, mpi_request,
		    sizeof(Mpi2SasIoUnitControlRequest_t)/4, issue_reset);
		goto issue_host_reset;
	}
	if (ioc->scsih_cmds.status & MPT3_CMD_REPLY_VALID) {
		mpi_reply = ioc->scsih_cmds.reply;
		dinitprintk(ioc, printk(MPT3SAS_INFO_FMT
		    "Io Unit Control sync timestamp (complete): "
		    "ioc_status(0x%04x), loginfo(0x%08x)\n",
		    ioc->name, le16_to_cpu(mpi_reply->IOCStatus),
		    le32_to_cpu(mpi_reply->IOCLogInfo)));
	}
issue_host_reset:
	if (issue_reset)
                mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
 out:
	mutex_unlock(&ioc->scsih_cmds.mutex);
	return;
}

/**
 * _base_fault_reset_work - workq handling ioc fault conditions
 * @work: input argument, used to derive ioc
 * Context: sleep.
 *
 * Return nothing.
 */
static void
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
_base_fault_reset_work(struct work_struct *work)
{
	struct MPT3SAS_ADAPTER *ioc =
	    container_of(work, struct MPT3SAS_ADAPTER, fault_reset_work.work);
#else
_base_fault_reset_work(void *arg)
{
	struct MPT3SAS_ADAPTER *ioc = (struct MPT3SAS_ADAPTER *)arg;
#endif
	unsigned long	 flags;
	u32 doorbell;
	int rc;
	struct task_struct *p;


	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	if ((ioc->shost_recovery && (ioc->ioc_coredump_loop == 0)) ||
			ioc->pci_error_recovery || ioc->remove_host)
		goto rearm_timer;
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);

	doorbell = mpt3sas_base_get_iocstate(ioc, 0);
	if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_MASK) {
		printk(MPT3SAS_ERR_FMT "SAS host is non-operational !!!!\n",
		    ioc->name);

		/* It may be possible that EEH recovery can resolve some of
		 * pci bus failure issues rather removing the dead ioc function
		 * by considering controller is in a non-operational state. So
		 * here priority is given to the EEH recovery. If it doesn't
		 * not resolve this issue, mpt3sas driver will consider this
		 * controller to non-operational state and remove the dead ioc
		 * function.
		 */
		if (ioc->non_operational_loop++ < 5) {
			spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock,
							 flags);
			goto rearm_timer;
		}

		/*
		 * Set remove_host flag early since kernel thread will
		 * take some time to execute.
		 */
		ioc->remove_host = 1;

		/*
		 * Call _scsih_flush_pending_cmds callback so that we flush all
		 * pending commands back to OS. This call is required to aovid
		 * deadlock at block layer. Dead IOC will fail to do diag reset,
		 * and this call is safe since dead ioc will never return any
		 * command back from HW.
		 */
		ioc->schedule_dead_ioc_flush_running_cmds(ioc);

		/*Remove the Dead Host */
		p = kthread_run(mpt3sas_remove_dead_ioc_func, ioc,
				"%s_dead_ioc_%d", ioc->driver_name, ioc->id);
		if (IS_ERR(p))
			printk(MPT3SAS_ERR_FMT "%s: Running mpt3sas_dead_ioc "
			    "thread failed !!!!\n", ioc->name, __func__);
		else
			printk(MPT3SAS_ERR_FMT "%s: Running mpt3sas_dead_ioc "
			    "thread success !!!!\n", ioc->name, __func__);
		return; /* don't rearm timer */
	}

	if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_COREDUMP) {
		u8 timeout = (ioc->manu_pg11.CoreDumpTOSec)?
				ioc->manu_pg11.CoreDumpTOSec:
				MPT3SAS_DEFAULT_COREDUMP_TIMEOUT_SECONDS;

		timeout /= (FAULT_POLLING_INTERVAL/1000);

		if (ioc->ioc_coredump_loop == 0) {
			mpt3sas_base_coredump_info(ioc, doorbell &
							MPI2_DOORBELL_DATA_MASK);
			/* do not accept any IOs and disable the interrupts */
			spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
			ioc->shost_recovery = 1;
			spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
			mpt3sas_scsih_clear_outstanding_scsi_tm_commands(ioc);
			mpt3sas_base_mask_interrupts(ioc);
			_base_clear_outstanding_mpt_commands(ioc);
			mpt3sas_ctl_clear_outstanding_ioctls(ioc);
		}

		drsprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: CoreDump loop %d.", ioc->name,
				__func__, ioc->ioc_coredump_loop));

		/* Wait until CoreDump completes or times out */
		if (ioc->ioc_coredump_loop++ < timeout) {
			spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock,
							 flags);
			goto rearm_timer;
		}
	}

	if (ioc->ioc_coredump_loop) {
		if ((doorbell & MPI2_IOC_STATE_MASK) != MPI2_IOC_STATE_COREDUMP)
			printk(MPT3SAS_ERR_FMT "%s: CoreDump completed. LoopCount: %d", ioc->name,
					__func__, ioc->ioc_coredump_loop);
		else
			printk(MPT3SAS_ERR_FMT "%s: CoreDump Timed out. LoopCount: %d", ioc->name,
					__func__, ioc->ioc_coredump_loop);

		ioc->ioc_coredump_loop = MPT3SAS_COREDUMP_LOOP_DONE;
	}

	ioc->non_operational_loop = 0;

	if ((doorbell & MPI2_IOC_STATE_MASK) != MPI2_IOC_STATE_OPERATIONAL) {
		rc = mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
		printk(MPT3SAS_WARN_FMT "%s: hard reset: %s\n", ioc->name,
		    __func__, (rc == 0) ? "success" : "failed");
		doorbell = mpt3sas_base_get_iocstate(ioc, 0);
		if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
			mpt3sas_print_fault_code(ioc, doorbell &
			    MPI2_DOORBELL_DATA_MASK);
		} else if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_COREDUMP)
			mpt3sas_base_coredump_info(ioc, doorbell &
			    MPI2_DOORBELL_DATA_MASK);
		if (rc && (doorbell & MPI2_IOC_STATE_MASK) !=
		    MPI2_IOC_STATE_OPERATIONAL)
			return; /* don't rearm timer */
#if defined(TARGET_MODE)
	} else {
		if (stm_callbacks.watchdog)
			/* target mode drivers watchdog */
			stm_callbacks.watchdog(ioc);
#endif
	}

	ioc->ioc_coredump_loop = 0;

	if (ioc->time_sync_interval && 
	    ++ioc->timestamp_update_count >= ioc->time_sync_interval) {
		ioc->timestamp_update_count = 0;
		_base_sync_drv_fw_timestamp(ioc);
	}

	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
 rearm_timer:
	if (ioc->fault_reset_work_q)
		queue_delayed_work(ioc->fault_reset_work_q,
		    &ioc->fault_reset_work,
		    msecs_to_jiffies(FAULT_POLLING_INTERVAL));
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
}

static void
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
_base_hba_hot_unplug_work(struct work_struct *work)
{
	struct MPT3SAS_ADAPTER *ioc =
	    container_of(work, struct MPT3SAS_ADAPTER, hba_hot_unplug_work.work);
#else
_base_hba_hot_unplug_work(void *arg)
{
	struct MPT3SAS_ADAPTER *ioc = (struct MPT3SAS_ADAPTER *)arg;
#endif  
	unsigned long	 flags;

	spin_lock_irqsave(&ioc->hba_hot_unplug_lock, flags);
	if (ioc->shost_recovery || ioc->pci_error_recovery)
		goto rearm_timer;

	if (mpt3sas_base_pci_device_is_unplugged(ioc)) {
		if (ioc->remove_host) {
			printk(MPT3SAS_ERR_FMT
			    "The IOC seems hot unplugged and the driver is "
			    "waiting for pciehp module to remove the PCIe "
			    "device instance associated with IOC!!!\n",
			    ioc->name);
			goto rearm_timer;
		}

		/* Set remove_host flag here, since kernel will invoke driver's
		 * .remove() callback function one after the other for all hot
		 * un-plugged devices, so it may take some time to call
		 * .remove() function for subsequent hot un-plugged
		 * PCI devices.
		 */
		ioc->remove_host = 1;
		_base_clear_outstanding_mpt_commands(ioc);
		mpt3sas_scsih_clear_outstanding_scsi_tm_commands(ioc);
		mpt3sas_ctl_clear_outstanding_ioctls(ioc);
	}

rearm_timer:
	if (ioc->hba_hot_unplug_work_q)
		queue_delayed_work(ioc->hba_hot_unplug_work_q,
		    &ioc->hba_hot_unplug_work,
		    msecs_to_jiffies(HBA_HOTUNPLUG_POLLING_INTERVAL));
	spin_unlock_irqrestore(&ioc->hba_hot_unplug_lock, flags);
}

/**
 * _base_sata_smart_poll_work - worker thread which will poll for SMART error
 * 				SATA drives for every 5 mints
 * @work: input argument, used to derive ioc
 * Context: sleep.
 *
 * Returns nothing.
 */
static void
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
_base_sata_smart_poll_work(struct work_struct *work)
{
	struct MPT3SAS_ADAPTER *ioc =
	    container_of(work, struct MPT3SAS_ADAPTER, smart_poll_work.work);
#else
_base_sata_smart_poll_work(void *arg)
{
	struct MPT3SAS_ADAPTER *ioc = (struct MPT3SAS_ADAPTER *)arg;
#endif
	if (ioc->shost_recovery || !mpt3sas_base_pci_device_is_available(ioc))
		goto rearm_timer;
	mpt3sas_scsih_sata_smart_polling(ioc);
rearm_timer:
	if (ioc->smart_poll_work_q)
		queue_delayed_work(ioc->smart_poll_work_q,
		    &ioc->smart_poll_work,
		    SATA_SMART_POLLING_INTERVAL * HZ);
}

/**
 * mpt3sas_base_start_watchdog - start the fault_reset_work_q
 * @ioc: per adapter object
 * Context: sleep.
 *
 * Return nothing.
 */
void
mpt3sas_base_start_watchdog(struct MPT3SAS_ADAPTER *ioc)
{
	unsigned long	 flags;

	if (ioc->fault_reset_work_q)
		return;
	
	ioc->timestamp_update_count = 0;
	/* initialize fault polling */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	INIT_DELAYED_WORK(&ioc->fault_reset_work, _base_fault_reset_work);
#else
	INIT_WORK(&ioc->fault_reset_work, _base_fault_reset_work, (void *)ioc);
#endif
	snprintf(ioc->fault_reset_work_q_name,
		sizeof(ioc->fault_reset_work_q_name), "poll_%s%d_status",
     		ioc->driver_name, ioc->id);
	ioc->fault_reset_work_q =
		create_singlethread_workqueue(ioc->fault_reset_work_q_name);
	if (!ioc->fault_reset_work_q) {
		printk(MPT3SAS_ERR_FMT "%s: failed (line=%d)\n",
		    ioc->name, __func__, __LINE__);
			return;
	}
	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	if (ioc->fault_reset_work_q)
		queue_delayed_work(ioc->fault_reset_work_q,
		    &ioc->fault_reset_work,
		    msecs_to_jiffies(FAULT_POLLING_INTERVAL));
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
}

/**
 * mpt3sas_base_stop_watchdog - stop the fault_reset_work_q
 * @ioc: per adapter object
 * Context: sleep.
 *
 * Return nothing.
 */
void
mpt3sas_base_stop_watchdog(struct MPT3SAS_ADAPTER *ioc)
{
	unsigned long flags;
	struct workqueue_struct *wq;

	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	wq = ioc->fault_reset_work_q;
	ioc->fault_reset_work_q = NULL;
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
	if (wq) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23))
		if (!cancel_delayed_work_sync(&ioc->fault_reset_work))
#else
		if (!cancel_delayed_work(&ioc->fault_reset_work))
#endif
			flush_workqueue(wq);
		destroy_workqueue(wq);
	}
}

void
mpt3sas_base_start_hba_unplug_watchdog(struct MPT3SAS_ADAPTER *ioc)
{
	unsigned long	 flags;

	if (ioc->hba_hot_unplug_work_q)
		return;

	/* initialize fault polling */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	INIT_DELAYED_WORK(&ioc->hba_hot_unplug_work,
	    _base_hba_hot_unplug_work);
#else
	INIT_WORK(&ioc->hba_hot_unplug_work,
	    _base_hba_hot_unplug_work, (void *)ioc);
#endif
	snprintf(ioc->hba_hot_unplug_work_q_name,
		sizeof(ioc->hba_hot_unplug_work_q_name), "poll_%s%d_hba_unplug",
     		ioc->driver_name, ioc->id);
	ioc->hba_hot_unplug_work_q =
		create_singlethread_workqueue(ioc->hba_hot_unplug_work_q_name);
	if (!ioc->hba_hot_unplug_work_q) {
		printk(MPT3SAS_ERR_FMT "%s: failed (line=%d)\n",
		    ioc->name, __func__, __LINE__);
			return;
	}

	spin_lock_irqsave(&ioc->hba_hot_unplug_lock, flags);
	if (ioc->hba_hot_unplug_work_q)
		queue_delayed_work(ioc->hba_hot_unplug_work_q,
		    &ioc->hba_hot_unplug_work,
		    msecs_to_jiffies(FAULT_POLLING_INTERVAL));
	spin_unlock_irqrestore(&ioc->hba_hot_unplug_lock, flags);
}

void
mpt3sas_base_stop_hba_unplug_watchdog(struct MPT3SAS_ADAPTER *ioc)
{
	unsigned long flags;
	struct workqueue_struct *wq;

	spin_lock_irqsave(&ioc->hba_hot_unplug_lock, flags);
	wq = ioc->hba_hot_unplug_work_q;
	ioc->hba_hot_unplug_work_q = NULL;
	spin_unlock_irqrestore(&ioc->hba_hot_unplug_lock, flags);

	if (wq) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23))
		if (!cancel_delayed_work_sync(&ioc->hba_hot_unplug_work))
#else
		if (!cancel_delayed_work(&ioc->hba_hot_unplug_work))
#endif
			flush_workqueue(wq);
		destroy_workqueue(wq);
	}
}

/**
 * mpt3sas_base_start_smart_polling - Create and start the smart polling
 * 					thread for SMART SATA drive
 * @ioc: per adapter object
 * Context: sleep.
 *
 * Return nothing.
 */
void
mpt3sas_base_start_smart_polling(struct MPT3SAS_ADAPTER *ioc)
{
	if (ioc->smart_poll_work_q)
		return;

	/* initialize SMART SATA drive polling */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19))
	INIT_DELAYED_WORK(&ioc->smart_poll_work, _base_sata_smart_poll_work);
#else
	INIT_WORK(&ioc->smart_poll_work, _base_sata_smart_poll_work, (void *)ioc);
#endif
	snprintf(ioc->smart_poll_work_q_name,
	    sizeof(ioc->smart_poll_work_q_name), "smart_poll_%d", ioc->id);
	ioc->smart_poll_work_q =
		create_singlethread_workqueue(ioc->smart_poll_work_q_name);
	if (!ioc->smart_poll_work_q) {
		printk(MPT3SAS_ERR_FMT "%s: failed (line=%d)\n",
		    ioc->name, __func__, __LINE__);
			return;
	}
	if (ioc->smart_poll_work_q)
		queue_delayed_work(ioc->smart_poll_work_q,
		    &ioc->smart_poll_work,
		    SATA_SMART_POLLING_INTERVAL * HZ);
}

/**
 * mpt3sas_base_stop_smart_polling - stop the smart polling thread
 * @ioc: per adapter object
 * Context: sleep.
 *
 * Return nothing.
 */
void
mpt3sas_base_stop_smart_polling(struct MPT3SAS_ADAPTER *ioc)
{
	struct workqueue_struct *wq;

	wq = ioc->smart_poll_work_q;
	ioc->smart_poll_work_q = NULL;
	if (wq) {
		if (!cancel_delayed_work(&ioc->smart_poll_work))
			flush_workqueue(wq);
		destroy_workqueue(wq);
	}
}

/**
 * mpt3sas_base_fault_info - verbose translation of firmware FAULT code
 * @ioc: per adapter object
 * @fault_code: fault code
 *
 * Return nothing.
 */
void
mpt3sas_base_fault_info(struct MPT3SAS_ADAPTER *ioc , u16 fault_code)
{
	printk(MPT3SAS_ERR_FMT "fault_state(0x%04x)!\n",
	    ioc->name, fault_code);
}

/**
 * mpt3sas_base_coredump_info - verbose translation of firmware CoreDump state
 * @ioc: per adapter object
 * @fault_code: fault code
 *
 * Return nothing.
 */
void
mpt3sas_base_coredump_info(struct MPT3SAS_ADAPTER *ioc , u16 fault_code)
{
	printk(MPT3SAS_ERR_FMT "coredump_state(0x%04x)!\n",
	    ioc->name, fault_code);
}

/**
 * mpt3sas_base_wait_for_coredump_completion - Wait until coredump
 *      completes or times out
 * @ioc: per adapter object
 *
  * Returns 0 for success, non-zero for failure.
 */
int
mpt3sas_base_wait_for_coredump_completion(struct MPT3SAS_ADAPTER *ioc,
		const char *caller)
{
	u8 timeout = (ioc->manu_pg11.CoreDumpTOSec)?ioc->manu_pg11.CoreDumpTOSec:
			MPT3SAS_DEFAULT_COREDUMP_TIMEOUT_SECONDS;

	int ioc_state = _base_wait_on_iocstate(ioc, MPI2_IOC_STATE_FAULT,
					timeout);

	if (ioc_state)
		printk(MPT3SAS_ERR_FMT "%s: CoreDump timed out. "
			" (ioc_state=0x%x)\n", ioc->name, caller, ioc_state);
	else
		printk(MPT3SAS_INFO_FMT "%s: CoreDump completed. "
			" (ioc_state=0x%x)\n", ioc->name, caller, ioc_state);

	return ioc_state;
}

/**
 * mpt3sas_halt_firmware - halt's mpt controller firmware
 * @ioc: per adapter object
 * @set_fault: set fw fault
 *
 * For debugging timeout related issues.  Writing 0xCOFFEE00
 * to the doorbell register will halt controller firmware. With
 * the purpose to stop both driver and firmware, the enduser can
 * obtain a ring buffer from controller UART.
 */
void
mpt3sas_halt_firmware(struct MPT3SAS_ADAPTER *ioc, u8 set_fault)
{
	u32 doorbell;

	if ((!ioc->fwfault_debug) && (!set_fault))
		return;

	if (!set_fault)
		dump_stack();

	doorbell = ioc->base_readl(&ioc->chip->Doorbell);
	if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
		mpt3sas_print_fault_code(ioc , doorbell);
	} 
	else if ((doorbell & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_COREDUMP)
		mpt3sas_base_coredump_info(ioc, doorbell &
		    MPI2_DOORBELL_DATA_MASK);
	else {
		writel(0xC0FFEE00, &ioc->chip->Doorbell);
		if (!set_fault)
			printk(MPT3SAS_ERR_FMT "Firmware is halted due to"
		    	 " command timeout\n", ioc->name);
	}
	
	if (set_fault)
		return;

	if (ioc->fwfault_debug == 2)
		for (;;);
	else
		panic("panic in %s\n", __func__);
}


#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0)) || \
    (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36) && \
     (defined(RHEL_MAJOR) && (RHEL_MAJOR != 6))))
/**
 * _base_group_cpus_on_irq - when there are more cpus than available 
 *				 msix vectors, then group cpus 
 * 				 together on same irq
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_base_group_cpus_on_irq(struct MPT3SAS_ADAPTER *ioc)
{
	struct adapter_reply_queue *reply_q;
	unsigned int i, cpu, group, nr_cpus, nr_msix, index = 0;
	cpu = cpumask_first(cpu_online_mask);
	nr_msix = ioc->reply_queue_count - ioc->high_iops_queues;
	nr_cpus = num_online_cpus();
	group = nr_cpus / nr_msix;

	list_for_each_entry(reply_q, &ioc->reply_queue_list, list) {

		if (reply_q->msix_index < ioc->high_iops_queues)
			continue;

		if (cpu >= nr_cpus)
			break;

		if (index < nr_cpus % nr_msix)
			group++;

		for (i = 0 ; i < group ; i++) {
			ioc->cpu_msix_table[cpu] = reply_q->msix_index;
			cpu = cpumask_next(cpu, cpu_online_mask);
		}
		index++;
	}
}
#endif

/**
 * _base_sas_ioc_info - verbose translation of the ioc status
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 * @request_hdr: request mf
 *
 * Return nothing.
 */
static void
_base_sas_ioc_info(struct MPT3SAS_ADAPTER *ioc, MPI2DefaultReply_t *mpi_reply,
	MPI2RequestHeader_t *request_hdr)
{
	u16 ioc_status = le16_to_cpu(mpi_reply->IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	char *desc = NULL;
	u16 frame_sz;
	char *func_str = NULL;

	/* SCSI_IO, RAID_PASS are handled from _scsih_scsi_ioc_info */
	if (request_hdr->Function == MPI2_FUNCTION_SCSI_IO_REQUEST ||
	    request_hdr->Function == MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH ||
	    request_hdr->Function == MPI2_FUNCTION_EVENT_NOTIFICATION)
		return;

	if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
		return;

	/*
	 * Older Firmware version doesn't support driver trigger pages.
	 * So, skip displaying 'config invalid type' type
	 * of error message.
	 */
	if (request_hdr->Function == MPI2_FUNCTION_CONFIG) {
		Mpi2ConfigRequest_t *rqst = (Mpi2ConfigRequest_t *)request_hdr;

		if ((rqst->ExtPageType ==
		    MPI2_CONFIG_EXTPAGETYPE_DRIVER_PERSISTENT_TRIGGER) &&
		    !(ioc->logging_level & MPT_DEBUG_CONFIG)) {
			return;
		}
	}


	switch (ioc_status) {

/****************************************************************************
*  Common IOCStatus values for all replies
****************************************************************************/

	case MPI2_IOCSTATUS_INVALID_FUNCTION:
		desc = "invalid function";
		break;
	case MPI2_IOCSTATUS_BUSY:
		desc = "busy";
		break;
	case MPI2_IOCSTATUS_INVALID_SGL:
		desc = "invalid sgl";
		break;
	case MPI2_IOCSTATUS_INTERNAL_ERROR:
		desc = "internal error";
		break;
	case MPI2_IOCSTATUS_INVALID_VPID:
		desc = "invalid vpid";
		break;
	case MPI2_IOCSTATUS_INSUFFICIENT_RESOURCES:
		desc = "insufficient resources";
		break;
	case MPI2_IOCSTATUS_INSUFFICIENT_POWER:
		desc = "insufficient power";
		break;
	case MPI2_IOCSTATUS_INVALID_FIELD:
		desc = "invalid field";
		break;
	case MPI2_IOCSTATUS_INVALID_STATE:
		desc = "invalid state";
		break;
	case MPI2_IOCSTATUS_OP_STATE_NOT_SUPPORTED:
		desc = "op state not supported";
		break;

/****************************************************************************
*  Config IOCStatus values
****************************************************************************/

	case MPI2_IOCSTATUS_CONFIG_INVALID_ACTION:
		desc = "config invalid action";
		break;
	case MPI2_IOCSTATUS_CONFIG_INVALID_TYPE:
		desc = "config invalid type";
		break;
	case MPI2_IOCSTATUS_CONFIG_INVALID_DATA:
		desc = "config invalid data";
		break;
	case MPI2_IOCSTATUS_CONFIG_NO_DEFAULTS:
		desc = "config no defaults";
		break;
	case MPI2_IOCSTATUS_CONFIG_CANT_COMMIT:
		desc = "config cant commit";
		break;

/****************************************************************************
*  SCSI IO Reply
****************************************************************************/

	case MPI2_IOCSTATUS_SCSI_RECOVERED_ERROR:
	case MPI2_IOCSTATUS_SCSI_INVALID_DEVHANDLE:
	case MPI2_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
	case MPI2_IOCSTATUS_SCSI_DATA_OVERRUN:
	case MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN:
	case MPI2_IOCSTATUS_SCSI_IO_DATA_ERROR:
	case MPI2_IOCSTATUS_SCSI_PROTOCOL_ERROR:
	case MPI2_IOCSTATUS_SCSI_TASK_TERMINATED:
	case MPI2_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:
	case MPI2_IOCSTATUS_SCSI_TASK_MGMT_FAILED:
	case MPI2_IOCSTATUS_SCSI_IOC_TERMINATED:
	case MPI2_IOCSTATUS_SCSI_EXT_TERMINATED:
		break;

/****************************************************************************
*  For use by SCSI Initiator and SCSI Target end-to-end data protection
****************************************************************************/

	case MPI2_IOCSTATUS_EEDP_GUARD_ERROR:
		if (!ioc->disable_eedp_support)
			desc = "eedp guard error";
		break;
	case MPI2_IOCSTATUS_EEDP_REF_TAG_ERROR:
		if (!ioc->disable_eedp_support)
			desc = "eedp ref tag error";
		break;
	case MPI2_IOCSTATUS_EEDP_APP_TAG_ERROR:
		if (!ioc->disable_eedp_support)
			desc = "eedp app tag error";
		break;

/****************************************************************************
*  SCSI Target values
****************************************************************************/

	case MPI2_IOCSTATUS_TARGET_INVALID_IO_INDEX:
		desc = "target invalid io index";
		break;
	case MPI2_IOCSTATUS_TARGET_ABORTED:
		desc = "target aborted";
		break;
	case MPI2_IOCSTATUS_TARGET_NO_CONN_RETRYABLE:
		desc = "target no conn retryable";
		break;
	case MPI2_IOCSTATUS_TARGET_NO_CONNECTION:
		desc = "target no connection";
		break;
	case MPI2_IOCSTATUS_TARGET_XFER_COUNT_MISMATCH:
		desc = "target xfer count mismatch";
		break;
	case MPI2_IOCSTATUS_TARGET_DATA_OFFSET_ERROR:
		desc = "target data offset error";
		break;
	case MPI2_IOCSTATUS_TARGET_TOO_MUCH_WRITE_DATA:
		desc = "target too much write data";
		break;
	case MPI2_IOCSTATUS_TARGET_IU_TOO_SHORT:
		desc = "target iu too short";
		break;
	case MPI2_IOCSTATUS_TARGET_ACK_NAK_TIMEOUT:
		desc = "target ack nak timeout";
		break;
	case MPI2_IOCSTATUS_TARGET_NAK_RECEIVED:
		desc = "target nak received";
		break;

/****************************************************************************
*  Serial Attached SCSI values
****************************************************************************/

	case MPI2_IOCSTATUS_SAS_SMP_REQUEST_FAILED:
		desc = "smp request failed";
		break;
	case MPI2_IOCSTATUS_SAS_SMP_DATA_OVERRUN:
		desc = "smp data overrun";
		break;

/****************************************************************************
*  Diagnostic Buffer Post / Diagnostic Release values
****************************************************************************/

	case MPI2_IOCSTATUS_DIAGNOSTIC_RELEASED:
		desc = "diagnostic released";
		break;
	default:
		break;
	}

	if (!desc)
		return;

	switch (request_hdr->Function) {
	case MPI2_FUNCTION_CONFIG:
		frame_sz = sizeof(Mpi2ConfigRequest_t) + ioc->sge_size;
		func_str = "config_page";
		break;
	case MPI2_FUNCTION_SCSI_TASK_MGMT:
		frame_sz = sizeof(Mpi2SCSITaskManagementRequest_t);
		func_str = "task_mgmt";
		break;
	case MPI2_FUNCTION_SAS_IO_UNIT_CONTROL:
		frame_sz = sizeof(Mpi2SasIoUnitControlRequest_t);
		func_str = "sas_iounit_ctl";
		break;
	case MPI2_FUNCTION_SCSI_ENCLOSURE_PROCESSOR:
		frame_sz = sizeof(Mpi2SepRequest_t);
		func_str = "enclosure";
		break;
	case MPI2_FUNCTION_IOC_INIT:
		frame_sz = sizeof(Mpi2IOCInitRequest_t);
		func_str = "ioc_init";
		break;
	case MPI2_FUNCTION_PORT_ENABLE:
		frame_sz = sizeof(Mpi2PortEnableRequest_t);
		func_str = "port_enable";
		break;
	case MPI2_FUNCTION_SMP_PASSTHROUGH:
		frame_sz = sizeof(Mpi2SmpPassthroughRequest_t) + ioc->sge_size;
		func_str = "smp_passthru";
		break;
	case MPI2_FUNCTION_NVME_ENCAPSULATED:
		frame_sz = sizeof(Mpi26NVMeEncapsulatedRequest_t) +
		    ioc->sge_size;
		func_str = "nvme_encapsulated";
		break;
	default:
		frame_sz = 32;
		func_str = "unknown";
		break;
	}

	printk(MPT3SAS_WARN_FMT "ioc_status: %s(0x%04x), request(0x%p),"
	    " (%s)\n", ioc->name, desc, ioc_status, request_hdr, func_str);

	_debug_dump_mf(request_hdr, frame_sz/4);
}

/**
 * _base_display_event_data - verbose translation of firmware asyn events
 * @ioc: per adapter object
 * @mpi_reply: reply mf payload returned from firmware
 *
 * Return nothing.
 */
static void
_base_display_event_data(struct MPT3SAS_ADAPTER *ioc,
	Mpi2EventNotificationReply_t *mpi_reply)
{
	char *desc = NULL;
	u16 event;

	if (!(ioc->logging_level & MPT_DEBUG_EVENTS))
		return;

	event = le16_to_cpu(mpi_reply->Event);

	if (ioc->warpdrive_msg)	{
		switch (event) {
		case MPI2_EVENT_IR_OPERATION_STATUS:
		case MPI2_EVENT_IR_VOLUME:
		case MPI2_EVENT_IR_PHYSICAL_DISK:
		case MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST:
		case MPI2_EVENT_LOG_ENTRY_ADDED:
			return;
		}	
	}

	switch (event) {
	case MPI2_EVENT_LOG_DATA:
		desc = "Log Data";
		break;
	case MPI2_EVENT_STATE_CHANGE:
		desc = "Status Change";
		break;
	case MPI2_EVENT_HARD_RESET_RECEIVED:
		desc = "Hard Reset Received";
		break;
	case MPI2_EVENT_EVENT_CHANGE:
		desc = "Event Change";
		break;
	case MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE:
		desc = "Device Status Change";
		break;
	case MPI2_EVENT_IR_OPERATION_STATUS:
		desc = "IR Operation Status";
		break;
	case MPI2_EVENT_SAS_DISCOVERY:
	{
		Mpi2EventDataSasDiscovery_t *event_data =
		    (Mpi2EventDataSasDiscovery_t *)mpi_reply->EventData;
		printk(MPT3SAS_INFO_FMT "SAS Discovery: (%s)", ioc->name,
		    (event_data->ReasonCode == MPI2_EVENT_SAS_DISC_RC_STARTED) ?
		    "start" : "stop");
		if (event_data->DiscoveryStatus)
			printk("discovery_status(0x%08x)",
			    le32_to_cpu(event_data->DiscoveryStatus));
		printk("\n");
		return;
	}
	case MPI2_EVENT_SAS_BROADCAST_PRIMITIVE:
		desc = "SAS Broadcast Primitive";
		break;
	case MPI2_EVENT_SAS_INIT_DEVICE_STATUS_CHANGE:
		desc = "SAS Init Device Status Change";
		break;
	case MPI2_EVENT_SAS_INIT_TABLE_OVERFLOW:
		desc = "SAS Init Table Overflow";
		break;
	case MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
		desc = "SAS Topology Change List";
		break;
	case MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE:
		desc = "SAS Enclosure Device Status Change";
		break;
	case MPI2_EVENT_IR_VOLUME:
		desc = "IR Volume";
		break;
	case MPI2_EVENT_IR_PHYSICAL_DISK:
		desc = "IR Physical Disk";
		break;
	case MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST:
		desc = "IR Configuration Change List";
		break;
	case MPI2_EVENT_LOG_ENTRY_ADDED:
		desc = "Log Entry Added";
		break;
	case MPI2_EVENT_TEMP_THRESHOLD:
		desc = "Temperature Threshold";
		break;
	case MPI2_EVENT_ACTIVE_CABLE_EXCEPTION:
		desc = "Cable Event";
		break;
	case MPI2_EVENT_SAS_DEVICE_DISCOVERY_ERROR:
		desc = "SAS Device Discovery Error";
		break;
	case MPI2_EVENT_PCIE_DEVICE_STATUS_CHANGE:
		desc = "PCIE Device Status Change";
		break;
	case MPI2_EVENT_PCIE_ENUMERATION:
	{
		Mpi26EventDataPCIeEnumeration_t *event_data =
			(Mpi26EventDataPCIeEnumeration_t *)mpi_reply->EventData;
		printk(MPT3SAS_INFO_FMT "PCIE Enumeration: (%s)", ioc->name,
			   (event_data->ReasonCode == MPI26_EVENT_PCIE_ENUM_RC_STARTED) ?
			   "start" : "stop");
		if (event_data->EnumerationStatus)
			printk("enumeration_status(0x%08x)",
				   le32_to_cpu(event_data->EnumerationStatus));
		printk("\n");
		return;
	}
	case MPI2_EVENT_PCIE_TOPOLOGY_CHANGE_LIST:
		desc = "PCIE Topology Change List";
		break;
	}

	if (!desc)
		return;

	printk(MPT3SAS_INFO_FMT "%s\n", ioc->name, desc);
}

/**
 * _base_sas_log_info - verbose translation of firmware log info
 * @ioc: per adapter object
 * @log_info: log info
 *
 * Return nothing.
 */
static void
_base_sas_log_info(struct MPT3SAS_ADAPTER *ioc , u32 log_info)
{
	union loginfo_type {
		u32	loginfo;
		struct {
			u32	subcode:16;
			u32	code:8;
			u32	originator:4;
			u32	bus_type:4;
		} dw;
	};
	union loginfo_type sas_loginfo;
	char *originator_str = NULL;

	sas_loginfo.loginfo = log_info;
	if (sas_loginfo.dw.bus_type != 3 /*SAS*/)
		return;

	/* each nexus loss loginfo */
	if (log_info == 0x31170000)
		return;

	/* eat the loginfos associated with task aborts */
	if (ioc->ignore_loginfos && (log_info == 0x30050000 || log_info ==
	    0x31140000 || log_info == 0x31130000))
		return;

	switch (sas_loginfo.dw.originator) {
	case 0:
		originator_str = "IOP";
		break;
	case 1:
		originator_str = "PL";
		break;
	case 2:
		if (ioc->warpdrive_msg)
			originator_str = "WarpDrive";
		else
			originator_str = "IR";
		break;
	}

	printk(MPT3SAS_WARN_FMT "log_info(0x%08x): originator(%s), "
	    "code(0x%02x), sub_code(0x%04x)\n", ioc->name, log_info,
	     originator_str, sas_loginfo.dw.code,
	     sas_loginfo.dw.subcode);
}

/**
 * _base_display_reply_info -
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Return nothing.
 */
static void
_base_display_reply_info(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;
	u16 ioc_status;
	u32 loginfo = 0;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (unlikely(!mpi_reply)) {
		printk(MPT3SAS_ERR_FMT "mpi_reply not valid at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus);
	if ((ioc_status & MPI2_IOCSTATUS_MASK) &&
	    (ioc->logging_level & MPT_DEBUG_REPLY)) {
		_base_sas_ioc_info(ioc , mpi_reply,
		   mpt3sas_base_get_msg_frame(ioc, smid));
	}
	if (ioc_status & MPI2_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE) {
		loginfo = le32_to_cpu(mpi_reply->IOCLogInfo);
		_base_sas_log_info(ioc, loginfo);
	}

	if (ioc_status || loginfo) {
		ioc_status &= MPI2_IOCSTATUS_MASK;
		mpt3sas_trigger_mpi(ioc, ioc_status, loginfo);
	}
}

/**
 * mpt3sas_base_done - base internal command completion routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
u8
mpt3sas_base_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (mpi_reply && mpi_reply->Function == MPI2_FUNCTION_EVENT_ACK)
		return mpt3sas_check_for_pending_internal_cmds(ioc, smid);

	if (ioc->base_cmds.status == MPT3_CMD_NOT_USED)
		return 1;

	ioc->base_cmds.status |= MPT3_CMD_COMPLETE;
	if (mpi_reply) {
		ioc->base_cmds.status |= MPT3_CMD_REPLY_VALID;
		memcpy(ioc->base_cmds.reply, mpi_reply, mpi_reply->MsgLength*4);
	}
	ioc->base_cmds.status &= ~MPT3_CMD_PENDING;

	complete(&ioc->base_cmds.done);
	return 1;
}

/**
 * _base_async_event - main callback handler for firmware asyn events
 * @ioc: per adapter object
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_base_async_event(struct MPT3SAS_ADAPTER *ioc, u8 msix_index, u32 reply)
{
	Mpi2EventNotificationReply_t *mpi_reply;
	Mpi2EventAckRequest_t *ack_request;
	u16 smid;
	struct _event_ack_list *delayed_event_ack;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (!mpi_reply)
		return 1;
	if (mpi_reply->Function != MPI2_FUNCTION_EVENT_NOTIFICATION)
		return 1;
	_base_display_event_data(ioc, mpi_reply);
	if (!(mpi_reply->AckRequired & MPI2_EVENT_NOTIFICATION_ACK_REQUIRED))
		goto out;
	smid = mpt3sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		delayed_event_ack = kzalloc(sizeof(*delayed_event_ack), GFP_ATOMIC);
		if (!delayed_event_ack)
			goto out;
		INIT_LIST_HEAD(&delayed_event_ack->list);
		delayed_event_ack->Event = mpi_reply->Event;
		delayed_event_ack->EventContext = mpi_reply->EventContext;
		list_add_tail(&delayed_event_ack->list,
			      &ioc->delayed_event_ack_list);
		dewtprintk(ioc, printk(MPT3SAS_INFO_FMT
		    "DELAYED: EVENT ACK: event (0x%04x)\n",
		    ioc->name, le16_to_cpu(mpi_reply->Event)));
		goto out;
	}
	ack_request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(ack_request, 0, sizeof(Mpi2EventAckRequest_t));
	ack_request->Function = MPI2_FUNCTION_EVENT_ACK;
	ack_request->Event = mpi_reply->Event;
	ack_request->EventContext = mpi_reply->EventContext;
	ack_request->VF_ID = 0;  /* TODO */
	ack_request->VP_ID = 0;
	ioc->put_smid_default(ioc, smid);

 out:

	/* scsih callback handler */
	mpt3sas_scsih_event_callback(ioc, msix_index, reply);

	/* ctl callback handler */
	mpt3sas_ctl_event_callback(ioc, msix_index, reply);

	return 1;
}

inline struct scsiio_tracker *
mpt3sas_base_scsi_cmd_priv(struct scsi_cmnd *scmd)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
	return (struct scsiio_tracker *)scmd->host_scribble;
#else
	return scsi_cmd_priv(scmd);
#endif
}

struct scsiio_tracker *
mpt3sas_get_st_from_smid(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	struct scsi_cmnd *cmd;

	if (WARN_ON(!smid) ||
	    WARN_ON(smid >= ioc->hi_priority_smid))
		return NULL;

	cmd = mpt3sas_scsih_scsi_lookup_get(ioc, smid);
	if (cmd)
		return mpt3sas_base_scsi_cmd_priv(cmd);

	return NULL;
}

/**
 * _base_get_cb_idx - obtain the callback index
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return callback index.
 */
static u8
_base_get_cb_idx(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	int i;
	u16 ctl_smid = ioc->shost->can_queue + INTERNAL_SCSIIO_FOR_IOCTL;
	u16 discovery_smid =
	    ioc->shost->can_queue + INTERNAL_SCSIIO_FOR_DISCOVERY;
	u8 cb_idx = 0xFF;

	if (smid < ioc->hi_priority_smid) {
		struct scsiio_tracker *st;

		if (smid < ctl_smid) {
			st = mpt3sas_get_st_from_smid(ioc, smid);
			if (st)
				cb_idx = st->cb_idx;
		} else if (smid < discovery_smid)
			cb_idx = ioc->ctl_cb_idx;
		else
			cb_idx = ioc->scsih_cb_idx;
	} else if (smid < ioc->internal_smid) {
		i = smid - ioc->hi_priority_smid;
		cb_idx = ioc->hpr_lookup[i].cb_idx;
	} else if (smid <= ioc->hba_queue_depth) {
		i = smid - ioc->internal_smid;
		cb_idx = ioc->internal_lookup[i].cb_idx;
	}
	return cb_idx;
}

/**
 * mpt3sas_base_mask_interrupts - disable interrupts
 * @ioc: per adapter object
 *
 * Disabling ResetIRQ, Reply and Doorbell Interrupts
 *
 * Return nothing.
 */
void
mpt3sas_base_mask_interrupts(struct MPT3SAS_ADAPTER *ioc)
{
	u32 him_register;

	ioc->mask_interrupts = 1;
	him_register = ioc->base_readl(&ioc->chip->HostInterruptMask);
	him_register |= MPI2_HIM_DIM + MPI2_HIM_RIM + MPI2_HIM_RESET_IRQ_MASK;
	writel(him_register, &ioc->chip->HostInterruptMask);
	ioc->base_readl(&ioc->chip->HostInterruptMask);
}

/**
 * mpt3sas_base_unmask_interrupts - enable interrupts
 * @ioc: per adapter object
 *
 * Enabling only Reply Interrupts
 *
 * Return nothing.
 */
void
mpt3sas_base_unmask_interrupts(struct MPT3SAS_ADAPTER *ioc)
{
	u32 him_register;

	him_register = ioc->base_readl(&ioc->chip->HostInterruptMask);
	him_register &= ~MPI2_HIM_RIM;
	writel(him_register, &ioc->chip->HostInterruptMask);
	ioc->mask_interrupts = 0;
}

union reply_descriptor {
	u64 word;
	struct {
		u32 low;
		u32 high;
	} u;
};

/**
 * _base_process_reply_queue - process the reply descriptors from reply queue
 * @reply_q : per IRQ's reply queue object
 *
 * returns number of reply descriptors processed from a reply queue.
 */
int
_base_process_reply_queue(struct adapter_reply_queue *reply_q)
{
	union reply_descriptor rd;
	u64 completed_cmds;
	u8 request_descript_type;
	u16 smid;
	u8 cb_idx;
	u32 reply;
	u8 msix_index = reply_q->msix_index;
	struct MPT3SAS_ADAPTER *ioc = reply_q->ioc;
	Mpi2ReplyDescriptorsUnion_t *rpf;
	u8 rc;

	completed_cmds = 0;
	if (!atomic_add_unless(&reply_q->busy, 1, 1))
		return completed_cmds;

	rpf = &reply_q->reply_post_free[reply_q->reply_post_host_index];
	request_descript_type = rpf->Default.ReplyFlags
	     & MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;
	if (request_descript_type == MPI2_RPY_DESCRIPT_FLAGS_UNUSED) {
		atomic_dec(&reply_q->busy);
		return completed_cmds;
	}

	cb_idx = 0xFF;
	do {
		rd.word = le64_to_cpu(rpf->Words);
		if (rd.u.low == UINT_MAX || rd.u.high == UINT_MAX)
			goto out;
		reply = 0;
		smid = le16_to_cpu(rpf->Default.DescriptorTypeDependent1);
		if (request_descript_type ==
		    MPI25_RPY_DESCRIPT_FLAGS_FAST_PATH_SCSI_IO_SUCCESS ||
		    request_descript_type ==
		    MPI2_RPY_DESCRIPT_FLAGS_SCSI_IO_SUCCESS ||
		    request_descript_type ==
		    MPI26_RPY_DESCRIPT_FLAGS_PCIE_ENCAPSULATED_SUCCESS) {
			cb_idx = _base_get_cb_idx(ioc, smid);
			if ((likely(cb_idx < MPT_MAX_CALLBACKS)) &&
			    (likely(mpt_callbacks[cb_idx] != NULL))) {
				rc = mpt_callbacks[cb_idx](ioc, smid,
				    msix_index, 0);
				if (rc)
					mpt3sas_base_free_smid(ioc, smid);
			}
		} else if (request_descript_type ==
		    MPI2_RPY_DESCRIPT_FLAGS_ADDRESS_REPLY) {
			reply = le32_to_cpu(
			    rpf->AddressReply.ReplyFrameAddress);
			if (reply > ioc->reply_dma_max_address ||
			    reply < ioc->reply_dma_min_address)
				reply = 0;
			if (smid) {
				cb_idx = _base_get_cb_idx(ioc, smid);
				if ((likely(cb_idx < MPT_MAX_CALLBACKS)) &&
				    (likely(mpt_callbacks[cb_idx] != NULL))) {
					rc = mpt_callbacks[cb_idx](ioc, smid,
					    msix_index, reply);
					if (reply)
						_base_display_reply_info(ioc,
						    smid, msix_index, reply);
					if (rc)
						mpt3sas_base_free_smid(ioc,
						    smid);
				}
			} else {
#if defined(TARGET_MODE)
				if (stm_callbacks.smid_handler)
					stm_callbacks.smid_handler(ioc,
					    msix_index, reply);
#endif
				_base_async_event(ioc, msix_index, reply);
			}

			/* reply free queue handling */
			if (reply) {
				ioc->reply_free_host_index =
				    (ioc->reply_free_host_index ==
				    (ioc->reply_free_queue_depth - 1)) ?
				    0 : ioc->reply_free_host_index + 1;
				ioc->reply_free[ioc->reply_free_host_index] =
				    cpu_to_le32(reply);
				if (ioc->is_mcpu_endpoint)
					_base_clone_reply_to_sys_mem(ioc, reply,
								ioc->reply_free_host_index);
				wmb();
				writel(ioc->reply_free_host_index,
				    &ioc->chip->ReplyFreeHostIndex);

			}
#if defined(TARGET_MODE)
		} else if (request_descript_type ==
		    MPI2_RPY_DESCRIPT_FLAGS_TARGET_COMMAND_BUFFER) {
			if (stm_callbacks.target_command)
				stm_callbacks.target_command(ioc,
				    &rpf->TargetCommandBuffer, msix_index);
		} else if (request_descript_type ==
		    MPI2_RPY_DESCRIPT_FLAGS_TARGETASSIST_SUCCESS) {
			if (stm_callbacks.target_assist)
				stm_callbacks.target_assist(ioc,
				    &rpf->TargetAssistSuccess);
			mpt3sas_base_free_smid(ioc, smid);
		}
#else
		}
#endif

		rpf->Words = cpu_to_le64(ULLONG_MAX);
		reply_q->reply_post_host_index =
		    (reply_q->reply_post_host_index ==
		    (ioc->reply_post_queue_depth - 1)) ? 0 :
		    reply_q->reply_post_host_index + 1;
		request_descript_type =
		    reply_q->reply_post_free[reply_q->reply_post_host_index].
		    Default.ReplyFlags & MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;
		completed_cmds++;

		/* Update the reply post host index after continuously
		 * processing the threshold number of Reply Descriptors.
		 * So that FW can find enough entries to post the Reply
		 * Descriptors in the reply descriptor post queue.
		 */
		if (completed_cmds >= ioc->thresh_hold) {
			if (ioc->combined_reply_queue) {
				writel(reply_q->reply_post_host_index |
				 ((msix_index  & 7) <<
				 MPI2_RPHI_MSIX_INDEX_SHIFT),
				 ioc->replyPostRegisterIndex[msix_index/8]);
			} else {
				writel(reply_q->reply_post_host_index |
				 (msix_index << MPI2_RPHI_MSIX_INDEX_SHIFT),
				 &ioc->chip->ReplyPostHostIndex);
			}

#if defined(MPT3SAS_ENABLE_IRQ_POLL)
			if (!reply_q->irq_poll_scheduled) {
				reply_q->irq_poll_scheduled = true;
				irq_poll_sched(&reply_q->irqpoll);
			}
			atomic_dec(&reply_q->busy);
			return completed_cmds;
#endif
		}
		if (request_descript_type == MPI2_RPY_DESCRIPT_FLAGS_UNUSED)
			goto out;
		if (!reply_q->reply_post_host_index)
			rpf = reply_q->reply_post_free;
		else
			rpf++;
	} while (1);

 out:

	if (!completed_cmds) {
		atomic_dec(&reply_q->busy);
		return completed_cmds;
	}

	wmb();
 	if (ioc->is_warpdrive) {
 		writel(reply_q->reply_post_host_index,
 			ioc->reply_post_host_index[msix_index]);
		atomic_dec(&reply_q->busy);
		return completed_cmds;
	}

	if (ioc->combined_reply_queue) {
		writel(reply_q->reply_post_host_index | ((msix_index  & 7) <<
			MPI2_RPHI_MSIX_INDEX_SHIFT), ioc->replyPostRegisterIndex[msix_index/8]);
	} else {
		writel(reply_q->reply_post_host_index | (msix_index <<
			MPI2_RPHI_MSIX_INDEX_SHIFT), &ioc->chip->ReplyPostHostIndex);
	}
	atomic_dec(&reply_q->busy);
	return completed_cmds;
}

/**
 * _base_interrupt - MPT adapter (IOC) specific interrupt handler.
 * @irq: irq number (not used)
 * @bus_id: bus identifier cookie == pointer to MPT_ADAPTER structure
 * @r: pt_regs pointer (not used)
 *
 * Return IRQ_HANDLE if processed, else IRQ_NONE.
 */
static irqreturn_t
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
_base_interrupt(int irq, void *bus_id)
#else
_base_interrupt(int irq, void *bus_id, struct pt_regs *r)
#endif
{
	struct adapter_reply_queue *reply_q = bus_id;
	struct MPT3SAS_ADAPTER *ioc = reply_q->ioc;

	if (ioc->mask_interrupts)
		return IRQ_NONE;

#if defined(MPT3SAS_ENABLE_IRQ_POLL)
	if (reply_q->irq_poll_scheduled)
		return IRQ_HANDLED;
#endif

	return ((_base_process_reply_queue(reply_q) > 0) ?
	    IRQ_HANDLED : IRQ_NONE); 
}

#if defined(MPT3SAS_ENABLE_IRQ_POLL)
/**
 * _base_irqpoll - IRQ poll callback handler
 * @irqpoll - irq_poll object
 * @budget - irq poll weight
 *
 * returns number of reply descriptors processed
 */ 
int
_base_irqpoll(struct irq_poll *irqpoll, int budget) {
	struct adapter_reply_queue *reply_q;
	int num_entries = 0;

	reply_q = container_of(irqpoll, struct adapter_reply_queue,
            irqpoll);
	if (reply_q->irq_line_enable) {
		disable_irq_nosync(reply_q->os_irq);
		reply_q->irq_line_enable = false;
	}
	
	num_entries = _base_process_reply_queue(reply_q);
	if (num_entries < budget) {
		irq_poll_complete(irqpoll);
		reply_q->irq_poll_scheduled = false;
		reply_q->irq_line_enable = true;
		enable_irq(reply_q->os_irq);
	}

	return num_entries;
}

/**
 * _base_init_irqpolls - initliaze IRQ polls
 * @ioc: per adapter object
 *
 * returns nothing
 */  
void
_base_init_irqpolls(struct MPT3SAS_ADAPTER *ioc)
{
	struct adapter_reply_queue *reply_q, *next;



	if (list_empty(&ioc->reply_queue_list))
		return;

	list_for_each_entry_safe(reply_q, next, &ioc->reply_queue_list, list) {
		irq_poll_init(&reply_q->irqpoll, ioc->thresh_hold, _base_irqpoll);
		reply_q->irq_poll_scheduled = false;
		reply_q->irq_line_enable = true;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0))
		if (ioc->msix_enable)
			reply_q->os_irq = reply_q->vector;
		else
			reply_q->os_irq = ioc->pdev->irq;
#else
		reply_q->os_irq = pci_irq_vector(ioc->pdev,
		    reply_q->msix_index);
#endif
	}
}
#endif

/**
 * _base_is_controller_msix_enabled - is controller support muli-reply queues
 * @ioc: per adapter object
 *
 */
static inline int
_base_is_controller_msix_enabled(struct MPT3SAS_ADAPTER *ioc)
{
	return (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_MSI_X_INDEX) && ioc->msix_enable;
}

/**
 ** This routine was added because mpt3sas_base_flush_reply_queues()
 ** skips over reply queues that are currently busy (i.e. being handled
 ** by interrupt processing in another core). If a reply queue was busy,
 ** then we need to call synchronize_irq() to make sure the other core
 ** has finished flushing the queue and completed any calls to the
 ** mid-layer scsi_done() routine.
 ** It might be possible to just add the synchronize_irq() call to
 ** mpt3sas_base_flush_reply_queues(), but that means it would be called
 ** from an IRQ context, which may lead to deadlocks or other issues.
 **
 ** mpt3sas_base_sync_reply_irqs - flush pending MSIX interrupts
 ** @ioc: per adapter object
 ** @poll: poll over reply descriptor pools incase interrupt for
 ** 		timed-out SCSI command got delayed
 ** Context: non ISR conext
 **
 ** Called when a Task Management request has completed.
 **
 ** Return nothing.
 **/
void
mpt3sas_base_sync_reply_irqs(struct MPT3SAS_ADAPTER *ioc, u8 poll)
{
	struct adapter_reply_queue *reply_q;

	/* If MSIX capability is turned off
	 * then multi-queues are not enabled
	 */
	if (!_base_is_controller_msix_enabled(ioc))
		return;

	list_for_each_entry(reply_q, &ioc->reply_queue_list, list) {
		if (ioc->shost_recovery || ioc->remove_host ||
			ioc->pci_error_recovery)
			return;
		/* TMs are on msix_index == 0 */
		if (reply_q->msix_index == 0)
			continue;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0))
		synchronize_irq(reply_q->vector);
#else
		synchronize_irq(pci_irq_vector(ioc->pdev, reply_q->msix_index));
#endif
#if defined(MPT3SAS_ENABLE_IRQ_POLL)
		if (reply_q->irq_poll_scheduled) {
			/* Calling irq_poll_disable will wait for any pending
			 * callbacks to have completed.
			 */
			irq_poll_disable(&reply_q->irqpoll);
			irq_poll_enable(&reply_q->irqpoll);
			/* check how the scheduled poll has ended,
			 * clean up only if necessary
			 */
			if (reply_q->irq_poll_scheduled) {
				reply_q->irq_poll_scheduled = false;
				reply_q->irq_line_enable = true;
				enable_irq(reply_q->os_irq);
			}
		}
#endif
		if (poll)
			_base_process_reply_queue(reply_q);
	}
}


/**
 * mpt3sas_base_release_callback_handler - clear interupt callback handler
 * @cb_idx: callback index
 *
 * Return nothing.
 */
void
mpt3sas_base_release_callback_handler(u8 cb_idx)
{
	mpt_callbacks[cb_idx] = NULL;
}
#if defined(TARGET_MODE)
EXPORT_SYMBOL(mpt3sas_base_release_callback_handler);
#endif

/**
 * mpt3sas_base_register_callback_handler - obtain index for the ISR handler
 * @cb_func: callback function
 *
 * Returns cb_func.
 */
u8
mpt3sas_base_register_callback_handler(MPT_CALLBACK cb_func)
{
	u8 cb_idx;

	for (cb_idx = MPT_MAX_CALLBACKS-1; cb_idx; cb_idx--)
		if (mpt_callbacks[cb_idx] == NULL)
			break;

	mpt_callbacks[cb_idx] = cb_func;
	return cb_idx;
}
#if defined(TARGET_MODE)
EXPORT_SYMBOL(mpt3sas_base_register_callback_handler);
#endif

/**
 * mpt3sas_base_initialize_callback_handler - initialize the ISR handler
 *
 * Return nothing.
 */
void
mpt3sas_base_initialize_callback_handler(void)
{
	u8 cb_idx;

	for (cb_idx = 0; cb_idx < MPT_MAX_CALLBACKS; cb_idx++)
		mpt3sas_base_release_callback_handler(cb_idx);
}

#if defined(TARGET_MODE)
/**
 * mpt3sas_base_stm_release_callback_handler - clear STM callback handler
 *
 * Return nothing.
 */
void
mpt3sas_base_stm_release_callback_handler(void)
{
	stm_callbacks.watchdog = NULL;
	stm_callbacks.target_command = NULL;
	stm_callbacks.target_assist = NULL;
	stm_callbacks.smid_handler = NULL;
	stm_callbacks.reset_handler = NULL;
}
EXPORT_SYMBOL(mpt3sas_base_stm_release_callback_handler);

/**
 * mpt3sas_base_stm_register_callback_handler - Set the STM callbacks
 * @stm_funcs: Structure containing the function pointers
 *
 */
void
mpt3sas_base_stm_register_callback_handler(struct STM_CALLBACK stm_funcs)
{

	stm_callbacks.watchdog = stm_funcs.watchdog;
	stm_callbacks.target_command = stm_funcs.target_command;
	stm_callbacks.target_assist = stm_funcs.target_assist;
	stm_callbacks.smid_handler = stm_funcs.smid_handler;
	stm_callbacks.reset_handler = stm_funcs.reset_handler;
}
EXPORT_SYMBOL(mpt3sas_base_stm_register_callback_handler);

/**
 * mpt3sas_base_stm_initialize_callback_handler - initialize the stm handler
 *
 * Return nothing.
 */
void
mpt3sas_base_stm_initialize_callback_handler(void)
{
	mpt3sas_base_stm_release_callback_handler();
}
#endif

/**
 * _base_build_zero_len_sge - build zero length sg entry
 * @ioc: per adapter object
 * @paddr: virtual address for SGE
 *
 * Create a zero length scatter gather entry to insure the IOCs hardware has
 * something to use if the target device goes brain dead and tries
 * to send data even when none is asked for.
 *
 * Return nothing.
 */
static void
_base_build_zero_len_sge(struct MPT3SAS_ADAPTER *ioc, void *paddr)
{
	u32 flags_length = (u32)((MPI2_SGE_FLAGS_LAST_ELEMENT |
	    MPI2_SGE_FLAGS_END_OF_BUFFER | MPI2_SGE_FLAGS_END_OF_LIST |
	    MPI2_SGE_FLAGS_SIMPLE_ELEMENT) <<
	    MPI2_SGE_FLAGS_SHIFT);
	ioc->base_add_sg_single(paddr, flags_length, -1);
}

/**
 * _base_add_sg_single_32 - Place a simple 32 bit SGE at address pAddr.
 * @paddr: virtual address for SGE
 * @flags_length: SGE flags and data transfer length
 * @dma_addr: Physical address
 *
 * Return nothing.
 */
static void
_base_add_sg_single_32(void *paddr, u32 flags_length, dma_addr_t dma_addr)
{
	Mpi2SGESimple32_t *sgel = paddr;

	flags_length |= (MPI2_SGE_FLAGS_32_BIT_ADDRESSING |
	    MPI2_SGE_FLAGS_SYSTEM_ADDRESS) << MPI2_SGE_FLAGS_SHIFT;
	sgel->FlagsLength = cpu_to_le32(flags_length);
	sgel->Address = cpu_to_le32(dma_addr);
}


/**
 * _base_add_sg_single_64 - Place a simple 64 bit SGE at address pAddr.
 * @paddr: virtual address for SGE
 * @flags_length: SGE flags and data transfer length
 * @dma_addr: Physical address
 *
 * Return nothing.
 */
static void
_base_add_sg_single_64(void *paddr, u32 flags_length, dma_addr_t dma_addr)
{
	Mpi2SGESimple64_t *sgel = paddr;

	flags_length |= (MPI2_SGE_FLAGS_64_BIT_ADDRESSING |
	    MPI2_SGE_FLAGS_SYSTEM_ADDRESS) << MPI2_SGE_FLAGS_SHIFT;
	sgel->FlagsLength = cpu_to_le32(flags_length);
	sgel->Address = cpu_to_le64(dma_addr);
}

/**
 * _base_get_chain_buffer_tracker - obtain chain tracker
 * @ioc: per adapter object
 * @smid: smid associated to an IO request
 *
 * Returns chain tracker from chain_lookup table using key as
 * smid and smid's chain_offset.
 */
static struct chain_tracker *
_base_get_chain_buffer_tracker(struct MPT3SAS_ADAPTER *ioc,
			      struct scsi_cmnd *scmd)
{
	struct chain_tracker *chain_req;
	struct scsiio_tracker *st = mpt3sas_base_scsi_cmd_priv(scmd);
	u16 smid = st->smid;
	u8 chain_offset =
	   atomic_read(&ioc->chain_lookup[smid - 1].chain_offset);

	if (chain_offset == ioc->chains_needed_per_io)
		return NULL;

	chain_req = &ioc->chain_lookup[smid - 1].chains_per_smid[chain_offset];
	atomic_inc(&ioc->chain_lookup[smid - 1].chain_offset);
	return chain_req;
}


/**
 * _base_build_sg - build generic sg
 * @ioc: per adapter object
 * @psge: virtual address for SGE
 * @data_out_dma: physical address for WRITES
 * @data_out_sz: data xfer size for WRITES
 * @data_in_dma: physical address for READS
 * @data_in_sz: data xfer size for READS
 *
 * Return nothing.
 */
static void
_base_build_sg(struct MPT3SAS_ADAPTER *ioc, void *psge,
	dma_addr_t data_out_dma, size_t data_out_sz, dma_addr_t data_in_dma,
	size_t data_in_sz)
{
	u32 sgl_flags;

	if (!data_out_sz && !data_in_sz) {
		_base_build_zero_len_sge(ioc, psge);
		return;
	}

	if (data_out_sz && data_in_sz) {
		/* WRITE sgel first */
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_END_OF_BUFFER | MPI2_SGE_FLAGS_HOST_TO_IOC);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_out_sz, data_out_dma);

		/* incr sgel */
		psge += ioc->sge_size;

		/* READ sgel last */
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
		    MPI2_SGE_FLAGS_END_OF_LIST);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_in_sz, data_in_dma);
	} else if (data_out_sz) /* WRITE */ {
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
		    MPI2_SGE_FLAGS_END_OF_LIST | MPI2_SGE_FLAGS_HOST_TO_IOC);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_out_sz, data_out_dma);
	} else if (data_in_sz) /* READ */ {
		sgl_flags = (MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_LAST_ELEMENT | MPI2_SGE_FLAGS_END_OF_BUFFER |
		    MPI2_SGE_FLAGS_END_OF_LIST);
		sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;
		ioc->base_add_sg_single(psge, sgl_flags |
		    data_in_sz, data_in_dma);
	}
}

/* IEEE format sgls */

/**
* _base_build_nvme_prp - This function is called for NVMe end devices to build
* a native SGL (NVMe PRP). The native SGL is built starting in the first PRP
* entry of the NVMe message (PRP1).  If the data buffer is small enough to be
* described entirely using PRP1, then PRP2 is not used.  If needed, PRP2 is
* used to describe a larger data buffer.  If the data buffer is too large to
* describe using the two PRP entriess inside the NVMe message, then PRP1
* describes the first data memory segment, and PRP2 contains a pointer to a PRP
* list located elsewhere in memory to describe the remaining data memory
* segments.  The PRP list will be contiguous.

* The native SGL for NVMe devices is a Physical Region Page (PRP).  A PRP
* consists of a list of PRP entries to describe a number of noncontigous
* physical memory segments as a single memory buffer, just as a SGL does.  Note
* however, that this function is only used by the IOCTL call, so the memory
* given will be guaranteed to be contiguous.  There is no need to translate
* non-contiguous SGL into a PRP in this case.  All PRPs will describe
* contiguous space that is one page size each.
*
* Each NVMe message contains two PRP entries.  The first (PRP1) either contains
* a PRP list pointer or a PRP element, depending upon the command.  PRP2
* contains the second PRP element if the memory being described fits within 2
* PRP entries, or a PRP list pointer if the PRP spans more than two entries.
*
* A PRP list pointer contains the address of a PRP list, structured as a linear
* array of PRP entries.  Each PRP entry in this list describes a segment of
* physical memory.
*
* Each 64-bit PRP entry comprises an address and an offset field.  The address
* always points at the beginning of a 4KB physical memory page, and the offset 
* describes where within that 4KB page the memory segment begins.  Only the
* first element in a PRP list may contain a non-zero offest, implying that all
* memory segments following the first begin at the start of a 4KB page.
*
* Each PRP element normally describes 4KB of physical memory, with exceptions
* for the first and last elements in the list.  If the memory being described
* by the list begins at a non-zero offset within the first 4KB page, then the
* first PRP element will contain a non-zero offset indicating where the region
* begins within the 4KB page.  The last memory segment may end before the end
* of the 4KB segment, depending upon the overall size of the memory being
* described by the PRP list. 
*
* Since PRP entries lack any indication of size, the overall data buffer length
* is used to determine where the end of the data memory buffer is located, and
* how many PRP entries are required to describe it.
*
* @ioc: per adapter object 
* @smid: system request message index for getting asscociated SGL
* @nvme_encap_request: the NVMe request msg frame pointer
* @data_out_dma: physical address for WRITES
* @data_out_sz: data xfer size for WRITES
* @data_in_dma: physical address for READS
* @data_in_sz: data xfer size for READS
*
* Returns nothing.
*/
static void
_base_build_nvme_prp(struct MPT3SAS_ADAPTER *ioc, u16 smid,
    Mpi26NVMeEncapsulatedRequest_t *nvme_encap_request,
    dma_addr_t data_out_dma, size_t data_out_sz, dma_addr_t data_in_dma,
    size_t data_in_sz)
{
	int		prp_size = NVME_PRP_SIZE;
	u64		*prp_entry, *prp1_entry, *prp2_entry;
	u64		*prp_page;
	dma_addr_t	prp_entry_dma, prp_page_dma, dma_addr;
	u32		offset, entry_len;
	u32		page_mask_result, page_mask;
	size_t		length;
	struct mpt3sas_nvme_cmd *nvme_cmd =
		(void *)nvme_encap_request->NVMe_Command;

	/*
	 * Not all commands require a data transfer. If no data, just return
	 * without constructing any PRP.
	 */
	if (!data_in_sz && !data_out_sz)
		return;

	/*
	 * Set pointers to PRP1 and PRP2, which are in the NVMe command.
	 * PRP1 is located at a 24 byte offset from the start of the NVMe
	 * command.  Then set the current PRP entry pointer to PRP1.
	 */
	prp1_entry = &nvme_cmd->prp1;
	prp2_entry = &nvme_cmd->prp2;
	prp_entry = prp1_entry;

	/*
	 * For the PRP entries, use the specially allocated buffer of
	 * contiguous memory.
	 * We don't need any PRP list, if data lengh is <= page_size,
	 * Also we are reusing this buffer while framing native NVMe 
	 * DSM command for SCSI UNMAP.
	 */
	if (data_in_sz > ioc->page_size || data_out_sz > ioc->page_size) {
		prp_page = (u64 *)mpt3sas_base_get_pcie_sgl(ioc, smid);
		prp_page_dma = mpt3sas_base_get_pcie_sgl_dma(ioc, smid);
	} else {
		prp_page = NULL;
		prp_page_dma = 0;
	}

	/*
	 * Check if we are within 1 entry of a page boundary we don't
	 * want our first entry to be a PRP List entry.
	 */
	page_mask = ioc->page_size - 1;
	page_mask_result = (uintptr_t)((u8 *)prp_page + prp_size) & page_mask;
	if (!page_mask_result)
	{
		/* Bump up to next page boundary. */
		prp_page = (u64 *)((u8 *)prp_page + prp_size);
		prp_page_dma = prp_page_dma + prp_size;
	}

	/*
	 * Set PRP physical pointer, which initially points to the current PRP
	 * DMA memory page.
	 */
	prp_entry_dma = prp_page_dma;

	/* Get physical address and length of the data buffer. */
	if (data_in_sz)
	{
		dma_addr = data_in_dma;
		length = data_in_sz;
	}
	else
	{
		dma_addr = data_out_dma;
		length = data_out_sz;
	}

	/* Loop while the length is not zero. */
	while (length)
	{
		/*
		 * Check if we need to put a list pointer here if we are at
		 * page boundary - prp_size (8 bytes).
		 */
		page_mask_result = (prp_entry_dma + prp_size) & page_mask;
		if (!page_mask_result)
		{
			/*
			 * This is the last entry in a PRP List, so we need to
			 * put a PRP list pointer here.  What this does is:
			 *   - bump the current memory pointer to the next
			 *     address, which will be the next full page.
			 *   - set the PRP Entry to point to that page.  This
			 *     is now the PRP List pointer.
			 *   - bump the PRP Entry pointer the start of the
			 *     next page.  Since all of this PRP memory is
			 *     contiguous, no need to get a new page - it's
			 *     just the next address.
			 */
			prp_entry_dma++;
			*prp_entry = cpu_to_le64(prp_entry_dma);
			prp_entry++;
		}

		/* Need to handle if entry will be part of a page. */
		offset = dma_addr & page_mask;
		entry_len = ioc->page_size - offset;

		if (prp_entry == prp1_entry)
		{
			/*
			 * Must fill in the first PRP pointer (PRP1) before
			 * moving on.
			 */
			*prp1_entry = cpu_to_le64(dma_addr);

			/*
			 * Now point to the second PRP entry within the
			 * command (PRP2).
			 */
			prp_entry = prp2_entry;
		}
		else if (prp_entry == prp2_entry)
		{
			/*
			 * Should the PRP2 entry be a PRP List pointer or just
			 * a regular PRP pointer?  If there is more than one
			 * more page of data, must use a PRP List pointer.
			 */
			if (length > ioc->page_size)
			{
				/*
				 * PRP2 will contain a PRP List pointer because
				 * more PRP's are needed with this command. The
				 * list will start at the beginning of the
				 * contiguous buffer.
				 */
				*prp2_entry = cpu_to_le64(prp_entry_dma);

				/*
				 * The next PRP Entry will be the start of the
				 * first PRP List.
				 */
				prp_entry = prp_page;
			}
			else
			{
				/*
				 * After this, the PRP Entries are complete.
				 * This command uses 2 PRP's and no PRP list.
				 */
				*prp2_entry = cpu_to_le64(dma_addr);
			}
		}
		else
		{
			/*
			 * Put entry in list and bump the addresses.
			 *
			 * After PRP1 and PRP2 are filled in, this will fill in
			 * all remaining PRP entries in a PRP List, one per
			 * each time through the loop.
			 */
			*prp_entry = cpu_to_le64(dma_addr);
			prp_entry++;
			prp_entry_dma++;
		}

		/*
		 * Bump the phys address of the command's data buffer by the
		 * entry_len.
		 */
		dma_addr += entry_len;

		/* Decrement length accounting for last partial page. */
		if (entry_len > length)
			length = 0;
		else
			length -= entry_len;
	}
}

/**
 * base_make_prp_nvme -
 * Prepare PRPs(Physical Region Page)- SGLs specific to NVMe drives only
 *
 * @ioc:		per adapter object
 * @scmd:		SCSI command from the mid-layer
 * @sg_scmd:		SG list pointer
 * @mpi_request:	mpi request
 * @smid:		msg Index
 * @sge_count:		scatter gather element count.
 *
 * Returns:		Nothing
 */
void
base_make_prp_nvme(struct MPT3SAS_ADAPTER *ioc,
		struct scsi_cmnd *scmd, 
		struct scatterlist *sg_scmd,
		Mpi25SCSIIORequest_t *mpi_request,
		u16 smid, int sge_count)
{
	int sge_len, num_prp_in_chain = 0;
	Mpi25IeeeSgeChain64_t *main_chain_element, *ptr_first_sgl;
	u64 *curr_buff;
	dma_addr_t msg_dma, sge_addr, offset;
	u32 page_mask, page_mask_result;
	u32 first_prp_len;
	int data_len;
	u32 nvme_pg_size;

	nvme_pg_size = max_t(u32, ioc->page_size, NVME_PRP_PAGE_SIZE);
	/*
	 * Nvme has a very convoluted prp format.  One prp is required
	 * for each page or partial page. Driver need to split up OS sg_list
	 * entries if it is longer than one page or cross a page
	 * boundary.  Driver also have to insert a PRP list pointer entry as
	 * the last entry in each physical page of the PRP list.
	 *
	 * NOTE: The first PRP "entry" is actually placed in the first
	 * SGL entry in the main message as IEEE 64 format.  The 2nd
	 * entry in the main message is the chain element, and the rest
	 * of the PRP entries are built in the contiguous pcie buffer.
	 */
	page_mask = nvme_pg_size - 1;

	/*
	 * Native SGL is needed.
	 * Put a chain element in main message frame that points to the first
	 * chain buffer.
	 *
	 * NOTE:  The ChainOffset field must be 0 when using a chain pointer to
	 *        a native SGL.
	 */

	/* Set main message chain element pointer */
	main_chain_element = (pMpi25IeeeSgeChain64_t)&mpi_request->SGL;
	/*
	 * For NVMe the chain element needs to be the 2nd SG entry in the main
	 * message.
	 */
	main_chain_element = (Mpi25IeeeSgeChain64_t *)
		((u8 *)main_chain_element + sizeof(MPI25_IEEE_SGE_CHAIN64));

	/*
	 * For the PRP entries, use the specially allocated buffer of
	 * contiguous memory.  Normal chain buffers can't be used
	 * because each chain buffer would need to be the size of an OS
	 * page (4k).
	 */
	curr_buff = mpt3sas_base_get_pcie_sgl(ioc, smid);
	msg_dma = mpt3sas_base_get_pcie_sgl_dma(ioc, smid);

	main_chain_element->Address = cpu_to_le64(msg_dma);
	main_chain_element->NextChainOffset = 0;
	main_chain_element->Flags = MPI2_IEEE_SGE_FLAGS_CHAIN_ELEMENT |
			MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR |
			MPI26_IEEE_SGE_FLAGS_NSF_NVME_PRP;

	/* Build first prp, sge need not to be page aligned*/
	ptr_first_sgl = (pMpi25IeeeSgeChain64_t)&mpi_request->SGL;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	data_len = scmd->request_bufflen;
#else
	data_len = scsi_bufflen(scmd);
#endif
	sge_addr = sg_dma_address(sg_scmd);
	sge_len = sg_dma_len(sg_scmd);

	offset = sge_addr & page_mask;
	first_prp_len = nvme_pg_size - offset;

	ptr_first_sgl->Address = cpu_to_le64(sge_addr);
	ptr_first_sgl->Length = cpu_to_le32(first_prp_len);

	data_len -= first_prp_len;

	if (sge_len > first_prp_len) {
		sge_addr += first_prp_len;
		sge_len -= first_prp_len;
	} else if (data_len && (sge_len == first_prp_len)) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
		sg_scmd++;
#else
		sg_scmd = sg_next(sg_scmd);
#endif
		sge_addr = sg_dma_address(sg_scmd);
		sge_len = sg_dma_len(sg_scmd);
	}

	for (;;) {
		offset = sge_addr & page_mask;

		/* Put PRP pointer due to page boundary*/
		page_mask_result = (uintptr_t)(curr_buff + 1) & page_mask;
		if (unlikely(!page_mask_result)) {
			scmd_printk(KERN_NOTICE,
				scmd, "page boundary curr_buff: 0x%p\n",
				curr_buff);
			msg_dma += 8;
			*curr_buff = cpu_to_le64(msg_dma);
			curr_buff++;
			num_prp_in_chain++;
		}

		*curr_buff = cpu_to_le64(sge_addr);
		curr_buff++;
		msg_dma += 8;
		num_prp_in_chain++;

		sge_addr += nvme_pg_size;
		sge_len -= nvme_pg_size;
		data_len -= nvme_pg_size;

		if (data_len <= 0)
			break;

		if (sge_len > 0)
			continue;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
		sg_scmd++;
#else
		sg_scmd = sg_next(sg_scmd);
#endif
		sge_addr = sg_dma_address(sg_scmd);
		sge_len = sg_dma_len(sg_scmd);
	}

	main_chain_element->Length =
		cpu_to_le32(num_prp_in_chain * sizeof(u64));

	return;
}

u32 base_mod64(u64 dividend, u32 divisor)
{
	u32 remainder;

	if (!divisor)
		pr_err(KERN_ERR "mpt3sas : DIVISOR is zero, in div fn\n");
	remainder = do_div(dividend, divisor);
	return remainder;
}


/**
 * base_is_prp_possible - This function is called for PCIe end devices to
 * check if we need to build a native NVMe PRP.
 * @ioc: per adapter object
 * @pcie_device: points to the PCIe device's info
 * @scmd: scsi command
 * @sg_scmd: SG list pointer
 * @sge_count: scatter gather element count.
 *
 * Returns 1 if native NVMe PRP to be built else 0 to build native SGL.
 */
static bool
base_is_prp_possible(struct MPT3SAS_ADAPTER *ioc,
	struct _pcie_device *pcie_device, struct scsi_cmnd *scmd, 
	struct scatterlist *sg_scmd, int sge_count)
{
	u32 data_length = 0;
	bool build_prp = true;
	u32 i, nvme_pg_size;

	nvme_pg_size = max_t(u32, ioc->page_size,
			NVME_PRP_PAGE_SIZE);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	data_length = cpu_to_le32(scmd->request_bufflen);
#else
	data_length = cpu_to_le32(scsi_bufflen(scmd));
#endif

	if (pcie_device &&
		(mpt3sas_scsih_is_pcie_scsi_device(pcie_device->device_info))) {
		build_prp = false;
		return build_prp;
	}

	/* If Datalenth is <= 16K and number of SGE’s entries are <= 2
	 * we built IEEE SGL
	 */
	if ((data_length <= NVME_PRP_PAGE_SIZE*4) && (sge_count <= 2)) {
		build_prp = false;
		return build_prp;
	}
	/*
	 ** Below code detects gaps/holes in IO data buffers.
	 ** What does holes/gaps mean?
	 ** Any SGE except first one in a SGL starts at non NVME page size
	 ** aligned address OR Any SGE except last one in a SGL ends at
	 ** non NVME page size boundary.
	 **
	 ** Driver has already informed block layer by setting boundary rules
	 ** for bio merging done at NVME page size boundary calling kernel API
	 ** blk_queue_virt_boundary inside slave_config.
	 ** Still there is possibility of IO coming with holes to driver because
	 ** of IO merging done by IO scheduler.
	 **
	 ** With SCSI BLK MQ enabled, there will be no IO with holes as there
	 ** is no IO scheduling so no IO merging.
	 **
	 ** With SCSI BLK MQ disabled, IO scheduler may attempt to merge IOs and
	 ** then sending IOs with holes.
	 **
	 ** Though driver can request block layer to disable IO merging by
	 ** calling queue_flag_set_unlocked(QUEUE_FLAG_NOMERGES,
	 ** sdev->request_queue) but user may tune sysfs parameter- nomerges
	 ** again to 0 or 1.
	 **
	 ** If in future IO scheduling is enabled with SCSI BLK MQ,
	 ** this algorithm to detect holes will be required in driver
	 ** for SCSI BLK MQ enabled case as well.
	 **
	 **/
	scsi_for_each_sg(scmd, sg_scmd, sge_count, i) {
		if ((i != 0) && (i != (sge_count - 1))) {
			if (base_mod64(sg_dma_len(sg_scmd), nvme_pg_size) ||
				base_mod64(sg_dma_address(sg_scmd),
					nvme_pg_size)) {
				build_prp = false;
				break;
			}
		}

		if ((sge_count > 1) && (i == 0)) {
			if ((base_mod64((sg_dma_address(sg_scmd) +
				sg_dma_len(sg_scmd)), nvme_pg_size))) {
				build_prp = false;
				break;
			}
		}

		if ((sge_count > 1) && (i == (sge_count - 1))) {
			if (base_mod64(sg_dma_address(sg_scmd), nvme_pg_size)) {
				build_prp = false;
				break;
			}
		}
	}
	return build_prp;
}

/**
 * _base_check_pcie_native_sgl - This function is called for PCIe end devices to
 * determine if the driver needs to build a native SGL.  If so, that native
 * SGL is built in the special contiguous buffers allocated especially for
 * PCIe SGL creation.  If the driver will not build a native SGL, return
 * TRUE and a normal IEEE SGL will be built.  Currently this routine
 * supports NVMe.
 * @ioc: per adapter object
 * @mpi_request: mf request pointer
 * @smid: system request message index
 * @scmd: scsi command
 * @pcie_device: points to the PCIe device's info
 *
 * Returns 0 if native SGL was built, 1 if no SGL was built
 */
static int
_base_check_pcie_native_sgl(struct MPT3SAS_ADAPTER *ioc,
	Mpi25SCSIIORequest_t *mpi_request, u16 smid, struct scsi_cmnd *scmd,
	struct _pcie_device *pcie_device)
{
	struct scatterlist *sg_scmd;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	u32 sges_left;
#else
	int sges_left;
#endif

	/* Get the SG list pointer and info. */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	if (!scmd->use_sg) {
		/* single buffer sge */
		scmd->SCp.dma_handle = pci_map_single(ioc->pdev,
		    scmd->request_buffer, scmd->request_bufflen,
		    scmd->sc_data_direction);
		if (pci_dma_mapping_error(scmd->SCp.dma_handle)) {
			sdev_printk(KERN_ERR, scmd->device, "pci_map_single"
			" failed: request for %d bytes!\n",
			scmd->request_bufflen);
			return 1;
		}
		sg_scmd = (struct scatterlist *) scmd->request_buffer;
		sges_left = 1;
	} else {
		/* sg list provided */
		sg_scmd = (struct scatterlist *) scmd->request_buffer;
		sges_left = pci_map_sg(ioc->pdev, sg_scmd, scmd->use_sg,
		    scmd->sc_data_direction);

		if (!sges_left) {
			sdev_printk(KERN_ERR, scmd->device, "pci_map_sg"
			    " failed: request for %d bytes!\n",
			    scmd->request_bufflen);
			return 1;
		}
	}
#else
	sg_scmd = scsi_sglist(scmd);
	sges_left = scsi_dma_map(scmd);
	if (sges_left < 0) {
		sdev_printk(KERN_ERR, scmd->device, "scsi_dma_map"
		" failed: request for %d bytes!\n", scsi_bufflen(scmd));
		return 1;
	}
#endif

	/* Check if we need to build a native SG list. */
	if (base_is_prp_possible(ioc, pcie_device,
				scmd, sg_scmd, sges_left) == 0) {
		/* We built a native SG list, just return. */
		goto out;
	}

	/*
	 * Build native NVMe PRP.
	 */
	base_make_prp_nvme(ioc, scmd, sg_scmd, mpi_request,
			smid, sges_left);

	return 0;
out:
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
#endif
	return 1;
}

/**
 * _base_build_sg_scmd - mpt2sas main sg creation routine
 *		pcie_device is unused here!
 * @ioc: per adapter object
 * @scmd: scsi command
 * @smid: system request message index 
 * @unused: unused pcie_device pointer
 * 
 * Context: none.
 *
 * The main routine that builds scatter gather table from a given
 * scsi request sent via the .queuecommand main handler.
 *
 * Returns 0 success, anything else error
 */
static int
_base_build_sg_scmd(struct MPT3SAS_ADAPTER *ioc,
	struct scsi_cmnd *scmd, u16 smid, struct _pcie_device *unused)
{
	Mpi25SCSIIORequest_t *mpi_request;
	dma_addr_t chain_dma;
	struct scatterlist *sg_scmd;
	void *sg_local, *chain;
	u32 chain_offset;
	u32 chain_length;
	u32 chain_flags;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	u32 sges_left;
#else
	int sges_left;
#endif
	u32 sges_in_segment;
	u32 sgl_flags;
	u32 sgl_flags_last_element;
	u32 sgl_flags_end_buffer;
	struct chain_tracker *chain_req;

	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);

	/* init scatter gather flags */
	sgl_flags = MPI2_SGE_FLAGS_SIMPLE_ELEMENT;
	if (scmd->sc_data_direction == DMA_TO_DEVICE)
		sgl_flags |= MPI2_SGE_FLAGS_HOST_TO_IOC;
	sgl_flags_last_element = (sgl_flags | MPI2_SGE_FLAGS_LAST_ELEMENT)
	    << MPI2_SGE_FLAGS_SHIFT;
	sgl_flags_end_buffer = (sgl_flags | MPI2_SGE_FLAGS_LAST_ELEMENT |
	    MPI2_SGE_FLAGS_END_OF_BUFFER | MPI2_SGE_FLAGS_END_OF_LIST)
	    << MPI2_SGE_FLAGS_SHIFT;
	sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	/* single buffer sge */
	if (!scmd->use_sg) {
		scmd->SCp.dma_handle = pci_map_single(ioc->pdev,
		    scmd->request_buffer, scmd->request_bufflen,
		    scmd->sc_data_direction);
		if (pci_dma_mapping_error(scmd->SCp.dma_handle)) {
			sdev_printk(KERN_ERR, scmd->device, "pci_map_single"
			" failed: request for %d bytes!\n",
			scmd->request_bufflen);
			return -ENOMEM;
		}
		ioc->base_add_sg_single(&mpi_request->SGL,
		    sgl_flags_end_buffer | scmd->request_bufflen,
		    scmd->SCp.dma_handle);
		return 0;
	}

	/* sg list provided */
	sg_scmd = (struct scatterlist *) scmd->request_buffer;
	sges_left = pci_map_sg(ioc->pdev, sg_scmd, scmd->use_sg,
	    scmd->sc_data_direction);

#if defined(CRACK_MONKEY_EEDP) 
	if (!ioc->disable_eedp_support) {
		if (scmd->cmnd[0] == INQUIRY) {
			scmd->host_scribble =
				page_address(((struct scatterlist *)
					scmd->request_buffer)[0].page)+
				      ((struct scatterlist *)
				       scmd->request_buffer)[0].offset;
		}
	}
#endif /* CRACK_MONKEY_EEDP */
	if (!sges_left) {
		sdev_printk(KERN_ERR, scmd->device, "pci_map_sg"
		" failed: request for %d bytes!\n", scmd->request_bufflen);
		return -ENOMEM;
	}
#else
	sg_scmd = scsi_sglist(scmd);
	sges_left = scsi_dma_map(scmd);
	if (sges_left < 0) {
		sdev_printk(KERN_ERR, scmd->device, "scsi_dma_map"
		" failed: request for %d bytes!\n", scsi_bufflen(scmd));
		return -ENOMEM;
	}

#if defined(CRACK_MONKEY_EEDP)
	if (!ioc->disable_eedp_support) {
		if (scmd->cmnd[0] == INQUIRY)
			scmd->host_scribble = page_address(sg_page(sg_scmd)) +
				sg_scmd[0].offset;
	}
#endif /* CRACK_MONKEY_EEDP */
#endif


	sg_local = &mpi_request->SGL;
	sges_in_segment = ioc->max_sges_in_main_message;
	if (sges_left <= sges_in_segment)
		goto fill_in_last_segment;

	mpi_request->ChainOffset = (offsetof(Mpi2SCSIIORequest_t, SGL) +
	    (sges_in_segment * ioc->sge_size))/4;

	/* fill in main message segment when there is a chain following */
	while (sges_in_segment) {
		if (sges_in_segment == 1)
			ioc->base_add_sg_single(sg_local,
			    sgl_flags_last_element | sg_dma_len(sg_scmd),
			    sg_dma_address(sg_scmd));
		else
			ioc->base_add_sg_single(sg_local, sgl_flags |
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
		sg_scmd++;
#else
		sg_scmd = sg_next(sg_scmd);
#endif
		sg_local += ioc->sge_size;
		sges_left--;
		sges_in_segment--;
	}

	/* initializing the chain flags and pointers */
	chain_flags = MPI2_SGE_FLAGS_CHAIN_ELEMENT << MPI2_SGE_FLAGS_SHIFT;
	chain_req = _base_get_chain_buffer_tracker(ioc, scmd);
	if (!chain_req)
		return -1;
	chain = chain_req->chain_buffer;
	chain_dma = chain_req->chain_buffer_dma;
	do {
		sges_in_segment = (sges_left <=
		    ioc->max_sges_in_chain_message) ? sges_left :
		    ioc->max_sges_in_chain_message;
		chain_offset = (sges_left == sges_in_segment) ?
		    0 : (sges_in_segment * ioc->sge_size)/4;
		chain_length = sges_in_segment * ioc->sge_size;
		if (chain_offset) {
			chain_offset = chain_offset <<
			    MPI2_SGE_CHAIN_OFFSET_SHIFT;
			chain_length += ioc->sge_size;
		}
		ioc->base_add_sg_single(sg_local, chain_flags | chain_offset |
		    chain_length, chain_dma);
		sg_local = chain;
		if (!chain_offset)
			goto fill_in_last_segment;

		/* fill in chain segments */
		while (sges_in_segment) {
			if (sges_in_segment == 1)
				ioc->base_add_sg_single(sg_local,
				    sgl_flags_last_element |
				    sg_dma_len(sg_scmd),
				    sg_dma_address(sg_scmd));
			else
				ioc->base_add_sg_single(sg_local, sgl_flags |
				    sg_dma_len(sg_scmd),
				    sg_dma_address(sg_scmd));
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
			sg_scmd++;
#else
			sg_scmd = sg_next(sg_scmd);
#endif
			sg_local += ioc->sge_size;
			sges_left--;
			sges_in_segment--;
		}

		chain_req = _base_get_chain_buffer_tracker(ioc, scmd);
		if (!chain_req)
			return -1;
		chain = chain_req->chain_buffer;
		chain_dma = chain_req->chain_buffer_dma;
	} while (1);


 fill_in_last_segment:

	/* fill the last segment */
	while (sges_left) {
		if (sges_left == 1)
			ioc->base_add_sg_single(sg_local, sgl_flags_end_buffer |
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		else
			ioc->base_add_sg_single(sg_local, sgl_flags |
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
		sg_scmd++;
#else
		sg_scmd = sg_next(sg_scmd);
#endif
		sg_local += ioc->sge_size;
		sges_left--;
	}

	return 0;
}

/* IEEE format sgls */

/**
 * _base_add_sg_single_ieee - add sg element for IEEE format
 * @paddr: virtual address for SGE
 * @flags: SGE flags
 * @chain_offset: number of 128 byte elements from start of segment
 * @length: data transfer length
 * @dma_addr: Physical address
 *
 * Return nothing.
 */
static void
_base_add_sg_single_ieee(void *paddr, u8 flags, u8 chain_offset, u32 length,
	dma_addr_t dma_addr)
{
	Mpi25IeeeSgeChain64_t *sgel = paddr;

	sgel->Flags = flags;
	sgel->NextChainOffset = chain_offset;
	sgel->Length = cpu_to_le32(length);
	sgel->Address = cpu_to_le64(dma_addr);
}

/**
 * _base_build_zero_len_sge_ieee - build zero length sg entry for IEEE format
 * @ioc: per adapter object
 * @paddr: virtual address for SGE
 *
 * Create a zero length scatter gather entry to insure the IOCs hardware has
 * something to use if the target device goes brain dead and tries
 * to send data even when none is asked for.
 *
 * Return nothing.
 */
static void
_base_build_zero_len_sge_ieee(struct MPT3SAS_ADAPTER *ioc, void *paddr)
{
	u8 sgl_flags = (MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
		MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR |
		MPI25_IEEE_SGE_FLAGS_END_OF_LIST);

	_base_add_sg_single_ieee(paddr, sgl_flags, 0, 0, -1);
}

/**
 * _base_build_sg_scmd_ieee - main sg creation routine for IEEE format
 * @ioc: per adapter object
 * @scmd: scsi command
 * @smid: system request message index 
 * @pcie_device: Pointer to pcie_device. If set, the pcie native sgl will be
 * constructed on need.
 * 
 * Context: none.
 *
 * The main routine that builds scatter gather table from a given
 * scsi request sent via the .queuecommand main handler.
 *
 * Returns 0 success, anything else error
 */
static int
_base_build_sg_scmd_ieee(struct MPT3SAS_ADAPTER *ioc,
	struct scsi_cmnd *scmd, u16 smid, struct _pcie_device *pcie_device)
{
	Mpi25SCSIIORequest_t *mpi_request;
	dma_addr_t chain_dma;
	struct scatterlist *sg_scmd;	/* s/g data entry */
	void *sg_local, *chain, *sgl_zero_addr;
	u32 chain_offset;
	u32 chain_length;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	u32 sges_left;
#else
	int sges_left;
#endif
	u32 sges_in_segment, sges_in_request_frame = 0;
	u8 simple_sgl_flags;
	u8 simple_sgl_flags_last;
	u8 chain_sgl_flags;
	struct chain_tracker *chain_req;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
	struct scatterlist *sg_prot_scmd = NULL; /* s/g prot entry */
	void *sg_prot_local;
	int prot_sges_left;
	u32 prot_sges_in_segment;
#endif
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);

	/* init scatter gather flags */
	simple_sgl_flags = MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR;
	simple_sgl_flags_last = simple_sgl_flags |
	    MPI25_IEEE_SGE_FLAGS_END_OF_LIST;
	chain_sgl_flags = MPI2_IEEE_SGE_FLAGS_CHAIN_ELEMENT |
	    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR;

	/* Check if we need to build a native SG list. */
	if ((pcie_device) && (_base_check_pcie_native_sgl(ioc, mpi_request,
	    smid, scmd, pcie_device) == 0)) {
		/* We built a native SG list, just return. */
		return 0;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	/* single buffer sge */
	if (!scmd->use_sg) {
		scmd->SCp.dma_handle = pci_map_single(ioc->pdev,
		    scmd->request_buffer, scmd->request_bufflen,
		    scmd->sc_data_direction);
		if (pci_dma_mapping_error(scmd->SCp.dma_handle)) {
			sdev_printk(KERN_ERR, scmd->device, "pci_map_single"
			" failed: request for %d bytes!\n",
			scmd->request_bufflen);
			return -ENOMEM;
		}

		_base_add_sg_single_ieee(&mpi_request->SGL,
		    simple_sgl_flags_last, 0, scmd->request_bufflen,
		    scmd->SCp.dma_handle);
		return 0;
	}

	/* sg list provided */
	sg_scmd = (struct scatterlist *) scmd->request_buffer;
	sges_left = pci_map_sg(ioc->pdev, sg_scmd, scmd->use_sg,
	    scmd->sc_data_direction);

#if defined(CRACK_MONKEY_EEDP)
	if (!ioc->disable_eedp_support) {
		if (scmd->cmnd[0] == INQUIRY) {
			scmd->host_scribble =
				page_address(((struct scatterlist *)
					scmd->request_buffer)[0].page)+
				      ((struct scatterlist *)
				       scmd->request_buffer)[0].offset;
		}
	}
#endif /* CRACK_MONKEY */
	if (!sges_left) {
		sdev_printk(KERN_ERR, scmd->device, "pci_map_sg"
		" failed: request for %d bytes!\n", scmd->request_bufflen);
		return -ENOMEM;
	}
#else
	sg_scmd = scsi_sglist(scmd);
	sges_left = scsi_dma_map(scmd);
	if (sges_left < 0) {
		sdev_printk(KERN_ERR, scmd->device, "scsi_dma_map"
		" failed: request for %d bytes!\n", scsi_bufflen(scmd));
		return -ENOMEM;
	}

#if defined(CRACK_MONKEY_EEDP)
	if (!ioc->disable_eedp_support) {
		if (scmd->cmnd[0] == INQUIRY)
			scmd->host_scribble = page_address(sg_page(sg_scmd)) +
				sg_scmd[0].offset;
	}
#endif /* CRACK_MONKEY */
#endif
	sgl_zero_addr = sg_local = &mpi_request->SGL;

	if (mpi_request->DMAFlags == MPI25_TA_DMAFLAGS_OP_D_H_D_D) {
		/* reserve last SGE for SGL1 */ 
		sges_in_request_frame = sges_in_segment =
		    ((ioc->request_sz - ioc->sge_size_ieee) -
		    offsetof(Mpi25SCSIIORequest_t, SGL))/ioc->sge_size_ieee;
	} else {
		sges_in_segment = (ioc->request_sz -
		   offsetof(Mpi25SCSIIORequest_t, SGL))/ioc->sge_size_ieee;
	}

	if (sges_left <= sges_in_segment)
		goto fill_in_last_segment;

	mpi_request->ChainOffset = (sges_in_segment - 1 /* chain element */) +
	    (offsetof(Mpi25SCSIIORequest_t, SGL)/ioc->sge_size_ieee);

	/* fill in main message segment when there is a chain following */
	while (sges_in_segment > 1) {
		_base_add_sg_single_ieee(sg_local, simple_sgl_flags, 0,
		    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
		sg_scmd++;
#else
		sg_scmd = sg_next(sg_scmd);
#endif
		sg_local += ioc->sge_size_ieee;
		sges_left--;
		sges_in_segment--;
	}

	/* initializing the pointers */
	chain_req = _base_get_chain_buffer_tracker(ioc, scmd);
	if (!chain_req)
		return -1;
	chain = chain_req->chain_buffer;
	chain_dma = chain_req->chain_buffer_dma;
	do {
		sges_in_segment = (sges_left <=
		    ioc->max_sges_in_chain_message) ? sges_left :
		    ioc->max_sges_in_chain_message;
		chain_offset = (sges_left == sges_in_segment) ?
		    0 : sges_in_segment;
		chain_length = sges_in_segment * ioc->sge_size_ieee;
		if (chain_offset)
			chain_length += ioc->sge_size_ieee;
		_base_add_sg_single_ieee(sg_local, chain_sgl_flags,
		    chain_offset, chain_length, chain_dma);

		sg_local = chain;
		if (!chain_offset)
			goto fill_in_last_segment;

		/* fill in chain segments */
		while (sges_in_segment) {
			_base_add_sg_single_ieee(sg_local, simple_sgl_flags, 0,
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
			sg_scmd++;
#else
			sg_scmd = sg_next(sg_scmd);
#endif
			sg_local += ioc->sge_size_ieee;
			sges_left--;
			sges_in_segment--;
		}

		chain_req = _base_get_chain_buffer_tracker(ioc, scmd);
		if (!chain_req)
			return -1;
		chain = chain_req->chain_buffer;
		chain_dma = chain_req->chain_buffer_dma;
	} while (1);


 fill_in_last_segment:

	/* fill the last segment */
	while (sges_left > 0) {
		if (sges_left == 1)
			_base_add_sg_single_ieee(sg_local,
			    simple_sgl_flags_last, 0, sg_dma_len(sg_scmd),
			    sg_dma_address(sg_scmd));
		else
			_base_add_sg_single_ieee(sg_local, simple_sgl_flags, 0,
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
		sg_scmd++;
#else
		sg_scmd = sg_next(sg_scmd);
#endif
		sg_local += ioc->sge_size_ieee;
		sges_left--;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27))
	if (mpi_request->DMAFlags == MPI25_TA_DMAFLAGS_OP_D_H_D_D)
		{
		mpi_request->SGLOffset1 = ((ioc->request_sz - ioc->sge_size_ieee)/4);
			sg_prot_scmd = scsi_prot_sglist(scmd);

			prot_sges_left = dma_map_sg(&ioc->pdev->dev, scsi_prot_sglist(scmd),
					scsi_prot_sg_count(scmd), scmd->sc_data_direction);
			if (!prot_sges_left) {
				sdev_printk(KERN_ERR, scmd->device, "pci_map_sg"
				" failed: request for %d bytes!\n", scsi_bufflen(scmd));
				scsi_dma_unmap(scmd);
				return -ENOMEM;
			}

			sg_prot_local = sgl_zero_addr +
					(sges_in_request_frame * ioc->sge_size_ieee);
			prot_sges_in_segment = 1; 

			if (prot_sges_left <= prot_sges_in_segment)
				goto fill_in_last_prot_segment;


			/* initializing the chain flags and pointers */
			chain_req = _base_get_chain_buffer_tracker(ioc, scmd);
			if (!chain_req)
				return -1;
			chain = chain_req->chain_buffer;
			chain_dma = chain_req->chain_buffer_dma;
			do {
				prot_sges_in_segment = (prot_sges_left <=
				    ioc->max_sges_in_chain_message) ? prot_sges_left :
				    ioc->max_sges_in_chain_message;
				chain_offset = (prot_sges_left == prot_sges_in_segment) ?
				    0 : prot_sges_in_segment;
				chain_length = prot_sges_in_segment * ioc->sge_size_ieee;
				if (chain_offset)
					chain_length += ioc->sge_size_ieee;
				_base_add_sg_single_ieee(sg_prot_local, chain_sgl_flags,
				    chain_offset, chain_length, chain_dma);

				sg_prot_local = chain;
				if (!chain_offset)
					goto fill_in_last_prot_segment;

				/* fill in chain segments */
				while (prot_sges_in_segment) {
					_base_add_sg_single_ieee(sg_prot_local, simple_sgl_flags, 0,
					    sg_dma_len(sg_prot_scmd), sg_dma_address(sg_prot_scmd));
					sg_prot_scmd = sg_next(sg_prot_scmd);
					sg_prot_local += ioc->sge_size_ieee;
					prot_sges_left--;
					prot_sges_in_segment--;
				}

				chain_req = _base_get_chain_buffer_tracker(ioc, scmd);
				if (!chain_req)
					return -1;
				chain = chain_req->chain_buffer;
				chain_dma = chain_req->chain_buffer_dma;
			} while (1);

		 fill_in_last_prot_segment:

			/* fill the last segment */
			while (prot_sges_left) {
				if (prot_sges_left == 1) {
					_base_add_sg_single_ieee(sg_prot_local,
					    simple_sgl_flags_last, 0, sg_dma_len(sg_prot_scmd),
					    sg_dma_address(sg_prot_scmd));
					}
				else {
					_base_add_sg_single_ieee(sg_prot_local, simple_sgl_flags, 0,
					    sg_dma_len(sg_prot_scmd), sg_dma_address(sg_prot_scmd));
					}
				sg_prot_scmd = sg_next(sg_prot_scmd);
				sg_prot_local += ioc->sge_size_ieee;
				prot_sges_left--;
			}
		}
#endif
	return 0;
}


/**
 * _base_build_sg_ieee - build generic sg for IEEE format
 * @ioc: per adapter object
 * @psge: virtual address for SGE
 * @data_out_dma: physical address for WRITES
 * @data_out_sz: data xfer size for WRITES
 * @data_in_dma: physical address for READS
 * @data_in_sz: data xfer size for READS
 *
 * Return nothing.
 */
static void
_base_build_sg_ieee(struct MPT3SAS_ADAPTER *ioc, void *psge,
	dma_addr_t data_out_dma, size_t data_out_sz, dma_addr_t data_in_dma,
	size_t data_in_sz)
{
	u8 sgl_flags;

	if (!data_out_sz && !data_in_sz) {
		_base_build_zero_len_sge_ieee(ioc, psge);
		return;
	}

	if (data_out_sz && data_in_sz) {
		/* WRITE sgel first */
		sgl_flags = MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR;
		_base_add_sg_single_ieee(psge, sgl_flags, 0, data_out_sz,
		    data_out_dma);

		/* incr sgel */
		psge += ioc->sge_size_ieee;

		/* READ sgel last */
		sgl_flags |= MPI25_IEEE_SGE_FLAGS_END_OF_LIST;
		_base_add_sg_single_ieee(psge, sgl_flags, 0, data_in_sz,
		    data_in_dma);
	} else if (data_out_sz) /* WRITE */ {
		sgl_flags = MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI25_IEEE_SGE_FLAGS_END_OF_LIST |
		    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR;
		_base_add_sg_single_ieee(psge, sgl_flags, 0, data_out_sz,
		    data_out_dma);
	} else if (data_in_sz) /* READ */ {
		sgl_flags = MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI25_IEEE_SGE_FLAGS_END_OF_LIST |
		    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR;
		_base_add_sg_single_ieee(psge, sgl_flags, 0, data_in_sz,
		    data_in_dma);
	}
}

#define convert_to_kb(x) ((x) << (PAGE_SHIFT - 10))

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,30))
/**
 * _base_config_dma_addressing - set dma addressing
 * @ioc: per adapter object
 * @pdev: PCI device struct
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_config_dma_addressing(struct MPT3SAS_ADAPTER *ioc, struct pci_dev *pdev)
{
	struct sysinfo s;
	char *desc = "64";
	u64 consistant_dma_mask = DMA_BIT_MASK(64); 
	
	if (ioc->is_mcpu_endpoint || ioc->use_32bit_dma)
		goto try_32bit:

	/* Set 63 bit DMA mask for all SAS3 and SAS35 controllers */
	if (ioc->hba_mpi_version_belonged > MPI2_VERSION) { 
		consistant_dma_mask = DMA_BIT_MASK(63); 
		desc = "63";
		ioc->dma_mask = 63;
	}

	if (sizeof(dma_addr_t) > 4) {
		uint64_t required_mask;

		/* have to first set mask to 64 to find max mask required */
		if (pci_set_dma_mask(pdev, consistant_dma_mask) != 0)
			goto try_32bit;

		required_mask = dma_get_required_mask(&pdev->dev);
		if (required_mask > DMA_32BIT_MASK &&
		    !pci_set_consistent_dma_mask(pdev, consistant_dma_mask)) {
			ioc->base_add_sg_single = &_base_add_sg_single_64;
			ioc->sge_size = sizeof(Mpi2SGESimple64_t);
			goto out;
		}
	}

 try_32bit:

	if (!pci_set_dma_mask(pdev, DMA_32BIT_MASK) &&
	    !pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK)) {
		ioc->base_add_sg_single = &_base_add_sg_single_32;
		ioc->sge_size = sizeof(Mpi2SGESimple32_t);
		desc = "32";
		ioc->dma_mask = 32;
	} else
		return -ENODEV;

 out:
	si_meminfo(&s);
	printk(MPT3SAS_INFO_FMT "%s BIT PCI BUS DMA ADDRESSING SUPPORTED, "
	    "total mem (%ld kB)\n", ioc->name, desc, convert_to_kb(s.totalram));

	return 0;
}
#else
/**
 * _base_config_dma_addressing - set dma addressing
 * @ioc: per adapter object
 * @pdev: PCI device struct
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_config_dma_addressing(struct MPT3SAS_ADAPTER *ioc, struct pci_dev *pdev)
{
	struct sysinfo s;
	char *desc = "64";
	u64 consistant_dma_mask = DMA_BIT_MASK(64); 

	if (ioc->is_mcpu_endpoint)
		goto try_32bit;

	/* Set 63 bit DMA mask for all SAS3 and SAS35 controllers */
	if (ioc->hba_mpi_version_belonged > MPI2_VERSION) { 
		consistant_dma_mask = DMA_BIT_MASK(63); 
		desc = "63";
	}

	if (sizeof(dma_addr_t) > 4) {
		const uint64_t required_mask =
		    dma_get_required_mask(&pdev->dev);
		if ((required_mask > DMA_BIT_MASK(32)) &&
		    !pci_set_dma_mask(pdev, consistant_dma_mask) &&
		    !pci_set_consistent_dma_mask(pdev, consistant_dma_mask)) {
			ioc->base_add_sg_single = &_base_add_sg_single_64;
			ioc->sge_size = sizeof(Mpi2SGESimple64_t);
			goto out;
		}
	}

try_32bit:
	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32))
	    && !pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32))) {
		ioc->base_add_sg_single = &_base_add_sg_single_32;
		ioc->sge_size = sizeof(Mpi2SGESimple32_t);
		desc = "32";
	} else
		return -ENODEV;

 out:
	si_meminfo(&s);
	printk(MPT3SAS_INFO_FMT "%s BIT PCI BUS DMA ADDRESSING SUPPORTED, "
	    "total mem (%ld kB)\n", ioc->name, desc, convert_to_kb(s.totalram));

	return 0;
}
#endif

/**
 * _base_check_and_get_msix_vectors - checks MSIX capabable.
 * @ioc: per adapter object
 *
 * Check to see if card is capable of MSIX, and return number
 * of avaliable msix vectors
 */
int
_base_check_and_get_msix_vectors(struct pci_dev *pdev)
{
	int base;
	u16 message_control, msix_vector_count;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	u8 revision;
#endif
	
	/* Check whether controller SAS2008 B0 controller,
	   if it is SAS2008 B0 controller use IO-APIC instead of MSIX */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	pci_read_config_byte(pdev, PCI_CLASS_REVISION, &revision);
	if (pdev->device == MPI2_MFGPAGE_DEVID_SAS2008 &&
		revision == SAS2_PCI_DEVICE_B0_REVISION) {
#else
	if (pdev->device == MPI2_MFGPAGE_DEVID_SAS2008 &&
	    pdev->revision == SAS2_PCI_DEVICE_B0_REVISION) {
#endif
		return -EINVAL;
	}

	base = pci_find_capability(pdev, PCI_CAP_ID_MSIX);
	if (!base)
		return -EINVAL;
	
	/* NUMA_IO not supported for older controllers */
	switch(pdev->device) {
		case MPI2_MFGPAGE_DEVID_SAS2004:
		case MPI2_MFGPAGE_DEVID_SAS2008:
		case MPI2_MFGPAGE_DEVID_SAS2108_1:
		case MPI2_MFGPAGE_DEVID_SAS2108_2:
		case MPI2_MFGPAGE_DEVID_SAS2108_3:
		case MPI2_MFGPAGE_DEVID_SAS2116_1:
		case MPI2_MFGPAGE_DEVID_SAS2116_2:
			return 1;
	}

	/* get msix vector count */
	pci_read_config_word(pdev, base + 2, &message_control);
	msix_vector_count = (message_control & 0x3FF) + 1;

	return msix_vector_count;
}

enum mpt3sas_pci_bus_speed {
	MPT_PCIE_SPEED_2_5GT		= 0x14,
	MPT_PCIE_SPEED_5_0GT		= 0x15,
	MPT_PCIE_SPEED_8_0GT		= 0x16,
	MPT_PCIE_SPEED_16_0GT		= 0x17,
	MPT_PCI_SPEED_UNKNOWN		= 0xff,
};

const unsigned char mpt3sas_pcie_link_speed[] = {
	MPT_PCI_SPEED_UNKNOWN,              /* 0 */
	MPT_PCIE_SPEED_2_5GT,               /* 1 */
	MPT_PCIE_SPEED_5_0GT,               /* 2 */
	MPT_PCIE_SPEED_8_0GT,               /* 3 */
	MPT_PCIE_SPEED_16_0GT,              /* 4 */
	MPT_PCI_SPEED_UNKNOWN,              /* 5 */
	MPT_PCI_SPEED_UNKNOWN,              /* 6 */
	MPT_PCI_SPEED_UNKNOWN,              /* 7 */
	MPT_PCI_SPEED_UNKNOWN,              /* 8 */
	MPT_PCI_SPEED_UNKNOWN,              /* 9 */
	MPT_PCI_SPEED_UNKNOWN,              /* A */
	MPT_PCI_SPEED_UNKNOWN,              /* B */
	MPT_PCI_SPEED_UNKNOWN,              /* C */
	MPT_PCI_SPEED_UNKNOWN,              /* D */
	MPT_PCI_SPEED_UNKNOWN,              /* E */
	MPT_PCI_SPEED_UNKNOWN               /* F */
};


/**
 * _base_check_and_enable_high_iops_queues - enable high iops mode
 * @ ioc - per adapter object
 * @ hba_msix_vector_count - msix vectors supported by HBA
 *
 * Enable high iops queues only if
 *  - HBA is a SEA/AERO controller and
 *  - MSI-Xs vector supported by the HBA is 128 and
 *  - total CPU count in the system >=16 and
 *  - loaded driver with default max_msix_vectors module parameter and
 *  - system booted in non kdump mode
 *
 * returns nothing.
 */
static void
_base_check_and_enable_high_iops_queues(struct MPT3SAS_ADAPTER *ioc,
    int hba_msix_vector_count)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	u16 lnksta;
	enum mpt3sas_pci_bus_speed speed;

	if (perf_mode == MPT_PERF_MODE_IOPS ||
	    perf_mode == MPT_PERF_MODE_LATENCY) {
		ioc->high_iops_queues = 0;
		return;
	}

	if (perf_mode == MPT_PERF_MODE_DEFAULT) {

		pcie_capability_read_word(ioc->pdev, PCI_EXP_LNKSTA, &lnksta);
		speed = mpt3sas_pcie_link_speed[lnksta & PCI_EXP_LNKSTA_CLS];

		dev_info(&ioc->pdev->dev, "PCIe device speed is %s\n",
		     speed == MPT_PCIE_SPEED_2_5GT ? "2.5GHz" :
		     speed == MPT_PCIE_SPEED_5_0GT ? "5.0GHz" :
		     speed == MPT_PCIE_SPEED_8_0GT ? "8.0GHz" :
		     speed == MPT_PCIE_SPEED_16_0GT ? "16.0GHz" :
		     "Unknown");

		if (speed < MPT_PCIE_SPEED_16_0GT) {
			ioc->high_iops_queues = 0;
			return;
		}
	}

	if (!reset_devices &&
	    hba_msix_vector_count == MPT3SAS_GEN35_MAX_MSIX_QUEUES &&
	    num_online_cpus() >= MPT3SAS_HIGH_IOPS_REPLY_QUEUES &&
	    max_msix_vectors == -1)
		ioc->high_iops_queues = MPT3SAS_HIGH_IOPS_REPLY_QUEUES;
	else
#endif
		ioc->high_iops_queues = 0;
}


/**
 * mpt3sas_base_disable_msix - disables msix
 * @ioc: per adapter object
 *
 */
void
mpt3sas_base_disable_msix(struct MPT3SAS_ADAPTER *ioc)
{
	if (!ioc->msix_enable)
		return;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0))
	pci_free_irq_vectors(ioc->pdev);
#else
	pci_disable_msix(ioc->pdev);
#endif
	ioc->msix_enable = 0;
}

/**
 * mpt3sas_base_free_irq - free irq
 * @ioc: per adapter object
 *
 * Freeing respective reply_queue from the list.
 */
void
mpt3sas_base_free_irq(struct MPT3SAS_ADAPTER *ioc)
{
	struct adapter_reply_queue *reply_q, *next;

	if (list_empty(&ioc->reply_queue_list))
		return;

	list_for_each_entry_safe(reply_q, next, &ioc->reply_queue_list, list) {
#if defined(MPT3SAS_ENABLE_IRQ_POLL)
		irq_poll_disable(&reply_q->irqpoll);
#endif
		list_del(&reply_q->list);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0))
		if (ioc->smp_affinity_enable)
			irq_set_affinity_hint(pci_irq_vector(ioc->pdev,
			    reply_q->msix_index), NULL);
		free_irq(pci_irq_vector(ioc->pdev, reply_q->msix_index),
		    reply_q);
#elif ((defined(RHEL_MAJOR) && (RHEL_MAJOR == 6)) || LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36))
		if (ioc->smp_affinity_enable) {
			irq_set_affinity_hint(reply_q->vector, NULL);
			free_cpumask_var(reply_q->affinity_hint);
		}
		free_irq(reply_q->vector, reply_q);
#else
		free_irq(reply_q->vector, reply_q);
#endif
		kfree(reply_q);
	}
}

/**
 * _base_request_irq - request irq
 * @ioc: per adapter object
 * @index: msix index into vector table
 * @vector: irq vector
 *
 * Inserting respective reply_queue into the list.
 */
static int
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0))
_base_request_irq(struct MPT3SAS_ADAPTER *ioc, u8 index)
#else
_base_request_irq(struct MPT3SAS_ADAPTER *ioc, u8 index, u32 vector)
#endif
{	
	struct adapter_reply_queue *reply_q;
	int r;

	reply_q =  kzalloc(sizeof(struct adapter_reply_queue), GFP_KERNEL);
	if (!reply_q) {
		printk(MPT3SAS_ERR_FMT "unable to allocate memory %d!\n",
		    ioc->name, (int)sizeof(struct adapter_reply_queue));
		return -ENOMEM;
	}
	reply_q->ioc = ioc;
	reply_q->msix_index = index;
#if ((defined(RHEL_MAJOR) && (RHEL_MAJOR == 6)) || \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36) && \
     LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0)))
	if (ioc->smp_affinity_enable) {	
		if (!alloc_cpumask_var(&reply_q->affinity_hint, GFP_KERNEL)) {
			kfree(reply_q);
			return -ENOMEM;
		}
		cpumask_clear(reply_q->affinity_hint);
	}
#endif
	atomic_set(&reply_q->busy, 0);
	if (ioc->msix_enable)
		snprintf(reply_q->name, MPT_NAME_LENGTH, "%s%d-msix%d",
		    ioc->driver_name, ioc->id, index);
	else
		snprintf(reply_q->name, MPT_NAME_LENGTH, "%s%d",
		    ioc->driver_name, ioc->id);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0))
	r = request_irq(pci_irq_vector(ioc->pdev, index), _base_interrupt,
	    IRQF_SHARED, reply_q->name, reply_q);
	if (r) {
		printk(MPT3SAS_ERR_FMT "unable to allocate interrupt %d!\n",
		    reply_q->name, pci_irq_vector(ioc->pdev, index));
		kfree(reply_q);
		return -EBUSY;
	}
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
	reply_q->vector = vector;
	r = request_irq(vector, _base_interrupt, IRQF_SHARED, reply_q->name,
	    reply_q);
	if (r) {
		printk(MPT3SAS_ERR_FMT "unable to allocate interrupt %d!\n",
		    reply_q->name, vector);
		kfree(reply_q);
		return -EBUSY;
	}
#else
	reply_q->vector = vector;
	r = request_irq(vector, _base_interrupt, SA_SHIRQ, reply_q->name,
	    reply_q);
	if (r) {
		printk(MPT3SAS_ERR_FMT "unable to allocate interrupt %d!\n",
		    reply_q->name, vector);
		kfree(reply_q);
		return -EBUSY;
	}
#endif

	INIT_LIST_HEAD(&reply_q->list);
	list_add_tail(&reply_q->list, &ioc->reply_queue_list);
	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0))
/**
 * _base_alloc_irq_vectors - allocate msix vectors
 * @ioc: per adapter object
 *
 */
static int
_base_alloc_irq_vectors(struct MPT3SAS_ADAPTER *ioc)
{
	int i, irq_flags = PCI_IRQ_MSIX;
	struct irq_affinity desc = { .pre_vectors = ioc->high_iops_queues };
	struct irq_affinity *descp = &desc;

	if (ioc->smp_affinity_enable)
		irq_flags |= PCI_IRQ_AFFINITY;
	else
		descp = NULL;

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT
	    "high_iops_queues: %d, reply_queue_count: %d\n",
	    ioc->name, ioc->high_iops_queues,
	    ioc->reply_queue_count));

	i = pci_alloc_irq_vectors_affinity(ioc->pdev,
	    ioc->high_iops_queues,
	    ioc->reply_queue_count, irq_flags, descp);

	return i;
}
#endif

/**
 * _base_enable_msix - enables msix, failback to io_apic
 * @ioc: per adapter object
 *
 */
static int
_base_enable_msix(struct MPT3SAS_ADAPTER *ioc)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0))
	struct msix_entry *entries, *a;
#endif
	int r, i, msix_vector_count, local_max_msix_vectors;
	u8 try_msix = 0;

	ioc->msix_load_balance = false;

	if (msix_disable == -1 || msix_disable == 0)
		try_msix = 1;

	if (!try_msix)
		goto try_ioapic;

	msix_vector_count = _base_check_and_get_msix_vectors(ioc->pdev);
	if (msix_vector_count <= 0) {
		dfailprintk(ioc, printk(MPT3SAS_INFO_FMT "msix not "
		    "supported\n", ioc->name));
		goto try_ioapic;
	}
	
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT
	    "MSI-X vectors supported: %d, no of cores: %d\n",
	    ioc->name, msix_vector_count, ioc->cpu_count));

	if (ioc->is_aero_ioc)
		_base_check_and_enable_high_iops_queues(ioc, msix_vector_count);

	ioc->reply_queue_count = min_t(int,
	    ioc->cpu_count + ioc->high_iops_queues,
	    msix_vector_count);

	if (!ioc->rdpq_array_enable && max_msix_vectors == -1) 
	{
		if(reset_devices)
			local_max_msix_vectors = 1;
		else
			local_max_msix_vectors = 8;
	}else
		local_max_msix_vectors = max_msix_vectors;


	if (local_max_msix_vectors > 0) {
		ioc->reply_queue_count = min_t(int, local_max_msix_vectors,
		    ioc->reply_queue_count);
	} else if (local_max_msix_vectors == 0)
		goto try_ioapic;

	/*
	 * Enable msix_load_balance only if combined reply queue mode is
	 * disabled on SAS3 & above generation HBA devices.
	 */
	if (!ioc->combined_reply_queue &&
	    ioc->hba_mpi_version_belonged != MPI2_VERSION) {
		printk(MPT3SAS_INFO_FMT
		    "combined reply queue is off, so enabling msix load balance\n",
		    ioc->name);
		ioc->msix_load_balance = true;
	}

	/*
	 * smp affinity setting is not need when msix load balance
	 * is enabled.
	 */
	if (ioc->msix_load_balance)
		ioc->smp_affinity_enable = 0;


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0))
	r = _base_alloc_irq_vectors(ioc);
	if (r < 0) {
		printk(MPT3SAS_WARN_FMT
		    "pci_alloc_irq_vectors failed (r=%d) !!!\n",
		    ioc->name, r);
		goto try_ioapic;
	}

	ioc->msix_enable = 1;
	for (i = 0; i < ioc->reply_queue_count; i++) {
		r = _base_request_irq(ioc, i);
		if (r) {
			mpt3sas_base_free_irq(ioc);
			mpt3sas_base_disable_msix(ioc);
			goto try_ioapic;
		}
	}
#else
	entries = kcalloc(ioc->reply_queue_count, sizeof(struct msix_entry),
	    GFP_KERNEL);
	if (!entries) {
		printk(MPT3SAS_WARN_FMT "kcalloc "
		    "failed @ at %s:%d/%s() !!!\n", ioc->name, __FILE__,
		    __LINE__, __func__);
		goto try_ioapic;
	}
	for (i = 0, a = entries; i < ioc->reply_queue_count; i++, a++)
		a->entry = i;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0))
	r = pci_enable_msix_exact(ioc->pdev, entries, ioc->reply_queue_count);
#else
	r = pci_enable_msix(ioc->pdev, entries, ioc->reply_queue_count);
#endif	

	if (r) {
		dfailprintk(ioc, printk(MPT3SAS_INFO_FMT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0))
		"pci_enable_msix_exact "
#else
		"pci_enable_msix "
#endif		
		"failed (r=%d) !!!\n", ioc->name, r));
		kfree(entries);
		goto try_ioapic;
	}

	ioc->msix_enable = 1;
	for (i = 0, a = entries; i < ioc->reply_queue_count; i++, a++) {
		r = _base_request_irq(ioc, i, a->vector);
		if (r) {
			mpt3sas_base_free_irq(ioc);
			mpt3sas_base_disable_msix(ioc);
			kfree(entries);
			goto try_ioapic;
		}
	}

	kfree(entries);
#endif

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "High IOPs queues : %s \n",
	    ioc->name, ioc->high_iops_queues ? "enabled" : "disabled"));

	return 0;

/* failback to io_apic interrupt routing */
 try_ioapic:
	ioc->high_iops_queues = 0;
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT
	    "High IOPs queues : disabled \n", ioc->name));
	ioc->reply_queue_count = 1;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0))
	r = _base_request_irq(ioc, 0);
#else
	r = _base_request_irq(ioc, 0, ioc->pdev->irq);
#endif
	return r;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0))
/**
 * _base_import_managed_irqs_affinity - import msix affinity of managed IRQs
 *     into local cpu mapping table.
 * @ioc - per adapter object 
 * 
 * Return nothing.
 */
static void
_base_import_managed_irqs_affinity(struct MPT3SAS_ADAPTER *ioc)
{
	struct adapter_reply_queue *reply_q;
	unsigned int cpu, nr_msix;
	int local_numa_node;
	unsigned int index = 0;

	nr_msix = ioc->reply_queue_count;
	if (!nr_msix)
		return;

	if (ioc->smp_affinity_enable) {

		/*
		 * set irq affinity to local numa node for those irqs
		 * corresponding to high iops queues.
		 */
		if (ioc->high_iops_queues) {
			local_numa_node = dev_to_node(&ioc->pdev->dev);
			for (index = 0; index < ioc->high_iops_queues;
			    index++) {
				irq_set_affinity_hint(pci_irq_vector(ioc->pdev,
				    index), cpumask_of_node(local_numa_node));
			}
		}

		list_for_each_entry(reply_q, &ioc->reply_queue_list, list) {
			const cpumask_t *mask;

			if (reply_q->msix_index < ioc->high_iops_queues)
				continue;

			mask = pci_irq_get_affinity(ioc->pdev,
			    reply_q->msix_index);
			if (!mask) {
				dinitprintk(ioc, printk(MPT3SAS_WARN_FMT
				    "no affinity for msi %x\n", ioc->name,
				    reply_q->msix_index));
				goto fall_back;
			}

			for_each_cpu_and(cpu, mask, cpu_online_mask) {
				if (cpu >= ioc->cpu_msix_table_sz)
					break;
				ioc->cpu_msix_table[cpu] = reply_q->msix_index;
			}
		}
		return;
	}

fall_back:
	_base_group_cpus_on_irq(ioc);
}
#endif

#if ((defined(RHEL_MAJOR) && (RHEL_MAJOR == 6)) || \
    ((LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)) && \
    (LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0))))

/**
 * _base_distribute_msix_vectors_explicity - distribute allocated msix vectors
 *     among the numa node and then distribute the per node allocated vectors
 *      among the CPUs exist in that node.
 * @ioc - per adapter object 
 *
 * Return nothing.
 */
static void
_base_distribute_msix_vectors_explicity(struct MPT3SAS_ADAPTER *ioc)
{
	struct adapter_reply_queue *reply_q;
	unsigned int cpu, nr_msix;
	unsigned long nr_nodes;
	int node_index=0; 

	/* First distribute the msix vectors among the numa nodes,
	 * then distribute the per node allocated vectors among the
	 * CPUs exist in that node. So that same MSIX vector is not
	 * assigned to the CPUs across the numa nodes.
	 *
	 * Only general reply queues are used for determine the affinity hint,
	 * high iops queues are excluded, so these high iops msix vectors are
	 * affinity to local numa node.
	 */
	nr_nodes = num_online_nodes();
	nr_msix = ioc->reply_queue_count - ioc->high_iops_queues;
	reply_q = list_entry(ioc->reply_queue_list.next,
	    struct adapter_reply_queue, list);
	while (reply_q && reply_q->msix_index < ioc->high_iops_queues)
		reply_q = list_entry(reply_q->list.next,
		     struct adapter_reply_queue, list);
	
	for (node_index=0; node_index < nr_nodes; node_index++) {
		unsigned int group, nr_msix_group;
		int cpu_count = 0, cpu_group_count = 0;
		int nr_cpus_per_node =  cpumask_weight(cpumask_of_node(node_index));
		nr_msix_group = nr_msix / nr_nodes;
		
		if (node_index < nr_msix % nr_nodes)
			nr_msix_group++;

		if (!nr_msix_group)
			continue;

		for_each_cpu(cpu, cpumask_of_node(node_index)) {

			if(!reply_q)
				return;

			group = nr_cpus_per_node / nr_msix_group;

			if (cpu_count < ((nr_cpus_per_node % nr_msix_group) * (group + 1)))
				group++;

			ioc->cpu_msix_table[cpu] = reply_q->msix_index;
			if (ioc->smp_affinity_enable)
			    cpumask_or(reply_q->affinity_hint,
				reply_q->affinity_hint, get_cpu_mask(cpu));
			
			cpu_count++;
			cpu_group_count++;
			if (cpu_group_count < group)
				continue;

			if (ioc->smp_affinity_enable) {
			    if (irq_set_affinity_hint(reply_q->vector,
					reply_q->affinity_hint))
				dinitprintk(ioc, printk(MPT3SAS_FMT
				    "error setting affinity hint for irq "
				    " vector %d\n", ioc->name, reply_q->vector));
			}
			reply_q = list_entry(reply_q->list.next,
                                    struct adapter_reply_queue, list);

			while (reply_q &&
			    reply_q->msix_index < ioc->high_iops_queues)
				reply_q = list_entry(reply_q->list.next,
				     struct adapter_reply_queue, list);

			cpu_group_count = 0;
		}
	}
}
#endif

/**
 * _base_assign_reply_queues - assigning msix index for each cpu
 * @ioc: per adapter object
 *
 * The enduser would need to set the affinity via /proc/irq/#/smp_affinity
 *
 * It would nice if we could call irq_set_affinity, however it is not
 * an exported symbol
 */
static void
_base_assign_reply_queues(struct MPT3SAS_ADAPTER *ioc)
{
	struct adapter_reply_queue *reply_q;
	int reply_queue;

	if (!_base_is_controller_msix_enabled(ioc))
		return;

	if (ioc->msix_load_balance)
		return;
	
	memset(ioc->cpu_msix_table, 0, ioc->cpu_msix_table_sz);

	/* NUMA Hardware bug workaround - drop to less reply queues */
	if (ioc->reply_queue_count > ioc->facts.MaxMSIxVectors) {
		ioc->reply_queue_count = ioc->facts.MaxMSIxVectors;
		reply_queue = 0;
		list_for_each_entry(reply_q, &ioc->reply_queue_list, list) {
			reply_q->msix_index = reply_queue;
			if (++reply_queue == ioc->reply_queue_count)
				reply_queue = 0;
		}
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0))

	_base_import_managed_irqs_affinity(ioc);
	
#elif ((defined(RHEL_MAJOR) && (RHEL_MAJOR == 6)) || ((LINUX_VERSION_CODE >= \
       KERNEL_VERSION(2,6,36))))

	_base_distribute_msix_vectors_explicity(ioc);

#else
	/* when there are more cpus than available msix vectors,
	 * then group cpus togeather on same irq
	 */

	_base_group_cpus_on_irq(ioc);	
#endif
}

/**
 * _base_wait_for_doorbell_int - waiting for controller interrupt(generated by
 * a write to the doorbell)
 * @ioc: per adapter object
 * @timeout: timeout in second
 *
 * Returns 0 for success, non-zero for failure.
 *
 * Notes: MPI2_HIS_IOC2SYS_DB_STATUS - set to one when IOC writes to doorbell.
 */
static int
_base_wait_for_doorbell_int(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	u32 cntdn, count;
	u32 int_status;

	count = 0;
	cntdn = 1000*timeout;
	do {
		int_status = ioc->base_readl(&ioc->chip->HostInterruptStatus);
		if (int_status & MPI2_HIS_IOC2SYS_DB_STATUS) {
			dhsprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: "
			    "successfull count(%d), timeout(%d)\n", ioc->name,
			    __func__, count, timeout));
			return 0;
		}
		msleep(1);
		count++;
	} while (--cntdn);

	printk(MPT3SAS_ERR_FMT "%s: failed due to timeout count(%d), "
	    "int_status(%x)!\n", ioc->name, __func__, count, int_status);
	return -EFAULT;
}

/**
 * _base_spin_on_doorbell_int - waiting for controller interrupt(generated by
 * a write to the doorbell)
 * @ioc: per adapter object
 * @timeout: timeout in second
 *
 * Returns 0 for success, non-zero for failure.
 *
 * Notes: MPI2_HIS_IOC2SYS_DB_STATUS - set to one when IOC writes to doorbell.
 */
static int
_base_spin_on_doorbell_int(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	u32 cntdn, count;
	u32 int_status;

	count = 0;
	cntdn = 2000 * timeout;
	do {
		int_status = ioc->base_readl(&ioc->chip->HostInterruptStatus);
		if (int_status & MPI2_HIS_IOC2SYS_DB_STATUS) {
			dhsprintk(ioc, pr_info(MPT3SAS_FMT
				"%s: successful count(%d), timeout(%d)\n",
				ioc->name, __func__, count, timeout));
			return 0;
		}
		udelay(500);
		count++;
	} while (--cntdn);

	printk(MPT3SAS_ERR_FMT "%s: failed due to timeout count(%d), "
	    "int_status(%x)!\n", ioc->name, __func__, count, int_status);
	return -EFAULT;
}

/**
 * _base_wait_for_doorbell_ack - waiting for controller to read the doorbell.
 * @ioc: per adapter object
 * @timeout: timeout in second
 *
 * Returns 0 for success, non-zero for failure.
 *
 * Notes: MPI2_HIS_SYS2IOC_DB_STATUS - set to one when host writes to
 * doorbell.
 */
static int
_base_wait_for_doorbell_ack(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	u32 cntdn, count;
	u32 int_status;
	u32 doorbell;

	count = 0;
	cntdn = 1000*timeout;
	do {
		int_status = ioc->base_readl(&ioc->chip->HostInterruptStatus);
		if (!(int_status & MPI2_HIS_SYS2IOC_DB_STATUS)) {
			dhsprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: "
			    "successfull count(%d), timeout(%d)\n", ioc->name,
			    __func__, count, timeout));
			return 0;
		} else if (int_status & MPI2_HIS_IOC2SYS_DB_STATUS) {
			doorbell = ioc->base_readl(&ioc->chip->Doorbell);
			if ((doorbell & MPI2_IOC_STATE_MASK) ==
			    MPI2_IOC_STATE_FAULT) {
				mpt3sas_print_fault_code(ioc , doorbell);
				return -EFAULT;
			}
			if ((doorbell & MPI2_IOC_STATE_MASK) ==
				MPI2_IOC_STATE_COREDUMP) {
				mpt3sas_base_coredump_info(ioc , doorbell);
				return -EFAULT;
			}
		} else if (int_status == 0xFFFFFFFF)
			goto out;

		msleep(1);
		count++;
	} while (--cntdn);

 out:
	printk(MPT3SAS_ERR_FMT "%s: failed due to timeout count(%d), "
	    "int_status(%x)!\n", ioc->name, __func__, count, int_status);
	return -EFAULT;
}

/**
 * _base_wait_for_doorbell_not_used - waiting for doorbell to not be in use
 * @ioc: per adapter object
 * @timeout: timeout in second
 *
 * Returns 0 for success, non-zero for failure.
 *
 */
static int
_base_wait_for_doorbell_not_used(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	u32 cntdn, count;
	u32 doorbell_reg;

	count = 0;
	cntdn = 1000*timeout;
	do {
		doorbell_reg = ioc->base_readl(&ioc->chip->Doorbell);
		if (!(doorbell_reg & MPI2_DOORBELL_USED)) {
			dhsprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: "
			    "successfull count(%d), timeout(%d)\n", ioc->name,
			    __func__, count, timeout));
			return 0;
		}
		msleep(1);
		count++;
	} while (--cntdn);

	printk(MPT3SAS_ERR_FMT "%s: failed due to timeout count(%d), "
	    "doorbell_reg(%x)!\n", ioc->name, __func__, count, doorbell_reg);
	return -EFAULT;
}

/**
 * _base_handshake_req_reply_wait - send request thru doorbell interface
 * @ioc: per adapter object
 * @request_bytes: request length
 * @request: pointer having request payload
 * @reply_bytes: reply length
 * @reply: pointer to reply payload
 * @timeout: timeout in second
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_handshake_req_reply_wait(struct MPT3SAS_ADAPTER *ioc, int request_bytes,
	u32 *request, int reply_bytes, u16 *reply, int timeout)
{
	MPI2DefaultReply_t *default_reply = (MPI2DefaultReply_t *)reply;
	int i;
	u8 failed;
	__le32 *mfp;

	/* make sure doorbell is not in use */
	if ((ioc->base_readl(&ioc->chip->Doorbell) & MPI2_DOORBELL_USED)) {
		printk(MPT3SAS_ERR_FMT "doorbell is in use "
		    " (line=%d)\n", ioc->name, __LINE__);
		return -EFAULT;
	}

	/* clear pending doorbell interrupts from previous state changes */
	if (ioc->base_readl(&ioc->chip->HostInterruptStatus) &
	    MPI2_HIS_IOC2SYS_DB_STATUS)
		writel(0, &ioc->chip->HostInterruptStatus);

	/* send message to ioc */
	writel(((MPI2_FUNCTION_HANDSHAKE<<MPI2_DOORBELL_FUNCTION_SHIFT) |
	    ((request_bytes/4)<<MPI2_DOORBELL_ADD_DWORDS_SHIFT)),
	    &ioc->chip->Doorbell);

	if ((_base_spin_on_doorbell_int(ioc, 5))) {
		printk(MPT3SAS_ERR_FMT "doorbell handshake "
		   "int failed (line=%d)\n", ioc->name, __LINE__);
		return -EFAULT;
	}
	writel(0, &ioc->chip->HostInterruptStatus);

	if ((_base_wait_for_doorbell_ack(ioc, 5))) {
		printk(MPT3SAS_ERR_FMT "doorbell handshake "
		    "ack failed (line=%d)\n", ioc->name, __LINE__);
		return -EFAULT;
	}

	/* send message 32-bits at a time */
	for (i = 0, failed = 0; i < request_bytes/4 && !failed; i++) {
		writel((u32)(request[i]), &ioc->chip->Doorbell);
		if ((_base_wait_for_doorbell_ack(ioc, 5)))
			failed = 1;
	}

	if (failed) {
		printk(MPT3SAS_ERR_FMT "doorbell handshake "
		    "sending request failed (line=%d)\n", ioc->name, __LINE__);
		return -EFAULT;
	}

	/* now wait for the reply */
	if ((_base_wait_for_doorbell_int(ioc, timeout))) {
		printk(MPT3SAS_ERR_FMT "doorbell handshake "
		   "int failed (line=%d)\n", ioc->name, __LINE__);
		return -EFAULT;
	}

	/* read the first two 16-bits, it gives the total length of the reply */
	reply[0] = (u16)(ioc->base_readl(&ioc->chip->Doorbell)
	    & MPI2_DOORBELL_DATA_MASK);
	writel(0, &ioc->chip->HostInterruptStatus);
	if ((_base_wait_for_doorbell_int(ioc, 5))) {
		printk(MPT3SAS_ERR_FMT "doorbell handshake "
		   "int failed (line=%d)\n", ioc->name, __LINE__);
		return -EFAULT;
	}
	reply[1] = (u16)(ioc->base_readl(&ioc->chip->Doorbell)
	    & MPI2_DOORBELL_DATA_MASK);
	writel(0, &ioc->chip->HostInterruptStatus);

	for (i = 2; i < default_reply->MsgLength * 2; i++)  {
		if ((_base_wait_for_doorbell_int(ioc, 5))) {
			printk(MPT3SAS_ERR_FMT "doorbell "
			    "handshake int failed (line=%d)\n", ioc->name,
			    __LINE__);
			return -EFAULT;
		}
		if (i >=  reply_bytes/2) /* overflow case */
			ioc->base_readl(&ioc->chip->Doorbell);
		else
			reply[i] = (u16)(ioc->base_readl(&ioc->chip->Doorbell)
			    & MPI2_DOORBELL_DATA_MASK);
		writel(0, &ioc->chip->HostInterruptStatus);
	}

	if (_base_wait_for_doorbell_int(ioc, 5)) {
		printk(MPT3SAS_ERR_FMT "doorbell handshake "
		   "int failed (line=%d)\n", ioc->name, __LINE__);
		return -EFAULT;
	}
	if (_base_wait_for_doorbell_not_used(ioc, 5) != 0) {
		dhsprintk(ioc, printk(MPT3SAS_INFO_FMT "doorbell is in use "
		    " (line=%d)\n", ioc->name, __LINE__));
	}
	writel(0, &ioc->chip->HostInterruptStatus);

	if (ioc->logging_level & MPT_DEBUG_INIT) {
		mfp = (__le32 *)reply;
		printk(MPT3SAS_INFO_FMT "\toffset:data\n", ioc->name);
		for (i = 0; i < reply_bytes/4; i++)
			printk(MPT3SAS_INFO_FMT "\t[0x%02x]:%08x\n", ioc->name,
			    i*4, le32_to_cpu(mfp[i]));
	}
	return 0;
}

/**
 * _base_wait_on_iocstate - waiting on a particular ioc state
 * @ioc_state: controller state { READY, OPERATIONAL, or RESET }
 * @timeout: timeout in second
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_wait_on_iocstate(struct MPT3SAS_ADAPTER *ioc, u32 ioc_state, int timeout)
{
	u32 count, cntdn;
	u32 current_state;

	count = 0;
	cntdn = 1000*timeout;
	do {
		current_state = mpt3sas_base_get_iocstate(ioc, 1);
		if (current_state == ioc_state)
			return 0;
		if (count && current_state == MPI2_IOC_STATE_FAULT)
			break;
		msleep(1);
		count++;
	} while (--cntdn);

	return current_state;
}

/**
 * _base_dump_reg_set -	This function will print hexdump of register set
 * @ioc: per adapter object
 *
 * Returns nothing.
 */
static inline void
_base_dump_reg_set(struct MPT3SAS_ADAPTER *ioc)
{
	unsigned int i, sz = 256;
	u32 __iomem *reg = (u32 __iomem *)ioc->chip;

	printk(MPT3SAS_FMT "System Register set:\n", ioc->name); 
	for (i = 0; i < (sz / sizeof(u32)); i++)
		printk("%08x: %08x\n", (i * 4), readl(&reg[i]));
}

/**
 * _base_diag_reset - the "big hammer" start of day reset
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_diag_reset(struct MPT3SAS_ADAPTER *ioc)
{
	u32 host_diagnostic;
	u32 ioc_state;
	u32 count;
	u32 hcb_size;

	printk(MPT3SAS_INFO_FMT "sending diag reset !!\n", ioc->name);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
	drsprintk(ioc, printk(MPT3SAS_INFO_FMT "Locking pci cfg space access\n",
	    ioc->name));
	pci_cfg_access_lock(ioc->pdev);
#endif
	drsprintk(ioc, printk(MPT3SAS_INFO_FMT "clear interrupts\n",
	    ioc->name));

	count = 0;
	do {
		/* Write magic sequence to WriteSequence register
		 * Loop until in diagnostic mode
		 */
		drsprintk(ioc, printk(MPT3SAS_INFO_FMT "write magic "
		    "sequence\n", ioc->name));
		writel(MPI2_WRSEQ_FLUSH_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_1ST_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_2ND_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_3RD_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_4TH_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_5TH_KEY_VALUE, &ioc->chip->WriteSequence);
		writel(MPI2_WRSEQ_6TH_KEY_VALUE, &ioc->chip->WriteSequence);

		/* wait 100 msec */
		msleep(100);

		if (count++ > 20) {
			printk(MPT3SAS_ERR_FMT
			    "Giving up writing magic sequence after 20 retries\n",
			    ioc->name);
			_base_dump_reg_set(ioc);
			goto out;
		}

		host_diagnostic = ioc->base_readl(&ioc->chip->HostDiagnostic);
		drsprintk(ioc, printk(MPT3SAS_INFO_FMT "wrote magic "
		    "sequence: count(%d), host_diagnostic(0x%08x)\n",
		    ioc->name, count, host_diagnostic));

	} while ((host_diagnostic & MPI2_DIAG_DIAG_WRITE_ENABLE) == 0);

	hcb_size = ioc->base_readl(&ioc->chip->HCBSize);

	drsprintk(ioc, printk(MPT3SAS_INFO_FMT "diag reset: issued\n",
	    ioc->name));
	writel(host_diagnostic | MPI2_DIAG_RESET_ADAPTER,
	     &ioc->chip->HostDiagnostic);

#if defined(DISABLE_RESET_SUPPORT)
	count = 0;
	do {
		/* wait 50 msec per loop */
		msleep(50);

		host_diagnostic = ioc->base_readl(&ioc->chip->HostDiagnostic);
		if (host_diagnostic == 0xFFFFFFFF)
			goto out;
		else if (count++ >= 300) /* wait for upto 15 seconds */
			goto out;
		if (!(count % 20))
			printk(KERN_INFO "waiting on diag reset bit to clear, "
			    "count = %d\n", (count / 20));
	} while (host_diagnostic & MPI2_DIAG_RESET_ADAPTER);
#else
	/* This delay allows the chip PCIe hardware time to finish reset tasks */
		msleep(MPI2_HARD_RESET_PCIE_FIRST_READ_DELAY_MICRO_SEC/1000);

	/* Approximately 300 second max wait */
	for (count = 0; count < (300000000 /
	    MPI2_HARD_RESET_PCIE_SECOND_READ_DELAY_MICRO_SEC); count++) {

		host_diagnostic = ioc->base_readl(&ioc->chip->HostDiagnostic);

		if (host_diagnostic == 0xFFFFFFFF) {
			printk(MPT3SAS_ERR_FMT
			    "Invalid host diagnostic register value\n",
			    ioc->name);
			_base_dump_reg_set(ioc);
			goto out;
		}
		if (!(host_diagnostic & MPI2_DIAG_RESET_ADAPTER))
			break;

		/* Wait to pass the second read delay window */
			msleep(MPI2_HARD_RESET_PCIE_SECOND_READ_DELAY_MICRO_SEC/1000);
	}
#endif

	if (host_diagnostic & MPI2_DIAG_HCB_MODE) {

		drsprintk(ioc, printk(MPT3SAS_INFO_FMT "restart the adapter "
		    "assuming the HCB Address points to good F/W\n",
		    ioc->name));
		host_diagnostic &= ~MPI2_DIAG_BOOT_DEVICE_SELECT_MASK;
		host_diagnostic |= MPI2_DIAG_BOOT_DEVICE_SELECT_HCDW;
		writel(host_diagnostic, &ioc->chip->HostDiagnostic);

		drsprintk(ioc, printk(MPT3SAS_INFO_FMT
		    "re-enable the HCDW\n", ioc->name));
		writel(hcb_size | MPI2_HCB_SIZE_HCB_ENABLE,
		    &ioc->chip->HCBSize);
	}

	drsprintk(ioc, printk(MPT3SAS_INFO_FMT "restart the adapter\n",
	    ioc->name));
	writel(host_diagnostic & ~MPI2_DIAG_HOLD_IOC_RESET,
	    &ioc->chip->HostDiagnostic);

	drsprintk(ioc, printk(MPT3SAS_INFO_FMT "disable writes to the "
	    "diagnostic register\n", ioc->name));
	writel(MPI2_WRSEQ_FLUSH_KEY_VALUE, &ioc->chip->WriteSequence);

	drsprintk(ioc, printk(MPT3SAS_INFO_FMT "Wait for FW to go to the "
	    "READY state\n", ioc->name));
	ioc_state = _base_wait_on_iocstate(ioc, MPI2_IOC_STATE_READY, 20);
	if (ioc_state) {
		printk(MPT3SAS_ERR_FMT "%s: failed going to ready state "
		    " (ioc_state=0x%x)\n", ioc->name, __func__, ioc_state);
		_base_dump_reg_set(ioc);
		goto out;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
	drsprintk(ioc, printk(MPT3SAS_INFO_FMT
	    "Unlocking pci cfg space access\n", ioc->name));
	pci_cfg_access_unlock(ioc->pdev);
#endif

	printk(MPT3SAS_INFO_FMT "diag reset: SUCCESS\n", ioc->name);
	return 0;

 out:
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
	drsprintk(ioc, printk(MPT3SAS_INFO_FMT
	    "Unlocking pci cfg space access\n", ioc->name));
	pci_cfg_access_unlock(ioc->pdev);
#endif

	printk(MPT3SAS_ERR_FMT "diag reset: FAILED\n", ioc->name);
	return -EFAULT;
}

/**
 * _base_wait_for_iocstate - Wait until the card is in READY or OPERATIONAL
 * @ioc: per adapter object
 * @timeout:
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_wait_for_iocstate(struct MPT3SAS_ADAPTER *ioc, int timeout)
{
	u32 ioc_state;
	int rc;

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	if (!mpt3sas_base_pci_device_is_available(ioc))
		return 0;

	ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
	dhsprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: ioc_state(0x%08x)\n",
	    ioc->name, __func__, ioc_state));

	if (((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_READY) ||
	    (ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_OPERATIONAL)
		return 0;

	if (ioc_state & MPI2_DOORBELL_USED) {
		dhsprintk(ioc, printk(MPT3SAS_INFO_FMT "unexpected doorbell "
		    "active!\n", ioc->name));
		goto issue_diag_reset;
	}

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
		mpt3sas_print_fault_code(ioc, ioc_state &
		    MPI2_DOORBELL_DATA_MASK);
		goto issue_diag_reset;
	}
	else if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_COREDUMP) {
		printk(MPT3SAS_ERR_FMT "%s: Skipping the diag reset here. "
		    " (ioc_state=0x%x)\n", ioc->name, __func__, ioc_state);
		return -EFAULT;
	}

	ioc_state = _base_wait_on_iocstate(ioc, MPI2_IOC_STATE_READY,
	    timeout);
	if (ioc_state) {
		printk(MPT3SAS_ERR_FMT "%s: failed going to ready state "
		    " (ioc_state=0x%x)\n", ioc->name, __func__, ioc_state);
		return -EFAULT;
	}

 issue_diag_reset:
	rc = _base_diag_reset(ioc);
	return rc;
}

/**
 * _base_check_for_fault_and_issue_reset - check if IOC is in fault state
 *     and if it is in fault state then issue diag reset.
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_check_for_fault_and_issue_reset(struct MPT3SAS_ADAPTER *ioc)
{
	u32 ioc_state;
	int rc = -EFAULT;

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	if (!mpt3sas_base_pci_device_is_available(ioc))
		return rc;

	ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
	dhsprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: ioc_state(0x%08x)\n",
	    ioc->name, __func__, ioc_state));

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
		mpt3sas_print_fault_code(ioc, ioc_state &
		    MPI2_DOORBELL_DATA_MASK);
		rc = _base_diag_reset(ioc);
	}
	else if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_COREDUMP) {
		mpt3sas_base_coredump_info(ioc, ioc_state &
			MPI2_DOORBELL_DATA_MASK);
		mpt3sas_base_wait_for_coredump_completion(ioc, __func__);
		rc = _base_diag_reset(ioc);
	}

	return rc;
}

/**
 * _base_get_ioc_facts - obtain ioc facts reply and save in ioc
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_get_ioc_facts(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2IOCFactsRequest_t mpi_request;
	Mpi2IOCFactsReply_t mpi_reply;
	struct mpt3sas_facts *facts;
	int mpi_reply_sz, mpi_request_sz, r;

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	r = _base_wait_for_iocstate(ioc, 10);
	if (r) {
		printk(MPT3SAS_ERR_FMT "%s: failed getting to correct state\n",
		    ioc->name, __func__);
		return r;
	}
	mpi_reply_sz = sizeof(Mpi2IOCFactsReply_t);
	mpi_request_sz = sizeof(Mpi2IOCFactsRequest_t);
	memset(&mpi_request, 0, mpi_request_sz);
	mpi_request.Function = MPI2_FUNCTION_IOC_FACTS;
	r = _base_handshake_req_reply_wait(ioc, mpi_request_sz,
	    (u32 *)&mpi_request, mpi_reply_sz, (u16 *)&mpi_reply, 5);

	if (r != 0) {
		printk(MPT3SAS_ERR_FMT "%s: handshake failed (r=%d)\n",
		    ioc->name, __func__, r);
		return r;
	}

	facts = &ioc->facts;
	memset(facts, 0, sizeof(struct mpt3sas_facts));
	facts->MsgVersion = le16_to_cpu(mpi_reply.MsgVersion);
	facts->HeaderVersion = le16_to_cpu(mpi_reply.HeaderVersion);
	facts->IOCNumber = mpi_reply.IOCNumber;
	printk(MPT3SAS_INFO_FMT
	    "IOC Number : %d\n", ioc->name, facts->IOCNumber);
	ioc->IOCNumber = facts->IOCNumber;
	facts->VP_ID = mpi_reply.VP_ID;
	facts->VF_ID = mpi_reply.VF_ID;
	facts->IOCExceptions = le16_to_cpu(mpi_reply.IOCExceptions);
	facts->MaxChainDepth = mpi_reply.MaxChainDepth;
	facts->WhoInit = mpi_reply.WhoInit;
	facts->NumberOfPorts = mpi_reply.NumberOfPorts;
	facts->MaxMSIxVectors = mpi_reply.MaxMSIxVectors;
	if (ioc->msix_enable && (facts->MaxMSIxVectors <= 
	    MAX_COMBINED_MSIX_VECTORS(ioc->is_gen35_ioc)))
		ioc->combined_reply_queue = 0;
	facts->RequestCredit = le16_to_cpu(mpi_reply.RequestCredit);
	facts->MaxReplyDescriptorPostQueueDepth =
	    le16_to_cpu(mpi_reply.MaxReplyDescriptorPostQueueDepth);
	facts->ProductID = le16_to_cpu(mpi_reply.ProductID);
	facts->IOCCapabilities = le32_to_cpu(mpi_reply.IOCCapabilities);
	if ((facts->IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_INTEGRATED_RAID))
		ioc->ir_firmware = 1;
	if ((facts->IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_RDPQ_ARRAY_CAPABLE)
		&& (!reset_devices))
		ioc->rdpq_array_capable = 1;
	else
		ioc->rdpq_array_capable = 0;
	if ((facts->IOCCapabilities & MPI26_IOCFACTS_CAPABILITY_ATOMIC_REQ)
		&& ioc->is_aero_ioc)
		ioc->atomic_desc_capable = 1;
	else
		ioc->atomic_desc_capable = 0;
	facts->FWVersion.Word = le32_to_cpu(mpi_reply.FWVersion.Word);
	facts->IOCRequestFrameSize =
	    le16_to_cpu(mpi_reply.IOCRequestFrameSize);
	if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
		facts->IOCMaxChainSegmentSize =
	    		le16_to_cpu(mpi_reply.IOCMaxChainSegmentSize);
	}
	facts->MaxInitiators = le16_to_cpu(mpi_reply.MaxInitiators);
	facts->MaxTargets = le16_to_cpu(mpi_reply.MaxTargets);
	ioc->shost->max_id = -1;
	facts->MaxSasExpanders = le16_to_cpu(mpi_reply.MaxSasExpanders);
	facts->MaxEnclosures = le16_to_cpu(mpi_reply.MaxEnclosures);
	facts->ProtocolFlags = le16_to_cpu(mpi_reply.ProtocolFlags);
	facts->HighPriorityCredit =
	    le16_to_cpu(mpi_reply.HighPriorityCredit);
	facts->ReplyFrameSize = mpi_reply.ReplyFrameSize;
	facts->MaxDevHandle = le16_to_cpu(mpi_reply.MaxDevHandle);
	facts->CurrentHostPageSize = mpi_reply.CurrentHostPageSize;

	/*
	 * Get the Page Size from IOC Facts. If it's 0, default to 4k.
	 */
	ioc->page_size = 1 << facts->CurrentHostPageSize;
	if (ioc->page_size == 1) {
		printk(MPT3SAS_INFO_FMT "CurrentHostPageSize is 0: Setting "
		    "default host page size to 4k\n", ioc->name);
		ioc->page_size = 1 << MPT3SAS_HOST_PAGE_SIZE_4K;
	}
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "CurrentHostPageSize(%d)\n",
                               ioc->name, facts->CurrentHostPageSize));

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "hba queue depth(%d), "
	    "max chains per io(%d)\n", ioc->name, facts->RequestCredit,
	    facts->MaxChainDepth));
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "request frame size(%d), "
	    "reply frame size(%d)\n", ioc->name,
	    facts->IOCRequestFrameSize * 4, facts->ReplyFrameSize * 4));
	return 0;
}

/**
 * _base_unmap_resources - free controller resources
 * @ioc: per adapter object
 */
static void
_base_unmap_resources(struct MPT3SAS_ADAPTER *ioc)
{
	struct pci_dev *pdev = ioc->pdev;

	printk(MPT3SAS_INFO_FMT "%s\n",
		ioc->name, __func__);

	mpt3sas_base_free_irq(ioc);
	mpt3sas_base_disable_msix(ioc);

	kfree(ioc->replyPostRegisterIndex);

	/* synchronizing freeing resource with pci_access_mutex lock */
	mutex_lock(&ioc->pci_access_mutex);
	if (ioc->chip_phys) {
		iounmap(ioc->chip);
		ioc->chip_phys = 0;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25))
	pci_release_regions(pdev);
#else
	pci_release_selected_regions(ioc->pdev, ioc->bars);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19))
	pci_disable_pcie_error_reporting(pdev);
#endif
	pci_disable_device(pdev);
	mutex_unlock(&ioc->pci_access_mutex);
	return;
}

/**
 * mpt3sas_base_map_resources - map in controller resources (io/irq/memap)
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt3sas_base_map_resources(struct MPT3SAS_ADAPTER *ioc)
{
	struct pci_dev *pdev = ioc->pdev;
	u32 memap_sz;
	u32 pio_sz;
	int i, r = 0, rc;
#ifndef CPQ_CIM
	u64 pio_chip = 0;
#endif
	phys_addr_t chip_phys = 0;
	struct adapter_reply_queue *reply_q;

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n",
	    ioc->name, __func__));

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25))
	if (pci_enable_device(pdev)) {
		printk(MPT3SAS_WARN_FMT "pci_enable_device: failed\n",
		    ioc->name);
		return -ENODEV;
	}

	if (pci_request_regions(pdev, ioc->driver_name)) {
		printk(MPT3SAS_WARN_FMT "pci_request_regions: failed\n",
		    ioc->name);
		r = -ENODEV;
		goto out_fail;
	}
#else
	ioc->bars = pci_select_bars(pdev, IORESOURCE_MEM);
	if (pci_enable_device_mem(pdev)) {
		printk(MPT3SAS_WARN_FMT "pci_enable_device_mem: "
		    "failed\n", ioc->name);
		return -ENODEV;
	}


	if (pci_request_selected_regions(pdev, ioc->bars,
	    ioc->driver_name)) {
		printk(MPT3SAS_WARN_FMT "pci_request_selected_regions: "
		    "failed\n", ioc->name);
		r = -ENODEV;
		goto out_fail;
	}
#endif

/* AER (Advanced Error Reporting) hooks */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19))
	pci_enable_pcie_error_reporting(pdev);
#endif

	pci_set_master(pdev);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
	/* for SLES10 ~ PCI EEH support */
	pci_save_state(pdev);
#endif

	if (_base_config_dma_addressing(ioc, pdev) != 0) {
		printk(MPT3SAS_WARN_FMT "no suitable DMA mask for %s\n",
		    ioc->name, pci_name(pdev));
		r = -ENODEV;
		goto out_fail;
	}

	for (i = 0, memap_sz = 0, pio_sz = 0 ; i < DEVICE_COUNT_RESOURCE; i++) {
		if (pci_resource_flags(pdev, i) & IORESOURCE_IO) {
			if (pio_sz)
				continue;
#if defined(CPQ_CIM)
			ioc->pio_chip = (u64)pci_resource_start(pdev, i);
#else
			pio_chip = (u64)pci_resource_start(pdev, i);
#endif
			pio_sz = pci_resource_len(pdev, i);
		} else if (pci_resource_flags(pdev, i) & IORESOURCE_MEM) {
			if (memap_sz)
				continue;
			ioc->chip_phys = pci_resource_start(pdev, i);
			chip_phys = ioc->chip_phys;
			memap_sz = pci_resource_len(pdev, i);
			ioc->chip = ioremap(ioc->chip_phys, memap_sz);
			if (ioc->chip == NULL) {
				printk(MPT3SAS_ERR_FMT "unable to map adapter "
				    "memory!\n", ioc->name);
				r = -EINVAL;
				goto out_fail;
			}
		}
	}

	mpt3sas_base_mask_interrupts(ioc);

	r = _base_get_ioc_facts(ioc);
	if (r) {
		rc = _base_check_for_fault_and_issue_reset(ioc);
		if (rc || (r = _base_get_ioc_facts(ioc)))
			goto out_fail;
	}

	if (!ioc->rdpq_array_enable_assigned) {
		ioc->rdpq_array_enable = ioc->rdpq_array_capable;
		ioc->rdpq_array_enable_assigned = 1;
	}

	r = _base_enable_msix(ioc);
	if (r)
		goto out_fail;

#if defined(MPT3SAS_ENABLE_IRQ_POLL)
	if (!ioc->is_driver_loading)
		_base_init_irqpolls(ioc);
#endif

	if (ioc->combined_reply_queue) {
	/* If this is an 96 vector supported device, set up ReplyPostIndex addresses */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18))
		ioc->replyPostRegisterIndex = kcalloc(ioc->nc_reply_index_count,
			sizeof(resource_size_t *), GFP_KERNEL);
#else
		ioc->replyPostRegisterIndex = kcalloc(ioc->nc_reply_index_count,
			sizeof(u64 *), GFP_KERNEL);
#endif
		if (!ioc->replyPostRegisterIndex) {
			printk(MPT3SAS_ERR_FMT
			    "allocation for reply Post Register Index failed!!!\n",
			    ioc->name);
			r = -ENOMEM;
			goto out_fail;
		}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18))
		for ( i = 0; i < ioc->nc_reply_index_count; i++ ) {
			ioc->replyPostRegisterIndex[i] =(resource_size_t *)
				((u8 *)&ioc->chip->Doorbell +
				MPI25_SUP_REPLY_POST_HOST_INDEX_OFFSET + (i * 0x10));
		}
#else
		for ( i = 0; i < ioc->nc_reply_index_count; i++ ) {
			ioc->replyPostRegisterIndex[i] = (u64 *)
				((u8 *)&ioc->chip->Doorbell +
                MPI25_SUP_REPLY_POST_HOST_INDEX_OFFSET + (i * 0x10));
		}
#endif
	}
	list_for_each_entry(reply_q, &ioc->reply_queue_list, list)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0))
		printk(MPT3SAS_INFO_FMT "%s: IRQ %d\n",
		    reply_q->name,  ((ioc->msix_enable) ? "PCI-MSI-X enabled" :
		    "IO-APIC enabled"),reply_q->vector);
#else
		printk(MPT3SAS_INFO_FMT "%s: IRQ %d\n",
		    reply_q->name,  ((ioc->msix_enable) ? "PCI-MSI-X enabled" :
		    "IO-APIC enabled"),
		    pci_irq_vector(ioc->pdev, reply_q->msix_index));
#endif
	printk(MPT3SAS_INFO_FMT "iomem(%pap), mapped(0x%p), size(%d)\n",
	    ioc->name, &chip_phys, ioc->chip, memap_sz);
#if defined(CPQ_CIM)
	printk(MPT3SAS_INFO_FMT "ioport(0x%016llx), size(%d)\n",
	    ioc->name, (unsigned long long)ioc->pio_chip, pio_sz);
#else
	printk(MPT3SAS_INFO_FMT "ioport(0x%016llx), size(%d)\n",
	    ioc->name, (unsigned long long)pio_chip, pio_sz);
#endif

/* This is causing SLES10 to fail when loading */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19))
	/* Save PCI configuration state for recovery from PCI AER/EEH errors */
	pci_save_state(pdev);
#endif
	return 0;

 out_fail:
	_base_unmap_resources(ioc);
	return r;
}

/**
 * mpt3sas_base_get_msg_frame - obtain request mf pointer
 * @ioc: per adapter object
 * @smid: system request message index(smid zero is invalid)
 *
 * Returns virt pointer to message frame.
 */
void *
mpt3sas_base_get_msg_frame(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	return (void *)(ioc->request + (smid * ioc->request_sz));
}
#if defined(TARGET_MODE)
EXPORT_SYMBOL(mpt3sas_base_get_msg_frame);
#endif

/**
 * mpt3sas_base_get_sense_buffer - obtain a sense buffer virt addr
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Returns virt pointer to sense buffer.
 */
void *
mpt3sas_base_get_sense_buffer(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	return (void *)(ioc->sense + ((smid - 1) * SCSI_SENSE_BUFFERSIZE));
}

/**
 * mpt3sas_base_get_sense_buffer_dma - obtain a sense buffer dma addr
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Returns phys pointer to the low 32bit address of the sense buffer.
 */
__le32
mpt3sas_base_get_sense_buffer_dma(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	return cpu_to_le32(ioc->sense_dma + ((smid - 1) *
	    SCSI_SENSE_BUFFERSIZE));
}

/**
 * mpt3sas_base_get_sense_buffer_dma_64 - obtain a sense buffer dma addr
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Returns phys pointer to the 64bit address of the sense buffer.
 */
__le64
mpt3sas_base_get_sense_buffer_dma_64(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	return cpu_to_le64(ioc->sense_dma + ((smid - 1) *
	    SCSI_SENSE_BUFFERSIZE));
}

/**
 * mpt3sas_base_get_pcie_sgl - obtain a PCIe SGL virt addr
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Returns virt pointer to a PCIe SGL.
 */
void *
mpt3sas_base_get_pcie_sgl(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	return (void *)(ioc->pcie_sg_lookup[smid - 1].pcie_sgl);
}

/**
 * mpt3sas_base_get_pcie_sgl_dma - obtain a PCIe SGL dma addr
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Returns phys pointer to the address of the PCIe buffer.
 */
dma_addr_t
mpt3sas_base_get_pcie_sgl_dma(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	return ioc->pcie_sg_lookup[smid - 1].pcie_sgl_dma;
}

/**
 * mpt3sas_base_get_reply_virt_addr - obtain reply frames virt address
 * @ioc: per adapter object
 * @phys_addr: lower 32 physical addr of the reply
 *
 * Converts 32bit lower physical addr into a virt address.
 */
void *
mpt3sas_base_get_reply_virt_addr(struct MPT3SAS_ADAPTER *ioc, u32 phys_addr)
{
	if (!phys_addr)
		return NULL;
	return ioc->reply + (phys_addr - (u32)ioc->reply_dma);
}
#if defined(TARGET_MODE)
EXPORT_SYMBOL(mpt3sas_base_get_reply_virt_addr);
#endif

/**
 * _base_get_msix_index - get the msix index
 * @ioc: per adapter object
 * @scmd: scsi_cmnd object
 *
 * returns msix index of general reply queues,
 * i.e. reply queue on which IO request's reply
 * should be posted by the HBA firmware.
 */
static inline u8
_base_get_msix_index(struct MPT3SAS_ADAPTER *ioc,
    struct scsi_cmnd *scmd)
{
	/* Enables reply_queue load balancing */
	if (ioc->msix_load_balance)
		return ioc->reply_queue_count ? base_mod64(atomic64_add_return(1,
		    &ioc->total_io_cnt), ioc->reply_queue_count) : 0;

	return ioc->cpu_msix_table[raw_smp_processor_id()];
}

/**
 * _base_sdev_nr_inflight_request -get number of inflight requests
 *                                of a request queue.
 * @ioc: per adapter object
 * @scmd: scsi_cmnd object
 *
 * returns number of inflight request of a request queue.
 */
inline unsigned long
_base_sdev_nr_inflight_request(struct MPT3SAS_ADAPTER *ioc, 
	struct scsi_cmnd *scmd)
{
	if (ioc->drv_internal_flags & MPT_DRV_INTERNAL_BITMAP_BLK_MQ) {
		struct blk_mq_hw_ctx *hctx =
			scmd->device->request_queue->queue_hw_ctx[0];
		return atomic_read(&hctx->nr_active);
	}
	else
		return atomic_read(&scmd->device->device_busy);
}

/**
 * _base_get_high_iops_msix_index - get the msix index of high iops queues
 * @ioc: per adapter object
 * @scmd: scsi_cmnd object
 *
 * returns msix index of high iops reply queues,
 * i.e. high iops reply queue on which IO request's
 * reply should be posted by the HBA firmware.
 */
static inline u8
_base_get_high_iops_msix_index(struct MPT3SAS_ADAPTER *ioc,
    struct scsi_cmnd *scmd)
{
	/**
	 * Round robin the IO interrupts among the high iops
	 * reply queues in terms of batch count 4 when outstanding
	 * IOs on the target device is >=8.
	 */
#if ((defined(RHEL_MAJOR) && (RHEL_MAJOR == 7 && RHEL_MINOR >= 2)) || \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(3,17,0))) 
	if (_base_sdev_nr_inflight_request(ioc, scmd) >
	    MPT3SAS_DEVICE_HIGH_IOPS_DEPTH)
#else
	if (scmd->device->device_busy >
	    MPT3SAS_DEVICE_HIGH_IOPS_DEPTH)
#endif
		return base_mod64((
		    atomic64_add_return(1, &ioc->high_iops_outstanding) /
		    MPT3SAS_HIGH_IOPS_BATCH_COUNT),
		    MPT3SAS_HIGH_IOPS_REPLY_QUEUES);

	return _base_get_msix_index(ioc, scmd);
}

/**
 * mpt3sas_base_get_smid - obtain a free smid from internal queue
 * @ioc: per adapter object
 * @cb_idx: callback index
 *
 * Returns smid (zero is invalid)
 */
u16
mpt3sas_base_get_smid(struct MPT3SAS_ADAPTER *ioc, u8 cb_idx)
{
	unsigned long flags;
	struct request_tracker *request;
	u16 smid;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	if (list_empty(&ioc->internal_free_list)) {
		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		printk(MPT3SAS_ERR_FMT "%s: smid not available\n",
		    ioc->name, __func__);
		return 0;
	}

	request = list_entry(ioc->internal_free_list.next,
	    struct request_tracker, tracker_list);
	request->cb_idx = cb_idx;
	smid = request->smid;
	list_del(&request->tracker_list);
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return smid;
}
#if defined(TARGET_MODE)
EXPORT_SYMBOL(mpt3sas_base_get_smid);
#endif

/**
 * mpt3sas_base_get_smid_scsiio - obtain a free smid from scsiio queue
 * @ioc: per adapter object
 * @cb_idx: callback index
 * @scmd: pointer to scsi command object
 *
 * Returns smid (zero is invalid)
 */
u16
mpt3sas_base_get_smid_scsiio(struct MPT3SAS_ADAPTER *ioc, u8 cb_idx,
	struct scsi_cmnd *scmd)
{
	struct scsiio_tracker *request;
	unsigned int tag = scmd->request->tag;
	u16 smid;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0))
	scmd->host_scribble = (unsigned char *)(&ioc->scsi_lookup[tag]);
#endif
	request = mpt3sas_base_scsi_cmd_priv(scmd);
	smid = tag + 1;
	request->cb_idx = cb_idx;
	request->smid = smid;
	request->scmd = scmd;
	return smid;
}
#if defined(TARGET_MODE)
EXPORT_SYMBOL(mpt3sas_base_get_smid_scsiio);
#endif

/**
 * mpt3sas_base_get_smid_hpr - obtain a free smid from hi-priority queue
 * @ioc: per adapter object
 * @cb_idx: callback index
 *
 * Returns smid (zero is invalid)
 */
u16
mpt3sas_base_get_smid_hpr(struct MPT3SAS_ADAPTER *ioc, u8 cb_idx)
{
	unsigned long flags;
	struct request_tracker *request;
	u16 smid;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	if (list_empty(&ioc->hpr_free_list)) {
		spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
		return 0;
	}

	request = list_entry(ioc->hpr_free_list.next,
	    struct request_tracker, tracker_list);
	request->cb_idx = cb_idx;
	smid = request->smid;
	list_del(&request->tracker_list);
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return smid;
}

static void
_base_recovery_check(struct MPT3SAS_ADAPTER *ioc)
{
	/*
	 * See mpt3sas_wait_for_commands_to_complete() call with
	 * regards to this code.
	 */
	if (ioc->shost_recovery && ioc->pending_io_count) {
		if (ioc->pending_io_count == 1)
			wake_up(&ioc->reset_wq);
		ioc->pending_io_count--;
	}
}

void mpt3sas_base_clear_st(struct MPT3SAS_ADAPTER *ioc,
			  struct scsiio_tracker *st)
{
	if (!st)
		return;
	if (WARN_ON(st->smid == 0))
		return;
	st->cb_idx = 0xFF;
	st->direct_io = 0;
	st->scmd = NULL;
	atomic_set(&ioc->chain_lookup[st->smid - 1].chain_offset, 0);
}

/**
 * mpt3sas_base_free_smid - put smid back on free_list
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return nothing.
 */
void
mpt3sas_base_free_smid(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	unsigned long flags;
	int i;
	struct scsiio_tracker *st;
	void *request;

	if (smid < ioc->hi_priority_smid) {
		/* scsiio queue */
		st = mpt3sas_get_st_from_smid(ioc, smid);

		if (!st) {
			_base_recovery_check(ioc);
			return;
		}
		/* Clear MPI request frame */
		request = mpt3sas_base_get_msg_frame(ioc, smid);
		memset(request, 0, ioc->request_sz);
		mpt3sas_base_clear_st(ioc, st);
		_base_recovery_check(ioc);
		return;
	}

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	if (smid < ioc->internal_smid) {
		/* hi-priority */
		i = smid - ioc->hi_priority_smid;
		ioc->hpr_lookup[i].cb_idx = 0xFF;
		list_add(&ioc->hpr_lookup[i].tracker_list, &ioc->hpr_free_list);
	} else if (smid <= ioc->hba_queue_depth) {
		/* internal queue */
		i = smid - ioc->internal_smid;
		ioc->internal_lookup[i].cb_idx = 0xFF;
		list_add(&ioc->internal_lookup[i].tracker_list,
		    &ioc->internal_free_list);
	}
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
}
#if defined(TARGET_MODE)
EXPORT_SYMBOL(mpt3sas_base_free_smid);
#endif

/**
 * _base_mpi_ep_writeq - 32 bit write to MMIO
 * @b: data payload
 * @addr: address in MMIO space
 * @writeq_lock: spin lock
 *
 * This special handling for MPI EP to take care of 32 bit 
 * environment where its not quarenteed to send the entire word
 * in one transfer.
 */
static inline void
_base_mpi_ep_writeq(__u64 b, volatile void __iomem *addr, spinlock_t *writeq_lock)
{
	unsigned long flags;
	__u64 data_out = b;

	spin_lock_irqsave(writeq_lock, flags);
	writel((u32)(data_out), addr);
	writel((u32)(data_out >> 32), (addr + 4));
#if (((LINUX_VERSION_CODE < KERNEL_VERSION(5,2,0)) && (!defined(RHEL_MAJOR))) || \
        (defined(RHEL_MAJOR) && ((RHEL_MAJOR == 8 && RHEL_MINOR < 2) || (RHEL_MAJOR < 8))))
	mmiowb();
#endif
	spin_unlock_irqrestore(writeq_lock, flags);
}

/**
 * _base_writeq - 64 bit write to MMIO
 * @ioc: per adapter object
 * @b: data payload
 * @addr: address in MMIO space
 * @writeq_lock: spin lock
 *
 * Glue for handling an atomic 64 bit word to MMIO. This special handling takes
 * care of 32 bit environment where its not quarenteed to send the entire word
 * in one transfer.
 */
#if defined(writeq) && defined(CONFIG_64BIT)
static inline void
_base_writeq(__u64 b, volatile void __iomem *addr, spinlock_t *writeq_lock)
{
	writeq(b, addr);
}
#else
static inline void
_base_writeq(__u64 b, volatile void __iomem *addr, spinlock_t *writeq_lock)
{
	unsigned long flags;
	__u64 data_out = b;

	spin_lock_irqsave(writeq_lock, flags);
	writel((u32)(data_out), addr);
	writel((u32)(data_out >> 32), (addr + 4));
	spin_unlock_irqrestore(writeq_lock, flags);
}
#endif

/**
 * _base_set_and_get_msix_index - get the msix index and assign to msix_io
 *                                variable of scsi tracker
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * returns msix index.
 */
static u8
_base_set_and_get_msix_index(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	struct scsiio_tracker *st;

	st = (smid < ioc->hi_priority_smid) ? (mpt3sas_get_st_from_smid(ioc, smid)) : (NULL);

	if (st == NULL)
		return  _base_get_msix_index(ioc, NULL);

	st->msix_io = ioc->get_msix_index_for_smlio(ioc, st->scmd);
	return st->msix_io;
}

/**
 * _base_put_smid_mpi_ep_scsi_io - send SCSI_IO request to firmware
 * @ioc: per adapter object
 * @smid: system request message index
 * @handle: device handle
 *
 * Return nothing.
 */
static void
_base_put_smid_mpi_ep_scsi_io(struct MPT3SAS_ADAPTER *ioc, u16 smid, u16 handle)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;
	void *mpi_req_iomem;

	__le32 *mfp = (__le32 *)mpt3sas_base_get_msg_frame(ioc, smid);
	_clone_sg_entries(ioc, (void*) mfp, smid);
	mpi_req_iomem = (void*)ioc->chip + MPI_FRAME_START_OFFSET + (smid * ioc->request_sz);
	_base_clone_mpi_to_sys_mem( mpi_req_iomem, (void*)mfp, ioc->request_sz);	
	
	descriptor.SCSIIO.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO;
	descriptor.SCSIIO.MSIxIndex =  _base_set_and_get_msix_index(ioc, smid);
	descriptor.SCSIIO.SMID = cpu_to_le16(smid);
	descriptor.SCSIIO.DevHandle = cpu_to_le16(handle);
	descriptor.SCSIIO.LMID = 0;
	_base_mpi_ep_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}
/**
 * _base_put_smid_scsi_io - send SCSI_IO request to firmware
 * @ioc: per adapter object
 * @smid: system request message index
 * @handle: device handle
 *
 * Return nothing.
 */
static void
_base_put_smid_scsi_io(struct MPT3SAS_ADAPTER *ioc, u16 smid, u16 handle)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;

	descriptor.SCSIIO.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO;
	descriptor.SCSIIO.MSIxIndex =  _base_set_and_get_msix_index(ioc, smid);
	descriptor.SCSIIO.SMID = cpu_to_le16(smid);
	descriptor.SCSIIO.DevHandle = cpu_to_le16(handle);
	descriptor.SCSIIO.LMID = 0;

	_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}

/**
 * _base_put_smid_fast_path - send fast path request to firmware
 * @ioc: per adapter object
 * @smid: system request message index
 * @handle: device handle
 * @msix_task: msix_task will be same as msix of IO incase of task abort else 0.
 *
 * Return nothing.
 */
static void
_base_put_smid_fast_path(struct MPT3SAS_ADAPTER *ioc, u16 smid, u16 handle)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;

	descriptor.SCSIIO.RequestFlags =
	    MPI25_REQ_DESCRIPT_FLAGS_FAST_PATH_SCSI_IO;
	descriptor.SCSIIO.MSIxIndex =  _base_set_and_get_msix_index(ioc, smid);
	descriptor.SCSIIO.SMID = cpu_to_le16(smid);
	descriptor.SCSIIO.DevHandle = cpu_to_le16(handle);
	descriptor.SCSIIO.LMID = 0;
	_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}

/**
 * _base_put_smid_hi_priority - send Task Managment request to firmware
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_task: msix_task will be same as msix of IO incase of task abort else 
 * 0. Return nothing. 
 */
static void
_base_put_smid_hi_priority(struct MPT3SAS_ADAPTER *ioc, u16 smid, u16 msix_task)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	void *mpi_req_iomem;
	u64 *request;

	if (ioc->is_mcpu_endpoint) {
		MPI2RequestHeader_t *request_hdr;
		__le32 *mfp = (__le32 *)mpt3sas_base_get_msg_frame(ioc, smid);
		request_hdr = (MPI2RequestHeader_t*) mfp;
		/*TBD 256 is offset within sys register. */
		mpi_req_iomem = (void*)ioc->chip + MPI_FRAME_START_OFFSET + (smid * ioc->request_sz);
		_base_clone_mpi_to_sys_mem( mpi_req_iomem, (void*)mfp, ioc->request_sz);	
	}

	request = (u64 *)&descriptor;

	descriptor.HighPriority.RequestFlags =
	    MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY;
	descriptor.HighPriority.MSIxIndex =  msix_task;
	descriptor.HighPriority.SMID = cpu_to_le16(smid);
	descriptor.HighPriority.LMID = 0;
	descriptor.HighPriority.Reserved1 = 0;
	if (ioc->is_mcpu_endpoint)
		_base_mpi_ep_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    	    &ioc->scsi_lookup_lock);
	else
		_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
		    &ioc->scsi_lookup_lock);
}

/**
 * _base_put_smid_nvme_encap - send NVMe encapsulated request to
 *  firmware
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return nothing.
 */
static void
_base_put_smid_nvme_encap(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;

	descriptor.Default.RequestFlags =
		MPI26_REQ_DESCRIPT_FLAGS_PCIE_ENCAPSULATED;
	descriptor.Default.MSIxIndex =  _base_set_and_get_msix_index(ioc, smid);
	descriptor.Default.SMID = cpu_to_le16(smid);
	descriptor.Default.LMID = 0;
	descriptor.Default.DescriptorTypeDependent = 0;
	_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}

/**
 * _base_put_smid_default - Default, primarily used for config pages
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Return nothing.
 */
static void
_base_put_smid_default(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	void *mpi_req_iomem;
	u64 *request;
	MPI2RequestHeader_t *request_hdr;

	if (ioc->is_mcpu_endpoint) {
		__le32 *mfp = (__le32 *)mpt3sas_base_get_msg_frame(ioc, smid);
		request_hdr = (MPI2RequestHeader_t*) mfp;
	
		_clone_sg_entries(ioc, (void*) mfp, smid);
	
		/* TBD 256 is offset within sys register */
		mpi_req_iomem = (void*)ioc->chip + MPI_FRAME_START_OFFSET + (smid * ioc->request_sz);
		_base_clone_mpi_to_sys_mem( mpi_req_iomem, (void*)mfp, ioc->request_sz);
	}
	request = (u64 *)&descriptor;
	descriptor.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	descriptor.Default.MSIxIndex =  _base_set_and_get_msix_index(ioc, smid);
	descriptor.Default.SMID = cpu_to_le16(smid);
	descriptor.Default.LMID = 0;
	descriptor.Default.DescriptorTypeDependent = 0;
	if (ioc->is_mcpu_endpoint)
		_base_mpi_ep_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    	    &ioc->scsi_lookup_lock);
	else
		_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    	    &ioc->scsi_lookup_lock);
}

#if defined(TARGET_MODE)
/**
 * _base_put_smid_target_assist - send Target Assist/Status to firmware
 * @ioc: per adapter object
 * @smid: system request message index
 * @io_index: value used to track the IO
 *
 * Return nothing.
 */
void
_base_put_smid_target_assist(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u16 io_index)
{
	Mpi2RequestDescriptorUnion_t descriptor;
	u64 *request = (u64 *)&descriptor;

	descriptor.SCSITarget.RequestFlags =
	    MPI2_REQ_DESCRIPT_FLAGS_SCSI_TARGET;
	descriptor.SCSITarget.MSIxIndex =  _base_set_and_get_msix_index(ioc, smid);
	descriptor.SCSITarget.SMID = cpu_to_le16(smid);
	descriptor.SCSITarget.LMID = 0;
	descriptor.SCSITarget.IoIndex = cpu_to_le16(io_index);
	_base_writeq(*request, &ioc->chip->RequestDescriptorPostLow,
	    &ioc->scsi_lookup_lock);
}
#endif

/**
* _base_put_smid_scsi_io_atomic - send SCSI_IO request to firmware using
*   Atomic Request Descriptor
* @ioc: per adapter object
* @smid: system request message index 
* @handle: device handle, unused in this function, for function type match 
*
* Return nothing.
*/
static void
_base_put_smid_scsi_io_atomic(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u16 handle)
{
	Mpi26AtomicRequestDescriptor_t descriptor;
	u32 *request = (u32 *)&descriptor;

	descriptor.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO;
	descriptor.MSIxIndex = _base_set_and_get_msix_index(ioc, smid);
	descriptor.SMID = cpu_to_le16(smid);

	writel(cpu_to_le32(*request), &ioc->chip->AtomicRequestDescriptorPost);
}

/**
* _base_put_smid_fast_path_atomic - send fast path request to firmware
*   using Atomic Request Descriptor
* @ioc: per adapter object
* @smid: system request message index
* @handle: device handle, unused in this function, for function type match 
* Return nothing.
*/
static void
_base_put_smid_fast_path_atomic(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u16 handle)
{
	Mpi26AtomicRequestDescriptor_t descriptor;
	u32 *request = (u32 *)&descriptor;

	descriptor.RequestFlags = MPI25_REQ_DESCRIPT_FLAGS_FAST_PATH_SCSI_IO;
	descriptor.MSIxIndex = _base_set_and_get_msix_index(ioc, smid);
	descriptor.SMID = cpu_to_le16(smid);

	writel(cpu_to_le32(*request), &ioc->chip->AtomicRequestDescriptorPost);
}

/**
* _base_put_smid_hi_priority_atomic - send Task Managment request to
*   firmware using Atomic Request Descriptor
* @ioc: per adapter object
* @smid: system request message index
*  
* @msix_task: msix_task will be same as msix of IO incase of task abort else 0
* 
* Return nothing.
*/
static void
_base_put_smid_hi_priority_atomic(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u16 msix_task)
{
	Mpi26AtomicRequestDescriptor_t descriptor;
	u32 *request = (u32 *)&descriptor;

	descriptor.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY;
	descriptor.MSIxIndex = msix_task;
	descriptor.SMID = cpu_to_le16(smid);

	writel(cpu_to_le32(*request), &ioc->chip->AtomicRequestDescriptorPost);
}

/**
* _base_put_smid_nvme_encap_atomic - send NVMe encapsulated request to
*   firmware using Atomic Request Descriptor
* @ioc: per adapter object
* @smid: system request message index
*
* Return nothing.
*/
static void
_base_put_smid_nvme_encap_atomic(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	Mpi26AtomicRequestDescriptor_t descriptor;
	u32 *request = (u32 *)&descriptor;

	descriptor.RequestFlags = MPI26_REQ_DESCRIPT_FLAGS_PCIE_ENCAPSULATED;
	descriptor.MSIxIndex = _base_set_and_get_msix_index(ioc, smid);
	descriptor.SMID = cpu_to_le16(smid);

	writel(cpu_to_le32(*request), &ioc->chip->AtomicRequestDescriptorPost);
}

/**
* _base_put_smid_default - Default, primarily used for config pages
*   use Atomic Request Descriptor
* @ioc: per adapter object
* @smid: system request message index
*
* Return nothing.
*/
static void
_base_put_smid_default_atomic(struct MPT3SAS_ADAPTER *ioc, u16 smid)
{
	Mpi26AtomicRequestDescriptor_t descriptor;
	u32 *request = (u32 *)&descriptor;

	descriptor.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	descriptor.MSIxIndex = _base_set_and_get_msix_index(ioc, smid);
	descriptor.SMID = cpu_to_le16(smid);

	writel(cpu_to_le32(*request), &ioc->chip->AtomicRequestDescriptorPost);
}


#if defined(TARGET_MODE)
/**
 * _base_put_smid_target_assist_atomic - send Target Assist/Status to 
 *  firmware using Atomic Request Descriptor
 * @ioc: per adapter object
 * @smid: system request message index
 * @io_index: value used to track the IO, unused, for function type match
 *  
 * Return nothing.
 */
static void
_base_put_smid_target_assist_atomic(struct MPT3SAS_ADAPTER *ioc, u16 smid,
	u16 io_index)
{
	Mpi26AtomicRequestDescriptor_t descriptor;
	u32 *request = (u32 *)&descriptor;

	descriptor.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_SCSI_TARGET;
	descriptor.MSIxIndex = _base_set_and_get_msix_index(ioc, smid);
	descriptor.SMID = cpu_to_le16(smid);

	writel(cpu_to_le32(*request), &ioc->chip->AtomicRequestDescriptorPost);
}
#endif


/**
 * _base_display_OEMs_branding - Display branding string
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_base_display_OEMs_branding(struct MPT3SAS_ADAPTER *ioc)
{
	switch(ioc->pdev->subsystem_vendor) {
	case PCI_VENDOR_ID_INTEL:
		switch (ioc->pdev->device) {
		case MPI2_MFGPAGE_DEVID_SAS2008:
			switch (ioc->pdev->subsystem_device) {
			case MPT2SAS_INTEL_RMS2LL080_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS2LL080_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS2LL040_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS2LL040_BRANDING);
				break;
			case MPT2SAS_INTEL_SSD910_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_SSD910_BRANDING);
				break;
			default:
				printk(MPT3SAS_INFO_FMT "Intel(R) Controller:"
				    " Device ID: 0x%X Subsystem ID: 0x%X\n", ioc->name,
				    ioc->pdev->device, ioc->pdev->subsystem_device);
				break;
			}
			break;
		case MPI2_MFGPAGE_DEVID_SAS2308_2:
			switch (ioc->pdev->subsystem_device) {
			case MPT2SAS_INTEL_RS25GB008_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RS25GB008_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25JB080_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS25JB080_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25JB040_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS25JB040_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25KB080_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS25KB080_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25KB040_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS25KB040_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25LB040_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS25LB040_BRANDING);
				break;
			case MPT2SAS_INTEL_RMS25LB080_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_INTEL_RMS25LB080_BRANDING);
				break;
			default:
				printk(MPT3SAS_INFO_FMT "Intel(R) Controller:"
				    " Device ID: 0x%X Subsystem ID: 0x%X\n", ioc->name,
				    ioc->pdev->device, ioc->pdev->subsystem_device);
				break;
			}
			break;
		case MPI25_MFGPAGE_DEVID_SAS3008:
			switch (ioc->pdev->subsystem_device) {
			case MPT3SAS_INTEL_RMS3JC080_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT3SAS_INTEL_RMS3JC080_BRANDING);
				break;
			case MPT3SAS_INTEL_RS3GC008_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT3SAS_INTEL_RS3GC008_BRANDING);
				break;
			case MPT3SAS_INTEL_RS3FC044_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT3SAS_INTEL_RS3FC044_BRANDING);
				break;
			case MPT3SAS_INTEL_RS3UC080_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT3SAS_INTEL_RS3UC080_BRANDING);
				break;
			case MPT3SAS_INTEL_RS3PC_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT3SAS_INTEL_RS3PC_BRANDING);
				break;
			default:
				printk(MPT3SAS_INFO_FMT "Intel(R) Controller:"
				    " Device ID: 0x%X Subsystem ID: 0x%X\n", ioc->name,
				    ioc->pdev->device, ioc->pdev->subsystem_device);
				break;
			}
			break;
		default:
			printk(MPT3SAS_INFO_FMT "Intel(R) Controller:"
			    " Device ID: 0x%X Subsystem ID: 0x%X\n", ioc->name,
			    ioc->pdev->device, ioc->pdev->subsystem_device);
			break;
		}
		break;
	case PCI_VENDOR_ID_DELL:
		switch (ioc->pdev->device) {
		case MPI2_MFGPAGE_DEVID_SAS2008:
			switch (ioc->pdev->subsystem_device) {
			case MPT2SAS_DELL_6GBPS_SAS_HBA_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_DELL_6GBPS_SAS_HBA_BRANDING);	
				break;
			case MPT2SAS_DELL_PERC_H200_ADAPTER_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_DELL_PERC_H200_ADAPTER_BRANDING);
				break;
			case MPT2SAS_DELL_PERC_H200_INTEGRATED_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_DELL_PERC_H200_INTEGRATED_BRANDING);
				break;
			case MPT2SAS_DELL_PERC_H200_MODULAR_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_DELL_PERC_H200_MODULAR_BRANDING);
				break;
			case MPT2SAS_DELL_PERC_H200_EMBEDDED_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_DELL_PERC_H200_EMBEDDED_BRANDING);
				break;
			case MPT2SAS_DELL_PERC_H200_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_DELL_PERC_H200_BRANDING);
				break;
			case MPT2SAS_DELL_6GBPS_SAS_SSDID:				
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_DELL_6GBPS_SAS_BRANDING);
				break;
			default:
				printk(MPT3SAS_INFO_FMT "Dell 6Gbps SAS HBA:"
				    " Device ID: 0x%X  Subsystem ID: 0x%X\n", ioc->name,
				    ioc->pdev->device, ioc->pdev->subsystem_device);
				break;
			}
			break;
		case MPI25_MFGPAGE_DEVID_SAS3008:
			switch (ioc->pdev->subsystem_device) {
			case MPT3SAS_DELL_12G_HBA_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT3SAS_DELL_12G_HBA_BRANDING);
				break;
			case MPT3SAS_DELL_HBA330_ADP_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT3SAS_DELL_HBA330_ADP_BRANDING);
				break;
			case MPT3SAS_DELL_HBA330_MINI_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT3SAS_DELL_HBA330_MINI_BRANDING);
				break;
        	        default:
				printk(MPT3SAS_INFO_FMT "Dell 12Gbps SAS HBA:"
				    " Device ID: 0x%X  Subsystem ID: 0x%X\n", ioc->name,
				    ioc->pdev->device, ioc->pdev->subsystem_device);
				break;
        	        }
                	break;
		default:
			printk(MPT3SAS_INFO_FMT "Dell SAS HBA:"
			    " Device ID: 0x%X  Subsystem ID: 0x%X\n", ioc->name,
			    ioc->pdev->device, ioc->pdev->subsystem_device);
                        break;
		}
		break;
#if ((defined(RHEL_MAJOR) && (RHEL_MAJOR == 5 && RHEL_MINOR >= 7)) || LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39))
	case PCI_VENDOR_ID_CISCO:
		switch (ioc->pdev->device) {
		case MPI25_MFGPAGE_DEVID_SAS3008:
			switch (ioc->pdev->subsystem_device) {
			case MPT3SAS_CISCO_12G_8E_HBA_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT3SAS_CISCO_12G_8E_HBA_BRANDING);
				break;
			case MPT3SAS_CISCO_12G_8I_HBA_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT3SAS_CISCO_12G_8I_HBA_BRANDING);
				break;
			default:
				printk(MPT3SAS_INFO_FMT "Cisco 12Gbps SAS HBA:"
				    " Device ID:0x%X Subsystem ID: 0x%X\n", ioc->name,
				    ioc->pdev->device, ioc->pdev->subsystem_device);
				break;
			}
			break;
		case MPI25_MFGPAGE_DEVID_SAS3108_1:
			switch (ioc->pdev->subsystem_device) {
			case MPT3SAS_CISCO_12G_AVILA_HBA_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT3SAS_CISCO_12G_AVILA_HBA_BRANDING);
				break;
			case MPT3SAS_CISCO_12G_COLUSA_MEZZANINE_HBA_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT3SAS_CISCO_12G_COLUSA_MEZZANINE_HBA_BRANDING);
				break;
			default:
				printk(MPT3SAS_INFO_FMT "Cisco 12Gbps SAS HBA:"
				    " Device ID: 0x%X Subsystem ID: 0x%X\n", ioc->name,
				    ioc->pdev->device, ioc->pdev->subsystem_device);
				break;
			}
			break;
		default:
			printk(MPT3SAS_INFO_FMT "Cisco SAS HBA:"
				" Device ID: 0x%X Subsystem ID: 0x%X\n", ioc->name,
				ioc->pdev->device, ioc->pdev->subsystem_device);
			break;
		}
		break;
#endif
	case MPT2SAS_HP_3PAR_SSVID:
		switch (ioc->pdev->device) {
		case MPI2_MFGPAGE_DEVID_SAS2004:
			switch (ioc->pdev->subsystem_device) {
			case MPT2SAS_HP_DAUGHTER_2_4_INTERNAL_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_HP_DAUGHTER_2_4_INTERNAL_BRANDING);
				break;
			default:
				printk(MPT3SAS_INFO_FMT "HP 6Gbps SAS HBA:"
				    " Device ID: 0x%X Subsystem ID: 0x%X\n", ioc->name,
				    ioc->pdev->device, ioc->pdev->subsystem_device);
				break;
			}
			break;
		case MPI2_MFGPAGE_DEVID_SAS2308_2:
			switch (ioc->pdev->subsystem_device) {
			case MPT2SAS_HP_2_4_INTERNAL_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_HP_2_4_INTERNAL_BRANDING);
				break;
			case MPT2SAS_HP_2_4_EXTERNAL_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_HP_2_4_EXTERNAL_BRANDING);
				break;
			case MPT2SAS_HP_1_4_INTERNAL_1_4_EXTERNAL_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_HP_1_4_INTERNAL_1_4_EXTERNAL_BRANDING);
				break;
			case MPT2SAS_HP_EMBEDDED_2_4_INTERNAL_SSDID:
				printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
				    MPT2SAS_HP_EMBEDDED_2_4_INTERNAL_BRANDING);
				break;
			default:
				printk(MPT3SAS_INFO_FMT "HP 12Gbps SAS HBA:"
				    " Device ID: 0x%X Subsystem ID: 0x%X\n", ioc->name,
				    ioc->pdev->device, ioc->pdev->subsystem_device);
				break;
			}
			break;
		default:
			printk(MPT3SAS_INFO_FMT "HP 12Gbps SAS HBA:"
			    " Device ID: 0x%X Subsystem ID: 0x%X\n", ioc->name,
			    ioc->pdev->device, ioc->pdev->subsystem_device);
			break;
		}
	}
}

/**
 * _base_display_fwpkg_version - sends FWUpload request to pull FWPkg 
 * 				 version from FW Image Header.
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_display_fwpkg_version(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2FWImageHeader_t *fw_img_hdr;
	Mpi26ComponentImageHeader_t *cmp_img_hdr;
	Mpi25FWUploadRequest_t *mpi_request;
	Mpi2FWUploadReply_t mpi_reply;
	int r = 0;
	u32  package_version = 0;
	void *fwpkg_data = NULL;
	dma_addr_t fwpkg_data_dma;
	u16 smid, ioc_status;
	size_t data_length;

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	if (ioc->base_cmds.status & MPT3_CMD_PENDING) {
		printk(MPT3SAS_ERR_FMT "%s: internal command already in use\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}

	data_length = sizeof(Mpi2FWImageHeader_t);
	fwpkg_data = pci_alloc_consistent(ioc->pdev, data_length,
		&fwpkg_data_dma);
	if (!fwpkg_data) {
		printk(MPT3SAS_ERR_FMT
		    "Memory allocation for fwpkg data got failed at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -ENOMEM;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		printk(MPT3SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		r = -EAGAIN;
		goto out;
	}

	ioc->base_cmds.status = MPT3_CMD_PENDING;
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->base_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi25FWUploadRequest_t));
	mpi_request->Function = MPI2_FUNCTION_FW_UPLOAD;
	mpi_request->ImageType = MPI2_FW_UPLOAD_ITYPE_FW_FLASH;
	mpi_request->ImageSize = data_length;
	ioc->build_sg(ioc, &mpi_request->SGL, 0, 0, fwpkg_data_dma,
	    data_length);
	init_completion(&ioc->base_cmds.done);
	ioc->put_smid_default(ioc, smid);
	/* Wait for 15 seconds */
	wait_for_completion_timeout(&ioc->base_cmds.done, 
			FW_IMG_HDR_READ_TIMEOUT*HZ);
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: complete\n",
	    ioc->name, __func__));
	if (!(ioc->base_cmds.status & MPT3_CMD_COMPLETE)) {
		printk(MPT3SAS_ERR_FMT "%s: timeout\n",
		    ioc->name, __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi25FWUploadRequest_t)/4);
		r = -ETIME;
	} 
	else {
		memset(&mpi_reply, 0, sizeof(Mpi2FWUploadReply_t));
		if (ioc->base_cmds.status & MPT3_CMD_REPLY_VALID) {
			memcpy(&mpi_reply, ioc->base_cmds.reply,
		    	    sizeof(Mpi2FWUploadReply_t));
			ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & 
			    MPI2_IOCSTATUS_MASK;
			if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
				fw_img_hdr = (Mpi2FWImageHeader_t *)fwpkg_data;
				if (le32_to_cpu(fw_img_hdr->Signature) ==
						MPI26_IMAGE_HEADER_SIGNATURE0_MPI26) {
					cmp_img_hdr = (Mpi26ComponentImageHeader_t *)(fwpkg_data);
					package_version =
						le32_to_cpu(cmp_img_hdr->ApplicationSpecific);
				}
				else	
					package_version =
						le32_to_cpu(fw_img_hdr->PackageVersion.Word);
				if (package_version)
					printk(MPT3SAS_INFO_FMT "FW Package Version(%02d.%02d.%02d.%02d)\n",
					    ioc->name, ((package_version) & 0xFF000000) >> 24,
					    ((package_version) & 0x00FF0000) >> 16,
					    ((package_version) & 0x0000FF00) >> 8,
					    (package_version) & 0x000000FF);
			}
			else {
				_debug_dump_mf(&mpi_reply,
				    sizeof(Mpi2FWUploadReply_t)/4);
			}
		}
	}
 	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
 out:
	if (fwpkg_data)
		pci_free_consistent(ioc->pdev, data_length, fwpkg_data,
		    fwpkg_data_dma);
	return r;
}

/**
 * _base_display_ioc_capabilities - Disply IOC's capabilities.
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_base_display_ioc_capabilities(struct MPT3SAS_ADAPTER *ioc)
{
	int i = 0;
	char desc[16];
	u8 revision;
	u32 iounit_pg1_flags;
	u32 bios_version;

	pci_read_config_byte(ioc->pdev, PCI_CLASS_REVISION, &revision);
	strncpy(desc, ioc->manu_pg0.ChipName, 16);
	bios_version = le32_to_cpu(ioc->bios_pg3.BiosVersion);
	printk(MPT3SAS_INFO_FMT "%s: FWVersion(%02d.%02d.%02d.%02d), "
	   "ChipRevision(0x%02x), BiosVersion(%02d.%02d.%02d.%02d)\n",
	   ioc->name, desc,
	  (ioc->facts.FWVersion.Word & 0xFF000000) >> 24,
	  (ioc->facts.FWVersion.Word & 0x00FF0000) >> 16,
	  (ioc->facts.FWVersion.Word & 0x0000FF00) >> 8,
	  ioc->facts.FWVersion.Word & 0x000000FF,
	  revision,
	  (bios_version & 0xFF000000) >> 24,
	  (bios_version & 0x00FF0000) >> 16,
	  (bios_version & 0x0000FF00) >> 8,
	   bios_version & 0x000000FF);
	_base_display_OEMs_branding(ioc);

	printk(MPT3SAS_INFO_FMT "Protocol=(", ioc->name);

	if (ioc->facts.ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_SCSI_INITIATOR) {
		printk("Initiator");
		i++;
	}

	if (ioc->facts.ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_SCSI_TARGET) {
		printk("%sTarget", i ? "," : "");
		i++;
	}

	if (ioc->facts.ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_NVME_DEVICES) {
		printk("%sNVMe", i ? "," : "");
		i++;
	}

	i = 0;
	printk("), ");
	printk("Capabilities=(");

	if ((!ioc->warpdrive_msg) && (ioc->facts.IOCCapabilities &
		    MPI2_IOCFACTS_CAPABILITY_INTEGRATED_RAID)) {
			printk("Raid");
			i++;
	}

	if (ioc->facts.IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_TLR) {
		printk("%sTLR", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_MULTICAST) {
		printk("%sMulticast", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_BIDIRECTIONAL_TARGET) {
		printk("%sBIDI Target", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_EEDP) {
		printk("%sEEDP", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_SNAPSHOT_BUFFER) {
		printk("%sSnapshot Buffer", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_DIAG_TRACE_BUFFER) {
		printk("%sDiag Trace Buffer", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_EXTENDED_BUFFER) {
		printk("%sDiag Extended Buffer", i ? "," : "");
		i++;
	}

	if (ioc->facts.IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_TASK_SET_FULL_HANDLING) {
		printk("%sTask Set Full", i ? "," : "");
		i++;
	}

	iounit_pg1_flags = le32_to_cpu(ioc->iounit_pg1.Flags);
	if (!(iounit_pg1_flags & MPI2_IOUNITPAGE1_NATIVE_COMMAND_Q_DISABLE)) {
		printk("%sNCQ", i ? "," : "");
		i++;
	}

	printk(")\n");
}

/**
 * mpt3sas_base_update_missing_delay - change the missing delay timers
 * @ioc: per adapter object
 * @device_missing_delay: amount of time till device is reported missing
 * @io_missing_delay: interval IO is returned when there is a missing device
 *
 * Return nothing.
 *
 * Passed on the command line, this function will modify the device missing
 * delay, as well as the io missing delay. This should be called at driver
 * load time.
 */
void
mpt3sas_base_update_missing_delay(struct MPT3SAS_ADAPTER *ioc,
	u16 device_missing_delay, u8 io_missing_delay)
{
	u16 dmd, dmd_new, dmd_orignal;
	u8 io_missing_delay_original;
	u16 sz;
	Mpi2SasIOUnitPage1_t *sas_iounit_pg1 = NULL;
	Mpi2ConfigReply_t mpi_reply;
	u8 num_phys = 0;
	u16 ioc_status;

	mpt3sas_config_get_number_hba_phys(ioc, &num_phys);
	if (!num_phys)
		return;

	sz = offsetof(Mpi2SasIOUnitPage1_t, PhyData) + (num_phys *
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

	/* device missing delay */
	dmd = sas_iounit_pg1->ReportDeviceMissingDelay;
	if (dmd & MPI2_SASIOUNIT1_REPORT_MISSING_UNIT_16)
		dmd = (dmd & MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK) * 16;
	else
		dmd = dmd & MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK;
	dmd_orignal = dmd;
	if (device_missing_delay > 0x7F) {
		dmd = (device_missing_delay > 0x7F0) ? 0x7F0 :
		    device_missing_delay;
		dmd = dmd / 16;
		dmd |= MPI2_SASIOUNIT1_REPORT_MISSING_UNIT_16;
	} else
		dmd = device_missing_delay;
	sas_iounit_pg1->ReportDeviceMissingDelay = dmd;

	/* io missing delay */
	io_missing_delay_original = sas_iounit_pg1->IODeviceMissingDelay;
	sas_iounit_pg1->IODeviceMissingDelay = io_missing_delay;

	if (!mpt3sas_config_set_sas_iounit_pg1(ioc, &mpi_reply, sas_iounit_pg1,
	    sz)) {
		if (dmd & MPI2_SASIOUNIT1_REPORT_MISSING_UNIT_16)
			dmd_new = (dmd &
			    MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK) * 16;
		else
			dmd_new =
		    dmd & MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK;
		printk(MPT3SAS_INFO_FMT "device_missing_delay: old(%d), "
		    "new(%d)\n", ioc->name, dmd_orignal, dmd_new);
		printk(MPT3SAS_INFO_FMT "ioc_missing_delay: old(%d), "
		    "new(%d)\n", ioc->name, io_missing_delay_original,
		    io_missing_delay);
		ioc->device_missing_delay = dmd_new;
		ioc->io_missing_delay = io_missing_delay;
	}

 out:
	kfree(sas_iounit_pg1);
}

/**
 * _base_update_ioc_page1_inlinewith_perf_mode - Update IOC Page1 fields
 *    according to performance mode.
 * @ioc : per adapter object
 *
 * Return nothing.
 */
static void
_base_update_ioc_page1_inlinewith_perf_mode(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2IOCPage1_t ioc_pg1;
	Mpi2ConfigReply_t mpi_reply;

	mpt3sas_config_get_ioc_pg1(ioc, &mpi_reply, &ioc->ioc_pg1_copy);
	memcpy(&ioc_pg1, &ioc->ioc_pg1_copy, sizeof(Mpi2IOCPage1_t));

	switch (perf_mode) {
	case MPT_PERF_MODE_DEFAULT:
	case MPT_PERF_MODE_BALANCED:
		if (ioc->high_iops_queues) {
			printk(MPT3SAS_INFO_FMT
			    "Enable interrupt coalescing only for first %d"
			    " reply queues\n", ioc->name,
			    MPT3SAS_HIGH_IOPS_REPLY_QUEUES);
			/* 
			 * If 31st bit is zero then interrupt coalescing is
			 * enabled for all reply descriptor post queues.
			 * If 31st bit is set to one then user can
			 * enable/disable interrupt coalescing on per reply
			 * descriptor post queue group(8) basis. So to enable
			 * interrupt coalescing only on first reply descriptor
			 * post queue group 31st bit and zero th bit is enabled.
			 */
			ioc_pg1.ProductSpecific = cpu_to_le32(0x80000000 |
			    ((1 << MPT3SAS_HIGH_IOPS_REPLY_QUEUES/8) - 1));
			mpt3sas_config_set_ioc_pg1(ioc, &mpi_reply, &ioc_pg1);
			printk(MPT3SAS_INFO_FMT
			    "performance mode: balanced\n", ioc->name);
			return;
		}
	/* Fall through */
	case MPT_PERF_MODE_LATENCY:
		/*
		 * Enable interrupt coalescing on all reply queues
		 * with timeout value 0xA
		 */
		ioc_pg1.CoalescingTimeout = cpu_to_le32(0xa);
		ioc_pg1.Flags |= cpu_to_le32(MPI2_IOCPAGE1_REPLY_COALESCING);
		ioc_pg1.ProductSpecific = 0;
		mpt3sas_config_set_ioc_pg1(ioc, &mpi_reply, &ioc_pg1);
		printk(MPT3SAS_INFO_FMT
		    "performance mode: latency\n", ioc->name);
		break;
	case MPT_PERF_MODE_IOPS:
		/*
		 * Enable interrupt coalescing on all reply queues.
		 */
		printk(MPT3SAS_INFO_FMT
		    "performance mode: iops with coalescing timeout: 0x%x\n",
		    ioc->name, le32_to_cpu(ioc_pg1.CoalescingTimeout));
		ioc_pg1.Flags |= cpu_to_le32(MPI2_IOCPAGE1_REPLY_COALESCING);
		ioc_pg1.ProductSpecific = 0;
		mpt3sas_config_set_ioc_pg1(ioc, &mpi_reply, &ioc_pg1);
		break;
	}
}

/**
 * _base_get_mpi_diag_triggers - get mpi diag trigger values from
 *				persistent pages
 * @ioc : per adapter object
 *
 * Return nothing.
 */
static void
_base_get_mpi_diag_triggers(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi26DriverTriggerPage4_t trigger_pg4;
	struct SL_WH_MPI_TRIGGER_T *status_tg;
	MPI26_DRIVER_IOCSTATUS_LOGINFO_TIGGER_ENTRY *mpi_status_tg;
	Mpi2ConfigReply_t mpi_reply;
	int r = 0, i = 0;
	u16 count = 0;
	u16 ioc_status;

	r = mpt3sas_config_get_driver_trigger_pg4(ioc, &mpi_reply,
	    &trigger_pg4);
	if (r)
		return;

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		dinitprintk(ioc,
		    pr_err(MPT3SAS_FMT
		    "%s: Failed to get trigger pg4, ioc_status(0x%04x)\n",
		    ioc->name, __func__, ioc_status));
		return;
	}

	if (le16_to_cpu(trigger_pg4.NumIOCStatusLogInfoTrigger)) {
		count = le16_to_cpu(trigger_pg4.NumIOCStatusLogInfoTrigger);
		count = min_t(u16, NUM_VALID_ENTRIES, count);
		ioc->diag_trigger_mpi.ValidEntries = count;

		status_tg = &ioc->diag_trigger_mpi.MPITriggerEntry[0];
		mpi_status_tg = &trigger_pg4.IOCStatusLoginfoTriggers[0];

		for (i = 0; i < count; i++) {
			status_tg->IOCStatus = le16_to_cpu(
			    mpi_status_tg->IOCStatus);
			status_tg->IocLogInfo = le32_to_cpu(
			    mpi_status_tg->LogInfo);

			status_tg++;
			mpi_status_tg++;
		}
	}
}

/**
 * _base_get_scsi_diag_triggers - get scsi diag trigger values from
 *				persistent pages
 * @ioc : per adapter object
 *
 * Return nothing.
 */
static void
_base_get_scsi_diag_triggers(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi26DriverTriggerPage3_t trigger_pg3;
	struct SL_WH_SCSI_TRIGGER_T *scsi_tg;
	MPI26_DRIVER_SCSI_SENSE_TIGGER_ENTRY *mpi_scsi_tg;
	Mpi2ConfigReply_t mpi_reply;
	int r = 0, i = 0;
	u16 count = 0;
	u16 ioc_status;

	r = mpt3sas_config_get_driver_trigger_pg3(ioc, &mpi_reply,
	    &trigger_pg3);
	if (r)
		return;

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		dinitprintk(ioc,
		    pr_err(MPT3SAS_FMT
		    "%s: Failed to get trigger pg3, ioc_status(0x%04x)\n",
		    ioc->name, __func__, ioc_status));
		return;
	}

	if (le16_to_cpu(trigger_pg3.NumSCSISenseTrigger)) {
		count = le16_to_cpu(trigger_pg3.NumSCSISenseTrigger);
		count = min_t(u16, NUM_VALID_ENTRIES, count);
		ioc->diag_trigger_scsi.ValidEntries = count;

		scsi_tg = &ioc->diag_trigger_scsi.SCSITriggerEntry[0];
		mpi_scsi_tg = &trigger_pg3.SCSISenseTriggers[0];
		for (i = 0; i < count; i++) {
			scsi_tg->ASCQ = mpi_scsi_tg->ASCQ;
			scsi_tg->ASC = mpi_scsi_tg->ASC;
			scsi_tg->SenseKey = mpi_scsi_tg->SenseKey;

			scsi_tg++;
			mpi_scsi_tg++;
		}
	}
}

/**
 * _base_get_event_diag_triggers - get event diag trigger values from
 *				persistent pages
 * @ioc : per adapter object
 *
 * Return nothing.
 */
static void
_base_get_event_diag_triggers(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi26DriverTriggerPage2_t trigger_pg2;
	struct SL_WH_EVENT_TRIGGER_T *event_tg;
	MPI26_DRIVER_MPI_EVENT_TIGGER_ENTRY *mpi_event_tg;
	Mpi2ConfigReply_t mpi_reply;
	int r = 0, i = 0;
	u16 count = 0;
	u16 ioc_status;

	r = mpt3sas_config_get_driver_trigger_pg2(ioc, &mpi_reply,
	    &trigger_pg2);
	if (r)
		return;

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		dinitprintk(ioc,
		    pr_err(MPT3SAS_FMT
		    "%s: Failed to get trigger pg2, ioc_status(0x%04x)\n",
		    ioc->name, __func__, ioc_status));
		return;
	}

	if (le16_to_cpu(trigger_pg2.NumMPIEventTrigger)) {
		count = le16_to_cpu(trigger_pg2.NumMPIEventTrigger);
		count = min_t(u16, NUM_VALID_ENTRIES, count);
		ioc->diag_trigger_event.ValidEntries = count;

		event_tg = &ioc->diag_trigger_event.EventTriggerEntry[0];
		mpi_event_tg = &trigger_pg2.MPIEventTriggers[0];
		for (i = 0; i < count; i++) {
			event_tg->EventValue = le16_to_cpu(
			    mpi_event_tg->MPIEventCode);
			event_tg->LogEntryQualifier = le16_to_cpu(
			    mpi_event_tg->MPIEventCodeSpecific);
			event_tg++;
			mpi_event_tg++;
		}
	}
}

/**
 * _base_get_master_diag_triggers - get master diag trigger values from
 *				persistent pages
 * @ioc : per adapter object
 *
 * Return nothing.
 */
static void
_base_get_master_diag_triggers(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi26DriverTriggerPage1_t trigger_pg1;
	Mpi2ConfigReply_t mpi_reply;
	int r;
	u16 ioc_status;

	r = mpt3sas_config_get_driver_trigger_pg1(ioc, &mpi_reply,
	    &trigger_pg1);
	if (r)
		return;

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		dinitprintk(ioc,
		    pr_err(MPT3SAS_FMT
		    "%s: Failed to get trigger pg1, ioc_status(0x%04x)\n",
		    ioc->name, __func__, ioc_status));
		return;
	}

	if (le16_to_cpu(trigger_pg1.NumMasterTrigger))
		ioc->diag_trigger_master.MasterData |=
		    le32_to_cpu(
		    trigger_pg1.MasterTriggers[0].MasterTriggerFlags);
}

/**
 * _base_check_for_trigger_pages_support - checks whether HBA FW supports
 *					driver trigger pages or not
 * @ioc : per adapter object
 *
 * Returns trigger flags mask if HBA FW supports driver trigger pages,
 * otherwise returns EFAULT.
 */
static int
_base_check_for_trigger_pages_support(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi26DriverTriggerPage0_t trigger_pg0;
	int r = 0;
	Mpi2ConfigReply_t mpi_reply;
	u16 ioc_status;

	r = mpt3sas_config_get_driver_trigger_pg0(ioc, &mpi_reply,
	    &trigger_pg0);
	if (r)
		return -EFAULT;

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
		return -EFAULT;

	return le16_to_cpu(trigger_pg0.TriggerFlags);
}

/**
 * _base_get_diag_triggers - Retrieve diag trigger values from
 *				persistent pages.
 * @ioc : per adapter object
 *
 * Return nothing.
 */
static void
_base_get_diag_triggers(struct MPT3SAS_ADAPTER *ioc)
{
	short trigger_flags;

	/*
	 * Default setting of master trigger.
	 */
	ioc->diag_trigger_master.MasterData =
	    (MASTER_TRIGGER_FW_FAULT + MASTER_TRIGGER_ADAPTER_RESET);

	trigger_flags = _base_check_for_trigger_pages_support(ioc);
	if (trigger_flags < 0)
		return;

	ioc->supports_trigger_pages = 1;

	/*
	 * Retrieve master diag trigger values from driver trigger pg1
	 * if master trigger bit enabled in TriggerFlags.
	 */
	if ((u16)trigger_flags &
	    MPI26_DRIVER_TRIGGER0_FLAG_MASTER_TRIGGER_VALID)
		_base_get_master_diag_triggers(ioc);

	/*
	 * Retrieve event diag trigger values from driver trigger pg2
	 * if event trigger bit enabled in TriggerFlags.
	 */
	if ((u16)trigger_flags &
	    MPI26_DRIVER_TRIGGER0_FLAG_MPI_EVENT_TRIGGER_VALID)
		_base_get_event_diag_triggers(ioc);

	/*
	 * Retrieve scsi diag trigger values from driver trigger pg3
	 * if scsi trigger bit enabled in TriggerFlags.
	 */
	if ((u16)trigger_flags &
	    MPI26_DRIVER_TRIGGER0_FLAG_SCSI_SENSE_TRIGGER_VALID)
		_base_get_scsi_diag_triggers(ioc);

	/*
	 * Retrieve mpi error diag trigger values from driver trigger pg4
	 * if loginfo trigger bit enabled in TriggerFlags.
	 */
	if ((u16)trigger_flags &
	    MPI26_DRIVER_TRIGGER0_FLAG_LOGINFO_TRIGGER_VALID)
		_base_get_mpi_diag_triggers(ioc);
}

/**
 * _base_update_diag_trigger_pages - Update the driver trigger pages after
 *			online FW update, incase updated FW supports driver
 *			trigger pages.
 * @ioc : per adapter object
 *
 * Return nothing.
 */
static void
_base_update_diag_trigger_pages(struct MPT3SAS_ADAPTER *ioc)
{

	if (ioc->diag_trigger_master.MasterData)
		mpt3sas_config_update_driver_trigger_pg1(ioc,
		    &ioc->diag_trigger_master, 1);

	if (ioc->diag_trigger_event.ValidEntries)
		mpt3sas_config_update_driver_trigger_pg2(ioc,
		    &ioc->diag_trigger_event, 1);

	if (ioc->diag_trigger_scsi.ValidEntries)
		mpt3sas_config_update_driver_trigger_pg3(ioc,
		    &ioc->diag_trigger_scsi, 1);

	if (ioc->diag_trigger_mpi.ValidEntries)
		mpt3sas_config_update_driver_trigger_pg4(ioc,
		    &ioc->diag_trigger_mpi, 1);
}

/**
 * _base_static_config_pages - static start of day config pages
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_base_static_config_pages(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2ConfigReply_t mpi_reply;
	u32 iounit_pg1_flags;
	int tg_flags;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0) && \
    LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
	u32 cap;
	int ret;
#endif

	ioc->nvme_abort_timeout = 30;
	mpt3sas_config_get_manufacturing_pg0(ioc, &mpi_reply, &ioc->manu_pg0);
	if (ioc->ir_firmware || ioc->is_warpdrive)
		mpt3sas_config_get_manufacturing_pg10(ioc, &mpi_reply,
		    &ioc->manu_pg10);

	mpt3sas_config_get_manufacturing_pg11(ioc, &mpi_reply, &ioc->manu_pg11);
	if ((!ioc->is_gen35_ioc) && (!ioc->disable_eedp_support)) {
		/*
		 * Ensure correct T10 PI operation if vendor left EEDPTagMode
		 * flag unset in NVDATA.
		 */
		if (ioc->manu_pg11.EEDPTagMode == 0) {
			printk(KERN_ERR "%s: overriding NVDATA EEDPTagMode setting\n",
			    ioc->name);
			ioc->manu_pg11.EEDPTagMode &= ~0x3;
			ioc->manu_pg11.EEDPTagMode |= 0x1;
			mpt3sas_config_set_manufacturing_pg11(ioc, &mpi_reply,
			    &ioc->manu_pg11);
		}
	}

	if (ioc->manu_pg11.AddlFlags2 & NVME_TASK_MNGT_CUSTOM_MASK)
		ioc->tm_custom_handling = 1;
	else { 
		ioc->tm_custom_handling = 0;
		if (ioc->manu_pg11.NVMeAbortTO < NVME_TASK_ABORT_MIN_TIMEOUT)
			ioc->nvme_abort_timeout = NVME_TASK_ABORT_MIN_TIMEOUT;
		else if (ioc->manu_pg11.NVMeAbortTO > NVME_TASK_ABORT_MAX_TIMEOUT)
			ioc->nvme_abort_timeout = NVME_TASK_ABORT_MAX_TIMEOUT;
		else 
			ioc->nvme_abort_timeout = ioc->manu_pg11.NVMeAbortTO;
	}
	
	ioc->time_sync_interval =
		ioc->manu_pg11.TimeSyncInterval & MPT3SAS_TIMESYNC_MASK;
	if (ioc->time_sync_interval) {
		if (ioc->manu_pg11.TimeSyncInterval & MPT3SAS_TIMESYNC_UNIT_MASK)
			ioc->time_sync_interval =
				ioc->time_sync_interval * SECONDS_PER_HOUR;
		else
			ioc->time_sync_interval =
				ioc->time_sync_interval * SECONDS_PER_MIN;
		dinitprintk(ioc, printk(MPT3SAS_FMT
		    "Driver-FW TimeSync interval is %d seconds. "
		    "ManuPg11 TimeSync Unit is in %s's", ioc->name,
		    ioc->time_sync_interval, ((ioc->manu_pg11.TimeSyncInterval &
		    MPT3SAS_TIMESYNC_UNIT_MASK) ? "Hour" : "Minute")));
	}
	else {
		if (ioc->is_gen35_ioc)
			pr_warn("%s: TimeSync Interval in Manuf page-11 is not enabled.\n"
			    "Periodic Time-Sync will be disabled \n", ioc->name);
	}

	mpt3sas_config_get_bios_pg2(ioc, &mpi_reply, &ioc->bios_pg2);
	mpt3sas_config_get_bios_pg3(ioc, &mpi_reply, &ioc->bios_pg3);
	mpt3sas_config_get_ioc_pg8(ioc, &mpi_reply, &ioc->ioc_pg8);
	mpt3sas_config_get_iounit_pg0(ioc, &mpi_reply, &ioc->iounit_pg0);
	mpt3sas_config_get_iounit_pg1(ioc, &mpi_reply, &ioc->iounit_pg1);
	mpt3sas_config_get_iounit_pg8(ioc, &mpi_reply, &ioc->iounit_pg8);
	_base_display_ioc_capabilities(ioc);

#if defined(CPQ_CIM)
	mpt3sas_config_get_ioc_pg1(ioc, &mpi_reply, &ioc->ioc_pg1);
#endif
	/*
	 * Enable task_set_full handling in iounit_pg1 when the
	 * facts capabilities indicate that its supported.
	 */
	iounit_pg1_flags = le32_to_cpu(ioc->iounit_pg1.Flags);
	if ((ioc->facts.IOCCapabilities &
		MPI2_IOCFACTS_CAPABILITY_TASK_SET_FULL_HANDLING))
		iounit_pg1_flags &=
		    ~MPI2_IOUNITPAGE1_DISABLE_TASK_SET_FULL_HANDLING;
	else
		iounit_pg1_flags |=
		    MPI2_IOUNITPAGE1_DISABLE_TASK_SET_FULL_HANDLING;
	ioc->iounit_pg1.Flags = cpu_to_le32(iounit_pg1_flags);
	mpt3sas_config_set_iounit_pg1(ioc, &mpi_reply, &ioc->iounit_pg1);
	

	if(ioc->iounit_pg8.NumSensors)
		ioc->temp_sensors_count = ioc->iounit_pg8.NumSensors;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0) && \
    LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
	/* Enabling PCIe extended tag feature if HBA supports,
	 * On Aero/SEA card sometimes this feature is getting disabled
	 * on kernel lessthan 4.11. */
	pcie_capability_read_dword(ioc->pdev, PCI_EXP_DEVCAP, &cap);
	if (cap & PCI_EXP_DEVCAP_EXT_TAG) {
		ret = pcie_capability_set_word(ioc->pdev, PCI_EXP_DEVCTL,
		    PCI_EXP_DEVCTL_EXT_TAG);
		if (!ret)
			dev_info(&ioc->pdev->dev,
			    "Enabled Extended Tags as Controller Supports\n");
		else
			dev_info(&ioc->pdev->dev,
			    "Unable to Enable Extended Tags feature\n");
	}
#endif
	if (ioc->is_aero_ioc)
		_base_update_ioc_page1_inlinewith_perf_mode(ioc);

	if (ioc->is_gen35_ioc) {
		if (ioc->is_driver_loading)
			_base_get_diag_triggers(ioc);
		else {
			/*
			 * In case of online HBA FW update operation,
			 * check whether updated FW supports the driver trigger
			 * pages or not.
			 * - If previous FW has not supported driver trigger
			 *   pages and newer FW supports them then update these
			 *   pages with current diag trigger values.
			 * - If previous FW has supported driver trigger pages
			 *   and new FW doesn't support them then disable
			 *   support_trigger_pages flag.
			 */
			tg_flags = _base_check_for_trigger_pages_support(ioc);
			if (!ioc->supports_trigger_pages && tg_flags != -EFAULT)
				_base_update_diag_trigger_pages(ioc);
			else if (ioc->supports_trigger_pages &&
			    tg_flags == -EFAULT)
				ioc->supports_trigger_pages = 0;
		}
	}
}


/**
 * mpt3sas_free_enclosure_list - release memory
 * @ioc: per adapter object
 *
 * Free memory allocated during encloure add.
 *
 * Return nothing.
 */
void
mpt3sas_free_enclosure_list(struct MPT3SAS_ADAPTER *ioc)
{
	struct _enclosure_node *enclosure_dev, *enclosure_dev_next;

	/* Free enclosure list */
	list_for_each_entry_safe(enclosure_dev,
			enclosure_dev_next, &ioc->enclosure_list, list) {
		list_del(&enclosure_dev->list);
		kfree(enclosure_dev);
	}
}

/**
 * _base_release_memory_pools - release memory
 * @ioc: per adapter object
 *
 * Free memory allocated from _base_allocate_memory_pools.
 *
 * Return nothing.
 */
static void
_base_release_memory_pools(struct MPT3SAS_ADAPTER *ioc)
{
	int i, j;
	int dma_alloc_count = 0;
	struct chain_tracker *ct;
	int count = ioc->rdpq_array_enable ? ioc->reply_queue_count : 1;

	dexitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	if (ioc->request) {
		dma_free_coherent(&ioc->pdev->dev, ioc->request_dma_sz,
		    ioc->request,  ioc->request_dma);
		dexitprintk(ioc, printk(MPT3SAS_INFO_FMT "request_pool(0x%p)"
		    ": free\n", ioc->name, ioc->request));
		ioc->request = NULL;
	}

	if (ioc->sense) {
		dma_pool_free(ioc->sense_dma_pool, ioc->sense, ioc->sense_dma);
		dma_pool_destroy(ioc->sense_dma_pool);
		dexitprintk(ioc, printk(MPT3SAS_INFO_FMT "sense_pool(0x%p)"
		    ": free\n", ioc->name, ioc->sense));
		ioc->sense = NULL;
	}

	if (ioc->reply) {
		dma_pool_free(ioc->reply_dma_pool, ioc->reply, ioc->reply_dma);
		dma_pool_destroy(ioc->reply_dma_pool);
		dexitprintk(ioc, printk(MPT3SAS_INFO_FMT "reply_pool(0x%p)"
		     ": free\n", ioc->name, ioc->reply));
		ioc->reply = NULL;
	}

	if (ioc->reply_free) {
		dma_pool_free(ioc->reply_free_dma_pool, ioc->reply_free,
		    ioc->reply_free_dma);
		dma_pool_destroy(ioc->reply_free_dma_pool);
		dexitprintk(ioc, printk(MPT3SAS_INFO_FMT "reply_free_pool"
		    "(0x%p): free\n", ioc->name, ioc->reply_free));
		ioc->reply_free = NULL;
	}

	if (ioc->reply_post) {
		dma_alloc_count = DIV_ROUND_UP(count,
				RDPQ_MAX_INDEX_IN_ONE_CHUNK);
		for (i = 0; i < count; i++) {
			if (i % RDPQ_MAX_INDEX_IN_ONE_CHUNK == 0
							&& dma_alloc_count) {
				if (ioc->reply_post[i].reply_post_free) {
					dma_pool_free(
					    ioc->reply_post_free_dma_pool,
					    ioc->reply_post[i].reply_post_free,
					ioc->reply_post[i].reply_post_free_dma);
					printk(MPT3SAS_ERR_FMT
					   "reply_post_free_pool(0x%p): free\n",
					   ioc->name,
					   ioc->reply_post[i].reply_post_free);
					ioc->reply_post[i].reply_post_free =
									NULL;
				}
				--dma_alloc_count;
			}
		}
		dma_pool_destroy(ioc->reply_post_free_dma_pool);
		if (ioc->reply_post_free_array &&
			ioc->rdpq_array_enable) {
			dma_pool_free(ioc->reply_post_free_array_dma_pool,
			    ioc->reply_post_free_array,
			    ioc->reply_post_free_array_dma);
			ioc->reply_post_free_array = NULL;
		}
		dma_pool_destroy(ioc->reply_post_free_array_dma_pool);
		kfree(ioc->reply_post);
	}
	
	if(ioc->pcie_sgl_dma_pool) {
		for (i = 0; i < ioc->scsiio_depth; i++) {
			dma_pool_free(ioc->pcie_sgl_dma_pool,
				      ioc->pcie_sg_lookup[i].pcie_sgl,
				      ioc->pcie_sg_lookup[i].pcie_sgl_dma);
		}
		dma_pool_destroy(ioc->pcie_sgl_dma_pool);
	}

	if (ioc->pcie_sg_lookup)
		kfree(ioc->pcie_sg_lookup);

	if (ioc->config_page) {
		dexitprintk(ioc, printk(MPT3SAS_INFO_FMT
		    "config_page(0x%p): free\n", ioc->name,
		    ioc->config_page));
		dma_free_coherent(&ioc->pdev->dev, ioc->config_page_sz,
		    ioc->config_page, ioc->config_page_dma);
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0))
	if (ioc->scsi_lookup) {
		free_pages((ulong)ioc->scsi_lookup, ioc->scsi_lookup_pages);
		ioc->scsi_lookup = NULL;
	}
#endif
	kfree(ioc->hpr_lookup);
	kfree(ioc->internal_lookup);
	if (ioc->chain_lookup) {
		for (i = 0; i < ioc->scsiio_depth; i++) {
			for (j = ioc->chains_per_prp_buffer;
			    j < ioc->chains_needed_per_io; j++) {
				ct = &ioc->chain_lookup[i].chains_per_smid[j];
				if (ct && ct->chain_buffer)
					dma_pool_free(ioc->chain_dma_pool,
					    ct->chain_buffer,
					    ct->chain_buffer_dma);
			}
			kfree(ioc->chain_lookup[i].chains_per_smid);
		}
		dma_pool_destroy(ioc->chain_dma_pool);
		kfree(ioc->chain_lookup);
		ioc->chain_lookup = NULL;
	}
}

/**
 * mpt3sas_check_same_4gb_region - checks whether all reply queues in a set are
 * 		     having same upper 32bits in their base memory address.
 * @reply_pool_start_address: Base address of a reply queue set
 * @pool_sz: Size of single Reply Descriptor Post Queues pool size
 *
 * Returns 1 if reply queues in a set have a same upper 32bits in their base memory address,
 * else 0
 */

static int
mpt3sas_check_same_4gb_region(long reply_pool_start_address, u32 pool_sz)
{
	long reply_pool_end_address;

	reply_pool_end_address = reply_pool_start_address + pool_sz;

	if (upper_32_bits(reply_pool_start_address) ==
			upper_32_bits(reply_pool_end_address))
		return 1;
	else
		return 0;
}


/**
 * _base_reduce_hba_queue_depth- Retry with reduced queue depth
 * @ioc: Adapter object
 *
 * Return: 0 for success, non-zero for failure.
 */
static inline int
_base_reduce_hba_queue_depth(struct MPT3SAS_ADAPTER *ioc)
{
	int reduce_sz = 64;
	if ((ioc->hba_queue_depth - reduce_sz) >
			(ioc->internal_depth + INTERNAL_SCSIIO_CMDS_COUNT)) {
		ioc->hba_queue_depth -= reduce_sz;
		return 0;
	} else
		return -ENOMEM;
}

/**
 * _base_allocate_reply_post_free_array - Allocating DMA'able memory
 *			for reply post free array.
 * @ioc: Adapter object
 * @reply_post_free_array_sz: DMA Pool size
 * Return: 0 for success, non-zero for failure.
 */

static int
_base_allocate_reply_post_free_array(struct MPT3SAS_ADAPTER *ioc,
		int reply_post_free_array_sz)
{
	ioc->reply_post_free_array_dma_pool =
	    dma_pool_create("reply_post_free_array pool",
	    &ioc->pdev->dev, reply_post_free_array_sz, 16, 0);
	if (!ioc->reply_post_free_array_dma_pool) {
		dinitprintk(ioc,
		    pr_err("reply_post_free_array pool: dma_pool_create failed\n"));
		return -ENOMEM;
	}
	ioc->reply_post_free_array =
	    dma_pool_alloc(ioc->reply_post_free_array_dma_pool,
	    GFP_KERNEL, &ioc->reply_post_free_array_dma);
	if (!ioc->reply_post_free_array) {
		dinitprintk(ioc, pr_err(
		    "reply_post_free_array pool: dma_pool_alloc failed\n"));
		return -EAGAIN; 
	}
	if (!mpt3sas_check_same_4gb_region((long)ioc->reply_post_free_array,
	    reply_post_free_array_sz)) {
		dinitprintk(ioc,
			pr_err("Bad Reply Free Pool! Reply Free (0x%p)"
			    "Reply Free dma = (0x%llx)\n",
			    ioc->reply_free,
			    (unsigned long long) ioc->reply_free_dma));
		ioc->use_32bit_dma = 1;
		return -EAGAIN;
	}

	return 0;
}

/**
 * base_alloc_rdpq_dma_pool - Allocating DMA'able memory
 *			for reply queues.
 * @ioc: Adapter object
 * @sz: DMA Pool size
 * Return: 0 for success, non-zero for failure.
 */
static int
base_alloc_rdpq_dma_pool(struct MPT3SAS_ADAPTER *ioc, int sz)
{
	int i = 0;
	u32 dma_alloc_count = 0;
	int reply_post_free_sz = ioc->reply_post_queue_depth *
		sizeof(Mpi2DefaultReplyDescriptor_t);
	int count = ioc->rdpq_array_enable ? ioc->reply_queue_count : 1;
	ioc->reply_post = kcalloc(count, sizeof(struct reply_post_struct), GFP_KERNEL);
	if (!ioc->reply_post) {
		printk(MPT3SAS_ERR_FMT
		    "reply_post_free pool: kcalloc failed\n", ioc->name);
		return -ENOMEM;
	}
	/*
	 *  For INVADER_SERIES each set of 8 reply queues(0-7, 8-15, ..) and
	 *  VENTURA_SERIES each set of 16 reply queues(0-15, 16-31, ..) should
	 *  be within 4GB boundary and also reply queues in a set must have same
	 *  upper 32-bits in their memory address. so here driver is allocating
	 *  the DMA'able memory for reply queues according.
	 *  Driver uses limitation of
	 *  VENTURA_SERIES to manage INVADER_SERIES as well.
	 */
	dma_alloc_count = DIV_ROUND_UP(count,
				RDPQ_MAX_INDEX_IN_ONE_CHUNK);
	ioc->reply_post_free_dma_pool =
		dma_pool_create("reply_post_free pool",
		    &ioc->pdev->dev, sz, 16, 0);
	if (!ioc->reply_post_free_dma_pool) {
		pr_err(KERN_ERR "reply_post_free pool: dma_pool_create failed\n");
		return -ENOMEM;
	}
	for (i = 0; i < count; i++) {
		if ((i % RDPQ_MAX_INDEX_IN_ONE_CHUNK == 0) && dma_alloc_count) {
			ioc->reply_post[i].reply_post_free =
				dma_pool_zalloc(ioc->reply_post_free_dma_pool,
				    GFP_KERNEL,
				    &ioc->reply_post[i].reply_post_free_dma);
			if (!ioc->reply_post[i].reply_post_free) {
				pr_err(KERN_ERR "reply_post_free pool: "
				    "dma_pool_alloc failed\n");
				return -EAGAIN; 
			}
		/* reply desc pool requires to be in same 4 gb region.
		 * Below function will check this.
		 * In case of failure, new pci pool will be created with updated
		 * alignment.
		 * For RDPQ buffers, driver allocates two separate pci pool.
		 * Alignment will be used such a way that next allocation if
		 * success, will always meet same 4gb region requirement.
		 * Flag dma_pool keeps track of each buffers pool,
		 * It will help driver while freeing the resources.
		 */
			if (!mpt3sas_check_same_4gb_region(
				(long)ioc->reply_post[i].reply_post_free, sz)) {
				dinitprintk(ioc,
				    printk(MPT3SAS_ERR_FMT "bad Replypost free pool(0x%p)"
				    "reply_post_free_dma = (0x%llx)\n", ioc->name,
				    ioc->reply_post[i].reply_post_free,
				    (unsigned long long)
				    ioc->reply_post[i].reply_post_free_dma));
				ioc->use_32bit_dma = 1;
				return -EAGAIN;
			}
			dma_alloc_count--;
		} else {
			ioc->reply_post[i].reply_post_free =
			    (Mpi2ReplyDescriptorsUnion_t *)
			    ((long)ioc->reply_post[i-1].reply_post_free
			    + reply_post_free_sz);
			ioc->reply_post[i].reply_post_free_dma =
			    (dma_addr_t)
			    (ioc->reply_post[i-1].reply_post_free_dma +
			    reply_post_free_sz);
		}
	}
	return 0;
}

/**
 * _base_allocate_pcie_sgl_pool - Allocating DMA'able memory
 *			for pcie sgl pools.
 * @ioc: Adapter object
 * @sz: DMA Pool size
 * @ct: Chain tracker
 * Return: 0 for success, non-zero for failure.
 */

static int
_base_allocate_pcie_sgl_pool(struct MPT3SAS_ADAPTER *ioc, int sz,
	struct chain_tracker *ct)
{
	int i = 0, j = 0;

	ioc->pcie_sgl_dma_pool = dma_pool_create("PCIe SGL pool", &ioc->pdev->dev, sz,
			ioc->page_size, 0);
	if (!ioc->pcie_sgl_dma_pool) {
		printk(MPT3SAS_ERR_FMT "PCIe SGL pool: dma_pool_create failed\n",
				ioc->name);
		return -ENOMEM;
	}

	ioc->chains_per_prp_buffer = sz/ioc->chain_segment_sz;
	ioc->chains_per_prp_buffer =
	    min(ioc->chains_per_prp_buffer, ioc->chains_needed_per_io);
	for (i = 0; i < ioc->scsiio_depth; i++) {
		ioc->pcie_sg_lookup[i].pcie_sgl =
		    dma_pool_alloc(ioc->pcie_sgl_dma_pool, GFP_KERNEL,
		    &ioc->pcie_sg_lookup[i].pcie_sgl_dma);
		if (!ioc->pcie_sg_lookup[i].pcie_sgl) {
			printk(MPT3SAS_ERR_FMT
			    "PCIe SGL pool: dma_pool_alloc failed\n", ioc->name);
			return -EAGAIN; 
		}

		if (!mpt3sas_check_same_4gb_region(
		    (long)ioc->pcie_sg_lookup[i].pcie_sgl, sz)) {
			printk(MPT3SAS_ERR_FMT "PCIE SGLs are not in same 4G !!"
			    " pcie sgl (0x%p) dma = (0x%llx)\n",
			    ioc->name, ioc->pcie_sg_lookup[i].pcie_sgl,
			    (unsigned long long)
			    ioc->pcie_sg_lookup[i].pcie_sgl_dma);
			ioc->use_32bit_dma = 1;
			return -EAGAIN;
		}

		for (j = 0; j < ioc->chains_per_prp_buffer; j++) {
			ct = &ioc->chain_lookup[i].chains_per_smid[j];
			ct->chain_buffer =
			    ioc->pcie_sg_lookup[i].pcie_sgl +
			    (j * ioc->chain_segment_sz);
			ct->chain_buffer_dma =
			    ioc->pcie_sg_lookup[i].pcie_sgl_dma +
			    (j * ioc->chain_segment_sz);
		}
	}
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "PCIe sgl pool depth(%d), "
	    "element_size(%d), pool_size(%d kB)\n",
	     ioc->name, ioc->scsiio_depth, sz, (sz * ioc->scsiio_depth)/1024));
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "Number of chains can "
	    "fit in a PRP page(%d)\n", ioc->name, ioc->chains_per_prp_buffer));
	return 0;
}

/**
 * _base_allocate_chain_dma_pool - Allocating DMA'able memory
 *			for chain dma pool.
 * @ioc: Adapter object
 * @sz: DMA Pool size
 * @ct: Chain tracker
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_allocate_chain_dma_pool(struct MPT3SAS_ADAPTER *ioc, int sz,
	struct chain_tracker *ctr)
{
	int i = 0, j = 0;

	ioc->chain_dma_pool = dma_pool_create("chain pool", &ioc->pdev->dev,
			ioc->chain_segment_sz, 16, 0);
	if (!ioc->chain_dma_pool) {
		printk(MPT3SAS_ERR_FMT "chain_dma_pool: dma_pool_create "
				"failed\n", ioc->name);
		return -ENOMEM;
	}

	for (i = 0; i < ioc->scsiio_depth; i++) {
		for (j = ioc->chains_per_prp_buffer;
		    j < ioc->chains_needed_per_io; j++) {
			ctr = &ioc->chain_lookup[i].chains_per_smid[j];
			ctr->chain_buffer = dma_pool_alloc(ioc->chain_dma_pool,
			    GFP_KERNEL, &ctr->chain_buffer_dma);
			if (!ctr->chain_buffer)
				return -EAGAIN;
			if (!mpt3sas_check_same_4gb_region(
				(long)ctr->chain_buffer, ioc->chain_segment_sz)) {
				printk(MPT3SAS_ERR_FMT
				    "Chain buffers are not in same 4G !!!"
				    "Chain buff (0x%p) dma = (0x%llx)\n",
				    ioc->name, ctr->chain_buffer,
				    (unsigned long long)ctr->chain_buffer_dma);
				    ioc->use_32bit_dma = 1;
					return -EAGAIN;
			}
		}
	}
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "chain_lookup depth"
	    "(%d), frame_size(%d), pool_size(%d kB)\n", ioc->name,
	    ioc->scsiio_depth, ioc->chain_segment_sz, ((ioc->scsiio_depth *
	    (ioc->chains_needed_per_io - ioc->chains_per_prp_buffer) *
	    ioc->chain_segment_sz))/1024));
	return 0;
}

/**
 * _base_allocate_sense_dma_pool - Allocating DMA'able memory
 *			for sense dma pool.
 * @ioc: Adapter object
 * @sz: DMA Pool size
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_allocate_sense_dma_pool(struct MPT3SAS_ADAPTER *ioc, int sz)
{
	
	ioc->sense_dma_pool =
	    dma_pool_create("sense pool", &ioc->pdev->dev, sz, 4, 0);
	if (!ioc->sense_dma_pool) {
		printk(MPT3SAS_ERR_FMT "sense pool: dma_pool_create failed\n",
				ioc->name);
		return -ENOMEM;
	}
	ioc->sense = dma_pool_alloc(ioc->sense_dma_pool,
	    GFP_KERNEL, &ioc->sense_dma);
	if (!ioc->sense) {
		printk(MPT3SAS_ERR_FMT "sense pool: dma_pool_alloc failed\n",
		    ioc->name);
		return -EAGAIN; 
	}
	/* sense buffer requires to be in same 4 gb region.
	 * Below function will check the same.
	 * In case of failure, new pci pool will be created with
	 * updated alignment.
	 * Older allocation and pool will be destroyed.
	 * Alignment will be used such a way that next allocation if success,
	 * will always meet same 4gb region requirement.
	 * Actual requirement is not alignment, but we need start and end of
	 * DMA address must have same upper 32 bit address.
	 */
	if (!mpt3sas_check_same_4gb_region((long)ioc->sense, sz)) {
		dinitprintk(ioc,
			pr_err("Bad Sense Pool! sense (0x%p)"
			    "sense_dma = (0x%llx)\n",
			    ioc->sense,
			    (unsigned long long) ioc->sense_dma));
		ioc->use_32bit_dma = 1;
		return -EAGAIN;
	}
	printk(MPT3SAS_INFO_FMT
	    "sense pool(0x%p) - dma(0x%llx): depth(%d), element_size(%d), pool_size (%d kB)\n",
	    ioc->name, ioc->sense, (unsigned long long)ioc->sense_dma,
	    ioc->scsiio_depth, SCSI_SENSE_BUFFERSIZE, sz/1024);
	return 0;
}

/**
 * _base_allocate_reply_free_dma_pool - Allocating DMA'able memory
 *			for reply free dma pool.
 * @ioc: Adapter object
 * @sz: DMA Pool size
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_allocate_reply_free_dma_pool(struct MPT3SAS_ADAPTER *ioc, int sz)
{
	/* reply free queue, 16 byte align */
	ioc->reply_free_dma_pool = dma_pool_create(
	    "reply_free pool", &ioc->pdev->dev, sz, 16, 0);
	if (!ioc->reply_free_dma_pool) {
		printk(MPT3SAS_ERR_FMT "reply_free pool: dma_pool_create "
		    "failed\n", ioc->name);
		return -ENOMEM;
	}
	ioc->reply_free = dma_pool_alloc(ioc->reply_free_dma_pool,
	    GFP_KERNEL, &ioc->reply_free_dma);
	if (!ioc->reply_free) {
		printk(MPT3SAS_ERR_FMT "reply_free pool: dma_pool_alloc "
		    "failed\n", ioc->name);
		return -EAGAIN;
	}
	if (!mpt3sas_check_same_4gb_region((long)ioc->reply_free, sz)) {
		dinitprintk(ioc,
			pr_err("Bad Reply Free Pool! Reply Free (0x%p)"
			    "Reply Free dma = (0x%llx)\n",
			    ioc->reply_free,
			    (unsigned long long) ioc->reply_free_dma));
		ioc->use_32bit_dma = 1;
		return -EAGAIN;
	}
	memset(ioc->reply_free, 0, sz);
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "reply_free pool(0x%p): "
	    "depth(%d), element_size(%d), pool_size(%d kB)\n", ioc->name,
	    ioc->reply_free, ioc->reply_free_queue_depth, 4, sz/1024));
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "reply_free_dma"
	    "(0x%llx)\n", ioc->name, (unsigned long long)ioc->reply_free_dma));
	return 0;
}

/**
 * _base_allocate_reply_pool - Allocating DMA'able memory
 *			for reply pool.
 * @ioc: Adapter object
 * @sz: DMA Pool size
 * Return: 0 for success, non-zero for failure.
 */
static int
_base_allocate_reply_pool(struct MPT3SAS_ADAPTER *ioc, int sz)
{
	/* reply pool, 4 byte align */
	ioc->reply_dma_pool = dma_pool_create("reply pool",
			&ioc->pdev->dev, sz, 4, 0);
	if (!ioc->reply_dma_pool) {
		printk(MPT3SAS_ERR_FMT "reply pool: dma_pool_create failed\n",
		    ioc->name);
		return -ENOMEM;
	}
	ioc->reply = dma_pool_alloc(ioc->reply_dma_pool, GFP_KERNEL,
	    &ioc->reply_dma);
	if (!ioc->reply) {
		printk(MPT3SAS_ERR_FMT "reply pool: dma_pool_alloc failed\n",
		    ioc->name);
		return -EAGAIN;
	}
	if (!mpt3sas_check_same_4gb_region((long)ioc->reply_free, sz)) {
		dinitprintk(ioc,
			pr_err("Bad Reply Pool! Reply (0x%p)"
			    "Reply dma = (0x%llx)\n",
			    ioc->reply,
			    (unsigned long long) ioc->reply_dma));
		ioc->use_32bit_dma = 1;
		return -EAGAIN;
	}
	ioc->reply_dma_min_address = (u32)(ioc->reply_dma);
	ioc->reply_dma_max_address = (u32)(ioc->reply_dma) + sz;
	printk(MPT3SAS_INFO_FMT
	    "reply pool(0x%p) - dma(0x%llx): depth(%d)"
	    "frame_size(%d), pool_size(%d kB)\n",
	    ioc->name, ioc->reply, (unsigned long long)ioc->reply_dma,
	    ioc->reply_free_queue_depth, ioc->reply_sz, sz/1024);
	return 0;
}

/**
 * _base_allocate_memory_pools - allocate start of day memory pools
 * @ioc: per adapter object
 *
 * Returns 0 success, anything else error
 */
static int
_base_allocate_memory_pools(struct MPT3SAS_ADAPTER *ioc)
{
	struct mpt3sas_facts *facts;
	u16 max_sge_elements;
	u16 chains_needed_per_io;
	u32 sz, total_sz, reply_post_free_sz, rc=0;
	u32 retry_sz;
	u32 rdpq_sz = 0, sense_sz = 0, reply_post_free_array_sz = 0;
	u32 sgl_sz = 0;
	u16 max_request_credit, nvme_blocks_needed;
	unsigned short sg_tablesize;
	u16 sge_size;
#if defined(TARGET_MODE)
	int num_cmd_buffers;
#endif
	int i = 0;
	struct chain_tracker *ct;
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

#if defined(TARGET_MODE)
	num_cmd_buffers = min_t(int, NUM_CMD_BUFFERS,
	    ioc->pfacts[0].MaxPostedCmdBuffers);
#endif

	retry_sz = 0;
	facts = &ioc->facts;
	/* command line tunables for max sgl entries */
	if (max_sgl_entries != -1) 
		sg_tablesize = max_sgl_entries;
	else {
		if (ioc->hba_mpi_version_belonged == MPI2_VERSION)
			sg_tablesize = MPT2SAS_SG_DEPTH;
		else
			sg_tablesize = MPT3SAS_SG_DEPTH;
	}
	/* max sgl entries <= MPT_KDUMP_MIN_PHYS_SEGMENTS in KDUMP mode */
	if (reset_devices)
		sg_tablesize = min_t(unsigned short, sg_tablesize,
					MPT_KDUMP_MIN_PHYS_SEGMENTS);
	if (sg_tablesize < MPT_MIN_PHYS_SEGMENTS)
		sg_tablesize = MPT_MIN_PHYS_SEGMENTS;
	else if (sg_tablesize > MPT_MAX_PHYS_SEGMENTS) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25))
		sg_tablesize = min_t(unsigned short, sg_tablesize,
					MPT_MAX_SG_SEGMENTS);
		printk(MPT3SAS_WARN_FMT
			"sg_tablesize(%u) is bigger than kernel"
			" defined %s(%u)\n", ioc->name,
			sg_tablesize, MPT_MAX_PHYS_SEGMENTS_STRING, MPT_MAX_PHYS_SEGMENTS);
#else
		sg_tablesize = MPT_MAX_PHYS_SEGMENTS;
#endif
	}

	if(ioc->is_mcpu_endpoint)
		ioc->shost->sg_tablesize = MPT_MIN_PHYS_SEGMENTS;
	else
		ioc->shost->sg_tablesize = sg_tablesize;

#if defined(TARGET_MODE)
	/* allocating 5 extra mf's */
	ioc->internal_depth = min_t(int,
	     (max_t(int, facts->HighPriorityCredit, num_cmd_buffers) + (5)),
	     (facts->RequestCredit / 4));
#else
	ioc->internal_depth = min_t(int, (facts->HighPriorityCredit + (5)),
		(facts->RequestCredit / 4));
#endif
	if (ioc->internal_depth < INTERNAL_CMDS_COUNT) {
		if (facts->RequestCredit <= (INTERNAL_CMDS_COUNT +
		    INTERNAL_SCSIIO_CMDS_COUNT)) {
			printk(MPT3SAS_ERR_FMT "IOC doesn't have enough"
			 " RequestCredits, it has just %d number of credits\n",
			 ioc->name, facts->RequestCredit);
			return -ENOMEM;
		 }
		ioc->internal_depth = 10;
	}

	ioc->hi_priority_depth = ioc->internal_depth - (5);
	
	/* command line tunables for max controller queue depth */
	if (max_queue_depth != -1 && max_queue_depth != 0) {
		max_request_credit = min_t(u16, max_queue_depth +
		    ioc->internal_depth, facts->RequestCredit);
		if (max_request_credit > MAX_HBA_QUEUE_DEPTH)
			max_request_credit =  MAX_HBA_QUEUE_DEPTH;
	} 
	else if (reset_devices) 
		max_request_credit = min_t(u16, facts->RequestCredit,
			(MPT3SAS_KDUMP_SCSI_IO_DEPTH + ioc->internal_depth));
	else
		max_request_credit = min_t(u16, facts->RequestCredit,
		    MAX_HBA_QUEUE_DEPTH);

retry:
	/* Firmware maintains additional facts->HighPriorityCredit number of
	 * credits for HiPriprity Request messages, so hba queue depth will be
	 * sum of max_request_credit and high priority queue depth.
	 */
	ioc->hba_queue_depth = max_request_credit + ioc->hi_priority_depth;

	/* request frame size */
	ioc->request_sz = facts->IOCRequestFrameSize * 4;

	/* reply frame size */
	ioc->reply_sz = facts->ReplyFrameSize * 4;
	/* chain segment size */
	if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
		if (facts->IOCMaxChainSegmentSize)
			ioc->chain_segment_sz = facts->IOCMaxChainSegmentSize * MAX_CHAIN_ELEMT_SZ;
		else
			/* set to 128 bytes size if IOCMaxChainSegmentSize is zero */
			ioc->chain_segment_sz = DEFAULT_NUM_FWCHAIN_ELEMTS * MAX_CHAIN_ELEMT_SZ;
	}
	else 
       		ioc->chain_segment_sz = ioc->request_sz;

	/* calculate the max scatter element size */
	sge_size = max_t(u16, ioc->sge_size, ioc->sge_size_ieee);

 retry_allocation:
	total_sz = 0;
	/* calculate number of sg elements left over in the 1st frame */
	if (ioc->hba_mpi_version_belonged == MPI2_VERSION) {
		max_sge_elements = ioc->request_sz - ((sizeof(Mpi2SCSIIORequest_t) -
	    		sizeof(Mpi2SGEIOUnion_t)) + ioc->sge_size);
	}
	else {
		/* reserve 2 SGE's, one for chain SGE and
		 * anther for SGL1 (i.e. for meta data)
		 */
		max_sge_elements = ioc->request_sz -
		 ((sizeof(Mpi25SCSIIORequest_t) -
		   sizeof(Mpi25SGEIOUnion_t)) + 2 * sge_size);
	}
	ioc->max_sges_in_main_message = max_sge_elements/sge_size;

	/* now do the same for a chain buffer */
	max_sge_elements = ioc->chain_segment_sz - sge_size;
	ioc->max_sges_in_chain_message = max_sge_elements/sge_size;

	/*
	 *  MPT3SAS_SG_DEPTH = CONFIG_FUSION_MAX_SGE
	 */
	chains_needed_per_io = ((ioc->shost->sg_tablesize -
	   ioc->max_sges_in_main_message)/ioc->max_sges_in_chain_message)
	    + 1;
	if (chains_needed_per_io > facts->MaxChainDepth) {
		chains_needed_per_io = facts->MaxChainDepth;
		ioc->shost->sg_tablesize = min_t(u16,
		ioc->max_sges_in_main_message + (ioc->max_sges_in_chain_message
		* chains_needed_per_io), ioc->shost->sg_tablesize);
	}
	
	/* Double the chains if DIX support is enabled for Meta data SGLs*/
	if ((prot_mask & 0x78) && ioc->hba_mpi_version_belonged != MPI2_VERSION)
		ioc->chains_needed_per_io = chains_needed_per_io * 2;
	else
		ioc->chains_needed_per_io = chains_needed_per_io;

	/* reply free queue sizing - taking into account for 64 FW events */
	ioc->reply_free_queue_depth = ioc->hba_queue_depth + 64;

	/* mCPU manage single counters for simplicity */
	if(ioc->is_mcpu_endpoint)
		ioc->reply_post_queue_depth = ioc->reply_free_queue_depth;
	else {
		/* calculate reply descriptor post queue depth */
		ioc->reply_post_queue_depth = ioc->hba_queue_depth +
							ioc->reply_free_queue_depth +  1 ;
		/* align the reply post queue on the next 16 count boundary */
		if (ioc->reply_post_queue_depth % 16)
			ioc->reply_post_queue_depth += 16 - (ioc->reply_post_queue_depth % 16);
	}


	if (ioc->reply_post_queue_depth >
	    facts->MaxReplyDescriptorPostQueueDepth) {
		ioc->reply_post_queue_depth = facts->MaxReplyDescriptorPostQueueDepth -
		    (facts->MaxReplyDescriptorPostQueueDepth % 16);
		ioc->hba_queue_depth = ((ioc->reply_post_queue_depth - 64) / 2) -1;
		ioc->reply_free_queue_depth = ioc->hba_queue_depth + 64;
	}

	printk(MPT3SAS_INFO_FMT "scatter gather: "
	    "sge_in_main_msg(%d), sge_per_chain(%d), sge_per_io(%d), "
	    "chains_per_io(%d)\n", ioc->name, ioc->max_sges_in_main_message,
	    ioc->max_sges_in_chain_message, ioc->shost->sg_tablesize,
	    ioc->chains_needed_per_io);

	ioc->scsiio_depth = ioc->hba_queue_depth -
	    ioc->hi_priority_depth - ioc->internal_depth;

	/* set the scsi host can_queue depth
	 * with some internal commands that could be outstanding
	 */
#if defined(TARGET_MODE)
	/* allocating 2 extra mf's */
	ioc->shost->can_queue = ioc->scsiio_depth -
				(num_cmd_buffers + INTERNAL_SCSIIO_CMDS_COUNT);
#else
	ioc->shost->can_queue = ioc->scsiio_depth - INTERNAL_SCSIIO_CMDS_COUNT;
#endif
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "scsi host: "
	    "can_queue depth (%d)\n", ioc->name, ioc->shost->can_queue));
	
	/* contiguous pool for request and chains, 16 byte align, one extra "
	 * "frame for smid=0
	 */
	sz = ((ioc->scsiio_depth + 1) * ioc->request_sz);

	/* hi-priority queue */
	sz += (ioc->hi_priority_depth * ioc->request_sz);

	/* internal queue */
	sz += (ioc->internal_depth * ioc->request_sz);

	ioc->request_dma_sz = sz;
	ioc->request = dma_alloc_coherent(&ioc->pdev->dev, sz,
			&ioc->request_dma, GFP_KERNEL);
	if (!ioc->request) {
		printk(MPT3SAS_ERR_FMT "request pool: dma_alloc_consistent "
		    "failed: hba_depth(%d), chains_per_io(%d), frame_sz(%d), "
		    "total(%d kB)\n", ioc->name, ioc->hba_queue_depth,
		    ioc->chains_needed_per_io, ioc->request_sz, sz/1024);
		if (ioc->scsiio_depth < MPT3SAS_SAS_QUEUE_DEPTH) {
			rc = -ENOMEM;
			goto out;
		}
		retry_sz = 64;
		if((ioc->hba_queue_depth - retry_sz) >
		   (ioc->internal_depth + INTERNAL_SCSIIO_CMDS_COUNT)) {
			ioc->hba_queue_depth -= retry_sz;
			goto retry_allocation;
		}
		else {
			rc = -ENOMEM;
			goto out;
		}
	}
	memset(ioc->request, 0, sz);

	if (retry_sz)
		printk(MPT3SAS_ERR_FMT "request pool: dma_alloc_consistent "
		    "succeed: hba_depth(%d), chains_per_io(%d), frame_sz(%d), "
		    "total(%d kb)\n", ioc->name, ioc->hba_queue_depth,
		    ioc->chains_needed_per_io, ioc->request_sz, sz/1024);

	/* hi-priority queue */
	ioc->hi_priority = ioc->request + ((ioc->scsiio_depth + 1) *
	    ioc->request_sz);
	ioc->hi_priority_dma = ioc->request_dma + ((ioc->scsiio_depth + 1) *
	    ioc->request_sz);

	/* internal queue */
	ioc->internal = ioc->hi_priority + (ioc->hi_priority_depth *
	    ioc->request_sz);
	ioc->internal_dma = ioc->hi_priority_dma + (ioc->hi_priority_depth *
	    ioc->request_sz);

	printk(MPT3SAS_INFO_FMT "request pool(0x%p) - dma(0x%llx): "
	    "depth(%d), frame_size(%d), pool_size(%d kB)\n", ioc->name,
	    ioc->request, (unsigned long long) ioc->request_dma,
	    ioc->hba_queue_depth, ioc->request_sz,
	    (ioc->hba_queue_depth * ioc->request_sz)/1024);

	total_sz += sz;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0))
	sz = ioc->scsiio_depth * sizeof(struct scsiio_tracker);
	ioc->scsi_lookup_pages = get_order(sz);
	ioc->scsi_lookup = (struct scsiio_tracker *)__get_free_pages(
	    GFP_KERNEL, ioc->scsi_lookup_pages);
	if (!ioc->scsi_lookup) {
		// Retry allocating memory by reducing the queue depth
		if ((max_request_credit - 64) >
		    (ioc->internal_depth + INTERNAL_SCSIIO_CMDS_COUNT)) {
			max_request_credit -= 64;
			if (ioc->request) {
				dma_free_coherent(&ioc->pdev->dev, ioc->request_dma_sz,
		    		    ioc->request,  ioc->request_dma);
				ioc->request = NULL;
			}
			goto retry;
		}
		else {
			printk(MPT3SAS_ERR_FMT "scsi_lookup: get_free_pages failed, "
			    "sz(%d)\n", ioc->name, (int)sz);
			rc = -ENOMEM;
			goto out;
		}
	}
#endif 
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "scsiio(0x%p): "
	    "depth(%d)\n", ioc->name, ioc->request,
	    ioc->scsiio_depth));

	/* initialize hi-priority queue smid's */
	ioc->hpr_lookup = kcalloc(ioc->hi_priority_depth,
	    sizeof(struct request_tracker), GFP_KERNEL);
	if (!ioc->hpr_lookup) {
		printk(MPT3SAS_ERR_FMT "hpr_lookup: kcalloc failed\n",
		    ioc->name);
		rc = -ENOMEM;
		goto out;
	}
	ioc->hi_priority_smid = ioc->scsiio_depth + 1;
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "hi_priority(0x%p): "
	    "depth(%d), start smid(%d)\n", ioc->name, ioc->hi_priority,
	    ioc->hi_priority_depth, ioc->hi_priority_smid));

	/* initialize internal queue smid's */
	ioc->internal_lookup = kcalloc(ioc->internal_depth,
	    sizeof(struct request_tracker), GFP_KERNEL);
	if (!ioc->internal_lookup) {
		printk(MPT3SAS_ERR_FMT "internal_lookup: kcalloc failed\n",
		    ioc->name);
		rc = -ENOMEM;
		goto out;
	}
	ioc->internal_smid = ioc->hi_priority_smid + ioc->hi_priority_depth;
	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "internal(0x%p): "
	    "depth(%d), start smid(%d)\n", ioc->name, ioc->internal,
	     ioc->internal_depth, ioc->internal_smid));

	sz = ioc->scsiio_depth * sizeof(struct chain_lookup);

	ioc->chain_lookup = kzalloc(sz, GFP_KERNEL);
	if (!ioc->chain_lookup) {
		// Retry allocating memory by reducing the queue depth
		if ((max_request_credit - 64) >
		    (ioc->internal_depth + INTERNAL_SCSIIO_CMDS_COUNT)) {
			max_request_credit -= 64;
			_base_release_memory_pools(ioc);
			goto retry;
		}
		else {
			printk(MPT3SAS_ERR_FMT "chain_lookup: __get_free_pages "
			"failed\n", ioc->name);
			rc = -ENOMEM;
			goto out;
		}
	}
	
	sz = ioc->chains_needed_per_io * sizeof(struct chain_tracker);
	for (i=0; i < ioc->scsiio_depth; i++) {
		ioc->chain_lookup[i].chains_per_smid = kzalloc(sz, GFP_KERNEL);
		if (!ioc->chain_lookup[i].chains_per_smid) {
			// Retry allocating memory by reducing the queue depth
			if ((max_request_credit - 64) >
			    (ioc->internal_depth + INTERNAL_SCSIIO_CMDS_COUNT)) {
				max_request_credit -= 64;
				_base_release_memory_pools(ioc);
				goto retry;
			}
			else {
				printk(MPT3SAS_ERR_FMT "chain_lookup: "
				" kzalloc failed\n", ioc->name);
				rc = -ENOMEM;
				goto out;
			}
		}
	}

	/*
	* The number of NVMe page sized blocks needed is: 
	*     (((sg_tablesize * 8) - 1) / (page_size - 8)) + 1 
	* ((sg_tablesize * 8) - 1) is the max PRP's minus the first PRP entry 
	* that is placed in the main message frame.  8 is the size of each PRP 
	* entry or PRP list pointer entry.  8 is subtracted from page_size 
	* because of the PRP list pointer entry at the end of a page, so this 
	* is not counted as a PRP entry.  The 1 added page is a round up. 
	* 
	* To avoid allocation failures due to the amount of memory that could 
	* be required for NVMe PRP's, only each set of NVMe blocks will be 
	* contiguous, so a new set is allocated for each possible I/O. 
	*/
	ioc->chains_per_prp_buffer = 0;
	if(ioc->facts.ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_NVME_DEVICES) {
		nvme_blocks_needed = (ioc->shost->sg_tablesize * NVME_PRP_SIZE) - 1;
		nvme_blocks_needed /= (ioc->page_size - NVME_PRP_SIZE);
		nvme_blocks_needed++;

		sz = sizeof(struct pcie_sg_list) * ioc->scsiio_depth;
		ioc->pcie_sg_lookup = kzalloc(sz, GFP_KERNEL);
		if (!ioc->pcie_sg_lookup) {
			printk(MPT3SAS_ERR_FMT "PCIe SGL lookup: kzalloc "
						"failed\n", ioc->name);
			rc = -ENOMEM;
			goto out;
		}
		sgl_sz = nvme_blocks_needed * ioc->page_size;
		if ((rc = _base_allocate_pcie_sgl_pool(ioc, sgl_sz, ct)) == -ENOMEM)
			return -ENOMEM;
		else if (rc == -EAGAIN)
			goto try_32bit_dma;
		total_sz += sgl_sz * ioc->scsiio_depth;
	}

	rc = _base_allocate_chain_dma_pool(ioc, ioc->chain_segment_sz, ct);
	if (rc == -ENOMEM)
		return -ENOMEM;
	else if (rc == -EAGAIN) {
		if (ioc->use_32bit_dma && ioc->dma_mask > 32)
			goto try_32bit_dma;
		else {
			if ((max_request_credit - 64) >
			    (ioc->internal_depth + INTERNAL_SCSIIO_CMDS_COUNT)) {
				max_request_credit -= 64;
				_base_release_memory_pools(ioc);
				goto retry_allocation;
			} else {
				printk(MPT3SAS_ERR_FMT "chain_lookup: "
						" dma_pool_alloc failed\n", ioc->name);
				return -ENOMEM;
			}
		}
	}
	total_sz += ioc->chain_segment_sz *
	    ((ioc->chains_needed_per_io - ioc->chains_per_prp_buffer) *
	    ioc->scsiio_depth);

	/* sense buffers, 4 byte align */
	sense_sz = ioc->scsiio_depth * SCSI_SENSE_BUFFERSIZE ;
	if ((rc = _base_allocate_sense_dma_pool(ioc, sense_sz)) == -ENOMEM)
		return -ENOMEM;
	else if (rc == -EAGAIN)
		goto try_32bit_dma;
	total_sz += sense_sz;
	/* reply pool, 4 byte align */
	sz = ioc->reply_free_queue_depth * ioc->reply_sz;
	if ((rc = _base_allocate_reply_pool(ioc, sz)) == -ENOMEM)
		return -ENOMEM;
	else if (rc == -EAGAIN)
		goto try_32bit_dma;
	total_sz += sz;

	/* reply free queue, 16 byte align */
	sz = ioc->reply_free_queue_depth * 4;
	if ((rc = _base_allocate_reply_free_dma_pool(ioc, sz)) == -ENOMEM)
		return -ENOMEM;
	else if (rc == -EAGAIN)
		goto try_32bit_dma;
	total_sz += sz;
	/* reply post queue, 16 byte align */
	reply_post_free_sz = ioc->reply_post_queue_depth *
	    sizeof(Mpi2DefaultReplyDescriptor_t);
	rdpq_sz = reply_post_free_sz * RDPQ_MAX_INDEX_IN_ONE_CHUNK;
	if (_base_is_controller_msix_enabled(ioc) && !ioc->rdpq_array_enable)
	rdpq_sz = reply_post_free_sz * ioc->reply_queue_count;
	if ((rc = base_alloc_rdpq_dma_pool(ioc, rdpq_sz)) == -ENOMEM)
		return -ENOMEM;
	else if (rc == -EAGAIN)
		goto try_32bit_dma;
	else {
		if (ioc->rdpq_array_enable && rc == 0) {
			reply_post_free_array_sz = ioc->reply_queue_count *
			    sizeof(Mpi2IOCInitRDPQArrayEntry);
			if ((rc = _base_allocate_reply_post_free_array(ioc,
			    reply_post_free_array_sz)) == -ENOMEM)
				return -ENOMEM;
			else if (rc == -EAGAIN)
				goto try_32bit_dma;
		}
	}
	total_sz += rdpq_sz;
	ioc->config_page_sz = 512;
	ioc->config_page = dma_alloc_coherent(&ioc->pdev->dev,
		ioc->config_page_sz, &ioc->config_page_dma, GFP_KERNEL);
	if (!ioc->config_page) {
		printk(MPT3SAS_ERR_FMT "config page: dma_pool_alloc "
		    "failed\n", ioc->name);
		rc = -ENOMEM;
		goto out;
	}
	printk(MPT3SAS_INFO_FMT "config page(0x%p) - dma(0x%llx): size(%d)\n",
	    ioc->name, ioc->config_page,
	    (unsigned long long)ioc->config_page_dma, ioc->config_page_sz);
	total_sz += ioc->config_page_sz;

	printk(MPT3SAS_INFO_FMT "Allocated physical memory: size(%d kB)\n",
	    ioc->name, total_sz/1024);
	printk(MPT3SAS_INFO_FMT "Current Controller Queue Depth(%d), "
	    "Max Controller Queue Depth(%d)\n",
	    ioc->name, ioc->shost->can_queue, facts->RequestCredit);

	return 0;

try_32bit_dma:
	_base_release_memory_pools(ioc);
	if (ioc->use_32bit_dma && (ioc->dma_mask > 32)) {
	/* Change dma coherent mask to 32 bit and reallocate */
		if (_base_config_dma_addressing(ioc, ioc->pdev) != 0) {
			pr_err("Setting 32 bit coherent DMA mask Failed %s\n",
				pci_name(ioc->pdev));
		return -ENODEV;
		}
	} else if (_base_reduce_hba_queue_depth(ioc) !=0)
		return -ENOMEM;
	goto retry_allocation;

 out:
	return rc;
}

/**
 *_base_flush_ios_and_panic - Flush the IOs and panic
 * @ioc: Pointer to MPT_ADAPTER structure
 *
 * Return nothing.
 */
void
_base_flush_ios_and_panic(struct MPT3SAS_ADAPTER *ioc, u16 fault_code)
{
	ioc->adapter_over_temp = 1;
	mpt3sas_base_stop_smart_polling(ioc);
	mpt3sas_base_stop_watchdog(ioc);
	mpt3sas_base_stop_hba_unplug_watchdog(ioc);
	mpt3sas_scsih_flush_running_cmds(ioc);
	mpt3sas_print_fault_code(ioc, fault_code);
}

/**
 * mpt3sas_base_get_iocstate - Get the current state of a MPT adapter.
 * @ioc: Pointer to MPT_ADAPTER structure
 * @cooked: Request raw or cooked IOC state
 *
 * Returns all IOC Doorbell register bits if cooked==0, else just the
 * Doorbell bits in MPI_IOC_STATE_MASK.
 */
u32
mpt3sas_base_get_iocstate(struct MPT3SAS_ADAPTER *ioc, int cooked)
{
	u32 s, sc;

	s = ioc->base_readl(&ioc->chip->Doorbell);
	sc = s & MPI2_IOC_STATE_MASK;
	if ((ioc->hba_mpi_version_belonged != MPI2_VERSION) &&
	    (sc != MPI2_IOC_STATE_MASK)) {
		if ((sc == MPI2_IOC_STATE_FAULT) &&
		    ((s & MPI2_DOORBELL_DATA_MASK) ==
		     IFAULT_IOP_OVER_TEMP_THRESHOLD_EXCEEDED)) {
			_base_flush_ios_and_panic(ioc, s & MPI2_DOORBELL_DATA_MASK);
			panic("TEMPERATURE FAULT: STOPPING; panic in %s\n", __func__);
		}
	}
	return cooked ? sc : s;
}
#if defined(TARGET_MODE)
EXPORT_SYMBOL(mpt3sas_base_get_iocstate);
#endif

/**
 * _base_send_ioc_reset - send doorbell reset
 * @ioc: per adapter object
 * @reset_type: currently only supports: MPI2_FUNCTION_IOC_MESSAGE_UNIT_RESET
 * @timeout: timeout in second
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_send_ioc_reset(struct MPT3SAS_ADAPTER *ioc, u8 reset_type, int timeout)
{
	u32 ioc_state;
	int r = 0;
	unsigned long flags;

	if (reset_type != MPI2_FUNCTION_IOC_MESSAGE_UNIT_RESET) {
		printk(MPT3SAS_ERR_FMT "%s: unknown reset_type\n",
		    ioc->name, __func__);
		return -EFAULT;
	}

	if (!(ioc->facts.IOCCapabilities &
	   MPI2_IOCFACTS_CAPABILITY_EVENT_REPLAY))
		return -EFAULT;

	printk(MPT3SAS_INFO_FMT "sending message unit reset !!\n", ioc->name);

	writel(reset_type << MPI2_DOORBELL_FUNCTION_SHIFT,
	    &ioc->chip->Doorbell);
	if ((_base_wait_for_doorbell_ack(ioc, 15)))
		r = -EFAULT;
	ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_COREDUMP
		&& (ioc->is_driver_loading == 1 || ioc->fault_reset_work_q == NULL)) {
		spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
		mpt3sas_base_coredump_info(ioc, ioc_state);
		mpt3sas_base_wait_for_coredump_completion(ioc, __func__);
		r = -EFAULT;
		goto out;
	}
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
	if (r != 0) // doorbell did not ACK
		goto out;
	ioc_state = _base_wait_on_iocstate(ioc, MPI2_IOC_STATE_READY,
	    timeout);
	if (ioc_state) {
		printk(MPT3SAS_ERR_FMT "%s: failed going to ready state "
		    " (ioc_state=0x%x)\n", ioc->name, __func__, ioc_state);
		r = -EFAULT;
		goto out;
	}
 out:
	printk(MPT3SAS_INFO_FMT "message unit reset: %s\n",
	    ioc->name, ((r == 0) ? "SUCCESS" : "FAILED"));
	return r;
}

int
mpt3sas_wait_for_ioc_to_operational(struct MPT3SAS_ADAPTER *ioc,
	int wait_count)
{
	int wait_state_count = 0;
	u32 ioc_state;

	if (mpt3sas_base_pci_device_is_unplugged(ioc))
		return -EFAULT;

	ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
	while (ioc_state != MPI2_IOC_STATE_OPERATIONAL) {

		if (mpt3sas_base_pci_device_is_unplugged(ioc))
			return -EFAULT;

		if (wait_state_count++ == wait_count) {
			printk(MPT3SAS_ERR_FMT
			    "%s: failed due to ioc not operational\n",
			    ioc->name, __func__);
			return -EFAULT;
		}
		ssleep(1);
		ioc_state = mpt3sas_base_get_iocstate(ioc, 1);
		printk(MPT3SAS_INFO_FMT "%s: waiting for "
		    "operational state(count=%d)\n", ioc->name,
		    __func__, wait_state_count);
	}
	if (wait_state_count)
		printk(MPT3SAS_INFO_FMT "%s: ioc is operational\n",
		    ioc->name, __func__);

	return 0;	
}

/**
 * mpt3sas_base_sas_iounit_control - send sas iounit control to FW
 * @ioc: per adapter object
 * @mpi_reply: the reply payload from FW
 * @mpi_request: the request payload sent to FW
 *
 * The SAS IO Unit Control Request message allows the host to perform low-level
 * operations, such as resets on the PHYs of the IO Unit, also allows the host
 * to obtain the IOC assigned device handles for a device if it has other
 * identifying information about the device, in addition allows the host to
 * remove IOC resources associated with the device.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt3sas_base_sas_iounit_control(struct MPT3SAS_ADAPTER *ioc,
	Mpi2SasIoUnitControlReply_t *mpi_reply,
	Mpi2SasIoUnitControlRequest_t *mpi_request)
{
	u16 smid;
	u8 issue_reset;
	int rc;
	void *request;

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	mutex_lock(&ioc->base_cmds.mutex);

	if (ioc->base_cmds.status != MPT3_CMD_NOT_USED) {
		printk(MPT3SAS_ERR_FMT "%s: base_cmd in use\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = mpt3sas_wait_for_ioc_to_operational(ioc, 10);
	if (rc)
		goto out;

	smid = mpt3sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		printk(MPT3SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = 0;
	ioc->base_cmds.status = MPT3_CMD_PENDING;
	request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->base_cmds.smid = smid;
	memcpy(request, mpi_request, sizeof(Mpi2SasIoUnitControlRequest_t));
	if (mpi_request->Operation == MPI2_SAS_OP_PHY_HARD_RESET ||
	    mpi_request->Operation == MPI2_SAS_OP_PHY_LINK_RESET)
		ioc->ioc_link_reset_in_progress = 1;
	init_completion(&ioc->base_cmds.done);
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->base_cmds.done,
	    msecs_to_jiffies(10000));
	if ((mpi_request->Operation == MPI2_SAS_OP_PHY_HARD_RESET ||
	    mpi_request->Operation == MPI2_SAS_OP_PHY_LINK_RESET) &&
	    ioc->ioc_link_reset_in_progress)
		ioc->ioc_link_reset_in_progress = 0;
	if (!(ioc->base_cmds.status & MPT3_CMD_COMPLETE)) {
		mpt3sas_check_cmd_timeout(ioc,
		    ioc->base_cmds.status, mpi_request,
		    sizeof(Mpi2SasIoUnitControlRequest_t)/4, issue_reset);
		goto issue_host_reset;
	}
	if (ioc->base_cmds.status & MPT3_CMD_REPLY_VALID)
		memcpy(mpi_reply, ioc->base_cmds.reply,
		    sizeof(Mpi2SasIoUnitControlReply_t));
	else
		memset(mpi_reply, 0, sizeof(Mpi2SasIoUnitControlReply_t));
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
	goto out;

 issue_host_reset:
	if (issue_reset)
		mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
	rc = -EFAULT;
 out:
	mutex_unlock(&ioc->base_cmds.mutex);
	return rc;
}
#if defined(TARGET_MODE)
EXPORT_SYMBOL(mpt3sas_base_sas_iounit_control);
#endif

/**
 * mpt3sas_base_scsi_enclosure_processor - sending request to sep device
 * @ioc: per adapter object
 * @mpi_reply: the reply payload from FW
 * @mpi_request: the request payload sent to FW
 *
 * The SCSI Enclosure Processor request message causes the IOC to
 * communicate with SES devices to control LED status signals.
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt3sas_base_scsi_enclosure_processor(struct MPT3SAS_ADAPTER *ioc,
	Mpi2SepReply_t *mpi_reply, Mpi2SepRequest_t *mpi_request)
{
	u16 smid;
	u8 issue_reset;
	int rc;
	void *request;

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	mutex_lock(&ioc->base_cmds.mutex);

	if (ioc->base_cmds.status != MPT3_CMD_NOT_USED) {
		printk(MPT3SAS_ERR_FMT "%s: base_cmd in use\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = mpt3sas_wait_for_ioc_to_operational(ioc, 10);
	if (rc)
		goto out;

	smid = mpt3sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		printk(MPT3SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		rc = -EAGAIN;
		goto out;
	}

	rc = 0;
	ioc->base_cmds.status = MPT3_CMD_PENDING;
	request = mpt3sas_base_get_msg_frame(ioc, smid);
	memset(request, 0, ioc->request_sz);
	ioc->base_cmds.smid = smid;
	memcpy(request, mpi_request, sizeof(Mpi2SepRequest_t));
	init_completion(&ioc->base_cmds.done);
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->base_cmds.done,
	    msecs_to_jiffies(10000));
	if (!(ioc->base_cmds.status & MPT3_CMD_COMPLETE)) {
		mpt3sas_check_cmd_timeout(ioc,
		    ioc->base_cmds.status, mpi_request,
		    sizeof(Mpi2SepRequest_t)/4, issue_reset);
		goto issue_host_reset;
	}
	if (ioc->base_cmds.status & MPT3_CMD_REPLY_VALID)
		memcpy(mpi_reply, ioc->base_cmds.reply,
		    sizeof(Mpi2SepReply_t));
	else
		memset(mpi_reply, 0, sizeof(Mpi2SepReply_t));
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
	goto out;

 issue_host_reset:
	if (issue_reset)
		mpt3sas_base_hard_reset_handler(ioc, FORCE_BIG_HAMMER);
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
	rc = -EFAULT;
 out:
	mutex_unlock(&ioc->base_cmds.mutex);
	return rc;
}

/**
 * _base_get_port_facts - obtain port facts reply and save in ioc
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_get_port_facts(struct MPT3SAS_ADAPTER *ioc, int port)
{
	Mpi2PortFactsRequest_t mpi_request;
	Mpi2PortFactsReply_t mpi_reply;
	struct mpt3sas_port_facts *pfacts;
	int mpi_reply_sz, mpi_request_sz, r;

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	mpi_reply_sz = sizeof(Mpi2PortFactsReply_t);
	mpi_request_sz = sizeof(Mpi2PortFactsRequest_t);
	memset(&mpi_request, 0, mpi_request_sz);
	mpi_request.Function = MPI2_FUNCTION_PORT_FACTS;
	mpi_request.PortNumber = port;
	r = _base_handshake_req_reply_wait(ioc, mpi_request_sz,
	    (u32 *)&mpi_request, mpi_reply_sz, (u16 *)&mpi_reply, 5);

	if (r != 0) {
		printk(MPT3SAS_ERR_FMT "%s: handshake failed (r=%d)\n",
		    ioc->name, __func__, r);
		return r;
	}

	pfacts = &ioc->pfacts[port];
	memset(pfacts, 0, sizeof(struct mpt3sas_port_facts));
	pfacts->PortNumber = mpi_reply.PortNumber;
	pfacts->VP_ID = mpi_reply.VP_ID;
	pfacts->VF_ID = mpi_reply.VF_ID;
	pfacts->MaxPostedCmdBuffers =
	    le16_to_cpu(mpi_reply.MaxPostedCmdBuffers);

	return 0;
}

/**
 * _base_send_ioc_init - send ioc_init to firmware
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_send_ioc_init(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2IOCInitRequest_t mpi_request;
	Mpi2IOCInitReply_t mpi_reply;
	int i, r = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0))
	ktime_t current_time;
#else
	struct timeval current_time;
#endif
	u16 ioc_status;
	u32 reply_post_free_ary_sz;

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	memset(&mpi_request, 0, sizeof(Mpi2IOCInitRequest_t));
	mpi_request.Function = MPI2_FUNCTION_IOC_INIT;
	mpi_request.WhoInit = MPI2_WHOINIT_HOST_DRIVER;
	mpi_request.VF_ID = 0; /* TODO */
	mpi_request.VP_ID = 0;
	mpi_request.MsgVersion = cpu_to_le16(ioc->hba_mpi_version_belonged); 
	mpi_request.HeaderVersion = cpu_to_le16(MPI2_HEADER_VERSION);
	mpi_request.HostPageSize = MPT3SAS_HOST_PAGE_SIZE_4K;

	if (_base_is_controller_msix_enabled(ioc))
		mpi_request.HostMSIxVectors = ioc->reply_queue_count;
	mpi_request.SystemRequestFrameSize = cpu_to_le16(ioc->request_sz/4);
	mpi_request.ReplyDescriptorPostQueueDepth =
	    cpu_to_le16(ioc->reply_post_queue_depth);
	mpi_request.ReplyFreeQueueDepth =
	    cpu_to_le16(ioc->reply_free_queue_depth);

	mpi_request.SenseBufferAddressHigh =
	    cpu_to_le32((u64)ioc->sense_dma >> 32);
	mpi_request.SystemReplyAddressHigh =
	    cpu_to_le32((u64)ioc->reply_dma >> 32);
	mpi_request.SystemRequestFrameBaseAddress =
	    cpu_to_le64((u64)ioc->request_dma);
	mpi_request.ReplyFreeQueueAddress =
	    cpu_to_le64((u64)ioc->reply_free_dma);

	if (ioc->rdpq_array_enable) {
		reply_post_free_ary_sz = ioc->reply_queue_count *
		    sizeof(Mpi2IOCInitRDPQArrayEntry);
		memset(ioc->reply_post_free_array, 0, reply_post_free_ary_sz);
		for (i = 0; i < ioc->reply_queue_count; i++)
			ioc->reply_post_free_array[i].RDPQBaseAddress =
			    cpu_to_le64((u64)ioc->reply_post[i].reply_post_free_dma);
		mpi_request.MsgFlags = MPI2_IOCINIT_MSGFLAG_RDPQ_ARRAY_MODE;
		mpi_request.ReplyDescriptorPostQueueAddress =
		    cpu_to_le64((u64)ioc->reply_post_free_array_dma);
	} else {
		mpi_request.ReplyDescriptorPostQueueAddress =
		    cpu_to_le64((u64)ioc->reply_post[0].reply_post_free_dma);
	}

	/* CoreDump. Set the flag to enable CoreDump in the FW */
	mpi_request.ConfigurationFlags |= MPI26_IOCINIT_CFGFLAGS_COREDUMP_ENABLE;

	/* This time stamp specifies number of milliseconds
	 * since epoch ~ midnight January 1, 1970.
	 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0))
	current_time = ktime_get_real();
	mpi_request.TimeStamp = cpu_to_le64(ktime_to_ms(current_time));
#else
	do_gettimeofday(&current_time);
	mpi_request.TimeStamp = cpu_to_le64((u64)current_time.tv_sec * 1000 +
	    (current_time.tv_usec / 1000));
#endif

	if (ioc->logging_level & MPT_DEBUG_INIT) {
		__le32 *mfp;
		int i;

		mfp = (__le32 *)&mpi_request;
		printk(MPT3SAS_INFO_FMT "\toffset:data\n", ioc->name);
		for (i = 0; i < sizeof(Mpi2IOCInitRequest_t)/4; i++)
			printk(MPT3SAS_INFO_FMT "\t[0x%02x]:%08x\n",
			    ioc->name, i*4, le32_to_cpu(mfp[i]));
	}

	r = _base_handshake_req_reply_wait(ioc,
	    sizeof(Mpi2IOCInitRequest_t), (u32 *)&mpi_request,
	    sizeof(Mpi2IOCInitReply_t), (u16 *)&mpi_reply, 30);

	if (r != 0) {
		printk(MPT3SAS_ERR_FMT "%s: handshake failed (r=%d)\n",
		    ioc->name, __func__, r);
		return r;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS ||
	    mpi_reply.IOCLogInfo) {
		printk(MPT3SAS_ERR_FMT "%s: failed\n", ioc->name, __func__);
		r = -EIO;
	}
	
	/* Reset TimeSync Counter*/
	 ioc->timestamp_update_count = 0;

	return r;
}

/**
 * mpt3sas_port_enable_done - command completion routine for port enable
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
u8
mpt3sas_port_enable_done(struct MPT3SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
	u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;
	u16 ioc_status;

	if (ioc->port_enable_cmds.status == MPT3_CMD_NOT_USED)
		return 1;

	mpi_reply = mpt3sas_base_get_reply_virt_addr(ioc, reply);
	if (!mpi_reply)
		return 1;

	if (mpi_reply->Function != MPI2_FUNCTION_PORT_ENABLE)
		return 1;

	ioc->port_enable_cmds.status &= ~MPT3_CMD_PENDING;
	ioc->port_enable_cmds.status |= MPT3_CMD_COMPLETE;
	ioc->port_enable_cmds.status |= MPT3_CMD_REPLY_VALID;
	memcpy(ioc->port_enable_cmds.reply, mpi_reply, mpi_reply->MsgLength*4);
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
		ioc->port_enable_failed = 1;

	if (ioc->is_driver_loading) {
		if (ioc_status == MPI2_IOCSTATUS_SUCCESS) {
			mpt3sas_port_enable_complete(ioc);
			return 1;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
		} else {
			ioc->start_scan_failed = ioc_status;
			ioc->start_scan = 0;
			return 1;
#endif
		}
	}
	complete(&ioc->port_enable_cmds.done);
	return 1;
}

/**
 * _base_send_port_enable - send port_enable(discovery stuff) to firmware
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_send_port_enable(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2PortEnableRequest_t *mpi_request;
	Mpi2PortEnableReply_t *mpi_reply;
	int r = 0;
	u16 smid;
	u16 ioc_status;

	printk(MPT3SAS_INFO_FMT "sending port enable !!\n", ioc->name);

	if (ioc->port_enable_cmds.status & MPT3_CMD_PENDING) {
		printk(MPT3SAS_ERR_FMT "%s: internal command already in use\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->port_enable_cb_idx);
	if (!smid) {
		printk(MPT3SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}

	ioc->port_enable_cmds.status = MPT3_CMD_PENDING;
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->port_enable_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2PortEnableRequest_t));
	mpi_request->Function = MPI2_FUNCTION_PORT_ENABLE;

	init_completion(&ioc->port_enable_cmds.done);
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->port_enable_cmds.done,
	    300*HZ);
	if (!(ioc->port_enable_cmds.status & MPT3_CMD_COMPLETE)) {
		printk(MPT3SAS_ERR_FMT "%s: timeout\n",
			ioc->name, __func__);
		_debug_dump_mf(mpi_request,
			sizeof(Mpi2PortEnableRequest_t)/4);
		if (ioc->port_enable_cmds.status & MPT3_CMD_RESET)
			r = -EFAULT;
		else
			r = -ETIME;
		goto out;
	}

	mpi_reply = ioc->port_enable_cmds.reply;
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT3SAS_ERR_FMT "%s: failed with (ioc_status=0x%08x)\n",
		    ioc->name, __func__, ioc_status);
		r = -EFAULT;
		goto out;
	}

 out:
	ioc->port_enable_cmds.status = MPT3_CMD_NOT_USED;
	printk(MPT3SAS_INFO_FMT "port enable: %s\n", ioc->name, ((r == 0) ?
	    "SUCCESS" : "FAILED"));
	return r;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
/**
 * mpt3sas_port_enable - initiate firmware discovery (don't wait for reply)
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt3sas_port_enable(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2PortEnableRequest_t *mpi_request;
	u16 smid;

	printk(MPT3SAS_INFO_FMT "sending port enable !!\n", ioc->name);

	if (ioc->port_enable_cmds.status & MPT3_CMD_PENDING) {
		printk(MPT3SAS_ERR_FMT "%s: internal command already in use\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->port_enable_cb_idx);
	if (!smid) {
		printk(MPT3SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}

	ioc->port_enable_cmds.status = MPT3_CMD_PENDING;
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->port_enable_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2PortEnableRequest_t));
	mpi_request->Function = MPI2_FUNCTION_PORT_ENABLE;

	ioc->put_smid_default(ioc, smid);
	return 0;
}
#endif

/**
 * _base_determine_wait_on_discovery - desposition
 * @ioc: per adapter object
 *
 * Decide whether to wait on discovery to complete. Used to either
 * locate boot device, or report volumes ahead of physical devices.
 *
 * Returns 1 for wait, 0 for don't wait
 */
static int
_base_determine_wait_on_discovery(struct MPT3SAS_ADAPTER *ioc)
{
	/* We wait for discovery to complete if IR firmware is loaded.
	 * The sas topology events arrive before PD events, so we need time to
	 * turn on the bit in ioc->pd_handles to indicate PD
	 * Also, it maybe required to report Volumes ahead of physical
	 * devices when MPI2_IOCPAGE8_IRFLAGS_LOW_VOLUME_MAPPING is set.
	 */
	if (ioc->ir_firmware)
		return 1;

	/* if no Bios, then we don't need to wait */
	if (!ioc->bios_pg3.BiosVersion)
		return 0;

	/* Bios is present, then we drop down here.
	 *
	 * If there any entries in the Bios Page 2, then we wait
	 * for discovery to complete.
	 */

	/* Current Boot Device */
	if ((ioc->bios_pg2.CurrentBootDeviceForm &
	    MPI2_BIOSPAGE2_FORM_MASK) ==
	    MPI2_BIOSPAGE2_FORM_NO_DEVICE_SPECIFIED &&
	/* Request Boot Device */
	   (ioc->bios_pg2.ReqBootDeviceForm &
	    MPI2_BIOSPAGE2_FORM_MASK) ==
	    MPI2_BIOSPAGE2_FORM_NO_DEVICE_SPECIFIED &&
	/* Alternate Request Boot Device */
	   (ioc->bios_pg2.ReqAltBootDeviceForm &
	    MPI2_BIOSPAGE2_FORM_MASK) ==
	    MPI2_BIOSPAGE2_FORM_NO_DEVICE_SPECIFIED)
		return 0;

	return 1;
}

/**
 * _base_unmask_events - turn on notification for this event
 * @ioc: per adapter object
 * @event: firmware event
 *
 * The mask is stored in ioc->event_masks.
 */
static void
_base_unmask_events(struct MPT3SAS_ADAPTER *ioc, u16 event)
{
	u32 desired_event;

	if (event >= 128)
		return;

	desired_event = (1 << (event % 32));

	if (event < 32)
		ioc->event_masks[0] &= ~desired_event;
	else if (event < 64)
		ioc->event_masks[1] &= ~desired_event;
	else if (event < 96)
		ioc->event_masks[2] &= ~desired_event;
	else if (event < 128)
		ioc->event_masks[3] &= ~desired_event;
}

/**
 * _base_event_notification - send event notification
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_event_notification(struct MPT3SAS_ADAPTER *ioc)
{
	Mpi2EventNotificationRequest_t *mpi_request;
	u16 smid;
	int r = 0;
	int i;

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	if (ioc->base_cmds.status & MPT3_CMD_PENDING) {
		printk(MPT3SAS_ERR_FMT "%s: internal command already in use\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}

	smid = mpt3sas_base_get_smid(ioc, ioc->base_cb_idx);
	if (!smid) {
		printk(MPT3SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		return -EAGAIN;
	}
	ioc->base_cmds.status = MPT3_CMD_PENDING;
	mpi_request = mpt3sas_base_get_msg_frame(ioc, smid);
	ioc->base_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2EventNotificationRequest_t));
	mpi_request->Function = MPI2_FUNCTION_EVENT_NOTIFICATION;
	mpi_request->VF_ID = 0; /* TODO */
	mpi_request->VP_ID = 0;
	for (i = 0; i < MPI2_EVENT_NOTIFY_EVENTMASK_WORDS; i++)
		mpi_request->EventMasks[i] =
		    cpu_to_le32(ioc->event_masks[i]);
	init_completion(&ioc->base_cmds.done);
	ioc->put_smid_default(ioc, smid);
	wait_for_completion_timeout(&ioc->base_cmds.done, 30*HZ);
	if (!(ioc->base_cmds.status & MPT3_CMD_COMPLETE)) {
		printk(MPT3SAS_ERR_FMT "%s: timeout\n",
			ioc->name, __func__);
		_debug_dump_mf(mpi_request,
			sizeof(Mpi2EventNotificationRequest_t)/4);
		if (ioc->base_cmds.status & MPT3_CMD_RESET)
			r = -EFAULT;
		else
			r = -ETIME;
	} else
		dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: complete\n",
		    ioc->name, __func__));
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;
	return r;
}

/**
 * mpt3sas_base_validate_event_type - validating event types
 * @ioc: per adapter object
 * @event: firmware event
 *
 * This will turn on firmware event notification when application
 * ask for that event. We don't mask events that are already enabled.
 */
void
mpt3sas_base_validate_event_type(struct MPT3SAS_ADAPTER *ioc, u32 *event_type)
{
	int i, j;
	u32 event_mask, desired_event;
	u8 send_update_to_fw;

	for (i = 0, send_update_to_fw = 0; i <
	    MPI2_EVENT_NOTIFY_EVENTMASK_WORDS; i++) {
		event_mask = ~event_type[i];
		desired_event = 1;
		for (j = 0; j < 32; j++) {
			if (!(event_mask & desired_event) &&
			    (ioc->event_masks[i] & desired_event)) {
				ioc->event_masks[i] &= ~desired_event;
				send_update_to_fw = 1;
			}
			desired_event = (desired_event << 1);
		}
	}

	if (!send_update_to_fw)
		return;

	mutex_lock(&ioc->base_cmds.mutex);
	_base_event_notification(ioc);
	mutex_unlock(&ioc->base_cmds.mutex);
}


/**
 * mpt3sas_base_make_ioc_ready - put controller in READY state
 * @ioc: per adapter object
 * @type: FORCE_BIG_HAMMER or SOFT_RESET
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt3sas_base_make_ioc_ready(struct MPT3SAS_ADAPTER *ioc, enum reset_type type)
{
	u32 ioc_state;
	int rc;
	int count;

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	if (!mpt3sas_base_pci_device_is_available(ioc))
		return 0;

	ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
	dhsprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: ioc_state(0x%08x)\n",
	    ioc->name, __func__, ioc_state));

	/* if in RESET state, it should move to READY state shortly */
	count = 0;
	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_RESET) {
		while ((ioc_state & MPI2_IOC_STATE_MASK) !=
		    MPI2_IOC_STATE_READY) {
			if (count++ == 10) {
				printk(MPT3SAS_ERR_FMT "%s: failed going to "
				    " ready state (ioc_state=0x%x)\n",
				    ioc->name, __func__, ioc_state);
				return -EFAULT;
			}
			ssleep(1);
			ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
		}
	}

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_READY)
		return 0;

	if (ioc_state & MPI2_DOORBELL_USED) {
		printk(MPT3SAS_INFO_FMT "unexpected doorbell active!\n",
		    ioc->name);
		goto issue_diag_reset;
	}

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
		mpt3sas_print_fault_code(ioc, ioc_state &
		    MPI2_DOORBELL_DATA_MASK);
		goto issue_diag_reset;
	}
	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_COREDUMP) {
		if (ioc->ioc_coredump_loop != MPT3SAS_COREDUMP_LOOP_DONE) {
			mpt3sas_base_coredump_info(ioc, ioc_state &
				MPI2_DOORBELL_DATA_MASK);
			mpt3sas_base_wait_for_coredump_completion(ioc, __func__);
		}

		goto issue_diag_reset;
	}

	if (type == FORCE_BIG_HAMMER)
		goto issue_diag_reset;

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_OPERATIONAL)
		if (!(_base_send_ioc_reset(ioc,
		    MPI2_FUNCTION_IOC_MESSAGE_UNIT_RESET, 15))) {
			return 0;
	}

 issue_diag_reset:
	rc = _base_diag_reset(ioc);
	return rc;
}

/**
 * _base_make_ioc_operational - put controller in OPERATIONAL state
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_base_make_ioc_operational(struct MPT3SAS_ADAPTER *ioc)
{
	int r, rc, i, index;
	unsigned long flags;
	u32 reply_address;
	u16 smid;
	struct _tr_list *delayed_tr, *delayed_tr_next;
	struct _sc_list *delayed_sc, *delayed_sc_next;
	struct _event_ack_list *delayed_event_ack, *delayed_event_ack_next;
	struct adapter_reply_queue *reply_q;
	Mpi2ReplyDescriptorsUnion_t *reply_post_free_contig;
	u8 hide_flag;

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	/* clean the delayed target reset list */
	list_for_each_entry_safe(delayed_tr, delayed_tr_next,
	    &ioc->delayed_tr_list, list) {
		list_del(&delayed_tr->list);
		kfree(delayed_tr);
	}

	list_for_each_entry_safe(delayed_tr, delayed_tr_next,
	    &ioc->delayed_tr_volume_list, list) {
		list_del(&delayed_tr->list);
		kfree(delayed_tr);
	}

	list_for_each_entry_safe(delayed_tr, delayed_tr_next,
	    &ioc->delayed_internal_tm_list, list) {
		list_del(&delayed_tr->list);
		kfree(delayed_tr);
	}

	list_for_each_entry_safe(delayed_sc, delayed_sc_next,
	    &ioc->delayed_sc_list, list) {
		list_del(&delayed_sc->list);
		kfree(delayed_sc);
	}

	list_for_each_entry_safe(delayed_event_ack, delayed_event_ack_next,
	    &ioc->delayed_event_ack_list, list) {
		list_del(&delayed_event_ack->list);
		kfree(delayed_event_ack);
	}

	/* initialize the scsi lookup free list */
	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0))
	smid = 1;
	for (i = 0; i < ioc->scsiio_depth; i++, smid++) {
		ioc->scsi_lookup[i].cb_idx = 0xFF;
		ioc->scsi_lookup[i].smid = smid;
		ioc->scsi_lookup[i].scmd = NULL;
		ioc->scsi_lookup[i].direct_io = 0;
	}
#endif
	/* hi-priority queue */
	INIT_LIST_HEAD(&ioc->hpr_free_list);
	smid = ioc->hi_priority_smid;
	for (i = 0; i < ioc->hi_priority_depth; i++, smid++) {
		ioc->hpr_lookup[i].cb_idx = 0xFF;
		ioc->hpr_lookup[i].smid = smid;
		list_add_tail(&ioc->hpr_lookup[i].tracker_list,
		    &ioc->hpr_free_list);
	}

	/* internal queue */
	INIT_LIST_HEAD(&ioc->internal_free_list);
	smid = ioc->internal_smid;
	for (i = 0; i < ioc->internal_depth; i++, smid++) {
		ioc->internal_lookup[i].cb_idx = 0xFF;
		ioc->internal_lookup[i].smid = smid;
		list_add_tail(&ioc->internal_lookup[i].tracker_list,
		    &ioc->internal_free_list);
	}
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);

	/* initialize Reply Free Queue */
	for (i = 0, reply_address = (u32)ioc->reply_dma ;
	    i < ioc->reply_free_queue_depth ; i++, reply_address +=
	    ioc->reply_sz) {
		ioc->reply_free[i] = cpu_to_le32(reply_address);
		if (ioc->is_mcpu_endpoint)
			_base_clone_reply_to_sys_mem(ioc, reply_address, i);
	}

	/* initialize reply queues */
	if (ioc->is_driver_loading)
		_base_assign_reply_queues(ioc);

	/* initialize Reply Post Free Queue */
	index = 0;
	reply_post_free_contig = ioc->reply_post[0].reply_post_free;
	list_for_each_entry(reply_q, &ioc->reply_queue_list, list) {
		/*
		 * If RDPQ is enabled, switch to the next allocation.
		 * Otherwise advance within the contiguous region.
		 */
		if (ioc->rdpq_array_enable) {
			reply_q->reply_post_free =
				ioc->reply_post[index++].reply_post_free;
		} else {
			reply_q->reply_post_free = reply_post_free_contig;
			reply_post_free_contig += ioc->reply_post_queue_depth;
		}

		reply_q->reply_post_host_index = 0;	

		for (i = 0; i < ioc->reply_post_queue_depth; i++)
			reply_q->reply_post_free[i].Words =
				cpu_to_le64(ULLONG_MAX);
		if (!_base_is_controller_msix_enabled(ioc))
			goto skip_init_reply_post_free_queue;
	}
 skip_init_reply_post_free_queue:

	r = _base_send_ioc_init(ioc);
	if (r) {
		/*
		 * No need to check IOC state for fault state & issue
		 * diag reset during host reset. This check is need 
		 * only during driver load time.
		 */
		if (!ioc->is_driver_loading)
			return r;

		rc = _base_check_for_fault_and_issue_reset(ioc);
		if (rc || (r = _base_send_ioc_init(ioc)))
			return r;
	}

	/* initialize reply free host index */
	ioc->reply_free_host_index = ioc->reply_free_queue_depth - 1;
	writel(ioc->reply_free_host_index, &ioc->chip->ReplyFreeHostIndex);

	/* initialize reply post host index */
	list_for_each_entry(reply_q, &ioc->reply_queue_list, list) {
		if (ioc->combined_reply_queue) {
			for ( i = 0; i < ioc->nc_reply_index_count; i++ )
				writel((reply_q->msix_index & 7)<< MPI2_RPHI_MSIX_INDEX_SHIFT,
					ioc->replyPostRegisterIndex[i]);
		} else {
			writel(reply_q->msix_index << MPI2_RPHI_MSIX_INDEX_SHIFT,
			    &ioc->chip->ReplyPostHostIndex);
		}

		if (!_base_is_controller_msix_enabled(ioc))
			goto skip_init_reply_post_host_index;
	}

 skip_init_reply_post_host_index:
	mpt3sas_base_start_hba_unplug_watchdog(ioc);
	mpt3sas_base_unmask_interrupts(ioc);
	if (ioc->hba_mpi_version_belonged != MPI2_VERSION) {
		r = _base_display_fwpkg_version(ioc);
		if (r)
			return r;
	}

	_base_static_config_pages(ioc);
	
	/* event_unmask and port_enable must be issued without any/very less window */
	r = _base_event_notification(ioc);
	if (r)
		return r;

	if (ioc->is_driver_loading) {
		if (ioc->is_warpdrive &&
		    ioc->manu_pg10.OEMIdentifier == 0x80) {
			hide_flag = (u8) (
			 le32_to_cpu(ioc->manu_pg10.OEMSpecificFlags0) &
			 MFG_PAGE10_HIDE_SSDS_MASK);
			if (hide_flag != MFG_PAGE10_HIDE_SSDS_MASK)
				ioc->mfg_pg10_hide_flag = hide_flag;
                }

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
		if (ioc->is_warpdrive)
                        mpt3sas_enable_diag_buffer(ioc, 1);
		else if (diag_buffer_enable != -1 && diag_buffer_enable != 0)
			mpt3sas_enable_diag_buffer(ioc, diag_buffer_enable);
		else if (ioc->manu_pg11.HostTraceBufferMaxSizeKB != 0)
			mpt3sas_enable_diag_buffer(ioc, 1);
		if (disable_discovery > 0)
			return r;
#endif
		ioc->wait_for_discovery_to_complete =
		    _base_determine_wait_on_discovery(ioc);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
		return r; /* scan_start and scan_finished support */
#endif
	}

	r = _base_send_port_enable(ioc);
	if (r)
		return r;

	return r;
}

/**
 * mpt3sas_base_free_resources - free resources controller resources
 * @ioc: per adapter object
 *
 * Return nothing.
 */
void
mpt3sas_base_free_resources(struct MPT3SAS_ADAPTER *ioc)
{
	dexitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	if (!ioc->chip_phys)
		return;

	mpt3sas_base_mask_interrupts(ioc);
	ioc->shost_recovery = 1;
	mpt3sas_base_make_ioc_ready(ioc, SOFT_RESET);
	ioc->shost_recovery = 0;
	_base_unmap_resources(ioc);
	return;
}

/**
 * mpt3sas_base_attach - attach controller instance
 * @ioc: per adapter object
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt3sas_base_attach(struct MPT3SAS_ADAPTER *ioc)
{
	int r, rc, i;
	int cpu_id, last_cpu_id = 0;

	dinitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	/* setup cpu_msix_table */
	ioc->cpu_count = num_online_cpus();
	for_each_online_cpu(cpu_id)
		last_cpu_id = cpu_id;
	ioc->cpu_msix_table_sz = last_cpu_id + 1;
	ioc->cpu_msix_table = kzalloc(ioc->cpu_msix_table_sz, GFP_KERNEL);
	ioc->reply_queue_count = 1;
	if (!ioc->cpu_msix_table) {
		printk(MPT3SAS_ERR_FMT
		    "allocation for cpu_msix_table failed!!!\n", ioc->name);
		r = -ENOMEM;
		goto out_free_resources;
	}

	if (ioc->is_warpdrive) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18))
		ioc->reply_post_host_index = kcalloc(ioc->cpu_msix_table_sz,
		    sizeof(resource_size_t *), GFP_KERNEL);
#else
		ioc->reply_post_host_index = kcalloc(ioc->cpu_msix_table_sz,
		    sizeof(u64 *), GFP_KERNEL);
#endif
		if (!ioc->reply_post_host_index) {
			printk(MPT3SAS_INFO_FMT
			    "allocation for reply_post_host_index failed!!!\n",
			    ioc->name);
			r = -ENOMEM;
			goto out_free_resources;
		}
	}
	ioc->rdpq_array_enable_assigned = 0;
	ioc->use_32bit_dma = 0;
	ioc->dma_mask = 64;

	if (ioc->is_aero_ioc)
		ioc->base_readl = &_base_readl_aero;
	else
		ioc->base_readl = &_base_readl;

	ioc->smp_affinity_enable = smp_affinity_enable;

	r = mpt3sas_base_map_resources(ioc);
	if (r)
		goto out_free_resources;

	if (ioc->is_warpdrive) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18))
		ioc->reply_post_host_index[0] =
		    (resource_size_t *)&ioc->chip->ReplyPostHostIndex;

		for (i = 1; i < ioc->cpu_msix_table_sz; i++)
			ioc->reply_post_host_index[i] = (resource_size_t *)
			    ((u8 *)&ioc->chip->Doorbell + (0x4000 + ((i - 1)
				* 4)));
#else
		ioc->reply_post_host_index[0] =
		    (u64 *)&ioc->chip->ReplyPostHostIndex;

		for (i = 1; i < ioc->cpu_msix_table_sz; i++)
			ioc->reply_post_host_index[i] = (u64 *)
			    ((u8 *)&ioc->chip->Doorbell + (0x4000 + ((i - 1)
				* 4)));
#endif
	}

	pci_set_drvdata(ioc->pdev, ioc->shost);
	r = _base_get_ioc_facts(ioc);
	if (r) {
		rc = _base_check_for_fault_and_issue_reset(ioc);
		if (rc || (r =_base_get_ioc_facts(ioc)))
			goto out_free_resources;
	}
	switch(ioc->hba_mpi_version_belonged) {
	case MPI2_VERSION:
		ioc->build_sg_scmd = &_base_build_sg_scmd;
		ioc->build_sg = &_base_build_sg;
		ioc->build_zero_len_sge = &_base_build_zero_len_sge;
		ioc->get_msix_index_for_smlio = &_base_get_msix_index;
		break;
	case MPI25_VERSION:
	case MPI26_VERSION:
		/*
		 * SAS3.0 support
		 *
		 * SCSI_IO, SMP_PASSTHRU, SATA_PASSTHRU, Target Assist, and
		 * Target Status - all require the IEEE formated scatter gather
		 * elements.
		 *
		 */
		ioc->build_sg_scmd = &_base_build_sg_scmd_ieee;
		ioc->build_sg = &_base_build_sg_ieee;
		ioc->build_nvme_prp = &_base_build_nvme_prp;
		ioc->build_zero_len_sge = &_base_build_zero_len_sge_ieee;
		ioc->sge_size_ieee = sizeof(Mpi2IeeeSgeSimple64_t);
	
		if (ioc->high_iops_queues)
			ioc->get_msix_index_for_smlio = &_base_get_high_iops_msix_index;
		else	
			ioc->get_msix_index_for_smlio = &_base_get_msix_index;

		break;
	}

	if (ioc->atomic_desc_capable) {
		ioc->put_smid_default = &_base_put_smid_default_atomic;
		ioc->put_smid_scsi_io = &_base_put_smid_scsi_io_atomic;
		ioc->put_smid_fast_path = &_base_put_smid_fast_path_atomic;
		ioc->put_smid_hi_priority = &_base_put_smid_hi_priority_atomic;
		ioc->put_smid_nvme_encap= &_base_put_smid_nvme_encap_atomic;
#if defined(TARGET_MODE)
		ioc->put_smid_target_assist = &_base_put_smid_target_assist_atomic;
#endif
	} else {
		ioc->put_smid_default = &_base_put_smid_default;
		if (ioc->is_mcpu_endpoint)
			ioc->put_smid_scsi_io = &_base_put_smid_mpi_ep_scsi_io;
		else
			ioc->put_smid_scsi_io = &_base_put_smid_scsi_io;
		ioc->put_smid_fast_path = &_base_put_smid_fast_path;
		ioc->put_smid_hi_priority = &_base_put_smid_hi_priority;
		ioc->put_smid_nvme_encap= &_base_put_smid_nvme_encap;
#if defined(TARGET_MODE)
		ioc->put_smid_target_assist = &_base_put_smid_target_assist;
#endif
	}
	/*
	 * These function pointers for other requests that don't
	 * require IEEE scatter gather elements.
	 *
	 * For example Configuration Pages and SAS IOUNIT Control don't.
	 */
	ioc->build_sg_mpi = &_base_build_sg;
	ioc->build_zero_len_sge_mpi = &_base_build_zero_len_sge;

	r = mpt3sas_base_make_ioc_ready(ioc, SOFT_RESET);
	if (r)
		goto out_free_resources;

	ioc->pfacts = kcalloc(ioc->facts.NumberOfPorts,
	    sizeof(struct mpt3sas_port_facts), GFP_KERNEL);
	if (!ioc->pfacts) {
		printk(MPT3SAS_ERR_FMT
		    "allocation for port facts failed!!!\n", ioc->name);
		r = -ENOMEM;
		goto out_free_resources;
	}

	for (i = 0 ; i < ioc->facts.NumberOfPorts; i++) {
		r = _base_get_port_facts(ioc, i);
		if (r) {
			rc = _base_check_for_fault_and_issue_reset(ioc);
			if (rc || (r = _base_get_port_facts(ioc, i)))
				goto out_free_resources;
		}
	}

	r = _base_allocate_memory_pools(ioc);
	if (r)
		goto out_free_resources;

	if (irqpoll_weight > 0)	
		ioc->thresh_hold = irqpoll_weight;
	else
		ioc->thresh_hold = ioc->hba_queue_depth/4;

#if defined(MPT3SAS_ENABLE_IRQ_POLL)
	_base_init_irqpolls(ioc);
#endif
	init_waitqueue_head(&ioc->reset_wq);

	/* allocate memory pd handle bitmask list */
	ioc->pd_handles_sz = (ioc->facts.MaxDevHandle / 8);
	if (ioc->facts.MaxDevHandle % 8)
		ioc->pd_handles_sz++;
	ioc->pd_handles = kzalloc(ioc->pd_handles_sz,
	    GFP_KERNEL);
	if (!ioc->pd_handles) {
		r = -ENOMEM;
		goto out_free_resources;
	}
	ioc->blocking_handles = kzalloc(ioc->pd_handles_sz,
	    GFP_KERNEL);
	if (!ioc->blocking_handles) {
		r = -ENOMEM;
		goto out_free_resources;
	}

	/* allocate memory for pending OS device add list */
	ioc->pend_os_device_add_sz = (ioc->facts.MaxDevHandle / 8);
	if (ioc->facts.MaxDevHandle % 8)
		ioc->pend_os_device_add_sz++;
	ioc->pend_os_device_add = kzalloc(ioc->pend_os_device_add_sz,
	    GFP_KERNEL);
	if (!ioc->pend_os_device_add)
		goto out_free_resources;

	ioc->device_remove_in_progress_sz = ioc->pend_os_device_add_sz;
	ioc->device_remove_in_progress = kzalloc(ioc->device_remove_in_progress_sz,
	    GFP_KERNEL);
	if (!ioc->device_remove_in_progress)
		goto out_free_resources;

	ioc->tm_tr_retry_sz = ioc->facts.MaxDevHandle * sizeof(u8);
	ioc->tm_tr_retry = kzalloc(ioc->tm_tr_retry_sz, GFP_KERNEL);
	if (!ioc->tm_tr_retry)
		goto out_free_resources;

	ioc->fwfault_debug = mpt3sas_fwfault_debug;

	/* base internal command bits */
	mutex_init(&ioc->base_cmds.mutex);
	ioc->base_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->base_cmds.status = MPT3_CMD_NOT_USED;

	/* port_enable command bits */
	ioc->port_enable_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->port_enable_cmds.status = MPT3_CMD_NOT_USED;

	/* transport internal command bits */
	ioc->transport_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->transport_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->transport_cmds.mutex);

	/* scsih internal command bits */
	ioc->scsih_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->scsih_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->scsih_cmds.mutex);

	/* task management internal command bits */
	ioc->tm_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->tm_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->tm_cmds.mutex);

	/* config page internal command bits */
	ioc->config_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->config_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->config_cmds.mutex);

	/* ctl module internal command bits */
	ioc->ctl_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->ctl_cmds.sense = kzalloc(SCSI_SENSE_BUFFERSIZE, GFP_KERNEL);
	ioc->ctl_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->ctl_cmds.mutex);

	/* ctl module diag_buffer internal command bits */
	ioc->ctl_diag_cmds.reply = kzalloc(ioc->reply_sz, GFP_KERNEL);
	ioc->ctl_diag_cmds.status = MPT3_CMD_NOT_USED;
	mutex_init(&ioc->ctl_diag_cmds.mutex);

	if (!ioc->base_cmds.reply || !ioc->port_enable_cmds.reply ||
	    !ioc->transport_cmds.reply || !ioc->scsih_cmds.reply ||
	    !ioc->tm_cmds.reply || !ioc->config_cmds.reply ||
	    !ioc->ctl_cmds.reply || !ioc->ctl_cmds.sense ||
	    !ioc->ctl_diag_cmds.reply) {
		r = -ENOMEM;
		goto out_free_resources;
	}

	for (i = 0; i < MPI2_EVENT_NOTIFY_EVENTMASK_WORDS; i++)
		ioc->event_masks[i] = -1;

	/* here we enable the events we care about */
	_base_unmask_events(ioc, MPI2_EVENT_SAS_DISCOVERY);
	_base_unmask_events(ioc, MPI2_EVENT_SAS_BROADCAST_PRIMITIVE);
	_base_unmask_events(ioc, MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST);
	_base_unmask_events(ioc, MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE);
	_base_unmask_events(ioc, MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE);
	_base_unmask_events(ioc, MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST);
	_base_unmask_events(ioc, MPI2_EVENT_IR_VOLUME);
	_base_unmask_events(ioc, MPI2_EVENT_IR_PHYSICAL_DISK);
	_base_unmask_events(ioc, MPI2_EVENT_IR_OPERATION_STATUS);
	_base_unmask_events(ioc, MPI2_EVENT_LOG_ENTRY_ADDED);
	_base_unmask_events(ioc, MPI2_EVENT_TEMP_THRESHOLD);
	_base_unmask_events(ioc, MPI2_EVENT_ACTIVE_CABLE_EXCEPTION);
	_base_unmask_events(ioc, MPI2_EVENT_SAS_DEVICE_DISCOVERY_ERROR);
	if (ioc->hba_mpi_version_belonged == MPI26_VERSION) {
		if (ioc->is_gen35_ioc) {
			_base_unmask_events(ioc, MPI2_EVENT_PCIE_DEVICE_STATUS_CHANGE);
			_base_unmask_events(ioc, MPI2_EVENT_PCIE_ENUMERATION);
			_base_unmask_events(ioc, MPI2_EVENT_PCIE_TOPOLOGY_CHANGE_LIST);
		}
	}
	
#if defined(TARGET_MODE)
	_base_unmask_events(ioc, MPI2_EVENT_SAS_INIT_DEVICE_STATUS_CHANGE);
	_base_unmask_events(ioc, MPI2_EVENT_SAS_INIT_TABLE_OVERFLOW);
	_base_unmask_events(ioc, MPI2_EVENT_HARD_RESET_RECEIVED);
#endif

	r = _base_make_ioc_operational(ioc);
	if (r)
		goto out_free_resources;

	/*
	 * Copy current copy of IOCFacts in prev_fw_facts
	 * and it will be used during online firmware upgrade.
	 */
	memcpy(&ioc->prev_fw_facts, &ioc->facts,
	    sizeof(struct mpt3sas_facts));

	ioc->non_operational_loop = 0;
	ioc->ioc_coredump_loop = 0;
	ioc->got_task_abort_from_ioctl = 0;
	ioc->got_task_abort_from_sysfs = 0;
	return 0;

 out_free_resources:

	ioc->remove_host = 1;

	mpt3sas_base_free_resources(ioc);
	_base_release_memory_pools(ioc);
	pci_set_drvdata(ioc->pdev, NULL);
	kfree(ioc->cpu_msix_table);
	if (ioc->is_warpdrive)
		kfree(ioc->reply_post_host_index);
	kfree(ioc->pd_handles);
	kfree(ioc->blocking_handles);
	kfree(ioc->tm_tr_retry);
	kfree(ioc->device_remove_in_progress);
	kfree(ioc->pend_os_device_add);
	kfree(ioc->tm_cmds.reply);
	kfree(ioc->transport_cmds.reply);
	kfree(ioc->scsih_cmds.reply);
	kfree(ioc->config_cmds.reply);
	kfree(ioc->base_cmds.reply);
	kfree(ioc->port_enable_cmds.reply);
	kfree(ioc->ctl_cmds.reply);
	kfree(ioc->ctl_cmds.sense);
	kfree(ioc->ctl_diag_cmds.reply);
	kfree(ioc->pfacts);
	ioc->ctl_cmds.reply = NULL;
	ioc->base_cmds.reply = NULL;
	ioc->tm_cmds.reply = NULL;
	ioc->scsih_cmds.reply = NULL;
	ioc->transport_cmds.reply = NULL;
	ioc->config_cmds.reply = NULL;
	ioc->pfacts = NULL;
	return r;
}


/**
 * mpt3sas_base_detach - remove controller instance
 * @ioc: per adapter object
 *
 * Return nothing.
 */
void
mpt3sas_base_detach(struct MPT3SAS_ADAPTER *ioc)
{
	dexitprintk(ioc, printk(MPT3SAS_INFO_FMT "%s\n", ioc->name,
	    __func__));

	mpt3sas_base_stop_watchdog(ioc);
	mpt3sas_base_stop_hba_unplug_watchdog(ioc);
	mpt3sas_base_free_resources(ioc);
	_base_release_memory_pools(ioc);
	mpt3sas_free_enclosure_list(ioc);
	pci_set_drvdata(ioc->pdev, NULL);
	kfree(ioc->cpu_msix_table);
	if (ioc->is_warpdrive)
		kfree(ioc->reply_post_host_index);
	kfree(ioc->pd_handles);
	kfree(ioc->blocking_handles);
	kfree(ioc->tm_tr_retry);
	kfree(ioc->device_remove_in_progress);
	kfree(ioc->pend_os_device_add);
	kfree(ioc->pfacts);
	kfree(ioc->ctl_diag_cmds.reply);
	kfree(ioc->ctl_cmds.reply);
	kfree(ioc->ctl_cmds.sense);
	kfree(ioc->base_cmds.reply);
	kfree(ioc->port_enable_cmds.reply);
	kfree(ioc->tm_cmds.reply);
	kfree(ioc->transport_cmds.reply);
	kfree(ioc->scsih_cmds.reply);
	kfree(ioc->config_cmds.reply);
}

static void
_base_clear_outstanding_mpt_commands(struct MPT3SAS_ADAPTER *ioc)
{
	struct _internal_qcmd *scsih_qcmd, *scsih_qcmd_next;
	unsigned long flags;

	if (ioc->transport_cmds.status & MPT3_CMD_PENDING) {
		ioc->transport_cmds.status |= MPT3_CMD_RESET;
		mpt3sas_base_free_smid(ioc, ioc->transport_cmds.smid);
		complete(&ioc->transport_cmds.done);
	}
	if (ioc->base_cmds.status & MPT3_CMD_PENDING) {
		ioc->base_cmds.status |= MPT3_CMD_RESET;
		mpt3sas_base_free_smid(ioc, ioc->base_cmds.smid);
		complete(&ioc->base_cmds.done);
	}
	if (ioc->port_enable_cmds.status & MPT3_CMD_PENDING) {
		ioc->port_enable_failed = 1;
		ioc->port_enable_cmds.status |= MPT3_CMD_RESET;
		mpt3sas_base_free_smid(ioc, ioc->port_enable_cmds.smid);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20))
		if (ioc->is_driver_loading) {
			ioc->start_scan_failed =
			    MPI2_IOCSTATUS_INTERNAL_ERROR;
			ioc->start_scan = 0;
			ioc->port_enable_cmds.status =
			    MPT3_CMD_NOT_USED;
		} else
			complete(&ioc->port_enable_cmds.done);
#else
		complete(&ioc->port_enable_cmds.done);
#endif
	}
	if (ioc->config_cmds.status & MPT3_CMD_PENDING) {
		ioc->config_cmds.status |= MPT3_CMD_RESET;
		mpt3sas_base_free_smid(ioc, ioc->config_cmds.smid);
		ioc->config_cmds.smid = USHORT_MAX;
		complete(&ioc->config_cmds.done);
	}

	spin_lock_irqsave(&ioc->scsih_q_internal_lock, flags);
	list_for_each_entry_safe(scsih_qcmd, scsih_qcmd_next,
	    &ioc->scsih_q_intenal_cmds, list) {
		if ((scsih_qcmd->status) & MPT3_CMD_PENDING) {
			scsih_qcmd->status |= MPT3_CMD_RESET;
			mpt3sas_base_free_smid(ioc, scsih_qcmd->smid);
		}
	}
	spin_unlock_irqrestore(&ioc->scsih_q_internal_lock, flags);
}

/**
 * _base_reset_handler - reset callback handler (for base)
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
static void
_base_reset_handler(struct MPT3SAS_ADAPTER *ioc, int reset_phase)
{
	mpt3sas_scsih_reset_handler(ioc, reset_phase);
	mpt3sas_ctl_reset_handler(ioc, reset_phase);
#if defined(TARGET_MODE)
	if (stm_callbacks.reset_handler)
		stm_callbacks.reset_handler(ioc, reset_phase);
#endif

	switch (reset_phase) {
	case MPT3_IOC_PRE_RESET:
		dtmprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: "
		    "MPT3_IOC_PRE_RESET\n", ioc->name, __func__));
		break;
	case MPT3_IOC_AFTER_RESET:
		dtmprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: "
		    "MPT3_IOC_AFTER_RESET\n", ioc->name, __func__));
		_base_clear_outstanding_mpt_commands(ioc);
		break;
	case MPT3_IOC_DONE_RESET:
		dtmprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: "
		    "MPT3_IOC_DONE_RESET\n", ioc->name, __func__));
		break;
	}
}

/**
 * mpt3sas_wait_for_commands_to_complete - reset controller
 * @ioc: Pointer to MPT_ADAPTER structure
 *
 * This function waiting(3s) for all pending commands to complete
 * prior to putting controller in reset.
 */
void
mpt3sas_wait_for_commands_to_complete(struct MPT3SAS_ADAPTER *ioc)
{
	u32 ioc_state;
	unsigned long flags;
	u16 i;
	struct scsiio_tracker *st;

	ioc->pending_io_count = 0;
	
	if (!mpt3sas_base_pci_device_is_available(ioc)) {
		printk(MPT3SAS_ERR_FMT
				"%s: pci error recovery reset or"
				" pci device unplug occured\n",
				ioc->name, __func__);
		return;
	}

	ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
	if ((ioc_state & MPI2_IOC_STATE_MASK) != MPI2_IOC_STATE_OPERATIONAL)
		return;

	/* pending command count */
	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	for (i = 1; i <= ioc->scsiio_depth; i++) {
		st = mpt3sas_get_st_from_smid(ioc, i);
		if (st && st->smid !=0) {
			if (st->cb_idx != 0xFF)
				ioc->pending_io_count++;
		}
	}
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);

	if (!ioc->pending_io_count)
		return;

	/* wait for pending commands to complete */
	wait_event_timeout(ioc->reset_wq, ioc->pending_io_count == 0, 10 * HZ);
}

/**
 * _base_check_ioc_facts_changes - Look for increase/decrease of IOCFacts
 *     attributes during online firmware upgrade and update the corresponding
 *     IOC variables accordingly.
 *
 * @ioc: Pointer to MPT_ADAPTER structure
 * @old_facts: IOCFacts object before firmware upgrade
 */
static int
_base_check_ioc_facts_changes(struct MPT3SAS_ADAPTER *ioc)
{
	u16 pd_handles_sz, tm_tr_retry_sz;
	void *pd_handles = NULL, *blocking_handles = NULL;
	void *pend_os_device_add = NULL, *device_remove_in_progress = NULL;
	u8 *tm_tr_retry = NULL;
	struct mpt3sas_facts *old_facts = &ioc->prev_fw_facts;

	if (ioc->facts.MaxDevHandle > old_facts->MaxDevHandle)
	{
		pd_handles_sz = (ioc->facts.MaxDevHandle / 8);
		if (ioc->facts.MaxDevHandle % 8)
			pd_handles_sz++;

		pd_handles = krealloc(ioc->pd_handles, pd_handles_sz,
		    GFP_KERNEL);
		if (!pd_handles) {
			printk(MPT3SAS_ERR_FMT
			    "Unable to allocate the memory for pd_handles of sz: %d\n",
			    ioc->name, pd_handles_sz );
			return -ENOMEM;
		}
		memset(pd_handles + ioc->pd_handles_sz, 0,
		    (pd_handles_sz - ioc->pd_handles_sz));
		ioc->pd_handles = pd_handles;

		blocking_handles = krealloc(ioc->blocking_handles, pd_handles_sz,
		    GFP_KERNEL);
		if (!blocking_handles) {
			printk(MPT3SAS_ERR_FMT
			    "Unable to allocate the memory for blocking_handles of sz: %d\n",
			    ioc->name, pd_handles_sz );
			return -ENOMEM;
		}
		memset(blocking_handles + ioc->pd_handles_sz, 0,
		    (pd_handles_sz - ioc->pd_handles_sz));
		ioc->blocking_handles = blocking_handles;
		ioc->pd_handles_sz = pd_handles_sz;

		pend_os_device_add = krealloc(ioc->pend_os_device_add, pd_handles_sz,
		    GFP_KERNEL);
		if (!pend_os_device_add) {
			printk(MPT3SAS_ERR_FMT
			    "Unable to allocate the memory for pend_os_device_add of sz: %d\n",
			    ioc->name, pd_handles_sz);
			return -ENOMEM;
		}
		memset(pend_os_device_add + ioc->pend_os_device_add_sz, 0,
		    (pd_handles_sz - ioc->pend_os_device_add_sz));
		ioc->pend_os_device_add = pend_os_device_add;
		ioc->pend_os_device_add_sz = pd_handles_sz;

		device_remove_in_progress = krealloc(ioc->device_remove_in_progress,
		    pd_handles_sz, GFP_KERNEL);
		if (!device_remove_in_progress) {
			printk(MPT3SAS_ERR_FMT
			    "Unable to allocate the memory for device_remove_in_progress of sz: %d\n",
			    ioc->name, pd_handles_sz);
			return -ENOMEM;
		}
		memset(device_remove_in_progress +
		    ioc->device_remove_in_progress_sz, 0,
		    (pd_handles_sz - ioc->device_remove_in_progress_sz));
		ioc->device_remove_in_progress = device_remove_in_progress;
		ioc->device_remove_in_progress_sz = pd_handles_sz;

		tm_tr_retry_sz = ioc->facts.MaxDevHandle * sizeof(u8);
		tm_tr_retry = krealloc(ioc->tm_tr_retry, tm_tr_retry_sz,
		    GFP_KERNEL);
		if (!tm_tr_retry) {
			printk(MPT3SAS_ERR_FMT
			    "Unable to allocate the memory for tm_tr_retry of sz: %d\n",
			    ioc->name,  tm_tr_retry_sz);
			return -ENOMEM;
		}
		memset(tm_tr_retry + ioc->tm_tr_retry_sz, 0,
		    (tm_tr_retry_sz - ioc->tm_tr_retry_sz));
		ioc->tm_tr_retry = tm_tr_retry;
		ioc->tm_tr_retry_sz = tm_tr_retry_sz;
	}

	// TODO - Check for other IOCFacts attributies also.
	
	memcpy(&ioc->prev_fw_facts, &ioc->facts, sizeof(struct mpt3sas_facts));
	return 0;
}

/**
 * mpt3sas_base_hard_reset_handler - reset controller
 * @ioc: Pointer to MPT_ADAPTER structure
 * @type: FORCE_BIG_HAMMER or SOFT_RESET
 *
 * Returns 0 for success, non-zero for failure.
 */
int
mpt3sas_base_hard_reset_handler(struct MPT3SAS_ADAPTER *ioc, enum reset_type type)
{
	int r;
	unsigned long flags;
	u32 ioc_state;
	u8 is_fault = 0, is_trigger = 0;

	dtmprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: enter\n", ioc->name,
	    __func__));

	/* wait for an active reset in progress to complete */
	if (!mutex_trylock(&ioc->reset_in_progress_mutex)) {
		do {
			ssleep(1);
		} while (ioc->shost_recovery == 1);
		dtmprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: exit\n", ioc->name,
		    __func__));
		return ioc->ioc_reset_status;
	}

	if (!mpt3sas_base_pci_device_is_available(ioc)) {
		printk(MPT3SAS_ERR_FMT
		    "%s: pci error recovery reset or"
		    " pci device unplug occured\n",
		    ioc->name, __func__);
		if (mpt3sas_base_pci_device_is_unplugged(ioc))
			ioc->schedule_dead_ioc_flush_running_cmds(ioc);
		r = 0;
		goto out_unlocked;
	}
	
	mpt3sas_halt_firmware(ioc, 0);

	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	ioc->shost_recovery = 1;
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);

	ioc_state = mpt3sas_base_get_iocstate(ioc, 0);
	if ((ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_REGISTERED) &&
	    (!(ioc->diag_buffer_status[MPI2_DIAG_BUF_TYPE_TRACE] &
	    MPT3_DIAG_BUFFER_IS_RELEASED))) {
		is_trigger = 1;
		if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT ||
			(ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_COREDUMP) {
			is_fault = 1;
			ioc->htb_rel.trigger_info_dwords[1] =
			    (ioc_state & MPI2_DOORBELL_DATA_MASK);
		}
	}
	_base_reset_handler(ioc, MPT3_IOC_PRE_RESET);
	mpt3sas_wait_for_commands_to_complete(ioc);
	mpt3sas_base_mask_interrupts(ioc);
	r = mpt3sas_base_make_ioc_ready(ioc, type);
	if (r)
		goto out;
	_base_reset_handler(ioc, MPT3_IOC_AFTER_RESET);

	/* If this hard reset is called while port enable is active, then
	 * there is no reason to call make_ioc_operational
	 */
	if (ioc->is_driver_loading && ioc->port_enable_failed) {
		ioc->remove_host = 1;
		r = -EFAULT;
		goto out;
	}

	r = _base_get_ioc_facts(ioc);
	if (r)
		goto out;

	r = _base_check_ioc_facts_changes(ioc);
	if (r) {
		printk(MPT3SAS_ERR_FMT
		    "Some of the parameters got changed in this new firmware"
		    " image and it requires system reboot\n", ioc->name);
		goto out;
	}

	if (ioc->rdpq_array_enable && !ioc->rdpq_array_capable)
		panic("%s: Issue occurred with flashing controller firmware. "
			"Please reboot the system and ensure that the correct"
			" firmware version is running\n", ioc->name);

	r = _base_make_ioc_operational(ioc);
	if (!r)
		_base_reset_handler(ioc, MPT3_IOC_DONE_RESET);

 out:
	printk(MPT3SAS_INFO_FMT "%s: %s\n",
	    ioc->name, __func__, ((r == 0) ? "SUCCESS" : "FAILED"));
	spin_lock_irqsave(&ioc->ioc_reset_in_progress_lock, flags);
	ioc->ioc_reset_status = r;
	ioc->shost_recovery = 0;
	spin_unlock_irqrestore(&ioc->ioc_reset_in_progress_lock, flags);
	ioc->ioc_reset_count++;
	mutex_unlock(&ioc->reset_in_progress_mutex);

#if defined(DISABLE_RESET_SUPPORT)
	if (r != 0) {
		struct task_struct *p;

		ioc->remove_host = 1;
		ioc->schedule_dead_ioc_flush_running_cmds(ioc);
		p = kthread_run(mpt3sas_remove_dead_ioc_func, ioc,
		    "mpt3sas_dead_ioc_%d", ioc->id);
		if (IS_ERR(p))
			printk(MPT3SAS_ERR_FMT "%s: Running"
			    " mpt3sas_dead_ioc thread failed !!!!\n",
			    ioc->name, __func__);
		else
			printk(MPT3SAS_ERR_FMT "%s: Running"
			    " mpt3sas_dead_ioc thread success !!!!\n",
			    ioc->name, __func__);
	}
#else
	/* Flush all the outstanding IOs even when diag reset fails*/
	if (r != 0)
		ioc->schedule_dead_ioc_flush_running_cmds(ioc);
#endif

 out_unlocked:
 	if ((r == 0) && is_trigger) {
		if (is_fault)
			mpt3sas_trigger_master(ioc, MASTER_TRIGGER_FW_FAULT);
		else
			mpt3sas_trigger_master(ioc,
			    MASTER_TRIGGER_ADAPTER_RESET);
	}
	dtmprintk(ioc, printk(MPT3SAS_INFO_FMT "%s: exit\n", ioc->name,
	    __func__));
	return r;
}
#if defined(TARGET_MODE)
EXPORT_SYMBOL(mpt3sas_base_hard_reset_handler);
#endif
