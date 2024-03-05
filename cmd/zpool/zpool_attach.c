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

static int
zpool_do_attach_or_replace(int argc, char **argv, int replacing)
{
	boolean_t force = B_FALSE;
	boolean_t rebuild = B_FALSE;
	boolean_t wait = B_FALSE;
	int c;
	nvlist_t *nvroot;
	char *poolname, *old_disk, *new_disk;
	zpool_handle_t *zhp;
	nvlist_t *props = NULL;
	char *propval;
	int ret;

	/* check options */
	while ((c = getopt(argc, argv, "fo:sw")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case 'o':
			if ((propval = strchr(optarg, '=')) == NULL) {
				(void) fprintf(stderr, gettext("missing "
				    "'=' for -o option\n"));
				zpool_usage(B_FALSE);
			}
			*propval = '\0';
			propval++;

			if ((strcmp(optarg, ZPOOL_CONFIG_ASHIFT) != 0) ||
			    (add_prop_list(optarg, propval, &props, B_TRUE)))
				zpool_usage(B_FALSE);
			break;
		case 's':
			rebuild = B_TRUE;
			break;
		case 'w':
			wait = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			zpool_usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* get pool name and check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing pool name argument\n"));
		zpool_usage(B_FALSE);
	}

	poolname = argv[0];

	if (argc < 2) {
		(void) fprintf(stderr,
		    gettext("missing <device> specification\n"));
		zpool_usage(B_FALSE);
	}

	old_disk = argv[1];

	if (argc < 3) {
		if (!replacing) {
			(void) fprintf(stderr,
			    gettext("missing <new_device> specification\n"));
			zpool_usage(B_FALSE);
		}
		new_disk = old_disk;
		argc -= 1;
		argv += 1;
	} else {
		new_disk = argv[2];
		argc -= 2;
		argv += 2;
	}

	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		zpool_usage(B_FALSE);
	}

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL) {
		nvlist_free(props);
		return (1);
	}

	if (zpool_get_config(zhp, NULL) == NULL) {
		(void) fprintf(stderr, gettext("pool '%s' is unavailable\n"),
		    poolname);
		zpool_close(zhp);
		nvlist_free(props);
		return (1);
	}

	/* unless manually specified use "ashift" pool property (if set) */
	if (!nvlist_exists(props, ZPOOL_CONFIG_ASHIFT)) {
		int intval;
		zprop_source_t src;
		char strval[ZPOOL_MAXPROPLEN];

		intval = zpool_get_prop_int(zhp, ZPOOL_PROP_ASHIFT, &src);
		if (src != ZPROP_SRC_DEFAULT) {
			(void) sprintf(strval, "%" PRId32, intval);
			verify(add_prop_list(ZPOOL_CONFIG_ASHIFT, strval,
			    &props, B_TRUE) == 0);
		}
	}

	nvroot = make_root_vdev(zhp, props, force, B_FALSE, replacing, B_FALSE,
	    argc, argv);
	if (nvroot == NULL) {
		zpool_close(zhp);
		nvlist_free(props);
		return (1);
	}

	ret = zpool_vdev_attach(zhp, old_disk, new_disk, nvroot, replacing,
	    rebuild);

	if (ret == 0 && wait) {
		zpool_wait_activity_t activity = ZPOOL_WAIT_RESILVER;
		char raidz_prefix[] = "raidz";
		if (replacing) {
			activity = ZPOOL_WAIT_REPLACE;
		} else if (strncmp(old_disk,
		    raidz_prefix, strlen(raidz_prefix)) == 0) {
			activity = ZPOOL_WAIT_RAIDZ_EXPAND;
		}
		ret = zpool_wait(zhp, activity);
	}

	nvlist_free(props);
	nvlist_free(nvroot);
	zpool_close(zhp);

	return (ret);
}

/*
 * zpool replace [-fsw] [-o property=value] <pool> <device> <new_device>
 *
 *	-f	Force attach, even if <new_device> appears to be in use.
 *	-s	Use sequential instead of healing reconstruction for resilver.
 *	-o	Set property=value.
 *	-w	Wait for replacing to complete before returning
 *
 * Replace <device> with <new_device>.
 */
int
zpool_do_replace(int argc, char **argv)
{
	return (zpool_do_attach_or_replace(argc, argv, B_TRUE));
}

/*
 * zpool attach [-fsw] [-o property=value] <pool> <device>|<vdev> <new_device>
 *
 *	-f	Force attach, even if <new_device> appears to be in use.
 *	-s	Use sequential instead of healing reconstruction for resilver.
 *	-o	Set property=value.
 *	-w	Wait for resilvering (mirror) or expansion (raidz) to complete
 *		before returning.
 *
 * Attach <new_device> to a <device> or <vdev>, where the vdev can be of type
 * mirror or raidz. If <device> is not part of a mirror, then <device> will
 * be transformed into a mirror of <device> and <new_device>. When a mirror
 * is involved, <new_device> will begin life with a DTL of [0, now], and will
 * immediately begin to resilver itself. For the raidz case, a expansion will
 * commence and reflow the raidz data across all the disks including the
 * <new_device>.
 */
int
zpool_do_attach(int argc, char **argv)
{
	return (zpool_do_attach_or_replace(argc, argv, B_FALSE));
}
