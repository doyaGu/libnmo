/**
 * @file ckanimation_schemas.c
 * @brief CKAnimation, CKKeyedAnimation, CKObjectAnimation schema implementation
 */

#include "object/builtin/nmo_animation_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_param_guids.h"
#include "object/nmo_object_enum_guids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_struct_guids.h"
#include <string.h>
#include <stddef.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    animation,
    nmo_animation_state_t,
    do {
        state->flags = CKANIMATION_LINKTOFRAMERATE | CKANIMATION_CANBEBREAK;
        state->frame_rate = 30.0f;
        state->length = 100.0f;
        state->current_step = 0.0f;
    } while (0),
    ((void)0))

NMO_DEFINE_OBJECT_LIFECYCLE(
    keyedanimation,
    nmo_keyedanimation_state_t,
    do {
        state->base.flags = CKANIMATION_LINKTOFRAMERATE | CKANIMATION_CANBEBREAK;
        state->base.frame_rate = 30.0f;
        state->base.length = 100.0f;
        state->base.current_step = 0.0f;
        state->merge_factor = 0.5f;
    } while (0),
    ((void)0))

NMO_DEFINE_OBJECT_LIFECYCLE(
    objectanimation,
    nmo_objectanimation_state_t,
    do {
        state->format = CKOBJANIM_FORMAT_NONE;
        state->merge_factor = 0.5f;
    } while (0),
    ((void)0))

/* CKAnimation flag bits (subset used during legacy load) */
#define CKANIMATION_LINKTOFRAMERATE       0x00000001u
#define CKANIMATION_CANBEBREAK            0x00000004u
#define CKANIMATION_ALIGNORIENTATION      0x00000010u

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_animation_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_animation_state_t, base),
                    sizeof(nmo_sceneobject_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_animation_state_t, has_data, CKPGUID_UINT8),
    NMO_FIELD(nmo_animation_state_t, flags, CKPGUID_UINT32),
    NMO_FIELD(nmo_animation_state_t, frame_rate, CKPGUID_FLOAT),
    NMO_FIELD(nmo_animation_state_t, has_length, CKPGUID_UINT8),
    NMO_FIELD(nmo_animation_state_t, length, CKPGUID_FLOAT),
    NMO_FIELD(nmo_animation_state_t, has_root_entity, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_animation_state_t, root_entity_id),
    NMO_FIELD(nmo_animation_state_t, has_character, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_animation_state_t, character_id),
    NMO_FIELD(nmo_animation_state_t, has_current_step, CKPGUID_UINT8),
    NMO_FIELD(nmo_animation_state_t, current_step, CKPGUID_FLOAT)
};

static const nmo_type_field_t nmo_keyedanimation_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_keyedanimation_state_t, base),
                    sizeof(nmo_animation_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_keyedanimation_state_t, animation_count, CKPGUID_UINT32),
    NMO_FIELD_REF_ARRAY(nmo_keyedanimation_state_t, animation_ids),
    NMO_FIELD(nmo_keyedanimation_state_t, has_merge, CKPGUID_UINT8),
    NMO_FIELD(nmo_keyedanimation_state_t, merged, CKPGUID_INT),
    NMO_FIELD(nmo_keyedanimation_state_t, merge_factor, CKPGUID_FLOAT),
    NMO_FIELD(nmo_keyedanimation_state_t, subanim_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_keyedanimation_state_t, subanims, NMO_GUID_STRUCT_CKKEYEDANIMATIONSUBANIM)
};

