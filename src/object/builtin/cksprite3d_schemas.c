/**
 * @file cksprite3d_schemas.c
 * @brief CKSprite3D schema implementation
 */

#include "object/nmo_sprite3d_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_object_enum_guids.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "type/nmo_reflection.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(sprite3d, nmo_sprite3d_state_t)

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_sprite3d_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_sprite3d_state_t, base),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_sprite3d_state_t, has_data, CKPGUID_BOOL),
    NMO_FIELD(nmo_sprite3d_state_t, mode, NMO_GUID_ENUM_VXSPRITE3D_TYPE),
    NMO_FIELD(nmo_sprite3d_state_t, half_width, CKPGUID_FLOAT),
    NMO_FIELD(nmo_sprite3d_state_t, half_height, CKPGUID_FLOAT),
    NMO_FIELD(nmo_sprite3d_state_t, offset, CKPGUID_2DVECTOR),
    NMO_FIELD(nmo_sprite3d_state_t, uv_rect, CKPGUID_RECT),
    NMO_FIELD_REF(nmo_sprite3d_state_t, material_id)
};

static nmo_status_t nmo_sprite3d_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_sprite3d_state_t *out_state)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_sprite3d_deserialize");
    }

    nmo_status_t result = nmo_3dentity_deserialize(&out_state->base, chunk, NULL, context);
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

NMO_DEFINE_OBJECT_SCHEMA_FIELDS(
    sprite3d,
    nmo_sprite3d_state_t,
    nmo_sprite3d_serialize,
    nmo_sprite3d_deserialize,
    nmo_sprite3d_fields,
    CKPGUID_SPRITE3D,
    "CKSprite3D",
    NMO_CID_SPRITE3D,
    CKPGUID_3DENTITY
)

static nmo_status_t nmo_sprite3d_serialize_internal(
    const nmo_sprite3d_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_sprite3d_serialize");
    }

    nmo_status_t result = nmo_3dentity_serialize(&in_state->base, out_chunk, NULL, context);
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

nmo_status_t nmo_sprite3d_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_sprite3d_state_t *out_state = (nmo_sprite3d_state_t *)instance;
    return nmo_sprite3d_deserialize_internal(chunk, context, out_state);
}

nmo_status_t nmo_sprite3d_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_sprite3d_state_t *in_state = (const nmo_sprite3d_state_t *)instance;
    return nmo_sprite3d_serialize_internal(in_state, out_chunk, context);
}

