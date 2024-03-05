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
 * Return 1 if a vdev is active (being used in a pool)
 * Return 0 if a vdev is inactive (offlined or faulted, or not in active pool)
 *
 * This is useful for checking if a disk in an active pool is offlined or
 * faulted.
 */
static int
vdev_is_active(char *vdev_path)
{
	int fd;
	fd = open(vdev_path, O_EXCL);
	if (fd < 0) {
		return (1);   /* cant open O_EXCL - disk is active */
	}

	close(fd);
	return (0);   /* disk is inactive in the pool */
}

/*
 * zpool labelclear [-f] <vdev>
 *
 *	-f	Force clearing the label for the vdevs which are members of
 *		the exported or foreign pools.
 *
 * Verifies that the vdev is not active and zeros out the label information
 * on the device.
 */
int
zpool_do_labelclear(int argc, char **argv)
{
	char vdev[MAXPATHLEN];
	char *name = NULL;
	int c, fd = -1, ret = 0;
	nvlist_t *config;
	pool_state_t state;
	boolean_t inuse = B_FALSE;
	boolean_t force = B_FALSE;

	/* check options */
	while ((c = getopt(argc, argv, "f")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			zpool_usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get vdev name */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing vdev name\n"));
		zpool_usage(B_FALSE);
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		zpool_usage(B_FALSE);
	}

	(void) strlcpy(vdev, argv[0], sizeof (vdev));

	/*
	 * If we cannot open an absolute path, we quit.
	 * Otherwise if the provided vdev name doesn't point to a file,
	 * try prepending expected disk paths and partition numbers.
	 */
	if ((fd = open(vdev, O_RDWR)) < 0) {
		int error;
		if (vdev[0] == '/') {
			(void) fprintf(stderr, gettext("failed to open "
			    "%s: %s\n"), vdev, strerror(errno));
			return (1);
		}

		error = zfs_resolve_shortname(argv[0], vdev, MAXPATHLEN);
		if (error == 0 && zfs_dev_is_whole_disk(vdev)) {
			if (zfs_append_partition(vdev, MAXPATHLEN) == -1)
				error = ENOENT;
		}

		if (error || ((fd = open(vdev, O_RDWR)) < 0)) {
			if (errno == ENOENT) {
				(void) fprintf(stderr, gettext(
				    "failed to find device %s, try "
				    "specifying absolute path instead\n"),
				    argv[0]);
				return (1);
			}

			(void) fprintf(stderr, gettext("failed to open %s:"
			    " %s\n"), vdev, strerror(errno));
			return (1);
		}
	}

	/*
	 * Flush all dirty pages for the block device.  This should not be
	 * fatal when the device does not support BLKFLSBUF as would be the
	 * case for a file vdev.
	 */
	if ((zfs_dev_flush(fd) != 0) && (errno != ENOTTY))
		(void) fprintf(stderr, gettext("failed to invalidate "
		    "cache for %s: %s\n"), vdev, strerror(errno));

	if (zpool_read_label(fd, &config, NULL) != 0) {
		(void) fprintf(stderr,
		    gettext("failed to read label from %s\n"), vdev);
		ret = 1;
		goto errout;
	}
	nvlist_free(config);

	ret = zpool_in_use(g_zfs, fd, &state, &name, &inuse);
	if (ret != 0) {
		(void) fprintf(stderr,
		    gettext("failed to check state for %s\n"), vdev);
		ret = 1;
		goto errout;
	}

	if (!inuse)
		goto wipe_label;

	switch (state) {
	default:
	case POOL_STATE_ACTIVE:
	case POOL_STATE_SPARE:
	case POOL_STATE_L2CACHE:
		/*
		 * We allow the user to call 'zpool offline -f'
		 * on an offlined disk in an active pool. We can check if
		 * the disk is online by calling vdev_is_active().
		 */
		if (force && !vdev_is_active(vdev))
			break;

		(void) fprintf(stderr, gettext(
		    "%s is a member (%s) of pool \"%s\""),
		    vdev, zpool_pool_state_to_name(state), name);

		if (force) {
			(void) fprintf(stderr, gettext(
			    ". Offline the disk first to clear its label."));
		}
		printf("\n");
		ret = 1;
		goto errout;

	case POOL_STATE_EXPORTED:
		if (force)
			break;
		(void) fprintf(stderr, gettext(
		    "use '-f' to override the following error:\n"
		    "%s is a member of exported pool \"%s\"\n"),
		    vdev, name);
		ret = 1;
		goto errout;

	case POOL_STATE_POTENTIALLY_ACTIVE:
		if (force)
			break;
		(void) fprintf(stderr, gettext(
		    "use '-f' to override the following error:\n"
		    "%s is a member of potentially active pool \"%s\"\n"),
		    vdev, name);
		ret = 1;
		goto errout;

	case POOL_STATE_DESTROYED:
		/* inuse should never be set for a destroyed pool */
		assert(0);
		break;
	}

wipe_label:
	ret = zpool_clear_label(fd);
	if (ret != 0) {
		(void) fprintf(stderr,
		    gettext("failed to clear label for %s\n"), vdev);
	}

errout:
	free(name);
	(void) close(fd);

	return (ret);
}

