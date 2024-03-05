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

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <libintl.h>
#include <libuutil.h>
#include <locale.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pwd.h>
#include <zone.h>
#include <sys/wait.h>
#include <zfs_prop.h>
#include <sys/fs/zfs.h>
#include <sys/stat.h>
#include <sys/systeminfo.h>
#include <sys/fm/fs/zfs.h>
#include <sys/fm/util.h>
#include <sys/fm/protocol.h>
#include <sys/zfs_ioctl.h>
#include <sys/mount.h>
#include <sys/sysmacros.h>

#include <math.h>

#include <libzfs.h>
#include <libzutil.h>

#include "zpool.h"
#include "zpool_common.h"
#include "zpool_util.h"
#include "zfs_comutil.h"
#include "zfeature_common.h"

#include "statcommon.h"

typedef struct set_cbdata {
	char *cb_propname;
	char *cb_value;
	zfs_type_t cb_type;
	vdev_cbdata_t cb_vdevs;
	boolean_t cb_any_successful;
} set_cbdata_t;

static int
set_pool_callback(zpool_handle_t *zhp, set_cbdata_t *cb)
{
	int error;

	/* Check if we have out-of-bounds features */
	if (strcmp(cb->cb_propname, ZPOOL_CONFIG_COMPATIBILITY) == 0) {
		boolean_t features[SPA_FEATURES];
		if (zpool_do_load_compat(cb->cb_value, features) !=
		    ZPOOL_COMPATIBILITY_OK)
			return (-1);

		nvlist_t *enabled = zpool_get_features(zhp);
		spa_feature_t i;
		for (i = 0; i < SPA_FEATURES; i++) {
			const char *fguid = spa_feature_table[i].fi_guid;
			if (nvlist_exists(enabled, fguid) && !features[i])
				break;
		}
		if (i < SPA_FEATURES)
			(void) fprintf(stderr, gettext("Warning: one or "
			    "more features already enabled on pool '%s'\n"
			    "are not present in this compatibility set.\n"),
			    zpool_get_name(zhp));
	}

	/* if we're setting a feature, check it's in compatibility set */
	if (zpool_prop_feature(cb->cb_propname) &&
	    strcmp(cb->cb_value, ZFS_FEATURE_ENABLED) == 0) {
		char *fname = strchr(cb->cb_propname, '@') + 1;
		spa_feature_t f;

		if (zfeature_lookup_name(fname, &f) == 0) {
			char compat[ZFS_MAXPROPLEN];
			if (zpool_get_prop(zhp, ZPOOL_PROP_COMPATIBILITY,
			    compat, ZFS_MAXPROPLEN, NULL, B_FALSE) != 0)
				compat[0] = '\0';

			boolean_t features[SPA_FEATURES];
			if (zpool_do_load_compat(compat, features) !=
			    ZPOOL_COMPATIBILITY_OK) {
				(void) fprintf(stderr, gettext("Error: "
				    "cannot enable feature '%s' on pool '%s'\n"
				    "because the pool's 'compatibility' "
				    "property cannot be parsed.\n"),
				    fname, zpool_get_name(zhp));
				return (-1);
			}

			if (!features[f]) {
				(void) fprintf(stderr, gettext("Error: "
				    "cannot enable feature '%s' on pool '%s'\n"
				    "as it is not specified in this pool's "
				    "current compatibility set.\n"
				    "Consider setting 'compatibility' to a "
				    "less restrictive set, or to 'off'.\n"),
				    fname, zpool_get_name(zhp));
				return (-1);
			}
		}
	}

	error = zpool_set_prop(zhp, cb->cb_propname, cb->cb_value);

	return (error);
}

static int
set_callback(zpool_handle_t *zhp, void *data)
{
	int error;
	set_cbdata_t *cb = (set_cbdata_t *)data;

	if (cb->cb_type == ZFS_TYPE_VDEV) {
		error = zpool_set_vdev_prop(zhp, *cb->cb_vdevs.cb_names,
		    cb->cb_propname, cb->cb_value);
	} else {
		assert(cb->cb_type == ZFS_TYPE_POOL);
		error = set_pool_callback(zhp, cb);
	}

	cb->cb_any_successful = !error;
	return (error);
}

int
zpool_do_set(int argc, char **argv)
{
	set_cbdata_t cb = { 0 };
	int error;
	char *vdev = NULL;

	current_prop_type = ZFS_TYPE_POOL;
	if (argc > 1 && argv[1][0] == '-') {
		(void) fprintf(stderr, gettext("invalid option '%c'\n"),
		    argv[1][1]);
		zpool_usage(B_FALSE);
	}

	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing property=value "
		    "argument\n"));
		zpool_usage(B_FALSE);
	}

	if (argc < 3) {
		(void) fprintf(stderr, gettext("missing pool name\n"));
		zpool_usage(B_FALSE);
	}

	if (argc > 4) {
		(void) fprintf(stderr, gettext("too many pool names\n"));
		zpool_usage(B_FALSE);
	}

	cb.cb_propname = argv[1];
	cb.cb_type = ZFS_TYPE_POOL;
	cb.cb_vdevs.cb_name_flags |= VDEV_NAME_TYPE_ID;
	cb.cb_value = strchr(cb.cb_propname, '=');
	if (cb.cb_value == NULL) {
		(void) fprintf(stderr, gettext("missing value in "
		    "property=value argument\n"));
		zpool_usage(B_FALSE);
	}

	*(cb.cb_value) = '\0';
	cb.cb_value++;
	argc -= 2;
	argv += 2;

	/* argv[0] is pool name */
	if (!is_pool(argv[0])) {
		(void) fprintf(stderr,
		    gettext("cannot open '%s': is not a pool\n"), argv[0]);
		return (EINVAL);
	}

	/* argv[1], when supplied, is vdev name */
	if (argc == 2) {

		if (strcmp(argv[1], "root") == 0)
			vdev = strdup("root-0");
		else
			vdev = strdup(argv[1]);

		if (!are_vdevs_in_pool(1, &vdev, argv[0], &cb.cb_vdevs)) {
			(void) fprintf(stderr, gettext(
			    "cannot find '%s' in '%s': device not in pool\n"),
			    vdev, argv[0]);
			free(vdev);
			return (EINVAL);
		}
		cb.cb_vdevs.cb_names = &vdev;
		cb.cb_vdevs.cb_names_count = 1;
		cb.cb_type = ZFS_TYPE_VDEV;
	}

	error = for_each_pool(1, argv, B_TRUE, NULL, ZFS_TYPE_POOL,
	    B_FALSE, set_callback, &cb);

	if (vdev != NULL)
		free(vdev);

	return (error);
}
