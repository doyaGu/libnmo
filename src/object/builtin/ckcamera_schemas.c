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
 * - Projection type (DWORD: CK_CAMERA_PROJECTION)
 * - FOV (float, full horizontal angle in radians)
 * - Orthographic zoom (float)
 * - Aspect ratio (DWORD: low=width, high=height of the ratio)
 * - Near clip plane (float)
 * - Far clip plane (float)
 */

#include "object/builtin/nmo_camera_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "type/nmo_param_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include "nmo_types.h"
#include <stddef.h>
#include <stdalign.h>
#include <string.h>

static void nmo_camera_set_defaults(void *instance)
{
    nmo_camera_state_t *state = instance;
    if (state == NULL) {
        return;
    }

    /* Mirrors RCKCamera ctor defaults (see CKRenderEngine/src/CKCamera.cpp). */
    state->projection_type = CK_PERSPECTIVEPROJECTION;
    state->fov = 0.5f;
    state->orthographic_zoom = 1.0f;
    state->aspect_width = 4;
    state->aspect_height = 3;
    state->near_plane = 1.0f;
    state->far_plane = 4000.0f;

    state->has_cameraonly_chunk = 1;
    state->has_fov_chunk = 0;
    state->has_proj_chunk = 0;
    state->has_ortho_chunk = 0;
    state->has_aspect_chunk = 0;
    state->has_planes_chunk = 0;
}

static const nmo_object_state_member_t nmo_camera_members[] = {
    NMO_STATE_VALUE(nmo_camera_state_t, projection_type),
    NMO_STATE_VALUE(nmo_camera_state_t, fov),
    NMO_STATE_VALUE(nmo_camera_state_t, orthographic_zoom),
    NMO_STATE_VALUE(nmo_camera_state_t, aspect_width),
    NMO_STATE_VALUE(nmo_camera_state_t, aspect_height),
    NMO_STATE_VALUE(nmo_camera_state_t, near_plane),
    NMO_STATE_VALUE(nmo_camera_state_t, far_plane),
    NMO_STATE_VALUE(nmo_camera_state_t, has_cameraonly_chunk),
    NMO_STATE_VALUE(nmo_camera_state_t, has_fov_chunk),
    NMO_STATE_VALUE(nmo_camera_state_t, has_proj_chunk),
    NMO_STATE_VALUE(nmo_camera_state_t, has_ortho_chunk),
    NMO_STATE_VALUE(nmo_camera_state_t, has_aspect_chunk),
    NMO_STATE_VALUE(nmo_camera_state_t, has_planes_chunk)
};

static const nmo_object_state_layout_t nmo_camera_layout = {
    .size = sizeof(nmo_camera_state_t),
    .base_vtable = &nmo_3dentity_vtable,
    .base_size = sizeof(nmo_3dentity_state_t),
    .members = nmo_camera_members,
    .member_count =
        sizeof(nmo_camera_members) / sizeof(nmo_camera_members[0]),
    .set_defaults = nmo_camera_set_defaults,
};

NMO_DEFINE_OBJECT_LAYOUT_OPS(camera, nmo_camera_layout)

static void nmo_camera_dispose_base_arrays(nmo_camera_state_t *state)
{
    if (state == NULL) return;
    nmo_beobject_state_t *beobject = &state->entity.base.base;
    nmo_array_dispose(&beobject->scripts);
    nmo_array_dispose(&beobject->attributes);
    nmo_array_dispose(&beobject->legacy_attributes);
}

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_camera_fields[] = {
    NMO_FIELD_NAMED("entity", offsetof(nmo_camera_state_t, entity),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_camera_state_t, projection_type, CKPGUID_UINT32),
    NMO_FIELD(nmo_camera_state_t, fov, CKPGUID_FLOAT),
    NMO_FIELD(nmo_camera_state_t, orthographic_zoom, CKPGUID_FLOAT),
    NMO_FIELD(nmo_camera_state_t, aspect_width, CKPGUID_INT),
    NMO_FIELD(nmo_camera_state_t, aspect_height, CKPGUID_INT),
    NMO_FIELD(nmo_camera_state_t, near_plane, CKPGUID_FLOAT),
    NMO_FIELD(nmo_camera_state_t, far_plane, CKPGUID_FLOAT),
    NMO_FIELD(nmo_camera_state_t, has_cameraonly_chunk, CKPGUID_UINT8),
    NMO_FIELD(nmo_camera_state_t, has_fov_chunk, CKPGUID_UINT8),
    NMO_FIELD(nmo_camera_state_t, has_proj_chunk, CKPGUID_UINT8),
    NMO_FIELD(nmo_camera_state_t, has_ortho_chunk, CKPGUID_UINT8),
    NMO_FIELD(nmo_camera_state_t, has_aspect_chunk, CKPGUID_UINT8),
    NMO_FIELD(nmo_camera_state_t, has_planes_chunk, CKPGUID_UINT8)
};

/* =============================================================================
 * CKCamera DESERIALIZATION
 * ============================================================================= */

