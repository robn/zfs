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
 * allow/unallow stuff
 */
/* copied from zfs/sys/dsl_deleg.h */
#define	ZFS_DELEG_PERM_CREATE		"create"
#define	ZFS_DELEG_PERM_DESTROY		"destroy"
#define	ZFS_DELEG_PERM_SNAPSHOT		"snapshot"
#define	ZFS_DELEG_PERM_ROLLBACK		"rollback"
#define	ZFS_DELEG_PERM_CLONE		"clone"
#define	ZFS_DELEG_PERM_PROMOTE		"promote"
#define	ZFS_DELEG_PERM_RENAME		"rename"
#define	ZFS_DELEG_PERM_MOUNT		"mount"
#define	ZFS_DELEG_PERM_SHARE		"share"
#define	ZFS_DELEG_PERM_SEND		"send"
#define	ZFS_DELEG_PERM_RECEIVE		"receive"
#define	ZFS_DELEG_PERM_ALLOW		"allow"
#define	ZFS_DELEG_PERM_USERPROP		"userprop"
#define	ZFS_DELEG_PERM_VSCAN		"vscan" /* ??? */
#define	ZFS_DELEG_PERM_USERQUOTA	"userquota"
#define	ZFS_DELEG_PERM_GROUPQUOTA	"groupquota"
#define	ZFS_DELEG_PERM_USERUSED		"userused"
#define	ZFS_DELEG_PERM_GROUPUSED	"groupused"
#define	ZFS_DELEG_PERM_USEROBJQUOTA	"userobjquota"
#define	ZFS_DELEG_PERM_GROUPOBJQUOTA	"groupobjquota"
#define	ZFS_DELEG_PERM_USEROBJUSED	"userobjused"
#define	ZFS_DELEG_PERM_GROUPOBJUSED	"groupobjused"

#define	ZFS_DELEG_PERM_HOLD		"hold"
#define	ZFS_DELEG_PERM_RELEASE		"release"
#define	ZFS_DELEG_PERM_DIFF		"diff"
#define	ZFS_DELEG_PERM_BOOKMARK		"bookmark"
#define	ZFS_DELEG_PERM_LOAD_KEY		"load-key"
#define	ZFS_DELEG_PERM_CHANGE_KEY	"change-key"

#define	ZFS_DELEG_PERM_PROJECTUSED	"projectused"
#define	ZFS_DELEG_PERM_PROJECTQUOTA	"projectquota"
#define	ZFS_DELEG_PERM_PROJECTOBJUSED	"projectobjused"
#define	ZFS_DELEG_PERM_PROJECTOBJQUOTA	"projectobjquota"

#define	ZFS_NUM_DELEG_NOTES ZFS_DELEG_NOTE_NONE

static zfs_deleg_perm_tab_t zfs_deleg_perm_tbl[] = {
	{ ZFS_DELEG_PERM_ALLOW, ZFS_DELEG_NOTE_ALLOW },
	{ ZFS_DELEG_PERM_CLONE, ZFS_DELEG_NOTE_CLONE },
	{ ZFS_DELEG_PERM_CREATE, ZFS_DELEG_NOTE_CREATE },
	{ ZFS_DELEG_PERM_DESTROY, ZFS_DELEG_NOTE_DESTROY },
	{ ZFS_DELEG_PERM_DIFF, ZFS_DELEG_NOTE_DIFF},
	{ ZFS_DELEG_PERM_HOLD, ZFS_DELEG_NOTE_HOLD },
	{ ZFS_DELEG_PERM_MOUNT, ZFS_DELEG_NOTE_MOUNT },
	{ ZFS_DELEG_PERM_PROMOTE, ZFS_DELEG_NOTE_PROMOTE },
	{ ZFS_DELEG_PERM_RECEIVE, ZFS_DELEG_NOTE_RECEIVE },
	{ ZFS_DELEG_PERM_RELEASE, ZFS_DELEG_NOTE_RELEASE },
	{ ZFS_DELEG_PERM_RENAME, ZFS_DELEG_NOTE_RENAME },
	{ ZFS_DELEG_PERM_ROLLBACK, ZFS_DELEG_NOTE_ROLLBACK },
	{ ZFS_DELEG_PERM_SEND, ZFS_DELEG_NOTE_SEND },
	{ ZFS_DELEG_PERM_SHARE, ZFS_DELEG_NOTE_SHARE },
	{ ZFS_DELEG_PERM_SNAPSHOT, ZFS_DELEG_NOTE_SNAPSHOT },
	{ ZFS_DELEG_PERM_BOOKMARK, ZFS_DELEG_NOTE_BOOKMARK },
	{ ZFS_DELEG_PERM_LOAD_KEY, ZFS_DELEG_NOTE_LOAD_KEY },
	{ ZFS_DELEG_PERM_CHANGE_KEY, ZFS_DELEG_NOTE_CHANGE_KEY },

	{ ZFS_DELEG_PERM_GROUPQUOTA, ZFS_DELEG_NOTE_GROUPQUOTA },
	{ ZFS_DELEG_PERM_GROUPUSED, ZFS_DELEG_NOTE_GROUPUSED },
	{ ZFS_DELEG_PERM_USERPROP, ZFS_DELEG_NOTE_USERPROP },
	{ ZFS_DELEG_PERM_USERQUOTA, ZFS_DELEG_NOTE_USERQUOTA },
	{ ZFS_DELEG_PERM_USERUSED, ZFS_DELEG_NOTE_USERUSED },
	{ ZFS_DELEG_PERM_USEROBJQUOTA, ZFS_DELEG_NOTE_USEROBJQUOTA },
	{ ZFS_DELEG_PERM_USEROBJUSED, ZFS_DELEG_NOTE_USEROBJUSED },
	{ ZFS_DELEG_PERM_GROUPOBJQUOTA, ZFS_DELEG_NOTE_GROUPOBJQUOTA },
	{ ZFS_DELEG_PERM_GROUPOBJUSED, ZFS_DELEG_NOTE_GROUPOBJUSED },
	{ ZFS_DELEG_PERM_PROJECTUSED, ZFS_DELEG_NOTE_PROJECTUSED },
	{ ZFS_DELEG_PERM_PROJECTQUOTA, ZFS_DELEG_NOTE_PROJECTQUOTA },
	{ ZFS_DELEG_PERM_PROJECTOBJUSED, ZFS_DELEG_NOTE_PROJECTOBJUSED },
	{ ZFS_DELEG_PERM_PROJECTOBJQUOTA, ZFS_DELEG_NOTE_PROJECTOBJQUOTA },
	{ NULL, ZFS_DELEG_NOTE_NONE }
};

