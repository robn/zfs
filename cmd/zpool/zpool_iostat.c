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
 * Flags for stats to display with "zpool iostats"
 */
enum iostat_type {
	IOS_DEFAULT = 0,
	IOS_LATENCY = 1,
	IOS_QUEUES = 2,
	IOS_L_HISTO = 3,
	IOS_RQ_HISTO = 4,
	IOS_COUNT,	/* always last element */
};

/* iostat_type entries as bitmasks */
#define	IOS_DEFAULT_M	(1ULL << IOS_DEFAULT)
#define	IOS_LATENCY_M	(1ULL << IOS_LATENCY)
#define	IOS_QUEUES_M	(1ULL << IOS_QUEUES)
#define	IOS_L_HISTO_M	(1ULL << IOS_L_HISTO)
#define	IOS_RQ_HISTO_M	(1ULL << IOS_RQ_HISTO)

/* Mask of all the histo bits */
#define	IOS_ANYHISTO_M (IOS_L_HISTO_M | IOS_RQ_HISTO_M)

/*
 * Lookup table for iostat flags to nvlist names.  Basically a list
 * of all the nvlists a flag requires.  Also specifies the order in
 * which data gets printed in zpool iostat.
 */
static const char *vsx_type_to_nvlist[IOS_COUNT][15] = {
	[IOS_L_HISTO] = {
	    ZPOOL_CONFIG_VDEV_TOT_R_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_TOT_W_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_DISK_R_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_DISK_W_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_SYNC_R_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_SYNC_W_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_ASYNC_R_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_ASYNC_W_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_SCRUB_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_TRIM_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_REBUILD_LAT_HISTO,
	    NULL},
	[IOS_LATENCY] = {
	    ZPOOL_CONFIG_VDEV_TOT_R_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_TOT_W_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_DISK_R_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_DISK_W_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_TRIM_LAT_HISTO,
	    ZPOOL_CONFIG_VDEV_REBUILD_LAT_HISTO,
	    NULL},
	[IOS_QUEUES] = {
	    ZPOOL_CONFIG_VDEV_SYNC_R_ACTIVE_QUEUE,
	    ZPOOL_CONFIG_VDEV_SYNC_W_ACTIVE_QUEUE,
	    ZPOOL_CONFIG_VDEV_ASYNC_R_ACTIVE_QUEUE,
	    ZPOOL_CONFIG_VDEV_ASYNC_W_ACTIVE_QUEUE,
	    ZPOOL_CONFIG_VDEV_SCRUB_ACTIVE_QUEUE,
	    ZPOOL_CONFIG_VDEV_TRIM_ACTIVE_QUEUE,
	    ZPOOL_CONFIG_VDEV_REBUILD_ACTIVE_QUEUE,
	    NULL},
	[IOS_RQ_HISTO] = {
	    ZPOOL_CONFIG_VDEV_SYNC_IND_R_HISTO,
	    ZPOOL_CONFIG_VDEV_SYNC_AGG_R_HISTO,
	    ZPOOL_CONFIG_VDEV_SYNC_IND_W_HISTO,
	    ZPOOL_CONFIG_VDEV_SYNC_AGG_W_HISTO,
	    ZPOOL_CONFIG_VDEV_ASYNC_IND_R_HISTO,
	    ZPOOL_CONFIG_VDEV_ASYNC_AGG_R_HISTO,
	    ZPOOL_CONFIG_VDEV_ASYNC_IND_W_HISTO,
	    ZPOOL_CONFIG_VDEV_ASYNC_AGG_W_HISTO,
	    ZPOOL_CONFIG_VDEV_IND_SCRUB_HISTO,
	    ZPOOL_CONFIG_VDEV_AGG_SCRUB_HISTO,
	    ZPOOL_CONFIG_VDEV_IND_TRIM_HISTO,
	    ZPOOL_CONFIG_VDEV_AGG_TRIM_HISTO,
	    ZPOOL_CONFIG_VDEV_IND_REBUILD_HISTO,
	    ZPOOL_CONFIG_VDEV_AGG_REBUILD_HISTO,
	    NULL},
};

/*
 * Given a cb->cb_flags with a histogram bit set, return the iostat_type.
 * Right now, only one histo bit is ever set at one time, so we can
 * just do a highbit64(a)
 */
#define	IOS_HISTO_IDX(a)	(highbit64(a & IOS_ANYHISTO_M) - 1)

typedef struct iostat_cbdata {
	uint64_t cb_flags;
	int cb_namewidth;
	int cb_iteration;
	boolean_t cb_verbose;
	boolean_t cb_literal;
	boolean_t cb_scripted;
	zpool_list_t *cb_list;
	vdev_cmd_data_list_t *vcdl;
	vdev_cbdata_t cb_vdevs;
} iostat_cbdata_t;

/*  iostat labels */
typedef struct name_and_columns {
	const char *name;	/* Column name */
	unsigned int columns;	/* Center name to this number of columns */
} name_and_columns_t;

#define	IOSTAT_MAX_LABELS	15	/* Max number of labels on one line */

static const name_and_columns_t iostat_top_labels[][IOSTAT_MAX_LABELS] =
{
	[IOS_DEFAULT] = {{"capacity", 2}, {"operations", 2}, {"bandwidth", 2},
	    {NULL}},
	[IOS_LATENCY] = {{"total_wait", 2}, {"disk_wait", 2}, {"syncq_wait", 2},
	    {"asyncq_wait", 2}, {"scrub", 1}, {"trim", 1}, {"rebuild", 1},
	    {NULL}},
	[IOS_QUEUES] = {{"syncq_read", 2}, {"syncq_write", 2},
	    {"asyncq_read", 2}, {"asyncq_write", 2}, {"scrubq_read", 2},
	    {"trimq_write", 2}, {"rebuildq_write", 2}, {NULL}},
	[IOS_L_HISTO] = {{"total_wait", 2}, {"disk_wait", 2}, {"syncq_wait", 2},
	    {"asyncq_wait", 2}, {NULL}},
	[IOS_RQ_HISTO] = {{"sync_read", 2}, {"sync_write", 2},
	    {"async_read", 2}, {"async_write", 2}, {"scrub", 2},
	    {"trim", 2}, {"rebuild", 2}, {NULL}},
};

/* Shorthand - if "columns" field not set, default to 1 column */
static const name_and_columns_t iostat_bottom_labels[][IOSTAT_MAX_LABELS] =
{
	[IOS_DEFAULT] = {{"alloc"}, {"free"}, {"read"}, {"write"}, {"read"},
	    {"write"}, {NULL}},
	[IOS_LATENCY] = {{"read"}, {"write"}, {"read"}, {"write"}, {"read"},
	    {"write"}, {"read"}, {"write"}, {"wait"}, {"wait"}, {"wait"},
	    {NULL}},
	[IOS_QUEUES] = {{"pend"}, {"activ"}, {"pend"}, {"activ"}, {"pend"},
	    {"activ"}, {"pend"}, {"activ"}, {"pend"}, {"activ"},
	    {"pend"}, {"activ"}, {"pend"}, {"activ"}, {NULL}},
	[IOS_L_HISTO] = {{"read"}, {"write"}, {"read"}, {"write"}, {"read"},
	    {"write"}, {"read"}, {"write"}, {"scrub"}, {"trim"}, {"rebuild"},
	    {NULL}},
	[IOS_RQ_HISTO] = {{"ind"}, {"agg"}, {"ind"}, {"agg"}, {"ind"}, {"agg"},
	    {"ind"}, {"agg"}, {"ind"}, {"agg"}, {"ind"}, {"agg"},
	    {"ind"}, {"agg"}, {NULL}},
};

static const char *histo_to_title[] = {
	[IOS_L_HISTO] = "latency",
	[IOS_RQ_HISTO] = "req_size",
};

/*
 * Return the number of labels in a null-terminated name_and_columns_t
 * array.
 *
 */
static unsigned int
label_array_len(const name_and_columns_t *labels)
{
	int i = 0;

	while (labels[i].name)
		i++;

	return (i);
}

/*
 * Return the number of strings in a null-terminated string array.
 * For example:
 *
 *     const char foo[] = {"bar", "baz", NULL}
 *
 * returns 2
 */
static uint64_t
str_array_len(const char *array[])
{
	uint64_t i = 0;
	while (array[i])
		i++;

	return (i);
}


/*
 * Return a default column width for default/latency/queue columns. This does
 * not include histograms, which have their columns autosized.
 */
static unsigned int
default_column_width(iostat_cbdata_t *cb, enum iostat_type type)
{
	unsigned long column_width = 5; /* Normal niceprint */
	static unsigned long widths[] = {
		/*
		 * Choose some sane default column sizes for printing the
		 * raw numbers.
		 */
		[IOS_DEFAULT] = 15, /* 1PB capacity */
		[IOS_LATENCY] = 10, /* 1B ns = 10sec */
		[IOS_QUEUES] = 6,   /* 1M queue entries */
		[IOS_L_HISTO] = 10, /* 1B ns = 10sec */
		[IOS_RQ_HISTO] = 6, /* 1M queue entries */
	};

	if (cb->cb_literal)
		column_width = widths[type];

	return (column_width);
}

/*
 * Print the column labels, i.e:
 *
 *   capacity     operations     bandwidth
 * alloc   free   read  write   read  write  ...
 *
 * If force_column_width is set, use it for the column width.  If not set, use
 * the default column width.
 */
static void
print_iostat_labels(iostat_cbdata_t *cb, unsigned int force_column_width,
    const name_and_columns_t labels[][IOSTAT_MAX_LABELS])
{
	int i, idx, s;
	int text_start, rw_column_width, spaces_to_end;
	uint64_t flags = cb->cb_flags;
	uint64_t f;
	unsigned int column_width = force_column_width;

	/* For each bit set in flags */
	for (f = flags; f; f &= ~(1ULL << idx)) {
		idx = lowbit64(f) - 1;
		if (!force_column_width)
			column_width = default_column_width(cb, idx);
		/* Print our top labels centered over "read  write" label. */
		for (i = 0; i < label_array_len(labels[idx]); i++) {
			const char *name = labels[idx][i].name;
			/*
			 * We treat labels[][].columns == 0 as shorthand
			 * for one column.  It makes writing out the label
			 * tables more concise.
			 */
			unsigned int columns = MAX(1, labels[idx][i].columns);
			unsigned int slen = strlen(name);

			rw_column_width = (column_width * columns) +
			    (2 * (columns - 1));

			text_start = (int)((rw_column_width) / columns -
			    slen / columns);
			if (text_start < 0)
				text_start = 0;

			printf("  ");	/* Two spaces between columns */

			/* Space from beginning of column to label */
			for (s = 0; s < text_start; s++)
				printf(" ");

			printf("%s", name);

			/* Print space after label to end of column */
			spaces_to_end = rw_column_width - text_start - slen;
			if (spaces_to_end < 0)
				spaces_to_end = 0;

			for (s = 0; s < spaces_to_end; s++)
				printf(" ");
		}
	}
}

/*
 * Utility function to print out a line of dashes like:
 *
 * 	--------------------------------  -----  -----  -----  -----  -----
 *
 * ...or a dashed named-row line like:
 *
 * 	logs                                  -      -      -      -      -
 *
 * @cb:				iostat data
 *
 * @force_column_width		If non-zero, use the value as the column width.
 * 				Otherwise use the default column widths.
 *
 * @name:			Print a dashed named-row line starting
 * 				with @name.  Otherwise, print a regular
 * 				dashed line.
 */
static void
print_iostat_dashes(iostat_cbdata_t *cb, unsigned int force_column_width,
    const char *name)
{
	int i;
	unsigned int namewidth;
	uint64_t flags = cb->cb_flags;
	uint64_t f;
	int idx;
	const name_and_columns_t *labels;
	const char *title;


	if (cb->cb_flags & IOS_ANYHISTO_M) {
		title = histo_to_title[IOS_HISTO_IDX(cb->cb_flags)];
	} else if (cb->cb_vdevs.cb_names_count) {
		title = "vdev";
	} else  {
		title = "pool";
	}

	namewidth = MAX(MAX(strlen(title), cb->cb_namewidth),
	    name ? strlen(name) : 0);


	if (name) {
		printf("%-*s", namewidth, name);
	} else {
		for (i = 0; i < namewidth; i++)
			(void) printf("-");
	}

	/* For each bit in flags */
	for (f = flags; f; f &= ~(1ULL << idx)) {
		unsigned int column_width;
		idx = lowbit64(f) - 1;
		if (force_column_width)
			column_width = force_column_width;
		else
			column_width = default_column_width(cb, idx);

		labels = iostat_bottom_labels[idx];
		for (i = 0; i < label_array_len(labels); i++) {
			if (name)
				printf("  %*s-", column_width - 1, " ");
			else
				printf("  %.*s", column_width,
				    "--------------------");
		}
	}
}


static void
print_iostat_separator_impl(iostat_cbdata_t *cb,
    unsigned int force_column_width)
{
	print_iostat_dashes(cb, force_column_width, NULL);
}

static void
print_iostat_separator(iostat_cbdata_t *cb)
{
	print_iostat_separator_impl(cb, 0);
}

static void
print_iostat_header_impl(iostat_cbdata_t *cb, unsigned int force_column_width,
    const char *histo_vdev_name)
{
	unsigned int namewidth;
	const char *title;

	color_start(ANSI_BOLD);

	if (cb->cb_flags & IOS_ANYHISTO_M) {
		title = histo_to_title[IOS_HISTO_IDX(cb->cb_flags)];
	} else if (cb->cb_vdevs.cb_names_count) {
		title = "vdev";
	} else  {
		title = "pool";
	}

	namewidth = MAX(MAX(strlen(title), cb->cb_namewidth),
	    histo_vdev_name ? strlen(histo_vdev_name) : 0);

	if (histo_vdev_name)
		printf("%-*s", namewidth, histo_vdev_name);
	else
		printf("%*s", namewidth, "");


	print_iostat_labels(cb, force_column_width, iostat_top_labels);
	printf("\n");

	printf("%-*s", namewidth, title);

	print_iostat_labels(cb, force_column_width, iostat_bottom_labels);
	if (cb->vcdl != NULL)
		print_cmd_columns(cb->vcdl, 0);

	printf("\n");

	print_iostat_separator_impl(cb, force_column_width);

	if (cb->vcdl != NULL)
		print_cmd_columns(cb->vcdl, 1);

	color_end();

	printf("\n");
}

static void
print_iostat_header(iostat_cbdata_t *cb)
{
	print_iostat_header_impl(cb, 0, NULL);
}

/*
 * Prints a size string (i.e. 120M) with the suffix ("M") colored
 * by order of magnitude. Uses column_size to add padding.
 */
static void
print_stat_color(const char *statbuf, unsigned int column_size)
{
	fputs("  ", stdout);
	size_t len = strlen(statbuf);
	while (len < column_size) {
		fputc(' ', stdout);
		column_size--;
	}
	if (*statbuf == '0') {
		color_start(ANSI_GRAY);
		fputc('0', stdout);
	} else {
		for (; *statbuf; statbuf++) {
			if (*statbuf == 'K') color_start(ANSI_GREEN);
			else if (*statbuf == 'M') color_start(ANSI_YELLOW);
			else if (*statbuf == 'G') color_start(ANSI_RED);
			else if (*statbuf == 'T') color_start(ANSI_BOLD_BLUE);
			else if (*statbuf == 'P') color_start(ANSI_MAGENTA);
			else if (*statbuf == 'E') color_start(ANSI_CYAN);
			fputc(*statbuf, stdout);
			if (--column_size <= 0)
				break;
		}
	}
	color_end();
}

/*
 * Display a single statistic.
 */
static void
print_one_stat(uint64_t value, enum zfs_nicenum_format format,
    unsigned int column_size, boolean_t scripted)
{
	char buf[64];

	zfs_nicenum_format(value, buf, sizeof (buf), format);

	if (scripted)
		printf("\t%s", buf);
	else
		print_stat_color(buf, column_size);
}

/*
 * Calculate the default vdev stats
 *
 * Subtract oldvs from newvs, apply a scaling factor, and save the resulting
 * stats into calcvs.
 */
static void
calc_default_iostats(vdev_stat_t *oldvs, vdev_stat_t *newvs,
    vdev_stat_t *calcvs)
{
	int i;

	memcpy(calcvs, newvs, sizeof (*calcvs));
	for (i = 0; i < ARRAY_SIZE(calcvs->vs_ops); i++)
		calcvs->vs_ops[i] = (newvs->vs_ops[i] - oldvs->vs_ops[i]);

	for (i = 0; i < ARRAY_SIZE(calcvs->vs_bytes); i++)
		calcvs->vs_bytes[i] = (newvs->vs_bytes[i] - oldvs->vs_bytes[i]);
}

/*
 * Internal representation of the extended iostats data.
 *
 * The extended iostat stats are exported in nvlists as either uint64_t arrays
 * or single uint64_t's.  We make both look like arrays to make them easier
 * to process.  In order to make single uint64_t's look like arrays, we set
 * __data to the stat data, and then set *data = &__data with count = 1.  Then,
 * we can just use *data and count.
 */
struct stat_array {
	uint64_t *data;
	uint_t count;	/* Number of entries in data[] */
	uint64_t __data; /* Only used when data is a single uint64_t */
};

static uint64_t
stat_histo_max(struct stat_array *nva, unsigned int len)
{
	uint64_t max = 0;
	int i;
	for (i = 0; i < len; i++)
		max = MAX(max, array64_max(nva[i].data, nva[i].count));

	return (max);
}

/*
 * Helper function to lookup a uint64_t array or uint64_t value and store its
 * data as a stat_array.  If the nvpair is a single uint64_t value, then we make
 * it look like a one element array to make it easier to process.
 */
static int
nvpair64_to_stat_array(nvlist_t *nvl, const char *name,
    struct stat_array *nva)
{
	nvpair_t *tmp;
	int ret;

	verify(nvlist_lookup_nvpair(nvl, name, &tmp) == 0);
	switch (nvpair_type(tmp)) {
	case DATA_TYPE_UINT64_ARRAY:
		ret = nvpair_value_uint64_array(tmp, &nva->data, &nva->count);
		break;
	case DATA_TYPE_UINT64:
		ret = nvpair_value_uint64(tmp, &nva->__data);
		nva->data = &nva->__data;
		nva->count = 1;
		break;
	default:
		/* Not a uint64_t */
		ret = EINVAL;
		break;
	}

	return (ret);
}

/*
 * Given a list of nvlist names, look up the extended stats in newnv and oldnv,
 * subtract them, and return the results in a newly allocated stat_array.
 * You must free the returned array after you are done with it with
 * free_calc_stats().
 *
 * Additionally, you can set "oldnv" to NULL if you simply want the newnv
 * values.
 */
static struct stat_array *
calc_and_alloc_stats_ex(const char **names, unsigned int len, nvlist_t *oldnv,
    nvlist_t *newnv)
{
	nvlist_t *oldnvx = NULL, *newnvx;
	struct stat_array *oldnva, *newnva, *calcnva;
	int i, j;
	unsigned int alloc_size = (sizeof (struct stat_array)) * len;

	/* Extract our extended stats nvlist from the main list */
	verify(nvlist_lookup_nvlist(newnv, ZPOOL_CONFIG_VDEV_STATS_EX,
	    &newnvx) == 0);
	if (oldnv) {
		verify(nvlist_lookup_nvlist(oldnv, ZPOOL_CONFIG_VDEV_STATS_EX,
		    &oldnvx) == 0);
	}

	newnva = safe_malloc(alloc_size);
	oldnva = safe_malloc(alloc_size);
	calcnva = safe_malloc(alloc_size);

	for (j = 0; j < len; j++) {
		verify(nvpair64_to_stat_array(newnvx, names[j],
		    &newnva[j]) == 0);
		calcnva[j].count = newnva[j].count;
		alloc_size = calcnva[j].count * sizeof (calcnva[j].data[0]);
		calcnva[j].data = safe_malloc(alloc_size);
		memcpy(calcnva[j].data, newnva[j].data, alloc_size);

		if (oldnvx) {
			verify(nvpair64_to_stat_array(oldnvx, names[j],
			    &oldnva[j]) == 0);
			for (i = 0; i < oldnva[j].count; i++)
				calcnva[j].data[i] -= oldnva[j].data[i];
		}
	}
	free(newnva);
	free(oldnva);
	return (calcnva);
}

static void
free_calc_stats(struct stat_array *nva, unsigned int len)
{
	int i;
	for (i = 0; i < len; i++)
		free(nva[i].data);

	free(nva);
}

static void
print_iostat_histo(struct stat_array *nva, unsigned int len,
    iostat_cbdata_t *cb, unsigned int column_width, unsigned int namewidth,
    double scale)
{
	int i, j;
	char buf[6];
	uint64_t val;
	enum zfs_nicenum_format format;
	unsigned int buckets;
	unsigned int start_bucket;

	if (cb->cb_literal)
		format = ZFS_NICENUM_RAW;
	else
		format = ZFS_NICENUM_1024;

	/* All these histos are the same size, so just use nva[0].count */
	buckets = nva[0].count;

	if (cb->cb_flags & IOS_RQ_HISTO_M) {
		/* Start at 512 - req size should never be lower than this */
		start_bucket = 9;
	} else {
		start_bucket = 0;
	}

	for (j = start_bucket; j < buckets; j++) {
		/* Print histogram bucket label */
		if (cb->cb_flags & IOS_L_HISTO_M) {
			/* Ending range of this bucket */
			val = (1UL << (j + 1)) - 1;
			zfs_nicetime(val, buf, sizeof (buf));
		} else {
			/* Request size (starting range of bucket) */
			val = (1UL << j);
			zfs_nicenum(val, buf, sizeof (buf));
		}

		if (cb->cb_scripted)
			printf("%llu", (u_longlong_t)val);
		else
			printf("%-*s", namewidth, buf);

		/* Print the values on the line */
		for (i = 0; i < len; i++) {
			print_one_stat(nva[i].data[j] * scale, format,
			    column_width, cb->cb_scripted);
		}
		printf("\n");
	}
}

static void
print_solid_separator(unsigned int length)
{
	while (length--)
		printf("-");
	printf("\n");
}

static void
print_iostat_histos(iostat_cbdata_t *cb, nvlist_t *oldnv,
    nvlist_t *newnv, double scale, const char *name)
{
	unsigned int column_width;
	unsigned int namewidth;
	unsigned int entire_width;
	enum iostat_type type;
	struct stat_array *nva;
	const char **names;
	unsigned int names_len;

	/* What type of histo are we? */
	type = IOS_HISTO_IDX(cb->cb_flags);

	/* Get NULL-terminated array of nvlist names for our histo */
	names = vsx_type_to_nvlist[type];
	names_len = str_array_len(names); /* num of names */

	nva = calc_and_alloc_stats_ex(names, names_len, oldnv, newnv);

	if (cb->cb_literal) {
		column_width = MAX(5,
		    (unsigned int) log10(stat_histo_max(nva, names_len)) + 1);
	} else {
		column_width = 5;
	}

	namewidth = MAX(cb->cb_namewidth,
	    strlen(histo_to_title[IOS_HISTO_IDX(cb->cb_flags)]));

	/*
	 * Calculate the entire line width of what we're printing.  The
	 * +2 is for the two spaces between columns:
	 */
	/*	 read  write				*/
	/*	-----  -----				*/
	/*	|___|  <---------- column_width		*/
	/*						*/
	/*	|__________|  <--- entire_width		*/
	/*						*/
	entire_width = namewidth + (column_width + 2) *
	    label_array_len(iostat_bottom_labels[type]);

	if (cb->cb_scripted)
		printf("%s\n", name);
	else
		print_iostat_header_impl(cb, column_width, name);

	print_iostat_histo(nva, names_len, cb, column_width,
	    namewidth, scale);

	free_calc_stats(nva, names_len);
	if (!cb->cb_scripted)
		print_solid_separator(entire_width);
}

/*
 * Calculate the average latency of a power-of-two latency histogram
 */
static uint64_t
single_histo_average(uint64_t *histo, unsigned int buckets)
{
	int i;
	uint64_t count = 0, total = 0;

	for (i = 0; i < buckets; i++) {
		/*
		 * Our buckets are power-of-two latency ranges.  Use the
		 * midpoint latency of each bucket to calculate the average.
		 * For example:
		 *
		 * Bucket          Midpoint
		 * 8ns-15ns:       12ns
		 * 16ns-31ns:      24ns
		 * ...
		 */
		if (histo[i] != 0) {
			total += histo[i] * (((1UL << i) + ((1UL << i)/2)));
			count += histo[i];
		}
	}

	/* Prevent divide by zero */
	return (count == 0 ? 0 : total / count);
}

static void
print_iostat_queues(iostat_cbdata_t *cb, nvlist_t *newnv)
{
	const char *names[] = {
		ZPOOL_CONFIG_VDEV_SYNC_R_PEND_QUEUE,
		ZPOOL_CONFIG_VDEV_SYNC_R_ACTIVE_QUEUE,
		ZPOOL_CONFIG_VDEV_SYNC_W_PEND_QUEUE,
		ZPOOL_CONFIG_VDEV_SYNC_W_ACTIVE_QUEUE,
		ZPOOL_CONFIG_VDEV_ASYNC_R_PEND_QUEUE,
		ZPOOL_CONFIG_VDEV_ASYNC_R_ACTIVE_QUEUE,
		ZPOOL_CONFIG_VDEV_ASYNC_W_PEND_QUEUE,
		ZPOOL_CONFIG_VDEV_ASYNC_W_ACTIVE_QUEUE,
		ZPOOL_CONFIG_VDEV_SCRUB_PEND_QUEUE,
		ZPOOL_CONFIG_VDEV_SCRUB_ACTIVE_QUEUE,
		ZPOOL_CONFIG_VDEV_TRIM_PEND_QUEUE,
		ZPOOL_CONFIG_VDEV_TRIM_ACTIVE_QUEUE,
		ZPOOL_CONFIG_VDEV_REBUILD_PEND_QUEUE,
		ZPOOL_CONFIG_VDEV_REBUILD_ACTIVE_QUEUE,
	};

	struct stat_array *nva;

	unsigned int column_width = default_column_width(cb, IOS_QUEUES);
	enum zfs_nicenum_format format;

	nva = calc_and_alloc_stats_ex(names, ARRAY_SIZE(names), NULL, newnv);

	if (cb->cb_literal)
		format = ZFS_NICENUM_RAW;
	else
		format = ZFS_NICENUM_1024;

	for (int i = 0; i < ARRAY_SIZE(names); i++) {
		uint64_t val = nva[i].data[0];
		print_one_stat(val, format, column_width, cb->cb_scripted);
	}

	free_calc_stats(nva, ARRAY_SIZE(names));
}

static void
print_iostat_latency(iostat_cbdata_t *cb, nvlist_t *oldnv,
    nvlist_t *newnv)
{
	int i;
	uint64_t val;
	const char *names[] = {
		ZPOOL_CONFIG_VDEV_TOT_R_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_TOT_W_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_DISK_R_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_DISK_W_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_SYNC_R_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_SYNC_W_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_ASYNC_R_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_ASYNC_W_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_SCRUB_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_TRIM_LAT_HISTO,
		ZPOOL_CONFIG_VDEV_REBUILD_LAT_HISTO,
	};
	struct stat_array *nva;

	unsigned int column_width = default_column_width(cb, IOS_LATENCY);
	enum zfs_nicenum_format format;

	nva = calc_and_alloc_stats_ex(names, ARRAY_SIZE(names), oldnv, newnv);

	if (cb->cb_literal)
		format = ZFS_NICENUM_RAWTIME;
	else
		format = ZFS_NICENUM_TIME;

	/* Print our avg latencies on the line */
	for (i = 0; i < ARRAY_SIZE(names); i++) {
		/* Compute average latency for a latency histo */
		val = single_histo_average(nva[i].data, nva[i].count);
		print_one_stat(val, format, column_width, cb->cb_scripted);
	}
	free_calc_stats(nva, ARRAY_SIZE(names));
}

/*
 * Print default statistics (capacity/operations/bandwidth)
 */
static void
print_iostat_default(vdev_stat_t *vs, iostat_cbdata_t *cb, double scale)
{
	unsigned int column_width = default_column_width(cb, IOS_DEFAULT);
	enum zfs_nicenum_format format;
	char na;	/* char to print for "not applicable" values */

	if (cb->cb_literal) {
		format = ZFS_NICENUM_RAW;
		na = '0';
	} else {
		format = ZFS_NICENUM_1024;
		na = '-';
	}

	/* only toplevel vdevs have capacity stats */
	if (vs->vs_space == 0) {
		if (cb->cb_scripted)
			printf("\t%c\t%c", na, na);
		else
			printf("  %*c  %*c", column_width, na, column_width,
			    na);
	} else {
		print_one_stat(vs->vs_alloc, format, column_width,
		    cb->cb_scripted);
		print_one_stat(vs->vs_space - vs->vs_alloc, format,
		    column_width, cb->cb_scripted);
	}

	print_one_stat((uint64_t)(vs->vs_ops[ZIO_TYPE_READ] * scale),
	    format, column_width, cb->cb_scripted);
	print_one_stat((uint64_t)(vs->vs_ops[ZIO_TYPE_WRITE] * scale),
	    format, column_width, cb->cb_scripted);
	print_one_stat((uint64_t)(vs->vs_bytes[ZIO_TYPE_READ] * scale),
	    format, column_width, cb->cb_scripted);
	print_one_stat((uint64_t)(vs->vs_bytes[ZIO_TYPE_WRITE] * scale),
	    format, column_width, cb->cb_scripted);
}

static const char *const class_name[] = {
	VDEV_ALLOC_BIAS_DEDUP,
	VDEV_ALLOC_BIAS_SPECIAL,
	VDEV_ALLOC_CLASS_LOGS
};

/*
 * Print out all the statistics for the given vdev.  This can either be the
 * toplevel configuration, or called recursively.  If 'name' is NULL, then this
 * is a verbose output, and we don't want to display the toplevel pool stats.
 *
 * Returns the number of stat lines printed.
 */
static unsigned int
print_vdev_stats(zpool_handle_t *zhp, const char *name, nvlist_t *oldnv,
    nvlist_t *newnv, iostat_cbdata_t *cb, int depth)
{
	nvlist_t **oldchild, **newchild;
	uint_t c, children, oldchildren;
	vdev_stat_t *oldvs, *newvs, *calcvs;
	vdev_stat_t zerovs = { 0 };
	char *vname;
	int i;
	int ret = 0;
	uint64_t tdelta;
	double scale;

	if (strcmp(name, VDEV_TYPE_INDIRECT) == 0)
		return (ret);

	calcvs = safe_malloc(sizeof (*calcvs));

	if (oldnv != NULL) {
		verify(nvlist_lookup_uint64_array(oldnv,
		    ZPOOL_CONFIG_VDEV_STATS, (uint64_t **)&oldvs, &c) == 0);
	} else {
		oldvs = &zerovs;
	}

	/* Do we only want to see a specific vdev? */
	for (i = 0; i < cb->cb_vdevs.cb_names_count; i++) {
		/* Yes we do.  Is this the vdev? */
		if (strcmp(name, cb->cb_vdevs.cb_names[i]) == 0) {
			/*
			 * This is our vdev.  Since it is the only vdev we
			 * will be displaying, make depth = 0 so that it
			 * doesn't get indented.
			 */
			depth = 0;
			break;
		}
	}

	if (cb->cb_vdevs.cb_names_count && (i == cb->cb_vdevs.cb_names_count)) {
		/* Couldn't match the name */
		goto children;
	}


	verify(nvlist_lookup_uint64_array(newnv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&newvs, &c) == 0);

	/*
	 * Print the vdev name unless it's is a histogram.  Histograms
	 * display the vdev name in the header itself.
	 */
	if (!(cb->cb_flags & IOS_ANYHISTO_M)) {
		if (cb->cb_scripted) {
			printf("%s", name);
		} else {
			if (strlen(name) + depth > cb->cb_namewidth)
				(void) printf("%*s%s", depth, "", name);
			else
				(void) printf("%*s%s%*s", depth, "", name,
				    (int)(cb->cb_namewidth - strlen(name) -
				    depth), "");
		}
	}

	/* Calculate our scaling factor */
	tdelta = newvs->vs_timestamp - oldvs->vs_timestamp;
	if ((oldvs->vs_timestamp == 0) && (cb->cb_flags & IOS_ANYHISTO_M)) {
		/*
		 * If we specify printing histograms with no time interval, then
		 * print the histogram numbers over the entire lifetime of the
		 * vdev.
		 */
		scale = 1;
	} else {
		if (tdelta == 0)
			scale = 1.0;
		else
			scale = (double)NANOSEC / tdelta;
	}

	if (cb->cb_flags & IOS_DEFAULT_M) {
		calc_default_iostats(oldvs, newvs, calcvs);
		print_iostat_default(calcvs, cb, scale);
	}
	if (cb->cb_flags & IOS_LATENCY_M)
		print_iostat_latency(cb, oldnv, newnv);
	if (cb->cb_flags & IOS_QUEUES_M)
		print_iostat_queues(cb, newnv);
	if (cb->cb_flags & IOS_ANYHISTO_M) {
		printf("\n");
		print_iostat_histos(cb, oldnv, newnv, scale, name);
	}

	if (cb->vcdl != NULL) {
		const char *path;
		if (nvlist_lookup_string(newnv, ZPOOL_CONFIG_PATH,
		    &path) == 0) {
			printf("  ");
			zpool_print_cmd(cb->vcdl, zpool_get_name(zhp), path);
		}
	}

	if (!(cb->cb_flags & IOS_ANYHISTO_M))
		printf("\n");

	ret++;

children:

	free(calcvs);

	if (!cb->cb_verbose)
		return (ret);

	if (nvlist_lookup_nvlist_array(newnv, ZPOOL_CONFIG_CHILDREN,
	    &newchild, &children) != 0)
		return (ret);

	if (oldnv) {
		if (nvlist_lookup_nvlist_array(oldnv, ZPOOL_CONFIG_CHILDREN,
		    &oldchild, &oldchildren) != 0)
			return (ret);

		children = MIN(oldchildren, children);
	}

	/*
	 * print normal top-level devices
	 */
	for (c = 0; c < children; c++) {
		uint64_t ishole = B_FALSE, islog = B_FALSE;

		(void) nvlist_lookup_uint64(newchild[c], ZPOOL_CONFIG_IS_HOLE,
		    &ishole);

		(void) nvlist_lookup_uint64(newchild[c], ZPOOL_CONFIG_IS_LOG,
		    &islog);

		if (ishole || islog)
			continue;

		if (nvlist_exists(newchild[c], ZPOOL_CONFIG_ALLOCATION_BIAS))
			continue;

		vname = zpool_vdev_name(g_zfs, zhp, newchild[c],
		    cb->cb_vdevs.cb_name_flags | VDEV_NAME_TYPE_ID);
		ret += print_vdev_stats(zhp, vname, oldnv ? oldchild[c] : NULL,
		    newchild[c], cb, depth + 2);
		free(vname);
	}

	/*
	 * print all other top-level devices
	 */
	for (uint_t n = 0; n < ARRAY_SIZE(class_name); n++) {
		boolean_t printed = B_FALSE;

		for (c = 0; c < children; c++) {
			uint64_t islog = B_FALSE;
			const char *bias = NULL;
			const char *type = NULL;

			(void) nvlist_lookup_uint64(newchild[c],
			    ZPOOL_CONFIG_IS_LOG, &islog);
			if (islog) {
				bias = VDEV_ALLOC_CLASS_LOGS;
			} else {
				(void) nvlist_lookup_string(newchild[c],
				    ZPOOL_CONFIG_ALLOCATION_BIAS, &bias);
				(void) nvlist_lookup_string(newchild[c],
				    ZPOOL_CONFIG_TYPE, &type);
			}
			if (bias == NULL || strcmp(bias, class_name[n]) != 0)
				continue;
			if (!islog && strcmp(type, VDEV_TYPE_INDIRECT) == 0)
				continue;

			if (!printed) {
				if ((!(cb->cb_flags & IOS_ANYHISTO_M)) &&
				    !cb->cb_scripted &&
				    !cb->cb_vdevs.cb_names) {
					print_iostat_dashes(cb, 0,
					    class_name[n]);
				}
				printf("\n");
				printed = B_TRUE;
			}

			vname = zpool_vdev_name(g_zfs, zhp, newchild[c],
			    cb->cb_vdevs.cb_name_flags | VDEV_NAME_TYPE_ID);
			ret += print_vdev_stats(zhp, vname, oldnv ?
			    oldchild[c] : NULL, newchild[c], cb, depth + 2);
			free(vname);
		}
	}

	/*
	 * Include level 2 ARC devices in iostat output
	 */
	if (nvlist_lookup_nvlist_array(newnv, ZPOOL_CONFIG_L2CACHE,
	    &newchild, &children) != 0)
		return (ret);

	if (oldnv) {
		if (nvlist_lookup_nvlist_array(oldnv, ZPOOL_CONFIG_L2CACHE,
		    &oldchild, &oldchildren) != 0)
			return (ret);

		children = MIN(oldchildren, children);
	}

	if (children > 0) {
		if ((!(cb->cb_flags & IOS_ANYHISTO_M)) && !cb->cb_scripted &&
		    !cb->cb_vdevs.cb_names) {
			print_iostat_dashes(cb, 0, "cache");
		}
		printf("\n");

		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, zhp, newchild[c],
			    cb->cb_vdevs.cb_name_flags);
			ret += print_vdev_stats(zhp, vname, oldnv ? oldchild[c]
			    : NULL, newchild[c], cb, depth + 2);
			free(vname);
		}
	}

	return (ret);
}

static int
refresh_iostat(zpool_handle_t *zhp, void *data)
{
	iostat_cbdata_t *cb = data;
	boolean_t missing;

	/*
	 * If the pool has disappeared, remove it from the list and continue.
	 */
	if (zpool_refresh_stats(zhp, &missing) != 0)
		return (-1);

	if (missing)
		pool_list_remove(cb->cb_list, zhp);

	return (0);
}

/*
 * Callback to print out the iostats for the given pool.
 */
static int
print_iostat(zpool_handle_t *zhp, void *data)
{
	iostat_cbdata_t *cb = data;
	nvlist_t *oldconfig, *newconfig;
	nvlist_t *oldnvroot, *newnvroot;
	int ret;

	newconfig = zpool_get_config(zhp, &oldconfig);

	if (cb->cb_iteration == 1)
		oldconfig = NULL;

	verify(nvlist_lookup_nvlist(newconfig, ZPOOL_CONFIG_VDEV_TREE,
	    &newnvroot) == 0);

	if (oldconfig == NULL)
		oldnvroot = NULL;
	else
		verify(nvlist_lookup_nvlist(oldconfig, ZPOOL_CONFIG_VDEV_TREE,
		    &oldnvroot) == 0);

	ret = print_vdev_stats(zhp, zpool_get_name(zhp), oldnvroot, newnvroot,
	    cb, 0);
	if ((ret != 0) && !(cb->cb_flags & IOS_ANYHISTO_M) &&
	    !cb->cb_scripted && cb->cb_verbose &&
	    !cb->cb_vdevs.cb_names_count) {
		print_iostat_separator(cb);
		if (cb->vcdl != NULL) {
			print_cmd_columns(cb->vcdl, 1);
		}
		printf("\n");
	}

	return (ret);
}

static int
get_columns(void)
{
	struct winsize ws;
	int columns = 80;
	int error;

	if (isatty(STDOUT_FILENO)) {
		error = ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
		if (error == 0)
			columns = ws.ws_col;
	} else {
		columns = 999;
	}

	return (columns);
}

/*
 * Return stat flags that are supported by all pools by both the module and
 * zpool iostat.  "*data" should be initialized to all 0xFFs before running.
 * It will get ANDed down until only the flags that are supported on all pools
 * remain.
 */
static int
get_stat_flags_cb(zpool_handle_t *zhp, void *data)
{
	uint64_t *mask = data;
	nvlist_t *config, *nvroot, *nvx;
	uint64_t flags = 0;
	int i, j;

	config = zpool_get_config(zhp, NULL);
	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);

	/* Default stats are always supported, but for completeness.. */
	if (nvlist_exists(nvroot, ZPOOL_CONFIG_VDEV_STATS))
		flags |= IOS_DEFAULT_M;

	/* Get our extended stats nvlist from the main list */
	if (nvlist_lookup_nvlist(nvroot, ZPOOL_CONFIG_VDEV_STATS_EX,
	    &nvx) != 0) {
		/*
		 * No extended stats; they're probably running an older
		 * module.  No big deal, we support that too.
		 */
		goto end;
	}

	/* For each extended stat, make sure all its nvpairs are supported */
	for (j = 0; j < ARRAY_SIZE(vsx_type_to_nvlist); j++) {
		if (!vsx_type_to_nvlist[j][0])
			continue;

		/* Start off by assuming the flag is supported, then check */
		flags |= (1ULL << j);
		for (i = 0; vsx_type_to_nvlist[j][i]; i++) {
			if (!nvlist_exists(nvx, vsx_type_to_nvlist[j][i])) {
				/* flag isn't supported */
				flags = flags & ~(1ULL  << j);
				break;
			}
		}
	}
end:
	*mask = *mask & flags;
	return (0);
}

/*
 * Return a bitmask of stats that are supported on all pools by both the module
 * and zpool iostat.
 */
static uint64_t
get_stat_flags(zpool_list_t *list)
{
	uint64_t mask = -1;

	/*
	 * get_stat_flags_cb() will lop off bits from "mask" until only the
	 * flags that are supported on all pools remain.
	 */
	pool_list_iter(list, B_FALSE, get_stat_flags_cb, &mask);
	return (mask);
}

/*
 * Same as get_interval_count(), but with additional checks to not misinterpret
 * guids as interval/count values.  Assumes VDEV_NAME_GUID is set in
 * cb.cb_vdevs.cb_name_flags.
 */
static void
get_interval_count_filter_guids(int *argc, char **argv, float *interval,
    unsigned long *count, iostat_cbdata_t *cb)
{
	char **tmpargv = argv;
	int argc_for_interval = 0;

	/* Is the last arg an interval value?  Or a guid? */
	if (*argc >= 1 && !are_vdevs_in_pool(1, &argv[*argc - 1], NULL,
	    &cb->cb_vdevs)) {
		/*
		 * The last arg is not a guid, so it's probably an
		 * interval value.
		 */
		argc_for_interval++;

		if (*argc >= 2 &&
		    !are_vdevs_in_pool(1, &argv[*argc - 2], NULL,
		    &cb->cb_vdevs)) {
			/*
			 * The 2nd to last arg is not a guid, so it's probably
			 * an interval value.
			 */
			argc_for_interval++;
		}
	}

	/* Point to our list of possible intervals */
	tmpargv = &argv[*argc - argc_for_interval];

	*argc = *argc - argc_for_interval;
	get_interval_count(&argc_for_interval, tmpargv,
	    interval, count);
}

/*
 * Set the minimum pool/vdev name column width.  The width must be at least 10,
 * but may be as large as the column width - 42 so it still fits on one line.
 * NOTE: 42 is the width of the default capacity/operations/bandwidth output
 */
static int
get_namewidth_iostat(zpool_handle_t *zhp, void *data)
{
	iostat_cbdata_t *cb = data;
	int width, available_width;

	/*
	 * get_namewidth() returns the maximum width of any name in that column
	 * for any pool/vdev/device line that will be output.
	 */
	width = get_namewidth(zhp, cb->cb_namewidth,
	    cb->cb_vdevs.cb_name_flags | VDEV_NAME_TYPE_ID, cb->cb_verbose);

	/*
	 * The width we are calculating is the width of the header and also the
	 * padding width for names that are less than maximum width.  The stats
	 * take up 42 characters, so the width available for names is:
	 */
	available_width = get_columns() - 42;

	/*
	 * If the maximum width fits on a screen, then great!  Make everything
	 * line up by justifying all lines to the same width.  If that max
	 * width is larger than what's available, the name plus stats won't fit
	 * on one line, and justifying to that width would cause every line to
	 * wrap on the screen.  We only want lines with long names to wrap.
	 * Limit the padding to what won't wrap.
	 */
	if (width > available_width)
		width = available_width;

	/*
	 * And regardless of whatever the screen width is (get_columns can
	 * return 0 if the width is not known or less than 42 for a narrow
	 * terminal) have the width be a minimum of 10.
	 */
	if (width < 10)
		width = 10;

	/* Save the calculated width */
	cb->cb_namewidth = width;

	return (0);
}

/*
 * zpool iostat [[-c [script1,script2,...]] [-lq]|[-rw]] [-ghHLpPvy] [-n name]
 *              [-T d|u] [[ pool ...]|[pool vdev ...]|[vdev ...]]
 *              [interval [count]]
 *
 *	-c CMD  For each vdev, run command CMD
 *	-g	Display guid for individual vdev name.
 *	-L	Follow links when resolving vdev path name.
 *	-P	Display full path for vdev name.
 *	-v	Display statistics for individual vdevs
 *	-h	Display help
 *	-p	Display values in parsable (exact) format.
 *	-H	Scripted mode.  Don't display headers, and separate properties
 *		by a single tab.
 *	-l	Display average latency
 *	-q	Display queue depths
 *	-w	Display latency histograms
 *	-r	Display request size histogram
 *	-T	Display a timestamp in date(1) or Unix format
 *	-n	Only print headers once
 *
 * This command can be tricky because we want to be able to deal with pool
 * creation/destruction as well as vdev configuration changes.  The bulk of this
 * processing is handled by the pool_list_* routines in zpool_iter.c.  We rely
 * on pool_list_update() to detect the addition of new pools.  Configuration
 * changes are all handled within libzfs.
 */
