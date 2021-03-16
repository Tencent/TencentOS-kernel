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
#define pr_fmt(fmt) KBUILD_MODNAME ": [COMM]" fmt

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/io-mapping.h>
#include <linux/interrupt.h>
#include <linux/inetdevice.h>
#include <net/addrconf.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/rtc.h>
#include <linux/aer.h>
#include <linux/debugfs.h>

#include "ossl_knl.h"
#include "hinic_hw_mgmt.h"
#include "hinic_hw.h"
#include "hinic_lld.h"
#include "hinic_pci_id_tbl.h"
#include "hinic_nic_dev.h"
#include "hinic_sriov.h"
#include "hinic_dbgtool_knl.h"
#include "hinic_nictool.h"

#define HINIC_PCI_CFG_REG_BAR		0
#define HINIC_PCI_INTR_REG_BAR		2
#define HINIC_PCI_DB_BAR		4
#define HINIC_PCI_VENDOR_ID		0x19e5

#define SELF_TEST_BAR_ADDR_OFFSET	0x883c

#define HINIC_SECOND_BASE (1000)
#define HINIC_SYNC_YEAR_OFFSET (1900)
#define HINIC_SYNC_MONTH_OFFSET (1)
#define HINIC_MINUTE_BASE (60)
#define HINIC_WAIT_TOOL_CNT_TIMEOUT	10000
#define HINIC_WAIT_SRIOV_CFG_TIMEOUT	15000

#define HINIC_DRV_DESC "Huawei(R) Intelligent Network Interface Card Driver"
#define HINICVF_DRV_DESC "Huawei(R) Intelligent Virtual Function Network Driver"

MODULE_AUTHOR("Huawei Technologies CO., Ltd");
MODULE_DESCRIPTION(HINIC_DRV_DESC);
MODULE_VERSION(HINIC_DRV_VERSION);
MODULE_LICENSE("GPL");

#if !(defined(HAVE_SRIOV_CONFIGURE) || defined(HAVE_RHEL6_SRIOV_CONFIGURE))
static DEVICE_ATTR(sriov_numvfs, 0664,
			hinic_sriov_numvfs_show, hinic_sriov_numvfs_store);
static DEVICE_ATTR(sriov_totalvfs, 0444,
			hinic_sriov_totalvfs_show, NULL);
#endif /* !(HAVE_SRIOV_CONFIGURE || HAVE_RHEL6_SRIOV_CONFIGURE) */

static struct attribute *hinic_attributes[] = {
#if !(defined(HAVE_SRIOV_CONFIGURE) || defined(HAVE_RHEL6_SRIOV_CONFIGURE))
	&dev_attr_sriov_numvfs.attr,
	&dev_attr_sriov_totalvfs.attr,
#endif /* !(HAVE_SRIOV_CONFIGURE || HAVE_RHEL6_SRIOV_CONFIGURE) */
	NULL
};

static const struct attribute_group hinic_attr_group = {
	.attrs		= hinic_attributes,
};

#ifdef CONFIG_PCI_IOV
static bool disable_vf_load;
module_param(disable_vf_load, bool, 0444);
MODULE_PARM_DESC(disable_vf_load,
		 "Disable virtual functions probe or not - default is false");
#endif /* CONFIG_PCI_IOV */

enum {
	HINIC_FUNC_IN_REMOVE = BIT(0),
	HINIC_FUNC_PRB_ERR = BIT(1),
	HINIC_FUNC_PRB_DELAY = BIT(2),
};

/* Structure pcidev private */
struct hinic_pcidev {
	struct pci_dev *pcidev;
	void *hwdev;
	struct card_node *chip_node;
	struct hinic_lld_dev lld_dev;
	/* Record the service object address,
	 * such as hinic_dev and toe_dev, fc_dev
	 */
	void *uld_dev[SERVICE_T_MAX];
	/* Record the service object name */
	char uld_dev_name[SERVICE_T_MAX][IFNAMSIZ];
	/* It is a the global variable for driver to manage
	 * all function device linked list
	 */
	struct list_head node;

	void __iomem *cfg_reg_base;
	void __iomem *intr_reg_base;
	u64 db_base_phy;
	void __iomem *db_base;

#if defined(__aarch64__)
	void __iomem *dwqe_mapping;
#else
	struct io_mapping *dwqe_mapping;
#endif
	/* lock for attach/detach uld */
	struct mutex pdev_mutex;
	struct hinic_sriov_info sriov_info;

	u32 init_state;
	/* setted when uld driver processing event */
	unsigned long state;
	struct pci_device_id id;

	unsigned long flag;

	struct work_struct slave_nic_work;
	struct workqueue_struct *slave_nic_init_workq;
	struct delayed_work slave_nic_init_dwork;
	enum hinic_chip_mode chip_mode;
	bool nic_cur_enable;
	bool nic_des_enable;
};

#define HINIC_EVENT_PROCESS_TIMEOUT	10000

#define FIND_BIT(num, n)	(((num) & (1UL << (n))) ? 1 : 0)
#define SET_BIT(num, n)		((num) | (1UL << (n)))
#define CLEAR_BIT(num, n)	((num) & (~(1UL << (n))))

#define MAX_CARD_ID 64
static u64 card_bit_map;
LIST_HEAD(g_hinic_chip_list);
struct hinic_uld_info g_uld_info[SERVICE_T_MAX] = { {0} };
static const char *s_uld_name[SERVICE_T_MAX] = {
	"nic", "ovs", "roce", "toe", "iwarp", "fc", "fcoe", "migrate"};

enum hinic_lld_status {
	HINIC_NODE_CHANGE	= BIT(0),
};

struct hinic_lld_lock {
	/* lock for chip list */
	struct mutex		lld_mutex;
	unsigned long		status;
	atomic_t		dev_ref_cnt;
};

struct hinic_lld_lock g_lld_lock;

#define WAIT_LLD_DEV_HOLD_TIMEOUT	(10 * 60 * 1000)	/* 10minutes */
#define WAIT_LLD_DEV_NODE_CHANGED	(10 * 60 * 1000)	/* 10minutes */
#define WAIT_LLD_DEV_REF_CNT_EMPTY	(2 * 60 * 1000)		/* 2minutes */

/* node in chip_node will changed, tools or driver can't get node
 * during this situation
 */
static void lld_lock_chip_node(void)
{
	u32 loop_cnt;

	mutex_lock(&g_lld_lock.lld_mutex);

	loop_cnt = 0;
	while (loop_cnt < WAIT_LLD_DEV_NODE_CHANGED) {
		if (!test_and_set_bit(HINIC_NODE_CHANGE, &g_lld_lock.status))
			break;

		loop_cnt++;

		if (loop_cnt % 10000 == 0)
			pr_warn("Wait for lld node change complete for %us\n",
				loop_cnt / 1000);

		usleep_range(900, 1000);
	}

	if (loop_cnt == WAIT_LLD_DEV_NODE_CHANGED)
		pr_warn("Wait for lld node change complete timeout when try to get lld lock\n");

	loop_cnt = 0;
	while (loop_cnt < WAIT_LLD_DEV_REF_CNT_EMPTY) {
		if (!atomic_read(&(g_lld_lock.dev_ref_cnt)))
			break;

		loop_cnt++;

		if (loop_cnt % 10000 == 0)
			pr_warn("Wait for lld dev unused for %us, reference count: %d\n",
				loop_cnt / 1000,
				atomic_read(&(g_lld_lock.dev_ref_cnt)));

		usleep_range(900, 1000);
	}

	if (loop_cnt == WAIT_LLD_DEV_REF_CNT_EMPTY)
		pr_warn("Wait for lld dev unused timeout\n");

	mutex_unlock(&g_lld_lock.lld_mutex);
}

static void lld_unlock_chip_node(void)
{
	clear_bit(HINIC_NODE_CHANGE, &g_lld_lock.status);
}

/* When tools or other drivers want to get node of chip_node, use this function
 * to prevent node be freed
 */
static void lld_dev_hold(void)
{
	u32 loop_cnt = 0;

	/* ensure there have not any chip node in changing */
	mutex_lock(&g_lld_lock.lld_mutex);

	while (loop_cnt < WAIT_LLD_DEV_HOLD_TIMEOUT) {
		if (!test_bit(HINIC_NODE_CHANGE, &g_lld_lock.status))
			break;

		loop_cnt++;

		if (loop_cnt % 10000 == 0)
			pr_warn("Wait lld node change complete for %us\n",
				loop_cnt / 1000);

		usleep_range(900, 1000);
	}

	if (loop_cnt == WAIT_LLD_DEV_HOLD_TIMEOUT)
		pr_warn("Wait lld node change complete timeout when try to hode lld dev\n");

	atomic_inc(&g_lld_lock.dev_ref_cnt);

	mutex_unlock(&g_lld_lock.lld_mutex);
}

static void lld_dev_put(void)
{
	atomic_dec(&g_lld_lock.dev_ref_cnt);
}

static void hinic_lld_lock_init(void)
{
	mutex_init(&g_lld_lock.lld_mutex);
	atomic_set(&g_lld_lock.dev_ref_cnt, 0);
}

static atomic_t tool_used_cnt;

void hinic_tool_cnt_inc(void)
{
	atomic_inc(&tool_used_cnt);
}

void hinic_tool_cnt_dec(void)
{
	atomic_dec(&tool_used_cnt);
}


static int attach_uld(struct hinic_pcidev *dev, enum hinic_service_type type,
		      struct hinic_uld_info *uld_info)
{
	void *uld_dev = NULL;
	int err;
	enum hinic_service_mode service_mode =
					hinic_get_service_mode(dev->hwdev);

	mutex_lock(&dev->pdev_mutex);

	if (dev->init_state < HINIC_INIT_STATE_HWDEV_INITED) {
		sdk_err(&dev->pcidev->dev, "SDK init failed, can not attach uld\n");
		err = -EFAULT;
		goto out_unlock;
	}

	if (dev->uld_dev[type]) {
		sdk_err(&dev->pcidev->dev,
			"%s driver has attached to pcie device\n",
			s_uld_name[type]);
		err = 0;
		goto out_unlock;
	}

	if ((hinic_get_func_mode(dev->hwdev) == FUNC_MOD_NORMAL_HOST) &&
	    (service_mode != HINIC_WORK_MODE_OVS_SP) &&
	    type == SERVICE_T_OVS && !hinic_support_ovs(dev->hwdev, NULL)) {
		sdk_warn(&dev->pcidev->dev, "Dev not support %s\n",
			 s_uld_name[type]);
		err = 0;
		goto out_unlock;
	}

	err = uld_info->probe(&dev->lld_dev, &uld_dev, dev->uld_dev_name[type]);
	if (err || !uld_dev) {
		sdk_err(&dev->pcidev->dev,
			"Failed to add object for %s driver to pcie device\n",
			s_uld_name[type]);
		goto probe_failed;
	}

	dev->uld_dev[type] = uld_dev;
	mutex_unlock(&dev->pdev_mutex);

	sdk_info(&dev->pcidev->dev,
		 "Attach %s driver to pcie device succeed\n", s_uld_name[type]);
	return 0;

probe_failed:
out_unlock:
	mutex_unlock(&dev->pdev_mutex);

	return err;
}

static void detach_uld(struct hinic_pcidev *dev, enum hinic_service_type type)
{
	struct hinic_uld_info *uld_info = &g_uld_info[type];
	u32 cnt = 0;

	mutex_lock(&dev->pdev_mutex);
	if (!dev->uld_dev[type]) {
		mutex_unlock(&dev->pdev_mutex);
		return;
	}

	while (cnt < HINIC_EVENT_PROCESS_TIMEOUT) {
		if (!test_and_set_bit(type, &dev->state))
			break;
		usleep_range(900, 1000);
		cnt++;
	}

	uld_info->remove(&dev->lld_dev, dev->uld_dev[type]);
	dev->uld_dev[type] = NULL;
	if (cnt < HINIC_EVENT_PROCESS_TIMEOUT)
		clear_bit(type, &dev->state);

	sdk_info(&dev->pcidev->dev,
		 "Detach %s driver from pcie device succeed\n",
		 s_uld_name[type]);
	mutex_unlock(&dev->pdev_mutex);
}

