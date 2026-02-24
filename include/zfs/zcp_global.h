// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

#ifndef _ZFS_ZCP_GLOBAL_H
#define	_ZFS_ZCP_GLOBAL_H

#include <zfs/lua/lua.h>

#ifdef	__cplusplus
extern "C" {
#endif

void zcp_load_globals(lua_State *);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_ZCP_GLOBAL_H */
