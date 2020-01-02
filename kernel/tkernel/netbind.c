#include <linux/tkernel.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <net/sock.h>

unsigned char prot_sock_flag[PROT_SOCK];
EXPORT_SYMBOL(prot_sock_flag);


static int netbind_proc_show(struct seq_file *m, void *v)
{
	int i;
	for(i=1; i<1024; i++) {
		if(prot_sock_flag[i])
			seq_printf(m, "%d\n", i);
	}
	return 0;
}

static int netbind_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, netbind_proc_show, NULL);
}

static ssize_t netbind_proc_write(struct file *file, const char __user *buf,
		size_t length, loff_t *ppos)
{
	int port, en;
	char *buffer, *p;
	int err;

	if (!buf || length > PAGE_SIZE)
		return -EINVAL;

	buffer = (char *)__get_free_page(GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	err = -EFAULT;
	if (copy_from_user(buffer, buf, length))
		goto out;

	err = -EINVAL;
	if (length < PAGE_SIZE)
		buffer[length] = '\0';
	else if (buffer[PAGE_SIZE-1])
		goto out;

	en = 1;
	p = buffer;
	if(*p=='+') {
		p++;
	} else if(*p=='-') {
		en = 0;
		p++;
	}

	if(*p < '0' || *p > '9')
		goto out;

	port = simple_strtoul(p, &p, 0);
	if(*p != '\n' && *p != '\r' && *p != '\0')
		goto out;

	if(port <= 0 || port >= PROT_SOCK)
		goto out;

	prot_sock_flag[port] = en;
	err = length;

out:
	free_page((unsigned long)buffer);
	return err;
}

static const struct file_operations netbind_proc_ops = {
	.open           = netbind_proc_open,
	.read           = seq_read,
	.write          = netbind_proc_write,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int __init nonpriv_netbind_init(void) {
	proc_create("nonpriv_netbind", 0, proc_tkernel, &netbind_proc_ops);
	return 0;
}
late_initcall(nonpriv_netbind_init);
