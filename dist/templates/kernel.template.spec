# A simplified kernel spec initially based on Tencent Linux Kernels and Fedora/CentOS
#
# By changing a few rpm macros, it's very convenient to build for different archs or
# kernel config styles, and build different components.
### Kenrel version relation macros
# Following variables filled by automation scripts:
# %%{kernel_unamer}: `uname -r` output, needed by scriptlets so prepare it early, eg. 5.18.19-2207.2.1.tks, 5.18.19-2207.2.1.tks+debug, 5.4.119-1-0009.1
# %%{kernel_majver}: Kernel RPM package version, eg. 5.15.0, 5.15.3, 5.16.0
# %%{kernel_relver}: Kernel RPM package release, eg. 2207.1, 0.20211115git1135ec008ef3.rc0.2207, 0009.11
# %%{rpm_name}: Kernel RPM package name, eg. kernel, kernel-tlinux4, kernel-stream kernel-stream-debug
# %%{rpm_vendor}: RPM package vendor
# %%{rpm_url}: RPM url
# TODO: kernel_unamer don't have distro mark
{{VERSIONSPEC}}

# TODO: We have a fixed tar name, might be better to include KDIST in tarname
%define kernel_tarname kernel-%{kernel_majver}-%{kernel_relver}

# This section defines following value:
# %%{kernel_arch}
# Since kernel arch name differs from many other definations, this will insert a script snip
# to handle the convertion, and error out on unsupported arch.
{{ARCHSPEC}}

# TODO: This is a workaround for kernel userspace tools (eg. perf), which doesn't
# support LTO, and causes FTBFS, need to remove this after LTO is available in
# upstream
%global _lto_cflags %{nil}

###### Kernel packaging options #################################################
# Since we need to generate kernel, kernel-subpackages, perf, bpftools, from
# this one single code tree, following build switches are very helpful.
#
# The following build options can be enabled or disabled with --with/--without
# in the rpmbuild command. But may by disabled by later checks#
#
# This section defines following options:
# with_core: kernel core pkg
# with_doc: kernel doc pkg
# with_headers: kernel headers pkg
# with_perf: perf tools pkg
# with_tools: kernel tools pkg
# with_bpftool: bpftool pkg
# with_debuginfo: debuginfo for all packages
# with_modsign: if mod should be signed
# with_kabichk: if kabi check is needed at the end of build
{{PKGPARAMSPEC}}

# Only use with cross build, don't touch it unless you know what you are doing
%define with_crossbuild	%{?_with_crossbuild: 1}		%{?!_with_crossbuild: 0}

###### Kernel signing params #################################################
### TODO: Currently only module signing, no secureboot
# module-keygen
# Should be an executable accepting two params:
# module-keygen <kernel ver> <kernel objdir>
# <kernel ver>: Kernel's version-release, `uname -r` output of that kernel
# <kernel objdir>: Kernel build dir, where built kernel objs, certs, and vmlinux is stored
#
# This executable should provide required keys for signing, or at least disable builtin keygen
%define use_builtin_module_keygen %{?_module_keygen: 0} %{?!_module_keygen: 1}

# module-signer
# Should be an executable accepting three params:
# module-signer <buildroot> <kernel ver> <kernel objdir>
# <kernel ver>: Kernel's version-release, `uname -r` output of that kernel
# <kernel objdir>: Kernel build dir, where built kernel objs, certs, and vmlinux is stored
# <buildroot>: RPM's buildroot, where kernel modules are installed into
#
# This executable should sign all kernel modules in <builddir>/lib/modules/<kernel ver>
# based on the info gatherable from <kernel objdir>.
%define use_builtin_module_signer %{?_module_signer: 0} %{?!_module_signer: 1}

###### Required RPM macros #####################################################

### Debuginfo handling
# Following macros controls RPM's builtin debuginfo extracting behaviour,
# tune it into a kernel friendly style.
#
# Kernel package needs its own method to pack the debuginfo files.
# This disables RPM's built-in debuginfo files packaging, we package
# debuginfo files manually use find-debuginfo.sh.
%undefine _debuginfo_subpackages
# This disables RH vendor macro's debuginfo package template generation.
# It only generates debuginfo for the main package, but we only want debuginfo
# for subpackages so disable it and do things manually.
%undefine _enable_debug_packages
# This disable find-debuginfo.sh from appending minimal debuginfo
# to every binary.
%undefine _include_minidebuginfo
# This disables debugsource package which collect source files for debug info,
# we pack the kernel source code manually.
%undefine _debugsource_packages
# TODO: This prevents find-debuginfo.sh from adding unique suffix to .ko.debug files
# that will make .ko.debug file names unrecognizable by `crash`
# We may patch `crash` to fix that or find a better way, since this stops the unique
# debug file renaming for userspace packages too.
%undefine _unique_debug_names
# Pass --reloc-debug-sections to eu-strip, .ko files are ET_REL files. So they have relocation
# sections for debug sections. Those sections will not be relinked. This help create .debug files
# that has cross debug section relocations resolved.
%global _find_debuginfo_opts -r
%global debuginfo_dir /usr/lib/debug

###### Build time config #######################################################
# Disable kernel building for non-supported arch, allow building userspace package
%ifarch %nobuildarches noarch
%global with_core 0
%endif

# Require cross compiler if cross compiling
%if %{with_crossbuild}
BuildRequires: binutils-%{_build_arch}-linux-gnu, gcc-%{_build_arch}-linux-gnu
%global with_perf 0
%global with_tools 0
%global with_bpftool 0
%global _cross_compile %{!?_cross_compile:%{_build_arch}-linux-gnu-}%{?_cross_compile:%{_cross_compile}}
%endif

# List the packages used during the kernel build
BuildRequires: kmod, patch, bash, coreutils, tar, git-core, which, gawk
BuildRequires: make, gcc, binutils, system-rpm-config, hmaccalc, bison, flex, gcc-c++
BuildRequires: bzip2, xz, findutils, gzip, perl-interpreter, perl-Carp, perl-devel
BuildRequires: net-tools, hostname, bc
BuildRequires: dwarves
BuildRequires: python3-devel, openssl-devel, elfutils-devel
BuildRequires: openssl
BuildRequires: gcc-plugin-devel
# glibc-static is required for a consistent build environment (specifically
# CONFIG_CC_CAN_LINK_STATIC=y).
BuildRequires: glibc-static

%if %{with_perf}
BuildRequires: zlib-devel binutils-devel newt-devel perl(ExtUtils::Embed) bison flex xz-devel
BuildRequires: audit-libs-devel
BuildRequires: java-devel
BuildRequires: libbabeltrace-devel
%ifnarch aarch64
BuildRequires: numactl-devel
%endif
%endif

%if %{with_tools}
BuildRequires: gettext ncurses-devel
BuildRequires: pciutils-devel libcap-devel libnl3-devel
%endif

%if %{with_doc}
BuildRequires: xmlto, asciidoc
%endif

%if %{with_bpftool}
BuildRequires: llvm
# We don't care about this utils's python version, since we only want rst2* commands during build time
BuildRequires: /usr/bin/rst2man
BuildRequires: zlib-devel binutils-devel
%endif

%if %{with_headers}
BuildRequires: rsync
%endif

###### Kernel packages sources #################################################
### Kernel tarball
Source0: %{kernel_tarname}.tar

### Build time scripts
# Script used to assist kernel building
Source10: filter-modules.sh

Source20: module-signer.sh
Source21: module-keygen.sh

