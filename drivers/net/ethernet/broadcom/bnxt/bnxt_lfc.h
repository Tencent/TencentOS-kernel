/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_LFC_H
#define BNXT_LFC_H

#ifdef CONFIG_BNXT_LFC

/* Assuming that no HWRM command requires more than 10 DMA address
 * as input requests.
 */
#define MAX_NUM_DMA_INDICATIONS	10
#define MAX_DMA_MEM_SIZE		0x10000 /*64K*/

/* To prevent mismatch between bnxtnvm user application and bnxt_lfc
 * keeping the max. size as 512.
 */
#define BNXT_LFC_MAX_HWRM_REQ_LENGTH HWRM_MAX_REQ_LEN
#define BNXT_LFC_MAX_HWRM_RESP_LENGTH (512)

#define BNXT_NVM_FLUSH_TIMEOUT	((DFLT_HWRM_CMD_TIMEOUT) * 100)
#define BNXT_LFC_DEV_NAME	"bnxt_lfc"
#define DRV_NAME		BNXT_LFC_DEV_NAME

#define BNXT_LFC_ERR(dev, fmt, arg...)					\
	dev_err(dev, "%s: %s:%d: "fmt "\n",				\
		DRV_NAME, __func__,					\
		__LINE__, ##arg)					\

#define BNXT_LFC_WARN(dev, fmt, arg...)					\
	dev_warn(dev, "%s: %s:%d: "fmt "\n",				\
		DRV_NAME, __func__,					\
		__LINE__, ##arg)					\

#define BNXT_LFC_INFO(dev, fmt, arg...)					\
	dev_info(dev, "%s: %s:%d: "fmt "\n",				\
		DRV_NAME, __func__,					\
		__LINE__, ##arg)					\

#define BNXT_LFC_DEBUG(dev, fmt, arg...)				\
	dev_dbg(dev, "%s: %s:%d: "fmt "\n",				\
		DRV_NAME, __func__,					\
		__LINE__, ##arg)					\

struct bnxt_lfc_dev_array {
	u32 taken;
	struct bnxt_lfc_dev *bnxt_lfc_dev;
};

struct bnxt_lfc_dev {
	struct pci_dev *pdev;
	struct net_device *ndev;

	struct bnxt *bp;
	struct bnxt_en_dev *en_dev;

	struct bnxt_ulp_ops uops;
	u32 bus;
	u32 devfn;

	/* dma_virt_addr to hold the virtual address
	 * of the DMA memory.
	 */
	void *dma_virt_addr[MAX_NUM_DMA_INDICATIONS];
	/* dma_addr to hold the DMA addresses*/
	dma_addr_t dma_addr[MAX_NUM_DMA_INDICATIONS];
};

struct bnxt_gloabl_dev {
	dev_t d_dev;
	struct class *d_class;
	struct cdev c_dev;

	struct file_operations fops;

	struct mutex bnxt_lfc_lock;
};

struct bnxt_eem_dmabuf_info {
	int	fd;
	u32	dir;
	int	type;
	int	pg_count;
	struct dma_buf	*buf;
	struct sg_table	sg;
	struct device	*dev;
	struct page	**pages;
	struct bnxt_ctx_pg_info	*tbl;
};

int32_t bnxt_lfc_init(void);
void bnxt_lfc_exit(void);

#else

static inline int32_t bnxt_lfc_init()
{
}

static inline void bnxt_lfc_exit()
{
}
#endif
#endif /*BNXT_LFC_H*/
