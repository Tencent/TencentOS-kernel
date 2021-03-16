#include <linux/tkernel.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/bitops.h>
#include <linux/shield_mounts.h>

#define SHIELD_PATH_MAX 1024

struct mount_pair {
	struct list_head list;
	char dev_name[SHIELD_PATH_MAX];
	char mnt_path[SHIELD_PATH_MAX];
};

static DEFINE_RWLOCK(shield_mounts_lock);
static LIST_HEAD(shield_mounts_list);
static unsigned int shield_mounts_count;
unsigned int sysctl_shield_mounts_max = 512;

bool is_mount_shielded(const char *dev_path, const char *mount_path)
{
	bool ret = false;
	struct mount_pair *p;

	read_lock(&shield_mounts_lock);

	list_for_each_entry(p, &shield_mounts_list, list) {
		if (!strcmp(p->dev_name, dev_path) &&
				!strcmp(p->mnt_path, mount_path)) {
			ret = true;
			break;
		}
	}

	read_unlock(&shield_mounts_lock);
	return ret;
}

static int shield_mounts_search_and_insert(struct mount_pair *item)
{
	int ret = -EEXIST;
	struct mount_pair *p;

	write_lock(&shield_mounts_lock);

	list_for_each_entry(p, &shield_mounts_list, list) {
		if (!strcmp(p->dev_name, item->dev_name) &&
				!strcmp(p->mnt_path, item->mnt_path))
			goto unlock;
	}

	if (shield_mounts_count >= sysctl_shield_mounts_max) {
		ret = -ENOMEM;
		goto unlock;
	}

	list_add_tail(&item->list, &shield_mounts_list);
	shield_mounts_count++;
	ret = 0;

unlock:
	write_unlock(&shield_mounts_lock);
	return ret;
}

static int shield_mounts_search_and_del(struct mount_pair *item)
{
	struct mount_pair *p;

	write_lock(&shield_mounts_lock);

	list_for_each_entry(p, &shield_mounts_list, list) {
		if (!strcmp(p->dev_name, item->dev_name) &&
				!strcmp(p->mnt_path, item->mnt_path)) {
			list_del(&p->list);
			kfree(p);
			shield_mounts_count--;
			goto unlock;
		}
	}

unlock:
	write_unlock(&shield_mounts_lock);
	return 0;
	
}
/*helper function*/
static void str_escape(char *s, const char *esc)
{
    char *p = s;
    while (p && *p != '\0') {
        char c = *p++;
        while (c != '\0' && strchr(esc, c))
        	c = *p++;
        *s++ = c;
    }
    *s = '\0';
}

	
/*
 * parse buffer to get shield mount info
 * "set devpath mountpoint": means to add a new shield mount info
 * "clear devpath mountpoint": means to delete an exist one
 * */
static int shield_mounts_parse(char *buf, bool *is_set, struct mount_pair *item)
{
	char *token;

	str_escape(buf, "\t\n");
	buf = skip_spaces(buf);
	token = strsep(&buf, " ");
	if (!token || !*token || !buf) 
		goto error;

	if (!strcmp(token, "set")) {
		/* set */
		*is_set = true;
	} else if (!strcmp(token, "clear")) {
		/* clear */
		*is_set = false;
	} else {
		printk(KERN_ERR"set parse error\n");
		goto error;
	}

	buf = skip_spaces(buf);
	/* dev path */
	token = strsep(&buf, " ");
	if (!buf || !token || !*token || strlen(token) > (PATH_MAX-1)) {
		printk(KERN_ERR"dev path faild\n");
	    goto error;
	}
	memcpy(item->dev_name, token, strlen(token)+1);

	/* mnt */
	buf = strim(buf);
	memcpy(item->mnt_path, buf, strlen(buf)+1);

	return 0;
error:
	printk(KERN_ERR"Failed to parse shield mounts pair\n");
	return -EFAULT;
}

static int shield_mounts_proc_show(struct seq_file *m, void *v)
{
	struct mount_pair *p;

	read_lock(&shield_mounts_lock);

	list_for_each_entry(p, &shield_mounts_list, list) {
		seq_printf(m, "%s on %s\n", p->dev_name, p->mnt_path);
	}

	read_unlock(&shield_mounts_lock);
	return 0;
}

static int shield_mounts_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, shield_mounts_proc_show, NULL);
}


static ssize_t shield_mounts_proc_write(struct file *file, const char __user *ubuf,
		size_t cnt, loff_t *ppos)
{
	char *buffer = NULL;
	int ret = cnt;
	int order = 1;
	bool is_set;
	struct mount_pair *item;

	/*max file lens for 8k*/
	if (!ubuf || cnt > PAGE_SIZE * (1 << order))
		return -EINVAL;

	buffer = (char *)__get_free_pages(GFP_KERNEL, order);
	if (!buffer)
		return -ENOMEM;	

	if (copy_from_user(buffer, ubuf, cnt)) {
		ret = -EFAULT;
		goto out;
	}

	buffer[cnt] = 0;
	
	item = kmalloc(sizeof(struct mount_pair), GFP_KERNEL);
	if (!item){
		printk(KERN_ERR"Failed to malloc mount_pair\n");
		ret = -ENOMEM;
		goto out;
	}

	/*parse buffer*/
	ret = shield_mounts_parse(buffer, &is_set, item);
	if (ret)
		goto out1;

	if(is_set) {
		/*set */
		ret = shield_mounts_search_and_insert(item);
		if (!ret)
			item = NULL;
	} else {
		/* clear */
		ret = shield_mounts_search_and_del(item);
	}
	
	if (!ret)
		ret = cnt; 
out1: 
	kfree(item);
out:
	free_pages((unsigned long) buffer, order);
	return ret;
}


int shield_mounts_release(struct inode *inode, struct file *file)
{
	return single_release(inode, file);
}
static const struct file_operations shield_mounts_proc_fops = {
	.open		= shield_mounts_proc_open,
	.read		= seq_read,
	.write		= shield_mounts_proc_write,
	.llseek		= seq_lseek,
	.release	= shield_mounts_release,
};

static int __init proc_shield_mounts_init(void)
{
	proc_create("shield_mounts", 0, proc_tkernel, &shield_mounts_proc_fops);
	return 0;
}

late_initcall(proc_shield_mounts_init);
