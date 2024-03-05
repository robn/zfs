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

#define	CHECKPOINT_OPT	1024

/*
 * Display the status for the given pool.
 */
static int
show_import(nvlist_t *config, boolean_t report_error)
{
	uint64_t pool_state;
	vdev_stat_t *vs;
	const char *name;
	uint64_t guid;
	uint64_t hostid = 0;
	const char *msgid;
	const char *hostname = "unknown";
	nvlist_t *nvroot, *nvinfo;
	zpool_status_t reason;
	zpool_errata_t errata;
	const char *health;
	uint_t vsc;
	const char *comment;
	status_cbdata_t cb = { 0 };

	verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
	    &name) == 0);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
	    &guid) == 0);
	verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
	    &pool_state) == 0);
	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);

	verify(nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &vsc) == 0);
	health = zpool_state_to_name(vs->vs_state, vs->vs_aux);

	reason = zpool_import_status(config, &msgid, &errata);

	/*
	 * If we're importing using a cachefile, then we won't report any
	 * errors unless we are in the scan phase of the import.
	 */
	if (reason != ZPOOL_STATUS_OK && !report_error)
		return (reason);

	(void) printf(gettext("   pool: %s\n"), name);
	(void) printf(gettext("     id: %llu\n"), (u_longlong_t)guid);
	(void) printf(gettext("  state: %s"), health);
	if (pool_state == POOL_STATE_DESTROYED)
		(void) printf(gettext(" (DESTROYED)"));
	(void) printf("\n");

	switch (reason) {
	case ZPOOL_STATUS_MISSING_DEV_R:
	case ZPOOL_STATUS_MISSING_DEV_NR:
	case ZPOOL_STATUS_BAD_GUID_SUM:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices are "
		    "missing from the system.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_LABEL_R:
	case ZPOOL_STATUS_CORRUPT_LABEL_NR:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices contains"
		    " corrupted data.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_DATA:
		(void) printf(
		    gettext(" status: The pool data is corrupted.\n"));
		break;

	case ZPOOL_STATUS_OFFLINE_DEV:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices "
		    "are offlined.\n"));
		break;

	case ZPOOL_STATUS_CORRUPT_POOL:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("The pool metadata is "
		    "corrupted.\n"));
		break;

	case ZPOOL_STATUS_VERSION_OLDER:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("The pool is formatted using "
		    "a legacy on-disk version.\n"));
		break;

	case ZPOOL_STATUS_VERSION_NEWER:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("The pool is formatted using "
		    "an incompatible version.\n"));
		break;

	case ZPOOL_STATUS_FEAT_DISABLED:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("Some supported "
		    "features are not enabled on the pool.\n\t"
		    "(Note that they may be intentionally disabled "
		    "if the\n\t'compatibility' property is set.)\n"));
		break;

	case ZPOOL_STATUS_COMPATIBILITY_ERR:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("Error reading or parsing "
		    "the file(s) indicated by the 'compatibility'\n"
		    "property.\n"));
		break;

	case ZPOOL_STATUS_INCOMPATIBLE_FEAT:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more features "
		    "are enabled on the pool despite not being\n"
		    "requested by the 'compatibility' property.\n"));
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_READ:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("The pool uses the following "
		    "feature(s) not supported on this system:\n"));
		color_start(ANSI_YELLOW);
		zpool_print_unsup_feat(config);
		color_end();
		break;

	case ZPOOL_STATUS_UNSUP_FEAT_WRITE:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("The pool can only be "
		    "accessed in read-only mode on this system. It\n\tcannot be"
		    " accessed in read-write mode because it uses the "
		    "following\n\tfeature(s) not supported on this system:\n"));
		color_start(ANSI_YELLOW);
		zpool_print_unsup_feat(config);
		color_end();
		break;

	case ZPOOL_STATUS_HOSTID_ACTIVE:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("The pool is currently "
		    "imported by another system.\n"));
		break;

	case ZPOOL_STATUS_HOSTID_REQUIRED:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("The pool has the "
		    "multihost property on.  It cannot\n\tbe safely imported "
		    "when the system hostid is not set.\n"));
		break;

	case ZPOOL_STATUS_HOSTID_MISMATCH:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("The pool was last accessed "
		    "by another system.\n"));
		break;

	case ZPOOL_STATUS_FAULTED_DEV_R:
	case ZPOOL_STATUS_FAULTED_DEV_NR:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices are "
		    "faulted.\n"));
		break;

	case ZPOOL_STATUS_BAD_LOG:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("An intent log record cannot "
		    "be read.\n"));
		break;

	case ZPOOL_STATUS_RESILVERING:
	case ZPOOL_STATUS_REBUILDING:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices were "
		    "being resilvered.\n"));
		break;

	case ZPOOL_STATUS_ERRATA:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("Errata #%d detected.\n"),
		    errata);
		break;

	case ZPOOL_STATUS_NON_NATIVE_ASHIFT:
		printf_color(ANSI_BOLD, gettext("status: "));
		printf_color(ANSI_YELLOW, gettext("One or more devices are "
		    "configured to use a non-native block size.\n"
		    "\tExpect reduced performance.\n"));
		break;

	default:
		/*
		 * No other status can be seen when importing pools.
		 */
		assert(reason == ZPOOL_STATUS_OK);
	}

	/*
	 * Print out an action according to the overall state of the pool.
	 */
	if (vs->vs_state == VDEV_STATE_HEALTHY) {
		if (reason == ZPOOL_STATUS_VERSION_OLDER ||
		    reason == ZPOOL_STATUS_FEAT_DISABLED) {
			(void) printf(gettext(" action: The pool can be "
			    "imported using its name or numeric identifier, "
			    "though\n\tsome features will not be available "
			    "without an explicit 'zpool upgrade'.\n"));
		} else if (reason == ZPOOL_STATUS_COMPATIBILITY_ERR) {
			(void) printf(gettext(" action: The pool can be "
			    "imported using its name or numeric\n\tidentifier, "
			    "though the file(s) indicated by its "
			    "'compatibility'\n\tproperty cannot be parsed at "
			    "this time.\n"));
		} else if (reason == ZPOOL_STATUS_HOSTID_MISMATCH) {
			(void) printf(gettext(" action: The pool can be "
			    "imported using its name or numeric "
			    "identifier and\n\tthe '-f' flag.\n"));
		} else if (reason == ZPOOL_STATUS_ERRATA) {
			switch (errata) {
			case ZPOOL_ERRATA_NONE:
				break;

			case ZPOOL_ERRATA_ZOL_2094_SCRUB:
				(void) printf(gettext(" action: The pool can "
				    "be imported using its name or numeric "
				    "identifier,\n\thowever there is a compat"
				    "ibility issue which should be corrected"
				    "\n\tby running 'zpool scrub'\n"));
				break;

			case ZPOOL_ERRATA_ZOL_2094_ASYNC_DESTROY:
				(void) printf(gettext(" action: The pool can"
				    "not be imported with this version of ZFS "
				    "due to\n\tan active asynchronous destroy. "
				    "Revert to an earlier version\n\tand "
				    "allow the destroy to complete before "
				    "updating.\n"));
				break;

			case ZPOOL_ERRATA_ZOL_6845_ENCRYPTION:
				(void) printf(gettext(" action: Existing "
				    "encrypted datasets contain an on-disk "
				    "incompatibility, which\n\tneeds to be "
				    "corrected. Backup these datasets to new "
				    "encrypted datasets\n\tand destroy the "
				    "old ones.\n"));
				break;

			case ZPOOL_ERRATA_ZOL_8308_ENCRYPTION:
				(void) printf(gettext(" action: Existing "
				    "encrypted snapshots and bookmarks contain "
				    "an on-disk\n\tincompatibility. This may "
				    "cause on-disk corruption if they are used"
				    "\n\twith 'zfs recv'. To correct the "
				    "issue, enable the bookmark_v2 feature.\n\t"
				    "No additional action is needed if there "
				    "are no encrypted snapshots or\n\t"
				    "bookmarks. If preserving the encrypted "
				    "snapshots and bookmarks is\n\trequired, "
				    "use a non-raw send to backup and restore "
				    "them. Alternately,\n\tthey may be removed"
				    " to resolve the incompatibility.\n"));
				break;
			default:
				/*
				 * All errata must contain an action message.
				 */
				assert(0);
			}
		} else {
			(void) printf(gettext(" action: The pool can be "
			    "imported using its name or numeric "
			    "identifier.\n"));
		}
	} else if (vs->vs_state == VDEV_STATE_DEGRADED) {
		(void) printf(gettext(" action: The pool can be imported "
		    "despite missing or damaged devices.  The\n\tfault "
		    "tolerance of the pool may be compromised if imported.\n"));
	} else {
		switch (reason) {
		case ZPOOL_STATUS_VERSION_NEWER:
			(void) printf(gettext(" action: The pool cannot be "
			    "imported.  Access the pool on a system running "
			    "newer\n\tsoftware, or recreate the pool from "
			    "backup.\n"));
			break;
		case ZPOOL_STATUS_UNSUP_FEAT_READ:
			printf_color(ANSI_BOLD, gettext("action: "));
			printf_color(ANSI_YELLOW, gettext("The pool cannot be "
			    "imported. Access the pool on a system that "
			    "supports\n\tthe required feature(s), or recreate "
			    "the pool from backup.\n"));
			break;
		case ZPOOL_STATUS_UNSUP_FEAT_WRITE:
			printf_color(ANSI_BOLD, gettext("action: "));
			printf_color(ANSI_YELLOW, gettext("The pool cannot be "
			    "imported in read-write mode. Import the pool "
			    "with\n"
			    "\t\"-o readonly=on\", access the pool on a system "
			    "that supports the\n\trequired feature(s), or "
			    "recreate the pool from backup.\n"));
			break;
		case ZPOOL_STATUS_MISSING_DEV_R:
		case ZPOOL_STATUS_MISSING_DEV_NR:
		case ZPOOL_STATUS_BAD_GUID_SUM:
			(void) printf(gettext(" action: The pool cannot be "
			    "imported. Attach the missing\n\tdevices and try "
			    "again.\n"));
			break;
		case ZPOOL_STATUS_HOSTID_ACTIVE:
			VERIFY0(nvlist_lookup_nvlist(config,
			    ZPOOL_CONFIG_LOAD_INFO, &nvinfo));

			if (nvlist_exists(nvinfo, ZPOOL_CONFIG_MMP_HOSTNAME))
				hostname = fnvlist_lookup_string(nvinfo,
				    ZPOOL_CONFIG_MMP_HOSTNAME);

			if (nvlist_exists(nvinfo, ZPOOL_CONFIG_MMP_HOSTID))
				hostid = fnvlist_lookup_uint64(nvinfo,
				    ZPOOL_CONFIG_MMP_HOSTID);

			(void) printf(gettext(" action: The pool must be "
			    "exported from %s (hostid=%"PRIx64")\n\tbefore it "
			    "can be safely imported.\n"), hostname, hostid);
			break;
		case ZPOOL_STATUS_HOSTID_REQUIRED:
			(void) printf(gettext(" action: Set a unique system "
			    "hostid with the zgenhostid(8) command.\n"));
			break;
		default:
			(void) printf(gettext(" action: The pool cannot be "
			    "imported due to damaged devices or data.\n"));
		}
	}

	/* Print the comment attached to the pool. */
	if (nvlist_lookup_string(config, ZPOOL_CONFIG_COMMENT, &comment) == 0)
		(void) printf(gettext("comment: %s\n"), comment);

	/*
	 * If the state is "closed" or "can't open", and the aux state
	 * is "corrupt data":
	 */
	if (((vs->vs_state == VDEV_STATE_CLOSED) ||
	    (vs->vs_state == VDEV_STATE_CANT_OPEN)) &&
	    (vs->vs_aux == VDEV_AUX_CORRUPT_DATA)) {
		if (pool_state == POOL_STATE_DESTROYED)
			(void) printf(gettext("\tThe pool was destroyed, "
			    "but can be imported using the '-Df' flags.\n"));
		else if (pool_state != POOL_STATE_EXPORTED)
			(void) printf(gettext("\tThe pool may be active on "
			    "another system, but can be imported using\n\t"
			    "the '-f' flag.\n"));
	}

	if (msgid != NULL) {
		(void) printf(gettext(
		    "   see: https://openzfs.github.io/openzfs-docs/msg/%s\n"),
		    msgid);
	}

	(void) printf(gettext(" config:\n\n"));

	cb.cb_namewidth = max_width(NULL, nvroot, 0, strlen(name),
	    VDEV_NAME_TYPE_ID);
	if (cb.cb_namewidth < 10)
		cb.cb_namewidth = 10;

	print_import_config(&cb, name, nvroot, 0);

	print_class_vdevs(NULL, &cb, nvroot, VDEV_ALLOC_BIAS_DEDUP);
	print_class_vdevs(NULL, &cb, nvroot, VDEV_ALLOC_BIAS_SPECIAL);
	print_class_vdevs(NULL, &cb, nvroot, VDEV_ALLOC_CLASS_LOGS);

	if (reason == ZPOOL_STATUS_BAD_GUID_SUM) {
		(void) printf(gettext("\n\tAdditional devices are known to "
		    "be part of this pool, though their\n\texact "
		    "configuration cannot be determined.\n"));
	}
	return (0);
}

