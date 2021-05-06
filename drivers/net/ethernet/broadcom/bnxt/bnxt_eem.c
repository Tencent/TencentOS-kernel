/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2020 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include "bnxt_compat.h"
#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_hwrm.h"
#include "bnxt_eem.h"

static int bnxt_hwrm_cfa_eem_qcaps(struct bnxt *bp, enum bnxt_eem_dir dir)
{
	struct bnxt_eem_ctx_mem_info *ctxp = &bp->eem_info->eem_ctx_info[dir];
	struct bnxt_eem_pg_misc *tbl = &ctxp->eem_misc[KEY0_TABLE];
	struct bnxt_eem_caps *cap = &bp->eem_info->eem_caps[dir];
	struct hwrm_cfa_eem_qcaps_output *resp;
	struct hwrm_cfa_eem_qcaps_input *req;
	u32 flags;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_CFA_EEM_QCAPS);
	if (!rc) {
		flags = dir == BNXT_EEM_DIR_TX ?
			CFA_EEM_QCAPS_REQ_FLAGS_PATH_TX :
			CFA_EEM_QCAPS_REQ_FLAGS_PATH_RX;
		req->flags = cpu_to_le32(flags);

		resp = hwrm_req_hold(bp, req);
		rc = hwrm_req_send(bp, req);
	}
	if (rc) {
		hwrm_req_drop(bp, req);
		netdev_warn(bp->dev, "Failed to read CFA %s EEM Capabilities\n",
			    BNXT_EEM_DIR(dir));
		return rc;
	}

	cap->flags = le32_to_cpu(resp->flags);
	cap->supported = le32_to_cpu(resp->supported);
	cap->max_entries_supported = le32_to_cpu(resp->max_entries_supported);
	cap->key_entry_size = le16_to_cpu(resp->key_entry_size);
	cap->record_entry_size = le16_to_cpu(resp->record_entry_size);
	cap->efc_entry_size = le16_to_cpu(resp->efc_entry_size);
	hwrm_req_drop(bp, req);

	netdev_dbg(bp->dev, "%s CFA EEM Capabilities:\n", BNXT_EEM_DIR(dir));
	netdev_dbg(bp->dev, "supported: 0x%x max_entries_supported: 0x%x\n",
		   resp->supported, resp->max_entries_supported);
	netdev_dbg(bp->dev, "key_entry_size: 0x%x record_entry_size: 0x%x\n",
		   resp->key_entry_size, resp->record_entry_size);

	tbl[KEY0_TABLE].type = KEY0_TABLE;
	tbl[KEY1_TABLE].type = KEY1_TABLE;
	tbl[RECORD_TABLE].type = RECORD_TABLE;
	tbl[EFC_TABLE].type = EFC_TABLE;

	tbl[KEY0_TABLE].entry_size = cap->key_entry_size;
	tbl[KEY1_TABLE].entry_size = cap->key_entry_size;
	tbl[RECORD_TABLE].entry_size = cap->record_entry_size;
	tbl[EFC_TABLE].entry_size = cap->efc_entry_size;

	if (BNXT_CHIP_P4(bp) && (dir == BNXT_EEM_DIR_TX))
		cap->record_entry_size = BNXT_EEM_TX_RECORD_SIZE;

	tbl[KEY0_TABLE].ctx_id = BNXT_EEM_CTX_ID_INVALID;
	tbl[KEY1_TABLE].ctx_id = BNXT_EEM_CTX_ID_INVALID;
	tbl[RECORD_TABLE].ctx_id = BNXT_EEM_CTX_ID_INVALID;
	tbl[EFC_TABLE].ctx_id = BNXT_EEM_CTX_ID_INVALID;

	return rc;
}

void bnxt_hwrm_query_eem_caps(struct bnxt *bp)
{
	if (!(bp->fw_cap & BNXT_FW_CAP_CFA_EEM))
		return;

	bp->eem_info = kzalloc(sizeof(*bp->eem_info), GFP_KERNEL);
	if (!bp->eem_info)
		return;

	/* Query tx eem caps */
	bnxt_hwrm_cfa_eem_qcaps(bp, BNXT_EEM_DIR_TX);

	/* Query rx eem caps */
	bnxt_hwrm_cfa_eem_qcaps(bp, BNXT_EEM_DIR_RX);
}

