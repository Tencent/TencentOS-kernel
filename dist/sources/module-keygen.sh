#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Default keygen
#
# We do nothing here, just check if CONFIG_MODULE_SIG_KEY is set to
# "certs/signing_key.pem", this is a special value for kernel config,
# Kbuild will automatically generate keys when this value is set.

KERNEL_UNAMER=$1
BUILD_DIR=$2

KCONFIG_FILE=$BUILD_DIR/.config

error() {
	echo "module-keygen: error: $*" >&2
	exit 1
}

if ! [[ -f $KCONFIG_FILE ]]; then
	error "can't find a valid kernel config: $KCONFIG_FILE"
fi

if ! grep -q '^CONFIG_MODULES=y' "$KCONFIG_FILE"; then
	echo "CONFIG_MODULES not enabled, quit. "
	exit 0
fi

if ! grep -q '^CONFIG_MODULE_SIG=y' "$KCONFIG_FILE"; then
	echo "CONFIG_MODULE_SIG not enabled, quit. "
	exit 0
fi

if ! grep -q '^CONFIG_MODULE_SIG_KEY="certs/signing_key.pem"' "$KCONFIG_FILE"; then
	error "CONFIG_MODULE_SIG_KEY=\"certs/signing_key.pem\" is not defined, can't gen keys."
fi

# Don't use Kbuild's signing, use %%{_module_signer} instead, be compatible with debuginfo and compression
echo "module-keygen: Disabling CONFIG_MODULE_SIG_FORCE, siginig within temporary builtin key."
sed -i -e "s/^CONFIG_MODULE_SIG_FORCE.*/# CONFIG_MODULE_SIG_FORCE is not set/" "$KCONFIG_FILE"

echo "module-keygen: $KERNEL_UNAMER is using builtin keys."
