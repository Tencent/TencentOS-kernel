#ifndef _LINUX_SLI_H
#define _LINUX_SLI_H
#include <linux/cgroup.h>

enum sli_memlat_stat_item {
	MEM_LAT_GLOBAL_DIRECT_RECLAIM,	/* global direct reclaim latency */
	MEM_LAT_MEMCG_DIRECT_RECLAIM,	/* memcg direct reclaim latency */
	MEM_LAT_DIRECT_COMPACT,		/* direct compact latency */
	MEM_LAT_GLOBAL_DIRECT_SWAPOUT,	/* global direct swapout latency */
	MEM_LAT_MEMCG_DIRECT_SWAPOUT,	/* memcg direct swapout latency */
	MEM_LAT_DIRECT_SWAPIN,		/* direct swapin latency */
	MEM_LAT_PAGE_ALLOC,		/* latency of page alloc */
	MEM_LAT_STAT_NR,
};

/* Memory latency histogram distribution, in milliseconds */
enum sli_lat_count {
	LAT_0_1,
	LAT_1_4,
	LAT_4_8,
	LAT_8_16,
	LAT_16_32,
	LAT_32_64,
	LAT_64_128,
	LAT_128_INF,
	LAT_COUNT_NR,
};

enum sli_schedlat_stat_item {
	SCHEDLAT_WAIT,
	SCHEDLAT_BLOCK,
	SCHEDLAT_IOBLOCK,
	SCHEDLAT_SLEEP,
	SCHEDLAT_LONGSYS,
	SCHEDLAT_RUNDELAY,
	SCHEDLAT_IRQTIME,
	SCHEDLAT_STAT_NR
};

struct sli_memlat_stat {
	unsigned long latency_max[MEM_LAT_STAT_NR];
	unsigned long item[MEM_LAT_STAT_NR][LAT_COUNT_NR];
};

struct sli_schedlat_stat {
	unsigned long latency_max[SCHEDLAT_STAT_NR];
	unsigned long item[SCHEDLAT_STAT_NR][LAT_COUNT_NR];
};

enum sli_event_type {
	SLI_SCHED_EVENT,
	SLI_MEM_EVENT,
	SLI_LONGTERM_EVENT,
	SLI_EVENT_NR
};

/*
 * Longterm event is the contiguous event statistic. Sli will
 * collect all the data(such as irqtime in the interrupt) during
 * the samping period(rather than based on the number of times which
 * over the threshold). It will help us to find the interference that
 * is small but contiguous(it may still affect the performance).
 */
enum sli_longterm_event {
	SLI_LONGTERM_RUNDELAY,
	SLI_LONGTERM_IRQTIME,
	SLI_LONGTERM_NR
};

struct sli_event {
	struct list_head event_node;
	struct rcu_head rcu;

	int event_type;
	int event_id;
};

struct sli_event_monitor {
	struct list_head event_head;
	struct work_struct sli_event_work;
	struct cgroup *cgrp;

	/* The minimum value is 1 tick */
	int period;
	int mbuf_enable;
	int overrun;
	unsigned long long last_update;

	unsigned long long schedlat_threshold[SCHEDLAT_STAT_NR];
	unsigned long long schedlat_count[SCHEDLAT_STAT_NR];
	atomic_long_t schedlat_statistics[SCHEDLAT_STAT_NR];

	unsigned long long memlat_threshold[MEM_LAT_STAT_NR];
	unsigned long long memlat_count[MEM_LAT_STAT_NR];
	atomic_long_t memlat_statistics[MEM_LAT_STAT_NR];

	unsigned long long longterm_threshold[SLI_LONGTERM_NR];
	atomic_long_t longterm_statistics[SLI_LONGTERM_NR];
};

int  sli_cgroup_alloc(struct cgroup *cgroup);
void sli_cgroup_free(struct cgroup *cgroup);
void sli_memlat_stat_start(u64 *start);
void sli_memlat_stat_end(enum sli_memlat_stat_item sidx, u64 start);
int  sli_memlat_stat_show(struct seq_file *m, struct cgroup *cgrp);
int  sli_memlat_max_show(struct seq_file *m, struct cgroup *cgrp);
void sli_schedlat_stat(struct task_struct *task,enum sli_schedlat_stat_item sidx, u64 delta);
void sli_schedlat_rundelay(struct task_struct *task, struct task_struct *prev, u64 delta);
int  sli_schedlat_stat_show(struct seq_file *m, struct cgroup *cgrp);
int  sli_schedlat_max_show(struct seq_file *m, struct cgroup *cgrp);
ssize_t cgroup_sli_control_write(struct kernfs_open_file *of, char *buf,
				 size_t nbytes, loff_t off);
int cgroup_sli_control_show(struct seq_file *sf, void *v);
void sli_update_tick(struct task_struct *tsk);
#endif /*_LINUX_SLI_H*/
