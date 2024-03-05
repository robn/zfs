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

uint_t timestamp_fmt = NODATE;

char history_str[HIS_MAX_RECORD_LEN];
boolean_t log_history = B_TRUE;

zfs_type_t current_prop_type = (ZFS_TYPE_POOL | ZFS_TYPE_VDEV);

/*
 * print_cmd_columns - Print custom column titles from -c
 *
 * If the user specified the "zpool status|iostat -c" then print their custom
 * column titles in the header.  For example, print_cmd_columns() would print
 * the "  col1  col2" part of this:
 *
 * $ zpool iostat -vc 'echo col1=val1; echo col2=val2'
 * ...
 *	      capacity     operations     bandwidth
 * pool        alloc   free   read  write   read  write  col1  col2
 * ----------  -----  -----  -----  -----  -----  -----  ----  ----
 * mypool       269K  1008M      0      0    107    946
 *   mirror     269K  1008M      0      0    107    946
 *     sdb         -      -      0      0    102    473  val1  val2
 *     sdc         -      -      0      0      5    473  val1  val2
 * ----------  -----  -----  -----  -----  -----  -----  ----  ----
 */
void
print_cmd_columns(vdev_cmd_data_list_t *vcdl, int use_dashes)
{
	int i, j;
	vdev_cmd_data_t *data = &vcdl->data[0];

	if (vcdl->count == 0 || data == NULL)
		return;

	/*
	 * Each vdev cmd should have the same column names unless the user did
	 * something weird with their cmd.  Just take the column names from the
	 * first vdev and assume it works for all of them.
	 */
	for (i = 0; i < vcdl->uniq_cols_cnt; i++) {
		printf("  ");
		if (use_dashes) {
			for (j = 0; j < vcdl->uniq_cols_width[i]; j++)
				printf("-");
		} else {
			printf_color(ANSI_BOLD, "%*s", vcdl->uniq_cols_width[i],
			    vcdl->uniq_cols[i]);
		}
	}
}

/*
 * Run one of the zpool status/iostat -c scripts with the help (-h) option and
 * print the result.
 *
 * name:	Short name of the script ('iostat').
 * path:	Full path to the script ('/usr/local/etc/zfs/zpool.d/iostat');
 */
static void
print_zpool_script_help(char *name, char *path)
{
	char *argv[] = {path, (char *)"-h", NULL};
	char **lines = NULL;
	int lines_cnt = 0;
	int rc;

	rc = libzfs_run_process_get_stdout_nopath(path, argv, NULL, &lines,
	    &lines_cnt);
	if (rc != 0 || lines == NULL || lines_cnt <= 0) {
		if (lines != NULL)
			libzfs_free_str_array(lines, lines_cnt);
		return;
	}

	for (int i = 0; i < lines_cnt; i++)
		if (!is_blank_str(lines[i]))
			printf("  %-14s  %s\n", name, lines[i]);

	libzfs_free_str_array(lines, lines_cnt);
}

/*
 * Go though the zpool status/iostat -c scripts in the user's path, run their
 * help option (-h), and print out the results.
 */
static void
print_zpool_dir_scripts(char *dirpath)
{
	DIR *dir;
	struct dirent *ent;
	char fullpath[MAXPATHLEN];
	struct stat dir_stat;

	if ((dir = opendir(dirpath)) != NULL) {
		/* print all the files and directories within directory */
		while ((ent = readdir(dir)) != NULL) {
			if (snprintf(fullpath, sizeof (fullpath), "%s/%s",
			    dirpath, ent->d_name) >= sizeof (fullpath)) {
				(void) fprintf(stderr,
				    gettext("internal error: "
				    "ZPOOL_SCRIPTS_PATH too large.\n"));
				exit(1);
			}

			/* Print the scripts */
			if (stat(fullpath, &dir_stat) == 0)
				if (dir_stat.st_mode & S_IXUSR &&
				    S_ISREG(dir_stat.st_mode))
					print_zpool_script_help(ent->d_name,
					    fullpath);
		}
		closedir(dir);
	}
}

/*
 * Print out help text for all zpool status/iostat -c scripts.
 */
void
print_zpool_script_list(const char *subcommand)
{
	char *dir, *sp, *tmp;

	printf(gettext("Available 'zpool %s -c' commands:\n"), subcommand);

	sp = zpool_get_cmd_search_path();
	if (sp == NULL)
		return;

	for (dir = strtok_r(sp, ":", &tmp);
	    dir != NULL;
	    dir = strtok_r(NULL, ":", &tmp))
		print_zpool_dir_scripts(dir);

	free(sp);
}

/*
 * print a pool vdev config for dry runs
 */
void
print_vdev_tree(zpool_handle_t *zhp, const char *name, nvlist_t *nv, int indent,
    const char *match, int name_flags)
{
	nvlist_t **child;
	uint_t c, children;
	char *vname;
	boolean_t printed = B_FALSE;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0) {
		if (name != NULL)
			(void) printf("\t%*s%s\n", indent, "", name);
		return;
	}

	for (c = 0; c < children; c++) {
		uint64_t is_log = B_FALSE, is_hole = B_FALSE;
		const char *class = "";

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_HOLE,
		    &is_hole);

		if (is_hole == B_TRUE) {
			continue;
		}

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);
		if (is_log)
			class = VDEV_ALLOC_BIAS_LOG;
		(void) nvlist_lookup_string(child[c],
		    ZPOOL_CONFIG_ALLOCATION_BIAS, &class);
		if (strcmp(match, class) != 0)
			continue;

		if (!printed && name != NULL) {
			(void) printf("\t%*s%s\n", indent, "", name);
			printed = B_TRUE;
		}
		vname = zpool_vdev_name(g_zfs, zhp, child[c], name_flags);
		print_vdev_tree(zhp, vname, child[c], indent + 2, "",
		    name_flags);
		free(vname);
	}
}

