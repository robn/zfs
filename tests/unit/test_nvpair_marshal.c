#include <sys/kmem.h>
#include <sys/nvpair.h>
#include <sys/nvpair_marshal.h>
#include <libnvpair.h>

#include "unit.h"

/* ========== */

/* Round-trip unmarshal/marhsal tests for scalar types. */

NV_MARSHAL_TYPE(nvm_boolean_t,
	("b",  NV_TYPE(BOOLEAN),  b,  NV_REQUIRED)
);

static MunitResult
test_nvm_boolean(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_boolean_value(in, "b", B_TRUE);

	nvm_boolean_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_boolean_t, &a));

	unit_eq(a.b, B_TRUE);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_boolean_t, &a));

	unit_eq(fnvlist_lookup_boolean_value(out, "b"), B_TRUE);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_byte_t,
	("byte",  NV_TYPE(BYTE),  byte,  NV_REQUIRED)
);

static MunitResult
test_nvm_byte(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_byte(in, "byte", 0x42);

	nvm_byte_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_byte_t, &a));

	unit_eq(a.byte, 0x42);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_byte_t, &a));

	unit_eq(fnvlist_lookup_byte(out, "byte"), 0x42);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_int8_t,
	("i8",  NV_TYPE(INT8),  i8,  NV_REQUIRED)
);

static MunitResult
test_nvm_int8(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_int8(in, "i8", -12);

	nvm_int8_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_int8_t, &a));

	unit_eq(a.i8, -12);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_int8_t, &a));

	unit_eq(fnvlist_lookup_int8(out, "i8"), -12);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_uint8_t,
	("u8",  NV_TYPE(UINT8),  u8,  NV_REQUIRED)
);

static MunitResult
test_nvm_uint8(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_uint8(in, "u8", 200);

	nvm_uint8_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_uint8_t, &a));

	unit_eq(a.u8, 200);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_uint8_t, &a));

	unit_eq(fnvlist_lookup_uint8(out, "u8"), 200);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_int16_t,
	("i16",		NV_TYPE(INT16),   i16,    NV_REQUIRED)
);

static MunitResult
test_nvm_int16(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_int16(in, "i16", -1234);

	nvm_int16_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_int16_t, &a));

	unit_eq(a.i16, -1234);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_int16_t, &a));

	unit_eq(fnvlist_lookup_int16(out, "i16"), -1234);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_uint16_t,
	("u16",  NV_TYPE(UINT16),  u16,  NV_REQUIRED)
);

static MunitResult
test_nvm_uint16(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_uint16(in, "u16", 54321);

	nvm_uint16_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_uint16_t, &a));

	unit_eq(a.u16, 54321);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_uint16_t, &a));

	unit_eq(fnvlist_lookup_uint16(out, "u16"), 54321);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_int32_t,
	("i32",  NV_TYPE(INT32),  i32,  NV_REQUIRED)
);

static MunitResult
test_nvm_int32(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_int32(in, "i32", -123456);

	nvm_int32_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_int32_t, &a));

	unit_eq(a.i32, -123456);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_int32_t, &a));

	unit_eq(fnvlist_lookup_int32(out, "i32"), -123456);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_uint32_t,
	("u32",  NV_TYPE(UINT32),  u32,  NV_REQUIRED)
);

static MunitResult
test_nvm_uint32(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_uint32(in, "u32", 3000000000);

	nvm_uint32_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_uint32_t, &a));

	unit_eq(a.u32, 3000000000);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_uint32_t, &a));

	unit_eq(fnvlist_lookup_uint32(out, "u32"), 3000000000);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_int64_t,
	("i64",  NV_TYPE(INT64),  i64,  NV_REQUIRED)
);

static MunitResult
test_nvm_int64(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_int64(in, "i64", -9876546410LL);

	nvm_int64_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_int64_t, &a));

	unit_eq(a.i64, -9876546410LL);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_int64_t, &a));

	unit_eq(fnvlist_lookup_int64(out, "i64"), -9876546410LL);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_uint64_t,
	("u64",  NV_TYPE(UINT64),  u64,  NV_REQUIRED)
);

