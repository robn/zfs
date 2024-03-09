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
 * zfs userspace [-Hinp] [-o field[,...]] [-s field [-s field]...]
 *               [-S field [-S field]...] [-t type[,...]]
 *               filesystem | snapshot | path
 * zfs groupspace [-Hinp] [-o field[,...]] [-s field [-s field]...]
 *                [-S field [-S field]...] [-t type[,...]]
 *                filesystem | snapshot | path
 * zfs projectspace [-Hp] [-o field[,...]] [-s field [-s field]...]
 *                [-S field [-S field]...] filesystem | snapshot | path
 *
 *	-H      Scripted mode; elide headers and separate columns by tabs.
 *	-i	Translate SID to POSIX ID.
 *	-n	Print numeric ID instead of user/group name.
 *	-o      Control which fields to display.
 *	-p	Use exact (parsable) numeric output.
 *	-s      Specify sort columns, descending order.
 *	-S      Specify sort columns, ascending order.
 *	-t      Control which object types to display.
 *
 *	Displays space consumed by, and quotas on, each user in the specified
 *	filesystem or snapshot.
 */

/* us_field_types, us_field_hdr and us_field_names should be kept in sync */
enum us_field_types {
	USFIELD_TYPE,
	USFIELD_NAME,
	USFIELD_USED,
	USFIELD_QUOTA,
	USFIELD_OBJUSED,
	USFIELD_OBJQUOTA
};
static const char *const us_field_hdr[] = { "TYPE", "NAME", "USED", "QUOTA",
				    "OBJUSED", "OBJQUOTA" };
static const char *const us_field_names[] = { "type", "name", "used", "quota",
				    "objused", "objquota" };
#define	USFIELD_LAST	(sizeof (us_field_names) / sizeof (char *))

#define	USTYPE_PSX_GRP	(1 << 0)
#define	USTYPE_PSX_USR	(1 << 1)
#define	USTYPE_SMB_GRP	(1 << 2)
#define	USTYPE_SMB_USR	(1 << 3)
#define	USTYPE_PROJ	(1 << 4)
#define	USTYPE_ALL	\
	(USTYPE_PSX_GRP | USTYPE_PSX_USR | USTYPE_SMB_GRP | USTYPE_SMB_USR | \
	    USTYPE_PROJ)

static int us_type_bits[] = {
	USTYPE_PSX_GRP,
	USTYPE_PSX_USR,
	USTYPE_SMB_GRP,
	USTYPE_SMB_USR,
	USTYPE_ALL
};
static const char *const us_type_names[] = { "posixgroup", "posixuser",
	"smbgroup", "smbuser", "all" };

typedef struct us_node {
	nvlist_t	*usn_nvl;
	uu_avl_node_t	usn_avlnode;
	uu_list_node_t	usn_listnode;
} us_node_t;

typedef struct us_cbdata {
	nvlist_t	**cb_nvlp;
	uu_avl_pool_t	*cb_avl_pool;
	uu_avl_t	*cb_avl;
	boolean_t	cb_numname;
	boolean_t	cb_nicenum;
	boolean_t	cb_sid2posix;
	zfs_userquota_prop_t cb_prop;
	zfs_sort_column_t *cb_sortcol;
	size_t		cb_width[USFIELD_LAST];
} us_cbdata_t;

static boolean_t us_populated = B_FALSE;

typedef struct {
	zfs_sort_column_t *si_sortcol;
	boolean_t	si_numname;
} us_sort_info_t;

static int
us_field_index(const char *field)
{
	for (int i = 0; i < USFIELD_LAST; i++) {
		if (strcmp(field, us_field_names[i]) == 0)
			return (i);
	}

	return (-1);
}

