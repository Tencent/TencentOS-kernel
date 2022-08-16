#!/bin/bash --norc
# SPDX-License-Identifier: GPL-2.0
#
# Check kernel commit messages

# shellcheck source=./lib.sh
. "$(dirname "$(realpath "$0")")/lib.sh"

usage()
{
	cat << EOF
check-commit-msg.sh <commit>...

	--extra-author		Who is triggering this check, check for corresponding signed-off-by in commit body
				For example, if this script is triggered by a Merge Request, the one who submitted
				the Merge Request should also sign all commits within the MR.

				Can be also enabled by setting EXTRA_AUTHOR=<author> in environmental variable.

	--gen-report		Generate error report under corrent directory for each commit being checked
				in current directory.

				Can be also enabled by setting GEN_REPORT=1 in environmental variable.

	--output-dir <DIR>	Specify where the report should be put if '--gen-report' is used
				 An directory path is expected.

				Defaults to current directory.

	--vendor-only		Author should be limited to vendor only.
				(Git commit email address must end with @$VENDOR.com).
EOF
}

if ! [[ -x "$TOPDIR/scripts/checkpatch.pl" ]]; then
	die "This command requires scripts/checkpatch.pl"
fi

COMMITS=()
HEAD_COMMIT=
OUTPUT_DIR=$(pwd)
VENDOR_ONLY=
while [[ $# -gt 0 ]]; do
	case $1 in
		--extra-author )
			EXTRA_AUTHOR=$2
			shift 2
			;;
		--output-dir )
			OUTPUT_DIR=$2
			shift 2
			;;
		--gen-report )
			GEN_REPORT=1
			shift
			;;
		--vendor-only )
			VENDOR_ONLY=1
			shift
			;;
		-* )
			usage
			exit 1
			;;
		* )
			COMMITS+=( "$1" )
			shift
	esac
done

if [[ -z "${COMMITS[*]}" ]]; then
	echo "Checking all commits that are ahead of remote tracking branch."

	if ! UPSTREAM_BASE=$(git -C "$repo" merge-base HEAD "@{u}" 2>/dev/null); then
		warn "Can't find a valid upstream, will only check HEAD commit."
		COMMITS=HEAD
	else
		if [[ $(git rev-list "$UPSTREAM_BASE..HEAD" --count) -eq 0 ]]; then
			COMMITS="HEAD"
		else
			COMMITS="$UPSTREAM_BASE..HEAD"
		fi
	fi
fi

is_valid_commit() {
	case $1 in
		*[!0-9A-Fa-f]* | "" )
			return 1
			;;
		* )
			if [ "${#1}" -lt 12 ] || [ "${#1}" -gt 41 ]; then
				return 1
			fi
			;;
	esac

	return 0
}

