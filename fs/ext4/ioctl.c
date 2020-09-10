// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ext4/ioctl.c
 *
 * Copyright (C) 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 */

#include <linux/fs.h>
#include <linux/capability.h>
#include <linux/time.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/quotaops.h>
#include <linux/uuid.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include "ext4_jbd2.h"
#include "ext4.h"
#include <linux/fsmap.h>
#include "fsmap.h"
#include <trace/events/ext4.h>

/**
 * Swap memory between @a and @b for @len bytes.
 *
 * @a:          pointer to first memory area
 * @b:          pointer to second memory area
 * @len:        number of bytes to swap
 *
 */
static void memswap(void *a, void *b, size_t len)
{
	unsigned char *ap, *bp;

	ap = (unsigned char *)a;
	bp = (unsigned char *)b;
	while (len-- > 0) {
		swap(*ap, *bp);
		ap++;
		bp++;
	}
}

/**
 * Swap i_data and associated attributes between @inode1 and @inode2.
 * This function is used for the primary swap between inode1 and inode2
 * and also to revert this primary swap in case of errors.
 *
 * Therefore you have to make sure, that calling this method twice
 * will revert all changes.
 *
 * @inode1:     pointer to first inode
 * @inode2:     pointer to second inode
 */
static void swap_inode_data(struct inode *inode1, struct inode *inode2)
{
	loff_t isize;
	struct ext4_inode_info *ei1;
	struct ext4_inode_info *ei2;

	ei1 = EXT4_I(inode1);
	ei2 = EXT4_I(inode2);

	swap(inode1->i_flags, inode2->i_flags);
	swap(inode1->i_version, inode2->i_version);
	swap(inode1->i_blocks, inode2->i_blocks);
	swap(inode1->i_bytes, inode2->i_bytes);
	swap(inode1->i_atime, inode2->i_atime);
	swap(inode1->i_mtime, inode2->i_mtime);

	memswap(ei1->i_data, ei2->i_data, sizeof(ei1->i_data));
	swap(ei1->i_flags, ei2->i_flags);
	swap(ei1->i_disksize, ei2->i_disksize);
	ext4_es_remove_extent(inode1, 0, EXT_MAX_BLOCKS);
	ext4_es_remove_extent(inode2, 0, EXT_MAX_BLOCKS);

	isize = i_size_read(inode1);
	i_size_write(inode1, i_size_read(inode2));
	i_size_write(inode2, isize);
}

/**
 * Swap the information from the given @inode and the inode
 * EXT4_BOOT_LOADER_INO. It will basically swap i_data and all other
 * important fields of the inodes.
 *
 * @sb:         the super block of the filesystem
 * @inode:      the inode to swap with EXT4_BOOT_LOADER_INO
 *
 */
static long swap_inode_boot_loader(struct super_block *sb,
				struct inode *inode)
{
	handle_t *handle;
	int err;
	struct inode *inode_bl;
	struct ext4_inode_info *ei_bl;
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	if (inode->i_nlink != 1 || !S_ISREG(inode->i_mode))
		return -EINVAL;

	if (!inode_owner_or_capable(inode) || !capable(CAP_SYS_ADMIN))
		return -EPERM;

	inode_bl = ext4_iget(sb, EXT4_BOOT_LOADER_INO);
	if (IS_ERR(inode_bl))
		return PTR_ERR(inode_bl);
	ei_bl = EXT4_I(inode_bl);

	filemap_flush(inode->i_mapping);
	filemap_flush(inode_bl->i_mapping);

	/* Protect orig inodes against a truncate and make sure,
	 * that only 1 swap_inode_boot_loader is running. */
	lock_two_nondirectories(inode, inode_bl);

	truncate_inode_pages(&inode->i_data, 0);
	truncate_inode_pages(&inode_bl->i_data, 0);

	/* Wait for all existing dio workers */
	ext4_inode_block_unlocked_dio(inode);
	ext4_inode_block_unlocked_dio(inode_bl);
	inode_dio_wait(inode);
	inode_dio_wait(inode_bl);

	handle = ext4_journal_start(inode_bl, EXT4_HT_MOVE_EXTENTS, 2);
	if (IS_ERR(handle)) {
		err = -EINVAL;
		goto journal_err_out;
	}

	/* Protect extent tree against block allocations via delalloc */
	ext4_double_down_write_data_sem(inode, inode_bl);

	if (inode_bl->i_nlink == 0) {
		/* this inode has never been used as a BOOT_LOADER */
		set_nlink(inode_bl, 1);
		i_uid_write(inode_bl, 0);
		i_gid_write(inode_bl, 0);
		inode_bl->i_flags = 0;
		ei_bl->i_flags = 0;
		inode_bl->i_version = 1;
		i_size_write(inode_bl, 0);
		inode_bl->i_mode = S_IFREG;
		if (ext4_has_feature_extents(sb)) {
			ext4_set_inode_flag(inode_bl, EXT4_INODE_EXTENTS);
			ext4_ext_tree_init(handle, inode_bl);
		} else
			memset(ei_bl->i_data, 0, sizeof(ei_bl->i_data));
	}

