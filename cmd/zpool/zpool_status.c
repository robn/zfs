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

#define	POWER_OPT 1024

/*
 * Converts a total number of seconds to a human readable string broken
 * down in to days/hours/minutes/seconds.
 */
static void
secs_to_dhms(uint64_t total, char *buf)
{
	uint64_t days = total / 60 / 60 / 24;
	uint64_t hours = (total / 60 / 60) % 24;
	uint64_t mins = (total / 60) % 60;
	uint64_t secs = (total % 60);

	if (days > 0) {
		(void) sprintf(buf, "%llu days %02llu:%02llu:%02llu",
		    (u_longlong_t)days, (u_longlong_t)hours,
		    (u_longlong_t)mins, (u_longlong_t)secs);
	} else {
		(void) sprintf(buf, "%02llu:%02llu:%02llu",
		    (u_longlong_t)hours, (u_longlong_t)mins,
		    (u_longlong_t)secs);
	}
}

/*
 * Print out detailed error scrub status.
 */
static void
print_err_scrub_status(pool_scan_stat_t *ps)
{
	time_t start, end, pause;
	uint64_t total_secs_left;
	uint64_t secs_left, mins_left, hours_left, days_left;
	uint64_t examined, to_be_examined;

	if (ps == NULL || ps->pss_error_scrub_func != POOL_SCAN_ERRORSCRUB) {
		return;
	}

	(void) printf(gettext(" scrub: "));

	start = ps->pss_error_scrub_start;
	end = ps->pss_error_scrub_end;
	pause = ps->pss_pass_error_scrub_pause;
	examined = ps->pss_error_scrub_examined;
	to_be_examined = ps->pss_error_scrub_to_be_examined;

	assert(ps->pss_error_scrub_func == POOL_SCAN_ERRORSCRUB);

	if (ps->pss_error_scrub_state == DSS_FINISHED) {
		total_secs_left = end - start;
		days_left = total_secs_left / 60 / 60 / 24;
		hours_left = (total_secs_left / 60 / 60) % 24;
		mins_left = (total_secs_left / 60) % 60;
		secs_left = (total_secs_left % 60);

		(void) printf(gettext("scrubbed %llu error blocks in %llu days "
		    "%02llu:%02llu:%02llu on %s"), (u_longlong_t)examined,
		    (u_longlong_t)days_left, (u_longlong_t)hours_left,
		    (u_longlong_t)mins_left, (u_longlong_t)secs_left,
		    ctime(&end));

		return;
	} else if (ps->pss_error_scrub_state == DSS_CANCELED) {
		(void) printf(gettext("error scrub canceled on %s"),
		    ctime(&end));
		return;
	}
	assert(ps->pss_error_scrub_state == DSS_ERRORSCRUBBING);

	/* Error scrub is in progress. */
	if (pause == 0) {
		(void) printf(gettext("error scrub in progress since %s"),
		    ctime(&start));
	} else {
		(void) printf(gettext("error scrub paused since %s"),
		    ctime(&pause));
		(void) printf(gettext("\terror scrub started on %s"),
		    ctime(&start));
	}

	double fraction_done = (double)examined / (to_be_examined + examined);
	(void) printf(gettext("\t%.2f%% done, issued I/O for %llu error"
	    " blocks"), 100 * fraction_done, (u_longlong_t)examined);

	(void) printf("\n");
}

/*
 * Print out detailed scrub status.
 */
static void
print_scan_scrub_resilver_status(pool_scan_stat_t *ps)
{
	time_t start, end, pause;
	uint64_t pass_scanned, scanned, pass_issued, issued, total_s, total_i;
	uint64_t elapsed, scan_rate, issue_rate;
	double fraction_done;
	char processed_buf[7], scanned_buf[7], issued_buf[7], total_s_buf[7];
	char total_i_buf[7], srate_buf[7], irate_buf[7], time_buf[32];

	printf("  ");
	printf_color(ANSI_BOLD, gettext("scan:"));
	printf(" ");

	/* If there's never been a scan, there's not much to say. */
	if (ps == NULL || ps->pss_func == POOL_SCAN_NONE ||
	    ps->pss_func >= POOL_SCAN_FUNCS) {
		(void) printf(gettext("none requested\n"));
		return;
	}

	start = ps->pss_start_time;
	end = ps->pss_end_time;
	pause = ps->pss_pass_scrub_pause;

	zfs_nicebytes(ps->pss_processed, processed_buf, sizeof (processed_buf));

	int is_resilver = ps->pss_func == POOL_SCAN_RESILVER;
	int is_scrub = ps->pss_func == POOL_SCAN_SCRUB;
	assert(is_resilver || is_scrub);

	/* Scan is finished or canceled. */
	if (ps->pss_state == DSS_FINISHED) {
		secs_to_dhms(end - start, time_buf);

		if (is_scrub) {
			(void) printf(gettext("scrub repaired %s "
			    "in %s with %llu errors on %s"), processed_buf,
			    time_buf, (u_longlong_t)ps->pss_errors,
			    ctime(&end));
		} else if (is_resilver) {
			(void) printf(gettext("resilvered %s "
			    "in %s with %llu errors on %s"), processed_buf,
			    time_buf, (u_longlong_t)ps->pss_errors,
			    ctime(&end));
		}
		return;
	} else if (ps->pss_state == DSS_CANCELED) {
		if (is_scrub) {
			(void) printf(gettext("scrub canceled on %s"),
			    ctime(&end));
		} else if (is_resilver) {
			(void) printf(gettext("resilver canceled on %s"),
			    ctime(&end));
		}
		return;
	}

	assert(ps->pss_state == DSS_SCANNING);

	/* Scan is in progress. Resilvers can't be paused. */
	if (is_scrub) {
		if (pause == 0) {
			(void) printf(gettext("scrub in progress since %s"),
			    ctime(&start));
		} else {
			(void) printf(gettext("scrub paused since %s"),
			    ctime(&pause));
			(void) printf(gettext("\tscrub started on %s"),
			    ctime(&start));
		}
	} else if (is_resilver) {
		(void) printf(gettext("resilver in progress since %s"),
		    ctime(&start));
	}

	scanned = ps->pss_examined;
	pass_scanned = ps->pss_pass_exam;
	issued = ps->pss_issued;
	pass_issued = ps->pss_pass_issued;
	total_s = ps->pss_to_examine;
	total_i = ps->pss_to_examine - ps->pss_skipped;

	/* we are only done with a block once we have issued the IO for it */
	fraction_done = (double)issued / total_i;

	/* elapsed time for this pass, rounding up to 1 if it's 0 */
	elapsed = time(NULL) - ps->pss_pass_start;
	elapsed -= ps->pss_pass_scrub_spent_paused;
	elapsed = (elapsed != 0) ? elapsed : 1;

	scan_rate = pass_scanned / elapsed;
	issue_rate = pass_issued / elapsed;

	/* format all of the numbers we will be reporting */
	zfs_nicebytes(scanned, scanned_buf, sizeof (scanned_buf));
	zfs_nicebytes(issued, issued_buf, sizeof (issued_buf));
	zfs_nicebytes(total_s, total_s_buf, sizeof (total_s_buf));
	zfs_nicebytes(total_i, total_i_buf, sizeof (total_i_buf));

	/* do not print estimated time if we have a paused scrub */
	(void) printf(gettext("\t%s / %s scanned"), scanned_buf, total_s_buf);
	if (pause == 0 && scan_rate > 0) {
		zfs_nicebytes(scan_rate, srate_buf, sizeof (srate_buf));
		(void) printf(gettext(" at %s/s"), srate_buf);
	}
	(void) printf(gettext(", %s / %s issued"), issued_buf, total_i_buf);
	if (pause == 0 && issue_rate > 0) {
		zfs_nicebytes(issue_rate, irate_buf, sizeof (irate_buf));
		(void) printf(gettext(" at %s/s"), irate_buf);
	}
	(void) printf(gettext("\n"));

	if (is_resilver) {
		(void) printf(gettext("\t%s resilvered, %.2f%% done"),
		    processed_buf, 100 * fraction_done);
	} else if (is_scrub) {
		(void) printf(gettext("\t%s repaired, %.2f%% done"),
		    processed_buf, 100 * fraction_done);
	}

	if (pause == 0) {
		/*
		 * Only provide an estimate iff:
		 * 1) we haven't yet issued all we expected, and
		 * 2) the issue rate exceeds 10 MB/s, and
		 * 3) it's either:
		 *    a) a resilver which has started repairs, or
		 *    b) a scrub which has entered the issue phase.
		 */
		if (total_i >= issued && issue_rate >= 10 * 1024 * 1024 &&
		    ((is_resilver && ps->pss_processed > 0) ||
		    (is_scrub && issued > 0))) {
			secs_to_dhms((total_i - issued) / issue_rate, time_buf);
			(void) printf(gettext(", %s to go\n"), time_buf);
		} else {
			(void) printf(gettext(", no estimated "
			    "completion time\n"));
		}
	} else {
		(void) printf(gettext("\n"));
	}
}

