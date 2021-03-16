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

#include <linux/pci_regs.h>

#include "ossl_knl_linux.h"

#define OSSL_MINUTE_BASE (60)

#if (KERNEL_VERSION(2, 6, 39) > LINUX_VERSION_CODE)
#if (!(RHEL_RELEASE_CODE && RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(6, 0)))
#ifdef HAVE_NETDEV_SELECT_QUEUE
#include <net/ip.h>
#include <linux/pkt_sched.h>
#endif /* HAVE_NETDEV_SELECT_QUEUE */
#endif /* !(RHEL_RELEASE_CODE > RHEL_RELEASE_VERSION(6,0)) */
#endif /* < 2.6.39 */
#if (KERNEL_VERSION(3, 4, 0) > LINUX_VERSION_CODE)
void _kc_skb_add_rx_frag(struct sk_buff *skb, int i, struct page *page,
			 int off, int size, unsigned int truesize)
{
	skb_fill_page_desc(skb, i, page, off, size);
	skb->len += size;
	skb->data_len += size;
	skb->truesize += truesize;
}

#endif /* < 3.4.0 */
#if (KERNEL_VERSION(3, 8, 0) > LINUX_VERSION_CODE)
/*
 * pci_sriov_get_totalvfs -- get total VFs supported on this device
 * @dev: the PCI PF device
 *
 * For a PCIe device with SRIOV support, return the PCIe
 * SRIOV capability value of TotalVFs.  Otherwise 0.
 */
int pci_sriov_get_totalvfs(struct pci_dev *dev)
{
	int sriov_cap_pos;
	u16 total_vfs = 0;

	if (dev->is_virtfn)
		return 0;

	sriov_cap_pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_SRIOV);
	pci_read_config_word(dev, sriov_cap_pos + PCI_SRIOV_TOTAL_VF,
			     &total_vfs);

	return total_vfs;
}

#endif
#if (KERNEL_VERSION(3, 10, 0) > LINUX_VERSION_CODE)
/*
 * pci_vfs_assigned - returns number of VFs are assigned to a guest
 * @dev: the PCI device
 *
 * Returns number of VFs belonging to this device that are assigned to a guest.
 * If device is not a physical function returns -ENODEV.
 */
int pci_vfs_assigned(struct pci_dev *dev)
{
	unsigned int vfs_assigned = 0;
#ifdef HAVE_PCI_DEV_FLAGS_ASSIGNED
	struct pci_dev *vfdev;
	unsigned short dev_id = 0;
	int sriov_cap_pos;

	/* only search if we are a PF. */
	if (dev->is_virtfn)
		return 0;

	/* determine the device ID for the VFs, the vendor ID will be the
	 * same as the PF so there is no need to check for that one.
	 */
	sriov_cap_pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_SRIOV);
	pci_read_config_word(dev, sriov_cap_pos + PCI_SRIOV_VF_DID, &dev_id);

	/* loop through all the VFs to see if we own any that are assigned. */
	vfdev = pci_get_device(dev->vendor, dev_id, NULL);
	while (vfdev) {
		/* It is considered assigned if it is a virtual function with
		 * our dev as the physical function and the assigned bit is set.
		 */
		if (vfdev->is_virtfn && vfdev->physfn == dev &&
		    vfdev->dev_flags & PCI_DEV_FLAGS_ASSIGNED)
			vfs_assigned++;

		vfdev = pci_get_device(dev->vendor, dev_id, vfdev);
	}
#endif /* HAVE_PCI_DEV_FLAGS_ASSIGNED */

	return vfs_assigned;
}

#endif /* 3.10.0 */
#if (KERNEL_VERSION(3, 13, 0) > LINUX_VERSION_CODE)
int __kc_dma_set_mask_and_coherent(struct device *dev, u64 mask)
{
	int err = dma_set_mask(dev, mask);

	if (!err)
		/*
		 * coherent mask for the same size will always succeed if
		 * dma_set_mask does. However we store the error anyways, due
		 * to some kernels which use gcc's warn_unused_result on their
		 * definition of dma_set_coherent_mask.
		 */
		err = dma_set_coherent_mask(dev, mask);
	return err;
}

void __kc_netdev_rss_key_fill(void *buffer, size_t len)
{
	/* Set of random keys generated using kernel random number generator */
	static const u8 seed[NETDEV_RSS_KEY_LEN] = {0xE6, 0xFA, 0x35, 0x62,
				0x95, 0x12, 0x3E, 0xA3, 0xFB, 0x46, 0xC1, 0x5F,
				0xB1, 0x43, 0x82, 0x5B, 0x6A, 0x49, 0x50, 0x95,
				0xCD, 0xAB, 0xD8, 0x11, 0x8F, 0xC5, 0xBD, 0xBC,
				0x6A, 0x4A, 0xB2, 0xD4, 0x1F, 0xFE, 0xBC, 0x41,
				0xBF, 0xAC, 0xB2, 0x9A, 0x8F, 0x70, 0xE9, 0x2A,
				0xD7, 0xB2, 0x80, 0xB6, 0x5B, 0xAA, 0x9D, 0x20};

	BUG_ON(len > NETDEV_RSS_KEY_LEN);
	memcpy(buffer, seed, len);
}

