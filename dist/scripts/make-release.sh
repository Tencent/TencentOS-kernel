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
	"")
		prepare_next_kernel_ver "$COMMIT" "$TOPDIR" || die "Failed preparing next release version info"
		;;
	*)
		die "Invalid param $1"
		;;
esac
AUTHOR_NAME=$(git config user.name) || die "Failed getting author name info from git config"
AUTHOR_MAIL=$(git config user.email) || die "Failed getting author email info from git config"
GITLOG=$(git -C "$TOPDIR" log "$KERNEL_PREV_RELREASE_TAG..$COMMIT" --pretty=oneline) || die "Failed getting changelog from git log"

AUTHOR="$AUTHOR_NAME <$AUTHOR_MAIL>"
RELEASE_VERSION="$KERNEL_MAJVER-$KERNEL_RELVER"
TAG_VERSION="release-$KERNEL_UNAMER"
CHANGELOG="* $(date +"%a %b %e %Y") $AUTHOR - $RELEASE_VERSION
$(echo "$GITLOG" | sed -E "s/^\S+/-/g")"

print_info() {
	cat << EOF
Please review following info:
\* DO NOT CHANGE THE FILE FORMAT \*

Tag: $TAG_VERSION
Release Version: $RELEASE_VERSION
Release Author: $AUTHOR
Changelog:
$CHANGELOG
EOF
}

dump_info() {
	print_info > "$DISTDIR/.release.stash"
}

update_info() {
	${EDITOR:-vi} "$DISTDIR/.release.stash" || die "Failed to call editor to edit the release info"
}

parse_info() {
	TAG_VERSION=$(sed -E -ne "s/^Tag:\s*(.*)/\1/p" "$DISTDIR/.release.stash")
	RELEASE_VERSION=$(sed -E -ne "s/\s*Release Version:\s*(.*)/\1/p" "$DISTDIR/.release.stash")
	AUTHOR=$(sed -E -ne "s/^Release Author:\s*(.*)/\1/p" "$DISTDIR/.release.stash")
	CHANGELOG=$(sed -n '/^* /,$p' "$DISTDIR/.release.stash")
}

while :; do
	_res="?"
	while :; do
		{
			print_info
			echo
			echo "(Press 'q' to exit preview.)"
		} | less
		echo "Is this OK? (y/n/q/e, Y: Do the release, N/Q: quit, E: edit)"
		read -r -n1 _res
		case $_res in
			n|N|q|Q )
				exit 0
				;;
			y|Y )
				echo "$CHANGELOG" >> "$DISTDIR/templates/changelog.new"
				echo "" >> "$DISTDIR/templates/changelog.new"
				cat "$DISTDIR/templates/changelog" >> "$DISTDIR/templates/changelog.new"
				mv "$DISTDIR/templates/changelog.new" "$DISTDIR/templates/changelog"
				git -C "$TOPDIR" add "$DISTPATH/templates/changelog"
				git -C "$TOPDIR" commit -m "$DISTPATH: release $RELEASE_VERSION

Upstream: no

Signed-off-by: $AUTHOR"
				git -C "$TOPDIR" tag "$TAG_VERSION"
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
