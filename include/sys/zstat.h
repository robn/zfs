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
	_ZSTAT_TYPE_OFFSET,
	_ZSTAT_TYPE_COUNTER,
} zstat_type_t;

#define	ZSTAT_COUNTER		_ZSTAT_TYPE_COUNTER

#define	_ZSTAT_GROUP(t, n)	\
	(((uint32_t)(t & 0xffff)) | (((uint32_t)n) << 16))
#define	ZSTAT_COUNTER_GROUP(n)	_ZSTAT_GROUP(_ZSTAT_TYPE_COUNTER, (n))

typedef struct zstat_def {
	const char	*zst_name;
	uint32_t	zst_type;
} zstat_def_t;

typedef struct zstat_slot {
	zstat_type_t	zst_type;
	union {
		uint_t		zst_offset;	/* offset to array of slots */
		wmsum_t		zst_counter;	/* counter type */
	};
} zstat_slot_t;

typedef struct zstat {
	uint_t		zst_nslots;
	kstat_t		*zst_ksp;
	zstat_slot_t	zst_slots[];
} zstat_t;

zstat_t *zstat_create(const char *name, const zstat_def_t *def, uint_t ndefs);
void zstat_destroy(zstat_t *zst);

static inline void _zstat_slot_add(zstat_slot_t *slot, int64_t v) {
	ASSERT3U(slot->zst_type, ==, _ZSTAT_TYPE_COUNTER);
	wmsum_add(&slot->zst_counter, v);
}
#define	zstat_add(zst, n, v)	_zstat_slot_add(&(zst)->zst_slots[(n)], v)
#define	zstat_sub(zst, n, v)	_zstat_slot_add(&(zst)->zst_slots[(n)], -v)
#define	zstat_inc(zst, n)	_zstat_slot_add(&(zst)->zst_slots[(n)], 1)
#define	zstat_dec(zst, n)	_zstat_slot_add(&(zst)->zst_slots[(n)], -1)

static inline void _zstat_slot_add_g(zstat_slot_t *slot, uint_t i, int64_t v) {
	ASSERT3U(slot->zst_type, ==, _ZSTAT_TYPE_OFFSET);
	_zstat_slot_add(&slot[slot->zst_offset + i], v);
}
#define	zstat_add_g(zst, n, i, v)	\
	_zstat_slot_add_g(&(zst)->zst_slots[(n)], i, v)
#define	zstat_sub_g(zst, n, i, v)	\
	_zstat_slot_add_g(&(zst)->zst_slots[(n)], i, -v)
#define	zstat_inc_g(zst, n, i)		\
	_zstat_slot_add_g(&(zst)->zst_slots[(n)], i, 1)
#define	zstat_dec_g(zst, n, i)		\
	_zstat_slot_add_g(&(zst)->zst_slots[(n)], i, -1)

#endif