static int
us_compare(const void *larg, const void *rarg, void *unused)
{
	const us_node_t *l = larg;
	const us_node_t *r = rarg;
	us_sort_info_t *si = (us_sort_info_t *)unused;
	zfs_sort_column_t *sortcol = si->si_sortcol;
	boolean_t numname = si->si_numname;
	nvlist_t *lnvl = l->usn_nvl;
	nvlist_t *rnvl = r->usn_nvl;
	int rc = 0;
	boolean_t lvb, rvb;

	for (; sortcol != NULL; sortcol = sortcol->sc_next) {
		const char *lvstr = "";
		const char *rvstr = "";
		uint32_t lv32 = 0;
		uint32_t rv32 = 0;
		uint64_t lv64 = 0;
		uint64_t rv64 = 0;
		zfs_prop_t prop = sortcol->sc_prop;
		const char *propname = NULL;
		boolean_t reverse = sortcol->sc_reverse;

		switch (prop) {
		case ZFS_PROP_TYPE:
			propname = "type";
			(void) nvlist_lookup_uint32(lnvl, propname, &lv32);
			(void) nvlist_lookup_uint32(rnvl, propname, &rv32);
			if (rv32 != lv32)
				rc = (rv32 < lv32) ? 1 : -1;
			break;
		case ZFS_PROP_NAME:
			propname = "name";
			if (numname) {
compare_nums:
				(void) nvlist_lookup_uint64(lnvl, propname,
				    &lv64);
				(void) nvlist_lookup_uint64(rnvl, propname,
				    &rv64);
				if (rv64 != lv64)
					rc = (rv64 < lv64) ? 1 : -1;
			} else {
				if ((nvlist_lookup_string(lnvl, propname,
				    &lvstr) == ENOENT) ||
				    (nvlist_lookup_string(rnvl, propname,
				    &rvstr) == ENOENT)) {
					goto compare_nums;
				}
				rc = strcmp(lvstr, rvstr);
			}
			break;
		case ZFS_PROP_USED:
		case ZFS_PROP_QUOTA:
			if (!us_populated)
				break;
			if (prop == ZFS_PROP_USED)
				propname = "used";
			else
				propname = "quota";
			(void) nvlist_lookup_uint64(lnvl, propname, &lv64);
			(void) nvlist_lookup_uint64(rnvl, propname, &rv64);
			if (rv64 != lv64)
				rc = (rv64 < lv64) ? 1 : -1;
			break;

		default:
			break;
		}

		if (rc != 0) {
			if (rc < 0)
				return (reverse ? 1 : -1);
			else
				return (reverse ? -1 : 1);
		}
	}

	/*
	 * If entries still seem to be the same, check if they are of the same
	 * type (smbentity is added only if we are doing SID to POSIX ID
	 * translation where we can have duplicate type/name combinations).
	 */
	if (nvlist_lookup_boolean_value(lnvl, "smbentity", &lvb) == 0 &&
	    nvlist_lookup_boolean_value(rnvl, "smbentity", &rvb) == 0 &&
	    lvb != rvb)
		return (lvb < rvb ? -1 : 1);

	return (0);
}

static boolean_t
zfs_prop_is_user(unsigned p)
{
	return (p == ZFS_PROP_USERUSED || p == ZFS_PROP_USERQUOTA ||
	    p == ZFS_PROP_USEROBJUSED || p == ZFS_PROP_USEROBJQUOTA);
}

static boolean_t
zfs_prop_is_group(unsigned p)
{
	return (p == ZFS_PROP_GROUPUSED || p == ZFS_PROP_GROUPQUOTA ||
	    p == ZFS_PROP_GROUPOBJUSED || p == ZFS_PROP_GROUPOBJQUOTA);
}

static boolean_t
zfs_prop_is_project(unsigned p)
{
	return (p == ZFS_PROP_PROJECTUSED || p == ZFS_PROP_PROJECTQUOTA ||
	    p == ZFS_PROP_PROJECTOBJUSED || p == ZFS_PROP_PROJECTOBJQUOTA);
}

