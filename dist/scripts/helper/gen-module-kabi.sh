#!/usr/bin/env sh
#

KERNEL=
SYMVER=
TMPDIR=

error() {
	echo "error: $*" > /dev/stderr
}

usage() {
	cat << EOF
Provide a list of modules, this script will print the SYMBOLS used by these modules

gen-module-kabi.sh: [--kernel <kernel name>] [--symver <symver file>] [<module name or file>, ...]

<kernel name>: default to \$(uname -r), or specify a installed kernel
<symver file>: default to /usr/lib/<kernel name>/symvers.gz, or specify a symver file
<module>: module name
EOF
}

while true; do
	case $1 in
		--symver )
			SYMVER=$2
			shift 2
			;;
		--kernel )
			KERNEL=$2
			shift 2
			;;
		--usage|--help|-h )
			usage
			exit 0
			;;
		* )
			break
			;;
	esac
done

if ! TMPDIR="$(mktemp -d -t gen-module-kabi.XXXXXX)"; then
	error "mktemp failed"
	exit 1
fi

trap 'ret=$?;
[[ -d $TMPDIR ]] && rm --one-file-system -rf -- "$TMPDIR";
exit $ret;
' EXIT

[ "$KERNEL" ] || KERNEL=$(uname -r)
[ -f "$SYMVER" ] || SYMVER=/lib/modules/$KERNEL/symvers.gz
[ -f "$SYMVER" ] || SYMVER=/boot/symvers-$KERNEL.gz
[ -f "$SYMVER" ] || SYMVER=./Module.symvers

if [[ "$#" -eq 0 ]]; then
	usage
fi

case $SYMVER in *.gz )
	gzip < "$SYMVER" -d > "$TMPDIR/symver"
	SYMVER=$TMPDIR/symver
	;;
esac

_get_export_syms() {
	_temp_extract=$TMPDIR/extracted.ko
	case $1 in
		*.ko )
			modfile=$1
			;;
		*.xz )
			modfile=$_temp_extract
			xz -c -d "$1" > "$_temp_extract"
			;;
		* )
			error "unsupported file format of file: $1"
	esac

	nm -uAg "$modfile"
}

for mod in "$@"; do
	if [[ $mod == *.ko ]] && [[ -f $mod ]]; then
		mods=$mod
	elif [[ $mod == *.ko.* ]] && [[ -f $mod ]]; then
		mods=$mod
	elif ! mods="$(modinfo --set-version "$KERNEL" --filename "$mod" 2> /dev/null)"; then
		error "'$mod' is not a valid module name"
	fi

	for m in $mods; do
		_get_export_syms "$m"
	done
done | sort | while read -r file_path type sym_name; do
	in_symver=0
	in_vmlinux=0
	sym_line=$(grep "\s$sym_name\s" "$SYMVER") && in_symver=1
	case $sym_line in
		# print only symbol in vmlinux
		*vmlinux*EXPORT_SYMBOL* )
			in_vmlinux=1
			;;
	esac
	echo -n "$file_path	$type	$sym_name"
	if [[ $in_symver -eq 0 ]]; then
		echo -n "	(NO-KABI: NOT IN SYMVER)"
	fi
	if [[ $in_vmlinux -eq 0 ]]; then
		echo -n "	(NO-KABI: NOT IN VMLINUX)"
	fi
	echo
done
