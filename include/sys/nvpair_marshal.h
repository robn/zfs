#ifndef _SYS_NVPAIR_MARSHAL_H
#define	_SYS_NVPAIR_MARSHAL_H

#include <sys/nvpair.h>

/*
 * Generic FOR_EACH over a variadic arg list. Bump the numbers if you ever need
 * more than 10 fields in one struct.
 */
#define	_NV_ARG_N(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,N,...) N
#define	_NV_RSEQ_N() 10,9,8,7,6,5,4,3,2,1,0
#define	_NV_NARG_(...) _NV_ARG_N(__VA_ARGS__)
#define	_NV_NARG(...) _NV_NARG_(__VA_ARGS__, _NV_RSEQ_N())

#define	_NV_FE_1(what, x) what(x)
#define	_NV_FE_2(what, x, ...) what(x) _NV_FE_1(what, __VA_ARGS__)
#define	_NV_FE_3(what, x, ...) what(x) _NV_FE_2(what, __VA_ARGS__)
#define	_NV_FE_4(what, x, ...) what(x) _NV_FE_3(what, __VA_ARGS__)
#define	_NV_FE_5(what, x, ...) what(x) _NV_FE_4(what, __VA_ARGS__)
#define	_NV_FE_6(what, x, ...) what(x) _NV_FE_5(what, __VA_ARGS__)
#define	_NV_FE_7(what, x, ...) what(x) _NV_FE_6(what, __VA_ARGS__)
#define	_NV_FE_8(what, x, ...) what(x) _NV_FE_7(what, __VA_ARGS__)
#define	_NV_FE_9(what, x, ...) what(x) _NV_FE_8(what, __VA_ARGS__)
#define	_NV_FE_10(what, x, ...) what(x) _NV_FE_9(what, __VA_ARGS__)

#define	_NV_CONCAT_(a, b) a##b
#define	_NV_CONCAT(a, b) _NV_CONCAT_(a, b)

#define	_NV_FOR_EACH(what, ...) \
	_NV_CONCAT(_NV_FE_, _NV_NARG(__VA_ARGS__))(what, __VA_ARGS__)

/* Token override infrastructure, see _NV_LTYPE_NV_ARRAY */
#define	_NV_OVERRIDE(x)		~, x
#define	_NV_PICK3(_a, _b, ...)	_b
#define	_NV_PICK(...)		_NV_PICK3(__VA_ARGS__)

/*
 * Field config is a 3+1 element tuple:
 *   (key-name, kind, field-name, flags)
 *
 * XXX document insanity
 */
#define	_NV_MEMBER_ONE(args)	_NV_MEMBER_ONE_ args
#define	_NV_MARSHAL_ONE(args)	_NV_MARSHAL_ONE_ args
#define	_NV_UNMARSHAL_ONE(args)	_NV_UNMARSHAL_ONE_ args

#define	_NV_MEMBER_ONE_(name, kind, field, ...) \
	_NV_TEMPLATE(MEMBER, name, kind, field, __VA_ARGS__)
#define	_NV_MARSHAL_ONE_(name, kind, field, ...) \
	_NV_TEMPLATE(MARSHAL, name, kind, field, __VA_ARGS__)
#define	_NV_UNMARSHAL_ONE_(name, kind, field, ...) \
	_NV_TEMPLATE(UNMARSHAL, name, kind, field, __VA_ARGS__)

