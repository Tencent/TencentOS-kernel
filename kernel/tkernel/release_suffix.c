#include <linux/tkernel.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define RELEASE_SUFFIX_MAX_LENS 16
int release_suffix = 0;

static int release_suffix_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", release_suffix);
	return 0;
}

static int release_suffix_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, release_suffix_proc_show, NULL);
}

static ssize_t release_suffix_proc_write(struct file *file, const char __user *ubuf,
		size_t cnt, loff_t *ppos)
{
	char buf[8] = {0};

	if (cnt != 2){
		return -EINVAL;
	}

	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;
	
	if (*buf == '0'){
		release_suffix = 0;
	}else if(*buf == '1'){
		release_suffix = 1;
	}else{
		return -EINVAL;
	}

	return cnt;

}

static const struct file_operations release_suffix_proc_fops = {
	.open		= release_suffix_proc_open,
	.read		= seq_read,
	.write		= release_suffix_proc_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_release_suffix_init(void)
{
	proc_create("release_suffix", 0, proc_tkernel, &release_suffix_proc_fops);
	return 0;
}

late_initcall(proc_release_suffix_init);
