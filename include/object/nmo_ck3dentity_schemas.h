/**
 * @file nmo_ck3dentity_schemas.h
 * @brief CK3dEntity schema definitions header
 */

#ifndef NMO_CK3DENTITY_SCHEMAS_H
#define NMO_CK3DENTITY_SCHEMAS_H

#include "object/nmo_ckrenderobject_schemas.h"
#include "object/nmo_object_type_common.h"
#include "core/nmo_math.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;
typedef struct nmo_result nmo_result_t;
typedef struct nmo_type_descriptor_t nmo_type_descriptor_t;

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
    nmo_ckrenderobject_state_t base; ///< Parent CKRenderObject state

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

NMO_API nmo_result_t nmo_ck3dentity_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_ck3dentity_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ck3dentity_vtable, nmo_register_ck3dentity_type)

NMO_API nmo_result_t nmo_ck3dentity_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CK3DENTITY_SCHEMAS_H */