static boolean_t
prop_list_contains_feature(nvlist_t *proplist)
{
	nvpair_t *nvp;
	for (nvp = nvlist_next_nvpair(proplist, NULL); NULL != nvp;
	    nvp = nvlist_next_nvpair(proplist, nvp)) {
		if (zpool_prop_feature(nvpair_name(nvp)))
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Add a property pair (name, string-value) into a property nvlist.
 */
int
add_prop_list(const char *propname, const char *propval, nvlist_t **props,
    boolean_t poolprop)
{
	zpool_prop_t prop = ZPOOL_PROP_INVAL;
	nvlist_t *proplist;
	const char *normnm;
	const char *strval;

	if (*props == NULL &&
	    nvlist_alloc(props, NV_UNIQUE_NAME, 0) != 0) {
		(void) fprintf(stderr,
		    gettext("internal error: out of memory\n"));
		return (1);
	}

	proplist = *props;

	if (poolprop) {
		const char *vname = zpool_prop_to_name(ZPOOL_PROP_VERSION);
		const char *cname =
		    zpool_prop_to_name(ZPOOL_PROP_COMPATIBILITY);

		if ((prop = zpool_name_to_prop(propname)) == ZPOOL_PROP_INVAL &&
		    (!zpool_prop_feature(propname) &&
		    !zpool_prop_vdev(propname))) {
			(void) fprintf(stderr, gettext("property '%s' is "
			    "not a valid pool or vdev property\n"), propname);
			return (2);
		}

		/*
		 * feature@ properties and version should not be specified
		 * at the same time.
		 */
		if ((prop == ZPOOL_PROP_INVAL && zpool_prop_feature(propname) &&
		    nvlist_exists(proplist, vname)) ||
		    (prop == ZPOOL_PROP_VERSION &&
		    prop_list_contains_feature(proplist))) {
			(void) fprintf(stderr, gettext("'feature@' and "
			    "'version' properties cannot be specified "
			    "together\n"));
			return (2);
		}

		/*
		 * if version is specified, only "legacy" compatibility
		 * may be requested
		 */
		if ((prop == ZPOOL_PROP_COMPATIBILITY &&
		    strcmp(propval, ZPOOL_COMPAT_LEGACY) != 0 &&
		    nvlist_exists(proplist, vname)) ||
		    (prop == ZPOOL_PROP_VERSION &&
		    nvlist_exists(proplist, cname) &&
		    strcmp(fnvlist_lookup_string(proplist, cname),
		    ZPOOL_COMPAT_LEGACY) != 0)) {
			(void) fprintf(stderr, gettext("when 'version' is "
			    "specified, the 'compatibility' feature may only "
			    "be set to '" ZPOOL_COMPAT_LEGACY "'\n"));
			return (2);
		}

		if (zpool_prop_feature(propname) || zpool_prop_vdev(propname))
			normnm = propname;
		else
			normnm = zpool_prop_to_name(prop);
	} else {
		zfs_prop_t fsprop = zfs_name_to_prop(propname);

		if (zfs_prop_valid_for_type(fsprop, ZFS_TYPE_FILESYSTEM,
		    B_FALSE)) {
			normnm = zfs_prop_to_name(fsprop);
		} else if (zfs_prop_user(propname) ||
		    zfs_prop_userquota(propname)) {
			normnm = propname;
		} else {
			(void) fprintf(stderr, gettext("property '%s' is "
			    "not a valid filesystem property\n"), propname);
			return (2);
		}
	}

	if (nvlist_lookup_string(proplist, normnm, &strval) == 0 &&
	    prop != ZPOOL_PROP_CACHEFILE) {
		(void) fprintf(stderr, gettext("property '%s' "
		    "specified multiple times\n"), propname);
		return (2);
	}

	if (nvlist_add_string(proplist, normnm, propval) != 0) {
		(void) fprintf(stderr, gettext("internal "
		    "error: out of memory\n"));
		return (1);
	}

	return (0);
}

/*
 * Set a default property pair (name, string-value) in a property nvlist
 */
int
add_prop_list_default(const char *propname, const char *propval,
    nvlist_t **props)
{
	const char *pval;

	if (nvlist_lookup_string(*props, propname, &pval) == 0)
		return (0);

	return (add_prop_list(propname, propval, props, B_TRUE));
}

/*
 * Given a leaf vdev name like 'L5' return its VDEV_CONFIG_PATH like
 * '/dev/disk/by-vdev/L5'.
 */
static const char *
vdev_name_to_path(zpool_handle_t *zhp, char *vdev)
{
	nvlist_t *vdev_nv = zpool_find_vdev(zhp, vdev, NULL, NULL, NULL);
	if (vdev_nv == NULL) {
		return (NULL);
	}
	return (fnvlist_lookup_string(vdev_nv, ZPOOL_CONFIG_PATH));
}

static int
zpool_power_on(zpool_handle_t *zhp, char *vdev)
{
	return (zpool_power(zhp, vdev, B_TRUE));
}

int
zpool_power_on_and_disk_wait(zpool_handle_t *zhp, char *vdev)
{
	int rc;

	rc = zpool_power_on(zhp, vdev);
	if (rc != 0)
		return (rc);

	zpool_disk_wait(vdev_name_to_path(zhp, vdev));

	return (0);
}

int
zpool_power_on_pool_and_wait_for_devices(zpool_handle_t *zhp)
{
	nvlist_t *nv;
	const char *path = NULL;
	int rc;

	/* Power up all the devices first */
	FOR_EACH_REAL_LEAF_VDEV(zhp, nv) {
		path = fnvlist_lookup_string(nv, ZPOOL_CONFIG_PATH);
		if (path != NULL) {
			rc = zpool_power_on(zhp, (char *)path);
			if (rc != 0) {
				return (rc);
			}
		}
	}

	/*
	 * Wait for their devices to show up.  Since we powered them on
	 * at roughly the same time, they should all come online around
	 * the same time.
	 */
	FOR_EACH_REAL_LEAF_VDEV(zhp, nv) {
		path = fnvlist_lookup_string(nv, ZPOOL_CONFIG_PATH);
		zpool_disk_wait(path);
	}

	return (0);
}

int
zpool_power_off(zpool_handle_t *zhp, char *vdev)
{
	return (zpool_power(zhp, vdev, B_FALSE));
}

/*
 * Do zpool_load_compat() and print error message on failure
 */
zpool_compat_status_t
zpool_do_load_compat(const char *compat, boolean_t *list)
{
	char report[1024];

	zpool_compat_status_t ret;

	ret = zpool_load_compat(compat, list, report, 1024);
	switch (ret) {

	case ZPOOL_COMPATIBILITY_OK:
		break;

	case ZPOOL_COMPATIBILITY_NOFILES:
	case ZPOOL_COMPATIBILITY_BADFILE:
	case ZPOOL_COMPATIBILITY_BADTOKEN:
		(void) fprintf(stderr, "Error: %s\n", report);
		break;

	case ZPOOL_COMPATIBILITY_WARNTOKEN:
		(void) fprintf(stderr, "Warning: %s\n", report);
		ret = ZPOOL_COMPATIBILITY_OK;
		break;
	}
	return (ret);
}

/*
 * Return 1 if cb_data->cb_names[0] is this vdev's name, 0 otherwise.
 */
static int
is_vdev_cb(void *zhp_data, nvlist_t *nv, void *cb_data)
{
	uint64_t guid;
	vdev_cbdata_t *cb = cb_data;
	zpool_handle_t *zhp = zhp_data;

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) != 0)
		return (0);

	return (guid == zpool_vdev_path_to_guid(zhp, cb->cb_names[0]));
}

