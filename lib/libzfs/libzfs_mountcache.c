// SPDX-License-Identifier: CDDL-1.0
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


#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <libzfs.h>
#include "libzfs_impl.h"

#define	WHITESPACE	" \n\r\t"

/* XXX ddi_strtoX probably, but some are missing in userspace */
static int
strtou32(const char *str, uint32_t *np)
{
	char *p;
	errno = 0;
	uint32_t n = strtol(str, &p, 10);
	if (errno != 0)
		return (errno);
	if (*p)
		return (EINVAL);
	*np = n;
	return (0);
}

static int
strtou64(const char *str, uint64_t *np)
{
	char *p;
	errno = 0;
	uint64_t n = strtoll(str, &p, 10);
	if (errno != 0)
		return (errno);
	if (*p)
		return (EINVAL);
	*np = n;
	return (0);
}

/*
 * XXX nomenclature, "dataset" vs "source". waiting to see all use cases
 *     before resolving.
 */
typedef struct {
	mount_t		mn_mount;

	int		mn_sort_id;
	int		mn_depth;

	avl_node_t	mn_sort_node;
	avl_node_t	mn_dataset_node;
	avl_node_t	mn_mountpoint_node;
} mountnode_t;

static int
mountcache_compare_id(const void *a, const void *b)
{
	const mountnode_t *mna = a;
	const mountnode_t *mnb = b;

	return (TREE_CMP(mna->mn_mount.m_id, mnb->mn_mount.m_id));
}

static int
mountcache_compare_sort_id(const void *a, const void *b)
{
	const mountnode_t *mna = a;
	const mountnode_t *mnb = b;

	return (TREE_CMP(mna->mn_sort_id, mnb->mn_sort_id));
}

static int
mountcache_compare_dataset(const void *a, const void *b)
{
	const mountnode_t *mna = a;
	const mountnode_t *mnb = b;

	int cmp = TREE_ISIGN(strcmp(mna->mn_mount.m_source,
	    mnb->mn_mount.m_source));
	if (cmp != 0)
		return (cmp);

	if (mna->mn_sort_id == INT_MAX)
		return (0);

	return (TREE_CMP(mna->mn_sort_id, mnb->mn_sort_id));
}

static int
mountcache_compare_mountpoint(const void *a, const void *b)
{
	const mountnode_t *mna = a;
	const mountnode_t *mnb = b;

	int cmp = TREE_ISIGN(strcmp(mna->mn_mount.m_mountpoint,
	    mnb->mn_mount.m_mountpoint));
	if (cmp != 0)
		return (cmp);

	if (mna->mn_sort_id == INT_MAX)
		return (0);

	return (TREE_CMP(mna->mn_sort_id, mnb->mn_sort_id));
}

static void
mountcache_assign_sort(avl_tree_t *idt, mountnode_t *mn,
    int *sort_id, int depth)
{
	mn->mn_sort_id = (*sort_id)++;
	mn->mn_depth = depth++;

	for (mountnode_t *cmn = avl_first(idt); cmn != NULL;
	    cmn = AVL_NEXT(idt, cmn)) {
		if (cmn->mn_mount.m_parent == mn->mn_mount.m_id)
			mountcache_assign_sort(idt, cmn, sort_id, depth);
	}
}

static void
mountcache_free_mountnode(mountnode_t *mn) {
	mount_t *m = &mn->mn_mount;
	free(m->m_root);
	free(m->m_mountpoint);
	free(m->m_mountopts);
	free(m->m_type);
	free(m->m_source);
	free(m->m_superopts);
	for (int i = 0; i < MAX_MOUNT_FIELDS && m->m_fields[i] != NULL; i++)
		free(m->m_fields[i]);
	free(mn);
}