static int bnxt_hwrm_cfa_eem_op(struct bnxt *bp, enum bnxt_eem_dir dir, u16 op)
{
	struct hwrm_cfa_eem_op_input *req;
	u32 flags;
	int rc;

	if (op != CFA_EEM_OP_REQ_OP_EEM_ENABLE &&
	    op != CFA_EEM_OP_REQ_OP_EEM_DISABLE &&
	    op != CFA_EEM_OP_REQ_OP_EEM_CLEANUP)
		return -EINVAL;

	rc = hwrm_req_init(bp, req, HWRM_CFA_EEM_OP);
	if (!rc) {
		flags = (dir == BNXT_EEM_DIR_TX) ?
			CFA_EEM_OP_REQ_FLAGS_PATH_TX :
			CFA_EEM_OP_REQ_FLAGS_PATH_RX;
		req->flags = cpu_to_le32(flags);
		req->op = cpu_to_le16(op);
		rc = hwrm_req_send(bp, req);
	}
	if (rc)
		netdev_warn(bp->dev, "%s CFA EEM OP:%d failed, rc=%d",
			    BNXT_EEM_DIR(dir), op, rc);
	else
		netdev_dbg(bp->dev, "%s CFA EEM OP:%d succeeded",
			   BNXT_EEM_DIR(dir), op);
	return rc;
}

static int bnxt_hwrm_cfa_eem_mem_unrgtr(struct bnxt *bp, u16 *ctx_id)
{
	struct hwrm_cfa_ctx_mem_unrgtr_input *req;
	int rc = 0;

	if (*ctx_id == BNXT_EEM_CTX_ID_INVALID)
		return rc;

	rc = hwrm_req_init(bp, req, HWRM_CFA_CTX_MEM_UNRGTR);
	if (!rc) {
		req->ctx_id = cpu_to_le16(*ctx_id);
		rc = hwrm_req_send(bp, req);
	}
	if (rc)
		netdev_warn(bp->dev, "Failed to Unregister CFA EEM Mem\n");
	else
		*ctx_id = BNXT_EEM_CTX_ID_INVALID;
	return rc;
}

static void bnxt_eem_ctx_unreg(struct bnxt *bp, enum bnxt_eem_dir dir)
{
	struct bnxt_eem_ctx_mem_info *ctxp = &bp->eem_info->eem_ctx_info[dir];
	struct bnxt_eem_pg_misc *tbl_misc;
	struct bnxt_ctx_pg_info *tbl;
	int tbl_type;

	for (tbl_type = KEY0_TABLE; tbl_type < MAX_TABLE; tbl_type++) {
		if (tbl_type == EFC_TABLE)
			continue;
		tbl = &ctxp->eem_tables[tbl_type];
		tbl_misc = &ctxp->eem_misc[tbl_type];
		bnxt_hwrm_cfa_eem_mem_unrgtr(bp, &tbl_misc->ctx_id);
		bnxt_free_ctx_pg_tbls(bp, tbl);
	}
}

static void bnxt_unconfig_eem(struct bnxt *bp, enum bnxt_eem_dir dir)
{
	bool configured = false;

	if (!(bp->fw_cap & BNXT_FW_CAP_CFA_EEM) ||
	    dir < BNXT_EEM_DIR_TX || dir > BNXT_EEM_DIR_RX)
		return;

	if (dir == BNXT_EEM_DIR_TX) {
		configured = bp->eem_info->eem_cfg_tx_dir ? true : false;
		bp->eem_info->eem_cfg_tx_dir = false;
	} else {
		configured = bp->eem_info->eem_cfg_rx_dir ? true : false;
		bp->eem_info->eem_cfg_rx_dir = false;
	}

	if (configured) {
		bnxt_hwrm_cfa_eem_op(bp, dir, CFA_EEM_OP_REQ_OP_EEM_DISABLE);
		bnxt_hwrm_cfa_eem_op(bp, dir, CFA_EEM_OP_REQ_OP_EEM_CLEANUP);
	}

	if (bp->eem_info->eem_primary)
		bnxt_eem_ctx_unreg(bp, dir);

	if (!bp->eem_info->eem_cfg_tx_dir && !bp->eem_info->eem_cfg_rx_dir) {
		bp->eem_info->eem_primary = false;
		bp->eem_info->eem_group = -1;
	}
}

