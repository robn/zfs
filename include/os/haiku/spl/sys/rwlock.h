#ifndef _SPL_RWLOCK_H
#define	_SPL_RWLOCK_H

typedef enum {
	RW_NONE		= 0,
	RW_WRITER	= 1,
	RW_READER	= 2
} krw_t;

#endif
