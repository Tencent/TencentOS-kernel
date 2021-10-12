// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2021, Intel Corporation. */

#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/random.h>
#include "ice.h"
#include "ice_lib.h"
#include "ice_fltr.h"


static struct dentry *ice_debugfs_root;


static void ice_dump_pf(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);

	dev_info(dev, "pf struct:\n");
	dev_info(dev, "\tmax_pf_txqs = %d\n", pf->max_pf_txqs);
	dev_info(dev, "\tmax_pf_rxqs = %d\n", pf->max_pf_rxqs);
	dev_info(dev, "\tnum_alloc_vsi = %d\n", pf->num_alloc_vsi);
	dev_info(dev, "\tnum_lan_tx = %d\n", pf->num_lan_tx);
	dev_info(dev, "\tnum_lan_rx = %d\n", pf->num_lan_rx);
	dev_info(dev, "\tnum_avail_tx = %d\n", ice_get_avail_txq_count(pf));
	dev_info(dev, "\tnum_avail_rx = %d\n", ice_get_avail_rxq_count(pf));
	dev_info(dev, "\tnum_lan_msix = %d\n", pf->num_lan_msix);
	dev_info(dev, "\tnum_rdma_msix = %d\n", pf->num_rdma_msix);
	dev_info(dev, "\trdma_base_vector = %d\n", pf->rdma_base_vector);
#ifdef HAVE_NETDEV_SB_DEV
	dev_info(dev, "\tnum_macvlan = %d\n", pf->num_macvlan);
	dev_info(dev, "\tmax_num_macvlan = %d\n", pf->max_num_macvlan);
#endif /* HAVE_NETDEV_SB_DEV */
	dev_info(dev, "\tirq_tracker->num_entries = %d\n",
		 pf->irq_tracker->num_entries);
	dev_info(dev, "\tirq_tracker->end = %d\n", pf->irq_tracker->end);
	dev_info(dev, "\tirq_tracker valid count = %d\n",
		 ice_get_valid_res_count(pf->irq_tracker));
	dev_info(dev, "\tnum_avail_sw_msix = %d\n", pf->num_avail_sw_msix);
	dev_info(dev, "\tsriov_base_vector = %d\n", pf->sriov_base_vector);
	dev_info(dev, "\tnum_alloc_vfs = %d\n", pf->num_alloc_vfs);
	dev_info(dev, "\tnum_qps_per_vf = %d\n", pf->num_qps_per_vf);
	dev_info(dev, "\tnum_msix_per_vf = %d\n", pf->num_msix_per_vf);
}

static void ice_dump_pf_vsi_list(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	u16 i;

	ice_for_each_vsi(pf, i) {
		struct ice_vsi *vsi = pf->vsi[i];

		if (!vsi)
			continue;

		dev_info(dev, "vsi[%d]:\n", i);
		dev_info(dev, "\tvsi = %pK\n", vsi);
		dev_info(dev, "\tvsi_num = %d\n", vsi->vsi_num);
		dev_info(dev, "\ttype = %s\n", ice_vsi_type_str(vsi->type));
		if (vsi->type == ICE_VSI_VF)
			dev_info(dev, "\tvf_id = %d\n", vsi->vf_id);
		dev_info(dev, "\tback = %pK\n", vsi->back);
		dev_info(dev, "\tnetdev = %pK\n", vsi->netdev);
		dev_info(dev, "\tmax_frame = %d\n", vsi->max_frame);
		dev_info(dev, "\trx_buf_len = %d\n", vsi->rx_buf_len);
		dev_info(dev, "\tnum_txq = %d\n", vsi->num_txq);
		dev_info(dev, "\tnum_rxq = %d\n", vsi->num_rxq);
		dev_info(dev, "\treq_txq = %d\n", vsi->req_txq);
		dev_info(dev, "\treq_rxq = %d\n", vsi->req_rxq);
		dev_info(dev, "\talloc_txq = %d\n", vsi->alloc_txq);
		dev_info(dev, "\talloc_rxq = %d\n", vsi->alloc_rxq);
		dev_info(dev, "\tnum_rx_desc = %d\n", vsi->num_rx_desc);
		dev_info(dev, "\tnum_tx_desc = %d\n", vsi->num_tx_desc);
		dev_info(dev, "\tnum_vlan = %d\n", vsi->num_vlan);
	}
}

