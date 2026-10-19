// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright (c) 2026, TrueNAS.
 */

#include <sys/owner.h>
#include <atomic.h>

static __thread void *tag;

void
spl_owner_set(spl_owner_t *so)
{
	atomic_store_ptr(&so->so_tag, &tag);
}

void
spl_owner_clear(spl_owner_t *so)
{
	atomic_store_ptr(&so->so_tag, NULL);
}

boolean_t
spl_owned(const spl_owner_t *so)
{
	return (atomic_load_ptr(&so->so_tag) == &tag);
}
