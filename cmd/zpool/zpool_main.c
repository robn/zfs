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

libzfs_handle_t *g_zfs;

/*
 * These libumem hooks provide a reasonable set of defaults for the allocator's
 * debugging facilities.
 */

#ifdef DEBUG
const char *
_umem_debug_init(void)
{
	return ("default,verbose"); /* $UMEM_DEBUG setting */
}

const char *
_umem_logging_init(void)
{
	return ("fail,contents"); /* $UMEM_LOGGING setting */
}
#endif

typedef enum {
	HELP_ADD,
	HELP_ATTACH,
	HELP_CLEAR,
	HELP_CREATE,
	HELP_CHECKPOINT,
	HELP_DESTROY,
	HELP_DETACH,
	HELP_EXPORT,
	HELP_HISTORY,
	HELP_IMPORT,
	HELP_IOSTAT,
	HELP_LABELCLEAR,
	HELP_LIST,
	HELP_OFFLINE,
	HELP_ONLINE,
	HELP_REPLACE,
	HELP_REMOVE,
	HELP_INITIALIZE,
	HELP_SCRUB,
	HELP_RESILVER,
	HELP_TRIM,
	HELP_STATUS,
	HELP_UPGRADE,
	HELP_EVENTS,
	HELP_GET,
	HELP_SET,
	HELP_SPLIT,
	HELP_SYNC,
	HELP_REGUID,
	HELP_REOPEN,
	HELP_VERSION,
	HELP_WAIT
} zpool_help_t;

typedef struct zpool_command {
	const char	*name;
	int		(*func)(int, char **);
	zpool_help_t	usage;
} zpool_command_t;

/*
 * Master command table.  Each ZFS command has a name, associated function, and
 * usage message.  The usage messages need to be internationalized, so we have
 * to have a function to return the usage message based on a command index.
 *
 * These commands are organized according to how they are displayed in the usage
 * message.  An empty command (one with a NULL name) indicates an empty line in
 * the generic usage message.
 */
static int zpool_do_version(int, char **);
static int zpool_do_help(int argc, char **argv);
static zpool_command_t command_table[] = {
	{ "version",	zpool_do_version,	HELP_VERSION		},
	{ NULL },
	{ "create",	zpool_do_create,	HELP_CREATE		},
	{ "destroy",	zpool_do_destroy,	HELP_DESTROY		},
	{ NULL },
	{ "add",	zpool_do_add,		HELP_ADD		},
	{ "remove",	zpool_do_remove,	HELP_REMOVE		},
	{ NULL },
	{ "labelclear",	zpool_do_labelclear,	HELP_LABELCLEAR		},
	{ NULL },
	{ "checkpoint",	zpool_do_checkpoint,	HELP_CHECKPOINT		},
	{ NULL },
	{ "list",	zpool_do_list,		HELP_LIST		},
	{ "iostat",	zpool_do_iostat,	HELP_IOSTAT		},
	{ "status",	zpool_do_status,	HELP_STATUS		},
	{ NULL },
	{ "online",	zpool_do_online,	HELP_ONLINE		},
	{ "offline",	zpool_do_offline,	HELP_OFFLINE		},
	{ "clear",	zpool_do_clear,		HELP_CLEAR		},
	{ "reopen",	zpool_do_reopen,	HELP_REOPEN		},
	{ NULL },
	{ "attach",	zpool_do_attach,	HELP_ATTACH		},
	{ "detach",	zpool_do_detach,	HELP_DETACH		},
	{ "replace",	zpool_do_replace,	HELP_REPLACE		},
	{ "split",	zpool_do_split,		HELP_SPLIT		},
	{ NULL },
	{ "initialize",	zpool_do_initialize,	HELP_INITIALIZE		},
	{ "resilver",	zpool_do_resilver,	HELP_RESILVER		},
	{ "scrub",	zpool_do_scrub,		HELP_SCRUB		},
	{ "trim",	zpool_do_trim,		HELP_TRIM		},
	{ NULL },
	{ "import",	zpool_do_import,	HELP_IMPORT		},
	{ "export",	zpool_do_export,	HELP_EXPORT		},
	{ "upgrade",	zpool_do_upgrade,	HELP_UPGRADE		},
	{ "reguid",	zpool_do_reguid,	HELP_REGUID		},
	{ NULL },
	{ "history",	zpool_do_history,	HELP_HISTORY		},
	{ "events",	zpool_do_events,	HELP_EVENTS		},
	{ NULL },
	{ "get",	zpool_do_get,		HELP_GET		},
	{ "set",	zpool_do_set,		HELP_SET		},
	{ "sync",	zpool_do_sync,		HELP_SYNC		},
	{ NULL },
	{ "wait",	zpool_do_wait,		HELP_WAIT		},
};

