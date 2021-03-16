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

#ifndef HINIC_CFG_H
#define HINIC_CFG_H

#define OS_VF_ID_TO_HW(os_vf_id) ((os_vf_id) + 1)
#define HW_VF_ID_TO_OS(hw_vf_id) ((hw_vf_id) - 1)

#define FW_SUPPORT_MAC_REUSE		0x1
#define FW_SUPPORT_MAC_REUSE_FUNC(hwdev)	\
	((hwdev)->fw_support_func_flag & FW_SUPPORT_MAC_REUSE)

#define HINIC_VLAN_PRIORITY_SHIFT	13

#define HINIC_RSS_INDIR_SIZE		256
#define HINIC_DCB_TC_MAX		0x8
#define HINIC_DCB_UP_MAX		0x8
#define HINIC_DCB_COS_MAX		0x8
#define HINIC_DCB_PG_MAX		0x8

#define HINIC_DCB_TSA_TC_SP		2
#define HINIC_DCB_TSA_TC_DWRR		0

#define HINIC_RSS_KEY_SIZE		40

#define HINIC_MAX_NUM_RQ		64

#define HINIC_MIN_MTU_SIZE		256
#define HINIC_MAX_JUMBO_FRAME_SIZE	9600

#define HINIC_LRO_MAX_WQE_NUM_UPPER	32
#define HINIC_LRO_MAX_WQE_NUM_LOWER	1
#define HINIC_LRO_MAX_WQE_NUM_DEFAULT_ARM 4
#define HINIC_LRO_MAX_WQE_NUM_DEFAULT_X86 8
#define HINIC_LRO_MAX_WQE_NUM_DEFAULT     8
#define HINIC_LRO_WQE_NUM_PANGEA_DEFAULT	32

#define HINIC_LRO_RX_TIMER_UPPER	1024
#define HINIC_LRO_RX_TIMER_LOWER	1
#define HINIC_LRO_RX_TIMER_DEFAULT	16
#define HINIC_LRO_RX_TIMER_DEFAULT_25GE	16
#define HINIC_LRO_RX_TIMER_DEFAULT_100GE 64
#define HINIC_LRO_RX_TIMER_DEFAULT_PG_10GE	10
#define HINIC_LRO_RX_TIMER_DEFAULT_PG_100GE 8

#if defined(__aarch64__)
#define HINIC_LOWEST_LATENCY		1
#define HINIC_RX_RATE_LOW		400000
#define HINIC_RX_COAL_TIME_LOW		20
#define HINIC_RX_PENDING_LIMIT_LOW	2
#define HINIC_RX_RATE_HIGH		1000000
#define HINIC_RX_COAL_TIME_HIGH		225
#define HINIC_RX_PENDING_LIMIT_HIGH	50
#define HINIC_RX_RATE_THRESH		35000
#define HINIC_TX_RATE_THRESH		35000
#define HINIC_RX_RATE_LOW_VM		400000
#define HINIC_RX_PENDING_LIMIT_HIGH_VM	50
#elif defined(__x86_64__)
#define HINIC_LOWEST_LATENCY		1
#define HINIC_RX_RATE_LOW		400000
#define HINIC_RX_COAL_TIME_LOW		16
#define HINIC_RX_PENDING_LIMIT_LOW	2
#define HINIC_RX_RATE_HIGH		1000000
#define HINIC_RX_COAL_TIME_HIGH		225
#define HINIC_RX_PENDING_LIMIT_HIGH	8
#define HINIC_RX_RATE_THRESH		50000
#define HINIC_TX_RATE_THRESH		50000
#define HINIC_RX_RATE_LOW_VM		100000
#define HINIC_RX_PENDING_LIMIT_HIGH_VM	87
#else
#define HINIC_LOWEST_LATENCY		1
#define HINIC_RX_RATE_LOW		400000
#define HINIC_RX_COAL_TIME_LOW		16
#define HINIC_RX_PENDING_LIMIT_LOW	2
#define HINIC_RX_RATE_HIGH		1000000
#define HINIC_RX_COAL_TIME_HIGH		225
#define HINIC_RX_PENDING_LIMIT_HIGH	8
#define HINIC_RX_RATE_THRESH		50000
#define HINIC_TX_RATE_THRESH		50000
#define HINIC_RX_RATE_LOW_VM		100000
#define HINIC_RX_PENDING_LIMIT_HIGH_VM	87
#endif /* end of defined(__x86_64__) */

