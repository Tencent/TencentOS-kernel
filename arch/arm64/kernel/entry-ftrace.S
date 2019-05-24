/*
 * arch/arm64/kernel/entry-ftrace.S
 *
 * Copyright (C) 2013 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>
#include <asm/ftrace.h>
#include <asm/insn.h>

/*
 * Gcc with -pg will put the following code in the beginning of each function:
 *      mov x0, x30
 *      bl _mcount
 *	[function's body ...]
 * "bl _mcount" may be replaced to "bl ftrace_caller" or NOP if dynamic
 * ftrace is enabled.
 *
 * Please note that x0 as an argument will not be used here because we can
 * get lr(x30) of instrumented function at any time by winding up call stack
 * as long as the kernel is compiled without -fomit-frame-pointer.
 * (or CONFIG_FRAME_POINTER, this is forced on arm64)
 *
 * stack layout after mcount_enter in _mcount():
 *
 * current sp/fp =>  0:+-----+
 * in _mcount()        | x29 | -> instrumented function's fp
 *                     +-----+
 *                     | x30 | -> _mcount()'s lr (= instrumented function's pc)
 * old sp       => +16:+-----+
 * when instrumented   |     |
 * function calls      | ... |
 * _mcount()           |     |
 *                     |     |
 * instrumented => +xx:+-----+
 * function's fp       | x29 | -> parent's fp
 *                     +-----+
 *                     | x30 | -> instrumented function's lr (= parent's pc)
 *                     +-----+
 *                     | ... |
 */

	.macro mcount_enter
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp
	.endm

	.macro mcount_exit
	ldp	x29, x30, [sp], #16
	ret
	.endm

	.macro mcount_adjust_addr rd, rn
	sub	\rd, \rn, #AARCH64_INSN_SIZE
	.endm

	/* for instrumented function's parent */
	.macro mcount_get_parent_fp reg
	ldr	\reg, [x29]
	ldr	\reg, [\reg]
	.endm

	/* for instrumented function */
	.macro mcount_get_pc0 reg
	mcount_adjust_addr	\reg, x30
	.endm

	.macro mcount_get_pc reg
	ldr	\reg, [x29, #8]
	mcount_adjust_addr	\reg, \reg
	.endm

	.macro mcount_get_lr reg
	ldr	\reg, [x29]
	ldr	\reg, [\reg, #8]
	.endm

	.macro mcount_get_lr_addr reg
	ldr	\reg, [x29]
	add	\reg, \reg, #8
	.endm

#ifndef CONFIG_DYNAMIC_FTRACE
/*
 * void _mcount(unsigned long return_address)
 * @return_address: return address to instrumented function
 *
 * This function makes calls, if enabled, to:
 *     - tracer function to probe instrumented function's entry,
 *     - ftrace_graph_caller to set up an exit hook
 */
ENTRY(_mcount)
	mcount_enter

	ldr_l	x2, ftrace_trace_function
	adr	x0, ftrace_stub
	cmp	x0, x2			// if (ftrace_trace_function
	b.eq	skip_ftrace_call	//     != ftrace_stub) {

	mcount_get_pc	x0		//       function's pc
	mcount_get_lr	x1		//       function's lr (= parent's pc)
	blr	x2			//   (*ftrace_trace_function)(pc, lr);

#ifndef CONFIG_FUNCTION_GRAPH_TRACER
skip_ftrace_call:			//   return;
	mcount_exit			// }
#else
	mcount_exit			//   return;
					// }
skip_ftrace_call:
	ldr_l	x2, ftrace_graph_return
	cmp	x0, x2			//   if ((ftrace_graph_return
	b.ne	ftrace_graph_caller	//        != ftrace_stub)

	ldr_l	x2, ftrace_graph_entry	//     || (ftrace_graph_entry
	adr_l	x0, ftrace_graph_entry_stub //     != ftrace_graph_entry_stub))
	cmp	x0, x2
	b.ne	ftrace_graph_caller	//     ftrace_graph_caller();

	mcount_exit
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
ENDPROC(_mcount)

#else /* CONFIG_DYNAMIC_FTRACE */
#ifndef CONFIG_DYNAMIC_FTRACE_WITH_REGS
/*
 * _mcount() is used to build the kernel with -pg option, but all the branch
 * instructions to _mcount() are replaced to NOP initially at kernel start up,
 * and later on, NOP to branch to ftrace_caller() when enabled or branch to
 * NOP when disabled per-function base.
 */
ENTRY(_mcount)
	ret
ENDPROC(_mcount)

/*
 * void ftrace_caller(unsigned long return_address)
 * @return_address: return address to instrumented function
 *
 * This function is a counterpart of _mcount() in 'static' ftrace, and
 * makes calls to:
 *     - tracer function to probe instrumented function's entry,
 *     - ftrace_graph_caller to set up an exit hook
 */
ENTRY(ftrace_caller)
	mcount_enter

	mcount_get_pc0	x0		//     function's pc
	mcount_get_lr	x1		//     function's lr

	.global ftrace_call
ftrace_call:				// tracer(pc, lr);
	nop				// This will be replaced with "bl xxx"
					// where xxx can be any kind of tracer.

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.global ftrace_graph_call
ftrace_graph_call:			// ftrace_graph_caller();
	nop				// If enabled, this will be replaced
					// "b ftrace_graph_caller"
#endif

	mcount_exit
ENDPROC(ftrace_caller)

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * void ftrace_graph_caller(void)
 *
 * Called from _mcount() or ftrace_caller() when function_graph tracer is
 * selected.
 * This function w/ prepare_ftrace_return() fakes link register's value on
 * the call stack in order to intercept instrumented function's return path
 * and run return_to_handler() later on its exit.
 */
ENTRY(ftrace_graph_caller)
	mcount_get_lr_addr	x0	// pointer to function's saved lr
	mcount_get_pc		x1	// function's pc
	mcount_get_parent_fp	x2	// parent's fp
	bl	prepare_ftrace_return	// prepare_ftrace_return(&lr, pc, fp)

	mcount_exit
ENDPROC(ftrace_graph_caller)
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#else /* CONFIG_DYNAMIC_FTRACE_WITH_REGS */

	.macro  ftrace_regs_entry, allregs=0
	/* make room for pt_regs, plus a callee frame */
	sub	sp, sp, #(S_FRAME_SIZE + 16)

	/* save function arguments */
	stp	x0, x1, [sp, #S_X0]
	stp	x2, x3, [sp, #S_X2]
	stp	x4, x5, [sp, #S_X4]
	stp	x6, x7, [sp, #S_X6]
	stp	x8, x9, [sp, #S_X8]

	.if \allregs == 1
	stp	x10, x11, [sp, #S_X10]
	stp	x12, x13, [sp, #S_X12]
	stp	x14, x15, [sp, #S_X14]
	stp	x16, x17, [sp, #S_X16]
	stp	x18, x19, [sp, #S_X18]
	stp	x20, x21, [sp, #S_X20]
	stp	x22, x23, [sp, #S_X22]
	stp	x24, x25, [sp, #S_X24]
	stp	x26, x27, [sp, #S_X26]
	.endif

	/* Save fp and x28, which is used in this function. */
	stp	x28, x29, [sp, #S_X28]

	/* The stack pointer as it was on ftrace_caller entry... */
	add	x28, sp, #(S_FRAME_SIZE + 16)
	/* ...and the link Register at callee entry */
	stp	x9, x28, [sp, #S_LR]	/* to pt_regs.r[30] and .sp */

	/* The program counter just after the ftrace call site */
	str	lr, [sp, #S_PC]

	/* Now fill in callee's preliminary stackframe. */
	stp	x29, x9, [sp, #S_FRAME_SIZE]
	/* Let FP point to it. */
	add	x29, sp, #S_FRAME_SIZE

	/* Our stackframe, stored inside pt_regs. */
	stp	x29, x30, [sp, #S_STACKFRAME]
	add	x29, sp, #S_STACKFRAME
	.endm

ENTRY(ftrace_regs_caller)
	ftrace_regs_entry	1
	b	ftrace_common
ENDPROC(ftrace_regs_caller)

ENTRY(ftrace_caller)
	ftrace_regs_entry	0
	b	ftrace_common
ENDPROC(ftrace_caller)

ENTRY(ftrace_common)

	mov	x3, sp				/* pt_regs are @sp */
	ldr_l	x2, function_trace_op, x0
	mov	x1, x9				/* parent IP */
	sub	x0, lr, #AARCH64_INSN_SIZE

	.global ftrace_call
ftrace_call:
	bl	ftrace_stub

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.global ftrace_graph_call	// ftrace_graph_caller();
ftrace_graph_call:
	nop				// If enabled, this will be replaced
					// "b ftrace_graph_caller"
#endif

/*
 * GCC's patchable-function-entry implicitly disables IPA-RA,
 * so all non-argument registers are either scratch / dead
 * or callee-saved (within the ftrace framework). Function
 * arguments of the call we are intercepting right now however
 * need to be preserved in any case.
 */
ftrace_common_return:
	/* restore function args */
	ldp	x0, x1, [sp]
	ldp	x2, x3, [sp, #S_X2]
	ldp	x4, x5, [sp, #S_X4]
	ldp	x6, x7, [sp, #S_X6]
	ldr	x8, [sp, #S_X8]

	/* restore fp and x28 */
	ldp	x28, x29, [sp, #S_X28]

	ldr	lr, [sp, #S_LR]
	ldr	x9, [sp, #S_PC]
	/* clean up both frames, ours and callee preliminary */
	add	sp, sp, #S_FRAME_SIZE + 16

	ret	x9
ENDPROC(ftrace_common)

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
	add	x0, sp, #S_LR
	ldr	x1, [sp, #S_PC]
	sub	x1, x1, #AARCH64_INSN_SIZE
	ldr	x2, [sp, #S_FRAME_SIZE]	   /* fp */
	bl	prepare_ftrace_return
	b	ftrace_common_return
ENDPROC(ftrace_graph_caller)
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_REGS */
#endif /* CONFIG_DYNAMIC_FTRACE */

ENTRY(ftrace_stub)
	ret
ENDPROC(ftrace_stub)

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/* save return value regs*/
	.macro save_return_regs
	sub sp, sp, #64
	stp x0, x1, [sp]
	stp x2, x3, [sp, #16]
	stp x4, x5, [sp, #32]
	stp x6, x7, [sp, #48]
	.endm

	/* restore return value regs*/
	.macro restore_return_regs
	ldp x0, x1, [sp]
	ldp x2, x3, [sp, #16]
	ldp x4, x5, [sp, #32]
	ldp x6, x7, [sp, #48]
	add sp, sp, #64
	.endm

/*
 * void return_to_handler(void)
 *
 * Run ftrace_return_to_handler() before going back to parent.
 * @fp is checked against the value passed by ftrace_graph_caller()
 * only when HAVE_FUNCTION_GRAPH_FP_TEST is enabled.
 */
ENTRY(return_to_handler)
	save_return_regs
	mov	x0, x29			//     parent's fp
	bl	ftrace_return_to_handler// addr = ftrace_return_to_hander(fp);
	mov	x30, x0			// restore the original return address
	restore_return_regs
	ret
END(return_to_handler)
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
