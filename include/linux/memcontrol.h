/* SPDX-License-Identifier: GPL-2.0-or-later */
/* memcontrol.h - Memory Controller
 *
 * Copyright IBM Corporation, 2007
 * Author Balbir Singh <balbir@linux.vnet.ibm.com>
 *
 * Copyright 2007 OpenVZ SWsoft Inc
 * Author: Pavel Emelianov <xemul@openvz.org>
 */

#ifndef _LINUX_MEMCONTROL_H
#define _LINUX_MEMCONTROL_H
#include <linux/cgroup.h>
#include <linux/vm_event_item.h>
#include <linux/hardirq.h>
#include <linux/jump_label.h>
#include <linux/page_counter.h>
#include <linux/vmpressure.h>
#include <linux/eventfd.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/writeback.h>
#include <linux/page-flags.h>

struct mem_cgroup;
struct page;
struct mm_struct;
struct kmem_cache;

/* Cgroup-specific page state, on top of universal node page state */
enum memcg_stat_item {
	MEMCG_CACHE = NR_VM_NODE_STAT_ITEMS,
	MEMCG_RSS,
	MEMCG_RSS_HUGE,
	MEMCG_SWAP,
	MEMCG_SOCK,
	/* XXX: why are these zone and not node counters? */
	MEMCG_KERNEL_STACK_KB,
	MEMCG_NR_STAT,
};

enum memcg_memory_event {
	MEMCG_LOW,
	MEMCG_HIGH,
	MEMCG_MAX,
	MEMCG_OOM,
	MEMCG_OOM_KILL,
	MEMCG_SWAP_MAX,
	MEMCG_SWAP_FAIL,
	MEMCG_PAGECACHE_MAX,
	MEMCG_PAGECACHE_OOM,
	MEMCG_NR_MEMORY_EVENTS,
};

enum mem_cgroup_protection {
	MEMCG_PROT_NONE,
	MEMCG_PROT_LOW,
	MEMCG_PROT_MIN,
};

struct mem_cgroup_reclaim_cookie {
	pg_data_t *pgdat;
	int priority;
	unsigned int generation;
};

#ifdef CONFIG_MEMCG

#define MEM_CGROUP_ID_SHIFT	16
#define MEM_CGROUP_ID_MAX	USHRT_MAX

struct mem_cgroup_id {
	int id;
	refcount_t ref;
};

/*
 * Per memcg event counter is incremented at every pagein/pageout. With THP,
 * it will be incremated by the number of pages. This counter is used for
 * for trigger some periodic events. This is straightforward and better
 * than using jiffies etc. to handle periodic memcg event.
 */
enum mem_cgroup_events_target {
	MEM_CGROUP_TARGET_THRESH,
	MEM_CGROUP_TARGET_SOFTLIMIT,
	MEM_CGROUP_TARGET_NUMAINFO,
	MEM_CGROUP_NTARGETS,
};

struct memcg_vmstats_percpu {
	long stat[MEMCG_NR_STAT];
	unsigned long events[NR_VM_EVENT_ITEMS];
	unsigned long nr_page_events;
	unsigned long targets[MEM_CGROUP_NTARGETS];
};

struct mem_cgroup_reclaim_iter {
	struct mem_cgroup *position;
	/* scan generation, increased every round-trip */
	unsigned int generation;
};

struct lruvec_stat {
	long count[NR_VM_NODE_STAT_ITEMS];
};

/*
 * Bitmap of shrinker::id corresponding to memcg-aware shrinkers,
 * which have elements charged to this memcg.
 */
struct memcg_shrinker_map {
	struct rcu_head rcu;
	unsigned long map[0];
};

/*
 * per-zone information in memory controller.
 */
struct mem_cgroup_per_node {
	struct lruvec		lruvec;

	/* Legacy local VM stats */
	struct lruvec_stat __percpu *lruvec_stat_local;

	/* Subtree VM stats (batched updates) */
	struct lruvec_stat __percpu *lruvec_stat_cpu;
	atomic_long_t		lruvec_stat[NR_VM_NODE_STAT_ITEMS];

	unsigned long		lru_zone_size[MAX_NR_ZONES][NR_LRU_LISTS];

	struct mem_cgroup_reclaim_iter	iter[DEF_PRIORITY + 1];

	struct memcg_shrinker_map __rcu	*shrinker_map;

	struct rb_node		tree_node;	/* RB tree node */
	unsigned long		usage_in_excess;/* Set to the value by which */
						/* the soft limit is exceeded*/
	bool			on_tree;
	bool			congested;	/* memcg has many dirty pages */
						/* backed by a congested BDI */

	struct mem_cgroup	*memcg;		/* Back pointer, we cannot */
						/* use container_of	   */
};

struct mem_cgroup_threshold {
	struct eventfd_ctx *eventfd;
	unsigned long threshold;
};

/* For threshold */
struct mem_cgroup_threshold_ary {
	/* An array index points to threshold just below or equal to usage. */
	int current_threshold;
	/* Size of entries[] */
	unsigned int size;
	/* Array of thresholds */
	struct mem_cgroup_threshold entries[0];
};

struct mem_cgroup_thresholds {
	/* Primary thresholds array */
	struct mem_cgroup_threshold_ary *primary;
	/*
	 * Spare threshold array.
	 * This is needed to make mem_cgroup_unregister_event() "never fail".
	 * It must be able to store at least primary->size - 1 entries.
	 */
	struct mem_cgroup_threshold_ary *spare;
};

enum memcg_kmem_state {
	KMEM_NONE,
	KMEM_ALLOCATED,
	KMEM_ONLINE,
};

