#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/cgroup.h>
#include <linux/module.h>
#include <linux/psi.h>
#include <linux/memcontrol.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>
#include <asm/irq_regs.h>
#include "../sched/sched.h"
#include <linux/sli.h>

#define LINE_SIZE 128
#define MAX_STACK_TRACE_DEPTH	64

struct member_offset {
	char member[LINE_SIZE];
	int  offset;
};

struct memlat_thr {
	u64 globa_direct_reclaim_thr;
	u64 memcg_direct_reclaim_thr;
	u64 direct_compact_thr;
	u64 globa_direct_swapout_thr;
	u64 memcg_direct_swapout_thr;
	u64 direct_swapin_thr;
};

struct schedlat_thr {
	u64 schedlat_wait_thr;
	u64 schedlat_block_thr;
	u64 schedlat_ioblock_thr;
	u64 schedlat_sleep_thr;
	u64 schedlat_rundelay_thr;
	u64 schedlat_longsys_thr;
};

static DEFINE_STATIC_KEY_TRUE(sli_no_enabled);
static DEFINE_RAW_SPINLOCK(lat_print_lock);
static struct memlat_thr memlat_threshold;
static struct schedlat_thr schedlat_threshold;

static const struct member_offset memlat_member_offset[] = {
	{ "globa_direct_reclaim_threshold=", offsetof(struct memlat_thr, globa_direct_reclaim_thr)},
	{ "memcg_direct_reclaim_threshold=", offsetof(struct memlat_thr, memcg_direct_reclaim_thr)},
	{ "direct_compact_threshold=", offsetof(struct memlat_thr, direct_compact_thr)},
	{ "globa_direct_swapout_threshold=", offsetof(struct memlat_thr, globa_direct_swapout_thr)},
	{ "memcg_direct_swapout_threshold=", offsetof(struct memlat_thr, memcg_direct_swapout_thr)},
	{ "direct_swapin_threshold=", offsetof(struct memlat_thr, direct_swapin_thr)},
	{ },
};

static const struct member_offset schedlat_member_offset[] = {
	{ "schedlat_wait_thr=",offsetof(struct schedlat_thr,schedlat_wait_thr) },
	{ "schedlat_block_thr=",offsetof(struct schedlat_thr,schedlat_block_thr) },
	{ "schedlat_ioblock_thr=",offsetof(struct schedlat_thr,schedlat_ioblock_thr) },
	{ "schedlat_sleep_thr=",offsetof(struct schedlat_thr,schedlat_sleep_thr) },
	{ "schedlat_rundelay_thr=",offsetof(struct schedlat_thr,schedlat_rundelay_thr) },
	{ "schedlat_longsys_thr=",offsetof(struct schedlat_thr,schedlat_longsys_thr) },
	{ },
};

static void store_task_stack(struct cgroup *cgrp,struct task_struct *task,u64 duration)
{
	unsigned long *entries;
	unsigned nr_entries = 0;
	unsigned long flags;
	int i;

	entries = kmalloc_array(MAX_STACK_TRACE_DEPTH, sizeof(*entries),
				GFP_ATOMIC);
	if (!entries)
		return;

	nr_entries = stack_trace_save_tsk(task, entries,
						  MAX_STACK_TRACE_DEPTH, 0);

	raw_spin_lock_irqsave(&lat_print_lock, flags);

	mbuf_print(cgrp,"comm:%s,pid:%d,duration=%lld\n",
			   task->comm,task->pid,duration);

	for (i = 0; i < nr_entries; i++)
		mbuf_print(cgrp,"[<0>] %pB\n", (void *)entries[i]);

	raw_spin_unlock_irqrestore(&lat_print_lock, flags);

	kfree(entries);
	return;
}

static u64 get_memlat_threshold(enum sli_memlat_stat_item sidx)
{
	long threshold = -1;

	switch (sidx) {
	case MEM_LAT_GLOBAL_DIRECT_RECLAIM:
		threshold = memlat_threshold.globa_direct_reclaim_thr;
		break;
	case MEM_LAT_MEMCG_DIRECT_RECLAIM:
		threshold = memlat_threshold.memcg_direct_reclaim_thr;
		break;
	case MEM_LAT_DIRECT_COMPACT:
		threshold = memlat_threshold.direct_compact_thr;
		break;
	case MEM_LAT_GLOBAL_DIRECT_SWAPOUT:
		threshold = memlat_threshold.globa_direct_swapout_thr;
		break;
	case MEM_LAT_MEMCG_DIRECT_SWAPOUT:
		threshold = memlat_threshold.memcg_direct_swapout_thr;
		break;
	case MEM_LAT_DIRECT_SWAPIN:
		threshold = memlat_threshold.direct_swapin_thr;
		break;
	default:
		break;
	}

	if (threshold != -1)
		threshold = threshold << 20;//ns to ms,2^20=1048576(about 1000000)

	return threshold;
}

