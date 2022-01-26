%global with_debuginfo 0
%global with_perf 1
%global with_tools 1
%if 0%{?rhel} == 6
%global rdist .oc6
%global debug_path /usr/lib/debug/lib/
%else
%global debug_path /usr/lib/debug/usr/lib/
%if 0%{?rhel} == 7
%global rdist .oc7
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

# Architectures we build tools/cpupower on
%define cpupowerarchs x86_64 aarch64

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
Source0: %{name}-%{version}.tar.gz
Source1: tlinux_cciss_link.modules
# Sources for kernel tools
Source2000: cpupower.service
Source2001: cpupower.config
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
%if %{with_tools}
BuildRequires: gettext ncurses-devel asciidoc
%ifnarch s390x
BuildRequires: pciutils-devel
BuildRequires: libcap-devel
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
This package contains OpenCloudOS kernel for arm64 Bare-Metal& VM

%package debuginfo-common
Summary: OpenCloudOS kernel vmlinux for crash debug
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

%if %{with_perf}
%package -n perf
Summary: Performance monitoring for the Linux kernel
Group: Development/System
License: GPLv2
%description -n perf
This package contains the perf tool, which enables performance monitoring
of the Linux kernel.

%package -n perf-debuginfo
Summary: Debug information for package perf
Group: Development/Debug
#Requires: %%{name}-debuginfo-common-%%{_target_cpu} = %%{version}-%%{release}
AutoReqProv: no
%description -n perf-debuginfo
This package provides debug information for the perf package.

# Note that this pattern only works right to match the .build-id
# symlinks because of the trailing nonmatching alternation and
# the leading .*, because of find-debuginfo.sh's buggy handling
# of matching the pattern against the symlinks file.
%{expand:%%global debuginfo_args %{?debuginfo_args} -p '.*%%{_bindir}/perf(\.debug)?|.*%%{_libexecdir}/perf-core/.*|XXX' -o perf-debuginfo.list}

%package -n python-perf
Summary: Python bindings for apps which will manipulate perf events
Group: Development/Libraries
%description -n python-perf
The python-perf package contains a module that permits applications
written in the Python programming language to use the interface
to manipulate perf events.

%{!?python_sitearch: %global python_sitearch %(%{__python} -c "from distutils.sysconfig import get_python_lib; print get_python_lib(1)")}

%package -n python-perf-debuginfo
Summary: Debug information for package perf python bindings
Group: Development/Debug
#Requires: %%{name}-debuginfo-common-%%{_target_cpu} = %%{version}-%%{release}
AutoReqProv: no
%description -n python-perf-debuginfo
This package provides debug information for the perf python bindings.

# the python_sitearch macro should already be defined from above
%{expand:%%global debuginfo_args %{?debuginfo_args} -p '.*%%{python_sitearch}/perf.so(\.debug)?|XXX' -o python-perf-debuginfo.list}


%endif # with_perf

%if %{with_tools}
%package -n kernel-tools
Summary: Assortment of tools for the Linux kernel
Group: Development/System
License: GPLv2
%ifarch %{cpupowerarchs}
Provides:  cpupowerutils = 1:009-0.6.p1
Obsoletes: cpupowerutils < 1:009-0.6.p1
Provides:  cpufreq-utils = 1:009-0.6.p1
Provides:  cpufrequtils = 1:009-0.6.p1
Obsoletes: cpufreq-utils < 1:009-0.6.p1
Obsoletes: cpufrequtils < 1:009-0.6.p1
Obsoletes: cpuspeed < 1:1.5-16
%endif
%description -n kernel-tools
This package contains the tools/ directory from the kernel source
and the supporting documentation.

%package -n kernel-tools-libs
Summary: Libraries for the kernel-tools
Group: Development/System
License: GPLv2
%description -n kernel-tools-libs
This package contains the libraries built from the tools/ directory
from the kernel source.


%endif # with_tools

# prep #########################################################################
%prep

%setup -q -c -T -a 0

# build ########################################################################
%build

cd %{name}-%{version}

