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

#ifndef HINIC_HWDEV_H_
#define HINIC_HWDEV_H_

#include "hinic_port_cmd.h"

/* to use 0-level CLA, page size must be: 64B(wqebb) * 4096(max_q_depth) */
#define HINIC_DEFAULT_WQ_PAGE_SIZE	0x40000
#define HINIC_HW_WQ_PAGE_SIZE		0x1000

#define HINIC_MSG_TO_MGMT_MAX_LEN		2016

#define HINIC_MGMT_STATUS_ERR_OK          0   /* Ok */
#define HINIC_MGMT_STATUS_ERR_PARAM       1   /* Invalid parameter */
#define HINIC_MGMT_STATUS_ERR_FAILED      2   /* Operation failed */
#define HINIC_MGMT_STATUS_ERR_PORT        3   /* Invalid port */
#define HINIC_MGMT_STATUS_ERR_TIMEOUT     4   /* Operation time out */
#define HINIC_MGMT_STATUS_ERR_NOMATCH     5   /* Version not match */
#define HINIC_MGMT_STATUS_ERR_EXIST       6   /* Entry exists */
#define HINIC_MGMT_STATUS_ERR_NOMEM       7   /* Out of memory */
#define HINIC_MGMT_STATUS_ERR_INIT        8   /* Feature not initialized */
#define HINIC_MGMT_STATUS_ERR_FAULT       9   /* Invalid address */
#define HINIC_MGMT_STATUS_ERR_PERM        10  /* Operation not permitted */
#define HINIC_MGMT_STATUS_ERR_EMPTY       11  /* Table empty */
#define HINIC_MGMT_STATUS_ERR_FULL        12  /* Table full */
#define HINIC_MGMT_STATUS_ERR_NOT_FOUND   13  /* Not found */
#define HINIC_MGMT_STATUS_ERR_BUSY        14  /* Device or resource busy */
#define HINIC_MGMT_STATUS_ERR_RESOURCE    15  /* No resources for operation */
#define HINIC_MGMT_STATUS_ERR_CONFIG      16  /* Invalid configuration */
#define HINIC_MGMT_STATUS_ERR_UNAVAIL     17  /* Feature unavailable */
#define HINIC_MGMT_STATUS_ERR_CRC         18  /* CRC check failed */
#define HINIC_MGMT_STATUS_ERR_NXIO        19  /* No such device or address */
#define HINIC_MGMT_STATUS_ERR_ROLLBACK    20  /* Chip rollback fail */
#define HINIC_MGMT_STATUS_ERR_LEN         32  /* Length too short or too long */
#define HINIC_MGMT_STATUS_ERR_UNSUPPORT   0xFF /* Feature not supported */

#define HINIC_CHIP_PRESENT 1
#define HINIC_CHIP_ABSENT 0

struct cfg_mgmt_info;
struct rdma_comp_resource;

struct hinic_hwif;
struct hinic_nic_io;
struct hinic_wqs;
struct hinic_aeqs;
struct hinic_ceqs;
struct hinic_mbox_func_to_func;
struct hinic_msg_pf_to_mgmt;
struct hinic_cmdqs;
struct hinic_multi_host_mgmt;

struct hinic_root_ctxt {
	u8	status;
	u8	version;
	u8	rsvd0[6];

	u16	func_idx;
	u16	rsvd1;
	u8	set_cmdq_depth;
	u8	cmdq_depth;
	u8	lro_en;
	u8	rsvd2;
	u8	ppf_idx;
	u8	rsvd3;
	u16	rq_depth;
	u16	rx_buf_sz;
	u16	sq_depth;
};

struct hinic_page_addr {
	void *virt_addr;
	u64 phys_addr;
};

struct mqm_addr_trans_tbl_info {
	u32 chunk_num;
	u32 search_gpa_num;
	u32 page_size;
	u32 page_num;
	struct hinic_page_addr *brm_srch_page_addr;
};

#define HINIC_PCIE_LINK_DOWN		0xFFFFFFFF

#define HINIC_DEV_ACTIVE_FW_TIMEOUT	(35 * 1000)
#define HINIC_DEV_BUSY_ACTIVE_FW	0xFE

#define HINIC_HW_WQ_NAME	"hinic_hardware"
#define HINIC_HEARTBEAT_PERIOD		1000
#define HINIC_HEARTBEAT_START_EXPIRE	5000

#define HINIC_CHIP_ERROR_TYPE_MAX	1024
#define HINIC_CHIP_FAULT_SIZE		\
	(HINIC_NODE_ID_MAX * FAULT_LEVEL_MAX * HINIC_CHIP_ERROR_TYPE_MAX)

