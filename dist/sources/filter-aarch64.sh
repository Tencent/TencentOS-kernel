#!/bin/bash

# Copied from Fedora

# This is the aarch64 override file for the core/drivers package split.  The
# module directories listed here and in the generic list in filter-modules.sh
# will be moved to the resulting kernel-modules package for this arch.
# Anything not listed in those files will be in the kernel-core package.
#
# Please review the default list in filter-modules.sh before making
# modifications to the overrides below.  If something should be removed across
# all arches, remove it in the default instead of per-arch.

driverdirs="atm auxdisplay bcma bluetooth firewire fpga infiniband leds media memstick message mmc mtd nfc ntb pcmcia power ssb soundwire staging tty uio w1"

ethdrvs="3com adaptec arc alteon atheros broadcom cadence calxeda chelsio cisco dec dlink emulex marvell micrel myricom neterion nvidia packetengines qlogic rdc sfc silan sis smsc stmicro sun tehuti ti via wiznet xircom"

drmdrvs="amd arm bridge ast exynos hisilicon i2c imx mgag200 meson msm nouveau panel pl111 radeon rockchip tegra sun4i tiny vc4"

singlemods="ntb_netdev iscsi_ibft iscsi_boot_sysfs megaraid pmcraid qedi qla1280 9pnet_rdma rpcrdma nvmet-rdma nvme-rdma hid-picolcd hid-prodikeys hwpoison-inject target_core_user sbp_target cxgbit chcr rnbd-client rnbd-server mlx5_ib mlx5_vdpa dfl-emif octeontx2-cpt octeontx2-cptvf spi-altera-dfl rvu_cptpf rvu_cptvf regmap-sdw regmap-sdw-mbq hid-playstation hid-nintendo configs"