/**
 * ice_dump_pf_fdir - output Flow Director stats to dmesg log
 * @pf: pointer to PF to get Flow Director HW stats for.
 */
static void ice_dump_pf_fdir(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	u16 pf_guar_pool = 0;
	u32 dev_fltr_size;
	u32 dev_fltr_cnt;
	u32 pf_fltr_cnt;
	u16 i;

	pf_fltr_cnt = rd32(hw, PFQF_FD_CNT);
	dev_fltr_cnt = rd32(hw, GLQF_FD_CNT);
	dev_fltr_size = rd32(hw, GLQF_FD_SIZE);

	ice_for_each_vsi(pf, i) {
		struct ice_vsi *vsi = pf->vsi[i];

		if (!vsi)
			continue;

		pf_guar_pool += vsi->num_gfltr;
	}

	dev_info(dev, "Flow Director filter usage:\n");
	dev_info(dev, "\tPF guaranteed used = %d\n",
		 (pf_fltr_cnt & PFQF_FD_CNT_FD_GCNT_M) >>
		 PFQF_FD_CNT_FD_GCNT_S);
	dev_info(dev, "\tPF best_effort used = %d\n",
		 (pf_fltr_cnt & PFQF_FD_CNT_FD_BCNT_M) >>
		 PFQF_FD_CNT_FD_BCNT_S);
	dev_info(dev, "\tdevice guaranteed used = %d\n",
		 (dev_fltr_cnt & GLQF_FD_CNT_FD_GCNT_M) >>
		 GLQF_FD_CNT_FD_GCNT_S);
	dev_info(dev, "\tdevice best_effort used = %d\n",
		 (dev_fltr_cnt & GLQF_FD_CNT_FD_BCNT_M) >>
		 GLQF_FD_CNT_FD_BCNT_S);
	dev_info(dev, "\tPF guaranteed pool = %d\n", pf_guar_pool);
	dev_info(dev, "\tdevice guaranteed pool = %d\n",
		 (dev_fltr_size & GLQF_FD_SIZE_FD_GSIZE_M) >>
		 GLQF_FD_SIZE_FD_GSIZE_S);
	dev_info(dev, "\tdevice best_effort pool = %d\n",
		 hw->func_caps.fd_fltr_best_effort);
}

/**
 * ice_vsi_dump_ctxt - print the passed in VSI context structure
 * @dev: Device used for dev_info prints
 * @ctxt: VSI context structure to print
 */
