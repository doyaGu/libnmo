/**
 * @file ckplace_schemas.c
 * @brief CKPlace schema implementation
 */

#include "object/nmo_place_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_struct_guids.h"
#include "object/nmo_beobject_schemas.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_array.h"
#include "core/nmo_arena.h"
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    place,
    nmo_place_state_t,
    do {
        nmo_status_t result = nmo_array_init(&state->portals, sizeof(nmo_place_portal_entry_t), 0, NULL);
        if (result != NMO_OK) return result;
        result = nmo_array_init(&state->reference_ids, sizeof(nmo_object_id_t), 0, NULL);
        if (result != NMO_OK) return result;
    } while (0),
    ((void)0))

static nmo_status_t nmo_place_deserialize_internal(
    nmo_place_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_place_deserialize");
    }

    nmo_status_t result = nmo_beobject_deserialize(&out_state->base, chunk, NULL, context);
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
            nmo_array_clear(&out_state->portals);
            nmo_status_t result = nmo_array_reserve(&out_state->portals, count);
            if (result != NMO_OK) return result;

            nmo_place_portal_entry_t *portals = NULL;
            result = nmo_array_extend(&out_state->portals, count, (void **)&portals);
            if (result != NMO_OK) return result;

            for (uint32_t i = 0; i < (uint32_t)count; ++i) {
                (void)nmo_chunk_read_object_id(chunk, &portals[i].place_id);
                (void)nmo_chunk_read_object_id(chunk, &portals[i].portal_id);
            }
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACEREFERENCES) == NMO_OK) {
        nmo_object_id_t *ids = NULL;
        size_t count = 0;
        nmo_status_t result = nmo_chunk_read_object_id_array(chunk, &ids, &count, arena);
        if (result == NMO_OK && count > 0) {
            nmo_array_clear(&out_state->reference_ids);
            result = nmo_array_alloc(&out_state->reference_ids, sizeof(nmo_object_id_t), count, NULL);
            if (result == NMO_OK) {
                memcpy(out_state->reference_ids.data, ids, sizeof(nmo_object_id_t) * count);
            }
        }
    }

    NMO_RETURN_OK();
}

static const nmo_type_field_t nmo_place_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_place_state_t, base),
                    sizeof(nmo_beobject_state_t), CKPGUID_BEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_place_state_t, has_camera, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_place_state_t, camera_id),
    NMO_FIELD(nmo_place_state_t, has_level, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_place_state_t, level_id),
    NMO_FIELD_ARRAY(nmo_place_state_t, portals, NMO_GUID_STRUCT_CKPLACEPORTALENTRY),
    NMO_FIELD_REF_ARRAY(nmo_place_state_t, reference_ids)
};

static nmo_status_t nmo_place_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_place_state_t *s = src;
    nmo_place_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_array_clone(&s->portals, &d->portals, &s->portals.allocator));
    return nmo_array_clone(&s->reference_ids, &d->reference_ids, &s->reference_ids.allocator);
}

static nmo_status_t nmo_place_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_place_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->portals.data, s->portals.count, "portals");
    NMO_VALIDATE_COUNT(s->reference_ids.data, s->reference_ids.count, "reference_ids");
    NMO_RETURN_OK();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    place,
    nmo_place_state_t,
    nmo_place_serialize,
    nmo_place_deserialize,
    nmo_place_fields,
    CKPGUID_PLACE,
    "CKPlace",
    NMO_CID_PLACE,
    CKPGUID_3DENTITY
)

static nmo_status_t nmo_place_serialize_internal(
    const nmo_place_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_place_serialize");
    }

    nmo_status_t result = nmo_beobject_serialize(&in_state->base, out_chunk, NULL, context);
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

    if (in_state->portals.count > 0 && in_state->portals.data) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACEPORTALS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->portals.count);
        if (result != NMO_OK) return result;

        const nmo_place_portal_entry_t *portals = NMO_ARRAY_DATA(
            nmo_place_portal_entry_t, &in_state->portals);
        for (uint32_t i = 0; i < in_state->portals.count; ++i) {
            result = nmo_chunk_write_object_id(out_chunk, portals[i].place_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, portals[i].portal_id);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_place_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_place_state_t *out_state = (nmo_place_state_t *)instance;
    return nmo_place_deserialize_internal(out_state, chunk, context);
}

nmo_status_t nmo_place_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_place_state_t *in_state = (const nmo_place_state_t *)instance;
    return nmo_place_serialize_internal(in_state, out_chunk, context);
}

