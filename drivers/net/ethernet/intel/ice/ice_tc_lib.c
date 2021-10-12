// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2021, Intel Corporation. */

#include "ice.h"
#include "ice_tc_lib.h"
#include "ice_lib.h"
#include "ice_fltr.h"

#ifdef HAVE_TC_SETUP_CLSFLOWER
/**
 * ice_detect_filter_conflict - detect filter conflict across TC
 * @pf: Pointer to PF structure
 * @tc_fltr: Pointer to TC flower filter structure
 *
 * This function detects filter mismatch type but using same port_number
 * across TC and allow/deny desired filter combination. Example is,
 * filter 1, dest_ip + dest_port (80) -> action is forward to TC 1
 * filter 2: dest_ip + src_port (80) -> action is forward to TC 2
 *
 * We do not want to support such config, to avoid situation where
 * packets are getting duplicated across both the TCs if incoming Rx
 * packet has same dest_ip + src_port (80) + dst_port (80).
 * Due to both filter being same high prio filter in HW, both rule
 * can match (whereas that is not expectation) and cause unexpected
 * packet mirroring.
 */
static int
ice_detect_filter_conflict(struct ice_pf *pf,
			   struct ice_tc_flower_fltr *tc_fltr)
{
	struct ice_tc_flower_lyr_2_4_hdrs *headers;
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_tc_flower_fltr *fltr;
	struct ice_tc_l4_hdr *l4_key;
	u16 sport = 0, dport = 0;

	/* header = outer header for non-tunnel filter,
	 * otherwise inner_headers
	 */
	headers = &tc_fltr->outer_headers;
	if (tc_fltr->flags & ICE_TC_FLWR_FIELD_TENANT_ID)
		headers = &tc_fltr->inner_headers;

	l4_key = &headers->l4_key;
	if (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_L4_PORT)
		sport = be16_to_cpu(l4_key->src_port);
	if (tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_L4_PORT)
		dport = be16_to_cpu(l4_key->dst_port);

	hlist_for_each_entry(fltr, &pf->tc_flower_fltr_list, tc_flower_node) {
		struct ice_tc_flower_lyr_2_4_hdrs *fltr_headers;
		struct ice_tc_l4_hdr *fltr_l4_key;
		u16 dst_port = 0, src_port = 0;

		/* if tc_class is same, skip, no check needed */
		if (fltr->action.tc_class == tc_fltr->action.tc_class)
			continue;

		/* if only either of them are set, skip it */
		if ((fltr->flags & ICE_TC_FLWR_FIELD_TENANT_ID) ^
		    (tc_fltr->flags & ICE_TC_FLWR_FIELD_TENANT_ID))
			continue;

		/* if this is tunnel filter, make sure tunnel ID is not same */
		if ((fltr->flags & ICE_TC_FLWR_FIELD_TENANT_ID) &&
		    (tc_fltr->flags & ICE_TC_FLWR_FIELD_TENANT_ID)) {
			if (fltr->tenant_id && tc_fltr->tenant_id &&
			    fltr->tenant_id == tc_fltr->tenant_id) {
				NL_SET_ERR_MSG_MOD(tc_fltr->extack,
						   "Unsupported filter combination across TC, filter exist with same tunnel key for other TC(see dmesg log)");
				dev_err(dev, "Unsupported filter combination across TC, TC %d has filter using same tunnel key (%u)\n",
					fltr->action.tc_class,
					be32_to_cpu(fltr->tenant_id));
				return -EOPNOTSUPP;
			}
		}

		fltr_headers = &fltr->outer_headers;
		if (fltr->flags & ICE_TC_FLWR_FIELD_TENANT_ID)
			fltr_headers = &fltr->inner_headers;

		/* access L4 params */
		fltr_l4_key = &fltr_headers->l4_key;
		if (fltr->flags & ICE_TC_FLWR_FIELD_DEST_L4_PORT)
			dst_port = be16_to_cpu(fltr_l4_key->dst_port);
		if (fltr->flags & ICE_TC_FLWR_FIELD_SRC_L4_PORT)
			src_port = be16_to_cpu(fltr_l4_key->src_port);

		/* proceed only if tc_class is different and filter types
		 * are different but actual value(s) of say port number are
		 * same, flag warning to user.
		 * e.g if filter one is like dest port = 80 -> tc_class(1)
		 * and second filter is like, src_port = 80 -> tc_class(2)
		 * Invariably packet can match both the filter and user
		 * will get expected packet mirroring to both the destination
		 * (means tc_class(1) and tc_class(2)). To avoid such
		 * behavior, block user from adding such conficting filter
		 */
		if (tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_L4_PORT) {
			if (dport && dst_port && dport == dst_port) {
				NL_SET_ERR_MSG_MOD(tc_fltr->extack,
						   "Unsupported filter combination across TC, filter exist with same destination port for other TC, as destination port based filter(see dmesg log)");
				dev_err(dev, "Unsupported filter combination across TC, TC %d has filter using same port number (%u) as destination port based filter. This is to avoid unexpected packet mirroring.\n",
					fltr->action.tc_class, dst_port);
				return -EOPNOTSUPP;
			}
			if (dport && src_port && dport == src_port) {
				NL_SET_ERR_MSG_MOD(tc_fltr->extack,
						   "Unsupported filter combination across TC, filter exist with same destination port for other TC, as source port based filter(see dmesg log)");
				dev_err(dev, "Unsupported filter combination across TC, TC %d has filter using same port number (%u) as source port based filter. This is to avoid unexpected packet mirroring.\n",
					fltr->action.tc_class, src_port);
				return -EOPNOTSUPP;
			}
		}

		if (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_L4_PORT)  {
			if (sport && dst_port && sport == dst_port) {
				NL_SET_ERR_MSG_MOD(tc_fltr->extack,
						   "Unsupported filter combination across TC, filter exist with same source port for other TC, as destination port based filter (see dmesg log)");
				dev_err(dev, "Unsupported filter combination across TC, TC %d has filter using same port number (%u) as destination port based filter. This is to avoid unexpected packet mirroring.\n",
					fltr->action.tc_class, dst_port);
				return -EOPNOTSUPP;
			}
			if (sport && src_port && sport == src_port) {
				NL_SET_ERR_MSG_MOD(tc_fltr->extack,
						   "Unsupported filter combination across TC, filter exist with same source port for other TC, as source port based filter (see dmesg log)");
				dev_err(dev, "Unsupported filter combination across TC, TC %d has filter using same port number (%u) as source port based filter. This is to avoid unexpected packet mirroring.\n",
					fltr->action.tc_class, src_port);
				return -EOPNOTSUPP;
			}
		}
	}

	return 0;
}

/**
 * ice_chnl_fltr_type_chk - filter type check
 * @pf: Pointer to PF
 * @tc_fltr: Pointer to TC flower filter structure
 * @final_fltr_type: Ptr to filter type (dest/src/dest+src port)
 *
 * This function is used to determine if given filter (based on input params)
 * should be allowed or not. For a given channel (aka ADQ VSI), supported
 * filter types are src port, dest port , src+dest port. SO this function
 * checks if any filter exist for specified channel (if so, channel specific
 * filter_type will be set), and see if it matches with the filter being added.
 * It returns 0 (upon success) or POSIX error code
 */
static int
ice_chnl_fltr_type_chk(struct ice_pf *pf, struct ice_tc_flower_fltr *tc_fltr,
		       enum ice_channel_fltr_type *final_fltr_type)
{
	enum ice_channel_fltr_type fltr_type = *final_fltr_type;
	struct device *dev = ice_pf_to_dev(pf);

	if (fltr_type == ICE_CHNL_FLTR_TYPE_INVALID) {
		/* L4 based filter, more granular, hence should be checked
		 * beore L3
		 */
		if ((tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_L4_PORT) &&
		    (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_L4_PORT))
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_DEST_PORT;
		else if (tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_L4_PORT)
			fltr_type = ICE_CHNL_FLTR_TYPE_DEST_PORT;
		else if (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_L4_PORT)
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_PORT;
		/* L3 (IPv4) based filter check */
		else if ((tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV4) &&
			 (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV4))
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_DEST_IPV4;
		else if (tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV4)
			fltr_type = ICE_CHNL_FLTR_TYPE_DEST_IPV4;
		else if (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV4)
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_IPV4;
		/* L3 (IPv6) based filter check */
		else if ((tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV6) &&
			 (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV6))
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_DEST_IPV6;
		else if (tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV6)
			fltr_type = ICE_CHNL_FLTR_TYPE_DEST_IPV6;
		else if (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV6)
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_IPV6;
		/* Tunnel filter check, inner criteria is open:
		 * any combination of inner L3 and/or L4
		 */
		else if (tc_fltr->flags & ICE_TC_FLWR_FIELD_TENANT_ID)
			fltr_type = ICE_CHNL_FLTR_TYPE_TENANT_ID;
		else
			return -EOPNOTSUPP;
	} else if (fltr_type == ICE_CHNL_FLTR_TYPE_SRC_PORT) {
		if ((tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_L4_PORT) &&
		    (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_L4_PORT)) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from SRC_PORT to SRC + DEST_PORT\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_DEST_PORT;
		} else if (tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_L4_PORT) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from SRC_PORT to DEST_PORT\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_DEST_PORT;
		}
	} else if (fltr_type == ICE_CHNL_FLTR_TYPE_DEST_PORT) {
		if ((tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_L4_PORT) &&
		    (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_L4_PORT)) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from DEST_PORT to SRC + DEST_PORT\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_DEST_PORT;
		} else if (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_L4_PORT) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from DEST_PORT to SRC_PORT\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_PORT;
		}
	} else if (fltr_type == ICE_CHNL_FLTR_TYPE_SRC_DEST_PORT) {
		/* must to have src/dest/src+dest port as part of filter
		 * criteria
		 */
		if ((!(tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_L4_PORT)) &&
		    (!(tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_L4_PORT)))
			return -EOPNOTSUPP;

		if ((tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_L4_PORT) &&
		    (!(tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_L4_PORT))) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from SRC+DEST_PORT to DEST_PORT\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_DEST_PORT;
		} else if ((tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_L4_PORT) &&
			   (!(tc_fltr->flags &
			      ICE_TC_FLWR_FIELD_DEST_L4_PORT))) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from SRC+DEST_PORT to SRC_PORT\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_PORT;
		}
	} else if (fltr_type == ICE_CHNL_FLTR_TYPE_TENANT_ID) {
		/* Now only allow filters which has VNI */
		if (!(tc_fltr->flags & ICE_TC_FLWR_FIELD_TENANT_ID))
			return -EOPNOTSUPP;
	} else if (fltr_type == ICE_CHNL_FLTR_TYPE_SRC_DEST_IPV4) {
		/* must to have src/dest/src+dest IPv4 addr as part of filter
		 * criteria
		 */
		if ((!(tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV4)) &&
		    (!(tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV4)))
			return -EOPNOTSUPP;

		if ((tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV4) &&
		    (!(tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV4))) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from SRC+DEST IPv4 addr to DEST IPv4 addr\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_DEST_IPV4;
		} else if ((tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV4) &&
			   (!(tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV4))) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from SRC+DEST IPv4 to SRC IPv4 addr\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_IPV4;
		}
	} else if (fltr_type == ICE_CHNL_FLTR_TYPE_DEST_IPV4) {
		if ((tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV4) &&
		    (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV4)) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from DEST IPv4 addr to SRC + DEST IPv4 addr\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_DEST_IPV4;
		} else if (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV4) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from DEST IPv4 addr to SRC IPv4 addr\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_IPV4;
		}
	} else if (fltr_type == ICE_CHNL_FLTR_TYPE_SRC_IPV4) {
		if ((tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV4) &&
		    (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV4)) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from SRC IPv4 addr to SRC + DEST IPv4 addr\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_DEST_IPV4;
		} else if (tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV4) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from SRC IPv4 addr to DEST IPv4 addr\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_DEST_IPV4;
		}
	} else if (fltr_type == ICE_CHNL_FLTR_TYPE_SRC_DEST_IPV6) {
		/* must to have src/dest/src+dest IPv6 addr as part of filter
		 * criteria
		 */
		if ((!(tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV6)) &&
		    (!(tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV6)))
			return -EOPNOTSUPP;

		if ((tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV6) &&
		    (!(tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV6))) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from SRC+DEST IPv6 addr to DEST IPv6 addr\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_DEST_IPV6;
		} else if ((tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV6) &&
			   (!(tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV6))) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from SRC+DEST IPv6 to SRC IPv6 addr\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_IPV6;
		}
	} else if (fltr_type == ICE_CHNL_FLTR_TYPE_DEST_IPV6) {
		if ((tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV6) &&
		    (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV6)) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from DEST IPv6 addr to SRC + DEST IPv6 addr\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_DEST_IPV6;
		} else if (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV6) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from DEST IPv6 addr to SRC IPv6 addr\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_IPV6;
		}
	} else if (fltr_type == ICE_CHNL_FLTR_TYPE_SRC_IPV6) {
		if ((tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV6) &&
		    (tc_fltr->flags & ICE_TC_FLWR_FIELD_SRC_IPV6)) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from SRC IPv6 addr to SRC + DEST IPv6 addr\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_SRC_DEST_IPV6;
		} else if (tc_fltr->flags & ICE_TC_FLWR_FIELD_DEST_IPV6) {
			dev_dbg(dev,
				"Changing filter type for action (tc_class %d) from SRC IPv6 addr to DEST IPv6 addr\n",
				tc_fltr->action.tc_class);
			fltr_type = ICE_CHNL_FLTR_TYPE_DEST_IPV6;
		}
	} else {
		return -EINVAL; /* unsupported filter type */
	}

	/* return the selected fltr_type */
	*final_fltr_type = fltr_type;

	return 0;
}

