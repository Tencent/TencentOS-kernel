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

# $1: relative dir path
# $2: optional git ref, if not set current worktree is used
# $3: optional git repo
ls_repo_dir() {
	local _path=$1
	local _gitref=$2
	local _repo=${3:-$TOPDIR}
	local _gitshow

	# If git reference is set and git repo is valid, try use the versioned Makefile
	if [[ "$_gitref" ]]; then
		if _gitshow=$(git -C "$_repo" show "$_gitref:$_path" 2>/dev/null); then
			echo "$_gitshow" | tail -n +3
			return 0
		fi
		warn "Failed to ls '$_path' from git reference '$_gitref', using current worktree as build source."
	fi

	ls -1ap "$_repo/$_path/"
}

# $1: relative file path
# $2: optional git ref, if not set current worktree is used
# $3: optional git repo
cat_repo_file() {
	local _path=$1
	local _gitref=$2
	local _repo=${3:-$TOPDIR}

	# If git reference is set and git repo is valid, try use the versioned Makefile
	if [[ "$_gitref" ]]; then
		if git -C "$_repo" show "$_gitref:$_path" 2>/dev/null; then
			return 0
		fi
		warn "Failed to retrive '$_path' from git reference '$_gitref', using current worktree as build source."
	fi

	cat "$_repo"/"$1"
}

# $1: keyword
# $2: optional git ref, if not set current Makefile is used
# $3: optional git repo
get_dist_makefile_var() {
	local _sedexp="/^$1\s*[:?]?=\s*(.*)/{s/^\s*^$1\s*[:?]?=\s*//;h};\${x;p}"
	local _gitref=$2
	local _repo=${3:-$TOPDIR}
	local _val

	_val=$(cat_repo_file "dist/Makefile" "$_gitref" "$_repo" | sed -nE -e "$_sedexp")
	case $_val in
		*\$* )
			die "Can't parse Makefile variable '$1', it references to other variables."
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
# If KDIST is not set (or it's set to "-" wnich is illegal value for KDIST), read from dist Makefile.
[ "${KDIST--}" == - ] && KDIST=$(get_dist_makefile_var KDIST)

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
		loongarch64 )
			echo "loongarch64"
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
		loongarch64 )
			echo "loongarch"
			;;
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
