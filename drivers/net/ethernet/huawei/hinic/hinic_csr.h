/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#ifndef HINIC_CSR_H
#define HINIC_CSR_H

#define HINIC_CSR_GLOBAL_BASE_ADDR			0x4000

/* HW interface registers */
#define HINIC_CSR_FUNC_ATTR0_ADDR			0x0
#define HINIC_CSR_FUNC_ATTR1_ADDR			0x4
#define HINIC_CSR_FUNC_ATTR2_ADDR			0x8
#define HINIC_CSR_FUNC_ATTR4_ADDR			0x10
#define HINIC_CSR_FUNC_ATTR5_ADDR			0x14

#define HINIC_FUNC_CSR_MAILBOX_DATA_OFF			0x80
#define HINIC_FUNC_CSR_MAILBOX_CONTROL_OFF		0x0100
#define HINIC_FUNC_CSR_MAILBOX_INT_OFFSET_OFF		0x0104
#define HINIC_FUNC_CSR_MAILBOX_RESULT_H_OFF		0x0108
#define HINIC_FUNC_CSR_MAILBOX_RESULT_L_OFF		0x010C

#define HINIC_CSR_DMA_ATTR_TBL_BASE			0xC80

#define HINIC_ELECTION_BASE				0x200

#define HINIC_CSR_DMA_ATTR_TBL_STRIDE			0x4
#define HINIC_CSR_DMA_ATTR_TBL_ADDR(idx)		\
			(HINIC_CSR_DMA_ATTR_TBL_BASE	\
			+ (idx) * HINIC_CSR_DMA_ATTR_TBL_STRIDE)

#define HINIC_PPF_ELECTION_STRIDE			0x4
#define HINIC_CSR_MAX_PORTS				4
#define HINIC_CSR_PPF_ELECTION_ADDR		\
			(HINIC_CSR_GLOBAL_BASE_ADDR + HINIC_ELECTION_BASE)

#define HINIC_CSR_GLOBAL_MPF_ELECTION_ADDR		\
			(HINIC_CSR_GLOBAL_BASE_ADDR + HINIC_ELECTION_BASE + \
			HINIC_CSR_MAX_PORTS * HINIC_PPF_ELECTION_STRIDE)

/* MSI-X registers */
#define HINIC_CSR_MSIX_CTRL_BASE			0x2000
#define HINIC_CSR_MSIX_CNT_BASE				0x2004

#define HINIC_CSR_MSIX_STRIDE				0x8

#define HINIC_CSR_MSIX_CTRL_ADDR(idx)			\
	(HINIC_CSR_MSIX_CTRL_BASE + (idx) * HINIC_CSR_MSIX_STRIDE)

#define HINIC_CSR_MSIX_CNT_ADDR(idx)			\
	(HINIC_CSR_MSIX_CNT_BASE + (idx) * HINIC_CSR_MSIX_STRIDE)

/* EQ registers */
#define HINIC_AEQ_MTT_OFF_BASE_ADDR			0x200
#define HINIC_CEQ_MTT_OFF_BASE_ADDR			0x400

#define HINIC_EQ_MTT_OFF_STRIDE				0x40

#define HINIC_CSR_AEQ_MTT_OFF(id)			\
	(HINIC_AEQ_MTT_OFF_BASE_ADDR + (id) * HINIC_EQ_MTT_OFF_STRIDE)

#define HINIC_CSR_CEQ_MTT_OFF(id)			\
	(HINIC_CEQ_MTT_OFF_BASE_ADDR + (id) * HINIC_EQ_MTT_OFF_STRIDE)

#define HINIC_CSR_EQ_PAGE_OFF_STRIDE			8

#define HINIC_AEQ_HI_PHYS_ADDR_REG(q_id, pg_num)	\
		(HINIC_CSR_AEQ_MTT_OFF(q_id) + \
		(pg_num) * HINIC_CSR_EQ_PAGE_OFF_STRIDE)

#define HINIC_AEQ_LO_PHYS_ADDR_REG(q_id, pg_num)	\
		(HINIC_CSR_AEQ_MTT_OFF(q_id) + \
		(pg_num) * HINIC_CSR_EQ_PAGE_OFF_STRIDE + 4)

#define HINIC_CEQ_HI_PHYS_ADDR_REG(q_id, pg_num)	\
		(HINIC_CSR_CEQ_MTT_OFF(q_id) + \
		(pg_num) * HINIC_CSR_EQ_PAGE_OFF_STRIDE)

#define HINIC_CEQ_LO_PHYS_ADDR_REG(q_id, pg_num)	\
		(HINIC_CSR_CEQ_MTT_OFF(q_id) + \
		(pg_num) * HINIC_CSR_EQ_PAGE_OFF_STRIDE + 4)

#define HINIC_EQ_HI_PHYS_ADDR_REG(type, q_id, pg_num)	\
		((u32)((type == HINIC_AEQ) ? \
		HINIC_AEQ_HI_PHYS_ADDR_REG(q_id, pg_num) : \
		HINIC_CEQ_HI_PHYS_ADDR_REG(q_id, pg_num)))

#define HINIC_EQ_LO_PHYS_ADDR_REG(type, q_id, pg_num)	\
		((u32)((type == HINIC_AEQ) ? \
		HINIC_AEQ_LO_PHYS_ADDR_REG(q_id, pg_num) : \
		HINIC_CEQ_LO_PHYS_ADDR_REG(q_id, pg_num)))

