/*
 *   Copyright (C) 2019 Tencent Ltd. All rights reserved.
 *
 *   File Name : batch.c
 *   Author    :
 *   Date      : 2019-12-26
 *   Descriptor:
 */

#include <linux/sysctl.h>
#include <linux/latencytop.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <linux/profile.h>
#include <linux/interrupt.h>
#include <linux/mempolicy.h>
#include <linux/migrate.h>
#include <linux/task_work.h>
#include <linux/hrtimer.h>
#include <linux/sched/batch.h>
#include <linux/proc_fs.h>
#include <linux/kfifo.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>

#include <trace/events/sched.h>
#include "sched.h"
#include "batch.h"
#include "fair.h"
#include "bt_debug.h"

void set_bt_load_weight(struct task_struct *p)
{
	int prio = p->static_prio - MIN_BT_PRIO;
	struct load_weight *load = &p->bt.load;

	load->weight = scale_load(sched_prio_to_weight[prio]);
	load->inv_weight = sched_prio_to_wmult[prio];
}

extern unsigned int offlinegroup_enabled;
const struct sched_class bt_sched_class;
unsigned int sysctl_idle_balance_bt_cost = 300000UL;
unsigned int sysctl_sched_bt_granularity_ns = 4000000;
unsigned int sysctl_sched_bt_load_fair = 1;
void * bt_cpu_control_set = 0;
unsigned int sysctl_sched_bt_ignore_cpubind = 0;

/*
 * sd_lb_stats_bt - Structure to store the statistics of a sched_domain
 * 		during load balancing.
 */
struct sd_lb_stats_bt {
	struct sched_group *busiest; /* Busiest group in this sd */
	struct sched_group *this;  /* Local group in this sd */
	unsigned long total_load;  /* Total load of all groups in sd */
	unsigned long total_bt_load;  /* Total load of all groups in sd */
	unsigned long total_pwr;   /*	Total power of all groups in sd */
	unsigned long avg_load;	   /* Average load across all groups in sd */
	unsigned long avg_bt_load;	   /* Average load across all groups in sd */

	/** Statistics of this group */
	unsigned long this_load;
	unsigned long this_bt_load;
	unsigned long this_load_per_task;
	unsigned long this_nr_running;
	unsigned long this_has_capacity;
	unsigned int  this_idle_cpus;

	/* Statistics of the busiest group */
	unsigned int  busiest_idle_cpus;
	unsigned long max_load;
	unsigned long max_bt_load;
	unsigned long busiest_load_per_task;
	unsigned long busiest_nr_running;
	unsigned long busiest_group_capacity;
	unsigned long busiest_has_capacity;
	unsigned int  busiest_group_weight;

	int group_imb; /* Is there imbalance in this sd */
};

/*
 * sg_lb_stats_bt - stats of a sched_group required for load_balancing
 */
struct sg_lb_stats_bt {
	unsigned long avg_load; /*Avg load across the CPUs of the group */
	unsigned long avg_bt_load; /*Avg load across the CPUs of the group */
	unsigned long group_load; /* Total load over the CPUs of the group */
	unsigned long group_bt_load; /* Total load over the CPUs of the group */
	unsigned long sum_nr_running; /* Nr tasks running in the group */
	unsigned long sum_weighted_load; /* Weighted load of group's tasks */
	unsigned long group_capacity;
	unsigned long idle_cpus;
	unsigned long group_weight;
	int group_imb; /* Is there an imbalance in the group ? */
	int group_has_capacity; /* Is there extra capacity in the group? */
};

/**
 * get_sd_load_idx_bt - Obtain the load index for a given sched domain.
 * @sd: The sched_domain whose load_idx is to be obtained.
 * @idle: The Idle status of the CPU for whose sd load_icx is obtained.
 */
static inline int get_sd_load_idx_bt(struct sched_domain *sd,
					enum cpu_idle_type idle)
{
	int load_idx;

	switch (idle) {
	case CPU_NOT_IDLE:
		load_idx = sd->busy_idx;
		break;

	case CPU_NEWLY_IDLE:
		load_idx = sd->newidle_idx;
		break;
	default:
		load_idx = sd->idle_idx;
		break;
	}

	return load_idx;
}

/*
 * move_task_bt - move a task from one runqueue to another runqueue.
 * Both runqueues must be locked.
 */
static void move_task_bt(struct task_struct *p, struct lb_env *env)
{
	deactivate_task(env->src_rq, p, 0);
	set_task_cpu(p, env->dst_cpu);
	activate_task(env->dst_rq, p, 0);
	check_preempt_curr(env->dst_rq, p, 0);
}

/**************************************************************
 * BT operations on generic schedulable entities:
 */

#ifdef CONFIG_BT_GROUP_SCHED

/* cpu runqueue to which this cfs_rq is attached */
static inline struct rq *rq_of_bt_rq(struct bt_rq *bt_rq)
{
	return bt_rq->rq;
}

/* An entity is a task if it doesn't "own" a runqueue */
#define bt_entity_is_task(se)	(!se->bt_my_q)

static inline struct task_struct *bt_task_of(struct sched_entity *bt)
{
#ifdef CONFIG_SCHED_DEBUG
	WARN_ON_ONCE(!bt_entity_is_task(bt));
#endif
	return container_of(bt, struct task_struct, bt);
}

/* Walk up scheduling entities hierarchy */
#define for_each_sched_bt_entity(se) \
		for (; se; se = se->parent)

static inline struct bt_rq *task_bt_rq(struct task_struct *p)
{
	return p->bt.bt_rq;
}

/* runqueue on which this entity is (to be) queued */
static inline struct bt_rq *bt_rq_of(struct sched_entity *se)
{
	return se->bt_rq;
}

/* runqueue "owned" by this group */
static inline struct bt_rq *group_bt_rq(struct sched_entity *grp)
{
	return grp->bt_my_q;
}

static inline void list_add_leaf_bt_rq(struct bt_rq *bt_rq)
{
	if (!bt_rq->on_list) {
		/*
		 * Ensure we either appear before our parent (if already
		 * enqueued) or force our parent to appear after us when it is
		 * enqueued.  The fact that we always enqueue bottom-up
		 * reduces this to two cases.
		 */
		if (bt_rq->tg->parent &&
			bt_rq->tg->parent->bt_rq[cpu_of(rq_of_bt_rq(bt_rq))]->on_list) {
			list_add_rcu(&bt_rq->leaf_bt_rq_list,
				&rq_of_bt_rq(bt_rq)->leaf_bt_rq_list);
		} else {
			list_add_tail_rcu(&bt_rq->leaf_bt_rq_list,
				&rq_of_bt_rq(bt_rq)->leaf_bt_rq_list);
		}

		bt_rq->on_list = 1;
	}
}

static inline void list_del_leaf_bt_rq(struct bt_rq *bt_rq)
{
	if (bt_rq->on_list) {
		list_del_rcu(&bt_rq->leaf_bt_rq_list);
		bt_rq->on_list = 0;
	}
}

/* Do the two (enqueued) entities belong to the same group ? */
static inline int
bt_is_same_group(struct sched_entity *se, struct sched_entity *pse)
{
	if (se->bt_rq == pse->bt_rq)
		return 1;

	return 0;
}

static inline struct sched_entity *parent_bt_entity(struct sched_entity *se)
{
	return se->parent;
}

/* return depth at which a sched entity is present in the hierarchy */
static inline int depth_bt(struct sched_entity *se)
{
	int depth = 0;

	for_each_sched_bt_entity(se)
		depth++;

	return depth;
}

static void
find_matching_bt(struct sched_entity **se, struct sched_entity **pse)
{
	int se_depth, pse_depth;

	/*
	 * preemption test can be made between sibling entities who are in the
	 * same cfs_rq i.e who have a common parent. Walk up the hierarchy of
	 * both tasks until we find their ancestors who are siblings of common
	 * parent.
	 */

	/* First walk up until both entities are at same depth */
	se_depth = depth_bt(*se);
	pse_depth = depth_bt(*pse);

	while (se_depth > pse_depth) {
		se_depth--;
		*se = parent_bt_entity(*se);
	}

	while (pse_depth > se_depth) {
		pse_depth--;
		*pse = parent_bt_entity(*pse);
	}

	while (!bt_is_same_group(*se, *pse)) {
		*se = parent_bt_entity(*se);
		*pse = parent_bt_entity(*pse);
	}
}

#else	/* !CONFIG_BT_GROUP_SCHED */

static inline struct task_struct *bt_task_of(struct sched_entity *bt_se)
{
	return container_of(bt_se, struct task_struct, bt);
}

static inline struct rq *rq_of_bt_rq(struct bt_rq *bt_rq)
{
	return container_of(bt_rq, struct rq, bt);
}

#define bt_entity_is_task(bt)	1

#define for_each_sched_bt_entity(bt) \
		for (; bt; bt = NULL)

static inline struct bt_rq *task_bt_rq(struct task_struct *p)
{
	return &task_rq(p)->bt;
}

static inline struct bt_rq *bt_rq_of(struct sched_entity *bt_se)
{
	struct task_struct *p = bt_task_of(bt_se);
	struct rq *rq = task_rq(p);

	return &rq->bt;
}

/* runqueue "owned" by this group */
static inline struct bt_rq *group_bt_rq(struct sched_entity *grp)
{
	return NULL;
}

static inline void list_add_leaf_bt_rq(struct bt_rq *bt_rq)
{
}

static inline void list_del_leaf_bt_rq(struct bt_rq *bt_rq)
{
}

static inline int
bt_is_same_group(struct sched_entity *se, struct sched_entity *pse)
{
	return 1;
}

static inline struct sched_entity *parent_bt_entity(struct sched_entity *bt)
{
	return NULL;
}

static inline void
find_matching_bt(struct sched_entity **se, struct sched_entity **pse)
{
}
#endif	/* CONFIG_BT_GROUP_SCHED */

static int do_sched_bt_period_timer(struct bt_bandwidth *bt_b, int overrun);

struct bt_bandwidth def_bt_bandwidth;

static enum hrtimer_restart sched_bt_period_timer(struct hrtimer *timer)
{
	struct bt_bandwidth *bt_b =
		container_of(timer, struct bt_bandwidth, bt_period_timer);
	int overrun;
	int idle = 0;

	for (;;) {
		overrun = hrtimer_forward_now(timer, bt_b->bt_period);
		if (!overrun){
			break;
		}

		idle = do_sched_bt_period_timer(bt_b, overrun);
	}

	return idle ? HRTIMER_NORESTART : HRTIMER_RESTART;
}

void init_bt_bandwidth(struct bt_bandwidth *bt_b, u64 period, u64 runtime)
{
	bt_b->bt_period = ns_to_ktime(period);
	bt_b->bt_runtime = runtime;
	bt_b->timer_active = 0;

	raw_spin_lock_init(&bt_b->bt_runtime_lock);

	hrtimer_init(&bt_b->bt_period_timer,
			CLOCK_MONOTONIC, HRTIMER_MODE_ABS_PINNED);
	bt_b->bt_period_timer.function = sched_bt_period_timer;
}

static void start_bandwidth_timer(struct hrtimer *period_timer, ktime_t period)
{
	unsigned long delta;
	ktime_t soft, hard, now;

	for (;;) {
		if (hrtimer_active(period_timer))
			break;

		now = hrtimer_cb_get_time(period_timer);
		hrtimer_forward(period_timer, now, period);

		soft = hrtimer_get_softexpires(period_timer);
		hard = hrtimer_get_expires(period_timer);
		delta = ktime_to_ns(ktime_sub(hard, soft));
		hrtimer_start_range_ns(period_timer, soft, delta,
					 HRTIMER_MODE_ABS_PINNED);
	}
}

static void start_bt_bandwidth(struct bt_bandwidth *bt_b)
{
	if (!offlinegroup_enabled &&
	    (!bt_bandwidth_enabled() || bt_b->bt_runtime == RUNTIME_INF))
		return;

	if (hrtimer_active(&bt_b->bt_period_timer))
		return;

	raw_spin_lock(&bt_b->bt_runtime_lock);
	bt_b->timer_active = 1;
	start_bandwidth_timer(&bt_b->bt_period_timer, bt_b->bt_period);
	raw_spin_unlock(&bt_b->bt_runtime_lock);
}

static inline u64 sched_bt_runtime(struct bt_rq *bt_rq)
{
	return bt_rq->bt_runtime;
}

static inline u64 sched_bt_period(struct bt_rq *bt_rq)
{
	return ktime_to_ns(def_bt_bandwidth.bt_period);
}

typedef struct bt_rq *bt_rq_iter_t;

#define for_each_bt_rq(bt_rq, iter, rq) \
	for ((void) iter, bt_rq = &rq->bt; bt_rq; bt_rq = NULL)

static inline int bt_rq_throttled(struct bt_rq *bt_rq)
{
	return bt_rq->bt_throttled;
}

static inline void sched_bt_rq_enqueue(struct bt_rq *bt_rq)
{
	struct rq *rq = rq_of_bt_rq(bt_rq);

	if (rq->curr == rq->idle)
		resched_curr(rq);
}

static inline const struct cpumask *sched_bt_period_mask(void)
{
	return cpu_online_mask;
}

static inline
struct bt_rq *sched_bt_period_bt_rq(struct bt_bandwidth *bt_b, int cpu)
{
	return &cpu_rq(cpu)->bt;
}

static inline struct bt_bandwidth *sched_bt_bandwidth(struct bt_rq *bt_rq)
{
	return &def_bt_bandwidth;
}

#ifdef CONFIG_SMP
static unsigned long capacity_of_bt(int cpu)
{
	return cpu_rq(cpu)->cpu_capacity;
}

/*
 * We ran out of runtime, see if we can borrow some from our neighbours.
 */
static int do_balance_bt_runtime(struct bt_rq *bt_rq)
{
	struct bt_bandwidth *bt_b = sched_bt_bandwidth(bt_rq);
	struct root_domain *rd = rq_of_bt_rq(bt_rq)->rd;
	int i, weight, more = 0;
	u64 bt_period;

	weight = cpumask_weight(rd->span);

	raw_spin_lock(&bt_b->bt_runtime_lock);
	bt_period = ktime_to_ns(bt_b->bt_period);
	for_each_cpu(i, rd->span) {
		struct bt_rq *iter = sched_bt_period_bt_rq(bt_b, i);
		s64 diff;

		if (iter == bt_rq)
			continue;

		raw_spin_lock(&iter->bt_runtime_lock);
		/*
		 * Either all rqs have inf runtime and there's nothing to steal
		 * or __disable_runtime() below sets a specific rq to inf to
		 * indicate its been disabled and disalow stealing.
		 */
		if (iter->bt_runtime == RUNTIME_INF)
			goto next;

		/*
		 * From runqueues with spare time, take 1/n part of their
		 * spare time, but no more than our period.
		 */
		diff = iter->bt_runtime - iter->bt_time;
		if (diff > 0) {
			diff = div_u64((u64)diff, weight);
			if (bt_rq->bt_runtime + diff > bt_period)
				diff = bt_period - bt_rq->bt_runtime;
			iter->bt_runtime -= diff;
			bt_rq->bt_runtime += diff;
			more = 1;
			if (bt_rq->bt_runtime == bt_period) {
				raw_spin_unlock(&iter->bt_runtime_lock);
				break;
			}
		}
next:
		raw_spin_unlock(&iter->bt_runtime_lock);
	}
	raw_spin_unlock(&bt_b->bt_runtime_lock);

	return more;
}

/*
 * Ensure this BT takes back all the runtime it lend to its neighbours.
 */
static void __disable_bt_runtime(struct rq *rq)
{
	struct root_domain *rd = rq->rd;
	bt_rq_iter_t iter;
	struct bt_rq *bt_rq;

	if (unlikely(!scheduler_running))
		return;

	for_each_bt_rq(bt_rq, iter, rq) {
		struct bt_bandwidth *bt_b = sched_bt_bandwidth(bt_rq);
		s64 want;
		int i;

		raw_spin_lock(&bt_b->bt_runtime_lock);
		raw_spin_lock(&bt_rq->bt_runtime_lock);
		/*
		 * Either we're all inf and nobody needs to borrow, or we're
		 * already disabled and thus have nothing to do, or we have
		 * exactly the right amount of runtime to take out.
		 */
		if (bt_rq->bt_runtime == RUNTIME_INF ||
				bt_rq->bt_runtime == bt_b->bt_runtime)
			goto balanced;
		raw_spin_unlock(&bt_rq->bt_runtime_lock);

		/*
		 * Calculate the difference between what we started out with
		 * and what we current have, that's the amount of runtime
		 * we lend and now have to reclaim.
		 */
		want = bt_b->bt_runtime - bt_rq->bt_runtime;

		/*
		 * Greedy reclaim, take back as much as we can.
		 */
		for_each_cpu(i, rd->span) {
			struct bt_rq *iter = sched_bt_period_bt_rq(bt_b, i);
			s64 diff;

			/*
			 * Can't reclaim from ourselves or disabled runqueues.
			 */
			if (iter == bt_rq || iter->bt_runtime == RUNTIME_INF)
				continue;

			raw_spin_lock(&iter->bt_runtime_lock);
			if (want > 0) {
				diff = min_t(s64, iter->bt_runtime, want);
				iter->bt_runtime -= diff;
				want -= diff;
			} else {
				iter->bt_runtime -= want;
				want -= want;
			}
			raw_spin_unlock(&iter->bt_runtime_lock);

			if (!want)
				break;
		}

		raw_spin_lock(&bt_rq->bt_runtime_lock);
		/*
		 * We cannot be left wanting - that would mean some runtime
		 * leaked out of the system.
		 */
		BUG_ON(want);
balanced:
		/*
		 * Disable all the borrow logic by pretending we have inf
		 * runtime - in which case borrowing doesn't make sense.
		 */
		bt_rq->bt_runtime = RUNTIME_INF;
		bt_rq->bt_throttled = 0;
		raw_spin_unlock(&bt_rq->bt_runtime_lock);
		raw_spin_unlock(&bt_b->bt_runtime_lock);
	}
}

static void __enable_bt_runtime(struct rq *rq)
{
	bt_rq_iter_t iter;
	struct bt_rq *bt_rq;

	if (unlikely(!scheduler_running))
		return;

	/*
	 * Reset each runqueue's bandwidth settings
	 */
	for_each_bt_rq(bt_rq, iter, rq) {
		struct bt_bandwidth *bt_b = sched_bt_bandwidth(bt_rq);

		raw_spin_lock(&bt_b->bt_runtime_lock);
		raw_spin_lock(&bt_rq->bt_runtime_lock);
		bt_rq->bt_runtime = bt_b->bt_runtime;
		bt_rq->bt_time = 0;
		bt_rq->bt_throttled = 0;
		raw_spin_unlock(&bt_rq->bt_runtime_lock);
		raw_spin_unlock(&bt_b->bt_runtime_lock);
	}
}

#if 0
int update_bt_runtime(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	int cpu = (int)(long)hcpu;

	switch (action) {
	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		disable_bt_runtime(cpu_rq(cpu));
		return NOTIFY_OK;

	case CPU_DOWN_FAILED:
	case CPU_DOWN_FAILED_FROZEN:
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		enable_bt_runtime(cpu_rq(cpu));
		return NOTIFY_OK;

	default:
		return NOTIFY_DONE;
	}
}
#endif

static int balance_bt_runtime(struct bt_rq *bt_rq)
{
	int more = 0;

	if (offlinegroup_enabled || !sched_feat(BT_RUNTIME_SHARE))
		return more;

	if (bt_rq->bt_time > bt_rq->bt_runtime) {
		raw_spin_unlock(&bt_rq->bt_runtime_lock);
		more = do_balance_bt_runtime(bt_rq);
		raw_spin_lock(&bt_rq->bt_runtime_lock);
	}

	return more;
}
#else /* !CONFIG_SMP */
static inline int balance_bt_runtime(struct bt_rq *bt_rq)
{
	return 0;
}
#endif /* CONFIG_SMP */

static void
dequeue_bt_entity(struct bt_rq *bt_rq, struct sched_entity *se, int flags);

