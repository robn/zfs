/*
 * HAIKU PORTING NOTES:
 * - stubs for libzfs
 */

#include <libzfs.h>
#include "../../libzfs_impl.h"

int
zpool_label_disk(libzfs_handle_t *hdl, zpool_handle_t *zhp, const char *name)
{
	(void) hdl, (void) zhp, (void) name;
	return (0);
}

int
zpool_relabel_disk(libzfs_handle_t *hdl, const char *path, const char *msg)
{
	(void) hdl, (void) path, (void) msg;
	return (0);
}
