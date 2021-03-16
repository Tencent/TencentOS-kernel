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

#ifndef HINIC_HW_H_
#define HINIC_HW_H_

#ifndef __BIG_ENDIAN__
#define __BIG_ENDIAN__    0x4321
#endif

#ifndef __LITTLE_ENDIAN__
#define __LITTLE_ENDIAN__    0x1234
#endif

#ifdef __BYTE_ORDER__
#undef __BYTE_ORDER__
#endif
/* X86 */
#define __BYTE_ORDER__    __LITTLE_ENDIAN__

enum hinic_mod_type {
	HINIC_MOD_COMM = 0,	/* HW communication module */
	HINIC_MOD_L2NIC = 1,	/* L2NIC module */
	HINIC_MOD_ROCE = 2,
	HINIC_MOD_IWARP = 3,
	HINIC_MOD_TOE = 4,
	HINIC_MOD_FLR = 5,
	HINIC_MOD_FCOE = 6,
	HINIC_MOD_CFGM = 7,	/* Configuration module */
	HINIC_MOD_CQM = 8,
	HINIC_MOD_VSWITCH = 9,
	HINIC_MOD_FC = 10,
	HINIC_MOD_OVS = 11,
	HINIC_MOD_FIC = 12,
	HINIC_MOD_MIGRATE = 13,
	HINIC_MOD_HILINK = 14,
	HINIC_MOD_HW_MAX = 16,	/* hardware max module id */

	/* Software module id, for PF/VF and multi-host */
	HINIC_MOD_SW_FUNC = 17,
	HINIC_MOD_MAX,
};

struct hinic_cmd_buf {
	void		*buf;
	dma_addr_t	dma_addr;
	u16		size;
};

enum hinic_ack_type {
	HINIC_ACK_TYPE_CMDQ,
	HINIC_ACK_TYPE_SHARE_CQN,
	HINIC_ACK_TYPE_APP_CQN,

	HINIC_MOD_ACK_MAX = 15,

};

#define HINIC_MGMT_CMD_UNSUPPORTED	0xFF

int hinic_msg_to_mgmt_sync(void *hwdev, enum hinic_mod_type mod, u8 cmd,
			   void *buf_in, u16 in_size,
			   void *buf_out, u16 *out_size, u32 timeout);

/* for pxe, ovs */
int hinic_msg_to_mgmt_poll_sync(void *hwdev, enum hinic_mod_type mod, u8 cmd,
				void *buf_in, u16 in_size,
				void *buf_out, u16 *out_size, u32 timeout);

/* PF/VF send msg to uP by api cmd, and return immediately */
int hinic_msg_to_mgmt_async(void *hwdev, enum hinic_mod_type mod, u8 cmd,
			    void *buf_in, u16 in_size);

int hinic_mbox_to_vf(void *hwdev, enum hinic_mod_type mod,
		     u16 vf_id, u8 cmd, void *buf_in, u16 in_size,
		     void *buf_out, u16 *out_size, u32 timeout);

int hinic_api_cmd_write_nack(void *hwdev, u8 dest,
			     void *cmd, u16 size);

int hinic_api_cmd_read_ack(void *hwdev, u8 dest,
			   void *cmd, u16 size, void *ack, u16 ack_size);
/* PF/VF send cmd to ucode by cmdq, and return if success.
 * timeout=0, use default timeout.
 */
int hinic_cmdq_direct_resp(void *hwdev, enum hinic_ack_type ack_type,
			   enum hinic_mod_type mod, u8 cmd,
			   struct hinic_cmd_buf *buf_in,
			   u64 *out_param, u32 timeout);
/* 1. whether need the timeout parameter
 * 2. out_param indicates the status of the microcode processing command
 */

/* PF/VF send cmd to ucode by cmdq, and return detailed result.
 * timeout=0, use default timeout.
 */
int hinic_cmdq_detail_resp(void *hwdev, enum hinic_ack_type ack_type,
			   enum hinic_mod_type mod, u8 cmd,
			   struct hinic_cmd_buf *buf_in,
			   struct hinic_cmd_buf *buf_out, u32 timeout);

