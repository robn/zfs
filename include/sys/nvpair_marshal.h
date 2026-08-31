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
 *	VERIFY(a.has_display_name);
 *	VERIFY(a.winner);
 *
 * See tests/unit/test_nvpair_marshal.c for many more examples.
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
 * - NVM_UNMARSHAL() will fail with E2BIG if the nvlist contains any keys
 *   not declared in the schema.
 *
 * - By design, NVM_MARSHAL() and NVM_UNMARSHAL() should be the direct inverse
 *   of each other, that is, nvlist == marshal(unmarshal(nvlist)) and
 *   struct == unmarshal(marshal(struct)). This only holds for data actually
 *   postively held in the struct or nvlist, ie, a default set for a missing
 *   option key using NVM_DEFAULT doesn't technically "exist", and so may be
 *   dropped during conversion.
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
 * - key-name: the nvlist key, as a string
 *
 * - kind: the kind of data the field represents, one of:
 *	NVM_FLAG
 *	NVM_SCALAR(K)
 *	NVM_ARRAY(K)
 *	NVM_ARRAY_N(K, N)
 *	NVM_STRUCT(T)
 *	NVM_STRUCT_ARRAY(T)
 *   See below for details of each kind.
 *
 * - field-name: the name of the generated struct member
 *
 * - req-rule: the presence requirement for the key, zero or one of:
 *	NVM_REQUIRED
 *	NVM_OPTIONAL
 *	NVM_DEFAULT(D)
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
 * - NVM_DEFAULT(D) indicates an optional field, and gives a default value.
 *   This is effectively the same as NVM_OPTIONAL, but D is used as the "not
 *   found" value instead of zero. Note that this is only available with
 *   NVM_SCALAR(K) kinds, see that section for more info.
 *
 * ### Kinds
 *
 * #### NVM_FLAG
 *
 * NVM_FLAG is a presence-only boolean. Because its presence or absence decides
 * it value, it makes no sense to define it as "required" or "optional", so any
 * req-rule is ignored.
 *
 *
 * NV pair type: DATA_TYPE_BOOLEAN
 * C field type: boolean_t
 *
 * #### NVM_SCALAR(K)
 *
 * NVM_SCALAR(K) is for scalar (single-value) data, basically the same as the
 * traditional non-array NV pair types.
 *
 * | K       | NV pair type            | C field type | Default |
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
 * As described above, NVM_DEFAULT(D) is like NVM_OPTIONAL, but will use D as
 * the default value when not found. This value is stored as a intptr_t in the
 * schema description; this allow NVM_DEFAULT("static string") to be safely
 * used as a default for STRING. Technically NVM_DEFAULT can be used for
 * NVLIST, but probably not in practice since NVM_DEFAULT currently requires
 * a const value, which would normally mean fixed at compile time. It's
 * included for completeness and to not add further complexity to the
 * implementation.
 *
 * #### NVM_ARRAY(K)
 *
 * NVM_ARRAY(K) is for arrays of a single type, basically the same as the
 * traditional array NV pairs. The list of K is the same as for NVM_SCALAR,
 * with HRTIME and DOUBLE excluded, which do not have corresponding array
 * NV pair types.
 *
 * The NV pair type is DATA_TYPE_<K>_ARRAY. The underlying C type is a pointer
 * to the C field type listed above.
 *
 * Since the array size is dynamic, an additional field
 * 'uint_t nelem_<field-name>' will be generated. NVM_UNMARSHAL() will populate
 * this field with the number of elements. NVM_MARSHAL() will assume there are
 * this many elements in the referenced array. 0 is a valid number of elements;
 * this is the empty array.
 *
 * NVM_OPTIONAL works with NVM_ARRAY(K); the has_ field will be generated as
 * normal to indicate the presence of the key/array _at all_, vs nelem_ which
 * indicates the number of elements. If the field is not present in the nvlist,
 * NVM_UNMARSHAL() will set the field to NULL (per normal NVM_OPTIONAL rules),
 * and will also set the nelem_ field to 0, so it can be used as a "how many"
 * check without having to check for presence.
 *
 * NVM_DEFAULT(D) does not work with NVM_ARRAY(K); it will fail to compile.
 *
 * #### NVM_ARRAY_N(K, N)
 *
 * NVM_ARRAY_N(K, N) is like NVM_ARRAY(K), except that any array which does
 * not have exactly N elements is rejected.
 * 
 * The underlying C type is a T[], with no additional allocation. The nelem_
 * field is no longer generated, since the length is known at compile time
 * (and you can ARRAY_SIZE() if you need to get it after the fact).
 *
 * NVM_UNMARSHAL() will return ERANGE if the nvlist has an array pair with
 * matching name and type but the wrong number of elements. This is true even
 * with NVM_OPTIONAL, since that is about the _presence_ of the field, not
 * its (malformed) contents.
 *
 * With NVM_OPTIONAL, if the array is not found, NVM_UNMARSHAL() will "zero"
 * all elements (per NVM_SCALAR(K)). NVM_DEFAULT(D) does not work with
 * NVM_ARRAY_N(K, N).
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
 * The NV pair type is always DATA_TYPE_NVLIST. The underlying C type is T*,
 * a pointer to the underlying C schema type.
 *
 * #### NVM_STRUCT_ARRAY(T)
 *
 * NVM_STRUCT_ARRAY(T) is like NVM_ARRAY(K), but for NVM_STRUCT(T). The rules
 * are what you'd expect - the array is walked and the nvlist or struct inside
 * is processed recursively.
 *
 * The NV pair type is always DATA_TYPE_NVLIST_ARRAY. The underlying C type
 * is T**. Like NVM_ARRAY(K), a nelem_<field-name> field is generated and
 * managed.
 *
 * There is currently no NVM_STRUCT_ARRAY_N(T, N).
 */