static MunitResult
test_nvm_uint64(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_uint64(in, "u64", 12345678901234567890ULL);

	nvm_uint64_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_uint64_t, &a));

	unit_eq(a.u64, 12345678901234567890ULL);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_uint64_t, &a));

	unit_eq(fnvlist_lookup_uint64(out, "u64"), 12345678901234567890ULL);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_string_t,
	("str",  NV_TYPE(STRING),  str,  NV_REQUIRED)
);

static MunitResult
test_nvm_string(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	fnvlist_add_string(in, "str", "hello");

	nvm_string_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_string_t, &a));

	unit_str_eq(a.str, "hello");

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_string_t, &a));

	unit_str_eq(fnvlist_lookup_string(out, "str"), "hello");

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_hrtime_t,
	("ts",  NV_TYPE(HRTIME),  ts,  NV_REQUIRED)
);

static MunitResult
test_nvm_hrtime(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	unit_ok(nvlist_add_hrtime(in, "ts", SEC2NSEC(10)));

	nvm_hrtime_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_hrtime_t, &a));

	unit_eq(a.ts, SEC2NSEC(10));

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_hrtime_t, &a));

	hrtime_t ts;
	unit_ok(nvlist_lookup_hrtime(out, "ts", &ts));
	unit_eq(ts, SEC2NSEC(10));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_double_t,
	("d",  NV_TYPE(DOUBLE),  d,  NV_REQUIRED)
);

static MunitResult
test_nvm_double(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();
	unit_ok(nvlist_add_double(in, "d", 3.5));

	nvm_double_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_double_t, &a));

	unit_eq(a.d, 3.5);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_double_t, &a));

	double d;
	unit_ok(nvlist_lookup_double(out, "d", &d));
	unit_eq(d, 3.5);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_nvlist_t,
	("nv",  NV_TYPE(NVLIST),  nv,  NV_REQUIRED)
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
	unit_ok(NV_UNMARSHAL(in, nvm_nvlist_t, &a));

	unit_eq(fnvlist_lookup_uint64(a.nv, "key"), 1);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_nvlist_t, &a));

	unit_ok(nvlist_lookup_nvlist(out, "nv", &nv));
	unit_eq(fnvlist_lookup_uint64(nv, "key"), 1);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

/* ========== */

NV_MARSHAL_TYPE(nvm_boolean_array_t,
	("arr",  NV_ARRAY(BOOLEAN),  arr,  NV_REQUIRED)
);

static MunitResult
test_nvm_boolean_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	const boolean_t arr[] = { B_TRUE, B_FALSE, B_TRUE };
	unit_ok(nvlist_add_boolean_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_boolean_array_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_boolean_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_boolean_array_t, &a));

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
NV_MARSHAL_TYPE(nvm_byte_array_t,
	("arr",  NV_ARRAY(BYTE),  arr,  NV_REQUIRED)
);

static MunitResult
test_nvm_byte_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	uchar_t arr[] = { 10, 20, 30 };
	unit_ok(nvlist_add_byte_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_byte_array_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_byte_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_byte_array_t, &a));

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

NV_MARSHAL_TYPE(nvm_int8_array_t,
	("arr",  NV_ARRAY(INT8),  arr,  NV_REQUIRED)
);

static MunitResult
test_nvm_int8_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	int8_t arr[] = { -1, 0, 1 };
	unit_ok(nvlist_add_int8_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_int8_array_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_int8_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_int8_array_t, &a));

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

NV_MARSHAL_TYPE(nvm_uint8_array_t,
	("arr",  NV_ARRAY(UINT8),  arr,  NV_REQUIRED)
);

static MunitResult
test_nvm_uint8_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	uint8_t arr[] = { 10, 20, 30 };
	unit_ok(nvlist_add_uint8_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_uint8_array_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_uint8_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_uint8_array_t, &a));

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

NV_MARSHAL_TYPE(nvm_int16_array_t,
	("arr",  NV_ARRAY(INT16),  arr,  NV_REQUIRED)
);

static MunitResult
test_nvm_int16_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	int16_t arr[] = { -100, 0, 100 };
	unit_ok(nvlist_add_int16_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_int16_array_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_int16_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_int16_array_t, &a));

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

NV_MARSHAL_TYPE(nvm_uint16_array_t,
	("arr",  NV_ARRAY(UINT16),  arr,  NV_REQUIRED)
);