static void ice_vsi_dump_ctxt(struct device *dev, struct ice_vsi_ctx *ctxt)
{
	struct ice_aqc_vsi_props *info;

	if (!ctxt)
		return;

	info = &ctxt->info;
	dev_info(dev, "Get VSI Parameters:\n");
	dev_info(dev, "\tVSI Number: %d Valid sections: 0x%04x\n",
		 ctxt->vsi_num, le16_to_cpu(info->valid_sections));

	dev_info(dev, "========================\n");
	dev_info(dev, "| Category - Switching |");
	dev_info(dev, "========================\n");
	dev_info(dev, "\tSwitch ID: %u\n", info->sw_id);
	dev_info(dev, "\tAllow Loopback: %s\n", (info->sw_flags &
		 ICE_AQ_VSI_SW_FLAG_ALLOW_LB) ? "enabled" : "disabled");
	dev_info(dev, "\tAllow Local Loopback: %s\n", (info->sw_flags &
		 ICE_AQ_VSI_SW_FLAG_LOCAL_LB) ? "enabled" : "disabled");
	dev_info(dev, "\tApply source VSI pruning: %s\n", (info->sw_flags &
		 ICE_AQ_VSI_SW_FLAG_SRC_PRUNE) ? "enabled" : "disabled");
	dev_info(dev, "\tEgress (Rx VLAN) pruning: %s\n",
		 (info->sw_flags2 & ICE_AQ_VSI_SW_FLAG_RX_PRUNE_EN_M) ?
		 "enabled" : "disabled");
	dev_info(dev, "\tLAN enable: %s\n", (info->sw_flags2 &
		 ICE_AQ_VSI_SW_FLAG_LAN_ENA) ? "enabled" : "disabled");
	dev_info(dev, "\tVEB statistic block ID: %u\n", info->veb_stat_id &
		 ICE_AQ_VSI_SW_VEB_STAT_ID_M);
	dev_info(dev, "\tVEB statistic block ID valid: %d\n",
		 (info->veb_stat_id & ICE_AQ_VSI_SW_VEB_STAT_ID_VALID) ? 1 : 0);

	dev_info(dev, "=======================\n");
	dev_info(dev, "| Category - Security |\n");
	dev_info(dev, "=======================\n");
	dev_info(dev, "\tAllow destination override: %s\n", (info->sec_flags &
		 ICE_AQ_VSI_SEC_FLAG_ALLOW_DEST_OVRD) ? "enabled" : "disabled");
	dev_info(dev, "\tEnable MAC anti-spoof: %s\n", (info->sec_flags &
		 ICE_AQ_VSI_SEC_FLAG_ENA_MAC_ANTI_SPOOF) ? "enabled" : "disabled");
	dev_info(dev, "\tIngress (Tx VLAN) pruning enables: %s\n",
		 (info->sec_flags & ICE_AQ_VSI_SEC_TX_PRUNE_ENA_M) ?
		 "enabled" : "disabled");

	dev_info(dev, "=================================\n");
	dev_info(dev, "| Category: Inner VLAN Handling |\n");
	dev_info(dev, "=================================\n");
	dev_info(dev, "\tPort Based Inner VLAN Insertion: PVLAN ID: %d PRIO: %d\n",
		 le16_to_cpu(info->port_based_inner_vlan) & VLAN_VID_MASK,
		 (le16_to_cpu(info->port_based_inner_vlan) & VLAN_PRIO_MASK) >>
		 VLAN_PRIO_SHIFT);
	dev_info(dev, "\tInner VLAN TX Mode: 0x%02x\n",
		 (info->inner_vlan_flags & ICE_AQ_VSI_INNER_VLAN_TX_MODE_M) >>
		 ICE_AQ_VSI_INNER_VLAN_TX_MODE_S);
	dev_info(dev, "\tInsert PVID: %s\n", (info->inner_vlan_flags &
		 ICE_AQ_VSI_INNER_VLAN_INSERT_PVID) ? "enabled" : "disabled");
	dev_info(dev, "\tInner VLAN and UP expose mode (RX): 0x%02x\n",
		 (info->inner_vlan_flags & ICE_AQ_VSI_INNER_VLAN_EMODE_M) >>
		 ICE_AQ_VSI_INNER_VLAN_EMODE_S);
	dev_info(dev, "\tBlock Inner VLAN from TX Descriptor: %s\n",
		 (info->inner_vlan_flags & ICE_AQ_VSI_INNER_VLAN_BLOCK_TX_DESC) ?
		 "enabled" : "disabled");

	dev_info(dev, "=================================\n");
	dev_info(dev, "| Category: Outer VLAN Handling |\n");
	dev_info(dev, "=================================\n");
	dev_info(dev, "\tPort Based Outer VLAN Insertion: PVID: %d PRIO: %d\n",
		 le16_to_cpu(info->port_based_outer_vlan) & VLAN_VID_MASK,
		 (le16_to_cpu(info->port_based_outer_vlan) & VLAN_PRIO_MASK) >>
		 VLAN_PRIO_SHIFT);
	dev_info(dev, "\tOuter VLAN and UP expose mode (RX): 0x%02x\n",
		 (info->outer_vlan_flags & ICE_AQ_VSI_OUTER_VLAN_EMODE_M) >>
		 ICE_AQ_VSI_OUTER_VLAN_EMODE_S);
	dev_info(dev, "\tOuter Tag type (Tx and Rx): 0x%02x\n",
		 (info->outer_vlan_flags & ICE_AQ_VSI_OUTER_TAG_TYPE_M) >>
		 ICE_AQ_VSI_OUTER_TAG_TYPE_S);
	dev_info(dev, "\tPort Based Outer VLAN Insert Enable: %s\n",
		 (info->outer_vlan_flags &
		 ICE_AQ_VSI_OUTER_VLAN_PORT_BASED_INSERT) ?
		 "enabled" : "disabled");
	dev_info(dev, "\tOuter VLAN TX Mode: 0x%02x\n",
		 (info->outer_vlan_flags & ICE_AQ_VSI_OUTER_VLAN_TX_MODE_M) >>
		 ICE_AQ_VSI_OUTER_VLAN_TX_MODE_S);
	dev_info(dev, "\tBlock Outer VLAN from TX Descriptor: %s\n",
		 (info->outer_vlan_flags & ICE_AQ_VSI_OUTER_VLAN_BLOCK_TX_DESC) ?
		 "enabled" : "disabled");
}

