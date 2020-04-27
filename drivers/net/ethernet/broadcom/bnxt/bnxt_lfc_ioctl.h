/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_LFC_IOCTL_H
#define BNXT_LFC_IOCTL_H

#define	BNXT_LFC_IOCTL_MAGIC	0x98
#define BNXT_LFC_VER		1

enum bnxt_lfc_req_type {
	BNXT_LFC_NVM_GET_VAR_REQ = 1,
	BNXT_LFC_NVM_SET_VAR_REQ,
	BNXT_LFC_NVM_FLUSH_REQ,
	BNXT_LFC_GENERIC_HWRM_REQ,
};

struct bnxt_lfc_req_hdr {
	uint32_t ver;
	uint32_t bus;
	uint32_t devfn;
	enum bnxt_lfc_req_type req_type;
};

struct bnxt_lfc_nvm_get_var_req {
	uint16_t option_num;
	uint16_t dimensions;
	uint16_t index_0;
	uint16_t index_1;
	uint16_t index_2;
	uint16_t index_3;
	uint16_t len_in_bits;
	uint8_t __user *out_val;
};

struct bnxt_lfc_nvm_set_var_req {
	uint16_t option_num;
	uint16_t dimensions;
	uint16_t index_0;
	uint16_t index_1;
	uint16_t index_2;
	uint16_t index_3;
	uint16_t len_in_bits;
	uint8_t __user *in_val;
};

struct dma_info {
	__u64 data;
	/* Based on read_or_write parameter
	 * LFC will either fill or read the
	 * data to or from the user memory
	 */
	__u32 length;
	/* Length of the data for read/write */
	__u16 offset;
	/* Offset at which HWRM input structure needs DMA address*/
	__u8 read_or_write;
	/* It should be 0 for write and 1 for read */
	__u8 unused;
};

struct blfc_fw_msg {
	__u64 usr_req;
	/* HWRM input structure */
	__u64 usr_resp;
	/* HWRM output structure */
	__u32 len_req;
	/* HWRM input structure length*/
	__u32 len_resp;
	/* HWRM output structure length*/
	__u32 timeout;
	/* HWRM command timeout. If 0 then
	 * LFC will provide default timeout
	 */
	__u32 num_dma_indications;
	/* Number of DMA addresses used in HWRM command */
	struct dma_info dma[0];
	/* User should allocate it with
	 * (sizeof(struct dma_info) * num_dma_indications)
	 */
};


struct bnxt_lfc_generic_msg {
	__u8 key;
	#define BNXT_LFC_KEY_DOMAIN_NO	1
	__u8 reserved[3];
	__u32 value;
};

struct bnxt_lfc_req {
	struct bnxt_lfc_req_hdr hdr;
	union {
		struct bnxt_lfc_nvm_get_var_req nvm_get_var_req;
		struct bnxt_lfc_nvm_set_var_req nvm_set_var_req;
		__u64 hreq; /* Pointer to "struct blfc_fw_msg" */
	} req;
};

#define	BNXT_LFC_REQ	_IOW(BNXT_LFC_IOCTL_MAGIC, 1, struct bnxt_lfc_req)
#endif /*BNXT_LFC_IOCTL_H*/