	swap_inode_data(inode, inode_bl);

	inode->i_ctime = inode_bl->i_ctime = current_time(inode);

	spin_lock(&sbi->s_next_gen_lock);
	inode->i_generation = sbi->s_next_generation++;
	inode_bl->i_generation = sbi->s_next_generation++;
	spin_unlock(&sbi->s_next_gen_lock);

	ext4_discard_preallocations(inode, 0);

	err = ext4_mark_inode_dirty(handle, inode);
	if (err < 0) {
		ext4_warning(inode->i_sb,
			"couldn't mark inode #%lu dirty (err %d)",
			inode->i_ino, err);
		/* Revert all changes: */
		swap_inode_data(inode, inode_bl);
	} else {
		err = ext4_mark_inode_dirty(handle, inode_bl);
		if (err < 0) {
			ext4_warning(inode_bl->i_sb,
				"couldn't mark inode #%lu dirty (err %d)",
				inode_bl->i_ino, err);
			/* Revert all changes: */
			swap_inode_data(inode, inode_bl);
			ext4_mark_inode_dirty(handle, inode);
		}
	}
	ext4_journal_stop(handle);
	ext4_double_up_write_data_sem(inode, inode_bl);

journal_err_out:
	ext4_inode_resume_unlocked_dio(inode);
	ext4_inode_resume_unlocked_dio(inode_bl);
	unlock_two_nondirectories(inode, inode_bl);
	iput(inode_bl);
	return err;
}

#ifdef CONFIG_EXT4_FS_ENCRYPTION
static int uuid_is_zero(__u8 u[16])
{
	int	i;

	for (i = 0; i < 16; i++)
		if (u[i])
			return 0;
	return 1;
}
#endif

static int ext4_ioctl_setflags(struct inode *inode,
			       unsigned int flags)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	handle_t *handle = NULL;
	int err = -EPERM, migrate = 0;
	struct ext4_iloc iloc;
	unsigned int oldflags, mask, i;
	unsigned int jflag;

	/* Is it quota file? Do not allow user to mess with it */
	if (ext4_is_quota_file(inode))
		goto flags_out;

	oldflags = ei->i_flags;

	/* The JOURNAL_DATA flag is modifiable only by root */
	jflag = flags & EXT4_JOURNAL_DATA_FL;

	/*
	 * The IMMUTABLE and APPEND_ONLY flags can only be changed by
	 * the relevant capability.
	 *
	 * This test looks nicer. Thanks to Pauline Middelink
	 */
	if ((flags ^ oldflags) & (EXT4_APPEND_FL | EXT4_IMMUTABLE_FL)) {
		if (!capable(CAP_LINUX_IMMUTABLE))
			goto flags_out;
	}

	/*
	 * The JOURNAL_DATA flag can only be changed by
	 * the relevant capability.
	 */
	if ((jflag ^ oldflags) & (EXT4_JOURNAL_DATA_FL)) {
		if (!capable(CAP_SYS_RESOURCE))
			goto flags_out;
	}
	if ((flags ^ oldflags) & EXT4_EXTENTS_FL)
		migrate = 1;

	if (flags & EXT4_EOFBLOCKS_FL) {
		/* we don't support adding EOFBLOCKS flag */
		if (!(oldflags & EXT4_EOFBLOCKS_FL)) {
			err = -EOPNOTSUPP;
			goto flags_out;
		}
	} else if (oldflags & EXT4_EOFBLOCKS_FL) {
		err = ext4_truncate(inode);
		if (err)
			goto flags_out;
	}

	handle = ext4_journal_start(inode, EXT4_HT_INODE, 1);
	if (IS_ERR(handle)) {
		err = PTR_ERR(handle);
		goto flags_out;
	}
	if (IS_SYNC(inode))
		ext4_handle_sync(handle);
	err = ext4_reserve_inode_write(handle, inode, &iloc);
	if (err)
		goto flags_err;

	for (i = 0, mask = 1; i < 32; i++, mask <<= 1) {
		if (!(mask & EXT4_FL_USER_MODIFIABLE))
			continue;
		/* These flags get special treatment later */
		if (mask == EXT4_JOURNAL_DATA_FL || mask == EXT4_EXTENTS_FL)
			continue;
		if (mask & flags)
			ext4_set_inode_flag(inode, i);
		else
			ext4_clear_inode_flag(inode, i);
	}

	ext4_set_inode_flags(inode);
	inode->i_ctime = current_time(inode);

	err = ext4_mark_iloc_dirty(handle, inode, &iloc);
