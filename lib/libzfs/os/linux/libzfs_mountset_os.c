// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2026, TrueNAS.
 */

#include <sys/mntent.h>
#include <libmount/libmount.h>
#include <libzfs.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include "../../libzfs_impl.h"

/*
 * zfs_mountset_t: snapshot of all ZFS mounts, query surface
 * zfs_mount_t: a single mount, read from the set
 */

struct zfs_mountset {
	/* libmount does the lifting for us, we delegate all the work here */
	struct libmnt_table *mset_tab;

	/*
	 * protecting access to mset_tab. this is basicaly a cheesy rrwlock;
	 * we need read reentrancy to keep the table pinned during iteration.
	 */
	kmutex_t mset_lock;
	kcondvar_t mset_cv;
	uint64_t mset_holds;
};

zfs_mountset_t *
zfs_mountset_open(void)
{
	struct libmnt_table *tab =
	    mnt_new_table_from_file("/proc/self/mountinfo");
	if (tab == NULL)
		return (NULL);

	zfs_mountset_t *mset = calloc(1, sizeof (zfs_mountset_t));
	mset->mset_tab = tab;
	mutex_init(&mset->mset_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&mset->mset_cv, NULL, CV_DEFAULT, NULL);
	mset->mset_holds = 0;

	return (mset);
}

void
zfs_mountset_close(zfs_mountset_t *mset)
{
	ASSERT(!MUTEX_HELD(&mset->mset_lock));
	VERIFY3U(mset->mset_holds, ==, 0);
	mutex_destroy(&mset->mset_lock);
	cv_destroy(&mset->mset_cv);
	mnt_unref_table(mset->mset_tab);
	free(mset);
}

void
zfs_mountset_enter(zfs_mountset_t *mset)
{
	ASSERT(!MUTEX_HELD(&mset->mset_lock));
	mutex_enter(&mset->mset_lock);
	mset->mset_holds++;
	mutex_exit(&mset->mset_lock);
}

void
zfs_mountset_exit(zfs_mountset_t *mset)
{
	ASSERT(!MUTEX_HELD(&mset->mset_lock));
	mutex_enter(&mset->mset_lock);
	mset->mset_holds--;
	cv_broadcast(&mset->mset_cv);
	mutex_exit(&mset->mset_lock);
}

static void
zfs_mountset_enter_update(zfs_mountset_t *mset)
{
	ASSERT(!MUTEX_HELD(&mset->mset_lock));
	mutex_enter(&mset->mset_lock);
	while (mset->mset_holds > 0)
		cv_wait(&mset->mset_cv, &mset->mset_lock);
}

static void
zfs_mountset_exit_update(zfs_mountset_t *mset)
{
	ASSERT(MUTEX_HELD(&mset->mset_lock));
	cv_broadcast(&mset->mset_cv);
	mutex_exit(&mset->mset_lock);
}

int
zfs_mountset_refresh(zfs_mountset_t *mset)
{
	zfs_mountset_enter_update(mset);
	VERIFY0(mnt_reset_table(mset->mset_tab));
	VERIFY0(mnt_table_parse_file(mset->mset_tab, "/proc/self/mountinfo"));
	zfs_mountset_exit_update(mset);
	return (0);
}

void
zfs_mount_hold(zfs_mount_t *mnt)
{
	mnt_ref_fs((struct libmnt_fs *) mnt);
}

void
zfs_mount_drop(zfs_mount_t *mnt)
{
	mnt_unref_fs((struct libmnt_fs *) mnt);
}

const char *
zfs_mount_get_dataset(zfs_mount_t *mnt)
{
	return (mnt_fs_get_source((struct libmnt_fs *) mnt));
}

const char *
zfs_mount_get_mountpoint(zfs_mount_t *mnt)
{
	return (mnt_fs_get_target((struct libmnt_fs *) mnt));
}

const char *
zfs_mount_get_options(zfs_mount_t *mnt)
{
	return (mnt_fs_get_options((struct libmnt_fs *) mnt));
}

int
zfs_mountset_find_dataset(zfs_mountset_t *mset, const char *dsname,
    zfs_mount_t **mntp)
{
	struct libmnt_fs *fs = mnt_table_find_source(mset->mset_tab,
	    dsname, MNT_ITER_BACKWARD);
	if (fs == NULL)
		return (ENOENT);

	/*
	 * We only deal in ZFS filesystems, so ignore anything else with
	 * the same source name.
	 *
	 * XXX actually, we should iterate over the others until we find
	 *     a ZFS; we don't have a monopoly on source names, they're just
	 *     strings.
	 */
	if (strcmp(mnt_fs_get_fstype(fs), MNTTYPE_ZFS) != 0)
		return (ENOENT);

	*mntp = (zfs_mount_t *) fs;
	return (0);
}