/**
 * ice_determine_gtp_tun_type - determine TUN type based on user params
 * @pf: Pointer to PF
 * @l4_proto : vale of L4 protocol type
 * @flags: TC filter flags
 * @rule_info: Pointer to rule_info structure
 *
 * Determine TUN type based on user input. For VxLAN and Geneve, it is
 * straight forward. But to detect, correct TUN type for GTP is
 * challenging because there is no native support for GTP in kernel
 * and user may want to filter on
 *          Outer UDP + GTP (optional) + Inner L3 + Inner L4
 * Actual API to add advanced switch filter expects caller to detect
 * and specify correct TUN type and based on TUN type, appropriate
 * type of rule is added in HW.
 */
static bool
ice_determine_gtp_tun_type(struct ice_pf *pf, u16 l4_proto, u32 flags,
			   struct ice_adv_rule_info *rule_info)
{
	u8 outer_ipv6 = 0, inner_ipv6 = 0;
	u8 outer_ipv4 = 0, inner_ipv4 = 0;

	/* if user specified enc IPv6 src/dest/src+dest IP */
	if (flags & (ICE_TC_FLWR_FIELD_ENC_DEST_IPV6 |
		     ICE_TC_FLWR_FIELD_ENC_SRC_IPV6))
		outer_ipv6 = 1;
	else if (flags & (ICE_TC_FLWR_FIELD_ENC_DEST_IPV4 |
				ICE_TC_FLWR_FIELD_ENC_SRC_IPV4))
		outer_ipv4 = 1;

	if (flags & (ICE_TC_FLWR_FIELD_DEST_IPV6 |
		     ICE_TC_FLWR_FIELD_SRC_IPV6))
		inner_ipv6 = 1;
	else if (flags & (ICE_TC_FLWR_FIELD_DEST_IPV4 |
			  ICE_TC_FLWR_FIELD_SRC_IPV4))
		inner_ipv4 = 1;
	else
		/* for GTP encap, specifying inner L3 is must at this point,
		 * inner L4 is optional
		 */
		return false;

	/* following block support various protocol combinations for GTP
	 * (at this pint we know that detected tunnel type is GTP based
	 * on outer UDP port (2152: GTP_U):
	 *     Outer IPv4 + Inner IPv4[6] + Inner TCP/UDP
	 *     Outer IPv4 + Inner IPv4[6]
	 *     Outer IPv6 + Inner IPv4[6] + Inner TCP/UDP
	 *     Outer IPv6 + Inner IPv4[6]
	 */
	if (!outer_ipv6 && !outer_ipv4) {
		if (inner_ipv4 && l4_proto == IPPROTO_TCP)
			rule_info->tun_type = ICE_SW_TUN_GTP_IPV4_TCP;
		else if (inner_ipv4 && l4_proto == IPPROTO_UDP)
			rule_info->tun_type = ICE_SW_TUN_GTP_IPV4_UDP;
		else if (inner_ipv6 && l4_proto == IPPROTO_TCP)
			rule_info->tun_type = ICE_SW_TUN_GTP_IPV6_TCP;
		else if (inner_ipv6 && l4_proto == IPPROTO_UDP)
			rule_info->tun_type = ICE_SW_TUN_GTP_IPV6_UDP;
		else if (inner_ipv4)
			rule_info->tun_type = ICE_SW_TUN_GTP_IPV4;
		else if (inner_ipv6)
			rule_info->tun_type = ICE_SW_TUN_GTP_IPV6;
		else
			/* no reason to proceed, error condition (must to
			 * specify inner L3 and/or inner L3 + inner L4)
			 */
			return false;
	} else if (outer_ipv4) {
		if (inner_ipv4 && l4_proto == IPPROTO_TCP)
			rule_info->tun_type = ICE_SW_TUN_IPV4_GTP_IPV4_TCP;
		else if (inner_ipv4 && l4_proto == IPPROTO_UDP)
			rule_info->tun_type = ICE_SW_TUN_IPV4_GTP_IPV4_UDP;
		else if (inner_ipv6 && l4_proto == IPPROTO_TCP)
			rule_info->tun_type = ICE_SW_TUN_IPV4_GTP_IPV6_TCP;
		else if (inner_ipv6 && l4_proto == IPPROTO_UDP)
			rule_info->tun_type = ICE_SW_TUN_IPV4_GTP_IPV6_UDP;
		else if (inner_ipv4)
			rule_info->tun_type = ICE_SW_TUN_IPV4_GTPU_IPV4;
		else if (inner_ipv6)
			rule_info->tun_type = ICE_SW_TUN_IPV4_GTPU_IPV6;
		else
			/* no reason to proceed, error condition (must to
			 * specify inner L3 and/or inner L3 + inner L4)
			 */
			return false;
	} else if (outer_ipv6) {
		if (inner_ipv4 && l4_proto == IPPROTO_TCP)
			rule_info->tun_type = ICE_SW_TUN_IPV6_GTP_IPV4_TCP;
		else if (inner_ipv4 && l4_proto == IPPROTO_UDP)
			rule_info->tun_type = ICE_SW_TUN_IPV6_GTP_IPV4_UDP;
		else if (inner_ipv6 && l4_proto == IPPROTO_TCP)
			rule_info->tun_type = ICE_SW_TUN_IPV6_GTP_IPV6_TCP;
		else if (inner_ipv6 && l4_proto == IPPROTO_UDP)
			rule_info->tun_type = ICE_SW_TUN_IPV6_GTP_IPV6_UDP;
		else if (inner_ipv4)
			rule_info->tun_type = ICE_SW_TUN_IPV6_GTPU_IPV4;
		else if (inner_ipv6)
			rule_info->tun_type = ICE_SW_TUN_IPV6_GTPU_IPV6;
		else
			/* no reason to proceed, error condition (must to
			 * specify inner L3 and/or inner L3 + inner L4)
			 */
			return false;
	}

	return true;
}

/**
 * ice_tc_count_lkups - determine lookup count for switch filter
 * @flags: tc-flower flags
 * @headers: Pointer to TC flower filter header structure
 * @fltr: Pointer to outer TC filter structure
 *
 * Determine lookup count based on TC flower input for switch filter.
 */
static int
ice_tc_count_lkups(u32 flags, struct ice_tc_flower_lyr_2_4_hdrs *headers,
		   struct ice_tc_flower_fltr *fltr)
{
	int lkups_cnt = 0;

	if (flags & ICE_TC_FLWR_FIELD_ETH_TYPE_ID)
		lkups_cnt++;

	/* is Tunnel ID specified */
	if (flags & ICE_TC_FLWR_FIELD_TENANT_ID) {
		/* For ADQ filter, outer DMAC gets added implictly */
		if (flags & ICE_TC_FLWR_FIELD_ENC_DST_MAC)
			lkups_cnt++;
		/* Copy outer L4 port for non-GTP tunnel */
		if (fltr->tunnel_type != TNL_GTP) {
			if (flags & ICE_TC_FLWR_FIELD_ENC_DEST_L4_PORT)
				if (headers->l3_key.ip_proto == IPPROTO_UDP)
					lkups_cnt++;
		}
		/* due to tunnel */
		lkups_cnt++;
	}

	/* is MAC fields specified? */
	if (flags & (ICE_TC_FLWR_FIELD_DST_MAC | ICE_TC_FLWR_FIELD_SRC_MAC))
		lkups_cnt++;

	/* is VLAN specified? */
	if (flags & ICE_TC_FLWR_FIELD_VLAN)
		lkups_cnt++;

	/* is IPv[4|6] fields specified? */
	if (flags & (ICE_TC_FLWR_FIELD_DEST_IPV4 | ICE_TC_FLWR_FIELD_SRC_IPV4))
		lkups_cnt++;
	else if (flags & (ICE_TC_FLWR_FIELD_DEST_IPV6 |
			  ICE_TC_FLWR_FIELD_SRC_IPV6))
		lkups_cnt++;

	/* is L4 (TCP/UDP/any other L4 protocol fields specified? */
	if (flags & (ICE_TC_FLWR_FIELD_DEST_L4_PORT |
		     ICE_TC_FLWR_FIELD_SRC_L4_PORT))
		lkups_cnt++;

	return lkups_cnt;
}

/**
 * ice_tc_fill_rules - fill filter rules based on tc fltr
 * @hw: pointer to hw structure
 * @flags: tc flower field flags
 * @tc_fltr: pointer to tc flower filter
 * @list: list of advance rule elements
 * @rule_info: pointer to information about rule
 * @l4_proto: pointer to information such as L4 proto type
 *
 * Fill ice_adv_lkup_elem list based on tc flower flags and
 * tc flower headers. This list should be used to add
 * advance filter in hardware.
 */
