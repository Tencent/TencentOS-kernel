/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2021, Intel Corporation. */

#ifndef _ICE_TC_LIB_H_
#define _ICE_TC_LIB_H_

#ifdef HAVE_TC_SETUP_CLSFLOWER
#define ICE_TC_FLWR_FIELD_DST_MAC		BIT(0)
#define ICE_TC_FLWR_FIELD_SRC_MAC		BIT(1)
#define ICE_TC_FLWR_FIELD_VLAN			BIT(2)
#define ICE_TC_FLWR_FIELD_DEST_IPV4		BIT(3)
#define ICE_TC_FLWR_FIELD_SRC_IPV4		BIT(4)
#define ICE_TC_FLWR_FIELD_DEST_IPV6		BIT(5)
#define ICE_TC_FLWR_FIELD_SRC_IPV6		BIT(6)
#define ICE_TC_FLWR_FIELD_DEST_L4_PORT		BIT(7)
#define ICE_TC_FLWR_FIELD_SRC_L4_PORT		BIT(8)
#define ICE_TC_FLWR_FIELD_TENANT_ID		BIT(9)
#define ICE_TC_FLWR_FIELD_ENC_DEST_IPV4		BIT(10)
#define ICE_TC_FLWR_FIELD_ENC_SRC_IPV4		BIT(11)
#define ICE_TC_FLWR_FIELD_ENC_DEST_IPV6		BIT(12)
#define ICE_TC_FLWR_FIELD_ENC_SRC_IPV6		BIT(13)
#define ICE_TC_FLWR_FIELD_ENC_DEST_L4_PORT	BIT(14)
#define ICE_TC_FLWR_FIELD_ENC_SRC_L4_PORT	BIT(15)
#define ICE_TC_FLWR_FIELD_ENC_DST_MAC		BIT(16)
#define ICE_TC_FLWR_FIELD_ETH_TYPE_ID		BIT(17)

/* TC flower supported filter match */
#define ICE_TC_FLWR_FLTR_FLAGS_DST_MAC		ICE_TC_FLWR_FIELD_DST_MAC
#define ICE_TC_FLWR_FLTR_FLAGS_VLAN		ICE_TC_FLWR_FIELD_VLAN
#define ICE_TC_FLWR_FLTR_FLAGS_DST_MAC_VLAN	(ICE_TC_FLWR_FIELD_DST_MAC | \
						 ICE_TC_FLWR_FIELD_VLAN)
#define ICE_TC_FLWR_FLTR_FLAGS_IPV4_DST_PORT	(ICE_TC_FLWR_FIELD_DEST_IPV4 | \
						 ICE_TC_FLWR_FIELD_DEST_L4_PORT)
#define ICE_TC_FLWR_FLTR_FLAGS_IPV4_SRC_PORT	(ICE_TC_FLWR_FIELD_DEST_IPV4 | \
						 ICE_TC_FLWR_FIELD_SRC_L4_PORT)
#define ICE_TC_FLWR_FLTR_FLAGS_IPV6_DST_PORT	(ICE_TC_FLWR_FIELD_DEST_IPV6 | \
						 ICE_TC_FLWR_FIELD_DEST_L4_PORT)
#define ICE_TC_FLWR_FLTR_FLAGS_IPV6_SRC_PORT	(ICE_TC_FLWR_FIELD_DEST_IPV6 | \
						 ICE_TC_FLWR_FIELD_SRC_L4_PORT)

#define ICE_TC_FLOWER_MASK_32	0xFFFFFFFF
#define ICE_TC_FLOWER_MASK_16	0xFFFF
#define ICE_TC_FLOWER_VNI_MAX	0xFFFFFFU

#ifdef HAVE_TC_INDIR_BLOCK
struct ice_indr_block_priv {
	struct net_device *netdev;
	struct ice_netdev_priv *np;
	struct list_head list;
};
#endif /* HAVE_TC_INDIR_BLOCK */

struct ice_tc_flower_action {
	u32 tc_class;
	enum ice_sw_fwd_act_type fltr_act;
};

struct ice_tc_vlan_hdr {
	__be16 vlan_id; /* Only last 12 bits valid */
#ifdef HAVE_FLOW_DISSECTOR_VLAN_PRIO
	u16 vlan_prio; /* Only last 3 bits valid (valid values: 0..7) */
#endif
};

struct ice_tc_l2_hdr {
	u8 dst_mac[ETH_ALEN];
	u8 src_mac[ETH_ALEN];
	__be16 n_proto;    /* Ethernet Protocol */
};

