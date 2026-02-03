/**
 * @file builtin_logic.c
 * @brief Builtin logic and comparison operations implementation
 *
 * Implements logic operations: And, Or, Not, Xor, Equal, NotEqual
 * Implements comparison operations: Less, LessEqual, Greater, GreaterEqual, Min, Max
 */

#include "type/builtin_operations.h"
#include "core/nmo_error.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ============================================================================
 * Boolean Logic Operations
 * ============================================================================ */

static nmo_status_t op_and_bool(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const bool a = *(const bool *)p1_data;
    const bool b = *(const bool *)p2_data;
    *(bool *)result_data = a && b;

    NMO_RETURN_OK();
}

static nmo_status_t op_or_bool(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const bool a = *(const bool *)p1_data;
    const bool b = *(const bool *)p2_data;
    *(bool *)result_data = a || b;

    NMO_RETURN_OK();
}

static nmo_status_t op_not_bool(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data;

    const bool a = *(const bool *)p1_data;
    *(bool *)result_data = !a;

    NMO_RETURN_OK();
}

static nmo_status_t op_xor_bool(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const bool a = *(const bool *)p1_data;
    const bool b = *(const bool *)p2_data;
    *(bool *)result_data = (a && !b) || (!a && b);

    NMO_RETURN_OK();
}

/* ============================================================================
 * Integer Comparison Operations
 * ============================================================================ */

static nmo_status_t op_equal_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;
    *(bool *)result_data = (a == b);

    NMO_RETURN_OK();
}

static nmo_status_t op_not_equal_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;
    *(bool *)result_data = (a != b);

    NMO_RETURN_OK();
}

static nmo_status_t op_less_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;
    *(bool *)result_data = (a < b);

    NMO_RETURN_OK();
}

static nmo_status_t op_less_equal_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;
    *(bool *)result_data = (a <= b);

    NMO_RETURN_OK();
}

static nmo_status_t op_greater_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;
    *(bool *)result_data = (a > b);

    NMO_RETURN_OK();
}

static nmo_status_t op_greater_equal_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;
    *(bool *)result_data = (a >= b);

    NMO_RETURN_OK();
}

static nmo_status_t op_min_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;
    *(int32_t *)result_data = (a < b) ? a : b;

    NMO_RETURN_OK();
}

static nmo_status_t op_max_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;
    *(int32_t *)result_data = (a > b) ? a : b;

    NMO_RETURN_OK();
}

/* ============================================================================
 * Float Comparison Operations
 * ============================================================================ */

static nmo_status_t op_equal_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    const float b = *(const float *)p2_data;
    *(bool *)result_data = (a == b);

    NMO_RETURN_OK();
}

static nmo_status_t op_not_equal_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    const float b = *(const float *)p2_data;
    *(bool *)result_data = (a != b);

    NMO_RETURN_OK();
}

static nmo_status_t op_less_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    const float b = *(const float *)p2_data;
    *(bool *)result_data = (a < b);

    NMO_RETURN_OK();
}

static nmo_status_t op_less_equal_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    const float b = *(const float *)p2_data;
    *(bool *)result_data = (a <= b);

    NMO_RETURN_OK();
}

static nmo_status_t op_greater_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    const float b = *(const float *)p2_data;
    *(bool *)result_data = (a > b);

    NMO_RETURN_OK();
}

static nmo_status_t op_greater_equal_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    const float b = *(const float *)p2_data;
    *(bool *)result_data = (a >= b);

    NMO_RETURN_OK();
}

static nmo_status_t op_min_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    const float b = *(const float *)p2_data;
    *(float *)result_data = (a < b) ? a : b;

    NMO_RETURN_OK();
}

static nmo_status_t op_max_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    const float b = *(const float *)p2_data;
    *(float *)result_data = (a > b) ? a : b;

    NMO_RETURN_OK();
}

/* ============================================================================
 * Registration Functions
 * ============================================================================ */

nmo_status_t nmo_register_logic_operations(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry
) {
    if (!operation_registry || !type_registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL operation_registry or type_registry");
    }

    nmo_operation_desc_t operations[] = {
        {
            .operation_guid = NMO_OP_GUID_AND,
            .name = "And",
            .description = "Logical AND: a && b",
            .p1_type_guid = NMO_TYPE_GUID_BOOL,
            .p2_type_guid = NMO_TYPE_GUID_BOOL,
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .function = op_and_bool,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_OR,
            .name = "Or",
            .description = "Logical OR: a || b",
            .p1_type_guid = NMO_TYPE_GUID_BOOL,
            .p2_type_guid = NMO_TYPE_GUID_BOOL,
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .function = op_or_bool,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_NOT,
            .name = "Not",
            .description = "Logical NOT: !a",
            .p1_type_guid = NMO_TYPE_GUID_BOOL,
            .p2_type_guid = {0, 0}, /* Unary operation */
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .flags = NMO_OP_UNARY,
            .function = op_not_bool,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_XOR,
            .name = "Xor",
            .description = "Logical XOR: a ^ b",
            .p1_type_guid = NMO_TYPE_GUID_BOOL,
            .p2_type_guid = NMO_TYPE_GUID_BOOL,
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .function = op_xor_bool,
            .priority = 100,
            .user_data = NULL
        }
    };

    const size_t count = sizeof(operations) / sizeof(operations[0]);
    return nmo_operation_registry_register_bulk(operation_registry, operations, count, type_registry, NULL);
}

