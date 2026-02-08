/**
 * @file builtin_operations.c
 * @brief Main builtin operations registration
 *
 * Provides registration functions for builtin operations.
 */

#include "type/nmo_operations.h"
#include "type/nmo_type_system.h"
#include "type/nmo_operation_system.h"
#include "core/nmo_error.h"
#include "core/nmo_logger.h"

/* ============================================================================
 * Operation Registration
 * ============================================================================ */

nmo_status_t nmo_register_builtin_operations(
    nmo_operation_registry_t *operation_registry,
    nmo_type_registry_t *type_registry
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
