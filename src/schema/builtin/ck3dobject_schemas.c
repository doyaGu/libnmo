/**
 * @file ck3dobject_schemas.c
 * @brief CK3dObject schema definitions
 *
 * Implements schema for CK3dObject (3D mesh objects).
 * 
 * Based on Virtools SDK reference:
 * - CK3dObject inherits from CK3dEntity
 * - Adds mesh reference and rendering properties
 * - Stores material, texture, and visibility settings
 * - Manages mesh deformation and animation data
 * 
 * Format structure (from reference Load/Save):
 * - CK3dEntity data (transform matrix, etc)
 * - Mesh reference (object ID)
 * - Rendering flags (wireframe, two-sided, etc)
 * - Optional: Material overrides
 * - Optional: Vertex deformation data
 * - Optional: Bounding box override
 * 
 * This is a PARTIAL schema - mesh/material details preserved in raw_tail.
 */

#include "schema/nmo_ck3dobject_schemas.h"
#include "schema/nmo_ck3dentity_schemas.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_schema_builder.h"
#include "schema/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>
#include "nmo_types.h"
#include <stddef.h>
#include <string.h>

/* =============================================================================
 * CK3dObject DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CK3dObject state from chunk
 * 
 * Reads 3D object mesh references and rendering flags.
 * 
 * Chunk format (version 7):
 * - CK3dEntity data (transform, flags, etc)
 * - DWORD mesh_id (object ID of CKMesh)
 * - DWORD rendering_flags (wireframe, culling, etc)
 * - Remaining data preserved as raw_tail (materials, deformations)
 * 
 * @param chunk Chunk containing CK3dObject data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ck3dobject_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck3dobject_state_t *out_state)
{
    if (!chunk || !arena || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to CK3dObject deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    // First deserialize parent CK3dEntity data
    nmo_result_t result = nmo_ck3dentity_deserialize(
        chunk, arena, &out_state->entity);
    if (result.code != NMO_OK) {
        return result;
    }

    // Read mesh reference
    result = nmo_chunk_read_object_id(chunk, &out_state->mesh_id);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read mesh ID");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }

    // Read rendering flags
    result = nmo_chunk_read_dword(chunk, &out_state->rendering_flags);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read rendering flags");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }

    // Preserve remaining data as raw tail
    // This includes: materials, textures, vertex deformations, etc
    size_t current_pos = nmo_chunk_get_position(chunk);
    size_t chunk_size = nmo_chunk_get_data_size(chunk);
    
    if (current_pos < chunk_size) {
        size_t remaining = chunk_size - current_pos;
        out_state->raw_tail = (uint8_t *)nmo_arena_alloc(arena, remaining, 1);
        if (out_state->raw_tail) {
            size_t bytes_read = nmo_chunk_read_and_fill_buffer(chunk, 
                out_state->raw_tail, remaining);
            if (bytes_read == remaining) {
                out_state->raw_tail_size = remaining;
            } else {
                out_state->raw_tail = NULL;
                out_state->raw_tail_size = 0;
            }
        }
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CK3dObject SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CK3dObject state to chunk
 * 
 * @param state State to serialize
 * @param chunk Chunk to write to
 * @param arena Arena for temporary allocations
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ck3dobject_serialize(
    const nmo_ck3dobject_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to CK3dObject serialize"));
    }

    // First serialize parent CK3dEntity data
    nmo_result_t result = nmo_ck3dentity_serialize(
        &in_state->entity, out_chunk, arena);
    if (result.code != NMO_OK) {
        return result;
    }

    // Write mesh reference
    result = nmo_chunk_write_object_id(out_chunk, in_state->mesh_id);
    if (result.code != NMO_OK) {
        return result;
    }

    // Write rendering flags
    result = nmo_chunk_write_dword(out_chunk, in_state->rendering_flags);
    if (result.code != NMO_OK) {
        return result;
    }

    // Write preserved tail data
    if (in_state->raw_tail_size > 0 && in_state->raw_tail) {
        result = nmo_chunk_write_buffer_no_size(out_chunk, in_state->raw_tail, in_state->raw_tail_size);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    return nmo_result_ok();
}

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/* =============================================================================
 * VTABLE IMPLEMENTATION
 * ============================================================================= */