flags_err:
	ext4_journal_stop(handle);
	if (err)
		goto flags_out;

	if ((jflag ^ oldflags) & (EXT4_JOURNAL_DATA_FL)) {
		/*
		 * Changes to the journaling mode can cause unsafe changes to
		 * S_DAX if we are using the DAX mount option.
		 */
		if (test_opt(inode->i_sb, DAX)) {
			err = -EBUSY;
			goto flags_out;
		}

		err = ext4_change_inode_journal_flag(inode, jflag);
		if (err)
			goto flags_out;
	}
	if (migrate) {
		if (flags & EXT4_EXTENTS_FL)
			err = ext4_ext_migrate(inode);
		else
			err = ext4_ind_migrate(inode);
	}

flags_out:
	return err;
}

#ifdef CONFIG_QUOTA
static int ext4_ioctl_setproject(struct file *filp, __u32 projid)
{
	struct inode *inode = file_inode(filp);
	struct super_block *sb = inode->i_sb;
	struct ext4_inode_info *ei = EXT4_I(inode);
	int err, rc;
	handle_t *handle;
	kprojid_t kprojid;
	struct ext4_iloc iloc;
	struct ext4_inode *raw_inode;
	struct dquot *transfer_to[MAXQUOTAS] = { };

	if (!ext4_has_feature_project(sb)) {
		if (projid != EXT4_DEF_PROJID)
			return -EOPNOTSUPP;
		else
			return 0;
	}

	if (EXT4_INODE_SIZE(sb) <= EXT4_GOOD_OLD_INODE_SIZE)
		return -EOPNOTSUPP;

	kprojid = make_kprojid(&init_user_ns, (projid_t)projid);

	if (projid_eq(kprojid, EXT4_I(inode)->i_projid))
		return 0;

	err = -EPERM;
	/* Is it quota file? Do not allow user to mess with it */
	if (ext4_is_quota_file(inode))
		return err;

	err = ext4_get_inode_loc(inode, &iloc);
	if (err)
		return err;

	raw_inode = ext4_raw_inode(&iloc);
	if (!EXT4_FITS_IN_INODE(raw_inode, ei, i_projid)) {
		err = ext4_expand_extra_isize(inode,
					      EXT4_SB(sb)->s_want_extra_isize,
					      &iloc);
		if (err)
			return err;
	} else {
		brelse(iloc.bh);
	}

	err = dquot_initialize(inode);
	if (err)
		return err;

	handle = ext4_journal_start(inode, EXT4_HT_QUOTA,
		EXT4_QUOTA_INIT_BLOCKS(sb) +
		EXT4_QUOTA_DEL_BLOCKS(sb) + 3);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	err = ext4_reserve_inode_write(handle, inode, &iloc);
	if (err)
		goto out_stop;

	transfer_to[PRJQUOTA] = dqget(sb, make_kqid_projid(kprojid));
	if (!IS_ERR(transfer_to[PRJQUOTA])) {

		/* __dquot_transfer() calls back ext4_get_inode_usage() which
		 * counts xattr inode references.
		 */
		down_read(&EXT4_I(inode)->xattr_sem);
		err = __dquot_transfer(inode, transfer_to);
		up_read(&EXT4_I(inode)->xattr_sem);
		dqput(transfer_to[PRJQUOTA]);
		if (err)
			goto out_dirty;
	}

	EXT4_I(inode)->i_projid = kprojid;
	inode->i_ctime = current_time(inode);
out_dirty:
	rc = ext4_mark_iloc_dirty(handle, inode, &iloc);
	if (!err)
		err = rc;
out_stop:
	ext4_journal_stop(handle);
	return err;
}
#else
static int ext4_ioctl_setproject(struct file *filp, __u32 projid)
{
	if (projid != EXT4_DEF_PROJID)
		return -EOPNOTSUPP;
	return 0;
}
#endif

/* Transfer internal flags to xflags */
static inline __u32 ext4_iflags_to_xflags(unsigned long iflags)
{
	__u32 xflags = 0;

	if (iflags & EXT4_SYNC_FL)
		xflags |= FS_XFLAG_SYNC;
	if (iflags & EXT4_IMMUTABLE_FL)
		xflags |= FS_XFLAG_IMMUTABLE;
	if (iflags & EXT4_APPEND_FL)
		xflags |= FS_XFLAG_APPEND;
	if (iflags & EXT4_NODUMP_FL)
		xflags |= FS_XFLAG_NODUMP;
	if (iflags & EXT4_NOATIME_FL)
		xflags |= FS_XFLAG_NOATIME;
	if (iflags & EXT4_PROJINHERIT_FL)
		xflags |= FS_XFLAG_PROJINHERIT;
	return xflags;
}

