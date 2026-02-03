/**
 * @file cksprite3d_schemas.c
 * @brief CKSprite3D schema implementation
 */

#include "object/nmo_cksprite3d_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(cksprite3d, nmo_cksprite3d_state_t)

static nmo_status_t nmo_cksprite3d_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_cksprite3d_state_t *out_state)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksprite3d_deserialize");
    }

    nmo_status_t result = nmo_ck3dentity_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITE3DDATA) == NMO_OK) {
        out_state->has_data = 1;
        (void)nmo_chunk_read_dword(chunk, &out_state->mode);
        (void)nmo_chunk_read_float(chunk, &out_state->half_width);
        (void)nmo_chunk_read_float(chunk, &out_state->half_height);
        (void)nmo_chunk_read_float(chunk, &out_state->offset.x);
        (void)nmo_chunk_read_float(chunk, &out_state->offset.y);
        (void)nmo_chunk_read_float(chunk, &out_state->uv_rect.left);
        (void)nmo_chunk_read_float(chunk, &out_state->uv_rect.top);
        (void)nmo_chunk_read_float(chunk, &out_state->uv_rect.right);
        (void)nmo_chunk_read_float(chunk, &out_state->uv_rect.bottom);
        (void)nmo_chunk_read_object_id(chunk, &out_state->material_id);
    }

    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    cksprite3d,
    nmo_cksprite3d_state_t,
    nmo_cksprite3d_serialize,
    nmo_cksprite3d_deserialize,
    NMO_GUID_CKSPRITE3D,
    "CKSprite3D",
    NMO_CID_SPRITE3D,
    NMO_GUID_CK3DENTITY
)

static nmo_status_t nmo_cksprite3d_serialize_internal(
    const nmo_cksprite3d_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_cksprite3d_serialize");
    }

    nmo_status_t result = nmo_ck3dentity_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (in_state->has_data) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_SPRITE3DDATA);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->mode);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->half_width);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->half_height);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->offset.x);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->offset.y);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->uv_rect.left);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->uv_rect.top);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->uv_rect.right);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->uv_rect.bottom);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->material_id);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_cksprite3d_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_cksprite3d_state_t *out_state = (nmo_cksprite3d_state_t *)instance;
    return nmo_cksprite3d_deserialize_internal(chunk, context, out_state);
}

nmo_status_t nmo_cksprite3d_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_cksprite3d_state_t *in_state = (const nmo_cksprite3d_state_t *)instance;
    return nmo_cksprite3d_serialize_internal(in_state, out_chunk, context);
}
