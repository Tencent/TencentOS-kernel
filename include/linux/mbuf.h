/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 bauerchen <bauerchen@tencent.com>
 */

#include <linux/cgroup.h>
#include <linux/rwsem.h>

#ifndef _CGROUP_MBUF_H
#define _CGROUP_MBUF_H

struct mbuf_struct {
	u32 mbuf_len;
	u32 mbuf_max_slots;
	u32 mbuf_frees;
	u32 mbuf_next_id;
	u32 mbuf_size_per_cg;
	spinlock_t mbuf_lock;
	char *mbuf;
	unsigned long *mbuf_bitmap;
};

struct mbuf_ring_desc {
	/* timestamp of this message */
	u64 ts_ns;
	/* message total len, ring_item + ->len = next_item */
	u16 len;
	/* text len text_len + sizeof(ring) = len */
	u16 text_len;
};

struct mbuf_ring {
	u32 base_idx;
	u32 first_idx;
	u64 first_seq;
	u32 next_idx;
	u64 next_seq;
	u32 end_idx;
};

struct mbuf_user_desc {
	u64 user_seq;
	u32 user_idx;
	char buf[1024];
};

/* each cgroup has a mbuf_slot struct */
struct mbuf_slot {
	u32 idx;
	/* write op must hold this lock */
	spinlock_t slot_lock;
	/* rate limit */
	struct ratelimit_state ratelimit;
	struct cgroup *owner;
	const struct mbuf_operations *ops;
	struct mbuf_ring *mring;
	struct mbuf_user_desc *udesc;
};

struct mbuf_operations {
	/* read message */
	ssize_t (*read) (struct mbuf_slot *, struct mbuf_user_desc *);
	/* get next available idx */
	u32 (*next)	(struct mbuf_ring *, u32);
	/* write message */
	ssize_t (*write) (struct cgroup *, const char *, va_list);
} ____cacheline_aligned;


void __init mbuf_bmap_init(void);
void __init setup_mbuf(void);
struct mbuf_slot *mbuf_slot_alloc(struct cgroup *cg);
void mbuf_free(struct cgroup *cg);
#endif