static const nmo_type_field_t nmo_objectanimation_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_objectanimation_state_t, base),
                    sizeof(nmo_sceneobject_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_objectanimation_state_t, format, NMO_GUID_ENUM_CK_OBJECTANIMATION_FORMAT),
    NMO_FIELD(nmo_objectanimation_state_t, root_pos, CKPGUID_VECTOR),
    NMO_FIELD(nmo_objectanimation_state_t, has_root_pos, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, flags, CKPGUID_UINT32),
    NMO_FIELD_REF(nmo_objectanimation_state_t, entity_id),
    NMO_FIELD(nmo_objectanimation_state_t, has_length, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, length, CKPGUID_FLOAT),
    NMO_FIELD(nmo_objectanimation_state_t, has_merge, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, merge_factor, CKPGUID_FLOAT),
    NMO_FIELD_REF(nmo_objectanimation_state_t, anim1_id),
    NMO_FIELD_REF(nmo_objectanimation_state_t, anim2_id),
    NMO_FIELD(nmo_objectanimation_state_t, has_shared_anim, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_objectanimation_state_t, shared_anim_id),
    NMO_FIELD(nmo_objectanimation_state_t, has_morph_counts, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, morph_vertex_count, CKPGUID_INT),
    NMO_FIELD(nmo_objectanimation_state_t, morph_key_count, CKPGUID_INT),
    NMO_FIELD_ARRAY_NAMED("raw_tail", offsetof(nmo_objectanimation_state_t, raw_tail),
                          sizeof(void *), CKPGUID_UINT8, NMO_FIELD_OPTIONAL, 0),
    NMO_FIELD(nmo_objectanimation_state_t, raw_tail_size, CKPGUID_UINT64)
};

/* =============================================================================
 * IDENTIFIER HELPERS
 * ============================================================================= */

static size_t nmo_animation_identifier_remaining_dwords(nmo_chunk_t *chunk)
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

static int nmo_animation_is_file_mode_deser(const nmo_chunk_t *chunk, void *context)
{
    const nmo_deserialize_context_t *deser_ctx = nmo_deserialize_context_get(context);
    return (chunk && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE)) ||
        (deser_ctx != NULL && (deser_ctx->flags & NMO_DESER_FLAG_FILE_MODE) != 0);
}

static int nmo_animation_is_file_mode_ser(const nmo_chunk_t *chunk, void *context)
{
    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    return (chunk && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE)) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
}

static nmo_status_t read_object_id_array(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_object_id_t **out_ids,
    uint32_t *out_count)
{
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_id_array(chunk, out_ids, &count, arena);
    if (result != NMO_OK) return result;
    *out_count = (uint32_t)count;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_animation_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    return nmo_object_default_copy(src, dst, type, arena);
}

static nmo_status_t nmo_animation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_keyedanimation_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_keyedanimation_state_t *s = src;
    nmo_keyedanimation_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->animation_ids,
                                              s->animation_ids, sizeof(nmo_object_id_t), s->animation_count));
    if (s->subanim_count > 0) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->subanims,
                                                  s->subanims, sizeof(nmo_keyedanimation_subanim_t),
                                                  s->subanim_count));
        for (uint32_t i = 0; i < s->subanim_count; ++i) {
            nmo_chunk_t *clone = NULL;
            NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &clone, s->subanims[i].chunk));
            d->subanims[i].chunk = clone;
        }
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_keyedanimation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_keyedanimation_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->animation_ids, s->animation_count, "animation_ids");
    NMO_VALIDATE_COUNT(s->subanims, s->subanim_count, "subanims");
    NMO_RETURN_OK();
}

static nmo_status_t nmo_objectanimation_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_objectanimation_state_t *s = src;
    nmo_objectanimation_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    return nmo_object_copy_bytes(arena, (void **)&d->raw_tail,
                                 s->raw_tail, s->raw_tail_size);
}

static nmo_status_t nmo_objectanimation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_objectanimation_state_t *s = instance;
    NMO_VALIDATE_BYTES(s->raw_tail, s->raw_tail_size, "raw_tail");
    NMO_RETURN_OK();
}

nmo_status_t nmo_animation_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository)
{
    (void)arena;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_animation_finish_loading");
    }

    nmo_animation_state_t *state = (nmo_animation_state_t *)instance;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)repository;

    NMO_RETURN_IF_ERROR(nmo_sceneobject_finish_loading(&state->base, arena, repository));

    if (state->frame_rate <= 0.0f) {
        state->frame_rate = 30.0f;
    }
    if (state->length < 0.0f) {
        state->length = 0.0f;
    }
    if (state->current_step < 0.0f) {
        state->current_step = 0.0f;
    }

    if (repo) {
        if (state->has_root_entity && state->root_entity_id != 0 &&
            nmo_object_repository_find_by_id(repo, state->root_entity_id) == NULL) {
            state->root_entity_id = 0;
            state->has_root_entity = 0;
        }
        if (state->has_character && state->character_id != 0 &&
            nmo_object_repository_find_by_id(repo, state->character_id) == NULL) {
            state->character_id = 0;
            state->has_character = 0;
        }
    } else {
        if (state->root_entity_id == 0) {
            state->has_root_entity = 0;
        }
        if (state->character_id == 0) {
            state->has_character = 0;
        }
    }

    if (!state->has_length) {
        state->length = 0.0f;
    }
    if (!state->has_current_step) {
        state->current_step = 0.0f;
    }

    return nmo_animation_validate(state, NULL, NULL);
}