#if defined(CONFIG_SMP)
struct memcg_padding {
	char x[0];
} ____cacheline_internodealigned_in_smp;
#define MEMCG_PADDING(name)      struct memcg_padding name;
#else
#define MEMCG_PADDING(name)
#endif

/*
 * Remember four most recent foreign writebacks with dirty pages in this
 * cgroup.  Inode sharing is expected to be uncommon and, even if we miss
 * one in a given round, we're likely to catch it later if it keeps
 * foreign-dirtying, so a fairly low count should be enough.
 *
 * See mem_cgroup_track_foreign_dirty_slowpath() for details.
 */
#define MEMCG_CGWB_FRN_CNT	4

struct memcg_cgwb_frn {
	u64 bdi_id;			/* bdi->id of the foreign inode */
	int memcg_id;			/* memcg->css.id of foreign inode */
	u64 at;				/* jiffies_64 at the time of dirtying */
	struct wb_completion done;	/* tracks in-flight foreign writebacks */
};

/*
 * The memory controller data structure. The memory controller controls both
 * page cache and RSS per cgroup. We would eventually like to provide
 * statistics based on the statistics developed by Rik Van Riel for clock-pro,
 * to help the administrator determine what knobs to tune.
 */
struct mem_cgroup {
	struct cgroup_subsys_state css;

	/* Private memcg ID. Used to ID objects that outlive the cgroup */
	struct mem_cgroup_id id;

	/* Accounted resources */
	struct page_counter memory;
	struct page_counter swap;
	struct page_counter pagecache;
	u64 pagecache_reclaim_ratio;
	u32 pagecache_max_ratio;

	/* Legacy consumer-oriented counters */
	struct page_counter memsw;
	struct page_counter kmem;
	struct page_counter tcpmem;

	/* Upper bound of normal memory consumption range */
	unsigned long high;

	/* Range enforcement for interrupt charges */
	struct work_struct high_work;

	unsigned long soft_limit;

	/* vmpressure notifications */
	struct vmpressure vmpressure;

	/*
	 * Should the accounting and control be hierarchical, per subtree?
	 */
	bool use_hierarchy;
	bool meminfo_recursive;

	/*
	 * Should the OOM killer kill all belonging tasks, had it kill one?
	 */
	bool oom_group;

	/* protected by memcg_oom_lock */
	bool		oom_lock;
	int		under_oom;

	int	swappiness;
	/* OOM-Killer disable */
	int		oom_kill_disable;

	/* memory.events and memory.events.local */
	struct cgroup_file events_file;
	struct cgroup_file events_local_file;

	/* handle for "memory.swap.events" */
	struct cgroup_file swap_events_file;

	/* protect arrays of thresholds */
	struct mutex thresholds_lock;

	/* thresholds for memory usage. RCU-protected */
	struct mem_cgroup_thresholds thresholds;

	/* thresholds for mem+swap usage. RCU-protected */
	struct mem_cgroup_thresholds memsw_thresholds;

	/* For oom notifier event fd */
	struct list_head oom_notify;

	/*
	 * Should we move charges of a task when a task is moved into this
	 * mem_cgroup ? And what type of charges should we move ?
	 */
	unsigned long move_charge_at_immigrate;
	/* taken only while moving_account > 0 */
	spinlock_t		move_lock;
	unsigned long		move_lock_flags;

	MEMCG_PADDING(_pad1_);

	/*
	 * set > 0 if pages under this cgroup are moving to other cgroup.
	 */
	atomic_t		moving_account;
	struct task_struct	*move_lock_task;

	/* Legacy local VM stats and events */
	struct memcg_vmstats_percpu __percpu *vmstats_local;

	/* Subtree VM stats and events (batched updates) */
	struct memcg_vmstats_percpu __percpu *vmstats_percpu;

	MEMCG_PADDING(_pad2_);

	atomic_long_t		vmstats[MEMCG_NR_STAT];
	atomic_long_t		vmevents[NR_VM_EVENT_ITEMS];

	/* memory.events */
	atomic_long_t		memory_events[MEMCG_NR_MEMORY_EVENTS];
	atomic_long_t		memory_events_local[MEMCG_NR_MEMORY_EVENTS];

	unsigned long		socket_pressure;

	/* Legacy tcp memory accounting */
	bool			tcpmem_active;
	int			tcpmem_pressure;

#ifdef CONFIG_MEMCG_KMEM
        /* Index in the kmem_cache->memcg_params.memcg_caches array */
	int kmemcg_id;
	enum memcg_kmem_state kmem_state;
	struct list_head kmem_caches;
#endif

	int last_scanned_node;
#if MAX_NUMNODES > 1
	nodemask_t	scan_nodes;
	atomic_t	numainfo_events;
	atomic_t	numainfo_updating;
#endif

#ifdef CONFIG_CGROUP_WRITEBACK
	struct list_head cgwb_list;
	struct wb_domain cgwb_domain;
	struct memcg_cgwb_frn cgwb_frn[MEMCG_CGWB_FRN_CNT];
#endif

	/* List of events which userspace want to receive */
	struct list_head event_list;
	spinlock_t event_list_lock;

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	struct deferred_split deferred_split_queue;
#endif

	struct mem_cgroup_per_node *nodeinfo[0];
	/* WARNING: nodeinfo must be the last member here */
};

/*
 * size of first charge trial. "32" comes from vmscan.c's magic value.
 * TODO: maybe necessary to use big numbers in big irons.
 */
#define MEMCG_CHARGE_BATCH 32U

/*
 * Iteration constructs for visiting all cgroups (under a tree).  If
 * loops are exited prematurely (break), mem_cgroup_iter_break() must
 * be used for reference counting.
 */
