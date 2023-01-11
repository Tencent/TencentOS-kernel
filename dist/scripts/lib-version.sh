#!/bin/bash
#
# Version conversion helpers for building a TK kernel.
#
# shellcheck source=./lib.sh
. "$(dirname "$(realpath "$0")")/lib.sh"

## Standardized kernel package version and uname are composed as follows:
#
# A valid tag:                             [PREFIX-]<KERNEL_MAJVER>-<KERNEL_RELVER>[.<KERNEL_DIST>]
# uname -r:                                         <KERNEL_MAJVER>-<KERNEL_RELVER>[.<KERNEL_DIST>][+<LOCALVER>]
# RPM NVR:      kepnel[-<KERNEL_DIST>][-<LOCALVER>]-<KERNEL_MAJVER>-<KERNEL_RELVER>
#               <NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN>-<VVVVVVVVVVVVV>-<RRRRRRRRRRRRR>
#
# Some NOTES about why we compose these string in above way:
# - Notice KERNEL_DIST is moved to N part of the RPM NVR, this is how TK3/TK4 release have been doing
#   and that is correct because we need to distingush between kernel release streams. And there are
#   things will break if we move it out of this part (mostly due to package name change).
# - RPM split the whole package name by '-', then recognize the right most part as R, second right
#   most part is V, so KERNEL_MAJVER and KERNEL_RELVER can't contain '-'.
# - LOCALVER is commonly used to present variants of kernel, that is, using same kernel repo commit/version/tag,
#   just built with a different config.
#   A example is RPM pkg kernel-5.4.119-1 (uname 5.4.119-1) and kernel-debug-5.4.119-1 (uname 5.4.119-1+debug),
#   the later one is same kernel built with more debug configs enabled. When kernel-5.4.119-1 run into unkown
#   issues, kernel-debug-5.4.119-1 could be installed to do more diagnosis.
# - Notice LOCALVER is moved to "N" part of the RPM NVR, because adding to "V" or "R" part breaks kernel
#   package versioning. A suffix, prefix or in-fix of "V" or "R" could cause the package or repo manager
#   to make variants override each other, and fails the system unexpectly. For example, an debug kernel
#   could be wrongly installed with a normal system wide package update, since the suffix made it had a
#   high version number and it shares same Name with vanilla kernel.
# - Some old TK4 tag will have KERNEL_DIST as part of KERNEL_RELVER, we cover that too.
#
## More explanations of each field:
#
### PREFIX: <none>/release/x86/aarch64/oc/....
# - Could be some well-known string like "release", "x86", ..., could be used to make tags more distinguishable.
#
### KERNEL_MAJVER: <VERSION>.<PATCHLEVEL>.<SUBLEVEL>
# - It's the standdard upstream linux kernel release version, presents in kernel's root Makefile, eg:
#   VERSION = 5
#   PATCHLEVEL = 4
#   SUBLEVEL = 203
#   Which stands for kernel 5.4.203
#
### KERNEL_RELVER: [0.<SNAPSHOT>.][<EXTRAVERSION>.]<REL>
# - If starts with 0, indicates it's a snapshot, unofficial release. Else it must be a tagged release.
#   The <SNAPSHOT> string is automatically generated using git commit as versioning base for untagged
#   test builds.
# - If EXTRAVERSION is non-empty, it must present here.
# - REL is a custom release string, should be alphanums be splitted by '.'.
#   eg. 0011, 0009.12, 0011.prerelease1, ...
#   eg. 2207.1.1, 2207.1.2, ...
#
# NOTE: due to historical reason, in KERNEL_RELVER, it could contain '-', but the final generated string that will be used in
# spec file and uname will always be converted to contain '.' only, to comply the RPM NVR naming style, also make things cleaner.
#
### KERNEL_DIST: <none>/tks/tlinux4/stable/stream/...
# Indicates this is a special build kernel, will show up in RPM package name to distinguish different kernel release stream.
# Is configurable through the KDIST variable in dist/Makefile.
#
# NOTE: Due to historical reason, if KDIST is added as first part of KERNEL_RELVER's <REL> string, it will be move to tail.
# To make the KERNEL_RELVER part consistent between RPM name, tag and uname.
#
# Example:
# git describe --tag			 RPM							 uname -r
# 5.4.119-1-tlinux4-0007		 kernel-tlinux4-5.4.119-1.0007				 5.4.119-1.0007.tlinux4
# 5.4.119-1-tlinux4-0007-2-g884a77bf0ba6 kernel-tlinux4-5.4.119-0.20211115git1135ec008ef3.1.0007 5.4.119-0.20211115git1135ec008ef3.1.0007.tlinux4
# 5.4.119-1-tlinux4-0007.subrelease	 kernel-tlinux4-5.4.119-1.0007.subrelease		 5.4.119-1.0007.subrelease.tlinux4
# 5.4.119-1-tlinux4-0007~rc1		 kernel-tlinux4-5.4.119-1.0007~rc1			 5.4.119-1.0007~rc1.tlinux4 (*)
#
# NOTE: Sometime TK4's release version may go backwards, it's a known issue we have to live with.
# TK4 used tag like 5.4.119-1-tlinux4-0007.prerelease to indicate a release candidate.
# You can check this with `rpmdev-vercmp`:
# $ rpmdev-vercmp 5.4.119-1-tlinux4-0007.prerelease 5.4.119-1-tlinux4-0007
# $ 5.4.119-1-tlinux4-0007.prerelease > 5.4.119-1-tlinux4-0007
# This means RPM thinks the release candidate is higher than the real release, to fix that,
# try use tilde symbol to indicate it's a RC.