/*
 * Returns 1 if cb_data->cb_names[0] is a vdev name, 0 otherwise.
 */
static int
is_vdev(zpool_handle_t *zhp, void *cb_data)
{
	return (for_each_vdev(zhp, is_vdev_cb, cb_data));
}

/*
 * Check if vdevs are in a pool
 *
 * Return 1 if all argv[] strings are vdev names in pool "pool_name". Otherwise
 * return 0.  If pool_name is NULL, then search all pools.
 */
int
are_vdevs_in_pool(int argc, char **argv, char *pool_name,
    vdev_cbdata_t *cb)
{
	char **tmp_name;
	int ret = 0;
	int i;
	int pool_count = 0;

	if ((argc == 0) || !*argv)
		return (0);

	if (pool_name)
		pool_count = 1;

	/* Temporarily hijack cb_names for a second... */
	tmp_name = cb->cb_names;

	/* Go though our list of prospective vdev names */
	for (i = 0; i < argc; i++) {
		cb->cb_names = argv + i;

		/* Is this name a vdev in our pools? */
		ret = for_each_pool(pool_count, &pool_name, B_TRUE, NULL,
		    ZFS_TYPE_POOL, B_FALSE, is_vdev, cb);
		if (!ret) {
			/* No match */
			break;
		}
	}

	cb->cb_names = tmp_name;

	return (ret);
}

static int
is_pool_cb(zpool_handle_t *zhp, void *data)
{
	char *name = data;
	if (strcmp(name, zpool_get_name(zhp)) == 0)
		return (1);

	return (0);
}

/*
 * Do we have a pool named *name?  If so, return 1, otherwise 0.
 */
int
is_pool(char *name)
{
	return (for_each_pool(0, NULL, B_TRUE, NULL, ZFS_TYPE_POOL, B_FALSE,
	    is_pool_cb, name));
}

/* Are all our argv[] strings pool names?  If so return 1, 0 otherwise. */
int
are_all_pools(int argc, char **argv)
{
	if ((argc == 0) || !*argv)
		return (0);

	while (--argc >= 0)
		if (!is_pool(argv[argc]))
			return (0);

	return (1);
}

/*
 * Helper function to print out vdev/pool names we can't resolve.  Used for an
 * error message.
 */
void
error_list_unresolved_vdevs(int argc, char **argv, char *pool_name,
    vdev_cbdata_t *cb)
{
	int i;
	char *name;
	char *str;
	for (i = 0; i < argc; i++) {
		name = argv[i];

		if (is_pool(name))
			str = gettext("pool");
		else if (are_vdevs_in_pool(1, &name, pool_name, cb))
			str = gettext("vdev in this pool");
		else if (are_vdevs_in_pool(1, &name, NULL, cb))
			str = gettext("vdev in another pool");
		else
			str = gettext("unknown");

		fprintf(stderr, "\t%s (%s)\n", name, str);
	}
}

/*
 * Given a vdev configuration, determine the maximum width needed for the device
 * name column.
 */
int
max_width(zpool_handle_t *zhp, nvlist_t *nv, int depth, int max,
    int name_flags)
{
	static const char *const subtypes[] =
	    {ZPOOL_CONFIG_SPARES, ZPOOL_CONFIG_L2CACHE, ZPOOL_CONFIG_CHILDREN};

	char *name = zpool_vdev_name(g_zfs, zhp, nv, name_flags);
	max = MAX(strlen(name) + depth, max);
	free(name);

	nvlist_t **child;
	uint_t children;
	for (size_t i = 0; i < ARRAY_SIZE(subtypes); ++i)
		if (nvlist_lookup_nvlist_array(nv, subtypes[i],
		    &child, &children) == 0)
			for (uint_t c = 0; c < children; ++c)
				max = MAX(max_width(zhp, child[c], depth + 2,
				    max, name_flags), max);

	return (max);
}