#define for_each_mem_cgroup_tree(iter, root)		\
	for (iter = mem_cgroup_iter(root, NULL, NULL);	\
	     iter != NULL;				\
	     iter = mem_cgroup_iter(root, iter, NULL))

#define for_each_mem_cgroup(iter)			\
	for (iter = mem_cgroup_iter(NULL, NULL, NULL);	\
	     iter != NULL;				\
	     iter = mem_cgroup_iter(NULL, iter, NULL))

extern struct mem_cgroup *root_mem_cgroup;

static inline bool mem_cgroup_is_root(struct mem_cgroup *memcg)
{
	return (memcg == root_mem_cgroup);
}

static inline bool mem_cgroup_disabled(void)
{
	return !cgroup_subsys_enabled(memory_cgrp_subsys);
}

static inline unsigned long mem_cgroup_protection(struct mem_cgroup *memcg,
						  bool in_low_reclaim)
{
	if (mem_cgroup_disabled())
		return 0;

	if (in_low_reclaim)
		return READ_ONCE(memcg->memory.emin);

	return max(READ_ONCE(memcg->memory.emin),
		   READ_ONCE(memcg->memory.elow));
}

enum mem_cgroup_protection mem_cgroup_protected(struct mem_cgroup *root,
						struct mem_cgroup *memcg);

int mem_cgroup_try_charge(struct page *page, struct mm_struct *mm,
			  gfp_t gfp_mask, struct mem_cgroup **memcgp,
			  bool compound);
int mem_cgroup_try_charge_delay(struct page *page, struct mm_struct *mm,
			  gfp_t gfp_mask, struct mem_cgroup **memcgp,
			  bool compound);
void mem_cgroup_commit_charge(struct page *page, struct mem_cgroup *memcg,
			      bool lrucare, bool compound);
void mem_cgroup_cancel_charge(struct page *page, struct mem_cgroup *memcg,
		bool compound);
void mem_cgroup_uncharge(struct page *page);
void mem_cgroup_uncharge_list(struct list_head *page_list);

void mem_cgroup_migrate(struct page *oldpage, struct page *newpage);

static struct mem_cgroup_per_node *
mem_cgroup_nodeinfo(struct mem_cgroup *memcg, int nid)
{
	return memcg->nodeinfo[nid];
}

/**
 * mem_cgroup_lruvec - get the lru list vector for a node or a memcg zone
 * @node: node of the wanted lruvec
 * @memcg: memcg of the wanted lruvec
 *
 * Returns the lru list vector holding pages for a given @node or a given
 * @memcg and @zone. This can be the node lruvec, if the memory controller
 * is disabled.
 */
static inline struct lruvec *mem_cgroup_lruvec(struct pglist_data *pgdat,
				struct mem_cgroup *memcg)
{
	struct mem_cgroup_per_node *mz;
	struct lruvec *lruvec;

	if (mem_cgroup_disabled()) {
		lruvec = node_lruvec(pgdat);
		goto out;
	}

	mz = mem_cgroup_nodeinfo(memcg, pgdat->node_id);
	lruvec = &mz->lruvec;
out:
	/*
	 * Since a node can be onlined after the mem_cgroup was created,
	 * we have to be prepared to initialize lruvec->pgdat here;
	 * and if offlined then reonlined, we need to reinitialize it.
	 */
	if (unlikely(lruvec->pgdat != pgdat))
		lruvec->pgdat = pgdat;
	return lruvec;
}

struct lruvec *mem_cgroup_page_lruvec(struct page *, struct pglist_data *);

struct mem_cgroup *mem_cgroup_from_task(struct task_struct *p);

struct mem_cgroup *get_mem_cgroup_from_mm(struct mm_struct *mm);

struct mem_cgroup *get_mem_cgroup_from_page(struct page *page);

static inline
struct mem_cgroup *mem_cgroup_from_css(struct cgroup_subsys_state *css){
	return css ? container_of(css, struct mem_cgroup, css) : NULL;
}

static inline void mem_cgroup_put(struct mem_cgroup *memcg)
{
	if (memcg)
		css_put(&memcg->css);
}

#define mem_cgroup_from_counter(counter, member)	\
	container_of(counter, struct mem_cgroup, member)

struct mem_cgroup *mem_cgroup_iter(struct mem_cgroup *,
				   struct mem_cgroup *,
				   struct mem_cgroup_reclaim_cookie *);
void mem_cgroup_iter_break(struct mem_cgroup *, struct mem_cgroup *);
int mem_cgroup_scan_tasks(struct mem_cgroup *,
			  int (*)(struct task_struct *, void *), void *);

static inline unsigned short mem_cgroup_id(struct mem_cgroup *memcg)
{
	if (mem_cgroup_disabled())
		return 0;

	return memcg->id.id;
}
struct mem_cgroup *mem_cgroup_from_id(unsigned short id);

static inline struct mem_cgroup *mem_cgroup_from_seq(struct seq_file *m)
{
	return mem_cgroup_from_css(seq_css(m));
}

static inline struct mem_cgroup *lruvec_memcg(struct lruvec *lruvec)
{
	struct mem_cgroup_per_node *mz;

	if (mem_cgroup_disabled())
		return NULL;

	mz = container_of(lruvec, struct mem_cgroup_per_node, lruvec);
	return mz->memcg;
}

/**
 * parent_mem_cgroup - find the accounting parent of a memcg
 * @memcg: memcg whose parent to find
 *
 * Returns the parent memcg, or NULL if this is the root or the memory
 * controller is in legacy no-hierarchy mode.
 */
static inline struct mem_cgroup *parent_mem_cgroup(struct mem_cgroup *memcg)
{
	if (!memcg->memory.parent)
		return NULL;
	return mem_cgroup_from_counter(memcg->memory.parent, memory);
}

