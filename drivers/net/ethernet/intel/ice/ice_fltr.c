// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2021, Intel Corporation. */

#include "ice.h"
#include "ice_fltr.h"

/**
 * ice_fltr_free_list - free filter lists helper
 * @dev: pointer to the device struct
 * @h: pointer to the list head to be freed
 *
 * Helper function to free filter lists previously created using
 * ice_fltr_add_mac_to_list
 */
void ice_fltr_free_list(struct device *dev, struct list_head *h)
{
	struct ice_fltr_list_entry *e, *tmp;

	list_for_each_entry_safe(e, tmp, h, list_entry) {
		list_del(&e->list_entry);
		devm_kfree(dev, e);
	}
}

/**
 * ice_fltr_add_entry_to_list - allocate and add filter entry to list
 * @dev: pointer to device needed by alloc function
 * @info: filter info struct that gets added to the passed in list
 * @list: pointer to the list which contains MAC filters entry
 */
static int
ice_fltr_add_entry_to_list(struct device *dev, struct ice_fltr_info *info,
			   struct list_head *list)
{
	struct ice_fltr_list_entry *entry;

	entry = devm_kzalloc(dev, sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return -ENOMEM;

	entry->fltr_info = *info;

	INIT_LIST_HEAD(&entry->list_entry);
	list_add(&entry->list_entry, list);

	return 0;
}

/**
 * ice_fltr_set_vlan_vsi_promisc
 * @hw: pointer to the hardware structure
 * @vsi: the VSI being configured
 * @promisc_mask: mask of promiscuous config bits
 *
 * Set VSI with all associated VLANs to given promiscuous mode(s)
 */
enum ice_status
ice_fltr_set_vlan_vsi_promisc(struct ice_hw *hw, struct ice_vsi *vsi, u8 promisc_mask)
{
	return ice_set_vlan_vsi_promisc(hw, vsi->idx, promisc_mask, false);
}

/**
 * ice_fltr_clear_vlan_vsi_promisc
 * @hw: pointer to the hardware structure
 * @vsi: the VSI being configured
 * @promisc_mask: mask of promiscuous config bits
 *
 * Clear VSI with all associated VLANs to given promiscuous mode(s)
 */
enum ice_status
ice_fltr_clear_vlan_vsi_promisc(struct ice_hw *hw, struct ice_vsi *vsi, u8 promisc_mask)
{
	return ice_set_vlan_vsi_promisc(hw, vsi->idx, promisc_mask, true);
}

/**
 * ice_fltr_clear_vsi_promisc - clear specified promiscuous mode(s)
 * @hw: pointer to the hardware structure
 * @vsi_handle: VSI handle to clear mode
 * @promisc_mask: mask of promiscuous config bits to clear
 * @vid: VLAN ID to clear VLAN promiscuous
 * @lport: logical port number to clear mode
 */
enum ice_status
ice_fltr_clear_vsi_promisc(struct ice_hw *hw, u16 vsi_handle, u8 promisc_mask,
			   u16 vid, u8 lport)
{
	return ice_clear_vsi_promisc(hw, vsi_handle, promisc_mask, vid);
}

/**
 * ice_fltr_set_vsi_promisc - set given VSI to given promiscuous mode(s)
 * @hw: pointer to the hardware structure
 * @vsi_handle: VSI handle to configure
 * @promisc_mask: mask of promiscuous config bits
 * @vid: VLAN ID to set VLAN promiscuous
 * @lport: logical port number to set promiscuous mode
 */
enum ice_status
ice_fltr_set_vsi_promisc(struct ice_hw *hw, u16 vsi_handle, u8 promisc_mask,
			 u16 vid, u8 lport)
{
	return ice_set_vsi_promisc(hw, vsi_handle, promisc_mask, vid);
}

/**
 * ice_fltr_add_mac_list - add list of MAC filters
 * @vsi: pointer to VSI struct
 * @list: list of filters
 */
enum ice_status
ice_fltr_add_mac_list(struct ice_vsi *vsi, struct list_head *list)
{
	return ice_add_mac(&vsi->back->hw, list);
}

/**
 * ice_fltr_remove_mac_list - remove list of MAC filters
 * @vsi: pointer to VSI struct
 * @list: list of filters
 */
enum ice_status
ice_fltr_remove_mac_list(struct ice_vsi *vsi, struct list_head *list)
{
	return ice_remove_mac(&vsi->back->hw, list);
}

/**
 * ice_fltr_add_vlan_list - add list of VLAN filters
 * @vsi: pointer to VSI struct
 * @list: list of filters
 */
static enum ice_status
ice_fltr_add_vlan_list(struct ice_vsi *vsi, struct list_head *list)
{
	return ice_add_vlan(&vsi->back->hw, list);
}

/**
 * ice_fltr_remove_vlan_list - remove list of VLAN filters
 * @vsi: pointer to VSI struct
 * @list: list of filters
 */
static enum ice_status
ice_fltr_remove_vlan_list(struct ice_vsi *vsi, struct list_head *list)
{
	return ice_remove_vlan(&vsi->back->hw, list);
}

/**
 * ice_fltr_add_mac_vlan_list - add list of MAC VLAN filters
 * @vsi: pointer to VSI struct
 * @list: list of filters
 */
static enum ice_status
ice_fltr_add_mac_vlan_list(struct ice_vsi *vsi, struct list_head *list)
{
	return ice_add_mac_vlan(&vsi->back->hw, list);
}

/**
 * ice_fltr_remove_mac_vlan_list - remove list of MAC VLAN filters
 * @vsi: pointer to VSI struct
 * @list: list of filters
 */
static enum ice_status
ice_fltr_remove_mac_vlan_list(struct ice_vsi *vsi, struct list_head *list)
{
	return ice_remove_mac_vlan(&vsi->back->hw, list);
}

/**
 * ice_fltr_add_eth_list - add list of ethertype filters
 * @vsi: pointer to VSI struct
 * @list: list of filters
 */
static enum ice_status
ice_fltr_add_eth_list(struct ice_vsi *vsi, struct list_head *list)
{
	return ice_add_eth_mac(&vsi->back->hw, list);
}

/**
 * ice_fltr_remove_eth_list - remove list of ethertype filters
 * @vsi: pointer to VSI struct
 * @list: list of filters
 */
static enum ice_status
ice_fltr_remove_eth_list(struct ice_vsi *vsi, struct list_head *list)
{
	return ice_remove_eth_mac(&vsi->back->hw, list);
}

/**
 * ice_fltr_remove_all - remove all filters associated with VSI
 * @vsi: pointer to VSI struct
 */
void ice_fltr_remove_all(struct ice_vsi *vsi)
{
	ice_remove_vsi_fltr(&vsi->back->hw, vsi->idx);
}

/**
 * ice_fltr_add_mac_to_list - add MAC filter info to exsisting list
 * @vsi: pointer to VSI struct
 * @list: list to add filter info to
 * @mac: MAC address to add
 * @action: filter action
 */
int
ice_fltr_add_mac_to_list(struct ice_vsi *vsi, struct list_head *list,
			 const u8 *mac, enum ice_sw_fwd_act_type action)
{
	struct ice_fltr_info info = { 0 };

	info.flag = ICE_FLTR_TX;
	info.src_id = ICE_SRC_ID_VSI;
	info.lkup_type = ICE_SW_LKUP_MAC;
	info.fltr_act = action;
	info.vsi_handle = vsi->idx;

	ether_addr_copy(info.l_data.mac.mac_addr, mac);

	return ice_fltr_add_entry_to_list(ice_pf_to_dev(vsi->back), &info,
					  list);
}

/**
 * ice_fltr_add_vlan_to_list - add VLAN filter info to exsisting list
 * @vsi: pointer to VSI struct
 * @list: list to add filter info to
 * @vlan: VLAN filter details
 */
static int
ice_fltr_add_vlan_to_list(struct ice_vsi *vsi, struct list_head *list,
			  struct ice_vlan *vlan)
{
	struct ice_fltr_info info = { 0 };

