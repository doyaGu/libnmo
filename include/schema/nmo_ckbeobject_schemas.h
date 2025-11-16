/**
 * @file nmo_ckbeobject_schemas.h
 * @brief Public API for CKBeObject schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKBeObject.
 * CKBeObject is the base class for behavioral objects (objects with scripts/attributes).
 * 
 * CKBeObject adds scripts, priority, and attributes on top of CKObject.
 * Many derived classes (CKRenderObject, CKMesh, CKTexture, etc.) do not override
 * Load/Save and inherit this serialization behavior directly.
 */

#ifndef NMO_CKBEOBJECT_SCHEMAS_H
#define NMO_CKBEOBJECT_SCHEMAS_H

#include "nmo_types.h"
#include "nmo_cksceneobject_schemas.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;

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
} nmo_ckbeobject_state_t;

/* =============================================================================
 * FUNCTION POINTER TYPES
 * ============================================================================= */

/**
 * @brief CKBeObject deserialize function pointer type
 * 
 * @param chunk Chunk containing CKBeObject data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckbeobject_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ckbeobject_state_t *out_state);

/**
 * @brief CKBeObject serialize function pointer type
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckbeobject_serialize_fn)(
    const nmo_ckbeobject_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKBeObject schema types
 * 
 * Registers schema types for CKBeObject state structures.
 * Must be called during initialization before using CKBeObject schemas.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_ckbeobject_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/* =============================================================================
 * PUBLIC API - ACCESSOR FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get the deserialize function for CKBeObject
 * 
 * Returns a function pointer that can be used to deserialize CKBeObject data
 * from a chunk into a nmo_ckbeobject_state_t structure.
 * 
 * @return Deserialize function pointer (never NULL)
 */
NMO_API nmo_ckbeobject_deserialize_fn nmo_get_ckbeobject_deserialize(void);

/**
 * @brief Get the serialize function for CKBeObject
 * 
 * Returns a function pointer that can be used to serialize a nmo_ckbeobject_state_t
 * structure into a chunk.
 * 
 * @return Serialize function pointer (never NULL)
 */
NMO_API nmo_ckbeobject_serialize_fn nmo_get_ckbeobject_serialize(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKBEOBJECT_SCHEMAS_H */