static void attach_ulds(struct hinic_pcidev *dev)
{
	enum hinic_service_type type;

	for (type = SERVICE_T_OVS; type < SERVICE_T_MAX; type++) {
		if (g_uld_info[type].probe)
			attach_uld(dev, type, &g_uld_info[type]);
	}
}

static void detach_ulds(struct hinic_pcidev *dev)
{
	enum hinic_service_type type;

	for (type = SERVICE_T_MAX - 1; type > SERVICE_T_NIC; type--) {
		if (g_uld_info[type].probe)
			detach_uld(dev, type);
	}
}

int hinic_register_uld(enum hinic_service_type type,
		       struct hinic_uld_info *uld_info)
{
	struct card_node *chip_node;
	struct hinic_pcidev *dev;

	if (type >= SERVICE_T_MAX) {
		pr_err("Unknown type %d of up layer driver to register\n",
		       type);
		return -EINVAL;
	}

	if (!uld_info || !uld_info->probe || !uld_info->remove) {
		pr_err("Invalid information of %s driver to register\n",
		       s_uld_name[type]);
		return -EINVAL;
	}

	lld_dev_hold();

	if (g_uld_info[type].probe) {
		pr_err("%s driver has registered\n", s_uld_name[type]);
		lld_dev_put();
		return -EINVAL;
	}

	memcpy(&g_uld_info[type], uld_info, sizeof(*uld_info));
	list_for_each_entry(chip_node, &g_hinic_chip_list, node) {
		list_for_each_entry(dev, &chip_node->func_list, node) {
			if (attach_uld(dev, type, uld_info)) {
				sdk_err(&dev->pcidev->dev,
					"Attach %s driver to pcie device failed\n",
					s_uld_name[type]);
				continue;
			}
		}
	}

	lld_dev_put();

	pr_info("Register %s driver succeed\n", s_uld_name[type]);
	return 0;
}
EXPORT_SYMBOL(hinic_register_uld);

void hinic_unregister_uld(enum hinic_service_type type)
{
	struct card_node *chip_node;
	struct hinic_pcidev *dev;
	struct hinic_uld_info *uld_info;

	if (type >= SERVICE_T_MAX) {
		pr_err("Unknown type %d of up layer driver to unregister\n",
		       type);
		return;
	}

	lld_dev_hold();
	list_for_each_entry(chip_node, &g_hinic_chip_list, node) {
		list_for_each_entry(dev, &chip_node->func_list, node) {
			detach_uld(dev, type);
		}
	}

	uld_info = &g_uld_info[type];
	memset(uld_info, 0, sizeof(*uld_info));
	lld_dev_put();
}
EXPORT_SYMBOL(hinic_unregister_uld);

static void hinic_sync_time_to_fmw(struct hinic_pcidev *pdev_pri)
{
	struct timeval tv = {0};
	struct rtc_time rt_time = {0};
	u64 tv_msec;
	int err;

	do_gettimeofday(&tv);

	tv_msec = tv.tv_sec * HINIC_SECOND_BASE +
			tv.tv_usec / HINIC_SECOND_BASE;
	err = hinic_sync_time(pdev_pri->hwdev, tv_msec);
	if (err) {
		sdk_err(&pdev_pri->pcidev->dev, "Synchronize UTC time to firmware failed, errno:%d.\n",
			err);
	} else {
		rtc_time_to_tm(tv.tv_sec, &rt_time);
		sdk_info(&pdev_pri->pcidev->dev, "Synchronize UTC time to firmware succeed. UTC time %d-%02d-%02d %02d:%02d:%02d.\n",
			 rt_time.tm_year + HINIC_SYNC_YEAR_OFFSET,
			 rt_time.tm_mon + HINIC_SYNC_MONTH_OFFSET,
			 rt_time.tm_mday, rt_time.tm_hour,
			 rt_time.tm_min, rt_time.tm_sec);
	}
}

enum hinic_ver_incompat_mode {
	/* New driver can't compat with old firmware */
	VER_INCOMP_NEW_DRV_OLD_FW,
	/* New Firmware can't compat with old driver */
	VER_INCOMP_NEW_FW_OLD_DRV,
};

struct hinic_version_incompat {
	char *version;
	char *advise;
	u32 incompat_mode;
};

struct hinic_version_incompat ver_incompat_table[] = {
	{
		.version = "1.2.2.0",
		.advise = "Mechanism of cos changed",
		.incompat_mode = BIT(VER_INCOMP_NEW_DRV_OLD_FW),
	},
	{
		.version = "1.2.3.0",
		.advise = "Driver get sevice mode from firmware",
		.incompat_mode = BIT(VER_INCOMP_NEW_DRV_OLD_FW),
	},
};

#define MAX_VER_FIELD_LEN	4
#define MAX_VER_SPLIT_NUM	4
static void __version_split(const char *str, int *split_num,
			    char rst[][MAX_VER_FIELD_LEN])
{
	const char delim = '.';
	const char *src;
	int cnt = 0;
	u16 idx, end, token_len;

	idx = 0;
	while (idx < strlen(str)) {
		for (end = idx; end < strlen(str); end++) {
			if (*(str + end) == delim)
				break;	/* find */
		}

		if (end != idx) {
			token_len = min_t(u16, end - idx,
					  MAX_VER_FIELD_LEN - 1);
			src = str + idx;
			memcpy(rst[cnt], src, token_len);
			if (++cnt >= MAX_VER_SPLIT_NUM)
				break;
		}

		idx = end + 1;	/* skip delim */
	}

	*split_num = cnt;
}

int hinic_version_cmp(char *ver1, char *ver2)
{
	char ver1_split[MAX_VER_SPLIT_NUM][MAX_VER_FIELD_LEN] = { {0} };
	char ver2_split[MAX_VER_SPLIT_NUM][MAX_VER_FIELD_LEN] = { {0} };
	int split1_num, split2_num;
	int ver1_num, ver2_num;
	int split;

	/* To compat older firmware version */
	if (ver1[0] == 'B')
		return -1;

	if (ver2[0] == 'B')
		return 1;

	__version_split(ver1, &split1_num, ver1_split);
	__version_split(ver2, &split2_num, ver2_split);

	if (split1_num != MAX_VER_SPLIT_NUM ||
	    split2_num != MAX_VER_SPLIT_NUM) {
		pr_err("Invalid version %s or %s\n", ver1, ver2);
		return 0;
	}

	for (split = 0; split < MAX_VER_SPLIT_NUM; split++) {
		ver1_num = local_atoi(ver1_split[split]);
		ver2_num = local_atoi(ver2_split[split]);

		if (ver1_num > ver2_num)
			return 1;
		else if (ver1_num < ver2_num)
			return -1;
	}

	return 0;
}

static int __version_mismatch(struct hinic_pcidev *pcidev, char *cur_fw_ver,
			      char *cur_drv_ver,
			      struct hinic_version_incompat *ver_incompat,
			      int start_entry)
{
	struct hinic_version_incompat *ver_incmp_tmp;
	int fw_ver_comp;
	int i, num_entry;

	fw_ver_comp = hinic_version_cmp(cur_fw_ver, ver_incompat->version);
	if (fw_ver_comp <= 0) {
		/* Check if new driver compatible with old fw */
		for (i = start_entry; i >= 0; i--) {
			ver_incmp_tmp = &ver_incompat_table[i];
			if (hinic_version_cmp(cur_fw_ver,
					      ver_incmp_tmp->version) >= 0)
				break;	/* Not need to check anymore */

			if (ver_incmp_tmp->incompat_mode &
				BIT(VER_INCOMP_NEW_DRV_OLD_FW)) {
				sdk_err(&pcidev->pcidev->dev,
					"Version incompatible: %s, please update firmware to %s, or use %s driver\n",
					ver_incmp_tmp->advise,
					cur_drv_ver, cur_fw_ver);
				return -EINVAL;
			}
		}

		goto compatible;
	}

	/* check if old driver compatible with new firmware */
	num_entry = (int)sizeof(ver_incompat_table) /
		    (int)sizeof(ver_incompat_table[0]);
	for (i = start_entry + 1; i < num_entry; i++) {
		ver_incmp_tmp = &ver_incompat_table[i];

		if (hinic_version_cmp(cur_fw_ver, ver_incmp_tmp->version) < 0)
			break;	/* Not need to check anymore */

		if (ver_incmp_tmp->incompat_mode &
			BIT(VER_INCOMP_NEW_FW_OLD_DRV)) {
			sdk_err(&pcidev->pcidev->dev,
				"Version incompatible: %s, please update driver to %s, or use %s firmware\n",
				ver_incmp_tmp->advise,
				cur_fw_ver, cur_drv_ver);
			return -EINVAL;
		}
	}

compatible:
	if (hinic_version_cmp(cur_drv_ver, cur_fw_ver) < 0)
		sdk_info(&pcidev->pcidev->dev,
			 "Firmware newer than driver, you'd better update driver to %s\n",
			 cur_fw_ver);
	else
		sdk_info(&pcidev->pcidev->dev,
			 "Driver newer than firmware, you'd better update firmware to %s\n",
			 cur_drv_ver);

	return 0;
}

static void hinic_ignore_minor_version(char *version)
{
	char ver_split[MAX_VER_SPLIT_NUM][MAX_VER_FIELD_LEN] = { {0} };
	int max_ver_len, split_num = 0;
	int err;

	__version_split(version, &split_num, ver_split);
	if (split_num != MAX_VER_SPLIT_NUM)
		return;

	max_ver_len = (int)strlen(version) + 1;
	memset(version, 0, max_ver_len);

	err = snprintf(version, max_ver_len, "%s.%s.%s.0",
		       ver_split[0], ver_split[1], ver_split[2]);
	if (err <= 0 || err >= max_ver_len)
		pr_err("Failed snprintf version, function return(%d) and dest_len(%d)\n",
		       err, max_ver_len);
}

static int hinic_detect_version_compatible(struct hinic_pcidev *pcidev)
{
	struct hinic_fw_version fw_ver = { {0} };
	struct hinic_version_incompat *ver_incompat;
	char drv_ver[MAX_VER_SPLIT_NUM * MAX_VER_FIELD_LEN] = {0};
	int idx, num_entry, drv_ver_len;
	int ver_mismatch;
	int err;

	err = hinic_get_fw_version(pcidev->hwdev, &fw_ver);
	if (err) {
		sdk_err(&pcidev->pcidev->dev,
			"Failed to get firmware version\n");
		return err;
	}

	drv_ver_len = min_t(int, (int)sizeof(drv_ver) - 1,
			    (int)strlen(HINIC_DRV_VERSION));
	memcpy(drv_ver, HINIC_DRV_VERSION, drv_ver_len);

	sdk_info(&pcidev->pcidev->dev, "Version info: driver %s, firmware %s\n",
		 drv_ver, fw_ver.mgmt_ver);

	hinic_ignore_minor_version(fw_ver.mgmt_ver);
	hinic_ignore_minor_version(drv_ver);
	ver_mismatch = hinic_version_cmp(drv_ver, fw_ver.mgmt_ver);
	if (!ver_mismatch)
		return 0;

	num_entry = (int)sizeof(ver_incompat_table) /
		    (int)sizeof(ver_incompat_table[0]);
	for (idx = num_entry - 1; idx >= 0; idx--) {
		ver_incompat = &ver_incompat_table[idx];

		if (hinic_version_cmp(drv_ver, ver_incompat->version) < 0)
			continue;

		/* Find older verion of driver in table */
		return __version_mismatch(pcidev, fw_ver.mgmt_ver, drv_ver,
					  ver_incompat, idx);
	}

	return 0;
}

