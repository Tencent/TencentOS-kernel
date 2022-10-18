#!/bin/bash --norc
# SPDX-License-Identifier: GPL-2.0
#
# Print out the tkernel version based on git commit and work tree.

# shellcheck source=./lib-version.sh
. "$(dirname "$(realpath "$0")")/lib-version.sh"

prepare_kernel_ver "$@"

echo "$KERNEL_UNAMER"