enum hinic_node_id {
	HINIC_NODE_ID_IPSU = 4,
	HINIC_NODE_ID_MGMT_HOST = 21, /* Host CPU send API to uP */
	HINIC_NODE_ID_MAX = 22
};

#define HINIC_HWDEV_INIT_MODES_MASK	((1UL << HINIC_HWDEV_ALL_INITED) - 1)

enum hinic_hwdev_func_state {
	HINIC_HWDEV_FUNC_INITED = HINIC_HWDEV_ALL_INITED,

	HINIC_HWDEV_FUNC_DEINIT,

	HINIC_HWDEV_STATE_BUSY = 31,
};

struct hinic_cqm_stats {
	atomic_t cqm_cmd_alloc_cnt;
	atomic_t cqm_cmd_free_cnt;
	atomic_t cqm_send_cmd_box_cnt;
	atomic_t cqm_send_cmd_imm_cnt;
	atomic_t cqm_db_addr_alloc_cnt;
	atomic_t cqm_db_addr_free_cnt;

	atomic_t cqm_fc_srq_create_cnt;
	atomic_t cqm_srq_create_cnt;
	atomic_t cqm_rq_create_cnt;

	atomic_t cqm_qpc_mpt_create_cnt;
	atomic_t cqm_nonrdma_queue_create_cnt;
	atomic_t cqm_rdma_queue_create_cnt;
	atomic_t cqm_rdma_table_create_cnt;

	atomic_t cqm_qpc_mpt_delete_cnt;
	atomic_t cqm_nonrdma_queue_delete_cnt;
	atomic_t cqm_rdma_queue_delete_cnt;
	atomic_t cqm_rdma_table_delete_cnt;

	atomic_t cqm_func_timer_clear_cnt;
	atomic_t cqm_func_hash_buf_clear_cnt;

	atomic_t cqm_scq_callback_cnt;
	atomic_t cqm_ecq_callback_cnt;
	atomic_t cqm_nocq_callback_cnt;
	atomic_t cqm_aeq_callback_cnt[112];
};

struct hinic_link_event_stats {
	atomic_t link_down_stats;
	atomic_t link_up_stats;
};

struct hinic_fault_event_stats {
	atomic_t chip_fault_stats[HINIC_NODE_ID_MAX][FAULT_LEVEL_MAX];
	atomic_t fault_type_stat[FAULT_TYPE_MAX];
	atomic_t pcie_fault_stats;
};

struct hinic_hw_stats {
	atomic_t heart_lost_stats;
	atomic_t nic_ucode_event_stats[HINIC_NIC_FATAL_ERROR_MAX];
	struct hinic_cqm_stats cqm_stats;
	struct hinic_link_event_stats link_event_stats;
	struct hinic_fault_event_stats fault_event_stats;
};

struct hinic_fault_info_node {
	struct list_head list;
	struct hinic_hwdev *hwdev;
	struct hinic_fault_recover_info info;
};

enum heartbeat_support_state {
	HEARTBEAT_NOT_SUPPORT = 0,
	HEARTBEAT_SUPPORT,
};

/* 25s for max 5 heartbeat event lost */
#define HINIC_HEARBEAT_ENHANCED_LOST		25000
struct hinic_heartbeat_enhanced {
	bool		en;	/* enable enhanced heartbeat or not */

	unsigned long	last_update_jiffies;
	u32		last_heartbeat;

	unsigned long	start_detect_jiffies;
};

#define HINIC_NORMAL_HOST_CAP	(HINIC_FUNC_MGMT | HINIC_FUNC_PORT | \
				 HINIC_FUNC_SUPP_RATE_LIMIT | \
				 HINIC_FUNC_SUPP_DFX_REG | \
				 HINIC_FUNC_SUPP_RX_MODE | \
				 HINIC_FUNC_SUPP_SET_VF_MAC_VLAN | \
				 HINIC_FUNC_SUPP_CHANGE_MAC | \
				 HINIC_FUNC_SUPP_ENCAP_TSO_CSUM)
#define HINIC_MULTI_BM_MASTER	(HINIC_FUNC_MGMT | HINIC_FUNC_PORT | \
				 HINIC_FUNC_SUPP_DFX_REG | \
				 HINIC_FUNC_SUPP_RX_MODE | \
				 HINIC_FUNC_SUPP_SET_VF_MAC_VLAN | \
				 HINIC_FUNC_SUPP_CHANGE_MAC)
