#ifndef _SPL_CONDVAR_H
#define	_SPL_CONDVAR_H

#include <sys/mutex.h>

typedef uintptr_t kcondvar_t;

#define	CV_DEFAULT		0

extern void cv_init(kcondvar_t *cv, char *name, int type, void *arg);
extern void cv_destroy(kcondvar_t *cv);
extern void cv_wait(kcondvar_t *cv, kmutex_t *mp);

#endif