static void
print_rebuild_status_impl(vdev_rebuild_stat_t *vrs, uint_t c, char *vdev_name)
{
	if (vrs == NULL || vrs->vrs_state == VDEV_REBUILD_NONE)
		return;

	printf("  ");
	printf_color(ANSI_BOLD, gettext("scan:"));
	printf(" ");

	uint64_t bytes_scanned = vrs->vrs_bytes_scanned;
	uint64_t bytes_issued = vrs->vrs_bytes_issued;
	uint64_t bytes_rebuilt = vrs->vrs_bytes_rebuilt;
	uint64_t bytes_est_s = vrs->vrs_bytes_est;
	uint64_t bytes_est_i = vrs->vrs_bytes_est;
	if (c > offsetof(vdev_rebuild_stat_t, vrs_pass_bytes_skipped) / 8)
		bytes_est_i -= vrs->vrs_pass_bytes_skipped;
	uint64_t scan_rate = (vrs->vrs_pass_bytes_scanned /
	    (vrs->vrs_pass_time_ms + 1)) * 1000;
	uint64_t issue_rate = (vrs->vrs_pass_bytes_issued /
	    (vrs->vrs_pass_time_ms + 1)) * 1000;
	double scan_pct = MIN((double)bytes_scanned * 100 /
	    (bytes_est_s + 1), 100);

	/* Format all of the numbers we will be reporting */
	char bytes_scanned_buf[7], bytes_issued_buf[7];
	char bytes_rebuilt_buf[7], bytes_est_s_buf[7], bytes_est_i_buf[7];
	char scan_rate_buf[7], issue_rate_buf[7], time_buf[32];
	zfs_nicebytes(bytes_scanned, bytes_scanned_buf,
	    sizeof (bytes_scanned_buf));
	zfs_nicebytes(bytes_issued, bytes_issued_buf,
	    sizeof (bytes_issued_buf));
	zfs_nicebytes(bytes_rebuilt, bytes_rebuilt_buf,
	    sizeof (bytes_rebuilt_buf));
	zfs_nicebytes(bytes_est_s, bytes_est_s_buf, sizeof (bytes_est_s_buf));
	zfs_nicebytes(bytes_est_i, bytes_est_i_buf, sizeof (bytes_est_i_buf));

	time_t start = vrs->vrs_start_time;
	time_t end = vrs->vrs_end_time;

	/* Rebuild is finished or canceled. */
	if (vrs->vrs_state == VDEV_REBUILD_COMPLETE) {
		secs_to_dhms(vrs->vrs_scan_time_ms / 1000, time_buf);
		(void) printf(gettext("resilvered (%s) %s in %s "
		    "with %llu errors on %s"), vdev_name, bytes_rebuilt_buf,
		    time_buf, (u_longlong_t)vrs->vrs_errors, ctime(&end));
		return;
	} else if (vrs->vrs_state == VDEV_REBUILD_CANCELED) {
		(void) printf(gettext("resilver (%s) canceled on %s"),
		    vdev_name, ctime(&end));
		return;
	} else if (vrs->vrs_state == VDEV_REBUILD_ACTIVE) {
		(void) printf(gettext("resilver (%s) in progress since %s"),
		    vdev_name, ctime(&start));
	}

	assert(vrs->vrs_state == VDEV_REBUILD_ACTIVE);

	(void) printf(gettext("\t%s / %s scanned"), bytes_scanned_buf,
	    bytes_est_s_buf);
	if (scan_rate > 0) {
		zfs_nicebytes(scan_rate, scan_rate_buf, sizeof (scan_rate_buf));
		(void) printf(gettext(" at %s/s"), scan_rate_buf);
	}
	(void) printf(gettext(", %s / %s issued"), bytes_issued_buf,
	    bytes_est_i_buf);
	if (issue_rate > 0) {
		zfs_nicebytes(issue_rate, issue_rate_buf,
		    sizeof (issue_rate_buf));
		(void) printf(gettext(" at %s/s"), issue_rate_buf);
	}
	(void) printf(gettext("\n"));

	(void) printf(gettext("\t%s resilvered, %.2f%% done"),
	    bytes_rebuilt_buf, scan_pct);

	if (vrs->vrs_state == VDEV_REBUILD_ACTIVE) {
		if (bytes_est_s >= bytes_scanned &&
		    scan_rate >= 10 * 1024 * 1024) {
			secs_to_dhms((bytes_est_s - bytes_scanned) / scan_rate,
			    time_buf);
			(void) printf(gettext(", %s to go\n"), time_buf);
		} else {
			(void) printf(gettext(", no estimated "
			    "completion time\n"));
		}
	} else {
		(void) printf(gettext("\n"));
	}
}

