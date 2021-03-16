/* SPDX-License-Identifier: GPL-2.0-only */
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

#ifndef HINIC_MGMT_INTERFACE_H
#define HINIC_MGMT_INTERFACE_H

#include <linux/etherdevice.h>
#include <linux/types.h>

#include "hinic_port_cmd.h"

/* up to driver event */
#define	HINIC_PORT_CMD_MGMT_RESET	0x0

struct hinic_msg_head {
	u8	status;
	u8	version;
	u8	resp_aeq_num;
	u8	rsvd0[5];
};

struct hinic_register_vf {
	u8	status;
	u8	version;
	u8	rsvd0[6];
};

struct hinic_tx_rate_cfg {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u32	tx_rate;
};

struct hinic_tx_rate_cfg_max_min {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u32	min_rate;
	u32	max_rate;
	u8	rsvd2[8];
};

struct hinic_port_mac_get {
	u16		func_id;
	u8		mac[ETH_ALEN];
	int		ret;
};

struct hinic_function_table {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rx_wqe_buf_size;
	u32	mtu;
};

struct hinic_cmd_qpn {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	base_qpn;
};

struct hinic_port_mac_set {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	vlan_id;
	u16	rsvd1;
	u8	mac[ETH_ALEN];
};

struct hinic_port_mac_update {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	vlan_id;
	u16	rsvd1;
	u8	old_mac[ETH_ALEN];
	u16	rsvd2;
	u8	new_mac[ETH_ALEN];
};

struct hinic_vport_state {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u8	state;
	u8	rsvd2[3];
};

struct hinic_port_state {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	state;
	u8	rsvd1;
	u16	func_id;
};

struct hinic_spoofchk_set {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	state;
	u8	rsvd1;
	u16	func_id;
};

struct hinic_mtu {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u32	mtu;
};

struct hinic_vlan_config {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	vlan_id;
};

struct hinic_speed_cmd {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	speed;
};

struct hinic_link_mode_cmd {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u16	supported;	/* 0xFFFF represent Invalid value */
	u16	advertised;
};

struct hinic_set_autoneg_cmd {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	enable;	/* 1: enable , 0: disable */
};

struct hinic_set_link_status {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	enable;
};

struct hinic_get_link {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	link_status;
	u8	rsvd1;
};

struct hinic_link_status_report {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	link_status;
	u8	port_id;
};

struct hinic_port_info {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u8	port_type;
	u8	autoneg_cap;
	u8	autoneg_state;
	u8	duplex;
	u8	speed;
	u8	resv2[3];
};

struct hinic_tso_config {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u8	tso_en;
	u8	resv2[3];
};

struct hinic_lro_config {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u8	lro_ipv4_en;
	u8	lro_ipv6_en;
	u8	lro_max_wqe_num;
	u8	resv2[13];
};

struct hinic_lro_timer {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	type;   /* 0: set timer value, 1: get timer value */
	u8	enable; /* when set lro time, enable should be 1 */
	u16	rsvd1;
	u32	timer;
};

struct hinic_checksum_offload {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u32	rx_csum_offload;
};

struct hinic_vlan_offload {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	vlan_rx_offload;
	u8	rsvd1[5];
};

struct hinic_vlan_filter {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	rsvd1[2];
	u32	vlan_filter_ctrl;
};

struct hinic_pause_config {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u32	auto_neg;
	u32	rx_pause;
	u32	tx_pause;
};

struct hinic_rx_mode_config {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u32	rx_mode;
};

/* rss */
struct nic_rss_indirect_tbl {
	u32 group_index;
	u32 offset;
	u32 size;
	u32 rsvd;
	u8 entry[HINIC_RSS_INDIR_SIZE];
};

struct nic_rss_context_tbl {
	u32 group_index;
	u32 offset;
	u32 size;
	u32 rsvd;
	u32 ctx;
};

struct hinic_rss_config {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	rss_en;
	u8	template_id;
	u8	rq_priority_number;
	u8	rsvd1[3];
	u8	prio_tc[HINIC_DCB_UP_MAX];
};

struct hinic_rss_template_mgmt {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	cmd;
	u8	template_id;
	u8	rsvd1[4];
};

struct hinic_rss_indir_table {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	template_id;
	u8	rsvd1;
	u8	indir[HINIC_RSS_INDIR_SIZE];
};

