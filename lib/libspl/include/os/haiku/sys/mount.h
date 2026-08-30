/*
 * HAIKU PORTING NOTES:
 * - bare minimum to make it build. I have no idea what mounting looks like yet.
 * - umount2 come on
 */

#ifndef _LIBSPL_SYS_MOUNT_H
#define	_LIBSPL_SYS_MOUNT_H

#define	MS_FORCE	(1<<0)
#define	MS_CRYPT	(1<<1)
#define	MS_OVERLAY	(1<<2)

/*
 * hack, zfs_main calls umount2() direct, not through an OS layer. I'm
 * unreasonably upset about this.
 */
#define	umount2(p, f)	((void) p, (void) f, 0)

#endif /* _LIBSPL_SYS_MOUNT_H */
