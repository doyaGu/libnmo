/**
 * @file nmo_builtin_operations.h
 * @brief Builtin operation definitions (Phase 6.1.4)
 *
 * Defines 50+ builtin operations following CKParameter operation system:
 * - Arithmetic: Add, Subtract, Multiply, Divide, Modulo, Negate, Abs, Power
 * - Logic: And, Or, Not, Xor, Equal, NotEqual
 * - Comparison: Less, LessEqual, Greater, GreaterEqual, Min, Max
 * - Bitwise: BitAnd, BitOr, BitXor, BitNot, ShiftLeft, ShiftRight, RotateLeft, RotateRight
 * - Trigonometry: Sin, Cos, Tan, Asin, Acos, Atan
 * - Vector: VectorAdd, VectorSub, VectorMul, VectorDiv, VectorDot, VectorCross
 */

#ifndef NMO_BUILTIN_OPERATIONS_H
#define NMO_BUILTIN_OPERATIONS_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_type_system.h"
#include "type/nmo_builtin_type_guids.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Operation Family GUIDs
 * ============================================================================ */

/* Arithmetic operations */
#define NMO_OP_GUID_ADD        NMO_GUID(0x6FED1C00, 0x00000001)
#define NMO_OP_GUID_SUBTRACT   NMO_GUID(0x6FED1C00, 0x00000002)
#define NMO_OP_GUID_MULTIPLY   NMO_GUID(0x6FED1C00, 0x00000003)
#define NMO_OP_GUID_DIVIDE     NMO_GUID(0x6FED1C00, 0x00000004)
#define NMO_OP_GUID_MODULO     NMO_GUID(0x6FED1C00, 0x00000005)
#define NMO_OP_GUID_NEGATE     NMO_GUID(0x6FED1C00, 0x00000006)
#define NMO_OP_GUID_ABS        NMO_GUID(0x6FED1C00, 0x00000007)
#define NMO_OP_GUID_POWER      NMO_GUID(0x6FED1C00, 0x00000008)

/* Logic operations */
#define NMO_OP_GUID_AND        NMO_GUID(0x6FED1C01, 0x00000001)
#define NMO_OP_GUID_OR         NMO_GUID(0x6FED1C01, 0x00000002)
#define NMO_OP_GUID_NOT        NMO_GUID(0x6FED1C01, 0x00000003)
#define NMO_OP_GUID_XOR        NMO_GUID(0x6FED1C01, 0x00000004)
#define NMO_OP_GUID_EQUAL      NMO_GUID(0x6FED1C01, 0x00000005)
#define NMO_OP_GUID_NOT_EQUAL  NMO_GUID(0x6FED1C01, 0x00000006)

/* Comparison operations */
#define NMO_OP_GUID_LESS       NMO_GUID(0x6FED1C02, 0x00000001)
#define NMO_OP_GUID_LESS_EQ    NMO_GUID(0x6FED1C02, 0x00000002)
#define NMO_OP_GUID_GREATER    NMO_GUID(0x6FED1C02, 0x00000003)
#define NMO_OP_GUID_GREATER_EQ NMO_GUID(0x6FED1C02, 0x00000004)
#define NMO_OP_GUID_MIN        NMO_GUID(0x6FED1C02, 0x00000005)
#define NMO_OP_GUID_MAX        NMO_GUID(0x6FED1C02, 0x00000006)

/* Bitwise operations */
#define NMO_OP_GUID_BIT_AND    NMO_GUID(0x6FED1C03, 0x00000001)
#define NMO_OP_GUID_BIT_OR     NMO_GUID(0x6FED1C03, 0x00000002)
#define NMO_OP_GUID_BIT_XOR    NMO_GUID(0x6FED1C03, 0x00000003)
#define NMO_OP_GUID_BIT_NOT    NMO_GUID(0x6FED1C03, 0x00000004)
#define NMO_OP_GUID_SHIFT_LEFT  NMO_GUID(0x6FED1C03, 0x00000005)
#define NMO_OP_GUID_SHIFT_RIGHT NMO_GUID(0x6FED1C03, 0x00000006)
#define NMO_OP_GUID_ROTATE_LEFT NMO_GUID(0x6FED1C03, 0x00000007)
#define NMO_OP_GUID_ROTATE_RIGHT NMO_GUID(0x6FED1C03, 0x00000008)

/* Trigonometry operations */
#define NMO_OP_GUID_SIN        NMO_GUID(0x6FED1C04, 0x00000001)
#define NMO_OP_GUID_COS        NMO_GUID(0x6FED1C04, 0x00000002)
#define NMO_OP_GUID_TAN        NMO_GUID(0x6FED1C04, 0x00000003)
#define NMO_OP_GUID_ASIN       NMO_GUID(0x6FED1C04, 0x00000004)
#define NMO_OP_GUID_ACOS       NMO_GUID(0x6FED1C04, 0x00000005)
#define NMO_OP_GUID_ATAN       NMO_GUID(0x6FED1C04, 0x00000006)

/* Vector operations */
#define NMO_OP_GUID_VECTOR_ADD   NMO_GUID(0x6FED1C05, 0x00000001)
#define NMO_OP_GUID_VECTOR_SUB   NMO_GUID(0x6FED1C05, 0x00000002)
#define NMO_OP_GUID_VECTOR_MUL   NMO_GUID(0x6FED1C05, 0x00000003)
#define NMO_OP_GUID_VECTOR_DIV   NMO_GUID(0x6FED1C05, 0x00000004)
#define NMO_OP_GUID_VECTOR_DOT   NMO_GUID(0x6FED1C05, 0x00000005)
#define NMO_OP_GUID_VECTOR_CROSS NMO_GUID(0x6FED1C05, 0x00000006)

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

/**
 * @brief Register vector operations
 */
NMO_API nmo_status_t nmo_register_vector_operations(
    nmo_operation_registry_t *operation_registry,
    const nmo_type_registry_t *type_registry
);

#ifdef __cplusplus
}
#endif

#endif /* NMO_BUILTIN_OPERATIONS_H */
