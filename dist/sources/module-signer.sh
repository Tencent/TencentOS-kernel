#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Default key-sign
#
# Just a wrapper for sign-file
#
# We depend on CONFIG_MODULE_SIG_KEY="certs/signing_key.pem"
# for the built-in keys.

KERNEL_UNAMER=$1
BUILD_DIR=$2
BASE_DIR=$3

KCONFIG_FILE=$BUILD_DIR/.config
MODULE_DIR=$BASE_DIR/lib/modules/$KERNEL_UNAMER

error() {
	echo "module-signer: error: $*" >&2
}

if ! [[ -f $KCONFIG_FILE ]]; then
	error "can't find a valid kernel config."
	exit 1
fi

if ! grep -q '^CONFIG_MODULES=y' "$KCONFIG_FILE"; then
	echo "CONFIG_MODULES=y is not defined in .config, skipping signing."
	exit 0
fi

if ! grep -q '^CONFIG_MODULE_SIG=y' "$KCONFIG_FILE"; then
	echo "CONFIG_MODULE_SIG=y is not defined in .config, skipping signing."
	exit 0
fi

if ! [[ -x $BUILD_DIR/scripts/sign-file ]]; then
	error "$BUILD_DIR/scripts/sign-file is not an executable file."
	exit 1
fi

if ! grep -q '^CONFIG_MODULE_SIG_KEY="certs/signing_key.pem"' "$KCONFIG_FILE"; then
	error "CONFIG_MODULE_SIG_KEY is not defined in .config, can't gen keys."
	exit 1
fi

echo "module-signer: Signing $KERNEL_UNAMER modules with builtin keys..."
PRIKEY="$BUILD_DIR/certs/signing_key.pem"
PUBKEY="$BUILD_DIR/certs/signing_key.x509"

if ! [[ -s $PRIKEY ]]; then
	error "private key file doesn't exist: $PRIKEY"
	exit 1
fi

if ! [[ -s $PUBKEY ]]; then
	error "public key file doesn't exist: $PUBKEY"
	exit 1
fi

JOB=$(nproc)
JOB=${JOB:-2}

export BUILD_DIR
export PRIKEY
export PUBKEY
# shellcheck disable=2016
find "$MODULE_DIR" -type f -name '*.ko' -print0 | xargs -0r -n 1 -P "$JOB" sh -c '
	$BUILD_DIR/scripts/sign-file sha256 "$PRIKEY" "$PUBKEY" "$1"
	rm -f $1.sig $1.dig

	if [ "~Module signature appended~" != "$(tail -c 28 "$1")" ]; then
		echo "module-signer: error: failed to sign $1."
		exit 1
	fi
' _ || exit $?

echo "module-signer: Signing $KERNEL_UNAMER done."
