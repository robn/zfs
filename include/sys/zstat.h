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

#ifndef _ZSTAT_H
#define	_ZSTAT_H

#include <sys/types.h>
#include <sys/kstat.h>
#include <sys/wmsum.h>

typedef enum ztat_type {
	ZSTAT_TYPE_COUNTER,
} zstat_type_t;

typedef struct zstat_def {
	const char	*zst_name;
	zstat_type_t	zst_type;
} zstat_def_t;

typedef struct zstat {
	uint_t		zst_nstat;
	kstat_t		*zst_ksp;
	wmsum_t		zst_sums[];
} zstat_t;

zstat_t *zstat_create(const zstat_def_t *def, uint_t ndef);
void zstat_destroy(zstat_t *zst);

static inline void zstat_inc(zstat_t *zst, uint_t n) {
	wmsum_add(&zst->zst_sums[n], 1);
}

#endif