/* PF/VF send cmd to ucode by cmdq, and return immediately */
int hinic_cmdq_async(void *hwdev, enum hinic_ack_type ack_type,
		     enum hinic_mod_type mod, u8 cmd,
		     struct hinic_cmd_buf *buf_in);

int hinic_ppf_tmr_start(void *hwdev);
int hinic_ppf_tmr_stop(void *hwdev);

/* CLP */
int hinic_clp_to_mgmt(void *hwdev, enum hinic_mod_type mod, u8 cmd,
		      void *buf_in, u16 in_size,
		      void *buf_out, u16 *out_size);


enum hinic_ceq_event {
	HINIC_NON_L2NIC_SCQ,
	HINIC_NON_L2NIC_ECQ,
	HINIC_NON_L2NIC_NO_CQ_EQ,
	HINIC_CMDQ,
	HINIC_L2NIC_SQ,
	HINIC_L2NIC_RQ,
	HINIC_MAX_CEQ_EVENTS,
};

typedef void (*hinic_ceq_event_cb)(void *handle, u32 ceqe_data);
int hinic_ceq_register_cb(void *hwdev, enum hinic_ceq_event event,
			  hinic_ceq_event_cb callback);
void hinic_ceq_unregister_cb(void *hwdev, enum hinic_ceq_event event);

enum hinic_aeq_type {
	HINIC_HW_INTER_INT = 0,
	HINIC_MBX_FROM_FUNC = 1,
	HINIC_MSG_FROM_MGMT_CPU = 2,
	HINIC_API_RSP = 3,
	HINIC_API_CHAIN_STS = 4,
	HINIC_MBX_SEND_RSLT = 5,
	HINIC_MAX_AEQ_EVENTS
};

enum hinic_aeq_sw_type {
	HINIC_STATELESS_EVENT = 0,
	HINIC_STATEFULL_EVENT = 1,
	HINIC_MAX_AEQ_SW_EVENTS
};

typedef void (*hinic_aeq_hwe_cb)(void *handle, u8 *data, u8 size);
int hinic_aeq_register_hw_cb(void *hwdev, enum hinic_aeq_type event,
			     hinic_aeq_hwe_cb hwe_cb);
void hinic_aeq_unregister_hw_cb(void *hwdev, enum hinic_aeq_type event);

typedef u8 (*hinic_aeq_swe_cb)(void *handle, u8 event, u64 data);
int hinic_aeq_register_swe_cb(void *hwdev, enum hinic_aeq_sw_type event,
			      hinic_aeq_swe_cb aeq_swe_cb);
void hinic_aeq_unregister_swe_cb(void *hwdev, enum hinic_aeq_sw_type event);

typedef void (*hinic_mgmt_msg_cb)(void *hwdev, void *pri_handle,
	u8 cmd, void *buf_in, u16 in_size, void *buf_out, u16 *out_size);

int hinic_register_mgmt_msg_cb(void *hwdev,
			       enum hinic_mod_type mod, void *pri_handle,
			       hinic_mgmt_msg_cb callback);
void hinic_unregister_mgmt_msg_cb(void *hwdev, enum hinic_mod_type mod);

struct hinic_cmd_buf *hinic_alloc_cmd_buf(void *hwdev);
void hinic_free_cmd_buf(void *hwdev, struct hinic_cmd_buf *buf);

int hinic_alloc_db_phy_addr(void *hwdev, u64 *db_base, u64 *dwqe_base);
void hinic_free_db_phy_addr(void *hwdev, u64 db_base, u64 dwqe_base);
int hinic_alloc_db_addr(void *hwdev, void __iomem **db_base,
			void __iomem **dwqe_base);
void hinic_free_db_addr(void *hwdev, void __iomem *db_base,
			void __iomem *dwqe_base);

struct nic_interrupt_info {
	u32 lli_set;
	u32 interrupt_coalesc_set;
	u16 msix_index;
	u8 lli_credit_limit;
	u8 lli_timer_cfg;
	u8 pending_limt;
	u8 coalesc_timer_cfg;
	u8 resend_timer_cfg;
};