static int
mountcache_load_data(avl_tree_t *sort_tree, avl_tree_t *dataset_tree,
    avl_tree_t *mountpoint_tree)
{
	/*
	 * Temporary tree for initial parse. Borrowing mn_dataset_node
	 * because its unused at this stage, and not needed beyond this
	 * function.
	 */
	avl_tree_t id_tree;
	avl_create(&id_tree, mountcache_compare_id, sizeof (mountnode_t),
	    offsetof(mountnode_t, mn_dataset_node));

	FILE *f = fopen("/proc/self/mountinfo", "re");
	if (f == NULL)
		return (errno);

	int err = 0;

	char *line = NULL;
	size_t len = 0, nread;
	while ((nread = getline(&line, &len, f)) != -1) {
		char *p = line;

		/*
		 * XXX NULL should err = EINVAL; break;
		 *     consider macro?
		 */
		const char *id = strsep(&p, WHITESPACE);
		const char *parent = strsep(&p, WHITESPACE);

		const char *major = strsep(&p, ":");
		const char *minor = strsep(&p, WHITESPACE);

		const char *root = strsep(&p, WHITESPACE);
		const char *mountpoint = strsep(&p, WHITESPACE);
		const char *mountopts = strsep(&p, WHITESPACE);

		const char *fields[MAX_MOUNT_FIELDS] = {};
		int fidx = 0;
		const char *field;
		while ((field = strsep(&p, WHITESPACE)) != NULL) {
			if (*field == '-')
				break;
			if (fidx < MAX_MOUNT_FIELDS)
				fields[fidx++] = field;
		}

		const char *type = strsep(&p, WHITESPACE);
		const char *source = strsep(&p, WHITESPACE);
		const char *superopts = strsep(&p, WHITESPACE);

		uint64_t nid; strtou64(id, &nid);
		uint64_t nparent; strtou64(parent, &nparent);

		uint32_t nmajor; strtou32(major, &nmajor);
		uint32_t nminor; strtou32(minor, &nminor);

		mountnode_t *mn = calloc(1, sizeof (mountnode_t));
		mount_t *m = &mn->mn_mount;
		m->m_id = nid;
		m->m_parent = nparent;
		m->m_major = nmajor;
		m->m_minor = nminor;
		m->m_root = strdup(root);
		m->m_mountpoint = strdup(mountpoint);
		m->m_mountopts = strdup(mountopts);
		m->m_type = strdup(type);
		m->m_source = strdup(source);
		m->m_superopts = strdup(superopts);

		for (int i = 0; i < MAX_MOUNT_FIELDS && fields[i] != NULL; i++)
			m->m_fields[i] = strdup(fields[i]);

		avl_add(&id_tree, mn);
	}

	if (err != 0 && ferror(f))
		err = errno;

	if (line != NULL)
		free(line);

	fclose(f);

	if (err != 0) {
		mountnode_t *mn;
		void *cookie = NULL;
		while ((mn = avl_destroy_nodes(&id_tree, &cookie)) != NULL) {
			mountcache_free_mountnode(mn);
		}
		avl_destroy(&id_tree);
		return (err);
	}

	int sort_id = 0;

	mountnode_t search = {};
	for (mountnode_t *root = avl_first(&id_tree); root;
	    root = AVL_NEXT(&id_tree, root)) {
		boolean_t is_root = root->mn_mount.m_id ==
		    root->mn_mount.m_parent;
		if (!is_root) {
			search.mn_mount.m_id = root->mn_mount.m_parent;
			if (!avl_find(&id_tree, &search, NULL))
				is_root = B_TRUE;
		}
		if (!is_root)
			continue;

		mountcache_assign_sort(&id_tree, root, &sort_id, 0);
	}

	avl_create(sort_tree, mountcache_compare_sort_id, sizeof (mountnode_t),
	    offsetof(mountnode_t, mn_sort_node));
	avl_create(dataset_tree, mountcache_compare_dataset,
	    sizeof (mountnode_t), offsetof(mountnode_t, mn_dataset_node));
	avl_create(mountpoint_tree, mountcache_compare_mountpoint,
	    sizeof (mountnode_t), offsetof(mountnode_t, mn_mountpoint_node));

	mountnode_t *mn;
	void *cookie = NULL;
	while ((mn = avl_destroy_nodes(&id_tree, &cookie)) != NULL) {
		avl_add(sort_tree, mn);
		avl_add(dataset_tree, mn);
		avl_add(mountpoint_tree, mn);
	}
	avl_destroy(&id_tree);

	return (0);
}

static void
mountcache_free_data(avl_tree_t *sort_tree, avl_tree_t *dataset_tree,
    avl_tree_t *mountpoint_tree)
{
	mountnode_t *mn;
	void *cookie = NULL;
	while ((mn = avl_destroy_nodes(sort_tree, &cookie)) != NULL) {
		avl_remove(dataset_tree, mn);
		avl_remove(mountpoint_tree, mn);
		mountcache_free_mountnode(mn);
	}
	avl_destroy(sort_tree);
	avl_destroy(dataset_tree);
	avl_destroy(mountpoint_tree);
}

struct mountcache {
	avl_tree_t mc_sort_tree;
	avl_tree_t mc_dataset_tree;
	avl_tree_t mc_mountpoint_tree;

	kmutex_t mc_lock;
	kcondvar_t mc_cv;
	uint64_t mc_holds;
};

int
mountcache_init(mountcache_t **mcp)
{
	mountcache_t *mc = calloc(1, sizeof (mountcache_t));

	int err = mountcache_load_data(&mc->mc_sort_tree, &mc->mc_dataset_tree,
	    &mc->mc_mountpoint_tree);
	if (err != 0) {
		free(mc);
		return (err);
	}

	mutex_init(&mc->mc_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&mc->mc_cv, NULL, CV_DEFAULT, NULL);

	*mcp = mc;
	return (0);
}

void
mountcache_free(mountcache_t *mc)
{
	ASSERT0(mc->mc_holds);
	cv_destroy(&mc->mc_cv);
	mutex_destroy(&mc->mc_lock);
	mountcache_free_data(&mc->mc_sort_tree, &mc->mc_dataset_tree,
	    &mc->mc_mountpoint_tree);
	free(mc);
}

void
mountcache_enter(mountcache_t *mc)
{
	ASSERT(!MUTEX_HELD(&mc->mc_lock));
	mutex_enter(&mc->mc_lock);
	mc->mc_holds++;
	mutex_exit(&mc->mc_lock);
}

