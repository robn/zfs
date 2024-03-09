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
 * list [-Hp][-r|-d max] [-o property[,...]] [-s property] ... [-S property]
 *      [-t type[,...]] [filesystem|volume|snapshot] ...
 *
 *	-H	Scripted mode; elide headers and separate columns by tabs
 *	-p	Display values in parsable (literal) format.
 *	-r	Recurse over all children
 *	-d	Limit recursion by depth.
 *	-o	Control which fields to display.
 *	-s	Specify sort columns, descending order.
 *	-S	Specify sort columns, ascending order.
 *	-t	Control which object types to display.
 *
 * When given no arguments, list all filesystems in the system.
 * Otherwise, list the specified datasets, optionally recursing down them if
 * '-r' is specified.
 */
typedef struct list_cbdata {
	boolean_t	cb_first;
	boolean_t	cb_literal;
	boolean_t	cb_scripted;
	zprop_list_t	*cb_proplist;
} list_cbdata_t;

/*
 * Given a list of columns to display, output appropriate headers for each one.
 */
static void
print_header(list_cbdata_t *cb)
{
	zprop_list_t *pl = cb->cb_proplist;
	char headerbuf[ZFS_MAXPROPLEN];
	const char *header;
	int i;
	boolean_t first = B_TRUE;
	boolean_t right_justify;

	color_start(ANSI_BOLD);

	for (; pl != NULL; pl = pl->pl_next) {
		if (!first) {
			(void) printf("  ");
		} else {
			first = B_FALSE;
		}

		right_justify = B_FALSE;
		if (pl->pl_prop != ZPROP_USERPROP) {
			header = zfs_prop_column_name(pl->pl_prop);
			right_justify = zfs_prop_align_right(pl->pl_prop);
		} else {
			for (i = 0; pl->pl_user_prop[i] != '\0'; i++)
				headerbuf[i] = toupper(pl->pl_user_prop[i]);
			headerbuf[i] = '\0';
			header = headerbuf;
		}

		if (pl->pl_next == NULL && !right_justify)
			(void) printf("%s", header);
		else if (right_justify)
			(void) printf("%*s", (int)pl->pl_width, header);
		else
			(void) printf("%-*s", (int)pl->pl_width, header);
	}

	color_end();

	(void) printf("\n");
}

/*
 * Decides on the color that the avail value should be printed in.
 * > 80% used = yellow
 * > 90% used = red
 */
static const char *
zfs_list_avail_color(zfs_handle_t *zhp)
{
	uint64_t used = zfs_prop_get_int(zhp, ZFS_PROP_USED);
	uint64_t avail = zfs_prop_get_int(zhp, ZFS_PROP_AVAILABLE);
	int percentage = (int)((double)avail / MAX(avail + used, 1) * 100);

	if (percentage > 20)
		return (NULL);
	else if (percentage > 10)
		return (ANSI_YELLOW);
	else
		return (ANSI_RED);
}

/*
 * Given a dataset and a list of fields, print out all the properties according
 * to the described layout.
 */
static void
print_dataset(zfs_handle_t *zhp, list_cbdata_t *cb)
{
	zprop_list_t *pl = cb->cb_proplist;
	boolean_t first = B_TRUE;
	char property[ZFS_MAXPROPLEN];
	nvlist_t *userprops = zfs_get_user_props(zhp);
	nvlist_t *propval;
	const char *propstr;
	boolean_t right_justify;

	for (; pl != NULL; pl = pl->pl_next) {
		if (!first) {
			if (cb->cb_scripted)
				(void) putchar('\t');
			else
				(void) fputs("  ", stdout);
		} else {
			first = B_FALSE;
		}

		if (pl->pl_prop == ZFS_PROP_NAME) {
			(void) strlcpy(property, zfs_get_name(zhp),
			    sizeof (property));
			propstr = property;
			right_justify = zfs_prop_align_right(pl->pl_prop);
		} else if (pl->pl_prop != ZPROP_USERPROP) {
			if (zfs_prop_get(zhp, pl->pl_prop, property,
			    sizeof (property), NULL, NULL, 0,
			    cb->cb_literal) != 0)
				propstr = "-";
			else
				propstr = property;
			right_justify = zfs_prop_align_right(pl->pl_prop);
		} else if (zfs_prop_userquota(pl->pl_user_prop)) {
			if (zfs_prop_get_userquota(zhp, pl->pl_user_prop,
			    property, sizeof (property), cb->cb_literal) != 0)
				propstr = "-";
			else
				propstr = property;
			right_justify = B_TRUE;
		} else if (zfs_prop_written(pl->pl_user_prop)) {
			if (zfs_prop_get_written(zhp, pl->pl_user_prop,
			    property, sizeof (property), cb->cb_literal) != 0)
				propstr = "-";
			else
				propstr = property;
			right_justify = B_TRUE;
		} else {
			if (nvlist_lookup_nvlist(userprops,
			    pl->pl_user_prop, &propval) != 0)
				propstr = "-";
			else
				propstr = fnvlist_lookup_string(propval,
				    ZPROP_VALUE);
			right_justify = B_FALSE;
		}

		/*
		 * zfs_list_avail_color() needs ZFS_PROP_AVAILABLE + USED
		 * - so we need another for() search for the USED part
		 * - when no colors wanted, we can skip the whole thing
		 */
		if (use_color() && pl->pl_prop == ZFS_PROP_AVAILABLE) {
			zprop_list_t *pl2 = cb->cb_proplist;
			for (; pl2 != NULL; pl2 = pl2->pl_next) {
				if (pl2->pl_prop == ZFS_PROP_USED) {
					color_start(zfs_list_avail_color(zhp));
					/* found it, no need for more loops */
					break;
				}
			}
		}

		/*
		 * If this is being called in scripted mode, or if this is the
		 * last column and it is left-justified, don't include a width
		 * format specifier.
		 */
		if (cb->cb_scripted || (pl->pl_next == NULL && !right_justify))
			(void) fputs(propstr, stdout);
		else if (right_justify)
			(void) printf("%*s", (int)pl->pl_width, propstr);
		else
			(void) printf("%-*s", (int)pl->pl_width, propstr);

		if (pl->pl_prop == ZFS_PROP_AVAILABLE)
			color_end();
	}

	(void) putchar('\n');
}

