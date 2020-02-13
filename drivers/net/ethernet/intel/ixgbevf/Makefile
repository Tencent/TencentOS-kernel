################################################################################
#
# Intel 82599 Virtual Function driver
# Copyright (c) 1999 - 2014 Intel Corporation.
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

###########################################################################
# Driver files

# core driver files
obj-$(CONFIG_IXGBEVF) += ixgbevf.o
ixgbevf-objs = ixgbevf_main.o ixgbevf_ethtool.o kcompat.o \
         ixgbe_vf.o ixgbe_mbx.o

ixgbevf-${CONFIG_PCI_HYPERV} += ixgbe_hv_vf.o