static int
ice_tc_fill_rules(struct ice_hw *hw, u32 flags,
		  struct ice_tc_flower_fltr *tc_fltr,
		  struct ice_adv_lkup_elem *list,
		  struct ice_adv_rule_info *rule_info,
		  u16 *l4_proto)
{
	struct ice_tc_flower_lyr_2_4_hdrs *headers = &tc_fltr->outer_headers;
	int i = 0;

	if (flags & ICE_TC_FLWR_FIELD_ETH_TYPE_ID) {
		list[i].type = ICE_ETYPE_OL;
		list[i].h_u.ethertype.ethtype_id = headers->l2_key.n_proto;
		list[i].m_u.ethertype.ethtype_id = headers->l2_mask.n_proto;
		i++;
	}

	/* copy L2 (MAC) fields, Outer UDP (in case of tunnel) port info */
	if (flags & ICE_TC_FLWR_FIELD_TENANT_ID) {
		u32 tenant_id;

		/* copy L2 (MAC) fields if specified, For tunnel outer DMAC
		 * is needed and supported and is part of outer_headers.dst_mac
		 * For VxLAN tunnel, supported ADQ filter config is:
		 * - Outer dest MAC + VNI + Inner IPv4 + Inner L4 ports
		 */
		if (flags & ICE_TC_FLWR_FIELD_ENC_DST_MAC) {
			list[i].type = ICE_MAC_OFOS;
			ether_addr_copy(list[i].h_u.eth_hdr.dst_addr,
					headers->l2_key.dst_mac);
			ether_addr_copy(list[i].m_u.eth_hdr.dst_addr,
					headers->l2_mask.dst_mac);
			i++;
		}
		/* copy outer UDP (enc_dst_port) only for non-GTP tunnel */
		if (tc_fltr->tunnel_type != TNL_GTP) {
			if ((flags & ICE_TC_FLWR_FIELD_ENC_DEST_L4_PORT) &&
			    headers->l3_key.ip_proto == IPPROTO_UDP) {
				list[i].type = ICE_UDP_OF;
				list[i].h_u.l4_hdr.dst_port =
					headers->l4_key.dst_port;
				list[i].m_u.l4_hdr.dst_port =
					headers->l4_mask.dst_port;
				i++;
			}
		}

		/* setup encap info in list elements such as VNI/encap key-id,
		 * mask, type of tunnel
		 */
		if (tc_fltr->tunnel_type == TNL_VXLAN)
			list[i].type = ICE_VXLAN;
		else if (tc_fltr->tunnel_type == TNL_GENEVE)
			list[i].type = ICE_GENEVE;
		else if (tc_fltr->tunnel_type == TNL_GTP)
			list[i].type = ICE_GTP;

		if (tc_fltr->tunnel_type == TNL_VXLAN ||
		    tc_fltr->tunnel_type == TNL_GENEVE) {
			tenant_id = be32_to_cpu(tc_fltr->tenant_id) << 8;
			list[i].h_u.tnl_hdr.vni = cpu_to_be32(tenant_id);
			if (tenant_id)
				/* 24 bit tunnel key: mask "\xff\xff\xff\x00" */
				memcpy(&list[i].m_u.tnl_hdr.vni,
				       "\xff\xff\xff\x00", 4);
			else
				memcpy(&list[i].m_u.tnl_hdr.vni,
				       "\x00\x00\x00\x00", 4);
		} else if (tc_fltr->tunnel_type == TNL_GTP) {
			tenant_id = be32_to_cpu(tc_fltr->tenant_id);
			list[i].h_u.gtp_hdr.teid = cpu_to_be32(tenant_id);
			if (tenant_id)
				/* 32 bit tunnel key: mask "\xff\xff\xff\xff" */
				memcpy(&list[i].m_u.gtp_hdr.teid,
				       "\xff\xff\xff\xff", 4);
			else
				memcpy(&list[i].m_u.gtp_hdr.teid,
				       "\x00\x00\x00x00", 4);
		}
		/* advance list index */
		i++;

		/* now access values from inner_headers such as inner MAC (if
		 * supported), inner IPv4[6], Inner L4 ports, hence update
		 * "headers" to point to inner_headers
		 */
		headers = &tc_fltr->inner_headers;
	} else {
		rule_info->tun_type = ICE_NON_TUN;
		/* copy L2 (MAC) fields, for non-tunnel case */
		if (flags & (ICE_TC_FLWR_FIELD_DST_MAC |
			     ICE_TC_FLWR_FIELD_SRC_MAC)) {
			struct ice_tc_l2_hdr *l2_key, *l2_mask;

			l2_key = &headers->l2_key;
			l2_mask = &headers->l2_mask;

			list[i].type = ICE_MAC_OFOS;
			if (flags & ICE_TC_FLWR_FIELD_DST_MAC) {
				ether_addr_copy(list[i].h_u.eth_hdr.dst_addr,
						l2_key->dst_mac);
				ether_addr_copy(list[i].m_u.eth_hdr.dst_addr,
						l2_mask->dst_mac);
			}
			if (flags & ICE_TC_FLWR_FIELD_SRC_MAC) {
				ether_addr_copy(list[i].h_u.eth_hdr.src_addr,
						l2_key->src_mac);
				ether_addr_copy(list[i].m_u.eth_hdr.src_addr,
						l2_mask->src_mac);
			}
			i++;
		}
	}

	/* copy VLAN info */
	if (flags & ICE_TC_FLWR_FIELD_VLAN) {
		list[i].type = ICE_VLAN_OFOS;
		list[i].h_u.vlan_hdr.vlan = headers->vlan_hdr.vlan_id;
		list[i].m_u.vlan_hdr.vlan = cpu_to_be16(0xFFFF);
		i++;
	}


	/* copy L3 (IPv[4|6]: src, dest) address */
	if (flags & (ICE_TC_FLWR_FIELD_DEST_IPV4 |
		     ICE_TC_FLWR_FIELD_SRC_IPV4)) {
		struct ice_tc_l3_hdr *l3_key, *l3_mask;

		/* For encap, Outer L3 and L4 based are not supported,
		 * hence if user specified L3, L4 fields, they are treated
		 * as inner L3 and L4 respectivelt
		 */
		if (flags & ICE_TC_FLWR_FIELD_TENANT_ID)
			list[i].type = ICE_IPV4_IL;
		else
			list[i].type = ICE_IPV4_OFOS;

		l3_key = &headers->l3_key;
		l3_mask = &headers->l3_mask;
		if (flags & ICE_TC_FLWR_FIELD_DEST_IPV4) {
			list[i].h_u.ipv4_hdr.dst_addr = l3_key->dst_ipv4;
			list[i].m_u.ipv4_hdr.dst_addr = l3_mask->dst_ipv4;
		}
		if (flags & ICE_TC_FLWR_FIELD_SRC_IPV4) {
			list[i].h_u.ipv4_hdr.src_addr = l3_key->src_ipv4;
			list[i].m_u.ipv4_hdr.src_addr = l3_mask->src_ipv4;
		}
		i++;
	} else if (flags & (ICE_TC_FLWR_FIELD_DEST_IPV6 |
			    ICE_TC_FLWR_FIELD_SRC_IPV6)) {
		struct ice_ipv6_hdr *ipv6_hdr, *ipv6_mask;
		struct ice_tc_l3_hdr *l3_key, *l3_mask;

		if (flags & ICE_TC_FLWR_FIELD_TENANT_ID)
			list[i].type = ICE_IPV6_IL;
		else
			list[i].type = ICE_IPV6_OFOS;
		ipv6_hdr = &list[i].h_u.ipv6_hdr;
		ipv6_mask = &list[i].m_u.ipv6_hdr;
		l3_key = &headers->l3_key;
		l3_mask = &headers->l3_mask;

		if (flags & ICE_TC_FLWR_FIELD_DEST_IPV6) {
			memcpy(&ipv6_hdr->dst_addr, &l3_key->dst_ipv6_addr,
			       sizeof(l3_key->dst_ipv6_addr));
			memcpy(&ipv6_mask->dst_addr, &l3_mask->dst_ipv6_addr,
			       sizeof(l3_mask->dst_ipv6_addr));
		}
		if (flags & ICE_TC_FLWR_FIELD_SRC_IPV6) {
			memcpy(&ipv6_hdr->src_addr, &l3_key->src_ipv6_addr,
			       sizeof(l3_key->src_ipv6_addr));
			memcpy(&ipv6_mask->src_addr, &l3_mask->src_ipv6_addr,
			       sizeof(l3_mask->src_ipv6_addr));
		}
		i++;
	}

	/* copy L4 (src, dest) port */
	if (flags & (ICE_TC_FLWR_FIELD_DEST_L4_PORT |
		     ICE_TC_FLWR_FIELD_SRC_L4_PORT)) {
		struct ice_tc_l4_hdr *l4_key, *l4_mask;
		u16 dst_port;

		l4_key = &headers->l4_key;
		l4_mask = &headers->l4_mask;
		dst_port = be16_to_cpu(l4_key->dst_port);
		if (headers->l3_key.ip_proto == IPPROTO_TCP) {
			list[i].type = ICE_TCP_IL;
			/* detected L4 proto is TCP */
			if (l4_proto)
				*l4_proto = IPPROTO_TCP;
		} else if (headers->l3_key.ip_proto == IPPROTO_UDP) {
			/* Check if UDP dst port is known as a tunnel port */
			if (ice_tunnel_port_in_use(hw, dst_port, NULL)) {
				list[i].type = ICE_UDP_OF;
				rule_info->tun_type = ICE_SW_TUN_VXLAN;
			} else {
				list[i].type = ICE_UDP_ILOS;
			}
			/* detected L4 proto is UDP */
			if (l4_proto)
				*l4_proto = IPPROTO_UDP;
		}
		if (flags & ICE_TC_FLWR_FIELD_DEST_L4_PORT) {
			list[i].h_u.l4_hdr.dst_port = l4_key->dst_port;
			list[i].m_u.l4_hdr.dst_port = l4_mask->dst_port;
		}
		if (flags & ICE_TC_FLWR_FIELD_SRC_L4_PORT) {
			list[i].h_u.l4_hdr.src_port = l4_key->src_port;
			list[i].m_u.l4_hdr.src_port = l4_mask->src_port;
		}
		i++;
	}

	return i;
}

#ifdef HAVE_TC_FLOW_RULE_INFRASTRUCTURE
static int ice_eswitch_tc_parse_action(struct ice_tc_flower_fltr *fltr,
				       struct flow_action_entry *act)
{
	struct ice_repr *repr;

	switch (act->id) {
	case FLOW_ACTION_DROP:
		fltr->action.fltr_act = ICE_DROP_PACKET;
		break;

	case FLOW_ACTION_REDIRECT:
		fltr->action.fltr_act = ICE_FWD_TO_VSI;

		if (ice_is_port_repr_netdev(act->dev)) {
			repr = ice_netdev_to_repr(act->dev);

			fltr->dest_vsi = repr->src_vsi;
			fltr->direction = ICE_ESWITCH_FLTR_INGRESS;
		} else if (netif_is_ice(act->dev)) {
			struct ice_netdev_priv *np = netdev_priv(act->dev);

			fltr->dest_vsi = np->vsi;
			fltr->direction = ICE_ESWITCH_FLTR_EGRESS;
		} else {
			NL_SET_ERR_MSG_MOD(fltr->extack,
					   "Unsupported netdevice in switchdev mode");
			return -EINVAL;
		}

		break;

	default:
		NL_SET_ERR_MSG_MOD(fltr->extack,
				   "Unsupported action in switchdev mode");
		return -EINVAL;
	}

	return 0;
}
#endif /* HAVE_TC_FLOW_RULE_INFRASTRUCTURE */