/* ========== */

/*
 * Field kinds. Each maps to a NV pair type and a C field type in
 * nvm_marshal()/nvm_unmarshal().
 */
typedef enum {
	/* NVM_FLAG */
	NVMK_FLAG,

	/* NVM_SCALAR */
	NVMK_BOOLEAN,
	NVMK_BYTE,
	NVMK_INT8,
	NVMK_UINT8,
	NVMK_INT16,
	NVMK_UINT16,
	NVMK_INT32,
	NVMK_UINT32,
	NVMK_INT64,
	NVMK_UINT64,
	NVMK_STRING,
	NVMK_HRTIME,
#ifndef _KERNEL
	NVMK_DOUBLE,
#endif
	NVMK_NVLIST,

	/* NVM_ARRAY, NVM_ARRAY_N */
	NVMK_BOOLEAN_ARRAY,
	NVMK_BYTE_ARRAY,
	NVMK_INT8_ARRAY,
	NVMK_UINT8_ARRAY,
	NVMK_INT16_ARRAY,
	NVMK_UINT16_ARRAY,
	NVMK_INT32_ARRAY,
	NVMK_UINT32_ARRAY,
	NVMK_INT64_ARRAY,
	NVMK_UINT64_ARRAY,
	NVMK_STRING_ARRAY,
	NVMK_NVLIST_ARRAY,

	/* NVM_STRUCT */
	NVMK_STRUCT,

	/* NVM_STRUCT_ARRAY */
	NVMK_STRUCT_ARRAY,
} nvm_kind_t;

/* Field flags. Modifies processing in various ways. */

/* Optional field, set has_, zero on not found instead of ENOENT. */
#define	NVMF_OPTIONAL	(1<<0)

/* Optional field with default, use nvmf_default as the not-found value. */
#define	NVMF_DEFAULT	(1<<1)

/* Array is fixed length; length is in nvmf_elem_size, enforce length checks. */
#define	NVMF_ARRAY_N	(1<<2)

/* Map field, nvmf_kind is the value type, nvmf_elem_size is for pair elem. */
#define	NVMF_MAP	(1<<3)

/*
 * Each row in NVM_SCHEMA creates a nvm_field_t. These are filled by the
 * _NVM_DESC_* macros below (eg _NVM_DESC_SCALAR etc); field order matters!
 */
typedef struct nvm_desc nvm_desc_t;
typedef struct nvm_field {
	/* Pair name, kind and flags, see above. */
	const char	*nvmf_name;
	nvm_kind_t	nvmf_kind;
	uint32_t	nvmf_flags;

	/* Offsets to fields in the generated struct. */
	size_t		nvmf_offset;		/* data field */
	size_t		nvmf_nelem_offset;	/* nelem_ field, NVM_ARRAY */
	size_t		nvmf_has_offset;	/* has_ field, NVM_OPTIONAL */

	/*
	 * For NVM_STRUCT/NVM_STRUCT_ARRAY, size of the schema type and
	 * pointer to description, so we can allocate it and recurse into it.
	 *
	 * For NVM_ARRAY_N, nvmf_elem_size holds the expected number of
	 * elements.
	 */
	size_t		nvmf_elem_size;
	const nvm_desc_t *nvmf_sub;

	/* Default value for NVM_DEFAULT */
	intptr_t	nvmf_default;
} nvm_field_t;

/* Schema description. */
struct nvm_desc {
	/* Pointer to field array and number of fields in it. */
	const nvm_field_t	*nvmd_fields;
	size_t			nvmd_nfields;

	/* Size of the struct, for allocations. */
	size_t			nvmd_sz;
};

/* ========== */

/*
 * Scalar type mappings. Technically, these are a single copyable variable in
 * an nvpair value slot that do not require any allocations. Strings and "raw"
 * nvlists are here because they are treated the same way; eg
 * fnvlist_lookup_string() just returns a pointer into the existing nvlist
 * data.
 *
 * For any K we get from the user, we can create one of three tokens:
 *
 * - _NVM_CTYPE_##K: the C type of the struct field
 * - _NVM_KIND_##K: the nvm_kind_t for the scalar form
 * - _NVM_KIND_ARRAY_##K: the nvm_kind_t for the array form (where present)
 *
 * Note also the special case of _NVM_CTYPE_ARRAY_##K, for when the scalar
 * and array ctype differ. Since we only have case of this, we use a macro
 * selector rather than write all the other types out twice.
 */
