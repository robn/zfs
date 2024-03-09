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

#include "zfs_iter.h"
#include "zfs_util.h"
#include "zfs_comutil.h"
#include "zfs_projectutil.h"

libzfs_handle_t *g_zfs;

static char history_str[HIS_MAX_RECORD_LEN];
static boolean_t log_history = B_TRUE;


static int zfs_do_help(int argc, char **argv);
static int zfs_do_version(int argc, char **argv);

/*
 * Enable a reasonable set of defaults for libumem debugging on DEBUG builds.
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
	HELP_CLONE,
	HELP_CREATE,
	HELP_DESTROY,
	HELP_GET,
	HELP_INHERIT,
	HELP_UPGRADE,
	HELP_LIST,
	HELP_MOUNT,
	HELP_PROMOTE,
	HELP_RECEIVE,
	HELP_RENAME,
	HELP_ROLLBACK,
	HELP_SEND,
	HELP_SET,
	HELP_SHARE,
	HELP_SNAPSHOT,
	HELP_UNMOUNT,
	HELP_UNSHARE,
	HELP_ALLOW,
	HELP_UNALLOW,
	HELP_USERSPACE,
	HELP_GROUPSPACE,
	HELP_PROJECTSPACE,
	HELP_PROJECT,
	HELP_HOLD,
	HELP_HOLDS,
	HELP_RELEASE,
	HELP_DIFF,
	HELP_BOOKMARK,
	HELP_CHANNEL_PROGRAM,
	HELP_LOAD_KEY,
	HELP_UNLOAD_KEY,
	HELP_CHANGE_KEY,
	HELP_VERSION,
	HELP_REDACT,
	HELP_JAIL,
	HELP_UNJAIL,
	HELP_WAIT,
	HELP_ZONE,
	HELP_UNZONE,
} zfs_help_t;

typedef struct zfs_command {
	const char	*name;
	int		(*func)(int argc, char **argv);
	zfs_help_t	usage;
} zfs_command_t;

/*
 * Master command table.  Each ZFS command has a name, associated function, and
 * usage message.  The usage messages need to be internationalized, so we have
 * to have a function to return the usage message based on a command index.
 *
 * These commands are organized according to how they are displayed in the usage
 * message.  An empty command (one with a NULL name) indicates an empty line in
 * the generic usage message.
 */
static zfs_command_t command_table[] = {
	{ "version",	zfs_do_version, 	HELP_VERSION		},
	{ NULL },
	{ "create",	zfs_do_create,		HELP_CREATE		},
	{ "destroy",	zfs_do_destroy,		HELP_DESTROY		},
	{ NULL },
	{ "snapshot",	zfs_do_snapshot,	HELP_SNAPSHOT		},
	{ "rollback",	zfs_do_rollback,	HELP_ROLLBACK		},
	{ "clone",	zfs_do_clone,		HELP_CLONE		},
	{ "promote",	zfs_do_promote,		HELP_PROMOTE		},
	{ "rename",	zfs_do_rename,		HELP_RENAME		},
	{ "bookmark",	zfs_do_bookmark,	HELP_BOOKMARK		},
	{ "program",    zfs_do_channel_program, HELP_CHANNEL_PROGRAM    },
	{ NULL },
	{ "list",	zfs_do_list,		HELP_LIST		},
	{ NULL },
	{ "set",	zfs_do_set,		HELP_SET		},
	{ "get",	zfs_do_get,		HELP_GET		},
	{ "inherit",	zfs_do_inherit,		HELP_INHERIT		},
	{ "upgrade",	zfs_do_upgrade,		HELP_UPGRADE		},
	{ NULL },
	{ "userspace",	zfs_do_userspace,	HELP_USERSPACE		},
	{ "groupspace",	zfs_do_userspace,	HELP_GROUPSPACE		},
	{ "projectspace", zfs_do_userspace,	HELP_PROJECTSPACE	},
	{ NULL },
	{ "project",	zfs_do_project,		HELP_PROJECT		},
	{ NULL },
	{ "mount",	zfs_do_mount,		HELP_MOUNT		},
	{ "unmount",	zfs_do_unmount,		HELP_UNMOUNT		},
	{ "share",	zfs_do_share,		HELP_SHARE		},
	{ "unshare",	zfs_do_unshare,		HELP_UNSHARE		},
	{ NULL },
	{ "send",	zfs_do_send,		HELP_SEND		},
	{ "receive",	zfs_do_receive,		HELP_RECEIVE		},
	{ NULL },
	{ "allow",	zfs_do_allow,		HELP_ALLOW		},
	{ NULL },
	{ "unallow",	zfs_do_unallow,		HELP_UNALLOW		},
	{ NULL },
	{ "hold",	zfs_do_hold,		HELP_HOLD		},
	{ "holds",	zfs_do_holds,		HELP_HOLDS		},
	{ "release",	zfs_do_release,		HELP_RELEASE		},
	{ "diff",	zfs_do_diff,		HELP_DIFF		},
	{ "load-key",	zfs_do_load_key,	HELP_LOAD_KEY		},
	{ "unload-key",	zfs_do_unload_key,	HELP_UNLOAD_KEY		},
	{ "change-key",	zfs_do_change_key,	HELP_CHANGE_KEY		},
	{ "redact",	zfs_do_redact,		HELP_REDACT		},
	{ "wait",	zfs_do_wait,		HELP_WAIT		},

#ifdef __FreeBSD__
	{ "jail",	zfs_do_jail,		HELP_JAIL		},
	{ "unjail",	zfs_do_unjail,		HELP_UNJAIL		},
#endif

#ifdef __linux__
	{ "zone",	zfs_do_zone,		HELP_ZONE		},
	{ "unzone",	zfs_do_unzone,		HELP_UNZONE		},
#endif
};