/* permission structure */
typedef struct deleg_perm {
	zfs_deleg_who_type_t	dp_who_type;
	const char		*dp_name;
	boolean_t		dp_local;
	boolean_t		dp_descend;
} deleg_perm_t;

/* */
typedef struct deleg_perm_node {
	deleg_perm_t		dpn_perm;

	uu_avl_node_t		dpn_avl_node;
} deleg_perm_node_t;

typedef struct fs_perm fs_perm_t;

/* permissions set */
typedef struct who_perm {
	zfs_deleg_who_type_t	who_type;
	const char		*who_name;		/* id */
	char			who_ug_name[256];	/* user/group name */
	fs_perm_t		*who_fsperm;		/* uplink */

	uu_avl_t		*who_deleg_perm_avl;	/* permissions */
} who_perm_t;

/* */
typedef struct who_perm_node {
	who_perm_t	who_perm;
	uu_avl_node_t	who_avl_node;
} who_perm_node_t;

typedef struct fs_perm_set fs_perm_set_t;
/* fs permissions */
struct fs_perm {
	const char		*fsp_name;

	uu_avl_t		*fsp_sc_avl;	/* sets,create */
	uu_avl_t		*fsp_uge_avl;	/* user,group,everyone */

	fs_perm_set_t		*fsp_set;	/* uplink */
};

/* */
typedef struct fs_perm_node {
	fs_perm_t	fspn_fsperm;
	uu_avl_t	*fspn_avl;

	uu_list_node_t	fspn_list_node;
} fs_perm_node_t;

/* top level structure */
struct fs_perm_set {
	uu_list_pool_t	*fsps_list_pool;
	uu_list_t	*fsps_list; /* list of fs_perms */

	uu_avl_pool_t	*fsps_named_set_avl_pool;
	uu_avl_pool_t	*fsps_who_perm_avl_pool;
	uu_avl_pool_t	*fsps_deleg_perm_avl_pool;
};

static inline const char *
deleg_perm_type(zfs_deleg_note_t note)
{
	/* subcommands */
	switch (note) {
		/* SUBCOMMANDS */
		/* OTHER */
	case ZFS_DELEG_NOTE_GROUPQUOTA:
	case ZFS_DELEG_NOTE_GROUPUSED:
	case ZFS_DELEG_NOTE_USERPROP:
	case ZFS_DELEG_NOTE_USERQUOTA:
	case ZFS_DELEG_NOTE_USERUSED:
	case ZFS_DELEG_NOTE_USEROBJQUOTA:
	case ZFS_DELEG_NOTE_USEROBJUSED:
	case ZFS_DELEG_NOTE_GROUPOBJQUOTA:
	case ZFS_DELEG_NOTE_GROUPOBJUSED:
	case ZFS_DELEG_NOTE_PROJECTUSED:
	case ZFS_DELEG_NOTE_PROJECTQUOTA:
	case ZFS_DELEG_NOTE_PROJECTOBJUSED:
	case ZFS_DELEG_NOTE_PROJECTOBJQUOTA:
		/* other */
		return (gettext("other"));
	default:
		return (gettext("subcommand"));
	}
}

static int
who_type2weight(zfs_deleg_who_type_t who_type)
{
	int res;
	switch (who_type) {
		case ZFS_DELEG_NAMED_SET_SETS:
		case ZFS_DELEG_NAMED_SET:
			res = 0;
			break;
		case ZFS_DELEG_CREATE_SETS:
		case ZFS_DELEG_CREATE:
			res = 1;
			break;
		case ZFS_DELEG_USER_SETS:
		case ZFS_DELEG_USER:
			res = 2;
			break;
		case ZFS_DELEG_GROUP_SETS:
		case ZFS_DELEG_GROUP:
			res = 3;
			break;
		case ZFS_DELEG_EVERYONE_SETS:
		case ZFS_DELEG_EVERYONE:
			res = 4;
			break;
		default:
			res = -1;
	}

	return (res);
}

static int
who_perm_compare(const void *larg, const void *rarg, void *unused)
{
	(void) unused;
	const who_perm_node_t *l = larg;
	const who_perm_node_t *r = rarg;
	zfs_deleg_who_type_t ltype = l->who_perm.who_type;
	zfs_deleg_who_type_t rtype = r->who_perm.who_type;
	int lweight = who_type2weight(ltype);
	int rweight = who_type2weight(rtype);
	int res = lweight - rweight;
	if (res == 0)
		res = strncmp(l->who_perm.who_name, r->who_perm.who_name,
		    ZFS_MAX_DELEG_NAME-1);

	if (res == 0)
		return (0);
	if (res > 0)
		return (1);
	else
		return (-1);
}

static int
deleg_perm_compare(const void *larg, const void *rarg, void *unused)
{
	(void) unused;
	const deleg_perm_node_t *l = larg;
	const deleg_perm_node_t *r = rarg;
	int res =  strncmp(l->dpn_perm.dp_name, r->dpn_perm.dp_name,
	    ZFS_MAX_DELEG_NAME-1);

	if (res == 0)
		return (0);

	if (res > 0)
		return (1);
	else
		return (-1);
}

static inline void
fs_perm_set_init(fs_perm_set_t *fspset)
{
	memset(fspset, 0, sizeof (fs_perm_set_t));

	if ((fspset->fsps_list_pool = uu_list_pool_create("fsps_list_pool",
	    sizeof (fs_perm_node_t), offsetof(fs_perm_node_t, fspn_list_node),
	    NULL, UU_DEFAULT)) == NULL)
		nomem();
	if ((fspset->fsps_list = uu_list_create(fspset->fsps_list_pool, NULL,
	    UU_DEFAULT)) == NULL)
		nomem();

	if ((fspset->fsps_named_set_avl_pool = uu_avl_pool_create(
	    "named_set_avl_pool", sizeof (who_perm_node_t), offsetof(
	    who_perm_node_t, who_avl_node), who_perm_compare,
	    UU_DEFAULT)) == NULL)
		nomem();

	if ((fspset->fsps_who_perm_avl_pool = uu_avl_pool_create(
	    "who_perm_avl_pool", sizeof (who_perm_node_t), offsetof(
	    who_perm_node_t, who_avl_node), who_perm_compare,
	    UU_DEFAULT)) == NULL)
		nomem();

	if ((fspset->fsps_deleg_perm_avl_pool = uu_avl_pool_create(
	    "deleg_perm_avl_pool", sizeof (deleg_perm_node_t), offsetof(
	    deleg_perm_node_t, dpn_avl_node), deleg_perm_compare, UU_DEFAULT))
	    == NULL)
		nomem();
}

