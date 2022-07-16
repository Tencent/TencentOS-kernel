//  SPDX-License-Identifier: GPL-2.0-only
/*
 *  Quality Monitor Buffer
 *  Aim to provide backup buffer for RQM to record critical message.
 *  Could be used to catch critical context when abnormal jitters occur.
 *
 *	Author: bauerchen <bauerchen@tencent.com>
 *	Copyright (C) 2021 Tencent, Inc
 */

#include <linux/kernel.h>
#include <linux/cgroup.h>
#include <linux/memblock.h>
#include <linux/cpu.h>
#include <linux/uaccess.h>
#include <linux/mbuf.h>
#include <linux/slab.h>
#include <linux/sched/clock.h>
#include <linux/ratelimit.h>

/* Define max mbuf len is 8M, and min is 2M */
#define MBUF_LEN_MAX (1 << 23)
#define MBUF_LEN_MIN (1 << 21)
#define MBUF_LEN_DEF MBUF_LEN_MIN

#define MBUF_MSG_LEN_MAX 1024

/* Monitor buffer support max 1024 items */
#define MBUF_SLOTS_MAX	1024
#define MBUF_SLOTS_MIN	256
#define MBUF_SLOTS_DEF  512

/* Global mbuf metadata struct */
static struct mbuf_struct g_mbuf = {
	.mbuf_len = MBUF_LEN_DEF,
	.mbuf_max_slots = MBUF_SLOTS_DEF,
	.mbuf_next_id = 0,
	.mbuf_size_per_cg = 0,
	.mbuf = NULL,
	.mbuf_bitmap = NULL,
};

static void __init mbuf_len_update(u64 size)
{
	if (size)
		size = roundup_pow_of_two(size);

	if (size > MBUF_LEN_MAX) {
		size = (u64)MBUF_LEN_MAX;
		pr_warn("mbuf: monitor buffer over [ %llu ] is not supported.\n",
				(u64)MBUF_LEN_MAX);
	}

	if (size < MBUF_LEN_MIN){
		size = (u64) MBUF_LEN_MIN;
		pr_warn("mbuf: monitor buffer less [ %llu ] is not supported.\n",
				(u64) MBUF_LEN_MIN);
	}

	g_mbuf.mbuf_len = size;
}

static int  __init mbuf_len_setup(char *str)
{

	u64 size;

	if (!str)
		return -EINVAL;

	size = memparse(str, &str);

	mbuf_len_update(size);

	return 0;

}
early_param("mbuf_len", mbuf_len_setup);

static int __init mbuf_max_items_setup(char *str)
{
	int num;

	if (!str)
		return -EINVAL;

	if (!get_option(&str, &num))
		return -EINVAL;

	if (num)
		num = roundup_pow_of_two(num);

	if (num > MBUF_SLOTS_MAX)
		num = MBUF_SLOTS_MAX;

	if (num < MBUF_SLOTS_MIN)
		num = MBUF_SLOTS_MIN;

	g_mbuf.mbuf_max_slots = num;

	return 0;
}
early_param("mbuf_max_items", mbuf_max_items_setup);

/* Alloc mbuf global bitmap, each bit stands for a mbuf slot. */
void __init mbuf_bmap_init(void)
{
	size_t alloc_size;
	void *mbuf_bitmap;

	alloc_size = max_t(size_t, g_mbuf.mbuf_max_slots / BITS_PER_BYTE + 1,
					   L1_CACHE_BYTES);
	mbuf_bitmap = kmalloc(alloc_size, __GFP_HIGH|__GFP_ZERO);

	if(!mbuf_bitmap){
		pr_err("mbuf: alloc mbuf_bitmap failed!\n");
		return;
	}
	g_mbuf.mbuf_bitmap = mbuf_bitmap;
	g_mbuf.mbuf_size_per_cg = g_mbuf.mbuf_len / g_mbuf.mbuf_max_slots;
}

/* Called by start_kernel() */
void __init setup_mbuf(void)
{
	/* mbuf has been alloced */
	if (g_mbuf.mbuf)
		return;

	g_mbuf.mbuf = memblock_alloc(g_mbuf.mbuf_len, PAGE_SIZE);
	if (unlikely(!g_mbuf.mbuf)) {
		pr_err("mbuf: memblock_alloc [ %u ] bytes failed\n",
				g_mbuf.mbuf_len);
		return;
	}

	g_mbuf.mbuf_frees = g_mbuf.mbuf_max_slots;
	spin_lock_init(&g_mbuf.mbuf_lock);

	pr_info("mbuf: mbuf_len:%u\n", g_mbuf.mbuf_len);
}