#define	NCOMMAND	(sizeof (command_table) / sizeof (command_table[0]))

zfs_command_t *current_command;

static const char *
get_usage(zfs_help_t idx)
{
	switch (idx) {
	case HELP_CLONE:
		return (gettext("\tclone [-p] [-o property=value] ... "
		    "<snapshot> <filesystem|volume>\n"));
	case HELP_CREATE:
		return (gettext("\tcreate [-Pnpuv] [-o property=value] ... "
		    "<filesystem>\n"
		    "\tcreate [-Pnpsv] [-b blocksize] [-o property=value] ... "
		    "-V <size> <volume>\n"));
	case HELP_DESTROY:
		return (gettext("\tdestroy [-fnpRrv] <filesystem|volume>\n"
		    "\tdestroy [-dnpRrv] "
		    "<filesystem|volume>@<snap>[%<snap>][,...]\n"
		    "\tdestroy <filesystem|volume>#<bookmark>\n"));
	case HELP_GET:
		return (gettext("\tget [-rHp] [-d max] "
		    "[-o \"all\" | field[,...]]\n"
		    "\t    [-t type[,...]] [-s source[,...]]\n"
		    "\t    <\"all\" | property[,...]> "
		    "[filesystem|volume|snapshot|bookmark] ...\n"));
	case HELP_INHERIT:
		return (gettext("\tinherit [-rS] <property> "
		    "<filesystem|volume|snapshot> ...\n"));
	case HELP_UPGRADE:
		return (gettext("\tupgrade [-v]\n"
		    "\tupgrade [-r] [-V version] <-a | filesystem ...>\n"));
	case HELP_LIST:
		return (gettext("\tlist [-Hp] [-r|-d max] [-o property[,...]] "
		    "[-s property]...\n\t    [-S property]... [-t type[,...]] "
		    "[filesystem|volume|snapshot] ...\n"));
	case HELP_MOUNT:
		return (gettext("\tmount\n"
		    "\tmount [-flvO] [-o opts] <-a|-R filesystem|"
		    "filesystem>\n"));
	case HELP_PROMOTE:
		return (gettext("\tpromote <clone-filesystem>\n"));
	case HELP_RECEIVE:
		return (gettext("\treceive [-vMnsFhu] "
		    "[-o <property>=<value>] ... [-x <property>] ...\n"
		    "\t    <filesystem|volume|snapshot>\n"
		    "\treceive [-vMnsFhu] [-o <property>=<value>] ... "
		    "[-x <property>] ... \n"
		    "\t    [-d | -e] <filesystem>\n"
		    "\treceive -A <filesystem|volume>\n"));
	case HELP_RENAME:
		return (gettext("\trename [-f] <filesystem|volume|snapshot> "
		    "<filesystem|volume|snapshot>\n"
		    "\trename -p [-f] <filesystem|volume> <filesystem|volume>\n"
		    "\trename -u [-f] <filesystem> <filesystem>\n"
		    "\trename -r <snapshot> <snapshot>\n"));
	case HELP_ROLLBACK:
		return (gettext("\trollback [-rRf] <snapshot>\n"));
	case HELP_SEND:
		return (gettext("\tsend [-DLPbcehnpsVvw] "
		    "[-i|-I snapshot]\n"
		    "\t     [-R [-X dataset[,dataset]...]]     <snapshot>\n"
		    "\tsend [-DnVvPLecw] [-i snapshot|bookmark] "
		    "<filesystem|volume|snapshot>\n"
		    "\tsend [-DnPpVvLec] [-i bookmark|snapshot] "
		    "--redact <bookmark> <snapshot>\n"
		    "\tsend [-nVvPe] -t <receive_resume_token>\n"
		    "\tsend [-PnVv] --saved filesystem\n"));
	case HELP_SET:
		return (gettext("\tset [-u] <property=value> ... "
		    "<filesystem|volume|snapshot> ...\n"));
	case HELP_SHARE:
		return (gettext("\tshare [-l] <-a [nfs|smb] | filesystem>\n"));
	case HELP_SNAPSHOT:
		return (gettext("\tsnapshot [-r] [-o property=value] ... "
		    "<filesystem|volume>@<snap> ...\n"));
	case HELP_UNMOUNT:
		return (gettext("\tunmount [-fu] "
		    "<-a | filesystem|mountpoint>\n"));
	case HELP_UNSHARE:
		return (gettext("\tunshare "
		    "<-a [nfs|smb] | filesystem|mountpoint>\n"));
	case HELP_ALLOW:
		return (gettext("\tallow <filesystem|volume>\n"
		    "\tallow [-ldug] "
		    "<\"everyone\"|user|group>[,...] <perm|@setname>[,...]\n"
		    "\t    <filesystem|volume>\n"
		    "\tallow [-ld] -e <perm|@setname>[,...] "
		    "<filesystem|volume>\n"
		    "\tallow -c <perm|@setname>[,...] <filesystem|volume>\n"
		    "\tallow -s @setname <perm|@setname>[,...] "
		    "<filesystem|volume>\n"));
	case HELP_UNALLOW:
		return (gettext("\tunallow [-rldug] "
		    "<\"everyone\"|user|group>[,...]\n"
		    "\t    [<perm|@setname>[,...]] <filesystem|volume>\n"
		    "\tunallow [-rld] -e [<perm|@setname>[,...]] "
		    "<filesystem|volume>\n"
		    "\tunallow [-r] -c [<perm|@setname>[,...]] "
		    "<filesystem|volume>\n"
		    "\tunallow [-r] -s @setname [<perm|@setname>[,...]] "
		    "<filesystem|volume>\n"));
	case HELP_USERSPACE:
		return (gettext("\tuserspace [-Hinp] [-o field[,...]] "
		    "[-s field] ...\n"
		    "\t    [-S field] ... [-t type[,...]] "
		    "<filesystem|snapshot|path>\n"));
	case HELP_GROUPSPACE:
		return (gettext("\tgroupspace [-Hinp] [-o field[,...]] "
		    "[-s field] ...\n"
		    "\t    [-S field] ... [-t type[,...]] "
		    "<filesystem|snapshot|path>\n"));
	case HELP_PROJECTSPACE:
		return (gettext("\tprojectspace [-Hp] [-o field[,...]] "
		    "[-s field] ... \n"
		    "\t    [-S field] ... <filesystem|snapshot|path>\n"));
	case HELP_PROJECT:
		return (gettext("\tproject [-d|-r] <directory|file ...>\n"
		    "\tproject -c [-0] [-d|-r] [-p id] <directory|file ...>\n"
		    "\tproject -C [-k] [-r] <directory ...>\n"
		    "\tproject [-p id] [-r] [-s] <directory ...>\n"));
	case HELP_HOLD:
		return (gettext("\thold [-r] <tag> <snapshot> ...\n"));
	case HELP_HOLDS:
		return (gettext("\tholds [-rHp] <snapshot> ...\n"));
	case HELP_RELEASE:
		return (gettext("\trelease [-r] <tag> <snapshot> ...\n"));
	case HELP_DIFF:
		return (gettext("\tdiff [-FHth] <snapshot> "
		    "[snapshot|filesystem]\n"));
	case HELP_BOOKMARK:
		return (gettext("\tbookmark <snapshot|bookmark> "
		    "<newbookmark>\n"));
	case HELP_CHANNEL_PROGRAM:
		return (gettext("\tprogram [-jn] [-t <instruction limit>] "
		    "[-m <memory limit (b)>]\n"
		    "\t    <pool> <program file> [lua args...]\n"));
	case HELP_LOAD_KEY:
		return (gettext("\tload-key [-rn] [-L <keylocation>] "
		    "<-a | filesystem|volume>\n"));
	case HELP_UNLOAD_KEY:
		return (gettext("\tunload-key [-r] "
		    "<-a | filesystem|volume>\n"));
	case HELP_CHANGE_KEY:
		return (gettext("\tchange-key [-l] [-o keyformat=<value>]\n"
		    "\t    [-o keylocation=<value>] [-o pbkdf2iters=<value>]\n"
		    "\t    <filesystem|volume>\n"
		    "\tchange-key -i [-l] <filesystem|volume>\n"));
	case HELP_VERSION:
		return (gettext("\tversion\n"));
	case HELP_REDACT:
		return (gettext("\tredact <snapshot> <bookmark> "
		    "<redaction_snapshot> ...\n"));
	case HELP_JAIL:
		return (gettext("\tjail <jailid|jailname> <filesystem>\n"));
	case HELP_UNJAIL:
		return (gettext("\tunjail <jailid|jailname> <filesystem>\n"));
	case HELP_WAIT:
		return (gettext("\twait [-t <activity>] <filesystem>\n"));
	case HELP_ZONE:
		return (gettext("\tzone <nsfile> <filesystem>\n"));
	case HELP_UNZONE:
		return (gettext("\tunzone <nsfile> <filesystem>\n"));
	default:
		__builtin_unreachable();
	}
}

