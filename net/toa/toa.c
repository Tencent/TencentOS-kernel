#include "toa.h"
#include <net/genetlink.h>
#include <net/inet_common.h>
#include <net/inet_connection_sock.h>
#include <net/inet_hashtables.h>

#define NIPQUAD_FMT "%u.%u.%u.%u"
#define NIPQUAD(addr) \
	((unsigned char *)&addr)[0], \
	((unsigned char *)&addr)[1], \
	((unsigned char *)&addr)[2], \
	((unsigned char *)&addr)[3]

/*
 * TOA	a new Tcp Option as Address,
 *	here address including IP and Port.
 *	the real {IP,Port} can be added into option field of TCP header,
 *	with LVS FULLNAT model, the realservice are still able to receive real {IP,Port} info.
 *	So far, this module only supports IPv4 and IPv6 mapped IPv4.
 *
 * Authors: 
 * 	Wen Li	<steel.mental@gmail.com>
 *	Yan Tian   <tianyan.7c00@gmail.com>
 *	Jiaming Wu <pukong.wjm@taobao.com>
 *	Jiajun Chen  <mofan.cjj@taobao.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 * 	2 of the License, or (at your option) any later version.
 *
 */
static int vtoa;
module_param(vtoa, int, 0600);
MODULE_PARM_DESC(vtoa, "vtoa module control.value can be 0 or 1 ;default is 0.");

#define  VTOA_GETPEERNAME_IPV4_DISABLE (0x1 << 0)
#define  VTOA_GETPEERNAME_IPV6_DISABLE (0x1 << 1)

static int disable_getpeername;
module_param(disable_getpeername, int, 0600);
MODULE_PARM_DESC(disable_getpeername, \
	"vtoa module control support getpeername.disable 1 ipv4, 2 ipv6, 3 ipv4 & ipv6;default is 0.");

static int dbg;
module_param(dbg, int, 0600);
MODULE_PARM_DESC(dbg, "debug log switch");

unsigned long sk_data_ready_addr = 0;

/*
 * Statistics of toa in proc /proc/net/toa_stats 
 */

struct toa_stats_entry toa_stats[] = {
	TOA_STAT_ITEM("syn_recv_sock_toa", SYN_RECV_SOCK_TOA_CNT),
	TOA_STAT_ITEM("syn_recv_sock_no_toa", SYN_RECV_SOCK_NO_TOA_CNT),
	TOA_STAT_ITEM("getname_toa_ok", GETNAME_TOA_OK_CNT),
	TOA_STAT_ITEM("getname_toa_mismatch", GETNAME_TOA_MISMATCH_CNT),
	TOA_STAT_ITEM("getname_toa_bypass", GETNAME_TOA_BYPASS_CNT),
	TOA_STAT_ITEM("getname_toa_empty", GETNAME_TOA_EMPTY_CNT),
	TOA_STAT_ITEM("vtoa_get", GETNAME_VTOA_GET_CNT),
	TOA_STAT_ITEM("vtoa_attr_err", GETNAME_VTOA_GET_ATTR_ERR_CNT),
	TOA_STAT_ITEM("vtoa_lookup_err", GETNAME_VTOA_GET_LOCKUP_ERR_CNT),
	TOA_STAT_ITEM("vtoa_netlink_err", GETNAME_VTOA_GET_NETLINK_ERR_CNT),
	TOA_STAT_ITEM("getname_vtoa_ipv4_err", GETNAME_VTOA_GETPEERNAME_IPV4_ERR_CNT),
	TOA_STAT_ITEM("getname_vtoa_ipv6_err", GETNAME_VTOA_GETPEERNAME_IPV6_ERR_CNT),
	TOA_STAT_END
};

DEFINE_TOA_STAT(struct toa_stat_mib, ext_stats);

enum {
	VTOA_CMD_UNSPEC,
	VTOA_CMD_GET,

	__VTOA_CMD_MAX
};
#define VTOA_CMD_MAX (__VTOA_CMD_MAX - 1)

enum {
	VTOA_ATTR_UNSPEC,
	VTOA_ATTR_REQ,
	
	VTOA_ATTR_I_VPCID,
	VTOA_ATTR_I_VMIP,
	VTOA_ATTR_I_SPORT,
	VTOA_ATTR_I_VIP,
	VTOA_ATTR_I_VPORT,
	__VTOA_ATTR_MAX
};
#define  VTOA_ATTR_MAX (__VTOA_ATTR_MAX - 1)

static struct nla_policy vtoa_policy[VTOA_ATTR_MAX + 1] =
{
	[VTOA_ATTR_REQ] = { .type = NLA_STRING},
	[VTOA_ATTR_I_VPCID] = { .type = NLA_U32},
	[VTOA_ATTR_I_VMIP] = { .type = NLA_U32},
	[VTOA_ATTR_I_SPORT] = { .type = NLA_U16},
	[VTOA_ATTR_I_VIP] = { .type = NLA_U32},
	[VTOA_ATTR_I_VPORT] = { .type = NLA_U16},
};
static struct genl_family vtoa_genl_family;

