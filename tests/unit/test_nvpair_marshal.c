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
#include <sys/nvpair_marshal.h>
#include <libnvpair.h>

#include "unit.h"

/* ========== */

NVM_SCHEMA(nvm_flag_t,
	("flag",  NVM_FLAG,  flag)
);

static MunitResult
test_nvm_flag_false(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_flag_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_flag_t, &a));
	unit_false(a.flag);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_flag_t, &a));
	unit_false(nvlist_exists(out, "flag"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

static MunitResult
test_nvm_flag_true(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_boolean(in, "flag");

	nvm_flag_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_flag_t, &a));
	unit_true(a.flag);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_flag_t, &a));
	unit_true(nvlist_exists(out, "flag"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

/* ========== */

/* Round-trip unmarshal/marhsal tests for scalar types. */

NVM_SCHEMA(nvm_boolean_t,
	("b",  NVM_SCALAR(BOOLEAN),  b,  NVM_REQUIRED)
);

static MunitResult
test_nvm_boolean_false(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_boolean_value(in, "b", B_FALSE);

	nvm_boolean_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_boolean_t, &a));

	unit_false(a.b);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_boolean_t, &a));

	unit_false(fnvlist_lookup_boolean_value(out, "b"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

static MunitResult
test_nvm_boolean_true(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_boolean_value(in, "b", B_TRUE);

	nvm_boolean_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_boolean_t, &a));

	unit_true(a.b);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_boolean_t, &a));

	unit_true(fnvlist_lookup_boolean_value(out, "b"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_byte_t,
	("byte",  NVM_SCALAR(BYTE),  byte,  NVM_REQUIRED)
);

static MunitResult
test_nvm_byte(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_byte(in, "byte", 0x42);

	nvm_byte_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_byte_t, &a));

	unit_eq(a.byte, 0x42);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_byte_t, &a));

	unit_eq(fnvlist_lookup_byte(out, "byte"), 0x42);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_int8_t,
	("i8",  NVM_SCALAR(INT8),  i8,  NVM_REQUIRED)
);

static MunitResult
test_nvm_int8(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_int8(in, "i8", -12);

	nvm_int8_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_int8_t, &a));

	unit_eq(a.i8, -12);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_int8_t, &a));

	unit_eq(fnvlist_lookup_int8(out, "i8"), -12);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_uint8_t,
	("u8",  NVM_SCALAR(UINT8),  u8,  NVM_REQUIRED)
);

static MunitResult
test_nvm_uint8(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_uint8(in, "u8", 200);

	nvm_uint8_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_uint8_t, &a));

	unit_eq(a.u8, 200);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_uint8_t, &a));

	unit_eq(fnvlist_lookup_uint8(out, "u8"), 200);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_int16_t,
	("i16",		NVM_SCALAR(INT16),   i16,    NVM_REQUIRED)
);

static MunitResult
test_nvm_int16(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_int16(in, "i16", -1234);

	nvm_int16_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_int16_t, &a));

	unit_eq(a.i16, -1234);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_int16_t, &a));

	unit_eq(fnvlist_lookup_int16(out, "i16"), -1234);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_uint16_t,
	("u16",  NVM_SCALAR(UINT16),  u16,  NVM_REQUIRED)
);

static MunitResult
test_nvm_uint16(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_uint16(in, "u16", 54321);

	nvm_uint16_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_uint16_t, &a));

	unit_eq(a.u16, 54321);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_uint16_t, &a));

	unit_eq(fnvlist_lookup_uint16(out, "u16"), 54321);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_int32_t,
	("i32",  NVM_SCALAR(INT32),  i32,  NVM_REQUIRED)
);

static MunitResult
test_nvm_int32(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_int32(in, "i32", -123456);

	nvm_int32_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_int32_t, &a));

	unit_eq(a.i32, -123456);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_int32_t, &a));

	unit_eq(fnvlist_lookup_int32(out, "i32"), -123456);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_uint32_t,
	("u32",  NVM_SCALAR(UINT32),  u32,  NVM_REQUIRED)
);

static MunitResult
test_nvm_uint32(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_uint32(in, "u32", 3000000000);

	nvm_uint32_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_uint32_t, &a));

	unit_eq(a.u32, 3000000000);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_uint32_t, &a));

	unit_eq(fnvlist_lookup_uint32(out, "u32"), 3000000000);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_int64_t,
	("i64",  NVM_SCALAR(INT64),  i64,  NVM_REQUIRED)
);

static MunitResult
test_nvm_int64(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_int64(in, "i64", -9876546410LL);

	nvm_int64_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_int64_t, &a));

	unit_eq(a.i64, -9876546410LL);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_int64_t, &a));

	unit_eq(fnvlist_lookup_int64(out, "i64"), -9876546410LL);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_uint64_t,
	("u64",  NVM_SCALAR(UINT64),  u64,  NVM_REQUIRED)
);

static MunitResult
test_nvm_uint64(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_uint64(in, "u64", 12345678901234567890ULL);

	nvm_uint64_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_uint64_t, &a));

	unit_eq(a.u64, 12345678901234567890ULL);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_uint64_t, &a));

	unit_eq(fnvlist_lookup_uint64(out, "u64"), 12345678901234567890ULL);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_string_t,
	("str",  NVM_SCALAR(STRING),  str,  NVM_REQUIRED)
);

static MunitResult
test_nvm_string(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_string(in, "str", "hello");

	nvm_string_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_string_t, &a));

	unit_str_eq(a.str, "hello");

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_string_t, &a));

	unit_str_eq(fnvlist_lookup_string(out, "str"), "hello");

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_hrtime_t,
	("ts",  NVM_SCALAR(HRTIME),  ts,  NVM_REQUIRED)
);

static MunitResult
test_nvm_hrtime(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	unit_ok(nvlist_add_hrtime(in, "ts", SEC2NSEC(10)));

	nvm_hrtime_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_hrtime_t, &a));

	unit_eq(a.ts, SEC2NSEC(10));

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_hrtime_t, &a));

	hrtime_t ts;
	unit_ok(nvlist_lookup_hrtime(out, "ts", &ts));
	unit_eq(ts, SEC2NSEC(10));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_double_t,
	("d",  NVM_SCALAR(DOUBLE),  d,  NVM_REQUIRED)
);

