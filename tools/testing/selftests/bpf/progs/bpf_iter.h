/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2020 Facebook */
/* "undefine" structs in vmlinux.h, because we "override" them below */
#define bpf_iter_meta bpf_iter_meta___not_used
#define bpf_iter__bpf_map bpf_iter__bpf_map___not_used
#define bpf_iter__ipv6_route bpf_iter__ipv6_route___not_used
#define bpf_iter__netlink bpf_iter__netlink___not_used
#define bpf_iter__task bpf_iter__task___not_used
#define bpf_iter__task_file bpf_iter__task_file___not_used
#include "vmlinux.h"
#undef bpf_iter_meta
#undef bpf_iter__bpf_map
#undef bpf_iter__ipv6_route
#undef bpf_iter__netlink
#undef bpf_iter__task
#undef bpf_iter__task_file

struct bpf_iter_meta {
	struct seq_file *seq;
	__u64 session_id;
	__u64 seq_num;
} __attribute__((preserve_access_index));

struct bpf_iter__ipv6_route {
	struct bpf_iter_meta *meta;
	struct fib6_info *rt;
} __attribute__((preserve_access_index));

struct bpf_iter__netlink {
	struct bpf_iter_meta *meta;
	struct netlink_sock *sk;
} __attribute__((preserve_access_index));

struct bpf_iter__task {
	struct bpf_iter_meta *meta;
	struct task_struct *task;
} __attribute__((preserve_access_index));

struct bpf_iter__task_file {
	struct bpf_iter_meta *meta;
	struct task_struct *task;
	__u32 fd;
	struct file *file;
} __attribute__((preserve_access_index));

struct bpf_iter__bpf_map {
	struct bpf_iter_meta *meta;
	struct bpf_map *map;
} __attribute__((preserve_access_index));
