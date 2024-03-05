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

typedef struct wait_data {
	char *wd_poolname;
	boolean_t wd_scripted;
	boolean_t wd_exact;
	boolean_t wd_headers_once;
	boolean_t wd_should_exit;
	/* Which activities to wait for */
	boolean_t wd_enabled[ZPOOL_WAIT_NUM_ACTIVITIES];
	float wd_interval;
	pthread_cond_t wd_cv;
	pthread_mutex_t wd_mutex;
} wait_data_t;

/* Add up the total number of bytes left to initialize/trim across all vdevs */
static uint64_t
vdev_activity_remaining(nvlist_t *nv, zpool_wait_activity_t activity)
{
	uint64_t bytes_remaining;
	nvlist_t **child;
	uint_t c, children;
	vdev_stat_t *vs;

	assert(activity == ZPOOL_WAIT_INITIALIZE ||
	    activity == ZPOOL_WAIT_TRIM);

	verify(nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);

	if (activity == ZPOOL_WAIT_INITIALIZE &&
	    vs->vs_initialize_state == VDEV_INITIALIZE_ACTIVE)
		bytes_remaining = vs->vs_initialize_bytes_est -
		    vs->vs_initialize_bytes_done;
	else if (activity == ZPOOL_WAIT_TRIM &&
	    vs->vs_trim_state == VDEV_TRIM_ACTIVE)
		bytes_remaining = vs->vs_trim_bytes_est -
		    vs->vs_trim_bytes_done;
	else
		bytes_remaining = 0;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		children = 0;

	for (c = 0; c < children; c++)
		bytes_remaining += vdev_activity_remaining(child[c], activity);

	return (bytes_remaining);
}

/* Add up the total number of bytes left to rebuild across top-level vdevs */
static uint64_t
vdev_activity_top_remaining(nvlist_t *nv)
{
	uint64_t bytes_remaining = 0;
	nvlist_t **child;
	uint_t children;
	int error;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		children = 0;

	for (uint_t c = 0; c < children; c++) {
		vdev_rebuild_stat_t *vrs;
		uint_t i;

		error = nvlist_lookup_uint64_array(child[c],
		    ZPOOL_CONFIG_REBUILD_STATS, (uint64_t **)&vrs, &i);
		if (error == 0) {
			if (vrs->vrs_state == VDEV_REBUILD_ACTIVE) {
				bytes_remaining += (vrs->vrs_bytes_est -
				    vrs->vrs_bytes_rebuilt);
			}
		}
	}

	return (bytes_remaining);
}

