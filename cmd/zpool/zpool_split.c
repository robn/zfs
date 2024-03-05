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
 * zpool split [-gLnP] [-o prop=val] ...
 *		[-o mntopt] ...
 *		[-R altroot] <pool> <newpool> [<device> ...]
 *
 *	-g      Display guid for individual vdev name.
 *	-L	Follow links when resolving vdev path name.
 *	-n	Do not split the pool, but display the resulting layout if
 *		it were to be split.
 *	-o	Set property=value, or set mount options.
 *	-P	Display full path for vdev name.
 *	-R	Mount the split-off pool under an alternate root.
 *	-l	Load encryption keys while importing.
 *
 * Splits the named pool and gives it the new pool name.  Devices to be split
 * off may be listed, provided that no more than one device is specified
 * per top-level vdev mirror.  The newly split pool is left in an exported
 * state unless -R is specified.
 *
 * Restrictions: the top-level of the pool pool must only be made up of
 * mirrors; all devices in the pool must be healthy; no device may be
 * undergoing a resilvering operation.
 */
int
zpool_do_split(int argc, char **argv)
{
	char *srcpool, *newpool, *propval;
	char *mntopts = NULL;
	splitflags_t flags;
	int c, ret = 0;
	int ms_status = 0;
	boolean_t loadkeys = B_FALSE;
	zpool_handle_t *zhp;
	nvlist_t *config, *props = NULL;

	flags.dryrun = B_FALSE;
	flags.import = B_FALSE;
	flags.name_flags = 0;

	/* check options */
	while ((c = getopt(argc, argv, ":gLR:lno:P")) != -1) {
		switch (c) {
		case 'g':
			flags.name_flags |= VDEV_NAME_GUID;
			break;
		case 'L':
			flags.name_flags |= VDEV_NAME_FOLLOW_LINKS;
			break;
		case 'R':
			flags.import = B_TRUE;
			if (add_prop_list(
			    zpool_prop_to_name(ZPOOL_PROP_ALTROOT), optarg,
			    &props, B_TRUE) != 0) {
				nvlist_free(props);
				zpool_usage(B_FALSE);
			}
			break;
		case 'l':
			loadkeys = B_TRUE;
			break;
		case 'n':
			flags.dryrun = B_TRUE;
			break;
		case 'o':
			if ((propval = strchr(optarg, '=')) != NULL) {
				*propval = '\0';
				propval++;
				if (add_prop_list(optarg, propval,
				    &props, B_TRUE) != 0) {
					nvlist_free(props);
					zpool_usage(B_FALSE);
				}
			} else {
				mntopts = optarg;
			}
			break;
		case 'P':
			flags.name_flags |= VDEV_NAME_PATH;
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
			break;
		}
	}

	if (!flags.import && mntopts != NULL) {
		(void) fprintf(stderr, gettext("setting mntopts is only "
		    "valid when importing the pool\n"));
		zpool_usage(B_FALSE);
	}

	if (!flags.import && loadkeys) {
		(void) fprintf(stderr, gettext("loading keys is only "
		    "valid when importing the pool\n"));
		zpool_usage(B_FALSE);
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("Missing pool name\n"));
		zpool_usage(B_FALSE);
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("Missing new pool name\n"));
		zpool_usage(B_FALSE);
	}

	srcpool = argv[0];
	newpool = argv[1];

	argc -= 2;
	argv += 2;

	if ((zhp = zpool_open(g_zfs, srcpool)) == NULL) {
		nvlist_free(props);
		return (1);
	}

	config = split_mirror_vdev(zhp, newpool, props, flags, argc, argv);
	if (config == NULL) {
		ret = 1;
	} else {
		if (flags.dryrun) {
			(void) printf(gettext("would create '%s' with the "
			    "following layout:\n\n"), newpool);
			print_vdev_tree(NULL, newpool, config, 0, "",
			    flags.name_flags);
			print_vdev_tree(NULL, "dedup", config, 0,
			    VDEV_ALLOC_BIAS_DEDUP, 0);
			print_vdev_tree(NULL, "special", config, 0,
			    VDEV_ALLOC_BIAS_SPECIAL, 0);
		}
	}

	zpool_close(zhp);

	if (ret != 0 || flags.dryrun || !flags.import) {
		nvlist_free(config);
		nvlist_free(props);
		return (ret);
	}

	/*
	 * The split was successful. Now we need to open the new
	 * pool and import it.
	 */
	if ((zhp = zpool_open_canfail(g_zfs, newpool)) == NULL) {
		nvlist_free(config);
		nvlist_free(props);
		return (1);
	}

	if (loadkeys) {
		ret = zfs_crypto_attempt_load_keys(g_zfs, newpool);
		if (ret != 0)
			ret = 1;
	}

	if (zpool_get_state(zhp) != POOL_STATE_UNAVAIL) {
		ms_status = zpool_enable_datasets(zhp, mntopts, 0);
		if (ms_status == EZFS_SHAREFAILED) {
			(void) fprintf(stderr, gettext("Split was successful, "
			    "datasets are mounted but sharing of some datasets "
			    "has failed\n"));
		} else if (ms_status == EZFS_MOUNTFAILED) {
			(void) fprintf(stderr, gettext("Split was successful"
			    ", but some datasets could not be mounted\n"));
			(void) fprintf(stderr, gettext("Try doing '%s' with a "
			    "different altroot\n"), "zpool import");
		}
	}
	zpool_close(zhp);
	nvlist_free(config);
	nvlist_free(props);

	return (ret);
}