static char * get_memlat_name(enum sli_memlat_stat_item sidx)
{
	char *name = NULL;

	switch (sidx) {
	case MEM_LAT_GLOBAL_DIRECT_RECLAIM:
		name = "globa_direct_reclaim";
		break;
	case MEM_LAT_MEMCG_DIRECT_RECLAIM:
		name =  "memcg_direct_reclaim";
		break;
	case MEM_LAT_DIRECT_COMPACT:
		name = "direct_compact";
		break;
	case MEM_LAT_GLOBAL_DIRECT_SWAPOUT:
		name = "globa_direct_swapout";
		break;
	case MEM_LAT_MEMCG_DIRECT_SWAPOUT:
		name = "memcg_direct_swapout";
		break;
	case MEM_LAT_DIRECT_SWAPIN:
		name = "direct_swapin";
		break;
	default:
		break;
	}

	return name;
}

static enum sli_memlat_count get_memlat_count_idx(u64 duration)
{
	enum sli_memlat_count idx;

	duration = duration >> 20;
	if (duration < 1)
		idx = MEM_LAT_0_1;
	else if (duration < 5)
		idx = MEM_LAT_1_5;
	else if (duration < 10)
		idx = MEM_LAT_5_10;
	else if (duration < 100)
		idx = MEM_LAT_10_100;
	else if (duration < 500)
		idx = MEM_LAT_100_500;
	else if (duration < 1000)
		idx = MEM_LAT_500_1000;
	else
		idx = MEM_LAT_1000_INF;

	return idx;
}

static u64 get_schedlat_threshold(enum sli_memlat_stat_item sidx)
{
	long threshold = -1;

	switch (sidx) {
	case SCHEDLAT_WAIT:
		threshold = schedlat_threshold.schedlat_wait_thr;
		break;
	case SCHEDLAT_BLOCK:
		threshold = schedlat_threshold.schedlat_block_thr;
		break;
	case SCHEDLAT_IOBLOCK:
		threshold = schedlat_threshold.schedlat_ioblock_thr;
		break;
	case SCHEDLAT_SLEEP:
		threshold = schedlat_threshold.schedlat_sleep_thr;
		break;
	case SCHEDLAT_RUNDELAY:
		threshold = schedlat_threshold.schedlat_rundelay_thr;
		break;
	case SCHEDLAT_LONGSYS:
		threshold = schedlat_threshold.schedlat_longsys_thr;
		break;
	default:
		break;
	}

	if (threshold != -1)
		threshold = threshold << 20;//ns to ms,2^20=1048576(about 1000000)

	return threshold;
}

static char * get_schedlat_name(enum sli_memlat_stat_item sidx)
{
	char *name = NULL;

	switch (sidx) {
	case SCHEDLAT_WAIT:
		name = "schedlat_wait";
		break;
	case SCHEDLAT_BLOCK:
		name =  "schedlat_block";
		break;
	case SCHEDLAT_IOBLOCK:
		name = "schedlat_ioblock";
		break;
	case SCHEDLAT_SLEEP:
		name = "schedlat_sleep";
		break;
	case SCHEDLAT_RUNDELAY:
		name = "schedlat_rundelay";
		break;
	case SCHEDLAT_LONGSYS:
		name = "schedlat_longsys";
		break;
	default:
		break;
	}

	return name;
}

static enum sli_memlat_count get_schedlat_count_idx(u64 duration)
{
	enum sli_memlat_count idx;

