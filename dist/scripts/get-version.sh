#!/bin/bash --norc
# SPDX-License-Identifier: GPL-2.0
#
# Print out the tkernel version based on git commit and work tree.
#
# $1: git reference, tag or commit
# $2: version type, 'nvr' or 'unamer', defaults to 'unamer'
#
# shellcheck source=./lib-version.sh
. "$(dirname "$(realpath "$0")")/lib-version.sh"

prepare_kernel_ver "$1"
case $2 in
	vr )
		echo "$KERNEL_MAJVER"-"$KERNEL_RELVER"
		;;
	nvr )
		echo "$KERNEL_NAME"-"$KERNEL_MAJVER"-"$KERNEL_RELVER"
		;;
	* | unamer )
		echo "$KERNEL_UNAMER"
		;;
esac
