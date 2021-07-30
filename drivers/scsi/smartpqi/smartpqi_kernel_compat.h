/*
 *    driver for Microchip PQI-based storage controllers
 *    Copyright (c) 2019-2021 Microchip Technology Inc. and its subsidiaries
 *    Copyright (c) 2016-2018 Microsemi Corporation
 *    Copyright (c) 2016 PMC-Sierra, Inc.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; version 2 of the License.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    Questions/Comments/Bugfixes to storagedev@microchip.com
 *
 */

#if !defined(_SMARTPQI_KERNEL_COMPAT_H)
#define _SMARTPQI_KERNEL_COMPAT_H

/* #define RHEL6 */
/* #define RHEL7 */
/* default is kernel.org */

/* ----- RHEL6 variants --------- */
#if \
	defined(RHEL6U0) || \
	defined(RHEL6U1) || \
	defined(RHEL6U2) || \
	defined(RHEL6U3) || \
	defined(RHEL6U4) || \
	defined(RHEL6U5) || \
	defined(RHEL6U6) || \
	defined(RHEL6U7) || \
	defined(RHEL6U8) || \
	defined(RHEL6U9) || \
	defined(RHEL6U10)
#define RHEL6
#endif

/* ----- RHEL7 variants --------- */
#if \
	defined(RHEL7U0)    || \
	defined(RHEL7U1)    || \
	defined(RHEL7U2)    || \
	defined(RHEL7U3)    || \
	defined(RHEL7U4)    || \
	defined(RHEL7U4ARM) || \
	defined(RHEL7U5)    || \
	defined(RHEL7U5ARM) || \
	defined(RHEL7U6)    || \
	defined(RHEL7U7)    || \
	defined(RHEL7U8)    || \
	defined(RHEL7U9)
#define RHEL7
#endif

/* ----- RHEL8 variants --------- */
#if \
	defined(RHEL8U0)    || \
	defined(RHEL8U1)    || \
	defined(RHEL8U2)    || \
	defined(RHEL8U3)    || \
	defined(RHEL8U4)    || \
	defined(RHEL8U5)    || \
	defined(RHEL8U6)    || \
	defined(RHEL8U7)
#define RHEL8
#endif

/* ----- SLES11 variants --------- */
#if \
	defined(SLES11SP0) || \
	defined(SLES11SP1) || \
	defined(SLES11SP2) || \
	defined(SLES11SP3) || \
	defined(SLES11SP4)
#define SLES11
#endif

/* ----- SLES12 variants --------- */
#if \
	defined(SLES12SP0) || \
	defined(SLES12SP1) || \
	defined(SLES12SP2) || \
	defined(SLES12SP3) || \
	defined(SLES12SP4) || \
	defined(SLES12SP5)
#define SLES12
#endif

/* ----- SLES15 variants --------- */
#if \
	defined(SLES15SP0) || \
	defined(SLES15SP1) || \
	defined(SLES15SP2) || \
	defined(SLES15SP3) || \
	defined(SLES15SP4)
#define SLES15
#endif

#include <scsi/scsi_tcq.h>
#include <linux/bsg-lib.h>
#include <linux/ktime.h>
#include <linux/dma-mapping.h>

#if defined(MSG_SIMPLE_TAG)
#define KFEATURE_HAS_SCSI_CHANGE_QUEUE_DEPTH		0
#if !defined(RHEL7U3)
#define KFEATURE_HAS_MQ_SUPPORT				0
#endif
#endif

#if defined(RHEL8) || defined(CENTOS7ALTARM)
#define KFEATURE_HAS_MQ_SUPPORT				0
#endif

#if defined(XEN7)
#define KCLASS4A
#endif

#if !defined(PCI_EXP_DEVCTL2_COMP_TIMEOUT)
#define PCI_EXP_DEVCTL2_COMP_TIMEOUT	0x000f
#if TORTUGA
#define KFEATURE_HAS_PCIE_CAPABILITY_SUPPORT		1
#else
#define KFEATURE_HAS_PCIE_CAPABILITY_SUPPORT		0
#endif
#endif

