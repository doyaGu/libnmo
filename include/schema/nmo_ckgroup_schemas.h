/**
 * @file nmo_ckgroup_schemas.h
 * @brief Public API for CKGroup schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKGroup.
 * CKGroup is a container for grouping CKBeObject instances.
 * 
 * Based on official Virtools SDK (reference/src/CKGroup.cpp:185-220):
 * - CKGroup stores an array of object IDs
 * - Simple identifier-based serialization
 * - PostLoad ensures group membership consistency
 */

#ifndef NMO_CKGROUP_SCHEMAS_H
#define NMO_CKGROUP_SCHEMAS_H

#include "nmo_types.h"
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
 * CKGroup STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKGroup state
 * 
 * CKGroup contains an array of CKBeObject references.
 * The group maintains bidirectional relationships - objects know which groups they're in.
 * 
 * Reference: reference/src/CKGroup.cpp:185-220
 */
typedef struct nmo_ckgroup_state {
    /* Base class state */
    nmo_ckbeobject_state_t base;      /**< CKBeObject base state */
    
    /* Object array */
    nmo_object_id_t *object_ids;      /**< Array of grouped object IDs */
    uint32_t object_count;            /**< Number of objects in group */
} nmo_ckgroup_state_t;

/* =============================================================================
 * FUNCTION POINTER TYPES
 * ============================================================================= */

/**
 * @brief CKGroup deserialize function pointer type
 * 
 * @param chunk Chunk containing CKGroup data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckgroup_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ckgroup_state_t *out_state);

/**
 * @brief CKGroup serialize function pointer type
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckgroup_serialize_fn)(
    const nmo_ckgroup_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKGroup schema types
 * 
 * Registers schema types for CKGroup state structures.
 * Must be called during initialization before using CKGroup schemas.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_ckgroup_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/* =============================================================================
 * PUBLIC API - ACCESSOR FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get the deserialize function for CKGroup
 * 
 * @return Deserialize function pointer
 */
NMO_API nmo_ckgroup_deserialize_fn nmo_get_ckgroup_deserialize(void);

/**
 * @brief Get the serialize function for CKGroup
 * 
 * @return Serialize function pointer
 */
NMO_API nmo_ckgroup_serialize_fn nmo_get_ckgroup_serialize(void);

/**
 * @brief CKGroup finish loading function pointer type
 * 
 * Resolves object references and establishes bidirectional group membership.
 * Called during Phase 15 (Object-Level FinishLoading) after deserialization.
 * 
 * @param state CKGroup state with object IDs
 * @param arena Arena for allocations
 * @param repository Object repository for reference resolution (opaque void*)
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckgroup_finish_loading_fn)(
    void *state,
    nmo_arena_t *arena,
    void *repository);

/**
 * @brief Get the finish_loading function for CKGroup
 * 
 * @return Finish loading function pointer
 */
NMO_API nmo_ckgroup_finish_loading_fn nmo_get_ckgroup_finish_loading(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKGROUP_SCHEMAS_H */
