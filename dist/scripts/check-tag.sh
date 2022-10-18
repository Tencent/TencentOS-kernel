#!/bin/bash --norc
# SPDX-License-Identifier: GPL-2.0
#
# $1: tag
# $2: (opt) git repo

# shellcheck source=./lib-version.sh
. "$(dirname "$(realpath "$0")")/lib-version.sh"

prepare_kernel_ver "$@"

# If tag is not recognized, prepare_kernel_ver will version it as snapshot
# use this as an indicator of invalid tag
if ! [[ $KTAGRELEASE ]]; then
	error "Invalid tag '$1'"
	exit 1
else
	info "Tag '$KTAGRELEASE' OK, Kernel version '$KERNEL_UNAMER', RPM version '${KERNEL_MAJVER//-/.}-${KERNEL_RELVER//-/.}'"
fi
