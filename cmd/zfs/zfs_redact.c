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

int
zfs_do_redact(int argc, char **argv)
{
	char *snap = NULL;
	char *bookname = NULL;
	char **rsnaps = NULL;
	int numrsnaps = 0;
	argv++;
	argc--;
	if (argc < 3) {
		(void) fprintf(stderr, gettext("too few arguments\n"));
		usage(B_FALSE);
	}

	snap = argv[0];
	bookname = argv[1];
	rsnaps = argv + 2;
	numrsnaps = argc - 2;

	nvlist_t *rsnapnv = fnvlist_alloc();

	for (int i = 0; i < numrsnaps; i++) {
		fnvlist_add_boolean(rsnapnv, rsnaps[i]);
	}

	int err = lzc_redact(snap, bookname, rsnapnv);
	fnvlist_free(rsnapnv);

	switch (err) {
	case 0:
		break;
	case ENOENT: {
		zfs_handle_t *zhp = zfs_open(g_zfs, snap, ZFS_TYPE_SNAPSHOT);
		if (zhp == NULL) {
			(void) fprintf(stderr, gettext("provided snapshot %s "
			    "does not exist\n"), snap);
		} else {
			zfs_close(zhp);
		}
		for (int i = 0; i < numrsnaps; i++) {
			zhp = zfs_open(g_zfs, rsnaps[i], ZFS_TYPE_SNAPSHOT);
			if (zhp == NULL) {
				(void) fprintf(stderr, gettext("provided "
				    "snapshot %s does not exist\n"), rsnaps[i]);
			} else {
				zfs_close(zhp);
			}
		}
		break;
	}
	case EEXIST:
		(void) fprintf(stderr, gettext("specified redaction bookmark "
		    "(%s) provided already exists\n"), bookname);
		break;
	case ENAMETOOLONG:
		(void) fprintf(stderr, gettext("provided bookmark name cannot "
		    "be used, final name would be too long\n"));
		break;
	case E2BIG:
		(void) fprintf(stderr, gettext("too many redaction snapshots "
		    "specified\n"));
		break;
	case EINVAL:
		if (strchr(bookname, '#') != NULL)
			(void) fprintf(stderr, gettext(
			    "redaction bookmark name must not contain '#'\n"));
		else
			(void) fprintf(stderr, gettext(
			    "redaction snapshot must be descendent of "
			    "snapshot being redacted\n"));
		break;
	case EALREADY:
		(void) fprintf(stderr, gettext("attempted to redact redacted "
		    "dataset or with respect to redacted dataset\n"));
		break;
	case ENOTSUP:
		(void) fprintf(stderr, gettext("redaction bookmarks feature "
		    "not enabled\n"));
		break;
	case EXDEV:
		(void) fprintf(stderr, gettext("potentially invalid redaction "
		    "snapshot; full dataset names required\n"));
		break;
	case ESRCH:
		(void) fprintf(stderr, gettext("attempted to resume redaction "
		    " with a mismatched redaction list\n"));
		break;
	default:
		(void) fprintf(stderr, gettext("internal error: %s\n"),
		    strerror(errno));
	}

	return (err);
}