static MunitResult
test_nvm_double(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	unit_ok(nvlist_add_double(in, "d", 3.5));

	nvm_double_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_double_t, &a));

	unit_eq(a.d, 3.5);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_double_t, &a));

	double d;
	unit_ok(nvlist_lookup_double(out, "d", &d));
	unit_eq(d, 3.5);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_nvlist_t,
	("nv",  NVM_SCALAR(NVLIST),  nv,  NVM_REQUIRED)
);

static MunitResult
test_nvm_nvlist(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvlist_t *nv = fnvlist_alloc();
	fnvlist_add_uint64(nv, "key", 1);
	fnvlist_add_nvlist(in, "nv", nv);
	fnvlist_free(nv);

	nvm_nvlist_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_nvlist_t, &a));

	unit_eq(fnvlist_lookup_uint64(a.nv, "key"), 1);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_nvlist_t, &a));

	unit_ok(nvlist_lookup_nvlist(out, "nv", &nv));
	unit_eq(fnvlist_lookup_uint64(nv, "key"), 1);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

/* ========== */

NVM_SCHEMA(nvm_boolean_array_t,
	("arr",  NVM_ARRAY(BOOLEAN),  arr,  NVM_REQUIRED)
);

static MunitResult
test_nvm_boolean_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	const boolean_t arr[] = { B_TRUE, B_FALSE, B_TRUE };
	unit_ok(nvlist_add_boolean_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_boolean_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_boolean_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_boolean_array_t, &a));

	boolean_t *out_arr;
	uint_t n;
	unit_ok(nvlist_lookup_boolean_array(out, "arr", &out_arr, &n));
	unit_eq(n, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < n; i++)
		unit_eq(out_arr[i], arr[i]);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}
NVM_SCHEMA(nvm_byte_array_t,
	("arr",  NVM_ARRAY(BYTE),  arr,  NVM_REQUIRED)
);

static MunitResult
test_nvm_byte_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	uchar_t arr[] = { 10, 20, 30 };
	unit_ok(nvlist_add_byte_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_byte_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_byte_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_byte_array_t, &a));

	uchar_t *out_arr;
	uint_t n;
	unit_ok(nvlist_lookup_byte_array(out, "arr", &out_arr, &n));
	unit_eq(n, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < n; i++)
		unit_eq(out_arr[i], arr[i]);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_int8_array_t,
	("arr",  NVM_ARRAY(INT8),  arr,  NVM_REQUIRED)
);

static MunitResult
test_nvm_int8_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	int8_t arr[] = { -1, 0, 1 };
	unit_ok(nvlist_add_int8_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_int8_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_int8_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_int8_array_t, &a));

	int8_t *out_arr;
	uint_t n;
	unit_ok(nvlist_lookup_int8_array(out, "arr", &out_arr, &n));
	unit_eq(n, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < n; i++)
		unit_eq(out_arr[i], arr[i]);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_uint8_array_t,
	("arr",  NVM_ARRAY(UINT8),  arr,  NVM_REQUIRED)
);

static MunitResult
test_nvm_uint8_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	uint8_t arr[] = { 10, 20, 30 };
	unit_ok(nvlist_add_uint8_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_uint8_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_uint8_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_uint8_array_t, &a));

	uint8_t *out_arr;
	uint_t n;
	unit_ok(nvlist_lookup_uint8_array(out, "arr", &out_arr, &n));
	unit_eq(n, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < n; i++)
		unit_eq(out_arr[i], arr[i]);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_int16_array_t,
	("arr",  NVM_ARRAY(INT16),  arr,  NVM_REQUIRED)
);

static MunitResult
test_nvm_int16_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	int16_t arr[] = { -100, 0, 100 };
	unit_ok(nvlist_add_int16_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_int16_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_int16_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_int16_array_t, &a));

	int16_t *out_arr;
	uint_t n;
	unit_ok(nvlist_lookup_int16_array(out, "arr", &out_arr, &n));
	unit_eq(n, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < n; i++)
		unit_eq(out_arr[i], arr[i]);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_uint16_array_t,
	("arr",  NVM_ARRAY(UINT16),  arr,  NVM_REQUIRED)
);

static MunitResult
test_nvm_uint16_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	uint16_t arr[] = { 1000, 2000, 3000 };
	unit_ok(nvlist_add_uint16_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_uint16_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_uint16_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_uint16_array_t, &a));

	uint16_t *out_arr;
	uint_t n;
	unit_ok(nvlist_lookup_uint16_array(out, "arr", &out_arr, &n));
	unit_eq(n, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < n; i++)
		unit_eq(out_arr[i], arr[i]);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_int32_array_t,
	("arr",  NVM_ARRAY(INT32),  arr,  NVM_REQUIRED)
);

static MunitResult
test_nvm_int32_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	int32_t arr[] = { -100000, 0, 100000 };
	unit_ok(nvlist_add_int32_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_int32_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_int32_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_int32_array_t, &a));

	int32_t *out_arr;
	uint_t n;
	unit_ok(nvlist_lookup_int32_array(out, "arr", &out_arr, &n));
	unit_eq(n, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < n; i++)
		unit_eq(out_arr[i], arr[i]);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_uint32_array_t,
	("arr",  NVM_ARRAY(UINT32),  arr,  NVM_REQUIRED)
);

static MunitResult
test_nvm_uint32_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	uint32_t arr[] = { 100000, 200000, 300000 };
	unit_ok(nvlist_add_uint32_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_uint32_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_uint32_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_uint32_array_t, &a));

	uint32_t *out_arr;
	uint_t n;
	unit_ok(nvlist_lookup_uint32_array(out, "arr", &out_arr, &n));
	unit_eq(n, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < n; i++)
		unit_eq(out_arr[i], arr[i]);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_int64_array_t,
	("arr",  NVM_ARRAY(INT64),  arr,  NVM_REQUIRED)
);

static MunitResult
test_nvm_int64_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	int64_t arr[] = { -1000000000LL, 0, 1000000000LL };
	unit_ok(nvlist_add_int64_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_int64_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_int64_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_int64_array_t, &a));

	int64_t *out_arr;
	uint_t n;
	unit_ok(nvlist_lookup_int64_array(out, "arr", &out_arr, &n));
	unit_eq(n, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < n; i++)
		unit_eq(out_arr[i], arr[i]);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_uint64_array_t,
	("arr",  NVM_ARRAY(UINT64),  arr,  NVM_REQUIRED)
);

