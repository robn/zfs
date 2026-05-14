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
	_ZSTAT_TYPE_COUNTER_ATOMIC,
	_ZSTAT_TYPE_COUNTER_PERCPU,
} zstat_type_t;

typedef struct zstat_def {
	const char		*zst_name;
	struct {
		zstat_type_t	zst_type;
		uint_t		zst_ngrouped;
	};
} zstat_def_t;

#define	ZSTAT_COUNTER_ATOMIC	{ _ZSTAT_TYPE_COUNTER_ATOMIC, 0 }
#define	ZSTAT_COUNTER_PERCPU	{ _ZSTAT_TYPE_COUNTER_PERCPU, 0 }

#define	_ZSTAT_GROUP(t, n)	{ t, n }

#define	ZSTAT_COUNTER_ATOMIC_GROUP(n)		\
	_ZSTAT_GROUP(_ZSTAT_TYPE_COUNTER_ATOMIC, n)
#define	ZSTAT_COUNTER_PERCPU_GROUP(n)		\
	_ZSTAT_GROUP(_ZSTAT_TYPE_COUNTER_PERCPU, n)

typedef struct zstat_slot {
	zstat_type_t	zst_type;
	union {
		uint_t		zst_offset;		/* offset to slots */
		uint64_t	zst_counter_atomic;	/* gp counter */
		wmsum_t		zst_counter_percpu;	/* percpu counter */
	};
} zstat_slot_t;

typedef struct zstat {
	uint_t		zst_nslots;
	kstat_t		*zst_ksp;
	zstat_slot_t	zst_slots[];
} zstat_t;

zstat_t *zstat_create(const char *name, const zstat_def_t *def, uint_t ndefs);
void zstat_destroy(zstat_t *zst);

/*
 * Low-level actions for each stat type.
 */
#define __ZSTAT_COUNTER_ATOMIC_INC(slot)		\
	atomic_inc_64(&((slot)->zst_counter_atomic))
#define __ZSTAT_COUNTER_ATOMIC_DEC(slot)		\
	atomic_dec_64(&((slot)->zst_counter_atomic))
#define __ZSTAT_COUNTER_ATOMIC_ADD(slot, v)		\
	atomic_add_64(&((slot)->zst_counter_atomic), (v))
#define __ZSTAT_COUNTER_ATOMIC_SUB(slot, v)		\
	atomic_sub_64(&((slot)->zst_counter_atomic), -(v))

#define __ZSTAT_COUNTER_PERCPU_INC(slot)	\
	wmsum_add(&((slot)->zst_counter_percpu), 1)
#define __ZSTAT_COUNTER_PERCPU_DEC(slot)	\
	wmsum_add(&((slot)->zst_counter_percpu), -1)
#define __ZSTAT_COUNTER_PERCPU_ADD(slot, v)	\
	wmsum_add(&((slot)->zst_counter_percpu), (v))
#define __ZSTAT_COUNTER_PERCPU_SUB(slot, v)	\
	wmsum_add(&((slot)->zst_counter_percpu), -(v))

/*
 * Apply an action to the stat in a slot. Slot and value are evaluated and
 * resolved here.
 */
#define	__ZSTAT_DO_SLOT(slot, ty, act) do {		\
	zstat_slot_t *__zstat_slot = (slot);		\
	ASSERT3U(__zstat_slot->zst_type, ==, ty);	\
	act(__zstat_slot);				\
} while (0)
#define	__ZSTAT_DO_SLOT_V(slot, ty, act, v) do {	\
	zstat_slot_t *__zstat_slot = (slot);		\
	ASSERT3U(__zstat_slot->zst_type, ==, ty);	\
	const int64_t __zstat_v = (v);			\
	act(__zstat_slot, __zstat_v);			\
} while (0)

/*
 * Expand an type and action name, then apply.
 */
