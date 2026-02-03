/**
 * @file builtin_operations.h
 * @brief Builtin operation definitions (Phase 6.1.4)
 *
 * Defines 50+ builtin operations following CKParameter operation system:
 * - Arithmetic: Add, Subtract, Multiply, Divide, Modulo, Negate, Abs, Power
 * - Logic: And, Or, Not, Xor, Equal, NotEqual
 * - Comparison: Less, LessEqual, Greater, GreaterEqual, Min, Max
 * - Bitwise: BitAnd, BitOr, BitXor, BitNot, ShiftLeft, ShiftRight, RotateLeft
 * - Trigonometry: Sin, Cos, Tan, Asin, Acos, Atan
 * - Vector: VectorAdd, VectorSub, VectorMul, VectorDiv, VectorDot, VectorCross, etc.
 */

#ifndef NMO_BUILTIN_OPERATIONS_H
#define NMO_BUILTIN_OPERATIONS_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "type/operation_system.h"
#include "type/type_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Operation Family GUIDs
 * ============================================================================ */

/* Arithmetic operations */
#define NMO_OP_GUID_ADD        ((nmo_guid_t){0x6FED1C00, 0x00000001})
#define NMO_OP_GUID_SUBTRACT   ((nmo_guid_t){0x6FED1C00, 0x00000002})
#define NMO_OP_GUID_MULTIPLY   ((nmo_guid_t){0x6FED1C00, 0x00000003})
#define NMO_OP_GUID_DIVIDE     ((nmo_guid_t){0x6FED1C00, 0x00000004})
#define NMO_OP_GUID_MODULO     ((nmo_guid_t){0x6FED1C00, 0x00000005})
#define NMO_OP_GUID_NEGATE     ((nmo_guid_t){0x6FED1C00, 0x00000006})
#define NMO_OP_GUID_ABS        ((nmo_guid_t){0x6FED1C00, 0x00000007})
#define NMO_OP_GUID_POWER      ((nmo_guid_t){0x6FED1C00, 0x00000008})

/* Logic operations */
#define NMO_OP_GUID_AND        ((nmo_guid_t){0x6FED1C01, 0x00000001})
#define NMO_OP_GUID_OR         ((nmo_guid_t){0x6FED1C01, 0x00000002})
#define NMO_OP_GUID_NOT        ((nmo_guid_t){0x6FED1C01, 0x00000003})
#define NMO_OP_GUID_XOR        ((nmo_guid_t){0x6FED1C01, 0x00000004})
#define NMO_OP_GUID_EQUAL      ((nmo_guid_t){0x6FED1C01, 0x00000005})
#define NMO_OP_GUID_NOT_EQUAL  ((nmo_guid_t){0x6FED1C01, 0x00000006})

/* Comparison operations */
#define NMO_OP_GUID_LESS       ((nmo_guid_t){0x6FED1C02, 0x00000001})
#define NMO_OP_GUID_LESS_EQ    ((nmo_guid_t){0x6FED1C02, 0x00000002})
#define NMO_OP_GUID_GREATER    ((nmo_guid_t){0x6FED1C02, 0x00000003})
#define NMO_OP_GUID_GREATER_EQ ((nmo_guid_t){0x6FED1C02, 0x00000004})
#define NMO_OP_GUID_MIN        ((nmo_guid_t){0x6FED1C02, 0x00000005})
#define NMO_OP_GUID_MAX        ((nmo_guid_t){0x6FED1C02, 0x00000006})