struct mctp_hdr {
	u16	resp_code;
	u16	reason_code;
	u32	manufacture_id;

	u8	cmd_rsvd;
	u8	major_cmd;
	u8	sub_cmd;
	u8	spc_field;
};

struct mctp_bdf_info {
	struct mctp_hdr hdr;	/* spc_field: pf index */
	u8		rsvd;
	u8		bus;
	u8		device;
	u8		function;
};

enum mctp_resp_code {
	/* COMMAND_COMPLETED = 0, */
	/* COMMAND_FAILED = 1, */
	/* COMMAND_UNAVALILABLE = 2, */
	COMMAND_UNSUPPORTED = 3,
};

static void __mctp_set_hdr(struct mctp_hdr *hdr,
			   struct hinic_mctp_host_info *mctp_info)
{
	u32 manufacture_id = 0x07DB;

	hdr->cmd_rsvd = 0;
	hdr->major_cmd = mctp_info->major_cmd;
	hdr->sub_cmd = mctp_info->sub_cmd;
	hdr->manufacture_id = cpu_to_be32(manufacture_id);
	hdr->resp_code = cpu_to_be16(hdr->resp_code);
	hdr->reason_code = cpu_to_be16(hdr->reason_code);
}

static void __mctp_get_bdf(struct hinic_pcidev *pci_adapter,
			   struct hinic_mctp_host_info *mctp_info)
{
	struct pci_dev *pdev = pci_adapter->pcidev;
	struct mctp_bdf_info *bdf_info = mctp_info->data;

	bdf_info->bus = pdev->bus->number;
	bdf_info->device = (u8)(pdev->devfn >> 3);	/* 5bits in devfn */
	bdf_info->function = (u8)(pdev->devfn & 0x7);	/* 3bits in devfn */

	memset(&bdf_info->hdr, 0, sizeof(bdf_info->hdr));
	__mctp_set_hdr(&bdf_info->hdr, mctp_info);
	bdf_info->hdr.spc_field =
		(u8)hinic_global_func_id_hw(pci_adapter->hwdev);

	mctp_info->data_len = sizeof(*bdf_info);
}

#define MCTP_MAJOR_CMD_PUBLIC		0x0
#define MCTP_MAJOR_CMD_NIC		0x1

#define MCTP_PUBLIC_SUB_CMD_BDF		0x1
#define MCTP_PUBLIC_SUB_CMD_DRV		0x4

#define MCTP_NIC_SUB_CMD_IP		0x1

static void __mctp_get_host_info(struct hinic_pcidev *dev,
				 struct hinic_mctp_host_info *mctp_info)
{
	struct mctp_hdr *hdr;

	switch ((((u16)mctp_info->major_cmd) << 8) | mctp_info->sub_cmd) {
	case (MCTP_MAJOR_CMD_PUBLIC << 8 | MCTP_PUBLIC_SUB_CMD_BDF):
		__mctp_get_bdf(dev, mctp_info);
		break;

	default:
		hdr = mctp_info->data;
		hdr->reason_code = COMMAND_UNSUPPORTED;
		__mctp_set_hdr(hdr, mctp_info);
		mctp_info->data_len = sizeof(*hdr);
		break;
	}
}

static bool __is_pcidev_match_chip_name(const char *ifname,
					struct hinic_pcidev *dev,
					struct card_node *chip_node,
					enum func_type type)
{
	if (!strncmp(chip_node->chip_name, ifname, IFNAMSIZ)) {
		if (type == TYPE_UNKNOWN) {
			if (dev->init_state < HINIC_INIT_STATE_HW_PART_INITED)
				return false;
		} else {
			if (dev->init_state >=
			    HINIC_INIT_STATE_HW_PART_INITED &&
			    hinic_func_type(dev->hwdev) != type)
				return false;
		}

		return true;
	}

	return false;
}

static struct hinic_pcidev *_get_pcidev_by_chip_name(char *ifname,
						     enum func_type type)
{
	struct card_node *chip_node;
	struct hinic_pcidev *dev;

	lld_dev_hold();
	list_for_each_entry(chip_node, &g_hinic_chip_list, node) {
		list_for_each_entry(dev, &chip_node->func_list, node) {
			if (test_bit(HINIC_FUNC_IN_REMOVE, &dev->flag))
				continue;

			if (__is_pcidev_match_chip_name(ifname, dev, chip_node,
							type)) {
				lld_dev_put();
				return dev;
			}
		}
	}

	lld_dev_put();

	return NULL;
}

static struct hinic_pcidev *hinic_get_pcidev_by_chip_name(char *ifname)
{
	struct hinic_pcidev *dev, *dev_hw_init;

	/* find hw init device first */
	dev_hw_init = _get_pcidev_by_chip_name(ifname, TYPE_UNKNOWN);
	if (dev_hw_init) {
		if (hinic_func_type(dev_hw_init->hwdev) == TYPE_PPF)
			return dev_hw_init;
	}

	dev = _get_pcidev_by_chip_name(ifname, TYPE_PPF);
	if (dev) {
		if (dev_hw_init && (dev_hw_init->init_state >= dev->init_state))
			return dev_hw_init;

		return dev;
	}

	dev = _get_pcidev_by_chip_name(ifname, TYPE_PF);
	if (dev) {
		if (dev_hw_init && (dev_hw_init->init_state >= dev->init_state))
			return dev_hw_init;

		return dev;
	}

	dev = _get_pcidev_by_chip_name(ifname, TYPE_VF);
	if (dev)
		return dev;

	return NULL;
}

static bool __is_pcidev_match_dev_name(const char *ifname,
				       struct hinic_pcidev *dev,
				       enum hinic_service_type type)
{
	struct hinic_nic_dev *nic_dev;
	enum hinic_service_type i;

	if (type == SERVICE_T_MAX) {
		for (i = SERVICE_T_OVS; i < SERVICE_T_MAX; i++) {
			if (!strncmp(dev->uld_dev_name[i], ifname, IFNAMSIZ))
				return true;
		}
	} else {
		if (!strncmp(dev->uld_dev_name[type], ifname, IFNAMSIZ))
			return true;
	}

	nic_dev = dev->uld_dev[SERVICE_T_NIC];
	if (nic_dev) {
		if (!strncmp(nic_dev->netdev->name, ifname, IFNAMSIZ))
			return true;
	}

	return false;
}

static struct hinic_pcidev *
	hinic_get_pcidev_by_dev_name(char *ifname, enum hinic_service_type type)
{
	struct card_node *chip_node;
	struct hinic_pcidev *dev;

	lld_dev_hold();
	list_for_each_entry(chip_node, &g_hinic_chip_list, node) {
		list_for_each_entry(dev, &chip_node->func_list, node) {
			if (test_bit(HINIC_FUNC_IN_REMOVE, &dev->flag))
				continue;

			if (__is_pcidev_match_dev_name(ifname, dev, type)) {
				lld_dev_put();
				return dev;
			}
		}
	}
	lld_dev_put();

	return NULL;
}

static struct hinic_pcidev *hinic_get_pcidev_by_ifname(char *ifname)
{
	struct hinic_pcidev *dev;

	/* support search hwdev by chip name, net device name,
	 * or fc device name
	 */
	/* Find pcidev by chip_name first */
	dev = hinic_get_pcidev_by_chip_name(ifname);
	if (dev)
		return dev;

	/* If ifname not a chip name,
	 * find pcidev by FC name or netdevice name
	 */
	return hinic_get_pcidev_by_dev_name(ifname, SERVICE_T_MAX);
}

int hinic_get_chip_name_by_hwdev(void *hwdev, char *ifname)
{
	struct card_node *chip_node;
	struct hinic_pcidev *dev;

	if (!hwdev || !ifname)
		return -EINVAL;

	lld_dev_hold();
	list_for_each_entry(chip_node, &g_hinic_chip_list, node) {
		list_for_each_entry(dev, &chip_node->func_list, node) {
			if (test_bit(HINIC_FUNC_IN_REMOVE, &dev->flag))
				continue;

			if (dev->hwdev == hwdev) {
				strncpy(ifname, chip_node->chip_name,
					IFNAMSIZ - 1);
				ifname[IFNAMSIZ - 1] = 0;
				lld_dev_put();
				return 0;
			}
		}
	}
	lld_dev_put();

	return -ENXIO;
}
EXPORT_SYMBOL(hinic_get_chip_name_by_hwdev);

static struct card_node *hinic_get_chip_node_by_hwdev(const void *hwdev)
{
	struct card_node *chip_node = NULL;
	struct card_node *node_tmp = NULL;
	struct hinic_pcidev *dev;

	if (!hwdev)
		return NULL;

	lld_dev_hold();
	list_for_each_entry(node_tmp, &g_hinic_chip_list, node) {
		if (!chip_node) {
			list_for_each_entry(dev, &node_tmp->func_list, node) {
				if (test_bit(HINIC_FUNC_IN_REMOVE, &dev->flag))
					continue;

				if (dev->hwdev == hwdev) {
					chip_node = node_tmp;
					break;
				}
			}
		}
	}

	lld_dev_put();

	return chip_node;
}

int hinic_get_pf_uld_array(struct pci_dev *pdev, u32 *dev_cnt, void *array[])
{
	struct hinic_pcidev *dev = pci_get_drvdata(pdev);
	struct card_node *chip_node;
	u32 cnt;

	if (!dev || !hinic_support_nic(dev->hwdev, NULL))
		return -EINVAL;

	lld_dev_hold();

	cnt = 0;
	chip_node = dev->chip_node;
	list_for_each_entry(dev, &chip_node->func_list, node) {
		if (test_bit(HINIC_FUNC_IN_REMOVE, &dev->flag))
			continue;

		if (dev->init_state < HINIC_INIT_STATE_NIC_INITED)
			continue;

		if (HINIC_FUNC_IS_VF(dev->hwdev))
			continue;

		if (!hinic_support_nic(dev->hwdev, NULL))
			continue;

		array[cnt] = dev->uld_dev[SERVICE_T_NIC];
		cnt++;
	}
	lld_dev_put();

	*dev_cnt = cnt;

	return 0;
}

int hinic_get_chip_cos_up_map(struct pci_dev *pdev, bool *is_setted, u8 *cos_up)
{
	struct hinic_pcidev *dev = pci_get_drvdata(pdev);
	struct card_node *chip_node;

	if (!dev)
		return -EINVAL;

	chip_node = dev->chip_node;
	*is_setted = chip_node->cos_up_setted;
	if (chip_node->cos_up_setted)
		memcpy(cos_up, chip_node->cos_up, sizeof(chip_node->cos_up));

	return 0;
}

int hinic_set_chip_cos_up_map(struct pci_dev *pdev, u8 *cos_up)
{
	struct hinic_pcidev *dev = pci_get_drvdata(pdev);
	struct card_node *chip_node;

	if (!dev)
		return -EINVAL;

	chip_node = dev->chip_node;
	chip_node->cos_up_setted = true;
	memcpy(chip_node->cos_up, cos_up, sizeof(chip_node->cos_up));

	return 0;
}

void *hinic_get_hwdev_by_ifname(char *ifname)
{
	struct hinic_pcidev *dev;

	dev = hinic_get_pcidev_by_ifname(ifname);
	if (dev)
		return dev->hwdev;

	return NULL;
}

void *hinic_get_uld_dev_by_ifname(char *ifname, enum hinic_service_type type)
{
	struct hinic_pcidev *dev;

	if (type >= SERVICE_T_MAX) {
		pr_err("Service type :%d is error\n", type);
		return NULL;
	}

	dev = hinic_get_pcidev_by_dev_name(ifname, type);
	if (dev)
		return dev->uld_dev[type];

	return NULL;
}

