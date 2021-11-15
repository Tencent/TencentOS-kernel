/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2021, Intel Corporation. */

#ifndef _ICE_COMMON_H_
#define _ICE_COMMON_H_

#include "ice.h"
#include "ice_type.h"
#include "ice_nvm.h"
#include "ice_flex_pipe.h"
#include "virtchnl.h"
#include "ice_switch.h"
#include "ice_fdir.h"

#define ICE_SQ_SEND_DELAY_TIME_MS	10
#define ICE_SQ_SEND_MAX_EXECUTE		3

enum ice_fw_modes {
	ICE_FW_MODE_NORMAL,
	ICE_FW_MODE_DBG,
	ICE_FW_MODE_REC,
	ICE_FW_MODE_ROLLBACK
};


void ice_set_umac_shared(struct ice_hw *hw);
enum ice_status ice_init_hw(struct ice_hw *hw);
void ice_deinit_hw(struct ice_hw *hw);
enum ice_status ice_check_reset(struct ice_hw *hw);
enum ice_status ice_reset(struct ice_hw *hw, enum ice_reset_req req);

enum ice_status ice_create_all_ctrlq(struct ice_hw *hw);
enum ice_status ice_init_all_ctrlq(struct ice_hw *hw);
void ice_shutdown_all_ctrlq(struct ice_hw *hw);
void ice_destroy_all_ctrlq(struct ice_hw *hw);
enum ice_status
ice_clean_rq_elem(struct ice_hw *hw, struct ice_ctl_q_info *cq,
		  struct ice_rq_event_info *e, u16 *pending);
enum ice_status
ice_get_link_status(struct ice_port_info *pi, bool *link_up);
enum ice_status ice_update_link_info(struct ice_port_info *pi);
enum ice_status
ice_acquire_res(struct ice_hw *hw, enum ice_aq_res_ids res,
		enum ice_aq_res_access_type access, u32 timeout);
void ice_release_res(struct ice_hw *hw, enum ice_aq_res_ids res);
enum ice_status
ice_alloc_hw_res(struct ice_hw *hw, u16 type, u16 num, bool btm, u16 *res);
enum ice_status
ice_free_hw_res(struct ice_hw *hw, u16 type, u16 num, u16 *res);
enum ice_status
ice_aq_alloc_free_res(struct ice_hw *hw, u16 num_entries,
		      struct ice_aqc_alloc_free_res_elem *buf, u16 buf_size,
		      enum ice_adminq_opc opc, struct ice_sq_cd *cd);
enum ice_status
ice_sq_send_cmd_nolock(struct ice_hw *hw, struct ice_ctl_q_info *cq,
		       struct ice_aq_desc *desc, void *buf, u16 buf_size,
		       struct ice_sq_cd *cd);
enum ice_status
ice_sq_send_cmd(struct ice_hw *hw, struct ice_ctl_q_info *cq,
		struct ice_aq_desc *desc, void *buf, u16 buf_size,
		struct ice_sq_cd *cd);
void ice_clear_pxe_mode(struct ice_hw *hw);

enum ice_status ice_get_caps(struct ice_hw *hw);

void ice_set_safe_mode_caps(struct ice_hw *hw);





enum ice_status
ice_write_rxq_ctx(struct ice_hw *hw, struct ice_rlan_ctx *rlan_ctx,
		  u32 rxq_index);
enum ice_status ice_clear_rxq_ctx(struct ice_hw *hw, u32 rxq_index);
enum ice_status
ice_clear_tx_cmpltnq_ctx(struct ice_hw *hw, u32 tx_cmpltnq_index);
enum ice_status
ice_write_tx_cmpltnq_ctx(struct ice_hw *hw,
			 struct ice_tx_cmpltnq_ctx *tx_cmpltnq_ctx,
			 u32 tx_cmpltnq_index);
enum ice_status
ice_clear_tx_drbell_q_ctx(struct ice_hw *hw, u32 tx_drbell_q_index);
enum ice_status
ice_write_tx_drbell_q_ctx(struct ice_hw *hw,
			  struct ice_tx_drbell_q_ctx *tx_drbell_q_ctx,
			  u32 tx_drbell_q_index);

