/**
 * @file ck3dentity_schemas.c
 * @brief CK3dEntity schema definitions
 *
 * Implements schema for CK3dEntity and related 3D entity types.
 * 
 * Based on Virtools SDK reference:
 * - CK3dEntity is the base class for all 3D positioned objects
 * - Stores transformation matrix (position, rotation, scale)
 * - Manages parent-child hierarchy for scene graph
 * - Handles world/local transform computations
 * 
 * Format structure (from reference Load/Save):
 * - World Matrix (VxMatrix 4x4 = 64 bytes)
 * - Flags (DWORD)
 * - Optional: Parent object ID (if parented)
 * - Optional: Z-order data (rendering priority)
 * - Optional: Additional transform data (pivot, bounding box)
 * 
 * This is a PARTIAL schema as mentioned in TODO - we preserve unknown
 * data via raw_tail fields for future math/render schema integration.
 */

#include "schema/nmo_ck3dentity_schemas.h"
#include "schema/nmo_ckrenderobject_schemas.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_schema_builder.h"
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
 * CK3dEntity DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CK3dEntity state from chunk
 * 
 * Reads the 3D entity transformation data and parent references.
 * This is a PARTIAL implementation - some fields are preserved in raw_tail
 * for future schema refinement.
 * 
 * Chunk format (version 7):
 * - DWORD flags (visibility, activity, etc from CKBeObject)
 * - 16 floats: 4x4 world transformation matrix
 * - DWORD entity_flags (local/world transform, etc)
 * - Optional data (preserved as raw_tail):
 *   - Parent object reference
 *   - Z-order/rendering data
 *   - Bounding box
 *   - Pivot point
 * 
 * @param chunk Chunk containing CK3dEntity data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
nmo_result_t nmo_ck3dentity_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ck3dentity_state_t *out_state)
{
    if (!chunk || !arena || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to CK3dEntity deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    // First deserialize parent CKRenderObject data
    nmo_result_t result = nmo_ckrenderobject_deserialize(
        chunk, arena, &out_state->render_object);
    if (result.code != NMO_OK) {
        return result;
    }

    // Read world transformation matrix (4x4 = 16 floats)
    for (int i = 0; i < 16; i++) {
        result = nmo_chunk_read_float(chunk, &out_state->world_matrix[i]);
        if (result.code != NMO_OK) {
            nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                          NMO_SEVERITY_ERROR,
                                          "Failed to read world matrix element");
            nmo_error_add_cause(err, result.error);
            return nmo_result_error(err);
        }
    }

    // Read entity flags
    result = nmo_chunk_read_dword(chunk, &out_state->entity_flags);
    if (result.code != NMO_OK) {
        nmo_error_t *err = NMO_ERROR(arena, NMO_ERR_VALIDATION_FAILED,
                                      NMO_SEVERITY_ERROR,
                                      "Failed to read entity flags");
        nmo_error_add_cause(err, result.error);
        return nmo_result_error(err);
    }

    // Preserve remaining data as raw tail for future schema refinement
    // This includes: parent ref, z-order, bounding box, pivot point
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
 * CK3dEntity SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CK3dEntity state to chunk
 * 
 * Writes the 3D entity transformation data in the original format.
 * 
 * @param state State to serialize
 * @param chunk Chunk to write to
 * @param arena Arena for temporary allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_ck3dentity_serialize(
    const nmo_ck3dentity_state_t *state,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena)
{
    if (!state || !chunk || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to CK3dEntity serialize"));
    }

    // First serialize parent CKRenderObject data
    nmo_result_t result = nmo_ckrenderobject_serialize(
        chunk, &state->render_object);
    if (result.code != NMO_OK) {
        return result;
    }

    // Write world transformation matrix
    for (int i = 0; i < 16; i++) {
        result = nmo_chunk_write_float(chunk, state->world_matrix[i]);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    // Write entity flags
    result = nmo_chunk_write_dword(chunk, state->entity_flags);
    if (result.code != NMO_OK) {
        return result;
    }

    // Write preserved tail data
    if (state->raw_tail_size > 0 && state->raw_tail) {
        result = nmo_chunk_write_buffer_no_size(chunk, state->raw_tail, state->raw_tail_size);
        if (result.code != NMO_OK) {
            return result;
        }
    }

    return nmo_result_ok();
}

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Register CK3dEntity state schema
 * 
 * Creates schema descriptor for CK3dEntity state structure.
 * This is separate from class hierarchy registration in ckobject_hierarchy.c.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ck3dentity_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (registry == NULL || arena == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ck3dentity_schemas"));
    }

    /* Get base types */
    const nmo_schema_type_t *float_type = nmo_schema_registry_find_by_name(registry, "float");
    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "uint32_t");
    
    if (float_type == NULL || uint32_type == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required types not found in registry"));
    }

    /* Register CK3dEntity state structure */
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CK3dEntityState",
                                                      sizeof(nmo_ck3dentity_state_t),
                                                      alignof(nmo_ck3dentity_state_t));
    
    /* World transformation matrix (16 floats) - simplified representation */
    nmo_builder_add_field_ex(&builder, "world_matrix", float_type,
                            offsetof(nmo_ck3dentity_state_t, world_matrix),
                            0);
    
    nmo_builder_add_field_ex(&builder, "entity_flags", uint32_type,
                            offsetof(nmo_ck3dentity_state_t, entity_flags),
                            0);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

/**
 * @brief Get CK3dEntity deserialize function pointer
 * 
 * Provides access to deserialization function for use in parser.c Phase 14.
 * 
 * @return Function pointer to nmo_ck3dentity_deserialize
 */
nmo_ck3dentity_deserialize_fn nmo_get_ck3dentity_deserialize(void) {
    return nmo_ck3dentity_deserialize;
}

/**
 * @brief Get CK3dEntity serialize function pointer
 * 
 * Provides access to serialization function for use in save pipeline.
 * 
 * @return Function pointer to nmo_ck3dentity_serialize
 */
nmo_ck3dentity_serialize_fn nmo_get_ck3dentity_serialize(void) {
    return nmo_ck3dentity_serialize;
}

/**
 * @brief Finish loading CK3dEntity
 * 
 * Performs reference resolution and runtime initialization.
 * Base implementation for 3D entities - derived classes may extend.
 * 
 * @param state Entity state
 * @param arena Arena for allocations
 * @param repository Object repository for reference resolution
 * @return Result indicating success or error
 */
nmo_result_t nmo_ck3dentity_finish_loading(
    void *state,
    nmo_arena_t *arena,
    void *repository)
{
    /* Base implementation does nothing special beyond RenderObject */
    (void)state;
    (void)arena;
    (void)repository;
    return nmo_result_ok();
}

/**
 * @brief Get finish_loading function for CK3dEntity
 * @return Finish loading function pointer
 */
nmo_ck3dentity_finish_loading_fn nmo_get_ck3dentity_finish_loading(void)
{
    return nmo_ck3dentity_finish_loading;
}
