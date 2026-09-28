#ifndef _SPL_REGISTRY_H
#define _SPL_REGISTRY_H

int spl_registry_register(const char *name, void *ptr);
int spl_registry_deregister(const char *name, void *ptr);

int spl_registry_hold(const char *name, void **ptrp);
int spl_registry_rele(const char *name, void *ptr);

#endif