#define	NCOMMAND	(ARRAY_SIZE(command_table))

static zpool_command_t *current_command;

static const char *
get_usage(zpool_help_t idx)
{
	switch (idx) {
	case HELP_ADD:
		return (gettext("\tadd [-fgLnP] [-o property=value] "
		    "<pool> <vdev> ...\n"));
	case HELP_ATTACH:
		return (gettext("\tattach [-fsw] [-o property=value] "
		    "<pool> <device> <new-device>\n"));
	case HELP_CLEAR:
		return (gettext("\tclear [[--power]|[-nF]] <pool> [device]\n"));
	case HELP_CREATE:
		return (gettext("\tcreate [-fnd] [-o property=value] ... \n"
		    "\t    [-O file-system-property=value] ... \n"
		    "\t    [-m mountpoint] [-R root] <pool> <vdev> ...\n"));
	case HELP_CHECKPOINT:
		return (gettext("\tcheckpoint [-d [-w]] <pool> ...\n"));
	case HELP_DESTROY:
		return (gettext("\tdestroy [-f] <pool>\n"));
	case HELP_DETACH:
		return (gettext("\tdetach <pool> <device>\n"));
	case HELP_EXPORT:
		return (gettext("\texport [-af] <pool> ...\n"));
	case HELP_HISTORY:
		return (gettext("\thistory [-il] [<pool>] ...\n"));
	case HELP_IMPORT:
		return (gettext("\timport [-d dir] [-D]\n"
		    "\timport [-o mntopts] [-o property=value] ... \n"
		    "\t    [-d dir | -c cachefile] [-D] [-l] [-f] [-m] [-N] "
		    "[-R root] [-F [-n]] -a\n"
		    "\timport [-o mntopts] [-o property=value] ... \n"
		    "\t    [-d dir | -c cachefile] [-D] [-l] [-f] [-m] [-N] "
		    "[-R root] [-F [-n]]\n"
		    "\t    [--rewind-to-checkpoint] <pool | id> [newpool]\n"));
	case HELP_IOSTAT:
		return (gettext("\tiostat [[[-c [script1,script2,...]"
		    "[-lq]]|[-rw]] [-T d | u] [-ghHLpPvy]\n"
		    "\t    [[pool ...]|[pool vdev ...]|[vdev ...]]"
		    " [[-n] interval [count]]\n"));
	case HELP_LABELCLEAR:
		return (gettext("\tlabelclear [-f] <vdev>\n"));
	case HELP_LIST:
		return (gettext("\tlist [-gHLpPv] [-o property[,...]] "
		    "[-T d|u] [pool] ... \n"
		    "\t    [interval [count]]\n"));
	case HELP_OFFLINE:
		return (gettext("\toffline [--power]|[[-f][-t]] <pool> "
		    "<device> ...\n"));
	case HELP_ONLINE:
		return (gettext("\tonline [--power][-e] <pool> <device> "
		    "...\n"));
	case HELP_REPLACE:
		return (gettext("\treplace [-fsw] [-o property=value] "
		    "<pool> <device> [new-device]\n"));
	case HELP_REMOVE:
		return (gettext("\tremove [-npsw] <pool> <device> ...\n"));
	case HELP_REOPEN:
		return (gettext("\treopen [-n] <pool>\n"));
	case HELP_INITIALIZE:
		return (gettext("\tinitialize [-c | -s | -u] [-w] <pool> "
		    "[<device> ...]\n"));
	case HELP_SCRUB:
		return (gettext("\tscrub [-s | -p] [-w] [-e] <pool> ...\n"));
	case HELP_RESILVER:
		return (gettext("\tresilver <pool> ...\n"));
	case HELP_TRIM:
		return (gettext("\ttrim [-dw] [-r <rate>] [-c | -s] <pool> "
		    "[<device> ...]\n"));
	case HELP_STATUS:
		return (gettext("\tstatus [--power] [-c [script1,script2,...]] "
		    "[-igLpPstvxD]  [-T d|u] [pool] ... \n"
		    "\t    [interval [count]]\n"));
	case HELP_UPGRADE:
		return (gettext("\tupgrade\n"
		    "\tupgrade -v\n"
		    "\tupgrade [-V version] <-a | pool ...>\n"));
	case HELP_EVENTS:
		return (gettext("\tevents [-vHf [pool] | -c]\n"));
	case HELP_GET:
		return (gettext("\tget [-Hp] [-o \"all\" | field[,...]] "
		    "<\"all\" | property[,...]> <pool> ...\n"));
	case HELP_SET:
		return (gettext("\tset <property=value> <pool>\n"
		    "\tset <vdev_property=value> <pool> <vdev>\n"));
	case HELP_SPLIT:
		return (gettext("\tsplit [-gLnPl] [-R altroot] [-o mntopts]\n"
		    "\t    [-o property=value] <pool> <newpool> "
		    "[<device> ...]\n"));
	case HELP_REGUID:
		return (gettext("\treguid <pool>\n"));
	case HELP_SYNC:
		return (gettext("\tsync [pool] ...\n"));
	case HELP_VERSION:
		return (gettext("\tversion\n"));
	case HELP_WAIT:
		return (gettext("\twait [-Hp] [-T d|u] [-t <activity>[,...]] "
		    "<pool> [interval]\n"));
	default:
		__builtin_unreachable();
	}
}

