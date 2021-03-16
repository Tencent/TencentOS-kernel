/*
** This module uses the netfilter interface to maintain statistics
** about the network traffic per task, on level of thread group
** and individual thread.
**
** General setup
** -------------
** Once the module is active, it is called for every packet that is
** transmitted by a local process and every packet that is received
** from an interface. Not only the packets that contain the user data
** are passed but also the TCP related protocol packets (SYN, ACK, ...).
**
** When the module discovers a packet for a connection (TCP) or local
** port (UDP) that is new, it creates a sockinfo structure. As soon as
** possible the sockinfo struct will be connected to a taskinfo struct
** that represents the proces or thread that is related to the socket.
** However, the task can only be determined when a packet is transmitted,
** i.e. the module is called during system call handling in the context
** of the transmitting process. At that moment the tgid (process) and
** pid (thread) can be obtained from the process administration to
** be stored in the module's own taskinfo structs (one for the process,
** one for the thread).
** For the time that the sockinfo struct can not be related to a taskinfo
** struct (e.g. when only packets are received), counters are maintained
** temporarily in the sockinfo struct. As soon as a related taskinfo struct
** is discovered when the task transmits, counters will be maintained in
** the taskinfo struct itself.
** When packets are only received for a socket (e.g. another machine is
** sending UDP packets to the local machine) while the local task
** never responds, no match to a process can be made and the packets
** remain unidentified by the netatop module. At least one packet should
** have been sent by a local process to be able to match packets for such
** socket.
** In the file /proc/netatop counters can be found that show the total
** number of packets sent/received and how many of these packets were
** unidentified (i.e. not accounted to a process/thread).
**
** Garbage collection
** ------------------
** The module uses a garbage collector to cleanup the unused sockinfo
** structs if connections do not exist any more (TCP) or have not been
** used for some time (TCP/UDP).
** Furthermore, the garbage collector checks if the taskinfo structs
** still represent existing processes or threads. If not, the taskinfo struct
** is destroyed (in case of a thread) or it is moved to a separate list of
** finished processes (in case of a process). Analysis programs can read
** the taskinfo of such finished process. When the taskinfo of a finished
** process is not read within 15 seconds, the taskinfo will be destroyed.
**
** A garbage collector cycle can be triggered by issueing a getsockopt
** call from an analysis program (e.g. atop). Apart from that, a time-based
** garbage collector cycle is issued anyhow every 15 seconds by the
** knetatop kernel thread.
**
** Interface with user mode
** ------------------------
** Programs can open an IP socket and use the getsockopt() system call
** to issue commands to this module. With the command ATOP_GETCNT_TGID
** the current counters can be obtained on process level (thread group)
** and with the command ATOP_GETCNT_PID the counters on thread level.
** For both commands, the tgid/pid has to be passed of the required thread
** (group). When the required thread (group) does not exist, an errno ESRCH
** is given.
**
** The command ATOP_GETCNT_EXIT can be issued to obtain the counters of
** an exited process. As stated above, such command has to be issued
** within 15 seconds after a process has been declared 'finished' by
** the garbage collector. Whenever this command is issued and no exited
** process is in the exitlist, the requesting process is blocked until
** an exited process is available.
**
** The command NETATOP_FORCE_GC activates the garbage collector of the
** netatop module to  determine if sockinfo's of old connections/ports
** can be destroyed and if taskinfo's of exited processes can be
** The command NETATOP_EMPTY_EXIT can be issued to wait until the exitlist
** with the taskinfo's of exited processes is empty.
** ----------------------------------------------------------------------
** Copyright (C) 2012    Gerlof Langeveld (gerlof.langeveld@atoptool.nl)
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License version 2 as
** published by the Free Software Foundation.
*/
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include <net/sock.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/proc_fs.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/udp.h>

#include "netatop.h"
#include "netatopversion.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gerlof Langeveld <gerlof.langeveld@atoptool.nl>");
MODULE_DESCRIPTION("Per-task network statistics");
MODULE_VERSION(NETATOPVERSION);

#define	GCINTERVAL	(HZ*15)		// interval garbage collector (jiffies)
#define	GCMAXUDP	(HZ*16)		// max inactivity for UDP     (jiffies)
#define	GCMAXTCP	(HZ*1800)	// max inactivity for TCP     (jiffies)
#define	GCMAXUNREF	(HZ*60)		// max time without taskref   (jiffies)

#define	SILIMIT		(4096*1024)	// maximum memory for sockinfo structs
#define	TILIMIT		(2048*1024)	// maximum memory for taskinfo structs

#define NF_IP_PRE_ROUTING       0
#define NF_IP_LOCAL_IN          1
#define NF_IP_FORWARD           2
#define NF_IP_LOCAL_OUT         3
#define NF_IP_POST_ROUTING      4

/*
** struct that maintains statistics about the network
** traffic caused per thread or thread group
*/
struct chainer {
	void			*next;
	void 			*prev;
};

struct taskinfobucket;

struct taskinfo {
	struct chainer		ch;

	pid_t			id;		// tgid or pid
	char			type;		// 'g' (thread group) or
						// 't' (thread)
	unsigned char		state;		// see below
	char			command[COMLEN];
	unsigned long		btime;		// start time of process
	unsigned long long	exittime;	// time inserted in exitlist

	struct taskcount	tc;
};

static struct kmem_cache	*ticache;	// taskinfo cache

// state values above
#define	CHECKED		1	// verified that task still exists
#define	INDELETE	2	// task exited but still in hash list
#define	FINISHED	3	// task on exit list

/*
** hash tables to find a particular thread group or thread
*/
#define	TBUCKS		1024	// must be multiple of 2!
#define	THASH(x, t)	(((x)+t)&(TBUCKS-1))

struct taskinfobucket {
	struct chainer	ch;
	spinlock_t	lock;
}	thash[TBUCKS];

static unsigned long	nrt;     // current number of taskinfo allocated
static unsigned long	nrt_ovf; // no taskinfo allocated due to overflow
static DEFINE_SPINLOCK(nrtlock);


static struct taskinfo	*exithead;	// linked list of exited processes
static struct taskinfo	*exittail;
static DEFINE_SPINLOCK(exitlock);

static DECLARE_WAIT_QUEUE_HEAD(exitlist_filled);
static DECLARE_WAIT_QUEUE_HEAD(exitlist_empty);

