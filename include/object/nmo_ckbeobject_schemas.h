/**
 * @file nmo_ckbeobject_schemas.h
 * @brief Public API for CKBeObject schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKBeObject.
 * CKBeObject is the base class for behavioral objects (objects with scripts/attributes).
 * 
 * CKBeObject adds scripts, priority, attributes, and single-activity flags on top of CKObject.
 * Many derived classes (CKRenderObject, CKMesh, CKTexture, etc.) do not override
 * Load/Save and inherit this serialization behavior directly.
 */

#ifndef NMO_CKBEOBJECT_SCHEMAS_H
#define NMO_CKBEOBJECT_SCHEMAS_H

#include "nmo_types.h"
#include "nmo_cksceneobject_schemas.h"
 #include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;
typedef struct nmo_type_descriptor_t nmo_type_descriptor_t;

/* =============================================================================
 * CKBeObject STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKBeObject state
 * 
 * Represents the behavioral object data including scripts, priority, and attributes.
 * Corresponds to data serialized in CKBeObject::Load/Save.
 * 
 * Reference: reference/src/CKBeObject.cpp:400-700
 */
typedef struct nmo_ckbeobject_state {
    /* Base class state */
    nmo_cksceneobject_state_t base;  /**< CKSceneObject base state */
    
    /* Scripts */
    nmo_object_id_t *script_ids;  /**< Array of script behavior IDs */
    uint32_t script_count;         /**< Number of scripts */
    
    /* Priority */
    int32_t priority;              /**< Execution priority (0 = default) */
    
    /* Attributes */
    nmo_object_id_t *attribute_parameter_ids;  /**< Array of attribute parameter IDs */
    uint32_t *attribute_types;                 /**< Array of attribute type IDs */
    uint32_t attribute_count;                  /**< Number of attributes */

    /* Attribute parameter sub-chunks (non-file mode) */
    nmo_chunk_t **attribute_chunks;            /**< Optional sub-chunks per attribute */
    uint32_t attribute_chunk_count;            /**< Number of attribute sub-chunks */

    /* Single activity flags (file save only) */
    uint8_t has_single_activity;               /**< True if single activity flags exist */
    uint32_t single_activity_flags;            /**< Scene object activity flags */

    /* Legacy attribute payload (CK_STATESAVE_ATTRIBUTES) */
    void *legacy_attributes_raw;               /**< Raw legacy attribute data */
    size_t legacy_attributes_size;             /**< Size of legacy attribute payload */
} nmo_ckbeobject_state_t;

/* =============================================================================
 * SERIALIZATION API (Type System)
 * ============================================================================= */

NMO_API nmo_result_t nmo_ckbeobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_ckbeobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ckbeobject_vtable, nmo_register_ckbeobject_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKBEOBJECT_SCHEMAS_H */
