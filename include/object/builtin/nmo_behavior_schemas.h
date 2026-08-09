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
#include "object/builtin/nmo_sceneobject_schemas.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ref.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_interface_data nmo_interface_data_t;
typedef struct nmo_logger nmo_logger_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;
typedef struct nmo_object_repository nmo_object_repository_t;

/** Atomic Behavior reference/sub-chunk pair for non-file state lanes. */
typedef struct nmo_behavior_ref {
    nmo_ref_t ref;
    nmo_chunk_t *chunk;
} nmo_behavior_ref_t;

static inline nmo_behavior_ref_t nmo_behavior_ref_from_id(nmo_object_id_t id)
{
    nmo_behavior_ref_t value;
    value.ref = nmo_ref_from_id(id);
    value.chunk = NULL;
    return value;
}

static inline nmo_object_id_t nmo_behavior_ref_runtime_id(
    const nmo_behavior_ref_t *value)
{
    return value != NULL ? nmo_ref_runtime_id(&value->ref) : NMO_OBJECT_ID_NONE;
}

/** Append a resolved runtime reference. The array owns a non-NULL chunk. */
NMO_API nmo_status_t nmo_behavior_ref_array_append(
    nmo_array_t *array,
    nmo_object_id_t id,
    nmo_chunk_t *chunk);

/** Find a resolved runtime reference by ID. */
NMO_API int nmo_behavior_ref_array_find(
    const nmo_array_t *array,
    nmo_object_id_t id,
    size_t *out_index);

/** Return a resolved runtime ID, or NMO_OBJECT_ID_NONE for an invalid item. */
NMO_API nmo_object_id_t nmo_behavior_ref_array_get_id(
    const nmo_array_t *array,
    size_t index);

typedef struct nmo_behavior_interface_parse_stats {
    size_t attempted_count;
    size_t parsed_count;
    size_t failed_count;
    size_t skipped_no_arena_count;
    size_t allocation_failure_count;
    nmo_status_t first_error;
    nmo_object_id_t first_error_object_id;
    uint32_t first_error_file_id;
    uint32_t first_error_chunk_version;
    uint32_t first_error_data_version;
    size_t first_error_reader_offset;
    size_t first_error_chunk_dwords;
} nmo_behavior_interface_parse_stats_t;

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
    nmo_ref_t owner;                       /**< Owner object (legacy formats) */
    uint32_t behavior_type;                /**< Legacy behavior type value */
    uint32_t save_flags;                   /**< Raw save flags from file (preserve unknown bits) */
    bool has_save_flags;                   /**< Whether save_flags was loaded from file */
    bool use_legacy_identifiers;           /**< Use legacy identifier values (0x1/0x2/0x4) */
    
    /* Building block data (only if CKBEHAVIOR_BUILDINGBLOCK flag set) */
    nmo_guid_t block_guid;                 /**< Building block GUID */
    uint32_t block_version;                /**< Building block version */
    
    /* Target parameter (only if CKBEHAVIOR_TARGETABLE flag set) */
    nmo_ref_t target_parameter;            /**< Target input parameter */
    
    /* Graph data arrays (only if not building block) */
    nmo_array_t sub_behaviors;             /**< Sub-behaviors (nmo_behavior_ref_t) */
    
    nmo_array_t sub_behavior_links;        /**< Sub-behavior links (nmo_behavior_ref_t) */
    
    nmo_array_t operations;                /**< Operations (nmo_behavior_ref_t) */
    
    /* Parameter arrays */
    nmo_array_t in_parameters;             /**< Input parameters (nmo_behavior_ref_t) */
    
    nmo_array_t out_parameters;            /**< Output parameters (nmo_behavior_ref_t) */
    
    nmo_array_t local_parameters;          /**< Local parameters (nmo_behavior_ref_t) */
    
    /* I/O arrays */
    nmo_array_t inputs;                    /**< Inputs (nmo_behavior_ref_t) */
    
    nmo_array_t outputs;                   /**< Outputs (nmo_behavior_ref_t) */
    
    /* Scene activity (optional) */
    uint32_t single_activity_flags;        /**< Scene activity flags */
    bool has_single_activity;              /**< Whether activity flags are present */
    
    /* Interface chunk (optional, for editing) */
    nmo_chunk_t *interface_chunk;          /**< Original InterfaceChunk oracle retained for diagnostics */
    bool has_interface;                    /**< Whether interface identifier is present */
    nmo_interface_data_t *interface_data;  /**< Structured InterfaceChunk data used for save after successful parse */
    bool interface_ids_are_runtime;        /**< Interface object IDs are runtime IDs rather than raw CK/file IDs */
} nmo_behavior_state_t;

static inline nmo_object_id_t nmo_behavior_owner_id(
    const nmo_behavior_state_t *state)
{
    return state != NULL
        ? nmo_ref_runtime_id(&state->owner)
        : NMO_OBJECT_ID_NONE;
}

static inline nmo_object_id_t nmo_behavior_target_parameter_id(
    const nmo_behavior_state_t *state)
{
    return state != NULL
        ? nmo_ref_runtime_id(&state->target_parameter)
        : NMO_OBJECT_ID_NONE;
}

static inline void nmo_behavior_set_owner_id(
    nmo_behavior_state_t *state,
    nmo_object_id_t id)
{
    if (state != NULL) state->owner = nmo_ref_from_id(id);
}

static inline void nmo_behavior_set_target_parameter_id(
    nmo_behavior_state_t *state,
    nmo_object_id_t id)
{
    if (state != NULL) state->target_parameter = nmo_ref_from_id(id);
}

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

NMO_API nmo_status_t nmo_behavior_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_behavior_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

/** Explicitly remove invalid Behavior references while preserving lane order. */
NMO_API nmo_status_t nmo_behavior_normalize_references(
    nmo_behavior_state_t *state,
    nmo_object_repository_t *repository,
    const nmo_type_registry_t *types,
    size_t *out_change_count);

/**
 * @brief Parse all pending interface chunks in the repository
 *
 * Called after all objects are loaded and IDs remapped. For each CKBehavior
 * with a raw interface_chunk, parses it into structured interface_data.
 * Successful and failed chunks are both kept as raw interface_chunk data so
 * callers can still preserve or inspect the original bytes.
 *
 * @param repo  Object repository
 * @param logger Optional logger for parse failures
 * @return NMO_OK if all interface chunks parsed, otherwise the first parse error
 */
NMO_API nmo_status_t nmo_behavior_parse_all_interfaces(
    nmo_object_repository_t *repo,
    nmo_logger_t *logger);

NMO_API nmo_status_t nmo_behavior_parse_all_interfaces_ex(
    nmo_object_repository_t *repo,
    nmo_logger_t *logger,
    nmo_behavior_interface_parse_stats_t *out_stats);

NMO_DECLARE_OBJECT_SCHEMA(nmo_behavior_vtable, nmo_register_behavior_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKBEHAVIOR_SCHEMAS_H */