int hinic_get_interrupt_cfg(void *hwdev,
			    struct nic_interrupt_info *interrupt_info);
int hinic_set_interrupt_cfg_direct(void *hwdev,
				   struct nic_interrupt_info *interrupt_info);
int hinic_set_interrupt_cfg(void *hwdev,
			    struct nic_interrupt_info interrupt_info);

/* The driver code implementation interface */
void hinic_misx_intr_clear_resend_bit(void *hwdev,
				      u16 msix_idx, u8 clear_resend_en);

struct hinic_sq_attr {
	u8 dma_attr_off;
	u8 pending_limit;
	u8 coalescing_time;
	u8 intr_en;
	u16 intr_idx;
	u32 l2nic_sqn;
	u64 ci_dma_base;
};

int hinic_set_ci_table(void *hwdev, u16 q_id, struct hinic_sq_attr *attr);

int hinic_set_root_ctxt(void *hwdev, u16 rq_depth, u16 sq_depth, int rx_buf_sz);
int hinic_clean_root_ctxt(void *hwdev);
void hinic_record_pcie_error(void *hwdev);

int hinic_func_rx_tx_flush(void *hwdev);

int hinic_func_tmr_bitmap_set(void *hwdev, bool enable);

struct hinic_init_para {
	/* Record hinic_pcidev or NDIS_Adapter pointer address */
	void *adapter_hdl;
	/* Record pcidev or Handler pointer address
	 * for example: ioremap interface input parameter
	 */
	void *pcidev_hdl;
	/* Record pcidev->dev or Handler pointer address which used to
	 * dma address application or dev_err print the parameter
	 */
	void *dev_hdl;

	void *cfg_reg_base;	/* Configure virtual address, bar0/1 */
	/* interrupt configuration register address, bar2/3 */
	void *intr_reg_base;
	u64 db_base_phy;
	void *db_base;	/* the doorbell address, bar4/5 higher 4M space */
	void *dwqe_mapping; /* direct wqe 4M, follow the doorbell address */
	void **hwdev;
	void *chip_node;
	/* In bmgw x86 host, driver can't send message to mgmt cpu directly,
	 * need to trasmit message ppf mbox to bmgw arm host.
	 */
	void *ppf_hwdev;
};

#ifndef IFNAMSIZ
#define IFNAMSIZ    16
#endif
#define MAX_FUNCTION_NUM 512
#define HINIC_MAX_PF_NUM 16
#define HINIC_MAX_COS	8
#define INIT_FAILED 0
#define INIT_SUCCESS 1
#define MAX_DRV_BUF_SIZE 4096

struct hinic_cmd_get_light_module_abs {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u8 port_id;
	u8 abs_status; /* 0:present, 1:absent */
	u8 rsv[2];
};

#define MODULE_TYPE_SFP		0x3
#define MODULE_TYPE_QSFP28	0x11
#define MODULE_TYPE_QSFP	0x0C
#define MODULE_TYPE_QSFP_PLUS	0x0D

#define SFP_INFO_MAX_SIZE	512
struct hinic_cmd_get_sfp_qsfp_info {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u8 port_id;
	u8 wire_type;
	u16 out_len;
	u8 sfp_qsfp_info[SFP_INFO_MAX_SIZE];
};

#define STD_SFP_INFO_MAX_SIZE	640
struct hinic_cmd_get_std_sfp_info {
	u8 status;
	u8 version;
	u8 rsvd0[6];

	u8 port_id;
	u8 wire_type;
	u16 eeprom_len;
	u32 rsvd;
	u8 sfp_info[STD_SFP_INFO_MAX_SIZE];
};

#define HINIC_MAX_PORT_ID	4

struct hinic_port_routine_cmd {
	bool up_send_sfp_info;
	bool up_send_sfp_abs;

	struct hinic_cmd_get_sfp_qsfp_info sfp_info;
	struct hinic_cmd_get_light_module_abs abs;
};

struct card_node {
	struct list_head node;
	struct list_head func_list;
	char chip_name[IFNAMSIZ];
	void *log_info;
	void *dbgtool_info;
	void *func_handle_array[MAX_FUNCTION_NUM];
	unsigned char dp_bus_num;
	u8 func_num;
	struct attribute dbgtool_attr_file;