#endif /* 3.13.0 */
#if (KERNEL_VERSION(3, 14, 0) > LINUX_VERSION_CODE)
int pci_enable_msix_range(struct pci_dev *dev, struct msix_entry *entries,
			  int minvec, int maxvec)
{
	int nvec = maxvec;
	int rc;

	if (maxvec < minvec)
		return -ERANGE;

	do {
		rc = pci_enable_msix(dev, entries, nvec);
		if (rc < 0) {
			return rc;
		} else if (rc > 0) {
			if (rc < minvec)
				return -ENOSPC;
			nvec = rc;
		}
	} while (rc);

	return nvec;
}

#endif
#if (KERNEL_VERSION(3, 16, 0) > LINUX_VERSION_CODE)
#ifdef HAVE_SET_RX_MODE
#ifdef NETDEV_HW_ADDR_T_UNICAST
int __kc_hw_addr_sync_dev(struct netdev_hw_addr_list *list,
			  struct net_device *dev,
			  int (*sync)(struct net_device *,
				      const unsigned char *),
			  int (*unsync)(struct net_device *,
					const unsigned char *))
{
	struct netdev_hw_addr *tmp;
	struct netdev_hw_addr *ha;
	int err;

	/* first go through and flush out any stale entries. */
	list_for_each_entry_safe(ha, tmp, &list->list, list) {
#if (KERNEL_VERSION(3, 10, 0) > LINUX_VERSION_CODE)
		if (!ha->synced || ha->refcount != 1)
#else
		if (!ha->sync_cnt || ha->refcount != 1)
#endif
			continue;

		if (unsync && unsync(dev, ha->addr))
			continue;

		list_del_rcu(&ha->list);
		kfree_rcu(ha, rcu_head);
		list->count--;
	}

	/* go through and sync new entries to the list. */
	list_for_each_entry_safe(ha, tmp, &list->list, list) {
#if (KERNEL_VERSION(3, 10, 0) > LINUX_VERSION_CODE)
		if (ha->synced)
#else
		if (ha->sync_cnt)
#endif
			continue;

		err = sync(dev, ha->addr);
		if (err)
			return err;
#if (KERNEL_VERSION(3, 10, 0) > LINUX_VERSION_CODE)
		ha->synced = true;
#else
		ha->sync_cnt++;
#endif
		ha->refcount++;
	}

	return 0;
}

void __kc_hw_addr_unsync_dev(struct netdev_hw_addr_list *list,
			     struct net_device *dev,
			     int (*unsync)(struct net_device *,
					   const unsigned char *))
{
	struct netdev_hw_addr *tmp;
	struct netdev_hw_addr *ha;

	list_for_each_entry_safe(ha, tmp, &list->list, list) {
#if (KERNEL_VERSION(3, 10, 0) > LINUX_VERSION_CODE)
		if (!ha->synced)
#else
		if (!ha->sync_cnt)
#endif
			continue;

		if (unsync && unsync(dev, ha->addr))
			continue;

#if (KERNEL_VERSION(3, 10, 0) > LINUX_VERSION_CODE)
		ha->synced = false;
#else
		ha->sync_cnt--;
#endif
		if (--ha->refcount)
			continue;

		list_del_rcu(&ha->list);
		kfree_rcu(ha, rcu_head);
		list->count--;
	}
}

#endif /* NETDEV_HW_ADDR_T_UNICAST  */
#ifndef NETDEV_HW_ADDR_T_MULTICAST
int __kc_dev_addr_sync_dev(struct dev_addr_list **list, int *count,
			   struct net_device *dev,
			   int (*sync)(struct net_device *,
				       const unsigned char *),
			   int (*unsync)(struct net_device *,
					 const unsigned char *))
{
	struct dev_addr_list **next = list;
	struct dev_addr_list *da;
	int err;

	/* first go through and flush out any stale entries. */
	while ((da = *next)) {
		if (da->da_synced && da->da_users == 1) {
			if (!unsync || !unsync(dev, da->da_addr)) {
				*next = da->next;
				kfree(da);
				(*count)--;
				continue;
			}
		}
		next = &da->next;
	}

	/* go through and sync new entries to the list. */
	for (da = *list; da; da = da->next) {
		if (da->da_synced)
			continue;

		err = sync(dev, da->da_addr);
		if (err)
			return err;

		da->da_synced++;
		da->da_users++;
	}

	return 0;
}