void *hinic_get_uld_by_chip_name(char *ifname, enum hinic_service_type type)
{
	struct hinic_pcidev *dev;

	/* support search hwdev by chip name, net device name,
	 * or fc device name, Find pcidev by chip_name first
	 */
	dev = hinic_get_pcidev_by_chip_name(ifname);
	if (dev)
		return dev->uld_dev[type];

	return NULL;
}

/* NOTICE: nictool can't use this function, because this function can't keep
 * tool context mutual exclusive with remove context
 */
void *hinic_get_ppf_uld_by_pdev(struct pci_dev *pdev,
				enum hinic_service_type type)
{
	struct hinic_pcidev *pci_adapter;
	struct card_node *chip_node;
	struct hinic_pcidev *dev;

	if (!pdev)
		return NULL;

	pci_adapter = pci_get_drvdata(pdev);
	if (!pci_adapter)
		return NULL;

	chip_node = pci_adapter->chip_node;
	lld_dev_hold();
	list_for_each_entry(dev, &chip_node->func_list, node) {
		/* can't test HINIC_FUNC_IN_REMOVE bit in dev->flag, because
		 * TOE will call this function when detach toe driver
		 */

		if (hinic_func_type(dev->hwdev) == TYPE_PPF) {
			lld_dev_put();
			return dev->uld_dev[type];
		}
	}
	lld_dev_put();

	return NULL;
}
EXPORT_SYMBOL(hinic_get_ppf_uld_by_pdev);

void *hinic_get_ppf_hwdev_by_pdev(struct pci_dev *pdev)
{
	struct hinic_pcidev *pci_adapter;
	struct card_node *chip_node;
	struct hinic_pcidev *dev;

	if (!pdev)
		return NULL;

	pci_adapter = pci_get_drvdata(pdev);
	if (!pci_adapter)
		return NULL;

	chip_node = pci_adapter->chip_node;
	lld_dev_hold();
	list_for_each_entry(dev, &chip_node->func_list, node) {
		if (dev->hwdev && hinic_func_type(dev->hwdev) == TYPE_PPF) {
			lld_dev_put();
			return dev->hwdev;
		}
	}
	lld_dev_put();

	return NULL;
}

void hinic_get_all_chip_id(void *id_info)
{
	struct nic_card_id *card_id = (struct nic_card_id *)id_info;
	struct card_node *chip_node;
	int i = 0;
	int id, err;

	lld_dev_hold();
	list_for_each_entry(chip_node, &g_hinic_chip_list, node) {
		err = sscanf(chip_node->chip_name, HINIC_CHIP_NAME "%d", &id);
		if (err <= 0)
			pr_err("Failed to get hinic id\n");

		card_id->id[i] = id;
		i++;
	}
	lld_dev_put();
	card_id->num = i;
}

static bool __is_func_valid(struct hinic_pcidev *dev)
{
	if (test_bit(HINIC_FUNC_IN_REMOVE, &dev->flag))
		return false;

	if (dev->init_state < HINIC_INIT_STATE_HWDEV_INITED)
		return false;

	if (HINIC_FUNC_IS_VF(dev->hwdev))
		return false;

	return true;
}

bool hinic_is_valid_bar_addr(u64 offset)
{
	struct card_node *chip_node = NULL;
	struct hinic_pcidev *dev;

	lld_dev_hold();
	list_for_each_entry(chip_node, &g_hinic_chip_list, node) {
		list_for_each_entry(dev, &chip_node->func_list, node) {
			if (hinic_func_type(dev->hwdev) == TYPE_VF)
				continue;

			if (test_bit(HINIC_FUNC_IN_REMOVE, &dev->flag))
				continue;

			if (offset == pci_resource_start(dev->pcidev, 0)) {
				lld_dev_put();
				return true;
			}
		}
	}
	lld_dev_put();

	return false;
}

void hinic_get_card_info(void *hwdev, void *bufin)
{
	struct card_node *chip_node = NULL;
	struct card_info *info = (struct card_info *)bufin;
	struct hinic_nic_dev *nic_dev;
	struct hinic_pcidev *dev;
	void *fun_hwdev;
	u32 i = 0;

	info->pf_num = 0;

	chip_node = hinic_get_chip_node_by_hwdev(hwdev);
	if (!chip_node)
		return;

	lld_dev_hold();
	list_for_each_entry(dev, &chip_node->func_list, node) {
		if (!__is_func_valid(dev))
			continue;

		fun_hwdev = dev->hwdev;

		if (((hinic_support_fc(fun_hwdev, NULL)) ||
		     (hinic_support_fcoe(fun_hwdev, NULL))) &&
		     dev->uld_dev[SERVICE_T_FC]) {
			info->pf[i].pf_type |= (u32)BIT(SERVICE_T_FC);
			strlcpy(info->pf[i].name,
				dev->uld_dev_name[SERVICE_T_FC], IFNAMSIZ);
		}

		if (hinic_support_nic(fun_hwdev, NULL)) {
			nic_dev = dev->uld_dev[SERVICE_T_NIC];
			if (nic_dev) {
				info->pf[i].pf_type |= (u32)BIT(SERVICE_T_NIC);
				strlcpy(info->pf[i].name,
					nic_dev->netdev->name, IFNAMSIZ);
			}
		}

		if ((hinic_support_ovs(fun_hwdev, NULL)) &&
		    dev->uld_dev[SERVICE_T_OVS])
			info->pf[i].pf_type |= (u32)BIT(SERVICE_T_OVS);

		if ((hinic_support_roce(fun_hwdev, NULL)) &&
		    dev->uld_dev[SERVICE_T_ROCE])
			info->pf[i].pf_type |= (u32)BIT(SERVICE_T_ROCE);

		if ((hinic_support_toe(fun_hwdev, NULL)) &&
		    dev->uld_dev[SERVICE_T_TOE])
			info->pf[i].pf_type |= (u32)BIT(SERVICE_T_TOE);

		if (hinic_func_for_mgmt(fun_hwdev))
			strlcpy(info->pf[i].name, "FOR_MGMT", IFNAMSIZ);

		if (hinic_func_for_pt(fun_hwdev))
			info->pf[i].pf_type |= (u32)BIT(SERVICE_T_PT);

		if (hinic_func_for_hwpt(fun_hwdev))
			info->pf[i].pf_type |= (u32)BIT(SERVICE_T_HWPT);

		strlcpy(info->pf[i].bus_info, pci_name(dev->pcidev),
			sizeof(info->pf[i].bus_info));
		info->pf_num++;
		i = info->pf_num;
	}
	lld_dev_put();
}

void hinic_get_card_func_info_by_card_name(
	const char *chip_name, struct hinic_card_func_info *card_func)
{
	struct card_node *chip_node = NULL;
	struct hinic_pcidev *dev;
	struct func_pdev_info *pdev_info;

	card_func->num_pf = 0;

	lld_dev_hold();
	list_for_each_entry(chip_node, &g_hinic_chip_list, node) {
		if (strncmp(chip_node->chip_name, chip_name, IFNAMSIZ))
			continue;

		list_for_each_entry(dev, &chip_node->func_list, node) {
			if (hinic_func_type(dev->hwdev) == TYPE_VF)
				continue;

			if (test_bit(HINIC_FUNC_IN_REMOVE, &dev->flag))
				continue;

			pdev_info = &card_func->pdev_info[card_func->num_pf];
			pdev_info->bar0_size = pci_resource_len(dev->pcidev, 0);
			pdev_info->bar0_phy_addr =
					pci_resource_start(dev->pcidev, 0);

			card_func->num_pf++;
			if (card_func->num_pf >= MAX_SIZE)
				break;
		}
	}

	lld_dev_put();
}

int hinic_get_device_id(void *hwdev, u16 *dev_id)
{
	struct card_node *chip_node = NULL;
	struct hinic_pcidev *dev;
	u16 vendor_id = 0;
	u16 device_id = 0;

	chip_node = hinic_get_chip_node_by_hwdev(hwdev);
	if (!chip_node)
		return -ENODEV;

	lld_dev_hold();
	list_for_each_entry(dev, &chip_node->func_list, node) {
		if (test_bit(HINIC_FUNC_IN_REMOVE, &dev->flag))
			continue;

		pci_read_config_word(dev->pcidev, 0, &vendor_id);
		if (vendor_id == HINIC_PCI_VENDOR_ID) {
			pci_read_config_word(dev->pcidev, 2, &device_id);
			break;
		}
	}
	lld_dev_put();
	*dev_id = device_id;

	return 0;
}

int hinic_get_pf_id(void *hwdev, u32 port_id, u32 *pf_id, u32 *isvalid)
{
	struct card_node *chip_node = NULL;
	struct hinic_pcidev *dev;

	chip_node = hinic_get_chip_node_by_hwdev(hwdev);
	if (!chip_node)
		return -ENODEV;

	lld_dev_hold();
	list_for_each_entry(dev, &chip_node->func_list, node) {
		if (hinic_physical_port_id(dev->hwdev) == port_id) {
			*pf_id = hinic_global_func_id(dev->hwdev);
			*isvalid = 1;
			break;
		}
	}
	lld_dev_put();

	return 0;
}

void get_fc_devname(char *devname)
{
	struct card_node *chip_node;
	struct hinic_pcidev *dev;

	lld_dev_hold();
	list_for_each_entry(chip_node, &g_hinic_chip_list, node) {
		list_for_each_entry(dev, &chip_node->func_list, node) {
			if (test_bit(HINIC_FUNC_IN_REMOVE, &dev->flag))
				continue;

			if (dev->init_state < HINIC_INIT_STATE_NIC_INITED)
				continue;

			if (HINIC_FUNC_IS_VF(dev->hwdev))
				continue;

			if (dev->uld_dev[SERVICE_T_FC]) {
				strlcpy(devname,
					dev->uld_dev_name[SERVICE_T_FC],
					IFNAMSIZ);
				lld_dev_put();
				return;
			}
		}
	}
	lld_dev_put();
}

enum hinic_init_state hinic_get_init_state(struct pci_dev *pdev)
{
	struct hinic_pcidev *dev = pci_get_drvdata(pdev);

	if (dev)
		return dev->init_state;

	return HINIC_INIT_STATE_NONE;
}

enum hinic_init_state hinic_get_init_state_by_ifname(char *ifname)
{
	struct hinic_pcidev *dev;

	dev = hinic_get_pcidev_by_ifname(ifname);
	if (dev)
		return dev->init_state;

	pr_err("Can not get device %s\n", ifname);

	return HINIC_INIT_STATE_NONE;
}

int hinic_get_self_test_result(char *ifname, u32 *result)
{
	struct hinic_pcidev *dev = NULL;

	dev = hinic_get_pcidev_by_ifname(ifname);
	if (!dev) {
		pr_err("Get pcidev failed by ifname: %s\n", ifname);
		return -EFAULT;
	}

	*result = be32_to_cpu(readl((u8 __iomem *)(dev->cfg_reg_base) +
				SELF_TEST_BAR_ADDR_OFFSET));
	return 0;
}

struct net_device *hinic_get_netdev_by_lld(struct hinic_lld_dev *lld_dev)
{
	struct hinic_pcidev *pci_adapter;
	struct hinic_nic_dev *nic_dev;

	if (!lld_dev || !hinic_support_nic(lld_dev->hwdev, NULL))
		return NULL;

	pci_adapter = pci_get_drvdata(lld_dev->pdev);
	nic_dev = pci_adapter->uld_dev[SERVICE_T_NIC];
	if (!nic_dev) {
		sdk_err(&pci_adapter->pcidev->dev,
			"There's no net device attached on the pci device");
		return NULL;
	}

	return nic_dev->netdev;
}
EXPORT_SYMBOL(hinic_get_netdev_by_lld);

