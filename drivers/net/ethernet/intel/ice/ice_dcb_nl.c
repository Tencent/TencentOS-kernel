// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2021, Intel Corporation. */

#include "ice.h"
#include "ice_dcb.h"
#include "ice_dcb_lib.h"
#include "ice_dcb_nl.h"
#include <net/dcbnl.h>

/**
 * ice_dcbnl_devreset - perform enough of a ifdown/ifup to sync DCBNL info
 * @netdev: device associated with interface that needs reset
 */
static void ice_dcbnl_devreset(struct net_device *netdev)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);

	while (ice_is_reset_in_progress(pf->state))
		usleep_range(1000, 2000);

	dev_close(netdev);
	netdev_state_change(netdev);
	dev_open(netdev, NULL);
	netdev_state_change(netdev);
}

/**
 * ice_dcbnl_getets - retrieve local ETS configuration
 * @netdev: the relevant netdev
 * @ets: struct to hold ETS configuration
 */
static int ice_dcbnl_getets(struct net_device *netdev, struct ieee_ets *ets)
{
	struct ice_dcbx_cfg *dcbxcfg;
	struct ice_pf *pf;

	pf = ice_netdev_to_pf(netdev);
	dcbxcfg = &pf->hw.port_info->qos_cfg.local_dcbx_cfg;

	ets->willing = dcbxcfg->etscfg.willing;
	ets->ets_cap = dcbxcfg->etscfg.maxtcs;
	ets->cbs = dcbxcfg->etscfg.cbs;
	memcpy(ets->tc_tx_bw, dcbxcfg->etscfg.tcbwtable, sizeof(ets->tc_tx_bw));
	memcpy(ets->tc_rx_bw, dcbxcfg->etscfg.tcbwtable, sizeof(ets->tc_rx_bw));
	memcpy(ets->tc_tsa, dcbxcfg->etscfg.tsatable, sizeof(ets->tc_tsa));
	memcpy(ets->prio_tc, dcbxcfg->etscfg.prio_table, sizeof(ets->prio_tc));
	memcpy(ets->tc_reco_bw, dcbxcfg->etsrec.tcbwtable,
	       sizeof(ets->tc_reco_bw));
	memcpy(ets->tc_reco_tsa, dcbxcfg->etsrec.tsatable,
	       sizeof(ets->tc_reco_tsa));
	memcpy(ets->reco_prio_tc, dcbxcfg->etscfg.prio_table,
	       sizeof(ets->reco_prio_tc));

	return 0;
}

/**
 * ice_dcbnl_setets - set IEEE ETS configuration
 * @netdev: pointer to relevant netdev
 * @ets: struct to hold ETS configuration
 */
static int ice_dcbnl_setets(struct net_device *netdev, struct ieee_ets *ets)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_dcbx_cfg *new_cfg;
	int bwcfg = 0, bwrec = 0;
	int err, i;

	if ((pf->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED) ||
	    !(pf->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	new_cfg = &pf->hw.port_info->qos_cfg.desired_dcbx_cfg;

	mutex_lock(&pf->tc_mutex);
#ifdef NETIF_F_HW_TC
	if (ice_is_adq_active(pf)) {
		netdev_err(netdev, "can't set DCB configuration when ADQ is active\n");
		err = ICE_DCB_NO_HW_CHG;
		goto ets_out;
	}
#endif /* NETIF_F_HW_TC */

	new_cfg->etscfg.willing = ets->willing;
	new_cfg->etscfg.cbs = ets->cbs;
	ice_for_each_traffic_class(i) {
		new_cfg->etscfg.tcbwtable[i] = ets->tc_tx_bw[i];
		bwcfg += ets->tc_tx_bw[i];
		new_cfg->etscfg.tsatable[i] = ets->tc_tsa[i];
		if (new_cfg->pfc_mode == ICE_QOS_MODE_VLAN) {
			/* in DSCP mode up->tc mapping cannot change */
			new_cfg->etscfg.prio_table[i] = ets->prio_tc[i];
			new_cfg->etsrec.prio_table[i] = ets->reco_prio_tc[i];
		}
		new_cfg->etsrec.tcbwtable[i] = ets->tc_reco_bw[i];
		bwrec += ets->tc_reco_bw[i];
		new_cfg->etsrec.tsatable[i] = ets->tc_reco_tsa[i];
	}

	if (ice_dcb_bwchk(pf, new_cfg)) {
		err = -EINVAL;
		goto ets_out;
	}

	new_cfg->etscfg.maxtcs = pf->hw.func_caps.common_cap.maxtc;

	if (!bwrec)
		new_cfg->etsrec.tcbwtable[0] = 100;

	err = ice_pf_dcb_cfg(pf, new_cfg, true);
	/* return of zero indicates new cfg applied */
	if (err == ICE_DCB_HW_CHG_RST)
		ice_dcbnl_devreset(netdev);
	if (err == ICE_DCB_NO_HW_CHG)
		err = ICE_DCB_HW_CHG_RST;

ets_out:
	mutex_unlock(&pf->tc_mutex);
	return err;
}

/**
 * ice_dcbnl_getnumtcs - Get max number of traffic classes supported
 * @dev: pointer to netdev struct
 * @tcid: TC ID
 * @num: total number of TCs supported by the adapter
 *
 * Return the total number of TCs supported
 */
static int
ice_dcbnl_getnumtcs(struct net_device *dev, int __always_unused tcid, u8 *num)
{
	struct ice_pf *pf = ice_netdev_to_pf(dev);

	if (!test_bit(ICE_FLAG_DCB_CAPABLE, pf->flags))
		return -EINVAL;

	*num = pf->hw.func_caps.common_cap.maxtc;
	return 0;
}

/**
 * ice_dcbnl_getdcbx - retrieve current DCBX capability
 * @netdev: pointer to the netdev struct
 */
static u8 ice_dcbnl_getdcbx(struct net_device *netdev)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);

	return pf->dcbx_cap;
}