static MunitResult
test_nvm_uint64_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	uint64_t arr[] = { 1, 2, 3, 4, 5 };
	unit_ok(nvlist_add_uint64_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_uint64_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_uint64_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_uint64_array_t, &a));

	uint64_t *out_arr;
	uint_t n;
	unit_ok(nvlist_lookup_uint64_array(out, "arr", &out_arr, &n));
	unit_eq(n, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < n; i++)
		unit_eq(out_arr[i], arr[i]);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_string_array_t,
	("arr",  NVM_ARRAY(STRING),  arr,  NVM_REQUIRED)
);

static MunitResult
test_nvm_string_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	const char *arr[] = { "alpha", "beta", "gamma" };
	unit_ok(nvlist_add_string_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_string_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_string_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_str_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_string_array_t, &a));

	char **out_arr;
	uint_t n;
	unit_ok(nvlist_lookup_string_array(out, "arr", &out_arr, &n));
	unit_eq(n, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < n; i++)
		unit_str_eq(out_arr[i], arr[i]);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_nvlist_array_t,
	("arr",  NVM_ARRAY(NVLIST),  arr,  NVM_REQUIRED)
);

static MunitResult
test_nvm_nvlist_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvlist_t *nv0 = fnvlist_alloc();
	nvlist_t *nv1 = fnvlist_alloc();
	unit_ok(nvlist_add_uint64(nv0, "key", 0));
	unit_ok(nvlist_add_uint64(nv1, "key", 1));

	const nvlist_t *arr[] = { nv0, nv1 };
	unit_ok(nvlist_add_nvlist_array(in, "arr", arr, ARRAY_SIZE(arr)));
	nvlist_free(nv0);
	nvlist_free(nv1);

	nvm_nvlist_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_nvlist_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	unit_eq(fnvlist_lookup_uint64(a.arr[0], "key"), 0);
	unit_eq(fnvlist_lookup_uint64(a.arr[1], "key"), 1);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_nvlist_array_t, &a));

	nvlist_t **out_arr;
	uint_t n;
	unit_ok(nvlist_lookup_nvlist_array(out, "arr", &out_arr, &n));
	unit_eq(n, ARRAY_SIZE(arr));
	unit_eq(fnvlist_lookup_uint64(out_arr[0], "key"), 0);
	unit_eq(fnvlist_lookup_uint64(out_arr[1], "key"), 1);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

/* ========== */

NVM_SCHEMA(nvm_struct_inner_t,
    ("key",  NVM_SCALAR(UINT64),  key,  NVM_REQUIRED)
);
NVM_SCHEMA(nvm_struct_t,
    ("nv",  NVM_STRUCT(nvm_struct_inner_t),  nv,  NVM_REQUIRED)
);

static MunitResult
test_nvm_struct(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvlist_t *nv = fnvlist_alloc();
	fnvlist_add_uint64(nv, "key", 1);
	fnvlist_add_nvlist(in, "nv", nv);
	fnvlist_free(nv);

	nvm_struct_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_struct_t, &a));

	unit_eq(a.nv.key, 1);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_struct_t, &a));

	unit_ok(nvlist_lookup_nvlist(out, "nv", &nv));
	unit_eq(fnvlist_lookup_uint64(nv, "key"), 1);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_struct_array_inner_t,
    ("key",  NVM_SCALAR(UINT64),  key,  NVM_REQUIRED)
);
NVM_SCHEMA(nvm_struct_array_t,
    ("arr",  NVM_STRUCT_ARRAY(nvm_struct_array_inner_t),  arr,  NVM_REQUIRED)
);

static MunitResult
test_nvm_struct_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvlist_t *nv0 = fnvlist_alloc();
	nvlist_t *nv1 = fnvlist_alloc();
	unit_ok(nvlist_add_uint64(nv0, "key", 0));
	unit_ok(nvlist_add_uint64(nv1, "key", 1));

	const nvlist_t *arr[] = { nv0, nv1 };
	unit_ok(nvlist_add_nvlist_array(in, "arr", arr, ARRAY_SIZE(arr)));
	nvlist_free(nv0);
	nvlist_free(nv1);

	nvm_struct_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_struct_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	unit_eq(a.arr[0].key, 0);
	unit_eq(a.arr[1].key, 1);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_struct_array_t, &a));

	nvlist_t **out_arr;
	uint_t n;
	unit_ok(nvlist_lookup_nvlist_array(out, "arr", &out_arr, &n));
	unit_eq(n, ARRAY_SIZE(arr));
	unit_eq(fnvlist_lookup_uint64(out_arr[0], "key"), 0);
	unit_eq(fnvlist_lookup_uint64(out_arr[1], "key"), 1);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

/* ========== */

NVM_SCHEMA(nvm_map_t,
	("map",  NVM_MAP(UINT64),  map,  NVM_REQUIRED),
);

static MunitResult
test_nvm_map(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvlist_t *map = fnvlist_alloc();
	unit_ok(nvlist_add_uint64(map, "foo", 1));
	unit_ok(nvlist_add_uint64(map, "bar", 2));
	unit_ok(nvlist_add_uint64(map, "baz", 3));
	unit_ok(nvlist_add_nvlist(in, "map", map));
	fnvlist_free(map);

	nvm_map_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_map_t, &a));

	unit_eq(a.nelem_map, 3);

	unit_str_eq(a.map[0].name, "foo");
	unit_eq(a.map[0].value, 1);

	unit_str_eq(a.map[1].name, "bar");
	unit_eq(a.map[1].value, 2);

	unit_str_eq(a.map[2].name, "baz");
	unit_eq(a.map[2].value, 3);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_map_t, &a));

	nvlist_t *out_map;
	unit_ok(nvlist_lookup_nvlist(out, "map", &out_map));

	unit_eq(fnvlist_num_pairs(out_map), 3);

	unit_eq(fnvlist_lookup_uint64(out_map, "foo"), 1);
	unit_eq(fnvlist_lookup_uint64(out_map, "bar"), 2);
	unit_eq(fnvlist_lookup_uint64(out_map, "baz"), 3);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_set_t,
	("set",  NVM_SET,  set,  NVM_REQUIRED),
);

