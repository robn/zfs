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

/*
 * Create nvlist by parameterised type, for tests that should work on all
 * types.
 */
static nvlist_t *
nvl_create_from_params(const MunitParameter params[])
{
	nvlist_t *nvl = NULL;
	const char *unique = munit_parameters_get(params, "unique");
	if (unique == NULL)
		munit_error("nvl_create_from_params: missing unique param");
	else if (strcmp(unique, "none") == 0)
		unit_ok(nvlist_alloc(&nvl, 0, KM_SLEEP));
	else if (strcmp(unique, "name") == 0)
		unit_ok(nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP));
	else if (strcmp(unique, "name_type") == 0)
		unit_ok(nvlist_alloc(&nvl, NV_UNIQUE_NAME_TYPE, KM_SLEEP));
	else
		munit_errorf("nvl_create_from_params: invalid unique '%s'",
		    unique);
	return (nvl);
}

/*
 * Two parameter variants, as some functions aren't implemented for non-unique
 * nvlists.
 */
static const MunitParameterEnum nvs_unique_params[] = {
	UNIT_PARAM("unique", "none", "name", "name_type"),
	{ 0 },
};
static const MunitParameterEnum nvs_unique_only_params[] = {
	UNIT_PARAM("unique", "name", "name_type"),
	{ 0 },
};
/* ========== */

static MunitResult
test_nvs_add_many(const MunitParameter params[], void *data)
{
	(void) data;

	nvlist_t *nvl = nvl_create_from_params(params);

	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[i]));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nvs_replace_many(const MunitParameter params[], void *data)
{
	(void) data;

	nvlist_t *nvl = nvl_create_from_params(params);

	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[i]));
	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[i]));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nvs_iterate_many(const MunitParameter params[], void *data)
{
	(void) data;

	nvlist_t *nvl = nvl_create_from_params(params);

	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[i]));

	uint_t npairs = 0;
	nvpair_t *pair = NULL;
	while ((pair = nvlist_next_nvpair(nvl, pair)) != NULL)
		npairs++;
	unit_eq(npairs, ARRAY_SIZE(nvl_rand_keys));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nvs_lookup_many(const MunitParameter params[], void *data)
{
	(void) data;

	nvlist_t *nvl = nvl_create_from_params(params);

	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[i]));

	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++) {
		const int idx =
		    munit_rand_int_range(0, ARRAY_SIZE(nvl_rand_keys)-1);
		unit_ok(nvlist_lookup_boolean(nvl, nvl_rand_keys[idx]));
	}

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nvs_exists_many(const MunitParameter params[], void *data)
{
	(void) data;

	nvlist_t *nvl = nvl_create_from_params(params);

	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[i]));

	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++) {
		const int idx =
		    munit_rand_int_range(0, ARRAY_SIZE(nvl_rand_keys)-1);
		unit_true(nvlist_exists(nvl, nvl_rand_keys[idx]));
	}

	nvlist_free(nvl);
	return (MUNIT_OK);
}

static MunitResult
test_nvs_replace_one(const MunitParameter params[], void *data)
{
	(void) data;

	nvlist_t *nvl = nvl_create_from_params(params);

	for (uint_t i = 0; i < ARRAY_SIZE(nvl_rand_keys); i++)
		unit_ok(nvlist_add_boolean(nvl, nvl_rand_keys[0]));

	nvlist_free(nvl);
	return (MUNIT_OK);
}

/* ========== */

static const MunitTest nvpair_stress_tests[] = {
	UNIT_TEST("nvs_add_many",
	    test_nvs_add_many, nvs_unique_params),
	UNIT_TEST("nvs_replace_many",
	    test_nvs_replace_many, nvs_unique_params),
	UNIT_TEST("nvs_iterate_many",
	    test_nvs_iterate_many, nvs_unique_params),
	UNIT_TEST("nvs_lookup_many",
	    test_nvs_lookup_many, nvs_unique_only_params),
	UNIT_TEST("nvs_exists_many",
	    test_nvs_exists_many, nvs_unique_params),
	UNIT_TEST("nvs_replace_one",
	    test_nvs_replace_one, nvs_unique_only_params),

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
