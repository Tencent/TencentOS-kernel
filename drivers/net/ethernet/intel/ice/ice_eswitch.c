// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2021, Intel Corporation. */

#if IS_ENABLED(CONFIG_NET_DEVLINK)
#include "ice.h"
#include "ice_lib.h"
#include "ice_eswitch.h"
#include "ice_fltr.h"
#include "ice_repr.h"
#include "ice_devlink.h"
#include "ice_pf_vsi_vlan_ops.h"
#include "ice_tc_lib.h"

/**
 * ice_eswitch_setup_env - configure switchdev HW filters
 * @pf: pointer to PF struct
 *
 * This function adds HW filters configuration specific for switchdev
 * mode.
 */
static int ice_eswitch_setup_env(struct ice_pf *pf)
{
	struct ice_vsi *uplink_vsi = pf->switchdev.uplink_vsi;
	struct ice_vsi *ctrl_vsi = pf->switchdev.control_vsi;
	struct ice_port_info *pi = pf->hw.port_info;
	struct ice_vsi_vlan_ops *vlan_ops;
	bool rule_added = false;

	vlan_ops = ice_get_compat_vsi_vlan_ops(ctrl_vsi);
	if (vlan_ops->dis_stripping(ctrl_vsi) ||
	    vlan_ops->dis_insertion(ctrl_vsi))
		return -ENODEV;

	ice_remove_vsi_fltr(&pf->hw, uplink_vsi->idx);

	if (ice_vsi_add_vlan_zero(uplink_vsi))
		goto err_def_rx;

	if (!ice_is_dflt_vsi_in_use(uplink_vsi->vsw)) {
		if (ice_set_dflt_vsi(uplink_vsi->vsw, uplink_vsi))
			goto err_def_rx;
		rule_added = true;
	}

	if (ice_cfg_dflt_vsi(pi, ctrl_vsi->idx, true, ICE_FLTR_TX))
		goto err_def_tx;

	if (ice_vsi_update_security(uplink_vsi, ice_vsi_ctx_set_allow_override))
		goto err_override_uplink;

	if (ice_vsi_update_security(ctrl_vsi, ice_vsi_ctx_set_allow_override))
		goto err_override_control;

	if (ice_fltr_update_flags_dflt_rule(ctrl_vsi, pi->dflt_tx_vsi_rule_id,
					    ICE_FLTR_TX,
					    ICE_SINGLE_ACT_LB_ENABLE))
		goto err_update_action;

	return 0;

err_update_action:
	ice_vsi_update_security(ctrl_vsi, ice_vsi_ctx_clear_allow_override);
err_override_control:
	ice_vsi_update_security(uplink_vsi, ice_vsi_ctx_clear_allow_override);
err_override_uplink:
	ice_cfg_dflt_vsi(pi, ctrl_vsi->idx, false, ICE_FLTR_TX);
err_def_tx:
	if (rule_added)
		ice_clear_dflt_vsi(uplink_vsi->vsw);
err_def_rx:
	ice_fltr_add_mac_and_broadcast(uplink_vsi,
				       uplink_vsi->port_info->mac.perm_addr,
				       ICE_FWD_TO_VSI);
	return -ENODEV;
}

/**
 * ice_eswitch_release_env - clear switchdev HW filters
 * @pf: pointer to PF struct
 *
 * This function removes HW filters configuration specific for switchdev
 * mode and restores default legacy mode settings.
 */
static void
ice_eswitch_release_env(struct ice_pf *pf)
{
	struct ice_vsi *uplink_vsi = pf->switchdev.uplink_vsi;
	struct ice_vsi *ctrl_vsi = pf->switchdev.control_vsi;
	struct ice_port_info *pi = pf->hw.port_info;

	ice_vsi_update_security(ctrl_vsi, ice_vsi_ctx_clear_allow_override);
	ice_vsi_update_security(uplink_vsi, ice_vsi_ctx_clear_allow_override);
	ice_cfg_dflt_vsi(pi, ctrl_vsi->idx, false, ICE_FLTR_TX);
	ice_clear_dflt_vsi(uplink_vsi->vsw);
	ice_fltr_add_mac_and_broadcast(uplink_vsi,
				       uplink_vsi->port_info->mac.perm_addr,
				       ICE_FWD_TO_VSI);
}