	duration = duration >> 20;
	if (duration < 1)
		idx = SCHEDLAT_0_1;
	else if (duration < 5)
		idx = SCHEDLAT_1_5;
	else if (duration < 10)
		idx = SCHEDLAT_5_10;
	else if (duration < 20)
		idx = SCHEDLAT_10_20;
	else if (duration < 30)
		idx = SCHEDLAT_20_30;
	else if (duration < 40)
		idx = SCHEDLAT_30_40;
	else if (duration < 50)
		idx = SCHEDLAT_40_50;
	else if (duration < 60)
		idx = SCHEDLAT_50_60;
	else if (duration < 70)
		idx = SCHEDLAT_60_70;
	else if (duration < 80)
		idx = SCHEDLAT_70_80;
	else if (duration < 90)
		idx = SCHEDLAT_80_90;
	else if (duration < 100)
		idx = SCHEDLAT_90_100;
	else if (duration < 500)
		idx = MEM_LAT_100_500;
	else if (duration < 1000)
		idx = MEM_LAT_500_1000;
	else
		idx = MEM_LAT_1000_INF;

	return idx;
}

static u64 sli_memlat_stat_gather(struct cgroup *cgrp,
				 enum sli_memlat_stat_item sidx,
				 enum sli_memlat_count cidx)
{
	u64 sum = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		sum += per_cpu_ptr(cgrp->sli_memlat_stat_percpu, cpu)->item[sidx][cidx];

	return sum;
}

int sli_memlat_stat_show(struct seq_file *m, struct cgroup *cgrp)
{
	enum sli_memlat_stat_item sidx;

	if (static_branch_likely(&sli_no_enabled)) {
		seq_printf(m,"sli is not enabled,please echo 1 > /proc/sli/sli_enabled!\n");
		return 0;
	}

	if (!cgrp->sli_memlat_stat_percpu)
		return 0;

	for (sidx = MEM_LAT_GLOBAL_DIRECT_RECLAIM;sidx < MEM_LAT_STAT_NR;sidx++) {
		seq_printf(m,"%s:\n",get_memlat_name(sidx));
		seq_printf(m, "\t0-1ms: \t\t%llu\n",
				   sli_memlat_stat_gather(cgrp, sidx, MEM_LAT_0_1));
		seq_printf(m, "\t1-5ms: \t\t%llu\n",
				   sli_memlat_stat_gather(cgrp, sidx, MEM_LAT_1_5));
		seq_printf(m, "\t5-10ms: \t%llu\n",
				   sli_memlat_stat_gather(cgrp, sidx, MEM_LAT_5_10));
		seq_printf(m, "\t10-100ms: \t%llu\n",
				   sli_memlat_stat_gather(cgrp, sidx, MEM_LAT_10_100));
		seq_printf(m, "\t100-500ms: \t%llu\n",
				   sli_memlat_stat_gather(cgrp, sidx, MEM_LAT_100_500));
		seq_printf(m, "\t500-1000ms: \t%llu\n",
				   sli_memlat_stat_gather(cgrp, sidx, MEM_LAT_500_1000));
		seq_printf(m, "\t>=1000ms: \t%llu\n",
				   sli_memlat_stat_gather(cgrp, sidx, MEM_LAT_1000_INF));
	}

	return 0;
}

void sli_memlat_stat_start(u64 *start)
{
	if (static_branch_likely(&sli_no_enabled))
		*start = 0;
	else
		*start = local_clock();
}

void sli_memlat_stat_end(enum sli_memlat_stat_item sidx, u64 start)
{
	enum sli_memlat_count cidx;
	u64 duration,threshold;
	struct mem_cgroup *memcg;
	struct cgroup *cgrp;

	if (static_branch_likely(&sli_no_enabled) || start == 0)
		return;

	duration = local_clock() - start;
	threshold = get_memlat_threshold(sidx);
	cidx = get_memlat_count_idx(duration);

	rcu_read_lock();
	memcg = mem_cgroup_from_task(current);
	if (!memcg || memcg == root_mem_cgroup ) {
		rcu_read_unlock();
		return;
	}

	cgrp = memcg->css.cgroup;
	if (cgrp && cgrp->sli_memlat_stat_percpu) {
		this_cpu_inc(cgrp->sli_memlat_stat_percpu->item[sidx][cidx]);
		if (duration >= threshold)
			store_task_stack(cgrp,current,duration);
	}
	rcu_read_unlock();
}

static void sli_memlat_thr_set(u64 value)
{
	memlat_threshold.globa_direct_reclaim_thr = value;
	memlat_threshold.memcg_direct_reclaim_thr = value;
	memlat_threshold.direct_compact_thr       = value;
	memlat_threshold.globa_direct_swapout_thr = value;
	memlat_threshold.memcg_direct_swapout_thr = value;
	memlat_threshold.direct_swapin_thr        = value;
}

