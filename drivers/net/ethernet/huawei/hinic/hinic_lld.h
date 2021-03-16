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

#ifndef HINIC_LLD_H_
#define HINIC_LLD_H_

#define HINIC_SLAVE_NIC_DELAY "hinic_slave_nic_delay"
#define HINIC_SLAVE_NIC_DELAY_TIME  (5 * HZ)

struct hinic_lld_dev {
	struct pci_dev *pdev;
	void *hwdev;
};

enum hinic_init_state {
	HINIC_INIT_STATE_NONE,
	HINIC_INIT_STATE_PCI_INITED,
	HINIC_INIT_STATE_HW_IF_INITED,
	HINIC_INIT_STATE_HW_PART_INITED,
	HINIC_INIT_STATE_HWDEV_INITED,
	HINIC_INIT_STATE_DBGTOOL_INITED,
	HINIC_INIT_STATE_NIC_INITED,
	HINIC_INIT_STATE_ALL_INITED,
};

struct hinic_uld_info {
	/* uld_dev: should not return null even the function capability
	 * is not support the up layer driver
	 * uld_dev_name: NIC driver should copy net device name.
	 * FC driver could copy fc device name.
	 * other up layer driver don`t need copy anything
	 */
	int (*probe)(struct hinic_lld_dev *lld_dev,
		     void **uld_dev, char *uld_dev_name);
	void (*remove)(struct hinic_lld_dev *lld_dev, void *uld_dev);
	int (*suspend)(struct hinic_lld_dev *lld_dev,
		       void *uld_dev, pm_message_t state);
	int (*resume)(struct hinic_lld_dev *lld_dev, void *uld_dev);
	void (*event)(struct hinic_lld_dev *lld_dev, void *uld_dev,
		      struct hinic_event_info *event);
	int (*ioctl)(void *uld_dev, u32 cmd, void *buf_in,
		     u32 in_size, void *buf_out, u32 *out_size);
};

/* Used for the ULD HiNIC PCIe driver registration interface,
 * the original interface is service_register_interface
 */
int hinic_register_uld(enum hinic_service_type uld_type,
		       struct hinic_uld_info *uld_info);

/* Used for the ULD HiNIC PCIe driver unregistration interface,
 * the original interface is service_unregister_interface
 */
void hinic_unregister_uld(enum hinic_service_type uld_type);

void *hinic_get_ppf_uld_by_pdev(struct pci_dev *pdev,
				enum hinic_service_type type);

/* used for TOE/IWARP */
struct net_device *hinic_get_netdev_by_lld(struct hinic_lld_dev *lld_dev);
/* used for TOE/IWARP */
void *hinic_get_hwdev_by_netdev(struct net_device *netdev);

struct net_device *hinic_get_netdev_by_pcidev(struct pci_dev *pdev);
void *hinic_get_hwdev_by_ifname(char *ifname);
int hinic_get_chip_name_by_hwdev(void *hwdev, char *ifname);
void *hinic_get_uld_dev_by_ifname(char *ifname, enum hinic_service_type type);
void *hinic_get_uld_by_chip_name(char *ifname, enum hinic_service_type type);

int hinic_get_pf_uld_array(struct pci_dev *pdev, u32 *dev_cnt, void *array[]);
int hinic_set_chip_cos_up_map(struct pci_dev *pdev, u8 *cos_up);
int hinic_get_chip_cos_up_map(struct pci_dev *pdev, bool *is_setted,
			      u8 *cos_up);
void hinic_get_all_chip_id(void *card_id);
void hinic_get_card_info(void *hwdev, void *bufin);
int hinic_get_device_id(void *hwdev, u16 *dev_id);
void get_fc_devname(char *devname);
int hinic_get_pf_id(void *hwdev, u32 port_id, u32 *pf_id, u32 *isvalid);

void hinic_tool_cnt_inc(void);
void hinic_tool_cnt_dec(void);

struct hinic_sriov_info;
struct hinic_sriov_info *hinic_get_sriov_info_by_pcidev(struct pci_dev *pdev);

int hinic_attach_nic(struct hinic_lld_dev *lld_dev);
void hinic_detach_nic(struct hinic_lld_dev *lld_dev);

int hinic_attach_roce(struct hinic_lld_dev *lld_dev);
void hinic_detach_roce(struct hinic_lld_dev *lld_dev);

int hinic_disable_nic_rss(struct hinic_lld_dev *lld_dev);
int hinic_enable_nic_rss(struct hinic_lld_dev *lld_dev);

int hinic_ovs_set_vf_nic_state(struct hinic_lld_dev *lld_dev,
			       u16 vf_func_id, bool en);

int hinic_ovs_set_vf_load_state(struct pci_dev *pdev);

int hinic_get_self_test_result(char *ifname, u32 *result);
enum hinic_init_state hinic_get_init_state_by_ifname(char *ifname);
enum hinic_init_state hinic_get_init_state(struct pci_dev *pdev);

extern struct hinic_uld_info g_uld_info[SERVICE_T_MAX];

struct pci_device_id *hinic_get_pci_device_id(struct pci_dev *pdev);
bool hinic_is_in_host(void);

bool hinic_is_valid_bar_addr(u64 offset);


#endif
