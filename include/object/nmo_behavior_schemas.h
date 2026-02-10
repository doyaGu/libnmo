/**
 * @file nmo_behavior_schemas.h
 * @brief Public API for CKBehavior schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKBehavior.
 * CKBehavior is the core class for behavior graphs and building blocks.
 * 
 * Based on official Virtools SDK (reference/src/CKBehavior.cpp:1472-1900):
 * - CKBehavior can be either a building block (function) or graph (sub-behaviors)
 * - Contains inputs/outputs, parameters (in/out/local), operations
 * - Sub-behaviors and links form the behavior graph
 * 
 * NOTE: This is a SIMPLIFIED implementation focusing on core serialization.
 * Complex graph manipulation and validation are handled at higher layers.
 */

#ifndef NMO_CKBEHAVIOR_SCHEMAS_H
#define NMO_CKBEHAVIOR_SCHEMAS_H

#include "nmo_types.h"
#include "core/nmo_array.h"
#include "core/nmo_guid.h"
#include "nmo_sceneobject_schemas.h"
#include "object/nmo_object_type_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/* =============================================================================
 * CKBehavior STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKBehavior state
 * 
 * CKBehavior represents either:
 * 1. Building Block: GUID-identified function (CKBEHAVIOR_BUILDINGBLOCK flag set)
 * 2. Behavior Graph: Container with sub-behaviors, links, operations
 * 
 * Storage layout:
 * 1. CK_STATESAVE_BEHAVIORINTERFACE (optional): Interface chunk for editing
 * 2. CK_STATESAVE_BEHAVIORNEWDATA: Core behavior data
 *    - Flags (behavior type, locked, etc.)
 *    - GUID + version (if building block)
 *    - Priority (if CKBEHAVIOR_PRIORITY flag set)
 *    - Compatible class ID (if CKBEHAVIOR_COMPATIBLECLASSID flag set)
 *    - Target parameter (if CKBEHAVIOR_TARGETABLE flag set)
 *    - Save flags (indicating which arrays follow)
 *    - Sub-behaviors array (if CK_STATESAVE_BEHAVIORSUBBEHAV)
 *    - Sub-behavior links array (if CK_STATESAVE_BEHAVIORSUBLINKS)
 *    - Operations array (if CK_STATESAVE_BEHAVIOROPERATIONS)
 *    - Input parameters array (if CK_STATESAVE_BEHAVIORINPARAMS)
 *    - Output parameters array (if CK_STATESAVE_BEHAVIOROUTPARAMS)
 *    - Local parameters array (if CK_STATESAVE_BEHAVIORLOCALPARAMS)
 *    - Inputs array (if CK_STATESAVE_BEHAVIORINPUTS)
 *    - Outputs array (if CK_STATESAVE_BEHAVIOROUTPUTS)
 * 3. CK_STATESAVE_BEHAVIORSINGLEACTIVITY (optional): Scene activity flags
 * 
 * Reference: reference/src/CKBehavior.cpp:1472-1900
 */
typedef struct nmo_behavior_state {
    /* Base class state */
    nmo_sceneobject_state_t base;  /**< CKSceneObject base state */
    
    /* Core behavior properties */
    uint32_t flags;                        /**< Behavior flags (type, locked, etc.) */
    int32_t priority;                      /**< Execution priority (default 0) */
    int32_t compatible_class_id;           /**< Compatible object class ID */
    nmo_object_id_t owner_id;              /**< Owner object ID (legacy formats) */
    uint32_t behavior_type;                /**< Legacy behavior type value */
    uint32_t save_flags;                   /**< Raw save flags from file (preserve unknown bits) */
    bool has_save_flags;                   /**< Whether save_flags was loaded from file */
    bool use_legacy_identifiers;           /**< Use legacy identifier values (0x1/0x2/0x4) */
    
    /* Building block data (only if CKBEHAVIOR_BUILDINGBLOCK flag set) */
    nmo_guid_t block_guid;                 /**< Building block GUID */
    uint32_t block_version;                /**< Building block version */
    
    /* Target parameter (only if CKBEHAVIOR_TARGETABLE flag set) */
    nmo_object_id_t target_parameter_id;   /**< Target parameter ID */
    
    /* Graph data arrays (only if not building block) */
    nmo_array_t sub_behaviors;             /**< Sub-behavior IDs (nmo_object_id_t) */
    nmo_array_t sub_behavior_chunks;       /**< Sub-behavior sub-chunks (nmo_chunk_t *) */
    
    nmo_array_t sub_behavior_links;        /**< Sub-behavior link IDs (nmo_object_id_t) */
    
    nmo_array_t operations;                /**< Operation IDs (nmo_object_id_t) */
    
    /* Parameter arrays */
    nmo_array_t in_parameters;             /**< Input parameter IDs (nmo_object_id_t) */
    
    nmo_array_t out_parameters;            /**< Output parameter IDs (nmo_object_id_t) */
    
    nmo_array_t local_parameters;          /**< Local parameter IDs (nmo_object_id_t) */
    nmo_array_t local_parameter_chunks;    /**< Local parameter sub-chunks (nmo_chunk_t *) */
    
    /* I/O arrays */
    nmo_array_t inputs;                    /**< Input IDs (BehaviorIO) */
    
    nmo_array_t outputs;                   /**< Output IDs (BehaviorIO) */
    
    /* Scene activity (optional) */
    uint32_t single_activity_flags;        /**< Scene activity flags */
    bool has_single_activity;              /**< Whether activity flags are present */
    
    /* Interface chunk (optional, for editing) */
    nmo_chunk_t *interface_chunk;          /**< Interface data chunk */
    bool has_interface;                    /**< Whether interface identifier is present */
} nmo_behavior_state_t;

/* =============================================================================
 * PUBLIC API
 * ============================================================================= */

NMO_API nmo_status_t nmo_behavior_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_behavior_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_behavior_vtable, nmo_register_behavior_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKBEHAVIOR_SCHEMAS_H */
