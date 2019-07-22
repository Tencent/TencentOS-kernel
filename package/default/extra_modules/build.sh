#! /bin/bash

src_dir=`pwd`
mod_dir="$( cd "$( dirname "$0"  )" && pwd  )"
mod_list=""

usage()
{
	echo "usage:"
	echo "	mod_build.sh [ prep | build | install ] args"
	exit 0
}

get_modlist()
{
	all_lines=`cat $mod_dir/list`
	IFS="
	"
	for line in $all_lines
	do
		mod=`echo $line | awk '{gsub(/^\s+|\s+$/, "");print}'`
		mod=${mod%%#*}
		if [ "$mod" = "" ] ; then
			continue
		fi
		mod_list="$mod_list $mod"
	done
	IFS=" "
}

do_prep()
{
	for mod in $mod_list
	do
		echo "prep $mod"
		tar xzf $mod_dir/$mod
	done
}

do_build()
{
	local objdir=$1

	for mod in $mod_list
	do
		echo "build $mod"
		name=${mod%.tar.gz}
		echo "make -C $objdir RPM_BUILD_MODULE=y M=../$name KERNELRELEASE=$tag_name KDIR=$src_dir modules"
		make -C $objdir RPM_BUILD_MODULE=y M=../$name KERNELRELEASE=$tag_name KDIR=$src_dir modules
	done
}

do_install()
{
	local objdir=$1
	local install_dir=$2

	for mod in $mod_list
	do
		echo "build $mod"
		name=${mod%.tar.gz}
		echo "make -C $objdir RPM_BUILD_MODULE=y INSTALL_MOD_PATH=$install_dir M=../$name KERNELRELEASE=$tag_name KDIR=$src_dir modules_install"
		make -C $objdir RPM_BUILD_MODULE=y INSTALL_MOD_PATH=$install_dir M=../$name KERNELRELEASE=$tag_name KDIR=$src_dir modules_install
	done
}

#main function
cmd=$1
tag_name=$2
get_modlist

if [ "$cmd" = "prep" ] ; then
	do_prep
elif [ "$cmd" = "build" ] ; then
	do_build $3
elif [ "$cmd" = "install" ] ; then
	do_install $3 $4
else
	usage
fi

cd $src_dir
exit 0