#ifdef HAVE_METADATA_PORT_INFO
/**
 * ice_eswitch_remap_ring - reconfigure ring of switchdev ctrl VSI
 * @ring: pointer to ring
 * @q_vector: pointer of q_vector which is connected with this ring
 * @netdev: netdevice connected with this ring
 */
static void
ice_eswitch_remap_ring(struct ice_ring *ring, struct ice_q_vector *q_vector,
		       struct net_device *netdev)
{
	ring->q_vector = q_vector;
	ring->next = NULL;
	ring->netdev = netdev;
}

/**
 * ice_eswitch_remap_rings_to_vectors - reconfigure rings of switchdev ctrl VSI
 * @pf: pointer to PF struct
 *
 * In switchdev number of allocated Tx/Rx rings is equal.
 *
 * This function fills q_vectors structures associated with representator and
 * move each ring pairs to port representator netdevs. Each port representor
 * will have dedicated 1 Tx/Rx ring pair, so number of rings pair is equal to
 * number of VFs.
 */
static void
ice_eswitch_remap_rings_to_vectors(struct ice_pf *pf)
{
	struct ice_vsi *vsi = pf->switchdev.control_vsi;
	int q_id;

	ice_for_each_txq(vsi, q_id) {
		struct ice_repr *repr = pf->vf[q_id].repr;
		struct ice_q_vector *q_vector = repr->q_vector;
		struct ice_ring *tx_ring = vsi->tx_rings[q_id];
		struct ice_ring *rx_ring = vsi->rx_rings[q_id];

		q_vector->vsi = vsi;
		q_vector->reg_idx = vsi->q_vectors[0]->reg_idx;

		q_vector->num_ring_tx = 1;
		q_vector->tx.ring = tx_ring;
		ice_eswitch_remap_ring(tx_ring, q_vector, repr->netdev);
		/* In switchdev mode, from OS stack perspective, there is only
		 * one queue for given netdev, so it needs to be indexed as 0.
		 */
		tx_ring->q_index = 0;

		q_vector->num_ring_rx = 1;
		q_vector->rx.ring = rx_ring;
		ice_eswitch_remap_ring(rx_ring, q_vector, repr->netdev);
	}
}

/**
 * ice_eswitch_setup_reprs - configure port reprs to run in switchdev mode
 * @pf: pointer to PF struct
 */
static int ice_eswitch_setup_reprs(struct ice_pf *pf)
{
	struct ice_vsi *ctrl_vsi = pf->switchdev.control_vsi;
	int max_vsi_num = 0;
	int i;

	ice_for_each_vf(pf, i) {
		struct ice_vsi *vsi = pf->vf[i].repr->src_vsi;
		struct ice_vf *vf = &pf->vf[i];

		ice_remove_vsi_fltr(&pf->hw, vsi->idx);
		vf->repr->dst = metadata_dst_alloc(0, METADATA_HW_PORT_MUX,
						   GFP_KERNEL);
		if (!vf->repr->dst) {
			ice_fltr_add_mac_and_broadcast(vsi,
						       vf->hw_lan_addr.addr,
						       ICE_FWD_TO_VSI);
			goto err;
		}

		if (ice_vsi_update_security(vsi, ice_vsi_ctx_clear_antispoof)) {
			ice_fltr_add_mac_and_broadcast(vsi,
						       vf->hw_lan_addr.addr,
						       ICE_FWD_TO_VSI);
			metadata_dst_free(vf->repr->dst);
			goto err;
		}

		if (ice_vsi_add_vlan_zero(vsi)) {
			ice_fltr_add_mac_and_broadcast(vsi,
						       vf->hw_lan_addr.addr,
						       ICE_FWD_TO_VSI);
			metadata_dst_free(vf->repr->dst);
			ice_vsi_update_security(vsi, ice_vsi_ctx_set_antispoof);
			goto err;
		}

		if (max_vsi_num < vsi->vsi_num)
			max_vsi_num = vsi->vsi_num;

		netif_napi_add(vf->repr->netdev, &vf->repr->q_vector->napi, ice_napi_poll,
			       NAPI_POLL_WEIGHT);

		netif_keep_dst(vf->repr->netdev);
	}

	kfree(ctrl_vsi->target_netdevs);

	ctrl_vsi->target_netdevs = kcalloc(max_vsi_num + 1,
					   sizeof(*ctrl_vsi->target_netdevs),
					   GFP_KERNEL);
	if (!ctrl_vsi->target_netdevs)
		goto err;

	ice_for_each_vf(pf, i) {
		struct ice_repr *repr = pf->vf[i].repr;
		struct ice_vsi *vsi = repr->src_vsi;
		struct metadata_dst *dst;

		ctrl_vsi->target_netdevs[vsi->vsi_num] = repr->netdev;

		dst = repr->dst;
		dst->u.port_info.port_id = vsi->vsi_num;
		dst->u.port_info.lower_dev = repr->netdev;
		ice_repr_set_traffic_vsi(repr, ctrl_vsi);
	}

	return 0;

err:
	for (i = i - 1; i >= 0; i--) {
		struct ice_vsi *vsi = pf->vf[i].repr->src_vsi;
		struct ice_vf *vf = &pf->vf[i];

		ice_vsi_update_security(vsi, ice_vsi_ctx_set_antispoof);
		metadata_dst_free(vf->repr->dst);
		ice_fltr_add_mac_and_broadcast(vsi, vf->hw_lan_addr.addr,
					       ICE_FWD_TO_VSI);
	}

	return -ENODEV;
}

