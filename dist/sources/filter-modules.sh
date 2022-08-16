#!/bin/bash
#
# Used Fedora kernel-ark repo as reference.
#
# Called as filter-modules.sh <depmod base path> <kernel version> <arch> <System.map>
#
# This script collects modules, and filters modules into the kernel-core and kernel-modules
# subpackages.  We list out subsystems/subdirs to prune from the installed
# module directory.  What is left is put into the kernel-core package.  What is
# pruned is contained in the kernel-modules package.
#
# This file contains the default subsys/subdirs to prune from all architectures.
# If an architecture needs to differ, we source a per-arch filter-<arch>.sh file
# that contains the set of override lists to be used instead.  If a module or
# subsys should be in kernel-modules on all arches, please change the defaults
# listed here.

# Set the default dirs and modules to filter out as external modules

driverdirs="atm auxdisplay bcma bluetooth firewire fmc fpga iio infiniband isdn leds media memstick message mfd mmc mtd nfc ntb pcmcia platform power powercap ssb soundwire staging thermal tty uio w1"

chardrvs="mwave pcmcia"

netdrvs="appletalk can dsa hamradio ieee802154 irda ppp slip usb wireless"

ethdrvs="3com adaptec alteon amd aquantia arc atheros broadcom cadence calxeda chelsio cisco dec dlink emulex icplus marvell mellanox micrel myricom neterion nvidia oki-semi packetengines qlogic rdc renesas sfc silan sis smsc stmicro sun tehuti ti wiznet xircom"

inputdrvs="gameport tablet touchscreen joystick"

hiddrvs="surface-hid"

scsidrvs="aacraid aic7xxx aic94xx be2iscsi bfa bnx2i bnx2fc csiostor cxgbi esas2r fcoe fnic hisi_sas isci libsas lpfc megaraid mpt2sas mpt3sas mvsas pm8001 qla2xxx qla4xxx sym53c8xx_2 ufs qedf"

usbdrvs="atm image misc serial wusbcore"

drmdrvs="amd ast bridge gma500 i2c i915 mgag200 nouveau panel radeon via"

netprots="6lowpan appletalk atm ax25 batman-adv bluetooth can dccp dsa ieee802154 irda l2tp mac80211 mac802154 mpls netrom nfc rds rfkill rose sctp smc wireless"

fsdrvs="affs befs cifs coda cramfs dlm ecryptfs hfs hfsplus jfs jffs2 minix ncpfs nilfs2 ocfs2 reiserfs romfs squashfs sysv ubifs ufs gfs2"

# .ko files to be filtered
singlemods="ntb_netdev iscsi_ibft iscsi_boot_sysfs megaraid pmcraid qedi qla1280 9pnet_rdma rpcrdma nvmet-rdma nvme-rdma hid-picolcd hid-prodikeys hwa-hc hwpoison-inject hid-sensor-hub target_core_user sbp_target cxgbit iw_cxgb3 iw_cxgb4 cxgb3i cxgb3i cxgb3i_ddp cxgb4i chcr chtls parport_serial ism regmap-sdw regmap-sdw-mbq arizona-micsupp hid-asus iTCO_wdt rnbd-client rnbd-server mlx5_ib mlx5_vdpa spi-altera-dfl nct6775 hid-playstation hid-nintendo ntc_thermistor configs"

# Overrides is individual modules which need to remain in kernel-core due to deps.
overrides="cec"

BASE_DIR=$1
KERNEL_UNAMER=$2
ARCH=$3
SYSTEM_MAP=$(realpath "$4")
MODULE_DIR=lib/modules/$KERNEL_UNAMER

# Grab the arch-specific filter list overrides
source "$(dirname "$(realpath "$0")")/filter-$ARCH.sh"

error() {
	echo "$@">&2
}

if ! cd "$BASE_DIR"; then
	error "Invalid base path: '$BASE_DIR'"
	exit 1
fi

if ! cd "$MODULE_DIR"; then
	error "Invalid kernel module path: '$MODULE_DIR'"
	exit 1
fi

# To be read from build path
core_modules_list=
modules_list=

# Read all kernel modules in the core modules list at the beginning
# filter them into external modules list step by step
#
# Not filtering internal or vdso so start with kernel/
core_modules_list=$(find kernel -name '*.ko')

filter_mods() {
	local prefix=$1 mods=$2 suffix=$3
	local mod filter_list

	for mod in $mods; do
		if ! filter_list=$(grep "$prefix$mod$suffix" <<< "$core_modules_list"); then
			error "$prefix$mod$suffix is marked as non-core module but not built, skipping."
		else
			core_modules_list=$(grep -v "$prefix$mod$suffix" <<< "$core_modules_list")
			modules_list+=$filter_list
			modules_list+=$'\n'
		fi
	done
}

filter_override() {
	local filter_list

	for mod in $1; do
		if filter_list=$(grep "$mod.ko" <<< "$modules_list"); then
			modules_list=$(grep -v "$mod.ko" <<< "$modules_list")
			core_modules_list+=$filter_list
			core_modules_list+=$'\n'
		fi
	done
}

filter_mods "drivers/" "$driverdirs" /
filter_mods "drivers/char/" "$chardrvs" /
filter_mods "drivers/net/" "$netdrvs" /
filter_mods "drivers/net/ethernet/" "$ethdrvs" /
filter_mods "drivers/input/" "$inputdrvs" /
filter_mods "drivers/hid/" "$hiddrvs" /
filter_mods "drivers/scsi/" "$scsidrvs" /
filter_mods "drivers/usb"/ "$usbdrvs" /
filter_mods "drivers/gpu/drm/" "$drmdrvs" /
filter_mods "net/" "$netprots" /
filter_mods "fs/" "$fsdrvs" /

# Just kill sound.
filter_mods "" "sound" /
filter_mods "drivers" "soundwire" /

# Filter single modules
filter_mods "" "$singlemods"

# Now process the override list to bring those modules back into core
filter_override "$overrides"

# Mask external mods to do a depmod check
for mod in $modules_list; do
	mv "$mod" "$mod.bak"
done

# Run depmod on the resulting module tree and make sure it isn't broken
depmod_err=$(depmod "$KERNEL_UNAMER" -b "$BASE_DIR" -naeF "$SYSTEM_MAP" 2>&1 1>/dev/null)
if [ "$depmod_err" ]; then
	error "Failed to filter out external modules, broken depmod:"
	error "$depmod_err"
	exit 1
fi

# Mask external mods to do a depmod check
for mod in $modules_list; do
	mv "$mod.bak" "$mod"
done

# Print the modules_list after sort, and prepend /lib/modules/<KERNEL_UNAMER>/ to each line
echo "$modules_list" | sort -n | sed "/^$/d;s/^/\/${MODULE_DIR//\//\\\/}\//"

exit 0
