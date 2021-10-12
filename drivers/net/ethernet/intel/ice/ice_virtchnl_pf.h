/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2021, Intel Corporation. */

#ifndef _ICE_VIRTCHNL_PF_H_
#define _ICE_VIRTCHNL_PF_H_
#include "ice.h"
#include "ice_virtchnl_fdir.h"
#include "ice_dcf.h"
#include "ice_vsi_vlan_ops.h"

#define ICE_VIRTCHNL_SUPPORTED_QTYPES	2

/* Restrict number of MAC Addr and VLAN that non-trusted VF can programmed */
#define ICE_MAX_VLAN_PER_VF		8
/* MAC filters: 1 is reserved for the VF's default/perm_addr/LAA MAC, 1 for
 * broadcast, and 16 for additional unicast/multicast filters
 */
#define ICE_MAX_MACADDR_PER_VF		18

/* Malicious Driver Detection */
#define ICE_DFLT_NUM_INVAL_MSGS_ALLOWED		10
#define ICE_MDD_EVENTS_THRESHOLD		30

/* Static VF transaction/status register def */
#define VF_DEVICE_STATUS		0xAA
#define VF_TRANS_PENDING_M		0x20

/* wait defines for polling PF_PCI_CIAD register status */
#define ICE_PCI_CIAD_WAIT_COUNT		100
#define ICE_PCI_CIAD_WAIT_DELAY_US	1

/* VF resource constraints */
#define ICE_MAX_VF_COUNT		256
#define ICE_MAX_QS_PER_VF	256
/* Maximum number of queue pairs to configure by default for a VF */
#define ICE_MAX_DFLT_QS_PER_VF		16
#define ICE_MIN_QS_PER_VF		1
#define ICE_NONQ_VECS_VF		1
#define ICE_MAX_SCATTER_QS_PER_VF	16
#define ICE_MAX_RSS_QS_PER_LARGE_VF	64
#define ICE_MAX_RSS_QS_PER_VF		16
#define ICE_NUM_VF_MSIX_MAX		65
#define ICE_NUM_VF_MSIX_LARGE		33
#define ICE_NUM_VF_MSIX_MED		17
#define ICE_NUM_VF_MSIX_SMALL		5
#define ICE_NUM_VF_MSIX_MULTIQ_MIN	3
#define ICE_MIN_INTR_PER_VF		(ICE_MIN_QS_PER_VF + 1)
#define ICE_MAX_VF_RESET_TRIES		40
#define ICE_MAX_VF_RESET_SLEEP_MS	20
#define ICE_MAX_IPSEC_CAPABLE_VF_ID	127

#define ice_for_each_vf(pf, i) \
	for ((i) = 0; (i) < (pf)->num_alloc_vfs; (i)++)

/* Max number of flexible descriptor rxdid */
#define ICE_FLEX_DESC_RXDID_MAX_NUM 64

/* Specific VF states */
enum ice_vf_states {
	ICE_VF_STATE_INIT = 0,		/* PF is initializing VF */
	ICE_VF_STATE_ACTIVE,		/* VF resources are allocated for use */
	ICE_VF_STATE_QS_ENA,		/* VF queue(s) enabled */
	ICE_VF_STATE_DIS,
	ICE_VF_STATE_MC_PROMISC,
	ICE_VF_STATE_UC_PROMISC,
	ICE_VF_STATES_NBITS
};

/* VF capabilities */
enum ice_virtchnl_cap {
	ICE_VIRTCHNL_VF_CAP_L2 = 0,
	ICE_VIRTCHNL_VF_CAP_PRIVILEGE,
};

/* DDP package type */
enum ice_pkg_type {
	ICE_PKG_TYPE_UNKNOWN = 0,
	ICE_PKG_TYPE_OS_DEFAULT,
	ICE_PKG_TYPE_COMMS,
	ICE_PKG_TYPE_WIRELESS_EDGE,
	ICE_PKG_TYPE_GTP_OVER_GRE,
	ICE_PKG_TYPE_END,
};

/* In ADQ, max 4 VSI's can be allocated per VF including primary VF VSI.
 * These variables are used to store indices, ID's and number of queues
 * for each VSI including that of primary VF VSI. Each Traffic class is
 * termed as channel and each channel can in-turn have 4 queues which
 * means max 16 queues overall per VF.
 */
