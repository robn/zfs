/*
 * HAIKU PORTING NOTES:
 * - zero from three on Solaris fstat compat on block size. another thing goes.
 */

#ifndef _LIBSPL_SYS_STAT_H
#define	_LIBSPL_SYS_STAT_H

#include_next <sys/stat.h>
#include <unistd.h>
#include <Drivers.h> /* for B_GET_DEVICE_SIZE */

static inline int
fstat64_blk(int fd, struct stat64 *st)
{
	if (fstat(fd, st) == -1)
		return (-1);

	/* In Linux we need to use an ioctl to get the size of a block device */
	if (S_ISBLK(st->st_mode)) {
		if (ioctl(fd, B_GET_DEVICE_SIZE,
		    &st->st_size, sizeof (st->st_size)) != 0)
			return (-1);
	}

	return (0);
}

#endif