void __kc_dev_addr_unsync_dev(struct dev_addr_list **list, int *count,
			      struct net_device *dev,
			      int (*unsync)(struct net_device *,
					    const unsigned char *))
{
	struct dev_addr_list *da;

	while ((da = *list) != NULL) {
		if (da->da_synced) {
			if (!unsync || !unsync(dev, da->da_addr)) {
				da->da_synced--;
				if (--da->da_users == 0) {
					*list = da->next;
					kfree(da);
					(*count)--;
					continue;
				}
			}
		}
		list = &da->next;
	}
}
#endif /* NETDEV_HW_ADDR_T_MULTICAST  */
#endif /* HAVE_SET_RX_MODE */
void *__kc_devm_kmemdup(struct device *dev, const void *src, size_t len,
			unsigned int gfp)
{
	void *p;

	p = devm_kzalloc(dev, len, gfp);
	if (p)
		memcpy(p, src, len);

	return p;
}

#endif /* 3.16.0 */
#if (KERNEL_VERSION(3, 19, 0) > LINUX_VERSION_CODE)
#ifdef HAVE_NET_GET_RANDOM_ONCE
static u8 __kc_netdev_rss_key[NETDEV_RSS_KEY_LEN];

void __kc_netdev_rss_key_fill(void *buffer, size_t len)
{
	BUG_ON(len > sizeof(__kc_netdev_rss_key));
	net_get_random_once(__kc_netdev_rss_key, sizeof(__kc_netdev_rss_key));
	memcpy(buffer, __kc_netdev_rss_key, len);
}

#endif
#endif
#if (KERNEL_VERSION(4, 1, 0) > LINUX_VERSION_CODE)
unsigned int cpumask_local_spread(unsigned int i, int node)
{
	int cpu;

	/* Wrap: we always want a cpu. */
	i %= num_online_cpus();

	if (node == -1) {
		for_each_cpu(cpu, cpu_online_mask)
			if (i-- == 0)
				return cpu;
	} else {
		/* NUMA first. */
		for_each_cpu_and(cpu, cpumask_of_node(node), cpu_online_mask)
			if (i-- == 0)
				return cpu;

		for_each_cpu(cpu, cpu_online_mask) {
			/* Skip NUMA nodes, done above. */
			if (cpumask_test_cpu(cpu, cpumask_of_node(node)))
				continue;

			if (i-- == 0)
				return cpu;
		}
	}
	BUG();
}

#endif
struct file *file_creat(const char *file_name)
{
	return filp_open(file_name, O_CREAT | O_RDWR | O_APPEND, 0);
}

struct file *file_open(const char *file_name)
{
	return filp_open(file_name, O_RDONLY, 0);
}

void file_close(struct file *file_handle)
{
	(void)filp_close(file_handle, NULL);
}

u32 get_file_size(struct file *file_handle)
{
	struct inode *file_inode;

	#if (KERNEL_VERSION(3, 19, 0) > LINUX_VERSION_CODE)
	file_inode = file_handle->f_dentry->d_inode;
	#else
	file_inode = file_handle->f_inode;
	#endif

	return (u32)(file_inode->i_size);
}

void set_file_position(struct file *file_handle, u32 position)
{
	file_handle->f_pos = position;
}

int file_read(struct file *file_handle, char *log_buffer,
	      u32 rd_length, u32 *file_pos)
{
	return (int)file_handle->f_op->read(file_handle, log_buffer,
					    rd_length, &file_handle->f_pos);
}

u32 file_write(struct file *file_handle, char *log_buffer, u32 wr_length)
{
	return (u32)file_handle->f_op->write(file_handle, log_buffer,
					     wr_length, &file_handle->f_pos);
}

static int _linux_thread_func(void *thread)
{
	struct sdk_thread_info *info = (struct sdk_thread_info *)thread;

	while (!kthread_should_stop())
		info->thread_fn(info->data);

	return 0;
}

int creat_thread(struct sdk_thread_info *thread_info)
{
	thread_info->thread_obj = kthread_run(_linux_thread_func,
					      thread_info, thread_info->name);
	if (!thread_info->thread_obj)
		return -EFAULT;

	return 0;
}

void stop_thread(struct sdk_thread_info *thread_info)
{
	if (thread_info->thread_obj)
		(void)kthread_stop(thread_info->thread_obj);
}

void utctime_to_localtime(u64 utctime, u64 *localtime)
{
	*localtime = utctime - sys_tz.tz_minuteswest * OSSL_MINUTE_BASE;
}

#ifndef HAVE_TIMER_SETUP
void initialize_timer(void *adapter_hdl, struct timer_list *timer)
{
	if (!adapter_hdl || !timer)
		return;

	init_timer(timer);
}
#endif

void add_to_timer(struct timer_list *timer, long period)
{
	if (!timer)
		return;

	add_timer(timer);
}

void stop_timer(struct timer_list *timer)
{
}

void delete_timer(struct timer_list *timer)
{
	if (!timer)
		return;

	del_timer_sync(timer);
}

int local_atoi(const char *name)
{
	int val = 0;

	for (;; name++) {
		switch (*name) {
		case '0' ... '9':
			val = 10 * val + (*name - '0');
			break;
		default:
			return val;
		}
	}
}