Source30: check-kabi

### Arch speficied kernel configs and kABI
# Start from Source1000 to Source1199, for kernel config
# Start from Source1200 to Source1399, for kabi
{{ARCHSOURCESPEC}}

### Userspace tools
# Start from Source2000 to Source2999, for userspace tools
Source2000: cpupower.service
Source2001: cpupower.config
# TK4 Legacy CCISS naming rule
Source2002: tlinux_cciss_link_compat.modules

### TK4 Legacy MLNX OFED
Source3000: MLNX_OFED_SRC-5.1-2.5.8.0.46.tgz

###### Kernel package definations ##############################################
### Main meta package
Summary: %{rpm_vendor} Linux kernel meta package
Name: %{rpm_name}
Version: %{kernel_majver}
Release: %{kernel_relver}%{?dist}
License: GPLv2
URL: %{rpm_url}
Vendor: ${rpm_vendor}

# We can't let RPM do the dependencies automatic because it'll then pick up
# a correct but undesirable perl dependency from the module headers which
# isn't required for the kernel proper to function
AutoReq: no
AutoProv: yes

# Kernel requirements
# installonlypkg(kernel) is a hint for RPM that this package shouldn't be auto-cleaned.
Provides: installonlypkg(kernel)
Provides: kernel = %{version}-%{release}
Provides: %{rpm_name} = %{version}-%{release}
Requires: %{rpm_name}-core = %{version}-%{release}
Requires: %{rpm_name}-modules = %{version}-%{release}
Requires: linux-firmware

%description
This is the meta package of %{?rpm_vendor:%{rpm_vendor} }Linux kernel, the core of operating system.

%if %{with_core}
### Kernel core package
%package core
Summary: %{rpm_vendor} Linux Kernel
Provides: installonlypkg(kernel)
Provides: kernel-core = %{version}-%{release}
Provides: kernel-uname-r = %{kernel_unamer}
Requires(pre): coreutils
Requires(post): coreutils kmod dracut
Requires(preun): coreutils kmod
Requires(post): %{_bindir}/kernel-install
Requires(preun): %{_bindir}/kernel-install
# Kernel install hooks & initramfs
%if 0%{?rhel} == 7 || "%{?dist}" == ".tl2"
Requires(post): systemd
Requires(preun): systemd
%else
Requires(post): systemd-udev
Requires(preun): systemd-udev
%endif

%description core
The kernel package contains the %{?rpm_vendor:%{rpm_vendor} } Linux kernel (vmlinuz), the core of
operating system. The kernel handles the basic functions
of the operating system: memory allocation, process allocation, device
input and output, etc.

### Kernel module package
%package modules
Summary: %{rpm_vendor} Kernel modules to match the %{rpm_name}-core kernel
Provides: installonlypkg(kernel-module)
Provides: kernel-modules = %{version}-%{release}
Provides: kernel-modules-extra = %{version}-%{release}
Requires: %{rpm_name}-core = %{version}-%{release}
AutoReq: no
AutoProv: yes
Requires(pre): kmod
Requires(postun): kmod
%description modules
This package provides commonly used kernel modules for the %{?2:%{2}-}core kernel package.

### Kernel devel package
%package devel
Summary: Development package for building kernel modules to match the %{version}-%{release} kernel
Release: %{release}
Provides: installonlypkg(kernel)
Provides: kernel-devel = %{version}-%{release}
Provides: kernel-devel-%{_target_cpu} = %{version}-%{release}
Provides: kernel-devel-uname-r = %{kernel_unamer}
AutoReqprov: no
%description devel
This package provides kernel headers and makefiles sufficient to build modules
against the %{version}-%{release} kernel package.

%if %{with_debuginfo}
### Kernel debuginfo package
%package debuginfo
Summary: Debug information for package %{rpm_name}
Requires: %{rpm_name}-debuginfo-common-%{_target_cpu}
Provides: installonlypkg(kernel)
Provides: kernel-debuginfo = %{version}-%{release}
AutoReqProv: no
%description debuginfo
This package provides debug information including
vmlinux, System.map for package %{rpm_name}.
This is required to use SystemTap with %{rpm_name}.
# debuginfo search rule
# If BTF presents, keep it so kernel can use it.
%if 0%{?rhel} != 7
# Old version of find-debuginfo.sh doesn't support this, so only do it for newer version. Old version of eu-strip seems doesn't strip BTF either, so should be fine.
%global _find_debuginfo_opts %{_find_debuginfo_opts} --keep-section '.BTF'
%endif
# Debuginfo file list for main kernel package
# The (\+.*)? is used to match all variant kernel
%global _find_debuginfo_opts %{_find_debuginfo_opts} -p '.*\/usr\/src\/kernels/.*|XXX' -o ignored-debuginfo.list -p $(echo '/.*/%{kernel_unamer}/.*|/.*%{kernel_unamer}(\.debug)?' | sed 's/+/[+]/g') -o debuginfo.list
# with_debuginfo
%endif
# with_core
%endif

%if %{with_debuginfo}
### Common debuginfo package
%package debuginfo-common-%{_target_cpu}
Summary: Kernel source files used by %{rpm_name}-debuginfo packages
Provides: installonlypkg(kernel)
Provides: kernel-debuginfo-common = %{version}-%{release}
%description debuginfo-common-%{_target_cpu}
This package is required by %{rpm_name}-debuginfo subpackages.
It provides the kernel source files common to all builds.
# No need to define extra debuginfo search rule here, use debugfiles.list
# with_debuginfo
%endif

%if %{with_headers}
%package headers
Summary: Header files for the Linux kernel for use by glibc
Obsoletes: glibc-kernheaders < 3.0-46
Provides: glibc-kernheaders = 3.0-46
Provides: kernel-headers = %{version}-%{release}
%description headers
Kernel-headers includes the C header files that specify the interface
between the Linux kernel and userspace libraries and programs. The
header files define structures and constants that are needed for
building most standard programs and are also needed for rebuilding the
glibc package.
# with_headers
%endif

%if %{with_perf}
%package -n perf
Summary: Performance monitoring for the Linux kernel
Requires: bzip2
License: GPLv2
%description -n perf
This package contains the perf tool, which enables performance monitoring
of the Linux kernel.

%package -n perf-debuginfo
Summary: Debug information for package perf
Requires: %{rpm_name}-debuginfo-common-%{_target_cpu} = %{version}-%{release}
AutoReqProv: no
%description -n perf-debuginfo
This package provides debug information for the perf package.
# debuginfo search rule
# Note that this pattern only works right to match the .build-id
# symlinks because of the trailing nonmatching alternation and
# the leading .*, because of find-debuginfo.sh's buggy handling
# of matching the pattern against the symlinks file.
%global _find_debuginfo_opts %{_find_debuginfo_opts} -p '.*%{_bindir}/perf(.*\.debug)?|.*%{_libexecdir}/perf-core/.*|.*%{_libdir}/libperf-jvmti.so(.*\.debug)?|XXX' -o perf-debuginfo.list

%package -n python3-perf
Summary: Python bindings for apps which will manipulate perf events
%description -n python3-perf
The python3-perf package contains a module that permits applications
written in the Python programming language to use the interface
to manipulate perf events.

%package -n python3-perf-debuginfo
Summary: Debug information for package perf python bindings
Requires: %{rpm_name}-debuginfo-common-%{_target_cpu} = %{version}-%{release}
AutoReqProv: no
%description -n python3-perf-debuginfo
This package provides debug information for the perf python bindings.
# debuginfo search rule
# the python_sitearch macro should already be defined from above
%global _find_debuginfo_opts %{_find_debuginfo_opts} -p '.*%{python3_sitearch}/perf.*\.so(.*\.debug)?|XXX' -o python3-perf-debuginfo.list
# with_perf
%endif