/*
 * Print rebuild status for top-level vdevs.
 */
static void
print_rebuild_status(zpool_handle_t *zhp, nvlist_t *nvroot)
{
	nvlist_t **child;
	uint_t children;

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		children = 0;

	for (uint_t c = 0; c < children; c++) {
		vdev_rebuild_stat_t *vrs;
		uint_t i;

		if (nvlist_lookup_uint64_array(child[c],
		    ZPOOL_CONFIG_REBUILD_STATS, (uint64_t **)&vrs, &i) == 0) {
			char *name = zpool_vdev_name(g_zfs, zhp,
			    child[c], VDEV_NAME_TYPE_ID);
			print_rebuild_status_impl(vrs, i, name);
			free(name);
		}
	}
}

/*
 * As we don't scrub checkpointed blocks, we want to warn the user that we
 * skipped scanning some blocks if a checkpoint exists or existed at any
 * time during the scan.  If a sequential instead of healing reconstruction
 * was performed then the blocks were reconstructed.  However, their checksums
 * have not been verified so we still print the warning.
 */
static void
print_checkpoint_scan_warning(pool_scan_stat_t *ps, pool_checkpoint_stat_t *pcs)
{
	if (ps == NULL || pcs == NULL)
		return;

	if (pcs->pcs_state == CS_NONE ||
	    pcs->pcs_state == CS_CHECKPOINT_DISCARDING)
		return;

	assert(pcs->pcs_state == CS_CHECKPOINT_EXISTS);

	if (ps->pss_state == DSS_NONE)
		return;

	if ((ps->pss_state == DSS_FINISHED || ps->pss_state == DSS_CANCELED) &&
	    ps->pss_end_time < pcs->pcs_start_time)
		return;

	if (ps->pss_state == DSS_FINISHED || ps->pss_state == DSS_CANCELED) {
		(void) printf(gettext("    scan warning: skipped blocks "
		    "that are only referenced by the checkpoint.\n"));
	} else {
		assert(ps->pss_state == DSS_SCANNING);
		(void) printf(gettext("    scan warning: skipping blocks "
		    "that are only referenced by the checkpoint.\n"));
	}
}

/*
 * Print the scan status.
 */
static void
print_scan_status(zpool_handle_t *zhp, nvlist_t *nvroot)
{
	uint64_t rebuild_end_time = 0, resilver_end_time = 0;
	boolean_t have_resilver = B_FALSE, have_scrub = B_FALSE;
	boolean_t have_errorscrub = B_FALSE;
	boolean_t active_resilver = B_FALSE;
	pool_checkpoint_stat_t *pcs = NULL;
	pool_scan_stat_t *ps = NULL;
	uint_t c;
	time_t scrub_start = 0, errorscrub_start = 0;

	if (nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_SCAN_STATS,
	    (uint64_t **)&ps, &c) == 0) {
		if (ps->pss_func == POOL_SCAN_RESILVER) {
			resilver_end_time = ps->pss_end_time;
			active_resilver = (ps->pss_state == DSS_SCANNING);
		}

		have_resilver = (ps->pss_func == POOL_SCAN_RESILVER);
		have_scrub = (ps->pss_func == POOL_SCAN_SCRUB);
		scrub_start = ps->pss_start_time;
		if (c > offsetof(pool_scan_stat_t,
		    pss_pass_error_scrub_pause) / 8) {
			have_errorscrub = (ps->pss_error_scrub_func ==
			    POOL_SCAN_ERRORSCRUB);
			errorscrub_start = ps->pss_error_scrub_start;
		}
	}

	boolean_t active_rebuild = check_rebuilding(nvroot, &rebuild_end_time);
	boolean_t have_rebuild = (active_rebuild || (rebuild_end_time > 0));

	/* Always print the scrub status when available. */
	if (have_scrub && scrub_start > errorscrub_start)
		print_scan_scrub_resilver_status(ps);
	else if (have_errorscrub && errorscrub_start >= scrub_start)
		print_err_scrub_status(ps);

	/*
	 * When there is an active resilver or rebuild print its status.
	 * Otherwise print the status of the last resilver or rebuild.
	 */
	if (active_resilver || (!active_rebuild && have_resilver &&
	    resilver_end_time && resilver_end_time > rebuild_end_time)) {
		print_scan_scrub_resilver_status(ps);
	} else if (active_rebuild || (!active_resilver && have_rebuild &&
	    rebuild_end_time && rebuild_end_time > resilver_end_time)) {
		print_rebuild_status(zhp, nvroot);
	}

	(void) nvlist_lookup_uint64_array(nvroot,
	    ZPOOL_CONFIG_CHECKPOINT_STATS, (uint64_t **)&pcs, &c);
	print_checkpoint_scan_warning(ps, pcs);
}

/*
 * Print out detailed removal status.
 */
