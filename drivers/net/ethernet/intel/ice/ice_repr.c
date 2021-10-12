// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2021, Intel Corporation. */

#include "ice.h"
#include "ice_eswitch.h"
#if IS_ENABLED(CONFIG_NET_DEVLINK)
#include "ice_devlink.h"
#endif /* CONFIG_NET_DEVLINK */
#include "ice_virtchnl_pf.h"
#include "ice_tc_lib.h"

#ifdef HAVE_NDO_GET_PHYS_PORT_NAME
/**
 * ice_repr_get_sw_port_id - get port ID associated with representor
 * @repr: pointer to port representor
 */
static int ice_repr_get_sw_port_id(struct ice_repr *repr)
{
	return repr->vf->pf->hw.port_info->lport;
}

/**
 * ice_repr_get_phys_port_name - get phys port name
 * @netdev: pointer to port representor netdev
 * @buf: write here port name
 * @len: max length of buf
 */
static int
ice_repr_get_phys_port_name(struct net_device *netdev, char *buf, size_t len)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_repr *repr = np->repr;
	int res;

#if IS_ENABLED(CONFIG_NET_DEVLINK)
	/* Devlink port is registered and devlink core is taking care of name formatting. */
	if (repr->vf->devlink_port.registered)
		return -EOPNOTSUPP;
#endif /* CONFIG_NET_DEVLINK */

	res = snprintf(buf, len, "pf%dvfr%d", ice_repr_get_sw_port_id(repr),
		       repr->vf->vf_id);
	if (res <= 0)
		return -EOPNOTSUPP;
	return 0;
}
#endif /* HAVE_NDO_GET_PHYS_PORT_NAME */

/**
 * ice_repr_get_stats64 - get VF stats for VFPR use
 * @netdev: pointer to port representor netdev
 * @stats: pointer to struct where stats can be stored
 */
#ifdef HAVE_VOID_NDO_GET_STATS64
static void
#else
static struct rtnl_link_stats64 *
#endif
ice_repr_get_stats64(struct net_device *netdev, struct rtnl_link_stats64 *stats)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);
	struct ice_eth_stats *eth_stats;
	struct ice_vsi *vsi;

	if (ice_check_vf_ready_for_cfg(np->repr->vf))
#ifdef HAVE_VOID_NDO_GET_STATS64
		return;
#else
		return stats;
#endif
	vsi = np->repr->src_vsi;

	ice_update_vsi_stats(vsi);
	eth_stats = &vsi->eth_stats;

	stats->tx_packets = eth_stats->tx_unicast + eth_stats->tx_broadcast +
			    eth_stats->tx_multicast;
	stats->rx_packets = eth_stats->rx_unicast + eth_stats->rx_broadcast +
			    eth_stats->rx_multicast;
	stats->tx_bytes = eth_stats->tx_bytes;
	stats->rx_bytes = eth_stats->rx_bytes;
	stats->multicast = eth_stats->rx_multicast;
	stats->tx_errors = eth_stats->tx_errors;
	stats->tx_dropped = eth_stats->tx_discards;
	stats->rx_dropped = eth_stats->rx_discards;
#ifndef HAVE_VOID_NDO_GET_STATS64

	return stats;
#endif
}

/**
 * ice_netdev_to_repr - Get port representor for given netdevice
 * @netdev: pointer to port representor netdev
 */
struct ice_repr *ice_netdev_to_repr(struct net_device *netdev)
{
	struct ice_netdev_priv *np = netdev_priv(netdev);

	return np->repr;
}

/**
 * ice_repr_open - Enable port representor's network interface
 * @netdev: network interface device structure
 *
 * The open entry point is called when a port representor's network
 * interface is made active by the system (IFF_UP). Corresponding
 * VF is notified about link status change.
 *
 * Returns 0 on success, negative value on failure
 */
static int ice_repr_open(struct net_device *netdev)
{
	struct ice_repr *repr = ice_netdev_to_repr(netdev);
	struct ice_vf *vf;

	vf = repr->vf;
	vf->link_forced = true;
	vf->link_up = true;
	ice_vc_notify_vf_link_state(vf);

	netif_carrier_on(netdev);
	netif_tx_start_all_queues(netdev);

	return 0;
}

