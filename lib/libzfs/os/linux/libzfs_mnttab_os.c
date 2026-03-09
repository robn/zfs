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
#include "../../libzfs_impl.h"

/* === zfs_mnttab_t: the entire table of mounts === */

zfs_mnttab_t *
zfs_mnttab_load(void)
{
	return ((zfs_mnttab_t *)
	    mnt_new_table_from_file("/proc/self/mountinfo"));
}

void
zfs_mnttab_addref(zfs_mnttab_t *mtab)
{
	mnt_ref_table((struct libmnt_table *) mtab);
}

void
zfs_mnttab_delref(zfs_mnttab_t *mtab)
{
	mnt_unref_table((struct libmnt_table *) mtab);
}

/* === zfs_mntent_t: a single mounted entity === */

void
zfs_mntent_addref(zfs_mntent_t *ment)
{
	mnt_ref_fs((struct libmnt_fs *) ment);
}

void
zfs_mntent_delref(zfs_mntent_t *ment)
{
	mnt_unref_fs((struct libmnt_fs *) ment);
}

const char *
zfs_mntent_get_dataset(zfs_mntent_t *ment)
{
	return (mnt_fs_get_source((struct libmnt_fs *) ment));
}

const char *
zfs_mntent_get_mountpoint(zfs_mntent_t *ment)
{
	return (mnt_fs_get_target((struct libmnt_fs *) ment));
}

/* === lookup single zfs_mntent_t in a zfs_mnttab_t === */

int
zfs_mnttab_find_dataset(zfs_mnttab_t *mtab, const char *dsname,
    zfs_mntent_t **ment)
{
	struct libmnt_fs *fs = mnt_table_find_source(
	    (struct libmnt_table *) mtab, dsname, MNT_ITER_FORWARD);
	if (fs == NULL)
		return (ENOENT);

	/* XXX assuming fs type for now */
	ASSERT0(strcmp(mnt_fs_get_fstype(fs), MNTTYPE_ZFS));

	*ment = (zfs_mntent_t *) fs;
	return (0);
}

int
zfs_mnttab_find_mountpoint(zfs_mnttab_t *mtab, const char *mountpoint,
    zfs_mntent_t **ment)
{
	struct libmnt_fs *fs = mnt_table_find_target(
	    (struct libmnt_table *) mtab, mountpoint, MNT_ITER_FORWARD);
	if (fs == NULL)
		return (ENOENT);

	/* XXX assuming fs type for now */
	ASSERT0(strcmp(mnt_fs_get_fstype(fs), MNTTYPE_ZFS));

	*ment = (zfs_mntent_t *) fs;
	return (0);
}

int
zfs_mnttab_find_pair(zfs_mnttab_t *mtab, const char *dsname,
    const char *mountpoint, zfs_mntent_t **ment)
{
	struct libmnt_fs *fs = mnt_table_find_pair(
	    (struct libmnt_table *) mtab, dsname, mountpoint,
	    MNT_ITER_FORWARD);
	if (fs == NULL)
		return (ENOENT);

	/* XXX assuming fs type for now */
	ASSERT0(strcmp(mnt_fs_get_fstype(fs), MNTTYPE_ZFS));

	*ment = (zfs_mntent_t *) fs;
	return (0);
}