int
zfs_mountset_find_mountpoint(zfs_mountset_t *mset, const char *mountpoint,
    zfs_mount_t **mntp)
{
	/*
	 * XXX path canonicalisation is undoubtedly an OS-specific function.
	 *     I'm not sure though what it looks like, and if it should be
	 *     automatic or a separate step. for now, doing nothing.
	 *
	 *     if/when I do something, it will likely involve libmnt_cache.
	 *       -- robn, 2026-03-12
	 */
	struct libmnt_fs *fs = mnt_table_find_target(mset->mset_tab,
	    mountpoint, MNT_ITER_BACKWARD);
	if (fs == NULL)
		return (ENOENT);

	/*
	 * We only deal in ZFS filesystems, so ignore anything else on that
	 * mountpoint.
	 */
	if (strcmp(mnt_fs_get_fstype(fs), MNTTYPE_ZFS) != 0)
		return (ENOENT);

	*mntp = (zfs_mount_t *) fs;
	return (0);
}

int
zfs_mountset_find_path(zfs_mountset_t *mset, const char *path,
    zfs_mount_t **mntp)
{
	/*
	 * XXX path canonicalisation is undoubtedly an OS-specific function.
	 *     I'm not sure though what it looks like, and if it should be
	 *     automatic or a separate step. for now, doing nothing.
	 *
	 *     if/when I do something, it will likely involve libmnt_cache.
	 *       -- robn, 2026-03-12
	 */

	int err = ENOENT;

	/* Fast path; if the path is a mountpoint, use it. */
	struct libmnt_fs *fs = mnt_table_find_target(mset->mset_tab,
	    path, MNT_ITER_BACKWARD);
	if (fs != NULL) {
		err = 0;
		goto out;
	}

#ifdef HAVE_STATX_MNT_ID
	/* Lookup the path to get the mount id */
	struct statx stx;
	if (statx(AT_FDCWD, path, AT_STATX_SYNC_AS_STAT | AT_SYMLINK_NOFOLLOW,
	    STATX_MNT_ID, &stx) != 0) {
		err = errno;
		goto out;
	}

	if (!(stx.stx_mask & STATX_MNT_ID))
		/* XXX unsure. maybe kernel doesn't know it? */
		goto out;

	/*
	 * XXX mnt_table_find_uniq_id() is safer but needs kernel 6.8
	 *     and I'll need to go learn more about how to ask for it
	 *       -- robn, 2026-03-12
	 */
	fs = mnt_table_find_id(mset->mset_tab, stx.stx_mnt_id);
	if (fs == NULL)
		/* XXX kernel told us it exists, maybe stale? refresh table? */
		goto out;

	err = 0;
#else
	/* XXX use stat and statvfs and compare major/minor */
#endif

out:
	if (err == 0) {
		/*
		 * We only deal in ZFS filesystems, so ignore anything else on
		 * that mountpoint.
		*/
		if (strcmp(mnt_fs_get_fstype(fs), MNTTYPE_ZFS) != 0)
			err = ENOENT;
		else
			*mntp = (zfs_mount_t *) fs;
	}

	return (err);
}

int
zfs_mountset_find_pair(zfs_mountset_t *mset, const char *dsname,
    const char *mountpoint, zfs_mount_t **mntp)
{
	struct libmnt_fs *fs = mnt_table_find_pair(mset->mset_tab,
	    dsname, mountpoint, MNT_ITER_BACKWARD);
	if (fs == NULL)
		return (ENOENT);

	/*
	 * We only deal in ZFS filesystems, so ignore anything else on that
	 * mountpoint.
	 */
	if (strcmp(mnt_fs_get_fstype(fs), MNTTYPE_ZFS) != 0)
		return (ENOENT);

	*mntp = (zfs_mount_t *) fs;
	return (0);
}