#if defined(RHEL6)
#define KFEATURE_HAS_WAIT_FOR_COMPLETION_IO		0
#define KFEATURE_HAS_2011_03_QUEUECOMMAND		0
#define KFEATURE_HAS_NO_WRITE_SAME			0
#define KFEATURE_HAS_ATOMIC_HOST_BUSY			0
#if defined(RHEL6U3) || defined(RHEL6U4) || defined(RHEL6U5)
#if defined(RHEL6U3)
#define KFEATURE_HAS_DMA_ZALLOC_COHERENT		0
#endif
#define KFEATURE_HAS_PCI_ENABLE_MSIX_RANGE		0
#endif
#if !defined(RHEL6U0) && !defined(RHEL6U1)
#define KFEATURE_HAS_LOCKLESS_DISPATCH_IO		1
#endif
#if defined(RHEL6U5)
#define KFEATURE_HAS_DMA_MASK_AND_COHERENT		0
#endif
#elif defined(RHEL7)
#if defined(RHEL7U0)
#define KFEATURE_HAS_PCI_ENABLE_MSIX_RANGE		0
#define KFEATURE_HAS_ATOMIC_HOST_BUSY			0
#endif
#if defined(RHEL7U1)
#define KFEATURE_HAS_ATOMIC_HOST_BUSY			0
#endif
#if defined(RHEL7U4ARM) || defined(RHEL7U5ARM)
#endif
#elif defined(SLES11)
#define KFEATURE_HAS_WAIT_FOR_COMPLETION_IO		0
#define KFEATURE_HAS_NO_WRITE_SAME			0
#define KFEATURE_HAS_ATOMIC_HOST_BUSY			0
#if defined(SLES11SP0) || defined(SLES11SP1)
#define KFEATURE_HAS_2011_03_QUEUECOMMAND		0
#endif
#if defined(SLES11SP3)
#define KFEATURE_HAS_DMA_ZALLOC_COHERENT		0
#define KFEATURE_HAS_PCI_ENABLE_MSIX_RANGE		0
#endif
#elif defined(SLES12)
#if defined(SLES12SP2) || defined(SLES12SP3)
#define KFEATURE_HAS_KTIME_SECONDS			1
#endif
#if defined(SLES12SP0)
#define KFEATURE_HAS_PCI_ENABLE_MSIX_RANGE		0
#define KFEATURE_HAS_ATOMIC_HOST_BUSY			0
#endif
#if defined(SLES12SP1)
#define KFEATURE_HAS_ATOMIC_HOST_BUSY			0
#endif
#elif defined(SLES15)
#define KFEATURE_HAS_SCSI_REQUEST			1
#define KFEATURE_HAS_KTIME_SECONDS			1
#elif defined(UBUNTU1404) || TORTUGA || defined(KCLASS3C)
#define KFEATURE_HAS_PCI_ENABLE_MSIX_RANGE		0
#define KFEATURE_HAS_ATOMIC_HOST_BUSY			0
#elif defined(OL7U2) || defined(KCLASS3B)
#define KFEATURE_HAS_DMA_MASK_AND_COHERENT		0
#define KFEATURE_HAS_WAIT_FOR_COMPLETION_IO		0
#define KFEATURE_HAS_ATOMIC_HOST_BUSY			0
#endif
#if defined(KCLASS4A)
#define KFEATURE_HAS_KTIME_SECONDS			1
#endif
#if defined(KCLASS4B) || defined(KCLASS4C) || defined(SLES12SP4) || \
    defined(SLES12SP5) || defined(RHEL8) || defined(KCLASS5A) || \
    defined(KCLASS5B) || defined(KCLASS5C) || defined(KCLASS5D) || \
    defined(SLES15SP2) || defined(SLES15SP3) || defined (CENTOS7ALTARM)
