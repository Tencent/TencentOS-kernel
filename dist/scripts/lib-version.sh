#!/bin/bash
#
# Version conversion helpers for building a TK kernel.
#
# shellcheck source=./lib.sh
. "$(dirname "$(realpath "$0")")/lib.sh"

## Autogenerated version pattern:
# <KERNEL_MAJVER>-<KERNEL_RELVER>.<KERNEL_DIST>

# KERNEL_MAJVER's format: <KVERSION>.<KPATCHLEVEL>.<KSUBLEVEL>
# It's the standdard upstream linux kernel release version,
# eg. 3.10.0; 5.4.119; 5.16.0
#
# KERNEL_RELVER's format: <REL>[.<SNAPSHOT>].<KEXTRAVERSION>
# If <REL> is 0, indicates it's a snapshot, unofficial release.
# If <REL> is >=1, indicates it the n'th release of that kernel version.
#
# KERNEL_DIST: <none>/tks/tlinux4/...
# Indicates this is a special build kernel.

# NOTE: This versioning script fully respects the git tag if tag's
# major kernel version matches the version numbers in kernel Makefile

# This naming style is compatible with Tencent Kernel public naming style.
# Take TK4 for example:
#
# Merge base:		git describe --tag			make install version	Generate version:
# 5.4.119 (master)	5.4.119-1-tlinux4-0007			5.4.119-1		5.4.119-1-tlinux4-0007
# 5.4.119 (master)	5.4.119-1-tlinux4-0007-2-g884a77bf0ba6	5.4.119-1		5.4.119-0.20211115git1135ec008ef3.1-tlinux4-0007
# 5.4.119 (master)	5.4.119-1-tlinux4-0007.subrelease	5.4.119-1		5.4.119-1-tlinux4-0007.subrelease
# 5.4.119 (master)	5.4.119-1-tlinux4-0007~rc1		5.4.119-1		5.4.119-1-tlinux4-0007~rc1 (*)
#
# NOTE: Sometime TK4's release version may go backwards, it's a known issue we have to live with.
# TK4 used tag like 5.4.119-1-tlinux4-0007.prerelease to indicate a release candidate.
# You can check this with `rpmdev-vercmp`:
# $ rpmdev-vercmp 5.4.119-1-tlinux4-0007.prerelease 5.4.119-1-tlinux4-0007
# $ 5.4.119-1-tlinux4-0007.prerelease > 5.4.119-1-tlinux4-0007
# This means RPM thinks the release candidate is higher than the real release, to fix that,
# try use tilde symbol to indicate it's a RC.

## Macros and values:
# Standard four-part linux kernel version from kernel's Makefile
KVERSION=
KPATCHLEVEL=
KSUBLEVEL=
KEXTRAVERSION=

# KPREMERGEWINDOW: If we are building a commit in the first merge window
# In the first merge time window, after a formal kernel release, and before rc1 release of next kernel,
# the KPATCHLEVEL will be stuck in lower value, which confuses RPM in many ways. So just bump
# KSUBLEVEL upon build in such case.
#
# For example, after 5.15 release, and before 5.16, upstream will start merging features for 5.16
# without bumping KPATCHLEVEL, which is stuck in 15.
# As now our build will be newer than 5.15 build, we have to make the version higher than 5.15
# To avoid conflict with 5.15 stable build like 5.15.Y-Z, we can't bump the Y part or Z part.
# So instead bump to 5.16 and mark it rc0 as 5.16.0-0.rc0. (Just as what Fedora does).
KPREMERGEWINDOW=

# KRCRELEASE: If we are building a rc-release
# KSNAPRELEASE: If we are building a snapshot-release
# Pre-release means we are building from an RC release
# Snap-release means we are building from an git commit without tag
export KRCRELEASE=
export KSNAPRELEASE=
export KTAGRELEASE=

