#ifndef _LIBSPL_SYS_STAT_H
#define	_LIBSPL_SYS_STAT_H

#include_next <sys/stat.h>

/*
 * Emulate Solaris' behavior of returning the block device size in fstat64().
 */
static inline int
fstat64_blk(int fd, struct stat64 *st)
{
  if (fstat64(fd, st) == -1)
    return (-1);

  if (S_ISBLK(st->st_mode)) {
    // XXX TD_GETGEOMETRY
    return (-1);
  }

  return (0);
}

#endif /* _LIBSPL_SYS_STAT_H */