static inline void fs_perm_fini(fs_perm_t *);
static inline void who_perm_fini(who_perm_t *);

static inline void
fs_perm_set_fini(fs_perm_set_t *fspset)
{
	fs_perm_node_t *node = uu_list_first(fspset->fsps_list);

	while (node != NULL) {
		fs_perm_node_t *next_node =
		    uu_list_next(fspset->fsps_list, node);
		fs_perm_t *fsperm = &node->fspn_fsperm;
		fs_perm_fini(fsperm);
		uu_list_remove(fspset->fsps_list, node);
		free(node);
		node = next_node;
	}

	uu_avl_pool_destroy(fspset->fsps_named_set_avl_pool);
	uu_avl_pool_destroy(fspset->fsps_who_perm_avl_pool);
	uu_avl_pool_destroy(fspset->fsps_deleg_perm_avl_pool);
}

static inline void
deleg_perm_init(deleg_perm_t *deleg_perm, zfs_deleg_who_type_t type,
    const char *name)
{
	deleg_perm->dp_who_type = type;
	deleg_perm->dp_name = name;
}

static inline void
who_perm_init(who_perm_t *who_perm, fs_perm_t *fsperm,
    zfs_deleg_who_type_t type, const char *name)
{
	uu_avl_pool_t	*pool;
	pool = fsperm->fsp_set->fsps_deleg_perm_avl_pool;

	memset(who_perm, 0, sizeof (who_perm_t));

	if ((who_perm->who_deleg_perm_avl = uu_avl_create(pool, NULL,
	    UU_DEFAULT)) == NULL)
		nomem();

	who_perm->who_type = type;
	who_perm->who_name = name;
	who_perm->who_fsperm = fsperm;
}

static inline void
who_perm_fini(who_perm_t *who_perm)
{
	deleg_perm_node_t *node = uu_avl_first(who_perm->who_deleg_perm_avl);

	while (node != NULL) {
		deleg_perm_node_t *next_node =
		    uu_avl_next(who_perm->who_deleg_perm_avl, node);

		uu_avl_remove(who_perm->who_deleg_perm_avl, node);
		free(node);
		node = next_node;
	}

	uu_avl_destroy(who_perm->who_deleg_perm_avl);
}

static inline void
fs_perm_init(fs_perm_t *fsperm, fs_perm_set_t *fspset, const char *fsname)
{
	uu_avl_pool_t	*nset_pool = fspset->fsps_named_set_avl_pool;
	uu_avl_pool_t	*who_pool = fspset->fsps_who_perm_avl_pool;

	memset(fsperm, 0, sizeof (fs_perm_t));

	if ((fsperm->fsp_sc_avl = uu_avl_create(nset_pool, NULL, UU_DEFAULT))
	    == NULL)
		nomem();

	if ((fsperm->fsp_uge_avl = uu_avl_create(who_pool, NULL, UU_DEFAULT))
	    == NULL)
		nomem();

	fsperm->fsp_set = fspset;
	fsperm->fsp_name = fsname;
}

static inline void
fs_perm_fini(fs_perm_t *fsperm)
{
	who_perm_node_t *node = uu_avl_first(fsperm->fsp_sc_avl);
	while (node != NULL) {
		who_perm_node_t *next_node = uu_avl_next(fsperm->fsp_sc_avl,
		    node);
		who_perm_t *who_perm = &node->who_perm;
		who_perm_fini(who_perm);
		uu_avl_remove(fsperm->fsp_sc_avl, node);
		free(node);
		node = next_node;
	}

	node = uu_avl_first(fsperm->fsp_uge_avl);
	while (node != NULL) {
		who_perm_node_t *next_node = uu_avl_next(fsperm->fsp_uge_avl,
		    node);
		who_perm_t *who_perm = &node->who_perm;
		who_perm_fini(who_perm);
		uu_avl_remove(fsperm->fsp_uge_avl, node);
		free(node);
		node = next_node;
	}

	uu_avl_destroy(fsperm->fsp_sc_avl);
	uu_avl_destroy(fsperm->fsp_uge_avl);
}

static void
set_deleg_perm_node(uu_avl_t *avl, deleg_perm_node_t *node,
    zfs_deleg_who_type_t who_type, const char *name, char locality)
{
	uu_avl_index_t idx = 0;

	deleg_perm_node_t *found_node = NULL;
	deleg_perm_t	*deleg_perm = &node->dpn_perm;

	deleg_perm_init(deleg_perm, who_type, name);

	if ((found_node = uu_avl_find(avl, node, NULL, &idx))
	    == NULL)
		uu_avl_insert(avl, node, idx);
	else {
		node = found_node;
		deleg_perm = &node->dpn_perm;
	}


	switch (locality) {
	case ZFS_DELEG_LOCAL:
		deleg_perm->dp_local = B_TRUE;
		break;
	case ZFS_DELEG_DESCENDENT:
		deleg_perm->dp_descend = B_TRUE;
		break;
	case ZFS_DELEG_NA:
		break;
	default:
		assert(B_FALSE); /* invalid locality */
	}
}

static inline int
parse_who_perm(who_perm_t *who_perm, nvlist_t *nvl, char locality)
{
	nvpair_t *nvp = NULL;
	fs_perm_set_t *fspset = who_perm->who_fsperm->fsp_set;
	uu_avl_t *avl = who_perm->who_deleg_perm_avl;
	zfs_deleg_who_type_t who_type = who_perm->who_type;

	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		const char *name = nvpair_name(nvp);
		data_type_t type = nvpair_type(nvp);
		uu_avl_pool_t *avl_pool = fspset->fsps_deleg_perm_avl_pool;
		deleg_perm_node_t *node =
		    safe_malloc(sizeof (deleg_perm_node_t));

		VERIFY(type == DATA_TYPE_BOOLEAN);

		uu_avl_node_init(node, &node->dpn_avl_node, avl_pool);
		set_deleg_perm_node(avl, node, who_type, name, locality);
	}

	return (0);
}

