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
 * zfs set property=value ... { fs | snap | vol } ...
 *
 * Sets the given properties for all datasets specified on the command line.
 */

static int
set_callback(zfs_handle_t *zhp, void *data)
{
	zprop_set_cbdata_t *cb = data;
	int ret = zfs_prop_set_list_flags(zhp, cb->cb_proplist, cb->cb_flags);

	if (ret != 0 || libzfs_errno(g_zfs) != EZFS_SUCCESS) {
		switch (libzfs_errno(g_zfs)) {
		case EZFS_MOUNTFAILED:
			(void) fprintf(stderr, gettext("property may be set "
			    "but unable to remount filesystem\n"));
			break;
		case EZFS_SHARENFSFAILED:
			(void) fprintf(stderr, gettext("property may be set "
			    "but unable to reshare filesystem\n"));
			break;
		}
	}
	return (ret);
}

int
zfs_do_set(int argc, char **argv)
{
	zprop_set_cbdata_t cb = { 0 };
	int ds_start = -1; /* argv idx of first dataset arg */
	int ret = 0;
	int i, c;

	/* check options */
	while ((c = getopt(argc, argv, "u")) != -1) {
		switch (c) {
		case 'u':
			cb.cb_flags |= ZFS_SET_NOMOUNT;
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
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing arguments\n"));
		usage(B_FALSE);
	}
	if (argc < 2) {
		if (strchr(argv[0], '=') == NULL) {
			(void) fprintf(stderr, gettext("missing property=value "
			    "argument(s)\n"));
		} else {
			(void) fprintf(stderr, gettext("missing dataset "
			    "name(s)\n"));
		}
		usage(B_FALSE);
	}

	/* validate argument order:  prop=val args followed by dataset args */
	for (i = 0; i < argc; i++) {
		if (strchr(argv[i], '=') != NULL) {
			if (ds_start > 0) {
				/* out-of-order prop=val argument */
				(void) fprintf(stderr, gettext("invalid "
				    "argument order\n"));
				usage(B_FALSE);
			}
		} else if (ds_start < 0) {
			ds_start = i;
		}
	}
	if (ds_start < 0) {
		(void) fprintf(stderr, gettext("missing dataset name(s)\n"));
		usage(B_FALSE);
	}

	/* Populate a list of property settings */
	if (nvlist_alloc(&cb.cb_proplist, NV_UNIQUE_NAME, 0) != 0)
		nomem();
	for (i = 0; i < ds_start; i++) {
		if (!parseprop(cb.cb_proplist, argv[i])) {
			ret = -1;
			goto error;
		}
	}

	ret = zfs_for_each(argc - ds_start, argv + ds_start, 0,
	    ZFS_TYPE_DATASET, NULL, NULL, 0, set_callback, &cb);

error:
	nvlist_free(cb.cb_proplist);
	return (ret);
}
