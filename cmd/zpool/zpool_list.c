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

typedef struct list_cbdata {
	boolean_t	cb_verbose;
	int		cb_name_flags;
	int		cb_namewidth;
	boolean_t	cb_scripted;
	zprop_list_t	*cb_proplist;
	boolean_t	cb_literal;
} list_cbdata_t;

/*
 * Given a list of columns to display, output appropriate headers for each one.
 */
static void
print_header(list_cbdata_t *cb)
{
	zprop_list_t *pl = cb->cb_proplist;
	char headerbuf[ZPOOL_MAXPROPLEN];
	const char *header;
	boolean_t first = B_TRUE;
	boolean_t right_justify;
	size_t width = 0;

	for (; pl != NULL; pl = pl->pl_next) {
		width = pl->pl_width;
		if (first && cb->cb_verbose) {
			/*
			 * Reset the width to accommodate the verbose listing
			 * of devices.
			 */
			width = cb->cb_namewidth;
		}

		if (!first)
			(void) fputs("  ", stdout);
		else
			first = B_FALSE;

		right_justify = B_FALSE;
		if (pl->pl_prop != ZPROP_USERPROP) {
			header = zpool_prop_column_name(pl->pl_prop);
			right_justify = zpool_prop_align_right(pl->pl_prop);
		} else {
			int i;

			for (i = 0; pl->pl_user_prop[i] != '\0'; i++)
				headerbuf[i] = toupper(pl->pl_user_prop[i]);
			headerbuf[i] = '\0';
			header = headerbuf;
		}

		if (pl->pl_next == NULL && !right_justify)
			(void) fputs(header, stdout);
		else if (right_justify)
			(void) printf("%*s", (int)width, header);
		else
			(void) printf("%-*s", (int)width, header);
	}

	(void) fputc('\n', stdout);
}

/*
 * Given a pool and a list of properties, print out all the properties according
 * to the described layout. Used by zpool_do_list().
 */
static void
print_pool(zpool_handle_t *zhp, list_cbdata_t *cb)
{
	zprop_list_t *pl = cb->cb_proplist;
	boolean_t first = B_TRUE;
	char property[ZPOOL_MAXPROPLEN];
	const char *propstr;
	boolean_t right_justify;
	size_t width;

	for (; pl != NULL; pl = pl->pl_next) {

		width = pl->pl_width;
		if (first && cb->cb_verbose) {
			/*
			 * Reset the width to accommodate the verbose listing
			 * of devices.
			 */
			width = cb->cb_namewidth;
		}

		if (!first) {
			if (cb->cb_scripted)
				(void) fputc('\t', stdout);
			else
				(void) fputs("  ", stdout);
		} else {
			first = B_FALSE;
		}

		right_justify = B_FALSE;
		if (pl->pl_prop != ZPROP_USERPROP) {
			if (zpool_get_prop(zhp, pl->pl_prop, property,
			    sizeof (property), NULL, cb->cb_literal) != 0)
				propstr = "-";
			else
				propstr = property;

			right_justify = zpool_prop_align_right(pl->pl_prop);
		} else if ((zpool_prop_feature(pl->pl_user_prop) ||
		    zpool_prop_unsupported(pl->pl_user_prop)) &&
		    zpool_prop_get_feature(zhp, pl->pl_user_prop, property,
		    sizeof (property)) == 0) {
			propstr = property;
		} else if (zfs_prop_user(pl->pl_user_prop) &&
		    zpool_get_userprop(zhp, pl->pl_user_prop, property,
		    sizeof (property), NULL) == 0) {
			propstr = property;
		} else {
			propstr = "-";
		}

		/*
		 * If this is being called in scripted mode, or if this is the
		 * last column and it is left-justified, don't include a width
		 * format specifier.
		 */
		if (cb->cb_scripted || (pl->pl_next == NULL && !right_justify))
			(void) fputs(propstr, stdout);
		else if (right_justify)
			(void) printf("%*s", (int)width, propstr);
		else
			(void) printf("%-*s", (int)width, propstr);
	}

	(void) fputc('\n', stdout);
}