struct ice_channel_vf {
	u16 vsi_idx; /* index in PF struct for all channel VSIs */
	u16 vsi_num; /* HW (absolute) index of this VSI */
	u16 num_qps; /* number of queue pairs requested by user */
	u16 offset;
	u64 max_tx_rate; /* Tx rate limiting for channels */

	/* type of filter: dest/src/dest+src port */
	u32 fltr_type;
};

struct ice_time_mac {
	unsigned long time_modified;
	u8 addr[ETH_ALEN];
};

/* VF MDD events print structure */
struct ice_mdd_vf_events {
	u16 count;			/* total count of Rx|Tx events */
	/* count number of the last printed event */
	u16 last_printed;
};

/* The VF VLAN information controlled by DCF */
struct ice_dcf_vlan_info {
	struct ice_vlan outer_port_vlan;
	u16 outer_stripping_tpid;
	u8 outer_stripping_ena:1;
	u8 applying:1;
};

#define ICE_HASH_IP_CTX_IP		0
#define ICE_HASH_IP_CTX_IP_ESP		1
#define ICE_HASH_IP_CTX_IP_UDP_ESP	2
#define ICE_HASH_IP_CTX_IP_AH		3
#define ICE_HASH_IP_CTX_IP_L2TPV3	4
#define ICE_HASH_IP_CTX_IP_PFCP		5
#define ICE_HASH_IP_CTX_IP_UDP		6
#define ICE_HASH_IP_CTX_IP_TCP		7
#define ICE_HASH_IP_CTX_IP_SCTP		8
#define ICE_HASH_IP_CTX_MAX		9

struct ice_vf_hash_ip_ctx {
	struct ice_rss_hash_cfg ctx[ICE_HASH_IP_CTX_MAX];
};

#define ICE_HASH_GTPU_CTX_EH_IP		0
#define ICE_HASH_GTPU_CTX_EH_IP_UDP	1
#define ICE_HASH_GTPU_CTX_EH_IP_TCP	2
#define ICE_HASH_GTPU_CTX_UP_IP		3
#define ICE_HASH_GTPU_CTX_UP_IP_UDP	4
#define ICE_HASH_GTPU_CTX_UP_IP_TCP	5
#define ICE_HASH_GTPU_CTX_DW_IP		6
#define ICE_HASH_GTPU_CTX_DW_IP_UDP	7
#define ICE_HASH_GTPU_CTX_DW_IP_TCP	8
#define ICE_HASH_GTPU_CTX_MAX		9

struct ice_vf_hash_gtpu_ctx {
	struct ice_rss_hash_cfg ctx[ICE_HASH_GTPU_CTX_MAX];
};

struct ice_vf_hash_ctx {
	struct ice_vf_hash_ip_ctx v4;
	struct ice_vf_hash_ip_ctx v6;
	struct ice_vf_hash_gtpu_ctx ipv4;
	struct ice_vf_hash_gtpu_ctx ipv6;
};

struct ice_vf;

