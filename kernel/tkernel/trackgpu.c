#include <linux/module.h>
#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/fs.h>
#include <linux/notifier.h>
#include <linux/timer.h>
#include <linux/mutex.h>
#include <linux/dnotify.h>
#include <linux/fsnotify.h>
#include <linux/trackgpu.h>

struct gpu_request_list gpu_list;
int sysctl_track_gpu __read_mostly = 0;
atomic_t gpu_req_count;

#define GPU_MAX_REQUEST 4096
struct gpu_wait *gpu_create_wait(int pid)
{
	struct gpu_wait *w;
	int c = atomic_read(&gpu_req_count);

	if (c >= GPU_MAX_REQUEST) {
		if (c >= GPU_MAX_REQUEST) {
			printk(KERN_ERR "maximum gpu requests detected aborting pid:%d...:%d\n", pid, c);
			return NULL;
		}
	}
	w = kmalloc(sizeof(*w), GFP_KERNEL);
	if (!w) {
		printk(KERN_ERR "alloc gpu wait failed\n");
		return NULL;
	}

	init_waitqueue_head(&w->wait);
	w->proceed = false;
	w->used = false;
	w->wakup = false;
	w->pid = pid;
	w->type = GPU_OPEN;

	mutex_lock(&gpu_list.lock);
	list_add_tail(&w->node, &gpu_list.list);
	mutex_unlock(&gpu_list.lock);
	atomic_inc(&gpu_req_count);

	return w;
}
EXPORT_SYMBOL_GPL(gpu_create_wait);

void gpu_clean(int pid)
{
	struct gpu_wait *w;

	mutex_lock(&gpu_list.lock);
	list_for_each_entry(w, &gpu_list.list, node) {
		if (w->pid == pid) {
			list_del(&w->node);
			kfree(w);
			atomic_dec(&gpu_req_count);
			goto out;
		}
	}
out:
	mutex_unlock(&gpu_list.lock);
}
EXPORT_SYMBOL_GPL(gpu_clean);

static struct gpu_wait *gpu_check_list(int pid)
{
	struct gpu_wait *w;
	struct gpu_wait *wait = NULL;

	mutex_lock(&gpu_list.lock);
	list_for_each_entry(w, &gpu_list.list, node) {
		if (w->pid == pid && !w->used) {
			w->used = true;
			wait = w;
		}
	}
	mutex_unlock(&gpu_list.lock);

	return wait;
}
static bool nl_send_msg(int gpu_pid, enum GPU_EVENT_TYPE event_type);
static struct sock *nl_sk = NULL;
/* This is the communication pid usually the gpu_manager pid.
 * the pid will not change once gpu_manager started.
 * we use this pid to communicate with gpu_manager in the user space.
 */
int comm_pid = -1;

static void nl_recv_msg(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;

	int msg_size;
	char msg_ok[32];
	int recv_pid = 0;
	char *recv_msg;
	struct gpu_wait *wait = NULL;
	char comp_msg[32];
	int len;
	int ret;

	nlh = nlmsg_hdr(skb);
	if (nlh->nlmsg_len < NLMSG_HDRLEN ||
	    skb->len < nlh->nlmsg_len) {
		printk(KERN_ERR "gpu:nl_recv_msg error\n");
		return;
	}

	comm_pid = nlh->nlmsg_pid;

	recv_msg = (char *)nlmsg_data(nlh);
	msg_size = strlen(recv_msg);

	memset(msg_ok, 0, sizeof(msg_ok));
	memset(comp_msg, 0, sizeof(comp_msg));
	len = min(sizeof(comp_msg), msg_size);
	/* During the test we found there are a lot irrelevant characters
	 * following the received messsage so we only use the first 32
	 * characters in the received message.
	 */
	strncpy(comp_msg, recv_msg, len);
	/* the format of the received message form gpu_manager is:pid ok xxx
	 * we retrieve the pid and ok from the received message and check the
	 * request list to decide whether the open can proceed or not
	 */
	ret = sscanf(comp_msg, "%d %s", &recv_pid, msg_ok);
	if (ret < 2) {
		return;
	}
	wait = gpu_check_list(recv_pid);
	if (wait != NULL) {
		if (strncmp(msg_ok, GPU_OK, 2) == 0) {
			wait->wakup = true;
			wait->proceed = true;
			wake_up(&wait->wait);
		} else {
			wait->wakup = true;
			wait->proceed = false;
			wake_up(&wait->wait);
		}
	}
}

static bool nl_send_msg(int gpu_pid, enum GPU_EVENT_TYPE event_type)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb_out;
	int msg_size;
	char msg[64];
	int res;

	if (comm_pid == -1)
		return false;

	/* The format send to gpu_manager(in user space) is:pid, open/close
	 * when gpu_manager got the msg it will response to the kernel
	 * with pid ok xxx so the open can proceed otherwise the open
	 * rejected.
	 */
	if (event_type == GPU_OPEN)
		sprintf(msg, "pid:%d, open", gpu_pid);
	else
		sprintf(msg, "pid:%d, close", gpu_pid);
	msg_size = strlen(msg);
	skb_out = nlmsg_new(msg_size, GFP_KERNEL);
	if (!skb_out)
		return false;
	nlh = nlmsg_put(skb_out, comm_pid, 0, NLMSG_DONE, msg_size, 0);
	if (!nlh)
		return false;

	NETLINK_CB(skb_out).dst_group = 0;
	strncpy(nlmsg_data(nlh), msg, msg_size);

	res = netlink_unicast(nl_sk, skb_out, comm_pid, 0);
	if (res < 0)
		return false;

	return true;
}

int notify_gpu(struct gpu_wait *wait, enum GPU_EVENT_TYPE event, int pid)
{
	if (event == GPU_OPEN) {
		if (!nl_send_msg(pid, GPU_OPEN)) {
			printk(KERN_ERR "gpu notifier:nl send msg failed pid:%d\n", wait->pid);
			wait->proceed = true;
			wait->wakup = true;
			wake_up(&wait->wait);
			return -EINVAL;
		}
	} else
		nl_send_msg(pid, GPU_CLOSE);

	return 0;
}

static int __init trackgpu_init(void)
{
	int err;
	struct netlink_kernel_cfg cfg = {
		.groups = 0,
		.flags = 0,
		.cb_mutex = NULL,
		.bind = NULL,
		.unbind = NULL,
		.compare = NULL,
		.input = nl_recv_msg,
	};

	nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, &cfg);
	if (!nl_sk) {
		pr_info("Error create netlink socket\n");
		return -EINVAL;
	}
	INIT_LIST_HEAD(&gpu_list.list);
	mutex_init(&gpu_list.lock);
	atomic_set(&gpu_req_count, 0);

	return 0;
}
late_initcall(trackgpu_init);