/**
 * ice_eswitch_release_reprs - clear PR VSIs configuration
 * @pf: poiner to PF struct
 * @ctrl_vsi: pointer to switchdev control VSI
 */
static void ice_eswitch_release_reprs(struct ice_pf *pf,
				      struct ice_vsi *ctrl_vsi)
{
	int i;

	kfree(ctrl_vsi->target_netdevs);
	ice_for_each_vf(pf, i) {
		struct ice_vsi *vsi = pf->vf[i].repr->src_vsi;
		struct ice_vf *vf = &pf->vf[i];

		ice_vsi_update_security(vsi, ice_vsi_ctx_set_antispoof);
		metadata_dst_free(vf->repr->dst);
		ice_fltr_add_mac_and_broadcast(vsi, vf->hw_lan_addr.addr,
					       ICE_FWD_TO_VSI);

		netif_napi_del(&vf->repr->q_vector->napi);
	}
}

/**
 * ice_eswitch_update_repr - reconfigure VF port representor
 * @vsi: VF VSI for which port representor is configured
 */
void ice_eswitch_update_repr(struct ice_vsi *vsi)
{
	struct ice_pf *pf = vsi->back;
	struct ice_repr *repr;
	struct ice_vf *vf;
	int ret;

	if (!ice_is_switchdev_running(pf))
		return;

	vf = &pf->vf[vsi->vf_id];
	repr = vf->repr;
	repr->src_vsi = vsi;
	repr->dst->u.port_info.port_id = vsi->vsi_num;

	ret = ice_vsi_update_security(vsi, ice_vsi_ctx_clear_antispoof);
	if (ret) {
		ice_fltr_add_mac_and_broadcast(vsi, vf->hw_lan_addr.addr, ICE_FWD_TO_VSI);
		dev_err(ice_pf_to_dev(pf), "Failed to update VF %d port representor", vsi->vf_id);
		return;
	}
}

/**
 * ice_eswitch_port_start_xmit - callback for packets transmit
 * @skb: send buffer
 * @netdev: network interface device structure
 *
 * Returns NETDEV_TX_OK if sent, else an error code
 */
netdev_tx_t
ice_eswitch_port_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct ice_netdev_priv *np;
	struct ice_repr *repr;
	struct ice_vsi *vsi;

	np = netdev_priv(netdev);
	vsi = np->vsi;

	if (ice_is_reset_in_progress(vsi->back->state))
		return NETDEV_TX_BUSY;

	repr = ice_netdev_to_repr(netdev);
	skb_dst_drop(skb);
	dst_hold((struct dst_entry *)repr->dst);
	skb_dst_set(skb, (struct dst_entry *)repr->dst);
	skb->queue_mapping = repr->vf->vf_id;

	return ice_start_xmit(skb, netdev);
}

/**
 * ice_eswitch_set_target_vsi - set switchdev context in Tx context descriptor
 * @skb: pointer to send buffer
 * @off: pointer to offload struct
 */
void ice_eswitch_set_target_vsi(struct sk_buff *skb,
				struct ice_tx_offload_params *off)
{
	struct metadata_dst *dst = skb_metadata_dst(skb);
	u64 cd_cmd, dst_vsi;

