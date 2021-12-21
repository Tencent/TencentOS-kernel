#ifndef __NET__TOA_H__
#define __NET__TOA_H__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/err.h>
#include <linux/time.h>
#include <linux/skbuff.h>
#include <net/tcp.h>
#include <net/inet_common.h>
#include <asm/uaccess.h>
#include <linux/netdevice.h>
#include <net/net_namespace.h>
#include <linux/fs.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/kallsyms.h>
#include <net/ipv6.h>
#include <net/transp_v6.h>

#define TOA_VERSION "2.0.0.0"

#define TOA_DBG(msg,...)			\
    do {						\
          if (0xff==dbg){printk(KERN_DEBUG "[DEBUG] TOA: comm:%s pid:%d " msg, current->comm, (int)current->pid, ##__VA_ARGS__);}       \
    } while (0)

#define TOA_INFO(msg...)			\
     do { \
          if(net_ratelimit()) \
               printk(KERN_INFO "TOA: " msg);\
     } while(0)

#define TCPOPT_REAL_CLIENTIP 200
/* |opcode|size|vmip+sport| = 1 + 1 + 6 */
#define TCPOLEN_REAL_CLIENTIP 8
#define TCPOPT_VM_VPCID 201
/* |opcode=1|opcode=1|opcode|size|vpcid| = 1 + 1 + 4 */
#define TCPOLEN_VM_VPCID 6
#define TCPOPT_VIP 202
/* |opcode|size|vip+vport| = 1 + 1 + 6 */
#define TCPOLEN_VIP 8

/* MUST be 4 bytes alignment */
struct toa_data {
	__u8 opcode;
	__u8 opsize;
	__u16 port;
	__u32 ip;
};

/* statistics about toa in proc /proc/net/toa_stat */
enum {
	SYN_RECV_SOCK_TOA_CNT = 1,
	SYN_RECV_SOCK_NO_TOA_CNT,
	GETNAME_TOA_OK_CNT,
	GETNAME_TOA_MISMATCH_CNT,
	GETNAME_TOA_BYPASS_CNT,
	GETNAME_TOA_EMPTY_CNT,
	GETNAME_VTOA_GET_CNT,
	GETNAME_VTOA_GET_ATTR_ERR_CNT,
	GETNAME_VTOA_GET_LOCKUP_ERR_CNT,
	GETNAME_VTOA_GET_NETLINK_ERR_CNT,
	GETNAME_VTOA_GETPEERNAME_IPV4_ERR_CNT,
	GETNAME_VTOA_GETPEERNAME_IPV6_ERR_CNT,
	TOA_STAT_LAST
};

struct toa_stats_entry {
	char *name;
	int entry;
};

#define TOA_STAT_ITEM(_name, _entry) { \
        .name = _name,            \
        .entry = _entry,          \
}

#define TOA_STAT_END {    \
        NULL,           \
        0,              \
}

struct toa_stat_mib {
	unsigned long mibs[TOA_STAT_LAST];
};

#define DEFINE_TOA_STAT(type, name)       \
        __typeof__(type) *name
#define TOA_INC_STATS(mib, field)         \
        (per_cpu_ptr(mib, smp_processor_id())->mibs[field]++)


#endif