#define KFEATURE_HAS_KTIME_SECONDS			1
#define KFEATURE_HAS_SCSI_REQUEST			1
#define KFEATURE_HAS_KTIME64				1
#endif
#if defined(KCLASS4C) || defined(RHEL8) || defined(SLES15SP1) || \
    defined(SLES15SP2) || defined(SLES15SP3) || defined(KCLASS5A) || \
    defined(KCLASS5B) || defined(KCLASS5C) || defined(KCLASS5D) || \
    defined(SLES12SP5) || defined (CENTOS7ALTARM)
#define KFEATURE_HAS_BSG_JOB_SMP_HANDLER		1
#endif
#if defined(RHEL8U3)
#define KFEATURE_HAS_HOST_BUSY_FUNCTION			1
#endif

#if defined(KCLASS3D)
#define KFEATURE_HAS_KTIME_SECONDS			1
#endif
#if defined(KCLASS5A) || defined(KCLASS5B) || defined(KCLASS5C) || defined(KCLASS5D) || \
    defined(KCLASS4D) || defined(SLES15SP2) || defined(SLES15SP3)
#define dma_zalloc_coherent	dma_alloc_coherent
#define shost_use_blk_mq(x)	1
#define KFEATURE_HAS_USE_CLUSTERING			0
#endif

#if defined(KCLASS5B) || defined(KCLASS5C) || defined(KCLASS5D) || \
    defined(KCLASS4D) || defined(SLES15SP2) || defined(SLES15SP3)
#define IOCTL_INT	unsigned int
#else
#define IOCTL_INT	int
#endif

#if defined(KCLASS5C) || defined(KCLASS5D)
#define KFEATURE_HAS_HOST_BUSY_FUNCTION			1
#define FIELD_SIZEOF(t, f) (sizeof(((t*)0)->f))
#define ioremap_nocache ioremap
#endif


#if !defined(from_timer)
#define KFEATURE_HAS_OLD_TIMER				1
#endif

/* default values */
#define KFEATURE_HAS_KTIME_SECONDS			1
#define KFEATURE_HAS_BSG_JOB_SMP_HANDLER		1
#define KFEATURE_HAS_SCSI_SANITIZE_INQUIRY_STRING	1
#define KFEATURE_HAS_KTIME64				1
#define KFEATURE_HAS_DMA_ZALLOC_COHERENT		0
#define KFEATURE_HAS_USE_CLUSTERING			0
#define shost_use_blk_mq(x)	1

