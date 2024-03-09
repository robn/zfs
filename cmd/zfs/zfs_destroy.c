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

#include <assert.h>
#include <ctype.h>
#include <sys/debug.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <libintl.h>
#include <libuutil.h>
#include <libnvpair.h>
#include <locale.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <zone.h>
#include <grp.h>
#include <pwd.h>
#include <umem.h>
#include <pthread.h>
#include <signal.h>
#include <sys/list.h>
#include <sys/mkdev.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/fs/zfs.h>
#include <sys/systeminfo.h>
#include <sys/types.h>
#include <time.h>
#include <sys/zfs_project.h>

#include <libzfs.h>
#include <libzfs_core.h>
#include <zfs_prop.h>
#include <zfs_deleg.h>
#include <libzutil.h>
#ifdef HAVE_IDMAP
#include <aclutils.h>
#include <directory.h>
#endif /* HAVE_IDMAP */

#include "zfs.h"
#include "zfs_common.h"
#include "zfs_iter.h"
#include "zfs_util.h"
#include "zfs_comutil.h"
#include "zfs_projectutil.h"

/*
 * zfs destroy [-rRf] <fs, vol>
 * zfs destroy [-rRd] <snap>
 *
 *	-r	Recursively destroy all children
 *	-R	Recursively destroy all dependents, including clones
 *	-f	Force unmounting of any dependents
 *	-d	If we can't destroy now, mark for deferred destruction
 *
 * Destroys the given dataset.  By default, it will unmount any filesystems,
 * and refuse to destroy a dataset that has any dependents.  A dependent can
 * either be a child, or a clone of a child.
 */
typedef struct destroy_cbdata {
	boolean_t	cb_first;
	boolean_t	cb_force;
	boolean_t	cb_recurse;
	boolean_t	cb_error;
	boolean_t	cb_doclones;
	zfs_handle_t	*cb_target;
	boolean_t	cb_defer_destroy;
	boolean_t	cb_verbose;
	boolean_t	cb_parsable;
	boolean_t	cb_dryrun;
	nvlist_t	*cb_nvl;
	nvlist_t	*cb_batchedsnaps;

	/* first snap in contiguous run */
	char		*cb_firstsnap;
	/* previous snap in contiguous run */
	char		*cb_prevsnap;
	int64_t		cb_snapused;
	char		*cb_snapspec;
	char		*cb_bookmark;
	uint64_t	cb_snap_count;
} destroy_cbdata_t;

/*
 * Check for any dependents based on the '-r' or '-R' flags.
 */
static int
destroy_check_dependent(zfs_handle_t *zhp, void *data)
{
	destroy_cbdata_t *cbp = data;
	const char *tname = zfs_get_name(cbp->cb_target);
	const char *name = zfs_get_name(zhp);

	if (strncmp(tname, name, strlen(tname)) == 0 &&
	    (name[strlen(tname)] == '/' || name[strlen(tname)] == '@')) {
		/*
		 * This is a direct descendant, not a clone somewhere else in
		 * the hierarchy.
		 */
		if (cbp->cb_recurse)
			goto out;

		if (cbp->cb_first) {
			(void) fprintf(stderr, gettext("cannot destroy '%s': "
			    "%s has children\n"),
			    zfs_get_name(cbp->cb_target),
			    zfs_type_to_name(zfs_get_type(cbp->cb_target)));
			(void) fprintf(stderr, gettext("use '-r' to destroy "
			    "the following datasets:\n"));
			cbp->cb_first = B_FALSE;
			cbp->cb_error = B_TRUE;
		}

		(void) fprintf(stderr, "%s\n", zfs_get_name(zhp));
	} else {
		/*
		 * This is a clone.  We only want to report this if the '-r'
		 * wasn't specified, or the target is a snapshot.
		 */
		if (!cbp->cb_recurse &&
		    zfs_get_type(cbp->cb_target) != ZFS_TYPE_SNAPSHOT)
			goto out;

		if (cbp->cb_first) {
			(void) fprintf(stderr, gettext("cannot destroy '%s': "
			    "%s has dependent clones\n"),
			    zfs_get_name(cbp->cb_target),
			    zfs_type_to_name(zfs_get_type(cbp->cb_target)));
			(void) fprintf(stderr, gettext("use '-R' to destroy "
			    "the following datasets:\n"));
			cbp->cb_first = B_FALSE;
			cbp->cb_error = B_TRUE;
			cbp->cb_dryrun = B_TRUE;
		}

		(void) fprintf(stderr, "%s\n", zfs_get_name(zhp));
	}

out:
	zfs_close(zhp);
	return (0);
}

