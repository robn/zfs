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

#ifndef _SYS_NVPAIR_MARSHAL_H
#define	_SYS_NVPAIR_MARSHAL_H

#include <sys/nvpair.h>
#include <sys/kmem.h>

/*
 * This is a struct marshaling layer for nvlists. The idea is that the
 * programmer provides a single description (schema) of the items in an nvlist,
 * the corresponding struct name, the type of the value and some options, and
 * the compiler generates a C struct and the necessary plumbing for a single
 * marshal or unmarshal call to populate an nvlist from a struct, or a struct
 * from an nvlist, including validating and rejecting any data that does not
 * match the description.
 *
 * ### Simple example
 *
 * Describe the schema:
 *
 *	NVM_SCHEMA(user_t,
 *	    ("id",           NVM_SCALAR(UINT64), id,           NVM_REQUIRED),
 *	    ("name",         NVM_SCALAR(STRING), name,         NVM_REQUIRED),
 *	    ("display_name", NVM_SCALAR(STRING), display_name, NVM_OPTIONAL),
 *	    ("winner",       NVM_FLAG,           winner),
 *	);
 *
 * This generates a struct like:
 *
 *	typedef struct {
 *		uint64_t	id;
 *		const char	*name;
 *		const char	*display_name;
 *		boolean_t	has_display_name;
 *		boolean_t	winner;
 *	} user_t;
 *
 * And two global objects that describe the struct and the mapping:
 *
 *	static const nvm_field_t _nvm__user_t__fields[];
 *	static const nvm_desc_t _nvm__user_t__desc;
 *
 * To marshal a struct (copy its contents into a nvlist), use the NVM_MARSHAL()
 * macro:
 *
 *	user_t user = {
 *		.id   = 12345,
 *		.name = "Barry B. Benson",
 *	};
 *	nvlist_t *nvl = fnvlist_alloc();
 *	VERIFY0(NVM_MARSHAL(nvl, user_t, &user));
 *
 * The resulting nvlist is:
 *
 *	nvlist version: 0
 *		id = 0x3039
 *		name = Barry B. Benson
 *
 * To unmarshal a nvlist (copy its contents into a struct), use the
 * NVM_UNMARSHAL() macro:
 *
 *	nvlist_t *nvl = fnvlist_alloc();
 *	fnvlist_add_uint64(nvl, "id", 54321);
 *	fnvlist_add_string(nvl, "name", "Adam Flayman")
 *	fnvlist_add_string(nvl, "display_name", "Adam");
 *	fnvlist_add_boolean(nvl, "winner");
 *	user_t user;
 *	VERIFY0(NVM_UNMARSHAL(nvl, user_t, &user));
 *	VERIFY3U(user.id, ==, 54321);
 *	VERIFY0(strcmp(user.name, "Adam Flayman"));
 *	VERIFY0(strcmp(user.display_name, "Adam"));
 *	VERIFY(user.has_display_name);
 *	VERIFY(user.winner);
 *
 * See tests/unit/test_nvpair_marshal.c for many more examples.
 *
 * ### Schemas
 *
 * Defining a schema is done using the NVM_SCHEMA macro:
 *
 *	NVM_SCHEMA(type, field-spec, [field-spec, ...]);
 *
 * Each field spec is a tuple:
 *
 *	(key-name, kind, field-name [, req-rule])
 *
 * - key-name: the nvlist key, as a string. If NULL, the field describes the
 *   "spill" field, see "Spill field" below.
 *
 * - kind: the kind of data the field represents, one of:
 *	NVM_FLAG
 *	NVM_SCALAR(D)
 *	NVM_ARRAY(D)
 *	NVM_ARRAY_N(D, N)
 *	NVM_MAP(D)
 *	NVM_SET
 *	NVM_STRUCT(T)
 *	NVM_STRUCT_ARRAY(T)
 *   See below for details of each kind.
 *
 * - field-name: the name of the generated struct member
 *
 * - req-rule: the presence requirement for the key, zero or one of:
 *	NVM_REQUIRED
 *	NVM_OPTIONAL
 *	NVM_DEFAULT(V)
 *   See below for general description, and descriptions of each kind for
 *   variations.
 *
 * ### Requirement rules
 *
 * The requirement rule describes whether or not a field is required, and what
 * to do if its not. Their exact semantics can vary a little depending on the
 * kind, but the purpose described here is the same.
 *
 * - NVM_REQUIRED indicates that a field is required.
 *   - NVM_UNMARSHAL() will return ENOENT if the key is not present in the
 *     nvlist or has the wrong type.
 *   - NVM_MARSHAL() will use the C field as-is as the input to the underlying
 *     nvlist_add_*().
 *
 * - NVM_OPTIONAL indicates that a field is optional.
 *   - NVM_SCHEMA() will generate an additional struct field,
 *     'boolean_t has_<field-name>' to store whether or not a value exists.
 *   - NVM_UNMARSHAL() will set the has_ field to B_TRUE if the nvlist field
 *     exists, or to B_FALSE if the key is not present or has the wrong type.
 *     If not found, the actual field will be set to an appropriate "zero"
 *     value for the underlying type, so it can be used as a default without
 *     checking the has_ field.
 *   - NVM_MARSHAL() will check the has_ field, and skip adding the value to
 *     the nvlist if it not B_TRUE (regardless of the value).
 *
 * - NVM_DEFAULT(V) indicates an optional field, and gives a default value.
 *   This is effectively the same as NVM_OPTIONAL, but V is used as the "not
 *   found" value instead of zero. Note that this is only available with
 *   NVM_SCALAR(D) kinds, see that section for more info.
 *
 * ### Field Kinds
 *
 * #### NVM_FLAG
 *
 * NVM_FLAG is a presence-only boolean. Because its presence or absence decides
 * it value, it makes no sense to define it as "required" or "optional", so any
 * req-rule is rejected.
 *
 * The NV pair type is DATA_TYPE_BOOLEAN; the C field type is boolean_t.
 *
 * #### NVM_SCALAR(D)
 *
 * NVM_SCALAR(D) is for scalar (single-value) data, basically the same as the
 * traditional non-array NV pair types.
 *
 * | D       | NV pair type            | C field type | Default |
 * | ------- | ----------------------- | ------------ | ------- |
 * | BOOLEAN | DATA_TYPE_BOOLEAN_VALUE | boolean_t    | B_FALSE |
 * | BYTE    | DATA_TYPE_BYTE          | uchar_t      | 0       |
 * | INT8    | DATA_TYPE_INT8          | int8_t       | 0       |
 * | UINT8   | DATA_TYPE_UINT8         | uint8_t      | 0       |
 * | INT16   | DATA_TYPE_INT16         | int16_t      | 0       |
 * | UINT16  | DATA_TYPE_UINT16        | uint16_t     | 0       |
 * | INT32   | DATA_TYPE_INT32         | int32_t      | 0       |
 * | UINT32  | DATA_TYPE_UINT32        | uint32_t     | 0       |
 * | INT64   | DATA_TYPE_INT64         | int64_t      | 0       |
 * | UINT64  | DATA_TYPE_UINT64        | uint64_t     | 0       |
 * | STRING  | DATA_TYPE_STRING        | const char * | NULL    |
 * | HRTIME  | DATA_TYPE_HRTIME        | hrtime_t     | 0       |
 * | DOUBLE  | DATA_TYPE_DOUBLE        | double       | 0.0     |
 * | NVLIST  | DATA_TYPE_NVLIST        | nvlist_t *   | NULL    |
 *
 * Note: DOUBLE is not available in the kernel.
 *
 * If not found, NVM_OPTIONAL will set the value field to an appropriate "zero"
 * value for the type as shown in the table.
 *
 * As described above, NVM_DEFAULT(V) is like NVM_OPTIONAL, but will use V as
 * the default value when not found. This value is stored as a intptr_t in the
 * schema description; this allow NVM_DEFAULT("static string") to be safely
 * used as a default for STRING. Technically NVM_DEFAULT can be used for
 * NVLIST, but probably not in practice since NVM_DEFAULT currently requires
 * a const value, which would normally mean fixed at compile time. It's
 * included for completeness and to not add further complexity to the
 * implementation.
 *
 * #### NVM_ARRAY(D)
 *
 * NVM_ARRAY(D) is for arrays of a single type, basically the same as the
 * traditional array NV pairs. The list of D is the same as for NVM_SCALAR,
 * with HRTIME and DOUBLE excluded, which do not have corresponding array
 * NV pair types.
 *
 * The NV pair type is DATA_TYPE_<D>_ARRAY. The underlying C type is a pointer
 * to the C field type listed above.
 *
 * Since the array size is dynamic, an additional field
 * 'uint_t nelem_<field-name>' will be generated. NVM_UNMARSHAL() will populate
 * this field with the number of elements. NVM_MARSHAL() will assume there are
 * this many elements in the referenced array. 0 is a valid number of elements;
 * this is the empty array.
 *
 * NVM_OPTIONAL works with NVM_ARRAY(D); the has_ field will be generated as
 * normal to indicate the presence of the key/array _at all_, vs nelem_ which
 * indicates the number of elements. If the field is not present in the nvlist,
 * NVM_UNMARSHAL() will set the field to NULL (per normal NVM_OPTIONAL rules),
 * and will also set the nelem_ field to 0, so it can be used as a "how many"
 * check without having to check for presence.
 *
 * NVM_DEFAULT(V) does not work with NVM_ARRAY(D); it will fail to compile.
 *
 * #### NVM_ARRAY_N(D, N)
 *
 * NVM_ARRAY_N(D, N) is like NVM_ARRAY(D), except that any array which does
 * not have exactly N elements is rejected.
 *
 * The underlying C type is an array[] of the C field type listed above, with
 * no additional allocation. The nelem_ field is no longer generated, since the
 * length is known at compile time (and you can ARRAY_SIZE() if you need to get
 * it after the fact).
 *
 * NVM_UNMARSHAL() will return ERANGE if the nvlist has an array pair with
 * matching name and type but the wrong number of elements. This is true even
 * with NVM_OPTIONAL, since that is about the _presence_ of the field, not
 * its (malformed) contents.
 *
 * With NVM_OPTIONAL, if the array is not found, NVM_UNMARSHAL() will "zero"
 * all elements (per NVM_SCALAR(D)). NVM_DEFAULT(V) does not work with
 * NVM_ARRAY_N(D, N).
 *
 * #### NVM_MAP(D)
 *
 * NVM_MAP(D) is for a "map" or "dictionary"-like object, with string keys
 * mapped to a value of type D. The list of D is the same as for NVM_SCALAR,
 * excluding NVLIST.
 *
 * The NV pair type is always DATA_TYPE_NVLIST. All pairs in the nvlist must
 * be DATA_TYPE_<D>.
 *
 * The underlying C type is an array of nvm_pair_*_t. These types have two
 * fields, 'name' and 'value', eg:
 *
 * typedef struct { const char *name; uint64_t value; } nvm_pair_uint64_t;
 *
 * Each of these "pair" elements are filled with the name and value from the
 * nvlist.
 *
 * In all other ways, NVM_MAP behaves like NVM_ARRAY - count of elements is
 * in nelem_<field> and behaviour of NVM_REQUIRED/NVM_OPTIONAL is the same.
 *
 * #### NVM_SET
 *
 * NVM_SET is a special-case of NVM_MAP(D) for string flags; it's what you
 * might expect if you wrote NVM_MAP(FLAG).
 *
 * The NV pair type is always DATA_TYPE_NVLIST. All pairs in the nvlist must
 * be DATA_TYPE_BOOLEAN, that is, there are no values.
 *
 * The underlying C type is an array of const char *, that is, a string array.
 * The behaviour is identical to NVM_ARRAY(STRING).
 *
 * #### NVM_STRUCT(T)
 *
 * NVM_STRUCT(T) is for a previously-defined NVM_SCHEMA(T, ...) type. When
 * encountered, NVM_MARSHAL() and NVM_UNMARSHAL() will recurse into it,
 * allocating a new nvlist or struct and attaching it as appropriate to the
 * parent.
 *
 * (Note: all allocations are accounted to the nvlist, see "Usage notes" above)
 *
 * The NV pair type is always DATA_TYPE_NVLIST. The underlying C type is T,
 * embedding the sub-schema type directly.
 *
 * #### NVM_STRUCT_ARRAY(T)
 *
 * NVM_STRUCT_ARRAY(T) is like NVM_ARRAY(D), but for NVM_STRUCT(T). The rules
 * are what you'd expect - the array is walked and the nvlist or struct inside
 * is processed recursively.
 *
 * The NV pair type is always DATA_TYPE_NVLIST_ARRAY. The underlying C type
 * is T**. Like NVM_ARRAY(D), a nelem_<field-name> field is generated and
 * managed.
 *
 * There is currently no NVM_STRUCT_ARRAY_N(T, N).
 *
 * ### Spill field
 *
 * The "spill" field is defined by setting its key-name parameter (first arg)
 * to NULL, and the field type to either NVM_MAP(D) or NVM_SET.
 *
 * Normally, if there are any extra keys in an nvlist that aren't defined in
 * the schema, NVM_UNMARSHAL() will fail with E2BIG. If a spill field is
 * defined, then the remaining keys will instead be used as inputs to the spill
 * field, that is, they will be type checked and unmarshaled as a NVM_MAP(D) or
 * NVM_SET and added to the field array as normal.
 *
 * If a spill field is defined and any of the remaining keys do not match
 * the type, NVM_UNMARSHAL() will return E2BIG (as above; keys that aren't
 * expected by the schema).
 *
 * ### Usage notes
 *
 * - NVM_MARSHAL() will not modify the original struct. Any allocations made
 *   will be accounted to the nvlist, and freed at the same time. Thus, it is
 *   safe to continue use the original struct after the nvlist has been freed.
 *
 * - NVM_UNMARSHAL() will not modify the original nvlist. Any allocations made
 *   will be accounted to the nvlist, and freed at the same time. Thus, it is
 *   _not_ safe to use the original struct after the nvlist has been freed;
 *   consider it as derived from the nvlist and bound to its lifetime.
 *
 * - By design, NVM_MARSHAL() and NVM_UNMARSHAL() should be the direct inverse
 *   of each other, that is, nvlist == marshal(unmarshal(nvlist)) and
 *   struct == unmarshal(marshal(struct)). This only holds for data actually
 *   postively held in the struct or nvlist, ie, a default set for a missing
 *   option key using NVM_DEFAULT doesn't technically "exist", and so will be
 *   dropped during conversion.
 */