static inline const char *
us_type2str(unsigned field_type)
{
	switch (field_type) {
	case USTYPE_PSX_USR:
		return ("POSIX User");
	case USTYPE_PSX_GRP:
		return ("POSIX Group");
	case USTYPE_SMB_USR:
		return ("SMB User");
	case USTYPE_SMB_GRP:
		return ("SMB Group");
	case USTYPE_PROJ:
		return ("Project");
	default:
		return ("Undefined");
	}
}

static int
userspace_cb(void *arg, const char *domain, uid_t rid, uint64_t space)
{
	us_cbdata_t *cb = (us_cbdata_t *)arg;
	zfs_userquota_prop_t prop = cb->cb_prop;
	char *name = NULL;
	const char *propname;
	char sizebuf[32];
	us_node_t *node;
	uu_avl_pool_t *avl_pool = cb->cb_avl_pool;
	uu_avl_t *avl = cb->cb_avl;
	uu_avl_index_t idx;
	nvlist_t *props;
	us_node_t *n;
	zfs_sort_column_t *sortcol = cb->cb_sortcol;
	unsigned type = 0;
	const char *typestr;
	size_t namelen;
	size_t typelen;
	size_t sizelen;
	int typeidx, nameidx, sizeidx;
	us_sort_info_t sortinfo = { sortcol, cb->cb_numname };
	boolean_t smbentity = B_FALSE;

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0)
		nomem();
	node = safe_malloc(sizeof (us_node_t));
	uu_avl_node_init(node, &node->usn_avlnode, avl_pool);
	node->usn_nvl = props;

	if (domain != NULL && domain[0] != '\0') {
#ifdef HAVE_IDMAP
		/* SMB */
		char sid[MAXNAMELEN + 32];
		uid_t id;
		uint64_t classes;
		int err;
		directory_error_t e;

		smbentity = B_TRUE;

		(void) snprintf(sid, sizeof (sid), "%s-%u", domain, rid);

		if (prop == ZFS_PROP_GROUPUSED || prop == ZFS_PROP_GROUPQUOTA) {
			type = USTYPE_SMB_GRP;
			err = sid_to_id(sid, B_FALSE, &id);
		} else {
			type = USTYPE_SMB_USR;
			err = sid_to_id(sid, B_TRUE, &id);
		}

		if (err == 0) {
			rid = id;
			if (!cb->cb_sid2posix) {
				e = directory_name_from_sid(NULL, sid, &name,
				    &classes);
				if (e != NULL)
					directory_error_free(e);
				if (name == NULL)
					name = sid;
			}
		}
#else
		nvlist_free(props);
		free(node);

		return (-1);
#endif /* HAVE_IDMAP */
	}

	if (cb->cb_sid2posix || domain == NULL || domain[0] == '\0') {
		/* POSIX or -i */
		if (zfs_prop_is_group(prop)) {
			type = USTYPE_PSX_GRP;
			if (!cb->cb_numname) {
				struct group *g;

				if ((g = getgrgid(rid)) != NULL)
					name = g->gr_name;
			}
		} else if (zfs_prop_is_user(prop)) {
			type = USTYPE_PSX_USR;
			if (!cb->cb_numname) {
				struct passwd *p;

				if ((p = getpwuid(rid)) != NULL)
					name = p->pw_name;
			}
		} else {
			type = USTYPE_PROJ;
		}
	}

	/*
	 * Make sure that the type/name combination is unique when doing
	 * SID to POSIX ID translation (hence changing the type from SMB to
	 * POSIX).
	 */
	if (cb->cb_sid2posix &&
	    nvlist_add_boolean_value(props, "smbentity", smbentity) != 0)
		nomem();

	/* Calculate/update width of TYPE field */
	typestr = us_type2str(type);
	typelen = strlen(gettext(typestr));
	typeidx = us_field_index("type");
	if (typelen > cb->cb_width[typeidx])
		cb->cb_width[typeidx] = typelen;
	if (nvlist_add_uint32(props, "type", type) != 0)
		nomem();

	/* Calculate/update width of NAME field */
	if ((cb->cb_numname && cb->cb_sid2posix) || name == NULL) {
		if (nvlist_add_uint64(props, "name", rid) != 0)
			nomem();
		namelen = snprintf(NULL, 0, "%u", rid);
	} else {
		if (nvlist_add_string(props, "name", name) != 0)
			nomem();
		namelen = strlen(name);
	}
	nameidx = us_field_index("name");
	if (nameidx >= 0 && namelen > cb->cb_width[nameidx])
		cb->cb_width[nameidx] = namelen;

	/*
	 * Check if this type/name combination is in the list and update it;
	 * otherwise add new node to the list.
	 */
	if ((n = uu_avl_find(avl, node, &sortinfo, &idx)) == NULL) {
		uu_avl_insert(avl, node, idx);
	} else {
		nvlist_free(props);
		free(node);
		node = n;
		props = node->usn_nvl;
	}

	/* Calculate/update width of USED/QUOTA fields */
	if (cb->cb_nicenum) {
		if (prop == ZFS_PROP_USERUSED || prop == ZFS_PROP_GROUPUSED ||
		    prop == ZFS_PROP_USERQUOTA || prop == ZFS_PROP_GROUPQUOTA ||
		    prop == ZFS_PROP_PROJECTUSED ||
		    prop == ZFS_PROP_PROJECTQUOTA) {
			zfs_nicebytes(space, sizebuf, sizeof (sizebuf));
		} else {
			zfs_nicenum(space, sizebuf, sizeof (sizebuf));
		}
	} else {
		(void) snprintf(sizebuf, sizeof (sizebuf), "%llu",
		    (u_longlong_t)space);
	}
	sizelen = strlen(sizebuf);
	if (prop == ZFS_PROP_USERUSED || prop == ZFS_PROP_GROUPUSED ||
	    prop == ZFS_PROP_PROJECTUSED) {
		propname = "used";
		if (!nvlist_exists(props, "quota"))
			(void) nvlist_add_uint64(props, "quota", 0);
	} else if (prop == ZFS_PROP_USERQUOTA || prop == ZFS_PROP_GROUPQUOTA ||
	    prop == ZFS_PROP_PROJECTQUOTA) {
		propname = "quota";
		if (!nvlist_exists(props, "used"))
			(void) nvlist_add_uint64(props, "used", 0);
	} else if (prop == ZFS_PROP_USEROBJUSED ||
	    prop == ZFS_PROP_GROUPOBJUSED || prop == ZFS_PROP_PROJECTOBJUSED) {
		propname = "objused";
		if (!nvlist_exists(props, "objquota"))
			(void) nvlist_add_uint64(props, "objquota", 0);
	} else if (prop == ZFS_PROP_USEROBJQUOTA ||
	    prop == ZFS_PROP_GROUPOBJQUOTA ||
	    prop == ZFS_PROP_PROJECTOBJQUOTA) {
		propname = "objquota";
		if (!nvlist_exists(props, "objused"))
			(void) nvlist_add_uint64(props, "objused", 0);
	} else {
		return (-1);
	}
	sizeidx = us_field_index(propname);
	if (sizeidx >= 0 && sizelen > cb->cb_width[sizeidx])
		cb->cb_width[sizeidx] = sizelen;

	if (nvlist_add_uint64(props, propname, space) != 0)
		nomem();

	return (0);
}

