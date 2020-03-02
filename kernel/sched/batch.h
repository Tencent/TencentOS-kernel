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

struct bt_bandwidth {
	raw_spinlock_t	bt_runtime_lock;
	ktime_t         bt_period;
	u64             bt_runtime;
	struct hrtimer  bt_period_timer;
	int             timer_active;
};

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
#ifdef CONFIG_BT_GROUP_SCHED
	/*
	 * BT Load tracking
	 */
	struct sched_avg_bt avg;
	u64 runnable_load_sum;
	unsigned long runnable_load_avg;


	unsigned long tg_load_avg_contrib;
#endif /* CONFIG_BT_GROUP_SCHED */
	atomic_long_t removed_load_avg, removed_util_avg;
#ifndef CONFIG_64BIT
	u64 load_last_update_time_copy;
#endif

	/*
	 *   h_load = weight * f(tg)
	 *
	 * Where f(tg) is the recursive weight fraction assigned to
	 * this group.
	 */
	unsigned long h_load;
#endif /* CONFIG_SMP */
#ifdef CONFIG_BT_GROUP_SCHED
	struct rq *rq;  /* cpu runqueue to which this cfs_rq is attached */

	/*
	 * leaf cfs_rqs are those that hold tasks (lowest schedulable entity in
	 * a hierarchy). Non-leaf lrqs hold other higher schedulable entities
	 * (like users, containers etc.)
	 *
	 * leaf_cfs_rq_list ties together list of leaf cfs_rq's in a cpu. This
	 * list is used during load balance.
	 */
	int on_list;
	struct list_head leaf_bt_rq_list;
	struct task_group *tg;  /* group that "owns" this runqueue */
#endif /* CONFIG_BT_GROUP_SCHED */

	int bt_throttled;
	u64 bt_time;
	u64 bt_runtime;

	u64 throttled_clock, throttled_clock_task;
	u64 throttled_clock_task_time;

	/* Nests inside the rq lock: */
	raw_spinlock_t bt_runtime_lock;
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

extern int update_bt_runtime(struct notifier_block *nfb, unsigned long action, void *hcpu);

extern struct bt_bandwidth def_bt_bandwidth;
extern void init_bt_bandwidth(struct bt_bandwidth *bt_b, u64 period, u64 runtime);

#endif