	if (!dst) {
		cd_cmd = ICE_TX_CTX_DESC_SWTCH_UPLINK << ICE_TXD_CTX_QW1_CMD_S;
		off->cd_qw1 |= (cd_cmd | ICE_TX_DESC_DTYPE_CTX);
	} else {
		cd_cmd = ICE_TX_CTX_DESC_SWTCH_VSI << ICE_TXD_CTX_QW1_CMD_S;
		dst_vsi = ((u64)dst->u.port_info.port_id <<
			   ICE_TXD_CTX_QW1_VSI_S) & ICE_TXD_CTX_QW1_VSI_M;
		off->cd_qw1 = cd_cmd | dst_vsi | ICE_TX_DESC_DTYPE_CTX;
	}
}
#else
static void
ice_eswitch_release_reprs(struct ice_pf __always_unused *pf,
			  struct ice_vsi __always_unused *ctrl_vsi)
{
}

static void
ice_eswitch_remap_rings_to_vectors(struct ice_pf *pf)
{
}

static int ice_eswitch_setup_reprs(struct ice_pf __always_unused *pf)
{
	return -ENODEV;
}

netdev_tx_t
ice_eswitch_port_start_xmit(struct sk_buff __always_unused *skb,
			    struct net_device __always_unused *netdev)
{
	return -EOPNOTSUPP;
}
#endif /* HAVE_METADATA_PORT_INFO */

/**
 * ice_eswitch_vsi_setup - configure switchdev control VSI
 * @pf: pointer to PF structure
 * @pi: pointer to port_info structure
 */
static struct ice_vsi *
ice_eswitch_vsi_setup(struct ice_pf *pf, struct ice_port_info *pi)
{
	return ice_vsi_setup(pf, pi, ICE_VSI_SWITCHDEV_CTRL, ICE_INVAL_VFID, NULL, 0);
}


/**
 * ice_eswitch_napi_del - remove NAPI handle for all port representors
 * @pf: pointer to PF structure
 */
static void ice_eswitch_napi_del(struct ice_pf *pf)
{
	int i;

	ice_for_each_vf(pf, i)
		netif_napi_del(&pf->vf[i].repr->q_vector->napi);
}

/**
 * ice_eswitch_napi_enable - enable NAPI for all port representors
 * @pf: pointer to PF structure
 */
static void ice_eswitch_napi_enable(struct ice_pf *pf)
{
	int i;

	ice_for_each_vf(pf, i)
		napi_enable(&pf->vf[i].repr->q_vector->napi);
}

/**
 * ice_eswitch_napi_disable - disable NAPI for all port representors
 * @pf: pointer to PF structure
 */
static void ice_eswitch_napi_disable(struct ice_pf *pf)
{
	int i;

	ice_for_each_vf(pf, i)
		napi_disable(&pf->vf[i].repr->q_vector->napi);
}

/**
 * ice_eswitch_set_rxdid - configure rxdid on all rx queues from VSI
 * @vsi: vsi to setup rxdid on
 * @rxdid: flex descriptor id
 */
static void ice_eswitch_set_rxdid(struct ice_vsi *vsi, u32 rxdid)
{
	struct ice_hw *hw = &vsi->back->hw;
	int i;

	ice_for_each_rxq(vsi, i) {
		struct ice_ring *ring = vsi->rx_rings[i];
		u16 pf_q = vsi->rxq_map[ring->q_index];

		ice_write_qrxflxp_cntxt(hw, pf_q, rxdid, 0x3, true);
	}
}

/**
 * ice_eswitch_enable_switchdev - configure eswitch in switchdev mode
 * @pf: pointer to PF structure
 */
static int
ice_eswitch_enable_switchdev(struct ice_pf *pf)
{
	struct ice_vsi *ctrl_vsi;

	pf->switchdev.control_vsi = ice_eswitch_vsi_setup(pf, pf->hw.port_info);
	if (!pf->switchdev.control_vsi)
		return -ENODEV;

	ctrl_vsi = pf->switchdev.control_vsi;
	pf->switchdev.uplink_vsi = ice_get_main_vsi(pf);
	if (!pf->switchdev.uplink_vsi)
		goto err_vsi;

	if (ice_eswitch_setup_env(pf))
		goto err_vsi;

	if (ice_repr_add_for_all_vfs(pf))
		goto err_repr_add;

	if (ice_eswitch_setup_reprs(pf))
		goto err_setup_reprs;

	ice_eswitch_remap_rings_to_vectors(pf);

	if (ice_vsi_open(ctrl_vsi))
		goto err_setup_reprs;

	ice_eswitch_napi_enable(pf);

	ice_eswitch_set_rxdid(ctrl_vsi, ICE_RXDID_FLEX_NIC_2);

	return 0;

err_setup_reprs:
	ice_repr_rem_from_all_vfs(pf);
err_repr_add:
	ice_eswitch_release_env(pf);
err_vsi:
	ice_vsi_release(ctrl_vsi);
	return -ENODEV;
}

