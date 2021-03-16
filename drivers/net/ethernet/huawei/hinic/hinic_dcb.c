// SPDX-License-Identifier: GPL-2.0-only
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": [NIC]" fmt

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>

#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_hw_mgmt.h"
#include "hinic_lld.h"
#include "hinic_nic_cfg.h"
#include "hinic_nic_dev.h"
#include "hinic_dcb.h"

#define DCB_HW_CFG_CHG		0
#define DCB_HW_CFG_NO_CHG	1
#define DCB_HW_CFG_ERR		2

#define DCB_CFG_CHG_PG_TX	0x1
#define DCB_CFG_CHG_PG_RX	0x2
#define DCB_CFG_CHG_PFC		0x4
#define DCB_CFG_CHG_UP_COS	0x8

u8 hinic_dcb_get_tc(struct hinic_dcb_config *dcb_cfg, int dir, u8 up)
{
	struct hinic_tc_cfg *tc_cfg = &dcb_cfg->tc_cfg[0];
	u8 tc = dcb_cfg->pg_tcs;

	if (!tc)
		return 0;

	for (tc--; tc; tc--) {
		if (BIT(up) & tc_cfg[tc].path[dir].up_map)
			break;
	}

	return tc;
}

#define UP_MAPPING(prio)	((u8)(1U << ((HINIC_DCB_UP_MAX - 1) - (prio))))

void hinic_dcb_config_init(struct hinic_nic_dev *nic_dev,
			   struct hinic_dcb_config *dcb_cfg)
{
	struct hinic_tc_cfg *tc;
	int i;

	memset(dcb_cfg->tc_cfg, 0, sizeof(dcb_cfg->tc_cfg));
	tc = &dcb_cfg->tc_cfg[0];
	/* All TC mapping to PG0 */
	for (i = 0; i < dcb_cfg->pg_tcs; i++) {
		tc = &dcb_cfg->tc_cfg[i];
		tc->path[HINIC_DCB_CFG_TX].pg_id = 0;
		tc->path[HINIC_DCB_CFG_TX].bw_pct = 100;
		tc->path[HINIC_DCB_CFG_TX].up_map = UP_MAPPING(i);
		tc->path[HINIC_DCB_CFG_RX].pg_id = 0;
		tc->path[HINIC_DCB_CFG_RX].bw_pct = 100;
		tc->path[HINIC_DCB_CFG_RX].up_map = UP_MAPPING(i);

		tc->pfc_en = false;
	}

	for (; i < HINIC_DCB_UP_MAX; i++) {
		tc->path[HINIC_DCB_CFG_TX].up_map |= UP_MAPPING(i);
		tc->path[HINIC_DCB_CFG_RX].up_map |= UP_MAPPING(i);
	}

	memset(dcb_cfg->bw_pct, 0, sizeof(dcb_cfg->bw_pct));
	/* Use PG0 in default, PG0's bw is 100% */
	dcb_cfg->bw_pct[HINIC_DCB_CFG_TX][0] = 100;
	dcb_cfg->bw_pct[HINIC_DCB_CFG_RX][0] = 100;
	dcb_cfg->pfc_state = false;
}

void hinic_init_ieee_settings(struct hinic_nic_dev *nic_dev)
{
	struct hinic_dcb_config *dcb_cfg = &nic_dev->dcb_cfg;
	struct ieee_ets *ets = &nic_dev->hinic_ieee_ets_default;
	struct ieee_pfc *pfc = &nic_dev->hinic_ieee_pfc;
	struct hinic_tc_attr *tc_attr;
	u8 i;

	memset(ets, 0x0, sizeof(struct ieee_ets));
	memset(&nic_dev->hinic_ieee_ets, 0x0, sizeof(struct ieee_ets));
	ets->ets_cap = dcb_cfg->pg_tcs;
	for (i = 0; i < HINIC_DCB_TC_MAX; i++) {
		tc_attr = &dcb_cfg->tc_cfg[i].path[HINIC_DCB_CFG_TX];
		ets->tc_tsa[i] = tc_attr->prio_type ?
			IEEE8021Q_TSA_STRICT : IEEE8021Q_TSA_ETS;
		ets->tc_tx_bw[i] = nic_dev->dcb_cfg.bw_pct[HINIC_DCB_CFG_TX][i];
		ets->tc_rx_bw[i] = nic_dev->dcb_cfg.bw_pct[HINIC_DCB_CFG_RX][i];
		ets->prio_tc[i] = hinic_dcb_get_tc(dcb_cfg,
						   HINIC_DCB_CFG_TX, i);
	}
	memcpy(&nic_dev->hinic_ieee_ets, ets, sizeof(struct ieee_ets));

	memset(pfc, 0x0, sizeof(struct ieee_pfc));
	pfc->pfc_cap = dcb_cfg->pfc_tcs;
	for (i = 0; i < dcb_cfg->pfc_tcs; i++) {
		if (dcb_cfg->tc_cfg[i].pfc_en)
			pfc->pfc_en |= (u8)BIT(i);
	}
}

static int hinic_set_up_cos_map(struct hinic_nic_dev *nic_dev,
				u8 num_cos, u8 *cos_up)
{
	u8 up_valid_bitmap, up_cos[HINIC_DCB_UP_MAX] = {0};
	u8 i;

	up_valid_bitmap = 0;
	for (i = 0; i < num_cos; i++) {
		if (cos_up[i] >= HINIC_DCB_UP_MAX) {
			hinic_info(nic_dev, drv, "Invalid up %d mapping to cos %d\n",
				   cos_up[i], i);
			return -EFAULT;
		}

		if (i > 0 && cos_up[i] >= cos_up[i - 1]) {
			hinic_info(nic_dev, drv,
				   "Invalid priority order, should be descending cos[%d]=%d, cos[%d]=%d\n",
				   i, cos_up[i], i - 1, cos_up[i - 1]);
			return -EINVAL;
		}

		up_valid_bitmap |= (u8)BIT(cos_up[i]);
		if (i == (num_cos - 1))
			up_cos[cos_up[i]] = nic_dev->default_cos_id;
		else
			up_cos[cos_up[i]] = i;	/* reverse up and cos */
	}

	for (i = 0; i < HINIC_DCB_UP_MAX; i++) {
		if (up_valid_bitmap & (u8)BIT(i))
			continue;

		up_cos[i] = nic_dev->default_cos_id;
	}

	nic_dev->up_valid_bitmap = up_valid_bitmap;
	memcpy(nic_dev->up_cos, up_cos, sizeof(up_cos));

	return hinic_sq_cos_mapping(nic_dev->netdev);
}

static int hinic_init_up_cos_map(struct hinic_nic_dev *nic_dev, u8 num_cos)
{
	u8 default_map[HINIC_DCB_COS_MAX] = {0};
	bool setted = false;
	u8 max_cos, cos_id, up;
	int err;

	max_cos = hinic_max_num_cos(nic_dev->hwdev);
	if (!max_cos || ((max_cos - 1) < nic_dev->default_cos_id)) {
		hinic_err(nic_dev, drv, "Max_cos is %d, default cos id %d\n",
			  max_cos, nic_dev->default_cos_id);
		return -EFAULT;
	}

	err = hinic_get_chip_cos_up_map(nic_dev->pdev, &setted, default_map);
	if (err) {
		hinic_err(nic_dev, drv, "Get chip cos_up map failed\n");
		return -EFAULT;
	}

	if (!setted) {
		/* Use (max_cos-1)~0 as default user priority and mapping
		 * to cos0~(max_cos-1)
		 */
		up = nic_dev->max_cos - 1;
		for (cos_id = 0; cos_id < nic_dev->max_cos; cos_id++, up--)
			default_map[cos_id] = up;
	}

	return hinic_set_up_cos_map(nic_dev, num_cos, default_map);
}

