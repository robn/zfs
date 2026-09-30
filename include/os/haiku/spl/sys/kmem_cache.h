#ifndef _SPL_KMEM_CACHE_H
#define	_SPL_KMEM_CACHE_H

typedef uintptr_t kmem_cache_t;

typedef int kmem_constructor_t(void *, void *, int);
typedef void kmem_destructor_t(void *, void *);
typedef void kmem_reclaim_t(void *);

extern kmem_cache_t *kmem_cache_create(const char *name,
    size_t bufsize, size_t align,
    kmem_constructor_t *constructor, kmem_destructor_t *destructor,
    kmem_reclaim_t *reclaim, void *priv, void *vmp, int cflags);
extern void kmem_cache_destroy(kmem_cache_t *cp);

extern void *kmem_cache_alloc(kmem_cache_t *cp, int flags);
extern void kmem_cache_free(kmem_cache_t *cp, void *ptr);

#endif
