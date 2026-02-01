/**
 * @file ckcamera_schemas.c
 * @brief CKCamera schema definitions
 *
 * Implements schema for CKCamera and related camera types.
 * 
 * Based on Virtools SDK reference:
 * - CKCamera inherits from CK3dEntity
 * - Stores camera projection parameters (FOV, aspect, near/far planes)
 * - Supports orthographic and perspective projection
 * - Manages target point for camera orientation
 * 
 * Format structure (from reference Load/Save):
 * - CK3dEntity data (transform matrix, etc)
 * - Projection type (DWORD: CK_PERSPECTIVEPROJECTION or CK_ORTHOGRAPHICPROJECTION)
 * - FOV angle (float, radians)
 * - Orthographic zoom (float)
 * - Packed width/height (DWORD: low=width, high=height)
 * - Near clip plane (float)
 * - Far clip plane (float)
 */

#include "object/nmo_ckcamera_schemas.h"
#include "object/nmo_ck3dentity_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

/* Identifier constants from CKDefines2.h */
#define CK_STATESAVE_CAMERAFOV       0x00400000u
#define CK_STATESAVE_CAMERAPROJTYPE  0x00800000u
#define CK_STATESAVE_CAMERAOTHOZOOM  0x01000000u
#define CK_STATESAVE_CAMERAASPECT    0x02000000u
#define CK_STATESAVE_CAMERAPLANES    0x04000000u
#define CK_STATESAVE_CAMERAONLY      0x0FC00000u

/* =============================================================================
 * CKCamera DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKCamera state from chunk
 * 
 * Reads camera projection parameters and 3D entity transform.
 * 
 * Chunk format (version 7):
 * - CK3dEntity data (transform, flags, etc)
 * - DWORD projection_type
 * - float fov
 * - float orthographic_zoom
 * - DWORD packed dimensions
 * - float near_plane
 * - float far_plane
 * 
 * @param chunk Chunk containing CKCamera data
 * @param arena Arena for allocations
 * @param out_state Output structure to fill
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ckcamera_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckcamera_state_t *out_state)
{
    if (!chunk || !arena || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to CKCamera deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    // First deserialize parent CK3dEntity data
    nmo_result_t result = nmo_ck3dentity_deserialize(
        chunk, arena, &out_state->entity);
    if (result.code != NMO_OK) {
        return result;
    }

    if (nmo_chunk_get_data_version(chunk) < 5) {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CAMERAFOV).code == NMO_OK) {
            (void)nmo_chunk_read_float(chunk, &out_state->fov);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CAMERAPROJTYPE).code == NMO_OK) {
            (void)nmo_chunk_read_dword(chunk, &out_state->projection_type);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CAMERAOTHOZOOM).code == NMO_OK) {
            (void)nmo_chunk_read_float(chunk, &out_state->orthographic_zoom);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CAMERAASPECT).code == NMO_OK) {
            (void)nmo_chunk_read_int(chunk, &out_state->width);
            (void)nmo_chunk_read_int(chunk, &out_state->height);
        }
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CAMERAPLANES).code == NMO_OK) {
            (void)nmo_chunk_read_float(chunk, &out_state->near_plane);
            (void)nmo_chunk_read_float(chunk, &out_state->far_plane);
        }
        return nmo_result_ok();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CAMERAONLY).code == NMO_OK) {
        (void)nmo_chunk_read_dword(chunk, &out_state->projection_type);
        (void)nmo_chunk_read_float(chunk, &out_state->fov);
        (void)nmo_chunk_read_float(chunk, &out_state->orthographic_zoom);

        uint32_t packed = 0;
        (void)nmo_chunk_read_dword(chunk, &packed);
        out_state->width = (int32_t)(packed & 0xFFFF);
        out_state->height = (int32_t)((packed >> 16) & 0xFFFF);

        (void)nmo_chunk_read_float(chunk, &out_state->near_plane);
        (void)nmo_chunk_read_float(chunk, &out_state->far_plane);
    }

    return nmo_result_ok();
}

/* =============================================================================
 * CKCamera SERIALIZATION
 * ============================================================================= */

/**
 * @brief Serialize CKCamera state to chunk
 * 
 * @param state State to serialize
 * @param chunk Chunk to write to
 * @param arena Arena for temporary allocations
 * @return Result indicating success or error
 */
static nmo_result_t nmo_ckcamera_serialize(
    const nmo_ckcamera_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to CKCamera serialize"));
    }

    // First serialize parent CK3dEntity data
    nmo_result_t result = nmo_ck3dentity_serialize(
        &in_state->entity, out_chunk, arena);
    if (result.code != NMO_OK) {
        return result;
    }

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CAMERAONLY);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_dword(out_chunk, in_state->projection_type);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(out_chunk, in_state->fov);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(out_chunk, in_state->orthographic_zoom);
    if (result.code != NMO_OK) return result;

    uint32_t packed = ((uint32_t)(in_state->height & 0xFFFF) << 16) |
        (uint32_t)(in_state->width & 0xFFFF);
    result = nmo_chunk_write_dword(out_chunk, packed);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(out_chunk, in_state->near_plane);
    if (result.code != NMO_OK) return result;

    result = nmo_chunk_write_float(out_chunk, in_state->far_plane);
    if (result.code != NMO_OK) return result;

    return nmo_result_ok();
}

