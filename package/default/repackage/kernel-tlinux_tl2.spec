%global with_debuginfo 0
%global with_perf 1
%if 0%{?rhel} == 6
%global rdist .oc6
%global debug_path /usr/lib/debug/lib/
%else
%global debug_path /usr/lib/debug/usr/lib/
%if 0%{?rhel} == 7
%global rdist .oc7
%global _enable_debug_packages        %{nil}
%global debug_package                %{nil}
%global __debug_package                %{nil}
%global __debug_install_post        %{nil}
%else
%if 0%{?rhel} == 8
%global rdist .oc8
%global __python  /usr/bin/python2
%global _enable_debug_packages        %{nil}
%global debug_package                %{nil}
%global __debug_package                %{nil}
%global __debug_install_post        %{nil}
%endif
%endif
%endif

%global dist %{nil}

Summary: Kernel for Tencent physical machine
Name: %{name}
Version: %{version}
Release: %{release_os}%{?rdist}
License: GPLv2
Vendor: Tencent
Packager: OpenCloudOS Team
Provides: kernel = %{version}-%{release}
Provides: kernel-core = %{version}-%{release}
Provides: kernel-modules = %{version}-%{release}
Provides: kernel-modules-extra = %{version}-%{release}
Group: System Environment/Kernel
Source0: %{name}-%{version}_bin.tar.gz
Source1: tlinux_cciss_link.modules
URL: http://www.tencent.com
ExclusiveArch:  x86_64
Distribution: Tencent Linux
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-build
BuildRequires: wget bc module-init-tools curl
Requires(pre): linux-firmware >= 20150904-44

# for the 'hostname' command
%if 0%{?rhel} >= 7
BuildRequires: hostname
%else
BuildRequires: net-tools
%endif

%description
This package contains OpenCloudOS kernel, the core of operating system.

%package debuginfo-common
Summary: OpenCloudOS kernel vmlinux for crash debug
Release: %{release}
Group: System Environment/Kernel
Provides: kernel-debuginfo-common kernel-debuginfo 
Obsoletes: kernel-debuginfo
AutoReqprov: no

%description debuginfo-common
This package container vmlinux, System.map and config files for kernel
debugging.

%package devel
Summary: Development package for building kernel modules to match the %{version}-%{release} kernel
Release: %{release}
Group: System Environment/Kernel
AutoReqprov: no

%description devel
This package provides kernel headers and makefiles sufficient to build modules
against the %{version}-%{release} kernel package.

%package headers
Summary: Header files for the Linux kernel for use by glibc
Group: Development/System
Obsoletes: glibc-kernheaders
Provides: glibc-kernheaders = 3.0-46
Provides: kernel-headers
Obsoletes: kernel-headers
AutoReqprov: no

%description headers
Kernel-headers includes the C header files that specify the interface
between the Linux kernel and userspace libraries and programs.  The
header files define structures and constants that are needed for
building most standard programs and are also needed for rebuilding the
glibc package.

# prep #########################################################################
%prep

%setup -q -c -T -a 0

# build ########################################################################
%build

# install ######################################################################
%install

echo install %{version}
arch=`uname -m`
if [ "$arch" != "x86_64" ];then
	echo "Unexpected error. Cannot build this rpm in non-x86_64 platform\n"
	exit 1
fi

pwd

cd %{name}-%{version}

cp -rp * %buildroot/

elfname=vmlinuz-%{tagged_name}%{?dist}
mapname=System.map-%{tagged_name}%{?dist}
configname=config-%{tagged_name}%{?dist}

mkdir -p %buildroot/boot
mv %buildroot/lib/modules/%tagged_name/vmlinuz  %buildroot/boot/${elfname}
mv %buildroot/lib/modules/%tagged_name/dist_compat/${mapname}  %buildroot/boot/${mapname}
mv %buildroot/lib/modules/%tagged_name/dist_compat/${configname}  %buildroot/boot/${configname}
mv %buildroot/lib/modules/%tagged_name/dist_compat/.vmlinuz-%{tagged_name}%{?dist}.hmac %buildroot/boot/

mv %buildroot/lib/modules/%tagged_name/dist_compat/modules.symbols.bin %buildroot/lib/modules/%tagged_name/
mv %buildroot/lib/modules/%tagged_name/dist_compat/modules.symbols  %buildroot/lib/modules/%tagged_name/
mv %buildroot/lib/modules/%tagged_name/dist_compat/modules.softdep %buildroot/lib/modules/%tagged_name/
mv %buildroot/lib/modules/%tagged_name/dist_compat/modules.devname %buildroot/lib/modules/%tagged_name/
mv %buildroot/lib/modules/%tagged_name/dist_compat/modules.dep.bin %buildroot/lib/modules/%tagged_name/
mv %buildroot/lib/modules/%tagged_name/dist_compat/modules.dep %buildroot/lib/modules/%tagged_name/
mv %buildroot/lib/modules/%tagged_name/dist_compat/modules.builtin.bin %buildroot/lib/modules/%tagged_name/
mv %buildroot/lib/modules/%tagged_name/dist_compat/modules.alias.bin %buildroot/lib/modules/%tagged_name/
mv %buildroot/lib/modules/%tagged_name/dist_compat/modules.alias %buildroot/lib/modules/%tagged_name/

