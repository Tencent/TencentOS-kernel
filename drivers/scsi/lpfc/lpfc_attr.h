/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017 Broadcom. All Rights Reserved. The term      *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.     *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
 * Portions Copyright (C) 2004-2005 Christoph Hellwig              *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#ifndef BUILD_BRCMFCOE

#define LPFC_ATTR(name, defval, minval, maxval, desc) \
static uint lpfc_##name = defval;\
module_param(lpfc_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_init(name, defval, minval, maxval)

#define LPFC_ATTR_R(name, defval, minval, maxval, desc) \
static uint lpfc_##name = defval;\
module_param(lpfc_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_show(name)\
lpfc_param_init(name, defval, minval, maxval)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO, lpfc_##name##_show, NULL)

#define LPFC_ATTR_RW(name, defval, minval, maxval, desc) \
static uint lpfc_##name = defval;\
module_param(lpfc_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_show(name)\
lpfc_param_init(name, defval, minval, maxval)\
lpfc_param_set(name, defval, minval, maxval)\
lpfc_param_store(name)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO | S_IWUSR,\
		   lpfc_##name##_show, lpfc_##name##_store)

#define LPFC_BBCR_ATTR_RW(name, defval, minval, maxval, desc) \
static uint lpfc_##name = defval;\
module_param(lpfc_##name, uint, 0444);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_show(name)\
lpfc_param_init(name, defval, minval, maxval)\
lpfc_param_store(name)\
static DEVICE_ATTR(lpfc_##name, 0444 | 0644,\
		   lpfc_##name##_show, lpfc_##name##_store)

#define LPFC_ATTR_HEX_R(name, defval, minval, maxval, desc) \
static uint lpfc_##name = defval;\
module_param(lpfc_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_hex_show(name)\
lpfc_param_init(name, defval, minval, maxval)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO, lpfc_##name##_show, NULL)

#define LPFC_ATTR_HEX_RW(name, defval, minval, maxval, desc) \
static uint lpfc_##name = defval;\
module_param(lpfc_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_hex_show(name)\
lpfc_param_init(name, defval, minval, maxval)\
lpfc_param_set(name, defval, minval, maxval)\
lpfc_param_store(name)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO | S_IWUSR,\
		   lpfc_##name##_show, lpfc_##name##_store)

#define LPFC_VPORT_ATTR(name, defval, minval, maxval, desc) \
static uint lpfc_##name = defval;\
module_param(lpfc_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_vport_param_init(name, defval, minval, maxval)

#define LPFC_VPORT_ATTR_R(name, defval, minval, maxval, desc) \
static uint lpfc_##name = defval;\
module_param(lpfc_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_vport_param_show(name)\
lpfc_vport_param_init(name, defval, minval, maxval)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO, lpfc_##name##_show, NULL)

#define LPFC_VPORT_ULL_ATTR_R(name, defval, minval, maxval, desc) \
static uint64_t lpfc_##name = defval;\
module_param(lpfc_##name, ullong, S_IRUGO);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_vport_param_show(name)\
lpfc_vport_param_init(name, defval, minval, maxval)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO, lpfc_##name##_show, NULL)

#define LPFC_VPORT_ATTR_RW(name, defval, minval, maxval, desc) \
static uint lpfc_##name = defval;\
module_param(lpfc_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_vport_param_show(name)\
lpfc_vport_param_init(name, defval, minval, maxval)\
lpfc_vport_param_set(name, defval, minval, maxval)\
lpfc_vport_param_store(name)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO | S_IWUSR,\
		   lpfc_##name##_show, lpfc_##name##_store)

#define LPFC_VPORT_ATTR_HEX_R(name, defval, minval, maxval, desc) \
static uint lpfc_##name = defval;\
module_param(lpfc_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_vport_param_hex_show(name)\
lpfc_vport_param_init(name, defval, minval, maxval)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO, lpfc_##name##_show, NULL)

#define LPFC_VPORT_ATTR_HEX_RW(name, defval, minval, maxval, desc) \
static uint lpfc_##name = defval;\
module_param(lpfc_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_vport_param_hex_show(name)\
lpfc_vport_param_init(name, defval, minval, maxval)\
lpfc_vport_param_set(name, defval, minval, maxval)\
lpfc_vport_param_store(name)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO | S_IWUSR,\
		   lpfc_##name##_show, lpfc_##name##_store)

#else

#define LPFC_ATTR(name, defval, minval, maxval, desc) \
static uint brcmfcoe_##name = defval;\
module_param(brcmfcoe_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(brcmfcoe_##name, desc);\
lpfc_param_init(name, defval, minval, maxval)

#define LPFC_ATTR_R(name, defval, minval, maxval, desc) \
static uint brcmfcoe_##name = defval;\
module_param(brcmfcoe_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(brcmfcoe_##name, desc);\
lpfc_param_show(name)\
lpfc_param_init(name, defval, minval, maxval)\
static DEVICE_ATTR(brcmfcoe_##name, S_IRUGO , lpfc_##name##_show, NULL)

#define LPFC_ATTR_RW(name, defval, minval, maxval, desc) \
static uint brcmfcoe_##name = defval;\
module_param(brcmfcoe_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(brcmfcoe_##name, desc);\
lpfc_param_show(name)\
lpfc_param_init(name, defval, minval, maxval)\
lpfc_param_set(name, defval, minval, maxval)\
lpfc_param_store(name)\
static DEVICE_ATTR(brcmfcoe_##name, S_IRUGO | S_IWUSR,\
		   lpfc_##name##_show, lpfc_##name##_store)

#define LPFC_BBCR_ATTR_RW(name, defval, minval, maxval, desc) \
static uint brcmfcoe_##name = defval;\
module_param(brcmfcoe_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(brcmfcoe_##name, desc);\
lpfc_param_show(name)\
lpfc_param_init(name, defval, minval, maxval)\
lpfc_param_store(name)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO | S_IWUSR,\
		   lpfc_##name##_show, lpfc_##name##_store)

#define LPFC_ATTR_HEX_R(name, defval, minval, maxval, desc) \
static uint brcmfcoe_##name = defval;\
module_param(brcmfcoe_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(brcmfcoe_##name, desc);\
lpfc_param_hex_show(name)\
lpfc_param_init(name, defval, minval, maxval)\
static DEVICE_ATTR(brcmfcoe_##name, S_IRUGO , lpfc_##name##_show, NULL)

#define LPFC_ATTR_HEX_RW(name, defval, minval, maxval, desc) \
static uint brcmfcoe_##name = defval;\
module_param(brcmfcoe_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(brcmfcoe_##name, desc);\
lpfc_param_hex_show(name)\
lpfc_param_init(name, defval, minval, maxval)\
lpfc_param_set(name, defval, minval, maxval)\
lpfc_param_store(name)\
static DEVICE_ATTR(brcmfcoe_##name, S_IRUGO | S_IWUSR,\
		   lpfc_##name##_show, lpfc_##name##_store)

#define LPFC_VPORT_ATTR(name, defval, minval, maxval, desc) \
static uint brcmfcoe_##name = defval;\
module_param(brcmfcoe_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(brcmfcoe_##name, desc);\
lpfc_vport_param_init(name, defval, minval, maxval)

#define LPFC_VPORT_ATTR_R(name, defval, minval, maxval, desc) \
static uint brcmfcoe_##name = defval;\
module_param(brcmfcoe_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(brcmfcoe_##name, desc);\
lpfc_vport_param_show(name)\
lpfc_vport_param_init(name, defval, minval, maxval)\
static DEVICE_ATTR(brcmfcoe_##name, S_IRUGO , lpfc_##name##_show, NULL)

#define LPFC_VPORT_ULL_ATTR_R(name, defval, minval, maxval, desc) \
static uint64_t brcmfcoe_##name = defval;\
module_param(brcmfcoe_##name, ullong, S_IRUGO);\
MODULE_PARM_DESC(brcmfcoe_##name, desc);\
lpfc_vport_param_show(name)\
lpfc_vport_param_init(name, defval, minval, maxval)\
static DEVICE_ATTR(brcmfcoe_##name, S_IRUGO , lpfc_##name##_show, NULL)

#define LPFC_VPORT_ATTR_RW(name, defval, minval, maxval, desc) \
static uint brcmfcoe_##name = defval;\
module_param(brcmfcoe_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(brcmfcoe_##name, desc);\
lpfc_vport_param_show(name)\
lpfc_vport_param_init(name, defval, minval, maxval)\
lpfc_vport_param_set(name, defval, minval, maxval)\
lpfc_vport_param_store(name)\
static DEVICE_ATTR(brcmfcoe_##name, S_IRUGO | S_IWUSR,\
		   lpfc_##name##_show, lpfc_##name##_store)

#define LPFC_VPORT_ATTR_HEX_R(name, defval, minval, maxval, desc) \
static uint brcmfcoe_##name = defval;\
module_param(brcmfcoe_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(brcmfcoe_##name, desc);\
lpfc_vport_param_hex_show(name)\
lpfc_vport_param_init(name, defval, minval, maxval)\
static DEVICE_ATTR(brcmfcoe_##name, S_IRUGO , lpfc_##name##_show, NULL)

#define LPFC_VPORT_ATTR_HEX_RW(name, defval, minval, maxval, desc) \
static uint brcmfcoe_##name = defval;\
module_param(brcmfcoe_##name, uint, S_IRUGO);\
MODULE_PARM_DESC(brcmfcoe_##name, desc);\
lpfc_vport_param_hex_show(name)\
lpfc_vport_param_init(name, defval, minval, maxval)\
lpfc_vport_param_set(name, defval, minval, maxval)\
lpfc_vport_param_store(name)\
static DEVICE_ATTR(brcmfcoe_##name, S_IRUGO | S_IWUSR,\
		   lpfc_##name##_show, lpfc_##name##_store)

#define	lpfc_drvr_version brcmfcoe_drvr_version
#define	lpfc_enable_fip brcmfcoe_enable_fip
#define	lpfc_temp_sensor brcmfcoe_temp_sensor
#define	lpfc_sriov_hw_max_virtfn brcmfcoe_sriov_hw_max_virtfn
#define	lpfc_xlane_supported brcmfcoe_xlane_supported

#define	dev_attr_lpfc_log_verbose dev_attr_brcmfcoe_log_verbose
#define	dev_attr_lpfc_lun_queue_depth dev_attr_brcmfcoe_lun_queue_depth
#define	dev_attr_lpfc_tgt_queue_depth dev_attr_brcmfcoe_tgt_queue_depth
#define	dev_attr_lpfc_hba_queue_depth dev_attr_brcmfcoe_hba_queue_depth
#define	dev_attr_lpfc_peer_port_login dev_attr_brcmfcoe_peer_port_login
#define	dev_attr_lpfc_fcp_class dev_attr_brcmfcoe_fcp_class
#define	dev_attr_lpfc_use_adisc dev_attr_brcmfcoe_use_adisc
#define	dev_attr_lpfc_first_burst_size dev_attr_brcmfcoe_first_burst_size
#define	dev_attr_lpfc_ack0 dev_attr_brcmfcoe_ack0
#define	dev_attr_lpfc_xri_rebalancing dev_attr_brcmfcoe_xri_rebalancing
#define	dev_attr_lpfc_scan_down dev_attr_brcmfcoe_scan_down
#define	dev_attr_lpfc_fcp_io_sched dev_attr_brcmfcoe_fcp_io_sched
#define	dev_attr_lpfc_ns_query dev_attr_brcmfcoe_ns_query
#define	dev_attr_lpfc_fcp2_no_tgt_reset dev_attr_brcmfcoe_fcp2_no_tgt_reset
#define	dev_attr_lpfc_cr_delay dev_attr_brcmfcoe_cr_delay
#define	dev_attr_lpfc_cr_count dev_attr_brcmfcoe_cr_count
#define	dev_attr_lpfc_multi_ring_support dev_attr_brcmfcoe_multi_ring_support
#define	dev_attr_lpfc_multi_ring_rctl dev_attr_brcmfcoe_multi_ring_rctl
#define	dev_attr_lpfc_multi_ring_type dev_attr_brcmfcoe_multi_ring_type
#define	dev_attr_lpfc_fdmi_on dev_attr_brcmfcoe_fdmi_on
#define	dev_attr_lpfc_enable_SmartSAN dev_attr_brcmfcoe_enable_SmartSAN
#define	dev_attr_lpfc_max_luns dev_attr_brcmfcoe_max_luns
#define	dev_attr_lpfc_fcf_failover_policy dev_attr_brcmfcoe_fcf_failover_policy
#define	dev_attr_lpfc_enable_fcp_priority dev_attr_brcmfcoe_enable_fcp_priority
#define	dev_attr_lpfc_poll_tmo dev_attr_brcmfcoe_poll_tmo
#define	dev_attr_lpfc_task_mgmt_tmo dev_attr_brcmfcoe_task_mgmt_tmo
#define	dev_attr_lpfc_enable_npiv dev_attr_brcmfcoe_enable_npiv
#define	dev_attr_lpfc_use_msi dev_attr_brcmfcoe_use_msi
#define	dev_attr_lpfc_cq_max_proc_limit dev_attr_brcmfcoe_cq_max_proc_limit
#define	dev_attr_lpfc_fcp_imax dev_attr_brcmfcoe_fcp_imax
#define	dev_attr_lpfc_cq_poll_threshold dev_attr_brcmfcoe_cq_poll_threshold
#define	dev_attr_lpfc_fcp_mq_threshold dev_attr_brcmfcoe_fcp_mq_threshold
#define	dev_attr_lpfc_hdw_queue dev_attr_brcmfcoe_hdw_queue
#define	dev_attr_lpfc_irq_chann dev_attr_brcmfcoe_irq_chann
#define	dev_attr_lpfc_enable_bg dev_attr_brcmfcoe_enable_bg
#define	dev_attr_lpfc_external_dif dev_attr_brcmfcoe_external_dif
#define	dev_attr_lpfc_enable_hba_reset dev_attr_brcmfcoe_enable_hba_reset
#define	dev_attr_lpfc_enable_hba_heartbeat \
	dev_attr_brcmfcoe_enable_hba_heartbeat
#define	dev_attr_lpfc_EnableXLane dev_attr_brcmfcoe_EnableXLane
#define	dev_attr_lpfc_XLanePriority dev_attr_brcmfcoe_XLanePriority
#define	dev_attr_lpfc_sg_seg_cnt dev_attr_brcmfcoe_sg_seg_cnt
#define	dev_attr_lpfc_enable_rrq dev_attr_brcmfcoe_enable_rrq
#define	dev_attr_lpfc_suppress_link_up dev_attr_brcmfcoe_suppress_link_up
#define	dev_attr_lpfc_throttle_log_cnt dev_attr_brcmfcoe_throttle_log_cnt
#define	dev_attr_lpfc_throttle_log_time dev_attr_brcmfcoe_throttle_log_time
#define	dev_attr_lpfc_enable_mds_diags dev_attr_brcmfcoe_enable_mds_diags
#define	lpfc_enable_bbcr brcmfcoe_enable_bbcr
#define	dev_attr_lpfc_enable_da_id dev_attr_brcmfcoe_enable_da_id
#define dev_attr_lpfc_suppress_rsp dev_attr_brcmfcoe_suppress_rsp
#define dev_attr_lpfc_enable_fc4_type dev_attr_brcmfcoe_enable_fc4_type
#define dev_attr_lpfc_xri_split dev_attr_brcmfcoe_xri_split
#define dev_attr_lpfc_ras_fwlog_buffsize dev_attr_brcmfcoe_ras_fwlog_buffsize
#define dev_attr_lpfc_ras_fwlog_level dev_attr_brcmfcoe_ras_fwlog_level
#define dev_attr_lpfc_ras_fwlog_func dev_attr_brcmfcoe_ras_fwlog_func

#define	lpfc_fcp_io_sched brcmfcoe_fcp_io_sched
#define	lpfc_ns_query brcmfcoe_ns_query
#define	lpfc_fcp2_no_tgt_reset brcmfcoe_fcp2_no_tgt_reset
#define	lpfc_cr_delay brcmfcoe_cr_delay
#define	lpfc_cr_count brcmfcoe_cr_count
#define	lpfc_multi_ring_support brcmfcoe_multi_ring_support
#define	lpfc_multi_ring_rctl brcmfcoe_multi_ring_rctl
#define	lpfc_multi_ring_type brcmfcoe_multi_ring_type
#define	lpfc_ack0 brcmfcoe_ack0
#define lpfc_xri_rebalancing brcmfcoe_xri_rebalancing
#define	lpfc_topology brcmfcoe_topology
#define	lpfc_link_speed brcmfcoe_link_speed
#define	lpfc_poll_tmo brcmfcoe_poll_tmo
#define	lpfc_task_mgmt_tmo brcmfcoe_task_mgmt_tmo
#define	lpfc_enable_npiv brcmfcoe_enable_npiv
#define	lpfc_fcf_failover_policy brcmfcoe_fcf_failover_policy
#define	lpfc_enable_rrq brcmfcoe_enable_rrq
#define	lpfc_enable_fcp_priority brcmfcoe_enable_fcp_priority
#define	lpfc_fdmi_on brcmfcoe_fdmi_on
#define	lpfc_enable_SmartSAN brcmfcoe_enable_SmartSAN
#define	lpfc_use_msi brcmfcoe_use_msi
#define	lpfc_cq_max_proc_limit brcmfcoe_cq_max_proc_limit
#define	lpfc_fcp_imax brcmfcoe_fcp_imax
#define	lpfc_cq_poll_threshold brcmfcoe_cq_poll_threshold
#define	lpfc_fcp_cpu_map brcmfcoe_fcp_cpu_map
#define	lpfc_fcp_mq_threshold brcmfcoe_fcp_mq_threshold
#define	lpfc_hdw_queue brcmfcoe_hdw_queue
#define	lpfc_irq_chann brcmfcoe_irq_chann
#define	lpfc_enable_hba_reset brcmfcoe_enable_hba_reset
#define	lpfc_enable_hba_heartbeat brcmfcoe_enable_hba_heartbeat
#define	lpfc_EnableXLane brcmfcoe_EnableXLane
#define	lpfc_XLanePriority brcmfcoe_XLanePriority
#define	lpfc_enable_bg brcmfcoe_enable_bg
#define	lpfc_prot_mask brcmfcoe_prot_mask
#define	lpfc_prot_guard brcmfcoe_prot_guard
#define	lpfc_external_dif brcmfcoe_external_dif
#define	lpfc_sg_seg_cnt brcmfcoe_sg_seg_cnt
#define	lpfc_hba_queue_depth brcmfcoe_hba_queue_depth
#define	lpfc_log_verbose brcmfcoe_log_verbose
#define	lpfc_aer_support brcmfcoe_aer_support
#define	lpfc_sriov_nr_virtfn brcmfcoe_sriov_nr_virtfn
#define	lpfc_req_fw_upgrade brcmfcoe_req_fw_upgrade
#define	lpfc_suppress_link_up brcmfcoe_suppress_link_up
#define	lpfc_throttle_log_cnt brcmfcoe_throttle_log_cnt
#define	lpfc_throttle_log_time brcmfcoe_throttle_log_time
#define	lpfc_enable_mds_diags brcmfcoe_enable_mds_diags
#define	lpfc_lun_queue_depth brcmfcoe_lun_queue_depth
#define	lpfc_tgt_queue_depth brcmfcoe_tgt_queue_depth
#define	lpfc_peer_port_login brcmfcoe_peer_port_login
#define	lpfc_fcp_class brcmfcoe_fcp_class
#define	lpfc_use_adisc brcmfcoe_use_adisc
#define	lpfc_first_burst_size brcmfcoe_first_burst_size
#define	lpfc_discovery_threads brcmfcoe_discovery_threads
#define	lpfc_max_luns brcmfcoe_max_luns
#define	lpfc_scan_down brcmfcoe_scan_down
#define	lpfc_enable_da_id brcmfcoe_enable_da_id
#define	lpfc_sli_mode brcmfcoe_sli_mode
#define	lpfc_delay_discovery brcmfcoe_delay_discovery
#define	lpfc_devloss_tmo brcmfcoe_devloss_tmo
#define	lpfc_drvr_stat_data brcmfcoe_drvr_stat_data
#define	lpfc_max_scsicmpl_time brcmfcoe_max_scsicmpl_time
#define	lpfc_nodev_tmo brcmfcoe_nodev_tmo
#define	lpfc_poll brcmfcoe_poll
#define	lpfc_stat_data_ctrl brcmfcoe_stat_data_ctrl
#define	lpfc_xlane_lun brcmfcoe_xlane_lun
#define	lpfc_xlane_lun_state brcmfcoe_xlane_lun_state
#define	lpfc_xlane_lun_status brcmfcoe_xlane_lun_status
#define	lpfc_xlane_priority brcmfcoe_xlane_priority
#define	lpfc_xlane_tgt brcmfcoe_xlane_tgt
#define	lpfc_xlane_vpt brcmfcoe_xlane_vpt
#define lpfc_enable_fc4_type brcmfcoe_enable_fc4_type
#define lpfc_xri_split brcmfcoe_xri_split
#define lpfc_suppress_rsp brcmfcoe_suppress_rsp
#define lpfc_ras_fwlog_buffsize brcmfcoe_ras_fwlog_buffsize
#define lpfc_ras_fwlog_level brcmfcoe_ras_fwlog_level
#define lpfc_ras_fwlog_func brcmfcoe_ras_fwlog_func 
#endif