static boolean_t
zfs_force_import_required(nvlist_t *config)
{
	uint64_t state;
	uint64_t hostid = 0;
	nvlist_t *nvinfo;

	state = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE);
	nvinfo = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_LOAD_INFO);

	/*
	 * The hostid on LOAD_INFO comes from the MOS label via
	 * spa_tryimport(). If its not there then we're likely talking to an
	 * older kernel, so use the top one, which will be from the label
	 * discovered in zpool_find_import(), or if a cachefile is in use, the
	 * local hostid.
	 */
	if (nvlist_lookup_uint64(nvinfo, ZPOOL_CONFIG_HOSTID, &hostid) != 0)
		(void) nvlist_lookup_uint64(config, ZPOOL_CONFIG_HOSTID,
		    &hostid);

	if (state != POOL_STATE_EXPORTED && hostid != get_system_hostid())
		return (B_TRUE);

	if (nvlist_exists(nvinfo, ZPOOL_CONFIG_MMP_STATE)) {
		mmp_state_t mmp_state = fnvlist_lookup_uint64(nvinfo,
		    ZPOOL_CONFIG_MMP_STATE);

		if (mmp_state != MMP_STATE_INACTIVE)
			return (B_TRUE);
	}

	return (B_FALSE);
}

/*
 * Perform the import for the given configuration.  This passes the heavy
 * lifting off to zpool_import_props(), and then mounts the datasets contained
 * within the pool.
 */
