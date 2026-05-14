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

#include <sys/zstat.h>
#include <sys/zfs_context.h>

static int
zstat_kstat_update(kstat_t *ksp, int rw)
{
	if (rw == KSTAT_WRITE)
		return (EACCES);

	zstat_t *zst = ksp->ks_private;

	kstat_named_t *kstat = (kstat_named_t *)ksp->ks_data;

	for (uint_t i = 0; i < zst->zst_nslots; i++) {
		zstat_slot_t *slot = &zst->zst_slots[i];
		if (slot->zst_type == _ZSTAT_TYPE_OFFSET)
			continue;

		switch (slot->zst_type) {
		case _ZSTAT_TYPE_COUNTER:
			kstat->value.ui64 = wmsum_value(&slot->zst_counter);
			break;
		default:
			__builtin_unreachable();
		}

		kstat++;
	}

	return (0);
}
/*
 * XXX mappings for other stat types
 *       _ZSTAT_TYPE_COUNTER -> KSTAT_DATA_UINT64, wmsum_t
 *       gauge? same?
 *       histogram?
 *       quantile?
 *       rolling avg?
 *         -- robn, 2024-05-22
 */
zstat_t *
zstat_create(const char *name, const zstat_def_t *def, uint_t ndef)
{
	/*
	 * Determine number of slots. One for each definition, plus extras for
	 * stat groups.
	 */
	uint_t nslots = ndef;
	for (uint_t i = 0; i < ndef; i++)
		nslots += (def[i].zst_type >> 16) & 0xffff;

	/* Allocate the zstat container with slots at the end. */
	zstat_t *zst = kmem_alloc(sizeof (zstat_t) +
	    nslots * sizeof (zstat_slot_t), KM_SLEEP);
	zst->zst_nslots = nslots;

	/*
	 * Initialise each slot. When we come across a group, make it an offset
	 * slot, and then take that many slots from the tail and initialise
	 * those with the wanted type.
	 */
	uint_t tail = ndef, nstats = 0;
	for (uint_t i = 0; i < ndef; i++) {
		zstat_type_t type = def[i].zst_type & 0xffff;

		zstat_slot_t *slot = &zst->zst_slots[i];
		uint_t count = (def[i].zst_type >> 16) & 0xffff;
		if (count > 0) {
			slot->zst_type = _ZSTAT_TYPE_OFFSET;
			slot->zst_offset = tail - i;
			slot += slot->zst_offset;
			tail += count;
		} else {
			count = 1;
		}

		for (uint_t n = 0; n < count; n++, slot++) {
			slot->zst_type = type;
			switch (type) {
			case _ZSTAT_TYPE_COUNTER:
				wmsum_init(&slot->zst_counter, 0);
				break;
			default:
				__builtin_unreachable();
			}
		}

		nstats += count;
	}

	/*
	 * Split and rewrite name into kstats module and statname.
	 *   foo.bar.baz => module=foo/bar, statname=baz
	 */
	char modulename[KSTAT_STRLEN], *statname = NULL, *p = modulename;
	strlcpy(modulename, name, KSTAT_STRLEN);
	while (p != NULL && (statname = strsep(&p, "."))) {
		if (p != NULL && p > statname)
			p[-1] = '/';
	}
	if (statname > modulename)
		statname[-1] = '\0';

	/*
	 * Allocate kstat. This might fail, but that shouldn't fail the zstat
	 * creation, as kstats are not the only way that this might be used.
	 */
	kstat_t *ksp = kstat_create(modulename, 0, statname, "misc",
	    KSTAT_TYPE_NAMED, nstats, 0);
	if (ksp == NULL)
		return (zst);

	/*
	 * Now create the kstats. We want them to have the same order as the
	 * slots array to make updating them just a walk through both lists.
	 * So we do it in two passes: first the non-offset slots, then the
	 * offset slots resolved into stat groups.
	 */
	kstat_named_t *kstat = (kstat_named_t *)ksp->ks_data;
	for (uint_t i = 0; i < ndef; i++) {
		/* First pass; skip offset slots */
		if (((def[i].zst_type >> 16) & 0xffff) != 0)
			continue;

		strlcpy(kstat->name, def[i].zst_name, KSTAT_STRLEN);

		zstat_type_t type = def[i].zst_type & 0xffff;
		switch (type) {
		case _ZSTAT_TYPE_COUNTER:
			kstat->data_type = KSTAT_DATA_UINT64;
			break;
		default:
			__builtin_unreachable();
		}

		kstat++;
	}
	for (uint_t i = 0; i < ndef; i++) {
		/* Second pass; skip non-offset slots */
		uint_t count = (def[i].zst_type >> 16) & 0xffff;
		if (count == 0)
			continue;

		zstat_type_t type = def[i].zst_type & 0xffff;

		for (u_int n = 0; n < count; n++) {
			size_t end =
			    strlcpy(kstat->name, def[i].zst_name, KSTAT_STRLEN);
			if (end < KSTAT_STRLEN)
				snprintf(&kstat->name[end], KSTAT_STRLEN-end,
				    "_%u", n);

			switch (type) {
			case _ZSTAT_TYPE_COUNTER:
				kstat->data_type = KSTAT_DATA_UINT64;
				break;
			default:
				__builtin_unreachable();
			}

			kstat++;
		}
	}

	ksp->ks_update = zstat_kstat_update;
	ksp->ks_private = zst;
	kstat_install(ksp);
	zst->zst_ksp = ksp;

	return (zst);
}

void
zstat_destroy(zstat_t *zst)
{
	if (zst->zst_ksp != NULL)
		kstat_delete(zst->zst_ksp);

	for (uint_t i = 0; i < zst->zst_nslots; i++) {
		zstat_slot_t *slot = &zst->zst_slots[i];
		switch (slot->zst_type) {
		case _ZSTAT_TYPE_COUNTER:
			wmsum_fini(&slot->zst_counter);
			break;
		default:
			__builtin_unreachable();
		}
	}

	kmem_free(zst, sizeof (zstat_t) + zst->zst_nslots * sizeof (wmsum_t));
}
