#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/cgroup.h>
#include <linux/module.h>
#include <linux/psi.h>
#include <linux/memcontrol.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/sysctl.h>
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
	u64 global_direct_reclaim_thr;
	u64 memcg_direct_reclaim_thr;
	u64 direct_compact_thr;
	u64 global_direct_swapout_thr;
	u64 memcg_direct_swapout_thr;
	u64 direct_swapin_thr;
	u64 page_alloc_thr;
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
static struct memlat_thr memlat_threshold;
static struct schedlat_thr schedlat_threshold;

static const struct member_offset memlat_member_offset[] = {
	{ "global_direct_reclaim_threshold=", offsetof(struct memlat_thr, global_direct_reclaim_thr)},
	{ "memcg_direct_reclaim_threshold=", offsetof(struct memlat_thr, memcg_direct_reclaim_thr)},
	{ "direct_compact_threshold=", offsetof(struct memlat_thr, direct_compact_thr)},
	{ "global_direct_swapout_threshold=", offsetof(struct memlat_thr, global_direct_swapout_thr)},
	{ "memcg_direct_swapout_threshold=", offsetof(struct memlat_thr, memcg_direct_swapout_thr)},
	{ "direct_swapin_threshold=", offsetof(struct memlat_thr, direct_swapin_thr)},
	{ "page_alloc_threshold=", offsetof(struct memlat_thr, page_alloc_thr)},
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

static void store_task_stack(struct task_struct *task, char *reason,
			     u64 duration, unsigned int skipnr)
{
	unsigned long *entries;
	unsigned nr_entries = 0;
	unsigned long flags;
	int i;
	struct cgroup *cgrp;

	entries = kmalloc_array(MAX_STACK_TRACE_DEPTH, sizeof(*entries),
				GFP_ATOMIC);
	if (!entries)
		return;

	nr_entries = stack_trace_save_tsk(task, entries, MAX_STACK_TRACE_DEPTH, skipnr);

	cgrp = get_cgroup_from_task(task);
	spin_lock_irqsave(&cgrp->cgrp_mbuf_lock, flags);

	mbuf_print(cgrp, "record reason:%s comm:%s pid:%d duration=%lld\n",
		   reason, task->comm, task->pid, duration);

	for (i = 0; i < nr_entries; i++)
		mbuf_print(cgrp, "[<0>] %pB\n", (void *)entries[i]);

	spin_unlock_irqrestore(&cgrp->cgrp_mbuf_lock, flags);

	kfree(entries);
	return;
}

static u64 get_memlat_threshold(enum sli_memlat_stat_item sidx)
{
	long threshold = -1;

	switch (sidx) {
	case MEM_LAT_GLOBAL_DIRECT_RECLAIM:
		threshold = memlat_threshold.global_direct_reclaim_thr;
		break;
	case MEM_LAT_MEMCG_DIRECT_RECLAIM:
		threshold = memlat_threshold.memcg_direct_reclaim_thr;
		break;
	case MEM_LAT_DIRECT_COMPACT:
		threshold = memlat_threshold.direct_compact_thr;
		break;
	case MEM_LAT_GLOBAL_DIRECT_SWAPOUT:
		threshold = memlat_threshold.global_direct_swapout_thr;
		break;
	case MEM_LAT_MEMCG_DIRECT_SWAPOUT:
		threshold = memlat_threshold.memcg_direct_swapout_thr;
		break;
	case MEM_LAT_DIRECT_SWAPIN:
		threshold = memlat_threshold.direct_swapin_thr;
		break;
	case MEM_LAT_PAGE_ALLOC:
		threshold = memlat_threshold.page_alloc_thr;
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
		name = "global_direct_reclaim";
		break;
	case MEM_LAT_MEMCG_DIRECT_RECLAIM:
		name =  "memcg_direct_reclaim";
		break;
	case MEM_LAT_DIRECT_COMPACT:
		name = "direct_compact";
		break;
	case MEM_LAT_GLOBAL_DIRECT_SWAPOUT:
		name = "global_direct_swapout";
		break;
	case MEM_LAT_MEMCG_DIRECT_SWAPOUT:
		name = "memcg_direct_swapout";
		break;
	case MEM_LAT_DIRECT_SWAPIN:
		name = "direct_swapin";
		break;
	case MEM_LAT_PAGE_ALLOC:
		name = "page_alloc";
		break;
	default:
		break;
	}

	return name;
}

static enum sli_lat_count get_lat_count_idx(u64 duration)
{
	enum sli_lat_count idx;

	duration = duration >> 20;
	if (duration < 1)
		idx = LAT_0_1;
	else if (duration < 4)
		idx = LAT_1_4;
	else if (duration < 8)
		idx = LAT_4_8;
	else if (duration < 16)
		idx = LAT_8_16;
	else if (duration < 32)
		idx = LAT_16_32;
	else if (duration < 64)
		idx = LAT_32_64;
	else if (duration < 128)
		idx = LAT_64_128;
	else
		idx = LAT_128_INF;

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
		threshold = threshold << 20;//ms to ns,2^20=1048576(about 1000000)

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

static u64 sli_memlat_stat_gather(struct cgroup *cgrp,
				 enum sli_memlat_stat_item sidx,
				 enum sli_lat_count cidx)
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
				   sli_memlat_stat_gather(cgrp, sidx, LAT_0_1));
		seq_printf(m, "\t1-4ms: \t\t%llu\n",
				   sli_memlat_stat_gather(cgrp, sidx, LAT_1_4));
		seq_printf(m, "\t4-8ms: \t\t%llu\n",
				   sli_memlat_stat_gather(cgrp, sidx, LAT_4_8));
		seq_printf(m, "\t8-16ms: \t%llu\n",
				   sli_memlat_stat_gather(cgrp, sidx, LAT_8_16));
		seq_printf(m, "\t16-32ms: \t%llu\n",
				   sli_memlat_stat_gather(cgrp, sidx, LAT_16_32));
		seq_printf(m, "\t32-64ms: \t%llu\n",
				   sli_memlat_stat_gather(cgrp, sidx, LAT_32_64));
		seq_printf(m, "\t64-128ms: \t%llu\n",
				   sli_memlat_stat_gather(cgrp, sidx, LAT_64_128));
		seq_printf(m, "\t>=128ms: \t%llu\n",
				   sli_memlat_stat_gather(cgrp, sidx, LAT_128_INF));
	}

	return 0;
}