nmo_status_t nmo_register_comparison_operations(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry
) {
    if (!operation_registry || !type_registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL operation_registry or type_registry");
    }

    nmo_operation_desc_t operations[] = {
        /* Integer comparisons */
        {
            .operation_guid = NMO_OP_GUID_EQUAL,
            .name = "Equal",
            .description = "Integer equality: a == b",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = NMO_TYPE_GUID_INT,
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .flags = NMO_OP_BINARY,
            .function = op_equal_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_NOT_EQUAL,
            .name = "NotEqual",
            .description = "Integer inequality: a != b",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = NMO_TYPE_GUID_INT,
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .flags = NMO_OP_BINARY,
            .function = op_not_equal_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_LESS,
            .name = "Less",
            .description = "Integer less than: a < b",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = NMO_TYPE_GUID_INT,
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .flags = NMO_OP_BINARY,
            .function = op_less_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_LESS_EQ,
            .name = "LessEqual",
            .description = "Integer less or equal: a <= b",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = NMO_TYPE_GUID_INT,
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .flags = NMO_OP_BINARY,
            .function = op_less_equal_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_GREATER,
            .name = "Greater",
            .description = "Integer greater than: a > b",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = NMO_TYPE_GUID_INT,
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .flags = NMO_OP_BINARY,
            .function = op_greater_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_GREATER_EQ,
            .name = "GreaterEqual",
            .description = "Integer greater or equal: a >= b",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = NMO_TYPE_GUID_INT,
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .flags = NMO_OP_BINARY,
            .function = op_greater_equal_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_MIN,
            .name = "Min",
            .description = "Integer minimum: min(a, b)",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = NMO_TYPE_GUID_INT,
            .result_type_guid = NMO_TYPE_GUID_INT, .flags = NMO_OP_BINARY, .function = op_min_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_MAX,
            .name = "Max",
            .description = "Integer maximum: max(a, b)",
            .p1_type_guid = NMO_TYPE_GUID_INT,
            .p2_type_guid = NMO_TYPE_GUID_INT,
            .result_type_guid = NMO_TYPE_GUID_INT, .flags = NMO_OP_BINARY, .function = op_max_int,
            .priority = 100,
            .user_data = NULL
        },

        /* Float comparisons */
        {
            .operation_guid = NMO_OP_GUID_EQUAL,
            .name = "Equal",
            .description = "Float equality: a == b",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = NMO_TYPE_GUID_FLOAT,
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .flags = NMO_OP_BINARY,
            .function = op_equal_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_NOT_EQUAL,
            .name = "NotEqual",
            .description = "Float inequality: a != b",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = NMO_TYPE_GUID_FLOAT,
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .flags = NMO_OP_BINARY,
            .function = op_not_equal_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_LESS,
            .name = "Less",
            .description = "Float less than: a < b",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = NMO_TYPE_GUID_FLOAT,
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .flags = NMO_OP_BINARY,
            .function = op_less_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_LESS_EQ,
            .name = "LessEqual",
            .description = "Float less or equal: a <= b",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = NMO_TYPE_GUID_FLOAT,
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .flags = NMO_OP_BINARY,
            .function = op_less_equal_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_GREATER,
            .name = "Greater",
            .description = "Float greater than: a > b",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = NMO_TYPE_GUID_FLOAT,
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .flags = NMO_OP_BINARY,
            .function = op_greater_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_GREATER_EQ,
            .name = "GreaterEqual",
            .description = "Float greater or equal: a >= b",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = NMO_TYPE_GUID_FLOAT,
            .result_type_guid = NMO_TYPE_GUID_BOOL,
            .flags = NMO_OP_BINARY,
            .function = op_greater_equal_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_MIN,
            .name = "Min",
            .description = "Float minimum: min(a, b)",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = NMO_TYPE_GUID_FLOAT,
            .result_type_guid = NMO_TYPE_GUID_FLOAT, .flags = NMO_OP_BINARY, .function = op_min_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_MAX,
            .name = "Max",
            .description = "Float maximum: max(a, b)",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = NMO_TYPE_GUID_FLOAT,
            .result_type_guid = NMO_TYPE_GUID_FLOAT, .flags = NMO_OP_BINARY, .function = op_max_float,
            .priority = 100,
            .user_data = NULL
        }
    };

    const size_t count = sizeof(operations) / sizeof(operations[0]);
    return nmo_operation_registry_register_bulk(operation_registry, operations, count, type_registry, NULL);
}
