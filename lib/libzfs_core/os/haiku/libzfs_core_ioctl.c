/*
 * HAIKU PORTING NOTES:
 * - just fail everything. need to find out what haiku's kernel control
 *   interface even looks like, but that cames later when we start building
 *   for the kernel.
 */

#include <sys/zfs_ioctl.h>
#include "libzfs_core_impl.h"

int
lzc_ioctl_fd_os(int fd, unsigned long request, zfs_cmd_t *zc)
{
	(void) fd, (void) request, (void) zc;
	return (ENOSYS);
}
