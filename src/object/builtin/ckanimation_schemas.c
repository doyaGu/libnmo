/**
 * @file ckanimation_schemas.c
 * @brief CKAnimation, CKKeyedAnimation, CKObjectAnimation schema implementation
 */

#include "object/nmo_ckanimation_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

#define CK_STATESAVE_ANIMATIONDATA        0x00000010u
#define CK_STATESAVE_ANIMATIONLENGTH      0x00000040u
#define CK_STATESAVE_ANIMATIONBODYPARTS   0x00000080u
#define CK_STATESAVE_ANIMATIONCHARACTER   0x00000100u
#define CK_STATESAVE_ANIMATIONCURRENTSTEP 0x00000200u

#define CK_STATESAVE_KEYEDANIMANIMLIST    0x00001000u
#define CK_STATESAVE_KEYEDANIMMERGE       0x00100000u
#define CK_STATESAVE_KEYEDANIMSUBANIMS    0x00200000u

#define CK_STATESAVE_OBJANIMNEWDATA       0x00001000u
#define CK_STATESAVE_OBJANIMSHARED        0x02000000u
#define CK_STATESAVE_OBJANIMCONTROLLERS   0x04000000u

/* CKAnimation flag bits (subset used during legacy load) */
#define CKANIMATION_LINKTOFRAMERATE       0x00000001u
#define CKANIMATION_CANBEBREAK            0x00000004u
#define CKANIMATION_ALIGNORIENTATION      0x00000010u

/* =============================================================================
 * IDENTIFIER HELPERS
 * ============================================================================= */

static size_t nmo_ckanimation_identifier_remaining_dwords(nmo_chunk_t *chunk)
{
    if (!chunk || !chunk->parser_state) {
        return 0;
    }

    nmo_chunk_parser_state_t *state = (nmo_chunk_parser_state_t *)chunk->parser_state;
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);

    size_t next_pos = 0;
    if (state->prev_identifier_pos + 1 < chunk->data.count) {
        next_pos = data[state->prev_identifier_pos + 1];
    }
    if (next_pos == 0 || next_pos > chunk->data.count) {
        next_pos = chunk->data.count;
    }
    if (next_pos < state->current_pos) {
        return 0;
    }

    return next_pos - state->current_pos;
}

static nmo_result_t read_object_id_array(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_object_id_t **out_ids,
    uint32_t *out_count)
{
    size_t count = 0;
    nmo_result_t result = nmo_chunk_read_object_id_array(chunk, out_ids, &count, arena);
    if (result.code != NMO_OK) return result;
    *out_count = (uint32_t)count;
    return nmo_result_ok();
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckanimation,
    nmo_ckanimation_state_t,
    nmo_ckanimation_serialize,
    nmo_ckanimation_deserialize,
    NMO_GUID_CKANIMATION,
    "CKAnimation",
    NMO_CID_ANIMATION,
    NMO_GUID_CKSCENEOBJECT
)

NMO_DEFINE_OBJECT_SCHEMA(
    ckkeyedanimation,
    nmo_ckkeyedanimation_state_t,
    nmo_ckkeyedanimation_serialize,
    nmo_ckkeyedanimation_deserialize,
    NMO_GUID_CKKEYEDANIMATION,
    "CKKeyedAnimation",
    NMO_CID_KEYEDANIMATION,
    NMO_GUID_CKANIMATION
)

NMO_DEFINE_OBJECT_SCHEMA(
    ckobjectanimation,
    nmo_ckobjectanimation_state_t,
    nmo_ckobjectanimation_serialize,
    nmo_ckobjectanimation_deserialize,
    NMO_GUID_CKOBJECTANIMATION,
    "CKObjectAnimation",
    NMO_CID_OBJECTANIMATION,
    NMO_GUID_CKANIMATION
)

static nmo_result_t write_object_id_array(
    nmo_chunk_t *chunk,
    const nmo_object_id_t *ids,
    uint32_t count)
{
    return nmo_chunk_write_object_id_array(chunk, ids, count);
}

