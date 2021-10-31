#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/cgroup.h>
#include <linux/rbtree.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <linux/err.h>
#include <linux/kernfs.h>
#include "cgroupfs.h"

#define CGROUPFS_MAGIC 3718152116619

static DEFINE_RWLOCK(cgroupfs_subdir_lock);

cgroupfs_entry_t *cgroupfs_root;

static int cgroufs_entry_num;

static void cgroupfs_free_inode(struct inode *inode)
{
	if (S_ISLNK(inode->i_mode))
		kfree(inode->i_link);
	free_inode_nonrcu(inode);
}

static inline void cgroupfs_free_entry(cgroupfs_entry_t *en)
{
	if (!en)
		return;
	if (en->name)
		kfree(en->name);
	kfree(en);
}

void cgroupfs_put_entry(cgroupfs_entry_t *en)
{
	if (refcount_dec_and_test(&en->refcnt)) {
		return;
	}
}

static cgroupfs_entry_t *cfs_subdir_first(cgroupfs_entry_t *dir)
{
	return rb_entry_safe(rb_first(&dir->subdir), cgroupfs_entry_t,
				subdir_node);
}

static cgroupfs_entry_t *cfs_subdir_next(cgroupfs_entry_t *dir)
{
	return rb_entry_safe(rb_next(&dir->subdir_node), cgroupfs_entry_t,
				subdir_node);
}

static int cfs_name_match(const char *name, cgroupfs_entry_t *en, unsigned int len)
{
	if (len < en->namelen)
		return -1;
	if (len > en->namelen)
		return 1;

	return memcmp(name, en->name, len);
}

static cgroupfs_entry_t *cpu_subdir_find(cgroupfs_entry_t *dir,
					const char *name,
					unsigned int len)
{
	struct rb_node *node = dir->subdir.rb_node;

	while (node) {
		cgroupfs_entry_t *en = rb_entry(node, cgroupfs_entry_t, subdir_node);
		int result = cfs_name_match(name, en, len);

		if (result < 0)
			node = node->rb_left;
		else if (result > 0)
			node = node->rb_right;
		else
			return en;
	}
	return NULL;
}

static bool cpu_subdir_insert(cgroupfs_entry_t *dir,
				cgroupfs_entry_t *en)
{
	struct rb_root *root = &dir->subdir;
	struct rb_node **new = &root->rb_node, *parent = NULL;

	write_lock(&cgroupfs_subdir_lock);
	while (*new) {
		cgroupfs_entry_t *this = rb_entry(*new, cgroupfs_entry_t, subdir_node);
		int result = cfs_name_match(en->name, this, en->namelen);

		parent = *new;
		if (result < 0)
			new = &(*new)->rb_left;
		else if (result > 0)
			new = &(*new)->rb_right;
		else {
			write_unlock(&cgroupfs_subdir_lock);
			return false;
		}
	}

	rb_link_node(&en->subdir_node, parent, new);
	rb_insert_color(&en->subdir_node, root);
	write_unlock(&cgroupfs_subdir_lock);
	cgroupfs_get_entry(dir);
	cgroufs_entry_num++;
	return true;
}

void cgroupfs_umount_remove_tree(cgroupfs_entry_t *root)
{
	cgroupfs_entry_t *en, *next;
	write_lock(&cgroupfs_subdir_lock);
	en = root;
	while (1) {
		write_unlock(&cgroupfs_subdir_lock);
		cond_resched();
		write_lock(&cgroupfs_subdir_lock);
		next = cfs_subdir_first(en);
		if (next) {
			if (S_ISREG(next->mode)) {
				rb_erase(&next->subdir_node, &en->subdir);
				cgroupfs_free_entry(next);
				continue;
			}
			else {
				en = next;
				continue;
			}
		}
		next = en->parent;
		rb_erase(&en->subdir_node, &next->subdir);
		cgroupfs_free_entry(en);

		if (en == root)
			break;
		en = next;
	}
	write_unlock(&cgroupfs_subdir_lock);
}

cgroupfs_entry_t *cgroupfs_alloc_private(const char *name, cgroupfs_entry_t *parent, int cgroupfs_type)
{
	cgroupfs_entry_t *p = kmalloc(sizeof(cgroupfs_entry_t), GFP_KERNEL);
	if (!p)
		return NULL;
	memset(p, 0, sizeof(cgroupfs_entry_t));
	p->namelen = strlen(name);
	p->name = kmalloc(p->namelen + 1, GFP_KERNEL);
	if (!p->name) {
		kfree(p);
		return NULL;
	}
	memcpy((void *)(p->name), (void *)name, p->namelen + 1);
	p->parent = parent;
	p->cgroupfs_type = cgroupfs_type;
	return p;
}

