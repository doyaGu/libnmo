/**
 * @file builtin_operations.c
 * @brief Main builtin operations registration
 *
 * Provides registration functions for all builtin operations and types.
 */

#include "type/nmo_builtin_operations.h"
#include "type/nmo_type_system.h"
#include "type/nmo_operation_system.h"
#include "core/nmo_math.h"
#include "core/nmo_color.h"
#include "core/nmo_error.h"
#include "core/nmo_logger.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

/* ============================================================================
 * Type Registration
 * ============================================================================ */

nmo_status_t nmo_register_builtin_types(nmo_type_registry_t *type_registry) {
    if (!type_registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type_registry");
    }

    /* Register basic scalar types */
    nmo_type_descriptor_t int_type = {
        .guid = NMO_TYPE_GUID_INT,
        .name = "INT",
        .size = sizeof(int32_t),
        .alignment = alignof(int32_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    nmo_status_t result = nmo_type_registry_register(type_registry, &int_type);
    if (result != NMO_OK) {
        return result;
    }

    /* Register FLOAT type */
    nmo_type_descriptor_t float_type = {
        .guid = NMO_TYPE_GUID_FLOAT,
        .name = "FLOAT",
        .size = sizeof(float),
        .alignment = alignof(float),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &float_type);
    if (result != NMO_OK) {
        return result;
    }

    /* Register BOOL type */
    nmo_type_descriptor_t bool_type = {
        .guid = NMO_TYPE_GUID_BOOL,
        .name = "BOOL",
        .size = sizeof(bool),
        .alignment = alignof(bool),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &bool_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t double_type = {
        .guid = NMO_TYPE_GUID_DOUBLE,
        .name = "DOUBLE",
        .size = sizeof(double),
        .alignment = alignof(double),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &double_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t int8_type = {
        .guid = NMO_TYPE_GUID_INT8,
        .name = "INT8",
        .size = sizeof(int8_t),
        .alignment = alignof(int8_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &int8_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t uint8_type = {
        .guid = NMO_TYPE_GUID_UINT8,
        .name = "UINT8",
        .size = sizeof(uint8_t),
        .alignment = alignof(uint8_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &uint8_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t int16_type = {
        .guid = NMO_TYPE_GUID_INT16,
        .name = "INT16",
        .size = sizeof(int16_t),
        .alignment = alignof(int16_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &int16_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t uint16_type = {
        .guid = NMO_TYPE_GUID_UINT16,
        .name = "UINT16",
        .size = sizeof(uint16_t),
        .alignment = alignof(uint16_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &uint16_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t uint32_type = {
        .guid = NMO_TYPE_GUID_UINT32,
        .name = "UINT32",
        .size = sizeof(uint32_t),
        .alignment = alignof(uint32_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &uint32_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t int64_type = {
        .guid = NMO_TYPE_GUID_INT64,
        .name = "INT64",
        .size = sizeof(int64_t),
        .alignment = alignof(int64_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &int64_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t uint64_type = {
        .guid = NMO_TYPE_GUID_UINT64,
        .name = "UINT64",
        .size = sizeof(uint64_t),
        .alignment = alignof(uint64_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &uint64_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t string_type = {
        .guid = NMO_TYPE_GUID_STRING,
        .name = "STRING",
        .size = sizeof(char *),
        .alignment = alignof(char *),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &string_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t pointer_type = {
        .guid = NMO_TYPE_GUID_POINTER,
        .name = "POINTER",
        .size = sizeof(void *),
        .alignment = alignof(void *),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_POINTER,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &pointer_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t chunk_type = {
        .guid = NMO_TYPE_GUID_CHUNK,
        .name = "CHUNK",
        .size = sizeof(void *),
        .alignment = alignof(void *),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_POINTER,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &chunk_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t guid_type = {
        .guid = NMO_TYPE_GUID_GUID,
        .name = "GUID",
        .size = sizeof(nmo_guid_t),
        .alignment = alignof(nmo_guid_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &guid_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t object_id_type = {
        .guid = NMO_TYPE_GUID_OBJECT_ID,
        .name = "OBJECT_ID",
        .size = sizeof(nmo_object_id_t),
        .alignment = alignof(nmo_object_id_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &object_id_type);
    if (result != NMO_OK) {
        return result;
    }

    /* Register Virtools math types */
    nmo_type_descriptor_t vec2_type = {
        .guid = NMO_TYPE_GUID_VECTOR2,
        .name = "VxVector2",
        .size = sizeof(nmo_vector2_t),
        .alignment = alignof(nmo_vector2_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &vec2_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t vec3_type = {
        .guid = NMO_TYPE_GUID_VECTOR3,
        .name = "VxVector3",
        .size = sizeof(nmo_vector_t),
        .alignment = alignof(nmo_vector_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &vec3_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t vec4_type = {
        .guid = NMO_TYPE_GUID_VECTOR4,
        .name = "VxVector4",
        .size = sizeof(nmo_vector4_t),
        .alignment = alignof(nmo_vector4_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &vec4_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t quat_type = {
        .guid = NMO_TYPE_GUID_QUATERNION,
        .name = "VxQuaternion",
        .size = sizeof(nmo_quaternion_t),
        .alignment = alignof(nmo_quaternion_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &quat_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t mat_type = {
        .guid = NMO_TYPE_GUID_MATRIX,
        .name = "VxMatrix",
        .size = sizeof(nmo_matrix_t),
        .alignment = alignof(nmo_matrix_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &mat_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t color_type = {
        .guid = NMO_TYPE_GUID_COLOR,
        .name = "VxColor",
        .size = sizeof(nmo_color_t),
        .alignment = alignof(nmo_color_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &color_type);
    if (result != NMO_OK) {
        return result;
    }

    nmo_type_descriptor_t rect_type = {
        .guid = NMO_TYPE_GUID_RECT,
        .name = "VxRect",
        .size = sizeof(nmo_rect_t),
        .alignment = alignof(nmo_rect_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_COPYABLE | NMO_TYPE_FLAG_POD,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
    };

    result = nmo_type_registry_register(type_registry, &rect_type);
    if (result != NMO_OK) {
        return result;
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Operation Registration
 * ============================================================================ */

nmo_status_t nmo_register_builtin_operations(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry
) {
    if (!operation_registry || !type_registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL operation_registry or type_registry");
    }

    nmo_status_t result;

    /* Register arithmetic operations (16 operations: 8 INT + 8 FLOAT) */
    result = nmo_register_arithmetic_operations(operation_registry, type_registry);
    if (result != NMO_OK) {
        return result;
    }

    /* Register logic operations (4 operations: BOOL only) */
    result = nmo_register_logic_operations(operation_registry, type_registry);
    if (result != NMO_OK) {
        return result;
    }

    /* Register comparison operations (16 operations: 8 INT + 8 FLOAT) */
    result = nmo_register_comparison_operations(operation_registry, type_registry);
    if (result != NMO_OK) {
        return result;
    }

    /* Register bitwise operations (8 operations: INT only) */
    result = nmo_register_bitwise_operations(operation_registry, type_registry);
    if (result != NMO_OK) {
        return result;
    }

    /* Register trigonometry operations (6 operations: FLOAT only) */
    result = nmo_register_trigonometry_operations(operation_registry, type_registry);
    if (result != NMO_OK) {
        return result;
    }

    /* Register vector operations (Vector2/3/4) */
    result = nmo_register_vector_operations(operation_registry, type_registry);
    if (result != NMO_OK) {
        return result;
    }

    NMO_RETURN_OK();
}
