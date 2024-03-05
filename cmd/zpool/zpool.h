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
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2011, 2020 by Delphix. All rights reserved.
 * Copyright (c) 2012 by Frederik Wessels. All rights reserved.
 * Copyright (c) 2012 by Cyril Plisko. All rights reserved.
 * Copyright (c) 2013 by Prasad Joshi (sTec). All rights reserved.
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>.
 * Copyright (c) 2017 Datto Inc.
 * Copyright (c) 2017 Open-E, Inc. All Rights Reserved.
 * Copyright (c) 2017, Intel Corporation.
 * Copyright (c) 2019, loli10K <ezomori.nozomu@gmail.com>
 * Copyright (c) 2021, Colm Buckley <colm@tuatha.org>
 * Copyright (c) 2021, Klara Inc.
 * Copyright [2021] Hewlett Packard Enterprise Development LP
 */

#ifndef	ZPOOL_H
#define	ZPOOL_H

#ifdef	__cplusplus
extern "C" {
#endif

int zpool_do_create(int, char **);
int zpool_do_destroy(int, char **);

int zpool_do_add(int, char **);
int zpool_do_remove(int, char **);
int zpool_do_labelclear(int, char **);

int zpool_do_checkpoint(int, char **);

int zpool_do_list(int, char **);
int zpool_do_iostat(int, char **);
int zpool_do_status(int, char **);

int zpool_do_online(int, char **);
int zpool_do_offline(int, char **);
int zpool_do_clear(int, char **);
int zpool_do_reopen(int, char **);

int zpool_do_reguid(int, char **);

int zpool_do_attach(int, char **);
int zpool_do_detach(int, char **);
int zpool_do_replace(int, char **);
int zpool_do_split(int, char **);

int zpool_do_initialize(int, char **);
int zpool_do_scrub(int, char **);
int zpool_do_resilver(int, char **);
int zpool_do_trim(int, char **);

int zpool_do_import(int, char **);
int zpool_do_export(int, char **);

int zpool_do_upgrade(int, char **);

int zpool_do_history(int, char **);
int zpool_do_events(int, char **);

int zpool_do_get(int, char **);
int zpool_do_set(int, char **);

int zpool_do_sync(int, char **);

int zpool_do_wait(int, char **);

__attribute__((noreturn)) void zpool_usage(boolean_t requested);

#ifdef	__cplusplus
}
#endif

#endif	/* ZPOOL_H */
