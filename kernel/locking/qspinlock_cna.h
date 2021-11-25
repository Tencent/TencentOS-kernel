/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _GEN_CNA_LOCK_SLOWPATH
#error "do not include this file"
#endif

#include <linux/topology.h>
#include <linux/sched/clock.h>
#include <linux/sched/rt.h>
#include <linux/moduleparam.h>
#include <linux/random.h>

/*
 * Implement a NUMA-aware version of MCS (aka CNA, or compact NUMA-aware lock).
 *
 * In CNA, spinning threads are organized in two queues, a primary queue for
 * threads running on the same NUMA node as the current lock holder, and a
 * secondary queue for threads running on other nodes. Schematically, it
 * looks like this:
 *
 *    cna_node
 *   +----------+     +--------+         +--------+
 *   |mcs:next  | --> |mcs:next| --> ... |mcs:next| --> NULL  [Primary queue]
 *   |mcs:locked| -.  +--------+         +--------+
 *   +----------+  |
 *                 `----------------------.
 *                                        v
 *                 +--------+         +--------+
 *                 |mcs:next| --> ... |mcs:next|            [Secondary queue]
 *                 +--------+         +--------+
 *                     ^                    |
 *                     `--------------------'
 *
 * N.B. locked := 1 if secondary queue is absent. Otherwise, it contains the
 * encoded pointer to the tail of the secondary queue, which is organized as a
 * circular list.
 *
 * After acquiring the MCS lock and before acquiring the spinlock, the MCS lock
 * holder checks whether the next waiter in the primary queue (if exists) is
 * running on the same NUMA node. If it is not, that waiter is detached from the
 * main queue and moved into the tail of the secondary queue. This way, we
 * gradually filter the primary queue, leaving only waiters running on the same
 * preferred NUMA node.
 *
 * For more details, see https://arxiv.org/abs/1810.05600.
 *
 * Authors: Alex Kogan <alex.kogan@oracle.com>
 *          Dave Dice <dave.dice@oracle.com>
 */

#define FLUSH_SECONDARY_QUEUE	1

#define CNA_PRIORITY_NODE      0xffff

#define DEFAULT_LLC_ID 0xfffe

struct cna_node {
	struct mcs_spinlock	mcs;
	u16			llc_id;
	u16			real_llc_id;
	u32			encoded_tail;	/* self */
	u64			start_time;
};

bool use_llc_spinlock = false;

static ulong numa_spinlock_threshold_ns = 1000000;   /* 1ms, by default */
module_param(numa_spinlock_threshold_ns, ulong, 0644);

static inline bool intra_node_threshold_reached(struct cna_node *cn)
{
	u64 current_time = local_clock();
	u64 threshold = cn->start_time + numa_spinlock_threshold_ns;

	return current_time > threshold;
}

/*
 * Controls the probability for enabling the ordering of the main queue
 * when the secondary queue is empty. The chosen value reduces the amount
 * of unnecessary shuffling of threads between the two waiting queues
 * when the contention is low, while responding fast enough and enabling
 * the shuffling when the contention is high.
 */
#define SHUFFLE_REDUCTION_PROB_ARG  (7)

/* Per-CPU pseudo-random number seed */
static DEFINE_PER_CPU(u32, seed);

/*
 * Return false with probability 1 / 2^@num_bits.
 * Intuitively, the larger @num_bits the less likely false is to be returned.
 * @num_bits must be a number between 0 and 31.
 */
static bool probably(unsigned int num_bits)
{
	u32 s;

	s = this_cpu_read(seed);
	s = next_pseudo_random32(s);
	this_cpu_write(seed, s);

	return s & ((1 << num_bits) - 1);
}

static void __init cna_init_nodes_per_cpu(unsigned int cpu)
{
	struct mcs_spinlock *base = per_cpu_ptr(&qnodes[0].mcs, cpu);
	int i;

	for (i = 0; i < MAX_NODES; i++) {
		struct cna_node *cn = (struct cna_node *)grab_mcs_node(base, i);

		/*
		*cpu_llc_id is not initialized when when this function is called
		*so just set a fake llc id.
		*/
		cn->real_llc_id = DEFAULT_LLC_ID;
		cn->encoded_tail = encode_tail(cpu, i);
		/*
		 * make sure @encoded_tail is not confused with other valid
		 * values for @locked (0 or 1)
		 */
		WARN_ON(cn->encoded_tail <= 1);
	}
}

/*
* must be called after cpu_llc_id is initialized.
*/
void cna_set_llc_id_per_cpu(unsigned int cpu)
{
	struct mcs_spinlock *base = per_cpu_ptr(&qnodes[0].mcs, cpu);
	int i;

	if (!use_llc_spinlock)
		return;

	for (i = 0; i < MAX_NODES; i++) {
		struct cna_node *cn = (struct cna_node *)grab_mcs_node(base, i);

		cn->real_llc_id = per_cpu(cpu_llc_id, cpu);
	}
}

