#!/bin/bash
#
# Copyright (c) 2020 Mellanox Technologies. All rights reserved.
#
# This Software is licensed under one of the following licenses:
#
# 1) under the terms of the "Common Public License 1.0" a copy of which is
#    available from the Open Source Initiative, see
#    http://www.opensource.org/licenses/cpl.php.
#
# 2) under the terms of the "The BSD License" a copy of which is
#    available from the Open Source Initiative, see
#    http://www.opensource.org/licenses/bsd-license.php.
#
# 3) under the terms of the "GNU General Public License (GPL) Version 2" a
#    copy of which is available from the Open Source Initiative, see
#    http://www.opensource.org/licenses/gpl-license.php.
#
# Licensee has the right to choose one of the above licenses.
#
# Redistributions of source code must retain the above copyright
# notice and one of the license notices.
#
# Redistributions in binary form must reproduce both the above copyright
# notice, one of the license notices in the documentation
# and/or other materials provided with the distribution.
#
# Author: Alaa Hleihel <alaa@mellanox.com>
#

set -ex

############################################################################
# Settings:

# Additional flags to pass to mlnxofedinstall
mlnxofedinstall_flags=""

#
# Disabling build of specific modules:
#
mlnxofedinstall_flags=" ${mlnxofedinstall_flags} \
	--without-mlx5_fpga_tools \
	--without-mlnx-rdma-rxe \
	--without-mlnx-nfsrdma \
	--without-mlnx-nvme \
	--without-isert \
	--without-iser \
	--without-srp \
	--without-rshim \
	--without-mdev \
	"

#
# Disabling checking for required packages:
#
# Uncomment the next line in case you have a customised build host that
# causes mlnxofedinstall to fail when checking for required packages (as
# they might be provided by packages with unexpected names, etc).
#
mlnxofedinstall_flags=" ${mlnxofedinstall_flags} --without-depcheck"



#
############################################################################
# get args

# kernel version number (for building modules)
KERNELRELEASE=$1; shift

# path to kernel rpm build directory (for temp build folder)
kbdir=$1; shift

# path to kernel rpm build root (for kernel sources and installing modules)
buildroot=$1; shift

# Path to full mlnx_ofed package
ofedsrc=${kbdir}/package/arm/mlx/MLNX_OFED_LINUX-5.1-2.5.8.0.46-rhel8.3-aarch64.tgz

mlx_splits_dir=${kbdir}/package/arm/mlx
mlx_splits="mlx_split*"
if [ ! -e "${ofedsrc}" ]; then
       cat $mlx_splits_dir/$mlx_splits > $ofedsrc
fi

if [ ! -e "${ofedsrc}" ]; then
	echo "ERROR: ofedsrc not provided or path '${ofedsrc}' does not exist." >&2
	echo 1
fi

if [ "X${KERNELRELEASE}" == "X" ]; then
	echo "ERROR: KERNELRELEASE not provided." >&2
	echo 1
fi
if [ ! -d "${kbdir}" ]; then
	echo "ERROR: kbdir not provided" >&2
	echo 1
fi
if [ ! -d "${buildroot}" ]; then
	echo "ERROR: buildroot not provided" >&2
	echo 1
fi

K_SRC=${buildroot}/usr/src/kernels/${KERNELRELEASE}
K_DIR_INSTALL=${buildroot}/lib/modules/${KERNELRELEASE}/kernel

export MAKE=${MAKE:-"make"}


############################################################################
#
# make sure we have each modules installed once under the new kernel:
# - if the module we want to install already exists, replace it wit the new one.
# - if it's a new module, then just install it.
#
function inst_mod()
{
	local mod=$1; shift

	local mod_name=$(basename ${mod})
	local tdir=

	for ii in $(find ${K_DIR_INSTALL} -name "*${mod_name}*")
	do
		# save module location
		tdir=$(dirname ${ii})

		/bin/rm -fv ${ii}
	done

	# install new module
	if [ ! -e "${tdir}" ]; then
		tdir="${K_DIR_INSTALL}/drivers/ofed_addon/"
		mkdir -p ${tdir}
	fi

	/bin/cp -fv ${mod} ${tdir}/
}