#define	_NVM_KIND_FLAG		NVMK_FLAG

#define	_NVM_CTYPE_BOOLEAN	boolean_t
#define	_NVM_KIND_BOOLEAN	NVMK_BOOLEAN
#define	_NVM_KIND_ARRAY_BOOLEAN	NVMK_BOOLEAN_ARRAY

#define	_NVM_CTYPE_BYTE		uchar_t
#define	_NVM_KIND_BYTE		NVMK_BYTE
#define	_NVM_KIND_ARRAY_BYTE	NVMK_BYTE_ARRAY

#define	_NVM_CTYPE_INT8		int8_t
#define	_NVM_KIND_INT8		NVMK_INT8
#define	_NVM_KIND_ARRAY_INT8	NVMK_INT8_ARRAY

#define	_NVM_CTYPE_UINT8	uint8_t
#define	_NVM_KIND_UINT8		NVMK_UINT8
#define	_NVM_KIND_ARRAY_UINT8	NVMK_UINT8_ARRAY

#define	_NVM_CTYPE_INT16	int16_t
#define	_NVM_KIND_INT16		NVMK_INT16
#define	_NVM_KIND_ARRAY_INT16	NVMK_INT16_ARRAY

#define	_NVM_CTYPE_UINT16	uint16_t
#define	_NVM_KIND_UINT16	NVMK_UINT16
#define	_NVM_KIND_ARRAY_UINT16	NVMK_UINT16_ARRAY

#define	_NVM_CTYPE_INT32	int32_t
#define	_NVM_KIND_INT32		NVMK_INT32
#define	_NVM_KIND_ARRAY_INT32	NVMK_INT32_ARRAY

#define	_NVM_CTYPE_UINT32	uint32_t
#define	_NVM_KIND_UINT32	NVMK_UINT32
#define	_NVM_KIND_ARRAY_UINT32	NVMK_UINT32_ARRAY

#define	_NVM_CTYPE_INT64	int64_t
#define	_NVM_KIND_INT64		NVMK_INT64
#define	_NVM_KIND_ARRAY_INT64	NVMK_INT64_ARRAY

#define	_NVM_CTYPE_UINT64	uint64_t
#define	_NVM_KIND_UINT64	NVMK_UINT64
#define	_NVM_KIND_ARRAY_UINT64	NVMK_UINT64_ARRAY

#define	_NVM_CTYPE_STRING	const char *
#define	_NVM_KIND_STRING	NVMK_STRING
#define	_NVM_CTYPE_ARRAY_STRING	_NVM_OVERRIDE(char *)
#define	_NVM_KIND_ARRAY_STRING	NVMK_STRING_ARRAY

#define	_NVM_CTYPE_HRTIME	hrtime_t
#define	_NVM_KIND_HRTIME	NVMK_HRTIME
/* No NV pair type for hrtime array */
 
#ifndef _KERNEL
#define	_NVM_CTYPE_DOUBLE	double
#define	_NVM_KIND_DOUBLE	NVMK_DOUBLE
/* No NV pair type for double array */
#endif

#define	_NVM_CTYPE_NVLIST	nvlist_t *
#define	_NVM_KIND_NVLIST	NVMK_NVLIST
#define	_NVM_KIND_ARRAY_NVLIST	NVMK_NVLIST_ARRAY

/* ========== */

/*
 * "Pair" types. These are for NVM_SET and NVM_MAP, which produce an array
 * of key/value pairs (or in the case of NVM_SET, an array of strings).
 */

#define	_NVM_PTYPE_FLAG		const char *
#define	_NVM_PTYPE_BOOLEAN	nvm_pair_boolean_t
#define	_NVM_PTYPE_BYTE		nvm_pair_byte_t
#define	_NVM_PTYPE_INT8		nvm_pair_int8_t
#define	_NVM_PTYPE_UINT8	nvm_pair_uint8_t
#define	_NVM_PTYPE_INT16	nvm_pair_int16_t
#define	_NVM_PTYPE_UINT16	nvm_pair_uint16_t
#define	_NVM_PTYPE_INT32	nvm_pair_int32_t
#define	_NVM_PTYPE_UINT32	nvm_pair_uint32_t
#define	_NVM_PTYPE_INT64	nvm_pair_int64_t
#define	_NVM_PTYPE_UINT64	nvm_pair_uint64_t
#define	_NVM_PTYPE_STRING	nvm_pair_string_t
#define	_NVM_PTYPE_HRTIME	nvm_pair_hrtime_t
#ifndef _KERNEL
#define	_NVM_PTYPE_DOUBLE	nvm_pair_double_t
#endif
#define	_NVM_PTYPE_NVLIST	nvm_pair_nvlist_t

