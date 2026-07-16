/**
 * @file nmo_patchmesh_schemas.h
 * @brief CKPatchMesh schema definitions
 */

#ifndef NMO_CKPATCHMESH_SCHEMAS_H
#define NMO_CKPATCHMESH_SCHEMAS_H

#include "object/builtin/nmo_mesh_schemas.h"
#include "object/nmo_object_enum_defs.h"
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
 * @brief Patch mesh format type
 */
typedef CK_PATCHMESH_FORMAT nmo_patchmesh_format_t;

/** Patch payload and its material reference as one indivisible record. */
typedef struct nmo_patchmesh_patch_record {
    nmo_ref_t material;
    nmo_patchmesh_patch_t patch;
} nmo_patchmesh_patch_record_t;

/**
 * @brief CKPatchMesh state
 */
typedef struct nmo_patchmesh_state {
    nmo_mesh_state_t base;

    nmo_patchmesh_format_t format;

    uint32_t patch_flags;
    int32_t iteration_count;
    int32_t vec_count;
    uint32_t total_count;
    nmo_vector_t *vectors;

    uint32_t patch_count;
    nmo_patchmesh_patch_record_t *patches;

    uint32_t edge_count;
    uint8_t *edge_data;
    size_t edge_data_size;

    uint32_t channel_count;
    nmo_patchmesh_channel_t *channels;

    /* Legacy DATA2 payloads */
    nmo_ref_t legacy_default_material;
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
    uint8_t has_legacy_smoothing;
    uint32_t legacy_material_count;
    nmo_ref_t *legacy_materials;
    uint8_t has_legacy_materials;
} nmo_patchmesh_state_t;

NMO_API nmo_status_t nmo_patchmesh_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_patchmesh_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_patchmesh_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_API nmo_status_t nmo_patchmesh_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

NMO_DECLARE_OBJECT_SCHEMA(nmo_patchmesh_vtable, nmo_register_patchmesh_type)

#ifdef __cplusplus
}
#endif

#endif /* NMO_CKPATCHMESH_SCHEMAS_H */