int sli_memlat_max_show(struct seq_file *m, struct cgroup *cgrp)
{
	enum sli_memlat_stat_item sidx;

	if (static_branch_likely(&sli_no_enabled)) {
		seq_printf(m,"sli is not enabled,please echo 1 > /proc/sli/sli_enabled && "
				   "echo 1 > /proc/sys/kernel/sched_schedstats\n"
				  );
		return 0;
	}

	if (!cgrp->sli_memlat_stat_percpu)
		return 0;

	for (sidx = MEM_LAT_GLOBAL_DIRECT_RECLAIM; sidx < MEM_LAT_STAT_NR; sidx++) {
		int cpu;
		unsigned long latency_max = 0, latency;

		for_each_possible_cpu(cpu) {
			latency = per_cpu_ptr(cgrp->sli_memlat_stat_percpu, cpu)->latency_max[sidx];
			per_cpu_ptr(cgrp->sli_memlat_stat_percpu, cpu)->latency_max[sidx] = 0;

			if (latency > latency_max)
				latency_max = latency;
		}

		seq_printf(m,"%s: %lu\n", get_memlat_name(sidx), latency_max);
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
	enum sli_lat_count cidx;
	u64 duration,threshold;
	struct mem_cgroup *memcg;
	struct cgroup *cgrp;

	if (static_branch_likely(&sli_no_enabled) || start == 0)
		return;

	duration = local_clock() - start;
	threshold = get_memlat_threshold(sidx);
	cidx = get_lat_count_idx(duration);

	rcu_read_lock();
	memcg = mem_cgroup_from_task(current);
	if (!memcg || memcg == root_mem_cgroup ) {
		rcu_read_unlock();
		return;
	}

	cgrp = memcg->css.cgroup;
	if (cgrp && cgrp->sli_memlat_stat_percpu) {
		this_cpu_inc(cgrp->sli_memlat_stat_percpu->item[sidx][cidx]);
		if (duration >= threshold) {
			char *lat_name;

			lat_name = get_memlat_name(sidx);
			store_task_stack(current, lat_name, duration, 0);
		}

		if (duration > this_cpu_read(cgrp->sli_memlat_stat_percpu->latency_max[sidx]))
			this_cpu_write(cgrp->sli_memlat_stat_percpu->latency_max[sidx], duration);
	}
	rcu_read_unlock();
}

static void sli_memlat_thr_set(u64 value)
{
	memlat_threshold.global_direct_reclaim_thr = value;
	memlat_threshold.memcg_direct_reclaim_thr = value;
	memlat_threshold.direct_compact_thr       = value;
	memlat_threshold.global_direct_swapout_thr = value;
	memlat_threshold.memcg_direct_swapout_thr = value;
	memlat_threshold.direct_swapin_thr	  = value;
	memlat_threshold.page_alloc_thr		  = value;
}

static int sli_memlat_thr_show(struct seq_file *m, void *v)
{
	seq_printf(m,"global_direct_reclaim_threshold = %llu ms\n"
			   "memcg_direct_reclaim_threshold = %llu ms\n"
			   "direct_compact_threshold       = %llu ms\n"
			   "global_direct_swapout_threshold = %llu ms\n"
			   "memcg_direct_swapout_threshold = %llu ms\n"
			   "direct_swapin_threshold        = %llu ms\n"
			   "page_alloc_threshold	   = %llu ms\n",
			   memlat_threshold.global_direct_reclaim_thr,
			   memlat_threshold.memcg_direct_reclaim_thr,
			   memlat_threshold.direct_compact_thr,
			   memlat_threshold.global_direct_swapout_thr,
			   memlat_threshold.memcg_direct_swapout_thr,
			   memlat_threshold.direct_swapin_thr,
			   memlat_threshold.page_alloc_thr
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

void sli_schedlat_stat(struct task_struct *task, enum sli_schedlat_stat_item sidx, u64 delta)
{
	enum sli_lat_count cidx;
	struct cgroup *cgrp = NULL;
	u64 threshold;

	if (static_branch_likely(&sli_no_enabled) || !task)
		return;

	threshold = get_schedlat_threshold(sidx);
	cidx = get_lat_count_idx(delta);

	rcu_read_lock();
	cgrp = get_cgroup_from_task(task);
	if (cgrp && cgrp->sli_schedlat_stat_percpu) {
		this_cpu_inc(cgrp->sli_schedlat_stat_percpu->item[sidx][cidx]);
		if (delta >= threshold) {
			char *lat_name;

			lat_name = get_schedlat_name(sidx);
			store_task_stack(task, lat_name, delta, 0);
		}

		if (delta > this_cpu_read(cgrp->sli_schedlat_stat_percpu->latency_max[sidx]))
			this_cpu_write(cgrp->sli_schedlat_stat_percpu->latency_max[sidx], delta);
	}
	rcu_read_unlock();
}

void sli_schedlat_rundelay(struct task_struct *task, struct task_struct *prev, u64 delta)
{
	enum sli_lat_count cidx;
	enum sli_schedlat_stat_item sidx = SCHEDLAT_RUNDELAY;
	struct cgroup *cgrp = NULL;
	u64 threshold;

	if (static_branch_likely(&sli_no_enabled) || !task || !prev)
		return;

	threshold = get_schedlat_threshold(sidx);
	cidx = get_lat_count_idx(delta);

	rcu_read_lock();
	cgrp = get_cgroup_from_task(task);
	if (cgrp && cgrp->sli_schedlat_stat_percpu) {
		this_cpu_inc(cgrp->sli_schedlat_stat_percpu->item[sidx][cidx]);
		if (delta >= threshold) {
			int i;
			unsigned long *entries;
			unsigned nr_entries = 0;
			unsigned long flags;

			entries = kmalloc_array(MAX_STACK_TRACE_DEPTH, sizeof(*entries),
						GFP_ATOMIC);
			if (!entries)
				goto out;

			nr_entries = stack_trace_save_tsk(prev, entries,
							  MAX_STACK_TRACE_DEPTH, 0);

			spin_lock_irqsave(&cgrp->cgrp_mbuf_lock, flags);

			mbuf_print(cgrp, "record reason:schedlat_rundelay next_comm:%s "
				   "next_pid:%d prev_comm:%s prev_pid:%d duration=%lld\n",
				   task->comm, task->pid, prev->comm, prev->pid, delta);

			for (i = 0; i < nr_entries; i++)
		                mbuf_print(cgrp, "[<0>] %pB\n", (void *)entries[i]);

			spin_unlock_irqrestore(&cgrp->cgrp_mbuf_lock, flags);
			kfree(entries);
		}

		if (delta > this_cpu_read(cgrp->sli_schedlat_stat_percpu->latency_max[sidx]))
			this_cpu_write(cgrp->sli_schedlat_stat_percpu->latency_max[sidx], delta);
	}

out:
	rcu_read_unlock();
}

#ifdef CONFIG_SCHED_INFO
void sli_check_longsys(struct task_struct *tsk)
{
	long delta;

	if (static_branch_likely(&sli_no_enabled))
		return;

	if (!tsk || tsk->sched_class != &fair_sched_class)
		return ;

	/* Longsys is performed only when TIF_RESCHED is set */
	if (!test_tsk_need_resched(tsk))
		return;

	/* Kthread is not belong to any cgroup */
	if (tsk->flags & PF_KTHREAD)
		return;

	if (!tsk->sched_info.kernel_exec_start ||
	    tsk->sched_info.task_switch != (tsk->nvcsw + tsk->nivcsw) ||
	    tsk->utime != tsk->sched_info.utime) {
		tsk->sched_info.utime = tsk->utime;
		tsk->sched_info.kernel_exec_start = rq_clock(this_rq());
		tsk->sched_info.task_switch = tsk->nvcsw + tsk->nivcsw;
		return;
	}

	delta = rq_clock(this_rq()) - tsk->sched_info.kernel_exec_start;
	sli_schedlat_stat(tsk, SCHEDLAT_LONGSYS, delta);
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
				 enum sli_lat_count cidx)
{
	u64 sum = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		sum += per_cpu_ptr(cgrp->sli_schedlat_stat_percpu, cpu)->item[sidx][cidx];

	return sum;
}

int sli_schedlat_max_show(struct seq_file *m, struct cgroup *cgrp)
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

	for (sidx = SCHEDLAT_WAIT; sidx < SCHEDLAT_STAT_NR; sidx++) {
		int cpu;
		unsigned long latency_max = 0, latency;

		for_each_possible_cpu(cpu) {
			latency = per_cpu_ptr(cgrp->sli_schedlat_stat_percpu, cpu)->latency_max[sidx];
			per_cpu_ptr(cgrp->sli_schedlat_stat_percpu, cpu)->latency_max[sidx] = 0;

			if (latency > latency_max)
				latency_max = latency;
		}

		seq_printf(m,"%s: %lu\n", get_schedlat_name(sidx), latency_max);
	}

	return 0;
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
				   sli_schedlat_stat_gather(cgrp, sidx, LAT_0_1));
		seq_printf(m, "\t1-4ms: \t\t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, LAT_1_4));
		seq_printf(m, "\t4-8ms: \t\t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, LAT_4_8));
		seq_printf(m, "\t8-16ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, LAT_8_16));
		seq_printf(m, "\t16-32ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, LAT_16_32));
		seq_printf(m, "\t32-64ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, LAT_32_64));
		seq_printf(m, "\t64-128ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, LAT_64_128));
		seq_printf(m, "\t>=128ms: \t%llu\n",
				   sli_schedlat_stat_gather(cgrp, sidx, LAT_128_INF));
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

	spin_lock_init(&cgroup->cgrp_mbuf_lock);
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
	/* default threshhold is the max value of u64 */
	sli_memlat_thr_set(-1);
	sli_schedlat_thr_set(-1);
	proc_mkdir("sli", NULL);
	proc_create("sli/sli_enabled", 0, NULL, &sli_enabled_fops);
	proc_create("sli/memory_latency_threshold", 0, NULL, &sli_memlat_thr_fops);
	proc_create("sli/sched_latency_threshold", 0, NULL, &sli_schedlat_thr_fops);
	return 0;
}

late_initcall(sli_proc_init);

