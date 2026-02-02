/**
 * @file ckplace_schemas.c
 * @brief CKPlace schema implementation
 */

#include "object/nmo_ckplace_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_ckbeobject_schemas.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

#define CK_STATESAVE_PLACEPORTALS    0x00001000u
#define CK_STATESAVE_PLACECAMERA     0x00002000u
#define CK_STATESAVE_PLACEREFERENCES 0x00004000u
#define CK_STATESAVE_PLACELEVEL      0x00008000u

static nmo_result_t nmo_ckplace_deserialize_internal(
    nmo_ckplace_state_t *out_state,
    nmo_chunk_t *chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckplace_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_ckbeobject_deserialize(&out_state->base, chunk, NULL, context);
    if (result.code != NMO_OK) {
        return result;
    }

    const int file_mode = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    if (!file_mode) {
        return nmo_result_ok();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACECAMERA).code == NMO_OK) {
        out_state->has_camera = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->camera_id);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACELEVEL).code == NMO_OK) {
        out_state->has_level = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->level_id);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACEPORTALS).code == NMO_OK) {
        int32_t count = 0;
        if (nmo_chunk_read_int(chunk, &count).code == NMO_OK && count > 0) {
            out_state->portal_count = (uint32_t)count;
            out_state->portals = (nmo_ckplace_portal_entry_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ckplace_portal_entry_t) * out_state->portal_count,
                _Alignof(nmo_ckplace_portal_entry_t));
            if (!out_state->portals) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate portal array"));
            }

            for (uint32_t i = 0; i < out_state->portal_count; ++i) {
                (void)nmo_chunk_read_object_id(chunk, &out_state->portals[i].place_id);
                (void)nmo_chunk_read_object_id(chunk, &out_state->portals[i].portal_id);
            }
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_PLACEREFERENCES).code == NMO_OK) {
        nmo_object_id_t *ids = NULL;
        size_t count = 0;
        nmo_result_t result = nmo_chunk_read_object_id_array(chunk, &ids, &count, arena);
        if (result.code == NMO_OK && count > 0) {
            out_state->reference_count = (uint32_t)count;
            out_state->reference_ids = ids;
        }
    }

    return nmo_result_ok();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckplace,
    nmo_ckplace_state_t,
    nmo_ckplace_serialize,
    nmo_ckplace_deserialize,
    NMO_GUID_CKPLACE,
    "CKPlace",
    NMO_CID_PLACE,
    NMO_GUID_CK3DENTITY
)

static nmo_result_t nmo_ckplace_serialize_internal(
    const nmo_ckplace_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckplace_serialize"));
    }

    nmo_result_t result = nmo_ckbeobject_serialize(&in_state->base, out_chunk, NULL, context);
    if (result.code != NMO_OK) {
        return result;
    }

    if (in_state->has_camera) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACECAMERA);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->camera_id);
        if (result.code != NMO_OK) return result;
    }

    const int file_mode = (out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    if (file_mode && in_state->has_level) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACELEVEL);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->level_id);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->portal_count > 0 && in_state->portals) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_PLACEPORTALS);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->portal_count);
        if (result.code != NMO_OK) return result;

        for (uint32_t i = 0; i < in_state->portal_count; ++i) {
            result = nmo_chunk_write_object_id(out_chunk, in_state->portals[i].place_id);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->portals[i].portal_id);
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_ckplace_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckplace_state_t *out_state = (nmo_ckplace_state_t *)instance;
    return nmo_ckplace_deserialize_internal(out_state, chunk, context);
}

nmo_result_t nmo_ckplace_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckplace_state_t *in_state = (const nmo_ckplace_state_t *)instance;
    return nmo_ckplace_serialize_internal(in_state, out_chunk, context);
}