# git snapshot versioning
KGIT_SNAPSHOT=
# Set if current commit is tagged with valid release info, force use it.
KGIT_FORCE_RELEAE=
# Set if current commit is tagged with a valid test tag name
KGIT_TESTBUILD_TAG=
# Release: Start from 1.0, indicate the release version, info embedded in git tag
KGIT_RELEASE=
KGIT_RELEASE_NUM=
KGIT_SUB_RELEASE_NUM=

### The formal kernel version and release

# Simulate `uname -r` output, which is always "$KVERSION.$KPATCHLEVEL.$KSUBLEVEL$KEXTRAVERSION"
export KERNEL_UNAMER=
# Basically: $KVERSION.$KPATCHLEVEL.$KSUBLEVEL (eg. 5.17.0, 5.16.3)
export KERNEL_MAJVER=
# Release version, may contain $KEXTRAVERSION (eg. 1, 0.rc0, 0.20220329gita11bf64a6e8f)
export KERNEL_RELVER=
# Kernel distro variable (eg. tks, tlinux4, <none>), with any leading "." or "-" removed
export KERNEL_DIST=
# Only used for make-release
export KERNEL_PREV_RELREASE_TAG=

# Get the tag of a git ref, if the git ref itself is a valid tag, just return itself
# else, search latest tag before this git ref.
_get_last_git_tag_of() {
	local gitref=$1; shift
	local last_tag tag
	local tagged

	for tag in $(git "$@" tag --points-at "$gitref"); do
		tagged=1
		last_tag="$tag"
		if [[ "$tag" == "$gitref" ]]; then
			break
		fi
		# If HEAD is tagged with multiple tags and user is not asking to use one of them,
		# use the first one found matching release info.
		if [[ "$gitref" == HEAD ]] && _get_rel_info_from_tag "$tag" > /dev/null; then
			break
		fi
	done

	if [[ -z "$last_tag" ]]; then
		tagged=0
		last_tag=$(git "$@" describe --tags --abbrev=0 "$gitref" 2>/dev/null)
	fi

	echo "$last_tag"
	return $tagged
}

# $1: git tag or git commit, defaults to HEAD
# $2: kernel source tree, should be a git repo
get_kernel_code_version() {
	local gitref=${1:-HEAD}
	local repo=${2:-$TOPDIR}
	local makefile

	makefile=$(git -C "$repo" show "$gitref:Makefile" 2>/dev/null || cat "$repo/Makefile")

	if [ ! "$makefile" ]; then
		die "Error: Failed to read Makefile"
		return 1
	fi

	KVERSION=$(sed -nE '/^VERSION\s*:?=\s*/{s///;p;q}' <<< "$makefile")
	KPATCHLEVEL=$(sed -nE '/^PATCHLEVEL\s*:?=\s*/{s///;p;q}' <<< "$makefile")
	KSUBLEVEL=$(sed -nE '/^SUBLEVEL\s*:?=\s*/{s///;p;q}' <<< "$makefile")
	KEXTRAVERSION=$(sed -nE '/^EXTRAVERSION\s*:?=\s*/{s///;p;q}' <<< "$makefile")

	if [[ -z "$KVERSION" ]] || [[ -z "$KPATCHLEVEL" ]] || [[ -z "$KSUBLEVEL" ]]; then
		die "Invalid VERSION, PATCHLEVEL or SUBLEVEL in Makefile"
		return 1
	fi

	# RC releases are always considered pre-release
	if [[ "$KEXTRAVERSION" == "rc"* ]] || [[ $KEXTRAVERSION == "-rc"* ]]; then
		KRCRELEASE=1
	fi

	# Read KDIST using gitref for historical accurate value.
	KERNEL_DIST=$(get_dist_makefile_var KDIST "$gitref" "$repo")

	return 0
}

