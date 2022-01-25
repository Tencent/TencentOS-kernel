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
%global _build_id_links none
%endif
%if 0%{?rhel} == 8
%global rdist .oc8
%global __python  /usr/bin/python2
%global _enable_debug_packages        %{nil}
%global debug_package                %{nil}
%global __debug_package                %{nil}
%global __debug_install_post        %{nil}
%global _build_id_links none
%endif
%endif

%global dist %{nil}

Summary: Kernel for Tencent physical machine
Name: %{name}
Version: %{version}
Release: %{release_os}%{?rdist}
License: GPLv2
Vendor: Tencent
Packager: tlinux team <g_CAPD_SRDC_OS@tencent.com>
Provides: kernel = %{version}-%{release}
Provides: kernel-core = %{version}-%{release}
Provides: kernel-modules = %{version}-%{release}
Provides: kernel-modules-extra = %{version}-%{release}
Group: System Environment/Kernel
Source0: %{name}-%{version}_bin.tar.gz
Source1: tlinux_cciss_link.modules
URL: http://www.tencent.com
ExclusiveArch:  aarch64
Distribution: Tencent Linux
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-build
BuildRequires: wget bc module-init-tools curl
%if %{with_perf}
BuildRequires: elfutils-devel zlib-devel binutils-devel newt-devel perl(ExtUtils::Embed) bison flex
BuildRequires: xmlto asciidoc hmaccalc
BuildRequires: audit-libs-devel
#BuildRequires: uboot-tools
%if 0%{?rhel} == 8
BuildRequires: python2-devel
%else
BuildRequires: python-devel
%endif


%ifnarch s390 s390x
BuildRequires: numactl-devel
%endif
%endif
Requires(pre): linux-firmware >= 20100806-2

# for the 'hostname' command
%if 0%{?rhel} == 7
BuildRequires: hostname
%else
BuildRequires: net-tools
%endif

%description
This package contains tlinux kernel for arm64 Bare-Metal& VM

%package debuginfo-common
Summary: tlinux kernel vmlinux for crash debug
Release: %{release}
Group: System Environment/Kernel
Provides: kernel-debuginfo-common kernel-debuginfo
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
if [ "$arch" != "aarch64" ];then
	echo "Unexpected error. Cannot build this rpm in non-aarch64 platform\n"
	exit 1
fi

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
echo "Install tlinux kernel"
%if 0%{?rhel} == 8
kernel-install add  %{tagged_name} /lib/modules/%{tagged_name}/vmlinuz
cp -p /lib/modules/%{tagged_name}/dist_compat/config-%{tagged_name}%{?dist} /boot
cp -p /lib/modules/%{tagged_name}/dist_compat/.vmlinuz-%{tagged_name}%{?dist}.hmac /boot
cp -p /lib/modules/%{tagged_name}/dist_compat/System.map-%{tagged_name}%{?dist} /boot
%else
%if 0%{?rhel} == 7
/sbin/new-kernel-pkg --package kernel --install %{tagged_name}%{?dist} --make-default|| exit $?
echo -e "Set Grub default to \"%{tagged_name}%{?dist}\" Done."
%endif
%endif

%preun
# preun #######################################################################
%if 0%{?rhel} == 8
kernel-install remove  %{tagged_name}
%else
%if 0%{?rhel} == 7
/sbin/new-kernel-pkg --rminitrd --dracut --remove %{tagged_name}%{?dist} || exit $?
echo -e "Remove \"%{tagged_name}%{?dist}\" Done."
%endif
%endif

%posttrans
# posttrans ####################################################################
%if 0%{?rhel} == 7
/sbin/new-kernel-pkg --package kernel --mkinitrd --dracut --depmod --update %{tagged_name}%{?dist} || exit $?
/sbin/new-kernel-pkg --package kernel --rpmposttrans %{tagged_name}%{?dist} || exit $?
%endif

%files
# files ########################################################################
%defattr(-,root,root)
/boot/vmlinuz-%%{tagged_name}%%{?dist}
/boot/.vmlinuz-%%{tagged_name}%%{?dist}.hmac
#/boot/uImage-%%{tagged_name}%%{?dist}
/boot/System.map-%%{tagged_name}%%{?dist}
/boot/config-%%{tagged_name}%%{?dist}
/boot/symvers-%{tagged_name}%{?dist}*
/etc/sysconfig/modules/tlinux_cciss_link.modules
/lib/modules/%%{tagged_name}/*
#do not package firmware ,use linux-firmware rpm instead
#/lib/firmware/

%files debuginfo-common
%defattr(-,root,root)
/boot/vmlinux-%{tagged_name}%{?dist}
%dir %debug_path
%debug_path/modules
#/boot/System.map-%%{version}-%%{title}-%%{release_os}-*
#/boot/config-%%{version}-%%{title}-%%{release_os}-*

%files headers
%defattr(-,root,root)
/usr/include/*

%files devel
%defattr(-,root,root)
/usr/src/kernels/%{tagged_name}%{?dist}

# changelog  ###################################################################
%changelog
* Mon Mar 11 2019 Jia Wang <jasperwang@tencent.com>
 - Initial 4.14.99 repository