static void
print_us_node(boolean_t scripted, boolean_t parsable, int *fields, int types,
    size_t *width, us_node_t *node)
{
	nvlist_t *nvl = node->usn_nvl;
	char valstr[MAXNAMELEN];
	boolean_t first = B_TRUE;
	int cfield = 0;
	int field;
	uint32_t ustype;

	/* Check type */
	(void) nvlist_lookup_uint32(nvl, "type", &ustype);
	if (!(ustype & types))
		return;

	while ((field = fields[cfield]) != USFIELD_LAST) {
		nvpair_t *nvp = NULL;
		data_type_t type;
		uint32_t val32 = -1;
		uint64_t val64 = -1;
		const char *strval = "-";

		while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL)
			if (strcmp(nvpair_name(nvp),
			    us_field_names[field]) == 0)
				break;

		type = nvp == NULL ? DATA_TYPE_UNKNOWN : nvpair_type(nvp);
		switch (type) {
		case DATA_TYPE_UINT32:
			val32 = fnvpair_value_uint32(nvp);
			break;
		case DATA_TYPE_UINT64:
			val64 = fnvpair_value_uint64(nvp);
			break;
		case DATA_TYPE_STRING:
			strval = fnvpair_value_string(nvp);
			break;
		case DATA_TYPE_UNKNOWN:
			break;
		default:
			(void) fprintf(stderr, "invalid data type\n");
		}

		switch (field) {
		case USFIELD_TYPE:
			if (type == DATA_TYPE_UINT32)
				strval = us_type2str(val32);
			break;
		case USFIELD_NAME:
			if (type == DATA_TYPE_UINT64) {
				(void) sprintf(valstr, "%llu",
				    (u_longlong_t)val64);
				strval = valstr;
			}
			break;
		case USFIELD_USED:
		case USFIELD_QUOTA:
			if (type == DATA_TYPE_UINT64) {
				if (parsable) {
					(void) sprintf(valstr, "%llu",
					    (u_longlong_t)val64);
					strval = valstr;
				} else if (field == USFIELD_QUOTA &&
				    val64 == 0) {
					strval = "none";
				} else {
					zfs_nicebytes(val64, valstr,
					    sizeof (valstr));
					strval = valstr;
				}
			}
			break;
		case USFIELD_OBJUSED:
		case USFIELD_OBJQUOTA:
			if (type == DATA_TYPE_UINT64) {
				if (parsable) {
					(void) sprintf(valstr, "%llu",
					    (u_longlong_t)val64);
					strval = valstr;
				} else if (field == USFIELD_OBJQUOTA &&
				    val64 == 0) {
					strval = "none";
				} else {
					zfs_nicenum(val64, valstr,
					    sizeof (valstr));
					strval = valstr;
				}
			}
			break;
		}

		if (!first) {
			if (scripted)
				(void) putchar('\t');
			else
				(void) fputs("  ", stdout);
		}
		if (scripted)
			(void) fputs(strval, stdout);
		else if (field == USFIELD_TYPE || field == USFIELD_NAME)
			(void) printf("%-*s", (int)width[field], strval);
		else
			(void) printf("%*s", (int)width[field], strval);

		first = B_FALSE;
		cfield++;
	}

	(void) putchar('\n');
}