int hinic_dcb_init(struct hinic_nic_dev *nic_dev)
{
	struct hinic_dcb_config *dcb_cfg = &nic_dev->dcb_cfg;
	u8 num_cos, support_cos = 0, default_cos = 0;
	u8 i, cos_valid_bitmap;
	int err;

	if (HINIC_FUNC_IS_VF(nic_dev->hwdev))
		return 0;

	cos_valid_bitmap = hinic_cos_valid_bitmap(nic_dev->hwdev);
	if (!cos_valid_bitmap) {
		hinic_err(nic_dev, drv, "None cos supported\n");
		return -EFAULT;
	}

	for (i = 0; i < HINIC_DCB_COS_MAX; i++) {
		if (cos_valid_bitmap & BIT(i)) {
			support_cos++;
			default_cos = i; /* Find max cos id as default cos */
		}
	}

	hinic_info(nic_dev, drv, "Support num cos %d, default cos %d\n",
		   support_cos, default_cos);

	num_cos = (u8)(1U << ilog2(support_cos));
	if (num_cos != support_cos)
		hinic_info(nic_dev, drv, "Adjust num_cos from %d to %d\n",
			   support_cos, num_cos);

	nic_dev->dcbx_cap = 0;
	nic_dev->max_cos = num_cos;
	nic_dev->default_cos_id = default_cos;
	dcb_cfg->pfc_tcs  = nic_dev->max_cos;
	dcb_cfg->pg_tcs   = nic_dev->max_cos;
	err = hinic_init_up_cos_map(nic_dev, num_cos);
	if (err) {
		hinic_info(nic_dev, drv, "Initialize up_cos mapping failed\n");
		return -EFAULT;
	}

	hinic_dcb_config_init(nic_dev, dcb_cfg);

	nic_dev->dcb_changes = DCB_CFG_CHG_PFC | DCB_CFG_CHG_PG_TX |
			       DCB_CFG_CHG_PG_RX | DCB_CFG_CHG_UP_COS;
	nic_dev->dcbx_cap = DCB_CAP_DCBX_HOST | DCB_CAP_DCBX_VER_CEE;

	memcpy(&nic_dev->tmp_dcb_cfg, &nic_dev->dcb_cfg,
	       sizeof(nic_dev->tmp_dcb_cfg));
	memcpy(&nic_dev->save_dcb_cfg, &nic_dev->dcb_cfg,
	       sizeof(nic_dev->save_dcb_cfg));

	hinic_init_ieee_settings(nic_dev);

	sema_init(&nic_dev->dcb_sem, 1);

	return 0;
}

void hinic_set_prio_tc_map(struct hinic_nic_dev *nic_dev)
{
	struct net_device *netdev = nic_dev->netdev;
	u8 prio, tc;

	for (prio = 0; prio < HINIC_DCB_UP_MAX; prio++) {
		tc = nic_dev->up_cos[prio];
		if (tc == nic_dev->default_cos_id)
			tc = nic_dev->max_cos - 1;

		netdev_set_prio_tc_map(netdev, prio, tc);
	}
}

int hinic_setup_tc(struct net_device *netdev, u8 tc)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	int err;

	if (!FUNC_SUPPORT_DCB(nic_dev->hwdev)) {
		nicif_err(nic_dev, drv, netdev,
			  "Current function doesn't support DCB\n");
		return -EOPNOTSUPP;
	}

	if (tc > nic_dev->dcb_cfg.pg_tcs) {
		nicif_err(nic_dev, drv, netdev, "Invalid num_tc: %d, max tc: %d\n",
			  tc, nic_dev->dcb_cfg.pg_tcs);
		return -EINVAL;
	}

	if (netif_running(netdev)) {
		err = hinic_close(netdev);
		if (err) {
			nicif_err(nic_dev, drv, netdev, "Failed to close device\n");
			return -EFAULT;
		}
	}

	if (tc) {
		/* TODO: verify if num_tc should be power of 2 */
		if (tc & (tc - 1)) {
			nicif_err(nic_dev, drv, netdev,
				  "Invalid num_tc: %d, must be power of 2\n",
				  tc);
			return -EINVAL;
		}

		netdev_set_num_tc(netdev, tc);
		hinic_set_prio_tc_map(nic_dev);

		set_bit(HINIC_DCB_ENABLE, &nic_dev->flags);
	} else {
		netdev_reset_tc(netdev);

		clear_bit(HINIC_DCB_ENABLE, &nic_dev->flags);
	}

	hinic_sq_cos_mapping(netdev);

	if (netif_running(netdev)) {
		err = hinic_open(netdev);
		if (err) {
			nicif_err(nic_dev, drv, netdev, "Failed to open device\n");
			return -EFAULT;
		}
	} else {
		hinic_update_num_qps(netdev);
	}

	hinic_configure_dcb(netdev);

	return 0;
}

u8 hinic_setup_dcb_tool(struct net_device *netdev, u8 *dcb_en, bool wr_flag)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	int err = 0;

	if (wr_flag) {
		if ((nic_dev->max_qps < nic_dev->dcb_cfg.pg_tcs) && *dcb_en) {
			netif_err(nic_dev, drv, netdev,
				  "max_qps:%d is less than %d\n",
				  nic_dev->max_qps, nic_dev->dcb_cfg.pg_tcs);
			return 1;
		}
		if (*dcb_en)
			set_bit(HINIC_DCB_ENABLE, &nic_dev->flags);
		else
			clear_bit(HINIC_DCB_ENABLE, &nic_dev->flags);
		/* hinic_setup_tc need get the nic_mutex lock again */
		mutex_unlock(&nic_dev->nic_mutex);
		/* kill the rtnl assert warning */
		rtnl_lock();
		err = hinic_setup_tc(netdev,
				     *dcb_en ? nic_dev->dcb_cfg.pg_tcs : 0);
		rtnl_unlock();
		mutex_lock(&nic_dev->nic_mutex);

		if (!err)
			netif_info(nic_dev, drv, netdev, "%s DCB\n",
				   *dcb_en ? "Enable" : "Disable");
	} else {
		*dcb_en = (u8)test_bit(HINIC_DCB_ENABLE, &nic_dev->flags);
	}

	return !!err;
}

static u8 hinic_dcbnl_get_state(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	return !!test_bit(HINIC_DCB_ENABLE, &nic_dev->flags);
}

static u8 hinic_dcbnl_set_state(struct net_device *netdev, u8 state)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u8 curr_state = !!test_bit(HINIC_DCB_ENABLE, &nic_dev->flags);
	int err = 0;

	if (!(nic_dev->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return 1;

	if (state == curr_state)
		return 0;

	if ((nic_dev->max_qps < nic_dev->dcb_cfg.pg_tcs) && state) {
		netif_err(nic_dev, drv, netdev,
			  "max_qps:%d is less than %d\n",
			  nic_dev->max_qps, nic_dev->dcb_cfg.pg_tcs);
		return 1;
	}

	err = hinic_setup_tc(netdev, state ? nic_dev->dcb_cfg.pg_tcs : 0);
	if (!err)
		netif_info(nic_dev, drv, netdev, "%s DCB\n",
			   state ? "Enable" : "Disable");

	return !!err;
}

static void hinic_dcbnl_get_perm_hw_addr(struct net_device *netdev,
					 u8 *perm_addr)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	int err;

	memset(perm_addr, 0xff, MAX_ADDR_LEN);

	err = hinic_get_default_mac(nic_dev->hwdev, perm_addr);
	if (err)
		nicif_err(nic_dev, drv, netdev, "Failed to get default mac\n");
}

void hinic_dcbnl_set_ets_tc_tool(struct net_device *netdev, u8 tc[], bool flag)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_tc_cfg *cfg = nic_dev->tmp_dcb_cfg.tc_cfg;
	struct hinic_tc_cfg *tc_conf = nic_dev->dcb_cfg.tc_cfg;
	u8 i, tc_tmp, j;

	if (flag) {
		/*need to clear first */
		for (i = 0; i < HINIC_DCB_TC_MAX; i++) {
			cfg[i].path[HINIC_DCB_CFG_TX].up_map = 0;
			cfg[i].path[HINIC_DCB_CFG_RX].up_map = 0;
		}
		for (i = 0; i < HINIC_DCB_TC_MAX; i++) {
			tc_tmp = tc[i];
			cfg[tc_tmp].path[HINIC_DCB_CFG_TX].up_map |= (u8)BIT(i);
			cfg[tc_tmp].path[HINIC_DCB_CFG_RX].up_map |= (u8)BIT(i);
			cfg[tc_tmp].path[HINIC_DCB_CFG_TX].pg_id = (u8)tc_tmp;
			cfg[tc_tmp].path[HINIC_DCB_CFG_RX].pg_id = (u8)tc_tmp;
		}
	} else {
		for (i = 0; i < HINIC_DCB_TC_MAX; i++) {
			for (j = 0; j < HINIC_DCB_TC_MAX; j++) {
				if (tc_conf[i].path[HINIC_DCB_CFG_TX].up_map &
					(u8)BIT(j)) {
					tc[j] = i;
				}
			}
		}
	}
}