#if !defined(KFEATURE_HAS_WAIT_FOR_COMPLETION_IO)
#define KFEATURE_HAS_WAIT_FOR_COMPLETION_IO		1
#endif
#if !defined(KFEATURE_HAS_2011_03_QUEUECOMMAND)
#define KFEATURE_HAS_2011_03_QUEUECOMMAND		1
#endif
#if !defined(KFEATURE_HAS_DMA_ZALLOC_COHERENT)
#define KFEATURE_HAS_DMA_ZALLOC_COHERENT		1
#endif
#if !defined(KFEATURE_HAS_PCI_ENABLE_MSIX_RANGE)
#define KFEATURE_HAS_PCI_ENABLE_MSIX_RANGE		1
#endif
#if !defined(KFEATURE_HAS_SCSI_CHANGE_QUEUE_DEPTH)
#define KFEATURE_HAS_SCSI_CHANGE_QUEUE_DEPTH		1
#endif
#if !defined(KFEATURE_HAS_MQ_SUPPORT)
#define KFEATURE_HAS_MQ_SUPPORT				1
#endif
#if !defined(KFEATURE_HAS_SCSI_SANITIZE_INQUIRY_STRING)
#define KFEATURE_HAS_SCSI_SANITIZE_INQUIRY_STRING	1
#endif
#if !defined(KFEATURE_HAS_PCIE_CAPABILITY_SUPPORT)
#define KFEATURE_HAS_PCIE_CAPABILITY_SUPPORT		1
#endif
#if !defined(KFEATURE_HAS_NO_WRITE_SAME)
#define KFEATURE_HAS_NO_WRITE_SAME			1
#endif
#if !defined(KFEATURE_HAS_BSG_JOB_SMP_HANDLER)
#define KFEATURE_HAS_BSG_JOB_SMP_HANDLER		0
#endif
#if !defined(KFEATURE_HAS_HOST_BUSY_FUNCTION)
#define KFEATURE_HAS_HOST_BUSY_FUNCTION			0
#endif
#if !defined(KFEATURE_HAS_SCSI_REQUEST)
#define KFEATURE_HAS_SCSI_REQUEST			0
#endif
#if !defined(KFEATURE_HAS_LOCKLESS_DISPATCH_IO)
#define KFEATURE_HAS_LOCKLESS_DISPATCH_IO		0
#endif
#if !defined(KFEATURE_HAS_USE_CLUSTERING)
#define KFEATURE_HAS_USE_CLUSTERING			1
#define IOCTL_INT int
#else
/* for tk4 */
#ifdef IOCTL_INT
#undef IOCTL_INT
#define IOCTL_INT unsigned int
#endif
#endif
#if !defined(KFEATURE_HAS_OLD_TIMER)
#define KFEATURE_HAS_OLD_TIMER				0
#endif
#if !defined(KFEATURE_HAS_KTIME_SECONDS)
#define KFEATURE_HAS_KTIME_SECONDS			0
#endif
#if !defined(KFEATURE_HAS_KTIME64)
#define KFEATURE_HAS_KTIME64				0
#endif
#if !defined(KFEATURE_HAS_DMA_MASK_AND_COHERENT)
#define KFEATURE_HAS_DMA_MASK_AND_COHERENT		1
#endif
#if !defined(KFEATURE_HAS_ATOMIC_HOST_BUSY)
#define KFEATURE_HAS_ATOMIC_HOST_BUSY			1
#endif

#if !defined(list_next_entry)
#define list_next_entry(pos, member) \
	list_entry((pos)->member.next, typeof(*(pos)), member)
#endif

#if !defined(list_first_entry_or_null)
#define list_first_entry_or_null(ptr, type, member) \
	(!list_empty(ptr) ? list_first_entry(ptr, type, member) : NULL)
#endif

#if !defined(TYPE_ZBC)
#define TYPE_ZBC	0x14
#endif

#if !defined(readq)
#define readq readq
static inline u64 readq(const volatile void __iomem *addr)
{
	u32 lower32;
	u32 upper32;

	lower32 = readl(addr);
	upper32 = readl(addr + 4);

	return ((u64)upper32 << 32) | lower32;
}
#endif

#if !defined(writeq)
#define writeq writeq
static inline void writeq(u64 value, volatile void __iomem *addr)
{
	u32 lower32;
	u32 upper32;

	lower32 = lower_32_bits(value);
	upper32 = upper_32_bits(value);

	writel(lower32, addr);
	writel(upper32, addr + 4);
}
#endif

static inline void pqi_disable_write_same(struct scsi_device *sdev)
{
#if KFEATURE_HAS_NO_WRITE_SAME
	sdev->no_write_same = 1;
#endif
}

#if !defined(PCI_DEVICE_SUB)
#define PCI_DEVICE_SUB(vend, dev, subvend, subdev) \
	.vendor = (vend), .device = (dev), \
	.subvendor = (subvend), .subdevice = (subdev)
#endif

#if !defined(PCI_VENDOR_ID_HPE)
#define PCI_VENDOR_ID_HPE		0x1590
#endif

#if !defined(PCI_VENDOR_ID_ADVANTECH)
#define PCI_VENDOR_ID_ADVANTECH		0x13fe
#endif

#if !defined(PCI_VENDOR_ID_FIBERHOME)
#define PCI_VENDOR_ID_FIBERHOME		0x1d8d
#endif

#if !defined(PCI_VENDOR_ID_GIGABYTE)
#define PCI_VENDOR_ID_GIGABYTE		0x1458
#endif

#if !defined(PCI_VENDOR_ID_FOXCONN)
#define PCI_VENDOR_ID_FOXCONN		0x105b
#endif