static int bnxt_eem_validate_num_entries(struct bnxt *bp,
					 enum bnxt_eem_dir dir,
					 u32 num_entries)
{
	struct bnxt_eem_caps *cap = &bp->eem_info->eem_caps[dir];

	if (num_entries < BNXT_EEM_MIN_ENTRIES ||
	    num_entries > cap->max_entries_supported ||
	    num_entries != roundup_pow_of_two(num_entries)) {
		netdev_warn(bp->dev, "EEM: Invalid num_entries requested: %u\n",
			    num_entries);
		return -EINVAL;
	}

	bp->eem_info->eem_ctx_info[dir].num_entries = num_entries;

	return 0;
}

static void bnxt_eem_pg_size_table(struct bnxt_eem_caps *cap, int tbl_type,
				   u32 num_entries, u32 *size, u8 *depth,
				   struct bnxt_eem_pg_misc *tbl_misc)
{
	u32 mem_size = 0;

	if (tbl_type == KEY0_TABLE || tbl_type == KEY1_TABLE) {
		mem_size = cap->key_entry_size * num_entries;
		tbl_misc->entry_size = cap->key_entry_size;
	}

	if (tbl_type == RECORD_TABLE) {
		mem_size = cap->record_entry_size * num_entries * 2;
		tbl_misc->entry_size = cap->record_entry_size;
	}

	if (tbl_type == EFC_TABLE) {
		mem_size = cap->efc_entry_size * num_entries;
		tbl_misc->entry_size = cap->efc_entry_size;
	}

	tbl_misc->type = tbl_type;
	*size = mem_size;
	*depth = (DIV_ROUND_UP(mem_size, BNXT_PAGE_SIZE) < MAX_CTX_PAGES) ?
		 1 : 2;
}

static u8 bnxt_eem_page_level_code(int pg_lvl)
{
	u8 page_level;

	switch (pg_lvl) {
	case PT_LVL_2:
		page_level = CFA_CTX_MEM_RGTR_REQ_PAGE_LEVEL_LVL_2;
		break;
	case PT_LVL_1:
		page_level = CFA_CTX_MEM_RGTR_REQ_PAGE_LEVEL_LVL_1;
		break;
	case PT_LVL_0:
		page_level = CFA_CTX_MEM_RGTR_REQ_PAGE_LEVEL_LVL_0;
		break;
	default:
		page_level = 0xff;
		break;
	}
	return page_level;
}

static u8 bnxt_eem_page_size_code(u32 page_size)
{
	u8 pg_sz_code = 0xff;

	switch (page_size) {
	case BNXT_EEM_PG_SZ_4K:
		pg_sz_code = CFA_CTX_MEM_RGTR_REQ_PAGE_SIZE_4K;
		break;
	case BNXT_EEM_PG_SZ_8K:
		pg_sz_code = CFA_CTX_MEM_RGTR_REQ_PAGE_SIZE_8K;
		break;
	case BNXT_EEM_PG_SZ_64K:
		pg_sz_code = CFA_CTX_MEM_RGTR_REQ_PAGE_SIZE_64K;
		break;
	case BNXT_EEM_PG_SZ_256K:
		pg_sz_code = CFA_CTX_MEM_RGTR_REQ_PAGE_SIZE_256K;
		break;
	case BNXT_EEM_PG_SZ_1M:
		pg_sz_code = CFA_CTX_MEM_RGTR_REQ_PAGE_SIZE_1M;
		break;
	case BNXT_EEM_PG_SZ_2M:
		pg_sz_code = CFA_CTX_MEM_RGTR_REQ_PAGE_SIZE_2M;
		break;
	case BNXT_EEM_PG_SZ_4M:
		pg_sz_code = CFA_CTX_MEM_RGTR_REQ_PAGE_SIZE_4M;
		break;
	case BNXT_EEM_PG_SZ_1G:
		pg_sz_code = CFA_CTX_MEM_RGTR_REQ_PAGE_SIZE_1G;
		break;
	default:
		break;
	}

	return pg_sz_code;
}