nmo_status_t nmo_keyedanimation_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository)
{
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_keyedanimation_finish_loading");
    }

    nmo_keyedanimation_state_t *state = (nmo_keyedanimation_state_t *)instance;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)repository;

    NMO_RETURN_IF_ERROR(nmo_animation_finish_loading(&state->base, arena, repository));

    if (state->animation_count > 0 && state->animation_ids == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "KeyedAnimation animation_ids missing");
    }
    if (state->subanim_count > 0 && state->subanims == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "KeyedAnimation subanims missing");
    }

    if (state->animation_count > 0) {
        nmo_object_id_t *ids = state->animation_ids;
        uint32_t kept = 0;
        for (uint32_t i = 0; i < state->animation_count; ++i) {
            nmo_object_id_t id = ids[i];
            if (id == 0) {
                continue;
            }
            if (repo && nmo_object_repository_find_by_id(repo, id) == NULL) {
                continue;
            }
            bool seen = false;
            for (uint32_t j = 0; j < kept; ++j) {
                if (ids[j] == id) {
                    seen = true;
                    break;
                }
            }
            if (seen) {
                continue;
            }
            ids[kept++] = id;
        }
        state->animation_count = kept;
    }

    if (state->subanim_count > 0) {
        uint32_t kept = 0;
        for (uint32_t i = 0; i < state->subanim_count; ++i) {
            nmo_keyedanimation_subanim_t sub = state->subanims[i];
            if (sub.object_id == 0) {
                continue;
            }
            if (repo && nmo_object_repository_find_by_id(repo, sub.object_id) == NULL) {
                continue;
            }
            state->subanims[kept++] = sub;
        }
        state->subanim_count = kept;
    }

    return nmo_keyedanimation_validate(state, NULL, NULL);
}

nmo_status_t nmo_objectanimation_finish_loading(
    void *instance,
    nmo_arena_t *arena,
    void *repository)
{
    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_objectanimation_finish_loading");
    }

    nmo_objectanimation_state_t *state = (nmo_objectanimation_state_t *)instance;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)repository;

    NMO_RETURN_IF_ERROR(nmo_sceneobject_finish_loading(&state->base, arena, repository));

    if (state->format < CKOBJANIM_FORMAT_NONE || state->format > CKOBJANIM_FORMAT_NEWDATA) {
        state->format = CKOBJANIM_FORMAT_NONE;
    }

    if (state->has_length && state->length < 0.0f) {
        state->length = 0.0f;
    }
    if (state->has_merge && state->merge_factor < 0.0f) {
        state->merge_factor = 0.0f;
    }

    if (state->has_morph_counts) {
        if (state->morph_vertex_count < 0) {
            state->morph_vertex_count = 0;
        }
        if (state->morph_key_count < 0) {
            state->morph_key_count = 0;
        }
    } else {
        state->morph_vertex_count = 0;
        state->morph_key_count = 0;
    }

    if (repo) {
        if (state->entity_id != 0 &&
            nmo_object_repository_find_by_id(repo, state->entity_id) == NULL) {
            state->entity_id = 0;
        }
        if (state->anim1_id != 0 &&
            nmo_object_repository_find_by_id(repo, state->anim1_id) == NULL) {
            state->anim1_id = 0;
        }
        if (state->anim2_id != 0 &&
            nmo_object_repository_find_by_id(repo, state->anim2_id) == NULL) {
            state->anim2_id = 0;
        }
        if (state->has_shared_anim && state->shared_anim_id != 0 &&
            nmo_object_repository_find_by_id(repo, state->shared_anim_id) == NULL) {
            state->shared_anim_id = 0;
            state->has_shared_anim = 0;
        }
    } else {
        if (state->entity_id == 0) {
            state->flags &= ~0x80u;
        }
    }

    if ((state->flags & 0x80u) == 0) {
        state->has_merge = 0;
        state->anim1_id = 0;
        state->anim2_id = 0;
    } else if (state->anim1_id == 0 || state->anim2_id == 0) {
        state->has_merge = 0;
        state->flags &= ~0x80u;
    }

    return nmo_objectanimation_validate(state, NULL, NULL);
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_EX_FIELDS_CUSTOM(
    animation,
    nmo_animation_state_t,
    nmo_animation_serialize,
    nmo_animation_deserialize,
    nmo_animation_finish_loading,
    nmo_animation_fields,
    CKPGUID_ANIMATION,
    "CKAnimation",
    NMO_CID_ANIMATION,
    CKPGUID_SCENEOBJECT
)