static void throttle_bt_rq(struct bt_rq *bt_rq)
{
	struct rq *rq = rq_of_bt_rq(bt_rq);
	struct sched_entity *bt;
	long task_delta, dequeue = 1;

	bt = bt_rq->tg->bt[cpu_of(rq)];

	task_delta = bt_rq->h_nr_running;
	for_each_sched_bt_entity(bt) {
		struct bt_rq *qbt_rq = bt_rq_of(bt);

		if (!bt->on_rq)
			break;

		if (dequeue)
			dequeue_bt_entity(qbt_rq, bt, DEQUEUE_SLEEP);
		qbt_rq->h_nr_running -= task_delta;

		if (qbt_rq->load.weight)
			dequeue = 0;
	}

	if (!bt) {
		rq->nr_running -= task_delta;
		rq->bt_nr_running -= task_delta;

		if (!rq->bt_nr_running && task_delta && !rq->bt_blocked_clock){
			rq->bt_blocked_clock = rq_clock(rq);
		}
	}

	bt_rq->bt_throttled = 1;
	bt_rq->throttled_clock = rq_clock(rq);
	bt_rq->throttled_clock_task = rq_clock_task(rq);
}

static void
enqueue_bt_entity(struct bt_rq *bt_rq, struct sched_entity *se, int flags);
void unthrottle_bt_rq(struct bt_rq *bt_rq)
{
	struct rq *rq = rq_of_bt_rq(bt_rq);
	struct sched_entity *bt;
	long task_delta, enqueue = 1;

	bt = bt_rq->tg->bt[cpu_of(rq)];

	bt_rq->bt_throttled = 0;

	update_rq_clock(rq);

	bt_rq->throttled_clock_task_time += rq_clock_task(rq) - bt_rq->throttled_clock_task;
	if (!bt_rq->load.weight)
		return;

	task_delta = bt_rq->h_nr_running;
	for_each_sched_bt_entity(bt) {
		struct bt_rq *qbt_rq = bt_rq_of(bt);

		if (bt->on_rq)
			enqueue = 0;

		if (enqueue)
			enqueue_bt_entity(qbt_rq, bt, ENQUEUE_WAKEUP);
		qbt_rq->h_nr_running += task_delta;

		if (bt_rq_throttled(bt_rq))
			break;
	}

	if (!bt) {
		rq->nr_running += task_delta;
		rq->bt_nr_running += task_delta;

		if (!rq->bt_nr_running && task_delta && !rq->bt_blocked_clock){
			rq->bt_blocked_clock = rq_clock(rq);
		}
	}
}

/* rq->task_clock normalized against any time this bt_rq has spent throttled */
static inline u64 bt_rq_clock_task(struct bt_rq *bt_rq)
{
	struct rq *rq = rq_of_bt_rq(bt_rq);

	if (unlikely(bt_rq_throttled(bt_rq)))
		return bt_rq->throttled_clock_task;

	return rq_clock_task(rq) - bt_rq->throttled_clock_task_time;
}

static int do_sched_bt_period_timer(struct bt_bandwidth *bt_b, int overrun)
{
	int i, idle = 1, throttled = 0;
	const struct cpumask *span;

	span = sched_bt_period_mask();

	for_each_cpu(i, span) {
		int enqueue = 0;
		struct bt_rq *bt_rq = sched_bt_period_bt_rq(bt_b, i);
		struct rq *rq = rq_of_bt_rq(bt_rq);

		raw_spin_lock(&rq->lock);
		if (bt_rq->bt_time) {
			u64 runtime;

			raw_spin_lock(&bt_rq->bt_runtime_lock);
			if (bt_rq->bt_throttled)
				balance_bt_runtime(bt_rq);
			runtime = bt_rq->bt_runtime;
			bt_rq->bt_time -= min(bt_rq->bt_time, overrun*runtime);
			if (bt_rq->bt_throttled && bt_rq->bt_time < runtime) {
				enqueue = 1;
				unthrottle_bt_rq(bt_rq);
#if 0
				/*
				 * Force a clock update if the CPU was idle,
				 * lest wakeup -> unthrottle time accumulate.
				 */
				if (bt_rq->nr_running && rq->curr == rq->idle)
					rq->skip_clock_update = -1;
#endif
			}
			if (bt_rq->bt_time || bt_rq->nr_running)
				idle = 0;
			raw_spin_unlock(&bt_rq->bt_runtime_lock);
		} else if (bt_rq->nr_running) {
			idle = 0;
			if (!bt_rq_throttled(bt_rq))
				enqueue = 1;
		}
		if (bt_rq->bt_throttled)
			throttled = 1;

		if (enqueue)
			sched_bt_rq_enqueue(bt_rq);
		raw_spin_unlock(&rq->lock);
	}

	if (!throttled && !offlinegroup_enabled &&
	    (!bt_bandwidth_enabled() || bt_b->bt_runtime == RUNTIME_INF))
		idle = 1;

	if (idle)
		bt_b->timer_active = 0;

	return idle;
}


static int sched_bt_runtime_exceeded(struct bt_rq *bt_rq)
{
	u64 runtime = sched_bt_runtime(bt_rq);

	if (bt_rq->bt_throttled)
		return bt_rq_throttled(bt_rq);

	if (runtime >= sched_bt_period(bt_rq))
		return 0;

	balance_bt_runtime(bt_rq);
	runtime = sched_bt_runtime(bt_rq);
	if (runtime == RUNTIME_INF)
		return 0;

	if (bt_rq->bt_time > runtime) {
		struct bt_bandwidth *bt_b = sched_bt_bandwidth(bt_rq);

		/*
		 * Don't actually throttle groups that have no runtime assigned
		 * but accrue some time due to boosting.
		 */
		if (!offlinegroup_enabled) {
			if (likely(bt_b->bt_runtime)) {
				static bool once = false;

				throttle_bt_rq(bt_rq);

				if (!once) {
					once = true;
					printk_deferred("sched: BT throttling activated\n");
				}
			} else {
				/*
				 * In case we did anyway, make it go away,
				 * replenishment is a joke, since it will replenish us
				 * with exactly 0 ns.
				 */
				bt_rq->bt_time = 0;
			}
		} else {
			throttle_bt_rq(bt_rq);
		}

		if (bt_rq_throttled(bt_rq)) {
			if (!bt_b->timer_active)
				start_bt_bandwidth(bt_b);
			return 1;
		}
	}

	return 0;
}

static void
account_bt_rq_runtime(struct bt_rq *bt_rq,
			unsigned long delta_exec)
{
	if (!offlinegroup_enabled &&
	    (!bt_bandwidth_enabled() || sched_bt_runtime(bt_rq) == RUNTIME_INF))
		return;

	raw_spin_lock(&bt_rq->bt_runtime_lock);
	bt_rq->bt_time += delta_exec;
	if (sched_bt_runtime_exceeded(bt_rq) && likely(bt_rq->curr)){
		resched_curr(rq_of_bt_rq(bt_rq));
	}
	raw_spin_unlock(&bt_rq->bt_runtime_lock);
}

/**************************************************************
 * Scheduling class tree data structure manipulation methods:
 */

static inline int bt_entity_before(struct sched_entity *a,
				struct sched_entity *b)
{
	return (s64)(a->vruntime - b->vruntime) < 0;
}

static void update_bt_min_vruntime(struct bt_rq *bt_rq)
{
	struct sched_entity *curr = bt_rq->curr;
	struct rb_node *leftmost = rb_first_cached(&bt_rq->tasks_timeline);

	u64 vruntime = bt_rq->min_vruntime;

	if (curr) {
		if (curr->on_rq)
			vruntime = bt_rq->curr->vruntime;
		else
			curr = NULL;
	}

	if (leftmost) {
		struct sched_entity *bt_se = rb_entry(leftmost,
						   struct sched_entity,
						   run_node);

		if (!curr)
			vruntime = bt_se->vruntime;
		else
			vruntime = min_vruntime(vruntime, bt_se->vruntime);
	}

	/* ensure we never gain time by being placed backwards. */
	bt_rq->min_vruntime = max_vruntime(bt_rq->min_vruntime, vruntime);
#ifndef CONFIG_64BIT
	/* memory barrior for writting */
	smp_wmb();
	bt_rq->min_vruntime_copy = bt_rq->min_vruntime;
#endif
}

/*
 * Enqueue an entity into the rb-tree:
 */
static void __enqueue_bt_entity(struct bt_rq *bt_rq, struct sched_entity *bt_se)
{
	struct rb_node **link = &bt_rq->tasks_timeline.rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct sched_entity *entry;
	int leftmost = 1;

	/*
	 * Find the right place in the rbtree:
	 */
	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct sched_entity, run_node);
		/*
		 * We dont care about collisions. Nodes with
		 * the same key stay together.
		 */
		if (bt_entity_before(bt_se, entry)) {
			link = &parent->rb_left;
		} else {
			link = &parent->rb_right;
			leftmost = 0;
		}
	}

	rb_link_node(&bt_se->run_node, parent, link);
	rb_insert_color_cached(&bt_se->run_node,
					&bt_rq->tasks_timeline, leftmost);
}

static void __dequeue_bt_entity(struct bt_rq *bt_rq, struct sched_entity *bt_se)
{
	rb_erase_cached(&bt_se->run_node, &bt_rq->tasks_timeline);
}

struct sched_entity *__pick_first_bt_entity(struct bt_rq *bt_rq)
{
	struct rb_node *left = rb_first_cached(&bt_rq->tasks_timeline);

	if (!left)
		return NULL;

	return rb_entry(left, struct sched_entity, run_node);
}

static struct sched_entity *__pick_next_bt_entity(struct sched_entity *bt_se)
{
	struct rb_node *next = rb_next(&bt_se->run_node);

	if (!next)
		return NULL;

	return rb_entry(next, struct sched_entity, run_node);
}

#ifdef CONFIG_SCHED_DEBUG
struct sched_entity *__pick_last_bt_entity(struct bt_rq *bt_rq)
{
	struct rb_node *last = rb_last(&bt_rq->tasks_timeline.rb_root);

	if (!last)
		return NULL;

	return rb_entry(last, struct sched_entity, run_node);
}
#endif

/*
 * delta /= w
 */
static inline unsigned long
calc_delta_bt(unsigned long delta, struct sched_entity *bt_se)
{
	if (unlikely(bt_se->load.weight != NICE_0_LOAD))
		delta = __calc_delta(delta, NICE_0_LOAD, &bt_se->load);

	return delta;
}

/*
 * We calculate the wall-time slice from the period by taking a part
 * proportional to the weight.
 *
 * s = p*P[w/rw]
 */
static u64 sched_bt_slice(struct bt_rq *bt_rq, struct sched_entity *se)
{
	u64 slice = __sched_period(bt_rq->nr_running + !se->on_rq);

	for_each_sched_bt_entity(se) {
		struct load_weight *load;
		struct load_weight lw;

		bt_rq = bt_rq_of(se);
		load = &bt_rq->load;

		if (unlikely(!se->on_rq)) {
			lw = bt_rq->load;

			update_load_add(&lw, se->load.weight);
			load = &lw;
		}
		slice = __calc_delta(slice, se->load.weight, load);
	}
	return slice;
}

/*
 * We calculate the vruntime slice of a to-be-inserted task.
 *
 * vs = s/w
 */
static u64 sched_bt_vslice(struct bt_rq *bt_rq, struct sched_entity *se)
{
	return calc_delta_bt(sched_bt_slice(bt_rq, se), se);
}

#ifdef CONFIG_SMP

/*
 * We choose a half-life close to 1 scheduling period.
 * Note: The tables below are dependent on this value.
 */
#define BT_LOAD_AVG_PERIOD	32
#define BT_LOAD_AVG_MAX		47742	/* maximum possible load avg */
#define BT_LOAD_AVG_MAX_N	345	/* number of full periods to produce LOAD_MAX_AVG */

/* Give new sched_entity start runnable values to heavy its load in infant time */
void init_bt_entity_runnable_average(struct sched_entity *se)
{
	struct sched_avg_bt *sa = &se->bt_avg;

	sa->last_update_time = 0;
	/*
	 * sched_avg's period_contrib should be strictly less then 1024, so
	 * we give it 1023 to make sure it is almost a period (1024us), and
	 * will definitely be update (after enqueue).
	 */
	sa->period_contrib = 1023;
	sa->load_avg = scale_load_down(se->load.weight);
	sa->load_sum = sa->load_avg * BT_LOAD_AVG_MAX;

	/*
	 * At this point, util_avg won't be used in select_task_rq_fair anyway
	 */
	sa->util_avg = 0;
	sa->util_sum = 0;
	/* when this task enqueue'ed, it will contribute to its cfs_rq's load_avg */
}

/*
 * With new tasks being created, their initial util_avgs are extrapolated
 * based on the bt_rq's current util_avg:
 *
 *   util_avg = bt_rq->util_avg / (bt_rq->load_avg + 1) * se.load.weight
 *
 * However, in many cases, the above util_avg does not give a desired
 * value. Moreover, the sum of the util_avgs may be divergent, such
 * as when the series is a harmonic series.
 *
 * To solve this problem, we also cap the util_avg of successive tasks to
 * only 1/2 of the left utilization budget:
 *
 *   util_avg_cap = (1024 - bt_rq->avg.util_avg) / 2^n
 *
 * where n denotes the nth task.
 *
 * For example, a simplest series from the beginning would be like:
 *
 *  task  util_avg: 512, 256, 128,  64,  32,   16,    8, ...
 * cfs_rq util_avg: 512, 768, 896, 960, 992, 1008, 1016, ...
 *
 * Finally, that extrapolated util_avg is clamped to the cap (util_avg_cap)
 * if util_avg > util_avg_cap.
 */
void post_init_bt_entity_util_avg(struct sched_entity *se)
{
	struct bt_rq *bt_rq = bt_rq_of(se);
	struct sched_avg_bt *sa = &se->bt_avg;
	long cap = (long)(scale_load_down(SCHED_LOAD_SCALE) - bt_rq->avg.util_avg) / 2;

	if (cap > 0) {
		if (bt_rq->avg.util_avg != 0) {
			sa->util_avg  = bt_rq->avg.util_avg * se->load.weight;
			sa->util_avg /= (bt_rq->avg.load_avg + 1);

			if (sa->util_avg > cap)
				sa->util_avg = cap;
		} else {
			sa->util_avg = cap;
		}
		sa->util_sum = sa->util_avg * BT_LOAD_AVG_MAX;
	}
}

#else
void init_bt_entity_runnable_average(struct sched_entity *se)
{
}

void post_init_bt_entity_util_avg(struct sched_entity *se)
{
}
#endif

/*
 * Update the current task's runtime bt_statistics. Skip current tasks that
 * are not in our scheduling class.
 */
static inline void
__update_curr_bt(struct bt_rq *bt_rq, struct sched_entity *curr,
		  unsigned long delta_exec)
{
	unsigned long delta_exec_weighted;

	schedstat_set(curr->bt_statistics->exec_max,
			  max((u64)delta_exec, curr->bt_statistics->exec_max));

	curr->sum_exec_runtime += delta_exec;
	schedstat_add(bt_rq->exec_clock, delta_exec);
	delta_exec_weighted = calc_delta_bt(delta_exec, curr);

	curr->vruntime += delta_exec_weighted;
	update_bt_min_vruntime(bt_rq);
}

static void update_curr_bt(struct bt_rq *bt_rq)
{
	struct sched_entity *curr = bt_rq->curr;
	u64 now = rq_clock_task(rq_of_bt_rq(bt_rq));
	unsigned long delta_exec;

	if (unlikely(!curr))
		return;

	/*
	 * Get the amount of time the current task was running
	 * since the last time we changed load (this cannot
	 * overflow on 32 bits):
	 */
	delta_exec = (unsigned long)(now - curr->exec_start);
	if (unlikely((s64)delta_exec <= 0))
		return;

	__update_curr_bt(bt_rq, curr, delta_exec);
	curr->exec_start = now;

	if (bt_entity_is_task(curr)) {
		struct task_struct *curtask = bt_task_of(curr);

		trace_sched_stat_runtime(curtask, delta_exec, curr->vruntime);
		cpuacct_charge(curtask, delta_exec);
		bt_cpuacct_charge(curtask, delta_exec);
		account_group_exec_runtime(curtask, delta_exec);
	}

	account_bt_rq_runtime(bt_rq, delta_exec);
}

static void update_curr_cb_bt(struct rq *rq)
{
	update_curr_bt(bt_rq_of(&rq->curr->bt));
}

static inline void
update_stats_wait_start_bt(struct bt_rq *bt_rq, struct sched_entity *bt_se)
{
	schedstat_set(bt_se->bt_statistics->wait_start,
			rq_clock(rq_of_bt_rq(bt_rq)));
}

/*
 * Task is being enqueued - update stats:
 */
static void
update_stats_enqueue_bt(struct bt_rq *bt_rq, struct sched_entity *se)
{
	/*
	 * Are we enqueueing a waiting task? (for current tasks
	 * a dequeue/enqueue event is a NOP)
	 */
	if (se != bt_rq->curr)
		update_stats_wait_start_bt(bt_rq, se);
}

static void
update_stats_wait_end_bt(struct bt_rq *bt_rq, struct sched_entity *se)
{
	u64 delta = rq_clock(rq_of_bt_rq(bt_rq)) - schedstat_val(se->bt_statistics->wait_start);

	schedstat_set(se->bt_statistics->wait_max,
			max(se->bt_statistics->wait_max, delta));
	schedstat_inc(se->bt_statistics->wait_count);
	schedstat_add(se->bt_statistics->wait_sum, delta);
#ifdef CONFIG_SCHEDSTATS
	if (bt_entity_is_task(se)) {
		trace_sched_stat_wait(bt_task_of(se), delta);
	}
#endif
	schedstat_set(se->bt_statistics->wait_start, 0);
}

static inline void
update_stats_dequeue_bt(struct bt_rq *bt_rq, struct sched_entity *se)
{
	/*
	 * Mark the end of the wait period if dequeueing a
	 * waiting task:
	 */
	if (se != bt_rq->curr)
		update_stats_wait_end_bt(bt_rq, se);
}

/*
 * We are picking a new current task - update its stats:
 */
static inline void
update_stats_curr_start_bt(struct bt_rq *bt_rq, struct sched_entity *se)
{
	/*
	 * We are starting a new run period:
	 */
	se->exec_start = rq_clock_task(rq_of_bt_rq(bt_rq));
}

static void
account_bt_entity_enqueue(struct bt_rq *bt_rq, struct sched_entity *se)
{
	update_load_add(&bt_rq->load, se->load.weight);

	if (!parent_bt_entity(se))
		update_load_add(&rq_of_bt_rq(bt_rq)->bt_load, se->load.weight);
#ifdef CONFIG_SMP
	if (bt_entity_is_task(se))
		list_add_tail(&se->group_node, &rq_of_bt_rq(bt_rq)->bt_tasks);
#endif

	bt_rq->nr_running++;
}

static void
account_bt_entity_dequeue(struct bt_rq *bt_rq, struct sched_entity *se)
{
	update_load_sub(&bt_rq->load, se->load.weight);

	if (!parent_bt_entity(se))
		update_load_sub(&rq_of_bt_rq(bt_rq)->bt_load, se->load.weight);
#ifdef CONFIG_SMP
	if (bt_entity_is_task(se))
		list_del_init(&se->group_node);
#endif

	bt_rq->nr_running--;
}

#ifdef CONFIG_BT_GROUP_SCHED
#ifdef CONFIG_SMP
static inline long calc_tg_weight_bt(struct task_group *tg, struct bt_rq *bt_rq)
{
	long tg_weight;

	/*
	 * Use this CPU's real-time load instead of the last load contribution
	 * as the updating of the contribution is delayed, and we will use the
	 * the real-time load to calc the share. See update_tg_load_avg().
	 */
	tg_weight = atomic64_read(&tg->bt_load_avg);
	tg_weight -= bt_rq->tg_load_avg_contrib;
	tg_weight += bt_rq->load.weight;

	return tg_weight;
}