static int bnxt_hwrm_cfa_eem_mem_rgtr(struct bnxt *bp, int page_level,
				      u32 page_size, u64 page_dir, u16 *ctx_id)
{
	struct hwrm_cfa_ctx_mem_rgtr_output *resp;
	struct hwrm_cfa_ctx_mem_rgtr_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_CFA_CTX_MEM_RGTR);
	if (!rc) {
		req->flags = 0;
		req->page_level = bnxt_eem_page_level_code(page_level);
		req->page_size = bnxt_eem_page_size_code(page_size);
		req->page_dir = cpu_to_le64(page_dir);

		resp = hwrm_req_hold(bp, req);
		rc = hwrm_req_send(bp, req);
	}
	if (rc) {
		hwrm_req_drop(bp, req);
		netdev_warn(bp->dev, "Failed to register CFA EEM Mem\n");
		return rc;
	}

	*ctx_id = le16_to_cpu(resp->ctx_id);
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_eem_ctx_reg(struct bnxt *bp, enum bnxt_eem_dir dir,
			    u16 grp_id, u32 num_entries)
{
	struct bnxt_eem_ctx_mem_info *ctxp = &bp->eem_info->eem_ctx_info[dir];
	struct bnxt_eem_caps *cap = &bp->eem_info->eem_caps[dir];
	struct bnxt_eem_pg_misc *tbl_misc;
	struct bnxt_ctx_pg_info *tbl;
	int tbl_type;
	u32 mem_size;
	u8 depth;
	int rc;

	ctxp->num_entries = num_entries;
	if (!num_entries)
		return 0;

	for (tbl_type = KEY0_TABLE; tbl_type < MAX_TABLE; tbl_type++) {
		tbl = &ctxp->eem_tables[tbl_type];
		tbl_misc = &ctxp->eem_misc[tbl_type];

		/* No EFC table support, skip EFC */
		if (tbl_type == EFC_TABLE) {
			tbl_misc->ctx_id = 0;
			tbl_misc->entry_size = 0;
			continue;
		}

		bnxt_eem_pg_size_table(cap, tbl_type, num_entries,
				       &mem_size, &depth, tbl_misc);
		rc = bnxt_alloc_ctx_pg_tbls(bp, tbl, mem_size, depth, false);
		if (rc) {
			netdev_err(bp->dev,
				   "alloc failed size 0x%x, depth %d\n",
				   mem_size, depth);
			goto cleanup;
		}

		rc = bnxt_hwrm_cfa_eem_mem_rgtr(bp, depth,
						BNXT_EEM_PAGE_SIZE,
						tbl->ring_mem.pg_tbl_map,
						&tbl_misc->ctx_id);
		if (rc)
			goto cleanup;
	}
	return rc;
cleanup:
	bnxt_eem_ctx_unreg(bp, dir);
	return rc;
}