enum hinic_board_type {
	HINIC_BOARD_UKNOWN        = 0,
	HINIC_BOARD_10GE          = 1,
	HINIC_BOARD_25GE          = 2,
	HINIC_BOARD_40GE          = 3,
	HINIC_BOARD_100GE         = 4,
	HINIC_BOARD_PG_TP_10GE    = 5,
	HINIC_BOARD_PG_SM_25GE	  = 6,
	HINIC_BOARD_PG_100GE      = 7,
};

enum hinic_os_type {
	HINIC_OS_UKNOWN       = 0,
	HINIC_OS_HUAWEI       = 1,
	HINIC_OS_NON_HUAWEI   = 2,
};

enum hinic_cpu_type {
	HINIC_CPU_UKNOWN      = 0,
	HINIC_CPU_X86_GENERIC = 1,
	HINIC_CPU_ARM_GENERIC = 2,
};

struct hinic_adaptive_rx_cfg {
	u32 lowest_lat;
	u32 rate_low;
	u32 coal_time_low;
	u32 pending_limit_low;
	u32 rate_high;
	u32 coal_time_high;
	u32 pending_limit_high;
	u32 rate_thresh;
};

struct hinic_lro_cfg {
	u32 enable;
	u32 timer;
	u32 buffer_size;
};

struct hinic_environment_info {
	enum hinic_board_type board;
	enum hinic_os_type os;
	enum hinic_cpu_type cpu;
};

struct hinic_adaptive_cfg {
	struct hinic_adaptive_rx_cfg adaptive_rx;
	struct hinic_lro_cfg lro;
};

enum hinic_rss_hash_type {
	HINIC_RSS_HASH_ENGINE_TYPE_XOR = 0,
	HINIC_RSS_HASH_ENGINE_TYPE_TOEP,

	HINIC_RSS_HASH_ENGINE_TYPE_MAX,
};

struct ifla_vf_info;
struct hinic_dcb_state;

struct nic_port_info {
	u8	port_type;
	u8	autoneg_cap;
	u8	autoneg_state;
	u8	duplex;
	u8	speed;
};

enum nic_media_type {
	MEDIA_UNKNOWN = -1,
	MEDIA_FIBRE = 0,
	MEDIA_COPPER,
	MEDIA_BACKPLANE
};

enum nic_speed_level {
	LINK_SPEED_10MB = 0,
	LINK_SPEED_100MB,
	LINK_SPEED_1GB,
	LINK_SPEED_10GB,
	LINK_SPEED_25GB,
	LINK_SPEED_40GB,
	LINK_SPEED_100GB,
	LINK_SPEED_LEVELS,
};

enum hinic_link_mode {
	HINIC_10GE_BASE_KR = 0,
	HINIC_40GE_BASE_KR4 = 1,
	HINIC_40GE_BASE_CR4 = 2,
	HINIC_100GE_BASE_KR4 = 3,
	HINIC_100GE_BASE_CR4 = 4,
	HINIC_25GE_BASE_KR_S = 5,
	HINIC_25GE_BASE_CR_S = 6,
	HINIC_25GE_BASE_KR = 7,
	HINIC_25GE_BASE_CR = 8,
	HINIC_GE_BASE_KX = 9,
	HINIC_LINK_MODE_NUMBERS,

	HINIC_SUPPORTED_UNKNOWN = 0xFFFF,
};

enum hinic_port_type {
	HINIC_PORT_TP,		/* BASET */
	HINIC_PORT_AUI,
	HINIC_PORT_MII,
	HINIC_PORT_FIBRE,	/* OPTICAL */
	HINIC_PORT_BNC,
	HINIC_PORT_ELEC,
	HINIC_PORT_COPPER,	/* PORT_DA */
	HINIC_PORT_AOC,
	HINIC_PORT_BACKPLANE,
	HINIC_PORT_NONE = 0xEF,
	HINIC_PORT_OTHER = 0xFF,
};