static int
ice_eswitch_add_tc_fltr(struct ice_vsi *vsi, struct ice_tc_flower_fltr *fltr)
{
	struct ice_tc_flower_lyr_2_4_hdrs *headers = &fltr->outer_headers;
	struct ice_adv_rule_info rule_info = { 0 };
	struct ice_rule_query_data rule_added;
	struct ice_adv_lkup_elem *list;
	struct ice_hw *hw = &vsi->back->hw;
	u32 flags = fltr->flags;
	enum ice_status status;
	int lkups_cnt;
	int ret = 0;
	int i;

	if (!flags || (flags & (ICE_TC_FLWR_FIELD_ENC_DEST_IPV4 |
				ICE_TC_FLWR_FIELD_ENC_SRC_IPV4 |
				ICE_TC_FLWR_FIELD_ENC_DEST_IPV6 |
				ICE_TC_FLWR_FIELD_ENC_SRC_IPV6 |
				ICE_TC_FLWR_FIELD_ENC_SRC_L4_PORT))) {
		NL_SET_ERR_MSG_MOD(fltr->extack,
				   "Unsupported encap field(s)");
		return -EOPNOTSUPP;
	}

	lkups_cnt = ice_tc_count_lkups(flags, headers, fltr);
	list = kcalloc(lkups_cnt, sizeof(*list), GFP_ATOMIC);
	if (!list)
		return -ENOMEM;

	i = ice_tc_fill_rules(hw, flags, fltr, list, &rule_info, NULL);
	if (i != lkups_cnt) {
		ret = -EINVAL;
		goto exit;
	}

	rule_info.sw_act.fltr_act = fltr->action.fltr_act;
	rule_info.sw_act.vsi_handle = fltr->dest_vsi->idx;
	rule_info.priority = 7;

	if (fltr->direction == ICE_ESWITCH_FLTR_INGRESS) {
		rule_info.sw_act.flag |= ICE_FLTR_RX;
		rule_info.sw_act.src = hw->pf_id;
		rule_info.rx = true;
	} else {
		rule_info.sw_act.flag |= ICE_FLTR_TX;
		rule_info.sw_act.src = vsi->idx;
		rule_info.rx = false;
	}

	/* specify the cookie as filter_rule_id */
	rule_info.fltr_rule_id = fltr->cookie;

	status = ice_add_adv_rule(hw, list, lkups_cnt, &rule_info, &rule_added);
	if (status == ICE_ERR_ALREADY_EXISTS) {
		NL_SET_ERR_MSG_MOD(fltr->extack,
				   "Unable to add filter because it already exist");
		ret = -EINVAL;
		goto exit;
	} else if (status) {
		NL_SET_ERR_MSG_MOD(fltr->extack,
				   "Unable to add filter due to error");
		ret = -EIO;
		goto exit;
	}

	/* store the output params, which are needed later for removing
	 * advanced switch filter
	 */
	fltr->rid = rule_added.rid;
	fltr->rule_id = rule_added.rule_id;

	if (fltr->direction == ICE_ESWITCH_FLTR_EGRESS) {
		if (ice_fltr_update_flags(vsi, fltr->rule_id, fltr->rid,
					  ICE_SINGLE_ACT_LAN_ENABLE))
			ice_rem_adv_rule_by_id(hw, &rule_added);
	}

exit:
	kfree(list);
	return ret;
}

/**
 * ice_add_tc_flower_adv_fltr - add appropriate filter rules
 * @vsi: Pointer to VSI
 * @tc_fltr: Pointer to TC flower filter structure
 *
 * based on filter parameters using Advance recipes supported
 * by OS package.
 */
int
ice_add_tc_flower_adv_fltr(struct ice_vsi *vsi,
			   struct ice_tc_flower_fltr *tc_fltr)
{
	struct ice_tc_flower_lyr_2_4_hdrs *headers = &tc_fltr->outer_headers;
	enum ice_channel_fltr_type fltr_type = ICE_CHNL_FLTR_TYPE_INVALID;
	struct ice_adv_rule_info rule_info = {0};
	struct ice_rule_query_data rule_added;
	struct ice_channel_vf *vf_ch = NULL;
	struct ice_adv_lkup_elem *list;
	struct ice_pf *pf = vsi->back;
	struct ice_hw *hw = &pf->hw;
	u32 flags = tc_fltr->flags;
	enum ice_status status;
	struct ice_vsi *ch_vsi;
	struct device *dev;
	struct ice_vf *vf;
	u16 lkups_cnt = 0;
	u16 l4_proto = 0;
	int ret = 0;
	u16 i = 0;

	dev = ice_pf_to_dev(pf);
	if (ice_is_safe_mode(pf)) {
		NL_SET_ERR_MSG_MOD(tc_fltr->extack,
				   "Unable to add filter because driver is in safe mode");
		return -EOPNOTSUPP;
	}

	if (!flags || (flags & (ICE_TC_FLWR_FIELD_ENC_DEST_IPV4 |
				ICE_TC_FLWR_FIELD_ENC_SRC_IPV4 |
				ICE_TC_FLWR_FIELD_ENC_DEST_IPV6 |
				ICE_TC_FLWR_FIELD_ENC_SRC_IPV6 |
				ICE_TC_FLWR_FIELD_ENC_SRC_L4_PORT))) {
		NL_SET_ERR_MSG_MOD(tc_fltr->extack,
				   "Unsupported encap field(s)");
		return -EOPNOTSUPP;
	}

	/* get the channel (aka ADQ VSI) */
	if (tc_fltr->dest_vsi)
		ch_vsi = tc_fltr->dest_vsi;
	else
		ch_vsi = vsi->tc_map_vsi[tc_fltr->action.tc_class];

	lkups_cnt = ice_tc_count_lkups(flags, headers, tc_fltr);
	list = kcalloc(lkups_cnt, sizeof(*list), GFP_ATOMIC);
	if (!list)
		return -ENOMEM;

	i = ice_tc_fill_rules(hw, flags, tc_fltr, list, &rule_info, &l4_proto);
	if (i != lkups_cnt) {
		ret = -EINVAL;
		goto exit;
	}

	/* Now determine correct TUN type of based on encap params */
	if ((flags & ICE_TC_FLWR_FIELD_TENANT_ID) &&
	    tc_fltr->tunnel_type == TNL_GTP) {
		if (!ice_determine_gtp_tun_type(pf, l4_proto, tc_fltr->flags,
						&rule_info)) {
			if (vsi->type == ICE_VSI_VF)
				dev_err(dev, "Unable to add filter because could not determine tun type, VSI %u, vf_id:%u\n",
					vsi->vsi_num, vsi->vf_id);
			else
				NL_SET_ERR_MSG_MOD(tc_fltr->extack,
						   "Unable to add filter because could not determine TUN type. ");
			ret = -EINVAL;
			goto exit;
		}
	}

	rule_info.sw_act.fltr_act = tc_fltr->action.fltr_act;
	if (tc_fltr->action.tc_class >= ICE_CHNL_START_TC) {
		if (!ch_vsi) {
			NL_SET_ERR_MSG_MOD(tc_fltr->extack,
					   "Unable to add filter because specified destination doesn't exist");
			ret = -EINVAL;
			goto exit;
		}

		/* dest_vsi is preset, means it is from virtchnl message */
		if (tc_fltr->dest_vsi) {
			if (vsi->type != ICE_VSI_VF ||
			    tc_fltr->dest_vsi->type != ICE_VSI_VF) {
				dev_err(dev, "Unexpected VSI(vf_id:%u) type: %u\n",
					vsi->vf_id, vsi->type);
				ret = -EINVAL;
				goto exit;
			}
			vf = &pf->vf[vsi->vf_id];
			if (!vf) {
				dev_err(dev, "VF is NULL for VSI->type: ICE_VF_VSI and vf_id %d\n",
					vsi->vf_id);
				ret = -EINVAL;
				goto exit;
			}
			vf_ch = &vf->ch[tc_fltr->action.tc_class];

			fltr_type = (enum ice_channel_fltr_type)
				    vf_ch->fltr_type;
		} else if (ch_vsi->ch) {
			fltr_type = ch_vsi->ch->fltr_type;
		} else {
			dev_err(dev, "Can't add switch rule, neither dest_vsi is valid now VSI channel but tc_class sepcified is %u\n",
				tc_fltr->action.tc_class);
			ret = -EINVAL;
			goto exit;
		}

		/* perform fltr_type check for channel (aka ADQ) VSI */
		ret = ice_chnl_fltr_type_chk(pf, tc_fltr, &fltr_type);
		if (ret) {
			NL_SET_ERR_MSG_MOD(tc_fltr->extack,
					   "Unable to add filter because filter type check failed");
			dev_err(dev, "Unable to add filter because filter type check failed");
			ret = -EINVAL;
			goto exit;
		}

		/* Code is applicable only for PF ADQ, for VF ADQ - such
		 * checks to be handled by VF driver
		 */
		if (ch_vsi && (ch_vsi->type == ICE_VSI_PF ||
			       ch_vsi->type == ICE_VSI_CHNL)) {
			ret = ice_detect_filter_conflict(pf, tc_fltr);
			if (ret)
				goto exit;
		}

		if (tc_fltr->dest_vsi) {
			if (vf_ch && !fltr_type)
				vf_ch->fltr_type = fltr_type;
		} else if (ch_vsi->ch) {
			ch_vsi->ch->fltr_type = fltr_type;
		}

		rule_info.sw_act.fltr_act = ICE_FWD_TO_VSI;
#ifdef __CHECKER__
		/* cppcheck-suppress nullPointerRedundantCheck */
#endif /* _CHECKER__ */
		rule_info.sw_act.vsi_handle = ch_vsi->idx;
		rule_info.priority = 7;

		rule_info.sw_act.src = hw->pf_id;
		rule_info.rx = true;

		dev_dbg(dev, "add switch rule for TC:%u vsi_idx:%u, lkups_cnt:%u\n",
			tc_fltr->action.tc_class,
			rule_info.sw_act.vsi_handle, lkups_cnt);
	} else {
		rule_info.sw_act.flag |= ICE_FLTR_TX;
		rule_info.sw_act.src = vsi->idx;
		rule_info.rx = false;
	}

	/* specify the cookie as filter_rule_id */
	rule_info.fltr_rule_id = tc_fltr->cookie;

	status = ice_add_adv_rule(hw, list, lkups_cnt, &rule_info, &rule_added);
	if (status == ICE_ERR_ALREADY_EXISTS) {
		NL_SET_ERR_MSG_MOD(tc_fltr->extack,
				   "Unable to add filter because it already exist");
		ret = -EINVAL;
		goto exit;
	} else if (status) {
		NL_SET_ERR_MSG_MOD(tc_fltr->extack,
				   "Unable to add filter due to error");
		ret = -EIO;
		goto exit;
	}

	/* store the output params, which are needed later for removing
	 * advanced switch filter
	 */
	tc_fltr->rid = rule_added.rid;
	tc_fltr->rule_id = rule_added.rule_id;
	if (tc_fltr->action.tc_class > 0 && ch_vsi) {
		/* For PF ADQ, VSI type is set as ICE_VSI_CHNL, and
		 * for PF ADQ filter, it is not yet set in tc_fltr,
		 * hence store the dest_vsi ptr in tc_fltr
		 */
		if (ch_vsi->type == ICE_VSI_CHNL)
			tc_fltr->dest_vsi = ch_vsi;
		/* keep track of advanced switch filter for
		 * destination VSI (channel VSI)
		 */
		ch_vsi->num_chnl_fltr++;
		/* in this case, dest_id is VSI handle (sw handle) */
		tc_fltr->dest_id = rule_added.vsi_handle;

		/* keeps track of channel filters for PF VSI */
		if (vsi->type == ICE_VSI_PF &&
		    (flags & (ICE_TC_FLWR_FIELD_DST_MAC |
			      ICE_TC_FLWR_FIELD_ENC_DST_MAC)))
			pf->num_dmac_chnl_fltrs++;
	}
	dev_dbg(dev, "added switch rule (lkups_cnt %u, flags 0x%x) for TC %u, rid %u, rule_id %u, vsi_idx %u\n",
		lkups_cnt, flags,
		tc_fltr->action.tc_class, rule_added.rid,
		rule_added.rule_id, rule_added.vsi_handle);
exit:
	kfree(list);
	return ret;
}

/**
 * ice_tc_set_ipv4 - Parse IPv4 addresses from TC flower filter
 * @match: Pointer to flow match structure
 * @fltr: Pointer to filter structure
 * @headers: inner or outer header fields
 * @is_encap: set true for tunnel IPv4 address
 */
