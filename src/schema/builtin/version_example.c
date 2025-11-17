/**
 * @file version_example.c
 * @brief Example of multi-version schema registration
 *
 * This file demonstrates how to register multiple versions of the same type
 * to support different file format versions. This is particularly useful for
 * types like CKMesh and CKTexture that have significantly different layouts
 * across Virtools versions.
 *
 * VERSIONING STRATEGY:
 * - Each version gets its own schema registration
 * - Version ranges are specified via since_version/deprecated_version/removed_version
 * - At load time, nmo_schema_registry_find_for_version() selects the right variant
 *
 * EXAMPLE SCENARIO:
 * - MeshData_v2: Used in Virtools 2.x-4.x (versions 2-4)
 * - MeshData_v5: Used in Virtools 5.x+ (versions 5+), completely redesigned
 */

#include "schema/nmo_schema.h"
#include "schema/nmo_schema_builder.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_schema_macros.h"
#include "core/nmo_error.h"
#include <stddef.h>

/* =============================================================================
 * EXAMPLE TYPE DEFINITIONS
 * ============================================================================= */

/**
 * @brief Legacy mesh data (v2-v4)
 *
 * Older Virtools versions used a simpler mesh format with:
 * - Single vertex array
 * - Single face array
 * - No multi-channel support
 */
typedef struct mesh_data_v2 {
    uint32_t vertex_count;
    uint32_t face_count;
    uint32_t material_id;      /* Single material only */
    uint32_t flags;
} mesh_data_v2_t;

/**
 * @brief Modern mesh data (v5+)
 *
 * Redesigned format with:
 * - Multiple vertex channels
 * - Material groups
 * - Bone weights
 * - Morph targets
 */
typedef struct mesh_data_v5 {
    uint32_t vertex_count;
    uint32_t face_count;
    uint32_t channel_count;    /* NEW: Multiple UV/color channels */
    uint32_t material_count;   /* NEW: Multiple materials */
    uint32_t bone_count;       /* NEW: Skeletal animation support */
    uint32_t morph_target_count; /* NEW: Blend shapes */
    uint32_t flags;
    uint32_t reserved;
} mesh_data_v5_t;

/* =============================================================================
 * SCHEMA DECLARATIONS
 * ============================================================================= */

/**
 * @brief Legacy mesh schema (v2-v4)
 *
 * Available in file versions 2-4, deprecated in version 5 (but still readable
 * for backward compatibility if needed).
 */
NMO_DECLARE_SCHEMA(MeshData_v2, mesh_data_v2_t) {
    SCHEMA_FIELD(vertex_count, u32, mesh_data_v2_t),
    SCHEMA_FIELD(face_count, u32, mesh_data_v2_t),
    SCHEMA_FIELD(material_id, u32, mesh_data_v2_t),
    SCHEMA_FIELD(flags, u32, mesh_data_v2_t)
};

/**
 * @brief Modern mesh schema (v5+)
 *
 * Introduced in version 5, used for all subsequent versions.
 */
NMO_DECLARE_SCHEMA(MeshData_v5, mesh_data_v5_t) {
    SCHEMA_FIELD(vertex_count, u32, mesh_data_v5_t),
    SCHEMA_FIELD(face_count, u32, mesh_data_v5_t),
    SCHEMA_FIELD(channel_count, u32, mesh_data_v5_t),
    SCHEMA_FIELD(material_count, u32, mesh_data_v5_t),
    SCHEMA_FIELD(bone_count, u32, mesh_data_v5_t),
    SCHEMA_FIELD(morph_target_count, u32, mesh_data_v5_t),
    SCHEMA_FIELD(flags, u32, mesh_data_v5_t),
    SCHEMA_FIELD(reserved, u32, mesh_data_v5_t)
};

/* =============================================================================
 * REGISTRATION FUNCTIONS
 * ============================================================================= */

/**
 * @brief Register legacy mesh schema (v2-v4)
 */
static nmo_result_t register_mesh_v2_schema(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    /* Create builder */
    nmo_schema_builder_t builder = nmo_builder_struct(
        arena,
        "MeshData",  /* Base name (no _v2 suffix for lookup) */
        sizeof(mesh_data_v2_t),
        _Alignof(mesh_data_v2_t));
    
    /* Set version range: since=2, deprecated=5, removed=0 */
    nmo_builder_set_since_version(&builder, 2);
    nmo_builder_set_deprecated_version(&builder, 5);
    /* Note: not removed (removed_version=0) so it can still be loaded for migration */
    
    /* Add fields from descriptor */
    for (size_t i = 0; i < sizeof(MeshData_v2_fields) / sizeof(MeshData_v2_fields[0]); i++) {
        const nmo_schema_field_descriptor_t *desc = &MeshData_v2_fields[i];
        const nmo_schema_type_t *field_type = 
            nmo_schema_registry_find_by_name(registry, desc->type_name);
        
        if (field_type == NULL) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
                NMO_SEVERITY_ERROR, "Field type not found"));
        }
        
        nmo_schema_field_t field = {
            .name = desc->name,
            .type = field_type,
            .offset = desc->offset,
            .annotations = desc->annotations,
            .since_version = desc->since_version,
            .deprecated_version = desc->deprecated_version,
            .removed_version = desc->removed_version
        };
        
        nmo_builder_add_field_manual(&builder, &field);
    }
    
    /* Build and register */
    return nmo_builder_build(&builder, registry);
}

