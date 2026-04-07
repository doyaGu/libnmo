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
    NMO_FIELD(nmo_objectanimation_state_t, controller_count, CKPGUID_UINT32),
    NMO_FIELD_OPT(nmo_objectanimation_state_t, controllers, CKPGUID_POINTER),
    NMO_FIELD(nmo_objectanimation_state_t, morph_key_parsed_count, CKPGUID_UINT32),
    NMO_FIELD_OPT(nmo_objectanimation_state_t, morph_keys, CKPGUID_POINTER),
    NMO_FIELD(nmo_objectanimation_state_t, morph_normals_id, CKPGUID_UINT32),
    NMO_FIELD(nmo_objectanimation_state_t, morph_normals_count, CKPGUID_UINT32),
    NMO_FIELD_OPT(nmo_objectanimation_state_t, morph_normals_sizes, CKPGUID_POINTER),
    NMO_FIELD_OPT(nmo_objectanimation_state_t, morph_normals_data, CKPGUID_POINTER),
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

    /* Deep copy controllers */
    if (s->controller_count > 0 && s->controllers != NULL) {
        nmo_objanim_controller_t *controllers = nmo_arena_alloc(
            arena, sizeof(nmo_objanim_controller_t) * s->controller_count,
            _Alignof(nmo_objanim_controller_t));
        if (!controllers) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate controllers array for copy");
        }
        memcpy(controllers, s->controllers, sizeof(nmo_objanim_controller_t) * s->controller_count);
        for (uint32_t i = 0; i < s->controller_count; ++i) {
            if (s->controllers[i].data_size > 0 && s->controllers[i].data != NULL) {
                void *data = nmo_arena_alloc(arena, s->controllers[i].data_size, 1);
                if (!data) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                     "Failed to allocate controller data for copy");
                }
                memcpy(data, s->controllers[i].data, s->controllers[i].data_size);
                controllers[i].data = data;
            }
        }
        d->controllers = controllers;
    }

    /* Deep copy morph keys */
    if (s->morph_key_parsed_count > 0 && s->morph_keys != NULL) {
        nmo_objanim_morph_key_t *morph_keys = nmo_arena_alloc(
            arena, sizeof(nmo_objanim_morph_key_t) * s->morph_key_parsed_count,
            _Alignof(nmo_objanim_morph_key_t));
        if (!morph_keys) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate morph keys array for copy");
        }
        memcpy(morph_keys, s->morph_keys, sizeof(nmo_objanim_morph_key_t) * s->morph_key_parsed_count);
        for (uint32_t i = 0; i < s->morph_key_parsed_count; ++i) {
            if (s->morph_keys[i].data_size > 0 && s->morph_keys[i].data != NULL) {
                void *data = nmo_arena_alloc(arena, s->morph_keys[i].data_size, 1);
                if (!data) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                     "Failed to allocate morph key data for copy");
                }
                memcpy(data, s->morph_keys[i].data, s->morph_keys[i].data_size);
                morph_keys[i].data = data;
            }
        }
        d->morph_keys = morph_keys;
    }

    /* Deep copy morph normals (both arrays must be present together) */
    if (s->morph_normals_count > 0 &&
        s->morph_normals_sizes != NULL && s->morph_normals_data != NULL) {
        uint32_t *sizes = nmo_arena_alloc(arena, sizeof(uint32_t) * s->morph_normals_count,
                                          _Alignof(uint32_t));
        if (!sizes) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate morph normals sizes for copy");
        }
        memcpy(sizes, s->morph_normals_sizes, sizeof(uint32_t) * s->morph_normals_count);
        d->morph_normals_sizes = sizes;

        void **data_ptrs = nmo_arena_alloc(arena, sizeof(void *) * s->morph_normals_count,
                                           _Alignof(void *));
        if (!data_ptrs) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate morph normals data array for copy");
        }
        for (uint32_t i = 0; i < s->morph_normals_count; ++i) {
            if (s->morph_normals_sizes[i] > 0 && s->morph_normals_data[i] != NULL) {
                void *data = nmo_arena_alloc(arena, s->morph_normals_sizes[i], 4);
                if (!data) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                     "Failed to allocate morph normals data for copy");
                }
                memcpy(data, s->morph_normals_data[i], s->morph_normals_sizes[i]);
                data_ptrs[i] = data;
            } else {
                data_ptrs[i] = NULL;
            }
        }
        d->morph_normals_data = data_ptrs;
    }

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
    NMO_VALIDATE_COUNT(s->controllers, s->controller_count, "controllers");
    NMO_VALIDATE_COUNT(s->morph_keys, s->morph_key_parsed_count, "morph_keys");
    NMO_VALIDATE_COUNT(s->morph_normals_sizes, s->morph_normals_count, "morph_normals_sizes");
    NMO_VALIDATE_COUNT(s->morph_normals_data, s->morph_normals_count, "morph_normals_data");
    NMO_VALIDATE_BYTES(s->raw_tail, s->raw_tail_size, "raw_tail");
    NMO_RETURN_OK();
}

