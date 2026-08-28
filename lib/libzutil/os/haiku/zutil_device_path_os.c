/*
 * HAIKU PORTING NOTES:
 * - for now, just no-ops following the FreeBSD line
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