#define EXT4_SUPPORTED_FS_XFLAGS (FS_XFLAG_SYNC | FS_XFLAG_IMMUTABLE | \
				  FS_XFLAG_APPEND | FS_XFLAG_NODUMP | \
				  FS_XFLAG_NOATIME | FS_XFLAG_PROJINHERIT)

/* Transfer xflags flags to internal */
static inline unsigned long ext4_xflags_to_iflags(__u32 xflags)
{
	unsigned long iflags = 0;

	if (xflags & FS_XFLAG_SYNC)
		iflags |= EXT4_SYNC_FL;
	if (xflags & FS_XFLAG_IMMUTABLE)
		iflags |= EXT4_IMMUTABLE_FL;
	if (xflags & FS_XFLAG_APPEND)
		iflags |= EXT4_APPEND_FL;
	if (xflags & FS_XFLAG_NODUMP)
		iflags |= EXT4_NODUMP_FL;
	if (xflags & FS_XFLAG_NOATIME)
		iflags |= EXT4_NOATIME_FL;
	if (xflags & FS_XFLAG_PROJINHERIT)
		iflags |= EXT4_PROJINHERIT_FL;

	return iflags;
}

static int ext4_shutdown(struct super_block *sb, unsigned long arg)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	__u32 flags;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (get_user(flags, (__u32 __user *)arg))
		return -EFAULT;

	if (flags > EXT4_GOING_FLAGS_NOLOGFLUSH)
		return -EINVAL;

	if (ext4_forced_shutdown(sbi))
		return 0;

	ext4_msg(sb, KERN_ALERT, "shut down requested (%d)", flags);

	switch (flags) {
	case EXT4_GOING_FLAGS_DEFAULT:
		freeze_bdev(sb->s_bdev);
		set_bit(EXT4_FLAGS_SHUTDOWN, &sbi->s_ext4_flags);
		thaw_bdev(sb->s_bdev, sb);
		break;
	case EXT4_GOING_FLAGS_LOGFLUSH:
		set_bit(EXT4_FLAGS_SHUTDOWN, &sbi->s_ext4_flags);
		if (sbi->s_journal && !is_journal_aborted(sbi->s_journal)) {
			(void) ext4_force_commit(sb);
			jbd2_journal_abort(sbi->s_journal, -ESHUTDOWN);
		}
		break;
	case EXT4_GOING_FLAGS_NOLOGFLUSH:
		set_bit(EXT4_FLAGS_SHUTDOWN, &sbi->s_ext4_flags);
		if (sbi->s_journal && !is_journal_aborted(sbi->s_journal))
			jbd2_journal_abort(sbi->s_journal, -ESHUTDOWN);
		break;
	default:
		return -EINVAL;
	}
	clear_opt(sb, DISCARD);
	return 0;
}

struct getfsmap_info {
	struct super_block	*gi_sb;
	struct fsmap_head __user *gi_data;
	unsigned int		gi_idx;
	__u32			gi_last_flags;
};

static int ext4_getfsmap_format(struct ext4_fsmap *xfm, void *priv)
{
	struct getfsmap_info *info = priv;
	struct fsmap fm;

	trace_ext4_getfsmap_mapping(info->gi_sb, xfm);

	info->gi_last_flags = xfm->fmr_flags;
	ext4_fsmap_from_internal(info->gi_sb, &fm, xfm);
	if (copy_to_user(&info->gi_data->fmh_recs[info->gi_idx++], &fm,
			sizeof(struct fsmap)))
		return -EFAULT;

	return 0;
}

static int ext4_ioc_getfsmap(struct super_block *sb,
			     struct fsmap_head __user *arg)
{
	struct getfsmap_info info = {0};
	struct ext4_fsmap_head xhead = {0};
	struct fsmap_head head;
	bool aborted = false;
	int error;

	if (copy_from_user(&head, arg, sizeof(struct fsmap_head)))
		return -EFAULT;
	if (memchr_inv(head.fmh_reserved, 0, sizeof(head.fmh_reserved)) ||
	    memchr_inv(head.fmh_keys[0].fmr_reserved, 0,
		       sizeof(head.fmh_keys[0].fmr_reserved)) ||
	    memchr_inv(head.fmh_keys[1].fmr_reserved, 0,
		       sizeof(head.fmh_keys[1].fmr_reserved)))
		return -EINVAL;
	/*
	 * ext4 doesn't report file extents at all, so the only valid
	 * file offsets are the magic ones (all zeroes or all ones).
	 */
	if (head.fmh_keys[0].fmr_offset ||
	    (head.fmh_keys[1].fmr_offset != 0 &&
	     head.fmh_keys[1].fmr_offset != -1ULL))
		return -EINVAL;