struct dentry *cgroupfs_iop_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	int cpu;
	cgroupfs_entry_t *sub, *parent = dir->i_private;
	struct inode *inode = NULL;
	int counted_cpu = -1, max_cpu = INT_MAX;
	read_lock(&cgroupfs_subdir_lock);
	sub = cpu_subdir_find(parent, dentry->d_name.name, dentry->d_name.len);
	if (!sub) {
		read_unlock(&cgroupfs_subdir_lock);
		return ERR_PTR(-ENOENT);
	}
	cgroupfs_get_entry(sub);
	read_unlock(&cgroupfs_subdir_lock);
	if (parent->cgroupfs_type == CGROUPFS_TYPE_CPUDIR && sub->cgroupfs_type == CGROUPFS_TYPE_CPUDIR) {
		cpu = sub->cpu;
		if (cgroupfs_cpu_dir_filter(cpu, &max_cpu, &counted_cpu, 1))
			goto out;
	}
	if (sub->cgroupfs_type == CGROUPFS_TYPE_CPUDIR) {
		d_set_d_op(dentry, sub->e_dops);
	}
	inode = cgroupfs_get_inode(sub);
out:
	cgroupfs_put_entry(sub);
	if (!inode)
		return ERR_PTR(-ENOENT);
	return d_splice_alias(inode, dentry);
}

const struct inode_operations cgroupfs_inode_operations = {
	.lookup = cgroupfs_iop_lookup,
};

int cgroupfs_readdir(struct file *file, struct dir_context *ctx)
{
	cgroupfs_entry_t *next;
	int i, cpu, skip;
	int counted_cpu = -1, max_cpu = INT_MAX, filter_cpu = 0;
	cgroupfs_entry_t *en;
	en = file_inode(file)->i_private;
	if (en->cgroupfs_type == CGROUPFS_TYPE_CPUDIR) {
		filter_cpu = 1;
	}

	if (!dir_emit_dots(file, ctx))
		return 0;

	i = ctx->pos - 2;
	read_lock(&cgroupfs_subdir_lock);
	en = cfs_subdir_first(en);
	for (;;) {
		if (!en) {
			read_unlock(&cgroupfs_subdir_lock);
			return 0;
		}
		if (!i)
			break;
		en = cfs_subdir_next(en);
		i--;
	}

	do {
		skip = 0;
		cgroupfs_get_entry(en);
		read_unlock(&cgroupfs_subdir_lock);
		if (filter_cpu && en->cgroupfs_type == CGROUPFS_TYPE_CPUDIR) {
			cpu = en->cpu;
			if (cgroupfs_cpu_dir_filter(cpu, &max_cpu, &counted_cpu, 0)) {
				skip = 1;
			}
		}
		if (!skip && !dir_emit(ctx, en->name, en->namelen,
			    en->inum, en->mode >> 12)) {
			cgroupfs_put_entry(en);
			return 0;
		}
		ctx->pos++;
		read_lock(&cgroupfs_subdir_lock);
		next = cfs_subdir_next(en);
		cgroupfs_put_entry(en);
		en = next;
	} while (en);

	read_unlock(&cgroupfs_subdir_lock);
	return 1;
}

static const struct file_operations cgroupfs_dir_operations = {
	.llseek		 = generic_file_llseek,
	.read		   = generic_read_dir,
	.iterate_shared	 = cgroupfs_readdir,
};

struct inode *cgroupfs_get_inode(cgroupfs_entry_t *en)
{
	struct inode *inode;
	inode = new_inode(en->sb);
	if (!inode)
		return NULL;
	inode->i_mode = en->mode;
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_op = en->e_iops;
	inode->i_fop = en->e_fops;
	inode->i_private = en;
	return inode;
}

static inline cgroupfs_entry_t *cgroupfs_new_entry(struct super_block *sb,
				const char *name, cgroupfs_entry_t *parent,
				int proc_type, umode_t mode)
{
	cgroupfs_entry_t *p;
	p = cgroupfs_alloc_private(name, parent, proc_type);
	if (!p)
		return NULL;
	p->sb = sb;
	p->e_iops = &cgroupfs_inode_operations;
	p->inum = get_next_ino();
	p->mode = mode;
	refcount_set(&p->refcnt, 1);
	if (S_ISDIR(mode)) {
		p->subdir = RB_ROOT;
		p->e_fops = &cgroupfs_dir_operations;
		if (proc_type == CGROUPFS_TYPE_CPUDIR)
			cgroupfs_set_sys_dops(p);
	}
	if (S_ISREG(mode) && proc_type <= CGROUPFS_TYPE_VMSTAT)
		cgroupfs_set_proc_fops(p);
	if (parent)
		cpu_subdir_insert(parent, p);
	return p;
}