%if %{with_tools}
%package -n kernel-tools
Summary: Assortment of tools for the Linux kernel
License: GPLv2
%ifarch %{cpupowerarchs}
Provides: cpupowerutils = 1:009-0.6.p1
Obsoletes: cpupowerutils < 1:009-0.6.p1
Provides: cpufreq-utils = 1:009-0.6.p1
Provides: cpufrequtils = 1:009-0.6.p1
Obsoletes: cpufreq-utils < 1:009-0.6.p1
Obsoletes: cpufrequtils < 1:009-0.6.p1
Obsoletes: cpuspeed < 1:1.5-16
Requires: kernel-tools-libs = %{version}-%{release}
%endif
%description -n kernel-tools
This package contains the tools/ directory from the kernel source
and the supporting documentation.

%package -n kernel-tools-libs
Summary: Libraries for the kernels-tools
License: GPLv2
%description -n kernel-tools-libs
This package contains the libraries built from the tools/ directory
from the kernel source.

%package -n kernel-tools-libs-devel
Summary: Assortment of tools for the Linux kernel
License: GPLv2
Requires: kernel-tools = %{version}-%{release}
%ifarch %{cpupowerarchs}
Provides: cpupowerutils-devel = 1:009-0.6.p1
Obsoletes: cpupowerutils-devel < 1:009-0.6.p1
%endif
Requires: kernel-tools-libs = %{version}-%{release}
Provides: kernel-tools-devel
%description -n kernel-tools-libs-devel
This package contains the development files for the tools/ directory from
the kernel source.

%package -n kernel-tools-debuginfo
Summary: Debug information for package kernel-tools
Requires: %{rpm_name}-debuginfo-common-%{_target_cpu} = %{version}-%{release}
AutoReqProv: no
%description -n kernel-tools-debuginfo
This package provides debug information for package kernel-tools.
# debuginfo search rule
# Note that this pattern only works right to match the .build-id
# symlinks because of the trailing nonmatching alternation and
# the leading .*, because of find-debuginfo.sh's buggy handling
# of matching the pattern against the symlinks file.
%global _find_debuginfo_opts %{_find_debuginfo_opts} -p '.*%{_bindir}/(cpupower|tmon|gpio-.*|iio_.*|ls.*|centrino-decode|powernow-k8-decode|turbostat|x86_energy_perf_policy|intel-speed-select|page_owner_sort|slabinfo)(.*\.debug)?|.*%{_libdir}/libcpupower.*|XXX' -o kernel-tools-debuginfo.list
# with_tools
%endif

%if %{with_bpftool}
%package -n bpftool
Summary: Inspection and simple manipulation of eBPF programs and maps
License: GPLv2
%description -n bpftool
This package contains the bpftool, which allows inspection and simple
manipulation of eBPF programs and maps.

%package -n bpftool-debuginfo
Summary: Debug information for package bpftool
Requires: %{rpm_name}-debuginfo-common-%{_target_cpu} = %{version}-%{release}
AutoReqProv: no
%description -n bpftool-debuginfo
This package provides debug information for the bpftool package.
# debuginfo search rule
%global _find_debuginfo_opts %{_find_debuginfo_opts} -p '.*%{_sbindir}/bpftool(.*\.debug)?|XXX' -o bpftool-debuginfo.list
# with_bpftool
%endif

###### common macros for build and install #####################################
### Signing scripts
# If externel module signer and keygen provided, ignore built-in keygen and
# signer, else use builtin keygen and signer.
%if %{use_builtin_module_signer}
	# SOURCE20 is just a wrapper for $BUILD_DIR/scripts/sign-file
	%define _module_signer %{SOURCE20}
%endif
%if %{use_builtin_module_keygen}
	# SOURCE21 is a dummy file, only perform some checks, we depend on Kbuild for builtin keygen
	%define _module_keygen %{SOURCE21}
%endif

### Prepare common build vars to share by %%prep, %%build and %%install section
# _KernSrc: Path to kernel source, located in _buildir
# _KernBuild: Path to the built kernel objects, could be same as $_KernSrc (just like source points to build under /lib/modules/<kver>)
# _KernVmlinuxH: path to vmlinux.h for BTF, located in _buildir
# KernUnameR: Get `uname -r` output of the built kernel
# KernModule: Kernel modules install path, located in %%{buildroot}
# KernDevel: Kernel headers and sources install path, located in %%{buildroot}
%global prepare_buildvar \
	cd %{kernel_tarname} \
	_KernSrc=%{_builddir}/%{rpm_name}-%{kernel_unamer}/%{kernel_tarname} \
	_KernBuild=%{_builddir}/%{rpm_name}-%{kernel_unamer}/%{kernel_unamer}-obj \
	_KernVmlinuxH=%{_builddir}/%{rpm_name}-%{kernel_unamer}/vmlinux.h \
	KernUnameR=%{kernel_unamer} \
	KernModule=%{buildroot}/lib/modules/%{kernel_unamer} \
	KernDevel=%{buildroot}/usr/src/kernels/%{kernel_unamer} \

###### Rpmbuild Prep Stage #####################################################
%prep
%setup -q -c -n %{rpm_name}-%{kernel_unamer}
%{prepare_buildvar}

# TODO: Apply test patch here
:

# Mangle /usr/bin/python shebangs to /usr/bin/python3
# Mangle all Python shebangs to be Python 3 explicitly
# -p preserves timestamps
# -n prevents creating ~backup files
# -i specifies the interpreter for the shebang
# This fixes errors such as
# *** ERROR: ambiguous python shebang in /usr/bin/kvm_stat: #!/usr/bin/python. Change it to python3 (or python2) explicitly.
# We patch all sources below for which we got a report/error.
find scripts/ tools/ Documentation/ \
	-type f -and \( \
		-name "*.py" -or \( -not -name "*.*" -exec grep -Iq '^#!.*python' {} \; \) \
	\) \
	-exec pathfix.py -i "%{__python3} %{py3_shbang_opts}" -p -n {} \+;

# Update kernel version and suffix info to make uname consistent with RPM version
# PATCHLEVEL inconsistent only happen on first merge window, but patch them all just in case
sed -i "/^VESION/cVERSION = $(echo %{kernel_majver} | cut -d '.' -f 1)" $_KernSrc/Makefile
sed -i "/^PATCHLEVEL/cPATCHLEVEL = $(echo %{kernel_majver} | cut -d '.' -f 2)" $_KernSrc/Makefile
sed -i "/^SUBLEVEL/cSUBLEVEL = $(echo %{kernel_majver} | cut -d '.' -f 3)" $_KernSrc/Makefile

# Patch the kernel to apply uname, the reason we use EXTRAVERSION to control uname
# instead of complete use LOCALVERSION is that, we don't want out scm/rpm version info
# get inherited by random kernels built reusing the config file under /boot, which
# will be confusing.
_KVERSION=$(sed -nE "/^VERSION\s*:?=\s*(.*)/{s/^\s*^VERSION\s*:?=\s*//;h};\${x;p}" $_KernSrc/Makefile)
_KPATCHLEVEL=$(sed -nE "/^PATCHLEVEL\s*:?=\s*(.*)/{s/^\s*^PATCHLEVEL\s*:?=\s*//;h};\${x;p}" $_KernSrc/Makefile)
_KSUBLEVEL=$(sed -nE "/^SUBLEVEL\s*:?=\s*(.*)/{s/^\s*^SUBLEVEL\s*:?=\s*//;h};\${x;p}" $_KernSrc/Makefile)
_KUNAMER_PREFIX=${_KVERSION}.${_KPATCHLEVEL}.${_KSUBLEVEL}
_KEXTRAVERSION=""
_KLOCALVERSION=""