void
nomem(void)
{
	(void) fprintf(stderr, gettext("internal error: out of memory\n"));
	exit(1);
}

/*
 * Utility function to guarantee malloc() success.
 */

void *
safe_malloc(size_t size)
{
	void *data;

	if ((data = calloc(1, size)) == NULL)
		nomem();

	return (data);
}

static void *
safe_realloc(void *data, size_t size)
{
	void *newp;
	if ((newp = realloc(data, size)) == NULL) {
		free(data);
		nomem();
	}

	return (newp);
}

static char *
safe_strdup(const char *str)
{
	char *dupstr = strdup(str);

	if (dupstr == NULL)
		nomem();

	return (dupstr);
}

/*
 * Callback routine that will print out information for each of
 * the properties.
 */
static int
usage_prop_cb(int prop, void *cb)
{
	FILE *fp = cb;

	(void) fprintf(fp, "\t%-15s ", zfs_prop_to_name(prop));

	if (zfs_prop_readonly(prop))
		(void) fprintf(fp, " NO    ");
	else
		(void) fprintf(fp, "YES    ");

	if (zfs_prop_inheritable(prop))
		(void) fprintf(fp, "  YES   ");
	else
		(void) fprintf(fp, "   NO   ");

	(void) fprintf(fp, "%s\n", zfs_prop_values(prop) ?: "-");

	return (ZPROP_CONT);
}