static inline int
parse_fs_perm(fs_perm_t *fsperm, nvlist_t *nvl)
{
	nvpair_t *nvp = NULL;
	fs_perm_set_t *fspset = fsperm->fsp_set;

	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		nvlist_t *nvl2 = NULL;
		const char *name = nvpair_name(nvp);
		uu_avl_t *avl = NULL;
		uu_avl_pool_t *avl_pool = NULL;
		zfs_deleg_who_type_t perm_type = name[0];
		char perm_locality = name[1];
		const char *perm_name = name + 3;
		who_perm_t *who_perm = NULL;

		assert('$' == name[2]);

		if (nvpair_value_nvlist(nvp, &nvl2) != 0)
			return (-1);

		switch (perm_type) {
		case ZFS_DELEG_CREATE:
		case ZFS_DELEG_CREATE_SETS:
		case ZFS_DELEG_NAMED_SET:
		case ZFS_DELEG_NAMED_SET_SETS:
			avl_pool = fspset->fsps_named_set_avl_pool;
			avl = fsperm->fsp_sc_avl;
			break;
		case ZFS_DELEG_USER:
		case ZFS_DELEG_USER_SETS:
		case ZFS_DELEG_GROUP:
		case ZFS_DELEG_GROUP_SETS:
		case ZFS_DELEG_EVERYONE:
		case ZFS_DELEG_EVERYONE_SETS:
			avl_pool = fspset->fsps_who_perm_avl_pool;
			avl = fsperm->fsp_uge_avl;
			break;

		default:
			assert(!"unhandled zfs_deleg_who_type_t");
		}

		who_perm_node_t *found_node = NULL;
		who_perm_node_t *node = safe_malloc(
		    sizeof (who_perm_node_t));
		who_perm = &node->who_perm;
		uu_avl_index_t idx = 0;

		uu_avl_node_init(node, &node->who_avl_node, avl_pool);
		who_perm_init(who_perm, fsperm, perm_type, perm_name);

		if ((found_node = uu_avl_find(avl, node, NULL, &idx))
		    == NULL) {
			if (avl == fsperm->fsp_uge_avl) {
				uid_t rid = 0;
				struct passwd *p = NULL;
				struct group *g = NULL;
				const char *nice_name = NULL;

				switch (perm_type) {
				case ZFS_DELEG_USER_SETS:
				case ZFS_DELEG_USER:
					rid = atoi(perm_name);
					p = getpwuid(rid);
					if (p)
						nice_name = p->pw_name;
					break;
				case ZFS_DELEG_GROUP_SETS:
				case ZFS_DELEG_GROUP:
					rid = atoi(perm_name);
					g = getgrgid(rid);
					if (g)
						nice_name = g->gr_name;
					break;

				default:
					break;
				}

				if (nice_name != NULL) {
					(void) strlcpy(
					    node->who_perm.who_ug_name,
					    nice_name, 256);
				} else {
					/* User or group unknown */
					(void) snprintf(
					    node->who_perm.who_ug_name,
					    sizeof (node->who_perm.who_ug_name),
					    "(unknown: %d)", rid);
				}
			}

			uu_avl_insert(avl, node, idx);
		} else {
			node = found_node;
			who_perm = &node->who_perm;
		}

		assert(who_perm != NULL);
		(void) parse_who_perm(who_perm, nvl2, perm_locality);
	}

	return (0);
}

static inline int
parse_fs_perm_set(fs_perm_set_t *fspset, nvlist_t *nvl)
{
	nvpair_t *nvp = NULL;
	uu_avl_index_t idx = 0;

	while ((nvp = nvlist_next_nvpair(nvl, nvp)) != NULL) {
		nvlist_t *nvl2 = NULL;
		const char *fsname = nvpair_name(nvp);
		data_type_t type = nvpair_type(nvp);
		fs_perm_t *fsperm = NULL;
		fs_perm_node_t *node = safe_malloc(sizeof (fs_perm_node_t));

		fsperm = &node->fspn_fsperm;

		VERIFY(DATA_TYPE_NVLIST == type);

		uu_list_node_init(node, &node->fspn_list_node,
		    fspset->fsps_list_pool);

		idx = uu_list_numnodes(fspset->fsps_list);
		fs_perm_init(fsperm, fspset, fsname);

		if (nvpair_value_nvlist(nvp, &nvl2) != 0)
			return (-1);

		(void) parse_fs_perm(fsperm, nvl2);

		uu_list_insert(fspset->fsps_list, node, idx);
	}

	return (0);
}

