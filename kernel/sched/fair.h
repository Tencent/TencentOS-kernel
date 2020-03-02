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
extern const unsigned int sched_nr_migrate_break;
extern unsigned long __read_mostly max_load_balance_interval;

#define MIN_U(x, y) ((x) < (y) ? (x) :(y))

enum fbq_type { regular, remote, all };

#define LBF_ALL_PINNED	0x01
#define LBF_NEED_BREAK	0x02
#define LBF_DST_PINNED  0x04
#define LBF_SOME_PINNED	0x08
#define LBF_BT_LB		0x10

struct lb_env {
	struct sched_domain	*sd;

	struct rq		*src_rq;
	int			src_cpu;

	int			dst_cpu;
	struct rq		*dst_rq;

	struct cpumask		*dst_grpmask;
	int			new_dst_cpu;
	enum cpu_idle_type	idle;
	long			imbalance;
	/* The set of CPUs under consideration for load-balancing */
	struct cpumask		*cpus;

	unsigned int		flags;

	unsigned int		loop;
	unsigned int		loop_break;
	unsigned int		loop_max;

	enum fbq_type		fbq_type;
	struct list_head	tasks;
};

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

extern unsigned long weighted_cpuload(struct rq *rq);
extern unsigned long source_load(int cpu, int type);
extern unsigned long target_load(int cpu, int type);

#ifdef CONFIG_NO_HZ_COMMON
/*
 * idle load balancing details
 * - When one of the busy CPUs notice that there may be an idle rebalancing
 *   needed, they will kick the idle load balancer, which then does idle
 *   load balancing for all the idle CPUs.
 */
struct _nohz{
	cpumask_var_t idle_cpus_mask;
	atomic_t nr_cpus;
	unsigned long next_balance;     /* in jiffy units */
} ____cacheline_aligned;

extern struct _nohz nohz;
#endif

void update_sysctl(void);

#endif