static const char *module_id_to_name(u16 module_id)
{
	switch (module_id) {
	case ICE_AQC_FW_LOG_ID_GENERAL:
		return "General";
	case ICE_AQC_FW_LOG_ID_CTRL:
		return "Control (Resets + Autoload)";
	case ICE_AQC_FW_LOG_ID_LINK:
		return "Link Management";
	case ICE_AQC_FW_LOG_ID_LINK_TOPO:
		return "Link Topology Detection";
	case ICE_AQC_FW_LOG_ID_DNL:
		return "DNL";
	case ICE_AQC_FW_LOG_ID_I2C:
		return "I2C";
	case ICE_AQC_FW_LOG_ID_SDP:
		return "SDP";
	case ICE_AQC_FW_LOG_ID_MDIO:
		return "MDIO";
	case ICE_AQC_FW_LOG_ID_ADMINQ:
		return "Admin Queue";
	case ICE_AQC_FW_LOG_ID_HDMA:
		return "HDMA";
	case ICE_AQC_FW_LOG_ID_LLDP:
		return "LLDP";
	case ICE_AQC_FW_LOG_ID_DCBX:
		return "DCBX";
	case ICE_AQC_FW_LOG_ID_DCB:
		return "DCB";
	case ICE_AQC_FW_LOG_ID_XLR:
		return "XLR";
	case ICE_AQC_FW_LOG_ID_NVM:
		return "NVM";
	case ICE_AQC_FW_LOG_ID_AUTH:
		return "Authentication";
	case ICE_AQC_FW_LOG_ID_VPD:
		return "VPD";
	case ICE_AQC_FW_LOG_ID_IOSF:
		return "IOSF";
	case ICE_AQC_FW_LOG_ID_PARSER:
		return "Parser";
	case ICE_AQC_FW_LOG_ID_SW:
		return "Switch";
	case ICE_AQC_FW_LOG_ID_SCHEDULER:
		return "Scheduler";
	case ICE_AQC_FW_LOG_ID_TXQ:
		return "Tx Queue Management";
	case ICE_AQC_FW_LOG_ID_ACL:
		return "ACL";
	case ICE_AQC_FW_LOG_ID_POST:
		return "Post";
	case ICE_AQC_FW_LOG_ID_WATCHDOG:
		return "Watchdog";
	case ICE_AQC_FW_LOG_ID_TASK_DISPATCH:
		return "Task Dispatcher";
	case ICE_AQC_FW_LOG_ID_MNG:
		return "Manageability";
	case ICE_AQC_FW_LOG_ID_SYNCE:
		return "Synce";
	case ICE_AQC_FW_LOG_ID_HEALTH:
		return "Health";
	case ICE_AQC_FW_LOG_ID_TSDRV:
		return "Time Sync";
	case ICE_AQC_FW_LOG_ID_PFREG:
		return "PF Registration";
	case ICE_AQC_FW_LOG_ID_MDLVER:
		return "Module Version";
	default:
		return "Unsupported";
	}
}