/* Whether any vdevs are 'spare' or 'replacing' vdevs */
static boolean_t
vdev_any_spare_replacing(nvlist_t *nv)
{
	nvlist_t **child;
	uint_t c, children;
	const char *vdev_type;

	(void) nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &vdev_type);

	if (strcmp(vdev_type, VDEV_TYPE_REPLACING) == 0 ||
	    strcmp(vdev_type, VDEV_TYPE_SPARE) == 0 ||
	    strcmp(vdev_type, VDEV_TYPE_DRAID_SPARE) == 0) {
		return (B_TRUE);
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		children = 0;

	for (c = 0; c < children; c++) {
		if (vdev_any_spare_replacing(child[c]))
			return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Print to stdout a single line, containing one column for each activity that
 * we are waiting for specifying how many bytes of work are left for that
 * activity.
 */
static void
print_wait_status_row(wait_data_t *wd, zpool_handle_t *zhp, int row)
{
	nvlist_t *config, *nvroot;
	uint_t c;
	int i;
	pool_checkpoint_stat_t *pcs = NULL;
	pool_scan_stat_t *pss = NULL;
	pool_removal_stat_t *prs = NULL;
	pool_raidz_expand_stat_t *pres = NULL;
	const char *const headers[] = {"DISCARD", "FREE", "INITIALIZE",
	    "REPLACE", "REMOVE", "RESILVER", "SCRUB", "TRIM", "RAIDZ_EXPAND"};
	int col_widths[ZPOOL_WAIT_NUM_ACTIVITIES];

	/* Calculate the width of each column */
	for (i = 0; i < ZPOOL_WAIT_NUM_ACTIVITIES; i++) {
		/*
		 * Make sure we have enough space in the col for pretty-printed
		 * numbers and for the column header, and then leave a couple
		 * spaces between cols for readability.
		 */
		col_widths[i] = MAX(strlen(headers[i]), 6) + 2;
	}

	if (timestamp_fmt != NODATE)
		print_timestamp(timestamp_fmt);

	/* Print header if appropriate */
	int term_height = terminal_height();
	boolean_t reprint_header = (!wd->wd_headers_once && term_height > 0 &&
	    row % (term_height-1) == 0);
	if (!wd->wd_scripted && (row == 0 || reprint_header)) {
		for (i = 0; i < ZPOOL_WAIT_NUM_ACTIVITIES; i++) {
			if (wd->wd_enabled[i])
				(void) printf("%*s", col_widths[i], headers[i]);
		}
		(void) fputc('\n', stdout);
	}

	/* Bytes of work remaining in each activity */
	int64_t bytes_rem[ZPOOL_WAIT_NUM_ACTIVITIES] = {0};

	bytes_rem[ZPOOL_WAIT_FREE] =
	    zpool_get_prop_int(zhp, ZPOOL_PROP_FREEING, NULL);

	config = zpool_get_config(zhp, NULL);
	nvroot = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE);

	(void) nvlist_lookup_uint64_array(nvroot,
	    ZPOOL_CONFIG_CHECKPOINT_STATS, (uint64_t **)&pcs, &c);
	if (pcs != NULL && pcs->pcs_state == CS_CHECKPOINT_DISCARDING)
		bytes_rem[ZPOOL_WAIT_CKPT_DISCARD] = pcs->pcs_space;

	(void) nvlist_lookup_uint64_array(nvroot,
	    ZPOOL_CONFIG_REMOVAL_STATS, (uint64_t **)&prs, &c);
	if (prs != NULL && prs->prs_state == DSS_SCANNING)
		bytes_rem[ZPOOL_WAIT_REMOVE] = prs->prs_to_copy -
		    prs->prs_copied;

	(void) nvlist_lookup_uint64_array(nvroot,
	    ZPOOL_CONFIG_SCAN_STATS, (uint64_t **)&pss, &c);
	if (pss != NULL && pss->pss_state == DSS_SCANNING &&
	    pss->pss_pass_scrub_pause == 0) {
		int64_t rem = pss->pss_to_examine - pss->pss_issued;
		if (pss->pss_func == POOL_SCAN_SCRUB)
			bytes_rem[ZPOOL_WAIT_SCRUB] = rem;
		else
			bytes_rem[ZPOOL_WAIT_RESILVER] = rem;
	} else if (check_rebuilding(nvroot, NULL)) {
		bytes_rem[ZPOOL_WAIT_RESILVER] =
		    vdev_activity_top_remaining(nvroot);
	}

	(void) nvlist_lookup_uint64_array(nvroot,
	    ZPOOL_CONFIG_RAIDZ_EXPAND_STATS, (uint64_t **)&pres, &c);
	if (pres != NULL && pres->pres_state == DSS_SCANNING) {
		int64_t rem = pres->pres_to_reflow - pres->pres_reflowed;
		bytes_rem[ZPOOL_WAIT_RAIDZ_EXPAND] = rem;
	}

	bytes_rem[ZPOOL_WAIT_INITIALIZE] =
	    vdev_activity_remaining(nvroot, ZPOOL_WAIT_INITIALIZE);
	bytes_rem[ZPOOL_WAIT_TRIM] =
	    vdev_activity_remaining(nvroot, ZPOOL_WAIT_TRIM);

	/*
	 * A replace finishes after resilvering finishes, so the amount of work
	 * left for a replace is the same as for resilvering.
	 *
	 * It isn't quite correct to say that if we have any 'spare' or
	 * 'replacing' vdevs and a resilver is happening, then a replace is in
	 * progress, like we do here. When a hot spare is used, the faulted vdev
	 * is not removed after the hot spare is resilvered, so parent 'spare'
	 * vdev is not removed either. So we could have a 'spare' vdev, but be
	 * resilvering for a different reason. However, we use it as a heuristic
	 * because we don't have access to the DTLs, which could tell us whether
	 * or not we have really finished resilvering a hot spare.
	 */
	if (vdev_any_spare_replacing(nvroot))
		bytes_rem[ZPOOL_WAIT_REPLACE] =  bytes_rem[ZPOOL_WAIT_RESILVER];

	for (i = 0; i < ZPOOL_WAIT_NUM_ACTIVITIES; i++) {
		char buf[64];
		if (!wd->wd_enabled[i])
			continue;

		if (wd->wd_exact) {
			(void) snprintf(buf, sizeof (buf), "%" PRIi64,
			    bytes_rem[i]);
		} else {
			zfs_nicenum(bytes_rem[i], buf, sizeof (buf));
		}

		if (wd->wd_scripted)
			(void) printf(i == 0 ? "%s" : "\t%s", buf);
		else
			(void) printf(" %*s", col_widths[i] - 1, buf);
	}
	(void) printf("\n");
	(void) fflush(stdout);
}

static void *
wait_status_thread(void *arg)
{
	wait_data_t *wd = (wait_data_t *)arg;
	zpool_handle_t *zhp;

	if ((zhp = zpool_open(g_zfs, wd->wd_poolname)) == NULL)
		return (void *)(1);

	for (int row = 0; ; row++) {
		boolean_t missing;
		struct timespec timeout;
		int ret = 0;
		(void) clock_gettime(CLOCK_REALTIME, &timeout);

		if (zpool_refresh_stats(zhp, &missing) != 0 || missing ||
		    zpool_props_refresh(zhp) != 0) {
			zpool_close(zhp);
			return (void *)(uintptr_t)(missing ? 0 : 1);
		}

		print_wait_status_row(wd, zhp, row);

		timeout.tv_sec += floor(wd->wd_interval);
		long nanos = timeout.tv_nsec +
		    (wd->wd_interval - floor(wd->wd_interval)) * NANOSEC;
		if (nanos >= NANOSEC) {
			timeout.tv_sec++;
			timeout.tv_nsec = nanos - NANOSEC;
		} else {
			timeout.tv_nsec = nanos;
		}
		pthread_mutex_lock(&wd->wd_mutex);
		if (!wd->wd_should_exit)
			ret = pthread_cond_timedwait(&wd->wd_cv, &wd->wd_mutex,
			    &timeout);
		pthread_mutex_unlock(&wd->wd_mutex);
		if (ret == 0) {
			break; /* signaled by main thread */
		} else if (ret != ETIMEDOUT) {
			(void) fprintf(stderr, gettext("pthread_cond_timedwait "
			    "failed: %s\n"), strerror(ret));
			zpool_close(zhp);
			return (void *)(uintptr_t)(1);
		}
	}

	zpool_close(zhp);
	return (void *)(0);
}

int
zpool_do_wait(int argc, char **argv)
{
	boolean_t verbose = B_FALSE;
	int c, i;
	unsigned long count;
	pthread_t status_thr;
	int error = 0;
	zpool_handle_t *zhp;

	wait_data_t wd;
	wd.wd_scripted = B_FALSE;
	wd.wd_exact = B_FALSE;
	wd.wd_headers_once = B_FALSE;
	wd.wd_should_exit = B_FALSE;

	pthread_mutex_init(&wd.wd_mutex, NULL);
	pthread_cond_init(&wd.wd_cv, NULL);

	/* By default, wait for all types of activity. */
	for (i = 0; i < ZPOOL_WAIT_NUM_ACTIVITIES; i++)
		wd.wd_enabled[i] = B_TRUE;

	while ((c = getopt(argc, argv, "HpT:t:")) != -1) {
		switch (c) {
		case 'H':
			wd.wd_scripted = B_TRUE;
			break;
		case 'n':
			wd.wd_headers_once = B_TRUE;
			break;
		case 'p':
			wd.wd_exact = B_TRUE;
			break;
		case 'T':
			get_timestamp_arg(*optarg);
			break;
		case 't':
			/* Reset activities array */
			memset(&wd.wd_enabled, 0, sizeof (wd.wd_enabled));

			for (char *tok; (tok = strsep(&optarg, ",")); ) {
				static const char *const col_opts[] = {
				    "discard", "free", "initialize", "replace",
				    "remove", "resilver", "scrub", "trim",
				    "raidz_expand" };

				for (i = 0; i < ARRAY_SIZE(col_opts); ++i)
					if (strcmp(tok, col_opts[i]) == 0) {
						wd.wd_enabled[i] = B_TRUE;
						goto found;
					}

				(void) fprintf(stderr,
				    gettext("invalid activity '%s'\n"), tok);
				zpool_usage(B_FALSE);
found:;
			}
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			zpool_usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	get_interval_count(&argc, argv, &wd.wd_interval, &count);
	if (count != 0) {
		/* This subcmd only accepts an interval, not a count */
		(void) fprintf(stderr, gettext("too many arguments\n"));
		zpool_usage(B_FALSE);
	}

	if (wd.wd_interval != 0)
		verbose = B_TRUE;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing 'pool' argument\n"));
		zpool_usage(B_FALSE);
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		zpool_usage(B_FALSE);
	}

	wd.wd_poolname = argv[0];

	if ((zhp = zpool_open(g_zfs, wd.wd_poolname)) == NULL)
		return (1);

	if (verbose) {
		/*
		 * We use a separate thread for printing status updates because
		 * the main thread will call lzc_wait(), which blocks as long
		 * as an activity is in progress, which can be a long time.
		 */
		if (pthread_create(&status_thr, NULL, wait_status_thread, &wd)
		    != 0) {
			(void) fprintf(stderr, gettext("failed to create status"
			    "thread: %s\n"), strerror(errno));
			zpool_close(zhp);
			return (1);
		}
	}

	/*
	 * Loop over all activities that we are supposed to wait for until none
	 * of them are in progress. Note that this means we can end up waiting
	 * for more activities to complete than just those that were in progress
	 * when we began waiting; if an activity we are interested in begins
	 * while we are waiting for another activity, we will wait for both to
	 * complete before exiting.
	 */
	for (;;) {
		boolean_t missing = B_FALSE;
		boolean_t any_waited = B_FALSE;

		for (i = 0; i < ZPOOL_WAIT_NUM_ACTIVITIES; i++) {
			boolean_t waited;

			if (!wd.wd_enabled[i])
				continue;

			error = zpool_wait_status(zhp, i, &missing, &waited);
			if (error != 0 || missing)
				break;

			any_waited = (any_waited || waited);
		}

		if (error != 0 || missing || !any_waited)
			break;
	}

	zpool_close(zhp);

	if (verbose) {
		uintptr_t status;
		pthread_mutex_lock(&wd.wd_mutex);
		wd.wd_should_exit = B_TRUE;
		pthread_cond_signal(&wd.wd_cv);
		pthread_mutex_unlock(&wd.wd_mutex);
		(void) pthread_join(status_thr, (void *)&status);
		if (status != 0)
			error = status;
	}

	pthread_mutex_destroy(&wd.wd_mutex);
	pthread_cond_destroy(&wd.wd_cv);
	return (error);
}