int
zfs_mountset_iter(zfs_mountset_t *mset, zfs_mountset_order_t order,
    zfs_mount_t **mntp)
{
	int err = 0;

	struct libmnt_iter *iter = mnt_new_iter(
	    order == ZFS_MOUNTSET_ORDER_UNMOUNT ?
	    MNT_ITER_BACKWARD : MNT_ITER_FORWARD);

	struct libmnt_fs *fs = (struct libmnt_fs *) *mntp;
	if (fs != NULL) {
		/*
		 * If caller passed a mount, then we're in an iteration
		 * loop. Reset the iterator to its position.
		 */
		err = mnt_table_set_iter(mset->mset_tab, iter, fs);
		if (err != 0)
			goto out;

		/*
		 * mnt_table_next_fs() returns the item at the iterator's
		 * current position (ie x++, not ++x). So, move one past it
		 * to get the loop rolling correctly.
		 */
		err = mnt_table_next_fs(mset->mset_tab, iter, &fs);
		if (err != 0)
			goto out;

		ASSERT3P(fs, ==, *mntp);
	}

	while ((err = mnt_table_next_fs(mset->mset_tab, iter, &fs)) == 0) {
		if (strcmp(mnt_fs_get_fstype(fs), MNTTYPE_ZFS) == 0)
			break;
	}

out:
	mnt_free_iter(iter);

	if (err < 0)
		return (-err);

	if (err > 0) {
		*mntp = NULL;
		return (ESRCH);
	}

	*mntp = (zfs_mount_t *) fs;
	return (0);
}

/*
 * XXX work out what the actual defaults are/should be, and set them up
 *     here? though it makes me wonder if the default methods are ever gonna
 *     be used.
 *
 *     mount(8) says:
 *
 *     defaults
 *
 *         Use the default options: rw, suid, dev, exec, auto, nouser, and
 *         async.
 *
 *         Note that the real set of all default mount options depends on the
 *         kernel and filesystem type. See the beginning of this section for
 *         more details.
 */

#define	OPT_DEFAULT_READONLY	"rw"
#define	OPT_DEFAULT_EXEC	"exec"
#define	OPT_DEFAULT_SETUID	"suid"
#define	OPT_DEFAULT_DEVICES	"dev"
#define	OPT_DEFAULT_MAND	"nomand"
#define	OPT_DEFAULT_ATIME	"relatime"
#define	OPT_DEFAULT_XATTR	"xattr"