/**
 * ice_eswitch_disable_switchdev - disable switchdev resources
 * @pf: pointer to PF structure
 */
static void ice_eswitch_disable_switchdev(struct ice_pf *pf)
{
	struct ice_vsi *ctrl_vsi = pf->switchdev.control_vsi;

	ice_eswitch_napi_disable(pf);
	ice_eswitch_release_env(pf);
	ice_vsi_release(ctrl_vsi);
	ice_eswitch_release_reprs(pf, ctrl_vsi);
	ice_repr_rem_from_all_vfs(pf);
}

#ifdef HAVE_METADATA_PORT_INFO
#ifdef HAVE_DEVLINK_ESWITCH_OPS_EXTACK
/**
 * ice_eswitch_mode_set - set new eswitch mode
 * @devlink: pointer to devlink structure
 * @mode: eswitch mode to switch to
 * @extack: pointer to extack structure
 */
int ice_eswitch_mode_set(struct devlink *devlink, u16 mode,
			 struct netlink_ext_ack *extack)
#else
int ice_eswitch_mode_set(struct devlink *devlink, u16 mode)
#endif /* HAVE_DEVLINK_ESWITCH_OPS_EXTACK */
{
	struct ice_pf *pf = devlink_priv(devlink);

	if (pf->eswitch_mode == mode)
		return 0;

	if (pf->num_alloc_vfs) {
		dev_info(ice_pf_to_dev(pf),
			 "Changing eswitch mode is allowed only if there is no VFs created");
		return -EOPNOTSUPP;
	}

	switch (mode) {
	case DEVLINK_ESWITCH_MODE_LEGACY:
		dev_info(ice_pf_to_dev(pf), "PF %d changed eswitch mode to legacy", pf->hw.pf_id);
		break;
	case DEVLINK_ESWITCH_MODE_SWITCHDEV:
	{
#ifdef NETIF_F_HW_TC
		if (ice_is_adq_active(pf)) {
			dev_err(ice_pf_to_dev(pf), "switchdev cannot be configured - ADQ is active. Delete ADQ configs using TC and try again\n");
			return -EOPNOTSUPP;
		}
#endif /* NETIF_F_HW_TC */

#ifdef HAVE_NETDEV_SB_DEV
		if (ice_is_offloaded_macvlan_ena(pf)) {
			dev_err(ice_pf_to_dev(pf), "switchdev cannot be configured -  L2 Forwarding Offload is currently enabled.\n");
			return -EOPNOTSUPP;
		}
#endif /* HAVE_NETDEV_SB_DEV */

		dev_info(ice_pf_to_dev(pf),
			 "PF %d changed eswitch mode to switchdev", pf->hw.pf_id);
		break;
	}
	default:
#ifdef HAVE_DEVLINK_ESWITCH_OPS_EXTACK
		NL_SET_ERR_MSG_MOD(extack, "Unknown eswitch mode");
#else
		dev_err(ice_pf_to_dev(pf), "Unknown eswitch mode");
#endif /* HAVE_DEVLINK_ESWITCH_OPS_EXTACK */
		return -EINVAL;
	}

	pf->eswitch_mode = mode;
	return 0;
}
#endif /* HAVE_METADATA_PORT_INFO */

/**
 * ice_eswitch_get_target_netdev - return port representor netdev
 * @rx_ring: pointer to rx ring
 * @rx_desc: pointer to rx descriptor
 *
 * When working in switchdev mode context (when control vsi is used), this
 * function returns netdev of appropriate port representor. For non-switchdev
 * context, regular netdev associated with rx ring is returned.
 */
struct net_device *
ice_eswitch_get_target_netdev(struct ice_ring *rx_ring,
			      union ice_32b_rx_flex_desc *rx_desc)
{
	struct ice_32b_rx_flex_desc_nic_2 *desc;
	struct ice_vsi *vsi = rx_ring->vsi;
	struct ice_vsi *control_vsi;
	u16 target_vsi_id;