/*
 * Callback routine that will print out a pool property value.
 */
static int
print_pool_prop_cb(int prop, void *cb)
{
	FILE *fp = cb;

	(void) fprintf(fp, "\t%-19s  ", zpool_prop_to_name(prop));

	if (zpool_prop_readonly(prop))
		(void) fprintf(fp, "  NO   ");
	else
		(void) fprintf(fp, " YES   ");

	if (zpool_prop_values(prop) == NULL)
		(void) fprintf(fp, "-\n");
	else
		(void) fprintf(fp, "%s\n", zpool_prop_values(prop));

	return (ZPROP_CONT);
}

/*
 * Callback routine that will print out a vdev property value.
 */
static int
print_vdev_prop_cb(int prop, void *cb)
{
	FILE *fp = cb;

	(void) fprintf(fp, "\t%-19s  ", vdev_prop_to_name(prop));

	if (vdev_prop_readonly(prop))
		(void) fprintf(fp, "  NO   ");
	else
		(void) fprintf(fp, " YES   ");

	if (vdev_prop_values(prop) == NULL)
		(void) fprintf(fp, "-\n");
	else
		(void) fprintf(fp, "%s\n", vdev_prop_values(prop));

	return (ZPROP_CONT);
}

/*
 * Display usage message.  If we're inside a command, display only the usage for
 * that command.  Otherwise, iterate over the entire command table and display
 * a complete usage message.
 */
__attribute__((noreturn)) void
zpool_usage(boolean_t requested)
{
	FILE *fp = requested ? stdout : stderr;

	if (current_command == NULL) {
		int i;

		(void) fprintf(fp, gettext("usage: zpool command args ...\n"));
		(void) fprintf(fp,
		    gettext("where 'command' is one of the following:\n\n"));

		for (i = 0; i < NCOMMAND; i++) {
			if (command_table[i].name == NULL)
				(void) fprintf(fp, "\n");
			else
				(void) fprintf(fp, "%s",
				    get_usage(command_table[i].usage));
		}

		(void) fprintf(fp,
		    gettext("\nFor further help on a command or topic, "
		    "run: %s\n"), "zpool help [<topic>]");
	} else {
		(void) fprintf(fp, gettext("usage:\n"));
		(void) fprintf(fp, "%s", get_usage(current_command->usage));
	}

	if (current_command != NULL &&
	    current_prop_type != (ZFS_TYPE_POOL | ZFS_TYPE_VDEV) &&
	    ((strcmp(current_command->name, "set") == 0) ||
	    (strcmp(current_command->name, "get") == 0) ||
	    (strcmp(current_command->name, "list") == 0))) {

		(void) fprintf(fp, "%s",
		    gettext("\nthe following properties are supported:\n"));

		(void) fprintf(fp, "\n\t%-19s  %s   %s\n\n",
		    "PROPERTY", "EDIT", "VALUES");

		/* Iterate over all properties */
		if (current_prop_type == ZFS_TYPE_POOL) {
			(void) zprop_iter(print_pool_prop_cb, fp, B_FALSE,
			    B_TRUE, current_prop_type);

			(void) fprintf(fp, "\t%-19s   ", "feature@...");
			(void) fprintf(fp, "YES   "
			    "disabled | enabled | active\n");

			(void) fprintf(fp, gettext("\nThe feature@ properties "
			    "must be appended with a feature name.\n"
			    "See zpool-features(7).\n"));
		} else if (current_prop_type == ZFS_TYPE_VDEV) {
			(void) zprop_iter(print_vdev_prop_cb, fp, B_FALSE,
			    B_TRUE, current_prop_type);
		}
	}

	/*
	 * See comments at end of main().
	 */
	if (getenv("ZFS_ABORT") != NULL) {
		(void) printf("dumping core by request\n");
		abort();
	}

	exit(requested ? 0 : 2);
}

