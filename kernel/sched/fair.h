/*
 *   Copyright (C) 2019 Tencent Ltd. All rights reserved.
 *
 *   File Name ：fair.h
 *   Author    ：
 *   Date      ：2019-12-26
 *   Descriptor：
 */

#ifndef _FAIR_H
#define _FAIR_H

extern unsigned int sched_nr_latency;

unsigned long calc_delta_mine(unsigned long delta_exec,
		unsigned long weight, struct load_weight *lw);

extern u64
__calc_delta(u64 delta_exec, unsigned long weight, struct load_weight *lw);
extern int idle_balance(struct rq *this_rq, struct rq_flags *rf);
static inline u64 max_vruntime(u64 max_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - max_vruntime);

	if (delta > 0)
		max_vruntime = vruntime;

	return max_vruntime;
}

static inline u64 min_vruntime(u64 min_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - min_vruntime);

	if (delta < 0)
		min_vruntime = vruntime;

	return min_vruntime;
}

u64 __sched_period(unsigned long nr_running);

void task_tick_numa(struct rq *rq, struct task_struct *curr);

void update_sysctl(void);

#endif
