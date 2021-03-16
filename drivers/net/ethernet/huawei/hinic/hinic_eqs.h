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

#ifndef HINIC_EQS_H
#include <linux/interrupt.h>

#define HINIC_EQS_H

#define HINIC_EQ_PAGE_SIZE		0x00001000

#define HINIC_MAX_AEQS			3
#define HINIC_MAX_CEQS			32

#define HINIC_EQ_MAX_PAGES		8

#define HINIC_AEQE_SIZE			64
#define HINIC_CEQE_SIZE			4

#define HINIC_AEQE_DESC_SIZE		4
#define HINIC_AEQE_DATA_SIZE		\
			(HINIC_AEQE_SIZE - HINIC_AEQE_DESC_SIZE)

#define HINIC_DEFAULT_AEQ_LEN		0x10000
#define HINIC_DEFAULT_CEQ_LEN		0x10000

#define HINIC_VMGW_DEFAULT_AEQ_LEN	128
#define HINIC_VMGW_DEFAULT_CEQ_LEN	1024

#define HINIC_MIN_AEQ_LEN		64
#define HINIC_MAX_AEQ_LEN		(512 * 1024)
#define HINIC_MIN_CEQ_LEN		64
#define HINIC_MAX_CEQ_LEN		(1024 * 1024)

#define	HINIC_CEQ_ID_CMDQ		0

#define EQ_IRQ_NAME_LEN			64

enum hinic_eq_type {
	HINIC_AEQ,
	HINIC_CEQ
};

enum hinic_eq_intr_mode {
	HINIC_INTR_MODE_ARMED,
	HINIC_INTR_MODE_ALWAYS,
};

enum hinic_eq_ci_arm_state {
	HINIC_EQ_NOT_ARMED,
	HINIC_EQ_ARMED,
};

struct hinic_eq_work {
	struct work_struct	work;
	void			*data;
};

struct hinic_ceq_tasklet_data {
	void	*data;
};

struct hinic_eq {
	struct hinic_hwdev		*hwdev;
	u16				q_id;
	enum hinic_eq_type		type;
	u32				page_size;
	u32				orig_page_size;
	u32				eq_len;

	u32				cons_idx;
	u16				wrapped;

	u16				elem_size;
	u16				num_pages;
	u32				num_elem_in_pg;

	struct irq_info		eq_irq;
	char				irq_name[EQ_IRQ_NAME_LEN];

	dma_addr_t			*dma_addr;
	u8				**virt_addr;
	dma_addr_t			*dma_addr_for_free;
	u8				**virt_addr_for_free;

	struct hinic_eq_work		aeq_work;
	struct tasklet_struct		ceq_tasklet;
	struct hinic_ceq_tasklet_data	ceq_tasklet_data;

	u64	hard_intr_jif;
	u64	soft_intr_jif;
};

struct hinic_aeq_elem {
	u8	aeqe_data[HINIC_AEQE_DATA_SIZE];
	u32	desc;
};

enum hinic_aeq_cb_state {
	HINIC_AEQ_HW_CB_REG = 0,
	HINIC_AEQ_HW_CB_RUNNING,
	HINIC_AEQ_SW_CB_REG,
	HINIC_AEQ_SW_CB_RUNNING,
};

struct hinic_aeqs {
	struct hinic_hwdev	*hwdev;

	hinic_aeq_hwe_cb	aeq_hwe_cb[HINIC_MAX_AEQ_EVENTS];
	hinic_aeq_swe_cb	aeq_swe_cb[HINIC_MAX_AEQ_SW_EVENTS];
	unsigned long		aeq_hw_cb_state[HINIC_MAX_AEQ_EVENTS];
	unsigned long		aeq_sw_cb_state[HINIC_MAX_AEQ_SW_EVENTS];

	struct hinic_eq		aeq[HINIC_MAX_AEQS];
	u16			num_aeqs;

	struct workqueue_struct *workq;
};

enum hinic_ceq_cb_state {
	HINIC_CEQ_CB_REG = 0,
	HINIC_CEQ_CB_RUNNING,
};

struct hinic_ceqs {
	struct hinic_hwdev	*hwdev;

	hinic_ceq_event_cb	ceq_cb[HINIC_MAX_CEQ_EVENTS];
	void			*ceq_data[HINIC_MAX_CEQ_EVENTS];
	unsigned long		ceq_cb_state[HINIC_MAX_CEQ_EVENTS];

	struct hinic_eq		ceq[HINIC_MAX_CEQS];
	u16			num_ceqs;
};

enum hinic_msg_pipe_state {
	PIPE_STATE_IDLE,
	PIPE_STATE_BUSY,
	PIPE_STATE_SUSPEND,
};

#define PIPE_CYCLE_MAX 10000

u32 hinic_func_busy_state_get(struct hinic_hwdev *hwdev);

void hinic_func_busy_state_set(struct hinic_hwdev *hwdev, u32 cfg);

u32 hinic_func_own_bit_get(struct hinic_hwdev *hwdev);

void hinic_func_own_bit_set(struct hinic_hwdev *hwdev, u32 cfg);

int hinic_aeqs_init(struct hinic_hwdev *hwdev, u16 num_aeqs,
		    struct irq_info *msix_entries);

void hinic_aeqs_free(struct hinic_hwdev *hwdev);

int hinic_ceqs_init(struct hinic_hwdev *hwdev, u16 num_ceqs,
		    struct irq_info *msix_entries);

void hinic_ceqs_free(struct hinic_hwdev *hwdev);

void hinic_get_ceq_irqs(struct hinic_hwdev *hwdev, struct irq_info *irqs,
			u16 *num_irqs);

void hinic_get_aeq_irqs(struct hinic_hwdev *hwdev, struct irq_info *irqs,
			u16 *num_irqs);

void hinic_dump_ceq_info(struct hinic_hwdev *hwdev);

void hinic_dump_aeq_info(struct hinic_hwdev *hwdev);

#endif
