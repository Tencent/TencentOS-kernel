#!/bin/bash --norc
#
# List available kernel config styles
#
# Params:
# $@: [ <filter>... ]		When non-empty, only show kernel configs style matching <filter>

# shellcheck source=./lib-config.sh
. "$(dirname "$(realpath "$0")")/lib-config.sh"

_echo() {
	echo "$1" 2>/dev/null || exit 0
}

for_each_config_target _echo "$@"
