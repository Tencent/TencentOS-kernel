/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2021, Intel Corporation. */

#ifndef _ICE_VLAN_MODE_H_
#define _ICE_VLAN_MODE_H_

#include "ice_osdep.h"

struct ice_hw;

bool ice_is_dvm_ena(struct ice_hw *hw);
enum ice_status ice_set_vlan_mode(struct ice_hw *hw);
void ice_post_pkg_dwnld_vlan_mode_cfg(struct ice_hw *hw);

#endif /* _ICE_VLAN_MODE_H */
