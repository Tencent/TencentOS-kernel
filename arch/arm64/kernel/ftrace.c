/*
 * arch/arm64/kernel/ftrace.c
 *
 * Copyright (C) 2013 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ftrace.h>
#include <linux/module.h>
#include <linux/swab.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>
#include <asm/debug-monitors.h>
#include <asm/ftrace.h>
#include <asm/insn.h>

#ifdef CONFIG_DYNAMIC_FTRACE
/*
 * Replace a single instruction, which may be a branch or NOP.
 * If @validate == true, a replaced instruction is checked against 'old'.
 */
static int ftrace_modify_code(unsigned long pc, u32 old, u32 new,
			      bool validate)
{
	u32 replaced;

	/*
	 * Note:
	 * We are paranoid about modifying text, as if a bug were to happen, it
	 * could cause us to read or write to someplace that could cause harm.
	 * Carefully read and modify the code with aarch64_insn_*() which uses
	 * probe_kernel_*(), and make sure what we read is what we expected it
	 * to be before modifying it.
	 */
	if (validate) {
		if (aarch64_insn_read((void *)pc, &replaced))
			return -EFAULT;

		if (replaced != old)
			return -EINVAL;
	}
	if (aarch64_insn_patch_text_nosync((void *)pc, new))
		return -EPERM;

	return 0;
}

/*
 * Replace tracer function in ftrace_caller()
 */
int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long pc;
	u32 new;

	pc = (unsigned long)&ftrace_call;
	new = aarch64_insn_gen_branch_imm(pc, (unsigned long)func,
					  AARCH64_INSN_BRANCH_LINK);

	return ftrace_modify_code(pc, 0, new, false);
}

#ifdef CONFIG_ARM64_MODULE_PLTS
static int install_ftrace_trampoline(struct module *mod, unsigned long *addr)
{
	struct plt_entry trampoline, *mod_trampoline;

	/*
	 * Iterate over
	 * mod->arch.ftrace_trampolines[MOD_ARCH_NR_FTRACE_TRAMPOLINES]
	 * The assignment to various ftrace functions happens here.
	 */
	if (*addr == FTRACE_ADDR)
		mod_trampoline = &mod->arch.ftrace_trampolines[0];
	else if (*addr == FTRACE_REGS_ADDR)
		mod_trampoline = &mod->arch.ftrace_trampolines[1];
	else
		return -EINVAL;

	trampoline = get_plt_entry(*addr, mod_trampoline);

	if (!plt_entries_equal(mod_trampoline, &trampoline)) {
		/* point the trampoline at our ftrace entry point */
		module_disable_ro(mod);
		*mod_trampoline = trampoline;
		module_enable_ro(mod, true);

		/* update trampoline before patching in the branch */
		smp_wmb();
	}
	*addr = (unsigned long)(void *)mod_trampoline;

	return 0;
}
#endif

/*
 * Turn on the call to ftrace_caller() in instrumented function
 */
int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long pc = rec->ip;
	u32 old, new;
	long offset = (long)pc - (long)addr;

	if (offset < -SZ_128M || offset >= SZ_128M) {
#ifdef CONFIG_ARM64_MODULE_PLTS
		struct module *mod;
		int ret;

		/*
		 * On kernels that support module PLTs, the offset between the
		 * branch instruction and its target may legally exceed the
		 * range of an ordinary relative 'bl' opcode. In this case, we
		 * need to branch via a trampoline in the module.
		 *
		 * NOTE: __module_text_address() must be called with preemption
		 * disabled, but we can rely on ftrace_lock to ensure that 'mod'
		 * retains its validity throughout the remainder of this code.
		 */
		preempt_disable();
		mod = __module_text_address(pc);
		preempt_enable();

		if (WARN_ON(!mod))
			return -EINVAL;

		/* Check against our well-known list of ftrace entry points */
		if (addr == FTRACE_ADDR || addr == FTRACE_REGS_ADDR) {
			ret = install_ftrace_trampoline(mod, &addr);
			if (ret < 0)
				return ret;
		} else
			return -EINVAL;

#else /* CONFIG_ARM64_MODULE_PLTS */
		return -EINVAL;
#endif /* CONFIG_ARM64_MODULE_PLTS */
	}

	old = aarch64_insn_gen_nop();
	new = aarch64_insn_gen_branch_imm(pc, addr, AARCH64_INSN_BRANCH_LINK);

	return ftrace_modify_code(pc, old, new, true);
}

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
			unsigned long addr)
{
	unsigned long pc = rec->ip;
	u32 old, new;

	old = aarch64_insn_gen_branch_imm(pc, old_addr,
					  AARCH64_INSN_BRANCH_LINK);
	new = aarch64_insn_gen_branch_imm(pc, addr, AARCH64_INSN_BRANCH_LINK);

	return ftrace_modify_code(pc, old, new, true);
}

/*
 * Ftrace with regs generates the tracer calls as close as possible to
 * the function entry; no stack frame has been set up at that point.
 * In order to make another call e.g to ftrace_caller, the LR must be
 * saved from being overwritten.
 * Between two functions, and with IPA-RA turned off, the scratch registers
 * are available, so move the LR to x9 before calling into ftrace.
 *
 * This function is called once during kernel startup for each call site.
 * The address passed is that of the actual branch, so patch in the LR saver
 * just before that.
 */
