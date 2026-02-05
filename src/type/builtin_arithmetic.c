/**
 * @file builtin_arithmetic.c
 * @brief Builtin arithmetic operations implementation
 *
 * Implements arithmetic operations: Add, Subtract, Multiply, Divide, Modulo, Negate, Abs, Power
 * Supports INT and FLOAT types.
 */

#include "type/nmo_builtin_operations.h"
#include "type/nmo_operation_system.h"
#include "core/nmo_error.h"
#include "core/nmo_logger.h"
#include <math.h>
#include <stdint.h>

/* ============================================================================
 * Integer Arithmetic Operations
 * ============================================================================ */

static nmo_status_t op_add_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;
    *(int32_t *)result_data = a + b;

    NMO_RETURN_OK();
}

static nmo_status_t op_subtract_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;
    *(int32_t *)result_data = a - b;

    NMO_RETURN_OK();
}

static nmo_status_t op_multiply_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;
    *(int32_t *)result_data = a * b;

    NMO_RETURN_OK();
}

static nmo_status_t op_divide_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;

    if (b == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Division by zero");
    }

    *(int32_t *)result_data = a / b;
    NMO_RETURN_OK();
}

static nmo_status_t op_modulo_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;

    if (b == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Modulo by zero");
    }

    *(int32_t *)result_data = a % b;
    NMO_RETURN_OK();
}

static nmo_status_t op_negate_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    *(int32_t *)result_data = -a;

    NMO_RETURN_OK();
}

static nmo_status_t op_abs_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    *(int32_t *)result_data = (a < 0) ? -a : a;

    NMO_RETURN_OK();
}

static nmo_status_t op_power_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;

    /* Convert to float, compute, convert back */
    const double result = pow((double)a, (double)b);
    *(int32_t *)result_data = (int32_t)result;

    NMO_RETURN_OK();
}

/* ============================================================================
 * Float Arithmetic Operations
 * ============================================================================ */

static nmo_status_t op_add_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    const float b = *(const float *)p2_data;
    *(float *)result_data = a + b;

    NMO_RETURN_OK();
}

static nmo_status_t op_subtract_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    const float b = *(const float *)p2_data;
    *(float *)result_data = a - b;

    NMO_RETURN_OK();
}

static nmo_status_t op_multiply_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    const float b = *(const float *)p2_data;
    *(float *)result_data = a * b;

    NMO_RETURN_OK();
}

static nmo_status_t op_divide_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    const float b = *(const float *)p2_data;

    if (b == 0.0f) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Division by zero");
    }

    *(float *)result_data = a / b;
    NMO_RETURN_OK();
}

static nmo_status_t op_modulo_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    const float b = *(const float *)p2_data;

    if (b == 0.0f) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Modulo by zero");
    }

    *(float *)result_data = fmodf(a, b);
    NMO_RETURN_OK();
}

static nmo_status_t op_negate_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    *(float *)result_data = -a;

    NMO_RETURN_OK();
}

static nmo_status_t op_abs_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    *(float *)result_data = fabsf(a);

    NMO_RETURN_OK();
}

static nmo_status_t op_power_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    const float b = *(const float *)p2_data;
    *(float *)result_data = powf(a, b);

    NMO_RETURN_OK();
}

/* ============================================================================
 * Registration
 * ============================================================================ */

