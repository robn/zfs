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

#include <sys/kmem.h>
#include <sys/nvpair.h>
#include <sys/sysmacros.h>

#include "unit.h"

/*
 * The tests here are adding, removing and manipulating many thousands of pairs
 * on a single nvlist. They're not expected to fail, but rather are designed
 * to be good for profiling and optimising the nvpair internals. You almost
 * certainly should be running these under your profiler of choice, and making
 * judicious use of the --no-fork and --iterations test switches.
 */

/* ========== */

/* a big list of random keys, for stress testing */
static char nvl_rand_keys[4096][32];

static void
nvl_rand_keys_init(void)
{
	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_rand_str(nvl_rand_keys[i], sizeof (nvl_rand_keys[i]));
}

static nvlist_t *
nvl_create_type(uint_t type)
{
	nvlist_t *nvl;
	unit_ok(nvlist_alloc(&nvl, type, KM_SLEEP));
	return (nvl);
}

/* ========== */

static MunitResult
test_nvs_add_many(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_type(0);

	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[i]));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nvs_add_many_unique(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_type(NV_UNIQUE_NAME);

	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[i]));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nvs_add_many_unique_type(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_type(NV_UNIQUE_NAME_TYPE);

	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[i]));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nvs_replace_many(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_type(0);

	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[i]));
	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[i]));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nvs_replace_many_unique(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_type(NV_UNIQUE_NAME);

	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[i]));
	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[i]));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nvs_replace_many_unique_type(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *nvl = nvl_create_type(NV_UNIQUE_NAME_TYPE);

	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[i]));
	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[i]));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* ========== */

static const MunitTest nvpair_stress_tests[] = {

	/* hashtable stress tests */
	UNIT_TEST("nvs_add_many",		test_nvs_add_many),
	UNIT_TEST("nvs_add_many_unique",	test_nvs_add_many_unique),
	UNIT_TEST("nvs_add_many_unique_type",	test_nvs_add_many_unique_type),
	UNIT_TEST("nvs_replace_many",		test_nvs_replace_many),
	UNIT_TEST("nvs_replace_many_unique",	test_nvs_replace_many_unique),
	UNIT_TEST("nvs_replace_many_unique_type",
	    test_nvs_replace_many_unique_type),

	{ 0 },
};

static const MunitSuite nvpair_stress_test_suite = {
	"nvs.",
	nvpair_stress_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	nvl_rand_keys_init();
	return (munit_suite_main(&nvpair_stress_test_suite, NULL, argc, argv));
}
