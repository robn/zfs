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
#define	MAX_FIELDS	(8)

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

typedef struct {
	uint64_t	m_id;
	uint64_t	m_parent;
	uint32_t	m_major;
	uint32_t	m_minor;
	char		*m_root;
	char		*m_mountpoint;
	char		*m_mountopts;
	char		*m_fields[MAX_FIELDS];
	char		*m_type;
	char		*m_source;
	char		*m_superopts;

	int		m_sort_id;
	int		m_depth;

	avl_node_t	m_sort_node;
	avl_node_t	m_mp_node;
} mount_t;

static int
mountcache_compare_id(const void *a, const void *b)
{
	const mount_t *ma = a;
	const mount_t *mb = b;

	return (TREE_CMP(ma->m_id, mb->m_id));
}

static int
mountcache_compare_sort_id(const void *a, const void *b)
{
	const mount_t *ma = a;
	const mount_t *mb = b;

	return (TREE_CMP(ma->m_sort_id, mb->m_sort_id));
}

static int
mountcache_compare_mp(const void *a, const void *b)
{
	const mount_t *ma = a;
	const mount_t *mb = b;

	int cmp = strcmp(ma->m_mountpoint, mb->m_mountpoint);
	if (cmp != 0)
		return (TREE_ISIGN(cmp));

	return (TREE_CMP(ma->m_sort_id, mb->m_sort_id));
}

static void
mountcache_assign_sort(avl_tree_t *idt, mount_t *m,
    int *sort_id, int depth)
{
	m->m_sort_id = (*sort_id)++;
	m->m_depth = depth++;

	for (mount_t *cm = avl_first(idt); cm != NULL;
	    cm = AVL_NEXT(idt, cm)) {
		if (cm->m_parent == m->m_id)
			mountcache_assign_sort(idt, cm, sort_id, depth);
	}
}

static void
mountcache_free_mount(mount_t *m) {
	free(m->m_root);
	free(m->m_mountpoint);
	free(m->m_mountopts);
	free(m->m_type);
	free(m->m_source);
	free(m->m_superopts);
	for (int i = 0; i < MAX_FIELDS && m->m_fields[i] != NULL; i++)
		free(m->m_fields[i]);
	free(m);
}

static int
mountcache_load_data(avl_tree_t *sort_tree, avl_tree_t *mp_tree)
{
	avl_tree_t id_tree;
	avl_create(&id_tree, mountcache_compare_id, sizeof (mount_t),
	    offsetof(mount_t, m_mp_node));

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

		const char *fields[MAX_FIELDS] = {};
		int fidx = 0;
		const char *field;
		while ((field = strsep(&p, WHITESPACE)) != NULL) {
			if (*field == '-')
				break;
			if (fidx < MAX_FIELDS)
				fields[fidx++] = field;
		}

		const char *type = strsep(&p, WHITESPACE);
		const char *source = strsep(&p, WHITESPACE);
		const char *superopts = strsep(&p, WHITESPACE);

		uint64_t nid; strtou64(id, &nid);
		uint64_t nparent; strtou64(parent, &nparent);

		uint32_t nmajor; strtou32(major, &nmajor);
		uint32_t nminor; strtou32(minor, &nminor);

		mount_t *m = calloc(1, sizeof (mount_t));
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

		for (int i = 0; i < MAX_FIELDS && fields[i] != NULL; i++)
			m->m_fields[i] = strdup(fields[i]);

		avl_add(&id_tree, m);
	}

	if (err != 0 && ferror(f))
		err = errno;

	if (line != NULL)
		free(line);

	fclose(f);

	if (err != 0) {
		mount_t *m;
		void *cookie = NULL;
		while ((m = avl_destroy_nodes(&id_tree, &cookie)) != NULL) {
			mountcache_free_mount(m);
		}
		avl_destroy(&id_tree);
		return (err);
	}

	int sort_id = 0;

	mount_t search = {};
	for (mount_t *root = avl_first(&id_tree); root;
	    root = AVL_NEXT(&id_tree, root)) {
		boolean_t is_root = root->m_id == root->m_parent;
		if (!is_root) {
			search.m_id = root->m_parent;
			if (!avl_find(&id_tree, &search, NULL))
				is_root = B_TRUE;
		}
		if (!is_root)
			continue;

		mountcache_assign_sort(&id_tree, root, &sort_id, 0);
	}

	avl_create(sort_tree, mountcache_compare_sort_id, sizeof (mount_t),
	    offsetof(mount_t, m_sort_node));
	avl_create(mp_tree, mountcache_compare_mp, sizeof (mount_t),
	    offsetof(mount_t, m_mp_node));

	mount_t *m;
	void *cookie = NULL;
	while ((m = avl_destroy_nodes(&id_tree, &cookie)) != NULL) {
		avl_add(sort_tree, m);
		avl_add(mp_tree, m);
	}
	avl_destroy(&id_tree);

	return (0);
}

static void
mountcache_free_data(avl_tree_t *sort_tree, avl_tree_t *mp_tree)
{
	mount_t *m;
	void *cookie = NULL;
	while ((m = avl_destroy_nodes(sort_tree, &cookie)) != NULL) {
		avl_remove(mp_tree, m);
		mountcache_free_mount(m);
	}
	avl_destroy(sort_tree);
	avl_destroy(mp_tree);
}

struct mountcache {
	avl_tree_t mc_sort_tree;
	avl_tree_t mc_mp_tree;
};

int
mountcache_init(mountcache_t **mcp)
{
	mountcache_t *mc = malloc(sizeof (mountcache_t));
	int err = mountcache_load_data(&mc->mc_sort_tree, &mc->mc_mp_tree);
	if (err != 0) {
		free(mc);
		return (err);
	}
	*mcp = mc;
	return (0);
}

void
mountcache_free(mountcache_t *mc)
{
	mountcache_free_data(&mc->mc_sort_tree, &mc->mc_mp_tree);
	free(mc);
}

int
mountcache_refresh(mountcache_t *mc)
{
	avl_tree_t sort_tree, mp_tree;
	int err = mountcache_load_data(&sort_tree, &mp_tree);
	if (err != 0)
		return (err);
	avl_swap(&mc->mc_sort_tree, &sort_tree);
	avl_swap(&mc->mc_mp_tree, &mp_tree);
	mountcache_free_data(&sort_tree, &mp_tree);
	return (0);
}

void
mountcache_dump(mountcache_t *mc)
{
	printf("%-4s  %-4s  %-15s  %s\n", "ID", "UP", "TYPE", "MOUNTPOINT");
	for (mount_t *m = avl_first(&mc->mc_mp_tree); m != NULL;
	    m = AVL_NEXT(&mc->mc_mp_tree, m)) {
		printf("%4lu  %4lu  %-15s  %.*s%s\n",
		    m->m_id, m->m_parent, m->m_type,
		    m->m_depth * 2,
		    "                                        ",
		    m->m_mountpoint);
	}
	printf("\n");
}
