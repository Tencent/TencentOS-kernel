/*
 *   Copyright (C) 2019 Tencent Ltd. All rights reserved.
 *
 *   File Name ：batch.h
 *   Author    ：
 *   Date      ：2019-12-26
 *   Descriptor：
 */

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BATCH_H
#define _BATCH_H

#include <linux/sched.h>
#include <uapi/linux/sched.h>

/* nflag of task_struct */
#define TNF_SCHED_BT    0x00000001


struct bt_rq {
	struct load_weight load;
	unsigned int nr_running, h_nr_running;
	unsigned long nr_uninterruptible;

	u64 exec_clock;
	u64 min_vruntime;
#ifndef CONFIG_64BIT
	u64 min_vruntime_copy;
#endif

	struct rb_root_cached tasks_timeline;
	struct rb_node *rb_leftmost;

	/*
	 * 'curr' points to currently running entity on this bt_rq.
	 * It is set to NULL otherwise (i.e when none are currently running).
	 */
	struct sched_entity *curr, *next, *last, *skip;

#ifdef	CONFIG_SCHED_DEBUG
	unsigned int nr_spread_over;
#endif

#ifdef CONFIG_SMP
/*
 * Load-tracking only depends on SMP, BT_GROUP_SCHED dependency below may be
 * removed when useful for applications beyond shares distribution (e.g.
 * load-balance).
 */


	/*
	 *   h_load = weight * f(tg)
	 *
	 * Where f(tg) is the recursive weight fraction assigned to
	 * this group.
	 */
	unsigned long h_load;
#endif /* CONFIG_SMP */
};

extern const struct sched_class bt_sched_class;

extern void init_bt_rq(struct bt_rq *bt_rq);
extern struct sched_entity *__pick_first_bt_entity(struct bt_rq *bt_rq);
extern struct sched_entity *__pick_last_bt_entity(struct bt_rq *bt_rq);
extern void set_bt_load_weight(struct task_struct *p);


static inline int bt_policy(int policy)
{
	if (policy == SCHED_BT)
		return 1;
	return 0;
}

static inline int task_has_bt_policy(struct task_struct *p)
{
	return bt_policy(p->policy);
}

#endif
