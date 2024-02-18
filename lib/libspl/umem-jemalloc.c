/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright (c) 2024 Rob Norris <robn@despairlabs.com>
 */

#include <umem.h>

#include <stddef.h>
#include <pthread.h>
#include <sys/list.h>
#include <atomic.h>

#include <jemalloc/jemalloc.h>

struct umem_cache {
	char			uc_name[UMEM_CACHE_NAMELEN + 1];
	size_t			uc_bufsize;
	size_t			uc_align;
	umem_constructor_t	*uc_constructor;
	umem_destructor_t	*uc_destructor;
	umem_reclaim_t		*uc_reclaim;
	void			*uc_private;
	unsigned		uc_je_arena;
	int			uc_je_flags;
	list_node_t		uc_node;

	uint64_t		uc_alloc_objs;
	uint64_t		uc_alloc_bytes;
};

list_t umem_cache_list;
pthread_mutex_t umem_cache_lock = PTHREAD_MUTEX_INITIALIZER;


umem_cache_t *
umem_cache_create(
    const char *name, size_t bufsize, size_t align,
    umem_constructor_t *constructor,
    umem_destructor_t *destructor,
    umem_reclaim_t *reclaim,
    void *priv, void *vmp, int cflags)
{
	(void) vmp;
	(void) cflags;

	if (name == NULL || !ISP2(align) || bufsize == 0)
		return (NULL);

	unsigned arena;
	size_t len = sizeof (arena);
	VERIFY0(mallctl("arenas.create", &arena, &len, NULL, 0));

	umem_cache_t *uc;
	uc = (umem_cache_t *)umem_zalloc(sizeof (umem_cache_t), UMEM_DEFAULT);
	if (uc == NULL)
		/* XXX destroy arena */
		return (NULL);

	strlcpy(uc->uc_name, name, UMEM_CACHE_NAMELEN);
	uc->uc_bufsize = bufsize;
	uc->uc_align = align;
	uc->uc_constructor = constructor;
	uc->uc_destructor = destructor;
	uc->uc_reclaim = reclaim;
	uc->uc_private = priv;
	uc->uc_je_arena = arena;
	uc->uc_je_flags =
	    MALLOCX_ARENA(arena) | MALLOCX_TCACHE_NONE |
	    ((align > 0) ? MALLOCX_ALIGN(align) : (0));

	atomic_store_64(&uc->uc_alloc_objs, 0);
	atomic_store_64(&uc->uc_alloc_bytes, 0);

	pthread_mutex_lock(&umem_cache_lock);
	list_insert_tail(&umem_cache_list, uc);
	pthread_mutex_unlock(&umem_cache_lock);

	return (uc);
}

void
umem_cache_destroy(umem_cache_t *uc)
{
	char mib[256] = {0};
	snprintf(mib, sizeof (mib), "arena.%d.destroy", uc->uc_je_arena);
	VERIFY0(mallctl(mib, NULL, NULL, NULL, 0));

	pthread_mutex_lock(&umem_cache_lock);
	list_remove(&umem_cache_list, uc);
	pthread_mutex_unlock(&umem_cache_lock);

	umem_free(uc, sizeof (umem_cache_t));
}

void *
umem_cache_alloc(umem_cache_t *uc, int flags)
{
	void *ptr = NULL;

	do {
		ptr = mallocx(uc->uc_bufsize, uc->uc_je_flags);
	} while (ptr == NULL && (flags & UMEM_NOFAIL));

	if (ptr != NULL && uc->uc_constructor != NULL)
		uc->uc_constructor(ptr, uc->uc_private, UMEM_DEFAULT);

	atomic_inc_64(&uc->uc_alloc_objs);
	atomic_add_64(&uc->uc_alloc_bytes, uc->uc_bufsize);

	return (ptr);
}

void
umem_cache_free(umem_cache_t *uc, void *ptr)
{
	if (uc->uc_destructor != NULL)
		uc->uc_destructor(ptr, uc->uc_private);

	sdallocx(ptr, uc->uc_bufsize, uc->uc_je_flags);

	atomic_sub_64(&uc->uc_alloc_bytes, uc->uc_bufsize);
	atomic_dec_64(&uc->uc_alloc_objs);
}

void
umem_cache_reap_now(umem_cache_t *uc)
{
	(void) uc;
}


void
umem_cache_dump(void)
{
	umem_cache_t *uc;

	fprintf(stderr, "%3s  %-32s  %8s  %8s  %10s\n",
	    "", "CACHE", "OBJS", "OBJSIZE", "SIZE");

	pthread_mutex_lock(&umem_cache_lock);
	for (uc = list_head(&umem_cache_list); uc != NULL;
	    uc = list_next(&umem_cache_list, uc)) {
		fprintf(stderr, "%3d  %-32s  %8lu  %8lu  %10lu\n",
		    uc->uc_je_arena, uc->uc_name,
		    uc->uc_alloc_objs, uc->uc_bufsize, uc->uc_alloc_bytes);
	}
	pthread_mutex_unlock(&umem_cache_lock);
}


void *
umem_alloc(size_t size, int flags)
{
	void *ptr = NULL;

	if (size == 0)
		return (NULL);

	do {
		ptr = mallocx(size, MALLOCX_TCACHE_NONE);
	} while (ptr == NULL && (flags & UMEM_NOFAIL));

	return (ptr);
}

void *
umem_alloc_aligned(size_t size, size_t align, int flags)
{
	void *ptr = NULL;

	if (size == 0)
		return (NULL);

	VERIFY3U(align, >, 0);
	VERIFY(ISP2(align));

	do {
		ptr = mallocx(size, MALLOCX_ALIGN(align) | MALLOCX_TCACHE_NONE);
	} while (ptr == NULL && (flags & UMEM_NOFAIL));

	return (ptr);
}

void *
umem_zalloc(size_t size, int flags)
{
	void *ptr = NULL;

	if (size == 0)
		return (NULL);

	do {
		ptr = mallocx(size, MALLOCX_ZERO | MALLOCX_TCACHE_NONE);
	} while (ptr == NULL && (flags & UMEM_NOFAIL));

	return (ptr);
}

void
umem_free(const void *ptr, size_t size)
{
	if (ptr != NULL)
		sdallocx((void *)ptr, size, 0);
}

void
umem_free_aligned(void *ptr, size_t size)
{
	if (ptr != NULL)
		sdallocx(ptr, size, 0);
}

void
umem_nofail_callback(umem_nofail_callback_t *cb)
{
	(void) cb;
}

int umem_loaded = 0;

static void
umem_init(void)
{
	if (umem_loaded++)
		return;

	list_create(&umem_cache_list,
	    sizeof (umem_cache_t), offsetof(umem_cache_t, uc_node));
}

static void
umem_fini(void)
{
	if (--umem_loaded)
		return;

	/* XXX make configurable (envvar? sig handler?) */
	umem_cache_dump();

	/* XXX will assert because still stuff on list. need a proper cleanup */
	/* list_destroy(&umem_cache_list); */
}

#ifdef __GNUC__
static void
umem_init(void) __attribute__((constructor));
static void
umem_fini(void) __attribute__((destructor));
#else
#pragma init(umem_init)
#pragma fini(umem_fini);
#endif