static unsigned long	nre;	// current number of taskinfo on exitlist

/*
** structs that uniquely identify a TCP connection (host endian format)
*/
struct tcpv4_ident {
	uint32_t	laddr;  /* local  IP  address */
	uint32_t	raddr;  /* remote IP  address */
	uint16_t	lport;  /* local  port number */
	uint16_t	rport;  /* remote port number */
};

struct tcpv6_ident {
	struct in6_addr	laddr;  /* local  IP  address */
	struct in6_addr	raddr;  /* remote IP  address */
	uint16_t	lport;  /* local  port number */
	uint16_t	rport;  /* remote port number */
};

/*
** struct to maintain the reference from a socket
** to a thread and thread-group
*/
struct sockinfo {
	struct chainer		ch;

	unsigned char		last_state;	// last known state of socket
	uint8_t			proto;  	// protocol

	union keydef {
		uint16_t		udp;	// UDP ident (only portnumber)
		struct tcpv4_ident	tcp4;	// TCP connection ident IPv4
		struct tcpv6_ident	tcp6;	// TCP connection ident IPv6
	} key;

	struct taskinfo		*tgp;		// ref to thread group
	struct taskinfo		*thp;		// ref to thread (or NULL)

	short			tgh;		// hash number of thread group
	short			thh;		// hash number of thread

	unsigned int		sndpacks;	// temporary counters in case
	unsigned int		rcvpacks; 	// known yet
	unsigned long		sndbytes;	// no relation to process is
	unsigned long		rcvbytes;

	unsigned long long	lastact;	// last updated (jiffies)
};

static struct kmem_cache	*sicache;	// sockinfo cache

/*
** hash table to find a socket reference
*/
#define	SBUCKS		1024	// must be multiple of 2!
#define	SHASHTCP4(x)	(((x).raddr+(x).lport+(x).rport)&(SBUCKS-1))
#define	SHASHUDP(x)	((x)&(SBUCKS-1))

struct {
	struct chainer	ch;
	spinlock_t	lock;
}	shash[SBUCKS];

static unsigned long	nrs;     // current number sockinfo allocated
static unsigned long	nrs_ovf; // no sockinfo allocated due to overflow
static DEFINE_SPINLOCK(nrslock);

/*
** various static counters
*/
static unsigned long	icmpsndbytes;
static unsigned long	icmpsndpacks;
static unsigned long	icmprcvbytes;
static unsigned long	icmprcvpacks;

static unsigned long	tcpsndpacks;
static unsigned long	tcprcvpacks;
static unsigned long	udpsndpacks;
static unsigned long	udprcvpacks;
static unsigned long	unidentudpsndpacks;
static unsigned long	unidentudprcvpacks;
static unsigned long	unidenttcpsndpacks;
static unsigned long	unidenttcprcvpacks;

static unsigned long	unknownproto;

static DEFINE_MUTEX(gclock);
static unsigned long long	gclast;	// last garbage collection (jiffies)

static struct task_struct	*knetatop_task;

static struct timespec	boottime;

/*
** function prototypes
*/
static void 		analyze_tcpv4_packet(struct sk_buff *,
				const struct net_device *, int, char,
				struct iphdr *, void *);

static void 		analyze_udp_packet(struct sk_buff *,
				const struct net_device *, int, char,
				struct iphdr *, void *);

static int		sock2task(char, struct sockinfo *,
					struct taskinfo **, short *,
				struct sk_buff *, const struct net_device *,
				int, char);

static void		update_taskcounters(struct sk_buff *,
				const struct net_device *,
				struct taskinfo *, char);

static void		update_sockcounters(struct sk_buff *,
				const struct net_device *,
				struct sockinfo *, char);

static void		sock2task_sync(struct sk_buff *,
				struct sockinfo *, struct taskinfo *);

static void		register_unident(struct sockinfo *);

static int		calc_reallen(struct sk_buff *,
			             const struct net_device *);

static void		get_tcpv4_ident(struct iphdr *, void *,
				char, union keydef *);

static struct sockinfo	*find_sockinfo(int, union keydef *, int, int);
static struct sockinfo	*make_sockinfo(int, union keydef *, int, int);

static void		wipesockinfo(void);
static void		wipetaskinfo(void);
static void		wipetaskexit(void);

static void		garbage_collector(void);
static void		gctaskexit(void);
static void		gcsockinfo(void);
static void		gctaskinfo(void);

static void		move_taskinfo(struct taskinfo *);
static void		delete_taskinfo(struct taskinfo *);
static void		delete_sockinfo(struct sockinfo *);

static struct taskinfo	*get_taskinfo(pid_t, char);

static int		getsockopt(struct sock *, int, void *, int *);

static int		netatop_open(struct inode *inode, struct file *file);

/*
** hook definitions
*/
static struct nf_hook_ops hookin_ipv4;
static struct nf_hook_ops hookout_ipv4;

/*
** getsockopt definitions for communication with user space
*/
static struct nf_sockopt_ops sockopts = {
	.pf             = PF_INET,
	.get_optmin     = NETATOP_BASE_CTL,
	.get_optmax     = NETATOP_BASE_CTL+6,
	.get            = getsockopt,
	.owner          = THIS_MODULE,
};

static struct file_operations netatop_proc_fops = {
	.open           = netatop_open,
	.read	        = seq_read,
	.llseek	        = seq_lseek,
	.release	= single_release,
	.owner          = THIS_MODULE,
};

/*
** hook function to be called for every incoming local packet
*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
#define	HOOK_ARG_TYPE	void *priv	
#define HOOK_STATE_ARGS const struct nf_hook_state *state

#define DEV_IN		state->in
#define DEV_OUT		state->out
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
#define	HOOK_ARG_TYPE	const struct nf_hook_ops *ops
#define HOOK_STATE_ARGS const struct nf_hook_state *state

#define DEV_IN		state->in
#define DEV_OUT		state->out
#else
#define	HOOK_ARG_TYPE	unsigned int hooknum
#define HOOK_STATE_ARGS const struct net_device *in, \
			const struct net_device *out, \
			int (*okfn)(struct sk_buff *)

#define DEV_IN		in
#define DEV_OUT		out
#endif

static unsigned int
ipv4_hookin(HOOK_ARG_TYPE, struct sk_buff *skb, HOOK_STATE_ARGS)
{
	struct iphdr	*iph;
	void		*trh;

	if (skb == NULL)		// useless socket buffer?
		return NF_ACCEPT;

	/*
	** get pointer to IP header and transport header
	*/
	iph = (struct iphdr *)skb_network_header(skb);
	trh = ((char *)iph + (iph->ihl * 4));

	/*
	** react on protocol number
	*/
	switch (iph->protocol) {
		case IPPROTO_TCP:
		tcprcvpacks++;
		analyze_tcpv4_packet(skb, DEV_IN, 0, 'i', iph, trh);
		break;

		case IPPROTO_UDP:
		udprcvpacks++;
		analyze_udp_packet(skb, DEV_IN, 0, 'i', iph, trh);
		break;

		case IPPROTO_ICMP:
		icmprcvpacks++;
		icmprcvbytes += skb->len + DEV_IN->hard_header_len + 4;
		break;

		default:
		unknownproto++;
	}

	// accept every packet after stats gathering
	return NF_ACCEPT;
}