_first_merge_window_detection() {
	local gitref=${1:-HEAD}
	local repo=${2:-$TOPDIR}
	local upstream_base upstream_lasttag upstream_gitdesc
	local tagged

	# If KSUBLEVEL or KEXTRAVERSION is  set, it's not in the first merge window of a major release
	[[ $KSUBLEVEL -eq 0 ]] || return 1
	[[ -n $KEXTRAVERSION ]] && return 1
	[[ $upstream ]] || upstream="@{u}"

	# Get latest merge base if forked from upstream to merge window detection
	# merge window is an upstream-only thing
	if upstream_base=$(git -C "$repo" merge-base "$gitref" "$upstream" 2>/dev/null); then
		upstream_gitdesc=$(git -C "$repo" describe --tags --abbrev=12 "$upstream_base" 2>/dev/null)
		upstream_lasttag=$(_get_last_git_tag_of "$upstream_base" -C "$repo")
		tagged=$?

		if \
			# If last tag is an tagged upstream release
			[[ $upstream_lasttag == v$KVERSION.$KPATCHLEVEL ]] && \
			# And if merge base is ahead of the taggewd release
			[[ "$tagged" -eq 1 ]]; then
			# Then it's in first merge window
			return 0
		fi
	else
		warn "Not tracking a valid upstream, merge window detecting is disabled ."
	fi

	return 1
}

# Get release info from git tag
_get_rel_info_from_tag() {
	local tag=$1 rel ret=0
	local kextraversion=${KEXTRAVERSION#-}

	if [[ $tag == *"$KVERSION.$KPATCHLEVEL.$KSUBLEVEL"* ]]; then
		rel=${tag#*"$KVERSION.$KPATCHLEVEL.$KSUBLEVEL"}
	elif [[ "$KSUBLEVEL" = "0" ]] && [[ $tag == *"$KVERSION.$KPATCHLEVEL"* ]]; then
		rel=${tag#*"$KVERSION.$KPATCHLEVEL"}
	elif [[ $KPREMERGEWINDOW ]] && [[ $tag == *"$KVERSION.$((KPATCHLEVEL + 1)).$KSUBLEVEL"* ]]; then
		rel=${tag#*"$KVERSION.$((KPATCHLEVEL + 1)).$KSUBLEVEL"}
	else
		return 1
	fi

	rel=${rel//-/.}
	rel=${rel#.}

	if [[ -z "$kextraversion" ]]; then
		# If previous KEXTRAVERSION is not empty but now empty,
		# still consider it a valid release tag since release candidate mark may get dropped.
		# But this really should look at the Makefile corresponding to that tag commit
		:
	elif [ "$kextraversion" -eq "$kextraversion" ] &>/dev/null; then
		case $rel in
			# Extra version is release number, ok
			$kextraversion | "$kextraversion."* ) ;;
			* ) return 1; ;;
		esac
	else
		# Remove RC liked tag, append them as suffix later.
		case $rel in
			# Plain version tag, eg. 5.17-rc3
			$kextraversion )
				rel=""
				;;
			# Plain version tag plus suffix, eg. 5.17-rc3.*
			"$kextraversion."* )
				rel=${rel#$kextraversion.}
				;;
			# Extra tag, eg 5.17-1.rc3*
			*".$kextraversion"* | *"-$kextraversion"* ) ;;
			* ) return 1; ;;
		esac
	fi

	# If KERNEL_DIST is added as prefix/semi-prefix/suffix, remove it from rel
	if [[ $KERNEL_DIST ]]; then
		case $rel in
			$KERNEL_DIST.*)
				rel=${rel#$KERNEL_DIST.}
				;;
			$kextraversion.$KERNEL_DIST.*)
				rel=${rel#$kextraversion.$KERNEL_DIST.}
				rel=$kextraversion.$rel
				;;
			*.$KERNEL_DIST)
				rel=${rel%.$KERNEL_DIST}
				;;
		esac
	fi

	echo "$rel"
}

