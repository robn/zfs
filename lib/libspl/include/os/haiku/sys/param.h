/*
 * HAIKU PORTING NOTES:
 * - just targets of opportunity, filled in one at a time.
 * - initially avoiding sys/param.h and unistd.h, even though Linux & FreeBSD
 *   too, because param vs params vs sysmacros vs ccompile is dogs breakfast
 *   and I'd like to take the oppoertunity to see if we can work out what's
 *   actually used.
 * - right, along the way I find out that there is a generic sys/sysmacros.h
 *   in libspl that also has MIN, MAX, and P2ROUNDUP (properly gated). so
 *   those are commented out below until I'm sure, and then will be removed.
 *   I guess those callers will need to include sys/sysmacros.h directly.
 * - there weren't any callers, just the PAGESIZE/limits thing below
 */

#ifndef _LIBSPL_SYS_PARAM_H
#define	_LIBSPL_SYS_PARAM_H

#include <sys/types.h>	/* for size_t, for spl_pagesize(), sigh */

#define	MAXNAMELEN	256

/*
 * in sysmacros on Linux, but libspl sys/types.h even includes sys/param.h
 * with the comment "for NBBY" so.
 */
#define	NBBY		8

/*
 * Haiku has it in /system/develop/headers/posix/sys/param.h, and matches
 * it to PATH_MAX. Doing it direct here, and now I think there's probably
 * a case for which of all the "old" names these days have POSIX equivalents
 * and we can just uplift the lot to that.
 */
#define	MAXPATHLEN	PATH_MAX

/*
 * lib/libspl/page.c, just memoising sysconf(_SC_PAGESIZE). Note that the
 * PAGESIZE undef is on Linux and FreeBSD too, and sees needed here as well,
 * at least because skein.c -> sys/sysmacros.h -> posix/limits.h ->
 * posix/arch/x86_64/limits.h -> PAGESIZE = 4096, then skein.c -> sys/types.h
 * ends up here. That's zero from three, suggests we should just not set
 * PAGESIZE anymore (I should check if C or POSIX owns it, given limits.h).
 */
#ifdef	PAGESIZE
#undef	PAGESIZE
#endif /* PAGESIZE */

extern size_t spl_pagesize(void);
#define	PAGESIZE	(spl_pagesize())

/* Present here on on Linux and FreeBSD */
#define	ptob(x)		((x) * PAGESIZE)

#if 0
// ifdef'd out, see porting notes

/* In sys/param.h on Haiku, gated. In sysmacros.h on Linux & FreeBSD, also
 * gated. Getting the sense that no one knows where this belongs? So lobbing
 * it here, ungated, to see what happens.
 */
#define	MIN(a,b)	(((a) < (b)) ? (a) : (b))
#define	MAX(a,b)	(((a) > (b)) ? (a) : (b))

/*
 * In sysmacros.h on Linux & FreeBSD. Comments suggest they might have come
 * from Solaris originally. Haiku sys/param.h has _ALIGNBYTES and _ALIGN(p),
 * so having these here is probably justifiable, but if these are truly
 * OpenZFS- (Solaris-)isms, then we should home them properly.
 */
/* Bringing them over one at a time as the compile needs them. */
#define	P2ROUNDUP(x, align)	((((x) - 1) | ((align) - 1)) + 1)
#endif

/* In platform sysmacros and/or ccompile. Maybe just in the generic. */
#define	ISP2(x)			(((x) & ((x) - 1)) == 0)

/*
 * In ccompile on FreeBSD. Haiku has the BSD-ish nitems() in sys/param.h,
 * and B_COUNT_OF() in os/support/SupportDefs.h. Hand rolled for the moment.
 */
#define	ARRAY_SIZE(a) (sizeof (a) / sizeof (a[0]))

/*
 * These are in ccompile on FreeBSD, and I guess in libc on Linux? I suspect
 * we'd do better to switch over to the regular versions across the board.
 */
#define	open64 open
#define	stat64 stat
#define	fstat64 fstat
#define	pwrite64 pwrite
#define	pread64 pread
#define	readdir64 readdir
#define	dirent64 dirent

/* In platform-specific sysmacros, same for both. */
#define	howmany(x, y)	(((x)+((y)-1))/(y))
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))

/* Linux sysmacros */
#define	DEV_BSIZE	512
#define	DEV_BSHIFT	9

#endif