all_types="%{kernel_all_types}"
no_sign="yes"
for type_name in $all_types
do
    sed -i '/^EXTRAVERSION/cEXTRAVERSION = -%{extra_version}' Makefile
    objdir=../%{name}-%{version}-obj-${type_name}
    [ -d ${objdir} ] || mkdir ${objdir}
    echo "include  $PWD/Makefile" > ${objdir}/Makefile
    cp ./package/arm/config.${type_name} ${objdir}/.config
    localversion='"'-`echo %{tagged_name}|cut -d- -f3-`%{?dist}'"'
    sed -i -e 's/CONFIG_LOCALVERSION=.*/CONFIG_LOCALVERSION='$localversion'/' ${objdir}/.config 
    make -C ${objdir} olddefconfig > /dev/null
    make -j16 -C ${objdir} RPM_BUILD_MODULE=y %{?jobs:-j %jobs} all > /dev/null

#  dealing with files under lib/modules
    make -j16  -C ${objdir} RPM_BUILD_MODULE=y modules > /dev/null

    %global perf_make make %{?_smp_mflags} -C tools/perf -s V=1 WERROR=0 NO_LIBUNWIND=1 HAVE_CPLUS_DEMANGLE=1 NO_GTK2=1 NO_STRLCPY=1 prefix=%{_prefix}
    %if %{with_perf}
    # perf
    %{perf_make} all
    %{perf_make} man
    %endif

    %if %{with_tools}
    %ifarch %{cpupowerarchs}
    # cpupower
    # make sure version-gen.sh is executable.
    chmod +x tools/power/cpupower/utils/version-gen.sh
    make %{?_smp_mflags} -C tools/power/cpupower CPUFREQ_BENCH=false
    %ifarch x86_64
	pushd tools/power/cpupower/debug/x86_64
	make %{?_smp_mflags} centrino-decode powernow-k8-decode
	popd
    %endif
    %ifarch x86_64
	pushd tools/power/x86/x86_energy_perf_policy/
	make
	popd
	pushd tools/power/x86/turbostat
	make
	popd
    %endif #turbostat/x86_energy_perf_policy
    %endif
    pushd tools
    make tmon
    popd
    %endif

done

# install ######################################################################
%install

echo install %{version}
arch=`uname -m`
if [ "$arch" != "aarch64" ];then
	echo "Unexpected error. Cannot build this rpm in non-aarch64 platform\n"
	exit 1
fi
mkdir -p %buildroot/boot
cd %{name}-%{version}

all_types="%{kernel_all_types}"
for type_name in $all_types
do
    elfname=vmlinuz-%{tagged_name}%{?dist}
    mapname=System.map-%{tagged_name}%{?dist}
    configname=config-%{tagged_name}%{?dist}
    builddir=../%{name}-%{version}-obj-${type_name}
%if 0%{?rhel} == 7
    cp -rpf ${builddir}/arch/arm64/boot/Image %buildroot/boot/${elfname}
