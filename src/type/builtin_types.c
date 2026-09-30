/**
 * @file builtin_types.c
 * @brief Builtin type registration
 *
 * Registers builtin CK2-like parameter value types in the GUID-based type system.
 */

#include "type_value_internal.h"
#include "type/nmo_type_system.h"
#include "core/nmo_math.h"
#include "core/nmo_color.h"
#include "core/nmo_array.h"
#include "core/nmo_hash.h"
#include "core/nmo_error.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_object_guids.h"
#include "type/nmo_param_guids.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdalign.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Type Registration
 * ============================================================================ */
nmo_status_t nmo_register_builtin_types(nmo_type_registry_t *type_registry);

static nmo_status_t nmo_add_builtin_alias(
    nmo_type_registry_t *type_registry,
    nmo_guid_t type_guid,
    const char *alias)
{
    if (!type_registry || !alias) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL type_registry or alias");
    }

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(type_registry, type_guid);
    if (!type) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "builtin alias target type not found");
    }

    return nmo_type_registry_add_name_alias(type_registry, type->id, alias);
}

#define BUILTIN_POD_FLAGS \
    (NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD)
#define BUILTIN_NO_BASE {0, 0}

#define BUILTIN_TYPE_EX(GUID, NAME, SIZE, ALIGN, CATEGORY, FLAGS, BASE, DESC, FIELDS, FIELD_COUNT, VT) \
    { \
        .guid = GUID##_INIT, \
        .id = NMO_TYPE_ID_INVALID, \
        .category = (CATEGORY), \
        .flags = (FLAGS), \
        .name = (NAME), \
        .description = (DESC), \
        .base_type = BASE, \
        .size = (SIZE), \
        .alignment = (ALIGN), \
        .fields = (FIELDS), \
        .field_count = (FIELD_COUNT), \
        .vtable = &nmo_builtin_vtable_##VT, \
    }

/* A type laid out exactly like the C type CTYPE, with the common POD flags. */
#define BUILTIN_TYPE(GUID, NAME, CTYPE, CATEGORY, BASE, DESC, VT) \
    { \
        .guid = GUID##_INIT, \
        .id = NMO_TYPE_ID_INVALID, \
        .category = (CATEGORY), \
        .flags = BUILTIN_POD_FLAGS, \
        .name = (NAME), \
        .description = (DESC), \
        .base_type = BASE, \
        .size = sizeof(CTYPE), \
        .alignment = alignof(CTYPE), \
        .vtable = &nmo_builtin_vtable_##VT, \
    }

#define BUILTIN_FIELD(GUID, TYPE, MEMBER) \
    { \
        .name = #MEMBER, \
        .type_guid = GUID##_INIT, \
        .offset = offsetof(TYPE, MEMBER), \
        .size = sizeof(((TYPE *)0)->MEMBER), \
        .flags = NMO_FIELD_REQUIRED, \
    }

static const nmo_type_field_t rect_fields[] = {
    BUILTIN_FIELD(CKPGUID_FLOAT, nmo_rect_t, left),
    BUILTIN_FIELD(CKPGUID_FLOAT, nmo_rect_t, top),
    BUILTIN_FIELD(CKPGUID_FLOAT, nmo_rect_t, right),
    BUILTIN_FIELD(CKPGUID_FLOAT, nmo_rect_t, bottom),
};

static const nmo_type_field_t euler_fields[] = {
    BUILTIN_FIELD(CKPGUID_FLOAT, nmo_eulerangles_t, x),
    BUILTIN_FIELD(CKPGUID_FLOAT, nmo_eulerangles_t, y),
    BUILTIN_FIELD(CKPGUID_FLOAT, nmo_eulerangles_t, z),
};

static const nmo_type_field_t box_fields[] = {
    BUILTIN_FIELD(CKPGUID_VECTOR, nmo_box_t, min),
    BUILTIN_FIELD(CKPGUID_VECTOR, nmo_box_t, max),
};

#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

