/**
 * @file nmo_cklevel_schemas.h
 * @brief Public API for CKLevel schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKLevel.
 * CKLevel is the top-level container managing scenes and global objects.
 * 
 * Based on official Virtools SDK (reference/src/CKLevel.cpp:346-471):
 * - CKLevel manages scene list and default level scene
 * - Stores current scene reference and level scene with embedded chunk
 * - Optionally stores inactive manager GUIDs and duplicate manager names
 */

#ifndef NMO_CKLEVEL_SCHEMAS_H
#define NMO_CKLEVEL_SCHEMAS_H

#include "nmo_types.h"
#include "core/nmo_guid.h"
#include "nmo_ckbeobject_schemas.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;

/* =============================================================================
 * CKLevel STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKLevel state
 * 
 * CKLevel is the root container for a Virtools composition, managing all scenes
 * and providing the execution context.
 * 
 * Storage layout:
 * 1. CK_STATESAVE_LEVELDEFAULTDATA: Legacy arrays (empty) + scene list
 * 2. CK_STATESAVE_LEVELSCENE: Current scene + level scene with embedded chunk
 * 3. CK_STATESAVE_LEVELINACTIVEMAN (optional): Inactive manager GUIDs
 * 4. CK_STATESAVE_LEVELDUPLICATEMAN (optional): Duplicate manager names
 * 
 * Reference: reference/src/CKLevel.cpp:346-471
 */
typedef struct nmo_cklevel_state {
    /* Base class state */
    nmo_ckbeobject_state_t base;         /**< CKBeObject base state */
    
    /* Scene management */
    nmo_object_id_t *scene_ids;          /**< Array of scene object IDs */
    uint32_t scene_count;                /**< Number of scenes in level */
    
    nmo_object_id_t current_scene_id;    /**< Current active scene ID */
    nmo_object_id_t level_scene_id;      /**< Default level scene ID */
    
    /* Level scene embedded chunk */
    nmo_chunk_t *level_scene_chunk;      /**< Embedded chunk for level scene */
    
    /* Manager state (optional, rarely used) */
    nmo_guid_t *inactive_manager_guids;  /**< Array of inactive manager GUIDs */
    uint32_t inactive_manager_count;     /**< Number of inactive managers */
    
    char **duplicate_manager_names;      /**< Array of duplicate manager names */
    uint32_t duplicate_manager_count;    /**< Number of duplicate managers */
} nmo_cklevel_state_t;

/* =============================================================================
 * FUNCTION POINTER TYPES
 * ============================================================================= */

/**
 * @brief CKLevel deserialize function pointer type
 * 
 * @param chunk Chunk containing CKLevel data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_cklevel_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_cklevel_state_t *out_state);

/**
 * @brief CKLevel serialize function pointer type
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_cklevel_serialize_fn)(
    const nmo_cklevel_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKLevel schema types
 * 
 * Registers schema types for CKLevel state structures.
 * Must be called during initialization before using CKLevel schemas.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_cklevel_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/* =============================================================================
 * PUBLIC API - ACCESSOR FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get the deserialize function for CKLevel
 * 
 * @return Deserialize function pointer
 */
NMO_API nmo_cklevel_deserialize_fn nmo_get_cklevel_deserialize(void);

/**
 * @brief Get the serialize function for CKLevel
 * 
 * @return Serialize function pointer
 */
NMO_API nmo_cklevel_serialize_fn nmo_get_cklevel_serialize(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKLEVEL_SCHEMAS_H */
