#/*******************************************************************
# * This file is part of the Emulex Linux Device Driver for         *
# * Fibre Channel Host Bus Adapters.                                *
# * Copyright (C) 2017-2020 Broadcom. All Rights Reserved. The term *
# * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.     *
# * Copyright (C) 2004-2012 Emulex.  All rights reserved.           *
# * EMULEX and SLI are trademarks of Emulex.                        *
# * www.broadcom.com                                                *
# *                                                                 *
# * This program is free software; you can redistribute it and/or   *
# * modify it under the terms of version 2 of the GNU General       *
# * Public License as published by the Free Software Foundation.    *
# * This program is distributed in the hope that it will be useful. *
# * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
# * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
# * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
# * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
# * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
# * more details, a copy of which can be found in the file COPYING  *
# * included with this package.                                     *
# *******************************************************************/
######################################################################

SUBLEVEL = $(shell echo ${KERNELVERSION} | \
sed -e 's/[0-9]*.[0-9]*.\([0-9]*\).[0-9]*.*/\1/')
EXTRAVERSION = $(shell echo ${KERNELRELEASE} | \
sed -e 's/[0-9]*.[0-9]*.[0-9]*.\([0-9]*\).*/\1/')
MAJORVERSION = $(shell echo ${KERNELVERSION} | \
sed -e 's/\([0-9]*\).*/\1/')
MINORVERSION = $(shell echo ${KERNELVERSION} | \
sed -e 's/[0-9]*.\([0-9]*\).*/\1/')

EXTRA_CFLAGS += -Werror -DKERNEL_MAJOR=${MAJORVERSION} -DKERNEL_MINOR=${MINORVERSION}

ifneq ($(GCOV),)
  EXTRA_CFLAGS += -fprofile-arcs -ftest-coverage
  EXTRA_CFLAGS += -O0
endif

#
# DISTRO	KERNELVERSION		    SUBLEVEL	EXTRAVERSION
# sles15-sp3	5.3.18-37			18	37
# sles15-sp2	5.3.18-22			18	22
# sles15-sp1	4.12.14-195			14	195
# sles15-sp0	4.12.14-23			14	23
# sles12-sp5	4.12.14-100			14	100
# sles12-sp4	4.12.14-94.4			14	94
# rhel8.4	4.18.0-259.el8					Alpha-1.0
# rhel8.3	4.18.0-240.el8
# rhel8.2	4.18.0-193.el8
# rhel8.1	4.18.0-147.el8
# rhel8.0	4.18.0-80.el8
#

ifeq (,$(findstring -DBUILD_BRCMFCOE,$(BUILD_FLAGS)))
ifeq ("$(wildcard /etc/debian_version)","")
   EXTRA_CFLAGS += -DBUILD_NVME 
endif
endif

# This will pick out a Citrix kernel, xs5.6 / xs6.0 / xs6.1
EXTRAXSINFO = $(shell echo ${KERNELVERSION} | \
grep 'xs[156].[016]' | sed -e 's/.*xs.*/xs/')

ifeq ($(EXTRAXSINFO),xs)
   EXTRA_CFLAGS += -DBUILD_CITRIX_XS
endif
EXTRAXSINFO = $(shell echo ${KERNELVERSION} | \
grep 'el8' | sed -e 's/.*el8.*/el8/')

ifeq ($(EXTRAXSINFO),el8)
   EXTRA_CFLAGS += -DBUILD_RHEL8
   RHEL8_NZ = $(shell echo "${KERNELVERSION} 4.18.0-83.el8.x86_64" | \
                tr " " "\n" | sort -V | head -n1)
ifeq ($(RHEL8_NZ), 4.18.0-83.el8.x86_64)
   EXTRA_CFLAGS += -DBUILD_RHEL8_NZ
endif
ifeq ($(shell test $(EXTRAVERSION) -ge 211; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_RHEL83
endif
# RHEL 8.4 adds NVME1+ Addendum
ifeq ($(shell test $(EXTRAVERSION) -ge 259; echo $$?), 0)
   EXTRA_CFLAGS += -DBUILD_NVME_PLUS
endif
endif

ifneq ("$(wildcard /etc/os-release)","")
   SLES15SP = $(shell grep -q 'sles:15:sp' /etc/os-release && echo 1)
ifeq ($(SLES15SP),1)
   EXTRA_CFLAGS += -DBUILD_SLES15SP
endif
   SLES12SP5 = $(shell grep -q 'sles:12:sp5' /etc/os-release && echo 1)
ifeq ($(SLES12SP5),1)
   EXTRA_CFLAGS += -DBUILD_SLES12SP5
endif
   SLES15SP2 = $(shell grep -q 'sles:15:sp2' /etc/os-release && echo 1)
ifeq ($(SLES15SP2),1)
   EXTRA_CFLAGS += -DBUILD_SLES15SP2 -DBUILD_NVME_PLUS
endif
   SLES15SP3 = $(shell grep -q 'sles:15:sp3' /etc/os-release && echo 1)
ifeq ($(SLES15SP3),1)
   EXTRA_CFLAGS += -DBUILD_SLES15SP2 -DBUILD_NVME_PLUS
endif
endif

# This will pick out an openEuler kernel
EXTRAOE1INFO = $(shell echo ${KERNELVERSION} | grep oe1 | \
	         sed -e 's/.*oe1.*/oe1/')
ifeq ($(EXTRAOE1INFO),oe1)
ifneq ("$(wildcard /etc/os-release)","")
OE1_20 = $(shell grep -q 'VERSION="20.' /etc/os-release && echo 1)
ifeq ($(OE1_20),1)
   EXTRA_CFLAGS += -DBUILD_OE1_20
endif
endif
endif

# This will pick out a xen kernel
EXTRAXENINFO = $(shell echo ${KERNELVERSION} | \
grep 'xen' | sed -e 's/.*xen.*/xen/')

EXTRA_CFLAGS += ${BUILD_FLAGS}

obj-$(CONFIG_SCSI_LPFC) := lpfc.o

lpfc-objs := lpfc_mem.o lpfc_sli.o lpfc_ct.o lpfc_els.o \
	lpfc_hbadisc.o	lpfc_init.o lpfc_mbox.o lpfc_nportdisc.o   \
	lpfc_scsi.o lpfc_attr.o lpfc_vport.o lpfc_debugfs.o lpfc_bsg.o \
	lpfc_nvme.o lpfc_nvmet.o lpfc_auth.o

obj-$(CONFIG_SCSI_BRCMFCOE) := brcmfcoe.o

brcmfcoe-objs := brcmfcoe_mem.o brcmfcoe_sli.o brcmfcoe_ct.o brcmfcoe_els.o brcmfcoe_hbadisc.o	\
	brcmfcoe_init.o brcmfcoe_mbox.o brcmfcoe_nportdisc.o brcmfcoe_scsi.o brcmfcoe_attr.o \
	brcmfcoe_vport.o brcmfcoe_debugfs.o brcmfcoe_bsg.o \
	brcmfcoe_nvme.o brcmfcoe_nvmet.o