#define	_ZSTAT_DO_SLOT(slot, ty, act)		\
	__ZSTAT_DO_SLOT(slot, _ZSTAT_TYPE_##ty, __ZSTAT_##ty##_##act)
#define	_ZSTAT_DO_SLOT_V(slot, ty, act, v)	\
	__ZSTAT_DO_SLOT_V(slot, _ZSTAT_TYPE_##ty, __ZSTAT_##ty##_##act, v)

/*
 * Resolve the nth slot in the stat collection.
 */
#define	__ZSTAT_SLOT_FOR(zst, n)	(&((zst)->zst_slots[(n)]))

/*
 * Apply the short type and action to the nth slot in the stat collection.
 */
#define	_ZSTAT_DO(zst, n, ty, act)	\
	_ZSTAT_DO_SLOT(__ZSTAT_SLOT_FOR(zst, n), ty, act)
#define	_ZSTAT_DO_V(zst, n, ty, act, v)	\
	_ZSTAT_DO_SLOT_V(__ZSTAT_SLOT_FOR(zst, n), ty, act, v)

/*
 * Get the slot at the given index in a stat group, from the offset slot.
 */
#define	__ZSTAT_OFFSET_SLOT(slot, i)	(&((slot)[(slot)->zst_offset+(i)]))

/*
 * Apply the type and action to a slot in a stat group. The original slot and
 * value are evaluted and resolved here, and then passed on to the regular slot
 * macro.
 */
#define	__ZSTAT_DO_OFFSET_SLOT(slot, i, ty, act) do {			\
	zstat_slot_t *__zstat_offset_slot = (slot);			\
	ASSERT3U(__zstat_offset_slot->zst_type, ==,			\
		_ZSTAT_TYPE_OFFSET);					\
	_ZSTAT_DO_SLOT(__ZSTAT_OFFSET_SLOT(__zstat_offset_slot, i),	\
		ty, act);						\
} while (0)
#define	__ZSTAT_DO_OFFSET_SLOT_V(slot, i, ty, act, v) do {		\
	zstat_slot_t *__zstat_offset_slot = (slot);			\
	ASSERT3U(__zstat_offset_slot->zst_type, ==,			\
		_ZSTAT_TYPE_OFFSET);					\
	const int64_t __zstat_o_v = (v);				\
	_ZSTAT_DO_SLOT_V(__ZSTAT_OFFSET_SLOT(__zstat_offset_slot, i),	\
		ty, act, __zstat_o_v);					\
} while (0)

/*
 * Apply the short type and action to a slot in a stat group.
 */
#define	_ZSTAT_DO_OFFSET(zst, n, i, ty, act)	\
	__ZSTAT_DO_OFFSET_SLOT(__ZSTAT_SLOT_FOR(zst, n), i, ty, act)
#define	_ZSTAT_DO_OFFSET_V(zst, n, i, ty, act, v)	\
	__ZSTAT_DO_OFFSET_SLOT_V(__ZSTAT_SLOT_FOR(zst, n), i, ty, act, v)

/*
 * Public high-level actions for each stat type.
 */
#define	zstat_counter_atomic_inc(zst, n)	\
	_ZSTAT_DO(zst, n, COUNTER_ATOMIC, INC)
#define	zstat_counter_atomic_dec(zst, n)	\
	_ZSTAT_DO(zst, n, COUNTER_ATOMIC, DEC)
#define	zstat_counter_atomic_add(zst, n, v)	\
	_ZSTAT_DO_V(zst, n, COUNTER_ATOMIC, ADD, v)
#define	zstat_counter_atomic_sub(zst, n, v)	\
	_ZSTAT_DO_V(zst, n, COUNTER_ATOMIC, SUB, v)

#define	zstat_counter_percpu_inc(zst, n)	\
	_ZSTAT_DO(zst, n, COUNTER_PERCPU, INC)
#define	zstat_counter_percpu_dec(zst, n)	\
	_ZSTAT_DO(zst, n, COUNTER_PERCPU, DEC)
#define	zstat_counter_percpu_add(zst, n, v)	\
	_ZSTAT_DO_V(zst, n, COUNTER_PERCPU, ADD, v)
#define	zstat_counter_percpu_sub(zst, n, v)	\
	_ZSTAT_DO_V(zst, n, COUNTER_PERCPU, SUB)


/*
 * Actions for a group stat type.
 */
#define	zstat_counter_atomic_inc_g(zst, n, i)		\
	_ZSTAT_DO_OFFSET(zst, n, i, COUNTER_ATOMIC, INC)
#define	zstat_counter_atomic_dec_g(zst, n, i)		\
	_ZSTAT_DO_OFFSET(zst, n, i, COUNTER_ATOMIC, DEC)
#define	zstat_counter_atomic_add_g(zst, n, i, v)	\
	_ZSTAT_DO_OFFSET_V(zst, n, i, COUNTER_ATOMIC, ADD, v)
#define	zstat_counter_atomic_sub_g(zst, n, i, v)	\
	_ZSTAT_DO_OFFSET_V(zst, n, i, COUNTER_ATOMIC, SUB, v)

#define	zstat_counter_percpu_inc_g(zst, n, i)		\
	_ZSTAT_DO_OFFSET(zst, n, i, COUNTER_PERCPU, INC)
#define	zstat_counter_percpu_dec_g(zst, n, i)		\
	_ZSTAT_DO_OFFSET(zst, n, i, COUNTER_PERCPU, DEC)
#define	zstat_counter_percpu_add_g(zst, n, i, v)	\
	_ZSTAT_DO_OFFSET_V(zst, n, i, COUNTER_PERCPU, ADD, v)
#define	zstat_counter_percpu_sub_g(zst, n, i, v)	\
	_ZSTAT_DO_OFFSET_V(zst, n, i, COUNTER_PERCPU, SUB, v)

#endif
