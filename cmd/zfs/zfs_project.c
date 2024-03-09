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
 * Copyright (c) 2017, Intle Corporation. All rights reserved.
 */

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <stddef.h>
#include <libintl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/list.h>
#include <sys/zfs_project.h>

#include "zfs_util.h"
#include "zfs_projectutil.h"

typedef struct zfs_project_item {
	list_node_t	zpi_list;
	char		zpi_name[0];
} zfs_project_item_t;

static void
zfs_project_item_alloc(list_t *head, const char *name)
{
	zfs_project_item_t *zpi;

	zpi = safe_malloc(sizeof (zfs_project_item_t) + strlen(name) + 1);
	strcpy(zpi->zpi_name, name);
	list_insert_tail(head, zpi);
}

static int
zfs_project_sanity_check(const char *name, zfs_project_control_t *zpc,
    struct stat *st)
{
	int ret;

	ret = stat(name, st);
	if (ret) {
		(void) fprintf(stderr, gettext("failed to stat %s: %s\n"),
		    name, strerror(errno));
		return (ret);
	}

	if (!S_ISREG(st->st_mode) && !S_ISDIR(st->st_mode)) {
		(void) fprintf(stderr, gettext("only support project quota on "
		    "regular file or directory\n"));
		return (-1);
	}

	if (!S_ISDIR(st->st_mode)) {
		if (zpc->zpc_dironly) {
			(void) fprintf(stderr, gettext(
			    "'-d' option on non-dir target %s\n"), name);
			return (-1);
		}

		if (zpc->zpc_recursive) {
			(void) fprintf(stderr, gettext(
			    "'-r' option on non-dir target %s\n"), name);
			return (-1);
		}
	}

	return (0);
}

static int
zfs_project_load_projid(const char *name, zfs_project_control_t *zpc)
{
	zfsxattr_t fsx;
	int ret, fd;

	fd = open(name, O_RDONLY | O_NOCTTY);
	if (fd < 0) {
		(void) fprintf(stderr, gettext("failed to open %s: %s\n"),
		    name, strerror(errno));
		return (fd);
	}

	ret = ioctl(fd, ZFS_IOC_FSGETXATTR, &fsx);
	if (ret)
		(void) fprintf(stderr,
		    gettext("failed to get xattr for %s: %s\n"),
		    name, strerror(errno));
	else
		zpc->zpc_expected_projid = fsx.fsx_projid;

	close(fd);
	return (ret);
}

