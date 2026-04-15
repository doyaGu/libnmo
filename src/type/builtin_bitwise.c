/**
 * @file builtin_bitwise.c
 * @brief Builtin bitwise operations implementation
 *
 * Implements bitwise operations: BitAnd, BitOr, BitXor, BitNot, ShiftLeft, ShiftRight,
 * RotateLeft, RotateRight
 * Supports INT type only.
 */

#include "type/nmo_operations.h"
#include "core/nmo_error.h"
#include <stdint.h>

/* ============================================================================
 * Integer Bitwise Operations
 * ============================================================================ */

static nmo_status_t op_bit_and_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;
    *(int32_t *)result_data = a & b;

    NMO_RETURN_OK();
}

static nmo_status_t op_bit_or_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;
    *(int32_t *)result_data = a | b;

    NMO_RETURN_OK();
}

static nmo_status_t op_bit_xor_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;
    *(int32_t *)result_data = a ^ b;

    NMO_RETURN_OK();
}

static nmo_status_t op_bit_not_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    *(int32_t *)result_data = ~a;

    NMO_RETURN_OK();
}

static nmo_status_t op_shift_left_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;

    if (b < 0 || b >= 32) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Shift count out of range");
    }

    *(int32_t *)result_data = a << b;
    NMO_RETURN_OK();
}

static nmo_status_t op_shift_right_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const int32_t a = *(const int32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;

    if (b < 0 || b >= 32) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Shift count out of range");
    }

    *(int32_t *)result_data = a >> b;
    NMO_RETURN_OK();
}

static nmo_status_t op_rotate_left_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const uint32_t a = *(const uint32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;

    if (b < 0 || b >= 32) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Rotate count out of range");
    }

    const uint32_t result = (a << b) | (a >> (32 - b));
    *(int32_t *)result_data = (int32_t)result;

    NMO_RETURN_OK();
}

static nmo_status_t op_rotate_right_int(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;

    const uint32_t a = *(const uint32_t *)p1_data;
    const int32_t b = *(const int32_t *)p2_data;

    if (b < 0 || b >= 32) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "Rotate count out of range");
    }

    const uint32_t result = (a >> b) | (a << (32 - b));
    *(int32_t *)result_data = (int32_t)result;

    NMO_RETURN_OK();
}

/* ============================================================================
 * Registration
 * ============================================================================ */

nmo_status_t nmo_register_bitwise_operations(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry
) {
    if (!operation_registry || !type_registry) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                                "NULL operation_registry or type_registry");
    }

    nmo_operation_desc_t operations[] = {
        {
            .operation_guid = NMO_OP_GUID_BIT_AND,
            .name = "BitAnd",
            .description = "Bitwise AND: a & b",
            .p1_type_guid = CKPGUID_INT,
            .p2_type_guid = CKPGUID_INT,
            .result_type_guid = CKPGUID_INT, .flags = NMO_OP_BINARY, .function = op_bit_and_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_BIT_OR,
            .name = "BitOr",
            .description = "Bitwise OR: a | b",
            .p1_type_guid = CKPGUID_INT,
            .p2_type_guid = CKPGUID_INT,
            .result_type_guid = CKPGUID_INT, .flags = NMO_OP_BINARY, .function = op_bit_or_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_BIT_XOR,
            .name = "BitXor",
            .description = "Bitwise XOR: a ^ b",
            .p1_type_guid = CKPGUID_INT,
            .p2_type_guid = CKPGUID_INT,
            .result_type_guid = CKPGUID_INT, .flags = NMO_OP_BINARY, .function = op_bit_xor_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_BIT_NOT,
            .name = "BitNot",
            .description = "Bitwise NOT: ~a",
            .p1_type_guid = CKPGUID_INT,
            .p2_type_guid = {0, 0}, /* Unary operation */
            .result_type_guid = CKPGUID_INT,
            .flags = NMO_OP_UNARY,
            .function = op_bit_not_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_SHIFT_LEFT,
            .name = "ShiftLeft",
            .description = "Bitwise left shift: a << b",
            .p1_type_guid = CKPGUID_INT,
            .p2_type_guid = CKPGUID_INT,
            .result_type_guid = CKPGUID_INT, .flags = NMO_OP_BINARY, .function = op_shift_left_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_SHIFT_RIGHT,
            .name = "ShiftRight",
            .description = "Bitwise right shift: a >> b",
            .p1_type_guid = CKPGUID_INT,
            .p2_type_guid = CKPGUID_INT,
            .result_type_guid = CKPGUID_INT, .flags = NMO_OP_BINARY, .function = op_shift_right_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_ROTATE_LEFT,
            .name = "RotateLeft",
            .description = "Bitwise rotate left: rol(a, b)",
            .p1_type_guid = CKPGUID_INT,
            .p2_type_guid = CKPGUID_INT,
            .result_type_guid = CKPGUID_INT, .flags = NMO_OP_BINARY, .function = op_rotate_left_int,
            .priority = 100,
            .user_data = NULL
        },
        {
            .operation_guid = NMO_OP_GUID_ROTATE_RIGHT,
            .name = "RotateRight",
            .description = "Bitwise rotate right: ror(a, b)",
            .p1_type_guid = CKPGUID_INT,
            .p2_type_guid = CKPGUID_INT,
            .result_type_guid = CKPGUID_INT, .flags = NMO_OP_BINARY,
            .function = op_rotate_right_int,
            .priority = 100,
            .user_data = NULL
        }
    };

    const size_t count = sizeof(operations) / sizeof(operations[0]);
    return nmo_operation_registry_register_bulk(operation_registry, operations, count, type_registry, NULL);
}
