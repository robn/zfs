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
 * Copyright (c) 2026, TrueNAS.
 */

#include <sys/mntent.h>
#include <sys/mutex.h>
#include <libzfs.h>
#include "libzfs_impl.h"

void
libzfs_mnttab_init(libzfs_handle_t *hdl)
{
	/*
	 * XXX mtab/mounts/mountinfo
	 * XXX refresh
	 */
	hdl->zh_mnttab = NULL;
	mutex_init(&hdl->zh_mnttab_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
libzfs_mnttab_cache(libzfs_handle_t *hdl, boolean_t enable)
{
	/* This is a no-op to preserve ABI backward compatibility. */
	(void) hdl, (void) enable;
}

void
libzfs_mnttab_fini(libzfs_handle_t *hdl)
{
	zfs_mnttab_delref(hdl->zh_mnttab);
	hdl->zh_mnttab = NULL;
	mutex_destroy(&hdl->zh_mnttab_lock);
}

static int
mnttab_update(libzfs_handle_t *hdl)
{
	ASSERT(MUTEX_HELD(&hdl->zh_mnttab_lock));

	/* XXX handle errors properly with an error callback? */
	zfs_mnttab_t *old = hdl->zh_mnttab;
	hdl->zh_mnttab = zfs_mnttab_load();
	if (hdl->zh_mnttab == NULL) {
		if (old == NULL)
			return (ENOMEM);
		hdl->zh_mnttab = old;
		return (ESTALE);
	}

	zfs_mnttab_delref(old);
	return (0);
}

static int
libzfs_mnttab_hold(libzfs_handle_t *hdl, zfs_mnttab_t **mtab)
{
	mutex_enter(&hdl->zh_mnttab_lock);
	if (hdl->zh_mnttab == NULL) {
		int err = mnttab_update(hdl);
		if (err != 0) {
			mutex_exit(&hdl->zh_mnttab_lock);
			return (err);
		}
	}
	zfs_mnttab_addref(hdl->zh_mnttab);
	*mtab = hdl->zh_mnttab;
	mutex_exit(&hdl->zh_mnttab_lock);
	return (0);
}

int
libzfs_mnttab_find(libzfs_handle_t *hdl, const char *fsname,
    struct mnttab *entry)
{
	zfs_mnttab_t *mtab;
	int err = libzfs_mnttab_hold(hdl, &mtab);
	if (err != 0)
		return (err);

	zfs_mntent_t *ment;
	err = zfs_mnttab_find_dataset(mtab, fsname, &ment);
	if (err != 0) {
		zfs_mnttab_delref(mtab);
		return (err);
	}

	/*
	 * XXX the whole problem is that its assumed that *entry is a pointer
	 *     to some immutable thing that lives basically forever, but
	 *     we're flipping that. so there's no free call.
	 *
	 *     probably the way to maintain this interface is to keep the
	 *     "last" entry in the libzfs_handle, and deallocate it each time
	 *     through find, and at shutdown. but, then its not reentrent or
	 *     anything
	 *
	 *     secondary question is what does mntopts mean here. we _need_
	 *     some kind of mntopts, but not stringy ones, I'll wager
	 *     this is all only a mess because this API is public. I'll see
	 *     how everything falls out before deciding what to do here.
	 *       -- robn, 2026-03-09
	 */
	entry->mnt_special = strdup(zfs_mntent_get_dataset(ment));
	entry->mnt_mountp = strdup(zfs_mntent_get_mountpoint(ment));
	entry->mnt_fstype = (char *) MNTTYPE_ZFS;
	entry->mnt_mntopts = NULL;

	zfs_mntent_delref(ment);
	zfs_mnttab_delref(mtab);

	return (0);
}

void
libzfs_mnttab_add(libzfs_handle_t *hdl, const char *special,
    const char *mountp, const char *mntopts)
{
	(void) hdl, (void) special, (void) mountp, (void) mntopts;

#if 0
	zfs_mnttab_t *mtab;
	int err = libzfs_mnttab_hold(hdl, &mtab);
	if (err != 0)
		return;

	/*
	 * Another thread may have already added this entry
	 * via mnttab_update. If so we should skip it.
	 */
	if (mnt_table_find_pair(mtab, special,
	    mountp, MNT_ITER_FORWARD) != NULL) {
		zfs_mnttab_rele_os(mtab);
		return;
	}

	struct libmnt_fs *fs = mnt_new_fs();
	mnt_fs_set_source(fs, special);
	mnt_fs_set_target(fs, mountp);
	mnt_fs_set_fstype(fs, MNTTYPE_ZFS);
	mnt_fs_set_options(fs, mntopts);

	mnt_table_add_fs(mtab, fs);
	mnt_unref_fs(fs);

	zfs_mnttab_rele_os(mtab);
#endif
}

void
libzfs_mnttab_remove(libzfs_handle_t *hdl, const char *fsname)
{
	(void) hdl, (void) fsname;
#if 0
	struct libmnt_table *mtab;
	int err = libzfs_mnttab_hold(hdl, &mtab);
	if (err != 0)
		return;

	struct libmnt_fs *fs = mnt_table_find_source(mtab, fsname,
	    MNT_ITER_FORWARD);
	if (fs == NULL) {
		zfs_mnttab_rele_os(mtab);
		return;
	}

	mnt_table_remove_fs(mtab, fs);

	zfs_mnttab_rele_os(mtab);
#endif
}