void *hinic_get_hwdev_by_netdev(struct net_device *netdev)
{
	struct hinic_nic_dev *nic_dev = netdev_priv(netdev);

	if (!nic_dev || !netdev)
		return NULL;

	return nic_dev->hwdev;
}
EXPORT_SYMBOL(hinic_get_hwdev_by_netdev);

struct net_device *hinic_get_netdev_by_pcidev(struct pci_dev *pdev)
{
	struct hinic_pcidev *pci_adapter;
	struct hinic_nic_dev *nic_dev;

	if (!pdev)
		return NULL;

	pci_adapter = pci_get_drvdata(pdev);
	if (!pci_adapter || !hinic_support_nic(pci_adapter->hwdev, NULL))
		return NULL;

	nic_dev = pci_adapter->uld_dev[SERVICE_T_NIC];
	if (!nic_dev) {
		sdk_err(&pci_adapter->pcidev->dev,
			"There`s no net device attached on the pci device");
		return NULL;
	}

	return nic_dev->netdev;
}
EXPORT_SYMBOL(hinic_get_netdev_by_pcidev);

struct hinic_sriov_info *hinic_get_sriov_info_by_pcidev(struct pci_dev *pdev)
{
	struct hinic_pcidev *pci_adapter = pci_get_drvdata(pdev);

	return &pci_adapter->sriov_info;
}

bool hinic_is_in_host(void)
{
	struct card_node *chip_node;
	struct hinic_pcidev *dev;

	lld_dev_hold();
	list_for_each_entry(chip_node, &g_hinic_chip_list, node) {
		list_for_each_entry(dev, &chip_node->func_list, node) {
			if (test_bit(HINIC_FUNC_IN_REMOVE, &dev->flag))
				continue;

			if (dev->init_state > HINIC_INIT_STATE_PCI_INITED &&
			    hinic_func_type(dev->hwdev) != TYPE_VF) {
				lld_dev_put();
				return true;
			}
		}
	}
	lld_dev_put();

	return false;
}

int hinic_attach_nic(struct hinic_lld_dev *lld_dev)
{
	struct hinic_pcidev *dev;

	if (!lld_dev)
		return -EINVAL;

	dev = container_of(lld_dev, struct hinic_pcidev, lld_dev);
	return attach_uld(dev, SERVICE_T_NIC, &g_uld_info[SERVICE_T_NIC]);
}
EXPORT_SYMBOL(hinic_attach_nic);

void hinic_detach_nic(struct hinic_lld_dev *lld_dev)
{
	struct hinic_pcidev *dev;

	if (!lld_dev)
		return;

	dev = container_of(lld_dev, struct hinic_pcidev, lld_dev);
	detach_uld(dev, SERVICE_T_NIC);
}
EXPORT_SYMBOL(hinic_detach_nic);

int hinic_attach_roce(struct hinic_lld_dev *lld_dev)
{
	struct hinic_pcidev *dev;

	if (!lld_dev)
		return -EINVAL;

	dev = container_of(lld_dev, struct hinic_pcidev, lld_dev);
	return attach_uld(dev, SERVICE_T_ROCE, &g_uld_info[SERVICE_T_ROCE]);
}
EXPORT_SYMBOL(hinic_attach_roce);

void hinic_detach_roce(struct hinic_lld_dev *lld_dev)
{
	struct hinic_pcidev *dev;

	if (!lld_dev)
		return;

	dev = container_of(lld_dev, struct hinic_pcidev, lld_dev);
	detach_uld(dev, SERVICE_T_ROCE);
}
EXPORT_SYMBOL(hinic_detach_roce);

static int __set_nic_rss_state(struct hinic_pcidev *dev, bool enable)
{
	void *nic_uld;
	int err = 0;

	if (test_bit(HINIC_FUNC_IN_REMOVE, &dev->flag))
		return 0;

	nic_uld = dev->uld_dev[SERVICE_T_NIC];
	if (!hinic_support_nic(dev->hwdev, NULL) || !nic_uld)
		return 0;

	if (hinic_func_type(dev->hwdev) == TYPE_VF)
		return 0;

	if (enable)
		err = hinic_enable_func_rss(nic_uld);
	else
		err = hinic_disable_func_rss(nic_uld);
	if (err) {
		sdk_err(&dev->pcidev->dev, "Failed to %s rss\n",
			enable ? "enable" : "disable");
	}

	return err;
}

int hinic_disable_nic_rss(struct hinic_lld_dev *lld_dev)
{
	struct hinic_pcidev *adapter;

	if (!lld_dev)
		return -EINVAL;

	adapter = container_of(lld_dev, struct hinic_pcidev, lld_dev);

	return __set_nic_rss_state(adapter, false);
}
EXPORT_SYMBOL(hinic_disable_nic_rss);

int hinic_enable_nic_rss(struct hinic_lld_dev *lld_dev)
{
	struct hinic_pcidev *adapter;

	if (!lld_dev)
		return -EINVAL;

	adapter = container_of(lld_dev, struct hinic_pcidev, lld_dev);

	return __set_nic_rss_state(adapter, true);
}
EXPORT_SYMBOL(hinic_enable_nic_rss);

struct pci_device_id *hinic_get_pci_device_id(struct pci_dev *pdev)
{
	struct hinic_pcidev *adapter;

	if (!pdev)
		return NULL;

	adapter = pci_get_drvdata(pdev);

	return &adapter->id;
}
EXPORT_SYMBOL(hinic_get_pci_device_id);

static int __set_nic_func_state(struct hinic_pcidev *pci_adapter)
{
	struct pci_dev *pdev = pci_adapter->pcidev;
	u16 func_id;
	int err;
	bool enable_nic;

	err = hinic_global_func_id_get(pci_adapter->hwdev, &func_id);
	if (err)
		return err;

	err = hinic_get_func_nic_enable(pci_adapter->hwdev, func_id,
					&enable_nic);
	if (err) {
		sdk_err(&pdev->dev, "Failed to get nic state\n");
		return err;
	}

	if (enable_nic) {
		if (is_multi_bm_slave(pci_adapter->hwdev))
			hinic_set_vf_dev_cap(pci_adapter->hwdev);

		err = attach_uld(pci_adapter, SERVICE_T_NIC,
				 &g_uld_info[SERVICE_T_NIC]);
		if (err) {
			sdk_err(&pdev->dev, "Failed to initialize NIC\n");
			return err;
		}

		if (pci_adapter->init_state < HINIC_INIT_STATE_NIC_INITED)
			pci_adapter->init_state = HINIC_INIT_STATE_NIC_INITED;
	} else {
		detach_uld(pci_adapter, SERVICE_T_NIC);
	}

	return 0;
}

int hinic_ovs_set_vf_nic_state(struct hinic_lld_dev *lld_dev, u16 vf_func_id,
			       bool en)
{
	struct hinic_pcidev *dev, *des_dev;
	struct hinic_nic_dev *uld_dev;
	int err = -EFAULT;

	if (!lld_dev)
		return -EINVAL;

	dev = pci_get_drvdata(lld_dev->pdev);

	if (!dev)
		return -EFAULT;
	/* find func_idx pci_adapter and disable or enable nic */
	lld_dev_hold();
	list_for_each_entry(des_dev, &dev->chip_node->func_list, node) {
		if (test_bit(HINIC_FUNC_IN_REMOVE, &dev->flag))
			continue;

		if (des_dev->init_state <
			HINIC_INIT_STATE_DBGTOOL_INITED &&
			!test_bit(HINIC_FUNC_PRB_ERR,
			      &des_dev->flag))
			continue;

		if (hinic_global_func_id(des_dev->hwdev) != vf_func_id)
			continue;

		if (des_dev->init_state <
		    HINIC_INIT_STATE_DBGTOOL_INITED) {
			break;
		}

		sdk_info(&dev->pcidev->dev, "Receive event: %s vf%d nic\n",
			 en ? "enable" : "disable", vf_func_id);

		err = 0;
		if (en) {
			if (des_dev->uld_dev[SERVICE_T_NIC]) {
				sdk_err(&des_dev->pcidev->dev,
					"%s driver has attached to pcie device, cannot set VF max_queue_num\n",
					s_uld_name[SERVICE_T_NIC]);
			} else {
				err = hinic_set_vf_dev_cap(des_dev->hwdev);

				if (err) {
					sdk_err(&des_dev->pcidev->dev,
						"%s driver Set VF max_queue_num failed, err=%d.\n",
						s_uld_name[SERVICE_T_NIC], err);

					break;
				}
			}

			err = attach_uld(des_dev, SERVICE_T_NIC,
					 &g_uld_info[SERVICE_T_NIC]);
			if (err) {
				sdk_err(&des_dev->pcidev->dev, "Failed to initialize NIC\n");
				break;
			}

			uld_dev = (struct hinic_nic_dev *)
			    (des_dev->uld_dev[SERVICE_T_NIC]);
			uld_dev->in_vm = true;
			uld_dev->is_vm_slave =
				is_multi_vm_slave(uld_dev->hwdev);
			uld_dev->is_bm_slave =
				is_multi_bm_slave(uld_dev->hwdev);
			if (des_dev->init_state < HINIC_INIT_STATE_NIC_INITED)
				des_dev->init_state =
					HINIC_INIT_STATE_NIC_INITED;
		} else {
			detach_uld(des_dev, SERVICE_T_NIC);
		}

		break;
	}
	lld_dev_put();

	return err;
}
EXPORT_SYMBOL(hinic_ovs_set_vf_nic_state);

static void slave_host_mgmt_work(struct work_struct *work)
{
	struct hinic_pcidev *pci_adapter =
			container_of(work, struct hinic_pcidev, slave_nic_work);

	__set_nic_func_state(pci_adapter);
}

static void __multi_host_mgmt(struct hinic_pcidev *dev,
			      struct hinic_multi_host_mgmt_event *mhost_mgmt)
{
	struct hinic_pcidev *des_dev;
	struct hinic_mhost_nic_func_state *nic_state = {0};

	switch (mhost_mgmt->sub_cmd) {
	case HINIC_MHOST_NIC_STATE_CHANGE:
		nic_state = mhost_mgmt->data;

		nic_state->status = 0;

		/* find func_idx pci_adapter and disable or enable nic */
		lld_dev_hold();
		list_for_each_entry(des_dev, &dev->chip_node->func_list, node) {
			if (test_bit(HINIC_FUNC_IN_REMOVE, &des_dev->flag))
				continue;

			if (des_dev->init_state <
			    HINIC_INIT_STATE_DBGTOOL_INITED &&
			    !test_bit(HINIC_FUNC_PRB_ERR,
				      &des_dev->flag))
				continue;

			if (hinic_global_func_id_hw(des_dev->hwdev) !=
			    nic_state->func_idx)
				continue;

			if (des_dev->init_state <
			    HINIC_INIT_STATE_DBGTOOL_INITED) {
				nic_state->status =
					test_bit(HINIC_FUNC_PRB_ERR,
						 &des_dev->flag) ? 1 : 0;
				break;
			}

			sdk_info(&dev->pcidev->dev, "Receive nic state changed event, state: %d\n",
				 nic_state->enable);

			/* schedule_work */
			schedule_work(&des_dev->slave_nic_work);

			break;
		}
		lld_dev_put();

		break;

	default:
		sdk_warn(&dev->pcidev->dev, "Received unknown multi-host mgmt event %d\n",
			 mhost_mgmt->sub_cmd);
		break;
	}
}

void hinic_event_process(void *adapter, struct hinic_event_info *event)
{
	struct hinic_pcidev *dev = adapter;
	enum hinic_service_type type;

	if (event->type == HINIC_EVENT_FMW_ACT_NTC)
		return hinic_sync_time_to_fmw(dev);
	else if (event->type == HINIC_EVENT_MCTP_GET_HOST_INFO)
		return __mctp_get_host_info(dev, &event->mctp_info);
	else if (event->type == HINIC_EVENT_MULTI_HOST_MGMT)
		return __multi_host_mgmt(dev, &event->mhost_mgmt);

	for (type = SERVICE_T_NIC; type < SERVICE_T_MAX; type++) {
		if (test_and_set_bit(type, &dev->state)) {
			sdk_warn(&dev->pcidev->dev, "Event: 0x%x can't handler, %s is in detach\n",
				 event->type, s_uld_name[type]);
			continue;
		}

		if (g_uld_info[type].event)
			g_uld_info[type].event(&dev->lld_dev,
					       dev->uld_dev[type], event);
		clear_bit(type, &dev->state);
	}
}

