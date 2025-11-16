/**
 * @file nmo_cksceneobject_schemas.h
 * @brief CKSceneObject schema declarations
 */

#ifndef NMO_CKSCENEOBJECT_SCHEMAS_H
#define NMO_CKSCENEOBJECT_SCHEMAS_H

#include "nmo_types.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "schema/nmo_ckobject_schemas.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_chunk nmo_chunk_t;

/* =============================================================================
 * CKSceneObject STATE
 * ============================================================================= */

/**
 * @brief CKSceneObject state structure
 */
typedef struct nmo_cksceneobject_state {
    nmo_ckobject_state_t base;  /**< Inherited CKObject state */
    uint8_t *raw_tail;           /**< Unknown/future data */
    size_t raw_tail_size;        /**< Size of unknown data */
} nmo_cksceneobject_state_t;

/* =============================================================================
 * FUNCTION TYPES
 * ============================================================================= */

/**
 * @brief CKSceneObject deserialize function type
 */
typedef nmo_result_t (*nmo_cksceneobject_deserialize_fn)(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cksceneobject_state_t *out_state);

/**
 * @brief CKSceneObject serialize function type
 */
typedef nmo_result_t (*nmo_cksceneobject_serialize_fn)(
    const nmo_cksceneobject_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

/* =============================================================================
 * DESERIALIZATION/SERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKSceneObject from chunk
 * 
 * @param chunk Chunk to read from
 * @param arena Arena for allocations
 * @param out_state Output state structure
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_cksceneobject_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cksceneobject_state_t *out_state);

/**
 * @brief Serialize CKSceneObject to chunk
 * 
 * @param in_state State to serialize (input)
 * @param out_chunk Chunk to write to (output)
 * @param arena Arena allocator for error handling
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_cksceneobject_serialize(
    const nmo_cksceneobject_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

/* =============================================================================
 * ACCESSOR FUNCTIONS
 * ============================================================================= */

/**
 * @brief Get CKSceneObject deserialize function pointer
 */
NMO_API nmo_cksceneobject_deserialize_fn nmo_get_cksceneobject_deserialize(void);

/**
 * @brief Get CKSceneObject serialize function pointer
 */
NMO_API nmo_cksceneobject_serialize_fn nmo_get_cksceneobject_serialize(void);

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CKSceneObject schemas
 * 
 * @param registry Schema registry
 * @param arena Arena for allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_cksceneobject_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSCENEOBJECT_SCHEMAS_H */