static nmo_result_t vtable_read_ck3dobject(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, nmo_arena_t *arena, void *out_ptr) {
    (void)type;
    return nmo_ck3dobject_deserialize(chunk, arena, (nmo_ck3dobject_state_t *)out_ptr);
}

static nmo_result_t vtable_write_ck3dobject(const nmo_schema_type_t *type,
    nmo_chunk_t *chunk, const void *in_ptr, nmo_arena_t *arena) {
    (void)type;
    return nmo_ck3dobject_serialize((const nmo_ck3dobject_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_ck3dobject_vtable = {
    .read = vtable_read_ck3dobject,
    .write = vtable_write_ck3dobject,
    .validate = NULL
};

/**
 * @brief Register CK3dObject schema
 */
/**
 * @brief Register CK3dObject state schema
 * 
 * Creates schema descriptor for CK3dObject state structure.
 * This is separate from class hierarchy registration in ckobject_hierarchy.c.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ck3dobject_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (registry == NULL || arena == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ck3dobject_schemas"));
    }

    /* Get base types */
    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "u32");
    
    if (uint32_type == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required uint32_t type not found in registry"));
    }

    /* Register CK3dObject state structure */
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CK3dObjectState",
                                                      sizeof(nmo_ck3dobject_state_t),
                                                      alignof(nmo_ck3dobject_state_t));
    
    /* Mesh reference and rendering flags */
    nmo_builder_add_field_ex(&builder, "mesh_id", uint32_type,
                            offsetof(nmo_ck3dobject_state_t, mesh_id),
                            0);
    nmo_builder_add_field_ex(&builder, "rendering_flags", uint32_type,
                            offsetof(nmo_ck3dobject_state_t, rendering_flags),
                            0);
    
    /* Set vtable for automated serialization */
    nmo_builder_set_vtable(&builder, &nmo_ck3dobject_vtable);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }

    /* Map class ID to schema */
    const nmo_schema_type_t *type = nmo_schema_registry_find_by_name(registry, "CK3dObjectState");
    if (type) {
        result = nmo_schema_registry_map_class_id(registry, NMO_CID_3DOBJECT, type);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    return nmo_result_ok();
}

/**
 * @brief Get CK3dObject deserialize function pointer
 * 
 * Provides access to deserialization function for use in parser.c Phase 14.
 * 
 * @return Function pointer to nmo_ck3dobject_deserialize
 */
nmo_ck3dobject_deserialize_fn nmo_get_ck3dobject_deserialize(void) {
    return nmo_ck3dobject_deserialize;
}

/**
 * @brief Get CK3dObject serialize function pointer
 * 
 * Provides access to serialization function for use in save pipeline.
 * 
 * @return Function pointer to nmo_ck3dobject_serialize
 */
nmo_ck3dobject_serialize_fn nmo_get_ck3dobject_serialize(void) {
    return nmo_ck3dobject_serialize;
}

/**
 * @brief Finish loading CK3dObject
 * 
 * Performs reference resolution for mesh linkage and material setup.
 * 
 * @param state 3D object state
 * @param arena Arena for allocations
 * @param repository Object repository for reference resolution
 * @return Result indicating success or error
 */
nmo_result_t nmo_ck3dobject_finish_loading(
    void *state,
    nmo_arena_t *arena,
    void *repository)
{
    /* Mesh reference resolution would go here */
    (void)state;
    (void)arena;
    (void)repository;
    return nmo_result_ok();
}

/**
 * @brief Get finish_loading function for CK3dObject
 * @return Finish loading function pointer
 */
nmo_ck3dobject_finish_loading_fn nmo_get_ck3dobject_finish_loading(void)
{
    return nmo_ck3dobject_finish_loading;
}