%endif
    #mkimage -A arm64 -O linux -T kernel -C none -a 0x80080000 -e 0x80080000 -n %{tagged_name} -d ${builddir}/arch/arm64/boot/Image %buildroot/boot/uImage-%{tagged_name}%{?dist}
    cp -rpf ${builddir}/System.map %buildroot/boot/${mapname}
    cp -rpf ${builddir}/.config %buildroot/boot/${configname}
    cp -rpf ${builddir}/vmlinux %buildroot/boot/vmlinux-%{tagged_name}%{?dist}
    sha512hmac %buildroot/boot/${elfname} | sed -e "s,$RPM_BUILD_ROOT,," > %buildroot/boot/.vmlinuz-%{tagged_name}%{?dist}.hmac

    # dealing with kernel-devel package
    objdir=${builddir}
    #KernelVer=%{version}-%{title}-%{release_os}-${type_name}
    KernelVer=%{tagged_name}%{?dist}

    ####### make kernel-devel package: methods comes from rhel6(kernel.spec) ####### 
    make -C ${objdir} RPM_BUILD_MODULE=y INSTALL_MOD_PATH=%buildroot modules_install > /dev/null
	#don't package firmware, use linux-firmware rpm instead
	rm -rf %buildroot/lib/firmware
    # And save the headers/makefiles etc for building modules against
    #
    # This all looks scary, but the end result is supposed to be:
    # * all arch relevant include/ files
    # * all Makefile/Kconfig files
    # * all script/ files
    rm -rf %buildroot/lib/modules/${KernelVer}/build
    rm -rf %buildroot/lib/modules/${KernelVer}/source
    mkdir -p %buildroot/lib/modules/${KernelVer}/build
    (cd $RPM_BUILD_ROOT/lib/modules/${KernelVer} ; ln -s build source)
    mkdir -p %buildroot/lib/modules/${KernelVer}/source/arch/arm64/kernel && cp -pf arch/arm64/kernel/module.lds %buildroot/lib/modules/${KernelVer}/source/arch/arm64/kernel/
    # dirs for additional modules per module-init-tools, kbuild/modules.txt
    mkdir -p %buildroot/lib/modules/${KernelVer}/extra
    mkdir -p %buildroot/lib/modules/${KernelVer}/updates
    mkdir -p %buildroot/lib/modules/${KernelVer}/weak-updates

%if 0%{?rhel} == 8
    cp -rpf ${builddir}/arch/arm64/boot/Image %buildroot/lib/modules/${KernelVer}/vmlinuz
%endif

    # first copy everything
    cp --parents `find  -type f -name "Makefile*" -o -name "Kconfig*"` %buildroot/lib/modules/${KernelVer}/build
    cp ${builddir}/Module.symvers %buildroot/lib/modules/${KernelVer}/build
    cp ${builddir}/System.map %buildroot/lib/modules/${KernelVer}/build

    if [ -s ${builddir}/Module.markers ]; then
      cp ${builddir}/Module.markers %buildroot/lib/modules/${KernelVer}/build
    fi

    # create the kABI metadata for use in packaging
    echo "**** GENERATING kernel ABI metadata ****"
    gzip -c9 < ${builddir}/Module.symvers > %buildroot/boot/symvers-${KernelVer}.gz
    #   chmod 0755 %_sourcedir/kabitool
    #   rm -f %{_tmppath}/kernel-$KernelVer-kabideps
    #   %_sourcedir/kabitool -s Module.symvers -o %{_tmppath}/kernel-$KernelVer-kabideps

%if %{with_kabichk}
    echo "**** kABI checking is enabled in kernel SPEC file. ****"
    chmod 0755 scripts/check-kabi
    if [ -e package/arm/Module.kabi_%{_target_cpu}$Flavour ]; then
        scripts/check-kabi -k package/arm/Module.kabi_%{_target_cpu}$Flavour -s ${builddir}/Module.symvers || exit 1
        echo "**** kABI checking SUCCESS. ****"
    else
        echo "**** NOTE: Cannot find reference Module.kabi file. ****"
	exit 1
    fi
