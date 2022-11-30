#!/usr/bin/env sh
#

error() {
	echo "error: $*" > /dev/stderr
}

usage() {
	cat << EOF
update-kabi.sh: <kABI Module.symvers> <new Module.symvers>

Take kABI Module.symvers as reference and generate a new kABI symvers file.
EOF
}

KABI=$1
SYMVER=$2

if ! [ -s "$KABI" ] || ! [ -s "$SYMVER" ]; then
	error "Invalid params."
	usage
	exit 1
fi

cat "$KABI" | while read -r _crc _symbol _vmlinux _gpl; do
	grep "\b$_symbol\b.*vmlinux" "$SYMVER"
done | sed -e "s/\t$//"