static int
do_import(nvlist_t *config, const char *newname, const char *mntopts,
    nvlist_t *props, int flags)
{
	int ret = 0;
	int ms_status = 0;
	zpool_handle_t *zhp;
	const char *name;
	uint64_t version;

	name = fnvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME);
	version = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_VERSION);

	if (!SPA_VERSION_IS_SUPPORTED(version)) {
		(void) fprintf(stderr, gettext("cannot import '%s': pool "
		    "is formatted using an unsupported ZFS version\n"), name);
		return (1);
	} else if (zfs_force_import_required(config) &&
	    !(flags & ZFS_IMPORT_ANY_HOST)) {
		mmp_state_t mmp_state = MMP_STATE_INACTIVE;
		nvlist_t *nvinfo;

		nvinfo = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_LOAD_INFO);
		if (nvlist_exists(nvinfo, ZPOOL_CONFIG_MMP_STATE))
			mmp_state = fnvlist_lookup_uint64(nvinfo,
			    ZPOOL_CONFIG_MMP_STATE);

		if (mmp_state == MMP_STATE_ACTIVE) {
			const char *hostname = "<unknown>";
			uint64_t hostid = 0;

			if (nvlist_exists(nvinfo, ZPOOL_CONFIG_MMP_HOSTNAME))
				hostname = fnvlist_lookup_string(nvinfo,
				    ZPOOL_CONFIG_MMP_HOSTNAME);

			if (nvlist_exists(nvinfo, ZPOOL_CONFIG_MMP_HOSTID))
				hostid = fnvlist_lookup_uint64(nvinfo,
				    ZPOOL_CONFIG_MMP_HOSTID);

			(void) fprintf(stderr, gettext("cannot import '%s': "
			    "pool is imported on %s (hostid: "
			    "0x%"PRIx64")\nExport the pool on the other "
			    "system, then run 'zpool import'.\n"),
			    name, hostname, hostid);
		} else if (mmp_state == MMP_STATE_NO_HOSTID) {
			(void) fprintf(stderr, gettext("Cannot import '%s': "
			    "pool has the multihost property on and the\n"
			    "system's hostid is not set. Set a unique hostid "
			    "with the zgenhostid(8) command.\n"), name);
		} else {
			const char *hostname = "<unknown>";
			time_t timestamp = 0;
			uint64_t hostid = 0;

			if (nvlist_exists(nvinfo, ZPOOL_CONFIG_HOSTNAME))
				hostname = fnvlist_lookup_string(nvinfo,
				    ZPOOL_CONFIG_HOSTNAME);
			else if (nvlist_exists(config, ZPOOL_CONFIG_HOSTNAME))
				hostname = fnvlist_lookup_string(config,
				    ZPOOL_CONFIG_HOSTNAME);

			if (nvlist_exists(config, ZPOOL_CONFIG_TIMESTAMP))
				timestamp = fnvlist_lookup_uint64(config,
				    ZPOOL_CONFIG_TIMESTAMP);

			if (nvlist_exists(nvinfo, ZPOOL_CONFIG_HOSTID))
				hostid = fnvlist_lookup_uint64(nvinfo,
				    ZPOOL_CONFIG_HOSTID);
			else if (nvlist_exists(config, ZPOOL_CONFIG_HOSTID))
				hostid = fnvlist_lookup_uint64(config,
				    ZPOOL_CONFIG_HOSTID);

			(void) fprintf(stderr, gettext("cannot import '%s': "
			    "pool was previously in use from another system.\n"
			    "Last accessed by %s (hostid=%"PRIx64") at %s"
			    "The pool can be imported, use 'zpool import -f' "
			    "to import the pool.\n"), name, hostname,
			    hostid, ctime(&timestamp));
		}

		return (1);
	}

	if (zpool_import_props(g_zfs, config, newname, props, flags) != 0)
		return (1);

	if (newname != NULL)
		name = newname;

	if ((zhp = zpool_open_canfail(g_zfs, name)) == NULL)
		return (1);

	/*
	 * Loading keys is best effort. We don't want to return immediately
	 * if it fails but we do want to give the error to the caller.
	 */
	if (flags & ZFS_IMPORT_LOAD_KEYS &&
	    zfs_crypto_attempt_load_keys(g_zfs, name) != 0)
			ret = 1;

	if (zpool_get_state(zhp) != POOL_STATE_UNAVAIL &&
	    !(flags & ZFS_IMPORT_ONLY)) {
		ms_status = zpool_enable_datasets(zhp, mntopts, 0);
		if (ms_status == EZFS_SHAREFAILED) {
			(void) fprintf(stderr, gettext("Import was "
			    "successful, but unable to share some datasets"));
		} else if (ms_status == EZFS_MOUNTFAILED) {
			(void) fprintf(stderr, gettext("Import was "
			    "successful, but unable to mount some datasets"));
		}
	}

	zpool_close(zhp);
	return (ret);
}

