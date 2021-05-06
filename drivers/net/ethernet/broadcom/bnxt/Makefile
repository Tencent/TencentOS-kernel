#!/usr/bin/make
# Makefile for building Linux Broadcom Gigabit ethernet driver as a module.
# $id$
KVER=
ifeq ($(KVER),)
  KVER=$(shell uname -r)
endif

KVER_MAJ=$(shell echo $(KVER) | cut -d "." -f1)

__ARCH=$(shell uname -m)

# PREFIX may be set by the RPM build to set the effective root.
PREFIX=
ifeq ($(shell ls /lib/modules/$(KVER)/build > /dev/null 2>&1 && echo build),)
# SuSE source RPMs
  _KVER=$(shell echo $(KVER) | cut -d "-" -f1,2)
  _KFLA=$(shell echo $(KVER) | cut -d "-" -f3)
  _ARCH=$(shell file -b /lib/modules/$(shell uname -r)/build | cut -d "/" -f5)
  ifeq ($(_ARCH),)
    _ARCH=$(__ARCH)
  endif
  ifeq ($(shell ls /usr/src/linux-$(_KVER)-obj > /dev/null 2>&1 && echo linux),)
    ifeq ($(shell ls /usr/src/kernels/$(KVER)-$(__ARCH) > /dev/null 2>&1 && echo linux),)
      LINUX=
    else
      LINUX=/usr/src/kernels/$(KVER)-$(__ARCH)
      LINUXSRC=$(LINUX)
    endif
  else
    LINUX=/usr/src/linux-$(_KVER)-obj/$(_ARCH)/$(_KFLA)
    LINUXSRC=/usr/src/linux-$(_KVER)
  endif
else
  LINUX=/lib/modules/$(KVER)/build
  ifeq ($(shell ls /lib/modules/$(KVER)/source > /dev/null 2>&1 && echo source),)
    LINUXSRC=$(LINUX)
  else
    LINUXSRC=/lib/modules/$(KVER)/source
  endif
endif

KDIR ?= $(srctree)
ifneq ($(KDIR),)
  LINUX=$(KDIR)
  LINUXSRC=$(LINUX)
endif

ifeq ($(shell ls $(LINUXSRC)/include/uapi/linux > /dev/null 2>&1 && echo uapi),)
  UAPI=
else
  UAPI=uapi
endif