#define HINIC_MULTI_BM_SLAVE	(HINIC_FUNC_SRIOV_EN_DFLT | \
				 HINIC_FUNC_SRIOV_NUM_FIX | \
				 HINIC_FUNC_FORCE_LINK_UP | \
				 HINIC_FUNC_OFFLOAD_OVS_UNSUPP)
#define HINIC_MULTI_VM_MASTER	(HINIC_FUNC_MGMT | HINIC_FUNC_PORT | \
				 HINIC_FUNC_SUPP_DFX_REG | \
				 HINIC_FUNC_SUPP_RX_MODE | \
				 HINIC_FUNC_SUPP_SET_VF_MAC_VLAN | \
				 HINIC_FUNC_SUPP_CHANGE_MAC)
#define HINIC_MULTI_VM_SLAVE	(HINIC_FUNC_MGMT | \
				 HINIC_FUNC_SUPP_DFX_REG | \
				 HINIC_FUNC_SRIOV_EN_DFLT | \
				 HINIC_FUNC_SUPP_RX_MODE | \
				 HINIC_FUNC_SUPP_CHANGE_MAC | \
				 HINIC_FUNC_OFFLOAD_OVS_UNSUPP)

#define MULTI_HOST_CHIP_MODE_SHIFT		0
#define MULTI_HOST_MASTER_MBX_STS_SHIFT		0x4
#define MULTI_HOST_PRIV_DATA_SHIFT		0x8

#define MULTI_HOST_CHIP_MODE_MASK		0xF
#define MULTI_HOST_MASTER_MBX_STS_MASK		0xF
#define MULTI_HOST_PRIV_DATA_MASK		0xFFFF

