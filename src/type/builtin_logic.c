/**
 * @file builtin_logic.c
 * @brief Builtin logic and comparison operations implementation
 *
 * Implements logic operations: And, Or, Not, Xor, Equal, NotEqual
 * Implements comparison operations: Less, LessEqual, Greater, GreaterEqual, Min, Max
 */

#include "type/nmo_operations.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_string.h"
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

static nmo_status_t register_op(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry,
    nmo_guid_t op_guid,
    const char *name,
    const char *description,
    nmo_guid_t p1_guid,
    nmo_guid_t p2_guid,
    nmo_guid_t result_guid,
    uint32_t flags,
    uint32_t priority,
    nmo_operation_fn function
) {
    nmo_operation_desc_t op = {
        .operation_guid = op_guid,
        .p1_type_guid = p1_guid,
        .p2_type_guid = p2_guid,
        .result_type_guid = result_guid,
        .function = function,
        .user_data = NULL,
        .flags = flags,
        .priority = priority,
        .name = name,
        .description = description,
    };
    return nmo_operation_registry_register(operation_registry, &op, type_registry);
}

#define DEFINE_SCALAR_CMP(tag, T) \
    static nmo_status_t op_equal_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        *(bool *)result_data = (a == b); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_not_equal_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        *(bool *)result_data = (a != b); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_less_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        *(bool *)result_data = (a < b); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_less_equal_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        *(bool *)result_data = (a <= b); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_greater_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        *(bool *)result_data = (a > b); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_greater_equal_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        *(bool *)result_data = (a >= b); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_min_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        *(T *)result_data = (a < b) ? a : b; \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_max_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        *(T *)result_data = (a > b) ? a : b; \
        NMO_RETURN_OK(); \
    }

DEFINE_SCALAR_CMP(int8, int8_t)
DEFINE_SCALAR_CMP(uint8, uint8_t)
DEFINE_SCALAR_CMP(int16, int16_t)
DEFINE_SCALAR_CMP(uint16, uint16_t)
DEFINE_SCALAR_CMP(uint32, uint32_t)
DEFINE_SCALAR_CMP(int64, int64_t)
DEFINE_SCALAR_CMP(uint64, uint64_t)
DEFINE_SCALAR_CMP(double, double)

static nmo_status_t op_equal_bool(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const bool a = *(const bool *)p1_data;
    const bool b = *(const bool *)p2_data;
    *(bool *)result_data = (a == b);
    NMO_RETURN_OK();
}

static nmo_status_t op_not_equal_bool(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const bool a = *(const bool *)p1_data;
    const bool b = *(const bool *)p2_data;
    *(bool *)result_data = (a != b);
    NMO_RETURN_OK();
}

static nmo_status_t op_equal_guid(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_guid_t a = *(const nmo_guid_t *)p1_data;
    const nmo_guid_t b = *(const nmo_guid_t *)p2_data;
    *(bool *)result_data = nmo_guid_equals(a, b);
    NMO_RETURN_OK();
}

static nmo_status_t op_not_equal_guid(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_guid_t a = *(const nmo_guid_t *)p1_data;
    const nmo_guid_t b = *(const nmo_guid_t *)p2_data;
    *(bool *)result_data = !nmo_guid_equals(a, b);
    NMO_RETURN_OK();
}

static nmo_status_t op_equal_pointer(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const void *a = *(void * const *)p1_data;
    const void *b = *(void * const *)p2_data;
    *(bool *)result_data = (a == b);
    NMO_RETURN_OK();
}

static nmo_status_t op_not_equal_pointer(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const void *a = *(void * const *)p1_data;
    const void *b = *(void * const *)p2_data;
    *(bool *)result_data = (a != b);
    NMO_RETURN_OK();
}

static nmo_status_t op_equal_object_id(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_object_id_t a = *(const nmo_object_id_t *)p1_data;
    const nmo_object_id_t b = *(const nmo_object_id_t *)p2_data;
    *(bool *)result_data = (a == b);
    NMO_RETURN_OK();
}

static nmo_status_t op_not_equal_object_id(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_object_id_t a = *(const nmo_object_id_t *)p1_data;
    const nmo_object_id_t b = *(const nmo_object_id_t *)p2_data;
    *(bool *)result_data = (a != b);
    NMO_RETURN_OK();
}

