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

Summary: Performance monitoring for the Linux kernel
Group: Development/System
Name: %{name}
Version: %{version}
Release: %{release_os}%{?dist}
License: GPLv2
Vendor: Tencent
Packager: OpenCloudOS Team
Provides: perf = %{version}-%{release}
Source0: %{name}-%{version}.tar.gz
URL: http://www.tencent.com
ExclusiveArch:  aarch64
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
This package contains the perf tool, which enables performance monitoring
of the Linux kernel.


%package -n python-perf
Summary: Python bindings for apps which will manipulate perf events
Group: Development/Libraries
%description -n python-perf
The python-perf package contains a module that permits applications
written in the Python programming language to use the interface
to manipulate perf events.

%{!?python_sitearch: %global python_sitearch %(%{__python} -c "from distutils.sysconfig import get_python_lib; print get_python_lib(1)")}


# prep #########################################################################
%prep

%setup -q -c -T -a 0

# build ########################################################################
%build

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


%global perf_make make %{?_smp_mflags} -C tools/perf -s V=1 WERROR=0 NO_LIBUNWIND=1 HAVE_CPLUS_DEMANGLE=1 NO_GTK2=1 NO_STRLCPY=1 prefix=%{_prefix}
# perf
%{perf_make} all
%{perf_make} man

# install ######################################################################
%install
cd %{name}-%{version}
%{perf_make} DESTDIR=$RPM_BUILD_ROOT install

# perf-python extension
%{perf_make} DESTDIR=$RPM_BUILD_ROOT install-python_ext

# perf man pages (note: implicit rpm magic compresses them later)
%{perf_make} DESTDIR=$RPM_BUILD_ROOT install-man

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


%postun

%files
%defattr(-,root,root)
%{_bindir}/perf
%dir %{_libexecdir}/perf-core
%{_libexecdir}/perf-core/*
%{_mandir}/man[1-8]/perf*
%{_sysconfdir}/bash_completion.d/perf
/usr/bin/trace
/usr/lib/traceevent/plugins/*
/usr/lib/perf
/usr/share/doc/*
/usr/share/perf-core/strace/groups
/usr/share/perf-core/strace/groups

%files -n python-perf
%defattr(-,root,root)
%{python_sitearch}


# changelog  ###################################################################
%changelog
* Thu Feb 2 2012 Samuel Liao <samuelliao@tencent.com>
 - Initial 3.0.18 repository
