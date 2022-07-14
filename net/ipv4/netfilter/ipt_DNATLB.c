// SPDX-License-Identifier: GPL-2.0-only
/*
 * (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2011 Patrick McHardy <kaber@trash.net>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <linux/netfilter/x_tables.h>
#include <net/netfilter/nf_nat.h>

#define MAX_DST_SIZE 10

struct nf_nat_ipv4_range_weight {
	unsigned int flags;
	__be32 min_ip;
	__be32 max_ip;
	union nf_conntrack_man_proto min;
	union nf_conntrack_man_proto max;
	unsigned short weight;
};

struct nf_nat_ipv4_range_weight_multi {
	unsigned int size;
	struct nf_nat_ipv4_range_weight range[MAX_DST_SIZE];
};

static int xt_nat_checkentry_v0(const struct xt_tgchk_param *par)
{
	return nf_ct_netns_get(par->net, par->family);
}

static void xt_nat_destroy(const struct xt_tgdtor_param *par)
{
	nf_ct_netns_put(par->net, par->family);
}

static void xt_nat_convert_range(struct nf_nat_range2 *dst,
				 const struct nf_nat_ipv4_range_weight *src)
{
	memset(&dst->min_addr, 0, sizeof(dst->min_addr));
	memset(&dst->max_addr, 0, sizeof(dst->max_addr));
	memset(&dst->base_proto, 0, sizeof(dst->base_proto));

	dst->flags = src->flags;
	dst->min_addr.ip = src->min_ip;
	dst->max_addr.ip = src->max_ip;
	dst->min_proto = src->min;
	dst->max_proto = src->max;
}

static unsigned int get_random_number(void)
{
    unsigned int rand_num;
    get_random_bytes(&rand_num, sizeof(unsigned int));
    return rand_num;
}

static const struct nf_nat_ipv4_range_weight *
get_range_lb(const struct nf_nat_ipv4_range_weight_multi *mr)
{
	int i = 0;
	unsigned int weight_acc = 0;
	unsigned int weight_mod = 0;
	unsigned int rand_num = 0;
	for (i = 0; i < mr->size; i++) {
		weight_acc += mr->range[i].weight;
	}
	rand_num = get_random_number();
	weight_mod = rand_num % weight_acc;
	weight_acc = 0;
	for (i = 0; i < mr->size; i++) {
		weight_acc += mr->range[i].weight;
		if (weight_mod <= weight_acc) {
			return &mr->range[i];
		}
	}
	return NULL;
}

static unsigned int xt_dnat_target_v0(struct sk_buff *skb,
				      const struct xt_action_param *par)
{
	const struct nf_nat_ipv4_range_weight_multi *mr = par->targinfo;
	struct nf_nat_range2 range;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;
	const struct nf_nat_ipv4_range_weight *range_weight;
	ct = nf_ct_get(skb, &ctinfo);
	WARN_ON(!(ct != NULL &&
		  (ctinfo == IP_CT_NEW || ctinfo == IP_CT_RELATED)));
	range_weight = get_range_lb(mr);
	if (range_weight == NULL) {
		return 0;
	}
	xt_nat_convert_range(&range, range_weight);
	return nf_nat_setup_info(ct, &range, NF_NAT_MANIP_DST);
}

static struct xt_target xt_nat_target_reg __read_mostly = {
	.name = "DNATLB",
	//.revision	= 0,
	.checkentry = xt_nat_checkentry_v0,
	.destroy = xt_nat_destroy,
	.target = xt_dnat_target_v0,
	.targetsize = sizeof(struct nf_nat_ipv4_range_weight_multi),
	.family = NFPROTO_IPV4,
	.table = "nat",
	.hooks = (1 << NF_INET_LOCAL_OUT),
	.me = THIS_MODULE,
};

static int __init

xt_nat_init(void)
{
	return xt_register_target(&xt_nat_target_reg);
}

static void __exit

xt_nat_exit(void)
{
	xt_unregister_target(&xt_nat_target_reg);
}

module_init(xt_nat_init);
module_exit(xt_nat_exit);
MODULE_LICENSE("GPL");
