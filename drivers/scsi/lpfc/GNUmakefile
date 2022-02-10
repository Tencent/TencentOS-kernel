#/*******************************************************************
# * This file is part of the Emulex Linux Device Driver for         *
# * Fibre Channel Host Bus Adapters.                                *
# * Copyright (C) 2020 Broadcom. All Rights Reserved. The term      *
# * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.     *
# * Copyright (C) 2004-2009 Emulex.  All rights reserved.           *
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

KERNELVERSION ?= $(shell uname -r)
BASEINCLUDE ?= /lib/modules/$(KERNELVERSION)/build
DRIVERDIR ?= $(PWD)
DEBUG_FS = $(shell grep CONFIG_DEBUG_FS=y $(BASEINCLUDE)/.config)

ifeq ($(DEBUG_FS),CONFIG_DEBUG_FS=y)
  EXTRA_CFLAGS += -DCONFIG_SCSI_LPFC_DEBUG_FS
  export EXTRA_CFLAGS
endif

ifneq ($(GCOV),)
  EXTRA_CFLAGS += -fprofile-arcs -ftest-coverage
  EXTRA_CFLAGS += -O0
  export EXTRA_CFLAGS
endif

clean-files += Modules.symvers Module.symvers Module.markers modules.order

export clean-files

$(info GNUmakefile setting EXTRA_CFLAGS to $(EXTRA_CFLAGS))
default:
	$(MAKE) -C $(BASEINCLUDE) M=$(DRIVERDIR) CONFIG_SCSI_LPFC=m modules

brcmfcoe:
	@cp lpfc_attr.c brcmfcoe_attr.c
	@cp lpfc_bsg.c brcmfcoe_bsg.c
	@cp lpfc_ct.c brcmfcoe_ct.c
	@cp lpfc_debugfs.c brcmfcoe_debugfs.c
	@cp lpfc_els.c brcmfcoe_els.c
	@cp lpfc_hbadisc.c brcmfcoe_hbadisc.c
	@cp lpfc_init.c brcmfcoe_init.c
	@cp lpfc_mbox.c brcmfcoe_mbox.c
	@cp lpfc_mem.c brcmfcoe_mem.c
	@cp lpfc_nportdisc.c brcmfcoe_nportdisc.c
	@cp lpfc_scsi.c brcmfcoe_scsi.c
	@cp lpfc_sli.c brcmfcoe_sli.c
	@cp lpfc_vport.c brcmfcoe_vport.c
	@cp lpfc_nvme.c brcmfcoe_nvme.c
	@cp lpfc_nvmet.c brcmfcoe_nvmet.c
	$(MAKE) -C $(BASEINCLUDE) M=$(DRIVERDIR) CONFIG_SCSI_BRCMFCOE=m BUILD_FLAGS=-DBUILD_BRCMFCOE modules
	@rm -f brcmfcoe_attr.c
	@rm -f brcmfcoe_bsg.c
	@rm -f brcmfcoe_ct.c
	@rm -f brcmfcoe_debugfs.c
	@rm -f brcmfcoe_els.c
	@rm -f brcmfcoe_hbadisc.c
	@rm -f brcmfcoe_init.c
	@rm -f brcmfcoe_mbox.c
	@rm -f brcmfcoe_mem.c
	@rm -f brcmfcoe_nportdisc.c
	@rm -f brcmfcoe_scsi.c
	@rm -f brcmfcoe_sli.c
	@rm -f brcmfcoe_vport.c
	@rm -f brcmfcoe_nvme.c
	@rm -f brcmfcoe_nvmet.c


install:
	@rm -f /lib/modules/$(KERNELVERSION)/kernel/drivers/scsi/lpfc.ko
	install -d /lib/modules/$(KERNELVERSION)/kernel/drivers/scsi/lpfc
	install -c lpfc.ko /lib/modules/$(KERNELVERSION)/kernel/drivers/scsi/lpfc
	depmod -a

brcmfcoeinstall:
	@rm -f /lib/modules/$(KERNELVERSION)/kernel/drivers/scsi/brcmfcoe.ko
	install -d /lib/modules/$(KERNELVERSION)/kernel/drivers/scsi/brcmfcoe
	install -c brcmfcoe.ko /lib/modules/$(KERNELVERSION)/kernel/drivers/scsi/brcmfcoe
	depmod -a

clean:
	$(MAKE) -C $(BASEINCLUDE) M=$(DRIVERDIR) CONFIG_SCSI_LPFC=m clean