static int
zfs_project_handle_one(const char *name, zfs_project_control_t *zpc)
{
	zfsxattr_t fsx;
	int ret, fd;

	fd = open(name, O_RDONLY | O_NOCTTY);
	if (fd < 0) {
		if (errno == ENOENT && zpc->zpc_ignore_noent)
			return (0);

		(void) fprintf(stderr, gettext("failed to open %s: %s\n"),
		    name, strerror(errno));
		return (fd);
	}

	ret = ioctl(fd, ZFS_IOC_FSGETXATTR, &fsx);
	if (ret) {
		(void) fprintf(stderr,
		    gettext("failed to get xattr for %s: %s\n"),
		    name, strerror(errno));
		goto out;
	}

	switch (zpc->zpc_op) {
	case ZFS_PROJECT_OP_LIST:
		(void) printf("%5u %c %s\n", fsx.fsx_projid,
		    (fsx.fsx_xflags & ZFS_PROJINHERIT_FL) ? 'P' : '-', name);
		goto out;
	case ZFS_PROJECT_OP_CHECK:
		if (fsx.fsx_projid == zpc->zpc_expected_projid &&
		    fsx.fsx_xflags & ZFS_PROJINHERIT_FL)
			goto out;

		if (!zpc->zpc_newline) {
			char c = '\0';

			(void) printf("%s%c", name, c);
			goto out;
		}

		if (fsx.fsx_projid != zpc->zpc_expected_projid)
			(void) printf("%s - project ID is not set properly "
			    "(%u/%u)\n", name, fsx.fsx_projid,
			    (uint32_t)zpc->zpc_expected_projid);

		if (!(fsx.fsx_xflags & ZFS_PROJINHERIT_FL))
			(void) printf("%s - project inherit flag is not set\n",
			    name);

		goto out;
	case ZFS_PROJECT_OP_CLEAR:
		if (!(fsx.fsx_xflags & ZFS_PROJINHERIT_FL) &&
		    (zpc->zpc_keep_projid ||
		    fsx.fsx_projid == ZFS_DEFAULT_PROJID))
			goto out;

		fsx.fsx_xflags &= ~ZFS_PROJINHERIT_FL;
		if (!zpc->zpc_keep_projid)
			fsx.fsx_projid = ZFS_DEFAULT_PROJID;
		break;
	case ZFS_PROJECT_OP_SET:
		if (fsx.fsx_projid == zpc->zpc_expected_projid &&
		    (!zpc->zpc_set_flag || fsx.fsx_xflags & ZFS_PROJINHERIT_FL))
			goto out;

		fsx.fsx_projid = zpc->zpc_expected_projid;
		if (zpc->zpc_set_flag)
			fsx.fsx_xflags |= ZFS_PROJINHERIT_FL;
		break;
	default:
		ASSERT(0);
		break;
	}

	ret = ioctl(fd, ZFS_IOC_FSSETXATTR, &fsx);
	if (ret)
		(void) fprintf(stderr,
		    gettext("failed to set xattr for %s: %s\n"),
		    name, strerror(errno));

out:
	close(fd);
	return (ret);
}

static int
zfs_project_handle_dir(const char *name, zfs_project_control_t *zpc,
    list_t *head)
{
	struct dirent *ent;
	DIR *dir;
	int ret = 0;

	dir = opendir(name);
	if (dir == NULL) {
		if (errno == ENOENT && zpc->zpc_ignore_noent)
			return (0);

		ret = -errno;
		(void) fprintf(stderr, gettext("failed to opendir %s: %s\n"),
		    name, strerror(errno));
		return (ret);
	}

	/* Non-top item, ignore the case of being removed or renamed by race. */
	zpc->zpc_ignore_noent = B_TRUE;
	errno = 0;
	while (!ret && (ent = readdir(dir)) != NULL) {
		char *fullname;

		/* skip "." and ".." */
		if (strcmp(ent->d_name, ".") == 0 ||
		    strcmp(ent->d_name, "..") == 0)
			continue;

		if (strlen(ent->d_name) + strlen(name) + 1 >= PATH_MAX) {
			errno = ENAMETOOLONG;
			break;
		}

		if (asprintf(&fullname, "%s/%s", name, ent->d_name) == -1) {
			errno = ENOMEM;
			break;
		}

		ret = zfs_project_handle_one(fullname, zpc);
		if (!ret && zpc->zpc_recursive && ent->d_type == DT_DIR)
			zfs_project_item_alloc(head, fullname);

		free(fullname);
	}

	if (errno && !ret) {
		ret = -errno;
		(void) fprintf(stderr, gettext("failed to readdir %s: %s\n"),
		    name, strerror(errno));
	}

	closedir(dir);
	return (ret);
}