/**
 * ice_repr_stop - Disable port representor's network interface
 * @netdev: network interface device structure
 *
 * The stop entry point is called when a port representor's network
 * interface is de-activated by the system. Corresponding
 * VF is notified about link status change.
 *
 * Returns 0 on success, negative value on failure
 */
static int ice_repr_stop(struct net_device *netdev)
{
	struct ice_repr *repr = ice_netdev_to_repr(netdev);
	struct ice_vf *vf;

	vf = repr->vf;
	vf->link_forced = true;
	vf->link_up = false;
	ice_vc_notify_vf_link_state(vf);

	netif_carrier_off(netdev);
	netif_tx_stop_all_queues(netdev);

	return 0;
}

#if IS_ENABLED(CONFIG_NET_DEVLINK) && defined(HAVE_DEVLINK_PORT_ATTR_PCI_VF)
static struct devlink_port *
ice_repr_get_devlink_port(struct net_device *netdev)
{
	struct ice_repr *repr = ice_netdev_to_repr(netdev);

	return &repr->vf->devlink_port;
}
#endif /* CONFIG_NET_DEVLINK && HAVE_DEVLINK_PORT_ATTR_PCI_VF*/

#ifdef HAVE_TC_SETUP_CLSFLOWER
static int
ice_repr_setup_tc_cls_flower(struct ice_repr *repr,
			     struct flow_cls_offload *flower)
{
	switch (flower->command) {
	case FLOW_CLS_REPLACE:
		return ice_add_cls_flower(repr->netdev, repr->src_vsi, flower);
	case FLOW_CLS_DESTROY:
		return ice_del_cls_flower(repr->src_vsi, flower);
	default:
		return -EINVAL;
	}
}
#ifdef HAVE_TC_CB_AND_SETUP_QDISC_MQPRIO
static int
ice_repr_setup_tc_block_cb(enum tc_setup_type type, void *type_data,
			   void *cb_priv)
{
	struct flow_cls_offload *flower = (struct flow_cls_offload *)type_data;
	struct ice_netdev_priv *np = (struct ice_netdev_priv *)cb_priv;

	switch (type) {
	case TC_SETUP_CLSFLOWER:
		return ice_repr_setup_tc_cls_flower(np->repr, flower);
	default:
		return -EOPNOTSUPP;
	}
}

static LIST_HEAD(ice_repr_block_cb_list);
#endif /* HAVE_TC_CB_AND_SETUP_QDISC_MQPRIO */

static int
#ifdef HAVE_NDO_SETUP_TC_REMOVE_TC_TO_NETDEV
ice_repr_setup_tc(struct net_device *netdev, enum tc_setup_type type,
		  void *type_data)
#elif defined(HAVE_NDO_SETUP_TC_CHAIN_INDEX)
ice_repr_setup_tc(struct net_device *netdev, u32 __always_unused handle,
		  __always_unused chain_index, __be16 proto,
		  struct tc_to_netdev *tc)
#else
ice_repr_setup_tc(struct net_device *netdev, u32 __always_unused handle,
		  __be16 __always_unused proto, struct tc_to_netdev *tc)
#endif
{
#ifndef HAVE_NDO_SETUP_TC_REMOVE_TC_TO_NETDEV
	struct tc_cls_flower_offload *cls_flower = tc->cls_flower;
	unsigned int type = tc->type;
#elif !defined(HAVE_TC_CB_AND_SETUP_QDISC_MQPRIO)
	struct tc_cls_flower_offload *cls_flower = (struct
						   tc_cls_flower_offload *)
						   type_data;
#endif /* HAVE_NDO_SETUP_TC_REMOVE_TC_TO_NETDEV */
	struct ice_netdev_priv *np = netdev_priv(netdev);

	switch (type) {
#ifdef HAVE_TC_CB_AND_SETUP_QDISC_MQPRIO
	case TC_SETUP_BLOCK:
		return flow_block_cb_setup_simple((struct flow_block_offload *)
						  type_data,
						  &ice_repr_block_cb_list,
						  ice_repr_setup_tc_block_cb,
						  np, np, true);
#elif !defined(HAVE_NDO_SETUP_TC_REMOVE_TC_TO_NETDEV) || !defined(HAVE_TC_CB_AND_SETUP_QDISC_MQPRIO)
	case TC_SETUP_CLSFLOWER:
		return ice_repr_setup_tc_cls_flower(np->repr, cls_flower);
#endif /* HAVE_TC_CB_AND_SETUP_QDISC_MQPRIO */
	default:
		return -EOPNOTSUPP;
	}
}
#endif /* ESWITCH_SUPPORT */

