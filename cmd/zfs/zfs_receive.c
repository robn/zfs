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
 * Restore a backup stream from stdin.
 */
int
zfs_do_receive(int argc, char **argv)
{
	int c, err = 0;
	recvflags_t flags = { 0 };
	boolean_t abort_resumable = B_FALSE;
	nvlist_t *props;

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0)
		nomem();

	/* check options */
	while ((c = getopt(argc, argv, ":o:x:dehMnuvFsAc")) != -1) {
		switch (c) {
		case 'o':
			if (!parseprop(props, optarg)) {
				nvlist_free(props);
				usage(B_FALSE);
			}
			break;
		case 'x':
			if (!parsepropname(props, optarg)) {
				nvlist_free(props);
				usage(B_FALSE);
			}
			break;
		case 'd':
			if (flags.istail) {
				(void) fprintf(stderr, gettext("invalid option "
				    "combination: -d and -e are mutually "
				    "exclusive\n"));
				usage(B_FALSE);
			}
			flags.isprefix = B_TRUE;
			break;
		case 'e':
			if (flags.isprefix) {
				(void) fprintf(stderr, gettext("invalid option "
				    "combination: -d and -e are mutually "
				    "exclusive\n"));
				usage(B_FALSE);
			}
			flags.istail = B_TRUE;
			break;
		case 'h':
			flags.skipholds = B_TRUE;
			break;
		case 'M':
			flags.forceunmount = B_TRUE;
			break;
		case 'n':
			flags.dryrun = B_TRUE;
			break;
		case 'u':
			flags.nomount = B_TRUE;
			break;
		case 'v':
			flags.verbose = B_TRUE;
			break;
		case 's':
			flags.resumable = B_TRUE;
			break;
		case 'F':
			flags.force = B_TRUE;
			break;
		case 'A':
			abort_resumable = B_TRUE;
			break;
		case 'c':
			flags.heal = B_TRUE;
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

	/* zfs recv -e (use "tail" name) implies -d (remove dataset "head") */
	if (flags.istail)
		flags.isprefix = B_TRUE;

	/* check number of arguments */
	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing snapshot argument\n"));
		usage(B_FALSE);
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	if (abort_resumable) {
		if (flags.isprefix || flags.istail || flags.dryrun ||
		    flags.resumable || flags.nomount) {
			(void) fprintf(stderr, gettext("invalid option\n"));
			usage(B_FALSE);
		}

		char namebuf[ZFS_MAX_DATASET_NAME_LEN];
		(void) snprintf(namebuf, sizeof (namebuf),
		    "%s/%%recv", argv[0]);

		if (zfs_dataset_exists(g_zfs, namebuf,
		    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME)) {
			zfs_handle_t *zhp = zfs_open(g_zfs,
			    namebuf, ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
			if (zhp == NULL) {
				nvlist_free(props);
				return (1);
			}
			err = zfs_destroy(zhp, B_FALSE);
			zfs_close(zhp);
		} else {
			zfs_handle_t *zhp = zfs_open(g_zfs,
			    argv[0], ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
			if (zhp == NULL)
				usage(B_FALSE);
			if (!zfs_prop_get_int(zhp, ZFS_PROP_INCONSISTENT) ||
			    zfs_prop_get(zhp, ZFS_PROP_RECEIVE_RESUME_TOKEN,
			    NULL, 0, NULL, NULL, 0, B_TRUE) == -1) {
				(void) fprintf(stderr,
				    gettext("'%s' does not have any "
				    "resumable receive state to abort\n"),
				    argv[0]);
				nvlist_free(props);
				zfs_close(zhp);
				return (1);
			}
			err = zfs_destroy(zhp, B_FALSE);
			zfs_close(zhp);
		}
		nvlist_free(props);
		return (err != 0);
	}

	if (isatty(STDIN_FILENO)) {
		(void) fprintf(stderr,
		    gettext("Error: Backup stream can not be read "
		    "from a terminal.\n"
		    "You must redirect standard input.\n"));
		nvlist_free(props);
		return (1);
	}
	err = zfs_receive(g_zfs, argv[0], props, &flags, STDIN_FILENO, NULL);
	nvlist_free(props);

	return (err != 0);
}