int
zfs_project_handle(const char *name, zfs_project_control_t *zpc)
{
	zfs_project_item_t *zpi;
	struct stat st;
	list_t head;
	int ret;

	ret = zfs_project_sanity_check(name, zpc, &st);
	if (ret)
		return (ret);

	if ((zpc->zpc_op == ZFS_PROJECT_OP_SET ||
	    zpc->zpc_op == ZFS_PROJECT_OP_CHECK) &&
	    zpc->zpc_expected_projid == ZFS_INVALID_PROJID) {
		ret = zfs_project_load_projid(name, zpc);
		if (ret)
			return (ret);
	}

	zpc->zpc_ignore_noent = B_FALSE;
	ret = zfs_project_handle_one(name, zpc);
	if (ret || !S_ISDIR(st.st_mode) || zpc->zpc_dironly ||
	    (!zpc->zpc_recursive &&
	    zpc->zpc_op != ZFS_PROJECT_OP_LIST &&
	    zpc->zpc_op != ZFS_PROJECT_OP_CHECK))
		return (ret);

	list_create(&head, sizeof (zfs_project_item_t),
	    offsetof(zfs_project_item_t, zpi_list));
	zfs_project_item_alloc(&head, name);
	while ((zpi = list_remove_head(&head)) != NULL) {
		if (!ret)
			ret = zfs_project_handle_dir(zpi->zpi_name, zpc, &head);
		free(zpi);
	}

	return (ret);
}

/*
 * 1) zfs project [-d|-r] <file|directory ...>
 *    List project ID and inherit flag of file(s) or directories.
 *    -d: List the directory itself, not its children.
 *    -r: List subdirectories recursively.
 *
 * 2) zfs project -C [-k] [-r] <file|directory ...>
 *    Clear project inherit flag and/or ID on the file(s) or directories.
 *    -k: Keep the project ID unchanged. If not specified, the project ID
 *	  will be reset as zero.
 *    -r: Clear on subdirectories recursively.
 *
 * 3) zfs project -c [-0] [-d|-r] [-p id] <file|directory ...>
 *    Check project ID and inherit flag on the file(s) or directories,
 *    report the outliers.
 *    -0: Print file name followed by a NUL instead of newline.
 *    -d: Check the directory itself, not its children.
 *    -p: Specify the referenced ID for comparing with the target file(s)
 *	  or directories' project IDs. If not specified, the target (top)
 *	  directory's project ID will be used as the referenced one.
 *    -r: Check subdirectories recursively.
 *
 * 4) zfs project [-p id] [-r] [-s] <file|directory ...>
 *    Set project ID and/or inherit flag on the file(s) or directories.
 *    -p: Set the project ID as the given id.
 *    -r: Set on subdirectories recursively. If not specify "-p" option,
 *	  it will use top-level directory's project ID as the given id,
 *	  then set both project ID and inherit flag on all descendants
 *	  of the top-level directory.
 *    -s: Set project inherit flag.
 */