static const char *log_level_to_name(u8 log_level)
{
	switch (log_level) {
	case ICE_FWLOG_LEVEL_NONE:
		return "None";
	case ICE_FWLOG_LEVEL_ERROR:
		return "Error";
	case ICE_FWLOG_LEVEL_WARNING:
		return "Warning";
	case ICE_FWLOG_LEVEL_NORMAL:
		return "Normal";
	case ICE_FWLOG_LEVEL_VERBOSE:
		return "Verbose";
	default:
		return "Unsupported";
	}
}

/**
 * ice_fwlog_dump_cfg - Dump current FW logging configuration
 * @hw: pointer to the HW structure
 */
static void ice_fwlog_dump_cfg(struct ice_hw *hw)
{
	struct device *dev = ice_pf_to_dev((struct ice_pf *)(hw->back));
	struct ice_fwlog_cfg *cfg;
	enum ice_status status;
	u16 i;

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return;

	status = ice_fwlog_get(hw, cfg);
	if (status) {
		kfree(cfg);
		return;
	}

	dev_info(dev, "FWLOG Configuration:\n");
	dev_info(dev, "Options: 0x%04x\n", cfg->options);
	dev_info(dev, "\tarq_ena: %s\n",
		 (cfg->options &
		  ICE_FWLOG_OPTION_ARQ_ENA) ? "true" : "false");
	dev_info(dev, "\tuart_ena: %s\n",
		 (cfg->options &
		  ICE_FWLOG_OPTION_UART_ENA) ? "true" : "false");
	dev_info(dev, "\tPF registered: %s\n",
		 (cfg->options &
		  ICE_FWLOG_OPTION_IS_REGISTERED) ? "true" : "false");

	dev_info(dev, "Module Entries:\n");
	for (i = 0; i < ICE_AQC_FW_LOG_ID_MAX; i++) {
		struct ice_fwlog_module_entry *entry =
			&cfg->module_entries[i];

		dev_info(dev, "\tModule ID %d (%s) Log Level %d (%s)\n",
			 entry->module_id, module_id_to_name(entry->module_id),
			 entry->log_level, log_level_to_name(entry->log_level));
	}

	kfree(cfg);
}

/**
 * ice_debugfs_command_write - write into command datum
 * @filp: the opened file
 * @buf: where to find the user's data
 * @count: the length of the user's data
 * @ppos: file position offset
 */