/* ========== */

/*
 * Field datatypes. These describe the way the field is stored in the nvlist,
 * and more-or-less map to a NV pair type.
 */
typedef enum {
	NVMD_FLAG,

	NVMD_BOOLEAN,
	NVMD_BYTE,
	NVMD_INT8,
	NVMD_UINT8,
	NVMD_INT16,
	NVMD_UINT16,
	NVMD_INT32,
	NVMD_UINT32,
	NVMD_INT64,
	NVMD_UINT64,
	NVMD_STRING,
	NVMD_HRTIME,
#ifndef _KERNEL
	NVMD_DOUBLE,
#endif
	NVMD_NVLIST,

	NVMD_BOOLEAN_ARRAY,
	NVMD_BYTE_ARRAY,
	NVMD_INT8_ARRAY,
	NVMD_UINT8_ARRAY,
	NVMD_INT16_ARRAY,
	NVMD_UINT16_ARRAY,
	NVMD_INT32_ARRAY,
	NVMD_UINT32_ARRAY,
	NVMD_INT64_ARRAY,
	NVMD_UINT64_ARRAY,
	NVMD_STRING_ARRAY,
	NVMD_NVLIST_ARRAY,
} nvm_datatype_t;

/*
 * C "shape". This describes how the field is represented in the generated
 * struct.
 */
