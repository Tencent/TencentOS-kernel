#include <linux/fs.h>
#include <linux/fs_context.h>
#include "cgroupfs.h"

extern int cpu_get_max_cpus(struct task_struct *p);
extern int cpuset_cgroups_cpu_allowed(struct task_struct *task, int cpu, int once);

static int cgroupfs_dop_revalidate(struct dentry *dentry, unsigned int flags)
{
	return 0;
}

int cgroupfs_cpu_dir_filter(int cpu, int *max_cpu, int *counted_cpu, int once)
{
	if (!once && *counted_cpu == -1) {
		*max_cpu = cpu_get_max_cpus(current);
		*counted_cpu = 0;
	}
	if (!once && *counted_cpu >= *max_cpu)
		return 1;
	if (!cpuset_cgroups_cpu_allowed(current, cpu, once))
		return 1;
	*counted_cpu += 1;
	return 0;
}

const struct dentry_operations cgroupfs_dir_dops = {
	.d_revalidate   = cgroupfs_dop_revalidate,
	.d_delete       = always_delete_dentry,
};

void cgroupfs_set_sys_dops(cgroupfs_entry_t *en)
{
	en->e_dops = &cgroupfs_dir_dops;
}