/**
 * @brief Deserialize CKCamera state from chunk
 * 
 * Reads camera projection parameters and 3D entity transform.
 * 
 * Chunk format (data version 5 and later):
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
static nmo_status_t nmo_camera_deserialize_internal(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_camera_state_t *out_state = (nmo_camera_state_t *)instance;
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);

    if (!chunk || !arena || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to CKCamera deserialize");
    }

    // First deserialize parent CK3dEntity data
    nmo_status_t result = nmo_3dentity_deserialize(&out_state->entity, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    out_state->has_cameraonly_chunk = 0;
    out_state->has_fov_chunk = 0;
    out_state->has_proj_chunk = 0;
    out_state->has_ortho_chunk = 0;
    out_state->has_aspect_chunk = 0;
    out_state->has_planes_chunk = 0;

    const uint32_t data_version = nmo_chunk_get_data_version(chunk);
    if (data_version < 5) {
        size_t payload_dwords = 0;
        nmo_status_t seek_result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CAMERAFOV, &payload_dwords);
        if (seek_result == NMO_OK) {
            if (payload_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
            out_state->has_fov_chunk = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->fov));
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
        seek_result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CAMERAPROJTYPE, &payload_dwords);
        if (seek_result == NMO_OK) {
            if (payload_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
            out_state->has_proj_chunk = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->projection_type));
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
        seek_result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CAMERAOTHOZOOM, &payload_dwords);
        if (seek_result == NMO_OK) {
            if (payload_dwords < 1u) return NMO_ERR_TRUNCATED_CHUNK;
            out_state->has_ortho_chunk = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->orthographic_zoom));
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
        seek_result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CAMERAASPECT, &payload_dwords);
        if (seek_result == NMO_OK) {
            if (payload_dwords < 2u) return NMO_ERR_TRUNCATED_CHUNK;
            out_state->has_aspect_chunk = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->aspect_width));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &out_state->aspect_height));
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
        seek_result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CAMERAPLANES, &payload_dwords);
        if (seek_result == NMO_OK) {
            if (payload_dwords < 2u) return NMO_ERR_TRUNCATED_CHUNK;
            out_state->has_planes_chunk = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->near_plane));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->far_plane));
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    } else {
        size_t payload_dwords = 0;
        nmo_status_t seek_result = nmo_chunk_seek_identifier_with_size(
            chunk, CK_STATESAVE_CAMERAONLY, &payload_dwords);
        if (seek_result == NMO_OK) {
            if (payload_dwords < 6u) return NMO_ERR_TRUNCATED_CHUNK;
            out_state->has_cameraonly_chunk = 1;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, (int32_t *)&out_state->projection_type));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->fov));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->orthographic_zoom));

            uint32_t packed = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &packed));
            out_state->aspect_width = (int32_t)(packed & 0xFFFF);
            out_state->aspect_height = (int32_t)((packed >> 16) & 0xFFFF);

            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->near_plane));
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->far_plane));
        } else if (seek_result != NMO_ERR_NOT_FOUND) return seek_result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_camera_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_camera_state_t *out_state = (nmo_camera_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_camera_state_t decoded;
    nmo_status_t result = nmo_camera_create(&decoded, type, context);
    if (result != NMO_OK) return result;

    result = nmo_camera_deserialize_internal(&decoded, chunk, type, context);
    if (result != NMO_OK) {
        nmo_camera_dispose_base_arrays(&decoded);
        return result;
    }

    nmo_camera_dispose_base_arrays(out_state);
    *out_state = decoded;
    return NMO_OK;
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
static nmo_status_t nmo_camera_serialize_internal(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_camera_state_t *in_state = (const nmo_camera_state_t *)instance;
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);

    if (!in_state || !out_chunk || !arena) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to CKCamera serialize");
    }

    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    const bool is_file = ((out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
    const uint32_t save_flags =
        nmo_serialize_context_get_save_flags(context);
    const bool write_camera = is_file ||
        (save_flags & CK_STATESAVE_CAMERAONLY) != 0;
    const uint32_t data_version = nmo_chunk_get_data_version(out_chunk);
    const bool has_legacy_layout =
        in_state->has_fov_chunk || in_state->has_proj_chunk ||
        in_state->has_ortho_chunk || in_state->has_aspect_chunk ||
        in_state->has_planes_chunk;
    const bool write_legacy = is_file && data_version < 5u &&
        (data_version != 0u || has_legacy_layout);
    if (data_version == 0u && !write_legacy) {
        out_chunk->data_version = NMO_CHUNK_DATA_VERSION_CURRENT;
    }
    const bool has_default_values =
        in_state->projection_type == CK_PERSPECTIVEPROJECTION && in_state->fov == 0.5f &&
        in_state->orthographic_zoom == 1.0f &&
        in_state->aspect_width == 4 && in_state->aspect_height == 3 &&
        in_state->near_plane == 1.0f && in_state->far_plane == 4000.0f;

    if (write_legacy) {
        if (in_state->has_cameraonly_chunk ||
            (!in_state->has_fov_chunk && in_state->fov != 0.5f) ||
            (!in_state->has_proj_chunk && in_state->projection_type != CK_PERSPECTIVEPROJECTION) ||
            (!in_state->has_ortho_chunk &&
             in_state->orthographic_zoom != 1.0f) ||
            (!in_state->has_aspect_chunk &&
             (in_state->aspect_width != 4 || in_state->aspect_height != 3)) ||
            (!in_state->has_planes_chunk &&
             (in_state->near_plane != 1.0f ||
              in_state->far_plane != 4000.0f))) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "Camera state does not match its legacy section layout");
        }
    } else if (is_file) {
        if (has_legacy_layout ||
            (!in_state->has_cameraonly_chunk && !has_default_values)) {
            NMO_RETURN_ERROR(
                NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                "Camera state does not match its modern section layout");
        }
    }

    const bool writes_packed_layout =
        (!is_file && write_camera) ||
        (is_file && !write_legacy &&
         in_state->has_cameraonly_chunk);
    if (writes_packed_layout &&
        (in_state->aspect_width < 0 || in_state->aspect_width > UINT16_MAX ||
         in_state->aspect_height < 0 || in_state->aspect_height > UINT16_MAX)) {
        NMO_RETURN_ERROR(
            NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
            "Camera dimensions cannot be represented by the packed layout");
    }

    // First serialize parent CK3dEntity data
    nmo_status_t result = nmo_3dentity_serialize(&in_state->entity, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (!write_camera) return NMO_OK;

    if (!is_file || !write_legacy) {
        if (is_file && !in_state->has_cameraonly_chunk) return NMO_OK;

        NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(
            out_chunk, CK_STATESAVE_CAMERAONLY));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
            out_chunk, in_state->projection_type));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(out_chunk, in_state->fov));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            out_chunk, in_state->orthographic_zoom));

        const uint32_t packed = ((uint32_t)in_state->aspect_height << 16) |
            (uint32_t)in_state->aspect_width;
        NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(out_chunk, packed));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            out_chunk, in_state->near_plane));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            out_chunk, in_state->far_plane));
        NMO_RETURN_OK();
    }

    if (in_state->has_fov_chunk) {
        NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(
            out_chunk, CK_STATESAVE_CAMERAFOV));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(out_chunk, in_state->fov));
    }
    if (in_state->has_proj_chunk) {
        NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(
            out_chunk, CK_STATESAVE_CAMERAPROJTYPE));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_dword(
            out_chunk, in_state->projection_type));
    }
    if (in_state->has_ortho_chunk) {
        NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(
            out_chunk, CK_STATESAVE_CAMERAOTHOZOOM));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            out_chunk, in_state->orthographic_zoom));
    }
    if (in_state->has_aspect_chunk) {
        NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(
            out_chunk, CK_STATESAVE_CAMERAASPECT));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, in_state->aspect_width));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_int(out_chunk, in_state->aspect_height));
    }
    if (in_state->has_planes_chunk) {
        NMO_RETURN_IF_ERROR(nmo_chunk_write_identifier(
            out_chunk, CK_STATESAVE_CAMERAPLANES));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            out_chunk, in_state->near_plane));
        NMO_RETURN_IF_ERROR(nmo_chunk_write_float(
            out_chunk, in_state->far_plane));
    }

    NMO_RETURN_OK();
}

NMO_DEFINE_OBJECT_STAGED_SERIALIZE(nmo_camera)

/**
 * @brief Runtime dependency preparation for CKCamera.
 */