static int mapping_bar(struct pci_dev *pdev, struct hinic_pcidev *pci_adapter)
{
	u32 db_dwqe_size;
	u64 dwqe_addr;

	pci_adapter->cfg_reg_base =
		pci_ioremap_bar(pdev, HINIC_PCI_CFG_REG_BAR);
	if (!pci_adapter->cfg_reg_base) {
		sdk_err(&pci_adapter->pcidev->dev,
			"Failed to map configuration regs\n");
		return -ENOMEM;
	}

	pci_adapter->intr_reg_base = pci_ioremap_bar(pdev,
						     HINIC_PCI_INTR_REG_BAR);
	if (!pci_adapter->intr_reg_base) {
		sdk_err(&pci_adapter->pcidev->dev,
			"Failed to map interrupt regs\n");
		goto map_intr_bar_err;
	}

	db_dwqe_size = hinic_get_db_size(pci_adapter->cfg_reg_base,
					 &pci_adapter->chip_mode);

	pci_adapter->db_base_phy = pci_resource_start(pdev, HINIC_PCI_DB_BAR);
	pci_adapter->db_base = ioremap(pci_adapter->db_base_phy,
				       db_dwqe_size);
	if (!pci_adapter->db_base) {
		sdk_err(&pci_adapter->pcidev->dev,
			"Failed to map doorbell regs\n");
		goto map_db_err;
	}

	if (pci_adapter->chip_mode != CHIP_MODE_NORMAL)
		return 0;

	dwqe_addr = pci_adapter->db_base_phy + db_dwqe_size;

#if defined(__aarch64__)
	/* arm do not support call ioremap_wc() */
	pci_adapter->dwqe_mapping = __ioremap(dwqe_addr, db_dwqe_size,
					      __pgprot(PROT_DEVICE_nGnRnE));
#else
	pci_adapter->dwqe_mapping = io_mapping_create_wc(dwqe_addr,
							 db_dwqe_size);

#endif  /* end of "defined(__aarch64__)" */
	if (!pci_adapter->dwqe_mapping) {
		sdk_err(&pci_adapter->pcidev->dev, "Failed to io_mapping_create_wc\n");
		goto mapping_dwqe_err;
	}

	return 0;

mapping_dwqe_err:
	iounmap(pci_adapter->db_base);

map_db_err:
	iounmap(pci_adapter->intr_reg_base);

map_intr_bar_err:
	iounmap(pci_adapter->cfg_reg_base);

	return -ENOMEM;
}

static void unmapping_bar(struct hinic_pcidev *pci_adapter)
{
	if (pci_adapter->chip_mode == CHIP_MODE_NORMAL) {
#if defined(__aarch64__)
		iounmap(pci_adapter->dwqe_mapping);
#else
		io_mapping_free(pci_adapter->dwqe_mapping);
#endif /* end of "defined(__aarch64__)" */
	}

	iounmap(pci_adapter->db_base);
	iounmap(pci_adapter->intr_reg_base);
	iounmap(pci_adapter->cfg_reg_base);
}


static int alloc_chip_node(struct hinic_pcidev *pci_adapter)
{
	struct card_node *chip_node;
	unsigned char i;
	unsigned char parent_bus_number = 0;
	int err;

	if  (!pci_is_root_bus(pci_adapter->pcidev->bus))
		parent_bus_number = pci_adapter->pcidev->bus->parent->number;

	if (parent_bus_number != 0) {
		list_for_each_entry(chip_node, &g_hinic_chip_list, node) {
			if (chip_node->dp_bus_num == parent_bus_number) {
				pci_adapter->chip_node = chip_node;
				return 0;
			}
		}
	} else if (pci_adapter->pcidev->device == HINIC_DEV_ID_1822_VF ||
		   pci_adapter->pcidev->device == HINIC_DEV_ID_1822_VF_HV) {
		list_for_each_entry(chip_node, &g_hinic_chip_list, node) {
			if (chip_node) {
				pci_adapter->chip_node = chip_node;
				return 0;
			}
		}
	}

	for (i = 0; i < MAX_CARD_ID; i++) {
		if (!FIND_BIT(card_bit_map, i)) {
			card_bit_map = (u64)SET_BIT(card_bit_map, i);
			break;
		}
	}

	if (i == MAX_CARD_ID) {
		sdk_err(&pci_adapter->pcidev->dev,
			"Failed to alloc card id\n");
		return -EFAULT;
	}

	chip_node = kzalloc(sizeof(*chip_node), GFP_KERNEL);
	if (!chip_node) {
		sdk_err(&pci_adapter->pcidev->dev,
			"Failed to alloc chip node\n");
		goto alloc_chip_err;
	}

	chip_node->dbgtool_attr_file.name = kzalloc(IFNAMSIZ, GFP_KERNEL);
	if (!(chip_node->dbgtool_attr_file.name)) {
		sdk_err(&pci_adapter->pcidev->dev,
			"Failed to alloc dbgtool attr file name\n");
		goto alloc_dbgtool_attr_file_err;
	}

	/* parent bus number */
	chip_node->dp_bus_num = parent_bus_number;

	err = snprintf(chip_node->chip_name, IFNAMSIZ, "%s%d",
		       HINIC_CHIP_NAME, i);
	if (err <= 0 || err >= IFNAMSIZ) {
		sdk_err(&pci_adapter->pcidev->dev,
			"Failed snprintf chip_name, function return(%d) and dest_len(%d)\n",
			err, IFNAMSIZ);
		goto alloc_dbgtool_attr_file_err;
	}

	err = snprintf((char *)chip_node->dbgtool_attr_file.name,
		       IFNAMSIZ, "%s%d", HINIC_CHIP_NAME, i);
	if (err <= 0 || err >= IFNAMSIZ) {
		sdk_err(&pci_adapter->pcidev->dev,
			"Failed snprintf dbgtool_attr_file_name, function return(%d) and dest_len(%d)\n",
			err, IFNAMSIZ);
		goto alloc_dbgtool_attr_file_err;
	}

	sdk_info(&pci_adapter->pcidev->dev,
		 "Add new chip %s to global list succeed\n",
		 chip_node->chip_name);

	list_add_tail(&chip_node->node, &g_hinic_chip_list);

	INIT_LIST_HEAD(&chip_node->func_list);
	pci_adapter->chip_node = chip_node;

	mutex_init(&chip_node->sfp_mutex);

	return 0;

alloc_dbgtool_attr_file_err:
	kfree(chip_node);

alloc_chip_err:
	card_bit_map = CLEAR_BIT(card_bit_map, i);
	return -ENOMEM;
}

static void free_chip_node(struct hinic_pcidev *pci_adapter)
{
	struct card_node *chip_node = pci_adapter->chip_node;
	u32 id;
	int err;

	if (list_empty(&chip_node->func_list)) {
		list_del(&chip_node->node);
		sdk_info(&pci_adapter->pcidev->dev,
			 "Delete chip %s from global list succeed\n",
			 chip_node->chip_name);
		err = sscanf(chip_node->chip_name, HINIC_CHIP_NAME "%u", &id);
		if (err <= 0)
			sdk_err(&pci_adapter->pcidev->dev, "Failed to get hinic id\n");

		card_bit_map = CLEAR_BIT(card_bit_map, id);

		kfree(chip_node->dbgtool_attr_file.name);
		kfree(chip_node);
	}
}

static bool hinic_get_vf_load_state(struct pci_dev *pdev)
{
	unsigned char parent_bus_number;
	struct card_node *chip_node;
	u8 id;

	if (!pdev->is_virtfn)
		return false;

	/* vf used in vm */
	if (pci_is_root_bus(pdev->bus))
		return disable_vf_load;

	parent_bus_number = pdev->bus->parent->number;

	lld_dev_hold();
	list_for_each_entry(chip_node, &g_hinic_chip_list, node) {
		if (chip_node->dp_bus_num == parent_bus_number) {
			for (id = 0; id < HINIC_MAX_PF_NUM; id++) {
				if (chip_node->pf_bus_num[id] ==
				    pdev->bus->number) {
					lld_dev_put();
					return chip_node->disable_vf_load[id];
				}
			}
		}
	}
	lld_dev_put();

	return disable_vf_load;
}

static void hinic_set_vf_load_state(struct hinic_pcidev *pci_adapter,
				    bool vf_load_state)
{
	struct card_node *chip_node;
	u16 func_id;

	if (hinic_func_type(pci_adapter->hwdev) == TYPE_VF)
		return;

	/* The VF on the BM slave side must be probed */
	if (is_multi_bm_slave(pci_adapter->hwdev))
		vf_load_state = false;

	func_id = hinic_global_func_id_hw(pci_adapter->hwdev);

	chip_node = pci_adapter->chip_node;
	chip_node->disable_vf_load[func_id] = vf_load_state;
	chip_node->pf_bus_num[func_id] = pci_adapter->pcidev->bus->number;

	sdk_info(&pci_adapter->pcidev->dev, "Current function support %s, %s vf load in host\n",
		 (hinic_support_ovs(pci_adapter->hwdev, NULL) ? "ovs" : "nic"),
		 (vf_load_state ? "disable" : "enable"));
}

int hinic_ovs_set_vf_load_state(struct pci_dev *pdev)
{
	struct hinic_pcidev *pci_adapter;

	if (!pdev) {
		pr_err("pdev is null.\n");
		return -EINVAL;
	}

	pci_adapter = pci_get_drvdata(pdev);
	if (!pci_adapter) {
		pr_err("pci_adapter is null.\n");
		return -EFAULT;
	}

	hinic_set_vf_load_state(pci_adapter, disable_vf_load);

	return 0;
}
EXPORT_SYMBOL(hinic_ovs_set_vf_load_state);

static int hinic_config_deft_mrss(struct pci_dev *pdev)
{
	return 0;
}

static int hinic_config_pci_cto(struct pci_dev *pdev)
{
	return 0;
}

static int hinic_pci_init(struct pci_dev *pdev)
{
	struct hinic_pcidev *pci_adapter = NULL;
	int err;

	err = hinic_config_deft_mrss(pdev);
	if (err) {
		sdk_err(&pdev->dev, "Failed to configure Max Read Request Size\n");
		return err;
	}

	err = hinic_config_pci_cto(pdev);
	if (err) {
		sdk_err(&pdev->dev, "Failed to configure Completion timeout\n");
		return err;
	}

	pci_adapter = kzalloc(sizeof(*pci_adapter), GFP_KERNEL);
	if (!pci_adapter) {
		sdk_err(&pdev->dev,
			"Failed to alloc pci device adapter\n");
		return -ENOMEM;
	}
	pci_adapter->pcidev = pdev;
	mutex_init(&pci_adapter->pdev_mutex);

	pci_set_drvdata(pdev, pci_adapter);

#ifdef CONFIG_PCI_IOV
	if (pdev->is_virtfn && hinic_get_vf_load_state(pdev)) {
		sdk_info(&pdev->dev, "VFs are not binded to hinic\n");
		return 0;
	}
#endif

	err = pci_enable_device(pdev);
	if (err) {
		sdk_err(&pdev->dev, "Failed to enable PCI device\n");
		goto pci_enable_err;
	}

	err = pci_request_regions(pdev, HINIC_DRV_NAME);
	if (err) {
		sdk_err(&pdev->dev, "Failed to request regions\n");
		goto pci_regions_err;
	}

	pci_enable_pcie_error_reporting(pdev);

	pci_set_master(pdev);

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		sdk_warn(&pdev->dev, "Couldn't set 64-bit DMA mask\n");
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			sdk_err(&pdev->dev, "Failed to set DMA mask\n");
			goto dma_mask_err;
		}
	}

	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		sdk_warn(&pdev->dev,
			 "Couldn't set 64-bit coherent DMA mask\n");
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			sdk_err(&pdev->dev,
				"Failed to set coherent DMA mask\n");
			goto dma_consistnet_mask_err;
		}
	}

	return 0;

