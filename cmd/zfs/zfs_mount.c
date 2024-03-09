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

#define	CHECK_SPINNER 30
#define	SPINNER_TIME 3		/* seconds */
#define	MOUNT_TIME 1		/* seconds */

typedef struct get_all_state {
	boolean_t	ga_verbose;
	get_all_cb_t	*ga_cbp;
} get_all_state_t;

static int
get_one_dataset(zfs_handle_t *zhp, void *data)
{
	static const char *const spin[] = { "-", "\\", "|", "/" };
	static int spinval = 0;
	static int spincheck = 0;
	static time_t last_spin_time = (time_t)0;
	get_all_state_t *state = data;
	zfs_type_t type = zfs_get_type(zhp);

	if (state->ga_verbose) {
		if (--spincheck < 0) {
			time_t now = time(NULL);
			if (last_spin_time + SPINNER_TIME < now) {
				update_progress(spin[spinval++ % 4]);
				last_spin_time = now;
			}
			spincheck = CHECK_SPINNER;
		}
	}

	/*
	 * Iterate over any nested datasets.
	 */
	if (zfs_iter_filesystems_v2(zhp, 0, get_one_dataset, data) != 0) {
		zfs_close(zhp);
		return (1);
	}

	/*
	 * Skip any datasets whose type does not match.
	 */
	if ((type & ZFS_TYPE_FILESYSTEM) == 0) {
		zfs_close(zhp);
		return (0);
	}
	libzfs_add_handle(state->ga_cbp, zhp);
	assert(state->ga_cbp->cb_used <= state->ga_cbp->cb_alloc);

	return (0);
}

static int
get_recursive_datasets(zfs_handle_t *zhp, void *data)
{
	get_all_state_t *state = data;
	int len = strlen(zfs_get_name(zhp));
	for (int i = 0; i < state->ga_count; ++i) {
		if (strcmp(state->ga_datasets[i], zfs_get_name(zhp)) == 0)
			return (get_one_dataset(zhp, data));
		else if ((strncmp(state->ga_datasets[i], zfs_get_name(zhp),
		    len) == 0) && state->ga_datasets[i][len] == '/') {
			(void) zfs_iter_filesystems_v2(zhp, 0,
			    get_recursive_datasets, data);
		}
	}
	zfs_close(zhp);
	return (0);
}

static void
get_all_datasets(get_all_state_t *state)
{
	if (state->ga_verbose)
		set_progress_header(gettext("Reading ZFS config"));
	if (state->ga_datasets == NULL)
		(void) zfs_iter_root(g_zfs, get_one_dataset, state);
	else
		(void) zfs_iter_root(g_zfs, get_recursive_datasets, state);

	if (state->ga_verbose)
		finish_progress(gettext("done."));
}

/*
 * Generic callback for sharing or mounting filesystems.  Because the code is so
 * similar, we have a common function with an extra parameter to determine which
 * mode we are using.
 */
typedef enum { OP_SHARE, OP_MOUNT } share_mount_op_t;

typedef struct share_mount_state {
	share_mount_op_t	sm_op;
	boolean_t	sm_verbose;
	int	sm_flags;
	char	*sm_options;
	enum sa_protocol	sm_proto; /* only valid for OP_SHARE */
	pthread_mutex_t	sm_lock; /* protects the remaining fields */
	uint_t	sm_total; /* number of filesystems to process */
	uint_t	sm_done; /* number of filesystems processed */
	int	sm_status; /* -1 if any of the share/mount operations failed */
} share_mount_state_t;

/*
 * Share or mount a dataset.
 */
