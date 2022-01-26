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
tlinux_branch=""
tlinux_release=""
extra_version=""
curdir=`pwd`
build_specdir="/usr/src/packages/SPECS"
build_srcdir="/usr/src/packages/SOURCES"
kernel_type="default"
spec_file_name=""
make_jobs=""
nosign_suffix=""
build_src_only=0
strip_sign_token=0
nosign_suffix=""
raw_tagged_name=""
# to control wheter to build rpm for xen domU
isXenU=""
build_perf=""
kernel_default_types=(default)

#funcitons
usage()
{
	echo "generate-rpms [-t \$tag_name ] [-j \$jobs ][-h]" 
	echo "     -t tag_name"
	echo "        Tagged name in the git tree."
	echo "     -j jobs"
	echo "        Specifies the number of jobs (threads) to run make."
	echo "     -p"
	echo "        build perf rpm only."
	echo "     -c"
	echo "        enable kabi check."

	echo -e "\n"
	echo "     Note:By default, we only generate latest tag kernel for standard kernels" 
	echo "          without -t"
	echo "          So, if you hope to get certain tag kernel, please use -t."
}

get_kernel_version()
{
	local makefile="$1/Makefile"

	if [ ! -e $makefile ]; then 
		echo "Error: No Makefile found."
		exit 1  
	fi      

	kernel_version+=`grep "^VERSION" ${makefile} | head -1 | awk -F "=" '{print $2}' | tr -d ' '`
	kernel_version+="."
	kernel_version+=`grep "^PATCHLEVEL" ${makefile} | head -1 | awk -F "=" '{print $2}' | tr -d ' '`
	kernel_version+="."
	kernel_version+=`grep "^SUBLEVEL" ${makefile} | head -1 | awk -F "=" '{print $2}' | tr -d ' '`
	#kernel_version+=`grep "^EXTRAVERSION" ${makefile} | head -1 | awk -F "=" '{print $2}' | tr -d ' '`
        
	echo "kernel version: $kernel_version"
}

get_tlinux_name()
{
	if [ -n "$tag_name" ]; then
		raw_tagged_name="$tag_name"
	else
		raw_tagged_name=`git describe --tags`
	fi
	
	if [ -z "${raw_tagged_name}" ];then
		echo "Error:Can't get kernel version from git tree."
		exit 1
	fi

        if [[ $raw_tagged_name != arm64-* ]]; then
                echo "$raw_tagged_name not valid tag name for arm64"
                exit 1
	fi

	tagged_name=${raw_tagged_name#arm64-}

	echo "${tagged_name}" | grep 'kvm_guest'
	if [ $? -eq 0 ]; then
		echo "start kvm guest build"
		kvm_guest="yes"
	fi

	echo "${tagged_name}" | grep 'xen_guest'
	if [ $? -eq 0 ]; then
		echo "start xen guest build"
		xen_guest="yes"
	fi

        echo "${tagged_name}" | grep 'ec'
        if [ $? -eq 0 ]; then
                echo "start ECkernel build"
                ec="yes"
        fi

        echo "${tagged_name}" | grep 'kasan'
        if [ $? -eq 0 ]; then
		nosign_suffix="_kasan"	
        fi

	
	#if [ "${tagged_name#*-*-*}" != ${tagged_name} ];then
		#echo "Error: bad tag name:$tagged_name."
		#exit 1
	#fi
	
	if [ "$build_perf" = "yes" ]; then
		rpm_name="perf"
	else
		rpm_name="kernel"
	fi
	#tlinux_release=${tagged_name#*-}
	tlinux_release=`echo $tagged_name|cut -d- -f2`.`echo $tagged_name|cut -d- -f3`
	extra_version=`echo $tagged_name | cut -d- -f2`
	kernel_full_name=${rpm_name}-${kernel_version}

	echo "$tlinux_release"
	echo "$kernel_full_name"
	echo "extra_version $extra_version"

}

prepare_tlinux_spec()
{
	local sign_token
	spec_file_name=${build_specdir}/${kernel_full_name}.spec
	spec_contents="%define name ${rpm_name} \n"
	spec_contents+="%define version ${kernel_version} \n"
	spec_contents+="%define release_os ${tlinux_release} \n"
	spec_contents+="%define tagged_name ${tagged_name} \n"
	spec_contents+="%define extra_version ${extra_version} \n"
	
	if [ "${check_kabi}" = "yes" ]; then
		check_kabi=1
	else
		check_kabi=0
	fi
	spec_contents+="%define with_kabichk ${check_kabi} \n"

	sign_token=
	if [ "$sign_token" = "" ]; then
		sign_token=0
	fi
	spec_contents+="%define sign_token ${sign_token} \n"

	
	if [ -z "${kernel_type}" ];then
		spec_contents+="%define kernel_all_types ${kernel_default_types[@]} \n"
	else
		spec_contents+="%define kernel_all_types ${kernel_type} \n"
	fi
	
	if [ -n "${make_jobs}" ];then
		 spec_contents+="%define jobs ${make_jobs} \n\n"
	fi

	if [ -e /etc/tlinux-release ];then
		 spec_contents+="%define debug_package %{nil}\n"
		 spec_contents+="%define __os_install_post %{nil}\n"
		
	fi

	if [[ $isXenU == "32"  && `uname -m` == "x86_64" ]];then
		spec_contents+="%define make_flag \"ARCH=i386\" \n"
	fi

	echo -e "${spec_contents}" > $spec_file_name

	if [ "$build_perf" = "yes" ]; then
		cat $curdir/perf-tlinux.spec >> $spec_file_name
	else
		cat $curdir/kernel-tlinux.spec >> $spec_file_name
	fi
}

#main function

#Parse Parameters
while getopts ":t:j:hdcp" option "$@"
do
	case $option in 
	"t" )
		tag_name=$OPTARG
		;;
	"j" )  
		make_jobs=$OPTARG
		;;
	"h" )  
		usage
		exit 0
		;;
	"p" )
		build_perf="yes"
		;;
	"c" )
		echo "-c kabi check enabled!"
		check_kabi="yes"
		;;
	"?" )  
		usage
		exit 1
		;;
	esac