static MunitResult
test_nvm_uint16_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	uint16_t arr[] = { 1000, 2000, 3000 };
	unit_ok(nvlist_add_uint16_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_uint16_array_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_uint16_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_uint16_array_t, &a));

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

NV_MARSHAL_TYPE(nvm_int32_array_t,
	("arr",  NV_ARRAY(INT32),  arr,  NV_REQUIRED)
);

static MunitResult
test_nvm_int32_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	int32_t arr[] = { -100000, 0, 100000 };
	unit_ok(nvlist_add_int32_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_int32_array_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_int32_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_int32_array_t, &a));

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

NV_MARSHAL_TYPE(nvm_uint32_array_t,
	("arr",  NV_ARRAY(UINT32),  arr,  NV_REQUIRED)
);

static MunitResult
test_nvm_uint32_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	uint32_t arr[] = { 100000, 200000, 300000 };
	unit_ok(nvlist_add_uint32_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_uint32_array_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_uint32_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_uint32_array_t, &a));

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

NV_MARSHAL_TYPE(nvm_int64_array_t,
	("arr",  NV_ARRAY(INT64),  arr,  NV_REQUIRED)
);

static MunitResult
test_nvm_int64_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	int64_t arr[] = { -1000000000LL, 0, 1000000000LL };
	unit_ok(nvlist_add_int64_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_int64_array_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_int64_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_int64_array_t, &a));

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

NV_MARSHAL_TYPE(nvm_uint64_array_t,
	("arr",  NV_ARRAY(UINT64),  arr,  NV_REQUIRED)
);

static MunitResult
test_nvm_uint64_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	uint64_t arr[] = { 1, 2, 3, 4, 5 };
	unit_ok(nvlist_add_uint64_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_uint64_array_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_uint64_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_uint64_array_t, &a));

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

NV_MARSHAL_TYPE(nvm_string_array_t,
	("arr",  NV_ARRAY(STRING),  arr,  NV_REQUIRED)
);

static MunitResult
test_nvm_string_array(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	const char *arr[] = { "alpha", "beta", "gamma" };
	unit_ok(nvlist_add_string_array(in, "arr", arr, ARRAY_SIZE(arr)));

	nvm_string_array_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_string_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	for (uint_t i = 0; i < a.nelem_arr; i++)
		unit_str_eq(a.arr[i], arr[i]);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_string_array_t, &a));

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

NV_MARSHAL_TYPE(nvm_nvlist_array_t,
	("arr",  NV_ARRAY(NVLIST),  arr,  NV_REQUIRED)
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
	unit_ok(NV_UNMARSHAL(in, nvm_nvlist_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	unit_eq(fnvlist_lookup_uint64(a.arr[0], "key"), 0);
	unit_eq(fnvlist_lookup_uint64(a.arr[1], "key"), 1);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_nvlist_array_t, &a));

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

NV_MARSHAL_TYPE(nvm_struct_inner_t,
    ("key",  NV_TYPE(UINT64),  key,  NV_REQUIRED)
);
NV_MARSHAL_TYPE(nvm_struct_t,
    ("nv",  NV_STRUCT(nvm_struct_inner_t),  nv,  NV_REQUIRED)
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
	unit_ok(NV_UNMARSHAL(in, nvm_struct_t, &a));

	unit_eq(a.nv.key, 1);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_struct_t, &a));

	unit_ok(nvlist_lookup_nvlist(out, "nv", &nv));
	unit_eq(fnvlist_lookup_uint64(nv, "key"), 1);

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_struct_array_inner_t,
    ("key",  NV_TYPE(UINT64),  key,  NV_REQUIRED)
);
NV_MARSHAL_TYPE(nvm_struct_array_t,
    ("arr",  NV_STRUCT_ARRAY(nvm_struct_array_inner_t),  arr,  NV_REQUIRED)
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
	unit_ok(NV_UNMARSHAL(in, nvm_struct_array_t, &a));

	unit_eq(a.nelem_arr, ARRAY_SIZE(arr));
	unit_eq(a.arr[0].key, 0);
	unit_eq(a.arr[1].key, 1);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_struct_array_t, &a));

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

NV_MARSHAL_TYPE(nvm_opt_scalar_t,
	("val",  NV_TYPE(UINT64),  val,  NV_OPTIONAL)
);