typedef enum {
	NVMC_SCALAR,		/* inline value (uint64_t, char*, T) */
	NVMC_ARRAY,		/* ptr + count (uint64_t*, nvm_pair_t*, T*) */
	NVMC_FIXED_ARRAY,	/* inline array (uint64_t[N]) */
} nvm_cshape_t;

/*
 * Transform function. Describes how nvm_marshal()/nvm_unmarshal() should
 * convert this field between an NV pair and a struct field.
 */
typedef enum {
	NVMT_DIRECT,		/* 1:1 from struct field to NV pair */
	NVMT_FIXED_ARRAY,	/* NV array to C fixed array, count enforced */
	NVMT_MAP,		/* nvlist to C array of nvm_pair_*_t */
	NVMT_STRUCT,		/* recursive (un)marshal to inline struct */
	NVMT_STRUCT_ARRAY,	/* array of nvlist to array of inline struct */
} nvm_transform_t;

/* Field flags. Modifies processing in various ways. */
typedef enum {
	NVMF_OPTIONAL = (1<<0),	/* Optional field, set has_, zero on ENOENT. */
	NVMF_DEFAULT = (1<<1),	/* Default field, := nvmf_default on ENOENT. */
} nvm_flags_t;

/*
 * Each row in NVM_SCHEMA creates a nvm_field_t. Filled by _NVM_DESC_ONE.
 * Order and packing aren't super important, but we try to keep the size down
 * without overcomplicating the dead space.
 */
typedef struct nvm_desc nvm_desc_t;
typedef struct nvm_field {
	/* NV data type, C shape and transform. */
	nvm_datatype_t	nvmf_datatype:5;
	nvm_cshape_t	nvmf_cshape:2;
	nvm_transform_t	nvmf_transform:3;

	/* Flags. */
	nvm_flags_t	nvmf_flags:2;

	/*
	 * For kinds that expand an nvlist pair into an array of its elements
	 * (NVM_MAP, NVM_SET), the datatype of the element pairs.
	 */
	nvm_datatype_t	nvmf_elem_datatype:5;

	/*
	 * Size of the C type of the data field, or for arrays, a single
	 * element in that array.
	 */
	size_t		nvmf_elem_size;

	/* Offset of the data field in the generated struct. */
	size_t		nvmf_offset;

	/*
	 * For NVMC_ARRAY, offset of the nelem_ field. For NVMC_FIXED_ARRAY,
	 * the number of elements in the array.
	 */
	size_t		nvmf_nelem_offset;

	/* For NVM_OPTIONAL/NVM_DEFAULT, offset of the has_ field. */
	size_t		nvmf_has_offset;

	/* Name of the nvlist field, or NULL for spill fields. */
	const char	*nvmf_name;

	/*
	 * For NVM_STRUCT(T) and NVM_STRUCT_ARRAY(T), the schema description of
	 * T, for recursive (un)marshaling.
	 */
	const nvm_desc_t *nvmf_subdesc;

	/* For NVM_DEFAULT(V), the default value. */
	uintptr_t	nvmf_default;
} nvm_field_t;

/* Schema description. */
struct nvm_desc {
	/* Pointer to field array and number of fields in it. */
	const nvm_field_t	*nvmd_fields;
	size_t			nvmd_nfields;

	/* Size of the generated struct, so callers can allocate one. */
	size_t			nvmd_struct_size;
};

/* ========== */

/*
 * Scalar type mappings. This is how we convert the D in NVM_SCALAR(D) /
 * NVM_ARRAY(D) to the underling C field type or NV data type.  Technically,
 * these are a single copyable variable in an nvpair value slot that do not
 * require any allocations. Strings and "raw" nvlists are here because they are
 * treated the same way; eg fnvlist_lookup_string() just returns a pointer into
 * the existing nvlist data.
 *
 * For any D we get from the user, we can create one of three tokens:
 *
 * - _NVM_CTYPE_##D: the C type of the struct field
 * - _NVM_DATATYPE_##D: the nvm_datatype_t for the scalar form
 * - _NVM_DATATYPE_ARRAY_##D: the nvm_datatype_t for the array form
 *
 * Note also the special case of _NVM_CTYPE_ARRAY_##D, for when the scalar
 * and array ctype differ. Since we only have one case of this, we use an
 * override rather than write all the other types out twice.
 */
#define	_NVM_CTYPE_BOOLEAN		boolean_t
#define	_NVM_DATATYPE_BOOLEAN		NVMD_BOOLEAN
#define	_NVM_DATATYPE_ARRAY_BOOLEAN	NVMD_BOOLEAN_ARRAY

#define	_NVM_CTYPE_BYTE			uchar_t
#define	_NVM_DATATYPE_BYTE		NVMD_BYTE
#define	_NVM_DATATYPE_ARRAY_BYTE	NVMD_BYTE_ARRAY

#define	_NVM_CTYPE_INT8			int8_t
#define	_NVM_DATATYPE_INT8		NVMD_INT8
#define	_NVM_DATATYPE_ARRAY_INT8	NVMD_INT8_ARRAY

