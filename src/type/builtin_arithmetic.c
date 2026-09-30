/**
 * @file builtin_arithmetic.c
 * @brief Builtin arithmetic operations implementation
 *
 * Implements arithmetic operations: Add, Subtract, Multiply, Divide, Modulo, Negate, Abs, Power
 * Supports INT/FLOAT plus extended scalar types (INT8/UINT8/INT16/UINT16/UINT32/INT64/UINT64/DOUBLE)
 * and CK2 derived float types (ANGLE/PERCENTAGE).
 */

#include "type/nmo_operations.h"
#include "type/nmo_operation_system.h"
#include "builtin_operation_table.h"
#include "core/nmo_error.h"
#include "core/nmo_logger.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ============================================================================
 * Integer Arithmetic Operations
 * ============================================================================ */

static nmo_status_t checked_div_by_zero_int64(int64_t divisor) {
    if (divisor == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Division by zero");
    }
    NMO_RETURN_OK();
}

static nmo_status_t checked_mod_by_zero_int64(int64_t divisor) {
    if (divisor == 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Modulo by zero");
    }
    NMO_RETURN_OK();
}

static nmo_status_t checked_div_by_zero_uint64(uint64_t divisor) {
    if (divisor == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Division by zero");
    }
    NMO_RETURN_OK();
}

static nmo_status_t checked_mod_by_zero_uint64(uint64_t divisor) {
    if (divisor == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Modulo by zero");
    }
    NMO_RETURN_OK();
}

#define NMO_ARITH_STORE_BITS(T, U, dst_ptr, u_value) \
    do { \
        U _tmp_u = (u_value); \
        memcpy((dst_ptr), &_tmp_u, sizeof(T)); \
    } while (0)