	bool cos_up_setted;
	u8 cos_up[HINIC_MAX_COS];
	bool ppf_state;
	u8 pf_bus_num[HINIC_MAX_PF_NUM];
	bool disable_vf_load[HINIC_MAX_PF_NUM];
	u32 vf_mbx_old_rand_id[MAX_FUNCTION_NUM];
	u32 vf_mbx_rand_id[MAX_FUNCTION_NUM];
	struct hinic_port_routine_cmd rt_cmd[HINIC_MAX_PORT_ID];

	/* mutex used for copy sfp info */
	struct mutex sfp_mutex;
};

enum hinic_hwdev_init_state {
	HINIC_HWDEV_NONE_INITED = 0,
	HINIC_HWDEV_CLP_INITED,
	HINIC_HWDEV_AEQ_INITED,
	HINIC_HWDEV_MGMT_INITED,
	HINIC_HWDEV_MBOX_INITED,
	HINIC_HWDEV_CMDQ_INITED,
	HINIC_HWDEV_COMM_CH_INITED,
	HINIC_HWDEV_ALL_INITED,
	HINIC_HWDEV_MAX_INVAL_INITED
};

enum hinic_func_mode {
	/* single host */
	FUNC_MOD_NORMAL_HOST,
	/* multi host, baremate, sdi side */
	FUNC_MOD_MULTI_BM_MASTER,
	/* multi host, baremate, host side */
	FUNC_MOD_MULTI_BM_SLAVE,
	/* multi host, vm mode, sdi side */
	FUNC_MOD_MULTI_VM_MASTER,
	/* multi host, vm mode, host side */
	FUNC_MOD_MULTI_VM_SLAVE,
};

enum hinic_func_cap {
	/* send message to mgmt cpu directly */
	HINIC_FUNC_MGMT = 1 << 0,
	/* setting port attribute, pause/speed etc. */
	HINIC_FUNC_PORT = 1 << 1,
	/* Enable SR-IOV in default */
	HINIC_FUNC_SRIOV_EN_DFLT = 1 << 2,
	/* Can't change VF num */
	HINIC_FUNC_SRIOV_NUM_FIX = 1 << 3,
	/* Fcorce pf/vf link up */
	HINIC_FUNC_FORCE_LINK_UP = 1 << 4,
	/* Support rate limit */
	HINIC_FUNC_SUPP_RATE_LIMIT = 1 << 5,
	HINIC_FUNC_SUPP_DFX_REG = 1 << 6,
	/* Support promisc/multicast/all-multi */
	HINIC_FUNC_SUPP_RX_MODE = 1 << 7,
	/* Set vf mac and vlan by ip link */
	HINIC_FUNC_SUPP_SET_VF_MAC_VLAN = 1 << 8,
	/* Support set mac by ifconfig */
	HINIC_FUNC_SUPP_CHANGE_MAC = 1 << 9,
	/* OVS don't support SCTP_CRC/HW_VLAN/LRO */
	HINIC_FUNC_OFFLOAD_OVS_UNSUPP = 1 << 10,
	/* OVS don't support encap-tso/encap-csum */
	HINIC_FUNC_SUPP_ENCAP_TSO_CSUM = 1 << 11,
};

#define FUNC_SUPPORT_MGMT(hwdev)		\
	(!!(hinic_get_func_feature_cap(hwdev) & HINIC_FUNC_MGMT))
#define FUNC_SUPPORT_PORT_SETTING(hwdev)	\
	(!!(hinic_get_func_feature_cap(hwdev) & HINIC_FUNC_PORT))
#define FUNC_SUPPORT_DCB(hwdev)			\
	(FUNC_SUPPORT_PORT_SETTING(hwdev))
#define FUNC_ENABLE_SRIOV_IN_DEFAULT(hwdev)	\
	(!!(hinic_get_func_feature_cap(hwdev) & \
	    HINIC_FUNC_SRIOV_EN_DFLT))