/*
** hook function to be called for every outgoing local packet
*/
static unsigned int
ipv4_hookout(HOOK_ARG_TYPE, struct sk_buff *skb, HOOK_STATE_ARGS)
{
	int		in_syscall = !in_interrupt();
	struct iphdr	*iph;
	void		*trh;

	if (skb == NULL)		// useless socket buffer?
		return NF_ACCEPT;

	/*
	** get pointer to IP header and transport header
	*/
	iph = (struct iphdr *)skb_network_header(skb);
	trh = skb_transport_header(skb);

	/*
	** react on protocol number
	*/
	switch (iph->protocol) {
		case IPPROTO_TCP:
		tcpsndpacks++;
		analyze_tcpv4_packet(skb, DEV_OUT, in_syscall, 'o', iph, trh);
		break;

		case IPPROTO_UDP:
		udpsndpacks++;
		analyze_udp_packet(skb, DEV_OUT, in_syscall, 'o', iph, trh);
		break;

		case IPPROTO_ICMP:
		icmpsndpacks++;
		icmpsndbytes += skb->len + DEV_OUT->hard_header_len + 4;
		break;

		default:
		unknownproto++;
	}

	// accept every packet after stats gathering
	return NF_ACCEPT;
}

/*
** generic function (for input and output) to analyze the current packet
*/
static void
analyze_tcpv4_packet(struct sk_buff *skb,
	const struct net_device *ndev,	// interface description
	int in_syscall,			// called during system call?
	char direction,			// incoming ('i') or outgoing ('o')
	struct iphdr *iph, void *trh)
{
	union keydef		key;
	struct sockinfo		*sip;
	int			bs;	// hash bucket for sockinfo
	unsigned long		sflags;

	/*
	** determine tcpv4_ident that identifies this TCP packet
	** and calculate hash bucket in sockinfo hash
	*/
	get_tcpv4_ident(iph, trh, direction, &key);

	/*
	** check if we have seen this tcpv4_ident before with a
	** corresponding thread and thread group
	*/
	bs = SHASHTCP4(key.tcp4);

	spin_lock_irqsave(&shash[bs].lock, sflags);

	if ( (sip = find_sockinfo(IPPROTO_TCP, &key, sizeof key.tcp4, bs))
								== NULL) {
		// no sockinfo yet: create one
		if ( (sip = make_sockinfo(IPPROTO_TCP, &key,
					sizeof key.tcp4, bs)) == NULL) {
			if (direction == 'i')
				unidenttcprcvpacks++;
			else
				unidenttcpsndpacks++;
			goto unlocks;
		}
	}

	if (skb->sk)
		sip->last_state = skb->sk->sk_state;

	/*
	** if needed (re)connect the sockinfo to a taskinfo and update
	** the counters
	*/

	// connect to thread group and update
	if (sock2task('g', sip, &sip->tgp, &sip->tgh,
				skb, ndev, in_syscall, direction)) {
		// connect to thread and update
		(void) sock2task('t', sip, &sip->thp, &sip->thh,
				skb, ndev, in_syscall, direction);
	}

unlocks:
	spin_unlock_irqrestore(&shash[bs].lock, sflags);
}


/*
** generic function (for input and output) to analyze the current packet
*/
static void
analyze_udp_packet(struct sk_buff *skb,
	const struct net_device *ndev,	// interface description
	int in_syscall,			// called during system call?
	char direction,			// incoming ('i') or outgoing ('o')
	struct iphdr *iph, void *trh)
{
	struct udphdr	*udph = (struct udphdr *)trh;
	uint16_t	udplocal = (direction == 'i' ?
					ntohs(udph->dest) : ntohs(udph->source));
	int		bs;	// hash bucket for sockinfo

	union keydef 	key;
	struct sockinfo	*sip;
	unsigned long	sflags;

	/*
	** check if we have seen this local UDP port before with a
	** corresponding thread and thread group
	*/
	key.udp	= udplocal;
	bs      = SHASHUDP(udplocal);

	spin_lock_irqsave(&shash[bs].lock, sflags);

	if ( (sip = find_sockinfo(IPPROTO_UDP, &key, sizeof key.udp, bs))
								== NULL) {
		// no sockinfo yet: create one
		if ( (sip = make_sockinfo(IPPROTO_UDP, &key,
				sizeof key.udp, bs)) == NULL) {
			if (direction == 'i')
				unidentudprcvpacks++;
			else
				unidentudpsndpacks++;
			goto unlocks;
		}
	}

	/*
	** if needed (re)connect the sockinfo to a taskinfo and update
	** the counters
	*/

	// connect to thread group and update
	if (sock2task('g', sip, &sip->tgp, &sip->tgh,
				skb, ndev, in_syscall, direction)) {
		// connect to thread and update
		(void) sock2task('t', sip, &sip->thp, &sip->thh,
				skb, ndev, in_syscall, direction);
	}

unlocks:
	spin_unlock_irqrestore(&shash[bs].lock, sflags);
}

