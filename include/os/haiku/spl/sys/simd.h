#ifndef _SPL_SIMD_H
#define _SPL_SIMD_H

#define	kfpu_allowed()		0
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)

/* simd_x86.h */
#define zfs_movbe_available()		0
#define zfs_sse2_available()		0
#define zfs_ssse3_available()		0
#define zfs_sse4_1_available()		0
#define zfs_shani_available()		0
#define zfs_sha512ext_available()	0
#define zfs_pclmulqdq_available()	0
#define zfs_vpclmulqdq_available()	0
#define	zfs_aes_available()		0
#define	zfs_vaes_available()		0
#define zfs_avx_available()		0
#define zfs_avx2_available()		0
#define zfs_avx512f_available()		0
#define zfs_avx512vl_available()	0

extern void simd_stat_init(void);
extern void simd_stat_fini(void);

#endif
