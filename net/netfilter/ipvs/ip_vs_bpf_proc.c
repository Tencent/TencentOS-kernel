/*
 * IPVS         An implementation of the IP virtual server support for the
 *              LINUX operating system.  IPVS is now implemented as a module
 *              over the Netfilter framework. IPVS can be used to build a
 *              high-performance and highly available server based on a
 *              cluster of servers.
 *
 * Authors:     Jianming Fan <jianmingfan@tencent.com>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * The IPVS code for kernel 2.2 was done by Wensong Zhang and Peter Kese,
 * with changes/fixes from Julian Anastasov, Lars Marowsky-Bree, Horms
 * and others. Many code here is taken from IP MASQ code of kernel 2.2.
 *
 * Changes:
 *
 */

#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/export.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/inet.h>
#include <linux/socket.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/fdtable.h>
#include <linux/sched/task.h>
#include <linux/file.h>
#include <linux/bpf.h>
#include <linux/rcupdate.h>
#include <linux/filter.h>
#include <net/net_namespace.h>
#include <net/ip_vs.h>
#include <linux/mutex.h>
/*  since map id is defined as static in kernel/bpf/syscall.c
 *  pass pid and bpf map fd from controller to ipvs.
 *  The controller shall open the map and pass its pid
 *  and map fd to ipvs.
 */
static int bpf_map_pid;
static unsigned int bpf_map_fd;
static unsigned int bpf_prog_fd1;
static unsigned int bpf_prog_fd2;
static struct bpf_prog *g_prog1;
static struct bpf_prog *g_prog2;
__be32 major_nic_ip;
static int ip_local_port_begin;
static int ip_local_port_end;
struct bpf_map  *conntrack_map;
bool no_route_to_host_fix;
struct ip_vs_iter_state {
	struct seq_net_private	p;
	struct hlist_head	*l;
};

/* make sure only one process write the interface */
static DEFINE_MUTEX(ip_vs_bpf_proc_lock);
/* return the POS - 1 th item in the list */
static void *ip_vs_svc_array(struct seq_file *seq, loff_t pos)
{
	int idx;
	struct ip_vs_service *cp;
	struct ip_vs_iter_state *iter = seq->private;

	for (idx = 0; idx < IP_VS_SVC_TAB_SIZE; idx++) {
		hlist_for_each_entry_rcu(cp, &ip_vs_svc_table[idx], s_list) {
			/* __ip_vs_conn_get() is not needed by
			 * ip_vs_conn_seq_show and ip_vs_conn_sync_seq_show
			 */
			if (pos-- == 0) {
				iter->l = &ip_vs_svc_table[idx];
				return cp;
			}
		}
		cond_resched_rcu();
	}

	return NULL;
}

static void *ip_vs_svc_seq_start(struct seq_file *seq, loff_t *pos)
__acquires(RCU)
{
	struct ip_vs_iter_state *iter = seq->private;

	iter->l = NULL;
	rcu_read_lock();
	return *pos ? ip_vs_svc_array(seq, *pos - 1) : SEQ_START_TOKEN;
}

static void *ip_vs_svc_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct ip_vs_service *cp = v;
	struct ip_vs_iter_state *iter = seq->private;
	struct hlist_node *e;
	struct hlist_head *l = iter->l;
	int idx;

	++*pos;
	if (v == SEQ_START_TOKEN)
		return ip_vs_svc_array(seq, 0);

	/* more on same hash chain? */
	e = rcu_dereference(hlist_next_rcu(&cp->s_list));
	if (e)
		return hlist_entry(e, struct ip_vs_service, s_list);

	idx = l - ip_vs_svc_table;
	while (++idx < IP_VS_SVC_TAB_SIZE) {
		hlist_for_each_entry_rcu(cp, &ip_vs_svc_table[idx], s_list) {
			iter->l = &ip_vs_svc_table[idx];
			return cp;
		}
		cond_resched_rcu();
	}
	iter->l = NULL;
	return NULL;
}

static void ip_vs_svc_seq_stop(struct seq_file *seq, void *v)
__releases(RCU)
{
	rcu_read_unlock();
}