static void
print_one_column(zpool_prop_t prop, uint64_t value, const char *str,
    boolean_t scripted, boolean_t valid, enum zfs_nicenum_format format)
{
	char propval[64];
	boolean_t fixed;
	size_t width = zprop_width(prop, &fixed, ZFS_TYPE_POOL);

	switch (prop) {
	case ZPOOL_PROP_SIZE:
	case ZPOOL_PROP_EXPANDSZ:
	case ZPOOL_PROP_CHECKPOINT:
	case ZPOOL_PROP_DEDUPRATIO:
		if (value == 0)
			(void) strlcpy(propval, "-", sizeof (propval));
		else
			zfs_nicenum_format(value, propval, sizeof (propval),
			    format);
		break;
	case ZPOOL_PROP_FRAGMENTATION:
		if (value == ZFS_FRAG_INVALID) {
			(void) strlcpy(propval, "-", sizeof (propval));
		} else if (format == ZFS_NICENUM_RAW) {
			(void) snprintf(propval, sizeof (propval), "%llu",
			    (unsigned long long)value);
		} else {
			(void) snprintf(propval, sizeof (propval), "%llu%%",
			    (unsigned long long)value);
		}
		break;
	case ZPOOL_PROP_CAPACITY:
		/* capacity value is in parts-per-10,000 (aka permyriad) */
		if (format == ZFS_NICENUM_RAW)
			(void) snprintf(propval, sizeof (propval), "%llu",
			    (unsigned long long)value / 100);
		else
			(void) snprintf(propval, sizeof (propval),
			    value < 1000 ? "%1.2f%%" : value < 10000 ?
			    "%2.1f%%" : "%3.0f%%", value / 100.0);
		break;
	case ZPOOL_PROP_HEALTH:
		width = 8;
		(void) strlcpy(propval, str, sizeof (propval));
		break;
	default:
		zfs_nicenum_format(value, propval, sizeof (propval), format);
	}

	if (!valid)
		(void) strlcpy(propval, "-", sizeof (propval));

	if (scripted)
		(void) printf("\t%s", propval);
	else
		(void) printf("  %*s", (int)width, propval);
}

static const char *const class_name[] = {
	VDEV_ALLOC_BIAS_DEDUP,
	VDEV_ALLOC_BIAS_SPECIAL,
	VDEV_ALLOC_CLASS_LOGS
};

/*
 * print static default line per vdev
 * not compatible with '-o' <proplist> option
 */