#define HINIC_AEQ_CTRL_0_ADDR_BASE			0xE00
#define HINIC_AEQ_CTRL_1_ADDR_BASE			0xE04
#define HINIC_AEQ_CONS_IDX_0_ADDR_BASE			0xE08
#define HINIC_AEQ_CONS_IDX_1_ADDR_BASE			0xE0C

#define HINIC_EQ_OFF_STRIDE				0x80

#define HINIC_CSR_AEQ_CTRL_0_ADDR(idx) \
	(HINIC_AEQ_CTRL_0_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

#define HINIC_CSR_AEQ_CTRL_1_ADDR(idx) \
	(HINIC_AEQ_CTRL_1_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

#define HINIC_CSR_AEQ_CONS_IDX_ADDR(idx) \
	(HINIC_AEQ_CONS_IDX_0_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

#define HINIC_CSR_AEQ_PROD_IDX_ADDR(idx) \
	(HINIC_AEQ_CONS_IDX_1_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

#define HINIC_CEQ_CTRL_0_ADDR_BASE			0x1000
#define HINIC_CEQ_CTRL_1_ADDR_BASE			0x1004
#define HINIC_CEQ_CONS_IDX_0_ADDR_BASE			0x1008
#define HINIC_CEQ_CONS_IDX_1_ADDR_BASE			0x100C

/* For multi-host mgmt
 * CEQ_CTRL_0_ADDR: bit26~29: uP write vf mode is normal(0x0),bmgw(0x1),
 * vmgw(0x2)
 */
#define HINIC_CSR_CEQ_CTRL_0_ADDR(idx) \
	(HINIC_CEQ_CTRL_0_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

#define HINIC_CSR_CEQ_CTRL_1_ADDR(idx) \
	(HINIC_CEQ_CTRL_1_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

#define HINIC_CSR_CEQ_CONS_IDX_ADDR(idx) \
	(HINIC_CEQ_CONS_IDX_0_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

#define HINIC_CSR_CEQ_PROD_IDX_ADDR(idx) \
	(HINIC_CEQ_CONS_IDX_1_ADDR_BASE + (idx) * HINIC_EQ_OFF_STRIDE)

/* API CMD registers */
#define HINIC_CSR_API_CMD_BASE				0xF000

#define HINIC_CSR_API_CMD_STRIDE			0x100

#define HINIC_CSR_API_CMD_CHAIN_HEAD_HI_ADDR(idx)	\
	(HINIC_CSR_API_CMD_BASE + 0x0 + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_CHAIN_HEAD_LO_ADDR(idx)	\
	(HINIC_CSR_API_CMD_BASE + 0x4 + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_STATUS_HI_ADDR(idx)		\
	(HINIC_CSR_API_CMD_BASE + 0x8 + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_STATUS_LO_ADDR(idx)		\
	(HINIC_CSR_API_CMD_BASE + 0xC + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_CHAIN_NUM_CELLS_ADDR(idx)	\
	(HINIC_CSR_API_CMD_BASE + 0x10 + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_CHAIN_CTRL_ADDR(idx)		\
	(HINIC_CSR_API_CMD_BASE + 0x14 + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_CHAIN_PI_ADDR(idx)		\
	(HINIC_CSR_API_CMD_BASE + 0x1C + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_CHAIN_REQ_ADDR(idx)		\
	(HINIC_CSR_API_CMD_BASE + 0x20 + (idx) * HINIC_CSR_API_CMD_STRIDE)

#define HINIC_CSR_API_CMD_STATUS_0_ADDR(idx)		\
	(HINIC_CSR_API_CMD_BASE + 0x30 + (idx) * HINIC_CSR_API_CMD_STRIDE)

/* VF control registers in pf */
#define HINIC_PF_CSR_VF_FLUSH_BASE		0x1F400
#define HINIC_PF_CSR_VF_FLUSH_STRIDE		0x4

#define HINIC_GLB_DMA_SO_RO_REPLACE_ADDR	0x488C

#define HINIC_ICPL_RESERVD_ADDR			0x9204

#define HINIC_PF_CSR_VF_FLUSH_OFF(idx)			\
	(HINIC_PF_CSR_VF_FLUSH_BASE + (idx) * HINIC_PF_CSR_VF_FLUSH_STRIDE)

#define HINIC_IPSU_CHANNEL_NUM		7
#define HINIC_IPSU_CHANNEL0_ADDR	0x404
#define HINIC_IPSU_CHANNEL_OFFSET	0x14
#define HINIC_IPSU_DIP_OFFSET		13
#define HINIC_IPSU_SIP_OFFSET		14
#define HINIC_IPSU_DIP_SIP_MASK		\
	((0x1 << HINIC_IPSU_SIP_OFFSET) | (0x1 << HINIC_IPSU_DIP_OFFSET))

#define HINIC_IPSURX_VXLAN_DPORT_ADDR	0x6d4

/* For multi-host mgmt
 * 0x75C0: bit0~3: uP write, host mode is bmwg or normal host
 *	   bit4~7: master host ppf write when function initializing
 *	   bit8~23: only for slave host PXE
 * 0x75C4: slave host status
 *	   bit0~7: host 0~7 functions status
 */
#define HINIC_HOST_MODE_ADDR			0x75C0
#define HINIC_MULT_HOST_SLAVE_STATUS_ADDR	0x75C4

#endif
