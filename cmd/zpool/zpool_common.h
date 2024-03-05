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

#ifndef	ZPOOL_COMMON_H
#define	ZPOOL_COMMON_H

#include "zpool_util.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	VDEV_ALLOC_CLASS_LOGS	"logs"

extern char history_str[HIS_MAX_RECORD_LEN];
extern boolean_t log_history;

extern zfs_type_t current_prop_type;

extern uint_t timestamp_fmt;

void print_cmd_columns(vdev_cmd_data_list_t *vcdl, int use_dashes);
void print_zpool_script_list(const char *subcommand);

void print_vdev_tree(zpool_handle_t *zhp, const char *name, nvlist_t *nv,
    int indent, const char *match, int name_flags);

int add_prop_list(const char *propname, const char *propval, nvlist_t **props,
    boolean_t poolprop);
int add_prop_list_default(const char *propname, const char *propval,
    nvlist_t **props);

int zpool_power_on_and_disk_wait(zpool_handle_t *zhp, char *vdev);
int zpool_power_on_pool_and_wait_for_devices(zpool_handle_t *zhp);
int zpool_power_off(zpool_handle_t *zhp, char *vdev);

zpool_compat_status_t zpool_do_load_compat(const char *compat, boolean_t *list);

int are_vdevs_in_pool(int argc, char **argv, char *pool_name,
    vdev_cbdata_t *cb);
int is_pool(char *name);
int are_all_pools(int argc, char **argv);

void error_list_unresolved_vdevs(int argc, char **argv, char *pool_name,
    vdev_cbdata_t *cb);

typedef struct status_cbdata {
	int		cb_count;
	int		cb_name_flags;
	int		cb_namewidth;
	boolean_t	cb_allpools;
	boolean_t	cb_verbose;
	boolean_t	cb_literal;
	boolean_t	cb_explain;
	boolean_t	cb_first;
	boolean_t	cb_dedup_stats;
	boolean_t	cb_print_unhealthy;
	boolean_t	cb_print_status;
	boolean_t	cb_print_slow_ios;
	boolean_t	cb_print_vdev_init;
	boolean_t	cb_print_vdev_trim;
	vdev_cmd_data_list_t	*vcdl;
	boolean_t	cb_print_power;
} status_cbdata_t;

int max_width(zpool_handle_t *zhp, nvlist_t *nv, int depth, int max,
    int name_flags);

const char *health_str_to_color(const char *health);
boolean_t is_blank_str(const char *str);
void zpool_print_cmd(vdev_cmd_data_list_t *vcdl, const char *pool,
    const char *path);

void print_status_config(zpool_handle_t *zhp, status_cbdata_t *cb,
    const char *name, nvlist_t *nv, int depth, boolean_t isspare,
    vdev_rebuild_stat_t *vrs);
void print_import_config(status_cbdata_t *cb, const char *name, nvlist_t *nv,
    int depth);

void print_class_vdevs(zpool_handle_t *zhp, status_cbdata_t *cb, nvlist_t *nv,
    const char *class);

void zpool_collect_leaves(zpool_handle_t *zhp, nvlist_t *nvroot, nvlist_t *res);

int get_namewidth(zpool_handle_t *zhp, int min_width, int flags,
    boolean_t verbose);
void get_interval_count(int *argcp, char **argv, float *iv, unsigned long *cnt);
void get_timestamp_arg(char c);

int terminal_height(void);

boolean_t check_rebuilding(nvlist_t *nvroot, uint64_t *rebuild_end_time);

#ifdef	__cplusplus
}
#endif

#endif	/* ZPOOL_H */