	info.flag = ICE_FLTR_TX;
	info.src_id = ICE_SRC_ID_VSI;
	info.lkup_type = ICE_SW_LKUP_VLAN;
	info.fltr_act = vlan->fwd_act;
	info.vsi_handle = vsi->idx;
	info.l_data.vlan.vlan_id = vlan->vid;
	info.l_data.vlan.tpid = vlan->tpid;
	info.l_data.vlan.tpid_valid = true;

	return ice_fltr_add_entry_to_list(ice_pf_to_dev(vsi->back), &info,
					  list);
}

/**
 * ice_fltr_add_mac_vlan_to_list - add MAC VLAN filter info to
 * exsisting list
 * @vsi: pointer to VSI struct
 * @list: list to add filter info to
 * @mac: MAC addr to add
 * @vlan_id: VLAN ID to add
 * @action: filter action
 */
static int
ice_fltr_add_mac_vlan_to_list(struct ice_vsi *vsi, struct list_head *list,
			      const u8 *mac, u16 vlan_id,
			      enum ice_sw_fwd_act_type action)
{
	struct ice_fltr_info info = { 0 };

	if (!is_valid_ether_addr(mac) ||
	    is_broadcast_ether_addr(mac) || !vlan_id)
		return -EINVAL;

	info.flag = ICE_FLTR_TX_RX;
	info.lkup_type = ICE_SW_LKUP_MAC_VLAN;
	info.fltr_act = action;
	info.vsi_handle = vsi->idx;
	info.src = vsi->vsi_num;