## Macros and values:
# Alias of four-part linux kernel version from kernel's Makefile
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
# Set to true if this is a rolling build tracks upstream
KROLLING=
# Set to 1 to allow git tag force override uname
KFORCEUNAMER=

# git snapshot versioning
KGIT_SNAPSHOT=
# Raw info from current git tag
KGIT_TAG_RELEASE_INFO_RAW=
# Set if current commit is tagged with valid release info
KGIT_TAG_RELEASE_INFO=
# Set if a previous commit is found tagged with valid release info
KGIT_LATEST_TAG_RELEASE_INFO=
# Set if current commit is tagged with a valid test tag name
KGIT_TESTBUILD_TAG=
# Release: Start from 1.0, indicate the release version, info embedded in git tag
KGIT_RELEASE_NUM=
KGIT_SUB_RELEASE_NUM=

### The formal kernel version and release
# Simulate `uname -r` output, which is always "$KVERSION.$KPATCHLEVEL.$KSUBLEVEL$KEXTRAVERSION"
export KERNEL_UNAMER=
# Basically: $KVERSION.$KPATCHLEVEL.$KSUBLEVEL (eg. 5.17.0, 5.16.3)
export KERNEL_MAJVER=
# Release version (eg. 1, 0.rc0, 0.20220329gita11bf64a6e8f), see comments at the beginning of this file
export KERNEL_RELVER=
# Kernel distro variable (eg. tks, tlinux4, <none>), with any leading "." or "-" removed, see comments at the beginning of this file
export KERNEL_DIST=
# Only used for make-release sub command, get latest release tag of current commit
export KERNEL_PREV_RELREASE_TAG=

# Set if it's a tagged release
export KTAGRELEASE=
# KTESTRELEASE: If we are building based on a test tag
KTESTRELEASE=
# KSNAPRELEASE: If we are building a snapshot-release
KSNAPRELEASE=
# KRCRELEASE: If we are building a rc-release
KRCRELEASE=

_is_num() {
	[ "$1" -eq "$1" ] &>/dev/null
}

