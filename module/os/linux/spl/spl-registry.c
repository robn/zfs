#include <sys/registry.h>
#include <sys/string.h>
#include <sys/atomic.h>
#include <sys/kmem.h>
#include <linux/rwsem.h>
#include <linux/hashtable.h>
#include <linux/jhash.h>

static DEFINE_HASHTABLE(spl_registry, 8);
static DECLARE_RWSEM(spl_registry_lock);

typedef struct {
	char			ri_name[32];
	void			*ri_ptr;
	uint64_t		ri_refcnt;
	struct hlist_node	ri_hlist;
	uint32_t		ri_hash;
} spl_registry_item_t;

static spl_registry_item_t *
spl_registry_lookup(const char *name)
{
	char truncname[32];
	strlcpy(truncname, name, sizeof (truncname));
	uint32_t hash = jhash(truncname, strlen(truncname), 0);

	spl_registry_item_t *ri;
	hash_for_each_possible(spl_registry, ri, ri_hlist, hash) {
		if (strcmp(ri->ri_name, truncname) == 0)
			return (ri);
	}

	return (NULL);
}

int
spl_registry_register(const char *name, void *ptr)
{
	spl_registry_item_t *ri =
	    kmem_alloc(sizeof (spl_registry_item_t), KM_SLEEP);
	strlcpy(ri->ri_name, name, sizeof (ri->ri_name));
	ri->ri_ptr = ptr;
	ri->ri_refcnt = 0;
	ri->ri_hash = jhash(ri->ri_name, strlen(ri->ri_name), 0);

	down_write(&spl_registry_lock);
	if (spl_registry_lookup(name)) {
		up_write(&spl_registry_lock);
		kmem_free(ri, sizeof (spl_registry_item_t));
		return (EEXIST);
	}

	hash_add(spl_registry, &ri->ri_hlist, ri->ri_hash);
	up_write(&spl_registry_lock);

	return (0);
}

int
spl_registry_deregister(const char *name, void *ptr)
{
	int err = 0;
	down_write(&spl_registry_lock);
	spl_registry_item_t *ri = spl_registry_lookup(name);
	if (ri == NULL || ri->ri_ptr != ptr)
		err = ENOENT;
	else if (ri->ri_refcnt > 0)
		err = EBUSY;
	if (err == 0)
		hash_del(&ri->ri_hlist);
	up_write(&spl_registry_lock);
	if (err == 0)
		kmem_free(ri, sizeof (spl_registry_item_t));
	return (err);
}

int
spl_registry_hold(const char *name, void **ptrp)
{
	int err = 0;
	down_read(&spl_registry_lock);
	spl_registry_item_t *ri = spl_registry_lookup(name);
	if (ri == NULL)
		err = ENOENT;
	else {
		atomic_inc_64(&ri->ri_refcnt);
		*ptrp = ri->ri_ptr;
	}
	up_read(&spl_registry_lock);
	return (err);
}

int
spl_registry_rele(const char *name, void *ptr)
{
	int err = 0;
	down_read(&spl_registry_lock);
	spl_registry_item_t *ri = spl_registry_lookup(name);
	if (ri == NULL || ri->ri_ptr != ptr)
		err = ENOENT;
	else
		atomic_dec_64(&ri->ri_refcnt);
	up_read(&spl_registry_lock);
	return (err);
}

EXPORT_SYMBOL(spl_registry_register);
EXPORT_SYMBOL(spl_registry_deregister);
EXPORT_SYMBOL(spl_registry_hold);
EXPORT_SYMBOL(spl_registry_rele);