static MunitResult
test_nvm_opt_scalar_false(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_opt_scalar_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_opt_scalar_t, &a));

	unit_false(a.has_val);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_opt_scalar_t, &a));

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
	unit_ok(NV_UNMARSHAL(in, nvm_opt_scalar_t, &a));

	unit_true(a.has_val);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_opt_scalar_t, &a));

	unit_true(nvlist_exists(out, "val"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_opt_array_t,
	("arr",  NV_ARRAY(UINT64),  arr,  NV_OPTIONAL)
);

static MunitResult
test_nvm_opt_array_false(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_opt_array_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_opt_array_t, &a));

	unit_false(a.has_arr);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_opt_array_t, &a));

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
	unit_ok(NV_UNMARSHAL(in, nvm_opt_array_t, &a));

	unit_true(a.has_arr);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_opt_array_t, &a));

	unit_true(nvlist_exists(out, "arr"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_opt_struct_inner_t,
	("key",  NV_TYPE(UINT64),  key,  NV_REQUIRED)
);
NV_MARSHAL_TYPE(nvm_opt_struct_t,
	("nv",  NV_STRUCT(nvm_opt_struct_inner_t),  nv,  NV_OPTIONAL)
);

static MunitResult
test_nvm_opt_struct_false(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_opt_struct_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_opt_struct_t, &a));

	unit_false(a.has_nv);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_opt_struct_t, &a));

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
	unit_ok(NV_UNMARSHAL(in, nvm_opt_struct_t, &a));

	unit_true(a.has_nv);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_opt_struct_t, &a));

	unit_true(nvlist_exists(out, "nv"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

NV_MARSHAL_TYPE(nvm_opt_struct_array_inner_t,
	("key",  NV_TYPE(UINT64),  key,  NV_REQUIRED)
);
NV_MARSHAL_TYPE(nvm_opt_struct_array_t,
	("arr",  NV_STRUCT_ARRAY(nvm_opt_struct_inner_t),  arr,  NV_OPTIONAL)
);

static MunitResult
test_nvm_opt_struct_array_false(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_opt_struct_array_t a;
	unit_ok(NV_UNMARSHAL(in, nvm_opt_struct_array_t, &a));

	unit_false(a.has_arr);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_opt_struct_array_t, &a));

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
	unit_ok(NV_UNMARSHAL(in, nvm_opt_struct_array_t, &a));

	unit_true(a.has_arr);

	nvlist_t *out = fnvlist_alloc();
	unit_ok(NV_MARSHAL(out, nvm_opt_struct_array_t, &a));

	unit_true(nvlist_exists(out, "arr"));

	fnvlist_free(in);
	fnvlist_free(out);
	return (MUNIT_OK);
}

/* ========== */

NV_MARSHAL_TYPE(nvm_req_default_t,
	("val",  NV_TYPE(UINT64),  val)
);

static MunitResult
test_nvm_req_default(const MunitParameter params[], void *data)
{
	(void) params; (void) data;

	nvlist_t *in = fnvlist_alloc();

	nvm_req_default_t a;
	unit_err(NV_UNMARSHAL(in, nvm_req_default_t, &a), ENOENT);

	fnvlist_add_uint64(in, "val", 1);

	unit_ok(NV_UNMARSHAL(in, nvm_req_default_t, &a));

	fnvlist_free(in);
	return (MUNIT_OK);
}

/* ========== */

static const MunitTest nvpair_marshal_tests[] = {
	UNIT_TEST("nvm_boolean",	test_nvm_boolean),
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

	UNIT_TEST("nvm_opt_scalar_false",	test_nvm_opt_scalar_false),
	UNIT_TEST("nvm_opt_scalar_true",	test_nvm_opt_scalar_true),
	UNIT_TEST("nvm_opt_array_false",	test_nvm_opt_array_false),
	UNIT_TEST("nvm_opt_array_true",		test_nvm_opt_array_true),
	UNIT_TEST("nvm_opt_struct_false",	test_nvm_opt_struct_false),
	UNIT_TEST("nvm_opt_struct_true",	test_nvm_opt_struct_true),
	UNIT_TEST("nvm_opt_struct_array_false",	test_nvm_opt_struct_array_false),
	UNIT_TEST("nvm_opt_struct_array_true",	test_nvm_opt_struct_array_true),

	UNIT_TEST("nvm_req_default",	test_nvm_req_default),

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