static MunitResult
test_nvm_set(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvlist_t *set = fnvlist_alloc();
	unit_ok(nvlist_add_boolean(set, "foo"));
	unit_ok(nvlist_add_boolean(set, "bar"));
	unit_ok(nvlist_add_boolean(set, "baz"));
	unit_ok(nvlist_add_nvlist(in, "set", set));
	fnvlist_free(set);

	nvm_set_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_set_t, &a));

	unit_eq(a.nelem_set, 3);

	unit_str_eq(a.set[0], "foo");
	unit_str_eq(a.set[1], "bar");
	unit_str_eq(a.set[2], "baz");

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_set_t, &a));

	nvlist_t *out_set;
	unit_ok(nvlist_lookup_nvlist(out, "set", &out_set));

	unit_eq(fnvlist_num_pairs(out_set), 3);

	unit_true(fnvlist_lookup_boolean(out_set, "foo"));
	unit_true(fnvlist_lookup_boolean(out_set, "bar"));
	unit_true(fnvlist_lookup_boolean(out_set, "baz"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

/* ========== */

NVM_SCHEMA(nvm_type_mismatch_scalar_t,
	("val",  NVM_SCALAR(UINT64),  val,  NVM_REQUIRED)
);

static MunitResult
test_nvm_type_mismatch_scalar(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	unit_ok(nvlist_add_uint32(in, "val", 32));

	nvm_type_mismatch_scalar_t a;
	unit_err(NVM_UNMARSHAL(in, nvm_type_mismatch_scalar_t, &a), ENOENT);

	fnvlist_free(in);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_type_mismatch_array_t,
	("val",  NVM_ARRAY(UINT64),  val,  NVM_REQUIRED)
);

static MunitResult
test_nvm_type_mismatch_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	unit_ok(nvlist_add_uint32(in, "val", 32));

	nvm_type_mismatch_array_t a;
	unit_err(NVM_UNMARSHAL(in, nvm_type_mismatch_array_t, &a), ENOENT);

	fnvlist_free(in);
	return (MUNIT_OK);
}

/* ========== */

NVM_SCHEMA(nvm_opt_scalar_t,
	("val",  NVM_SCALAR(UINT64),  val,  NVM_OPTIONAL)
);

static MunitResult
test_nvm_opt_scalar_false(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_opt_scalar_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_opt_scalar_t, &a));

	unit_false(a.has_val);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_opt_scalar_t, &a));

	unit_false(nvlist_exists(out, "val"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

static MunitResult
test_nvm_opt_scalar_true(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	fnvlist_add_uint64(in, "val", 1);

	nvm_opt_scalar_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_opt_scalar_t, &a));

	unit_true(a.has_val);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_opt_scalar_t, &a));

	unit_true(nvlist_exists(out, "val"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_opt_array_t,
	("arr",  NVM_ARRAY(UINT64),  arr,  NVM_OPTIONAL)
);

static MunitResult
test_nvm_opt_array_false(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_opt_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_opt_array_t, &a));

	unit_false(a.has_arr);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_opt_array_t, &a));

	unit_false(nvlist_exists(out, "arr"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

static MunitResult
test_nvm_opt_array_true(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	uint64_t arr[] = {};
	unit_ok(nvlist_add_uint64_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_opt_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_opt_array_t, &a));

	unit_true(a.has_arr);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_opt_array_t, &a));

	unit_true(nvlist_exists(out, "arr"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_opt_struct_inner_t,
	("key",  NVM_SCALAR(UINT64),  key,  NVM_REQUIRED)
);
NVM_SCHEMA(nvm_opt_struct_t,
	("nv",  NVM_STRUCT(nvm_opt_struct_inner_t),  nv,  NVM_OPTIONAL)
);

static MunitResult
test_nvm_opt_struct_false(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_opt_struct_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_opt_struct_t, &a));

	unit_false(a.has_nv);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_opt_struct_t, &a));

	unit_false(nvlist_exists(out, "nv"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

static MunitResult
test_nvm_opt_struct_true(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvlist_t *nv = fnvlist_alloc();
	fnvlist_add_uint64(nv, "key", 1);
	fnvlist_add_nvlist(in, "nv", nv);
	fnvlist_free(nv);

	nvm_opt_struct_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_opt_struct_t, &a));

	unit_true(a.has_nv);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_opt_struct_t, &a));

	unit_true(nvlist_exists(out, "nv"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_opt_struct_array_inner_t,
	("key",  NVM_SCALAR(UINT64),  key,  NVM_REQUIRED)
);
NVM_SCHEMA(nvm_opt_struct_array_t,
	("arr",  NVM_STRUCT_ARRAY(nvm_opt_struct_inner_t),  arr,  NVM_OPTIONAL)
);

static MunitResult
test_nvm_opt_struct_array_false(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_opt_struct_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_opt_struct_array_t, &a));

	unit_false(a.has_arr);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_opt_struct_array_t, &a));

	unit_false(nvlist_exists(out, "arr"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

static MunitResult
test_nvm_opt_struct_array_true(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvlist_t *nv0 = fnvlist_alloc();
	nvlist_t *nv1 = fnvlist_alloc();
	unit_ok(nvlist_add_uint64(nv0, "key", 0));
	unit_ok(nvlist_add_uint64(nv1, "key", 1));

	const nvlist_t *arr[] = { nv0, nv1 };
	unit_ok(nvlist_add_nvlist_array(in, "arr", arr, ARRAY_SIZE(arr)));
	nvlist_free(nv0);
	nvlist_free(nv1);

	nvm_opt_struct_array_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_opt_struct_array_t, &a));

	unit_true(a.has_arr);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_opt_struct_array_t, &a));

	unit_true(nvlist_exists(out, "arr"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_opt_map_t,
	("map",  NVM_MAP(UINT64),  map,  NVM_OPTIONAL),
);

static MunitResult
test_nvm_opt_map_false(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_opt_map_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_opt_map_t, &a));

	unit_false(a.has_map);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_opt_map_t, &a));

	unit_false(nvlist_exists(out, "map"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

static MunitResult
test_nvm_opt_map_true(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvlist_t *map = fnvlist_alloc();
	unit_ok(nvlist_add_nvlist(in, "map", map));
	fnvlist_free(map);

	nvm_opt_map_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_opt_map_t, &a));

	unit_true(a.has_map);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_opt_map_t, &a));

	unit_true(nvlist_exists(out, "map"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_opt_set_t,
	("set",  NVM_SET,  set,  NVM_OPTIONAL),
);

static MunitResult
test_nvm_opt_set_false(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_opt_set_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_opt_set_t, &a));

	unit_false(a.has_set);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_opt_set_t, &a));

	unit_false(nvlist_exists(out, "set"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

static MunitResult
test_nvm_opt_set_true(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvlist_t *set = fnvlist_alloc();
	unit_ok(nvlist_add_nvlist(in, "set", set));
	fnvlist_free(set);

	nvm_opt_set_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_opt_set_t, &a));

	unit_true(a.has_set);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_opt_set_t, &a));

	unit_true(nvlist_exists(out, "set"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

/* ========== */

NVM_SCHEMA(nvm_req_missing_t,
	("val",  NVM_SCALAR(UINT64),  val)
);

static MunitResult
test_nvm_req_missing(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_req_missing_t a;
	unit_err(NVM_UNMARSHAL(in, nvm_req_missing_t, &a), ENOENT);

	fnvlist_add_uint64(in, "val", 1);

	unit_ok(NVM_UNMARSHAL(in, nvm_req_missing_t, &a));

	fnvlist_free(in);
	return (MUNIT_OK);
}

/* ========== */

NVM_SCHEMA(nvm_opt_zero_struct_t,
	("val",  NVM_SCALAR(UINT64),   val,    NVM_REQUIRED),
);

NVM_SCHEMA(nvm_opt_zero_t,
	("b",       NVM_SCALAR(BOOLEAN),                        b,       NVM_OPTIONAL),
	("val",     NVM_SCALAR(UINT64),                         val,     NVM_OPTIONAL),
	("str",     NVM_SCALAR(STRING),                         str,     NVM_OPTIONAL),
	("nv",      NVM_SCALAR(NVLIST),                         nv,      NVM_OPTIONAL),
	("arr",     NVM_ARRAY(UINT64),                        arr,     NVM_OPTIONAL),
	("arr_n",   NVM_ARRAY_N(UINT64, 5),                   arr_n,   NVM_OPTIONAL),
	("st",      NVM_STRUCT(nvm_opt_zero_struct_t),        st,      NVM_OPTIONAL),
	("st_arr",  NVM_STRUCT_ARRAY(nvm_opt_zero_struct_t),  st_arr,  NVM_OPTIONAL),
);

static MunitResult
test_nvm_opt_zero(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_opt_zero_t a;
	memset(&a, 0x5a, sizeof (a));
	unit_ok(NVM_UNMARSHAL(in, nvm_opt_zero_t, &a));

	unit_false(a.b);
	unit_zero(a.val);
	unit_null(a.str);
	unit_null(a.nv);
	unit_null(a.arr);
	for (uint_t i = 0; i < ARRAY_SIZE(a.arr_n); i++)
		unit_zero(a.arr_n[i]);
	//unit_null(a.st);
	unit_null(a.st_arr);

	fnvlist_free(in);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_opt_default_t,
	("val",		NVM_SCALAR(UINT64),	val,	NVM_DEFAULT(23)),
	("b",		NVM_SCALAR(BOOLEAN),	b,	NVM_DEFAULT(B_TRUE)),
	("str",		NVM_SCALAR(STRING),	str,	NVM_DEFAULT("hello")),
);

static MunitResult
test_nvm_opt_default(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_opt_default_t a;
	memset(&a, 0x5a, sizeof (a));
	unit_ok(NVM_UNMARSHAL(in, nvm_opt_default_t, &a));

	unit_true(a.b);
	unit_false(a.has_b);
	unit_eq(a.val, 23);
	unit_false(a.has_val);
	unit_str_eq(a.str, "hello");
	unit_false(a.has_str);

	fnvlist_add_uint64(in, "val", 23);
	memset(&a, 0x5a, sizeof (a));
	unit_ok(NVM_UNMARSHAL(in, nvm_opt_default_t, &a));

	unit_eq(a.val, 23);
	unit_true(a.has_val);

	fnvlist_free(in);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_partial_default_t,
	("def",		NVM_SCALAR(UINT64),	def,	NVM_DEFAULT(23)),
	("req",		NVM_SCALAR(UINT64),	req,	NVM_REQUIRED),
	("opt",		NVM_SCALAR(UINT64),	opt,	NVM_OPTIONAL),
	("arr",		NVM_ARRAY(UINT64),	arr,	NVM_OPTIONAL),
	("arr_n",	NVM_ARRAY_N(UINT64, 5),	arr_n,	NVM_OPTIONAL),
);

static MunitResult
test_nvm_partial_default(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	fnvlist_add_uint64(in, "def", 555);
	fnvlist_add_uint64(in, "opt", 999);

	nvm_partial_default_t a;
	memset(&a, 0x5a, sizeof (a));
	unit_err(NVM_UNMARSHAL(in, nvm_partial_default_t, &a), ENOENT);

	unit_eq(a.def, 23);
	unit_false(a.has_def);

	unit_zero(a.req);

	unit_zero(a.opt);
	unit_false(a.has_opt);

	unit_null(a.arr);
	unit_zero(a.nelem_arr);
	unit_false(a.has_arr);

	for (uint_t i = 0; i < ARRAY_SIZE(a.arr_n); i++)
		unit_zero(a.arr_n[i]);
	unit_false(a.has_arr_n);

	fnvlist_free(in);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_nv_null_req_t,
	("val",		NVM_SCALAR(UINT64),	val,	NVM_REQUIRED),
);

static MunitResult
test_nvm_nv_null_req(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvm_nv_null_req_t a;
	unit_err(NVM_UNMARSHAL(NULL, nvm_nv_null_req_t, &a), ENOENT);

	unit_zero(a.val);

	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_nv_null_opt_t,
	("val",		NVM_SCALAR(UINT64),	val,	NVM_OPTIONAL),
);

static MunitResult
test_nvm_nv_null_opt(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvm_nv_null_opt_t a;
	unit_ok(NVM_UNMARSHAL(NULL, nvm_nv_null_opt_t, &a));

	unit_zero(a.val);
	unit_false(a.has_val);

	return (MUNIT_OK);
}

/* ========== */

NVM_SCHEMA(nvm_scalar_array_n_t,
	("arr",  NVM_ARRAY_N(UINT64, 5),  arr,  NVM_REQUIRED)
);

static MunitResult
test_nvm_scalar_array_n(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	uint64_t arr[] = { 1, 2, 3, 4, 5 };
	unit_ok(nvlist_add_uint64_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_scalar_array_n_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_scalar_array_n_t, &a));

	for (uint_t i = 0; i < ARRAY_SIZE(arr); i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_scalar_array_n_t, &a));

	uint64_t *out_arr;
	uint_t n;
	unit_ok(nvlist_lookup_uint64_array(out, "arr", &out_arr, &n));
	unit_eq(n, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < n; i++)
		unit_eq(out_arr[i], arr[i]);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_scalar_array_n_str_t,
	("arr",  NVM_ARRAY_N(STRING, 3),  arr,  NVM_REQUIRED)
);

