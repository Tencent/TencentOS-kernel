#!/bin/bash --norc
# SPDX-License-Identifier: GPL-2.0
#
# Generate kernel config according to config matrix
#
# Params:
# $@: [ <filter>... ]		When non-empty, only generate kernel configs style matching <filter>

# shellcheck source=./lib-config.sh
. "$(dirname "$(realpath "$0")")/lib-config.sh"

# Populate basic config entries based on our config file tree
populate_configs "$@"

# Process the config files with make olddefconfig
makedef_configs "$@"

# Check config values (eg. LOCALVERSION)
sanity_check_configs "$@"
