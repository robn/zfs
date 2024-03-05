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
get_callback_vdev(zpool_handle_t *zhp, char *vdevname, void *data)
{
	zprop_get_cbdata_t *cbp = (zprop_get_cbdata_t *)data;
	char value[ZFS_MAXPROPLEN];
	zprop_source_t srctype;

	for (zprop_list_t *pl = cbp->cb_proplist; pl != NULL;
	    pl = pl->pl_next) {
		char *prop_name;
		/*
		 * If the first property is pool name, it is a special
		 * placeholder that we can skip. This will also skip
		 * over the name property when 'all' is specified.
		 */
		if (pl->pl_prop == ZPOOL_PROP_NAME &&
		    pl == cbp->cb_proplist)
			continue;

		if (pl->pl_prop == ZPROP_INVAL) {
			prop_name = pl->pl_user_prop;
		} else {
			prop_name = (char *)vdev_prop_to_name(pl->pl_prop);
		}
		if (zpool_get_vdev_prop(zhp, vdevname, pl->pl_prop,
		    prop_name, value, sizeof (value), &srctype,
		    cbp->cb_literal) == 0) {
			zprop_print_one_property(vdevname, cbp, prop_name,
			    value, srctype, NULL, NULL);
		}
	}

	return (0);
}

static int
get_callback_vdev_cb(void *zhp_data, nvlist_t *nv, void *data)
{
	zpool_handle_t *zhp = zhp_data;
	zprop_get_cbdata_t *cbp = (zprop_get_cbdata_t *)data;
	char *vdevname;
	const char *type;
	int ret;

	/*
	 * zpool_vdev_name() transforms the root vdev name (i.e., root-0) to the
	 * pool name for display purposes, which is not desired. Fallback to
	 * zpool_vdev_name() when not dealing with the root vdev.
	 */
	type = fnvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE);
	if (zhp != NULL && strcmp(type, "root") == 0)
		vdevname = strdup("root-0");
	else
		vdevname = zpool_vdev_name(g_zfs, zhp, nv,
		    cbp->cb_vdevs.cb_name_flags);

	(void) vdev_expand_proplist(zhp, vdevname, &cbp->cb_proplist);

	ret = get_callback_vdev(zhp, vdevname, data);

	free(vdevname);

	return (ret);
}

static int
get_callback(zpool_handle_t *zhp, void *data)
{
	zprop_get_cbdata_t *cbp = (zprop_get_cbdata_t *)data;
	char value[ZFS_MAXPROPLEN];
	zprop_source_t srctype;
	zprop_list_t *pl;
	int vid;

	if (cbp->cb_type == ZFS_TYPE_VDEV) {
		if (strcmp(cbp->cb_vdevs.cb_names[0], "all-vdevs") == 0) {
			for_each_vdev(zhp, get_callback_vdev_cb, data);
		} else {
			/* Adjust column widths for vdev properties */
			for (vid = 0; vid < cbp->cb_vdevs.cb_names_count;
			    vid++) {
				vdev_expand_proplist(zhp,
				    cbp->cb_vdevs.cb_names[vid],
				    &cbp->cb_proplist);
			}
			/* Display the properties */
			for (vid = 0; vid < cbp->cb_vdevs.cb_names_count;
			    vid++) {
				get_callback_vdev(zhp,
				    cbp->cb_vdevs.cb_names[vid], data);
			}
		}
	} else {
		assert(cbp->cb_type == ZFS_TYPE_POOL);
		for (pl = cbp->cb_proplist; pl != NULL; pl = pl->pl_next) {
			/*
			 * Skip the special fake placeholder. This will also
			 * skip over the name property when 'all' is specified.
			 */
			if (pl->pl_prop == ZPOOL_PROP_NAME &&
			    pl == cbp->cb_proplist)
				continue;

			if (pl->pl_prop == ZPROP_INVAL &&
			    zfs_prop_user(pl->pl_user_prop)) {
				srctype = ZPROP_SRC_LOCAL;

				if (zpool_get_userprop(zhp, pl->pl_user_prop,
				    value, sizeof (value), &srctype) != 0)
					continue;

				zprop_print_one_property(zpool_get_name(zhp),
				    cbp, pl->pl_user_prop, value, srctype,
				    NULL, NULL);
			} else if (pl->pl_prop == ZPROP_INVAL &&
			    (zpool_prop_feature(pl->pl_user_prop) ||
			    zpool_prop_unsupported(pl->pl_user_prop))) {
				srctype = ZPROP_SRC_LOCAL;

				if (zpool_prop_get_feature(zhp,
				    pl->pl_user_prop, value,
				    sizeof (value)) == 0) {
					zprop_print_one_property(
					    zpool_get_name(zhp), cbp,
					    pl->pl_user_prop, value, srctype,
					    NULL, NULL);
				}
			} else {
				if (zpool_get_prop(zhp, pl->pl_prop, value,
				    sizeof (value), &srctype,
				    cbp->cb_literal) != 0)
					continue;

				zprop_print_one_property(zpool_get_name(zhp),
				    cbp, zpool_prop_to_name(pl->pl_prop),
				    value, srctype, NULL, NULL);
			}
		}
	}

	return (0);
}

/*
 * zpool get [-Hp] [-o "all" | field[,...]] <"all" | property[,...]> <pool> ...
 *
 *	-H	Scripted mode.  Don't display headers, and separate properties
 *		by a single tab.
 *	-o	List of columns to display.  Defaults to
 *		"name,property,value,source".
 * 	-p	Display values in parsable (exact) format.
 *
 * Get properties of pools in the system. Output space statistics
 * for each one as well as other attributes.
 */