	info.l_data.mac_vlan.vlan_id = vlan_id;
	ether_addr_copy(info.l_data.mac_vlan.mac_addr, mac);

	return ice_fltr_add_entry_to_list(ice_pf_to_dev(vsi->back), &info,
					  list);
}

/**
 * ice_fltr_add_eth_to_list - add ethertype filter info to exsisting list
 * @vsi: pointer to VSI struct
 * @list: list to add filter info to
 * @ethertype: ethertype of packet that matches filter
 * @flag: filter direction, Tx or Rx
 * @action: filter action
 */
static int
ice_fltr_add_eth_to_list(struct ice_vsi *vsi, struct list_head *list,
			 u16 ethertype, u16 flag,
			 enum ice_sw_fwd_act_type action)
{
	struct ice_fltr_info info = { 0 };

	info.flag = flag;
	info.lkup_type = ICE_SW_LKUP_ETHERTYPE;
	info.fltr_act = action;
	info.vsi_handle = vsi->idx;
	info.l_data.ethertype_mac.ethertype = ethertype;

	if (flag == ICE_FLTR_TX)
		info.src_id = ICE_SRC_ID_VSI;
	else
		info.src_id = ICE_SRC_ID_LPORT;

	return ice_fltr_add_entry_to_list(ice_pf_to_dev(vsi->back), &info,
					  list);
}

/**
 * ice_fltr_prepare_mac - add or remove MAC rule
 * @vsi: pointer to VSI struct
 * @mac: MAC address to add
 * @action: action to be performed on filter match
 * @mac_action: pointer to add or remove MAC function
 */
static enum ice_status
ice_fltr_prepare_mac(struct ice_vsi *vsi, const u8 *mac,
		     enum ice_sw_fwd_act_type action,
		     enum ice_status (*mac_action)(struct ice_vsi *,
						   struct list_head *))
{
	enum ice_status result;
	LIST_HEAD(tmp_list);

	if (ice_fltr_add_mac_to_list(vsi, &tmp_list, mac, action)) {
		ice_fltr_free_list(ice_pf_to_dev(vsi->back), &tmp_list);
		return ICE_ERR_NO_MEMORY;
	}

