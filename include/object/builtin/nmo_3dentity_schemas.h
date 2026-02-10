/**
 * @file nmo_3dentity_schemas.h
 * @brief CK3dEntity schema definitions header
 */

#ifndef NMO_CK3DENTITY_SCHEMAS_H
#define NMO_CK3DENTITY_SCHEMAS_H

#include "object/builtin/nmo_renderobject_schemas.h"
#include "object/nmo_object_struct_defs.h"
#include "object/nmo_object_type_common.h"
#include "core/nmo_math.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor nmo_type_descriptor_t;

/**
 * @brief CK3dEntity state structure
 * 
 * Represents the deserialized state of a CK3dEntity object.
 */
typedef struct nmo_3dentity_state {
    nmo_renderobject_state_t base; ///< Parent CKRenderObject state

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
    nmo_3dentity_skin_t *skin;
} nmo_3dentity_state_t;

NMO_API nmo_status_t nmo_3dentity_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_3dentity_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_3dentity_vtable, nmo_register_3dentity_type)

NMO_API nmo_status_t nmo_3dentity_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CK3DENTITY_SCHEMAS_H */