static int
find_command_idx(const char *command, int *idx)
{
	for (int i = 0; i < NCOMMAND; ++i) {
		if (command_table[i].name == NULL)
			continue;

		if (strcmp(command, command_table[i].name) == 0) {
			*idx = i;
			return (0);
		}
	}
	return (1);
}

/*
 * Display version message
 */
static int
zpool_do_version(int argc, char **argv)
{
	(void) argc, (void) argv;
	return (zfs_version_print() != 0);
}

/* Display documentation */
static int
zpool_do_help(int argc, char **argv)
{
	char page[MAXNAMELEN];
	if (argc < 3 || strcmp(argv[2], "zpool") == 0)
		strcpy(page, "zpool");
	else if (strcmp(argv[2], "concepts") == 0 ||
	    strcmp(argv[2], "props") == 0)
		snprintf(page, sizeof (page), "zpool%s", argv[2]);
	else
		snprintf(page, sizeof (page), "zpool-%s", argv[2]);

	execlp("man", "man", page, NULL);

	fprintf(stderr, "couldn't run man program: %s", strerror(errno));
	return (-1);
}

int
main(int argc, char **argv)
{
	int ret = 0;
	int i = 0;
	char *cmdname;
	char **newargv;

	(void) setlocale(LC_ALL, "");
	(void) setlocale(LC_NUMERIC, "C");
	(void) textdomain(TEXT_DOMAIN);
	srand(time(NULL));

	opterr = 0;

	/*
	 * Make sure the user has specified some command.
	 */
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing command\n"));
		zpool_usage(B_FALSE);
	}

	cmdname = argv[1];

	/*
	 * Special case '-?'
	 */
	if ((strcmp(cmdname, "-?") == 0) || strcmp(cmdname, "--help") == 0)
		zpool_usage(B_TRUE);

	/*
	 * Special case '-V|--version'
	 */
	if ((strcmp(cmdname, "-V") == 0) || (strcmp(cmdname, "--version") == 0))
		return (zpool_do_version(argc, argv));

	/*
	 * Special case 'help'
	 */
	if (strcmp(cmdname, "help") == 0)
		return (zpool_do_help(argc, argv));

	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, "%s\n", libzfs_error_init(errno));
		return (1);
	}

	libzfs_print_on_error(g_zfs, B_TRUE);

	zfs_save_arguments(argc, argv, history_str, sizeof (history_str));

	/*
	 * Many commands modify input strings for string parsing reasons.
	 * We create a copy to protect the original argv.
	 */
	newargv = safe_malloc((argc + 1) * sizeof (newargv[0]));
	for (i = 0; i < argc; i++)
		newargv[i] = strdup(argv[i]);
	newargv[argc] = NULL;

	/*
	 * Run the appropriate command.
	 */
	if (find_command_idx(cmdname, &i) == 0) {
		current_command = &command_table[i];
		ret = command_table[i].func(argc - 1, newargv + 1);
	} else if (strchr(cmdname, '=')) {
		verify(find_command_idx("set", &i) == 0);
		current_command = &command_table[i];
		ret = command_table[i].func(argc, newargv);
	} else if (strcmp(cmdname, "freeze") == 0 && argc == 3) {
		/*
		 * 'freeze' is a vile debugging abomination, so we treat
		 * it as such.
		 */
		zfs_cmd_t zc = {"\0"};

		(void) strlcpy(zc.zc_name, argv[2], sizeof (zc.zc_name));
		ret = zfs_ioctl(g_zfs, ZFS_IOC_POOL_FREEZE, &zc);
		if (ret != 0) {
			(void) fprintf(stderr,
			gettext("failed to freeze pool: %d\n"), errno);
			ret = 1;
		}

		log_history = 0;
	} else {
		(void) fprintf(stderr, gettext("unrecognized "
		    "command '%s'\n"), cmdname);
		zpool_usage(B_FALSE);
		ret = 1;
	}

	for (i = 0; i < argc; i++)
		free(newargv[i]);
	free(newargv);

	if (ret == 0 && log_history)
		(void) zpool_log_history(g_zfs, history_str);

	libzfs_fini(g_zfs);

	/*
	 * The 'ZFS_ABORT' environment variable causes us to dump core on exit
	 * for the purposes of running ::findleaks.
	 */
	if (getenv("ZFS_ABORT") != NULL) {
		(void) printf("dumping core by request\n");
		abort();
	}

	return (ret);
}