static MunitResult
test_nvm_scalar_array_n_str(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	const char *arr[] = { "foo", "bar", "baz" };
	unit_ok(nvlist_add_string_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_scalar_array_n_str_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_scalar_array_n_str_t, &a));

	for (uint_t i = 0; i < ARRAY_SIZE(arr); i++)
		unit_str_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_scalar_array_n_str_t, &a));

	char **out_arr;
	uint_t n;
	unit_ok(nvlist_lookup_string_array(out, "arr", &out_arr, &n));
	unit_eq(n, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < n; i++)
		unit_str_eq(out_arr[i], arr[i]);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

static MunitResult
test_nvm_scalar_array_n_erange(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	uint64_t arr[] = { 1, 2, 3, 4 };
	unit_ok(nvlist_add_uint64_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_scalar_array_n_t a;
	unit_err(NVM_UNMARSHAL(in, nvm_scalar_array_n_t, &a), ERANGE);

	fnvlist_free(in);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_scalar_array_n_opt_t,
	("arr",  NVM_ARRAY_N(UINT64, 5),  arr,  NVM_OPTIONAL)
);

static MunitResult
test_nvm_scalar_array_n_opt(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_scalar_array_n_opt_t a;

	/* missing optional key is ok, as always */
	unit_ok(NVM_UNMARSHAL(in, nvm_scalar_array_n_opt_t, &a));
	unit_false(a.has_arr);

	/* if its there, it must have the correct number of elements */
	uint64_t arr[] = { 1, 2, 3, 4, 5 };
	unit_ok(nvlist_add_uint64_array(in, "arr", arr, ARRAY_SIZE(arr)));
	unit_ok(NVM_UNMARSHAL(in, nvm_scalar_array_n_opt_t, &a));
	unit_true(a.has_arr);

	/* present with wrong number of elements still fails */
	unit_ok(nvlist_add_uint64_array(in, "arr", arr, ARRAY_SIZE(arr)-1));
	unit_err(NVM_UNMARSHAL(in, nvm_scalar_array_n_opt_t, &a), ERANGE);

	fnvlist_free(in);
	return (MUNIT_OK);
}

/* ========== */

NVM_SCHEMA(nvm_extra_keys_t,
	("foo",  NVM_SCALAR(UINT64),  foo,  NVM_REQUIRED)
);

static MunitResult
test_nvm_extra_keys(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	unit_ok(nvlist_add_uint64(in, "foo", 1));
	unit_ok(nvlist_add_uint64(in, "bar", 2));
	unit_ok(nvlist_add_uint64(in, "baz", 3));

	nvm_extra_keys_t a;
	unit_err(NVM_UNMARSHAL(in, nvm_extra_keys_t, &a), E2BIG);

	fnvlist_free(in);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_spill_t,
	("foo",  NVM_SCALAR(UINT64),  foo,  NVM_REQUIRED),
	(NULL,   NVM_MAP(UINT64),  spill)
);

static MunitResult
test_nvm_spill(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	unit_ok(nvlist_add_uint64(in, "foo", 1));
	unit_ok(nvlist_add_uint64(in, "bar", 2));
	unit_ok(nvlist_add_uint64(in, "baz", 3));

	nvm_spill_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_spill_t, &a));

	unit_eq(a.foo, 1);

	unit_eq(a.nelem_spill, 2);

	unit_str_eq(a.spill[0].name, "bar");
	unit_eq(a.spill[0].value, 2);

	unit_str_eq(a.spill[1].name, "baz");
	unit_eq(a.spill[1].value, 3);

	fnvlist_free(in);
	return (MUNIT_OK);
}