enum hinic_link_status {
	HINIC_LINK_DOWN = 0,
	HINIC_LINK_UP
};

struct nic_pause_config {
	u32 auto_neg;
	u32 rx_pause;
	u32 tx_pause;
};

struct nic_lro_info {
	u16 func_id;
	u8 lro_ipv4_en;
	u8 lro_ipv6_en;
	u8 lro_max_wqe_num;
	u8 lro_timer_en;
	u32 lro_period;
};

struct nic_rss_type {
	u8 tcp_ipv6_ext;
	u8 ipv6_ext;
	u8 tcp_ipv6;
	u8 ipv6;
	u8 tcp_ipv4;
	u8 ipv4;
	u8 udp_ipv6;
	u8 udp_ipv4;
};

struct hinic_vport_stats {
	u64 tx_unicast_pkts_vport;
	u64 tx_unicast_bytes_vport;
	u64 tx_multicast_pkts_vport;
	u64 tx_multicast_bytes_vport;
	u64 tx_broadcast_pkts_vport;
	u64 tx_broadcast_bytes_vport;

	u64 rx_unicast_pkts_vport;
	u64 rx_unicast_bytes_vport;
	u64 rx_multicast_pkts_vport;
	u64 rx_multicast_bytes_vport;
	u64 rx_broadcast_pkts_vport;
	u64 rx_broadcast_bytes_vport;

	u64 tx_discard_vport;
	u64 rx_discard_vport;
	u64 tx_err_vport;
	u64 rx_err_vport;
};

struct hinic_phy_port_stats {
	u64 mac_rx_total_pkt_num;
	u64 mac_rx_total_oct_num;
	u64 mac_rx_bad_pkt_num;
	u64 mac_rx_bad_oct_num;
	u64 mac_rx_good_pkt_num;
	u64 mac_rx_good_oct_num;
	u64 mac_rx_uni_pkt_num;
	u64 mac_rx_multi_pkt_num;
	u64 mac_rx_broad_pkt_num;

	u64 mac_tx_total_pkt_num;
	u64 mac_tx_total_oct_num;
	u64 mac_tx_bad_pkt_num;
	u64 mac_tx_bad_oct_num;
	u64 mac_tx_good_pkt_num;
	u64 mac_tx_good_oct_num;
	u64 mac_tx_uni_pkt_num;
	u64 mac_tx_multi_pkt_num;
	u64 mac_tx_broad_pkt_num;

	u64 mac_rx_fragment_pkt_num;
	u64 mac_rx_undersize_pkt_num;
	u64 mac_rx_undermin_pkt_num;
	u64 mac_rx_64_oct_pkt_num;
	u64 mac_rx_65_127_oct_pkt_num;
	u64 mac_rx_128_255_oct_pkt_num;
	u64 mac_rx_256_511_oct_pkt_num;
	u64 mac_rx_512_1023_oct_pkt_num;
	u64 mac_rx_1024_1518_oct_pkt_num;
	u64 mac_rx_1519_2047_oct_pkt_num;
	u64 mac_rx_2048_4095_oct_pkt_num;
	u64 mac_rx_4096_8191_oct_pkt_num;
	u64 mac_rx_8192_9216_oct_pkt_num;
	u64 mac_rx_9217_12287_oct_pkt_num;
	u64 mac_rx_12288_16383_oct_pkt_num;
	u64 mac_rx_1519_max_bad_pkt_num;
	u64 mac_rx_1519_max_good_pkt_num;
	u64 mac_rx_oversize_pkt_num;
	u64 mac_rx_jabber_pkt_num;