static nmo_status_t op_equal_string(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_string_t *a = (const nmo_string_t *)p1_data;
    const nmo_string_t *b = (const nmo_string_t *)p2_data;
    *(bool *)result_data = (nmo_string_equals(a, b) != 0);
    NMO_RETURN_OK();
}

static nmo_status_t op_not_equal_string(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_string_t *a = (const nmo_string_t *)p1_data;
    const nmo_string_t *b = (const nmo_string_t *)p2_data;
    *(bool *)result_data = (nmo_string_equals(a, b) == 0);
    NMO_RETURN_OK();
}

static nmo_status_t op_less_string(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_string_t *a = (const nmo_string_t *)p1_data;
    const nmo_string_t *b = (const nmo_string_t *)p2_data;
    *(bool *)result_data = (nmo_string_compare(a, b) < 0);
    NMO_RETURN_OK();
}

static nmo_status_t op_less_equal_string(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_string_t *a = (const nmo_string_t *)p1_data;
    const nmo_string_t *b = (const nmo_string_t *)p2_data;
    *(bool *)result_data = (nmo_string_compare(a, b) <= 0);
    NMO_RETURN_OK();
}

static nmo_status_t op_greater_string(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_string_t *a = (const nmo_string_t *)p1_data;
    const nmo_string_t *b = (const nmo_string_t *)p2_data;
    *(bool *)result_data = (nmo_string_compare(a, b) > 0);
    NMO_RETURN_OK();
}