static int
destroy_batched(destroy_cbdata_t *cb)
{
	int error = zfs_destroy_snaps_nvl(g_zfs,
	    cb->cb_batchedsnaps, B_FALSE);
	fnvlist_free(cb->cb_batchedsnaps);
	cb->cb_batchedsnaps = fnvlist_alloc();
	return (error);
}

static int
destroy_callback(zfs_handle_t *zhp, void *data)
{
	destroy_cbdata_t *cb = data;
	const char *name = zfs_get_name(zhp);
	int error;

	if (cb->cb_verbose) {
		if (cb->cb_parsable) {
			(void) printf("destroy\t%s\n", name);
		} else if (cb->cb_dryrun) {
			(void) printf(gettext("would destroy %s\n"),
			    name);
		} else {
			(void) printf(gettext("will destroy %s\n"),
			    name);
		}
	}

	/*
	 * Ignore pools (which we've already flagged as an error before getting
	 * here).
	 */
	if (strchr(zfs_get_name(zhp), '/') == NULL &&
	    zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) {
		zfs_close(zhp);
		return (0);
	}
	if (cb->cb_dryrun) {
		zfs_close(zhp);
		return (0);
	}

	/*
	 * We batch up all contiguous snapshots (even of different
	 * filesystems) and destroy them with one ioctl.  We can't
	 * simply do all snap deletions and then all fs deletions,
	 * because we must delete a clone before its origin.
	 */
	if (zfs_get_type(zhp) == ZFS_TYPE_SNAPSHOT) {
		cb->cb_snap_count++;
		fnvlist_add_boolean(cb->cb_batchedsnaps, name);
		if (cb->cb_snap_count % 10 == 0 && cb->cb_defer_destroy) {
			error = destroy_batched(cb);
			if (error != 0) {
				zfs_close(zhp);
				return (-1);
			}
		}
	} else {
		error = destroy_batched(cb);
		if (error != 0 ||
		    zfs_unmount(zhp, NULL, cb->cb_force ? MS_FORCE : 0) != 0 ||
		    zfs_destroy(zhp, cb->cb_defer_destroy) != 0) {
			zfs_close(zhp);
			/*
			 * When performing a recursive destroy we ignore errors
			 * so that the recursive destroy could continue
			 * destroying past problem datasets
			 */
			if (cb->cb_recurse) {
				cb->cb_error = B_TRUE;
				return (0);
			}
			return (-1);
		}
	}

	zfs_close(zhp);
	return (0);
}

static int
destroy_print_cb(zfs_handle_t *zhp, void *arg)
{
	destroy_cbdata_t *cb = arg;
	const char *name = zfs_get_name(zhp);
	int err = 0;

	if (nvlist_exists(cb->cb_nvl, name)) {
		if (cb->cb_firstsnap == NULL)
			cb->cb_firstsnap = strdup(name);
		if (cb->cb_prevsnap != NULL)
			free(cb->cb_prevsnap);
		/* this snap continues the current range */
		cb->cb_prevsnap = strdup(name);
		if (cb->cb_firstsnap == NULL || cb->cb_prevsnap == NULL)
			nomem();
		if (cb->cb_verbose) {
			if (cb->cb_parsable) {
				(void) printf("destroy\t%s\n", name);
			} else if (cb->cb_dryrun) {
				(void) printf(gettext("would destroy %s\n"),
				    name);
			} else {
				(void) printf(gettext("will destroy %s\n"),
				    name);
			}
		}
	} else if (cb->cb_firstsnap != NULL) {
		/* end of this range */
		uint64_t used = 0;
		err = lzc_snaprange_space(cb->cb_firstsnap,
		    cb->cb_prevsnap, &used);
		cb->cb_snapused += used;
		free(cb->cb_firstsnap);
		cb->cb_firstsnap = NULL;
		free(cb->cb_prevsnap);
		cb->cb_prevsnap = NULL;
	}
	zfs_close(zhp);
	return (err);
}

static int
destroy_print_snapshots(zfs_handle_t *fs_zhp, destroy_cbdata_t *cb)
{
	int err;
	assert(cb->cb_firstsnap == NULL);
	assert(cb->cb_prevsnap == NULL);
	err = zfs_iter_snapshots_sorted_v2(fs_zhp, 0, destroy_print_cb, cb, 0,
	    0);
	if (cb->cb_firstsnap != NULL) {
		uint64_t used = 0;
		if (err == 0) {
			err = lzc_snaprange_space(cb->cb_firstsnap,
			    cb->cb_prevsnap, &used);
		}
		cb->cb_snapused += used;
		free(cb->cb_firstsnap);
		cb->cb_firstsnap = NULL;
		free(cb->cb_prevsnap);
		cb->cb_prevsnap = NULL;
	}
	return (err);
}