static inline bool mem_cgroup_is_descendant(struct mem_cgroup *memcg,
			      struct mem_cgroup *root)
{
	if (root == memcg)
		return true;
	if (!root->use_hierarchy)
		return false;
	return cgroup_is_descendant(memcg->css.cgroup, root->css.cgroup);
}

static inline bool mm_match_cgroup(struct mm_struct *mm,
				   struct mem_cgroup *memcg)
{
	struct mem_cgroup *task_memcg;
	bool match = false;

	rcu_read_lock();
	task_memcg = mem_cgroup_from_task(rcu_dereference(mm->owner));
	if (task_memcg)
		match = mem_cgroup_is_descendant(task_memcg, memcg);
	rcu_read_unlock();
	return match;
}

struct cgroup_subsys_state *mem_cgroup_css_from_page(struct page *page);
ino_t page_cgroup_ino(struct page *page);

static inline bool mem_cgroup_online(struct mem_cgroup *memcg)
{
	if (mem_cgroup_disabled())
		return true;
	return !!(memcg->css.flags & CSS_ONLINE);
}

/*
 * For memory reclaim.
 */
int mem_cgroup_select_victim_node(struct mem_cgroup *memcg);

void mem_cgroup_update_lru_size(struct lruvec *lruvec, enum lru_list lru,
		int zid, int nr_pages);

static inline
unsigned long mem_cgroup_get_zone_lru_size(struct lruvec *lruvec,
		enum lru_list lru, int zone_idx)
{
	struct mem_cgroup_per_node *mz;

	mz = container_of(lruvec, struct mem_cgroup_per_node, lruvec);
	return mz->lru_zone_size[zone_idx][lru];
}

void mem_cgroup_handle_over_high(void);

unsigned long mem_cgroup_get_max(struct mem_cgroup *memcg);

unsigned long mem_cgroup_size(struct mem_cgroup *memcg);

void mem_cgroup_print_oom_context(struct mem_cgroup *memcg,
				struct task_struct *p);

void mem_cgroup_print_oom_meminfo(struct mem_cgroup *memcg);

static inline void mem_cgroup_enter_user_fault(void)
{
	WARN_ON(current->in_user_fault);
	current->in_user_fault = 1;
}

static inline void mem_cgroup_exit_user_fault(void)
{
	WARN_ON(!current->in_user_fault);
	current->in_user_fault = 0;
}

static inline bool task_in_memcg_oom(struct task_struct *p)
{
	return p->memcg_in_oom;
}

bool mem_cgroup_oom_synchronize(bool wait);
struct mem_cgroup *mem_cgroup_get_oom_group(struct task_struct *victim,
					    struct mem_cgroup *oom_domain);
void mem_cgroup_print_oom_group(struct mem_cgroup *memcg);

#ifdef CONFIG_MEMCG_SWAP
extern int do_swap_account;
#endif

struct mem_cgroup *lock_page_memcg(struct page *page);
void __unlock_page_memcg(struct mem_cgroup *memcg);
void unlock_page_memcg(struct page *page);

/*
 * idx can be of type enum memcg_stat_item or node_stat_item.
 * Keep in sync with memcg_exact_page_state().
 */
static inline unsigned long memcg_page_state(struct mem_cgroup *memcg, int idx)
{
	long x = atomic_long_read(&memcg->vmstats[idx]);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;
}

/*
 * idx can be of type enum memcg_stat_item or node_stat_item.
 * Keep in sync with memcg_exact_page_state().
 */
static inline unsigned long memcg_page_state_local(struct mem_cgroup *memcg,
						   int idx)
{
	long x = 0;
	int cpu;

	for_each_possible_cpu(cpu)
		x += per_cpu(memcg->vmstats_local->stat[idx], cpu);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;
}

void __mod_memcg_state(struct mem_cgroup *memcg, int idx, int val);

/* idx can be of type enum memcg_stat_item or node_stat_item */
static inline void mod_memcg_state(struct mem_cgroup *memcg,
				   int idx, int val)
{
	unsigned long flags;

	local_irq_save(flags);
	__mod_memcg_state(memcg, idx, val);
	local_irq_restore(flags);
}

/**
 * mod_memcg_page_state - update page state statistics
 * @page: the page
 * @idx: page state item to account
 * @val: number of pages (positive or negative)
 *
 * The @page must be locked or the caller must use lock_page_memcg()
 * to prevent double accounting when the page is concurrently being
 * moved to another memcg:
 *
 *   lock_page(page) or lock_page_memcg(page)
 *   if (TestClearPageState(page))
 *     mod_memcg_page_state(page, state, -1);
 *   unlock_page(page) or unlock_page_memcg(page)
 *
 * Kernel pages are an exception to this, since they'll never move.
 */
static inline void __mod_memcg_page_state(struct page *page,
					  int idx, int val)
{
	if (page->mem_cgroup)
		__mod_memcg_state(page->mem_cgroup, idx, val);
}

static inline void mod_memcg_page_state(struct page *page,
					int idx, int val)
{
	if (page->mem_cgroup)
		mod_memcg_state(page->mem_cgroup, idx, val);
}

static inline unsigned long lruvec_page_state(struct lruvec *lruvec,
					      enum node_stat_item idx)
{
	struct mem_cgroup_per_node *pn;
	long x;

	if (mem_cgroup_disabled())
		return node_page_state(lruvec_pgdat(lruvec), idx);

	pn = container_of(lruvec, struct mem_cgroup_per_node, lruvec);
	x = atomic_long_read(&pn->lruvec_stat[idx]);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;
}

static inline unsigned long lruvec_page_state_local(struct lruvec *lruvec,
						    enum node_stat_item idx)
{
	struct mem_cgroup_per_node *pn;
	long x = 0;
	int cpu;