struct hinic_rss_template_key {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	template_id;
	u8	rsvd1;
	u8	key[HINIC_RSS_KEY_SIZE];
};

struct hinic_rss_engine_type {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	template_id;
	u8	hash_engine;
	u8	rsvd1[4];
};

struct hinic_rss_context_table {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	template_id;
	u8	rsvd1;
	u32	context;
};

struct hinic_up_ets_cfg {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8 port_id;
	u8 rsvd1[3];
	u8 up_tc[HINIC_DCB_UP_MAX];
	u8 pg_bw[HINIC_DCB_PG_MAX];
	u8 pgid[HINIC_DCB_UP_MAX];
	u8 up_bw[HINIC_DCB_UP_MAX];
	u8 prio[HINIC_DCB_PG_MAX];
};

struct hinic_set_pfc {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	pfc_en;
	u8	pfc_bitmap;
	u8	rsvd1[4];
};

struct hinic_set_micro_pfc {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	micro_pfc_en;
	u8	rsvd1;
	u8	cfg_rq_max;
	u8	cfg_rq_depth;
	u16	rq_sm_thd;
};

struct hinic_cos_up_map {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	port_id;
	/* every bit indicate index of map is valid or not */
	u8	cos_valid_mask;
	u16	rsvd1;

	/* user priority in cos(index:cos, value: up) */
	u8	map[HINIC_DCB_COS_MAX];
};

struct hinic_set_rq_iq_mapping {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u8	map[HINIC_MAX_NUM_RQ];
	u32	num_rqs;
	u32	rq_depth;
};

#define HINIC_PFC_SET_FUNC_THD	0
#define HINIC_PFC_SET_GLB_THD	1
struct hinic_pfc_thd {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	op_type;
	u16	func_thd;
	u16	glb_thd;
};

/* set iq enable to ucode */
struct hinic_cmd_enable_iq {
	u8	rq_depth;
	u8	num_rq;
	u16	glb_rq_id;

	u16	q_id;
	u16	lower_thd;

	u16	force_en;	/* 1: force unlock, 0: depend on condition */
	u16	prod_idx;
};

/* set iq enable to mgmt cpu */
struct hinic_cmd_enable_iq_mgmt {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	rq_depth;
	u8	num_rq;
	u16	glb_rq_id;

	u16	q_id;
	u16	lower_thd;

	u16	force_en;	/* 1: force unlock, 0: depend on condition */
	u16	prod_idx;
};

struct hinic_port_link_status {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	link;
	u8	port_id;
};

struct hinic_cable_plug_event {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	plugged;	/* 0: unplugged, 1: plugged */
	u8	port_id;
};

struct hinic_link_err_event {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	err_type;
	u8	port_id;
};

struct hinic_sync_time_info {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u64	mstime;
};

#define HINIC_PORT_STATS_VERSION	0

struct hinic_port_stats_info {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u16 func_id;
	u16 rsvd1;
	u32 stats_version;
	u32 stats_size;
};

struct hinic_port_stats {
	u8 status;
	u8 version;
	u8 rsvd[6];

	struct hinic_phy_port_stats stats;
};

struct hinic_cmd_vport_stats {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	struct hinic_vport_stats stats;
};

struct hinic_vf_vlan_config {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u16 func_id;
	u16 vlan_id;
	u8  qos;
	u8  rsvd1[7];
};

struct hinic_port_ipsu_mac {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	index;
	u16	func_id;
	u16	vlan_id;
	u8	mac[ETH_ALEN];
};

/* get or set loopback mode, need to modify by base API */
#define HINIC_INTERNAL_LP_MODE 5
#define LOOP_MODE_MIN 1
#define LOOP_MODE_MAX 6

struct hinic_port_loopback {
	u8 status;
	u8 version;
	u8 rsvd[6];

	u32 mode;
	u32 en;
};

#define HINIC_COMPILE_TIME_LEN	20
struct hinic_version_info {
	u8 status;
	u8 version;
	u8 rsvd[6];

	u8 ver[HINIC_FW_VERSION_NAME];
	u8 time[HINIC_COMPILE_TIME_LEN];
};