/* Get mbuf ring desc text pointer */
static char *mbuf_text(struct mbuf_ring_desc *desc)
{
	return (char *)desc + sizeof(struct mbuf_ring_desc);
}

/* Get next mbuf_slot idx */
static u32 mbuf_next(struct mbuf_ring *mring, u32 curr_idx)
{
	struct mbuf_ring_desc *cdesc, *ndesc;
	u32 frees, next_idx;

	cdesc = (struct mbuf_ring_desc *)(g_mbuf.mbuf + curr_idx);
	next_idx = curr_idx + cdesc->len;
	/*
	 * If frees are not enough to store mbuf_ring_desc struct,
	 * just goto head
	 */
	frees = mring->end_idx - next_idx;
	if(frees < sizeof(struct mbuf_ring_desc)){
		next_idx = mring->base_idx;
		goto next;
	}

	ndesc = (struct mbuf_ring_desc *)(g_mbuf.mbuf + next_idx);
	if (!ndesc->len && next_idx != mring->next_idx)
		next_idx = mring->base_idx;

next:
	return next_idx;
}

static inline struct mbuf_ring_desc *get_ring_desc_from_idx(
					struct mbuf_ring *ring, u32 idx)
{
	return (struct mbuf_ring_desc *)(g_mbuf.mbuf + idx);
}

/* Read mbuf message according to its idx */
static ssize_t mbuf_read(struct mbuf_slot *mb, struct mbuf_user_desc *udesc)
{
	struct mbuf_ring *mring;
	struct mbuf_ring_desc *desc;
	ssize_t ret;
	size_t i, len, tbuf_len;

	tbuf_len = sizeof(udesc->buf);
	mring = mb->mring;

	if (udesc->user_seq < mring->first_seq) {
		udesc->user_seq = mring->first_seq;
		udesc->user_idx = mring->first_idx;
		ret = -1;
		goto out;
	}

	desc = get_ring_desc_from_idx(mring, udesc->user_idx);

	len = sprintf(udesc->buf, "%llu:", desc->ts_ns);
	for (i = 0; i < desc->text_len; i++) {
		unsigned char c = mbuf_text(desc)[i];

		if (c < ' ' || c >= 127 || c == '\\')
			continue;
		else
			udesc->buf[len++] = c;

		if (len >= tbuf_len)
			break;
	}

	len = len >= tbuf_len ? tbuf_len - 1 : len;
	udesc->buf[len] = '\n';
	udesc->user_seq++;
	ret = len;

out:
	return ret;
}

static int mbuf_prepare(struct mbuf_ring *mring, u32 msg_size)
{
	u32 frees;

	if (unlikely(msg_size > MBUF_MSG_LEN_MAX)) {
		return -ENOMEM;
	}

	while (mring->first_seq < mring->next_seq) {

		if (mring->first_idx < mring->next_idx)
			frees = max(mring->end_idx - mring->next_idx,
					   mring->first_idx - mring->base_idx);
		else
			frees = mring->first_idx - mring->next_idx;

		if (frees > msg_size)
			break;

		/* Drop old message until se have enough contiguous space */
		mring->first_idx = mbuf_next(mring, mring->first_idx);
		mring->first_seq++;
	}
	return 0;
}

/* Write monitor buffer message */
static ssize_t do_mbuf_write(struct cgroup *cg, char *buffer, size_t size)
{
	struct mbuf_ring *mring;
	struct mbuf_ring_desc *desc;
	size_t len;
	unsigned long flags;

	if (size >= g_mbuf.mbuf_size_per_cg){
		pr_err("mbuf: write message need less than [ %u ] bytes\n",
				g_mbuf.mbuf_size_per_cg);
		return 0;
	}

	mring = cg->mbuf->mring;
	len = sizeof(struct mbuf_ring_desc) + size;

	spin_lock_irqsave(&cg->mbuf->slot_lock, flags);

	if (mbuf_prepare(mring, len)){
		spin_unlock_irqrestore(&cg->mbuf->slot_lock, flags);
		pr_err("mbuf: Can not find enough space.\n");
		return 0;
	}

	if (mring->next_idx + len >= mring->end_idx) {
		/* Set remain buffer to 0 if we go to head */
		memset(g_mbuf.mbuf + mring->next_idx, 0, mring->end_idx - mring->next_idx);
		mring->next_idx = mring->base_idx;
	}

	desc = (struct mbuf_ring_desc *)(g_mbuf.mbuf + mring->next_idx);
	memcpy(mbuf_text(desc), buffer, size);
	desc->len = size + sizeof(struct mbuf_ring_desc);
	desc->text_len = size;
	desc->ts_ns = local_clock();
	mring->next_idx += desc->len;
	mring->next_seq++;

	spin_unlock_irqrestore(&cg->mbuf->slot_lock, flags);

	return size;
}