static int __init cna_init_nodes(void)
{
	unsigned int cpu;

	/*
	 * this will break on 32bit architectures, so we restrict
	 * the use of CNA to 64bit only (see arch/x86/Kconfig)
	 */
	BUILD_BUG_ON(sizeof(struct cna_node) > sizeof(struct qnode));
	/* we store an ecoded tail word in the node's @locked field */
	BUILD_BUG_ON(sizeof(u32) > sizeof(unsigned int));

	for_each_possible_cpu(cpu)
		cna_init_nodes_per_cpu(cpu);

	return 0;
}

static __always_inline void cna_init_node(struct mcs_spinlock *node)
{
	bool priority = !in_task() || irqs_disabled() || rt_task(current);
	struct cna_node *cn = (struct cna_node *)node;

	cn->llc_id = priority ? CNA_PRIORITY_NODE : cn->real_llc_id;
	cn->start_time = 0;
}

/*
 * cna_splice_head -- splice the entire secondary queue onto the head of the
 * primary queue.
 *
 * Returns the new primary head node or NULL on failure.
 */
static struct mcs_spinlock *
cna_splice_head(struct qspinlock *lock, u32 val,
		struct mcs_spinlock *node, struct mcs_spinlock *next)
{
	struct mcs_spinlock *head_2nd, *tail_2nd;
	u32 new;

	tail_2nd = decode_tail(node->locked);
	head_2nd = tail_2nd->next;

	if (next) {
		/*
		 * If the primary queue is not empty, the primary tail doesn't
		 * need to change and we can simply link the secondary tail to
		 * the old primary head.
		 */
		tail_2nd->next = next;
	} else {
		/*
		 * When the primary queue is empty, the secondary tail becomes
		 * the primary tail.
		 */

		/*
		 * Speculatively break the secondary queue's circular link such
		 * that when the secondary tail becomes the primary tail it all
		 * works out.
		 */
		tail_2nd->next = NULL;

		/*
		 * tail_2nd->next = NULL;	old = xchg_tail(lock, tail);
		 *				prev = decode_tail(old);
		 * try_cmpxchg_release(...);	WRITE_ONCE(prev->next, node);
		 *
		 * If the following cmpxchg() succeeds, our stores will not
		 * collide.
		 */
		new = ((struct cna_node *)tail_2nd)->encoded_tail |
			_Q_LOCKED_VAL;
		if (!atomic_try_cmpxchg_release(&lock->val, &val, new)) {
			/* Restore the secondary queue's circular link. */
			tail_2nd->next = head_2nd;
			return NULL;
		}
	}

	/* The primary queue head now is what was the secondary queue head. */
	return head_2nd;
}

static inline bool cna_try_clear_tail(struct qspinlock *lock, u32 val,
				      struct mcs_spinlock *node)
{
	/*
	 * We're here because the primary queue is empty; check the secondary
	 * queue for remote waiters.
	 */
	if (node->locked > 1) {
		struct mcs_spinlock *next;

		/*
		 * When there are waiters on the secondary queue, try to move
		 * them back onto the primary queue and let them rip.
		 */
		next = cna_splice_head(lock, val, node, NULL);
		if (next) {
			arch_mcs_lock_handoff(&next->locked, 1);
			return true;
		}

		return false;
	}

	/* Both queues are empty. Do what MCS does. */
	return __try_clear_tail(lock, val, node);
}

/*
 * cna_splice_next -- splice the next node from the primary queue onto
 * the secondary queue.
 */
static void cna_splice_next(struct mcs_spinlock *node,
			    struct mcs_spinlock *next,
			    struct mcs_spinlock *nnext)
{
	/* remove 'next' from the main queue */
	node->next = nnext;

	/* stick `next` on the secondary queue tail */
	if (node->locked <= 1) { /* if secondary queue is empty */
		struct cna_node *cn = (struct cna_node *)node;

		/* create secondary queue */
		next->next = next;

		cn->start_time = local_clock();
		/* secondary queue is not empty iff start_time != 0 */
		WARN_ON(!cn->start_time);
	} else {
		/* add to the tail of the secondary queue */
		struct mcs_spinlock *tail_2nd = decode_tail(node->locked);
		struct mcs_spinlock *head_2nd = tail_2nd->next;

		tail_2nd->next = next;
		next->next = head_2nd;
	}

	node->locked = ((struct cna_node *)next)->encoded_tail;
}

/*
 * cna_order_queue - check whether the next waiter in the main queue is on
 * the same NUMA node as the lock holder; if not, and it has a waiter behind
 * it in the main queue, move the former onto the secondary queue.
 * Returns 1 if the next waiter runs on the same NUMA node; 0 otherwise.
 */
