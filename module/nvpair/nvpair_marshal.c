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

#include <sys/nvpair_marshal.h>
#ifndef _KERNEL
#include <stddef.h>	/* for offsetof, typeof */
#endif

#define	_NVM_FIELD(f, base)		((void *)((base) + ((f)->nvmf_offset)))
#define	_NVM_FIELD_HAS(f, base)		((boolean_t *)((base) + ((f)->nvmf_has_offset)))
#define	_NVM_FIELD_NELEM(f, base)	((uint_t *)((base) + ((f)->nvmf_nelem_offset)))

#define	_NVM_FIELD_BASE(f, base)	((uintptr_t)_NVM_FIELD((f), (base)))

#define	_NVM_FIELD_ELEM(f, base, elem)	((void *)(((uintptr_t)*(void **)_NVM_FIELD((f), (base))) + ((elem) * (f)->nvmf_elem_size)))

static const data_type_t nvm_nvtype[] = {
	[NVMK_FLAG]		= DATA_TYPE_BOOLEAN,

	[NVMK_BOOLEAN]		= DATA_TYPE_BOOLEAN_VALUE,
	[NVMK_BYTE]		= DATA_TYPE_BYTE,
	[NVMK_INT8]		= DATA_TYPE_INT8,
	[NVMK_UINT8]		= DATA_TYPE_UINT8,
	[NVMK_INT16]		= DATA_TYPE_INT16,
	[NVMK_UINT16]		= DATA_TYPE_UINT16,
	[NVMK_INT32]		= DATA_TYPE_INT32,
	[NVMK_UINT32]		= DATA_TYPE_UINT32,
	[NVMK_INT64]		= DATA_TYPE_INT64,
	[NVMK_UINT64]		= DATA_TYPE_UINT64,
	[NVMK_STRING]		= DATA_TYPE_STRING,
	[NVMK_HRTIME]		= DATA_TYPE_HRTIME,
#ifndef _KERNEL
	[NVMK_DOUBLE]		= DATA_TYPE_DOUBLE,
#endif
	[NVMK_NVLIST]		= DATA_TYPE_NVLIST,

	[NVMK_BOOLEAN_ARRAY]	= DATA_TYPE_BOOLEAN_ARRAY,
	[NVMK_BYTE_ARRAY]	= DATA_TYPE_BYTE_ARRAY,
	[NVMK_INT8_ARRAY]	= DATA_TYPE_INT8_ARRAY,
	[NVMK_UINT8_ARRAY]	= DATA_TYPE_UINT8_ARRAY,
	[NVMK_INT16_ARRAY]	= DATA_TYPE_INT16_ARRAY,
	[NVMK_UINT16_ARRAY]	= DATA_TYPE_UINT16_ARRAY,
	[NVMK_INT32_ARRAY]	= DATA_TYPE_INT32_ARRAY,
	[NVMK_UINT32_ARRAY]	= DATA_TYPE_UINT32_ARRAY,
	[NVMK_INT64_ARRAY]	= DATA_TYPE_INT64_ARRAY,
	[NVMK_UINT64_ARRAY]	= DATA_TYPE_UINT64_ARRAY,
	[NVMK_STRING_ARRAY]	= DATA_TYPE_STRING_ARRAY,
	[NVMK_NVLIST_ARRAY]	= DATA_TYPE_NVLIST_ARRAY,
};

typedef union {
	boolean_t	b;
	uchar_t		byte;
	int8_t		i8;
	uint8_t		u8;
	int16_t		i16;
	uint16_t	u16;
	int32_t		i32;
	uint32_t	u32;
	int64_t		i64;
	uint64_t	u64;
	const char	*str;
	hrtime_t	hrtime;
#ifndef _KERNEL
	double		d;
#endif
	nvlist_t	*nvl;
} nvm_scalar_u;

typedef union {
	boolean_t	*b;
	uchar_t		*byte;
	int8_t		*i8;
	uint8_t		*u8;
	int16_t		*i16;
	uint16_t	*u16;
	int32_t		*i32;
	uint32_t	*u32;
	int64_t		*i64;
	uint64_t	*u64;
	const char	**str;
	nvlist_t	**nvl;
} nvm_array_u;

typedef struct {
	const char *name;
	void *value;
} nvm_pair_t;

