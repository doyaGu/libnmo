/**
 * @file nmo_ck3dobject_schemas.h
 * @brief CK3dObject schema definitions header
 */

#ifndef NMO_CK3DOBJECT_SCHEMAS_H
#define NMO_CK3DOBJECT_SCHEMAS_H

#include "schema/nmo_ck3dentity_schemas.h"
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
 * @brief CK3dObject state structure
 * 
 * Represents the deserialized state of a CK3dObject (3D mesh object).
 * This is a PARTIAL schema - mesh/material details preserved as raw data.
 */
typedef struct nmo_ck3dobject_state {
    nmo_ck3dentity_state_t entity;  ///< Parent CK3dEntity state
    
    // Mesh and rendering data
    nmo_object_id_t mesh_id;       ///< Reference to CKMesh object
    uint32_t rendering_flags;      ///< Rendering flags (wireframe, culling, etc)
    
    // Preserved unknown data for future schema refinement
    void *raw_tail;                ///< Remaining chunk data (materials, deformations)
    size_t raw_tail_size;          ///< Size of preserved data
} nmo_ck3dobject_state_t;

/* Function pointer types for vtable */
typedef nmo_result_t (*nmo_ck3dobject_deserialize_fn)(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck3dobject_state_t *out_state);

typedef nmo_result_t (*nmo_ck3dobject_serialize_fn)(
    const nmo_ck3dobject_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena);

typedef nmo_result_t (*nmo_ck3dobject_finish_loading_fn)(
    void *state,
    nmo_arena_t *arena,
    void *repository);

/**
 * @brief Register CK3dObject state schema
 * 
 * @param registry Schema registry
 * @param arena Arena for allocations
 * @return Result indicating success or error
 */
NMO_API nmo_result_t nmo_register_ck3dobject_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

/**
 * @brief Get CK3dObject deserialize function
 * @return Function pointer for deserialization
 */
NMO_API nmo_ck3dobject_deserialize_fn nmo_get_ck3dobject_deserialize(void);

/**
 * @brief Get CK3dObject serialize function
 * @return Function pointer for serialization
 */
NMO_API nmo_ck3dobject_serialize_fn nmo_get_ck3dobject_serialize(void);

/**
 * @brief Get CK3dObject finish_loading function
 * @return Function pointer for finish loading
 */
NMO_API nmo_ck3dobject_finish_loading_fn nmo_get_ck3dobject_finish_loading(void);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CK3DOBJECT_SCHEMAS_H */