/**
 * ice_dcbnl_setdcbx - set required DCBX capability
 * @netdev: the corresponding netdev
 * @mode: required mode
 */
static u8 ice_dcbnl_setdcbx(struct net_device *netdev, u8 mode)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_qos_cfg *qos_cfg;

	/* if FW LLDP agent is running, DCBNL not allowed to change mode */
	if (test_bit(ICE_FLAG_FW_LLDP_AGENT, pf->flags))
		return ICE_DCB_NO_HW_CHG;

	/* No support for LLD_MANAGED modes or CEE+IEEE */
	if ((mode & DCB_CAP_DCBX_LLD_MANAGED) ||
	    ((mode & DCB_CAP_DCBX_VER_IEEE) && (mode & DCB_CAP_DCBX_VER_CEE)) ||
	    !(mode & DCB_CAP_DCBX_HOST))
		return ICE_DCB_NO_HW_CHG;

	/* Already set to the given mode no change */
	if (mode == pf->dcbx_cap)
		return ICE_DCB_NO_HW_CHG;

	pf->dcbx_cap = mode;
	qos_cfg = &pf->hw.port_info->qos_cfg;
	if (mode & DCB_CAP_DCBX_VER_CEE) {
		if (qos_cfg->local_dcbx_cfg.pfc_mode == ICE_QOS_MODE_DSCP)
			return ICE_DCB_NO_HW_CHG;
		qos_cfg->local_dcbx_cfg.dcbx_mode = ICE_DCBX_MODE_CEE;
	} else {
		qos_cfg->local_dcbx_cfg.dcbx_mode = ICE_DCBX_MODE_IEEE;
	}

	dev_info(ice_pf_to_dev(pf), "DCBx mode = 0x%x\n", mode);
	return ICE_DCB_HW_CHG_RST;
}

/**
 * ice_dcbnl_get_perm_hw_addr - MAC address used by DCBX
 * @netdev: pointer to netdev struct
 * @perm_addr: buffer to return permanent MAC address
 */
static void ice_dcbnl_get_perm_hw_addr(struct net_device *netdev, u8 *perm_addr)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_port_info *pi = pf->hw.port_info;
	int i, j;

	memset(perm_addr, 0xff, MAX_ADDR_LEN);

	for (i = 0; i < netdev->addr_len; i++)
		perm_addr[i] = pi->mac.perm_addr[i];

	for (j = 0; j < netdev->addr_len; j++, i++)
		perm_addr[i] = pi->mac.perm_addr[j];
}

/**
 * ice_get_pfc_delay - Retrieve PFC Link Delay
 * @hw: pointer to HW struct
 * @delay: holds the PFC Link Delay value
 */
static void ice_get_pfc_delay(struct ice_hw *hw, u16 *delay)
{
	u32 val;

	val = rd32(hw, PRTDCB_GENC);
	*delay = (u16)((val & PRTDCB_GENC_PFCLDA_M) >> PRTDCB_GENC_PFCLDA_S);
}

/**
 * ice_dcbnl_getpfc - retrieve local IEEE PFC config
 * @netdev: pointer to netdev struct
 * @pfc: struct to hold PFC info
 */
static int ice_dcbnl_getpfc(struct net_device *netdev, struct ieee_pfc *pfc)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_port_info *pi = pf->hw.port_info;
	struct ice_dcbx_cfg *dcbxcfg;
	int i;

	dcbxcfg = &pi->qos_cfg.local_dcbx_cfg;
	pfc->pfc_cap = dcbxcfg->pfc.pfccap;
	pfc->pfc_en = dcbxcfg->pfc.pfcena;
	pfc->mbc = dcbxcfg->pfc.mbc;
	ice_get_pfc_delay(&pf->hw, &pfc->delay);

	ice_for_each_traffic_class(i) {
		pfc->requests[i] = pf->stats.priority_xoff_tx[i];
		pfc->indications[i] = pf->stats.priority_xoff_rx[i];
	}

	return 0;
}

/**
 * ice_dcbnl_setpfc - set local IEEE PFC config
 * @netdev: pointer to relevant netdev
 * @pfc: pointer to struct holding PFC config
 */
