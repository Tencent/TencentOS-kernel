#ifndef __LINUX_CGROUPFS_H
#define __LINUX_CGROUPFS_H
enum cgroupfs_file_type{
	CGROUPFS_TYPE_MEMINFO,
	CGROUPFS_TYPE_CPUINFO,
	CGROUPFS_TYPE_STAT,
	CGROUPFS_TYPE_UPTIME,
	CGROUPFS_TYPE_LOADAVG,
	CGROUPFS_TYPE_DKSTATS,
	CGROUPFS_TYPE_VMSTAT,
	CGROUPFS_TYPE_CPUDIR,
	CGROUPFS_TYPE_NORMAL_DIR,
	CGROUPFS_TYPE_LAST,
};

typedef struct cgroupfs_entry {
	struct rb_root subdir;
	struct rb_node subdir_node;
	const char *name;
	struct cgroupfs_entry *parent;
	struct super_block *sb;
	refcount_t refcnt;
	u64 inum;
	int cgroupfs_type;
	int cpu;
	umode_t mode;
	int namelen;
	const struct inode_operations *e_iops;
	const struct file_operations *e_fops;
	const struct dentry_operations *e_dops;
	KABI_RESERVE(1);
	KABI_RESERVE(2);
	KABI_RESERVE(3);
	KABI_RESERVE(4);
} cgroupfs_entry_t;

cgroupfs_entry_t *cgroupfs_alloc_private(const char *name,
		cgroupfs_entry_t *parent, int cgroupfs_type);
void cgroupfs_set_proc_fops(cgroupfs_entry_t *en);
void cgroupfs_set_sys_dops(cgroupfs_entry_t *en);
struct inode *cgroupfs_get_inode(cgroupfs_entry_t *en);

static inline void cgroupfs_get_entry(cgroupfs_entry_t *en)
{
	refcount_inc(&en->refcnt);
}

void cgroupfs_put_entry(cgroupfs_entry_t *en);
int cgroupfs_cpu_dir_filter(int cpu, int *max_cpu, int *counted_cpu, int once);
#endif