static inline const char *
deleg_perm_comment(zfs_deleg_note_t note)
{
	const char *str = "";

	/* subcommands */
	switch (note) {
		/* SUBCOMMANDS */
	case ZFS_DELEG_NOTE_ALLOW:
		str = gettext("Must also have the permission that is being"
		    "\n\t\t\t\tallowed");
		break;
	case ZFS_DELEG_NOTE_CLONE:
		str = gettext("Must also have the 'create' ability and 'mount'"
		    "\n\t\t\t\tability in the origin file system");
		break;
	case ZFS_DELEG_NOTE_CREATE:
		str = gettext("Must also have the 'mount' ability");
		break;
	case ZFS_DELEG_NOTE_DESTROY:
		str = gettext("Must also have the 'mount' ability");
		break;
	case ZFS_DELEG_NOTE_DIFF:
		str = gettext("Allows lookup of paths within a dataset;"
		    "\n\t\t\t\tgiven an object number. Ordinary users need this"
		    "\n\t\t\t\tin order to use zfs diff");
		break;
	case ZFS_DELEG_NOTE_HOLD:
		str = gettext("Allows adding a user hold to a snapshot");
		break;
	case ZFS_DELEG_NOTE_MOUNT:
		str = gettext("Allows mount/umount of ZFS datasets");
		break;
	case ZFS_DELEG_NOTE_PROMOTE:
		str = gettext("Must also have the 'mount'\n\t\t\t\tand"
		    " 'promote' ability in the origin file system");
		break;
	case ZFS_DELEG_NOTE_RECEIVE:
		str = gettext("Must also have the 'mount' and 'create'"
		    " ability");
		break;
	case ZFS_DELEG_NOTE_RELEASE:
		str = gettext("Allows releasing a user hold which\n\t\t\t\t"
		    "might destroy the snapshot");
		break;
	case ZFS_DELEG_NOTE_RENAME:
		str = gettext("Must also have the 'mount' and 'create'"
		    "\n\t\t\t\tability in the new parent");
		break;
	case ZFS_DELEG_NOTE_ROLLBACK:
		str = gettext("");
		break;
	case ZFS_DELEG_NOTE_SEND:
		str = gettext("");
		break;
	case ZFS_DELEG_NOTE_SHARE:
		str = gettext("Allows sharing file systems over NFS or SMB"
		    "\n\t\t\t\tprotocols");
		break;
	case ZFS_DELEG_NOTE_SNAPSHOT:
		str = gettext("");
		break;
	case ZFS_DELEG_NOTE_LOAD_KEY:
		str = gettext("Allows loading or unloading an encryption key");
		break;
	case ZFS_DELEG_NOTE_CHANGE_KEY:
		str = gettext("Allows changing or adding an encryption key");
		break;
/*
 *	case ZFS_DELEG_NOTE_VSCAN:
 *		str = gettext("");
 *		break;
 */
		/* OTHER */
	case ZFS_DELEG_NOTE_GROUPQUOTA:
		str = gettext("Allows accessing any groupquota@... property");
		break;
	case ZFS_DELEG_NOTE_GROUPUSED:
		str = gettext("Allows reading any groupused@... property");
		break;
	case ZFS_DELEG_NOTE_USERPROP:
		str = gettext("Allows changing any user property");
		break;
	case ZFS_DELEG_NOTE_USERQUOTA:
		str = gettext("Allows accessing any userquota@... property");
		break;
	case ZFS_DELEG_NOTE_USERUSED:
		str = gettext("Allows reading any userused@... property");
		break;
	case ZFS_DELEG_NOTE_USEROBJQUOTA:
		str = gettext("Allows accessing any userobjquota@... property");
		break;
	case ZFS_DELEG_NOTE_GROUPOBJQUOTA:
		str = gettext("Allows accessing any \n\t\t\t\t"
		    "groupobjquota@... property");
		break;
	case ZFS_DELEG_NOTE_GROUPOBJUSED:
		str = gettext("Allows reading any groupobjused@... property");
		break;
	case ZFS_DELEG_NOTE_USEROBJUSED:
		str = gettext("Allows reading any userobjused@... property");
		break;
	case ZFS_DELEG_NOTE_PROJECTQUOTA:
		str = gettext("Allows accessing any projectquota@... property");
		break;
	case ZFS_DELEG_NOTE_PROJECTOBJQUOTA:
		str = gettext("Allows accessing any \n\t\t\t\t"
		    "projectobjquota@... property");
		break;
	case ZFS_DELEG_NOTE_PROJECTUSED:
		str = gettext("Allows reading any projectused@... property");
		break;
	case ZFS_DELEG_NOTE_PROJECTOBJUSED:
		str = gettext("Allows accessing any \n\t\t\t\t"
		    "projectobjused@... property");
		break;
		/* other */
	default:
		str = "";
	}

	return (str);
}

struct allow_opts {
	boolean_t local;
	boolean_t descend;
	boolean_t user;
	boolean_t group;
	boolean_t everyone;
	boolean_t create;
	boolean_t set;
	boolean_t recursive; /* unallow only */
	boolean_t prt_usage;

	boolean_t prt_perms;
	char *who;
	char *perms;
	const char *dataset;
};

static inline int
prop_cmp(const void *a, const void *b)
{
	const char *str1 = *(const char **)a;
	const char *str2 = *(const char **)b;
	return (strcmp(str1, str2));
}

static void
allow_usage(boolean_t un, boolean_t requested, const char *msg)
{
	const char *opt_desc[] = {
		"-h", gettext("show this help message and exit"),
		"-l", gettext("set permission locally"),
		"-d", gettext("set permission for descents"),
		"-u", gettext("set permission for user"),
		"-g", gettext("set permission for group"),
		"-e", gettext("set permission for everyone"),
		"-c", gettext("set create time permission"),
		"-s", gettext("define permission set"),
		/* unallow only */
		"-r", gettext("remove permissions recursively"),
	};
	size_t unallow_size = sizeof (opt_desc) / sizeof (char *);
	size_t allow_size = unallow_size - 2;
	const char *props[ZFS_NUM_PROPS];
	int i;
	size_t count = 0;
	FILE *fp = requested ? stdout : stderr;
	zprop_desc_t *pdtbl = zfs_prop_get_table();
	const char *fmt = gettext("%-16s %-14s\t%s\n");

	(void) fprintf(fp, gettext("Usage: %s\n"), get_usage(un ? HELP_UNALLOW :
	    HELP_ALLOW));
	(void) fprintf(fp, gettext("Options:\n"));
	for (i = 0; i < (un ? unallow_size : allow_size); i += 2) {
		const char *opt = opt_desc[i];
		const char *optdsc = opt_desc[i + 1];
		(void) fprintf(fp, gettext("  %-10s  %s\n"), opt, optdsc);
	}

	(void) fprintf(fp, gettext("\nThe following permissions are "
	    "supported:\n\n"));
	(void) fprintf(fp, fmt, gettext("NAME"), gettext("TYPE"),
	    gettext("NOTES"));
	for (i = 0; i < ZFS_NUM_DELEG_NOTES; i++) {
		const char *perm_name = zfs_deleg_perm_tbl[i].z_perm;
		zfs_deleg_note_t perm_note = zfs_deleg_perm_tbl[i].z_note;
		const char *perm_type = deleg_perm_type(perm_note);
		const char *perm_comment = deleg_perm_comment(perm_note);
		(void) fprintf(fp, fmt, perm_name, perm_type, perm_comment);
	}

	for (i = 0; i < ZFS_NUM_PROPS; i++) {
		zprop_desc_t *pd = &pdtbl[i];
		if (pd->pd_visible != B_TRUE)
			continue;

		if (pd->pd_attr == PROP_READONLY)
			continue;

		props[count++] = pd->pd_name;
	}
	props[count] = NULL;

	qsort(props, count, sizeof (char *), prop_cmp);

	for (i = 0; i < count; i++)
		(void) fprintf(fp, fmt, props[i], gettext("property"), "");

	if (msg != NULL)
		(void) fprintf(fp, gettext("\nzfs: error: %s"), msg);

	exit(requested ? 0 : 2);
}

static inline const char *
munge_args(int argc, char **argv, boolean_t un, size_t expected_argc,
    char **permsp)
{
	if (un && argc == expected_argc - 1)
		*permsp = NULL;
	else if (argc == expected_argc)
		*permsp = argv[argc - 2];
	else
		allow_usage(un, B_FALSE,
		    gettext("wrong number of parameters\n"));

	return (argv[argc - 1]);
}

