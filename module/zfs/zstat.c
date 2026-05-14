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
	kstat_named_t *kstats = (kstat_named_t *)ksp->ks_data;

	// XXX mapping
	for (uint_t i = 0; i < zst->zst_nstat; i++)
		kstats[i].value.ui64 = wmsum_value(&zst->zst_sums[i]);

	return (0);
}
/*
 * XXX mappings for other stat types
 *       ZSTAT_TYPE_COUNTER -> KSTAT_DATA_UINT64, wmsum_t
 *       gauge? same?
 *       histogram?
 *       quantile?
 *       rolling avg?
 *         -- robn, 2024-05-22
 */
zstat_t *
zstat_create(const char *name, const zstat_def_t *def, uint_t ndef)
{
	zstat_t *zst = kmem_alloc(sizeof (zstat_t) + ndef * sizeof (wmsum_t),
	    KM_SLEEP);

	zst->zst_nstat = ndef;
	for (uint_t i = 0; i < ndef; i++)
		wmsum_init(&zst->zst_sums[i], 0); // XXX type mapping

	/* XXX for now, they're all bolted to kstats; in the future something a
	 *     bit more generic, or not at all -- robn, 2024-05-22 */

	/*
	 * split and rewrite name into kstats module and statname.
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

	zst->zst_ksp = kstat_create(modulename, 0, statname, "misc",
	    KSTAT_TYPE_NAMED, ndef, 0);
	if (zst->zst_ksp == NULL)
		return (zst);

	kstat_named_t *kstats = (kstat_named_t *)zst->zst_ksp->ks_data;
	for (uint_t i = 0; i < ndef; i++) {
		strlcpy(kstats[i].name, def[i].zst_name, KSTAT_STRLEN);
		kstats[i].data_type = KSTAT_DATA_UINT64; // XXX kstat mapping
	}
	zst->zst_ksp->ks_update = zstat_kstat_update;
	zst->zst_ksp->ks_private = zst;
	kstat_install(zst->zst_ksp);

	return (zst);
}

void
zstat_destroy(zstat_t *zst)
{
	if (zst->zst_ksp != NULL)
		kstat_delete(zst->zst_ksp);

	for (uint_t i = 0; i < zst->zst_nstat; i++)
		wmsum_fini(&zst->zst_sums[i]);	// XXX type mapping

	kmem_free(zst, sizeof (zstat_t) + zst->zst_nstat * sizeof (wmsum_t));
}