struct ice_vc_vf_ops {
	int (*get_ver_msg)(struct ice_vf *vf, u8 *msg);
	int (*get_vf_res_msg)(struct ice_vf *vf, u8 *msg);
	void (*reset_vf)(struct ice_vf *vf);
	int (*add_mac_addr_msg)(struct ice_vf *vf, u8 *msg);
	int (*del_mac_addr_msg)(struct ice_vf *vf, u8 *msg);
	int (*cfg_qs_msg)(struct ice_vf *vf, u8 *msg);
	int (*ena_qs_msg)(struct ice_vf *vf, u8 *msg);
	int (*dis_qs_msg)(struct ice_vf *vf, u8 *msg);
	int (*request_qs_msg)(struct ice_vf *vf, u8 *msg);
	int (*cfg_irq_map_msg)(struct ice_vf *vf, u8 *msg);
	int (*config_rss_key)(struct ice_vf *vf, u8 *msg);
	int (*config_rss_lut)(struct ice_vf *vf, u8 *msg);
	int (*get_stats_msg)(struct ice_vf *vf, u8 *msg);
	int (*cfg_promiscuous_mode_msg)(struct ice_vf *vf, u8 *msg);
	int (*add_vlan_msg)(struct ice_vf *vf, u8 *msg);
	int (*remove_vlan_msg)(struct ice_vf *vf, u8 *msg);
	int (*query_rxdid)(struct ice_vf *vf);
	int (*get_rss_hena)(struct ice_vf *vf);
	int (*set_rss_hena_msg)(struct ice_vf *vf, u8 *msg);
	int (*ena_vlan_stripping)(struct ice_vf *vf);
	int (*dis_vlan_stripping)(struct ice_vf *vf);
#ifdef HAVE_TC_SETUP_CLSFLOWER
	int (*add_qch_msg)(struct ice_vf *vf, u8 *msg);
	int (*add_switch_filter_msg)(struct ice_vf *vf, u8 *msg);
	int (*del_switch_filter_msg)(struct ice_vf *vf, u8 *msg);
	int (*del_qch_msg)(struct ice_vf *vf, u8 *msg);
#endif /* HAVE_TC_SETUP_CLSFLOWER */
	int (*rdma_msg)(struct ice_vf *vf, u8 *msg, u16 msglen);
	int (*cfg_rdma_irq_map_msg)(struct ice_vf *vf, u8 *msg);
	int (*clear_rdma_irq_map)(struct ice_vf *vf);
	int (*dcf_vlan_offload_msg)(struct ice_vf *vf, u8 *msg);
	int (*dcf_cmd_desc_msg)(struct ice_vf *vf, u8 *msg, u16 msglen);
	int (*dcf_cmd_buff_msg)(struct ice_vf *vf, u8 *msg, u16 msglen);
	int (*dis_dcf_cap)(struct ice_vf *vf);
	int (*dcf_get_vsi_map)(struct ice_vf *vf);
	int (*dcf_query_pkg_info)(struct ice_vf *vf);
	int (*handle_rss_cfg_msg)(struct ice_vf *vf, u8 *msg, bool add);
	int (*add_fdir_fltr_msg)(struct ice_vf *vf, u8 *msg);
	int (*del_fdir_fltr_msg)(struct ice_vf *vf, u8 *msg);
	int (*get_max_rss_qregion)(struct ice_vf *vf);
	int (*ena_qs_v2_msg)(struct ice_vf *vf, u8 *msg);
	int (*dis_qs_v2_msg)(struct ice_vf *vf, u8 *msg);
	int (*map_q_vector_msg)(struct ice_vf *vf, u8 *msg);
	int (*get_offload_vlan_v2_caps)(struct ice_vf *vf);
	int (*add_vlan_v2_msg)(struct ice_vf *vf, u8 *msg);
	int (*remove_vlan_v2_msg)(struct ice_vf *vf, u8 *msg);
	int (*ena_vlan_stripping_v2_msg)(struct ice_vf *vf, u8 *msg);
	int (*dis_vlan_stripping_v2_msg)(struct ice_vf *vf, u8 *msg);
	int (*ena_vlan_insertion_v2_msg)(struct ice_vf *vf, u8 *msg);
	int (*dis_vlan_insertion_v2_msg)(struct ice_vf *vf, u8 *msg);
};

/* VF information structure */
struct ice_vf {
	struct ice_pf *pf;


	u16 vf_id;			/* VF ID in the PF space */
	u16 lan_vsi_idx;		/* index into PF struct */
	u16 ctrl_vsi_idx;
	struct ice_vf_fdir fdir;
	struct ice_vf_hash_ctx hash_ctx;
	/* first vector index of this VF in the PF space */
	int first_vector_idx;
	struct ice_sw *vf_sw_id;	/* switch ID the VF VSIs connect to */
	struct virtchnl_version_info vf_ver;
	u32 driver_caps;		/* reported by VF driver */
	u16 stag;			/* VF Port Extender (PE) stag if used */
	struct virtchnl_ether_addr dev_lan_addr;
	struct virtchnl_ether_addr hw_lan_addr;
	struct ice_time_mac legacy_last_added_umac;
	DECLARE_BITMAP(txq_ena, ICE_MAX_QS_PER_VF);
	DECLARE_BITMAP(rxq_ena, ICE_MAX_QS_PER_VF);
	struct ice_vlan port_vlan_info;	/* Port VLAN ID, QoS, and TPID */
	struct virtchnl_vlan_caps vlan_v2_caps;
	struct ice_dcf_vlan_info dcf_vlan_info;
	u8 pf_set_mac:1;		/* VF MAC address set by VMM admin */
	u8 trusted:1;
	u8 spoofchk:1;
#ifdef HAVE_NDO_SET_VF_LINK_STATE
	u8 link_forced:1;
	u8 link_up:1;			/* only valid if VF link is forced */
#endif
	/* VSI indices - actual VSI pointers are maintained in the PF structure
	 * When assigned, these will be non-zero, because VSI 0 is always
	 * the main LAN VSI for the PF.
	 */
	u16 lan_vsi_num;		/* ID as used by firmware */
	unsigned int min_tx_rate;	/* Minimum Tx bandwidth limit in Mbps */
	unsigned int max_tx_rate;	/* Maximum Tx bandwidth limit in Mbps */
	DECLARE_BITMAP(vf_states, ICE_VF_STATES_NBITS);	/* VF runtime states */

