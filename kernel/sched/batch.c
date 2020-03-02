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

#include <trace/events/sched.h>
#include "batch.h"
#include "sched.h"
#include "fair.h"

void set_bt_load_weight(struct task_struct *p)
{
	int prio = p->static_prio - MIN_BT_PRIO;
	struct load_weight *load = &p->bt.load;

	load->weight = scale_load(sched_prio_to_weight[prio]);
	load->inv_weight = sched_prio_to_wmult[prio];
}

const struct sched_class bt_sched_class;
unsigned int sysctl_idle_balance_bt_cost = 300000UL;

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

static inline int bt_rq_throttled(struct bt_rq *bt_rq)
{
	return bt_rq->bt_throttled;
}

static inline struct bt_rq *bt_rq_of(struct sched_entity *bt_se)
{
	struct task_struct *p = bt_task_of(bt_se);
	struct rq *rq = task_rq(p);

	return &rq->bt;
}

static inline struct sched_entity *parent_bt_entity(struct sched_entity *bt)
{
	return NULL;
}

static int do_sched_bt_period_timer(struct bt_bandwidth *bt_b, int overrun);

struct bt_bandwidth def_bt_bandwidth;

static enum hrtimer_restart sched_bt_period_timer(struct hrtimer *timer)
{
	struct bt_bandwidth *bt_b =
		container_of(timer, struct bt_bandwidth, bt_period_timer);
	ktime_t now;
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
	if (!bt_bandwidth_enabled() || bt_b->bt_runtime == RUNTIME_INF)
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

static inline void sched_bt_rq_enqueue(struct bt_rq *bt_rq)
{
	struct rq *rq = rq_of_bt_rq(bt_rq);

	if (rq->curr == rq->idle && rq->bt_nr_running)
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

static void disable_bt_runtime(struct rq *rq)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&rq->lock, flags);
	__disable_bt_runtime(rq);
	raw_spin_unlock_irqrestore(&rq->lock, flags);
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

static void enable_bt_runtime(struct rq *rq)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&rq->lock, flags);
	__enable_bt_runtime(rq);
	raw_spin_unlock_irqrestore(&rq->lock, flags);
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

	if (!sched_feat(BT_RUNTIME_SHARE))
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
				bt_rq->bt_throttled = 0;
				enqueue = 1;
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

	if (!throttled && (!bt_bandwidth_enabled() || bt_b->bt_runtime == RUNTIME_INF))
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
		if (likely(bt_b->bt_runtime)) {
			static bool once = false;

			bt_rq->bt_throttled = 1;

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
	if (!bt_bandwidth_enabled() || sched_bt_runtime(bt_rq) == RUNTIME_INF)
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
	u64 now = rq_of_bt_rq(bt_rq)->clock_task;
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
			rq_of_bt_rq(bt_rq)->clock);
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
	schedstat_set(se->bt_statistics->wait_max,
			max(se->bt_statistics->wait_max,
			rq_of_bt_rq(bt_rq)->clock - se->bt_statistics->wait_start));
	schedstat_set(se->bt_statistics->wait_count,
			se->bt_statistics->wait_count + 1);
	schedstat_set(se->bt_statistics->wait_sum,
			se->bt_statistics->wait_sum +
			rq_of_bt_rq(bt_rq)->clock - se->bt_statistics->wait_start);