void hinic_dcbnl_set_ets_pecent_tool(struct net_device *netdev,
				     u8 percent[], bool flag)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	int i;

	if (flag) {
		for (i = 0; i < HINIC_DCB_COS_MAX; i++) {
			nic_dev->tmp_dcb_cfg.bw_pct[HINIC_DCB_CFG_TX][i] =
				percent[i];
			nic_dev->tmp_dcb_cfg.bw_pct[HINIC_DCB_CFG_RX][i] =
				percent[i];
		}
	} else {
		for (i = 0; i < HINIC_DCB_COS_MAX; i++)
			percent[i] =
			nic_dev->dcb_cfg.bw_pct[HINIC_DCB_CFG_TX][i];
	}
}

static void hinic_dcbnl_set_pg_tc_cfg_tx(struct net_device *netdev, int tc,
					 u8 prio, u8 pg_id, u8 bw_pct,
					 u8 up_map)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (tc > HINIC_DCB_TC_MAX - 1)
		return;

	if (prio != DCB_ATTR_VALUE_UNDEFINED)
		nic_dev->tmp_dcb_cfg.tc_cfg[tc].path[0].prio_type = prio;
	if (pg_id != DCB_ATTR_VALUE_UNDEFINED)
		nic_dev->tmp_dcb_cfg.tc_cfg[tc].path[0].pg_id = pg_id;
	if (bw_pct != DCB_ATTR_VALUE_UNDEFINED)
		nic_dev->tmp_dcb_cfg.tc_cfg[tc].path[0].bw_pct = bw_pct;
	/* if all priority mapping to the same tc,
	 * up_map is 0xFF, and it's a valid value
	 */
	nic_dev->tmp_dcb_cfg.tc_cfg[tc].path[0].up_map = up_map;
}

static void hinic_dcbnl_set_pg_bwg_cfg_tx(struct net_device *netdev, int bwg_id,
					  u8 bw_pct)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (bwg_id > HINIC_DCB_PG_MAX - 1)
		return;

	nic_dev->tmp_dcb_cfg.bw_pct[0][bwg_id] = bw_pct;
}

static void hinic_dcbnl_set_pg_tc_cfg_rx(struct net_device *netdev, int tc,
					 u8 prio, u8 pg_id, u8 bw_pct,
					 u8 up_map)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (tc > HINIC_DCB_TC_MAX - 1)
		return;

	if (prio != DCB_ATTR_VALUE_UNDEFINED)
		nic_dev->tmp_dcb_cfg.tc_cfg[tc].path[1].prio_type = prio;
	if (pg_id != DCB_ATTR_VALUE_UNDEFINED)
		nic_dev->tmp_dcb_cfg.tc_cfg[tc].path[1].pg_id = pg_id;
	if (bw_pct != DCB_ATTR_VALUE_UNDEFINED)
		nic_dev->tmp_dcb_cfg.tc_cfg[tc].path[1].bw_pct = bw_pct;

	nic_dev->tmp_dcb_cfg.tc_cfg[tc].path[1].up_map = up_map;
}

static void hinic_dcbnl_set_pg_bwg_cfg_rx(struct net_device *netdev, int bwg_id,
					  u8 bw_pct)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (bwg_id > HINIC_DCB_PG_MAX - 1)
		return;

	nic_dev->tmp_dcb_cfg.bw_pct[1][bwg_id] = bw_pct;
}

static void hinic_dcbnl_get_pg_tc_cfg_tx(struct net_device *netdev, int tc,
					 u8 *prio, u8 *pg_id, u8 *bw_pct,
					 u8 *up_map)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (tc > HINIC_DCB_TC_MAX - 1)
		return;

	*prio = nic_dev->dcb_cfg.tc_cfg[tc].path[0].prio_type;
	*pg_id = nic_dev->dcb_cfg.tc_cfg[tc].path[0].pg_id;
	*bw_pct = nic_dev->dcb_cfg.tc_cfg[tc].path[0].bw_pct;
	*up_map = nic_dev->dcb_cfg.tc_cfg[tc].path[0].up_map;
}

static void hinic_dcbnl_get_pg_bwg_cfg_tx(struct net_device *netdev, int bwg_id,
					  u8 *bw_pct)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (bwg_id > HINIC_DCB_PG_MAX - 1)
		return;

	*bw_pct = nic_dev->dcb_cfg.bw_pct[0][bwg_id];
}

static void hinic_dcbnl_get_pg_tc_cfg_rx(struct net_device *netdev, int tc,
					 u8 *prio, u8 *pg_id, u8 *bw_pct,
					 u8 *up_map)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (tc > HINIC_DCB_TC_MAX - 1)
		return;

	*prio = nic_dev->dcb_cfg.tc_cfg[tc].path[1].prio_type;
	*pg_id = nic_dev->dcb_cfg.tc_cfg[tc].path[1].pg_id;
	*bw_pct = nic_dev->dcb_cfg.tc_cfg[tc].path[1].bw_pct;
	*up_map = nic_dev->dcb_cfg.tc_cfg[tc].path[1].up_map;
}

static void hinic_dcbnl_get_pg_bwg_cfg_rx(struct net_device *netdev, int bwg_id,
					  u8 *bw_pct)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (bwg_id > HINIC_DCB_PG_MAX - 1)
		return;

	*bw_pct = nic_dev->dcb_cfg.bw_pct[1][bwg_id];
}

void hinic_dcbnl_set_pfc_cfg_tool(struct net_device *netdev, u8 setting)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u8 i;

	for (i = 0; i < HINIC_DCB_TC_MAX; i++) {
		nic_dev->tmp_dcb_cfg.tc_cfg[i].pfc_en = !!(setting & BIT(i));
		if (nic_dev->tmp_dcb_cfg.tc_cfg[i].pfc_en !=
			nic_dev->dcb_cfg.tc_cfg[i].pfc_en) {
			nic_dev->tmp_dcb_cfg.pfc_state = true;
		}
	}
}

void hinic_dcbnl_set_ets_strict_tool(struct net_device *netdev,
				     u8 *setting, bool flag)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_tc_cfg *cfg = nic_dev->tmp_dcb_cfg.tc_cfg;
	struct hinic_tc_cfg *conf = nic_dev->dcb_cfg.tc_cfg;
	u8 i;

	if (flag) {
		for (i = 0; i < HINIC_DCB_COS_MAX; i++) {
			cfg[i].path[HINIC_DCB_CFG_TX].prio_type =
				!!(*setting & BIT(i)) ? 2 : 0;
			cfg[i].path[HINIC_DCB_CFG_RX].prio_type =
				!!(*setting & BIT(i)) ? 2 : 0;
		}
	} else {
		for (i = 0; i < HINIC_DCB_COS_MAX; i++) {
			*setting = *setting |
				(u8)((u32)(!!(conf[i].path[0].prio_type)) << i);
		}
	}
}

void hinic_dcbnl_set_pfc_en_tool(struct net_device *netdev,
				 u8 *value, bool flag)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (flag)
		nic_dev->tmp_dcb_cfg.pfc_state = !!(*value);
	else
		*value = nic_dev->tmp_dcb_cfg.pfc_state;
}

void hinic_dcbnl_set_ets_en_tool(struct net_device *netdev,
				 u8 *value, bool flag)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (flag) {
		if (*value)
			set_bit(HINIC_ETS_ENABLE, &nic_dev->flags);
		else
			clear_bit(HINIC_ETS_ENABLE, &nic_dev->flags);
	} else {
		*value = (u8)test_bit(HINIC_ETS_ENABLE, &nic_dev->flags);
	}
}

static void hinic_dcbnl_set_pfc_cfg(struct net_device *netdev, int prio,
				    u8 setting)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	nic_dev->tmp_dcb_cfg.tc_cfg[prio].pfc_en = !!setting;
	if (nic_dev->tmp_dcb_cfg.tc_cfg[prio].pfc_en !=
	    nic_dev->dcb_cfg.tc_cfg[prio].pfc_en)
		nic_dev->tmp_dcb_cfg.pfc_state = true;
}

void hinic_dcbnl_get_pfc_cfg_tool(struct net_device *netdev, u8 *setting)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u8 i;

	for (i = 0; i < HINIC_DCB_TC_MAX; i++) {
		*setting = *setting |
			(u8)((u32)(nic_dev->dcb_cfg.tc_cfg[i].pfc_en) << i);
	}
}