	if (mem_cgroup_disabled())
		return node_page_state(lruvec_pgdat(lruvec), idx);

	pn = container_of(lruvec, struct mem_cgroup_per_node, lruvec);
	for_each_possible_cpu(cpu)
		x += per_cpu(pn->lruvec_stat_local->count[idx], cpu);
#ifdef CONFIG_SMP
	if (x < 0)
		x = 0;
#endif
	return x;
}

void __mod_lruvec_state(struct lruvec *lruvec, enum node_stat_item idx,
			int val);
void __mod_lruvec_slab_state(void *p, enum node_stat_item idx, int val);
void mod_memcg_obj_state(void *p, int idx, int val);

static inline void mod_lruvec_state(struct lruvec *lruvec,
				    enum node_stat_item idx, int val)
{
	unsigned long flags;

	local_irq_save(flags);
	__mod_lruvec_state(lruvec, idx, val);
	local_irq_restore(flags);
}

static inline void __mod_lruvec_page_state(struct page *page,
					   enum node_stat_item idx, int val)
{
	pg_data_t *pgdat = page_pgdat(page);
	struct lruvec *lruvec;

	/* Untracked pages have no memcg, no lruvec. Update only the node */
	if (!page->mem_cgroup) {
		__mod_node_page_state(pgdat, idx, val);
		return;
	}

	lruvec = mem_cgroup_lruvec(pgdat, page->mem_cgroup);
	__mod_lruvec_state(lruvec, idx, val);
}

static inline void mod_lruvec_page_state(struct page *page,
					 enum node_stat_item idx, int val)
{
	unsigned long flags;

	local_irq_save(flags);
	__mod_lruvec_page_state(page, idx, val);
	local_irq_restore(flags);
}

unsigned long mem_cgroup_soft_limit_reclaim(pg_data_t *pgdat, int order,
						gfp_t gfp_mask,
						unsigned long *total_scanned);

void __count_memcg_events(struct mem_cgroup *memcg, enum vm_event_item idx,
			  unsigned long count);

static inline void count_memcg_events(struct mem_cgroup *memcg,
				      enum vm_event_item idx,
				      unsigned long count)
{
	unsigned long flags;

	local_irq_save(flags);
	__count_memcg_events(memcg, idx, count);
	local_irq_restore(flags);
}

static inline void count_memcg_page_event(struct page *page,
					  enum vm_event_item idx)
{
	if (page->mem_cgroup)
		count_memcg_events(page->mem_cgroup, idx, 1);
}

static inline void count_memcg_event_mm(struct mm_struct *mm,
					enum vm_event_item idx)
{
	struct mem_cgroup *memcg;

	if (mem_cgroup_disabled())
		return;

	rcu_read_lock();
	memcg = mem_cgroup_from_task(rcu_dereference(mm->owner));
	if (likely(memcg))
		count_memcg_events(memcg, idx, 1);
	rcu_read_unlock();
}

static inline void memcg_memory_event(struct mem_cgroup *memcg,
				      enum memcg_memory_event event)
{
	atomic_long_inc(&memcg->memory_events_local[event]);
	cgroup_file_notify(&memcg->events_local_file);

	do {
		atomic_long_inc(&memcg->memory_events[event]);
		cgroup_file_notify(&memcg->events_file);

		if (!cgroup_subsys_on_dfl(memory_cgrp_subsys))
			break;
		if (cgrp_dfl_root.flags & CGRP_ROOT_MEMORY_LOCAL_EVENTS)
			break;
	} while ((memcg = parent_mem_cgroup(memcg)) &&
		 !mem_cgroup_is_root(memcg));
}

static inline void memcg_memory_event_mm(struct mm_struct *mm,
					 enum memcg_memory_event event)
{
	struct mem_cgroup *memcg;

	if (mem_cgroup_disabled())
		return;