	xhead.fmh_iflags = head.fmh_iflags;
	xhead.fmh_count = head.fmh_count;
	ext4_fsmap_to_internal(sb, &xhead.fmh_keys[0], &head.fmh_keys[0]);
	ext4_fsmap_to_internal(sb, &xhead.fmh_keys[1], &head.fmh_keys[1]);

	trace_ext4_getfsmap_low_key(sb, &xhead.fmh_keys[0]);
	trace_ext4_getfsmap_high_key(sb, &xhead.fmh_keys[1]);

	info.gi_sb = sb;
	info.gi_data = arg;
	error = ext4_getfsmap(sb, &xhead, ext4_getfsmap_format, &info);
	if (error == EXT4_QUERY_RANGE_ABORT) {
		error = 0;
		aborted = true;
	} else if (error)
		return error;

	/* If we didn't abort, set the "last" flag in the last fmx */
	if (!aborted && info.gi_idx) {
		info.gi_last_flags |= FMR_OF_LAST;
		if (copy_to_user(&info.gi_data->fmh_recs[info.gi_idx - 1].fmr_flags,
				 &info.gi_last_flags,
				 sizeof(info.gi_last_flags)))
			return -EFAULT;
	}

	/* copy back header */
	head.fmh_entries = xhead.fmh_entries;
	head.fmh_oflags = xhead.fmh_oflags;
	if (copy_to_user(arg, &head, sizeof(struct fsmap_head)))
		return -EFAULT;

	return 0;
}

static int ext4_ioctl_check_project(struct inode *inode, struct fsxattr *fa)
{
	/*
	 * Project Quota ID state is only allowed to change from within the init
	 * namespace. Enforce that restriction only if we are trying to change
	 * the quota ID state. Everything else is allowed in user namespaces.
	 */
	if (current_user_ns() == &init_user_ns)
		return 0;

	if (__kprojid_val(EXT4_I(inode)->i_projid) != fa->fsx_projid)
		return -EINVAL;

	if (ext4_test_inode_flag(inode, EXT4_INODE_PROJINHERIT)) {
		if (!(fa->fsx_xflags & FS_XFLAG_PROJINHERIT))
			return -EINVAL;
	} else {
		if (fa->fsx_xflags & FS_XFLAG_PROJINHERIT)
			return -EINVAL;
	}

	return 0;
}

