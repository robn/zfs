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
 * zpool add [-fgLnP] [-o property=value] <pool> <vdev> ...
 *
 *	-f	Force addition of devices, even if they appear in use
 *	-g	Display guid for individual vdev name.
 *	-L	Follow links when resolving vdev path name.
 *	-n	Do not add the devices, but display the resulting layout if
 *		they were to be added.
 *	-o	Set property=value.
 *	-P	Display full path for vdev name.
 *
 * Adds the given vdevs to 'pool'.  As with create, the bulk of this work is
 * handled by make_root_vdev(), which constructs the nvlist needed to pass to
 * libzfs.
 */
int
zpool_do_add(int argc, char **argv)
{
	boolean_t force = B_FALSE;
	boolean_t dryrun = B_FALSE;
	int name_flags = 0;
	int c;
	nvlist_t *nvroot;
	char *poolname;
	int ret;
	zpool_handle_t *zhp;
	nvlist_t *config;
	nvlist_t *props = NULL;
	char *propval;

	/* check options */
	while ((c = getopt(argc, argv, "fgLno:P")) != -1) {
		switch (c) {
		case 'f':
			force = B_TRUE;
			break;
		case 'g':
			name_flags |= VDEV_NAME_GUID;
			break;
		case 'L':
			name_flags |= VDEV_NAME_FOLLOW_LINKS;
			break;
		case 'n':
			dryrun = B_TRUE;
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
		case 'P':
			name_flags |= VDEV_NAME_PATH;
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
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing vdev specification\n"));
		zpool_usage(B_FALSE);
	}

	poolname = argv[0];

	argc--;
	argv++;

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL)
		return (1);

	if ((config = zpool_get_config(zhp, NULL)) == NULL) {
		(void) fprintf(stderr, gettext("pool '%s' is unavailable\n"),
		    poolname);
		zpool_close(zhp);
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

	/* pass off to make_root_vdev for processing */
	nvroot = make_root_vdev(zhp, props, force, !force, B_FALSE, dryrun,
	    argc, argv);
	if (nvroot == NULL) {
		zpool_close(zhp);
		return (1);
	}

	if (dryrun) {
		nvlist_t *poolnvroot;
		nvlist_t **l2child, **sparechild;
		uint_t l2children, sparechildren, c;
		char *vname;
		boolean_t hadcache = B_FALSE, hadspare = B_FALSE;

		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &poolnvroot) == 0);

		(void) printf(gettext("would update '%s' to the following "
		    "configuration:\n\n"), zpool_get_name(zhp));

		/* print original main pool and new tree */
		print_vdev_tree(zhp, poolname, poolnvroot, 0, "",
		    name_flags | VDEV_NAME_TYPE_ID);
		print_vdev_tree(zhp, NULL, nvroot, 0, "", name_flags);

		/* print other classes: 'dedup', 'special', and 'log' */
		if (zfs_special_devs(poolnvroot, VDEV_ALLOC_BIAS_DEDUP)) {
			print_vdev_tree(zhp, "dedup", poolnvroot, 0,
			    VDEV_ALLOC_BIAS_DEDUP, name_flags);
			print_vdev_tree(zhp, NULL, nvroot, 0,
			    VDEV_ALLOC_BIAS_DEDUP, name_flags);
		} else if (zfs_special_devs(nvroot, VDEV_ALLOC_BIAS_DEDUP)) {
			print_vdev_tree(zhp, "dedup", nvroot, 0,
			    VDEV_ALLOC_BIAS_DEDUP, name_flags);
		}

		if (zfs_special_devs(poolnvroot, VDEV_ALLOC_BIAS_SPECIAL)) {
			print_vdev_tree(zhp, "special", poolnvroot, 0,
			    VDEV_ALLOC_BIAS_SPECIAL, name_flags);
			print_vdev_tree(zhp, NULL, nvroot, 0,
			    VDEV_ALLOC_BIAS_SPECIAL, name_flags);
		} else if (zfs_special_devs(nvroot, VDEV_ALLOC_BIAS_SPECIAL)) {
			print_vdev_tree(zhp, "special", nvroot, 0,
			    VDEV_ALLOC_BIAS_SPECIAL, name_flags);
		}

		if (num_logs(poolnvroot) > 0) {
			print_vdev_tree(zhp, "logs", poolnvroot, 0,
			    VDEV_ALLOC_BIAS_LOG, name_flags);
			print_vdev_tree(zhp, NULL, nvroot, 0,
			    VDEV_ALLOC_BIAS_LOG, name_flags);
		} else if (num_logs(nvroot) > 0) {
			print_vdev_tree(zhp, "logs", nvroot, 0,
			    VDEV_ALLOC_BIAS_LOG, name_flags);
		}

		/* Do the same for the caches */
		if (nvlist_lookup_nvlist_array(poolnvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2child, &l2children) == 0 && l2children) {
			hadcache = B_TRUE;
			(void) printf(gettext("\tcache\n"));
			for (c = 0; c < l2children; c++) {
				vname = zpool_vdev_name(g_zfs, NULL,
				    l2child[c], name_flags);
				(void) printf("\t  %s\n", vname);
				free(vname);
			}
		}
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2child, &l2children) == 0 && l2children) {
			if (!hadcache)
				(void) printf(gettext("\tcache\n"));
			for (c = 0; c < l2children; c++) {
				vname = zpool_vdev_name(g_zfs, NULL,
				    l2child[c], name_flags);
				(void) printf("\t  %s\n", vname);
				free(vname);
			}
		}
		/* And finally the spares */
		if (nvlist_lookup_nvlist_array(poolnvroot, ZPOOL_CONFIG_SPARES,
		    &sparechild, &sparechildren) == 0 && sparechildren > 0) {
			hadspare = B_TRUE;
			(void) printf(gettext("\tspares\n"));
			for (c = 0; c < sparechildren; c++) {
				vname = zpool_vdev_name(g_zfs, NULL,
				    sparechild[c], name_flags);
				(void) printf("\t  %s\n", vname);
				free(vname);
			}
		}
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    &sparechild, &sparechildren) == 0 && sparechildren > 0) {
			if (!hadspare)
				(void) printf(gettext("\tspares\n"));
			for (c = 0; c < sparechildren; c++) {
				vname = zpool_vdev_name(g_zfs, NULL,
				    sparechild[c], name_flags);
				(void) printf("\t  %s\n", vname);
				free(vname);
			}
		}

		ret = 0;
	} else {
		ret = (zpool_add(zhp, nvroot) != 0);
	}

	nvlist_free(props);
	nvlist_free(nvroot);
	zpool_close(zhp);

	return (ret);
}