static const struct net_device_ops ice_repr_netdev_ops = {
#ifdef HAVE_NDO_GET_PHYS_PORT_NAME
	.ndo_get_phys_port_name = ice_repr_get_phys_port_name,
#endif /* HAVE_NDO_GET_PHYS_PORT_NAME */
	.ndo_get_stats64 = ice_repr_get_stats64,
	.ndo_open = ice_repr_open,
	.ndo_stop = ice_repr_stop,
#if IS_ENABLED(CONFIG_NET_DEVLINK)
	.ndo_start_xmit = ice_eswitch_port_start_xmit,
#ifdef HAVE_DEVLINK_PORT_ATTR_PCI_VF
	.ndo_get_devlink_port = ice_repr_get_devlink_port,
#endif /* HAVE_DEVLINK_PORT_ATTR_PCI_VF */
#ifdef HAVE_TC_SETUP_CLSFLOWER
#ifdef HAVE_RHEL7_NETDEV_OPS_EXT_NDO_SETUP_TC
	.extended.ndo_setup_tc_rh = ice_repr_setup_tc,
#else
	.ndo_setup_tc = ice_repr_setup_tc,
#endif /* HAVE_RHEL7_NETDEV_OPS_EXT_NDO_SETUP_TC */
#endif /* HAVE_TC_SETUP_CLSFLOWER */
#endif /* CONFIG_NET_DEVLINK */
};

/**
 * ice_is_port_repr_netdev - Check if a given netdevice is a port representor
 * netdev
 * @netdev: pointer to netdev
 */
bool ice_is_port_repr_netdev(struct net_device *netdev)
{
	return netdev && (netdev->netdev_ops == &ice_repr_netdev_ops);
}

/**
 * ice_repr_reg_netdev - register port representor netdev
 * @netdev: pointer to port representor netdev
 */
static int
ice_repr_reg_netdev(struct net_device *netdev)
{
	eth_hw_addr_random(netdev);
	netdev->netdev_ops = &ice_repr_netdev_ops;
	ice_set_ethtool_repr_ops(netdev);

#ifdef NETIF_F_HW_TC
	netdev->hw_features |= NETIF_F_HW_TC;
#endif /* NETIF_F_HW_TC */

	netif_carrier_off(netdev);
	netif_tx_stop_all_queues(netdev);

	return register_netdev(netdev);
}

/**
 * ice_repr_add - add representor for VF
 * @vf: pointer to VF structure
 */
static int ice_repr_add(struct ice_vf *vf)
{
	struct ice_q_vector *q_vector;
	struct ice_netdev_priv *np;
	struct ice_repr *repr;
	int err;

	repr = kzalloc(sizeof(*repr), GFP_KERNEL);
	if (!repr)
		return -ENOMEM;

	repr->netdev = alloc_etherdev(sizeof(struct ice_netdev_priv));
	if (!repr->netdev) {
		err =  -ENOMEM;
		goto err_alloc;
	}

	repr->src_vsi = ice_get_vf_vsi(vf);
	repr->vf = vf;
	vf->repr = repr;
	np = netdev_priv(repr->netdev);
	np->repr = repr;

	q_vector = kzalloc(sizeof(*q_vector), GFP_KERNEL);
	if (!q_vector) {
		err = -ENOMEM;
		goto err_alloc_q_vector;
	}
	repr->q_vector = q_vector;

#if IS_ENABLED(CONFIG_NET_DEVLINK)
#ifdef HAVE_DEVLINK_PORT_ATTR_PCI_VF
	err = ice_devlink_create_vf_port(vf);
	if (err)
		goto err_devlink;
#endif /* HAVE_DEVLINK_PORT_ATTR_PCI_VF */
#endif /* CONFIG_NET_DEVLINK */

	err = ice_repr_reg_netdev(repr->netdev);
	if (err)
		goto err_netdev;

#if IS_ENABLED(CONFIG_NET_DEVLINK)
#ifdef HAVE_DEVLINK_PORT_ATTR_PCI_VF
	devlink_port_type_eth_set(&vf->devlink_port, repr->netdev);
#endif /* HAVE_DEVLINK_PORT_ATTR_PCI_VF */
#endif /* CONFIG_NET_DEVLINK */

	return 0;

err_netdev:
#if IS_ENABLED(CONFIG_NET_DEVLINK)
#ifdef HAVE_DEVLINK_PORT_ATTR_PCI_VF
	ice_devlink_destroy_vf_port(vf);
err_devlink:
#endif /* HAVE_DEVLINK_PORT_ATTR_PCI_VF */
#endif /* CONFIG_NET_DEVLINK */
	kfree(repr->q_vector);
	vf->repr->q_vector = NULL;
err_alloc_q_vector:
	free_netdev(repr->netdev);
	repr->netdev = NULL;
err_alloc:
	kfree(repr);
	vf->repr = NULL;
	return err;
}

