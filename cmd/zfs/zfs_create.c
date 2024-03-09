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
 * Return a default volblocksize for the pool which always uses more than
 * half of the data sectors.  This primarily applies to dRAID which always
 * writes full stripe widths.
 */
static uint64_t
default_volblocksize(zpool_handle_t *zhp, nvlist_t *props)
{
	uint64_t volblocksize, asize = SPA_MINBLOCKSIZE;
	nvlist_t *tree, **vdevs;
	uint_t nvdevs;

	nvlist_t *config = zpool_get_config(zhp, NULL);

	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &tree) != 0 ||
	    nvlist_lookup_nvlist_array(tree, ZPOOL_CONFIG_CHILDREN,
	    &vdevs, &nvdevs) != 0) {
		return (ZVOL_DEFAULT_BLOCKSIZE);
	}

	for (int i = 0; i < nvdevs; i++) {
		nvlist_t *nv = vdevs[i];
		uint64_t ashift, ndata, nparity;

		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_ASHIFT, &ashift) != 0)
			continue;

		if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_DRAID_NDATA,
		    &ndata) == 0) {
			/* dRAID minimum allocation width */
			asize = MAX(asize, ndata * (1ULL << ashift));
		} else if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NPARITY,
		    &nparity) == 0) {
			/* raidz minimum allocation width */
			if (nparity == 1)
				asize = MAX(asize, 2 * (1ULL << ashift));
			else
				asize = MAX(asize, 4 * (1ULL << ashift));
		} else {
			/* mirror or (non-redundant) leaf vdev */
			asize = MAX(asize, 1ULL << ashift);
		}
	}

	/*
	 * Calculate the target volblocksize such that more than half
	 * of the asize is used. The following table is for 4k sectors.
	 *
	 * n   asize   blksz  used  |   n   asize   blksz  used
	 * -------------------------+---------------------------------
	 * 1   4,096   8,192  100%  |   9  36,864  32,768   88%
	 * 2   8,192   8,192  100%  |  10  40,960  32,768   80%
	 * 3  12,288   8,192   66%  |  11  45,056  32,768   72%
	 * 4  16,384  16,384  100%  |  12  49,152  32,768   66%
	 * 5  20,480  16,384   80%  |  13  53,248  32,768   61%
	 * 6  24,576  16,384   66%  |  14  57,344  32,768   57%
	 * 7  28,672  16,384   57%  |  15  61,440  32,768   53%
	 * 8  32,768  32,768  100%  |  16  65,536  65,636  100%
	 *
	 * This is primarily a concern for dRAID which always allocates
	 * a full stripe width.  For dRAID the default stripe width is
	 * n=8 in which case the volblocksize is set to 32k. Ignoring
	 * compression there are no unused sectors.  This same reasoning
	 * applies to raidz[2,3] so target 4 sectors to minimize waste.
	 */
	uint64_t tgt_volblocksize = ZVOL_DEFAULT_BLOCKSIZE;
	while (tgt_volblocksize * 2 <= asize)
		tgt_volblocksize *= 2;

	const char *prop = zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE);
	if (nvlist_lookup_uint64(props, prop, &volblocksize) == 0) {

		/* Issue a warning when a non-optimal size is requested. */
		if (volblocksize < ZVOL_DEFAULT_BLOCKSIZE) {
			(void) fprintf(stderr, gettext("Warning: "
			    "volblocksize (%llu) is less than the default "
			    "minimum block size (%llu).\nTo reduce wasted "
			    "space a volblocksize of %llu is recommended.\n"),
			    (u_longlong_t)volblocksize,
			    (u_longlong_t)ZVOL_DEFAULT_BLOCKSIZE,
			    (u_longlong_t)tgt_volblocksize);
		} else if (volblocksize < tgt_volblocksize) {
			(void) fprintf(stderr, gettext("Warning: "
			    "volblocksize (%llu) is much less than the "
			    "minimum allocation\nunit (%llu), which wastes "
			    "at least %llu%% of space. To reduce wasted "
			    "space,\nuse a larger volblocksize (%llu is "
			    "recommended), fewer dRAID data disks\n"
			    "per group, or smaller sector size (ashift).\n"),
			    (u_longlong_t)volblocksize, (u_longlong_t)asize,
			    (u_longlong_t)((100 * (asize - volblocksize)) /
			    asize), (u_longlong_t)tgt_volblocksize);
		}
	} else {
		volblocksize = tgt_volblocksize;
		fnvlist_add_uint64(props, prop, volblocksize);
	}

	return (volblocksize);
}