	result = mac_action(vsi, &tmp_list);
	ice_fltr_free_list(ice_pf_to_dev(vsi->back), &tmp_list);
	return result;
}

/**
 * ice_fltr_prepare_mac_and_broadcast - add or remove MAC and broadcast filter
 * @vsi: pointer to VSI struct
 * @mac: MAC address to add
 * @action: action to be performed on filter match
 * @mac_action: pointer to add or remove MAC function
 */
static enum ice_status
ice_fltr_prepare_mac_and_broadcast(struct ice_vsi *vsi, const u8 *mac,
				   enum ice_sw_fwd_act_type action,
				   enum ice_status(*mac_action)
				   (struct ice_vsi *, struct list_head *))
{
	u8 broadcast[ETH_ALEN];
	enum ice_status result;
	LIST_HEAD(tmp_list);

	eth_broadcast_addr(broadcast);
	if (ice_fltr_add_mac_to_list(vsi, &tmp_list, mac, action) ||
	    ice_fltr_add_mac_to_list(vsi, &tmp_list, broadcast, action)) {
		ice_fltr_free_list(ice_pf_to_dev(vsi->back), &tmp_list);
		return ICE_ERR_NO_MEMORY;
	}

	result = mac_action(vsi, &tmp_list);
	ice_fltr_free_list(ice_pf_to_dev(vsi->back), &tmp_list);
	return result;
}

/**
 * ice_fltr_prepare_vlan - add or remove VLAN filter
 * @vsi: pointer to VSI struct
 * @vlan: VLAN filter details
 * @vlan_action: pointer to add or remove VLAN function
 */
static enum ice_status
ice_fltr_prepare_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan,
		      enum ice_status (*vlan_action)(struct ice_vsi *,
						     struct list_head *))
{
	enum ice_status result;
	LIST_HEAD(tmp_list);

	if (ice_fltr_add_vlan_to_list(vsi, &tmp_list, vlan))
		return ICE_ERR_NO_MEMORY;

	result = vlan_action(vsi, &tmp_list);
	ice_fltr_free_list(ice_pf_to_dev(vsi->back), &tmp_list);
	return result;
}

/**
 * ice_fltr_prepare_mac_vlan - add or remove MAC VLAN filter
 * @vsi: pointer to VSI struct
 * @mac: MAC address to add
 * @vlan_id: VLAN ID to add
 * @action: action to be performed on filter match
 * @mac_vlan_action: pointer to add or remove MAC VLAN function
 */
static enum ice_status
ice_fltr_prepare_mac_vlan(struct ice_vsi *vsi, const u8 *mac, u16 vlan_id,
			  enum ice_sw_fwd_act_type action,
			  enum ice_status (mac_vlan_action)(struct ice_vsi *,
							    struct list_head *))
{
	enum ice_status result;
	LIST_HEAD(tmp_list);

	if (ice_fltr_add_mac_vlan_to_list(vsi, &tmp_list, mac, vlan_id,
					  action))
		return ICE_ERR_NO_MEMORY;

	result = mac_vlan_action(vsi, &tmp_list);
	ice_fltr_free_list(ice_pf_to_dev(vsi->back), &tmp_list);
	return result;
}

/**
 * ice_fltr_prepare_eth - add or remove ethertype filter
 * @vsi: pointer to VSI struct
 * @ethertype: ethertype of packet to be filtered
 * @flag: direction of packet, Tx or Rx
 * @action: action to be performed on filter match
 * @eth_action: pointer to add or remove ethertype function
 */
static enum ice_status
ice_fltr_prepare_eth(struct ice_vsi *vsi, u16 ethertype, u16 flag,
		     enum ice_sw_fwd_act_type action,
		     enum ice_status (*eth_action)(struct ice_vsi *,
						   struct list_head *))
{
	enum ice_status result;
	LIST_HEAD(tmp_list);

	if (ice_fltr_add_eth_to_list(vsi, &tmp_list, ethertype, flag, action))
		return ICE_ERR_NO_MEMORY;

	result = eth_action(vsi, &tmp_list);
	ice_fltr_free_list(ice_pf_to_dev(vsi->back), &tmp_list);
	return result;
}

/**
 * ice_fltr_add_mac - add single MAC filter
 * @vsi: pointer to VSI struct
 * @mac: MAC to add
 * @action: action to be performed on filter match
 */