static void
parse_allow_args(int argc, char **argv, boolean_t un, struct allow_opts *opts)
{
	int uge_sum = opts->user + opts->group + opts->everyone;
	int csuge_sum = opts->create + opts->set + uge_sum;
	int ldcsuge_sum = csuge_sum + opts->local + opts->descend;
	int all_sum = un ? ldcsuge_sum + opts->recursive : ldcsuge_sum;

	if (uge_sum > 1)
		allow_usage(un, B_FALSE,
		    gettext("-u, -g, and -e are mutually exclusive\n"));

	if (opts->prt_usage) {
		if (argc == 0 && all_sum == 0)
			allow_usage(un, B_TRUE, NULL);
		else
			usage(B_FALSE);
	}

	if (opts->set) {
		if (csuge_sum > 1)
			allow_usage(un, B_FALSE,
			    gettext("invalid options combined with -s\n"));

		opts->dataset = munge_args(argc, argv, un, 3, &opts->perms);
		if (argv[0][0] != '@')
			allow_usage(un, B_FALSE,
			    gettext("invalid set name: missing '@' prefix\n"));
		opts->who = argv[0];
	} else if (opts->create) {
		if (ldcsuge_sum > 1)
			allow_usage(un, B_FALSE,
			    gettext("invalid options combined with -c\n"));
		opts->dataset = munge_args(argc, argv, un, 2, &opts->perms);
	} else if (opts->everyone) {
		if (csuge_sum > 1)
			allow_usage(un, B_FALSE,
			    gettext("invalid options combined with -e\n"));
		opts->dataset = munge_args(argc, argv, un, 2, &opts->perms);
	} else if (uge_sum == 0 && argc > 0 && strcmp(argv[0], "everyone")
	    == 0) {
		opts->everyone = B_TRUE;
		argc--;
		argv++;
		opts->dataset = munge_args(argc, argv, un, 2, &opts->perms);
	} else if (argc == 1 && !un) {
		opts->prt_perms = B_TRUE;
		opts->dataset = argv[argc-1];
	} else {
		opts->dataset = munge_args(argc, argv, un, 3, &opts->perms);
		opts->who = argv[0];
	}

	if (!opts->local && !opts->descend) {
		opts->local = B_TRUE;
		opts->descend = B_TRUE;
	}
}

static void
store_allow_perm(zfs_deleg_who_type_t type, boolean_t local, boolean_t descend,
    const char *who, char *perms, nvlist_t *top_nvl)
{
	int i;
	char ld[2] = { '\0', '\0' };
	char who_buf[MAXNAMELEN + 32];
	char base_type = '\0';
	char set_type = '\0';
	nvlist_t *base_nvl = NULL;
	nvlist_t *set_nvl = NULL;
	nvlist_t *nvl;

	if (nvlist_alloc(&base_nvl, NV_UNIQUE_NAME, 0) != 0)
		nomem();
	if (nvlist_alloc(&set_nvl, NV_UNIQUE_NAME, 0) !=  0)
		nomem();

	switch (type) {
	case ZFS_DELEG_NAMED_SET_SETS:
	case ZFS_DELEG_NAMED_SET:
		set_type = ZFS_DELEG_NAMED_SET_SETS;
		base_type = ZFS_DELEG_NAMED_SET;
		ld[0] = ZFS_DELEG_NA;
		break;
	case ZFS_DELEG_CREATE_SETS:
	case ZFS_DELEG_CREATE:
		set_type = ZFS_DELEG_CREATE_SETS;
		base_type = ZFS_DELEG_CREATE;
		ld[0] = ZFS_DELEG_NA;
		break;
	case ZFS_DELEG_USER_SETS:
	case ZFS_DELEG_USER:
		set_type = ZFS_DELEG_USER_SETS;
		base_type = ZFS_DELEG_USER;
		if (local)
			ld[0] = ZFS_DELEG_LOCAL;
		if (descend)
			ld[1] = ZFS_DELEG_DESCENDENT;
		break;
	case ZFS_DELEG_GROUP_SETS:
	case ZFS_DELEG_GROUP:
		set_type = ZFS_DELEG_GROUP_SETS;
		base_type = ZFS_DELEG_GROUP;
		if (local)
			ld[0] = ZFS_DELEG_LOCAL;
		if (descend)
			ld[1] = ZFS_DELEG_DESCENDENT;
		break;
	case ZFS_DELEG_EVERYONE_SETS:
	case ZFS_DELEG_EVERYONE:
		set_type = ZFS_DELEG_EVERYONE_SETS;
		base_type = ZFS_DELEG_EVERYONE;
		if (local)
			ld[0] = ZFS_DELEG_LOCAL;
		if (descend)
			ld[1] = ZFS_DELEG_DESCENDENT;
		break;

	default:
		assert(set_type != '\0' && base_type != '\0');
	}

	if (perms != NULL) {
		char *curr = perms;
		char *end = curr + strlen(perms);

		while (curr < end) {
			char *delim = strchr(curr, ',');
			if (delim == NULL)
				delim = end;
			else
				*delim = '\0';

			if (curr[0] == '@')
				nvl = set_nvl;
			else
				nvl = base_nvl;

			(void) nvlist_add_boolean(nvl, curr);
			if (delim != end)
				*delim = ',';
			curr = delim + 1;
		}

		for (i = 0; i < 2; i++) {
			char locality = ld[i];
			if (locality == 0)
				continue;

			if (!nvlist_empty(base_nvl)) {
				if (who != NULL)
					(void) snprintf(who_buf,
					    sizeof (who_buf), "%c%c$%s",
					    base_type, locality, who);
				else
					(void) snprintf(who_buf,
					    sizeof (who_buf), "%c%c$",
					    base_type, locality);

				(void) nvlist_add_nvlist(top_nvl, who_buf,
				    base_nvl);
			}


			if (!nvlist_empty(set_nvl)) {
				if (who != NULL)
					(void) snprintf(who_buf,
					    sizeof (who_buf), "%c%c$%s",
					    set_type, locality, who);
				else
					(void) snprintf(who_buf,
					    sizeof (who_buf), "%c%c$",
					    set_type, locality);

				(void) nvlist_add_nvlist(top_nvl, who_buf,
				    set_nvl);
			}
		}
	} else {
		for (i = 0; i < 2; i++) {
			char locality = ld[i];
			if (locality == 0)
				continue;

			if (who != NULL)
				(void) snprintf(who_buf, sizeof (who_buf),
				    "%c%c$%s", base_type, locality, who);
			else
				(void) snprintf(who_buf, sizeof (who_buf),
				    "%c%c$", base_type, locality);
			(void) nvlist_add_boolean(top_nvl, who_buf);

			if (who != NULL)
				(void) snprintf(who_buf, sizeof (who_buf),
				    "%c%c$%s", set_type, locality, who);
			else
				(void) snprintf(who_buf, sizeof (who_buf),
				    "%c%c$", set_type, locality);
			(void) nvlist_add_boolean(top_nvl, who_buf);
		}
	}
}

