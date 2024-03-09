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
 * Array of prefixes to exclude –
 * a linear search, even if executed for each dataset,
 * is plenty good enough.
 */
typedef struct zfs_send_exclude_arg {
	size_t count;
	const char **list;
} zfs_send_exclude_arg_t;

static boolean_t
zfs_do_send_exclude(zfs_handle_t *zhp, void *context)
{
	zfs_send_exclude_arg_t *excludes = context;
	const char *name = zfs_get_name(zhp);

	for (size_t i = 0; i < excludes->count; ++i) {
		size_t len = strlen(excludes->list[i]);
		if (strncmp(name, excludes->list[i], len) == 0 &&
		    memchr("/@", name[len], sizeof ("/@")))
			return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Send a backup stream to stdout.
 */
int
zfs_do_send(int argc, char **argv)
{
	char *fromname = NULL;
	char *toname = NULL;
	char *resume_token = NULL;
	char *cp;
	zfs_handle_t *zhp;
	sendflags_t flags = { 0 };
	int c, err;
	nvlist_t *dbgnv = NULL;
	char *redactbook = NULL;
	zfs_send_exclude_arg_t excludes = { 0 };

	struct option long_options[] = {
		{"replicate",	no_argument,		NULL, 'R'},
		{"skip-missing",	no_argument,	NULL, 's'},
		{"redact",	required_argument,	NULL, 'd'},
		{"props",	no_argument,		NULL, 'p'},
		{"parsable",	no_argument,		NULL, 'P'},
		{"dedup",	no_argument,		NULL, 'D'},
		{"proctitle",	no_argument,		NULL, 'V'},
		{"verbose",	no_argument,		NULL, 'v'},
		{"dryrun",	no_argument,		NULL, 'n'},
		{"large-block",	no_argument,		NULL, 'L'},
		{"embed",	no_argument,		NULL, 'e'},
		{"resume",	required_argument,	NULL, 't'},
		{"compressed",	no_argument,		NULL, 'c'},
		{"raw",		no_argument,		NULL, 'w'},
		{"backup",	no_argument,		NULL, 'b'},
		{"holds",	no_argument,		NULL, 'h'},
		{"saved",	no_argument,		NULL, 'S'},
		{"exclude",	required_argument,	NULL, 'X'},
		{0, 0, 0, 0}
	};

	/* check options */
	while ((c = getopt_long(argc, argv, ":i:I:RsDpVvnPLeht:cwbd:SX:",
	    long_options, NULL)) != -1) {
		switch (c) {
		case 'X':
			for (char *ds; (ds = strsep(&optarg, ",")) != NULL; ) {
				if (!zfs_name_valid(ds, ZFS_TYPE_DATASET) ||
				    strchr(ds, '/') == NULL) {
					(void) fprintf(stderr, gettext("-X %s: "
					    "not a valid non-root dataset name"
					    ".\n"), ds);
					usage(B_FALSE);
				}
				excludes.list = safe_realloc(excludes.list,
				    sizeof (char *) * (excludes.count + 1));
				excludes.list[excludes.count++] = ds;
			}
			break;
		case 'i':
			if (fromname)
				usage(B_FALSE);
			fromname = optarg;
			break;
		case 'I':
			if (fromname)
				usage(B_FALSE);
			fromname = optarg;
			flags.doall = B_TRUE;
			break;
		case 'R':
			flags.replicate = B_TRUE;
			break;
		case 's':
			flags.skipmissing = B_TRUE;
			break;
		case 'd':
			redactbook = optarg;
			break;
		case 'p':
			flags.props = B_TRUE;
			break;
		case 'b':
			flags.backup = B_TRUE;
			break;
		case 'h':
			flags.holds = B_TRUE;
			break;
		case 'P':
			flags.parsable = B_TRUE;
			break;
		case 'V':
			flags.progressastitle = B_TRUE;
			break;
		case 'v':
			flags.verbosity++;
			flags.progress = B_TRUE;
			break;
		case 'D':
			(void) fprintf(stderr,
			    gettext("WARNING: deduplicated send is no "
			    "longer supported.  A regular,\n"
			    "non-deduplicated stream will be generated.\n\n"));
			break;
		case 'n':
			flags.dryrun = B_TRUE;
			break;
		case 'L':
			flags.largeblock = B_TRUE;
			break;
		case 'e':
			flags.embed_data = B_TRUE;
			break;
		case 't':
			resume_token = optarg;
			break;
		case 'c':
			flags.compress = B_TRUE;
			break;
		case 'w':
			flags.raw = B_TRUE;
			flags.compress = B_TRUE;
			flags.embed_data = B_TRUE;
			flags.largeblock = B_TRUE;
			break;
		case 'S':
			flags.saved = B_TRUE;
			break;
		case ':':
			/*
			 * If a parameter was not passed, optopt contains the
			 * value that would normally lead us into the
			 * appropriate case statement.  If it's > 256, then this
			 * must be a longopt and we should look at argv to get
			 * the string.  Otherwise it's just the character, so we
			 * should use it directly.
			 */
			if (optopt <= UINT8_MAX) {
				(void) fprintf(stderr,
				    gettext("missing argument for '%c' "
				    "option\n"), optopt);
			} else {
				(void) fprintf(stderr,
				    gettext("missing argument for '%s' "
				    "option\n"), argv[optind - 1]);
			}
			free(excludes.list);
			usage(B_FALSE);
			break;
		case '?':
		default:
			/*
			 * If an invalid flag was passed, optopt contains the
			 * character if it was a short flag, or 0 if it was a
			 * longopt.
			 */
			if (optopt != 0) {
				(void) fprintf(stderr,
				    gettext("invalid option '%c'\n"), optopt);
			} else {
				(void) fprintf(stderr,
				    gettext("invalid option '%s'\n"),
				    argv[optind - 1]);

			}
			free(excludes.list);
			usage(B_FALSE);
		}
	}

	if ((flags.parsable || flags.progressastitle) && flags.verbosity == 0)
		flags.verbosity = 1;

	if (excludes.count > 0 && !flags.replicate) {
		free(excludes.list);
		(void) fprintf(stderr, gettext("Cannot specify "
		    "dataset exclusion (-X) on a non-recursive "
		    "send.\n"));
		return (1);
	}

	argc -= optind;
	argv += optind;

	if (resume_token != NULL) {
		if (fromname != NULL || flags.replicate || flags.props ||
		    flags.backup || flags.holds ||
		    flags.saved || redactbook != NULL) {
			free(excludes.list);
			(void) fprintf(stderr,
			    gettext("invalid flags combined with -t\n"));
			usage(B_FALSE);
		}
		if (argc > 0) {
			free(excludes.list);
			(void) fprintf(stderr, gettext("too many arguments\n"));
			usage(B_FALSE);
		}
	} else {
		if (argc < 1) {
			free(excludes.list);
			(void) fprintf(stderr,
			    gettext("missing snapshot argument\n"));
			usage(B_FALSE);
		}
		if (argc > 1) {
			free(excludes.list);
			(void) fprintf(stderr, gettext("too many arguments\n"));
			usage(B_FALSE);
		}
	}

	if (flags.saved) {
		if (fromname != NULL || flags.replicate || flags.props ||
		    flags.doall || flags.backup ||
		    flags.holds || flags.largeblock || flags.embed_data ||
		    flags.compress || flags.raw || redactbook != NULL) {
			free(excludes.list);

			(void) fprintf(stderr, gettext("incompatible flags "
			    "combined with saved send flag\n"));
			usage(B_FALSE);
		}
		if (strchr(argv[0], '@') != NULL) {
			free(excludes.list);

			(void) fprintf(stderr, gettext("saved send must "
			    "specify the dataset with partially-received "
			    "state\n"));
			usage(B_FALSE);
		}
	}

	if (flags.raw && redactbook != NULL) {
		free(excludes.list);
		(void) fprintf(stderr,
		    gettext("Error: raw sends may not be redacted.\n"));
		return (1);
	}

	if (!flags.dryrun && isatty(STDOUT_FILENO)) {
		free(excludes.list);
		(void) fprintf(stderr,
		    gettext("Error: Stream can not be written to a terminal.\n"
		    "You must redirect standard output.\n"));
		return (1);
	}

	if (flags.saved) {
		zhp = zfs_open(g_zfs, argv[0], ZFS_TYPE_DATASET);
		if (zhp == NULL) {
			free(excludes.list);
			return (1);
		}

		err = zfs_send_saved(zhp, &flags, STDOUT_FILENO,
		    resume_token);
		free(excludes.list);
		zfs_close(zhp);
		return (err != 0);
	} else if (resume_token != NULL) {
		free(excludes.list);
		return (zfs_send_resume(g_zfs, &flags, STDOUT_FILENO,
		    resume_token));
	}

	if (flags.skipmissing && !flags.replicate) {
		free(excludes.list);
		(void) fprintf(stderr,
		    gettext("skip-missing flag can only be used in "
		    "conjunction with replicate\n"));
		usage(B_FALSE);
	}

	/*
	 * For everything except -R and -I, use the new, cleaner code path.
	 */
	if (!(flags.replicate || flags.doall)) {
		char frombuf[ZFS_MAX_DATASET_NAME_LEN];

		if (fromname != NULL && (strchr(fromname, '#') == NULL &&
		    strchr(fromname, '@') == NULL)) {
			/*
			 * Neither bookmark or snapshot was specified.  Print a
			 * warning, and assume snapshot.
			 */
			(void) fprintf(stderr, "Warning: incremental source "
			    "didn't specify type, assuming snapshot. Use '@' "
			    "or '#' prefix to avoid ambiguity.\n");
			(void) snprintf(frombuf, sizeof (frombuf), "@%s",
			    fromname);
			fromname = frombuf;
		}
		if (fromname != NULL &&
		    (fromname[0] == '#' || fromname[0] == '@')) {
			/*
			 * Incremental source name begins with # or @.
			 * Default to same fs as target.
			 */
			char tmpbuf[ZFS_MAX_DATASET_NAME_LEN];
			(void) strlcpy(tmpbuf, fromname, sizeof (tmpbuf));
			(void) strlcpy(frombuf, argv[0], sizeof (frombuf));
			cp = strchr(frombuf, '@');
			if (cp != NULL)
				*cp = '\0';
			(void) strlcat(frombuf, tmpbuf, sizeof (frombuf));
			fromname = frombuf;
		}

		zhp = zfs_open(g_zfs, argv[0], ZFS_TYPE_DATASET);
		if (zhp == NULL) {
			free(excludes.list);
			return (1);
		}
		err = zfs_send_one(zhp, fromname, STDOUT_FILENO, &flags,
		    redactbook);

		free(excludes.list);
		zfs_close(zhp);
		return (err != 0);
	}

	if (fromname != NULL && strchr(fromname, '#')) {
		(void) fprintf(stderr,
		    gettext("Error: multiple snapshots cannot be "
		    "sent from a bookmark.\n"));
		free(excludes.list);
		return (1);
	}

	if (redactbook != NULL) {
		(void) fprintf(stderr, gettext("Error: multiple snapshots "
		    "cannot be sent redacted.\n"));
		free(excludes.list);
		return (1);
	}

	if ((cp = strchr(argv[0], '@')) == NULL) {
		(void) fprintf(stderr, gettext("Error: "
		    "Unsupported flag with filesystem or bookmark.\n"));
		free(excludes.list);
		return (1);
	}
	*cp = '\0';
	toname = cp + 1;
	zhp = zfs_open(g_zfs, argv[0], ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (zhp == NULL) {
		free(excludes.list);
		return (1);
	}

	/*
	 * If they specified the full path to the snapshot, chop off
	 * everything except the short name of the snapshot, but special
	 * case if they specify the origin.
	 */
	if (fromname && (cp = strchr(fromname, '@')) != NULL) {
		char origin[ZFS_MAX_DATASET_NAME_LEN];
		zprop_source_t src;

		(void) zfs_prop_get(zhp, ZFS_PROP_ORIGIN,
		    origin, sizeof (origin), &src, NULL, 0, B_FALSE);

		if (strcmp(origin, fromname) == 0) {
			fromname = NULL;
			flags.fromorigin = B_TRUE;
		} else {
			*cp = '\0';
			if (cp != fromname && strcmp(argv[0], fromname)) {
				zfs_close(zhp);
				free(excludes.list);
				(void) fprintf(stderr,
				    gettext("incremental source must be "
				    "in same filesystem\n"));
				usage(B_FALSE);
			}
			fromname = cp + 1;
			if (strchr(fromname, '@') || strchr(fromname, '/')) {
				zfs_close(zhp);
				free(excludes.list);
				(void) fprintf(stderr,
				    gettext("invalid incremental source\n"));
				usage(B_FALSE);
			}
		}
	}

	if (flags.replicate && fromname == NULL)
		flags.doall = B_TRUE;

	err = zfs_send(zhp, fromname, toname, &flags, STDOUT_FILENO,
	    excludes.count > 0 ? zfs_do_send_exclude : NULL,
	    &excludes, flags.verbosity >= 3 ? &dbgnv : NULL);

	if (flags.verbosity >= 3 && dbgnv != NULL) {
		/*
		 * dump_nvlist prints to stdout, but that's been
		 * redirected to a file.  Make it print to stderr
		 * instead.
		 */
		(void) dup2(STDERR_FILENO, STDOUT_FILENO);
		dump_nvlist(dbgnv, 0);
		nvlist_free(dbgnv);
	}

	zfs_close(zhp);
	free(excludes.list);
	return (err != 0);
}