static int
snapshot_to_nvl_cb(zfs_handle_t *zhp, void *arg)
{
	destroy_cbdata_t *cb = arg;
	int err = 0;

	/* Check for clones. */
	if (!cb->cb_doclones && !cb->cb_defer_destroy) {
		cb->cb_target = zhp;
		cb->cb_first = B_TRUE;
		err = zfs_iter_dependents_v2(zhp, 0, B_TRUE,
		    destroy_check_dependent, cb);
	}

	if (err == 0) {
		if (nvlist_add_boolean(cb->cb_nvl, zfs_get_name(zhp)))
			nomem();
	}
	zfs_close(zhp);
	return (err);
}

static int
gather_snapshots(zfs_handle_t *zhp, void *arg)
{
	destroy_cbdata_t *cb = arg;
	int err = 0;

	err = zfs_iter_snapspec_v2(zhp, 0, cb->cb_snapspec,
	    snapshot_to_nvl_cb, cb);
	if (err == ENOENT)
		err = 0;
	if (err != 0)
		goto out;

	if (cb->cb_verbose) {
		err = destroy_print_snapshots(zhp, cb);
		if (err != 0)
			goto out;
	}

	if (cb->cb_recurse)
		err = zfs_iter_filesystems_v2(zhp, 0, gather_snapshots, cb);

out:
	zfs_close(zhp);
	return (err);
}

static int
destroy_clones(destroy_cbdata_t *cb)
{
	nvpair_t *pair;
	for (pair = nvlist_next_nvpair(cb->cb_nvl, NULL);
	    pair != NULL;
	    pair = nvlist_next_nvpair(cb->cb_nvl, pair)) {
		zfs_handle_t *zhp = zfs_open(g_zfs, nvpair_name(pair),
		    ZFS_TYPE_SNAPSHOT);
		if (zhp != NULL) {
			boolean_t defer = cb->cb_defer_destroy;
			int err;

			/*
			 * We can't defer destroy non-snapshots, so set it to
			 * false while destroying the clones.
			 */
			cb->cb_defer_destroy = B_FALSE;
			err = zfs_iter_dependents_v2(zhp, 0, B_FALSE,
			    destroy_callback, cb);
			cb->cb_defer_destroy = defer;
			zfs_close(zhp);
			if (err != 0)
				return (err);
		}
	}
	return (0);
}