/*
** connect the sockinfo to the correct taskinfo and update the counters
*/
static int
sock2task(char idtype, struct sockinfo *sip, struct taskinfo **tipp,
	short *hash, struct sk_buff *skb, const struct net_device *ndev,
	int in_syscall, char direction)
{
	pid_t		curid;
	unsigned long	tflags;

	if (*tipp == NULL) {
		/*
		** no taskinfo connected yet for this reference from
		** sockinfo; to connect to a taskinfo, we must
		** be in system call handling now --> verify
		*/
		if (!in_syscall) {
			if (idtype == 'g')
				update_sockcounters(skb, ndev, sip, direction);

			return 0;	// failed
		}

		/*
		** try to find existing taskinfo or create new taskinfo
		*/
		curid = (idtype == 'g' ? current->tgid : current->pid);

		*hash = THASH(curid, idtype);		// calc hashQ

		spin_lock_irqsave(&thash[*hash].lock, tflags);

		if ( (*tipp = get_taskinfo(curid, idtype)) == NULL) {
			/*
			** not possible to connect
			*/
			spin_unlock_irqrestore(&thash[*hash].lock, tflags);

			if (idtype == 'g')
				update_sockcounters(skb, ndev, sip, direction);

			return 0;			// failed
		}

		/*
		** new connection made:
		** update task counters with sock counters
		*/
		sock2task_sync(skb, sip, *tipp);
	} else {
		/*
		** already related to thread group or thread
		** lock existing task
		*/
		spin_lock_irqsave(&thash[*hash].lock, tflags);

		/*
		** check if socket has been passed to another process in the
		** meantime, like programs as xinetd use to do
		** if so, connect sockinfo to the new task
		*/
		if (in_syscall) {
			curid = (idtype == 'g' ? current->tgid : current->pid);

			if ((*tipp)->id != curid) {
				spin_unlock_irqrestore(&thash[*hash].lock,
									tflags);
				*hash = THASH(curid, idtype);

				spin_lock_irqsave(&thash[*hash].lock, tflags);

				if ( (*tipp = get_taskinfo(curid, idtype))
								== NULL) {
					spin_unlock_irqrestore(
						&thash[*hash].lock, tflags);
					return 0;
				}
			}
		}
	}

	update_taskcounters(skb, ndev, *tipp, direction);

	spin_unlock_irqrestore(&thash[*hash].lock, tflags);

	return 1;
}

/*
** update the statistics of a particular thread group or thread
*/
static void
update_taskcounters(struct sk_buff *skb, const struct net_device *ndev,
		struct taskinfo *tip, char direction)
{
	struct iphdr	*iph = (struct iphdr *)skb_network_header(skb);
	int		reallen = calc_reallen(skb, ndev);

	switch (iph->protocol) {
		case IPPROTO_TCP:
		if (direction == 'i') {
			tip->tc.tcprcvpacks++;
			tip->tc.tcprcvbytes += reallen;
		} else {
			tip->tc.tcpsndpacks++;
			tip->tc.tcpsndbytes += reallen;
		}
		break;

		case IPPROTO_UDP:
		if (direction == 'i') {
			tip->tc.udprcvpacks++;
			tip->tc.udprcvbytes += reallen;
		} else {
			tip->tc.udpsndpacks++;
			tip->tc.udpsndbytes += reallen;
		}
	}
}

/*
** update the statistics of a sockinfo without a connected task
*/
static void
update_sockcounters(struct sk_buff *skb, const struct net_device *ndev,
		struct sockinfo *sip, char direction)
{
	int	reallen = calc_reallen(skb, ndev);

	if (direction == 'i') {
		sip->rcvpacks++;
		sip->rcvbytes += reallen;
	} else {
		sip->sndpacks++;
		sip->sndbytes += reallen;
	}
}

/*
** add the temporary counters in the sockinfo to the new connected task
*/
static void
sock2task_sync(struct sk_buff *skb, struct sockinfo *sip, struct taskinfo *tip)
{
	struct iphdr	*iph = (struct iphdr *)skb_network_header(skb);

	switch (iph->protocol) {
		case IPPROTO_TCP:
		tip->tc.tcprcvpacks	+= sip->rcvpacks;
		tip->tc.tcprcvbytes 	+= sip->rcvbytes;
		tip->tc.tcpsndpacks	+= sip->sndpacks;
		tip->tc.tcpsndbytes	+= sip->sndbytes;
		break;

		case IPPROTO_UDP:
		tip->tc.udprcvpacks	+= sip->rcvpacks;
		tip->tc.udprcvbytes 	+= sip->rcvbytes;
		tip->tc.udpsndpacks	+= sip->sndpacks;
		tip->tc.udpsndbytes	+= sip->sndbytes;
	}
}

static void
register_unident(struct sockinfo *sip)
{
	switch (sip->proto) {
		case IPPROTO_TCP:
		unidenttcprcvpacks += sip->rcvpacks;
		unidenttcpsndpacks += sip->sndpacks;
		break;

		case IPPROTO_UDP:
		unidentudprcvpacks += sip->rcvpacks;
		unidentudpsndpacks += sip->sndpacks;
	}
}

/*
** calculate the number of bytes that are really sent or received
*/
static int
calc_reallen(struct sk_buff *skb, const struct net_device *ndev)
{
	/*
	** calculate the real load of this packet on the network:
	**
	**  - length of IP header, TCP/UDP header and data (skb->len)
	**
	**    since packet assembly/disassembly is done by the IP layer
	**    (we get an input packet that has been assembled already and
	**    an output packet that still has to be assembled), additional
	**    IP headers/interface headers and interface headers have
	**    to be calculated for packets that are larger than the mtu
	**
	**  - interface header length + 4 bytes crc
	*/
	int reallen = skb->len;

	if (reallen > ndev->mtu)
		reallen += (reallen / ndev->mtu) *
				(sizeof(struct iphdr) + ndev->hard_header_len + 4);

	reallen += ndev->hard_header_len + 4;

	return reallen;
}

/*
** find the tcpv4_ident for the current packet, represented by
** the skb_buff
*/
static void
get_tcpv4_ident(struct iphdr *iph, void *trh, char direction, union keydef *key)
{
	struct tcphdr	*tcph = (struct tcphdr *)trh;

	memset(key, 0, sizeof *key); 	// important for memcmp later on

	/*
	** determine local/remote IP address and
	** determine local/remote port number
	*/
	switch (direction) {
		case 'i':	// incoming packet
		key->tcp4.laddr = ntohl(iph->daddr);
		key->tcp4.raddr = ntohl(iph->saddr);
		key->tcp4.lport = ntohs(tcph->dest);
		key->tcp4.rport = ntohs(tcph->source);
		break;

		case 'o':	// outgoing packet
		key->tcp4.laddr = ntohl(iph->saddr);
		key->tcp4.raddr = ntohl(iph->daddr);
		key->tcp4.lport = ntohs(tcph->source);
		key->tcp4.rport = ntohs(tcph->dest);
	}
}