#define FUNC_SRIOV_FIX_NUM_VF(hwdev)		\
	(!!(hinic_get_func_feature_cap(hwdev) & \
	    HINIC_FUNC_SRIOV_NUM_FIX))
#define FUNC_SUPPORT_RX_MODE(hwdev)		\
	(!!(hinic_get_func_feature_cap(hwdev) & \
	    HINIC_FUNC_SUPP_RX_MODE))
#define FUNC_SUPPORT_RATE_LIMIT(hwdev)		\
	(!!(hinic_get_func_feature_cap(hwdev) & \
	    HINIC_FUNC_SUPP_RATE_LIMIT))
#define FUNC_SUPPORT_SET_VF_MAC_VLAN(hwdev)	\
	(!!(hinic_get_func_feature_cap(hwdev) & \
	    HINIC_FUNC_SUPP_SET_VF_MAC_VLAN))
#define FUNC_SUPPORT_CHANGE_MAC(hwdev)		\
	(!!(hinic_get_func_feature_cap(hwdev) & \
	    HINIC_FUNC_SUPP_CHANGE_MAC))
#define FUNC_FORCE_LINK_UP(hwdev)		\
	(!!(hinic_get_func_feature_cap(hwdev) & \
	    HINIC_FUNC_FORCE_LINK_UP))
#define FUNC_SUPPORT_SCTP_CRC(hwdev)		\
	(!(hinic_get_func_feature_cap(hwdev) &  \
	   HINIC_FUNC_OFFLOAD_OVS_UNSUPP))
#define FUNC_SUPPORT_HW_VLAN(hwdev)		\
	(!(hinic_get_func_feature_cap(hwdev) &  \
	   HINIC_FUNC_OFFLOAD_OVS_UNSUPP))
#define FUNC_SUPPORT_LRO(hwdev)			\
	(!(hinic_get_func_feature_cap(hwdev) &  \
	   HINIC_FUNC_OFFLOAD_OVS_UNSUPP))
#define FUNC_SUPPORT_ENCAP_TSO_CSUM(hwdev)	\
	(!!(hinic_get_func_feature_cap(hwdev) & \
	   HINIC_FUNC_SUPP_ENCAP_TSO_CSUM))

int hinic_init_hwdev(struct hinic_init_para *para);
int hinic_set_vf_dev_cap(void *hwdev);
void hinic_free_hwdev(void *hwdev);
void hinic_shutdown_hwdev(void *hwdev);
void hinic_set_api_stop(void *hwdev);

void hinic_ppf_hwdev_unreg(void *hwdev);
void hinic_ppf_hwdev_reg(void *hwdev, void *ppf_hwdev);

void hinic_qps_num_set(void *hwdev, u32 num_qps);


bool hinic_is_hwdev_mod_inited(void *hwdev, enum hinic_hwdev_init_state state);
enum hinic_func_mode hinic_get_func_mode(void *hwdev);
u64 hinic_get_func_feature_cap(void *hwdev);

enum hinic_service_mode {
	HINIC_WORK_MODE_OVS	= 0,
	HINIC_WORK_MODE_UNKNOWN,
	HINIC_WORK_MODE_NIC,
	HINIC_WORK_MODE_OVS_SP = 7,
	HINIC_WORK_MODE_OVS_DQ,
	HINIC_WORK_MODE_INVALID	= 0xFF,
};

enum hinic_service_mode hinic_get_service_mode(void *hwdev);

#define WORK_AS_OVS_MODE(mode) \
	(((mode) == HINIC_WORK_MODE_OVS) || \
	    ((mode) == HINIC_WORK_MODE_OVS_SP) || \
	    ((mode) == HINIC_WORK_MODE_OVS_DQ))

int hinic_slq_init(void *dev, int num_wqs);
void hinic_slq_uninit(void *dev);
int hinic_slq_alloc(void *dev, u16 wqebb_size, u16 q_depth,
		    u16 page_size, u64 *cla_addr, void **handle);
void hinic_slq_free(void *dev, void *handle);
u64 hinic_slq_get_addr(void *handle, u16 index);
u64 hinic_slq_get_first_pageaddr(void *handle);