nmo_status_t nmo_register_arithmetic_operations(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry
) {
    if (!operation_registry || !type_registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL operation_registry or type_registry");
    }

    nmo_operation_desc_t operations[] = {
        /* Integer operations */
        {
            .operation_guid = NMO_OP_GUID_ADD,
            .name = "Add",
            .description = "Integer addition: a + b",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = NMO_TYPE_GUID_INT,
            .result_type_guid = NMO_TYPE_GUID_INT, .flags = NMO_OP_BINARY, .function = op_add_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_SUBTRACT,
            .name = "Subtract",
            .description = "Integer subtraction: a - b",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = NMO_TYPE_GUID_INT,
            .result_type_guid = NMO_TYPE_GUID_INT, .flags = NMO_OP_BINARY, .function = op_subtract_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_MULTIPLY,
            .name = "Multiply",
            .description = "Integer multiplication: a * b",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = NMO_TYPE_GUID_INT,
            .result_type_guid = NMO_TYPE_GUID_INT, .flags = NMO_OP_BINARY, .function = op_multiply_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_DIVIDE,
            .name = "Divide",
            .description = "Integer division: a / b",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = NMO_TYPE_GUID_INT,
            .result_type_guid = NMO_TYPE_GUID_INT, .flags = NMO_OP_BINARY, .function = op_divide_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_MODULO,
            .name = "Modulo",
            .description = "Integer modulo: a % b",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = NMO_TYPE_GUID_INT,
            .result_type_guid = NMO_TYPE_GUID_INT, .flags = NMO_OP_BINARY, .function = op_modulo_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_NEGATE,
            .name = "Negate",
            .description = "Integer negation: -a",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = {0, 0}, /* Unary operation */
            .result_type_guid = NMO_TYPE_GUID_INT, .flags = NMO_OP_UNARY, .function = op_negate_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_ABS,
            .name = "Abs",
            .description = "Integer absolute value: |a|",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = {0, 0}, /* Unary operation */
            .result_type_guid = NMO_TYPE_GUID_INT, .flags = NMO_OP_UNARY, .function = op_abs_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_POWER,
            .name = "Power",
            .description = "Integer power: a^b",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = NMO_TYPE_GUID_INT,
            .result_type_guid = NMO_TYPE_GUID_INT, .flags = NMO_OP_BINARY, .function = op_power_int,
            .priority = 100,
            .user_data = NULL
        },

        /* Float operations */
        {
            .operation_guid = NMO_OP_GUID_ADD,
            .name = "Add",
            .description = "Float addition: a + b",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = NMO_TYPE_GUID_FLOAT,
            .result_type_guid = NMO_TYPE_GUID_FLOAT, .flags = NMO_OP_BINARY, .function = op_add_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_SUBTRACT,
            .name = "Subtract",
            .description = "Float subtraction: a - b",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = NMO_TYPE_GUID_FLOAT,
            .result_type_guid = NMO_TYPE_GUID_FLOAT, .flags = NMO_OP_BINARY, .function = op_subtract_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_MULTIPLY,
            .name = "Multiply",
            .description = "Float multiplication: a * b",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = NMO_TYPE_GUID_FLOAT,
            .result_type_guid = NMO_TYPE_GUID_FLOAT, .flags = NMO_OP_BINARY, .function = op_multiply_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_DIVIDE,
            .name = "Divide",
            .description = "Float division: a / b",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = NMO_TYPE_GUID_FLOAT,
            .result_type_guid = NMO_TYPE_GUID_FLOAT, .flags = NMO_OP_BINARY, .function = op_divide_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_MODULO,
            .name = "Modulo",
            .description = "Float modulo: fmod(a, b)",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = NMO_TYPE_GUID_FLOAT,
            .result_type_guid = NMO_TYPE_GUID_FLOAT, .flags = NMO_OP_BINARY, .function = op_modulo_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_NEGATE,
            .name = "Negate",
            .description = "Float negation: -a",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = {0, 0}, /* Unary operation */
            .result_type_guid = NMO_TYPE_GUID_FLOAT, .flags = NMO_OP_UNARY, .function = op_negate_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_ABS,
            .name = "Abs",
            .description = "Float absolute value: |a|",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = {0, 0}, /* Unary operation */
            .result_type_guid = NMO_TYPE_GUID_FLOAT, .flags = NMO_OP_UNARY, .function = op_abs_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_POWER,
            .name = "Power",
            .description = "Float power: a^b",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = NMO_TYPE_GUID_FLOAT,
            .result_type_guid = NMO_TYPE_GUID_FLOAT, .flags = NMO_OP_BINARY, .function = op_power_float,
            .priority = 100,
            .user_data = NULL
        }
    };

    const size_t count = sizeof(operations) / sizeof(operations[0]);
    return nmo_operation_registry_register_bulk(operation_registry, operations, count, type_registry, NULL);
}
