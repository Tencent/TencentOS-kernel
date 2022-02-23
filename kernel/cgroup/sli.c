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
#include <linux/rculist.h>

#define MAX_STACK_TRACE_DEPTH	64

static DEFINE_STATIC_KEY_FALSE(sli_enabled);
static DEFINE_STATIC_KEY_FALSE(sli_monitor_enabled);

static struct sli_event_monitor default_sli_event_monitor;
static struct workqueue_struct *sli_workqueue;

static void sli_event_monitor_init(struct sli_event_monitor *event_monitor, struct cgroup *cgrp)
{
	INIT_LIST_HEAD_RCU(&event_monitor->event_head);

	memset(&event_monitor->schedlat_threshold, 0xff, sizeof(event_monitor->schedlat_threshold));
	memset(&event_monitor->schedlat_count, 0xff, sizeof(event_monitor->schedlat_count));
	memset(&event_monitor->memlat_threshold, 0xff, sizeof(event_monitor->memlat_threshold));
	memset(&event_monitor->memlat_count, 0xff, sizeof(event_monitor->memlat_count));
	memset(&event_monitor->longterm_threshold, 0xff, sizeof(event_monitor->longterm_threshold));

	event_monitor->last_update = jiffies;
	event_monitor->cgrp = cgrp;
}

static inline struct sli_event_monitor *get_sli_event_monitor(struct cgroup *cgrp)
{
	return &default_sli_event_monitor;
}

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

