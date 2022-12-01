#!/bin/bash --norc
# SPDX-License-Identifier: GPL-2.0
#
# This script takes the merged config files and processes them through listnewconfig
# then append the new options to pending config

# shellcheck source=./lib-config.sh
. "$(dirname "$(realpath "$0")")/lib-config.sh"

# shellcheck disable=SC2164
export CONFIG_OUTDIR=$DISTDIR/workdir
AUTOFIX=0

usage()
{
	cat << EOF
check-configs.sh [sub command] [--autofix] [ <config targets> ... ]
	Available sub command:

	check-new-configs		Check for new unset kernel options.
					If --autofix (or env var AUTOFIX) is enabled,
					will append to pending configs

	check-dup-configs		Check for kernel configs of the same value in different archs.
					If --autofix (or env var AUTOFIX) is enabled,
					will move these configs from arch
					specific config to the default base config.

	check-diff-configs		Check for kernel configs that failed to enabled,
					which means, they are set =y/=m in config matrix,
					but end up disabled in the final .config file.
					Doesn't support AUTOFIX since issues caused by
					Kconfig dependency is complex and always need manual fix.
EOF
}

# Use make listnewconfig to check new Kconfigs
check_new_configs() {
	# First generate plain concated config files
	populate_configs "$@"

	# Check if there are unset config
	_check_new_kconfigs() {
		local arch=$1 config_file=$2 base_file=$3 base_configs new_configs

		echo_green "=== Checking $config_file..."
		new_configs=$(config_make "$arch" KCONFIG_CONFIG="$config_file" --no-print-directory listnewconfig)

		if [ -n "$new_configs" ]; then
			echo_yellow "=== Following configs are not set properly:"
			echo "$new_configs"

			if [[ $AUTOFIX -eq 1 ]]; then
				echo_green "=== Appending these new configs to the base config file: '$base_file'"
				echo "$new_configs" >> "$base_file"

				base_configs=$(<"$base_file")
				{
					echo "$base_configs"
					echo "$new_configs"
				} | config_sanitizer > "$base_file"
			fi
		else
			echo_green "=== Config is all clear"
		fi

		# Add a newline as seperator
		echo
	}

	for_each_config_product _check_new_kconfigs "$@"
}

check_dup_configs() {
	# Merge common config items among different archs with same value into base default config.
	_dedup_single_dir() {
		local dir=$1
		local common_conf base_conf

		echo_green "=== Checking config dir '$dir'"
		for arch in "${CONFIG_ARCH[@]}"; do
			[[ -e "$dir/$arch.config" ]] || continue

			if [[ -z "$common_conf" ]]; then
				# Read-in first arch specific config
				common_conf=$(config_sanitizer < "$dir/$arch.config")
			else
				# Keep the common config items of differnt archs
				# The content is filtered with config_sanitizer so `comm` is enough
				common_conf=$(comm -12 \
					<(echo "$common_conf") \
					<(config_sanitizer < "$dir/$arch.config")
				)
			fi
		done

		if [[ "$common_conf" ]]; then
			echo_yellow "=== Common configs shared by all sub arch:"
			echo "$common_conf"

			if [[ $AUTOFIX -eq 1 ]]; then
				base_conf=$(<"$dir/default.config")
				{
					echo "$base_conf"
					echo "$common_conf"
				} | config_sanitizer > "$dir/default.config"
			fi
		else
			echo_green "=== Config is all clear"
		fi
	}

	_get_config_dirs() {
		shift; printf "%s\n" "$@"
	}

	for dir in $(for_each_config_target _get_config_dirs "$@" | sort -u); do
		_dedup_single_dir "$dir"
	done
}

# generate diff file using kernel script diffconfig after make olddefconfig
# param: config.file.old config.file
check_diff_configs() {
	local ALL_CONFIGS

	[[ -x "$TOPDIR"/scripts/diffconfig ]] || die "This command rely on linux's in-tree diffconfig script"

	ALL_CONFIGS=$(find "$TOPDIR" -name "Kconfig*" -exec grep -h "^config .*" {} + | sed "s/^config //")

	# First generate plain concated config files
	populate_configs "$@"

	_make_a_copy() {
		cp "$2" "$2.orig"
	}
	for_each_config_product _make_a_copy "$@"

	makedef_configs "$@"

	# Check why a config is not enabled
	_check_config_issue() {
		local _config=$1 _config_file=$2

		if ! echo "$ALL_CONFIGS" | grep -q "\b$_config\b"; then
			echo "$_config no longer exists, maybe it's deprecated or renamed."
			return
		fi

		echo "$_config have following depends:"
		get_all_depends "$_config" "$_config_file" || echo "No dependency found, likely not available on this arch."
		echo
	}

	_check_diff_configs() {
		local arch=$1 config_file=$2 diff_configs

		echo "=== Checking config $config_file"

		echo "= Parsing Kconfig ..."
		kconfig_parser "$arch" "$TOPDIR/Kconfig" "$config_file"

		echo "= Checking configs ..."
		diff_configs=$("$TOPDIR"/scripts/diffconfig "$config_file.orig" "$config_file" |
			# Ignore new added config, they should be covered by check_new_configs
			# If kernel didn't ask us to fill one config, then it's an auto-selected config
			grep -v "^+" | \
			# Ignore disappeared configs that were unset
			grep -v "^\-.*n$" \
		)

		if [[ -z "$diff_configs" ]]; then
			echo_green "=== Config is all clear"
			return
		fi

		echo "$diff_configs" | while read -r _line; do
			local config config_hint=""
			case $_line in
				-*" m" | -*" y" )
					config=${_line% *}
					config=${config#-}
					config_hint=$(_check_config_issue "$config" "$config_file")
					;;
			esac

			echo_yellow "$_line"
			if [[ "$config_hint" ]]; then
				echo_yellow "$config_hint" | sed "s/^/    /"
			fi
			echo
		done
		echo
	}

	for_each_config_product _check_diff_configs "$@"
}

CMD=$1; shift
[[ $1 == '--autofix' ]] && AUTOFIX=1
case "$CMD" in
	check-new-configs )
		check_new_configs "$@"
		;;
	check-dup-configs )
		check_dup_configs "$@"
		;;
	check-diff-configs )
		check_diff_configs "$@"
		;;
	*)
		usage
		exit 1
		;;
esac