enum ice_status
ice_aq_get_rss_lut(struct ice_hw *hw, struct ice_aq_get_set_rss_lut_params *get_params);
enum ice_status
ice_aq_set_rss_lut(struct ice_hw *hw, struct ice_aq_get_set_rss_lut_params *set_params);
enum ice_status
ice_aq_get_rss_key(struct ice_hw *hw, u16 vsi_handle,
		   struct ice_aqc_get_set_rss_keys *keys);
enum ice_status
ice_aq_set_rss_key(struct ice_hw *hw, u16 vsi_handle,
		   struct ice_aqc_get_set_rss_keys *keys);
enum ice_status
ice_aq_move_recfg_lan_txq(struct ice_hw *hw, u8 num_qs, bool is_move,
			  bool is_tc_change, bool subseq_call, bool flush_pipe,
			  u8 timeout, u32 *blocked_cgds,
			  struct ice_aqc_move_txqs_data *buf, u16 buf_size,
			  u8 *txqs_moved, struct ice_sq_cd *cd);

bool ice_check_sq_alive(struct ice_hw *hw, struct ice_ctl_q_info *cq);
enum ice_status ice_aq_q_shutdown(struct ice_hw *hw, bool unloading);
void ice_fill_dflt_direct_cmd_desc(struct ice_aq_desc *desc, u16 opcode);
extern const struct ice_ctx_ele ice_tlan_ctx_info[];
enum ice_status
ice_set_ctx(struct ice_hw *hw, u8 *src_ctx, u8 *dest_ctx,
	    const struct ice_ctx_ele *ce_info);

extern struct mutex ice_global_cfg_lock_sw;

enum ice_status
ice_aq_send_cmd(struct ice_hw *hw, struct ice_aq_desc *desc,
		void *buf, u16 buf_size, struct ice_sq_cd *cd);
enum ice_status ice_aq_get_fw_ver(struct ice_hw *hw, struct ice_sq_cd *cd);

enum ice_status
ice_aq_send_driver_ver(struct ice_hw *hw, struct ice_driver_ver *dv,
		       struct ice_sq_cd *cd);
enum ice_status
ice_aq_set_port_params(struct ice_port_info *pi, u16 bad_frame_vsi,
		       bool save_bad_pac, bool pad_short_pac, bool double_vlan,
		       struct ice_sq_cd *cd);
enum ice_status
ice_aq_get_phy_caps(struct ice_port_info *pi, bool qual_mods, u8 report_mode,
		    struct ice_aqc_get_phy_caps_data *caps,
		    struct ice_sq_cd *cd);
enum ice_status
ice_aq_list_caps(struct ice_hw *hw, void *buf, u16 buf_size, u32 *cap_count,
		 enum ice_adminq_opc opc, struct ice_sq_cd *cd);
enum ice_status
ice_discover_dev_caps(struct ice_hw *hw, struct ice_hw_dev_caps *dev_caps);
void
ice_update_phy_type(u64 *phy_type_low, u64 *phy_type_high,
		    u16 link_speeds_bitmap);
enum ice_status
ice_aq_manage_mac_write(struct ice_hw *hw, const u8 *mac_addr, u8 flags,
			struct ice_sq_cd *cd);

enum ice_status ice_clear_pf_cfg(struct ice_hw *hw);
enum ice_status
ice_aq_set_phy_cfg(struct ice_hw *hw, struct ice_port_info *pi,
		   struct ice_aqc_set_phy_cfg_data *cfg, struct ice_sq_cd *cd);
bool ice_fw_supports_link_override(struct ice_hw *hw);
enum ice_status
ice_get_link_default_override(struct ice_link_default_override_tlv *ldo,
			      struct ice_port_info *pi);
bool ice_is_phy_caps_an_enabled(struct ice_aqc_get_phy_caps_data *caps);

enum ice_fc_mode ice_caps_to_fc_mode(u8 caps);
enum ice_fec_mode ice_caps_to_fec_mode(u8 caps, u8 fec_options);
enum ice_status
ice_set_fc(struct ice_port_info *pi, u8 *aq_failures,
	   bool ena_auto_link_update);
enum ice_status
ice_cfg_phy_fc(struct ice_port_info *pi, struct ice_aqc_set_phy_cfg_data *cfg,
	       enum ice_fc_mode req_mode);
bool
ice_phy_caps_equals_cfg(struct ice_aqc_get_phy_caps_data *caps,
			struct ice_aqc_set_phy_cfg_data *cfg);