static ssize_t
ice_debugfs_command_write(struct file *filp, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct ice_pf *pf = filp->private_data;
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	char *cmd_buf, *cmd_buf_tmp;
	ssize_t ret;
	char **argv;
	int argc;

	/* don't allow partial writes and writes when reset is in progress*/
	if (*ppos != 0 || ice_is_reset_in_progress(pf->state))
		return 0;

	cmd_buf = memdup_user(buf, count + 1);
	if (IS_ERR(cmd_buf))
		return PTR_ERR(cmd_buf);
	cmd_buf[count] = '\0';

	cmd_buf_tmp = strchr(cmd_buf, '\n');
	if (cmd_buf_tmp) {
		*cmd_buf_tmp = '\0';
		count = (size_t)cmd_buf_tmp - (size_t)cmd_buf + 1;
	}

	argv = argv_split(GFP_KERNEL, cmd_buf, &argc);
	if (!argv) {
		ret = -ENOMEM;
		goto err_copy_from_user;
	}

	if (argc > 1 && !strncmp(argv[1], "vsi", 3)) {
		if (argc == 3 && !strncmp(argv[0], "get", 3)) {
			struct ice_vsi_ctx *vsi_ctx;

			vsi_ctx = devm_kzalloc(dev, sizeof(*vsi_ctx),
					       GFP_KERNEL);
			if (!vsi_ctx) {
				ret = -ENOMEM;
				goto command_write_done;
			}
			ret = kstrtou16(argv[2], 0, &vsi_ctx->vsi_num);
			if (ret) {
				devm_kfree(dev, vsi_ctx);
				goto command_help;
			}
			ret = ice_aq_get_vsi_params(hw, vsi_ctx, NULL);
			if (ret) {
				ret = -EINVAL;
				devm_kfree(dev, vsi_ctx);
				goto command_help;
			}

			ice_vsi_dump_ctxt(dev, vsi_ctx);
			devm_kfree(dev, vsi_ctx);
		} else {
			goto command_help;
		}
	} else if (argc == 2 && !strncmp(argv[0], "dump", 4) &&
		   !strncmp(argv[1], "switch", 6)) {
		ret = ice_dump_sw_cfg(hw);
		if (ret) {
			ret = -EINVAL;
			dev_err(dev, "dump switch failed\n");
			goto command_write_done;
		}
	} else if (argc == 2 && !strncmp(argv[0], "dump", 4) &&
		   !strncmp(argv[1], "capabilities", 11)) {
		ice_dump_caps(hw);
	} else if (argc == 2 && !strncmp(argv[0], "dump", 4) &&
		   !strncmp(argv[1], "fwlog_cfg", 9)) {
		ice_fwlog_dump_cfg(&pf->hw);
	} else if (argc == 4 && !strncmp(argv[0], "dump", 4) &&
		   !strncmp(argv[1], "ptp", 3) &&
		   !strncmp(argv[2], "func", 4) &&
		   !strncmp(argv[3], "capabilities", 11)) {
		ice_dump_ptp_func_caps(hw);
	} else if (argc == 4 && !strncmp(argv[0], "dump", 4) &&
		   !strncmp(argv[1], "ptp", 3) &&
		   !strncmp(argv[2], "dev", 3) &&
		   !strncmp(argv[3], "capabilities", 11)) {
		ice_dump_ptp_dev_caps(hw);
	} else if (argc == 2 && !strncmp(argv[0], "dump", 4) &&
		   !strncmp(argv[1], "ports", 5)) {
		dev_info(dev, "port_info:\n");
		ice_dump_port_info(hw->port_info);
#ifdef ICE_ADD_PROBES
	} else if (argc == 2 && !strncmp(argv[0], "dump", 4) &&
		   !strncmp(argv[1], "arfs_stats", 10)) {
		struct ice_vsi *vsi = ice_get_main_vsi(pf);

		if (!vsi) {
			dev_err(dev, "Failed to find PF VSI\n");
		} else if (vsi->netdev->features & NETIF_F_NTUPLE) {
			struct ice_arfs_active_fltr_cntrs *fltr_cntrs;

			fltr_cntrs = vsi->arfs_fltr_cntrs;

			/* active counters can be updated by multiple CPUs */
			smp_mb__before_atomic();
			dev_info(dev, "arfs_active_tcpv4_filters: %d\n",
				 atomic_read(&fltr_cntrs->active_tcpv4_cnt));
			dev_info(dev, "arfs_active_tcpv6_filters: %d\n",
				 atomic_read(&fltr_cntrs->active_tcpv6_cnt));
			dev_info(dev, "arfs_active_udpv4_filters: %d\n",
				 atomic_read(&fltr_cntrs->active_udpv4_cnt));
			dev_info(dev, "arfs_active_udpv6_filters: %d\n",
				 atomic_read(&fltr_cntrs->active_udpv6_cnt));
		}
#endif /* ICE_ADD_PROBES */
	} else if (argc == 2 && !strncmp(argv[0], "dump", 4)) {
		if (!strncmp(argv[1], "mmcast", 6)) {
			ice_dump_sw_rules(hw, ICE_SW_LKUP_MAC);
		} else if (!strncmp(argv[1], "vlan", 4)) {
			ice_dump_sw_rules(hw, ICE_SW_LKUP_VLAN);
		} else if (!strncmp(argv[1], "eth", 3)) {
			ice_dump_sw_rules(hw, ICE_SW_LKUP_ETHERTYPE);
		} else if (!strncmp(argv[1], "pf_vsi", 6)) {
			ice_dump_pf_vsi_list(pf);
		} else if (!strncmp(argv[1], "pf_port_num", 11)) {
			dev_info(dev, "pf_id = %d, port_num = %d\n",
				 hw->pf_id, hw->port_info->lport);
		} else if (!strncmp(argv[1], "pf", 2)) {
			ice_dump_pf(pf);
		} else if (!strncmp(argv[1], "vfs", 3)) {
			ice_dump_all_vfs(pf);
		} else if (!strncmp(argv[1], "fdir_stats", 10)) {
			ice_dump_pf_fdir(pf);
		} else if (!strncmp(argv[1], "reset_stats", 11)) {
			dev_info(dev, "core reset count: %d\n",
				 pf->corer_count);
			dev_info(dev, "global reset count: %d\n",
				 pf->globr_count);
			dev_info(dev, "emp reset count: %d\n", pf->empr_count);
			dev_info(dev, "pf reset count: %d\n", pf->pfr_count);
		}

#ifdef CONFIG_DCB
	} else if (argc == 3 && !strncmp(argv[0], "lldp", 4) &&
				!strncmp(argv[1], "get", 3)) {
		u8 mibtype;
		u16 llen, rlen;
		u8 *buff;

		if (!strncmp(argv[2], "local", 5))
			mibtype = ICE_AQ_LLDP_MIB_LOCAL;
		else if (!strncmp(argv[2], "remote", 6))
			mibtype = ICE_AQ_LLDP_MIB_REMOTE;
		else
			goto command_help;

		buff = devm_kzalloc(dev, ICE_LLDPDU_SIZE, GFP_KERNEL);
		if (!buff) {
			ret = -ENOMEM;
			goto command_write_done;
		}

		ret = ice_aq_get_lldp_mib(hw,
					  ICE_AQ_LLDP_BRID_TYPE_NEAREST_BRID,
					  mibtype, (void *)buff,
					  ICE_LLDPDU_SIZE,
					  &llen, &rlen, NULL);

		if (!ret) {
			if (mibtype == ICE_AQ_LLDP_MIB_LOCAL) {
				dev_info(dev, "LLDP MIB (local)\n");
				print_hex_dump(KERN_INFO, "LLDP MIB (local): ",
					       DUMP_PREFIX_OFFSET, 16, 1,
					       buff, llen, true);
			} else if (mibtype == ICE_AQ_LLDP_MIB_REMOTE) {
				dev_info(dev, "LLDP MIB (remote)\n");
				print_hex_dump(KERN_INFO, "LLDP MIB (remote): ",
					       DUMP_PREFIX_OFFSET, 16, 1,
					       buff, rlen, true);
			}
		} else {
			dev_err(dev, "GET LLDP MIB failed. Status: %ld\n", ret);
		}
		devm_kfree(dev, buff);
#endif /* CONFIG_DCB */
	} else if ((argc > 1) && !strncmp(argv[1], "scheduling", 10)) {
		if (argc == 4 && !strncmp(argv[0], "get", 3) &&
		    !strncmp(argv[2], "tree", 4) &&
		    !strncmp(argv[3], "topology", 8)) {
			ice_dump_port_topo(hw->port_info);
		}
	} else if (argc == 4 && !strncmp(argv[0], "set_ts_pll", 10)) {
		u8 time_ref_freq;
		u8 time_ref_sel;
		u8 src_tmr_mode;

		ret = kstrtou8(argv[1], 0, &time_ref_freq);
		if (ret)
			goto command_help;
		ret = kstrtou8(argv[2], 0, &time_ref_sel);
		if (ret)
			goto command_help;
		ret = kstrtou8(argv[3], 0, &src_tmr_mode);
		if (ret)
			goto command_help;

		ice_cgu_cfg_ts_pll(pf, false, (enum ice_time_ref_freq)time_ref_freq,
				   (enum ice_cgu_time_ref_sel)time_ref_sel,
				   (enum ice_src_tmr_mode)src_tmr_mode);
		ice_cgu_cfg_ts_pll(pf, true, (enum ice_time_ref_freq)time_ref_freq,
				   (enum ice_cgu_time_ref_sel)time_ref_sel,
				   (enum ice_src_tmr_mode)src_tmr_mode);
	} else {
command_help:
		dev_info(dev, "unknown or invalid command '%s'\n", cmd_buf);
		dev_info(dev, "available commands\n");
		dev_info(dev, "\t get vsi <vsinum>\n");
		dev_info(dev, "\t dump switch\n");
		dev_info(dev, "\t dump ports\n");
		dev_info(dev, "\t dump capabilities\n");
		dev_info(dev, "\t dump fwlog_cfg\n");
		dev_info(dev, "\t dump ptp func capabilities\n");
		dev_info(dev, "\t dump ptp dev capabilities\n");
		dev_info(dev, "\t dump mmcast\n");
		dev_info(dev, "\t dump vlan\n");
		dev_info(dev, "\t dump eth\n");
		dev_info(dev, "\t dump pf_vsi\n");
		dev_info(dev, "\t dump pf\n");
		dev_info(dev, "\t dump pf_port_num\n");
		dev_info(dev, "\t dump vfs\n");
		dev_info(dev, "\t dump reset_stats\n");
		dev_info(dev, "\t dump fdir_stats\n");
		dev_info(dev, "\t get scheduling tree topology\n");
		dev_info(dev, "\t get scheduling tree topology portnum <port>\n");
#ifdef CONFIG_DCB
		dev_info(dev, "\t lldp get local\n");
		dev_info(dev, "\t lldp get remote\n");
#endif /* CONFIG_DCB */
#ifdef ICE_ADD_PROBES
		dev_info(dev, "\t dump arfs_stats\n");
#endif /* ICE_ADD_PROBES */
		ret = -EINVAL;
		goto command_write_done;
	}

	/* if we get here, nothing went wrong; return bytes copied */
	ret = (ssize_t)count;

command_write_done:
	argv_free(argv);
err_copy_from_user:
	kfree(cmd_buf);
	return ret;
}