/*
 * Return the color associated with a health string.  This includes returning
 * NULL for no color change.
 */
const char *
health_str_to_color(const char *health)
{
	if (strcmp(health, gettext("FAULTED")) == 0 ||
	    strcmp(health, gettext("SUSPENDED")) == 0 ||
	    strcmp(health, gettext("UNAVAIL")) == 0) {
		return (ANSI_RED);
	}

	if (strcmp(health, gettext("OFFLINE")) == 0 ||
	    strcmp(health, gettext("DEGRADED")) == 0 ||
	    strcmp(health, gettext("REMOVED")) == 0) {
		return (ANSI_YELLOW);
	}

	return (NULL);
}

/*
 * Called for each leaf vdev.  Returns 0 if the vdev is healthy.
 * A vdev is unhealthy if any of the following are true:
 * 1) there are read, write, or checksum errors,
 * 2) its state is not ONLINE, or
 * 3) slow IO reporting was requested (-s) and there are slow IOs.
 */
static int
vdev_health_check_cb(void *hdl_data, nvlist_t *nv, void *data)
{
	status_cbdata_t *cb = data;
	vdev_stat_t *vs;
	uint_t vsc;
	(void) hdl_data;

	if (nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &vsc) != 0)
		return (1);

	if (vs->vs_checksum_errors || vs->vs_read_errors ||
	    vs->vs_write_errors || vs->vs_state != VDEV_STATE_HEALTHY)
		return (1);

	if (cb->cb_print_slow_ios && vs->vs_slow_ios)
		return (1);

	return (0);
}

/* Return 1 if string is NULL, empty, or whitespace; return 0 otherwise. */
boolean_t
is_blank_str(const char *str)
{
	for (; str != NULL && *str != '\0'; ++str)
		if (!isblank(*str))
			return (B_FALSE);
	return (B_TRUE);
}

/* Print command output lines for specific vdev in a specific pool */
void
zpool_print_cmd(vdev_cmd_data_list_t *vcdl, const char *pool, const char *path)
{
	vdev_cmd_data_t *data;
	int i, j;
	const char *val;

	for (i = 0; i < vcdl->count; i++) {
		if ((strcmp(vcdl->data[i].path, path) != 0) ||
		    (strcmp(vcdl->data[i].pool, pool) != 0)) {
			/* Not the vdev we're looking for */
			continue;
		}

		data = &vcdl->data[i];
		/* Print out all the output values for this vdev */
		for (j = 0; j < vcdl->uniq_cols_cnt; j++) {
			val = NULL;
			/* Does this vdev have values for this column? */
			for (int k = 0; k < data->cols_cnt; k++) {
				if (strcmp(data->cols[k],
				    vcdl->uniq_cols[j]) == 0) {
					/* yes it does, record the value */
					val = data->lines[k];
					break;
				}
			}
			/*
			 * Mark empty values with dashes to make output
			 * awk-able.
			 */
			if (val == NULL || is_blank_str(val))
				val = "-";

			printf("%*s", vcdl->uniq_cols_width[j], val);
			if (j < vcdl->uniq_cols_cnt - 1)
				fputs("  ", stdout);
		}

		/* Print out any values that aren't in a column at the end */
		for (j = data->cols_cnt; j < data->lines_cnt; j++) {
			/* Did we have any columns?  If so print a spacer. */
			if (vcdl->uniq_cols_cnt > 0)
				fputs("  ", stdout);

			val = data->lines[j];
			fputs(val ?: "", stdout);
		}
		break;
	}
}

typedef struct spare_cbdata {
	uint64_t	cb_guid;
	zpool_handle_t	*cb_zhp;
} spare_cbdata_t;

static boolean_t
find_vdev(nvlist_t *nv, uint64_t search)
{
	uint64_t guid;
	nvlist_t **child;
	uint_t c, children;

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID, &guid) == 0 &&
	    search == guid)
		return (B_TRUE);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0) {
		for (c = 0; c < children; c++)
			if (find_vdev(child[c], search))
				return (B_TRUE);
	}

	return (B_FALSE);
}

static int
find_spare(zpool_handle_t *zhp, void *data)
{
	spare_cbdata_t *cbp = data;
	nvlist_t *config, *nvroot;

	config = zpool_get_config(zhp, NULL);
	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);

	if (find_vdev(nvroot, cbp->cb_guid)) {
		cbp->cb_zhp = zhp;
		return (1);
	}

	zpool_close(zhp);
	return (0);
}

/*
 * Print vdev initialization status for leaves
 */
static void
print_status_initialize(vdev_stat_t *vs, boolean_t verbose)
{
	if (verbose) {
		if ((vs->vs_initialize_state == VDEV_INITIALIZE_ACTIVE ||
		    vs->vs_initialize_state == VDEV_INITIALIZE_SUSPENDED ||
		    vs->vs_initialize_state == VDEV_INITIALIZE_COMPLETE) &&
		    !vs->vs_scan_removing) {
			char zbuf[1024];
			char tbuf[256];
			struct tm zaction_ts;

			time_t t = vs->vs_initialize_action_time;
			int initialize_pct = 100;
			if (vs->vs_initialize_state !=
			    VDEV_INITIALIZE_COMPLETE) {
				initialize_pct = (vs->vs_initialize_bytes_done *
				    100 / (vs->vs_initialize_bytes_est + 1));
			}

			(void) localtime_r(&t, &zaction_ts);
			(void) strftime(tbuf, sizeof (tbuf), "%c", &zaction_ts);

			switch (vs->vs_initialize_state) {
			case VDEV_INITIALIZE_SUSPENDED:
				(void) snprintf(zbuf, sizeof (zbuf), ", %s %s",
				    gettext("suspended, started at"), tbuf);
				break;
			case VDEV_INITIALIZE_ACTIVE:
				(void) snprintf(zbuf, sizeof (zbuf), ", %s %s",
				    gettext("started at"), tbuf);
				break;
			case VDEV_INITIALIZE_COMPLETE:
				(void) snprintf(zbuf, sizeof (zbuf), ", %s %s",
				    gettext("completed at"), tbuf);
				break;
			}

			(void) printf(gettext("  (%d%% initialized%s)"),
			    initialize_pct, zbuf);
		} else {
			(void) printf(gettext("  (uninitialized)"));
		}
	} else if (vs->vs_initialize_state == VDEV_INITIALIZE_ACTIVE) {
		(void) printf(gettext("  (initializing)"));
	}
}

