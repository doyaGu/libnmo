/**
 * @file nmo_ckobject_schemas.h
 * @brief Public API for CKObject schema-based serialization
 *
 * Provides schema definitions and (de)serialization functions for CKObject
 * and its derived classes. Used by the object deserialization pipeline in
 * parser.c Phase 14.
 */

#ifndef NMO_CKOBJECT_SCHEMAS_H
#define NMO_CKOBJECT_SCHEMAS_H

#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;

/* =============================================================================
 * CKObject STATE STRUCTURES
 * ============================================================================= */

/**
 * @brief CKObject base state
 * 
 * Minimal state for CKObject (visibility flags only).
 * Corresponds to data serialized in CKObject::Load/Save.
 */
typedef struct nmo_ckobject_state {
    uint32_t visibility_flags;  /**< Visibility flags (VISIBLE/HIERARCHICAL) */
} nmo_ckobject_state_t;

/* Visibility flag constants */
#define NMO_CKOBJECT_VISIBLE          0x01  /**< Object is visible */
#define NMO_CKOBJECT_HIERARCHICAL     0x02  /**< Object has hierarchical hide */

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKObject schema types
 * 
 * Registers schema types for CKObject state structures.
 * Should be called during schema registry initialization.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_ckobject_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/* =============================================================================
 * DESERIALIZATION FUNCTIONS
 * ============================================================================= */

/**
 * @brief Deserialize CKObject state from chunk
 * 
 * Reads CKObject visibility state from chunk using identifier-based reading.
 * Symmetric to CKObject::Load in Virtools SDK.
 * 
 * @param chunk Chunk containing object data
 * @param arena Arena for allocations
 * @param out_state Output state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckobject_deserialize_fn)(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckobject_state_t *out_state);

/**
 * @brief Deserialize CKObject from chunk (implementation)
 */
NMO_API nmo_result_t nmo_ckobject_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckobject_state_t *out_state);

/**
 * @brief Get CKObject deserialize function pointer
 * @return Function pointer to CKObject deserialize implementation
 */
NMO_API nmo_ckobject_deserialize_fn nmo_get_ckobject_deserialize(void);

/* =============================================================================
 * SERIALIZATION FUNCTIONS
 * ============================================================================= */

/**
 * @brief Serialize CKObject state to chunk
 * 
 * Writes CKObject visibility state to chunk using identifier-based writing.
 * Symmetric to CKObject::Save in Virtools SDK.
 * 
 * @param chunk Chunk to write to
 * @param state Input state structure
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckobject_serialize_fn)(
    nmo_chunk_t *chunk,
    const nmo_ckobject_state_t *state);

/**
 * @brief Serialize CKObject to chunk (implementation)
 */
NMO_API nmo_result_t nmo_ckobject_serialize(
    nmo_chunk_t *chunk,
    const nmo_ckobject_state_t *state);

/**
 * @brief Get CKObject serialize function pointer
 * @return Function pointer to CKObject serialize implementation
 */
NMO_API nmo_ckobject_serialize_fn nmo_get_ckobject_serialize(void);

/* =============================================================================
 * FINISH LOADING FUNCTIONS (Phase 15 - PostLoad equivalent)
 * ============================================================================= */

/**
 * @brief Object-level finish loading function
 * 
 * Called after deserialization to resolve references and initialize runtime state.
 * Equivalent to CKObject::PostLoad() in Virtools SDK.
 * 
 * @param state Deserialized object state
 * @param arena Arena for allocations
 * @param repository Object repository for reference resolution (opaque void*)
 * @return Result indicating success or error
 */
typedef nmo_result_t (*nmo_ckobject_finish_loading_fn)(
    void *state,
    nmo_arena_t *arena,
    void *repository);

/**
 * @brief Finish loading CKObject (base implementation)
 * 
 * Base class implementation does nothing - derived classes override.
 */
NMO_API nmo_result_t nmo_ckobject_finish_loading(
    void *state,
    nmo_arena_t *arena,
    void *repository);

/**
 * @brief Get CKObject finish_loading function pointer
 * @return Function pointer to CKObject finish_loading implementation
 */
NMO_API nmo_ckobject_finish_loading_fn nmo_get_ckobject_finish_loading(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKOBJECT_SCHEMAS_H */