NMO_DEFINE_OBJECT_PREPARE_DEFAULT(nmo_camera)

/**
 * @brief Runtime dependency remap for CKCamera.
 */
nmo_status_t nmo_camera_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_camera_remap_dependencies");
    }

    nmo_camera_state_t *state = (nmo_camera_state_t *)instance;
    nmo_status_t result = nmo_3dentity_remap_dependencies(&state->entity, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    return nmo_object_default_validate(state, NULL, NULL);
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

static nmo_status_t nmo_camera_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_camera_state_t *state = instance;
    return nmo_3dentity_vtable.validate(&state->entity, NULL, context);
}

nmo_type_vtable_t nmo_camera_vtable = {
    .prepare_dependencies = nmo_camera_prepare_dependencies,
    .remap_dependencies = nmo_camera_remap_dependencies,
    .pre_delete = nmo_object_pre_delete_checked,
    .post_delete = nmo_object_post_delete_noop,
    NMO_OBJECT_VTABLE(
        nmo_camera_create,
        nmo_camera_destroy,
        nmo_camera_serialize,
        nmo_camera_deserialize,
        nmo_camera_copy,
        nmo_camera_validate,
        nmo_camera_equals,
        nmo_camera_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_camera_type,
    CKPGUID_CAMERA,
    "CKCamera",
    NMO_CID_CAMERA,
    CKPGUID_3DENTITY,
    nmo_camera_state_t,
    &nmo_camera_vtable,
    nmo_camera_fields)