/**
 * @brief Register modern mesh schema (v5+)
 */
static nmo_result_t register_mesh_v5_schema(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    /* Create builder */
    nmo_schema_builder_t builder = nmo_builder_struct(
        arena,
        "MeshData",  /* Same base name as v2 */
        sizeof(mesh_data_v5_t),
        _Alignof(mesh_data_v5_t));
    
    /* Set version range: since=5, never deprecated/removed */
    nmo_builder_set_since_version(&builder, 5);
    /* deprecated_version=0 and removed_version=0 means "still current" */
    
    /* Add fields from descriptor */
    for (size_t i = 0; i < sizeof(MeshData_v5_fields) / sizeof(MeshData_v5_fields[0]); i++) {
        const nmo_schema_field_descriptor_t *desc = &MeshData_v5_fields[i];
        const nmo_schema_type_t *field_type = 
            nmo_schema_registry_find_by_name(registry, desc->type_name);
        
        if (field_type == NULL) {
            return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
                NMO_SEVERITY_ERROR, "Field type not found"));
        }
        
        nmo_schema_field_t field = {
            .name = desc->name,
            .type = field_type,
            .offset = desc->offset,
            .annotations = desc->annotations,
            .since_version = desc->since_version,
            .deprecated_version = desc->deprecated_version,
            .removed_version = desc->removed_version
        };
        
        nmo_builder_add_field_manual(&builder, &field);
    }
    
    /* Build and register */
    return nmo_builder_build(&builder, registry);
}

/**
 * @brief Register all mesh data schema variants
 *
 * This function demonstrates how to register multiple versions of the same
 * type. The registry will contain both variants, and at load time, the
 * appropriate one is selected based on file version.
 *
 * @param registry Schema registry
 * @param arena Arena for allocations
 * @return Result with error on failure
 */
nmo_result_t nmo_register_multi_version_example(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    nmo_result_t result;
    
    /* Register legacy version (v2-v4) */
    result = register_mesh_v2_schema(registry, arena);
    if (result.code != NMO_OK) {
        return result;
    }
    
    /* Register modern version (v5+) */
    result = register_mesh_v5_schema(registry, arena);
    if (result.code != NMO_OK) {
        return result;
    }
    
    return nmo_result_ok();
}

/* =============================================================================
 * USAGE EXAMPLE
 * ============================================================================= */

/**
 * @brief Example: Load mesh data based on file version
 *
 * This shows how the version management system works at runtime.
 */
#if 0
nmo_result_t load_mesh_data(
    nmo_schema_registry_t *registry,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    uint32_t file_version,
    void **out_data)
{
    /* Find the right schema variant for this file version */
    const nmo_schema_type_t *schema = 
        nmo_schema_registry_find_for_version(registry, "MeshData", file_version);
    
    if (schema == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "No compatible MeshData schema for this version"));
    }
    
    /* Allocate buffer for the correct version */
    void *data = nmo_arena_alloc(arena, schema->size, schema->align);
    if (data == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
            NMO_SEVERITY_ERROR, "Failed to allocate mesh data"));
    }
    
    /* Read using the version-appropriate schema */
    nmo_result_t result = nmo_schema_read_struct(schema, chunk, arena, data);
    if (result.code != NMO_OK) {
        return result;
    }
    
    *out_data = data;
    return nmo_result_ok();
}

/**
 * @brief Example: Find all variants for migration analysis
 */
nmo_result_t analyze_mesh_versions(nmo_schema_registry_t *registry, nmo_arena_t *arena)
{
    const nmo_schema_type_t **variants = NULL;
    size_t count = 0;
    
    /* Get all MeshData variants */
    nmo_result_t result = nmo_schema_registry_find_all_variants(
        registry, "MeshData", arena, &variants, &count);
    
    if (result.code != NMO_OK) {
        return result;
    }
    
    printf("Found %zu MeshData variants:\n", count);
    for (size_t i = 0; i < count; i++) {
        const nmo_schema_type_t *variant = variants[i];
        printf("  - %s: size=%zu, since_version=%u, deprecated_version=%u\n",
               variant->name,
               variant->size,
               variant->since_version,
               variant->deprecated_version);
    }
    
    return nmo_result_ok();
}
#endif