nmo_status_t nmo_animation_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_animation_validate(instance, type, context);
}

nmo_status_t nmo_animation_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_animation_remap_dependencies");
    }

    nmo_animation_state_t *state = (nmo_animation_state_t *)instance;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)context;

    NMO_RETURN_IF_ERROR(nmo_sceneobject_remap_dependencies(&state->base, NULL, context));

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

nmo_status_t nmo_keyedanimation_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_keyedanimation_validate(instance, type, context);
}

nmo_status_t nmo_keyedanimation_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_keyedanimation_remap_dependencies");
    }

    nmo_keyedanimation_state_t *state = (nmo_keyedanimation_state_t *)instance;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)context;

    NMO_RETURN_IF_ERROR(nmo_animation_remap_dependencies(&state->base, NULL, context));

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

nmo_status_t nmo_objectanimation_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_objectanimation_validate(instance, type, context);
}

nmo_status_t nmo_objectanimation_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_objectanimation_remap_dependencies");
    }

    nmo_objectanimation_state_t *state = (nmo_objectanimation_state_t *)instance;
    nmo_object_repository_t *repo = (nmo_object_repository_t *)context;

    NMO_RETURN_IF_ERROR(nmo_sceneobject_remap_dependencies(&state->base, NULL, context));

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

static nmo_status_t nmo_animation_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_animation_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_animation_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

static nmo_status_t nmo_keyedanimation_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_keyedanimation_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_keyedanimation_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

static nmo_status_t nmo_objectanimation_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_objectanimation_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_objectanimation_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(animation, nmo_animation_state_t)
NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(keyedanimation, nmo_keyedanimation_state_t)
NMO_DEFINE_OBJECT_STATE_OPS_CUSTOM(objectanimation, nmo_objectanimation_state_t)

nmo_type_vtable_t nmo_animation_vtable = {
    .prepare_dependencies = nmo_animation_prepare_dependencies,
    .remap_dependencies = nmo_animation_remap_dependencies,
    .pre_delete = nmo_animation_pre_delete,
    .post_delete = nmo_animation_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_animation_create,
        nmo_animation_destroy,
        nmo_animation_serialize,
        nmo_animation_deserialize,
        nmo_animation_copy,
        nmo_animation_validate,
        nmo_animation_equals,
        nmo_animation_hash)
};

nmo_type_vtable_t nmo_keyedanimation_vtable = {
    .prepare_dependencies = nmo_keyedanimation_prepare_dependencies,
    .remap_dependencies = nmo_keyedanimation_remap_dependencies,
    .pre_delete = nmo_keyedanimation_pre_delete,
    .post_delete = nmo_keyedanimation_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_keyedanimation_create,
        nmo_keyedanimation_destroy,
        nmo_keyedanimation_serialize,
        nmo_keyedanimation_deserialize,
        nmo_keyedanimation_copy,
        nmo_keyedanimation_validate,
        nmo_keyedanimation_equals,
        nmo_keyedanimation_hash)
};

nmo_type_vtable_t nmo_objectanimation_vtable = {
    .prepare_dependencies = nmo_objectanimation_prepare_dependencies,
    .remap_dependencies = nmo_objectanimation_remap_dependencies,
    .pre_delete = nmo_objectanimation_pre_delete,
    .post_delete = nmo_objectanimation_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_objectanimation_create,
        nmo_objectanimation_destroy,
        nmo_objectanimation_serialize,
        nmo_objectanimation_deserialize,
        nmo_objectanimation_copy,
        nmo_objectanimation_validate,
        nmo_objectanimation_equals,
        nmo_objectanimation_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_animation_type,
    CKPGUID_ANIMATION,
    "CKAnimation",
    NMO_CID_ANIMATION,
    CKPGUID_SCENEOBJECT,
    nmo_animation_state_t,
    &nmo_animation_vtable,
    nmo_animation_fields)

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_keyedanimation_type,
    CKPGUID_KEYEDANIMATION,
    "CKKeyedAnimation",
    NMO_CID_KEYEDANIMATION,
    CKPGUID_ANIMATION,
    nmo_keyedanimation_state_t,
    &nmo_keyedanimation_vtable,
    nmo_keyedanimation_fields)

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_objectanimation_type,
    CKPGUID_OBJECTANIMATION,
    "CKObjectAnimation",
    NMO_CID_OBJECTANIMATION,
    CKPGUID_ANIMATION,
    nmo_objectanimation_state_t,
    &nmo_objectanimation_vtable,
    nmo_objectanimation_fields)