/*
** search for the sockinfo holding the given address info
** the appropriate hash bucket must have been locked before calling
*/
static struct sockinfo *
find_sockinfo(int proto, union keydef *identp, int identsz, int hash)
{
	struct sockinfo	*sip = shash[hash].ch.next;

	/*
	** search for appropriate struct
	*/
	while (sip != (void *)&shash[hash].ch) {
		if ( memcmp(&sip->key, identp, identsz) == 0 &&
						sip->proto == proto) {
			sip->lastact = jiffies_64;
			return sip;
		}

		sip = sip->ch.next;
	}

	return NULL;	// not existing
}

/*
** create a new sockinfo and fill
** the appropriate hash bucket must have been locked before calling
*/
static struct sockinfo *
make_sockinfo(int proto, union keydef *identp, int identsz, int hash)
{
	struct sockinfo	*sip;
	unsigned long	flags;

	/*
	** check if the threshold of memory used for sockinfo structs
	** is reached to avoid that a fork bomb of processes opening
	** a socket leads to memory overload
	*/
	if ( (nrs+1) * sizeof(struct sockinfo) > SILIMIT) {
		spin_lock_irqsave(&nrslock, flags);
		nrs_ovf++;
		spin_unlock_irqrestore(&nrslock, flags);
		return NULL;
	}

	if ( (sip = kmem_cache_alloc(sicache, GFP_ATOMIC)) == NULL)
		return NULL;

	spin_lock_irqsave(&nrslock, flags);
	nrs++;
	spin_unlock_irqrestore(&nrslock, flags);

	/*
	** insert new struct in doubly linked list
	*/
	memset(sip, '\0', sizeof *sip);

	sip->ch.next 		= &shash[hash].ch;
	sip->ch.prev 		=  shash[hash].ch.prev;
	((struct sockinfo *)shash[hash].ch.prev)->ch.next = sip;
	shash[hash].ch.prev 	= sip;

	sip->proto 		= proto;
	sip->lastact 		= jiffies_64;
	sip->key	 	= *identp;

	return sip;
}

/*
** search the taskinfo structure holding the info about the given id/type
** if such taskinfo is not yet present, create a new one
*/
static struct taskinfo *
get_taskinfo(pid_t id, char type)
{
	int		bt = THASH(id, type);
	struct taskinfo	*tip = thash[bt].ch.next;
	unsigned long	tflags;

	/*
	** search if id exists already
	*/
	while (tip != (void *)&thash[bt].ch) {
		if (tip->id == id && tip->type == type)
			return tip;

		tip = tip->ch.next;
	}

	/*
	** check if the threshold of memory used for taskinfo structs
	** is reached to avoid that a fork bomb of processes opening
	** a socket lead to memory overload
	*/
	if ( (nre+nrt+1) * sizeof(struct taskinfo) > TILIMIT) {
		spin_lock_irqsave(&nrtlock, tflags);
		nrt_ovf++;
		spin_unlock_irqrestore(&nrtlock, tflags);
		return NULL;
	}

	/*
	** id not known yet
	** add new entry to hash list
	*/
	if ( (tip = kmem_cache_alloc(ticache, GFP_ATOMIC)) == NULL)
		return NULL;

	spin_lock_irqsave(&nrtlock, tflags);
	nrt++;
	spin_unlock_irqrestore(&nrtlock, tflags);

	/*
	** insert new struct in doubly linked list
	** and fill values
	*/
	memset(tip, '\0', sizeof *tip);

	tip->ch.next 		= &thash[bt].ch;
	tip->ch.prev 		=  thash[bt].ch.prev;
	((struct taskinfo *)thash[bt].ch.prev)->ch.next = tip;
	thash[bt].ch.prev  	= tip;

	tip->id    	= id;
	tip->type    	= type;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
	tip->btime	= div_u64((current->real_start_time +
				(boottime.tv_sec * NSEC_PER_SEC +
					boottime.tv_sec)), NSEC_PER_SEC);
#else
	// current->real_start_time is type u64
	tip->btime 	= current->real_start_time.tv_sec + boottime.tv_sec;

	if (current->real_start_time.tv_nsec + boottime.tv_nsec > NSEC_PER_SEC)
		tip->btime++;
#endif

	strncpy(tip->command, current->comm, COMLEN);

	return tip;
}

/*
** garbage collector that removes:
** - exited tasks that are not by user mode programs
** - sockinfo's that are not used any more
** - taskinfo's that do not exist any more
**
** a mutex avoids that the garbage collector runs several times in parallel
**
** this function may only be called in process context!
*/
static void
garbage_collector(void)
{
	mutex_lock(&gclock);

	if (jiffies_64 < gclast + (HZ/2)) { // maximum 2 GC cycles per second
		mutex_unlock(&gclock);
		return;
	}

	gctaskexit();	// remove remaining taskinfo structs from exit list

	gcsockinfo();	// clean up sockinfo structs in shash list

	gctaskinfo();	// clean up taskinfo structs in thash list

	gclast = jiffies_64;

	mutex_unlock(&gclock);
}

/*
** tasks in the exitlist can be read by a user mode process for a limited
** amount of time; this function removes all taskinfo structures that have
** not been read within that period of time
** notice that exited processes are chained to the tail, so the oldest
** can be found at the head
*/
static void
gctaskexit()
{
	unsigned long	flags;
	struct taskinfo	*tip;

	spin_lock_irqsave(&exitlock, flags);

	for (tip=exithead; tip;) {
		if (jiffies_64 < tip->exittime + GCINTERVAL)
			break;

		// remove taskinfo from exitlist
		exithead = tip->ch.next;
		kmem_cache_free(ticache, tip);
		nre--;
		tip = exithead;
	}

	/*
	** if list empty now, then exithead and exittail both NULL
	** wakeup waiters for emptylist
	*/
	if (nre == 0) {
		exittail = NULL;
		wake_up_interruptible(&exitlist_empty);
	}

	spin_unlock_irqrestore(&exitlock, flags);
}