static void
print_removal_status(zpool_handle_t *zhp, pool_removal_stat_t *prs)
{
	char copied_buf[7], examined_buf[7], total_buf[7], rate_buf[7];
	time_t start, end;
	nvlist_t *config, *nvroot;
	nvlist_t **child;
	uint_t children;
	char *vdev_name;

	if (prs == NULL || prs->prs_state == DSS_NONE)
		return;

	/*
	 * Determine name of vdev.
	 */
	config = zpool_get_config(zhp, NULL);
	nvroot = fnvlist_lookup_nvlist(config,
	    ZPOOL_CONFIG_VDEV_TREE);
	verify(nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0);
	assert(prs->prs_removing_vdev < children);
	vdev_name = zpool_vdev_name(g_zfs, zhp,
	    child[prs->prs_removing_vdev], B_TRUE);

	printf_color(ANSI_BOLD, gettext("remove: "));

	start = prs->prs_start_time;
	end = prs->prs_end_time;
	zfs_nicenum(prs->prs_copied, copied_buf, sizeof (copied_buf));

	/*
	 * Removal is finished or canceled.
	 */
	if (prs->prs_state == DSS_FINISHED) {
		uint64_t minutes_taken = (end - start) / 60;

		(void) printf(gettext("Removal of vdev %llu copied %s "
		    "in %lluh%um, completed on %s"),
		    (longlong_t)prs->prs_removing_vdev,
		    copied_buf,
		    (u_longlong_t)(minutes_taken / 60),
		    (uint_t)(minutes_taken % 60),
		    ctime((time_t *)&end));
	} else if (prs->prs_state == DSS_CANCELED) {
		(void) printf(gettext("Removal of %s canceled on %s"),
		    vdev_name, ctime(&end));
	} else {
		uint64_t copied, total, elapsed, mins_left, hours_left;
		double fraction_done;
		uint_t rate;

		assert(prs->prs_state == DSS_SCANNING);

		/*
		 * Removal is in progress.
		 */
		(void) printf(gettext(
		    "Evacuation of %s in progress since %s"),
		    vdev_name, ctime(&start));

		copied = prs->prs_copied > 0 ? prs->prs_copied : 1;
		total = prs->prs_to_copy;
		fraction_done = (double)copied / total;

		/* elapsed time for this pass */
		elapsed = time(NULL) - prs->prs_start_time;
		elapsed = elapsed > 0 ? elapsed : 1;
		rate = copied / elapsed;
		rate = rate > 0 ? rate : 1;
		mins_left = ((total - copied) / rate) / 60;
		hours_left = mins_left / 60;

		zfs_nicenum(copied, examined_buf, sizeof (examined_buf));
		zfs_nicenum(total, total_buf, sizeof (total_buf));
		zfs_nicenum(rate, rate_buf, sizeof (rate_buf));

		/*
		 * do not print estimated time if hours_left is more than
		 * 30 days
		 */
		(void) printf(gettext(
		    "\t%s copied out of %s at %s/s, %.2f%% done"),
		    examined_buf, total_buf, rate_buf, 100 * fraction_done);
		if (hours_left < (30 * 24)) {
			(void) printf(gettext(", %lluh%um to go\n"),
			    (u_longlong_t)hours_left, (uint_t)(mins_left % 60));
		} else {
			(void) printf(gettext(
			    ", (copy is slow, no estimated time)\n"));
		}
	}
	free(vdev_name);

	if (prs->prs_mapping_memory > 0) {
		char mem_buf[7];
		zfs_nicenum(prs->prs_mapping_memory, mem_buf, sizeof (mem_buf));
		(void) printf(gettext(
		    "\t%s memory used for removed device mappings\n"),
		    mem_buf);
	}
}

/*
 * Print out detailed raidz expansion status.
 */
static void
print_raidz_expand_status(zpool_handle_t *zhp, pool_raidz_expand_stat_t *pres)
{
	char copied_buf[7];

	if (pres == NULL || pres->pres_state == DSS_NONE)
		return;

	/*
	 * Determine name of vdev.
	 */
	nvlist_t *config = zpool_get_config(zhp, NULL);
	nvlist_t *nvroot = fnvlist_lookup_nvlist(config,
	    ZPOOL_CONFIG_VDEV_TREE);
	nvlist_t **child;
	uint_t children;
	verify(nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0);
	assert(pres->pres_expanding_vdev < children);

	printf_color(ANSI_BOLD, gettext("expand: "));

	time_t start = pres->pres_start_time;
	time_t end = pres->pres_end_time;
	char *vname =
	    zpool_vdev_name(g_zfs, zhp, child[pres->pres_expanding_vdev], 0);
	zfs_nicenum(pres->pres_reflowed, copied_buf, sizeof (copied_buf));

	/*
	 * Expansion is finished or canceled.
	 */
	if (pres->pres_state == DSS_FINISHED) {
		char time_buf[32];
		secs_to_dhms(end - start, time_buf);

		(void) printf(gettext("expanded %s-%u copied %s in %s, "
		    "on %s"), vname, (int)pres->pres_expanding_vdev,
		    copied_buf, time_buf, ctime((time_t *)&end));
	} else {
		char examined_buf[7], total_buf[7], rate_buf[7];
		uint64_t copied, total, elapsed, secs_left;
		double fraction_done;
		uint_t rate;

		assert(pres->pres_state == DSS_SCANNING);

		/*
		 * Expansion is in progress.
		 */
		(void) printf(gettext(
		    "expansion of %s-%u in progress since %s"),
		    vname, (int)pres->pres_expanding_vdev, ctime(&start));

		copied = pres->pres_reflowed > 0 ? pres->pres_reflowed : 1;
		total = pres->pres_to_reflow;
		fraction_done = (double)copied / total;

		/* elapsed time for this pass */
		elapsed = time(NULL) - pres->pres_start_time;
		elapsed = elapsed > 0 ? elapsed : 1;
		rate = copied / elapsed;
		rate = rate > 0 ? rate : 1;
		secs_left = (total - copied) / rate;

		zfs_nicenum(copied, examined_buf, sizeof (examined_buf));
		zfs_nicenum(total, total_buf, sizeof (total_buf));
		zfs_nicenum(rate, rate_buf, sizeof (rate_buf));

		/*
		 * do not print estimated time if hours_left is more than
		 * 30 days
		 */
		(void) printf(gettext("\t%s / %s copied at %s/s, %.2f%% done"),
		    examined_buf, total_buf, rate_buf, 100 * fraction_done);
		if (pres->pres_waiting_for_resilver) {
			(void) printf(gettext(", paused for resilver or "
			    "clear\n"));
		} else if (secs_left < (30 * 24 * 3600)) {
			char time_buf[32];
			secs_to_dhms(secs_left, time_buf);
			(void) printf(gettext(", %s to go\n"), time_buf);
		} else {
			(void) printf(gettext(
			    ", (copy is slow, no estimated time)\n"));
		}
	}
	free(vname);
}
static void
print_checkpoint_status(pool_checkpoint_stat_t *pcs)
{
	time_t start;
	char space_buf[7];

	if (pcs == NULL || pcs->pcs_state == CS_NONE)
		return;

	(void) printf(gettext("checkpoint: "));

	start = pcs->pcs_start_time;
	zfs_nicenum(pcs->pcs_space, space_buf, sizeof (space_buf));

	if (pcs->pcs_state == CS_CHECKPOINT_EXISTS) {
		char *date = ctime(&start);

		/*
		 * ctime() adds a newline at the end of the generated
		 * string, thus the weird format specifier and the
		 * strlen() call used to chop it off from the output.
		 */
		(void) printf(gettext("created %.*s, consumes %s\n"),
		    (int)(strlen(date) - 1), date, space_buf);
		return;
	}

	assert(pcs->pcs_state == CS_CHECKPOINT_DISCARDING);

	(void) printf(gettext("discarding, %s remaining.\n"),
	    space_buf);
}

