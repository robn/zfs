/*
 * HAIKU PORTING NOTES:
 * - brand new
 */

#ifndef _LIBSPL_SYS_ERRNO_H
#define	_LIBSPL_SYS_ERRNO_H

/*
 * /system/develop/headers/os/support/Errors.h seems to define everything.
 * I'm making it up as I go along, but the _BASE ranges defined there seem
 * to just be on paper, because there's good evidence that the error codes
 * are all just int anyway, and there's mappings between ranges (eg most
 * POSIX errors are in the B_POSIX_ERROR_BASE range, but some are mapped
 * into the GENERAL range.
 *
 * However, it seems that POSIX errors might be negative or positive depending
 * on configuration, so we need to use toe B_TO_POSIX_ERROR() to make sure
 * it matches its true counterparts.
 *
 * Decision time:
 * - ECKSUM -> B_BAD_DATA (from the general range)
 * - ENOTACTIVE -> ECANCELED (matches FreeBSD)
 * - (EFRAGS never used in OpenZFS, not bothering)
 */

#include <errno.h>

#define	ECKSUM		B_TO_POSIX_ERROR(B_BAD_DATA)
#define	ENOTACTIVE	ECANCELED

#endif /* _LIBSPL_SYS_ERRNO_H */
