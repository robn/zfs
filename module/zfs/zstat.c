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

zstat_t *
zstat_create(const zstat_def_t *def, uint_t ndef)
{
	(void) def;
	(void) ndef;
	return (NULL);
}

void
zstat_destroy(zstat_t *zst)
{
	(void) zst;
}

#if 0
static int
brt_kstats_update(kstat_t *ksp, int rw)
{
	brt_stats_t *bs = ksp->ks_data;

	if (rw == KSTAT_WRITE)
		return (EACCES);

	bs->brt_addref_entry_in_memory.value.ui64 =
	    wmsum_value(&brt_sums.brt_addref_entry_in_memory);
	bs->brt_addref_entry_not_on_disk.value.ui64 =
	    wmsum_value(&brt_sums.brt_addref_entry_not_on_disk);
	bs->brt_addref_entry_on_disk.value.ui64 =
	    wmsum_value(&brt_sums.brt_addref_entry_on_disk);
	bs->brt_addref_entry_read_lost_race.value.ui64 =
	    wmsum_value(&brt_sums.brt_addref_entry_read_lost_race);
	bs->brt_decref_entry_in_memory.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_entry_in_memory);
	bs->brt_decref_entry_loaded_from_disk.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_entry_loaded_from_disk);
	bs->brt_decref_entry_not_in_memory.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_entry_not_in_memory);
	bs->brt_decref_entry_not_on_disk.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_entry_not_on_disk);
	bs->brt_decref_entry_read_lost_race.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_entry_read_lost_race);
	bs->brt_decref_entry_still_referenced.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_entry_still_referenced);
	bs->brt_decref_free_data_later.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_free_data_later);
	bs->brt_decref_free_data_now.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_free_data_now);
	bs->brt_decref_no_entry.value.ui64 =
	    wmsum_value(&brt_sums.brt_decref_no_entry);

	return (0);
}

static void
brt_stat_init(void)
{

	wmsum_init(&brt_sums.brt_addref_entry_in_memory, 0);
	wmsum_init(&brt_sums.brt_addref_entry_not_on_disk, 0);
	wmsum_init(&brt_sums.brt_addref_entry_on_disk, 0);
	wmsum_init(&brt_sums.brt_addref_entry_read_lost_race, 0);
	wmsum_init(&brt_sums.brt_decref_entry_in_memory, 0);
	wmsum_init(&brt_sums.brt_decref_entry_loaded_from_disk, 0);
	wmsum_init(&brt_sums.brt_decref_entry_not_in_memory, 0);
	wmsum_init(&brt_sums.brt_decref_entry_not_on_disk, 0);
	wmsum_init(&brt_sums.brt_decref_entry_read_lost_race, 0);
	wmsum_init(&brt_sums.brt_decref_entry_still_referenced, 0);
	wmsum_init(&brt_sums.brt_decref_free_data_later, 0);
	wmsum_init(&brt_sums.brt_decref_free_data_now, 0);
	wmsum_init(&brt_sums.brt_decref_no_entry, 0);

	brt_ksp = kstat_create("zfs", 0, "brtstats", "misc", KSTAT_TYPE_NAMED,
	    sizeof (brt_stats) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);
	if (brt_ksp != NULL) {
		brt_ksp->ks_data = &brt_stats;
		brt_ksp->ks_update = brt_kstats_update;
		kstat_install(brt_ksp);
	}
}

static void
brt_stat_fini(void)
{
	if (brt_ksp != NULL) {
		kstat_delete(brt_ksp);
		brt_ksp = NULL;
	}

	wmsum_fini(&brt_sums.brt_addref_entry_in_memory);
	wmsum_fini(&brt_sums.brt_addref_entry_not_on_disk);
	wmsum_fini(&brt_sums.brt_addref_entry_on_disk);
	wmsum_fini(&brt_sums.brt_addref_entry_read_lost_race);
	wmsum_fini(&brt_sums.brt_decref_entry_in_memory);
	wmsum_fini(&brt_sums.brt_decref_entry_loaded_from_disk);
	wmsum_fini(&brt_sums.brt_decref_entry_not_in_memory);
	wmsum_fini(&brt_sums.brt_decref_entry_not_on_disk);
	wmsum_fini(&brt_sums.brt_decref_entry_read_lost_race);
	wmsum_fini(&brt_sums.brt_decref_entry_still_referenced);
	wmsum_fini(&brt_sums.brt_decref_free_data_later);
	wmsum_fini(&brt_sums.brt_decref_free_data_now);
	wmsum_fini(&brt_sums.brt_decref_no_entry);
}

#endif
