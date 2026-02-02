/**
 * @file nmo_cksprite3d_schemas.h
 * @brief CKSprite3D schema definitions
 */

#ifndef NMO_CKSPRITE3D_SCHEMAS_H
#define NMO_CKSPRITE3D_SCHEMAS_H

#include "object/nmo_ck3dentity_schemas.h"
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
 * @brief CKSprite3D state
 */
typedef struct nmo_cksprite3d_state {
    nmo_ck3dentity_state_t base;

    uint8_t has_data;
    uint32_t mode;
    float half_width;
    float half_height;
    nmo_vector2_t offset;
    nmo_rect_t uv_rect;
    nmo_object_id_t material_id;
} nmo_cksprite3d_state_t;

NMO_API nmo_result_t nmo_cksprite3d_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_result_t nmo_cksprite3d_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_cksprite3d_vtable, nmo_register_cksprite3d_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSPRITE3D_SCHEMAS_H */