typedef void (*comm_up_self_msg_proc)(void *handle, void *buf_in,
				u16 in_size, void *buf_out, u16 *out_size);

void hinic_comm_recv_mgmt_self_cmd_reg(void *hwdev, u8 cmd,
				       comm_up_self_msg_proc proc);

void hinic_comm_recv_up_self_cmd_unreg(void *hwdev, u8 cmd);

int hinic_micro_log_path_set(void *hwdev, u8 *log_path);
int hinic_micro_log_func_en(void *hwdev, u8 is_en);

/* defined by chip */
enum hinic_fault_type {
	FAULT_TYPE_CHIP,
	FAULT_TYPE_UCODE,
	FAULT_TYPE_MEM_RD_TIMEOUT,
	FAULT_TYPE_MEM_WR_TIMEOUT,
	FAULT_TYPE_REG_RD_TIMEOUT,
	FAULT_TYPE_REG_WR_TIMEOUT,
	FAULT_TYPE_PHY_FAULT,
	FAULT_TYPE_MAX,
};

/* defined by chip */
enum hinic_fault_err_level {
	/* default err_level=FAULT_LEVEL_FATAL if
	 * type==FAULT_TYPE_MEM_RD_TIMEOUT || FAULT_TYPE_MEM_WR_TIMEOUT ||
	 *	 FAULT_TYPE_REG_RD_TIMEOUT || FAULT_TYPE_REG_WR_TIMEOUT ||
	 *	 FAULT_TYPE_UCODE
	 * other: err_level in event.chip.err_level if type==FAULT_TYPE_CHIP
	 */
	FAULT_LEVEL_FATAL,
	FAULT_LEVEL_SERIOUS_RESET,
	FAULT_LEVEL_SERIOUS_FLR,
	FAULT_LEVEL_GENERAL,
	FAULT_LEVEL_SUGGESTION,
	FAULT_LEVEL_MAX
};

enum hinic_fault_source_type {
	/* same as FAULT_TYPE_CHIP */
	HINIC_FAULT_SRC_HW_MGMT_CHIP = 0,
	/* same as FAULT_TYPE_UCODE */
	HINIC_FAULT_SRC_HW_MGMT_UCODE,
	/* same as FAULT_TYPE_MEM_RD_TIMEOUT */
	HINIC_FAULT_SRC_HW_MGMT_MEM_RD_TIMEOUT,
	/* same as FAULT_TYPE_MEM_WR_TIMEOUT */
	HINIC_FAULT_SRC_HW_MGMT_MEM_WR_TIMEOUT,
	/* same as FAULT_TYPE_REG_RD_TIMEOUT */
	HINIC_FAULT_SRC_HW_MGMT_REG_RD_TIMEOUT,
	/* same as FAULT_TYPE_REG_WR_TIMEOUT */
	HINIC_FAULT_SRC_HW_MGMT_REG_WR_TIMEOUT,
	HINIC_FAULT_SRC_SW_MGMT_UCODE,
	HINIC_FAULT_SRC_MGMT_WATCHDOG,
	HINIC_FAULT_SRC_MGMT_RESET = 8,
	HINIC_FAULT_SRC_HW_PHY_FAULT,
	HINIC_FAULT_SRC_HOST_HEARTBEAT_LOST = 20,
	HINIC_FAULT_SRC_TYPE_MAX,
};

struct hinic_fault_sw_mgmt {
	u8 event_id;
	u64 event_data;
};

union hinic_fault_hw_mgmt {
	u32 val[4];
	/* valid only type==FAULT_TYPE_CHIP */
	struct {
		u8 node_id;
		/* enum hinic_fault_err_level */
		u8 err_level;
		u16 err_type;
		u32 err_csr_addr;
		u32 err_csr_value;
		/* func_id valid only err_level==FAULT_LEVEL_SERIOUS_FLR */
		u16 func_id;
		u16 rsvd2;
	} chip;

	/* valid only type==FAULT_TYPE_UCODE */
	struct {
		u8 cause_id;
		u8 core_id;
		u8 c_id;
		u8 rsvd3;
		u32 epc;
		u32 rsvd4;
		u32 rsvd5;
	} ucode;