static nmo_status_t write_object_id_array(
    nmo_chunk_t *chunk,
    const nmo_object_id_t *ids,
    uint32_t count)
{
    return nmo_chunk_write_object_id_array(chunk, ids, count);
}

/* Animation controller type constants */
#define CKANIMATION_LINPOS_CONTROL      0x637c4301u
#define CKANIMATION_LINROT_CONTROL      0x49ed4002u
#define CKANIMATION_LINSCL_CONTROL      0x654a3a04u
#define CKANIMATION_LINSCLAXIS_CONTROL  0x2f200b08u
#define CKANIMATION_TCBPOS_CONTROL      0x347e4a01u
#define CKANIMATION_TCBROT_CONTROL      0x45b52a02u
#define CKANIMATION_TCBSCL_CONTROL      0x1b545904u
#define CKANIMATION_TCBSCLAXIS_CONTROL  0x32595908u
#define CKANIMATION_BEZIERPOS_CONTROL   0x921ab801u
#define CKANIMATION_BEZIERSCL_CONTROL   0x18ab4404u

uint32_t nmo_objanim_controller_key_size(uint32_t type)
{
    switch (type) {
    case CKANIMATION_LINPOS_CONTROL:      return 16;
    case CKANIMATION_LINROT_CONTROL:      return 20;
    case CKANIMATION_LINSCL_CONTROL:      return 16;
    case CKANIMATION_LINSCLAXIS_CONTROL:  return 20;
    case CKANIMATION_TCBPOS_CONTROL:      return 36;
    case CKANIMATION_TCBROT_CONTROL:      return 40;
    case CKANIMATION_TCBSCL_CONTROL:      return 36;
    case CKANIMATION_TCBSCLAXIS_CONTROL:  return 40;
    case CKANIMATION_BEZIERPOS_CONTROL:   return 44;
    case CKANIMATION_BEZIERSCL_CONTROL:   return 44;
    default: return 0;
    }
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
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR, "Failed to read raw tail buffer");
    }

    *out_data = data;
    *out_size = remaining_bytes;
    NMO_RETURN_OK();
}

/* Read controllers in CONTROLLERS format: loop of {type(DWORD), size_dwords(DWORD), data[]} until type==0 */
static nmo_status_t read_controllers_loop(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_objectanimation_state_t *out_state)
{
    nmo_objanim_controller_t local_controllers[8];
    uint32_t count = 0;

    while (count < 8) {
        uint32_t type = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &type));
        if (type == 0) {
            break;
        }

        uint32_t size_dwords = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &size_dwords));
        uint32_t data_size = size_dwords * 4;

        void *data = NULL;
        if (data_size > 0) {
            data = nmo_arena_alloc(arena, data_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate controller data buffer");
            }
            size_t bytes_read = nmo_chunk_read_and_fill_buffer_nosize(chunk, data, data_size);
            if (bytes_read != data_size) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                 "Failed to read controller data");
            }
        }

        local_controllers[count].type = type;
        local_controllers[count].key_count = 0;
        local_controllers[count].data_size = data_size;
        local_controllers[count].data = data;
        count++;
    }

    if (count > 0) {
        nmo_objanim_controller_t *controllers = nmo_arena_alloc(
            arena, sizeof(nmo_objanim_controller_t) * count,
            _Alignof(nmo_objanim_controller_t));
        if (!controllers) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate controllers array");
        }
        memcpy(controllers, local_controllers, sizeof(nmo_objanim_controller_t) * count);
        out_state->controllers = controllers;
        out_state->controller_count = count;
    }

    NMO_RETURN_OK();
}