static int sli_memlat_thr_show(struct seq_file *m, void *v)
{
	seq_printf(m,"globa_direct_reclaim_threshold = %llu ms\n"
			   "memcg_direct_reclaim_threshold = %llu ms\n"
			   "direct_compact_threshold       = %llu ms\n"
			   "globa_direct_swapout_threshold = %llu ms\n"
			   "memcg_direct_swapout_threshold = %llu ms\n"
			   "direct_swapin_threshold        = %llu ms\n",
			   memlat_threshold.globa_direct_reclaim_thr,
			   memlat_threshold.memcg_direct_reclaim_thr,
			   memlat_threshold.direct_compact_thr,
			   memlat_threshold.globa_direct_swapout_thr,
			   memlat_threshold.memcg_direct_swapout_thr,
			   memlat_threshold.direct_swapin_thr
			   );

	return 0;
}

static int sli_memlat_thr_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, sli_memlat_thr_show, NULL);
}

static ssize_t sli_memlat_thr_write(struct file *file, 
		const char __user *buf, size_t count, loff_t *offs)
{
	char line[LINE_SIZE];
	u64 value;
	char *ptr;
	int i = 0;

	memset(line, 0, LINE_SIZE);

	count = min_t(size_t, count, LINE_SIZE - 1);

	if (strncpy_from_user(line, buf, count) < 0)
		return -EINVAL;

	if (strlen(line) < 1)
		return -EINVAL;

	if (!kstrtou64(line, 0, &value)) {
		sli_memlat_thr_set(value);
		return count;
	}

	for ( ; memlat_member_offset[i].member ; i++) {
		int lenstr = strlen(memlat_member_offset[i].member);

		ptr = strnstr(line, memlat_member_offset[i].member, lenstr);
		if (lenstr && ptr) {
			if (kstrtou64(ptr+lenstr, 0, &value))
				return -EINVAL;

			*((u64 *)((char *)&memlat_threshold + memlat_member_offset[i].offset)) = value;
			return count;
		}
	}

	return -EINVAL;
}

static const struct file_operations sli_memlat_thr_fops = {
	.open		= sli_memlat_thr_open,
	.read		= seq_read,
	.write		= sli_memlat_thr_write,
	.release	= single_release,
};

void sli_schedlat_stat(struct task_struct *task,enum sli_schedlat_stat_item sidx, u64 delta)
{
	enum sli_schedlat_count cidx;
	struct mem_cgroup *memcg;
	struct cgroup *cgrp;
	u64 threshold;

	if (static_branch_likely(&sli_no_enabled) || !task)
		return;

	threshold = get_schedlat_threshold(sidx);
	cidx = get_schedlat_count_idx(delta);

	rcu_read_lock();
	memcg = mem_cgroup_from_task(task);
	if (!memcg || memcg == root_mem_cgroup ) {
		rcu_read_unlock();
		return;
	}
	cgrp = memcg->css.cgroup;

	if (cgrp && cgrp->sli_schedlat_stat_percpu) {
		this_cpu_inc(cgrp->sli_schedlat_stat_percpu->item[sidx][cidx]);
		if (delta >= threshold)
			store_task_stack(cgrp,task,delta);
	}
	rcu_read_unlock();
}

#ifdef CONFIG_SCHED_INFO
void sli_schedlat_syscall_enter(struct task_struct *task)
{
	if (static_branch_likely(&sli_no_enabled))
		return;

	task->sched_info.in_syscall = 1;
	task->sched_info.syscall_exec_time = 0;
	task->sched_info.syscall_start = local_clock();
}

static void sli_schedlat_syscall_stat(struct task_struct *task,
				enum sli_schedlat_stat_item sidx, 
				unsigned long nr,
				u64 delta)
{
	enum sli_schedlat_count cidx;
	struct mem_cgroup *memcg;
	struct cgroup *cgrp;
	u64 threshold;

	if (static_branch_likely(&sli_no_enabled) || !task)
		return;

	threshold = get_schedlat_threshold(sidx);
	cidx = get_schedlat_count_idx(delta);

	rcu_read_lock();
	memcg = mem_cgroup_from_task(task);
	if (!memcg || memcg == root_mem_cgroup ) {
		rcu_read_unlock();
		return;
	}
	cgrp = memcg->css.cgroup;

	if (cgrp && cgrp->sli_schedlat_stat_percpu) {
		this_cpu_inc(cgrp->sli_schedlat_stat_percpu->item[sidx][cidx]);
		if (delta >= threshold)
			mbuf_print(cgrp,"comm:%s,pid:%d,syscall:%d,duration=%lld\n",task->comm,task->pid,nr,delta);
	}
	rcu_read_unlock();
}