struct vtoa_req{
	u32 if_idx;
	__be32 sip;
	__be32 dip;
	__be16 sport;
	__be16 dport;
};

struct vtoa_info{
	u32 i_vpcid;
	__be32 i_vmip;
	__be32 i_vip;
	__be16 i_sport;
	__be16 i_vport;
};


/*
 * Funcs for vtoa hooks
 */

/* Parse TCP options in skb, try to get client ip, port, vpcid, vmip, vport
 * @param skb [in] received skb, it should be a ack/get-ack packet.
 * @return 0 if we don't get client ip/port , vpcid and vmip/vport;
 * 1 we get vtoa data;
 * -1 something wrong .
 */
static int get_vtoa_data(struct sk_buff *skb, struct sock *sk)
{
	struct tcphdr *th;
	int length;
	unsigned char *ptr;
	int is_toa = 0;

	/*TOA_DBG("get_vtoa_data called\n");*/

	if (NULL != skb) {
		th = tcp_hdr(skb);
		length = (th->doff * 4) - sizeof(struct tcphdr);
		ptr = (unsigned char *) (th + 1);

		while (length > 0) {
			int opcode = *ptr++;
			int opsize;
			switch (opcode) {
			case TCPOPT_EOL:
				return -1;
			case TCPOPT_NOP:/* Ref: RFC 793 section 3.1 */
				length--;
				continue;
			default:
				opsize = *ptr++;
				if (opsize < 2)	/* "silly options" */
					return -1;
				if (opsize > length)
					/* don't parse partial options */
					return -1;
				if (TCPOPT_REAL_CLIENTIP == opcode
					&& TCPOLEN_REAL_CLIENTIP == opsize) {
					sk->sk_tvpc_info.sport =
							*((__u16 *)(ptr));
					sk->sk_tvpc_info.vmip =
							*((__u32 *)(ptr + 2));
					is_toa++;
				} else if (TCPOPT_VM_VPCID == opcode
						&& TCPOLEN_VM_VPCID == opsize) {
					sk->sk_tvpc_info.vpcid =
							*((__u32 *)(ptr));
					is_toa++;
				} else if (TCPOPT_VIP == opcode
						&& TCPOLEN_VIP == opsize) {
					sk->sk_tvpc_info.vport =
						*((__u16 *)(ptr));
					sk->sk_tvpc_info.vip =
						*((__u32 *)(ptr + 2));
					is_toa++;
				}
				ptr += opsize - 2;
				length -= opsize;
			}
		}
		TOA_DBG("%s tcp source:%u dest:%u vpcid:%u vmip:%pI4 sport:%u vip:%pI4 vport:%u\n", 
			__func__, ntohs(th->source), ntohs(th->dest), sk->sk_tvpc_info.vpcid, 
			&sk->sk_tvpc_info.vmip, sk->sk_tvpc_info.sport, 
			&sk->sk_tvpc_info.vip, sk->sk_tvpc_info.vport);
	}
	return is_toa;
}

/* get client ip from socket
 * @param sock [in] the socket to getpeername() or getsockname()
 * @param uaddr [out] the place to put client ip, port
 * @param uaddr_len [out] lenth of @uaddr
 * @peer [in] if(peer), try to get remote address; if(!peer), try to get local address
 * @return return what the original inet_getname() returns.
 */