long ext4_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	struct super_block *sb = inode->i_sb;
	struct ext4_inode_info *ei = EXT4_I(inode);
	unsigned int flags;

	ext4_debug("cmd = %u, arg = %lu\n", cmd, arg);

	switch (cmd) {
	case FS_IOC_GETFSMAP:
		return ext4_ioc_getfsmap(sb, (void __user *)arg);
	case EXT4_IOC_GETFLAGS:
		flags = ei->i_flags & EXT4_FL_USER_VISIBLE;
		return put_user(flags, (int __user *) arg);
	case EXT4_IOC_SETFLAGS: {
		int err;

		if (!inode_owner_or_capable(inode))
			return -EACCES;

		if (get_user(flags, (int __user *) arg))
			return -EFAULT;

		if (flags & ~EXT4_FL_USER_VISIBLE)
			return -EOPNOTSUPP;
		/*
		 * chattr(1) grabs flags via GETFLAGS, modifies the result and
		 * passes that to SETFLAGS. So we cannot easily make SETFLAGS
		 * more restrictive than just silently masking off visible but
		 * not settable flags as we always did.
		 */
		flags &= EXT4_FL_USER_MODIFIABLE;
		if (ext4_mask_flags(inode->i_mode, flags) != flags)
			return -EOPNOTSUPP;

		err = mnt_want_write_file(filp);
		if (err)
			return err;

		inode_lock(inode);
		err = ext4_ioctl_setflags(inode, flags);
		inode_unlock(inode);
		mnt_drop_write_file(filp);
		return err;
	}
	case EXT4_IOC_GETVERSION:
	case EXT4_IOC_GETVERSION_OLD:
		return put_user(inode->i_generation, (int __user *) arg);
	case EXT4_IOC_SETVERSION:
	case EXT4_IOC_SETVERSION_OLD: {
		handle_t *handle;
		struct ext4_iloc iloc;
		__u32 generation;
		int err;

		if (!inode_owner_or_capable(inode))
			return -EPERM;

		if (ext4_has_metadata_csum(inode->i_sb)) {
			ext4_warning(sb, "Setting inode version is not "
				     "supported with metadata_csum enabled.");
			return -ENOTTY;
		}

		err = mnt_want_write_file(filp);
		if (err)
			return err;
		if (get_user(generation, (int __user *) arg)) {
			err = -EFAULT;
			goto setversion_out;
		}

		inode_lock(inode);
		handle = ext4_journal_start(inode, EXT4_HT_INODE, 1);
		if (IS_ERR(handle)) {
			err = PTR_ERR(handle);
			goto unlock_out;
		}
		err = ext4_reserve_inode_write(handle, inode, &iloc);
		if (err == 0) {
			inode->i_ctime = current_time(inode);
			inode->i_generation = generation;
			err = ext4_mark_iloc_dirty(handle, inode, &iloc);
		}
		ext4_journal_stop(handle);

unlock_out:
		inode_unlock(inode);
setversion_out:
		mnt_drop_write_file(filp);
		return err;
	}
	case EXT4_IOC_GROUP_EXTEND: {
		ext4_fsblk_t n_blocks_count;
		int err, err2=0;

		err = ext4_resize_begin(sb);
		if (err)
			return err;

		if (get_user(n_blocks_count, (__u32 __user *)arg)) {
			err = -EFAULT;
			goto group_extend_out;
		}

		if (ext4_has_feature_bigalloc(sb)) {
			ext4_msg(sb, KERN_ERR,
				 "Online resizing not supported with bigalloc");
			err = -EOPNOTSUPP;
			goto group_extend_out;
		}

		err = mnt_want_write_file(filp);
		if (err)
			goto group_extend_out;

		err = ext4_group_extend(sb, EXT4_SB(sb)->s_es, n_blocks_count);
		if (EXT4_SB(sb)->s_journal) {
			jbd2_journal_lock_updates(EXT4_SB(sb)->s_journal);
			err2 = jbd2_journal_flush(EXT4_SB(sb)->s_journal);
			jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);
		}
		if (err == 0)
			err = err2;
		mnt_drop_write_file(filp);
group_extend_out:
		ext4_resize_end(sb);
		return err;
	}

	case EXT4_IOC_MOVE_EXT: {
		struct move_extent me;
		struct fd donor;
		int err;

		if (!(filp->f_mode & FMODE_READ) ||
		    !(filp->f_mode & FMODE_WRITE))
			return -EBADF;

		if (copy_from_user(&me,
			(struct move_extent __user *)arg, sizeof(me)))
			return -EFAULT;
		me.moved_len = 0;

		donor = fdget(me.donor_fd);
		if (!donor.file)
			return -EBADF;

		if (!(donor.file->f_mode & FMODE_WRITE)) {
			err = -EBADF;
			goto mext_out;
		}

		if (ext4_has_feature_bigalloc(sb)) {
			ext4_msg(sb, KERN_ERR,
				 "Online defrag not supported with bigalloc");
			err = -EOPNOTSUPP;
			goto mext_out;
		} else if (IS_DAX(inode)) {
			ext4_msg(sb, KERN_ERR,
				 "Online defrag not supported with DAX");
			err = -EOPNOTSUPP;
			goto mext_out;
		}

		err = mnt_want_write_file(filp);
		if (err)
			goto mext_out;

		err = ext4_move_extents(filp, donor.file, me.orig_start,
					me.donor_start, me.len, &me.moved_len);
		mnt_drop_write_file(filp);

		if (copy_to_user((struct move_extent __user *)arg,
				 &me, sizeof(me)))
			err = -EFAULT;
mext_out:
		fdput(donor);
		return err;
	}

	case EXT4_IOC_GROUP_ADD: {
		struct ext4_new_group_data input;
		int err, err2=0;

		err = ext4_resize_begin(sb);
		if (err)
			return err;

		if (copy_from_user(&input, (struct ext4_new_group_input __user *)arg,
				sizeof(input))) {
			err = -EFAULT;
			goto group_add_out;
		}

		if (ext4_has_feature_bigalloc(sb)) {
			ext4_msg(sb, KERN_ERR,
				 "Online resizing not supported with bigalloc");
			err = -EOPNOTSUPP;
			goto group_add_out;
		}

		err = mnt_want_write_file(filp);
		if (err)
			goto group_add_out;

		err = ext4_group_add(sb, &input);
		if (EXT4_SB(sb)->s_journal) {
			jbd2_journal_lock_updates(EXT4_SB(sb)->s_journal);
			err2 = jbd2_journal_flush(EXT4_SB(sb)->s_journal);
			jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);
		}
		if (err == 0)
			err = err2;
		mnt_drop_write_file(filp);
		if (!err && ext4_has_group_desc_csum(sb) &&
		    test_opt(sb, INIT_INODE_TABLE))
			err = ext4_register_li_request(sb, input.group);