/*
 * Print vdev TRIM status for leaves
 */
static void
print_status_trim(vdev_stat_t *vs, boolean_t verbose)
{
	if (verbose) {
		if ((vs->vs_trim_state == VDEV_TRIM_ACTIVE ||
		    vs->vs_trim_state == VDEV_TRIM_SUSPENDED ||
		    vs->vs_trim_state == VDEV_TRIM_COMPLETE) &&
		    !vs->vs_scan_removing) {
			char zbuf[1024];
			char tbuf[256];
			struct tm zaction_ts;

			time_t t = vs->vs_trim_action_time;
			int trim_pct = 100;
			if (vs->vs_trim_state != VDEV_TRIM_COMPLETE) {
				trim_pct = (vs->vs_trim_bytes_done *
				    100 / (vs->vs_trim_bytes_est + 1));
			}

			(void) localtime_r(&t, &zaction_ts);
			(void) strftime(tbuf, sizeof (tbuf), "%c", &zaction_ts);

			switch (vs->vs_trim_state) {
			case VDEV_TRIM_SUSPENDED:
				(void) snprintf(zbuf, sizeof (zbuf), ", %s %s",
				    gettext("suspended, started at"), tbuf);
				break;
			case VDEV_TRIM_ACTIVE:
				(void) snprintf(zbuf, sizeof (zbuf), ", %s %s",
				    gettext("started at"), tbuf);
				break;
			case VDEV_TRIM_COMPLETE:
				(void) snprintf(zbuf, sizeof (zbuf), ", %s %s",
				    gettext("completed at"), tbuf);
				break;
			}

			(void) printf(gettext("  (%d%% trimmed%s)"),
			    trim_pct, zbuf);
		} else if (vs->vs_trim_notsup) {
			(void) printf(gettext("  (trim unsupported)"));
		} else {
			(void) printf(gettext("  (untrimmed)"));
		}
	} else if (vs->vs_trim_state == VDEV_TRIM_ACTIVE) {
		(void) printf(gettext("  (trimming)"));
	}
}

/*
 * Print out configuration state as requested by status_callback.
 */