static int ip_vs_svc_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "Protocol  Vip     Vport  mode\n");
	} else {
		const struct ip_vs_service *cp = v;
		struct net *net = seq_file_net(seq);

		if (!net_eq(cp->ipvs->net, net))
			return 0;

		seq_printf(seq, "%-8s  %08X  %04X  %d\n",
			   ip_vs_proto_name(cp->protocol),
			   ntohl(cp->addr.ip),  ntohs(cp->port),
			   cp->skip_bpf);
	}
	return 0;
}

static const struct seq_operations ip_vs_svc_seq_ops = {
	.start = ip_vs_svc_seq_start,
	.next  = ip_vs_svc_seq_next,
	.stop  = ip_vs_svc_seq_stop,
	.show  = ip_vs_svc_seq_show,
};

static int ip_vs_svc_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &ip_vs_svc_seq_ops,
			sizeof(struct ip_vs_iter_state));
}

/*  shall return count to make userspace happy.
 *  don't assume user string will end with \0.
 */
extern struct ip_vs_service *
__ip_vs_service_find(struct netns_ipvs *ipvs, int af, __u16 protocol,
		     const union nf_inet_addr *vaddr, __be16 vport);

/* Register the vip as clusterip.
 * format:
 *    echo 10.10.10.1:80:tcp > thisfile
 */
static ssize_t ip_vs_svc_write(struct file *file,
			       const char __user *ubuf,
			       size_t count,
			       loff_t *ppos)
{
	char ids[3][20];
	char buf[100];
	char *s = buf;
	char *token;
	__be32 ip;
	__be16 port;
	u16 proto;
	const char delim[2] = ":";
	struct ip_vs_service *svc;
	int i = 0;

	memset(ids, 0, sizeof(ids));
	memset(buf, 0, sizeof(buf));
	if (*ppos > 0) {
		pr_err("%s %d\n", __func__, __LINE__);
		return -EFAULT;
	}

	if (copy_from_user(buf, ubuf, count)) {
		pr_err("%s %d\n", __func__, __LINE__);
		return -EFAULT;
	}

	while ((token = strsep(&s, delim)) != NULL) {
		strncpy(ids[i], token, 20);
		i++;
	}

	if (i != 3) {
		pr_err("%s input fields error %d\n", __func__, i);
		return -EINVAL;
	}
	if (in4_pton(ids[0], -1, (u8 *)&ip, -1, NULL) != 1) {
		pr_err("invalid ip addr\n");
		return -EINVAL;
	}
	if (kstrtou16(ids[1], 0, &port) != 0) {
		pr_err("%s %d\n", __FILE__, __LINE__);
		return -EINVAL;
	}
	port = htons(port);

	if (strncmp(ids[2], "tcp", 3) == 0) {
		proto = IPPROTO_TCP;
	} else if (strncmp(ids[2], "udp", 3) == 0) {
		proto = IPPROTO_UDP;
	} else {
		pr_err("invalid proto\n");
		return -EINVAL;
	}

	rcu_read_lock();
	svc = __ip_vs_service_find(net_ipvs(current->nsproxy->net_ns),
				   AF_INET, proto, (union nf_inet_addr *)&(ip),
				   port);
	rcu_read_unlock();
	if (!svc) {
		pr_err("%s service not exist ip 0x%x port %d proto %d\n",
		       __func__, ip, port, proto);
		return -ENOENT;
	}
	svc->skip_bpf = 1;
	return count;
}

/* The code is mostly borrowed from bpf_task_fd_query
 *
 * Note XXX: Since bpf_map_inc/bpf_map_put is not exported,
 * the follow code can't release bpf map memory.
 * The controller shall inform ipvs before delete the bpf map or
 * else the bpf map will leak.
 */
static inline void bpf_map_inc2(struct bpf_map *map)
{
	atomic_inc(&map->refcnt);
}

static int bpf_prog_get_pid(int pid,
				   unsigned int fd,
				   unsigned long long addr,
				   struct bpf_prog **prog)
{
	struct files_struct *files;
	struct task_struct *task;
	struct file *file;
	int err = 0;

	task = get_pid_task(find_vpid(pid), PIDTYPE_PID);
	if (!task)
		return -ENOENT;