	rcu_read_lock();
	memcg = mem_cgroup_from_task(rcu_dereference(mm->owner));
	if (likely(memcg))
		memcg_memory_event(memcg, event);
	rcu_read_unlock();
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
void mem_cgroup_split_huge_fixup(struct page *head);
#endif

#else /* CONFIG_MEMCG */

#define MEM_CGROUP_ID_SHIFT	0
#define MEM_CGROUP_ID_MAX	0

struct mem_cgroup;

static inline bool mem_cgroup_is_root(struct mem_cgroup *memcg)
{
	return true;
}

static inline bool mem_cgroup_disabled(void)
{
	return true;
}

static inline void memcg_memory_event(struct mem_cgroup *memcg,
				      enum memcg_memory_event event)
{
}

static inline void memcg_memory_event_mm(struct mm_struct *mm,
					 enum memcg_memory_event event)
{
}

static inline unsigned long mem_cgroup_protection(struct mem_cgroup *memcg,
						  bool in_low_reclaim)
{
	return 0;
}

static inline enum mem_cgroup_protection mem_cgroup_protected(
	struct mem_cgroup *root, struct mem_cgroup *memcg)
{
	return MEMCG_PROT_NONE;
}

static inline int mem_cgroup_try_charge(struct page *page, struct mm_struct *mm,
					gfp_t gfp_mask,
					struct mem_cgroup **memcgp,
					bool compound)
{
	*memcgp = NULL;
	return 0;
}

static inline int mem_cgroup_try_charge_delay(struct page *page,
					      struct mm_struct *mm,
					      gfp_t gfp_mask,
					      struct mem_cgroup **memcgp,
					      bool compound)
{
	*memcgp = NULL;
	return 0;
}

static inline void mem_cgroup_commit_charge(struct page *page,
					    struct mem_cgroup *memcg,
					    bool lrucare, bool compound)
{
}

static inline void mem_cgroup_cancel_charge(struct page *page,
					    struct mem_cgroup *memcg,
					    bool compound)
{
}

static inline void mem_cgroup_uncharge(struct page *page)
{
}

static inline void mem_cgroup_uncharge_list(struct list_head *page_list)
{
}

static inline void mem_cgroup_migrate(struct page *old, struct page *new)
{
}

static inline struct lruvec *mem_cgroup_lruvec(struct pglist_data *pgdat,
				struct mem_cgroup *memcg)
{
	return node_lruvec(pgdat);
}

static inline struct lruvec *mem_cgroup_page_lruvec(struct page *page,
						    struct pglist_data *pgdat)
{
	return &pgdat->lruvec;
}

static inline bool mm_match_cgroup(struct mm_struct *mm,
		struct mem_cgroup *memcg)
{
	return true;
}

static inline struct mem_cgroup *get_mem_cgroup_from_mm(struct mm_struct *mm)
{
	return NULL;
}

static inline struct mem_cgroup *get_mem_cgroup_from_page(struct page *page)
{
	return NULL;
}

static inline void mem_cgroup_put(struct mem_cgroup *memcg)
{
}

static inline struct mem_cgroup *
mem_cgroup_iter(struct mem_cgroup *root,
		struct mem_cgroup *prev,
		struct mem_cgroup_reclaim_cookie *reclaim)
{
	return NULL;
}

static inline void mem_cgroup_iter_break(struct mem_cgroup *root,
					 struct mem_cgroup *prev)
{
}

static inline int mem_cgroup_scan_tasks(struct mem_cgroup *memcg,
		int (*fn)(struct task_struct *, void *), void *arg)
{
	return 0;
}

static inline unsigned short mem_cgroup_id(struct mem_cgroup *memcg)
{
	return 0;
}

static inline struct mem_cgroup *mem_cgroup_from_id(unsigned short id)
{
	WARN_ON_ONCE(id);
	/* XXX: This should always return root_mem_cgroup */
	return NULL;
}

static inline struct mem_cgroup *mem_cgroup_from_seq(struct seq_file *m)
{
	return NULL;
}

static inline struct mem_cgroup *lruvec_memcg(struct lruvec *lruvec)
{
	return NULL;
}

static inline bool mem_cgroup_online(struct mem_cgroup *memcg)
{
	return true;
}

static inline
unsigned long mem_cgroup_get_zone_lru_size(struct lruvec *lruvec,
		enum lru_list lru, int zone_idx)
{
	return 0;
}

static inline unsigned long mem_cgroup_get_max(struct mem_cgroup *memcg)
{
	return 0;
}

static inline unsigned long mem_cgroup_size(struct mem_cgroup *memcg)
{
	return 0;
}

static inline void
mem_cgroup_print_oom_context(struct mem_cgroup *memcg, struct task_struct *p)
{
}

static inline void
mem_cgroup_print_oom_meminfo(struct mem_cgroup *memcg)
{
}

static inline struct mem_cgroup *lock_page_memcg(struct page *page)
{
	return NULL;
}

static inline void __unlock_page_memcg(struct mem_cgroup *memcg)
{
}

static inline void unlock_page_memcg(struct page *page)
{
}

static inline void mem_cgroup_handle_over_high(void)
{
}

static inline void mem_cgroup_enter_user_fault(void)
{
}

static inline void mem_cgroup_exit_user_fault(void)
{
}

static inline bool task_in_memcg_oom(struct task_struct *p)
{
	return false;
}

static inline bool mem_cgroup_oom_synchronize(bool wait)
{
	return false;
}

static inline struct mem_cgroup *mem_cgroup_get_oom_group(
	struct task_struct *victim, struct mem_cgroup *oom_domain)
{
	return NULL;
}

static inline void mem_cgroup_print_oom_group(struct mem_cgroup *memcg)
{
}

static inline unsigned long memcg_page_state(struct mem_cgroup *memcg, int idx)
{
	return 0;
}

static inline unsigned long memcg_page_state_local(struct mem_cgroup *memcg,
						   int idx)
{
	return 0;
}

static inline void __mod_memcg_state(struct mem_cgroup *memcg,
				     int idx,
				     int nr)
{
}

static inline void mod_memcg_state(struct mem_cgroup *memcg,
				   int idx,
				   int nr)
{
}

static inline void __mod_memcg_page_state(struct page *page,
					  int idx,
					  int nr)
{
}

static inline void mod_memcg_page_state(struct page *page,
					int idx,
					int nr)
{
}

static inline unsigned long lruvec_page_state(struct lruvec *lruvec,
					      enum node_stat_item idx)
{
	return node_page_state(lruvec_pgdat(lruvec), idx);
}

static inline unsigned long lruvec_page_state_local(struct lruvec *lruvec,
						    enum node_stat_item idx)
{
	return node_page_state(lruvec_pgdat(lruvec), idx);
}

static inline void __mod_lruvec_state(struct lruvec *lruvec,
				      enum node_stat_item idx, int val)
{
	__mod_node_page_state(lruvec_pgdat(lruvec), idx, val);
}

static inline void mod_lruvec_state(struct lruvec *lruvec,
				    enum node_stat_item idx, int val)
{
	mod_node_page_state(lruvec_pgdat(lruvec), idx, val);
}

static inline void __mod_lruvec_page_state(struct page *page,
					   enum node_stat_item idx, int val)
{
	__mod_node_page_state(page_pgdat(page), idx, val);
}

static inline void mod_lruvec_page_state(struct page *page,
					 enum node_stat_item idx, int val)
{
	mod_node_page_state(page_pgdat(page), idx, val);
}

static inline void __mod_lruvec_slab_state(void *p, enum node_stat_item idx,
					   int val)
{
	struct page *page = virt_to_head_page(p);

	__mod_node_page_state(page_pgdat(page), idx, val);
}

static inline void mod_memcg_obj_state(void *p, int idx, int val)
{
}

static inline
unsigned long mem_cgroup_soft_limit_reclaim(pg_data_t *pgdat, int order,
					    gfp_t gfp_mask,
					    unsigned long *total_scanned)
{
	return 0;
}

static inline void mem_cgroup_split_huge_fixup(struct page *head)
{
}

static inline void count_memcg_events(struct mem_cgroup *memcg,
				      enum vm_event_item idx,
				      unsigned long count)
{
}

static inline void __count_memcg_events(struct mem_cgroup *memcg,
					enum vm_event_item idx,
					unsigned long count)
{
}

static inline void count_memcg_page_event(struct page *page,
					  int idx)
{
}

static inline
void count_memcg_event_mm(struct mm_struct *mm, enum vm_event_item idx)
{
}
#endif /* CONFIG_MEMCG */

/* idx can be of type enum memcg_stat_item or node_stat_item */
static inline void __inc_memcg_state(struct mem_cgroup *memcg,
				     int idx)
{
	__mod_memcg_state(memcg, idx, 1);
}

/* idx can be of type enum memcg_stat_item or node_stat_item */
static inline void __dec_memcg_state(struct mem_cgroup *memcg,
				     int idx)
{
	__mod_memcg_state(memcg, idx, -1);
}

/* idx can be of type enum memcg_stat_item or node_stat_item */
static inline void __inc_memcg_page_state(struct page *page,
					  int idx)
{
	__mod_memcg_page_state(page, idx, 1);
}

/* idx can be of type enum memcg_stat_item or node_stat_item */
static inline void __dec_memcg_page_state(struct page *page,
					  int idx)
{
	__mod_memcg_page_state(page, idx, -1);
}

static inline void __inc_lruvec_state(struct lruvec *lruvec,
				      enum node_stat_item idx)
{
	__mod_lruvec_state(lruvec, idx, 1);
}

static inline void __dec_lruvec_state(struct lruvec *lruvec,
				      enum node_stat_item idx)
{
	__mod_lruvec_state(lruvec, idx, -1);
}

static inline void __inc_lruvec_page_state(struct page *page,
					   enum node_stat_item idx)
{
	__mod_lruvec_page_state(page, idx, 1);
}

static inline void __dec_lruvec_page_state(struct page *page,
					   enum node_stat_item idx)
{
	__mod_lruvec_page_state(page, idx, -1);
}

static inline void __inc_lruvec_slab_state(void *p, enum node_stat_item idx)
{
	__mod_lruvec_slab_state(p, idx, 1);
}

static inline void __dec_lruvec_slab_state(void *p, enum node_stat_item idx)
{
	__mod_lruvec_slab_state(p, idx, -1);
}

/* idx can be of type enum memcg_stat_item or node_stat_item */
static inline void inc_memcg_state(struct mem_cgroup *memcg,
				   int idx)
{
	mod_memcg_state(memcg, idx, 1);
}

/* idx can be of type enum memcg_stat_item or node_stat_item */
static inline void dec_memcg_state(struct mem_cgroup *memcg,
				   int idx)
{
	mod_memcg_state(memcg, idx, -1);
}

/* idx can be of type enum memcg_stat_item or node_stat_item */
static inline void inc_memcg_page_state(struct page *page,
					int idx)
{
	mod_memcg_page_state(page, idx, 1);
}

/* idx can be of type enum memcg_stat_item or node_stat_item */
static inline void dec_memcg_page_state(struct page *page,
					int idx)
{
	mod_memcg_page_state(page, idx, -1);
}

static inline void inc_lruvec_state(struct lruvec *lruvec,
				    enum node_stat_item idx)
{
	mod_lruvec_state(lruvec, idx, 1);
}

static inline void dec_lruvec_state(struct lruvec *lruvec,
				    enum node_stat_item idx)
{
	mod_lruvec_state(lruvec, idx, -1);
}

static inline void inc_lruvec_page_state(struct page *page,
					 enum node_stat_item idx)
{
	mod_lruvec_page_state(page, idx, 1);
}

static inline void dec_lruvec_page_state(struct page *page,
					 enum node_stat_item idx)
{
	mod_lruvec_page_state(page, idx, -1);
}

#ifdef CONFIG_CGROUP_WRITEBACK

struct wb_domain *mem_cgroup_wb_domain(struct bdi_writeback *wb);
void mem_cgroup_wb_stats(struct bdi_writeback *wb, unsigned long *pfilepages,
			 unsigned long *pheadroom, unsigned long *pdirty,
			 unsigned long *pwriteback);

void mem_cgroup_track_foreign_dirty_slowpath(struct page *page,
					     struct bdi_writeback *wb);

static inline void mem_cgroup_track_foreign_dirty(struct page *page,
						  struct bdi_writeback *wb)
{
	if (mem_cgroup_disabled())
		return;

