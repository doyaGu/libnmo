/**
 * @file nmo_ckscene_schemas.h
 * @brief Public API for CKScene schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKScene.
 * CKScene manages scene objects with initial states and rendering settings.
 * 
 * Based on official Virtools SDK (reference/src/CKScene.cpp:692-890):
 * - CKScene stores scene objects with initial value chunks
 * - Each object has flags controlling activation/reset behavior
 * - Rendering settings include fog, background, lighting
 * - Environment settings control scene behavior
 */

#ifndef NMO_CKSCENE_SCHEMAS_H
#define NMO_CKSCENE_SCHEMAS_H

#include "nmo_types.h"
#include "nmo_ckbeobject_schemas.h"
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
 * CKScene STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief Scene object descriptor
 * 
 * Stores per-object information in a scene, including initial state and flags.
 * 
 * Reference: CKSceneObjectDesc in reference/include/CKScene.h
 */
typedef struct nmo_scene_object_desc {
    nmo_object_id_t object_id;      /**< Scene object ID */
    nmo_chunk_t *initial_value;     /**< Initial state chunk (can be NULL) */
    uint32_t flags;                 /**< Object flags (active, reset, activate) */
} nmo_scene_object_desc_t;

/**
 * @brief CKScene state
 * 
 * CKScene manages a collection of scene objects with their initial states
 * and rendering environment settings.
 * 
 * Storage layout:
 * 1. CK_STATESAVE_SCENENEWDATA: Level + object descriptors
 * 2. CK_STATESAVE_SCENELAUNCHED: Environment settings
 * 3. CK_STATESAVE_SCENERENDERSETTINGS: Background, fog, lighting
 * 
 * Reference: reference/src/CKScene.cpp:692-890
 */
typedef struct nmo_ckscene_state {
    /* Base class state */
    nmo_ckbeobject_state_t base;               /**< CKBeObject base state */
    
    /* Scene hierarchy */
    nmo_object_id_t level_id;                  /**< Parent level ID */
    
    /* Scene objects */
    nmo_scene_object_desc_t *object_descs;     /**< Array of scene object descriptors */
    uint32_t object_count;                     /**< Number of scene objects */
    
    /* Environment settings */
    uint32_t environment_settings;             /**< Scene behavior flags */
    
    /* Rendering settings */
    uint32_t background_color;                 /**< Background ARGB color */
    uint32_t ambient_light_color;              /**< Ambient light ARGB color */
    
    /* Fog settings */
    uint32_t fog_mode;                         /**< Fog mode (VXFOG_MODE enum) */
    uint32_t fog_color;                        /**< Fog ARGB color */
    float fog_start;                           /**< Fog start distance */
    float fog_end;                             /**< Fog end distance */
    float fog_density;                         /**< Fog density (for exponential modes) */
    
    /* Scene references */
    nmo_object_id_t background_texture_id;     /**< Background texture ID */
    nmo_object_id_t starting_camera_id;        /**< Starting camera ID */
} nmo_ckscene_state_t;

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_result_t nmo_ckscene_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_ckscene_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ckscene_vtable, nmo_register_ckscene_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSCENE_SCHEMAS_H */