static char * get_memlat_name(enum sli_memlat_stat_item sidx)
{
	char *name = NULL;

	switch (sidx) {
	case MEM_LAT_GLOBAL_DIRECT_RECLAIM:
		name = "memlat_global_direct_reclaim";
		break;
	case MEM_LAT_MEMCG_DIRECT_RECLAIM:
		name =  "memlat_memcg_direct_reclaim";
		break;
	case MEM_LAT_DIRECT_COMPACT:
		name = "memlat_direct_compact";
		break;
	case MEM_LAT_GLOBAL_DIRECT_SWAPOUT:
		name = "memlat_global_direct_swapout";
		break;
	case MEM_LAT_MEMCG_DIRECT_SWAPOUT:
		name = "memlat_memcg_direct_swapout";
		break;
	case MEM_LAT_DIRECT_SWAPIN:
		name = "memlat_direct_swapin";
		break;
	case MEM_LAT_PAGE_ALLOC:
		name = "memlat_page_alloc";
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
	case SCHEDLAT_IRQTIME:
		name = "schedlat_irqtime";
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

	if (!static_branch_likely(&sli_enabled)) {
		seq_printf(m, "sli is not enabled, please echo 1 > /proc/sli/sli_enabled\n");
		return 0;
	}

	if (!cgrp->sli_memlat_stat_percpu)
		return 0;

	for (sidx = MEM_LAT_GLOBAL_DIRECT_RECLAIM;sidx < MEM_LAT_STAT_NR;sidx++) {
		seq_printf(m, "%s:\n", get_memlat_name(sidx));
		seq_printf(m, "0-1ms: %llu\n", sli_memlat_stat_gather(cgrp, sidx, LAT_0_1));
		seq_printf(m, "1-4ms: %llu\n", sli_memlat_stat_gather(cgrp, sidx, LAT_1_4));
		seq_printf(m, "4-8ms: %llu\n", sli_memlat_stat_gather(cgrp, sidx, LAT_4_8));
		seq_printf(m, "8-16ms: %llu\n", sli_memlat_stat_gather(cgrp, sidx, LAT_8_16));
		seq_printf(m, "16-32ms: %llu\n", sli_memlat_stat_gather(cgrp, sidx, LAT_16_32));
		seq_printf(m, "32-64ms: %llu\n", sli_memlat_stat_gather(cgrp, sidx, LAT_32_64));
		seq_printf(m, "64-128ms: %llu\n", sli_memlat_stat_gather(cgrp, sidx, LAT_64_128));
		seq_printf(m, ">=128ms: %llu\n", sli_memlat_stat_gather(cgrp, sidx, LAT_128_INF));
	}

	return 0;
}

int sli_memlat_max_show(struct seq_file *m, struct cgroup *cgrp)
{
	enum sli_memlat_stat_item sidx;

	if (!static_branch_likely(&sli_enabled)) {
		seq_printf(m, "sli is not enabled, please echo 1 > /proc/sli/sli_enabled\n");
		return 0;
	}

	if (!cgrp->sli_memlat_stat_percpu)
		return 0;

	for (sidx = MEM_LAT_GLOBAL_DIRECT_RECLAIM; sidx < MEM_LAT_STAT_NR; sidx++) {
		int cpu;
		unsigned long latency_sum = 0;

		for_each_possible_cpu(cpu)
			latency_sum += per_cpu_ptr(cgrp->sli_memlat_stat_percpu, cpu)->latency_max[sidx];

		seq_printf(m, "%s: %lu\n", get_memlat_name(sidx), latency_sum);
	}

	return 0;
}

void sli_memlat_stat_start(u64 *start)
{
	if (!static_branch_likely(&sli_enabled))
		*start = 0;
	else
		*start = local_clock();
}

void sli_memlat_stat_end(enum sli_memlat_stat_item sidx, u64 start)
{
	struct mem_cgroup *memcg;
	struct cgroup *cgrp;

	if (!static_branch_likely(&sli_enabled) || start == 0)
		return;

	rcu_read_lock();
	memcg = mem_cgroup_from_task(current);
	if (!memcg || memcg == root_mem_cgroup)
		goto out;

	cgrp = memcg->css.cgroup;
	if (cgrp && cgroup_parent(cgrp)) {
		enum sli_lat_count cidx;
		u64 duration;

		duration = local_clock() - start;
		cidx = get_lat_count_idx(duration);

		duration = duration >> 10;
		this_cpu_inc(cgrp->sli_memlat_stat_percpu->item[sidx][cidx]);
		this_cpu_add(cgrp->sli_memlat_stat_percpu->latency_max[sidx], duration);

		if (static_branch_unlikely(&sli_monitor_enabled)) {
			struct sli_event_monitor *event_monitor;

			event_monitor = get_sli_event_monitor(cgrp);
			if (duration < READ_ONCE(event_monitor->memlat_threshold[sidx]))
				goto out;

			if (event_monitor->mbuf_enable) {
				char *lat_name;

				lat_name = get_memlat_name(sidx);
				store_task_stack(current, lat_name, duration, 0);
			}
		}
	}

out:
	rcu_read_unlock();
}

void sli_schedlat_stat(struct task_struct *task, enum sli_schedlat_stat_item sidx, u64 delta)
{
	struct cgroup *cgrp = NULL;

	if (!static_branch_likely(&sli_enabled) || !task)
		return;

	rcu_read_lock();
	cgrp = get_cgroup_from_task(task);
	if (cgrp && cgroup_parent(cgrp)) {
		enum sli_lat_count cidx = get_lat_count_idx(delta);

		delta = delta >> 10;
		this_cpu_inc(cgrp->sli_schedlat_stat_percpu->item[sidx][cidx]);
		this_cpu_add(cgrp->sli_schedlat_stat_percpu->latency_max[sidx], delta);

		if (static_branch_unlikely(&sli_monitor_enabled)) {
			struct sli_event_monitor *event_monitor;

			event_monitor = get_sli_event_monitor(cgrp);
			if (delta < READ_ONCE(event_monitor->schedlat_threshold[sidx]))
				goto out;

			if (event_monitor->mbuf_enable) {
				char *lat_name;

				lat_name = get_schedlat_name(sidx);
				store_task_stack(task, lat_name, delta, 0);
			}
		}
	}

out:
	rcu_read_unlock();
}

void sli_schedlat_rundelay(struct task_struct *task, struct task_struct *prev, u64 delta)
{
	enum sli_schedlat_stat_item sidx = SCHEDLAT_RUNDELAY;
	struct cgroup *cgrp = NULL;

	if (!static_branch_likely(&sli_enabled) || !task || !prev)
		return;

	rcu_read_lock();
	cgrp = get_cgroup_from_task(task);
	if (cgrp && cgroup_parent(cgrp)) {
		enum sli_lat_count cidx = get_lat_count_idx(delta);

		delta = delta >> 10;
		this_cpu_inc(cgrp->sli_schedlat_stat_percpu->item[sidx][cidx]);
		this_cpu_add(cgrp->sli_schedlat_stat_percpu->latency_max[sidx], delta);

		if (static_branch_unlikely(&sli_monitor_enabled)) {
			struct sli_event_monitor *event_monitor;

			event_monitor = get_sli_event_monitor(cgrp);
			if (delta < READ_ONCE(event_monitor->schedlat_threshold[sidx]))
				goto out;

			if (event_monitor->mbuf_enable) {
				int i;
				unsigned long *entries;
				unsigned int nr_entries = 0;
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
		}
	}

out:
	rcu_read_unlock();
}

#ifdef CONFIG_SCHED_INFO
void sli_check_longsys(struct task_struct *tsk)
{
	long delta;

	if (!static_branch_likely(&sli_enabled))
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

	if (!static_branch_likely(&sli_enabled)) {
		seq_printf(m, "sli is not enabled, please echo 1 > /proc/sli/sli_enabled\n");
		return 0;
	}

	if (!cgrp->sli_schedlat_stat_percpu)
		return 0;

	for (sidx = SCHEDLAT_WAIT; sidx < SCHEDLAT_STAT_NR; sidx++) {
		int cpu;
		unsigned long latency_sum = 0;

		for_each_possible_cpu(cpu)
			latency_sum += per_cpu_ptr(cgrp->sli_schedlat_stat_percpu, cpu)->latency_max[sidx];

		seq_printf(m, "%s: %lu\n", get_schedlat_name(sidx), latency_sum);
	}

	return 0;
}

int sli_schedlat_stat_show(struct seq_file *m, struct cgroup *cgrp)
{
	enum sli_schedlat_stat_item sidx;

	if (!static_branch_likely(&sli_enabled)) {
		seq_printf(m, "sli is not enabled, please echo 1 > /proc/sli/sli_enabled\n");
		return 0;
	}

	if (!cgrp->sli_schedlat_stat_percpu)
		return 0;

	for (sidx = SCHEDLAT_WAIT;sidx < SCHEDLAT_STAT_NR;sidx++) {
		seq_printf(m, "%s:\n", get_schedlat_name(sidx));
		seq_printf(m, "0-1ms: %llu\n", sli_schedlat_stat_gather(cgrp, sidx, LAT_0_1));
		seq_printf(m, "1-4ms: %llu\n", sli_schedlat_stat_gather(cgrp, sidx, LAT_1_4));
		seq_printf(m, "4-8ms: %llu\n", sli_schedlat_stat_gather(cgrp, sidx, LAT_4_8));
		seq_printf(m, "8-16ms: %llu\n", sli_schedlat_stat_gather(cgrp, sidx, LAT_8_16));
		seq_printf(m, "16-32ms: %llu\n", sli_schedlat_stat_gather(cgrp, sidx, LAT_16_32));
		seq_printf(m, "32-64ms: %llu\n", sli_schedlat_stat_gather(cgrp, sidx, LAT_32_64));
		seq_printf(m, "64-128ms: %llu\n", sli_schedlat_stat_gather(cgrp, sidx, LAT_64_128));
		seq_printf(m, ">=128ms: %llu\n", sli_schedlat_stat_gather(cgrp, sidx, LAT_128_INF));
	}

	return 0;
}

static int sli_enabled_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", static_key_enabled(&sli_enabled));
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
		if (static_key_enabled(&sli_enabled))
			static_branch_disable(&sli_enabled);
		break;
	case '1':
		if (!static_key_enabled(&sli_enabled))
			static_branch_enable(&sli_enabled);
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
	sli_event_monitor_init(&default_sli_event_monitor, NULL);
	sli_workqueue = alloc_workqueue("events_unbound", WQ_UNBOUND, WQ_UNBOUND_MAX_ACTIVE);
	if (!sli_workqueue) {
		printk(KERN_ERR "Create sli workqueue failed!\n");
		return -1;
	}
	proc_mkdir("sli", NULL);
	proc_create("sli/sli_enabled", 0, NULL, &sli_enabled_fops);
	return 0;
}

late_initcall(sli_proc_init);

