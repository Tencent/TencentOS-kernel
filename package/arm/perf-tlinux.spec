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

%if 0%{?rhel} == 8
%global __python %{__python2}
%endif

%global build_env DESTDIR="%{buildroot}" prefix=%{_prefix} lib=%{_lib} PYTHON=%{__python3} INSTALL_ROOT=%{buildroot}
%global bpftool_make make %{?_smp_mflags} -C tools/bpf/bpftool %{build_env} mandir=%{_mandir} bash_compdir=%{_sysconfdir}/bash_completion.d/

Summary: Performance monitoring for the Linux kernel
Group: Development/System
Name: %{name}
Version: %{version}
Release: %{release_os}%{?dist}
License: GPLv2
Vendor: Tencent
Packager: tlinux team <g_CAPD_SRDC_OS@tencent.com>
Provides: perf = %{version}-%{release}
Source0: %{name}-%{version}.tar.gz
URL: http://www.tencent.com
ExclusiveArch:  aarch64
Distribution: Tencent Linux
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-build
BuildRequires: wget bc module-init-tools curl
BuildRequires: elfutils-devel zlib-devel binutils-devel newt-devel perl(ExtUtils::Embed) bison flex
BuildRequires: xmlto asciidoc
BuildRequires: audit-libs-devel
%if 0%{?rhel} == 8
BuildRequires: python2-devel
%else
BuildRequires: python-devel
%endif
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


%package -n bpftool
Summary: Inspection and simple manipulation of eBPF programs and maps
License: GPLv2
BuildRequires: llvm
%if 0%{?rhel} == 7
BuildRequires: python2-docutils
%else
BuildRequires: python3-docutils
%endif
%description -n bpftool
This package contains the bpftool, which allows inspection and simple
manipulation of eBPF programs and maps.


%{!?python_sitearch: %global python_sitearch %(%{__python} -c "from distutils.sysconfig import get_python_lib; print get_python_lib(1)")}


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

%global perf_make make %{?_smp_mflags} -C tools/perf -s V=1 WERROR=0 NO_LIBUNWIND=1 HAVE_CPLUS_DEMANGLE=1 NO_GTK2=1 NO_STRLCPY=1 prefix=%{_prefix} lib=%{_lib}
# perf
%{perf_make} all
%{perf_make} man

# bpftool
%{bpftool_make}

# install ######################################################################
%install
cd %{name}-%{version}
%{perf_make} DESTDIR=$RPM_BUILD_ROOT install
mkdir -p %{buildroot}%{_libdir}
touch %{buildroot}%{_libdir}/libperf-jvmti.so
rm -rf %{buildroot}%{_bindir}/trace

# perf-python extension
%{perf_make} DESTDIR=$RPM_BUILD_ROOT install-python_ext

# perf man pages (note: implicit rpm magic compresses them later)
%{perf_make} DESTDIR=$RPM_BUILD_ROOT install-man

%{bpftool_make} install doc-install
/usr/lib/rpm/brp-compress

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
%{_bindir}/perf*
%{_sysconfdir}/bash_completion.d/perf
%dir %{_prefix}/lib/perf
%{_prefix}/lib/perf/*
%dir %{_libdir}/traceevent/
%{_libdir}/traceevent/*
%{_libdir}/libperf-jvmti.so
%dir %{_libexecdir}/perf-core
%{_libexecdir}/perf-core/*
%{_datadir}/perf-core/*
%{_docdir}/perf-tip/tips.txt
%{_mandir}/man[1-8]/perf*

%files -n python-perf
%defattr(-,root,root)
%{python_sitearch}

%files -n bpftool
%defattr(-,root,root)
%{_sbindir}/bpftool
%{_sysconfdir}/bash_completion.d/bpftool
%{_mandir}/man8/bpftool*.gz
%{_mandir}/man7/bpf-helpers.7.gz


# changelog  ###################################################################
%changelog
* Thu Feb 2 2012 Samuel Liao <samuelliao@tencent.com>
 - Initial 3.0.18 repository