/* Bitwise operations */
#define NMO_OP_GUID_BIT_AND    ((nmo_guid_t){0x6FED1C03, 0x00000001})
#define NMO_OP_GUID_BIT_OR     ((nmo_guid_t){0x6FED1C03, 0x00000002})
#define NMO_OP_GUID_BIT_XOR    ((nmo_guid_t){0x6FED1C03, 0x00000003})
#define NMO_OP_GUID_BIT_NOT    ((nmo_guid_t){0x6FED1C03, 0x00000004})
#define NMO_OP_GUID_SHIFT_LEFT ((nmo_guid_t){0x6FED1C03, 0x00000005})
#define NMO_OP_GUID_SHIFT_RIGHT ((nmo_guid_t){0x6FED1C03, 0x00000006})
#define NMO_OP_GUID_ROTATE_LEFT ((nmo_guid_t){0x6FED1C03, 0x00000007})

/* Trigonometry operations */
#define NMO_OP_GUID_SIN        ((nmo_guid_t){0x6FED1C04, 0x00000001})
#define NMO_OP_GUID_COS        ((nmo_guid_t){0x6FED1C04, 0x00000002})
#define NMO_OP_GUID_TAN        ((nmo_guid_t){0x6FED1C04, 0x00000003})
#define NMO_OP_GUID_ASIN       ((nmo_guid_t){0x6FED1C04, 0x00000004})
#define NMO_OP_GUID_ACOS       ((nmo_guid_t){0x6FED1C04, 0x00000005})
#define NMO_OP_GUID_ATAN       ((nmo_guid_t){0x6FED1C04, 0x00000006})

/* ============================================================================
 * Builtin Type GUIDs (common types)
 * ============================================================================ */

#define NMO_TYPE_GUID_INT      ((nmo_guid_t){0x6FED1D00, 0x00000001})
#define NMO_TYPE_GUID_FLOAT    ((nmo_guid_t){0x6FED1D00, 0x00000002})
#define NMO_TYPE_GUID_BOOL     ((nmo_guid_t){0x6FED1D00, 0x00000003})
#define NMO_TYPE_GUID_VECTOR2  ((nmo_guid_t){0x6FED1D00, 0x00000004})
#define NMO_TYPE_GUID_VECTOR3  ((nmo_guid_t){0x6FED1D00, 0x00000005})
#define NMO_TYPE_GUID_VECTOR4  ((nmo_guid_t){0x6FED1D00, 0x00000006})
#define NMO_TYPE_GUID_QUATERNION ((nmo_guid_t){0x6FED1D00, 0x00000007})
#define NMO_TYPE_GUID_MATRIX     ((nmo_guid_t){0x6FED1D00, 0x00000008})
#define NMO_TYPE_GUID_COLOR      ((nmo_guid_t){0x6FED1D00, 0x00000009})

/* ============================================================================
 * Registration Functions
 * ============================================================================ */

/**
 * @brief Register all builtin operations
 *
 * Registers 50+ builtin operations in the operation registry.
 * Requires types to be registered in type_registry first.
 *
 * @param operation_registry Operation registry
 * @param type_registry      Type registry with builtin types
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_status_t nmo_register_builtin_operations(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry
);

/**
 * @brief Register builtin types
 *
 * Registers common builtin types (INT, FLOAT, BOOL) and Virtools math types
 * (VxVector2/3/4, VxQuaternion, VxMatrix, VxColor).
 *
 * @param type_registry Type registry
 * @return NMO_OK on success, error code on failure
 */
NMO_API nmo_status_t nmo_register_builtin_types(
    nmo_type_registry_t *type_registry
);

/**
 * @brief Register arithmetic operations
 */
NMO_API nmo_status_t nmo_register_arithmetic_operations(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry
);

/**
 * @brief Register logic operations
 */
NMO_API nmo_status_t nmo_register_logic_operations(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry
);

/**
 * @brief Register comparison operations
 */
NMO_API nmo_status_t nmo_register_comparison_operations(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry
);

/**
 * @brief Register bitwise operations
 */
NMO_API nmo_status_t nmo_register_bitwise_operations(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry
);

/**
 * @brief Register trigonometry operations
 */
NMO_API nmo_status_t nmo_register_trigonometry_operations(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry
);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BUILTIN_OPERATIONS_H */