%endif

    rm -rf %buildroot/lib/modules/${KernelVer}/build/Documentation
    rm -rf %buildroot/lib/modules/${KernelVer}/build/scripts
    rm -rf %buildroot/lib/modules/${KernelVer}/build/include
    cp ${builddir}/.config %buildroot/lib/modules/${KernelVer}/build
    cp -a scripts %buildroot/lib/modules/${KernelVer}/build
    cp -a ${builddir}/scripts %buildroot/lib/modules/${KernelVer}/build
    rm -f %buildroot/lib/modules/${KernelVer}/build/scripts/*.o
    rm -f %buildroot/lib/modules/${KernelVer}/build/scripts/*/*.o
    cp -a --parents arch/arm64/include %buildroot/lib/modules/${KernelVer}/build/
    (cd ${builddir} ; cp -a --parents arch/arm64/include/generated %buildroot/lib/modules/${KernelVer}/build/)
    mkdir -p %buildroot/lib/modules/${KernelVer}/build/include

    cp -a ${builddir}/include/config %buildroot/lib/modules/${KernelVer}/build/include/
    cp -a ${builddir}/include/generated  %buildroot/lib/modules/${KernelVer}/build/include/

    cd include
    cp -a acpi asm-generic clocksource crypto drm dt-bindings keys kvm linux math-emu media misc net pcmcia ras rdma scsi soc sound target trace uapi video xen %buildroot/lib/modules/${KernelVer}/build/include

    cp -a ../arch/arm64 $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include/asm-arm64
    pushd $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include
    ln -s asm-arm64 asm
    popd
    
    # Make sure the Makefile and version.h have a matching timestamp so that
    # external modules can be built
    touch -r %buildroot/lib/modules/${KernelVer}/build/Makefile %buildroot/lib/modules/${KernelVer}/build/include/generated/uapi/linux/version.h
    #touch -r %buildroot/lib/modules/${KernelVer}/build/.config %buildroot/lib/modules/${KernelVer}/build/include/linux/autoconf.h
	# Copy .config to include/config/auto.conf so "make prepare" is unnecessary.
    cp %buildroot/lib/modules/$KernelVer/build/.config %buildroot/lib/modules/${KernelVer}/build/include/config/auto.conf
    cd ..


    find %buildroot/lib/modules/${KernelVer} -name "*.ko" -type f >modnames


    mkdir -p %buildroot%debug_path/modules/${KernelVer}
    cp -r %buildroot/lib/modules/${KernelVer}/kernel %buildroot%debug_path/modules/${KernelVer}
    xargs --no-run-if-empty strip -g < modnames
    
    xargs --no-run-if-empty chmod u+x < modnames

    # Generate a list of modules for block and networking.
    fgrep /drivers/ modnames | xargs --no-run-if-empty nm -upA |
    sed -n 's,^.*/\([^/]*\.ko\):  *U \(.*\)$,\1 \2,p' > drivers.undef

	collect_modules_list()
    {
      sed -r -n -e "s/^([^ ]+) \\.?($2)\$/\\1/p" drivers.undef |
      LC_ALL=C sort -u > $RPM_BUILD_ROOT/lib/modules/$KernelVer/modules.$1
      if [ ! -z "$3" ]; then
        sed -r -e "/^($3)\$/d" -i $RPM_BUILD_ROOT/lib/modules/$KernelVer/modules.$1
      fi
    }

    collect_modules_list networking 'register_netdev|ieee80211_register_hw|usbnet_probe|phy_driver_register|rt2x00(pci|usb)_probe|register_netdevice'
    collect_modules_list block 'ata_scsi_ioctl|scsi_add_host|scsi_add_host_with_dma|blk_alloc_queue|blk_init_queue|register_mtd_blktrans|scsi_esp_register|scsi_register_device_handler|blk_queue_physical_block_size' 'pktcdvd.ko|dm-mod.ko'
    collect_modules_list drm 'drm_open|drm_init'
    collect_modules_list modesetting 'drm_crtc_init'
    # detect missing or incorrect license tags
    rm -f modinfo
    while read i
    do
      echo -n "${i#%buildroot/lib/modules/${KernelVer}/} " >> modinfo
      /sbin/modinfo -l $i >> modinfo
    done < modnames

    egrep -v \
          'GPL( v2)?$|Dual BSD/GPL$|Dual MPL/GPL$|GPL and additional rights$' \
          modinfo && exit 1

    rm -f modinfo modnames
    # remove files that will be auto generated by depmod at rpm -i time
    #for i in alias alias.bin ccwmap dep dep.bin ieee1394map inputmap isapnpmap ofmap pcimap seriomap symbols symbols.bin usbmap
    #do
    #  rm -f %buildroot/lib/modules/${KernelVer}/modules.$i
    #done

    # Move the devel headers out of the root file system
    mkdir -p %buildroot/usr/src/kernels
    mv %buildroot/lib/modules/${KernelVer}/build %buildroot/usr/src/kernels/${KernelVer}
    ln -sf /usr/src/kernels/${KernelVer} %buildroot/lib/modules/${KernelVer}/build
    #
    # Generate the kernel-modules-* files lists
    #

