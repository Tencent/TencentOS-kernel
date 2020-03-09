%define sign_server 10.12.65.118

%global with_debuginfo 0
%if 0%{?rhel} == 6
%global dist .tl1
%global debug_path /usr/lib/debug/lib/
%else
%global debug_path /usr/lib/debug/usr/lib/
%if 0%{?rhel} == 7
%global dist .tl2
%endif
%endif

Summary: Kernel tools for the Linux kernel
Group: Development/System
Name: %{name}
Version: %{version}
Release: %{release_os}%{?dist}
License: GPLv2
Vendor: Tencent
Packager: tlinux team <g_APD_SRDC_OS@tencent.com>
Provides: kernel-tools = %{version}-%{release}
Source0: %{name}-%{version}.tar.gz
URL: http://www.tencent.com
ExclusiveArch:  x86_64
Distribution: Tencent Linux
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-build
BuildRequires: wget bc module-init-tools curl
BuildRequires: elfutils-devel zlib-devel binutils-devel newt-devel python-devel perl(ExtUtils::Embed) bison flex
BuildRequires: xmlto asciidoc
BuildRequires: audit-libs-devel
%ifnarch s390 s390x
BuildRequires: numactl-devel
%endif

# for the 'hostname' command
%if 0%{?rhel} == 7
BuildRequires: hostname
%else
BuildRequires: net-tools
%endif

%description
This package contains the kernel-tools tool


# prep #########################################################################
%prep

%setup -q -c -T -a 0

# build ########################################################################
%build


if [ ! -f /etc/tlinux-release ]; then
	echo "Error: please build this rpm on tlinux\n"
	exit 1
fi


cd %{name}-%{version}
all_types="%{kernel_all_types}"
num_processor=`cat /proc/cpuinfo | grep 'processor' | wc -l`
if [ $num_processor -gt 8 ]; then
    num_processor=8
fi

type_name=default

objdir=../%{name}-%{version}-obj-${type_name}
[ -d ${objdir} ] || mkdir ${objdir}
bash scripts/mkmakefile $PWD ${objdir} 2 6
if [ "$no_sign" != "yes" ]; then
cp ./package/default/config.${type_name} ${objdir}/.config
else
cp ./package/default/config.${type_name}_nosign ${objdir}/.config
fi
localversion='"'-`echo %{tagged_name}|cut -d- -f3-`%{?dist}'"'
sed -i -e 's/CONFIG_LOCALVERSION=.*/CONFIG_LOCALVERSION='$localversion'/' ${objdir}/.config 
make -C ${objdir} silentoldconfig > /dev/null
make -C ${objdir} tools/cpupower



# install ######################################################################
%install
type_name=default
cd %{name}-%{version}
objdir=../%{name}-%{version}-obj-${type_name}
make -C ${objdir} DESTDIR=$RPM_BUILD_ROOT tools/cpupower_install

%pre
# pre #########################################################################
system_arch=`uname -m`

if [ %{_target_cpu} != ${system_arch} ]; then
	echo "ERROR: Failed installing this rpm!!!!"
	echo "This rpm is intended for %{_target_cpu} platform. It seems your system is ${system_arch}.";
	exit 1;
fi;

if [ ! -f /etc/tlinux-release -o ! -f /etc/redhat-release ]; then
	echo "Error: Cannot install this rpm on non-tlinux OS"
	exit 1;
fi

%post
# post #########################################################################


%postun

%files
%defattr(-,root,root)
%{_bindir}/cpupower
%{_bindir}/cpufreq-bench_plot.sh
/etc/cpufreq-bench.conf
/usr/sbin/cpufreq-bench
/usr/share/*
/usr/man/*
/usr/lib64/*
/usr/include/*

# changelog  ###################################################################
%changelog
* Thu Feb 2 2012 Samuel Liao <samuelliao@tencent.com>
 - Initial 3.0.18 repository
