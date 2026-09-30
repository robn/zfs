#ifndef _SPL_UIO_H
#define _SPL_UIO_H

#include_next <sys/uio.h>

typedef struct iovec iovec_t;

typedef enum zfs_uio_rw {
	UIO_READ =		0,
	UIO_WRITE =		1,
} zfs_uio_rw_t;

typedef enum zfs_uio_seg {
	UIO_SYSSPACE =	0,
} zfs_uio_seg_t;

typedef struct zfs_uio {
	struct iovec	*uio_iov;
	int		uio_iovcnt;
	zfs_uio_seg_t	uio_segflg;
} zfs_uio_t;

#define	zfs_uio_segflg(u)		(u)->uio_segflg
#define	zfs_uio_iovbase(u, idx)		(u)->uio_iov[(idx)].iov_base

#endif
