#!/bin/bash --norc
#
# To support different arches, compile options, and distributions,
# Kernel configs are managed with a hierarchy matrix.
#
# This script is the helper for parsing and updating the config matrix
#
# shellcheck source=./lib.sh
. "$(dirname "$(realpath "$0")")/lib.sh"

CONFIG_PATH=${CONFIG_PATH:-$DISTDIR/configs}
# shellcheck disable=SC2206
CONFIG_ARCH=( $SPEC_ARCH )
CONFIG_SPECS=( "$CONFIG_PATH"/[0-9][0-9]* )
CONFIG_OUTDIR=$SOURCEDIR
CONFIG_CACHE=$DISTDIR/workdir/config_cache


_get_config_cross_compiler () {
	if [[ "$1" == $(get_native_arch) ]]; then
		:
	elif [[ -d "$TOPDIR/scripts/dummy-tools/" ]]; then
		echo "scripts/dummy-tools/"
	else
		echo "$DISTDIR/scripts/dummy-tools/"
	fi
}

get_config_val() {
	# If it's y/m/n/0-9, return plain text, else get from .config
	#
	# To be more accurate, maybe we need to check right/left-value?
	case $1 in
		n )
			;;
		y|m|[0-9] )
			echo "$1" ;;
		* )
			local _val
			_val=$(grep "^CONFIG_$1=" "$2")
			_val=${_val#*=}
			echo "${_val:-not set}"
			;;
	esac
}

# Eval a Kconfig condition statement
kconfig_eval() {
	local _line=$1

	local _if
	# Convert Kconfig cond statement to bash syntax then eval it
	_if=$(echo "$_line" | sed -E \
		-e "s/(\|\||&&|=|!=|<|<=|>|>=|!|\(|\))/ \1 /g" \
		-e "s/([[:alnum:]_-]+)/ \"\$(get_kconf_val \'\1\')\" /g")
	eval "[[ $_if ]]"
}

get_depends() {
	local _conf=$1
	local _deps _line

	sed -nE "s/^[[:space:]]*depends on //p;" "$CONFIG_CACHE/$_conf"
}

# $1: Parsed Kconfig file
get_all_depends() {
	local _dep
	local _deps=( )

	[[ ! -e "$CONFIG_CACHE/$1" ]] && return 1

	# shellcheck disable=SC2207
	while read -r _dep; do
		if ! [[ "$_dep" ]]; then
			continue
		elif [[ "$_dep" =~ y|n|m ]]; then
			_deps+=( "$_dep" )
		elif [[ "$_dep" =~ [a-zA-Z0-9] ]]; then
			_deps+=( "$_dep[=$(get_config_val "$_dep" "$2")]" )
		else
			_deps+=( "$_dep" )
		fi
	done <<< "$(get_depends "$1" | sed -E \
		-e 's/^([^\(])/(\1/' \
		-e 's/([^\)])$/\1)/' \
		-e '2,$s/^/\&\&/' \
		-e 's/(\|\||&&|=|!=|<|<=|>|>=|!|\(|\)|([[:alnum:]_-]+))/\n\1\n/g')"
	# The regex above simply tokenize the Kconfig expression

	echo "${_deps[@]}"

	return 0
}