static long calc_bt_shares(struct bt_rq *bt_rq, struct task_group *tg)
{
	long tg_weight, load, shares;

	tg_weight = calc_tg_weight_bt(tg, bt_rq);
	load = bt_rq->load.weight;

	shares = (tg->bt_shares * load);
	if (tg_weight)
		shares /= tg_weight;

	if (shares < MIN_BT_SHARES)
		shares = MIN_BT_SHARES;
	if (shares > tg->bt_shares)
		shares = tg->bt_shares;

	return shares;
}
#else /* CONFIG_SMP */
static inline long calc_bt_shares(struct bt_rq *bt_rq, struct task_group *tg)
{
	return tg->bt_shares;
}
#endif /* CONFIG_SMP */

static void reweight_bt_entity(struct bt_rq *bt_rq, struct sched_entity *se,
			    unsigned long weight)
{
	if (se->on_rq) {
		/* commit outstanding execution time */
		if (bt_rq->curr == se)
			update_curr_bt(bt_rq);
		account_bt_entity_dequeue(bt_rq, se);
	}

	update_load_set(&se->load, weight);

	if (se->on_rq)
		account_bt_entity_enqueue(bt_rq, se);
}

static void update_bt_shares(struct bt_rq *bt_rq)
{
	struct task_group *tg;
	struct sched_entity *se;
	long shares;

	tg = bt_rq->tg;
	se = tg->bt[cpu_of(rq_of_bt_rq(bt_rq))];
	if (!se || bt_rq_throttled(bt_rq))
		return;
#ifndef CONFIG_SMP
	if (likely(se->load.weight == tg->bt_shares))
		return;
#endif
	shares = calc_bt_shares(bt_rq, tg);

	reweight_bt_entity(bt_rq_of(se), se, shares);
}
#else /* CONFIG_BT_GROUP_SCHED */
static inline void update_bt_shares(struct bt_rq *bt_rq)
{
}
#endif /* CONFIG_BT_GROUP_SCHED */

#if defined(CONFIG_SMP) && defined(CONFIG_BT_GROUP_SCHED)
/* Precomputed fixed inverse multiplies for multiplication by y^n */
static const u32 bt_runnable_avg_yN_inv[] = {
	0xffffffff, 0xfa83b2da, 0xf5257d14, 0xefe4b99a, 0xeac0c6e6, 0xe5b906e6,
	0xe0ccdeeb, 0xdbfbb796, 0xd744fcc9, 0xd2a81d91, 0xce248c14, 0xc9b9bd85,
	0xc5672a10, 0xc12c4cc9, 0xbd08a39e, 0xb8fbaf46, 0xb504f333, 0xb123f581,
	0xad583ee9, 0xa9a15ab4, 0xa5fed6a9, 0xa2704302, 0x9ef5325f, 0x9b8d39b9,
	0x9837f050, 0x94f4efa8, 0x91c3d373, 0x8ea4398a, 0x8b95c1e3, 0x88980e80,
	0x85aac367, 0x82cd8698,
};

/*
 * Precomputed \Sum y^k { 1<=k<=n }.  These are floor(true_value) to prevent
 * over-estimates when re-combining.
 */
static const u32 bt_runnable_avg_yN_sum[] = {
	    0, 1002, 1982, 2941, 3880, 4798, 5697, 6576, 7437, 8279, 9103,
	 9909,10698,11470,12226,12966,13690,14398,15091,15769,16433,17082,
	17718,18340,18949,19545,20128,20698,21256,21802,22336,22859,23371,
};

/*
 * Approximate:
 *   val * y^n,    where y^32 ~= 0.5 (~1 scheduling period)
 */
static __always_inline u64 decay_bt_load(u64 val, u64 n)
{
	unsigned int local_n;

	if (!n)
		return val;
	else if (unlikely(n > BT_LOAD_AVG_PERIOD * 63))
		return 0;

	/* after bounds checking we can collapse to 32-bit */
	local_n = n;

	/*
	 * As y^PERIOD = 1/2, we can combine
	 *    y^n = 1/2^(n/PERIOD) * k^(n%PERIOD)
	 * With a look-up table which covers k^n (n<PERIOD)
	 *
	 * To achieve constant time decay_load.
	 */
	if (unlikely(local_n >= BT_LOAD_AVG_PERIOD)) {
		val >>= local_n / BT_LOAD_AVG_PERIOD;
		local_n %= BT_LOAD_AVG_PERIOD;
	}

	val *= bt_runnable_avg_yN_inv[local_n];
	/* We don't use SRR here since we always want to round down. */
	return val >> 32;
}

/*
 * For updates fully spanning n periods, the contribution to runnable
 * average will be: \Sum 1024*y^n
 *
 * We can compute this reasonably efficiently by combining:
 *   y^PERIOD = 1/2 with precomputed \Sum 1024*y^n {for  n <PERIOD}
 */
static u32 __compute_runnable_contrib_bt(u64 n)
{
	u32 contrib = 0;

	if (likely(n <= BT_LOAD_AVG_PERIOD))
		return bt_runnable_avg_yN_sum[n];
	else if (unlikely(n >= BT_LOAD_AVG_MAX_N))
		return BT_LOAD_AVG_MAX;

	/* Compute \Sum k^n combining precomputed values for k^i, \Sum k^j */
	do {
		contrib /= 2; /* y^LOAD_AVG_PERIOD = 1/2 */
		contrib += bt_runnable_avg_yN_sum[BT_LOAD_AVG_PERIOD];

		n -= BT_LOAD_AVG_PERIOD;
	} while (n > BT_LOAD_AVG_PERIOD);

	contrib = decay_bt_load(contrib, n);
	return contrib + bt_runnable_avg_yN_sum[n];
}

/*
 * We can represent the historical contribution to runnable average as the
 * coefficients of a geometric series.  To do this we sub-divide our runnable
 * history into segments of approximately 1ms (1024us); label the segment that
 * occurred N-ms ago p_N, with p_0 corresponding to the current period, e.g.
 *
 * [<- 1024us ->|<- 1024us ->|<- 1024us ->| ...
 *      p0            p1           p2
 *     (now)       (~1ms ago)  (~2ms ago)
 *
 * Let u_i denote the fraction of p_i that the entity was runnable.
 *
 * We then designate the fractions u_i as our co-efficients, yielding the
 * following representation of historical load:
 *   u_0 + u_1*y + u_2*y^2 + u_3*y^3 + ...
 *
 * We choose y based on the with of a reasonably scheduling period, fixing:
 *   y^32 = 0.5
 *
 * This means that the contribution to load ~32ms ago (u_32) will be weighted
 * approximately half as much as the contribution to load within the last ms
 * (u_0).
 *
 * When a period "rolls over" and we have new u_0`, multiplying the previous
 * sum again by y is sufficient to update:
 *   load_avg = u_0` + y*(u_0 + u_1*y + u_2*y^2 + ... )
 *            = u_0 + u_1*y + u_2*y^2 + ... [re-labeling u_i --> u_{i+1}]
 */
static __always_inline int
__update_bt_load_avg(u64 now, struct sched_avg_bt *sa,
		  unsigned long weight, int running, struct bt_rq *bt_rq)
{
	u64 delta, periods;
	u32 contrib;
	int delta_w, decayed = 0;

	delta = now - sa->last_update_time;
	/*
	 * This should only happen when time goes backwards, which it
	 * unfortunately does during sched clock init when we swap over to TSC.
	 */
	if ((s64)delta < 0) {
		sa->last_update_time = now;
		return 0;
	}

	/*
	 * Use 1024ns as the unit of measurement since it's a reasonable
	 * approximation of 1us and fast to compute.
	 */
	delta >>= 10;
	if (!delta)
		return 0;
	sa->last_update_time = now;

	/* delta_w is the amount already accumulated against our next period */
	delta_w = sa->period_contrib;
	if (delta + delta_w >= 1024) {
		decayed = 1;

		/* how much left for next period will start over, we don't know yet */
		sa->period_contrib = 0;

		/*
		 * Now that we know we're crossing a period boundary, figure
		 * out how much from delta we need to complete the current
		 * period and accrue it.
		 */
		delta_w = 1024 - delta_w;
		if (weight) {
			sa->load_sum += weight * delta_w;
			if (bt_rq)
				bt_rq->runnable_load_sum += weight * delta_w;
		}
		if (running)
			sa->util_sum += delta_w;

		delta -= delta_w;

		/* Figure out how many additional periods this update spans */
		periods = delta / 1024;
		delta %= 1024;

		sa->load_sum = decay_bt_load(sa->load_sum, periods + 1);
		if (bt_rq) {
			bt_rq->runnable_load_sum =
				decay_bt_load(bt_rq->runnable_load_sum, periods + 1);
		}
		sa->util_sum = decay_bt_load((u64)(sa->util_sum), periods + 1);

		/* Efficiently calculate \sum (1..n_period) 1024*y^i */
		contrib = __compute_runnable_contrib_bt(periods);
		if (weight) {
			sa->load_sum += weight * contrib;
			if (bt_rq)
				bt_rq->runnable_load_sum += weight * contrib;
		}
		if (running)
			sa->util_sum += contrib;
	}

	/* Remainder of delta accrued against u_0` */
	if (weight) {
		sa->load_sum += weight * delta;
		if (bt_rq)
			bt_rq->runnable_load_sum += weight * delta;
	}
	if (running)
		sa->util_sum += delta;

	sa->period_contrib += delta;
	if (decayed) {
		sa->load_avg = div_u64(sa->load_sum, BT_LOAD_AVG_MAX);
		if (bt_rq) {
			bt_rq->runnable_load_avg =
				div_u64(bt_rq->runnable_load_sum, BT_LOAD_AVG_MAX);
		}
		sa->util_avg = (sa->util_sum << SCHED_LOAD_SHIFT) / BT_LOAD_AVG_MAX;
	}

	return decayed;
}

#ifdef CONFIG_BT_GROUP_SCHED
/*
 * Updating tg's load_avg is necessary before update_cfs_share (which is done)
 * and effective_load (which is not done because it is too costly).
 */
static inline void update_tg_bt_load_avg(struct bt_rq *bt_rq, int force)
{
	long delta = bt_rq->avg.load_avg - bt_rq->tg_load_avg_contrib;

	if (force || abs(delta) > bt_rq->tg_load_avg_contrib / 64) {
		atomic_long_add(delta, &bt_rq->tg->bt_load_avg);
		bt_rq->tg_load_avg_contrib = bt_rq->avg.load_avg;
	}
}
#else
static inline void update_tg_bt_load_avg(struct bt_rq *bt_rq, int force) {}
#endif

/*
 * Unsigned subtract and clamp on underflow.
 *
 * Explicitly do a load-store to ensure the intermediate value never hits
 * memory. This allows lockless observations without ever seeing the negative
 * values.
 */
#define sub_positive(_ptr, _val) do {				\
	typeof(_ptr) ptr = (_ptr);				\
	typeof(*ptr) val = (_val);				\
	typeof(*ptr) res, var = READ_ONCE(*ptr);		\
	res = var - val;					\
	if (res > var)						\
		res = 0;					\
	WRITE_ONCE(*ptr, res);					\
} while (0)

/* Group cfs_rq's load_avg is used for task_h_load and update_bt_share */
static inline int update_bt_rq_load_avg(u64 now, struct bt_rq *bt_rq)
{
	struct sched_avg_bt *sa = &bt_rq->avg;
	int decayed, removed = 0;

	if (atomic_long_read(&bt_rq->removed_load_avg)) {
		long r = atomic_long_xchg(&bt_rq->removed_load_avg, 0);
		sub_positive(&sa->load_avg, r);
		sub_positive(&sa->load_sum, r * BT_LOAD_AVG_MAX);
		removed = 1;
	}
	if (atomic_long_read(&bt_rq->removed_util_avg)) {
		long r = atomic_long_xchg(&bt_rq->removed_util_avg, 0);
		sub_positive(&sa->util_avg, r);
		sub_positive(&sa->util_sum,
			((r *BT_LOAD_AVG_MAX) >> SCHED_LOAD_SHIFT));
	}

	decayed = __update_bt_load_avg(now, sa,
		scale_load_down(bt_rq->load.weight), bt_rq->curr != NULL, bt_rq);

#ifndef CONFIG_64BIT
	smp_wmb();
	bt_rq->load_last_update_time_copy = sa->last_update_time;
#endif
	return decayed || removed;
}

/* Update task and its cfs_rq load average */
static inline void update_bt_load_avg(struct sched_entity *se, int update_tg)
{
	struct bt_rq *bt_rq = bt_rq_of(se);
	u64 now = bt_rq_clock_task(bt_rq);;

	/*
	 * Track task load average for carrying it to new CPU after migrated, and
	 * track group sched_entity load average for task_h_load calc in migration
	 */
	__update_bt_load_avg(now, &se->bt_avg,
		se->on_rq * scale_load_down(se->load.weight), bt_rq->curr == se, NULL);
	if (update_bt_rq_load_avg(now, bt_rq) && update_tg)
		update_tg_bt_load_avg(bt_rq, 0);
}

/* Add the load generated by se into cfs_rq's load average */
static inline void
enqueue_bt_entity_load_avg(struct bt_rq *bt_rq, struct sched_entity *se)
{
	struct sched_avg_bt *sa = &se->bt_avg;
	u64 now = bt_rq_clock_task(bt_rq);
	int migrated = 0, decayed;

	if (sa->last_update_time == 0) {
		sa->last_update_time = now;
		migrated = 1;
	} else {
		__update_bt_load_avg(now, sa,
			se->on_rq * scale_load_down(se->load.weight),
			bt_rq->curr == se, NULL);
	}

	decayed = update_bt_rq_load_avg(now, bt_rq);
	bt_rq->runnable_load_avg += sa->load_avg;
	bt_rq->runnable_load_sum += sa->load_sum;

	if (migrated) {
		bt_rq->avg.load_avg += sa->load_avg;
		bt_rq->avg.load_sum += sa->load_sum;
		bt_rq->avg.util_avg += sa->util_avg;
		bt_rq->avg.util_sum += sa->util_sum;
	}

	if (decayed || migrated)
		update_tg_bt_load_avg(bt_rq, 0);
}

/* Remove the runnable load generated by se from cfs_rq's runnable load average */
static inline void
dequeue_bt_entity_load_avg(struct bt_rq *bt_rq, struct sched_entity *se)
{
	update_bt_load_avg(se, 1);

	bt_rq->runnable_load_avg =
		max_t(long, bt_rq->runnable_load_avg - se->bt_avg.load_avg, 0);
	bt_rq->runnable_load_sum =
		max_t(s64, bt_rq->runnable_load_sum - se->bt_avg.load_sum, 0);
}

#ifndef CONFIG_64BIT
static inline u64 bt_rq_last_update_time(struct bt_rq *bt_rq)
{
	u64 last_update_time_copy;
	u64 last_update_time;

	do {
		last_update_time_copy = bt_rq->load_last_update_time_copy;
		smp_rmb();
		last_update_time = bt_rq->avg.last_update_time;
	} while (last_update_time != last_update_time_copy);

	return last_update_time;
}
#else
static inline u64 bt_rq_last_update_time(struct bt_rq *bt_rq)
{
	return bt_rq->avg.last_update_time;
}
#endif

/*
 * Task first catches up with cfs_rq, and then subtract
 * itself from the cfs_rq (task must be off the queue now).
 */
void remove_bt_entity_load_avg(struct sched_entity *se)
{
	struct bt_rq *bt_rq = bt_rq_of(se);
	u64 last_update_time;

	/*
	 * Newly created task or never used group entity should not be removed
	 * from its (source) cfs_rq
	 */
	if (se->bt_avg.last_update_time == 0)
		return;

	last_update_time = bt_rq_last_update_time(bt_rq);

	__update_bt_load_avg(last_update_time, &se->bt_avg, 0, 0, NULL);
	atomic_long_add(se->bt_avg.load_avg, &bt_rq->removed_load_avg);
	atomic_long_add(se->bt_avg.util_avg, &bt_rq->removed_util_avg);
}

/*
 * Update the rq's load with the elapsed running time before entering
 * idle. if the last scheduled task is not a CFS task, idle_enter will
 * be the only way to update the runnable statistic.
 */
void idle_enter_bt(struct rq *this_rq)
{
}

/*
 * Update the rq's load with the elapsed idle time before a task is
 * scheduled. if the newly scheduled task is not a CFS task, idle_exit will
 * be the only way to update the runnable statistic.
 */
void idle_exit_bt(struct rq *this_rq)
{
}

#else
static inline void update_bt_load_avg(struct sched_entity *se, int update_tg) {}
static inline void
enqueue_bt_entity_load_avg(struct bt_rq *bt_rq, struct sched_entity *se) {}
static inline void remove_bt_entity_load_avg(struct sched_entity *se) {}
static inline void
dequeue_bt_entity_load_avg(struct bt_rq *bt_rq, struct sched_entity *se) {}
#endif

static void enqueue_bt_sleeper(struct bt_rq *bt_rq, struct sched_entity *se)
{
#if defined(CONFIG_SCHEDSTATS) || defined(CONFIG_LATENCYTOP)
	struct task_struct *tsk = NULL;

	if (bt_entity_is_task(se))
		tsk = bt_task_of(se);

	if (se->bt_statistics->sleep_start) {
		u64 delta = rq_clock(rq_of_bt_rq(bt_rq)) - schedstat_val(se->bt_statistics->sleep_start);

		if ((s64)delta < 0)
			delta = 0;

#ifdef CONFIG_SCHEDSTATS
		if (unlikely(delta > se->bt_statistics->sleep_max))
			se->bt_statistics->sleep_max = delta;
#endif

		se->bt_statistics->sleep_start = 0;
#ifdef CONFIG_SCHEDSTATS
		se->bt_statistics->sum_sleep_runtime += delta;
#endif

		if (tsk) {
			account_scheduler_latency(tsk, delta >> 10, 1);
#ifdef CONFIG_SCHEDSTATS
			trace_sched_stat_sleep(tsk, delta);
#endif
		}
	}
	if (se->bt_statistics->block_start) {
		u64 delta = rq_clock(rq_of_bt_rq(bt_rq)) - schedstat_val(se->bt_statistics->block_start);

		if ((s64)delta < 0)
			delta = 0;

#ifdef CONFIG_SCHEDSTATS
		if (unlikely(delta > se->bt_statistics->block_max))
			se->bt_statistics->block_max = delta;
#endif

		se->bt_statistics->block_start = 0;
#ifdef CONFIG_SCHEDSTATS
		se->bt_statistics->sum_sleep_runtime += delta;
#endif

		if (tsk) {
#ifdef CONFIG_SCHEDSTATS
			if (tsk->in_iowait) {
				se->bt_statistics->iowait_sum += delta;
				se->bt_statistics->iowait_count++;
				trace_sched_stat_iowait(tsk, delta);
			}
#endif

			trace_sched_stat_blocked(tsk, delta);

			/*
			 * Blocking time is in units of nanosecs, so shift by
			 * 20 to get a milliseconds-range estimation of the
			 * amount of time that the task spent sleeping:
			 */
			if (unlikely(prof_on == SLEEP_PROFILING)) {
				profile_hits(SLEEP_PROFILING,
						(void *)get_wchan(tsk),
						delta >> 20);
			}
			account_scheduler_latency(tsk, delta >> 10, 0);
		}
	}
#endif
}

static void check_bt_spread(struct bt_rq *bt_rq, struct sched_entity *se)
{
#ifdef CONFIG_SCHED_DEBUG
	s64 d = se->vruntime - bt_rq->min_vruntime;

	if (d < 0)
		d = -d;

	if (d > 3*sysctl_sched_latency)
		schedstat_inc(bt_rq->nr_spread_over);
#endif
}

