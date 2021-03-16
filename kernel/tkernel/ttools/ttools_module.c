#include <linux/module.h>
#include <linux/errno.h>
#include <linux/percpu.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <generated/utsrelease.h>
#include <linux/kallsyms.h>
#include <linux/ptrace.h>
#include "ttools.h"

#define TTOOLS_MINOR		254
#define TTOOLS_VER		"2.0"


struct ttools_pid {
	struct list_head list;
	struct task_struct *task;
};

static LIST_HEAD(ttools_protected_pids);
static DEFINE_SPINLOCK(ttools_pids_lock);

static struct inode **p_anon_inode_inode;

static bool is_annon_inode(struct inode *inode)
{
	return inode == *p_anon_inode_inode;
}

static int ttools_ptrace_protect_task(struct task_struct *p_task)
{
	struct ttools_pid *p_item;
	bool exist = false;
	struct ttools_pid *pid_item = kmalloc(sizeof(struct ttools_pid), GFP_KERNEL);
	if (!pid_item)
		return -ENOMEM;

	spin_lock(&ttools_pids_lock);
	list_for_each_entry(p_item, &ttools_protected_pids, list) {
		if (p_item->task == p_task) {
			exist = true;
			break;
		}
	}
	if (!exist) {
		pid_item->task = p_task;
		list_add_tail(&pid_item->list, &ttools_protected_pids);
	}
	spin_unlock(&ttools_pids_lock);
	if (exist) {
		kfree(pid_item);
		return -EEXIST;
	}
	return 0;
}

static int ttools_ptrace_unprotect_task(struct task_struct *p_task)
{
	struct ttools_pid *p_item;

	spin_lock(&ttools_pids_lock);
	list_for_each_entry(p_item, &ttools_protected_pids, list) {
		if (p_item->task == p_task) {
			list_del(&p_item->list);
			kfree(p_item);
			break;
		}
	}
	spin_unlock(&ttools_pids_lock);
	return 0;
}

static bool ttools_task_ptrace_protected(struct task_struct *p_task)
{
	struct ttools_pid *p_item;
	bool ret = false;

	spin_lock(&ttools_pids_lock);
	list_for_each_entry(p_item, &ttools_protected_pids, list) {
		if (p_item->task == p_task) {
			ret = true;
			break;
		}
	}
	spin_unlock(&ttools_pids_lock);
	return ret;
}

static int ttools_ptrace_hook(long request, long pid, struct task_struct *task, long addr, long data)
{
	if (ttools_task_ptrace_protected(task->group_leader))
		return -EPERM;
	return 0;
}

static void ttools_clean_task_list(void)
{
	struct ttools_pid *p_item;
	struct ttools_pid *p_item2;

	spin_lock(&ttools_pids_lock);
	list_for_each_entry_safe(p_item, p_item2, &ttools_protected_pids, list) {
		list_del(&p_item->list);
		kfree(p_item);
	}
	spin_unlock(&ttools_pids_lock);
}

static int ttools_get_fd_refs_cnt(struct ttools_fd_ref *p_ref)
{
	struct dentry *dentry;
	long dentry_cnt, f_cnt;
	bool is_annon;
	struct fd f;
	f = fdget(p_ref->fd);
	if (!f.file)
		return -EBADF;
	is_annon = is_annon_inode(f.file->f_inode);
	f_cnt = atomic_long_read(&(f.file->f_count)) - (f.flags & FDPUT_FPUT);
	dentry = f.file->f_path.dentry;
	if (!dentry)
		dentry_cnt = 0;
	else
		dentry_cnt = dentry->d_lockref.count;
	fdput(f);
	if (is_annon || !dentry_cnt)
		p_ref->ref_cnt = f_cnt;
	else
		p_ref->ref_cnt = dentry_cnt;
	return 0;
}

static long ttools_dev_ioctl(struct file *filp,
		unsigned int ioctl, unsigned long arg)
{
	int ret = -EINVAL;
	void __user *argp = (void __user *)arg;
	struct ttools_fd_ref fd_ref;

	switch (ioctl) {
		case TTOOLS_PTRACE_PROTECT:
			ret = ttools_ptrace_protect_task(current->group_leader);
			break;
		case TTOOLS_PTRACE_UNPROTECT:
			ret = ttools_ptrace_unprotect_task(current->group_leader);
			break;
		case TTOOLS_GET_FD_REFS_CNT:
			if (copy_from_user(&fd_ref, argp, sizeof(struct ttools_fd_ref)))
				return -EFAULT;
			ret = ttools_get_fd_refs_cnt(&fd_ref);
			if (ret)
				return ret;
			if (copy_to_user(argp, &fd_ref, sizeof(struct ttools_fd_ref)))
				return -EFAULT;
			break;
	}
	return ret;
}

static struct file_operations ttools_chardev_ops = {
	.unlocked_ioctl = ttools_dev_ioctl,
	.compat_ioctl   = ttools_dev_ioctl,
	.llseek		= noop_llseek,
};

static struct miscdevice ttools_dev = {
	TTOOLS_MINOR,
	"ttools",
	&ttools_chardev_ops,
	.mode = 0666,
};


static void flush_icache_1(void *info)
{
	smp_mb();
#ifdef CONFIG_X86
	sync_core();
#endif
}

static int ttools_init(void)
{
	int ret;
	unsigned long addr;

	addr = kallsyms_lookup_name("anon_inode_inode");
	if (!addr)
		return -ENODEV;
	*((void**)&p_anon_inode_inode) = (void**)addr;

	ttools_chardev_ops.owner = THIS_MODULE;
	ret = misc_register(&ttools_dev);
	ptrace_pre_hook = ttools_ptrace_hook;
	smp_wmb();
	smp_call_function(flush_icache_1, NULL, 1);
	pr_info("ttools " TTOOLS_VER " loaded\n");
	return ret;
}

static void ttools_exit(void)
{
	ptrace_pre_hook = NULL;
	smp_wmb();
	smp_call_function(flush_icache_1, NULL, 1);
	misc_deregister(&ttools_dev);
	ttools_clean_task_list();
	pr_info("ttools " TTOOLS_VER " unloaded\n");
}

module_init(ttools_init);
module_exit(ttools_exit);
MODULE_DESCRIPTION("ttools " TTOOLS_VER " for "  UTS_RELEASE);
MODULE_LICENSE("GPL");
