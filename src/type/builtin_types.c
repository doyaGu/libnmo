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
#include "object/nmo_object_guids.h"
#include "object/nmo_param_guids.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdalign.h>
#include <string.h>
#include <stdio.h>
static void nmo_patch_uint32_alias_vtable(
    nmo_type_registry_t *registry,
    nmo_guid_t guid)
{
    nmo_type_descriptor_t *type =
        (nmo_type_descriptor_t *)nmo_type_registry_find_by_guid(registry, guid);
    if (!type || type->vtable || type->size != sizeof(uint32_t)) {
        return;
    }

    type->alignment = alignof(uint32_t);
    type->vtable = &nmo_builtin_vtable_uint32;
}

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

nmo_status_t nmo_register_builtin_types(nmo_type_registry_t *type_registry) {
    if (!type_registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type_registry");
    }

    /* Register basic scalar types */

    /* CKPGUID_NONE: placeholder/no-type (hidden) */
    nmo_type_descriptor_t none_type = {
        .guid = CKPGUID_NONE,
        .name = "none",
        .size = 0,
        .alignment = 1,
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR | NMO_TYPE_CATEGORY_HIDDEN,
        .flags = 0,
        .id = NMO_TYPE_ID_INVALID,
        .description = "No type / placeholder",
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_none,
    };

    nmo_status_t result = nmo_type_registry_register(type_registry, &none_type);
    if (result != NMO_OK) {
        return result;
    }

    /* CKPGUID_VOIDBUF: variable-sized raw buffer; fixed-size metadata only */
    nmo_type_descriptor_t voidbuf_type = {
        .guid = CKPGUID_VOIDBUF,
        .name = "voidbuf",
        .size = 0,
        .alignment = 1,
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE,
        .id = NMO_TYPE_ID_INVALID,
        .description = "Raw variable-sized buffer",
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_voidbuf,
    };

    result = nmo_type_registry_register(type_registry, &voidbuf_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t int_type = {
        .guid = CKPGUID_INT,
        .name = "int",
        .size = sizeof(int32_t),
        .alignment = alignof(int32_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_int,
    };

    result = nmo_type_registry_register(type_registry, &int_type);
    if (result != NMO_OK) {
        return result;
    }

    /* Register FLOAT type */
    nmo_type_descriptor_t float_type = {
        .guid = CKPGUID_FLOAT,
        .name = "float",
        .size = sizeof(float),
        .alignment = alignof(float),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_float,
    };

    result = nmo_type_registry_register(type_registry, &float_type);
    if (result != NMO_OK) {
        return result;
    }

    /* CK2 derived scalar types */
    nmo_type_descriptor_t angle_type = {
        .guid = CKPGUID_ANGLE,
        .name = "angle",
        .size = sizeof(float),
        .alignment = alignof(float),
        .class_id = 0,
        .base_type = CKPGUID_FLOAT,
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = "Angle (float)",
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_angle,
    };

    result = nmo_type_registry_register(type_registry, &angle_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t percentage_type = {
        .guid = CKPGUID_PERCENTAGE,
        .name = "percentage",
        .size = sizeof(float),
        .alignment = alignof(float),
        .class_id = 0,
        .base_type = CKPGUID_FLOAT,
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = "Percentage (float)",
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_percentage,
    };

    result = nmo_type_registry_register(type_registry, &percentage_type);
    if (result != NMO_OK) {
        return result;
    }

    /* Register BOOL type */
    nmo_type_descriptor_t bool_type = {
        .guid = CKPGUID_BOOL,
        .name = "bool",
        .size = sizeof(int32_t),          /* Virtools BOOL is 4 bytes (int-sized) */
        .alignment = sizeof(int32_t),
        .class_id = 0,
        .base_type = CKPGUID_INT, /* Virtools: Boolean derived from Integer */
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_bool,
    };

    result = nmo_type_registry_register(type_registry, &bool_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t double_type = {
        .guid = CKPGUID_DOUBLE,
        .name = "double",
        .size = sizeof(double),
        .alignment = alignof(double),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_double,
    };

    result = nmo_type_registry_register(type_registry, &double_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t int8_type = {
        .guid = CKPGUID_INT8,
        .name = "int8",
        .size = sizeof(int8_t),
        .alignment = alignof(int8_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_int8,
    };

    result = nmo_type_registry_register(type_registry, &int8_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t uint8_type = {
        .guid = CKPGUID_UINT8,
        .name = "uint8",
        .size = sizeof(uint8_t),
        .alignment = alignof(uint8_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_uint8,
    };

    result = nmo_type_registry_register(type_registry, &uint8_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t int16_type = {
        .guid = CKPGUID_INT16,
        .name = "int16",
        .size = sizeof(int16_t),
        .alignment = alignof(int16_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_int16,
    };

    result = nmo_type_registry_register(type_registry, &int16_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t uint16_type = {
        .guid = CKPGUID_UINT16,
        .name = "uint16",
        .size = sizeof(uint16_t),
        .alignment = alignof(uint16_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_uint16,
    };

    result = nmo_type_registry_register(type_registry, &uint16_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t uint32_type = {
        .guid = CKPGUID_UINT32,
        .name = "uint32",
        .size = sizeof(uint32_t),
        .alignment = alignof(uint32_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_uint32,
    };

    result = nmo_type_registry_register(type_registry, &uint32_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t classid_type = {
        .guid = CKPGUID_CLASSID,
        .name = "classid",
        .size = sizeof(uint32_t),
        .alignment = alignof(uint32_t),
        .class_id = 0,
        .base_type = CKPGUID_UINT32,
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = "Virtools class id (uint32)",
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_classid,
    };

    result = nmo_type_registry_register(type_registry, &classid_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t int64_type = {
        .guid = CKPGUID_INT64,
        .name = "int64",
        .size = sizeof(int64_t),
        .alignment = alignof(int64_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_int64,
    };

    result = nmo_type_registry_register(type_registry, &int64_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t uint64_type = {
        .guid = CKPGUID_UINT64,
        .name = "uint64",
        .size = sizeof(uint64_t),
        .alignment = alignof(uint64_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_uint64,
    };

    result = nmo_type_registry_register(type_registry, &uint64_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t string_type = {
        .guid = CKPGUID_STRING,
        .name = "string",
        .size = sizeof(char *),
        .alignment = alignof(char *),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_string,
    };

    result = nmo_type_registry_register(type_registry, &string_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t pointer_type = {
        .guid = CKPGUID_POINTER,
        .name = "pointer",
        .size = sizeof(void *),
        .alignment = alignof(void *),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_POINTER,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_pointer,
    };

    result = nmo_type_registry_register(type_registry, &pointer_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t chunk_type = {
        .guid = CKPGUID_STATECHUNK,
        .name = "chunk",
        .size = sizeof(void *),
        .alignment = alignof(void *),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_POINTER,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_chunk,
    };

    result = nmo_type_registry_register(type_registry, &chunk_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t guid_type = {
        .guid = CKPGUID_GUID,
        .name = "guid",
        .size = sizeof(nmo_guid_t),
        .alignment = alignof(nmo_guid_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_guid,
    };

    result = nmo_type_registry_register(type_registry, &guid_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t object_id_type = {
        .guid = CKPGUID_ID,
        .name = "object_id",
        .size = sizeof(nmo_object_id_t),
        .alignment = alignof(nmo_object_id_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_object_id,
    };

    result = nmo_type_registry_register(type_registry, &object_id_type);
    if (result != NMO_OK) {
        return result;
    }

    /* Register Virtools math types */
    nmo_type_descriptor_t vec2_type = {
        .guid = CKPGUID_2DVECTOR,
        .name = "vector2",
        .size = sizeof(nmo_vector2_t),
        .alignment = alignof(nmo_vector2_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_vector2,
    };

    result = nmo_type_registry_register(type_registry, &vec2_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t vec3_type = {
        .guid = CKPGUID_VECTOR,
        .name = "vector3",
        .size = sizeof(nmo_vector_t),
        .alignment = alignof(nmo_vector_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_vector3,
    };

    result = nmo_type_registry_register(type_registry, &vec3_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t vec4_type = {
        .guid = CKPGUID_VECTOR4,
        .name = "vector4",
        .size = sizeof(nmo_vector4_t),
        .alignment = alignof(nmo_vector4_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_vector4,
    };

    result = nmo_type_registry_register(type_registry, &vec4_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t quat_type = {
        .guid = CKPGUID_QUATERNION,
        .name = "quaternion",
        .size = sizeof(nmo_quaternion_t),
        .alignment = alignof(nmo_quaternion_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_quaternion,
    };

    result = nmo_type_registry_register(type_registry, &quat_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t mat_type = {
        .guid = CKPGUID_MATRIX,
        .name = "matrix",
        .size = sizeof(nmo_matrix_t),
        .alignment = alignof(nmo_matrix_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_matrix,
    };

    result = nmo_type_registry_register(type_registry, &mat_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t color_type = {
        .guid = CKPGUID_COLOR,
        .name = "color",
        .size = sizeof(nmo_color_t),
        .alignment = alignof(nmo_color_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_color,
    };

    result = nmo_type_registry_register(type_registry, &color_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t rect_type = {
        .guid = CKPGUID_RECT,
        .name = "rect",
        .size = sizeof(nmo_rect_t),
        .alignment = alignof(nmo_rect_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = (const nmo_type_field_t[]){
            { .name = "left",   .description = NULL, .type_guid = CKPGUID_FLOAT, .offset = offsetof(nmo_rect_t, left),   .size = sizeof(float), .flags = NMO_FIELD_REQUIRED },
            { .name = "top",    .description = NULL, .type_guid = CKPGUID_FLOAT, .offset = offsetof(nmo_rect_t, top),    .size = sizeof(float), .flags = NMO_FIELD_REQUIRED },
            { .name = "right",  .description = NULL, .type_guid = CKPGUID_FLOAT, .offset = offsetof(nmo_rect_t, right),  .size = sizeof(float), .flags = NMO_FIELD_REQUIRED },
            { .name = "bottom", .description = NULL, .type_guid = CKPGUID_FLOAT, .offset = offsetof(nmo_rect_t, bottom), .size = sizeof(float), .flags = NMO_FIELD_REQUIRED },
        },
        .field_count = 4,
        .vtable = &nmo_builtin_vtable_rect,
    };

    result = nmo_type_registry_register(type_registry, &rect_type);
    if (result != NMO_OK) {
        return result;
    }

    /* Euler angles (3 floats) */
    nmo_type_descriptor_t euler_type = {
        .guid = CKPGUID_EULERANGLES,
        .name = "euler_angles",
        .size = sizeof(nmo_eulerangles_t),
        .alignment = alignof(nmo_eulerangles_t),
        .class_id = 0,
        .base_type = CKPGUID_VECTOR, /* Virtools: Euler Angles derived from Vector */
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = (const nmo_type_field_t[]){
            { .name = "x", .description = NULL, .type_guid = CKPGUID_FLOAT, .offset = offsetof(nmo_eulerangles_t, x), .size = sizeof(float), .flags = NMO_FIELD_REQUIRED },
            { .name = "y", .description = NULL, .type_guid = CKPGUID_FLOAT, .offset = offsetof(nmo_eulerangles_t, y), .size = sizeof(float), .flags = NMO_FIELD_REQUIRED },
            { .name = "z", .description = NULL, .type_guid = CKPGUID_FLOAT, .offset = offsetof(nmo_eulerangles_t, z), .size = sizeof(float), .flags = NMO_FIELD_REQUIRED },
        },
        .field_count = 3,
        .vtable = &nmo_builtin_vtable_eulerangles,
    };

    result = nmo_type_registry_register(type_registry, &euler_type);
    if (result != NMO_OK) {
        return result;
    }

    /* Bounding box: {min, max} */
    nmo_type_descriptor_t box_type = {
        .guid = CKPGUID_BOX,
        .name = "box",
        .size = sizeof(nmo_box_t),
        .alignment = alignof(nmo_box_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = NMO_TYPE_ID_INVALID,
        .description = NULL,
        .fields = (const nmo_type_field_t[]){
            { .name = "min", .description = NULL, .type_guid = CKPGUID_VECTOR, .offset = offsetof(nmo_box_t, min), .size = sizeof(nmo_vector_t), .flags = NMO_FIELD_REQUIRED },
            { .name = "max", .description = NULL, .type_guid = CKPGUID_VECTOR, .offset = offsetof(nmo_box_t, max), .size = sizeof(nmo_vector_t), .flags = NMO_FIELD_REQUIRED },
        },
        .field_count = 2,
        .vtable = &nmo_builtin_vtable_box,
    };

    result = nmo_type_registry_register(type_registry, &box_type);
    if (result != NMO_OK) {
        return result;
    }

    /* Container/meta types present in CK2 */
    nmo_type_descriptor_t array_type = {
        .guid = CKPGUID_ARRAY,
        .name = "array",
        .size = sizeof(nmo_array_t),
        .alignment = alignof(nmo_array_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_ARRAY | NMO_TYPE_CATEGORY_HIDDEN,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE,
        .id = NMO_TYPE_ID_INVALID,
        .description = "Array meta-type",
        .fields = NULL,
        .field_count = 0,
        .vtable = &nmo_builtin_vtable_array,
    };

    result = nmo_type_registry_register(type_registry, &array_type);
    if (result != NMO_OK) {
        return result;
    }

    /*
     * Name aliases
     *
     * The type parser and DSL are intentionally case-sensitive, and tests
     * expect legacy/uppercase spellings (e.g. INT, UINT32, STRING) to resolve.
     */
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_INT, "INT"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_FLOAT, "FLOAT"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_BOOL, "BOOL"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_STRING, "STRING"));

    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT32, "UINT32"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT64, "UINT64"));

    /* C-like convenience spellings used in parser tests */
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT32, "uint"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT32, "UINT"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT32, "uint32_t"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT64, "uint64_t"));

    /* Platform-sized unsigned type */
    if (sizeof(size_t) == 8) {
        NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT64, "size_t"));
    } else {
        NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_UINT32, "size_t"));
    }

    /* Virtools-style names */
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_VECTOR, "VxVector3"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_MATRIX, "VxMatrix"));
    NMO_RETURN_IF_ERROR(nmo_add_builtin_alias(type_registry, CKPGUID_COLOR, "VxColor"));

    NMO_RETURN_OK();
}

nmo_status_t nmo_builtin_types_patch_vtables(nmo_type_registry_t *registry)
{
    if (!registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "NULL type_registry");
    }

    /* Attach vtable to JSON-loaded Time type */
    nmo_type_descriptor_t *time_type =
        (nmo_type_descriptor_t *)nmo_type_registry_find_by_guid(
            registry, (nmo_guid_t)CKPGUID_TIME_INIT);
    if (time_type && !time_type->vtable) {
        time_type->vtable = &nmo_builtin_vtable_time;
    }

    size_t type_count = nmo_type_registry_get_type_count(registry);
    for (nmo_type_id_t id = 0; id < (nmo_type_id_t)type_count; ++id) {
        nmo_type_descriptor_t *type =
            (nmo_type_descriptor_t *)nmo_type_registry_get_by_id(registry, id);
        if (!type || !type->valid || type->vtable ||
            nmo_guid_is_null(type->base_type)) {
            continue;
        }

        const nmo_type_descriptor_t *base =
            nmo_type_registry_find_by_guid(registry, type->base_type);
        if (!base || !base->vtable || type->size != base->size) {
            continue;
        }

        type->alignment = base->alignment;
        type->vtable = base->vtable;
    }

    nmo_patch_uint32_alias_vtable(registry, CKPGUID_COPYDEPENDENCIES);
    nmo_patch_uint32_alias_vtable(registry, CKPGUID_DELETEDEPENDENCIES);
    nmo_patch_uint32_alias_vtable(registry, CKPGUID_REPLACEDEPENDENCIES);
    nmo_patch_uint32_alias_vtable(registry, CKPGUID_SAVEDEPENDENCIES);
    nmo_patch_uint32_alias_vtable(registry, CKPGUID_MESSAGE);
    nmo_patch_uint32_alias_vtable(registry, CKPGUID_ATTRIBUTE);
    nmo_patch_uint32_alias_vtable(registry, CKPGUID_OBJECTARRAY);
    nmo_patch_uint32_alias_vtable(registry, CKPGUID_2DCURVE);

    NMO_RETURN_OK();
}