static int
import_pools(nvlist_t *pools, nvlist_t *props, char *mntopts, int flags,
    char *orig_name, char *new_name,
    boolean_t do_destroyed, boolean_t pool_specified, boolean_t do_all,
    importargs_t *import)
{
	nvlist_t *config = NULL;
	nvlist_t *found_config = NULL;
	uint64_t pool_state;

	/*
	 * At this point we have a list of import candidate configs. Even if
	 * we were searching by pool name or guid, we still need to
	 * post-process the list to deal with pool state and possible
	 * duplicate names.
	 */
	int err = 0;
	nvpair_t *elem = NULL;
	boolean_t first = B_TRUE;
	while ((elem = nvlist_next_nvpair(pools, elem)) != NULL) {

		verify(nvpair_value_nvlist(elem, &config) == 0);

		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
		    &pool_state) == 0);
		if (!do_destroyed && pool_state == POOL_STATE_DESTROYED)
			continue;
		if (do_destroyed && pool_state != POOL_STATE_DESTROYED)
			continue;

		verify(nvlist_add_nvlist(config, ZPOOL_LOAD_POLICY,
		    import->policy) == 0);

		if (!pool_specified) {
			if (first)
				first = B_FALSE;
			else if (!do_all)
				(void) fputc('\n', stdout);

			if (do_all) {
				err |= do_import(config, NULL, mntopts,
				    props, flags);
			} else {
				/*
				 * If we're importing from cachefile, then
				 * we don't want to report errors until we
				 * are in the scan phase of the import. If
				 * we get an error, then we return that error
				 * to invoke the scan phase.
				 */
				if (import->cachefile && !import->scan)
					err = show_import(config, B_FALSE);
				else
					(void) show_import(config, B_TRUE);
			}
		} else if (import->poolname != NULL) {
			const char *name;

			/*
			 * We are searching for a pool based on name.
			 */
			verify(nvlist_lookup_string(config,
			    ZPOOL_CONFIG_POOL_NAME, &name) == 0);

			if (strcmp(name, import->poolname) == 0) {
				if (found_config != NULL) {
					(void) fprintf(stderr, gettext(
					    "cannot import '%s': more than "
					    "one matching pool\n"),
					    import->poolname);
					(void) fprintf(stderr, gettext(
					    "import by numeric ID instead\n"));
					err = B_TRUE;
				}
				found_config = config;
			}
		} else {
			uint64_t guid;

			/*
			 * Search for a pool by guid.
			 */
			verify(nvlist_lookup_uint64(config,
			    ZPOOL_CONFIG_POOL_GUID, &guid) == 0);

			if (guid == import->guid)
				found_config = config;
		}
	}

	/*
	 * If we were searching for a specific pool, verify that we found a
	 * pool, and then do the import.
	 */
	if (pool_specified && err == 0) {
		if (found_config == NULL) {
			(void) fprintf(stderr, gettext("cannot import '%s': "
			    "no such pool available\n"), orig_name);
			err = B_TRUE;
		} else {
			err |= do_import(found_config, new_name,
			    mntopts, props, flags);
		}
	}

	/*
	 * If we were just looking for pools, report an error if none were
	 * found.
	 */
	if (!pool_specified && first)
		(void) fprintf(stderr,
		    gettext("no pools available to import\n"));
	return (err);
}

