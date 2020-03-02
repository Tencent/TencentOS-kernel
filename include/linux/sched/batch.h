/*
 *   Copyright (C) 2019 Tencent Ltd. All rights reserved.
 *
 *   File Name ：batch.h
 *   Author    ：
 *   Date      ：2019-12-26
 *   Descriptor：
 */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SCHED_BATCH_H
#define _SCHED_BATCH_H

#define MAX_CFS_PRIO		139
#define MIN_BT_PRIO			140
#define MAX_BT_PRIO			179
#define BT_PRIO_WIDTH		(MAX_BT_PRIO - MIN_BT_PRIO + 1)

/*
 * Convert user-nice values [ -20 ... 0 ... 19 ]
 * to bt static priority [ MIN_BT_PRI + 1 ..MAX_BT_PRIO ],
 * and back.
 */
#define NICE_TO_BT_PRIO(nice)	(MAX_RT_PRIO + (nice) + 20 + 40)
#define PRIO_TO_BT_NICE(prio)	((prio) - MAX_RT_PRIO - 20 - 40)
#define TASK_BT_NICE(p)		    PRIO_TO_BT_NICE((p)->static_prio)

extern unsigned int sched_bt_on;

static inline int cfs_prio(int prio)
{
	if (prio >= MAX_RT_PRIO && prio < MIN_BT_PRIO)
		return 1;
	return 0;
}

static inline int bt_prio(int prio)
{
	if (prio > MAX_CFS_PRIO && prio < MAX_PRIO)
		return 1;
	return 0;
}

static inline void bt_prio_adjust_pos(int *prio)
{
	int priority = *prio;

	if (cfs_prio(priority))
		*prio = priority + BT_PRIO_WIDTH;
}

static inline void bt_prio_adjust_neg(int *prio)
{
	int priority = *prio;

	if (bt_prio(priority))
		*prio = priority - BT_PRIO_WIDTH;
}

#endif /* _SCHED_BATCH_H */