case $KernUnameR in
	$_KUNAMER_PREFIX* )
		_KEXTRAVERSION=$(echo "$KernUnameR" | sed -e "s/^$_KUNAMER_PREFIX//")

		# Anything after "+" belongs to LOCALVERSION, eg, +debug/+minimal marker.
		_KLOCALVERSION=$(echo "$_KEXTRAVERSION" | sed -ne 's/.*\([+].*\)$/\1/p')
		_KEXTRAVERSION=$(echo "$_KEXTRAVERSION" | sed -e 's/[+].*$//')

		# Update Makefile to embed uname
		sed -i "/^EXTRAVERSION/cEXTRAVERSION = $_KEXTRAVERSION" $_KernSrc/Makefile
		# Save LOCALVERSION in .dist.localversion, it will be set to .config after
		# .config is ready in BuildConfig.
		echo "$_KLOCALVERSION" > $_KernSrc/.dist.localversion
		;;
	* )
		echo "FATAL: error: kernel version doesn't match with kernel spec." >&2 && exit 1
		;;
	esac

###### Rpmbuild Build Stage ####################################################
%build

### Make flags
#
# Those defination have to be defined after %%build macro, %%build macro may change
# some build flags, and we have to inherit these changes.
#
# NOTE: kernel's tools build system doesn't playwell with command line variables, some
# `override` statement will stop working after recursive Makefile `include` call.
# So keep these variables as environment variables so they are available globally,
# `make` will transformed env vars into makefile variable in every iteration.

## Common flags
%global make %{__make} %{_smp_mflags}
%global kernel_make_opts INSTALL_HDR_PATH=%{buildroot}/usr INSTALL_MOD_PATH=%{buildroot} KERNELRELEASE=$KernUnameR
%global tools_make_opts DESTDIR="%{buildroot}" prefix=%{_prefix} lib=%{_lib} PYTHON=%{__python3} INSTALL_ROOT="%{buildroot}"

# TK4: Workaround, Makefile fails with mass parallel build
%global tools_make_opts %{tools_make_opts} -j1

## Cross compile flags
%if %{with_crossbuild}
%global kernel_make_opts %{kernel_make_opts} CROSS_COMPILE=%{_cross_compile} ARCH=%{kernel_arch}
%global __strip %{_build_arch}-linux-gnu-strip
%endif

# Drop host cflags for crossbuild, arch options from build target will break host compiler
%if !%{with_crossbuild}
%global kernel_make_opts %{kernel_make_opts} HOSTCFLAGS="%{?build_cflags}" HOSTLDFLAGS="%{?build_ldflags}"
%endif

## make for host tool, reset arch and flags for native host bulid
%global host_make CFLAGS= LDFLAGS= ARCH= %{make}

## make for kernel
%global kernel_make %{make} %{kernel_make_opts}

## make for tools
%global tools_make CFLAGS="${RPM_OPT_FLAGS}" LDFLAGS="%{__global_ldflags}" %{make} %{tools_make_opts}
%global perf_make EXTRA_CFLAGS="${RPM_OPT_FLAGS}" LDFLAGS="%{__global_ldflags}" WERROR=0 NO_LIBUNWIND=1 HAVE_CPLUS_DEMANGLE=1 NO_GTK2=1 NO_STRLCPY=1 NO_BIONIC=1 %{make} %{tools_make_opts}
%global bpftool_make EXTRA_CFLAGS="${RPM_OPT_FLAGS}" EXTRA_LDFLAGS="%{__global_ldflags}" VMLINUX_H="$_KernVmlinuxH" %{make} %{tools_make_opts}

### Real make
%{prepare_buildvar}

# Prepare Kernel config
BuildConfig() {
	mkdir -p $_KernBuild
	pushd $_KernBuild
	cp $1 .config

	[ "$_KernBuild" != "$_KernSrc" ] && echo "include $_KernSrc/Makefile" > Makefile
	[ "$_KernBuild" != "$_KernSrc" ] && cp $_KernSrc/.dist.localversion ./

	# Respect scripts/setlocalversion, avoid it from potentially mucking with our version numbers.
	# Also update LOCALVERSION in .config
	cp .dist.localversion .scmversion
	"$_KernSrc"/scripts/config --file .config --set-str LOCALVERSION "$(cat .dist.localversion)"

	# Ensures build-ids are unique to allow parallel debuginfo
	sed -i -e "s/^CONFIG_BUILD_SALT.*/CONFIG_BUILD_SALT=\"$KernUnameR\"/" .config

	# Call olddefconfig before make all, set all unset config to default value.
	# The packager uses CROSS_COMPILE=scripts/dummy-tools for generating .config
	# so compiler related config are always unset, let's just use defconfig for them for now
	%{kernel_make} olddefconfig

	%if %{with_modsign}
	# Don't use Kbuild's signing, use %%{_module_signer} instead, be compatible with debuginfo and compression
	sed -i -e "s/^CONFIG_MODULE_SIG_ALL.*/# CONFIG_MODULE_SIG_ALL is not set/" .config
	%else
	# Not signing, unset all signing related configs
	sed -i -e "s/^CONFIG_MODULE_SIG_ALL=.*/# CONFIG_MODULE_SIG_ALL is not set/" .config
	sed -i -e "s/^CONFIG_MODULE_SIG_FORCE=.*/# CONFIG_MODULE_SIG_FORCE is not set/" .config
	sed -i -e "s/^CONFIG_MODULE_SIG=.*/# CONFIG_MODULE_SIG is not set/" .config
	# Lockdown can't work without module sign
	sed -i -e "s/^CONFIG_SECURITY_LOCKDOWN_LSM=.*/# CONFIG_SECURITY_LOCKDOWN_LSM is not set/" .config
	sed -i -e "s/^CONFIG_SECURITY_LOCKDOWN_LSM_EARLY=.*/# CONFIG_SECURITY_LOCKDOWN_LSM_EARLY is not set/" .config
	%endif
	# Don't use kernel's builtin module compression, imcompatible with debuginfo packaging and signing
	sed -i -e "s/^\(CONFIG_DECOMPRESS_.*\)=y/# \1 is not set/" .config
	popd
}