/* =============================================================================
 * SCHEMA REGISTRATION
 * ============================================================================= */

/**
 * @brief Vtable read wrapper for CKCamera
 */
static nmo_result_t nmo_ckcamera_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_ckcamera_deserialize(chunk, arena, (nmo_ckcamera_state_t *)out_ptr);
}

/**
 * @brief Vtable write wrapper for CKCamera
 */
static nmo_result_t nmo_ckcamera_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckcamera_serialize(
        (const nmo_ckcamera_state_t *)in_ptr,
        chunk,
        arena);
}

/**
 * @brief Vtable for CKCamera schema operations
 */
static const nmo_schema_vtable_t nmo_ckcamera_vtable = {
    .read = nmo_ckcamera_vtable_read,
    .write = nmo_ckcamera_vtable_write,
    .validate = NULL
};

/**
 * @brief Register CKCamera state schema
 * 
 * Creates schema descriptor for CKCamera state structure.
 * This is separate from class hierarchy registration in ckobject_hierarchy.c.
 * 
 * @param registry Schema registry to register into
 * @param arena Arena for schema allocations
 * @return Result indicating success or error
 */
nmo_result_t nmo_register_ckcamera_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (registry == NULL || arena == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckcamera_schemas"));
    }

    /* Get base types */
    const nmo_schema_type_t *float_type = nmo_schema_registry_find_by_name(registry, "f32");
    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "u32");
    
    if (float_type == NULL || uint32_type == NULL) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required types not found in registry"));
    }

    /* Register CKCamera state structure */
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "CKCameraState",
                                                      sizeof(nmo_ckcamera_state_t),
                                                      alignof(nmo_ckcamera_state_t));
    
    /* Camera projection parameters */
    nmo_builder_add_field_ex(&builder, "projection_type", uint32_type,
                            offsetof(nmo_ckcamera_state_t, projection_type),
                            0);
    nmo_builder_add_field_ex(&builder, "fov", float_type,
                            offsetof(nmo_ckcamera_state_t, fov),
                            0);
    nmo_builder_add_field_ex(&builder, "orthographic_zoom", float_type,
                            offsetof(nmo_ckcamera_state_t, orthographic_zoom),
                            0);
    nmo_builder_add_field_ex(&builder, "near_plane", float_type,
                            offsetof(nmo_ckcamera_state_t, near_plane),
                            0);
    nmo_builder_add_field_ex(&builder, "far_plane", float_type,
                            offsetof(nmo_ckcamera_state_t, far_plane),
                            0);
    nmo_builder_add_field_ex(&builder, "width", uint32_type,
                            offsetof(nmo_ckcamera_state_t, width),
                            0);
    nmo_builder_add_field_ex(&builder, "height", uint32_type,
                            offsetof(nmo_ckcamera_state_t, height),
                            0);
    
    /* Attach vtable for optimized read/write */
    nmo_builder_set_vtable(&builder, &nmo_ckcamera_vtable);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return result;
    }

    return nmo_result_ok();
}

/**
 * @brief Get CKCamera deserialize function pointer
 * 
 * Provides access to deserialization function for use in parser.c Phase 14.
 * 
 * @return Function pointer to nmo_ckcamera_deserialize
 */
nmo_ckcamera_deserialize_fn nmo_get_ckcamera_deserialize(void) {
    return nmo_ckcamera_deserialize;
}

/**
 * @brief Get CKCamera serialize function pointer
 * 
 * Provides access to serialization function for use in save pipeline.
 * 
 * @return Function pointer to nmo_ckcamera_serialize
 */
nmo_ckcamera_serialize_fn nmo_get_ckcamera_serialize(void) {
    return nmo_ckcamera_serialize;
}

/**
 * @brief Finish loading CKCamera
 * 
 * Performs reference resolution and runtime initialization for cameras.
 * 
 * @param state Camera state
 * @param arena Arena for allocations
 * @param repository Object repository for reference resolution
 * @return Result indicating success or error
 */
nmo_result_t nmo_ckcamera_finish_loading(
    void *state,
    nmo_arena_t *arena,
    void *repository)
{
    /* Camera-specific initialization could go here */
    (void)state;
    (void)arena;
    (void)repository;
    return nmo_result_ok();
}

/**
 * @brief Get finish_loading function for CKCamera
 * @return Finish loading function pointer
 */
nmo_ckcamera_finish_loading_fn nmo_get_ckcamera_finish_loading(void)
{
    return nmo_ckcamera_finish_loading;
}


