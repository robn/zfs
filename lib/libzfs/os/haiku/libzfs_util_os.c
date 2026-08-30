/*
 * HAIKU PORTING NOTES:
 * - stubs for libzfs
 */

#include <libzfs.h>
#include "../../libzfs_impl.h"

const char *
libzfs_error_init(int error)
{
	(void) error;
	return ("Failed to initialize the libzfs library.");
}

int
libzfs_load_module(void)
{
	return (0);
}

int
find_shares_object(differ_info_t *di)
{
	(void) di;
	return (ENOSYS);
}

int
zfs_destroy_snaps_nvl_os(libzfs_handle_t *hdl, nvlist_t *snaps)
{
	(void) hdl, (void) snaps;
	return (ENOSYS);
}

char *
zfs_version_kernel(void)
{
	return (NULL);
}