	u64 num_inval_msgs;		/* number of continuous invalid msgs */
	u64 num_valid_msgs;		/* number of valid msgs detected */
	unsigned long vf_caps;		/* VF's adv. capabilities */
	u16 num_req_qs;			/* num of queue pairs requested by VF */
	u16 num_mac;
	u16 num_vf_qs;			/* num of queue configured per VF */
	u8 vlan_strip_ena;		/* Outer and Inner VLAN strip enable */
#define ICE_INNER_VLAN_STRIP_ENA	BIT(0)
#define ICE_OUTER_VLAN_STRIP_ENA	BIT(1)
	/* ADQ related variables */
	u8 adq_enabled; /* flag to enable ADQ */
	u8 adq_fltr_ena; /* flag to denote that ADQ filters are applied */
	u8 num_tc;
	u16 num_dmac_chnl_fltrs;
	struct ice_channel_vf ch[VIRTCHNL_MAX_ADQ_V2_CHANNELS];
	struct hlist_head tc_flower_fltr_list;
	struct ice_mdd_vf_events mdd_rx_events;
	struct ice_mdd_vf_events mdd_tx_events;
	struct ice_repr *repr;
	DECLARE_BITMAP(opcodes_allowlist, VIRTCHNL_OP_MAX);
	struct ice_vc_vf_ops vc_ops;

#if IS_ENABLED(CONFIG_NET_DEVLINK)
	/* devlink port data */
	struct devlink_port devlink_port;
#endif /* CONFIG_NET_DEVLINK */
};

/**
 * ice_vc_get_max_chnl_tc_allowed
 * @vf: pointer to the VF info
 *
 * This function returns max channel TC allowed depends upon "driver_caps"
 */
static inline u32 ice_vc_get_max_chnl_tc_allowed(struct ice_vf *vf)
{
	if (vf->driver_caps & VIRTCHNL_VF_OFFLOAD_ADQ_V2)
		return VIRTCHNL_MAX_ADQ_V2_CHANNELS;
	else
		return VIRTCHNL_MAX_ADQ_CHANNELS;
}

/**
 * ice_vf_chnl_dmac_fltr_cnt - number of dmac based channel filters
 * @vf: pointer to the VF info
 */
static inline u16 ice_vf_chnl_dmac_fltr_cnt(struct ice_vf *vf)
{
	return vf->num_dmac_chnl_fltrs;
}


#ifdef CONFIG_PCI_IOV
void ice_dump_all_vfs(struct ice_pf *pf);
struct ice_vsi *ice_get_vf_vsi(struct ice_vf *vf);
void ice_process_vflr_event(struct ice_pf *pf);
int ice_sriov_configure(struct pci_dev *pdev, int num_vfs);
int ice_set_vf_mac(struct net_device *netdev, int vf_id, u8 *mac);
int
ice_get_vf_cfg(struct net_device *netdev, int vf_id, struct ifla_vf_info *ivi);

void ice_free_vfs(struct ice_pf *pf);
void ice_vc_process_vf_msg(struct ice_pf *pf, struct ice_rq_event_info *event);

/* VF configuration related iplink handlers */
void ice_vc_notify_link_state(struct ice_pf *pf);
void ice_vc_notify_reset(struct ice_pf *pf);
void ice_vc_notify_vf_link_state(struct ice_vf *vf);
void ice_vc_change_ops_to_repr(struct ice_vc_vf_ops *ops);
void ice_vc_set_dflt_vf_ops(struct ice_vc_vf_ops *ops);
bool ice_reset_all_vfs(struct ice_pf *pf, bool is_vflr);
bool ice_reset_vf(struct ice_vf *vf, bool is_vflr);
void ice_restore_all_vfs_msi_state(struct pci_dev *pdev);
bool
ice_is_malicious_vf(struct ice_pf *pf, struct ice_rq_event_info *event,
		    u16 num_msg_proc, u16 num_msg_pending);


