#!/bin/bash --norc
# SPDX-License-Identifier: GPL-2.0

# shellcheck source=../lib.sh
. "/$(dirname "$(realpath "$0")")/../lib.sh"

AUTHOR="$(git config user.name) <$(git config user.email)>"
COMMIT=$1

[ -z "$COMMIT" ] && die "Usage: $0 <upstream commit id>"

_resolve_conflict_shell() {
	error "Failed to backport, please fix the problem, then commit it."
	error "then exit the shell with \`exit 0\`."
	error "To abort the backport, exited with a dirty worktree or exit with non-zero."

	export PS1="RESOLVING CONFLICT"

	$SHELL || {
		RET=$?
		error "Shell exited with non-zero, aborting backport"
		git reset --hard HEAD
		exit $RET
	}

	git diff-index --quiet HEAD || {
		RET=$?
		error "Dirty worktree, aborting backport"
		git reset --hard HEAD
		exit $RET
	}
}

_fetch_upstream() {
	# Try to fetch from well-known upstream
	local commit=$1
	local upstream_repos=(
		"torvalds https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/patch/?id=<> git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git"
		"tip https://git.kernel.org/pub/scm/linux/kernel/git/tip/tip.git/patch/?id=<> git://git.kernel.org/pub/scm/linux/kernel/git/tip/tip.git"
		"mm https://git.kernel.org/pub/scm/linux/kernel/git/akpm/mm.git/patch/?id=<> git://git.kernel.org/pub/scm/linux/kernel/git/akpm/mm.git"
		"stable https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/patch/?id=<> git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git"
	)

	for repo in "${upstream_repos[@]}"; do
		repo_http_git=${repo#* }
		repo_name=${repo%% *}
		repo_http=${repo_http_git% *}
		repo_git=${repo_http_git#* }
		commit_url=${repo_http/<>/$commit}
		echo curl --fail --silent --output /dev/null "$commit_url"
		if curl --fail --silent --output /dev/null "$commit_url"; then
			echo "Found commit '$commit' in '$repo_name' repo"
			echo git fetch "$repo_git" "$commit"
			git fetch "$repo_git" "$commit"
			return
		fi
	done

	echo "Can't find commit $commit in upstream."
	return 1
}

if ! git cat-file -e "$COMMIT"; then
	# Available locally, just cherry-pick
	_fetch_upstream "$COMMIT"
fi

if ! git cherry-pick "$COMMIT"; then
	_resolve_conflict_shell
	CONFLICT=Resolved
else
	CONFLICT=None
fi

git commit \
	--am \
	--author="$AUTHOR" \
	--date=now \
	--message \
	"$(git log -1 --format=%s "$COMMIT")

$MSG

Upstream commit: $COMMIT
Conflict: $CONFLICT

Backport of following upstream commit:

$(git log -1 "$COMMIT")

Signed-off-by: $AUTHOR"