#define MULTI_HOST_REG_SET(val, member)			\
				(((val) & MULTI_HOST_##member##_MASK) \
					<< MULTI_HOST_##member##_SHIFT)
#define MULTI_HOST_REG_GET(val, member)			\
				(((val) >> MULTI_HOST_##member##_SHIFT) \
					& MULTI_HOST_##member##_MASK)
#define MULTI_HOST_REG_CLEAR(val, member)	\
				((val) & (~(MULTI_HOST_##member##_MASK \
					<< MULTI_HOST_##member##_SHIFT)))

#define HINIC_BOARD_TYPE_MULTI_HOST_ETH_25GE	12

/* new version of roce qp not limited by power of 2 */
#define HINIC_CMD_VER_ROCE_QP		1
/* new version for add function id in multi-host */
#define HINIC_CMD_VER_FUNC_ID		2

struct hinic_hwdev {
	void *adapter_hdl;  /* pointer to hinic_pcidev or NDIS_Adapter */
	void *pcidev_hdl;   /* pointer to pcidev or Handler */
	void *dev_hdl;      /* pointer to pcidev->dev or Handler, for
			     * sdk_err() or dma_alloc()
			     */
	u32 wq_page_size;

	void *cqm_hdl;
	void *chip_node;

	struct hinic_hwif *hwif; /* include void __iomem *bar */
	struct hinic_nic_io *nic_io;
	struct cfg_mgmt_info *cfg_mgmt;
	struct rdma_comp_resource  *rdma_comp_res;
	struct hinic_wqs *wqs;	/* for FC slq */
	struct mqm_addr_trans_tbl_info mqm_att;

	struct hinic_aeqs *aeqs;
	struct hinic_ceqs *ceqs;

	struct hinic_mbox_func_to_func *func_to_func;

	struct hinic_msg_pf_to_mgmt *pf_to_mgmt;
	struct hinic_clp_pf_to_mgmt *clp_pf_to_mgmt;

	struct hinic_cmdqs *cmdqs;

	struct hinic_page_addr page_pa0;
	struct hinic_page_addr page_pa1;

	hinic_event_handler event_callback;
	void *event_pri_handle;

	struct semaphore recover_sem;
	bool collect_log_flag;
	bool history_fault_flag;
	struct hinic_fault_recover_info history_fault;
	void *recover_pri_hd;
	hinic_fault_recover_handler recover_cb;

	struct work_struct fault_work;
	struct semaphore fault_list_sem;

	struct work_struct timer_work;
	struct workqueue_struct *workq;
	struct timer_list heartbeat_timer;
	/* true represent heartbeat lost, false represent heartbeat restore */
	u32 heartbeat_lost;
	int chip_present_flag;
	struct hinic_heartbeat_enhanced heartbeat_ehd;
	struct hinic_hw_stats hw_stats;
	u8 *chip_fault_stats;

	u32 statufull_ref_cnt;
	ulong func_state;

	u64 feature_cap;	/* enum hinic_func_cap */
	enum hinic_func_mode	func_mode;

	struct hinic_multi_host_mgmt	*mhost_mgmt;

	/* In bmgw x86 host, driver can't send message to mgmt cpu directly,
	 * need to trasmit message ppf mbox to bmgw arm host.
	 */
	struct semaphore ppf_sem;
	void *ppf_hwdev;

	struct semaphore func_sem;
	int func_ref;
	struct hinic_board_info board_info;
#define MGMT_VERSION_MAX_LEN	32
	u8	mgmt_ver[MGMT_VERSION_MAX_LEN];
	u64	fw_support_func_flag;
};

int hinic_init_comm_ch(struct hinic_hwdev *hwdev);

void hinic_uninit_comm_ch(struct hinic_hwdev *hwdev);

int hinic_ppf_ext_db_init(void *dev);

int hinic_ppf_ext_db_deinit(void *dev);

enum hinic_set_arm_type {
	HINIC_SET_ARM_CMDQ,
	HINIC_SET_ARM_SQ,
	HINIC_SET_ARM_TYPE_NUM,
};

int hinic_set_arm_bit(void *hwdev, enum hinic_set_arm_type q_type, u16 q_id);

void hinic_set_chip_present(void *hwdev);
void hinic_force_complete_all(void *hwdev);

void hinic_init_heartbeat(struct hinic_hwdev *hwdev);
void hinic_destroy_heartbeat(struct hinic_hwdev *hwdev);

u8 hinic_nic_sw_aeqe_handler(void *handle, u8 event, u64 data);


int hinic_enable_fast_recycle(void *hwdev, bool enable);
int hinic_l2nic_reset_base(struct hinic_hwdev *hwdev, u16 reset_flag);

enum l2nic_resource_type {
	RES_TYPE_NIC_FUNC = 0,
	RES_TYPE_FLUSH_BIT,
	RES_TYPE_PF_BW_CFG,
	RES_TYPE_MQM,
	RES_TYPE_SMF,
	RES_TYPE_CMDQ_ROOTCTX,
	RES_TYPE_SQ_CI_TABLE,
	RES_TYPE_CEQ,
	RES_TYPE_MBOX,
	RES_TYPE_AEQ,
};

void hinic_notify_dcb_state_event(struct hinic_hwdev *hwdev,
				  struct hinic_dcb_state *dcb_state);

int hinic_pf_msg_to_mgmt_sync(void *hwdev, enum hinic_mod_type mod, u8 cmd,
			      void *buf_in, u16 in_size,
			      void *buf_out, u16 *out_size, u32 timeout);

int hinic_pf_send_clp_cmd(void *hwdev, enum hinic_mod_type mod, u8 cmd,
			  void *buf_in, u16 in_size,
			  void *buf_out, u16 *out_size);

int hinic_get_bios_pf_bw_limit(void *hwdev, u32 *pf_bw_limit);

void hinic_fault_work_handler(struct work_struct *work);
void hinic_swe_fault_handler(struct hinic_hwdev *hwdev, u8 level,
			     u8 event, u64 val);

bool hinic_mgmt_event_ack_first(u8 mod, u8 cmd);

int hinic_set_wq_page_size(struct hinic_hwdev *hwdev, u16 func_idx,
			   u32 page_size);

int hinic_phy_init_status_judge(void *hwdev);

int hinic_hilink_info_show(struct hinic_hwdev *hwdev);
extern int hinic_api_csr_rd32(void *hwdev, u8 dest, u32 addr, u32 *val);
extern int hinic_api_csr_wr32(void *hwdev, u8 dest, u32 addr, u32 val);

int hinic_ppf_process_mbox_msg(struct hinic_hwdev *hwdev, u16 pf_idx, u16 vf_id,
			       enum hinic_mod_type mod, u8 cmd, void *buf_in,
			       u16 in_size, void *buf_out, u16 *out_size);

#define HINIC_SDI_MODE_UNKNOWN		0
#define HINIC_SDI_MODE_BM		1
#define HINIC_SDI_MODE_VM		2
#define HINIC_SDI_MODE_MAX		3
int hinic_get_sdi_mode(struct hinic_hwdev *hwdev, u16 *cur_mode);


void mgmt_heartbeat_event_handler(void *hwdev, void *buf_in, u16 in_size,
				  void *buf_out, u16 *out_size);

#endif
