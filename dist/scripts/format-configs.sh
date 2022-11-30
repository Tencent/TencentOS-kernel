#!/bin/bash --norc
# SPDX-License-Identifier: GPL-2.0
#
# Generate kernel config according to config matrix

# shellcheck source=./lib-config.sh
. "/$(dirname "$(realpath "$0")")/lib-config.sh"

# Sort and remove duplicated items in each config base file
for file in "$CONFIG_PATH"/*/*/*.config; do
	config_sanitizer < "$file" > "$file.fmt.tmp"

	mv "$file.fmt.tmp" "$file"
done