static void read_raw_tail(nmo_chunk_t *chunk, nmo_arena_t *arena,
                          void **out_data, size_t *out_size)
{
    size_t pos = nmo_chunk_get_position(chunk);
    size_t total_bytes = nmo_chunk_get_data_size(chunk);
    size_t total_dwords = total_bytes / 4;

    if (pos >= total_dwords) {
        *out_data = NULL;
        *out_size = 0;
        return;
    }

    size_t remaining_bytes = (total_dwords - pos) * 4;
    void *data = nmo_arena_alloc(arena, remaining_bytes, 1);
    if (!data) {
        *out_data = NULL;
        *out_size = 0;
        return;
    }

    size_t bytes_read = nmo_chunk_read_and_fill_buffer(chunk, data, remaining_bytes);
    if (bytes_read != remaining_bytes) {
        *out_data = NULL;
        *out_size = 0;
        return;
    }

    *out_data = data;
    *out_size = remaining_bytes;
}

static nmo_result_t nmo_ckanimation_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_ckanimation_state_t *out_state)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckanimation_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    {
        nmo_result_t result = nmo_cksceneobject_deserialize(&out_state->base, chunk, NULL, context);
        if (result.code != NMO_OK) return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ANIMATIONDATA).code == NMO_OK) {
        out_state->has_data = 1;

        size_t remaining_dwords = nmo_ckanimation_identifier_remaining_dwords(chunk);
        if (remaining_dwords == 3) {
            int32_t can_interrupt = 0;
            int32_t linked_to_framerate = 0;
            float frame_rate = 0.0f;

            (void)nmo_chunk_read_int(chunk, &can_interrupt);
            (void)nmo_chunk_read_int(chunk, &linked_to_framerate);
            (void)nmo_chunk_read_float(chunk, &frame_rate);

            out_state->flags = 0;
            if (linked_to_framerate) {
                out_state->flags |= CKANIMATION_LINKTOFRAMERATE;
            }
            if (can_interrupt) {
                out_state->flags |= CKANIMATION_CANBEBREAK;
            }
            out_state->frame_rate = frame_rate;
        } else if (remaining_dwords >= 2) {
            (void)nmo_chunk_read_dword(chunk, &out_state->flags);
            (void)nmo_chunk_read_float(chunk, &out_state->frame_rate);

            for (size_t i = 2; i < remaining_dwords; ++i) {
                uint32_t tmp = 0;
                (void)nmo_chunk_read_dword(chunk, &tmp);
            }
        } else if (remaining_dwords == 1) {
            (void)nmo_chunk_read_dword(chunk, &out_state->flags);
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ANIMATIONLENGTH).code == NMO_OK) {
        out_state->has_length = 1;
        (void)nmo_chunk_read_float(chunk, &out_state->length);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ANIMATIONBODYPARTS).code == NMO_OK) {
        out_state->has_root_entity = 1;
        /* Legacy list of body parts (ignored) */
        int32_t count = 0;
        (void)nmo_chunk_read_int(chunk, &count);
        for (int32_t i = 0; i < count; ++i) {
            nmo_object_id_t tmp = 0;
            (void)nmo_chunk_read_object_id(chunk, &tmp);
        }
        (void)nmo_chunk_read_object_id(chunk, &out_state->root_entity_id);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ANIMATIONCHARACTER).code == NMO_OK) {
        out_state->has_character = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->character_id);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ANIMATIONCURRENTSTEP).code == NMO_OK) {
        out_state->has_current_step = 1;
        (void)nmo_chunk_read_float(chunk, &out_state->current_step);
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckanimation_serialize_internal(
    const nmo_ckanimation_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckanimation_serialize"));
    }

    {
        nmo_result_t result = nmo_cksceneobject_serialize(&in_state->base, out_chunk, NULL, context);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->has_data) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONDATA);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->flags);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->frame_rate);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->has_length) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONLENGTH);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->length);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->has_root_entity) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONBODYPARTS);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, 0);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->root_entity_id);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->has_character) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONCHARACTER);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->character_id);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->has_current_step) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONCURRENTSTEP);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->current_step);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckkeyedanimation_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_ckkeyedanimation_state_t *out_state)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckkeyedanimation_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_ckanimation_deserialize_internal(chunk, context, &out_state->base);
    if (result.code != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_KEYEDANIMANIMLIST).code == NMO_OK) {
        (void)read_object_id_array(chunk, arena,
                                   &out_state->animation_ids,
                                   &out_state->animation_count);
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_KEYEDANIMMERGE).code == NMO_OK) {
        out_state->has_merge = 1;
        (void)nmo_chunk_read_int(chunk, &out_state->merged);
        (void)nmo_chunk_read_float(chunk, &out_state->merge_factor);
    }

    const int file_mode = (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    if (!file_mode && nmo_chunk_seek_identifier(chunk, CK_STATESAVE_KEYEDANIMSUBANIMS).code == NMO_OK) {
        uint32_t count = 0;
        (void)nmo_chunk_read_dword(chunk, &count);
        if (count > 0) {
            out_state->subanim_count = count;
            out_state->subanims = (nmo_ckkeyedanimation_subanim_t *)nmo_arena_alloc(
                arena, sizeof(nmo_ckkeyedanimation_subanim_t) * count,
                _Alignof(nmo_ckkeyedanimation_subanim_t));
            if (!out_state->subanims) {
                return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                    NMO_SEVERITY_ERROR, "Failed to allocate subanim array"));
            }

            for (uint32_t i = 0; i < count; ++i) {
                (void)nmo_chunk_read_object_id(chunk, &out_state->subanims[i].object_id);
                (void)nmo_chunk_read_sub_chunk(chunk, &out_state->subanims[i].chunk);
            }
        }
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckkeyedanimation_serialize_internal(
    const nmo_ckkeyedanimation_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckkeyedanimation_serialize"));
    }

    nmo_result_t result = nmo_ckanimation_serialize_internal(&in_state->base, out_chunk, context);
    if (result.code != NMO_OK) return result;

    if (in_state->animation_count > 0 && in_state->animation_ids) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KEYEDANIMANIMLIST);
        if (result.code != NMO_OK) return result;
        result = write_object_id_array(out_chunk, in_state->animation_ids, in_state->animation_count);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->has_merge) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KEYEDANIMMERGE);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->merged);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
        if (result.code != NMO_OK) return result;
    }

    const int file_mode = (out_chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    if (!file_mode && in_state->subanim_count > 0 && in_state->subanims) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KEYEDANIMSUBANIMS);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->subanim_count);
        if (result.code != NMO_OK) return result;
        for (uint32_t i = 0; i < in_state->subanim_count; ++i) {
            nmo_chunk_t *sub = in_state->subanims[i].chunk;
            if (!sub) {
                sub = nmo_chunk_create(arena);
            }
            result = nmo_chunk_write_object_id(out_chunk, in_state->subanims[i].object_id);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_sub_chunk(out_chunk, sub);
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckobjectanimation_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_ckobjectanimation_state_t *out_state)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckobjectanimation_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    {
        nmo_result_t result = nmo_cksceneobject_deserialize(&out_state->base, chunk, NULL, context);
        if (result.code != NMO_OK) return result;
    }

    uint32_t data_version = nmo_chunk_get_data_version(chunk);

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMSHARED).code == NMO_OK) {
        out_state->format = NMO_OBJANIM_FORMAT_SHARED;
        out_state->has_shared_anim = 1;
        (void)nmo_chunk_read_object_id(chunk, &out_state->shared_anim_id);
        out_state->has_root_pos = 1;
        (void)nmo_chunk_read_vector3(chunk, &out_state->root_pos);
        for (int i = 0; i < 4; ++i) {
            float tmp = 0.0f;
            (void)nmo_chunk_read_float(chunk, &tmp);
        }
        (void)nmo_chunk_read_dword(chunk, &out_state->flags);
        (void)nmo_chunk_read_object_id(chunk, &out_state->entity_id);
        if (out_state->flags & 0x80u) {
            out_state->has_merge = 1;
            (void)nmo_chunk_read_float(chunk, &out_state->merge_factor);
            (void)nmo_chunk_read_object_id(chunk, &out_state->anim1_id);
            (void)nmo_chunk_read_object_id(chunk, &out_state->anim2_id);
        }
        read_raw_tail(chunk, arena, &out_state->raw_tail, &out_state->raw_tail_size);
        return nmo_result_ok();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMCONTROLLERS).code == NMO_OK) {
        out_state->format = NMO_OBJANIM_FORMAT_CONTROLLERS;
        out_state->has_root_pos = 1;
        (void)nmo_chunk_read_vector3(chunk, &out_state->root_pos);
        for (int i = 0; i < 4; ++i) {
            float tmp = 0.0f;
            (void)nmo_chunk_read_float(chunk, &tmp);
        }
        (void)nmo_chunk_read_dword(chunk, &out_state->flags);
        (void)nmo_chunk_read_object_id(chunk, &out_state->entity_id);
        out_state->has_length = 1;
        (void)nmo_chunk_read_float(chunk, &out_state->length);
        if (out_state->flags & 0x80u) {
            out_state->has_merge = 1;
            (void)nmo_chunk_read_float(chunk, &out_state->merge_factor);
            (void)nmo_chunk_read_object_id(chunk, &out_state->anim1_id);
            (void)nmo_chunk_read_object_id(chunk, &out_state->anim2_id);
        }
        read_raw_tail(chunk, arena, &out_state->raw_tail, &out_state->raw_tail_size);
        return nmo_result_ok();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMNEWDATA).code == NMO_OK) {
        out_state->format = NMO_OBJANIM_FORMAT_NEWDATA;
        out_state->has_root_pos = 1;
        (void)nmo_chunk_read_vector3(chunk, &out_state->root_pos);
        for (int i = 0; i < 4; ++i) {
            float tmp = 0.0f;
            (void)nmo_chunk_read_float(chunk, &tmp);
        }
        out_state->has_morph_counts = 1;
        (void)nmo_chunk_read_int(chunk, &out_state->morph_vertex_count);
        (void)nmo_chunk_read_int(chunk, &out_state->morph_key_count);
        (void)nmo_chunk_read_dword(chunk, &out_state->flags);
        (void)nmo_chunk_read_object_id(chunk, &out_state->entity_id);
        out_state->has_length = 1;
        (void)nmo_chunk_read_float(chunk, &out_state->length);
        if (out_state->flags & 0x80u) {
            out_state->has_merge = 1;
            (void)nmo_chunk_read_float(chunk, &out_state->merge_factor);
            (void)nmo_chunk_read_object_id(chunk, &out_state->anim1_id);
            (void)nmo_chunk_read_object_id(chunk, &out_state->anim2_id);
        }
        read_raw_tail(chunk, arena, &out_state->raw_tail, &out_state->raw_tail_size);
        return nmo_result_ok();
    }

    if (out_state->format == NMO_OBJANIM_FORMAT_NONE) {
        if (data_version < 1) {
            out_state->format = NMO_OBJANIM_FORMAT_LEGACY;
        }
        read_raw_tail(chunk, arena, &out_state->raw_tail, &out_state->raw_tail_size);
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckobjectanimation_serialize_internal(
    const nmo_ckobjectanimation_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckobjectanimation_serialize"));
    }

    {
        nmo_result_t result = nmo_cksceneobject_serialize(&in_state->base, out_chunk, NULL, context);
        if (result.code != NMO_OK) return result;
    }

    switch (in_state->format) {
    case NMO_OBJANIM_FORMAT_SHARED: {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMSHARED);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->shared_anim_id);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->root_pos);
        if (result.code != NMO_OK) return result;
        for (int i = 0; i < 4; ++i) {
            result = nmo_chunk_write_float(out_chunk, 0.0f);
            if (result.code != NMO_OK) return result;
        }
        result = nmo_chunk_write_dword(out_chunk, in_state->flags);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->entity_id);
        if (result.code != NMO_OK) return result;
        if (in_state->has_merge && (in_state->flags & 0x80u)) {
            result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->anim1_id);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->anim2_id);
            if (result.code != NMO_OK) return result;
        }
        break;
    }
    case NMO_OBJANIM_FORMAT_CONTROLLERS: {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMCONTROLLERS);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->root_pos);
        if (result.code != NMO_OK) return result;
        for (int i = 0; i < 4; ++i) {
            result = nmo_chunk_write_float(out_chunk, 0.0f);
            if (result.code != NMO_OK) return result;
        }
        result = nmo_chunk_write_dword(out_chunk, in_state->flags);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->entity_id);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->has_length ? in_state->length : 0.0f);
        if (result.code != NMO_OK) return result;
        if (in_state->has_merge && (in_state->flags & 0x80u)) {
            result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->anim1_id);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->anim2_id);
            if (result.code != NMO_OK) return result;
        }
        break;
    }
    case NMO_OBJANIM_FORMAT_NEWDATA: {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMNEWDATA);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->root_pos);
        if (result.code != NMO_OK) return result;
        for (int i = 0; i < 4; ++i) {
            result = nmo_chunk_write_float(out_chunk, 0.0f);
            if (result.code != NMO_OK) return result;
        }
        result = nmo_chunk_write_int(out_chunk, in_state->has_morph_counts ? in_state->morph_vertex_count : 0);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->has_morph_counts ? in_state->morph_key_count : 0);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->flags);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->entity_id);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->has_length ? in_state->length : 0.0f);
        if (result.code != NMO_OK) return result;
        if (in_state->has_merge && (in_state->flags & 0x80u)) {
            result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->anim1_id);
            if (result.code != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->anim2_id);
            if (result.code != NMO_OK) return result;
        }
        break;
    }
    default:
        break;
    }

    if (in_state->raw_tail && in_state->raw_tail_size > 0) {
        nmo_result_t result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                             in_state->raw_tail,
                                                             in_state->raw_tail_size);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