	if (unlikely(&page->mem_cgroup->css != wb->memcg_css))
		mem_cgroup_track_foreign_dirty_slowpath(page, wb);
}

void mem_cgroup_flush_foreign(struct bdi_writeback *wb);

#else	/* CONFIG_CGROUP_WRITEBACK */

static inline struct wb_domain *mem_cgroup_wb_domain(struct bdi_writeback *wb)
{
	return NULL;
}

static inline void mem_cgroup_wb_stats(struct bdi_writeback *wb,
				       unsigned long *pfilepages,
				       unsigned long *pheadroom,
				       unsigned long *pdirty,
				       unsigned long *pwriteback)
{
}

static inline void mem_cgroup_track_foreign_dirty(struct page *page,
						  struct bdi_writeback *wb)
{
}

static inline void mem_cgroup_flush_foreign(struct bdi_writeback *wb)
{
}

#endif	/* CONFIG_CGROUP_WRITEBACK */

struct sock;
bool mem_cgroup_charge_skmem(struct mem_cgroup *memcg, unsigned int nr_pages);
void mem_cgroup_uncharge_skmem(struct mem_cgroup *memcg, unsigned int nr_pages);
#ifdef CONFIG_MEMCG

extern unsigned int vm_pagecache_limit_retry_times __read_mostly;
extern void mem_cgroup_shrink_pagecache(struct mem_cgroup *memcg, gfp_t gfp_mask);

extern struct static_key_false memcg_sockets_enabled_key;
#define mem_cgroup_sockets_enabled static_branch_unlikely(&memcg_sockets_enabled_key)
void mem_cgroup_sk_alloc(struct sock *sk);
void mem_cgroup_sk_free(struct sock *sk);
static inline bool mem_cgroup_under_socket_pressure(struct mem_cgroup *memcg)
{
	if (!cgroup_subsys_on_dfl(memory_cgrp_subsys) && memcg->tcpmem_pressure)
		return true;
	do {
		if (time_before(jiffies, memcg->socket_pressure))
			return true;
	} while ((memcg = parent_mem_cgroup(memcg)));
	return false;
}

extern int memcg_expand_shrinker_maps(int new_id);

extern void memcg_set_shrinker_bit(struct mem_cgroup *memcg,
				   int nid, int shrinker_id);
#else
#define mem_cgroup_sockets_enabled 0
static inline void mem_cgroup_sk_alloc(struct sock *sk) { };
static inline void mem_cgroup_sk_free(struct sock *sk) { };
static inline bool mem_cgroup_under_socket_pressure(struct mem_cgroup *memcg)
{
	return false;
}

static inline void memcg_set_shrinker_bit(struct mem_cgroup *memcg,
					  int nid, int shrinker_id)
{
}
#endif

struct kmem_cache *memcg_kmem_get_cache(struct kmem_cache *cachep);
void memcg_kmem_put_cache(struct kmem_cache *cachep);

#ifdef CONFIG_MEMCG_KMEM
int __memcg_kmem_charge(struct page *page, gfp_t gfp, int order);
void __memcg_kmem_uncharge(struct page *page, int order);
int __memcg_kmem_charge_memcg(struct page *page, gfp_t gfp, int order,
			      struct mem_cgroup *memcg);
void __memcg_kmem_uncharge_memcg(struct mem_cgroup *memcg,
				 unsigned int nr_pages);

extern struct static_key_false memcg_kmem_enabled_key;
extern struct workqueue_struct *memcg_kmem_cache_wq;

extern int memcg_nr_cache_ids;
void memcg_get_cache_ids(void);
void memcg_put_cache_ids(void);

/*
 * Helper macro to loop through all memcg-specific caches. Callers must still
 * check if the cache is valid (it is either valid or NULL).
 * the slab_mutex must be held when looping through those caches
 */
#define for_each_memcg_cache_index(_idx)	\
	for ((_idx) = 0; (_idx) < memcg_nr_cache_ids; (_idx)++)

static inline bool memcg_kmem_enabled(void)
{
	return static_branch_unlikely(&memcg_kmem_enabled_key);
}

static inline int memcg_kmem_charge(struct page *page, gfp_t gfp, int order)
{
	if (memcg_kmem_enabled())
		return __memcg_kmem_charge(page, gfp, order);
	return 0;
}

static inline void memcg_kmem_uncharge(struct page *page, int order)
{
	if (memcg_kmem_enabled())
		__memcg_kmem_uncharge(page, order);
}

static inline int memcg_kmem_charge_memcg(struct page *page, gfp_t gfp,
					  int order, struct mem_cgroup *memcg)
{
	if (memcg_kmem_enabled())
		return __memcg_kmem_charge_memcg(page, gfp, order, memcg);
	return 0;
}

static inline void memcg_kmem_uncharge_memcg(struct page *page, int order,
					     struct mem_cgroup *memcg)
{
	if (memcg_kmem_enabled())
		__memcg_kmem_uncharge_memcg(memcg, 1 << order);
}

/*
 * helper for accessing a memcg's index. It will be used as an index in the
 * child cache array in kmem_cache, and also to derive its name. This function
 * will return -1 when this is not a kmem-limited memcg.
 */
static inline int memcg_cache_id(struct mem_cgroup *memcg)
{
	return memcg ? memcg->kmemcg_id : -1;
}

struct mem_cgroup *mem_cgroup_from_obj(void *p);

#else

static inline int memcg_kmem_charge(struct page *page, gfp_t gfp, int order)
{
	return 0;
}

static inline void memcg_kmem_uncharge(struct page *page, int order)
{
}

static inline int __memcg_kmem_charge(struct page *page, gfp_t gfp, int order)
{
	return 0;
}

static inline void __memcg_kmem_uncharge(struct page *page, int order)
{
}

#define for_each_memcg_cache_index(_idx)	\
	for (; NULL; )

static inline bool memcg_kmem_enabled(void)
{
	return false;
}

static inline int memcg_cache_id(struct mem_cgroup *memcg)
{
	return -1;
}

static inline void memcg_get_cache_ids(void)
{
}

static inline void memcg_put_cache_ids(void)
{
}

static inline struct mem_cgroup *mem_cgroup_from_obj(void *p)
{
       return NULL;
}

#endif /* CONFIG_MEMCG_KMEM */

#endif /* _LINUX_MEMCONTROL_H */