/* Registration order is significant: it fixes the runtime type ids. */
static const nmo_type_descriptor_t builtin_types[] = {
    BUILTIN_TYPE_EX(CKPGUID_NONE, "none", 0, 1,
                    NMO_TYPE_CATEGORY_SCALAR | NMO_TYPE_CATEGORY_HIDDEN, 0,
                    BUILTIN_NO_BASE, "No type / placeholder", NULL, 0, none),
    /* Variable-sized raw buffer; only fixed-size metadata is described here. */
    BUILTIN_TYPE_EX(CKPGUID_VOIDBUF, "voidbuf", 0, 1,
                    NMO_TYPE_CATEGORY_SCALAR, NMO_TYPE_FLAG_SERIALIZABLE,
                    BUILTIN_NO_BASE, "Raw variable-sized buffer", NULL, 0, voidbuf),

    BUILTIN_TYPE(CKPGUID_INT, "int", int32_t, NMO_TYPE_CATEGORY_SCALAR,
                 BUILTIN_NO_BASE, NULL, int),
    BUILTIN_TYPE(CKPGUID_FLOAT, "float", float, NMO_TYPE_CATEGORY_SCALAR,
                 BUILTIN_NO_BASE, NULL, float),

    /* CK2 derived scalar types */
    BUILTIN_TYPE(CKPGUID_TIME, "Time", float, NMO_TYPE_CATEGORY_SCALAR,
                 CKPGUID_FLOAT_INIT, "Time (milliseconds)", time),
    BUILTIN_TYPE(CKPGUID_ANGLE, "angle", float, NMO_TYPE_CATEGORY_SCALAR,
                 CKPGUID_FLOAT_INIT, "Angle (float)", angle),
    BUILTIN_TYPE(CKPGUID_PERCENTAGE, "percentage", float, NMO_TYPE_CATEGORY_SCALAR,
                 CKPGUID_FLOAT_INIT, "Percentage (float)", percentage),
    /* Virtools BOOL is int-sized and derived from Integer. */
    BUILTIN_TYPE(CKPGUID_BOOL, "bool", int32_t, NMO_TYPE_CATEGORY_SCALAR,
                 CKPGUID_INT_INIT, NULL, bool),

    BUILTIN_TYPE(CKPGUID_DOUBLE, "double", double, NMO_TYPE_CATEGORY_SCALAR,
                 BUILTIN_NO_BASE, NULL, double),
    BUILTIN_TYPE(CKPGUID_INT8, "int8", int8_t, NMO_TYPE_CATEGORY_SCALAR,
                 BUILTIN_NO_BASE, NULL, int8),
    BUILTIN_TYPE(CKPGUID_UINT8, "uint8", uint8_t, NMO_TYPE_CATEGORY_SCALAR,
                 BUILTIN_NO_BASE, NULL, uint8),
    BUILTIN_TYPE(CKPGUID_INT16, "int16", int16_t, NMO_TYPE_CATEGORY_SCALAR,
                 BUILTIN_NO_BASE, NULL, int16),
    BUILTIN_TYPE(CKPGUID_UINT16, "uint16", uint16_t, NMO_TYPE_CATEGORY_SCALAR,
                 BUILTIN_NO_BASE, NULL, uint16),
    BUILTIN_TYPE(CKPGUID_UINT32, "uint32", uint32_t, NMO_TYPE_CATEGORY_SCALAR,
                 BUILTIN_NO_BASE, NULL, uint32),
    BUILTIN_TYPE(CKPGUID_CLASSID, "classid", uint32_t, NMO_TYPE_CATEGORY_SCALAR,
                 CKPGUID_UINT32_INIT, "Virtools class id (uint32)", classid),
    BUILTIN_TYPE(CKPGUID_INT64, "int64", int64_t, NMO_TYPE_CATEGORY_SCALAR,
                 BUILTIN_NO_BASE, NULL, int64),
    BUILTIN_TYPE(CKPGUID_UINT64, "uint64", uint64_t, NMO_TYPE_CATEGORY_SCALAR,
                 BUILTIN_NO_BASE, NULL, uint64),
    BUILTIN_TYPE(CKPGUID_STRING, "string", char *, NMO_TYPE_CATEGORY_SCALAR,
                 BUILTIN_NO_BASE, NULL, string),
    BUILTIN_TYPE(CKPGUID_POINTER, "pointer", void *, NMO_TYPE_CATEGORY_POINTER,
                 BUILTIN_NO_BASE, NULL, pointer),
    BUILTIN_TYPE(CKPGUID_STATECHUNK, "chunk", void *, NMO_TYPE_CATEGORY_POINTER,
                 BUILTIN_NO_BASE, NULL, chunk),
    BUILTIN_TYPE(CKPGUID_GUID, "guid", nmo_guid_t, NMO_TYPE_CATEGORY_SCALAR,
                 BUILTIN_NO_BASE, NULL, guid),
    BUILTIN_TYPE(CKPGUID_ID, "object_id", nmo_object_id_t, NMO_TYPE_CATEGORY_SCALAR,
                 BUILTIN_NO_BASE, NULL, object_id),

    /* Virtools math types */
    BUILTIN_TYPE(CKPGUID_2DVECTOR, "vector2", nmo_vector2_t, NMO_TYPE_CATEGORY_STRUCT,
                 BUILTIN_NO_BASE, NULL, vector2),
    BUILTIN_TYPE(CKPGUID_VECTOR, "vector3", nmo_vector_t, NMO_TYPE_CATEGORY_STRUCT,
                 BUILTIN_NO_BASE, NULL, vector3),
    BUILTIN_TYPE(CKPGUID_VECTOR4, "vector4", nmo_vector4_t, NMO_TYPE_CATEGORY_STRUCT,
                 BUILTIN_NO_BASE, NULL, vector4),
    BUILTIN_TYPE(CKPGUID_QUATERNION, "quaternion", nmo_quaternion_t,
                 NMO_TYPE_CATEGORY_STRUCT, BUILTIN_NO_BASE, NULL, quaternion),
    BUILTIN_TYPE(CKPGUID_MATRIX, "matrix", nmo_matrix_t, NMO_TYPE_CATEGORY_STRUCT,
                 BUILTIN_NO_BASE, NULL, matrix),
    BUILTIN_TYPE(CKPGUID_COLOR, "color", nmo_color_t, NMO_TYPE_CATEGORY_STRUCT,
                 BUILTIN_NO_BASE, NULL, color),
    BUILTIN_TYPE_EX(CKPGUID_RECT, "rect", sizeof(nmo_rect_t), alignof(nmo_rect_t),
                    NMO_TYPE_CATEGORY_STRUCT, BUILTIN_POD_FLAGS, BUILTIN_NO_BASE, NULL,
                    rect_fields, COUNT_OF(rect_fields), rect),
    /* Euler angles are derived from Vector in Virtools. */
    BUILTIN_TYPE_EX(CKPGUID_EULERANGLES, "euler_angles", sizeof(nmo_eulerangles_t),
                    alignof(nmo_eulerangles_t), NMO_TYPE_CATEGORY_STRUCT, BUILTIN_POD_FLAGS,
                    CKPGUID_VECTOR_INIT, NULL,
                    euler_fields, COUNT_OF(euler_fields), eulerangles),
    /* Bounding box: {min, max} */
    BUILTIN_TYPE_EX(CKPGUID_BOX, "box", sizeof(nmo_box_t), alignof(nmo_box_t),
                    NMO_TYPE_CATEGORY_STRUCT, BUILTIN_POD_FLAGS, BUILTIN_NO_BASE, NULL,
                    box_fields, COUNT_OF(box_fields), box),

    /* Container/meta type present in CK2 */
    BUILTIN_TYPE_EX(CKPGUID_ARRAY, "array", sizeof(nmo_array_t), alignof(nmo_array_t),
                    NMO_TYPE_CATEGORY_ARRAY | NMO_TYPE_CATEGORY_HIDDEN,
                    NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE,
                    BUILTIN_NO_BASE, "Array meta-type", NULL, 0, array),
};

