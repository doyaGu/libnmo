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
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ck3dentity_schemas.h"
#include "object/nmo_schema_interface.h"
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
nmo_result_t nmo_ckcamera_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckcamera_state_t *out_state = (nmo_ckcamera_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!chunk || !arena || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to CKCamera deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    // First deserialize parent CK3dEntity data
    nmo_result_t result = nmo_ck3dentity_deserialize(&out_state->entity, chunk, NULL, context);
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
nmo_result_t nmo_ckcamera_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckcamera_state_t *in_state = (const nmo_ckcamera_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
                                          NMO_SEVERITY_ERROR,
                                          "Invalid arguments to CKCamera serialize"));
    }

    // First serialize parent CK3dEntity data
    nmo_result_t result = nmo_ck3dentity_serialize(&in_state->entity, out_chunk, NULL, context);
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

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckcamera,
    nmo_ckcamera_state_t,
    nmo_ckcamera_serialize,
    nmo_ckcamera_deserialize,
    NMO_GUID_CKCAMERA,
    "CKCamera",
    NMO_CID_CAMERA,
    NMO_GUID_CK3DENTITY
)


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
    void *instance,
    nmo_arena_t *arena,
    void *repository)
{
    /* Camera-specific initialization could go here */
    (void)instance;
    (void)arena;
    (void)repository;
    return nmo_result_ok();
}