#define	_NVM_CTYPE_UINT8		uint8_t
#define	_NVM_DATATYPE_UINT8		NVMD_UINT8
#define	_NVM_DATATYPE_ARRAY_UINT8	NVMD_UINT8_ARRAY

#define	_NVM_CTYPE_INT16		int16_t
#define	_NVM_DATATYPE_INT16		NVMD_INT16
#define	_NVM_DATATYPE_ARRAY_INT16	NVMD_INT16_ARRAY

#define	_NVM_CTYPE_UINT16		uint16_t
#define	_NVM_DATATYPE_UINT16		NVMD_UINT16
#define	_NVM_DATATYPE_ARRAY_UINT16	NVMD_UINT16_ARRAY

#define	_NVM_CTYPE_INT32		int32_t
#define	_NVM_DATATYPE_INT32		NVMD_INT32
#define	_NVM_DATATYPE_ARRAY_INT32	NVMD_INT32_ARRAY

#define	_NVM_CTYPE_UINT32		uint32_t
#define	_NVM_DATATYPE_UINT32		NVMD_UINT32
#define	_NVM_DATATYPE_ARRAY_UINT32	NVMD_UINT32_ARRAY

#define	_NVM_CTYPE_INT64		int64_t
#define	_NVM_DATATYPE_INT64		NVMD_INT64
#define	_NVM_DATATYPE_ARRAY_INT64	NVMD_INT64_ARRAY

#define	_NVM_CTYPE_UINT64		uint64_t
#define	_NVM_DATATYPE_UINT64		NVMD_UINT64
#define	_NVM_DATATYPE_ARRAY_UINT64	NVMD_UINT64_ARRAY

#define	_NVM_CTYPE_STRING		const char *
#define	_NVM_DATATYPE_STRING		NVMD_STRING
#define	_NVM_CTYPE_ARRAY_STRING		_NVM_OVERRIDE(char *)
#define	_NVM_DATATYPE_ARRAY_STRING	NVMD_STRING_ARRAY

/* No NV pair type for HRTIME array. */
#define	_NVM_CTYPE_HRTIME		hrtime_t
#define	_NVM_DATATYPE_HRTIME		NVMD_HRTIME

#ifndef _KERNEL
/* No NV pair type for DOUBLE array. */
#define	_NVM_CTYPE_DOUBLE		double
#define	_NVM_DATATYPE_DOUBLE		NVMD_DOUBLE
#endif

#define	_NVM_CTYPE_NVLIST		nvlist_t *
#define	_NVM_DATATYPE_NVLIST		NVMD_NVLIST
#define	_NVM_DATATYPE_ARRAY_NVLIST	NVMD_NVLIST_ARRAY

/* Special case for stringy flag; no C type. */
#define	_NVM_DATATYPE_FLAG		NVMD_FLAG

/* ========== */

/*
 * "Pair" types. These are for NVM_SET and NVM_MAP, which produce an array
 * of key/value pairs (or in the case of NVM_SET, an array of strings).
 */

#define	_NVM_PAIRTYPE_BOOLEAN	nvm_pair_boolean_t
#define	_NVM_PAIRTYPE_BYTE	nvm_pair_byte_t
#define	_NVM_PAIRTYPE_INT8	nvm_pair_int8_t
#define	_NVM_PAIRTYPE_UINT8	nvm_pair_uint8_t
#define	_NVM_PAIRTYPE_INT16	nvm_pair_int16_t
#define	_NVM_PAIRTYPE_UINT16	nvm_pair_uint16_t
#define	_NVM_PAIRTYPE_INT32	nvm_pair_int32_t
#define	_NVM_PAIRTYPE_UINT32	nvm_pair_uint32_t
#define	_NVM_PAIRTYPE_INT64	nvm_pair_int64_t
#define	_NVM_PAIRTYPE_UINT64	nvm_pair_uint64_t
#define	_NVM_PAIRTYPE_STRING	nvm_pair_string_t
#define	_NVM_PAIRTYPE_HRTIME	nvm_pair_hrtime_t
#ifndef _KERNEL
#define	_NVM_PAIRTYPE_DOUBLE	nvm_pair_double_t
#endif
#define	_NVM_PAIRTYPE_NVLIST	nvm_pair_nvlist_t

typedef struct { const char *name; boolean_t value; }	_NVM_PAIRTYPE_BOOLEAN;
typedef struct { const char *name; uchar_t value; }	_NVM_PAIRTYPE_BYTE;
typedef struct { const char *name; int8_t value; }	_NVM_PAIRTYPE_INT8;
typedef struct { const char *name; uint8_t value; }	_NVM_PAIRTYPE_UINT8;
typedef struct { const char *name; int16_t value; }	_NVM_PAIRTYPE_INT16;
typedef struct { const char *name; uint16_t value; }	_NVM_PAIRTYPE_UINT16;
typedef struct { const char *name; int32_t value; }	_NVM_PAIRTYPE_INT32;
typedef struct { const char *name; uint32_t value; }	_NVM_PAIRTYPE_UINT32;
typedef struct { const char *name; int64_t value; }	_NVM_PAIRTYPE_INT64;
typedef struct { const char *name; uint64_t value; }	_NVM_PAIRTYPE_UINT64;
typedef struct { const char *name; const char *value; }	_NVM_PAIRTYPE_STRING;
typedef struct { const char *name; hrtime_t value; }	_NVM_PAIRTYPE_HRTIME;
#ifndef _KERNEL
typedef struct { const char *name; double value; }	_NVM_PAIRTYPE_DOUBLE;
#endif
typedef struct { const char *name; nvlist_t *value; }	_NVM_PAIRTYPE_NVLIST;

/* ========== */

/*
 * Field kinds.
 *
 * The kind is used as as a selector for the various elements that go into
 * _NVM_STRUCT_ONE and _NVM_DESC_ONE below.
 *
 * Each kind is expected to define at least these tokens, that resolve to:
 * - _NVM_CSHAPE_##kind:    nvm_cshape_t enum variant
 * - _NVM_TRANSFORM_##kind: nvm_transform_t enum variant
 * - _NVM_DATATYPE_##kind:  nvm_datatype_t enum variant
 * - _NVM_CTYPE_##kind:     C type
 *
 * Additional tokens may defined as overrides for defaults:
 * - _NVM_ELEM_DATATYPE_##kind: nvm_datatype_t for nvmf_elem_kind (default: 0)
 *
 * Compile-time checks are enforced for each field, which may be disabled
 * by overriding defaults:
 *
 * - _NVM_ALLOW_OPTIONAL_##kind: a field of this kind may be marked optional
 *                               or default (default: true)
 * - _NVM_ALLOW_DEFAULT_##kind: a field of this kind may be marked default
 *                              (default: false)
 * - _NVM_ALLOW_SPILL_##kind: a field of this kind may be a spill field
 *                            (default: false)
 *         
 *
 * - _NVM_NAME_##kind: macro that takes the key name, and emits the key name.
 *                     default is _NVM_NAME_NOSPILL (reject name == NULL)
 * - _NVM_REQ_FLAGS_##kind: macro that takes a requirement rule, emits a flag
 *                          set for nvmf_flags. default is _NVM_REQ_FLAGS_ALL
 * - _NVM_DEFAULT_VALUE_##kind: macro that takes a requirement rule, emits
 *				the default value for nvmf_default. default
 *				is _NVM_DEFAULT_VALUE_NODEFAULT
 */