#define ANTI_ATTACK_DEFAULT_CIR 500000
#define ANTI_ATTACK_DEFAULT_XIR 600000
#define ANTI_ATTACK_DEFAULT_CBS 10000000
#define ANTI_ATTACK_DEFAULT_XBS 12000000
/* set physical port Anti-Attack rate */
struct hinic_port_anti_attack_rate {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	enable; /* 1: enable rate-limiting, 0: disable rate-limiting */
	u32	cir;	/* Committed Information Rate */
	u32	xir;	/* eXtended Information Rate */
	u32	cbs;	/* Committed Burst Size */
	u32	xbs;	/* eXtended Burst Size */
};

struct hinic_clear_sq_resource {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
};

struct hinic_l2nic_reset {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	reset_flag;
};

struct hinic_super_cqe {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u16 func_id;
	u16 super_cqe_en;
};

struct hinic_capture_info {
	u8 status;
	u8 version;
	u8 rsvd[6];

	u32 op_type;
	u32 func_id;
	u32 is_en_trx;
	u32 offset_cos;
	u32 data_vlan;
};

struct hinic_port_rt_cmd {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	pf_id;
	u8	enable;
	u8	rsvd1[6];
};

struct fw_support_func {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u64	flag;
	u64	rsvd;
};

struct hinic_vf_dcb_state {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	struct hinic_dcb_state state;
};

struct hinic_port_funcs_state {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;	/* pf_id */
	u8	drop_en;
	u8	rsvd1;
};

struct hinic_reset_link_cfg {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
};

struct hinic_force_pkt_drop {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u8	port;
	u8	rsvd1[3];
};

struct hinic_set_link_follow {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;
	u8	follow_status;
	u8	rsvd2[3];
};

int hinic_init_function_table(void *hwdev, u16 rx_buf_sz);

int hinic_get_base_qpn(void *hwdev, u16 *global_qpn);

int hinic_get_fw_support_func(void *hwdev);

int hinic_vf_func_init(struct hinic_hwdev *hwdev);

void hinic_vf_func_free(struct hinic_hwdev *hwdev);

void hinic_unregister_vf_msg_handler(void *hwdev, u16 vf_id);

int hinic_set_port_routine_cmd_report(void *hwdev, bool enable);

int hinic_refresh_nic_cfg(void *hwdev, struct nic_port_info *port_info);

int hinic_save_dcb_state(struct hinic_hwdev *hwdev,
			 struct hinic_dcb_state *dcb_state);

void hinic_clear_vf_infos(void *hwdev, u16 vf_id);

/* OVS module interface, for BMGW cpath command */
enum hinic_hiovs_cmd {
	OVS_SET_CPATH_VLAN = 39,
	OVS_GET_CPATH_VLAN = 40,
	OVS_DEL_CPATH_VLAN = 43,
};

struct cmd_cpath_vlan {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	vlan_id;
	u16	pf_id;
};

/* HILINK module interface */

/* cmd of mgmt CPU message for HILINK module */
enum hinic_hilink_cmd {
	HINIC_HILINK_CMD_GET_LINK_INFO		= 0x3,
	HINIC_HILINK_CMD_SET_LINK_SETTINGS	= 0x8,
};

enum hilink_info_print_event {
	HILINK_EVENT_LINK_UP = 1,
	HILINK_EVENT_LINK_DOWN,
	HILINK_EVENT_CABLE_PLUGGED,
	HILINK_EVENT_MAX_TYPE,
};

enum hinic_link_port_type {
	LINK_PORT_FIBRE	= 1,
	LINK_PORT_ELECTRIC,
	LINK_PORT_COPPER,
	LINK_PORT_AOC,
	LINK_PORT_BACKPLANE,
	LINK_PORT_BASET,
	LINK_PORT_MAX_TYPE,
};

enum hilink_fibre_subtype {
	FIBRE_SUBTYPE_SR = 1,
	FIBRE_SUBTYPE_LR,
	FIBRE_SUBTYPE_MAX,
};

enum hilink_fec_type {
	HILINK_FEC_RSFEC,
	HILINK_FEC_BASEFEC,
	HILINK_FEC_NOFEC,
	HILINK_FEC_MAX_TYPE,
};

struct hi30_ffe_data {
	u8 PRE2;
	u8 PRE1;
	u8 POST1;
	u8 POST2;
	u8 MAIN;
};

