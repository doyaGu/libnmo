/**
 * @file nmo_cksprite3d_schemas.h
 * @brief CKSprite3D schema definitions
 */

#ifndef NMO_CKSPRITE3D_SCHEMAS_H
#define NMO_CKSPRITE3D_SCHEMAS_H

#include "object/nmo_ck3dentity_schemas.h"
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

NMO_API nmo_result_t nmo_register_cksprite3d_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena);

NMO_API nmo_result_t nmo_cksprite3d_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_cksprite3d_state_t *out_state);

NMO_API nmo_result_t nmo_cksprite3d_serialize(
    const nmo_cksprite3d_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKSPRITE3D_SCHEMAS_H */
