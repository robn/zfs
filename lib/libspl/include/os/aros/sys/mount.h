#ifndef _LIBSPL_SYS_MOUNT_H
#define _LIBSPL_SYS_MOUNT_H

#include_next <sys/mount.h>

/*
 * AROS doesn't have mounts in the way that POSIX thinks about them so this is
 * just made-up stuff to get things compiling for now.
 */
#define  MS_FORCE    0x00000001
#define  MS_DETACH   0x00000002
#define  MS_OVERLAY  0x00000004
#define  MS_CRYPT    0x00000008

/* AROS statfs isn't actually 64-bit capable, but this gets us compiling. */
#define statfs64 statfs

#endif /* _LIBSPL_SYS_MOUNT_H */
