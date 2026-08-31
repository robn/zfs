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

/*
 * Get pointers to the data, has_ and nelem_ fields. Note that these need
 * deref to get to the actual data element inside. No field cshape/flag checks
 * first; if the field doesn't have has_ or nelem_, the return pointer is
 * garbage.
 */
#define	_NVM_FIELD(f, base)		((void *)((base) + ((f)->nvmf_offset)))
#define	_NVM_FIELD_HAS(f, base) \
	((boolean_t *)((base) + ((f)->nvmf_has_offset)))
#define	_NVM_FIELD_NELEM(f, base) \
	((uint_t *)((base) + ((f)->nvmf_nelem_offset)))

/*
 * For NVMC_ARRAY, get a pointer to the array proper by dereferencing the field
 * offset pointer.
 */
#define	_NVM_FIELD_ARRAY(f, base)	(*(void **)_NVM_FIELD((f), (base)))

/* For NVMC_ARRAY, set the array pointer and number of elements. */
#define	_NVM_FIELD_SET_ARRAY(f, base, ptr, n)	do { \
		_NVM_FIELD_ARRAY((f), (base)) = (ptr); \
		*_NVM_FIELD_NELEM((f), (base)) = (n); \
	} while (0)

/*
 * Get a pointer to an array element by index, given a certain element size.
 * This is not a nvm_field_t operator, just any generic array.
 */
#define	_NVM_ARRAY_ELEM(ptr, elem, elem_size) \
	((void *)((uintptr_t)(ptr) + ((elem) * (elem_size))))

/* For NVMC_ARRAY, get the pointer to an array element by index. */
#define	_NVM_FIELD_ELEM(f, base, elem) \
	_NVM_ARRAY_ELEM(_NVM_FIELD_ARRAY((f), (base)), (elem), \
	    (f)->nvmf_elem_size)

/*
 * Map nvm_datatype_t to data_type_t. This is mostly done to make sure we can
 * present a symmetrical interface through the macros, in particular for
 * NVM_SCALAR and NVM_ARRAY, and so we can hide certain inconsistencies that
 * would make things harder for the user, like the
 * BOOLEAN/BOOLEAN_VALUE/BOOLEAN_ARRAY mismatch, or the difference in C types
 * for STRING and STRING_ARRAY. It also gives us room to grow and change in
 * the future without worrying about the nvpair API itself too much.
 */
static const data_type_t nvm_nvtype[] = {
	[NVMD_FLAG]		= DATA_TYPE_BOOLEAN,

	[NVMD_BOOLEAN]		= DATA_TYPE_BOOLEAN_VALUE,
	[NVMD_BYTE]		= DATA_TYPE_BYTE,
	[NVMD_INT8]		= DATA_TYPE_INT8,
	[NVMD_UINT8]		= DATA_TYPE_UINT8,
	[NVMD_INT16]		= DATA_TYPE_INT16,
	[NVMD_UINT16]		= DATA_TYPE_UINT16,
	[NVMD_INT32]		= DATA_TYPE_INT32,
	[NVMD_UINT32]		= DATA_TYPE_UINT32,
	[NVMD_INT64]		= DATA_TYPE_INT64,
	[NVMD_UINT64]		= DATA_TYPE_UINT64,
	[NVMD_STRING]		= DATA_TYPE_STRING,
	[NVMD_HRTIME]		= DATA_TYPE_HRTIME,
#ifndef _KERNEL
	[NVMD_DOUBLE]		= DATA_TYPE_DOUBLE,
#endif
	[NVMD_NVLIST]		= DATA_TYPE_NVLIST,

	[NVMD_BOOLEAN_ARRAY]	= DATA_TYPE_BOOLEAN_ARRAY,
	[NVMD_BYTE_ARRAY]	= DATA_TYPE_BYTE_ARRAY,
	[NVMD_INT8_ARRAY]	= DATA_TYPE_INT8_ARRAY,
	[NVMD_UINT8_ARRAY]	= DATA_TYPE_UINT8_ARRAY,
	[NVMD_INT16_ARRAY]	= DATA_TYPE_INT16_ARRAY,
	[NVMD_UINT16_ARRAY]	= DATA_TYPE_UINT16_ARRAY,
	[NVMD_INT32_ARRAY]	= DATA_TYPE_INT32_ARRAY,
	[NVMD_UINT32_ARRAY]	= DATA_TYPE_UINT32_ARRAY,
	[NVMD_INT64_ARRAY]	= DATA_TYPE_INT64_ARRAY,
	[NVMD_UINT64_ARRAY]	= DATA_TYPE_UINT64_ARRAY,
	[NVMD_STRING_ARRAY]	= DATA_TYPE_STRING_ARRAY,
	[NVMD_NVLIST_ARRAY]	= DATA_TYPE_NVLIST_ARRAY,
};

