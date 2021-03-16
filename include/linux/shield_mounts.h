#ifndef __SHIELD_MOUNTS_H__
#define __SHIELD_MOUNTS_H__

bool is_mount_shielded(const char *dev_path, const char *mount_path);
extern unsigned int sysctl_shield_mounts_max;

#endif