static int
ice_tc_set_ipv4(struct flow_match_ipv4_addrs *match,
		struct ice_tc_flower_fltr *fltr,
		struct ice_tc_flower_lyr_2_4_hdrs *headers, bool is_encap)
{
	if (match->key->dst) {
		if (is_encap)
			fltr->flags |= ICE_TC_FLWR_FIELD_ENC_DEST_IPV4;
		else
			fltr->flags |= ICE_TC_FLWR_FIELD_DEST_IPV4;
		headers->l3_key.dst_ipv4 = match->key->dst;
		headers->l3_mask.dst_ipv4 = match->mask->dst;
	}
	if (match->key->src) {
		if (is_encap)
			fltr->flags |= ICE_TC_FLWR_FIELD_ENC_SRC_IPV4;
		else
			fltr->flags |= ICE_TC_FLWR_FIELD_SRC_IPV4;
		headers->l3_key.src_ipv4 = match->key->src;
		headers->l3_mask.src_ipv4 = match->mask->src;
	}
	return 0;
}

/**
 * ice_tc_set_ipv6 - Parse IPv6 addresses from TC flower filter
 * @match: Pointer to flow match structure
 * @fltr: Pointer to filter structure
 * @headers: inner or outer header fields
 * @is_encap: set true for tunnel IPv6 address
 */
static int
ice_tc_set_ipv6(struct flow_match_ipv6_addrs *match,
		struct ice_tc_flower_fltr *fltr,
		struct ice_tc_flower_lyr_2_4_hdrs *headers, bool is_encap)
{
	struct ice_tc_l3_hdr *l3_key, *l3_mask;

	/* src and dest IPV6 address should not be LOOPBACK
	 * (0:0:0:0:0:0:0:1), which can be represented as ::1
	 */
	if (ipv6_addr_loopback(&match->key->dst) ||
	    ipv6_addr_loopback(&match->key->src)) {
		NL_SET_ERR_MSG_MOD(fltr->extack, "Bad ipv6, addr is LOOPBACK");
		return -EINVAL;
	}
	/* if src/dest IPv6 address is *,* error */
	if (ipv6_addr_any(&match->mask->dst) &&
	    ipv6_addr_any(&match->mask->src)) {
		NL_SET_ERR_MSG_MOD(fltr->extack,
				   "Bad src/dest IPV6, addr is any");
		return -EINVAL;
	}
	if (!ipv6_addr_any(&match->mask->dst)) {
		if (is_encap)
			fltr->flags |= ICE_TC_FLWR_FIELD_ENC_DEST_IPV6;
		else
			fltr->flags |= ICE_TC_FLWR_FIELD_DEST_IPV6;
	}
	if (!ipv6_addr_any(&match->mask->src)) {
		if (is_encap)
			fltr->flags |= ICE_TC_FLWR_FIELD_ENC_SRC_IPV6;
		else
			fltr->flags |= ICE_TC_FLWR_FIELD_SRC_IPV6;
	}

	l3_key = &headers->l3_key;
	l3_mask = &headers->l3_mask;

	if (fltr->flags & (ICE_TC_FLWR_FIELD_ENC_SRC_IPV6 |
			   ICE_TC_FLWR_FIELD_SRC_IPV6)) {
		memcpy(&l3_key->src_ipv6_addr, &match->key->src.s6_addr,
		       sizeof(match->key->src.s6_addr));
		memcpy(&l3_mask->src_ipv6_addr, &match->mask->src.s6_addr,
		       sizeof(match->mask->src.s6_addr));
	}
	if (fltr->flags & (ICE_TC_FLWR_FIELD_ENC_DEST_IPV6 |
			   ICE_TC_FLWR_FIELD_DEST_IPV6)) {
		memcpy(&l3_key->dst_ipv6_addr, &match->key->dst.s6_addr,
		       sizeof(match->key->dst.s6_addr));
		memcpy(&l3_mask->dst_ipv6_addr, &match->mask->dst.s6_addr,
		       sizeof(match->mask->dst.s6_addr));
	}

	return 0;
}

/**
 * ice_tc_set_port - Parse ports from TC flower filter
 * @match: Flow match structure
 * @fltr: Pointer to filter structure
 * @headers: inner or outer header fields
 * @is_encap: set true for tunnel port
 */
static int
ice_tc_set_port(struct flow_match_ports match,
		struct ice_tc_flower_fltr *fltr,
		struct ice_tc_flower_lyr_2_4_hdrs *headers, bool is_encap)
{
	if (match.key->dst) {
		if (is_encap)
			fltr->flags |= ICE_TC_FLWR_FIELD_ENC_DEST_L4_PORT;
		else
			fltr->flags |= ICE_TC_FLWR_FIELD_DEST_L4_PORT;
		headers->l4_key.dst_port = match.key->dst;
		headers->l4_mask.dst_port = match.mask->dst;
	}
	if (match.key->src) {
		if (is_encap)
			fltr->flags |= ICE_TC_FLWR_FIELD_ENC_SRC_L4_PORT;
		else
			fltr->flags |= ICE_TC_FLWR_FIELD_SRC_L4_PORT;
		headers->l4_key.src_port = match.key->src;
		headers->l4_mask.src_port = match.mask->src;
	}
	return 0;
}

#if defined(HAVE_TC_FLOWER_ENC) && defined(HAVE_TC_INDIR_BLOCK)
/**
 * ice_is_tnl_gtp - detect if tunnel type is GTP or not
 * @tunnel_dev: ptr to tunnel device
 * @rule: ptr to flow_rule
 *
 * If curr_tnl_type is TNL_LAST and "flow_rule" is non-NULL, then
 * check if enc_dst_port is well known GTP port (2152)
 * if so - return true (indicating that tunnel type is GTP), otherwise false.
 */
static bool
ice_is_tnl_gtp(struct net_device *tunnel_dev,
	       struct flow_rule *rule)
{
	/* if flow_rule is non-NULL, proceed with detecting possibility
	 * of GTP tunnel. Unlike VXLAN and GENEVE, there is no such API
	 * like  netif_is_gtp since GTP is not natively supported in kernel
	 */
	if (rule && (!is_vlan_dev(tunnel_dev))) {
		struct flow_match_ports match;
		u16 enc_dst_port;

		if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_PORTS)) {
			netdev_err(tunnel_dev,
				   "Tunnel HW offload is not supported, ENC_PORTs are not specified\n");
			return false;
		}

		/* get ENC_PORTS info */
		flow_rule_match_enc_ports(rule, &match);
		enc_dst_port = be16_to_cpu(match.key->dst);

		/* Outer UDP port is GTP well known port,
		 * if 'enc_dst_port' matched with GTP wellknown port,
		 * return true from this function.
		 */
		if (enc_dst_port != ICE_GTP_TNL_WELLKNOWN_PORT) {
			netdev_err(tunnel_dev,
				   "Tunnel HW offload is not supported for non-GTP tunnel, ENC_DST_PORT is %u\n",
				   enc_dst_port);
			return false;
		}

		/* all checks passed including outer UDP port  to be qualified
		 * for GTP tunnel
		 */
		return true;
	}
	return false;
}

/**
 * ice_tc_tun_get_type - get the tunnel type
 * @tunnel_dev: ptr to tunnel device
 * @rule: ptr to flow_rule
 *
 * This function detects appropriate tunnel_type if specified device is
 * tunnel device such as vxlan/geneve othertwise it tries to detect
 * tunnel type based on outer GTP port (2152)
 */
int
ice_tc_tun_get_type(struct net_device *tunnel_dev,
		    struct flow_rule *rule)
{
#ifdef HAVE_VXLAN_TYPE
#if IS_ENABLED(CONFIG_VXLAN)
	if (netif_is_vxlan(tunnel_dev))
		return TNL_VXLAN;
#endif /* HAVE_VXLAN_TYPE */
#elif defined(HAVE_GENEVE_TYPE)
#if IS_ENABLED(CONFIG_GENEVE)
	if (netif_is_geneve(tunnel_dev))
		return TNL_GENEVE;
#endif
#endif /* HAVE_GENEVE_TYPE */
	/* detect possibility of GTP tunnel type based on input */
	if (ice_is_tnl_gtp(tunnel_dev, rule))
		return TNL_GTP;

	return TNL_LAST;
}

/**
 * ice_tc_tun_info - Parse and store tunnel info
 * @pf: ptr to PF device
 * @f: Pointer to struct flow_cls_offload
 * @fltr: Pointer to filter structure
 * @tunnel: type of tunnel (e.g. VxLAN, Geneve, GTP)
 *
 * Parse tunnel attributes such as tunnel_id and store them.
 */
static int
ice_tc_tun_info(struct ice_pf *pf, struct flow_cls_offload *f,
		struct ice_tc_flower_fltr *fltr,
		enum ice_tunnel_type tunnel)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);

	/* match on VNI */
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct device *dev = ice_pf_to_dev(pf);
		struct flow_match_enc_keyid enc_keyid;
		u32 key_id;

		flow_rule_match_enc_keyid(rule, &enc_keyid);
		if (!enc_keyid.mask->keyid) {
			dev_err(dev, "Bad mask for encap key_id 0x%04x, it must be non-zero\n",
				be32_to_cpu(enc_keyid.mask->keyid));
			return -EINVAL;
		}

		if (enc_keyid.mask->keyid !=
				cpu_to_be32(ICE_TC_FLOWER_MASK_32)) {
			dev_err(dev, "Bad mask value for encap key_id 0x%04x\n",
				be32_to_cpu(enc_keyid.mask->keyid));
			return -EINVAL;
		}

		key_id = be32_to_cpu(enc_keyid.key->keyid);
		if (tunnel == TNL_VXLAN || tunnel == TNL_GENEVE) {
			/* VNI is only 3 bytes, applicable for VXLAN/GENEVE */
			if (key_id > ICE_TC_FLOWER_VNI_MAX) {
				dev_err(dev, "VNI out of range : 0x%x\n",
					key_id);
				return -EINVAL;
			}
		}
		fltr->flags |= ICE_TC_FLWR_FIELD_TENANT_ID;
		fltr->tenant_id = enc_keyid.key->keyid;
	} else if (tunnel == TNL_GTP) {
		/* User didn't specify tunnel_key but indicated
		 * intention about GTP tunnel.
		 * For GTP tunnel, support for wild-card tunnel-ID
		 */
		fltr->flags |= ICE_TC_FLWR_FIELD_TENANT_ID;
		fltr->tenant_id = 0;
	}

	return 0;
}

/**
 * ice_tc_tun_parse - Parse tunnel attributes from TC flower filter
 * @filter_dev: Pointer to device on which filter is being added
 * @vsi: Pointer to VSI structure
 * @f: Pointer to struct flow_cls_offload
 * @fltr: Pointer to filter structure
 * @headers: inner or outer header fields
 */
static int
ice_tc_tun_parse(struct net_device *filter_dev, struct ice_vsi *vsi,
		 struct flow_cls_offload *f,
		 struct ice_tc_flower_fltr *fltr,
		 struct ice_tc_flower_lyr_2_4_hdrs *headers)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	enum ice_tunnel_type tunnel_type;
	struct ice_pf *pf = vsi->back;
	struct device *dev;
	int err = 0;

	dev = ice_pf_to_dev(pf);
	tunnel_type = ice_tc_tun_get_type(filter_dev, rule);

	/* VXLAN and GTP tunnel are supported now */
	if (tunnel_type == TNL_VXLAN || tunnel_type == TNL_GTP) {
		err = ice_tc_tun_info(pf, f, fltr, tunnel_type);
		if (err) {
			dev_err(dev, "Failed to parse tunnel (tunnel_type %u) attributes\n",
				tunnel_type);
			return err;
		}
	} else {
		dev_err(dev, "Tunnel HW offload is not supported for the tunnel type: %d\n",
			tunnel_type);
		return -EOPNOTSUPP;
	}
	fltr->tunnel_type = tunnel_type;
	headers->l3_key.ip_proto = IPPROTO_UDP;
	return err;
}