int
zfs_mountset_apply(zfs_mountset_t *mset, zfs_mountbuilder_t *mb)
{
	int err = 0;

	struct libmnt_context *mctx = mnt_new_context();

	/* We will provide everything; ignore fstab etc. */
	mnt_context_set_optsmode(mctx, MNT_OMODE_NOTAB);

	/* Disable any table updates, we will manage this ourselves. */
	mnt_context_disable_mtab(mctx, 1);

	/* Disable libmount security policies; they aren't applicable here. */
	mnt_context_force_unrestricted(mctx);

	/*
	 * For the actually mount/unmount call, call syscalls directly; don't
	 * call out to userspace helpers.
	 */
	mnt_context_disable_helpers(mctx, 1);

	/* Initial setup for requested operation */
	if (mb->mb_op == ZFS_MOUNTBUILDER_OP_MOUNT) {
		/* New mount; set source & target to dataset & mountpoint. */
		if ((err = mnt_context_set_source(mctx,
		    mb->mb_dataset)) != 0)
			goto out;
		if ((err = mnt_context_set_target(mctx,
		    mb->mb_mountpoint)) != 0)
			goto out;

		/* There is only ZFS. */
		mnt_context_set_fstype(mctx, MNTTYPE_ZFS);
	} else {
		/*
		 * Mount or remount; copy the provided fs and add it to the
		 * context. Copy is necessary because we don't want to modify
		 * the caller's version.
		 */
		if ((err = mnt_context_set_fs(mctx,
		    mnt_copy_fs(NULL, (struct libmnt_fs *) mb->mb_mount))) != 0)
			goto out;
	}

	if (mb->mb_opt_changed_readonly != ZFS_MOUNTOPT_UNCHANGED) {
		mnt_context_append_options(mctx,
		    mb->mb_opt_changed_readonly == ZFS_MOUNTOPT_SET ?
		    (mb->mb_opt_val_readonly ? "ro" : "rw") :
		    OPT_DEFAULT_READONLY);
	}
	if (mb->mb_opt_changed_exec != ZFS_MOUNTOPT_UNCHANGED) {
		mnt_context_append_options(mctx,
		    mb->mb_opt_changed_exec == ZFS_MOUNTOPT_SET ?
		    (mb->mb_opt_val_exec ? "exec" : "noexec") :
		    OPT_DEFAULT_EXEC);
	}
	if (mb->mb_opt_changed_setuid != ZFS_MOUNTOPT_UNCHANGED) {
		mnt_context_append_options(mctx,
		    mb->mb_opt_changed_setuid == ZFS_MOUNTOPT_SET ?
		    (mb->mb_opt_val_setuid ? "suid" : "nosuid") :
		    OPT_DEFAULT_SETUID);
	}
	if (mb->mb_opt_changed_devices != ZFS_MOUNTOPT_UNCHANGED) {
		mnt_context_append_options(mctx,
		    mb->mb_opt_changed_devices == ZFS_MOUNTOPT_SET ?
		    (mb->mb_opt_val_devices ? "dev" : "nodev") :
		    OPT_DEFAULT_DEVICES);
	}
	if (mb->mb_opt_changed_mand != ZFS_MOUNTOPT_UNCHANGED) {
		mnt_context_append_options(mctx,
		    mb->mb_opt_changed_mand == ZFS_MOUNTOPT_SET ?
		    (mb->mb_opt_val_mand ? "mand" : "nomand") :
		    OPT_DEFAULT_MAND);
	}

	if (mb->mb_opt_changed_atime != ZFS_MOUNTOPT_UNCHANGED) {
		mnt_context_append_options(mctx,
		    mb->mb_opt_changed_atime == ZFS_MOUNTOPT_SET ?
		    (mb->mb_opt_val_atime == ZFS_MOUNTOPT_ATIME_ON ? "atime" :
		    mb->mb_opt_val_atime == ZFS_MOUNTOPT_ATIME_RELATIVE ? "relatime" : "noatime") :
		    OPT_DEFAULT_ATIME);
	}
	if (mb->mb_opt_changed_xattr != ZFS_MOUNTOPT_UNCHANGED) {
		mnt_context_append_options(mctx,
		    mb->mb_opt_changed_xattr == ZFS_MOUNTOPT_SET ?
		    (mb->mb_opt_val_xattr == ZFS_MOUNTOPT_XATTR_ON ? "xattr" :
		    mb->mb_opt_val_xattr == ZFS_MOUNTOPT_XATTR_DIR ? "dirxattr" :
		    mb->mb_opt_val_xattr == ZFS_MOUNTOPT_XATTR_SA ? "saxattr" : "noxattr") :
		    OPT_DEFAULT_XATTR);
	}

	if (mb->mb_raw_opts != NULL) {
		for (uint_t i = 0; i < mb->mb_raw_count; i++)
			mnt_context_append_options(mctx, mb->mb_raw_opts[i]);
	}

	if (mb->mb_op == ZFS_MOUNTBUILDER_OP_UNMOUNT) {
		if (mb->mb_op_flags & ZFS_MOUNTBUILDER_UNMOUNT_DETACH)
			mnt_context_enable_lazy(mctx, 1);
		if ((err = mnt_context_prepare_umount(mctx)) != 0)
			goto out;
	} else {
		if (mb->mb_op == ZFS_MOUNTBUILDER_OP_REMOUNT)
			mnt_context_append_options(mctx, "remount");
		if ((err = mnt_context_prepare_mount(mctx)) != 0)
			goto out;
	}

	fprintf(stderr, "apply ready: op=%s ds=%s mp=%s opts=%s\n",
	    mb->mb_op == ZFS_MOUNTBUILDER_OP_MOUNT ? "MOUNT" :
	    mb->mb_op == ZFS_MOUNTBUILDER_OP_UNMOUNT ? "UNMOUNT" : "REMOUNT",
	    mnt_context_get_source(mctx), mnt_context_get_target(mctx),
	    mnt_context_get_options(mctx));

	if (mb->mb_op == ZFS_MOUNTBUILDER_OP_UNMOUNT)
		err = mnt_context_do_umount(mctx);
	else
		err = mnt_context_do_mount(mctx);

	zfs_mountset_enter_update(mset);

	/* XXX on success, apply to mset */
	(void) mset;

	zfs_mountset_exit_update(mset);

out:
	if (err != 0) {
		int syserr = 0;
		if (mnt_context_syscall_called(mctx))
			syserr = mnt_context_get_syscall_errno(mctx);
		if (syserr != 0)
			err = syserr;
		else {
			char buf[512];
			mnt_context_get_excode(mctx, -err, buf, sizeof (buf));
			fprintf(stderr,
			    "libmount internal error [%d]: %s\n", -err, buf);
			err = EIO;
		}
	}

	mnt_free_context(mctx);
	zfs_mountbuilder_free(mb);

	return (err);
}