static int bnxt_hwrm_cfa_eem_cfg(struct bnxt *bp, enum bnxt_eem_dir dir,
				 u32 cfg_flags,
				 u16 grp_id)
{
	struct bnxt_eem_ctx_mem_info *ctxp = &bp->eem_info->eem_ctx_info[dir];
	struct bnxt_eem_pg_misc *tbl = &ctxp->eem_misc[KEY0_TABLE];
	struct hwrm_cfa_eem_cfg_input *req;
	u32 flags;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_CFA_EEM_CFG);
	if (!rc) {
		flags = (dir == BNXT_EEM_DIR_TX) ?
			CFA_EEM_CFG_REQ_FLAGS_PATH_TX :
			CFA_EEM_CFG_REQ_FLAGS_PATH_RX;
		flags |= CFA_EEM_CFG_REQ_FLAGS_PREFERRED_OFFLOAD;

		if (cfg_flags) {
			req->key0_ctx_id = cpu_to_le16(tbl[KEY0_TABLE].ctx_id);
			req->key1_ctx_id = cpu_to_le16(tbl[KEY1_TABLE].ctx_id);
			req->record_ctx_id = cpu_to_le16(tbl[RECORD_TABLE].ctx_id);
			req->efc_ctx_id = cpu_to_le16(tbl[EFC_TABLE].ctx_id);
			req->num_entries = cpu_to_le32(ctxp->num_entries);
		} else {
			flags |= CFA_EEM_CFG_REQ_FLAGS_SECONDARY_PF;
		}
		req->flags = cpu_to_le32(flags);
		req->group_id = cpu_to_le16(grp_id);

		rc = hwrm_req_send(bp, req);
	}
	if (rc) {
		netdev_warn(bp->dev, "Failed to configure %s CFA EEM: rc=%d",
			    BNXT_EEM_DIR(dir), rc);
		return rc;
	}
	netdev_dbg(bp->dev, "Configured %s EEM\n", BNXT_EEM_DIR(dir));
	return rc;
}

static int bnxt_config_eem(struct bnxt *bp, enum bnxt_eem_dir dir, u32 flags,
			   u32 num_entries, u16 grp_id)
{
	int rc = 0;

	if (!(bp->fw_cap & BNXT_FW_CAP_CFA_EEM) || dir < BNXT_EEM_DIR_TX ||
	    dir > BNXT_EEM_DIR_RX ||
	    bnxt_eem_validate_num_entries(bp, dir, num_entries))
		return -EINVAL;

	if ((bp->eem_info->eem_cfg_tx_dir && (dir == BNXT_EEM_DIR_TX)) ||
	    (bp->eem_info->eem_cfg_rx_dir && (dir == BNXT_EEM_DIR_RX))) {
		netdev_warn(bp->dev,
			    "already config the EEM in dir %d\n",
			    dir);
		return 0;
	}

	bp->eem_info->eem_primary = flags ? true : false;
	if (bp->eem_info->eem_primary) {
		rc = bnxt_eem_ctx_reg(bp, dir, grp_id, num_entries);
		if (rc)
			goto cleanup;
	}

	rc = bnxt_hwrm_cfa_eem_cfg(bp, dir, flags, grp_id);
	if (rc)
		goto cleanup;

	rc = bnxt_hwrm_cfa_eem_op(bp, dir, CFA_EEM_OP_REQ_OP_EEM_ENABLE);
	if (rc)
		goto cleanup;

	if (dir == BNXT_EEM_DIR_TX)
		bp->eem_info->eem_cfg_tx_dir = true;
	else
		bp->eem_info->eem_cfg_rx_dir = true;
	bp->eem_info->eem_group = grp_id;

	return rc;
cleanup:
	bnxt_unconfig_eem(bp, dir);
	return rc;
}

int bnxt_eem_clear_cfg_system_memory(struct bnxt *bp)
{
	int dir;

	netdev_dbg(bp->dev, "call undo system memory\n");
	for (dir = BNXT_EEM_DIR_TX; dir < BNXT_EEM_NUM_DIR; dir++)
		bnxt_unconfig_eem(bp, dir);

	return 0;
}

int bnxt_eem_cfg_system_memory(struct bnxt *bp, u32 entry_num)
{
	int dir;
	int rc;

	netdev_dbg(bp->dev,
		   "call cfg system memory, entry num: %d\n",
		   entry_num);

	for (dir = BNXT_EEM_DIR_TX; dir < BNXT_EEM_NUM_DIR; dir++) {
		rc = bnxt_config_eem(bp, dir, true, entry_num, 0);
		if (rc) {
			netdev_err(bp->dev,
				   "config EEM fail. dir %d entry_num %d\n",
				   dir, entry_num);
			goto cleanup;
		}
	}
	return rc;
cleanup:
	for (dir = BNXT_EEM_DIR_TX; dir < BNXT_EEM_NUM_DIR; dir++)
		bnxt_unconfig_eem(bp, dir);

	return rc;
}