// to be continued
static int cgroupfs_new_cpu_dir(struct super_block *sb, cgroupfs_entry_t *parent, umode_t mode)
{
	int cpu;
	char cpustr[20];
	cgroupfs_entry_t *dir;
	for_each_online_cpu(cpu) {
		sprintf(cpustr, "cpu%d", cpu);
		dir = cgroupfs_new_entry(sb, cpustr, parent, CGROUPFS_TYPE_CPUDIR, mode);
		if (!dir)
			return -1;
		dir->cpu = cpu;
	}
	return 0;
}


struct super_operations cgroupfs_super_operations = {
	.statfs  = simple_statfs,
	.drop_inode     = generic_delete_inode,
	.free_inode     = cgroupfs_free_inode,
};

static int cgroupfs_fill_root(struct super_block *s, unsigned long magic, cgroupfs_entry_t *en)
{
	struct inode *inode;
	struct dentry *root;

	s->s_blocksize = PAGE_SIZE;
	s->s_blocksize_bits = PAGE_SHIFT;
	s->s_magic = magic;
	s->s_op = &cgroupfs_super_operations;
	s->s_time_gran = 1;

	inode = new_inode(s);
	if (!inode)
		return -ENOMEM;
	inode->i_ino = en->inum;
	inode->i_mode = en->mode;
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_op = en->e_iops;
	inode->i_fop = en->e_fops;
	inode->i_private = en;
	set_nlink(inode, 2);
	root = d_make_root(inode);
	if (!root)
		return -ENOMEM;
	s->s_root = root;
	return 0;
}

static int cgroupfs_fill_super (struct super_block *sb, void *data, int silent)
{
	int err = -ENOMEM;
	cgroupfs_entry_t *proc, *sys, *entry, *cpu, *root_entry;
	umode_t f_mode = S_IFREG | 0644, d_mode = S_IFDIR | 0755;
	root_entry = cgroupfs_new_entry(sb, "/", NULL, CGROUPFS_TYPE_NORMAL_DIR, d_mode);
	if (!root_entry)
		return err;
	root_entry->parent = root_entry;
	cgroupfs_root = root_entry;
	if (cgroupfs_fill_root(sb, CGROUPFS_MAGIC, root_entry))
		return err;

	proc = cgroupfs_new_entry(sb, "proc", root_entry, CGROUPFS_TYPE_NORMAL_DIR, d_mode);
	if (!proc)
		return err;
	sys = cgroupfs_new_entry(sb, "sys", root_entry, CGROUPFS_TYPE_NORMAL_DIR, d_mode);
	if (!sys)
		return err;
	cgroupfs_new_entry(sb, "meminfo", proc, CGROUPFS_TYPE_MEMINFO, f_mode);
	cgroupfs_new_entry(sb, "cpuinfo", proc, CGROUPFS_TYPE_CPUINFO, f_mode);
	cgroupfs_new_entry(sb, "stat", proc, CGROUPFS_TYPE_STAT, f_mode);
	cgroupfs_new_entry(sb, "uptime", proc, CGROUPFS_TYPE_UPTIME, f_mode);
	cgroupfs_new_entry(sb, "loadavg", proc, CGROUPFS_TYPE_LOADAVG, f_mode);
	cgroupfs_new_entry(sb, "diskstats", proc, CGROUPFS_TYPE_DKSTATS, f_mode);
	cgroupfs_new_entry(sb, "vmstat", proc, CGROUPFS_TYPE_VMSTAT, f_mode);

	entry = cgroupfs_new_entry(sb, "devices", sys, CGROUPFS_TYPE_NORMAL_DIR, d_mode);
	if (!entry)
		return err;
	entry = cgroupfs_new_entry(sb, "system", entry, CGROUPFS_TYPE_NORMAL_DIR, d_mode);
	if (!entry)
		return err;
	cpu = cgroupfs_new_entry(sb, "cpu", entry, CGROUPFS_TYPE_CPUDIR, d_mode);
	if (!cpu)
		return err;
	cgroupfs_new_cpu_dir(sb, cpu, d_mode);
	return 0;
}

static struct dentry *cgroupfs_get_super(struct file_system_type *fst,
	int flags, const char *devname, void *data)
{
	return mount_single(fst, flags, data, cgroupfs_fill_super);
}

static void cgroupfs_kill_sb(struct super_block *sb)
{
	kill_anon_super(sb);
	cgroupfs_umount_remove_tree(cgroupfs_root);
}

static struct file_system_type cgroup_fs_type = {
	.owner   = THIS_MODULE,
	.name    = "cgroupfs",
	.mount   = cgroupfs_get_super,
	.kill_sb = cgroupfs_kill_sb,
};

int __init cgroupfs_init(void)
{
	printk("register cgroupfs\n");
	return register_filesystem(&cgroup_fs_type);
}

static void __exit cgroupfs_exit(void)
{
	unregister_filesystem(&cgroup_fs_type);
}

module_init(cgroupfs_init);
module_exit(cgroupfs_exit);