/**
 * ice_parse_tunnel_attr - Parse tunnel attributes from TC flower filter
 * @filter_dev: Pointer to device on which filter is being added
 * @vsi: Pointer to VSI structure
 * @f: Pointer to struct flow_cls_offload
 * @fltr: Pointer to filter structure
 * @headers: inner or outer header fields
 */
static int
ice_parse_tunnel_attr(struct net_device *filter_dev, struct ice_vsi *vsi,
		      struct flow_cls_offload *f,
		      struct ice_tc_flower_fltr *fltr,
		      struct ice_tc_flower_lyr_2_4_hdrs *headers)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct flow_match_control enc_control;
	int err;

	err = ice_tc_tun_parse(filter_dev, vsi, f, fltr, headers);
	if (err) {
		NL_SET_ERR_MSG_MOD(fltr->extack,
				   "failed to parse tunnel attributes");
		return err;
	}

	flow_rule_match_enc_control(rule, &enc_control);

	if (enc_control.key->addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_enc_ipv4_addrs(rule, &match);
		if (ice_tc_set_ipv4(&match, fltr, headers, true))
			return -EINVAL;
	} else if (enc_control.key->addr_type ==
					FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		struct flow_match_ipv6_addrs match;

		flow_rule_match_enc_ipv6_addrs(rule, &match);
		if (ice_tc_set_ipv6(&match, fltr, headers, true))
			return -EINVAL;
	}

#ifdef HAVE_TC_FLOWER_ENC_IP
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_IP)) {
		struct flow_match_ip match;

		flow_rule_match_enc_ip(rule, &match);
		headers->l3_key.tos = match.key->tos;
		headers->l3_key.ttl = match.key->ttl;
		headers->l3_mask.tos = match.mask->tos;
		headers->l3_mask.ttl = match.mask->ttl;
	}
#endif /* HAVE_TC_FLOWER_ENC_IP */

	if (fltr->tunnel_type == TNL_GTP &&
	    flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_enc_ports(rule, &match);
		/* store away outer L4 port info and mark it for tunnel */
		if (ice_tc_set_port(match, fltr, headers, true))
			return -EINVAL;
	}
	return 0;
}
#endif /* HAVE_TC_FLOWER_ENC && HAVE_TC_INDIR_BLOCK */

/**
 * ice_parse_cls_flower - Parse TC flower filters provided by kernel
 * @vsi: Pointer to the VSI
 * @filter_dev: Pointer to device on which filter is being added
 * @f: Pointer to struct flow_cls_offload
 * @fltr: Pointer to filter structure
 */
#ifdef HAVE_TC_INDIR_BLOCK
static int
ice_parse_cls_flower(struct net_device *filter_dev, struct ice_vsi *vsi,
		     struct flow_cls_offload *f,
		     struct ice_tc_flower_fltr *fltr)
#else
static int
ice_parse_cls_flower(struct net_device __always_unused *filter_dev,
		     struct ice_vsi __always_unused *vsi,
		     struct tc_cls_flower_offload *f,
		     struct ice_tc_flower_fltr *fltr)
#endif /* HAVE_TC_INDIR_BLOCK */
{
	struct ice_tc_flower_lyr_2_4_hdrs *headers = &fltr->outer_headers;
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct flow_dissector *dissector = rule->match.dissector;
	u16 n_proto_mask = 0, n_proto_key = 0, addr_type = 0;

	if (dissector->used_keys &
	    ~(BIT(FLOW_DISSECTOR_KEY_CONTROL) |
	      BIT(FLOW_DISSECTOR_KEY_BASIC) |
	      BIT(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
#ifdef HAVE_TC_FLOWER_VLAN_IN_TAGS
	      BIT(FLOW_DISSECTOR_KEY_VLANID) |
#endif
#ifndef HAVE_TC_FLOWER_VLAN_IN_TAGS
	      BIT(FLOW_DISSECTOR_KEY_VLAN) |
#endif
	      BIT(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
#ifdef HAVE_TC_FLOWER_ENC
	      BIT(FLOW_DISSECTOR_KEY_ENC_KEYID) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_PORTS) |
	      BIT(FLOW_DISSECTOR_KEY_ENC_CONTROL) |
#ifdef HAVE_TC_FLOWER_ENC_IP
	      BIT(FLOW_DISSECTOR_KEY_ENC_IP) |
#endif /* HAVE_TC_FLOWER_ENC_IP */
#endif /* HAVE_TC_FLOWER_ENC */
	      BIT(FLOW_DISSECTOR_KEY_PORTS))) {
		NL_SET_ERR_MSG_MOD(fltr->extack, "Unsupported key used");
		return -EOPNOTSUPP;
	}

#if defined(HAVE_TC_FLOWER_ENC) && defined(HAVE_TC_INDIR_BLOCK)
	if ((flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS) ||
	     flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS) ||
	     flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_KEYID) ||
	     flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_PORTS))) {
		int err;

		err = ice_parse_tunnel_attr(filter_dev, vsi, f, fltr, headers);
		if (err) {
			NL_SET_ERR_MSG_MOD(fltr->extack,
					   "Failed to parse TC flower tunnel attributes");
			return err;
		}

		/* header pointers should point to the inner headers, outer
		 * header were already set by ice_parse_tunnel_attr
		 */
		headers = &fltr->inner_headers;
	} else {
		fltr->tunnel_type = TNL_LAST;
	}
#else /* HAVE_TC_FLOWER_ENC && HAVE_TC_INDIR_BLOCK */
	fltr->tunnel_type = TNL_LAST;
#endif /* HAVE_TC_FLOWER_ENC && HAVE_TC_INDIR_BLOCK */

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;

		flow_rule_match_basic(rule, &match);

		n_proto_key = ntohs(match.key->n_proto);
		n_proto_mask = ntohs(match.mask->n_proto);

		if (n_proto_key == ETH_P_ALL || n_proto_key == 0) {
			n_proto_key = 0;
			n_proto_mask = 0;
		} else {
			if (!ice_is_adq_active(vsi->back))
				fltr->flags |= ICE_TC_FLWR_FIELD_ETH_TYPE_ID;
		}

		headers->l2_key.n_proto = cpu_to_be16(n_proto_key);
		headers->l2_mask.n_proto = cpu_to_be16(n_proto_mask);
		headers->l3_key.ip_proto = match.key->ip_proto;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;

		flow_rule_match_eth_addrs(rule, &match);

		if (!is_zero_ether_addr(match.key->dst)) {
			ether_addr_copy(headers->l2_key.dst_mac,
					match.key->dst);
			ether_addr_copy(headers->l2_mask.dst_mac,
					match.mask->dst);
			fltr->flags |= ICE_TC_FLWR_FIELD_DST_MAC;
		}

		if (!is_zero_ether_addr(match.key->src)) {
			ether_addr_copy(headers->l2_key.src_mac,
					match.key->src);
			ether_addr_copy(headers->l2_mask.src_mac,
					match.mask->src);
			fltr->flags |= ICE_TC_FLWR_FIELD_SRC_MAC;
		}
	}

#ifdef HAVE_TC_FLOWER_VLAN_IN_TAGS
	if (dissector_uses_key(dissector, FLOW_DISSECTOR_KEY_VLANID)) {
		struct flow_dissector_key_tags *key =
			(struct flow_dissector_key_tags *)
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_VLANID,
						  f->key);
		struct flow_dissector_key_tags *mask =
			(struct flow_dissector_key_tags *)
			skb_flow_dissector_target(f->dissector,
						  FLOW_DISSECTOR_KEY_VLANID,
						  f->mask);

		if (mask->vlan_id) {
			if (mask->vlan_id == VLAN_VID_MASK) {
				fltr->flags |= ICE_TC_FLWR_FIELD_VLAN;
				fltr->flags &= ~ICE_TC_FLWR_FIELD_ETH_TYPE_ID;
			} else {
				NL_SET_ERR_MSG_MOD(fltr->extack,
						   "Bad VLAN mask");
				return -EINVAL;
			}
		}
		headers->vlan_hdr.vlan_id =
				cpu_to_be16(key->vlan_id & VLAN_VID_MASK);
#ifdef HAVE_FLOW_DISSECTOR_VLAN_PRIO
		if (mask->vlan_priority)
			headers->vlan_hdr.vlan_prio = key->vlan_priority;
#endif
	}
#else /* !HAVE_TC_FLOWER_VLAN_IN_TAGS */
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_VLAN) ||
	    is_vlan_dev(filter_dev)) {
		struct flow_dissector_key_vlan mask;
		struct flow_dissector_key_vlan key;
		struct flow_match_vlan match;

		if (is_vlan_dev(filter_dev)) {
			match.key = &key;
			match.key->vlan_id = vlan_dev_vlan_id(filter_dev);
			match.key->vlan_priority = 0;
			match.mask = &mask;
			memset(match.mask, 0xff, sizeof(*match.mask));
			match.mask->vlan_priority = 0;
		} else {
			flow_rule_match_vlan(rule, &match);
		}

		if (match.mask->vlan_id) {
			if (match.mask->vlan_id == VLAN_VID_MASK) {
				fltr->flags |= ICE_TC_FLWR_FIELD_VLAN;
				fltr->flags &= ~ICE_TC_FLWR_FIELD_ETH_TYPE_ID;
			} else {
				NL_SET_ERR_MSG_MOD(fltr->extack,
						   "Bad VLAN mask");
				return -EINVAL;
			}
		}

		headers->vlan_hdr.vlan_id =
				cpu_to_be16(match.key->vlan_id & VLAN_VID_MASK);
#ifdef HAVE_FLOW_DISSECTOR_VLAN_PRIO
		if (match.mask->vlan_priority)
			headers->vlan_hdr.vlan_prio = match.key->vlan_priority;
#endif
	}
#endif /* HAVE_TC_FLOWER_VLAN_IN_TAGS */

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_match_control match;

		flow_rule_match_control(rule, &match);

		addr_type = match.key->addr_type;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_ipv4_addrs(rule, &match);
		if (ice_tc_set_ipv4(&match, fltr, headers, false))
			return -EINVAL;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		struct flow_match_ipv6_addrs match;

		flow_rule_match_ipv6_addrs(rule, &match);
		if (ice_tc_set_ipv6(&match, fltr, headers, false))
			return -EINVAL;
	}

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(rule, &match);
		if (ice_tc_set_port(match, fltr, headers, false))
			return -EINVAL;
		switch (headers->l3_key.ip_proto) {
		case IPPROTO_TCP:
		case IPPROTO_UDP:
			break;
		default:
			NL_SET_ERR_MSG_MOD(fltr->extack,
					   "Only UDP and TCP transport are supported");
			return -EINVAL;
		}
	}
	return 0;
}

/**
 * ice_add_remove_tc_flower_dflt_fltr - add or remove default filter
 * @vsi: Pointer to VSI
 * @tc_fltr: Pointer to TC flower filter structure
 * @add: true if filter is being added.
 *
 * Add or remove default filter using default recipes to add MAC
 * or VLAN or MAC-VLAN filters.
 */
static int
ice_add_remove_tc_flower_dflt_fltr(struct ice_vsi *vsi,
				   struct ice_tc_flower_fltr *tc_fltr, bool add)
{
	struct ice_tc_flower_lyr_2_4_hdrs *headers = &tc_fltr->outer_headers;
	struct ice_vsi_vlan_ops *vlan_ops = ice_get_compat_vsi_vlan_ops(vsi);
	enum ice_sw_fwd_act_type act = tc_fltr->action.fltr_act;
	u16 vlan_id =  be16_to_cpu(headers->vlan_hdr.vlan_id);
	const u8 *dst_mac = headers->l2_key.dst_mac;
	int err;

