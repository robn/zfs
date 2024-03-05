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

typedef struct scrub_cbdata {
	int	cb_type;
	pool_scrub_cmd_t cb_scrub_cmd;
} scrub_cbdata_t;

static boolean_t
zpool_has_checkpoint(zpool_handle_t *zhp)
{
	nvlist_t *config, *nvroot;

	config = zpool_get_config(zhp, NULL);

	if (config != NULL) {
		pool_checkpoint_stat_t *pcs = NULL;
		uint_t c;

		nvroot = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE);
		(void) nvlist_lookup_uint64_array(nvroot,
		    ZPOOL_CONFIG_CHECKPOINT_STATS, (uint64_t **)&pcs, &c);

		if (pcs == NULL || pcs->pcs_state == CS_NONE)
			return (B_FALSE);

		assert(pcs->pcs_state == CS_CHECKPOINT_EXISTS ||
		    pcs->pcs_state == CS_CHECKPOINT_DISCARDING);
		return (B_TRUE);
	}

	return (B_FALSE);
}

static int
scrub_callback(zpool_handle_t *zhp, void *data)
{
	scrub_cbdata_t *cb = data;
	int err;

	/*
	 * Ignore faulted pools.
	 */
	if (zpool_get_state(zhp) == POOL_STATE_UNAVAIL) {
		(void) fprintf(stderr, gettext("cannot scan '%s': pool is "
		    "currently unavailable\n"), zpool_get_name(zhp));
		return (1);
	}

	err = zpool_scan(zhp, cb->cb_type, cb->cb_scrub_cmd);

	if (err == 0 && zpool_has_checkpoint(zhp) &&
	    cb->cb_type == POOL_SCAN_SCRUB) {
		(void) printf(gettext("warning: will not scrub state that "
		    "belongs to the checkpoint of pool '%s'\n"),
		    zpool_get_name(zhp));
	}

	return (err != 0);
}

static int
wait_callback(zpool_handle_t *zhp, void *data)
{
	zpool_wait_activity_t *act = data;
	return (zpool_wait(zhp, *act));
}

/*
 * zpool scrub [-s | -p] [-w] [-e] <pool> ...
 *
 *	-e	Only scrub blocks in the error log.
 *	-s	Stop.  Stops any in-progress scrub.
 *	-p	Pause. Pause in-progress scrub.
 *	-w	Wait.  Blocks until scrub has completed.
 */
int
zpool_do_scrub(int argc, char **argv)
{
	int c;
	scrub_cbdata_t cb;
	boolean_t wait = B_FALSE;
	int error;

	cb.cb_type = POOL_SCAN_SCRUB;
	cb.cb_scrub_cmd = POOL_SCRUB_NORMAL;

	boolean_t is_error_scrub = B_FALSE;
	boolean_t is_pause = B_FALSE;
	boolean_t is_stop = B_FALSE;

	/* check options */
	while ((c = getopt(argc, argv, "spwe")) != -1) {
		switch (c) {
		case 'e':
			is_error_scrub = B_TRUE;
			break;
		case 's':
			is_stop = B_TRUE;
			break;
		case 'p':
			is_pause = B_TRUE;
			break;
		case 'w':
			wait = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			zpool_usage(B_FALSE);
		}
	}

	if (is_pause && is_stop) {
		(void) fprintf(stderr, gettext("invalid option "
		    "combination :-s and -p are mutually exclusive\n"));
		zpool_usage(B_FALSE);
	} else {
		if (is_error_scrub)
			cb.cb_type = POOL_SCAN_ERRORSCRUB;

		if (is_pause) {
			cb.cb_scrub_cmd = POOL_SCRUB_PAUSE;
		} else if (is_stop) {
			cb.cb_type = POOL_SCAN_NONE;
		} else {
			cb.cb_scrub_cmd = POOL_SCRUB_NORMAL;
		}
	}

	if (wait && (cb.cb_type == POOL_SCAN_NONE ||
	    cb.cb_scrub_cmd == POOL_SCRUB_PAUSE)) {
		(void) fprintf(stderr, gettext("invalid option combination: "
		    "-w cannot be used with -p or -s\n"));
		zpool_usage(B_FALSE);
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		zpool_usage(B_FALSE);
	}

	error = for_each_pool(argc, argv, B_TRUE, NULL, ZFS_TYPE_POOL,
	    B_FALSE, scrub_callback, &cb);

	if (wait && !error) {
		zpool_wait_activity_t act = ZPOOL_WAIT_SCRUB;
		error = for_each_pool(argc, argv, B_TRUE, NULL, ZFS_TYPE_POOL,
		    B_FALSE, wait_callback, &act);
	}

	return (error);
}

/*
 * zpool resilver <pool> ...
 *
 *	Restarts any in-progress resilver
 */
int
zpool_do_resilver(int argc, char **argv)
{
	int c;
	scrub_cbdata_t cb;

	cb.cb_type = POOL_SCAN_RESILVER;
	cb.cb_scrub_cmd = POOL_SCRUB_NORMAL;

	/* check options */
	while ((c = getopt(argc, argv, "")) != -1) {
		switch (c) {
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			zpool_usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		zpool_usage(B_FALSE);
	}

	return (for_each_pool(argc, argv, B_TRUE, NULL, ZFS_TYPE_POOL,
	    B_FALSE, scrub_callback, &cb));
}
