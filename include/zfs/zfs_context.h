// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

#ifndef _ZFS_ZFS_CONTEXT_H
#define	_ZFS_ZFS_CONTEXT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This code compiles in three different contexts. When __KERNEL__ is defined,
 * the code uses "unix-like" kernel interfaces. When _STANDALONE is defined, the
 * code is running in a reduced capacity environment of the boot loader which is
 * generally a subset of both POSIX and kernel interfaces (with a few unique
 * interfaces too). When neither are defined, it's in a userland POSIX or
 * similar environment.
 */
#if defined(__KERNEL__) || defined(_STANDALONE)
#include <spl/types.h>
#include <spl/atomic.h>
#include <spl/sysmacros.h>
#include <spl/vmsystm.h>
#include <spl/condvar.h>
#include <spl/cmn_err.h>
#include <spl/kmem.h>
#include <spl/kmem_cache.h>
#include <spl/vmem.h>
#include <spl/misc.h>
#include <spl/taskq.h>
#include <spl/param.h>
#include <spl/disp.h>
#include <spl/debug.h>
#include <spl/random.h>
#include <spl/string.h>
#include <spl/byteorder.h>
#include <spl/list.h>
#include <spl/time.h>
#include <spl/zone.h>
#include <spl/kstat.h>
#include <zfs/zfs_debug.h>
#include <zfs/sysevent.h>
#include <zfs/sysevent/eventdefs.h>
#include <zfs/zfs_delay.h>
#include <spl/sunddi.h>
#include <spl/ctype.h>
#include <spl/disp.h>
#include <spl/trace.h>
#include <spl/procfs_list.h>
#include <spl/mod.h>
#include <zfs/uio_impl.h>
#include <zfs/zfs_context_os.h>
#else /* _KERNEL || _STANDALONE */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <setjmp.h>
#include <assert.h>
#include <limits.h>
#include <atomic.h>
#include <dirent.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>
#include <sys/mman.h>
#include <spl/types.h>
#include <spl/cred.h>
#include <spl/sysmacros.h>
#include <sys/resource.h>
#include <spl/byteorder.h>
#include <spl/list.h>
#include <spl/mod.h>
#include <spl/uio.h>
#include <zfs/zfs_debug.h>
#include <spl/kstat.h>
#include <zfs/u8_textprep.h>
#include <zfs/sysevent.h>
#include <zfs/sysevent/eventdefs.h>
#include <spl/sunddi.h>
#include <spl/debug.h>
#include <spl/zone.h>

#include <spl/mutex.h>
#include <spl/rwlock.h>
#include <spl/condvar.h>
#include <spl/cmn_err.h>
#include <spl/thread.h>
#include <spl/taskq.h>
#include <spl/tsd.h>
#include <spl/procfs_list.h>
#include <spl/kmem.h>
#include <zfs/zfs_delay.h>
#include <spl/vnode.h>
#include <spl/callb.h>
#include <spl/trace.h>
#include <spl/systm.h>
#include <spl/misc.h>
#include <spl/random.h>

#include <zfs/zfs_context_os.h>

#endif  /* _KERNEL || _STANDALONE */

#ifdef __cplusplus
};
#endif

#endif	/* _ZFS_ZFS_CONTEXT_H */