/*
 * A union type that can cover any NVMC_SCALAR C type. Lets us reduce the
 * amount of casting we have to do.
 */
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

/* Same, but for NVMC_ARRAY. */
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

/* A generic pairtype for working with MAP and SET. */
typedef struct {
	const char *name;
	void *value;
} nvm_pair_t;

/* Sanity check the nvm_pair_*_t types against the generic nvm_pair_t. */
#define _NVM_CHECK_PAIRTYPE(D) \
	_Static_assert( \
	    sizeof(_NVM_PAIRTYPE_##D) == sizeof(nvm_pair_t) && \
	    offsetof(_NVM_PAIRTYPE_##D, name) == offsetof(nvm_pair_t, name) && \
	    offsetof(_NVM_PAIRTYPE_##D, value) == offsetof(nvm_pair_t, value), \
	    "Pair struct layout mismatch for " #D)

_NVM_CHECK_PAIRTYPE(BOOLEAN);
_NVM_CHECK_PAIRTYPE(BYTE);
_NVM_CHECK_PAIRTYPE(INT8);
_NVM_CHECK_PAIRTYPE(UINT8);
_NVM_CHECK_PAIRTYPE(INT16);
_NVM_CHECK_PAIRTYPE(UINT16);
_NVM_CHECK_PAIRTYPE(INT32);
_NVM_CHECK_PAIRTYPE(UINT32);
_NVM_CHECK_PAIRTYPE(INT64);
_NVM_CHECK_PAIRTYPE(UINT64);
_NVM_CHECK_PAIRTYPE(STRING);
_NVM_CHECK_PAIRTYPE(HRTIME);
#ifndef _KERNEL
_NVM_CHECK_PAIRTYPE(DOUBLE);
#endif
_NVM_CHECK_PAIRTYPE(NVLIST);

/* ========== */

/*
 * Add a name & value pair to the nvlist, according to the NV datatype in dt.
 * NVMD_FLAG is handled like a regular scalar type since its such a miniscule
 * difference in structure.
 */
static int
nvm_marshal_pair(nvlist_t *nv, nvm_datatype_t dt, const char *name,
    const void *v, uint_t nelem)
{
	int err = 0;

	const nvm_scalar_u *su = v;
	const nvm_array_u *au = v;

	switch (dt) {
	case NVMD_FLAG:
		if (su->b)
			err = nvlist_add_boolean(nv, name);
		break;

	case NVMD_BOOLEAN:
		err = nvlist_add_boolean_value(nv, name, su->b);
		break;
	case NVMD_BYTE:
		err = nvlist_add_byte(nv, name, su->byte);
		break;
	case NVMD_INT8:
		err = nvlist_add_int8(nv, name, su->i8);
		break;
	case NVMD_UINT8:
		err = nvlist_add_uint8(nv, name, su->u8);
		break;
	case NVMD_INT16:
		err = nvlist_add_int16(nv, name, su->i16);
		break;
	case NVMD_UINT16:
		err = nvlist_add_uint16(nv, name, su->u16);
		break;
	case NVMD_INT32:
		err = nvlist_add_int32(nv, name, su->i32);
		break;
	case NVMD_UINT32:
		err = nvlist_add_uint32(nv, name, su->u32);
		break;
	case NVMD_INT64:
		err = nvlist_add_int64(nv, name, su->i64);
		break;
	case NVMD_UINT64:
		err = nvlist_add_uint64(nv, name, su->u64);
		break;
	case NVMD_STRING:
		err = nvlist_add_string(nv, name, su->str);
		break;
	case NVMD_HRTIME:
		err = nvlist_add_hrtime(nv, name, su->hrtime);
		break;
#ifndef _KERNEL
	case NVMD_DOUBLE:
		err = nvlist_add_double(nv, name, su->d);
		break;
#endif
	case NVMD_NVLIST:
		err = nvlist_add_nvlist(nv, name, su->nvl);
		break;

	case NVMD_BOOLEAN_ARRAY:
		err = nvlist_add_boolean_array(nv, name, au->b, nelem);
		break;
	case NVMD_BYTE_ARRAY:
		err = nvlist_add_byte_array(nv, name, au->byte, nelem);
		break;
	case NVMD_INT8_ARRAY:
		err = nvlist_add_int8_array(nv, name, au->i8, nelem);
		break;
	case NVMD_UINT8_ARRAY:
		err = nvlist_add_uint8_array(nv, name, au->u8, nelem);
		break;
	case NVMD_INT16_ARRAY:
		err = nvlist_add_int16_array(nv, name, au->i16, nelem);
		break;
	case NVMD_UINT16_ARRAY:
		err = nvlist_add_uint16_array(nv, name, au->u16, nelem);
		break;
	case NVMD_INT32_ARRAY:
		err = nvlist_add_int32_array(nv, name, au->i32, nelem);
		break;
	case NVMD_UINT32_ARRAY:
		err = nvlist_add_uint32_array(nv, name, au->u32, nelem);
		break;
	case NVMD_INT64_ARRAY:
		err = nvlist_add_int64_array(nv, name, au->i64, nelem);
		break;
	case NVMD_UINT64_ARRAY:
		err = nvlist_add_uint64_array(nv, name, au->u64, nelem);
		break;
	case NVMD_STRING_ARRAY:
		err = nvlist_add_string_array(nv, name,
		    (const char * const *)au->str, nelem);
		break;
	case NVMD_NVLIST_ARRAY:
		err = nvlist_add_nvlist_array(nv, name,
		    (const nvlist_t * const *)au->nvl, nelem);
		break;
	}

	return (err);
}

/* NVMT_DIRECT. Just add the pair as-is to the nvlist. */
static int
nvm_marshal_direct(nvlist_t *nv, const nvm_field_t *f, uintptr_t base)
{
	uint_t nelem =
	    (f->nvmf_cshape == NVMC_ARRAY) ? *_NVM_FIELD_NELEM(f, base) : 0;
	return (nvm_marshal_pair(nv, f->nvmf_datatype, f->nvmf_name,
	    _NVM_FIELD(f, base), nelem));
}

/*
 * NVMT_FIXED_ARRAY. It's stored as a regular pair, just have to set up
 * the pointer and nelems differently.
 */
static int
nvm_marshal_fixed_array(nvlist_t *nv, const nvm_field_t *f, uintptr_t base)
{
	void *arr = _NVM_FIELD(f, base);
	return (nvm_marshal_pair(nv, f->nvmf_datatype, f->nvmf_name,
	    &arr, f->nvmf_nelem_offset));
}

/*
 * NVMT_MAP. Walk the array of nvm_pair_t, and add each name/value as a pair to
 * the nvlist.
 *
 * If the field is a spill field (nvmf_name == NULL), then the pairs will be
 * added directly to the given nvlist. Otherwise, a new nvlist will be created,
 * the pairs added to it, and once done, that nvlist added to the given nvlist.
 *
 * If a key already exists in the nvlist, the entire op will abort with EEXIST.
 * This is both a safety against duplicates when the pair array is supplied by
 * thye user, and protection against a spill pair trampling a true schema
 * field.
 *
 * NVMD_FLAG is handled here as a special case, since it's about the same
 * amount of code to make a dummy boolean_t and call nvm_marshal_pair(), but
 * harder to read.
 */
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
		else if (f->nvmf_elem_datatype == NVMD_FLAG)
			err = nvlist_add_boolean(map, pair->name);
		else
			err = nvm_marshal_pair(map, f->nvmf_elem_datatype,
			    pair->name, &pair->value, 0);
	}

	if (map == nv)
		return (err);

	if (err == 0)
		err = nvlist_add_nvlist(nv, f->nvmf_name, map);
	nvlist_free(map);

	return (err);
}

/*
 * NVMT_STRUCT. Create an nvlist, marshal the inline struct into it according
 * to the sub-schema description, and add the nvlist to the given list with
 * the name for the field.
 */
static int
nvm_marshal_struct(nvlist_t *nv, const nvm_field_t *f, uintptr_t base)
{
	nvlist_t *sub = NULL;
	int err = nvlist_alloc(&sub, NV_UNIQUE_NAME, 0);
	if (err == 0)
		err = nvm_marshal(sub, f->nvmf_subdesc, _NVM_FIELD(f, base));
	if (err == 0)
		err = nvlist_add_nvlist(nv, f->nvmf_name, sub);
	nvlist_free(sub);
	return (err);
}

/*
 * NVMT_STRUCT_ARRAY. Creating an array of nvlists, walking the array of
 * structs, and marshaling each into its own nvlist, before finally adding
 * the nvlist array to the given nvlist and freeing it all.
 *
 * It's all fairly linear because on any error, we have to unwind all the work
 * we've done before ejecting.
 */
static int
nvm_marshal_struct_array(nvlist_t *nv, const nvm_field_t *f, uintptr_t base)
{
	uint_t nelem = *_NVM_FIELD_NELEM(f, base);
	if (nelem == 0) {
		/* No elements, so add empty array. */
		return (nvlist_add_nvlist_array(nv, f->nvmf_name, NULL, 0));
	}

	/* Allocate array of nvlist_t pointers. */
	nvlist_t **arr = kmem_zalloc(nelem * sizeof (nvlist_t *), KM_SLEEP);

	/* Keep going while there's no error. */
	int err = 0;
	for (uint_t i = 0; err == 0 && i < nelem; i++) {
		nvlist_t *sub = NULL;
		err = nvlist_alloc(&sub, NV_UNIQUE_NAME, 0);
		if (err == 0) {
			/* Marshal the i'th element into its own nvlist. */
			err = nvm_marshal(sub, f->nvmf_subdesc,
			    _NVM_FIELD_ELEM(f, base, i));
		}
		arr[i] = sub;
	}

	if (err == 0) {
		/* Array completed, add it to the nvlist. */
		err = nvlist_add_nvlist_array(nv, f->nvmf_name,
		    (const nvlist_t * const *)arr, nelem);
	}

	/*
	 * Error or not, we don't need these nvlists anymore. We stop at the
	 * first NULL, which presumably means an error and we didn't end up
	 * filling all the slots.
	 */
	for (uint_t i = 0; i < nelem && arr[i] != NULL; i++)
		nvlist_free(arr[i]);

	/* And deallocate the pointer array too. */
	kmem_free(arr, nelem * sizeof (nvlist_t *));

	return (err);
}

/*
 * Public marshal entry point. Populate the given nvlist from the object,
 * according to the schema.
 */
int
nvm_marshal(nvlist_t *nv, const nvm_desc_t *desc, void *a)
{
	uintptr_t base = (uintptr_t)a;
	int err = 0;

	const nvm_field_t *spill = NULL;

	for (size_t i = 0; err == 0 && i < desc->nvmd_nfields; i++) {
		const nvm_field_t *f = &desc->nvmd_fields[i];

		/* Track the spill field. */
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

		/* Dispatch to the transform function for the field. */
		switch (f->nvmf_transform) {
		case NVMT_DIRECT:
			err = nvm_marshal_direct(nv, f, base);
			break;
		case NVMT_FIXED_ARRAY:
			err = nvm_marshal_fixed_array(nv, f, base);
			break;
		case NVMT_MAP:
			err = nvm_marshal_map(nv, f, base);
			break;
		case NVMT_STRUCT:
			err = nvm_marshal_struct(nv, f, base);
			break;
		case NVMT_STRUCT_ARRAY:
			err = nvm_marshal_struct_array(nv, f, base);
			break;
		}
	}

	/* If there was a spill field, apply it as a map to the nvlist. */
	if (err == 0 && spill != NULL)
		err = nvm_marshal_map(nv, spill, base);

	return (err);
}

/* ========== */

/* Set this scalar to an appropriate "zero" value for its type. */
static void
nvm_zero_scalar(nvm_datatype_t dt, nvm_scalar_u *u)
{
	switch (dt) {
	case NVMD_FLAG:
	case NVMD_BOOLEAN:
		u->b = B_FALSE;
		break;
	case NVMD_BYTE:
		u->byte = 0;
		break;
	case NVMD_INT8:
		u->i8 = 0;
		break;
	case NVMD_UINT8:
		u->u8 = 0;
		break;
	case NVMD_INT16:
		u->i16 = 0;
		break;
	case NVMD_UINT16:
		u->u16 = 0;
		break;
	case NVMD_INT32:
		u->i32 = 0;
		break;
	case NVMD_UINT32:
		u->u32 = 0;
		break;
	case NVMD_INT64:
		u->i64 = 0;
		break;
	case NVMD_UINT64:
		u->u64 = 0;
		break;
	case NVMD_STRING:
		u->str = NULL;
		break;
	case NVMD_HRTIME:
		u->hrtime = 0;
		break;
#ifndef _KERNEL
	case NVMD_DOUBLE:
		u->d = 0.0;
		break;
#endif
	case NVMD_NVLIST:
		u->nvl = NULL;
		break;

	default:
		__builtin_unreachable();
	}
}

/* Set this scalar to this default value, in a way appropriate to its type. */
static void
nvm_default_scalar(nvm_datatype_t dt, nvm_scalar_u *u, const uintptr_t def)
{
	switch (dt) {
	case NVMD_BOOLEAN:
		u->b = def;
		break;
	case NVMD_BYTE:
		u->byte = def;
		break;
	case NVMD_INT8:
		u->i8 = (intptr_t)def;
		break;
	case NVMD_UINT8:
		u->u8 = def;
		break;
	case NVMD_INT16:
		u->i16 = (intptr_t)def;
		break;
	case NVMD_UINT16:
		u->u16 = def;
		break;
	case NVMD_INT32:
		u->i32 = (intptr_t)def;
		break;
	case NVMD_UINT32:
		u->u32 = def;
		break;
	case NVMD_INT64:
		u->i64 = (intptr_t)def;
		break;
	case NVMD_UINT64:
		u->u64 = def;
		break;
	case NVMD_STRING:
		u->str = (const char *)def;
		break;
	case NVMD_HRTIME:
		u->hrtime = def;
		break;

	default:
		__builtin_unreachable();
	}
}

/* Zero all elements in this array, by their type. */
static void
nvm_zero_array_fixed(nvm_datatype_t dt, void *arr, uint_t nelem)
{
	nvm_array_u *u = (nvm_array_u *)&arr;

	for (uint_t i = 0; i < nelem; i++) {
		switch (dt) {
		case NVMD_BOOLEAN_ARRAY:
			nvm_zero_scalar(NVMD_BOOLEAN,
			    (nvm_scalar_u *)&u->b[i]);
			break;
		case NVMD_BYTE_ARRAY:
			nvm_zero_scalar(NVMD_BYTE,
			    (nvm_scalar_u *)&u->byte[i]);
			break;
		case NVMD_INT8_ARRAY:
			nvm_zero_scalar(NVMD_INT8,
			    (nvm_scalar_u *)&u->i8[i]);
			break;
		case NVMD_UINT8_ARRAY:
			nvm_zero_scalar(NVMD_UINT8,
			    (nvm_scalar_u *)&u->u8[i]);
			break;
		case NVMD_INT16_ARRAY:
			nvm_zero_scalar(NVMD_INT16,
			    (nvm_scalar_u *)&u->i16[i]);
			break;
		case NVMD_UINT16_ARRAY:
			nvm_zero_scalar(NVMD_UINT16,
			    (nvm_scalar_u *)&u->u16[i]);
			break;
		case NVMD_INT32_ARRAY:
			nvm_zero_scalar(NVMD_INT32,
			    (nvm_scalar_u *)&u->i32[i]);
			break;
		case NVMD_UINT32_ARRAY:
			nvm_zero_scalar(NVMD_UINT32,
			    (nvm_scalar_u *)&u->u32[i]);
			break;
		case NVMD_INT64_ARRAY:
			nvm_zero_scalar(NVMD_INT64,
			    (nvm_scalar_u *)&u->i64[i]);
			break;
		case NVMD_UINT64_ARRAY:
			nvm_zero_scalar(NVMD_UINT64,
			    (nvm_scalar_u *)&u->u64[i]);
			break;
		case NVMD_STRING_ARRAY:
			nvm_zero_scalar(NVMD_STRING,
			    (nvm_scalar_u *)&u->str[i]);
			break;
		case NVMD_NVLIST_ARRAY:
			nvm_zero_scalar(NVMD_NVLIST,
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
			/* Has a default, use it. */
			nvm_default_scalar(f->nvmf_datatype,
			    _NVM_FIELD(f, base), f->nvmf_default);
		else if (f->nvmf_subdesc)
			/* Recursive reset. */
			nvm_reset(f->nvmf_subdesc,
			    (uintptr_t)_NVM_FIELD(f, base));
		else
			/* Anything else gets zeroed. */
			nvm_zero_scalar(f->nvmf_datatype, _NVM_FIELD(f, base));
		break;

	case NVMC_ARRAY:
		/*
		 * Dynamic array just gets a zeroed. Any allocation is tracked
		 * elsewhere, so we can drop it without leaking it.
		 */
		_NVM_FIELD_SET_ARRAY(f, base, NULL, 0);
		break;

	case NVMC_FIXED_ARRAY:
		/* Fixed array, zero the elements. */
		nvm_zero_array_fixed(f->nvmf_datatype,
		    _NVM_FIELD(f, base), f->nvmf_nelem_offset);
		break;
	}
}

/* "Reset" is to apply defaults or zero to each field, as appropriate. */
static void
nvm_reset(const nvm_desc_t *desc, uintptr_t base)
{
	for (size_t i = 0; i < desc->nvmd_nfields; i++)
		nvm_reset_field(&desc->nvmd_fields[i], base);
}

/* ========== */

/*
 * Pull the value of the given type out of the pair. It's assumed the pair is
 * of the right type; the caller will need to handle any error. NVMD_FLAG is
 * handled specially by allowing a NULL pair, so the whole thing can be driven
 * by nvlist_lookup_nvpair() in the caller.
 */
static int
nvm_unmarshal_pair(nvpair_t *pair, nvm_datatype_t dt, void *v, uint_t *nelemp)
{
	int err = 0;

	switch (dt) {
	case NVMD_FLAG:
		*(boolean_t *)v = (pair != NULL);
		break;

	case NVMD_BOOLEAN:
		err = nvpair_value_boolean_value(pair, v);
		break;
	case NVMD_BYTE:
		err = nvpair_value_byte(pair, v);
		break;
	case NVMD_INT8:
		err = nvpair_value_int8(pair, v);
		break;
	case NVMD_UINT8:
		err = nvpair_value_uint8(pair, v);
		break;
	case NVMD_INT16:
		err = nvpair_value_int16(pair, v);
		break;
	case NVMD_UINT16:
		err = nvpair_value_uint16(pair, v);
		break;
	case NVMD_INT32:
		err = nvpair_value_int32(pair, v);
		break;
	case NVMD_UINT32:
		err = nvpair_value_uint32(pair, v);
		break;
	case NVMD_INT64:
		err = nvpair_value_int64(pair, v);
		break;
	case NVMD_UINT64:
		err = nvpair_value_uint64(pair, v);
		break;
	case NVMD_STRING:
		err = nvpair_value_string(pair, v);
		break;
	case NVMD_HRTIME:
		err = nvpair_value_hrtime(pair, v);
		break;
#ifndef _KERNEL
	case NVMD_DOUBLE:
		err = nvpair_value_double(pair, v);
		break;
#endif
	case NVMD_NVLIST:
		err = nvpair_value_nvlist(pair, v);
		break;

	case NVMD_BOOLEAN_ARRAY:
		err = nvpair_value_boolean_array(pair, v, nelemp);
		break;
	case NVMD_BYTE_ARRAY:
		err = nvpair_value_byte_array(pair, v, nelemp);
		break;
	case NVMD_INT8_ARRAY:
		err = nvpair_value_int8_array(pair, v, nelemp);
		break;
	case NVMD_UINT8_ARRAY:
		err = nvpair_value_uint8_array(pair, v, nelemp);
		break;
	case NVMD_INT16_ARRAY:
		err = nvpair_value_int16_array(pair, v, nelemp);
		break;
	case NVMD_UINT16_ARRAY:
		err = nvpair_value_uint16_array(pair, v, nelemp);
		break;
	case NVMD_INT32_ARRAY:
		err = nvpair_value_int32_array(pair, v, nelemp);
		break;
	case NVMD_UINT32_ARRAY:
		err = nvpair_value_uint32_array(pair, v, nelemp);
		break;
	case NVMD_INT64_ARRAY:
		err = nvpair_value_int64_array(pair, v, nelemp);
		break;
	case NVMD_UINT64_ARRAY:
		err = nvpair_value_uint64_array(pair, v, nelemp);
		break;
	case NVMD_STRING_ARRAY:
		err = nvpair_value_string_array(pair, v, nelemp);
		break;
	case NVMD_NVLIST_ARRAY:
		err = nvpair_value_nvlist_array(pair, v, nelemp);
		break;
	}

	return (err);
}

/* NVMT_DIRECT. Just unpack the pair according to the field params. */
static int
nvm_unmarshal_direct(nvpair_t *pair, const nvm_field_t *f, uintptr_t base)
{
	uint_t *nelemp =
	    (f->nvmf_cshape == NVMC_ARRAY) ? _NVM_FIELD_NELEM(f, base) : NULL;
	return (nvm_unmarshal_pair(pair, f->nvmf_datatype,
	    _NVM_FIELD(f, base), nelemp));

}

/*
 * NVMT_FIXED_ARRAY. We unpack it from an array pair, then check the element
 * count. If it matches, we copy the array contents into place.
 */
static int
nvm_unmarshal_fixed_array(nvpair_t *pair, const nvm_field_t *f, uintptr_t base)
{
	void *tarr;
	uint_t tnelem;

	int err = nvm_unmarshal_pair(pair, f->nvmf_datatype, &tarr, &tnelem);
	if (err == 0) {
		if (tnelem != f->nvmf_nelem_offset)
			err = ERANGE;
		else
			memcpy(_NVM_FIELD(f, base), tarr,
			    f->nvmf_elem_size * f->nvmf_nelem_offset);
	}

	return (err);
}

/*
 * NVMT_MAP. This is split into two parts. nvm_marshal_map() takes the nvpair_t
 * and pulls the nvlist_t out of it, then passes it to nvm_unmarshal_map_nv().
 * That's there so that nvm_unmarshal() can call here too for spill handling,
 * where the "map" nvlist is the user-provided "root" nvlist, which has no
 * pair.
 *
 * There's two nvlist_t arguments. nvalloc is the nvlist that the pair array
 * will be allocated against, ie the user-provided nvlist. map is the one
 * from the pair, which will be an embedded nvlist on the original somewhere.
 * In the spill case, they will be the same.
 *
 * The skip nvlist is for the spill case, where map is the user-provided
 * nvlist. If present, its keys are the ones from the schema that we already
 * unmarshaled, so we don't want to spill them too.
 */
static int
nvm_unmarshal_map_nv(nvlist_t *nvalloc, nvlist_t *map, const nvm_field_t *f,
    uintptr_t base, nvlist_t *skip)
{
	uint_t nelem = fnvlist_num_pairs(map);
	if (nelem == 0) {
		/* Empty nvlist, just zero the field. */
		_NVM_FIELD_SET_ARRAY(f, base, NULL, 0);
		return (0);
	}

	int err = 0;

	/* Allocate space for the pair array. */
	void *arr;
	if ((err = nvlist_alloc_aux(nvalloc,
	    f->nvmf_elem_size * nelem, &arr)) != 0)
		return (err);

	/* Walk the nvlist and extract the pairs. */
	uint_t elem = 0;
	for (nvpair_t *ep = nvlist_next_nvpair(map, NULL);
	    err == 0 && ep != NULL; ep = nvlist_next_nvpair(map, ep)) {
		const char *ename = nvpair_name(ep);

		/* If its on the skip list, ignore it. */
		if (skip != NULL && nvlist_exists(skip, ename))
			continue;

		/* Get a handle on the pair array element. */
		nvm_pair_t *nvmp = _NVM_ARRAY_ELEM(arr, elem, f->nvmf_elem_size);
		elem++;

		nvmp->name = ename;
		if (f->nvmf_elem_datatype == NVMD_FLAG)
			/*
			 * Flags are just strings, and the "pair" is just
			 * char pointer. We're done.
			 */
			continue;

		/* Extract the pair value according to type. */
		err = nvm_unmarshal_pair(ep, f->nvmf_elem_datatype,
		    &nvmp->value, NULL);

		if (err == EINVAL)
			/*
			 * We know the pair exists, so it must be the wrong
			 * type, which means we found something not described
			 * by the schema, and thus ENOENT.
			 */
			err = ENOENT;
	}

	if (err == 0) {
		/*
		 * Note that we're setting nelem to number of elements we saw,
		 * not the number in the nvlist, as we may have skipped some.
		 */
		_NVM_FIELD_SET_ARRAY(f, base, arr, elem);
	}

	return (err);
}

/*
 * Pull the nvlist out of the pair, then pass it to nvm_unmarshal_map_nv().
 * See comment there.
 */
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

/* NVMT_STRUCT. Pull the nvlist, then recurse into nvm_unmarshal(). */
static int
nvm_unmarshal_struct(nvpair_t *pair, const nvm_field_t *f, uintptr_t base)
{
	nvlist_t *sub = NULL;
	int err = nvpair_value_nvlist(pair, &sub);
	if (err != 0)
		return (err);
	return (nvm_unmarshal(sub, f->nvmf_subdesc, _NVM_FIELD(f, base)));
}

/*
 * NVMT_STRUCT_ARRAY. Pull the nvlist array, allocate an array of the target
 * struct, then unmarshal each element into it.
 */
static int
nvm_unmarshal_struct_array(nvlist_t *nvalloc,
    nvpair_t *pair, const nvm_field_t *f, uintptr_t base)
{
	nvlist_t **arr;
	uint_t nelem;

	int err = nvpair_value_nvlist_array(pair, &arr, &nelem);
	if (err != 0)
		return (err);

	if (nelem == 0) {
		/* Empty nvlist, so just zero the array. */
		_NVM_FIELD_SET_ARRAY(f, base, NULL, 0);
		return (0);
	}

	/* Allocate the array. */
	void *elems = NULL;
	err = nvlist_alloc_aux(nvalloc, f->nvmf_elem_size * nelem, &elems);
	if (err != 0)
		return (err);

	/* Loop the nvlists and unmarshal them into the matching element. */
	for (uint_t i = 0; err == 0 && i < nelem; i++)
		err = nvm_unmarshal(arr[i], f->nvmf_subdesc,
		    _NVM_ARRAY_ELEM(elems, i, f->nvmf_elem_size));

	if (err == 0) {
		/* Done, hook it up to the top object. */
		_NVM_FIELD_SET_ARRAY(f, base, elems, nelem);
	}

	return (err);
}

/*
 * Pull the field from the nvlist into the object. Note that nv can be NULL,
 * as no input is valid for a schema with all optional fields. (Allowing it
 * makes recursive unmarshal easier to implement).
 */
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
		if ((err == 0 &&
		    nvpair_type(pair) != nvm_nvtype[f->nvmf_datatype]) ||
		    (err == EINVAL && !nvlist_exists(nv, f->nvmf_name)))
			pair = NULL;
	}

	/*
	 * A NULL pair is only acceptable for NVMD_FLAG, since the key's
	 * presence or absence is the value. For all others, its not found.
	 */
	int err = 0;
	if (pair == NULL && f->nvmf_datatype != NVMD_FLAG)
		err = ENOENT;

	/* Dispatch the pair to the right handler. */
	if (err == 0) {
		switch (f->nvmf_transform) {
		case NVMT_DIRECT:
			err = nvm_unmarshal_direct(pair, f, base);
			break;
		case NVMT_FIXED_ARRAY:
			err = nvm_unmarshal_fixed_array(pair, f, base);
			break;
		case NVMT_MAP:
			err = nvm_unmarshal_map(nv, pair, f, base);
			break;
		case NVMT_STRUCT:
			err = nvm_unmarshal_struct(pair, f, base);
			break;
		case NVMT_STRUCT_ARRAY:
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
 * Main unmarshal entry. Populate the object from the nvlist, according to the
 * schema.
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

		/* Track the spill field. */
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