void hinic_dcbnl_get_tc_num_tool(struct net_device *netdev, u8 *tc_num)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	*tc_num = nic_dev->max_cos;
}

static void hinic_dcbnl_get_pfc_cfg(struct net_device *netdev, int prio,
				    u8 *setting)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (prio > HINIC_DCB_TC_MAX - 1)
		return;

	*setting = nic_dev->dcb_cfg.tc_cfg[prio].pfc_en;
}

static u8 hinic_dcbnl_getcap(struct net_device *netdev, int cap_id,
			     u8 *dcb_cap)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	switch (cap_id) {
	case DCB_CAP_ATTR_PG:
		*dcb_cap = true;
		break;
	case DCB_CAP_ATTR_PFC:
		*dcb_cap = true;
		break;
	case DCB_CAP_ATTR_UP2TC:
		*dcb_cap = false;
		break;
	case DCB_CAP_ATTR_PG_TCS:
		*dcb_cap = 0x80;
		break;
	case DCB_CAP_ATTR_PFC_TCS:
		*dcb_cap = 0x80;
		break;
	case DCB_CAP_ATTR_GSP:
		*dcb_cap = true;
		break;
	case DCB_CAP_ATTR_BCN:
		*dcb_cap = false;
		break;
	case DCB_CAP_ATTR_DCBX:
		*dcb_cap = nic_dev->dcbx_cap;
		break;
	default:
		*dcb_cap = false;
		break;
	}

	return 0;
}

static u8 hinic_sync_tc_cfg(struct hinic_tc_cfg *tc_dst,
			    struct hinic_tc_cfg *tc_src, int dir)
{
	u8 tc_dir_change = (dir == HINIC_DCB_CFG_TX) ?
			   DCB_CFG_CHG_PG_TX : DCB_CFG_CHG_PG_RX;
	u8 changes = 0;

	if (tc_dst->path[dir].prio_type != tc_src->path[dir].prio_type) {
		tc_dst->path[dir].prio_type = tc_src->path[dir].prio_type;
		changes |= tc_dir_change;
	}

	if (tc_dst->path[dir].pg_id != tc_src->path[dir].pg_id) {
		tc_dst->path[dir].pg_id = tc_src->path[dir].pg_id;
		changes |= tc_dir_change;
	}

	if (tc_dst->path[dir].bw_pct != tc_src->path[dir].bw_pct) {
		tc_dst->path[dir].bw_pct = tc_src->path[dir].bw_pct;
		changes |= tc_dir_change;
	}

	if (tc_dst->path[dir].up_map != tc_src->path[dir].up_map) {
		tc_dst->path[dir].up_map = tc_src->path[dir].up_map;
		changes |= (tc_dir_change | DCB_CFG_CHG_PFC);
	}

	return changes;
}

static u8 hinic_sync_dcb_cfg(struct hinic_nic_dev *nic_dev)
{
	struct hinic_dcb_config *dcb_cfg = &nic_dev->dcb_cfg;
	struct hinic_dcb_config *tmp_dcb_cfg = &nic_dev->tmp_dcb_cfg;
	struct hinic_tc_cfg *tc_dst, *tc_src;
	u8 changes = 0;
	int i;

	for (i = 0; i < HINIC_DCB_UP_MAX; i++) {
		tc_src = &tmp_dcb_cfg->tc_cfg[i];
		tc_dst = &dcb_cfg->tc_cfg[i];

		changes |= hinic_sync_tc_cfg(tc_dst, tc_src, HINIC_DCB_CFG_TX);
		changes |= hinic_sync_tc_cfg(tc_dst, tc_src, HINIC_DCB_CFG_RX);
	}

	for (i = 0; i < HINIC_DCB_PG_MAX; i++) {
		if (dcb_cfg->bw_pct[HINIC_DCB_CFG_TX][i] !=
			tmp_dcb_cfg->bw_pct[HINIC_DCB_CFG_TX][i]) {
			dcb_cfg->bw_pct[HINIC_DCB_CFG_TX][i] =
				tmp_dcb_cfg->bw_pct[HINIC_DCB_CFG_TX][i];
			changes |= DCB_CFG_CHG_PG_TX;
		}

		if (dcb_cfg->bw_pct[HINIC_DCB_CFG_RX][i] !=
			tmp_dcb_cfg->bw_pct[HINIC_DCB_CFG_RX][i]) {
			dcb_cfg->bw_pct[HINIC_DCB_CFG_RX][i] =
				tmp_dcb_cfg->bw_pct[HINIC_DCB_CFG_RX][i];
			changes |= DCB_CFG_CHG_PG_RX;
		}
	}

	for (i = 0; i < HINIC_DCB_UP_MAX; i++) {
		if (dcb_cfg->tc_cfg[i].pfc_en !=
			tmp_dcb_cfg->tc_cfg[i].pfc_en) {
			dcb_cfg->tc_cfg[i].pfc_en =
				tmp_dcb_cfg->tc_cfg[i].pfc_en;
			changes |= DCB_CFG_CHG_PFC;
		}
	}

	if (dcb_cfg->pfc_state != tmp_dcb_cfg->pfc_state) {
		dcb_cfg->pfc_state = tmp_dcb_cfg->pfc_state;
		changes |= DCB_CFG_CHG_PFC;
	}

	return changes;
}

static void hinic_dcb_get_pfc_map(struct hinic_nic_dev *nic_dev,
				  struct hinic_dcb_config *dcb_cfg, u8 *pfc_map)
{
	u8 i, up;
	u8 pfc_en = 0, outof_range_pfc = 0;

	for (i = 0; i < dcb_cfg->pfc_tcs; i++) {
		up = (HINIC_DCB_UP_MAX - 1) - i;
		if (dcb_cfg->tc_cfg[up].pfc_en)
			*pfc_map |= (u8)BIT(up);
	}

	for (i = 0; i < HINIC_DCB_UP_MAX; i++) {
		up = (HINIC_DCB_UP_MAX - 1) - i;
		if (dcb_cfg->tc_cfg[up].pfc_en)
			pfc_en |= (u8)BIT(up);
	}

	*pfc_map = pfc_en & nic_dev->up_valid_bitmap;
	outof_range_pfc = pfc_en & (~nic_dev->up_valid_bitmap);

	if (outof_range_pfc)
		hinic_info(nic_dev, drv,
			   "PFC setting out of range, 0x%x will be ignored\n",
			   outof_range_pfc);
}

static bool is_cos_in_use(u8 cos, u8 up_valid_bitmap, u8 *up_cos)
{
	u32 i;

	for (i = 0; i < HINIC_DCB_UP_MAX; i++) {
		if (!(up_valid_bitmap & BIT(i)))
			continue;

		if (cos == up_cos[i])
			return true;
	}

	return false;
}

static void hinic_dcb_adjust_up_bw(struct hinic_nic_dev *nic_dev, u8 *up_pgid,
				   u8 *up_bw)
{
	u8 tmp_cos, pg_id;
	u16 bw_all;
	u8 bw_remain, cos_cnt;

	for (pg_id = 0; pg_id < HINIC_DCB_PG_MAX; pg_id++) {
		bw_all = 0;
		cos_cnt = 0;
		/* Find all up mapping to the same pg */
		for (tmp_cos = 0; tmp_cos < HINIC_DCB_UP_MAX; tmp_cos++) {
			if (!is_cos_in_use(tmp_cos, nic_dev->up_valid_bitmap,
					   nic_dev->up_cos))
				continue;

			if (up_pgid[tmp_cos] == pg_id) {
				bw_all += up_bw[tmp_cos];
				cos_cnt++;
			}
		}

		if (bw_all <= 100 || !cos_cnt)
			continue;

		/* Calculate up percent of bandwidth group, The sum of
		 * percentages for priorities in the same priority group
		 * must be 100
		 */
		bw_remain = 100 % cos_cnt;
		for (tmp_cos = 0; tmp_cos < HINIC_DCB_UP_MAX; tmp_cos++) {
			if (!is_cos_in_use(tmp_cos, nic_dev->up_valid_bitmap,
					   nic_dev->up_cos))
				continue;

			if (up_pgid[tmp_cos] == pg_id) {
				up_bw[tmp_cos] =
					(u8)(100 * up_bw[tmp_cos] / bw_all +
					(u8)!!bw_remain);
				if (bw_remain)
					bw_remain--;
			}
		}
	}
}

