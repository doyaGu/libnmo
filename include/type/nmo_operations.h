/**
 * @file nmo_operations.h
 * @brief Builtin operation definitions (Phase 6.1.4)
 *
 * Defines 50+ builtin operations following CKParameter operation system:
 * - Arithmetic: Add, Subtract, Multiply, Divide, Modulo, Negate, Abs, Power
 * - Logic: And, Or, Not, Xor, Equal, NotEqual
 * - Comparison: Less, LessEqual, Greater, GreaterEqual, Min, Max
 * - Bitwise: BitAnd, BitOr, BitXor, BitNot, ShiftLeft, ShiftRight, RotateLeft, RotateRight
 * - Trigonometry: Sin, Cos, Tan, Asin, Acos, Atan
 * - Vector: Add/Sub/Mul/Div (type variants of arithmetic), DotProduct, CrossProduct
 */

#ifndef NMO_OPERATIONS_H
#define NMO_OPERATIONS_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_guids.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Operation Family GUIDs
 *
 * Matched operations use real Virtools CK2 GUIDs from CKParameterOperations.
 * libnmo-extension operations (no Virtools equivalent) use synthetic 0x6FED1C0x.
 * ============================================================================ */

/* Arithmetic operations — Virtools GUIDs */
#define NMO_OP_GUID_ADD        NMO_GUID(0x33CC6B49, 0x3589282B) /* Addition */
#define NMO_OP_GUID_SUBTRACT   NMO_GUID(0x67641171, 0x6499077A) /* Subtraction */
#define NMO_OP_GUID_MULTIPLY   NMO_GUID(0x38996B85, 0x334E35C2) /* Multiplication */
#define NMO_OP_GUID_DIVIDE     NMO_GUID(0x748C3502, 0x0FEA0CF1) /* Division */
#define NMO_OP_GUID_MODULO     NMO_GUID(0x2A3F4AB6, 0x753A4A20) /* Modulo */
#define NMO_OP_GUID_NEGATE     NMO_GUID(0x599C2CB6, 0x3E7D4DE8) /* Opposite */
#define NMO_OP_GUID_ABS        NMO_GUID(0x1ADA7470, 0x62097629) /* Absolute Value */
#define NMO_OP_GUID_POWER      NMO_GUID(0x6FED1C00, 0x00000008) /* libnmo extension */

/* Logic operations — Virtools GUIDs */
#define NMO_OP_GUID_AND        NMO_GUID(0x4A450980, 0x6B230426) /* And */
#define NMO_OP_GUID_OR         NMO_GUID(0x26D81CC1, 0x340273B0) /* Or */
#define NMO_OP_GUID_NOT        NMO_GUID(0x0E5C02E8, 0x3AAD7BB8) /* Not */
#define NMO_OP_GUID_XOR        NMO_GUID(0x014405EB, 0x2E087EEC) /* Xor */
#define NMO_OP_GUID_EQUAL      NMO_GUID(0x6C62476B, 0x7A922973) /* Equal */
#define NMO_OP_GUID_NOT_EQUAL  NMO_GUID(0xCF50ABBB, 0x0B50A0C8) /* Not Equal */

/* Comparison operations — Virtools GUIDs */
#define NMO_OP_GUID_LESS       NMO_GUID(0x74A36EF4, 0x61262B24) /* Inferior */
#define NMO_OP_GUID_LESS_EQ    NMO_GUID(0x287F6201, 0x7FC624CA) /* Inferior or Equal */
#define NMO_OP_GUID_GREATER    NMO_GUID(0x05B92E50, 0x5FC91D82) /* Superior */
#define NMO_OP_GUID_GREATER_EQ NMO_GUID(0x16581103, 0x0A1977BB) /* Superior or Equal */
#define NMO_OP_GUID_MIN        NMO_GUID(0x6EA214CB, 0x43BE6B56) /* Min */
#define NMO_OP_GUID_MAX        NMO_GUID(0x7C2E01A8, 0x6E702206) /* Max */

/* Bitwise operations — libnmo extensions (no Virtools equivalent) */
#define NMO_OP_GUID_BIT_AND     NMO_GUID(0x6FED1C03, 0x00000001)
#define NMO_OP_GUID_BIT_OR      NMO_GUID(0x6FED1C03, 0x00000002)
#define NMO_OP_GUID_BIT_XOR     NMO_GUID(0x6FED1C03, 0x00000003)
#define NMO_OP_GUID_BIT_NOT     NMO_GUID(0x6FED1C03, 0x00000004)
#define NMO_OP_GUID_SHIFT_LEFT  NMO_GUID(0x6FED1C03, 0x00000005)
#define NMO_OP_GUID_SHIFT_RIGHT NMO_GUID(0x6FED1C03, 0x00000006)
#define NMO_OP_GUID_ROTATE_LEFT NMO_GUID(0x6FED1C03, 0x00000007)
#define NMO_OP_GUID_ROTATE_RIGHT NMO_GUID(0x6FED1C03, 0x00000008)

/* Trigonometry operations — Virtools GUIDs */
#define NMO_OP_GUID_SIN        NMO_GUID(0x7EC428B2, 0x5B8F0AD1) /* Sine */
#define NMO_OP_GUID_COS        NMO_GUID(0x51D66B9A, 0x03514475) /* Cosine */
#define NMO_OP_GUID_TAN        NMO_GUID(0x31E02E80, 0x7475246B) /* Tangent */
#define NMO_OP_GUID_ASIN       NMO_GUID(0x07181CEF, 0x4C632C22) /* Arc Sine */
#define NMO_OP_GUID_ACOS       NMO_GUID(0x7EED1D16, 0x345B7184) /* Arc Cosine */
#define NMO_OP_GUID_ATAN       NMO_GUID(0x30547441, 0x611F682C) /* Arc Tangent 2 */

/* Vector operations — merged into Virtools arithmetic or own Virtools GUIDs.
 * VectorAdd/Sub/Mul/Div are type variants of Addition/Subtraction/etc.
 * DotProduct and CrossProduct have their own Virtools operation GUIDs. */
#define NMO_OP_GUID_VECTOR_DOT   NMO_GUID(0x229F54E2, 0x751350BD) /* Dot Product */
#define NMO_OP_GUID_VECTOR_CROSS NMO_GUID(0x3BE05DF3, 0x143A49E0) /* Cross Product */

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
 * @brief Patch vtables for JSON-loaded types (e.g. Time)
 *
 * Call after extension loading to attach vtables to types that were
 * registered from JSON data rather than from nmo_register_builtin_types.
 */
NMO_API nmo_status_t nmo_builtin_types_patch_vtables(
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

#endif /* NMO_OPERATIONS_H */
