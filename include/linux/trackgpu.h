/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TRACKGPU_H
#define _LINUX_TRACKGPU_H

#define GPU_DEV "/dev/nvidiactl"
#define GPU_TIMER_EXPIRE 2
#define GPU_RETRY_TIMES 5
#define GPU_OK	"ok"
struct gpu_request_list {
	struct list_head list;
	struct mutex lock;
};

enum GPU_EVENT_TYPE {
	GPU_OPEN,
	GPU_CLOSE,
};
extern int sysctl_track_gpu;
struct gpu_wait {
	struct list_head node;
        wait_queue_head_t wait;
        bool proceed;
        bool wakup;
        int pid;
        int type;
	bool used;
};

extern struct gpu_wait *gpu_create_wait(int pid);
extern int notify_gpu(struct gpu_wait *wait, enum GPU_EVENT_TYPE e, int pid);
extern void gpu_clean(int pid);


#endif
