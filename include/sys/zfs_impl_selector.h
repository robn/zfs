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
 * Copyright (c) 2022, Rob Norris <robn@despairlabs.com>
 */

#ifndef	_SYS_ZFS_IMPL_SELECTOR_H
#define	_SYS_ZFS_IMPL_SELECTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#define	MAX_IMPLS	(16)
#define IMPL_NAME_MAX	(16)

typedef struct {
	char			name[IMPL_NAME_MAX];
} zfs_impl_base_t;

typedef struct {
	zfs_impl_base_t	*	all_impl[MAX_IMPLS];
	zfs_impl_base_t	*	supp_impl[MAX_IMPLS];
	zfs_impl_base_t		*fastest_impl;
	boolean_t		initialized;
	size_t			supp_cnt;
	size_t			cycle_impl_idx;
} zfs_impl_selector_t;

#ifdef __cplusplus
};
#endif

#endif	/* _SYS_ZFS_IMPL_SELECTOR_H */