#ifdef IFLA_VF_VLAN_INFO_MAX
int
ice_set_vf_port_vlan(struct net_device *netdev, int vf_id, u16 vlan_id, u8 qos,
		     __be16 vlan_proto);
#else
int
ice_set_vf_port_vlan(struct net_device *netdev, int vf_id, u16 vlan_id, u8 qos);
#endif

#ifdef HAVE_NDO_SET_VF_MIN_MAX_TX_RATE
int
ice_set_vf_bw(struct net_device *netdev, int vf_id, int min_tx_rate,
	      int max_tx_rate);
#else
int ice_set_vf_bw(struct net_device *netdev, int vf_id, int tx_rate);
#endif

int ice_set_vf_trust(struct net_device *netdev, int vf_id, bool trusted);

#ifdef HAVE_NDO_SET_VF_LINK_STATE
int ice_set_vf_link_state(struct net_device *netdev, int vf_id, int link_state);
#endif

int ice_check_vf_ready_for_cfg(struct ice_vf *vf);

int ice_set_vf_spoofchk(struct net_device *netdev, int vf_id, bool ena);

int ice_calc_vf_reg_idx(struct ice_vf *vf, struct ice_q_vector *q_vector,
			u8 tc);

void ice_set_vf_state_qs_dis(struct ice_vf *vf);
#ifdef HAVE_VF_STATS
int
ice_get_vf_stats(struct net_device *netdev, int vf_id,
		 struct ifla_vf_stats *vf_stats);
#endif /* HAVE_VF_STATS */
bool ice_is_any_vf_in_promisc(struct ice_pf *pf);
void
ice_vf_lan_overflow_event(struct ice_pf *pf, struct ice_rq_event_info *event);
void ice_print_vfs_mdd_events(struct ice_pf *pf);
void ice_print_vf_rx_mdd_event(struct ice_vf *vf);
enum ice_pkg_type ice_pkg_name_to_type(struct ice_hw *hw);
bool ice_vc_validate_pattern(struct ice_vf *vf,
			     struct virtchnl_proto_hdrs *proto);
struct ice_vsi *ice_vf_ctrl_vsi_setup(struct ice_vf *vf);
int
ice_vc_send_msg_to_vf(struct ice_vf *vf, u32 v_opcode,
		      enum virtchnl_status_code v_retval, u8 *msg, u16 msglen);
bool ice_vc_isvalid_vsi_id(struct ice_vf *vf, u16 vsi_id);
bool ice_vf_is_port_vlan_ena(struct ice_vf *vf);
#else /* CONFIG_PCI_IOV */
#if IS_ENABLED(CONFIG_NET_DEVLINK)
static inline struct ice_vsi *ice_get_vf_vsi(struct ice_vf *vf)
{
	return NULL;
}
#endif /* CONFIG_NET_DEVLINK */
static inline void ice_dump_all_vfs(struct ice_pf *pf) { }
static inline void ice_process_vflr_event(struct ice_pf *pf) { }
static inline void ice_free_vfs(struct ice_pf *pf) { }
static inline
void ice_vc_process_vf_msg(struct ice_pf *pf, struct ice_rq_event_info *event) { }
static inline void ice_vc_notify_link_state(struct ice_pf *pf) { }
static inline void ice_vc_notify_reset(struct ice_pf *pf) { }
static inline void ice_vc_notify_vf_link_state(struct ice_vf *vf) { }
static inline void ice_vc_change_ops_to_repr(struct ice_vc_vf_ops *ops) { }
static inline int ice_check_vf_ready_for_cfg(struct ice_vf *vf)
{
	return -EOPNOTSUPP;
}
static inline void ice_vc_set_dflt_vf_ops(struct ice_vc_vf_ops *ops) { }
static inline void ice_set_vf_state_qs_dis(struct ice_vf *vf) { }
static inline
void ice_vf_lan_overflow_event(struct ice_pf *pf, struct ice_rq_event_info *event) { }
static inline void ice_print_vfs_mdd_events(struct ice_pf *pf) { }
static inline void ice_print_vf_rx_mdd_event(struct ice_vf *vf) { }
static inline void ice_restore_all_vfs_msi_state(struct pci_dev *pdev) { }
static inline bool
ice_is_malicious_vf(struct ice_pf __always_unused *pf,
		    struct ice_rq_event_info __always_unused *event,
		    u16 __always_unused num_msg_proc,
		    u16 __always_unused num_msg_pending)
{
	return false;
}