ifeq ($(BCMMODDIR),)
  ifeq ($(shell ls /lib/modules/$(KVER)/updates > /dev/null 2>&1 && echo 1),1)
    BCMMODDIR=/lib/modules/$(KVER)/updates
  else
    ifeq ($(shell grep -q "search.*[[:space:]]updates" /etc/depmod.conf > /dev/null 2>&1 && echo 1),1)
      BCMMODDIR=/lib/modules/$(KVER)/updates
    else
      ifeq ($(shell grep -q "search.*[[:space:]]updates" /etc/depmod.d/* > /dev/null 2>&1 && echo 1),1)
        BCMMODDIR=/lib/modules/$(KVER)/updates
      else
        ifeq ($(shell expr $(KVER_MAJ) \>= 3), 1)
          BCMMODDIR=/lib/modules/$(KVER)/updates
        else
          BCMMODDIR=/lib/modules/$(KVER)/kernel/drivers/net
        endif
      endif
    endif
  endif
endif

ifneq ($(shell grep -o "pci_enable_msix_range" $(LINUXSRC)/include/linux/pci.h),)
  DISTRO_CFLAG = -DHAVE_MSIX_RANGE
else
  DISTRO_CFLAG =
endif

ifneq ($(shell grep -o "msix_cap" $(LINUXSRC)/include/linux/pci.h),)
  ifeq ($(shell grep -o "pci_dev_rh1" $(LINUXSRC)/include/linux/pci.h),)
    DISTRO_CFLAG += -DHAVE_MSIX_CAP
  endif
endif

ifneq ($(shell grep -so "GENL_ID_GENERATE" $(LINUXSRC)/include/uapi/linux/genetlink.h),)
  DISTRO_CFLAG += -DHAVE_GENL_ID_GENERATE
endif

ifneq ($(shell grep "genl_register_family_with_ops" $(LINUXSRC)/include/net/genetlink.h),)
  DISTRO_CFLAG += -DHAVE_GENL_REG_FAMILY_WITH_OPS
endif

ifneq ($(shell grep -A 8 "genl_family {" $(LINUXSRC)/include/net/genetlink.h | grep -o "struct nla_policy"),)
  DISTRO_CFLAG += -DHAVE_GENL_POLICY
endif

ifneq ($(shell grep "hlist_for_each_entry_safe" $(LINUXSRC)/include/linux/list.h | grep "tpos" > /dev/null 2>&1 && echo tpos),)
  DISTRO_CFLAG += -DHAVE_OLD_HLIST
endif

ifneq ($(shell grep -o "csum_level" $(LINUXSRC)/include/linux/skbuff.h),)
  DISTRO_CFLAG += -DHAVE_CSUM_LEVEL
endif

ifneq ($(shell grep -o "build_skb" $(LINUXSRC)/include/linux/skbuff.h),)
  DISTRO_CFLAG += -DHAVE_BUILD_SKB
  ifneq ($(shell grep "build_skb" $(LINUXSRC)/include/linux/skbuff.h | grep "int frag_size" > /dev/null 2>&1 && echo frag_size),)
    DISTRO_CFLAG += -DHAVE_NEW_BUILD_SKB
  endif
endif

ifneq ($(shell grep -o "inner_network_offset" $(LINUXSRC)/include/linux/skbuff.h),)
  DISTRO_CFLAG += -DHAVE_INNER_NETWORK_OFFSET
endif

ifeq ($(shell grep -o "skb_frag_size" $(LINUXSRC)/include/linux/skbuff.h),)
  DISTRO_CFLAG += -DNO_SKB_FRAG_SIZE
endif

ifneq ($(shell grep -so "n_proto" $(LINUXSRC)/include/net/flow_keys.h),)
  DISTRO_CFLAG += -DHAVE_N_PROTO
endif

ifneq ($(shell grep -so "flow_keys" $(LINUXSRC)/include/net/flow_keys.h),)
  DISTRO_CFLAG += -DHAVE_FLOW_KEYS
endif

ifneq ($(shell grep -o "skb_get_hash_raw" $(LINUXSRC)/include/linux/skbuff.h),)
  DISTRO_CFLAG += -DHAVE_GET_HASH_RAW
endif

ifneq ($(shell grep -o "PKT_HASH_TYPE" $(LINUXSRC)/include/linux/skbuff.h),)
  DISTRO_CFLAG += -DHAVE_SKB_HASH_TYPE
endif

ifneq ($(shell grep -o "SKB_GSO_UDP_TUNNEL_CSUM" $(LINUXSRC)/include/linux/skbuff.h),)
  DISTRO_CFLAG += -DHAVE_SKB_GSO_UDP_TUNNEL_CSUM
else
ifneq ($(shell grep -o "SKB_GSO_UDP_TUNNEL" $(LINUXSRC)/include/linux/skbuff.h),)
  DISTRO_CFLAG += -DHAVE_SKB_GSO_UDP_TUNNEL
endif
endif

ifneq ($(shell grep -o "skb_frag_page" $(LINUXSRC)/include/linux/skbuff.h),)
  DISTRO_CFLAG += -DHAVE_SKB_FRAG_PAGE
endif

ifneq ($(shell grep -o "skb_frag_off_add" $(LINUXSRC)/include/linux/skbuff.h),)
  DISTRO_CFLAG += -DHAVE_SKB_FRAG_ACCESSORS
endif

ifneq ($(shell grep "skb_checksum_none_assert" $(LINUXSRC)/include/linux/skbuff.h > /dev/null 2>&1 && echo skb_cs_none_assert),)
  DISTRO_CFLAG += -DHAVE_SKB_CHECKSUM_NONE_ASSERT
endif

ifneq ($(shell grep -o "xmit_more" $(LINUXSRC)/include/linux/skbuff.h),)
  DISTRO_CFLAG += -DHAVE_SKB_XMIT_MORE
endif

ifneq ($(shell grep -so "min_tx_rate" $(LINUXSRC)/include/$(UAPI)/linux/if_link.h),)
  DISTRO_CFLAG += -DHAVE_IFLA_TX_RATE
endif

ifneq ($(shell grep -so "IFLA_XDP_PROG_ID" $(LINUXSRC)/include/$(UAPI)/linux/if_link.h),)
  DISTRO_CFLAG += -DHAVE_IFLA_XDP_PROG_ID
endif

ifneq ($(shell grep -o "dma_set_mask_and_coherent" $(LINUXSRC)/include/linux/dma-mapping.h),)
  DISTRO_CFLAG += -DHAVE_SET_MASK_AND_COHERENT
endif

ifneq ($(shell grep -o "dma_set_coherent_mask" $(LINUXSRC)/include/linux/dma-mapping.h),)
  DISTRO_CFLAG += -DHAVE_SET_COHERENT_MASK
endif

ifneq ($(shell grep "dma_buf_export" $(LINUXSRC)/include/linux/dma-buf.h | grep "exp_info"),)
  DISTRO_CFLAG += -DHAVE_DMA_EXPORT
endif

ifneq ($(shell grep -w "dma_buf_export" $(LINUXSRC)/include/linux/dma-buf.h | grep "define"),)
  ifneq ($(shell grep "dma_buf_export" $(LINUXSRC)/include/linux/dma-buf.h | grep "resv"),)
    DISTRO_CFLAG += -DHAVE_DMA_EXPORT_FLAG_RESV
  else
    DISTRO_CFLAG += -DHAVE_DMA_EXPORT_FLAG
  endif
endif

ifneq ($(shell grep -w "(*kmap_atomic)" $(LINUXSRC)/include/linux/dma-buf.h),)
  DISTRO_CFLAG += -DHAVE_DMABUF_KMAP_ATOMIC
endif

ifneq ($(shell grep -w "(*kmap)" $(LINUXSRC)/include/linux/dma-buf.h),)
  DISTRO_CFLAG += -DHAVE_DMABUF_KMAP
endif

ifneq ($(shell grep -w "(*map_atomic)" $(LINUXSRC)/include/linux/dma-buf.h),)
  DISTRO_CFLAG += -DHAVE_DMABUF_MAP_ATOMIC
endif

ifneq ($(shell grep -w "(*map)" $(LINUXSRC)/include/linux/dma-buf.h),)
  DISTRO_CFLAG += -DHAVE_DMABUF_MAP
endif

ifneq ($(shell grep -w "attach" $(LINUXSRC)/include/linux/dma-buf.h | grep "struct device"),)
  DISTRO_CFLAG += -DHAVE_DMABUF_ATTACH_DEV
endif

ifneq ($(shell ls $(LINUXSRC)/include/linux/dma-attrs.h > /dev/null 2>&1 && echo dma_attrs),)
  DISTRO_CFLAG += -DHAVE_DMA_ATTRS_H
endif

ifneq ($(shell grep -o "dma_map_page_attrs" $(LINUXSRC)/include/linux/dma-mapping.h),)
  DISTRO_CFLAG += -DHAVE_DMA_MAP_PAGE_ATTRS
else
  ifneq ($(shell grep -so "dma_map_page_attrs" $(LINUXSRC)/include/asm-generic/dma-mapping-common.h),)
    DISTRO_CFLAG += -DHAVE_DMA_MAP_PAGE_ATTRS
  endif
endif

ifneq ($(shell grep -o "dma_zalloc_coherent" $(LINUXSRC)/include/linux/dma-mapping.h),)
  DISTRO_CFLAG += -DHAVE_DMA_ZALLOC_COHERENT
endif

ifneq ($(shell grep -o "ndo_udp_tunnel_add" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NDO_UDP_TUNNEL
  ifneq ($(shell grep -A 24 "net_device_ops_extended" $(LINUXSRC)/include/linux/netdevice.h | grep -o "ndo_udp_tunnel_add"),)
    DISTRO_CFLAG += -DHAVE_NDO_UDP_TUNNEL_RH
  endif
else
  ifneq ($(shell grep -o "ndo_add_vxlan_port" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DHAVE_NDO_ADD_VXLAN
    ifneq ($(shell grep -so "vxlan_get_rx_port" $(LINUXSRC)/include/net/vxlan.h),)
      DISTRO_CFLAG += -DHAVE_VXLAN_GET_RX_PORT
    endif
  endif
endif

ifneq ($(shell grep -o "struct dev_addr_list" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_DEV_ADDR_LIST
endif

ifneq ($(shell grep "netif_set_real_num_tx" $(LINUXSRC)/include/linux/netdevice.h > /dev/null 2>&1 && echo real_tx),)
  DISTRO_CFLAG += -DHAVE_NETIF_SET_REAL_NUM_TX
else
  DISTRO_CFLAG += -DVOID_NETIF_SET_NUM_TX
endif

ifneq ($(shell grep "netif_set_real_num_tx" $(LINUXSRC)/include/linux/netdevice.h | grep void > /dev/null 2>&1 && echo netif_set_real),)
  DISTRO_CFLAG += -DVOID_NETIF_SET_NUM_TX
endif

ifneq ($(shell grep -o "netdev_tx_sent_queue" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NETDEV_TX_QUEUE_CTRL
endif

ifneq ($(shell ls $(LINUXSRC)/include/net/flow_dissector.h > /dev/null 2>&1 && echo flow),)
  DISTRO_CFLAG += -DNEW_FLOW_KEYS -DHAVE_FLOW_KEYS
  ifneq ($(shell grep -so "static inline bool skb_flow_dissect_flow_keys" $(LINUXSRC)/include/linux/skbuff.h),)
    ifneq ($(shell grep -A 2 "static inline bool skb_flow_dissect_flow_keys" $(LINUXSRC)/include/linux/skbuff.h | grep -o "unsigned int flags"),)
      DISTRO_CFLAG += -DHAVE_NEW_FLOW_DISSECTOR_WITH_FLAGS
    endif
    ifneq ($(shell grep -o "FLOW_DIS_ENCAPSULATION" $(LINUXSRC)/include/net/flow_dissector.h),)
      DISTRO_CFLAG += -DHAVE_NEW_FLOW_DISSECTOR
    endif
  endif
  ifneq ($(shell grep -o "flow_hash_from_keys" $(LINUXSRC)/include/net/flow_dissector.h),)
    DISTRO_CFLAG += -DHAVE_FLOW_HASH_FROM_KEYS
  endif
endif

ifneq ($(shell ls $(LINUXSRC)/include/net/flow_offload.h > /dev/null 2>&1 && echo flow_offload),)
  DISTRO_CFLAG += -DHAVE_FLOW_OFFLOAD_H
  ifneq ($(shell grep -so "struct flow_cls_offload" $(LINUXSRC)/include/net/flow_offload.h),)
    DISTRO_CFLAG += -DHAVE_TC_FLOW_CLS_OFFLOAD
  endif
  ifneq ($(shell grep -o "flow_block_cb_setup_simple" $(LINUXSRC)/include/net/flow_offload.h),)
    DISTRO_CFLAG += -DHAVE_SETUP_TC_BLOCK_HELPER
  endif
  ifneq ($(shell grep -o "__flow_indr_block_cb_register" $(LINUXSRC)/include/net/flow_offload.h),)
    DISTRO_CFLAG += -DHAVE_FLOW_INDR_BLOCK_CB
  endif
  ifneq ($(shell grep -o "FLOW_ACTION_POLICE" $(LINUXSRC)/include/net/flow_offload.h),)
    DISTRO_CFLAG += -DHAVE_FLOW_ACTION_POLICE
  endif
  ifneq ($(shell grep -o "flow_action_basic_hw_stats_check" $(LINUXSRC)/include/net/flow_offload.h),)
    DISTRO_CFLAG += -DHAVE_FLOW_ACTION_BASIC_HW_STATS_CHECK
  endif
endif

ifneq ($(shell ls $(LINUXSRC)/include/net/udp_tunnel.h > /dev/null 2>&1 && echo udp_tunnel),)
  DISTRO_CFLAG += -DHAVE_UDP_TUNNEL_H
endif

ifneq ($(shell grep -o "ether_addr_equal" $(LINUXSRC)/include/linux/etherdevice.h),)
  DISTRO_CFLAG += -DHAVE_ETHER_ADDR_EQUAL
endif

ifneq ($(shell grep -o "ether_addr_copy" $(LINUXSRC)/include/linux/etherdevice.h),)
  DISTRO_CFLAG += -DHAVE_ETHER_ADDR_COPY
endif

ifneq ($(shell grep -o "eth_broadcast_addr" $(LINUXSRC)/include/linux/etherdevice.h),)
  DISTRO_CFLAG += -DHAVE_ETH_BROADCAST_ADDR
endif

ifneq ($(shell grep -o "eth_get_headlen" $(LINUXSRC)/include/linux/etherdevice.h),)
  DISTRO_CFLAG += -DHAVE_ETH_GET_HEADLEN
endif

ifneq ($(shell grep -o "eth_hw_addr_random" $(LINUXSRC)/include/linux/etherdevice.h),)
  DISTRO_CFLAG += -DHAVE_ETH_HW_ADDR_RANDOM
endif

ifneq ($(shell grep -s "eth_get_headlen" $(LINUXSRC)/include/linux/etherdevice.h | grep -o "struct net_device"),)
  DISTRO_CFLAG += -DHAVE_ETH_GET_HEADLEN_NEW
endif

ifneq ($(shell grep -o "get_rxnfc" $(LINUXSRC)/include/linux/ethtool.h),)
  DISTRO_CFLAG += -DHAVE_RXNFC
  ifneq ($(shell grep -A 2 "get_rxnfc" $(LINUXSRC)/include/linux/ethtool.h | grep -o "void"),)
    DISTRO_CFLAG += -DHAVE_RXNFC_VOID
  endif
endif

ifneq ($(shell grep -o "get_rxfh_key_size" $(LINUXSRC)/include/linux/ethtool.h),)
  ifneq ($(shell grep -o "ETH_RSS_HASH_TOP" $(LINUXSRC)/include/linux/ethtool.h),)
    DISTRO_CFLAG += -DHAVE_GET_RXFH_KEY_SIZE
  endif
endif

ifneq ($(shell grep -o "(\*set_rxfh)" $(LINUXSRC)/include/linux/ethtool.h),)
  DISTRO_CFLAG += -DHAVE_SET_RXFH
endif

ifneq ($(shell grep -o "get_rxfh_indir_size" $(LINUXSRC)/include/linux/ethtool.h),)
  DISTRO_CFLAG += -DHAVE_RXFH_INDIR_SIZE
endif

ifneq ($(shell grep -o "set_phys_id" $(LINUXSRC)/include/linux/ethtool.h),)
  DISTRO_CFLAG += -DHAVE_SET_PHYS_ID
endif

ifneq ($(shell grep -so "ETHTOOL_LINK_MODE_25000baseCR_Full_BIT" $(LINUXSRC)/include/$(UAPI)/linux/ethtool.h),)
  ifeq ($(shell grep -o "^struct new_ethtool_ops {" $(LINUXSRC)/include/linux/ethtool.h),)
    DISTRO_CFLAG += -DHAVE_ETHTOOL_GLINKSETTINGS_25G
  endif
endif

ifneq ($(shell grep -so "ETHTOOL_LINK_MODE_50000baseCR_Full_BIT" $(LINUXSRC)/include/$(UAPI)/linux/ethtool.h),)
    DISTRO_CFLAG += -DHAVE_ETHTOOL_GLINKSETTINGS_PAM4
endif

ifneq ($(shell grep -so "ethtool_tcpip6_spec" $(LINUXSRC)/include/$(UAPI)/linux/ethtool.h),)
  DISTRO_CFLAG += -DHAVE_ETHTOOL_IP6_SPEC
endif

ifeq ($(shell grep -o "rx_cpu_rmap" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DNO_NETDEV_CPU_RMAP
else
  ifneq ($(shell grep -o "irq_run_affinity_notifiers" $(LINUXSRC)/include/linux/interrupt.h),)
    DISTRO_CFLAG += -DNO_NETDEV_CPU_RMAP
  endif
endif

ifneq ($(shell grep -o "hw_features" $(LINUXSRC)/include/linux/netdevice.h),)
  ifeq ($(shell grep -o "get_netdev_hw_features" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DNETDEV_HW_FEATURES
  endif
endif

ifneq ($(shell grep -o "netdev_notifier_info_to_dev" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NETDEV_NOTIFIER_INFO_TO_DEV
endif

ifneq ($(shell grep "register_netdevice_notifier_rh" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_REGISTER_NETDEVICE_NOTIFIER_RH
endif

ifneq ($(shell grep -o "hw_enc_features" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DNETDEV_HW_ENC_FEATURES
endif

ifneq ($(shell grep -o "sriov_configure" $(LINUXSRC)/include/linux/pci.h),)
  DISTRO_CFLAG += -DPCIE_SRIOV_CONFIGURE
  ifneq ($(shell grep -A 2 "pci_driver_rh" $(LINUXSRC)/include/linux/pci.h | \
                 grep -o "sriov_configure"),)
    DISTRO_CFLAG += -DSRIOV_CONF_DEF_IN_PCI_DRIVER_RH
  endif
endif

ifneq ($(shell grep -o "pci_vfs_assigned" $(LINUXSRC)/include/linux/pci.h),)
  DISTRO_CFLAG += -DHAVE_PCI_VFS_ASSIGNED
endif

ifneq ($(shell grep -o "pci_num_vf" $(LINUXSRC)/include/linux/pci.h),)
  DISTRO_CFLAG += -DHAVE_PCI_NUM_VF
endif

ifneq ($(shell grep -o "ndo_fix_features" $(LINUXSRC)/include/linux/netdevice.h),)
  ifeq ($(shell grep -o "net_device_ops_ext" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DNETDEV_FEATURE_CONTROL
  endif
  ifneq ($(shell grep -o "net_device_ops_extended" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DNETDEV_FEATURE_CONTROL
  endif
endif

ifneq ($(shell grep -o "ndo_rx_flow_steer" $(LINUXSRC)/include/linux/netdevice.h),)
  ifeq ($(shell grep -o "netdev_rfs_info" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DNETDEV_RX_FLOW_STEER
  endif
endif

ifneq ($(shell grep -o "ndo_busy_poll" $(LINUXSRC)/include/linux/netdevice.h),)
  ifeq ($(shell grep -o "net_device_extended" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DNETDEV_BUSY_POLL
  endif
endif

ifneq ($(shell grep -o "ndo_get_stats64" $(LINUXSRC)/include/linux/netdevice.h),)
  ifeq ($(shell grep -o "net_device_ops_ext" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DNETDEV_GET_STATS64
  endif
  ifneq ($(shell grep -o "net_device_ops_extended" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DNETDEV_GET_STATS64
  endif
  ifneq ($(shell grep "ndo_get_stats64" $(LINUXSRC)/include/linux/netdevice.h | grep -o "void"),)
    DISTRO_CFLAG += -DNETDEV_GET_STATS64_VOID
  endif
endif

ifneq ($(shell grep -o "ndo_get_vf_config" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NDO_GET_VF_CONFIG
endif

ifneq ($(shell grep -A 2 "ndo_bridge_getlink" $(LINUXSRC)/include/linux/netdevice.h | grep -o "nlflags"),)
  ifneq ($(shell grep -A 3 "ndo_dflt_bridge_getlink" $(LINUXSRC)/include/linux/rtnetlink.h | grep -o "filter_mask"),)
    DISTRO_CFLAG += -DHAVE_NDO_BRIDGE_GETLINK
  endif
endif

ifneq ($(shell grep -A 4 "ndo_bridge_setlink" $(LINUXSRC)/include/linux/netdevice.h | grep -o "netlink_ext_ack"),)
  DISTRO_CFLAG += -DHAVE_NDO_BRIDGE_SETLINK_EXTACK
endif

ifneq ($(shell grep -o "ndo_set_vf_link_state" $(LINUXSRC)/include/linux/netdevice.h),)
  ifeq ($(shell grep -o "net_device_ops_ext" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DHAVE_NDO_SET_VF_LINK_STATE
  endif
  ifneq ($(shell grep -o "net_device_ops_extended" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DHAVE_NDO_SET_VF_LINK_STATE
  endif
endif

ifneq ($(shell grep -o "ndo_set_vf_spoofchk" $(LINUXSRC)/include/linux/netdevice.h),)
  ifeq ($(shell grep -o "net_device_ops_ext" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DHAVE_VF_SPOOFCHK
  endif
  ifneq ($(shell grep -o "net_device_ops_extended" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DHAVE_VF_SPOOFCHK
  endif
endif

ifneq ($(shell grep -A 1 "ndo_set_vf_vlan" $(LINUXSRC)/include/linux/netdevice.h | grep -o "proto"),)
  ifeq ($(shell grep -o "RH_KABI_EXTEND(struct net_device_ops_extended extended)" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DNEW_NDO_SET_VF_VLAN
  endif
endif

ifneq ($(shell grep -o "ndo_set_vf_vlan_rh73" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NDO_SET_VF_VLAN_RH73
endif

ifneq ($(shell grep -o "ndo_set_vf_trust" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NDO_SET_VF_TRUST
  ifneq ($(shell grep -A 3 "net_device_ops_extended" $(LINUXSRC)/include/linux/netdevice.h | grep -o "ndo_set_vf_trust"),)
    DISTRO_CFLAG += -DHAVE_NDO_SET_VF_TRUST_RH
  endif
endif

ifneq ($(shell grep -o "ndo_set_vf_queues" $(LINUXSRC)/include/linux/netdevice.h),)
  ifeq ($(shell grep -o "net_device_ops_ext" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DHAVE_NDO_SET_VF_QUEUES
  endif
endif

ifneq ($(shell grep -o "ndo_change_mtu_rh74" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NDO_CHANGE_MTU_RH74
endif

ifneq ($(shell grep -o "RH_KABI_USE_P(16, struct net_device_extended \*extended)" $(LINUXSRC)/include/linux/netdevice.h),)
        DISTRO_CFLAG += -DHAVE_NET_DEVICE_EXT
endif

ifneq ($(shell grep -o "RH_KABI_EXTEND(struct net_device_ops_extended extended)" $(LINUXSRC)/include/linux/netdevice.h),)
  ifneq ($(shell grep -o "ndo_get_phys_port_name" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DHAVE_EXT_GET_PHYS_PORT_NAME
  endif
endif

ifneq ($(shell grep -o "ndo_setup_tc_rh72" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NDO_SETUP_TC_RH72
endif

ifneq ($(shell grep -o "(\*ndo_setup_tc_rh)" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NDO_SETUP_TC_RH
endif

ifneq ($(shell grep -o "tc_setup_cb_egdev_call" $(LINUXSRC)/include/net/act_api.h),)
  DISTRO_CFLAG += -DHAVE_TC_CB_EGDEV
endif

ifneq ($(shell grep -o "ndo_setup_tc" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_SETUP_TC
  ifneq ($(shell grep -o "struct tc_to_netdev" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DHAVE_TC_TO_NETDEV
    ifneq ($(shell grep -o "struct tc_mqprio_qopt" $(LINUXSRC)/include/linux/netdevice.h),)
      DISTRO_CFLAG += -DHAVE_MQPRIO_QOPT
    endif
    ifneq ($(shell grep -A 1 "ndo_setup_tc" $(LINUXSRC)/include/linux/netdevice.h | grep -o "u32 chain_index"),)
      DISTRO_CFLAG += -DHAVE_CHAIN_INDEX
    endif
  endif
  ifneq ($(shell grep -o "enum tc_setup_type" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DHAVE_TC_SETUP_TYPE
  endif
  ifneq ($(shell grep -so "tc_cls_flower_offload" $(LINUXSRC)/include/net/pkt_cls.h),)
    DISTRO_CFLAG += -DHAVE_TC_CLS_FLOWER_OFFLOAD
  endif
  ifneq ($(shell grep -o "TC_SETUP_BLOCK" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DHAVE_TC_SETUP_BLOCK
  endif
  ifneq ($(shell grep -o "TC_SETUP_QDISC_MQPRIO" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DHAVE_TC_SETUP_QDISC_MQPRIO
  endif
  ifneq ($(shell grep -so "tcf_mirred_dev" $(LINUXSRC)/include/net/tc_act/tc_mirred.h),)
    DISTRO_CFLAG += -DHAVE_TCF_MIRRED_DEV
  endif
  ifneq ($(shell grep -so "tc_cls_can_offload_and_chain0" $(LINUXSRC)/include/net/pkt_cls.h),)
    DISTRO_CFLAG += -DHAVE_TC_CLS_CAN_OFFLOAD_AND_CHAIN0
  endif
  ifneq ($(shell grep -s -A 2 "tcf_block_cb_register" $(LINUXSRC)/include/net/pkt_cls.h | grep -o "netlink_ext_ack"),)
    DISTRO_CFLAG += -DHAVE_TC_CB_REG_EXTACK
  endif
  ifneq ($(shell grep -so "tcf_exts_for_each_action" $(LINUXSRC)/include/net/pkt_cls.h),)
    DISTRO_CFLAG += -DHAVE_TC_EXTS_FOR_ACTION
  endif
  ifneq ($(shell grep -s -A 3 "struct tc_cls_matchall_offload" $(LINUXSRC)/include/net/pkt_cls.h | grep -o "flow_rule"),)
    DISTRO_CFLAG += -DHAVE_TC_MATCHALL_FLOW_RULE
  endif
endif

ifneq ($(shell grep -so "FLOW_DISSECTOR_KEY_ICMP" $(LINUXSRC)/include/net/flow_dissector.h),)
  DISTRO_CFLAG += -DHAVE_FLOW_DISSECTOR_KEY_ICMP
endif

ifneq ($(shell grep -so "rhashtable" $(LINUXSRC)/include/linux/rhashtable.h),)
    DISTRO_CFLAG += -DHAVE_RHASHTABLE
endif

ifneq ($(shell grep -so "tcf_exts_to_list" $(LINUXSRC)/include/net/pkt_cls.h),)
    DISTRO_CFLAG += -DHAVE_TCF_EXTS_TO_LIST
endif

ifneq ($(shell grep -so "tcf_exts_stats_update" $(LINUXSRC)/include/net/pkt_cls.h),)
    DISTRO_CFLAG += -DHAVE_TCF_STATS_UPDATE
endif

ifneq ($(shell grep -so "tcf_exts_has_actions" $(LINUXSRC)/include/net/pkt_cls.h),)
    DISTRO_CFLAG += -DHAVE_TCF_EXTS_HAS_ACTIONS
endif

ifneq ($(shell grep -so "is_tcf_tunnel_set" $(LINUXSRC)/include/net/tc_act/tc_tunnel_key.h),)
  DISTRO_CFLAG += -DHAVE_TCF_TUNNEL
endif

ifneq ($(shell grep -o "netdev_get_num_tc" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_GET_NUM_TC
endif

ifneq ($(shell grep -so "netdev_features_t" $(LINUXSRC)/include/linux/netdev_features.h ||	\
	 grep -o "netdev_features_t" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NETDEV_FEATURES_T
endif

ifneq ($(shell grep -o "ndo_fix_features" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NDO_FIX_FEATURES
endif

ifneq ($(shell grep -o "netif_set_real_num_rx" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NETIF_SET_REAL_NUM_RX
endif

ifneq ($(shell grep -o "netif_get_num_default_rss_queues" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NETIF_GET_DEFAULT_RSS
endif

ifneq ($(shell grep -o "ndo_vlan_rx_register" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_VLAN_RX_REGISTER
endif

ifneq ($(shell grep -o "ndo_get_port_parent_id" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NDO_GET_PORT_PARENT_ID
endif

ifneq ($(shell grep -o "net_device_ops_extended" $(LINUXSRC)/include/linux/netdevice.h),)
  ifneq ($(shell grep -o "ndo_xdp_xmit" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DHAVE_EXT_NDO_XDP_XMIT
  endif
else ifneq ($(shell grep -o "ndo_xdp" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NDO_XDP
  ifneq ($(shell grep -o "ndo_bpf" $(LINUXSRC)/include/linux/netdevice.h),)
    DISTRO_CFLAG += -DHAVE_NDO_BPF
  endif
  ifneq ($(shell ls $(LINUXSRC)/include/net/bpf_trace.h > /dev/null 2>&1 && echo bpf_trace),)
    DISTRO_CFLAG += -DHAVE_BPF_TRACE
  endif
  ifneq ($(shell grep -o "xdp_set_data_meta_invalid" $(LINUXSRC)/include/linux/filter.h),)
    DISTRO_CFLAG += -DHAVE_XDP_SET_DATA_META_INVALID
  endif
  ifneq ($(shell grep -o "void bpf_prog_add" $(LINUXSRC)/include/linux/bpf.h),)
    DISTRO_CFLAG += -DHAVE_VOID_BPF_PROG_ADD
  endif
endif

ifneq ($(shell grep -so "enum xdp_action" $(LINUXSRC)/include/uapi/linux/bpf.h),)
  DISTRO_CFLAG += -DHAVE_XDP_ACTION
endif
ifneq ($(shell grep -so "XDP_REDIRECT" $(LINUXSRC)/include/uapi/linux/bpf.h),)
  DISTRO_CFLAG += -DHAVE_XDP_REDIRECT
endif
ifneq ($(shell grep -so "struct xdp_frame" $(LINUXSRC)/include/net/xdp.h),)
  DISTRO_CFLAG += -DHAVE_XDP_FRAME
endif
ifneq ($(shell grep -so "enum xdp_mem_type" $(LINUXSRC)/include/net/xdp.h),)
  DISTRO_CFLAG += -DHAVE_XDP_MEM_TYPE
endif
ifneq ($(shell grep -so "struct xdp_rxq_info" $(LINUXSRC)/include/net/xdp.h),)
  DISTRO_CFLAG += -DHAVE_XDP_RXQ_INFO
  ifneq ($(shell grep -o "xdp_rxq_info_is_reg" $(LINUXSRC)/include/net/xdp.h),)
      DISTRO_CFLAG += -DHAVE_XDP_RXQ_INFO_IS_REG
  endif
endif

ifneq ($(shell grep -so "xdp_data_hard_end" $(LINUXSRC)/include/net/xdp.h),)
  DISTRO_CFLAG += -DHAVE_XDP_FRAME_SZ
endif

ifneq ($(shell grep -so "page_pool_release_page" $(LINUXSRC)/include/net/page_pool.h),)
  DISTRO_CFLAG += -DHAVE_PAGE_POOL_RELEASE_PAGE
endif

ifneq ($(shell grep -o "netdev_name" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NETDEV_NAME
endif

ifneq ($(shell grep -o "netdev_update_features" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NETDEV_UPDATE_FEATURES
endif

ifneq ($(shell grep -o "napi_hash_add" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NAPI_HASH_ADD
endif

ifneq ($(shell grep -o "napi_hash_del" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NAPI_HASH_DEL
endif

ifneq ($(shell grep "napi_complete_done" $(LINUXSRC)/include/linux/netdevice.h | grep -o "bool"),)
  DISTRO_CFLAG += -DHAVE_NEW_NAPI_COMPLETE_DONE
endif

ifneq ($(shell grep -o "min_mtu" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_MIN_MTU
endif

ifneq ($(shell grep -o "prog_attached" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_PROG_ATTACHED
endif

ifneq ($(shell grep -o "netdev_xmit_more" $(LINUXSRC)/include/linux/netdevice.h),)
  DISTRO_CFLAG += -DHAVE_NETDEV_XMIT_MORE
endif

ifneq ($(shell grep -A 1 "ndo_tx_timeout" $(LINUXSRC)/include/linux/netdevice.h | grep -o txqueue),)
  DISTRO_CFLAG += -DHAVE_NDO_TX_TIMEOUT_QUEUE
endif

ifneq ($(shell grep -o "netpoll_poll_dev" $(LINUXSRC)/include/linux/netpoll.h),)
  DISTRO_CFLAG += -DHAVE_NETPOLL_POLL_DEV
endif

ifneq ($(shell grep -o "prandom_bytes" $(LINUXSRC)/include/linux/random.h),)
  DISTRO_CFLAG += -DHAVE_PRANDOM_BYTES
endif

ifneq ($(shell grep -o "tcp_v6_check" $(LINUXSRC)/include/net/ip6_checksum.h),)
  DISTRO_CFLAG += -DHAVE_TCP_V6_CHECK
endif

ifneq ($(shell grep -o "usleep_range" $(LINUXSRC)/include/linux/delay.h),)
  DISTRO_CFLAG += -DHAVE_USLEEP_RANGE
endif

ifneq ($(shell grep -o "vzalloc" $(LINUXSRC)/include/linux/vmalloc.h),)
  DISTRO_CFLAG += -DHAVE_VZALLOC
endif

ifneq ($(shell grep -o "kmalloc_array" $(LINUXSRC)/include/linux/slab.h),)
  DISTRO_CFLAG += -DHAVE_KMALLOC_ARRAY
endif

ifneq ($(shell grep -o "pcie_capability_read_word" $(LINUXSRC)/include/linux/pci.h),)
  DISTRO_CFLAG += -DHAVE_PCIE_CAPABILITY_READ_WORD
endif

ifneq ($(shell grep -o "pcie_link_width" $(LINUXSRC)/include/linux/pci.h),)
  DISTRO_CFLAG += -DHAVE_PCI_LINK_WIDTH
endif

ifneq ($(shell grep -o "PCIE_SPEED_2_5GT" $(LINUXSRC)/include/linux/pci.h),)
  DISTRO_CFLAG += -DHAVE_PCIE_BUS_SPEED
endif

ifneq ($(shell grep -o "pci_is_bridge" $(LINUXSRC)/include/linux/pci.h),)
  DISTRO_CFLAG += -DHAVE_PCI_IS_BRIDGE
endif

ifneq ($(shell grep -o "pci_upstream_bridge" $(LINUXSRC)/include/linux/pci.h),)
  DISTRO_CFLAG += -DHAVE_PCI_UPSTREAM_BRIDGE
endif

ifneq ($(shell grep -o "pcie_print_link_status" $(LINUXSRC)/include/linux/pci.h),)
  DISTRO_CFLAG += -DHAVE_PCI_PRINT_LINK_STATUS
endif

ifneq ($(shell grep -o "_genl_register_family_with_ops_grps" $(LINUXSRC)/include/net/genetlink.h),)
  DISTRO_CFLAG += -DHAVE_GENL_REG_OPS_GRPS
endif

ifneq ($(shell grep -o "pci_physfn" $(LINUXSRC)/include/linux/pci.h),)
  DISTRO_CFLAG += -DHAVE_PCI_PHYSFN
endif

ifneq ($(shell grep -o "pcie_flr" $(LINUXSRC)/include/linux/pci.h),)
  DISTRO_CFLAG += -DHAVE_PCIE_FLR
endif

ifneq ($(shell grep -o "pci_get_dsn" $(LINUXSRC)/include/linux/pci.h),)
  DISTRO_CFLAG += -DHAVE_PCI_GET_DSN
endif

ifneq ($(shell ls $(LINUXSRC)/include/$(UAPI)/linux/net_tstamp.h > /dev/null 2>&1 && echo net_tstamp),)
  ifneq ($(shell ls $(LINUXSRC)/include/linux/timecounter.h > /dev/null 2>&1 && echo timecounter),)
    ifneq ($(shell ls $(LINUXSRC)/include/linux/timekeeping.h > /dev/null 2>&1 && echo timekeeping),)
      ifneq ($(shell grep -o "HWTSTAMP_FILTER_PTP_V2_EVENT" $(LINUXSRC)/include/$(UAPI)/linux/net_tstamp.h),)
        DISTRO_CFLAG += -DHAVE_IEEE1588_SUPPORT
      endif
    endif
  endif
endif

ifneq ($(shell grep -so "gettimex64" $(LINUXSRC)/include/linux/ptp_clock_kernel.h),)
  DISTRO_CFLAG += -DHAVE_PTP_GETTIMEX64
endif

ifneq ($(shell grep -o "time64_to_tm" $(LINUXSRC)/include/linux/time.h),)
  DISTRO_CFLAG += -DHAVE_TIME64
endif

ifneq ($(shell grep -o "timer_setup" $(LINUXSRC)/include/linux/timer.h),)
  DISTRO_CFLAG += -DHAVE_TIMER_SETUP
endif

ifneq ($(shell grep -s "devlink_ops" $(LINUXSRC)/include/net/devlink.h),)
  DISTRO_CFLAG += -DHAVE_DEVLINK
endif

ifneq ($(shell grep -s "devlink_param" $(LINUXSRC)/include/net/devlink.h),)
  DISTRO_CFLAG += -DHAVE_DEVLINK_PARAM
  ifneq ($(shell grep -s -A 2 "int (\*validate)" $(LINUXSRC)/include/net/devlink.h | grep "struct netlink_ext_ack \*extack"),)
    DISTRO_CFLAG += -DHAVE_DEVLINK_VALIDATE_NEW
  endif
endif

ifneq ($(shell grep -s -A 7 "devlink_port_attrs" $(LINUXSRC)/include/net/devlink.h | grep -o "netdev_phys_item_id"),)
  DISTRO_CFLAG += -DHAVE_DEVLINK_PORT_ATTRS
endif

ifneq ($(shell grep -s "ndo_get_devlink_port" $(LINUXSRC)/include/net/devlink.h),)
  DISTRO_CFLAG += -DHAVE_NDO_DEVLINK_PORT
endif

ifneq ($(shell grep -s "devlink_port_params_register" $(LINUXSRC)/include/net/devlink.h),)
  DISTRO_CFLAG += -DHAVE_DEVLINK_PORT_PARAM
endif

ifneq ($(shell grep -s "devlink_params_publish" $(LINUXSRC)/include/net/devlink.h),)
  DISTRO_CFLAG += -DHAVE_DEVLINK_PARAM_PUBLISH
endif

ifneq ($(shell grep -s -A 1 "eswitch_mode_set" $(LINUXSRC)/include/net/devlink.h | grep -o "netlink_ext_ack"),)
  DISTRO_CFLAG += -DHAVE_ESWITCH_MODE_SET_EXTACK
endif

ifneq ($(shell grep -so "DEVLINK_PARAM_GENERIC_ID_IGNORE_ARI" $(LINUXSRC)/include/net/devlink.h),)
  DISTRO_CFLAG += -DHAVE_IGNORE_ARI
endif

ifneq ($(shell grep -so "info_get" $(LINUXSRC)/include/net/devlink.h),)
  DISTRO_CFLAG += -DHAVE_DEVLINK_INFO
  ifneq ($(shell grep -so "devlink_info_board_serial_number_put" $(LINUXSRC)/include/net/devlink.h),)
    DISTRO_CFLAG += -DHAVE_DEVLINK_INFO_BSN_PUT
  endif
endif

ifneq ($(shell grep -so "flash_update" $(LINUXSRC)/include/net/devlink.h),)
  DISTRO_CFLAG += -DHAVE_DEVLINK_FLASH_UPDATE
endif

ifneq ($(shell grep -so "devlink_flash_update_status_notify" $(LINUXSRC)/include/net/devlink.h),)
  DISTRO_CFLAG += -DHAVE_DEVLINK_FLASH_UPDATE_STATUS
endif

ifneq ($(shell grep -so "struct devlink_health_reporter" $(LINUXSRC)/include/net/devlink.h),)
  DISTRO_CFLAG += -DHAVE_DEVLINK_HEALTH_REPORT
endif

ifneq ($(shell grep -so "devlink_health_reporter_state_update" $(LINUXSRC)/include/net/devlink.h),)
  DISTRO_CFLAG += -DHAVE_DEVLINK_HEALTH_REPORTER_STATE_UPDATE
endif

ifneq ($(shell grep -s -A 1 "(*recover)" $(LINUXSRC)/include/net/devlink.h | grep netlink_ext_ack),)
  DISTRO_CFLAG += -DHAVE_DEVLINK_HEALTH_REPORT_EXTACK
endif

ifneq ($(shell grep -so "devlink_health_reporter_recovery_done" $(LINUXSRC)/include/net/devlink.h),)
  DISTRO_CFLAG += -DHAVE_DEVLINK_HEALTH_REPORTER_RECOVERY_DONE
endif

ifneq ($(shell grep -s -A 2 "devlink_health_reporter_create" $(LINUXSRC)/include/net/devlink.h | grep auto_recover),)
  DISTRO_CFLAG += -DHAVE_DEVLINK_HEALTH_AUTO_RECOVER
endif

# Check if the file exists or not
ifneq ($(shell grep -s "switchdev_ops" $(LINUXSRC)/include/net/switchdev.h),)
  DISTRO_CFLAG += -DHAVE_SWITCHDEV
endif

ifneq ($(shell grep -s "METADATA_HW_PORT_MUX" $(LINUXSRC)/include/net/dst_metadata.h),)
  DISTRO_CFLAG += -DHAVE_METADATA_HW_PORT_MUX
endif

ifneq ($(shell grep -so "(*ieee_delapp)" $(LINUXSRC)/include/net/dcbnl.h),)
  DISTRO_CFLAG += -DHAVE_IEEE_DELAPP
endif

ifneq ($(shell grep -so "dcb_ieee_getapp_prio_dscp_mask_map" $(LINUXSRC)/include/net/dcbnl.h),)
  DISTRO_CFLAG += -DHAVE_DSCP_MASK_MAP
endif

ifneq ($(shell grep -o cpumask_local_spread $(LINUXSRC)/include/linux/cpumask.h),)
  DISTRO_CFLAG += -DHAVE_CPUMASK_LOCAL_SPREAD
endif

ifneq ($(shell grep -o cpumask_set_cpu_local_first $(LINUXSRC)/include/linux/cpumask.h),)
  DISTRO_CFLAG += -DHAVE_CPUMASK_LOCAL_FIRST
endif

ifeq ($(shell grep -so "ETH_RESET_AP" $(LINUXSRC)/include/$(UAPI)/linux/ethtool.h),)
  DISTRO_CFLAG += -DNO_ETH_RESET_AP
endif

ifneq ($(shell ls $(LINUXSRC)/include/linux/dim.h > /dev/null 2>&1 && echo dim),)
  DISTRO_CFLAG += -DHAVE_DIM
endif

ifneq ($(shell grep -o simple_open $(LINUXSRC)/include/linux/fs.h),)
  DISTRO_CFLAG += -DHAVE_SIMPLE_OPEN
endif

ifneq ($(shell grep -o hwmon_device_register_with_groups $(LINUXSRC)/include/linux/hwmon.h),)
  DISTRO_CFLAG += -DHAVE_NEW_HWMON_API
endif

ifneq ($(shell grep -o ETH_RESET_CRASHDUMP $(LINUXSRC)/include/uapi/linux/ethtool.h),)
  DISTRO_CFLAG += -DHAVE_ETHTOOL_RESET_CRASHDUMP
endif

override EXTRA_CFLAGS += ${DISTRO_CFLAG} -g -DCHIMP_FW -D__LINUX -DCONFIG_BNXT_SRIOV -DCONFIG_BNXT_DCB -DCONFIG_BNXT_FLASHDEV -DHSI_DBG_DISABLE -DCONFIG_BNXT_LFC

cflags-y += $(EXTRA_CFLAGS)

BCM_DRV = bnxt_en.ko
ifneq ($(KERNELRELEASE),)

ifeq ($(shell expr $(KVER_MAJ) \>= 5), 1)
        BNXT_DBGFS_OBJ = bnxt_debugfs.o
else
        BNXT_DBGFS_OBJ = bnxt_debugfs_cpt.o
endif

obj-m += bnxt_en.o
bnxt_en-y := bnxt.o bnxt_hwrm.o bnxt_ethtool.o bnxt_sriov.o bnxt_dcb.o bnxt_ulp.o bnxt_xdp.o bnxt_ptp.o bnxt_vfr.o bnxt_tc.o bnxt_devlink.o bnxt_lfc.o bnxt_netlink.o bnxt_dim.o bnxt_eem.o $(BNXT_DBGFS_OBJ) #decode_hsi.o

else

default:
ifeq ($(CROSS_COMPILE),)
	make -C $(LINUX) M=$(shell pwd) modules
else ifneq ($(CROSS_COMPILE),)
	make -C $(LINUXSRC) M=$(shell pwd) modules CROSS_COMPILE=$(CROSS_COMPILE) ARCH=$(ARCH)
endif

yocto_all:
	$(MAKE) -C $(LINUXSRC) M=$(shell pwd)

modules_install:
	$(MAKE) -C $(LINUXSRC) M=$(shell pwd) modules_install

endif

install: default
	@if [ "$(KDIR)" != "" ]; then \
	echo "Cannot use install with KDIR option"; exit 2;\
	fi
	mkdir -p $(PREFIX)$(BCMMODDIR);
	install -m 444 $(BCM_DRV) $(PREFIX)$(BCMMODDIR);
	@if [ "$(PREFIX)" = "" ]; then /sbin/depmod -a ;\
	else echo " *** Run '/sbin/depmod -a' to update the module database.";\
	fi

.PHONEY: all clean install

clean:
	-rm -f bnxt.o bnxt.mod.c bnxt.mod.o .bnxt.*.cmd *.cmd *.markers *.order *.symvers decode_hsi.o .decode_*
	-rm -rf .tmp_versions
	-rm -rf bnxt_en.o bnxt_en.ko bnxt_en.mod.o bnxt_en.mod.c .bnxt_en.* bnxt_sriov.o .bnxt_sriov.* bnxt_ethtool.o .bnxt_ethtool.* bnxt_dcb.o .bnxt_dcb.* bnxt_ulp.o .bnxt_ulp.* bnxt_ptp.o .bnxt_ptp.* bnxt_xdp.o .bnxt_xdp.* bnxt_lfc.o .bnxt_lfc.* bnxt_hwrm.o .bnxt_hwrm.*
	-rm -rf bnxt_vfr.o .bnxt_vfr.* bnxt_tc.o .bnxt_tc.* bnxt_devlink.o .bnxt_devlink.* bnxt_netlink.o .bnxt_netlink.*
	-rm -rf bnxt_dim.o .bnxt_dim.* bnxt_debugfs*.o .bnxt_debugfs* bnxt_eem.o .bnxt_eem*
	-rm -f Module.markers Module.symvers modules.order
