/*
 * HAIKU PORTING NOTES:
 * - stubs for libzfs
 */

#include <libzfs.h>
#include "../../libzfs_impl.h"

int
do_mount(zfs_handle_t *zhp, const char *mntpt, const char *opts, int flags)
{
	(void) zhp, (void) mntpt, (void) opts, (void) flags;
	return (ENOSYS);
}

int
do_unmount(zfs_handle_t *zhp, const char *mntpt, int flags)
{
	(void) zhp, (void) mntpt, (void) flags;
	return (ENOSYS);
}

int
zfs_mount_setattr(zfs_handle_t *zhp, uint32_t nspflags)
{
	(void) zhp, (void) nspflags;
	return (ENOSYS);
}

int
zfs_mount_delegation_check(void)
{
	return (0);
}

void
zpool_disable_datasets_os(zpool_handle_t *zhp, boolean_t force)
{
	(void) zhp, (void) force;
}

void
zpool_disable_volume_os(const char *name)
{
	(void) name;
}
