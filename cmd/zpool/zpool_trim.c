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

/*
 * zpool trim [-d] [-r <rate>] [-c | -s] <pool> [<device> ...]
 *
 *	-c		Cancel. Ends any in-progress trim.
 *	-d		Secure trim.  Requires kernel and device support.
 *	-r <rate>	Sets the TRIM rate in bytes (per second). Supports
 *			adding a multiplier suffix such as 'k' or 'm'.
 *	-s		Suspend. TRIM can then be restarted with no flags.
 *	-w		Wait. Blocks until trimming has completed.
 */
int
zpool_do_trim(int argc, char **argv)
{
	struct option long_options[] = {
		{"cancel",	no_argument,		NULL,	'c'},
		{"secure",	no_argument,		NULL,	'd'},
		{"rate",	required_argument,	NULL,	'r'},
		{"suspend",	no_argument,		NULL,	's'},
		{"wait",	no_argument,		NULL,	'w'},
		{0, 0, 0, 0}
	};

	pool_trim_func_t cmd_type = POOL_TRIM_START;
	uint64_t rate = 0;
	boolean_t secure = B_FALSE;
	boolean_t wait = B_FALSE;

	int c;
	while ((c = getopt_long(argc, argv, "cdr:sw", long_options, NULL))
	    != -1) {
		switch (c) {
		case 'c':
			if (cmd_type != POOL_TRIM_START &&
			    cmd_type != POOL_TRIM_CANCEL) {
				(void) fprintf(stderr, gettext("-c cannot be "
				    "combined with other options\n"));
				zpool_usage(B_FALSE);
			}
			cmd_type = POOL_TRIM_CANCEL;
			break;
		case 'd':
			if (cmd_type != POOL_TRIM_START) {
				(void) fprintf(stderr, gettext("-d cannot be "
				    "combined with the -c or -s options\n"));
				zpool_usage(B_FALSE);
			}
			secure = B_TRUE;
			break;
		case 'r':
			if (cmd_type != POOL_TRIM_START) {
				(void) fprintf(stderr, gettext("-r cannot be "
				    "combined with the -c or -s options\n"));
				zpool_usage(B_FALSE);
			}
			if (zfs_nicestrtonum(g_zfs, optarg, &rate) == -1) {
				(void) fprintf(stderr, "%s: %s\n",
				    gettext("invalid value for rate"),
				    libzfs_error_description(g_zfs));
				zpool_usage(B_FALSE);
			}
			break;
		case 's':
			if (cmd_type != POOL_TRIM_START &&
			    cmd_type != POOL_TRIM_SUSPEND) {
				(void) fprintf(stderr, gettext("-s cannot be "
				    "combined with other options\n"));
				zpool_usage(B_FALSE);
			}
			cmd_type = POOL_TRIM_SUSPEND;
			break;
		case 'w':
			wait = B_TRUE;
			break;
		case '?':
			if (optopt != 0) {
				(void) fprintf(stderr,
				    gettext("invalid option '%c'\n"), optopt);
			} else {
				(void) fprintf(stderr,
				    gettext("invalid option '%s'\n"),
				    argv[optind - 1]);
			}
			zpool_usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		zpool_usage(B_FALSE);
		return (-1);
	}

	if (wait && (cmd_type != POOL_TRIM_START)) {
		(void) fprintf(stderr, gettext("-w cannot be used with -c or "
		    "-s\n"));
		zpool_usage(B_FALSE);
	}

	char *poolname = argv[0];
	zpool_handle_t *zhp = zpool_open(g_zfs, poolname);
	if (zhp == NULL)
		return (-1);

	trimflags_t trim_flags = {
		.secure = secure,
		.rate = rate,
		.wait = wait,
	};

	nvlist_t *vdevs = fnvlist_alloc();
	if (argc == 1) {
		/* no individual leaf vdevs specified, so add them all */
		nvlist_t *config = zpool_get_config(zhp, NULL);
		nvlist_t *nvroot = fnvlist_lookup_nvlist(config,
		    ZPOOL_CONFIG_VDEV_TREE);
		zpool_collect_leaves(zhp, nvroot, vdevs);
		trim_flags.fullpool = B_TRUE;
	} else {
		trim_flags.fullpool = B_FALSE;
		for (int i = 1; i < argc; i++) {
			fnvlist_add_boolean(vdevs, argv[i]);
		}
	}

	int error = zpool_trim(zhp, cmd_type, vdevs, &trim_flags);

	fnvlist_free(vdevs);
	zpool_close(zhp);

	return (error);
}
