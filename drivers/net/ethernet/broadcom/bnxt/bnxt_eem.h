/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2020 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_EEM_H
#define BNXT_EEM_H

#include <linux/bitops.h>

#if (PAGE_SHIFT < 12)				/* < 4K >> 4K */
#define BNXT_EEM_PAGE_SHIFT 12
#elif (PAGE_SHIFT <= 13)			/* 4K, 8K */
#define BNXT_EEM_PAGE_SHIFT PAGE_SHIFT
#elif (PAGE_SHIFT < 16)				/* 16K, 32K >> 8K */
#define BNXT_EEM_PAGE_SHIFT 13
#elif (PAGE_SHIFT <= 17)			/* 64K, 128K >> 64K */
#define BNXT_EEM_PAGE_SHIFT 16
#elif (PAGE_SHIFT <= 19)			/* 256K, 512K >> 256K */
#define BNXT_EEM_PAGE_SHIFT 18
#elif (PAGE_SHIFT <= 22)			/* 1M, 2M, 4M */
#define BNXT_EEM_PAGE_SHIFT PAGE_SHIFT
#elif (PAGE_SHIFT <= 29)			/* 8M ... 512M >> 4M */
#define BNXT_EEM_PAGE_SHIFT 22
#else						/* >= 1G >> 1G */
#define BNXT_EEM_PAGE_SHIFT	30
#endif

#define BNXT_EEM_PAGE_SIZE	BIT(BNXT_EEM_PAGE_SHIFT)

#define BNXT_EEM_PG_SZ_4K	BIT(12)
#define BNXT_EEM_PG_SZ_8K	BIT(13)
#define BNXT_EEM_PG_SZ_64K	BIT(16)
#define BNXT_EEM_PG_SZ_256K	BIT(18)
#define BNXT_EEM_PG_SZ_1M	BIT(20)
#define BNXT_EEM_PG_SZ_2M	BIT(21)
#define BNXT_EEM_PG_SZ_4M	BIT(22)
#define BNXT_EEM_PG_SZ_1G	BIT(30)

#define	BNXT_EEM_MIN_ENTRIES	BIT(15)	/* 32K */
#define	BNXT_EEM_MAX_ENTRIES	BIT(27)	/* 128M */

#define BNXT_EEM_TX_RECORD_SIZE 64
#define BNXT_EEM_CTX_ID_INVALID 0xFFFF

enum bnxt_eem_dir {
	BNXT_EEM_DIR_TX,
	BNXT_EEM_DIR_RX,
	BNXT_EEM_NUM_DIR
};

#define BNXT_EEM_DIR(dir)       ((dir) == BNXT_EEM_DIR_TX ? "TX" : "RX")

enum bnxt_eem_table_type {
	KEY0_TABLE,
	KEY1_TABLE,
	RECORD_TABLE,
	EFC_TABLE,
	MAX_TABLE
};

enum bnxt_pg_tbl_lvl {
	PT_LVL_0,
	PT_LVL_1,
	PT_LVL_2,
	PT_LVL_MAX
};

/* CFA EEM Caps saved from the response to HWRM_CFA_EEM_QCAPS */
struct bnxt_eem_caps {
	u32	flags;
	u32	supported;
	u32	max_entries_supported;
	u16	key_entry_size;
	u16	record_entry_size;
	u16	efc_entry_size;
};

struct bnxt_eem_pg_misc {
	int	type;
	u16	ctx_id;
	u32	entry_size;
};

struct bnxt_eem_ctx_mem_info {
	u32				num_entries;
	struct bnxt_eem_pg_misc		eem_misc[MAX_TABLE];
	struct bnxt_ctx_pg_info		eem_tables[MAX_TABLE];
};

struct bnxt_eem_info {
	u8			eem_cfg_tx_dir:1;
	u8			eem_cfg_rx_dir:1;
	u8                      eem_primary:1;
	u16			eem_group;
	struct bnxt_eem_caps	eem_caps[BNXT_EEM_NUM_DIR];
	struct bnxt_eem_ctx_mem_info
				eem_ctx_info[BNXT_EEM_NUM_DIR];
};

void bnxt_hwrm_query_eem_caps(struct bnxt *bp);
int bnxt_eem_clear_cfg_system_memory(struct bnxt *bp);
int bnxt_eem_cfg_system_memory(struct bnxt *bp, u32 entry_num);
#endif
