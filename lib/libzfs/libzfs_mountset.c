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

#include <sys/mutex.h>
#include <sys/avl.h>
#include <libzfs.h>
#include "libzfs_impl.h"

void
libzfs_mountset_init(libzfs_handle_t *hdl)
{
	if (hdl->zh_mountset == NULL)
		hdl->zh_mountset = zfs_mountset_open();
}

void
libzfs_mountset_fini(libzfs_handle_t *hdl)
{
	if (hdl->zh_mountset != NULL) {
		zfs_mountset_close(hdl->zh_mountset);
		hdl->zh_mountset = NULL;
	}
}

zfs_mountset_t *
libzfs_mountset_enter(libzfs_handle_t *hdl)
{
	zfs_mountset_enter(hdl->zh_mountset);
	return (hdl->zh_mountset);
}

int
libzfs_mountset_apply(libzfs_handle_t *hdl, zfs_mountbuilder_t *mb)
{
	return (zfs_mountset_apply(hdl->zh_mountset, mb));
}

static zfs_mountbuilder_t *
zfs_mountbuilder_alloc(void)
{
	zfs_mountbuilder_t *mb = calloc(1, sizeof (zfs_mountbuilder_t));
	return (mb);
}

void
zfs_mountbuilder_free(zfs_mountbuilder_t *mb)
{
	if (mb->mb_raw_opts != NULL) {
		for (uint_t i = 0; i < mb->mb_raw_count; i++)
			free(mb->mb_raw_opts[i]);
		free(mb->mb_raw_opts);
	}

	if (mb->mb_op == ZFS_MOUNTBUILDER_OP_MOUNT) {
		free(mb->mb_dataset);
		free(mb->mb_mountpoint);
	} else {
		zfs_mount_drop(mb->mb_mount);
	}

	free(mb);
}

zfs_mountbuilder_t *
zfs_mountbuilder_for_mount(const char *dataset, const char *mountpoint,
    uint32_t flags)
{
	if (flags != 0)
		return (NULL);

	zfs_mountbuilder_t *mb = zfs_mountbuilder_alloc();
	mb->mb_op = ZFS_MOUNTBUILDER_OP_MOUNT;
	mb->mb_dataset = strdup(dataset);
	mb->mb_mountpoint = strdup(mountpoint);
	return (mb);
}

zfs_mountbuilder_t *
zfs_mountbuilder_for_unmount(zfs_mount_t *mnt, uint32_t flags)
{
	if ((flags & ~ZFS_MOUNTBUILDER_UNMOUNT_DETACH) != 0)
		return (NULL);

	zfs_mountbuilder_t *mb = zfs_mountbuilder_alloc();
	mb->mb_op = ZFS_MOUNTBUILDER_OP_UNMOUNT;
	mb->mb_op_flags = flags;
	mb->mb_mount = mnt;
	zfs_mount_hold(mb->mb_mount);
	return (mb);
}

zfs_mountbuilder_t *
zfs_mountbuilder_for_remount(zfs_mount_t *mnt, uint32_t flags)
{
	if (flags != 0)
		return (NULL);

	zfs_mountbuilder_t *mb = zfs_mountbuilder_alloc();
	mb->mb_op = ZFS_MOUNTBUILDER_OP_REMOUNT;
	mb->mb_mount = mnt;
	zfs_mount_hold(mb->mb_mount);
	return (mb);
}

void
zfs_mountbuilder_set_readonly(zfs_mountbuilder_t *mb, bool set)
{
	mb->mb_opt_changed_readonly = ZFS_MOUNTOPT_SET;
	mb->mb_opt_val_readonly = set;
}

void
zfs_mountbuilder_set_exec(zfs_mountbuilder_t *mb, bool set)
{
	mb->mb_opt_changed_exec = ZFS_MOUNTOPT_SET;
	mb->mb_opt_val_exec = set;
}

void
zfs_mountbuilder_set_setuid(zfs_mountbuilder_t *mb, bool set)
{
	mb->mb_opt_changed_setuid = ZFS_MOUNTOPT_SET;
	mb->mb_opt_val_setuid = set;
}

void
zfs_mountbuilder_set_devices(zfs_mountbuilder_t *mb, bool set)
{
	mb->mb_opt_changed_devices = ZFS_MOUNTOPT_SET;
	mb->mb_opt_val_devices = set;
}

void
zfs_mountbuilder_set_mand(zfs_mountbuilder_t *mb, bool set)
{
	mb->mb_opt_changed_mand = ZFS_MOUNTOPT_SET;
	mb->mb_opt_val_mand = set;
}

void
zfs_mountbuilder_set_atime(zfs_mountbuilder_t *mb, zfs_mountopt_atime_t set)
{
	mb->mb_opt_changed_atime = ZFS_MOUNTOPT_SET;
	mb->mb_opt_val_atime = set;
}

void
zfs_mountbuilder_set_xattr(zfs_mountbuilder_t *mb, zfs_mountopt_xattr_t set)
{
	mb->mb_opt_changed_xattr = ZFS_MOUNTOPT_SET;
	mb->mb_opt_val_xattr = set;
}

void
zfs_mountbuilder_default_readonly(zfs_mountbuilder_t *mb)
{
	mb->mb_opt_changed_readonly = ZFS_MOUNTOPT_DEFAULT;
}

void
zfs_mountbuilder_default_exec(zfs_mountbuilder_t *mb)
{
	mb->mb_opt_changed_exec = ZFS_MOUNTOPT_DEFAULT;
}

void
zfs_mountbuilder_default_setuid(zfs_mountbuilder_t *mb)
{
	mb->mb_opt_changed_setuid = ZFS_MOUNTOPT_DEFAULT;
}

void
zfs_mountbuilder_default_devices(zfs_mountbuilder_t *mb)
{
	mb->mb_opt_changed_devices = ZFS_MOUNTOPT_DEFAULT;
}

void
zfs_mountbuilder_default_mand(zfs_mountbuilder_t *mb)
{
	mb->mb_opt_changed_mand = ZFS_MOUNTOPT_DEFAULT;
}

void
zfs_mountbuilder_default_atime(zfs_mountbuilder_t *mb)
{
	mb->mb_opt_changed_atime = ZFS_MOUNTOPT_DEFAULT;
}

void
zfs_mountbuilder_default_xattr(zfs_mountbuilder_t *mb)
{
	mb->mb_opt_changed_xattr = ZFS_MOUNTOPT_DEFAULT;
}

void
zfs_mountbuilder_raw_option(zfs_mountbuilder_t *mb, const char *opt)
{
	if (opt == NULL)
		return;

	/* XXX temp drop "zfsutil" until I figure out where I am */
	if (strcmp(opt, "zfsutil") == 0)
		return;

	if (mb->mb_raw_opts == NULL) {
		mb->mb_raw_alloc = 8;
		mb->mb_raw_opts = malloc(sizeof (char *) * mb->mb_raw_alloc);
	} else if (mb->mb_raw_count == mb->mb_raw_alloc) {
		mb->mb_raw_alloc *= 2;
		mb->mb_raw_opts = realloc(mb->mb_raw_opts,
		    sizeof (char *) * mb->mb_raw_alloc);
	}

	fprintf(stderr, "zfs_mountbuilder_raw_opt: %s\n", opt);

	mb->mb_raw_opts[mb->mb_raw_count++] = strdup(opt);
}