# Check if a commit id is valid upstream commit id
# $1: commit id to check
is_valid_upstream_commit() {
	local commit=$1
	local upstream_repo=(
		"torvals https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/patch/?id=<>"
		"stable https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/patch/?id=<>"
		"tip https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/patch/?id=<>"
	)
	local repo repo_name repo_url commit_url

	for repo in "${upstream_repo[@]}"; do
		repo_name=${repo%% *}
		repo_url=${repo#* }
		commit_url=${repo_url/<>/$commit}
		if curl --fail --silent --output /dev/null "$commit_url"; then
			echo "$commit_url"
			return 0
		fi
	done

	return 1
}

# TODO(katinzhou): 需要测试。且能支持本地调用检查commit msg是否合规
# Check a commit for all potential issues
# returns number of issues found
check_commit() {
	local ret=0
	local commit=$1
	local commit_msg=$2

	local author=$(git log --pretty=format:"%an" "$commit" -1)
	local author_email=$(git log --pretty=format:"%ae" "$commit" -1)
	local committer=$(git log --pretty=format:"%cn" "$commit" -1)
	local committer_email=$(git log --pretty=format:"%ce" "$commit" -1)
	local commit_body=$(git log --pretty=format:"%b" "$commit" -1)
	local commit_summary=$(git log --pretty=format:"%s" "$commit" -1)

	# Ignore merge commit
	if [ "$(git show --no-patch --format="%P" "$commit" | wc -w)" -ne 1 ]; then
		echo "Found merge commit $commit"
		echo "NOTICE: This checking routine only follows one parent commit."
		return 0
	fi

	local ignore_line="ERROR: Please use git commit description style 'commit <12+ chars of sha1> (\"<title line>\")'"

	local patch_content
	if ! patch_content=$(git format-patch --stdout -1 "$commit"); then
		echo "ERROR: FATAL: Failed to format $commit into a patch"
		echo
		return 1
	fi

	if ! echo "$commit_body" | grep -iq "^checkpatch:.*\bno\b"; then
		local patch_err
		patch_err=$(echo "$patch_content" | "$TOPDIR/scripts/checkpatch.pl" --terse | grep -v "$ignore_line" | grep "ERROR: ")

		if [[ -n "$patch_err" ]]; then
			echo "ERROR: scripts/checkpatch.pl reported following error:"
			echo "    $patch_err"
			echo
			ret=$(( ret + 1 ))
		fi
	fi

	if ! echo "$commit_body" | grep -q "Signed-off-by: .*\b$author\b"; then
		echo "ERROR: Commit author ($author) need to sign the commit, so a Signed-off-by: <$author's sign> is needed"
		echo
		ret=$(( ret + 1 ))
	fi

	if [[ $EXTRA_AUTHOR ]]; then
		if ! echo "$commit_body" | grep -q "Signed-off-by: .*\b$EXTRA_AUTHOR\b"; then
			echo "ERROR: Commit co-author (eg. MR author) need to sign the commit, so a Signed-off-by: <$EXTRA_AUTHOR's sign> is needed"
			echo
			ret=$(( ret + 1 ))
		fi
	fi

	upstream_commit=$(echo "$commit_body" | sed -nE "s/^(Upstream commit:|Upstream: commit) (\S+)/\2/pg")
	upstream_status=$(echo "$commit_body" | sed -nE "s/^(Upstream status:|Upstream:) (\S+)/\2/pg")
	commits_cnt=$(echo "$upstream_commit" | wc -w)

	if [[ $commit_summary == $VENDOR:* ]]; then
		if [[ -z "$upstream_status" ]]; then
			echo "ERROR: It seems this is a downstream commit."
			echo "    Please add following mark in the commit message:"
			echo '    ```'
			echo '    Upstream status: <no/pending/downstream-only...>'
			echo '    ```'
			echo "    to explain this is a downstream commit."
			echo
			ret=$(( ret + 1 ))
		fi

		if [[ $commits_cnt -ne 0 ]]; then
			echo "ERROR: It seems this is a downstream commit, but a Upstream commit mark is provided."
			echo
			ret=$(( ret + 1 ))
		fi
	fi

	# This is an upstream commit
	if [[ $commits_cnt -eq 0 ]]; then
		if [[ $VENDOR_ONLY ]] && [[ $commit_summary != $VENDOR:* ]]; then
			echo "ERROR: '$VENDOR:' is missing in the commit summary, and no 'Upstream commit:' mark is found."
			echo "    If this is a downstream commit, please add '$VENDOR': in commit summary, and add this mark in commit message:"
			echo
			ret=$(( ret + 1 ))
		fi

		if [[ -z "$upstream_status" ]]; then
			echo "ERROR: No upstream mark found."
			echo "    If this is a downstream commit, please add this mark:"
			echo '    ```'
			echo '    Upstream status: <no/pending/downstream-only...>'
			echo '    ```'
			echo "    to explain this is a downstream commit."
			echo
			echo "    If this is a backported upstream commit, please add one and only one mark:"
			echo '    ```'
			echo "    Upstream commit: <commit id>"
			echo '    ```'
			echo "    to indicate which commit is being backported."
			echo
			ret=$(( ret + 1 ))
		elif [[ "$upstream_status" =~ ^[a-f0-9]{7,}$ ]]; then
			echo "ERROR: It seems you pasted a plain commit id after 'Upstream:' or 'Upstream status:' mark."
			echo "    Please use this format instead:"
			echo '    ```'
			echo "    Upstream commit: <commit id>"
			echo '    ```'
			echo "    If this is a actually downstream commit, please use less confusing words"
			echo "    for the 'Upstream status:' mark."
			echo
			ret=$(( ret + 1 ))
		fi
	else
		if [[ $commits_cnt -ne 1 ]]; then
			echo "ERROR: It seems this is a upstream commit, please add one *and only one* upstream commit indicator per commit, in following format:"
			echo '    ```'
			echo "    Upstream commit: <commit id>"
			echo '    ```'
			echo "    If this is a downstream commit, please add \"$VENDOR:\" header in commit summary."
			echo
			ret=$(( ret + 1 ))
		else
			if ! is_valid_commit "$upstream_commit"; then
				echo "ERROR: $upstream_commit is not an valid commit!"
				echo
				ret=$(( ret + 1 ))
			else
				if ! is_valid_upstream_commit "$upstream_commit" > /dev/null; then
					echo "ERROR: $upstream_commit is not an valid upstream commit!"
					echo
					ret=$(( ret + 1 ))
				fi
			fi
		fi

		conflict_cnt=$(echo "$commit_body" | grep -c "^Conflict: ")
		if [ "$conflict_cnt" -ne 1 ]; then
			echo "ERROR: This is an upstream commit, please add one (and only one) conflict indicator in following format:"
			echo '    ```'
			echo "        Conflict: <none/refactored/minor/resolved...>"
			echo '    ```'
			echo
			ret=$(( ret + 1 ))
		fi
	fi

	if [[ $VENDOR_ONLY ]] && [[ $author_email != *@$VENDOR.com ]]; then
		echo "ERROR: The author of this commit is not from $VENDOR.com, did you forget to reset author?"
		echo "       You can reset author to your self for better backports commits tracking, use:"
		echo "       > git commit --amend --reset-author"
		echo "       Or:"
		echo "       > git commit --amend --author=\"yourname <yourname@$VENDOR.com>\""
		echo "       If there are many commits to be modified, use:"
		echo "       > git filter-branch."
		echo
		ret=$(( ret + 1 ))
	fi

	return $ret
}

if ! git_logs=$(git log --no-walk=sorted --no-decorate --pretty=oneline --first-parent "${COMMITS[*]}"); then
	die "Failed to parse git references '${COMMITS[*]}'"
	exit 1
fi

IDX=0
ERROR_MSG=""
echo "$git_logs" | while read -r commit_id commit_msg; do
	echo "=== Checking $commit_id ('$commit_msg') ..."

	IDX=$(( IDX + 1 ))
	ERROR_MSG=$(check_commit "$commit_id")
	RET=$?
	OUTFILE=$(printf "$OUTPUT_DIR/%04d-%s.err" $IDX "$commit_id")

	if [[ $RET -ne 0 ]]; then
		echo_yellow "Found following issues with $commit_id ('$commit_msg'):"
		echo_red "$ERROR_MSG"
		if [[ "$GEN_REPORT" ]] && [[ "$GEN_REPORT" != "0" ]]; then
			{
				echo "$ERROR_MSG"
			}	> "$OUTFILE"
		fi
	else
		if [[ -n "$ERROR_MSG" ]]; then
			echo_yellow "$commit_id have some warnings:"
			echo_red "$ERROR_MSG"
		else
			echo_green "$commit_id looks OK"
		fi
	fi

	# Newline as seperator
	echo
done