group_add_out:
		ext4_resize_end(sb);
		return err;
	}

	case EXT4_IOC_MIGRATE:
	{
		int err;
		if (!inode_owner_or_capable(inode))
			return -EACCES;

		err = mnt_want_write_file(filp);
		if (err)
			return err;
		/*
		 * inode_mutex prevent write and truncate on the file.
		 * Read still goes through. We take i_data_sem in
		 * ext4_ext_swap_inode_data before we switch the
		 * inode format to prevent read.
		 */
		inode_lock((inode));
		err = ext4_ext_migrate(inode);
		inode_unlock((inode));
		mnt_drop_write_file(filp);
		return err;
	}

	case EXT4_IOC_ALLOC_DA_BLKS:
	{
		int err;
		if (!inode_owner_or_capable(inode))
			return -EACCES;

		err = mnt_want_write_file(filp);
		if (err)
			return err;
		err = ext4_alloc_da_blocks(inode);
		mnt_drop_write_file(filp);
		return err;
	}

	case EXT4_IOC_SWAP_BOOT:
	{
		int err;
		if (!(filp->f_mode & FMODE_WRITE))
			return -EBADF;
		err = mnt_want_write_file(filp);
		if (err)
			return err;
		err = swap_inode_boot_loader(sb, inode);
		mnt_drop_write_file(filp);
		return err;
	}

	case EXT4_IOC_RESIZE_FS: {
		ext4_fsblk_t n_blocks_count;
		int err = 0, err2 = 0;
		ext4_group_t o_group = EXT4_SB(sb)->s_groups_count;

		if (ext4_has_feature_bigalloc(sb)) {
			ext4_msg(sb, KERN_ERR,
				 "Online resizing not (yet) supported with bigalloc");
			return -EOPNOTSUPP;
		}

		if (copy_from_user(&n_blocks_count, (__u64 __user *)arg,
				   sizeof(__u64))) {
			return -EFAULT;
		}

		err = ext4_resize_begin(sb);
		if (err)
			return err;

		err = mnt_want_write_file(filp);
		if (err)
			goto resizefs_out;

		err = ext4_resize_fs(sb, n_blocks_count);
		if (EXT4_SB(sb)->s_journal) {
			jbd2_journal_lock_updates(EXT4_SB(sb)->s_journal);
			err2 = jbd2_journal_flush(EXT4_SB(sb)->s_journal);
			jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);
		}
		if (err == 0)
			err = err2;
		mnt_drop_write_file(filp);
		if (!err && (o_group > EXT4_SB(sb)->s_groups_count) &&
		    ext4_has_group_desc_csum(sb) &&
		    test_opt(sb, INIT_INODE_TABLE))
			err = ext4_register_li_request(sb, o_group);

resizefs_out:
		ext4_resize_end(sb);
		return err;
	}

	case FITRIM:
	{
		struct request_queue *q = bdev_get_queue(sb->s_bdev);
		struct fstrim_range range;
		int ret = 0;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (!blk_queue_discard(q))
			return -EOPNOTSUPP;

		if (copy_from_user(&range, (struct fstrim_range __user *)arg,
		    sizeof(range)))
			return -EFAULT;

		range.minlen = max((unsigned int)range.minlen,
				   q->limits.discard_granularity);
		ret = ext4_trim_fs(sb, &range);
		if (ret < 0)
			return ret;

		if (copy_to_user((struct fstrim_range __user *)arg, &range,
		    sizeof(range)))
			return -EFAULT;

		return 0;
	}
	case EXT4_IOC_PRECACHE_EXTENTS:
		return ext4_ext_precache(inode);

	case EXT4_IOC_SET_ENCRYPTION_POLICY:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPNOTSUPP;
		return fscrypt_ioctl_set_policy(filp, (const void __user *)arg);

	case EXT4_IOC_GET_ENCRYPTION_PWSALT: {
#ifdef CONFIG_EXT4_FS_ENCRYPTION
		int err, err2;
		struct ext4_sb_info *sbi = EXT4_SB(sb);
		handle_t *handle;

		if (!ext4_has_feature_encrypt(sb))
			return -EOPNOTSUPP;
		if (uuid_is_zero(sbi->s_es->s_encrypt_pw_salt)) {
			err = mnt_want_write_file(filp);
			if (err)
				return err;
			handle = ext4_journal_start_sb(sb, EXT4_HT_MISC, 1);
			if (IS_ERR(handle)) {
				err = PTR_ERR(handle);
				goto pwsalt_err_exit;
			}
			err = ext4_journal_get_write_access(handle, sbi->s_sbh);
			if (err)
				goto pwsalt_err_journal;
			generate_random_uuid(sbi->s_es->s_encrypt_pw_salt);
			err = ext4_handle_dirty_metadata(handle, NULL,
							 sbi->s_sbh);
		pwsalt_err_journal:
			err2 = ext4_journal_stop(handle);
			if (err2 && !err)
				err = err2;
		pwsalt_err_exit:
			mnt_drop_write_file(filp);
			if (err)
				return err;
		}
		if (copy_to_user((void __user *) arg,
				 sbi->s_es->s_encrypt_pw_salt, 16))
			return -EFAULT;
		return 0;
#else
		return -EOPNOTSUPP;
#endif
	}
	case EXT4_IOC_GET_ENCRYPTION_POLICY:
		return fscrypt_ioctl_get_policy(filp, (void __user *)arg);

	case EXT4_IOC_FSGETXATTR:
	{
		struct fsxattr fa;

		memset(&fa, 0, sizeof(struct fsxattr));
		fa.fsx_xflags = ext4_iflags_to_xflags(ei->i_flags & EXT4_FL_USER_VISIBLE);

		if (ext4_has_feature_project(inode->i_sb)) {
			fa.fsx_projid = (__u32)from_kprojid(&init_user_ns,
				EXT4_I(inode)->i_projid);
		}

		if (copy_to_user((struct fsxattr __user *)arg,
				 &fa, sizeof(fa)))
			return -EFAULT;
		return 0;
	}
	case EXT4_IOC_FSSETXATTR:
	{
		struct fsxattr fa;
		int err;

		if (copy_from_user(&fa, (struct fsxattr __user *)arg,
				   sizeof(fa)))
			return -EFAULT;

		/* Make sure caller has proper permission */
		if (!inode_owner_or_capable(inode))
			return -EACCES;

		if (fa.fsx_xflags & ~EXT4_SUPPORTED_FS_XFLAGS)
			return -EOPNOTSUPP;

		flags = ext4_xflags_to_iflags(fa.fsx_xflags);
		if (ext4_mask_flags(inode->i_mode, flags) != flags)
			return -EOPNOTSUPP;

		err = mnt_want_write_file(filp);
		if (err)
			return err;

		inode_lock(inode);
		err = ext4_ioctl_check_project(inode, &fa);
		if (err)
			goto out;
		flags = (ei->i_flags & ~EXT4_FL_XFLAG_VISIBLE) |
			 (flags & EXT4_FL_XFLAG_VISIBLE);
		err = ext4_ioctl_setflags(inode, flags);
		if (err)
			goto out;
		err = ext4_ioctl_setproject(filp, fa.fsx_projid);
out:
		inode_unlock(inode);
		mnt_drop_write_file(filp);
		return err;
	}
	case EXT4_IOC_SHUTDOWN:
		return ext4_shutdown(sb, arg);
	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