kconfig_parser() {
	local _arch=$1 _kconfig=$2 _config=$3

	[[ -s $_kconfig ]] || error "Invalid Kconfig '$_kconfig'"
	[[ -s $_config ]] || error "Invalid config file '$_kconfig'"

	rm -r "${CONFIG_CACHE:?}" 2>/dev/null
	mkdir -p "$CONFIG_CACHE"

	local _CONFIG_FILE=$CONFIG_CACHE/_invalid
	local _PARSE_DEPS=()

	_reader() {
		local _f=$1

		if [[ $_f == *"\$(SRCARCH)"* ]]; then
			_f=${_f/\$(SRCARCH)/$(get_kernel_src_arch "$_arch")}
		fi

		local _line
		while :; do
			_line=${_line%%#*}
			case $_line in
				"config "* | "menuconfig "* )
					_CONFIG_FILE=${_line#* }
					_CONFIG_FILE=${_CONFIG_FILE## }
					_CONFIG_FILE=$CONFIG_CACHE/$_CONFIG_FILE
					for _dep in "${_PARSE_DEPS[@]}"; do [[ $_dep ]] && echo "depends on $_dep"; done >> "$_CONFIG_FILE"
					;;
				"source "* )
					_line=${_line#* }
					_line=${_line#\"}
					_line=${_line%\"}
					_reader "$_line"
					;;
				"if "* )
					_line=${_line#if }
					_PARSE_DEPS+=( "${_line}" )
					;;
				"menu "* | "choice" )
					local _sub_dep=""
					while read -r _line; do
						case $_line in
							if*|menu*|config*|end*|source*|choice* ) break ;;
							"depends on"* )
								_line="${_line#depends on}"
								_sub_dep="${_sub_dep:+$_sub_dep && }(${_line## })"
								;;
							"visible if"* )
								_line="${_line#visible if}"
								_sub_dep="${_sub_dep:+$_sub_dep && }(${_line## })"
								;;
						esac
					done
					_PARSE_DEPS+=( "${_sub_dep## }" )
					continue
					;;
				"end"* )
					# We don't care about correctness,
					# it's always paired with previous if/menu/choice
					unset '_PARSE_DEPS[${#_PARSE_DEPS[@]}-1]'
					;;
				'' | ' ' )
					;;
				* )
					echo "$_line" >> "$_CONFIG_FILE"
			esac
			IFS='' read -r _line || break
		done <<< "$(<"$_f")"
	}

	pushd "$(dirname "$_kconfig")" > /dev/null || die "Failed pushd to '$TOPDIR'"

	_reader "$_kconfig"

	popd || die
}

# Call make for config related make target. Will set proper cross compiler and kernel arch name.
# $1: Target arch.
config_make() {
	local arch=$1; shift
	local config_cross_compiler config_arch

	pushd "$TOPDIR" >/dev/null || die "Not in a valid git repo."

	config_cross_compiler=$(_get_config_cross_compiler "$arch")
	config_arch=$(get_kernel_arch "$arch")

	if [[ -z "$config_arch" ]]; then
		die "Unsupported arch $arch"
	fi

	make ARCH="$config_arch" CROSS_COMPILE="$config_cross_compiler" "$@"

	popd >/dev/null || die "Failed popd"
}

# Dedup, and filter valid config lines, print the sanitized content sorted by config name.
config_sanitizer() {
	# AWK will add an extra column of CONFIG_NAME for sort to work properly
	LC_ALL=C
	awk '
		/is not set/ {
			split($0, val, "#");
			split(val[2], val);
			configs[val[1]]=$0
		}
		/^CONFIG.*=/ {
			split($0, val, "=");
			if (val[2] == "n")
				configs[val[1]]="# "val[1]" is not set"
			else
				configs[val[1]]=$0
		}
		END {
			for (config in configs) {
				print config " " configs[config]
			}
		}' \
	| LC_ALL=C sort -k 1 \
	| cut -d ' ' -f 2-
}

