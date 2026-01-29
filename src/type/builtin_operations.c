/**
 * @file builtin_operations.c
 * @brief Main builtin operations registration
 *
 * Provides registration functions for all builtin operations and types.
 */

#include "type/builtin_operations.h"
#include "type/type_system.h"
#include "type/operation_system.h"
#include "core/nmo_error.h"
#include "core/nmo_logger.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

/* ============================================================================
 * Type Registration
 * ============================================================================ */

nmo_result_t nmo_register_builtin_types(nmo_type_registry_t *type_registry) {
    if (!type_registry) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL type_registry");
    }

    /* Register INT type */
    nmo_type_descriptor_t int_type = {
        .guid = NMO_TYPE_GUID_INT,
        .name = "INT",
        .size = sizeof(int32_t),
        .alignment = alignof(int32_t),
        .class_id = 0,
        .base_type = {0, 0},
        .category = 0,
        .flags = 0,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
        .creator_plugin = NULL
    };

    nmo_result_t result = nmo_type_registry_register(type_registry, &int_type);
    if (result.code != NMO_OK) {
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
        .category = 0,
        .flags = 0,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
        .creator_plugin = NULL
    };

    result = nmo_type_registry_register(type_registry, &float_type);
    if (result.code != NMO_OK) {
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
        .category = 0,
        .flags = 0,
        .id = 0,
        .description = NULL,
        .fields = NULL,
        .field_count = 0,
        .vtable = NULL,
        .creator_plugin = NULL
    };

    result = nmo_type_registry_register(type_registry, &bool_type);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Vector types will be registered later in Phase 6.2+ */

    return nmo_result_ok();
}

/* ============================================================================
 * Operation Registration
 * ============================================================================ */

nmo_result_t nmo_register_builtin_operations(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry
) {
    if (!operation_registry || !type_registry) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL operation_registry or type_registry");
    }

    nmo_result_t result;

    /* Register arithmetic operations (16 operations: 8 INT + 8 FLOAT) */
    result = nmo_register_arithmetic_operations(operation_registry, type_registry);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register logic operations (4 operations: BOOL only) */
    result = nmo_register_logic_operations(operation_registry, type_registry);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register comparison operations (16 operations: 8 INT + 8 FLOAT) */
    result = nmo_register_comparison_operations(operation_registry, type_registry);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register bitwise operations (7 operations: INT only) */
    result = nmo_register_bitwise_operations(operation_registry, type_registry);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Register trigonometry operations (6 operations: FLOAT only) */
    result = nmo_register_trigonometry_operations(operation_registry, type_registry);
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}
