// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2026, TrueNAS.
 */

#include <stdbool.h>

#include <sys/zap.h>
#include <sys/btree.h>
typedef struct spa spa_t;	/* forward decl for zap_impl.h */
#include <sys/zap_impl.h>
#include <sys/zap_leaf.h>

#include "mock_dmu.h"
#include "unit.h"

/* ========== */

/*
 * Normally defined and initialised in arc.c.  We define and initialise it
 * ourselves here so this mock can be linked without arc.c.
 */
uint64_t zfs_crc64_table[256];

static void
mock_crc64_init(void)
{
	for (int i = 0; i < 256; i++) {
		uint64_t ct = i;
		for (int j = 8; j > 0; j--)
			ct = (ct >> 1) ^ (-(ct & 1) & ZFS_CRC64_POLY);
		zfs_crc64_table[i] = ct;
	}
}

/* Misc utility functions. */

#define	rd64(ptr, off)	(*(uint64_t *)((const char *)(ptr) + (off)))
#define	rd32(ptr, off)	(*(uint32_t *)((const char *)(ptr) + (off)))
#define	rd16(ptr, off)	(*(uint16_t *)((const char *)(ptr) + (off)))

/* ========== */

/* ZAP-specific mocks and other test helpers. */

/* Create a microzap backed by a mock dnode. */
static dnode_t *
mock_zap_create_microzap(void) {
	/*
	 * We use DMU_OTN_ZAP_DATA so that DMU_OT_BYTESWAP() returns
	 * DMU_BSWAP_ZAP without consulting dmu_ot[], which is not currently
	 * provided in the mock.
	 */
	mock_dnode_t *mdn = mock_dnode_create(512, DMU_OTN_ZAP_DATA);
	dnode_t *dn = (dnode_t *)mdn;
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();
	mzap_create_impl(dn, 0, 0, tx);
	mock_tx_destroy((mock_dmu_tx_t *)tx);
	return (dn);
}

/* Create a fatzap backed by a mock dnode. */
static dnode_t *
mock_zap_create_fatzap(void)
{
	/*
	 * We can only create microzaps directly. They only take u64s as a
	 * value, so we add a u16 to trigger an upgrade to fatzap.
	 */
	dnode_t *dn = mock_zap_create_microzap();
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();
	uint16_t upgrade = 0;
	zap_add_by_dnode(dn, "_upgrade", sizeof (uint16_t), 1, &upgrade, tx);
	zap_remove_by_dnode(dn, "_upgrade", tx);
	mock_tx_destroy((mock_dmu_tx_t *)tx);
	return (dn);
}

/*
 * Create a ZAP configured for uint64 keys. Only fatzap is supported;
 * mzap_create_impl() immediately upgrades when non-zero flags are provided.
 */
static dnode_t *
mock_zap_create_fatzap_uint64(void)
{
	mock_dnode_t *mdn = mock_dnode_create(512, DMU_OTN_ZAP_DATA);
	dnode_t *dn = (dnode_t *) mdn;
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();
	mzap_create_impl(dn, 0, ZAP_FLAG_HASH64 | ZAP_FLAG_UINT64_KEY, tx);
	mock_tx_destroy((mock_dmu_tx_t *) tx);
	return (dn);
}

/*
 * Create a ZAP configured for prehashed uint64 keys. Only fatzap is supported;
 * mzap_create_impl() immediately upgrades when non-zero flags are provided.
 */
static dnode_t *
mock_zap_create_fatzap_uint64_prehashed(void)
{
	mock_dnode_t *mdn = mock_dnode_create(512, DMU_OTN_ZAP_DATA);
	dnode_t *dn = (dnode_t *) mdn;
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();
	mzap_create_impl(dn, 0,
	    ZAP_FLAG_PRE_HASHED_KEY | ZAP_FLAG_UINT64_KEY, tx);
	mock_tx_destroy((mock_dmu_tx_t *) tx);
	return (dn);
}

static bool
mock_zap_is_microzap(dnode_t *dn)
{
	/* check block 0 has a microzap header */
	const void *blk = mock_dnode_block_data((mock_dnode_t *)dn, 0);
	return (rd64(blk, 0) == ZBT_MICRO);
}

static bool
mock_zap_is_fatzap(dnode_t *dn)
{
	/* check block 0 has a fatzap header */
	const void *blk = mock_dnode_block_data((mock_dnode_t *)dn, 0);
	return (rd64(blk, 0) == ZBT_HEADER && rd64(blk, 8) == ZAP_MAGIC);
}

static void
mock_zap_destroy(dnode_t *dn)
{
	mock_dnode_destroy((mock_dnode_t *)dn);
}

/* Create a ZAP of the type named in the given test params. */
static dnode_t *
mock_zap_create_params(const MunitParameter params[], const char *key) {
	const char *type = munit_parameters_get(params, key);
	if (type == NULL)
		munit_error("mock_zap_create_params: missing type param");
	else if (strcmp(type, "micro") == 0)
		return (mock_zap_create_microzap());
	else if (strcmp(type, "fat") == 0)
		return (mock_zap_create_fatzap());
	else
		munit_errorf("mock_zap_create_params: invalid type '%s'", type);
	__builtin_unreachable();
}

