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
#include <sys/mutex.h>
#include <libzfs.h>
#include "libzfs_impl.h"

typedef struct mnttab_node {
	struct mnttab mtn_mt;
	avl_node_t mtn_node;
} mnttab_node_t;

static mnttab_node_t *
mnttab_node_alloc(libzfs_handle_t *hdl, const char *special,
    const char *mountp, const char *mntopts)
{
	mnttab_node_t *mtn = zfs_alloc(hdl, sizeof (mnttab_node_t));
	mtn->mtn_mt.mnt_special = zfs_strdup(hdl, special);
	mtn->mtn_mt.mnt_mountp = zfs_strdup(hdl, mountp);
	mtn->mtn_mt.mnt_fstype = (char *)MNTTYPE_ZFS;
	mtn->mtn_mt.mnt_mntopts = zfs_strdup(hdl, mntopts);
	return (mtn);
}

static void
mnttab_node_free(libzfs_handle_t *hdl, mnttab_node_t *mtn)
{
	(void) hdl;
	free(mtn->mtn_mt.mnt_special);
	free(mtn->mtn_mt.mnt_mountp);
	free(mtn->mtn_mt.mnt_mntopts);
	free(mtn);
}

static int
mnttab_cache_compare(const void *arg1, const void *arg2)
{
	const mnttab_node_t *mtn1 = (const mnttab_node_t *)arg1;
	const mnttab_node_t *mtn2 = (const mnttab_node_t *)arg2;
	int rv;

	rv = strcmp(mtn1->mtn_mt.mnt_special, mtn2->mtn_mt.mnt_special);

	return (TREE_ISIGN(rv));
}

void
libzfs_mnttab_init(libzfs_handle_t *hdl)
{
	mutex_init(&hdl->mnttab_cache_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&hdl->mnttab_cache_cv, NULL, CV_DEFAULT, NULL);
	ASSERT0(avl_numnodes(&hdl->mnttab_cache));
	avl_create(&hdl->mnttab_cache, mnttab_cache_compare,
	    sizeof (mnttab_node_t), offsetof(mnttab_node_t, mtn_node));
	hdl->mnttab_cache_loaded = B_FALSE;
	hdl->mnttab_cache_holds = 0;
}

void
libzfs_mnttab_fini(libzfs_handle_t *hdl)
{
	void *cookie = NULL;
	mnttab_node_t *mtn;

	ASSERT0(hdl->mnttab_cache_holds);

	while ((mtn = avl_destroy_nodes(&hdl->mnttab_cache, &cookie))
	    != NULL)
		mnttab_node_free(hdl, mtn);
	hdl->mnttab_cache_loaded = B_FALSE;

	avl_destroy(&hdl->mnttab_cache);

	cv_destroy(&hdl->mnttab_cache_cv);
	(void) mutex_destroy(&hdl->mnttab_cache_lock);
}

void
libzfs_mnttab_cache(libzfs_handle_t *hdl, boolean_t enable)
{
	/* This is a no-op to preserve ABI backward compatibility. */
	(void) hdl, (void) enable;
}

static int
mnttab_update(libzfs_handle_t *hdl)
{
	FILE *mnttab;
	struct mnttab entry;

	ASSERT(MUTEX_HELD(&hdl->mnttab_cache_lock));
	ASSERT(!hdl->mnttab_cache_loaded);

	if ((mnttab = fopen(MNTTAB, "re")) == NULL)
		return (ENOENT);

	while (getmntent(mnttab, &entry) == 0) {
		mnttab_node_t *mtn;
		avl_index_t where;

		if (strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0)
			continue;

		mtn = mnttab_node_alloc(hdl, entry.mnt_special,
		    entry.mnt_mountp, entry.mnt_mntopts);

		/* Exclude duplicate mounts */
		if (avl_find(&hdl->mnttab_cache, mtn, &where) != NULL) {
			mnttab_node_free(hdl, mtn);
			continue;
		}

		avl_add(&hdl->mnttab_cache, mtn);
	}

	(void) fclose(mnttab);

	hdl->mnttab_cache_loaded = B_TRUE;

	return (0);
}

static void
mnttab_enter(libzfs_handle_t *hdl)
{
	ASSERT(!MUTEX_HELD(&hdl->mnttab_cache_lock));
	mutex_enter(&hdl->mnttab_cache_lock);

	if (hdl->mnttab_cache_holds == 0 &&
	    !hdl->mnttab_cache_loaded) {
		mnttab_update(hdl);
	}

	hdl->mnttab_cache_holds++;

	mutex_exit(&hdl->mnttab_cache_lock);
}

static void
mnttab_exit(libzfs_handle_t *hdl)
{
	ASSERT(!MUTEX_HELD(&hdl->mnttab_cache_lock));
	mutex_enter(&hdl->mnttab_cache_lock);
	if (--hdl->mnttab_cache_holds == 0)
		cv_broadcast(&hdl->mnttab_cache_cv);
	mutex_exit(&hdl->mnttab_cache_lock);
}

static void
mnttab_enter_update(libzfs_handle_t *hdl)
{
	ASSERT(!MUTEX_HELD(&hdl->mnttab_cache_lock));
	mutex_enter(&hdl->mnttab_cache_lock);
	while (hdl->mnttab_cache_holds != 0)
		cv_wait(&hdl->mnttab_cache_cv, &hdl->mnttab_cache_lock);
	if (!hdl->mnttab_cache_loaded)
		mnttab_update(hdl);
	ASSERT(MUTEX_HELD(&hdl->mnttab_cache_lock));
}

static void
mnttab_exit_update(libzfs_handle_t *hdl)
{
	ASSERT(MUTEX_HELD(&hdl->mnttab_cache_lock));
	cv_broadcast(&hdl->mnttab_cache_cv);
	mutex_exit(&hdl->mnttab_cache_lock);
}

void
libzfs_mnttab_add(libzfs_handle_t *hdl, const char *special,
    const char *mountp, const char *mntopts)
{
	mnttab_node_t *mtn;

	mnttab_enter_update(hdl);

	mtn = mnttab_node_alloc(hdl, special, mountp, mntopts);

	/*
	 * Another thread may have already added this entry
	 * via mnttab_update. If so we should skip it.
	 */
	if (avl_find(&hdl->mnttab_cache, mtn, NULL) != NULL)
		mnttab_node_free(hdl, mtn);
	else
		avl_add(&hdl->mnttab_cache, mtn);

	mnttab_exit_update(hdl);
}

void
libzfs_mnttab_remove(libzfs_handle_t *hdl, const char *fsname)
{
	mnttab_node_t find;
	mnttab_node_t *ret;

	mnttab_enter_update(hdl);

	find.mtn_mt.mnt_special = (char *)fsname;
	if ((ret = avl_find(&hdl->mnttab_cache, (void *)&find, NULL)) != NULL) {
		avl_remove(&hdl->mnttab_cache, ret);
		mnttab_node_free(hdl, ret);
	}

	mnttab_exit_update(hdl);
}

int
libzfs_mnttab_find(libzfs_handle_t *hdl, const char *fsname,
    struct mnttab *entry)
{
	mnttab_node_t find;
	mnttab_node_t *mtn;
	int ret = ENOENT;

	mnttab_enter(hdl);

	find.mtn_mt.mnt_special = (char *)fsname;
	mtn = avl_find(&hdl->mnttab_cache, &find, NULL);
	if (mtn) {
		*entry = mtn->mtn_mt;
		ret = 0;
	}

	mnttab_exit(hdl);

	return (ret);
}