typedef struct target_exists_args {
	const char	*poolname;
	uint64_t	poolguid;
} target_exists_args_t;

static int
name_or_guid_exists(zpool_handle_t *zhp, void *data)
{
	target_exists_args_t *args = data;
	nvlist_t *config = zpool_get_config(zhp, NULL);
	int found = 0;

	if (config == NULL)
		return (0);

	if (args->poolname != NULL) {
		const char *pool_name;

		verify(nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME,
		    &pool_name) == 0);
		if (strcmp(pool_name, args->poolname) == 0)
			found = 1;
	} else {
		uint64_t pool_guid;

		verify(nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
		    &pool_guid) == 0);
		if (pool_guid == args->poolguid)
			found = 1;
	}
	zpool_close(zhp);

	return (found);
}

/*
 * zpool import [-d dir] [-D]
 *       import [-o mntopts] [-o prop=value] ... [-R root] [-D] [-l]
 *              [-d dir | -c cachefile | -s] [-f] -a
 *       import [-o mntopts] [-o prop=value] ... [-R root] [-D] [-l]
 *              [-d dir | -c cachefile | -s] [-f] [-n] [-F] <pool | id>
 *              [newpool]
 *
 *	-c	Read pool information from a cachefile instead of searching
 *		devices. If importing from a cachefile config fails, then
 *		fallback to searching for devices only in the directories that
 *		exist in the cachefile.
 *
 *	-d	Scan in a specific directory, other than /dev/.  More than
 *		one directory can be specified using multiple '-d' options.
 *
 *	-D	Scan for previously destroyed pools or import all or only
 *		specified destroyed pools.
 *
 *	-R	Temporarily import the pool, with all mountpoints relative to
 *		the given root.  The pool will remain exported when the machine
 *		is rebooted.
 *
 *	-V	Import even in the presence of faulted vdevs.  This is an
 *		intentionally undocumented option for testing purposes, and
 *		treats the pool configuration as complete, leaving any bad
 *		vdevs in the FAULTED state. In other words, it does verbatim
 *		import.
 *
 *	-f	Force import, even if it appears that the pool is active.
 *
 *	-F	Attempt rewind if necessary.
 *
 *	-n	See if rewind would work, but don't actually rewind.
 *
 *	-N	Import the pool but don't mount datasets.
 *
 *	-T	Specify a starting txg to use for import. This option is
 *		intentionally undocumented option for testing purposes.
 *
 *	-a	Import all pools found.
 *
 *	-l	Load encryption keys while importing.
 *
 *	-o	Set property=value and/or temporary mount options (without '=').
 *
 *	-s	Scan using the default search path, the libblkid cache will
 *		not be consulted.
 *
 *	--rewind-to-checkpoint
 *		Import the pool and revert back to the checkpoint.
 *
 * The import command scans for pools to import, and import pools based on pool
 * name and GUID.  The pool can also be renamed as part of the import process.
 */