%if 0%{?rhel} == 8
    mkdir -p %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.symbols.bin %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.symbols %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.softdep %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.devname %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.dep.bin %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.dep %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.builtin.bin %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.alias.bin %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.alias %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/boot/$configname %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/boot/System.map-$KernelVer %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/boot/.vmlinuz-%{tagged_name}%{?dist}.hmac %buildroot/lib/modules/$KernelVer/dist_compat
%endif


    env -i PATH=$PATH LD_LIBRARY_PATH=$LD_LIBRARY_PATH HOME=$HOME USER=$USER bash -c "$PWD/package/arm/mlx/mlnx_ofed_integrate_into_kernel.sh $KernelVer $PWD %buildroot"

    # Copy the System.map file for depmod to use, and create a backup of the
    # full module tree so we can restore it after we're done filtering
    cp ${builddir}/System.map $RPM_BUILD_ROOT/.
    pushd $RPM_BUILD_ROOT
    mkdir restore
    cp -r lib/modules/$KernelVer/* restore/.

    find lib/modules/$KernelVer/ -not -type d | sort -n > modules.list
    find lib/modules/$KernelVer/ -type d | sort -n > module-dirs.list

    # Run depmod on the resulting module tree and make sure it isn't broken
    depmod -b . -aeF ./System.map $KernelVer &> depmod.out
    if [ -s depmod.out ]; then
        echo "Depmod failure"
        cat depmod.out
        exit 1
    else
        rm depmod.out
    fi

    # Cleanup
    rm System.map
    cp -r restore/* lib/modules/$KernelVer/.
    rm -rf restore
    popd

%if 0%{?rhel} == 8
    mv %buildroot/lib/modules/$KernelVer/modules.symbols.bin %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.symbols %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.softdep %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.devname %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.dep.bin %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.dep %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.builtin.bin %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.alias.bin %buildroot/lib/modules/$KernelVer/dist_compat
    mv %buildroot/lib/modules/$KernelVer/modules.alias %buildroot/lib/modules/$KernelVer/dist_compat
%endif

    # Make sure the files lists start with absolute paths or rpmbuild fails.
    # Also add in the dir entries
    sed -e 's/^lib*/%dir \/lib/' $RPM_BUILD_ROOT/module-dirs.list > ../core.list
    sed -e 's/^lib*/\/lib/' $RPM_BUILD_ROOT/modules.list >> ../core.list

    # Cleanup
    rm -f $RPM_BUILD_ROOT/modules.list
    rm -f $RPM_BUILD_ROOT/module-dirs.list

    %if %{with_perf}
    # perf tool binary and supporting scripts/binaries
    %{perf_make} DESTDIR=$RPM_BUILD_ROOT install

    # perf-python extension
    %{perf_make} DESTDIR=$RPM_BUILD_ROOT install-python_ext

    # perf man pages (note: implicit rpm magic compresses them later)
    %{perf_make} DESTDIR=$RPM_BUILD_ROOT install-man
    %endif

    %if %{with_tools}
    %ifarch %{cpupowerarchs}
    make -C tools/power/cpupower DESTDIR=$RPM_BUILD_ROOT libdir=%{_libdir} mandir=%{_mandir} CPUFREQ_BENCH=false install
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
    %ifarch %{ix86} x86_64
	mkdir -p %{buildroot}%{_mandir}/man8
	pushd tools/power/x86/x86_energy_perf_policy
	make DESTDIR=%{buildroot} install
	popd
	pushd tools/power/x86/turbostat
	make DESTDIR=%{buildroot} install
	popd
    %endif #turbostat/x86_energy_perf_policy
    pushd tools/thermal/tmon
    make INSTALL_ROOT=%{buildroot} install
    popd
    %endif
    %endif
done

mkdir -p %buildroot/etc/sysconfig/modules
install -m755 %SOURCE1 %buildroot/etc/sysconfig/modules

