/**
 * @file nmo_level_schemas.h
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
#include "core/nmo_array.h"
#include "core/nmo_guid.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

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
typedef struct nmo_level_state {
    /* Base class state */
    nmo_beobject_state_t base;         /**< CKBeObject base state */
    
    /* Scene management */
    nmo_array_t scene_ids;               /**< Scene object IDs (nmo_object_id_t) */
    
    nmo_object_id_t current_scene_id;    /**< Current active scene ID */
    nmo_object_id_t level_scene_id;      /**< Default level scene ID */
    
    /* Level scene embedded chunk */
    nmo_chunk_t *level_scene_chunk;      /**< Embedded chunk for level scene */
    
    /* Manager state (optional, rarely used) */
    uint8_t has_inactive_manager_section; /**< Presence of LEVELINACTIVEMAN section */
    nmo_array_t inactive_manager_guids;  /**< Inactive manager GUIDs (nmo_guid_t) */
    
    nmo_array_t duplicate_manager_names; /**< Duplicate manager names (char *) */
} nmo_level_state_t;

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_status_t nmo_level_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_level_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_level_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

NMO_DECLARE_OBJECT_SCHEMA(nmo_level_vtable, nmo_register_level_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKLEVEL_SCHEMAS_H */