	/* get_files_struct is not exported! hard code it */
	task_lock(task);
	files = task->files;
	task_unlock(task);

	put_task_struct(task);
	if (!files)
		return -ENOENT;

	err = 0;
	spin_lock(&files->file_lock);
	file = fcheck_files(files, fd);
	if (!file)
		err = -EBADF;
	else
		get_file(file);
	spin_unlock(&files->file_lock);
	if (err)
		goto out;

	if (file->f_op != (const struct file_operations *)addr) {
		err = -EINVAL;
		goto out_fput;
	}

	*prog = file->private_data;
	if (*prog)
		bpf_prog_inc(*prog);
out_fput:
	fput(file);
out:
	return err;
}

static int bpf_conntrack_map_get(int pid,
				 unsigned int fd,
				 unsigned long long addr,
				 struct bpf_map **map)
{
	struct files_struct *files;
	struct task_struct *task;
	struct file *file;
	int err = 0;

	task = get_pid_task(find_vpid(pid), PIDTYPE_PID);

	if (!task)
		return -ENOENT;

	/* get_files_struct is not exported! hard code it */
	task_lock(task);
	files = task->files;
	task_unlock(task);

	put_task_struct(task);
	if (!files)
		return -ENOENT;

	err = 0;
	spin_lock(&files->file_lock);
	file = fcheck_files(files, fd);
	if (!file)
		err = -EBADF;
	else
		get_file(file);
	spin_unlock(&files->file_lock);
	if (err)
		goto out;

	if (file->f_op != (const struct file_operations *)addr) {
		err = -EINVAL;
		goto out_fput;
	}

	*map = file->private_data;
	if (*map)
		bpf_map_inc2(*map);
out_fput:
	fput(file);
out:
	return err;
}

/* only rmmod calls me, no traffic now! */
int ip_vs_bpf_put(void)
{
	if (g_prog1)
		bpf_prog_put(g_prog1);
	if (g_prog2)
		bpf_prog_put(g_prog2);
	/* prog free using call_rcu, wait before map free to avoid panic */
	synchronize_rcu();
	if (conntrack_map)
		(*(resolve_addrs.bpf_map_put))(conntrack_map);
	return 0;
}

/* Note the error info can't be more than 4000
 * or else truncated!
 */


static ssize_t ip_vs_bpf_read(struct file *file,
			      char __user *ubuf,
			      size_t count,
			      loff_t *ppos)
{
	int len = 0;
	char buf[100];

	memset(buf, 0, sizeof(buf));
	if (*ppos > 0) {
		/* return 0 to sigal eof to user */
		return 0;
	}

	/* snprintf need tail to save null bytes */
	len = snprintf(buf, sizeof(buf), "%d:%u:%u:%u\n",
		       bpf_map_pid, bpf_map_fd,
		       bpf_prog_fd1, bpf_prog_fd2);

	if (copy_to_user(ubuf, buf, len))
		return -EFAULT;

	/* so next time ppos will be bigger than 0 and return 0 */
	*ppos = len;
	return  len;
}

