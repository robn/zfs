#ifndef _SPL_VMEM_H
#define	_SPL_VMEM_H

#include <stdlib.h>

#define	vmem_alloc(sz, fl)	malloc(sz)
#define	vmem_zalloc(sz, fl)	calloc(1, (sz))
#define	vmem_free(ptr, sz)	free(ptr)

#endif
