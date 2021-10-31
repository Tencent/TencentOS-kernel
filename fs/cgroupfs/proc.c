#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/cgroup.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include "cgroupfs.h"

extern int cpuset_cgroupfs_cpuinfo_show(struct seq_file *m, void *v);
extern int cpuset_cgroupfs_stat_show(struct seq_file *m, void *v);
extern int mem_cgroupfs_meminfo_show(struct seq_file *m, void *v);
extern int cpuacct_cgroupfs_uptime_show(struct seq_file *m, void *v);
extern int cpuset_cgroupfs_loadavg_show(struct seq_file *m, void *v);
extern int blkcg_cgroupfs_dkstats_show(struct seq_file *m, void *v);
extern int mem_cgroupfs_vmstat_show(struct seq_file *m, void *v);
extern int blkcg_cgroupfs_dkstats_show(struct seq_file *m, void *v);

static int cgroup_fs_show(struct seq_file *m, void *v)
{
	cgroupfs_entry_t *private = m->private;
	switch (private->cgroupfs_type) {
	case CGROUPFS_TYPE_MEMINFO:
		return mem_cgroupfs_meminfo_show(m, v);
	case CGROUPFS_TYPE_CPUINFO:
		return cpuset_cgroupfs_cpuinfo_show(m, v);
	case CGROUPFS_TYPE_STAT:
		return cpuset_cgroupfs_stat_show(m, v);
	case CGROUPFS_TYPE_UPTIME:
		return cpuacct_cgroupfs_uptime_show(m, v);
	case CGROUPFS_TYPE_LOADAVG:
		return cpuset_cgroupfs_loadavg_show(m, v);
	case CGROUPFS_TYPE_DKSTATS:
		return blkcg_cgroupfs_dkstats_show(m, v);
	case CGROUPFS_TYPE_VMSTAT:
		return mem_cgroupfs_vmstat_show(m, v);
	default:
		break;
	}
	return 0;
}

static int cgroupfs_seq_open(struct inode *inode, struct file *file)
{
	void *private = inode->i_private;
	return single_open(file, cgroup_fs_show, private);
}

static int cgroupfs_seq_release(struct inode *inode, struct file *file)
{
	return single_release(inode, file);
}

static struct file_operations cgroupfs_file_ops = {
	.open    = cgroupfs_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = cgroupfs_seq_release,
};

void cgroupfs_set_proc_fops(cgroupfs_entry_t *en)
{
	en->e_fops = &cgroupfs_file_ops;
};