/*
 * zfs create [-Pnpv] [-o prop=value] ... fs
 * zfs create [-Pnpsv] [-b blocksize] [-o prop=value] ... -V vol size
 *
 * Create a new dataset.  This command can be used to create filesystems
 * and volumes.  Snapshot creation is handled by 'zfs snapshot'.
 * For volumes, the user must specify a size to be used.
 *
 * The '-s' flag applies only to volumes, and indicates that we should not try
 * to set the reservation for this volume.  By default we set a reservation
 * equal to the size for any volume.  For pools with SPA_VERSION >=
 * SPA_VERSION_REFRESERVATION, we set a refreservation instead.
 *
 * The '-p' flag creates all the non-existing ancestors of the target first.
 *
 * The '-n' flag is no-op (dry run) mode.  This will perform a user-space sanity
 * check of arguments and properties, but does not check for permissions,
 * available space, etc.
 *
 * The '-u' flag prevents the newly created file system from being mounted.
 *
 * The '-v' flag is for verbose output.
 *
 * The '-P' flag is used for parseable output.  It implies '-v'.
 */
int
zfs_do_create(int argc, char **argv)
{
	zfs_type_t type = ZFS_TYPE_FILESYSTEM;
	zpool_handle_t *zpool_handle = NULL;
	nvlist_t *real_props = NULL;
	uint64_t volsize = 0;
	int c;
	boolean_t noreserve = B_FALSE;
	boolean_t bflag = B_FALSE;
	boolean_t parents = B_FALSE;
	boolean_t dryrun = B_FALSE;
	boolean_t nomount = B_FALSE;
	boolean_t verbose = B_FALSE;
	boolean_t parseable = B_FALSE;
	int ret = 1;
	nvlist_t *props;
	uint64_t intval;
	const char *strval;

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0)
		nomem();

	/* check options */
	while ((c = getopt(argc, argv, ":PV:b:nso:puv")) != -1) {
		switch (c) {
		case 'V':
			type = ZFS_TYPE_VOLUME;
			if (zfs_nicestrtonum(g_zfs, optarg, &intval) != 0) {
				(void) fprintf(stderr, gettext("bad volume "
				    "size '%s': %s\n"), optarg,
				    libzfs_error_description(g_zfs));
				goto error;
			}

			if (nvlist_add_uint64(props,
			    zfs_prop_to_name(ZFS_PROP_VOLSIZE), intval) != 0)
				nomem();
			volsize = intval;
			break;
		case 'P':
			verbose = B_TRUE;
			parseable = B_TRUE;
			break;
		case 'p':
			parents = B_TRUE;
			break;
		case 'b':
			bflag = B_TRUE;
			if (zfs_nicestrtonum(g_zfs, optarg, &intval) != 0) {
				(void) fprintf(stderr, gettext("bad volume "
				    "block size '%s': %s\n"), optarg,
				    libzfs_error_description(g_zfs));
				goto error;
			}

			if (nvlist_add_uint64(props,
			    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE),
			    intval) != 0)
				nomem();
			break;
		case 'n':
			dryrun = B_TRUE;
			break;
		case 'o':
			if (!parseprop(props, optarg))
				goto error;
			break;
		case 's':
			noreserve = B_TRUE;
			break;
		case 'u':
			nomount = B_TRUE;
			break;
		case 'v':
			verbose = B_TRUE;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing size "
			    "argument\n"));
			goto badusage;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			goto badusage;
		}
	}

	if ((bflag || noreserve) && type != ZFS_TYPE_VOLUME) {
		(void) fprintf(stderr, gettext("'-s' and '-b' can only be "
		    "used when creating a volume\n"));
		goto badusage;
	}
	if (nomount && type != ZFS_TYPE_FILESYSTEM) {
		(void) fprintf(stderr, gettext("'-u' can only be "
		    "used when creating a filesystem\n"));
		goto badusage;
	}

	argc -= optind;
	argv += optind;

	/* check number of arguments */
	if (argc == 0) {
		(void) fprintf(stderr, gettext("missing %s argument\n"),
		    zfs_type_to_name(type));
		goto badusage;
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		goto badusage;
	}

	if (dryrun || type == ZFS_TYPE_VOLUME) {
		char msg[ZFS_MAX_DATASET_NAME_LEN * 2];
		char *p;

		if ((p = strchr(argv[0], '/')) != NULL)
			*p = '\0';
		zpool_handle = zpool_open(g_zfs, argv[0]);
		if (p != NULL)
			*p = '/';
		if (zpool_handle == NULL)
			goto error;

		(void) snprintf(msg, sizeof (msg),
		    dryrun ? gettext("cannot verify '%s'") :
		    gettext("cannot create '%s'"), argv[0]);
		if (props && (real_props = zfs_valid_proplist(g_zfs, type,
		    props, 0, NULL, zpool_handle, B_TRUE, msg)) == NULL) {
			zpool_close(zpool_handle);
			goto error;
		}
	}

	if (type == ZFS_TYPE_VOLUME) {
		const char *prop = zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE);
		uint64_t volblocksize = default_volblocksize(zpool_handle,
		    real_props);

		if (volblocksize != ZVOL_DEFAULT_BLOCKSIZE &&
		    nvlist_lookup_string(props, prop, &strval) != 0) {
			char *tmp;
			if (asprintf(&tmp, "%llu",
			    (u_longlong_t)volblocksize) == -1)
				nomem();
			nvlist_add_string(props, prop, tmp);
			free(tmp);
		}

		/*
		 * If volsize is not a multiple of volblocksize, round it
		 * up to the nearest multiple of the volblocksize.
		 */
		if (volsize % volblocksize) {
			volsize = P2ROUNDUP_TYPED(volsize, volblocksize,
			    uint64_t);

			if (nvlist_add_uint64(props,
			    zfs_prop_to_name(ZFS_PROP_VOLSIZE), volsize) != 0) {
				nvlist_free(props);
				nomem();
			}
		}
	}

	if (type == ZFS_TYPE_VOLUME && !noreserve) {
		uint64_t spa_version;
		zfs_prop_t resv_prop;

		spa_version = zpool_get_prop_int(zpool_handle,
		    ZPOOL_PROP_VERSION, NULL);
		if (spa_version >= SPA_VERSION_REFRESERVATION)
			resv_prop = ZFS_PROP_REFRESERVATION;
		else
			resv_prop = ZFS_PROP_RESERVATION;

		volsize = zvol_volsize_to_reservation(zpool_handle, volsize,
		    real_props);

		if (nvlist_lookup_string(props, zfs_prop_to_name(resv_prop),
		    &strval) != 0) {
			if (nvlist_add_uint64(props,
			    zfs_prop_to_name(resv_prop), volsize) != 0) {
				nvlist_free(props);
				nomem();
			}
		}
	}
	if (zpool_handle != NULL) {
		zpool_close(zpool_handle);
		nvlist_free(real_props);
	}

	if (parents && zfs_name_valid(argv[0], type)) {
		/*
		 * Now create the ancestors of target dataset.  If the target
		 * already exists and '-p' option was used we should not
		 * complain.
		 */
		if (zfs_dataset_exists(g_zfs, argv[0], type)) {
			ret = 0;
			goto error;
		}
		if (verbose) {
			(void) printf(parseable ? "create_ancestors\t%s\n" :
			    dryrun ?  "would create ancestors of %s\n" :
			    "create ancestors of %s\n", argv[0]);
		}
		if (!dryrun) {
			if (zfs_create_ancestors(g_zfs, argv[0]) != 0) {
				goto error;
			}
		}
	}

	if (verbose) {
		nvpair_t *nvp = NULL;
		(void) printf(parseable ? "create\t%s\n" :
		    dryrun ? "would create %s\n" : "create %s\n", argv[0]);
		while ((nvp = nvlist_next_nvpair(props, nvp)) != NULL) {
			uint64_t uval;
			const char *sval;

			switch (nvpair_type(nvp)) {
			case DATA_TYPE_UINT64:
				VERIFY0(nvpair_value_uint64(nvp, &uval));
				(void) printf(parseable ?
				    "property\t%s\t%llu\n" : "\t%s=%llu\n",
				    nvpair_name(nvp), (u_longlong_t)uval);
				break;
			case DATA_TYPE_STRING:
				VERIFY0(nvpair_value_string(nvp, &sval));
				(void) printf(parseable ?
				    "property\t%s\t%s\n" : "\t%s=%s\n",
				    nvpair_name(nvp), sval);
				break;
			default:
				(void) fprintf(stderr, "property '%s' "
				    "has illegal type %d\n",
				    nvpair_name(nvp), nvpair_type(nvp));
				abort();
			}
		}
	}
	if (dryrun) {
		ret = 0;
		goto error;
	}

	/* pass to libzfs */
	if (zfs_create(g_zfs, argv[0], type, props) != 0)
		goto error;

	if (log_history) {
		(void) zpool_log_history(g_zfs, history_str);
		log_history = B_FALSE;
	}

	if (nomount) {
		ret = 0;
		goto error;
	}

	ret = zfs_mount_and_share(g_zfs, argv[0], ZFS_TYPE_DATASET);
error:
	nvlist_free(props);
	return (ret);
badusage:
	nvlist_free(props);
	usage(B_FALSE);
	return (2);
}
