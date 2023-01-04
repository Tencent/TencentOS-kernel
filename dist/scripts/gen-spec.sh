#!/bin/bash --norc
# SPDX-License-Identifier: GPL-2.0

# shellcheck source=./lib-version.sh
. "/$(dirname "$(realpath "$0")")/lib-version.sh"

usage()
{
	cat << EOF
gen-spec.sh [OPTION]

	--kernel-dist		Kernel distribution marker, eg. tks/tlinux4/tlinux3
	--kernel-config		Kernel config base name, will look for the corresponding kernel config under $DISTDIR/configs
	--kernel-variant	Kernel variant, eg. debug, gcov, kdb
	--build-arch		What arch's are supported, defaults to '$SPEC_ARCH'"
EOF
}

# gen-spec need to parse info from source (currently only parse LOCALVERSION from config),
# so check if the kernel configs are valid here.
prepare_source_info() {
	local localversion arch_localversion file

	if [ -z "$KERNEL_CONFIG" ]; then
		die "Config target not specified."
	fi

	for arch in $BUILD_ARCH; do
		file="$SOURCEDIR/$KERNEL_CONFIG.$arch.config"
		if ! [ -e "$file" ]; then
			die "Config file missing '$file'"
		fi
		if [ -z "$localversion" ]; then
			localversion=$(sed -ne 's/^CONFIG_LOCALVERSION=\(.*\)$/\1/pg' "$file")
		else
			arch_localversion=$(sed -ne 's/^CONFIG_LOCALVERSION=\(.*\)$/\1/pg' "$file")
			if [ "$arch_localversion" != "$localversion" ]; then
				die "LOCALVERSION inconsistent between sub-arches for config target '$file', this breaks SRPM package naming."
			fi
		fi
	done

	localversion=${localversion#\"}
	localversion=${localversion%\"}
	LOCALVERSION=$localversion
}

DEFAULT_DISALBED=" kabichk "
while [[ $# -gt 0 ]]; do
	case $1 in
		--kernel-config )
			KERNEL_CONFIG=$2
			shift 2
			;;
		--build-arch )
			BUILD_ARCH=$2
			shift 2
			;;
		--gitref )
			GITREF=$2
			shift 2
			;;
		--set-default-disabled )
			for param in $2; do
				DEFAULT_DISALBED=" $param $DEFAULT_DISALBED"
			done
			shift 2
			;;
		--set-default-enabled )
			for param in $2; do
				DEFAULT_DISALBED="${DEFAULT_DISALBED/ $param /}"
			done
			shift 2
			;;
		* )
			die "Unrecognized parameter $1"
			;;
	esac
done

BUILD_ARCH="${BUILD_ARCH:-$SPEC_ARCH}"

# This helper prepares LOCALVERSION
prepare_source_info
# This function will prepare $KERNEL_MAJVER, $KERNEL_RELVER
prepare_kernel_ver "${GITREF:-HEAD}" "$LOCALVERSION"

_gen_arch_spec() {
	local arch kernel_arch
	cat << EOF
ExclusiveArch: $BUILD_ARCH
EOF
	for arch in $BUILD_ARCH; do
		if ! kernel_arch=$(get_kernel_arch "$arch"); then
			die "Unsupported arch '$arch'"
		fi
		cat << EOF
%ifarch $arch
%define kernel_arch $kernel_arch
%endif
EOF
	done
}

_gen_kerver_spec() {
	cat << EOF
%define kernel_majver $KERNEL_MAJVER
%define kernel_relver $KERNEL_RELVER
%define kernel_unamer $KERNEL_UNAMER
%define rpm_name $KERNEL_NAME
%define rpm_vendor $(get_dist_makefile_var VENDOR_CAPITALIZED)
%define rpm_url $(get_dist_makefile_var URL)
EOF
}

_gen_arch_source() {
	# Source1000 - Source1199 for kernel config
	local config_source_num=1000 arch
	for arch in $BUILD_ARCH; do
		echo "Source$config_source_num: $KERNEL_CONFIG.$arch.config"
		config_source_num=$((config_source_num + 1))
	done

	# Source1200 - Source1399 for kabi source
	local kabi_source_num=1200 arch
	for arch in $BUILD_ARCH; do
		echo "Source$kabi_source_num: Module.kabi_$arch"
		kabi_source_num=$((kabi_source_num + 1))
	done
}

_gen_config_build() {
	cat << EOF
%ifnarch $BUILD_ARCH
{error:unsupported arch}
%endif
EOF
	local config_source_num=1000 arch
	for arch in $BUILD_ARCH; do
		cat << EOF
%ifarch $arch
BuildConfig %{SOURCE$config_source_num}
%endif
EOF
		config_source_num=$((config_source_num + 1))
	done
}

_gen_kabi_check() {
	cat << EOF
%ifnarch $BUILD_ARCH
{error:unsupported arch}
%endif
EOF
	local kabi_source_num=1200 arch
	for arch in $BUILD_ARCH; do
		cat << EOF
%ifarch $arch
CheckKernelABI %{SOURCE$kabi_source_num}
%endif
EOF
		kabi_source_num=$((kabi_source_num + 1))
	done
}

_gen_changelog_spec() {
	cat "$DISTDIR/templates/changelog"
}

_gen_pkgopt_spec() {
	local enabled_opts disabled_opts opts_output
	for opt in \
		core \
		doc \
		headers \
		perf \
		tools \
		bpftool \
		debuginfo \
		modsign \
		kabichk
	do
		case $DEFAULT_DISALBED in
			*" $opt "* )
				disabled_opts="$disabled_opts $opt"
				opts_output="$opts_output
%define with_$opt	%{?_with_$opt: 1}	%{?!_with_$opt: 0}"
				;;
			* )
				enabled_opts="$enabled_opts $opt"
				opts_output="$opts_output
%define with_$opt	%{?_without_$opt: 0}	%{?!_without_$opt: 1}"
		esac
	done

	# Marker for dist build system, a little bit ugly, maybe find a better way to present this info later
	echo ""
	echo "# === Package options ==="
	echo "# Eanbled by default: $enabled_opts"
	echo "# Disabled by default: $disabled_opts"
	echo "$opts_output"
}

gen_spec() {
	local _line
	local _spec

	while IFS='' read -r _line; do
		case $_line in
			"{{PKGPARAMSPEC}}"* )
				_spec+="$(_gen_pkgopt_spec)"
				;;
			"{{ARCHSPEC}}"* )
				_spec+="$(_gen_arch_spec)"
				;;
			"{{VERSIONSPEC}}"* )
				_spec+="$(_gen_kerver_spec)"
				;;
			"{{ARCHSOURCESPEC}}"* )
				_spec+="$(_gen_arch_source)"
				;;
			"{{CONFBUILDSPEC}}"* )
				_spec+="$(_gen_config_build)"
				;;
			"{{KABICHECKSPEC}}"* )
				_spec+="$(_gen_kabi_check)"
				;;
			"{{CHANGELOGSPEC}}"* )
				_spec+="$(_gen_changelog_spec)"
				;;
			"{{"*"}}"*)
				die "Unrecognized template keywork $_line"
				;;
			* )
				_spec+="$_line"
				;;
		esac
		_spec+="
"
	done

	echo "$_spec"
}

gen_spec < "$DISTDIR/templates/kernel.template.spec"