static void
print_list_stats(zpool_handle_t *zhp, const char *name, nvlist_t *nv,
    list_cbdata_t *cb, int depth, boolean_t isspare)
{
	nvlist_t **child;
	vdev_stat_t *vs;
	uint_t c, children;
	char *vname;
	boolean_t scripted = cb->cb_scripted;
	uint64_t islog = B_FALSE;
	const char *dashes = "%-*s      -      -      -        -         "
	    "-      -      -      -         -\n";

	verify(nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);

	if (name != NULL) {
		boolean_t toplevel = (vs->vs_space != 0);
		uint64_t cap;
		enum zfs_nicenum_format format;
		const char *state;

		if (cb->cb_literal)
			format = ZFS_NICENUM_RAW;
		else
			format = ZFS_NICENUM_1024;

		if (strcmp(name, VDEV_TYPE_INDIRECT) == 0)
			return;

		if (scripted)
			(void) printf("\t%s", name);
		else if (strlen(name) + depth > cb->cb_namewidth)
			(void) printf("%*s%s", depth, "", name);
		else
			(void) printf("%*s%s%*s", depth, "", name,
			    (int)(cb->cb_namewidth - strlen(name) - depth), "");

		/*
		 * Print the properties for the individual vdevs. Some
		 * properties are only applicable to toplevel vdevs. The
		 * 'toplevel' boolean value is passed to the print_one_column()
		 * to indicate that the value is valid.
		 */
		if (VDEV_STAT_VALID(vs_pspace, c) && vs->vs_pspace)
			print_one_column(ZPOOL_PROP_SIZE, vs->vs_pspace, NULL,
			    scripted, B_TRUE, format);
		else
			print_one_column(ZPOOL_PROP_SIZE, vs->vs_space, NULL,
			    scripted, toplevel, format);
		print_one_column(ZPOOL_PROP_ALLOCATED, vs->vs_alloc, NULL,
		    scripted, toplevel, format);
		print_one_column(ZPOOL_PROP_FREE, vs->vs_space - vs->vs_alloc,
		    NULL, scripted, toplevel, format);
		print_one_column(ZPOOL_PROP_CHECKPOINT,
		    vs->vs_checkpoint_space, NULL, scripted, toplevel, format);
		print_one_column(ZPOOL_PROP_EXPANDSZ, vs->vs_esize, NULL,
		    scripted, B_TRUE, format);
		print_one_column(ZPOOL_PROP_FRAGMENTATION,
		    vs->vs_fragmentation, NULL, scripted,
		    (vs->vs_fragmentation != ZFS_FRAG_INVALID && toplevel),
		    format);
		cap = (vs->vs_space == 0) ? 0 :
		    (vs->vs_alloc * 10000 / vs->vs_space);
		print_one_column(ZPOOL_PROP_CAPACITY, cap, NULL,
		    scripted, toplevel, format);
		print_one_column(ZPOOL_PROP_DEDUPRATIO, 0, NULL,
		    scripted, toplevel, format);
		state = zpool_state_to_name(vs->vs_state, vs->vs_aux);
		if (isspare) {
			if (vs->vs_aux == VDEV_AUX_SPARED)
				state = "INUSE";
			else if (vs->vs_state == VDEV_STATE_HEALTHY)
				state = "AVAIL";
		}
		print_one_column(ZPOOL_PROP_HEALTH, 0, state, scripted,
		    B_TRUE, format);
		(void) fputc('\n', stdout);
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return;

	/* list the normal vdevs first */
	for (c = 0; c < children; c++) {
		uint64_t ishole = B_FALSE;

		if (nvlist_lookup_uint64(child[c],
		    ZPOOL_CONFIG_IS_HOLE, &ishole) == 0 && ishole)
			continue;

		if (nvlist_lookup_uint64(child[c],
		    ZPOOL_CONFIG_IS_LOG, &islog) == 0 && islog)
			continue;

		if (nvlist_exists(child[c], ZPOOL_CONFIG_ALLOCATION_BIAS))
			continue;

		vname = zpool_vdev_name(g_zfs, zhp, child[c],
		    cb->cb_name_flags | VDEV_NAME_TYPE_ID);
		print_list_stats(zhp, vname, child[c], cb, depth + 2, B_FALSE);
		free(vname);
	}

	/* list the classes: 'logs', 'dedup', and 'special' */
	for (uint_t n = 0; n < ARRAY_SIZE(class_name); n++) {
		boolean_t printed = B_FALSE;

		for (c = 0; c < children; c++) {
			const char *bias = NULL;
			const char *type = NULL;

			if (nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
			    &islog) == 0 && islog) {
				bias = VDEV_ALLOC_CLASS_LOGS;
			} else {
				(void) nvlist_lookup_string(child[c],
				    ZPOOL_CONFIG_ALLOCATION_BIAS, &bias);
				(void) nvlist_lookup_string(child[c],
				    ZPOOL_CONFIG_TYPE, &type);
			}
			if (bias == NULL || strcmp(bias, class_name[n]) != 0)
				continue;
			if (!islog && strcmp(type, VDEV_TYPE_INDIRECT) == 0)
				continue;

			if (!printed) {
				/* LINTED E_SEC_PRINTF_VAR_FMT */
				(void) printf(dashes, cb->cb_namewidth,
				    class_name[n]);
				printed = B_TRUE;
			}
			vname = zpool_vdev_name(g_zfs, zhp, child[c],
			    cb->cb_name_flags | VDEV_NAME_TYPE_ID);
			print_list_stats(zhp, vname, child[c], cb, depth + 2,
			    B_FALSE);
			free(vname);
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0 && children > 0) {
		/* LINTED E_SEC_PRINTF_VAR_FMT */
		(void) printf(dashes, cb->cb_namewidth, "cache");
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, zhp, child[c],
			    cb->cb_name_flags);
			print_list_stats(zhp, vname, child[c], cb, depth + 2,
			    B_FALSE);
			free(vname);
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES, &child,
	    &children) == 0 && children > 0) {
		/* LINTED E_SEC_PRINTF_VAR_FMT */
		(void) printf(dashes, cb->cb_namewidth, "spare");
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, zhp, child[c],
			    cb->cb_name_flags);
			print_list_stats(zhp, vname, child[c], cb, depth + 2,
			    B_TRUE);
			free(vname);
		}
	}
}

/*
 * Generic callback function to list a pool.
 */