/*
** cleanup sockinfo structures that are connected to finished processes
*/
static void
gcsockinfo()
{
	int		i;
	struct sockinfo	*sip, *sipsave;
	unsigned long	sflags, tflags;
	struct pid	*pid;

	/*
	** go through all sockinfo hash buckets
	*/
	for (i=0; i < SBUCKS; i++) {
		if (shash[i].ch.next == (void *)&shash[i].ch)
			continue;	// quick return without lock

		spin_lock_irqsave(&shash[i].lock, sflags);

		sip  = shash[i].ch.next;

		/*
		** search all sockinfo structs chained in one bucket
		*/
		while (sip != (void *)&shash[i].ch) {
			/*
			** TCP connections that were not in
			** state ESTABLISHED or LISTEN can be
			** eliminated
			*/
			if (sip->proto == IPPROTO_TCP) {
				switch (sip->last_state) {
					case TCP_ESTABLISHED:
					case TCP_LISTEN:
					break;

					default:
					sipsave = sip->ch.next;
					delete_sockinfo(sip);
					sip = sipsave;
					continue;
				}
			}

			/*
			** check if this sockinfo has no relation
			** for a while with a thread group
			** if so, delete the sockinfo
			*/
			if (sip->tgp == NULL) {
				if (sip->lastact + GCMAXUNREF < jiffies_64) {
					register_unident(sip);
					sipsave = sip->ch.next;
					delete_sockinfo(sip);
					sip = sipsave;
				} else {
					sip = sip->ch.next;
				}
				continue;
			}

			/*
			** check if referred thread group is
			** already marked as 'indelete' during this
			** sockinfo search
			** if so, delete this sockinfo
			*/
			spin_lock_irqsave(&thash[sip->tgh].lock, tflags);

			if (sip->tgp->state == INDELETE) {
				spin_unlock_irqrestore(&thash[sip->tgh].lock,
									tflags);
				sipsave = sip->ch.next;
				delete_sockinfo(sip);
				sip = sipsave;
				continue;
			}

			/*
			** check if referred thread group still exists;
			** this step will be skipped if we already verified
			** the existance of the thread group earlier during
			** this garbage collection cycle
			*/
			if (sip->tgp->state != CHECKED) {
				/*
				** connected thread group not yet verified
				** during this cycle, so check if it still
				** exists
				** if not, mark the thread group as 'indelete'
				** (it can not be deleted right now because
				** we might find other sockinfo's referring
				** to this thread group during the current
				** cycle) and delete this sockinfo
				** if the thread group exists, just mark
				** it  as 'checked' for this cycle
				*/
				rcu_read_lock();
				pid = find_vpid(sip->tgp->id);
				rcu_read_unlock();

				if (pid == NULL) {
					sip->tgp->state = INDELETE;
					spin_unlock_irqrestore(
						&thash[sip->tgh].lock, tflags);

					sipsave = sip->ch.next;
					delete_sockinfo(sip);
					sip = sipsave;
					continue;
				} else {
					sip->tgp->state = CHECKED;
				}
			}

			spin_unlock_irqrestore(&thash[sip->tgh].lock, tflags);

			/*
			** check if this sockinfo has a relation with a thread
			** if not, skip further handling of this sockinfo
			*/
			if (sip->thp == NULL) {
				sip = sip->ch.next;
				continue;
			}

			/*
			** check if referred thread is already marked
			** as 'indelete' during this sockinfo search
			** if so, break connection
			*/
			spin_lock_irqsave(&thash[sip->thh].lock, tflags);

			if (sip->thp->state == INDELETE) {
				spin_unlock_irqrestore(&thash[sip->thh].lock,
									tflags);
				sip->thp = NULL;
				sip = sip->ch.next;
				continue;
			}

			/*
			** check if referred thread is already checked
			** during this sockinfo search
			*/
			if (sip->thp->state == CHECKED) {
				spin_unlock_irqrestore(&thash[sip->thh].lock,
								tflags);
				sip = sip->ch.next;
				continue;
			}

			/*
			** connected thread not yet verified
			** check if it still exists
			** if not, mark it as 'indelete' and break connection
			** if thread exists, mark it 'checked'
			*/
			rcu_read_lock();
			pid = find_vpid(sip->thp->id);
			rcu_read_unlock();

			if (pid == NULL) {
				sip->thp->state = INDELETE;
				sip->thp = NULL;
			} else {
				sip->thp->state = CHECKED;
			}

			spin_unlock_irqrestore(&thash[sip->thh].lock, tflags);

			/*
			** check if a TCP port has not been used
			** for some time --> destroy even if the thread
			** (group) is still there
			*/
			if (sip->proto == IPPROTO_TCP &&
				sip->lastact + GCMAXTCP < jiffies_64) {
				sipsave = sip->ch.next;
				delete_sockinfo(sip);
				sip = sipsave;
				continue;
			}

			/*
			** check if a UDP port has not been used
			** for some time --> destroy even if the thread
			** (group) is still there
			** e.g. outgoing DNS requests (to remote port 53) are
			** issued every time with another source port being
			** a new object that should not be kept too long;
			** local well-known ports are useful to keep
			*/
			if (sip->proto == IPPROTO_UDP &&
				sip->lastact + GCMAXUDP < jiffies_64 &&
				sip->key.udp > 1024)                {
				sipsave = sip->ch.next;
				delete_sockinfo(sip);
				sip = sipsave;
				continue;
			}

			sip = sip->ch.next;
		}

		spin_unlock_irqrestore(&shash[i].lock, sflags);
	}
}

/*
** remove taskinfo structures of finished tasks from hash list
*/
static void
gctaskinfo()
{
	int		i;
	struct taskinfo	*tip, *tipsave;
	unsigned long	tflags;
	struct pid	*pid;

	/*
	** go through all taskinfo hash buckets
	*/
	for (i=0; i < TBUCKS; i++) {
		if (thash[i].ch.next == (void *)&thash[i].ch)
			continue;	// quick return without lock

		spin_lock_irqsave(&thash[i].lock, tflags);

		tip = thash[i].ch.next;

		/*
		** check all taskinfo structs chained to this bucket
		*/
		while (tip != (void *)&thash[i].ch) {
			switch (tip->state) {
				/*
				** remove INDELETE tasks from the hash buckets
				** -- move thread group to exitlist
				** -- destroy thread right away
				*/
				case INDELETE:
				tipsave = tip->ch.next;

				if (tip->type == 'g')
					move_taskinfo(tip);	// thread group
				else
					delete_taskinfo(tip);	// thread

				tip = tipsave;
				break;

				case CHECKED:
				tip->state = 0;
				tip = tip->ch.next;
				break;

				default:	// not checked yet
				rcu_read_lock();
				pid = find_vpid(tip->id);
				rcu_read_unlock();

				if (pid == NULL) {
					tipsave = tip->ch.next;

					if (tip->type == 'g')
						move_taskinfo(tip);
					else
						delete_taskinfo(tip);

					tip = tipsave;
				} else {
					tip = tip->ch.next;
				}
			}
		}

		spin_unlock_irqrestore(&thash[i].lock, tflags);
	}
}