	/* valid only type==FAULT_TYPE_MEM_RD_TIMEOUT ||
	 * FAULT_TYPE_MEM_WR_TIMEOUT
	 */
	struct {
		u32 err_csr_ctrl;
		u32 err_csr_data;
		u32 ctrl_tab;
		u32 mem_index;
	} mem_timeout;

	/* valid only type==FAULT_TYPE_REG_RD_TIMEOUT ||
	 * FAULT_TYPE_REG_WR_TIMEOUT
	 */
	struct {
		u32 err_csr;
		u32 rsvd6;
		u32 rsvd7;
		u32 rsvd8;
	} reg_timeout;

	struct {
		/* 0: read; 1: write */
		u8 op_type;
		u8 port_id;
		u8 dev_ad;
		u8 rsvd9;
		u32 csr_addr;
		u32 op_data;
		u32 rsvd10;
	} phy_fault;
};

/* defined by chip */
struct hinic_fault_event {
	/* enum hinic_fault_type */
	u8 type;
	u8 rsvd0[3];
	union hinic_fault_hw_mgmt event;
};

struct hinic_fault_recover_info {
	u8 fault_src; /* enum hinic_fault_source_type */
	u8 fault_lev; /* enum hinic_fault_err_level */
	u8 rsvd0[2];
	union {
		union hinic_fault_hw_mgmt hw_mgmt;
		struct hinic_fault_sw_mgmt sw_mgmt;
		u32 mgmt_rsvd[4];
		u32 host_rsvd[4];
	} fault_data;
};

struct hinic_dcb_state {
	u8	dcb_on;
	u8	default_cos;
	u8	up_cos[8];
};

enum link_err_type {
	LINK_ERR_MODULE_UNRECOGENIZED,
	LINK_ERR_NUM,
};

enum port_module_event_type {
	HINIC_PORT_MODULE_CABLE_PLUGGED,
	HINIC_PORT_MODULE_CABLE_UNPLUGGED,
	HINIC_PORT_MODULE_LINK_ERR,
	HINIC_PORT_MODULE_MAX_EVENT,
};

struct hinic_port_module_event {
	enum port_module_event_type type;
	enum link_err_type err_type;
};

struct hinic_event_link_info {
	u8 valid;
	u8 port_type;
	u8 autoneg_cap;
	u8 autoneg_state;
	u8 duplex;
	u8 speed;
};

struct hinic_mctp_host_info {
	u8 major_cmd;
	u8 sub_cmd;
	u8 rsvd[2];

	u32 data_len;
	void *data;
};

/* multi host mgmt event sub cmd */
enum hinic_mhost_even_type {
	HINIC_MHOST_NIC_STATE_CHANGE = 1,
};

struct hinic_mhost_nic_func_state {
	u8 status;

	u8 enable;
	u16 func_idx;
};

struct hinic_multi_host_mgmt_event {
	u16 sub_cmd;
	u16 rsvd[3];

	void *data;
};

enum hinic_event_type {
	HINIC_EVENT_LINK_DOWN = 0,
	HINIC_EVENT_LINK_UP = 1,
	HINIC_EVENT_HEART_LOST = 2,
	HINIC_EVENT_FAULT = 3,
	HINIC_EVENT_NOTIFY_VF_DCB_STATE = 4,
	HINIC_EVENT_DCB_STATE_CHANGE = 5,
	HINIC_EVENT_FMW_ACT_NTC = 6,
	HINIC_EVENT_PORT_MODULE_EVENT = 7,
	HINIC_EVENT_MCTP_GET_HOST_INFO,
	HINIC_EVENT_MULTI_HOST_MGMT,
	HINIC_EVENT_INIT_MIGRATE_PF,
};

struct hinic_event_info {
	enum hinic_event_type type;
	union {
		struct hinic_event_link_info link_info;
		struct hinic_fault_event info;
		struct hinic_dcb_state dcb_state;
		struct hinic_port_module_event module_event;
		u8 vf_default_cos;
		struct hinic_mctp_host_info mctp_info;
		struct hinic_multi_host_mgmt_event mhost_mgmt;
	};
};