done


#test if parameter of kernel type is valid
if [ -n "${kernel_type}" ] && [ ! -e config."${kernel_type}" ]
then
	echo "Error: bad kernel type name. Config file for ${kernel_type} does not exist."
	usage
	exit 1
elif [ -n "${kernel_type}" ] && [ -n "$isXenU" ] && [ ! -e config."${kernel_type}".xenU ]
then
	echo "Error: bad kernel type name. Config file for ${kernel_type}.xenU does not exist."
	usage
	exit 1

fi

 
if [ -e /etc/SuSE-release ];then
	build_specdir="/usr/src/packages/SPECS"
	build_srcdir="/usr/src/packages/SOURCES"
elif [ -e /etc/tlinux-release ];then
	build_specdir="$HOME/rpmbuild/SPECS"
	build_srcdir="$HOME/rpmbuild/SOURCES"
else
	echo "Error: Compile on unknown system."
	exit 1
fi


if [ ! -e "${build_srcdir}" ]; then
	mkdir $build_srcdir
	[ $? == 1 ] &&  exit 1
fi

if [ ! -e "${build_specdir}" ]; then
	mkdir $build_specdir
	[ $? == 1 ] &&  exit 1
fi

cd ../../
origsrc=`pwd`

#if [ -n "$tag_name" ]; then
#	local_valid_branch=`git branch | grep "*" | awk '{print $2 }'`
#	git checkout "${tag_name}"
#	if [ $? -ne 0 ];then
#		echo "Error: bad tag name [$tag_name] "
#		exit 1
#	fi
#fi


get_kernel_version $origsrc
get_tlinux_name
echo "Prepare $kernel_full_name source."
prepare_tlinux_spec

cp package/default/tlinux_cciss_link.modules $build_srcdir
cp package/default/cpupower.service $build_srcdir
cp package/default/cpupower.config $build_srcdir