enum ice_status ice_fltr_add_mac(struct ice_vsi *vsi, const u8 *mac,
				 enum ice_sw_fwd_act_type action)
{
	return ice_fltr_prepare_mac(vsi, mac, action, ice_fltr_add_mac_list);
}

/**
 * ice_fltr_add_mac_and_broadcast - add single MAC and broadcast
 * @vsi: pointer to VSI struct
 * @mac: MAC to add
 * @action: action to be performed on filter match
 */
enum ice_status
ice_fltr_add_mac_and_broadcast(struct ice_vsi *vsi, const u8 *mac,
			       enum ice_sw_fwd_act_type action)
{
	return ice_fltr_prepare_mac_and_broadcast(vsi, mac, action,
						  ice_fltr_add_mac_list);
}

/**
 * ice_fltr_remove_mac - remove MAC filter
 * @vsi: pointer to VSI struct
 * @mac: filter MAC to remove
 * @action: action to remove
 */
enum ice_status ice_fltr_remove_mac(struct ice_vsi *vsi, const u8 *mac,
				    enum ice_sw_fwd_act_type action)
{
	return ice_fltr_prepare_mac(vsi, mac, action, ice_fltr_remove_mac_list);
}

/**
 * ice_fltr_add_vlan - add single VLAN filter
 * @vsi: pointer to VSI struct
 * @vlan: VLAN filter details
 */
enum ice_status ice_fltr_add_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan)
{
	return ice_fltr_prepare_vlan(vsi, vlan, ice_fltr_add_vlan_list);
}

/**
 * ice_fltr_remove_vlan - remove VLAN filter
 * @vsi: pointer to VSI struct
 * @vlan: VLAN filter details
 */
enum ice_status ice_fltr_remove_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan)
{
	return ice_fltr_prepare_vlan(vsi, vlan, ice_fltr_remove_vlan_list);
}

/**
 * ice_fltr_add_mac_vlan - add single MAC VLAN filter
 * @vsi: pointer to VSI struct
 * @mac: MAC address to add
 * @vlan_id: VLAN ID to add
 * @action: action to be performed on filter match
 */
enum ice_status
ice_fltr_add_mac_vlan(struct ice_vsi *vsi, const u8 *mac, u16 vlan_id,
		      enum ice_sw_fwd_act_type action)
{
	return ice_fltr_prepare_mac_vlan(vsi, mac, vlan_id, action,
					 ice_fltr_add_mac_vlan_list);
}

/**
 * ice_fltr_remove_mac_vlan - remove MAC VLAN filter
 * @vsi: pointer to VSI struct
 * @mac: MAC address to add
 * @vlan_id: filter MAC VLAN to remove
 * @action: action to remove
 */
enum ice_status
ice_fltr_remove_mac_vlan(struct ice_vsi *vsi, const u8 *mac, u16 vlan_id,
			 enum ice_sw_fwd_act_type action)
{
	return ice_fltr_prepare_mac_vlan(vsi, mac, vlan_id, action,
					 ice_fltr_remove_mac_vlan_list);
}

/**
 * ice_fltr_add_eth - add specyfic ethertype filter
 * @vsi: pointer to VSI struct
 * @ethertype: ethertype of filter
 * @flag: direction of packet to be filtered, Tx or Rx
 * @action: action to be performed on filter match
 */
enum ice_status ice_fltr_add_eth(struct ice_vsi *vsi, u16 ethertype, u16 flag,
				 enum ice_sw_fwd_act_type action)
{
	return ice_fltr_prepare_eth(vsi, ethertype, flag, action,
				    ice_fltr_add_eth_list);
}

/**
 * ice_fltr_remove_eth - remove ethertype filter
 * @vsi: pointer to VSI struct
 * @ethertype: ethertype of filter
 * @flag: direction of filter
 * @action: action to remove
 */
enum ice_status ice_fltr_remove_eth(struct ice_vsi *vsi, u16 ethertype,
				    u16 flag, enum ice_sw_fwd_act_type action)
{
	return ice_fltr_prepare_eth(vsi, ethertype, flag, action,
				    ice_fltr_remove_eth_list);
}