## $1: .config file
BuildKernel() {
	echo "*** Start building kernel $KernUnameR"
	mkdir -p $_KernBuild
	pushd $_KernBuild

	%if %{with_modsign}
	# Call keygen here, if it generate the module keys, it should come before kbuild,
	# so kbuild may avoid regenerate cert keys.
	%{_module_keygen} "$KernUnameR" "$_KernBuild"
	%endif

	# Build vmlinux
	%{kernel_make} all
	# Build modules
	grep -q "CONFIG_MODULES=y" ".config" && %{kernel_make} modules
	# CONFIG_KERNEL_HEADER_TEST generates some extra files in the process of
	# testing so just delete
	find . -name *.h.s -delete

	%if %{with_bpftool}
	echo "*** Building bootstrap bpftool and extrace vmlinux.h"
	if ! [ -s $_KernVmlinuxH ]; then
		# Precompile a minimized bpftool without vmlinux.h, use it to extract vmlinux.h
		# for bootstraping the full feature bpftool
		%{host_make} -C $_KernSrc/tools/bpf/bpftool/ VMLINUX_BTF= VMLINUX_H=
		# Prefer to extract the vmlinux.h from the vmlinux that were just compiled
		# fallback to use host's vmlinux
		# Skip this if bpftools is too old and doesn't support BTF dump
		if $_KernSrc/tools/bpf/bpftool/bpftool btf help 2>&1 | grep -q "\bdump\b"; then
			if grep -q "CONFIG_DEBUG_INFO_BTF=y" ".config"; then
				$_KernSrc/tools/bpf/bpftool/bpftool btf dump file vmlinux format c > $_KernVmlinuxH
			else
				$_KernSrc/tools/bpf/bpftool/bpftool btf dump file /sys/kernel/btf/vmlinux format c > $_KernVmlinuxH
			fi
		fi
		%{host_make} -C $_KernSrc/tools/bpf/bpftool/ clean
	fi
	%endif

	popd
}

BuildPerf() {
	%{perf_make} -C tools/perf all
	%{perf_make} -C tools/perf man
}

BuildTools() {
	%{tools_make} -C tools/power/cpupower CPUFREQ_BENCH=false DEBUG=false

%ifarch x86_64
	%{tools_make} -C tools/power/cpupower/debug/x86_64 centrino-decode powernow-k8-decode
	%{tools_make} -C tools/power/x86/x86_energy_perf_policy
	%{tools_make} -C tools/power/x86/turbostat
	%{tools_make} -C tools/power/x86/intel-speed-select
%endif

	%{tools_make} -C tools/thermal/tmon/
	%{tools_make} -C tools/iio/
	%{tools_make} -C tools/gpio/
	%{tools_make} -C tools/vm/ slabinfo page_owner_sort
}

BuildBpfTool() {
	%{bpftool_make} -C tools/bpf/bpftool
}

%if %{with_core}
{{CONFBUILDSPEC}} # `BuildConfig <.config from CONFSOURCESPEC>`
BuildKernel
%endif

%if %{with_perf}
BuildPerf
%endif

%if %{with_tools}
BuildTools
%endif

%if %{with_bpftool}
BuildBpfTool
%endif

###### Rpmbuild Install Stage ##################################################
%install
%{prepare_buildvar}

