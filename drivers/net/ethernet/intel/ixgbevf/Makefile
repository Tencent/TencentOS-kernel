# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 1999 - 2018 Intel Corporation.
#
# Makefile for the Intel(R) 82599 VF ethernet driver
#

obj-$(CONFIG_IXGBEVF) += ixgbevf.o

ixgbevf-objs := ixgbe_vf.o \
                ixgbe_mbx.o \
                ixgbevf_ethtool.o \
                ixgbevf_main.o \
		kcompat.o

ixgbevf-${CONFIG_PCI_HYPERV:m=y} += ixgbe_hv_vf.o