static MunitResult
test_nvm_spill_type(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	unit_ok(nvlist_add_uint64(in, "foo", 1));
	unit_ok(nvlist_add_uint64(in, "bar", 2));
	unit_ok(nvlist_add_uint32(in, "baz", 3));

	nvm_spill_t a;
	unit_err(NVM_UNMARSHAL(in, nvm_spill_t, &a), E2BIG);

	fnvlist_free(in);
	return (MUNIT_OK);
}

static MunitResult
test_nvm_spill_none(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	unit_ok(nvlist_add_uint64(in, "foo", 1));

	nvm_spill_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_spill_t, &a));

	unit_eq(a.foo, 1);

	unit_zero(a.nelem_spill);

	fnvlist_free(in);
	return (MUNIT_OK);
}

NVM_SCHEMA(nvm_spill_flags_t,
	(NULL,   NVM_SET,  spill)
);

static MunitResult
test_nvm_spill_flags(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	unit_ok(nvlist_add_boolean(in, "foo"));
	unit_ok(nvlist_add_boolean(in, "bar"));
	unit_ok(nvlist_add_boolean(in, "baz"));

	nvm_spill_flags_t a;
	unit_ok(NVM_UNMARSHAL(in, nvm_spill_flags_t, &a));

	unit_eq(a.nelem_spill, 3);

	fnvlist_free(in);
	return (MUNIT_OK);
}

/* ========== */

/*
 * Testing a deep & complex schema. This generates a lot of random data and
 * layouts in an effort to exercise the rather difficult pointer control in
 * nvm_unmarshal() and nvm_marshal(). Running a few thousand iterations of
 * this test is a reasonably good stress of the system.
 */

NVM_SCHEMA(nvm_complex_elem_t,
	("flag", NVM_FLAG,				flag),
	("str",	 NVM_SCALAR(STRING),			str,	NVM_OPTIONAL)
);

NVM_SCHEMA(nvm_complex_inner_t,
	("i32",	 NVM_SCALAR(INT32),			i32,	NVM_OPTIONAL),
	("elems", NVM_STRUCT_ARRAY(nvm_complex_elem_t),	elems,	NVM_REQUIRED)
);

NVM_SCHEMA(nvm_complex_t,
	("u64",  NVM_SCALAR(UINT64),			u64,	NVM_REQUIRED),
	("arr",  NVM_ARRAY(UINT64),			arr,	NVM_REQUIRED),
	("inner", NVM_STRUCT(nvm_complex_inner_t),	inner,	NVM_REQUIRED),
	("arr5", NVM_ARRAY_N(INT8, 5),			arr5,	NVM_REQUIRED),
	("elems", NVM_STRUCT_ARRAY(nvm_complex_elem_t),	elems,	NVM_REQUIRED),
	("flag", NVM_FLAG,				flag),
);