/*
 * Display usage message.  If we're inside a command, display only the usage for
 * that command.  Otherwise, iterate over the entire command table and display
 * a complete usage message.
 */
static __attribute__((noreturn)) void
usage(boolean_t requested)
{
	int i;
	boolean_t show_properties = B_FALSE;
	FILE *fp = requested ? stdout : stderr;

	if (current_command == NULL) {

		(void) fprintf(fp, gettext("usage: zfs command args ...\n"));
		(void) fprintf(fp,
		    gettext("where 'command' is one of the following:\n\n"));

		for (i = 0; i < NCOMMAND; i++) {
			if (command_table[i].name == NULL)
				(void) fprintf(fp, "\n");
			else
				(void) fprintf(fp, "%s",
				    get_usage(command_table[i].usage));
		}

		(void) fprintf(fp, gettext("\nEach dataset is of the form: "
		    "pool/[dataset/]*dataset[@name]\n"));
	} else {
		(void) fprintf(fp, gettext("usage:\n"));
		(void) fprintf(fp, "%s", get_usage(current_command->usage));
	}

	if (current_command != NULL &&
	    (strcmp(current_command->name, "set") == 0 ||
	    strcmp(current_command->name, "get") == 0 ||
	    strcmp(current_command->name, "inherit") == 0 ||
	    strcmp(current_command->name, "list") == 0))
		show_properties = B_TRUE;

	if (show_properties) {
		(void) fprintf(fp, "%s",
		    gettext("\nThe following properties are supported:\n"));

		(void) fprintf(fp, "\n\t%-14s %s  %s   %s\n\n",
		    "PROPERTY", "EDIT", "INHERIT", "VALUES");

		/* Iterate over all properties */
		(void) zprop_iter(usage_prop_cb, fp, B_FALSE, B_TRUE,
		    ZFS_TYPE_DATASET);

		(void) fprintf(fp, "\t%-15s ", "userused@...");
		(void) fprintf(fp, " NO       NO   <size>\n");
		(void) fprintf(fp, "\t%-15s ", "groupused@...");
		(void) fprintf(fp, " NO       NO   <size>\n");
		(void) fprintf(fp, "\t%-15s ", "projectused@...");
		(void) fprintf(fp, " NO       NO   <size>\n");
		(void) fprintf(fp, "\t%-15s ", "userobjused@...");
		(void) fprintf(fp, " NO       NO   <size>\n");
		(void) fprintf(fp, "\t%-15s ", "groupobjused@...");
		(void) fprintf(fp, " NO       NO   <size>\n");
		(void) fprintf(fp, "\t%-15s ", "projectobjused@...");
		(void) fprintf(fp, " NO       NO   <size>\n");
		(void) fprintf(fp, "\t%-15s ", "userquota@...");
		(void) fprintf(fp, "YES       NO   <size> | none\n");
		(void) fprintf(fp, "\t%-15s ", "groupquota@...");
		(void) fprintf(fp, "YES       NO   <size> | none\n");
		(void) fprintf(fp, "\t%-15s ", "projectquota@...");
		(void) fprintf(fp, "YES       NO   <size> | none\n");
		(void) fprintf(fp, "\t%-15s ", "userobjquota@...");
		(void) fprintf(fp, "YES       NO   <size> | none\n");
		(void) fprintf(fp, "\t%-15s ", "groupobjquota@...");
		(void) fprintf(fp, "YES       NO   <size> | none\n");
		(void) fprintf(fp, "\t%-15s ", "projectobjquota@...");
		(void) fprintf(fp, "YES       NO   <size> | none\n");
		(void) fprintf(fp, "\t%-15s ", "written@<snap>");
		(void) fprintf(fp, " NO       NO   <size>\n");
		(void) fprintf(fp, "\t%-15s ", "written#<bookmark>");
		(void) fprintf(fp, " NO       NO   <size>\n");

		(void) fprintf(fp, gettext("\nSizes are specified in bytes "
		    "with standard units such as K, M, G, etc.\n"));
		(void) fprintf(fp, "%s", gettext("\nUser-defined properties "
		    "can be specified by using a name containing a colon "
		    "(:).\n"));
		(void) fprintf(fp, gettext("\nThe {user|group|project}"
		    "[obj]{used|quota}@ properties must be appended with\n"
		    "a user|group|project specifier of one of these forms:\n"
		    "    POSIX name      (eg: \"matt\")\n"
		    "    POSIX id        (eg: \"126829\")\n"
		    "    SMB name@domain (eg: \"matt@sun\")\n"
		    "    SMB SID         (eg: \"S-1-234-567-89\")\n"));
	} else {
		(void) fprintf(fp,
		    gettext("\nFor the property list, run: %s\n"),
		    "zfs set|get");
		(void) fprintf(fp,
		    gettext("\nFor the delegated permission list, run: %s\n"),
		    "zfs allow|unallow");
		(void) fprintf(fp,
		    gettext("\nFor further help on a command or topic, "
		    "run: %s\n"), "zfs help [<topic>]");
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

/*
 * Take a property=value argument string and add it to the given nvlist.
 * Modifies the argument inplace.
 */
static boolean_t
parseprop(nvlist_t *props, char *propname)
{
	char *propval;

	if ((propval = strchr(propname, '=')) == NULL) {
		(void) fprintf(stderr, gettext("missing "
		    "'=' for property=value argument\n"));
		return (B_FALSE);
	}
	*propval = '\0';
	propval++;
	if (nvlist_exists(props, propname)) {
		(void) fprintf(stderr, gettext("property '%s' "
		    "specified multiple times\n"), propname);
		return (B_FALSE);
	}
	if (nvlist_add_string(props, propname, propval) != 0)
		nomem();
	return (B_TRUE);
}

/*
 * Take a property name argument and add it to the given nvlist.
 * Modifies the argument inplace.
 */
static boolean_t
parsepropname(nvlist_t *props, char *propname)
{
	if (strchr(propname, '=') != NULL) {
		(void) fprintf(stderr, gettext("invalid character "
		    "'=' in property argument\n"));
		return (B_FALSE);
	}
	if (nvlist_exists(props, propname)) {
		(void) fprintf(stderr, gettext("property '%s' "
		    "specified multiple times\n"), propname);
		return (B_FALSE);
	}
	if (nvlist_add_boolean(props, propname) != 0)
		nomem();
	return (B_TRUE);
}

static int
parse_depth(char *opt, int *flags)
{
	char *tmp;
	int depth;

	depth = (int)strtol(opt, &tmp, 0);
	if (*tmp) {
		(void) fprintf(stderr,
		    gettext("%s is not an integer\n"), optarg);
		usage(B_FALSE);
	}
	if (depth < 0) {
		(void) fprintf(stderr,
		    gettext("Depth can not be negative.\n"));
		usage(B_FALSE);
	}
	*flags |= (ZFS_ITER_DEPTH_LIMIT|ZFS_ITER_RECURSE);
	return (depth);
}

#define	PROGRESS_DELAY 2		/* seconds */

static const char *pt_reverse =
	"\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b";
static time_t pt_begin;
static char *pt_header = NULL;
static boolean_t pt_shown;

static void
start_progress_timer(void)
{
	pt_begin = time(NULL) + PROGRESS_DELAY;
	pt_shown = B_FALSE;
}

static void
set_progress_header(const char *header)
{
	assert(pt_header == NULL);
	pt_header = safe_strdup(header);
	if (pt_shown) {
		(void) printf("%s: ", header);
		(void) fflush(stdout);
	}
}

static void
update_progress(const char *update)
{
	if (!pt_shown && time(NULL) > pt_begin) {
		int len = strlen(update);

		(void) printf("%s: %s%*.*s", pt_header, update, len, len,
		    pt_reverse);
		(void) fflush(stdout);
		pt_shown = B_TRUE;
	} else if (pt_shown) {
		int len = strlen(update);

		(void) printf("%s%*.*s", update, len, len, pt_reverse);
		(void) fflush(stdout);
	}
}

static void
finish_progress(const char *done)
{
	if (pt_shown) {
		(void) puts(done);
		(void) fflush(stdout);
	}
	free(pt_header);
	pt_header = NULL;
}

static int
zfs_mount_and_share(libzfs_handle_t *hdl, const char *dataset, zfs_type_t type)
{
	zfs_handle_t *zhp = NULL;
	int ret = 0;

	zhp = zfs_open(hdl, dataset, type);
	if (zhp == NULL)
		return (1);

	/*
	 * Volumes may neither be mounted or shared.  Potentially in the
	 * future filesystems detected on these volumes could be mounted.
	 */
	if (zfs_get_type(zhp) == ZFS_TYPE_VOLUME) {
		zfs_close(zhp);
		return (0);
	}

	/*
	 * Mount and/or share the new filesystem as appropriate.  We provide a
	 * verbose error message to let the user know that their filesystem was
	 * in fact created, even if we failed to mount or share it.
	 *
	 * If the user doesn't want the dataset automatically mounted, then
	 * skip the mount/share step
	 */
	if (zfs_prop_valid_for_type(ZFS_PROP_CANMOUNT, type, B_FALSE) &&
	    zfs_prop_get_int(zhp, ZFS_PROP_CANMOUNT) == ZFS_CANMOUNT_ON) {
		if (zfs_mount_delegation_check()) {
			(void) fprintf(stderr, gettext("filesystem "
			    "successfully created, but it may only be "
			    "mounted by root\n"));
			ret = 1;
		} else if (zfs_mount(zhp, NULL, 0) != 0) {
			(void) fprintf(stderr, gettext("filesystem "
			    "successfully created, but not mounted\n"));
			ret = 1;
		} else if (zfs_share(zhp, NULL) != 0) {
			(void) fprintf(stderr, gettext("filesystem "
			    "successfully created, but not shared\n"));
			ret = 1;
		}
		zfs_commit_shares(NULL);
	}

	zfs_close(zhp);

	return (ret);
}

static int
find_command_idx(const char *command, int *idx)
{
	int i;

	for (i = 0; i < NCOMMAND; i++) {
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
zfs_do_version(int argc, char **argv)
{
	(void) argc, (void) argv;
	return (zfs_version_print() != 0);
}

/* Display documentation */
static int
zfs_do_help(int argc, char **argv)
{
	char page[MAXNAMELEN];
	if (argc < 3 || strcmp(argv[2], "zfs") == 0)
		strcpy(page, "zfs");
	else if (strcmp(argv[2], "concepts") == 0 ||
	    strcmp(argv[2], "props") == 0)
		snprintf(page, sizeof (page), "zfs%s", argv[2]);
	else
		snprintf(page, sizeof (page), "zfs-%s", argv[2]);

	execlp("man", "man", page, NULL);

	fprintf(stderr, "couldn't run man program: %s", strerror(errno));
	return (-1);
}

int
main(int argc, char **argv)
{
	int ret = 0;
	int i = 0;
	const char *cmdname;
	char **newargv;

	(void) setlocale(LC_ALL, "");
	(void) setlocale(LC_NUMERIC, "C");
	(void) textdomain(TEXT_DOMAIN);

	opterr = 0;

	/*
	 * Make sure the user has specified some command.
	 */
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing command\n"));
		usage(B_FALSE);
	}

	cmdname = argv[1];

	/*
	 * The 'umount' command is an alias for 'unmount'
	 */
	if (strcmp(cmdname, "umount") == 0)
		cmdname = "unmount";

	/*
	 * The 'recv' command is an alias for 'receive'
	 */
	if (strcmp(cmdname, "recv") == 0)
		cmdname = "receive";

	/*
	 * The 'snap' command is an alias for 'snapshot'
	 */
	if (strcmp(cmdname, "snap") == 0)
		cmdname = "snapshot";

	/*
	 * Special case '-?'
	 */
	if ((strcmp(cmdname, "-?") == 0) ||
	    (strcmp(cmdname, "--help") == 0))
		usage(B_TRUE);

	/*
	 * Special case '-V|--version'
	 */
	if ((strcmp(cmdname, "-V") == 0) || (strcmp(cmdname, "--version") == 0))
		return (zfs_do_version(argc, argv));

	/*
	 * Special case 'help'
	 */
	if (strcmp(cmdname, "help") == 0)
		return (zfs_do_help(argc, argv));

	if ((g_zfs = libzfs_init()) == NULL) {
		(void) fprintf(stderr, "%s\n", libzfs_error_init(errno));
		return (1);
	}

	zfs_save_arguments(argc, argv, history_str, sizeof (history_str));

	libzfs_print_on_error(g_zfs, B_TRUE);

	zfs_setproctitle_init(argc, argv, environ);

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
	libzfs_mnttab_cache(g_zfs, B_TRUE);
	if (find_command_idx(cmdname, &i) == 0) {
		current_command = &command_table[i];
		ret = command_table[i].func(argc - 1, newargv + 1);
	} else if (strchr(cmdname, '=') != NULL) {
		verify(find_command_idx("set", &i) == 0);
		current_command = &command_table[i];
		ret = command_table[i].func(argc, newargv);
	} else {
		(void) fprintf(stderr, gettext("unrecognized "
		    "command '%s'\n"), cmdname);
		usage(B_FALSE);
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