long ext4_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	/* These are just misnamed, they actually get/put from/to user an int */
	switch (cmd) {
	case EXT4_IOC32_GETFLAGS:
		cmd = EXT4_IOC_GETFLAGS;
		break;
	case EXT4_IOC32_SETFLAGS:
		cmd = EXT4_IOC_SETFLAGS;
		break;
	case EXT4_IOC32_GETVERSION:
		cmd = EXT4_IOC_GETVERSION;
		break;
	case EXT4_IOC32_SETVERSION:
		cmd = EXT4_IOC_SETVERSION;
		break;
	case EXT4_IOC32_GROUP_EXTEND:
		cmd = EXT4_IOC_GROUP_EXTEND;
		break;
	case EXT4_IOC32_GETVERSION_OLD:
		cmd = EXT4_IOC_GETVERSION_OLD;
		break;
	case EXT4_IOC32_SETVERSION_OLD:
		cmd = EXT4_IOC_SETVERSION_OLD;
		break;
	case EXT4_IOC32_GETRSVSZ:
		cmd = EXT4_IOC_GETRSVSZ;
		break;
	case EXT4_IOC32_SETRSVSZ:
		cmd = EXT4_IOC_SETRSVSZ;
		break;
	case EXT4_IOC32_GROUP_ADD: {
		struct compat_ext4_new_group_input __user *uinput;
		struct ext4_new_group_input input;
		mm_segment_t old_fs;
		int err;

		uinput = compat_ptr(arg);
		err = get_user(input.group, &uinput->group);
		err |= get_user(input.block_bitmap, &uinput->block_bitmap);
		err |= get_user(input.inode_bitmap, &uinput->inode_bitmap);
		err |= get_user(input.inode_table, &uinput->inode_table);
		err |= get_user(input.blocks_count, &uinput->blocks_count);
		err |= get_user(input.reserved_blocks,
				&uinput->reserved_blocks);
		if (err)
			return -EFAULT;
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = ext4_ioctl(file, EXT4_IOC_GROUP_ADD,
				 (unsigned long) &input);
		set_fs(old_fs);
		return err;
	}
	case EXT4_IOC_MOVE_EXT:
	case EXT4_IOC_RESIZE_FS:
	case EXT4_IOC_PRECACHE_EXTENTS:
	case EXT4_IOC_SET_ENCRYPTION_POLICY:
	case EXT4_IOC_GET_ENCRYPTION_PWSALT:
	case EXT4_IOC_GET_ENCRYPTION_POLICY:
	case EXT4_IOC_SHUTDOWN:
	case FS_IOC_GETFSMAP:
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return ext4_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif
