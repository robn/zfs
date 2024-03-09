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

typedef struct holds_cbdata {
	boolean_t	cb_recursive;
	const char	*cb_snapname;
	nvlist_t	**cb_nvlp;
	size_t		cb_max_namelen;
	size_t		cb_max_taglen;
} holds_cbdata_t;

#define	STRFTIME_FMT_STR "%a %b %e %H:%M %Y"
#define	DATETIME_BUF_LEN (32)
/*
 *
 */
static void
print_holds(boolean_t scripted, int nwidth, int tagwidth, nvlist_t *nvl,
    boolean_t parsable)
{
	int i;
	nvpair_t *nvp = NULL;
	const char *const hdr_cols[] = { "NAME", "TAG", "TIMESTAMP" };
	const char *col;

	if (!scripted) {
		for (i = 0; i < 3; i++) {
			col = gettext(hdr_cols[i]);
			if (i < 2)
				(void) printf("%-*s  ", i ? tagwidth : nwidth,
				    col);
			else
				(void) printf("%s\n", col);
		}
	}

	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		const char *zname = nvpair_name(nvp);
		nvlist_t *nvl2;
		nvpair_t *nvp2 = NULL;
		(void) nvpair_value_nvlist(nvp, &nvl2);
		while ((nvp2 = nvlist_next_nvpair(nvl2, nvp2)) != NULL) {
			char tsbuf[DATETIME_BUF_LEN];
			const char *tagname = nvpair_name(nvp2);
			uint64_t val = 0;
			time_t time;
			struct tm t;

			(void) nvpair_value_uint64(nvp2, &val);
			time = (time_t)val;
			(void) localtime_r(&time, &t);
			(void) strftime(tsbuf, DATETIME_BUF_LEN,
			    gettext(STRFTIME_FMT_STR), &t);

			if (scripted) {
				if (parsable) {
					(void) printf("%s\t%s\t%ld\n", zname,
					    tagname, time);
				} else {
					(void) printf("%s\t%s\t%s\n", zname,
					    tagname, tsbuf);
				}
			} else {
				if (parsable) {
					(void) printf("%-*s  %-*s  %ld\n",
					    nwidth, zname, tagwidth,
					    tagname, time);
				} else {
					(void) printf("%-*s  %-*s  %s\n",
					    nwidth, zname, tagwidth,
					    tagname, tsbuf);
				}
			}
		}
	}
}

/*
 * Generic callback function to list a dataset or snapshot.
 */
static int
holds_callback(zfs_handle_t *zhp, void *data)
{
	holds_cbdata_t *cbp = data;
	nvlist_t *top_nvl = *cbp->cb_nvlp;
	nvlist_t *nvl = NULL;
	nvpair_t *nvp = NULL;
	const char *zname = zfs_get_name(zhp);
	size_t znamelen = strlen(zname);

	if (cbp->cb_recursive) {
		const char *snapname;
		char *delim  = strchr(zname, '@');
		if (delim == NULL)
			return (0);

		snapname = delim + 1;
		if (strcmp(cbp->cb_snapname, snapname))
			return (0);
	}

	if (zfs_get_holds(zhp, &nvl) != 0)
		return (-1);

	if (znamelen > cbp->cb_max_namelen)
		cbp->cb_max_namelen  = znamelen;

	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		const char *tag = nvpair_name(nvp);
		size_t taglen = strlen(tag);
		if (taglen > cbp->cb_max_taglen)
			cbp->cb_max_taglen  = taglen;
	}

	return (nvlist_add_nvlist(top_nvl, zname, nvl));
}

/*
 * zfs holds [-rHp] <snap> ...
 *
 *	-r	Lists holds that are set on the named snapshots recursively.
 *	-H	Scripted mode; elide headers and separate columns by tabs.
 *	-p	Display values in parsable (literal) format.
 */
int
zfs_do_holds(int argc, char **argv)
{
	int c;
	boolean_t errors = B_FALSE;
	boolean_t scripted = B_FALSE;
	boolean_t recursive = B_FALSE;
	boolean_t parsable = B_FALSE;

	int types = ZFS_TYPE_SNAPSHOT;
	holds_cbdata_t cb = { 0 };

	int limit = 0;
	int ret = 0;
	int flags = 0;

	/* check options */
	while ((c = getopt(argc, argv, "rHp")) != -1) {
		switch (c) {
		case 'r':
			recursive = B_TRUE;
			break;
		case 'H':
			scripted = B_TRUE;
			break;
		case 'p':
			parsable = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	if (recursive) {
		types |= ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME;
		flags |= ZFS_ITER_RECURSE;
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1)
		usage(B_FALSE);

	nvlist_t *nvl = fnvlist_alloc();

	for (int i = 0; i < argc; ++i) {
		char *snapshot = argv[i];
		const char *delim;
		const char *snapname;

		delim = strchr(snapshot, '@');
		if (delim == NULL) {
			(void) fprintf(stderr,
			    gettext("'%s' is not a snapshot\n"), snapshot);
			errors = B_TRUE;
			continue;
		}
		snapname = delim + 1;
		if (recursive)
			snapshot[delim - snapshot] = '\0';

		cb.cb_recursive = recursive;
		cb.cb_snapname = snapname;
		cb.cb_nvlp = &nvl;

		/*
		 *  1. collect holds data, set format options
		 */
		ret = zfs_for_each(1, argv + i, flags, types, NULL, NULL, limit,
		    holds_callback, &cb);
		if (ret != 0)
			errors = B_TRUE;
	}

	/*
	 *  2. print holds data
	 */
	print_holds(scripted, cb.cb_max_namelen, cb.cb_max_taglen, nvl,
	    parsable);

	if (nvlist_empty(nvl))
		(void) fprintf(stderr, gettext("no datasets available\n"));

	nvlist_free(nvl);

	return (errors);
}