/* Hint: ubuf format likes 1893:pid:mapid:progid1:progid2 */
static ssize_t ip_vs_bpf_write(struct file *file,
			       const char __user *ubuf,
			       size_t count,
			       loff_t *ppos)
{
	int err = 0;
	struct bpf_map *map = NULL;
	struct bpf_prog *prog1 = NULL;
	struct bpf_prog *prog2 = NULL;
	const char delim[2] = ":";
	char ids[5][20];
	char *token;
	int tag, pid;
	unsigned int mapid, progid1, progid2;
	char buf[100];
	int i = 0;
	char *s = buf;

	memset(ids, 0, sizeof(ids));
	memset(buf, 0, sizeof(buf));

	if (*ppos > 0) {
		pr_err("%s %d\n", __func__, __LINE__);
		return -EFAULT;
	}

	/* singleton:conntrack_map is assigned once,
	 * and be nulled in module exit
	 */
	if (conntrack_map) {
		pr_err("%s %d conntrack_map exists\n",
		       __func__, __LINE__);
		return -EEXIST;
	}

	if (copy_from_user(buf, ubuf, count)) {
		pr_err("%s %d\n", __FILE__, __LINE__);
		return -EFAULT;
	}
	while ((token = strsep(&s, delim)) != NULL) {
		strncpy(ids[i], token, 20);
		i++;
	}

	if (i != 5) {
		pr_err("input fields %d\n", i);
		return -EINVAL;
	}
	pr_info("%s %s %s %s %s %s\n", __func__,
		ids[0], ids[1], ids[2], ids[3], ids[4]);
	if (kstrtoint(ids[0], 0, &tag) != 0) {
		pr_err("%s %d\n", __FILE__, __LINE__);
		return -EINVAL;
	}
	if (kstrtoint(ids[1], 0, &pid) != 0) {
		pr_err("%s %d\n", __FILE__, __LINE__);
		return -EINVAL;
	}
	if (kstrtouint(ids[2], 0, &mapid) != 0) {
		pr_err("%s %d\n", __FILE__, __LINE__);
		return -EINVAL;
	}
	if (kstrtouint(ids[3], 0, &progid1) != 0) {
		pr_err("%s %d\n", __FILE__, __LINE__);
		return -EINVAL;
	}
	if (kstrtouint(ids[4], 0, &progid2) != 0) {
		pr_err("%s %d\n", __FILE__, __LINE__);
		return -EINVAL;
	}

	pr_info("%s %d %d %d %d %d\n",
		__func__, tag, pid, mapid, progid1, progid2);
	if (tag != 1893) {
		pr_err("%s %d\n", __FILE__, __LINE__);
		return -EINVAL;
	}

	err = bpf_conntrack_map_get(pid, mapid,
				    (long long)(resolve_addrs.bpf_map_fops),
				    &map);
	if (err != 0 || !map) {
		pr_err("%s acquire bpf_map failed\n", __func__);
		return -EINVAL;
	}
	err = bpf_prog_get_pid(pid, progid1,
			       (long long)(resolve_addrs.bpf_prog_fops),
			       &prog1);
	if (err != 0 || !prog1) {
		pr_err("%s acquire bpf_prog failed\n", __func__);
		return -EINVAL;
	}
	err = bpf_prog_get_pid(pid, progid2,
			       (long long)(resolve_addrs.bpf_prog_fops),
			       &prog2);
	if (err != 0 || !prog2) {
		pr_err("%s acquire bpf_prog failed\n", __func__);
		return -EINVAL;
	}
	/* for read only */
	bpf_map_pid = pid;
	bpf_map_fd = mapid;
	bpf_prog_fd1 = progid1;
	bpf_prog_fd2 = progid2;
	conntrack_map = map;
	g_prog1 = prog1;
	g_prog2 = prog2;
	pr_info("%s pid %u  mapfd %u progfd %u %u\n",
		__func__, pid, mapid, progid1, progid2);
	return count;
}

static ssize_t ip_vs_devip_read(struct file *file,
				char __user *ubuf,
				size_t count,
				loff_t *ppos)
{
	int len = 0;
	char buf[100];

	memset(buf, 0, sizeof(buf));
	if (*ppos > 0) {
		/* return 0 to sigal eof to user */
		return 0;
	}
	/* snprintf need tail to save null bytes */
	len = snprintf(buf, sizeof(buf), "0x%x\n", major_nic_ip);
	if (copy_to_user(ubuf, buf, len))
		return -EFAULT;
	/* so next time ppos will be bigger than 0 and return 0 */
	*ppos = len;
	return len;
}

/*  interface e.g
 *  echo 10.10.10.1 > thisfile for the vm's nic
 *
 *  only one eth nic is allowed to attached the bpf.
 *  kube-restart shall reuse the same argument!
 *  or else it shall rmmod ipvs-bpf before write this.
 */