int
zpool_do_import(int argc, char **argv)
{
	char **searchdirs = NULL;
	char *env, *envdup = NULL;
	int nsearch = 0;
	int c;
	int err = 0;
	nvlist_t *pools = NULL;
	boolean_t do_all = B_FALSE;
	boolean_t do_destroyed = B_FALSE;
	char *mntopts = NULL;
	uint64_t searchguid = 0;
	char *searchname = NULL;
	char *propval;
	nvlist_t *policy = NULL;
	nvlist_t *props = NULL;
	int flags = ZFS_IMPORT_NORMAL;
	uint32_t rewind_policy = ZPOOL_NO_REWIND;
	boolean_t dryrun = B_FALSE;
	boolean_t do_rewind = B_FALSE;
	boolean_t xtreme_rewind = B_FALSE;
	boolean_t do_scan = B_FALSE;
	boolean_t pool_exists = B_FALSE;
	boolean_t pool_specified = B_FALSE;
	uint64_t txg = -1ULL;
	char *cachefile = NULL;
	importargs_t idata = { 0 };
	char *endptr;

	struct option long_options[] = {
		{"rewind-to-checkpoint", no_argument, NULL, CHECKPOINT_OPT},
		{0, 0, 0, 0}
	};

	/* check options */
	while ((c = getopt_long(argc, argv, ":aCc:d:DEfFlmnNo:R:stT:VX",
	    long_options, NULL)) != -1) {
		switch (c) {
		case 'a':
			do_all = B_TRUE;
			break;
		case 'c':
			cachefile = optarg;
			break;
		case 'd':
			searchdirs = safe_realloc(searchdirs,
			    (nsearch + 1) * sizeof (char *));
			searchdirs[nsearch++] = optarg;
			break;
		case 'D':
			do_destroyed = B_TRUE;
			break;
		case 'f':
			flags |= ZFS_IMPORT_ANY_HOST;
			break;
		case 'F':
			do_rewind = B_TRUE;
			break;
		case 'l':
			flags |= ZFS_IMPORT_LOAD_KEYS;
			break;
		case 'm':
			flags |= ZFS_IMPORT_MISSING_LOG;
			break;
		case 'n':
			dryrun = B_TRUE;
			break;
		case 'N':
			flags |= ZFS_IMPORT_ONLY;
			break;
		case 'o':
			if ((propval = strchr(optarg, '=')) != NULL) {
				*propval = '\0';
				propval++;
				if (add_prop_list(optarg, propval,
				    &props, B_TRUE))
					goto error;
			} else {
				mntopts = optarg;
			}
			break;
		case 'R':
			if (add_prop_list(zpool_prop_to_name(
			    ZPOOL_PROP_ALTROOT), optarg, &props, B_TRUE))
				goto error;
			if (add_prop_list_default(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE), "none", &props))
				goto error;
			break;
		case 's':
			do_scan = B_TRUE;
			break;
		case 't':
			flags |= ZFS_IMPORT_TEMP_NAME;
			if (add_prop_list_default(zpool_prop_to_name(
			    ZPOOL_PROP_CACHEFILE), "none", &props))
				goto error;
			break;

		case 'T':
			errno = 0;
			txg = strtoull(optarg, &endptr, 0);
			if (errno != 0 || *endptr != '\0') {
				(void) fprintf(stderr,
				    gettext("invalid txg value\n"));
				zpool_usage(B_FALSE);
			}
			rewind_policy = ZPOOL_DO_REWIND | ZPOOL_EXTREME_REWIND;
			break;
		case 'V':
			flags |= ZFS_IMPORT_VERBATIM;
			break;
		case 'X':
			xtreme_rewind = B_TRUE;
			break;
		case CHECKPOINT_OPT:
			flags |= ZFS_IMPORT_CHECKPOINT;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			zpool_usage(B_FALSE);
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			zpool_usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if (cachefile && nsearch != 0) {
		(void) fprintf(stderr, gettext("-c is incompatible with -d\n"));
		zpool_usage(B_FALSE);
	}

	if (cachefile && do_scan) {
		(void) fprintf(stderr, gettext("-c is incompatible with -s\n"));
		zpool_usage(B_FALSE);
	}

	if ((flags & ZFS_IMPORT_LOAD_KEYS) && (flags & ZFS_IMPORT_ONLY)) {
		(void) fprintf(stderr, gettext("-l is incompatible with -N\n"));
		zpool_usage(B_FALSE);
	}

	if ((flags & ZFS_IMPORT_LOAD_KEYS) && !do_all && argc == 0) {
		(void) fprintf(stderr, gettext("-l is only meaningful during "
		    "an import\n"));
		zpool_usage(B_FALSE);
	}

	if ((dryrun || xtreme_rewind) && !do_rewind) {
		(void) fprintf(stderr,
		    gettext("-n or -X only meaningful with -F\n"));
		zpool_usage(B_FALSE);
	}
	if (dryrun)
		rewind_policy = ZPOOL_TRY_REWIND;
	else if (do_rewind)
		rewind_policy = ZPOOL_DO_REWIND;
	if (xtreme_rewind)
		rewind_policy |= ZPOOL_EXTREME_REWIND;

	/* In the future, we can capture further policy and include it here */
	if (nvlist_alloc(&policy, NV_UNIQUE_NAME, 0) != 0 ||
	    nvlist_add_uint64(policy, ZPOOL_LOAD_REQUEST_TXG, txg) != 0 ||
	    nvlist_add_uint32(policy, ZPOOL_LOAD_REWIND_POLICY,
	    rewind_policy) != 0)
		goto error;

	/* check argument count */
	if (do_all) {
		if (argc != 0) {
			(void) fprintf(stderr, gettext("too many arguments\n"));
			zpool_usage(B_FALSE);
		}
	} else {
		if (argc > 2) {
			(void) fprintf(stderr, gettext("too many arguments\n"));
			zpool_usage(B_FALSE);
		}
	}

	/*
	 * Check for the effective uid.  We do this explicitly here because
	 * otherwise any attempt to discover pools will silently fail.
	 */
	if (argc == 0 && geteuid() != 0) {
		(void) fprintf(stderr, gettext("cannot "
		    "discover pools: permission denied\n"));

		free(searchdirs);
		nvlist_free(props);
		nvlist_free(policy);
		return (1);
	}

	/*
	 * Depending on the arguments given, we do one of the following:
	 *
	 *	<none>	Iterate through all pools and display information about
	 *		each one.
	 *
	 *	-a	Iterate through all pools and try to import each one.
	 *
	 *	<id>	Find the pool that corresponds to the given GUID/pool
	 *		name and import that one.
	 *
	 *	-D	Above options applies only to destroyed pools.
	 */
	if (argc != 0) {
		char *endptr;

		errno = 0;
		searchguid = strtoull(argv[0], &endptr, 10);
		if (errno != 0 || *endptr != '\0') {
			searchname = argv[0];
			searchguid = 0;
		}
		pool_specified = B_TRUE;

		/*
		 * User specified a name or guid.  Ensure it's unique.
		 */
		target_exists_args_t search = {searchname, searchguid};
		pool_exists = zpool_iter(g_zfs, name_or_guid_exists, &search);
	}

	/*
	 * Check the environment for the preferred search path.
	 */
	if ((searchdirs == NULL) && (env = getenv("ZPOOL_IMPORT_PATH"))) {
		char *dir, *tmp = NULL;

		envdup = strdup(env);

		for (dir = strtok_r(envdup, ":", &tmp);
		    dir != NULL;
		    dir = strtok_r(NULL, ":", &tmp)) {
			searchdirs = safe_realloc(searchdirs,
			    (nsearch + 1) * sizeof (char *));
			searchdirs[nsearch++] = dir;
		}
	}

	idata.path = searchdirs;
	idata.paths = nsearch;
	idata.poolname = searchname;
	idata.guid = searchguid;
	idata.cachefile = cachefile;
	idata.scan = do_scan;
	idata.policy = policy;

	libpc_handle_t lpch = {
		.lpc_lib_handle = g_zfs,
		.lpc_ops = &libzfs_config_ops,
		.lpc_printerr = B_TRUE
	};
	pools = zpool_search_import(&lpch, &idata);

	if (pools != NULL && pool_exists &&
	    (argc == 1 || strcmp(argv[0], argv[1]) == 0)) {
		(void) fprintf(stderr, gettext("cannot import '%s': "
		    "a pool with that name already exists\n"),
		    argv[0]);
		(void) fprintf(stderr, gettext("use the form '%s "
		    "<pool | id> <newpool>' to give it a new name\n"),
		    "zpool import");
		err = 1;
	} else if (pools == NULL && pool_exists) {
		(void) fprintf(stderr, gettext("cannot import '%s': "
		    "a pool with that name is already created/imported,\n"),
		    argv[0]);
		(void) fprintf(stderr, gettext("and no additional pools "
		    "with that name were found\n"));
		err = 1;
	} else if (pools == NULL) {
		if (argc != 0) {
			(void) fprintf(stderr, gettext("cannot import '%s': "
			    "no such pool available\n"), argv[0]);
		}
		err = 1;
	}

	if (err == 1) {
		free(searchdirs);
		free(envdup);
		nvlist_free(policy);
		nvlist_free(pools);
		nvlist_free(props);
		return (1);
	}

	err = import_pools(pools, props, mntopts, flags,
	    argc >= 1 ? argv[0] : NULL,
	    argc >= 2 ? argv[1] : NULL,
	    do_destroyed, pool_specified, do_all, &idata);

	/*
	 * If we're using the cachefile and we failed to import, then
	 * fallback to scanning the directory for pools that match
	 * those in the cachefile.
	 */
	if (err != 0 && cachefile != NULL) {
		(void) printf(gettext("cachefile import failed, retrying\n"));

		/*
		 * We use the scan flag to gather the directories that exist
		 * in the cachefile. If we need to fallback to searching for
		 * the pool config, we will only search devices in these
		 * directories.
		 */
		idata.scan = B_TRUE;
		nvlist_free(pools);
		pools = zpool_search_import(&lpch, &idata);

		err = import_pools(pools, props, mntopts, flags,
		    argc >= 1 ? argv[0] : NULL,
		    argc >= 2 ? argv[1] : NULL,
		    do_destroyed, pool_specified, do_all, &idata);
	}

error:
	nvlist_free(props);
	nvlist_free(pools);
	nvlist_free(policy);
	free(searchdirs);
	free(envdup);

	return (err ? 1 : 0);
}