/*
 * Name aliases
 *
 * The type parser is intentionally case-sensitive, and tests expect
 * legacy/uppercase spellings (e.g. INT, UINT32, STRING) and C-like
 * spellings (uint, uint32_t) to resolve.
 */
typedef struct builtin_alias {
    nmo_guid_t type_guid;
    const char *alias;
} builtin_alias_t;

static const builtin_alias_t builtin_aliases[] = {
    {CKPGUID_INT_INIT, "INT"},
    {CKPGUID_FLOAT_INIT, "FLOAT"},
    {CKPGUID_BOOL_INIT, "BOOL"},
    {CKPGUID_STRING_INIT, "STRING"},
    {CKPGUID_UINT32_INIT, "UINT32"},
    {CKPGUID_UINT64_INIT, "UINT64"},
    {CKPGUID_UINT32_INIT, "uint"},
    {CKPGUID_UINT32_INIT, "UINT"},
    {CKPGUID_UINT32_INIT, "uint32_t"},
    {CKPGUID_UINT64_INIT, "uint64_t"},
    /* size_t is platform sized. */
#if SIZE_MAX == UINT64_MAX
    {CKPGUID_UINT64_INIT, "size_t"},
#else
    {CKPGUID_UINT32_INIT, "size_t"},
#endif
    /* Virtools-style names */
    {CKPGUID_VECTOR_INIT, "VxVector3"},
    {CKPGUID_MATRIX_INIT, "VxMatrix"},
    {CKPGUID_COLOR_INIT, "VxColor"},
};

nmo_status_t nmo_register_builtin_types(nmo_type_registry_t *type_registry) {
    if (!type_registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type_registry");
    }

    for (size_t i = 0; i < COUNT_OF(builtin_types); ++i) {
        NMO_RETURN_IF_ERROR(nmo_type_registry_register(type_registry, &builtin_types[i]));
    }

    for (size_t i = 0; i < COUNT_OF(builtin_aliases); ++i) {
        NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(
            type_registry, builtin_aliases[i].type_guid, builtin_aliases[i].alias));
    }

    NMO_RETURN_OK();
}