NMO_DEFINE_OBJECT_SCHEMA_EX_FIELDS_CUSTOM(
    keyedanimation,
    nmo_keyedanimation_state_t,
    nmo_keyedanimation_serialize,
    nmo_keyedanimation_deserialize,
    nmo_keyedanimation_finish_loading,
    nmo_keyedanimation_fields,
    CKPGUID_KEYEDANIMATION,
    "CKKeyedAnimation",
    NMO_CID_KEYEDANIMATION,
    CKPGUID_ANIMATION
)

NMO_DEFINE_OBJECT_SCHEMA_EX_FIELDS_CUSTOM(
    objectanimation,
    nmo_objectanimation_state_t,
    nmo_objectanimation_serialize,
    nmo_objectanimation_deserialize,
    nmo_objectanimation_finish_loading,
    nmo_objectanimation_fields,
    CKPGUID_OBJECTANIMATION,
    "CKObjectAnimation",
    NMO_CID_OBJECTANIMATION,
    CKPGUID_ANIMATION
)

static nmo_status_t write_object_id_array(
    nmo_chunk_t *chunk,
    const nmo_object_id_t *ids,
    uint32_t count)
{
    return nmo_chunk_write_object_id_array(chunk, ids, count);
}

static nmo_status_t read_raw_tail(nmo_chunk_t *chunk, nmo_arena_t *arena,
                                  void **out_data, size_t *out_size)
{
    size_t pos = nmo_chunk_get_position(chunk);
    size_t total_bytes = nmo_chunk_get_data_size(chunk);
    size_t total_dwords = total_bytes / 4;

    if (pos >= total_dwords) {
        *out_data = NULL;
        *out_size = 0;
        NMO_RETURN_OK();
    }

    size_t remaining_bytes = (total_dwords - pos) * 4;
    void *data = nmo_arena_alloc(arena, remaining_bytes, 1);
    if (!data) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate raw tail buffer");
    }

    size_t bytes_read = nmo_chunk_read_and_fill_buffer_nosize(chunk, data, remaining_bytes);
    if (bytes_read != remaining_bytes) {
        NMO_RETURN_ERROR(NMO_ERR_EOF, NMO_SEVERITY_ERROR, "Failed to read raw tail buffer");
    }

    *out_data = data;
    *out_size = remaining_bytes;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_animation_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_animation_state_t *out_state)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_animation_deserialize");
    }

    {
        nmo_status_t result = nmo_sceneobject_deserialize(&out_state->base, chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    out_state->has_data = 0;
    out_state->flags = CKANIMATION_LINKTOFRAMERATE | CKANIMATION_CANBEBREAK;
    out_state->frame_rate = 30.0f;
    out_state->has_length = 0;
    out_state->length = 100.0f;
    out_state->has_root_entity = 0;
    out_state->root_entity_id = 0;
    out_state->has_character = 0;
    out_state->character_id = 0;
    out_state->has_current_step = 0;
    out_state->current_step = 0.0f;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ANIMATIONDATA) == NMO_OK) {
        out_state->has_data = 1;

        size_t remaining_dwords = nmo_animation_identifier_remaining_dwords(chunk);
        if (remaining_dwords == 3) {
            int32_t can_interrupt = 0;
            int32_t linked_to_framerate = 0;
            float frame_rate = 0.0f;

            nmo_status_t result = nmo_chunk_read_int(chunk, &can_interrupt);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_int(chunk, &linked_to_framerate);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_float(chunk, &frame_rate);
            if (result != NMO_OK) return result;

            out_state->flags = 0;
            if (linked_to_framerate) {
                out_state->flags |= CKANIMATION_LINKTOFRAMERATE;
            }
            if (can_interrupt) {
                out_state->flags |= CKANIMATION_CANBEBREAK;
            }
            out_state->frame_rate = frame_rate;
        } else if (remaining_dwords >= 2) {
            nmo_status_t result = nmo_chunk_read_dword(chunk, &out_state->flags);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_float(chunk, &out_state->frame_rate);
            if (result != NMO_OK) return result;

            for (size_t i = 2; i < remaining_dwords; ++i) {
                uint32_t tmp = 0;
                result = nmo_chunk_read_dword(chunk, &tmp);
                if (result != NMO_OK) return result;
            }
        } else if (remaining_dwords == 1) {
            nmo_status_t result = nmo_chunk_read_dword(chunk, &out_state->flags);
            if (result != NMO_OK) return result;
        }
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ANIMATIONLENGTH) == NMO_OK) {
        out_state->has_length = 1;
        nmo_status_t result = nmo_chunk_read_float(chunk, &out_state->length);
        if (result != NMO_OK) return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ANIMATIONBODYPARTS) == NMO_OK) {
        out_state->has_root_entity = 1;
        /* Legacy list of body parts (ignored) */
        int32_t count = 0;
        nmo_status_t result = nmo_chunk_read_int(chunk, &count);
        if (result != NMO_OK) return result;
        if (count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid body part count");
        }
        for (int32_t i = 0; i < count; ++i) {
            nmo_object_id_t tmp = 0;
            result = nmo_chunk_read_object_id(chunk, &tmp);
            if (result != NMO_OK) return result;
        }
        result = nmo_chunk_read_object_id(chunk, &out_state->root_entity_id);
        if (result != NMO_OK) return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ANIMATIONCHARACTER) == NMO_OK) {
        out_state->has_character = 1;
        nmo_status_t result = nmo_chunk_read_object_id(chunk, &out_state->character_id);
        if (result != NMO_OK) return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_ANIMATIONCURRENTSTEP) == NMO_OK) {
        out_state->has_current_step = 1;
        nmo_status_t result = nmo_chunk_read_float(chunk, &out_state->current_step);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_animation_serialize_internal(
    const nmo_animation_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_animation_serialize");
    }

    {
        nmo_status_t result = nmo_sceneobject_serialize(&in_state->base, out_chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    const bool is_file = nmo_animation_is_file_mode_ser(out_chunk, context);
    if (!is_file && save_flags == 0) {
        NMO_RETURN_OK();
    }

    if (is_file || (save_flags & CK_STATESAVE_ANIMATIONDATA) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONDATA);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->flags);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->frame_rate);
        if (result != NMO_OK) return result;
    }

    if (is_file || (save_flags & CK_STATESAVE_ANIMATIONLENGTH) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONLENGTH);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->length);
        if (result != NMO_OK) return result;
    }

    if (is_file || (save_flags & CK_STATESAVE_ANIMATIONBODYPARTS) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONBODYPARTS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, 0);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->root_entity_id);
        if (result != NMO_OK) return result;
    }

    if (is_file || (save_flags & CK_STATESAVE_ANIMATIONCHARACTER) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONCHARACTER);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->character_id);
        if (result != NMO_OK) return result;
    }

    if (is_file || (save_flags & CK_STATESAVE_ANIMATIONCURRENTSTEP) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONCURRENTSTEP);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->current_step);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_keyedanimation_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_keyedanimation_state_t *out_state)
{
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_keyedanimation_deserialize");
    }

    NMO_RETURN_IF_ERROR(nmo_keyedanimation_create(out_state, NULL, context));

    nmo_status_t result = nmo_animation_deserialize_internal(chunk, context, &out_state->base);
    if (result != NMO_OK) return result;

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_KEYEDANIMANIMLIST) == NMO_OK) {
        result = read_object_id_array(chunk, arena,
                                      &out_state->animation_ids,
                                      &out_state->animation_count);
        if (result != NMO_OK) return result;
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_KEYEDANIMMERGE) == NMO_OK) {
        out_state->has_merge = 1;
        result = nmo_chunk_read_int(chunk, &out_state->merged);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_float(chunk, &out_state->merge_factor);
        if (result != NMO_OK) return result;
    }

    const bool is_file = nmo_animation_is_file_mode_deser(chunk, context);
    if (!is_file && nmo_chunk_seek_identifier(chunk, CK_STATESAVE_KEYEDANIMSUBANIMS) == NMO_OK) {
        uint32_t count = 0;
        result = nmo_chunk_read_dword(chunk, &count);
        if (result != NMO_OK) return result;
        if (count > 10000u) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "Subanim count exceeds maximum");
        }
        if (count > 0) {
            out_state->subanim_count = count;
            out_state->subanims = (nmo_keyedanimation_subanim_t *)nmo_arena_alloc(
                arena, sizeof(nmo_keyedanimation_subanim_t) * count,
                _Alignof(nmo_keyedanimation_subanim_t));
            if (!out_state->subanims) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate subanim array");
            }

            for (uint32_t i = 0; i < count; ++i) {
                result = nmo_chunk_read_object_id(chunk, &out_state->subanims[i].object_id);
                if (result != NMO_OK) return result;
                result = nmo_chunk_read_sub_chunk(chunk, &out_state->subanims[i].chunk);
                if (result != NMO_OK) return result;
            }
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_keyedanimation_serialize_internal(
    const nmo_keyedanimation_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_keyedanimation_serialize");
    }

    nmo_status_t result = nmo_animation_serialize_internal(&in_state->base, out_chunk, context);
    if (result != NMO_OK) return result;

    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    const bool is_file = nmo_animation_is_file_mode_ser(out_chunk, context);

    if (is_file || (save_flags & CK_STATESAVE_KEYEDANIMANIMLIST) != 0) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KEYEDANIMANIMLIST);
        if (result != NMO_OK) return result;
        result = write_object_id_array(out_chunk, in_state->animation_ids, in_state->animation_count);
        if (result != NMO_OK) return result;
    }

    if (is_file || (save_flags & CK_STATESAVE_KEYEDANIMMERGE) != 0) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KEYEDANIMMERGE);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->merged);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
        if (result != NMO_OK) return result;
    }

    if (!is_file && (save_flags & CK_STATESAVE_KEYEDANIMSUBANIMS) != 0) {
        const uint32_t count = in_state->animation_count;
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KEYEDANIMSUBANIMS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, count);
        if (result != NMO_OK) return result;
        for (uint32_t i = 0; i < count; ++i) {
            nmo_chunk_t *sub = NULL;
            nmo_object_id_t object_id = 0;
            if (in_state->animation_ids && i < in_state->animation_count) {
                object_id = in_state->animation_ids[i];
            } else if (in_state->subanims && i < in_state->subanim_count) {
                object_id = in_state->subanims[i].object_id;
            }
            if (in_state->subanims && i < in_state->subanim_count) {
                sub = in_state->subanims[i].chunk;
            }
            result = nmo_chunk_write_object_id(out_chunk, object_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_sub_chunk(out_chunk, sub);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_objectanimation_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_objectanimation_state_t *out_state)
{
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_objectanimation_deserialize");
    }

    NMO_RETURN_IF_ERROR(nmo_objectanimation_create(out_state, NULL, context));

    {
        nmo_status_t result = nmo_sceneobject_deserialize(&out_state->base, chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    out_state->format = CKOBJANIM_FORMAT_NONE;
    out_state->has_root_pos = 0;
    out_state->root_pos.x = 0.0f;
    out_state->root_pos.y = 0.0f;
    out_state->root_pos.z = 0.0f;
    out_state->flags = 0;
    out_state->entity_id = 0;
    out_state->has_length = 0;
    out_state->length = 0.0f;
    out_state->has_merge = 0;
    out_state->merge_factor = 0.5f;
    out_state->anim1_id = 0;
    out_state->anim2_id = 0;
    out_state->has_shared_anim = 0;
    out_state->shared_anim_id = 0;
    out_state->has_morph_counts = 0;
    out_state->morph_vertex_count = 0;
    out_state->morph_key_count = 0;
    out_state->raw_tail = NULL;
    out_state->raw_tail_size = 0;

    uint32_t data_version = nmo_chunk_get_data_version(chunk);

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMSHARED) == NMO_OK) {
        out_state->format = CKOBJANIM_FORMAT_SHARED;
        out_state->has_shared_anim = 1;
        nmo_status_t result = nmo_chunk_read_object_id(chunk, &out_state->shared_anim_id);
        if (result != NMO_OK) return result;
        out_state->has_root_pos = 1;
        result = nmo_chunk_read_vector3(chunk, &out_state->root_pos);
        if (result != NMO_OK) return result;
        for (int i = 0; i < 4; ++i) {
            float tmp = 0.0f;
            result = nmo_chunk_read_float(chunk, &tmp);
            if (result != NMO_OK) return result;
        }
        result = nmo_chunk_read_dword(chunk, &out_state->flags);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_object_id(chunk, &out_state->entity_id);
        if (result != NMO_OK) return result;
        if (out_state->flags & 0x80u) {
            out_state->has_merge = 1;
            result = nmo_chunk_read_float(chunk, &out_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_object_id(chunk, &out_state->anim1_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_object_id(chunk, &out_state->anim2_id);
            if (result != NMO_OK) return result;
        }
        result = read_raw_tail(chunk, arena, &out_state->raw_tail, &out_state->raw_tail_size);
        if (result != NMO_OK) return result;
        NMO_RETURN_OK();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMCONTROLLERS) == NMO_OK) {
        out_state->format = CKOBJANIM_FORMAT_CONTROLLERS;
        out_state->has_root_pos = 1;
        nmo_status_t result = nmo_chunk_read_vector3(chunk, &out_state->root_pos);
        if (result != NMO_OK) return result;
        for (int i = 0; i < 4; ++i) {
            float tmp = 0.0f;
            result = nmo_chunk_read_float(chunk, &tmp);
            if (result != NMO_OK) return result;
        }
        result = nmo_chunk_read_dword(chunk, &out_state->flags);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_object_id(chunk, &out_state->entity_id);
        if (result != NMO_OK) return result;
        out_state->has_length = 1;
        result = nmo_chunk_read_float(chunk, &out_state->length);
        if (result != NMO_OK) return result;
        if (out_state->flags & 0x80u) {
            out_state->has_merge = 1;
            result = nmo_chunk_read_float(chunk, &out_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_object_id(chunk, &out_state->anim1_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_object_id(chunk, &out_state->anim2_id);
            if (result != NMO_OK) return result;
        }
        result = read_raw_tail(chunk, arena, &out_state->raw_tail, &out_state->raw_tail_size);
        if (result != NMO_OK) return result;
        NMO_RETURN_OK();
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMNEWDATA) == NMO_OK) {
        out_state->format = CKOBJANIM_FORMAT_NEWDATA;
        out_state->has_root_pos = 1;
        nmo_status_t result = nmo_chunk_read_vector3(chunk, &out_state->root_pos);
        if (result != NMO_OK) return result;
        for (int i = 0; i < 4; ++i) {
            float tmp = 0.0f;
            result = nmo_chunk_read_float(chunk, &tmp);
            if (result != NMO_OK) return result;
        }
        out_state->has_morph_counts = 1;
        result = nmo_chunk_read_int(chunk, &out_state->morph_vertex_count);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_int(chunk, &out_state->morph_key_count);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_dword(chunk, &out_state->flags);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_object_id(chunk, &out_state->entity_id);
        if (result != NMO_OK) return result;
        out_state->has_length = 1;
        result = nmo_chunk_read_float(chunk, &out_state->length);
        if (result != NMO_OK) return result;
        if (out_state->flags & 0x80u) {
            out_state->has_merge = 1;
            result = nmo_chunk_read_float(chunk, &out_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_object_id(chunk, &out_state->anim1_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_object_id(chunk, &out_state->anim2_id);
            if (result != NMO_OK) return result;
        }
        result = read_raw_tail(chunk, arena, &out_state->raw_tail, &out_state->raw_tail_size);
        if (result != NMO_OK) return result;
        NMO_RETURN_OK();
    }

    if (out_state->format == CKOBJANIM_FORMAT_NONE) {
        if (data_version < 1) {
            out_state->format = CKOBJANIM_FORMAT_LEGACY;
        }
        nmo_status_t result = read_raw_tail(chunk, arena, &out_state->raw_tail, &out_state->raw_tail_size);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_objectanimation_serialize_internal(
    const nmo_objectanimation_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_objectanimation_serialize");
    }

    {
        nmo_status_t result = nmo_sceneobject_serialize(&in_state->base, out_chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    const bool is_file = nmo_animation_is_file_mode_ser(out_chunk, context);
    if (!is_file && (save_flags & CK_STATESAVE_OBJANIMALL) == 0) {
        NMO_RETURN_OK();
    }

    switch (in_state->format) {
    case CKOBJANIM_FORMAT_SHARED: {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMSHARED);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->shared_anim_id);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->root_pos);
        if (result != NMO_OK) return result;
        for (int i = 0; i < 4; ++i) {
            result = nmo_chunk_write_float(out_chunk, 0.0f);
            if (result != NMO_OK) return result;
        }
        result = nmo_chunk_write_dword(out_chunk, in_state->flags);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->entity_id);
        if (result != NMO_OK) return result;
        if ((in_state->flags & 0x80u) != 0) {
            result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->anim1_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->anim2_id);
            if (result != NMO_OK) return result;
        }
        break;
    }
    case CKOBJANIM_FORMAT_CONTROLLERS: {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMCONTROLLERS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->root_pos);
        if (result != NMO_OK) return result;
        for (int i = 0; i < 4; ++i) {
            result = nmo_chunk_write_float(out_chunk, 0.0f);
            if (result != NMO_OK) return result;
        }
        result = nmo_chunk_write_dword(out_chunk, in_state->flags);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->entity_id);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->length);
        if (result != NMO_OK) return result;
        if ((in_state->flags & 0x80u) != 0) {
            result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->anim1_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->anim2_id);
            if (result != NMO_OK) return result;
        }
        break;
    }
    case CKOBJANIM_FORMAT_NEWDATA: {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMNEWDATA);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->root_pos);
        if (result != NMO_OK) return result;
        for (int i = 0; i < 4; ++i) {
            result = nmo_chunk_write_float(out_chunk, 0.0f);
            if (result != NMO_OK) return result;
        }
        result = nmo_chunk_write_int(out_chunk, in_state->morph_vertex_count);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_int(out_chunk, in_state->morph_key_count);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, in_state->flags);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->entity_id);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->length);
        if (result != NMO_OK) return result;
        if ((in_state->flags & 0x80u) != 0) {
            result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->anim1_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->anim2_id);
            if (result != NMO_OK) return result;
        }
        break;
    }
    default:
        break;
    }

    if (in_state->raw_tail && in_state->raw_tail_size > 0) {
        nmo_status_t result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                             in_state->raw_tail,
                                                             in_state->raw_tail_size);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_animation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_animation_state_t *out_state = (nmo_animation_state_t *)instance;
    return nmo_animation_deserialize_internal(chunk, context, out_state);
}

nmo_status_t nmo_animation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_animation_state_t *in_state = (const nmo_animation_state_t *)instance;
    return nmo_animation_serialize_internal(in_state, out_chunk, context);
}

nmo_status_t nmo_keyedanimation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_keyedanimation_state_t *out_state = (nmo_keyedanimation_state_t *)instance;
    return nmo_keyedanimation_deserialize_internal(chunk, context, out_state);
}

nmo_status_t nmo_keyedanimation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_keyedanimation_state_t *in_state =
        (const nmo_keyedanimation_state_t *)instance;
    return nmo_keyedanimation_serialize_internal(in_state, out_chunk, context);
}

nmo_status_t nmo_objectanimation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_objectanimation_state_t *out_state = (nmo_objectanimation_state_t *)instance;
    return nmo_objectanimation_deserialize_internal(chunk, context, out_state);
}

nmo_status_t nmo_objectanimation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_objectanimation_state_t *in_state =
        (const nmo_objectanimation_state_t *)instance;
    return nmo_objectanimation_serialize_internal(in_state, out_chunk, context);
}