/* NVM_FLAG. A "presence" field; present -> true, absent -> false. */
#define	_NVM_CSHAPE_NVM_FLAG		NVMC_SCALAR
#define	_NVM_TRANSFORM_NVM_FLAG		NVMT_DIRECT
#define	_NVM_DATATYPE_NVM_FLAG		_NVM_DATATYPE_FLAG
#define	_NVM_CTYPE_NVM_FLAG		_NVM_CTYPE_BOOLEAN

/* Reject NVM_OPTIONAL and NVM_DEFAULT, nonsensical for a presence field. */
#define	_NVM_ALLOW_OPTIONAL_NVM_FLAG	_NVM_OVERRIDE(false)


/* NVM_SCALAR(D). Constructed directly out of the scalar type mappings above. */
#define	_NVM_CSHAPE_NVM_SCALAR(D)		NVMC_SCALAR
#define	_NVM_TRANSFORM_NVM_SCALAR(D)		NVMT_DIRECT
#define	_NVM_DATATYPE_NVM_SCALAR(D)		_NVM_DATATYPE_##D
#define	_NVM_CTYPE_NVM_SCALAR(D)		_NVM_CTYPE_##D

/* Permit NVM_DEFAULT for scalar types. */
#define	_NVM_ALLOW_DEFAULT_NVM_SCALAR(D)	_NVM_OVERRIDE(true)


/* NVM_ARRAY(D). Like NVM_SCALAR, but uses the array types.  */
#define	_NVM_CSHAPE_NVM_ARRAY(D)		NVMC_ARRAY
#define	_NVM_TRANSFORM_NVM_ARRAY(D)		NVMT_DIRECT
#define	_NVM_DATATYPE_NVM_ARRAY(D)		_NVM_DATATYPE_ARRAY_##D

/*
 * Permit an override for array types, to support string arrays, which have
 * a different type in the nvlist API to strings. See _NVM_CTYPE_ARRAY_STRING.
 */