#if !defined(PCI_VENDOR_ID_HUAWEI)
#define PCI_VENDOR_ID_HUAWEI		0x19e5
#endif

#if !defined(PCI_VENDOR_ID_H3C)
#define PCI_VENDOR_ID_H3C		0x193d
#endif

#if !defined(PCI_VENDOR_ID_QUANTA)
#define PCI_VENDOR_ID_QUANTA		0x152d
#endif

#if !defined(PCI_VENDOR_ID_INSPUR)
#define PCI_VENDOR_ID_INSPUR		0x1bd4
#endif

#if !defined(offsetofend)
#define offsetofend(TYPE, MEMBER) \
	(offsetof(TYPE, MEMBER)	+ sizeof(((TYPE *)0)->MEMBER))
#endif

void pqi_compat_init_scsi_host_template(struct scsi_host_template *template);
void pqi_compat_init_scsi_host(struct Scsi_Host *shost,
	struct pqi_ctrl_info *ctrl_info);

#if !KFEATURE_HAS_WAIT_FOR_COMPLETION_IO

static inline unsigned long wait_for_completion_io_timeout(struct completion *x,
	unsigned long timeout)
{
	return wait_for_completion_timeout(x, timeout);
}

static inline unsigned long wait_for_completion_io(struct completion *x)
{
	wait_for_completion(x);
	return 0;
}

#endif	/* !KFEATURE_HAS_WAIT_FOR_COMPLETION_IO */

#if KFEATURE_HAS_2011_03_QUEUECOMMAND

#define PQI_SCSI_QUEUE_COMMAND		pqi_scsi_queue_command

static inline void pqi_scsi_done(struct scsi_cmnd *scmd)
{
	pqi_prep_for_scsi_done(scmd);
	if (scmd && scmd->scsi_done)
		scmd->scsi_done(scmd);
}

#else

int pqi_scsi_queue_command_compat(struct scsi_cmnd *scmd,
	void (*done)(struct scsi_cmnd *));

#define PQI_SCSI_QUEUE_COMMAND		pqi_scsi_queue_command_compat

static inline void pqi_scsi_done(struct scsi_cmnd *scmd)
{
	void (*scsi_done)(struct scsi_cmnd *);

	pqi_prep_for_scsi_done(scmd);
	if (scmd) {
		scsi_done = (void(*)(struct scsi_cmnd *))scmd->SCp.ptr;
		scsi_done(scmd);
	}
}

#endif	/* KFEATURE_HAS_2011_03_QUEUECOMMAND */

#if !KFEATURE_HAS_DMA_ZALLOC_COHERENT

static inline void *dma_zalloc_coherent(struct device *dev, size_t size,
	dma_addr_t *dma_handle, gfp_t flag)
{
	void *ret = dma_alloc_coherent(dev, size, dma_handle,
		flag | __GFP_ZERO);
	return ret;
}

#endif	/* !KFEATURE_HAS_DMA_ZALLOC_COHERENT */

#if !KFEATURE_HAS_PCI_ENABLE_MSIX_RANGE

int pci_enable_msix_range(struct pci_dev *pci_dev, struct msix_entry *entries,
	int minvec, int maxvec);

#endif	/* !KFEATURE_HAS_PCI_ENABLE_MSIX_RANGE */

#if !KFEATURE_HAS_SCSI_CHANGE_QUEUE_DEPTH

int scsi_change_queue_depth(struct scsi_device *sdev, int queue_depth);

#endif	/* !KFEATURE_HAS_SCSI_CHANGE_QUEUE_DEPTH */

#if !KFEATURE_HAS_SCSI_SANITIZE_INQUIRY_STRING

void scsi_sanitize_inquiry_string(unsigned char *s, int len);

#endif	/* !KFEATURE_HAS_SCSI_SANITIZE_INQUIRY_STRING */

#if !KFEATURE_HAS_PCIE_CAPABILITY_SUPPORT

#define PCI_EXP_DEVCTL2			40	/* Device Control 2 */