/* Read controllers in NEWDATA format: morph keys + 4 inline controllers + optional morph normals */
static nmo_status_t read_newdata_controllers(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_objectanimation_state_t *out_state)
{
    nmo_objanim_controller_t local_controllers[8];
    uint32_t count = 0;

    /* 1. Read morph keys if present */
    if (out_state->morph_key_count > 0) {
        int32_t morph_key_count = out_state->morph_key_count;
        if (morph_key_count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "Invalid morph key count");
        }
        nmo_objanim_morph_key_t *morph_keys = nmo_arena_alloc(
            arena, sizeof(nmo_objanim_morph_key_t) * (uint32_t)morph_key_count,
            _Alignof(nmo_objanim_morph_key_t));
        if (!morph_keys) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate morph keys array");
        }

        for (int32_t i = 0; i < morph_key_count; ++i) {
            float time_step = 0.0f;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &time_step));
            morph_keys[i].time_step = time_step;

            uint32_t size_bytes = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &size_bytes));
            morph_keys[i].data_size = size_bytes;

            if (size_bytes > 0) {
                void *data = nmo_arena_alloc(arena, size_bytes, 4);
                if (!data) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                     "Failed to allocate morph key data");
                }
                size_t bytes_read = nmo_chunk_read_and_fill_buffer_nosize(chunk, data, size_bytes);
                if (bytes_read != size_bytes) {
                    NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                     "Failed to read morph key data");
                }
                morph_keys[i].data = data;
            } else {
                morph_keys[i].data = NULL;
            }
        }

        out_state->morph_keys = morph_keys;
        out_state->morph_key_parsed_count = (uint32_t)morph_key_count;
    }

    /* 2. Read 4 controllers: position, scale, rotation, scaleAxis */
    static const uint32_t controller_types[4] = {
        CKANIMATION_LINPOS_CONTROL,
        CKANIMATION_LINSCL_CONTROL,
        CKANIMATION_LINROT_CONTROL,
        CKANIMATION_LINSCLAXIS_CONTROL
    };

    for (int i = 0; i < 4; ++i) {
        uint32_t buf_size = 0;
        uint32_t key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &buf_size));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &key_count));

        if (key_count > 0 && buf_size > 0) {
            void *data = nmo_arena_alloc(arena, buf_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate controller data");
            }
            size_t bytes_read = nmo_chunk_read_and_fill_buffer_nosize(chunk, data, buf_size);
            if (bytes_read != buf_size) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                 "Failed to read controller data");
            }

            local_controllers[count].type = controller_types[i];
            local_controllers[count].key_count = key_count;
            local_controllers[count].data_size = buf_size;
            local_controllers[count].data = data;
            count++;
        }
    }

    /* 3. Check for optional morph normals */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMMORPHCOMP) == NMO_OK &&
        out_state->morph_key_parsed_count > 0) {
        out_state->morph_normals_id = CK_STATESAVE_OBJANIMMORPHCOMP;
        out_state->morph_normals_count = out_state->morph_key_parsed_count;

        uint32_t *sizes = nmo_arena_alloc(arena, sizeof(uint32_t) * out_state->morph_normals_count,
                                          _Alignof(uint32_t));
        void **data_ptrs = nmo_arena_alloc(arena, sizeof(void *) * out_state->morph_normals_count,
                                           _Alignof(void *));
        if (!sizes || !data_ptrs) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate morph normals arrays");
        }

        for (uint32_t i = 0; i < out_state->morph_normals_count; ++i) {
            uint32_t size_bytes = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &size_bytes));
            sizes[i] = size_bytes;

            if (size_bytes > 0) {
                void *data = nmo_arena_alloc(arena, size_bytes, 4);
                if (!data) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                     "Failed to allocate morph normal data");
                }
                size_t bytes_read = nmo_chunk_read_and_fill_buffer_nosize(chunk, data, size_bytes);
                if (bytes_read != size_bytes) {
                    NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                     "Failed to read morph normal data");
                }
                data_ptrs[i] = data;
            } else {
                data_ptrs[i] = NULL;
            }
        }

        out_state->morph_normals_sizes = sizes;
        out_state->morph_normals_data = data_ptrs;
    } else if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMMORPHNORMALS) == NMO_OK &&
               out_state->morph_key_parsed_count > 0) {
        out_state->morph_normals_id = CK_STATESAVE_OBJANIMMORPHNORMALS;
        out_state->morph_normals_count = out_state->morph_key_parsed_count;

        uint32_t *sizes = nmo_arena_alloc(arena, sizeof(uint32_t) * out_state->morph_normals_count,
                                          _Alignof(uint32_t));
        void **data_ptrs = nmo_arena_alloc(arena, sizeof(void *) * out_state->morph_normals_count,
                                           _Alignof(void *));
        if (!sizes || !data_ptrs) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate morph normals arrays");
        }

        for (uint32_t i = 0; i < out_state->morph_normals_count; ++i) {
            uint32_t size_bytes = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &size_bytes));
            sizes[i] = size_bytes;

            if (size_bytes > 0) {
                void *data = nmo_arena_alloc(arena, size_bytes, 4);
                if (!data) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                     "Failed to allocate morph normal data");
                }
                size_t bytes_read = nmo_chunk_read_and_fill_buffer_nosize(chunk, data, size_bytes);
                if (bytes_read != size_bytes) {
                    NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                     "Failed to read morph normal data");
                }
                data_ptrs[i] = data;
            } else {
                data_ptrs[i] = NULL;
            }
        }

        out_state->morph_normals_sizes = sizes;
        out_state->morph_normals_data = data_ptrs;
    }

    /* Copy controllers to arena-allocated array */
    if (count > 0) {
        nmo_objanim_controller_t *controllers = nmo_arena_alloc(
            arena, sizeof(nmo_objanim_controller_t) * count,
            _Alignof(nmo_objanim_controller_t));
        if (!controllers) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate controllers array");
        }
        memcpy(controllers, local_controllers, sizeof(nmo_objanim_controller_t) * count);
        out_state->controllers = controllers;
        out_state->controller_count = count;
    }

    NMO_RETURN_OK();
}