static void
print_us(boolean_t scripted, boolean_t parsable, int *fields, int types,
    size_t *width, boolean_t rmnode, uu_avl_t *avl)
{
	us_node_t *node;
	const char *col;
	int cfield = 0;
	int field;

	if (!scripted) {
		boolean_t first = B_TRUE;

		while ((field = fields[cfield]) != USFIELD_LAST) {
			col = gettext(us_field_hdr[field]);
			if (field == USFIELD_TYPE || field == USFIELD_NAME) {
				(void) printf(first ? "%-*s" : "  %-*s",
				    (int)width[field], col);
			} else {
				(void) printf(first ? "%*s" : "  %*s",
				    (int)width[field], col);
			}
			first = B_FALSE;
			cfield++;
		}
		(void) printf("\n");
	}

	for (node = uu_avl_first(avl); node; node = uu_avl_next(avl, node)) {
		print_us_node(scripted, parsable, fields, types, width, node);
		if (rmnode)
			nvlist_free(node->usn_nvl);
	}
}

int
zfs_do_userspace(int argc, char **argv)
{
	zfs_handle_t *zhp;
	zfs_userquota_prop_t p;
	uu_avl_pool_t *avl_pool;
	uu_avl_t *avl_tree;
	uu_avl_walk_t *walk;
	char *delim;
	char deffields[] = "type,name,used,quota,objused,objquota";
	char *ofield = NULL;
	char *tfield = NULL;
	int cfield = 0;
	int fields[256];
	int i;
	boolean_t scripted = B_FALSE;
	boolean_t prtnum = B_FALSE;
	boolean_t parsable = B_FALSE;
	boolean_t sid2posix = B_FALSE;
	int ret = 0;
	int c;
	zfs_sort_column_t *sortcol = NULL;
	int types = USTYPE_PSX_USR | USTYPE_SMB_USR;
	us_cbdata_t cb;
	us_node_t *node;
	us_node_t *rmnode;
	uu_list_pool_t *listpool;
	uu_list_t *list;
	uu_avl_index_t idx = 0;
	uu_list_index_t idx2 = 0;

	if (argc < 2)
		usage(B_FALSE);

	if (strcmp(argv[0], "groupspace") == 0) {
		/* Toggle default group types */
		types = USTYPE_PSX_GRP | USTYPE_SMB_GRP;
	} else if (strcmp(argv[0], "projectspace") == 0) {
		types = USTYPE_PROJ;
		prtnum = B_TRUE;
	}

	while ((c = getopt(argc, argv, "nHpo:s:S:t:i")) != -1) {
		switch (c) {
		case 'n':
			if (types == USTYPE_PROJ) {
				(void) fprintf(stderr,
				    gettext("invalid option 'n'\n"));
				usage(B_FALSE);
			}
			prtnum = B_TRUE;
			break;
		case 'H':
			scripted = B_TRUE;
			break;
		case 'p':
			parsable = B_TRUE;
			break;
		case 'o':
			ofield = optarg;
			break;
		case 's':
		case 'S':
			if (zfs_add_sort_column(&sortcol, optarg,
			    c == 's' ? B_FALSE : B_TRUE) != 0) {
				(void) fprintf(stderr,
				    gettext("invalid field '%s'\n"), optarg);
				usage(B_FALSE);
			}
			break;
		case 't':
			if (types == USTYPE_PROJ) {
				(void) fprintf(stderr,
				    gettext("invalid option 't'\n"));
				usage(B_FALSE);
			}
			tfield = optarg;
			break;
		case 'i':
			if (types == USTYPE_PROJ) {
				(void) fprintf(stderr,
				    gettext("invalid option 'i'\n"));
				usage(B_FALSE);
			}
			sid2posix = B_TRUE;
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

	if (argc < 1) {
		(void) fprintf(stderr, gettext("missing dataset name\n"));
		usage(B_FALSE);
	}
	if (argc > 1) {
		(void) fprintf(stderr, gettext("too many arguments\n"));
		usage(B_FALSE);
	}

	/* Use default output fields if not specified using -o */
	if (ofield == NULL)
		ofield = deffields;
	do {
		if ((delim = strchr(ofield, ',')) != NULL)
			*delim = '\0';
		if ((fields[cfield++] = us_field_index(ofield)) == -1) {
			(void) fprintf(stderr, gettext("invalid type '%s' "
			    "for -o option\n"), ofield);
			return (-1);
		}
		if (delim != NULL)
			ofield = delim + 1;
	} while (delim != NULL);
	fields[cfield] = USFIELD_LAST;

	/* Override output types (-t option) */
	if (tfield != NULL) {
		types = 0;

		do {
			boolean_t found = B_FALSE;

			if ((delim = strchr(tfield, ',')) != NULL)
				*delim = '\0';
			for (i = 0; i < sizeof (us_type_bits) / sizeof (int);
			    i++) {
				if (strcmp(tfield, us_type_names[i]) == 0) {
					found = B_TRUE;
					types |= us_type_bits[i];
					break;
				}
			}
			if (!found) {
				(void) fprintf(stderr, gettext("invalid type "
				    "'%s' for -t option\n"), tfield);
				return (-1);
			}
			if (delim != NULL)
				tfield = delim + 1;
		} while (delim != NULL);
	}

	if ((zhp = zfs_path_to_zhandle(g_zfs, argv[0], ZFS_TYPE_FILESYSTEM |
	    ZFS_TYPE_SNAPSHOT)) == NULL)
		return (1);
	if (zfs_get_underlying_type(zhp) != ZFS_TYPE_FILESYSTEM) {
		(void) fprintf(stderr, gettext("operation is only applicable "
		    "to filesystems and their snapshots\n"));
		zfs_close(zhp);
		return (1);
	}

	if ((avl_pool = uu_avl_pool_create("us_avl_pool", sizeof (us_node_t),
	    offsetof(us_node_t, usn_avlnode), us_compare, UU_DEFAULT)) == NULL)
		nomem();
	if ((avl_tree = uu_avl_create(avl_pool, NULL, UU_DEFAULT)) == NULL)
		nomem();

	/* Always add default sorting columns */
	(void) zfs_add_sort_column(&sortcol, "type", B_FALSE);
	(void) zfs_add_sort_column(&sortcol, "name", B_FALSE);

	cb.cb_sortcol = sortcol;
	cb.cb_numname = prtnum;
	cb.cb_nicenum = !parsable;
	cb.cb_avl_pool = avl_pool;
	cb.cb_avl = avl_tree;
	cb.cb_sid2posix = sid2posix;

	for (i = 0; i < USFIELD_LAST; i++)
		cb.cb_width[i] = strlen(gettext(us_field_hdr[i]));

	for (p = 0; p < ZFS_NUM_USERQUOTA_PROPS; p++) {
		if ((zfs_prop_is_user(p) &&
		    !(types & (USTYPE_PSX_USR | USTYPE_SMB_USR))) ||
		    (zfs_prop_is_group(p) &&
		    !(types & (USTYPE_PSX_GRP | USTYPE_SMB_GRP))) ||
		    (zfs_prop_is_project(p) && types != USTYPE_PROJ))
			continue;

		cb.cb_prop = p;
		if ((ret = zfs_userspace(zhp, p, userspace_cb, &cb)) != 0) {
			zfs_close(zhp);
			return (ret);
		}
	}
	zfs_close(zhp);

	/* Sort the list */
	if ((node = uu_avl_first(avl_tree)) == NULL)
		return (0);

	us_populated = B_TRUE;

	listpool = uu_list_pool_create("tmplist", sizeof (us_node_t),
	    offsetof(us_node_t, usn_listnode), NULL, UU_DEFAULT);
	list = uu_list_create(listpool, NULL, UU_DEFAULT);
	uu_list_node_init(node, &node->usn_listnode, listpool);

	while (node != NULL) {
		rmnode = node;
		node = uu_avl_next(avl_tree, node);
		uu_avl_remove(avl_tree, rmnode);
		if (uu_list_find(list, rmnode, NULL, &idx2) == NULL)
			uu_list_insert(list, rmnode, idx2);
	}

	for (node = uu_list_first(list); node != NULL;
	    node = uu_list_next(list, node)) {
		us_sort_info_t sortinfo = { sortcol, cb.cb_numname };

		if (uu_avl_find(avl_tree, node, &sortinfo, &idx) == NULL)
			uu_avl_insert(avl_tree, node, idx);
	}

	uu_list_destroy(list);
	uu_list_pool_destroy(listpool);

	/* Print and free node nvlist memory */
	print_us(scripted, parsable, fields, types, cb.cb_width, B_TRUE,
	    cb.cb_avl);

	zfs_free_sort_columns(sortcol);

	/* Clean up the AVL tree */
	if ((walk = uu_avl_walk_start(cb.cb_avl, UU_WALK_ROBUST)) == NULL)
		nomem();

	while ((node = uu_avl_walk_next(walk)) != NULL) {
		uu_avl_remove(cb.cb_avl, node);
		free(node);
	}

	uu_avl_walk_end(walk);
	uu_avl_destroy(avl_tree);
	uu_avl_pool_destroy(avl_pool);

	return (ret);
}