static int ftrace_setup_lr_saver(unsigned long addr)
{
	u32 old, new;

	old = aarch64_insn_gen_nop();
	/* "mov x9, lr" is officially aliased from "orr x9, xzr, lr". */
	new = aarch64_insn_gen_logical_shifted_reg(AARCH64_INSN_REG_9,
		AARCH64_INSN_REG_ZR, AARCH64_INSN_REG_LR, 0,
		AARCH64_INSN_VARIANT_64BIT, AARCH64_INSN_LOGIC_ORR);
	return ftrace_modify_code(addr - AARCH64_INSN_SIZE, old, new, true);
}
#endif

/*
 * Turn off the call to ftrace_caller() in instrumented function
 */
int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec,
		    unsigned long addr)
{
	unsigned long pc = rec->ip;
	bool validate = true;
	u32 old = 0, new;
	long offset = (long)pc - (long)addr;

	/*
	 * -fpatchable-function-entry= does not generate a profiling call
	 *  initially; the NOPs are already there. So instead,
	 *  put the LR saver there ahead of time, in order to avoid
	 *  any race condition over patching 2 instructions.
	 */
	if (IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_REGS) &&
		addr == MCOUNT_ADDR)
		return ftrace_setup_lr_saver(pc);

	if (offset < -SZ_128M || offset >= SZ_128M) {
#ifdef CONFIG_ARM64_MODULE_PLTS
		u32 replaced;

		/*
		 * 'mod' is only set at module load time, but if we end up
		 * dealing with an out-of-range condition, we can assume it
		 * is due to a module being loaded far away from the kernel.
		 */
		if (!mod) {
			preempt_disable();
			mod = __module_text_address(pc);
			preempt_enable();

			if (WARN_ON(!mod))
				return -EINVAL;
		}

		/*
		 * The instruction we are about to patch may be a branch and
		 * link instruction that was redirected via a PLT entry. In
		 * this case, the normal validation will fail, but we can at
		 * least check that we are dealing with a branch and link
		 * instruction that points into the right module.
		 */
		if (aarch64_insn_read((void *)pc, &replaced))
			return -EFAULT;

		if (!aarch64_insn_is_bl(replaced) ||
		    !within_module(pc + aarch64_get_branch_offset(replaced),
				   mod))
			return -EINVAL;

		validate = false;
#else /* CONFIG_ARM64_MODULE_PLTS */
		return -EINVAL;
#endif /* CONFIG_ARM64_MODULE_PLTS */
	} else {
		old = aarch64_insn_gen_branch_imm(pc, addr,
						  AARCH64_INSN_BRANCH_LINK);
	}

	new = aarch64_insn_gen_nop();

	return ftrace_modify_code(pc, old, new, validate);
}

void arch_ftrace_update_code(int command)
{
	ftrace_modify_all_code(command);
}

int __init ftrace_dyn_arch_init(void)
{
	return 0;
}
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * function_graph tracer expects ftrace_return_to_handler() to be called
 * on the way back to parent. For this purpose, this function is called
 * in _mcount() or ftrace_caller() to replace return address (*parent) on
 * the call stack to return_to_handler.
 *
 * Note that @frame_pointer is used only for sanity check later.
 */
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr,
			   unsigned long frame_pointer)
{
	unsigned long return_hooker = (unsigned long)&return_to_handler;
	unsigned long old;
	struct ftrace_graph_ent trace;
	int err;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	/*
	 * Note:
	 * No protection against faulting at *parent, which may be seen
	 * on other archs. It's unlikely on AArch64.
	 */
	old = *parent;

	trace.func = self_addr;
	trace.depth = current->curr_ret_stack + 1;

	/* Only trace if the calling function expects to */
	if (!ftrace_graph_entry(&trace))
		return;

	err = ftrace_push_return_trace(old, self_addr, &trace.depth,
				       frame_pointer, NULL);
	if (err == -EBUSY)
		return;
	else
		*parent = return_hooker;
}

#ifdef CONFIG_DYNAMIC_FTRACE
/*
 * Turn on/off the call to ftrace_graph_caller() in ftrace_caller()
 * depending on @enable.
 */
static int ftrace_modify_graph_caller(bool enable)
{
	unsigned long pc = (unsigned long)&ftrace_graph_call;
	u32 branch, nop;

	branch = aarch64_insn_gen_branch_imm(pc,
					     (unsigned long)ftrace_graph_caller,
					     AARCH64_INSN_BRANCH_NOLINK);
	nop = aarch64_insn_gen_nop();

	if (enable)
		return ftrace_modify_code(pc, nop, branch, true);
	else
		return ftrace_modify_code(pc, branch, nop, true);
}

int ftrace_enable_ftrace_graph_caller(void)
{
	return ftrace_modify_graph_caller(true);
}

int ftrace_disable_ftrace_graph_caller(void)
{
	return ftrace_modify_graph_caller(false);
}
#endif /* CONFIG_DYNAMIC_FTRACE */
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