#define	_NV_TEMPLATE(func, name, kind, field, ...) \
	_NV_CONCAT(_NV_##func##_, _NV_TEMPLATE_##kind)(name, kind, field, __VA_ARGS__)

/*
 * Code fragments for defining, testing, and setting the has_<xxx> field
 * for optional fields. The "empty" variant is the same as NV_REQUIRED,
 * making that the default if the fourth arg is dropped.
 */
#define	_NV_HAS_FLAG_(field)
#define	_NV_HAS_FLAG_NV_REQUIRED(field)
#define	_NV_HAS_FLAG_NV_OPTIONAL(field) boolean_t has_##field;

#define	_NV_HAS_TEST_(field)		(1)
#define	_NV_HAS_TEST_NV_REQUIRED(field)	(1)
#define	_NV_HAS_TEST_NV_OPTIONAL(field)	(a->has_##field)

#define	_NV_HAS_SET_(field)
#define	_NV_HAS_SET_NV_REQUIRED(field)
#define	_NV_HAS_SET_NV_OPTIONAL(field) \
	a->has_##field = (err == 0); \
	err = (err == ENOENT) ? 0 : err;

#define	_NV_HAS_FLAG(field, req)	_NV_HAS_FLAG_##req(field)
#define	_NV_HAS_TEST(field, req)	_NV_HAS_TEST_##req(field)
#define	_NV_HAS_SET(field, req)		_NV_HAS_SET_##req(field)

/*
 * Scalar types are ones that are a single copyable variable in an nvpair value
 * slot, and do not require any allocations. Strings and "raw" nvlists fall
 * into the latter category, as they are just pointers into the existing nvlist
 * data.
 */
#define	_NV_CTYPE_BOOLEAN	boolean_t
#define	_NV_LTYPE_BOOLEAN	boolean_value

#define	_NV_CTYPE_BYTE		uchar_t
#define	_NV_LTYPE_BYTE		byte

#define	_NV_CTYPE_INT8		int8_t
#define	_NV_LTYPE_INT8		int8

#define	_NV_CTYPE_UINT8		uint8_t
#define	_NV_LTYPE_UINT8		uint8

#define	_NV_CTYPE_INT16		int16_t
#define	_NV_LTYPE_INT16		int16

#define	_NV_CTYPE_UINT16	uint16_t
#define	_NV_LTYPE_UINT16	uint16

#define	_NV_CTYPE_INT32		int32_t
#define	_NV_LTYPE_INT32		int32

#define	_NV_CTYPE_UINT32	uint32_t
#define	_NV_LTYPE_UINT32	uint32

#define	_NV_CTYPE_INT64		int64_t
#define	_NV_LTYPE_INT64		int64

#define	_NV_CTYPE_UINT64	uint64_t
#define	_NV_LTYPE_UINT64	uint64

#define	_NV_CTYPE_STRING	const char *
#define	_NV_LTYPE_STRING	string

#define	_NV_CTYPE_HRTIME	hrtime_t
#define	_NV_LTYPE_HRTIME	hrtime

#define	_NV_CTYPE_DOUBLE	double
#define	_NV_LTYPE_DOUBLE	double

#define	_NV_CTYPE_NVLIST	nvlist_t *
#define	_NV_LTYPE_NVLIST	nvlist


/* NV_TYPE(x) for a scalar type. */
#define	_NV_TEMPLATE_NV_TYPE(T)	SCALAR
#define	_NV_CTYPE_NV_TYPE(T)	_NV_CTYPE_##T
#define	_NV_LTYPE_NV_TYPE(T)	_NV_LTYPE_##T

/* Single field, with option existence flag for NV_OPTIONAL. */
#define	_NV_MEMBER_SCALAR(name, kind, field, req) \
	_NV_CTYPE_##kind field; \
	_NV_HAS_FLAG(field, req)

/* Scalar marshalling just wraps nvlist_add_* and nvlist_lookup_* */
#define	_NV_MARSHAL_SCALAR(name, kind, field, req) \
	if (err == 0 && _NV_HAS_TEST(field, req)) \
		err = _NV_CONCAT(nvlist_add_, _NV_LTYPE_##kind) \
		    (nv, name, a->field);
#define	_NV_UNMARSHAL_SCALAR(name, kind, field, req) \
	if (err == 0) { \
		err = _NV_CONCAT(nvlist_lookup_, _NV_LTYPE_##kind) \
		    (nv, name, &a->field); \
		_NV_HAS_SET(field, req) \
	}

/* NV_ARRAY(x) for array types. */
#define	_NV_TEMPLATE_NV_ARRAY(T)	ARRAY

/*
 * For the most part, we use the scalar CTYPE and LTYPE, with some overrides
 * where they don't work out for arrays.
 */

/*
 * nvpair uses _boolean_value() for scalar, and _boolean_array() for array,
 * so override the LTYPE selection.
 */
#define	_NV_LTYPE_ARRAY_OVERRIDE_BOOLEAN	_NV_OVERRIDE(boolean)

/* LTYPE picks override if present, otherwise scalar LTYPE. */
#define	_NV_LTYPE_NV_ARRAY(T) \
	_NV_PICK(_NV_LTYPE_ARRAY_OVERRIDE_##T, _NV_LTYPE_NV_TYPE(T))

/* String ctype is const for scalar, non-const for string array. */
#define	_NV_CTYPE_ARRAY_OVERRIDE_STRING		_NV_OVERRIDE(char *)

/* CTYPE picks override if present, otherwise scalar LTYPE. */
#define	_NV_CTYPE_NV_ARRAY(T) \
	_NV_PICK(_NV_CTYPE_ARRAY_OVERRIDE_##T, _NV_CTYPE_NV_TYPE(T))

/*
 * nvlist_add_string_array() and nvlist_add_nvlist_array() are very particular
 * about their arg type, so we force a cast for those.
 */
#define	_NV_CAST_ADD_STRING	_NV_OVERRIDE((const char * const *))
#define	_NV_CAST_ADD_NVLIST	_NV_OVERRIDE((const nvlist_t * const *))

/* CAST_ADD picks the cast if present, otherwise nothing. */
#define	_NV_CAST_ADD_NV_ARRAY(T)	_NV_PICK(_NV_CAST_ADD_##T, )

/* Pointer to first element of array, existence flag, and number of elements. */
#define	_NV_MEMBER_ARRAY(name, kind, field, req) \
	_NV_CTYPE_##kind *field; \
	uint_t nelem_##field; \
	_NV_HAS_FLAG(field, req)

/* Array marshalling is like scalar, just calling array add and lookup. */
#define	_NV_MARSHAL_ARRAY(name, kind, field, req) \
	if (err == 0 && _NV_HAS_TEST(field, req)) \
		err = _NV_CONCAT(_NV_CONCAT(nvlist_add_, _NV_LTYPE_##kind), _array) \
		    (nv, name, _NV_CAST_ADD_##kind a->field, a->nelem_##field);
#define	_NV_UNMARSHAL_ARRAY(name, kind, field, req) \
	if (err == 0) { \
		err = _NV_CONCAT(_NV_CONCAT(nvlist_lookup_, _NV_LTYPE_##kind), _array) \
		    (nv, name, &a->field, &a->nelem_##field); \
		_NV_HAS_SET(field, req) \
	}


/* NV_STRUCT(T) for recursive marshalling; T previously created by
 * NV_MARSHAL_TYPE. */
#define	_NV_TEMPLATE_NV_STRUCT(T)	STRUCT
#define	_NV_CTYPE_NV_STRUCT(T)		T

/* Single struct instance embdedded in the outer struct. */
#define	_NV_MEMBER_STRUCT(name, kind, field, req) \
	_NV_CTYPE_##kind field; \
	_NV_HAS_FLAG(field, req)

/*
 * Marshalling is creating a nvlist, caling the marshal function for the
 * type, then adding the nvlist to the parent.
 */
#define	_NV_MARSHAL_STRUCT(name, kind, field, req) \
	if (err == 0 && _NV_HAS_TEST(field, req)) { \
		nvlist_t *_sub = NULL; \
		err = nvlist_alloc(&_sub, NV_UNIQUE_NAME, 0); \
		if (err == 0) \
			err = _NV_CONCAT(_NV_CONCAT(_, _NV_CTYPE_##kind), __marshal) \
			    (_sub, &a->field); \
		if (err == 0) \
			err = nvlist_add_nvlist(nv, name, _sub); \
		nvlist_free(_sub); \
	}

/*
 * Unmarshaling is similar; get the nvlist by name, then call the type
 * unmarshal function.
 */
#define	_NV_UNMARSHAL_STRUCT(name, kind, field, req) \
	if (err == 0) { \
		nvlist_t *_sub = NULL; \
		err = nvlist_lookup_nvlist(nv, name, &_sub); \
		if (err == 0) { \
			err = _NV_CONCAT(_NV_CONCAT(_, _NV_CTYPE_##kind), __unmarshal) \
			    (_sub, &a->field); \
		} \
		_NV_HAS_SET(field, req) \
	}


/* NV_STRUCT_ARRAY(T). Array of T, converted to/from nvlist array. */
#define	_NV_TEMPLATE_NV_STRUCT_ARRAY(T)		STRUCT_ARRAY
#define	_NV_CTYPE_NV_STRUCT_ARRAY(T)		T

/* Same as ARRAY. but with a custom type. */
#define	_NV_MEMBER_STRUCT_ARRAY(name, kind, field, req) \
	_NV_CTYPE_##kind *field; \
	uint_t nelem_##field; \
	_NV_HAS_FLAG(field, req)

/*
 * Create an nvlist array, then loop all elements on the array field, convert
 * and copy them in. Once completed, nvlist_add_nvlist_array() to copy the
 * lot into the marshal target nvlist. Then destroy all the intermediate
 * nvlists.
 */
#define	_NV_MARSHAL_STRUCT_ARRAY(name, kind, field, req) \
	if (err == 0  && _NV_HAS_TEST(field, req)) { \
		nvlist_t *_arr[a->nelem_##field] = {}; \
		for (uint_t i = 0; err == 0 && i < a->nelem_##field; i++) { \
			nvlist_t *_sub = NULL; \
			err = nvlist_alloc(&_sub, NV_UNIQUE_NAME, 0); \
			if (err == 0) \
				err = _NV_CONCAT(_NV_CONCAT(_, _NV_CTYPE_##kind), \
				    __marshal) (_sub, &a->field[i]); \
			if (err == 0) \
				_arr[i] = _sub; \
		} \
		if (err == 0) \
			err = nvlist_add_nvlist_array(nv, name, \
			    (const nvlist_t * const*) _arr, a->nelem_##field); \
		for (uint_t i = 0; i < a->nelem_##field; i++) \
			if (_arr[i]) \
				nvlist_free(_arr[i]); \
	}

/*
 * Unmarshaling is the reverse. Get the nvlist array from the from source,
 * loop and convert them all, then put the completed array onto the target
 * struct.
 *
 * Note that the array is allocated from the nvlist, and so will be freed
 * automatically when the nvlist is freed, thus preserving the same semantics
 * as the other unmarshal calls - data in unmarshal target structs do not
 * outlive the source nvlist.
 */
#define	_NV_UNMARSHAL_STRUCT_ARRAY(name, kind, field, req) \
	if (err == 0) { \
		nvlist_t **_arr; \
		uint_t _n; \
		err = nvlist_lookup_nvlist_array(nv, name, &_arr, &_n); \
		if (err == 0) \
			err = nvlist_alloc_aux(nv, sizeof (_NV_CTYPE_##kind) * _n, \
			    (void **) &a->field); \
		if (err == 0) { \
			for (uint_t i = 0; err == 0 && i < _n; i++) { \
				err = _NV_CONCAT(_NV_CONCAT(_, _NV_CTYPE_##kind), \
				    __unmarshal)(_arr[i], &a->field[i]); \
			} \
			a->nelem_##field = _n; \
		} \
		_NV_HAS_SET(field, req) \
	}

/*
 * XXX TODO:
 *      - refuse/collect all additional keys into a "leftovers" nvlist
 *        (needed for ZK_WILDCARDLIST)
 *      - fixed-length arrays, eg NV_ARRAY_N(UINT64, 5). comes into its own
 *        for NV_STRUCT_ARRAY_N(marshalled_t, 5); fixed allocation. return
 *        ERANGE if exists but number doesn't match
 *      - ergonomics: consider NV_ARRAY() as a wrapper over a scalar type;
 *        NV_ARRAY(NV_TYPE(UINT64)), NV_ARRAY(NV_STRUCT(foo_t))
 *      - alternate storage type/class, eg NV_TYPE(INT32) + NV_REALTYPE(dmu_objset_type_t)
 *        int32 for all conversions, but is the realtype in the generated
 *        struct, removing need for casts
 *        - thinking on the above, I wonder if at least `NV_TYPE()` is a
 *          tuple of the nvlist type (DATA_TYPE_INT32), the primitive type
 *          assocated with it (int32_t), the name of the type in the nvpair
 *          functions (int32) and then optionally, the actual type we put
 *          in the struct (usually the same as the primitive type, but in this
 *          case, dmu_objset_type_t). it does make the marshal functions more
 *          complicated since we have to bounce through (eg) a stack int32_t
 *          and then reassign to the struct, or vice-versa. for simplicity we
 *          could do that even for the case where they're they're both the
 *          same, its just a tiny bit more overhead that possible doesn't
 *          matter
 *      - hmm, we could make allocating nvlist_unmarshal() and even
 *        fnvlist_unmarshal() if we were happy to allocate the return object.
 */

/* Top-level generators and main entry points. */

#define	NV_MARSHAL_TYPE(T, ...) \
typedef struct T { \
	_NV_FOR_EACH(_NV_MEMBER_ONE, __VA_ARGS__) \
} T; \
static int __maybe_unused \
_##T##__marshal(nvlist_t *nv, T *a) \
{ \
	int err = 0; \
	_NV_FOR_EACH(_NV_MARSHAL_ONE, __VA_ARGS__) \
	return (err); \
} \
\
static int __maybe_unused \
_##T##__unmarshal(nvlist_t *nv, T *a) \
{ \
	int err = 0; \
	_NV_FOR_EACH(_NV_UNMARSHAL_ONE, __VA_ARGS__) \
	return (err); \
}

#define	NV_MARSHAL(nv, T, a)   _##T##__marshal((nv), (a))
#define	NV_UNMARSHAL(nv, T, a) _##T##__unmarshal((nv), (a))

#endif