static const struct file_operations ice_debugfs_command_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.write = ice_debugfs_command_write,
};

/**
 * ice_debugfs_pf_init - setup the debugfs directory
 * @pf: the ice that is starting up
 */
void ice_debugfs_pf_init(struct ice_pf *pf)
{
	const char *name = pci_name(pf->pdev);
	struct dentry *pfile;

	pf->ice_debugfs_pf = debugfs_create_dir(name, ice_debugfs_root);
	if (IS_ERR(pf->ice_debugfs_pf))
		return;

	pfile = debugfs_create_file("command", 0600, pf->ice_debugfs_pf, pf,
				    &ice_debugfs_command_fops);
	if (!pfile)
		goto create_failed;

	return;

create_failed:
	dev_err(ice_pf_to_dev(pf), "debugfs dir/file for %s failed\n", name);
	debugfs_remove_recursive(pf->ice_debugfs_pf);
}

/**
 * ice_debugfs_pf_exit - clear out the ices debugfs entries
 * @pf: the ice that is stopping
 */
void ice_debugfs_pf_exit(struct ice_pf *pf)
{
	debugfs_remove_recursive(pf->ice_debugfs_pf);
	pf->ice_debugfs_pf = NULL;
}

/**
 * ice_debugfs_init - create root directory for debugfs entries
 */
void ice_debugfs_init(void)
{
	ice_debugfs_root = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (IS_ERR(ice_debugfs_root))
		pr_info("init of debugfs failed\n");
}

/**
 * ice_debugfs_exit - remove debugfs entries
 */
void ice_debugfs_exit(void)
{
	debugfs_remove_recursive(ice_debugfs_root);
	ice_debugfs_root = NULL;
}