_search_for_release_tag() {
	local tag=$1
	local repo=$2

	# Look back for 5 commits for a valid tag
	local limit=5
	while :; do
		limit=$((limit - 1))

		if [[ $limit -le 0 ]] || ! tag=$(git -C "$repo" describe --tags --abbrev=0 "$tag^" 2>/dev/null); then
			warn "No valid tag found that are eligible for versioning, please fix your repo and tag it properly."
			return 1
		fi

		if _get_rel_info_from_tag "$tag" > /dev/null; then
			echo "$tag"
			return 0
		fi
	done
}

# Get release info from git tag
#
# We try to store the RPM NVR (Name, Version, Release) info's VR part in git tag
# N: is always kernel
# V: is kernel's major release version (eg. 5.18, 5.18.0, 5.17.2)
# R: is a tokens seperated with '.' (eg 1[.KDIST], 2[.KDIST], 2.1[.KDIST], 0.rc1[.KDIST])
#    could also be 0.YYYYMMDDgit<commit> for snapshot release.
#    But ideally all git tag are for formal release so snapshot tag shouldn't appear in repo.
#
# A tag that contains VR is considered a release tag.
#
# $1: git tag or git commit, defaults to HEAD
# $2: kernel source tree, should be a git repo
# $3: optional upstream branch, remote branch name current HEAD is tracking
get_kernel_git_version()
{
	local gitref=${1:-HEAD}
	local repo=${2:-$TOPDIR}
	local upstream=${3:-"@{u}"}

	local last_tag release_tag release_info git_desc
	local tag tagged

	# Get current commit's snapshot name, format: YYYYMMDDgit<commit>,
	# or YYYYMMDD (Only if repo is missing, eg. running this script with tarball)
	if ! KGIT_SNAPSHOT=$(git rev-parse --short=12 "$gitref" 2>/dev/null); then
		warn "Invalid git reference '$gitref' or git repo is unavailable, versioning it as a dated snapshot"
		KGIT_SNAPSHOT=$(date +"%Y%m%d")
		KSNAPRELEASE=1
		return
	fi
	KGIT_SNAPSHOT=$(date +"%Y%m%d")git$KGIT_SNAPSHOT

	# Check if first merge-window, and set KPREMERGEWINDOW, see comment above about KPREMERGEWINDOW
	if _first_merge_window_detection "$@"; then
		KPREMERGEWINDOW=1
	fi

	# Get latest git tag of this commit
	last_tag=$(_get_last_git_tag_of "$gitref" -C "$repo")
	tagged=$?
	# Check and get latest release git tag, in case current tag is a test tag
	# (eg. current tag is fix_xxxx, rebase_test_xxxx, or user tagged for fun, and previous release tag is 5.18.0-1[.KDIST])
	if _get_rel_info_from_tag "$last_tag" > /dev/null; then
		# Latest tag is a release tag, just use it
		release_tag=$last_tag
	else
		warn "Latest git tag '$last_tag' is not a release tag, it does't match Makefile version '$KVERSION.$KPATCHLEVEL.$KSUBLEVEL$KEXTRAVERSION'"
		if release_tag=$(_search_for_release_tag "$last_tag" "$repo"); then
			warn "Found release tag '$release_tag'."
		fi
	fi

	if [[ "$release_tag" ]]; then
		git_desc=$(git -C "$repo" describe --tags --abbrev=12 "$gitref" 2>/dev/null)
		release_info=$(_get_rel_info_from_tag "$release_tag")

		if ! [[ $release_info ]]; then
			warn "No extra release info in release tag, might be a upstream tag." \
				"Please make a release commit with 'make dist-release' for a formal release."
			KGIT_RELEASE_NUM=0
			KGIT_SUB_RELEASE_NUM=0
		elif [[ $release_info == 0.* ]]; then
			KGIT_RELEASE_NUM=${release_info##*.}
			KGIT_RELEASE_NUM=${KGIT_RELEASE_NUM##*-}
			KGIT_SUB_RELEASE_NUM=${release_info%."$KGIT_RELEASE_NUM"}
			KGIT_SUB_RELEASE_NUM=${KGIT_SUB_RELEASE_NUM##*.}
		else
			KGIT_RELEASE_NUM=${release_info%%.*}
			KGIT_RELEASE_NUM=${KGIT_RELEASE_NUM%%-*}
			KGIT_SUB_RELEASE_NUM=${release_info#"$KGIT_RELEASE_NUM".}
			KGIT_SUB_RELEASE_NUM=${KGIT_SUB_RELEASE_NUM%%.*}
		fi

		# Fix release numbers, if it's not a number
		if ! [ "$KGIT_RELEASE_NUM" -eq "$KGIT_RELEASE_NUM" ] &>/dev/null; then
			warn "Unrecognizable release number: $KGIT_RELEASE_NUM, resetting to 0"
			KGIT_RELEASE_NUM=0
		fi
		if ! [ "$KGIT_SUB_RELEASE_NUM" -eq "$KGIT_SUB_RELEASE_NUM" ] &>/dev/null; then
			KGIT_SUB_RELEASE_NUM=0
		fi

		if [[ "$tagged" -eq 1 ]] && [[ "$release_tag" == "$last_tag" ]]; then
			if [[ $release_info ]] && [[ "$KGIT_RELEASE_NUM" -ne 0 ]]; then
				# This commit is tagged and it's a valid release tag, juse use it
				KGIT_FORCE_RELEAE=$release_info
				KTAGRELEASE=$release_tag
			fi
		elif [[ "$last_tag" == "$git_desc" ]]; then
			# A dumb assumption here, if it's not in *.* format (v5.4, 4.12.0, etc...) it's a test tag
			if [[ $last_tag != v*.* ]] && [[ $last_tag != *.*.* ]]; then
				warn "'$last_tag' looks like a test tag, using it as versioning suffix."
				warn "Please tag properly for release build, now versioning it as a test build."
				KGIT_TESTBUILD_TAG=testbuild.$last_tag
			else
				warn "'$last_tag' looks like a kernel release tag but out-of-sync with Makefile" \
					"ignoring it and versioning as snapshot."
				warn "Please tag properly for release build."
				KSNAPRELEASE=1
			fi
		else
			# Just a simple untagged commit, nothing special
			KSNAPRELEASE=1
		fi

		KERNEL_PREV_RELREASE_TAG=$release_tag
	else
		KSNAPRELEASE=1
		KGIT_RELEASE_NUM=0
	fi

}

_prepare_kernel_ver() {
	if ! get_kernel_code_version "$@"; then
		return 1
	fi

	if ! get_kernel_git_version "$@"; then
		return 1
	fi

	# Disable PRE-merge window detection for tagged commit,
	# We want to following user provided tag strictly
	if [[ ! $KGIT_FORCE_RELEAE ]]; then
		if [[ $KPREMERGEWINDOW ]]; then
			KPATCHLEVEL=$(( KPATCHLEVEL + 1 ))
		fi
	fi
}

### Generate a RPM friendly version based on kernel tag and commit info
#
# Examples:
# (Ignoring pre-release window detection, see comments about KPREMERGEWINDOW)
# (Also assume Makefile's kernel version info is all correct)
#
# git describe --tags                 Generated version (uname -r)                    Corresponding RPM version
# v5.18                               5.18.0-0[.KDIST]                                kernel[-kdist]-5.18.0-0.rc1
# v5.18.0                             5.18.0-0[.KDIST]                                kernel[-kdist]-5.18.0-0.rc1
# v5.18.1                             5.18.1-0[.KDIST]                                kernel[-kdist]-5.18.1-0.rc1
# v5.18.0-1-gac28df2                  5.18.0-0.20220428gitac28df2cd5d0[.KDIST]        kernel[-kdist]-5.18.0-0.20220428gitac28df2cd5d0 *
# v5.18.0-3-g16cadc5                  5.18.0-0.20220428git16cadc58d50c[.KDIST]        kernel[-kdist]-5.18.0-0.20220428git16cadc58d50c *
# v5.18-rc1                           5.18.0-0.rc1[.KDIST]                            kernel[-kdist]-5.18.0-0.rc1
# v5.18-rc1-1-g380a504                5.18.0-0.20220428git380a504e42d9.rc1[.KDIST]    kernel[-kdist]-5.18.0-0.20220428git380a504e42d9.rc1
# 5.18.0-1[.KDIST]                    5.18.0-1[.KDIST]                                kernel[-kdist]-5.18.0-1
# 5.18.12-3[.KDIST]                   5.18.12-3[.KDIST]                               kernel[-kdist]-5.18.12-3
# 5.18.12-3[.KDIST]-5-g9318b03        5.18.12-0.20220428git9318b0349d5c.3[.KDIST]     kernel[-kdist]-5.18.12-0.20220428git9318b0349d5c.3
# 5.18.0-12.rc1[.KDIST]               5.18.0-12.rc1[.KDIST]                           kernel[-kdist]-5.18.0-12.rc1
# 5.18.0-12[.KDIST]-2-g551f9cd        5.18.0-0.20220428git551f9cd79ece.12[.KDIST]     kernel[-kdist]-5.18.0-0.20220428git551f9cd79ece.12
# x86-5.4.119-19-0010.prerelease      5.4.119-19-0010.prerelease                      kernel[-kdist]-5.4.119-19-0010.prerelease **
#
# * NOTE: random commit snapshot can't be versioning in non-decreasing since one can always amend/rebase, so only the date stamp matters now.
#
# As you may have noticed, release always start with '0' unless a git tag have release >= 1,
# The tag should be generated by other commands that comes later in this script.
prepare_kernel_ver() {
	_prepare_kernel_ver "$@"

	# If it's a tagged commit, and the release number in tag is non-zero, use that
	if [[ $KGIT_FORCE_RELEAE ]]; then
		KERNEL_MAJVER="$KVERSION.$KPATCHLEVEL.$KSUBLEVEL"
		KERNEL_RELVER="$KGIT_FORCE_RELEAE"
	else
		local krelease=0
		if [[ $KSNAPRELEASE ]]; then
			krelease=$krelease.$KGIT_SNAPSHOT
		elif [[ $KGIT_RELEASE ]]; then
			krelease=$krelease.$KGIT_RELEASE
		fi

		if [[ $KEXTRAVERSION ]]; then
			krelease="$krelease.${KEXTRAVERSION##-}"
		elif [[ $KPREMERGEWINDOW ]]; then
			krelease="$krelease.rc0"
		fi

		if [[ $KGIT_TESTBUILD_TAG ]]; then
			# '-' is not allowed in release name, but commonly used in tag
			krelease="$krelease.${KGIT_TESTBUILD_TAG//-/_}"
		fi

		KERNEL_MAJVER="$KVERSION.$KPATCHLEVEL.$KSUBLEVEL"
		KERNEL_RELVER="$krelease${KDIST:+.$KDIST}"
	fi

	KERNEL_UNAMER="$KERNEL_MAJVER-$KERNEL_RELVER"
}

### Generate formal release version based on kernel tag and commit info
#
# Examples:
# (Ignoring pre-release window detection, see comments about KPREMERGEWINDOW)
# (Also assume Makefile's kernel version info is all correct)
#
# git describe --tags                 Generated Version (uname -r)                    Corresponding RPM version
# v5.18                               5.18.0-1[.KDIST]                                kernel[-kdist]-5.18.0-1
# v5.18.0                             5.18.0-1[.KDIST]                                kernel[-kdist]-5.18.0-1
# v5.18.1                             5.18.1-1[.KDIST]                                kernel[-kdist]-5.18.1-1
# v5.18.0-1-gac28df2                  5.18.0-1[.KDIST]                                kernel[-kdist]-5.18.0-1
# v5.18.0-3-g16cadc5                  5.18.0-1[.KDIST]                                kernel[-kdist]-5.18.0-1
# v5.18-rc1                           5.18.0-1.rc1[.KDIST]                            kernel[-kdist]-5.18.0-1.rc1
# v5.18-rc1-1-g380a504                5.18.0-1.rc1[.KDIST]                            kernel[-kdist]-5.18.0-1.rc1
# 5.18.0-1[.KDIST]                    5.18.0-2[.KDIST]                                kernel[-kdist]-5.18.0-2
# 5.18.12-3[.KDIST]                   5.18.12-4[.KDIST]                               kernel[-kdist]-5.18.12-4
# 5.18.12-3[.KDIST]-5-g9318b03        5.18.12-4[.KDIST]                               kernel[-kdist]-5.18.12-4
# 5.18.0-12.rc1[.KDIST]               5.18.0-12.rc1[.KDIST]                           kernel[-kdist]-5.18.0-13.rc1
# 5.18.0-12[.KDIST]-2-g551f9cd        5.18.0-0.20220428git551f9cd79ece.12[.KDIST]     kernel[-kdist]-5.18.0-13
# x86-5.4.119-19-0010.prerelease      5.4.119-20[.KDIST]                              kernel[-kdist]-5.4.119-20[.KDIST]
#
# * NOTE: random commit snapshot can't be versioning in non-decreasing since one can always amend/rebase, so only the date stamp matters now.
# ** NOTE: Yes, this script is compatible with TK4
#
# As you may have noticed, release always start with '0' unless a git tag have release >= 1,
# The tag should be generated by other commands that comes later in this script.
prepare_next_kernel_ver() {
	_prepare_kernel_ver "$@"

	if [[ $KPREMERGEWINDOW ]]; then
		warn "Upstream is in merge window, forcing a formal release is not recommanded."
	fi

	krelease=$((KGIT_RELEASE_NUM + 1))

	if [[ $KEXTRAVERSION ]]; then
		krelease="$krelease.${KEXTRAVERSION##-}"
	elif [[ $KPREMERGEWINDOW ]]; then
		krelease="$krelease.rc0"
	fi

	KERNEL_MAJVER="$KVERSION.$KPATCHLEVEL.$KSUBLEVEL"
	KERNEL_RELVER="$krelease"
	KERNEL_UNAMER="$KERNEL_MAJVER-$KERNEL_RELVER${KDIST:+.$KDIST}"
}

# Get next formal kernel version based on previous git tag
# Same as prepare_next_kernel_ver, but increase sub version instead.
# eg. instead of 5.18.12-3[.KDIST] -> 5.18.12-4[.KDIST], this generates 5.18.12-3[.KDIST] -> 5.18.12-3.1[.KDIST]
prepare_next_sub_kernel_ver() {
	_prepare_kernel_ver "$@"

	if [[ $KPREMERGEWINDOW ]]; then
		warn "Upstream is in merge window, forcing a formal release is not recommanded."
	fi

	krelease=$((KGIT_RELEASE_NUM + 0))
	krelease=$krelease.$((KGIT_SUB_RELEASE_NUM + 1))

	if [[ $KEXTRAVERSION ]]; then
		krelease="$krelease.${KEXTRAVERSION##-}"
	elif [[ $KPREMERGEWINDOW ]]; then
		krelease="$krelease.rc0"
	fi

	KERNEL_MAJVER="$KVERSION.$KPATCHLEVEL.$KSUBLEVEL"
	KERNEL_RELVER="$krelease"
	KERNEL_UNAMER="$KERNEL_MAJVER-$KERNEL_RELVER${KDIST:+.$KDIST}"
}