static void
print_error_log(zpool_handle_t *zhp)
{
	nvlist_t *nverrlist = NULL;
	nvpair_t *elem;
	char *pathname;
	size_t len = MAXPATHLEN * 2;

	if (zpool_get_errlog(zhp, &nverrlist) != 0)
		return;

	(void) printf("errors: Permanent errors have been "
	    "detected in the following files:\n\n");

	pathname = safe_malloc(len);
	elem = NULL;
	while ((elem = nvlist_next_nvpair(nverrlist, elem)) != NULL) {
		nvlist_t *nv;
		uint64_t dsobj, obj;

		verify(nvpair_value_nvlist(elem, &nv) == 0);
		verify(nvlist_lookup_uint64(nv, ZPOOL_ERR_DATASET,
		    &dsobj) == 0);
		verify(nvlist_lookup_uint64(nv, ZPOOL_ERR_OBJECT,
		    &obj) == 0);
		zpool_obj_to_path(zhp, dsobj, obj, pathname, len);
		(void) printf("%7s %s\n", "", pathname);
	}
	free(pathname);
	nvlist_free(nverrlist);
}

static void
print_spares(zpool_handle_t *zhp, status_cbdata_t *cb, nvlist_t **spares,
    uint_t nspares)
{
	uint_t i;
	char *name;

	if (nspares == 0)
		return;

	(void) printf(gettext("\tspares\n"));

	for (i = 0; i < nspares; i++) {
		name = zpool_vdev_name(g_zfs, zhp, spares[i],
		    cb->cb_name_flags);
		print_status_config(zhp, cb, name, spares[i], 2, B_TRUE, NULL);
		free(name);
	}
}

static void
print_l2cache(zpool_handle_t *zhp, status_cbdata_t *cb, nvlist_t **l2cache,
    uint_t nl2cache)
{
	uint_t i;
	char *name;

	if (nl2cache == 0)
		return;

	(void) printf(gettext("\tcache\n"));

	for (i = 0; i < nl2cache; i++) {
		name = zpool_vdev_name(g_zfs, zhp, l2cache[i],
		    cb->cb_name_flags);
		print_status_config(zhp, cb, name, l2cache[i], 2,
		    B_FALSE, NULL);
		free(name);
	}
}

static void
print_dedup_stats(nvlist_t *config)
{
	ddt_histogram_t *ddh;
	ddt_stat_t *dds;
	ddt_object_t *ddo;
	uint_t c;
	char dspace[6], mspace[6];

	/*
	 * If the pool was faulted then we may not have been able to
	 * obtain the config. Otherwise, if we have anything in the dedup
	 * table continue processing the stats.
	 */
	if (nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_DDT_OBJ_STATS,
	    (uint64_t **)&ddo, &c) != 0)
		return;

	(void) printf("\n");
	(void) printf(gettext(" dedup: "));
	if (ddo->ddo_count == 0) {
		(void) printf(gettext("no DDT entries\n"));
		return;
	}

	zfs_nicebytes(ddo->ddo_dspace, dspace, sizeof (dspace));
	zfs_nicebytes(ddo->ddo_mspace, mspace, sizeof (mspace));
	(void) printf("DDT entries %llu, size %s on disk, %s in core\n",
	    (u_longlong_t)ddo->ddo_count,
	    dspace,
	    mspace);

	verify(nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_DDT_STATS,
	    (uint64_t **)&dds, &c) == 0);
	verify(nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_DDT_HISTOGRAM,
	    (uint64_t **)&ddh, &c) == 0);
	zpool_dump_ddt(dds, ddh);
}

/*
 * Display a summary of pool status.  Displays a summary such as:
 *
 *        pool: tank
 *	status: DEGRADED
 *	reason: One or more devices ...
 *         see: https://openzfs.github.io/openzfs-docs/msg/ZFS-xxxx-01
 *	config:
 *		mirror		DEGRADED
 *                c1t0d0	OK
 *                c2t0d0	UNAVAIL
 *
 * When given the '-v' option, we print out the complete config.  If the '-e'
 * option is specified, then we print out error rate information as well.
 */