typedef struct { const char *name; boolean_t value; }   _NVM_PTYPE_BOOLEAN;
typedef struct { const char *name; uchar_t value; }     _NVM_PTYPE_BYTE;
typedef struct { const char *name; int8_t value; }      _NVM_PTYPE_INT8;
typedef struct { const char *name; uint8_t value; }     _NVM_PTYPE_UINT8;
typedef struct { const char *name; int16_t value; }     _NVM_PTYPE_INT16;
typedef struct { const char *name; uint16_t value; }    _NVM_PTYPE_UINT16;
typedef struct { const char *name; int32_t value; }     _NVM_PTYPE_INT32;
typedef struct { const char *name; uint32_t value; }    _NVM_PTYPE_UINT32;
typedef struct { const char *name; int64_t value; }     _NVM_PTYPE_INT64;
typedef struct { const char *name; uint64_t value; }    _NVM_PTYPE_UINT64;
typedef struct { const char *name; const char *value; } _NVM_PTYPE_STRING;
typedef struct { const char *name; hrtime_t value; }    _NVM_PTYPE_HRTIME;
#ifndef _KERNEL
typedef struct { const char *name; double value; }      _NVM_PTYPE_DOUBLE;
#endif
typedef struct { const char *name; nvlist_t *value; }   _NVM_PTYPE_NVLIST;

/* ========== */

/*
 * Shapes. A "shape" is our internal name for the "kind" or "type" of the
 * field, since as you see, those terms are somewhat overloaded! The shape is
 * something like the "group" of kinds that share an implementation:
 *
 * | Kind                | Shape        |
 * | ------------------- | ------------ |
 * | NVM_FLAG            | FLAG         |
 * | NVM_SCALAR(K)       | SCALAR       |
 * | NVM_ARRAY(K)        | ARRAY        |
 * | NVM_ARRAY_N(K)      | ARRAY_N      |
 * | NVM_STRUCT(T)       | STRUCT       |
 * | NVM_STRUCT_ARRAY(T) | STRUCT_ARRAY |
 *
 * At minimum, each shape is required to define three macros:
 *
 * - _NVM_SHAPE_##kind provides the mapping in the table above. This is used
 *   by the _NVM_MEMBER_ONE and NV_DESC_ONE dispatchers to find their way to...
 *
 * - _NVM_MEMBER_##shape(name, kind, field, req, def) is the generator for
 *   for the struct members for the wanted field. It emits the literal C text
 *   that form the body of the struct.
 *
 * - _NVM_desc_##shape(T, name, kind, field, req, def) is the generator for
 *   the nvm_field_t that describes this field. It emits a single braced row
 *   for inclusion in the fields array.
 *
 * See NVM_FLAG for the simplest possible version. The rest get complicated
 * because there's a lot of variations to deal with and a lot shared fragments,
 * but that's the basics.
 */

/*
 * NVM_FLAG. A straight boolean field. No notion of "presence", the value _is_
 * the presence.
 */
#define	_NVM_SHAPE_NVM_FLAG	FLAG

#define	_NVM_MEMBER_FLAG(name, kind, field, ...) \
	_NVM_CTYPE_BOOLEAN field;

#define	_NVM_DESC_FLAG(T, name, kind, field, ...) \
	{ name, _NVM_KIND_FLAG, B_FALSE, \
	    offsetof(T, field), 0, 0, 0, NULL, 0 },

/*
 * NVM_SCALAR(K). Constructed directly out of the scalar type mappings above.
 */
#define	_NVM_SHAPE_NVM_SCALAR(K)	SCALAR

/* Map from schema kind to scalar type mappings. */
#define	_NVM_CTYPE_NVM_SCALAR(K)	_NVM_CTYPE_##K
#define	_NVM_KIND_NVM_SCALAR(K)		_NVM_KIND_##K

#define	_NVM_MEMBER_SCALAR(name, kind, field, req, def) \
	_NVM_CTYPE_##kind field; \
	_NVM_HAS_FLAG(field, req)

#define	_NVM_DESC_SCALAR(T, name, kind, field, req, def) \
	{ name, _NVM_KIND_##kind, \
	    _NVM_REQ_FLAG(req) | _NVM_DEF_FLAG(def), \
	    offsetof(T, field), 0, _NVM_HAS_OFFSET(T, field, req), 0, NULL, \
	    _NVM_DEF_VALUE(def) },

/*
 * NVM_ARRAY(K). Like NVM_SCALAR, but uses the array types.
 */
#define	_NVM_SHAPE_NVM_ARRAY(K)		ARRAY

/*
 * If an override is listed for the array version of a type, use it. We don't
 * strictly need it but it makes things tractable when working with marshaled
 * structs and nvlist_() calls in the same code without requiring difficult
 * casts.
 */
