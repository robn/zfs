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

typedef struct loadkey_cbdata {
	boolean_t cb_loadkey;
	boolean_t cb_recursive;
	boolean_t cb_noop;
	char *cb_keylocation;
	uint64_t cb_numfailed;
	uint64_t cb_numattempted;
} loadkey_cbdata_t;

static int
load_key_callback(zfs_handle_t *zhp, void *data)
{
	int ret;
	boolean_t is_encroot;
	loadkey_cbdata_t *cb = data;
	uint64_t keystatus = zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS);

	/*
	 * If we are working recursively, we want to skip loading / unloading
	 * keys for non-encryption roots and datasets whose keys are already
	 * in the desired end-state.
	 */
	if (cb->cb_recursive) {
		ret = zfs_crypto_get_encryption_root(zhp, &is_encroot, NULL);
		if (ret != 0)
			return (ret);
		if (!is_encroot)
			return (0);

		if ((cb->cb_loadkey && keystatus == ZFS_KEYSTATUS_AVAILABLE) ||
		    (!cb->cb_loadkey && keystatus == ZFS_KEYSTATUS_UNAVAILABLE))
			return (0);
	}

	cb->cb_numattempted++;

	if (cb->cb_loadkey)
		ret = zfs_crypto_load_key(zhp, cb->cb_noop, cb->cb_keylocation);
	else
		ret = zfs_crypto_unload_key(zhp);

	if (ret != 0) {
		cb->cb_numfailed++;
		return (ret);
	}

	return (0);
}

static int
load_unload_keys(int argc, char **argv, boolean_t loadkey)
{
	int c, ret = 0, flags = 0;
	boolean_t do_all = B_FALSE;
	loadkey_cbdata_t cb = { 0 };

	cb.cb_loadkey = loadkey;

	while ((c = getopt(argc, argv, "anrL:")) != -1) {
		/* noop and alternate keylocations only apply to zfs load-key */
		if (loadkey) {
			switch (c) {
			case 'n':
				cb.cb_noop = B_TRUE;
				continue;
			case 'L':
				cb.cb_keylocation = optarg;
				continue;
			default:
				break;
			}
		}

		switch (c) {
		case 'a':
			do_all = B_TRUE;
			cb.cb_recursive = B_TRUE;
			break;
		case 'r':
			flags |= ZFS_ITER_RECURSE;
			cb.cb_recursive = B_TRUE;
			break;
		default:
			(void) fprintf(stderr,
			    gettext("invalid option '%c'\n"), optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	if (!do_all && argc == 0) {
		(void) fprintf(stderr,
		    gettext("Missing dataset argument or -a option\n"));
		usage(B_FALSE);
	}

	if (do_all && argc != 0) {
		(void) fprintf(stderr,
		    gettext("Cannot specify dataset with -a option\n"));
		usage(B_FALSE);
	}

	if (cb.cb_recursive && cb.cb_keylocation != NULL &&
	    strcmp(cb.cb_keylocation, "prompt") != 0) {
		(void) fprintf(stderr, gettext("alternate keylocation may only "
		    "be 'prompt' with -r or -a\n"));
		usage(B_FALSE);
	}

	ret = zfs_for_each(argc, argv, flags,
	    ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME, NULL, NULL, 0,
	    load_key_callback, &cb);

	if (cb.cb_noop || (cb.cb_recursive && cb.cb_numattempted != 0)) {
		(void) printf(gettext("%llu / %llu key(s) successfully %s\n"),
		    (u_longlong_t)(cb.cb_numattempted - cb.cb_numfailed),
		    (u_longlong_t)cb.cb_numattempted,
		    loadkey ? (cb.cb_noop ? "verified" : "loaded") :
		    "unloaded");
	}

	if (cb.cb_numfailed != 0)
		ret = -1;

	return (ret);
}

int
zfs_do_load_key(int argc, char **argv)
{
	return (load_unload_keys(argc, argv, B_TRUE));
}


int
zfs_do_unload_key(int argc, char **argv)
{
	return (load_unload_keys(argc, argv, B_FALSE));
}
