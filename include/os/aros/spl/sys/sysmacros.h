#ifndef _SYS_SYSMACROS_H
#define _SYS_SYSMACROS_H

#ifdef __cplusplus
extern "C" {
#endif

/* common macros */
#ifndef	ARRAY_SIZE
#define	ARRAY_SIZE(a) (sizeof (a) / sizeof (a[0]))
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

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SYSMACROS_H */