static int
list_callback(zpool_handle_t *zhp, void *data)
{
	list_cbdata_t *cbp = data;

	print_pool(zhp, cbp);

	if (cbp->cb_verbose) {
		nvlist_t *config, *nvroot;

		config = zpool_get_config(zhp, NULL);
		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &nvroot) == 0);
		print_list_stats(zhp, NULL, nvroot, cbp, 0, B_FALSE);
	}

	return (0);
}

/*
 * Set the minimum pool/vdev name column width.  The width must be at least 9,
 * but may be as large as needed.
 */
static int
get_namewidth_list(zpool_handle_t *zhp, void *data)
{
	list_cbdata_t *cb = data;
	int width;

	width = get_namewidth(zhp, cb->cb_namewidth,
	    cb->cb_name_flags | VDEV_NAME_TYPE_ID, cb->cb_verbose);

	if (width < 9)
		width = 9;

	cb->cb_namewidth = width;

	return (0);
}

/*
 * zpool list [-gHLpP] [-o prop[,prop]*] [-T d|u] [pool] ... [interval [count]]
 *
 *	-g	Display guid for individual vdev name.
 *	-H	Scripted mode.  Don't display headers, and separate properties
 *		by a single tab.
 *	-L	Follow links when resolving vdev path name.
 *	-o	List of properties to display.  Defaults to
 *		"name,size,allocated,free,expandsize,fragmentation,capacity,"
 *		"dedupratio,health,altroot"
 *	-p	Display values in parsable (exact) format.
 *	-P	Display full path for vdev name.
 *	-T	Display a timestamp in date(1) or Unix format
 *
 * List all pools in the system, whether or not they're healthy.  Output space
 * statistics for each one, as well as health status summary.
 */
int
zpool_do_list(int argc, char **argv)
{
	int c;
	int ret = 0;
	list_cbdata_t cb = { 0 };
	static char default_props[] =
	    "name,size,allocated,free,checkpoint,expandsize,fragmentation,"
	    "capacity,dedupratio,health,altroot";
	char *props = default_props;
	float interval = 0;
	unsigned long count = 0;
	zpool_list_t *list;
	boolean_t first = B_TRUE;
	current_prop_type = ZFS_TYPE_POOL;

	/* check options */
	while ((c = getopt(argc, argv, ":gHLo:pPT:v")) != -1) {
		switch (c) {
		case 'g':
			cb.cb_name_flags |= VDEV_NAME_GUID;
			break;
		case 'H':
			cb.cb_scripted = B_TRUE;
			break;
		case 'L':
			cb.cb_name_flags |= VDEV_NAME_FOLLOW_LINKS;
			break;
		case 'o':
			props = optarg;
			break;
		case 'P':
			cb.cb_name_flags |= VDEV_NAME_PATH;
			break;
		case 'p':
			cb.cb_literal = B_TRUE;
			break;
		case 'T':
			get_timestamp_arg(*optarg);
			break;
		case 'v':
			cb.cb_verbose = B_TRUE;
			cb.cb_namewidth = 8;	/* 8 until precalc is avail */
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
		}
	}

	argc -= optind;
	argv += optind;

	get_interval_count(&argc, argv, &interval, &count);

	if (zprop_get_list(g_zfs, props, &cb.cb_proplist, ZFS_TYPE_POOL) != 0)
		zpool_usage(B_FALSE);

	for (;;) {
		if ((list = pool_list_get(argc, argv, &cb.cb_proplist,
		    ZFS_TYPE_POOL, cb.cb_literal, &ret)) == NULL)
			return (1);

		if (pool_list_count(list) == 0)
			break;

		cb.cb_namewidth = 0;
		(void) pool_list_iter(list, B_FALSE, get_namewidth_list, &cb);

		if (timestamp_fmt != NODATE)
			print_timestamp(timestamp_fmt);

		if (!cb.cb_scripted && (first || cb.cb_verbose)) {
			print_header(&cb);
			first = B_FALSE;
		}
		ret = pool_list_iter(list, B_TRUE, list_callback, &cb);

		if (interval == 0)
			break;

		if (count != 0 && --count == 0)
			break;

		pool_list_free(list);

		(void) fflush(stdout);
		(void) fsleep(interval);
	}

	if (argc == 0 && !cb.cb_scripted && pool_list_count(list) == 0) {
		(void) printf(gettext("no pools available\n"));
		ret = 0;
	}

	pool_list_free(list);
	zprop_free_list(cb.cb_proplist);
	return (ret);
}