int pcie_capability_clear_and_set_word(struct pci_dev *dev, int pos,
	u16 clear, u16 set);

#endif	/* !KFEATURE_HAS_PCIE_CAPABILITY_SUPPORT */

static inline u16 pqi_get_hw_queue(struct pqi_ctrl_info *ctrl_info,
	struct scsi_cmnd *scmd)
{
	u16 hw_queue;

#if KFEATURE_HAS_MQ_SUPPORT
	if (shost_use_blk_mq(scmd->device->host))
		hw_queue = blk_mq_unique_tag_to_hwq(blk_mq_unique_tag(scmd->request));
	else
		hw_queue = smp_processor_id();
#else
	hw_queue = smp_processor_id();
#endif
	if (hw_queue > ctrl_info->max_hw_queue_index)
		hw_queue = 0;

	return hw_queue;
}

#ifdef KFEATURE_NEEDS_BLK_RQ_IS_PASSTHROUGH

static inline bool blk_rq_is_passthrough(struct request *rq)
{
	return rq->cmd_type != REQ_TYPE_FS;
}

#endif	/* KFEATURE_NEEDS_BLK_RQ_IS_PASSTHROUGH */

#if !KFEATURE_HAS_BSG_JOB_SMP_HANDLER

int pqi_sas_smp_handler_compat(struct Scsi_Host *shost, struct sas_rphy *rphy,
	struct request *req);

void pqi_bsg_job_done(struct bsg_job *job, int result,
	unsigned int reply_payload_rcv_len);

#define PQI_SAS_SMP_HANDLER		pqi_sas_smp_handler_compat

#else

#define PQI_SAS_SMP_HANDLER		pqi_sas_smp_handler

static inline void pqi_bsg_job_done(struct bsg_job *job, int result,
	unsigned int reply_payload_rcv_len)
{
	bsg_job_done(job, result, reply_payload_rcv_len);
}

#endif	/* !KFEATURE_HAS_BSG_JOB_SMP_HANDLER */

#if KFEATURE_HAS_OLD_TIMER
#define from_timer(var, callback_timer, timer_fieldname) \
	container_of(callback_timer, typeof(*var), timer_fieldname)

#if !defined(TIMER_DATA_TYPE)
#define TIMER_DATA_TYPE		unsigned long
#define TIMER_FUNC_TYPE		void (*)(TIMER_DATA_TYPE)
#endif

static inline void timer_setup (struct timer_list *timer,
	void (*func) (struct timer_list *), unsigned long data)
{
	init_timer(timer);
	timer->function = (TIMER_FUNC_TYPE) func;
	timer->data = (unsigned long) timer;
}
#endif	/* KFEATURE_HAS_OLD_TIMER */

#if !KFEATURE_HAS_KTIME64
#define time64_to_tm	time_to_tm
#endif

#if !KFEATURE_HAS_KTIME_SECONDS
static inline unsigned long ktime_get_real_seconds(void)
{
	ktime_t tv;
	struct timeval time;

	tv = ktime_get_real();
	time = ktime_to_timeval(tv);

	return time.tv_sec;
}
#endif

#if !KFEATURE_HAS_DMA_MASK_AND_COHERENT

static inline int pqi_dma_set_mask_and_coherent(struct device *device, u64 mask)
{
	return dma_set_mask(device, mask);
}

#else

static inline int pqi_dma_set_mask_and_coherent(struct device *device, u64 mask)
{
	return dma_set_mask_and_coherent(device, mask);
}

#endif	/* !KFEATURE_HAS_DMA_MASK_AND_COHERENT */

static inline bool pqi_scsi_host_busy(struct Scsi_Host *shost)
{
#if KFEATURE_HAS_HOST_BUSY_FUNCTION
	return scsi_host_busy(shost);
#else
#if KFEATURE_HAS_ATOMIC_HOST_BUSY
	return atomic_read(&shost->host_busy) > 0;
#else
	return shost->host_busy > 0;
#endif
#endif
}

#endif	/* _SMARTPQI_KERNEL_COMPAT_H */
