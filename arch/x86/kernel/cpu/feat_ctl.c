// SPDX-License-Identifier: GPL-2.0
#include <linux/tboot.h>

#include <asm/cpufeature.h>
#include <asm/msr-index.h>
#include <asm/processor.h>

static void clear_sgx_caps(void)
{
	setup_clear_cpu_cap(X86_FEATURE_SGX);
	setup_clear_cpu_cap(X86_FEATURE_SGX_LC);
}

static int __init nosgx(char *str)
{
	clear_sgx_caps();

	return 0;
}

early_param("nosgx", nosgx);

void init_ia32_feat_ctl(struct cpuinfo_x86 *c)
{
	bool enable_sgx;
	u64 msr;

	if (rdmsrl_safe(MSR_IA32_FEAT_CTL, &msr)) {
		clear_sgx_caps();
		return;
	}

	/*
	 * Enable SGX if and only if the kernel supports SGX and Launch Control
	 * is supported, i.e. disable SGX if the LE hash MSRs can't be written.
	 */
	enable_sgx = cpu_has(c, X86_FEATURE_SGX) &&
		     cpu_has(c, X86_FEATURE_SGX_LC) &&
		     IS_ENABLED(CONFIG_X86_SGX);

	if (msr & FEAT_CTL_LOCKED)
		goto update_sgx;

	/*
	 * Ignore whatever value BIOS left in the MSR to avoid enabling random
	 * features or faulting on the WRMSR.
	 */
	msr = FEAT_CTL_LOCKED;

	/*
	 * Enable VMX if and only if the kernel may do VMXON at some point,
	 * i.e. KVM is enabled, to avoid unnecessarily adding an attack vector
	 * for the kernel, e.g. using VMX to hide malicious code.
	 */
	if (cpu_has(c, X86_FEATURE_VMX) && IS_ENABLED(CONFIG_KVM_INTEL)) {
		msr |= FEAT_CTL_VMX_ENABLED_OUTSIDE_SMX;

		if (tboot_enabled())
			msr |= FEAT_CTL_VMX_ENABLED_INSIDE_SMX;
	}

	if (enable_sgx)
		msr |= FEAT_CTL_SGX_ENABLED | FEAT_CTL_SGX_LC_ENABLED;

	wrmsrl(MSR_IA32_FEAT_CTL, msr);

update_sgx:
	if (!(msr & FEAT_CTL_SGX_ENABLED) ||
	    !(msr & FEAT_CTL_SGX_LC_ENABLED) || !enable_sgx) {
		if (enable_sgx)
			pr_err_once("SGX disabled by BIOS\n");
		clear_sgx_caps();
	}
}
