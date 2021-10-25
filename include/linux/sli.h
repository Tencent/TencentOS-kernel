#ifndef _LINUX_SLI_H
#define _LINUX_SLI_H

enum sli_memlat_stat_item {
	MEM_LAT_GLOBAL_DIRECT_RECLAIM,	/* global direct reclaim latency */
	MEM_LAT_MEMCG_DIRECT_RECLAIM,	/* memcg direct reclaim latency */
	MEM_LAT_DIRECT_COMPACT,		/* direct compact latency */
	MEM_LAT_GLOBAL_DIRECT_SWAPOUT,	/* global direct swapout latency */
	MEM_LAT_MEMCG_DIRECT_SWAPOUT,	/* memcg direct swapout latency */
	MEM_LAT_DIRECT_SWAPIN,		/* direct swapin latency */
	MEM_LAT_STAT_NR,
};

/* Memory latency histogram distribution, in milliseconds */
enum sli_memlat_count {
	MEM_LAT_0_1,
	MEM_LAT_1_5,
	MEM_LAT_5_10,
	MEM_LAT_10_100,
	MEM_LAT_100_500,
	MEM_LAT_500_1000,
	MEM_LAT_1000_INF,
	MEM_LAT_COUNT_NR,
};

enum sli_schedlat_stat_item {
	SCHEDLAT_WAIT,
	SCHEDLAT_BLOCK,
	SCHEDLAT_IOBLOCK,
	SCHEDLAT_SLEEP,
	SCHEDLAT_RUNDELAY,
	SCHEDLAT_LONGSYS,
	SCHEDLAT_STAT_NR
};

enum sli_schedlat_count {
	SCHEDLAT_0_1,
	SCHEDLAT_1_5,
	SCHEDLAT_5_10,
	SCHEDLAT_10_20,
	SCHEDLAT_20_30,
	SCHEDLAT_30_40,
	SCHEDLAT_40_50,
	SCHEDLAT_50_60,
	SCHEDLAT_60_70,
	SCHEDLAT_70_80,
	SCHEDLAT_80_90,
	SCHEDLAT_90_100,
	SCHEDLAT_100_500,
	SCHEDLAT_500_1000,
	SCHEDLAT_1000_INF,
	SCHEDLAT_COUNT_NR,
};

struct sli_memlat_stat {
	unsigned long item[MEM_LAT_STAT_NR][MEM_LAT_COUNT_NR];
};

struct sli_schedlat_stat {
	unsigned long item[SCHEDLAT_STAT_NR][SCHEDLAT_COUNT_NR];
};

int  sli_cgroup_alloc(struct cgroup *cgroup);
void sli_cgroup_free(struct cgroup *cgroup);
void sli_memlat_stat_start(u64 *start);
void sli_memlat_stat_end(enum sli_memlat_stat_item sidx, u64 start);
int  sli_memlat_stat_show(struct seq_file *m, struct cgroup *cgrp);
void sli_schedlat_stat(struct task_struct *task,enum sli_schedlat_stat_item sidx, u64 delta);
int  sli_schedlat_stat_show(struct seq_file *m, struct cgroup *cgrp);
#ifdef CONFIG_SCHED_INFO
void sli_schedlat_syscall_enter(struct task_struct *task);
void sli_schedlat_syscall_exit(struct task_struct *task,unsigned long n);
void sli_schedlat_syscall_schedout(struct task_struct *task);
void sli_schedlat_syscall_schedin(struct task_struct *task);
#else
static void sli_schedlat_syscall_enter(struct task_struct *task){}
static void sli_schedlat_syscall_exit(struct task_struct *task,unsigned long n){}
static void sli_schedlat_syscall_schedout(struct task_struct *task){}
static void sli_schedlat_syscall_schedin(struct task_struct *task){}
#endif
#endif /*_LINUX_SLI_H*/