	switch (tc_fltr->flags) {
	case ICE_TC_FLWR_FLTR_FLAGS_DST_MAC:
		if (add) {
			err = ice_fltr_add_mac(vsi, dst_mac, act);
			if (err)
				NL_SET_ERR_MSG_MOD(tc_fltr->extack,
						   "Could not add MAC filters");
		} else {
			err = ice_fltr_remove_mac(vsi, dst_mac, act);
			if (err)
				NL_SET_ERR_MSG_MOD(tc_fltr->extack,
						   "Could not remove MAC filters");
		}
		break;
	case ICE_TC_FLWR_FLTR_FLAGS_VLAN:
		if (add) {
			struct ice_vlan vlan =
				ICE_VLAN(ETH_P_8021Q, vlan_id, 0, act);
			err = vlan_ops->add_vlan(vsi, &vlan);
			if (err)
				NL_SET_ERR_MSG_MOD(tc_fltr->extack,
						   "Could not add VLAN filters");
		} else {
			struct ice_vlan vlan =
				ICE_VLAN(ETH_P_8021Q, vlan_id, 0, act);
			err = vlan_ops->del_vlan(vsi, &vlan);
			if (err)
				NL_SET_ERR_MSG_MOD(tc_fltr->extack,
						   "Could not delete VLAN filters");
		}
		break;
	case ICE_TC_FLWR_FLTR_FLAGS_DST_MAC_VLAN:
		if (add) {
			err = ice_fltr_add_mac_vlan(vsi, dst_mac, vlan_id, act);
			if (err)
				NL_SET_ERR_MSG_MOD(tc_fltr->extack,
						   "Could not add MAC VLAN filters");
		} else {
			err = ice_fltr_remove_mac_vlan(vsi, dst_mac, vlan_id,
						       act);
			if (err)
				NL_SET_ERR_MSG_MOD(tc_fltr->extack,
						   "Could not remove MAC VLAN filters");
		}
		break;
	default:
		NL_SET_ERR_MSG_MOD(tc_fltr->extack,
				   "Not a default filter type");
		err = -EOPNOTSUPP;
		break;
	}
	return err;
}

/**
 * ice_add_switch_fltr - Add TC flower filters
 * @vsi: Pointer to VSI
 * @fltr: Pointer to struct ice_tc_flower_fltr
 *
 * Add filter in HW switch block
 */
static int
ice_add_switch_fltr(struct ice_vsi *vsi, struct ice_tc_flower_fltr *fltr)
{
	if (ice_is_eswitch_mode_switchdev(vsi->back))
		return ice_eswitch_add_tc_fltr(vsi, fltr);

#ifdef HAVE_TC_CB_AND_SETUP_QDISC_MQPRIO
	if (fltr->action.fltr_act == ICE_FWD_TO_QGRP)
		return -EOPNOTSUPP;
#endif /* HAVE_TC_CB_AND_SETUP_QDISC_MQPRIO */
	if (fltr->flags == ICE_TC_FLWR_FLTR_FLAGS_DST_MAC ||
	    fltr->flags == ICE_TC_FLWR_FLTR_FLAGS_VLAN ||
	    fltr->flags == ICE_TC_FLWR_FLTR_FLAGS_DST_MAC_VLAN)
		return ice_add_remove_tc_flower_dflt_fltr(vsi, fltr, true);
#ifdef HAVE_TC_SETUP_CLSFLOWER
	return ice_add_tc_flower_adv_fltr(vsi, fltr);
#else
	return -EOPNOTSUPP;
#endif /* HAVE_TC_SETUP_CLSFLOWER */
}

#ifdef HAVE_TC_CB_AND_SETUP_QDISC_MQPRIO
/**
 * ice_handle_tclass_action - Support directing to a traffic class
 * @vsi: Pointer to VSI
 * @cls_flower: Pointer to TC flower offload structure
 * @fltr: Pointer to TC flower filter structure
 *
 * Support directing traffic to a traffic class
 */
static int
ice_handle_tclass_action(struct ice_vsi *vsi,
			 struct flow_cls_offload *cls_flower,
			 struct ice_tc_flower_fltr *fltr)
{
	int tc = tc_classid_to_hwtc(vsi->netdev, cls_flower->classid);
	struct ice_vsi *main_vsi;

	if (tc < 0) {
		NL_SET_ERR_MSG_MOD(fltr->extack,
				   "Unable to add filter because specified destination is invalid");
		return -EINVAL;
	}
	if (!tc) {
		NL_SET_ERR_MSG_MOD(fltr->extack,
				   "Unable to add filter because of invalid destination");
		return -EINVAL;
	}

	if (!(vsi->all_enatc & BIT(tc))) {
		NL_SET_ERR_MSG_MOD(fltr->extack,
				   "Unable to add filter because of non-existence destination");
		return -EINVAL;
	}

	/* Redirect to a TC class or Queue Group */
	main_vsi = ice_get_main_vsi(vsi->back);
	if (!main_vsi || !main_vsi->netdev) {
		NL_SET_ERR_MSG_MOD(fltr->extack,
				   "Unable to add filter because of invalid netdevice");
		return -EINVAL;
	}

	if ((fltr->flags & ICE_TC_FLWR_FIELD_TENANT_ID) &&
	    (fltr->flags & (ICE_TC_FLWR_FIELD_DST_MAC |
			   ICE_TC_FLWR_FIELD_SRC_MAC))) {
		NL_SET_ERR_MSG_MOD(fltr->extack,
				   "Unable to add filter because filter using tunnel key and inner MAC is unsupported combination");
		return -EOPNOTSUPP;
	}

	/* For ADQ, filter must include dest MAC address, otherwise unwanted
	 * packets with unrelated MAC address get delivered to ADQ VSIs as long
	 * as remaining filter criteria is satisfied such as dest IP address
	 * and dest/src L4 port. Following code is trying to handle:
	 * 1. For non-tunnel, if user specify MAC addresses, use them (means
	 * this code won't do anything
	 * 2. For non-tunnel, if user didn't specify MAC address, add implicit
	 * dest MAC to be lower netdev's active unicast MAC address
	 * 3. For tunnel,  as of now tc-filter thru flower classifier doesn't
	 * have provision for user to specify outer DMAC, hence driver to
	 * implicitly add outer dest MAC to be lower netdev's active unicast
	 * MAC address.
	 */
	if (fltr->flags & ICE_TC_FLWR_FIELD_TENANT_ID)  {
		if (!(fltr->flags & ICE_TC_FLWR_FIELD_ENC_DST_MAC)) {
			ether_addr_copy(fltr->outer_headers.l2_key.dst_mac,
					main_vsi->netdev->dev_addr);
			eth_broadcast_addr(fltr->outer_headers.l2_mask.dst_mac);
			fltr->flags |= ICE_TC_FLWR_FIELD_ENC_DST_MAC;
		}
	} else if (!(fltr->flags & ICE_TC_FLWR_FIELD_DST_MAC)) {
		ether_addr_copy(fltr->outer_headers.l2_key.dst_mac,
				main_vsi->netdev->dev_addr);
		eth_broadcast_addr(fltr->outer_headers.l2_mask.dst_mac);
		fltr->flags |= ICE_TC_FLWR_FIELD_DST_MAC;
	}

	/* validate specified dest MAC address, make sure either it belongs to
	 * lower netdev or any of non-offloaded MACVLAN. Non-offloaded MACVLANs
	 * MAC address are added as unicast MAC filter destined to main VSI.
	 */
	if (!ice_mac_fltr_exist(&main_vsi->back->hw,
				fltr->outer_headers.l2_key.dst_mac,
				main_vsi->idx)) {
		NL_SET_ERR_MSG_MOD(fltr->extack,
				   "Unable to add filter because legacy MAC filter for specified destination doesn't exist");
		return -EINVAL;
	}

	/* Make sure VLAN is already added to main VSI, before allowing ADQ to
	 * add a VLAN based filter such as MAC + VLAN + L4 port.
	 */
	if (fltr->flags & ICE_TC_FLWR_FIELD_VLAN) {
		u16 vlan_id = be16_to_cpu(fltr->outer_headers.vlan_hdr.vlan_id);

		if (!ice_vlan_fltr_exist(&main_vsi->back->hw, vlan_id,
					 main_vsi->idx)) {
			NL_SET_ERR_MSG_MOD(fltr->extack,
					   "Unable to add filter because legacy VLAN filter for specified destination doesn't exist");
			return -EINVAL;
		}
	}
	fltr->action.fltr_act = ICE_FWD_TO_VSI;
	fltr->action.tc_class = tc;

	return 0;
}
#endif /* HAVE_TC_CB_AND_SETUP_QDISC_MQPRIO */

/**
 * ice_parse_tc_flower_actions - Parse the actions for a TC filter
 * @vsi: Pointer to VSI
 * @cls_flower: Pointer to TC flower offload structure
 * @fltr: Pointer to TC flower filter structure
 *
 * Parse the actions for a TC filter
 */
