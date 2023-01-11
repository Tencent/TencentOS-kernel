#!/bin/bash --norc
#
# Print out the tkernel version based on git commit and work tree.
#
# shellcheck source=./lib-version.sh
. "$(dirname "$(realpath "$0")")/lib-version.sh"

COMMIT=HEAD
case $1 in
	--sub-release )
		prepare_next_sub_kernel_ver "$COMMIT" "$TOPDIR" || die "Failed preparing next sub release version info"
		;;
	--maj-release)
		prepare_next_kernel_ver "$COMMIT" "$TOPDIR" || die "Failed preparing next release version info"
		;;
	*)
		die "Invalid param $1, usage $0 {--maj-release|--sub-release}"
		;;
esac
AUTHOR_NAME=$(git config user.name) || die "Failed getting author name info from git config"
AUTHOR_MAIL=$(git config user.email) || die "Failed getting author email info from git config"
GITLOG=$(git -C "$TOPDIR" log "$KERNEL_PREV_RELREASE_TAG..$COMMIT" --pretty=oneline) || die "Failed getting changelog from git log"

if [[ "$KTAGRELEASE" ]]; then
	warn "You are generating changelog from a tagged release commit, however changelog update"
	warn "should be done before tagging a release, please be careful with what you are doing or fix your workflow."
	prepare_kernel_ver "$COMMIT"
fi

if [[ -z "$GITLOG" ]]; then
	error "No change found since last tag, using dummy changelog."
	GITLOG="- Accumulated bug fix and improvements."
fi

AUTHOR="$AUTHOR_NAME <$AUTHOR_MAIL>"
RELEASE_VERSION="$KERNEL_MAJVER-$KERNEL_RELVER"
TAG_VERSION="$KERNEL_UNAMER"
CHANGELOG_HDR="* $(date +"%a %b %e %Y") $AUTHOR - $RELEASE_VERSION"
CHANGELOG="$(echo "$GITLOG" | sed -E "s/^\S+/-/g")"

print_preview() {
	cat << EOF
Please review following info:
Tag: ${TAG_VERSION:-<skipped>}
Release Version: $RELEASE_VERSION
Release Author: $AUTHOR
Changelog:
$CHANGELOG_HDR
$CHANGELOG
EOF
}

print_info() {
	cat << EOF
Please review following info:
!!! DO NOT CHANGE THE FILE FORMAT !!!

// You can set "Tag:" to empty to skip tagging.
// but it's strongly recommended to tag after changlog update, to make versioning more consistent.
Tag: $TAG_VERSION
Release Version: $RELEASE_VERSION
Release Author: $AUTHOR
Changelog:
* $(date +"%a %b %e %Y") <Author> - <Release Version>"
$CHANGELOG
EOF
}

dump_info() {
	print_info > "$DISTDIR/.release.stash"
}

update_info() {
	${EDITOR:-vi} "$DISTDIR/.release.stash" >/dev/tty
	[[ $? -eq 0 ]] || die "Failed to call editor to edit the release info"
}

parse_info() {
	TAG_VERSION=$(sed -E -ne "s/^Tag:\s*(.*)/\1/p" "$DISTDIR/.release.stash")
	RELEASE_VERSION=$(sed -E -ne "s/\s*Release Version:\s*(.*)/\1/p" "$DISTDIR/.release.stash")
	AUTHOR=$(sed -E -ne "s/^Release Author:\s*(.*)/\1/p" "$DISTDIR/.release.stash")
	CHANGELOG=$(sed -n '/^* /,$p' "$DISTDIR/.release.stash" | tail -n +2)
	CHANGELOG_HDR="* $(date +"%a %b %e %Y") $AUTHOR - $RELEASE_VERSION"
}

while :; do
	_res="?"
	while :; do
		{
			print_preview
			echo
			echo "(Press 'q' to exit preview, Press 'e' to edit above info, Press 'y' to commit.)"
		} | less
		echo "Is this OK? (y/n/q/e, Y: Do the release, N/Q: quit, E: edit)"
		read -r -n1 _res
		case $_res in
			n|N|q|Q )
				exit 0
				;;
			y|Y )
				info "Updating spec changelog and tagging HEAD... "
				echo "$CHANGELOG_HDR" >> "$DISTDIR/templates/changelog.new"
				echo "$CHANGELOG" >> "$DISTDIR/templates/changelog.new"
				echo "" >> "$DISTDIR/templates/changelog.new"
				cat "$DISTDIR/templates/changelog" >> "$DISTDIR/templates/changelog.new"
				mv "$DISTDIR/templates/changelog.new" "$DISTDIR/templates/changelog"
				git -C "$TOPDIR" add "$DISTPATH/templates/changelog"
				git -C "$TOPDIR" commit -m "$DISTPATH: release $RELEASE_VERSION

Upstream: no

Signed-off-by: $AUTHOR"
				if [[ $TAG_VERSION ]]; then
					if ! git -C "$TOPDIR" tag "$TAG_VERSION"; then
						error "Failed to tag '$TAG_VERSION', this tag may already exists."
						error "Changelog update should be done before tagging a release, so you may either use dist-new-release to tag, or fix the tag later manually."
					fi
				else
					warn "Please ensure a tag corresponding to '$RELEASE_VERSION' is added to repo to make changelog consistent."
				fi

				exit 0
				;;
			e|E )
				dump_info
				update_info
				parse_info
				;;
		esac
	done
done