int
zpool_do_get(int argc, char **argv)
{
	zprop_get_cbdata_t cb = { 0 };
	zprop_list_t fake_name = { 0 };
	int ret;
	int c, i;
	char *propstr = NULL;
	char *vdev = NULL;

	cb.cb_first = B_TRUE;

	/*
	 * Set up default columns and sources.
	 */
	cb.cb_sources = ZPROP_SRC_ALL;
	cb.cb_columns[0] = GET_COL_NAME;
	cb.cb_columns[1] = GET_COL_PROPERTY;
	cb.cb_columns[2] = GET_COL_VALUE;
	cb.cb_columns[3] = GET_COL_SOURCE;
	cb.cb_type = ZFS_TYPE_POOL;
	cb.cb_vdevs.cb_name_flags |= VDEV_NAME_TYPE_ID;
	current_prop_type = cb.cb_type;

	/* check options */
	while ((c = getopt(argc, argv, ":Hpo:")) != -1) {
		switch (c) {
		case 'p':
			cb.cb_literal = B_TRUE;
			break;
		case 'H':
			cb.cb_scripted = B_TRUE;
			break;
		case 'o':
			memset(&cb.cb_columns, 0, sizeof (cb.cb_columns));
			i = 0;

			for (char *tok; (tok = strsep(&optarg, ",")); ) {
				static const char *const col_opts[] =
				{ "name", "property", "value", "source",
				    "all" };
				static const zfs_get_column_t col_cols[] =
				{ GET_COL_NAME, GET_COL_PROPERTY, GET_COL_VALUE,
				    GET_COL_SOURCE };

				if (i == ZFS_GET_NCOLS - 1) {
					(void) fprintf(stderr, gettext("too "
					"many fields given to -o "
					"option\n"));
					zpool_usage(B_FALSE);
				}

				for (c = 0; c < ARRAY_SIZE(col_opts); ++c)
					if (strcmp(tok, col_opts[c]) == 0)
						goto found;

				(void) fprintf(stderr,
				    gettext("invalid column name '%s'\n"), tok);
				zpool_usage(B_FALSE);

found:
				if (c >= 4) {
					if (i > 0) {
						(void) fprintf(stderr,
						    gettext("\"all\" conflicts "
						    "with specific fields "
						    "given to -o option\n"));
						zpool_usage(B_FALSE);
					}

					memcpy(cb.cb_columns, col_cols,
					    sizeof (col_cols));
					i = ZFS_GET_NCOLS - 1;
				} else
					cb.cb_columns[i++] = col_cols[c];
			}
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			zpool_usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing property "
		    "argument\n"));
		zpool_usage(B_FALSE);
	}

	/* Properties list is needed later by zprop_get_list() */
	propstr = argv[0];

	argc--;
	argv++;

	if (argc == 0) {
		/* No args, so just print the defaults. */
	} else if (are_all_pools(argc, argv)) {
		/* All the args are pool names */
	} else if (are_all_pools(1, argv)) {
		/* The first arg is a pool name */
		if ((argc == 2 && strcmp(argv[1], "all-vdevs") == 0) ||
		    (argc == 2 && strcmp(argv[1], "root") == 0) ||
		    are_vdevs_in_pool(argc - 1, argv + 1, argv[0],
		    &cb.cb_vdevs)) {

			if (strcmp(argv[1], "root") == 0)
				vdev = strdup("root-0");
			else
				vdev = strdup(argv[1]);

			/* ... and the rest are vdev names */
			cb.cb_vdevs.cb_names = &vdev;
			cb.cb_vdevs.cb_names_count = argc - 1;
			cb.cb_type = ZFS_TYPE_VDEV;
			argc = 1; /* One pool to process */
		} else {
			fprintf(stderr, gettext("Expected a list of vdevs in"
			    " \"%s\", but got:\n"), argv[0]);
			error_list_unresolved_vdevs(argc - 1, argv + 1,
			    argv[0], &cb.cb_vdevs);
			fprintf(stderr, "\n");
			zpool_usage(B_FALSE);
			return (1);
		}
	} else {
		/*
		 * The first arg isn't a pool name,
		 */
		fprintf(stderr, gettext("missing pool name.\n"));
		fprintf(stderr, "\n");
		zpool_usage(B_FALSE);
		return (1);
	}

	if (zprop_get_list(g_zfs, propstr, &cb.cb_proplist,
	    cb.cb_type) != 0) {
		/* Use correct list of valid properties (pool or vdev) */
		current_prop_type = cb.cb_type;
		zpool_usage(B_FALSE);
	}

	if (cb.cb_proplist != NULL) {
		fake_name.pl_prop = ZPOOL_PROP_NAME;
		fake_name.pl_width = strlen(gettext("NAME"));
		fake_name.pl_next = cb.cb_proplist;
		cb.cb_proplist = &fake_name;
	}

	ret = for_each_pool(argc, argv, B_TRUE, &cb.cb_proplist, cb.cb_type,
	    cb.cb_literal, get_callback, &cb);

	if (cb.cb_proplist == &fake_name)
		zprop_free_list(fake_name.pl_next);
	else
		zprop_free_list(cb.cb_proplist);

	if (vdev != NULL)
		free(vdev);

	return (ret);
}
