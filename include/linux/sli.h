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
	SCHEDLAT_RUNDELAY,
	SCHEDLAT_LONGSYS,
	SCHEDLAT_IRQTIME,
	SCHEDLAT_STAT_NR
};

struct sli_memlat_stat {
	unsigned long latency_max[SCHEDLAT_STAT_NR];
	unsigned long item[MEM_LAT_STAT_NR][LAT_COUNT_NR];
};

struct sli_schedlat_stat {
	unsigned long latency_max[SCHEDLAT_STAT_NR];
	unsigned long item[SCHEDLAT_STAT_NR][LAT_COUNT_NR];
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
#ifdef CONFIG_SCHED_INFO
void sli_check_longsys(struct task_struct *tsk);
#else
static void sli_check_longsys(struct task_struct *tsk){};
#endif
#endif /*_LINUX_SLI_H*/