nmo_result_t nmo_ckanimation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckanimation_state_t *out_state = (nmo_ckanimation_state_t *)instance;
    return nmo_ckanimation_deserialize_internal(chunk, context, out_state);
}

nmo_result_t nmo_ckanimation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckanimation_state_t *in_state = (const nmo_ckanimation_state_t *)instance;
    return nmo_ckanimation_serialize_internal(in_state, out_chunk, context);
}

nmo_result_t nmo_ckkeyedanimation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckkeyedanimation_state_t *out_state = (nmo_ckkeyedanimation_state_t *)instance;
    return nmo_ckkeyedanimation_deserialize_internal(chunk, context, out_state);
}

nmo_result_t nmo_ckkeyedanimation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckkeyedanimation_state_t *in_state =
        (const nmo_ckkeyedanimation_state_t *)instance;
    return nmo_ckkeyedanimation_serialize_internal(in_state, out_chunk, context);
}

nmo_result_t nmo_ckobjectanimation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckobjectanimation_state_t *out_state = (nmo_ckobjectanimation_state_t *)instance;
    return nmo_ckobjectanimation_deserialize_internal(chunk, context, out_state);
}

nmo_result_t nmo_ckobjectanimation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckobjectanimation_state_t *in_state =
        (const nmo_ckobjectanimation_state_t *)instance;
    return nmo_ckobjectanimation_serialize_internal(in_state, out_chunk, context);
}