static int
share_mount_one(zfs_handle_t *zhp, int op, int flags, enum sa_protocol protocol,
    boolean_t explicit, const char *options)
{
	char mountpoint[ZFS_MAXPROPLEN];
	char shareopts[ZFS_MAXPROPLEN];
	char smbshareopts[ZFS_MAXPROPLEN];
	const char *cmdname = op == OP_SHARE ? "share" : "mount";
	struct mnttab mnt;
	uint64_t zoned, canmount;
	boolean_t shared_nfs, shared_smb;

	assert(zfs_get_type(zhp) & ZFS_TYPE_FILESYSTEM);

	/*
	 * Check to make sure we can mount/share this dataset.  If we
	 * are in the global zone and the filesystem is exported to a
	 * local zone, or if we are in a local zone and the
	 * filesystem is not exported, then it is an error.
	 */
	zoned = zfs_prop_get_int(zhp, ZFS_PROP_ZONED);

	if (zoned && getzoneid() == GLOBAL_ZONEID) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "dataset is exported to a local zone\n"), cmdname,
		    zfs_get_name(zhp));
		return (1);

	} else if (!zoned && getzoneid() != GLOBAL_ZONEID) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "permission denied\n"), cmdname,
		    zfs_get_name(zhp));
		return (1);
	}

	/*
	 * Ignore any filesystems which don't apply to us. This
	 * includes those with a legacy mountpoint, or those with
	 * legacy share options.
	 */
	verify(zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) == 0);
	verify(zfs_prop_get(zhp, ZFS_PROP_SHARENFS, shareopts,
	    sizeof (shareopts), NULL, NULL, 0, B_FALSE) == 0);
	verify(zfs_prop_get(zhp, ZFS_PROP_SHARESMB, smbshareopts,
	    sizeof (smbshareopts), NULL, NULL, 0, B_FALSE) == 0);

	if (op == OP_SHARE && strcmp(shareopts, "off") == 0 &&
	    strcmp(smbshareopts, "off") == 0) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot share '%s': "
		    "legacy share\n"), zfs_get_name(zhp));
		(void) fprintf(stderr, gettext("use exports(5) or "
		    "smb.conf(5) to share this filesystem, or set "
		    "the sharenfs or sharesmb property\n"));
		return (1);
	}

	/*
	 * We cannot share or mount legacy filesystems. If the
	 * shareopts is non-legacy but the mountpoint is legacy, we
	 * treat it as a legacy share.
	 */
	if (strcmp(mountpoint, "legacy") == 0) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "legacy mountpoint\n"), cmdname, zfs_get_name(zhp));
		(void) fprintf(stderr, gettext("use %s(8) to "
		    "%s this filesystem\n"), cmdname, cmdname);
		return (1);
	}

	if (strcmp(mountpoint, "none") == 0) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': no "
		    "mountpoint set\n"), cmdname, zfs_get_name(zhp));
		return (1);
	}

	/*
	 * canmount	explicit	outcome
	 * on		no		pass through
	 * on		yes		pass through
	 * off		no		return 0
	 * off		yes		display error, return 1
	 * noauto	no		return 0
	 * noauto	yes		pass through
	 */
	canmount = zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT);
	if (canmount == ZFS_CANMOUNT_OFF) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "'canmount' property is set to 'off'\n"), cmdname,
		    zfs_get_name(zhp));
		return (1);
	} else if (canmount == ZFS_CANMOUNT_NOAUTO && !explicit) {
		/*
		 * When performing a 'zfs mount -a', we skip any mounts for
		 * datasets that have 'noauto' set. Sharing a dataset with
		 * 'noauto' set is only allowed if it's mounted.
		 */
		if (op == OP_MOUNT)
			return (0);
		if (op == OP_SHARE && !zfs_is_mounted(zhp, NULL)) {
			/* also purge it from existing exports */
			zfs_unshare(zhp, mountpoint, NULL);
			return (0);
		}
	}

	/*
	 * If this filesystem is encrypted and does not have
	 * a loaded key, we can not mount it.
	 */
	if ((flags & MS_CRYPT) == 0 &&
	    zfs_prop_get_int(zhp, ZFS_PROP_ENCRYPTION) != ZIO_CRYPT_OFF &&
	    zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS) ==
	    ZFS_KEYSTATUS_UNAVAILABLE) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "encryption key not loaded\n"), cmdname, zfs_get_name(zhp));
		return (1);
	}

	/*
	 * If this filesystem is inconsistent and has a receive resume
	 * token, we can not mount it.
	 */
	if (zfs_prop_get_int(zhp, ZFS_PROP_INCONSISTENT) &&
	    zfs_prop_get(zhp, ZFS_PROP_RECEIVE_RESUME_TOKEN,
	    NULL, 0, NULL, NULL, 0, B_TRUE) == 0) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "Contains partially-completed state from "
		    "\"zfs receive -s\", which can be resumed with "
		    "\"zfs send -t\"\n"),
		    cmdname, zfs_get_name(zhp));
		return (1);
	}

	if (zfs_prop_get_int(zhp, ZFS_PROP_REDACTED) && !(flags & MS_FORCE)) {
		if (!explicit)
			return (0);

		(void) fprintf(stderr, gettext("cannot %s '%s': "
		    "Dataset is not complete, was created by receiving "
		    "a redacted zfs send stream.\n"), cmdname,
		    zfs_get_name(zhp));
		return (1);
	}

	/*
	 * At this point, we have verified that the mountpoint and/or
	 * shareopts are appropriate for auto management. If the
	 * filesystem is already mounted or shared, return (failing
	 * for explicit requests); otherwise mount or share the
	 * filesystem.
	 */
	switch (op) {
	case OP_SHARE: {
		enum sa_protocol prot[] = {SA_PROTOCOL_NFS, SA_NO_PROTOCOL};
		shared_nfs = zfs_is_shared(zhp, NULL, prot);
		*prot = SA_PROTOCOL_SMB;
		shared_smb = zfs_is_shared(zhp, NULL, prot);

		if ((shared_nfs && shared_smb) ||
		    (shared_nfs && strcmp(shareopts, "on") == 0 &&
		    strcmp(smbshareopts, "off") == 0) ||
		    (shared_smb && strcmp(smbshareopts, "on") == 0 &&
		    strcmp(shareopts, "off") == 0)) {
			if (!explicit)
				return (0);

			(void) fprintf(stderr, gettext("cannot share "
			    "'%s': filesystem already shared\n"),
			    zfs_get_name(zhp));
			return (1);
		}

		if (!zfs_is_mounted(zhp, NULL) &&
		    zfs_mount(zhp, NULL, flags) != 0)
			return (1);

		*prot = protocol;
		if (zfs_share(zhp, protocol == SA_NO_PROTOCOL ? NULL : prot))
			return (1);

	}
		break;

	case OP_MOUNT:
		mnt.mnt_mntopts = (char *)(options ?: "");

		if (!hasmntopt(&mnt, MNTOPT_REMOUNT) &&
		    zfs_is_mounted(zhp, NULL)) {
			if (!explicit)
				return (0);

			(void) fprintf(stderr, gettext("cannot mount "
			    "'%s': filesystem already mounted\n"),
			    zfs_get_name(zhp));
			return (1);
		}

		if (zfs_mount(zhp, options, flags) != 0)
			return (1);
		break;
	}

	return (0);
}