	u64 mac_rx_pause_num;
	u64 mac_rx_pfc_pkt_num;
	u64 mac_rx_pfc_pri0_pkt_num;
	u64 mac_rx_pfc_pri1_pkt_num;
	u64 mac_rx_pfc_pri2_pkt_num;
	u64 mac_rx_pfc_pri3_pkt_num;
	u64 mac_rx_pfc_pri4_pkt_num;
	u64 mac_rx_pfc_pri5_pkt_num;
	u64 mac_rx_pfc_pri6_pkt_num;
	u64 mac_rx_pfc_pri7_pkt_num;
	u64 mac_rx_control_pkt_num;
	u64 mac_rx_y1731_pkt_num;
	u64 mac_rx_sym_err_pkt_num;
	u64 mac_rx_fcs_err_pkt_num;
	u64 mac_rx_send_app_good_pkt_num;
	u64 mac_rx_send_app_bad_pkt_num;

	u64 mac_tx_fragment_pkt_num;
	u64 mac_tx_undersize_pkt_num;
	u64 mac_tx_undermin_pkt_num;
	u64 mac_tx_64_oct_pkt_num;
	u64 mac_tx_65_127_oct_pkt_num;
	u64 mac_tx_128_255_oct_pkt_num;
	u64 mac_tx_256_511_oct_pkt_num;
	u64 mac_tx_512_1023_oct_pkt_num;
	u64 mac_tx_1024_1518_oct_pkt_num;
	u64 mac_tx_1519_2047_oct_pkt_num;
	u64 mac_tx_2048_4095_oct_pkt_num;
	u64 mac_tx_4096_8191_oct_pkt_num;
	u64 mac_tx_8192_9216_oct_pkt_num;
	u64 mac_tx_9217_12287_oct_pkt_num;
	u64 mac_tx_12288_16383_oct_pkt_num;
	u64 mac_tx_1519_max_bad_pkt_num;
	u64 mac_tx_1519_max_good_pkt_num;
	u64 mac_tx_oversize_pkt_num;
	u64 mac_tx_jabber_pkt_num;

	u64 mac_tx_pause_num;
	u64 mac_tx_pfc_pkt_num;
	u64 mac_tx_pfc_pri0_pkt_num;
	u64 mac_tx_pfc_pri1_pkt_num;
	u64 mac_tx_pfc_pri2_pkt_num;
	u64 mac_tx_pfc_pri3_pkt_num;
	u64 mac_tx_pfc_pri4_pkt_num;
	u64 mac_tx_pfc_pri5_pkt_num;
	u64 mac_tx_pfc_pri6_pkt_num;
	u64 mac_tx_pfc_pri7_pkt_num;
	u64 mac_tx_control_pkt_num;
	u64 mac_tx_y1731_pkt_num;
	u64 mac_tx_1588_pkt_num;
	u64 mac_tx_err_all_pkt_num;
	u64 mac_tx_from_app_good_pkt_num;
	u64 mac_tx_from_app_bad_pkt_num;

	u64 mac_rx_higig2_ext_pkt_num;
	u64 mac_rx_higig2_message_pkt_num;
	u64 mac_rx_higig2_error_pkt_num;
	u64 mac_rx_higig2_cpu_ctrl_pkt_num;
	u64 mac_rx_higig2_unicast_pkt_num;
	u64 mac_rx_higig2_broadcast_pkt_num;
	u64 mac_rx_higig2_l2_multicast_pkt_num;
	u64 mac_rx_higig2_l3_multicast_pkt_num;

	u64 mac_tx_higig2_message_pkt_num;
	u64 mac_tx_higig2_ext_pkt_num;
	u64 mac_tx_higig2_cpu_ctrl_pkt_num;
	u64 mac_tx_higig2_unicast_pkt_num;
	u64 mac_tx_higig2_broadcast_pkt_num;
	u64 mac_tx_higig2_l2_multicast_pkt_num;
	u64 mac_tx_higig2_l3_multicast_pkt_num;
};

enum hinic_rq_filter_type {
	HINIC_RQ_FILTER_TYPE_NONE      = 0x0,
	HINIC_RQ_FILTER_TYPE_MAC_ONLY  = (1 << 0),
	HINIC_RQ_FILTER_TYPE_VLAN_ONLY = (1 << 1),
	HINIC_RQ_FILTER_TYPE_VLANMAC   = (1 << 2),
	HINIC_RQ_FILTER_TYPE_VXLAN     = (1 << 3),
	HINIC_RQ_FILTER_TYPE_GENEVE    = (1 << 4),
};