static int
construct_fsacl_list(boolean_t un, struct allow_opts *opts, nvlist_t **nvlp)
{
	if (nvlist_alloc(nvlp, NV_UNIQUE_NAME, 0) != 0)
		nomem();

	if (opts->set) {
		store_allow_perm(ZFS_DELEG_NAMED_SET, opts->local,
		    opts->descend, opts->who, opts->perms, *nvlp);
	} else if (opts->create) {
		store_allow_perm(ZFS_DELEG_CREATE, opts->local,
		    opts->descend, NULL, opts->perms, *nvlp);
	} else if (opts->everyone) {
		store_allow_perm(ZFS_DELEG_EVERYONE, opts->local,
		    opts->descend, NULL, opts->perms, *nvlp);
	} else {
		char *curr = opts->who;
		char *end = curr + strlen(curr);

		while (curr < end) {
			const char *who;
			zfs_deleg_who_type_t who_type = ZFS_DELEG_WHO_UNKNOWN;
			char *endch;
			char *delim = strchr(curr, ',');
			char errbuf[256];
			char id[64];
			struct passwd *p = NULL;
			struct group *g = NULL;

			uid_t rid;
			if (delim == NULL)
				delim = end;
			else
				*delim = '\0';

			rid = (uid_t)strtol(curr, &endch, 0);
			if (opts->user) {
				who_type = ZFS_DELEG_USER;
				if (*endch != '\0')
					p = getpwnam(curr);
				else
					p = getpwuid(rid);

				if (p != NULL)
					rid = p->pw_uid;
				else if (*endch != '\0') {
					(void) snprintf(errbuf, sizeof (errbuf),
					    gettext("invalid user %s\n"), curr);
					allow_usage(un, B_TRUE, errbuf);
				}
			} else if (opts->group) {
				who_type = ZFS_DELEG_GROUP;
				if (*endch != '\0')
					g = getgrnam(curr);
				else
					g = getgrgid(rid);

				if (g != NULL)
					rid = g->gr_gid;
				else if (*endch != '\0') {
					(void) snprintf(errbuf, sizeof (errbuf),
					    gettext("invalid group %s\n"),
					    curr);
					allow_usage(un, B_TRUE, errbuf);
				}
			} else {
				if (*endch != '\0') {
					p = getpwnam(curr);
				} else {
					p = getpwuid(rid);
				}

				if (p == NULL) {
					if (*endch != '\0') {
						g = getgrnam(curr);
					} else {
						g = getgrgid(rid);
					}
				}

				if (p != NULL) {
					who_type = ZFS_DELEG_USER;
					rid = p->pw_uid;
				} else if (g != NULL) {
					who_type = ZFS_DELEG_GROUP;
					rid = g->gr_gid;
				} else {
					(void) snprintf(errbuf, sizeof (errbuf),
					    gettext("invalid user/group %s\n"),
					    curr);
					allow_usage(un, B_TRUE, errbuf);
				}
			}

			(void) sprintf(id, "%u", rid);
			who = id;

			store_allow_perm(who_type, opts->local,
			    opts->descend, who, opts->perms, *nvlp);
			curr = delim + 1;
		}
	}

	return (0);
}

static void
print_set_creat_perms(uu_avl_t *who_avl)
{
	const char *sc_title[] = {
		gettext("Permission sets:\n"),
		gettext("Create time permissions:\n"),
		NULL
	};
	who_perm_node_t *who_node = NULL;
	int prev_weight = -1;

	for (who_node = uu_avl_first(who_avl); who_node != NULL;
	    who_node = uu_avl_next(who_avl, who_node)) {
		uu_avl_t *avl = who_node->who_perm.who_deleg_perm_avl;
		zfs_deleg_who_type_t who_type = who_node->who_perm.who_type;
		const char *who_name = who_node->who_perm.who_name;
		int weight = who_type2weight(who_type);
		boolean_t first = B_TRUE;
		deleg_perm_node_t *deleg_node;

		if (prev_weight != weight) {
			(void) printf("%s", sc_title[weight]);
			prev_weight = weight;
		}

		if (who_name == NULL || strnlen(who_name, 1) == 0)
			(void) printf("\t");
		else
			(void) printf("\t%s ", who_name);

		for (deleg_node = uu_avl_first(avl); deleg_node != NULL;
		    deleg_node = uu_avl_next(avl, deleg_node)) {
			if (first) {
				(void) printf("%s",
				    deleg_node->dpn_perm.dp_name);
				first = B_FALSE;
			} else
				(void) printf(",%s",
				    deleg_node->dpn_perm.dp_name);
		}

		(void) printf("\n");
	}
}