/**
 * ice_repr_rem - remove representor from VF
 * @vf: pointer to VF structure
 */
static void ice_repr_rem(struct ice_vf *vf)
{
#if IS_ENABLED(CONFIG_NET_DEVLINK)
#ifdef HAVE_DEVLINK_PORT_ATTR_PCI_VF
	ice_devlink_destroy_vf_port(vf);
#endif /* HAVE_DEVLINK_PORT_ATTR_PCI_VF */
#endif /* CONFIG_NET_DEVLINK */
	kfree(vf->repr->q_vector);
	vf->repr->q_vector = NULL;
	unregister_netdev(vf->repr->netdev);
	free_netdev(vf->repr->netdev);
	vf->repr->netdev = NULL;
	kfree(vf->repr);
	vf->repr = NULL;
}


/**
 * ice_repr_add_for_all_vfs - add port representor for all VFs
 * @pf: pointer to PF structure
 */
int ice_repr_add_for_all_vfs(struct ice_pf *pf)
{
	int err;
	int i;


	ice_for_each_vf(pf, i) {
		struct ice_vf *vf = &pf->vf[i];

		err = ice_repr_add(vf);
		if (err)
			goto err;

		ice_vc_change_ops_to_repr(&vf->vc_ops);
	}

	return 0;

err:
	for (i = i - 1; i >= 0; i--) {
		struct ice_vf *vf = &pf->vf[i];

		ice_repr_rem(vf);
		ice_vc_set_dflt_vf_ops(&vf->vc_ops);
	}

	return err;
}

/**
 * ice_repr_rem_from_all_vfs - remove port representor for all VFs
 * @pf: pointer to PF structure
 */
void ice_repr_rem_from_all_vfs(struct ice_pf *pf)
{
	int i;


	ice_for_each_vf(pf, i) {
		struct ice_vf *vf = &pf->vf[i];

		ice_repr_rem(vf);
		ice_vc_set_dflt_vf_ops(&vf->vc_ops);
	}
}

/**
 * ice_repr_start_tx_queues - start Tx queues of port representor
 * @repr: pointer to repr structure
 */
void ice_repr_start_tx_queues(struct ice_repr *repr)
{
	netif_carrier_on(repr->netdev);
	netif_tx_start_all_queues(repr->netdev);
}

/**
 * ice_repr_stop_tx_queues - stop Tx queues of port representor
 * @repr: pointer to repr structure
 */
void ice_repr_stop_tx_queues(struct ice_repr *repr)
{
	netif_carrier_off(repr->netdev);
	netif_tx_stop_all_queues(repr->netdev);
}

#ifdef HAVE_METADATA_PORT_INFO
/**
 * ice_repr_set_traffic_vsi - set traffic VSI for port representor
 * @repr: repr on with VSI will be set
 * @vsi: pointer to VSI that will be used by port representor to pass traffic
 */
void ice_repr_set_traffic_vsi(struct ice_repr *repr, struct ice_vsi *vsi)
{
	struct ice_netdev_priv *np = netdev_priv(repr->netdev);

	np->vsi = vsi;
}
#endif /* HAVE_METADATA_PORT_INFO */
