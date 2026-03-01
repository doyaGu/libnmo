/**
 * @file nmo_beobject_schemas.h
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
#include "core/nmo_array.h"
#include "object/builtin/nmo_sceneobject_schemas.h"
#include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

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
typedef struct nmo_beobject_state {
    /* Base class state */
    nmo_sceneobject_state_t base;  /**< CKSceneObject base state */
    
    /* Scripts */
    nmo_array_t script_ids;        /**< Script behavior IDs (nmo_object_id_t) */
    
    /* Priority */
    int32_t priority;              /**< Execution priority (0 = default) */
    
    /* Attributes */
    nmo_array_t attribute_parameter_ids;       /**< Attribute parameter IDs (nmo_object_id_t) */
    nmo_array_t attribute_types;               /**< Attribute type IDs (uint32_t) */

    /* Attribute parameter sub-chunks (non-file mode) */
    nmo_array_t attribute_chunks;              /**< Attribute sub-chunks (nmo_chunk_t *) */

    /* Single activity flags (file save only) */
    uint8_t has_single_activity;               /**< True if single activity flags exist */
    uint32_t single_activity_flags;            /**< Scene object activity flags */

    /* Legacy attribute payload (CK_STATESAVE_ATTRIBUTES) */
    nmo_array_t legacy_attributes_raw;         /**< Legacy attribute payload (uint8_t) */
} nmo_beobject_state_t;

/* =============================================================================
 * SERIALIZATION API (Type System)
 * ============================================================================= */

NMO_API nmo_status_t nmo_beobject_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_beobject_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_beobject_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

NMO_DECLARE_OBJECT_SCHEMA(nmo_beobject_vtable, nmo_register_beobject_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKBEOBJECT_SCHEMAS_H */