void
print_status_config(zpool_handle_t *zhp, status_cbdata_t *cb, const char *name,
    nvlist_t *nv, int depth, boolean_t isspare, vdev_rebuild_stat_t *vrs)
{
	nvlist_t **child, *root;
	uint_t c, i, vsc, children;
	pool_scan_stat_t *ps = NULL;
	vdev_stat_t *vs;
	char rbuf[6], wbuf[6], cbuf[6];
	char *vname;
	uint64_t notpresent;
	spare_cbdata_t spare_cb;
	const char *state;
	const char *type;
	const char *path = NULL;
	const char *rcolor = NULL, *wcolor = NULL, *ccolor = NULL,
	    *scolor = NULL;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		children = 0;

	verify(nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &vsc) == 0);

	verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) == 0);

	if (strcmp(type, VDEV_TYPE_INDIRECT) == 0)
		return;

	state = zpool_state_to_name(vs->vs_state, vs->vs_aux);

	if (isspare) {
		/*
		 * For hot spares, we use the terms 'INUSE' and 'AVAILABLE' for
		 * online drives.
		 */
		if (vs->vs_aux == VDEV_AUX_SPARED)
			state = gettext("INUSE");
		else if (vs->vs_state == VDEV_STATE_HEALTHY)
			state = gettext("AVAIL");
	}

	/*
	 * If '-e' is specified then top-level vdevs and their children
	 * can be pruned if all of their leaves are healthy.
	 */
	if (cb->cb_print_unhealthy && depth > 0 &&
	    for_each_vdev_in_nvlist(nv, vdev_health_check_cb, cb) == 0) {
		return;
	}

	printf_color(health_str_to_color(state),
	    "\t%*s%-*s  %-8s", depth, "", cb->cb_namewidth - depth,
	    name, state);

	if (!isspare) {
		if (vs->vs_read_errors)
			rcolor = ANSI_RED;

		if (vs->vs_write_errors)
			wcolor = ANSI_RED;

		if (vs->vs_checksum_errors)
			ccolor = ANSI_RED;

		if (vs->vs_slow_ios)
			scolor = ANSI_BLUE;

		if (cb->cb_literal) {
			fputc(' ', stdout);
			printf_color(rcolor, "%5llu",
			    (u_longlong_t)vs->vs_read_errors);
			fputc(' ', stdout);
			printf_color(wcolor, "%5llu",
			    (u_longlong_t)vs->vs_write_errors);
			fputc(' ', stdout);
			printf_color(ccolor, "%5llu",
			    (u_longlong_t)vs->vs_checksum_errors);
		} else {
			zfs_nicenum(vs->vs_read_errors, rbuf, sizeof (rbuf));
			zfs_nicenum(vs->vs_write_errors, wbuf, sizeof (wbuf));
			zfs_nicenum(vs->vs_checksum_errors, cbuf,
			    sizeof (cbuf));
			fputc(' ', stdout);
			printf_color(rcolor, "%5s", rbuf);
			fputc(' ', stdout);
			printf_color(wcolor, "%5s", wbuf);
			fputc(' ', stdout);
			printf_color(ccolor, "%5s", cbuf);
		}
		if (cb->cb_print_slow_ios) {
			if (children == 0)  {
				/* Only leafs vdevs have slow IOs */
				zfs_nicenum(vs->vs_slow_ios, rbuf,
				    sizeof (rbuf));
			} else {
				snprintf(rbuf, sizeof (rbuf), "-");
			}

			if (cb->cb_literal)
				printf_color(scolor, " %5llu",
				    (u_longlong_t)vs->vs_slow_ios);
			else
				printf_color(scolor, " %5s", rbuf);
		}
		if (cb->cb_print_power) {
			if (children == 0)  {
				/* Only leaf vdevs have physical slots */
				switch (zpool_power_current_state(zhp, (char *)
				    fnvlist_lookup_string(nv,
				    ZPOOL_CONFIG_PATH))) {
				case 0:
					printf_color(ANSI_RED, " %5s",
					    gettext("off"));
					break;
				case 1:
					printf(" %5s", gettext("on"));
					break;
				default:
					printf(" %5s", "-");
				}
			} else {
				printf(" %5s", "-");
			}
		}
	}

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NOT_PRESENT,
	    &notpresent) == 0) {
		verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) == 0);
		(void) printf("  %s %s", gettext("was"), path);
	} else if (vs->vs_aux != 0) {
		(void) printf("  ");
		color_start(ANSI_RED);
		switch (vs->vs_aux) {
		case VDEV_AUX_OPEN_FAILED:
			(void) printf(gettext("cannot open"));
			break;

		case VDEV_AUX_BAD_GUID_SUM:
			(void) printf(gettext("missing device"));
			break;

		case VDEV_AUX_NO_REPLICAS:
			(void) printf(gettext("insufficient replicas"));
			break;

		case VDEV_AUX_VERSION_NEWER:
			(void) printf(gettext("newer version"));
			break;

		case VDEV_AUX_UNSUP_FEAT:
			(void) printf(gettext("unsupported feature(s)"));
			break;

		case VDEV_AUX_ASHIFT_TOO_BIG:
			(void) printf(gettext("unsupported minimum blocksize"));
			break;

		case VDEV_AUX_SPARED:
			verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID,
			    &spare_cb.cb_guid) == 0);
			if (zpool_iter(g_zfs, find_spare, &spare_cb) == 1) {
				if (strcmp(zpool_get_name(spare_cb.cb_zhp),
				    zpool_get_name(zhp)) == 0)
					(void) printf(gettext("currently in "
					    "use"));
				else
					(void) printf(gettext("in use by "
					    "pool '%s'"),
					    zpool_get_name(spare_cb.cb_zhp));
				zpool_close(spare_cb.cb_zhp);
			} else {
				(void) printf(gettext("currently in use"));
			}
			break;

		case VDEV_AUX_ERR_EXCEEDED:
			if (vs->vs_read_errors + vs->vs_write_errors +
			    vs->vs_checksum_errors == 0 && children == 0 &&
			    vs->vs_slow_ios > 0) {
				(void) printf(gettext("too many slow I/Os"));
			} else {
				(void) printf(gettext("too many errors"));
			}
			break;

		case VDEV_AUX_IO_FAILURE:
			(void) printf(gettext("experienced I/O failures"));
			break;

		case VDEV_AUX_BAD_LOG:
			(void) printf(gettext("bad intent log"));
			break;

		case VDEV_AUX_EXTERNAL:
			(void) printf(gettext("external device fault"));
			break;

		case VDEV_AUX_SPLIT_POOL:
			(void) printf(gettext("split into new pool"));
			break;

		case VDEV_AUX_ACTIVE:
			(void) printf(gettext("currently in use"));
			break;

		case VDEV_AUX_CHILDREN_OFFLINE:
			(void) printf(gettext("all children offline"));
			break;

		case VDEV_AUX_BAD_LABEL:
			(void) printf(gettext("invalid label"));
			break;

		default:
			(void) printf(gettext("corrupted data"));
			break;
		}
		color_end();
	} else if (children == 0 && !isspare &&
	    getenv("ZPOOL_STATUS_NON_NATIVE_ASHIFT_IGNORE") == NULL &&
	    VDEV_STAT_VALID(vs_physical_ashift, vsc) &&
	    vs->vs_configured_ashift < vs->vs_physical_ashift) {
		(void) printf(
		    gettext("  block size: %dB configured, %dB native"),
		    1 << vs->vs_configured_ashift, 1 << vs->vs_physical_ashift);
	}

	if (vs->vs_scan_removing != 0) {
		(void) printf(gettext("  (removing)"));
	} else if (VDEV_STAT_VALID(vs_noalloc, vsc) && vs->vs_noalloc != 0) {
		(void) printf(gettext("  (non-allocating)"));
	}

	/* The root vdev has the scrub/resilver stats */
	root = fnvlist_lookup_nvlist(zpool_get_config(zhp, NULL),
	    ZPOOL_CONFIG_VDEV_TREE);
	(void) nvlist_lookup_uint64_array(root, ZPOOL_CONFIG_SCAN_STATS,
	    (uint64_t **)&ps, &c);

	/*
	 * If you force fault a drive that's resilvering, its scan stats can
	 * get frozen in time, giving the false impression that it's
	 * being resilvered.  That's why we check the state to see if the vdev
	 * is healthy before reporting "resilvering" or "repairing".
	 */
	if (ps != NULL && ps->pss_state == DSS_SCANNING && children == 0 &&
	    vs->vs_state == VDEV_STATE_HEALTHY) {
		if (vs->vs_scan_processed != 0) {
			(void) printf(gettext("  (%s)"),
			    (ps->pss_func == POOL_SCAN_RESILVER) ?
			    "resilvering" : "repairing");
		} else if (vs->vs_resilver_deferred) {
			(void) printf(gettext("  (awaiting resilver)"));
		}
	}

	/* The top-level vdevs have the rebuild stats */
	if (vrs != NULL && vrs->vrs_state == VDEV_REBUILD_ACTIVE &&
	    children == 0 && vs->vs_state == VDEV_STATE_HEALTHY) {
		if (vs->vs_rebuild_processed != 0) {
			(void) printf(gettext("  (resilvering)"));
		}
	}

	if (cb->vcdl != NULL) {
		if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) == 0) {
			printf("  ");
			zpool_print_cmd(cb->vcdl, zpool_get_name(zhp), path);
		}
	}

	/* Display vdev initialization and trim status for leaves. */
	if (children == 0) {
		print_status_initialize(vs, cb->cb_print_vdev_init);
		print_status_trim(vs, cb->cb_print_vdev_trim);
	}

	(void) printf("\n");

	for (c = 0; c < children; c++) {
		uint64_t islog = B_FALSE, ishole = B_FALSE;

		/* Don't print logs or holes here */
		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &islog);
		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_HOLE,
		    &ishole);
		if (islog || ishole)
			continue;
		/* Only print normal classes here */
		if (nvlist_exists(child[c], ZPOOL_CONFIG_ALLOCATION_BIAS))
			continue;

		/* Provide vdev_rebuild_stats to children if available */
		if (vrs == NULL) {
			(void) nvlist_lookup_uint64_array(nv,
			    ZPOOL_CONFIG_REBUILD_STATS,
			    (uint64_t **)&vrs, &i);
		}

		vname = zpool_vdev_name(g_zfs, zhp, child[c],
		    cb->cb_name_flags | VDEV_NAME_TYPE_ID);
		print_status_config(zhp, cb, vname, child[c], depth + 2,
		    isspare, vrs);
		free(vname);
	}
}

