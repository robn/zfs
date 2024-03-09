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

typedef struct snap_cbdata {
	nvlist_t *sd_nvl;
	boolean_t sd_recursive;
	const char *sd_snapname;
} snap_cbdata_t;

static int
zfs_snapshot_cb(zfs_handle_t *zhp, void *arg)
{
	snap_cbdata_t *sd = arg;
	char *name;
	int rv = 0;
	int error;

	if (sd->sd_recursive &&
	    zfs_prop_get_int(zhp, ZFS_PROP_INCONSISTENT) != 0) {
		zfs_close(zhp);
		return (0);
	}

	error = asprintf(&name, "%s@%s", zfs_get_name(zhp), sd->sd_snapname);
	if (error == -1)
		nomem();
	fnvlist_add_boolean(sd->sd_nvl, name);
	free(name);

	if (sd->sd_recursive)
		rv = zfs_iter_filesystems_v2(zhp, 0, zfs_snapshot_cb, sd);
	zfs_close(zhp);
	return (rv);
}

/*
 * zfs snapshot [-r] [-o prop=value] ... <fs@snap>
 *
 * Creates a snapshot with the given name.  While functionally equivalent to
 * 'zfs create', it is a separate command to differentiate intent.
 */
int
zfs_do_snapshot(int argc, char **argv)
{
	int ret = 0;
	int c;
	nvlist_t *props;
	snap_cbdata_t sd = { 0 };
	boolean_t multiple_snaps = B_FALSE;

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0)
		nomem();
	if (nvlist_alloc(&sd.sd_nvl, NV_UNIQUE_NAME, 0) != 0)
		nomem();

	/* check options */
	while ((c = getopt(argc, argv, "ro:")) != -1) {
		switch (c) {
		case 'o':
			if (!parseprop(props, optarg)) {
				nvlist_free(sd.sd_nvl);
				nvlist_free(props);
				return (1);
			}
			break;
		case 'r':
			sd.sd_recursive = B_TRUE;
			multiple_snaps = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			goto usage;
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing snapshot argument\n"));
		goto usage;
	}

	if (argc > 1)
		multiple_snaps = B_TRUE;
	for (; argc > 0; argc--, argv++) {
		char *atp;
		zfs_handle_t *zhp;

		atp = strchr(argv[0], '@');
		if (atp == NULL)
			goto usage;
		*atp = '\0';
		sd.sd_snapname = atp + 1;
		zhp = zfs_open(g_zfs, argv[0],
		    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
		if (zhp == NULL)
			goto usage;
		if (zfs_snapshot_cb(zhp, &sd) != 0)
			goto usage;
	}

	ret = zfs_snapshot_nvl(g_zfs, sd.sd_nvl, props);
	nvlist_free(sd.sd_nvl);
	nvlist_free(props);
	if (ret != 0 && multiple_snaps)
		(void) fprintf(stderr, gettext("no snapshots were created\n"));
	return (ret != 0);

usage:
	nvlist_free(sd.sd_nvl);
	nvlist_free(props);
	usage(B_FALSE);
	return (-1);
}