struct hinic_rq_filter_info {
	u16	qid;
	u8	filter_type;/* 1: mac, 8: vxlan */
	u8	qflag;/*0:stdq, 1:defq, 2: netq*/

	u8	mac[ETH_ALEN];
	struct {
		u8	inner_mac[ETH_ALEN];
		u32	vni;
	} vxlan;
};

#define HINIC_MGMT_VERSION_MAX_LEN	32

#define HINIC_FW_VERSION_NAME	16
#define HINIC_FW_VERSION_SECTION_CNT	4
#define HINIC_FW_VERSION_SECTION_BORDER	0xFF
struct hinic_fw_version {
	u8	mgmt_ver[HINIC_FW_VERSION_NAME];
	u8	microcode_ver[HINIC_FW_VERSION_NAME];
	u8	boot_ver[HINIC_FW_VERSION_NAME];
};

enum hinic_valid_link_settings {
	HILINK_LINK_SET_SPEED = 0x1,
	HILINK_LINK_SET_AUTONEG = 0x2,
	HILINK_LINK_SET_FEC = 0x4,
};

struct hinic_link_ksettings {
	u32	valid_bitmap;
	u32	speed;		/* enum nic_speed_level */
	u8	autoneg;	/* 0 - off; 1 - on */
	u8	fec;		/* 0 - RSFEC; 1 - BASEFEC; 2 - NOFEC */
};

enum hinic_link_follow_status {
	HINIC_LINK_FOLLOW_DEFAULT,
	HINIC_LINK_FOLLOW_PORT,
	HINIC_LINK_FOLLOW_SEPARATE,
	HINIC_LINK_FOLLOW_STATUS_MAX,
};

enum hinic_lro_en_status {
	HINIC_LRO_STATUS_DISABLE,
	HINIC_LRO_STATUS_ENABLE,
	HINIC_LRO_STATUS_UNSET,
};

#define HINIC_VLAN_FILTER_EN		BIT(0)
#define HINIC_BROADCAST_FILTER_EX_EN	BIT(1)

/* Set mac_vlan table */
int hinic_set_mac(void *hwdev, const u8 *mac_addr, u16 vlan_id, u16 func_id);

int hinic_del_mac(void *hwdev, const u8 *mac_addr, u16 vlan_id, u16 func_id);

int hinic_update_mac(void *hwdev, u8 *old_mac, u8 *new_mac,
		     u16 vlan_id, u16 func_id);
int hinic_update_mac_vlan(void *hwdev, u16 old_vlan, u16 new_vlan, int vf_id);
/* Obtaining the permanent mac */
int hinic_get_default_mac(void *hwdev, u8 *mac_addr);
/* Check whether the current solution is using this interface,
 * the current code does not invoke the sdk interface to set mtu
 */
int hinic_set_port_mtu(void *hwdev, u32 new_mtu);
/* Set vlan leaf table */
int hinic_add_vlan(void *hwdev, u16 vlan_id, u16 func_id);

int hinic_set_vlan_fliter(void *hwdev, u32 vlan_filter_ctrl);

int hinic_del_vlan(void *hwdev, u16 vlan_id, u16 func_id);

int hinic_get_port_info(void *hwdev, struct nic_port_info *port_info);

int hinic_set_autoneg(void *hwdev, bool enable);

int hinic_force_port_relink(void *hwdev);

int hinic_get_link_mode(void *hwdev, enum hinic_link_mode *supported,
			enum hinic_link_mode *advertised);

int hinic_set_port_link_status(void *hwdev, bool enable);

int hinic_set_speed(void *hwdev, enum nic_speed_level speed);
/* SPEED_UNKNOWN = -1,SPEED_10MB_LINK = 0 */
int hinic_get_speed(void *hwdev, enum nic_speed_level *speed);

int hinic_get_link_state(void *hwdev, u8 *link_state);

int hinic_set_pause_info(void *hwdev, struct nic_pause_config nic_pause);

int hinic_get_hw_pause_info(void *hwdev, struct nic_pause_config *nic_pause);

int hinic_get_pause_info(void *hwdev, struct nic_pause_config *nic_pause);

