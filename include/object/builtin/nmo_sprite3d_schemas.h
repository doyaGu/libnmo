/**
 * @file nmo_sprite3d_schemas.h
 * @brief CKSprite3D schema definitions
 */

#ifndef NMO_CKSPRITE3D_SCHEMAS_H
#define NMO_CKSPRITE3D_SCHEMAS_H

#include "object/builtin/nmo_3dentity_schemas.h"
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
 * @brief CKSprite3D state
 */
typedef struct nmo_sprite3d_state {
    nmo_3dentity_state_t base;

    uint8_t has_data;
    uint32_t mode;
    float half_width;
    float half_height;
    nmo_vector2_t offset;
    nmo_rect_t uv_rect;
    nmo_object_id_t material_id;
} nmo_sprite3d_state_t;

NMO_API nmo_status_t nmo_sprite3d_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_sprite3d_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_sprite3d_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_sprite3d_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_sprite3d_vtable, nmo_register_sprite3d_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSPRITE3D_SCHEMAS_H */