enum hinic_ucode_event_type {
	HINIC_INTERNAL_TSO_FATAL_ERROR = 0x0,
	HINIC_INTERNAL_LRO_FATAL_ERROR = 0x1,
	HINIC_INTERNAL_TX_FATAL_ERROR = 0x2,
	HINIC_INTERNAL_RX_FATAL_ERROR = 0x3,
	HINIC_INTERNAL_OTHER_FATAL_ERROR = 0x4,
	HINIC_NIC_FATAL_ERROR_MAX = 0x8,
};

typedef void (*hinic_event_handler)(void *handle,
		struct hinic_event_info *event);

typedef void (*hinic_fault_recover_handler)(void *pri_handle,
					struct hinic_fault_recover_info info);
/* only register once */
void hinic_event_register(void *dev, void *pri_handle,
			  hinic_event_handler callback);
void hinic_event_unregister(void *dev);

void hinic_detect_hw_present(void *hwdev);

void hinic_set_chip_absent(void *hwdev);

int hinic_get_chip_present_flag(void *hwdev);

void hinic_set_pcie_order_cfg(void *handle);

int hinic_get_mgmt_channel_status(void *handle);

enum hinic_led_mode {
	HINIC_LED_MODE_ON,
	HINIC_LED_MODE_OFF,
	HINIC_LED_MODE_FORCE_1HZ,
	HINIC_LED_MODE_FORCE_2HZ,
	HINIC_LED_MODE_FORCE_4HZ,
	HINIC_LED_MODE_1HZ,
	HINIC_LED_MODE_2HZ,
	HINIC_LED_MODE_4HZ,
	HINIC_LED_MODE_INVALID,
};

enum hinic_led_type {
	HINIC_LED_TYPE_LINK,
	HINIC_LED_TYPE_LOW_SPEED,
	HINIC_LED_TYPE_HIGH_SPEED,
	HINIC_LED_TYPE_INVALID,
};

int hinic_reset_led_status(void *hwdev, u8 port);
int hinic_set_led_status(void *hwdev, u8 port, enum hinic_led_type type,
			 enum hinic_led_mode mode);

struct hinic_board_info {
	u32	board_type;
	u32	port_num;
	u32	port_speed;
	u32	pcie_width;
	u32	host_num;
	u32	pf_num;
	u32	vf_total_num;
	u32	tile_num;
	u32	qcm_num;
	u32	core_num;
	u32	work_mode;
	u32	service_mode;
	u32	pcie_mode;
	u32	cfg_addr;
	u32	boot_sel;
	u32	board_id;
};

int hinic_get_board_info(void *hwdev, struct hinic_board_info *info);
bool hinic_get_ppf_status(void *hwdev);

struct hw_pf_info {
	u16	glb_func_idx;
	u16	glb_pf_vf_offset;
	u8	p2p_idx;
	u8	itf_idx;
	u16	max_vfs;
	u16	max_queue_num;
	u16	ovs_q_vf_num[9];
	u32	resv;
};

struct hinic_hw_pf_infos {
	u8	num_pfs;
	u8	rsvd1[3];

	struct hw_pf_info infos[16];
};

int hinic_get_hw_pf_infos(void *hwdev, struct hinic_hw_pf_infos *infos);
int hinic_set_ip_check(void *hwdev, bool ip_check_ctl);
int hinic_mbox_to_host_sync(void *hwdev, enum hinic_mod_type mod,
			    u8 cmd, void *buf_in, u16 in_size, void *buf_out,
			    u16 *out_size, u32 timeout);
int hinic_mbox_ppf_to_vf(void *hwdev, enum hinic_mod_type mod, u16 func_id,
			 u8 cmd, void *buf_in, u16 in_size, void *buf_out,
			 u16 *out_size, u32 timeout);

int hinic_get_card_present_state(void *hwdev, bool *card_present_state);

void hinic_migrate_report(void *dev);
int hinic_set_vxlan_udp_dport(void *hwdev, u32 udp_port);
bool is_multi_vm_slave(void *hwdev);
bool is_multi_bm_slave(void *hwdev);

#endif