static void
print_uge_deleg_perms(uu_avl_t *who_avl, boolean_t local, boolean_t descend,
    const char *title)
{
	who_perm_node_t *who_node = NULL;
	boolean_t prt_title = B_TRUE;
	uu_avl_walk_t *walk;

	if ((walk = uu_avl_walk_start(who_avl, UU_WALK_ROBUST)) == NULL)
		nomem();

	while ((who_node = uu_avl_walk_next(walk)) != NULL) {
		const char *who_name = who_node->who_perm.who_name;
		const char *nice_who_name = who_node->who_perm.who_ug_name;
		uu_avl_t *avl = who_node->who_perm.who_deleg_perm_avl;
		zfs_deleg_who_type_t who_type = who_node->who_perm.who_type;
		char delim = ' ';
		deleg_perm_node_t *deleg_node;
		boolean_t prt_who = B_TRUE;

		for (deleg_node = uu_avl_first(avl);
		    deleg_node != NULL;
		    deleg_node = uu_avl_next(avl, deleg_node)) {
			if (local != deleg_node->dpn_perm.dp_local ||
			    descend != deleg_node->dpn_perm.dp_descend)
				continue;

			if (prt_who) {
				const char *who = NULL;
				if (prt_title) {
					prt_title = B_FALSE;
					(void) printf("%s", title);
				}

				switch (who_type) {
				case ZFS_DELEG_USER_SETS:
				case ZFS_DELEG_USER:
					who = gettext("user");
					if (nice_who_name)
						who_name  = nice_who_name;
					break;
				case ZFS_DELEG_GROUP_SETS:
				case ZFS_DELEG_GROUP:
					who = gettext("group");
					if (nice_who_name)
						who_name  = nice_who_name;
					break;
				case ZFS_DELEG_EVERYONE_SETS:
				case ZFS_DELEG_EVERYONE:
					who = gettext("everyone");
					who_name = NULL;
					break;

				default:
					assert(who != NULL);
				}

				prt_who = B_FALSE;
				if (who_name == NULL)
					(void) printf("\t%s", who);
				else
					(void) printf("\t%s %s", who, who_name);
			}

			(void) printf("%c%s", delim,
			    deleg_node->dpn_perm.dp_name);
			delim = ',';
		}

		if (!prt_who)
			(void) printf("\n");
	}

	uu_avl_walk_end(walk);
}

static void
print_fs_perms(fs_perm_set_t *fspset)
{
	fs_perm_node_t *node = NULL;
	char buf[MAXNAMELEN + 32];
	const char *dsname = buf;

	for (node = uu_list_first(fspset->fsps_list); node != NULL;
	    node = uu_list_next(fspset->fsps_list, node)) {
		uu_avl_t *sc_avl = node->fspn_fsperm.fsp_sc_avl;
		uu_avl_t *uge_avl = node->fspn_fsperm.fsp_uge_avl;
		int left = 0;

		(void) snprintf(buf, sizeof (buf),
		    gettext("---- Permissions on %s "),
		    node->fspn_fsperm.fsp_name);
		(void) printf("%s", dsname);
		left = 70 - strlen(buf);
		while (left-- > 0)
			(void) printf("-");
		(void) printf("\n");

		print_set_creat_perms(sc_avl);
		print_uge_deleg_perms(uge_avl, B_TRUE, B_FALSE,
		    gettext("Local permissions:\n"));
		print_uge_deleg_perms(uge_avl, B_FALSE, B_TRUE,
		    gettext("Descendent permissions:\n"));
		print_uge_deleg_perms(uge_avl, B_TRUE, B_TRUE,
		    gettext("Local+Descendent permissions:\n"));
	}
}

static fs_perm_set_t fs_perm_set = { NULL, NULL, NULL, NULL };

struct deleg_perms {
	boolean_t un;
	nvlist_t *nvl;
};

static int
set_deleg_perms(zfs_handle_t *zhp, void *data)
{
	struct deleg_perms *perms = (struct deleg_perms *)data;
	zfs_type_t zfs_type = zfs_get_type(zhp);

	if (zfs_type != ZFS_TYPE_FILESYSTEM && zfs_type != ZFS_TYPE_VOLUME)
		return (0);

	return (zfs_set_fsacl(zhp, perms->un, perms->nvl));
}

static int
zfs_do_allow_unallow_impl(int argc, char **argv, boolean_t un)
{
	zfs_handle_t *zhp;
	nvlist_t *perm_nvl = NULL;
	nvlist_t *update_perm_nvl = NULL;
	int error = 1;
	int c;
	struct allow_opts opts = { 0 };

	const char *optstr = un ? "ldugecsrh" : "ldugecsh";

	/* check opts */
	while ((c = getopt(argc, argv, optstr)) != -1) {
		switch (c) {
		case 'l':
			opts.local = B_TRUE;
			break;
		case 'd':
			opts.descend = B_TRUE;
			break;
		case 'u':
			opts.user = B_TRUE;
			break;
		case 'g':
			opts.group = B_TRUE;
			break;
		case 'e':
			opts.everyone = B_TRUE;
			break;
		case 's':
			opts.set = B_TRUE;
			break;
		case 'c':
			opts.create = B_TRUE;
			break;
		case 'r':
			opts.recursive = B_TRUE;
			break;
		case ':':
			(void) fprintf(stderr, gettext("missing argument for "
			    "'%c' option\n"), optopt);
			usage(B_FALSE);
			break;
		case 'h':
			opts.prt_usage = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, gettext("invalid option '%c'\n"),
			    optopt);
			usage(B_FALSE);
		}
	}

	argc -= optind;
	argv += optind;

	/* check arguments */
	parse_allow_args(argc, argv, un, &opts);

	/* try to open the dataset */
	if ((zhp = zfs_open(g_zfs, opts.dataset, ZFS_TYPE_FILESYSTEM |
	    ZFS_TYPE_VOLUME)) == NULL) {
		(void) fprintf(stderr, "Failed to open dataset: %s\n",
		    opts.dataset);
		return (-1);
	}

	if (zfs_get_fsacl(zhp, &perm_nvl) != 0)
		goto cleanup2;

	fs_perm_set_init(&fs_perm_set);
	if (parse_fs_perm_set(&fs_perm_set, perm_nvl) != 0) {
		(void) fprintf(stderr, "Failed to parse fsacl permissions\n");
		goto cleanup1;
	}

	if (opts.prt_perms)
		print_fs_perms(&fs_perm_set);
	else {
		(void) construct_fsacl_list(un, &opts, &update_perm_nvl);
		if (zfs_set_fsacl(zhp, un, update_perm_nvl) != 0)
			goto cleanup0;

		if (un && opts.recursive) {
			struct deleg_perms data = { un, update_perm_nvl };
			if (zfs_iter_filesystems_v2(zhp, 0, set_deleg_perms,
			    &data) != 0)
				goto cleanup0;
		}
	}

	error = 0;

cleanup0:
	nvlist_free(perm_nvl);
	nvlist_free(update_perm_nvl);
cleanup1:
	fs_perm_set_fini(&fs_perm_set);
cleanup2:
	zfs_close(zhp);

	return (error);
}

int
zfs_do_allow(int argc, char **argv)
{
	return (zfs_do_allow_unallow_impl(argc, argv, B_FALSE));
}

int
zfs_do_unallow(int argc, char **argv)
{
	return (zfs_do_allow_unallow_impl(argc, argv, B_TRUE));
}
