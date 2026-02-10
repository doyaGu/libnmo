/**
 * @file ckplace_schemas.c
 * @brief CKPlace schema implementation
 */

#include "object/nmo_ckplace_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_struct_guids.h"
#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(ckplace, nmo_ckplace_state_t)

static nmo_status_t nmo_ckplace_deserialize_internal(
    nmo_ckplace_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckplace_deserialize");
    }

    nmo_status_t result = nmo_ckbeobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    const int file_mode = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    if (!file_mode) {
        NMO_RETURN_OK();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACECAMERA) == NMO_OK) {
        out_state->has_camera = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->camera_id);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACELEVEL) == NMO_OK) {
        out_state->has_level = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->level_id);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACEPORTALS) == NMO_OK) {
        int32_t count = 0;
        if (nmo_chunk_read_int(chunk, &count) == NMO_OK && count > 0) {
            out_state->portal_count = (uint32_t)count;
            out_state->portals = (nmo_place_portal_entry_t *)nmo_arena_alloc(
                arena, sizeof(nmo_place_portal_entry_t) * out_state->portal_count,
                _Alignof(nmo_place_portal_entry_t));
            if (!out_state->portals) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate portal array");
            }

            for (uint32_t i = 0; i < out_state->portal_count; ++i) {
                (void)nmo_chunk_read_object_id(chunk, &out_state->portals[i].place_id);
                (void)nmo_chunk_read_object_id(chunk, &out_state->portals[i].portal_id);
            }
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACEREFERENCES) == NMO_OK) {
        nmo_object_id_t *ids = NULL;
        size_t count = 0;
        nmo_status_t result = nmo_chunk_read_object_id_array(chunk, &ids, &count, arena);
        if (result == NMO_OK && count > 0) {
            out_state->reference_count = (uint32_t)count;
            out_state->reference_ids = ids;
        }
    }

    NMO_RETURN_OK();
}

static const nmo_type_field_t nmo_ckplace_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_ckplace_state_t, base),
                    sizeof(nmo_ckbeobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_ckplace_state_t, has_camera, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_ckplace_state_t, camera_id),
    NMO_FIELD(nmo_ckplace_state_t, has_level, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_ckplace_state_t, level_id),
    NMO_FIELD(nmo_ckplace_state_t, portal_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_ckplace_state_t, portals, NMO_GUID_STRUCT_CKPLACEPORTALENTRY),
    NMO_FIELD(nmo_ckplace_state_t, reference_count, CKPGUID_UINT32),
    NMO_FIELD_REF_ARRAY(nmo_ckplace_state_t, reference_ids)
};

static nmo_status_t ckplace_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_ckplace_state_t *s = src;
    nmo_ckplace_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->portals,
                                              s->portals, sizeof(nmo_place_portal_entry_t), s->portal_count));
    return nmo_object_copy_array(arena, (void **)&d->reference_ids,
                                 s->reference_ids, sizeof(nmo_object_id_t), s->reference_count);
}

static nmo_status_t ckplace_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_ckplace_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->portals, s->portal_count, "portals");
    NMO_VALIDATE_COUNT(s->reference_ids, s->reference_count, "reference_ids");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    ckplace,
    nmo_ckplace_state_t,
    nmo_ckplace_serialize,
    nmo_ckplace_deserialize,
    nmo_ckplace_fields,
    CKPGUID_PLACE,
    "CKPlace",
    NMO_CID_PLACE,
    CKPGUID_3DENTITY
)

static nmo_status_t nmo_ckplace_serialize_internal(
    const nmo_ckplace_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckplace_serialize");
    }

    nmo_status_t result = nmo_ckbeobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    if (in_state->has_camera) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACECAMERA);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->camera_id);
        if (result != NMO_OK) return result;
    }

    const int file_mode = (out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    if (file_mode && in_state->has_level) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACELEVEL);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->level_id);
        if (result != NMO_OK) return result;
    }

    if (in_state->portal_count > 0 && in_state->portals) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACEPORTALS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->portal_count);
        if (result != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->portal_count; ++i) {
            result = nmo_chunk_write_object_id(out_chunk, in_state->portals[i].place_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->portals[i].portal_id);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_ckplace_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckplace_state_t *out_state = (nmo_ckplace_state_t *)instance;
    return nmo_ckplace_deserialize_internal(out_state, chunk, context);
}

nmo_status_t nmo_ckplace_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckplace_state_t *in_state = (const nmo_ckplace_state_t *)instance;
    return nmo_ckplace_serialize_internal(in_state, out_chunk, context);
}

