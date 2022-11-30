#!/bin/bash --norc
# SPDX-License-Identifier: GPL-2.0

# shellcheck source=./lib-version.sh
. "$(dirname "$(realpath "$0")")/lib-version.sh"

prepare_next_kernel_ver "$@"

echo "$KERNEL_UNAMER"