static int ice_dcbnl_setpfc(struct net_device *netdev, struct ieee_pfc *pfc)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_dcbx_cfg *new_cfg;
	int err;

	if ((pf->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED) ||
	    !(pf->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	mutex_lock(&pf->tc_mutex);
#ifdef NETIF_F_HW_TC
	if (ice_is_adq_active(pf)) {
		netdev_err(netdev, "can't set DCB configuration when ADQ is active\n");
		err = ICE_DCB_NO_HW_CHG;
		goto pfc_out;
	}
#endif /* NETIF_F_HW_TC */

	new_cfg = &pf->hw.port_info->qos_cfg.desired_dcbx_cfg;

	if (pfc->pfc_cap)
		new_cfg->pfc.pfccap = pfc->pfc_cap;
	else
		new_cfg->pfc.pfccap = pf->hw.func_caps.common_cap.maxtc;

	new_cfg->pfc.pfcena = pfc->pfc_en;

	err = ice_pf_dcb_cfg(pf, new_cfg, true);
	if (err == ICE_DCB_HW_CHG_RST)
		ice_dcbnl_devreset(netdev);
	if (err == ICE_DCB_NO_HW_CHG)
		err = ICE_DCB_HW_CHG_RST;
#ifdef NETIF_F_HW_TC
pfc_out:
#endif /* NETIF_F_HW_TC */
	mutex_unlock(&pf->tc_mutex);
	return err;
}

/**
 * ice_dcbnl_get_pfc_cfg - Get CEE PFC config
 * @netdev: pointer to netdev struct
 * @prio: corresponding user priority
 * @setting: the PFC setting for given priority
 */
static void
ice_dcbnl_get_pfc_cfg(struct net_device *netdev, int prio, u8 *setting)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_port_info *pi = pf->hw.port_info;

	if ((pf->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED) ||
	    !(pf->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return;

	if (prio >= ICE_MAX_USER_PRIORITY)
		return;

	*setting = (pi->qos_cfg.local_dcbx_cfg.pfc.pfcena >> prio) & 0x1;
	dev_dbg(ice_pf_to_dev(pf), "Get PFC Config up=%d, setting=%d, pfcenable=0x%x\n",
		prio, *setting, pi->qos_cfg.local_dcbx_cfg.pfc.pfcena);
}

/**
 * ice_dcbnl_set_pfc_cfg - Set CEE PFC config
 * @netdev: the corresponding netdev
 * @prio: User Priority
 * @set: PFC setting to apply
 */
static void ice_dcbnl_set_pfc_cfg(struct net_device *netdev, int prio, u8 set)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_dcbx_cfg *new_cfg;

	if ((pf->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED) ||
	    !(pf->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return;

	if (prio >= ICE_MAX_USER_PRIORITY)
		return;

	new_cfg = &pf->hw.port_info->qos_cfg.desired_dcbx_cfg;

	new_cfg->pfc.pfccap = pf->hw.func_caps.common_cap.maxtc;
	if (set)
		new_cfg->pfc.pfcena |= BIT(prio);
	else
		new_cfg->pfc.pfcena &= ~BIT(prio);

	dev_dbg(ice_pf_to_dev(pf), "Set PFC config UP:%d set:%d pfcena:0x%x\n",
		prio, set, new_cfg->pfc.pfcena);
}

/**
 * ice_dcbnl_getpfcstate - get CEE PFC mode
 * @netdev: pointer to netdev struct
 */
static u8 ice_dcbnl_getpfcstate(struct net_device *netdev)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_port_info *pi = pf->hw.port_info;

	/* Return enabled if any UP enabled for PFC */
	if (pi->qos_cfg.local_dcbx_cfg.pfc.pfcena)
		return 1;

	return 0;
}

/**
 * ice_dcbnl_getstate - get DCB enabled state
 * @netdev: pointer to netdev struct
 */
static u8 ice_dcbnl_getstate(struct net_device *netdev)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	u8 state = 0;

	state = test_bit(ICE_FLAG_DCB_CAPABLE, pf->flags);

	dev_dbg(ice_pf_to_dev(pf), "DCB enabled state = %d\n", state);
	return state;
}

/**
 * ice_dcbnl_setstate - Set CEE DCB state
 * @netdev: pointer to relevant netdev
 * @state: state value to set
 */
static u8 ice_dcbnl_setstate(struct net_device *netdev, u8 state)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);

	if ((pf->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED) ||
	    !(pf->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return ICE_DCB_NO_HW_CHG;

	/* Nothing to do */
	if (!!state == test_bit(ICE_FLAG_DCB_ENA, pf->flags))
		return ICE_DCB_NO_HW_CHG;

	if (state) {
		set_bit(ICE_FLAG_DCB_ENA, pf->flags);
		memcpy(&pf->hw.port_info->qos_cfg.desired_dcbx_cfg,
		       &pf->hw.port_info->qos_cfg.local_dcbx_cfg,
		       sizeof(struct ice_dcbx_cfg));
	} else {
		clear_bit(ICE_FLAG_DCB_ENA, pf->flags);
	}

	return ICE_DCB_HW_CHG;
}

/**
 * ice_dcbnl_get_pg_tc_cfg_tx - get CEE PG Tx config
 * @netdev: pointer to netdev struct
 * @prio: the corresponding user priority
 * @prio_type: traffic priority type
 * @pgid: the BW group ID the traffic class belongs to
 * @bw_pct: BW percentage for the corresponding BWG
 * @up_map: prio mapped to corresponding TC
 */
static void
ice_dcbnl_get_pg_tc_cfg_tx(struct net_device *netdev, int prio,
			   u8 __always_unused *prio_type, u8 *pgid,
			   u8 __always_unused *bw_pct,
			   u8 __always_unused *up_map)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_port_info *pi = pf->hw.port_info;

	if ((pf->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED) ||
	    !(pf->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return;

	if (prio >= ICE_MAX_USER_PRIORITY)
		return;

	*pgid = pi->qos_cfg.local_dcbx_cfg.etscfg.prio_table[prio];
	dev_dbg(ice_pf_to_dev(pf), "Get PG config prio=%d tc=%d\n", prio,
		*pgid);
}

/**
 * ice_dcbnl_set_pg_tc_cfg_tx - set CEE PG Tx config
 * @netdev: pointer to relevant netdev
 * @tc: the corresponding traffic class
 * @prio_type: the traffic priority type
 * @bwg_id: the BW group ID the TC belongs to
 * @bw_pct: the BW perventage for the BWG
 * @up_map: prio mapped to corresponding TC
 */
static void
ice_dcbnl_set_pg_tc_cfg_tx(struct net_device *netdev, int tc,
			   u8 __always_unused prio_type,
			   u8 __always_unused bwg_id,
			   u8 __always_unused bw_pct, u8 up_map)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_dcbx_cfg *new_cfg;
	int i;

	if ((pf->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED) ||
	    !(pf->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return;

	if (tc >= ICE_MAX_TRAFFIC_CLASS)
		return;

	new_cfg = &pf->hw.port_info->qos_cfg.desired_dcbx_cfg;

	/* prio_type, bwg_id and bw_pct per UP are not supported */

	ice_for_each_traffic_class(i) {
		if (up_map & BIT(i))
			new_cfg->etscfg.prio_table[i] = tc;
	}
	new_cfg->etscfg.tsatable[tc] = ICE_IEEE_TSA_ETS;
}

/**
 * ice_dcbnl_get_pg_bwg_cfg_tx - Get CEE PGBW config
 * @netdev: pointer to the netdev struct
 * @pgid: corresponding traffic class
 * @bw_pct: the BW percentage for the corresponding TC
 */
static void
ice_dcbnl_get_pg_bwg_cfg_tx(struct net_device *netdev, int pgid, u8 *bw_pct)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_port_info *pi = pf->hw.port_info;

	if ((pf->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED) ||
	    !(pf->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return;

	if (pgid >= ICE_MAX_TRAFFIC_CLASS)
		return;

	*bw_pct = pi->qos_cfg.local_dcbx_cfg.etscfg.tcbwtable[pgid];
	dev_dbg(ice_pf_to_dev(pf), "Get PG BW config tc=%d bw_pct=%d\n",
		pgid, *bw_pct);
}

/**
 * ice_dcbnl_set_pg_bwg_cfg_tx - set CEE PG Tx BW config
 * @netdev: the corresponding netdev
 * @pgid: Correspongind traffic class
 * @bw_pct: the BW percentage for the specified TC
 */
static void
ice_dcbnl_set_pg_bwg_cfg_tx(struct net_device *netdev, int pgid, u8 bw_pct)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_dcbx_cfg *new_cfg;

	if ((pf->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED) ||
	    !(pf->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return;

	if (pgid >= ICE_MAX_TRAFFIC_CLASS)
		return;

	new_cfg = &pf->hw.port_info->qos_cfg.desired_dcbx_cfg;

	new_cfg->etscfg.tcbwtable[pgid] = bw_pct;
}

/**
 * ice_dcbnl_get_pg_tc_cfg_rx - Get CEE PG Rx config
 * @netdev: pointer to netdev struct
 * @prio: the corresponding user priority
 * @prio_type: the traffic priority type
 * @pgid: the PG ID
 * @bw_pct: the BW percentage for the corresponding BWG
 * @up_map: prio mapped to corresponding TC
 */
static void
ice_dcbnl_get_pg_tc_cfg_rx(struct net_device *netdev, int prio,
			   u8 __always_unused *prio_type, u8 *pgid,
			   u8 __always_unused *bw_pct,
			   u8 __always_unused *up_map)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_port_info *pi = pf->hw.port_info;

	if ((pf->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED) ||
	    !(pf->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return;

	if (prio >= ICE_MAX_USER_PRIORITY)
		return;

	*pgid = pi->qos_cfg.local_dcbx_cfg.etscfg.prio_table[prio];
}

/**
 * ice_dcbnl_set_pg_tc_cfg_rx
 * @netdev: relevant netdev struct
 * @prio: corresponding user priority
 * @prio_type: the traffic priority type
 * @pgid: the PG ID
 * @bw_pct: BW percentage for corresponding BWG
 * @up_map: prio mapped to corresponding TC
 *
 * lldpad requires this function pointer to be non-NULL to complete CEE config.
 */
static void
ice_dcbnl_set_pg_tc_cfg_rx(struct net_device *netdev,
			   int __always_unused prio,
			   u8 __always_unused prio_type,
			   u8 __always_unused pgid,
			   u8 __always_unused bw_pct,
			   u8 __always_unused up_map)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);

	dev_dbg(ice_pf_to_dev(pf), "Rx TC PG Config Not Supported.\n");
}

/**
 * ice_dcbnl_get_pg_bwg_cfg_rx - Get CEE PG BW Rx config
 * @netdev: pointer to netdev struct
 * @pgid: the corresponding traffic class
 * @bw_pct: the BW percentage for the corresponding TC
 */
static void
ice_dcbnl_get_pg_bwg_cfg_rx(struct net_device *netdev, int __always_unused pgid,
			    u8 *bw_pct)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);

	if ((pf->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED) ||
	    !(pf->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return;

	*bw_pct = 0;
}

/**
 * ice_dcbnl_set_pg_bwg_cfg_rx
 * @netdev: the corresponding netdev
 * @pgid: corresponding TC
 * @bw_pct: BW percentage for given TC
 *
 * lldpad requires this function pointer to be non-NULL to complete CEE config.
 */
static void
ice_dcbnl_set_pg_bwg_cfg_rx(struct net_device *netdev, int __always_unused pgid,
			    u8 __always_unused bw_pct)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);

	dev_dbg(ice_pf_to_dev(pf), "Rx BWG PG Config Not Supported.\n");
}

/**
 * ice_dcbnl_get_cap - Get DCBX capabilities of adapter
 * @netdev: pointer to netdev struct
 * @capid: the capability type
 * @cap: the capability value
 */
static u8 ice_dcbnl_get_cap(struct net_device *netdev, int capid, u8 *cap)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);

	if (!(test_bit(ICE_FLAG_DCB_CAPABLE, pf->flags)))
		return ICE_DCB_NO_HW_CHG;

	switch (capid) {
	case DCB_CAP_ATTR_PG:
		*cap = true;
		break;
	case DCB_CAP_ATTR_PFC:
		*cap = true;
		break;
	case DCB_CAP_ATTR_UP2TC:
		*cap = false;
		break;
	case DCB_CAP_ATTR_PG_TCS:
		*cap = 0x80;
		break;
	case DCB_CAP_ATTR_PFC_TCS:
		*cap = 0x80;
		break;
	case DCB_CAP_ATTR_GSP:
		*cap = false;
		break;
	case DCB_CAP_ATTR_BCN:
		*cap = false;
		break;
	case DCB_CAP_ATTR_DCBX:
		*cap = pf->dcbx_cap;
		break;
	default:
		*cap = false;
		break;
	}

	dev_dbg(ice_pf_to_dev(pf), "DCBX Get Capability cap=%d capval=0x%x\n",
		capid, *cap);
	return 0;
}