static ssize_t ip_vs_devip_write(struct file *file,
				 const char __user *ubuf,
				 size_t count,
				 loff_t *ppos)
{
	char buf[100];
	__be32 ip;

	memset(buf, 0, sizeof(buf));
	if (*ppos > 0) {
		pr_err("%s %d\n", __func__, __LINE__);
		return -EFAULT;
	}
	if (copy_from_user(buf, ubuf, count)) {
		pr_err("%s %d\n", __FILE__, __LINE__);
		return -EFAULT;
	}
	if (in4_pton(buf, -1, (u8 *)&ip, -1, NULL) != 1) {
		pr_err("%s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	/* for kube-restart case, the arg shall be the same or else
	 * error
	 */
	if (major_nic_ip != 0 && major_nic_ip != ip) {
		pr_err("%s %d\n", __func__, __LINE__);
		return -EEXIST;
	}
	/* kube start first time after ipvs.ko in */
	if (major_nic_ip == 0)
		major_nic_ip = ip;

	return count;
}

void ip_vs_get_local_port_range(int *low, int *high)
{
	*low = ip_local_port_begin;
	*high = ip_local_port_end;
}

static ssize_t ip_vs_port_read(struct file *file,
			       char __user *ubuf,
			       size_t count,
			       loff_t *ppos)
{
	int len = 0;
	char buf[100];

	memset(buf, 0, sizeof(buf));

	if (*ppos > 0)
		/* return 0 to sigal eof to user */
		return 0;

	/* snprintf need tail to save null bytes */
	len = snprintf(buf, sizeof(buf), "%d:%d\n",
		       ip_local_port_begin, ip_local_port_end);
	if (copy_to_user(ubuf, buf, len))
		return -EFAULT;

	/* so next time ppos will be bigger than 0 and return 0 */
	*ppos = len;
	return len;
}

/* lowport:highport */
static ssize_t ip_vs_port_write(struct file *file,
				const char __user *ubuf,
				size_t count,
				loff_t *ppos)
{
	const char delim[2] = ":";
	char buf[100];
	int i = 0;
	char *s = buf;
	char ports[2][20];
	static int sigleton;
	int begin, end;
	char *token;

	memset(ports, 0, sizeof(ports));
	memset(buf, 0, sizeof(buf));

	if (*ppos > 0) {
		pr_err("%s %d\n", __func__, __LINE__);
		return -EFAULT;
	}

	if (copy_from_user(buf, ubuf, count)) {
		pr_err("%s %d\n", __FILE__, __LINE__);
		return -EFAULT;
	}
	while ((token = strsep(&s, delim)) != NULL) {
		strncpy(ports[i], token, 20);
		i++;
	}

	if (i != 2) {
		pr_err("input fields %d\n", i);
		return -EINVAL;
	}
	if (kstrtoint(ports[0], 0, &begin) != 0) {
		pr_err("%s %d\n", __FILE__, __LINE__);
		return -EINVAL;
	}
	if (kstrtoint(ports[1], 0, &end) != 0) {
		pr_err("%s %d\n", __FILE__, __LINE__);
		return -EINVAL;
	}

	if (begin < 1024 || begin >= end)
		return -EINVAL;

	/* modify the port shall restart the module */
	if (sigleton != 0 &&
	    (ip_local_port_begin != begin ||
	     ip_local_port_end != end)) {
		pr_err("ipvs: modify the port range shall restart the module");
		return -EEXIST;
	}

	sigleton = 1;
	ip_local_port_begin = begin;
	ip_local_port_end = end;
	return count;
}

static ssize_t ip_vs_bpf_fix_write(struct file *file,
				   const char __user *ubuf,
				   size_t count,
				   loff_t *ppos)
{
	char buf[10];
	int fix;

	memset(buf, 0, sizeof(buf));
	if (*ppos > 0) {
		pr_err("%s %d\n", __func__, __LINE__);
		return -EFAULT;
	}
	if (copy_from_user(buf, ubuf, count)) {
		pr_err("%s %d\n", __FILE__, __LINE__);
		return -EFAULT;
	}
	if (kstrtoint(buf, 0, &fix) != 0) {
		pr_err("%s %d\n", __FILE__, __LINE__);
		return -EINVAL;
	}
	if (fix != 1 && fix != 0) {
		pr_err("%s %d\n", __FILE__, __LINE__);
		return -EINVAL;
	}

	no_route_to_host_fix = fix;
	return count;
}

static ssize_t ip_vs_bpf_fix_read(struct file *file,
				  char __user *ubuf,
				  size_t count,
				  loff_t *ppos)
{
	int len = 0;
	char buf[10];

	memset(buf, 0, sizeof(buf));
	if (*ppos > 0) {
		/* return 0 to sigal eof to user */
		return 0;
	}
	/* snprintf need tail to save null bytes */
	len = snprintf(buf, sizeof(buf), "0x%d\n", no_route_to_host_fix);
	if (copy_to_user(ubuf, buf, len))
		return -EFAULT;
	/* so next time ppos will be bigger than 0 and return 0 */
	*ppos = len;
	return len;
}

static const struct file_operations ip_vs_svc_fops = {
	.owner	 = THIS_MODULE,
	.open    = ip_vs_svc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_net,
	.write =  ip_vs_svc_write,
};

static const struct file_operations ip_vs_bpf_fops = {
	.owner	 = THIS_MODULE,
	.read    = ip_vs_bpf_read,
	.write =  ip_vs_bpf_write,
};

static const struct file_operations ip_vs_devip_fops = {
	.owner	 = THIS_MODULE,
	.read    = ip_vs_devip_read,
	.write =  ip_vs_devip_write,
};

static const struct file_operations ip_vs_port_fops = {
	.owner	 = THIS_MODULE,
	.read    = ip_vs_port_read,
	.write =  ip_vs_port_write,
};

static const struct file_operations ip_vs_bpf_fix_fops = {
	.owner	 = THIS_MODULE,
	.read    = ip_vs_bpf_fix_read,
	.write   = ip_vs_bpf_fix_write,
};

struct ip_vs_err_map {
	int errno;
	const char *name;
};

static const struct ip_vs_err_map bpf_err_list[] = {
	{BPF_UNLINK_LOOKUP, "BpfunlinkLookup"},
	{BPF_UNLINK_DEL, "BpfunlinkDel"},
	{BPF_UNLINK_DEL2, "BpfunlinkDel2"},
	{BPF_UNLINK_MAP_NULL, "BpfunlinkMapNull"},
	{BPF_NEW_SPORT, "BpfSport"},
	{BPF_NEW_INSERT, "BpfInsert"},
	{BPF_NEW_RACE, "BpfRace"},
	{BPF_NEW_RACE_RETRY, "BpfRaceRetry"},
	{BPF_XMIT_LOCAL_RS, "BpfXmitLocalRs"},
	{BPF_UT_TEST, "BpfUT"},
	{BPF_UT_TEST2, "BpfUT2"},
	{0, NULL},
};

static unsigned long bpf_get_cpu_field(void __percpu *bpf_stat,
				       int cpu, int offt)
{
	return  *(((unsigned long *)per_cpu_ptr(bpf_stat, cpu)) + offt);
}

static unsigned long bpf_fold_field(void __percpu *bpf_stat, int offt)
{
	unsigned long res = 0;
	int i;

	for_each_possible_cpu(i)
		res += bpf_get_cpu_field(bpf_stat, i, offt);
	return res;
}

static int ip_vs_bpf_stat_show(struct seq_file *seq, void *v)
{
	int i;
	struct net *net = seq->private;
	struct netns_ipvs *ipvs = net->ipvs;

	for (i = 0; bpf_err_list[i].name; i++) {
		seq_printf(seq, "%s: %lu\n", bpf_err_list[i].name,
			   bpf_fold_field(ipvs->bpf_stat,
					  bpf_err_list[i].errno));
	}
	return 0;
}

static int ip_vs_bpf_stat_open(struct inode *inode, struct file *file)
{
	return single_open_net(inode, file, ip_vs_bpf_stat_show);
}

static const struct file_operations ip_vs_bpf_stat_fops = {
	.owner	 = THIS_MODULE,
	.open	 = ip_vs_bpf_stat_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release_net,
};

struct cidrs __rcu *non_masq_cidrs;
static int find_leading_zero(__u32 mask)
{
	__u32 msb = 1 << 31;
	int i;

	for (i = 0; i < 32; i++) {
		if ((mask << i) & msb)
			break;
	}
	return i;
}

#define CIDRLEN sizeof("255.255.255.255/24:")
static ssize_t ip_vs_nosnat_read(struct file *file,
				 char __user *ubuf,
				 size_t count,
				 loff_t *ppos)
{
	char buf[MAXCIDRNUM * CIDRLEN];
	int written = 0;
	int remaining = sizeof(buf) - written;
	int ret = 0;
	int i = 0;
	struct cidrs *c;

	if (*ppos > 0)
		return 0;

	memset(buf, 0, sizeof(buf));
	rcu_read_lock();
	c = rcu_dereference(non_masq_cidrs);
	if (!c) {
		rcu_read_unlock();
		return -EFAULT;
	}
	for (i = 0; i < c->len; i++) {
		/* snprintf need tail to save null bytes */
		ret = snprintf(buf + written, remaining, "%d.%d.%d.%d/%d:",
			       (c->items[i].netip >> 24) & 0xff,
			       (c->items[i].netip >> 16) & 0xff,
			       (c->items[i].netip >> 8) & 0xff,
			       c->items[i].netip & 0xff,
			       find_leading_zero(~(c->items[i].netmask))
			       );

		if (ret < 0 || ret >= remaining) {
			rcu_read_unlock();
			return -EFAULT;
		}

		written += ret;
		remaining -= ret;
	}
	rcu_read_unlock();

	if (copy_to_user(ubuf, buf, written))
		return -EFAULT;

	/* so next time ppos will be bigger than 0 and return 0 */
	*ppos = written;
	return written;
}

/* echo "10.10.10.1/24:1.1.3.4/25" > me */
static ssize_t ip_vs_nosnat_write(struct file *file,
				  const char __user *ubuf,
				  size_t count,
				  loff_t *ppos)
{
	/* take care of stack of */
	char *buf;
	const char delim[2] = ":";
	char *token;
	char *s;
	int i;
	int cidrnum = 0;
	int ret = count;
	struct cidrs *new = NULL;
	struct cidrs *old;
	char cidrs2[MAXCIDRNUM][CIDRLEN];

	if (*ppos > 0)
		return -EFAULT;
	/* prevent buffer of */
	if (count > MAXCIDRNUM * CIDRLEN)
		return -EINVAL;

	/* prevent mulitple user processes write to me */
	mutex_lock(&ip_vs_bpf_proc_lock);

	buf = kzalloc(MAXCIDRNUM * CIDRLEN, GFP_KERNEL);
	if (!buf) {
		mutex_unlock(&ip_vs_bpf_proc_lock);
		return -ENOMEM;
	}

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new) {
		mutex_unlock(&ip_vs_bpf_proc_lock);
		kfree(buf);
		return -ENOMEM;
	}

	memset(cidrs2, 0, sizeof(cidrs2));

	if (copy_from_user(buf, ubuf, count)) {
		ret = -EFAULT;
		goto out;
	}

	i = 0;
	if (count == 1 && strncmp(buf, ":", 1) == 0)
		goto skip_parse;

	s = buf;
	while ((token = strsep(&s, delim)) != NULL) {
		if (i > MAXCIDRNUM - 1) {
			ret = -EINVAL;
			goto out;
		}
		strncpy(cidrs2[i], token, CIDRLEN);
		i++;
	}
	cidrnum = i;
	for (i = 0; i < cidrnum; i++) {
		char ip[20];
		char mask[3];
		__u32  ipi;
		__u32  maski;

		memset(ip, 0, sizeof(ip));
		memset(mask, 0, sizeof(mask));

		s = cidrs2[i];

		token = strsep(&s, "/");
		if (!token) {
			ret = -EINVAL;
			goto out;
		};

		strncpy(ip, token, sizeof(ip) - 1);
		if (in4_pton(ip, -1, (u8 *)&ipi, -1, NULL) != 1) {
			ret = -EINVAL;
			goto out;
		}
		new->items[i].netip = ntohl(ipi);

		token = strsep(&s, "/");
		if (!token) {
			ret = -EINVAL;
			goto out;
		}

		if (strlen(token) > 2) {
			ret = -EINVAL;
			goto out;
		}
		strncpy(mask, token, 2);
		if (kstrtouint(mask, 0, &maski) != 0) {
			ret = -EINVAL;
			goto out;
		}
		if (maski > 32) {
			ret = -EINVAL;
			goto out;
		}

		/* Note: in compiler 1 << 32 is 0. during run time,
		 * 1 << (32 - i), where i is zero, return 1
		 */
		if (maski == 0)
			new->items[i].netmask = 0;
		else
			new->items[i].netmask = ~((1 << (32 - maski)) - 1);
	}

skip_parse:
	/* reserve the old one to free */
	old = rcu_dereference(non_masq_cidrs);
	/* publish the cidrs */
	new->len = i;
	rcu_assign_pointer(non_masq_cidrs, new);

	/* free the old one after grace period*/
	synchronize_rcu();
	kfree(old);
out:
	kfree(buf);
	/* the new one is not published, delete it */
	if (ret < 0)
		kfree(new);

	mutex_unlock(&ip_vs_bpf_proc_lock);
	return ret;
}

static const struct file_operations ip_vs_nosnat_fops = {
	.owner = THIS_MODULE,
	.read  = ip_vs_nosnat_read,
	.write = ip_vs_nosnat_write,
};

int ip_vs_svc_proc_init(struct netns_ipvs *ipvs)
{
	struct cidrs *c;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;
	rcu_assign_pointer(non_masq_cidrs, c);

	if (!proc_create("ip_vs_svc_ip_attr", 0600,
		    ipvs->net->proc_net, &ip_vs_svc_fops))
		goto out_svc_ip;
	if (!proc_create("ip_vs_bpf_id", 0600,
		    ipvs->net->proc_net, &ip_vs_bpf_fops))
		goto out_bpf_id;
	if (!proc_create("ip_vs_devip", 0600,
		    ipvs->net->proc_net, &ip_vs_devip_fops))
		goto out_devip;
	if (!proc_create("ip_vs_port_range", 0600,
		    ipvs->net->proc_net, &ip_vs_port_fops))
		goto out_port_range;
	if (!proc_create("ip_vs_bpf_stat", S_IRUGO, ipvs->net->proc_net,
			 &ip_vs_bpf_stat_fops))
		goto out_bpf_stat;
	if (!proc_create("ip_vs_bpf_fix", 0600,
			 ipvs->net->proc_net, &ip_vs_bpf_fix_fops))
		goto out_bpf_fix;
	if (!proc_create("ip_vs_non_masq_cidrs", 0600,
			 ipvs->net->proc_net, &ip_vs_nosnat_fops))
		goto out_non_masq;

	return 0;
out_non_masq:
	remove_proc_entry("ip_vs_bpf_fix", ipvs->net->proc_net);
out_bpf_fix:
	remove_proc_entry("ip_vs_bpf_stat", ipvs->net->proc_net);
out_bpf_stat:
	remove_proc_entry("ip_vs_port_range", ipvs->net->proc_net);
out_port_range:
	remove_proc_entry("ip_vs_devip", ipvs->net->proc_net);
out_devip:
	remove_proc_entry("ip_vs_bpf_id", ipvs->net->proc_net);
out_bpf_id:
	remove_proc_entry("ip_vs_svc_ip_attr", ipvs->net->proc_net);
out_svc_ip:
	return -ENOMEM;
}

int ip_vs_svc_proc_cleanup(struct netns_ipvs *ipvs)
{
	struct cidrs *old;
	remove_proc_entry("ip_vs_svc_ip_attr", ipvs->net->proc_net);
	remove_proc_entry("ip_vs_bpf_id", ipvs->net->proc_net);
	remove_proc_entry("ip_vs_devip", ipvs->net->proc_net);
	remove_proc_entry("ip_vs_port_range", ipvs->net->proc_net);
	remove_proc_entry("ip_vs_bpf_stat", ipvs->net->proc_net);
	remove_proc_entry("ip_vs_bpf_fix", ipvs->net->proc_net);
	remove_proc_entry("ip_vs_non_masq_cidrs", ipvs->net->proc_net);
	old = rcu_dereference(non_masq_cidrs);
	rcu_assign_pointer(non_masq_cidrs, NULL);
	synchronize_rcu();
	kfree(old);
	return 0;
}