# Iterate the dot product of all config options
# param: <callback> [<filter>... ]
#   [<filter>... ]: Filters config targets to use. eg. kernel-default-*, kernel-minimal-debug
#   <callback>: A callback function, see below for <callback> params.
#
# Will generate a matrix, and call <callback> like this:
# <callback> <config-target> [ <dir entry of configs in inherit order> ... ]
#
# eg.
# <callback> "kernel-generic-release" "$TOPDIR/00pending/kernel" "$TOPDIR/20preset/generic" "$TOPDIR/50variant/release"
# <callback> "kernel-generic-debug" "$TOPDIR/00pending/kernel" "$TOPDIR/20preset/generic" "$TOPDIR/50variant/debug"
# ...
#
for_each_config_target () {
	local filters callback
	local target cur_target i j

	callback=$1; shift
	filters=("$@")

	_match () {
		[[ ${#filters[@]} -eq 0 ]] && return 0

		for _f in "${filters[@]}"; do
			# shellcheck disable=SC2053
			[[ $1 == $_f ]] && return 0
		done

		return 1
	}

	# The '_' is used to bootstrap the file loop
	local -a hierarchy=( _ ) prev_hierarchy target_conf prev_target_conf

	for folder in "${CONFIG_SPECS[@]}"; do
		prev_hierarchy=( "${hierarchy[@]}" )
		prev_target_conf=( "${target_conf[@]}" )

		hierarchy=( )
		target_conf=( )

		i=0
		for config in "$folder"/*; do
			if ! [[ -e "$config/default.config" ]]; then
				error "Invalid config folder $config"
				return 1
			fi

			j=0
			for target in "${prev_hierarchy[@]}"; do
				cur_target=$target-$(basename "$config")
				hierarchy+=( "$cur_target" )
				# config files can't have spece in their path since the
				# list is split by space, seems no better way
				target_conf[$i]="${prev_target_conf[$j]} $config"
				i=$(( i + 1 ))
				j=$(( j + 1 ))
			done
		done
	done

	i=0
	for target in "${hierarchy[@]}"; do
		# split target_conf by space is what we want here
		target="${target#_-}"
		# shellcheck disable=SC2086
		_match $target && "$callback" "${target#_-}" ${target_conf[$i]}
		i=$(( i + 1 ))
	done
}

# Iterate populated config files
# param: <callback> [<filter>... ]
#   [<filter>... ]: Filters config targets to use. eg. kernel-default-*, kernel-minimal-debug
#   <callback>: A callback function, see below for <callback> params.
#
# Will generate a matrix, and call <callback> like this:
# <callback> <arch> <config-file> [ <backing config files in inherit order>... ]
#
# eg.
# <callback> "x86_64" "kernel-generic-release" \
#            "$TOPDIR/00pending/kernel/default.config" "$TOPDIR/00pending/kernel/x86_64.config" \
#            "$TOPDIR/20preset/generic/default.config" "$TOPDIR/20preset/generic/x86_64.config" \
#            "$TOPDIR/50variant/release/default.config" "$TOPDIR/50variant/release/x86_64.config"
# <callback> "aarch64" "kernel-generic-release" \
#            "$TOPDIR/00pending/kernel/default.config" "$TOPDIR/00pending/kernel/aarch64.config" \
#            "$TOPDIR/20preset/generic/default.config" "$TOPDIR/20preset/generic/aarch64.config" \
#            "$TOPDIR/50variant/release/default.config" "$TOPDIR/50variant/release/aarch64.config"
# ...
#
for_each_config_product () {
	local _wrapper_cb=$1; shift

	_wrapper () {
		local target=$1; shift
		local files=( "$@" )

		for arch in "${CONFIG_ARCH[@]}"; do
			local config_basename="$target.$arch.config"
			local config_product="$CONFIG_OUTDIR/$config_basename"
			local config_files=( )

			for _conf in "${files[@]}"; do
				config_files+=( "$_conf/default.config" )
				if [[ -e "$_conf/$arch.config" ]]; then
					config_files+=( "$_conf/$arch.config" )
				fi
			done

			$_wrapper_cb "$arch" "$config_product" "${config_files[@]}"
		done
	}

	for_each_config_target _wrapper "$@"
}

# Simply concat the backing config files in order into a single files for each config target
populate_configs () {
	_merge_config () {
		local target=$1; shift
		local config_basename output_config
		for arch in "${CONFIG_ARCH[@]}"; do
			config_basename="$target.$arch.config"
			output_config="$CONFIG_OUTDIR/$config_basename"

			echo "Populating $config_basename from base configs..."

			for conf in "$@"; do
				cat "$conf/default.config"
				if [[ -e "$conf/$arch.config" ]]; then
					cat "$conf/$arch.config"
				fi
			done | config_sanitizer > "$output_config"
		done
	}

	for_each_config_target _merge_config "$@"
}

makedef_configs () {
	_makedef_config() {
		local target=$1; shift
		local populated_config

		for arch in "${CONFIG_ARCH[@]}"; do
			populated_config="$CONFIG_OUTDIR/$target.$arch.config"

			# config base name is always in this format: <name>.<arch>.config
			echo "Processing $(basename "$populated_config") with make olddefconfig..."

			if ! [ -f "$populated_config" ]; then
				error "Config not found: '$populated_config'"
				continue
			fi

			pushd "$TOPDIR" > /dev/null || exit 1

			config_make "$arch" KCONFIG_CONFIG="$populated_config" olddefconfig

			popd > /dev/null || return
		done
	}

	for_each_config_target _makedef_config "$@"
}