static void
place_bt_entity(struct bt_rq *bt_rq, struct sched_entity *se, int initial)
{
	u64 vruntime = bt_rq->min_vruntime;

	/*
	 * The 'current' period is already promised to the current tasks,
	 * however the extra weight of the new task will slow them down a
	 * little, place the new task so that it fits in the slot that
	 * stays open at the end.
	 */
	if (initial && sched_feat(START_DEBIT))
		vruntime += sched_bt_vslice(bt_rq, se);

	/* sleeps up to a single latency don't count. */
	if (!initial) {
		unsigned long thresh = sysctl_sched_latency;

		/*
		 * Halve their sleep time's effect, to allow
		 * for a gentler effect of sleepers:
		 */
		if (sched_feat(GENTLE_FAIR_SLEEPERS))
			thresh >>= 1;

		vruntime -= thresh;
	}

	/* ensure we never gain time by being placed backwards. */
	se->vruntime = max_vruntime(se->vruntime, vruntime);
}

static void
enqueue_bt_entity(struct bt_rq *bt_rq, struct sched_entity *se, int flags)
{
	bool renorm = !(flags & ENQUEUE_WAKEUP) || (flags & ENQUEUE_MIGRATED);
	bool curr = bt_rq->curr == se;

	/*
	 * Update the normalized vruntime before updating min_vruntime
	 * through callig update_curr().
	 */
	if (renorm)
		se->vruntime += bt_rq->min_vruntime;

	/*
	 * Update run-time bt_statistics of the 'current'.
	 */
	update_curr_bt(bt_rq);
	enqueue_bt_entity_load_avg(bt_rq, se);
	account_bt_entity_enqueue(bt_rq, se);
	update_bt_shares(bt_rq);

	if (flags & ENQUEUE_WAKEUP) {
		place_bt_entity(bt_rq, se, 0);
		enqueue_bt_sleeper(bt_rq, se);
	}

	update_stats_enqueue_bt(bt_rq, se);
	check_bt_spread(bt_rq, se);
	if (!curr)
		__enqueue_bt_entity(bt_rq, se);

	se->on_rq = 1;
	if (bt_rq->nr_running == 1)
		list_add_leaf_bt_rq(bt_rq);
	start_bt_bandwidth(&def_bt_bandwidth);
}

static void __clear_buddies_last_bt(struct sched_entity *se)
{
	for_each_sched_bt_entity(se) {
		struct bt_rq *bt_rq = bt_rq_of(se);

		if (bt_rq->last == se)
			bt_rq->last = NULL;
		else
			break;
	}
}

static void __clear_buddies_next_bt(struct sched_entity *se)
{
	for_each_sched_bt_entity(se) {
		struct bt_rq *bt_rq = bt_rq_of(se);

		if (bt_rq->next == se)
			bt_rq->next = NULL;
		else
			break;
	}
}

static void __clear_buddies_skip_bt(struct sched_entity *se)
{
	for_each_sched_bt_entity(se) {
		struct bt_rq *bt_rq = bt_rq_of(se);

		if (bt_rq->skip == se)
			bt_rq->skip = NULL;
		else
			break;
	}
}

static void clear_buddies_bt(struct bt_rq *bt_rq, struct sched_entity *se)
{
	if (bt_rq->last == se)
		__clear_buddies_last_bt(se);

	if (bt_rq->next == se)
		__clear_buddies_next_bt(se);

	if (bt_rq->skip == se)
		__clear_buddies_skip_bt(se);
}

static void
dequeue_bt_entity(struct bt_rq *bt_rq, struct sched_entity *se, int flags)
{
	/*
	 * Update run-time bt_statistics of the 'current'.
	 */
	update_curr_bt(bt_rq);
	update_bt_load_avg(se, 1);

	update_stats_dequeue_bt(bt_rq, se);
	if (flags & DEQUEUE_SLEEP) {
#if defined(CONFIG_SCHEDSTATS) || defined(CONFIG_LATENCYTOP)
		if (bt_entity_is_task(se)) {
			struct task_struct *tsk = bt_task_of(se);

			if (tsk->state & TASK_INTERRUPTIBLE)
				schedstat_set(se->bt_statistics->sleep_start,
						rq_clock(rq_of_bt_rq(bt_rq)));
			if (tsk->state & TASK_UNINTERRUPTIBLE)
				schedstat_set(se->bt_statistics->block_start,
						rq_clock(rq_of_bt_rq(bt_rq)));
		}
#endif
	}

	clear_buddies_bt(bt_rq, se);

	if (se != bt_rq->curr)
		__dequeue_bt_entity(bt_rq, se);
	se->on_rq = 0;
	account_bt_entity_dequeue(bt_rq, se);

	/*
	 * Normalize the entity after updating the min_vruntime because the
	 * update can refer to the ->curr item and we need to reflect this
	 * movement in our normalized position.
	 */
	if (!(flags & DEQUEUE_SLEEP))
		se->vruntime -= bt_rq->min_vruntime;

	if ((flags & (DEQUEUE_SAVE | DEQUEUE_MOVE)) != DEQUEUE_SAVE)
		update_bt_min_vruntime(bt_rq);
	update_bt_shares(bt_rq);
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void
check_preempt_tick_bt(struct bt_rq *bt_rq, struct sched_entity *curr)
{
	unsigned long ideal_runtime, delta_exec;
	struct sched_entity *se;
	s64 delta;

	ideal_runtime = sched_bt_slice(bt_rq, curr);
	delta_exec = curr->sum_exec_runtime - curr->prev_sum_exec_runtime;
	if (delta_exec > ideal_runtime) {
		resched_curr(rq_of_bt_rq(bt_rq));
		/*
		 * The current task ran long enough, ensure it doesn't get
		 * re-elected due to buddy favours.
		 */
		clear_buddies_bt(bt_rq, curr);
		return;
	}

	/*
	 * Ensure that a task that missed wakeup preemption by a
	 * narrow margin doesn't have to wait for a full slice.
	 * This also mitigates buddy induced latencies under load.
	 */
	if (delta_exec < sysctl_sched_min_granularity)
		return;

	se = __pick_first_bt_entity(bt_rq);
	delta = curr->vruntime - se->vruntime;

	if (delta < 0)
		return;

	if (delta > ideal_runtime)
		resched_curr(rq_of_bt_rq(bt_rq));
}

static void
set_next_bt_entity(struct bt_rq *bt_rq, struct sched_entity *se)
{
	/* 'current' is not kept within the tree. */
	if (se->on_rq) {
		/*
		 * Any task has to be enqueued before it get to execute on
		 * a CPU. So account for the time it spent waiting on the
		 * runqueue.
		 */
		update_stats_wait_end_bt(bt_rq, se);
		__dequeue_bt_entity(bt_rq, se);
		update_bt_load_avg(se, 1);
	}

	update_stats_curr_start_bt(bt_rq, se);
	bt_rq->curr = se;
#ifdef CONFIG_SCHEDSTATS
	/*
	 * Track our maximum slice length, if the CPU's load is at
	 * least twice that of our own weight (i.e. dont track it
	 * when there are only lesser-weight tasks around):
	 */
	if (rq_of_bt_rq(bt_rq)->bt_load.weight >= 2*se->load.weight) {
		se->bt_statistics->slice_max = max(se->bt_statistics->slice_max,
			se->sum_exec_runtime - se->prev_sum_exec_runtime);
	}
#endif
	se->prev_sum_exec_runtime = se->sum_exec_runtime;
}

static int
wakeup_preempt_bt_entity(struct sched_entity *curr, struct sched_entity *se);

/*
 * Pick the next process, keeping these things in mind, in this order:
 * 1) keep things fair between processes/task groups
 * 2) pick the "next" process, since someone really wants that to run
 * 3) pick the "last" process, for cache locality
 * 4) do not run the "skip" process, if something else is available
 */
static struct sched_entity *pick_next_bt_entity(struct bt_rq *bt_rq)
{
	struct sched_entity *se = __pick_first_bt_entity(bt_rq);
	struct sched_entity *left = se;

	/*
	 * Avoid running the skip buddy, if running something else can
	 * be done without getting too unfair.
	 */
	if (bt_rq->skip == se) {
		struct sched_entity *second = __pick_next_bt_entity(se);

		if (second && wakeup_preempt_bt_entity(second, left) < 1)
			se = second;
	}

	/*
	 * Prefer last buddy, try to return the CPU to a preempted task.
	 */
	if (bt_rq->last && wakeup_preempt_bt_entity(bt_rq->last, left) < 1)
		se = bt_rq->last;

	/*
	 * Someone really wants this to run. If it's not unfair, run it.
	 */
	if (bt_rq->next && wakeup_preempt_bt_entity(bt_rq->next, left) < 1)
		se = bt_rq->next;

	clear_buddies_bt(bt_rq, se);

	return se;
}

static void put_prev_bt_entity(struct bt_rq *bt_rq, struct sched_entity *prev)
{
	/*
	 * If still on the runqueue then deactivate_task()
	 * was not called and update_curr() has to be done:
	 */
	if (prev->on_rq)
		update_curr_bt(bt_rq);

	check_bt_spread(bt_rq, prev);
	if (prev->on_rq) {
		update_stats_wait_start_bt(bt_rq, prev);
		/* Put 'current' back into the tree. */
		__enqueue_bt_entity(bt_rq, prev);
		/* in !on_rq case, update occurred at dequeue */
		update_bt_load_avg(prev, 0);
	}
	bt_rq->curr = NULL;
}

static void
bt_entity_tick(struct bt_rq *bt_rq, struct sched_entity *curr, int queued)
{
	/*
	 * Update run-time bt_statistics of the 'current'.
	 */
	update_curr_bt(bt_rq);
	/* Ensure that runnable average is periodically updated */
	update_bt_load_avg(curr, 1);
	update_bt_shares(bt_rq);

#ifdef CONFIG_SCHED_HRTICK
	/*
	 * queued ticks are scheduled to match the slice, so don't bother
	 * validating it and just reschedule.
	 */
	if (queued) {
		resched_curr(rq_of_bt_rq(bt_rq));
		return;
	}
#endif

	if (bt_rq->nr_running > 1)
		check_preempt_tick_bt(bt_rq, curr);
}

/*
 * The enqueue_task method is called before nr_running is
 * increased. Here we update the fair scheduling stats and
 * then put the task into the rbtree:
 */
static void
enqueue_task_bt(struct rq *rq, struct task_struct *p, int flags)
{
	struct bt_rq *bt_rq;
	struct sched_entity *se = &p->bt;

	for_each_sched_bt_entity(se) {
		if (se->on_rq)
			break;
		bt_rq = bt_rq_of(se);
		enqueue_bt_entity(bt_rq, se, flags);

		if (bt_rq_throttled(bt_rq))
			break;

		bt_rq->h_nr_running++;

		flags = ENQUEUE_WAKEUP;
	}

	for_each_sched_bt_entity(se) {
		bt_rq =bt_rq_of(se);
		bt_rq->h_nr_running++;

		if (bt_rq_throttled(bt_rq))
			break;

		update_bt_load_avg(se, 1);
		update_bt_shares(bt_rq);
	}

	if (!se) {
		if (!rq->bt_nr_running){
			rq->bt_blocked_clock = rq_clock(rq);
		}

		rq->bt_nr_running++;
		add_nr_running(rq, 1);
	}
}

static void set_next_buddy_bt(struct sched_entity *se);

/*
 * The dequeue_task method is called before nr_running is
 * decreased. We remove the task from the rbtree and
 * update the fair scheduling stats:
 */
static void dequeue_task_bt(struct rq *rq, struct task_struct *p, int flags)
{
	struct bt_rq *bt_rq;
	struct sched_entity *se = &p->bt;
	int task_sleep = flags & DEQUEUE_SLEEP;

	for_each_sched_bt_entity(se) {
		bt_rq = bt_rq_of(se);
		dequeue_bt_entity(bt_rq, se, flags);

		if (bt_rq_throttled(bt_rq))
			break;

		bt_rq->h_nr_running--;

		/* Don't dequeue parent if it has other entities besides us */
		if (bt_rq->load.weight) {
			/*
			 * Bias pick_next to pick a task from this cfs_rq, as
			 * p is sleeping when it is within its sched_slice.
			 */
			if (task_sleep && parent_bt_entity(se))
				set_next_buddy_bt(parent_bt_entity(se));

			/* avoid re-evaluating load for this entity */
			se = parent_bt_entity(se);
			break;
		}
		flags |= DEQUEUE_SLEEP;
	}

	for_each_sched_bt_entity(se) {
		bt_rq =bt_rq_of(se);
		bt_rq->h_nr_running--;

		if (bt_rq_throttled(bt_rq))
			break;

		update_bt_load_avg(se, 1);
		update_bt_shares(bt_rq);
	}

	if (!se) {
		sub_nr_running(rq, 1);
		rq->bt_nr_running--;

		if (!rq->bt_nr_running){
			rq->bt_blocked_clock = 0;
		}
	}
}

#ifdef CONFIG_SMP

/* Used instead of source_bt_load when we know the type == 0 */
static unsigned long bt_weighted_cpuload(const int cpu)
{
	return cpu_rq(cpu)->bt_load.weight;
}

/*
 * Return a low guess at the load of a migration-source cpu weighted
 * according to the scheduling class and "nice" value.
 *
 * We want to under-estimate the load of migration sources, to
 * balance conservatively.
 */
static unsigned long source_bt_load(int cpu, int type)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long total = bt_weighted_cpuload(cpu);

	if (type == 0 || !sched_feat(LB_BIAS))
		return total;

	return min(rq->cpu_bt_load[type-1], total);
}

/*
 * Return a high guess at the load of a migration-target cpu weighted
 * according to the scheduling class and "nice" value.
 */
static unsigned long target_bt_load(int cpu, int type)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long total = bt_weighted_cpuload(cpu);

	if (type == 0 || !sched_feat(LB_BIAS))
		return total;

	return max(rq->cpu_bt_load[type-1], total);
}

static unsigned long cpu_avg_bt_load_per_task(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long nr_running = ACCESS_ONCE(rq->bt_nr_running);

	if (nr_running)
		return rq->bt_load.weight / nr_running;

	return 0;
}

static unsigned int bt_load_factor(struct rq *rq)
{
	u64 diff = 0;

	if (rq->bt_blocked_clock) {
		u32 gran_ns = sysctl_sched_bt_granularity_ns;

		// should be using rq_clock
		diff = rq->clock - rq->bt_blocked_clock;
		diff = (diff + (gran_ns >> 1)) / gran_ns;
	}

	return min(diff >> 1, (u64)12);
}

/*
 * find_idlest_group finds and returns the least busy CPU group within the
 * domain.
 */
static struct sched_group *
find_idlest_group_bt(struct sched_domain *sd, struct task_struct *p,
		  int this_cpu, int load_idx)
{
	struct sched_group *idlest = NULL, *group = sd->groups;
	unsigned long min_load = ULONG_MAX, this_load = 0;
	int imbalance = 100 + (sd->imbalance_pct-100)/2;
	unsigned int sg_vain_power, fair_load;

	fair_load = sysctl_sched_bt_load_fair ? 1 : 0;

	do {
		unsigned long load, avg_load;
		int local_group;
		int i;
		struct rq *rq = NULL;

		/* Skip over this group if it has no CPUs allowed */
		if (!cpumask_intersects(sched_group_cpus(group),
					tsk_cpus_allowed(p)))
			continue;

		local_group = cpumask_test_cpu(this_cpu,
					       sched_group_cpus(group));

		/* Tally up the load of all CPUs in the group */
		avg_load = 0;
		sg_vain_power = 0;

		for_each_cpu(i, sched_group_cpus(group)) {
			rq = cpu_rq(i);

			/* Bias balancing toward cpus of our domain */
			if (local_group){
				load = source_bt_load(i, load_idx) << bt_load_factor(rq);
				if(fair_load){
					load += source_load(i, load_idx);
				}
			}else{
				load = target_bt_load(i, load_idx) << bt_load_factor(rq);
				if(fair_load){
					load += target_load(i, load_idx);
				}
			}

			avg_load += load;
			if (rq->nr_running > rq->bt_nr_running || rq->bt.bt_throttled){
				sg_vain_power += rq->cpu_capacity;
			}
		}

		/* Adjust by relative CPU power of the group */
		avg_load = (avg_load * SCHED_CAPACITY_SCALE) /
				max((s64)(group->sgc->capacity - sg_vain_power), (s64)1);

		if (local_group) {
			this_load = avg_load;
		} else if (avg_load < min_load) {
			min_load = avg_load;
			idlest = group;
		}
	} while (group = group->next, group != sd->groups);

	if (!idlest || 100*this_load < imbalance*min_load)
		return NULL;
	return idlest;
}

/*
 * find_idlest_cpu - find the idlest cpu among the cpus in group.
 */
static int
find_idlest_cpu_bt(struct sched_group *group, struct task_struct *p, int this_cpu)
{
	unsigned long load, min_load = ULONG_MAX;
	int idlest = -1;
	int i, fair_load;
	struct rq *rq;

	fair_load = sysctl_sched_bt_load_fair ? 1 : 0;

	/* Traverse only the allowed CPUs */
	for_each_cpu_and(i, sched_group_cpus(group), tsk_cpus_allowed(p)) {
		rq = cpu_rq(i);

		load = bt_weighted_cpuload(i) << bt_load_factor(rq);
		if(fair_load){
			load += weighted_cpuload(rq);
		}

		if ((load < min_load || (load == min_load && i == this_cpu))&&
		    !cpu_rq(i)->bt.bt_throttled && rq->nr_running == rq->bt_nr_running) {
			min_load = load;
			idlest = i;
		}
	}

	return idlest;
}

static int select_idle_sibling_bt(struct task_struct *p, int target)
{
	struct sched_domain *sd;
	struct sched_group *sg;
	int i = task_cpu(p);
	int dst_cpu = target;
	int new_cpu = -1;
	int loop;

	if (idle_cpu(dst_cpu) && !cpu_rq(dst_cpu)->bt.bt_throttled)
		return dst_cpu;

	/*
	 * If the prevous cpu is cache affine and idle, don't be stupid.
	 */
	if (i != dst_cpu && cpus_share_cache(i, dst_cpu) && idle_cpu(i) &&
	    !cpu_rq(i)->bt.bt_throttled)
		return i;

	/*
	 * Otherwise, iterate the domains and find an elegible idle cpu.
	 */
	sd = rcu_dereference(per_cpu(sd_llc, dst_cpu));
	for_each_lower_domain(sd) {
		sg = sd->groups;
		do {
			if (!cpumask_intersects(sched_group_cpus(sg),
						tsk_cpus_allowed(p)))
				goto next;

			loop = 0;
			for_each_cpu(i, sched_group_cpus(sg)) {
				if (i == dst_cpu || !idle_cpu(i) || cpu_rq(i)->bt.bt_throttled) {
					loop = 1;
					continue;
				}
				if (new_cpu == -1)
					new_cpu = i;
			}

			if (loop)
				goto next;

			dst_cpu = cpumask_first_and(sched_group_cpus(sg),
					tsk_cpus_allowed(p));
			goto done;
next:
			sg = sg->next;
		} while (sg != sd->groups);
	}
done:
	if (dst_cpu == target && new_cpu != -1)
		dst_cpu = new_cpu;

	return dst_cpu;
}