static nmo_status_t op_greater_equal_string(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const nmo_string_t *a = (const nmo_string_t *)p1_data;
    const nmo_string_t *b = (const nmo_string_t *)p2_data;
    *(bool *)result_data = (nmo_string_compare(a, b) >= 0);
    NMO_RETURN_OK();
}

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

    nmo_status_t s = NMO_OK;

    /* BOOL equality */
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_EQUAL, "Equal", "Bool equality: a == b",
                    NMO_TYPE_GUID_BOOL, NMO_TYPE_GUID_BOOL, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_equal_bool);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_NOT_EQUAL, "NotEqual", "Bool inequality: a != b",
                    NMO_TYPE_GUID_BOOL, NMO_TYPE_GUID_BOOL, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_not_equal_bool);
    NMO_RETURN_IF_ERROR(s);

    /* Signed/unsigned integers + float (existing) */
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_EQUAL, "Equal", "INT8 equality: a == b",
                    NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_equal_int8);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_NOT_EQUAL, "NotEqual", "INT8 inequality: a != b",
                    NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_not_equal_int8);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS, "Less", "INT8 less than: a < b",
                    NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_int8);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS_EQ, "LessEqual", "INT8 less or equal: a <= b",
                    NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_equal_int8);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER, "Greater", "INT8 greater than: a > b",
                    NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_int8);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER_EQ, "GreaterEqual", "INT8 greater or equal: a >= b",
                    NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_equal_int8);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MIN, "Min", "INT8 minimum: min(a, b)",
                    NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_INT8, NMO_OP_BINARY, 100, op_min_int8);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MAX, "Max", "INT8 maximum: max(a, b)",
                    NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_INT8, NMO_TYPE_GUID_INT8, NMO_OP_BINARY, 100, op_max_int8);
    NMO_RETURN_IF_ERROR(s);

    s = register_op(operation_registry, type_registry, NMO_OP_GUID_EQUAL, "Equal", "UINT8 equality: a == b",
                    NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_equal_uint8);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_NOT_EQUAL, "NotEqual", "UINT8 inequality: a != b",
                    NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_not_equal_uint8);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS, "Less", "UINT8 less than: a < b",
                    NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_uint8);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS_EQ, "LessEqual", "UINT8 less or equal: a <= b",
                    NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_equal_uint8);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER, "Greater", "UINT8 greater than: a > b",
                    NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_uint8);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER_EQ, "GreaterEqual", "UINT8 greater or equal: a >= b",
                    NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_equal_uint8);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MIN, "Min", "UINT8 minimum: min(a, b)",
                    NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_UINT8, NMO_OP_BINARY, 100, op_min_uint8);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MAX, "Max", "UINT8 maximum: max(a, b)",
                    NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_UINT8, NMO_TYPE_GUID_UINT8, NMO_OP_BINARY, 100, op_max_uint8);
    NMO_RETURN_IF_ERROR(s);

    /* INT16/UINT16 */
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_EQUAL, "Equal", "INT16 equality: a == b",
                    NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_equal_int16);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_NOT_EQUAL, "NotEqual", "INT16 inequality: a != b",
                    NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_not_equal_int16);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS, "Less", "INT16 less than: a < b",
                    NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_int16);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS_EQ, "LessEqual", "INT16 less or equal: a <= b",
                    NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_equal_int16);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER, "Greater", "INT16 greater than: a > b",
                    NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_int16);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER_EQ, "GreaterEqual", "INT16 greater or equal: a >= b",
                    NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_equal_int16);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MIN, "Min", "INT16 minimum: min(a, b)",
                    NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_INT16, NMO_OP_BINARY, 100, op_min_int16);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MAX, "Max", "INT16 maximum: max(a, b)",
                    NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_INT16, NMO_TYPE_GUID_INT16, NMO_OP_BINARY, 100, op_max_int16);
    NMO_RETURN_IF_ERROR(s);

    s = register_op(operation_registry, type_registry, NMO_OP_GUID_EQUAL, "Equal", "UINT16 equality: a == b",
                    NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_equal_uint16);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_NOT_EQUAL, "NotEqual", "UINT16 inequality: a != b",
                    NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_not_equal_uint16);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS, "Less", "UINT16 less than: a < b",
                    NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_uint16);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS_EQ, "LessEqual", "UINT16 less or equal: a <= b",
                    NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_equal_uint16);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER, "Greater", "UINT16 greater than: a > b",
                    NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_uint16);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER_EQ, "GreaterEqual", "UINT16 greater or equal: a >= b",
                    NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_equal_uint16);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MIN, "Min", "UINT16 minimum: min(a, b)",
                    NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_UINT16, NMO_OP_BINARY, 100, op_min_uint16);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MAX, "Max", "UINT16 maximum: max(a, b)",
                    NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_UINT16, NMO_TYPE_GUID_UINT16, NMO_OP_BINARY, 100, op_max_uint16);
    NMO_RETURN_IF_ERROR(s);

    /* INT32/FLOAT comparisons (existing functions) */
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_EQUAL, "Equal", "Integer equality: a == b",
                    NMO_TYPE_GUID_INT, NMO_TYPE_GUID_INT, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_equal_int);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_NOT_EQUAL, "NotEqual", "Integer inequality: a != b",
                    NMO_TYPE_GUID_INT, NMO_TYPE_GUID_INT, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_not_equal_int);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS, "Less", "Integer less than: a < b",
                    NMO_TYPE_GUID_INT, NMO_TYPE_GUID_INT, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_int);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS_EQ, "LessEqual", "Integer less or equal: a <= b",
                    NMO_TYPE_GUID_INT, NMO_TYPE_GUID_INT, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_equal_int);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER, "Greater", "Integer greater than: a > b",
                    NMO_TYPE_GUID_INT, NMO_TYPE_GUID_INT, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_int);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER_EQ, "GreaterEqual", "Integer greater or equal: a >= b",
                    NMO_TYPE_GUID_INT, NMO_TYPE_GUID_INT, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_equal_int);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MIN, "Min", "Integer minimum: min(a, b)",
                    NMO_TYPE_GUID_INT, NMO_TYPE_GUID_INT, NMO_TYPE_GUID_INT, NMO_OP_BINARY, 100, op_min_int);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MAX, "Max", "Integer maximum: max(a, b)",
                    NMO_TYPE_GUID_INT, NMO_TYPE_GUID_INT, NMO_TYPE_GUID_INT, NMO_OP_BINARY, 100, op_max_int);
    NMO_RETURN_IF_ERROR(s);

    s = register_op(operation_registry, type_registry, NMO_OP_GUID_EQUAL, "Equal", "Float equality: a == b",
                    NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_equal_float);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_NOT_EQUAL, "NotEqual", "Float inequality: a != b",
                    NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_not_equal_float);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS, "Less", "Float less than: a < b",
                    NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_float);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS_EQ, "LessEqual", "Float less or equal: a <= b",
                    NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_equal_float);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER, "Greater", "Float greater than: a > b",
                    NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_float);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER_EQ, "GreaterEqual", "Float greater or equal: a >= b",
                    NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_equal_float);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MIN, "Min", "Float minimum: min(a, b)",
                    NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_FLOAT, NMO_OP_BINARY, 100, op_min_float);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MAX, "Max", "Float maximum: max(a, b)",
                    NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_FLOAT, NMO_TYPE_GUID_FLOAT, NMO_OP_BINARY, 100, op_max_float);
    NMO_RETURN_IF_ERROR(s);

    /* Derived float types: ANGLE/PERCENTAGE keep their result type (priority > base) */
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MIN, "Min", "Angle minimum: min(a, b)",
                    CKPGUID_ANGLE, CKPGUID_ANGLE, CKPGUID_ANGLE, NMO_OP_BINARY, 110, op_min_float);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MAX, "Max", "Angle maximum: max(a, b)",
                    CKPGUID_ANGLE, CKPGUID_ANGLE, CKPGUID_ANGLE, NMO_OP_BINARY, 110, op_max_float);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MIN, "Min", "Percentage minimum: min(a, b)",
                    CKPGUID_PERCENTAGE, CKPGUID_PERCENTAGE, CKPGUID_PERCENTAGE, NMO_OP_BINARY, 110, op_min_float);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MAX, "Max", "Percentage maximum: max(a, b)",
                    CKPGUID_PERCENTAGE, CKPGUID_PERCENTAGE, CKPGUID_PERCENTAGE, NMO_OP_BINARY, 110, op_max_float);
    NMO_RETURN_IF_ERROR(s);

    /* UINT32 */
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_EQUAL, "Equal", "UINT32 equality: a == b",
                    NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_equal_uint32);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_NOT_EQUAL, "NotEqual", "UINT32 inequality: a != b",
                    NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_not_equal_uint32);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS, "Less", "UINT32 less than: a < b",
                    NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_uint32);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS_EQ, "LessEqual", "UINT32 less or equal: a <= b",
                    NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_equal_uint32);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER, "Greater", "UINT32 greater than: a > b",
                    NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_uint32);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER_EQ, "GreaterEqual", "UINT32 greater or equal: a >= b",
                    NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_equal_uint32);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MIN, "Min", "UINT32 minimum: min(a, b)",
                    NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_UINT32, NMO_OP_BINARY, 100, op_min_uint32);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MAX, "Max", "UINT32 maximum: max(a, b)",
                    NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_UINT32, NMO_TYPE_GUID_UINT32, NMO_OP_BINARY, 100, op_max_uint32);
    NMO_RETURN_IF_ERROR(s);

    /* INT64/UINT64 */
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_EQUAL, "Equal", "INT64 equality: a == b",
                    NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_equal_int64);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_NOT_EQUAL, "NotEqual", "INT64 inequality: a != b",
                    NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_not_equal_int64);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS, "Less", "INT64 less than: a < b",
                    NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_int64);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS_EQ, "LessEqual", "INT64 less or equal: a <= b",
                    NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_equal_int64);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER, "Greater", "INT64 greater than: a > b",
                    NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_int64);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER_EQ, "GreaterEqual", "INT64 greater or equal: a >= b",
                    NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_equal_int64);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MIN, "Min", "INT64 minimum: min(a, b)",
                    NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_INT64, NMO_OP_BINARY, 100, op_min_int64);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MAX, "Max", "INT64 maximum: max(a, b)",
                    NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_INT64, NMO_TYPE_GUID_INT64, NMO_OP_BINARY, 100, op_max_int64);
    NMO_RETURN_IF_ERROR(s);

    s = register_op(operation_registry, type_registry, NMO_OP_GUID_EQUAL, "Equal", "UINT64 equality: a == b",
                    NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_equal_uint64);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_NOT_EQUAL, "NotEqual", "UINT64 inequality: a != b",
                    NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_not_equal_uint64);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS, "Less", "UINT64 less than: a < b",
                    NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_uint64);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS_EQ, "LessEqual", "UINT64 less or equal: a <= b",
                    NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_equal_uint64);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER, "Greater", "UINT64 greater than: a > b",
                    NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_uint64);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER_EQ, "GreaterEqual", "UINT64 greater or equal: a >= b",
                    NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_equal_uint64);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MIN, "Min", "UINT64 minimum: min(a, b)",
                    NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_UINT64, NMO_OP_BINARY, 100, op_min_uint64);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MAX, "Max", "UINT64 maximum: max(a, b)",
                    NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_UINT64, NMO_TYPE_GUID_UINT64, NMO_OP_BINARY, 100, op_max_uint64);
    NMO_RETURN_IF_ERROR(s);

    /* DOUBLE */
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_EQUAL, "Equal", "Double equality: a == b",
                    NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_equal_double);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_NOT_EQUAL, "NotEqual", "Double inequality: a != b",
                    NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_not_equal_double);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS, "Less", "Double less than: a < b",
                    NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_double);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS_EQ, "LessEqual", "Double less or equal: a <= b",
                    NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_equal_double);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER, "Greater", "Double greater than: a > b",
                    NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_double);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER_EQ, "GreaterEqual", "Double greater or equal: a >= b",
                    NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_equal_double);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MIN, "Min", "Double minimum: min(a, b)",
                    NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_DOUBLE, NMO_OP_BINARY, 100, op_min_double);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_MAX, "Max", "Double maximum: max(a, b)",
                    NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_DOUBLE, NMO_TYPE_GUID_DOUBLE, NMO_OP_BINARY, 100, op_max_double);
    NMO_RETURN_IF_ERROR(s);

    /* STRING lexicographic comparisons (bool result only) */
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_EQUAL, "Equal", "String equality: a == b",
                    NMO_TYPE_GUID_STRING, NMO_TYPE_GUID_STRING, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_equal_string);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_NOT_EQUAL, "NotEqual", "String inequality: a != b",
                    NMO_TYPE_GUID_STRING, NMO_TYPE_GUID_STRING, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_not_equal_string);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS, "Less", "String less than: a < b",
                    NMO_TYPE_GUID_STRING, NMO_TYPE_GUID_STRING, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_string);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_LESS_EQ, "LessEqual", "String less or equal: a <= b",
                    NMO_TYPE_GUID_STRING, NMO_TYPE_GUID_STRING, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_less_equal_string);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER, "Greater", "String greater than: a > b",
                    NMO_TYPE_GUID_STRING, NMO_TYPE_GUID_STRING, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_string);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_GREATER_EQ, "GreaterEqual", "String greater or equal: a >= b",
                    NMO_TYPE_GUID_STRING, NMO_TYPE_GUID_STRING, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_greater_equal_string);
    NMO_RETURN_IF_ERROR(s);

    /* GUID/POINTER/OBJECT_ID equality */
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_EQUAL, "Equal", "GUID equality: a == b",
                    NMO_TYPE_GUID_GUID, NMO_TYPE_GUID_GUID, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_equal_guid);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_NOT_EQUAL, "NotEqual", "GUID inequality: a != b",
                    NMO_TYPE_GUID_GUID, NMO_TYPE_GUID_GUID, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_not_equal_guid);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_EQUAL, "Equal", "Pointer equality: a == b",
                    NMO_TYPE_GUID_POINTER, NMO_TYPE_GUID_POINTER, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_equal_pointer);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_NOT_EQUAL, "NotEqual", "Pointer inequality: a != b",
                    NMO_TYPE_GUID_POINTER, NMO_TYPE_GUID_POINTER, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_not_equal_pointer);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_EQUAL, "Equal", "ObjectID equality: a == b",
                    NMO_TYPE_GUID_OBJECT_ID, NMO_TYPE_GUID_OBJECT_ID, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_equal_object_id);
    NMO_RETURN_IF_ERROR(s);
    s = register_op(operation_registry, type_registry, NMO_OP_GUID_NOT_EQUAL, "NotEqual", "ObjectID inequality: a != b",
                    NMO_TYPE_GUID_OBJECT_ID, NMO_TYPE_GUID_OBJECT_ID, NMO_TYPE_GUID_BOOL, NMO_OP_BINARY, 100, op_not_equal_object_id);
    NMO_RETURN_IF_ERROR(s);

    NMO_RETURN_OK();
}