void sli_schedlat_syscall_exit(struct task_struct *task,unsigned long nr)
{
	unsigned long long delta;

	if (static_branch_likely(&sli_no_enabled))
		return;

	delta = local_clock() - task->sched_info.syscall_start;
	task->sched_info.syscall_exec_time += delta;
	task->sched_info.in_syscall = 0;
	sli_schedlat_syscall_stat(task,SCHEDLAT_LONGSYS,nr,task->sched_info.syscall_exec_time);

}

void sli_schedlat_syscall_schedout(struct task_struct *task)
{
	unsigned long long syscall_exec;

	if (static_branch_likely(&sli_no_enabled))
		return;

	if (task->sched_info.in_syscall) {
		syscall_exec = local_clock() - task->sched_info.syscall_start;
		task->sched_info.syscall_exec_time += syscall_exec;
	}

}

void sli_schedlat_syscall_schedin(struct task_struct *task)
{
	if (static_branch_likely(&sli_no_enabled))
		return;

	if (task->sched_info.in_syscall) {
		task->sched_info.syscall_start = local_clock();
	}

}
#endif

static void sli_schedlat_thr_set(u64 value)
{
	schedlat_threshold.schedlat_wait_thr = value;
	schedlat_threshold.schedlat_block_thr = value;
	schedlat_threshold.schedlat_ioblock_thr = value;
	schedlat_threshold.schedlat_sleep_thr = value;
	schedlat_threshold.schedlat_rundelay_thr = value;
	schedlat_threshold.schedlat_longsys_thr = value;
}

static u64 sli_schedlat_stat_gather(struct cgroup *cgrp,
				 enum sli_schedlat_stat_item sidx,
				 enum sli_schedlat_count cidx)
{
	u64 sum = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		sum += per_cpu_ptr(cgrp->sli_schedlat_stat_percpu, cpu)->item[sidx][cidx];

	return sum;
}

