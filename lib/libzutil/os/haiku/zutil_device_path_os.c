/*
 * HAIKU PORTING NOTES:
 * - for now, just no-ops following the FreeBSD line
 * - stub for zfs_dev_is_whole_disk
 */

#include <libzutil.h>

char *
zfs_strip_partition(const char *dev)
{
	return (strdup(dev));
}

int
zfs_append_partition(char *path, size_t max_len)
{
	return (strnlen(path, max_len));
}

const char *
zfs_strip_path(const char *path)
{
	return (path);
}

boolean_t
zfs_dev_is_whole_disk(const char *dev_name)
{
	(void) dev_name;
	return (B_TRUE);
}

char *
zfs_get_underlying_path(const char *dev_name)
{
	(void) dev_name;
	return (NULL);
}

boolean_t
is_mpath_whole_disk(const char *path)
{
	(void) path;
	return (B_FALSE);
}
