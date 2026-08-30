/*
 * HAIKU PORTING NOTES:
 * - for now, just no-ops following the FreeBSD line
 * - very basic zpool_open_func for zdb mostly
 */

#include <fcntl.h>

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
	rdsk_node_t *rn = arg;

	int fd = open(rn->rn_name, O_RDONLY|O_CLOEXEC);
	if (fd == -1)
		return;

	/* XXX lots of checks and such */

	nvlist_t *config;
	int num_labels;
	if (zpool_read_label(fd, &config, &num_labels) != 0) {
		close(fd);
		return;
	}

	if (num_labels == 0) {
		nvlist_free(config);
		return;
	}

	rn->rn_config = config;
	rn->rn_num_labels = num_labels;
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

int
zfs_dev_flush(int fd)
{
	(void) fd;
	return (0);
}

void
update_vdev_config_dev_sysfs_path(nvlist_t *nv, const char *path,
    const char *key)
{
	(void) nv, (void) path, (void) key;
}

void
update_vdevs_config_dev_sysfs_path(nvlist_t *config)
{
	(void) config;
}

int
zpool_disk_wait(const char *path)
{
	(void) path;
	return (ENOSYS);
}

int
zpool_label_disk_wait(const char *path, int timeout_ms)
{
	(void) path, (void) timeout_ms;
	return (ENOSYS);
}
