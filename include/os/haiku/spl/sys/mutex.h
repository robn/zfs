#ifndef _SPL_MUTEX_H
#define _SPL_MUTEX_H

typedef uintptr_t kmutex_t;

#define	MUTEX_DEFAULT		0

extern void spl_mutex_init(kmutex_t *m, char *name, int type, void *arg);
extern void spl_mutex_destroy(kmutex_t *m);
extern void spl_mutex_enter(kmutex_t *m);
extern int spl_mutex_enter_interruptible(kmutex_t *m);
extern void spl_mutex_enter_nested(kmutex_t *m, unsigned int subclass);
extern int spl_mutex_tryenter(kmutex_t *m);
extern void spl_mutex_exit(kmutex_t *m);
extern int spl_mutex_owned(kmutex_t *m);
extern int spl_mutex_owner(kmutex_t *m);

#define	mutex_init(lock, desc, type, arg)	spl_mutex_init(lock, desc, type, arg)
#define	mutex_destroy(lock)			spl_mutex_destroy(lock)
#define	mutex_enter(lock)			spl_mutex_enter(lock)
#define mutex_enter_interruptible(lock)		spl_mutex_enter_interruptible(lock)
#define mutex_enter_nested(lock, type)		spl_mutex_enter_nested(lock)
#define mutex_tryenter(lock)			spl_mutex_tryenter(lock)
#define mutex_exit(lock)			spl_mutex_exit(lock)
#define mutex_owned(lock)			spl_mutex_owned(lock)
#define mutex_owner(lock)			spl_mutex_owner(lock)

#endif
