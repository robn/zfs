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
 * Copyright (c) 2025 Rob Norris <robn@despairlabs.com>
 */

#include <umem.h>

#include <stddef.h>
#include <pthread.h>
#include <sys/list.h>
#include <sys/sysmacros.h>
#include <atomic.h>

#include <mimalloc.h>

struct umem_cache {
	char			uc_name[UMEM_CACHE_NAMELEN + 1];
	size_t			uc_bufsize;
	size_t			uc_align;
	umem_constructor_t	*uc_constructor;
	umem_destructor_t	*uc_destructor;
	umem_reclaim_t		*uc_reclaim;
	void			*uc_private;
};

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

	umem_cache_t *uc;
	uc = (umem_cache_t *)umem_zalloc(sizeof (umem_cache_t), UMEM_DEFAULT);
	if (uc == NULL)
		return (NULL);

	strlcpy(uc->uc_name, name, UMEM_CACHE_NAMELEN);
	uc->uc_bufsize = bufsize;
	uc->uc_align = align;
	uc->uc_constructor = constructor;
	uc->uc_destructor = destructor;
	uc->uc_reclaim = reclaim;
	uc->uc_private = priv;

	return (uc);
}

void
umem_cache_destroy(umem_cache_t *uc)
{
	umem_free(uc, sizeof (umem_cache_t));
}

void *
umem_cache_alloc(umem_cache_t *uc, int flags)
{
	void *ptr = NULL;

	do {
		ptr = (uc->uc_align == 0) ?
		    mi_malloc(uc->uc_bufsize) :
		    mi_malloc_aligned(uc->uc_bufsize, uc->uc_align);
	} while (ptr == NULL && (flags & UMEM_NOFAIL));

	if (ptr != NULL && uc->uc_constructor != NULL)
		uc->uc_constructor(ptr, uc->uc_private, UMEM_DEFAULT);

	return (ptr);
}

void
umem_cache_free(umem_cache_t *uc, void *ptr)
{
	if (uc->uc_destructor != NULL)
		uc->uc_destructor(ptr, uc->uc_private);
	mi_free(ptr);
}

void
umem_cache_reap_now(umem_cache_t *uc)
{
        (void) uc;
}

void *
umem_alloc(size_t size, int flags)
{
	void *ptr = NULL;

	if (size == 0)
		return (NULL);

	do {
		ptr = mi_malloc(size);
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
		ptr = mi_malloc_aligned(size, align);
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
		ptr = mi_zalloc(size);
	} while (ptr == NULL && (flags & UMEM_NOFAIL));

	return (ptr);
}

void
umem_free(const void *ptr, size_t size)
{
	(void) size;
	if (ptr != NULL)
		mi_free((void *)ptr);
}

void
umem_free_aligned(void *ptr, size_t size)
{
	(void) size;
	if (ptr != NULL)
		mi_free(ptr);
}

void
umem_nofail_callback(umem_nofail_callback_t *cb)
{
	(void) cb;
}