dma_consistnet_mask_err:
dma_mask_err:
	pci_clear_master(pdev);
	pci_disable_pcie_error_reporting(pdev);
	pci_release_regions(pdev);

pci_regions_err:
	pci_disable_device(pdev);

pci_enable_err:
	pci_set_drvdata(pdev, NULL);
	kfree(pci_adapter);

	return err;
}

static void hinic_pci_deinit(struct pci_dev *pdev)
{
	struct hinic_pcidev *pci_adapter = pci_get_drvdata(pdev);

	pci_clear_master(pdev);
	pci_release_regions(pdev);
	pci_disable_pcie_error_reporting(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	kfree(pci_adapter);
}

static void hinic_notify_ppf_unreg(struct hinic_pcidev *pci_adapter)
{
	struct card_node *chip_node = pci_adapter->chip_node;
	struct hinic_pcidev *dev;

	if (hinic_func_type(pci_adapter->hwdev) != TYPE_PPF)
		return;

	lld_lock_chip_node();
	list_for_each_entry(dev, &chip_node->func_list, node) {
		hinic_ppf_hwdev_unreg(dev->hwdev);
	}
	lld_unlock_chip_node();
}

static void hinic_notify_ppf_reg(struct hinic_pcidev *pci_adapter)
{
	struct card_node *chip_node = pci_adapter->chip_node;
	struct hinic_pcidev *dev;

	if (hinic_func_type(pci_adapter->hwdev) != TYPE_PPF)
		return;

	lld_lock_chip_node();
	list_for_each_entry(dev, &chip_node->func_list, node) {
		hinic_ppf_hwdev_reg(dev->hwdev, pci_adapter->hwdev);
	}
	lld_unlock_chip_node();
}

#ifdef CONFIG_X86
/**
 * cfg_order_reg - when cpu model is haswell or broadwell, should configure dma
 * order register to zero
 * @pci_adapter: pci adapter
 */
void cfg_order_reg(struct hinic_pcidev *pci_adapter)
{
	u8 cpu_model[] = {0x3c, 0x3f, 0x45, 0x46, 0x3d, 0x47, 0x4f, 0x56};
	struct cpuinfo_x86 *cpuinfo;
	u32 i;

	if (HINIC_FUNC_IS_VF(pci_adapter->hwdev))
		return;

	cpuinfo = &cpu_data(0);
	for (i = 0; i < sizeof(cpu_model); i++) {
		if (cpu_model[i] == cpuinfo->x86_model)
			hinic_set_pcie_order_cfg(pci_adapter->hwdev);
	}
}
#endif

static int hinic_func_init(struct pci_dev *pdev,
			   struct hinic_pcidev *pci_adapter)
{
	struct hinic_init_para init_para;
	bool vf_load_state;
	int err;

	init_para.adapter_hdl = pci_adapter;
	init_para.pcidev_hdl = pdev;
	init_para.dev_hdl = &pdev->dev;
	init_para.cfg_reg_base = pci_adapter->cfg_reg_base;
	init_para.intr_reg_base = pci_adapter->intr_reg_base;
	init_para.db_base = pci_adapter->db_base;
	init_para.db_base_phy = pci_adapter->db_base_phy;
	init_para.dwqe_mapping = pci_adapter->dwqe_mapping;
	init_para.hwdev = &pci_adapter->hwdev;
	init_para.chip_node = pci_adapter->chip_node;
	init_para.ppf_hwdev = hinic_get_ppf_hwdev_by_pdev(pdev);
	err = hinic_init_hwdev(&init_para);
	if (err < 0) {
		pci_adapter->hwdev = NULL;
		sdk_err(&pdev->dev, "Failed to initialize hardware device\n");
		return -EFAULT;
	} else if (err > 0) {
		if (err == (1 << HINIC_HWDEV_ALL_INITED) &&
		    pci_adapter->init_state < HINIC_INIT_STATE_HW_IF_INITED) {
			pci_adapter->init_state = HINIC_INIT_STATE_HW_IF_INITED;
			sdk_info(&pdev->dev,
				 "Initialize hardware device later\n");
			queue_delayed_work(pci_adapter->slave_nic_init_workq,
					   &pci_adapter->slave_nic_init_dwork,
					   HINIC_SLAVE_NIC_DELAY_TIME);
			set_bit(HINIC_FUNC_PRB_DELAY, &pci_adapter->flag);
		} else if (err != (1 << HINIC_HWDEV_ALL_INITED)) {
			sdk_err(&pdev->dev,
				"Initialize hardware device partitial failed\n");
			hinic_detect_version_compatible(pci_adapter);
			hinic_notify_ppf_reg(pci_adapter);
			pci_adapter->init_state =
				HINIC_INIT_STATE_HW_PART_INITED;
		}
		return -EFAULT;
	}

	hinic_notify_ppf_reg(pci_adapter);
	pci_adapter->init_state = HINIC_INIT_STATE_HWDEV_INITED;

	vf_load_state = hinic_support_ovs(pci_adapter->hwdev, NULL) ?
			true : disable_vf_load;

	hinic_set_vf_load_state(pci_adapter, vf_load_state);
	hinic_qps_num_set(pci_adapter->hwdev, 0);

	pci_adapter->lld_dev.pdev = pdev;
	pci_adapter->lld_dev.hwdev = pci_adapter->hwdev;
	pci_adapter->sriov_info.pdev = pdev;
	pci_adapter->sriov_info.hwdev = pci_adapter->hwdev;

	hinic_event_register(pci_adapter->hwdev, pci_adapter,
			     hinic_event_process);

	if (!HINIC_FUNC_IS_VF(pci_adapter->hwdev))
		hinic_sync_time_to_fmw(pci_adapter);

	/* dbgtool init */
	lld_lock_chip_node();
	err = dbgtool_knl_init(pci_adapter->hwdev, pci_adapter->chip_node);
	if (err) {
		lld_unlock_chip_node();
		sdk_err(&pdev->dev, "Failed to initialize dbgtool\n");
		hinic_event_unregister(pci_adapter->hwdev);
		return err;
	}
	lld_unlock_chip_node();

	pci_adapter->init_state = HINIC_INIT_STATE_DBGTOOL_INITED;

	err = hinic_detect_version_compatible(pci_adapter);
	if (err)
		return err;

	if (!HINIC_FUNC_IS_VF(pci_adapter->hwdev) &&
	    FUNC_ENABLE_SRIOV_IN_DEFAULT(pci_adapter->hwdev)) {
		hinic_pci_sriov_enable(pdev,
				       hinic_func_max_vf(pci_adapter->hwdev));
	}

	/* NIC is base driver, probe firstly */
	err = __set_nic_func_state(pci_adapter);
	if (err)
		return err;

	attach_ulds(pci_adapter);
	if (!HINIC_FUNC_IS_VF(pci_adapter->hwdev)) {
		err = sysfs_create_group(&pdev->dev.kobj, &hinic_attr_group);
		if (err) {
			sdk_err(&pdev->dev, "Failed to create sysfs group\n");
			return -EFAULT;
		}
	}

#ifdef CONFIG_X86
	cfg_order_reg(pci_adapter);
#endif

	sdk_info(&pdev->dev, "Pcie device probed\n");
	pci_adapter->init_state = HINIC_INIT_STATE_ALL_INITED;

	return 0;
}

static void hinic_func_deinit(struct pci_dev *pdev)
{
	struct hinic_pcidev *pci_adapter = pci_get_drvdata(pdev);

	/* When function deinit, disable mgmt initiative report events firstly,
	 * then flush mgmt work-queue.
	 */
	hinic_disable_mgmt_msg_report(pci_adapter->hwdev);
	if (pci_adapter->init_state >= HINIC_INIT_STATE_HW_PART_INITED)
		hinic_flush_mgmt_workq(pci_adapter->hwdev);

	hinic_set_func_deinit_flag(pci_adapter->hwdev);

	if (pci_adapter->init_state >= HINIC_INIT_STATE_NIC_INITED) {
		detach_ulds(pci_adapter);
		detach_uld(pci_adapter, SERVICE_T_NIC);
	}

	if (pci_adapter->init_state >= HINIC_INIT_STATE_DBGTOOL_INITED) {
		lld_lock_chip_node();
		dbgtool_knl_deinit(pci_adapter->hwdev, pci_adapter->chip_node);
		lld_unlock_chip_node();
		hinic_event_unregister(pci_adapter->hwdev);
	}

	hinic_notify_ppf_unreg(pci_adapter);
	if (pci_adapter->init_state >= HINIC_INIT_STATE_HW_IF_INITED) {
		/* Remove the current node from  node-list first,
		 * then it's safe to free hwdev
		 */
		lld_lock_chip_node();
		list_del(&pci_adapter->node);
		lld_unlock_chip_node();

		hinic_free_hwdev(pci_adapter->hwdev);
	}
}

static void wait_tool_unused(void)
{
	u32 loop_cnt = 0;

	while (loop_cnt < HINIC_WAIT_TOOL_CNT_TIMEOUT) {
		if (!atomic_read(&tool_used_cnt))
			return;

		usleep_range(9900, 10000);
		loop_cnt++;
	}
}

static inline void wait_sriov_cfg_complete(struct hinic_pcidev *pci_adapter)
{
	struct hinic_sriov_info *sriov_info;
	u32 loop_cnt = 0;

	sriov_info = &pci_adapter->sriov_info;

	set_bit(HINIC_FUNC_REMOVE, &sriov_info->state);
	usleep_range(9900, 10000);

	while (loop_cnt < HINIC_WAIT_SRIOV_CFG_TIMEOUT) {
		if (!test_bit(HINIC_SRIOV_ENABLE, &sriov_info->state) &&
		    !test_bit(HINIC_SRIOV_DISABLE, &sriov_info->state))
			return;

		usleep_range(9900, 10000);
		loop_cnt++;
	}
}

static void hinic_remove(struct pci_dev *pdev)
{
	struct hinic_pcidev *pci_adapter = pci_get_drvdata(pdev);

	if (!pci_adapter)
		return;

	sdk_info(&pdev->dev, "Pcie device remove begin\n");
#ifdef CONFIG_PCI_IOV
	if (pdev->is_virtfn && hinic_get_vf_load_state(pdev)) {
		pci_set_drvdata(pdev, NULL);
		kfree(pci_adapter);
		return;
	}
#endif
	cancel_delayed_work_sync(&pci_adapter->slave_nic_init_dwork);
	flush_workqueue(pci_adapter->slave_nic_init_workq);
	destroy_workqueue(pci_adapter->slave_nic_init_workq);

	if (pci_adapter->init_state >= HINIC_INIT_STATE_HW_IF_INITED)
		hinic_detect_hw_present(pci_adapter->hwdev);

	switch (pci_adapter->init_state) {
	case HINIC_INIT_STATE_ALL_INITED:
		if (!HINIC_FUNC_IS_VF(pci_adapter->hwdev))
			sysfs_remove_group(&pdev->dev.kobj, &hinic_attr_group);
	case HINIC_INIT_STATE_NIC_INITED:
		/* Don't support hotplug when SR-IOV is enabled now.
		 * So disable SR-IOV capability as normal.
		 */
		if (!HINIC_FUNC_IS_VF(pci_adapter->hwdev)) {
			wait_sriov_cfg_complete(pci_adapter);
			hinic_pci_sriov_disable(pdev);
		}
	case HINIC_INIT_STATE_DBGTOOL_INITED:
	case HINIC_INIT_STATE_HWDEV_INITED:
	case HINIC_INIT_STATE_HW_PART_INITED:
	case HINIC_INIT_STATE_HW_IF_INITED:
	case HINIC_INIT_STATE_PCI_INITED:
		set_bit(HINIC_FUNC_IN_REMOVE, &pci_adapter->flag);
		lld_lock_chip_node();
		cancel_work_sync(&pci_adapter->slave_nic_work);
		lld_unlock_chip_node();

		wait_tool_unused();

		if (pci_adapter->init_state >= HINIC_INIT_STATE_HW_IF_INITED)
			hinic_func_deinit(pdev);

		lld_lock_chip_node();
		if (pci_adapter->init_state < HINIC_INIT_STATE_HW_IF_INITED)
			list_del(&pci_adapter->node);
		nictool_k_uninit();
		free_chip_node(pci_adapter);
		lld_unlock_chip_node();
		unmapping_bar(pci_adapter);
		hinic_pci_deinit(pdev);

		break;

	default:
		break;
	}

	sdk_info(&pdev->dev, "Pcie device removed\n");
}

static void slave_host_init_delay_work(struct work_struct *work)
{
	struct delayed_work *delay = to_delayed_work(work);
	struct hinic_pcidev *pci_adapter = container_of(delay,
		struct hinic_pcidev, slave_nic_init_dwork);
	struct pci_dev *pdev = pci_adapter->pcidev;
	struct card_node *chip_node = pci_adapter->chip_node;
	int found = 0;
	struct hinic_pcidev *ppf_pcidev = NULL;
	int err;

	if (!hinic_get_master_host_mbox_enable(pci_adapter->hwdev)) {
		queue_delayed_work(pci_adapter->slave_nic_init_workq,
				   &pci_adapter->slave_nic_init_dwork,
				   HINIC_SLAVE_NIC_DELAY_TIME);
		return;
	}
	if (hinic_func_type(pci_adapter->hwdev) == TYPE_PPF) {
		err = hinic_func_init(pdev, pci_adapter);
		clear_bit(HINIC_FUNC_PRB_DELAY, &pci_adapter->flag);
		if (err)
			set_bit(HINIC_FUNC_PRB_ERR, &pci_adapter->flag);
		return;
	}

	/* Make sure the PPF must be the first one */
	lld_dev_hold();
	list_for_each_entry(ppf_pcidev, &chip_node->func_list, node) {
		if (ppf_pcidev &&
		    hinic_func_type(ppf_pcidev->hwdev) == TYPE_PPF) {
			found = 1;
			break;
		}
	}
	lld_dev_put();
	if (found && ppf_pcidev->init_state == HINIC_INIT_STATE_ALL_INITED) {
		err = hinic_func_init(pdev, pci_adapter);
		clear_bit(HINIC_FUNC_PRB_DELAY, &pci_adapter->flag);
		if (err)
			set_bit(HINIC_FUNC_PRB_ERR, &pci_adapter->flag);
		return;
	}
	queue_delayed_work(pci_adapter->slave_nic_init_workq,
			   &pci_adapter->slave_nic_init_dwork,
			   HINIC_SLAVE_NIC_DELAY_TIME);
}

static int hinic_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct hinic_pcidev *pci_adapter;
	int err;

	sdk_info(&pdev->dev, "Pcie device probe begin\n");

	err = hinic_pci_init(pdev);
	if (err)
		return err;

#ifdef CONFIG_PCI_IOV
	if (pdev->is_virtfn && hinic_get_vf_load_state(pdev))
		return 0;
#endif

	pci_adapter = pci_get_drvdata(pdev);
	clear_bit(HINIC_FUNC_PRB_ERR, &pci_adapter->flag);
	clear_bit(HINIC_FUNC_PRB_DELAY, &pci_adapter->flag);
	err = mapping_bar(pdev, pci_adapter);
	if (err) {
		sdk_err(&pdev->dev, "Failed to map bar\n");
		goto map_bar_failed;
	}

	pci_adapter->id = *id;
	INIT_WORK(&pci_adapter->slave_nic_work, slave_host_mgmt_work);
	pci_adapter->slave_nic_init_workq =
		create_singlethread_workqueue(HINIC_SLAVE_NIC_DELAY);
	if (!pci_adapter->slave_nic_init_workq) {
		sdk_err(&pdev->dev,
			"Failed to create work queue:%s\n",
			HINIC_SLAVE_NIC_DELAY);
		goto ceate_nic_delay_work_fail;
	}
	INIT_DELAYED_WORK(&pci_adapter->slave_nic_init_dwork,
			  slave_host_init_delay_work);

	/* if chip information of pcie function exist,
	 * add the function into chip
	 */
	lld_lock_chip_node();
	err = alloc_chip_node(pci_adapter);
	if (err) {
		sdk_err(&pdev->dev,
			"Failed to add new chip node to global list\n");
		goto alloc_chip_node_fail;
	}

	err = nictool_k_init();
	if (err) {
		sdk_warn(&pdev->dev, "Failed to init nictool");
		goto init_nictool_err;
	}

	list_add_tail(&pci_adapter->node, &pci_adapter->chip_node->func_list);

	lld_unlock_chip_node();

	pci_adapter->init_state = HINIC_INIT_STATE_PCI_INITED;

	err = hinic_func_init(pdev, pci_adapter);
	if (err)
		goto func_init_err;

	return 0;

func_init_err:
	if (!test_bit(HINIC_FUNC_PRB_DELAY, &pci_adapter->flag))
		set_bit(HINIC_FUNC_PRB_ERR, &pci_adapter->flag);
	return 0;

init_nictool_err:
	free_chip_node(pci_adapter);

alloc_chip_node_fail:
	lld_unlock_chip_node();
ceate_nic_delay_work_fail:
	unmapping_bar(pci_adapter);
map_bar_failed:
	hinic_pci_deinit(pdev);


	sdk_err(&pdev->dev, "Pcie device probe failed\n");
	return err;
}

