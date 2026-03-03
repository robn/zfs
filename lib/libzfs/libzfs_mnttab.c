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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2019 Joyent, Inc.
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 * Copyright (c) 2012 DEY Storage Systems, Inc.  All rights reserved.
 * Copyright (c) 2012 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * Copyright (c) 2013 Martin Matuska. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright 2017 Nexenta Systems, Inc.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>
 * Copyright 2017-2018 RackTop Systems.
 * Copyright (c) 2019 Datto Inc.
 * Copyright (c) 2019, loli10K <ezomori.nozomu@gmail.com>
 * Copyright (c) 2021 Matt Fiddaman
 */

#include <sys/mntent.h>
#include <libmount/libmount.h>
#include <libzfs.h>
#include "libzfs_impl.h"

void
libzfs_mnttab_init(libzfs_handle_t *hdl)
{
	/*
	 * XXX mtab/mounts/mountinfo
	 * XXX refresh
	 */
	mutex_init(&hdl->lz_mnttab_lock, NULL, MUTEX_DEFAULT, NULL);
}

static int
libzfs_mnttab_update(libzfs_handle_t *hdl)
{
	ASSERT(MUTEX_HELD(&hdl->lz_mnttab_lock));

	/* XXX handle errors properly with an error callback? */
	struct libmnt_table *old = hdl->lz_mnttab;
	hdl->lz_mnttab = mnt_new_table_from_file("/proc/self/mountinfo");
	if (hdl->lz_mnttab == NULL) {
		if (old == NULL)
			return (ENOMEM);
		hdl->lz_mnttab = old;
		return (ESTALE);
	}
	mnt_unref_table(old);
	return (0);
}

void
libzfs_mnttab_fini(libzfs_handle_t *hdl)
{
	mnt_unref_table(hdl->lz_mnttab);
	hdl->lz_mnttab = NULL;
	mutex_destroy(&hdl->lz_mnttab_lock);
}

void
libzfs_mnttab_cache(libzfs_handle_t *hdl, boolean_t enable)
{
	(void) hdl, (void) enable;
}

static int
libzfs_mnttab_hold(libzfs_handle_t *hdl, struct libmnt_table **mtab)
{
	mutex_enter(&hdl->lz_mnttab_lock);
	if (hdl->lz_mnttab == NULL) {
		int err = libzfs_mnttab_update(hdl);
		if (err != 0) {
			mutex_exit(&hdl->lz_mnttab_lock);
			return (err);
		}
	}
	mnt_ref_table(hdl->lz_mnttab);
	*mtab = hdl->lz_mnttab;
	mutex_exit(&hdl->lz_mnttab_lock);
	return (0);
}

static void
libzfs_mnttab_rele(struct libmnt_table *mtab)
{
	mnt_unref_table(mtab);
}

int
libzfs_mnttab_find(libzfs_handle_t *hdl, const char *fsname,
    struct mnttab *entry)
{
	struct libmnt_table *mtab;
	int err = libzfs_mnttab_hold(hdl, &mtab);
	if (err != 0)
		return (err);

	struct libmnt_fs *fs = mnt_table_find_source(mtab, fsname,
	    MNT_ITER_FORWARD);
	if (fs == NULL) {
		libzfs_mnttab_rele(mtab);
		return (ENOENT);
	}

	/* XXX assuming fs type for now */
	ASSERT0(strcmp(mnt_fs_get_fstype(fs), MNTTYPE_ZFS));

	/* XXX cast away const for now, but struct mnttab will die in the end */
	entry->mnt_special = (char *) mnt_fs_get_source(fs);
	entry->mnt_mountp = (char *) mnt_fs_get_target(fs);
	entry->mnt_fstype = (char *) mnt_fs_get_fstype(fs);
	entry->mnt_mntopts = (char *) mnt_fs_get_options(fs);

	/*
	 * XXX if this was the last ref, then the strings we just returned
	 *     are now freed. just another way this is a bad api
	 */
	libzfs_mnttab_rele(mtab);

	return (0);
}

