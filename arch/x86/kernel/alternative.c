// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) "SMP alternatives: " fmt

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/stringify.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/memory.h>
#include <linux/stop_machine.h>
#include <linux/slab.h>
#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/mmu_context.h>
#include <linux/bsearch.h>
#include <asm/text-patching.h>
#include <asm/alternative.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/mce.h>
#include <asm/nmi.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/io.h>
#include <asm/fixmap.h>

int __read_mostly alternatives_patched;

EXPORT_SYMBOL_GPL(alternatives_patched);

#define MAX_PATCH_LEN (255-1)

static int __initdata_or_module debug_alternative;

static int __init debug_alt(char *str)
{
	debug_alternative = 1;
	return 1;
}
__setup("debug-alternative", debug_alt);

static int noreplace_smp;

static int __init setup_noreplace_smp(char *str)
{
	noreplace_smp = 1;
	return 1;
}
__setup("noreplace-smp", setup_noreplace_smp);

#define DPRINTK(fmt, args...)						\
do {									\
	if (debug_alternative)						\
		printk(KERN_DEBUG "%s: " fmt "\n", __func__, ##args);	\
} while (0)

#define DUMP_BYTES(buf, len, fmt, args...)				\
do {									\
	if (unlikely(debug_alternative)) {				\
		int j;							\
									\
		if (!(len))						\
			break;						\
									\
		printk(KERN_DEBUG fmt, ##args);				\
		for (j = 0; j < (len) - 1; j++)				\
			printk(KERN_CONT "%02hhx ", buf[j]);		\
		printk(KERN_CONT "%02hhx\n", buf[j]);			\
	}								\
} while (0)

/*
 * Each GENERIC_NOPX is of X bytes, and defined as an array of bytes
 * that correspond to that nop. Getting from one nop to the next, we
 * add to the array the offset that is equal to the sum of all sizes of
 * nops preceding the one we are after.
 *
 * Note: The GENERIC_NOP5_ATOMIC is at the end, as it breaks the
 * nice symmetry of sizes of the previous nops.
 */
#if defined(GENERIC_NOP1) && !defined(CONFIG_X86_64)
static const unsigned char intelnops[] =
{
	GENERIC_NOP1,
	GENERIC_NOP2,
	GENERIC_NOP3,
	GENERIC_NOP4,
	GENERIC_NOP5,
	GENERIC_NOP6,
	GENERIC_NOP7,
	GENERIC_NOP8,
	GENERIC_NOP5_ATOMIC
};
static const unsigned char * const intel_nops[ASM_NOP_MAX+2] =
{
	NULL,
	intelnops,
	intelnops + 1,
	intelnops + 1 + 2,
	intelnops + 1 + 2 + 3,
	intelnops + 1 + 2 + 3 + 4,
	intelnops + 1 + 2 + 3 + 4 + 5,
	intelnops + 1 + 2 + 3 + 4 + 5 + 6,
	intelnops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
	intelnops + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8,
};
#endif

#ifdef K8_NOP1
static const unsigned char k8nops[] =
{
	K8_NOP1,
	K8_NOP2,
	K8_NOP3,
	K8_NOP4,
	K8_NOP5,
	K8_NOP6,
	K8_NOP7,
	K8_NOP8,
	K8_NOP5_ATOMIC
};
static const unsigned char * const k8_nops[ASM_NOP_MAX+2] =
{
	NULL,
	k8nops,
	k8nops + 1,
	k8nops + 1 + 2,
	k8nops + 1 + 2 + 3,
	k8nops + 1 + 2 + 3 + 4,
	k8nops + 1 + 2 + 3 + 4 + 5,
	k8nops + 1 + 2 + 3 + 4 + 5 + 6,
	k8nops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
	k8nops + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8,
};
#endif

#if defined(K7_NOP1) && !defined(CONFIG_X86_64)
static const unsigned char k7nops[] =
{
	K7_NOP1,
	K7_NOP2,
	K7_NOP3,
	K7_NOP4,
	K7_NOP5,
	K7_NOP6,
	K7_NOP7,
	K7_NOP8,
	K7_NOP5_ATOMIC
};
static const unsigned char * const k7_nops[ASM_NOP_MAX+2] =
{
	NULL,
	k7nops,
	k7nops + 1,
	k7nops + 1 + 2,
	k7nops + 1 + 2 + 3,
	k7nops + 1 + 2 + 3 + 4,
	k7nops + 1 + 2 + 3 + 4 + 5,
	k7nops + 1 + 2 + 3 + 4 + 5 + 6,
	k7nops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
	k7nops + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8,
};
#endif

#ifdef P6_NOP1
static const unsigned char p6nops[] =
{
	P6_NOP1,
	P6_NOP2,
	P6_NOP3,
	P6_NOP4,
	P6_NOP5,
	P6_NOP6,
	P6_NOP7,
	P6_NOP8,
	P6_NOP5_ATOMIC
};
static const unsigned char * const p6_nops[ASM_NOP_MAX+2] =
{
	NULL,
	p6nops,
	p6nops + 1,
	p6nops + 1 + 2,
	p6nops + 1 + 2 + 3,
	p6nops + 1 + 2 + 3 + 4,
	p6nops + 1 + 2 + 3 + 4 + 5,
	p6nops + 1 + 2 + 3 + 4 + 5 + 6,
	p6nops + 1 + 2 + 3 + 4 + 5 + 6 + 7,
	p6nops + 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8,
};
#endif

/* Initialize these to a safe default */
#ifdef CONFIG_X86_64
const unsigned char * const *ideal_nops = p6_nops;
#else
const unsigned char * const *ideal_nops = intel_nops;
#endif

void __init arch_init_ideal_nops(void)
{
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_INTEL:
		/*
		 * Due to a decoder implementation quirk, some
		 * specific Intel CPUs actually perform better with
		 * the "k8_nops" than with the SDM-recommended NOPs.
		 */
		if (boot_cpu_data.x86 == 6 &&
		    boot_cpu_data.x86_model >= 0x0f &&
		    boot_cpu_data.x86_model != 0x1c &&
		    boot_cpu_data.x86_model != 0x26 &&
		    boot_cpu_data.x86_model != 0x27 &&
		    boot_cpu_data.x86_model < 0x30) {
			ideal_nops = k8_nops;
		} else if (boot_cpu_has(X86_FEATURE_NOPL)) {
			   ideal_nops = p6_nops;
		} else {
#ifdef CONFIG_X86_64
			ideal_nops = k8_nops;
#else
			ideal_nops = intel_nops;
#endif
		}
		break;

	case X86_VENDOR_HYGON:
		ideal_nops = p6_nops;
		return;

	case X86_VENDOR_AMD:
		if (boot_cpu_data.x86 > 0xf) {
			ideal_nops = p6_nops;
			return;
		}

		/* fall through */

	default:
#ifdef CONFIG_X86_64
		ideal_nops = k8_nops;
#else
		if (boot_cpu_has(X86_FEATURE_K8))
			ideal_nops = k8_nops;
		else if (boot_cpu_has(X86_FEATURE_K7))
			ideal_nops = k7_nops;
		else
			ideal_nops = intel_nops;
#endif
	}
}

/* Use this to add nops to a buffer, then text_poke the whole buffer. */
static void __init_or_module add_nops(void *insns, unsigned int len)
{
	while (len > 0) {
		unsigned int noplen = len;
		if (noplen > ASM_NOP_MAX)
			noplen = ASM_NOP_MAX;
		memcpy(insns, ideal_nops[noplen], noplen);
		insns += noplen;
		len -= noplen;
	}
}

extern struct alt_instr __alt_instructions[], __alt_instructions_end[];
extern s32 __smp_locks[], __smp_locks_end[];
void text_poke_early(void *addr, const void *opcode, size_t len);

/*
 * Are we looking at a near JMP with a 1 or 4-byte displacement.
 */
static inline bool is_jmp(const u8 opcode)
{
	return opcode == 0xeb || opcode == 0xe9;
}

static void __init_or_module
recompute_jump(struct alt_instr *a, u8 *orig_insn, u8 *repl_insn, u8 *insn_buff)
{
	u8 *next_rip, *tgt_rip;
	s32 n_dspl, o_dspl;
	int repl_len;

	if (a->replacementlen != 5)
		return;

	o_dspl = *(s32 *)(insn_buff + 1);

	/* next_rip of the replacement JMP */
	next_rip = repl_insn + a->replacementlen;
	/* target rip of the replacement JMP */
	tgt_rip  = next_rip + o_dspl;
	n_dspl = tgt_rip - orig_insn;

	DPRINTK("target RIP: %px, new_displ: 0x%x", tgt_rip, n_dspl);

	if (tgt_rip - orig_insn >= 0) {
		if (n_dspl - 2 <= 127)
			goto two_byte_jmp;
		else
			goto five_byte_jmp;
	/* negative offset */
	} else {
		if (((n_dspl - 2) & 0xff) == (n_dspl - 2))
			goto two_byte_jmp;
		else
			goto five_byte_jmp;
	}

two_byte_jmp:
	n_dspl -= 2;

	insn_buff[0] = 0xeb;
	insn_buff[1] = (s8)n_dspl;
	add_nops(insn_buff + 2, 3);

	repl_len = 2;
	goto done;

five_byte_jmp:
	n_dspl -= 5;

	insn_buff[0] = 0xe9;
	*(s32 *)&insn_buff[1] = n_dspl;

	repl_len = 5;

done:

	DPRINTK("final displ: 0x%08x, JMP 0x%lx",
		n_dspl, (unsigned long)orig_insn + n_dspl + repl_len);
}

/*
 * "noinline" to cause control flow change and thus invalidate I$ and
 * cause refetch after modification.
 */
static void __init_or_module noinline optimize_nops(struct alt_instr *a, u8 *instr)
{
	unsigned long flags;
	int i;

	for (i = 0; i < a->padlen; i++) {
		if (instr[i] != 0x90)
			return;
	}

	local_irq_save(flags);
	add_nops(instr + (a->instrlen - a->padlen), a->padlen);
	local_irq_restore(flags);

	DUMP_BYTES(instr, a->instrlen, "%px: [%d:%d) optimized NOPs: ",
		   instr, a->instrlen - a->padlen, a->padlen);
}

/*
 * Replace instructions with better alternatives for this CPU type. This runs
 * before SMP is initialized to avoid SMP problems with self modifying code.
 * This implies that asymmetric systems where APs have less capabilities than
 * the boot processor are not handled. Tough. Make sure you disable such
 * features by hand.
 *
 * Marked "noinline" to cause control flow change and thus insn cache
 * to refetch changed I$ lines.
 */
void __init_or_module noinline apply_alternatives(struct alt_instr *start,
						  struct alt_instr *end)
{
	struct alt_instr *a;
	u8 *instr, *replacement;
	u8 insn_buff[MAX_PATCH_LEN];

	DPRINTK("alt table %px, -> %px", start, end);
	/*
	 * The scan order should be from start to end. A later scanned
	 * alternative code can overwrite previously scanned alternative code.
	 * Some kernel functions (e.g. memcpy, memset, etc) use this order to
	 * patch code.
	 *
	 * So be careful if you want to change the scan order to any other
	 * order.
	 */
	for (a = start; a < end; a++) {
		int insn_buff_sz = 0;

		instr = (u8 *)&a->instr_offset + a->instr_offset;
		replacement = (u8 *)&a->repl_offset + a->repl_offset;
		BUG_ON(a->instrlen > sizeof(insn_buff));
		BUG_ON(a->cpuid >= (NCAPINTS + NBUGINTS) * 32);
		if (!boot_cpu_has(a->cpuid)) {
			if (a->padlen > 1)
				optimize_nops(a, instr);

			continue;
		}

		DPRINTK("feat: %d*32+%d, old: (%pS (%px) len: %d), repl: (%px, len: %d), pad: %d",
			a->cpuid >> 5,
			a->cpuid & 0x1f,
			instr, instr, a->instrlen,
			replacement, a->replacementlen, a->padlen);

		DUMP_BYTES(instr, a->instrlen, "%px: old_insn: ", instr);
		DUMP_BYTES(replacement, a->replacementlen, "%px: rpl_insn: ", replacement);

		memcpy(insn_buff, replacement, a->replacementlen);
		insn_buff_sz = a->replacementlen;

		/*
		 * 0xe8 is a relative jump; fix the offset.
		 *
		 * Instruction length is checked before the opcode to avoid
		 * accessing uninitialized bytes for zero-length replacements.
		 */
		if (a->replacementlen == 5 && *insn_buff == 0xe8) {
			*(s32 *)(insn_buff + 1) += replacement - instr;
			DPRINTK("Fix CALL offset: 0x%x, CALL 0x%lx",
				*(s32 *)(insn_buff + 1),
				(unsigned long)instr + *(s32 *)(insn_buff + 1) + 5);
		}

		if (a->replacementlen && is_jmp(replacement[0]))
			recompute_jump(a, instr, replacement, insn_buff);

		if (a->instrlen > a->replacementlen) {
			add_nops(insn_buff + a->replacementlen,
				 a->instrlen - a->replacementlen);
			insn_buff_sz += a->instrlen - a->replacementlen;
		}
		DUMP_BYTES(insn_buff, insn_buff_sz, "%px: final_insn: ", instr);

		text_poke_early(instr, insn_buff, insn_buff_sz);
	}
}

#ifdef CONFIG_SMP
static void alternatives_smp_lock(const s32 *start, const s32 *end,
				  u8 *text, u8 *text_end)
{
	const s32 *poff;

	for (poff = start; poff < end; poff++) {
		u8 *ptr = (u8 *)poff + *poff;

		if (!*poff || ptr < text || ptr >= text_end)
			continue;
		/* turn DS segment override prefix into lock prefix */
		if (*ptr == 0x3e)
			text_poke(ptr, ((unsigned char []){0xf0}), 1);
	}
}

static void alternatives_smp_unlock(const s32 *start, const s32 *end,
				    u8 *text, u8 *text_end)
{
	const s32 *poff;

	for (poff = start; poff < end; poff++) {
		u8 *ptr = (u8 *)poff + *poff;

		if (!*poff || ptr < text || ptr >= text_end)
			continue;
		/* turn lock prefix into DS segment override prefix */
		if (*ptr == 0xf0)
			text_poke(ptr, ((unsigned char []){0x3E}), 1);
	}
}

struct smp_alt_module {
	/* what is this ??? */
	struct module	*mod;
	char		*name;

	/* ptrs to lock prefixes */
	const s32	*locks;
	const s32	*locks_end;

	/* .text segment, needed to avoid patching init code ;) */
	u8		*text;
	u8		*text_end;

	struct list_head next;
};
static LIST_HEAD(smp_alt_modules);
static bool uniproc_patched = false;	/* protected by text_mutex */

void __init_or_module alternatives_smp_module_add(struct module *mod,
						  char *name,
						  void *locks, void *locks_end,
						  void *text,  void *text_end)
{
	struct smp_alt_module *smp;

	mutex_lock(&text_mutex);
	if (!uniproc_patched)
		goto unlock;

	if (num_possible_cpus() == 1)
		/* Don't bother remembering, we'll never have to undo it. */
		goto smp_unlock;

	smp = kzalloc(sizeof(*smp), GFP_KERNEL);
	if (NULL == smp)
		/* we'll run the (safe but slow) SMP code then ... */
		goto unlock;

	smp->mod	= mod;
	smp->name	= name;
	smp->locks	= locks;
	smp->locks_end	= locks_end;
	smp->text	= text;
	smp->text_end	= text_end;
	DPRINTK("locks %p -> %p, text %p -> %p, name %s\n",
		smp->locks, smp->locks_end,
		smp->text, smp->text_end, smp->name);

	list_add_tail(&smp->next, &smp_alt_modules);
smp_unlock:
	alternatives_smp_unlock(locks, locks_end, text, text_end);
unlock:
	mutex_unlock(&text_mutex);
}

void __init_or_module alternatives_smp_module_del(struct module *mod)
{
	struct smp_alt_module *item;

	mutex_lock(&text_mutex);
	list_for_each_entry(item, &smp_alt_modules, next) {
		if (mod != item->mod)
			continue;
		list_del(&item->next);
		kfree(item);
		break;
	}
	mutex_unlock(&text_mutex);
}

void alternatives_enable_smp(void)
{
	struct smp_alt_module *mod;

	/* Why bother if there are no other CPUs? */
	BUG_ON(num_possible_cpus() == 1);

	mutex_lock(&text_mutex);

	if (uniproc_patched) {
		pr_info("switching to SMP code\n");
		BUG_ON(num_online_cpus() != 1);
		clear_cpu_cap(&boot_cpu_data, X86_FEATURE_UP);
		clear_cpu_cap(&cpu_data(0), X86_FEATURE_UP);
		list_for_each_entry(mod, &smp_alt_modules, next)
			alternatives_smp_lock(mod->locks, mod->locks_end,
					      mod->text, mod->text_end);
		uniproc_patched = false;
	}
	mutex_unlock(&text_mutex);
}

/*
 * Return 1 if the address range is reserved for SMP-alternatives.
 * Must hold text_mutex.
 */
int alternatives_text_reserved(void *start, void *end)
{
	struct smp_alt_module *mod;
	const s32 *poff;
	u8 *text_start = start;
	u8 *text_end = end;

	lockdep_assert_held(&text_mutex);

	list_for_each_entry(mod, &smp_alt_modules, next) {
		if (mod->text > text_end || mod->text_end < text_start)
			continue;
		for (poff = mod->locks; poff < mod->locks_end; poff++) {
			const u8 *ptr = (const u8 *)poff + *poff;

			if (text_start <= ptr && text_end > ptr)
				return 1;
		}
	}

	return 0;
}
#endif /* CONFIG_SMP */

#ifdef CONFIG_PARAVIRT
void __init_or_module apply_paravirt(struct paravirt_patch_site *start,
				     struct paravirt_patch_site *end)
{
	struct paravirt_patch_site *p;
	char insn_buff[MAX_PATCH_LEN];

	for (p = start; p < end; p++) {
		unsigned int used;

		BUG_ON(p->len > MAX_PATCH_LEN);
		/* prep the buffer with the original instructions */
		memcpy(insn_buff, p->instr, p->len);
		used = pv_ops.init.patch(p->type, insn_buff, (unsigned long)p->instr, p->len);

		BUG_ON(used > p->len);

		/* Pad the rest with nops */
		add_nops(insn_buff + used, p->len - used);
		text_poke_early(p->instr, insn_buff, p->len);
	}
}
extern struct paravirt_patch_site __start_parainstructions[],
	__stop_parainstructions[];
#endif	/* CONFIG_PARAVIRT */

/*
 * Self-test for the INT3 based CALL emulation code.
 *
 * This exercises int3_emulate_call() to make sure INT3 pt_regs are set up
 * properly and that there is a stack gap between the INT3 frame and the
 * previous context. Without this gap doing a virtual PUSH on the interrupted
 * stack would corrupt the INT3 IRET frame.
 *
 * See entry_{32,64}.S for more details.
 */

/*
 * We define the int3_magic() function in assembly to control the calling
 * convention such that we can 'call' it from assembly.
 */

extern void int3_magic(unsigned int *ptr); /* defined in asm */

asm (
"	.pushsection	.init.text, \"ax\", @progbits\n"
"	.type		int3_magic, @function\n"
"int3_magic:\n"
"	movl	$1, (%" _ASM_ARG1 ")\n"
"	ret\n"
"	.size		int3_magic, .-int3_magic\n"
"	.popsection\n"
);

extern __initdata unsigned long int3_selftest_ip; /* defined in asm below */

static int __init
int3_exception_notify(struct notifier_block *self, unsigned long val, void *data)
{
	struct die_args *args = data;
	struct pt_regs *regs = args->regs;

	if (!regs || user_mode(regs))
		return NOTIFY_DONE;

	if (val != DIE_INT3)
		return NOTIFY_DONE;

	if (regs->ip - INT3_INSN_SIZE != int3_selftest_ip)
		return NOTIFY_DONE;

	int3_emulate_call(regs, (unsigned long)&int3_magic);
	return NOTIFY_STOP;
}

static void __init int3_selftest(void)
{
	static __initdata struct notifier_block int3_exception_nb = {
		.notifier_call	= int3_exception_notify,
		.priority	= INT_MAX-1, /* last */
	};
	unsigned int val = 0;

	BUG_ON(register_die_notifier(&int3_exception_nb));

	/*
	 * Basically: int3_magic(&val); but really complicated :-)
	 *
	 * Stick the address of the INT3 instruction into int3_selftest_ip,
	 * then trigger the INT3, padded with NOPs to match a CALL instruction
	 * length.
	 */
	asm volatile ("1: int3; nop; nop; nop; nop\n\t"
		      ".pushsection .init.data,\"aw\"\n\t"
		      ".align " __ASM_SEL(4, 8) "\n\t"
		      ".type int3_selftest_ip, @object\n\t"
		      ".size int3_selftest_ip, " __ASM_SEL(4, 8) "\n\t"
		      "int3_selftest_ip:\n\t"
		      __ASM_SEL(.long, .quad) " 1b\n\t"
		      ".popsection\n\t"
		      : ASM_CALL_CONSTRAINT
		      : __ASM_SEL_RAW(a, D) (&val)
		      : "memory");

	BUG_ON(val != 1);

	unregister_die_notifier(&int3_exception_nb);
}

void __init alternative_instructions(void)
{
	int3_selftest();

	/*
	 * The patching is not fully atomic, so try to avoid local
	 * interruptions that might execute the to be patched code.
	 * Other CPUs are not running.
	 */
	stop_nmi();

	/*
	 * Don't stop machine check exceptions while patching.
	 * MCEs only happen when something got corrupted and in this
	 * case we must do something about the corruption.
	 * Ignoring it is worse than an unlikely patching race.
	 * Also machine checks tend to be broadcast and if one CPU
	 * goes into machine check the others follow quickly, so we don't
	 * expect a machine check to cause undue problems during to code
	 * patching.
	 */

#if defined(CONFIG_NUMA_AWARE_SPINLOCKS)
        cna_configure_spin_lock_slowpath();
#endif

	apply_alternatives(__alt_instructions, __alt_instructions_end);

#ifdef CONFIG_SMP
	/* Patch to UP if other cpus not imminent. */
	if (!noreplace_smp && (num_present_cpus() == 1 || setup_max_cpus <= 1)) {
		uniproc_patched = true;
		alternatives_smp_module_add(NULL, "core kernel",
					    __smp_locks, __smp_locks_end,
					    _text, _etext);
	}

	if (!uniproc_patched || num_possible_cpus() == 1) {
		free_init_pages("SMP alternatives",
				(unsigned long)__smp_locks,
				(unsigned long)__smp_locks_end);
	}
#endif

	apply_paravirt(__parainstructions, __parainstructions_end);

	restart_nmi();
	alternatives_patched = 1;
}

/**
 * text_poke_early - Update instructions on a live kernel at boot time
 * @addr: address to modify
 * @opcode: source of the copy
 * @len: length to copy
 *
 * When you use this code to patch more than one byte of an instruction
 * you need to make sure that other CPUs cannot execute this code in parallel.
 * Also no thread must be currently preempted in the middle of these
 * instructions. And on the local CPU you need to be protected against NMI or
 * MCE handlers seeing an inconsistent instruction while you patch.
 */
void __init_or_module text_poke_early(void *addr, const void *opcode,
				      size_t len)
{
	unsigned long flags;

	if (boot_cpu_has(X86_FEATURE_NX) &&
	    is_module_text_address((unsigned long)addr)) {
		/*
		 * Modules text is marked initially as non-executable, so the
		 * code cannot be running and speculative code-fetches are
		 * prevented. Just change the code.
		 */
		memcpy(addr, opcode, len);
	} else {
		local_irq_save(flags);
		memcpy(addr, opcode, len);
		local_irq_restore(flags);
		sync_core();

		/*
		 * Could also do a CLFLUSH here to speed up CPU recovery; but
		 * that causes hangs on some VIA CPUs.
		 */
	}
}

__ro_after_init struct mm_struct *poking_mm;
__ro_after_init unsigned long poking_addr;

static void *__text_poke(void *addr, const void *opcode, size_t len)
{
	bool cross_page_boundary = offset_in_page(addr) + len > PAGE_SIZE;
	struct page *pages[2] = {NULL};
	temp_mm_state_t prev;
	unsigned long flags;
	pte_t pte, *ptep;
	spinlock_t *ptl;
	pgprot_t pgprot;

	/*
	 * While boot memory allocator is running we cannot use struct pages as
	 * they are not yet initialized. There is no way to recover.
	 */
	BUG_ON(!after_bootmem);

	if (!core_kernel_text((unsigned long)addr)) {
		pages[0] = vmalloc_to_page(addr);
		if (cross_page_boundary)
			pages[1] = vmalloc_to_page(addr + PAGE_SIZE);
	} else {
		pages[0] = virt_to_page(addr);
		WARN_ON(!PageReserved(pages[0]));
		if (cross_page_boundary)
			pages[1] = virt_to_page(addr + PAGE_SIZE);
	}
	/*
	 * If something went wrong, crash and burn since recovery paths are not
	 * implemented.
	 */
	BUG_ON(!pages[0] || (cross_page_boundary && !pages[1]));

	local_irq_save(flags);

	/*
	 * Map the page without the global bit, as TLB flushing is done with
	 * flush_tlb_mm_range(), which is intended for non-global PTEs.
	 */
	pgprot = __pgprot(pgprot_val(PAGE_KERNEL) & ~_PAGE_GLOBAL);

	/*
	 * The lock is not really needed, but this allows to avoid open-coding.
	 */
	ptep = get_locked_pte(poking_mm, poking_addr, &ptl);

	/*
	 * This must not fail; preallocated in poking_init().
	 */
	VM_BUG_ON(!ptep);

	pte = mk_pte(pages[0], pgprot);
	set_pte_at(poking_mm, poking_addr, ptep, pte);

	if (cross_page_boundary) {
		pte = mk_pte(pages[1], pgprot);
		set_pte_at(poking_mm, poking_addr + PAGE_SIZE, ptep + 1, pte);
	}

	/*
	 * Loading the temporary mm behaves as a compiler barrier, which
	 * guarantees that the PTE will be set at the time memcpy() is done.
	 */
	prev = use_temporary_mm(poking_mm);

	kasan_disable_current();
	memcpy((u8 *)poking_addr + offset_in_page(addr), opcode, len);
	kasan_enable_current();

	/*
	 * Ensure that the PTE is only cleared after the instructions of memcpy
	 * were issued by using a compiler barrier.
	 */
	barrier();

	pte_clear(poking_mm, poking_addr, ptep);
	if (cross_page_boundary)
		pte_clear(poking_mm, poking_addr + PAGE_SIZE, ptep + 1);

	/*
	 * Loading the previous page-table hierarchy requires a serializing
	 * instruction that already allows the core to see the updated version.
	 * Xen-PV is assumed to serialize execution in a similar manner.
	 */
	unuse_temporary_mm(prev);

	/*
	 * Flushing the TLB might involve IPIs, which would require enabled
	 * IRQs, but not if the mm is not used, as it is in this point.
	 */
	flush_tlb_mm_range(poking_mm, poking_addr, poking_addr +
			   (cross_page_boundary ? 2 : 1) * PAGE_SIZE,
			   PAGE_SHIFT, false);

	/*
	 * If the text does not match what we just wrote then something is
	 * fundamentally screwy; there's nothing we can really do about that.
	 */
	BUG_ON(memcmp(addr, opcode, len));

	pte_unmap_unlock(ptep, ptl);
	local_irq_restore(flags);
	return addr;
}

/**
 * text_poke - Update instructions on a live kernel
 * @addr: address to modify
 * @opcode: source of the copy
 * @len: length to copy
 *
 * Only atomic text poke/set should be allowed when not doing early patching.
 * It means the size must be writable atomically and the address must be aligned
 * in a way that permits an atomic write. It also makes sure we fit on a single
 * page.
 *
 * Note that the caller must ensure that if the modified code is part of a
 * module, the module would not be removed during poking. This can be achieved
 * by registering a module notifier, and ordering module removal and patching
 * trough a mutex.
 */
void *text_poke(void *addr, const void *opcode, size_t len)
{
	lockdep_assert_held(&text_mutex);

	return __text_poke(addr, opcode, len);
}

/**
 * text_poke_kgdb - Update instructions on a live kernel by kgdb
 * @addr: address to modify
 * @opcode: source of the copy
 * @len: length to copy
 *
 * Only atomic text poke/set should be allowed when not doing early patching.
 * It means the size must be writable atomically and the address must be aligned
 * in a way that permits an atomic write. It also makes sure we fit on a single
 * page.
 *
 * Context: should only be used by kgdb, which ensures no other core is running,
 *	    despite the fact it does not hold the text_mutex.
 */
void *text_poke_kgdb(void *addr, const void *opcode, size_t len)
{
	return __text_poke(addr, opcode, len);
}

static void do_sync_core(void *info)
{
	sync_core();
}

static struct bp_patching_desc {
	struct text_poke_loc *vec;
	int nr_entries;
} bp_patching;

static int patch_cmp(const void *key, const void *elt)
{
	struct text_poke_loc *tp = (struct text_poke_loc *) elt;

	if (key < tp->addr)
		return -1;
	if (key > tp->addr)
		return 1;
	return 0;
}
NOKPROBE_SYMBOL(patch_cmp);

int poke_int3_handler(struct pt_regs *regs)
{
	struct text_poke_loc *tp;
	unsigned char int3 = 0xcc;
	void *ip;

	/*
	 * Having observed our INT3 instruction, we now must observe
	 * bp_patching.nr_entries.
	 *
	 * 	nr_entries != 0			INT3
	 * 	WMB				RMB
	 * 	write INT3			if (nr_entries)
	 *
	 * Idem for other elements in bp_patching.
	 */
	smp_rmb();

	if (likely(!bp_patching.nr_entries))
		return 0;

	if (user_mode(regs))
		return 0;

	/*
	 * Discount the sizeof(int3). See text_poke_bp_batch().
	 */
	ip = (void *) regs->ip - sizeof(int3);

	/*
	 * Skip the binary search if there is a single member in the vector.
	 */
	if (unlikely(bp_patching.nr_entries > 1)) {
		tp = bsearch(ip, bp_patching.vec, bp_patching.nr_entries,
			     sizeof(struct text_poke_loc),
			     patch_cmp);
		if (!tp)
			return 0;
	} else {
		tp = bp_patching.vec;
		if (tp->addr != ip)
			return 0;
	}

	/* set up the specified breakpoint detour */
	regs->ip = (unsigned long) tp->detour;

	return 1;
}
NOKPROBE_SYMBOL(poke_int3_handler);

/**
 * text_poke_bp_batch() -- update instructions on live kernel on SMP
 * @tp:			vector of instructions to patch
 * @nr_entries:		number of entries in the vector
 *
 * Modify multi-byte instruction by using int3 breakpoint on SMP.
 * We completely avoid stop_machine() here, and achieve the
 * synchronization using int3 breakpoint.
 *
 * The way it is done:
 * 	- For each entry in the vector:
 *		- add a int3 trap to the address that will be patched
 *	- sync cores
 *	- For each entry in the vector:
 *		- update all but the first byte of the patched range
 *	- sync cores
 *	- For each entry in the vector:
 *		- replace the first byte (int3) by the first byte of
 *		  replacing opcode
 *	- sync cores
 */
void text_poke_bp_batch(struct text_poke_loc *tp, unsigned int nr_entries)
{
	int patched_all_but_first = 0;
	unsigned char int3 = 0xcc;
	unsigned int i;

	lockdep_assert_held(&text_mutex);

	bp_patching.vec = tp;
	bp_patching.nr_entries = nr_entries;

	/*
	 * Corresponding read barrier in int3 notifier for making sure the
	 * nr_entries and handler are correctly ordered wrt. patching.
	 */
	smp_wmb();

	/*
	 * First step: add a int3 trap to the address that will be patched.
	 */
	for (i = 0; i < nr_entries; i++)
		text_poke(tp[i].addr, &int3, sizeof(int3));

	on_each_cpu(do_sync_core, NULL, 1);

	/*
	 * Second step: update all but the first byte of the patched range.
	 */
	for (i = 0; i < nr_entries; i++) {
		if (tp[i].len - sizeof(int3) > 0) {
			text_poke((char *)tp[i].addr + sizeof(int3),
				  (const char *)tp[i].opcode + sizeof(int3),
				  tp[i].len - sizeof(int3));
			patched_all_but_first++;
		}
	}

	if (patched_all_but_first) {
		/*
		 * According to Intel, this core syncing is very likely
		 * not necessary and we'd be safe even without it. But
		 * better safe than sorry (plus there's not only Intel).
		 */
		on_each_cpu(do_sync_core, NULL, 1);
	}

	/*
	 * Third step: replace the first byte (int3) by the first byte of
	 * replacing opcode.
	 */
	for (i = 0; i < nr_entries; i++)
		text_poke(tp[i].addr, tp[i].opcode, sizeof(int3));

	on_each_cpu(do_sync_core, NULL, 1);
	/*
	 * sync_core() implies an smp_mb() and orders this store against
	 * the writing of the new instruction.
	 */
	bp_patching.vec = NULL;
	bp_patching.nr_entries = 0;
}

/**
 * text_poke_bp() -- update instructions on live kernel on SMP
 * @addr:	address to patch
 * @opcode:	opcode of new instruction
 * @len:	length to copy
 * @handler:	address to jump to when the temporary breakpoint is hit
 *
 * Update a single instruction with the vector in the stack, avoiding
 * dynamically allocated memory. This function should be used when it is
 * not possible to allocate memory.
 */
void text_poke_bp(void *addr, const void *opcode, size_t len, void *handler)
{
	struct text_poke_loc tp = {
		.detour = handler,
		.addr = addr,
		.len = len,
	};

	if (len > POKE_MAX_OPCODE_SIZE) {
		WARN_ONCE(1, "len is larger than %d\n", POKE_MAX_OPCODE_SIZE);
		return;
	}

	memcpy((void *)tp.opcode, opcode, len);

	text_poke_bp_batch(&tp, 1);
}