void
ice_copy_phy_caps_to_cfg(struct ice_port_info *pi,
			 struct ice_aqc_get_phy_caps_data *caps,
			 struct ice_aqc_set_phy_cfg_data *cfg);
enum ice_status
ice_cfg_phy_fec(struct ice_port_info *pi, struct ice_aqc_set_phy_cfg_data *cfg,
		enum ice_fec_mode fec);
enum ice_status
ice_aq_set_link_restart_an(struct ice_port_info *pi, bool ena_link,
			   struct ice_sq_cd *cd);
enum ice_status
ice_aq_set_mac_cfg(struct ice_hw *hw, u16 max_frame_size, struct ice_sq_cd *cd);
enum ice_status
ice_aq_get_link_info(struct ice_port_info *pi, bool ena_lse,
		     struct ice_link_status *link, struct ice_sq_cd *cd);
enum ice_status
ice_aq_set_event_mask(struct ice_hw *hw, u8 port_num, u16 mask,
		      struct ice_sq_cd *cd);
enum ice_status
ice_aq_set_mac_loopback(struct ice_hw *hw, bool ena_lpbk, struct ice_sq_cd *cd);


enum ice_status
ice_aq_set_port_id_led(struct ice_port_info *pi, bool is_orig_mode,
		       struct ice_sq_cd *cd);
enum ice_status
ice_aq_sff_eeprom(struct ice_hw *hw, u16 lport, u8 bus_addr,
		  u16 mem_addr, u8 page, u8 set_page, u8 *data, u8 length,
		  bool write, struct ice_sq_cd *cd);

enum ice_status
ice_aq_prog_topo_dev_nvm(struct ice_hw *hw,
			 struct ice_aqc_link_topo_params *topo_params,
			 struct ice_sq_cd *cd);
enum ice_status
ice_aq_read_topo_dev_nvm(struct ice_hw *hw,
			 struct ice_aqc_link_topo_params *topo_params,
			 u32 start_address, u8 *buf, u8 buf_size,
			 struct ice_sq_cd *cd);


void ice_dump_port_info(struct ice_port_info *pi);
void ice_dump_caps(struct ice_hw *hw);
void ice_dump_ptp_dev_caps(struct ice_hw *hw);
void ice_dump_ptp_func_caps(struct ice_hw *hw);
enum ice_status ice_dump_port_dflt_topo(struct ice_port_info *pi);
void ice_dump_port_topo(struct ice_port_info *pi);

enum ice_status
ice_aq_get_port_options(struct ice_hw *hw,
			struct ice_aqc_get_port_options_elem *options,
			u8 *option_count, u8 lport, bool lport_valid,
			u8 *active_option_idx, bool *active_option_valid);
enum ice_status
ice_cfg_vsi_rdma(struct ice_port_info *pi, u16 vsi_handle, u16 tc_bitmap,
		 u16 *max_rdmaqs);
enum ice_status
ice_ena_vsi_rdma_qset(struct ice_port_info *pi, u16 vsi_handle, u8 tc,
		      u16 *rdma_qset, u16 num_qsets, u32 *qset_teid);
enum ice_status
ice_dis_vsi_rdma_qset(struct ice_port_info *pi, u16 count, u32 *qset_teid,
		      u16 *q_id);
enum ice_status
ice_dis_vsi_txq(struct ice_port_info *pi, u16 vsi_handle, u8 tc, u8 num_queues,
		u16 *q_handle, u16 *q_ids, u32 *q_teids,
		enum ice_disq_rst_src rst_src, u16 vmvf_num,
		struct ice_sq_cd *cd);
enum ice_status
ice_cfg_vsi_lan(struct ice_port_info *pi, u16 vsi_handle, u16 tc_bitmap,
		u16 *max_lanqs);
enum ice_status
ice_ena_vsi_txq(struct ice_port_info *pi, u16 vsi_handle, u8 tc, u16 q_handle,
		u8 num_qgrps, struct ice_aqc_add_tx_qgrp *buf, u16 buf_size,
		struct ice_sq_cd *cd);
enum ice_status ice_replay_vsi(struct ice_hw *hw, u16 vsi_handle);
void ice_replay_post(struct ice_hw *hw);
struct ice_q_ctx *
ice_get_lan_q_ctx(struct ice_hw *hw, u16 vsi_handle, u8 tc, u16 q_handle);
enum ice_status ice_sbq_rw_reg_lp(struct ice_hw *hw,
				  struct ice_sbq_msg_input *in, bool lock);