static void hinic_dcb_dump_configuration(struct hinic_nic_dev *nic_dev,
					 u8 *up_tc, u8 *up_pgid, u8 *up_bw,
					 u8 *pg_bw, u8 *up_strict, u8 *bw_pct)
{
	u8 i;
	u8 cos;

	for (i = 0; i < HINIC_DCB_UP_MAX; i++) {
		if (!(nic_dev->up_valid_bitmap & BIT(i)))
			continue;

		cos = nic_dev->up_cos[i];
		hinic_info(nic_dev, drv,
			   "up: %d, cos: %d, tc: %d, pgid: %d, bw: %d, tsa: %d\n",
			   i, cos, up_tc[cos], up_pgid[cos], up_bw[cos],
			   up_strict[cos]);
	}

	for (i = 0; i < HINIC_DCB_PG_MAX; i++)
		hinic_info(nic_dev, drv, "pgid: %d, bw: %d\n", i, pg_bw[i]);
}

/* Ucode thread timeout is 210ms, must be lagger then 210ms */
#define HINIC_WAIT_PORT_IO_STOP		250

static int hinic_stop_port_traffic_flow(struct hinic_nic_dev *nic_dev)
{
	int err = 0;

	down(&nic_dev->dcb_sem);

	if (nic_dev->disable_port_cnt++ != 0)
		goto out;

	err = hinic_force_port_disable(nic_dev);
	if (err) {
		hinic_err(nic_dev, drv, "Failed to disable port\n");
		goto set_port_err;
	}

	err = hinic_set_port_funcs_state(nic_dev->hwdev, false);
	if (err) {
		hinic_err(nic_dev, drv,
			  "Failed to disable all functions in port\n");
		goto set_port_funcs_err;
	}

	hinic_info(nic_dev, drv, "Stop port traffic flow\n");

	goto out;

set_port_funcs_err:
	hinic_force_set_port_state(nic_dev, !!netif_running(nic_dev->netdev));

set_port_err:
out:
	if (err)
		nic_dev->disable_port_cnt--;

	up(&nic_dev->dcb_sem);

	return err;
}

static int hinic_start_port_traffic_flow(struct hinic_nic_dev *nic_dev)
{
	int err;

	down(&nic_dev->dcb_sem);

	nic_dev->disable_port_cnt--;
	if (nic_dev->disable_port_cnt > 0) {
		up(&nic_dev->dcb_sem);
		return 0;
	}

	nic_dev->disable_port_cnt = 0;
	up(&nic_dev->dcb_sem);

	err = hinic_force_set_port_state(nic_dev,
					 !!netif_running(nic_dev->netdev));
	if (err)
		hinic_err(nic_dev, drv, "Failed to disable port\n");

	err = hinic_set_port_funcs_state(nic_dev->hwdev, true);
	if (err)
		hinic_err(nic_dev, drv,
			  "Failed to disable all functions in port\n");

	hinic_info(nic_dev, drv, "Start port traffic flow\n");

	return err;
}

static int __set_hw_cos_up_map(struct hinic_nic_dev *nic_dev)
{
	u8 cos, cos_valid_bitmap, cos_up_map[HINIC_DCB_COS_MAX] = {0};
	u8 i;
	int err;

	cos_valid_bitmap = 0;
	for (i = 0; i < HINIC_DCB_UP_MAX; i++) {
		if (!(nic_dev->up_valid_bitmap & BIT(i)))
			continue;

		cos = nic_dev->up_cos[i];
		cos_up_map[cos] = i;
		cos_valid_bitmap |= (u8)BIT(cos);
	}

	err = hinic_dcb_set_cos_up_map(nic_dev->hwdev, cos_valid_bitmap,
				       cos_up_map);
	if (err) {
		hinic_info(nic_dev, drv, "Set cos_up map failed\n");
		return err;
	}

	return 0;
}

static int __set_hw_ets(struct hinic_nic_dev *nic_dev)
{
	struct hinic_dcb_config *dcb_cfg = &nic_dev->dcb_cfg;
	struct ieee_ets *my_ets = &nic_dev->hinic_ieee_ets;
	struct hinic_tc_attr *tc_attr;
	u8 up_tc[HINIC_DCB_UP_MAX] = {0};
	u8 up_pgid[HINIC_DCB_UP_MAX] = {0};
	u8 up_bw[HINIC_DCB_UP_MAX] = {0};
	u8 pg_bw[HINIC_DCB_UP_MAX] = {0};
	u8 up_strict[HINIC_DCB_UP_MAX] = {0};
	u8 i, tc, cos;
	int err;

	for (i = 0; i < HINIC_DCB_UP_MAX; i++) {
		if (!(nic_dev->up_valid_bitmap & BIT(i)))
			continue;

		cos = nic_dev->up_cos[i];
		if ((nic_dev->dcbx_cap & DCB_CAP_DCBX_VER_IEEE)) {
			up_tc[cos] = my_ets->prio_tc[i];
			up_pgid[cos] = my_ets->prio_tc[i];
			up_bw[cos] = 100;
			up_strict[i] =
				(my_ets->tc_tsa[cos] == IEEE8021Q_TSA_STRICT) ?
				HINIC_DCB_TSA_TC_SP : HINIC_DCB_TSA_TC_DWRR;

		} else {
			tc = hinic_dcb_get_tc(dcb_cfg, HINIC_DCB_CFG_TX, i);
			tc_attr = &dcb_cfg->tc_cfg[tc].path[HINIC_DCB_CFG_TX];
			up_tc[cos] = tc;
			up_pgid[cos] = tc_attr->pg_id;
			up_bw[cos] = tc_attr->bw_pct;
			up_strict[cos] = tc_attr->prio_type ?
				HINIC_DCB_TSA_TC_SP : HINIC_DCB_TSA_TC_DWRR;
		}
	}

	hinic_dcb_adjust_up_bw(nic_dev, up_pgid, up_bw);

	if (nic_dev->dcbx_cap & DCB_CAP_DCBX_VER_IEEE) {
		for (i = 0; i < HINIC_DCB_PG_MAX; i++)
			pg_bw[i] = my_ets->tc_tx_bw[i];
	} else {
		for (i = 0; i < HINIC_DCB_PG_MAX; i++)
			pg_bw[i] = dcb_cfg->bw_pct[HINIC_DCB_CFG_TX][i];
	}

	if (test_bit(HINIC_DCB_ENABLE, &nic_dev->flags))
		hinic_dcb_dump_configuration(nic_dev, up_tc, up_pgid,
					     up_bw, pg_bw, up_strict,
					     pg_bw);

	err = hinic_dcb_set_ets(nic_dev->hwdev, up_tc, pg_bw, up_pgid,
				up_bw, up_strict);
	if (err) {
		hinic_err(nic_dev, drv, "Failed to set ets with mode:%d\n",
			  nic_dev->dcbx_cap);
		return err;
	}

	hinic_info(nic_dev, drv, "Set ets to hw done with mode:%d\n",
		   nic_dev->dcbx_cap);

	return 0;
}

u8 hinic_dcbnl_set_ets_tool(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	u8 state = DCB_HW_CFG_CHG;
	int err;

	nic_dev->dcb_changes |= hinic_sync_dcb_cfg(nic_dev);
	if (!nic_dev->dcb_changes)
		return DCB_HW_CFG_CHG;

	err = hinic_stop_port_traffic_flow(nic_dev);
	if (err)
		return DCB_HW_CFG_ERR;
	/* wait all traffic flow stopped */
	if (netdev->reg_state == NETREG_REGISTERED)
		msleep(HINIC_WAIT_PORT_IO_STOP);

	if (nic_dev->dcb_changes & DCB_CFG_CHG_UP_COS) {
		err = __set_hw_cos_up_map(nic_dev);
		if (err) {
			hinic_info(nic_dev, drv,
				   "Set cos_up map to hardware failed\n");
			state = DCB_HW_CFG_ERR;
			goto out;
		}

		nic_dev->dcb_changes &= (~DCB_CFG_CHG_UP_COS);
	}

	if (nic_dev->dcb_changes & (DCB_CFG_CHG_PG_TX | DCB_CFG_CHG_PG_RX)) {
		err = __set_hw_ets(nic_dev);
		if (err) {
			state = DCB_HW_CFG_ERR;
			goto out;
		}

		nic_dev->dcb_changes &=
				(~(DCB_CFG_CHG_PG_TX | DCB_CFG_CHG_PG_RX));
	}

out:
	hinic_start_port_traffic_flow(nic_dev);

	return state;
}