function handle_rpm()
{
	local pkg=$1; shift

	/bin/rm -rf ext_rpm
	mkdir -p ext_rpm
	cd ext_rpm

	# extract rpm
	rpm2cpio ${pkg} | cpio -id

	# copy all modules
	for mod in $(find . -name "*.ko*")
	do
		inst_mod ${mod}
	done

	cd -
}

function handle_rpms_ofa_kernel()
{
	local mod_rpms=$1; shift

	for mrpm in ${mod_rpms}
	do
		case "${mrpm}" in
			*mlnx-ofa_kernel*devel*)
				continue
				;;
			*mlnx-ofa_kernel*)
				handle_rpm ${mrpm}
				;;
		esac
	done
}

function handle_rpms_all()
{
	local mod_rpms=$1; shift

	for mrpm in ${mod_rpms}
	do
		case "${mrpm}" in
			*mlnx-ofa_kernel*)
				continue
				;;
			*)
				handle_rpm ${mrpm}
				;;
		esac
	done
}

__cleanup()
{
	sed -i '/__os_install_post/d' ~/.rpmmacros
	sed -i '/__brp_mangle_shebangs/d' ~/.rpmmacros
}
trap '__cleanup' INT TERM ERR EXIT

############################################################################

cd ${kbdir}
/bin/rm -rf ofedbuild && mkdir ofedbuild && cd ofedbuild
mkdir mtmp
TMPD="$PWD/mtmp"

# extract MLNX_OFED
tar xzf ${ofedsrc}
cd MLNX_OFED_LINUX-*

distro=$(cat distro 2>/dev/null)
echo "This MLNX_OFED was built for distro: ${distro}"

#this will cause error in chroot environment
#echo "%__os_install_post %{nil}" >> ~/.rpmmacros
echo "%__brp_mangle_shebangs %{nil}" >> ~/.rpmmacros

# compile modules against target kernel
./mlnxofedinstall \
	--add-kernel-support-build-only \
	--disable-kmp \
	--distro ${distro} \
	--kernel ${KERNELRELEASE} \
	--kernel-sources ${K_SRC} \
	--tmpdir ${TMPD} \
	--skip-distro-check \
	--skip-repo ${mlnxofedinstall_flags}

sed -i '/__os_install_post/d' ~/.rpmmacros
sed -i '/__brp_mangle_shebangs/d' ~/.rpmmacros

# fix permissiosn
chown -R ${USER} ${TMPD}

echo
echo "Compilation done, now get the modules and copy the to kernel buildroot..."
echo

# open RPMs and copy modules
/bin/rm -rf ${TMPD}/new
mkdir -p ${TMPD}/new
cd ${TMPD}/new
tar xzf ${TMPD}/MLNX_OFED_LINUX-*/MLNX_OFED_LINUX-*tgz

# get all kernel module rpms that were built against target kernel
mod_rpms=
for pkg in $(find ${TMPD}/new/MLNX_OFED_LINUX-*/RPMS -name "*rpm")
do
	if rpm -qlp ${pkg} | grep "\.ko" | grep -q "${KERNELRELEASE}"; then
		# take each rpm only once
		rpkg=$(readlink -f ${pkg})
		if ! echo "${mod_rpms}" | grep -q "${rpkg}"; then
			mod_rpms="${mod_rpms} ${rpkg}"
		fi
	fi
done

# first handle ofa_kernel rpm since it's our base package
handle_rpms_ofa_kernel "${mod_rpms}"

# now handle the additional packages
handle_rpms_all "${mod_rpms}"

# cleanup
cd ${kbdir}
/bin/rm -rf ofedbuild
