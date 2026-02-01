/**
 * @file nmo_ck3dentity_schemas.h
 * @brief CK3dEntity schema definitions header
 */

#ifndef NMO_CK3DENTITY_SCHEMAS_H
#define NMO_CK3DENTITY_SCHEMAS_H

#include "object/nmo_ckrenderobject_schemas.h"
#include "core/nmo_math.h"
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
 * @brief CK3dEntity skin vertex data
 */
typedef struct nmo_ck3dentity_skin_vertex {
    uint32_t bone_count;
    nmo_vector_t initial_pos;
    uint32_t *bone_indices;
    float *bone_weights;
} nmo_ck3dentity_skin_vertex_t;

/**
 * @brief CK3dEntity skin bone data
 */
typedef struct nmo_ck3dentity_skin_bone {
    nmo_object_id_t bone_id;
    uint32_t bone_flags;
    nmo_matrix_t inverse_bind_matrix;
} nmo_ck3dentity_skin_bone_t;

/**
 * @brief CK3dEntity skin data
 */
typedef struct nmo_ck3dentity_skin {
    nmo_matrix_t object_init_matrix;
    uint32_t bone_count;
    nmo_ck3dentity_skin_bone_t *bones;
    uint32_t vertex_count;
    nmo_ck3dentity_skin_vertex_t *vertices;
    uint32_t normal_count;
    nmo_vector_t *normals;
} nmo_ck3dentity_skin_t;

/**
 * @brief CK3dEntity state structure
 * 
 * Represents the deserialized state of a CK3dEntity object.
 */
typedef struct nmo_ck3dentity_state {
    nmo_ckrenderobject_state_t render_object; ///< Parent CKRenderObject state

    /* Transform data */
    float world_matrix[16];    ///< 4x4 world transformation matrix
    uint32_t entity_flags;     ///< CK_3DENTITY flags
    uint32_t moveable_flags;   ///< VX_MOVEABLE flags

    /* Hierarchy and references */
    nmo_object_id_t parent_id;
    nmo_object_id_t place_id;
    int32_t z_order;

    /* Meshes */
    nmo_object_id_t current_mesh_id;
    uint32_t mesh_count;
    nmo_object_id_t *mesh_ids;

    /* Animations */
    uint32_t animation_count;
    nmo_object_id_t *animation_ids;

    /* Skin data (optional) */
    nmo_ck3dentity_skin_t *skin;
} nmo_ck3dentity_state_t;

/* Function pointer types for vtable */
typedef nmo_result_t (*nmo_ck3dentity_deserialize_fn)(
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena,
    nmo_ck3dentity_state_t *out_state);

typedef nmo_result_t (*nmo_ck3dentity_serialize_fn)(
    const nmo_ck3dentity_state_t *in_state,
    nmo_chunk_t *out_chunk,
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
    nmo_chunk_t *out_chunk,
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
    const nmo_ck3dentity_state_t *in_state,
    nmo_chunk_t *out_chunk,
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