int hinic_set_rx_mode(void *hwdev, u32 enable);

/* offload feature */
int hinic_set_rx_vlan_offload(void *hwdev, u8 en);

int hinic_set_rx_csum_offload(void *hwdev, u32 en);

int hinic_set_tx_tso(void *hwdev, u8 tso_en);

/* Linux NIC used */
int hinic_set_rx_lro_state(void *hwdev, u8 lro_en, u32 lro_timer, u32 wqe_num);

/* Win NIC used */
int hinic_set_rx_lro(void *hwdev, u8 ipv4_en, u8 ipv6_en, u8 max_wqe_num);

/* Related command dcbtool */
int hinic_dcb_set_pfc(void *hwdev, u8 pfc_en, u8 pfc_bitmap);

int hinic_dcb_get_pfc(void *hwdev, u8 *pfc_en_bitmap);

int hinic_dcb_set_ets(void *hwdev, u8 *up_tc, u8 *pg_bw, u8 *pgid,
		      u8 *up_bw, u8 *prio);

int hinic_dcb_get_ets(void *hwdev, u8 *up_tc, u8 *pg_bw, u8 *pgid,
		      u8 *up_bw, u8 *prio);

int hinic_dcb_set_cos_up_map(void *hwdev, u8 cos_valid_bitmap, u8 *cos_up);

int hinic_dcb_set_rq_iq_mapping(void *hwdev, u32 num_rqs, u8 *map);

int hinic_set_pfc_threshold(void *hwdev, u16 op_type, u16 threshold);

int hinic_set_bp_thd(void *hwdev, u16 threshold);

int hinic_disable_fw_bp(void *hwdev);

int hinic_set_iq_enable(void *hwdev, u16 q_id, u16 lower_thd, u16 prod_idx);

int hinic_set_iq_enable_mgmt(void *hwdev, u16 q_id, u16 lower_thd,
			     u16 prod_idx);

/* nictool adaptation interface*/
int hinic_set_lro_aging_timer(void *hwdev, u8 timer_en, u32 period);
/* There should be output parameters, add the
 * output parameter struct nic_up_offload *cfg
 */
int hinic_get_rx_lro(void *hwdev, struct nic_lro_info *lro_info);

int hinic_get_jumbo_frame_size(void *hwdev, u32 *jumbo_size);

int hinic_set_jumbo_frame_size(void *hwdev, u32 jumbo_size);

int hinic_set_loopback_mode(void *hwdev, bool enable);
int hinic_set_loopback_mode_ex(void *hwdev, u32 mode, u32 enable);
int hinic_get_loopback_mode_ex(void *hwdev, u32 *mode, u32 *enable);

int hinic_get_port_enable_state(void *hwdev, bool *enable);

int hinic_get_vport_enable_state(void *hwdev, bool *enable);

int hinic_set_lli_state(void *hwdev, u8 lli_state);

int hinic_set_vport_enable(void *hwdev, bool enable);

int hinic_set_port_enable(void *hwdev, bool enable);

/* rss */
int hinic_set_rss_type(void *hwdev, u32 tmpl_idx, struct nic_rss_type rss_type);

int hinic_get_rss_type(void *hwdev, u32 tmpl_idx,
		       struct nic_rss_type *rss_type);

int hinic_rss_set_template_tbl(void *hwdev, u32 tmpl_idx, const u8 *temp);

int hinic_rss_get_template_tbl(void *hwdev, u32 tmpl_idx, u8 *temp);

int hinic_rss_get_hash_engine(void *hwdev, u8 tmpl_idx, u8 *type);

int hinic_rss_set_hash_engine(void *hwdev, u8 tmpl_idx, u8 type);

int hinic_rss_get_indir_tbl(void *hwdev, u32 tmpl_idx, u32 *indir_table);

int hinic_rss_set_indir_tbl(void *hwdev, u32 tmpl_idx, const u32 *indir_table);

int hinic_rss_cfg(void *hwdev, u8 rss_en, u8 tmpl_idx, u8 tc_num, u8 *prio_tc);

int hinic_rss_template_alloc(void *hwdev, u8 *tmpl_idx);