#define DEFINE_SIGNED_INT_ARITH(tag, T, U, MINV, MAXV) \
    static nmo_status_t op_add_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        const U ua = (U)a; \
        const U ub = (U)b; \
        const U ur = (U)(ua + ub); \
        NMO_ARITH_STORE_BITS(T, U, (T *)result_data, ur); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_subtract_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        const U ua = (U)a; \
        const U ub = (U)b; \
        const U ur = (U)(ua - ub); \
        NMO_ARITH_STORE_BITS(T, U, (T *)result_data, ur); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_multiply_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        const U ua = (U)a; \
        const U ub = (U)b; \
        const U ur = (U)(ua * ub); \
        NMO_ARITH_STORE_BITS(T, U, (T *)result_data, ur); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_divide_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        if (b == (T)0) { \
            return checked_div_by_zero_int64(0); \
        } \
        if (a == (T)(MINV) && b == (T)-1) { \
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Division overflow"); \
        } \
        *(T *)result_data = (T)(a / b); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_modulo_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        if (b == (T)0) { \
            return checked_mod_by_zero_int64(0); \
        } \
        if (a == (T)(MINV) && b == (T)-1) { \
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Modulo overflow"); \
        } \
        *(T *)result_data = (T)(a % b); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_negate_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const U ua = (U)a; \
        const U ur = (U)(0 - ua); \
        NMO_ARITH_STORE_BITS(T, U, (T *)result_data, ur); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_abs_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        if (a < (T)0) { \
            return op_negate_##tag(p1_data, p1_type, NULL, NULL, result_data, result_type, user_data); \
        } \
        *(T *)result_data = a; \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_power_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        const double r = pow((double)a, (double)b); \
        if (!isfinite(r) || r < (double)(MINV) || r > (double)(MAXV)) { \
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Power result out of range"); \
        } \
        *(T *)result_data = (T)r; \
        NMO_RETURN_OK(); \
    }

#define DEFINE_UNSIGNED_INT_ARITH(tag, T, U, MAXV) \
    static nmo_status_t op_add_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        *(T *)result_data = (T)(a + b); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_subtract_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        *(T *)result_data = (T)(a - b); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_multiply_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        *(T *)result_data = (T)(a * b); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_divide_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        if (b == (T)0) { \
            return checked_div_by_zero_uint64(0); \
        } \
        *(T *)result_data = (T)(a / b); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_modulo_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        if (b == (T)0) { \
            return checked_mod_by_zero_uint64(0); \
        } \
        *(T *)result_data = (T)(a % b); \
        NMO_RETURN_OK(); \
    } \
    static nmo_status_t op_power_##tag( \
        const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type, \
        void *result_data, const nmo_type_descriptor_t *result_type, void *user_data \
    ) { \
        (void)p1_type; (void)p2_type; (void)result_type; (void)user_data; \
        const T a = *(const T *)p1_data; \
        const T b = *(const T *)p2_data; \
        const double r = pow((double)a, (double)b); \
        if (!isfinite(r) || r < 0.0 || r > (double)(MAXV)) { \
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Power result out of range"); \
        } \
        *(T *)result_data = (T)r; \
        NMO_RETURN_OK(); \
    }

DEFINE_SIGNED_INT_ARITH(int8, int8_t, uint8_t, INT8_MIN, INT8_MAX)
DEFINE_UNSIGNED_INT_ARITH(uint8, uint8_t, uint8_t, UINT8_MAX)
DEFINE_SIGNED_INT_ARITH(int16, int16_t, uint16_t, INT16_MIN, INT16_MAX)
DEFINE_UNSIGNED_INT_ARITH(uint16, uint16_t, uint16_t, UINT16_MAX)
DEFINE_SIGNED_INT_ARITH(int, int32_t, uint32_t, INT32_MIN, INT32_MAX)
DEFINE_UNSIGNED_INT_ARITH(uint32, uint32_t, uint32_t, UINT32_MAX)
DEFINE_SIGNED_INT_ARITH(int64, int64_t, uint64_t, INT64_MIN, INT64_MAX)
DEFINE_UNSIGNED_INT_ARITH(uint64, uint64_t, uint64_t, UINT64_MAX)

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
 * Double Arithmetic Operations
 * ============================================================================ */

static nmo_status_t op_add_double(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const double a = *(const double *)p1_data;
    const double b = *(const double *)p2_data;
    *(double *)result_data = a + b;
    NMO_RETURN_OK();
}

static nmo_status_t op_subtract_double(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const double a = *(const double *)p1_data;
    const double b = *(const double *)p2_data;
    *(double *)result_data = a - b;
    NMO_RETURN_OK();
}

static nmo_status_t op_multiply_double(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const double a = *(const double *)p1_data;
    const double b = *(const double *)p2_data;
    *(double *)result_data = a * b;
    NMO_RETURN_OK();
}

static nmo_status_t op_divide_double(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const double a = *(const double *)p1_data;
    const double b = *(const double *)p2_data;
    if (b == 0.0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Division by zero");
    }
    *(double *)result_data = a / b;
    NMO_RETURN_OK();
}

static nmo_status_t op_modulo_double(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const double a = *(const double *)p1_data;
    const double b = *(const double *)p2_data;
    if (b == 0.0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Modulo by zero");
    }
    *(double *)result_data = fmod(a, b);
    NMO_RETURN_OK();
}

static nmo_status_t op_negate_double(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data;
    const double a = *(const double *)p1_data;
    *(double *)result_data = -a;
    NMO_RETURN_OK();
}

static nmo_status_t op_abs_double(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_data; (void)p2_type; (void)result_type; (void)user_data;
    const double a = *(const double *)p1_data;
    *(double *)result_data = fabs(a);
    NMO_RETURN_OK();
}

static nmo_status_t op_power_double(
    const void *p1_data, const nmo_type_descriptor_t *p1_type, const void *p2_data, const nmo_type_descriptor_t *p2_type,
    void *result_data, const nmo_type_descriptor_t *result_type, void *user_data
) {
    (void)p1_type; (void)p2_type; (void)result_type; (void)user_data;
    const double a = *(const double *)p1_data;
    const double b = *(const double *)p2_data;
    *(double *)result_data = pow(a, b);
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

    const nmo_operation_desc_t operations[] = {
        /* Signed integers */
        NMO_OP_ROW_BINARY(NMO_OP_GUID_ADD, "Add", "INT8 addition: a + b", CKPGUID_INT8, 100, op_add_int8),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_SUBTRACT, "Subtract", "INT8 subtraction: a - b", CKPGUID_INT8, 100, op_subtract_int8),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MULTIPLY, "Multiply", "INT8 multiplication: a * b", CKPGUID_INT8, 100, op_multiply_int8),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_DIVIDE, "Divide", "INT8 division: a / b", CKPGUID_INT8, 100, op_divide_int8),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MODULO, "Modulo", "INT8 modulo: a %% b", CKPGUID_INT8, 100, op_modulo_int8),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_NEGATE, "Negate", "INT8 negation: -a", CKPGUID_INT8, 100, op_negate_int8),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_ABS, "Abs", "INT8 absolute value: |a|", CKPGUID_INT8, 100, op_abs_int8),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_POWER, "Power", "INT8 power: a^b", CKPGUID_INT8, 100, op_power_int8),

        NMO_OP_ROW_BINARY(NMO_OP_GUID_ADD, "Add", "INT16 addition: a + b", CKPGUID_INT16, 100, op_add_int16),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_SUBTRACT, "Subtract", "INT16 subtraction: a - b", CKPGUID_INT16, 100, op_subtract_int16),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MULTIPLY, "Multiply", "INT16 multiplication: a * b", CKPGUID_INT16, 100, op_multiply_int16),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_DIVIDE, "Divide", "INT16 division: a / b", CKPGUID_INT16, 100, op_divide_int16),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MODULO, "Modulo", "INT16 modulo: a % b", CKPGUID_INT16, 100, op_modulo_int16),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_NEGATE, "Negate", "INT16 negation: -a", CKPGUID_INT16, 100, op_negate_int16),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_ABS, "Abs", "INT16 absolute value: |a|", CKPGUID_INT16, 100, op_abs_int16),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_POWER, "Power", "INT16 power: a^b", CKPGUID_INT16, 100, op_power_int16),

        /* Keep existing INT32 operations (same behavior) */
        NMO_OP_ROW_BINARY(NMO_OP_GUID_ADD, "Add", "Integer addition: a + b", CKPGUID_INT, 100, op_add_int),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_SUBTRACT, "Subtract", "Integer subtraction: a - b", CKPGUID_INT, 100, op_subtract_int),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MULTIPLY, "Multiply", "Integer multiplication: a * b", CKPGUID_INT, 100, op_multiply_int),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_DIVIDE, "Divide", "Integer division: a / b", CKPGUID_INT, 100, op_divide_int),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MODULO, "Modulo", "Integer modulo: a % b", CKPGUID_INT, 100, op_modulo_int),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_NEGATE, "Negate", "Integer negation: -a", CKPGUID_INT, 100, op_negate_int),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_ABS, "Abs", "Integer absolute value: |a|", CKPGUID_INT, 100, op_abs_int),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_POWER, "Power", "Integer power: a^b", CKPGUID_INT, 100, op_power_int),

        NMO_OP_ROW_BINARY(NMO_OP_GUID_ADD, "Add", "INT64 addition: a + b", CKPGUID_INT64, 100, op_add_int64),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_SUBTRACT, "Subtract", "INT64 subtraction: a - b", CKPGUID_INT64, 100, op_subtract_int64),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MULTIPLY, "Multiply", "INT64 multiplication: a * b", CKPGUID_INT64, 100, op_multiply_int64),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_DIVIDE, "Divide", "INT64 division: a / b", CKPGUID_INT64, 100, op_divide_int64),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MODULO, "Modulo", "INT64 modulo: a % b", CKPGUID_INT64, 100, op_modulo_int64),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_NEGATE, "Negate", "INT64 negation: -a", CKPGUID_INT64, 100, op_negate_int64),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_ABS, "Abs", "INT64 absolute value: |a|", CKPGUID_INT64, 100, op_abs_int64),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_POWER, "Power", "INT64 power: a^b", CKPGUID_INT64, 100, op_power_int64),

        /* Unsigned integers */
        NMO_OP_ROW_BINARY(NMO_OP_GUID_ADD, "Add", "UINT8 addition: a + b", CKPGUID_UINT8, 100, op_add_uint8),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_SUBTRACT, "Subtract", "UINT8 subtraction: a - b", CKPGUID_UINT8, 100, op_subtract_uint8),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MULTIPLY, "Multiply", "UINT8 multiplication: a * b", CKPGUID_UINT8, 100, op_multiply_uint8),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_DIVIDE, "Divide", "UINT8 division: a / b", CKPGUID_UINT8, 100, op_divide_uint8),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MODULO, "Modulo", "UINT8 modulo: a % b", CKPGUID_UINT8, 100, op_modulo_uint8),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_POWER, "Power", "UINT8 power: a^b", CKPGUID_UINT8, 100, op_power_uint8),

        NMO_OP_ROW_BINARY(NMO_OP_GUID_ADD, "Add", "UINT16 addition: a + b", CKPGUID_UINT16, 100, op_add_uint16),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_SUBTRACT, "Subtract", "UINT16 subtraction: a - b", CKPGUID_UINT16, 100, op_subtract_uint16),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MULTIPLY, "Multiply", "UINT16 multiplication: a * b", CKPGUID_UINT16, 100, op_multiply_uint16),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_DIVIDE, "Divide", "UINT16 division: a / b", CKPGUID_UINT16, 100, op_divide_uint16),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MODULO, "Modulo", "UINT16 modulo: a %% b", CKPGUID_UINT16, 100, op_modulo_uint16),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_POWER, "Power", "UINT16 power: a^b", CKPGUID_UINT16, 100, op_power_uint16),

        NMO_OP_ROW_BINARY(NMO_OP_GUID_ADD, "Add", "UINT32 addition: a + b", CKPGUID_UINT32, 100, op_add_uint32),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_SUBTRACT, "Subtract", "UINT32 subtraction: a - b", CKPGUID_UINT32, 100, op_subtract_uint32),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MULTIPLY, "Multiply", "UINT32 multiplication: a * b", CKPGUID_UINT32, 100, op_multiply_uint32),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_DIVIDE, "Divide", "UINT32 division: a / b", CKPGUID_UINT32, 100, op_divide_uint32),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MODULO, "Modulo", "UINT32 modulo: a %% b", CKPGUID_UINT32, 100, op_modulo_uint32),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_POWER, "Power", "UINT32 power: a^b", CKPGUID_UINT32, 100, op_power_uint32),

        NMO_OP_ROW_BINARY(NMO_OP_GUID_ADD, "Add", "UINT64 addition: a + b", CKPGUID_UINT64, 100, op_add_uint64),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_SUBTRACT, "Subtract", "UINT64 subtraction: a - b", CKPGUID_UINT64, 100, op_subtract_uint64),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MULTIPLY, "Multiply", "UINT64 multiplication: a * b", CKPGUID_UINT64, 100, op_multiply_uint64),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_DIVIDE, "Divide", "UINT64 division: a / b", CKPGUID_UINT64, 100, op_divide_uint64),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MODULO, "Modulo", "UINT64 modulo: a %% b", CKPGUID_UINT64, 100, op_modulo_uint64),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_POWER, "Power", "UINT64 power: a^b", CKPGUID_UINT64, 100, op_power_uint64),

        /* Float */
        NMO_OP_ROW_BINARY(NMO_OP_GUID_ADD, "Add", "Float addition: a + b", CKPGUID_FLOAT, 100, op_add_float),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_SUBTRACT, "Subtract", "Float subtraction: a - b", CKPGUID_FLOAT, 100, op_subtract_float),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MULTIPLY, "Multiply", "Float multiplication: a * b", CKPGUID_FLOAT, 100, op_multiply_float),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_DIVIDE, "Divide", "Float division: a / b", CKPGUID_FLOAT, 100, op_divide_float),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MODULO, "Modulo", "Float modulo: fmod(a, b)", CKPGUID_FLOAT, 100, op_modulo_float),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_NEGATE, "Negate", "Float negation: -a", CKPGUID_FLOAT, 100, op_negate_float),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_ABS, "Abs", "Float absolute value: |a|", CKPGUID_FLOAT, 100, op_abs_float),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_POWER, "Power", "Float power: a^b", CKPGUID_FLOAT, 100, op_power_float),

        /* CK2 derived float types: use float implementation but keep result type */
        NMO_OP_ROW_BINARY(NMO_OP_GUID_ADD, "Add", "Angle addition: a + b", CKPGUID_ANGLE, 110, op_add_float),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_SUBTRACT, "Subtract", "Angle subtraction: a - b", CKPGUID_ANGLE, 110, op_subtract_float),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MULTIPLY, "Multiply", "Angle multiplication: a * b", CKPGUID_ANGLE, 110, op_multiply_float),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_DIVIDE, "Divide", "Angle division: a / b", CKPGUID_ANGLE, 110, op_divide_float),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MODULO, "Modulo", "Angle modulo: fmod(a, b)", CKPGUID_ANGLE, 110, op_modulo_float),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_NEGATE, "Negate", "Angle negation: -a", CKPGUID_ANGLE, 110, op_negate_float),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_ABS, "Abs", "Angle absolute value: |a|", CKPGUID_ANGLE, 110, op_abs_float),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_POWER, "Power", "Angle power: a^b", CKPGUID_ANGLE, 110, op_power_float),

        NMO_OP_ROW_BINARY(NMO_OP_GUID_ADD, "Add", "Percentage addition: a + b", CKPGUID_PERCENTAGE, 110, op_add_float),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_SUBTRACT, "Subtract", "Percentage subtraction: a - b", CKPGUID_PERCENTAGE, 110, op_subtract_float),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MULTIPLY, "Multiply", "Percentage multiplication: a * b", CKPGUID_PERCENTAGE, 110, op_multiply_float),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_DIVIDE, "Divide", "Percentage division: a / b", CKPGUID_PERCENTAGE, 110, op_divide_float),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MODULO, "Modulo", "Percentage modulo: fmod(a, b)", CKPGUID_PERCENTAGE, 110, op_modulo_float),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_NEGATE, "Negate", "Percentage negation: -a", CKPGUID_PERCENTAGE, 110, op_negate_float),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_ABS, "Abs", "Percentage absolute value: |a|", CKPGUID_PERCENTAGE, 110, op_abs_float),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_POWER, "Power", "Percentage power: a^b", CKPGUID_PERCENTAGE, 110, op_power_float),

        /* Double */
        NMO_OP_ROW_BINARY(NMO_OP_GUID_ADD, "Add", "Double addition: a + b", CKPGUID_DOUBLE, 100, op_add_double),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_SUBTRACT, "Subtract", "Double subtraction: a - b", CKPGUID_DOUBLE, 100, op_subtract_double),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MULTIPLY, "Multiply", "Double multiplication: a * b", CKPGUID_DOUBLE, 100, op_multiply_double),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_DIVIDE, "Divide", "Double division: a / b", CKPGUID_DOUBLE, 100, op_divide_double),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_MODULO, "Modulo", "Double modulo: fmod(a, b)", CKPGUID_DOUBLE, 100, op_modulo_double),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_NEGATE, "Negate", "Double negation: -a", CKPGUID_DOUBLE, 100, op_negate_double),
        NMO_OP_ROW_UNARY(NMO_OP_GUID_ABS, "Abs", "Double absolute value: |a|", CKPGUID_DOUBLE, 100, op_abs_double),
        NMO_OP_ROW_BINARY(NMO_OP_GUID_POWER, "Power", "Double power: a^b", CKPGUID_DOUBLE, 100, op_power_double),
    };

    return nmo_register_operation_table(operation_registry, operations,
                                        sizeof(operations) / sizeof(operations[0]),
                                        type_registry);
}