static MunitResult
test_nvm_complex(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	/* the "top" input nvlist for nvm_complex_t */
	nvlist_t *top = fnvlist_alloc();

	fnvlist_add_uint64(top, "u64", unit_rand_uint64());

	uint64_t arr[10];
	for (uint_t i = 0; i < ARRAY_SIZE(arr); i++)
		arr[i] = unit_rand_uint64();
	fnvlist_add_uint64_array(top, "arr", arr, ARRAY_SIZE(arr));

	int8_t arr5[5];
	for (uint_t i = 0; i < ARRAY_SIZE(arr5); i++)
		arr5[i] = munit_rand_int_range(INT8_MIN, INT8_MAX);
	fnvlist_add_int8_array(top, "arr5", arr5, ARRAY_SIZE(arr5));

	if (unit_rand_bool())
		fnvlist_add_boolean(top, "flag");

	/* the "inner" input nvlist for nvm_complex_inner_t */
	nvlist_t *inner = fnvlist_alloc();
	if (unit_rand_bool())
		fnvlist_add_int32(inner, "i32",
		    munit_rand_int_range(INT32_MIN, INT32_MAX));

	/*
	 * create an array of 20 input nvlists for nvm_complex_elem_t, then
	 * choose a random pivot. the left will go to the inner->elems array,
	 * the right to top elems array. the pivot could be at start or end of
	 * the array, in which case one of the arrays will be empty.
	 */
	nvlist_t *elems[20];
	uint_t pivot = munit_rand_int_range(0, ARRAY_SIZE(elems));
	for (uint_t i = 0; i < ARRAY_SIZE(elems); i++) {
		elems[i] = fnvlist_alloc();
		if (unit_rand_bool())
			fnvlist_add_boolean(elems[i], "flag");
		if (unit_rand_bool()) {
			char str[32];
			unit_rand_str(str, sizeof (str));
			fnvlist_add_string(elems[i], "str", str);
		}
	}

	if (pivot == 0) {
		/* empty to inner, everything to top */
		fnvlist_add_nvlist_array(inner, "elems", NULL, 0);
		fnvlist_add_nvlist_array(top, "elems",
		    (const nvlist_t * const *)elems, ARRAY_SIZE(elems));
	} else if (pivot == ARRAY_SIZE(elems)) {
		/* everything to inner, empty to top */
		fnvlist_add_nvlist_array(inner, "elems",
		    (const nvlist_t * const *)elems, ARRAY_SIZE(elems));
		fnvlist_add_nvlist_array(top, "elems", NULL, 0);
	} else {
		fnvlist_add_nvlist_array(inner, "elems",
		    (const nvlist_t * const *)elems, pivot);
		fnvlist_add_nvlist_array(top, "elems",
		    (const nvlist_t * const *)&elems[pivot],
		    ARRAY_SIZE(elems)-pivot);
	}

	/* final assembly */
	fnvlist_add_nvlist(top, "inner", inner);

	/* go! */
	nvm_complex_t a;
	unit_ok(NVM_UNMARSHAL(top, nvm_complex_t, &a));

	/* compare top basics */
	unit_eq(a.u64, fnvlist_lookup_uint64(top, "u64"));
	unit_eq(a.flag, nvlist_exists(top, "flag"));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < ARRAY_SIZE(arr); i++)
		unit_eq(a.arr[i], arr[i]);

	unit_eq(ARRAY_SIZE(a.arr5), ARRAY_SIZE(arr5));
	for (uint_t i = 0; i < ARRAY_SIZE(arr5); i++)
		unit_eq(a.arr5[i], arr5[i]);

	/* inner optional int32 */
	unit_eq(a.inner.has_i32, nvlist_exists(inner, "i32"));
	if (a.inner.has_i32)
		unit_eq(a.inner.i32, fnvlist_lookup_int32(inner, "i32"));

	/* check elems array sizes match pivot */
	unit_eq(a.inner.nelem_elems, pivot);
	unit_eq(a.nelem_elems, ARRAY_SIZE(elems)-pivot);

	for (uint_t i = 0; i < ARRAY_SIZE(elems); i++) {
		nvlist_t *nvelem = elems[i];
		nvm_complex_elem_t *elem =
		    (i < pivot) ? &a.inner.elems[i] : &a.elems[i-pivot];
		unit_eq(elem->flag, nvlist_exists(nvelem, "flag"));
		unit_eq(elem->has_str, nvlist_exists(nvelem, "str"));
		if (elem->has_str)
			unit_str_eq(elem->str,
			    fnvlist_lookup_string(nvelem, "str"));
	}

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NVM_MARSHAL(out, nvm_complex_t, &a));

	unit_eq(fnvlist_lookup_uint64(out, "u64"),
	    fnvlist_lookup_uint64(top, "u64"));
	unit_eq(fnvlist_lookup_boolean(out, "flag"),
	    fnvlist_lookup_boolean(top, "flag"));

	uint_t nelem;

	uint64_t *out_arr;
	unit_ok(nvlist_lookup_uint64_array(out, "arr", &out_arr, &nelem));
	unit_eq(nelem, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < ARRAY_SIZE(arr); i++)
		unit_eq(out_arr[i], arr[i]);

	int8_t *out_arr5;
	unit_ok(nvlist_lookup_int8_array(out, "arr5", &out_arr5, &nelem));
	unit_eq(nelem, ARRAY_SIZE(arr5));
	for (uint_t i = 0; i < ARRAY_SIZE(arr5); i++)
		unit_eq(out_arr5[i], arr5[i]);

	nvlist_t *out_inner;
	unit_ok(nvlist_lookup_nvlist(out, "inner", &out_inner));

	/* inner optional int32 */
	unit_eq(nvlist_exists(out_inner, "i32"), nvlist_exists(inner, "i32"));
	if (nvlist_exists(out_inner, "i32")) {
		unit_eq(fnvlist_lookup_int32(out_inner, "i32"),
		    fnvlist_lookup_int32(inner, "i32"));
	}

	nvlist_t **out_elems, **out_inner_elems;
	uint_t nelem_elems, nelem_inner_elems;

	unit_ok(nvlist_lookup_nvlist_array(out_inner, "elems",
	    &out_inner_elems, &nelem_inner_elems));
	unit_ok(nvlist_lookup_nvlist_array(out, "elems",
	    &out_elems, &nelem_elems));

	/* check elems array sizes match pivot */
	unit_eq(nelem_inner_elems, pivot);
	unit_eq(nelem_elems, ARRAY_SIZE(elems)-pivot);

	for (uint_t i = 0; i < ARRAY_SIZE(elems); i++) {
		nvlist_t *inelem = elems[i];
		nvlist_t *outelem =
		    (i < pivot) ? out_inner_elems[i] : out_elems[i-pivot];
		unit_eq(nvlist_exists(outelem, "flag"),
		    nvlist_exists(inelem, "flag"));
		unit_eq(nvlist_exists(outelem, "str"),
		    nvlist_exists(inelem, "str"));
		if (nvlist_exists(outelem, "str"))
			unit_str_eq(fnvlist_lookup_string(outelem, "str"),
			    fnvlist_lookup_string(inelem, "str"));
	}

	for (uint_t i = 0; i < ARRAY_SIZE(elems); i++)
		fnvlist_free(elems[i]);

	fnvlist_free(inner);
	fnvlist_free(top);
	fnvlist_free(out);
	return (MUNIT_OK);
}