static const struct pci_device_id hinic_pci_table[] = {
	{PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_1822_PF), HINIC_BOARD_25GE},
	{PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_1822_VF), 0},
	{PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_1822_VF_HV), 0},
	{PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_1822_SMTIO), HINIC_BOARD_PG_SM_25GE},
	{PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_1822_PANGEA_100GE),
	 HINIC_BOARD_PG_100GE},
	{PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_1822_PANGEA_TP_10GE),
	 HINIC_BOARD_PG_TP_10GE},
	{PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_1822_KR_40GE), HINIC_BOARD_40GE},
	{PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_1822_KR_100GE), HINIC_BOARD_100GE},
	{PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_1822_KR_25GE), HINIC_BOARD_25GE},
	{PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_1822_MULTI_HOST), HINIC_BOARD_25GE},
	{PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_1822_100GE), HINIC_BOARD_100GE},
	{PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_1822_DUAL_25GE), HINIC_BOARD_25GE},
	{0, 0}
};

MODULE_DEVICE_TABLE(pci, hinic_pci_table);

/**
 * hinic_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 *
 * Since we only need error detecting not error handling, so we
 * always return PCI_ERS_RESULT_CAN_RECOVER to tell the AER
 * driver that we don't need reset(error handling).
 */
static pci_ers_result_t hinic_io_error_detected(struct pci_dev *pdev,
						pci_channel_state_t state)
{
	struct hinic_pcidev *pci_adapter;

	sdk_err(&pdev->dev,
		"Uncorrectable error detected, log and cleanup error status: 0x%08x\n",
		state);

	pci_cleanup_aer_uncorrect_error_status(pdev);
	pci_adapter = pci_get_drvdata(pdev);

	if (pci_adapter)
		hinic_record_pcie_error(pci_adapter->hwdev);

	return PCI_ERS_RESULT_CAN_RECOVER;
}

static void hinic_shutdown(struct pci_dev *pdev)
{
	struct hinic_pcidev *pci_adapter = pci_get_drvdata(pdev);

	sdk_info(&pdev->dev, "Shutdown device\n");

	if (pci_adapter)
		hinic_shutdown_hwdev(pci_adapter->hwdev);

	pci_disable_device(pdev);

	if (pci_adapter)
		hinic_set_api_stop(pci_adapter->hwdev);
}

#ifdef HAVE_RHEL6_SRIOV_CONFIGURE
static struct pci_driver_rh hinic_driver_rh = {
	.sriov_configure = hinic_pci_sriov_configure,
};
#endif

/* Cause we only need error detecting not error handling, so only error_detected
 * callback is enough.
 */
static struct pci_error_handlers hinic_err_handler = {
	.error_detected = hinic_io_error_detected,
};

static struct pci_driver hinic_driver = {
	.name		 = HINIC_DRV_NAME,
	.id_table	 = hinic_pci_table,
	.probe		 = hinic_probe,
	.remove		 = hinic_remove,
	.shutdown	 = hinic_shutdown,

#if defined(HAVE_SRIOV_CONFIGURE)
	.sriov_configure = hinic_pci_sriov_configure,
#elif defined(HAVE_RHEL6_SRIOV_CONFIGURE)
	.rh_reserved = &hinic_driver_rh,
#endif

	.err_handler	 = &hinic_err_handler
};

static int __init hinic_lld_init(void)
{
	pr_info("%s - version %s\n", HINIC_DRV_DESC, HINIC_DRV_VERSION);
	memset(g_uld_info, 0, sizeof(g_uld_info));
	atomic_set(&tool_used_cnt, 0);

	hinic_lld_lock_init();


	/* register nic driver information first, and add net device in
	 * nic_probe called by hinic_probe.
	 */
	hinic_register_uld(SERVICE_T_NIC, &nic_uld_info);

	return pci_register_driver(&hinic_driver);
}

static void __exit hinic_lld_exit(void)
{

	pci_unregister_driver(&hinic_driver);

	hinic_unregister_uld(SERVICE_T_NIC);

}

module_init(hinic_lld_init);
module_exit(hinic_lld_exit);
int hinic_register_micro_log(struct hinic_micro_log_info *micro_log_info)
{
	struct card_node *chip_node;
	struct hinic_pcidev *dev;

	if (!micro_log_info || !micro_log_info->init ||
	    !micro_log_info->deinit) {
		pr_err("Invalid information of micro log info to register\n");
		return -EINVAL;
	}

	lld_dev_hold();
	list_for_each_entry(chip_node, &g_hinic_chip_list, node) {
		list_for_each_entry(dev, &chip_node->func_list, node) {
			if (test_bit(HINIC_FUNC_IN_REMOVE, &dev->flag))
				continue;

			if (hinic_func_type(dev->hwdev) == TYPE_PPF) {
				if (micro_log_info->init(dev->hwdev)) {
					sdk_err(&dev->pcidev->dev,
						"micro log init failed\n");
					continue;
				}
			}
		}
	}
	lld_dev_put();
	pr_info("Register micro log succeed\n");

	return 0;
}
EXPORT_SYMBOL(hinic_register_micro_log);

void hinic_unregister_micro_log(struct hinic_micro_log_info *micro_log_info)
{
	struct card_node *chip_node;
	struct hinic_pcidev *dev;

	if (!micro_log_info)
		return;

	lld_dev_hold();
	list_for_each_entry(chip_node, &g_hinic_chip_list, node) {
		list_for_each_entry(dev, &chip_node->func_list, node) {
			if (test_bit(HINIC_FUNC_IN_REMOVE, &dev->flag))
				continue;

			if (hinic_func_type(dev->hwdev) == TYPE_PPF)
				micro_log_info->deinit(dev->hwdev);
		}
	}
	lld_dev_put();
	pr_info("Unregister micro log succeed\n");
}
EXPORT_SYMBOL(hinic_unregister_micro_log);
