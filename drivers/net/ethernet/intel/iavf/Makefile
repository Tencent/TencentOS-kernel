# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2013, Intel Corporation.

ccflags-y += -I$(src)
subdir-ccflags-y += -I$(src)

obj-$(CONFIG_IAVF) += iavf.o

iavf-y := iavf_main.o \
	iavf_ethtool.o \
	iavf_virtchnl.o \
	iavf_adminq.o	 \
	iavf_common.o	 \
	iavf_txrx.o	 \
	kcompat.o

iavf-$(CONFIG_PTP_1588_CLOCK:m=y) += iavf_ptp.o