static int
ice_parse_tc_flower_actions(struct ice_vsi *vsi,
			    struct flow_cls_offload *cls_flower,
			    struct ice_tc_flower_fltr *fltr)
{
#ifdef HAVE_TC_FLOW_RULE_INFRASTRUCTURE
	struct flow_rule *rule = flow_cls_offload_flow_rule(cls_flower);
	struct flow_action *flow_action = &rule->action;
	struct flow_action_entry *act;
	int i;
#else
	struct tcf_exts *exts = cls_flower->exts;
	struct tc_action *tc_act;
#if defined(HAVE_TCF_EXTS_FOR_EACH_ACTION)
	int i;
#else
	struct tc_action *temp;
	LIST_HEAD(tc_actions);
#endif
#endif /* HAVE_TC_FLOW_RULE_INFRASTRUCTURE */

#ifdef HAVE_TC_CB_AND_SETUP_QDISC_MQPRIO
	if (cls_flower->classid)
		return ice_handle_tclass_action(vsi, cls_flower, fltr);
#endif /* HAVE_TC_CB_AND_SETUP_QDISC_MQPRIO */

#ifdef HAVE_TC_FLOW_RULE_INFRASTRUCTURE
	if (!flow_action_has_entries(flow_action))
#elif defined(HAVE_NDO_SETUP_TC_REMOVE_TC_TO_NETDEV)
	if (!tcf_exts_has_actions(exts))
#else
	if (tc_no_actions(exts))
#endif
		return -EINVAL;

#ifdef HAVE_TC_FLOW_RULE_INFRASTRUCTURE
	flow_action_for_each(i, act, flow_action) {
#elif defined(HAVE_TCF_EXTS_FOR_EACH_ACTION)
	tcf_exts_for_each_action(i, tc_act, exts) {
#elif defined(HAVE_TCF_EXTS_TO_LIST)
	tcf_exts_to_list(exts, &tc_actions);

	list_for_each_entry_safe(tc_act, temp, &tc_actions, list) {
#else
	list_for_each_entry_safe(tc_act, temp, &(exts)->actions, list) {
#endif /* HAVE_TCF_EXTS_TO_LIST */
#ifdef HAVE_TC_FLOW_RULE_INFRASTRUCTURE
		if (ice_is_eswitch_mode_switchdev(vsi->back)) {
			int err =  ice_eswitch_tc_parse_action(fltr, act);

			if (err)
				return err;
		}
#else
		if (ice_is_eswitch_mode_switchdev(vsi->back))
			return -EINVAL;
#endif /* HAVE_TC_FLOW_RULE_INFRASTRUCTURE */
		/* Allow only one rule per filter */

		/* Drop action */
#ifdef HAVE_TC_FLOW_RULE_INFRASTRUCTURE
		if (act->id == FLOW_ACTION_DROP) {
#else
		if (is_tcf_gact_shot(tc_act)) {
#endif
			NL_SET_ERR_MSG_MOD(fltr->extack,
					   "Unsupported action DROP");
			return -EINVAL;
		}
		fltr->action.fltr_act = ICE_FWD_TO_VSI;
	}
	return 0;
}

/**
 * ice_del_tc_fltr - deletes a filter from HW table
 * @vsi: Pointer to VSI
 * @fltr: Pointer to struct ice_tc_flower_fltr
 *
 * This function deletes a filter from HW table and manages book-keeping
 */
static int ice_del_tc_fltr(struct ice_vsi *vsi, struct ice_tc_flower_fltr *fltr)
{
	struct ice_pf *pf = vsi->back;
	int err;

	if (fltr->flags == ICE_TC_FLWR_FLTR_FLAGS_DST_MAC ||
	    fltr->flags == ICE_TC_FLWR_FLTR_FLAGS_VLAN ||
	    fltr->flags == ICE_TC_FLWR_FLTR_FLAGS_DST_MAC_VLAN) {
		err = ice_add_remove_tc_flower_dflt_fltr(vsi, fltr, false);
	} else {
		struct ice_rule_query_data rule_rem;

		rule_rem.rid = fltr->rid;
		rule_rem.rule_id = fltr->rule_id;
		rule_rem.vsi_handle = fltr->dest_id;
		err = ice_rem_adv_rule_by_id(&pf->hw, &rule_rem);
	}

	if (err) {
		if (err == ICE_ERR_DOES_NOT_EXIST) {
			NL_SET_ERR_MSG_MOD(fltr->extack,
					   "filter does not exist\n");
			return -ENOENT;
		}
		NL_SET_ERR_MSG_MOD(fltr->extack,
				   "Failed to delete TC flower filter");
		return -EIO;
	}

	/* update advanced switch filter count for destination
	 * VSI if filter destination was VSI
	 */
	if (fltr->dest_vsi) {
		if (fltr->dest_vsi->type == ICE_VSI_CHNL) {
			struct ice_channel *ch = fltr->dest_vsi->ch;

			fltr->dest_vsi->num_chnl_fltr--;

			/* reset filter type for channel if channel filter
			 * count reaches zero
			 */
			if (!fltr->dest_vsi->num_chnl_fltr && ch)
				ch->fltr_type = ICE_CHNL_FLTR_TYPE_INVALID;

			/* keeps track of channel filters for PF VSI */
			if (vsi->type == ICE_VSI_PF &&
			    (fltr->flags & (ICE_TC_FLWR_FIELD_DST_MAC |
					    ICE_TC_FLWR_FIELD_ENC_DST_MAC)))
				pf->num_dmac_chnl_fltrs--;
		}
	}
	return 0;
}

/**
 * ice_add_tc_fltr - adds a TC flower filter
 * @netdev: Pointer to netdev
 * @vsi: Pointer to VSI
 * @f: Pointer to flower offload structure
 * @__fltr: Pointer to struct ice_tc_flower_fltr
 *
 * This function parses tc-flower input fields, parses action,
 * and adds a filter.
 */
#ifdef HAVE_TC_INDIR_BLOCK
static int
ice_add_tc_fltr(struct net_device *netdev, struct ice_vsi *vsi,
		struct flow_cls_offload *f,
		struct ice_tc_flower_fltr **__fltr)
#else
static int
ice_add_tc_fltr(struct net_device *netdev, struct ice_vsi *vsi,
		struct tc_cls_flower_offload *f,
		struct ice_tc_flower_fltr **__fltr)
#endif /* HAVE_TC_INDIR_BLOCK */
{
	struct ice_tc_flower_fltr *fltr;
	int err;

	/* by default, set output to be INVALID */
	*__fltr = NULL;

	fltr = kzalloc(sizeof(*fltr), GFP_KERNEL);
	if (!fltr)
		return -ENOMEM;

	fltr->cookie = f->cookie;
#ifdef HAVE_TC_FLOWER_OFFLOAD_COMMON_EXTACK
	fltr->extack = f->common.extack;
#endif
	fltr->src_vsi = vsi;
	INIT_HLIST_NODE(&fltr->tc_flower_node);

	err = ice_parse_cls_flower(netdev, vsi, f, fltr);
	if (err < 0)
		goto err;

	err = ice_parse_tc_flower_actions(vsi, f, fltr);
	if (err < 0)
		goto err;

	err = ice_add_switch_fltr(vsi, fltr);
	if (err < 0)
		goto err;

	/* return the newly created filter */
	*__fltr = fltr;

	return 0;
err:
	kfree(fltr);
	return err;
}

/**
 * ice_find_tc_flower_fltr - Find the TC flower filter in the list
 * @pf: Pointer to PF
 * @cookie: filter specific cookie
 */
static struct ice_tc_flower_fltr *
ice_find_tc_flower_fltr(struct ice_pf *pf, unsigned long cookie)
{
	struct ice_tc_flower_fltr *fltr;

	hlist_for_each_entry(fltr, &pf->tc_flower_fltr_list, tc_flower_node)
		if (cookie == fltr->cookie)
			return fltr;

	return NULL;
}

/**
 * ice_add_cls_flower - add TC flower filters
 * @netdev: Pointer to filter device
 * @vsi: Pointer to VSI
 * @cls_flower: Pointer to flower offload structure
 */
int
#ifdef HAVE_TC_INDIR_BLOCK
ice_add_cls_flower(struct net_device *netdev, struct ice_vsi *vsi,
		   struct flow_cls_offload *cls_flower)
#else
ice_add_cls_flower(struct net_device __always_unused *netdev,
		   struct ice_vsi *vsi,
		   struct tc_cls_flower_offload *cls_flower)
#endif /* HAVE_TC_INDIR_BLOCK */
{
#ifdef HAVE_TC_FLOWER_OFFLOAD_COMMON_EXTACK
	struct netlink_ext_ack *extack = cls_flower->common.extack;
#endif /* HAVE_TC_FLOWER_OFFLOAD_COMMON_EXTACK */
	struct net_device *vsi_netdev = vsi->netdev;
	struct ice_tc_flower_fltr *fltr;
	struct ice_pf *pf = vsi->back;
	int err = 0;

	if (ice_is_reset_in_progress(pf->state))
		return -EBUSY;
	if (test_bit(ICE_FLAG_FW_LLDP_AGENT, pf->flags))
		return -EINVAL;

#ifdef HAVE_TC_FLOW_INDIR_DEV
	if ((ice_tc_tun_get_type(netdev, NULL) == TNL_LAST) &&
	    ice_is_port_repr_netdev(netdev))
		vsi_netdev = netdev;
#else
	if (ice_is_port_repr_netdev(netdev))
		vsi_netdev = netdev;
#endif /* HAVE_TC_FLOW_INDIR_DEV */

	if (!(vsi_netdev->features & NETIF_F_HW_TC) &&
	    !test_bit(ICE_FLAG_CLS_FLOWER, pf->flags)) {
#ifdef HAVE_TC_FLOWER_OFFLOAD_COMMON_EXTACK
#ifdef HAVE_TC_INDIR_BLOCK
		/* Based on TC indirect notifications from kernel, all ice
		 * devices get an instance of rule from higher level device.
		 * Avoid triggering explicit error in this case.
		 */
		if (netdev == vsi_netdev)
			NL_SET_ERR_MSG_MOD(extack,
					   "can't apply TC flower filters, turn ON hw-tc-offload and try again");
#else
		NL_SET_ERR_MSG_MOD(extack,
				   "can't apply TC flower filters, turn ON hw-tc-offload and try again");
#endif /* HAVE_TC_INDIR_BLOCK */
#else  /* !HAVE_TC_FLOWER_OFFLOAD_COMMON_EXTACK */
		netdev_err(vsi_netdev,
			   "can't apply TC flower filters, turn ON hw-tc-offload and try again\n");
#endif /* HAVE_TC_FLOWER_OFFLOAD_COMMON_EXTACK */
		return -EINVAL;
	}

	/* avoid duplicate entries, if exists - return error */
	fltr = ice_find_tc_flower_fltr(pf, cls_flower->cookie);
	if (fltr) {
#ifdef HAVE_TC_FLOWER_OFFLOAD_COMMON_EXTACK
		NL_SET_ERR_MSG_MOD(extack,
				   "filter cookie already exists, ignoring");
#else
		netdev_warn(vsi_netdev,
			    "filter cookie %lx already exists, ignoring\n",
			    fltr->cookie);
#endif /* HAVE_TC_FLOWER_OFFLOAD_COMMON_EXTACK */
		return -EEXIST;
	}

	/* prep and add tc-flower filter in HW */
	err = ice_add_tc_fltr(netdev, vsi, cls_flower, &fltr);
	if (err)
		return err;

	/* add filter into an ordered list */
	hlist_add_head(&fltr->tc_flower_node, &pf->tc_flower_fltr_list);
	return 0;
}

/**
 * ice_del_cls_flower - delete TC flower filters
 * @vsi: Pointer to VSI
 * @cls_flower: Pointer to struct flow_cls_offload
 */
int
ice_del_cls_flower(struct ice_vsi *vsi, struct flow_cls_offload *cls_flower)
{
	struct ice_tc_flower_fltr *fltr;
	struct ice_pf *pf = vsi->back;
	int err;

	/* find filter */
	fltr = ice_find_tc_flower_fltr(pf, cls_flower->cookie);
	if (!fltr) {
		/* when egress qdisc is deleted, driver deletes all channel
		 * filters so that there are no stale filters left in
		 * HW (as per design) because deleting egress qdisc means,
		 * deleting all channel VSIs, hence no reason to keep filters
		 * destined to those channel VSIs. But software (OS) still
		 * sees those filters being offloaded in HW. In this situation
		 * user can try to delete those filters or OS will try to
		 * delete them one by one when ingress qdisc is deleted from
		 * given interace (ethX) and driver won't find those filters in
		 * its list of filters, hence don't return error. Return the
		 * error only when there are still active channel(s) and can't
		 * find requested filter and/or failed to delet the filter,
		 * otherwise return success
		 */
		/* means no channels are configured or channels are deleted and
		 * channel filter list is empty
		 */
		if (!test_bit(ICE_FLAG_TC_MQPRIO, pf->flags) &&
		    hlist_empty(&pf->tc_flower_fltr_list))
			return 0;

#ifdef HAVE_TC_FLOWER_OFFLOAD_COMMON_EXTACK
		NL_SET_ERR_MSG_MOD(cls_flower->common.extack,
				   "failed to delete TC flower filter because unable to find it");
#else
		dev_err(ice_pf_to_dev(pf),
			"failed to delete TC flower filter because unable to find it\n");
#endif
		return -EINVAL;
	}

#ifdef HAVE_TC_FLOWER_OFFLOAD_COMMON_EXTACK
	fltr->extack = cls_flower->common.extack;
#endif
	/* delete filter from HW */
	err = ice_del_tc_fltr(vsi, fltr);
	if (err)
		return err;

	/* delete filter from an ordered list */
	hlist_del(&fltr->tc_flower_node);

	/* free the filter node */
	kfree(fltr);

	return 0;
}

/**
 * ice_replay_tc_fltrs - replay tc filters
 * @pf: pointer to PF struct
 */
void ice_replay_tc_fltrs(struct ice_pf *pf)
{
	struct ice_tc_flower_fltr *fltr;
	struct hlist_node *node;

	hlist_for_each_entry_safe(fltr, node,
				  &pf->tc_flower_fltr_list,
				  tc_flower_node) {
		fltr->extack = NULL;
		ice_add_switch_fltr(fltr->src_vsi, fltr);
	}
}
#endif /* HAVE_TC_SETUP_CLSFLOWER */