void
mountcache_exit(mountcache_t *mc)
{
	ASSERT(!MUTEX_HELD(&mc->mc_lock));
	mutex_enter(&mc->mc_lock);
	if (--mc->mc_holds == 0)
		cv_broadcast(&mc->mc_cv);
	mutex_exit(&mc->mc_lock);
}

int
mountcache_refresh(mountcache_t *mc)
{
	ASSERT(!MUTEX_HELD(&mc->mc_lock));
	mutex_enter(&mc->mc_lock);
	while (mc->mc_holds != 0)
		cv_wait(&mc->mc_cv, &mc->mc_lock);

	avl_tree_t sort_tree, dataset_tree, mountpoint_tree;
	int err = mountcache_load_data(&sort_tree, &dataset_tree,
	    &mountpoint_tree);
	if (err != 0) {
		mutex_exit(&mc->mc_lock);
		return (err);
	}

	avl_swap(&mc->mc_sort_tree, &sort_tree);
	avl_swap(&mc->mc_dataset_tree, &dataset_tree);
	avl_swap(&mc->mc_mountpoint_tree, &mountpoint_tree);
	mountcache_free_data(&sort_tree, &dataset_tree, &mountpoint_tree);

	mutex_exit(&mc->mc_lock);
	return (0);
}

void
mountcache_dump(mountcache_t *mc)
{
	mountcache_enter(mc);

	boolean_t did_header = B_FALSE;
	const mount_t *m = NULL;
	while ((m = mountcache_foreach_mountpoint(mc, m)) != NULL) {
		if (!did_header) {
			printf("%-4s  %-4s  %-15s  %-20s  %s\n",
			    "ID", "UP", "TYPE", "SOURCE", "MOUNTPOINT");
			did_header = B_TRUE;
		}
		printf("%4lu  %4lu  %-15s  %-20s  %.*s%s\n",
		    m->m_id, m->m_parent, m->m_type, m->m_source,
		    ((const mountnode_t *)m)->mn_depth * 2,
		    "                                        ",
		    m->m_mountpoint);
	}

	printf("\n");

	mountcache_exit(mc);
}

/*
 * XXX this is a placeholder filter function that determines if a given
 *     mountnode is a ZFS mount. its here because most existing callers
 *     filter out non-ZFS mounts, so us doing it is a nice quality-of-life
 *     improvement, and its probably expected because this _is_ a ZFS library
 *     after all. on the other hand, this is also turning into quite a nice
 *     mount data access library, and once everything is converted it may
 *     be that we actually do need to be able to access non-zfs mounts in
 *     order to understand deep trees that include zfs and non-zfs mounts
 *     (especially on either side). but, I didn't want to guess at an
 *     interface yet, so for now, this is here as an in/out function, which
 *     is likey whatever future form will take anyway, maybe just with
 *     different rules, or a set of different functions.
 *       -- robn, 2026-02-25
 */
static inline boolean_t
mountcache_mountnode_is_zfs(mountcache_t *mc, const mountnode_t *mn)
{
	(void) mc;
	if (mn == NULL)
		return (B_FALSE);
	if (strcmp(mn->mn_mount.m_type, "zfs") != 0)
		return (B_FALSE);
	return (B_TRUE);
}

static const mountnode_t *
mountcache_foreach(mountcache_t *mc, avl_tree_t *tree, const mountnode_t *mn)
{
	if (mn == NULL)
		mn = avl_first(tree);
	else
		mn = AVL_NEXT(tree, (mountnode_t *)mn);
	while (mn != NULL && !mountcache_mountnode_is_zfs(mc, mn))
		mn = AVL_NEXT(tree, (mountnode_t *)mn);
	return (mn);
}

const mount_t *
mountcache_foreach_dataset(mountcache_t *mc, const mount_t *m)
{
	return ((const mount_t *) mountcache_foreach(mc,
	    &mc->mc_dataset_tree, (const mountnode_t *) m));
}

const mount_t *
mountcache_foreach_mountpoint(mountcache_t *mc, const mount_t *m)
{
	return ((const mount_t *) mountcache_foreach(mc,
	    &mc->mc_mountpoint_tree, (const mountnode_t *) m));
}

const mount_t *
mountcache_find_by_dataset(mountcache_t *mc, const char *dsname)
{
	mountnode_t search = {
		.mn_mount = {
			.m_source = (char *)dsname,
		},
		.mn_sort_id = INT_MAX,
	};
	mountnode_t *mn = avl_find(&mc->mc_dataset_tree, &search, NULL);
	if (mountcache_mountnode_is_zfs(mc, mn))
		return (&mn->mn_mount);
	return (NULL);
}

const mount_t *
mountcache_find_by_mountpoint(mountcache_t *mc, const char *mountpoint)
{
	mountnode_t search = {
		.mn_mount = {
			.m_mountpoint = (char *)mountpoint,
		},
		.mn_sort_id = INT_MAX,
	};
	mountnode_t *mn = avl_find(&mc->mc_mountpoint_tree, &search, NULL);
	if (mountcache_mountnode_is_zfs(mc, mn))
		return (&mn->mn_mount);
	return (NULL);
}
