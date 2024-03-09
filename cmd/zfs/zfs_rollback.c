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
 * zfs rollback [-rRf] <snapshot>
 *
 *	-r	Delete any intervening snapshots before doing rollback
 *	-R	Delete any snapshots and their clones
 *	-f	ignored for backwards compatibility
 *
 * Given a filesystem, rollback to a specific snapshot, discarding any changes
 * since then and making it the active dataset.  If more recent snapshots exist,
 * the command will complain unless the '-r' flag is given.
 */
typedef struct rollback_cbdata {
	uint64_t	cb_create;
	uint8_t		cb_younger_ds_printed;
	boolean_t	cb_first;
	int		cb_doclones;
	char		*cb_target;
	int		cb_error;
	boolean_t	cb_recurse;
} rollback_cbdata_t;

static int
rollback_check_dependent(zfs_handle_t *zhp, void *data)
{
	rollback_cbdata_t *cbp = data;

	if (cbp->cb_first && cbp->cb_recurse) {
		(void) fprintf(stderr, gettext("cannot rollback to "
		    "'%s': clones of previous snapshots exist\n"),
		    cbp->cb_target);
		(void) fprintf(stderr, gettext("use '-R' to "
		    "force deletion of the following clones and "
		    "dependents:\n"));
		cbp->cb_first = 0;
		cbp->cb_error = 1;
	}

	(void) fprintf(stderr, "%s\n", zfs_get_name(zhp));

	zfs_close(zhp);
	return (0);
}


/*
 * Report some snapshots/bookmarks more recent than the one specified.
 * Used when '-r' is not specified. We reuse this same callback for the
 * snapshot dependents - if 'cb_dependent' is set, then this is a
 * dependent and we should report it without checking the transaction group.
 */
static int
rollback_check(zfs_handle_t *zhp, void *data)
{
	rollback_cbdata_t *cbp = data;
	/*
	 * Max number of younger snapshots and/or bookmarks to display before
	 * we stop the iteration.
	 */
	const uint8_t max_younger = 32;

	if (cbp->cb_doclones) {
		zfs_close(zhp);
		return (0);
	}

	if (zfs_prop_get_int(zhp, ZFS_PROP_CREATETXG) > cbp->cb_create) {
		if (cbp->cb_first && !cbp->cb_recurse) {
			(void) fprintf(stderr, gettext("cannot "
			    "rollback to '%s': more recent snapshots "
			    "or bookmarks exist\n"),
			    cbp->cb_target);
			(void) fprintf(stderr, gettext("use '-r' to "
			    "force deletion of the following "
			    "snapshots and bookmarks:\n"));
			cbp->cb_first = 0;
			cbp->cb_error = 1;
		}

		if (cbp->cb_recurse) {
			if (zfs_iter_dependents_v2(zhp, 0, B_TRUE,
			    rollback_check_dependent, cbp) != 0) {
				zfs_close(zhp);
				return (-1);
			}
		} else {
			(void) fprintf(stderr, "%s\n",
			    zfs_get_name(zhp));
			cbp->cb_younger_ds_printed++;
		}
	}
	zfs_close(zhp);

	if (cbp->cb_younger_ds_printed == max_younger) {
		/*
		 * This non-recursive rollback is going to fail due to the
		 * presence of snapshots and/or bookmarks that are younger than
		 * the rollback target.
		 * We printed some of the offending objects, now we stop
		 * zfs_iter_snapshot/bookmark iteration so we can fail fast and
		 * avoid iterating over the rest of the younger objects
		 */
		(void) fprintf(stderr, gettext("Output limited to %d "
		    "snapshots/bookmarks\n"), max_younger);
		return (-1);
	}
	return (0);
}

int
zfs_do_rollback(int argc, char **argv)
{
	int ret = 0;
	int c;
	boolean_t force = B_FALSE;
	rollback_cbdata_t cb = { 0 };
	zfs_handle_t *zhp, *snap;
	char parentname[ZFS_MAX_DATASET_NAME_LEN];
	char *delim;
	uint64_t min_txg = 0;

	/* check options */
	while ((c = getopt(argc, argv, "rRf")) != -1) {
		switch (c) {
		case 'r':
			cb.cb_recurse = 1;
			break;
		case 'R':
			cb.cb_recurse = 1;
			cb.cb_doclones = 1;
			break;
		case 'f':
			force = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing dataset argument\n"));
		usage(B_FALSE);
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	/* open the snapshot */
	if ((snap = zfs_open(g_zfs, argv[0], ZFS_TYPE_SNAPSHOT)) == NULL)
		return (1);

	/* open the parent dataset */
	(void) strlcpy(parentname, argv[0], sizeof (parentname));
	verify((delim = strrchr(parentname, '@')) != NULL);
	*delim = '\0';
	if ((zhp = zfs_open(g_zfs, parentname, ZFS_TYPE_DATASET)) == NULL) {
		zfs_close(snap);
		return (1);
	}

	/*
	 * Check for more recent snapshots and/or clones based on the presence
	 * of '-r' and '-R'.
	 */
	cb.cb_target = argv[0];
	cb.cb_create = zfs_prop_get_int(snap, ZFS_PROP_CREATETXG);
	cb.cb_first = B_TRUE;
	cb.cb_error = 0;

	if (cb.cb_create > 0)
		min_txg = cb.cb_create;

	if ((ret = zfs_iter_snapshots_v2(zhp, 0, rollback_check, &cb,
	    min_txg, 0)) != 0)
		goto out;
	if ((ret = zfs_iter_bookmarks_v2(zhp, 0, rollback_check, &cb)) != 0)
		goto out;

	if ((ret = cb.cb_error) != 0)
		goto out;

	/*
	 * Rollback parent to the given snapshot.
	 */
	ret = zfs_rollback(zhp, snap, force);

out:
	zfs_close(snap);
	zfs_close(zhp);

	if (ret == 0)
		return (0);
	else
		return (1);
}