/* Read controllers in LEGACY format: identifier-based sections */
static nmo_status_t read_legacy_controllers(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_objectanimation_state_t *out_state)
{
    nmo_objanim_controller_t local_controllers[8];
    uint32_t count = 0;

    /* Skip old morphkeys identifier if present */
    (void)nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMMORPHKEYS);

    /* Read morph keys (legacy format) */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMMORPHKEYS2) == NMO_OK) {
        int32_t morph_key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &morph_key_count));
        if (morph_key_count > 0) {
            int32_t morph_vertex_count = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &morph_vertex_count));

            out_state->has_morph_counts = 1;
            out_state->morph_key_count = morph_key_count;
            out_state->morph_vertex_count = morph_vertex_count;

            nmo_objanim_morph_key_t *morph_keys = nmo_arena_alloc(
                arena, sizeof(nmo_objanim_morph_key_t) * (uint32_t)morph_key_count,
                _Alignof(nmo_objanim_morph_key_t));
            if (!morph_keys) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate morph keys array");
            }

            for (int32_t i = 0; i < morph_key_count; ++i) {
                float time_step = 0.0f;
                NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &time_step));
                morph_keys[i].time_step = time_step;

                uint32_t size_bytes = 0;
                NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &size_bytes));
                morph_keys[i].data_size = size_bytes;

                if (size_bytes > 0) {
                    void *data = nmo_arena_alloc(arena, size_bytes, 4);
                    if (!data) {
                        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                         "Failed to allocate morph key data");
                    }
                    size_t bytes_read = nmo_chunk_read_and_fill_buffer_nosize(chunk, data, size_bytes);
                    if (bytes_read != size_bytes) {
                        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                         "Failed to read morph key data");
                    }
                    morph_keys[i].data = data;
                } else {
                    morph_keys[i].data = NULL;
                }
            }

            out_state->morph_keys = morph_keys;
            out_state->morph_key_parsed_count = (uint32_t)morph_key_count;
        }
    }

    /* Read position controller */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMPOSKEYS) == NMO_OK) {
        uint32_t buf_size = 0;
        uint32_t key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &buf_size));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &key_count));

        if (key_count > 0 && buf_size > 0) {
            void *data = nmo_arena_alloc(arena, buf_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate position controller data");
            }
            size_t bytes_read = nmo_chunk_read_and_fill_buffer_nosize(chunk, data, buf_size);
            if (bytes_read != buf_size) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                 "Failed to read position controller data");
            }

            local_controllers[count].type = CKANIMATION_LINPOS_CONTROL;
            local_controllers[count].key_count = key_count;
            local_controllers[count].data_size = buf_size;
            local_controllers[count].data = data;
            count++;
        }
    }

    /* Read rotation controller + scale axis controller */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMROTKEYS) == NMO_OK) {
        uint32_t rot_buf_size = 0;
        uint32_t rot_key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &rot_buf_size));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &rot_key_count));

        if (rot_key_count > 0 && rot_buf_size > 0) {
            void *data = nmo_arena_alloc(arena, rot_buf_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate rotation controller data");
            }
            size_t bytes_read = nmo_chunk_read_and_fill_buffer_nosize(chunk, data, rot_buf_size);
            if (bytes_read != rot_buf_size) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                 "Failed to read rotation controller data");
            }

            local_controllers[count].type = CKANIMATION_LINROT_CONTROL;
            local_controllers[count].key_count = rot_key_count;
            local_controllers[count].data_size = rot_buf_size;
            local_controllers[count].data = data;
            count++;
        }

        uint32_t axis_buf_size = 0;
        uint32_t axis_key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &axis_buf_size));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &axis_key_count));

        if (axis_key_count > 0 && axis_buf_size > 0) {
            void *data = nmo_arena_alloc(arena, axis_buf_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate scale axis controller data");
            }
            size_t bytes_read = nmo_chunk_read_and_fill_buffer_nosize(chunk, data, axis_buf_size);
            if (bytes_read != axis_buf_size) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                 "Failed to read scale axis controller data");
            }

            local_controllers[count].type = CKANIMATION_LINSCLAXIS_CONTROL;
            local_controllers[count].key_count = axis_key_count;
            local_controllers[count].data_size = axis_buf_size;
            local_controllers[count].data = data;
            count++;
        }
    }

    /* Read scale controller */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMSCLKEYS) == NMO_OK) {
        uint32_t buf_size = 0;
        uint32_t key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &buf_size));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &key_count));

        if (key_count > 0 && buf_size > 0) {
            void *data = nmo_arena_alloc(arena, buf_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate scale controller data");
            }
            size_t bytes_read = nmo_chunk_read_and_fill_buffer_nosize(chunk, data, buf_size);
            if (bytes_read != buf_size) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                 "Failed to read scale controller data");
            }

            local_controllers[count].type = CKANIMATION_LINSCL_CONTROL;
            local_controllers[count].key_count = key_count;
            local_controllers[count].data_size = buf_size;
            local_controllers[count].data = data;
            count++;
        }
    }

    /* Read legacy header fields */
    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMFLAGS) == NMO_OK) {
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->flags));
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMENTITY) == NMO_OK) {
        NMO_RETURN_IF_ERROR(nmo_chunk_read_object_id(chunk, &out_state->entity_id));
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMLENGTH) == NMO_OK) {
        out_state->has_length = 1;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->length));
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMMERGE) == NMO_OK) {
        NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->merge_factor));
        int32_t merged = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &merged));
        if (merged) {
            out_state->flags |= 0x80u;
            out_state->has_merge = 1;
        } else {
            out_state->flags &= ~0x80u;
            out_state->has_merge = 0;
        }
        NMO_RETURN_IF_ERROR(nmo_chunk_read_object_id(chunk, &out_state->anim1_id));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_object_id(chunk, &out_state->anim2_id));
    }

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_OBJANIMNEWDATA) == NMO_OK) {
        out_state->has_root_pos = 1;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_vector3(chunk, &out_state->root_pos));
    }

    /* Copy controllers to arena-allocated array */
    if (count > 0) {
        nmo_objanim_controller_t *controllers = nmo_arena_alloc(
            arena, sizeof(nmo_objanim_controller_t) * count,
            _Alignof(nmo_objanim_controller_t));
        if (!controllers) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate controllers array");
        }
        memcpy(controllers, local_controllers, sizeof(nmo_objanim_controller_t) * count);
        out_state->controllers = controllers;
        out_state->controller_count = count;
    }

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
    out_state->controller_count = 0;
    out_state->controllers = NULL;
    out_state->morph_key_parsed_count = 0;
    out_state->morph_keys = NULL;
    out_state->morph_normals_id = 0;
    out_state->morph_normals_count = 0;
    out_state->morph_normals_sizes = NULL;
    out_state->morph_normals_data = NULL;
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
        /* SHARED format has no controller data, keep raw_tail for any remainder */
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
        /* CONTROLLERS format: parse controller loop */
        result = read_controllers_loop(chunk, arena, out_state);
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
        /* NEWDATA format: parse morph keys + 4 inline controllers */
        result = read_newdata_controllers(chunk, arena, out_state);
        if (result != NMO_OK) return result;
        NMO_RETURN_OK();
    }

    if (out_state->format == CKOBJANIM_FORMAT_NONE) {
        if (data_version < 1) {
            out_state->format = CKOBJANIM_FORMAT_LEGACY;
            /* LEGACY format: parse identifier-based sections */
            nmo_status_t result = read_legacy_controllers(chunk, arena, out_state);
            if (result != NMO_OK) return result;
        } else {
            /* Unknown format or empty, use raw_tail as fallback */
            nmo_status_t result = read_raw_tail(chunk, arena, &out_state->raw_tail, &out_state->raw_tail_size);
            if (result != NMO_OK) return result;
        }
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

    /* Write controller data based on format */
    if (in_state->format == CKOBJANIM_FORMAT_CONTROLLERS) {
        /* CONTROLLERS format: write {type, data_size/4, data} per controller + terminator 0 */
        for (uint32_t i = 0; i < in_state->controller_count; ++i) {
            const nmo_objanim_controller_t *ctrl = &in_state->controllers[i];
            nmo_status_t result = nmo_chunk_write_dword(out_chunk, ctrl->type);
            if (result != NMO_OK) return result;
            uint32_t size_dwords = ctrl->data_size / 4;
            result = nmo_chunk_write_dword(out_chunk, size_dwords);
            if (result != NMO_OK) return result;
            if (ctrl->data_size > 0 && ctrl->data != NULL) {
                result = nmo_chunk_write_buffer_no_size(out_chunk, ctrl->data, ctrl->data_size);
                if (result != NMO_OK) return result;
            }
        }
        /* Write terminator */
        nmo_status_t result = nmo_chunk_write_dword(out_chunk, 0);
        if (result != NMO_OK) return result;
    } else if (in_state->format == CKOBJANIM_FORMAT_NEWDATA) {
        /* NEWDATA format: write morph keys, then 4 controllers as {bufSize, keyCount, data} */
        if (in_state->morph_key_parsed_count > 0 && in_state->morph_keys != NULL) {
            for (uint32_t i = 0; i < in_state->morph_key_parsed_count; ++i) {
                const nmo_objanim_morph_key_t *key = &in_state->morph_keys[i];
                nmo_status_t result = nmo_chunk_write_float(out_chunk, key->time_step);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_dword(out_chunk, key->data_size);
                if (result != NMO_OK) return result;
                if (key->data_size > 0 && key->data != NULL) {
                    result = nmo_chunk_write_buffer_no_size(out_chunk, key->data, key->data_size);
                    if (result != NMO_OK) return result;
                }
            }
        }

        /* Write 4 controllers in fixed order: position, scale, rotation, scaleAxis */
        static const uint32_t expected_types[4] = {
            CKANIMATION_LINPOS_CONTROL,
            CKANIMATION_LINSCL_CONTROL,
            CKANIMATION_LINROT_CONTROL,
            CKANIMATION_LINSCLAXIS_CONTROL
        };

        for (int slot = 0; slot < 4; ++slot) {
            const nmo_objanim_controller_t *ctrl = NULL;
            for (uint32_t i = 0; i < in_state->controller_count; ++i) {
                if (in_state->controllers[i].type == expected_types[slot]) {
                    ctrl = &in_state->controllers[i];
                    break;
                }
            }

            uint32_t buf_size = ctrl ? ctrl->data_size : 0;
            uint32_t key_count = ctrl ? ctrl->key_count : 0;
            nmo_status_t result = nmo_chunk_write_dword(out_chunk, buf_size);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, key_count);
            if (result != NMO_OK) return result;
            if (ctrl && buf_size > 0 && ctrl->data != NULL) {
                result = nmo_chunk_write_buffer_no_size(out_chunk, ctrl->data, buf_size);
                if (result != NMO_OK) return result;
            }
        }

        /* Write optional morph normals if present */
        if (in_state->morph_normals_id != 0 && in_state->morph_normals_count > 0) {
            nmo_status_t result = nmo_chunk_write_identifier(out_chunk, in_state->morph_normals_id);
            if (result != NMO_OK) return result;
            for (uint32_t i = 0; i < in_state->morph_normals_count; ++i) {
                uint32_t size_bytes = in_state->morph_normals_sizes[i];
                result = nmo_chunk_write_dword(out_chunk, size_bytes);
                if (result != NMO_OK) return result;
                if (size_bytes > 0 && in_state->morph_normals_data[i] != NULL) {
                    result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                            in_state->morph_normals_data[i],
                                                            size_bytes);
                    if (result != NMO_OK) return result;
                }
            }
        }
    } else if (in_state->format == CKOBJANIM_FORMAT_LEGACY) {
        /* LEGACY format: write identifier-based sections */

        /* Morph keys */
        if (in_state->morph_key_parsed_count > 0 && in_state->morph_keys != NULL) {
            nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMMORPHKEYS2);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_int(out_chunk, (int32_t)in_state->morph_key_parsed_count);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_int(out_chunk, in_state->morph_vertex_count);
            if (result != NMO_OK) return result;
            for (uint32_t i = 0; i < in_state->morph_key_parsed_count; ++i) {
                const nmo_objanim_morph_key_t *mk = &in_state->morph_keys[i];
                result = nmo_chunk_write_float(out_chunk, mk->time_step);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_dword(out_chunk, mk->data_size);
                if (result != NMO_OK) return result;
                if (mk->data_size > 0 && mk->data != NULL) {
                    result = nmo_chunk_write_buffer_no_size(out_chunk, mk->data, mk->data_size);
                    if (result != NMO_OK) return result;
                }
            }
        }

        /* Write controllers with their legacy identifiers */
        for (uint32_t i = 0; i < in_state->controller_count; ++i) {
            const nmo_objanim_controller_t *ctrl = &in_state->controllers[i];
            uint32_t id = 0;
            if (ctrl->type == CKANIMATION_LINPOS_CONTROL)
                id = CK_STATESAVE_OBJANIMPOSKEYS;
            else if (ctrl->type == CKANIMATION_LINROT_CONTROL)
                id = CK_STATESAVE_OBJANIMROTKEYS;
            else if (ctrl->type == CKANIMATION_LINSCL_CONTROL)
                id = CK_STATESAVE_OBJANIMSCLKEYS;
            else if (ctrl->type == CKANIMATION_LINSCLAXIS_CONTROL)
                id = CK_STATESAVE_OBJANIMROTKEYS; /* packed with rotation */

            /* ScaleAxis is packed inside ROTKEYS section, skip standalone write */
            if (ctrl->type == CKANIMATION_LINSCLAXIS_CONTROL)
                continue;

            if (id != 0) {
                nmo_status_t result = nmo_chunk_write_identifier(out_chunk, id);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_dword(out_chunk, ctrl->data_size);
                if (result != NMO_OK) return result;
                result = nmo_chunk_write_dword(out_chunk, ctrl->key_count);
                if (result != NMO_OK) return result;
                if (ctrl->data_size > 0 && ctrl->data != NULL) {
                    result = nmo_chunk_write_buffer_no_size(out_chunk, ctrl->data, ctrl->data_size);
                    if (result != NMO_OK) return result;
                }

                /* If this is rotation, append scale axis controller data */
                if (ctrl->type == CKANIMATION_LINROT_CONTROL) {
                    const nmo_objanim_controller_t *axis = NULL;
                    for (uint32_t j = 0; j < in_state->controller_count; ++j) {
                        if (in_state->controllers[j].type == CKANIMATION_LINSCLAXIS_CONTROL) {
                            axis = &in_state->controllers[j];
                            break;
                        }
                    }
                    if (axis != NULL) {
                        result = nmo_chunk_write_dword(out_chunk, axis->data_size);
                        if (result != NMO_OK) return result;
                        result = nmo_chunk_write_dword(out_chunk, axis->key_count);
                        if (result != NMO_OK) return result;
                        if (axis->data_size > 0 && axis->data != NULL) {
                            result = nmo_chunk_write_buffer_no_size(out_chunk, axis->data, axis->data_size);
                            if (result != NMO_OK) return result;
                        }
                    } else {
                        result = nmo_chunk_write_dword(out_chunk, 0);
                        if (result != NMO_OK) return result;
                        result = nmo_chunk_write_dword(out_chunk, 0);
                        if (result != NMO_OK) return result;
                    }
                }
            }
        }

        /* Legacy header fields */
        if (in_state->flags != 0) {
            nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMFLAGS);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_dword(out_chunk, in_state->flags);
            if (result != NMO_OK) return result;
        }

        if (in_state->entity_id != 0) {
            nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMENTITY);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->entity_id);
            if (result != NMO_OK) return result;
        }

        if (in_state->has_length) {
            nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMLENGTH);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_float(out_chunk, in_state->length);
            if (result != NMO_OK) return result;
        }

        if (in_state->has_merge) {
            nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMMERGE);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_int(out_chunk, (in_state->flags & 0x80u) ? 1 : 0);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->anim1_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_object_id(out_chunk, in_state->anim2_id);
            if (result != NMO_OK) return result;
        }

        if (in_state->has_root_pos) {
            nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMNEWDATA);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_vector3(out_chunk, &in_state->root_pos);
            if (result != NMO_OK) return result;
        }

        /* Fallback raw_tail for any remaining unparsed data */
        if (in_state->raw_tail && in_state->raw_tail_size > 0) {
            nmo_status_t result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                                 in_state->raw_tail,
                                                                 in_state->raw_tail_size);
            if (result != NMO_OK) return result;
        }
    } else {
        /* Fallback: write raw_tail if present */
        if (in_state->raw_tail && in_state->raw_tail_size > 0) {
            nmo_status_t result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                                 in_state->raw_tail,
                                                                 in_state->raw_tail_size);
            if (result != NMO_OK) return result;
        }
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