static int
status_callback(zpool_handle_t *zhp, void *data)
{
	status_cbdata_t *cbp = data;
	nvlist_t *config, *nvroot;
	const char *msgid;
	zpool_status_t reason;
	zpool_errata_t errata;
	const char *health;
	uint_t c;
	vdev_stat_t *vs;

	config = zpool_get_config(zhp, NULL);
	reason = zpool_get_status(zhp, &msgid, &errata);

	cbp->cb_count++;

	/*
	 * If we were given 'zpool status -x', only report those pools with
	 * problems.
	 */
	if (cbp->cb_explain &&
	    (reason == ZPOOL_STATUS_OK ||
	    reason == ZPOOL_STATUS_VERSION_OLDER ||
	    reason == ZPOOL_STATUS_FEAT_DISABLED ||
	    reason == ZPOOL_STATUS_COMPATIBILITY_ERR ||
	    reason == ZPOOL_STATUS_INCOMPATIBLE_FEAT)) {
		if (!cbp->cb_allpools) {
			(void) printf(gettext("pool '%s' is healthy\n"),
			    zpool_get_name(zhp));
			if (cbp->cb_first)
				cbp->cb_first = B_FALSE;
		}
		return (0);
	}

	if (cbp->cb_first)
		cbp->cb_first = B_FALSE;
	else
		(void) printf("\n");

	nvroot = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE);
	verify(nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);

	health = zpool_get_state_str(zhp);

	printf("  ");
	printf_color(ANSI_BOLD, gettext("pool:"));
	printf(" %s\n", zpool_get_name(zhp));
	fputc(' ', stdout);
	printf_color(ANSI_BOLD, gettext("state: "));

	printf_color(health_str_to_color(health), "%s", health);

	fputc('\n', stdout);

	switch (reason) {
	case ZPOOL_STATUS_MISSING_DEV_R:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices could "
		    "not be opened.  Sufficient replicas exist for\n\tthe pool "
		    "to continue functioning in a degraded state.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Attach the missing device "
		    "and online it using 'zpool online'.\n"));
		break;

	case ZPOOL_STATUS_MISSING_DEV_NR:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices could "
		    "not be opened.  There are insufficient\n\treplicas for the"
		    " pool to continue functioning.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Attach the missing device "
		    "and online it using 'zpool online'.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_LABEL_R:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices could "
		    "not be used because the label is missing or\n\tinvalid.  "
		    "Sufficient replicas exist for the pool to continue\n\t"
		    "functioning in a degraded state.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Replace the device using "
		    "'zpool replace'.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_LABEL_NR:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices could "
		    "not be used because the label is missing \n\tor invalid.  "
		    "There are insufficient replicas for the pool to "
		    "continue\n\tfunctioning.\n"));
		zpool_explain_recover(zpool_get_handle(zhp),
		    zpool_get_name(zhp), reason, config);
		break;

	case ZPOOL_STATUS_FAILING_DEV:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices has "
		    "experienced an unrecoverable error.  An\n\tattempt was "
		    "made to correct the error.  Applications are "
		    "unaffected.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
			printf_color(ANSI_YELLOW, gettext("Determine if the "
		    "device needs to be replaced, and clear the errors\n\tusing"
		    " 'zpool clear' or replace the device with 'zpool "
		    "replace'.\n"));
		break;

	case ZPOOL_STATUS_OFFLINE_DEV:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices has "
		    "been taken offline by the administrator.\n\tSufficient "
		    "replicas exist for the pool to continue functioning in "
		    "a\n\tdegraded state.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Online the device "
		    "using 'zpool online' or replace the device with\n\t'zpool "
		    "replace'.\n"));
		break;

	case ZPOOL_STATUS_REMOVED_DEV:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices has "
		    "been removed by the administrator.\n\tSufficient "
		    "replicas exist for the pool to continue functioning in "
		    "a\n\tdegraded state.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Online the device "
		    "using zpool online' or replace the device with\n\t'zpool "
		    "replace'.\n"));
		break;

	case ZPOOL_STATUS_RESILVERING:
	case ZPOOL_STATUS_REBUILDING:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices is "
		    "currently being resilvered.  The pool will\n\tcontinue "
		    "to function, possibly in a degraded state.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Wait for the resilver to "
		    "complete.\n"));
		break;

	case ZPOOL_STATUS_REBUILD_SCRUB:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices have "
		    "been sequentially resilvered, scrubbing\n\tthe pool "
		    "is recommended.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Use 'zpool scrub' to "
		    "verify all data checksums.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_DATA:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices has "
		    "experienced an error resulting in data\n\tcorruption.  "
		    "Applications may be affected.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Restore the file in question"
		    " if possible.  Otherwise restore the\n\tentire pool from "
		    "backup.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_POOL:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("The pool metadata is "
		    "corrupted and the pool cannot be opened.\n"));
		zpool_explain_recover(zpool_get_handle(zhp),
		    zpool_get_name(zhp), reason, config);
		break;

	case ZPOOL_STATUS_VERSION_OLDER:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("The pool is formatted using "
		    "a legacy on-disk format.  The pool can\n\tstill be used, "
		    "but some features are unavailable.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Upgrade the pool using "
		    "'zpool upgrade'.  Once this is done, the\n\tpool will no "
		    "longer be accessible on software that does not support\n\t"
		    "feature flags.\n"));
		break;

	case ZPOOL_STATUS_VERSION_NEWER:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("The pool has been upgraded "
		    "to a newer, incompatible on-disk version.\n\tThe pool "
		    "cannot be accessed on this system.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Access the pool from a "
		    "system running more recent software, or\n\trestore the "
		    "pool from backup.\n"));
		break;

	case ZPOOL_STATUS_FEAT_DISABLED:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("Some supported and "
		    "requested features are not enabled on the pool.\n\t"
		    "The pool can still be used, but some features are "
		    "unavailable.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Enable all features using "
		    "'zpool upgrade'. Once this is done,\n\tthe pool may no "
		    "longer be accessible by software that does not support\n\t"
		    "the features. See zpool-features(7) for details.\n"));
		break;

	case ZPOOL_STATUS_COMPATIBILITY_ERR:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("This pool has a "
		    "compatibility list specified, but it could not be\n\t"
		    "read/parsed at this time. The pool can still be used, "
		    "but this\n\tshould be investigated.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Check the value of the "
		    "'compatibility' property against the\n\t"
		    "appropriate file in " ZPOOL_SYSCONF_COMPAT_D " or "
		    ZPOOL_DATA_COMPAT_D ".\n"));
		break;

	case ZPOOL_STATUS_INCOMPATIBLE_FEAT:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more features "
		    "are enabled on the pool despite not being\n\t"
		    "requested by the 'compatibility' property.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Consider setting "
		    "'compatibility' to an appropriate value, or\n\t"
		    "adding needed features to the relevant file in\n\t"
		    ZPOOL_SYSCONF_COMPAT_D " or " ZPOOL_DATA_COMPAT_D ".\n"));
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_READ:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("The pool cannot be accessed "
		    "on this system because it uses the\n\tfollowing feature(s)"
		    " not supported on this system:\n"));
		zpool_print_unsup_feat(config);
		(void) printf("\n");
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Access the pool from a "
		    "system that supports the required feature(s),\n\tor "
		    "restore the pool from backup.\n"));
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_WRITE:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("The pool can only be "
		    "accessed in read-only mode on this system. It\n\tcannot be"
		    " accessed in read-write mode because it uses the "
		    "following\n\tfeature(s) not supported on this system:\n"));
		zpool_print_unsup_feat(config);
		(void) printf("\n");
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("The pool cannot be accessed "
		    "in read-write mode. Import the pool with\n"
		    "\t\"-o readonly=on\", access the pool from a system that "
		    "supports the\n\trequired feature(s), or restore the "
		    "pool from backup.\n"));
		break;

	case ZPOOL_STATUS_FAULTED_DEV_R:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices are "
		    "faulted in response to persistent errors.\n\tSufficient "
		    "replicas exist for the pool to continue functioning "
		    "in a\n\tdegraded state.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Replace the faulted device, "
		    "or use 'zpool clear' to mark the device\n\trepaired.\n"));
		break;

	case ZPOOL_STATUS_FAULTED_DEV_NR:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices are "
		    "faulted in response to persistent errors.  There are "
		    "insufficient replicas for the pool to\n\tcontinue "
		    "functioning.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Destroy and re-create the "
		    "pool from a backup source.  Manually marking the device\n"
		    "\trepaired using 'zpool clear' may allow some data "
		    "to be recovered.\n"));
		break;

	case ZPOOL_STATUS_IO_FAILURE_MMP:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("The pool is suspended "
		    "because multihost writes failed or were delayed;\n\t"
		    "another system could import the pool undetected.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Make sure the pool's devices"
		    " are connected, then reboot your system and\n\timport the "
		    "pool.\n"));
		break;

	case ZPOOL_STATUS_IO_FAILURE_WAIT:
	case ZPOOL_STATUS_IO_FAILURE_CONTINUE:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices are "
		    "faulted in response to IO failures.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Make sure the affected "
		    "devices are connected, then run 'zpool clear'.\n"));
		break;

	case ZPOOL_STATUS_BAD_LOG:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("An intent log record "
		    "could not be read.\n"
		    "\tWaiting for administrator intervention to fix the "
		    "faulted pool.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Either restore the affected "
		    "device(s) and run 'zpool online',\n"
		    "\tor ignore the intent log records by running "
		    "'zpool clear'.\n"));
		break;

	case ZPOOL_STATUS_NON_NATIVE_ASHIFT:
		(void) printf(gettext("status: One or more devices are "
		    "configured to use a non-native block size.\n"
		    "\tExpect reduced performance.\n"));
		(void) printf(gettext("action: Replace affected devices with "
		    "devices that support the\n\tconfigured block size, or "
		    "migrate data to a properly configured\n\tpool.\n"));
		break;

	case ZPOOL_STATUS_HOSTID_MISMATCH:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("Mismatch between pool hostid"
		    " and system hostid on imported pool.\n\tThis pool was "
		    "previously imported into a system with a different "
		    "hostid,\n\tand then was verbatim imported into this "
		    "system.\n"));
		printf_color(ANSI_BOLD, gettext("action: "));
		printf_color(ANSI_YELLOW, gettext("Export this pool on all "
		    "systems on which it is imported.\n"
		    "\tThen import it to correct the mismatch.\n"));
		break;

	case ZPOOL_STATUS_ERRATA:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("Errata #%d detected.\n"),
		    errata);

		switch (errata) {
		case ZPOOL_ERRATA_NONE:
			break;

		case ZPOOL_ERRATA_ZOL_2094_SCRUB:
			printf_color(ANSI_BOLD, gettext("action: "));
			printf_color(ANSI_YELLOW, gettext("To correct the issue"
			    " run 'zpool scrub'.\n"));
			break;

		case ZPOOL_ERRATA_ZOL_6845_ENCRYPTION:
			(void) printf(gettext("\tExisting encrypted datasets "
			    "contain an on-disk incompatibility\n\twhich "
			    "needs to be corrected.\n"));
			printf_color(ANSI_BOLD, gettext("action: "));
			printf_color(ANSI_YELLOW, gettext("To correct the issue"
			    " backup existing encrypted datasets to new\n\t"
			    "encrypted datasets and destroy the old ones. "
			    "'zfs mount -o ro' can\n\tbe used to temporarily "
			    "mount existing encrypted datasets readonly.\n"));
			break;

		case ZPOOL_ERRATA_ZOL_8308_ENCRYPTION:
			(void) printf(gettext("\tExisting encrypted snapshots "
			    "and bookmarks contain an on-disk\n\tincompat"
			    "ibility. This may cause on-disk corruption if "
			    "they are used\n\twith 'zfs recv'.\n"));
			printf_color(ANSI_BOLD, gettext("action: "));
			printf_color(ANSI_YELLOW, gettext("To correct the"
			    "issue, enable the bookmark_v2 feature. No "
			    "additional\n\taction is needed if there are no "
			    "encrypted snapshots or bookmarks.\n\tIf preserving"
			    "the encrypted snapshots and bookmarks is required,"
			    " use\n\ta non-raw send to backup and restore them."
			    " Alternately, they may be\n\tremoved to resolve "
			    "the incompatibility.\n"));
			break;

		default:
			/*
			 * All errata which allow the pool to be imported
			 * must contain an action message.
			 */
			assert(0);
		}
		break;

	default:
		/*
		 * The remaining errors can't actually be generated, yet.
		 */
		assert(reason == ZPOOL_STATUS_OK);
	}

	if (msgid != NULL) {
		printf("   ");
		printf_color(ANSI_BOLD, gettext("see:"));
		printf(gettext(
		    " https://openzfs.github.io/openzfs-docs/msg/%s\n"),
		    msgid);
	}

	if (config != NULL) {
		uint64_t nerr;
		nvlist_t **spares, **l2cache;
		uint_t nspares, nl2cache;

		print_scan_status(zhp, nvroot);

		pool_removal_stat_t *prs = NULL;
		(void) nvlist_lookup_uint64_array(nvroot,
		    ZPOOL_CONFIG_REMOVAL_STATS, (uint64_t **)&prs, &c);
		print_removal_status(zhp, prs);

		pool_checkpoint_stat_t *pcs = NULL;
		(void) nvlist_lookup_uint64_array(nvroot,
		    ZPOOL_CONFIG_CHECKPOINT_STATS, (uint64_t **)&pcs, &c);
		print_checkpoint_status(pcs);

		pool_raidz_expand_stat_t *pres = NULL;
		(void) nvlist_lookup_uint64_array(nvroot,
		    ZPOOL_CONFIG_RAIDZ_EXPAND_STATS, (uint64_t **)&pres, &c);
		print_raidz_expand_status(zhp, pres);

		cbp->cb_namewidth = max_width(zhp, nvroot, 0, 0,
		    cbp->cb_name_flags | VDEV_NAME_TYPE_ID);
		if (cbp->cb_namewidth < 10)
			cbp->cb_namewidth = 10;

		color_start(ANSI_BOLD);
		(void) printf(gettext("config:\n\n"));
		(void) printf(gettext("\t%-*s  %-8s %5s %5s %5s"),
		    cbp->cb_namewidth, "NAME", "STATE", "READ", "WRITE",
		    "CKSUM");
		color_end();

		if (cbp->cb_print_slow_ios) {
			printf_color(ANSI_BOLD, " %5s", gettext("SLOW"));
		}

		if (cbp->cb_print_power) {
			printf_color(ANSI_BOLD, " %5s", gettext("POWER"));
		}

		if (cbp->vcdl != NULL)
			print_cmd_columns(cbp->vcdl, 0);

		printf("\n");

		print_status_config(zhp, cbp, zpool_get_name(zhp), nvroot, 0,
		    B_FALSE, NULL);

		print_class_vdevs(zhp, cbp, nvroot, VDEV_ALLOC_BIAS_DEDUP);
		print_class_vdevs(zhp, cbp, nvroot, VDEV_ALLOC_BIAS_SPECIAL);
		print_class_vdevs(zhp, cbp, nvroot, VDEV_ALLOC_CLASS_LOGS);

		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2cache, &nl2cache) == 0)
			print_l2cache(zhp, cbp, l2cache, nl2cache);

		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    &spares, &nspares) == 0)
			print_spares(zhp, cbp, spares, nspares);

		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_ERRCOUNT,
		    &nerr) == 0) {
			(void) printf("\n");
			if (nerr == 0) {
				(void) printf(gettext(
				    "errors: No known data errors\n"));
			} else if (!cbp->cb_verbose) {
				color_start(ANSI_RED);
				(void) printf(gettext("errors: %llu data "
				    "errors, use '-v' for a list\n"),
				    (u_longlong_t)nerr);
				color_end();
			} else {
				print_error_log(zhp);
			}
		}

		if (cbp->cb_dedup_stats)
			print_dedup_stats(config);
	} else {
		(void) printf(gettext("config: The configuration cannot be "
		    "determined.\n"));
	}

	return (0);
}