struct hi30_ctle_data {
	u8 ctlebst[3];
	u8 ctlecmband[3];
	u8 ctlermband[3];
	u8 ctleza[3];
	u8 ctlesqh[3];
	u8 ctleactgn[3];
	u8 ctlepassgn;
};

struct hi30_dfe_data {
	u8 fix_tap1_cen;
	u8 fix_tap1_edg;
	u8 dfefxtap[6];
	u8 dfefloattap[6];
};

struct hilink_sfp_power {
	u32 rx_power;
	u32 tx_power;
	u32 rsvd;
	u32 is_invalid;
};

#define HILINK_MAX_LANE		4

struct hilink_lane {
	u8	lane_used;
	u8	hi30_ffe[5];
	u8	hi30_ctle[19];
	u8	hi30_dfe[14];
	u8	rsvd4;
};

struct hinic_link_info {
	u8	vendor_name[16];
	/* port type:
	 * 1 - fiber; 2 - electric; 3 - copper; 4 - AOC; 5 - backplane;
	 * 6 - baseT; 0xffff - unknown
	 *
	 * port subtype:
	 * Only when port_type is fiber:
	 * 1 - SR; 2 - LR
	 */
	u32	port_type;
	u32	port_sub_type;
	u32	cable_length;
	u8	cable_temp;
	u8	cable_max_speed;/* 1(G)/10(G)/25(G)... */
	u8	sfp_type;	/* 0 - qsfp; 1 - sfp */
	u8	rsvd0;
	u32	power[4];	/* uW; if is sfp, only power[2] is valid */

	u8	an_state;	/* 0 - off; 1 - on */
	u8	fec;		/* 0 - RSFEC; 1 - BASEFEC; 2 - NOFEC */
	u16	speed;		/* 1(G)/10(G)/25(G)... */

	u8	cable_absent;	/* 0 - cable present; 1 - cable unpresent */
	u8	alos;		/* 0 - yes; 1 - no */
	u8	rx_los;		/* 0 - yes; 1 - no */
	u8	pma_status;
	u32	pma_dbg_info_reg;	/* pma debug info:  */
	u32	pma_signal_ok_reg;	/* signal ok:  */

	u32	pcs_err_blk_cnt_reg;	/* error block counter: */
	u32	rf_lf_status_reg;	/* RF/LF status: */
	u8	pcs_link_reg;		/* pcs link: */
	u8	mac_link_reg;		/* mac link: */
	u8	mac_tx_en;
	u8	mac_rx_en;
	u32	pcs_err_cnt;

	/* struct hinic_hilink_lane: 40 bytes */
	u8	lane1[40];	/* 25GE lane in old firmware */

	u8	rsvd1[266];	/* hilink machine state */

	u8	lane2[HILINK_MAX_LANE * 40];	/* max 4 lane for 40GE/100GE */

	u8	rsvd2[2];
};

struct hinic_hilink_link_info {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	port_id;
	u8	info_type;	/* 1: link up  2: link down  3 cable plugged */
	u8	rsvd1;

	struct hinic_link_info info;

	u8	rsvd2[352];
};

struct hinic_link_ksettings_info {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	rsvd1;

	u32	valid_bitmap;
	u32	speed;		/* enum nic_speed_level */
	u8	autoneg;	/* 0 - off; 1 - on */
	u8	fec;		/* 0 - RSFEC; 1 - BASEFEC; 2 - NOFEC */
	u8	rsvd2[18];	/* reserved for duplex, port, etc. */
};

enum hinic_tx_promsic {
	HINIC_TX_PROMISC_ENABLE	= 0,
	HINIC_TX_PROMISC_DISABLE	= 1,
};

struct hinic_promsic_info {
	u8	status;
	u8	version;
	u8	rsvd0[6];
	u16	func_id;
	u8	cfg;
	u8	rsvd1;
};


struct hinic_netq_cfg_msg {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u8	netq_en;
	u8	rsvd;
};

/* add/del rxq filter msg */
struct hinic_rq_filter_msg {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_id;
	u16	qid;
	u8	filter_type;
	u8	qflag;/*0:stdq, 1:defq, 2: netq*/

	u8	mac[6];
	struct {
		u8	inner_mac[6];
		u32	vni;
	} vxlan;
};

int hinic_get_hilink_link_info(void *hwdev, struct hinic_link_info *info);

#endif
