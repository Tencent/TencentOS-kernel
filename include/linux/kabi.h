/* SPDX-License-Identifier: GPL-2.0 */
/*
 * kabi.h
 *
 */

#ifndef _LINUX_KABI_H
#define _LINUX_KABI_H

# define _KABI_REPLACE(_orig, _new)			\
	union {						\
		_new;					\
		struct {				\
			_orig;				\
		} __UNIQUE_ID(kabi_hide);		\
	}

#define KABI_REPLACE(_orig, _new)	_KABI_REPLACE(_orig, _new)

#define _KABI_RESERVE(n)	unsigned long kabi_reserved##n
#define _KABI_RESERVE_P(n)	void (*kabi_reserved##n)(void)
#define KABI_RESERVE(n)		_KABI_RESERVE(n)
#define KABI_RESERVE_P(n)	_KABI_RESERVE_P(n)

#define KABI_USE(n, _new)	KABI_REPLACE(_KABI_RESERVE(n), _new)
#define KABI_USE_P(n, _new)	KABI_REPLACE(_KABI_RESERVE_P(n), _new)

#endif /* _LINUX_KABI_H */