static int hinic_dcbnl_set_df_ieee_cfg(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct ieee_ets *ets_default = &nic_dev->hinic_ieee_ets_default;
	struct ieee_pfc *my_pfc = &nic_dev->hinic_ieee_pfc;
	struct ieee_ets *my_ets = &nic_dev->hinic_ieee_ets;
	struct ieee_pfc pfc = {0};
	int err1 = 0;
	int err2 = 0;
	u8 flag = 0;

	if (!(nic_dev->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return 0;

	if (memcmp(my_ets, ets_default, sizeof(struct ieee_ets)))
		flag |= (u8)BIT(0);

	if (my_pfc->pfc_en)
		flag |= (u8)BIT(1);
	if (!flag)
		return 0;

	err1 = hinic_stop_port_traffic_flow(nic_dev);
	if (err1)
		return err1;
	if (netdev->reg_state == NETREG_REGISTERED)
		msleep(HINIC_WAIT_PORT_IO_STOP);

	if (flag & BIT(0)) {
		memcpy(my_ets, ets_default, sizeof(struct ieee_ets));
		err1 = __set_hw_ets(nic_dev);
	}
	if (flag & BIT(1)) {
		my_pfc->pfc_en = 0;
		err2 = hinic_dcb_set_pfc(nic_dev->hwdev, false, pfc.pfc_en);
		if (err2)
			nicif_err(nic_dev, drv, netdev, "Failed to set pfc\n");
	}

	hinic_start_port_traffic_flow(nic_dev);

	return (err1 || err2) ? -EINVAL : 0;
}

u8 hinic_dcbnl_set_pfc_tool(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_dcb_config *dcb_cfg = &nic_dev->dcb_cfg;
	u8 state = DCB_HW_CFG_CHG;
	int err;

	nic_dev->dcb_changes |= hinic_sync_dcb_cfg(nic_dev);
	if (!nic_dev->dcb_changes)
		return DCB_HW_CFG_CHG;

	if (nic_dev->dcb_changes & DCB_CFG_CHG_PFC) {
		u8 pfc_map = 0;

		hinic_dcb_get_pfc_map(nic_dev, dcb_cfg, &pfc_map);
		err = hinic_dcb_set_pfc(nic_dev->hwdev, dcb_cfg->pfc_state,
					pfc_map);
		if (err) {
			hinic_info(nic_dev, drv, "Failed to %s PFC\n",
				   dcb_cfg->pfc_state ? "enable" : "disable");
			state = DCB_HW_CFG_ERR;
			goto out;
		}

		if (dcb_cfg->pfc_state)
			hinic_info(nic_dev, drv, "Set PFC: 0x%x to hw done\n",
				   pfc_map);
		else
			hinic_info(nic_dev, drv, "Disable PFC, enable tx/rx pause\n");

		nic_dev->dcb_changes &= (~DCB_CFG_CHG_PFC);
	}
out:

	return state;
}

u8 hinic_dcbnl_set_all(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_dcb_config *dcb_cfg = &nic_dev->dcb_cfg;
	u8 state = DCB_HW_CFG_CHG;
	int err;

	if (!(nic_dev->dcbx_cap & DCB_CAP_DCBX_VER_CEE))
		return DCB_HW_CFG_ERR;

	nic_dev->dcb_changes |= hinic_sync_dcb_cfg(nic_dev);
	if (!nic_dev->dcb_changes)
		return DCB_HW_CFG_NO_CHG;

	err = hinic_stop_port_traffic_flow(nic_dev);
	if (err)
		return DCB_HW_CFG_ERR;
	/* wait all traffic flow stopped */
	if (netdev->reg_state == NETREG_REGISTERED)
		msleep(HINIC_WAIT_PORT_IO_STOP);

	if (nic_dev->dcb_changes & DCB_CFG_CHG_UP_COS) {
		err = __set_hw_cos_up_map(nic_dev);
		if (err) {
			hinic_info(nic_dev, drv,
				   "Set cos_up map to hardware failed\n");
			state = DCB_HW_CFG_ERR;
			goto out;
		}

		nic_dev->dcb_changes &= (~DCB_CFG_CHG_UP_COS);
	}

	if (nic_dev->dcb_changes & (DCB_CFG_CHG_PG_TX | DCB_CFG_CHG_PG_RX)) {
		err = __set_hw_ets(nic_dev);
		if (err) {
			state = DCB_HW_CFG_ERR;
			goto out;
		}

		nic_dev->dcb_changes &=
				(~(DCB_CFG_CHG_PG_TX | DCB_CFG_CHG_PG_RX));
	}

	if (nic_dev->dcb_changes & DCB_CFG_CHG_PFC) {
		u8 pfc_map = 0;

		hinic_dcb_get_pfc_map(nic_dev, dcb_cfg, &pfc_map);
		err = hinic_dcb_set_pfc(nic_dev->hwdev, dcb_cfg->pfc_state,
					pfc_map);
		if (err) {
			hinic_info(nic_dev, drv, "Failed to %s PFC\n",
				   dcb_cfg->pfc_state ? "enable" : "disable");
			state = DCB_HW_CFG_ERR;
			goto out;
		}

		if (dcb_cfg->pfc_state)
			hinic_info(nic_dev, drv, "Set PFC: 0x%x to hw done\n",
				   pfc_map);
		else
			hinic_info(nic_dev, drv, "Disable PFC, enable tx/rx pause\n");

		nic_dev->dcb_changes &= (~DCB_CFG_CHG_PFC);
	}

out:
	hinic_start_port_traffic_flow(nic_dev);

	return state;
}

static int hinic_dcbnl_ieee_get_ets(struct net_device *netdev,
				    struct ieee_ets *ets)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct ieee_ets *my_ets = &nic_dev->hinic_ieee_ets;

	ets->ets_cap = my_ets->ets_cap;
	memcpy(ets->tc_tx_bw, my_ets->tc_tx_bw, sizeof(ets->tc_tx_bw));
	memcpy(ets->tc_rx_bw, my_ets->tc_rx_bw, sizeof(ets->tc_rx_bw));
	memcpy(ets->prio_tc, my_ets->prio_tc, sizeof(ets->prio_tc));
	memcpy(ets->tc_tsa, my_ets->tc_tsa, sizeof(ets->tc_tsa));

	return 0;
}

static int hinic_dcbnl_ieee_set_ets(struct net_device *netdev,
				    struct ieee_ets *ets)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_dcb_config *dcb_cfg = &nic_dev->dcb_cfg;
	struct ieee_ets *my_ets = &nic_dev->hinic_ieee_ets;
	struct ieee_ets back_ets;
	int err, i;
	u8 max_tc = 0;
	u16 total_bw = 0;

	if (!(nic_dev->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	if (!memcmp(ets->tc_tx_bw, my_ets->tc_tx_bw, sizeof(ets->tc_tx_bw)) &&
	    !memcmp(ets->tc_rx_bw, my_ets->tc_rx_bw, sizeof(ets->tc_rx_bw)) &&
	    !memcmp(ets->prio_tc, my_ets->prio_tc, sizeof(ets->prio_tc)) &&
	    !memcmp(ets->tc_tsa, my_ets->tc_tsa, sizeof(ets->tc_tsa)))
		return 0;

	for (i = 0; i < HINIC_DCB_TC_MAX; i++)
		total_bw += ets->tc_tx_bw[i];
	if (!total_bw)
		return -EINVAL;

	for (i = 0; i < dcb_cfg->pg_tcs; i++) {
		if (ets->prio_tc[i] > max_tc)
			max_tc = ets->prio_tc[i];
	}
	if (max_tc)
		max_tc++;

	if (max_tc > dcb_cfg->pg_tcs)
		return -EINVAL;

	max_tc = max_tc ? dcb_cfg->pg_tcs : 0;
	memcpy(&back_ets, my_ets, sizeof(struct ieee_ets));
	memcpy(my_ets->tc_tx_bw, ets->tc_tx_bw, sizeof(ets->tc_tx_bw));
	memcpy(my_ets->tc_rx_bw, ets->tc_rx_bw, sizeof(ets->tc_rx_bw));
	memcpy(my_ets->prio_tc, ets->prio_tc, sizeof(ets->prio_tc));
	memcpy(my_ets->tc_tsa, ets->tc_tsa, sizeof(ets->tc_tsa));

	if (max_tc != netdev_get_num_tc(netdev)) {
		err = hinic_setup_tc(netdev, max_tc);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to setup tc with max_tc:%d, err:%d\n",
				  max_tc, err);
			memcpy(my_ets, &back_ets, sizeof(struct ieee_ets));
			return err;
		}
	}

	err = hinic_stop_port_traffic_flow(nic_dev);
	if (err)
		return err;
	if (netdev->reg_state == NETREG_REGISTERED)
		msleep(HINIC_WAIT_PORT_IO_STOP);

	err = __set_hw_ets(nic_dev);

	hinic_start_port_traffic_flow(nic_dev);

	return err;
}

