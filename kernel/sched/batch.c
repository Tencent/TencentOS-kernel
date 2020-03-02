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

static inline struct sched_entity *parent_bt_entity(struct sched_entity *bt)
{
	return NULL;
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

	bt_rq->nr_running++;
}

static void
account_bt_entity_dequeue(struct bt_rq *bt_rq, struct sched_entity *se)
{
	update_load_sub(&bt_rq->load, se->load.weight);

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
/**
 * idle_cpu - is a given cpu idle currently?
 * @cpu: the processor in question.
 */
static int idle_cpu_bt(int cpu)
{
	struct rq *rq = cpu_rq(cpu);

	if (rq->curr != rq->idle)
		return 0;

	if (rq->nr_running)
		return 0;

#ifdef CONFIG_SMP
	if (!llist_empty(&rq->wake_list))
		return 0;
#endif

	return 1;
}

static int select_idle_sibling_bt(struct task_struct *p, int target)
{
	struct sched_domain *sd;
	struct sched_group *sg;
	int i = task_cpu(p);

	if (idle_cpu_bt(target))
		return target;

	/*
	 * If the prevous cpu is cache affine and idle, don't be stupid.
	 */
	if (i != target && cpus_share_cache(i, target) && idle_cpu_bt(i))
		return i;

	/*
	 * Otherwise, iterate the domains and find an elegible idle cpu.
	 */
	sd = rcu_dereference(per_cpu(sd_llc, target));
	for_each_lower_domain(sd) {
		sg = sd->groups;
		do {
			if (!cpumask_intersects(sched_group_cpus(sg),
						tsk_cpus_allowed(p)))
				goto next;

			for_each_cpu(i, sched_group_cpus(sg)) {
				if (i == target || !idle_cpu_bt(i))
					goto next;
			}

			target = cpumask_first_and(sched_group_cpus(sg),
					tsk_cpus_allowed(p));
			goto done;
next:
			sg = sg->next;
		} while (sg != sd->groups);
	}
done:
	return target;
}

static int select_idle_cpu(struct task_struct *p, int target)
{
	struct sched_domain *sd;
	struct sched_group *sg;
	int i = task_cpu(p);

	if (idle_cpu_bt(target))
		return target;

	/*
	 * If the prevous cpu is cache affine and idle, don't be stupid.
	 */
	if (i != target && cpus_share_cache(i, target) && idle_cpu_bt(i))
		return i;

	/*
	 * Otherwise, iterate the domains and find an elegible idle cpu.
	 */
	sd = rcu_dereference(per_cpu(sd_llc, target));
	for_each_lower_domain(sd) {
		sg = sd->groups;
		do {
			if (!cpumask_intersects(sched_group_cpus(sg),
						tsk_cpus_allowed(p)))
				goto next;

			for_each_cpu(i, sched_group_cpus(sg)) {
				if (idle_cpu_bt(i) && cpumask_test_cpu(i, tsk_cpus_allowed(p))) {
					target = i;
					goto done;
				}
			}
next:
			sg = sg->next;
		} while (sg != sd->groups);
	}

done:
	return target;
}


static int
select_task_rq_bt(struct task_struct *p, int prev_cpu, int sd_flag, int wake_flags)
{
	struct sched_domain *tmp, *affine_sd = NULL, *sd = NULL;
	int cpu = smp_processor_id();
	int new_cpu = prev_cpu;
	int want_affine = 0;

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
		new_cpu = select_idle_sibling_bt(p, prev_cpu);
		goto unlock;
	}

	new_cpu = select_idle_cpu(p, prev_cpu);

unlock:
	rcu_read_unlock();

	return new_cpu;
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
	int new_tasks = -1;

	bt_rq = &rq->bt;
again:
	if (!bt_rq->nr_running)
		goto idle;

	put_prev_task(rq, prev);

	se = pick_next_bt_entity(bt_rq);
	set_next_bt_entity(bt_rq, se);

	p = bt_task_of(se);

	return p;

idle:
	new_tasks = idle_balance(rq, rf);

	/*
	 * Because idle_balance() releases (and re-acquires) rq->lock, it is
	 * possible for any higher priority task to appear. In that case we
	 * must re-start the pick_next_entity() loop.
	 */
	if (new_tasks < 0)
		return RETRY_TASK;

	if (new_tasks > 0)
		goto again;

	return NULL;
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

#ifdef CONFIG_SMP
static void rq_online_bt(struct rq *rq)
{
	update_sysctl();
}

static void rq_offline_bt(struct rq *rq)
{
	update_sysctl();
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
#endif

	.set_curr_task		= set_curr_task_bt,
	.task_tick		= task_tick_bt,
	.task_fork		= task_fork_bt,

	.prio_changed		= prio_changed_bt,
	.switched_from		= switched_from_bt,
	.switched_to		= switched_to_bt,

	.get_rr_interval	= get_rr_interval_bt,
};