#### Header
make  INSTALL_HDR_PATH=$RPM_BUILD_ROOT/usr headers_install
# Do headers_check but don't die if it fails.
make  INSTALL_HDR_PATH=$RPM_BUILD_ROOT/usr headers_check \
     > hdrwarnings.txt || :
if grep -q exist hdrwarnings.txt; then
   sed s:^$RPM_BUILD_ROOT/usr/include/:: hdrwarnings.txt
   # Temporarily cause a build failure if header inconsistencies.
   # exit 1
fi

find $RPM_BUILD_ROOT/usr/include \
     \( -name .install -o -name .check -o \
     	-name ..install.cmd -o -name ..check.cmd \) | xargs rm -f

# glibc provides scsi headers for itself, for now
rm -rf $RPM_BUILD_ROOT/usr/include/scsi
rm -f $RPM_BUILD_ROOT/usr/include/asm*/atomic.h
rm -f $RPM_BUILD_ROOT/usr/include/asm*/io.h
rm -f $RPM_BUILD_ROOT/usr/include/asm*/irq.h

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
%if 0%{?rhel} == 8
cp -p /lib/modules/%{tagged_name}/dist_compat/config-%{tagged_name}%{?dist} /boot
cp -p /lib/modules/%{tagged_name}/dist_compat/.vmlinuz-%{tagged_name}%{?dist}.hmac /boot
cp -p /lib/modules/%{tagged_name}/dist_compat/System.map-%{tagged_name}%{?dist} /boot
kernel-install add %{tagged_name} /lib/modules/%{tagged_name}/vmlinuz
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

%files -f core.list
# files ########################################################################
%defattr(-,root,root)
%if 0%{?rhel} == 7
/boot/vmlinuz-%%{tagged_name}%%{?dist}
/boot/.vmlinuz-%%{tagged_name}%%{?dist}.hmac
#/boot/uImage-%%{tagged_name}%%{?dist}
/boot/System.map-%%{tagged_name}%%{?dist}
/boot/config-%%{tagged_name}%%{?dist}
%endif
/boot/symvers-%{tagged_name}%{?dist}*
/etc/sysconfig/modules/tlinux_cciss_link.modules
#/lib/modules/%%{tagged_name}/vmlinuz
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

%if %{with_perf}
%files -n perf
%defattr(-,root,root)
%{_bindir}/*
/usr/lib64/*
/usr/lib/traceevent/plugins/*.so
/usr/lib/perf/examples/bpf/*
/usr/lib/perf/include/bpf/*
/usr/share/*
%dir %{_libexecdir}/perf-core
%{_libexecdir}/perf-core/*
%{_sysconfdir}/bash_completion.d/perf

%files -n python-perf
%defattr(-,root,root)
%{python_sitearch}

%if %{with_debuginfo}
%files -f perf-debuginfo.list -n perf-debuginfo
%defattr(-,root,root)

%files -f python-perf-debuginfo.list -n python-perf-debuginfo
%defattr(-,root,root)
%endif
%endif # with_perf

%if %{with_tools}
%files -n kernel-tools -f cpupower.lang
%defattr(-,root,root)
%ifarch %{cpupowerarchs}
%{_bindir}/cpupower
%ifarch x86_64
%{_bindir}/centrino-decode
%{_bindir}/powernow-k8-decode
%endif
%{_unitdir}/cpupower.service
%{_mandir}/man[1-8]/cpupower*
%config(noreplace) %{_sysconfdir}/sysconfig/cpupower
%ifarch %{ix86} x86_64
%{_bindir}/x86_energy_perf_policy
%{_mandir}/man8/x86_energy_perf_policy*
%{_bindir}/turbostat
%{_mandir}/man8/turbostat*
%endif
%endif
%{_bindir}/tmon

%ifarch %{cpupowerarchs}
%files -n kernel-tools-libs
%defattr(-,root,root)
%{_libdir}/libcpupower.so.0
%{_libdir}/libcpupower.so.0.0.1
%endif
%endif # with_tools

# changelog  ###################################################################
%changelog
* Mon Mar 11 2019 Jia Wang <jasperwang@tencent.com>
 - Initial 4.14.99 repository