static int
select_task_rq_bt(struct task_struct *p, int prev_cpu, int sd_flag, int wake_flags)
{
	struct sched_domain *tmp, *affine_sd = NULL, *sd = NULL;
	int cpu = smp_processor_id();
	int new_cpu = prev_cpu;
	int want_affine = 0;
	bool check_cpumask;

	if (offlinegroup_enabled && sysctl_sched_bt_ignore_cpubind)
		check_cpumask = false;
	else
		check_cpumask = true;

	if (check_cpumask && p->nr_cpus_allowed == 1)
		return prev_cpu;

	if (sd_flag & SD_BALANCE_WAKE) {
		if (!check_cpumask ||
		    (check_cpumask && cpumask_test_cpu(cpu, tsk_cpus_allowed(p))))
			want_affine = 1;
		new_cpu = prev_cpu;
	}

	rcu_read_lock();
	for_each_domain(cpu, tmp) {
		if (!(tmp->flags & SD_LOAD_BALANCE))
			continue;

		/*
		 * If both cpu and prev_cpu are part of this domain,
		 * cpu is a valid SD_WAKE_AFFINE target.
		 */
		if (want_affine && (tmp->flags & SD_WAKE_AFFINE) &&
		    cpumask_test_cpu(prev_cpu, sched_domain_span(tmp))) {
			affine_sd = tmp;
			break;
		}

		if (tmp->flags & sd_flag)
			sd = tmp;
	}

	if (affine_sd) {
		new_cpu = select_idle_sibling_bt(p, prev_cpu);
		if (new_cpu == -1 && cpu != prev_cpu){
			new_cpu = select_idle_sibling_bt(p, cpu);
		}

		goto unlock;
	}

	while (sd) {
		int load_idx = sd->forkexec_idx;
		struct sched_group *group;
		int weight;

		if (!(sd->flags & sd_flag)) {
			sd = sd->child;
			continue;
		}

		if (sd_flag & SD_BALANCE_WAKE)
			load_idx = sd->wake_idx;

		group = find_idlest_group_bt(sd, p, cpu, load_idx);
		if (!group) {
			sd = sd->child;
			continue;
		}

		new_cpu = find_idlest_cpu_bt(group, p, cpu);
		if (new_cpu == -1 || new_cpu == cpu) {
			/* Now try balancing at a lower domain level of cpu */
			sd = sd->child;
			continue;
		}

		/* Now try balancing at a lower domain level of new_cpu */
		cpu = new_cpu;
		weight = sd->span_weight;
		sd = NULL;
		for_each_domain(cpu, tmp) {
			if (weight <= tmp->span_weight)
				break;
			if (tmp->flags & sd_flag)
				sd = tmp;
		}
		/* while loop will break here if sd == NULL */
	}

unlock:
	rcu_read_unlock();

	if (new_cpu == -1 || !cpu_rq(new_cpu)->bt.bt_runtime)
		new_cpu = task_cpu(p);

	return new_cpu;
}

#ifdef CONFIG_BT_GROUP_SCHED
/*
 * Called immediately before a task is migrated to a new cpu; task_cpu(p) and
 * cfs_rq_of(p) references at time of call are still valid and identify the
 * previous cpu.  However, the caller only guarantees p->pi_lock is held; no
 * other assumptions, including the state of rq->lock, should be made.
 */
static void
migrate_task_rq_bt(struct task_struct *p)
{
	/*
	 * We are supposed to update the task to "current" time, then its up to date
	 * and ready to go to new CPU/cfs_rq. But we have difficulty in getting
	 * what current time is, so simply throw away the out-of-date time. This
	 * will result in the wakee task is less decayed, but giving the wakee more
	 * load sounds not bad.
	 */
	remove_bt_entity_load_avg(&p->bt);

	/* Tell new CPU we are migrated */
	p->bt.bt_avg.last_update_time = 0;

	/* We have migrated, no longer consider this task hot */
	p->bt.exec_start = 0;
}
#endif

static void task_dead_bt(struct task_struct *p)
{
	remove_bt_entity_load_avg(&p->bt);
}
#endif

static unsigned long
wakeup_gran_bt(struct sched_entity *curr, struct sched_entity *se)
{
	unsigned long gran = sysctl_sched_wakeup_granularity;

	/*
	 * Since its curr running now, convert the gran from real-time
	 * to virtual-time in his units.
	 *
	 * By using 'se' instead of 'curr' we penalize light tasks, so
	 * they get preempted easier. That is, if 'se' < 'curr' then
	 * the resulting gran will be larger, therefore penalizing the
	 * lighter, if otoh 'se' > 'curr' then the resulting gran will
	 * be smaller, again penalizing the lighter task.
	 *
	 * This is especially important for buddies when the leftmost
	 * task is higher priority than the buddy.
	 */
	return calc_delta_bt(gran, se);
}

/*
 * Should 'se' preempt 'curr'.
 *
 *             |s1
 *        |s2
 *   |s3
 *         g
 *      |<--->|c
 *
 *  w(c, s1) = -1
 *  w(c, s2) =  0
 *  w(c, s3) =  1
 *
 */
static int
wakeup_preempt_bt_entity(struct sched_entity *curr, struct sched_entity *se)
{
	s64 gran, vdiff = curr->vruntime - se->vruntime;

	if (vdiff <= 0)
		return -1;

	gran = wakeup_gran_bt(curr, se);
	if (vdiff > gran)
		return 1;

	return 0;
}

static void set_last_buddy_bt(struct sched_entity *se)
{
	if (bt_entity_is_task(se))
		return;

	for_each_sched_bt_entity(se)
		bt_rq_of(se)->last = se;
}

static void set_next_buddy_bt(struct sched_entity *se)
{
	if (bt_entity_is_task(se))
		return;

	for_each_sched_bt_entity(se)
		bt_rq_of(se)->next = se;
}

static void set_skip_buddy_bt(struct sched_entity *se)
{
	for_each_sched_bt_entity(se)
		bt_rq_of(se)->skip = se;
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void check_preempt_wakeup_bt(struct rq *rq, struct task_struct *p, int wake_flags)
{
	struct task_struct *curr = rq->curr;
	struct sched_entity *se = &curr->bt, *pse = &p->bt;
	struct bt_rq *bt_rq = task_bt_rq(curr);
	int scale = bt_rq->nr_running >= sched_nr_latency;
	int next_buddy_marked = 0;

	if (unlikely(se == pse))
		return;

	if (sched_feat(NEXT_BUDDY) && scale && !(wake_flags & WF_FORK)) {
		set_next_buddy_bt(pse);
		next_buddy_marked = 1;
	}

	/*
	 * We can come here with TIF_NEED_RESCHED already set from new task
	 * wake up path.
	 *
	 * Note: this also catches the edge-case of curr being in a throttled
	 * group (e.g. via set_curr_task), since update_curr() (in the
	 * enqueue of curr) will have resulted in resched being set.  This
	 * prevents us from potentially nominating it as a false LAST_BUDDY
	 * below.
	 */
	if (test_tsk_need_resched(curr))
		return;

	/* BT tasks are by definition preempted by non-bt tasks. */
	if (likely(p->policy < SCHED_BT))
		goto preempt;

	if (!sched_feat(WAKEUP_PREEMPTION))
		return;

	find_matching_bt(&se, &pse);
	update_curr_bt(bt_rq_of(se));
	BUG_ON(!pse);
	if (wakeup_preempt_bt_entity(se, pse) == 1) {
		/*
		 * Bias pick_next to pick the sched entity that is
		 * triggering this preemption.
		 */
		if (!next_buddy_marked)
			set_next_buddy_bt(pse);
		goto preempt;
	}

	return;

preempt:
	resched_curr(rq);
	/*
	 * Only set the backward buddy when the current task is still
	 * on the rq. This can happen when a wakeup gets interleaved
	 * with schedule on the ->pre_schedule() or idle_balance()
	 * point, either of which can * drop the rq lock.
	 *
	 * Also, during early boot the idle thread is in the fair class,
	 * for obvious reasons its a bad idea to schedule back to it.
	 */
	if (unlikely(!se->on_rq || curr == rq->idle))
		return;

	if (sched_feat(LAST_BUDDY) && scale && bt_entity_is_task(se))
		set_last_buddy_bt(se);
}

static struct task_struct *pick_next_task_bt(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
	struct task_struct *p;
	struct bt_rq *bt_rq;
	struct sched_entity *se;

	bt_rq = &rq->bt;
	if (!bt_rq->nr_running)
		return NULL;

	if (bt_rq_throttled(bt_rq))
		return NULL;

	put_prev_task(rq, prev);

	do {
		se = pick_next_bt_entity(bt_rq);
		set_next_bt_entity(bt_rq, se);
		bt_rq = group_bt_rq(se);
	}while(bt_rq && bt_rq->nr_running);

	p = bt_task_of(se);

	return p;
}

/*
 * Account for a descheduled task:
 */
static void put_prev_task_bt(struct rq *rq, struct task_struct *prev)
{
	struct sched_entity *se = &prev->bt;
	struct bt_rq *bt_rq;

	for_each_sched_bt_entity(se) {
		bt_rq = bt_rq_of(se);
		put_prev_bt_entity(bt_rq, se);
	}
}

/*
 * sched_yield() is very simple
 *
 * The magic of dealing with the ->skip buddy is in pick_next_entity.
 */
static void yield_task_bt(struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	struct bt_rq *bt_rq = task_bt_rq(curr);
	struct sched_entity *se = &curr->bt;

	/*
	 * Are we the only task in the tree?
	 */
	if (unlikely(rq->bt_nr_running == 1))
		return;

	clear_buddies_bt(bt_rq, se);

	update_rq_clock(rq);
	/*
	 * Update run-time bt_statistics of the 'current'.
	 */
	update_curr_bt(bt_rq);
	/*
	 * Tell update_rq_clock() that we've just updated,
	 * so we don't do microscopic update in schedule()
	 * and double the fastpath cost.
	 */
	rq_clock_skip_update(rq, true);

	set_skip_buddy_bt(se);
}

static bool yield_to_task_bt(struct rq *rq, struct task_struct *p, bool preempt)
{
	struct sched_entity *se = &p->bt;

	if (!se->on_rq)
		return false;

	/* Tell the scheduler that we'd really like pse to run next. */
	set_next_buddy_bt(se);

	yield_task_bt(rq);

	return true;
}

/*
 * can_migrate_bt_task - may task p from runqueue rq be migrated to this_cpu?
 */
static
int can_migrate_bt_task(struct task_struct *p, struct lb_env *env)
{
	bool check_cpumask;

	/*
	 * We do not migrate tasks that are:
	 * 1) throttled_lb_pair, or
	 * 2) cannot be migrated to this CPU due to cpus_allowed, or
	 * 3) running (obviously), or
	 * 4) are cache-hot on their current CPU.
	 */
	if (offlinegroup_enabled && sysctl_sched_bt_ignore_cpubind)
		check_cpumask = false;
	else
		check_cpumask = true;

	if (check_cpumask && !cpumask_test_cpu(env->dst_cpu, tsk_cpus_allowed(p))) {
		int cpu;

		schedstat_inc(p->se.bt_statistics->nr_failed_migrations_affine);

		/*
		 * Remember if this task can be migrated to any other cpu in
		 * our sched_group. We may want to revisit it if we couldn't
		 * meet load balance goals by pulling other tasks on src_cpu.
		 *
		 * Also avoid computing new_dst_cpu if we have already computed
		 * one in current iteration.
		 */
		if (!env->dst_grpmask || (env->flags & LBF_SOME_PINNED))
			return 0;

		/* Prevent to re-select dst_cpu via env's cpus */
		for_each_cpu_and(cpu, env->dst_grpmask, env->cpus) {
			if (cpumask_test_cpu(cpu, tsk_cpus_allowed(p))) {
				env->flags |= LBF_SOME_PINNED;
				env->new_dst_cpu = cpu;
				break;
			}
		}

		return 0;
	}

	/* Record that we found atleast one task that could run on dst_cpu */
	env->flags &= ~LBF_ALL_PINNED;

	if (task_running(env->src_rq, p)) {
		schedstat_inc(p->se.bt_statistics->nr_failed_migrations_running);
		return 0;
	}

	return 1;
}

/*
 * move_one_bt_task tries to move exactly one task from busiest to this_rq, as
 * part of active balancing operations within "domain".
 * Returns 1 if successful and 0 otherwise.
 *
 * Called with both runqueues locked.
 */
static int move_one_bt_task(struct lb_env *env)
{
	struct task_struct *p, *n;

	list_for_each_entry_safe(p, n, &env->src_rq->bt_tasks, bt.group_node) {
		if (!can_migrate_bt_task(p, env))
			continue;

		move_task_bt(p, env);
		/*
		 * Right now, this is only the second place move_task_bt()
		 * is called, so we can safely collect move_task_bt()
		 * stats here rather than inside move_task_bt().
		 */
		schedstat_inc(env->sd->lb_gained[env->idle]);
		return 1;
	}
	return 0;
}

static unsigned long task_h_bt_load(struct task_struct *p);

/*
 * move_tasks_bt tries to move up to imbalance weighted load from busiest to
 * this_rq, as part of a balancing operation within domain "sd".
 * Returns 1 if successful and 0 otherwise.
 *
 * Called with both runqueues locked.
 */
static int move_tasks_bt(struct lb_env *env)
{
	struct list_head *tasks = &env->src_rq->bt_tasks;
	struct task_struct *p;
	unsigned long load;
	int pulled = 0;

	if (env->imbalance <= 0)
		return 0;

	while (!list_empty(tasks)) {
		p = list_first_entry(tasks, struct task_struct, bt.group_node);

		env->loop++;
		/* We've more or less seen every task there is, call it quits */
		if (env->loop > env->loop_max)
			break;

		/* take a breather every nr_migrate tasks */
		if (env->loop > env->loop_break) {
			env->loop_break += sched_nr_migrate_break;
			env->flags |= LBF_NEED_BREAK;
			break;
		}

		if (!can_migrate_bt_task(p, env))
			goto next;

		load = task_h_bt_load(p);

		if (sched_feat(LB_MIN) && load < 16 && !env->sd->nr_balance_failed_bt)
			goto next;

		if ((load / 2) > env->imbalance)
			goto next;

		move_task_bt(p, env);
		pulled++;
		env->imbalance -= load;

#ifdef CONFIG_PREEMPT
		/*
		 * NEWIDLE balancing is a source of latency, so preemptible
		 * kernels will stop after the first task is pulled to minimize
		 * the critical section.
		 */
		if (env->idle == CPU_NEWLY_IDLE)
			break;
#endif

		/*
		 * We only want to steal up to the prescribed amount of
		 * weighted load.
		 */
		if (env->imbalance <= 0)
			break;

		continue;
next:
		list_move_tail(&p->bt.group_node, tasks);
	}

	/*
	 * Right now, this is one of only two places move_task_bt() is called,
	 * so we can safely collect move_task_bt() stats here rather than
	 * inside move_task_bt().
	 */
	schedstat_add(env->sd->lb_gained[env->idle], pulled);

	return pulled;
}

#ifdef CONFIG_BT_GROUP_SCHED
static void update_blocked_averages_bt(int cpu)
{
	struct rq *rq = cpu_rq(cpu);
	struct bt_rq *bt_rq;
	unsigned long flags;

	raw_spin_lock_irqsave(&rq->lock, flags);
	update_rq_clock(rq);

	/*
	 * Iterates the task_group tree in a bottom up fashion, see
	 * list_add_leaf_cfs_rq() for details.
	 */
	for_each_leaf_bt_rq(rq, bt_rq) {
		if (update_bt_rq_load_avg(bt_rq_clock_task(bt_rq), bt_rq))
			update_tg_bt_load_avg(bt_rq, 0);
	}

	raw_spin_unlock_irqrestore(&rq->lock, flags);
}

/*
 * Compute the cpu's hierarchical load factor for each task group.
 * This needs to be done in a top-down fashion because the load of a child
 * group is a fraction of its parents load.
 */
static int tg_bt_load_down(struct task_group *tg, void *data)
{
	unsigned long load;
	long cpu = (long)data;

	if (!tg->parent) {
		load = cpu_rq(cpu)->bt_load.weight;
	} else {
		load = tg->parent->bt_rq[cpu]->h_load;
		load *= tg->bt[cpu]->load.weight;
		load /= tg->parent->bt_rq[cpu]->load.weight + 1;
	}

	tg->bt_rq[cpu]->h_load = load;

	return 0;
}

static void update_h_bt_load(long cpu)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long now = jiffies;

	if (rq->h_bt_load_throttle == now)
		return;

	rq->h_bt_load_throttle = now;

	rcu_read_lock();
	walk_tg_tree(tg_bt_load_down, tg_nop, (void *)cpu);
	rcu_read_unlock();
}

static unsigned long task_h_bt_load(struct task_struct *p)
{
	struct bt_rq *bt_rq = task_bt_rq(p);
	unsigned long load;

	load = p->bt.load.weight;
	load = div_u64(load * bt_rq->h_load, bt_rq->load.weight + 1);

	return load;
}
#else
static inline void update_blocked_averages_bt(int cpu)
{
}

static inline void update_h_bt_load(long cpu)
{
}

static unsigned long task_h_bt_load(struct task_struct *p)
{
	return p->bt.load.weight;
}
#endif

#ifdef CONFIG_CFS_BANDWIDTH
/* cpu online calback */
static void __maybe_unused update_runtime_enabled(struct rq *rq)
{
	return;
	#if 0
	struct task_group *tg;

	lockdep_assert_held(&rq->lock);

	rcu_read_lock();
	list_for_each_entry_rcu(tg, &task_groups, list) {
		struct cfs_bandwidth *cfs_b = &tg->cfs_bandwidth;
		struct cfs_rq *cfs_rq = tg->cfs_rq[cpu_of(rq)];

		raw_spin_lock(&cfs_b->lock);
		cfs_rq->runtime_enabled = cfs_b->quota != RUNTIME_INF;
		raw_spin_unlock(&cfs_b->lock);
	}
	rcu_read_unlock();
	#endif
}

/* cpu offline callback */
static void __maybe_unused unthrottle_offline_cfs_rqs(struct rq *rq)
{
	return;
	#if 0
	struct task_group *tg;

	lockdep_assert_held(&rq->lock);

	rcu_read_lock();
	list_for_each_entry_rcu(tg, &task_groups, list) {
		struct bt_rq *bt_rq = tg->bt_rq[cpu_of(rq)];

		if (!bt_rq->runtime_enabled)
			continue;

		/*
		 * clock_task is not advancing so we just need to make sure
		 * there's some valid quota amount
		 */
		bt_rq->runtime_remaining = 1;
		/*
		 * Offline rq is schedulable till cpu is completely disabled
		 * in take_cpu_down(), so we prevent new cfs throttling here.
		 */
		bt_rq->runtime_enabled = 0;

		if (bt_rq_throttled(bt_rq))
			unthrottle_bt_rq(bt_rq);
	}
	rcu_read_unlock();
	#endif
}
#else
static inline void update_runtime_enabled(struct rq *rq) {}
static inline void unthrottle_offline_cfs_rqs(struct rq *rq) {}
#endif

#ifdef CONFIG_SMP
static void rq_online_bt(struct rq *rq)
{
	update_sysctl();
	__enable_bt_runtime(rq);

	update_runtime_enabled(rq);
}

static void rq_offline_bt(struct rq *rq)
{
	update_sysctl();
	__disable_bt_runtime(rq);

	unthrottle_offline_cfs_rqs(rq);
}

/**
 * fix_small_imbalance_bt - Calculate the minor imbalance that exists
 *			amongst the groups of a sched_domain, during
 *			load balancing.
 * @env: The load balancing environment.
 * @sds: Statistics of the sched_domain whose imbalance is to be calculated.
 */