/*
 * Reports progress in the form "(current/total)".  Not thread-safe.
 */
static void
report_mount_progress(int current, int total)
{
	static time_t last_progress_time = 0;
	time_t now = time(NULL);
	char info[32];

	/* display header if we're here for the first time */
	if (current == 1) {
		set_progress_header(gettext("Mounting ZFS filesystems"));
	} else if (current != total && last_progress_time + MOUNT_TIME >= now) {
		/* too soon to report again */
		return;
	}

	last_progress_time = now;

	(void) sprintf(info, "(%d/%d)", current, total);

	if (current == total)
		finish_progress(info);
	else
		update_progress(info);
}

/*
 * zfs_foreach_mountpoint() callback that mounts or shares one filesystem and
 * updates the progress meter.
 */
static int
share_mount_one_cb(zfs_handle_t *zhp, void *arg)
{
	share_mount_state_t *sms = arg;
	int ret;

	ret = share_mount_one(zhp, sms->sm_op, sms->sm_flags, sms->sm_proto,
	    B_FALSE, sms->sm_options);

	pthread_mutex_lock(&sms->sm_lock);
	if (ret != 0)
		sms->sm_status = ret;
	sms->sm_done++;
	if (sms->sm_verbose)
		report_mount_progress(sms->sm_done, sms->sm_total);
	pthread_mutex_unlock(&sms->sm_lock);
	return (ret);
}

