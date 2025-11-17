/**
 * @file nmo_param_meta.h
 * @brief Parameter metadata structures for Virtools Parameter system integration
 *
 * This module defines metadata structures to support the Virtools Parameter system.
 * These structures allow schema types to carry Parameter-specific information
 * (GUID, derived types, default sizes, etc.) needed for full Parameter reconstruction.
 */

#ifndef NMO_PARAM_META_H
#define NMO_PARAM_META_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "core/nmo_error.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct nmo_schema_registry;
struct nmo_arena;

/* =============================================================================
 * PARAMETER TYPE CLASSIFICATION
 * ============================================================================= */

/**
 * @brief Parameter kind enumeration
 *
 * Categorizes parameter types according to Virtools conventions.
 * Maps to different CKParameterTypeDesc structures in original SDK.
 */
typedef enum nmo_param_kind {
    NMO_PARAM_SCALAR,      /**< Scalar types (int, float, bool, string) */
    NMO_PARAM_ENUM,        /**< Enumeration (CKEnumStruct) */
    NMO_PARAM_FLAGS,       /**< Bit flags (CKFlagsStruct) */
    NMO_PARAM_STRUCT,      /**< Composite structure (CKStructStruct) */
    NMO_PARAM_OBJECT_REF,  /**< Reference to Virtools object */
} nmo_param_kind_t;

/* =============================================================================
 * PARAMETER METADATA
 * ============================================================================= */

/**
 * @brief Parameter metadata structure
 *
 * Equivalent to key fields from CKParameterTypeDesc in Virtools SDK.
 * Attached to nmo_schema_type_t to enable Parameter system reconstruction.
 */
typedef struct nmo_param_meta {
    nmo_param_kind_t kind;      /**< Parameter kind */
    nmo_guid_t guid;            /**< Parameter type GUID (CKPGUID) */
    nmo_guid_t derived_from;    /**< Base parameter type GUID (0 if none) */
    uint32_t default_size;      /**< Default size in bytes */
    uint32_t class_id;          /**< For OBJECT_REF: target CK_CLASSID */
    uint32_t flags;             /**< Parameter-specific flags */
    
    /* Extended information (optional) */
    const char *creator_plugin; /**< Plugin that defined this type (can be NULL) */
    const char *ui_name;        /**< Display name for UI (can be NULL) */
    const char *description;    /**< Type description (can be NULL) */
} nmo_param_meta_t;

/* =============================================================================
 * PARAMETER FLAGS
 * ============================================================================= */

/**
 * @brief Parameter type flags (equivalent to CK_PARAMETERTYPE_FLAGS)
 *
 * Control parameter behavior, visibility, and usage.
 */
typedef enum nmo_param_flags {
    NMO_PARAM_FLAG_HIDDEN       = 0x00000001, /**< Hidden from UI */
    NMO_PARAM_FLAG_EDITOR_ONLY  = 0x00000002, /**< Editor-only, not runtime */
    NMO_PARAM_FLAG_DERIVED      = 0x00000004, /**< Derived from another type */
    NMO_PARAM_FLAG_CUSTOM       = 0x00000008, /**< Custom/plugin-defined */
    NMO_PARAM_FLAG_READONLY     = 0x00000010, /**< Read-only in UI */
    NMO_PARAM_FLAG_ANIMATABLE   = 0x00000020, /**< Can be animated */
    NMO_PARAM_FLAG_SERIALIZABLE = 0x00000040, /**< Serialized to file */
} nmo_param_flags_t;

/* =============================================================================
 * COMMON PARAMETER GUIDs (CKPGUID_*)
 * ============================================================================= */

/* Helper macro for GUID creation */
#define CKPGUID(d1, d2) ((nmo_guid_t){(d1), (d2)})

/* Scalar types */
#define CKPGUID_INT         CKPGUID(0x00000001, 0x00000000)
#define CKPGUID_FLOAT       CKPGUID(0x00000002, 0x00000000)
#define CKPGUID_BOOL        CKPGUID(0x00000003, 0x00000000)
#define CKPGUID_STRING      CKPGUID(0x00000004, 0x00000000)
#define CKPGUID_KEY         CKPGUID(0x00000005, 0x00000000)

/* Math types */
#define CKPGUID_VECTOR      CKPGUID(0x00000010, 0x00000000)
#define CKPGUID_2DVECTOR    CKPGUID(0x00000011, 0x00000000)
#define CKPGUID_QUATERNION  CKPGUID(0x00000012, 0x00000000)
#define CKPGUID_MATRIX      CKPGUID(0x00000013, 0x00000000)
#define CKPGUID_COLOR       CKPGUID(0x00000014, 0x00000000)
#define CKPGUID_BOX         CKPGUID(0x00000015, 0x00000000)
#define CKPGUID_RECT        CKPGUID(0x00000016, 0x00000000)

/* ID types */
#define CKPGUID_OBJECT      CKPGUID(0x00000020, 0x00000000)
#define CKPGUID_ID          CKPGUID(0x00000021, 0x00000000)

/* Enum types (examples) */
#define CKPGUID_BLENDMODE         CKPGUID(0x00000100, 0x00000000)
#define CKPGUID_FILTERMODE        CKPGUID(0x00000101, 0x00000000)
#define CKPGUID_COMPARISONFUNCTION CKPGUID(0x00000102, 0x00000000)

/* Struct types (examples) */
#define CKPGUID_MATERIAL_TEXBLEND CKPGUID(0x00000200, 0x00000000)

/* Note: Actual GUIDs should match Virtools SDK constants */

/* =============================================================================
 * REGISTRATION API
 * ============================================================================= */

/**
 * @brief Register all core parameter types with metadata
 *
 * Registers 14 parameter types with complete metadata:
 *   - Scalars: int, float, bool, string, key (5 types)
 *   - Math: Vector, 2DVector, Quaternion, Matrix, Color, Box, Rect (7 types)
 *   - References: Object, ID (2 types)
 *
 * Each type includes GUID, kind, default_size, flags, and optional UI info.
 *
 * @param registry Schema registry to register into
 * @param arena Arena for allocations
 * @return NMO_OK on success, error code otherwise
 */
NMO_API nmo_result_t nmo_register_param_types(
    struct nmo_schema_registry *registry,
    struct nmo_arena *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_PARAM_META_H */