/**
 * ice_fltr_update_rule_flags - update lan_en/lb_en flags
 * @hw: pointer to hw
 * @rule_id: id of rule being updated
 * @recipe_id: recipe id of rule
 * @act: current action field
 * @type: Rx or Tx
 * @src: source VSI
 * @new_flags: combinations of lb_en and lan_en
 */
static enum ice_status
ice_fltr_update_rule_flags(struct ice_hw *hw, u16 rule_id, u16 recipe_id,
			   u32 act, u16 type, u16 src, u32 new_flags)
{
	struct ice_aqc_sw_rules_elem *s_rule;
	enum ice_status err;
	u32 flags_mask;

	s_rule = kzalloc(ICE_SW_RULE_RX_TX_NO_HDR_SIZE, GFP_KERNEL);
	if (!s_rule)
		return ICE_ERR_NO_MEMORY;

	flags_mask = ICE_SINGLE_ACT_LB_ENABLE | ICE_SINGLE_ACT_LAN_ENABLE;
	act &= ~flags_mask;
	act |= (flags_mask & new_flags);

	s_rule->pdata.lkup_tx_rx.recipe_id = cpu_to_le16(recipe_id);
	s_rule->pdata.lkup_tx_rx.index = cpu_to_le16(rule_id);
	s_rule->pdata.lkup_tx_rx.act = cpu_to_le32(act);

	if (type & ICE_FLTR_RX) {
		s_rule->pdata.lkup_tx_rx.src =
			cpu_to_le16(hw->port_info->lport);
		s_rule->type = cpu_to_le16(ICE_AQC_SW_RULES_T_LKUP_RX);

	} else {
		s_rule->pdata.lkup_tx_rx.src = cpu_to_le16(src);
		s_rule->type = cpu_to_le16(ICE_AQC_SW_RULES_T_LKUP_TX);
	}

	err = ice_aq_sw_rules(hw, s_rule, ICE_SW_RULE_RX_TX_NO_HDR_SIZE, 1,
			      ice_aqc_opc_update_sw_rules, NULL);

	kfree(s_rule);
	return err;
}

/**
 * ice_fltr_build_action - build action for rule
 * @vsi_id: id of VSI which is use to build action
 */
static u32
ice_fltr_build_action(u16 vsi_id)
{
	return ((vsi_id << ICE_SINGLE_ACT_VSI_ID_S) & ICE_SINGLE_ACT_VSI_ID_M) |
		ICE_SINGLE_ACT_VSI_FORWARDING | ICE_SINGLE_ACT_VALID_BIT;
}

/**
 * ice_fltr_find_adv_entry - find advanced rule
 * @rules: list of rules
 * @rule_id: id of wanted rule
 */
static struct ice_adv_fltr_mgmt_list_entry *
ice_fltr_find_adv_entry(struct list_head *rules, u16 rule_id)
{
	struct ice_adv_fltr_mgmt_list_entry *entry;

	list_for_each_entry(entry, rules, list_entry) {
		if (entry->rule_info.fltr_rule_id == rule_id)
			return entry;
	}

	return NULL;
}

/**
 * ice_fltr_update_adv_rule_flags - update flags on advanced rule
 * @vsi: pointer to VSI
 * @recipe_id: id of recipe
 * @entry: advanced rule entry
 * @new_flags: flags to update
 */
static enum ice_status
ice_fltr_update_adv_rule_flags(struct ice_vsi *vsi, u16 recipe_id,
			       struct ice_adv_fltr_mgmt_list_entry *entry,
			       u32 new_flags)
{
	struct ice_adv_rule_info *info = &entry->rule_info;
	struct ice_sw_act_ctrl *act = &info->sw_act;
	u32 action;

	if (act->fltr_act != ICE_FWD_TO_VSI)
		return ICE_ERR_NOT_SUPPORTED;

	action = ice_fltr_build_action(act->fwd_id.hw_vsi_id);

	return ice_fltr_update_rule_flags(&vsi->back->hw, info->fltr_rule_id,
					  recipe_id, action, info->sw_act.flag,
					  act->src, new_flags);
}

