/**
 * @file builtin_trigonometry.c
 * @brief Builtin trigonometry operations implementation
 *
 * Implements trigonometry operations: Sin, Cos, Tan, Asin, Acos, Atan
 * Supports FLOAT type only.
 */

#include "type/builtin_operations.h"
#include "core/nmo_error.h"
#include <math.h>

/* ============================================================================
 * Float Trigonometry Operations
 * ============================================================================ */

static nmo_result_t op_sin_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    *(float *)result_data = sinf(a);

    return nmo_result_ok();
}

static nmo_result_t op_cos_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    *(float *)result_data = cosf(a);

    return nmo_result_ok();
}

static nmo_result_t op_tan_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    *(float *)result_data = tanf(a);

    return nmo_result_ok();
}

static nmo_result_t op_asin_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;

    if (a < -1.0f || a > 1.0f) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "asin domain error: input must be in [-1, 1]");
    }

    *(float *)result_data = asinf(a);
    return nmo_result_ok();
}

static nmo_result_t op_acos_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;

    if (a < -1.0f || a > 1.0f) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "acos domain error: input must be in [-1, 1]");
    }

    *(float *)result_data = acosf(a);
    return nmo_result_ok();
}

static nmo_result_t op_atan_float(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data;

    const float a = *(const float *)p1_data;
    *(float *)result_data = atanf(a);

    return nmo_result_ok();
}

/* ============================================================================
 * Registration
 * ============================================================================ */

nmo_result_t nmo_register_trigonometry_operations(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry
) {
    if (!operation_registry || !type_registry) {
        return nmo_result_errorf(NULL, NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "NULL operation_registry or type_registry");
    }

    nmo_operation_desc_t operations[] = {
        {
            .operation_guid = NMO_OP_GUID_SIN,
            .name = "Sin",
            .description = "Sine function: sin(a)",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = {0, 0}, /* Unary operation */
            .result_type_guid = NMO_TYPE_GUID_FLOAT,
            .flags = NMO_OP_UNARY,
            .function = op_sin_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_COS,
            .name = "Cos",
            .description = "Cosine function: cos(a)",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = {0, 0}, /* Unary operation */
            .result_type_guid = NMO_TYPE_GUID_FLOAT,
            .flags = NMO_OP_UNARY,
            .function = op_cos_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_TAN,
            .name = "Tan",
            .description = "Tangent function: tan(a)",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = {0, 0}, /* Unary operation */
            .result_type_guid = NMO_TYPE_GUID_FLOAT,
            .flags = NMO_OP_UNARY,
            .function = op_tan_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_ASIN,
            .name = "Asin",
            .description = "Arcsine function: asin(a)",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = {0, 0}, /* Unary operation */
            .result_type_guid = NMO_TYPE_GUID_FLOAT,
            .flags = NMO_OP_UNARY,
            .function = op_asin_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_ACOS,
            .name = "Acos",
            .description = "Arccosine function: acos(a)",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = {0, 0}, /* Unary operation */
            .result_type_guid = NMO_TYPE_GUID_FLOAT,
            .flags = NMO_OP_UNARY,
            .function = op_acos_float,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_ATAN,
            .name = "Atan",
            .description = "Arctangent function: atan(a)",
            .p1_type_guid = NMO_TYPE_GUID_FLOAT,
            .p2_type_guid = {0, 0}, /* Unary operation */
            .result_type_guid = NMO_TYPE_GUID_FLOAT,
            .flags = NMO_OP_UNARY,
            .function = op_atan_float,
            .priority = 100,
            .user_data = NULL
        }
    };

    const size_t count = sizeof(operations) / sizeof(operations[0]);
    return nmo_operation_registry_register_bulk(operation_registry, operations, count, type_registry, NULL);
}
