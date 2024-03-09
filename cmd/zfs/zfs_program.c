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
zfs_do_channel_program(int argc, char **argv)
{
	int ret, fd, c;
	size_t progsize, progread;
	nvlist_t *outnvl = NULL;
	uint64_t instrlimit = ZCP_DEFAULT_INSTRLIMIT;
	uint64_t memlimit = ZCP_DEFAULT_MEMLIMIT;
	boolean_t sync_flag = B_TRUE, json_output = B_FALSE;
	zpool_handle_t *zhp;

	/* check options */
	while ((c = getopt(argc, argv, "nt:m:j")) != -1) {
		switch (c) {
		case 't':
		case 'm': {
			uint64_t arg;
			char *endp;

			errno = 0;
			arg = strtoull(optarg, &endp, 0);
			if (errno != 0 || *endp != '\0') {
				(void) fprintf(stderr, gettext(
				    "invalid argument "
				    "'%s': expected integer\n"), optarg);
				goto usage;
			}

			if (c == 't') {
				instrlimit = arg;
			} else {
				ASSERT3U(c, ==, 'm');
				memlimit = arg;
			}
			break;
		}
		case 'n': {
			sync_flag = B_FALSE;
			break;
		}
		case 'j': {
			json_output = B_TRUE;
			break;
		}
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			goto usage;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 2) {
		(void) fprintf(stderr,
		    gettext("invalid number of arguments\n"));
		goto usage;
	}

	const char *poolname = argv[0];
	const char *filename = argv[1];
	if (strcmp(filename, "-") == 0) {
		fd = 0;
		filename = "standard input";
	} else if ((fd = open(filename, O_RDONLY)) < 0) {
		(void) fprintf(stderr, gettext("cannot open '%s': %s\n"),
		    filename, strerror(errno));
		return (1);
	}

	if ((zhp = zpool_open(g_zfs, poolname)) == NULL) {
		(void) fprintf(stderr, gettext("cannot open pool '%s'\n"),
		    poolname);
		if (fd != 0)
			(void) close(fd);
		return (1);
	}
	zpool_close(zhp);

	/*
	 * Read in the channel program, expanding the program buffer as
	 * necessary.
	 */
	progread = 0;
	progsize = 1024;
	char *progbuf = safe_malloc(progsize);
	do {
		ret = read(fd, progbuf + progread, progsize - progread);
		progread += ret;
		if (progread == progsize && ret > 0) {
			progsize *= 2;
			progbuf = safe_realloc(progbuf, progsize);
		}
	} while (ret > 0);

	if (fd != 0)
		(void) close(fd);
	if (ret < 0) {
		free(progbuf);
		(void) fprintf(stderr,
		    gettext("cannot read '%s': %s\n"),
		    filename, strerror(errno));
		return (1);
	}
	progbuf[progread] = '\0';

	/*
	 * Any remaining arguments are passed as arguments to the lua script as
	 * a string array:
	 * {
	 *	"argv" -> [ "arg 1", ... "arg n" ],
	 * }
	 */
	nvlist_t *argnvl = fnvlist_alloc();
	fnvlist_add_string_array(argnvl, ZCP_ARG_CLIARGV,
	    (const char **)argv + 2, argc - 2);

	if (sync_flag) {
		ret = lzc_channel_program(poolname, progbuf,
		    instrlimit, memlimit, argnvl, &outnvl);
	} else {
		ret = lzc_channel_program_nosync(poolname, progbuf,
		    instrlimit, memlimit, argnvl, &outnvl);
	}

	if (ret != 0) {
		/*
		 * On error, report the error message handed back by lua if one
		 * exists.  Otherwise, generate an appropriate error message,
		 * falling back on strerror() for an unexpected return code.
		 */
		const char *errstring = NULL;
		const char *msg = gettext("Channel program execution failed");
		uint64_t instructions = 0;
		if (outnvl != NULL && nvlist_exists(outnvl, ZCP_RET_ERROR)) {
			const char *es = NULL;
			(void) nvlist_lookup_string(outnvl,
			    ZCP_RET_ERROR, &es);
			if (es == NULL)
				errstring = strerror(ret);
			else
				errstring = es;
			if (ret == ETIME) {
				(void) nvlist_lookup_uint64(outnvl,
				    ZCP_ARG_INSTRLIMIT, &instructions);
			}
		} else {
			switch (ret) {
			case EINVAL:
				errstring =
				    "Invalid instruction or memory limit.";
				break;
			case ENOMEM:
				errstring = "Return value too large.";
				break;
			case ENOSPC:
				errstring = "Memory limit exhausted.";
				break;
			case ETIME:
				errstring = "Timed out.";
				break;
			case EPERM:
				errstring = "Permission denied. Channel "
				    "programs must be run as root.";
				break;
			default:
				(void) zfs_standard_error(g_zfs, ret, msg);
			}
		}
		if (errstring != NULL)
			(void) fprintf(stderr, "%s:\n%s\n", msg, errstring);

		if (ret == ETIME && instructions != 0)
			(void) fprintf(stderr,
			    gettext("%llu Lua instructions\n"),
			    (u_longlong_t)instructions);
	} else {
		if (json_output) {
			(void) nvlist_print_json(stdout, outnvl);
		} else if (nvlist_empty(outnvl)) {
			(void) fprintf(stdout, gettext("Channel program fully "
			    "executed and did not produce output.\n"));
		} else {
			(void) fprintf(stdout, gettext("Channel program fully "
			    "executed and produced output:\n"));
			dump_nvlist(outnvl, 4);
		}
	}

	free(progbuf);
	fnvlist_free(outnvl);
	fnvlist_free(argnvl);
	return (ret != 0);

usage:
	usage(B_FALSE);
	return (-1);
}
