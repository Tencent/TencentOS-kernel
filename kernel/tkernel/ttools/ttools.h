#ifndef __TTOOLS_H__
#define __TTOOLS_H__

#include <linux/types.h>
#include <linux/ioctl.h>

#define TTOOLS_IO 0xEE

struct ttools_fd_ref {
	int fd;
	long ref_cnt;
};

#define TTOOLS_PTRACE_PROTECT		_IO(TTOOLS_IO, 0x00)
#define TTOOLS_PTRACE_UNPROTECT		_IO(TTOOLS_IO, 0x01)
#define TTOOLS_GET_FD_REFS_CNT		_IOWR(TTOOLS_IO, 0x02, struct ttools_fd_ref)

#endif