static void
append_options(char *mntopts, char *newopts)
{
	int len = strlen(mntopts);

	/* original length plus new string to append plus 1 for the comma */
	if (len + 1 + strlen(newopts) >= MNT_LINE_MAX) {
		(void) fprintf(stderr, gettext("the opts argument for "
		    "'%s' option is too long (more than %d chars)\n"),
		    "-o", MNT_LINE_MAX);
		usage(B_FALSE);
	}

	if (*mntopts)
		mntopts[len++] = ',';

	(void) strcpy(&mntopts[len], newopts);
}

static enum sa_protocol
sa_protocol_decode(const char *protocol)
{
	for (enum sa_protocol i = 0; i < ARRAY_SIZE(sa_protocol_names); ++i)
		if (strcmp(protocol, sa_protocol_names[i]) == 0)
			return (i);

	(void) fputs(gettext("share type must be one of: "), stderr);
	for (enum sa_protocol i = 0;
	    i < ARRAY_SIZE(sa_protocol_names); ++i)
		(void) fprintf(stderr, "%s%s",
		    i != 0 ? ", " : "", sa_protocol_names[i]);
	(void) fputc('\n', stderr);
	usage(B_FALSE);
}

static int
share_mount(int op, int argc, char **argv)
{
	int do_all = 0;
	int recursive = 0;
	boolean_t verbose = B_FALSE;
	int c, ret = 0;
	char *options = NULL;
	int flags = 0;

	/* check options */
	while ((c = getopt(argc, argv, op == OP_MOUNT ? ":aRlvo:Of" : "al"))
	    != -1) {
		switch (c) {
		case 'a':
			do_all = 1;
			break;
		case 'R':
			recursive = 1;
			break;
		case 'v':
			verbose = B_TRUE;
			break;
		case 'l':
			flags |= MS_CRYPT;
			break;
		case 'o':
			if (*optarg == '\0') {
				(void) fprintf(stderr, gettext("empty mount "
				    "options (-o) specified\n"));
				usage(B_FALSE);
			}

			if (options == NULL)
				options = safe_malloc(MNT_LINE_MAX + 1);

			/* option validation is done later */
			append_options(options, optarg);
			break;
		case 'O':
			flags |= MS_OVERLAY;
			break;
		case 'f':
			flags |= MS_FORCE;
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

	/* check number of arguments */
	if (do_all || recursive) {
		enum sa_protocol protocol = SA_NO_PROTOCOL;

		if (op == OP_SHARE && argc > 0) {
			protocol = sa_protocol_decode(argv[0]);
			argc--;
			argv++;
		}

		if (argc != 0 && do_all) {
			(void) fprintf(stderr, gettext("too many arguments\n"));
			usage(B_FALSE);
		}

		if (argc == 0 && recursive) {
			(void) fprintf(stderr,
			    gettext("no dataset provided\n"));
			usage(B_FALSE);
		}

		start_progress_timer();
		get_all_cb_t cb = { 0 };
		get_all_state_t state = { 0 };
		if (argc == 0) {
			state.ga_datasets = NULL;
			state.ga_count = -1;
		} else {
			zfs_handle_t *zhp;
			for (int i = 0; i < argc; i++) {
				zhp = zfs_open(g_zfs, argv[i],
				    ZFS_TYPE_FILESYSTEM);
				if (zhp == NULL)
					usage(B_FALSE);
				zfs_close(zhp);
			}
			state.ga_datasets = argv;
			state.ga_count = argc;
		}
		state.ga_verbose = verbose;
		state.ga_cbp = &cb;
		get_all_datasets(&state);

		if (cb.cb_used == 0) {
			free(options);
			return (0);
		}

		share_mount_state_t share_mount_state = { 0 };
		share_mount_state.sm_op = op;
		share_mount_state.sm_verbose = verbose;
		share_mount_state.sm_flags = flags;
		share_mount_state.sm_options = options;
		share_mount_state.sm_proto = protocol;
		share_mount_state.sm_total = cb.cb_used;
		pthread_mutex_init(&share_mount_state.sm_lock, NULL);

		/* For a 'zfs share -a' operation start with a clean slate. */
		if (op == OP_SHARE)
			zfs_truncate_shares(NULL);

		/*
		 * libshare isn't mt-safe, so only do the operation in parallel
		 * if we're mounting. Additionally, the key-loading option must
		 * be serialized so that we can prompt the user for their keys
		 * in a consistent manner.
		 */
		zfs_foreach_mountpoint(g_zfs, cb.cb_handles, cb.cb_used,
		    share_mount_one_cb, &share_mount_state,
		    op == OP_MOUNT && !(flags & MS_CRYPT));
		zfs_commit_shares(NULL);

		ret = share_mount_state.sm_status;

		for (int i = 0; i < cb.cb_used; i++)
			zfs_close(cb.cb_handles[i]);
		free(cb.cb_handles);
	} else if (argc == 0) {
		FILE *mnttab;
		struct mnttab entry;

		if ((op == OP_SHARE) || (options != NULL)) {
			(void) fprintf(stderr, gettext("missing filesystem "
			    "argument (specify -a for all)\n"));
			usage(B_FALSE);
		}

		/*
		 * When mount is given no arguments, go through
		 * /proc/self/mounts and display any active ZFS mounts.
		 * We hide any snapshots, since they are controlled
		 * automatically.
		 */

		if ((mnttab = fopen(MNTTAB, "re")) == NULL) {
			free(options);
			return (ENOENT);
		}

		while (getmntent(mnttab, &entry) == 0) {
			if (strcmp(entry.mnt_fstype, MNTTYPE_ZFS) != 0 ||
			    strchr(entry.mnt_special, '@') != NULL)
				continue;

			(void) printf("%-30s  %s\n", entry.mnt_special,
			    entry.mnt_mountp);
		}

		(void) fclose(mnttab);
	} else {
		zfs_handle_t *zhp;

		if (argc > 1) {
			(void) fprintf(stderr,
			    gettext("too many arguments\n"));
			usage(B_FALSE);
		}

		if ((zhp = zfs_open(g_zfs, argv[0],
		    ZFS_TYPE_FILESYSTEM)) == NULL) {
			ret = 1;
		} else {
			ret = share_mount_one(zhp, op, flags, SA_NO_PROTOCOL,
			    B_TRUE, options);
			zfs_commit_shares(NULL);
			zfs_close(zhp);
		}
	}

	free(options);
	return (ret);
}

/*
 * zfs mount -a
 * zfs mount filesystem
 *
 * Mount all filesystems, or mount the given filesystem.
 */
int
zfs_do_mount(int argc, char **argv)
{
	return (share_mount(OP_MOUNT, argc, argv));
}

/*
 * zfs share -a [nfs | smb]
 * zfs share filesystem
 *
 * Share all filesystems, or share the given filesystem.
 */
int
zfs_do_share(int argc, char **argv)
{
	return (share_mount(OP_SHARE, argc, argv));
}
