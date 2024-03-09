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
 * zfs bookmark <fs@source>|<fs#source> <fs#bookmark>
 *
 * Creates a bookmark with the given name from the source snapshot
 * or creates a copy of an existing source bookmark.
 */
int
zfs_do_bookmark(int argc, char **argv)
{
	char *source, *bookname;
	char expbuf[ZFS_MAX_DATASET_NAME_LEN];
	int source_type;
	nvlist_t *nvl;
	int ret = 0;
	int c;

	/* check options */
	while ((c = getopt(argc, argv, "")) != -1) {
		switch (c) {
		case '?':
			(void) fprintf(stderr,
			    gettext("invalid option '%c'\n"), optopt);
			goto usage;
		}
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing source argument\n"));
		goto usage;
	}
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing bookmark argument\n"));
		goto usage;
	}

	source = argv[0];
	bookname = argv[1];

	if (strchr(source, '@') == NULL && strchr(source, '#') == NULL) {
		(void) fprintf(stderr,
		    gettext("invalid source name '%s': "
		    "must contain a '@' or '#'\n"), source);
		goto usage;
	}
	if (strchr(bookname, '#') == NULL) {
		(void) fprintf(stderr,
		    gettext("invalid bookmark name '%s': "
		    "must contain a '#'\n"), bookname);
		goto usage;
	}

	/*
	 * expand source or bookname to full path:
	 * one of them may be specified as short name
	 */
	{
		char **expand;
		char *source_short, *bookname_short;
		source_short = strpbrk(source, "@#");
		bookname_short = strpbrk(bookname, "#");
		if (source_short == source &&
		    bookname_short == bookname) {
			(void) fprintf(stderr, gettext(
			    "either source or bookmark must be specified as "
			    "full dataset paths"));
			goto usage;
		} else if (source_short != source &&
		    bookname_short != bookname) {
			expand = NULL;
		} else if (source_short != source) {
			strlcpy(expbuf, source, sizeof (expbuf));
			expand = &bookname;
		} else if (bookname_short != bookname) {
			strlcpy(expbuf, bookname, sizeof (expbuf));
			expand = &source;
		} else {
			abort();
		}
		if (expand != NULL) {
			*strpbrk(expbuf, "@#") = '\0'; /* dataset name in buf */
			(void) strlcat(expbuf, *expand, sizeof (expbuf));
			*expand = expbuf;
		}
	}

	/* determine source type */
	switch (*strpbrk(source, "@#")) {
		case '@': source_type = ZFS_TYPE_SNAPSHOT; break;
		case '#': source_type = ZFS_TYPE_BOOKMARK; break;
		default: abort();
	}

	/* test the source exists */
	zfs_handle_t *zhp;
	zhp = zfs_open(g_zfs, source, source_type);
	if (zhp == NULL)
		goto usage;
	zfs_close(zhp);

	nvl = fnvlist_alloc();
	fnvlist_add_string(nvl, bookname, source);
	ret = lzc_bookmark(nvl, NULL);
	fnvlist_free(nvl);

	if (ret != 0) {
		const char *err_msg = NULL;
		char errbuf[1024];

		(void) snprintf(errbuf, sizeof (errbuf),
		    dgettext(TEXT_DOMAIN,
		    "cannot create bookmark '%s'"), bookname);

		switch (ret) {
		case EXDEV:
			err_msg = "bookmark is in a different pool";
			break;
		case ZFS_ERR_BOOKMARK_SOURCE_NOT_ANCESTOR:
			err_msg = "source is not an ancestor of the "
			    "new bookmark's dataset";
			break;
		case EEXIST:
			err_msg = "bookmark exists";
			break;
		case EINVAL:
			err_msg = "invalid argument";
			break;
		case ENOTSUP:
			err_msg = "bookmark feature not enabled";
			break;
		case ENOSPC:
			err_msg = "out of space";
			break;
		case ENOENT:
			err_msg = "dataset does not exist";
			break;
		default:
			(void) zfs_standard_error(g_zfs, ret, errbuf);
			break;
		}
		if (err_msg != NULL) {
			(void) fprintf(stderr, "%s: %s\n", errbuf,
			    dgettext(TEXT_DOMAIN, err_msg));
		}
	}

	return (ret != 0);

usage:
	usage(B_FALSE);
	return (-1);
}
