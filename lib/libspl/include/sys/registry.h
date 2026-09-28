#ifndef _SPL_REGISTRY_H
#define _SPL_REGISTRY_H

static inline int
spl_registry_register(const char *name, void *ptr)
{
	(void) name, (void) ptr;
	return (ENOSYS);
}

static inline int
spl_registry_deregister(const char *name, void *ptr)
{
	(void) name, (void) ptr;
	return (ENOSYS);
}

static inline int
spl_registry_hold(const char *name, void **ptrp)
{
	(void) name, (void) ptrp;
	return (ENOSYS);
}

static inline int
spl_registry_rele(const char *name, void *ptr)
{
	(void) name, (void) ptr;
	return (ENOSYS);
}

#endif