/* Sanity check the nvm_pair_*_t types against the generuc nvm_pair_t. */
#define _NVM_CHECK_PTYPE(K) \
	_Static_assert( \
	    sizeof(_NVM_PTYPE_##K) == sizeof(nvm_pair_t) && \
	    offsetof(_NVM_PTYPE_##K, name) == offsetof(nvm_pair_t, name) && \
	    offsetof(_NVM_PTYPE_##K, value) == offsetof(nvm_pair_t, value), \
	    "Pair struct layout mismatch for " #K)

_NVM_CHECK_PTYPE(BOOLEAN);
_NVM_CHECK_PTYPE(BYTE);
_NVM_CHECK_PTYPE(INT8);
_NVM_CHECK_PTYPE(UINT8);
_NVM_CHECK_PTYPE(INT16);
_NVM_CHECK_PTYPE(UINT16);
_NVM_CHECK_PTYPE(INT32);
_NVM_CHECK_PTYPE(UINT32);
_NVM_CHECK_PTYPE(INT64);
_NVM_CHECK_PTYPE(UINT64);
_NVM_CHECK_PTYPE(STRING);
_NVM_CHECK_PTYPE(HRTIME);
#ifndef _KERNEL
_NVM_CHECK_PTYPE(DOUBLE);
#endif
_NVM_CHECK_PTYPE(NVLIST);

static int
nvm_marshal_pair(nvlist_t *nv, nvm_kind_t k, const char *name,
    const void *v, uint_t nelem)
{
	int err = 0;

	const nvm_scalar_u *su = v;
	const nvm_array_u *au = v;

	/* Add the data to nvpair according to the field type. */
	switch (k) {
	case NVMK_FLAG:
		if (su->b)
			err = nvlist_add_boolean(nv, name);
		break;

	/* Scalar types just call the matching nvlist_add_ function. */
	case NVMK_BOOLEAN:
		err = nvlist_add_boolean_value(nv, name, su->b);
		break;
	case NVMK_BYTE:
		err = nvlist_add_byte(nv, name, su->byte);
		break;
	case NVMK_INT8:
		err = nvlist_add_int8(nv, name, su->i8);
		break;
	case NVMK_UINT8:
		err = nvlist_add_uint8(nv, name, su->u8);
		break;
	case NVMK_INT16:
		err = nvlist_add_int16(nv, name, su->i16);
		break;
	case NVMK_UINT16:
		err = nvlist_add_uint16(nv, name, su->u16);
		break;
	case NVMK_INT32:
		err = nvlist_add_int32(nv, name, su->i32);
		break;
	case NVMK_UINT32:
		err = nvlist_add_uint32(nv, name, su->u32);
		break;
	case NVMK_INT64:
		err = nvlist_add_int64(nv, name, su->i64);
		break;
	case NVMK_UINT64:
		err = nvlist_add_uint64(nv, name, su->u64);
		break;
	case NVMK_STRING:
		err = nvlist_add_string(nv, name, su->str);
		break;
	case NVMK_HRTIME:
		err = nvlist_add_hrtime(nv, name, su->hrtime);
		break;
#ifndef _KERNEL
	case NVMK_DOUBLE:
		err = nvlist_add_double(nv, name, su->d);
		break;
#endif
	case NVMK_NVLIST:
		err = nvlist_add_nvlist(nv, name, su->nvl);
		break;

	/*
	 * Array types just call the matching nvlist_add_*_array function. This
	 * works for ARRAY and ARRAY_N, as we set up the pointers above.
	 */

	case NVMK_BOOLEAN_ARRAY:
		err = nvlist_add_boolean_array(nv, name, au->b, nelem);
		break;
	case NVMK_BYTE_ARRAY:
		err = nvlist_add_byte_array(nv, name, au->byte, nelem);
		break;
	case NVMK_INT8_ARRAY:
		err = nvlist_add_int8_array(nv, name, au->i8, nelem);
		break;
	case NVMK_UINT8_ARRAY:
		err = nvlist_add_uint8_array(nv, name, au->u8, nelem);
		break;
	case NVMK_INT16_ARRAY:
		err = nvlist_add_int16_array(nv, name, au->i16, nelem);
		break;
	case NVMK_UINT16_ARRAY:
		err = nvlist_add_uint16_array(nv, name, au->u16, nelem);
		break;
	case NVMK_INT32_ARRAY:
		err = nvlist_add_int32_array(nv, name, au->i32, nelem);
		break;
	case NVMK_UINT32_ARRAY:
		err = nvlist_add_uint32_array(nv, name, au->u32, nelem);
		break;
	case NVMK_INT64_ARRAY:
		err = nvlist_add_int64_array(nv, name, au->i64, nelem);
		break;
	case NVMK_UINT64_ARRAY:
		err = nvlist_add_uint64_array(nv, name, au->u64, nelem);
		break;
	case NVMK_STRING_ARRAY:
		err = nvlist_add_string_array(nv, name,
		    (const char * const *)au->str, nelem);
		break;
	case NVMK_NVLIST_ARRAY:
		err = nvlist_add_nvlist_array(nv, name,
		    (const nvlist_t * const *)au->nvl, nelem);
		break;
	}

	return (err);
}

static int
nvm_marshal_direct(nvlist_t *nv, const nvm_field_t *f, uintptr_t base)
{
	return (nvm_marshal_pair(nv, f->nvmf_kind, f->nvmf_name,
	    _NVM_FIELD(f, base), *_NVM_FIELD_NELEM(f, base)));
}

static int
nvm_marshal_fixed_array(nvlist_t *nv, const nvm_field_t *f, uintptr_t base)
{
	void *arr = _NVM_FIELD(f, base);
	return (nvm_marshal_pair(nv, f->nvmf_kind, f->nvmf_name,
	    &arr, f->nvmf_nelem_offset));
}

static int
nvm_marshal_map(nvlist_t *nv, const nvm_field_t *f, uintptr_t base)
{
	int err = 0;

	nvlist_t *map;
	if (f->nvmf_name == NULL)
		map = nv;
	else {
		err = nvlist_alloc(&map, NV_UNIQUE_NAME, 0);
		if (err != 0)
			return (err);
	}

	uint_t nelem = *_NVM_FIELD_NELEM(f, base);

	for (uint_t elem = 0; err == 0 && elem < nelem; elem++) {
		nvm_pair_t *pair = _NVM_FIELD_ELEM(f, base, elem);
		if (nvlist_exists(map, pair->name))
			err = EEXIST;
		else if (f->nvmf_elem_kind == NVMK_FLAG)
			err = nvlist_add_boolean(map, pair->name);
		else
			err = nvm_marshal_pair(map, f->nvmf_elem_kind,
			    pair->name, &pair->value, 0);
	}

	if (err == 0 && map != nv) {
		err = nvlist_add_nvlist(nv, f->nvmf_name, map);
		nvlist_free(map);
	}

	return (err);
}

static int
nvm_marshal_struct(nvlist_t *nv, const nvm_field_t *f, uintptr_t base)
{
	nvlist_t *sub = NULL;
	int err = nvlist_alloc(&sub, NV_UNIQUE_NAME, 0);
	if (err == 0)
		err = nvm_marshal(sub, f->nvmf_sub, _NVM_FIELD(f, base));
	if (err == 0)
		err = nvlist_add_nvlist(nv, f->nvmf_name, sub);
	nvlist_free(sub);
	return (err);
}

static int
nvm_marshal_struct_array(nvlist_t *nv, const nvm_field_t *f, uintptr_t base)
{
	/*
	 * Array of struct types are the same, but we have to allocate the
	 * array, create a new nvlist for each element and marshal into it, add
	 * it to the parent nvlist, then free it all. If any fail, we have to
	 * tear it all down.
	 */
	uint_t nelem = *_NVM_FIELD_NELEM(f, base);
	if (nelem == 0)
		return (0);

	nvlist_t **arr = kmem_zalloc(nelem * sizeof (nvlist_t *), KM_SLEEP);

	int err = 0;
	for (uint_t j = 0; err == 0 && j < nelem; j++) {
		nvlist_t *sub = NULL;
		err = nvlist_alloc(&sub, NV_UNIQUE_NAME, 0);
		if (err == 0) {
			err = nvm_marshal(sub, f->nvmf_sub,
			    (void *)((uintptr_t)(*(void **)_NVM_FIELD_BASE(f, base)) +
			    j * f->nvmf_elem_size));
		}
		if (err == 0)
			arr[j] = sub;
		else
			nvlist_free(sub);
	}

	if (err == 0) {
		err = nvlist_add_nvlist_array(nv, f->nvmf_name,
		    (const nvlist_t * const *)arr, nelem);
	}

	for (uint_t j = 0; j < nelem; j++)
		if (arr[j] != NULL)
			nvlist_free(arr[j]);

	kmem_free(arr, nelem * sizeof (nvlist_t *));

	return (err);
}

static int
nvm_marshal_field(nvlist_t *nv, const nvm_field_t *f, uintptr_t base)
{
	int err = 0;

	switch (f->nvmf_adapter) {
	case NVMA_DIRECT:
		err = nvm_marshal_direct(nv, f, base);
		break;
	case NVMA_FIXED_ARRAY:
		err = nvm_marshal_fixed_array(nv, f, base);
		break;
	case NVMA_MAP:
		err = nvm_marshal_map(nv, f, base);
		break;
	case NVMA_STRUCT:
		err = nvm_marshal_struct(nv, f, base);
		break;
	case NVMA_STRUCT_ARRAY:
		err = nvm_marshal_struct_array(nv, f, base);
		break;
	}

	return (err);
}

/*
 * Marshaling function. Uses the schema in `desc` to pull things out of `a`
 * and add them to `nv`.
 */
int
nvm_marshal(nvlist_t *nv, const nvm_desc_t *desc, void *a)
{
	uintptr_t base = (uintptr_t)a;
	int err = 0;

	const nvm_field_t *spill = NULL;

	for (size_t i = 0; err == 0 && i < desc->nvmd_nfields; i++) {
		const nvm_field_t *f = &desc->nvmd_fields[i];

		if (f->nvmf_name == NULL) {
			ASSERT0P(spill);
			spill = f;
			continue;
		}

		/*
		 * If the field is optional and the has_ flag is not set,
		 * then skip the field entirely.
		 */
		if ((f->nvmf_flags & NVMF_OPTIONAL) &&
		    !*_NVM_FIELD_HAS(f, base))
			continue;

		err = nvm_marshal_field(nv, f, base);
	}

	if (err == 0 && spill != NULL)
		err = nvm_marshal_map(nv, spill, base);

	return (err);
}

/* ========== */

static void
nvm_zero_scalar(nvm_kind_t k, nvm_scalar_u *u)
{
	switch (k) {
	case NVMK_FLAG:
	case NVMK_BOOLEAN:
		u->b = B_FALSE;
		break;
	case NVMK_BYTE:
		u->byte = 0;
		break;
	case NVMK_INT8:
		u->i8 = 0;
		break;
	case NVMK_UINT8:
		u->u8 = 0;
		break;
	case NVMK_INT16:
		u->i16 = 0;
		break;
	case NVMK_UINT16:
		u->u16 = 0;
		break;
	case NVMK_INT32:
		u->i32 = 0;
		break;
	case NVMK_UINT32:
		u->u32 = 0;
		break;
	case NVMK_INT64:
		u->i64 = 0;
		break;
	case NVMK_UINT64:
		u->u64 = 0;
		break;
	case NVMK_STRING:
		u->str = NULL;
		break;
	case NVMK_HRTIME:
		u->hrtime = 0;
		break;
#ifndef _KERNEL
	case NVMK_DOUBLE:
		u->d = 0.0;
		break;
#endif
	case NVMK_NVLIST:
		u->nvl = NULL;
		break;

	default:
		__builtin_unreachable();
	}
}

static void
nvm_default_scalar(nvm_kind_t k, nvm_scalar_u *u, const uintptr_t def)
{
	switch (k) {
	case NVMK_BOOLEAN:
		u->b = def;
		break;
	case NVMK_BYTE:
		u->byte = def;
		break;
	case NVMK_INT8:
		u->i8 = (intptr_t)def;
		break;
	case NVMK_UINT8:
		u->u8 = def;
		break;
	case NVMK_INT16:
		u->i16 = (intptr_t)def;
		break;
	case NVMK_UINT16:
		u->u16 = def;
		break;
	case NVMK_INT32:
		u->i32 = (intptr_t)def;
		break;
	case NVMK_UINT32:
		u->u32 = def;
		break;
	case NVMK_INT64:
		u->i64 = (intptr_t)def;
		break;
	case NVMK_UINT64:
		u->u64 = def;
		break;
	case NVMK_STRING:
		u->str = (const char *)def;
		break;
	case NVMK_HRTIME:
		u->hrtime = def;
		break;
#ifndef _KERNEL
	case NVMK_DOUBLE:
		u->d = (double)def;
		break;
#endif
	case NVMK_NVLIST:
		u->nvl = (nvlist_t *)def;
		break;

	default:
		__builtin_unreachable();
	}
}

static void
nvm_zero_array_fixed(nvm_kind_t k, void *arr, uint_t nelem)
{
	nvm_array_u *u = (nvm_array_u *)&arr;

	for (uint_t i = 0; i < nelem; i++) {
		switch (k) {
		case NVMK_BOOLEAN_ARRAY:
			nvm_zero_scalar(NVMK_BOOLEAN,
			    (nvm_scalar_u *)&u->b[i]);
			break;
		case NVMK_BYTE_ARRAY:
			nvm_zero_scalar(NVMK_BYTE,
			    (nvm_scalar_u *)&u->byte[i]);
			break;
		case NVMK_INT8_ARRAY:
			nvm_zero_scalar(NVMK_INT8,
			    (nvm_scalar_u *)&u->i8[i]);
			break;
		case NVMK_UINT8_ARRAY:
			nvm_zero_scalar(NVMK_UINT8,
			    (nvm_scalar_u *)&u->u8[i]);
			break;
		case NVMK_INT16_ARRAY:
			nvm_zero_scalar(NVMK_INT16,
			    (nvm_scalar_u *)&u->i16[i]);
			break;
		case NVMK_UINT16_ARRAY:
			nvm_zero_scalar(NVMK_UINT16,
			    (nvm_scalar_u *)&u->u16[i]);
			break;
		case NVMK_INT32_ARRAY:
			nvm_zero_scalar(NVMK_INT32,
			    (nvm_scalar_u *)&u->i32[i]);
			break;
		case NVMK_UINT32_ARRAY:
			nvm_zero_scalar(NVMK_UINT32,
			    (nvm_scalar_u *)&u->u32[i]);
			break;
		case NVMK_INT64_ARRAY:
			nvm_zero_scalar(NVMK_INT64,
			    (nvm_scalar_u *)&u->i64[i]);
			break;
		case NVMK_UINT64_ARRAY:
			nvm_zero_scalar(NVMK_UINT64,
			    (nvm_scalar_u *)&u->u64[i]);
			break;
		case NVMK_STRING_ARRAY:
			nvm_zero_scalar(NVMK_STRING,
			    (nvm_scalar_u *)&u->str[i]);
			break;
		case NVMK_NVLIST_ARRAY:
			nvm_zero_scalar(NVMK_NVLIST,
			    (nvm_scalar_u *)&u->nvl[i]);
			break;
		default:
			__builtin_unreachable();
		}
	}
}

static void nvm_reset(const nvm_desc_t *desc, uintptr_t base);

static void
nvm_reset_field(const nvm_field_t *f, uintptr_t base)
{
	if (f->nvmf_flags & NVMF_OPTIONAL)
		*_NVM_FIELD_HAS(f, base) = B_FALSE;

	switch (f->nvmf_cshape) {
	case NVMC_SCALAR:
		if (f->nvmf_flags & NVMF_DEFAULT)
			nvm_default_scalar(f->nvmf_kind, _NVM_FIELD(f, base),
			    f->nvmf_default);
		else
			nvm_zero_scalar(f->nvmf_kind, _NVM_FIELD(f, base));
		break;

	case NVMC_DYN_ARRAY:
		/* Works for NVM_ARRAY, NVM_MAP, NVM_SET, and NVM_STRUCT_ARRAY! */
		*(void **)_NVM_FIELD(f, base) = NULL;
		*_NVM_FIELD_NELEM(f, base) = 0;
		break;

	case NVMC_FIXED_ARRAY:
		nvm_zero_array_fixed(f->nvmf_kind,
		    _NVM_FIELD(f, base), f->nvmf_nelem_offset);
		break;

	case NVMC_EMBEDDED:
		nvm_reset(f->nvmf_sub, _NVM_FIELD_BASE(f, base));
		break;
	}
}

static void
nvm_reset(const nvm_desc_t *desc, uintptr_t base)
{
	for (size_t i = 0; i < desc->nvmd_nfields; i++)
		nvm_reset_field(&desc->nvmd_fields[i], base);
}

/* ========== */

static int
nvm_unmarshal_pair(nvpair_t *pair, nvm_kind_t k, void *v, uint_t *nelemp)
{
	int err = 0;

	switch (k) {
	case NVMK_FLAG:
		*(boolean_t *)v = (pair != NULL);
		break;

	case NVMK_BOOLEAN:
		err = nvpair_value_boolean_value(pair, v);
		break;
	case NVMK_BYTE:
		err = nvpair_value_byte(pair, v);
		break;
	case NVMK_INT8:
		err = nvpair_value_int8(pair, v);
		break;
	case NVMK_UINT8:
		err = nvpair_value_uint8(pair, v);
		break;
	case NVMK_INT16:
		err = nvpair_value_int16(pair, v);
		break;
	case NVMK_UINT16:
		err = nvpair_value_uint16(pair, v);
		break;
	case NVMK_INT32:
		err = nvpair_value_int32(pair, v);
		break;
	case NVMK_UINT32:
		err = nvpair_value_uint32(pair, v);
		break;
	case NVMK_INT64:
		err = nvpair_value_int64(pair, v);
		break;
	case NVMK_UINT64:
		err = nvpair_value_uint64(pair, v);
		break;
	case NVMK_STRING:
		err = nvpair_value_string(pair, v);
		break;
	case NVMK_HRTIME:
		err = nvpair_value_hrtime(pair, v);
		break;
#ifndef _KERNEL
	case NVMK_DOUBLE:
		err = nvpair_value_double(pair, v);
		break;
#endif
	case NVMK_NVLIST:
		err = nvpair_value_nvlist(pair, v);
		break;

	case NVMK_BOOLEAN_ARRAY:
		err = nvpair_value_boolean_array(pair, v, nelemp);
		break;
	case NVMK_BYTE_ARRAY:
		err = nvpair_value_byte_array(pair, v, nelemp);
		break;
	case NVMK_INT8_ARRAY:
		err = nvpair_value_int8_array(pair, v, nelemp);
		break;
	case NVMK_UINT8_ARRAY:
		err = nvpair_value_uint8_array(pair, v, nelemp);
		break;
	case NVMK_INT16_ARRAY:
		err = nvpair_value_int16_array(pair, v, nelemp);
		break;
	case NVMK_UINT16_ARRAY:
		err = nvpair_value_uint16_array(pair, v, nelemp);
		break;
	case NVMK_INT32_ARRAY:
		err = nvpair_value_int32_array(pair, v, nelemp);
		break;
	case NVMK_UINT32_ARRAY:
		err = nvpair_value_uint32_array(pair, v, nelemp);
		break;
	case NVMK_INT64_ARRAY:
		err = nvpair_value_int64_array(pair, v, nelemp);
		break;
	case NVMK_UINT64_ARRAY:
		err = nvpair_value_uint64_array(pair, v, nelemp);
		break;
	case NVMK_STRING_ARRAY:
		err = nvpair_value_string_array(pair, v, nelemp);
		break;
	case NVMK_NVLIST_ARRAY:
		err = nvpair_value_nvlist_array(pair, v, nelemp);
		break;
	}

	return (err);
}

static int
nvm_unmarshal_direct(nvpair_t *pair, const nvm_field_t *f, uintptr_t base)
{
	return (nvm_unmarshal_pair(pair, f->nvmf_kind, _NVM_FIELD(f, base),
	    _NVM_FIELD_NELEM(f, base)));
}

static int
nvm_unmarshal_fixed_array(nvpair_t *pair, const nvm_field_t *f, uintptr_t base)
{
	void *tarr;
	uint_t tnelem;

	int err = nvm_unmarshal_pair(pair, f->nvmf_kind, &tarr, &tnelem);
	if (err == 0) {
		if (tnelem != f->nvmf_nelem_offset)
			err = ERANGE;
		else
			memcpy(_NVM_FIELD(f, base), tarr,
			    f->nvmf_elem_size * f->nvmf_nelem_offset);
	}

	return (err);
}

static int
nvm_unmarshal_map_nv(nvlist_t *nvalloc, nvlist_t *map, const nvm_field_t *f,
    uintptr_t base, nvlist_t *skip)
{
	uint_t nelem = fnvlist_num_pairs(map);
	if (nelem == 0) {
		*((void **)_NVM_FIELD_BASE(f, base)) = NULL;
		*_NVM_FIELD_NELEM(f, base) = 0;
		return (0);
	}

	int err = 0;

	void *arr;
	if ((err = nvlist_alloc_aux(nvalloc,
	    f->nvmf_elem_size * nelem, (void **)&arr)) != 0)
		return (err);

	uint_t elem = 0;
	for (nvpair_t *ep = nvlist_next_nvpair(map, NULL);
	    err == 0 && ep != NULL; ep = nvlist_next_nvpair(map, ep)) {
		const char *ename = nvpair_name(ep);
		if (skip != NULL && nvlist_exists(skip, ename))
			continue;

		nvm_pair_t *nvmp = (void *)((uintptr_t)arr + (elem * f->nvmf_elem_size));
		elem++;

		nvmp->name = ename;
		if (f->nvmf_elem_kind == NVMK_FLAG)
			continue;

		err = nvm_unmarshal_pair(ep, f->nvmf_elem_kind,
		    &nvmp->value, NULL);

		if (err == EINVAL)
			/* Type mismatch. */
			err = ENOENT;
	}

	/*
	 * If we copied in less than the amount in the nvlist, the we must
	 * have skipped some (spill case).
	 */
	if (err == 0) {
		/*
		 * Note that we're setting nelem to number of elements we saw,
		 * not the number in the nvlist, as we may have skipped some.
		 */
		*((void **)_NVM_FIELD(f, base)) = arr;
		*_NVM_FIELD_NELEM(f, base) = elem;
	}

	return (err);
}

static int
nvm_unmarshal_map(nvlist_t *nvalloc, nvpair_t *pair, const nvm_field_t *f,
    uintptr_t base)
{
	nvlist_t *map;

	int err = nvpair_value_nvlist(pair, &map);
	if (err != 0)
		return (err);

	return (nvm_unmarshal_map_nv(nvalloc, map, f, base, NULL));
}

static int
nvm_unmarshal_struct(nvpair_t *pair, const nvm_field_t *f, uintptr_t base)
{
	nvlist_t *sub = NULL;
	int err = nvpair_value_nvlist(pair, &sub);
	if (err != 0)
		return (err);
	return (nvm_unmarshal(sub, f->nvmf_sub, _NVM_FIELD(f, base)));
}

static int
nvm_unmarshal_struct_array(nvlist_t *nvalloc,
    nvpair_t *pair, const nvm_field_t *f, uintptr_t base)
{
	/*
	 * Array of nvlists to convert to array of unmarshaled structs.  The
	 * structs are allocated from the source nvlist, so the lifetime of the
	 * output is bound to the nvlist.
	 */
	nvlist_t **arr;
	uint_t nelem;

	int err = nvpair_value_nvlist_array(pair, &arr, &nelem);
	if (err != 0)
		return (err);

	if (nelem == 0) {
		*((void **)_NVM_FIELD_BASE(f, base)) = NULL;
		*_NVM_FIELD_NELEM(f, base) = 0;
		return (0);
	}

	void *elems = NULL;
	err = nvlist_alloc_aux(nvalloc, f->nvmf_elem_size * nelem, &elems);
	if (err != 0)
		return (err);

	for (uint_t j = 0; err == 0 && j < nelem; j++)
		err = nvm_unmarshal(arr[j], f->nvmf_sub,
		    (void *)((uintptr_t)elems + j * f->nvmf_elem_size));

	if (err == 0) {
		*((void **)_NVM_FIELD_BASE(f, base)) = elems;
		*_NVM_FIELD_NELEM(f, base) = nelem;
	}

	return (err);
}

static int
nvm_unmarshal_field(nvlist_t *nv, const nvm_field_t *f, uintptr_t base)
{
	nvpair_t *pair = NULL;

	if (nv != NULL) {
		/* Look up the named pair. */
		int err = nvlist_lookup_nvpair(nv, f->nvmf_name, &pair);

		/*
		 * There's two special cases that should be treated as
		 * "not found":
		 * - found a pair, but the type isn't what we expect
		 * - lookup failed, and we can confirm it really doesn't exist
		 */
		if ((err == 0 && nvpair_type(pair) != nvm_nvtype[f->nvmf_kind]) ||
		    (err == EINVAL && !nvlist_exists(nv, f->nvmf_name)))
			pair = NULL;
	}

	/*
	 * A NULL pair is only acceptable for NVMK_FLAG, since the key's
	 * presence or absence is the value. For all others, its not found.
	 */
	int err = 0;
	if (pair == NULL && f->nvmf_kind != NVMK_FLAG)
		err = ENOENT;

	/* Dispatch the pair to the right handler. */
	if (err == 0) {
		switch (f->nvmf_adapter) {
		case NVMA_DIRECT:
			err = nvm_unmarshal_direct(pair, f, base);
			break;
		case NVMA_FIXED_ARRAY:
			err = nvm_unmarshal_fixed_array(pair, f, base);
			break;
		case NVMA_MAP:
			err = nvm_unmarshal_map(nv, pair, f, base);
			break;
		case NVMA_STRUCT:
			err = nvm_unmarshal_struct(pair, f, base);
			break;
		case NVMA_STRUCT_ARRAY:
			err = nvm_unmarshal_struct_array(nv, pair, f, base);
			break;
		}
	}

	if (err != 0)
		/* Not found or other error, reset the field. */
		nvm_reset_field(f, base);

	if (f->nvmf_flags & NVMF_OPTIONAL) {
		/*
		 * If it was optional, set the has_ flag accordingly. For
		 * ENOENT, clear the error, since that's a valid case. Note
		 * that in the error case any default was already set in the
		 * call to nvm_reset_field() above.
		 */
		*_NVM_FIELD_HAS(f, base) = (err == 0);
		if (err == ENOENT)
			err = 0;
	}

	return (err);
}

/*
 * Unmarshaling function. Uses the schema in `desc` to pull things out of `nv`
 * and set them in `a`.
 */
int
nvm_unmarshal(nvlist_t *nv, const nvm_desc_t *desc, void *a)
{
	uintptr_t base = (uintptr_t)a;
	int err = 0;

	/*
	 * We keep track of the schema fields we've seen in the nvlist so we
	 * can scan at the end and identify any extra fields that shouldn't
	 * be there.
	 */
	nvlist_t *seen;
	if ((err = nvlist_alloc(&seen, NV_UNIQUE_NAME, 0)) != 0)
		return (err);

	/*
	 * Track the spill field if we see it, for postprocessing. There will
	 * never be more than one; the macros enforce that at compile time.
	 */
	const nvm_field_t *spill = NULL;

	for (size_t i = 0; err == 0 && i < desc->nvmd_nfields; i++) {
		const nvm_field_t *f = &desc->nvmd_fields[i];
		const char *name = f->nvmf_name;

		if (name == NULL) {
			ASSERT0P(spill);
			spill = f;
			continue;
		}

		err = nvm_unmarshal_field(nv, f, base);

		if (err == 0)
			/* Note that we've processed this field. */
			err = nvlist_add_boolean(seen, name);
	}

	/*
	 * Processing leftovers on the nvlist. Method is different depending
	 * on whether or not a spill field is defined.
	 */
	if (err == 0 && spill != NULL) {
		/*
		 * Spill fields can only ever be MAP or SET. We call the
		 * unmap-from-nvlist helper function with the top nvlist and
		 * the list of known fields as the skip list.
		 */
		ASSERT3U(spill->nvmf_adapter, ==, NVMA_MAP);
		err = nvm_unmarshal_map_nv(nv, nv, spill, base, seen);
		if (err == ENOENT)
			/* Type mismatch; treat it like an unexpected extra. */
			err = E2BIG;
	} else {
		/*
		 * Walk the input nvlist. If there are any fields that aren't
		 * on the seen list, then they weren't in the schema and so the
		 * whole thing gets rejected.
		 */
		for (nvpair_t *pair = nvlist_next_nvpair(nv, NULL);
		    err == 0 && pair != NULL;
		    pair = nvlist_next_nvpair(nv, pair)) {
			if (nvlist_exists(seen, nvpair_name(pair)))
				continue;
			err = E2BIG;
		}
	}

	nvlist_free(seen);

	if (err != 0)
		/* On error, zero/default all fields. */
		nvm_reset(desc, base);

	return (err);
}