struct ice_tc_l3_hdr {
	u8 ip_proto;    /* IPPROTO value */
	union {
		struct {
			struct in_addr dst_ip;
			struct in_addr src_ip;
		} v4;
		struct {
			struct in6_addr dst_ip6;
			struct in6_addr src_ip6;
		} v6;
	} ip;
#define dst_ipv6	ip.v6.dst_ip6.s6_addr32
#define dst_ipv6_addr	ip.v6.dst_ip6.s6_addr
#define src_ipv6	ip.v6.src_ip6.s6_addr32
#define src_ipv6_addr	ip.v6.src_ip6.s6_addr
#define dst_ipv4	ip.v4.dst_ip.s_addr
#define src_ipv4	ip.v4.src_ip.s_addr

	u8 tos;
	u8 ttl;
};

struct ice_tc_l4_hdr {
	__be16 dst_port;
	__be16 src_port;
};

struct ice_tc_flower_lyr_2_4_hdrs {
	/* L2 layer fields with their mask */
	struct ice_tc_l2_hdr l2_key;
	struct ice_tc_l2_hdr l2_mask;
	struct ice_tc_vlan_hdr vlan_hdr;
	/* L3 (IPv4[6]) layer fields with their mask */
	struct ice_tc_l3_hdr l3_key;
	struct ice_tc_l3_hdr l3_mask;

	/* L4 layer fields with their mask */
	struct ice_tc_l4_hdr l4_key;
	struct ice_tc_l4_hdr l4_mask;
};

enum ice_eswitch_fltr_direction {
	ICE_ESWITCH_FLTR_INGRESS,
	ICE_ESWITCH_FLTR_EGRESS,
};

struct ice_tc_flower_fltr {
	struct hlist_node tc_flower_node;

	/* cookie becomes filter_rule_id if rule is added successfully */
	unsigned long cookie;

	/* add_adv_rule returns information like recipe ID, rule_id. Store
	 * those values since they are needed to remove advanced rule
	 */
	u16 rid;
	u16 rule_id;
	/* this could be queue/vsi_idx (sw handle)/queue_group, depending upon
	 * destination type
	 */
	u16 dest_id;
	/* if dest_id is vsi_idx, then need to store destination VSI ptr */
	struct ice_vsi *dest_vsi;
	/* direction of fltr for eswitch use case */
	enum ice_eswitch_fltr_direction direction;

	/* Parsed TC flower configuration params */
	struct ice_tc_flower_lyr_2_4_hdrs outer_headers;
	struct ice_tc_flower_lyr_2_4_hdrs inner_headers;
	struct ice_vsi *src_vsi;
	__be32 tenant_id;
	u32 flags;
#define ICE_TC_FLWR_TNL_TYPE_NONE        0xff
	u8 tunnel_type;
	struct ice_tc_flower_action	action;

	/* cache ptr which is used wherever needed to communicate netlink
	 * messages
	 */
	struct netlink_ext_ack *extack;
};

/**
 * ice_is_chnl_fltr - is this a valid channel filter
 * @f: Pointer to tc-flower filter
 *
 * Criteria to determine of given filter is valid channel filter
 * or not is based on its "destination". If destination is hw_tc (aka tc_class)
 * and it is non-zero, then it is valid channel (aka ADQ) filter
 */
static inline bool ice_is_chnl_fltr(struct ice_tc_flower_fltr *f)
{
	return !!f->action.tc_class;
}

/**
 * ice_chnl_dmac_fltr_cnt - DMAC based CHNL filter count
 * @pf: Pointer to PF
 */
static inline int ice_chnl_dmac_fltr_cnt(struct ice_pf *pf)
{
	return pf->num_dmac_chnl_fltrs;
}
int
ice_add_tc_flower_adv_fltr(struct ice_vsi *vsi,
			   struct ice_tc_flower_fltr *tc_fltr);
#if defined(HAVE_TC_FLOWER_ENC) && defined(HAVE_TC_INDIR_BLOCK)
int
ice_tc_tun_get_type(struct net_device *tunnel_dev,
		    struct flow_rule *rule);
#endif /* HAVE_TC_FLOWER_ENC && HAVE_TC_INDIR_BLOCK */
int
#ifdef HAVE_TC_INDIR_BLOCK
ice_add_cls_flower(struct net_device *netdev, struct ice_vsi *vsi,
		   struct flow_cls_offload *cls_flower);
#else
ice_add_cls_flower(struct net_device __always_unused *netdev,
		   struct ice_vsi *vsi,
		   struct tc_cls_flower_offload *cls_flower);
#endif /* HAVE_TC_INDIR_BLOCK */
int
ice_del_cls_flower(struct ice_vsi *vsi, struct flow_cls_offload *cls_flower);
void ice_replay_tc_fltrs(struct ice_pf *pf);
#endif /* HAVE_TC_SETUP_CLSFLOWER */

#endif /* _ICE_TC_LIB_H_ */