/*
** remove all sockinfo structs
*/
static void
wipesockinfo()
{
	struct sockinfo	*sip, *sipsave;
	int 		i;
	unsigned long	sflags;

	for (i=0; i < SBUCKS; i++) {
		spin_lock_irqsave(&shash[i].lock, sflags);

		sip = shash[i].ch.next;

		/*
		** free all structs chained in one bucket
		*/
		while (sip != (void *)&shash[i].ch) {
			sipsave = sip->ch.next;
			delete_sockinfo(sip);
			sip = sipsave;
		}

		spin_unlock_irqrestore(&shash[i].lock, sflags);
	}
}

/*
** remove all taskinfo structs from hash list
*/
static void
wipetaskinfo()
{
	struct taskinfo	*tip, *tipsave;
	int 		i;
	unsigned long	tflags;

	for (i=0; i < TBUCKS; i++) {
		spin_lock_irqsave(&thash[i].lock, tflags);

		tip = thash[i].ch.next;

		/*
		** free all structs chained in one bucket
		*/
		while (tip != (void *)&thash[i].ch) {
			tipsave = tip->ch.next;
			delete_taskinfo(tip);
			tip = tipsave;
		}

		spin_unlock_irqrestore(&thash[i].lock, tflags);
	}
}

/*
** remove all taskinfo structs from exit list
*/
static void
wipetaskexit()
{
	gctaskexit();
}

/*
** move one taskinfo struct from hash bucket to exitlist
*/
static void
move_taskinfo(struct taskinfo *tip)
{
	unsigned long 	flags;

	/*
	** remove from hash list
	*/
	((struct taskinfo *)tip->ch.next)->ch.prev = tip->ch.prev;
	((struct taskinfo *)tip->ch.prev)->ch.next = tip->ch.next;

	spin_lock_irqsave(&nrtlock, flags);
	nrt--;
	spin_unlock_irqrestore(&nrtlock, flags);

	/*
	** add to exitlist
	*/
	tip->ch.next 	= NULL;
	tip->state   	= FINISHED;
	tip->exittime	= jiffies_64;

	spin_lock_irqsave(&exitlock, flags);

	if (exittail) {			// list filled?
		exittail->ch.next = tip;
		exittail          = tip;
	} else {			// list empty
		exithead = exittail = tip;
	}

	nre++;

	wake_up_interruptible(&exitlist_filled);

	spin_unlock_irqrestore(&exitlock, flags);
}

/*
** remove one taskinfo struct for the hash bucket chain
*/
static void
delete_taskinfo(struct taskinfo *tip)
{
	unsigned long	flags;

	((struct taskinfo *)tip->ch.next)->ch.prev = tip->ch.prev;
	((struct taskinfo *)tip->ch.prev)->ch.next = tip->ch.next;

	kmem_cache_free(ticache, tip);

	spin_lock_irqsave(&nrtlock, flags);
	nrt--;
	spin_unlock_irqrestore(&nrtlock, flags);
}

/*
** remove one sockinfo struct for the hash bucket chain
*/
static void
delete_sockinfo(struct sockinfo *sip)
{
	unsigned long	flags;

	((struct sockinfo *)sip->ch.next)->ch.prev = sip->ch.prev;
	((struct sockinfo *)sip->ch.prev)->ch.next = sip->ch.next;

	kmem_cache_free(sicache, sip);

	spin_lock_irqsave(&nrslock, flags);
	nrs--;
	spin_unlock_irqrestore(&nrslock, flags);
}

/*
** read function for /proc/netatop
*/
static int
netatop_show(struct seq_file *m, void *v)
{
	seq_printf(m, "tcpsndpacks:  %12lu (unident: %9lu)\n"
		"tcprcvpacks:  %12lu (unident: %9lu)\n"
		"udpsndpacks:  %12lu (unident: %9lu)\n"
		"udprcvpacks:  %12lu (unident: %9lu)\n\n"
		"icmpsndpacks: %12lu\n"
		"icmprcvpacks: %12lu\n\n"
		"#sockinfo:    %12lu (overflow: %8lu)\n"
		"#taskinfo:    %12lu (overflow: %8lu)\n"
		"#taskexit:    %12lu\n\n"
		"modversion:   %14s\n",
		tcpsndpacks,  unidenttcpsndpacks,
		tcprcvpacks,  unidenttcprcvpacks,
		udpsndpacks,  unidentudpsndpacks,
		udprcvpacks,  unidentudprcvpacks,
		icmpsndpacks, icmprcvpacks,
		nrs,          nrs_ovf,
		nrt,          nrt_ovf,
		nre,          NETATOPVERSION);
	return 0;
}

static int
netatop_open(struct inode *inode, struct file *file)
{
	return single_open(file, netatop_show, NULL);
}