int sli_schedlat_stat_show(struct seq_file *m, struct cgroup *cgrp)
{
	enum sli_schedlat_stat_item sidx;

	if (static_branch_likely(&sli_no_enabled)) {
		seq_printf(m,"sli is not enabled,please echo 1 > /proc/sli/sli_enabled && "
				   "echo 1 > /proc/sys/kernel/sched_schedstats\n"
				  );
		return 0;
	}

	if (!cgrp->sli_schedlat_stat_percpu)
		return 0;

	for (sidx = SCHEDLAT_WAIT;sidx < SCHEDLAT_STAT_NR;sidx++) {
		seq_printf(m,"%s:\n",get_schedlat_name(sidx));
		seq_printf(m, "\t0-1ms: \t\t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, SCHEDLAT_0_1));
		seq_printf(m, "\t1-5ms: \t\t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, SCHEDLAT_1_5));
		seq_printf(m, "\t5-10ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, SCHEDLAT_5_10));
		seq_printf(m, "\t10-20ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, SCHEDLAT_10_20));
		seq_printf(m, "\t20-30ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, SCHEDLAT_20_30));
		seq_printf(m, "\t30-40ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, SCHEDLAT_30_40));
		seq_printf(m, "\t40-50ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, SCHEDLAT_40_50));
		seq_printf(m, "\t50-60ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, SCHEDLAT_50_60));
		seq_printf(m, "\t60-70ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, SCHEDLAT_60_70));
		seq_printf(m, "\t70-80ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, SCHEDLAT_70_80));
		seq_printf(m, "\t80-90ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, SCHEDLAT_80_90));
		seq_printf(m, "\t90-100ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, SCHEDLAT_90_100));
		seq_printf(m, "\t100-500ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, SCHEDLAT_100_500));
		seq_printf(m, "\t500-1000ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, SCHEDLAT_500_1000));
		seq_printf(m, "\t>=1000ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, SCHEDLAT_1000_INF));
	}

	return 0;
}

static int sli_schedlat_thr_show(struct seq_file *m, void *v)
{
	seq_printf(m,"schedlat_wait_thr = %llu ms\n"
			   "schedlat_block_thr = %llu ms\n"
			   "schedlat_ioblock_thr = %llu ms\n"
			   "schedlat_sleep_thr = %llu ms\n"
			   "schedlat_rundelay_thr = %llu ms\n"
			   "schedlat_longsys_thr = %llu ms\n",
			   schedlat_threshold.schedlat_wait_thr,
			   schedlat_threshold.schedlat_block_thr,
			   schedlat_threshold.schedlat_ioblock_thr,
			   schedlat_threshold.schedlat_sleep_thr,
			   schedlat_threshold.schedlat_rundelay_thr,
			   schedlat_threshold.schedlat_longsys_thr
			   );

	return 0;
}

static int sli_schedlat_thr_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, sli_schedlat_thr_show, NULL);
}

static ssize_t sli_schedlat_thr_write(struct file *file, 
		const char __user *buf, size_t count, loff_t *offs)
{
	char line[LINE_SIZE];
	u64 value;
	char *ptr;
	int i = 0;

	memset(line, 0, LINE_SIZE);

	count = min_t(size_t, count, LINE_SIZE - 1);

	if (strncpy_from_user(line, buf, count) < 0)
		return -EINVAL;

	if (strlen(line) < 1)
		return -EINVAL;

	if (!kstrtou64(line, 0, &value)) {
		sli_schedlat_thr_set(value);
		return count;
	}

	for ( ; schedlat_member_offset[i].member ; i++) {
		int lenstr = strlen(schedlat_member_offset[i].member);

		ptr = strnstr(line, schedlat_member_offset[i].member, lenstr);
		if (lenstr && ptr) {
			if (kstrtou64(ptr+lenstr, 0, &value))
				return -EINVAL;

			*((u64 *)((char *)&schedlat_threshold + schedlat_member_offset[i].offset)) = value;
			return count;
		}
	}

	return -EINVAL;
}

static const struct file_operations sli_schedlat_thr_fops = {
	.open		= sli_schedlat_thr_open,
	.read		= seq_read,
	.write		= sli_schedlat_thr_write,
	.release	= single_release,
};

static int sli_enabled_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", !static_key_enabled(&sli_no_enabled));
	return 0;
}

static int sli_enabled_open(struct inode *inode, struct file *file)
{
	return single_open(file, sli_enabled_show, NULL);
}

static ssize_t sli_enabled_write(struct file *file, const char __user *ubuf,
				    size_t count, loff_t *ppos)
{
	char val = -1;
	int ret = count;

	if (count < 1 || *ppos) {
		ret = -EINVAL;
		goto out;
	}

	if (copy_from_user(&val, ubuf, 1)) {
		ret = -EFAULT;
		goto out;
	}

	switch (val) {
	case '0':
		static_branch_enable(&sli_no_enabled);
		break;
	case '1':
		static_branch_disable(&sli_no_enabled);
		break;
	default:
		ret = -EINVAL;
	}

out:
	return ret;
}

static const struct file_operations sli_enabled_fops = {
	.open       = sli_enabled_open,
	.read       = seq_read,
	.write      = sli_enabled_write,
	.llseek     = seq_lseek,
	.release    = single_release,
};

int sli_cgroup_alloc(struct cgroup *cgroup)
{
	if (!cgroup)
		return 0;

	cgroup->sli_memlat_stat_percpu = alloc_percpu(struct sli_memlat_stat);
	if (!cgroup->sli_memlat_stat_percpu)
		return -ENOMEM;

	cgroup->sli_schedlat_stat_percpu = alloc_percpu(struct sli_schedlat_stat);
	if (!cgroup->sli_schedlat_stat_percpu) {
		free_percpu(cgroup->sli_memlat_stat_percpu);
		return -ENOMEM;
	}

	return 0;
}

void sli_cgroup_free(struct cgroup *cgroup)
{
	if (!cgroup)
		return;

	if (cgroup->sli_memlat_stat_percpu)
		free_percpu(cgroup->sli_memlat_stat_percpu);
	if (cgroup->sli_schedlat_stat_percpu)
		free_percpu(cgroup->sli_schedlat_stat_percpu);
}

static int __init sli_proc_init(void)
{
	/*default threshhold value is 10s*/
	sli_memlat_thr_set(10000);
	sli_schedlat_thr_set(10000);
	proc_mkdir("sli", NULL);
	proc_create("sli/sli_enabled", 0, NULL, &sli_enabled_fops);
	proc_create("sli/memory_latency_threshold", 0, NULL, &sli_memlat_thr_fops);
	proc_create("sli/sched_latency_threshold", 0, NULL, &sli_schedlat_thr_fops);
	return 0;
}

late_initcall(sli_proc_init);