static inline
void fix_small_imbalance_bt(struct lb_env *env, struct sd_lb_stats_bt *sds)
{
	unsigned long tmp, pwr_now = 0, pwr_move = 0;
	unsigned int imbn = 2;
	unsigned long scaled_busy_load_per_task;
	unsigned long mid_load;
	unsigned int busiest_power = max(sds->busiest->sgc->capacity_bt,
					(unsigned long)SCHED_CAPACITY_SCALE);
	unsigned int this_power = max(sds->this->sgc->capacity_bt,
					(unsigned long)SCHED_CAPACITY_SCALE);

	if (sds->this_nr_running) {
		sds->this_load_per_task /= sds->this_nr_running;
		if (sds->busiest_load_per_task >
				sds->this_load_per_task)
			imbn = 1;
	} else {
		sds->this_load_per_task =
			cpu_avg_bt_load_per_task(env->dst_cpu) +
				cpu_avg_bt_load_per_task(env->dst_cpu);
	}

	scaled_busy_load_per_task = sds->busiest_load_per_task
					 * SCHED_CAPACITY_SCALE;
	scaled_busy_load_per_task /= busiest_power;

	mid_load = (sds->max_load + sds->max_bt_load) >> 1;
	if (mid_load - sds->this_load + scaled_busy_load_per_task >=
			(scaled_busy_load_per_task * imbn)) {
		env->imbalance = sds->busiest_load_per_task;
		return;
	}

	/*
	 * OK, we don't have enough imbalance to justify moving tasks,
	 * however we may be able to increase total CPU power used by
	 * moving them.
	 */

	pwr_now += busiest_power *
			min(sds->busiest_load_per_task, mid_load);
	pwr_now += this_power *
			min(sds->this_load_per_task, sds->this_bt_load);
	pwr_now /= SCHED_CAPACITY_SCALE;

	/* Amount of load we'd subtract */
	tmp = (sds->busiest_load_per_task * SCHED_CAPACITY_SCALE) / busiest_power;
	if (mid_load > tmp)
		pwr_move += busiest_power *
			min(sds->busiest_load_per_task, mid_load - tmp);

	/* Amount of load we'd add */
	if (mid_load * busiest_power <
		sds->busiest_load_per_task * SCHED_CAPACITY_SCALE)
		tmp = (mid_load * busiest_power) / this_power;
	else
		tmp = (sds->busiest_load_per_task * SCHED_CAPACITY_SCALE) / this_power;

	pwr_move += this_power * min(sds->this_load_per_task, sds->this_bt_load + tmp);
	pwr_move /= SCHED_CAPACITY_SCALE;

	/* Move if we gain throughput */
	if (pwr_move > pwr_now)
		env->imbalance = sds->busiest_load_per_task;
}

/**
 * calculate_imbalance_bt - Calculate the amount of imbalance present within the
 *			 groups of a given sched_domain during load balance.
 * @env: load balance environment
 * @sds: statistics of the sched_domain whose imbalance is to be calculated.
 */
static inline void calculate_imbalance_bt(struct lb_env *env, struct sd_lb_stats_bt *sds)
{
	unsigned long max_pull, load_above_capacity = ~0UL;
	unsigned int busiest_power, this_power;

	sds->busiest_load_per_task /= sds->busiest_nr_running;
	if (sds->group_imb) {
		sds->busiest_load_per_task =
			min(sds->busiest_load_per_task, sds->avg_bt_load);
	}

	/*
	 * In the presence of smp nice balancing, certain scenarios can have
	 * max load less than avg load(as we skip the groups at or below
	 * its cpu_capacity, while calculating max_load..)
	 */
	if (sds->max_load < sds->avg_bt_load) {
		env->imbalance = 0;
		return fix_small_imbalance_bt(env, sds);
	}

	busiest_power = max(sds->busiest->sgc->capacity_bt, (unsigned long)SCHED_CAPACITY_SCALE);
	this_power = max(sds->this->sgc->capacity_bt, (unsigned long)SCHED_CAPACITY_SCALE);
	if (!sds->group_imb) {
		/*
		 * Don't want to pull so many tasks that a group would go idle.
		 */
		load_above_capacity = (sds->busiest_nr_running -
						sds->busiest_group_capacity);

		load_above_capacity *= (SCHED_LOAD_SCALE * SCHED_CAPACITY_SCALE);

		load_above_capacity /= busiest_power;
	}

	/*
	 * We're trying to get all the cpus to the average_load, so we don't
	 * want to push ourselves above the average load, nor do we wish to
	 * reduce the max loaded cpu below the average load. At the same time,
	 * we also don't want to reduce the group load below the group capacity
	 * (so that we can implement power-savings policies etc). Thus we look
	 * for the minimum possible imbalance.
	 * Be careful of negative numbers as they'll appear as very large values
	 * with unsigned longs.
	 */
	max_pull = min(((sds->max_bt_load + sds->max_load) >> 1) - sds->avg_bt_load,
			load_above_capacity);

	/* How much load to actually move to equalise the imbalance */
	env->imbalance = min(max_pull * busiest_power,
		(sds->avg_bt_load - sds->this_bt_load) * this_power)
			/ SCHED_CAPACITY_SCALE;

	/*
	 * if *imbalance is less than the average load per runnable task
	 * there is no guarantee that any tasks will be moved so we'll have
	 * a think about bumping its value to force at least one task to be
	 * moved
	 */
	if (env->imbalance < sds->busiest_load_per_task)
		return fix_small_imbalance_bt(env, sds);

}

static int bt_group_balance_cpu(int cpu, struct sched_group *sg)
{
	int balance_cpu = sg->bt_balance_cpu;
	struct rq *rq = NULL;

	if (balance_cpu != -1) {
		rq = cpu_rq(balance_cpu);
		if (rq->bt.bt_throttled || !rq->bt.bt_runtime)
			rq->do_lb = 0;
	}

	if (balance_cpu == -1 || !rq->do_lb) {
		if (cpumask_test_cpu(cpu, group_balance_mask(sg))) {
			balance_cpu = cpu;

			if (!test_bit(NOHZ_TICK_STOPPED, &cpu_rq(balance_cpu)->nohz_flags)) {
				sg->bt_balance_cpu = balance_cpu;
				cpu_rq(balance_cpu)->do_lb = 1;
			}
		}
	}

	return balance_cpu;
}

static inline int
fix_small_capacity_bt(struct sched_domain *sd, struct sched_group *group)
{
	/*
	 * Only siblings can have significantly less than SCHED_CAPACITY_SCALE
	 */
	if (!(sd->flags & SD_SHARE_CPUCAPACITY))
		return 0;

	/*
	 * If ~90% of the cpu_capacity is still there, we're good.
	 */
	if (group->sgc->capacity_bt * 32 > group->sgc->capacity_orig * 29)
		return 1;

	return 0;
}

/**
 * update_sg_lb_stats_bt - Update sched_group's statistics for load balancing.
 * @env: The load balancing environment.
 * @group: sched_group whose statistics are to be updated.
 * @load_idx: Load index of sched_domain of this_cpu for load calc.
 * @local_group: Does group contain this_cpu.
 * @balance: Should we balance.
 * @sgs: variable to hold the statistics for this group.
 */
static inline void update_sg_lb_stats_bt(struct lb_env *env,
			struct sched_group *group, int load_idx,
			int local_group, int *balance, struct sg_lb_stats_bt *sgs,
			bool *overload)
{
	unsigned long nr_running, bt_nr_running;
	unsigned long max_nr_running, min_nr_running;
	unsigned long load, max_cpu_load, min_cpu_load;
	unsigned int balance_cpu = -1, first_idle_cpu = 0;
	unsigned long avg_load_per_task = 0;
	unsigned long vain_power = 0, use_power = 0;
	int cpu_num = 0, cpu_i = 0;
	bool sg_vain = false;
	int i, fair_load;

	fair_load = sysctl_sched_bt_load_fair ? 1 : 0;

	if (local_group){
		balance_cpu = bt_group_balance_cpu(env->dst_cpu, group);
		balance_cpu = balance_cpu != -1 ? balance_cpu : group_balance_cpu(group);
	}

	/* Tally up the load of all CPUs in the group */
	max_cpu_load = 0;
	min_cpu_load = (~0UL) >> 1;
	max_nr_running = 0;
	min_nr_running = (~0UL) >> 1;

	for_each_cpu_and(i, sched_group_cpus(group), env->cpus) {
		struct rq *rq = cpu_rq(i);
		u32 load_factor;
		bool block;

		bt_nr_running = rq->bt.h_nr_running;
		nr_running = rq->nr_running + bt_nr_running - rq->bt_nr_running;
		load_factor = bt_load_factor(rq);

		cpu_num++;
		block = false;
		if (rq->nr_running > rq->bt_nr_running || rq->bt.bt_throttled ||
		    !rq->bt.bt_runtime) {
			cpu_i++;
			block = true;
			vain_power += rq->cpu_capacity;
		}

		/* Bias balancing toward cpus of our domain */
		if (local_group) {
			if (idle_cpu(i) && rq->bt.bt_runtime && !first_idle_cpu &&
					cpumask_test_cpu(i, group_balance_mask(group))) {
				first_idle_cpu = 1;
				balance_cpu = i;
			}

			load = fair_load ? target_load(i, load_idx) : 0;
			sgs->group_bt_load += load + target_bt_load(i, load_idx);
			load += target_bt_load(i, load_idx) << load_factor;
			sgs->group_load += load;
		} else {
			load = fair_load ? source_load(i, load_idx) : 0;
			sgs->group_bt_load += load + source_bt_load(i, load_idx);
			load += source_bt_load(i, load_idx) << load_factor;
			sgs->group_load += load;
			if (load > max_cpu_load)
				max_cpu_load = load;
			if (min_cpu_load > load && !block)
				min_cpu_load = load;

			if (nr_running > max_nr_running)
				max_nr_running = nr_running;
			if (min_nr_running > nr_running && !block)
				min_nr_running = nr_running;
		}

		if (load || rq->nr_running > rq->bt_nr_running)
			use_power += rq->cpu_capacity;

		sgs->sum_nr_running += bt_nr_running;

		if (rq->bt.h_nr_running && (rq->nr_running > 1 || block))
			*overload = true;

		sgs->sum_weighted_load += bt_weighted_cpuload(i);
		if (idle_cpu(i))
			sgs->idle_cpus++;
	}

	if(cpu_num && cpu_num == cpu_i)
		sg_vain = true;

	/*
	 * First idle cpu or the first cpu(busiest) in this sched group
	 * is eligible for doing load balancing at this and above
	 * domains. In the newly idle case, we will allow all the cpu's
	 * to do the newly idle load balance.
	 */
	if (local_group) {
		if (unlikely(sg_vain)) {
			*balance = 0;
			return;
		}

		if (env->idle != CPU_NEWLY_IDLE) {
			if (balance_cpu != env->dst_cpu) {
				*balance = 0;
				return;
			}
			update_group_capacity(env->sd, env->dst_cpu);
		} else if (time_after_eq(jiffies, group->sgc->next_update))
			update_group_capacity(env->sd, env->dst_cpu);
	}

	/* Adjust by relative CPU power of the group */
	sgs->avg_load = (sgs->group_load * SCHED_CAPACITY_SCALE) /
				max(use_power, (unsigned long)SCHED_CAPACITY_SCALE);
	sgs->avg_bt_load = (sgs->group_bt_load * SCHED_CAPACITY_SCALE) /
				max(use_power, (unsigned long)SCHED_CAPACITY_SCALE);

	group->sgc->capacity_bt = max((s64)(group->sgc->capacity - vain_power), (s64)1);

	if (unlikely(sg_vain))
		return;

	/*
	 * Consider the group unbalanced when the imbalance is larger
	 * than the average weight of a task.
	 *
	 * APZ: with cgroup the avg task weight can vary wildly and
	 *      might not be a suitable number - should we keep a
	 *      normalized nr_running number somewhere that negates
	 *      the hierarchy?
	 */
	if (sgs->sum_nr_running)
		avg_load_per_task = sgs->sum_weighted_load / sgs->sum_nr_running;

	if (max_cpu_load >= (min_cpu_load + avg_load_per_task) &&
	    max_nr_running > (min_nr_running + 1))
		sgs->group_imb = 1;

	sgs->group_capacity = DIV_ROUND_CLOSEST(group->sgc->capacity_bt,
						SCHED_CAPACITY_SCALE);
	if (!sgs->group_capacity)
		sgs->group_capacity = fix_small_capacity_bt(env->sd, group);
	sgs->group_weight = group->group_weight - cpu_i;

	if (sgs->group_capacity > sgs->sum_nr_running)
		sgs->group_has_capacity = 1;
}

static bool update_sd_pick_busiest_bt(struct lb_env *env,
				   struct sd_lb_stats_bt *sds,
				   struct sched_group *sg,
				   struct sg_lb_stats_bt *sgs)
{
	if (sgs->avg_load <= sds->max_load)
		return false;

	if (sgs->sum_nr_running > sgs->group_capacity)
		return true;

	if (sgs->group_imb)
		return true;

	/*
	 * ASYM_PACKING needs to move all the work to the lowest
	 * numbered CPUs in the group, therefore mark all grou
	 * higher than ourself as busy.
	 */
	if ((env->sd->flags & SD_ASYM_PACKING) && sgs->sum_nr_running &&
	    env->dst_cpu < group_first_cpu(sg)) {
		if (!sds->busiest)
			return true;

		if (group_first_cpu(sds->busiest) > group_first_cpu(sg))
			return true;
	}

	if (sgs->group_load > sgs->group_bt_load && sgs->avg_load > sds->max_load)
		return true;

	return false;
}

/**
 * update_sd_lb_stats_bt - Update sched_domain's statistics for load balancing.
 * @env: The load balancing environment.
 * @balance: Should we balance.
 * @sds: variable to hold the statistics for this sched_domain.
 */
static inline void update_sd_lb_stats_bt(struct lb_env *env,
					int *balance, struct sd_lb_stats_bt *sds)
{
	struct sched_domain *child = env->sd->child;
	struct sched_group *sg = env->sd->groups;
	struct sg_lb_stats_bt sgs;
	int load_idx, prefer_sibling = 0;
	bool overload = false;

	if (child && child->flags & SD_PREFER_SIBLING)
		prefer_sibling = 1;

	load_idx = get_sd_load_idx_bt(env->sd, env->idle);

	do {
		int local_group;

		local_group = cpumask_test_cpu(env->dst_cpu, sched_group_cpus(sg));
		memset(&sgs, 0, sizeof(sgs));
		update_sg_lb_stats_bt(env, sg, load_idx, local_group, balance, &sgs,
						&overload);

		if (local_group && !(*balance))
			return;

		sds->total_load += sgs.group_load;
		sds->total_bt_load += sgs.group_bt_load;
		sds->total_pwr += sg->sgc->capacity_bt;

		/*
		 * In case the child domain prefers tasks go to siblings
		 * first, lower the sg capacity to one so that we'll try
		 * and move all the excess tasks away. We lower the capacity
		 * of a group only if the local group has the capacity to fit
		 * these excess tasks, i.e. nr_running < group_capacity. The
		 * extra check prevents the case where you always pull from the
		 * heaviest group when it is already under-utilized (possible
		 * with a large weight task outweighs the tasks on the system).
		 */
		if (prefer_sibling && !local_group && sds->this_has_capacity)
			sgs.group_capacity = min(sgs.group_capacity, 1UL);

		if (local_group) {
			sds->this_load = sgs.avg_load;
			sds->this_bt_load = sgs.avg_bt_load;
			sds->this = sg;
			sds->this_nr_running = sgs.sum_nr_running;
			sds->this_load_per_task = sgs.sum_weighted_load;
			sds->this_has_capacity = sgs.group_has_capacity;
			sds->this_idle_cpus = sgs.idle_cpus;
		} else if (update_sd_pick_busiest_bt(env, sds, sg, &sgs)) {
			sds->max_load = sgs.avg_load;
			sds->max_bt_load = sgs.avg_bt_load;
			sds->busiest = sg;
			sds->busiest_nr_running = sgs.sum_nr_running;
			sds->busiest_idle_cpus = sgs.idle_cpus;
			sds->busiest_group_capacity = sgs.group_capacity;
			sds->busiest_load_per_task = sgs.sum_weighted_load;
			sds->busiest_has_capacity = sgs.group_has_capacity;
			sds->busiest_group_weight = sgs.group_weight;
			sds->group_imb = sgs.group_imb;
		}

		sg = sg->next;
	} while (sg != env->sd->groups);

	if (!env->sd->parent) {
		/* update overload indicator if we are at root domain */
		if (env->dst_rq->rd->overload_bt != overload)
			env->dst_rq->rd->overload_bt = overload;
	}

}

static int check_asym_packing_bt(struct lb_env *env, struct sd_lb_stats_bt *sds)
{
	int busiest_cpu;
	unsigned int power;

	if (!(env->sd->flags & SD_ASYM_PACKING))
		return 0;

	if (!sds->busiest)
		return 0;

	busiest_cpu = group_first_cpu(sds->busiest);
	if (env->dst_cpu > busiest_cpu)
		return 0;

	power = max(sds->busiest->sgc->capacity_bt, (unsigned long)SCHED_CAPACITY_SCALE);
	env->imbalance = DIV_ROUND_CLOSEST(
		(sds->max_load + sds->max_bt_load) * power >> 1, SCHED_CAPACITY_SCALE);

	return 1;
}

/******* find_busiest_group_bt() helpers end here *********************/

/**
 * find_busiest_group - Returns the busiest group within the sched_domain
 * if there is an imbalance. If there isn't an imbalance, and
 * the user has opted for power-savings, it returns a group whose
 * CPUs can be put to idle by rebalancing those tasks elsewhere, if
 * such a group exists.
 *
 * Also calculates the amount of weighted load which should be moved
 * to restore balance.
 *
 * @env: The load balancing environment.
 * @balance: Pointer to a variable indicating if this_cpu
 *	is the appropriate cpu to perform load balancing at this_level.
 *
 * Returns:	- the busiest group if imbalance exists.
 *		- If no imbalance and user has opted for power-savings balance,
 *		   return the least loaded group whose CPUs can be
 *		   put to idle by rebalancing its tasks onto our group.
 */
static struct sched_group *
find_busiest_group_bt(struct lb_env *env, int *balance)
{
	struct sd_lb_stats_bt sds;

	memset(&sds, 0, sizeof(sds));

	/*
	 * Compute the various statistics relavent for load balancing at
	 * this level.
	 */
	update_sd_lb_stats_bt(env, balance, &sds);

	/*
	 * this_cpu is not the appropriate cpu to perform load balancing at
	 * this level.
	 */
	if (!(*balance))
		goto ret;

	if ((env->idle == CPU_IDLE || env->idle == CPU_NEWLY_IDLE) &&
	    check_asym_packing_bt(env, &sds))
		return sds.busiest;

	/* There is no busy sibling group to pull tasks from */
	if (!sds.busiest || sds.busiest_nr_running == 0)
		goto out_balanced;

	sds.avg_load = (SCHED_CAPACITY_SCALE * sds.total_load) / (sds.total_pwr + 1);
	sds.avg_bt_load = (SCHED_CAPACITY_SCALE * sds.total_bt_load)/(sds.total_pwr+1);

	/*
	 * If the busiest group is imbalanced the below checks don't
	 * work because they assumes all things are equal, which typically
	 * isn't true due to cpus_allowed constraints and the like.
	 */
	if (sds.group_imb)
		goto force_balance;

	/* SD_BALANCE_NEWIDLE trumps SMP nice when underutilized */
	if (env->idle == CPU_NEWLY_IDLE && sds.this_has_capacity &&
			!sds.busiest_has_capacity)
		goto force_balance;

	/*
	 * If the local group is more busy than the selected busiest group
	 * don't try and pull any tasks.
	 */
	if (sds.this_load >= sds.max_load || sds.this_bt_load >= sds.avg_bt_load)
		goto out_balanced;

	/*
	 * Don't pull any tasks if this group is already above the domain
	 * average load.
	 */
	if (sds.this_load >= sds.avg_load)
		goto out_balanced;

	if (env->idle == CPU_IDLE) {
		/*
		 * This cpu is idle. If the busiest group load doesn't
		 * have more tasks than the number of available cpu's and
		 * there is no imbalance between this and busiest group
		 * wrt to idle cpu's, it is balanced.
		 */
		if ((sds.this_idle_cpus <= sds.busiest_idle_cpus + 1) &&
		    sds.busiest_nr_running <= sds.busiest_group_weight)
			goto out_balanced;
	} else {
		/*
		 * In the CPU_NEWLY_IDLE, CPU_NOT_IDLE cases, use
		 * imbalance_pct to be conservative.
		 */
		if (100 * sds.max_load <= env->sd->imbalance_pct * sds.this_load)
			goto out_balanced;
	}

force_balance:
	/* Looks like there is an imbalance. Compute it */
	calculate_imbalance_bt(env, &sds);
	return sds.busiest;

out_balanced:
ret:
	env->imbalance = 0;
	return NULL;
}

/*
 * find_busiest_queue_bt - find the busiest runqueue among the cpus in group.
 */