/*
 * Print the configuration of an exported pool.  Iterate over all vdevs in the
 * pool, printing out the name and status for each one.
 */
void
print_import_config(status_cbdata_t *cb, const char *name, nvlist_t *nv,
    int depth)
{
	nvlist_t **child;
	uint_t c, children;
	vdev_stat_t *vs;
	const char *type;
	char *vname;

	verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) == 0);
	if (strcmp(type, VDEV_TYPE_MISSING) == 0 ||
	    strcmp(type, VDEV_TYPE_HOLE) == 0)
		return;

	verify(nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);

	(void) printf("\t%*s%-*s", depth, "", cb->cb_namewidth - depth, name);
	(void) printf("  %s", zpool_state_to_name(vs->vs_state, vs->vs_aux));

	if (vs->vs_aux != 0) {
		(void) printf("  ");

		switch (vs->vs_aux) {
		case VDEV_AUX_OPEN_FAILED:
			(void) printf(gettext("cannot open"));
			break;

		case VDEV_AUX_BAD_GUID_SUM:
			(void) printf(gettext("missing device"));
			break;

		case VDEV_AUX_NO_REPLICAS:
			(void) printf(gettext("insufficient replicas"));
			break;

		case VDEV_AUX_VERSION_NEWER:
			(void) printf(gettext("newer version"));
			break;

		case VDEV_AUX_UNSUP_FEAT:
			(void) printf(gettext("unsupported feature(s)"));
			break;

		case VDEV_AUX_ERR_EXCEEDED:
			(void) printf(gettext("too many errors"));
			break;

		case VDEV_AUX_ACTIVE:
			(void) printf(gettext("currently in use"));
			break;

		case VDEV_AUX_CHILDREN_OFFLINE:
			(void) printf(gettext("all children offline"));
			break;

		case VDEV_AUX_BAD_LABEL:
			(void) printf(gettext("invalid label"));
			break;

		default:
			(void) printf(gettext("corrupted data"));
			break;
		}
	}
	(void) printf("\n");

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return;

	for (c = 0; c < children; c++) {
		uint64_t is_log = B_FALSE;

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);
		if (is_log)
			continue;
		if (nvlist_exists(child[c], ZPOOL_CONFIG_ALLOCATION_BIAS))
			continue;

		vname = zpool_vdev_name(g_zfs, NULL, child[c],
		    cb->cb_name_flags | VDEV_NAME_TYPE_ID);
		print_import_config(cb, vname, child[c], depth + 2);
		free(vname);
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
	    &child, &children) == 0) {
		(void) printf(gettext("\tcache\n"));
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, NULL, child[c],
			    cb->cb_name_flags);
			(void) printf("\t  %s\n", vname);
			free(vname);
		}
	}

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
	    &child, &children) == 0) {
		(void) printf(gettext("\tspares\n"));
		for (c = 0; c < children; c++) {
			vname = zpool_vdev_name(g_zfs, NULL, child[c],
			    cb->cb_name_flags);
			(void) printf("\t  %s\n", vname);
			free(vname);
		}
	}
}

/*
 * Print specialized class vdevs.
 *
 * These are recorded as top level vdevs in the main pool child array
 * but with "is_log" set to 1 or an "alloc_bias" string. We use either
 * print_status_config() or print_import_config() to print the top level
 * class vdevs then any of their children (eg mirrored slogs) are printed
 * recursively - which works because only the top level vdev is marked.
 */
