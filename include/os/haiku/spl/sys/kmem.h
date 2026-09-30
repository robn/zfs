#ifndef _SPL_KMEM_H
#define	_SPL_KMEM_H

#include <stdlib.h>

#define	kmem_alloc(sz, fl)	malloc(sz)
#define	kmem_zalloc(sz, fl)	calloc(1, (sz))
#define	kmem_free(ptr, sz)	free(ptr)

#define	KM_SLEEP		(0)
#define	KM_NOSLEEP		(1)

#endif