static struct rq *find_busiest_queue_bt(struct lb_env *env,
				     struct sched_group *group)
{
	struct rq *busiest = NULL, *rq;
	unsigned long max_load = 0;
	int i, fair_load, max_factor = 0;

	fair_load = sysctl_sched_bt_load_fair ? 1 : 0;

	for_each_cpu(i, sched_group_cpus(group)) {
		unsigned long power = capacity_of_bt(i);
		unsigned long capacity = DIV_ROUND_CLOSEST(power,
							   SCHED_CAPACITY_SCALE);
		unsigned long wl;
		u32 load_factor;

		if (!capacity)
			capacity = fix_small_capacity_bt(env->sd, group);

		if (!cpumask_test_cpu(i, env->cpus))
			continue;

		rq = cpu_rq(i);
		load_factor = bt_load_factor(rq);
		wl = bt_weighted_cpuload(i) << load_factor;
		if(fair_load)
			wl += weighted_cpuload(rq);

		/*
		 * When comparing with imbalance, use bt_weighted_cpuload()
		 * which is not scaled with the cpu power.
		 */
		if (capacity && (rq->nr_running == 1 || !rq->bt.h_nr_running) &&
		    wl > env->imbalance)
			continue;

		/*
		 * For the load comparisons with the other cpu's, consider
		 * the bt_weighted_cpuload() scaled with the cpu power, so that
		 * the load can be moved away from the cpu that is potentially
		 * running at a lower capacity.
		 */
		wl = (wl * SCHED_CAPACITY_SCALE) / power;

		if (wl > max_load) {
			max_load = wl;
			busiest = rq;
			max_factor = load_factor;
		}
	}

	if (unlikely(max_factor)){
		 env->flags |= LBF_BT_LB;
	}

	return busiest;
}

/*
 * active_bt_load_balance_cpu_stop is run by cpu stopper. It pushes
 * running tasks off the busiest CPU onto idle CPUs. It requires at
 * least 1 task to be running on each physical CPU where possible, and
 * avoids physical / logical imbalances.
 */
static int active_bt_load_balance_cpu_stop(void *data)
{
	struct rq *busiest_rq = data;
	int busiest_cpu = cpu_of(busiest_rq);
	int target_cpu = busiest_rq->push_cpu_bt;
	struct rq *target_rq = cpu_rq(target_cpu);
	struct sched_domain *sd;

	raw_spin_lock_irq(&busiest_rq->lock);

	/* make sure the requested cpu hasn't gone down in the meantime */
	if (unlikely(busiest_cpu != smp_processor_id() ||
		     !busiest_rq->active_balance_bt))
		goto out_unlock;

	/* Is there any task to move? */
	if (busiest_rq->nr_running <= 1 || !busiest_rq->bt_nr_running)
		goto out_unlock;

	/*
	 * This condition is "impossible", if it occurs
	 * we need to fix it. Originally reported by
	 * Bjorn Helgaas on a 128-cpu setup.
	 */
	BUG_ON(busiest_rq == target_rq);

	/* move a task from busiest_rq to target_rq */
	double_lock_balance(busiest_rq, target_rq);

	/* Search for an sd spanning us and the target CPU. */
	rcu_read_lock();
	for_each_domain(target_cpu, sd) {
		if ((sd->flags & SD_LOAD_BALANCE) &&
		    cpumask_test_cpu(busiest_cpu, sched_domain_span(sd)))
				break;
	}

	if (likely(sd)) {
		struct lb_env env = {
			.sd		= sd,
			.dst_cpu	= target_cpu,
			.dst_rq		= target_rq,
			.src_cpu	= busiest_rq->cpu,
			.src_rq		= busiest_rq,
			.idle		= CPU_IDLE,
		};

		schedstat_inc(sd->alb_count);

		if (move_one_bt_task(&env))
			schedstat_inc(sd->alb_pushed);
		else
			schedstat_inc(sd->alb_failed);
	}
	rcu_read_unlock();
	double_unlock_balance(busiest_rq, target_rq);
out_unlock:
	busiest_rq->active_balance_bt = 0;
	raw_spin_unlock_irq(&busiest_rq->lock);
	return 0;
}

#define MAX_PINNED_INTERVAL_BT	512

/* Working cpumask for load_balance and load_balance_newidle. */
DEFINE_PER_CPU(cpumask_var_t, bt_load_balance_mask);

static int need_active_balance_bt(struct lb_env *env)
{
	struct sched_domain *sd = env->sd;

	if (env->idle == CPU_NEWLY_IDLE) {

		/*
		 * ASYM_PACKING needs to force migrate tasks from busy but
		 * higher numbered CPUs in order to pack all tasks in the
		 * lowest numbered CPUs.
		 */
		if ((sd->flags & SD_ASYM_PACKING) && env->src_cpu > env->dst_cpu)
			return 1;
	}

	return unlikely(sd->nr_balance_failed_bt > sd->cache_nice_tries+2);
}

/*
 * Check this_cpu to ensure it is balanced within domain. Attempt to move
 * tasks if there is an imbalance.
 */
static int load_balance_bt(int this_cpu, struct rq *this_rq,
			struct sched_domain *sd, enum cpu_idle_type idle,
			int *balance)
{
	int ld_moved, cur_ld_moved, active_balance = 0;
	struct sched_group *group;
	struct rq *busiest;
	unsigned long flags;
	struct cpumask *cpus = this_cpu_cpumask_var_ptr(bt_load_balance_mask);

	struct lb_env env = {
		.sd		= sd,
		.dst_cpu	= this_cpu,
		.dst_rq		= this_rq,
		.dst_grpmask    = sched_group_span(sd->groups),
		.idle		= idle,
		.loop_break	= sched_nr_migrate_break,
		.cpus		= cpus,
	};

	bool check_cpumask;

	if (offlinegroup_enabled && sysctl_sched_bt_ignore_cpubind)
		check_cpumask = true;
	else
		check_cpumask = false;

	/*
	 * For NEWLY_IDLE load_balancing, we don't need to consider
	 * other cpus in our group
	 */
	if (idle == CPU_NEWLY_IDLE)
		env.dst_grpmask = NULL;

	cpumask_and(cpus, sched_domain_span(sd), cpu_active_mask);

	schedstat_inc(sd->lb_count[idle]);

redo:
	group = find_busiest_group_bt(&env, balance);

	if (*balance == 0)
		goto out_balanced;

	if (!group) {
		schedstat_inc(sd->lb_nobusyg[idle]);
		goto out_balanced;
	}

	busiest = find_busiest_queue_bt(&env, group);
	if (!busiest) {
		schedstat_inc(sd->lb_nobusyq[idle]);
		goto out_balanced;
	}

	BUG_ON(busiest == env.dst_rq);

	schedstat_add(sd->lb_imbalance[idle], env.imbalance);

	ld_moved = 0;
	if ((busiest->nr_running > 1 || busiest->bt.bt_throttled
	     || !busiest->bt.bt_runtime) && busiest->bt.h_nr_running) {
		/*
		 * Attempt to move tasks. If find_busiest_group has found
		 * an imbalance but busiest->nr_running <= 1, the group is
		 * still unbalanced. ld_moved simply stays zero, so it is
		 * correctly treated as an imbalance.
		 */
		env.flags |= LBF_ALL_PINNED;
		env.src_cpu   = busiest->cpu;
		env.src_rq    = busiest;
		env.loop_max  = min(sysctl_sched_nr_migrate, busiest->bt.h_nr_running);

		update_h_bt_load(env.src_cpu);
more_balance:
		local_irq_save(flags);
		double_rq_lock(env.dst_rq, busiest);
		update_rq_clock(busiest);

		/*
		 * cur_ld_moved - load moved in current iteration
		 * ld_moved     - cumulative load moved across iterations
		 */
		cur_ld_moved = move_tasks_bt(&env);
		ld_moved += cur_ld_moved;
		double_rq_unlock(env.dst_rq, busiest);
		local_irq_restore(flags);

		/*
		 * some other cpu did the load balance for us.
		 */
		if (cur_ld_moved && env.dst_cpu != smp_processor_id())
			resched_cpu(env.dst_cpu);

		if (env.flags & LBF_NEED_BREAK) {
			env.flags &= ~LBF_NEED_BREAK;
			goto more_balance;
		}

		/*
		 * Revisit (affine) tasks on src_cpu that couldn't be moved to
		 * us and move them to an alternate dst_cpu in our sched_group
		 * where they can run. The upper limit on how many times we
		 * iterate on same src_cpu is dependent on number of cpus in our
		 * sched_group.
		 *
		 * This changes load balance semantics a bit on who can move
		 * load to a given_cpu. In addition to the given_cpu itself
		 * (or a ilb_cpu acting on its behalf where given_cpu is
		 * nohz-idle), we now have balance_cpu in a position to move
		 * load to given_cpu. In rare situations, this may cause
		 * conflicts (balance_cpu and given_cpu/ilb_cpu deciding
		 * _independently_ and at _same_ time to move some load to
		 * given_cpu) causing exceess load to be moved to given_cpu.
		 * This however should not happen so much in practice and
		 * moreover subsequent load balance cycles should correct the
		 * excess load moved.
		 */
		if ((env.flags & LBF_SOME_PINNED) && env.imbalance > 0) {

			env.dst_rq	 = cpu_rq(env.new_dst_cpu);
			env.dst_cpu	 = env.new_dst_cpu;
			env.flags	&= ~LBF_SOME_PINNED;
			env.loop	 = 0;
			env.loop_break	 = sched_nr_migrate_break;

			/* Prevent to re-select dst_cpu via env's cpus */
			cpumask_clear_cpu(env.dst_cpu, env.cpus);

			/*
			 * Go back to "more_balance" rather than "redo" since we
			 * need to continue with same src_cpu.
			 */
			goto more_balance;
		}

		/* All tasks on this runqueue were pinned by CPU affinity */
		if (unlikely(env.flags & LBF_ALL_PINNED)) {
			cpumask_clear_cpu(cpu_of(busiest), cpus);
			if (!cpumask_empty(cpus)) {
				env.loop = 0;
				env.loop_break = sched_nr_migrate_break;
				goto redo;
			}
			goto out_balanced;
		}
	}

	if (!ld_moved) {
		schedstat_inc(sd->lb_failed[idle]);
		/*
		 * Increment the failure counter only on periodic balance.
		 * We do not want newidle balance, which can be very
		 * frequent, pollute the failure counter causing
		 * excessive cache_hot migrations and active balances.
		 */
		if (idle != CPU_NEWLY_IDLE)
			sd->nr_balance_failed_bt++;

		if (need_active_balance_bt(&env)) {
			raw_spin_lock_irqsave(&busiest->lock, flags);

			/* don't kick the active_load_balance_cpu_stop,
			 * if the curr task on busiest cpu can't be
			 * moved to this_cpu
			 */
			if (check_cpumask &&
			    !cpumask_test_cpu(this_cpu, tsk_cpus_allowed(busiest->curr))) {
				raw_spin_unlock_irqrestore(&busiest->lock,
							    flags);
				env.flags |= LBF_ALL_PINNED;
				goto out_one_pinned;
			}

			/*
			 * ->active_balance synchronizes accesses to
			 * ->active_balance_work.  Once set, it's cleared
			 * only after active load balance is finished.
			 */
			if (!busiest->active_balance_bt) {
				busiest->active_balance_bt = 1;
				busiest->push_cpu_bt = this_cpu;
				active_balance = 1;
			}
			raw_spin_unlock_irqrestore(&busiest->lock, flags);

			if (active_balance) {
				stop_one_cpu_nowait(cpu_of(busiest),
					active_bt_load_balance_cpu_stop, busiest,
					&busiest->active_bt_balance_work);
			}

			/*
			 * We've kicked active balancing, reset the failure
			 * counter.
			 */
			sd->nr_balance_failed_bt = sd->cache_nice_tries+1;
		}
	} else
		sd->nr_balance_failed_bt = 0;

	if (likely(!active_balance)) {
		/* We were unbalanced, so reset the balancing interval */
		sd->balance_interval_bt = sd->min_interval;
	} else {
		/*
		 * If we've begun active balancing, start to back off. This
		 * case may not be covered by the all_pinned logic if there
		 * is only 1 task on the busy runqueue (because we don't call
		 * move_tasks).
		 */
		if (sd->balance_interval_bt < sd->max_interval)
			sd->balance_interval_bt *= 2;
	}

	goto out;

out_balanced:
	schedstat_inc(sd->lb_balanced[idle]);

	sd->nr_balance_failed_bt = 0;

out_one_pinned:
	/* tune up the balancing interval */
	if (((env.flags & LBF_ALL_PINNED) &&
			sd->balance_interval_bt < MAX_PINNED_INTERVAL_BT) ||
			(sd->balance_interval_bt < sd->max_interval))
		sd->balance_interval_bt *= 2;

	ld_moved = 0;
out:
	if(unlikely(env.flags & LBF_BT_LB)){
		sd->balance_interval_bt = 0;
	}

	return ld_moved;
}

/*
 * idle_balance_bt is called by schedule() if this_cpu is about to become
 * idle. Attempts to pull tasks from other CPUs.
 */
int idle_balance_bt(struct rq *this_rq, struct rq_flags *rf)
{
	struct sched_domain *sd;
	int this_cpu = this_rq->cpu;
	int pulled_task = 0;
	unsigned long next_balance = jiffies + HZ;

	if (likely(!sched_bt_on))
		return 0;

	if (this_rq->bt.bt_throttled || !this_rq->bt.bt_runtime)
		return 0;

	this_rq->idle_stamp = rq_clock(this_rq);

	rq_unpin_lock(this_rq, rf);

	if (this_rq->avg_idle < sysctl_idle_balance_bt_cost ||
			!this_rq->rd->overload_bt)
		return 0;

	/*
	 * Drop the rq->lock, but keep IRQ/preempt disabled.
	 */
	raw_spin_unlock(&this_rq->lock);

	update_blocked_averages_bt(this_cpu);
	rcu_read_lock();
	for_each_domain(this_cpu, sd) {
		unsigned long interval;
		int balance = 1;

		if (!(sd->flags & SD_LOAD_BALANCE))
			continue;

		if (sd->flags & SD_BALANCE_NEWIDLE) {
			/* If we've pulled tasks over stop searching: */
			pulled_task = load_balance_bt(this_cpu, this_rq,
						   sd, CPU_NEWLY_IDLE, &balance);
		}

		interval = msecs_to_jiffies(sd->balance_interval_bt);
		if (time_after(next_balance, sd->last_balance_bt + interval))
			next_balance = sd->last_balance_bt + interval;
		if (pulled_task) {
			this_rq->idle_stamp = 0;
			break;
		}
	}
	rcu_read_unlock();

	raw_spin_lock(&this_rq->lock);

	if (pulled_task || time_after(jiffies, this_rq->next_balance_bt)) {
		/*
		 * We are going idle. next_balance may be set based on
		 * a busy processor. So reset next_balance.
		 */
		this_rq->next_balance_bt = next_balance;
	}

	rq_repin_lock(this_rq, rf);

	return pulled_task;
}

static DEFINE_SPINLOCK(bt_balancing);

/*
 * It checks each scheduling domain to see if it is due to be balanced,
 * and initiates a balancing operation if so.
 *
 * Balancing parameters are set up in init_sched_domains.
 */
static void rebalance_domains_bt(int cpu, enum cpu_idle_type idle)
{
	int balance = 1;
	struct rq *rq = cpu_rq(cpu);
	unsigned long interval;
	struct sched_domain *sd;
	/* Earliest time when we have to do rebalance again */
	unsigned long next_balance = jiffies + 60*HZ;
	int update_next_balance = 0;
	int need_serialize;

	update_blocked_averages_bt(cpu);

	if (rq->nr_running > rq->bt_nr_running || rq->bt.bt_throttled) {
		rq->do_lb = 0;
		return;
	}

	rcu_read_lock();
	for_each_domain(cpu, sd) {
		if (!(sd->flags & SD_LOAD_BALANCE))
			continue;

		interval = sd->balance_interval_bt;
		if (idle != CPU_IDLE)
			interval *= sd->busy_factor;

		/* scale ms to jiffies */
		interval = msecs_to_jiffies(interval);
		interval = clamp(interval, 1UL, max_load_balance_interval);

		need_serialize = sd->flags & SD_SERIALIZE;

		if (need_serialize) {
			if (!spin_trylock(&bt_balancing))
				goto out;
		}

		if (time_after_eq(jiffies, sd->last_balance_bt + interval)) {
			if (load_balance_bt(cpu, rq, sd, idle, &balance)) {
				/*
				 * The LBF_SOME_PINNED logic could have changed
				 * env->dst_cpu, so we can't know our idle
				 * state even if we migrated tasks. Update it.
				 */
				idle = idle_cpu(cpu) ? CPU_IDLE : CPU_NOT_IDLE;
			}
			sd->last_balance_bt = jiffies;
		}
		if (need_serialize)
			spin_unlock(&bt_balancing);
out:
		if (time_after(next_balance, sd->last_balance_bt + interval)) {
			next_balance = sd->last_balance_bt + interval;
			update_next_balance = 1;
		}

		/*
		 * Stop the load balance at this level. There is another
		 * CPU in our sched group which is doing load balancing more
		 * actively.
		 */
		if (!balance)
			break;
	}
	rcu_read_unlock();

	/*
	 * next_balance will be updated only when there is a need.
	 * When the cpu is attached to null domain for ex, it will not be
	 * updated.
	 */
	if (likely(update_next_balance))
		rq->next_balance_bt = next_balance;
}

#ifdef CONFIG_NO_HZ_COMMON
/*
 * In CONFIG_NO_HZ_COMMON case, the idle balance kickee will do the
 * rebalancing for all the cpus for whom scheduler ticks are stopped.
 */
static void nohz_idle_balance_bt(int this_cpu, enum cpu_idle_type idle)
{
	struct rq *this_rq = cpu_rq(this_cpu);
	struct rq *rq;
	int balance_cpu;
	u64 next_balance;

	if (idle != CPU_IDLE ||
	    !test_bit(NOHZ_BALANCE_KICK, nohz_flags(this_cpu)))
		goto end;

	for_each_cpu(balance_cpu, nohz.idle_cpus_mask) {
		if (balance_cpu == this_cpu || !idle_cpu(balance_cpu))
			continue;

		/*
		 * If this cpu gets work to do, stop the load balancing
		 * work being done for other cpus. Next load
		 * balancing owner will pick it up.
		 */
		if (need_resched())
			break;

		rq = cpu_rq(balance_cpu);

		/*
		 * If time for next balance is due,
		 * do the balance.
		 */
		if (time_after_eq(jiffies, rq->next_balance_bt)) {
			raw_spin_lock_irq(&rq->lock);
			update_rq_clock(rq);
			update_idle_cpu_bt_load(rq);
			raw_spin_unlock_irq(&rq->lock);
			rebalance_domains_bt(balance_cpu, CPU_IDLE);
		}

		if (time_after(this_rq->next_balance_bt, rq->next_balance_bt))
			this_rq->next_balance_bt = rq->next_balance_bt;
	}

	next_balance = nohz.next_balance;
	next_balance = next_balance < jiffies ? this_rq->next_balance_bt :
			MIN_U(next_balance, this_rq->next_balance_bt);

	nohz.next_balance = next_balance;
end:
	clear_bit(NOHZ_BALANCE_KICK, nohz_flags(this_cpu));
}

#else
static void nohz_idle_balance_bt(int this_cpu, enum cpu_idle_type idle) { }
#endif

/*
 * run_rebalance_domains_bt is triggered when needed from the scheduler tick.
 * Also triggered for nohz idle balancing (with nohz_balancing_kick set).
 */
static void run_rebalance_domains_bt(struct softirq_action *h)
{
	int this_cpu = smp_processor_id();
	enum cpu_idle_type idle = idle_cpu(this_cpu)?
						CPU_IDLE : CPU_NOT_IDLE;

	if (likely(!sched_bt_on))
		return;

	rebalance_domains_bt(this_cpu, idle);

	idle = idle_bt_cpu(this_cpu) ? CPU_IDLE : CPU_NOT_IDLE;
	/*
	 * If this cpu has a pending nohz_balance_kick, then do the
	 * balancing on behalf of the other idle cpus whose ticks are
	 * stopped.
	 */
	nohz_idle_balance_bt(this_cpu, idle);
}

