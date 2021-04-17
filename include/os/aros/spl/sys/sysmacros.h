#ifndef _SYS_SYSMACROS_H
#define _SYS_SYSMACROS_H

#ifdef __cplusplus
extern "C" {
#endif

/* common macros */
#ifndef	ARRAY_SIZE
#define	ARRAY_SIZE(a) (sizeof (a) / sizeof (a[0]))
#endif
#ifndef	DIV_ROUND_UP
#define	DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#endif

/*
 * Macro for checking power of 2 address alignment.
 */
#define	IS_P2ALIGNED(v, a) ((((uintptr_t)(v)) & ((uintptr_t)(a) - 1)) == 0)

/*
 * Macro to determine if value is a power of 2
 */
#define	ISP2(x)		(((x) & ((x) - 1)) == 0)

/*
 * return x rounded up to an align boundary
 * eg, P2ROUNDUP(0x1234, 0x100) == 0x1300 (0x13*align)
 * eg, P2ROUNDUP(0x5600, 0x100) == 0x5600 (0x56*align)
 */
#define	P2ROUNDUP(x, align)		(-(-(x) & -(align)))

/*
 * Typed version of the P2* macros.  These macros should be used to ensure
 * that the result is correctly calculated based on the data type of (x),
 * which is passed in as the last argument, regardless of the data
 * type of the alignment.  For example, if (x) is of type uint64_t,
 * and we want to round it up to a page boundary using "PAGESIZE" as
 * the alignment, we can do either
 *	P2ROUNDUP(x, (uint64_t)PAGESIZE)
 * or
 *	P2ROUNDUP_TYPED(x, PAGESIZE, uint64_t)
 */
#define	P2ALIGN_TYPED(x, align, type)	\
	((type)(x) & -(type)(align))
#define	P2PHASE_TYPED(x, align, type)	\
	((type)(x) & ((type)(align) - 1))
#define	P2NPHASE_TYPED(x, align, type)	\
	(-(type)(x) & ((type)(align) - 1))
#define	P2ROUNDUP_TYPED(x, align, type)	\
	(-(-(type)(x) & -(type)(align)))
#define	P2END_TYPED(x, align, type)	\
	(-(~(type)(x) & -(type)(align)))
#define	P2PHASEUP_TYPED(x, align, phase, type)	\
	((type)(phase) - (((type)(phase) - (type)(x)) & -(type)(align)))
#define	P2CROSS_TYPED(x, y, align, type)	\
	(((type)(x) ^ (type)(y)) > (type)(align) - 1)
#define	P2SAMEHIGHBIT_TYPED(x, y, type) \
	(((type)(x) ^ (type)(y)) < ((type)(x) & (type)(y)))

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SYSMACROS_H */