static inline bool
ice_reset_all_vfs(struct ice_pf __always_unused *pf,
		  bool __always_unused is_vflr)
{
	return true;
}

static inline bool
ice_reset_vf(struct ice_vf __always_unused *vf, bool __always_unused is_vflr)
{
	return true;
}

static inline int
ice_sriov_configure(struct pci_dev __always_unused *pdev,
		    int __always_unused num_vfs)
{
	return -EOPNOTSUPP;
}

static inline int
ice_set_vf_mac(struct net_device __always_unused *netdev,
	       int __always_unused vf_id, u8 __always_unused *mac)
{
	return -EOPNOTSUPP;
}

static inline int
ice_get_vf_cfg(struct net_device __always_unused *netdev,
	       int __always_unused vf_id,
	       struct ifla_vf_info __always_unused *ivi)
{
	return -EOPNOTSUPP;
}

#ifdef HAVE_NDO_SET_VF_TRUST
static inline int
ice_set_vf_trust(struct net_device __always_unused *netdev,
		 int __always_unused vf_id, bool __always_unused trusted)
{
	return -EOPNOTSUPP;
}
#endif /* HAVE_NDO_SET_VF_TRUST */

#ifdef IFLA_VF_VLAN_INFO_MAX
static inline int
ice_set_vf_port_vlan(struct net_device __always_unused *netdev,
		     int __always_unused vf_id, u16 __always_unused vid,
		     u8 __always_unused qos, __be16 __always_unused v_proto)
{
	return -EOPNOTSUPP;
}
#else
static inline int
ice_set_vf_port_vlan(struct net_device __always_unused *netdev,
		     int __always_unused vf_id, u16 __always_unused vid,
		     u8 __always_unused qos)
{
	return -EOPNOTSUPP;
}
#endif /* IFLA_VF_VLAN_INFO_MAX */

static inline int
ice_set_vf_spoofchk(struct net_device __always_unused *netdev,
		    int __always_unused vf_id, bool __always_unused ena)
{
	return -EOPNOTSUPP;
}

#ifdef HAVE_NDO_SET_VF_LINK_STATE
static inline int
ice_set_vf_link_state(struct net_device __always_unused *netdev,
		      int __always_unused vf_id, int __always_unused link_state)
{
	return -EOPNOTSUPP;
}
#endif /* HAVE_NDO_SET_VF_LINK_STATE */

#ifdef HAVE_NDO_SET_VF_MIN_MAX_TX_RATE
static inline int
ice_set_vf_bw(struct net_device __always_unused *netdev,
	      int __always_unused vf_id, int __always_unused min_tx_rate,
	      int __always_unused max_tx_rate)
#else
static inline int
ice_set_vf_bw(struct net_device __always_unused *netdev,
	      int __always_unused vf_id, int __always_unused max_tx_rate)
#endif
{
	return -EOPNOTSUPP;
}

static inline int
ice_calc_vf_reg_idx(struct ice_vf __always_unused *vf,
		    struct ice_q_vector __always_unused *q_vector,
		    u8 __always_unused tc)
{
	return 0;
}

#ifdef HAVE_VF_STATS
static inline int
ice_get_vf_stats(struct net_device __always_unused *netdev,
		 int __always_unused vf_id,
		 struct ifla_vf_stats __always_unused *vf_stats)
{
	return -EOPNOTSUPP;
}
#endif /* HAVE_VF_STATS */

static inline bool ice_is_any_vf_in_promisc(struct ice_pf __always_unused *pf)
{
	return false;
}

static inline enum ice_pkg_type ice_pkg_name_to_type(struct ice_hw *hw)
{
	return ICE_PKG_TYPE_UNKNOWN;
}

static inline struct ice_vsi *
ice_vf_ctrl_vsi_setup(struct ice_vf __always_unused *vf)
{
	return NULL;
}

static inline int
ice_vc_send_msg_to_vf(struct ice_vf *vf, u32 v_opcode,
		      enum virtchnl_status_code v_retval, u8 *msg, u16 msglen)
{
	return 0;
}

static inline bool ice_vc_isvalid_vsi_id(struct ice_vf *vf, u16 vsi_id)
{
	return 0;
}
static inline bool ice_vf_is_port_vlan_ena(struct ice_vf __always_unused *vf)
{
	return false;
}
#endif /* CONFIG_PCI_IOV */
#endif /* _ICE_VIRTCHNL_PF_H_ */