int hinic_rss_template_free(void *hwdev, u8 tmpl_idx);

/* disable or enable traffic of all functions in the same port */
int hinic_set_port_funcs_state(void *hwdev, bool enable);

int hinic_reset_port_link_cfg(void *hwdev);

int hinic_get_vport_stats(void *hwdev, struct hinic_vport_stats *stats);

int hinic_get_phy_port_stats(void *hwdev, struct hinic_phy_port_stats *stats);

int hinic_get_mgmt_version(void *hwdev, u8 *mgmt_ver);

int hinic_get_fw_version(void *hwdev, struct hinic_fw_version *fw_ver);

int hinic_save_vf_mac(void *hwdev, u16 vf_id, u8 *mac);

int hinic_add_vf_vlan(void *hwdev, int vf_id, u16 vlan, u8 qos);

int hinic_kill_vf_vlan(void *hwdev, int vf_id);

int hinic_set_vf_mac(void *hwdev, int vf_id, unsigned char *mac_addr);

u16 hinic_vf_info_vlanprio(void *hwdev, int vf_id);

bool hinic_vf_is_registered(void *hwdev, u16 vf_id);

void hinic_get_vf_config(void *hwdev, u16 vf_id, struct ifla_vf_info *ivi);

void hinic_notify_all_vfs_link_changed(void *hwdev, u8 link);

void hinic_save_pf_link_status(void *hwdev, u8 link);

int hinic_set_vf_link_state(void *hwdev, u16 vf_id, int link);

#ifdef HAVE_VF_SPOOFCHK_CONFIGURE
int hinic_set_vf_spoofchk(void *hwdev, u16 vf_id, bool spoofchk);
bool hinic_vf_info_spoofchk(void *hwdev, int vf_id);
#endif

#ifdef HAVE_NDO_SET_VF_TRUST
int hinic_set_vf_trust(void *hwdev, u16 vf_id, bool trust);
bool hinic_vf_info_trust(void *hwdev, int vf_id);
#endif

int hinic_set_vf_tx_rate(void *hwdev, u16 vf_id, u32 max_rate, u32 min_rate);

int hinic_init_vf_hw(void *hwdev, u16 start_vf_id, u16 end_vf_id);

int hinic_deinit_vf_hw(void *hwdev, u16 start_vf_id, u16 end_vf_id);

int hinic_set_dcb_state(void *hwdev, struct hinic_dcb_state *dcb_state);

int hinic_get_dcb_state(void *hwdev, struct hinic_dcb_state *dcb_state);

int hinic_get_pf_dcb_state(void *hwdev, struct hinic_dcb_state *dcb_state);

int hinic_set_ipsu_mac(void *hwdev, u16 index, u8 *mac_addr, u16 vlan_id,
		       u16 func_id);
int hinic_get_ipsu_mac(void *hwdev, u16 index, u8 *mac_addr, u16 *vlan_id,
		       u16 *func_id);
int hinic_set_anti_attack(void *hwdev, bool enable);

int hinic_flush_sq_res(void *hwdev);

int hinic_set_super_cqe_state(void *hwdev, bool enable);

int hinic_set_func_capture_en(void *hwdev, u16 func_id, bool cap_en);

int hinic_force_drop_tx_pkt(void *hwdev);

int hinic_update_pf_bw(void *hwdev);

int hinic_set_pf_bw_limit(void *hwdev, u32 bw_limit);

int hinic_set_link_status_follow(void *hwdev,
				 enum hinic_link_follow_status status);
int hinic_disable_tx_promisc(void *hwdev);

/* HILINK module */
int hinic_set_link_settings(void *hwdev, struct hinic_link_ksettings *settings);


int hinic_enable_netq(void *hwdev, u8 en);
int hinic_add_hw_rqfilter(void *hwdev,
			  struct hinic_rq_filter_info *filter_info);
int hinic_del_hw_rqfilter(void *hwdev,
			  struct hinic_rq_filter_info *filter_info);
int hinic_get_sfp_eeprom(void *hwdev, u8 *data, u16 *len);
int hinic_get_sfp_type(void *hwdev, u8 *data0, u8 *data1);

#endif