	control_vsi = vsi->back->switchdev.control_vsi;
	if (vsi != control_vsi)
		return rx_ring->netdev;

	desc = (struct ice_32b_rx_flex_desc_nic_2 *)rx_desc;
	target_vsi_id = le16_to_cpu(desc->src_vsi);

	return vsi->target_netdevs[target_vsi_id];
}

/**
 * ice_eswitch_mode_get - get current eswitch mode
 * @devlink: pointer to devlink structure
 * @mode: output parameter for current eswitch mode
 */
int ice_eswitch_mode_get(struct devlink *devlink, u16 *mode)
{
	struct ice_pf *pf = devlink_priv(devlink);

	*mode = pf->eswitch_mode;
	return 0;
}

/**
 * ice_is_eswitch_mode_switchdev - check if eswitch mode is set to switchdev
 * @pf: pointer to PF structure
 *
 * Returns true if eswitch mode is set to DEVLINK_ESWITCH_MODE_SWITCHDEV,
 * false otherwise.
 */
bool ice_is_eswitch_mode_switchdev(struct ice_pf *pf)
{
	return pf->eswitch_mode == DEVLINK_ESWITCH_MODE_SWITCHDEV;
}

/**
 * ice_eswitch_release - cleanup eswitch
 * @pf: pointer to PF structure
 */
void ice_eswitch_release(struct ice_pf *pf)
{
	if (pf->eswitch_mode == DEVLINK_ESWITCH_MODE_LEGACY)
		return;

	ice_eswitch_disable_switchdev(pf);
	pf->switchdev.is_running = false;
}

/**
 * ice_eswitch_configure - configure eswitch
 * @pf: pointer to PF structure
 */
int ice_eswitch_configure(struct ice_pf *pf)
{
	int status;

	if (pf->eswitch_mode == DEVLINK_ESWITCH_MODE_LEGACY || pf->switchdev.is_running)
		return 0;

	status = ice_eswitch_enable_switchdev(pf);
	if (status)
		return status;

	pf->switchdev.is_running = true;
	return 0;
}

/**
 * ice_eswitch_start_all_tx_queues - start Tx queues of all port representors
 * @pf: pointer to PF structure
 */
static void ice_eswitch_start_all_tx_queues(struct ice_pf *pf)
{
	struct ice_repr *repr;
	int i;

	if (test_bit(ICE_DOWN, pf->state))
		return;

	ice_for_each_vf(pf, i) {
		repr = pf->vf[i].repr;
		if (repr)
			ice_repr_start_tx_queues(repr);
	}
}

/**
 * ice_eswitch_stop_all_tx_queues - stop Tx queues of all port representors
 * @pf: pointer to PF structure
 */
void ice_eswitch_stop_all_tx_queues(struct ice_pf *pf)
{
	struct ice_repr *repr;
	int i;

	if (test_bit(ICE_DOWN, pf->state))
		return;

	ice_for_each_vf(pf, i) {
		repr = pf->vf[i].repr;
		if (repr)
			ice_repr_stop_tx_queues(repr);
	}
}

/**
 * ice_eswitch_rebuild - rebuild eswitch
 * @pf: pointer to PF structure
 */
int ice_eswitch_rebuild(struct ice_pf *pf)
{
	struct ice_vsi *ctrl_vsi = pf->switchdev.control_vsi;
	int status;

	ice_eswitch_napi_disable(pf);
	ice_eswitch_napi_del(pf);

	status = ice_eswitch_setup_env(pf);
	if (status)
		return status;

	status = ice_eswitch_setup_reprs(pf);
	if (status)
		return status;

	ice_eswitch_remap_rings_to_vectors(pf);

#ifdef HAVE_TC_SETUP_CLSFLOWER
	ice_replay_tc_fltrs(pf);
#endif /* HAVE_TC_SETUP_CLSFLOWER */

	status = ice_vsi_open(ctrl_vsi);
	if (status)
		return status;

	ice_eswitch_napi_enable(pf);
	ice_eswitch_set_rxdid(ctrl_vsi, ICE_RXDID_FLEX_NIC_2);
	ice_eswitch_start_all_tx_queues(pf);

	return 0;
}
#endif /* CONFIG_NET_DEVLINK */