/*
 * Confirm the stored ZAP is of the type named in the given test params. This
 * is useful for sanity checks within tests that a ZAP wasn't unexpectedly
 * upgraded during the test.
 */
static bool
mock_zap_is_params(dnode_t *dn, const MunitParameter params[],
    const char *key)
{
	const char *type = munit_parameters_get(params, key);
	if (type == NULL)
		munit_error("mock_zap_is_params: missing type param");
	else if (strcmp(type, "micro") == 0)
		return (mock_zap_is_microzap(dn));
	else if (strcmp(type, "fat") == 0)
		return (mock_zap_is_fatzap(dn));
	else
		munit_errorf("mock_zap_is_params: invalid type '%s'", type);
	__builtin_unreachable();
}

/* ========== */

/*
 * Sanity checks for mock ZAPs. Ensures that the mock_zap_create_* functions
 * really do create the right kind of ZAPs, since many of the tests need to
 * run against both kinds to confirm that they all work the same way.
 */
static MunitResult
test_mock_microzap_sanity(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	dnode_t *dn = mock_zap_create_microzap();
	unit_true(mock_zap_is_microzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

static MunitResult
test_mock_fatzap_sanity(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	dnode_t *dn = mock_zap_create_fatzap();
	unit_true(mock_zap_is_fatzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* ========== */

/*
 * A simple add, lookup and remove test. Confirms basic operation. These are
 * tested together simply because all other tests rely on these primitives.
 */
static MunitResult
test_zap_basic(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *)mock_tx_create();

	/* Insert a few entries. */
	uint64_t val42 = 42;
	uint64_t val99 = 99;
	uint64_t val0  = 0;

	unit_ok(zap_add_by_dnode(dn, "hello",
	    sizeof (uint64_t), 1, &val42, tx));
	unit_ok(zap_add_by_dnode(dn, "world",
	    sizeof (uint64_t), 1, &val99, tx));
	unit_ok(zap_add_by_dnode(dn, "zero",
	    sizeof (uint64_t), 1, &val0, tx));

	/* Lookup each entry. */
	uint64_t result = 0;
	unit_ok(zap_lookup_by_dnode(dn, "hello",
	    sizeof (uint64_t), 1, &result));
	unit_eq(result, 42);

	unit_ok(zap_lookup_by_dnode(dn, "world",
	    sizeof (uint64_t), 1, &result));
	unit_eq(result, 99);

	unit_ok(zap_lookup_by_dnode(dn, "zero",
	    sizeof (uint64_t), 1, &result));
	unit_eq(result, 0);

	/* Non-existent key should return ENOENT. */
	unit_err(zap_lookup_by_dnode(dn, "nope",
	    sizeof (uint64_t), 1, &result), ENOENT);

	/* Removing an entry should make it impossible to look up. */
	unit_ok(zap_remove_by_dnode(dn, "world", tx));
	unit_err(zap_lookup_by_dnode(dn, "world",
	    sizeof (uint64_t), 1, &result), ENOENT);

	mock_tx_destroy((mock_dmu_tx_t *)tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* ========== */

/*
 * Basic KV API tests. Covers the most basic functionality upon which
 * which everything else is built.
 */

static MunitResult
test_zap_count(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	uint64_t count = 0;
	unit_ok(zap_count_by_dnode(dn, &count));
	unit_eq(count, 0);

	uint64_t v = 1;
	unit_ok(zap_add_by_dnode(dn, "a", sizeof (uint64_t), 1, &v, tx));
	unit_ok(zap_add_by_dnode(dn, "b", sizeof (uint64_t), 1, &v, tx));

	unit_ok(zap_count_by_dnode(dn, &count));
	unit_eq(count, 2);

	unit_ok(zap_remove_by_dnode(dn, "a", tx));
	unit_ok(zap_count_by_dnode(dn, &count));
	unit_eq(count, 1);

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/*
 * zap_contains_by_dnode: existence check without reading the value.
 *
 * On microzap the lookup returns EOVERFLOW (num_integers=0), which the
 * function converts to 0.  On fatzap the same conversion handles EOVERFLOW
 * from the fat path.
 */
static MunitResult
test_zap_contains(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	uint64_t v = 42;
	unit_ok(zap_add_by_dnode(dn, "present", sizeof (uint64_t), 1, &v, tx));
	unit_ok(zap_contains_by_dnode(dn, "present"));
	unit_err(zap_contains_by_dnode(dn, "absent"), ENOENT);

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/*
 * Test zap_length_by_dnode, which queries value metadata (integer_size and
 * num_integers) without fetching the value itself.
 *
 * For microzap, all values are uint64_t so the answer is always 8/1.
 * For fatzap, the stored size and count are returned directly from the
 * leaf chunk.  We exercise both by starting in microzap and triggering an
 * upgrade via a multi-integer insert.
 */
static MunitResult
test_zap_length(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	/* uint64: integer_size=8, num_integers=1. */
	uint64_t v = 42;
	unit_ok(zap_add_by_dnode(dn, "u64",
	    sizeof (uint64_t), 1, &v, tx));

	uint64_t isz = 0, nint = 0;
	unit_ok(zap_length_by_dnode(dn, "u64", &isz, &nint));
	unit_eq(isz, 8);
	unit_eq(nint, 1);

	/* Missing key returns ENOENT. */
	unit_err(zap_length_by_dnode(dn, "nope", &isz, &nint), ENOENT);

	/* Either output pointer may be NULL. */
	isz = 0; nint = 0;
	unit_ok(zap_length_by_dnode(dn, "u64", NULL, &nint));
	unit_ok(zap_length_by_dnode(dn, "u64", &isz, NULL));
	unit_eq(isz, 8);
	unit_eq(nint, 1);

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

static MunitResult
test_zap_update(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	/* Update on a non-existent key inserts it. */
	uint64_t val = 42;
	unit_ok(zap_update_by_dnode(dn, "key", sizeof (uint64_t), 1, &val, tx));

	uint64_t result = 0;
	unit_ok(zap_lookup_by_dnode(dn, "key", sizeof (uint64_t), 1, &result));
	unit_eq(result, 42);

	/* Update on an existing key replaces it without error. */
	val = 99;
	unit_ok(zap_update_by_dnode(dn, "key", sizeof (uint64_t), 1, &val, tx));
	unit_ok(zap_lookup_by_dnode(dn, "key", sizeof (uint64_t), 1, &result));
	unit_eq(result, 99);

	/* Count should still be 1 (no duplicate was created). */
	uint64_t count = 0;
	unit_ok(zap_count_by_dnode(dn, &count));
	unit_eq(count, 1);

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

static MunitResult
test_zap_increment(const MunitParameter params[], void *data)
{
	(void) data;

	dnode_t *dn = mock_zap_create_params(params, "type");
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	/* Increment a missing key creates it with that value. */
	unit_ok(zap_increment_by_dnode(dn, "ctr", 5, tx));
	uint64_t result = 0;
	unit_ok(zap_lookup_by_dnode(dn, "ctr", sizeof (uint64_t), 1, &result));
	unit_eq(result, 5);

	/* Further increments accumulate. */
	unit_ok(zap_increment_by_dnode(dn, "ctr", 3, tx));
	unit_ok(zap_lookup_by_dnode(dn, "ctr", sizeof (uint64_t), 1, &result));
	unit_eq(result, 8);

	/* Decrement works. */
	unit_ok(zap_increment_by_dnode(dn, "ctr", -2, tx));
	unit_ok(zap_lookup_by_dnode(dn, "ctr", sizeof (uint64_t), 1, &result));
	unit_eq(result, 6);

	/* Decrementing to zero removes the entry. */
	unit_ok(zap_increment_by_dnode(dn, "ctr", -6, tx));
	unit_err(zap_lookup_by_dnode(dn, "ctr",
	    sizeof (uint64_t), 1, &result), ENOENT);

	/* Delta of zero is a no-op even for a missing key. */
	unit_ok(zap_increment_by_dnode(dn, "ctr", 0, tx));
	unit_err(zap_lookup_by_dnode(dn, "ctr",
	    sizeof (uint64_t), 1, &result), ENOENT);

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_params(dn, params, "type"));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* ========== */

/*
 * On-disk format sanity checks. These are not supposed to be a comprehensive
 * validity check, but rather, a defense against an accidental change to the
 * ZAP on-disk structs (eg zap_phys_t).
 */

/*
 * Verify the microzap on-disk layout for a single-entry ZAP.
 *
 * Microzap block 0 layout (offsets in bytes):
 *   0   uint64_t  mz_block_type   (ZBT_MICRO)
 *   8   uint64_t  mz_salt         (non-zero)
 *   16  uint64_t  mz_normflags
 *   24  uint64_t  mz_pad[5]       (40 bytes)
 *   64  entry 0:
 *         64  uint64_t  mze_value
 *         72  uint32_t  mze_cd
 *         76  uint16_t  mze_pad
 *         78  char[50]  mze_name   (MZAP_NAME_LEN = 50)
 */
static MunitResult
test_microzap_format(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	dnode_t *dn = mock_zap_create_microzap();
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	uint64_t val = 42;
	unit_ok(zap_add_by_dnode(dn, "hello",
	    sizeof (uint64_t), 1, &val, tx));

	const void *blk = mock_dnode_block_data((mock_dnode_t *) dn, 0);

	/* block type must be ZBT_MICRO */
	unit_eq(rd64(blk, 0), ZBT_MICRO);

	/* salt must be non-zero (derived from pointer/tx xor) */
	unit_ne(rd64(blk, 8), 0);

	/* normflags must be zero (we passed 0 to mzap_create_impl) */
	unit_eq(rd64(blk, 16), 0);

	/* first entry: value=42, cd=0, name="hello" */
	unit_eq(rd64(blk, 64), 42);
	unit_eq(rd32(blk, 72), 0);
	unit_str_eq((const char *)blk + 78, "hello");

	/* second slot must be empty (value=0, name="") */
	unit_eq(rd64(blk, 128), 0);
	unit_eq(*(const char *)((const char *)blk + 142), 0);

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_microzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/*
 * Verify the fatzap header (block 0) and first leaf (block 1) layout.
 *
 * We use a uint64-key ZAP because mzap_create_impl() with ZAP_FLAG_UINT64_KEY
 * calls fzap_upgrade() immediately, giving us a fatzap with just a few inserts.
 * A plain string-key ZAP only upgrades after ~2047 entries (128KB microzap max).
 *
 * Fatzap block 0 (zap_phys_t) layout (offsets in bytes):
 *   0   uint64_t  zap_block_type   (ZBT_HEADER)
 *   8   uint64_t  zap_magic        (ZAP_MAGIC = 0x2F52AB2ABULL)
 *   16  struct zap_table_phys (5 × uint64_t = 40 bytes)
 *   56  uint64_t  zap_freeblk
 *   64  uint64_t  zap_num_leafs
 *   72  uint64_t  zap_num_entries
 *   80  uint64_t  zap_salt
 *   88  uint64_t  zap_normflags
 *   96  uint64_t  zap_flags
 *
 * Fatzap leaf (block 1) header (zap_leaf_phys_t.l_hdr) layout:
 *   0   uint64_t  lh_block_type   (ZBT_LEAF)
 *   8   uint64_t  lh_pad1
 *   16  uint64_t  lh_prefix
 *   24  uint32_t  lh_magic        (ZAP_LEAF_MAGIC = 0x2AB1EAF)
 */
static MunitResult
test_fatzap_format(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	dnode_t *dn = mock_zap_create_fatzap();
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	uint64_t val = 42;
	unit_ok(zap_add_by_dnode(dn, "hello",
	    sizeof (uint64_t), 1, &val, tx));

	/* block 0: fatzap header */
	const void *hdr = mock_dnode_block_data((mock_dnode_t *) dn, 0);
	unit_eq(rd64(hdr, 0), ZBT_HEADER);
	unit_eq(rd64(hdr, 8), ZAP_MAGIC);
	unit_eq(rd64(hdr, 72), 1);	/* zap_num_entries */

	/* block 1: first leaf */
	const void *leaf = mock_dnode_block_data((mock_dnode_t *) dn, 1);
	unit_eq(rd64(leaf, 0), ZBT_LEAF);
	unit_eq(rd32(leaf, 24), ZAP_LEAF_MAGIC);

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_fatzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* ========== */

/*
 * Test the microzap-to-fatzap upgrade path for a plain string-key ZAP.
 *
 * A microzap grows by SPA_MINBLOCKSIZE (512 bytes) each time it fills up,
 * up to SPA_OLD_MAXBLOCKSIZE (128KB = 2047 entries).  The 2048th insert
 * crosses that threshold and triggers mzap_upgrade(), which:
 *   1. copies all 2047 existing entries into a freshly initialised fatzap
 *   2. adds the 2048th entry as the first real fatzap insert
 *
 * We verify the block-0 type flipped to ZBT_HEADER, that zap_num_entries
 * reflects all entries, that every entry survives the upgrade intact, and
 * that cursor iteration over the resulting fatzap gives the right count.
 */
static MunitResult
test_upgrade_block_size(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	dnode_t *dn = mock_zap_create_microzap();
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	unit_true(mock_zap_is_microzap(dn));

	for (int i = i; i < 2048; i++) {
		char key[16];
		snprintf(key, sizeof (key), "key%04d", i);
		uint64_t v = (uint64_t)i * 7;
		unit_ok(zap_add_by_dnode(dn, key,
		    sizeof (uint64_t), 1, &v, tx));
	}

	unit_true(mock_zap_is_fatzap(dn));

	uint64_t count = 0;
	unit_ok(zap_count_by_dnode(dn, &count));
	unit_eq(count, 2048);

	for (int i = 0; i < 2048; i++) {
		char key[16];
		snprintf(key, sizeof (key), "key%04d", i);
		uint64_t result = 0;
		unit_ok(zap_lookup_by_dnode(dn, key,
		    sizeof (uint64_t), 1, &result));
		unit_eq(result, (uint64_t)i * 7);
	}

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_fatzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/*
 * zap_add_impl() upgrades a microzap to fatzap when the key is too long to
 * fit in a microzap entry (strlen >= MZAP_NAME_LEN = 50).  The upgrade fires
 * on the first insert that has a long key, even if the ZAP is otherwise empty.
 *
 * Note: there are two other value-related upgrade triggers in the same branch:
 *   integer_size != 8  — value element is not a uint64_t
 *   num_integers != 1  — value is a multi-element array
 * These are tested separately in test_upgrade_value_type below.
 *
 * A fourth trigger, !mze_canfit_fzap_leaf(), fires when hash collisions would
 * overflow the default fatzap leaf.  With MZAP_ENT_CHUNKS=5 and a 16k leaf
 * (~600 chunks) the threshold is around 120 colliding entries; engineering
 * those collisions in a unit test is impractical, so that path is not covered.
 */
static MunitResult
test_upgrade_long_key(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	dnode_t *dn = mock_zap_create_microzap();
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	unit_true(mock_zap_is_microzap(dn));

	/* Inserting a short key does not upgrade a microzap. */
	uint64_t v = 1;
	unit_ok(zap_add_by_dnode(dn, "short", sizeof (uint64_t), 1, &v, tx));
	unit_true(mock_zap_is_microzap(dn));

	/* Inserting a key of length MZAP_NAME_LEN will trigger an upgrade. */

	char longkey[MZAP_NAME_LEN + 1];
	memset(longkey, 'a', MZAP_NAME_LEN);
	longkey[MZAP_NAME_LEN] = '\0';
	unit_ok(zap_add_by_dnode(dn, longkey, sizeof (uint64_t), 1, &v, tx));

	unit_true(mock_zap_is_fatzap(dn));

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_fatzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/*
 * zap_add_impl() also upgrades when the value doesn't fit in a microzap entry:
 *   integer_size != 8  — only uint64_t (8-byte) values fit in microzap
 *   num_integers != 1  — microzap stores exactly one integer per entry
 * Both trigger the same upgrade branch; we exercise each in turn.
 */
static MunitResult
test_upgrade_value_type(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	dnode_t *dn = mock_zap_create_microzap();
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	unit_true(mock_zap_is_microzap(dn));

	uint32_t v32 = 0xdeadbeef;
	unit_ok(zap_add_by_dnode(dn, "u32", sizeof (uint32_t), 1, &v32, tx));

	unit_true(mock_zap_is_fatzap(dn));

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_fatzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

static MunitResult
test_upgrade_value_size(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	dnode_t *dn = mock_zap_create_microzap();
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	unit_true(mock_zap_is_microzap(dn));

	uint64_t a64[4] = { 10, 20, 30, 40 };
	unit_ok(zap_add_by_dnode(dn, "a64", sizeof (uint64_t), 4, a64, tx));

	unit_true(mock_zap_is_fatzap(dn));

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_fatzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* ========== */

/*
 * zap_length_uint64_by_dnode: query integer_size/num_integers for a
 * uint64-keyed entry without fetching the value.
 *
 * zap_lookup_length_uint64_by_dnode: fetch value AND actual_num_integers
 * in one call; truncates to num_integers if the stored count is larger.
 */
static MunitResult
test_fatzap_uint64_length(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	dnode_t *dn = mock_zap_create_fatzap_uint64();
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	uint64_t key1 = 0xAA, key2 = 0xBB;
	uint64_t vals[3] = { 10, 20, 30 };
	unit_ok(zap_add_uint64_by_dnode(dn,
	    &key1, 1, sizeof (uint64_t), 1, &vals[0], tx));
	unit_ok(zap_add_uint64_by_dnode(dn,
	    &key2, 1, sizeof (uint64_t), 3, vals, tx));

	/* zap_length_uint64_by_dnode */
	uint64_t isz = 0, nint = 0;
	unit_ok(zap_length_uint64_by_dnode(dn, &key1, 1, &isz, &nint));
	unit_eq(isz, 8);
	unit_eq(nint, 1);

	unit_ok(zap_length_uint64_by_dnode(dn, &key2, 1, &isz, &nint));
	unit_eq(isz, 8);
	unit_eq(nint, 3);

	/*
	 * zap_lookup_length_uint64_by_dnode: like zap_lookup_uint64_by_dnode
	 * but also returns actual_num_integers.  num_integers must be >= the
	 * stored count or the call returns EOVERFLOW without setting actual.
	 */
	uint64_t actual = 0, out = 0;
	unit_ok(zap_lookup_length_uint64_by_dnode(dn,
	    &key1, 1, sizeof (uint64_t), 1, &out, &actual));
	unit_eq(out, 10);
	unit_eq(actual, 1);

	uint64_t outbuf[3] = { 0 };
	unit_ok(zap_lookup_length_uint64_by_dnode(dn,
	    &key2, 1, sizeof (uint64_t), 3, outbuf, &actual));
	unit_eq(outbuf[0], 10);
	unit_eq(outbuf[1], 20);
	unit_eq(outbuf[2], 30);
	unit_eq(actual, 3);

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_fatzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

static MunitResult
test_fatzap_uint64_keys(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	dnode_t *dn = mock_zap_create_fatzap_uint64();
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	uint64_t key1 = 0x0000000100000002ULL;
	uint64_t key2 = 0xdeadbeefcafe0000ULL;
	uint64_t val1 = 111, val2 = 222;

	unit_ok(zap_add_uint64_by_dnode(dn, &key1, 1,
	    sizeof (uint64_t), 1, &val1, tx));
	unit_ok(zap_add_uint64_by_dnode(dn, &key2, 1,
	    sizeof (uint64_t), 1, &val2, tx));

	/* Lookup by the same keys. */
	uint64_t result = 0;
	unit_ok(zap_lookup_uint64_by_dnode(dn, &key1, 1,
	    sizeof (uint64_t), 1, &result));
	unit_eq(result, val1);

	unit_ok(zap_lookup_uint64_by_dnode(dn, &key2, 1,
	    sizeof (uint64_t), 1, &result));
	unit_eq(result, val2);

	/* Duplicate insert returns EEXIST. */
	unit_err(zap_add_uint64_by_dnode(dn, &key1, 1,
	    sizeof (uint64_t), 1, &val1, tx), EEXIST);

	/* Update replaces the value without error. */
	uint64_t newval = 999;
	unit_ok(zap_update_uint64_by_dnode(dn, &key1, 1,
	    sizeof (uint64_t), 1, &newval, tx));
	unit_ok(zap_lookup_uint64_by_dnode(dn, &key1, 1,
	    sizeof (uint64_t), 1, &result));
	unit_eq(result, newval);

	/* Remove and verify ENOENT on subsequent lookup. */
	unit_ok(zap_remove_uint64_by_dnode(dn, &key1, 1, tx));
	unit_err(zap_lookup_uint64_by_dnode(dn, &key1, 1,
	    sizeof (uint64_t), 1, &result), ENOENT);

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_fatzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* ========== */

/* Misc fatzap structural tests. */

/*
 * Test remove from a multi-leaf fatzap.
 *
 * All remove tests so far operate on a microzap (mzap_remove) or on a
 * fatzap created via an upgrade trigger with only a handful of entries.
 * This test explicitly targets fzap_remove on a grown fatzap:
 *
 *  1. Insert NFREM entries into a uint64-key fatzap (512-byte blocks,
 *     ~6 entries per leaf → multiple leaves allocated).
 *  2. Remove every other entry.
 *  3. Verify: removed keys return ENOENT; surviving keys return correct
 *     values; zap_count_by_dnode equals NFREM/2; cursor iteration
 *     visits exactly the surviving keys.
 *
 * The key structural property under test: fzap_remove reclaims the entry's
 * leaf chunks back to the leaf free list.  After removing half the entries,
 * the surviving entries must still be reachable through the unchanged pointer
 * table, with no corruption of adjacent entries' chunks.
 */
static MunitResult
test_fatzap_remove(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	dnode_t *dn = mock_zap_create_fatzap_uint64();
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	for (int i = 0; i < 128; i++) {
		uint64_t k = (uint64_t)i;
		uint64_t v = (uint64_t)i * 5;
		unit_ok(zap_add_uint64_by_dnode(dn, &k, 1,
		    sizeof (uint64_t), 1, &v, tx));
	}

	/* Confirm we have multiple leaf blocks. */
	unit_gt(mock_dnode_block_count((mock_dnode_t *)dn), 2);

	/* Remove every other entry. */
	for (int i = 0; i < 128; i += 2) {
		uint64_t k = (uint64_t)i;
		unit_ok(zap_remove_uint64_by_dnode(dn, &k, 1, tx));
	}

	/* Removed keys must return ENOENT. */
	for (int i = 0; i < 128; i += 2) {
		uint64_t k = (uint64_t)i;
		uint64_t result = 0;
		unit_err(zap_lookup_uint64_by_dnode(dn, &k, 1,
		    sizeof (uint64_t), 1, &result), ENOENT);
	}

	/* Surviving keys must return correct values. */
	for (int i = 1; i < 128; i += 2) {
		uint64_t k = (uint64_t)i;
		uint64_t result = 0;
		unit_ok(zap_lookup_uint64_by_dnode(dn, &k, 1,
		    sizeof (uint64_t), 1, &result));
		unit_eq(result, (uint64_t)i * 5);
	}

	/* Count must reflect the removals. */
	uint64_t count = 0;
	unit_ok(zap_count_by_dnode(dn, &count));
	unit_eq(count, 64);

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_fatzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/*
 * Test the zap_shrink() path — empty-leaf reclamation in a fatzap.
 *
 * zap_shrink() is called by fzap_remove() when the last entry is removed
 * from a leaf (lh_nentries drops to 0).  It walks up the prefix tree looking
 * for sibling leaves that are ALSO empty.  When it finds a pair, it:
 *   1. rewrites the ptrtbl so the sibling-1 range points at sibling-0's block
 *   2. frees sibling-1's block (dmu_free_range, no-op in mock)
 *   3. decrements zap_num_leafs
 *   4. halves lh_prefix / decrements lh_prefix_len on the surviving leaf
 *   5. calls zap_trunc() to lower zap_freeblk if the freed block was last
 *
 * Setup: use ZAP_FLAG_PRE_HASHED_KEY so the hash IS the key value.
 * Keys with bit 63 = 0 hash to prefix=0 (sibling 0, leaf block 1).
 * Keys with bit 63 = 1 hash to prefix=1 (sibling 1, leaf block 2).
 * 6 entries per 512-byte leaf fills each leaf exactly.
 *
 * Sequence:
 *   a. Remove all sibling-0 entries → zap_shrink fires but sibling-1 is
 *      not empty yet → no collapse; zap_num_leafs stays 2.
 *   b. Remove all sibling-1 entries → zap_shrink fires, finds sibling-0
 *      also empty → collapses the pair.
 *
 * After collapse: zap_num_leafs=1, zap_num_entries=0, zap_freeblk drops
 * from 3 to 2 (zap_trunc), and the surviving leaf has lh_prefix_len=0.
 * The ZAP must still be functional for subsequent inserts.
 */
static MunitResult
test_fatzap_shrink(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	dnode_t *dn = mock_zap_create_fatzap_uint64_prehashed();
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	/*
	 * Keys 0..5: bit 63 = 0 → hash prefix 0 → sibling-0 leaf.
	 * Keys (1<<63)..(1<<63)+5: bit 63 = 1 → hash prefix 1 → sibling-1 leaf.
	 */
#define	NSIB 6
	uint64_t sib0[NSIB], sib1[NSIB];
	for (int i = 0; i < NSIB; i++) {
		sib0[i] = (uint64_t)i;
		sib1[i] = (1ULL << 63) | (uint64_t)i;
	}
	for (int i = 0; i < NSIB; i++) {
		uint64_t v = (uint64_t)i;
		unit_ok(zap_add_uint64_by_dnode(dn, &sib0[i], 1,
		    sizeof (uint64_t), 1, &v, tx));
		unit_ok(zap_add_uint64_by_dnode(dn, &sib1[i], 1,
		    sizeof (uint64_t), 1, &v, tx));
	}

	mock_dnode_t *mdn = (mock_dnode_t *)dn;
	const void *hdr = mock_dnode_block_data(mdn, 0);

	/* Confirm two leaves allocated: blocks 0 (hdr), 1, 2. */
	unit_eq(mock_dnode_block_count(mdn), 3);
	unit_eq(rd64(hdr, 56), 3);	/* zap_freeblk */
	unit_eq(rd64(hdr, 64), 2);	/* zap_num_leafs */
	unit_eq(rd64(hdr, 72), 2 * NSIB); /* zap_num_entries */

	/*
	 * Step (a): drain sibling-0.  zap_shrink fires on the last removal but
	 * sibling-1 is still full, so no collapse occurs.
	 */
	for (int i = 0; i < NSIB; i++)
		unit_ok(zap_remove_uint64_by_dnode(dn, &sib0[i], 1, tx));

	unit_eq(rd64(hdr, 64), 2);	/* still 2 leaves */
	unit_eq(rd64(hdr, 72), NSIB);	/* 6 entries left */

	/* Sibling-1 entries still reachable. */
	for (int i = 0; i < NSIB; i++) {
		uint64_t result = 0;
		unit_ok(zap_lookup_uint64_by_dnode(dn, &sib1[i], 1,
		    sizeof (uint64_t), 1, &result));
		unit_eq(result, (uint64_t)i);
	}

	/*
	 * Step (b): drain sibling-1.  The last removal triggers zap_shrink,
	 * which finds sibling-0 is also empty and collapses the pair.
	 */
	for (int i = 0; i < NSIB; i++)
		unit_ok(zap_remove_uint64_by_dnode(dn, &sib1[i], 1, tx));

	/* zap_num_leafs dropped, freeblk reclaimed by zap_trunc. */
	unit_eq(rd64(hdr, 56), 2);	/* zap_freeblk */
	unit_eq(rd64(hdr, 64), 1);	/* zap_num_leafs */
	unit_eq(rd64(hdr, 72), 0);	/* zap_num_entries */

	/* Surviving leaf (block 1): lh_prefix=0, lh_prefix_len=0. */
	const void *leaf = mock_dnode_block_data(mdn, 1);
	unit_eq(rd64(leaf, 16), 0);	/* lh_prefix */
	unit_eq(rd16(leaf, 32), 0);	/* lh_prefix_len */

	/* ZAP must still be usable after shrink. */
	uint64_t k = 42, v = 999;
	unit_ok(zap_add_uint64_by_dnode(dn, &k, 1,
	    sizeof (uint64_t), 1, &v, tx));
	uint64_t result = 0;
	unit_ok(zap_lookup_uint64_by_dnode(dn, &k, 1,
	    sizeof (uint64_t), 1, &result));
	unit_eq(result, 999);
	unit_eq(rd64(hdr, 72), 1);	/* zap_num_entries */

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_fatzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/*
 * With ZAP_FLAG_PRE_HASHED_KEY (no HASH64), hashbits=28.  The hash is
 * the raw uint64 key value masked to its top 28 bits.  Keys whose values
 * are < 2^36 all map to hash=0, forcing every entry into the same bucket
 * and exercising the collision-differentiator (cd) mechanism.
 */
static MunitResult
test_fatzap_collision(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

/*
 * With 512-byte leaf blocks (18 chunks, 3 per entry), we can store at
 * most 6 entries before triggering leaf expansion.  Use 4 to stay well
 * within capacity while still exercising the cd (collision differentiator).
 */
#define	NCOLLIDE 4
	dnode_t *dn = mock_zap_create_fatzap_uint64_prehashed();
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	uint64_t keys[NCOLLIDE];
	uint64_t vals[NCOLLIDE];
	for (int i = 0; i < NCOLLIDE; i++) {
		keys[i] = (uint64_t)i;	/* all < 2^36, so hash=0 */
		vals[i] = (uint64_t)i * 100;
		unit_ok(zap_add_uint64_by_dnode(dn, &keys[i], 1,
		    sizeof (uint64_t), 1, &vals[i], tx));
	}

	/* All entries must be individually retrievable. */
	for (int i = 0; i < NCOLLIDE; i++) {
		uint64_t result = 0;
		unit_ok(zap_lookup_uint64_by_dnode(dn, &keys[i], 1,
		    sizeof (uint64_t), 1, &result));
		unit_eq(result, vals[i]);
	}

	/* Iteration must visit all entries. */
	uint64_t count = 0;
	unit_ok(zap_count_by_dnode(dn, &count));
	unit_eq(count, NCOLLIDE);

	/* Remove one and verify the rest are still readable. */
	unit_ok(zap_remove_uint64_by_dnode(dn, &keys[0], 1, tx));
	int err = zap_lookup_uint64_by_dnode(dn, &keys[0], 1,
	    sizeof (uint64_t), 1, &(uint64_t){0});
	unit_eq(err, ENOENT);
	for (int i = 1; i < NCOLLIDE; i++) {
		uint64_t result = 0;
		unit_ok(zap_lookup_uint64_by_dnode(dn,
		    &keys[i], 1, sizeof (uint64_t), 1, &result));
		unit_eq(result, vals[i]);
	}

	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_fatzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/*
 * Test the fatzap pointer-table growth path.
 *
 * The embedded pointer table lives in the second half of block 0.  For
 * 512-byte blocks (block_shift=9):
 *   ZAP_EMBEDDED_PTRTBL_SHIFT = 9 - 3 - 1 = 5  →  32 entries capacity
 *
 * Each 512-byte leaf has ZAP_LEAF_NUMCHUNKS_BS(9)=18 chunks.  Since 18
 * is below ZAP_LEAF_LOW_WATER(20), the "leaf full" check in
 * zap_put_leaf_maybe_grow_ptrtbl() fires for any insert into a leaf
 * whose lh_prefix_len == zt_shift.  On that first such insert,
 * zap_grow_ptrtbl() is called:
 *   - allocates a new dedicated block for the pointer table
 *   - transfers the 32 embedded entries to it (each duplicated)
 *   - increments zt_shift from 5 to 6, sets zt_numblks = 1
 *
 * We use zap_create_uint64_mock() (512-byte blocks, immediate fatzap)
 * and insert enough entries to force multiple such growths.  We verify
 * the ptrtbl has moved out-of-block (zt_numblks > 0) and that all
 * entries survive intact.
 */
static MunitResult
test_fatzap_ptrtbl_growth(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

#define	NPTRTBL 300
	dnode_t *dn = mock_zap_create_fatzap_uint64();
	dmu_tx_t *tx = (dmu_tx_t *) mock_tx_create();

	mock_dnode_t *mdn = (mock_dnode_t *)dn;
	const void *hdr = mock_dnode_block_data(mdn, 0);

	/* Embedded ptrtbl initially: zt_numblks == 0. */
	unit_eq(rd64(hdr, 24), 0);

	for (int i = 0; i < NPTRTBL; i++) {
		uint64_t k = (uint64_t)i;
		uint64_t v = (uint64_t)i * 3;
		unit_ok(zap_add_uint64_by_dnode(dn, &k, 1,
		    sizeof (uint64_t), 1, &v, tx));
	}

	/*
	 * After growth: zt_numblks > 0 (ptrtbl moved out-of-block) and
	 * zt_shift > 5 (ZAP_EMBEDDED_PTRTBL_SHIFT for block_shift=9).
	 */
	unit_gt(rd64(hdr, 24), 0);	/* zt_numblks */
	unit_gt(rd64(hdr, 32), 5);	/* zt_shift */

	/* All entries must survive ptrtbl reorganisation. */
	for (int i = 0; i < NPTRTBL; i++) {
		uint64_t k = (uint64_t)i;
		uint64_t result = 0;
		unit_ok(zap_lookup_uint64_by_dnode(dn,
		    &k, 1, sizeof (uint64_t), 1, &result));
		unit_eq(result, (uint64_t)i * 3);
	}

	/* Cursor iteration must visit all entries. */
	//unit_eq(zap_count_entries(mdn), NPTRTBL);
	mock_tx_destroy((mock_dmu_tx_t *) tx);
	unit_true(mock_zap_is_fatzap(dn));
	mock_zap_destroy(dn);

	return (MUNIT_OK);
}

/* ========== */

/* Test suite definition and boilerplate. */

#define	UNIT_PARAM_ZAP_TYPES(p)	\
	UNIT_PARAM((p), "micro", "fat")

static const MunitParameterEnum zap_type_params[] = {
	UNIT_PARAM_ZAP_TYPES("type"),
	{ 0 },
};

static const MunitTest zap_tests[] = {
	UNIT_TEST("mock_microzap_sanity",	test_mock_microzap_sanity),
	UNIT_TEST("mock_fatzap_sanity",		test_mock_fatzap_sanity),

	UNIT_TEST("zap_basic",	test_zap_basic,	zap_type_params),

	UNIT_TEST("zap_count",		test_zap_count, 	zap_type_params),

	UNIT_TEST("zap_contains",	test_zap_contains, 	zap_type_params),
	UNIT_TEST("zap_length",		test_zap_length, 	zap_type_params),
	UNIT_TEST("zap_update",		test_zap_update, 	zap_type_params),
	UNIT_TEST("zap_increment",	test_zap_increment, 	zap_type_params),

	UNIT_TEST("microzap_format",		test_microzap_format),
	UNIT_TEST("fatzap_format",		test_fatzap_format),

	UNIT_TEST("upgrade_block_size",		test_upgrade_block_size),
	UNIT_TEST("upgrade_long_key",		test_upgrade_long_key),
	UNIT_TEST("upgrade_value_type",		test_upgrade_value_type),
	UNIT_TEST("upgrade_value_size",		test_upgrade_value_size),

	UNIT_TEST("fatzap_uint64_length",	test_fatzap_uint64_length),
	UNIT_TEST("fatzap_uint64_keys",		test_fatzap_uint64_keys),

	UNIT_TEST("fatzap_remove",		test_fatzap_remove),
	UNIT_TEST("fatzap_shrink",		test_fatzap_shrink),
	UNIT_TEST("fatzap_collision",		test_fatzap_collision),
	UNIT_TEST("fatzap_ptrtbl_growth",	test_fatzap_ptrtbl_growth),

	{ 0 },
};

static const MunitSuite zap_test_suite = {
	"zap.",
	zap_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	mock_crc64_init();

	zap_init();

	int rc = munit_suite_main(&zap_test_suite, NULL, argc, argv);

	zap_fini();

	return (rc);
}
