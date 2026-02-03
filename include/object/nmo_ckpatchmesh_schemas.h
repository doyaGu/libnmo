/**
 * @file nmo_ckpatchmesh_schemas.h
 * @brief CKPatchMesh schema definitions
 */

#ifndef NMO_CKPATCHMESH_SCHEMAS_H
#define NMO_CKPATCHMESH_SCHEMAS_H

#include "object/nmo_ckmesh_schemas.h"
#include "object/nmo_object_type_common.h"
#include "core/nmo_math.h"
#include "nmo_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct nmo_arena nmo_arena_t;
typedef struct nmo_chunk nmo_chunk_t;

typedef struct nmo_type_descriptor_t nmo_type_descriptor_t;

/**
 * @brief Patch mesh format type
 */
typedef enum nmo_ckpatchmesh_format {
    NMO_PATCHMESH_FORMAT_NONE = 0,
    NMO_PATCHMESH_FORMAT_DATA3 = 1,
    NMO_PATCHMESH_FORMAT_DATA2 = 2
} nmo_ckpatchmesh_format_t;

/**
 * @brief Patch data (type + smoothing + raw indices)
 */
typedef struct nmo_ckpatchmesh_patch {
    uint32_t type;
    uint32_t smoothing_group;
    uint8_t data[40];
} nmo_ckpatchmesh_patch_t;

/**
 * @brief Patch mesh texture channel
 */
typedef struct nmo_ckpatchmesh_channel {
    nmo_object_id_t material_id;
    uint32_t flags;
    uint32_t type;
    uint32_t subtype;
    uint32_t patch_count;
    uint8_t *patches_raw;
    uint32_t uv_count;
    nmo_vector2_t *uvs;
} nmo_ckpatchmesh_channel_t;

/**
 * @brief CKPatchMesh state
 */
typedef struct nmo_ckpatchmesh_state {
    nmo_ck_mesh_state_t base;

    nmo_ckpatchmesh_format_t format;

    uint32_t patch_flags;
    int32_t iteration_count;
    int32_t vec_count;
    uint32_t total_count;
    nmo_vector_t *vectors;

    uint32_t patch_count;
    nmo_object_id_t *patch_material_ids;
    nmo_ckpatchmesh_patch_t *patches;

    uint32_t edge_count;
    uint8_t *edge_data;
    size_t edge_data_size;

    uint32_t channel_count;
    nmo_ckpatchmesh_channel_t *channels;

    /* Legacy DATA2 payloads */
    nmo_object_id_t legacy_default_material_id;
    uint32_t legacy_patch_count;
    uint8_t *legacy_patch_data;
    size_t legacy_patch_data_size;
    uint32_t legacy_edge_count;
    uint8_t *legacy_edge_data;
    size_t legacy_edge_data_size;
    uint32_t legacy_tvpatch_count;
    uint8_t *legacy_tvpatch_data;
    size_t legacy_tvpatch_data_size;
    uint32_t legacy_uv_count;
    uint8_t *legacy_uv_data;
    size_t legacy_uv_data_size;
    uint32_t legacy_smoothing_count;
    uint32_t *legacy_smoothing_groups;
    uint32_t legacy_material_count;
    nmo_object_id_t *legacy_material_ids;
} nmo_ckpatchmesh_state_t;

NMO_API nmo_status_t nmo_ckpatchmesh_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_ckpatchmesh_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_ckpatchmesh_vtable, nmo_register_ckpatchmesh_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPATCHMESH_SCHEMAS_H */