/*
 * Generic callback function to list a dataset or snapshot.
 */
static int
list_callback(zfs_handle_t *zhp, void *data)
{
	list_cbdata_t *cbp = data;

	if (cbp->cb_first) {
		if (!cbp->cb_scripted)
			print_header(cbp);
		cbp->cb_first = B_FALSE;
	}

	print_dataset(zhp, cbp);

	return (0);
}

int
zfs_do_list(int argc, char **argv)
{
	int c;
	char default_fields[] =
	    "name,used,available,referenced,mountpoint";
	int types = ZFS_TYPE_DATASET;
	boolean_t types_specified = B_FALSE;
	char *fields = default_fields;
	list_cbdata_t cb = { 0 };
	int limit = 0;
	int ret = 0;
	zfs_sort_column_t *sortcol = NULL;
	int flags = ZFS_ITER_PROP_LISTSNAPS | ZFS_ITER_ARGS_CAN_BE_PATHS;

	/* check options */
	while ((c = getopt(argc, argv, "HS:d:o:prs:t:")) != -1) {
		switch (c) {
		case 'o':
			fields = optarg;
			break;
		case 'p':
			cb.cb_literal = B_TRUE;
			flags |= ZFS_ITER_LITERAL_PROPS;
			break;
		case 'd':
			limit = parse_depth(optarg, &flags);
			break;
		case 'r':
			flags |= ZFS_ITER_RECURSE;
			break;
		case 'H':
			cb.cb_scripted = B_TRUE;
			break;
		case 's':
			if (zfs_add_sort_column(&sortcol, optarg,
			    B_FALSE) != 0) {
				(void) fprintf(stderr,
				    gettext("invalid property '%s'\n"), optarg);
				usage(B_FALSE);
			}
			break;
		case 'S':
			if (zfs_add_sort_column(&sortcol, optarg,
			    B_TRUE) != 0) {
				(void) fprintf(stderr,
				    gettext("invalid property '%s'\n"), optarg);
				usage(B_FALSE);
			}
			break;
		case 't':
			types = 0;
			types_specified = B_TRUE;
			flags &= ~ZFS_ITER_PROP_LISTSNAPS;

			for (char *tok; (tok = strsep(&optarg, ",")); ) {
				static const char *const type_subopts[] = {
					"filesystem",
					"fs",
					"volume",
					"vol",
					"snapshot",
					"snap",
					"bookmark",
					"all"
				};
				static const int type_types[] = {
					ZFS_TYPE_FILESYSTEM,
					ZFS_TYPE_FILESYSTEM,
					ZFS_TYPE_VOLUME,
					ZFS_TYPE_VOLUME,
					ZFS_TYPE_SNAPSHOT,
					ZFS_TYPE_SNAPSHOT,
					ZFS_TYPE_BOOKMARK,
					ZFS_TYPE_DATASET | ZFS_TYPE_BOOKMARK
				};

				for (c = 0; c < ARRAY_SIZE(type_subopts); ++c)
					if (strcmp(tok, type_subopts[c]) == 0) {
						types |= type_types[c];
						goto found3;
					}

				(void) fprintf(stderr,
				    gettext("invalid type '%s'\n"), tok);
				usage(B_FALSE);
found3:;
			}
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage(B_FALSE);
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/*
	 * If "-o space" and no types were specified, don't display snapshots.
	 */
	if (strcmp(fields, "space") == 0 && types_specified == B_FALSE)
		types &= ~ZFS_TYPE_SNAPSHOT;

	/*
	 * Handle users who want to list all snapshots or bookmarks
	 * of the current dataset (ex. 'zfs list -t snapshot <dataset>').
	 */
	if ((types == ZFS_TYPE_SNAPSHOT || types == ZFS_TYPE_BOOKMARK) &&
	    argc > 0 && (flags & ZFS_ITER_RECURSE) == 0 && limit == 0) {
		flags |= (ZFS_ITER_DEPTH_LIMIT | ZFS_ITER_RECURSE);
		limit = 1;
	}

	/*
	 * If the user specifies '-o all', the zprop_get_list() doesn't
	 * normally include the name of the dataset.  For 'zfs list', we always
	 * want this property to be first.
	 */
	if (zprop_get_list(g_zfs, fields, &cb.cb_proplist, ZFS_TYPE_DATASET)
	    != 0)
		usage(B_FALSE);

	cb.cb_first = B_TRUE;

	/*
	 * If we are only going to list and sort by properties that are "fast"
	 * then we can use "simple" mode and avoid populating the properties
	 * nvlist.
	 */
	if (zfs_list_only_by_fast(cb.cb_proplist) &&
	    zfs_sort_only_by_fast(sortcol))
		flags |= ZFS_ITER_SIMPLE;

	ret = zfs_for_each(argc, argv, flags, types, sortcol, &cb.cb_proplist,
	    limit, list_callback, &cb);

	zprop_free_list(cb.cb_proplist);
	zfs_free_sort_columns(sortcol);

	if (ret == 0 && cb.cb_first && !cb.cb_scripted)
		(void) fprintf(stderr, gettext("no datasets available\n"));

	return (ret);
}