InstKernelBasic() {
	####### Basic environment ##################
	# prepare and pushd into the kernel module top dir
	mkdir -p $KernModule
	pushd $KernModule

	####### modules_install ##################
	pushd $_KernBuild
	# Override $(mod-fw) because we don't want it to install any firmware
	# we'll get it from the linux-firmware package and we don't want conflicts
	grep -q "CONFIG_MODULES=y" ".config" && %{kernel_make} mod-fw= modules_install
	# Check again, don't package firmware, use linux-firmware rpm instead
	rm -rf %{buildroot}/lib/firmware
	popd

	####### Prepare kernel modules files for packaging ################
	# Don't package depmod files, they should be auto generated by depmod at rpm -i
	rm -f modules.{alias,alias.bin,builtin.alias.bin,builtin.bin} \
		modules.{dep,dep.bin,devname,softdep,symbols,symbols.bin}

	# Process kernel modules
	find . -name "*.ko" -type f | \
	while read -r _kmodule; do
		# Mark it executable so strip and find-debuginfo can see it
		chmod u+x "$_kmodule"

		# Detect missing or incorrect license tags
		modinfo "$_kmodule" -l | grep -E -qv \
			'GPL( v2)?$|Dual BSD/GPL$|Dual MPL/GPL$|GPL and additional rights$' && \
			echo "Module $_kmodule has incorrect license." >&2 && exit 1

		# Collect module symbol reference info for later usage
		case "$kmodule" in */drivers/*) nm -upA "$_kmodule" ;;
		esac | sed -n 's,^.*/\([^/]*\.ko\):  *U \(.*\)$,\1 \2,p' >> drivers.undef
	done || exit $?

	# Generate a list of modules for block and networking.
	collect_modules_list() {
		sed -r -n -e "s/^([^ ]+) \\.?($2)\$/\\1/p" drivers.undef |
			LC_ALL=C sort -u > $KernModule/modules.$1
		if [ ! -z "$3" ]; then
			sed -r -e "/^($3)\$/d" -i $KernModule/modules.$1
		fi
	}

	collect_modules_list networking \
		'register_netdev|ieee80211_register_hw|usbnet_probe|phy_driver_register|rt(l_|2x00)(pci|usb)_probe|register_netdevice'
	collect_modules_list block \
		'ata_scsi_ioctl|scsi_add_host|scsi_add_host_with_dma|blk_alloc_queue|blk_init_queue|register_mtd_blktrans|scsi_esp_register|scsi_register_device_handler|blk_queue_physical_block_size' \
		'pktcdvd.ko|dm-mod.ko'
	collect_modules_list drm \
		'drm_open|drm_init'
	collect_modules_list modesetting \
		'drm_crtc_init'
	# Finish preparing the kernel module files

	###### Install kernel core components #############################
	mkdir -p %{buildroot}/boot
	install -m 644 $_KernBuild/.config config
	install -m 644 $_KernBuild/.config %{buildroot}/boot/config-$KernUnameR
	install -m 644 $_KernBuild/System.map System.map
	install -m 644 $_KernBuild/System.map %{buildroot}/boot/System.map-$KernUnameR

	# NOTE: If we need to sign the vmlinuz, this is the place to do it.
	%ifarch aarch64
	install -m 644 $_KernBuild/arch/arm64/boot/Image vmlinuz
	%endif

	%ifarch riscv64
	mkdir dtb
	cp $_KernBuild/arch/riscv/boot/dts/*/*.dtb dtb/
	install -m 644 $_KernBuild/arch/riscv/boot/Image vmlinuz
	%endif

	%ifarch x86_64
	install -m 644 $_KernBuild/arch/x86/boot/bzImage vmlinuz
	%endif

	%ifarch loongarch64
	install -m 644 $_KernBuild/vmlinuz vmlinuz
	%endif

	install -m 644 vmlinuz %{buildroot}/boot/vmlinuz-$KernUnameR

	sha512hmac %{buildroot}/boot/vmlinuz-$KernUnameR | sed -e "s,%{buildroot},," > .vmlinuz.hmac
	cp .vmlinuz.hmac %{buildroot}/boot/.vmlinuz-$KernUnameR.hmac

	###### kABI checking and packaging #############################
	# Always create the kABI metadata for use in packaging
	echo "**** GENERATING kernel ABI metadata ****"
	gzip -c9 < $_KernBuild/Module.symvers > symvers.gz
	cp symvers.gz %{buildroot}/boot/symvers-$KernUnameR.gz

	###### End of installing kernel modules and core
	popd
}

CheckKernelABI() {
	echo "**** kABI checking is enabled. ****"
	if ! [ -s "$1" ]; then
		echo "**** But cannot find reference Module.kabi file. ****"
	else
		cp $1 %{buildroot}/Module.kabi
		%{SOURCE30} -k %{buildroot}/Module.kabi -s $_KernBuild/Module.symvers || exit 1
		rm %{buildroot}/Module.kabi
	fi
}

InstKernelDevel() {
	###### Install kernel-devel package ###############################
	### TODO: need tidy up
	### Save the headers/makefiles etc for building modules against.
	# This all looks scary, but the end result is supposed to be:
	# * all arch relevant include/ files
	# * all Makefile/Kconfig files
	# * all script/ files

	# `modules_install` will symlink build to $_KernBuild, and source to $_KernSrc, remove the symlinks first
	rm -rf $KernModule/{build,source}
	mkdir -p $KernModule/{extra,updates,weak-updates}

	# Symlink $KernDevel to kernel module build path
	ln -sf /usr/src/kernels/$KernUnameR $KernModule/build
	ln -sf /usr/src/kernels/$KernUnameR $KernModule/source

	# Start installing kernel devel files
	mkdir -p $KernDevel
	pushd $KernDevel

	# First copy everything
	(cd $_KernSrc; cp --parents $(find . -type f -name "Makefile*" -o -name "Kconfig*") $KernDevel/)
	# Copy built config and sym files
	cp $_KernBuild/Module.symvers .
	cp $_KernBuild/System.map .
	cp $_KernBuild/.config .
	if [ -s $_KernBuild/Module.markers ]; then
		cp $_KernBuild/Module.markers .
	fi

	# We may want to keep Documentation, I got complain from users of missing Makefile
	# of Documentation when building custom module with document.
	# rm -rf build/Documentation
	# Script files
	rm -rf scripts
	cp -a $_KernSrc/scripts .
	cp -a $_KernBuild/scripts .
	find scripts -iname "*.o" -o -iname "*.cmd" -delete

	# Include files
	rm -rf include
	cp -a $_KernSrc/include .
	cp -a $_KernBuild/include/config include/
	cp -a $_KernBuild/include/generated include/

	# SELinux
	mkdir -p security/selinux/
	cp -a $_KernSrc/security/selinux/include security/selinux/

	# Set arch name
	Arch=$(head -4 $_KernBuild/.config | sed -ne "s/.*Linux\/\([^\ ]*\).*/\1/p" | sed -e "s/x86_64/x86/" )

	# Arch include
	mkdir -p arch/$Arch
	cp -a $_KernSrc/arch/$Arch/include arch/$Arch/
	cp -a $_KernBuild/arch/$Arch/include arch/$Arch/

	if [ -d $_KernBuild/arch/$Arch/scripts ]; then
		cp -a $_KernBuild/arch/$Arch/scripts arch/$Arch/ || :
	fi

	# Kernel module build dependency
	if [ -f $_KernBuild/tools/objtool/objtool ]; then
		cp -a $_KernBuild/tools/objtool/objtool tools/objtool/ || :
	fi

	if [ -f $_KernBuild/tools/objtool/fixdep ]; then
		cp -a $_KernBuild/tools/objtool/fixdep tools/objtool/ || :
	fi

	cp -a $_KernSrc/arch/$Arch/*lds arch/$Arch/ &>/dev/null || :
	cp -a $_KernBuild/arch/$Arch/*lds arch/$Arch/ &>/dev/null || :

	mkdir -p arch/$Arch/kernel
	if [ -f $_KernSrc/arch/$Arch/kernel/module.lds ]; then
		cp -a $_KernSrc/arch/$Arch/kernel/module.lds arch/$Arch/kernel/
	fi
	if [ -f $_KernBuild/arch/$Arch/kernel/module.lds ]; then
		cp -a $_KernBuild/arch/$Arch/kernel/module.lds arch/$Arch/kernel/
	fi

	# Copy include/asm-$Arch for better compatibility with some old system
	cp -a $_KernSrc/arch/$Arch include/asm-$Arch

	# Delete obj files
	find include -iname "*.o" -o -iname "*.cmd" -delete

	# Make sure the Makefile and version.h have a matching timestamp so that
	# external modules can be built
	touch -r Makefile include/generated/uapi/linux/version.h
	touch -r .config include/linux/autoconf.h

	# Done
	popd
}

InstKernelHeaders () {
	%{kernel_make} headers_install
	find %{buildroot}/usr/include \
		\( -name .install -o -name .check -o \
		-name ..install.cmd -o -name ..check.cmd \) -delete
}

InstPerf () {
	%{perf_make} -C tools/perf install-bin install-traceevent-plugins install-python_ext install-man

	# remove the 'trace' symlink.
	rm -f %{buildroot}%{_bindir}/trace
}

InstTools() {
	%{tools_make} -C tools/power/cpupower DESTDIR=%{buildroot} libdir=%{_libdir} mandir=%{_mandir} CPUFREQ_BENCH=false install
	rm -f %{buildroot}%{_libdir}/*.{a,la}
	%find_lang cpupower
	mv cpupower.lang ../

%ifarch x86_64
	pushd tools/power/cpupower/debug/x86_64
	install -m755 centrino-decode %{buildroot}%{_bindir}/centrino-decode
	install -m755 powernow-k8-decode %{buildroot}%{_bindir}/powernow-k8-decode
	popd
%endif

	chmod 0755 %{buildroot}%{_libdir}/libcpupower.so*
	mkdir -p %{buildroot}%{_unitdir} %{buildroot}%{_sysconfdir}/sysconfig
	install -m644 %{SOURCE2000} %{buildroot}%{_unitdir}/cpupower.service
	install -m644 %{SOURCE2001} %{buildroot}%{_sysconfdir}/sysconfig/cpupower

%ifarch x86_64
	mkdir -p %{buildroot}%{_mandir}/man8
	%{tools_make} -C tools/power/x86/x86_energy_perf_policy DESTDIR=%{buildroot} install
	%{tools_make} -C tools/power/x86/turbostat DESTDIR=%{buildroot} install
	%{tools_make} -C tools/power/x86/intel-speed-select DESTDIR=%{buildroot} install
%endif

	%{tools_make} -C tools/thermal/tmon install
	%{tools_make} -C tools/iio install
	%{tools_make} -C tools/gpio install

	pushd tools/vm/
	install -m755 slabinfo %{buildroot}%{_bindir}/slabinfo
	install -m755 page_owner_sort %{buildroot}%{_bindir}/page_owner_sort
	popd

%ifarch x86_64
	mkdir -p %{buildroot}%{_sysconfdir}/sysconfig/modules
	install -m755 %{SOURCE2002} %{buildroot}%{_sysconfdir}/sysconfig/modules
%endif
	# with_tools
}

InstBpfTool () {
	%{bpftool_make} -C tools/bpf/bpftool bash_compdir=%{_sysconfdir}/bash_completion.d/ mandir=%{_mandir} install doc-install
}

CollectKernelFile() {
	###### Collect file list #########################################
	pushd %{buildroot}

	# Collect all module files and dirs
	(find lib/modules/$KernUnameR/ -not -type d | sed -e 's/^lib*/\/lib/';
	 find lib/modules/$KernUnameR/ -type d | sed -e 's/^lib*/%dir \/lib/') | sort -n > core.list

	# Do module splitting, filter-modules.sh will generate a list of
	# modules to be split into external module package
	# Rest of the modules stay in core package
	%SOURCE10 "%{buildroot}" "$KernUnameR" "%{_target_cpu}" "$_KernBuild/System.map" non-core-modules >> modules.list || exit $?

	comm -23 core.list modules.list > core.list.tmp
	mv core.list.tmp core.list

	popd

	# Make these file list usable in rpm build dir
	mv %{buildroot}/core.list ../
	mv %{buildroot}/modules.list ../
}

