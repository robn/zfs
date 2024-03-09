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
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 * Copyright 2012 Milan Jurik. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * Copyright (c) 2013 Steven Hartland.  All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>.
 * Copyright 2016 Nexenta Systems, Inc.
 * Copyright (c) 2019 Datto Inc.
 * Copyright (c) 2019, loli10K <ezomori.nozomu@gmail.com>
 * Copyright 2019 Joyent, Inc.
 * Copyright (c) 2019, 2020 by Christian Schwarz. All rights reserved.
 */

#ifndef	ZFS_H
#define	ZFS_H

#ifdef	__cplusplus
extern "C" {
#endif

int zfs_do_clone(int, char **);
int zfs_do_create(int, char **);
int zfs_do_destroy(int, char **);
int zfs_do_get(int, char **);
int zfs_do_inherit(int, char **);
int zfs_do_list(int, char **);
int zfs_do_mount(int, char **);
int zfs_do_rename(int, char **);
int zfs_do_rollback(int, char **);
int zfs_do_set(int, char **);
int zfs_do_upgrade(int, char **);
int zfs_do_snapshot(int, char **);
int zfs_do_unmount(int, char **);
int zfs_do_share(int, char **);
int zfs_do_unshare(int, char **);
int zfs_do_send(int, char **);
int zfs_do_receive(int, char **);
int zfs_do_promote(int, char **);
int zfs_do_userspace(int, char **);
int zfs_do_allow(int, char **);
int zfs_do_unallow(int, char **);
int zfs_do_hold(int, char **);
int zfs_do_holds(int, char **);
int zfs_do_release(int, char **);
int zfs_do_diff(int, char **);
int zfs_do_bookmark(int, char **);
int zfs_do_channel_program(int, char **);
int zfs_do_load_key(int, char **);
int zfs_do_unload_key(int, char **);
int zfs_do_change_key(int, char **);
int zfs_do_project(int, char **);
int zfs_do_redact(int, char **);
int zfs_do_wait(int, char **);

#ifdef __FreeBSD__
int zfs_do_jail(int, char **);
int zfs_do_unjail(int, char **);
#endif

#ifdef __linux__
int zfs_do_zone(int, char **);
int zfs_do_unzone(int, char **);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* ZPOOL_H */