void
libzfs_mnttab_add(libzfs_handle_t *hdl, const char *special,
    const char *mountp, const char *mntopts)
{
	struct libmnt_table *mtab;
	int err = libzfs_mnttab_hold(hdl, &mtab);
	if (err != 0)
		return;

	/*
	 * XXX if it exists, assume lost race with libzfs_mnttab_update() and
	 *     return. following existing logic for now
	 */
	if (mnt_table_find_pair(mtab, special,
	    mountp, MNT_ITER_FORWARD) != NULL) {
		libzfs_mnttab_rele(mtab);
		return;
	}

	struct libmnt_fs *fs = mnt_new_fs();
	mnt_fs_set_source(fs, special);
	mnt_fs_set_target(fs, mountp);
	mnt_fs_set_fstype(fs, MNTTYPE_ZFS);
	mnt_fs_set_options(fs, mntopts);

	mnt_table_add_fs(mtab, fs);
	mnt_unref_fs(fs);

	libzfs_mnttab_rele(mtab);
}

void
libzfs_mnttab_remove(libzfs_handle_t *hdl, const char *fsname)
{
	struct libmnt_table *mtab;
	int err = libzfs_mnttab_hold(hdl, &mtab);
	if (err != 0)
		return;

	struct libmnt_fs *fs = mnt_table_find_source(mtab, fsname,
	    MNT_ITER_FORWARD);
	if (fs == NULL) {
		libzfs_mnttab_rele(mtab);
		return;
	}

	mnt_table_remove_fs(mtab, fs);

	libzfs_mnttab_rele(mtab);
}

/*
 * Iterate over mounted children of the specified dataset
 */
static int
child_match_cb(struct libmnt_fs *cfs, void *arg)
{
	const char *name = arg;

	/* Ignore non-ZFS entries */
	if (strcmp(mnt_fs_get_fstype(cfs), MNTTYPE_ZFS) != 0)
		return (0);

	/* Ignore datasets not within the provided dataset */
	const char *source = mnt_fs_get_source(cfs);
	size_t namelen = strlen(name);
	if (strncmp(source, name, namelen) != 0 ||
	    source[namelen] != '/')
		return (0);

	/* Skip snapshot of any child dataset */
	if (strchr(source, '@') != NULL)
		return (0);

	return (1);
}

int
zfs_iter_mounted(zfs_handle_t *zhp, zfs_iter_f func, void *data)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	char mnt_prop[ZFS_MAXPROPLEN];
	zfs_handle_t *mtab_zhp;
	int err = 0, ierr = 0;

	struct libmnt_table *mtab;
	err = libzfs_mnttab_hold(hdl, &mtab);
	if (err != 0)
		return (err);

	const char *name = zfs_get_name(zhp);

	/*
	 * XXX not using mnt_table_next_child_fs() here because it only
	 *     does immediate children, so we would need to go recursive
	 */
	/*
	 * XXX weirdly, this is instantly better than the previous version
	 *     because it will find all the multimounts, not that the
	 *     changelist callback knows what the hell to do with it.
	 */
	struct libmnt_fs *fs = NULL;
	struct libmnt_iter *iter = mnt_new_iter(MNT_ITER_FORWARD);
	while (ierr == 0 && (err = mnt_table_find_next_fs(mtab, iter,
	    child_match_cb, (char *) name, &fs)) == 0) {
		if ((mtab_zhp = zfs_open(hdl, mnt_fs_get_source(fs),
		    ZFS_TYPE_FILESYSTEM)) == NULL)
			continue;

		/* Ignore legacy mounts as they are user managed */
		VERIFY0(zfs_prop_get(mtab_zhp, ZFS_PROP_MOUNTPOINT, mnt_prop,
		    sizeof (mnt_prop), NULL, NULL, 0, B_FALSE));
		if (strcmp(mnt_prop, "legacy") == 0) {
			zfs_close(mtab_zhp);
			continue;
		}

		ierr = func(mtab_zhp, data);
	}

	libzfs_mnttab_rele(mtab);

	return ((err < 0) ? -err : ierr);
}
