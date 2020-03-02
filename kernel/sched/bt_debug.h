/*
 *   Copyright (C) 2019 Tencent Ltd. All rights reserved.
 *
 *   File Name : bt_debug.h
 *   Author    :
 *   Date      : 2019-12-26
 *   Descriptor:
 */

#ifndef _BT_STAT_H
#define _BT_STAT_H

/*
 * This allows printing both to /proc/sched_debug and
 * to the console
 */
#define SEQ_printf(m, x...)			\
do {						\
	if (m)					\
		seq_printf(m, x);		\
	else					\
		printk(x);			\
} while (0)

extern long long nsec_high(unsigned long long nsec);
extern unsigned long nsec_low(unsigned long long nsec);
#define SPLIT_NS(x) (nsec_high(x), nsec_low(x))

extern void print_bt_stats(struct seq_file *m, int cpu);
#ifdef CONFIG_SCHED_DEBUG
extern void print_bt_stats(struct seq_file *m, int cpu);
#else
void print_bt_stats(struct seq_file *m, int cpu) {}
#endif

#endif
