#!/bin/bash --norc
#
# List files to be generated for kernel config styles
#
# Params:
# $@: [ <filter>... ]		When non-empty, only show kernel configs style matching <filter>

# shellcheck source=./lib-config.sh
. "$(dirname "$(realpath "$0")")/lib-config.sh"

_echo_file() {
	for arch in "${CONFIG_ARCH[@]}"; do
		echo "$CONFIG_OUTDIR/$1.$arch.config"
	done
}

for_each_config_target _echo_file "$@"
