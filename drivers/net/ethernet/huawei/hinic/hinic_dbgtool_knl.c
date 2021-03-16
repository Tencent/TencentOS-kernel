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

#include <linux/time.h>
#include <linux/timex.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/if.h>
#include <linux/ioctl.h>
#include <linux/pci.h>
#include <linux/fs.h>

#include "ossl_knl.h"
#include "hinic_hw.h"
#include "hinic_hwdev.h"
#include "hinic_hw_mgmt.h"
#include "hinic_nic_dev.h"
#include "hinic_lld.h"
#include "hinic_dbgtool_knl.h"

struct ffm_intr_info {
	u8 node_id;
	/* error level of the interrupt source */
	u8 err_level;
	/* Classification by interrupt source properties */
	u16 err_type;
	u32 err_csr_addr;
	u32 err_csr_value;
};

#define DBGTOOL_MSG_MAX_SIZE	2048ULL
#define HINIC_SELF_CMD_UP2PF_FFM		0x26

void *g_card_node_array[MAX_CARD_NUM] = {0};
void *g_card_vir_addr[MAX_CARD_NUM] = {0};
u64 g_card_phy_addr[MAX_CARD_NUM] = {0};
/* lock for g_card_vir_addr */
struct mutex	g_addr_lock;
int card_id;

/* dbgtool character device name, class name, dev path */
#define CHR_DEV_DBGTOOL "dbgtool_chr_dev"
#define CLASS_DBGTOOL "dbgtool_class"
#define DBGTOOL_DEV_PATH "/dev/dbgtool_chr_dev"

struct dbgtool_k_glb_info {
	struct semaphore dbgtool_sem;
	struct ffm_record_info *ffm;
};

dev_t dbgtool_dev_id;			/* device id */
struct cdev dbgtool_chr_dev;		/* struct of char device */

struct class *dbgtool_d_class;		/* struct of char class */

int g_dbgtool_init_flag;
int g_dbgtool_ref_cnt;

static int dbgtool_knl_open(struct inode *pnode,
			    struct file *pfile)
{
	return 0;
}

static int dbgtool_knl_release(struct inode *pnode,
			       struct file *pfile)
{
	return 0;
}

static ssize_t dbgtool_knl_read(struct file *pfile,
				char __user *ubuf,
				size_t size,
				loff_t *ppos)
{
	return 0;
}

static ssize_t dbgtool_knl_write(struct file *pfile,
				 const char __user *ubuf,
				 size_t size,
				 loff_t *ppos)
{
	return 0;
}

static bool is_valid_phy_addr(u64 offset)
{
	int i;

	for (i = 0; i < MAX_CARD_NUM; i++) {
		if (offset == g_card_phy_addr[i])
			return true;
	}

	return false;
}

