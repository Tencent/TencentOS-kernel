# SPDX-License-Identifier: GPL-2.0
#
# A simple and clean build system initially inspired by Fedora ARK Kernel and Tencent Linux Kernel public.
#

TOPDIR := $(shell git rev-parse --show-toplevel)
ifeq ("$(TOPDIR)", "")
	TOPDIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
endif

DISTPATH = dist
DISTDIR := $(TOPDIR)/$(DISTPATH)
ifeq ("$(abspath $(DISTDIR)/Makefile)", "$(lastword $(MAKEFILE_LIST))")
$(error Can't detect Makefile, aborting)
endif

### Downstream marker, update this when forking to another downstream
KDIST =
### NOTE: This export only applies to sub targets, for global commands, export KDIST explicitly in shell.
export KDIST := $(KDIST)

### Vendor mark
VENDOR = tencent
VENDOR_CAPITALIZED = Tencent
URL = https://github.com/Tencent/TencentOS-kernel

### Get native arch for binary build by default
NATIVE_ARCH := $(shell uname -m | sed -e 's/amd64/x86_64/;s/arm64/aarch64/;s/*86$$/x86/')

###### Build parameters, change them with `make <PARAM>=<VALUE>` ######
# When building binary package, which arch to build against
ARCH := $(NATIVE_ARCH)
# ARCH to be covered by spec file
SPEC_ARCH := x86_64 aarch64 riscv64
# Kernel variant, will be appended to kernel release string as suffix
# to indicate this is a variant kernel (eg. debug; minimal;)
VARIANT :=
# Which kernel config to use, this build system supports multiple config targets,
# Get the available config by scripts/ls-config.sh
CONFIG := generic-release
# Build a specific tag/commit with `make TAG=<tag>` or `make COMMIT=<commit>`
TAG := $(COMMIT)
# Extra RPM flags
RPMFLAGS :=
####### Build parameters end ######

### Basic variables
DISTCONFIGDIR = $(DISTDIR)/configs
DISTWORKDIR = $(DISTDIR)/workdir

DISTTEMPLATES = $(wildcard $(DISTDIR)/templates/*)
DISTCONFIGS = $(shell find $(DISTDIR)/configs -type f)
DISTSCRIPTS = $(wildcard $(DISTDIR)/scripts/*)
DISTSOURCES = $(wildcard $(DISTDIR)/sources/*)
DISTKABIS = $(wildcard $(DISTDIR)/kabi/*)
DISTFILES = $(DISTDIR) $(DISTTEMPLATES) $(DISTCONFIGS) $(DISTSCRIPTS) $(DISTSOURCES) $(DISTKABIS)

### Force unify arch name, so Kbuild, and other Makefiles can work seamlessly
# BUILD_ARCH is for RPM, Dist make
BUILD_ARCH := $(ARCH)
override BUILD_ARCH := $(shell echo $(BUILD_ARCH) | sed -e 's/amd64/x86_64/;s/arm64/aarch64/;s/*86$$/x86/')
# ARCH is for Kbuild
override ARCH := $(shell echo $(BUILD_ARCH) | sed -e 's/amd64/x86_64/;s/aarch64/arm64/;s/*86$$/x86/')

### Check if TAG is valid
TAG := HEAD
# If COMMIT is specified, this line won't take effect
COMMIT := $(shell git rev-parse --verify --quiet $(TAG))
ifeq ($(COMMIT),)
$(error Invalid git reference, tag '$(TAG)' commit '$(COMMIT)', aborting)
endif
# Prefer using TAG, if not set, use COMMIT
TAG := $(COMMIT)
GITREF := $(TAG)

### Build files
KFULLVER := $(shell KDIST=$(KDIST) $(DISTDIR)/scripts/get-version.sh $(GITREF) vr)
CONFIGFILE := $(shell KDIST=$(KDIST) $(DISTDIR)/scripts/ls-config-files.sh $(CONFIG))
WORKDIRS = $(DISTWORKDIR) $(RPM_TOPDIR) $(RPM_BUILDDIR) $(RPM_RPMDIR) $(RPM_SOURCEDIR) $(RPM_SPECDIR) $(RPM_SRCRPMDIR) $(RPM_BUILDROOTDIR)
SPECFILE = $(RPM_SOURCEDIR)/kernel.spec
TARFILE = $(RPM_SOURCEDIR)/kernel-$(KFULLVER).tar
TESTPATCH = $(RPM_SOURCEDIR)/linux-kernel-test.patch
ifeq ($(CONFIGFILE),)
$(error Invalid CONFIG value '$(CONFIG)', no matching config target found)
endif

### RPM files
RPM_TOPDIR := $(DISTDIR)/rpm
RPM_BUILDDIR := $(RPM_TOPDIR)/BUILD
RPM_RPMDIR := $(RPM_TOPDIR)/RPMS
RPM_SOURCEDIR := $(RPM_TOPDIR)/SOURCES
RPM_SPECDIR := $(RPM_TOPDIR)/SPECS
RPM_SRCRPMDIR := $(RPM_TOPDIR)/SRPMS
RPM_BUILDROOTDIR := $(RPM_TOPDIR)/BUILDROOT

### RPM build options
# All params enabled by default (except kABI check, see below), DEFAULT_ENABLED overrides DEFAULT_DISABLE.
DEFAULT_DISABLED=
DEFAULT_ENABLED=

## A few shortcut for commonly used params:
# Disable KABI check by default
KABI=0
ifeq ($(KABI), 0)
override DEFAULT_DISABLED := kabichk $(DEFAULT_DISABLED)
endif

# Enabled module sign by default
MODSIGN=1
ifeq ($(MODSIGN), 0)
override DEFAULT_DISABLED := modsign $(DEFAULT_DISABLED)
endif

# Allow to skip RPM dependency check
NODEP=0
ifeq ($(NODEP), 1)
override RPMFLAGS := --nodeps $(RPMFLAGS)
endif

# Crossbuild
ifneq ($(BUILD_ARCH),$(NATIVE_ARCH))
RPMCROSSFLAGS = --with crossbuild --target $(BUILD_ARCH) --define "_cross_compile $(BUILD_ARCH)-linux-gnu-"
else
RPMCROSSFLAGS = --without crossbuild
endif

# For re-distribute to another distro
ifneq ($(DIST),)
override RPMFLAGS := --define "dist .$(DIST)" $(RPMFLAGS)
$(info "NOTE: DIST is set, building for another distro $(DIST)")
$(info "      You shoudn't do this unless you know what you are doing.")
endif

# Always make sure workdir exists
$(shell mkdir -p $(WORKDIRS))

default: dist-help

$(TARFILE):
	@echo "Generating kernel source tar: $(TARFILE)"
	@cd $(TOPDIR); git archive $(GITREF) $(TOPDIR) --format=tar --prefix=kernel-$(KFULLVER)/ --output $(TARFILE)

dist-tarball: $(TARFILE)
	@echo "$(TARFILE)"

$(CONFIGFILE): $(DISTFILES)
	@echo "Generating kernel config style '$(CONFIG)'"
	@$(DISTDIR)/scripts/gen-configs.sh $(CONFIG)

dist-configs: $(CONFIGFILE)
dist-config: dist-configs
	rm -f $(TOPDIR)/.config
	cp $(RPM_SOURCEDIR)/$(CONFIG).$(BUILD_ARCH).config $(TOPDIR)/.config
	@printf "\033[0;32mDefault kernel config copied as $(TOPDIR)/.config\033[0m\n"

# TODO: Build from a unclean tree is not working yet
# which can't be detected by make.
# $(TESTPATCH): always-rebuild
# 	@git diff --no-renames HEAD -- ":(exclude)$(DISTDIR)" > $(TESTPATCH)
# 	@[ -s "$(TESTPATCH)" ] && echo "Building from a unclean tree" || :

# TODO: Remove always-rebuild - currently these targets depend on git worktree or variables,
.PHONY: always-rebuild
$(SPECFILE): always-rebuild
	@echo "Generating kernel RPM spec: $(SPECFILE)"
	@$(DISTDIR)/scripts/gen-spec.sh \
		--gitref "$(GITREF)" \
		--build-arch "$(SRPM_ARCH)" \
		--kernel-config "$(CONFIG)" \
		--kernel-variant "$(VARIANT)" \
		--set-default-disabled "$(DEFAULT_DISABLED)" \
		--set-default-enabled "$(DEFAULT_ENABLED)" \
		> $(SPECFILE)
	@grep -A2 "# == Package options ==" $(SPECFILE) | cut -c3-

dist-specfile: $(SPECFILE) $(TOPDIR)/.config
	@echo "$(SPECFILE)"

dist-sources: dist-configs $(TARFILE) $(DISTSOURCES) $(DISTKABIS) $(SPECFILE)
	@cp $(DISTSOURCES) $(DISTKABIS) $(RPM_SOURCEDIR)

define DO_RPMBUILD
@echo "=== DISTBUILD ==="
@echo "Building kernel: $(shell rpmspec -q --qf "%{name}-%{version}-%{release}\n" --srpm $(SPECFILE))"
@echo "Kernel uname-r: $(shell rpmspec -q --provides $(SPECFILE) | grep kernel-uname-r | awk -F ' = ' '{print $$2}')"
@echo "Config style: $(CONFIG)"
@echo "RPM build flags: $(1)"
@echo "=== RPMBULID ==="
rpmbuild \
	--define '_topdir $(RPM_TOPDIR)' \
	--define '_builddir $(RPM_BUILDDIR)' \
	--define '_rpmdir $(RPM_RPMDIR)' \
	--define '_sourcedir $(RPM_SOURCEDIR)' \
	--define '_specdir $(RPM_SPECDIR)' \
	--define '_srcrpmdir $(RPM_SRCRPMDIR)' \
	--define '_buildrootdir $(RPM_BUILDROOTDIR)' \
	$(SPECFILE) $(1)
endef

dist-srpm: dist-sources
	$(call DO_RPMBUILD,-bs --nodeps --rmsource --rmspec $(RPMFLAGS))

dist: dist-rpm
dist-rpms: dist-rpm
dist-rpm: dist-sources
	$(call DO_RPMBUILD,-bb $(RPMCROSSFLAGS) --rmsource --rmspec $(RPMFLAGS))

dist-prep: dist-sources
	$(call DO_RPMBUILD,-bp --nodeps $(RPMCROSSFLAGS) $(RPMFLAGS))

dist-new-release:
	@$(DISTDIR)/scripts/make-release.sh

dist-new-sub-release:
	@$(DISTDIR)/scripts/make-release.sh --sub-release

dist-clean:
	@for i in $(RPM_TOPDIR)/* $(DISTWORKDIR); do \
		echo Cleaning up $$i; \
		rm -rf $$i/*; \
		rm -rf $$i/.* 2>/dev/null; \
	done; :;

BUILDDEPS=$(shell rpmspec -q --buildrequires $(SPECFILE) | cut -d ' ' -f 1)
MISSINGDEPS=$(shell echo "$(BUILDDEPS)" | xargs -n1 echo | while read -r _d; do rpm -q --whatprovides "$$_d" >/dev/null || echo "$$_d"; done)
dist-check-buildrequires: $(SPECFILE)
	@if [ -n "$(MISSINGDEPS)" ]; then \
		echo "Error: Build dependency packages missing, please install: $(MISSINGDEPS)"; \
		echo "Hint: You can try run \`make dist-install-buildrequires\` to fix this."; \
		exit 1; \
		fi;

dist-install-buildrequires: $(SPECFILE)
	@if [ -n "$(MISSINGDEPS)" ]; then \
		echo "Installing kernel build dependency '$(MISSINGDEPS)' using yum..."; \
		echo "Missing dependency packages: '$(MISSINGDEPS)...'"; \
		if [ -x /usr/bin/yum ]; then \
		echo "Trying to install..."; \
		echo "$(MISSINGDEPS)" | sudo xargs yum install -y && exit 0; \
		else \
		echo "Yum is not available for current user."; \
		fi; \
		echo "Error: Intallation failed."; \
		exit 1; \
		fi

dist-check-requires: dist-check-buildrequires
	@if [ ! -x /usr/bin/python3 ]; then \
		echo "ERROR: Python 3 is required." ; \
		exit 1; \
		fi
	@if [ ! -x /usr/bin/git ]; then \
		echo "ERROR: Git is required." ; \
		exit 1; \
		fi

dist-format-config:
	@$(DISTDIR)/scripts/format-configs.sh
dist-format-configs: dist-format-config

dist-check-new-config:
	@echo "Checking for unset Kconfig..."
	@$(DISTDIR)/scripts/check-configs.sh check-new-configs $(CONFIG)
dist-check-new-configs: dist-check-new-config

dist-fix-new-config:
	@echo "Fixing unset Kconfig issue..."
	@$(DISTDIR)/scripts/check-configs.sh check-new-configs --autofix $(CONFIG)
dist-fix-new-configs: dist-fix-new-config

dist-check-dup-config:
	@echo "Checking for duplicated Kconfig..."
	@$(DISTDIR)/scripts/check-configs.sh check-dup-configs $(CONFIG)
dist-check-dup-configs: dist-check-dup-config

dist-fix-dup-config:
	@echo "Fixing duplicated Kconfig issue..."
	@$(DISTDIR)/scripts/check-configs.sh check-dup-configs --autofix $(CONFIG)
dist-fix-dup-configs: dist-fix-dup-config

dist-check-diff-config:
	@echo "Checking for duplicated Kconfig..."
	@$(DISTDIR)/scripts/check-configs.sh check-diff-configs $(CONFIG)
dist-check-diff-configs: dist-check-diff-config

dist-check-config: dist-check-new-config dist-check-dup-config dist-check-diff-config
dist-check-configs: dist-check-config

dist-check-commit:
	@echo "Checking for new commits..."
	@$(DISTDIR)/scripts/check-commits.sh
dist-check-commits: dist-check-commit

dist-check-tag:
	@echo "Checking tag of '$(TAG)' ..."
	@$(DISTDIR)/scripts/check-tag.sh '$(TAG)'

dist-check-kabi: dist-sources
	@echo "Checking kABI for $(BUILD_ARCH)..."
	$(call DO_RPMBUILD,-bi $(RPMCROSSFLAGS) \
		--without doc --without headers --without perf --without tools --without bpftool \
		--without debuginfo --without selftest --without modsign \
		--with kabichk \
	)

dist-update-kabi: KABI_WORKDIR:=$(shell mktemp -d $(DISTWORKDIR)/kabi.XXXX)
dist-update-kabi: RPM_BUILDDIR:=$(KABI_WORKDIR)
dist-update-kabi: dist-sources dist-check-buildrequires
	@echo "Updating kABI for $(BUILD_ARCH)..."
	$(call DO_RPMBUILD,-bc $(RPMCROSSFLAGS) \
		--without doc --without headers --without perf --without tools --without bpftool \
		--without debuginfo --without selftest --without modsign --without kabichk \
	)
	MODSYM=$$(find $(KABI_WORKDIR) -maxdepth 3 -type f -name 'Module.symvers'); \
	if [ -f "$$MODSYM" ]; then \
		echo "Using $$MODSYM as kABI reference file."; \
		$(DISTDIR)/scripts/helper/update-kabi.sh \
		$(DISTDIR)/kabi/Module.kabi_$(BUILD_ARCH) $$MODSYM > $(KABI_WORKDIR)/Module.kabi.updated; \
		cp $(KABI_WORKDIR)/Module.kabi.updated $(DISTDIR)/kabi/Module.kabi_$(BUILD_ARCH); \
	else \
		echo "Failed to find Module.symvers."; \
		exit 1; \
	fi

# Do a fast check
dist-check: dist-check-commit dist-check-new-config
dist-help:
	@echo  'This helps you to manage, release, and develop $(VENDOR_CAPITALIZED) Linux kernel.'
	@echo  'Use below make targets as sub-command:'
	@echo
	@echo  'NOTE: Before you submit any patch, please see the "Sanity check" targets and at lease run:'
	@echo  '`make dist-check`'
	@echo  'And ensure there is no error.'
	@echo
	@echo  'For building a kernel RPM distrobution package:'
	@echo  '  dist, dist-rpms      - Alias to dist-rpm.'
	@echo  '  dist-rpm             - Create the binary RPMS for the kernel and put it under:'
	@echo  '                         $(RPM_RPMDIR)'
	@echo  '  dist-srpm            - Create a source RPM and put it under:'
	@echo  '                         $(RPM_SRCRPMDIR)'
	@echo  '  dist-prep            - Prep the kernel dist build source code under:'
	@echo  '                         $(RPM_BUILDDIR)'
	@echo
	@echo  'Available params (most of these params are usable for all sub-commands besides dist-rpm/dist-srpm):'
	@echo  '  ARCH="$(ARCH)"'
	@echo  '    Target ARCH used for binary/RPM build.'
	@echo  '  SPEC_ARCH="$(SPEC_ARCH)"'
	@echo  '    Targer ARCH coverted by spec/SRPM build.'
	@echo  '  TAG="$(TAG)" (or COMMIT="$(COMMIT)")'
	@echo  '    Specify a git tag or commit, and this Makefile will build the kernel from that version.'
	@echo  '  VARIANT="$(VARIANT)"'
	@echo  '    Kernel variant, will be appended to kernel release string as suffix'
	@echo  '    to indicate this is a variant kernel (eg. debug; minimal;), especially'
	@echo  '    useful when combined with CONFIG=<variant config>'
	@echo  '  RPMFLAGS="$(RPMFLAGS)"'
	@echo  '    Extra RPM flags to be passed to rpmbuild for RPM bulding related commands.'
	@echo  '  CONFIG="$(CONFIG)"'
	@echo  '    Which kernel config to use, $(VENDOR_CAPITALIZED) build system supports multiple config targets,'
	@echo  '    Avaiable targets:'
	@$(DISTDIR)/scripts/ls-config-targets.sh | xargs -n3 printf "	    %s	    %s	    %s\n"
	@echo
	@echo  'To make a new kernel version release:'
	@echo  '  dist-new-release     - Update changelog, increase release number and tag a new commit properly.'
	@echo  '  dist-new-sub-release - Same as dist-new-release but increase the sub release number (eg. 5.18.0-1, 5.18.0-1.1, 5.18.0-1.2, ...).'
	@echo
	@echo  'Build prepare:'
	@echo  '  dist-check-requires		- Check build time and dist targets package dependency.'
	@echo  '  dist-check-buildrequires	- Check build time package dependency.'
	@echo  '  dist-install-buildrequires	- Check and automatically install build time dependency, requires root.'
	@echo
	@echo  'Sanity check:'
	@echo  '  dist-check-commit		- Check commit message and patch.'
	@printf "  				  \033[1;33m* Please at least ensure this check doesn't raise any error before submitting a patch.\033[0m\n"
	@echo  '  dist-check-diff-config	- Check for invalid configs. If a CONFIG_XXX=y is set in config file but gone after make oldconfig,'
	@echo  '  				  it it considered a invalid config. There are many potential resons causing this, changed or missing'
	@echo  '  				  config dependency, deprecated config, auto-select configs, this tool will also provide some hint.'
	@echo  '  dist-check-dup-config		- Check for duplicated configs, '
	@echo  '  dist-check-new-config		- Check for unset configs. If any config is not set, kbuild will prompt during built time.'
	@echo  '  				  This helper will check for unset configs so you can set them properly in predefined config file.'
	@echo  'Code clean up:'
	@echo  '  dist-fix-new-config		- Same as dist-check-new-configs but automatically set unset configs to its default value and'
	@echo  '  				  update config files.'
	@echo  '  dist-fix-dup-config		- Automatically remove duplicated configs.'
	@echo  "  dist-format-config		- Sort and simplify the base config files, won\'t change the final config output."
	@printf '  				  \033[1;33m* Please run this before submitting any config change.\033[0m\n'
	@echo  '  dist-clean			- Clean up directories under:'
	@echo  '				  $(DISTDIR)'
	@echo
	@echo  'Configuration targets:'
	@echo  '  dist-configs			- Generate dist config files, using config matrix in:'
	@echo  '  				  $(DISTDIR)/config/'
	@echo  '  dist-config			- Generate config, and override .config file with the config corresponding'
	@echo  '				  to arch $(BUILD_ARCH)'
	@echo