# TK4 Legacy MLNX OFED
BuildInstMLNXOFED() {
	inst_mod() {
		src_mod=$1
		src_mod_name=$(basename "$src_mod")
		dest=""

		# Replace old module
		for mod in $(find $KernModule -name "*$src_mod_name*"); do
			echo "MLNX_OFED: REPLACING: kernel module $mod"
			dest=$(dirname "$mod")
			rm -f "$mod"
		done

		# Install new module
		if [ ! -d "$dest" ]; then
			echo "MLNX_OFED: NEW: kernel module $dest/$mod"
			dest="$KernModule/kernel/drivers/ofed_addon"
			mkdir -p $dest
		fi

		cp -f "$src_mod" "$dest/"
	}

	handle_rpm() {
		rm -rf extracted
		mkdir -p extracted && pushd extracted

		rpm2cpio $1 | cpio -id
		find . -name "*.ko" -or -name "*.ko.xz" | while read -r mod; do
			inst_mod "$mod"
		done
		# find . -name "*.debug" | while read -r mod; do
		#       inst_debuginfo "$mod"
		# done

		popd
	}

	mkdir mlnx-ofed
	pushd mlnx-ofed

	tar -xzvf %{SOURCE3000}
	pushd MLNX_OFED_SRC*

	# Unset $HOME, when doing koji build, koji insert special macros into ~/.rpmmacros that will break MLNX installer:
	# Koji sets _rpmfilename  %%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm,
	# But the installer assumes "_rpmfilename %{_build_name_fmt}" so it will fail to find the built rpm.
	DISTRO=$(echo "%{?dist}" | sed "s/\.//g")
	HOME= ./install.pl --build-only --kernel-only --without-depcheck --distro $DISTRO \
	--kernel $KernUnameR --kernel-sources $KernDevel \
	--without-mlx5_fpga_tools --without-mlnx-rdma-rxe --without-mlnx-nfsrdma \
	--without-mlnx-nvme --without-isert --without-iser --without-srp --without-rshim --without-mdev \
	--disable-kmp

	# get all kernel module rpms that were built against target kernel
	find RPMS -name "*.rpm" -type f | while read -r pkg; do
		if rpm -qlp $pkg | grep "\.ko" | grep -q "$KernelUnameR"; then
			handle_rpm "$(realpath $pkg)"
		fi
	done

	popd

	popd
}

###### Start Kernel Install

%if %{with_core}
InstKernelBasic

%if %{with_kabichk}
{{KABICHECKSPEC}} # `CheckKernelABI <Module.kabi from KABISOURCESPEC>`
%endif

InstKernelDevel
%endif

%if %{with_headers}
InstKernelHeaders
%endif

BuildInstMLNXOFED

%if %{with_perf}
InstPerf
%endif

%if %{with_tools}
InstTools
%endif

%if %{with_bpftool}
InstBpfTool
%endif

CollectKernelFile

###### Debuginfo ###############################################################
%if %{with_debuginfo}

###### Kernel core debuginfo #######
%if %{with_core}
mkdir -p %{buildroot}%{debuginfo_dir}/lib/modules/$KernUnameR
cp -rpf $_KernBuild/vmlinux %{buildroot}%{debuginfo_dir}/lib/modules/$KernUnameR/vmlinux
ln -sf %{debuginfo_dir}/lib/modules/$KernUnameR/vmlinux %{buildroot}/boot/vmlinux-$KernUnameR
%endif

# All binary installation are done here, so run __debug_install_post, then undefine it.
# This triggers find-debuginfo.sh, undefine prevents it from being triggered again
# in post %%install. we do this here because we need to compress and sign modules
# after the debuginfo extraction.
%__debug_install_post

# Delete the debuginfo for kernel-devel files
rm -rf %{buildroot}%{debuginfo_dir}/usr/src

%endif
%undefine __debug_install_post

###### Finally, module sign and compress ######
%if %{with_modsign}
### Sign after debuginfo extration, extraction breaks signature
%{_module_signer} "$KernUnameR" "$_KernBuild" "%{buildroot}" || exit $?
%endif

### Compression after signing, compressed module can't be signed
# Spawn at most 16 workers, at least 2 workers, each worker compress 4 files
NPROC=$(nproc --all)
[ "$NPROC" ] || NPROC=2
[ "$NPROC" -gt 16 ] && NPROC=16
find "$KernModule" -type f -name '*.ko' -print0 | xargs -0r -P${NPROC} -n4 xz -T1;

### Change module path in file lists
for list in ../*.list; do
	sed -i -e 's/\.ko$/\.ko.xz/' $list
done

###### RPM scriptslets #########################################################
### Core package
# Pre
%if %{with_core}
%pre core
# Best effort try to avoid installing with wrong arch
if command -v uname > /dev/null; then
	system_arch=$(uname -m)
	if [ %{_target_cpu} != $system_arch ]; then
		echo "WARN: This kernel is built for %{_target_cpu}. but your system is $system_arch." > /dev/stderr
	fi
fi

%post core
touch %{_localstatedir}/lib/rpm-state/%{name}-%{version}-%{version}%{?dist}.installing_core

%posttrans core
# Weak modules
if command -v weak-modules > /dev/null; then
	weak-modules --add-kernel %{kernel_unamer} || exit $?
fi
# Boot entry and depmod files
if command -v kernel-install > /dev/null; then
	kernel-install add %{kernel_unamer} /lib/modules/%{kernel_unamer}/vmlinuz
elif command -v new-kernel-pkg > /dev/null; then
	new-kernel-pkg --package kernel --install %{kernel_unamer} --kernel-args="crashkernel=512M-12G:128M,12G-64G:256M,64G-128G:512M,128G-:768M" --make-default || exit $?
	new-kernel-pkg --package kernel --mkinitrd --dracut --depmod --update %{kernel_unamer} || exit $?
else
	echo "NOTICE: No available kernel install handler found. Please make sure boot loader and initramfs are properly configured after the installation." > /dev/stderr
fi

# If match, the selinux will be disabled.
is_set_selinux=0
if [ -e /proc/config.gz ]; then
    zcat /proc/config.gz | grep -q "^CONFIG_SECURITY_SELINUX=y"
    is_set_selinux=$?
elif [ -e /boot/config-$(uname -r) ]; then
    cat /boot/config-$(uname -r) | grep -q "^CONFIG_SECURITY_SELINUX=y"
    is_set_selinux=$?
elif [ -e /proc/kallsyms ]; then
    cat /proc/kallsyms | grep -q " selinux_init$"
    is_set_selinux=$?
else
    echo "Ignore selinux adjustments"
fi

# if CONFIG_SECURITY_SELINUX is not set, we should disable selinux.
[ $is_set_selinux -ne 0 ] && {
    echo "Selinux is not supported by current running kernel, disabling SELinux globally to avoid potential system failure."
    echo "Please update /etc/selinux/config manually after testing SELinux functionality. Now setting SELINUX=disabled"
    grep -q "^SELINUX *= *.*$" /etc/selinux/config && sed -ri "s/^ *SELINUX *= *enforcing *$/SELINUX=disabled/" /etc/selinux/config || echo "SELINUX=disabled" >> /etc/selinux/config
}

# Just in case kernel-install didn't depmod
depmod -A %{kernel_unamer}
# Core install done
rm -f %{_localstatedir}/lib/rpm-state/%{name}-%{version}-%{version}%{?dist}.installing_core

%preun core
# Boot entry and depmod files
if command -v kernel-install > /dev/null; then
	kernel-install remove %{kernel_unamer} /lib/modules/%{kernel_unamer}/vmlinuz || exit $?
elif command -v new-kernel-pkg > /dev/null; then
	/sbin/new-kernel-pkg --rminitrd --dracut --remove %{kernel_unamer}
else
	echo "NOTICE: No available kernel uninstall handler found. Please make sure boot loader and initramfs are properly cleared after the uninstallation." > /dev/stderr
fi

# Weak modules
if command -v weak-modules > /dev/null; then
	weak-modules --remove-kernel %{kernel_unamer} || exit $?
fi

### Module package
%post modules
depmod -a %{kernel_unamer}
if [ ! -f %{_localstatedir}/lib/rpm-state/%{name}-%{version}-%{version}%{?dist}.installing_core ]; then
	touch %{_localstatedir}/lib/rpm-state/%{name}-%{version}-%{version}%{?dist}.need_to_run_dracut
fi

%posttrans modules
if [ -f %{_localstatedir}/lib/rpm-state/%{name}-%{version}-%{version}%{?dist}.need_to_run_dracut ]; then\
	dracut -f --kver "%{kernel_unamer}"
	rm -f %{_localstatedir}/lib/rpm-state/%{name}-%{version}-%{version}%{?dist}.need_to_run_dracut
fi

%postun modules
depmod -a %{kernel_unamer}

### Devel package
%post devel
if [ -f /etc/sysconfig/kernel ]; then
	. /etc/sysconfig/kernel || exit $?
fi
# This hardlink merges same devel files across different kernel packages
if [ "$HARDLINK" != "no" -a -x /usr/bin/hardlink -a ! -e /run/ostree-booted ]; then
	(cd /usr/src/kernels/%{kernel_unamer} && /usr/bin/find . -type f | while read -r f; do
		hardlink /usr/src/kernels/*/$f $f > /dev/null
	done)