void ice_sbq_lock(struct ice_hw *hw);
void ice_sbq_unlock(struct ice_hw *hw);
enum ice_status ice_sbq_rw_reg(struct ice_hw *hw, struct ice_sbq_msg_input *in);
void
ice_stat_update40(struct ice_hw *hw, u32 reg, bool prev_stat_loaded,
		  u64 *prev_stat, u64 *cur_stat);
void
ice_stat_update32(struct ice_hw *hw, u32 reg, bool prev_stat_loaded,
		  u64 *prev_stat, u64 *cur_stat);
enum ice_fw_modes ice_get_fw_mode(struct ice_hw *hw);
void ice_print_rollback_msg(struct ice_hw *hw);
bool ice_is_generic_mac(struct ice_hw *hw);
bool ice_is_e810(struct ice_hw *hw);
enum ice_status
ice_sched_query_elem(struct ice_hw *hw, u32 node_teid,
		     struct ice_aqc_txsched_elem_data *buf);
enum ice_status
ice_aq_set_driver_param(struct ice_hw *hw, enum ice_aqc_driver_params idx,
			u32 value, struct ice_sq_cd *cd);
enum ice_status
ice_aq_get_driver_param(struct ice_hw *hw, enum ice_aqc_driver_params idx,
			u32 *value, struct ice_sq_cd *cd);
enum ice_status
ice_aq_set_gpio(struct ice_hw *hw, u16 gpio_ctrl_handle, u8 pin_idx, bool value,
		struct ice_sq_cd *cd);
enum ice_status
ice_aq_get_gpio(struct ice_hw *hw, u16 gpio_ctrl_handle, u8 pin_idx,
		bool *value, struct ice_sq_cd *cd);
enum ice_status
ice_aq_set_lldp_mib(struct ice_hw *hw, u8 mib_type, void *buf, u16 buf_size,
		    struct ice_sq_cd *cd);
bool ice_fw_supports_lldp_fltr_ctrl(struct ice_hw *hw);
enum ice_status
ice_lldp_fltr_add_remove(struct ice_hw *hw, u16 vsi_num, bool add);
enum ice_status
ice_aq_read_i2c(struct ice_hw *hw, struct ice_aqc_link_topo_addr topo_addr,
		u16 bus_addr, __le16 addr, u8 params, u8 *data,
		struct ice_sq_cd *cd);
enum ice_status
ice_aq_write_i2c(struct ice_hw *hw, struct ice_aqc_link_topo_addr topo_addr,
		 u16 bus_addr, __le16 addr, u8 params, u8 *data,
		 struct ice_sq_cd *cd);
enum ice_status
ice_aq_set_health_status_config(struct ice_hw *hw, u8 event_source,
				struct ice_sq_cd *cd);
bool ice_is_fw_health_report_supported(struct ice_hw *hw);
bool ice_fw_supports_report_dflt_cfg(struct ice_hw *hw);

/* E810T PCA9575 IO controller registers */
#define ICE_PCA9575_P0_IN	0x0
#define ICE_PCA9575_P1_IN	0x1
#define ICE_PCA9575_P0_CFG	0x8
#define ICE_PCA9575_P1_CFG	0x9
#define ICE_PCA9575_P0_OUT	0xA
#define ICE_PCA9575_P1_OUT	0xB

/* E810T PCA9575 IO controller pin control */
#define ICE_E810T_P0_GNSS_PRSNT_N	BIT(4)
#define ICE_E810T_P1_SMA1_DIR_EN	BIT(4)
#define ICE_E810T_P1_SMA1_TX_EN		BIT(5)
#define ICE_E810T_P1_SMA2_UFL2_RX_DIS	BIT(3)
#define ICE_E810T_P1_SMA2_DIR_EN	BIT(6)
#define ICE_E810T_P1_SMA2_TX_EN		BIT(7)

enum ice_status
ice_read_e810t_pca9575_reg(struct ice_hw *hw, u8 offset, u8 *data);
enum ice_status
ice_write_e810t_pca9575_reg(struct ice_hw *hw, u8 offset, u8 data);
bool ice_e810t_is_pca9575_present(struct ice_hw *hw);
#endif /* _ICE_COMMON_H_ */