int
zpool_do_iostat(int argc, char **argv)
{
	int c;
	int ret;
	int npools;
	float interval = 0;
	unsigned long count = 0;
	int winheight = 24;
	zpool_list_t *list;
	boolean_t verbose = B_FALSE;
	boolean_t latency = B_FALSE, l_histo = B_FALSE, rq_histo = B_FALSE;
	boolean_t queues = B_FALSE, parsable = B_FALSE, scripted = B_FALSE;
	boolean_t omit_since_boot = B_FALSE;
	boolean_t guid = B_FALSE;
	boolean_t follow_links = B_FALSE;
	boolean_t full_name = B_FALSE;
	boolean_t headers_once = B_FALSE;
	iostat_cbdata_t cb = { 0 };
	char *cmd = NULL;

	/* Used for printing error message */
	const char flag_to_arg[] = {[IOS_LATENCY] = 'l', [IOS_QUEUES] = 'q',
	    [IOS_L_HISTO] = 'w', [IOS_RQ_HISTO] = 'r'};

	uint64_t unsupported_flags;

	/* check options */
	while ((c = getopt(argc, argv, "c:gLPT:vyhplqrwnH")) != -1) {
		switch (c) {
		case 'c':
			if (cmd != NULL) {
				fprintf(stderr,
				    gettext("Can't set -c flag twice\n"));
				exit(1);
			}

			if (getenv("ZPOOL_SCRIPTS_ENABLED") != NULL &&
			    !libzfs_envvar_is_set("ZPOOL_SCRIPTS_ENABLED")) {
				fprintf(stderr, gettext(
				    "Can't run -c, disabled by "
				    "ZPOOL_SCRIPTS_ENABLED.\n"));
				exit(1);
			}

			if ((getuid() <= 0 || geteuid() <= 0) &&
			    !libzfs_envvar_is_set("ZPOOL_SCRIPTS_AS_ROOT")) {
				fprintf(stderr, gettext(
				    "Can't run -c with root privileges "
				    "unless ZPOOL_SCRIPTS_AS_ROOT is set.\n"));
				exit(1);
			}
			cmd = optarg;
			verbose = B_TRUE;
			break;
		case 'g':
			guid = B_TRUE;
			break;
		case 'L':
			follow_links = B_TRUE;
			break;
		case 'P':
			full_name = B_TRUE;
			break;
		case 'T':
			get_timestamp_arg(*optarg);
			break;
		case 'v':
			verbose = B_TRUE;
			break;
		case 'p':
			parsable = B_TRUE;
			break;
		case 'l':
			latency = B_TRUE;
			break;
		case 'q':
			queues = B_TRUE;
			break;
		case 'H':
			scripted = B_TRUE;
			break;
		case 'w':
			l_histo = B_TRUE;
			break;
		case 'r':
			rq_histo = B_TRUE;
			break;
		case 'y':
			omit_since_boot = B_TRUE;
			break;
		case 'n':
			headers_once = B_TRUE;
			break;
		case 'h':
			zpool_usage(B_FALSE);
			break;
		case '?':
			if (optopt == 'c') {
				print_zpool_script_list("iostat");
				exit(0);
			} else {
				fprintf(stderr,
				    gettext("invalid option '%c'\n"), optopt);
			}
			zpool_usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	cb.cb_literal = parsable;
	cb.cb_scripted = scripted;

	if (guid)
		cb.cb_vdevs.cb_name_flags |= VDEV_NAME_GUID;
	if (follow_links)
		cb.cb_vdevs.cb_name_flags |= VDEV_NAME_FOLLOW_LINKS;
	if (full_name)
		cb.cb_vdevs.cb_name_flags |= VDEV_NAME_PATH;
	cb.cb_iteration = 0;
	cb.cb_namewidth = 0;
	cb.cb_verbose = verbose;

	/* Get our interval and count values (if any) */
	if (guid) {
		get_interval_count_filter_guids(&argc, argv, &interval,
		    &count, &cb);
	} else {
		get_interval_count(&argc, argv, &interval, &count);
	}

	if (argc == 0) {
		/* No args, so just print the defaults. */
	} else if (are_all_pools(argc, argv)) {
		/* All the args are pool names */
	} else if (are_vdevs_in_pool(argc, argv, NULL, &cb.cb_vdevs)) {
		/* All the args are vdevs */
		cb.cb_vdevs.cb_names = argv;
		cb.cb_vdevs.cb_names_count = argc;
		argc = 0; /* No pools to process */
	} else if (are_all_pools(1, argv)) {
		/* The first arg is a pool name */
		if (are_vdevs_in_pool(argc - 1, argv + 1, argv[0],
		    &cb.cb_vdevs)) {
			/* ...and the rest are vdev names */
			cb.cb_vdevs.cb_names = argv + 1;
			cb.cb_vdevs.cb_names_count = argc - 1;
			argc = 1; /* One pool to process */
		} else {
			fprintf(stderr, gettext("Expected either a list of "));
			fprintf(stderr, gettext("pools, or list of vdevs in"));
			fprintf(stderr, " \"%s\", ", argv[0]);
			fprintf(stderr, gettext("but got:\n"));
			error_list_unresolved_vdevs(argc - 1, argv + 1,
			    argv[0], &cb.cb_vdevs);
			fprintf(stderr, "\n");
			zpool_usage(B_FALSE);
			return (1);
		}
	} else {
		/*
		 * The args don't make sense. The first arg isn't a pool name,
		 * nor are all the args vdevs.
		 */
		fprintf(stderr, gettext("Unable to parse pools/vdevs list.\n"));
		fprintf(stderr, "\n");
		return (1);
	}

	if (cb.cb_vdevs.cb_names_count != 0) {
		/*
		 * If user specified vdevs, it implies verbose.
		 */
		cb.cb_verbose = B_TRUE;
	}

	/*
	 * Construct the list of all interesting pools.
	 */
	ret = 0;
	if ((list = pool_list_get(argc, argv, NULL, ZFS_TYPE_POOL, parsable,
	    &ret)) == NULL)
		return (1);

	if (pool_list_count(list) == 0 && argc != 0) {
		pool_list_free(list);
		return (1);
	}

	if (pool_list_count(list) == 0 && interval == 0) {
		pool_list_free(list);
		(void) fprintf(stderr, gettext("no pools available\n"));
		return (1);
	}

	if ((l_histo || rq_histo) && (cmd != NULL || latency || queues)) {
		pool_list_free(list);
		(void) fprintf(stderr,
		    gettext("[-r|-w] isn't allowed with [-c|-l|-q]\n"));
		zpool_usage(B_FALSE);
		return (1);
	}

	if (l_histo && rq_histo) {
		pool_list_free(list);
		(void) fprintf(stderr,
		    gettext("Only one of [-r|-w] can be passed at a time\n"));
		zpool_usage(B_FALSE);
		return (1);
	}

	/*
	 * Enter the main iostat loop.
	 */
	cb.cb_list = list;

	if (l_histo) {
		/*
		 * Histograms tables look out of place when you try to display
		 * them with the other stats, so make a rule that you can only
		 * print histograms by themselves.
		 */
		cb.cb_flags = IOS_L_HISTO_M;
	} else if (rq_histo) {
		cb.cb_flags = IOS_RQ_HISTO_M;
	} else {
		cb.cb_flags = IOS_DEFAULT_M;
		if (latency)
			cb.cb_flags |= IOS_LATENCY_M;
		if (queues)
			cb.cb_flags |= IOS_QUEUES_M;
	}

	/*
	 * See if the module supports all the stats we want to display.
	 */
	unsupported_flags = cb.cb_flags & ~get_stat_flags(list);
	if (unsupported_flags) {
		uint64_t f;
		int idx;
		fprintf(stderr,
		    gettext("The loaded zfs module doesn't support:"));

		/* for each bit set in unsupported_flags */
		for (f = unsupported_flags; f; f &= ~(1ULL << idx)) {
			idx = lowbit64(f) - 1;
			fprintf(stderr, " -%c", flag_to_arg[idx]);
		}

		fprintf(stderr, ".  Try running a newer module.\n");
		pool_list_free(list);

		return (1);
	}

	for (;;) {
		if ((npools = pool_list_count(list)) == 0)
			(void) fprintf(stderr, gettext("no pools available\n"));
		else {
			/*
			 * If this is the first iteration and -y was supplied
			 * we skip any printing.
			 */
			boolean_t skip = (omit_since_boot &&
			    cb.cb_iteration == 0);

			/*
			 * Refresh all statistics.  This is done as an
			 * explicit step before calculating the maximum name
			 * width, so that any * configuration changes are
			 * properly accounted for.
			 */
			(void) pool_list_iter(list, B_FALSE, refresh_iostat,
			    &cb);

			/*
			 * Iterate over all pools to determine the maximum width
			 * for the pool / device name column across all pools.
			 */
			cb.cb_namewidth = 0;
			(void) pool_list_iter(list, B_FALSE,
			    get_namewidth_iostat, &cb);

			if (timestamp_fmt != NODATE)
				print_timestamp(timestamp_fmt);

			if (cmd != NULL && cb.cb_verbose &&
			    !(cb.cb_flags & IOS_ANYHISTO_M)) {
				cb.vcdl = all_pools_for_each_vdev_run(argc,
				    argv, cmd, g_zfs, cb.cb_vdevs.cb_names,
				    cb.cb_vdevs.cb_names_count,
				    cb.cb_vdevs.cb_name_flags);
			} else {
				cb.vcdl = NULL;
			}


			/*
			 * Check terminal size so we can print headers
			 * even when terminal window has its height
			 * changed.
			 */
			winheight = terminal_height();
			/*
			 * Are we connected to TTY? If not, headers_once
			 * should be true, to avoid breaking scripts.
			 */
			if (winheight < 0)
				headers_once = B_TRUE;

			/*
			 * If it's the first time and we're not skipping it,
			 * or either skip or verbose mode, print the header.
			 *
			 * The histogram code explicitly prints its header on
			 * every vdev, so skip this for histograms.
			 */
			if (((++cb.cb_iteration == 1 && !skip) ||
			    (skip != verbose) ||
			    (!headers_once &&
			    (cb.cb_iteration % winheight) == 0)) &&
			    (!(cb.cb_flags & IOS_ANYHISTO_M)) &&
			    !cb.cb_scripted)
				print_iostat_header(&cb);

			if (skip) {
				(void) fflush(stdout);
				(void) fsleep(interval);
				continue;
			}

			pool_list_iter(list, B_FALSE, print_iostat, &cb);

			/*
			 * If there's more than one pool, and we're not in
			 * verbose mode (which prints a separator for us),
			 * then print a separator.
			 *
			 * In addition, if we're printing specific vdevs then
			 * we also want an ending separator.
			 */
			if (((npools > 1 && !verbose &&
			    !(cb.cb_flags & IOS_ANYHISTO_M)) ||
			    (!(cb.cb_flags & IOS_ANYHISTO_M) &&
			    cb.cb_vdevs.cb_names_count)) &&
			    !cb.cb_scripted) {
				print_iostat_separator(&cb);
				if (cb.vcdl != NULL)
					print_cmd_columns(cb.vcdl, 1);
				printf("\n");
			}

			if (cb.vcdl != NULL)
				free_vdev_cmd_data_list(cb.vcdl);

		}

		if (interval == 0)
			break;

		if (count != 0 && --count == 0)
			break;

		(void) fflush(stdout);
		(void) fsleep(interval);
	}

	pool_list_free(list);

	return (ret);
}