fi
%endif

### kernel-tools package
%if %{with_tools}
%post -n kernel-tools-libs
/sbin/ldconfig

%postun -n kernel-tools-libs
/sbin/ldconfig
%endif

###### Rpmbuild packaging file list ############################################
### empty meta-package
%if %{with_core}
%files
%{nil}

%files core -f core.list
%defattr(-,root,root)
# Mark files as ghost in case rewritten after install (eg. by kernel-install script)
%ghost /boot/vmlinuz-%{kernel_unamer}
%ghost /boot/.vmlinuz-%{kernel_unamer}.hmac
/boot/System.map-%{kernel_unamer}
/boot/config-%{kernel_unamer}
/boot/symvers-%{kernel_unamer}.gz
# Initramfs will be generated after install
%ghost /boot/initramfs-%{kernel_unamer}%{?dist}.img
# Make depmod files ghost files of the core package
%ghost /lib/modules/%{kernel_unamer}/modules.alias
%ghost /lib/modules/%{kernel_unamer}/modules.alias.bin
%ghost /lib/modules/%{kernel_unamer}/modules.builtin.bin
%ghost /lib/modules/%{kernel_unamer}/modules.builtin.alias.bin
%ghost /lib/modules/%{kernel_unamer}/modules.dep
%ghost /lib/modules/%{kernel_unamer}/modules.dep.bin
%ghost /lib/modules/%{kernel_unamer}/modules.devname
%ghost /lib/modules/%{kernel_unamer}/modules.softdep
%ghost /lib/modules/%{kernel_unamer}/modules.symbols
%ghost /lib/modules/%{kernel_unamer}/modules.symbols.bin

%files modules -f modules.list
%ifarch x86_64
# TK4 Legacy CCISS naming rule
#
# This might be duplicated with tlinux_cciss_link.modules from the old kernel-tlinux package,
# but that's fine since the script itself checks if it's neccessary to re-apply the workaround
%{_sysconfdir}/sysconfig/modules/tlinux_cciss_link_compat.modules
%endif
%defattr(-,root,root)

%files devel
%defattr(-,root,root)
/usr/src/kernels/%{kernel_unamer}

%if %{with_debuginfo}
%files debuginfo -f debuginfo.list
%defattr(-,root,root)
/boot/vmlinux-%{kernel_unamer}
%endif
# with_core
%endif

%if %{with_debuginfo}
%files debuginfo-common-%{_target_cpu} -f debugfiles.list
%defattr(-,root,root)
%endif

%if %{with_headers}
%files headers
%defattr(-,root,root)
/usr/include/*
%endif

%if %{with_perf}
%files -n perf
%defattr(-,root,root)
%{_bindir}/perf
%dir %{_libdir}/traceevent/
%{_libdir}/traceevent/*
%{_libdir}/libperf-jvmti.so
%dir %{_prefix}/lib/perf
%{_prefix}/lib/perf/*
%dir %{_libexecdir}/perf-core
%{_libexecdir}/perf-core/*
%{_datadir}/perf-core/*
%{_mandir}/man[1-8]/perf*
%{_sysconfdir}/bash_completion.d/perf
%{_docdir}/perf-tip/tips.txt
# TODO: Missing doc?
# %%doc linux-%%{kernel_unamer}/tools/perf/Documentation/examples.txt

%files -n python3-perf
%defattr(-,root,root)
%{python3_sitearch}/*

%if %{with_debuginfo}
%files -f perf-debuginfo.list -n perf-debuginfo
%defattr(-,root,root)

%files -f python3-perf-debuginfo.list -n python3-perf-debuginfo
%defattr(-,root,root)
%endif
# with_perf
%endif

%if %{with_tools}
%files -n kernel-tools -f cpupower.lang
%defattr(-,root,root)
%{_bindir}/cpupower
%{_datadir}/bash-completion/completions/cpupower
%ifarch x86_64
%{_bindir}/centrino-decode
%{_bindir}/powernow-k8-decode
%endif
%{_unitdir}/cpupower.service
%{_mandir}/man[1-8]/cpupower*
%config(noreplace) %{_sysconfdir}/sysconfig/cpupower
%ifarch x86_64
%{_bindir}/x86_energy_perf_policy
%{_mandir}/man8/x86_energy_perf_policy*
%{_bindir}/turbostat
%{_mandir}/man8/turbostat*
%{_bindir}/intel-speed-select
%endif
%{_bindir}/tmon
%{_bindir}/iio_event_monitor
%{_bindir}/iio_generic_buffer
%{_bindir}/lsiio
%{_bindir}/lsgpio
%{_bindir}/gpio-*
%{_bindir}/page_owner_sort
%{_bindir}/slabinfo

%files -n kernel-tools-libs
%defattr(-,root,root)
%{_libdir}/libcpupower.so.0
%{_libdir}/libcpupower.so.0.0.1

%files -n kernel-tools-libs-devel
%defattr(-,root,root)
%{_libdir}/libcpupower.so
%{_includedir}/cpufreq.h

%if %{with_debuginfo}
%files -f kernel-tools-debuginfo.list -n kernel-tools-debuginfo
%defattr(-,root,root)
%endif
# with_tools
%endif

%if %{with_bpftool}
%files -n bpftool
%defattr(-,root,root)
%{_sbindir}/bpftool
%{_sysconfdir}/bash_completion.d/bpftool
%{_mandir}/man8/bpftool.8.gz
%{_mandir}/man8/bpftool-*.8.gz
%{_mandir}/man7/bpf-helpers.7.gz

%if %{with_debuginfo}
%files -f bpftool-debuginfo.list -n bpftool-debuginfo
%defattr(-,root,root)
%endif
# with_bpftool
%endif

###### Changelog ###############################################################
%changelog
{{CHANGELOGSPEC}}