#ifdef CONFIG_SCHEDSTATS
	if (bt_entity_is_task(se)) {
		trace_sched_stat_wait(bt_task_of(se),
			rq_of_bt_rq(bt_rq)->clock - se->bt_statistics->wait_start);
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

static void enqueue_bt_sleeper(struct bt_rq *bt_rq, struct sched_entity *se)
{
#if defined(CONFIG_SCHEDSTATS) || defined(CONFIG_LATENCYTOP)
	struct task_struct *tsk = NULL;

	if (bt_entity_is_task(se))
		tsk = bt_task_of(se);

	if (se->bt_statistics->sleep_start) {
		u64 delta = rq_of_bt_rq(bt_rq)->clock - se->bt_statistics->sleep_start;

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
		u64 delta = rq_of_bt_rq(bt_rq)->clock - se->bt_statistics->block_start;

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
	/*
	 * Update the normalized vruntime before updating min_vruntime
	 * through callig update_curr().
	 */
	if (!(flags & ENQUEUE_WAKEUP) || (flags & ENQUEUE_MIGRATED))
		se->vruntime += bt_rq->min_vruntime;

	/*
	 * Update run-time bt_statistics of the 'current'.
	 */
	update_curr_bt(bt_rq);
	account_bt_entity_enqueue(bt_rq, se);

	if (flags & ENQUEUE_WAKEUP) {
		place_bt_entity(bt_rq, se, 0);
		enqueue_bt_sleeper(bt_rq, se);
	}

	update_stats_enqueue_bt(bt_rq, se);
	check_bt_spread(bt_rq, se);
	if (se != bt_rq->curr)
		__enqueue_bt_entity(bt_rq, se);
	se->on_rq = 1;
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

	update_stats_dequeue_bt(bt_rq, se);
	if (flags & DEQUEUE_SLEEP) {
#if defined(CONFIG_SCHEDSTATS) || defined(CONFIG_LATENCYTOP)
		if (bt_entity_is_task(se)) {
			struct task_struct *tsk = bt_task_of(se);

			if (tsk->state & TASK_INTERRUPTIBLE)
				se->bt_statistics->sleep_start = rq_of_bt_rq(bt_rq)->clock;
			if (tsk->state & TASK_UNINTERRUPTIBLE)
				se->bt_statistics->block_start = rq_of_bt_rq(bt_rq)->clock;
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

	update_bt_min_vruntime(bt_rq);
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
	}

	update_stats_curr_start_bt(bt_rq, se);
	bt_rq->curr = se;
#ifdef CONFIG_SCHEDSTATS
	/*
	 * Track our maximum slice length, if the CPU's load is at
	 * least twice that of our own weight (i.e. dont track it
	 * when there are only lesser-weight tasks around):
	 */
	if (bt_rq->load.weight >= 2*se->load.weight) {
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

		bt_rq->h_nr_running++;

		flags = ENQUEUE_WAKEUP;
	}

	if (!se) {
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

	if (!se) {
		sub_nr_running(rq, 1);
		rq->bt_nr_running--;
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

static inline unsigned long effective_bt_load(struct task_group *tg, int cpu,
		unsigned long wl, unsigned long wg)
{
	return wl;
}

static int wake_affine_bt(struct sched_domain *sd, struct task_struct *p, int sync)
{
	s64 this_load, load;
	int idx, this_cpu, prev_cpu;
	unsigned long tl_per_task;
	struct task_group *tg;
	unsigned long weight;
	int balanced;

	idx	  = sd->wake_idx;
	this_cpu  = smp_processor_id();
	prev_cpu  = task_cpu(p);
	load	  = source_bt_load(prev_cpu, idx) + source_bt_load(prev_cpu, idx);
	this_load = target_bt_load(this_cpu, idx) + target_bt_load(this_cpu, idx);

	/*
	 * If sync wakeup then subtract the (maximum possible)
	 * effect of the currently running task from the load
	 * of the current CPU:
	 */
	if (sync) {
		tg = task_group(current);
		weight = current->bt.load.weight;

		this_load += effective_bt_load(tg, this_cpu, -weight, -weight);
		load += effective_bt_load(tg, prev_cpu, 0, -weight);
	}

	tg = task_group(p);
	weight = p->bt.load.weight;

	/*
	 * In low-load situations, where prev_cpu is idle and this_cpu is idle
	 * due to the sync cause above having dropped this_load to 0, we'll
	 * always have an imbalance, but there's really nothing you can do
	 * about that, so that's good too.
	 *
	 * Otherwise check if either cpus are near enough in load to allow this
	 * task to be woken on this_cpu.
	 */
	if (this_load > 0) {
		s64 this_eff_load, prev_eff_load;

		this_eff_load = 100;
		this_eff_load *= capacity_of_bt(prev_cpu);
		this_eff_load *= this_load +
			effective_bt_load(tg, this_cpu, weight, weight);

		prev_eff_load = 100 + (sd->imbalance_pct - 100) / 2;
		prev_eff_load *= capacity_of_bt(this_cpu);
		prev_eff_load *= load + effective_bt_load(tg, prev_cpu, 0, weight);

		balanced = this_eff_load <= prev_eff_load;
	} else
		balanced = true;

	/*
	 * If the currently running task will sleep within
	 * a reasonable amount of time then attract this newly
	 * woken task:
	 */
	if (sync && balanced)
		return 1;

	schedstat_inc(p->se.statistics.nr_wakeups_affine_attempts);
	tl_per_task = cpu_avg_bt_load_per_task(this_cpu) +
			cpu_avg_bt_load_per_task(this_cpu);

	if (balanced ||
	    (this_load <= load &&
	     (this_load + target_bt_load(prev_cpu, idx) +
	      target_bt_load(prev_cpu, idx)) <= tl_per_task)) {
		/*
		 * This domain has SD_WAKE_AFFINE and
		 * p is cache cold in this domain, and
		 * there is no bad imbalance.
		 */
		schedstat_inc(sd->ttwu_move_affine);
		schedstat_inc(p->se.statistics.nr_wakeups_affine);

		return 1;
	}
	return 0;
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

	do {
		unsigned long load, avg_load;
		int local_group;
		int i;

		/* Skip over this group if it has no CPUs allowed */
		if (!cpumask_intersects(sched_group_cpus(group),
					tsk_cpus_allowed(p)))
			continue;

		local_group = cpumask_test_cpu(this_cpu,
					       sched_group_cpus(group));

		/* Tally up the load of all CPUs in the group */
		avg_load = 0;

		for_each_cpu(i, sched_group_cpus(group)) {
			/* Bias balancing toward cpus of our domain */
			if (local_group)
				load = source_bt_load(i, load_idx)+source_bt_load(i, load_idx);
			else
				load = target_bt_load(i, load_idx)+target_bt_load(i, load_idx);

			avg_load += load;
		}

		/* Adjust by relative CPU power of the group */
		avg_load = (avg_load * SCHED_POWER_SCALE) / group->sgc->capacity;

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
	int i;
	struct rq *rq;

	/* Traverse only the allowed CPUs */
	for_each_cpu_and(i, sched_group_cpus(group), tsk_cpus_allowed(p)) {
		load = bt_weighted_cpuload(i) + bt_weighted_cpuload(i);
		rq = cpu_rq(i);

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
	int sync = wake_flags & WF_SYNC;

	if (p->nr_cpus_allowed == 1)
		return prev_cpu;

	if (sd_flag & SD_BALANCE_WAKE) {
		if (cpumask_test_cpu(cpu, tsk_cpus_allowed(p)))
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
		if (cpu != prev_cpu && wake_affine_bt(affine_sd, p, sync))
			prev_cpu = cpu;

		new_cpu = select_idle_sibling_bt(p, prev_cpu);
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

void remove_entity_load_avg_bt(struct sched_entity *se)
{
	struct bt_rq *bt_rq = bt_rq_of(se);

	/*
	 * tasks cannot exit without having gone through wake_up_new_task() ->
	 * post_init_entity_util_avg() which will have added things to the
	 * cfs_rq, so we can remove unconditionally.
	 *
	 * Similarly for groups, they will have passed through
	 * post_init_entity_util_avg() before unregister_sched_fair_group()
	 * calls this.
	 */

//	sync_entity_load_avg(se);
//	atomic_long_add(se->avg.load_avg, &bt_rq->removed_load_avg);
//	atomic_long_add(se->avg.util_avg, &bt_rq->removed_util_avg);
}

static void migrate_task_rq_bt(struct task_struct *p)
{
	/*
	 * As blocked tasks retain absolute vruntime the migration needs to
	 * deal with this by subtracting the old and adding the new
	 * min_vruntime -- the latter is done by enqueue_entity() when placing
	 * the task on the new runqueue.
	 */
	if (p->state == TASK_WAKING) {
		struct sched_entity *se = &p->bt;
		struct bt_rq *bt_rq = bt_rq_of(se);
		u64 min_vruntime;

#ifndef CONFIG_64BIT
		u64 min_vruntime_copy;

		do {
			min_vruntime_copy = bt_rq->min_vruntime_copy;
			smp_rmb();
			min_vruntime = bt_rq->min_vruntime;
		} while (min_vruntime != min_vruntime_copy);
#else
		min_vruntime = bt_rq->min_vruntime;
#endif

		se->vruntime -= min_vruntime;
	}

	/*
	 * We are supposed to update the task to "current" time, then its up to date
	 * and ready to go to new CPU/cfs_rq. But we have difficulty in getting
	 * what current time is, so simply throw away the out-of-date time. This
	 * will result in the wakee task is less decayed, but giving the wakee more
	 * load sounds not bad.
	 */
	remove_entity_load_avg_bt(&p->bt);

//	/* Tell new CPU we are migrated */
//	p->bt.avg.last_update_time = 0;

	/* We have migrated, no longer consider this task hot */
	p->bt.exec_start = 0;
}

static void task_dead_bt(struct task_struct *p)
{
	remove_entity_load_avg_bt(&p->bt);
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

	se = pick_next_bt_entity(bt_rq);
	set_next_bt_entity(bt_rq, se);

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
//	 rq->skip_clock_update = 1;

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
	/*
	 * We do not migrate tasks that are:
	 * 1) throttled_lb_pair, or
	 * 2) cannot be migrated to this CPU due to cpus_allowed, or
	 * 3) running (obviously), or
	 * 4) are cache-hot on their current CPU.
	 */
	if (!cpumask_test_cpu(env->dst_cpu, tsk_cpus_allowed(p))) {
		int cpu;

		schedstat_inc(p->se.statistics.nr_failed_migrations_affine);

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
		schedstat_inc(p->se.statistics.nr_failed_migrations_running);
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

static unsigned long task_h_bt_load(struct task_struct *p)
{
	return p->bt.load.weight;
}

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
					 * SCHED_POWER_SCALE;
	scaled_busy_load_per_task /= sds->busiest->sgc->capacity;

	if (sds->max_load - sds->this_load + scaled_busy_load_per_task >=
			(scaled_busy_load_per_task * imbn)) {
		env->imbalance = sds->busiest_load_per_task;
		return;
	}

	/*
	 * OK, we don't have enough imbalance to justify moving tasks,
	 * however we may be able to increase total CPU power used by
	 * moving them.
	 */

	pwr_now += sds->busiest->sgc->capacity *
			min(sds->busiest_load_per_task, sds->max_load);
	pwr_now += sds->this->sgc->capacity *
			min(sds->this_load_per_task, sds->this_load);
	pwr_now /= SCHED_POWER_SCALE;

	/* Amount of load we'd subtract */
	tmp = (sds->busiest_load_per_task * SCHED_POWER_SCALE) /
		sds->busiest->sgc->capacity;
	if (sds->max_load > tmp)
		pwr_move += sds->busiest->sgc->capacity *
			min(sds->busiest_load_per_task, sds->max_load - tmp);

	/* Amount of load we'd add */
	if (sds->max_load * sds->busiest->sgc->capacity <
		sds->busiest_load_per_task * SCHED_POWER_SCALE)
		tmp = (sds->max_load * sds->busiest->sgc->capacity) /
			sds->this->sgc->capacity;
	else
		tmp = (sds->busiest_load_per_task * SCHED_POWER_SCALE) /
			sds->this->sgc->capacity;
	pwr_move += sds->this->sgc->capacity *
			min(sds->this_load_per_task, sds->this_load + tmp);
	pwr_move /= SCHED_POWER_SCALE;

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

	sds->busiest_load_per_task /= sds->busiest_nr_running;
	if (sds->group_imb) {
		sds->busiest_load_per_task =
			min(sds->busiest_load_per_task, sds->avg_load);
	}

	/*
	 * In the presence of smp nice balancing, certain scenarios can have
	 * max load less than avg load(as we skip the groups at or below
	 * its cpu_capacity, while calculating max_load..)
	 */
	if (sds->max_load < sds->avg_load) {
		env->imbalance = 0;
		return fix_small_imbalance_bt(env, sds);
	}

	if (!sds->group_imb) {
		/*
		 * Don't want to pull so many tasks that a group would go idle.
		 */
		load_above_capacity = (sds->busiest_nr_running -
						sds->busiest_group_capacity);

		load_above_capacity *= (SCHED_LOAD_SCALE * SCHED_POWER_SCALE);

		load_above_capacity /= sds->busiest->sgc->capacity;
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
	max_pull = min(sds->max_load - sds->avg_load, load_above_capacity);

	/* How much load to actually move to equalise the imbalance */
	env->imbalance = min(max_pull * sds->busiest->sgc->capacity,
		(sds->avg_load - sds->this_load) * sds->this->sgc->capacity)
			/ SCHED_POWER_SCALE;

	/*
	 * if *imbalance is less than the average load per runnable task
	 * there is no guarantee that any tasks will be moved so we'll have
	 * a think about bumping its value to force at least one task to be
	 * moved
	 */
	if (env->imbalance < sds->busiest_load_per_task)
		return fix_small_imbalance_bt(env, sds);

}

static inline int
fix_small_capacity_bt(struct sched_domain *sd, struct sched_group *group)
{
	/*
	 * Only siblings can have significantly less than SCHED_POWER_SCALE
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
	unsigned long nr_running, max_nr_running, min_nr_running;
	unsigned long load, max_cpu_load, min_cpu_load;
	unsigned int balance_cpu = -1, first_idle_cpu = 0;
	unsigned long avg_load_per_task = 0;
	int i;

	if (local_group)
		balance_cpu = group_balance_cpu(group);

	/* Tally up the load of all CPUs in the group */
	max_cpu_load = 0;
	min_cpu_load = ~0UL;
	max_nr_running = 0;
	min_nr_running = ~0UL;

	for_each_cpu_and(i, sched_group_cpus(group), env->cpus) {
		struct rq *rq = cpu_rq(i);

		nr_running = rq->nr_running;

		/* Bias balancing toward cpus of our domain */
		if (local_group) {
			if (idle_cpu(i) && rq->bt.bt_runtime && !first_idle_cpu &&
					cpumask_test_cpu(i, group_balance_mask(group))) {
				first_idle_cpu = 1;
				balance_cpu = i;
			}

			load = target_bt_load(i, load_idx)+target_bt_load(i, load_idx);
		} else {
			load = source_bt_load(i, load_idx)+source_bt_load(i, load_idx);
			if (load > max_cpu_load)
				max_cpu_load = load;
			if (min_cpu_load > load)
				min_cpu_load = load;

			if (nr_running > max_nr_running)
				max_nr_running = nr_running;
			if (min_nr_running > nr_running)
				min_nr_running = nr_running;
		}

		sgs->group_load += load;
		sgs->sum_nr_running += nr_running;

		if (rq->nr_running > 1 && rq->bt_nr_running)
			*overload = true;

		sgs->sum_weighted_load += bt_weighted_cpuload(i)+bt_weighted_cpuload(i);
		if (idle_cpu(i))
			sgs->idle_cpus++;
	}

	/*
	 * First idle cpu or the first cpu(busiest) in this sched group
	 * is eligible for doing load balancing at this and above
	 * domains. In the newly idle case, we will allow all the cpu's
	 * to do the newly idle load balance.
	 */
	if (local_group) {
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
	sgs->avg_load = (sgs->group_load*SCHED_POWER_SCALE) / group->sgc->capacity;

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

	if ((max_cpu_load - min_cpu_load) >= avg_load_per_task &&
	    (max_nr_running - min_nr_running) > 1)
		sgs->group_imb = 1;

	sgs->group_capacity = DIV_ROUND_CLOSEST(group->sgc->capacity,
						SCHED_POWER_SCALE);
	if (!sgs->group_capacity)
		sgs->group_capacity = fix_small_capacity_bt(env->sd, group);
	sgs->group_weight = group->group_weight;

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
		sds->total_pwr += sg->sgc->capacity;

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
			sds->this = sg;
			sds->this_nr_running = sgs.sum_nr_running;
			sds->this_load_per_task = sgs.sum_weighted_load;
			sds->this_has_capacity = sgs.group_has_capacity;
			sds->this_idle_cpus = sgs.idle_cpus;
		} else if (update_sd_pick_busiest_bt(env, sds, sg, &sgs)) {
			sds->max_load = sgs.avg_load;
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

	power = max(sds->busiest->sgc->capacity_bt, (u32)SCHED_POWER_SCALE);
	env->imbalance = DIV_ROUND_CLOSEST(
		(sds->max_load + sds->max_bt_load) * power >> 1, SCHED_POWER_SCALE);

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

	sds.avg_load = (SCHED_POWER_SCALE * sds.total_load) / sds.total_pwr;

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
	if (sds.this_load >= sds.max_load)
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
	int i;

	for_each_cpu(i, sched_group_cpus(group)) {
		unsigned long power = capacity_of_bt(i);
		unsigned long capacity = DIV_ROUND_CLOSEST(power,
							   SCHED_POWER_SCALE);
		unsigned long wl;

		if (!capacity)
			capacity = fix_small_capacity_bt(env->sd, group);

		if (!cpumask_test_cpu(i, env->cpus))
			continue;

		rq = cpu_rq(i);
		wl = bt_weighted_cpuload(i) + bt_weighted_cpuload(i);

		/*
		 * When comparing with imbalance, use bt_weighted_cpuload()
		 * which is not scaled with the cpu power.
		 */
		if (capacity && (rq->nr_running == 1 || !rq->bt_nr_running) &&
		    wl > env->imbalance)
			continue;

		/*
		 * For the load comparisons with the other cpu's, consider
		 * the bt_weighted_cpuload() scaled with the cpu power, so that
		 * the load can be moved away from the cpu that is potentially
		 * running at a lower capacity.
		 */
		wl = (wl * SCHED_POWER_SCALE) / power;

		if (wl > max_load) {
			max_load = wl;
			busiest = rq;
		}
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
	struct cpumask *cpus = get_cpu_var(bt_load_balance_mask);

	struct lb_env env = {
		.sd		= sd,
		.dst_cpu	= this_cpu,
		.dst_rq		= this_rq,
		.dst_grpmask    = sched_group_cpus(sd->groups),
		.idle		= idle,
		.loop_break	= sched_nr_migrate_break,
		.cpus		= cpus,
	};

	/*
	 * For NEWLY_IDLE load_balancing, we don't need to consider
	 * other cpus in our group
	 */
	if (idle == CPU_NEWLY_IDLE)
		env.dst_grpmask = NULL;

	cpumask_copy(cpus, cpu_active_mask);

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
	if (busiest->nr_running > 1) {
		/*
		 * Attempt to move tasks. If find_busiest_group has found
		 * an imbalance but busiest->nr_running <= 1, the group is
		 * still unbalanced. ld_moved simply stays zero, so it is
		 * correctly treated as an imbalance.
		 */
		env.flags |= LBF_ALL_PINNED;
		env.src_cpu   = busiest->cpu;
		env.src_rq    = busiest;
		env.loop_max  = min(sysctl_sched_nr_migrate, busiest->nr_running);

		//update_h_load(env.src_cpu);
more_balance:
		local_irq_save(flags);
		double_rq_lock(env.dst_rq, busiest);

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
			if (!cpumask_test_cpu(this_cpu,
					tsk_cpus_allowed(busiest->curr))) {
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
	return ld_moved;
}

/*
 * idle_balance_bt is called by schedule() if this_cpu is about to become
 * idle. Attempts to pull tasks from other CPUs.
 */
int idle_balance_bt(int this_cpu, struct rq *this_rq)
{
	struct sched_domain *sd;
	int pulled_task = 0;
	unsigned long next_balance = jiffies + HZ;

	if (this_rq->bt.bt_throttled || !this_rq->bt.bt_runtime)
		return 0;

	this_rq->idle_stamp = this_rq->clock;

	if (this_rq->avg_idle < sysctl_idle_balance_bt_cost ||
			!this_rq->rd->overload_bt)
		return 0;

	/*
	 * Drop the rq->lock, but keep IRQ/preempt disabled.
	 */
	raw_spin_unlock(&this_rq->lock);

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
	bt_rq->bt_time = 0;
	bt_rq->bt_throttled = 0;
	bt_rq->bt_runtime = 0;
	raw_spin_lock_init(&bt_rq->bt_runtime_lock);
}

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
	.rq_online		= rq_online_bt,
	.rq_offline		= rq_offline_bt,
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

	/*
	 * There's always some BT tasks in the root group
	 * -- migration, kstopmachine etc..
	 */
	if (sysctl_sched_bt_runtime == 0)
		return -EBUSY;

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