int hinic_mem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long vmsize = vma->vm_end - vma->vm_start;
	phys_addr_t offset = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;
	phys_addr_t phy_addr;

	if (vmsize > (PAGE_SIZE * (1 << DBGTOOL_PAGE_ORDER))) {
		pr_err("Map size = %lu is bigger than alloc\n", vmsize);
		return -EAGAIN;
	}

	if (offset && !is_valid_phy_addr((u64)offset) &&
	    !hinic_is_valid_bar_addr((u64)offset)) {
		pr_err("offset is invalid");
		return -EAGAIN;
	}

	/* old version of tool set vma->vm_pgoff to 0 */
	phy_addr = offset ? offset : g_card_phy_addr[card_id];
	if (!phy_addr) {
		pr_err("Card_id = %d physical address is 0\n", card_id);
		return -EAGAIN;
	}

	if (remap_pfn_range(vma, vma->vm_start,
			    (phy_addr >> PAGE_SHIFT),
			    vmsize, vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

/**
 * dbgtool_knl_api_cmd_read - used for read operations
 * @para: the dbgtool parameter
 * @g_func_handle_array: global function handle
 * Return: 0 - success, negative - failure
 */
long dbgtool_knl_api_cmd_read(struct dbgtool_param *para,
			      void **g_func_handle_array)
{
	long ret = 0;
	u8 *cmd;
	u16 size;
	void *ack;
	u16 ack_size;
	u32 pf_id;
	void *hwdev;

	pf_id = para->param.api_rd.pf_id;
	if (pf_id >= 16) {
		pr_err("PF id(0x%x) too big\n", pf_id);
		return -EFAULT;
	}

	/* obtaining pf_id chipif pointer */
	hwdev = g_func_handle_array[pf_id];
	if (!hwdev) {
		pr_err("PF id(0x%x) handle null in api cmd read\n", pf_id);
		return -EFAULT;
	}

	/* alloc cmd and ack memory */
	size = para->param.api_rd.size;
	if (para->param.api_rd.size == 0) {
		pr_err("Read cmd size invalid\n");
		return -EINVAL;
	}
	cmd = kzalloc((unsigned long long)size, GFP_KERNEL);
	if (!cmd) {
		pr_err("Alloc read cmd mem fail\n");
		return -ENOMEM;
	}

	ack_size = para->param.api_rd.ack_size;
	if (para->param.api_rd.ack_size == 0) {
		pr_err("Read cmd ack size is 0\n");
		ret = -ENOMEM;
		goto alloc_ack_mem_fail;
	}

	ack = kzalloc((unsigned long long)ack_size, GFP_KERNEL);
	if (!ack) {
		pr_err("Alloc read ack mem fail\n");
		ret = -ENOMEM;
		goto alloc_ack_mem_fail;
	}

	/* cmd content copied from user-mode */
	if (copy_from_user(cmd, para->param.api_rd.cmd, (unsigned long)size)) {
		pr_err("Copy cmd from user fail\n");
		ret = -EFAULT;
		goto copy_user_cmd_fail;
	}
	/* Invoke the api cmd interface read content*/
	ret = hinic_api_cmd_read_ack(hwdev, para->param.api_rd.dest,
				     cmd, size, ack, ack_size);
	if (ret) {
		pr_err("Api send single cmd ack fail!\n");
		goto api_rd_fail;
	}

	/* Copy the contents of the ack to the user state */
	if (copy_to_user(para->param.api_rd.ack, ack, ack_size)) {
		pr_err("Copy ack to user fail\n");
		ret = -EFAULT;
	}
api_rd_fail:
copy_user_cmd_fail:
	kfree(ack);
alloc_ack_mem_fail:
	kfree(cmd);
	return ret;
}

/**
 * dbgtool_knl_api_cmd_write - used for write operations
 * @para: the dbgtool parameter
 * @g_func_handle_array: global function handle
 * Return: 0 - success, negative - failure
 */
long dbgtool_knl_api_cmd_write(struct dbgtool_param *para,
			       void **g_func_handle_array)
{
	long ret = 0;
	u8 *cmd;
	u16 size;
	u32 pf_id;
	void *hwdev;

	pf_id = para->param.api_wr.pf_id;
	if (pf_id >= 16) {
		pr_err("PF id(0x%x) too big\n", pf_id);
		return -EFAULT;
	}

	/* obtaining chipif pointer according to pf_id */
	hwdev = g_func_handle_array[pf_id];
	if (!hwdev) {
		pr_err("PF id(0x%x) handle null\n", pf_id);
		return -EFAULT;
	}

	/* alloc cmd memory */
	size = para->param.api_wr.size;
	if (para->param.api_wr.size == 0) {
		pr_err("Write cmd size invalid\n");
		return -EINVAL;
	}
	cmd = kzalloc((unsigned long long)size, GFP_KERNEL);
	if (!cmd) {
		pr_err("Alloc write cmd mem fail\n");
		return -ENOMEM;
	}

	/* cmd content copied from user-mode */
	if (copy_from_user(cmd, para->param.api_wr.cmd, (unsigned long)size)) {
		pr_err("Copy cmd from user fail\n");
		ret = -EFAULT;
		goto copy_user_cmd_fail;
	}

	/* api cmd interface is invoked to write the content */
	ret = hinic_api_cmd_write_nack(hwdev, para->param.api_wr.dest,
				       cmd, size);
	if (ret)
		pr_err("Api send single cmd nack fail\n");

copy_user_cmd_fail:
	kfree(cmd);
	return ret;
}

void chipif_get_all_pf_dev_info(struct pf_dev_info *dev_info, int card_idx,
				void **g_func_handle_array)
{
	u32 func_idx;
	struct hinic_hwdev *hwdev;

	if (!dev_info) {
		pr_err("Params error!\n");
		return;
	}

	/* pf at most 16 */
	for (func_idx = 0; func_idx < 16; func_idx++) {
		hwdev = (struct hinic_hwdev *)g_func_handle_array[func_idx];

		dev_info[func_idx].phy_addr = g_card_phy_addr[card_idx];

		if (!hwdev) {
			dev_info[func_idx].bar0_size = 0;
			dev_info[func_idx].bus = 0;
			dev_info[func_idx].slot = 0;
			dev_info[func_idx].func = 0;
		} else {
			dev_info[func_idx].bar0_size =
				pci_resource_len
				(((struct pci_dev *)hwdev->pcidev_hdl), 0);
			dev_info[func_idx].bus =
				((struct pci_dev *)
				hwdev->pcidev_hdl)->bus->number;
			dev_info[func_idx].slot =
				PCI_SLOT(((struct pci_dev *)hwdev->pcidev_hdl)
						->devfn);
			dev_info[func_idx].func =
				PCI_FUNC(((struct pci_dev *)hwdev->pcidev_hdl)
						->devfn);
		}
	}
}

/**
 * dbgtool_knl_pf_dev_info_get - Obtain the pf sdk_info
 * @para: the dbgtool parameter
 * @g_func_handle_array: global function handle
 * Return: 0 - success, negative - failure
 */
long dbgtool_knl_pf_dev_info_get(struct dbgtool_param *para,
				 void **g_func_handle_array)
{
	struct pf_dev_info dev_info[16] = { {0} };
	unsigned char *tmp;
	int i;

	mutex_lock(&g_addr_lock);
	if (!g_card_vir_addr[card_id]) {
		g_card_vir_addr[card_id] =
			(void *)__get_free_pages(GFP_KERNEL,
						 DBGTOOL_PAGE_ORDER);
		if (!g_card_vir_addr[card_id]) {
			pr_err("Alloc dbgtool api chain fail!\n");
			mutex_unlock(&g_addr_lock);
			return -EFAULT;
		}

		memset(g_card_vir_addr[card_id], 0,
		       PAGE_SIZE * (1 << DBGTOOL_PAGE_ORDER));

		g_card_phy_addr[card_id] =
			virt_to_phys(g_card_vir_addr[card_id]);
		if (!g_card_phy_addr[card_id]) {
			pr_err("phy addr for card %d is 0\n", card_id);
			free_pages((unsigned long)g_card_vir_addr[card_id],
				   DBGTOOL_PAGE_ORDER);
			g_card_vir_addr[card_id] = NULL;
			mutex_unlock(&g_addr_lock);
			return -EFAULT;
		}

		tmp = g_card_vir_addr[card_id];
		for (i = 0; i < (1 << DBGTOOL_PAGE_ORDER); i++) {
			SetPageReserved(virt_to_page(tmp));
			tmp += PAGE_SIZE;
		}
	}
	mutex_unlock(&g_addr_lock);

	chipif_get_all_pf_dev_info(dev_info, card_id, g_func_handle_array);

	/* Copy the dev_info to user mode */
	if (copy_to_user(para->param.dev_info, dev_info,
			 (unsigned int)sizeof(dev_info))) {
		pr_err("Copy dev_info to user fail\n");
		return -EFAULT;
	}

	return 0;
}

/**
 * dbgtool_knl_ffm_info_rd - Read ffm information
 * @para: the dbgtool parameter
 * @dbgtool_info: the dbgtool info
 * Return: 0 - success, negative - failure
 */
long dbgtool_knl_ffm_info_rd(struct dbgtool_param *para,
			     struct dbgtool_k_glb_info *dbgtool_info)
{
	/* Copy the ffm_info to user mode */
	if (copy_to_user(para->param.ffm_rd, dbgtool_info->ffm,
			 (unsigned int)sizeof(struct ffm_record_info))) {
		pr_err("Copy ffm_info to user fail\n");
		return -EFAULT;
	}

	return 0;
}

/**
 * dbgtool_knl_ffm_info_clr - Clear FFM information
 * @para: unused
 * @dbgtool_info: the dbgtool info
 */
void dbgtool_knl_ffm_info_clr(struct dbgtool_param *para,
			      struct dbgtool_k_glb_info *dbgtool_info)
{
	dbgtool_info->ffm->ffm_num = 0;
}

/**
 * dbgtool_knl_msg_to_up - After receiving dbgtool command sends a message to uP
 * @para: the dbgtool parameter
 * @g_func_handle_array: global function handle
 * Return: 0 - success, negative - failure
 */
long dbgtool_knl_msg_to_up(struct dbgtool_param *para,
			   void **g_func_handle_array)
{
	long ret = 0;
	void *buf_in;
	void *buf_out;
	u16 out_size;
	u8 pf_id;

	if (para->param.msg2up.in_size > DBGTOOL_MSG_MAX_SIZE) {
		pr_err("User data(%d) more than 2KB\n",
		       para->param.msg2up.in_size);
		return -EFAULT;
	}

	pf_id = para->param.msg2up.pf_id;
	/* pf at most 16 */
	if (pf_id >= 16) {
		pr_err("PF id(0x%x) too big in message to mgmt\n", pf_id);
		return -EFAULT;
	}

	if (!g_func_handle_array[pf_id]) {
		pr_err("PF id(0x%x) handle null in message to mgmt\n", pf_id);
		return -EFAULT;
	}

	/* alloc buf_in and buf_out memory, apply for 2K */
	buf_in = kzalloc(DBGTOOL_MSG_MAX_SIZE, GFP_KERNEL);
	if (!buf_in) {
		pr_err("Alloc buf_in mem fail\n");
		return -ENOMEM;
	}

	buf_out = kzalloc(DBGTOOL_MSG_MAX_SIZE, 0);
	if (!buf_out) {
		pr_err("Alloc buf_out mem fail\n");
		ret = -ENOMEM;
		goto alloc_buf_out_mem_fail;
	}

	/* copy buf_in from the user state */
	if (copy_from_user(buf_in, para->param.msg2up.buf_in,
			   (unsigned long)para->param.msg2up.in_size)) {
		pr_err("Copy buf_in from user fail\n");
		ret = -EFAULT;
		goto copy_user_buf_in_fail;
	}

	out_size = DBGTOOL_MSG_MAX_SIZE;
	/* Invoke the pf2up communication interface */
	ret = hinic_msg_to_mgmt_sync(g_func_handle_array[pf_id],
				     para->param.msg2up.mod,
				     para->param.msg2up.cmd,
				     buf_in,
				     para->param.msg2up.in_size,
				     buf_out,
				     &out_size,
				     0);
	if (ret)
		goto msg_2_up_fail;

	/* Copy the out_size and buf_out content to user mode */
	if (copy_to_user(para->param.msg2up.out_size, &out_size,
			 (unsigned int)sizeof(out_size))) {
		pr_err("Copy out_size to user fail\n");
		ret = -EFAULT;
		goto copy_out_size_fail;
	}

	if (copy_to_user(para->param.msg2up.buf_out, buf_out, out_size)) {
		pr_err("Copy buf_out to user fail\n");
		ret = -EFAULT;
	}

copy_out_size_fail:
msg_2_up_fail:
copy_user_buf_in_fail:
	kfree(buf_out);
alloc_buf_out_mem_fail:
	kfree(buf_in);
	return ret;
}

long dbgtool_knl_free_mem(int id)
{
	unsigned char *tmp;
	int i;

	mutex_lock(&g_addr_lock);

	if (!g_card_vir_addr[id]) {
		mutex_unlock(&g_addr_lock);
		return 0;
	}

	tmp = g_card_vir_addr[id];
	for (i = 0; i < (1 << DBGTOOL_PAGE_ORDER); i++) {
		ClearPageReserved(virt_to_page(tmp));
		tmp += PAGE_SIZE;
	}

	free_pages((unsigned long)g_card_vir_addr[id], DBGTOOL_PAGE_ORDER);
	g_card_vir_addr[id] = NULL;
	g_card_phy_addr[id] = 0;

	mutex_unlock(&g_addr_lock);

	return 0;
}

/**
 * dbgtool_knl_unlocked_ioctl - dbgtool ioctl entry
 * @pfile: the pointer to file
 * @cmd: the command type
 */
long dbgtool_knl_unlocked_ioctl(struct file *pfile,
				unsigned int cmd,
				unsigned long arg)
{
	long ret = 0;
	unsigned int real_cmd;
	struct dbgtool_param param;
	struct dbgtool_k_glb_info *dbgtool_info;
	struct card_node *card_info = NULL;
	int i;

	(void)memset(&param, 0, sizeof(param));

	if (copy_from_user(&param, (void *)arg, sizeof(param))) {
		pr_err("Copy param from user fail\n");
		return -EFAULT;
	}

	param.chip_name[IFNAMSIZ - 1] = '\0';
	for (i = 0; i < MAX_CARD_NUM; i++) {
		card_info = (struct card_node *)g_card_node_array[i];
		if (!card_info)
			continue;
		if (!strncmp(param.chip_name, card_info->chip_name, IFNAMSIZ))
			break;
	}

	if (i == MAX_CARD_NUM || !card_info) {
		pr_err("Can't find this card %s\n", param.chip_name);
		return -EFAULT;
	}

	card_id = i;

	dbgtool_info = (struct dbgtool_k_glb_info *)card_info->dbgtool_info;

	down(&dbgtool_info->dbgtool_sem);

	real_cmd = _IOC_NR(cmd);

	switch (real_cmd) {
	case DBGTOOL_CMD_API_RD:
		ret = dbgtool_knl_api_cmd_read(&param,
					       card_info->func_handle_array);
		break;
	case DBGTOOL_CMD_API_WR:
		ret = dbgtool_knl_api_cmd_write(&param,
						card_info->func_handle_array);
		break;
	case DBGTOOL_CMD_FFM_RD:
		ret = dbgtool_knl_ffm_info_rd(&param, dbgtool_info);
		break;
	case DBGTOOL_CMD_FFM_CLR:
		dbgtool_knl_ffm_info_clr(&param, dbgtool_info);
		break;
	case DBGTOOL_CMD_PF_DEV_INFO_GET:
		ret = dbgtool_knl_pf_dev_info_get(&param,
						  card_info->func_handle_array);
		break;
	case DBGTOOL_CMD_MSG_2_UP:
		ret = dbgtool_knl_msg_to_up(&param,
					    card_info->func_handle_array);
		break;
	case DBGTOOL_CMD_FREE_MEM:
		ret = dbgtool_knl_free_mem(i);
		break;
	default:
		pr_err("Dbgtool cmd(x%x) not support now\n", real_cmd);
		ret = -EFAULT;
	}

	up(&dbgtool_info->dbgtool_sem);
	return ret;
}

/**
 * ffm_intr_msg_record - FFM interruption records sent up
 * @handle: the function handle
 * @buf_in: the pointer to input buffer
 * @buf_out: the pointer to outputput buffer
 */
void ffm_intr_msg_record(void *handle, void *buf_in, u16 in_size,
			 void *buf_out, u16 *out_size)
{
	struct dbgtool_k_glb_info *dbgtool_info;
	struct ffm_intr_info *intr;
	u32 ffm_idx;
	struct timex txc;
	struct rtc_time rctm;
	struct card_node *card_info = NULL;
	bool flag = false;
	int i, j;

	for (i = 0; i < MAX_CARD_NUM; i++) {
		card_info = (struct card_node *)g_card_node_array[i];
		if (!card_info)
			continue;

		for (j = 0; j < MAX_FUNCTION_NUM; j++) {
			if (handle == card_info->func_handle_array[j]) {
				flag = true;
				break;
			}
		}

		if (flag)
			break;
	}

	if (i == MAX_CARD_NUM || !card_info) {
		pr_err("Id(%d) cant find this card\n", i);
		return;
	}

	dbgtool_info = (struct dbgtool_k_glb_info *)card_info->dbgtool_info;
	if (!dbgtool_info) {
		pr_err("Dbgtool info is null\n");
		return;
	}

	intr = (struct ffm_intr_info *)buf_in;

	if (!dbgtool_info->ffm)
		return;

	ffm_idx = dbgtool_info->ffm->ffm_num;
	if (ffm_idx < FFM_RECORD_NUM_MAX) {
		pr_info("%s: recv intr, ffm_idx: %d\n", __func__, ffm_idx);

		dbgtool_info->ffm->ffm[ffm_idx].node_id = intr->node_id;
		dbgtool_info->ffm->ffm[ffm_idx].err_level = intr->err_level;
		dbgtool_info->ffm->ffm[ffm_idx].err_type = intr->err_type;
		dbgtool_info->ffm->ffm[ffm_idx].err_csr_addr =
						intr->err_csr_addr;
		dbgtool_info->ffm->ffm[ffm_idx].err_csr_value =
						intr->err_csr_value;

		/* Obtain the current UTC time */
		 do_gettimeofday(&txc.time);

		/* Calculate the time in date value to tm */
		 rtc_time_to_tm((unsigned long)txc.time.tv_sec +
				60 * 60 * 8, &rctm);

		/* tm_year starts from 1900; 0->1900, 1->1901, and so on */
		dbgtool_info->ffm->ffm[ffm_idx].year =
					(u16)(rctm.tm_year + 1900);
		/* tm_mon starts from 0, 0 indicates January, and so on */
		dbgtool_info->ffm->ffm[ffm_idx].mon = (u8)rctm.tm_mon + 1;
		dbgtool_info->ffm->ffm[ffm_idx].mday = (u8)rctm.tm_mday;
		dbgtool_info->ffm->ffm[ffm_idx].hour = (u8)rctm.tm_hour;
		dbgtool_info->ffm->ffm[ffm_idx].min = (u8)rctm.tm_min;
		dbgtool_info->ffm->ffm[ffm_idx].sec = (u8)rctm.tm_sec;

		dbgtool_info->ffm->ffm_num++;
	}
}

static const struct file_operations dbgtool_file_operations = {
	.owner = THIS_MODULE,
	.open = dbgtool_knl_open,
	.release = dbgtool_knl_release,
	.read = dbgtool_knl_read,
	.write = dbgtool_knl_write,
	.unlocked_ioctl = dbgtool_knl_unlocked_ioctl,
	.mmap = hinic_mem_mmap,
};

/**
 * dbgtool_knl_init - dbgtool character device init
 * @hwdev: the pointer to hardware device
 * @chip_node: the pointer to card node
 * Return: 0 - success, negative - failure
 */
int dbgtool_knl_init(void *vhwdev, void *chip_node)
{
	int ret = 0;
	int id;
	struct dbgtool_k_glb_info *dbgtool_info;
	struct device *pdevice;
	struct card_node *chip_info = (struct card_node *)chip_node;
	struct hinic_hwdev *hwdev = vhwdev;

	if (hinic_func_type(hwdev) == TYPE_VF)
		return 0;

	ret = sysfs_create_file(&((struct device *)(hwdev->dev_hdl))->kobj,
				&chip_info->dbgtool_attr_file);
	if (ret) {
		pr_err("Failed to sysfs create file\n");
		return ret;
	}

	chip_info->func_handle_array[hinic_global_func_id(hwdev)] = hwdev;

	hinic_comm_recv_mgmt_self_cmd_reg(hwdev, HINIC_SELF_CMD_UP2PF_FFM,
					  ffm_intr_msg_record);

	if (chip_info->dbgtool_info) {
		chip_info->func_num++;
		return 0;
	}

	dbgtool_info = (struct dbgtool_k_glb_info *)
			kzalloc(sizeof(struct dbgtool_k_glb_info), GFP_KERNEL);
	if (!dbgtool_info) {
		pr_err("Failed to allocate dbgtool_info\n");
		ret = -EFAULT;
		goto dbgtool_info_fail;
	}
	chip_info->dbgtool_info = dbgtool_info;

	/* FFM init */
	dbgtool_info->ffm = (struct ffm_record_info *)
				kzalloc(sizeof(struct ffm_record_info),
					GFP_KERNEL);
	if (!dbgtool_info->ffm) {
		pr_err("Failed to allocate cell contexts for a chain\n");
		ret = -EFAULT;
		goto dbgtool_info_ffm_fail;
	}

	sema_init(&dbgtool_info->dbgtool_sem, 1);

	ret = sscanf(chip_info->chip_name, HINIC_CHIP_NAME "%d", &id);
	if (ret <= 0) {
		pr_err("Failed to get hinic id\n");
		goto sscanf_chdev_fail;
	}

	g_card_node_array[id] = chip_info;
	chip_info->func_num++;

	if (g_dbgtool_init_flag) {
		g_dbgtool_ref_cnt++;
		/* already initialized */
		return 0;
	}

	/*alloc device id*/
	ret = alloc_chrdev_region(&(dbgtool_dev_id), 0, 1, CHR_DEV_DBGTOOL);
	if (ret) {
		pr_err("Alloc dbgtool chrdev region fail, ret=0x%x\n", ret);
		goto alloc_chdev_fail;
	}

	/*init device*/
	cdev_init(&(dbgtool_chr_dev), &dbgtool_file_operations);

	/*add device*/
	ret = cdev_add(&(dbgtool_chr_dev), dbgtool_dev_id, 1);
	if (ret) {
		pr_err("Add dgbtool dev fail, ret=0x%x\n", ret);
		goto cdev_add_fail;
	}

	dbgtool_d_class = class_create(THIS_MODULE, CLASS_DBGTOOL);
	if (IS_ERR(dbgtool_d_class)) {
		pr_err("Create dgbtool class fail\n");
		ret = -EFAULT;
		goto cls_create_fail;
	}

	/* Export device information to user space
	 * (/sys/class/class name/device name)
	 */
	pdevice = device_create(dbgtool_d_class, NULL,
				dbgtool_dev_id, NULL, CHR_DEV_DBGTOOL);
	if (IS_ERR(pdevice)) {
		pr_err("Create dgbtool device fail\n");
		ret = -EFAULT;
		goto dev_create_fail;
	}
	g_dbgtool_init_flag = 1;
	g_dbgtool_ref_cnt = 1;
	mutex_init(&g_addr_lock);

	return 0;

dev_create_fail:
	class_destroy(dbgtool_d_class);
cls_create_fail:
	cdev_del(&(dbgtool_chr_dev));
cdev_add_fail:
	unregister_chrdev_region(dbgtool_dev_id, 1);
alloc_chdev_fail:
	g_card_node_array[id] = NULL;
sscanf_chdev_fail:
	kfree(dbgtool_info->ffm);
dbgtool_info_ffm_fail:
	kfree(dbgtool_info);
	dbgtool_info = NULL;
	chip_info->dbgtool_info = NULL;
dbgtool_info_fail:
	hinic_comm_recv_up_self_cmd_unreg(hwdev, HINIC_SELF_CMD_UP2PF_FFM);
	chip_info->func_handle_array[hinic_global_func_id(hwdev)] = NULL;
	sysfs_remove_file(&((struct device *)(hwdev->dev_hdl))->kobj,
			  &chip_info->dbgtool_attr_file);
	return ret;
}

/**
 * dbgtool_knl_deinit - dbgtool character device deinit
 * @hwdev: the pointer to hardware device
 * @chip_node: the pointer to card node
 */
void dbgtool_knl_deinit(void *vhwdev, void *chip_node)
{
	struct dbgtool_k_glb_info *dbgtool_info;
	struct card_node *chip_info = (struct card_node *)chip_node;
	int id;
	int err;
	struct hinic_hwdev *hwdev = vhwdev;

	if (hinic_func_type(hwdev) == TYPE_VF)
		return;

	hinic_comm_recv_up_self_cmd_unreg(hwdev, HINIC_SELF_CMD_UP2PF_FFM);

	chip_info->func_handle_array[hinic_global_func_id(hwdev)] = NULL;

	sysfs_remove_file(&((struct device *)(hwdev->dev_hdl))->kobj,
			  &chip_info->dbgtool_attr_file);

	chip_info->func_num--;
	if (chip_info->func_num)
		return;

	err = sscanf(chip_info->chip_name, HINIC_CHIP_NAME "%d", &id);
	if (err <= 0)
		pr_err("Failed to get hinic id\n");

	g_card_node_array[id] = NULL;

	dbgtool_info = chip_info->dbgtool_info;
	/* FFM deinit */
	kfree(dbgtool_info->ffm);
	dbgtool_info->ffm = NULL;

	kfree(dbgtool_info);
	chip_info->dbgtool_info = NULL;

	(void)dbgtool_knl_free_mem(id);

	if (g_dbgtool_init_flag) {
		if ((--g_dbgtool_ref_cnt))
			return;
	}

	if (!dbgtool_d_class)
		return;

	device_destroy(dbgtool_d_class, dbgtool_dev_id);
	class_destroy(dbgtool_d_class);
	dbgtool_d_class = NULL;

	cdev_del(&(dbgtool_chr_dev));
	unregister_chrdev_region(dbgtool_dev_id, 1);

	g_dbgtool_init_flag = 0;
}