#define	_NVM_CTYPE_NVM_ARRAY(K) \
	_NVM_SELECT_OVERRIDE(_NVM_CTYPE_ARRAY_##K, _NVM_CTYPE_##K)

#define	_NVM_KIND_NVM_ARRAY(K)	_NVM_KIND_ARRAY_##K

#define	_NVM_MEMBER_ARRAY(name, kind, field, req, def) \
	_NVM_CTYPE_##kind *field; \
	uint_t nelem_##field; \
	_NVM_HAS_FLAG(field, req)

#define	_NVM_DESC_ARRAY(T, name, kind, field, req, def) \
	{ name, _NVM_KIND_##kind, _NVM_REQ_FLAG(req), \
	    offsetof(T, field), offsetof(T, nelem_##field), \
	    _NVM_HAS_OFFSET(T, field, req), 0, NULL, \
	    _NVM_DEF_INVALID(def) },

/*
 * NVM_ARRAY_N(K, N). Like NVM_ARRAY, but enforces fixed-size array.
 */
#define	_NVM_SHAPE_NVM_ARRAY_N(K, N)	ARRAY_N

/* Follow NVM_ARRAY(K) on types. */
#define	_NVM_CTYPE_NVM_ARRAY_N(K, N)	_NVM_CTYPE_NVM_ARRAY(K)
#define	_NVM_KIND_NVM_ARRAY_N(K, N)	_NVM_KIND_NVM_ARRAY(K)

/* Extract element count for the field initialiser. */
#define _NVM_NELEMS_NVM_ARRAY_N(K, N)	N

/* Create the array embedded in the struct. */
#define	_NVM_MEMBER_ARRAY_N(name, kind, field, req, def) \
	_NVM_CTYPE_##kind field[_NVM_NELEMS_##kind]; \
	_NVM_HAS_FLAG(field, req)

/* Like NVM_ARRAY, but adding NVMF_ARRAY_N flag and the element count. */
#define	_NVM_DESC_ARRAY_N(T, name, kind, field, req, def) \
	{ name, _NVM_KIND_##kind, _NVM_REQ_FLAG(req)|NVMF_ARRAY_N, \
	    offsetof(T, field), _NVM_NELEMS_##kind, \
	    _NVM_HAS_OFFSET(T, field, req), sizeof (_NVM_CTYPE_##kind), NULL, \
	    _NVM_DEF_INVALID(def) },

/*
 * NVM_STRUCT(T). T must have been declared earlier via NVM_SCHEMA(T, ...) so
 * that its field description already exists. Beyond a couple of extra field
 * types, the field description is like any other scalar type.
 */
#define	_NVM_SHAPE_NVM_STRUCT(T)	STRUCT

#define	_NVM_CTYPE_NVM_STRUCT(T)	T

#define	_NVM_MEMBER_STRUCT(name, kind, field, req, def) \
	_NVM_MEMBER_SCALAR(name, kind, field, req, def)

#define	_NVM_DESC_STRUCT(T, name, kind, field, req, def) \
	{ name, NVMK_STRUCT, _NVM_REQ_FLAG(req), \
	    offsetof(T, field), 0, _NVM_HAS_OFFSET(T, field, req), \
	    sizeof (_NVM_CTYPE_##kind), \
	    &_NVM_CONCAT(_NVM_CONCAT(_nvm__, _NVM_CTYPE_##kind), __desc), \
	    _NVM_DEF_INVALID(def) },

/*
 * NVM_STRUCT_ARRAY(T). Same as NVM_STRUCT(T), but following NVM_ARRAY instead
 * of NVM_SCALAR.
 */
#define	_NVM_SHAPE_NVM_STRUCT_ARRAY(T)	STRUCT_ARRAY

#define	_NVM_CTYPE_NVM_STRUCT_ARRAY(T)	T

#define	_NVM_MEMBER_STRUCT_ARRAY(name, kind, field, req, def) \
	_NVM_MEMBER_ARRAY(name, kind, field, req, def)

#define	_NVM_DESC_STRUCT_ARRAY(T, name, kind, field, req, def) \
	{ name, NVMK_STRUCT_ARRAY, _NVM_REQ_FLAG(req), \
	    offsetof(T, field), offsetof(T, nelem_##field), \
	    _NVM_HAS_OFFSET(T, field, req), \
	    sizeof (_NVM_CTYPE_##kind), \
	    &_NVM_CONCAT(_NVM_CONCAT(_nvm__, _NVM_CTYPE_##kind), __desc), \
	    _NVM_DEF_INVALID(def) },

/*
 * NVM_MAP(K).
 */
#define	_NVM_SHAPE_NVM_MAP(K)		MAP

#define	_NVM_CTYPE_NVM_MAP(K)		_NVM_CTYPE_##K
#define	_NVM_PTYPE_NVM_MAP(K)		_NVM_PTYPE_##K

#define	_NVM_KIND_NVM_MAP(K)		_NVM_KIND_##K

#define	_NVM_MEMBER_MAP(name, kind, field, req, def) \
	_NVM_PTYPE_##kind *field; \
	uint_t nelem_##field; \
	_NVM_HAS_FLAG(field, req)

#define	_NVM_DESC_MAP(T, name, kind, field, req, def) \
	{ name, _NVM_KIND_##kind, _NVM_REQ_FLAG(req)|NVMF_MAP, \
	    offsetof(T, field), offsetof(T, nelem_##field), \
	    _NVM_HAS_OFFSET(T, field, req), sizeof (_NVM_PTYPE_##kind), \
	    NULL, _NVM_DEF_INVALID(def) },

/*
 * NVM_SET
 */
#define	_NVM_SHAPE_NVM_SET		SET

#define	_NVM_CTYPE_NVM_SET		_NVM_CTYPE_STRING
#define	_NVM_KIND_NVM_SET		_NVM_KIND_FLAG

#define	_NVM_MEMBER_SET(name, kind, field, req, def) \
	_NVM_CTYPE_##kind *field; \
	uint_t nelem_##field; \
	_NVM_HAS_FLAG(field, req)

#define	_NVM_DESC_SET(T, name, kind, field, req, def) \
	{ name, _NVM_KIND_##kind, _NVM_REQ_FLAG(req)|NVMF_MAP, \
	    offsetof(T, field), offsetof(T, nelem_##field), \
	    _NVM_HAS_OFFSET(T, field, req), sizeof (_NVM_CTYPE_##kind), \
	    NULL, _NVM_DEF_INVALID(def) },

/* ========== */

/*
 * Requirement rules. We expand the single arg we get from the caller into two
 * args for the field tuple, a required/optional flag, and the default value
 * spec. These in turn are used as tokens to compute the actual output later
 * depending on which kind is calling.
 *
 * This is complicated a little by the req-rule being optional, and by not
 * being keyed on kind like everything else.
 */

/*
 * Raw expansion for whatever the caller provides in the fourth arg in the
 * field tuple (if anything).
 */
#define	NVM_REQUIRED		_NVM_REQUIRED, 0
#define	NVM_OPTIONAL		_NVM_OPTIONAL, 0
#define	NVM_DEFAULT(D)		_NVM_OPTIONAL, _NVM_DEFAULT(D)

/*
 * Entry points for _NVM_MEMBER_ONE__ and _NVM_DESC_ONE__ below. Since their
 * req-rule value may not be provided, we recieve the complete remaining
 * args, which will be either the two args expanded from above, or empty list.
 */

/*
 * Get the required/optional flag from the pair. Default is _NVM_REQUIRED.
 * This is the 'req' token that will be used to form _NVM_HAS_FLAG_##req etc
 * below.
 */
#define	_NVM_REQ_(D)			_NVM_REQUIRED
#define	_NVM_REQ__NVM_REQUIRED(D)	_NVM_REQUIRED
#define	_NVM_REQ__NVM_OPTIONAL(D)	_NVM_OPTIONAL
#define	_NVM_REQ(req, ...)		_NVM_REQ_##req(__VA_ARGS__)

/*
 * Get the default value spec from the pair. Default is 0 (because
 * _NVM_REQUIRED has no default. This is the 'def' token that will be used to
 * form _NVM_DEF_FLAG_##def etc below.
 */
#define	_NVM_DEF_(D)			0
#define	_NVM_DEF__NVM_REQUIRED(D)	D
#define	_NVM_DEF__NVM_OPTIONAL(D)	D
#define	_NVM_DEF(req, ...)		_NVM_DEF_##req(__VA_ARGS__)

/*
 * Expansions for optional fields and flags. Dispatched on 'req', which will
 * always be set from the expansions above.
 */

/* Flag for nvmf_flags; enables the optional field code. */
#define	_NVM_REQ_FLAG__NVM_REQUIRED	0
#define	_NVM_REQ_FLAG__NVM_OPTIONAL	NVMF_OPTIONAL
#define	_NVM_REQ_FLAG(req)		_NVM_REQ_FLAG_##req

/* Struct member for the has_ field itself, when NVM_OPTIONAL in use. */
#define	_NVM_HAS_FLAG__NVM_REQUIRED(field)
#define	_NVM_HAS_FLAG__NVM_OPTIONAL(field)	boolean_t has_##field;
#define	_NVM_HAS_FLAG(field, req)		_NVM_HAS_FLAG_##req(field)

/* Offset of the the has_ field for nvmf_has_offset. */
#define	_NVM_HAS_OFFSET__NVM_REQUIRED(T, field)	0
#define	_NVM_HAS_OFFSET__NVM_OPTIONAL(T, field)	offsetof(T, has_##field)
#define	_NVM_HAS_OFFSET(T, field, req)		_NVM_HAS_OFFSET_##req(T, field)

/*
 * Expansions for the default value and flags. 'def' here is either '0' or
 * _NVM_DEFAULT(D), from the expansions above. 0 means that NVM_DEFAULT was not
 * supplied by the caller.
 */

/* Flag for nvmf_flags; enables alternate default processing. */
#define	_NVM_DEF_FLAG_0			0
#define	_NVM_DEF_FLAG__NVM_DEFAULT(D)	NVMF_DEFAULT
#define	_NVM_DEF_FLAG(def)		_NVM_DEF_FLAG_##def

/*
 * Value for nvmf_default; explicit cast to intptr_t since it could be
 * anything.
 */
#define	_NVM_DEF_VALUE_0		0
#define	_NVM_DEF_VALUE__NVM_DEFAULT(D)	((intptr_t)D)
#define	_NVM_DEF_VALUE(def)		_NVM_DEF_VALUE_##def

/*
 * Alternate value expansion for field shapes that can't use NVM_DEFAULT. If
 * it wasn't supplied, set it 0, move on. If it was, set it to the address of
 * a non-existing symbol; this will cause the link to fail.
 */
#define	_NVM_DEF_INVALID_0			_NVM_DEF_VALUE_0
#define	_NVM_DEF_INVALID__NVM_DEFAULT(D) \
	&__nvm_default_value_can_only_be_used_with_scalar_types
#define	_NVM_DEF_INVALID(def)			_NVM_DEF_INVALID_##def

/* ========== */

/*
 * Field tuple dispatch. _NVM_MEMBER_ONE and _NVM_DESC_ONE are called once per
 * field tuple from NVM_SCHEMA. The single arg is either the full tuple
 * including the parens, or the empty token (for the "trailing comma" case).
 *
 * _NVM_CALL_TUPLE tests the arg, and if its a tuple, calls the actual
 * implementing function with the tuple contents as args. A call with the empty
 * token is discarded.
 *
 * In the actual function, _NVM_SHAPE_##kind is computing the shape to dispatch
 * to, then the member or desc function name is built.  name, kind and field
 * are passed through directly, but because the requirement rule is optional,
 * we can't assume its presence; we split it into separate "requirement" and
 * "default" here so its all ready to go when the receiver gets it.
 *
 * _NVM_DESC_##shape gets an extra arg, T, the actual C struct name, so it can
 * call offsetof() on its fields.
 */

#define	_NVM_MEMBER_ONE(arg)	_NVM_CALL_TUPLE(_NVM_MEMBER_ONE_, arg)
#define	_NVM_MEMBER_ONE_(name, kind, field, ...) \
	_NVM_CONCAT(_NVM_MEMBER_, _NVM_SHAPE_##kind) \
	    (name, kind, field, _NVM_REQ(__VA_ARGS__), _NVM_DEF(__VA_ARGS__))

#define	_NVM_DESC_ONE(T, arg)	_NVM_CALL_TUPLE_ARG(_NVM_DESC_ONE_, T, arg)
#define	_NVM_DESC_ONE_(T, name, kind, field, ...) \
	_NVM_CONCAT(_NVM_DESC_, _NVM_SHAPE_##kind) \
	    (T, name, kind, field, _NVM_REQ(__VA_ARGS__), _NVM_DEF(__VA_ARGS__))

/* ========== */

/* Public interface. */

/*
 * NVM_SCHEMA. Creates a typedef struct T, and the fields and description data.
 * The body of each is filled out by looping over the field tuples with a
 * different target function for each.
 */
#define	NVM_SCHEMA(T, ...) \
typedef struct T { \
	_NVM_FOR_EACH(_NVM_MEMBER_ONE, __VA_ARGS__) \
} T; \
static const nvm_field_t __maybe_unused _nvm__##T##__fields[] = { \
	_NVM_FOR_EACH_ARG(_NVM_DESC_ONE, T, __VA_ARGS__) \
}; \
static const nvm_desc_t __maybe_unused _nvm__##T##__desc = { \
	_nvm__##T##__fields, ARRAY_SIZE(_nvm__##T##__fields), sizeof (T) \
}

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
#define _NVM_OVERRIDE(v)		, v
#define _NVM_SELECT_OVERRIDE(...)	_NVM_SELECT_3(, __VA_ARGS__)

/*
 * Determine if the passed arg is a tuple. If it is, the expansion will be
 * _NVM_PROBE_TUPLE_(arg), which expands to ', 1', that is, a two-arg
 * replacement.  If not, the expansion is just '_NVM_PROBE_TUPLE_ <whatever>',
 * a single argument.
 */
#define	_NVM_PROBE_TUPLE(arg)	_NVM_PROBE_TUPLE_ arg
#define	_NVM_PROBE_TUPLE_(...)	, 1

/*
 * Return a function name depending on whether or not the arg is a tuple. If
 * it is, returns the passed in function; if not, _NVM_DISCARD, an
 * arg-swallowing no-op.
 */
#define	_NVM_IF_TUPLE(arg, fn) \
	_NVM_CALL(_NVM_SELECT_3, _NVM_PROBE_TUPLE(arg), fn, _NVM_DISCARD)

/* Consume args, do nothing. */
#define	_NVM_DISCARD(...)

/* If the tuple arg is in fact a tuple, call the given function with the
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
#define _NVM_ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, \
                  _14, _15, _16, _17, _18, _19, _20, N, ...) N

#define _NVM_RSEQ_N() \
        20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

#define _NVM_NARG_(...) _NVM_ARG_N(__VA_ARGS__)
#define _NVM_NARG(...)  _NVM_NARG_(__VA_ARGS__, _NVM_RSEQ_N())

/*
 * Call ladder to implement foreach. Calls a function, then passes the
 * remainder through to the next step until all arguments are exhausted. Note
 * that we have our own deferred eval call here rather than using _NVM_CALL,
 * because almost certainly the caller is using that in their call pipeline and
 * using it ourselves will break the recursion.
 */

#define _NVM_FE_CALL(fn, ...)	fn(__VA_ARGS__)

#define _NVM_FE_1(fn, arg, x)       fn(arg, x)
#define _NVM_FE_2(fn, arg, x, ...)  fn(arg, x) _NVM_FE_1(fn, arg, __VA_ARGS__)
#define _NVM_FE_3(fn, arg, x, ...)  fn(arg, x) _NVM_FE_2(fn, arg, __VA_ARGS__)
#define _NVM_FE_4(fn, arg, x, ...)  fn(arg, x) _NVM_FE_3(fn, arg, __VA_ARGS__)
#define _NVM_FE_5(fn, arg, x, ...)  fn(arg, x) _NVM_FE_4(fn, arg, __VA_ARGS__)
#define _NVM_FE_6(fn, arg, x, ...)  fn(arg, x) _NVM_FE_5(fn, arg, __VA_ARGS__)
#define _NVM_FE_7(fn, arg, x, ...)  fn(arg, x) _NVM_FE_6(fn, arg, __VA_ARGS__)
#define _NVM_FE_8(fn, arg, x, ...)  fn(arg, x) _NVM_FE_7(fn, arg, __VA_ARGS__)
#define _NVM_FE_9(fn, arg, x, ...)  fn(arg, x) _NVM_FE_8(fn, arg, __VA_ARGS__)
#define _NVM_FE_10(fn, arg, x, ...) fn(arg, x) _NVM_FE_9(fn, arg, __VA_ARGS__)
#define _NVM_FE_11(fn, arg, x, ...) fn(arg, x) _NVM_FE_10(fn, arg, __VA_ARGS__)
#define _NVM_FE_12(fn, arg, x, ...) fn(arg, x) _NVM_FE_11(fn, arg, __VA_ARGS__)
#define _NVM_FE_13(fn, arg, x, ...) fn(arg, x) _NVM_FE_12(fn, arg, __VA_ARGS__)
#define _NVM_FE_14(fn, arg, x, ...) fn(arg, x) _NVM_FE_13(fn, arg, __VA_ARGS__)
#define _NVM_FE_15(fn, arg, x, ...) fn(arg, x) _NVM_FE_14(fn, arg, __VA_ARGS__)
#define _NVM_FE_16(fn, arg, x, ...) fn(arg, x) _NVM_FE_15(fn, arg, __VA_ARGS__)
#define _NVM_FE_17(fn, arg, x, ...) fn(arg, x) _NVM_FE_16(fn, arg, __VA_ARGS__)
#define _NVM_FE_18(fn, arg, x, ...) fn(arg, x) _NVM_FE_17(fn, arg, __VA_ARGS__)
#define _NVM_FE_19(fn, arg, x, ...) fn(arg, x) _NVM_FE_18(fn, arg, __VA_ARGS__)
#define _NVM_FE_20(fn, arg, x, ...) fn(arg, x) _NVM_FE_19(fn, arg, __VA_ARGS__)

/* Foreach entry point, with arg to pass as first arg to call. */
#define _NVM_FOR_EACH_ARG(fn, arg, ...) \
    _NVM_CONCAT(_NVM_FE_, _NVM_NARG(__VA_ARGS__))(fn, arg, __VA_ARGS__)

/* No-arg version, bouncing through _NVM_FE_CALL to consume the arg. */
#define _NVM_FOR_EACH(fn, ...) \
    _NVM_FOR_EACH_ARG(_NVM_FE_CALL, fn, __VA_ARGS__)


/*
 * XXX TODO:
 *	- alternate storage type/class, eg NVM_SCALAR(INT32) +
 *	  NVM_REALTYPE(dmu_objset_type_t). int32 for all conversions, but is
 *	  the realtype in the generated struct, removing need for casts
 *
 *	- hmm, we could make allocating nvlist_unmarshal() and even
 *	  fnvlist_unmarshal() if we were happy to allocate the return object.
 *	- a struct with no fields doesn't work (and isn't meaningful C
 *	  anyway) - an ioctl with an empty schema just doesn't need a
 *	  marshalled type at all.
 *	- it would be nice to be able to provide further constraints, eg
 *	  value between x and y, or if the realtype is an enum, a valid
 *	  value for it.
 *	- nvmf_nelem_offset reused as NVM_ARRAY_N elem count, consider rename
 *	- could NVM_ARRAY_N just be NVM_ARRAY, and the second type makes the
 *	  difference?
 */

#endif