void mbuf_reset(struct mbuf_ring *mring)
{
	mring->first_idx = mring->base_idx;
	mring->first_seq = 0;
	mring->next_idx = mring->base_idx;
	mring->next_seq = 0;
}

static ssize_t mbuf_write(struct cgroup *cg, const char *fmt, va_list args)
{
	static char buf[MBUF_MSG_LEN_MAX];
	char *text = buf;
	size_t t_len;
	ssize_t ret;
	/* Store string to buffer */
	t_len = vscnprintf(text, sizeof(buf), fmt, args);

	/* Write string to mbuf */
	ret = do_mbuf_write(cg, text, t_len);

	return ret;
}

const struct mbuf_operations mbuf_ops = {
	.read		= mbuf_read,
	.next		= mbuf_next,
	.write		= mbuf_write,
};

static int get_next_mbuf_id(unsigned long *addr, u32 start)
{
	u32 index;

	index = find_next_zero_bit(addr, g_mbuf.mbuf_max_slots, start);
	if (unlikely(index >= g_mbuf.mbuf_max_slots))
		return g_mbuf.mbuf_max_slots;

	return index;
}

static void mbuf_slot_init(struct mbuf_slot *mb, struct cgroup *cg, u32 index)
{
	mb->owner = cg;
	mb->idx = index;
	mb->ops = &mbuf_ops;
	spin_lock_init(&mb->slot_lock);
	ratelimit_state_init(&mb->ratelimit, 5 * HZ,50);

	mb->mring = (struct mbuf_ring *)((char *)mb + sizeof(struct mbuf_slot));
	mb->mring->base_idx = index *
				g_mbuf.mbuf_size_per_cg + sizeof(struct mbuf_slot) + sizeof(struct mbuf_ring);
	mb->mring->end_idx = (index + 1) * g_mbuf.mbuf_size_per_cg - 1;

	mbuf_reset(mb->mring);
}

struct mbuf_slot *mbuf_slot_alloc(struct cgroup *cg)
{
	struct mbuf_slot *mb;
	u32 index = 0;
	u32 try_times;
	unsigned long *m_bitmap;
	unsigned long flags;

	/* If mbuf_bitmap or mbuf not ready, just return NULL. */
	if (!g_mbuf.mbuf_bitmap || !g_mbuf.mbuf) {
		pr_warn_ratelimited("mbuf: mbuf bitmap or mbuf pointer is NULL, alloc failed\n");
		return NULL;
	}

	spin_lock_irqsave(&g_mbuf.mbuf_lock, flags);

	if (g_mbuf.mbuf_frees == 0) {
		pr_warn_ratelimited("mbuf: reached max num, alloc failed\n");
		spin_unlock_irqrestore(&g_mbuf.mbuf_lock, flags);
		return NULL;
	}

	/* Alloc a free mbuf_slot from global mbuf, according mbuf_bitmap */
	m_bitmap = g_mbuf.mbuf_bitmap;

	try_times = 1;
again:
	index = get_next_mbuf_id(m_bitmap, g_mbuf.mbuf_next_id);

	/* Rescan next avail idx from head if current idx reach end */
	if (index == g_mbuf.mbuf_max_slots && try_times--) {
		g_mbuf.mbuf_next_id = 0;
		goto again;
	}

	if (unlikely(index == g_mbuf.mbuf_max_slots)) {
		/*
		 * Just a protection mechanism, its must be a bug
		 * if function reached here.
		 */
		pr_warn_ratelimited("mbuf: frees and bitmap not coincident, just return\n");
		spin_unlock_irqrestore(&g_mbuf.mbuf_lock, flags);
		return NULL;
	}

	__set_bit(index, m_bitmap);
	g_mbuf.mbuf_next_id = index;

	mb = (struct mbuf_slot *)(g_mbuf.mbuf + index * g_mbuf.mbuf_size_per_cg);
	mbuf_slot_init(mb, cg, index);
	g_mbuf.mbuf_frees--;

	spin_unlock_irqrestore(&g_mbuf.mbuf_lock, flags);

	return mb;
}

void mbuf_free(struct cgroup *cg)
{
	unsigned long flags;

	spin_lock_irqsave(&g_mbuf.mbuf_lock, flags);

	/* Make current idx the next available buffer */
	g_mbuf.mbuf_next_id = cg->mbuf->idx;
	__clear_bit(g_mbuf.mbuf_next_id, g_mbuf.mbuf_bitmap);

	g_mbuf.mbuf_frees++;
	spin_unlock_irqrestore(&g_mbuf.mbuf_lock, flags);
}

