#ifndef _ZFS_ABD_OS_H
#define	_ZFS_ABD_OS_H

struct abd_scatter {
	uint_t          abd_offset;
	uint_t          abd_iovcnt;
	struct iovec    abd_iov[1]; /* actually variable-length */
};

struct abd_linear {
	void            *abd_buf;
};

#endif