#define	_NVM_CTYPE_NVM_ARRAY(D) \
	_NVM_SELECT_OVERRIDE(_NVM_CTYPE_ARRAY_##D, _NVM_CTYPE_##D)


/*
 * NVM_ARRAY_N(D, N). Like NVM_ARRAY(D), and follows its types, but enforces
 * the specified length.
 */
#define	_NVM_CSHAPE_NVM_ARRAY_N(D, N)		NVMC_FIXED_ARRAY
#define	_NVM_TRANSFORM_NVM_ARRAY_N(D, N)	NVMT_FIXED_ARRAY
#define	_NVM_DATATYPE_NVM_ARRAY_N(D, N)		_NVM_DATATYPE_NVM_ARRAY(D)
#define	_NVM_CTYPE_NVM_ARRAY_N(D, N)		_NVM_CTYPE_NVM_ARRAY(D)

/* NVMC_FIXED_ARRAY needs to extract the element count for the initialiser. */
#define	_NVM_NELEMS_NVM_ARRAY_N(D, N)		N


/*
 * NVM_MAP(D). Maps an nvlist of type D pairs to a C array of nvm_pair_*_t.
 * See "Pair types" above.
 */
#define	_NVM_CSHAPE_NVM_MAP(D)		NVMC_ARRAY
#define	_NVM_TRANSFORM_NVM_MAP(D)	NVMT_MAP
#define	_NVM_DATATYPE_NVM_MAP(D)	_NVM_DATATYPE_NVLIST
#define	_NVM_CTYPE_NVM_MAP(D)		_NVM_PAIRTYPE_##D

/* Element datatype matches the pair type. */
#define	_NVM_ELEM_DATATYPE_NVM_MAP(D)	_NVM_OVERRIDE(_NVM_DATATYPE_##D)

/* MAP is a valid spill target. */
#define	_NVM_ALLOW_SPILL_NVM_MAP(D)	_NVM_OVERRIDE(true)


/* NVM_SET. Like NVM_MAP, but for an nvlist of flags to a string array. */
#define	_NVM_CSHAPE_NVM_SET		NVMC_ARRAY
#define	_NVM_TRANSFORM_NVM_SET		NVMT_MAP
#define	_NVM_DATATYPE_NVM_SET		_NVM_DATATYPE_NVLIST
#define	_NVM_CTYPE_NVM_SET		_NVM_CTYPE_STRING

/* Element datatype is a flag ie valueless. */
#define	_NVM_ELEM_DATATYPE_NVM_SET	_NVM_OVERRIDE(_NVM_DATATYPE_FLAG)

/* SET is a valid spill target. */
#define	_NVM_ALLOW_SPILL_NVM_SET	_NVM_OVERRIDE(true)


/*
 * NVM_STRUCT(T). T must have been declared earlier via NVM_SCHEMA(T, ...) so
 * that its field description already exists. Beyond a couple of extra field
 * types, the field description is like any other scalar type.
 */
#define	_NVM_CSHAPE_NVM_STRUCT(T)	NVMC_SCALAR
#define	_NVM_TRANSFORM_NVM_STRUCT(T)	NVMT_STRUCT
#define	_NVM_DATATYPE_NVM_STRUCT(T)	NVMD_NVLIST
#define	_NVM_CTYPE_NVM_STRUCT(T)	T

/* Set the sub-schema description so we can recurse. */
#define	_NVM_SUBDESC_NVM_STRUCT(T)	_NVM_OVERRIDE(NVM_DESC(T))


/*
 * NVM_STRUCT_ARRAY(T). Same as NVM_STRUCT(T), but following NVM_ARRAY instead
 * of NVM_SCALAR.
 */
#define	_NVM_CSHAPE_NVM_STRUCT_ARRAY(T)		NVMC_ARRAY
#define	_NVM_TRANSFORM_NVM_STRUCT_ARRAY(T)	NVMT_STRUCT_ARRAY
#define	_NVM_DATATYPE_NVM_STRUCT_ARRAY(T)	NVMD_NVLIST_ARRAY
#define	_NVM_CTYPE_NVM_STRUCT_ARRAY(T)		T

/* Set the sub-schema description so we can recurse. */
#define	_NVM_SUBDESC_NVM_STRUCT_ARRAY(T)	_NVM_OVERRIDE(NVM_DESC(T))

/* ========== */

/*
 * Requirement rules. We get a single arg from the caller, which is used to
 * control multiple elements:
 *
 * - rule flags in nvmf_flags (NVMF_OPTIONAL, NVMF_DEFAULT)
 * - default value in nvmf_default
 * - creation of the has_ field in the generated struct
 * - initialiser for nvmf_has_offset, to point to the has_ field
 */

/*
 * Convert the user-provided rule to a consistent internal form that we can
 * select on. The user is optional, and defaults to NVM_REQUIRED.
 */
#define	_NVM_RR_		_NVM_RR_REQUIRED(0)
#define	_NVM_RR_NVM_REQUIRED	_NVM_RR_REQUIRED(0)
#define	_NVM_RR_NVM_OPTIONAL	_NVM_RR_OPTIONAL(0)
#define	_NVM_RR_NVM_DEFAULT(V)	_NVM_RR_DEFAULT(V)
#define	_NVM_RR(rr, ...)	_NVM_RR_##rr

/* The actual requirement, regardless of default. */
#define	_NVM_RR_REQ__NVM_RR_REQUIRED(D)	REQUIRED
#define	_NVM_RR_REQ__NVM_RR_OPTIONAL(D)	OPTIONAL
#define	_NVM_RR_REQ__NVM_RR_DEFAULT(V)	OPTIONAL
#define	_NVM_RR_REQ(rr)			_NVM_RR_REQ_##rr

/* Default value for the rule, to set nvmf_default. */
#define	_NVM_RR_VALUE__NVM_RR_REQUIRED(V)	((uintptr_t)0)
#define	_NVM_RR_VALUE__NVM_RR_OPTIONAL(V)	((uintptr_t)0)
#define	_NVM_RR_VALUE__NVM_RR_DEFAULT(V)	((uintptr_t)(V))
#define	_NVM_RR_VALUE(rr)			_NVM_RR_VALUE_##rr

/* Rule flags for nvmf_flags. */
#define	_NVM_RR_FLAGS__NVM_RR_REQUIRED(D)	(0)
#define	_NVM_RR_FLAGS__NVM_RR_OPTIONAL(D)	(NVMF_OPTIONAL)
#define	_NVM_RR_FLAGS__NVM_RR_DEFAULT(V)	(NVMF_OPTIONAL|NVMF_DEFAULT)
#define	_NVM_RR_FLAGS(rr)			_NVM_RR_FLAGS_##rr

/* Emit the has_ field for OPTIONAL and DEFAULT. */
#define	_NVM_HAS_FLAG_REQUIRED(field)
#define	_NVM_HAS_FLAG_OPTIONAL(field)	boolean_t has_##field;
#define	_NVM_HAS_FLAG(field, rr) \
	_NVM_CONCAT(_NVM_HAS_FLAG_, _NVM_RR_REQ(rr))(field)

/* Emit the offset of the has_ field for OPTIONAL and DEFAULT, otherwise 0. */
#define	_NVM_HAS_OFFSET_REQUIRED(T, field)	0
#define	_NVM_HAS_OFFSET_OPTIONAL(T, field)	offsetof(T, has_##field)
#define	_NVM_HAS_OFFSET(T, field, rr) \
	_NVM_CONCAT(_NVM_HAS_OFFSET_, _NVM_RR_REQ(rr))(T, field)

/* ========== */

/*
 * Field tuple dispatch for generating the C struct. Called from NVM_SCHEMA
 * with the raw field tuple (including parens), or the empty token (for the
 * "trailing comma" case). _NVM_CALL_TUPLE tests the arg and calls
 * _NVM_STRUCT_ONE_ if its a tuple (so discarding the empty token). That
 * expands the optional fourth arg in the internal req-rule, and lands properly
 * in _NVM_STRUCT_ONE__ to generate the necessary struct fields.
 *
 * Each schema field is expanded according to the _NVM_STRUCT_##cshape for
 * the field kind (below), and then _NVM_HAS_FLAG() to emit the has_ field
 * if the field is optional.
 */
#define	_NVM_STRUCT_ONE(arg)	_NVM_CALL_TUPLE(_NVM_STRUCT_ONE_, arg)
#define	_NVM_STRUCT_ONE_(name, kind, field, ...) \
	_NVM_STRUCT_ONE__(name, kind, field, _NVM_RR(__VA_ARGS__))
#define	_NVM_STRUCT_ONE__(_name, kind, field, rr) \
	_NVM_CONCAT(_NVM_STRUCT_, _NVM_CSHAPE_##kind)(kind, field) \
	_NVM_HAS_FLAG(field, rr)

/* NVMC_SCALAR is just the straight C type and nothing else. */
#define	_NVM_STRUCT_NVMC_SCALAR(kind, field) \
	_NVM_CTYPE_##kind field;

/* NVMC_ARRAY is a dynamic array, so a pointer + count */
#define	_NVM_STRUCT_NVMC_ARRAY(kind, field) \
	_NVM_CTYPE_##kind *field; \
	uint_t nelem_##field;

/* NVMC_FIXED_ARRAY is an inline array of _NVM_NELEMS_##kind elements. */
#define	_NVM_STRUCT_NVMC_FIXED_ARRAY(kind, field) \
	_NVM_CTYPE_##kind field[_NVM_NELEMS_##kind];

/* ========== */

/*
 * Field tuple dispatch for generating the nvm_field_t in the schema
 * description. As above, the expansion proceeds in __NVM_DESC_ONE__. The
 * name of the generated type is passed through from NVM_SCHEMA as T.
 *
 * Almost all elements are described above in "Field kinds". Where these are
 * overrideable, the macros setting up the defaults are below.
 */
#define	_NVM_DESC_ONE(T, arg)	_NVM_CALL_TUPLE_ARG(_NVM_DESC_ONE_, T, arg)
#define	_NVM_DESC_ONE_(T, name, kind, field, ...) \
	_NVM_DESC_ONE__(T, name, kind, field, _NVM_RR(__VA_ARGS__))
#define	_NVM_DESC_ONE__(T, name, kind, field, rr) \
	{ \
	    .nvmf_datatype = _NVM_DATATYPE_##kind, \
	    .nvmf_cshape = _NVM_CSHAPE_##kind, \
	    .nvmf_transform = _NVM_TRANSFORM_##kind, \
	    .nvmf_flags = _NVM_RR_FLAGS(rr), \
	    .nvmf_elem_datatype = _NVM_ELEM_DATATYPE(kind), \
	    .nvmf_elem_size = sizeof (_NVM_CTYPE_##kind), \
	    .nvmf_offset = offsetof(T, field), \
	    .nvmf_nelem_offset = _NVM_NELEM_OFFSET(kind, T, field), \
	    .nvmf_has_offset = _NVM_HAS_OFFSET(T, field, rr), \
	    .nvmf_default = _NVM_RR_VALUE(rr), \
	    .nvmf_name = (name), \
	    .nvmf_subdesc = _NVM_SUBDESC(kind), \
	}, \

/*
 * nvmf_nelem_offset changes based on the C shape. 0 for scalars, the actual
 * offset for arrays, and the number of elements for fixed arrays.
 */
#define	_NVM_NELEM_OFFSET(kind, T, field)	\
	_NVM_CONCAT(_NVM_NELEM_OFFSET_, _NVM_CSHAPE_##kind)(kind, T, field)
#define	_NVM_NELEM_OFFSET_NVMC_SCALAR(kind, T, field)		0
#define	_NVM_NELEM_OFFSET_NVMC_ARRAY(kind, T, field) \
	offsetof(T, nelem_##field)
#define	_NVM_NELEM_OFFSET_NVMC_FIXED_ARRAY(kind, T, field) \
	_NVM_NELEMS_##kind

/* nvmf_elem_datatype is irrelevant for most field kinds, and defaults to 0. */
#define	_NVM_ELEM_DATATYPE(kind) \
	_NVM_SELECT_OVERRIDE(_NVM_ELEM_DATATYPE_##kind, 0)

/* nvmf_subdesc is irrelevant for most field kinds, and defaults to NULL. */
#define	_NVM_SUBDESC(kind)	_NVM_SELECT_OVERRIDE(_NVM_SUBDESC_##kind, NULL)

/* ========== */

/*
 * Field tuple dispatch for compile-time assertions. Each emits a bunch of
 * _Static_assert lines for the compiler to run.
 */
#define	_NVM_ASSERT_ONE(arg)	_NVM_CALL_TUPLE(_NVM_ASSERT_ONE_, arg)
#define	_NVM_ASSERT_ONE_(name, kind, field, ...) \
	_NVM_ASSERT_ONE__(name, kind, field, _NVM_RR(__VA_ARGS__))
#define	_NVM_ASSERT_ONE__(name, kind, field, rr) \
	_NVM_ASSERT_SPILL(kind, name, rr) \
	_NVM_ASSERT_OPTIONAL(kind, rr) \
	_NVM_ASSERT_DEFAULT(kind, rr)

/*
 * If a field uses NVM_OPTIONAL or NVM_DEFAULT, make sure the field kind allows
 * it. Defaults to true, kind may override via _NVM_ALLOW_OPTIONAL_##kind.
 */
#define	_NVM_ASSERT_OPTIONAL(kind, rr) \
	_Static_assert( !(_NVM_RR_FLAGS(rr) & NVMF_OPTIONAL) || \
	    _NVM_SELECT_OVERRIDE(_NVM_ALLOW_OPTIONAL_##kind, true), \
	    #kind " fields can only be NVM_REQUIRED");

/*
 * If a field uses NVM_DEFAULT, make sure the field kind allows it. Defaults
 * to false, kind may override via _NVM_ALLOW_DEFAULT_##kind.
 */
#define	_NVM_ASSERT_DEFAULT(kind, rr) \
	_Static_assert( !(_NVM_RR_FLAGS(rr) & NVMF_DEFAULT) || \
	    _NVM_SELECT_OVERRIDE(_NVM_ALLOW_DEFAULT_##kind, false), \
	    #kind " fields cannot be NVM_DEFAULT");

/*
 * If a field is a spill field (name == NULL), make sure the field kind allows
 * it. Defaults to false, kind may override via _NVM_ALLOW_SPILL_##kind. Also,
 * force a spill field to be NVM_REQUIRED; since its a virtual field, it makes
 * no sense for it to be optional (it might be _empty_, but that's different).
 */
#define	_NVM_ASSERT_SPILL(kind, name, rr) \
	_Static_assert( (name != NULL) || \
	    _NVM_SELECT_OVERRIDE(_NVM_ALLOW_SPILL_##kind, false), \
	    #kind " is not a valid kind for a spill field"); \
	_Static_assert( (name != NULL) || \
	    !(_NVM_RR_FLAGS(rr) & NVMF_OPTIONAL), \
	    "Spill fields can only be NVM_REQUIRED");

/* ========== */

/*
 * Field tuple dispatch for the spill field count. _NVM_SPILL_ONE emits an
 * expression that resolves to either 1 or 0 if the name is NULL or not. These
 * produce a constant addition for the _Static_assert in NVM_SCHEMA below, so
 * we can ensure that no more than one spill fields are defined.
 *
 * Because the conditional itself is generated from the entire schema, it
 * doesn't fit into _NVM_ASSERT_ONE, which is checking single fields.
 */
#define	_NVM_COUNT_SPILL_ONE(arg) \
	_NVM_CALL_TUPLE(_NVM_COUNT_SPILL_ONE_, arg)
#define	_NVM_COUNT_SPILL_ONE_(name, ...) \
	+ ((name) == NULL ? 1U : 0U)

/* ========== */

/* Public interface. */

/*
 * NVM_SCHEMA. Converts the incoming field tuples to:
 * - a typedef struct T
 * - the nvm_field_t array of fields
 * - the nvm_desc_t description object
 * - a set of static assertions checking each field
 * - a single static assertion ensuring there's no more than one spill field.
 */
#define	NVM_SCHEMA(T, ...) \
typedef struct T { \
	_NVM_FOR_EACH(_NVM_STRUCT_ONE, __VA_ARGS__) \
} T; \
static const nvm_field_t __maybe_unused _nvm__##T##__fields[] = { \
	_NVM_FOR_EACH_ARG(_NVM_DESC_ONE, T, __VA_ARGS__) \
}; \
static const nvm_desc_t __maybe_unused _nvm__##T##__desc = { \
	.nvmd_fields = _nvm__##T##__fields, \
	.nvmd_nfields = ARRAY_SIZE(_nvm__##T##__fields), \
	.nvmd_struct_size = sizeof (T) \
}; \
_NVM_FOR_EACH(_NVM_ASSERT_ONE, __VA_ARGS__) \
_Static_assert((0U \
	_NVM_FOR_EACH(_NVM_COUNT_SPILL_ONE, __VA_ARGS__) \
) < 2, "Only one spill field allowed for " #T)

/* Pointer to the description for the given type. */
#define	NVM_DESC(T)	(&_nvm__##T##__desc)

/*
 * Prototypes for the actual marshal/unmarshal functions. The second arg is
 * points to the description, use NVM_DESC(). Most of the time the NVM_MARSHAL
 * and NVM_UNMARSHAL macros are what you want.
 */
int nvm_marshal(nvlist_t *nv, const nvm_desc_t *desc, void *a);
int nvm_unmarshal(nvlist_t *nv, const nvm_desc_t *desc, void *a);

/*
 * NVM_MARSHAL and NVM_UNMARSHAL. Resolve T into the name of the nvm_desc_t,
 * then call the true function.
 */
#define	NVM_MARSHAL(nv, T, a)	nvm_marshal((nv), NVM_DESC(T), (a))
#define	NVM_UNMARSHAL(nv, T, a)	nvm_unmarshal((nv), NVM_DESC(T), (a))

/* ========== */

/*
 * The macro magic that makes all this work. Cribbed from various sources,
 * pretty much all of it common practice for what we're doing. You are not
 * expected to understand this (and I'm not sure I do either).
 */

/* Deferred eval. Ensures arguments are fully expanded before invocation. */
#define	_NVM_CALL(fn, ...)	fn(__VA_ARGS__)

/*
 * Select the third arg. Useful positional selector.
 */
#define	_NVM_SELECT_3(_a, _b, _c, ...) _c

/*
 * Value override. Define a token with _NVM_OVERRIDE(v). Call
 * _NVM_SELECT_OVERRIDE(ov, dv). If ov is a token defined with
 * _NVM_OVERRIDE(v), returns v. Otherwise, resolves dv normally and returns
 * that.
 *
 * Note that we push an additional arg into _NVM_SELECT_3 to push the result
 * out to the third arg, and avoid needing a _NVM_SELECT_2 as well.
 */
/* CSTYLED */
#define	_NVM_OVERRIDE(v)		, v
#define	_NVM_SELECT_OVERRIDE(...)	_NVM_SELECT_3(, __VA_ARGS__)

/*
 * Determine if the passed arg is a tuple. If it is, the expansion will be
 * _NVM_PROBE_TUPLE_(arg), which expands to ', 1', that is, a two-arg
 * replacement.  If not, the expansion is just '_NVM_PROBE_TUPLE_ <whatever>',
 * a single argument.
 */
/* CSTYLED */
#define	_NVM_PROBE_TUPLE_(...)	, 1
#define	_NVM_PROBE_TUPLE(arg)	_NVM_PROBE_TUPLE_ arg

/*
 * Return a function name depending on whether or not the arg is a tuple. If
 * it is, returns the passed in function; if not, _NVM_DISCARD, an
 * arg-swallowing no-op.
 */
#define	_NVM_IF_TUPLE(arg, fn) \
	_NVM_CALL(_NVM_SELECT_3, _NVM_PROBE_TUPLE(arg), fn, _NVM_DISCARD)

/* Consume args, do nothing. */
#define	_NVM_DISCARD(...)

/*
 * If the tuple arg is in fact a tuple, call the given function with the
 * tuple contents as args. In the extra-arg version, a passed in-arg is copied
 * as the first arg of the new call.
 */
#define	_NVM_CALL_TUPLE(fn, tuple) \
	_NVM_CALL(_NVM_IF_TUPLE(tuple, fn), _NVM_FLATTEN tuple)
#define	_NVM_CALL_TUPLE_ARG(fn, arg, tuple) \
	_NVM_CALL(_NVM_IF_TUPLE(tuple, fn), arg, _NVM_FLATTEN tuple)

/*
 * Paste args directly vs passing them on as args elsewhere. That is, remove
 * the parens from a tuple.
 */
#define	_NVM_FLATTEN(...)	__VA_ARGS__

/* Standard deferred token paste. */
#define	_NVM_CONCAT_(a, b)	a##b
#define	_NVM_CONCAT(a, b)	_NVM_CONCAT_(a, b)

/* Arg counting infrastructure for foreach below. */
#define	_NVM_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, \
	_14, _15, _16, _17, _18, _19, _20, N, ...) N

#define	_NVM_RSEQ_N() \
	20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

#define	_NVM_NARG_(...) _NVM_ARG_N(__VA_ARGS__)
#define	_NVM_NARG(...)  _NVM_NARG_(__VA_ARGS__, _NVM_RSEQ_N())

/*
 * Call ladder to implement foreach. Calls a function, then passes the
 * remainder through to the next step until all arguments are exhausted. Note
 * that we have our own deferred eval call here rather than using _NVM_CALL,
 * because almost certainly the caller is using that in their call pipeline and
 * using it ourselves will break the recursion.
 */

#define	_NVM_FE_CALL(fn, ...)	fn(__VA_ARGS__)

#define	_NVM_FE_1(fn, arg, x)	    fn(arg, x)
#define	_NVM_FE_2(fn, arg, x, ...)  fn(arg, x) _NVM_FE_1(fn, arg, __VA_ARGS__)
#define	_NVM_FE_3(fn, arg, x, ...)  fn(arg, x) _NVM_FE_2(fn, arg, __VA_ARGS__)
#define	_NVM_FE_4(fn, arg, x, ...)  fn(arg, x) _NVM_FE_3(fn, arg, __VA_ARGS__)
#define	_NVM_FE_5(fn, arg, x, ...)  fn(arg, x) _NVM_FE_4(fn, arg, __VA_ARGS__)
#define	_NVM_FE_6(fn, arg, x, ...)  fn(arg, x) _NVM_FE_5(fn, arg, __VA_ARGS__)
#define	_NVM_FE_7(fn, arg, x, ...)  fn(arg, x) _NVM_FE_6(fn, arg, __VA_ARGS__)
#define	_NVM_FE_8(fn, arg, x, ...)  fn(arg, x) _NVM_FE_7(fn, arg, __VA_ARGS__)
#define	_NVM_FE_9(fn, arg, x, ...)  fn(arg, x) _NVM_FE_8(fn, arg, __VA_ARGS__)
#define	_NVM_FE_10(fn, arg, x, ...) fn(arg, x) _NVM_FE_9(fn, arg, __VA_ARGS__)
#define	_NVM_FE_11(fn, arg, x, ...) fn(arg, x) _NVM_FE_10(fn, arg, __VA_ARGS__)
#define	_NVM_FE_12(fn, arg, x, ...) fn(arg, x) _NVM_FE_11(fn, arg, __VA_ARGS__)
#define	_NVM_FE_13(fn, arg, x, ...) fn(arg, x) _NVM_FE_12(fn, arg, __VA_ARGS__)
#define	_NVM_FE_14(fn, arg, x, ...) fn(arg, x) _NVM_FE_13(fn, arg, __VA_ARGS__)
#define	_NVM_FE_15(fn, arg, x, ...) fn(arg, x) _NVM_FE_14(fn, arg, __VA_ARGS__)
#define	_NVM_FE_16(fn, arg, x, ...) fn(arg, x) _NVM_FE_15(fn, arg, __VA_ARGS__)
#define	_NVM_FE_17(fn, arg, x, ...) fn(arg, x) _NVM_FE_16(fn, arg, __VA_ARGS__)
#define	_NVM_FE_18(fn, arg, x, ...) fn(arg, x) _NVM_FE_17(fn, arg, __VA_ARGS__)
#define	_NVM_FE_19(fn, arg, x, ...) fn(arg, x) _NVM_FE_18(fn, arg, __VA_ARGS__)
#define	_NVM_FE_20(fn, arg, x, ...) fn(arg, x) _NVM_FE_19(fn, arg, __VA_ARGS__)

/* Foreach entry point, with arg to pass as first arg to call. */
#define	_NVM_FOR_EACH_ARG(fn, arg, ...) \
    _NVM_CONCAT(_NVM_FE_, _NVM_NARG(__VA_ARGS__))(fn, arg, __VA_ARGS__)

/* No-arg version, bouncing through _NVM_FE_CALL to consume the arg. */
#define	_NVM_FOR_EACH(fn, ...) \
    _NVM_FOR_EACH_ARG(_NVM_FE_CALL, fn, __VA_ARGS__)

#endif
