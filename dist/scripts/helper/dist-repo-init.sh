#!/bin/bash --norc
# SPDX-License-Identifier: GPL-2.0
#
# After dropping the dist build system into a upstream kernel repo,
# use this to hook the dist build system into Kbuild Makefile.

# shellcheck source=../lib.sh
. "/$(dirname "$(realpath "$0")")/../lib.sh"

VENDOR_CAPITALIZED=$(get_dist_makefile_var VENDOR_CAPITALIZED)
KMAKEFILE_CONTENT=""

[ -d "$TOPDIR" ] || {
	die "Not a valid git repo."
}

[ -e "$TOPDIR/Kbuild" ] && [ -e "$TOPDIR/Kconfig" ] && [ -e "$TOPDIR/Makefile" ] || {
	die "Not a valid kernel repo."
}

[ -e "$DISTDIR/Makefile" ] || {
	die "Dist files are not properly configured, aborting."
}

[[ "$DISTDIR" == "$TOPDIR"/* ]] || {
	die "Dist files are not properly configured, aborting."
}

if [[ $1 == "--reset" ]]; then
	### Clean up changelog
	echo -n > "$DISTDIR"/templates/changelog

	### Clean up kABI
	for i in $SPEC_ARCH; do
		echo -n > "$DISTDIR"/kabi/Module.kabi_$i
	done

	### Clean up generic-default config
	echo -n > "$DISTDIR"/configs/00base/generic/default.config
	for i in $SPEC_ARCH; do
		echo -n > "$DISTDIR"/configs/00base/generic/$i.config
	done
fi

### Patch .gitignore
grep -qF "# ${VENDOR_CAPITALIZED:+${VENDOR_CAPITALIZED} }dist files" "$TOPDIR/.gitignore" || {
	echo "Updating Kernel .gitignore to ignore dist files..."
	cat >> "$TOPDIR/.gitignore" << EOF

# ${VENDOR_CAPITALIZED:+${VENDOR_CAPITALIZED} }dist files
/$DISTPATH/rpm
/$DISTPATH/workdir
EOF
}

### Patch .gitattributes
for i in ".ci" "$DISTPATH"; do \
	[[ -d $TOPDIR/$i ]] || continue
	grep -qF "$i/" $TOPDIR/.gitattributes 2>/dev/null || {
		echo "Updating .gitattributes to exclude $i/..."
		echo "$i/ export-ignore" >> $TOPDIR/.gitattributes
	}
done;

### Patch kernel Makefile
grep -qF "# dist_make: ${VENDOR_CAPITALIZED:+${VENDOR_CAPITALIZED} }" "$TOPDIR/Makefile" || {
	echo "Updating Kenrel Makefile..."
	KMAKEFILE_CONTENT=$(<"$TOPDIR/Makefile")
	cat > "$TOPDIR/Makefile" << EOF
# SPDX-License-Identifier: GPL-2.0

# dist_make: ${VENDOR_CAPITALIZED:+${VENDOR_CAPITALIZED} }Dist Makefile, which contains dist-* make targets
ifneq (\$(shell echo \$(MAKECMDGOALS) | grep "^dist-"),)
include $DISTPATH/Makefile
else

$KMAKEFILE_CONTENT

endif # dist_make
EOF
}
