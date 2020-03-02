/*
 *   Copyright (C) 2019 Tencent Ltd. All rights reserved.
 *
 *   File Name : bt_debug.c
 *   Author    :
 *   Date      : 2019-12-26
 *   Descriptor:
 */

#include <linux/proc_fs.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/seq_file.h>
#include <linux/kallsyms.h>
#include <linux/utsname.h>
#include <linux/mempolicy.h>
#include <linux/debugfs.h>

#include "sched.h"
#include "bt_debug.h"

void print_bt_rq(struct seq_file *m, int cpu, struct bt_rq *bt_rq)
{
	s64 MIN_vruntime = -1, min_vruntime, max_vruntime = -1,
		spread, rq0_min_vruntime, spread0;
	struct rq *rq = cpu_rq(cpu);
	struct sched_entity *last;
	unsigned long flags;

	SEQ_printf(m, "\nbt_rq[%d]:\n", cpu);

	SEQ_printf(m, "  .%-30s: %lld.%06ld\n", "exec_clock",
			SPLIT_NS(bt_rq->exec_clock));

	raw_spin_lock_irqsave(&rq->lock, flags);
	if (bt_rq->rb_leftmost)
		MIN_vruntime = (__pick_first_bt_entity(bt_rq))->vruntime;
	last = __pick_last_bt_entity(bt_rq);
	if (last)
		max_vruntime = last->vruntime;
	min_vruntime = bt_rq->min_vruntime;
	rq0_min_vruntime = cpu_rq(0)->bt.min_vruntime;
	raw_spin_unlock_irqrestore(&rq->lock, flags);
	SEQ_printf(m, "  .%-30s: %lld.%06ld\n", "MIN_vruntime",
			SPLIT_NS(MIN_vruntime));
	SEQ_printf(m, "  .%-30s: %lld.%06ld\n", "min_vruntime",
			SPLIT_NS(min_vruntime));
	SEQ_printf(m, "  .%-30s: %lld.%06ld\n", "max_vruntime",
			SPLIT_NS(max_vruntime));
	spread = max_vruntime - MIN_vruntime;
	SEQ_printf(m, "  .%-30s: %lld.%06ld\n", "spread",
			SPLIT_NS(spread));
	spread0 = min_vruntime - rq0_min_vruntime;
	SEQ_printf(m, "  .%-30s: %lld.%06ld\n", "spread0",
			SPLIT_NS(spread0));
	SEQ_printf(m, "  .%-30s: %d\n", "nr_spread_over",
			bt_rq->nr_spread_over);
	SEQ_printf(m, "  .%-30s: %d\n", "nr_running", bt_rq->nr_running);
	SEQ_printf(m, "  .%-30s: %ld\n", "load", bt_rq->load.weight);
}

#ifdef CONFIG_SCHED_DEBUG
void print_bt_stats(struct seq_file *m, int cpu)
{
	struct bt_rq *bt_rq;

	rcu_read_lock();
	bt_rq = &cpu_rq(cpu)->bt;
	print_bt_rq(m, cpu, bt_rq);
	rcu_read_unlock();
}
#endif
