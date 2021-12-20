#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/init_task.h>
#include <linux/fs_struct.h>
#include "../internal.h"
#include "cgroupfs.h"

extern int cpu_get_max_cpus(struct task_struct *p);
extern int cpuset_cgroups_cpu_allowed(struct task_struct *task, int cpu, int once);

static int __attribute__((unused)) cgroupfs_dop_revalidate(
			struct dentry *dentry, unsigned int flags)
{
	cgroupfs_entry_t *en;

	en = dentry->d_inode->i_private;
	if (en->cgroupfs_type & CGROUPFS_TYPE_CPUDIR)
		return 0;
	return 1;
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

static struct vfsmount *cgroupfs_dop_auto_mount(struct path *target)
{
	cgroupfs_entry_t *en;
	struct path path;
	int err;

	en = target->dentry->d_inode->i_private;
	err = vfs_path_lookup(init_task.fs->root.dentry, init_task.fs->root.mnt,
		en->private, 0, &path);
	if (err)
		return ERR_PTR(err);
	dput(target->dentry);
	target->dentry = path.dentry;
	target->mnt = path.mnt;
	return 0;
}

const struct dentry_operations cgroupfs_dir_dops = {
	.d_delete       = always_delete_dentry,
	.d_automount    = cgroupfs_dop_auto_mount,
};

void cgroupfs_set_sys_dops(cgroupfs_entry_t *en)
{
	en->e_dops = &cgroupfs_dir_dops;
}
