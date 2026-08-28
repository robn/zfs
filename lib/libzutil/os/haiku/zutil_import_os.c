/*
 * HAIKU PORTING NOTES:
 * - for now, just no-ops following the FreeBSD line
 */

#include <sys/avl.h>

#include <libzutil.h>

#include "zutil_import.h"

void
update_vdev_config_dev_strs(nvlist_t *nv)
{
	(void) nv;
}

boolean_t
zpool_dev_probe_ok(const char *path)
{
	(void) path;
	return (B_TRUE);
}

boolean_t
zpool_dev_probe_ok_fd(int fd)
{
	(void) fd;
	return (B_TRUE);
}

void
zpool_open_func(void *arg)
{
	(void) arg;
	return;
}

const char * const *
zpool_default_search_paths(size_t *count)
{
	*count = 0;
	return (NULL);
}

int
zpool_find_import_blkid(libpc_handle_t *hdl, pthread_mutex_t *lock,
    avl_tree_t **slice_cache)
{
	(void) hdl, (void) lock, (void) slice_cache;
	return (ENOSYS);
}

void
update_vdevs_config_dev_sysfs_path(nvlist_t *config)
{
	(void) config;
}