void
print_class_vdevs(zpool_handle_t *zhp, status_cbdata_t *cb, nvlist_t *nv,
    const char *class)
{
	uint_t c, children;
	nvlist_t **child;
	boolean_t printed = B_FALSE;

	assert(zhp != NULL || !cb->cb_verbose);

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN, &child,
	    &children) != 0)
		return;

	for (c = 0; c < children; c++) {
		uint64_t is_log = B_FALSE;
		const char *bias = NULL;
		const char *type = NULL;

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);

		if (is_log) {
			bias = (char *)VDEV_ALLOC_CLASS_LOGS;
		} else {
			(void) nvlist_lookup_string(child[c],
			    ZPOOL_CONFIG_ALLOCATION_BIAS, &bias);
			(void) nvlist_lookup_string(child[c],
			    ZPOOL_CONFIG_TYPE, &type);
		}

		if (bias == NULL || strcmp(bias, class) != 0)
			continue;
		if (!is_log && strcmp(type, VDEV_TYPE_INDIRECT) == 0)
			continue;

		if (!printed) {
			(void) printf("\t%s\t\n", gettext(class));
			printed = B_TRUE;
		}

		char *name = zpool_vdev_name(g_zfs, zhp, child[c],
		    cb->cb_name_flags | VDEV_NAME_TYPE_ID);
		if (cb->cb_print_status)
			print_status_config(zhp, cb, name, child[c], 2,
			    B_FALSE, NULL);
		else
			print_import_config(cb, name, child[c], 2);
		free(name);
	}
}

void
zpool_collect_leaves(zpool_handle_t *zhp, nvlist_t *nvroot, nvlist_t *res)
{
	uint_t children = 0;
	nvlist_t **child;
	uint_t i;

	(void) nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &child, &children);

	if (children == 0) {
		char *path = zpool_vdev_name(g_zfs, zhp, nvroot,
		    VDEV_NAME_PATH);

		if (strcmp(path, VDEV_TYPE_INDIRECT) != 0 &&
		    strcmp(path, VDEV_TYPE_HOLE) != 0)
			fnvlist_add_boolean(res, path);

		free(path);
		return;
	}

	for (i = 0; i < children; i++) {
		zpool_collect_leaves(zhp, child[i], res);
	}
}

/*
 * Return the required length of the pool/vdev name column.  The minimum
 * allowed width and output formatting flags must be provided.
 */
int
get_namewidth(zpool_handle_t *zhp, int min_width, int flags, boolean_t verbose)
{
	nvlist_t *config, *nvroot;
	int width = min_width;

	if ((config = zpool_get_config(zhp, NULL)) != NULL) {
		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &nvroot) == 0);
		size_t poolname_len = strlen(zpool_get_name(zhp));
		if (verbose == B_FALSE) {
			width = MAX(poolname_len, min_width);
		} else {
			width = MAX(poolname_len,
			    max_width(zhp, nvroot, 0, min_width, flags));
		}
	}

	return (width);
}

/*
 * Parse the input string, get the 'interval' and 'count' value if there is one.
 */
void
get_interval_count(int *argcp, char **argv, float *iv,
    unsigned long *cnt)
{
	float interval = 0;
	unsigned long count = 0;
	int argc = *argcp;

	/*
	 * Determine if the last argument is an integer or a pool name
	 */
	if (argc > 0 && zfs_isnumber(argv[argc - 1])) {
		char *end;

		errno = 0;
		interval = strtof(argv[argc - 1], &end);

		if (*end == '\0' && errno == 0) {
			if (interval == 0) {
				(void) fprintf(stderr, gettext(
				    "interval cannot be zero\n"));
				zpool_usage(B_FALSE);
			}
			/*
			 * Ignore the last parameter
			 */
			argc--;
		} else {
			/*
			 * If this is not a valid number, just plow on.  The
			 * user will get a more informative error message later
			 * on.
			 */
			interval = 0;
		}
	}

	/*
	 * If the last argument is also an integer, then we have both a count
	 * and an interval.
	 */
	if (argc > 0 && zfs_isnumber(argv[argc - 1])) {
		char *end;

		errno = 0;
		count = interval;
		interval = strtof(argv[argc - 1], &end);

		if (*end == '\0' && errno == 0) {
			if (interval == 0) {
				(void) fprintf(stderr, gettext(
				    "interval cannot be zero\n"));
				zpool_usage(B_FALSE);
			}

			/*
			 * Ignore the last parameter
			 */
			argc--;
		} else {
			interval = 0;
		}
	}

	*iv = interval;
	*cnt = count;
	*argcp = argc;
}

void
get_timestamp_arg(char c)
{
	if (c == 'u')
		timestamp_fmt = UDATE;
	else if (c == 'd')
		timestamp_fmt = DDATE;
	else
		zpool_usage(B_FALSE);
}

/*
 * Terminal height, in rows. Returns -1 if stdout is not connected to a TTY or
 * if we were unable to determine its size.
 */
int
terminal_height(void)
{
	struct winsize win;

	if (isatty(STDOUT_FILENO) == 0)
		return (-1);

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &win) != -1 && win.ws_row > 0)
		return (win.ws_row);

	return (-1);
}

/*
 * Returns B_TRUE if there is an active rebuild in progress.  Otherwise,
 * B_FALSE is returned and 'rebuild_end_time' is set to the end time for
 * the last completed (or cancelled) rebuild.
 */
boolean_t
check_rebuilding(nvlist_t *nvroot, uint64_t *rebuild_end_time)
{
	nvlist_t **child;
	uint_t children;
	boolean_t rebuilding = B_FALSE;
	uint64_t end_time = 0;

	if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		children = 0;

	for (uint_t c = 0; c < children; c++) {
		vdev_rebuild_stat_t *vrs;
		uint_t i;

		if (nvlist_lookup_uint64_array(child[c],
		    ZPOOL_CONFIG_REBUILD_STATS, (uint64_t **)&vrs, &i) == 0) {

			if (vrs->vrs_end_time > end_time)
				end_time = vrs->vrs_end_time;

			if (vrs->vrs_state == VDEV_REBUILD_ACTIVE) {
				rebuilding = B_TRUE;
				end_time = 0;
				break;
			}
		}
	}

	if (rebuild_end_time != NULL)
		*rebuild_end_time = end_time;

	return (rebuilding);
}