/*
** called when user spce issues system call getsockopt()
*/
static int
getsockopt(struct sock *sk, int cmd, void __user *user, int *len)
{
	int			bt;
	struct taskinfo		*tip;
	char			tasktype = 't';
	struct netpertask	npt;
	unsigned long		tflags;

	/*
	** verify the proper privileges
	*/
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	/*
	** react on command
	*/
	switch (cmd) {
		case NETATOP_PROBE:
		break;

		case NETATOP_FORCE_GC:
		garbage_collector();
		break;

		case NETATOP_EMPTY_EXIT:
		while (nre > 0) {
			if (wait_event_interruptible(exitlist_empty, nre == 0))
				return -ERESTARTSYS;
		}
		break;

		case NETATOP_GETCNT_EXIT:
		if (nre == 0)
			wake_up_interruptible(&exitlist_empty);

		if (*len < sizeof(pid_t))
			return -EINVAL;

		if (*len > sizeof npt)
			*len = sizeof npt;

		spin_lock_irqsave(&exitlock, tflags);

		/*
		** check if an exited process is present
		** if not, wait for it...
		*/
		while (nre == 0) {
			spin_unlock_irqrestore(&exitlock, tflags);

			if ( wait_event_interruptible(exitlist_filled, nre > 0))
				return -ERESTARTSYS;

			spin_lock_irqsave(&exitlock, tflags);
		}

		/*
		** get first eprocess from exitlist and remove it from there
		*/
		tip = exithead;

		if ( (exithead = tip->ch.next) == NULL)
			exittail = NULL;

		nre--;

		spin_unlock_irqrestore(&exitlock, tflags);

		/*
		** pass relevant info to user mode
		** and free taskinfo struct
		*/
		npt.id		= tip->id;
		npt.tc		= tip->tc;
		npt.btime	= tip->btime;
		memcpy(npt.command, tip->command, COMLEN);

		if (copy_to_user(user, &npt, *len) != 0)
			return -EFAULT;

		kmem_cache_free(ticache, tip);

		return 0;

		case NETATOP_GETCNT_TGID:
		tasktype = 'g';

		case NETATOP_GETCNT_PID:
		if (*len < sizeof(pid_t))
			return -EINVAL;

		if (*len > sizeof npt)
			*len = sizeof npt;

		if (copy_from_user(&npt, user, *len) != 0)
			return -EFAULT;

		/*
		** search requested id in taskinfo hash
		*/
		bt = THASH(npt.id, tasktype);	// calculate hash

		if (thash[bt].ch.next == (void *)&thash[bt].ch)
			return -ESRCH;		// quick return without lock

		spin_lock_irqsave(&thash[bt].lock, tflags);

		tip = thash[bt].ch.next;

		while (tip != (void *)&thash[bt].ch) {
			// is this the one?
			if (tip->id == npt.id && tip->type == tasktype) {
				/*
				** found: copy results to user space
				*/
				memcpy(npt.command, tip->command, COMLEN);
				npt.tc    = tip->tc;
				npt.btime = tip->btime;

				spin_unlock_irqrestore(&thash[bt].lock, tflags);

				if (copy_to_user(user, &npt, *len) != 0)
					return -EFAULT;
				else
					return 0;
			}

			tip = tip->ch.next;
		}

		spin_unlock_irqrestore(&thash[bt].lock, tflags);
		return -ESRCH;

		default:
		printk(KERN_INFO "unknown getsockopt command %d\n", cmd);
		return -EINVAL;
	}

	return 0;
}

/*
** kernel mode thread: initiate garbage collection every N seconds
*/
static int netatop_thread(void *dummy)
{
	while (!kthread_should_stop()) {
		/*
		** do garbage collection
		*/
		garbage_collector();

		/*
		** wait a while
		*/
		(void) schedule_timeout_interruptible(GCINTERVAL);
	}

	return 0;
}

/*
** called when module loaded
*/
int
init_module()
{
	int i;

	/*
	** initialize caches for taskinfo and sockinfo
	*/
	ticache = kmem_cache_create("Netatop_taskinfo",
					sizeof (struct taskinfo), 0, 0, NULL);
	if (!ticache)
		return -EFAULT;

	sicache = kmem_cache_create("Netatop_sockinfo",
					sizeof (struct sockinfo), 0, 0, NULL);
	if (!sicache) {
		kmem_cache_destroy(ticache);
		return -EFAULT;
	}

	/*
	** initialize hash table for taskinfo and sockinfo
	*/
	for (i=0; i < TBUCKS; i++) {
		thash[i].ch.next = &thash[i].ch;
		thash[i].ch.prev = &thash[i].ch;
		spin_lock_init(&thash[i].lock);
	}

	for (i=0; i < SBUCKS; i++) {
		shash[i].ch.next = &shash[i].ch;
		shash[i].ch.prev = &shash[i].ch;
		spin_lock_init(&shash[i].lock);
	}

	getboottime(&boottime);

	/*
	** register getsockopt for user space communication
	*/
	if (nf_register_sockopt(&sockopts) < 0) {
		kmem_cache_destroy(ticache);
		kmem_cache_destroy(sicache);
		return -1;
	}

	/*
	** create a new kernel mode thread for time-driven garbage collection
	** after creation, the thread waits until it is woken up
	*/
	knetatop_task = kthread_create(netatop_thread, NULL, "knetatop");

	if (IS_ERR(knetatop_task)) {
		nf_unregister_sockopt(&sockopts);
		kmem_cache_destroy(ticache);
		kmem_cache_destroy(sicache);
		return -1;
	}

	/*
	** prepare hooks and register
	*/
	hookin_ipv4.hooknum	= NF_IP_LOCAL_IN;	// input packs
	hookin_ipv4.hook	= ipv4_hookin;		// func to call
	hookin_ipv4.pf		= PF_INET;		// IPV4 packets
	hookin_ipv4.priority	= NF_IP_PRI_FIRST;	// highest prio

	hookout_ipv4.hooknum	= NF_IP_LOCAL_OUT;	// output packs
	hookout_ipv4.hook	= ipv4_hookout;		// func to call
	hookout_ipv4.pf		= PF_INET;		// IPV4 packets
	hookout_ipv4.priority	= NF_IP_PRI_FIRST;	// highest prio

	nf_register_net_hook(&init_net, &hookin_ipv4);			// register hook
	nf_register_net_hook(&init_net, &hookout_ipv4);		// register hook

	/*
	** create a /proc-entry to produce status-info on request
	*/
	proc_create("netatop", 0444, NULL, &netatop_proc_fops);

	/*
	** all admi prepared; kick off kernel mode thread
	*/
	wake_up_process(knetatop_task);

	return 0;		// return success
}

/*
** called when module unloaded
*/
void
cleanup_module()
{
	/*
	** tell kernel daemon to stop
	*/
	kthread_stop(knetatop_task);

	/*
	** unregister netfilter hooks and other miscellaneous stuff
	*/
	nf_unregister_net_hook(&init_net, &hookin_ipv4);
	nf_unregister_net_hook(&init_net, &hookout_ipv4);

	remove_proc_entry("netatop", NULL);

	nf_unregister_sockopt(&sockopts);

	/*
	** destroy allocated stats
	*/
	wipesockinfo();
	wipetaskinfo();
	wipetaskexit();

	/*
	** destroy caches
	*/
	kmem_cache_destroy(ticache);
	kmem_cache_destroy(sicache);
}