int
zfs_do_destroy(int argc, char **argv)
{
	destroy_cbdata_t cb = { 0 };
	int rv = 0;
	int err = 0;
	int c;
	zfs_handle_t *zhp = NULL;
	char *at, *pound;
	zfs_type_t type = ZFS_TYPE_DATASET;

	/* check options */
	while ((c = getopt(argc, argv, "vpndfrR")) != -1) {
		switch (c) {
		case 'v':
			cb.cb_verbose = B_TRUE;
			break;
		case 'p':
			cb.cb_verbose = B_TRUE;
			cb.cb_parsable = B_TRUE;
			break;
		case 'n':
			cb.cb_dryrun = B_TRUE;
			break;
		case 'd':
			cb.cb_defer_destroy = B_TRUE;
			type = ZFS_TYPE_SNAPSHOT;
			break;
		case 'f':
			cb.cb_force = B_TRUE;
			break;
		case 'r':
			cb.cb_recurse = B_TRUE;
			break;
		case 'R':
			cb.cb_recurse = B_TRUE;
			cb.cb_doclones = B_TRUE;
			break;
		case '?':
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc == 0) {
		(void) fprintf(stderr, gettext("missing dataset argument\n"));
		usage(B_FALSE);
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	at = strchr(argv[0], '@');
	pound = strchr(argv[0], '#');
	if (at != NULL) {

		/* Build the list of snaps to destroy in cb_nvl. */
		cb.cb_nvl = fnvlist_alloc();

		*at = '\0';
		zhp = zfs_open(g_zfs, argv[0],
		    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
		if (zhp == NULL) {
			nvlist_free(cb.cb_nvl);
			return (1);
		}

		cb.cb_snapspec = at + 1;
		if (gather_snapshots(zfs_handle_dup(zhp), &cb) != 0 ||
		    cb.cb_error) {
			rv = 1;
			goto out;
		}

		if (nvlist_empty(cb.cb_nvl)) {
			(void) fprintf(stderr, gettext("could not find any "
			    "snapshots to destroy; check snapshot names.\n"));
			rv = 1;
			goto out;
		}

		if (cb.cb_verbose) {
			char buf[16];
			zfs_nicebytes(cb.cb_snapused, buf, sizeof (buf));
			if (cb.cb_parsable) {
				(void) printf("reclaim\t%llu\n",
				    (u_longlong_t)cb.cb_snapused);
			} else if (cb.cb_dryrun) {
				(void) printf(gettext("would reclaim %s\n"),
				    buf);
			} else {
				(void) printf(gettext("will reclaim %s\n"),
				    buf);
			}
		}

		if (!cb.cb_dryrun) {
			if (cb.cb_doclones) {
				cb.cb_batchedsnaps = fnvlist_alloc();
				err = destroy_clones(&cb);
				if (err == 0) {
					err = zfs_destroy_snaps_nvl(g_zfs,
					    cb.cb_batchedsnaps, B_FALSE);
				}
				if (err != 0) {
					rv = 1;
					goto out;
				}
			}
			if (err == 0) {
				err = zfs_destroy_snaps_nvl(g_zfs, cb.cb_nvl,
				    cb.cb_defer_destroy);
			}
		}

		if (err != 0)
			rv = 1;
	} else if (pound != NULL) {
		int err;
		nvlist_t *nvl;

		if (cb.cb_dryrun) {
			(void) fprintf(stderr,
			    "dryrun is not supported with bookmark\n");
			return (-1);
		}

		if (cb.cb_defer_destroy) {
			(void) fprintf(stderr,
			    "defer destroy is not supported with bookmark\n");
			return (-1);
		}

		if (cb.cb_recurse) {
			(void) fprintf(stderr,
			    "recursive is not supported with bookmark\n");
			return (-1);
		}

		/*
		 * Unfortunately, zfs_bookmark() doesn't honor the
		 * casesensitivity setting.  However, we can't simply
		 * remove this check, because lzc_destroy_bookmarks()
		 * ignores non-existent bookmarks, so this is necessary
		 * to get a proper error message.
		 */
		if (!zfs_bookmark_exists(argv[0])) {
			(void) fprintf(stderr, gettext("bookmark '%s' "
			    "does not exist.\n"), argv[0]);
			return (1);
		}

		nvl = fnvlist_alloc();
		fnvlist_add_boolean(nvl, argv[0]);

		err = lzc_destroy_bookmarks(nvl, NULL);
		if (err != 0) {
			(void) zfs_standard_error(g_zfs, err,
			    "cannot destroy bookmark");
		}

		nvlist_free(nvl);

		return (err);
	} else {
		/* Open the given dataset */
		if ((zhp = zfs_open(g_zfs, argv[0], type)) == NULL)
			return (1);

		cb.cb_target = zhp;

		/*
		 * Perform an explicit check for pools before going any further.
		 */
		if (!cb.cb_recurse && strchr(zfs_get_name(zhp), '/') == NULL &&
		    zfs_get_type(zhp) == ZFS_TYPE_FILESYSTEM) {
			(void) fprintf(stderr, gettext("cannot destroy '%s': "
			    "operation does not apply to pools\n"),
			    zfs_get_name(zhp));
			(void) fprintf(stderr, gettext("use 'zfs destroy -r "
			    "%s' to destroy all datasets in the pool\n"),
			    zfs_get_name(zhp));
			(void) fprintf(stderr, gettext("use 'zpool destroy %s' "
			    "to destroy the pool itself\n"), zfs_get_name(zhp));
			rv = 1;
			goto out;
		}

		/*
		 * Check for any dependents and/or clones.
		 */
		cb.cb_first = B_TRUE;
		if (!cb.cb_doclones && zfs_iter_dependents_v2(zhp, 0, B_TRUE,
		    destroy_check_dependent, &cb) != 0) {
			rv = 1;
			goto out;
		}

		if (cb.cb_error) {
			rv = 1;
			goto out;
		}
		cb.cb_batchedsnaps = fnvlist_alloc();
		if (zfs_iter_dependents_v2(zhp, 0, B_FALSE, destroy_callback,
		    &cb) != 0) {
			rv = 1;
			goto out;
		}

		/*
		 * Do the real thing.  The callback will close the
		 * handle regardless of whether it succeeds or not.
		 */
		err = destroy_callback(zhp, &cb);
		zhp = NULL;
		if (err == 0) {
			err = zfs_destroy_snaps_nvl(g_zfs,
			    cb.cb_batchedsnaps, cb.cb_defer_destroy);
		}
		if (err != 0 || cb.cb_error == B_TRUE)
			rv = 1;
	}

out:
	fnvlist_free(cb.cb_batchedsnaps);
	fnvlist_free(cb.cb_nvl);
	if (zhp != NULL)
		zfs_close(zhp);
	return (rv);
}