%pre
# pre #########################################################################
system_arch=`uname -m`

if [ %{_target_cpu} != ${system_arch} ]; then
	echo "ERROR: Failed installing this rpm!!!!"
	echo "This rpm is intended for %{_target_cpu} platform. It seems your system is ${system_arch}.";
	exit 1;
fi;

%post
# post #########################################################################
echo "Install OpenCloudOS kernel"
%if 0%{?rhel} >= 7
/sbin/new-kernel-pkg --package kernel --install %{tagged_name}%{?dist} --kernel-args="crashkernel=512M-12G:128M,12G-64G:256M,64G-128G:512M,128G-:768M" --make-default|| exit $?
%else
/sbin/new-kernel-pkg --install %{tagged_name}%{?dist} --kernel-args="crashkernel=512M-12G:128M,12G-64G:256M,64G-128G:512M,128G-:768M" --banner="tkernel 3.0" || exit $?

# check relase info to get suffix 
suffix=default

# modify boot menu
grubconfig=/boot/grub/grub.conf
if [ -d /sys/firmware/efi -a -f /boot/efi/EFI/tencent/grub.efi ]; then
	grubconfig=/boot/efi/EFI/tencent/grub.conf
fi

# reduce multiple blank lines to one
sed '/^$/{
        N
        /^\n$/D
}' ${grubconfig} > ${grubconfig}.tmp
mv ${grubconfig}.tmp ${grubconfig}
rm -f ${grubconfig}.tmp

# set grub default according to $suffix we already got
count=-1
defcount=0
while read line
do
    echo $line | grep -q "title"
    if [ $? -eq 0 ]; then
        (( count++ ))
    fi
    if [ -f /etc/SuSE-release ]; then
        echo $line | grep -q "%{tagged_name}%{?dist}"
    elif [ -f /etc/redhat-release ]; then
        echo $line | grep -q "title.*\(%{tagged_name}%{?dist}\).*"
    fi
    result=$?
    if [ $result -eq 0 ] ; then
        break
    fi
    if [ -f /etc/SuSE-release ]; then
        echo $line | grep -q "title %{tagged_name}%{?dist}"
    elif [ -f /etc/redhat-release ]; then
        echo $line | grep -q "title.*(%{tagged_name}%{?dist}).*"
    fi
    if [ $? -eq 0 ] ; then
        defcount=$count
    fi
done < $grubconfig

if [ $result -eq 0 ] ; then
    index=$count
else
    index=$defcount
fi

if [ -f /etc/SuSE-release ]; then
    sed -n "/^default /!p;
        {/^default /c\
        \default $index
        }
        " $grubconfig > $grubconfig.NEW
    mv $grubconfig.NEW $grubconfig
elif [ -f /etc/redhat-release ]; then
    sed -n "/^default=/!p;
        {/^default=/c\
        \default=$index
        }
        " $grubconfig > $grubconfig.NEW
    mv $grubconfig.NEW $grubconfig
fi
%endif
echo -e "Set Grub default to \"%{tagged_name}%{?dist}\" Done."

%postun
# postun #######################################################################
/sbin/new-kernel-pkg --rminitrd --dracut --remove %{tagged_name}%{?dist} || exit $?
echo -e "Remove \"%{tagged_name}%{?dist}\" Done."

%if 0%{?rhel} >= 7
%posttrans
# posttrans ####################################################################
/sbin/new-kernel-pkg --package kernel --mkinitrd --dracut --depmod --update %{tagged_name}%{?dist} || exit $?
/sbin/new-kernel-pkg --package kernel --rpmposttrans %{tagged_name}%{?dist} || exit $?
%endif

%files
# files ########################################################################
%defattr(-,root,root)
/boot/vmlinuz-%%{tagged_name}%%{?dist}
/boot/.vmlinuz-%%{tagged_name}%%{?dist}.hmac
/boot/System.map-%%{tagged_name}%%{?dist}
/boot/config-%%{tagged_name}%%{?dist}
/boot/symvers-%{tagged_name}%{?dist}*
/etc/sysconfig/modules/tlinux_cciss_link.modules
/lib/modules/%%{tagged_name}/*

%files debuginfo-common
%defattr(-,root,root)
/boot/vmlinux-%{tagged_name}%{?dist}
%dir %debug_path
%debug_path/modules
#/boot/System.map-%%{version}-%%{title}-%%{release_os}-*
#/boot/config-%%{version}-%%{title}-%%{release_os}-*


%files devel
%defattr(-,root,root)
/usr/src/kernels/%{tagged_name}%{?dist}

%files headers
%defattr(-,root,root)
/usr/include/*

# changelog  ###################################################################
%changelog
* Thu Feb 2 2012 Samuel Liao <samuelliao@tencent.com>
 - Initial 3.0.18 repository