/* ========== */

static const MunitTest nvpair_marshal_tests[] = {
	UNIT_TEST("nvm_flag_false",	test_nvm_flag_false),
	UNIT_TEST("nvm_flag_true",	test_nvm_flag_true),

	UNIT_TEST("nvm_boolean_false",	test_nvm_boolean_false),
	UNIT_TEST("nvm_boolean_true",	test_nvm_boolean_true),

	UNIT_TEST("nvm_byte",		test_nvm_byte),
	UNIT_TEST("nvm_int8",		test_nvm_int8),
	UNIT_TEST("nvm_uint8",		test_nvm_uint8),
	UNIT_TEST("nvm_int16",		test_nvm_int16),
	UNIT_TEST("nvm_uint16",		test_nvm_uint16),
	UNIT_TEST("nvm_int32",		test_nvm_int32),
	UNIT_TEST("nvm_uint32",		test_nvm_uint32),
	UNIT_TEST("nvm_int64",		test_nvm_int64),
	UNIT_TEST("nvm_uint64",		test_nvm_uint64),
	UNIT_TEST("nvm_string",		test_nvm_string),
	UNIT_TEST("nvm_hrtime",		test_nvm_hrtime),
	UNIT_TEST("nvm_double",		test_nvm_double),
	UNIT_TEST("nvm_nvlist",		test_nvm_nvlist),

	UNIT_TEST("nvm_boolean_array",	test_nvm_boolean_array),
	UNIT_TEST("nvm_byte_array",	test_nvm_byte_array),
	UNIT_TEST("nvm_int8_array",	test_nvm_int8_array),
	UNIT_TEST("nvm_uint8_array",	test_nvm_uint8_array),
	UNIT_TEST("nvm_int16_array",	test_nvm_int16_array),
	UNIT_TEST("nvm_uint16_array",	test_nvm_uint16_array),
	UNIT_TEST("nvm_int32_array",	test_nvm_int32_array),
	UNIT_TEST("nvm_uint32_array",	test_nvm_uint32_array),
	UNIT_TEST("nvm_int64_array",	test_nvm_int64_array),
	UNIT_TEST("nvm_uint64_array",	test_nvm_uint64_array),
	UNIT_TEST("nvm_string_array",	test_nvm_string_array),
	UNIT_TEST("nvm_nvlist_array",	test_nvm_nvlist_array),

	UNIT_TEST("nvm_struct",		test_nvm_struct),
	UNIT_TEST("nvm_struct_array",	test_nvm_struct_array),

	UNIT_TEST("nvm_map",		test_nvm_map),
	UNIT_TEST("nvm_set",		test_nvm_set),

	UNIT_TEST("nvm_type_mismatch_scalar",	test_nvm_type_mismatch_scalar),
	UNIT_TEST("nvm_type_mismatch_array",	test_nvm_type_mismatch_array),

	UNIT_TEST("nvm_opt_scalar_false",	test_nvm_opt_scalar_false),
	UNIT_TEST("nvm_opt_scalar_true",	test_nvm_opt_scalar_true),
	UNIT_TEST("nvm_opt_array_false",	test_nvm_opt_array_false),
	UNIT_TEST("nvm_opt_array_true",		test_nvm_opt_array_true),
	UNIT_TEST("nvm_opt_struct_false",	test_nvm_opt_struct_false),
	UNIT_TEST("nvm_opt_struct_true",	test_nvm_opt_struct_true),
	UNIT_TEST("nvm_opt_struct_array_false",	test_nvm_opt_struct_array_false),
	UNIT_TEST("nvm_opt_struct_array_true",	test_nvm_opt_struct_array_true),
	UNIT_TEST("nvm_opt_map_false",		test_nvm_opt_map_false),
	UNIT_TEST("nvm_opt_map_true",		test_nvm_opt_map_true),
	UNIT_TEST("nvm_opt_set_false",		test_nvm_opt_set_false),
	UNIT_TEST("nvm_opt_set_true",		test_nvm_opt_set_true),

	UNIT_TEST("nvm_req_missing",	test_nvm_req_missing),

	UNIT_TEST("nvm_opt_zero",	test_nvm_opt_zero),
	UNIT_TEST("nvm_opt_default",	test_nvm_opt_default),

	UNIT_TEST("nvm_partial_default",	test_nvm_partial_default),

	UNIT_TEST("nvm_nv_null_req",	test_nvm_nv_null_req),
	UNIT_TEST("nvm_nv_null_opt",	test_nvm_nv_null_opt),

	UNIT_TEST("nvm_scalar_array_n",		test_nvm_scalar_array_n),
	UNIT_TEST("nvm_scalar_array_n_erange",	test_nvm_scalar_array_n_erange),
	UNIT_TEST("nvm_scalar_array_n_opt",	test_nvm_scalar_array_n_opt),
	UNIT_TEST("nvm_scalar_array_n_str",	test_nvm_scalar_array_n_str),

	UNIT_TEST("nvm_extra_keys",	test_nvm_extra_keys),
	UNIT_TEST("nvm_spill",		test_nvm_spill),
	UNIT_TEST("nvm_spill_type",		test_nvm_spill_type),
	UNIT_TEST("nvm_spill_none",		test_nvm_spill_none),
	UNIT_TEST("nvm_spill_flags",		test_nvm_spill_flags),

	UNIT_TEST("nvm_complex",	test_nvm_complex),

	/* XXX deep struct/struct array schemas need more testing around has_ */

	{ 0 },
};

static const MunitSuite nvpair_marshal_test_suite = {
	"nvpair_marshal.",
	nvpair_marshal_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&nvpair_marshal_test_suite, NULL, argc, argv));
}