# Get the tag of a git ref, if the git ref itself is a valid tag, just return itself
# else, search latest tag before this git ref.
_get_last_git_tag_of() {
	local gitref=$1; shift
	local last_tag tag
	local tagged

	# If multiple tags presents, used the one specified by user
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
# Parse fondunmental kernel versioning info from Makefiles.
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
	# Replace '-' in KEXTRAVERSION
	KEXTRAVERSION=${KEXTRAVERSION//-/.}
	KEXTRAVERSION=${KEXTRAVERSION#.}

	if [[ -z "$KVERSION" ]] || [[ -z "$KPATCHLEVEL" ]] || [[ -z "$KSUBLEVEL" ]]; then
		die "Invalid VERSION, PATCHLEVEL or SUBLEVEL in Makefile"
		return 1
	fi

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

_do_strip_kernel_majver() {
	local rel

	if [[ $1 == *"$KVERSION.$KPATCHLEVEL.$KSUBLEVEL"* ]]; then
		rel=${1#*"$KVERSION.$KPATCHLEVEL.$KSUBLEVEL"}
	elif [[ "$KSUBLEVEL" = "0" ]] && [[ $1 == *"$KVERSION.$KPATCHLEVEL"* ]]; then
		rel=${1#*"$KVERSION.$KPATCHLEVEL"}
	elif [[ $KPREMERGEWINDOW ]] && [[ $1 == *"$KVERSION.$((KPATCHLEVEL + 1)).$KSUBLEVEL"* ]]; then
		rel=${1#*"$KVERSION.$((KPATCHLEVEL + 1)).$KSUBLEVEL"}
	else
		return 1
	fi

	echo "$rel"
}

# Check and strip the leading VERSION.PATCHLEVEL.SUBLEVEL of a tag,
# (eg. 5.18.19) and potential prefixes. If the tag doesn't match its corresponding,
# kernel version, return 1.
_check_strip_kernel_majver() {
	local tag=$1 rel
	local makefile
	local _kversion _kpatchlevel _ksublevel

	if rel=$(_do_strip_kernel_majver "$tag"); then
		echo "$rel"
		return 0
	fi

	# Update VERSION/PATCHLEVEL/SUBLEVEL using target Makefile, because y upstream
	# changes them very frequently and may out of sync with previous tag.
	if makefile=$(git show "$tag:Makefile" 2>/dev/null); then
		_kversion=$(sed -nE '/^VERSION\s*:?=\s*/{s///;p;q}' <<< "$makefile")
		_kpatchlevel=$(sed -nE '/^PATCHLEVEL\s*:?=\s*/{s///;p;q}' <<< "$makefile")
		_ksublevel=$(sed -nE '/^SUBLEVEL\s*:?=\s*/{s///;p;q}' <<< "$makefile")
	fi

	if rel=$(KVERSION=$_kversion KPATCHLEVEL=$_kpatchlevel KSUBLEVEL=$_ksublevel _do_strip_kernel_majver "$tag"); then
		echo "$rel"
		return 0
	fi

	return 1
}

# Get release info from git tag
_get_rel_info_from_tag() {
	local tag=$1 rel

	if ! rel=$(_check_strip_kernel_majver "$@"); then
		return 1
	fi
	rel=${rel//-/.}
	rel=${rel#.}

	# If KERNEL_DIST is added as prefix/semi-prefix/suffix, remove it from rel
	if [[ $KERNEL_DIST ]]; then
		case $rel in
			$KERNEL_DIST.*)
				rel=${rel#$KERNEL_DIST.}
				;;
			$KEXTRAVERSION.$KERNEL_DIST.*)
				rel=${rel#$KEXTRAVERSION.$KERNEL_DIST.}
				rel=$KEXTRAVERSION.$rel
				;;
			*.$KERNEL_DIST)
				rel=${rel%.$KERNEL_DIST}
				;;
		esac
	fi

	# If KEXTRAVERSION is added, remove it
	if [[ -z "$KEXTRAVERSION" ]]; then
		# If previous KEXTRAVERSION is not empty but now empty,
		# still consider it a valid release tag since release candidate mark may get dropped.
		# But this really should look at the Makefile corresponding to that tag commit
		:
	elif _is_num "$KEXTRAVERSION"; then
		case $rel in
			# Extra version is release number, remove it and add later
			$KEXTRAVERSION | "$KEXTRAVERSION."* )
				rel=${rel#$KEXTRAVERSION}
				rel=${rel#.}
				;;
			* ) return 1; ;;
		esac
	else
		# Remove RC liked tag, append them as suffix later.
		case $rel in
			# Plain version tag, eg. 5.17-rc3
			$KEXTRAVERSION )
				rel=""
				;;
			# Plain version tag plus suffix, eg. 5.17-rc3.*
			"$KEXTRAVERSION."* )
				rel=${rel#$KEXTRAVERSION.}
				;;
			# Already appended as , eg 5.17-1.rc3*
			*".$KEXTRAVERSION" )
				rel=${rel%.$KEXTRAVERSION}
				;;
			* ) return 1; ;;
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
# We try to parse and verify RPM NVR (Name, Version, Release) info's 'VR' part using git tag or commit info
# N: is always kernel
# V: is kernel's major release version (eg. 5.18, 5.18.0, 5.17.2)
# R: is a tokens seperated with '.' (eg 1[.KDIST], 2[.KDIST], 2.1[.KDIST], 0.rc1[.KDIST])
#    could also be 0.YYYYMMDDgit<commit> for snapshot release.
#    But ideally all git tag are for formal release so snapshot tag shouldn't appear in repo.
#
# With a tag that contains valid VR info it's considered a tag release, else it's a snapshot release.
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
	if [[ $KROLLING ]] && _first_merge_window_detection "$@"; then
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
		warn "Latest git tag '$last_tag' is not a release tag, it does't match Makefile version '$KVERSION.$KPATCHLEVEL.$KSUBLEVEL-$KEXTRAVERSION'"
		if release_tag=$(_search_for_release_tag "$last_tag" "$repo"); then
			warn "Found release tag '$release_tag'."
		fi
	fi

	if [[ "$release_tag" ]]; then
		git_desc=$(git -C "$repo" describe --tags --abbrev=12 "$gitref" 2>/dev/null)
		release_info=$(_get_rel_info_from_tag "$release_tag")
		release_info_raw=$(_check_strip_kernel_majver "$release_tag")

		if ! [[ $release_info ]]; then
			warn "No extra release info in release tag, might be a upstream tag." \
				"Please make a release commit with 'make dist-release' for a formal release."
			release_info=0
			KGIT_RELEASE_NUM=0
			KGIT_SUB_RELEASE_NUM=0
		elif [[ $release_info == 0.* ]]; then
			KGIT_RELEASE_NUM=${release_info##*.}
			KGIT_SUB_RELEASE_NUM=${release_info%$KGIT_RELEASE_NUM}
			KGIT_SUB_RELEASE_NUM=${KGIT_SUB_RELEASE_NUM%.}
			KGIT_SUB_RELEASE_NUM=${KGIT_SUB_RELEASE_NUM##*.}
		else
			KGIT_RELEASE_NUM=${release_info%%.*}
			KGIT_SUB_RELEASE_NUM=${release_info#$KGIT_RELEASE_NUM}
			KGIT_SUB_RELEASE_NUM=${KGIT_SUB_RELEASE_NUM#.}
			KGIT_SUB_RELEASE_NUM=${KGIT_SUB_RELEASE_NUM%%.*}
		fi

		KERNEL_PREV_RELREASE_TAG=$release_tag
		KGIT_LATEST_TAG_RELEASE_INFO=$release_info

		if [[ "$tagged" -eq 1 ]] && [[ "$release_tag" == "$last_tag" ]]; then
			if [[ $release_info ]]; then
				# This commit is tagged and it's a valid release tag, juse use it
				if [[ "$KGIT_RELEASE_NUM" != '0' ]]; then
					KGIT_TAG_RELEASE_INFO=$release_info
					KGIT_TAG_RELEASE_INFO_RAW=$release_info_raw
					KTAGRELEASE=$release_tag
				else
					warn "'$release_tag' is not a formal release tag, using snapshot versioning."
					KGIT_SNAPSHOT=1
				fi
			else
				# Tagged but no release info from current tag, could be upstream style tag
				KGIT_TAG_RELEASE_INFO=1
				# It's not a valid tag
				KTAGRELEASE=
			fi

			# If current tag is release tag, previous release tag should be another one
			KERNEL_PREV_RELREASE_TAG=$(_search_for_release_tag ${release_tag}^ "$repo")

		elif [[ "$last_tag" == "$git_desc" ]]; then
			# It's tagged, but the tag is not a release tag
			# A dumb assumption here, if it's not in *.* format (v5.4, 4.12.0, etc...) it's a test tag
			if [[ $last_tag != v*.* ]] && [[ $last_tag != *.*.* ]]; then
				warn "'$last_tag' looks like a test tag, using it as versioning suffix."
				warn "Please tag properly for release build, now versioning it as a test build."
				KGIT_TESTBUILD_TAG=test.$last_tag
				KGIT_TESTBUILD_TAG=${KGIT_TESTBUILD_TAG//-/_}
				KTESTRELEASE=1
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
	else
		# No tag or no repo info available, use snapshot version
		KSNAPRELEASE=1
		KGIT_RELEASE_NUM=0
		KGIT_SUB_RELEASE_NUM=0
	fi

	# Fix release numbers, if it's not a number
	if ! _is_num "$KGIT_RELEASE_NUM"; then
		warn "Unrecognizable release number: $KGIT_RELEASE_NUM, resetting to 0"
		KGIT_RELEASE_NUM=0
	fi
	if ! _is_num "$KGIT_SUB_RELEASE_NUM"; then
		KGIT_SUB_RELEASE_NUM=0
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
	if [[ $KPREMERGEWINDOW ]]; then
		if [[ ! $KTAGRELEASE ]]; then
			KPATCHLEVEL=$(( KPATCHLEVEL + 1 ))
			KEXTRAVERSION="rc0"
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
	local gitref=$1 localversion=$2
	local krelease

	case $localversion in
		+* | '' ) localversion=${localversion#+} ;;
		*) die "Unexpected LOCALVERSION '$localversion', run dist-check-configs for more info."
	esac

	_prepare_kernel_ver "$gitref"
	if [[ $KSNAPRELEASE ]]; then
		# For snpashot version, it should start with 0. and
		# KEXTRAVERSION will be appended at the tail of git info.
		krelease=0.$KGIT_SNAPSHOT
		[[ ${KEXTRAVERSION:-0} != "0" ]] && krelease=$krelease.$KEXTRAVERSION
		# Release numbers will be appended too if available as a version hint for users.
		[[ ${KGIT_RELEASE_NUM:-0} != "0" ]] && krelease=$krelease.$KGIT_RELEASE_NUM
		[[ ${KGIT_SUB_RELEASE_NUM:-0} != "0" ]] && krelease=$krelease.$KGIT_SUB_RELEASE_NUM
	elif [[ $KTESTRELEASE ]]; then
		# For test tag, use the most recent release tag we can find and
		# append the test suffix.
		krelease=0
		[[ ${KEXTRAVERSION:-0} != "0" ]] && krelease=$krelease.$KEXTRAVERSION
		[[ $KGIT_LATEST_TAG_RELEASE_INFO ]] && krelease=$krelease.$KGIT_LATEST_TAG_RELEASE_INFO.$KGIT_TESTBUILD_TAG
	else
		if [[ $KTAGRELEASE ]]; then
			# If the git tag matches all release info, respect it.
			krelease=$KGIT_TAG_RELEASE_INFO
		else
			# Upstream or unknown, set release to start with "0."
			# so it can be updated easily later.
			# And if it's a rc release, use "0.0" to ensure it have
			# lower priority.
			if [[ "$KRCRELEASE" ]]; then
				krelease=0.0
			else
				krelease=0.1
			fi
		fi

		# If KEXTRAVERSION is not number it will break the release syntax
		# if added as prefix, add as suffix in such case
		if [[ $KEXTRAVERSION ]]; then
			if _is_num "${KEXTRAVERSION%%.*}"; then
				krelease="$KEXTRAVERSION.$krelease"
			else
				krelease="$krelease.$KEXTRAVERSION"
			fi
		fi
	fi

	KERNEL_NAME="kernel${KDIST:+-$KDIST}"
	KERNEL_MAJVER="$KVERSION.$KPATCHLEVEL.$KSUBLEVEL"
	KERNEL_RELVER="$krelease"
	KERNEL_UNAMER="$KERNEL_MAJVER-$KERNEL_RELVER${KERNEL_DIST:+.$KERNEL_DIST}"

	if [[ $KFORCEUNAMER ]] && [[ $KGIT_TAG_RELEASE_INFO_RAW ]]; then
		KERNEL_UNAMER="$KERNEL_MAJVER$KGIT_TAG_RELEASE_INFO_RAW"
	fi

	if [[ $localversion ]]; then
		KERNEL_NAME="$KERNEL_NAME-$localversion"
		KERNEL_UNAMER="$KERNEL_UNAMER+$localversion"
	fi
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

	# TK4 left-pads the release number with 0
	KGIT_RELEASE_NUM=$(echo "$KGIT_RELEASE_NUM" | sed 's/^0*//')
	krelease=$((KGIT_RELEASE_NUM + 1))

	if [[ $KEXTRAVERSION ]]; then
		if _is_num "${KEXTRAVERSION%%.*}"; then
			krelease="$KEXTRAVERSION.$krelease"
		else
			krelease="$krelease.$KEXTRAVERSION"
		fi
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

	KGIT_RELEASE_NUM=$(echo "$KGIT_RELEASE_NUM" | sed 's/^0*//')
	krelease=$KGIT_RELEASE_NUM
	krelease=$krelease.$((KGIT_SUB_RELEASE_NUM + 1))

	if [[ $KEXTRAVERSION ]]; then
		if _is_num "${KEXTRAVERSION%%.*}"; then
			krelease="$KEXTRAVERSION.$krelease"
		else
			krelease="$krelease.$KEXTRAVERSION"
		fi
	fi

	KERNEL_MAJVER="$KVERSION.$KPATCHLEVEL.$KSUBLEVEL"
	KERNEL_RELVER="$krelease"
	KERNEL_UNAMER="$KERNEL_MAJVER-$KERNEL_RELVER${KDIST:+.$KDIST}"
}