#endif /* CONFIG_SMP */

/*
 * scheduler tick hitting a task of our scheduling class:
 */
static void task_tick_bt(struct rq *rq, struct task_struct *curr, int queued)
{
	struct bt_rq *bt_rq;
	struct sched_entity *se = &curr->bt;

	for_each_sched_bt_entity(se) {
		bt_rq = bt_rq_of(se);
		bt_entity_tick(bt_rq, se, queued);
	}

	if (static_branch_unlikely(&sched_numa_balancing))
		task_tick_numa(rq, curr);
}

/*
 * called on fork with the child task as argument from the parent's context
 *  - child not yet on the tasklist
 *  - preemption disabled
 */
static void task_fork_bt(struct task_struct *p)
{
	struct bt_rq *bt_rq;
	struct sched_entity *se = &p->bt, *curr;
	int this_cpu = smp_processor_id();
	struct rq *rq = this_rq();
	unsigned long flags;

	raw_spin_lock_irqsave(&rq->lock, flags);

	update_rq_clock(rq);

	bt_rq = task_bt_rq(current);
	curr = bt_rq->curr;

	/*
	 * Not only the cpu but also the task_group of the parent might have
	 * been changed after parent->se.parent,cfs_rq were copied to
	 * child->se.parent,cfs_rq. So call __set_task_cpu() to make those
	 * of child point to valid ones.
	 */
	rcu_read_lock();
	__set_task_cpu(p, this_cpu);
	rcu_read_unlock();

	update_curr_bt(bt_rq);

	if (curr)
		se->vruntime = curr->vruntime;
	place_bt_entity(bt_rq, se, 1);

	if (sysctl_sched_child_runs_first && curr && bt_entity_before(curr, se)) {
		/*
		 * Upon rescheduling, sched_class::put_prev_task() will place
		 * 'current' within the tree based on its new key value.
		 */
		swap(curr->vruntime, se->vruntime);
		resched_curr(rq);
	}

	se->vruntime -= bt_rq->min_vruntime;

	raw_spin_unlock_irqrestore(&rq->lock, flags);
}

/*
 * Priority of the task has changed. Check to see if we preempt
 * the current task.
 */
static void
prio_changed_bt(struct rq *rq, struct task_struct *p, int oldprio)
{
	if (!p->bt.on_rq)
		return;

	/*
	 * Reschedule if we are currently running on this runqueue and
	 * our priority decreased, or if we are not currently running on
	 * this runqueue and our priority is higher than the current's
	 */
	if (rq->curr == p) {
		if (p->prio > oldprio)
			resched_curr(rq);
	} else
		check_preempt_curr(rq, p, 0);
}

static void switched_from_bt(struct rq *rq, struct task_struct *p)
{
	struct sched_entity *se = &p->bt;
	struct bt_rq *bt_rq = bt_rq_of(se);

	/*
	 * Ensure the task's vruntime is normalized, so that when it's
	 * switched back to the fair class the enqueue_entity(.flags=0) will
	 * do the right thing.
	 *
	 * If it's on_rq, then the dequeue_entity(.flags=0) will already
	 * have normalized the vruntime, if it's !on_rq, then only when
	 * the task is sleeping will it still have non-normalized vruntime.
	 */
	if (!p->on_rq && p->state != TASK_RUNNING) {
		/*
		 * Fix up our vruntime so that the current sleep doesn't
		 * cause 'unlimited' sleep bonus.
		 */
		place_bt_entity(bt_rq, se, 0);
		se->vruntime -= bt_rq->min_vruntime;
	}
#if defined(CONFIG_BT_GROUP_SCHED) && defined(CONFIG_SMP)
	/* Catch up with the cfs_rq and remove our load when we leave */
	__update_bt_load_avg(bt_rq->avg.last_update_time, &se->bt_avg,
		se->on_rq * scale_load_down(se->load.weight), bt_rq->curr == se, NULL);

	sub_positive(&bt_rq->avg.load_avg, se->bt_avg.load_avg);
	sub_positive(&bt_rq->avg.load_sum, se->bt_avg.load_sum);
	sub_positive(&bt_rq->avg.util_avg, se->bt_avg.util_avg);
	sub_positive(&bt_rq->avg.util_sum, se->bt_avg.util_sum);
#endif
}

/*
 * We switched to the sched_fair class.
 */
static void switched_to_bt(struct rq *rq, struct task_struct *p)
{
	BUG_ON(!bt_prio(p->static_prio));

//    attach_task_bt_rq(p);

	if (!p->bt.on_rq)
		return;

	/*
	 * We were most likely switched from sched_rt, so
	 * kick off the schedule if running, otherwise just see
	 * if we can still preempt the current task.
	 */
	if (rq->curr == p)
		resched_curr(rq);
	else
		check_preempt_curr(rq, p, 0);
}

/* Account for a task changing its policy or group.
 *
 * This routine is mostly called to set cfs_rq->curr field when a task
 * migrates between groups/classes.
 */
static void set_curr_task_bt(struct rq *rq)
{
	struct sched_entity *se = &rq->curr->bt;

	for_each_sched_bt_entity(se) {
		struct bt_rq *bt_rq = bt_rq_of(se);

		set_next_bt_entity(bt_rq, se);
		/* ensure bandwidth has been allocated on our new cfs_rq */
		account_bt_rq_runtime(bt_rq, 0);
	}
}

void init_bt_rq(struct bt_rq *bt_rq)
{
	bt_rq->tasks_timeline.rb_root = RB_ROOT;
	bt_rq->tasks_timeline.rb_leftmost = NULL;
	bt_rq->min_vruntime = (u64)(-(1LL << 20));
#ifndef CONFIG_64BIT
	bt_rq->min_vruntime_copy = bt_rq->min_vruntime;
#endif
#ifdef CONFIG_SMP
	atomic_long_set(&bt_rq->removed_load_avg, 0);
	atomic_long_set(&bt_rq->removed_util_avg, 0);
#endif

	bt_rq->bt_time = 0;
	bt_rq->bt_throttled = 0;
	bt_rq->bt_runtime = RUNTIME_INF;
	raw_spin_lock_init(&bt_rq->bt_runtime_lock);
}

#ifdef CONFIG_BT_GROUP_SCHED
static void task_change_group_bt(struct task_struct *p, int on_rq)
{
	struct bt_rq *bt_rq;
	/*
	 * If the task was not on the rq at the time of this cgroup movement
	 * it must have been asleep, sleeping tasks keep their ->vruntime
	 * absolute on their old rq until wakeup (needed for the fair sleeper
	 * bonus in place_entity()).
	 *
	 * If it was on the rq, we've just 'preempted' it, which does convert
	 * ->vruntime to a relative base.
	 *
	 * Make sure both cases convert their relative position when migrating
	 * to another cgroup's rq. This does somewhat interfere with the
	 * fair sleeper stuff for the first placement, but who cares.
	 */
	/*
	 * When !on_rq, vruntime of the task has usually NOT been normalized.
	 * But there are some cases where it has already been normalized:
	 *
	 * - Moving a forked child which is waiting for being woken up by
	 *   wake_up_new_task().
	 * - Moving a task which has been woken up by try_to_wake_up() and
	 *   waiting for actually being woken up by sched_ttwu_pending().
	 *
	 * To prevent boost or penalty in the new cfs_rq caused by delta
	 * min_vruntime between the two cfs_rqs, we skip vruntime adjustment.
	 */
	if (!on_rq && (!p->bt.sum_exec_runtime || p->state == TASK_WAKING))
		on_rq = 1;

	if (!on_rq)
		p->bt.vruntime -= bt_rq_of(&p->bt)->min_vruntime;
	set_task_rq(p, task_cpu(p));
	if (!on_rq) {
		bt_rq = bt_rq_of(&p->bt);
		p->bt.vruntime += bt_rq->min_vruntime;
#ifdef CONFIG_SMP
		/* Virtually synchronize task with its new cfs_rq */
		p->bt.bt_avg.last_update_time = bt_rq->avg.last_update_time;
		bt_rq->avg.load_avg += p->bt.bt_avg.load_avg;
		bt_rq->avg.load_sum += p->bt.bt_avg.load_sum;
		bt_rq->avg.util_avg += p->bt.bt_avg.util_avg;
		bt_rq->avg.util_sum += p->bt.bt_avg.util_sum;
#endif
	}
}

void free_bt_sched_group(struct task_group *tg)
{
	int i;

	for_each_possible_cpu(i) {
		if (tg->bt_rq)
			kfree(tg->bt_rq[i]);
		if (tg->bt) {
			if (likely(tg->bt[i])) {
				remove_bt_entity_load_avg(tg->bt[i]);
				kfree(tg->bt[i]->bt_statistics);
			}
			kfree(tg->bt[i]);
		}
	}

	kfree(tg->bt_rq);
	kfree(tg->bt);
}

int alloc_bt_sched_group(struct task_group *tg, struct task_group *parent)
{
	struct bt_rq *bt_rq;
	struct sched_entity *se;
	struct sched_statistics *stat;
	int i;

	tg->bt_rq = kzalloc(sizeof(bt_rq) * nr_cpu_ids, GFP_KERNEL);
	if (!tg->bt_rq)
		goto err;
	tg->bt = kzalloc(sizeof(se) * nr_cpu_ids, GFP_KERNEL);
	if (!tg->bt)
		goto err;

	tg->bt_shares = NICE_0_LOAD;

	for_each_possible_cpu(i) {
		bt_rq = kzalloc_node(sizeof(struct bt_rq),
				      GFP_KERNEL, cpu_to_node(i));
		if (!bt_rq)
			goto err;

		se = kzalloc_node(sizeof(struct sched_entity),
				  GFP_KERNEL, cpu_to_node(i));
		if (!se)
			goto err_free_rq;

		stat = kzalloc_node(sizeof(struct sched_statistics),
				  GFP_KERNEL, cpu_to_node(i));
		if (!stat)
			goto err_free_se;

		se->bt_statistics = stat;
		init_bt_rq(bt_rq);
		init_tg_bt_entry(tg, bt_rq, se, i, parent->bt[i]);
		init_bt_entity_runnable_average(se);
		post_init_bt_entity_util_avg(se);
	}

	return 1;

err_free_se:
	kfree(se);
err_free_rq:
	kfree(bt_rq);
err:
	return 0;
}

static void sync_throttle_bt(struct task_group *tg, int cpu)
{
	struct bt_rq *pbt_rq;

	if (!bt_bandwidth_enabled())
		return;

	if (!tg->parent)
		return;

	pbt_rq = tg->parent->bt_rq[cpu];

	pbt_rq->throttled_clock_task = rq_clock_task(cpu_rq(cpu));
}


void online_bt_sched_group(struct task_group *tg)
{
	struct sched_entity *se;
	struct rq *rq;
	int i;

	for_each_possible_cpu(i) {
		rq = cpu_rq(i);
		se = tg->bt[i];

		raw_spin_lock_irq(&rq->lock);
		sync_throttle_bt(tg, i);
		raw_spin_unlock_irq(&rq->lock);
	}
}

void unregister_bt_sched_group(struct task_group *tg)
{
	unsigned long flags;
	struct rq *rq;
	int cpu;

	for_each_possible_cpu(cpu) {
		if (tg->bt[cpu])
			remove_bt_entity_load_avg(tg->bt[cpu]);

		/*
		 * Only empty task groups can be destroyed; so we can speculatively
		 * check on_list without danger of it being re-added.
		 */
		if (!tg->bt_rq[cpu]->on_list)
			continue;

		rq = cpu_rq(cpu);

		raw_spin_lock_irqsave(&rq->lock, flags);
		list_del_leaf_bt_rq(tg->bt_rq[cpu]);
		raw_spin_unlock_irqrestore(&rq->lock, flags);
	}
}

void init_tg_bt_entry(struct task_group *tg, struct bt_rq *bt_rq,
			struct sched_entity *se, int cpu,
			struct sched_entity *parent)
{
	struct rq *rq = cpu_rq(cpu);

	bt_rq->tg = tg;
	bt_rq->rq = rq;

	tg->bt_rq[cpu] = bt_rq;
	tg->bt[cpu] = se;

	/* se could be NULL for root_task_group */
	if (!se)
		return;

	if (!parent)
		se->bt_rq = &rq->bt;
	else
		se->bt_rq = parent->bt_my_q;

	se->bt_my_q = bt_rq;
	/* guarantee group entities always have weight */
	update_load_set(&se->load, NICE_0_LOAD);
	se->parent = parent;
}

static DEFINE_MUTEX(bt_shares_mutex);

int sched_group_set_bt_shares(struct task_group *tg, unsigned long shares)
{
	int i;
	unsigned long flags;

	/*
	 * We can't change the weight of the root cgroup.
	 */
	if (!tg->bt[0])
		return -EINVAL;

	shares = clamp(shares, scale_load(MIN_BT_SHARES), scale_load(MAX_BT_SHARES));

	mutex_lock(&bt_shares_mutex);
	if (tg->bt_shares == shares)
		goto done;

	tg->bt_shares = shares;
	for_each_possible_cpu(i) {
		struct rq *rq = cpu_rq(i);
		struct sched_entity *se;

		se = tg->bt[i];
		/* Propagate contribution to hierarchy */
		raw_spin_lock_irqsave(&rq->lock, flags);
		for_each_sched_bt_entity(se)
			update_bt_shares(group_bt_rq(se));
		raw_spin_unlock_irqrestore(&rq->lock, flags);
	}

done:
	mutex_unlock(&bt_shares_mutex);
	return 0;
}
#else /* CONFIG_BT_GROUP_SCHED */

void free_bt_sched_group(struct task_group *tg) { }

int alloc_bt_sched_group(struct task_group *tg, struct task_group *parent)
{
	return 1;
}

void unregister_bt_sched_group(struct task_group *tg) { }

#endif /* CONFIG_BT_GROUP_SCHED */

static unsigned int get_rr_interval_bt(struct rq *rq, struct task_struct *task)
{
	struct sched_entity *se = &task->bt;
	unsigned int rr_interval = 0;

	/*
	 * Time slice is 0 for SCHED_OTHER tasks that are on an otherwise
	 * idle runqueue:
	 */
	if (rq->bt.load.weight)
		rr_interval = NS_TO_JIFFIES(sched_bt_slice(bt_rq_of(se), se));

	return rr_interval;
}

/*
 * All the scheduling class methods:
 */
const struct sched_class bt_sched_class = {
	.next			= &idle_sched_class,
	.enqueue_task		= enqueue_task_bt,
	.dequeue_task		= dequeue_task_bt,
	.yield_task		= yield_task_bt,
	.yield_to_task		= yield_to_task_bt,

	.check_preempt_curr	= check_preempt_wakeup_bt,

	.pick_next_task		= pick_next_task_bt,
	.put_prev_task		= put_prev_task_bt,

#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_bt,
#ifdef CONFIG_BT_GROUP_SCHED
	.migrate_task_rq	= migrate_task_rq_bt,
#endif
	.rq_online		= rq_online_bt,
	.rq_offline		= rq_offline_bt,
#ifdef CONFIG_SMP
	.task_dead		= task_dead_bt,
#endif
	.set_cpus_allowed       = set_cpus_allowed_common,
#endif

	.set_curr_task		= set_curr_task_bt,
	.task_tick		= task_tick_bt,
	.task_fork		= task_fork_bt,

	.prio_changed		= prio_changed_bt,
	.switched_from		= switched_from_bt,
	.switched_to		= switched_to_bt,

	.get_rr_interval	= get_rr_interval_bt,

	.update_curr		= update_curr_cb_bt,
#ifdef CONFIG_BT_GROUP_SCHED
	.task_change_group	= task_change_group_bt,
#endif
};

__init void init_sched_bt_class(void)
{
#ifdef CONFIG_SMP
	open_softirq(SCHED_BT_SOFTIRQ, run_rebalance_domains_bt);
#endif /* SMP */
}

static int sched_bt_global_constraints(void)
{
	unsigned long flags;
	int i;

	if (sysctl_sched_bt_period <= 0)
		return -EINVAL;

	if (offlinegroup_enabled)
		return 0;

	if (sysctl_sched_bt_runtime == 0)
		return -EINVAL;

	raw_spin_lock_irqsave(&def_bt_bandwidth.bt_runtime_lock, flags);
	for_each_possible_cpu(i) {
		struct bt_rq *bt_rq = &cpu_rq(i)->bt;

		raw_spin_lock(&bt_rq->bt_runtime_lock);
		bt_rq->bt_runtime = global_bt_runtime();
		raw_spin_unlock(&bt_rq->bt_runtime_lock);
	}
	raw_spin_unlock_irqrestore(&def_bt_bandwidth.bt_runtime_lock, flags);

	return 0;
}

int sched_bt_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret;
	int old_period, old_runtime;
	static DEFINE_MUTEX(mutex);

	mutex_lock(&mutex);
	old_period = sysctl_sched_bt_period;
	old_runtime = sysctl_sched_bt_runtime;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);

	if (!ret && write) {
		ret = sched_bt_global_constraints();
		if (ret) {
			sysctl_sched_bt_period = old_period;
			sysctl_sched_bt_runtime = old_runtime;
		} else {
			def_bt_bandwidth.bt_runtime = global_bt_runtime();
			def_bt_bandwidth.bt_period =
				ns_to_ktime(global_bt_period());
		}
	}
	mutex_unlock(&mutex);

	return ret;
}

static int offline_proc_show(struct seq_file *m, void *v) {
	unsigned long * n = (unsigned long*)m->private;

	seq_printf(m, "%lu\n", *n);
	return 0;
}

static int offline_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, offline_proc_show, PDE_DATA(inode));
}

#define OFFLINE_NUMBUF 8
static ssize_t offline_proc_write(struct file *file, const char __user *ubuf,
		size_t cnt, loff_t *ppos)
{
	unsigned long * start = (unsigned long *)bt_cpu_control_set;
	unsigned long *to = (unsigned long *)PDE_DATA(file_inode(file));
	char buffer[OFFLINE_NUMBUF];
	int cpu = to - start;
	struct bt_rq *bt_rq;
	unsigned long tmp;
	int cpn;

	cpn = min((int)OFFLINE_NUMBUF, (int)cnt);
	if (copy_from_user(buffer, ubuf, cpn))
		return -EFAULT;

	buffer[cpn - 1] = '\0';
	if (kstrtoul(buffer, 0, &tmp) || tmp > 100)
		return -EINVAL;

	*to = tmp;
	bt_rq = &cpu_rq(cpu)->bt;

	raw_spin_lock(&bt_rq->bt_runtime_lock);
	bt_rq->bt_runtime = (u64)sysctl_sched_bt_period * NSEC_PER_USEC * tmp / 100;
	raw_spin_unlock(&bt_rq->bt_runtime_lock);

	return cnt;
}

static const struct file_operations info_fops = {
	.open           = offline_proc_open,
	.read           = seq_read,
	.write          = offline_proc_write,
	.llseek         = seq_lseek,
	.release        = single_release,
};

void init_offline_cpu_control(void)
{
	int i, nr = num_online_cpus();
	char buffer[20] = "offline";
	struct proc_dir_entry * dir;

	if(!offlinegroup_enabled)
		return;

	dir= proc_mkdir(buffer, NULL);

	if (!dir)
		return;

	bt_cpu_control_set = kmalloc(sizeof(unsigned long)*nr, GFP_KERNEL);

	if(!bt_cpu_control_set)
		return;

	for(i=0; i<nr; i++) {
		sprintf(buffer,"%s%d","cpu",i);
		proc_create_data(buffer, 0400, dir, &info_fops, (void *)((unsigned long *)bt_cpu_control_set + i));
		(*((unsigned long*)bt_cpu_control_set+i)) = 100;
	}
	return;
}
