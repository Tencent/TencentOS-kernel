#!/bin/bash
#
# Shell helpers
#

RED="\033[0;31m"
GREEN="\033[0;32m"
YELLOW="\033[1;33m"
NC="\033[0m"

echo_green() {
	echo -en "$GREEN"
	echo "$@"
	echo -en "$NC"
}

echo_yellow() {
	echo -en "$YELLOW"
	echo "$@"
	echo -en "$NC"
}

echo_red() {
	echo -en "$RED"
	echo "$@"
	echo -en "$NC"
}

info() {
	echo_green -n "info: " >&2
	echo "$@"
}

warn() {
	echo_yellow -n "warn: " >&2
	echo "$@" >&2
}

error() {
	echo_red -n "error: " >&2
	echo "$@" >&2
}

die() {
	echo_red -n "fatal: " >&2
	echo "$@" >&2
	exit 1
}

get_dist_makefile_var() {
	local _lib_source=${BASH_SOURCE[0]}
	local _val

	# Just one sed call, fast and simple
	# Match anyline start with "^$1\s*=\s*", strip and remove matching part then store in hold buffer.
	# Pprint the hold buffer on exit. This ensure the last assigned value is used, matches Makefile syntax well
	_val=$(sed -nE -e \
		"/^$1\s*:?=\s*(.*)/{s/^\s*^$1\s*:?=\s*//;h};\${x;p}" \
		"$(dirname "$(realpath "$_lib_source")")/../Makefile")

	case $_val in
		*\$* )
			die "Can't parse Makefile variable '$1', which references to other variables."
			;;
	esac

	echo "$_val"
}

[ "$TOPDIR" ] || TOPDIR=$(git rev-parse --show-toplevel 2>/dev/null)
[ "$TOPDIR" ] || TOPDIR="$(realpath "$(dirname "$(realpath "$0")")/../..")"
[ "$VENDOR" ] || VENDOR=$(get_dist_makefile_var VENDOR)
[ "$DISTPATH" ] || DISTPATH=$(get_dist_makefile_var DISTPATH)
[ "$DISTDIR" ] || DISTDIR=$TOPDIR/$DISTPATH
[ "$SOURCEDIR" ] || SOURCEDIR=$DISTDIR/rpm/SOURCES
[ "$SPEC_ARCH" ] || SPEC_ARCH=$(get_dist_makefile_var SPEC_ARCH)

if ! [ -s "$TOPDIR/Makefile" ]; then
	echo "Dist tools can only be run within a valid Linux Kernel git workspace." >&2
	exit 1
fi

if [ -z "$VENDOR" ] || ! [ -s "$DISTDIR/Makefile" ]; then
	echo "Dist tools can't work without properly configured dist file." >&2
	exit 1
fi

# Just use uname to get native arch
get_native_arch () {
	uname -m
}

# Convert any arch name into linux kernel arch name
#
# There is an inconsistence between Linux kernel's arch naming and
# RPM arch naming. eg. Linux kernel uses arm64 instead of aarch64
# used by RPM, i686 vs x86, amd64 vs x86_64. Most are just same things
# with different name due to historical reasons.
get_kernel_arch () {
	case $1 in
		riscv64 )
			echo "riscv"
			;;
		arm64 | aarch64 )
			echo "arm64"
			;;
		i386 | i686 | x86 )
			echo "x86"
			;;
		amd64 | x86_64 )
			echo "x86_64"
			;;
	esac
}

# Convert any arch name into linux kernel src arch name
#
# Similiar to get_kernel_arch but return the corresponding
# source code base sub path in arch/
get_kernel_src_arch () {
	case $1 in
		riscv64 )
			echo "riscv"
			;;
		arm64 | aarch64 )
			echo "arm64"
			;;
		i386 | i686 | x86 | amd64 | x86_64 )
			echo "x86"
			;;
	esac
}

type git >/dev/null || die 'git is required'
type make >/dev/null || die 'make is required'