static int
inet_getname_vtoa(struct socket *sock,
		  struct sockaddr *uaddr,
		  int peer)
{
	int retval = 0;
	struct sock *sk = sock->sk;
	struct sockaddr_in *sin = (struct sockaddr_in *) uaddr;

	/* call orginal one */
	retval = inet_getname(sock, uaddr, peer);
	if (disable_getpeername & VTOA_GETPEERNAME_IPV4_DISABLE){
		TOA_INC_STATS(ext_stats, 
			GETNAME_VTOA_GETPEERNAME_IPV4_ERR_CNT);
		TOA_DBG("%s called, toa ipv getpeername disable\n", __func__);
		return retval;
	}
	
	TOA_DBG("%s called, sk_data_ready:%px retval:%d peer:%d vmip:%x sport:%u vip:%x vport:%u\n", 
		__func__, sk?sk->sk_data_ready:NULL, retval, peer, 
		sk?sk->sk_tvpc_info.vmip:0, sk?ntohs(sk->sk_tvpc_info.sport):0,
		sk?sk->sk_tvpc_info.vip:0, sk?ntohs(sk->sk_tvpc_info.vport):0);

	/* set our value if need */
	if (retval == sizeof(struct sockaddr_in) && peer && sk) {
		if (sk_data_ready_addr == (unsigned long) sk->sk_data_ready) {
			/*syscall accept must go here,
			 *so change the client ip/port and vip/vport here
			*/
			if (0 != sk->sk_tvpc_info.vmip
				&& 0 != sk->sk_tvpc_info.sport) {
				TOA_INC_STATS(ext_stats, GETNAME_TOA_OK_CNT);
				sin->sin_port = sk->sk_tvpc_info.sport;
				sin->sin_addr.s_addr = sk->sk_tvpc_info.vmip;
			} else if (0 == sk->sk_tvpc_info.vmip
				&& 0 == sk->sk_tvpc_info.sport) {
				TOA_INC_STATS(ext_stats,
						SYN_RECV_SOCK_NO_TOA_CNT);
				sk->sk_tvpc_info.sport = sin->sin_port;
				sk->sk_tvpc_info.vmip = sin->sin_addr.s_addr;
			} else
				TOA_INFO("vmip or sport can not 0.\n");

			/*do not get vip and vport,
			 *maybe vpcgw do not pass them,
			 *maybe it is a flow just for this host
			*/
			if (0 == sk->sk_tvpc_info.vip
				&& 0 == sk->sk_tvpc_info.vport) {
				struct sockaddr_in addr;
				if (inet_getname(sock, (struct sockaddr *)&addr,
					0) == sizeof(struct sockaddr_in)) {
					sk->sk_tvpc_info.vport = addr.sin_port;
					sk->sk_tvpc_info.vip =
						addr.sin_addr.s_addr;
				}
			}
		} else {
			TOA_INC_STATS(ext_stats, GETNAME_TOA_BYPASS_CNT);
		}
	} else { /* no need to get client ip */
		TOA_INC_STATS(ext_stats, GETNAME_TOA_EMPTY_CNT);
	}

	return retval;
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static int
inet6_getname_vtoa(struct socket *sock,
		struct sockaddr *uaddr,
		int peer)
{
	int retval = 0;
	struct sock *sk = sock->sk;
	struct sockaddr_in6 *sin = (struct sockaddr_in6 *) uaddr;
	
	/* call orginal one */
	retval = inet6_getname(sock, uaddr, peer);
	if (disable_getpeername & VTOA_GETPEERNAME_IPV6_DISABLE){
		TOA_INC_STATS(ext_stats, 
			GETNAME_VTOA_GETPEERNAME_IPV6_ERR_CNT);
		TOA_DBG("%s called, toa ipv6 getpeername disable\n", __func__);
		return retval;
	}

	TOA_DBG("%s called, sk_data_ready:%px retval:%d peer:%d vmip:%x sport:%u vip:%x vport:%u\n", 
		__func__, sk?sk->sk_data_ready:NULL, retval, peer, 
		sk?sk->sk_tvpc_info.vmip:0, sk?ntohs(sk->sk_tvpc_info.sport):0,
		sk?sk->sk_tvpc_info.vip:0, sk?ntohs(sk->sk_tvpc_info.vport):0);

	/* set our value if need */
	if (retval == sizeof(struct sockaddr_in6) && peer && sk) {
		if (sk_data_ready_addr == (unsigned long) sk->sk_data_ready) {
			if (0 != sk->sk_tvpc_info.vmip
				&& 0 != sk->sk_tvpc_info.sport) {
				TOA_INC_STATS(ext_stats, GETNAME_TOA_OK_CNT);
				sin->sin6_port = sk->sk_tvpc_info.sport;
				ipv6_addr_set(&sin->sin6_addr, 0, 0,
						htonl(0x0000FFFF),
						sk->sk_tvpc_info.vmip);
			} else if (0 == sk->sk_tvpc_info.vmip
				  && 0 == sk->sk_tvpc_info.sport) {
				TOA_INC_STATS(ext_stats,
					SYN_RECV_SOCK_NO_TOA_CNT);
				sk->sk_tvpc_info.sport = sin->sin6_port;
				sk->sk_tvpc_info.vmip =
					sin->sin6_addr.s6_addr32[3];
			} else
				TOA_INFO("vmip or sport can not 0.\n");

			if (0 == sk->sk_tvpc_info.vip
				&& 0 == sk->sk_tvpc_info.vport) {
				struct sockaddr_in6 addr;
				if (inet6_getname(sock,
					(struct sockaddr *)&addr,
					0) == sizeof(struct sockaddr_in6)) {
					sk->sk_tvpc_info.vport =
							addr.sin6_port;
					sk->sk_tvpc_info.vip =
						addr.sin6_addr.s6_addr32[3];
				}
			}
		} else {
			TOA_INC_STATS(ext_stats, GETNAME_TOA_BYPASS_CNT);
		}
	} else { /* no need to get client ip */
		TOA_INC_STATS(ext_stats, GETNAME_TOA_EMPTY_CNT);
	}

	return retval;
}
#endif

/* The three way handshake has completed - we got a valid synack -
 * now create the new socket.
 * We need to save toa data into the new socket.
 * @param sk [out]  the socket
 * @param skb [in] the ack/ack-get packet
 * @param req [in] the open request for this connection
 * @param dst [out] route cache entry
 * @return NULL if fail new socket if succeed.
 */
static struct sock *
tcp_v4_syn_recv_sock_vtoa(const struct sock *sk,
			struct sk_buff *skb,
			struct request_sock *req,
			struct dst_entry *dst,
			struct request_sock *req_unhash,
			bool *own_req)
{
	struct sock *newsock = NULL;
	int ret = 0;
	
	TOA_DBG("%s called\n", __func__);

	/* call orginal one */
	newsock = tcp_v4_syn_recv_sock(sk, skb, req, dst, req_unhash, own_req);
	/* set our value if need */
	if (NULL != newsock) {
		memset(&newsock->sk_tvpc_info, 0,
				sizeof(newsock->sk_tvpc_info));
		ret = get_vtoa_data(skb, newsock);
		if (ret > 0)
			TOA_INC_STATS(ext_stats, SYN_RECV_SOCK_TOA_CNT);
		else
			TOA_INC_STATS(ext_stats,
				SYN_RECV_SOCK_NO_TOA_CNT);
	}
	return newsock;
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static struct sock *
tcp_v6_syn_recv_sock_vtoa(const struct sock *sk,
			struct sk_buff *skb,
			struct request_sock *req,
			struct dst_entry *dst,
			struct request_sock *req_unhash,
			bool *own_req)
{
	struct sock *newsock = NULL;
	int ret = 0;
	TOA_DBG("%s called\n", __func__);

	/* call orginal one */
	newsock = tcp_v6_syn_recv_sock(sk, skb, req, dst, req_unhash, own_req);

	/* set our value if need */
	if (NULL != newsock) {
		memset(&newsock->sk_tvpc_info, 0,
			sizeof(newsock->sk_tvpc_info));
		ret = get_vtoa_data(skb, newsock);
		if (ret > 0)
			TOA_INC_STATS(ext_stats, SYN_RECV_SOCK_TOA_CNT);
		else
			TOA_INC_STATS(ext_stats,
					SYN_RECV_SOCK_NO_TOA_CNT);
	}
	return newsock;
}
#endif
 /*
 * Funcs for toa hooks 
 */

/* Parse TCP options in skb, try to get client ip, port
 * @param skb [in] received skb, it should be a ack/get-ack packet.
 * @return NULL if we don't get client ip/port;
 *         value of toa_data in ret_ptr if we get client ip/port.
 */
static void * get_toa_data(struct sk_buff *skb)
{
	struct tcphdr *th;
	int length;
	unsigned char *ptr;

	struct toa_data tdata;

	void *ret_ptr = NULL;

	//TOA_DBG("get_toa_data called\n");

	if (NULL != skb) {
		th = tcp_hdr(skb);
		length = (th->doff * 4) - sizeof (struct tcphdr);
		ptr = (unsigned char *) (th + 1);

		while (length > 0) {
			int opcode = *ptr++;
			int opsize;
			switch (opcode) {
			case TCPOPT_EOL:
				return NULL;
			case TCPOPT_NOP:	/* Ref: RFC 793 section 3.1 */
				length--;
				continue;
			default:
				opsize = *ptr++;
				if (opsize < 2)	/* "silly options" */
					return NULL;
				if (opsize > length)
					return NULL;	/* don't parse partial options */
				if (TCPOPT_REAL_CLIENTIP == opcode && TCPOLEN_REAL_CLIENTIP == opsize) {
					memcpy(&tdata, ptr - 2, sizeof (tdata));
					TOA_DBG("find toa data: ip = %u.%u.%u.%u, port = %u\n", NIPQUAD(tdata.ip),
						ntohs(tdata.port));
					memcpy(&ret_ptr, &tdata, sizeof (ret_ptr));
					TOA_DBG("coded toa data: %px\n", ret_ptr);
					return ret_ptr;
				}
				ptr += opsize - 2;
				length -= opsize;
			}
		}
	}
	return NULL;
}

/* get client ip from socket 
 * @param sock [in] the socket to getpeername() or getsockname()
 * @param uaddr [out] the place to put client ip, port
 * @param uaddr_len [out] lenth of @uaddr
 * @peer [in] if(peer), try to get remote address; if(!peer), try to get local address
 * @return return what the original inet_getname() returns.
 */
static int
inet_getname_toa(struct socket *sock, struct sockaddr *uaddr, int peer)
{
	int retval = 0;
	struct sock *sk = sock->sk;
	struct sockaddr_in *sin = (struct sockaddr_in *) uaddr;
	struct toa_data tdata;

	/* call orginal one */
	retval = inet_getname(sock, uaddr, peer);
	
	TOA_DBG("%s called, sk_user_data:%px sk_data_ready:%px retval:%d peer:%d\n", 
		__func__, sk?sk->sk_user_data:NULL, sk?sk->sk_data_ready:NULL, retval, peer);

	/* set our value if need */
	if (sk && retval == sizeof(struct sockaddr_in) && NULL != sk->sk_user_data && peer) {
		if (sk_data_ready_addr == (unsigned long) sk->sk_data_ready) {
			memcpy(&tdata, &sk->sk_user_data, sizeof (tdata));
			if (TCPOPT_REAL_CLIENTIP == tdata.opcode && TCPOLEN_REAL_CLIENTIP == tdata.opsize) {
				TOA_INC_STATS(ext_stats, GETNAME_TOA_OK_CNT);
				//TOA_DBG("inet_getname_toa: set new sockaddr, ip %u.%u.%u.%u -> %u.%u.%u.%u, port %u -> %u\n",
				//		NIPQUAD(sin->sin_addr.s_addr), NIPQUAD(tdata.ip), ntohs(sin->sin_port),
				//		ntohs(tdata.port));
				sin->sin_port = tdata.port;
				sin->sin_addr.s_addr = tdata.ip;
			} else { /* sk_user_data doesn't belong to us */
				TOA_INC_STATS(ext_stats, GETNAME_TOA_MISMATCH_CNT);
				//TOA_DBG("inet_getname_toa: invalid toa data, ip %u.%u.%u.%u port %u opcode %u opsize %u\n",
				//		NIPQUAD(tdata.ip), ntohs(tdata.port), tdata.opcode, tdata.opsize);
			}
		} else {
			TOA_INC_STATS(ext_stats, GETNAME_TOA_BYPASS_CNT);
		}
	} else { /* no need to get client ip */
		TOA_INC_STATS(ext_stats, GETNAME_TOA_EMPTY_CNT);
	} 

	return retval;
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static int
inet6_getname_toa(struct socket *sock, struct sockaddr *uaddr, int peer)
{
	int retval = 0;
	struct sock *sk = sock->sk;
	struct sockaddr_in6 *sin = (struct sockaddr_in6 *) uaddr;
	struct toa_data tdata;

	/* call orginal one */
	retval = inet6_getname(sock, uaddr, peer);

	TOA_DBG("%s called, sk_user_data:%px sk_data_ready:%px retval:%d peer:%d\n", 
		__func__, sk?sk->sk_user_data:NULL, sk?sk->sk_data_ready:NULL, retval, peer);

	/* set our value if need */
	if (sk && retval == sizeof(struct sockaddr_in6) && NULL != sk->sk_user_data && peer) {
		if (sk_data_ready_addr == (unsigned long) sk->sk_data_ready) {
			memcpy(&tdata, &sk->sk_user_data, sizeof (tdata));
			if (TCPOPT_REAL_CLIENTIP == tdata.opcode && TCPOLEN_REAL_CLIENTIP == tdata.opsize) {
				TOA_INC_STATS(ext_stats, GETNAME_TOA_OK_CNT);
				sin->sin6_port = tdata.port;
				ipv6_addr_set(&sin->sin6_addr, 0, 0, htonl(0x0000FFFF), tdata.ip);
			} else { /* sk_user_data doesn't belong to us */
				TOA_INC_STATS(ext_stats, GETNAME_TOA_MISMATCH_CNT);
			}
		} else {
			TOA_INC_STATS(ext_stats, GETNAME_TOA_BYPASS_CNT);
		}
	} else { /* no need to get client ip */
		TOA_INC_STATS(ext_stats, GETNAME_TOA_EMPTY_CNT);
	} 

	return retval;
}
#endif

/* The three way handshake has completed - we got a valid synack -
 * now create the new socket.
 * We need to save toa data into the new socket.
 * @param sk [out]  the socket
 * @param skb [in] the ack/ack-get packet
 * @param req [in] the open request for this connection
 * @param dst [out] route cache entry
 * @return NULL if fail new socket if succeed.
 */
static struct sock *
tcp_v4_syn_recv_sock_toa(const  struct sock *sk, struct sk_buff *skb,
				  struct request_sock *req,
				  struct dst_entry *dst,
				  struct request_sock *req_unhash,
				  bool *own_req)
{
	struct sock *newsock = NULL;

	/* call orginal one */
	newsock = tcp_v4_syn_recv_sock(sk, skb, req, dst, req_unhash, own_req);
	
	TOA_DBG("%s called, newsock:%px sk_user_data:%px\n", 
		__func__, newsock, newsock?newsock->sk_user_data:NULL);

	/* set our value if need */
	if (NULL != newsock && NULL == newsock->sk_user_data) {
		newsock->sk_user_data = get_toa_data(skb);
		if(NULL != newsock->sk_user_data){
			TOA_INC_STATS(ext_stats, SYN_RECV_SOCK_TOA_CNT);
		} else {
			TOA_INC_STATS(ext_stats, SYN_RECV_SOCK_NO_TOA_CNT);
		}
	}
	return newsock;
}

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
static struct sock *
tcp_v6_syn_recv_sock_toa(const struct sock *sk, struct sk_buff *skb,
					 struct request_sock *req,
					 struct dst_entry *dst,
					 struct request_sock *req_unhash,
					 bool *own_req)
{
	struct sock *newsock = NULL;

	/* call orginal one */
	newsock = tcp_v6_syn_recv_sock(sk, skb, req, dst, req_unhash, own_req);
	
	TOA_DBG("%s called, newsock:%px sk_user_data:%px\n", 
		__func__, newsock, newsock?newsock->sk_user_data:NULL);

	/* set our value if need */
	if (NULL != newsock && NULL == newsock->sk_user_data) {
		newsock->sk_user_data = get_toa_data(skb);
		if(NULL != newsock->sk_user_data){
			TOA_INC_STATS(ext_stats, SYN_RECV_SOCK_TOA_CNT);
		} else {
			TOA_INC_STATS(ext_stats, SYN_RECV_SOCK_NO_TOA_CNT);
		}
	}
	return newsock;
}
#endif

/*
 * HOOK FUNCS 
 */

/* replace the functions with our functions */
static inline int
hook_toa_functions(void)
{
	struct proto_ops *inet_stream_ops_p;
	struct proto_ops *inet6_stream_ops_p;
	struct inet_connection_sock_af_ops *ipv4_specific_p;
	struct inet_connection_sock_af_ops *ipv6_specific_p;

	/* hook inet_getname for ipv4 */
	inet_stream_ops_p = (struct proto_ops *)&inet_stream_ops;
	if (vtoa)
		inet_stream_ops_p->getname = inet_getname_vtoa;
	else
		inet_stream_ops_p->getname = inet_getname_toa;

	TOA_INFO("CPU [%u] hooked inet_getname <%px> --> <%px>\n", smp_processor_id(), inet_getname,
		 inet_stream_ops_p->getname);

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	/* hook inet6_getname for ipv6 */
	inet6_stream_ops_p = (struct proto_ops *)&inet6_stream_ops;
	if (vtoa)
		inet6_stream_ops_p->getname = inet6_getname_vtoa;
	else
		inet6_stream_ops_p->getname = inet6_getname_toa;

	TOA_INFO("CPU [%u] hooked inet6_getname <%px> --> <%px>\n", smp_processor_id(), inet6_getname,
		 inet6_stream_ops_p->getname);
#endif

	/* hook tcp_v4_syn_recv_sock for ipv4 */
	ipv4_specific_p = (struct inet_connection_sock_af_ops *)&ipv4_specific;
	if (vtoa)
		ipv4_specific_p->syn_recv_sock = tcp_v4_syn_recv_sock_vtoa;
	else
		ipv4_specific_p->syn_recv_sock = tcp_v4_syn_recv_sock_toa;

	TOA_INFO("CPU [%u] hooked tcp_v4_syn_recv_sock <%px> --> <%px>\n", smp_processor_id(), tcp_v4_syn_recv_sock,
		 ipv4_specific_p->syn_recv_sock);

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	/* hook tcp_v6_syn_recv_sock for ipv6 */
	ipv6_specific_p = (struct inet_connection_sock_af_ops *)&ipv6_specific;
	if (vtoa)
		ipv6_specific_p->syn_recv_sock = tcp_v6_syn_recv_sock_vtoa;
	else
		ipv6_specific_p->syn_recv_sock = tcp_v6_syn_recv_sock_toa;

	TOA_INFO("CPU [%u] hooked tcp_v6_syn_recv_sock <%px> --> <%px>\n", smp_processor_id(), tcp_v6_syn_recv_sock,
		 ipv6_specific_p->syn_recv_sock);
#endif

	return 0;
}

/* replace the functions to original ones */
static int
unhook_toa_functions(void)
{
        struct proto_ops *inet_stream_ops_p;
        struct proto_ops *inet6_stream_ops_p;
        struct inet_connection_sock_af_ops *ipv4_specific_p;
        struct inet_connection_sock_af_ops *ipv6_specific_p;

	/* unhook inet_getname for ipv4 */
	inet_stream_ops_p = (struct proto_ops *)&inet_stream_ops;
	inet_stream_ops_p->getname = inet_getname;
	TOA_INFO("CPU [%u] unhooked inet_getname\n", smp_processor_id());

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	/* unhook inet6_getname for ipv6 */
	inet6_stream_ops_p = (struct proto_ops *)&inet6_stream_ops;
	inet6_stream_ops_p->getname = inet6_getname;
	TOA_INFO("CPU [%u] unhooked inet6_getname\n", smp_processor_id());
#endif

	/* unhook tcp_v4_syn_recv_sock for ipv4 */
	ipv4_specific_p = (struct inet_connection_sock_af_ops *)&ipv4_specific;
	ipv4_specific_p->syn_recv_sock = tcp_v4_syn_recv_sock;
	TOA_INFO("CPU [%u] unhooked tcp_v4_syn_recv_sock\n", smp_processor_id());

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	/* unhook tcp_v6_syn_recv_sock for ipv6 */
	ipv6_specific_p = (struct inet_connection_sock_af_ops *)&ipv6_specific;
	ipv6_specific_p->syn_recv_sock = tcp_v6_syn_recv_sock;
	TOA_INFO("CPU [%u] unhooked tcp_v6_syn_recv_sock\n", smp_processor_id());
#endif

	return 0;
}

/*
 * Statistics of toa in proc /proc/net/toa_stats 
 */
static int toa_stats_show(struct seq_file *seq, void *v){
	int i, j;

	/* print CPU first */
	seq_printf(seq, "                                  ");
	for (i = 0; i < NR_CPUS; i++)
		if (cpu_online(i))
			seq_printf(seq, "CPU%d       ", i);
	seq_putc(seq, '\n');

	i = 0;
	while (NULL != toa_stats[i].name) {
		seq_printf(seq, "%-25s:", toa_stats[i].name);
		for (j = 0; j < NR_CPUS; j++) {
			if (cpu_online(j)) {
				seq_printf(seq, "%10lu ",
					   *(((unsigned long *) per_cpu_ptr(ext_stats, j)) + toa_stats[i].entry));
			}
		}
		seq_putc(seq, '\n');
		i++;
	}
	return 0;
}

static int toa_stats_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, toa_stats_show, NULL);
}

static const struct file_operations toa_stats_fops = {
	.owner = THIS_MODULE,
	.open = toa_stats_seq_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int vpc_inet_dump_one_sk(struct vtoa_req *req, struct vtoa_info *info)
{
	struct sock *sk;
	int err = -EINVAL;

	sk = inet_lookup(&init_net, &tcp_hashinfo, NULL, 0, req->sip,
			 req->sport, req->dip,
			 req->dport, req->if_idx);
	if (!sk){
		TOA_INFO("inet_lookup fail(%d), if_idx %u sip %pI4 dip %pI4 sport %u dport %u.\n", 
					err, req->if_idx, &req->sip, &req->dip,
					ntohs(req->sport), ntohs(req->dport));
		TOA_INC_STATS(ext_stats, GETNAME_VTOA_GET_LOCKUP_ERR_CNT);
		goto out_nosk;
	}

	if (sk->sk_state == TCP_LISTEN){
		TOA_INFO("socket listen fail(%d), if_idx %u sip %pI4 dip %pI4 sport %u dport %u sk_state %u.\n",
			err, req->if_idx, &req->sip, &req->dip,
			ntohs(req->sport), ntohs(req->dport), sk->sk_state);
		TOA_INC_STATS(ext_stats, GETNAME_VTOA_GET_LOCKUP_ERR_CNT);
		goto out_listen;
	}

	info->i_vpcid = sk->sk_tvpc_info.vpcid;
	info->i_vmip = sk->sk_tvpc_info.vmip;
	info->i_sport = sk->sk_tvpc_info.sport;
	info->i_vip = sk->sk_tvpc_info.vip;
	info->i_vport = sk->sk_tvpc_info.vport;
	err = 0;
	
out_listen:
	if (sk) {
		if (sk->sk_state == TCP_TIME_WAIT)
			inet_twsk_put((struct inet_timewait_sock *)sk);
		else
			sock_put(sk);
	}
out_nosk:
	return err;
}


static int vpc_vtoa_netlink_send(struct nlmsghdr *nlh, struct vtoa_info *info)
{
	struct sk_buff *skb;
	void *hdr;
	int err;
	
	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb) {
		TOA_INFO("nlmsg_new fail, vpcid %d vmip %pI4 sport %u vip %pI4 vport %u.\n", 
			info->i_vpcid, &info->i_vmip, ntohs(info->i_sport),
			&info->i_vip, ntohs(info->i_vport));
		TOA_INC_STATS(ext_stats, GETNAME_VTOA_GET_NETLINK_ERR_CNT);
		return -ENOMEM;
	}

	hdr = genlmsg_put(skb, nlh->nlmsg_pid, 0, &vtoa_genl_family, 0, VTOA_CMD_GET);
	if (!hdr){
		TOA_INFO("genlmsg_put fail, vpcid %d vmip %pI4 sport %u vip %pI4 vport %u.\n", 
			info->i_vpcid, &info->i_vmip, ntohs(info->i_sport),
			&info->i_vip, ntohs(info->i_vport));
		TOA_INC_STATS(ext_stats, GETNAME_VTOA_GET_NETLINK_ERR_CNT);
		nlmsg_free(skb);
		return -ENOMEM;
	}
	
	nla_put_u32(skb, VTOA_ATTR_I_VPCID, info->i_vpcid);
	nla_put_u32(skb, VTOA_ATTR_I_VMIP, info->i_vmip);
	nla_put_u16(skb, VTOA_ATTR_I_SPORT, info->i_sport);
	nla_put_u32(skb, VTOA_ATTR_I_VIP, info->i_vip);
	nla_put_u16(skb, VTOA_ATTR_I_VPORT, info->i_vport);
	genlmsg_end(skb, hdr);

	err = genlmsg_unicast(&init_net, skb, nlh->nlmsg_pid);
	if (err < 0){
		TOA_INFO("genlmsg_unicast fail(%d), vpcid %d vmip %pI4 sport %u vip %pI4 vport %u.\n", 
			err, 
			info->i_vpcid, &info->i_vmip, ntohs(info->i_sport),
			&info->i_vip, ntohs(info->i_vport));
		TOA_INC_STATS(ext_stats, GETNAME_VTOA_GET_NETLINK_ERR_CNT);
		return -EAGAIN;
	}

	return 0;
}

static int vpc_vtoa_get(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr **attrs = info->attrs;
	struct nlmsghdr *nlh = nlmsg_hdr(skb);
	struct vtoa_req *req;
	struct vtoa_info vinfo;
	int err;

	TOA_INC_STATS(ext_stats, GETNAME_VTOA_GET_CNT);
	
	if (!attrs || 
		!attrs[VTOA_ATTR_REQ]) {
		TOA_INFO("attrs null.\n");
		TOA_INC_STATS(ext_stats, GETNAME_VTOA_GET_ATTR_ERR_CNT);
		return -EINVAL;
	}
	if (nla_len(attrs[VTOA_ATTR_REQ]) != sizeof(struct vtoa_req)){
		TOA_INFO("attrs len(%u) err.\n", nla_len(attrs[VTOA_ATTR_REQ]));
		TOA_INC_STATS(ext_stats, GETNAME_VTOA_GET_ATTR_ERR_CNT);
		return -EINVAL;
	}

	req = nla_data(attrs[VTOA_ATTR_REQ]);

	memset(&vinfo, 0, sizeof(vinfo));
	err = vpc_inet_dump_one_sk(req, &vinfo);
	if (err != 0){
		return err;
	}
	
	err = vpc_vtoa_netlink_send(nlh, &vinfo);
	
	TOA_DBG("req[if_idx:%u sip:%pI4 dip:%pI4 sport:%u dport:%u] info["
		"vpcid:%u vmip:%pI4 sport:%u vip:%pI4 vport:%u] err:%d\n",
		req->if_idx, &req->sip, &req->dip,
		ntohs(req->sport), ntohs(req->dport), vinfo.i_vpcid, &vinfo.i_vmip,
		ntohs(vinfo.i_sport), &vinfo.i_vip, ntohs(vinfo.i_vport), err);
	
	return err;
}

static struct genl_ops vtoa_genl_ops[] =
{
	{
		.cmd = VTOA_CMD_GET,
		.flags = 0,
		.doit = vpc_vtoa_get,
		.dumpit = NULL,
	},
};

static struct genl_family vtoa_genl_family = {
	.hdrsize = 0,
	.name = "VPC_VTOA",
	.version = 1,
	.maxattr = VTOA_ATTR_MAX,
	.policy = vtoa_policy,
	.module = THIS_MODULE,
	.ops = vtoa_genl_ops,
	.n_ops = ARRAY_SIZE(vtoa_genl_ops),
};


/*
 * TOA module init and destory 
 */

/* module init */
	static int __init
toa_init(void)
{

	TOA_INFO("TOA " TOA_VERSION " by pukong.wjm\n");

	/* alloc statistics array for toa */
	if (NULL == (ext_stats = alloc_percpu(struct toa_stat_mib)))
		return 1;
	proc_create("toa_stats", 0, init_net.proc_net, &toa_stats_fops);

	/* get the address of function sock_def_readable
	 * so later we can know whether the sock is for rpc, tux or others 
	 */
	sk_data_ready_addr = kallsyms_lookup_name("sock_def_readable");
	TOA_INFO("CPU [%u] sk_data_ready_addr = kallsyms_lookup_name(sock_def_readable) = %lu\n", 
			smp_processor_id(), sk_data_ready_addr);
	if(0 == sk_data_ready_addr) {
		TOA_INFO("cannot find sock_def_readable.\n");
		goto err;
	}

	if (genl_register_family(&vtoa_genl_family) != 0){
		TOA_INFO("vtoa_genl_family register fail.\n");
		goto err;
	}

	/* hook funcs for parse and get toa */
	hook_toa_functions();

	TOA_INFO("toa loaded\n");
	return 0;

err:
	remove_proc_entry("toa_stats", init_net.proc_net);
	if (NULL != ext_stats) {
		free_percpu(ext_stats);
		ext_stats = NULL;
	}

	return 1;
}

/* module cleanup*/
static void __exit
toa_exit(void)
{
	unhook_toa_functions();
	synchronize_net();
	
	genl_unregister_family(&vtoa_genl_family);
	remove_proc_entry("toa_stats", init_net.proc_net);
	if (NULL != ext_stats) {
		free_percpu(ext_stats);
		ext_stats = NULL;
	}
	TOA_INFO("toa unloaded\n");
}

module_init(toa_init);
module_exit(toa_exit);
MODULE_LICENSE("GPL");

