#ifndef _SPL_ATOMIC_H
#define _SPL_ATOMIC_H

#include <arch/atomic.h>

#define atomic_load_32(v)	atomic_get((int32*)(v))
#define atomic_swap_32(v, x)	atomic_get_and_set((int32*)(v), (x))

#endif