/**
 * ice_fltr_find_regular_entry - find regular rule
 * @rules: list of rules
 * @rule_id: id of wanted rule
 */
static struct ice_fltr_mgmt_list_entry *
ice_fltr_find_regular_entry(struct list_head *rules, u16 rule_id)
{
	struct ice_fltr_mgmt_list_entry *entry;

	list_for_each_entry(entry, rules, list_entry) {
		if (entry->fltr_info.fltr_rule_id == rule_id)
			return entry;
	}

	return NULL;
}

/**
 * ice_fltr_update_regular_rule - update flags on regular rule
 * @vsi: pointer to VSI
 * @recipe_id: id of recipe
 * @entry: regular rule entry
 * @new_flags: flags to update
 */
static enum ice_status
ice_fltr_update_regular_rule(struct ice_vsi *vsi, u16 recipe_id,
			     struct ice_fltr_mgmt_list_entry *entry,
			     u32 new_flags)
{
	struct ice_fltr_info *info = &entry->fltr_info;
	u32 action;

	if (info->fltr_act != ICE_FWD_TO_VSI)
		return ICE_ERR_NOT_SUPPORTED;

	action = ice_fltr_build_action(info->fwd_id.hw_vsi_id);

	return ice_fltr_update_rule_flags(&vsi->back->hw, info->fltr_rule_id,
					  recipe_id, action, info->flag,
					  info->src, new_flags);
}

/**
 * ice_fltr_update_flags - update flags on rule
 * @vsi: pointer to VSI
 * @rule_id: id of rule
 * @recipe_id: id of recipe
 * @new_flags: flags to update
 *
 * Function updates flags on regular and advance rule.
 *
 * Flags should be a combination of ICE_SINGLE_ACT_LB_ENABLE and
 * ICE_SINGLE_ACT_LAN_ENABLE.
 */
enum ice_status
ice_fltr_update_flags(struct ice_vsi *vsi, u16 rule_id, u16 recipe_id,
		      u32 new_flags)
{
	struct ice_adv_fltr_mgmt_list_entry *adv_entry;
	struct ice_fltr_mgmt_list_entry *regular_entry;
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_sw_recipe *recp_list;
	struct list_head *fltr_rules;

	recp_list = &hw->switch_info->recp_list[recipe_id];
	if (!recp_list)
		return ICE_ERR_DOES_NOT_EXIST;

	fltr_rules = &recp_list->filt_rules;
	regular_entry = ice_fltr_find_regular_entry(fltr_rules, rule_id);
	if (regular_entry)
		return ice_fltr_update_regular_rule(vsi, recipe_id,
						    regular_entry, new_flags);

	adv_entry = ice_fltr_find_adv_entry(fltr_rules, rule_id);
	if (adv_entry)
		return ice_fltr_update_adv_rule_flags(vsi, recipe_id,
						      adv_entry, new_flags);

	return ICE_ERR_DOES_NOT_EXIST;
}

/**
 * ice_fltr_update_flags_dflt_rule - update flags on default rule
 * @vsi: pointer to VSI
 * @rule_id: id of rule
 * @direction: Tx or Rx
 * @new_flags: flags to update
 *
 * Function updates flags on default rule with ICE_SW_LKUP_DFLT.
 *
 * Flags should be a combination of ICE_SINGLE_ACT_LB_ENABLE and
 * ICE_SINGLE_ACT_LAN_ENABLE.
 */
enum ice_status
ice_fltr_update_flags_dflt_rule(struct ice_vsi *vsi, u16 rule_id, u8 direction,
				u32 new_flags)
{
	u32 action = ice_fltr_build_action(vsi->vsi_num);
	struct ice_hw *hw = &vsi->back->hw;

	return ice_fltr_update_rule_flags(hw, rule_id, ICE_SW_LKUP_DFLT, action,
					  direction, vsi->vsi_num, new_flags);
}