static int hinic_dcbnl_ieee_get_pfc(struct net_device *netdev,
				    struct ieee_pfc *pfc)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct ieee_pfc *my_pfc = &nic_dev->hinic_ieee_pfc;

	pfc->pfc_en = my_pfc->pfc_en;
	pfc->pfc_cap = my_pfc->pfc_cap;

	return 0;
}

static int hinic_dcbnl_ieee_set_pfc(struct net_device *netdev,
				    struct ieee_pfc *pfc)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_dcb_config *dcb_cfg = &nic_dev->dcb_cfg;
	struct ieee_pfc *my_pfc = &nic_dev->hinic_ieee_pfc;
	struct ieee_ets *my_ets = &nic_dev->hinic_ieee_ets;
	int err, i;
	u8 pfc_map, max_tc;
	u8 outof_range_pfc = 0;
	bool pfc_en;

	if (!(nic_dev->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	if (my_pfc->pfc_en == pfc->pfc_en)
		return 0;

	pfc_map = pfc->pfc_en & nic_dev->up_valid_bitmap;
	outof_range_pfc = pfc->pfc_en & (~nic_dev->up_valid_bitmap);
	if (outof_range_pfc)
		netif_info(nic_dev, drv, netdev,
			   "pfc setting out of range, 0x%x will be ignored\n",
			   outof_range_pfc);

	err = hinic_stop_port_traffic_flow(nic_dev);
	if (err)
		return err;
	if (netdev->reg_state == NETREG_REGISTERED)
		msleep(HINIC_WAIT_PORT_IO_STOP);

	pfc_en = pfc_map ? true : false;
	max_tc = 0;
	for (i = 0; i < dcb_cfg->pg_tcs; i++) {
		if (my_ets->prio_tc[i] > max_tc)
			max_tc = my_ets->prio_tc[i];
	}
	pfc_en = max_tc ? pfc_en : false;

	err = hinic_dcb_set_pfc(nic_dev->hwdev, pfc_en, pfc_map);
	if (err) {
		hinic_info(nic_dev, drv,
			   "Failed to set pfc to hw with pfc_map:0x%x err:%d\n",
			   pfc_map, err);
		hinic_start_port_traffic_flow(nic_dev);
		return err;
	}

	hinic_start_port_traffic_flow(nic_dev);
	my_pfc->pfc_en = pfc->pfc_en;
	hinic_info(nic_dev, drv,
		   "Set pfc successfully with pfc_map:0x%x, pfc_en:%d\n",
		   pfc_map, pfc_en);

	return 0;
}

#ifdef NUMTCS_RETURNS_U8
static u8 hinic_dcbnl_getnumtcs(struct net_device *netdev, int tcid, u8 *num)
#else
static int hinic_dcbnl_getnumtcs(struct net_device *netdev, int tcid, u8 *num)
#endif
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_dcb_config *dcb_cfg = &nic_dev->dcb_cfg;

	if (!test_bit(HINIC_DCB_ENABLE, &nic_dev->flags))
		return -EINVAL;

	switch (tcid) {
	case DCB_NUMTCS_ATTR_PG:
		*num = dcb_cfg->pg_tcs;
		break;
	case DCB_NUMTCS_ATTR_PFC:
		*num = dcb_cfg->pfc_tcs;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#ifdef NUMTCS_RETURNS_U8
static u8 hinic_dcbnl_setnumtcs(struct net_device *netdev, int tcid, u8 num)
#else
static int hinic_dcbnl_setnumtcs(struct net_device *netdev, int tcid, u8 num)
#endif
{
	return -EINVAL;
}

static u8 hinic_dcbnl_getpfcstate(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	return (u8)nic_dev->dcb_cfg.pfc_state;
}

static void hinic_dcbnl_setpfcstate(struct net_device *netdev, u8 state)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	nic_dev->tmp_dcb_cfg.pfc_state = !!state;
}

static u8 hinic_dcbnl_getdcbx(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	return nic_dev->dcbx_cap;
}

static u8 hinic_dcbnl_setdcbx(struct net_device *netdev, u8 mode)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	int err;

	if (((mode & DCB_CAP_DCBX_VER_IEEE) && (mode & DCB_CAP_DCBX_VER_CEE)) ||
	    ((mode & DCB_CAP_DCBX_LLD_MANAGED) &&
	    (!(mode & DCB_CAP_DCBX_HOST)))) {
		nicif_info(nic_dev, drv, netdev,
			   "Set dcbx failed with invalid mode:%d\n", mode);
		return 1;
	}

	if (nic_dev->dcbx_cap == mode)
		return 0;
	nic_dev->dcbx_cap = mode;

	if (mode & DCB_CAP_DCBX_VER_CEE) {
		u8 mask = DCB_CFG_CHG_PFC | DCB_CFG_CHG_PG_TX |
			DCB_CFG_CHG_PG_RX;
		nic_dev->dcb_changes |= mask;
		hinic_dcbnl_set_all(netdev);
	} else if (mode & DCB_CAP_DCBX_VER_IEEE) {
		if (netdev_get_num_tc(netdev)) {
			err = hinic_setup_tc(netdev, 0);
			if (err) {
				nicif_err(nic_dev, drv, netdev,
					  "Failed to setup tc with mode:%d\n",
					  mode);
				return 1;
			}
		}

		hinic_dcbnl_set_df_ieee_cfg(netdev);
		hinic_force_port_relink(nic_dev->hwdev);
	} else {
		err = hinic_setup_tc(netdev, 0);
		if (err) {
			nicif_err(nic_dev, drv, netdev,
				  "Failed to setup tc with mode:%d\n", mode);
			return 1;
		}
	}
	nicif_info(nic_dev, drv, netdev, "Change dcbx mode to 0x%x\n", mode);

	return 0;
}

const struct dcbnl_rtnl_ops hinic_dcbnl_ops = {
	/* IEEE 802.1Qaz std */
	.ieee_getets	= hinic_dcbnl_ieee_get_ets,
	.ieee_setets	= hinic_dcbnl_ieee_set_ets,
	.ieee_getpfc	= hinic_dcbnl_ieee_get_pfc,
	.ieee_setpfc	= hinic_dcbnl_ieee_set_pfc,

	/* CEE std */
	.getstate	= hinic_dcbnl_get_state,
	.setstate	= hinic_dcbnl_set_state,
	.getpermhwaddr	= hinic_dcbnl_get_perm_hw_addr,
	.setpgtccfgtx	= hinic_dcbnl_set_pg_tc_cfg_tx,
	.setpgbwgcfgtx	= hinic_dcbnl_set_pg_bwg_cfg_tx,
	.setpgtccfgrx	= hinic_dcbnl_set_pg_tc_cfg_rx,
	.setpgbwgcfgrx	= hinic_dcbnl_set_pg_bwg_cfg_rx,
	.getpgtccfgtx	= hinic_dcbnl_get_pg_tc_cfg_tx,
	.getpgbwgcfgtx	= hinic_dcbnl_get_pg_bwg_cfg_tx,
	.getpgtccfgrx	= hinic_dcbnl_get_pg_tc_cfg_rx,
	.getpgbwgcfgrx	= hinic_dcbnl_get_pg_bwg_cfg_rx,
	.setpfccfg	= hinic_dcbnl_set_pfc_cfg,
	.getpfccfg	= hinic_dcbnl_get_pfc_cfg,
	.setall		= hinic_dcbnl_set_all,
	.getcap		= hinic_dcbnl_getcap,
	.getnumtcs	= hinic_dcbnl_getnumtcs,
	.setnumtcs	= hinic_dcbnl_setnumtcs,
	.getpfcstate	= hinic_dcbnl_getpfcstate,
	.setpfcstate	= hinic_dcbnl_setpfcstate,

	/* DCBX configuration */
	.getdcbx	= hinic_dcbnl_getdcbx,
	.setdcbx	= hinic_dcbnl_setdcbx,
};

