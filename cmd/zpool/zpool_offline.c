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
 * zpool offline [-ft]|[--power] <pool> <device> ...
 *
 *
 *	-f	Force the device into a faulted state.
 *
 *	-t	Only take the device off-line temporarily.  The offline/faulted
 *		state will not be persistent across reboots.
 *
 *	--power Power off the enclosure slot to the drive (if possible)
 */
int
zpool_do_offline(int argc, char **argv)
{
	int c, i;
	char *poolname;
	zpool_handle_t *zhp;
	int ret = 0;
	boolean_t istmp = B_FALSE;
	boolean_t fault = B_FALSE;
	boolean_t is_power_off = B_FALSE;

	struct option long_options[] = {
		{"power", no_argument, NULL, POWER_OPT},
		{0, 0, 0, 0}
	};

	/* check options */
	while ((c = getopt_long(argc, argv, "ft", long_options, NULL)) != -1) {
		switch (c) {
		case 'f':
			fault = B_TRUE;
			break;
		case 't':
			istmp = B_TRUE;
			break;
		case POWER_OPT:
			is_power_off = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			zpool_usage(B_FALSE);
		}
	}

	if (is_power_off && fault) {
		(void) fprintf(stderr,
		    gettext("-0 and -f cannot be used together\n"));
		zpool_usage(B_FALSE);
		return (1);
	}

	if (is_power_off && istmp) {
		(void) fprintf(stderr,
		    gettext("-0 and -t cannot be used together\n"));
		zpool_usage(B_FALSE);
		return (1);
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name\n"));
		zpool_usage(B_FALSE);
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing device name\n"));
		zpool_usage(B_FALSE);
	}

	poolname = argv[0];

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	for (i = 1; i < argc; i++) {
		uint64_t guid = zpool_vdev_path_to_guid(zhp, argv[i]);
		if (is_power_off) {
			/*
			 * Note: we have to power off first, then set REMOVED,
			 * or else zpool_vdev_set_removed_state() returns
			 * EAGAIN.
			 */
			ret = zpool_power_off(zhp, argv[i]);
			if (ret != 0) {
				(void) fprintf(stderr, "%s %s %d\n",
				    gettext("unable to power off slot for"),
				    argv[i], ret);
			}
			zpool_vdev_set_removed_state(zhp, guid, VDEV_AUX_NONE);

		} else if (fault) {
			vdev_aux_t aux;
			if (istmp == B_FALSE) {
				/* Force the fault to persist across imports */
				aux = VDEV_AUX_EXTERNAL_PERSIST;
			} else {
				aux = VDEV_AUX_EXTERNAL;
			}

			if (guid == 0 || zpool_vdev_fault(zhp, guid, aux) != 0)
				ret = 1;
		} else {
			if (zpool_vdev_offline(zhp, argv[i], istmp) != 0)
				ret = 1;
		}
	}

	zpool_close(zhp);

	return (ret);
}
