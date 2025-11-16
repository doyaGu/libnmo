/**
 * @file nmo_ckbehavior_schemas.h
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
#include "core/nmo_guid.h"
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
typedef struct nmo_ckbehavior_state {
    /* Base class state */
    nmo_cksceneobject_state_t base;  /**< CKSceneObject base state */
    
    /* Core behavior properties */
    uint32_t flags;                        /**< Behavior flags (type, locked, etc.) */
    int32_t priority;                      /**< Execution priority (default 0) */
    int32_t compatible_class_id;           /**< Compatible object class ID */
    
    /* Building block data (only if CKBEHAVIOR_BUILDINGBLOCK flag set) */
    nmo_guid_t block_guid;                 /**< Building block GUID */
    uint32_t block_version;                /**< Building block version */
    
    /* Target parameter (only if CKBEHAVIOR_TARGETABLE flag set) */
    nmo_object_id_t target_parameter_id;   /**< Target parameter ID */
    
    /* Graph data arrays (only if not building block) */
    nmo_object_id_t *sub_behaviors;        /**< Sub-behavior IDs */
    uint32_t sub_behavior_count;           /**< Number of sub-behaviors */
    
    nmo_object_id_t *sub_behavior_links;   /**< Sub-behavior link IDs */
    uint32_t sub_behavior_link_count;      /**< Number of links */
    
    nmo_object_id_t *operations;           /**< Operation IDs */
    uint32_t operation_count;              /**< Number of operations */
    
    /* Parameter arrays */
    nmo_object_id_t *in_parameters;        /**< Input parameter IDs */
    uint32_t in_parameter_count;           /**< Number of input parameters */
    
    nmo_object_id_t *out_parameters;       /**< Output parameter IDs */
    uint32_t out_parameter_count;          /**< Number of output parameters */
    
    nmo_object_id_t *local_parameters;     /**< Local parameter IDs */
    uint32_t local_parameter_count;        /**< Number of local parameters */
    
    /* I/O arrays */
    nmo_object_id_t *inputs;               /**< Input IDs (BehaviorIO) */
    uint32_t input_count;                  /**< Number of inputs */
    
    nmo_object_id_t *outputs;              /**< Output IDs (BehaviorIO) */
    uint32_t output_count;                 /**< Number of outputs */
    
    /* Scene activity (optional) */
    uint32_t single_activity_flags;        /**< Scene activity flags */
    bool has_single_activity;              /**< Whether activity flags are present */
    
    /* Interface chunk (optional, for editing) */
    nmo_chunk_t *interface_chunk;          /**< Interface data chunk */
} nmo_ckbehavior_state_t;

/* =============================================================================
 * FUNCTION POINTER TYPES
 * ============================================================================= */

/**
 * @brief CKBehavior deserialize function pointer type
 * 
 * @param chunk Chunk containing CKBehavior data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckbehavior_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ckbehavior_state_t *out_state);

/**
 * @brief CKBehavior serialize function pointer type
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckbehavior_serialize_fn)(
    const nmo_ckbehavior_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKBehavior schema types
 * 
 * Registers schema types for CKBehavior state structures.
 * Must be called during initialization before using CKBehavior schemas.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_ckbehavior_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/* =============================================================================
 * PUBLIC API - ACCESSOR FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get the deserialize function for CKBehavior
 * 
 * @return Deserialize function pointer
 */
NMO_API nmo_ckbehavior_deserialize_fn nmo_get_ckbehavior_deserialize(void);

/**
 * @brief Get the serialize function for CKBehavior
 * 
 * @return Serialize function pointer
 */
NMO_API nmo_ckbehavior_serialize_fn nmo_get_ckbehavior_serialize(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKBEHAVIOR_SCHEMAS_H */
