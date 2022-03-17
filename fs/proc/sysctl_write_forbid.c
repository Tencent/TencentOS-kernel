#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/namei.h>

LIST_HEAD(sysctl_restrict_list);
DEFINE_SPINLOCK(sysctl_restrict_lock);

static int sysctl_restrict_show(struct seq_file *m, void *v)
{
	struct sysctl_restrict_record *sysctl_restrict_entry;

	rcu_read_lock();
	list_for_each_entry_rcu(sysctl_restrict_entry, &sysctl_restrict_list, list) {
		seq_printf(m, "%s", sysctl_restrict_entry->procname);
		seq_puts(m, "\n");
	}
	rcu_read_unlock();

	return 0;
}

static int sysctl_restrict_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, sysctl_restrict_show, NULL);
}

static ssize_t sysctl_restrict_write(struct file *file, const char __user *buf, 
                                     size_t len, loff_t *ppos)
{
	int ret;
	int length;
	struct path path;
	char *tmp_procname;
	struct sysctl_restrict_record *sysctl_restrict_entry;
	struct sysctl_restrict_record *new_restrict_entry;

	tmp_procname = kzalloc(PATH_MAX, GFP_KERNEL);
	if (!tmp_procname) {
		ret = -ENOMEM;
		return ret;
	}
	
	length = strncpy_from_user(tmp_procname, buf, len);
	if (tmp_procname[len - 1] != '\n') {
		ret = -EINVAL;
		goto out;  
	} else {
		/* convert last character from '\n' to '\0' */
		tmp_procname[len - 1] = '\0';
	}

	if ((tmp_procname[0] == 'c') && (tmp_procname[1] == '\0')) {
		spin_lock(&sysctl_restrict_lock);
		list_for_each_entry(sysctl_restrict_entry, &sysctl_restrict_list, list) {
			list_del_rcu(&sysctl_restrict_entry->list);
			kfree_rcu(sysctl_restrict_entry, rcu);
		}
		spin_unlock(&sysctl_restrict_lock);
		ret = length;
		goto out;
	} else if (tmp_procname[0] == '+') {
		rcu_read_lock();
		list_for_each_entry_rcu(sysctl_restrict_entry, &sysctl_restrict_list, list) {
			/* already exist in restrict list */
			if (strncmp(sysctl_restrict_entry->procname, tmp_procname + 1, PATH_MAX) == 0) {
				rcu_read_unlock();
				ret = -EEXIST;
				goto out;
			}
		}
		rcu_read_unlock();

		/* not a valid path for a sysctl parameter */ 
		ret = kern_path(tmp_procname + 1, 0, &path);
		if (ret != 0) {
			goto out;
		}

		/* should not be a directory */
		if (S_ISDIR(path.dentry->d_inode->i_mode)) {
			path_put(&path);
			ret = -EISDIR;
			goto out;
		}

		path_put(&path);

		/* create a new entry for a valid sysctl path */
		new_restrict_entry = kzalloc(sizeof(struct sysctl_restrict_record), GFP_KERNEL);
		if (!new_restrict_entry) {
			ret = -ENOMEM;
			goto out;
		}
		strncpy(new_restrict_entry->procname, tmp_procname + 1, len - 1);

		spin_lock(&sysctl_restrict_lock);
		list_add_tail_rcu(&new_restrict_entry->list, &sysctl_restrict_list);
		spin_unlock(&sysctl_restrict_lock);
		
		ret = length;
		goto out;
	} else if (tmp_procname[0] == '-') {
		spin_lock(&sysctl_restrict_lock);
		list_for_each_entry(sysctl_restrict_entry, &sysctl_restrict_list, list) {
			if (strncmp(sysctl_restrict_entry->procname, tmp_procname + 1, PATH_MAX) == 0) {
				list_del_rcu(&sysctl_restrict_entry->list);
				spin_unlock(&sysctl_restrict_lock);
				kfree_rcu(sysctl_restrict_entry, rcu);
				ret = length;
				goto out;
			}
		}
		spin_unlock(&sysctl_restrict_lock);
	}

	ret = -EINVAL;
out:
	kfree(tmp_procname);
	return ret;
}

static const struct file_operations sysctl_restrict_file_ops = {
	.open    = sysctl_restrict_open,
	.read    = seq_read,
	.write	 = sysctl_restrict_write,
	.llseek  = seq_lseek,
	.release = seq_release
};

static int __init sysctl_restrict_init(void)
{
	proc_create("sysctl_write_forbid", 0644, NULL, &sysctl_restrict_file_ops);

	return 0;
}
fs_initcall(sysctl_restrict_init);