/**
 * ice_dcbnl_getapp - get CEE APP
 * @netdev: pointer to netdev struct
 * @idtype: the App selector
 * @id: the App ethtype or port number
 */
#ifdef HAVE_DCBNL_OPS_SETAPP_RETURN_INT
static int ice_dcbnl_getapp(struct net_device *netdev, u8 idtype, u16 id)
#else
static u8 ice_dcbnl_getapp(struct net_device *netdev, u8 idtype, u16 id)
#endif /* HAVE_DCBNL_OPS_SETAPP_RETURN_INT */
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct dcb_app app = {
				.selector = idtype,
				.protocol = id,
			     };

	if ((pf->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED) ||
	    !(pf->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return -EINVAL;

	return dcb_getapp(netdev, &app);
}

/**
 * ice_dcbnl_find_app - Search for APP in given DCB config
 * @cfg: struct to hold DCBX config
 * @app: struct to hold app data to look for
 */
static bool
ice_dcbnl_find_app(struct ice_dcbx_cfg *cfg,
		   struct ice_dcb_app_priority_table *app)
{
	unsigned int i;

	for (i = 0; i < cfg->numapps; i++) {
		if (app->selector == cfg->app[i].selector &&
		    app->prot_id == cfg->app[i].prot_id &&
		    app->priority == cfg->app[i].priority)
			return true;
	}

	return false;
}

#define ICE_BYTES_PER_DSCP_VAL		8

/**
 * ice_dcbnl_setapp - set local IEEE App config
 * @netdev: relevant netdev struct
 * @app: struct to hold app config info
 */
static int ice_dcbnl_setapp(struct net_device *netdev, struct dcb_app *app)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_dcb_app_priority_table new_app;
	struct ice_dcbx_cfg *old_cfg, *new_cfg;
	u8 max_tc;
	int ret;

	/* ONLY DSCP APP TLVs have operational significance */
	if (app->selector != IEEE_8021QAZ_APP_SEL_DSCP)
		return -EINVAL;

	/* only allow APP TLVs in SW Mode */
	if (pf->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED) {
		netdev_err(netdev, "can't do DSCP QoS when FW DCB agent active\n");
		return -EINVAL;
	}

	if (!(pf->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	if (!ice_is_feature_supported(pf, ICE_F_DSCP))
		return -EOPNOTSUPP;

#ifdef NETIF_F_HW_TC
	if (ice_is_adq_active(pf)) {
		netdev_err(netdev,
			   "can't set DCB configuration when ADQ is active\n");
		return ICE_DCB_NO_HW_CHG;
	}
#endif /* NETIF_F_HW_TC */
	if (app->protocol >= ICE_DSCP_NUM_VAL) {
		netdev_err(netdev, "DSCP value 0x%04X out of range\n",
			   app->protocol);
		return -EINVAL;
	}

	max_tc = pf->hw.func_caps.common_cap.maxtc;
	if (app->priority >= max_tc) {
		netdev_err(netdev, "TC %d out of range, max TC %d\n",
			   app->priority, max_tc);
		return -EINVAL;
	}


	/* grab TC mutex */
	mutex_lock(&pf->tc_mutex);

	new_cfg = &pf->hw.port_info->qos_cfg.desired_dcbx_cfg;
	old_cfg = &pf->hw.port_info->qos_cfg.local_dcbx_cfg;

	ret = dcb_ieee_setapp(netdev, app);
	if (ret)
		goto setapp_out;

	if (test_and_set_bit(app->protocol, new_cfg->dscp_mapped)) {
		netdev_err(netdev, "DSCP value 0x%04X already user mapped\n",
			   app->protocol);
		ret = dcb_ieee_delapp(netdev, app);
		if (ret)
			netdev_err(netdev, "Failed to delete re-mapping TLV\n");
		ret = -EINVAL;
		goto setapp_out;
	}

	new_app.selector = app->selector;
	new_app.prot_id = app->protocol;
	new_app.priority = app->priority;

	/* If port is not in DSCP mode, need to set */
	if (old_cfg->pfc_mode == ICE_QOS_MODE_VLAN) {
		int i, j;

		/* set DSCP mode */
		ret = ice_aq_set_pfc_mode(&pf->hw, ICE_AQC_PFC_DSCP_BASED_PFC,
					  NULL);
		if (ret) {
			netdev_err(netdev, "Failed to set DSCP PFC mode %d\n",
				   ret);
			goto setapp_out;
		}
		netdev_info(netdev, "Switched QoS to L3 DSCP mode\n");

		new_cfg->pfc_mode = ICE_QOS_MODE_DSCP;

		/* set default DSCP QoS values */
		new_cfg->etscfg.willing = 0;
		new_cfg->pfc.pfccap = max_tc;
		new_cfg->pfc.willing = 0;

		for (i = 0; i < max_tc; i++)
			for (j = 0; j < ICE_BYTES_PER_DSCP_VAL; j++) {
				int dscp, offset;

				dscp = (i * max_tc) + j;
				offset = max_tc * ICE_BYTES_PER_DSCP_VAL;

				new_cfg->dscp_map[dscp] = i;
				/* if less that 8 TCs supported */
				if (max_tc < ICE_MAX_TRAFFIC_CLASS)
					new_cfg->dscp_map[dscp + offset] = i;
			}

		new_cfg->etscfg.tcbwtable[0] = 100;
		new_cfg->etscfg.tsatable[0] = ICE_IEEE_TSA_ETS;
		new_cfg->etscfg.prio_table[0] = 0;

		for (i = 1; i < max_tc; i++) {
			new_cfg->etscfg.tcbwtable[i] = 0;
			new_cfg->etscfg.tsatable[i] = ICE_IEEE_TSA_ETS;
			new_cfg->etscfg.prio_table[i] = i;
		}
	} /* end of switching to DSCP mode */

	/* apply new mapping for this DSCP value */
	new_cfg->dscp_map[app->protocol] = app->priority;
	new_cfg->app[new_cfg->numapps++] = new_app;

	ret = ice_pf_dcb_cfg(pf, new_cfg, true);
	/* return of zero indicates new cfg applied */
	if (ret == ICE_DCB_HW_CHG_RST)
		ice_dcbnl_devreset(netdev);
	else
		ret = ICE_DCB_NO_HW_CHG;

setapp_out:
	mutex_unlock(&pf->tc_mutex);
	return ret;
}

/**
 * ice_dcbnl_delapp - Delete local IEEE App config
 * @netdev: relevant netdev
 * @app: struct to hold app too delete
 *
 * Will not delete first application required by the FW
 */
static int ice_dcbnl_delapp(struct net_device *netdev, struct dcb_app *app)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_dcbx_cfg *old_cfg, *new_cfg;
	unsigned int i, j;
	int ret = 0;

	if (pf->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED) {
		netdev_err(netdev, "can't delete DSCP netlink app when FW DCB agent is active\n");
		return -EINVAL;
	}

	mutex_lock(&pf->tc_mutex);
#ifdef NETIF_F_HW_TC
	if (ice_is_adq_active(pf)) {
		netdev_err(netdev, "can't set DCB configuration when ADQ is active\n");
		ret = ICE_DCB_NO_HW_CHG;
		goto delapp_out;
	}
#endif /* NETIF_F_HW_TC */
	old_cfg = &pf->hw.port_info->qos_cfg.local_dcbx_cfg;

	ret = dcb_ieee_delapp(netdev, app);
	if (ret)
		goto delapp_out;

	new_cfg = &pf->hw.port_info->qos_cfg.desired_dcbx_cfg;

	for (i = 0; i < new_cfg->numapps; i++) {
		if (app->selector == new_cfg->app[i].selector &&
		    app->protocol == new_cfg->app[i].prot_id &&
		    app->priority == new_cfg->app[i].priority) {
			new_cfg->app[i].selector = 0;
			new_cfg->app[i].prot_id = 0;
			new_cfg->app[i].priority = 0;
			break;
		}
	}

	/* Did not find DCB App */
	if (i == new_cfg->numapps) {
		ret = -EINVAL;
		goto delapp_out;
	}

	new_cfg->numapps--;

	for (j = i; j < new_cfg->numapps; j++) {
		new_cfg->app[j].selector = old_cfg->app[j + 1].selector;
		new_cfg->app[j].prot_id = old_cfg->app[j + 1].prot_id;
		new_cfg->app[j].priority = old_cfg->app[j + 1].priority;
	}

	/* if not a DSCP APP TLV or DSCP is not supported, we are done */
	if (app->selector != IEEE_8021QAZ_APP_SEL_DSCP ||
	    !ice_is_feature_supported(pf, ICE_F_DSCP)) {
		ret = ICE_DCB_HW_CHG;
		goto delapp_out;
	}

	/* if DSCP TLV, then need to address change in mapping */
	clear_bit(app->protocol, new_cfg->dscp_mapped);
	/* remap this DSCP value to default value */
	new_cfg->dscp_map[app->protocol] = app->protocol %
					   ICE_BYTES_PER_DSCP_VAL;

	/* if the last DSCP mapping just got deleted, need to switch
	 * to L2 VLAN QoS mode
	 */
	if (bitmap_empty(new_cfg->dscp_mapped, ICE_DSCP_NUM_VAL) &&
	    new_cfg->pfc_mode == ICE_QOS_MODE_DSCP) {

		ret = ice_aq_set_pfc_mode(&pf->hw,
					  ICE_AQC_PFC_VLAN_BASED_PFC,
					  NULL);
		if (ret) {
			netdev_info(netdev, "Failed to set VLAN PFC mode %d\n",
				    ret);
			goto delapp_out;
		}
		netdev_info(netdev, "Switched QoS to L2 VLAN mode\n");

		new_cfg->pfc_mode = ICE_QOS_MODE_VLAN;

		ret = ice_dcb_sw_dflt_cfg(pf, true, true);
	} else {
		ret = ice_pf_dcb_cfg(pf, new_cfg, true);
	}

	/* return of ICE_DCB_HW_CHG_RST indicates new cfg applied
	 * and reset needs to be performed
	 */
	if (ret == ICE_DCB_HW_CHG_RST)
		ice_dcbnl_devreset(netdev);

	/* if the change was not siginificant enough to actually call
	 * the reconfiguration flow, we still need to tell caller that
	 * their request was successfully handled
	 */
	if (ret == ICE_DCB_NO_HW_CHG)
		ret = ICE_DCB_HW_CHG;

delapp_out:
	mutex_unlock(&pf->tc_mutex);
	return ret;
}

/**
 * ice_dcbnl_cee_set_all - Commit CEE DCB settings to HW
 * @netdev: the corresponding netdev
 */
static u8 ice_dcbnl_cee_set_all(struct net_device *netdev)
{
	struct ice_pf *pf = ice_netdev_to_pf(netdev);
	struct ice_dcbx_cfg *new_cfg;
	int err;

	if ((pf->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED) ||
	    !(pf->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return ICE_DCB_NO_HW_CHG;

	new_cfg = &pf->hw.port_info->qos_cfg.desired_dcbx_cfg;

	mutex_lock(&pf->tc_mutex);
#ifdef NETIF_F_HW_TC
	if (ice_is_adq_active(pf)) {
		netdev_err(netdev, "can't set DCB configuration when ADQ is active\n");
		err = ICE_DCB_NO_HW_CHG;
		goto out;
	}
#endif /* NETIF_F_HW_TC */

	err = ice_pf_dcb_cfg(pf, new_cfg, true);

#ifdef NETIF_F_HW_TC
out:
#endif /* NETIF_F_HW_TC */
	mutex_unlock(&pf->tc_mutex);
	return (err != ICE_DCB_HW_CHG_RST) ? ICE_DCB_NO_HW_CHG : err;
}

static const struct dcbnl_rtnl_ops dcbnl_ops = {
	/* IEEE 802.1Qaz std */
	.ieee_getets = ice_dcbnl_getets,
	.ieee_setets = ice_dcbnl_setets,
	.ieee_getpfc = ice_dcbnl_getpfc,
	.ieee_setpfc = ice_dcbnl_setpfc,
	.ieee_setapp = ice_dcbnl_setapp,
	.ieee_delapp = ice_dcbnl_delapp,

	/* CEE std */
	.getstate = ice_dcbnl_getstate,
	.setstate = ice_dcbnl_setstate,
	.getpermhwaddr = ice_dcbnl_get_perm_hw_addr,
	.setpgtccfgtx = ice_dcbnl_set_pg_tc_cfg_tx,
	.setpgbwgcfgtx = ice_dcbnl_set_pg_bwg_cfg_tx,
	.setpgtccfgrx = ice_dcbnl_set_pg_tc_cfg_rx,
	.setpgbwgcfgrx = ice_dcbnl_set_pg_bwg_cfg_rx,
	.getpgtccfgtx = ice_dcbnl_get_pg_tc_cfg_tx,
	.getpgbwgcfgtx = ice_dcbnl_get_pg_bwg_cfg_tx,
	.getpgtccfgrx = ice_dcbnl_get_pg_tc_cfg_rx,
	.getpgbwgcfgrx = ice_dcbnl_get_pg_bwg_cfg_rx,
	.setpfccfg = ice_dcbnl_set_pfc_cfg,
	.getpfccfg = ice_dcbnl_get_pfc_cfg,
	.setall = ice_dcbnl_cee_set_all,
	.getcap = ice_dcbnl_get_cap,
	.getnumtcs = ice_dcbnl_getnumtcs,
	.getpfcstate = ice_dcbnl_getpfcstate,
	.getapp = ice_dcbnl_getapp,

	/* DCBX configuration */
	.getdcbx = ice_dcbnl_getdcbx,
	.setdcbx = ice_dcbnl_setdcbx,
};

/**
 * ice_dcbnl_set_all - set all the apps and ieee data from DCBX config
 * @vsi: pointer to VSI struct
 */
void ice_dcbnl_set_all(struct ice_vsi *vsi)
{
	struct net_device *netdev = vsi->netdev;
	struct ice_dcbx_cfg *dcbxcfg;
	struct ice_port_info *pi;
	struct dcb_app sapp;
	struct ice_pf *pf;
	unsigned int i;

	if (!netdev)
		return;

	pf = ice_netdev_to_pf(netdev);
	pi = pf->hw.port_info;

	/* SW DCB taken care of by SW Default Config */
	if (pf->dcbx_cap & DCB_CAP_DCBX_HOST)
		return;

	/* DCB not enabled */
	if (!test_bit(ICE_FLAG_DCB_ENA, pf->flags))
		return;

	dcbxcfg = &pi->qos_cfg.local_dcbx_cfg;

	for (i = 0; i < dcbxcfg->numapps; i++) {
		u8 prio, tc_map;

		prio = dcbxcfg->app[i].priority;
		tc_map = BIT(dcbxcfg->etscfg.prio_table[prio]);

		/* Add APP only if the TC is enabled for this VSI */
		if (tc_map & vsi->tc_cfg.ena_tc) {
			sapp.selector = dcbxcfg->app[i].selector;
			sapp.protocol = dcbxcfg->app[i].prot_id;
			sapp.priority = prio;
			dcb_ieee_setapp(netdev, &sapp);
		}
	}
#ifdef HAVE_DCBNL_IEEE_DELAPP
	/* Notify user-space of the changes */
	dcbnl_ieee_notify(netdev, RTM_SETDCB, DCB_CMD_IEEE_SET, 0, 0);
#endif /* HAVE_DCBNL_IEEE_DELAPP */
}

/**
 * ice_dcbnl_vsi_del_app - Delete APP on all VSIs
 * @vsi: pointer to the main VSI
 * @app: APP to delete
 *
 * Delete given APP from all the VSIs for given PF
 */
static void
ice_dcbnl_vsi_del_app(struct ice_vsi *vsi,
		      struct ice_dcb_app_priority_table *app)
{
	struct dcb_app sapp;
	int err;

	sapp.selector = app->selector;
	sapp.protocol = app->prot_id;
	sapp.priority = app->priority;
	err = ice_dcbnl_delapp(vsi->netdev, &sapp);
	dev_dbg(ice_pf_to_dev(vsi->back), "Deleting app for VSI idx=%d err=%d sel=%d proto=0x%x, prio=%d\n",
		vsi->idx, err, app->selector, app->prot_id, app->priority);
}

/**
 * ice_dcbnl_flush_apps - Delete all removed APPs
 * @pf: the corresponding PF
 * @old_cfg: old DCBX configuration data
 * @new_cfg: new DCBX configuration data
 *
 * Find and delete all APPS that are not present in the passed
 * DCB configuration
 */
void
ice_dcbnl_flush_apps(struct ice_pf *pf, struct ice_dcbx_cfg *old_cfg,
		     struct ice_dcbx_cfg *new_cfg)
{
	struct ice_vsi *main_vsi = ice_get_main_vsi(pf);
	unsigned int i;

	if (!main_vsi)
		return;

	for (i = 0; i < old_cfg->numapps; i++) {
		struct ice_dcb_app_priority_table app = old_cfg->app[i];

		/* The APP is not available anymore delete it */
		if (!ice_dcbnl_find_app(new_cfg, &app))
			ice_dcbnl_vsi_del_app(main_vsi, &app);
	}
}

/**
 * ice_dcbnl_setup - setup DCBNL
 * @vsi: VSI to get associated netdev from
 */
void ice_dcbnl_setup(struct ice_vsi *vsi)
{
	struct net_device *netdev = vsi->netdev;
	struct ice_pf *pf;

	pf = ice_netdev_to_pf(netdev);
	if (!test_bit(ICE_FLAG_DCB_CAPABLE, pf->flags))
		return;

	netdev->dcbnl_ops = &dcbnl_ops;
	ice_dcbnl_set_all(vsi);
}