static int cna_order_queue(struct mcs_spinlock *node)
{
	struct mcs_spinlock *next = READ_ONCE(node->next);
	struct cna_node *cn = (struct cna_node *)node;
	int llc_id, next_llc_id;

	if (!next)
		return 0;

	llc_id = cn->llc_id;
	next_llc_id = ((struct cna_node *)next)->llc_id;

	if (next_llc_id != llc_id && next_llc_id != CNA_PRIORITY_NODE) {
		struct mcs_spinlock *nnext = READ_ONCE(next->next);

		if (nnext)
			cna_splice_next(node, next, nnext);

		return 0;
	}
	return 1;
}

#define LOCK_IS_BUSY(lock) (atomic_read(&(lock)->val) & _Q_LOCKED_PENDING_MASK)

/* Abuse the pv_wait_head_or_lock() hook to get some work done */
static __always_inline u32 cna_wait_head_or_lock(struct qspinlock *lock,
						 struct mcs_spinlock *node)
{
	struct cna_node *cn = (struct cna_node *)node;

	if (node->locked <= 1 && probably(SHUFFLE_REDUCTION_PROB_ARG)) {
		/*
		 * When the secondary queue is empty, skip the calls to
		 * cna_order_queue() below with high probability. This optimization
		 * reduces the overhead of unnecessary shuffling of threads
		 * between waiting queues when the lock is only lightly contended.
		 */
		return 0;
	}

	if (!cn->start_time || !intra_node_threshold_reached(cn)) {
		
		/*
		 * We are at the head of the wait queue, no need to use
		 * the fake NUMA node ID.
		 */
		if (cn->llc_id == CNA_PRIORITY_NODE)
			cn->llc_id = cn->real_llc_id;

		/*
		 * Try and put the time otherwise spent spin waiting on
		 * _Q_LOCKED_PENDING_MASK to use by sorting our lists.
		 */
		while (LOCK_IS_BUSY(lock) && !cna_order_queue(node))
			cpu_relax();
	} else {
		cn->start_time = FLUSH_SECONDARY_QUEUE;
	}

	return 0; /* we lied; we didn't wait, go do so now */
}

static inline void cna_lock_handoff(struct mcs_spinlock *node,
				 struct mcs_spinlock *next)
{
	struct cna_node *cn = (struct cna_node *)node;
	u32 val = 1;


	if (cn->start_time != FLUSH_SECONDARY_QUEUE) {
		if (node->locked > 1) {
			val = node->locked;	/* preseve secondary queue */

			/*
			 * We have a local waiter, either real or fake one;
			 * reload @next in case it was changed by cna_order_queue().
			 */
			next = node->next;

			/*
			 * Pass over NUMA node id of primary queue, to maintain the
			 * preference even if the next waiter is on a different node.
			 */
			((struct cna_node *)next)->llc_id = cn->llc_id;

			((struct cna_node *)next)->start_time = cn->start_time;
		}
	} else {
		/*
		 * We decided to flush the secondary queue;
		 * this can only happen if that queue is not empty.
		 */

		WARN_ON(node->locked <= 1);
		/*
		 * Splice the secondary queue onto the primary queue and pass the lock
		 * to the longest waiting remote waiter.
		 */

		next = cna_splice_head(NULL, 0, node, next);
 	}

	arch_mcs_lock_handoff(&next->locked, val);
}

/*
 * Constant (boot-param configurable) flag selecting the LLC-aware variant
 * of spinlock.  Possible values: -1 (off) / 0 (auto, default) / 1 (on).
 */
static int llc_spinlock_flag = -1;

static int __init llc_spinlock_setup(char *str)
{
	if (!strcmp(str, "auto")) {
		llc_spinlock_flag = 0;
		return 1;
	} else if (!strcmp(str, "on")) {
		llc_spinlock_flag = 1;
		return 1;
	} else if (!strcmp(str, "off")) {
		llc_spinlock_flag = -1;
		return 1;
	}

	return 0;
}
__setup("llc_spinlock=", llc_spinlock_setup);

void __cna_queued_spin_lock_slowpath(struct qspinlock *lock, u32 val);

/*
 * Switch to the llc-friendly slow path for spinlocks when we have
 * multiple llc nodes in native environment, unless the user has
 * overridden this default behavior by setting the llc_spinlock flag.
 */
void __init cna_configure_spin_lock_slowpath(void)
{
	if (llc_spinlock_flag < 0)
		return;

	if (llc_spinlock_flag == 0 && 
		    pv_ops.lock.queued_spin_lock_slowpath !=
			native_queued_spin_lock_slowpath)
		return;

	cna_init_nodes();

	use_llc_spinlock = true;

	pv_ops.lock.queued_spin_lock_slowpath = __cna_queued_spin_lock_slowpath;

	pr_info("Enabling CNA spinlock\n");
}