int
zfs_do_project(int argc, char **argv)
{
	zfs_project_control_t zpc = {
		.zpc_expected_projid = ZFS_INVALID_PROJID,
		.zpc_op = ZFS_PROJECT_OP_DEFAULT,
		.zpc_dironly = B_FALSE,
		.zpc_keep_projid = B_FALSE,
		.zpc_newline = B_TRUE,
		.zpc_recursive = B_FALSE,
		.zpc_set_flag = B_FALSE,
	};
	int ret = 0, c;

	if (argc < 2)
		usage(B_FALSE);

	while ((c = getopt(argc, argv, "0Ccdkp:rs")) != -1) {
		switch (c) {
		case '0':
			zpc.zpc_newline = B_FALSE;
			break;
		case 'C':
			if (zpc.zpc_op != ZFS_PROJECT_OP_DEFAULT) {
				(void) fprintf(stderr, gettext("cannot "
				    "specify '-C' '-c' '-s' together\n"));
				usage(B_FALSE);
			}

			zpc.zpc_op = ZFS_PROJECT_OP_CLEAR;
			break;
		case 'c':
			if (zpc.zpc_op != ZFS_PROJECT_OP_DEFAULT) {
				(void) fprintf(stderr, gettext("cannot "
				    "specify '-C' '-c' '-s' together\n"));
				usage(B_FALSE);
			}

			zpc.zpc_op = ZFS_PROJECT_OP_CHECK;
			break;
		case 'd':
			zpc.zpc_dironly = B_TRUE;
			/* overwrite "-r" option */
			zpc.zpc_recursive = B_FALSE;
			break;
		case 'k':
			zpc.zpc_keep_projid = B_TRUE;
			break;
		case 'p': {
			char *endptr;

			errno = 0;
			zpc.zpc_expected_projid = strtoull(optarg, &endptr, 0);
			if (errno != 0 || *endptr != '\0') {
				(void) fprintf(stderr,
				    gettext("project ID must be less than "
				    "%u\n"), UINT32_MAX);
				usage(B_FALSE);
			}
			if (zpc.zpc_expected_projid >= UINT32_MAX) {
				(void) fprintf(stderr,
				    gettext("invalid project ID\n"));
				usage(B_FALSE);
			}
			break;
		}
		case 'r':
			zpc.zpc_recursive = B_TRUE;
			/* overwrite "-d" option */
			zpc.zpc_dironly = B_FALSE;
			break;
		case 's':
			if (zpc.zpc_op != ZFS_PROJECT_OP_DEFAULT) {
				(void) fprintf(stderr, gettext("cannot "
				    "specify '-C' '-c' '-s' together\n"));
				usage(B_FALSE);
			}

			zpc.zpc_set_flag = B_TRUE;
			zpc.zpc_op = ZFS_PROJECT_OP_SET;
			break;
		default:
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	if (zpc.zpc_op == ZFS_PROJECT_OP_DEFAULT) {
		if (zpc.zpc_expected_projid != ZFS_INVALID_PROJID)
			zpc.zpc_op = ZFS_PROJECT_OP_SET;
		else
			zpc.zpc_op = ZFS_PROJECT_OP_LIST;
	}

	switch (zpc.zpc_op) {
	case ZFS_PROJECT_OP_LIST:
		if (zpc.zpc_keep_projid) {
			(void) fprintf(stderr,
			    gettext("'-k' is only valid together with '-C'\n"));
			usage(B_FALSE);
		}
		if (!zpc.zpc_newline) {
			(void) fprintf(stderr,
			    gettext("'-0' is only valid together with '-c'\n"));
			usage(B_FALSE);
		}
		break;
	case ZFS_PROJECT_OP_CHECK:
		if (zpc.zpc_keep_projid) {
			(void) fprintf(stderr,
			    gettext("'-k' is only valid together with '-C'\n"));
			usage(B_FALSE);
		}
		break;
	case ZFS_PROJECT_OP_CLEAR:
		if (zpc.zpc_dironly) {
			(void) fprintf(stderr,
			    gettext("'-d' is useless together with '-C'\n"));
			usage(B_FALSE);
		}
		if (!zpc.zpc_newline) {
			(void) fprintf(stderr,
			    gettext("'-0' is only valid together with '-c'\n"));
			usage(B_FALSE);
		}
		if (zpc.zpc_expected_projid != ZFS_INVALID_PROJID) {
			(void) fprintf(stderr,
			    gettext("'-p' is useless together with '-C'\n"));
			usage(B_FALSE);
		}
		break;
	case ZFS_PROJECT_OP_SET:
		if (zpc.zpc_dironly) {
			(void) fprintf(stderr,
			    gettext("'-d' is useless for set project ID and/or "
			    "inherit flag\n"));
			usage(B_FALSE);
		}
		if (zpc.zpc_keep_projid) {
			(void) fprintf(stderr,
			    gettext("'-k' is only valid together with '-C'\n"));
			usage(B_FALSE);
		}
		if (!zpc.zpc_newline) {
			(void) fprintf(stderr,
			    gettext("'-0' is only valid together with '-c'\n"));
			usage(B_FALSE);
		}
		break;
	default:
		ASSERT(0);
		break;
	}

	argv += optind;
	argc -= optind;
	if (argc == 0) {
		(void) fprintf(stderr,
		    gettext("missing file or directory target(s)\n"));
		usage(B_FALSE);
	}

	for (int i = 0; i < argc; i++) {
		int err;

		err = zfs_project_handle(argv[i], &zpc);
		if (err && !ret)
			ret = err;
	}

	return (ret);
}

