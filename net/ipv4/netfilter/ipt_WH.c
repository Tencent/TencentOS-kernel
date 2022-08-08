#include <linux/version.h>
#include <linux/module.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <linux/netfilter/x_tables.h>
#include <net/netfilter/nf_nat.h>
/*
 * add to mangle table,int OUTPUT phase put target ip,port info into tcp option。
 */
static unsigned int write_target_to_tcp_options(struct sk_buff *skb)
{
	struct iphdr *iph;
	struct tcphdr *tcph;
	char new_head[120];
	int hdr_len;
	char option_addr[8];
	uint32_t tmp_ip;
	uint16_t tmp_port;
	int data_len;
	char *d;

	iph = ip_hdr(skb);
	tcph = (struct tcphdr *)skb_transport_header(skb);
	if (skb->data[0] != 0x45 || iph->protocol != 0x06) {
		//no ipv4 and tcp packet，just ignore
		return 0;
	}
	if (tcph->syn != 1) {
		//not first packet，no need to modify header
		return 0;
	}
	if (skb_headroom(skb) < 22) {
		return 0;
	}

	hdr_len = (iph->ihl + tcph->doff) * 4;

	memcpy(new_head, skb->data, hdr_len);
	tmp_ip = htonl(iph->daddr);
	tmp_port = htons(tcph->dest);
	option_addr[0] = 0xfd;
	option_addr[1] = 0x08;
	memcpy(option_addr + 2, &tmp_ip, 4);
	memcpy(option_addr + 6, &tmp_port, 2);
	memcpy(new_head + hdr_len, option_addr, 8);
	d = new_head;
	skb_pull(skb, hdr_len);
	skb_push(skb, hdr_len + 8);
	memcpy(skb->data, new_head, hdr_len + 8);
	skb->transport_header = skb->transport_header - 8;
	skb->network_header = skb->network_header - 8;
	iph = ip_hdr(skb); //update iph point to new ip header
	iph->tot_len = htons(skb->len);
	iph->check = 0; //re-calculate ip checksum
	iph->check = ip_fast_csum(iph, iph->ihl);

	tcph = (struct tcphdr *)skb_transport_header(skb);
	tcph->doff = tcph->doff + 2;
	tcph->check = 0;
	data_len = (skb->len - iph->ihl * 4); //tcp segment length
	tcph->check =
		csum_tcpudp_magic(iph->saddr, iph->daddr, data_len,
				  iph->protocol,
				  csum_partial((char *)tcph, data_len, 0));
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	return 0;
}

static unsigned int xt_wh_target(struct sk_buff *skb,
				 const struct xt_action_param *par)
{
	write_target_to_tcp_options(skb);
	return NF_ACCEPT;
}

static struct xt_target xt_wh_target_reg __read_mostly = {
	.name = "WH",
	// .revision    = 1,
	.target = xt_wh_target,
	.targetsize = sizeof(int),
	.table = "mangle",
	.family = NFPROTO_IPV4,
	.hooks = (1 << NF_INET_LOCAL_OUT),
	.me = THIS_MODULE,
};

static int __init xt_nat_init(void)
{
	return xt_register_target(&xt_wh_target_reg);
}

static void __exit xt_nat_exit(void)
{
	xt_unregister_target(&xt_wh_target_reg);
}

module_init(xt_nat_init);
module_exit(xt_nat_exit);

MODULE_LICENSE("GPL");
