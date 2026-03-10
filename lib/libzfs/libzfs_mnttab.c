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

static uint_t mnttab_last_entry_key;

static void
mnttab_last_entry_free(void *arg)
{
	struct mnttab *ent = arg;
	if (ent == NULL)
		return;
	free(ent->mnt_special);
	free(ent->mnt_mountp);
	free(ent->mnt_fstype);
	free(ent->mnt_mntopts);
	free(ent);
}

void
libzfs_mnttab_init(libzfs_handle_t *hdl)
{
	libzfs_mountset_init(hdl);
	tsd_create(&mnttab_last_entry_key, mnttab_last_entry_free);
}

void
libzfs_mnttab_cache(libzfs_handle_t *hdl, boolean_t enable)
{
	/* This is a no-op to preserve API backward compatibility. */
	(void) hdl, (void) enable;
}

void
libzfs_mnttab_fini(libzfs_handle_t *hdl)
{
	libzfs_mountset_fini(hdl);
	tsd_destroy(&mnttab_last_entry_key);
}

int
libzfs_mnttab_find(libzfs_handle_t *hdl, const char *fsname,
    struct mnttab *entry)
{
	zfs_mountset_t *mset = libzfs_mountset_enter(hdl);

	zfs_mount_t *mnt;
	int err = zfs_mountset_find_dataset(mset, fsname, &mnt);
	if (err != 0) {
		zfs_mountset_exit(mset);
		return (err);
	}

	/*
	 * The caller is expecting us to fill in the provided struct mnttab
	 * with four string pointers, but there is no provision for freeing or
	 * otherwise releasing those strings. This function is also expected to
	 * be thread-safe. So we are faced with either holding (leaking) memory
	 * "forever", or doing something to keep the returned strings alive for
	 * "long enough".
	 *
	 * Typically, callers will immediately compare or copy the pieces
	 * they're interested in, and not hold onto the entry for very long
	 * (*entry is usually on the stack). So, its probably reasonable to
	 * keep these values alive until either the next call to
	 * libzfs_mnttab_find(), or until the thread exits.
	 *
	 * So, we create out own struct mnttab, copy everything of interest
	 * into it, and retain it in a thread-specific data key with a cleanup
	 * function (see libzfs_mnttab_init() and mnttab_last_entry_free()).
	 * So long as the caller doesn't try to hold onto or accumulate
	 * returned entries, this should be safe.
	 */
	struct mnttab *tent = tsd_get(mnttab_last_entry_key);
	if (tent != NULL)
		mnttab_last_entry_free(tent);
	tent = calloc(1, sizeof (struct mnttab));
	tent->mnt_special = strdup(zfs_mount_get_dataset(mnt));
	tent->mnt_mountp = strdup(zfs_mount_get_mountpoint(mnt));
	tent->mnt_fstype = strdup(MNTTYPE_ZFS); /* XXX asserted in backend for now */
	tent->mnt_mntopts = strdup(zfs_mount_get_options(mnt));
	tsd_set(mnttab_last_entry_key, tent);

	memcpy(entry, tent, sizeof (struct mnttab));

	zfs_mountset_exit(mset);

	return (0);
}

/*
 * libzfs_mnttab_add() and libzfs_mnttab_remove() should have only ever been
 * internal libzfs functions, but for some reason were exported, so for now
 * we're stuck with them. They were called to update the table when a dataset
 * was mounted or unmounted.
 *
 * To keep them working, we simply refresh the table from the source. Any other
 * outstanding table refs will not be updated, but anything still using this
 * API has no knowledge of table refs, so there won't be any.
 */
void
libzfs_mnttab_add(libzfs_handle_t *hdl, const char *special,
    const char *mountp, const char *mntopts)
{
	(void) special, (void) mountp, (void) mntopts;

	zfs_mountset_refresh(hdl->zh_mountset);
}

void
libzfs_mnttab_remove(libzfs_handle_t *hdl, const char *fsname)
{
	(void) fsname;
	zfs_mountset_refresh(hdl->zh_mountset);
}
