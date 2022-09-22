#!/bin/bash
# ==============================================================================
#   Description:  Automaticly generating kernel rpm package with specified branch
#                 according to HEAD tag.
#
#        Author:  David Shan <davidshan@tencent.com>
# ===============================================================================

#variables
kernel_full_name=""
kernel_version=""
tlinux_release=""
curdir=`pwd`


build_specdir=`readlink -f ~/rpmbuild/SPECS`
build_srcdir=`readlink -f ~/rpmbuild/SOURCES`
dist="tl2"

kernel_type="default"
spec_file_name=""
make_jobs=""
tag_name=""
nosign_suffix=""
build_src_only=0
strip_sign_token=0
# to control wheter to build rpm for xen domU
isXenU=""
kernel_default_types=(default)

get_kernel_version()
{
	kernel_version=`echo $tag_name|cut -d- -f1`
	#kernel_version=${kernel_version}-1
	#echo "kernel version: $kernel_version"
	echo "kernel version: ${kernel_version}"
}

get_tlinux_name()
{
	if [ -n "$tag_name" ]; then
		tagged_name="$tag_name"
	else
		tagged_name=`git describe --tags`
	fi
	
	if [ -z "${tagged_name}" ];then
		echo "Error:Can't get kernel version from git tree."
		exit 1
	fi

	#if [ "${tagged_name#*-*-*}" != ${tagged_name} ];then
		#echo "Error: bad tag name:$tagged_name."
		#exit 1
	#fi
	
    rpm_name="kernel"
	echo "rpm_name $rpm_name"

    tlinux_release=`echo $tagged_name|cut -d- -f2`.`echo $tagged_name|cut -d- -f3`
    extra_version=`echo $tagged_name | cut -d- -f2`
    kernel_full_name=${rpm_name}-${kernel_version}
	#tlinux_release=${tagged_name#*-}
}

prepare_tlinux_spec()
{
	spec_file_name=${build_specdir}/${kernel_full_name}_${dist}.spec
	spec_contents="%define name ${rpm_name} \n"
	spec_contents+="%define version ${kernel_version} \n"
	spec_contents+="%define release_os ${tlinux_release} \n"
	spec_contents+="%define tagged_name ${tagged_name} \n"
	
	echo -e "${spec_contents}" > $spec_file_name

	if [ "$kvm_guest" = "yes" ] ; then
		cat $curdir/kernel-tlinux_kvmguest_${dist}.spec >> $spec_file_name
	else
		cat $curdir/kernel-tlinux_${dist}.spec >> $spec_file_name
		#cp $curdir/${kernel_full_name}_${dist}.spec $spec_file_name
	fi
}

prepare_tlinux_bin_sources()
{
	local kernel_tl3_rpm=${rpm_name}-${kernel_version}-${tlinux_release}.tl3.x86_64.rpm
	local kernel_debuginfo_tl3_rpm=${rpm_name}-debuginfo-common-${kernel_version}-${tlinux_release}.tl3.x86_64.rpm
	local kernel_headers_tl3_rpm=${rpm_name}-headers-${kernel_version}-${tlinux_release}.tl3.x86_64.rpm
	local kernel_devel_tl3_rpm=${rpm_name}-devel-${kernel_version}-${tlinux_release}.tl3.x86_64.rpm

	if [ ! -f $kernel_tl3_rpm ]; then
		echo "$kernel_tl3_rpm not exist"
		exit 1
	fi

	if [ ! -f $kernel_debuginfo_tl3_rpm ]; then
		echo "$kernel_debuginfo_tl3_rpm not exist"
		exit 1
	fi

	if [ ! -f $kernel_headers_tl3_rpm ]; then
		echo "$kernel_headers_tl3_rpm not exist"
		exit 1
	fi

	rm -rf ${rpm_name}-${kernel_version}
	rm -rf ${rpm_name}-${kernel_version}_bin.tar.gz
	mkdir ${rpm_name}-${kernel_version}
	pushd .
	cd ${rpm_name}-${kernel_version}
	rpm2cpio ../$kernel_tl3_rpm | cpio -divm
	rpm2cpio ../$kernel_debuginfo_tl3_rpm | cpio -divm
	rpm2cpio ../$kernel_headers_tl3_rpm | cpio -divm
	rpm2cpio ../$kernel_devel_tl3_rpm | cpio -divm
	popd
	tar -czvf ${rpm_name}-${kernel_version}_bin.tar.gz ${rpm_name}-${kernel_version}
}

#main function

if [ $# -eq 0 ]; then
	echo "please specify tag"
	exit 1
fi

tag_name=$1
 
if [ ! -e "${build_srcdir}" ]; then
	mkdir $build_srcdir
	[ $? == 1 ] &&  exit 1
fi

if [ ! -e "${build_specdir}" ]; then
	mkdir $build_specdir
	[ $? == 1 ] &&  exit 1
fi

origsrc=`pwd`

get_kernel_version $origsrc
get_tlinux_name
prepare_tlinux_bin_sources
echo "Prepare $kernel_full_name source."
prepare_tlinux_spec


if test -e ${build_srcdir}/${kernel_full_name}; then
	rm -fr ${build_srcdir}/${kernel_full_name}
fi

rm -rf /var/tmp/${kernel_full_name}

cp ${kernel_full_name}_bin.tar.gz ${build_srcdir}/

echo "Build kernel rpm package."

rpmbuild -bb ${target} ${spec_file_name}
