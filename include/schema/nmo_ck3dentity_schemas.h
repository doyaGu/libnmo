/**
 * @file nmo_ck3dentity_schemas.h
 * @brief CK3dEntity schema definitions header
 */

#ifndef NMO_CK3DENTITY_SCHEMAS_H
#define NMO_CK3DENTITY_SCHEMAS_H

#include "schema/nmo_ckrenderobject_schemas.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_schema_registry nmo_schema_registry_t;
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;

/**
 * @brief CK3dEntity state structure
 * 
 * Represents the deserialized state of a CK3dEntity object.
 * This is a PARTIAL schema - some fields are preserved as raw data.
 */
typedef struct nmo_ck3dentity_state {
    nmo_ckrenderobject_state_t render_object; ///< Parent CKRenderObject state
    
    // Transform data
    float world_matrix[16];    ///< 4x4 world transformation matrix
    uint32_t entity_flags;     ///< Entity flags (local/world, etc)
    
    // Preserved unknown data for future schema refinement
    void *raw_tail;           ///< Remaining chunk data (parent ref, z-order, bbox, pivot)
    size_t raw_tail_size;     ///< Size of preserved data
} nmo_ck3dentity_state_t;

/* Function pointer types for vtable */
typedef nmo_result_t (*nmo_ck3dentity_deserialize_fn)(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck3dentity_state_t *out_state);

typedef nmo_result_t (*nmo_ck3dentity_serialize_fn)(
    const nmo_ck3dentity_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena);

typedef nmo_result_t (*nmo_ck3dentity_finish_loading_fn)(
    void *state,
    nmo_arena_t *arena,
    void *repository);

/**
 * @brief Register CK3dEntity state schema
 * 
 * @param registry Schema registry
 * @param arena Arena for allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_ck3dentity_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/**
 * @brief Deserialize CK3dEntity from chunk (public API)
 * 
 * @param chunk Chunk containing CK3dEntity data
 * @param arena Arena for allocations
 * @param out_state Output state structure
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_ck3dentity_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck3dentity_state_t *out_state);

/**
 * @brief Serialize CK3dEntity to chunk (public API)
 * 
 * @param state State to serialize
 * @param chunk Chunk to write to
 * @param arena Arena for temporary allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_ck3dentity_serialize(
    const nmo_ck3dentity_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena);

/**
 * @brief Get CK3dEntity deserialize function
 * @return Function pointer for deserialization
 */
NMO_API nmo_ck3dentity_deserialize_fn nmo_get_ck3dentity_deserialize(void);

/**
 * @brief Get CK3dEntity serialize function
 * @return Function pointer for serialization
 */
NMO_API nmo_ck3dentity_serialize_fn nmo_get_ck3dentity_serialize(void);

/**
 * @brief Get CK3dEntity finish_loading function
 * @return Function pointer for finish loading
 */
NMO_API nmo_ck3dentity_finish_loading_fn nmo_get_ck3dentity_finish_loading(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CK3DENTITY_SCHEMAS_H */