/*
 * zpool status [-c [script1,script2,...]] [-igLpPstvx] [--power] [-T d|u] ...
 *              [pool] [interval [count]]
 *
 *	-c CMD	For each vdev, run command CMD
 *	-e	Display only unhealthy vdevs
 *	-i	Display vdev initialization status.
 *	-g	Display guid for individual vdev name.
 *	-L	Follow links when resolving vdev path name.
 *	-p	Display values in parsable (exact) format.
 *	-P	Display full path for vdev name.
 *	-s	Display slow IOs column.
 *	-v	Display complete error logs
 *	-x	Display only pools with potential problems
 *	-D	Display dedup status (undocumented)
 *	-t	Display vdev TRIM status.
 *	-T	Display a timestamp in date(1) or Unix format
 *	--power	Display vdev enclosure slot power status
 *
 * Describes the health status of all pools or some subset.
 */
int
zpool_do_status(int argc, char **argv)
{
	int c;
	int ret;
	float interval = 0;
	unsigned long count = 0;
	status_cbdata_t cb = { 0 };
	char *cmd = NULL;

	struct option long_options[] = {
		{"power", no_argument, NULL, POWER_OPT},
		{0, 0, 0, 0}
	};

	/* check options */
	while ((c = getopt_long(argc, argv, "c:eigLpPsvxDtT:", long_options,
	    NULL)) != -1) {
		switch (c) {
		case 'c':
			if (cmd != NULL) {
				fprintf(stderr,
				    gettext("Can't set -c flag twice\n"));
				exit(1);
			}

			if (getenv("ZPOOL_SCRIPTS_ENABLED") != NULL &&
			    !libzfs_envvar_is_set("ZPOOL_SCRIPTS_ENABLED")) {
				fprintf(stderr, gettext(
				    "Can't run -c, disabled by "
				    "ZPOOL_SCRIPTS_ENABLED.\n"));
				exit(1);
			}

			if ((getuid() <= 0 || geteuid() <= 0) &&
			    !libzfs_envvar_is_set("ZPOOL_SCRIPTS_AS_ROOT")) {
				fprintf(stderr, gettext(
				    "Can't run -c with root privileges "
				    "unless ZPOOL_SCRIPTS_AS_ROOT is set.\n"));
				exit(1);
			}
			cmd = optarg;
			break;
		case 'e':
			cb.cb_print_unhealthy = B_TRUE;
			break;
		case 'i':
			cb.cb_print_vdev_init = B_TRUE;
			break;
		case 'g':
			cb.cb_name_flags |= VDEV_NAME_GUID;
			break;
		case 'L':
			cb.cb_name_flags |= VDEV_NAME_FOLLOW_LINKS;
			break;
		case 'p':
			cb.cb_literal = B_TRUE;
			break;
		case 'P':
			cb.cb_name_flags |= VDEV_NAME_PATH;
			break;
		case 's':
			cb.cb_print_slow_ios = B_TRUE;
			break;
		case 'v':
			cb.cb_verbose = B_TRUE;
			break;
		case 'x':
			cb.cb_explain = B_TRUE;
			break;
		case 'D':
			cb.cb_dedup_stats = B_TRUE;
			break;
		case 't':
			cb.cb_print_vdev_trim = B_TRUE;
			break;
		case 'T':
			get_timestamp_arg(*optarg);
			break;
		case POWER_OPT:
			cb.cb_print_power = B_TRUE;
			break;
		case '?':
			if (optopt == 'c') {
				print_zpool_script_list("status");
				exit(0);
			} else {
				fprintf(stderr,
				    gettext("invalid option '%c'\n"), optopt);
			}
			zpool_usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	get_interval_count(&argc, argv, &interval, &count);

	if (argc == 0)
		cb.cb_allpools = B_TRUE;

	cb.cb_first = B_TRUE;
	cb.cb_print_status = B_TRUE;

	for (;;) {
		if (timestamp_fmt != NODATE)
			print_timestamp(timestamp_fmt);

		if (cmd != NULL)
			cb.vcdl = all_pools_for_each_vdev_run(argc, argv, cmd,
			    NULL, NULL, 0, 0);

		ret = for_each_pool(argc, argv, B_TRUE, NULL, ZFS_TYPE_POOL,
		    cb.cb_literal, status_callback, &cb);

		if (cb.vcdl != NULL)
			free_vdev_cmd_data_list(cb.vcdl);

		if (argc == 0 && cb.cb_count == 0)
			(void) fprintf(stderr, gettext("no pools available\n"));
		else if (cb.cb_explain && cb.cb_first && cb.cb_allpools)
			(void) printf(gettext("all pools are healthy\n"));

		if (ret != 0)
			return (ret);

		if (interval == 0)
			break;

		if (count != 0 && --count == 0)
			break;

		(void) fflush(stdout);
		(void) fsleep(interval);
	}

	return (0);
}
