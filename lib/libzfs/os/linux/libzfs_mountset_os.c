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
zfs_mountset_foreach(zfs_mountset_t *mset, zfs_mountset_order_t order,
    zfs_mountset_foreach_f func, void *arg)
{
	struct libmnt_fs *fs = NULL;
	struct libmnt_iter *iter = mnt_new_iter(
	    order == ZFS_MOUNTSET_ORDER_UNMOUNT ?
	    MNT_ITER_BACKWARD : MNT_ITER_FORWARD);

	boolean_t aborted = B_FALSE;
	int err = 0;
	while (!aborted &&
	    (err = mnt_table_next_fs(mset->mset_tab, iter, &fs)) == 0) {
		if (strcmp(mnt_fs_get_fstype(fs), MNTTYPE_ZFS) != 0)
			continue;
		aborted = func(mset, (zfs_mount_t *) fs, arg);
	}

	mnt_free_iter(iter);

	if (err < 0)
		return (-err);
	if (aborted)
		return (-1);
	return (0);
}
