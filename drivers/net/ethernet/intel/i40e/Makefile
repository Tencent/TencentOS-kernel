################################################################################
#
# Intel(R) 40-10 Gigabit Ethernet Connection Network Driver
# Copyright(c) 2013 - 2017 Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# The full GNU General Public License is included in this distribution in
# the file called "COPYING".
#
# Contact Information:
# e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
# Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
#
################################################################################

ccflags-y += -I$(src)
obj-$(CONFIG_I40E) += i40e.o

i40e-y := i40e_main.o \
	i40e_ethtool.o \
	i40e_adminq.o \
	i40e_common.o \
	i40e_hmc.o \
	i40e_lan_hmc.o \
	i40e_nvm.o \
	i40e_ddp.o \
	i40e_debugfs.o \
	i40e_diag.o \
	i40e_filters.o \
	i40e_txrx.o \
	i40e_ptp.o \
	i40e_client.o \
	kcompat_vfd.o \
	kcompat.o \
	i40e_virtchnl_pf.o

i40e-$(CONFIG_DCB) += i40e_dcb.o i40e_dcb_nl.o