if [ "$kvm_guest" = "yes" ] ; then
	cp package/default/kvm_guest/*.patch $build_srcdir
fi

if [ "$ec" = "yes" ] ; then
        cp package/default/ec/*.patch $build_srcdir
fi

if test -e ${build_srcdir}/${kernel_full_name}; then
	rm -fr ${build_srcdir}/${kernel_full_name}
fi

#tagged_name is a confirmed tag name and will be used for final source.
git archive --format=tar --prefix=${kernel_full_name}/ ${raw_tagged_name} | (cd ${build_srcdir} && tar xf  -)
if [ $? -ne 0 ];then
	echo "Error:can't prepare $kernel_full_name source with git archive!"
	exit 1
fi

cd ${build_srcdir}/${kernel_full_name}

if [[ $isXenU == "32" ]]
then
	config_suffix="xenU32"
	target="--target i686"
elif [[ $isXenU == "64" ]]
then
	config_suffix="xenU"
fi
echo $config_suffix

if [ -z "${kernel_type}" ];then
	for type_name in "${kernel_default_types[@]}"
	do
		if [ -n "$isXenU" ]; then
			echo mv -f ${build_srcdir}/${kernel_full_name}/package/default/config.$type_name.$config_suffix${nosign_suffix} ${build_srcdir}/${kernel_full_name}/package/default/config.$type_name{nosign_suffix}
			mv -f ${build_srcdir}/${kernel_full_name}/package/default/config.$type_name.$config_suffix${nosign_suffix} ${build_srcdir}/${kernel_full_name}/package/default/config.$type_name${nosign_suffix}
		fi
	done
else
	if [ -n "$isXenU" ]; then
		echo mv -f ${build_srcdir}/${kernel_full_name}/package/default/config.$kernel_type.$config_suffix${nosign_suffix} ${build_srcdir}/${kernel_full_name}/package/default/config.$kernel_type${nosign_suffix}
		mv -f ${build_srcdir}/${kernel_full_name}/package/default/config.$kernel_type.$config_suffix${nosign_suffix} ${build_srcdir}/${kernel_full_name}/package/default/config.$kernel_type${nosign_suffix}

	elif [ "$kvm_guest" = "yes" ] ; then
		echo \
		cp -f ${build_srcdir}/${kernel_full_name}/package/default/kvm_guest/config.${kernel_type}${config_suffix}${nosign_suffix} ${build_srcdir}/${kernel_full_name}/package/default/config.$kernel_type${nosign_suffix}
		cp -f ${build_srcdir}/${kernel_full_name}/package/default/kvm_guest/config.${kernel_type}${config_suffix}${nosign_suffix} ${build_srcdir}/${kernel_full_name}/package/default/config.$kernel_type${nosign_suffix}
	elif [ "$xen_guest" = "yes" ] ; then
		echo \
		cp -f ${build_srcdir}/${kernel_full_name}/package/default/xen_guest/config.${kernel_type}${config_suffix}${nosign_suffix} ${build_srcdir}/${kernel_full_name}/package/default/config.$kernel_type${nosign_suffix}
		cp -f ${build_srcdir}/${kernel_full_name}/package/default/xen_guest/config.${kernel_type}${config_suffix}${nosign_suffix} ${build_srcdir}/${kernel_full_name}/package/default/config.$kernel_type${nosign_suffix}
	fi
fi

cd ..
tar -zcf ${kernel_full_name}.tar.gz ${kernel_full_name}
rm -fr ${kernel_full_name}

#if [ -n "$tag_name" ];then
#	cd $origsrc
#	git checkout $local_valid_branch > /dev/null
#fi


echo "Build kernel rpm package."
if [ $build_src_only -eq 0 ]; then
	rpmbuild -bb ${target} ${spec_file_name}
	sed -i 's/^%define sign_token.*/%define sign_token please_enter_your_token_here/' ${spec_file_name}
else
	if [ $strip_sign_token -ne 0 ]; then
		sed -i 's/^%define sign_token.*/%define sign_token please_enter_your_token_here/' ${spec_file_name}
	fi
fi

echo "Build kernel source rpm package."

cd ${build_srcdir}

tar -zxf ${kernel_full_name}.tar.gz

rm -fr ${kernel_full_name}/package/default/generate-rpms.sh 
tar -zcf ${kernel_full_name}.tar.gz ${kernel_full_name}

rpmbuild -bs ${target} ${spec_file_name}