int hinic_dcb_reset_hw_config(struct hinic_nic_dev *nic_dev)
{
	struct net_device *netdev = nic_dev->netdev;
	u8 state;

	hinic_dcb_config_init(nic_dev, &nic_dev->tmp_dcb_cfg);
	state = hinic_dcbnl_set_all(netdev);
	if (state == DCB_HW_CFG_ERR)
		return -EFAULT;

	if (state == DCB_HW_CFG_CHG)
		hinic_info(nic_dev, drv,
			   "Reset hardware DCB configuration done\n");

	return 0;
}

void hinic_configure_dcb(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);
	int err;

	if (test_bit(HINIC_DCB_ENABLE, &nic_dev->flags)) {
		memcpy(&nic_dev->tmp_dcb_cfg, &nic_dev->save_dcb_cfg,
		       sizeof(nic_dev->tmp_dcb_cfg));
		hinic_dcbnl_set_all(netdev);
	} else {
		memcpy(&nic_dev->save_dcb_cfg, &nic_dev->tmp_dcb_cfg,
		       sizeof(nic_dev->save_dcb_cfg));
		err = hinic_dcb_reset_hw_config(nic_dev);
		if (err)
			nicif_warn(nic_dev, drv, netdev,
				   "Failed to reset hw dcb configuration\n");
	}
}

static bool __is_cos_up_map_change(struct hinic_nic_dev *nic_dev, u8 *cos_up)
{
	u8 cos, up;

	for (cos = 0; cos < nic_dev->max_cos; cos++) {
		up = cos_up[cos];
		if (BIT(up) != (nic_dev->up_valid_bitmap & BIT(up)))
			return true;
	}

	return false;
}

int __set_cos_up_map(struct hinic_nic_dev *nic_dev, u8 *cos_up)
{
	struct net_device *netdev;
	u8 state;
	int err = 0;

	if (!nic_dev || !cos_up)
		return -EINVAL;

	netdev = nic_dev->netdev;

	if (test_and_set_bit(HINIC_DCB_UP_COS_SETTING, &nic_dev->dcb_flags)) {
		nicif_err(nic_dev, drv, netdev,
			  "Cos_up map setting in inprocess, please try again later\n");
		return -EFAULT;
	}

	nicif_info(nic_dev, drv, netdev, "Set cos2up:%d%d%d%d%d%d%d%d\n",
		   cos_up[0], cos_up[1], cos_up[2], cos_up[3],
		   cos_up[4], cos_up[5], cos_up[6], cos_up[7]);

	if (!__is_cos_up_map_change(nic_dev, cos_up)) {
		nicif_err(nic_dev, drv, netdev,
			  "Same mapping, don't need to change anything\n");
		err = 0;
		goto out;
	}

	err = hinic_set_up_cos_map(nic_dev, nic_dev->max_cos, cos_up);
	if (err) {
		err = -EFAULT;
		goto out;
	}

	nic_dev->dcb_changes = DCB_CFG_CHG_PG_TX | DCB_CFG_CHG_PG_RX |
			       DCB_CFG_CHG_PFC | DCB_CFG_CHG_UP_COS;

	if (test_bit(HINIC_DCB_ENABLE, &nic_dev->flags)) {
		/* Change map in kernel */
		hinic_set_prio_tc_map(nic_dev);

		state = hinic_dcbnl_set_all(netdev);
		if (state == DCB_HW_CFG_ERR) {
			nicif_err(nic_dev, drv, netdev,
				  "Reconfig dcb to hw failed\n");
			err = -EFAULT;
		}
	}

out:
	clear_bit(HINIC_DCB_UP_COS_SETTING, &nic_dev->dcb_flags);

	return err;
}

int hinic_get_num_cos(struct hinic_nic_dev *nic_dev, u8 *num_cos)
{
	if (!nic_dev || !num_cos)
		return -EINVAL;

	*num_cos = nic_dev->max_cos;

	return 0;
}

int hinic_get_cos_up_map(struct hinic_nic_dev *nic_dev, u8 *num_cos,
			 u8 *cos_up)
{
	u8 up, cos;

	if (!nic_dev || !cos_up)
		return -EINVAL;

	for (cos = 0; cos < HINIC_DCB_COS_MAX; cos++) {
		for (up = 0; up < HINIC_DCB_UP_MAX; up++) {
			if (!(nic_dev->up_valid_bitmap & BIT(up)))
				continue;

			if (nic_dev->up_cos[up] == cos ||
			    nic_dev->up_cos[up] == nic_dev->default_cos_id)
				cos_up[cos] = up;
		}
	}

	*num_cos = nic_dev->max_cos;

	return 0;
}

static int __stop_port_flow(void *uld_array[], u32 num_dev)
{
	struct hinic_nic_dev *tmp_dev;
	u32 i, idx;
	int err;

	for (idx = 0; idx < num_dev; idx++) {
		tmp_dev = (struct hinic_nic_dev *)uld_array[idx];
		err = hinic_stop_port_traffic_flow(tmp_dev);
		if (err) {
			nicif_err(tmp_dev, drv, tmp_dev->netdev,
				  "Stop port traffic flow failed\n");
			goto stop_port_err;
		}
	}

	/* wait all traffic flow stopped */
	msleep(HINIC_WAIT_PORT_IO_STOP);

	return 0;

stop_port_err:
	for (i = 0; i < idx; i++) {
		tmp_dev = (struct hinic_nic_dev *)uld_array[i];
		hinic_start_port_traffic_flow(tmp_dev);
	}

	return err;
}

static void __start_port_flow(void *uld_array[], u32 num_dev)
{
	struct hinic_nic_dev *tmp_dev;
	u32 idx;

	for (idx = 0; idx < num_dev; idx++) {
		tmp_dev = (struct hinic_nic_dev *)uld_array[idx];
		hinic_start_port_traffic_flow(tmp_dev);
	}
}

/* for hinicadm tool, need to chang all port of the chip */
int hinic_set_cos_up_map(struct hinic_nic_dev *nic_dev, u8 *cos_up)
{
	void *uld_array[HINIC_MAX_PF_NUM];
	struct hinic_nic_dev *tmp_dev;
	u8 num_cos, old_cos_up[HINIC_DCB_COS_MAX] = {0};
	u32 i, idx, num_dev = 0;
	int err, rollback_err;

	/* Save old map, in case of set failed */
	err = hinic_get_cos_up_map(nic_dev, &num_cos, old_cos_up);
	if (err || !num_cos) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Get old cos_up map failed\n");
		return -EFAULT;
	}

	if (!memcmp(cos_up, old_cos_up, sizeof(u8) * num_cos)) {
		nicif_info(nic_dev, drv, nic_dev->netdev,
			   "Same cos2up map, don't need to change anything\n");
		return 0;
	}

	/* Get all pf of this chip */
	err = hinic_get_pf_uld_array(nic_dev->pdev, &num_dev, uld_array);
	if (err) {
		nicif_err(nic_dev, drv, nic_dev->netdev,
			  "Get all pf private handle failed\n");
		return -EFAULT;
	}

	err = __stop_port_flow(uld_array, num_dev);
	if (err)
		return -EFAULT;

	for (idx = 0; idx < num_dev; idx++) {
		tmp_dev = (struct hinic_nic_dev *)uld_array[idx];
		err = __set_cos_up_map(tmp_dev, cos_up);
		if (err) {
			nicif_err(tmp_dev, drv, tmp_dev->netdev,
				  "Set cos_up map to hw failed\n");
			goto set_err;
		}
	}

	__start_port_flow(uld_array, num_dev);

	hinic_set_chip_cos_up_map(nic_dev->pdev, cos_up);

	return 0;

set_err:
	/* undo all settings */
	for (i = 0; i < idx; i++) {
		tmp_dev = (struct hinic_nic_dev *)uld_array[i];
		rollback_err = __set_cos_up_map(tmp_dev, old_cos_up);
		if (rollback_err)
			nicif_err(tmp_dev, drv, tmp_dev->netdev,
				  "Undo cos_up map to hw failed\n");
	}

	__start_port_flow(uld_array, num_dev);

	return err;
}
