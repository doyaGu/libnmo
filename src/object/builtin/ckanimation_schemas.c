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
#include "core/nmo_utils.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_struct_guids.h"
#include <string.h>
#include <stddef.h>

NMO_DEFINE_OBJECT_LIFECYCLE(
    animation,
    nmo_animation_state_t,
    do {
        nmo_status_t result = nmo_sceneobject_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
        state->flags = CKANIMATION_LINKTOFRAMERATE | CKANIMATION_CANBEBREAK;
        state->frame_rate = 30.0f;
        state->length = 100.0f;
        state->current_step = 0.0f;
    } while (0),
    nmo_sceneobject_vtable.destroy(&state->base, NULL, context))

NMO_DEFINE_OBJECT_LIFECYCLE(
    keyedanimation,
    nmo_keyedanimation_state_t,
    do {
        nmo_status_t result = nmo_animation_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
        state->merge_factor = 0.5f;
    } while (0),
    nmo_animation_vtable.destroy(&state->base, NULL, context))

NMO_DEFINE_OBJECT_LIFECYCLE(
    objectanimation,
    nmo_objectanimation_state_t,
    do {
        nmo_status_t result = nmo_sceneobject_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
        state->format = CKOBJANIM_FORMAT_NONE;
        state->merge_factor = 0.5f;
    } while (0),
    nmo_sceneobject_vtable.destroy(&state->base, NULL, context))

/* CKAnimation flag bits (subset used during legacy load) */
#define CKANIMATION_LINKTOFRAMERATE       0x00000001u
#define CKANIMATION_CANBEBREAK            0x00000004u
#define CKANIMATION_ALIGNORIENTATION      0x00000010u

/* =============================================================================
 * REFLECTION FIELDS
 * ============================================================================= */

static const nmo_type_field_t nmo_animation_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_animation_state_t, base),
                    sizeof(nmo_sceneobject_state_t), CKPGUID_SCENEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_animation_state_t, has_data, CKPGUID_UINT8),
    NMO_FIELD(nmo_animation_state_t, flags, CKPGUID_UINT32),
    NMO_FIELD(nmo_animation_state_t, frame_rate, CKPGUID_FLOAT),
    NMO_FIELD(nmo_animation_state_t, has_length, CKPGUID_UINT8),
    NMO_FIELD(nmo_animation_state_t, length, CKPGUID_FLOAT),
    NMO_FIELD(nmo_animation_state_t, has_root_entity, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_animation_state_t, root_entity),
    NMO_FIELD(nmo_animation_state_t, has_character, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_animation_state_t, character),
    NMO_FIELD(nmo_animation_state_t, has_current_step, CKPGUID_UINT8),
    NMO_FIELD(nmo_animation_state_t, current_step, CKPGUID_FLOAT)
};

static const nmo_type_field_t nmo_keyedanimation_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_keyedanimation_state_t, base),
                    sizeof(nmo_animation_state_t), CKPGUID_ANIMATION,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_keyedanimation_state_t, animation_count, CKPGUID_UINT32),
    NMO_FIELD_REF_RECORD_ARRAY_COUNTED(nmo_keyedanimation_state_t, animation_ids, animation_count),
    NMO_FIELD(nmo_keyedanimation_state_t, has_merge, CKPGUID_UINT8),
    NMO_FIELD(nmo_keyedanimation_state_t, merged, CKPGUID_INT),
    NMO_FIELD(nmo_keyedanimation_state_t, merge_factor, CKPGUID_FLOAT),
    NMO_FIELD(nmo_keyedanimation_state_t, subanim_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY_COUNTED(nmo_keyedanimation_state_t, subanims, subanim_count, 1, NMO_GUID_STRUCT_CKKEYEDANIMATIONSUBANIM)
};

static const nmo_type_field_t nmo_objectanimation_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_objectanimation_state_t, base),
                    sizeof(nmo_sceneobject_state_t), CKPGUID_SCENEOBJECT,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_objectanimation_state_t, format, NMO_GUID_ENUM_CK_OBJECTANIMATION_FORMAT),
    NMO_FIELD(nmo_objectanimation_state_t, root_pos, CKPGUID_VECTOR),
    NMO_FIELD(nmo_objectanimation_state_t, has_root_pos, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, flags, CKPGUID_UINT32),
    NMO_FIELD_REF(nmo_objectanimation_state_t, entity),
    NMO_FIELD(nmo_objectanimation_state_t, has_length, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, length, CKPGUID_FLOAT),
    NMO_FIELD(nmo_objectanimation_state_t, has_merge, CKPGUID_UINT8),
    NMO_FIELD(nmo_objectanimation_state_t, merge_factor, CKPGUID_FLOAT),
    NMO_FIELD_REF(nmo_objectanimation_state_t, anim1),
    NMO_FIELD_REF(nmo_objectanimation_state_t, anim2),
    NMO_FIELD(nmo_objectanimation_state_t, has_shared_anim, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_objectanimation_state_t, shared_anim),
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
    NMO_FIELD_ARRAY_COUNTED_FLAGS(nmo_objectanimation_state_t, raw_tail, raw_tail_size, 1,
                                  CKPGUID_UINT8, NMO_FIELD_OPTIONAL, 0),
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

static nmo_status_t read_ref_array(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ref_t **out_refs,
    uint32_t *out_count)
{
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(
        chunk, &count);
    if (result != NMO_OK) return result;
    if (count > UINT32_MAX || count > SIZE_MAX / sizeof(nmo_ref_t)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Animation reference count exceeds limits");
    }
    if (count > nmo_animation_identifier_remaining_dwords(chunk)) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Animation reference count exceeds identifier payload");
    }
    nmo_ref_t *refs = NULL;
    if (count > 0) {
        refs = (nmo_ref_t *)nmo_arena_alloc(
            arena, count * sizeof(nmo_ref_t), _Alignof(nmo_ref_t));
        if (!refs) {
            NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                             "Failed to allocate animation references");
        }
        for (size_t i = 0; i < count; ++i) {
            result = nmo_ref_read(chunk, &refs[i]);
            if (result != NMO_OK) return result;
        }
    }
    *out_refs = refs;
    *out_count = (uint32_t)count;
    NMO_RETURN_OK();
}

static void nmo_objectanimation_check_refs(
    nmo_objectanimation_state_t *state,
    void *context)
{
    const nmo_object_repository_t *repository =
        (const nmo_object_repository_t *)
            nmo_deserialize_context_get_repository(context);
    const nmo_type_registry_t *types =
        nmo_deserialize_context_get_type_registry(context);
    nmo_ref_check_class(&state->entity, repository, types, NMO_CID_3DENTITY);
    nmo_ref_check_class(&state->anim1, repository, types, NMO_CID_OBJECTANIMATION);
    nmo_ref_check_class(&state->anim2, repository, types, NMO_CID_OBJECTANIMATION);
    nmo_ref_check_class(
        &state->shared_anim, repository, types, NMO_CID_OBJECTANIMATION);
}

static nmo_status_t nmo_animation_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    (void)arena;
    if (src == NULL || dst == NULL) return NMO_ERR_INVALID_ARGUMENT;
    if (src != dst) *(nmo_animation_state_t *)dst =
        *(const nmo_animation_state_t *)src;
    return NMO_OK;
}

static nmo_status_t nmo_animation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_animation_state_t *state = instance;
    return nmo_sceneobject_vtable.validate(&state->base, NULL, context);
}

static nmo_status_t nmo_keyedanimation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static nmo_status_t nmo_keyedanimation_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    if (src == NULL || dst == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_keyedanimation_state_t *s = src;
    nmo_keyedanimation_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_keyedanimation_validate(s, NULL, NULL));
    if (src == dst) return NMO_OK;

    nmo_keyedanimation_state_t copied = *s;
    copied.animation_ids = NULL;
    copied.subanims = NULL;
    nmo_status_t result = nmo_object_copy_array(
        arena, (void **)&copied.animation_ids,
        s->animation_ids, sizeof(nmo_ref_t), s->animation_count);
    if (result != NMO_OK) return result;
    if (s->subanim_count > 0) {
        result = nmo_object_copy_array(
            arena, (void **)&copied.subanims,
            s->subanims, sizeof(nmo_keyedanimation_subanim_t),
            s->subanim_count);
        if (result != NMO_OK) return result;
        for (uint32_t i = 0; i < s->subanim_count; ++i) {
            nmo_chunk_t *clone = NULL;
            result = nmo_object_copy_chunk(
                arena, &clone, s->subanims[i].chunk);
            if (result != NMO_OK) return result;
            copied.subanims[i].chunk = clone;
        }
    }
    *d = copied;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_keyedanimation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_keyedanimation_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->animation_ids, s->animation_count, "animation_ids");
    NMO_VALIDATE_COUNT(s->subanims, s->subanim_count, "subanims");
    return nmo_animation_vtable.validate(&s->base, NULL, context);
}

static nmo_status_t nmo_keyedanimation_enumerate_refs(
    const void *instance,
    const nmo_type_descriptor_t *type,
    nmo_type_ref_visitor_fn visitor,
    void *user_data)
{
    (void)type;
    const nmo_keyedanimation_state_t *state = instance;
    if (!state || !visitor) return NMO_OK;
    NMO_RETURN_IF_ERROR(nmo_keyedanimation_validate(state, NULL, NULL));
    for (uint32_t i = 0; i < state->animation_count; ++i) {
        const nmo_object_id_t id = nmo_ref_runtime_id(
            &state->animation_ids[i]);
        if (id != NMO_OBJECT_ID_NONE &&
            !visitor(user_data, id, 0, "animation_ids", i)) {
            return NMO_OK;
        }
    }
    for (uint32_t i = 0; i < state->subanim_count; ++i) {
        const nmo_object_id_t id = nmo_ref_runtime_id(
            &state->subanims[i].ref);
        if (id != NMO_OBJECT_ID_NONE &&
            !visitor(user_data, id, 0, "subanims.ref", i)) {
            return NMO_OK;
        }
    }
    return NMO_OK;
}

static nmo_status_t nmo_objectanimation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static nmo_status_t nmo_objectanimation_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    if (src == NULL || dst == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_objectanimation_state_t *s = src;
    nmo_objectanimation_state_t *d = dst;
    NMO_RETURN_IF_ERROR(nmo_objectanimation_validate(s, NULL, NULL));
    if (src == dst) return NMO_OK;

    nmo_objectanimation_state_t copied = *s;
    copied.controllers = NULL;
    copied.morph_keys = NULL;
    copied.morph_normals_sizes = NULL;
    copied.morph_normals_data = NULL;
    copied.raw_tail = NULL;

    /* Deep copy controllers */
    nmo_status_t result = nmo_object_copy_array(
        arena, (void **)&copied.controllers,
        s->controllers, sizeof(nmo_objanim_controller_t),
        s->controller_count);
    if (result != NMO_OK) return result;
    if (s->controller_count > 0) {
        for (uint32_t i = 0; i < s->controller_count; ++i) {
            copied.controllers[i].data = NULL;
            result = nmo_object_copy_bytes(
                arena, &copied.controllers[i].data,
                s->controllers[i].data, s->controllers[i].data_size);
            if (result != NMO_OK) return result;
        }
    }

    /* Deep copy morph keys */
    result = nmo_object_copy_array(
        arena, (void **)&copied.morph_keys,
        s->morph_keys, sizeof(nmo_objanim_morph_key_t),
        s->morph_key_parsed_count);
    if (result != NMO_OK) return result;
    if (s->morph_key_parsed_count > 0) {
        for (uint32_t i = 0; i < s->morph_key_parsed_count; ++i) {
            copied.morph_keys[i].data = NULL;
            result = nmo_object_copy_bytes(
                arena, &copied.morph_keys[i].data,
                s->morph_keys[i].data, s->morph_keys[i].data_size);
            if (result != NMO_OK) return result;
        }
    }

    /* Deep copy morph normals (both arrays must be present together) */
    result = nmo_object_copy_array(
        arena, (void **)&copied.morph_normals_sizes,
        s->morph_normals_sizes, sizeof(uint32_t),
        s->morph_normals_count);
    if (result != NMO_OK) return result;
    result = nmo_object_copy_array(
        arena, (void **)&copied.morph_normals_data,
        s->morph_normals_data, sizeof(void *), s->morph_normals_count);
    if (result != NMO_OK) return result;
    if (s->morph_normals_count > 0) {
        for (uint32_t i = 0; i < s->morph_normals_count; ++i) {
            copied.morph_normals_data[i] = NULL;
            result = nmo_object_copy_bytes(
                arena, &copied.morph_normals_data[i],
                s->morph_normals_data[i], s->morph_normals_sizes[i]);
            if (result != NMO_OK) return result;
        }
    }

    result = nmo_object_copy_bytes(
        arena, (void **)&copied.raw_tail,
        s->raw_tail, s->raw_tail_size);
    if (result != NMO_OK) return result;
    *d = copied;
    return NMO_OK;
}

static nmo_status_t nmo_objectanimation_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_objectanimation_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->controllers, s->controller_count, "controllers");
    NMO_VALIDATE_COUNT(s->morph_keys, s->morph_key_parsed_count, "morph_keys");
    NMO_VALIDATE_COUNT(s->morph_normals_sizes, s->morph_normals_count, "morph_normals_sizes");
    NMO_VALIDATE_COUNT(s->morph_normals_data, s->morph_normals_count, "morph_normals_data");
    NMO_VALIDATE_BYTES(s->raw_tail, s->raw_tail_size, "raw_tail");
    size_t allocation_size = 0;
    if (!nmo_safe_mul_size(
            s->controller_count, sizeof(nmo_objanim_controller_t),
            &allocation_size) ||
        !nmo_safe_mul_size(
            s->morph_key_parsed_count,
            sizeof(nmo_objanim_morph_key_t), &allocation_size) ||
        !nmo_safe_mul_size(
            s->morph_normals_count, sizeof(uint32_t), &allocation_size) ||
        !nmo_safe_mul_size(
            s->morph_normals_count, sizeof(void *), &allocation_size)) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    for (uint32_t i = 0; i < s->controller_count; ++i) {
        NMO_VALIDATE_BYTES(
            s->controllers[i].data,
            s->controllers[i].data_size,
            "controller data");
    }
    for (uint32_t i = 0; i < s->morph_key_parsed_count; ++i) {
        NMO_VALIDATE_BYTES(
            s->morph_keys[i].data,
            s->morph_keys[i].data_size,
            "morph key data");
    }
    for (uint32_t i = 0; i < s->morph_normals_count; ++i) {
        NMO_VALIDATE_BYTES(
            s->morph_normals_data[i],
            s->morph_normals_sizes[i],
            "morph normal data");
    }
    return nmo_sceneobject_vtable.validate(&s->base, NULL, context);
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

    NMO_RETURN_IF_ERROR(nmo_sceneobject_remap_dependencies(&state->base, NULL, context));

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

    NMO_RETURN_IF_ERROR(nmo_animation_remap_dependencies(&state->base, NULL, context));

    if (state->animation_count > 0 && state->animation_ids == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "KeyedAnimation animation_ids missing");
    }
    if (state->subanim_count > 0 && state->subanims == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "KeyedAnimation subanims missing");
    }

    /* Keep unresolved, duplicate, and null entries in their original lanes.
     * Explicit normalization owns any destructive repair. */
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

    NMO_RETURN_IF_ERROR(nmo_sceneobject_remap_dependencies(&state->base, NULL, context));

    /* Preserve serialized values during dependency resolution. */
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

static bool nmo_animation_ref_equals(
    const nmo_ref_t *lhs,
    const nmo_ref_t *rhs)
{
    return lhs->raw_id == rhs->raw_id &&
        lhs->id == rhs->id &&
        lhs->state == rhs->state;
}

static bool nmo_animation_float_equals(float lhs, float rhs)
{
    return memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
}

static bool nmo_animation_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_animation_state_t *lhs = a;
    const nmo_animation_state_t *rhs = b;
    return nmo_sceneobject_vtable.equals(&lhs->base, &rhs->base) &&
        lhs->has_data == rhs->has_data &&
        lhs->flags == rhs->flags &&
        nmo_animation_float_equals(lhs->frame_rate, rhs->frame_rate) &&
        lhs->has_length == rhs->has_length &&
        nmo_animation_float_equals(lhs->length, rhs->length) &&
        lhs->has_root_entity == rhs->has_root_entity &&
        nmo_animation_ref_equals(&lhs->root_entity, &rhs->root_entity) &&
        lhs->has_character == rhs->has_character &&
        nmo_animation_ref_equals(&lhs->character, &rhs->character) &&
        lhs->has_current_step == rhs->has_current_step &&
        nmo_animation_float_equals(lhs->current_step, rhs->current_step);
}

static uint32_t nmo_animation_hash_bytes(
    uint32_t hash,
    const void *data,
    size_t size)
{
    const uint8_t *bytes = data;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t nmo_animation_hash_ref(
    uint32_t hash,
    const nmo_ref_t *ref)
{
    hash = nmo_animation_hash_bytes(
        hash, &ref->raw_id, sizeof(ref->raw_id));
    hash = nmo_animation_hash_bytes(hash, &ref->id, sizeof(ref->id));
    return nmo_animation_hash_bytes(
        hash, &ref->state, sizeof(ref->state));
}

static uint32_t nmo_animation_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_animation_state_t *state = instance;
    uint32_t hash = nmo_sceneobject_vtable.hash(&state->base);
#define NMO_ANIMATION_HASH_FIELD(field) \
    hash = nmo_animation_hash_bytes( \
        hash, &state->field, sizeof(state->field))
    NMO_ANIMATION_HASH_FIELD(has_data);
    NMO_ANIMATION_HASH_FIELD(flags);
    NMO_ANIMATION_HASH_FIELD(frame_rate);
    NMO_ANIMATION_HASH_FIELD(has_length);
    NMO_ANIMATION_HASH_FIELD(length);
    NMO_ANIMATION_HASH_FIELD(has_root_entity);
    hash = nmo_animation_hash_ref(hash, &state->root_entity);
    NMO_ANIMATION_HASH_FIELD(has_character);
    hash = nmo_animation_hash_ref(hash, &state->character);
    NMO_ANIMATION_HASH_FIELD(has_current_step);
    NMO_ANIMATION_HASH_FIELD(current_step);
#undef NMO_ANIMATION_HASH_FIELD
    return hash;
}

static bool nmo_animation_chunk_array_equals(
    const nmo_arena_array_t *lhs,
    const nmo_arena_array_t *rhs)
{
    if (lhs->count != rhs->count) return false;
    if (lhs->count == 0) return true;
    if (lhs->data == NULL || rhs->data == NULL ||
        lhs->element_size != sizeof(uint32_t) ||
        rhs->element_size != sizeof(uint32_t) ||
        lhs->count > lhs->capacity || rhs->count > rhs->capacity) {
        return false;
    }
    return memcmp(
        lhs->data, rhs->data,
        lhs->count * sizeof(uint32_t)) == 0;
}

static bool nmo_animation_chunk_equals(
    const nmo_chunk_t *lhs,
    const nmo_chunk_t *rhs)
{
    if (lhs == rhs) return true;
    if (lhs == NULL || rhs == NULL) return false;
    return lhs->class_id == rhs->class_id &&
        lhs->data_version == rhs->data_version &&
        lhs->chunk_version == rhs->chunk_version &&
        ((lhs->chunk_options ^ rhs->chunk_options) &
         NMO_CHUNK_OPTION_FILE) == 0 &&
        nmo_animation_chunk_array_equals(&lhs->data, &rhs->data) &&
        nmo_animation_chunk_array_equals(&lhs->ids, &rhs->ids) &&
        nmo_animation_chunk_array_equals(
            &lhs->chunk_refs, &rhs->chunk_refs) &&
        nmo_animation_chunk_array_equals(&lhs->managers, &rhs->managers);
}

static bool nmo_keyedanimation_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_keyedanimation_state_t *lhs = a;
    const nmo_keyedanimation_state_t *rhs = b;
    if (nmo_keyedanimation_validate(lhs, NULL, NULL) != NMO_OK ||
        nmo_keyedanimation_validate(rhs, NULL, NULL) != NMO_OK ||
        !nmo_animation_vtable.equals(&lhs->base, &rhs->base) ||
        lhs->animation_count != rhs->animation_count ||
        lhs->has_merge != rhs->has_merge ||
        lhs->merged != rhs->merged ||
        !nmo_animation_float_equals(
            lhs->merge_factor, rhs->merge_factor) ||
        lhs->subanim_count != rhs->subanim_count) {
        return false;
    }
    for (uint32_t i = 0; i < lhs->animation_count; ++i) {
        if (!nmo_animation_ref_equals(
                &lhs->animation_ids[i], &rhs->animation_ids[i])) {
            return false;
        }
    }
    for (uint32_t i = 0; i < lhs->subanim_count; ++i) {
        if (!nmo_animation_ref_equals(
                &lhs->subanims[i].ref, &rhs->subanims[i].ref) ||
            !nmo_animation_chunk_equals(
                lhs->subanims[i].chunk, rhs->subanims[i].chunk)) {
            return false;
        }
    }
    return true;
}

static uint32_t nmo_animation_hash_chunk_array(
    uint32_t hash,
    const nmo_arena_array_t *array)
{
    hash = nmo_animation_hash_bytes(
        hash, &array->count, sizeof(array->count));
    if (array->count == 0 || array->data == NULL ||
        array->element_size != sizeof(uint32_t) ||
        array->count > array->capacity) {
        return hash;
    }
    return nmo_animation_hash_bytes(
        hash, array->data, array->count * sizeof(uint32_t));
}

static uint32_t nmo_animation_hash_chunk(
    uint32_t hash,
    const nmo_chunk_t *chunk)
{
    const uint8_t present = chunk != NULL;
    hash = nmo_animation_hash_bytes(hash, &present, sizeof(present));
    if (chunk == NULL) return hash;
    const uint8_t is_file =
        (chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0;
    hash = nmo_animation_hash_bytes(
        hash, &chunk->class_id, sizeof(chunk->class_id));
    hash = nmo_animation_hash_bytes(
        hash, &chunk->data_version, sizeof(chunk->data_version));
    hash = nmo_animation_hash_bytes(
        hash, &chunk->chunk_version, sizeof(chunk->chunk_version));
    hash = nmo_animation_hash_bytes(hash, &is_file, sizeof(is_file));
    hash = nmo_animation_hash_chunk_array(hash, &chunk->data);
    hash = nmo_animation_hash_chunk_array(hash, &chunk->ids);
    hash = nmo_animation_hash_chunk_array(hash, &chunk->chunk_refs);
    return nmo_animation_hash_chunk_array(hash, &chunk->managers);
}

static uint32_t nmo_keyedanimation_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_keyedanimation_state_t *state = instance;
    if (nmo_keyedanimation_validate(state, NULL, NULL) != NMO_OK) return 0;
    uint32_t hash = nmo_animation_vtable.hash(&state->base);
    hash = nmo_animation_hash_bytes(
        hash, &state->animation_count, sizeof(state->animation_count));
    for (uint32_t i = 0; i < state->animation_count; ++i) {
        hash = nmo_animation_hash_ref(hash, &state->animation_ids[i]);
    }
    hash = nmo_animation_hash_bytes(
        hash, &state->has_merge, sizeof(state->has_merge));
    hash = nmo_animation_hash_bytes(
        hash, &state->merged, sizeof(state->merged));
    hash = nmo_animation_hash_bytes(
        hash, &state->merge_factor, sizeof(state->merge_factor));
    hash = nmo_animation_hash_bytes(
        hash, &state->subanim_count, sizeof(state->subanim_count));
    for (uint32_t i = 0; i < state->subanim_count; ++i) {
        hash = nmo_animation_hash_ref(hash, &state->subanims[i].ref);
        hash = nmo_animation_hash_chunk(
            hash, state->subanims[i].chunk);
    }
    return hash;
}

static bool nmo_animation_buffer_equals(
    const void *lhs,
    const void *rhs,
    size_t size)
{
    if (size == 0) return true;
    return lhs != NULL && rhs != NULL && memcmp(lhs, rhs, size) == 0;
}

static bool nmo_objectanimation_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_objectanimation_state_t *lhs = a;
    const nmo_objectanimation_state_t *rhs = b;
    if (nmo_objectanimation_validate(lhs, NULL, NULL) != NMO_OK ||
        nmo_objectanimation_validate(rhs, NULL, NULL) != NMO_OK ||
        !nmo_sceneobject_vtable.equals(&lhs->base, &rhs->base) ||
        lhs->format != rhs->format ||
        !nmo_animation_float_equals(lhs->root_pos.x, rhs->root_pos.x) ||
        !nmo_animation_float_equals(lhs->root_pos.y, rhs->root_pos.y) ||
        !nmo_animation_float_equals(lhs->root_pos.z, rhs->root_pos.z) ||
        lhs->has_root_pos != rhs->has_root_pos ||
        lhs->flags != rhs->flags ||
        !nmo_animation_ref_equals(&lhs->entity, &rhs->entity) ||
        lhs->has_length != rhs->has_length ||
        !nmo_animation_float_equals(lhs->length, rhs->length) ||
        lhs->has_merge != rhs->has_merge ||
        !nmo_animation_float_equals(
            lhs->merge_factor, rhs->merge_factor) ||
        !nmo_animation_ref_equals(&lhs->anim1, &rhs->anim1) ||
        !nmo_animation_ref_equals(&lhs->anim2, &rhs->anim2) ||
        lhs->has_shared_anim != rhs->has_shared_anim ||
        !nmo_animation_ref_equals(
            &lhs->shared_anim, &rhs->shared_anim) ||
        lhs->has_morph_counts != rhs->has_morph_counts ||
        lhs->morph_vertex_count != rhs->morph_vertex_count ||
        lhs->morph_key_count != rhs->morph_key_count ||
        lhs->controller_count != rhs->controller_count ||
        lhs->morph_key_parsed_count != rhs->morph_key_parsed_count ||
        lhs->morph_normals_id != rhs->morph_normals_id ||
        lhs->morph_normals_count != rhs->morph_normals_count ||
        lhs->raw_tail_size != rhs->raw_tail_size) {
        return false;
    }
    for (uint32_t i = 0; i < lhs->controller_count; ++i) {
        const nmo_objanim_controller_t *lhs_controller =
            &lhs->controllers[i];
        const nmo_objanim_controller_t *rhs_controller =
            &rhs->controllers[i];
        if (lhs_controller->type != rhs_controller->type ||
            lhs_controller->key_count != rhs_controller->key_count ||
            lhs_controller->data_size != rhs_controller->data_size ||
            !nmo_animation_buffer_equals(
                lhs_controller->data, rhs_controller->data,
                lhs_controller->data_size)) {
            return false;
        }
    }
    for (uint32_t i = 0; i < lhs->morph_key_parsed_count; ++i) {
        const nmo_objanim_morph_key_t *lhs_key = &lhs->morph_keys[i];
        const nmo_objanim_morph_key_t *rhs_key = &rhs->morph_keys[i];
        if (!nmo_animation_float_equals(
                lhs_key->time_step, rhs_key->time_step) ||
            lhs_key->data_size != rhs_key->data_size ||
            !nmo_animation_buffer_equals(
                lhs_key->data, rhs_key->data, lhs_key->data_size)) {
            return false;
        }
    }
    for (uint32_t i = 0; i < lhs->morph_normals_count; ++i) {
        if (lhs->morph_normals_sizes[i] != rhs->morph_normals_sizes[i] ||
            !nmo_animation_buffer_equals(
                lhs->morph_normals_data[i], rhs->morph_normals_data[i],
                lhs->morph_normals_sizes[i])) {
            return false;
        }
    }
    return nmo_animation_buffer_equals(
        lhs->raw_tail, rhs->raw_tail, lhs->raw_tail_size);
}

static uint32_t nmo_objectanimation_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_objectanimation_state_t *state = instance;
    if (nmo_objectanimation_validate(state, NULL, NULL) != NMO_OK) return 0;
    uint32_t hash = nmo_sceneobject_vtable.hash(&state->base);
#define NMO_OBJECTANIMATION_HASH_FIELD(field) \
    hash = nmo_animation_hash_bytes( \
        hash, &state->field, sizeof(state->field))
    NMO_OBJECTANIMATION_HASH_FIELD(format);
    NMO_OBJECTANIMATION_HASH_FIELD(root_pos.x);
    NMO_OBJECTANIMATION_HASH_FIELD(root_pos.y);
    NMO_OBJECTANIMATION_HASH_FIELD(root_pos.z);
    NMO_OBJECTANIMATION_HASH_FIELD(has_root_pos);
    NMO_OBJECTANIMATION_HASH_FIELD(flags);
    hash = nmo_animation_hash_ref(hash, &state->entity);
    NMO_OBJECTANIMATION_HASH_FIELD(has_length);
    NMO_OBJECTANIMATION_HASH_FIELD(length);
    NMO_OBJECTANIMATION_HASH_FIELD(has_merge);
    NMO_OBJECTANIMATION_HASH_FIELD(merge_factor);
    hash = nmo_animation_hash_ref(hash, &state->anim1);
    hash = nmo_animation_hash_ref(hash, &state->anim2);
    NMO_OBJECTANIMATION_HASH_FIELD(has_shared_anim);
    hash = nmo_animation_hash_ref(hash, &state->shared_anim);
    NMO_OBJECTANIMATION_HASH_FIELD(has_morph_counts);
    NMO_OBJECTANIMATION_HASH_FIELD(morph_vertex_count);
    NMO_OBJECTANIMATION_HASH_FIELD(morph_key_count);
    NMO_OBJECTANIMATION_HASH_FIELD(controller_count);
    for (uint32_t i = 0; i < state->controller_count; ++i) {
        const nmo_objanim_controller_t *controller = &state->controllers[i];
        hash = nmo_animation_hash_bytes(
            hash, &controller->type, sizeof(controller->type));
        hash = nmo_animation_hash_bytes(
            hash, &controller->key_count, sizeof(controller->key_count));
        hash = nmo_animation_hash_bytes(
            hash, &controller->data_size, sizeof(controller->data_size));
        hash = nmo_animation_hash_bytes(
            hash, controller->data, controller->data_size);
    }
    NMO_OBJECTANIMATION_HASH_FIELD(morph_key_parsed_count);
    for (uint32_t i = 0; i < state->morph_key_parsed_count; ++i) {
        const nmo_objanim_morph_key_t *key = &state->morph_keys[i];
        hash = nmo_animation_hash_bytes(
            hash, &key->time_step, sizeof(key->time_step));
        hash = nmo_animation_hash_bytes(
            hash, &key->data_size, sizeof(key->data_size));
        hash = nmo_animation_hash_bytes(
            hash, key->data, key->data_size);
    }
    NMO_OBJECTANIMATION_HASH_FIELD(morph_normals_id);
    NMO_OBJECTANIMATION_HASH_FIELD(morph_normals_count);
    for (uint32_t i = 0; i < state->morph_normals_count; ++i) {
        hash = nmo_animation_hash_bytes(
            hash, &state->morph_normals_sizes[i],
            sizeof(state->morph_normals_sizes[i]));
        hash = nmo_animation_hash_bytes(
            hash, state->morph_normals_data[i],
            state->morph_normals_sizes[i]);
    }
    NMO_OBJECTANIMATION_HASH_FIELD(raw_tail_size);
    hash = nmo_animation_hash_bytes(
        hash, state->raw_tail, state->raw_tail_size);
#undef NMO_OBJECTANIMATION_HASH_FIELD
    return hash;
}

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
    NMO_OBJECT_VTABLE_EX(
        nmo_keyedanimation_create,
        nmo_keyedanimation_destroy,
        nmo_keyedanimation_serialize,
        nmo_keyedanimation_deserialize,
        nmo_keyedanimation_copy,
        nmo_keyedanimation_validate,
        nmo_keyedanimation_equals,
        nmo_keyedanimation_hash,
        nmo_keyedanimation_enumerate_refs)
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
    CKPGUID_SCENEOBJECT,
    nmo_objectanimation_state_t,
    &nmo_objectanimation_vtable,
    nmo_objectanimation_fields)

static nmo_status_t write_ref_array(
    nmo_chunk_t *chunk,
    const nmo_ref_t *refs,
    uint32_t count)
{
    return nmo_ref_write_sequence(chunk, refs, count);
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

    NMO_RETURN_IF_ERROR(
        nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, remaining_bytes));

    *out_data = data;
    *out_size = remaining_bytes;
    NMO_RETURN_OK();
}

/* Read controllers in CONTROLLERS format: loop of {type(DWORD), size_dwords(DWORD), data[]} until type==0 */
static nmo_status_t nmo_animation_seek_optional(
    nmo_chunk_t *chunk,
    uint32_t identifier,
    bool *out_found)
{
    nmo_status_t result = nmo_chunk_seek_identifier(chunk, identifier);
    if (result == NMO_OK) {
        *out_found = true;
        return NMO_OK;
    }
    *out_found = false;
    return result == NMO_ERR_NOT_FOUND ? NMO_OK : result;
}

static nmo_status_t nmo_animation_validate_payload_size(
    nmo_chunk_t *chunk,
    uint32_t size_bytes)
{
    const size_t required_dwords = ((size_t)size_bytes + 3u) / 4u;
    if (required_dwords > nmo_animation_identifier_remaining_dwords(chunk)) {
        NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                         "Animation payload exceeds identifier section");
    }
    return NMO_OK;
}

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
        if (size_dwords > UINT32_MAX / 4u ||
            (size_t)size_dwords >
                nmo_animation_identifier_remaining_dwords(chunk)) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Controller payload exceeds identifier section");
        }
        uint32_t data_size = size_dwords * 4;

        void *data = NULL;
        if (data_size > 0) {
            data = nmo_arena_alloc(arena, data_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate controller data buffer");
            }
            NMO_RETURN_IF_ERROR(
                nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, data_size));
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
    bool section_found = false;

    /* 1. Read morph keys if present */
    if (out_state->morph_key_count < 0 || out_state->morph_vertex_count < 0) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                         "Invalid morph key counts");
    }
    if (out_state->morph_key_count > 0) {
        int32_t morph_key_count = out_state->morph_key_count;
        if ((size_t)morph_key_count >
                SIZE_MAX / sizeof(nmo_objanim_morph_key_t) ||
            (size_t)morph_key_count >
                nmo_animation_identifier_remaining_dwords(chunk) / 2u) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Morph key count exceeds identifier payload");
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
            NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
                chunk, size_bytes));

            if (size_bytes > 0) {
                void *data = nmo_arena_alloc(arena, size_bytes, 4);
                if (!data) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                     "Failed to allocate morph key data");
                }
                NMO_RETURN_IF_ERROR(
                    nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, size_bytes));
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
        NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
            chunk, buf_size));

        if (key_count > 0 && buf_size > 0) {
            void *data = nmo_arena_alloc(arena, buf_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate controller data");
            }
            NMO_RETURN_IF_ERROR(
                nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, buf_size));

            local_controllers[count].type = controller_types[i];
            local_controllers[count].key_count = key_count;
            local_controllers[count].data_size = buf_size;
            local_controllers[count].data = data;
            count++;
        }
    }

    /* 3. Check for optional morph normals */
    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_OBJANIMMORPHCOMP, &section_found));
    if (section_found && out_state->morph_key_parsed_count > 0) {
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
            NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
                chunk, size_bytes));

            if (size_bytes > 0) {
                void *data = nmo_arena_alloc(arena, size_bytes, 4);
                if (!data) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                     "Failed to allocate morph normal data");
                }
                NMO_RETURN_IF_ERROR(
                    nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, size_bytes));
                data_ptrs[i] = data;
            } else {
                data_ptrs[i] = NULL;
            }
        }

        out_state->morph_normals_sizes = sizes;
        out_state->morph_normals_data = data_ptrs;
    } else {
        NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
            chunk, CK_STATESAVE_OBJANIMMORPHNORMALS, &section_found));
        if (section_found && out_state->morph_key_parsed_count > 0) {
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
            NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
                chunk, size_bytes));

            if (size_bytes > 0) {
                void *data = nmo_arena_alloc(arena, size_bytes, 4);
                if (!data) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                     "Failed to allocate morph normal data");
                }
                NMO_RETURN_IF_ERROR(
                    nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, size_bytes));
                data_ptrs[i] = data;
            } else {
                data_ptrs[i] = NULL;
            }
        }

        out_state->morph_normals_sizes = sizes;
        out_state->morph_normals_data = data_ptrs;
        }
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
    bool section_found = false;

    /* Skip old morphkeys identifier if present */
    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_OBJANIMMORPHKEYS, &section_found));

    /* Read morph keys (legacy format) */
    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_OBJANIMMORPHKEYS2, &section_found));
    if (section_found) {
        int32_t morph_key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &morph_key_count));
        if (morph_key_count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                             "Invalid morph key count");
        }
        if (morph_key_count > 0) {
            int32_t morph_vertex_count = 0;
            NMO_RETURN_IF_ERROR(nmo_chunk_read_int(chunk, &morph_vertex_count));
            if (morph_vertex_count < 0) {
                NMO_RETURN_ERROR(NMO_ERR_INVALID_FORMAT, NMO_SEVERITY_ERROR,
                                 "Invalid morph vertex count");
            }
            if ((size_t)morph_key_count >
                    SIZE_MAX / sizeof(nmo_objanim_morph_key_t) ||
                (size_t)morph_key_count >
                    nmo_animation_identifier_remaining_dwords(chunk) / 2u) {
                NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                                 "Morph key count exceeds identifier payload");
            }

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
                NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
                    chunk, size_bytes));

                if (size_bytes > 0) {
                    void *data = nmo_arena_alloc(arena, size_bytes, 4);
                    if (!data) {
                        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                         "Failed to allocate morph key data");
                    }
                    NMO_RETURN_IF_ERROR(nmo_chunk_read_and_fill_buffer_nosize_checked(
                        chunk, data, size_bytes));
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
    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_OBJANIMPOSKEYS, &section_found));
    if (section_found) {
        uint32_t buf_size = 0;
        uint32_t key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &buf_size));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &key_count));
        NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
            chunk, buf_size));

        if (key_count > 0 && buf_size > 0) {
            void *data = nmo_arena_alloc(arena, buf_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate position controller data");
            }
            NMO_RETURN_IF_ERROR(
                nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, buf_size));

            local_controllers[count].type = CKANIMATION_LINPOS_CONTROL;
            local_controllers[count].key_count = key_count;
            local_controllers[count].data_size = buf_size;
            local_controllers[count].data = data;
            count++;
        }
    }

    /* Read rotation controller + scale axis controller */
    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_OBJANIMROTKEYS, &section_found));
    if (section_found) {
        uint32_t rot_buf_size = 0;
        uint32_t rot_key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &rot_buf_size));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &rot_key_count));
        NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
            chunk, rot_buf_size));

        if (rot_key_count > 0 && rot_buf_size > 0) {
            void *data = nmo_arena_alloc(arena, rot_buf_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate rotation controller data");
            }
            NMO_RETURN_IF_ERROR(
                nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, rot_buf_size));

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
        NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
            chunk, axis_buf_size));

        if (axis_key_count > 0 && axis_buf_size > 0) {
            void *data = nmo_arena_alloc(arena, axis_buf_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate scale axis controller data");
            }
            NMO_RETURN_IF_ERROR(
                nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, axis_buf_size));

            local_controllers[count].type = CKANIMATION_LINSCLAXIS_CONTROL;
            local_controllers[count].key_count = axis_key_count;
            local_controllers[count].data_size = axis_buf_size;
            local_controllers[count].data = data;
            count++;
        }
    }

    /* Read scale controller */
    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_OBJANIMSCLKEYS, &section_found));
    if (section_found) {
        uint32_t buf_size = 0;
        uint32_t key_count = 0;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &buf_size));
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &key_count));
        NMO_RETURN_IF_ERROR(nmo_animation_validate_payload_size(
            chunk, buf_size));

        if (key_count > 0 && buf_size > 0) {
            void *data = nmo_arena_alloc(arena, buf_size, 4);
            if (!data) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                                 "Failed to allocate scale controller data");
            }
            NMO_RETURN_IF_ERROR(
                nmo_chunk_read_and_fill_buffer_nosize_checked(chunk, data, buf_size));

            local_controllers[count].type = CKANIMATION_LINSCL_CONTROL;
            local_controllers[count].key_count = key_count;
            local_controllers[count].data_size = buf_size;
            local_controllers[count].data = data;
            count++;
        }
    }

    /* Read legacy header fields */
    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_OBJANIMFLAGS, &section_found));
    if (section_found) {
        NMO_RETURN_IF_ERROR(nmo_chunk_read_dword(chunk, &out_state->flags));
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_OBJANIMENTITY, &section_found));
    if (section_found) {
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &out_state->entity));
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_OBJANIMLENGTH, &section_found));
    if (section_found) {
        out_state->has_length = 1;
        NMO_RETURN_IF_ERROR(nmo_chunk_read_float(chunk, &out_state->length));
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_OBJANIMMERGE, &section_found));
    if (section_found) {
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
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &out_state->anim1));
        NMO_RETURN_IF_ERROR(nmo_ref_read(chunk, &out_state->anim2));
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_OBJANIMNEWDATA, &section_found));
    if (section_found) {
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
    out_state->root_entity = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->has_character = 0;
    out_state->character = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->has_current_step = 0;
    out_state->current_step = 0.0f;
    bool section_found = false;

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_ANIMATIONDATA, &section_found));
    if (section_found) {
        out_state->has_data = 1;

        size_t remaining_dwords = nmo_animation_identifier_remaining_dwords(chunk);
        if (remaining_dwords == 0) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Animation data section is empty");
        }
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

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_ANIMATIONLENGTH, &section_found));
    if (section_found) {
        out_state->has_length = 1;
        nmo_status_t result = nmo_chunk_read_float(chunk, &out_state->length);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_ANIMATIONBODYPARTS, &section_found));
    if (section_found) {
        out_state->has_root_entity = 1;
        /* Legacy list of body parts (ignored) */
        int32_t count = 0;
        nmo_status_t result = nmo_chunk_read_int(chunk, &count);
        if (result != NMO_OK) return result;
        if (count < 0) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR, "Invalid body part count");
        }
        const size_t remaining_dwords =
            nmo_animation_identifier_remaining_dwords(chunk);
        if (remaining_dwords == 0 ||
            (size_t)count > remaining_dwords - 1u) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Body part count exceeds identifier payload");
        }
        for (int32_t i = 0; i < count; ++i) {
            nmo_object_id_t tmp = 0;
            result = nmo_chunk_read_object_id(chunk, &tmp);
            if (result != NMO_OK) return result;
        }
        nmo_ref_t root_entity = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        result = nmo_ref_read(chunk, &root_entity);
        if (result != NMO_OK) return result;
        nmo_ref_check_class(
            &root_entity,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_3DENTITY);
        out_state->root_entity = root_entity;
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_ANIMATIONCHARACTER, &section_found));
    if (section_found) {
        out_state->has_character = 1;
        nmo_ref_t character = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
        nmo_status_t result = nmo_ref_read(chunk, &character);
        if (result != NMO_OK) return result;
        nmo_ref_check_class(
            &character,
            (const nmo_object_repository_t *)
                nmo_deserialize_context_get_repository(context),
            nmo_deserialize_context_get_type_registry(context),
            NMO_CID_CHARACTER);
        out_state->character = character;
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_ANIMATIONCURRENTSTEP, &section_found));
    if (section_found) {
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
        result = nmo_ref_write(out_chunk, &in_state->root_entity);
        if (result != NMO_OK) return result;
    }

    if (is_file || (save_flags & CK_STATESAVE_ANIMATIONCHARACTER) != 0) {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_ANIMATIONCHARACTER);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->character);
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
    bool section_found = false;

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_KEYEDANIMANIMLIST, &section_found));
    if (section_found) {
        nmo_ref_t *animation_ids = NULL;
        uint32_t animation_count = 0;
        result = read_ref_array(chunk, arena, &animation_ids, &animation_count);
        if (result != NMO_OK) return result;
        const nmo_object_repository_t *repository =
            nmo_deserialize_context_get_repository(context);
        const nmo_type_registry_t *types =
            nmo_deserialize_context_get_type_registry(context);
        for (uint32_t i = 0; i < animation_count; ++i) {
            nmo_ref_check_class(
                &animation_ids[i], repository, types,
                NMO_CID_OBJECTANIMATION);
        }
        out_state->animation_ids = animation_ids;
        out_state->animation_count = animation_count;
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_KEYEDANIMMERGE, &section_found));
    if (section_found) {
        out_state->has_merge = 1;
        result = nmo_chunk_read_int(chunk, &out_state->merged);
        if (result != NMO_OK) return result;
        result = nmo_chunk_read_float(chunk, &out_state->merge_factor);
        if (result != NMO_OK) return result;
    }

    const bool is_file = nmo_animation_is_file_mode_deser(chunk, context);
    if (!is_file) {
        NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
            chunk, CK_STATESAVE_KEYEDANIMSUBANIMS, &section_found));
    }
    if (!is_file && section_found) {
        uint32_t count = 0;
        result = nmo_chunk_read_dword(chunk, &count);
        if (result != NMO_OK) return result;
        if (count > 10000u) {
            NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                             "Subanim count exceeds maximum");
        }
        const size_t remaining_dwords =
            nmo_animation_identifier_remaining_dwords(chunk);
        if ((size_t)count > remaining_dwords / 2u) {
            NMO_RETURN_ERROR(NMO_ERR_TRUNCATED_CHUNK, NMO_SEVERITY_ERROR,
                             "Subanim count exceeds identifier payload");
        }
        if (count > 0) {
            nmo_keyedanimation_subanim_t *subanims =
                (nmo_keyedanimation_subanim_t *)nmo_arena_alloc(
                arena, sizeof(nmo_keyedanimation_subanim_t) * count,
                _Alignof(nmo_keyedanimation_subanim_t));
            if (!subanims) {
                NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate subanim array");
            }

            for (uint32_t i = 0; i < count; ++i) {
                subanims[i].ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
                subanims[i].chunk = NULL;
                result = nmo_ref_read(chunk, &subanims[i].ref);
                if (result != NMO_OK) return result;
                result = nmo_chunk_read_sub_chunk(chunk, &subanims[i].chunk);
                if (result != NMO_OK) return result;
                nmo_ref_check_class(
                    &subanims[i].ref,
                    (const nmo_object_repository_t *)
                        nmo_deserialize_context_get_repository(context),
                    nmo_deserialize_context_get_type_registry(context),
                    NMO_CID_OBJECTANIMATION);
            }
            out_state->subanims = subanims;
            out_state->subanim_count = count;
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
    NMO_RETURN_IF_ERROR(nmo_keyedanimation_validate(in_state, NULL, NULL));

    nmo_status_t result = nmo_animation_serialize_internal(&in_state->base, out_chunk, context);
    if (result != NMO_OK) return result;

    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    const bool is_file = nmo_animation_is_file_mode_ser(out_chunk, context);

    if (is_file || (save_flags & CK_STATESAVE_KEYEDANIMANIMLIST) != 0) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KEYEDANIMANIMLIST);
        if (result != NMO_OK) return result;
        result = write_ref_array(out_chunk, in_state->animation_ids, in_state->animation_count);
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
        const uint32_t count = in_state->subanim_count;
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_KEYEDANIMSUBANIMS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_dword(out_chunk, count);
        if (result != NMO_OK) return result;
        for (uint32_t i = 0; i < count; ++i) {
            result = nmo_ref_write(out_chunk, &in_state->subanims[i].ref);
            if (result != NMO_OK) return result;
            result = nmo_chunk_write_sub_chunk(
                out_chunk, in_state->subanims[i].chunk);
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
    out_state->entity = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->has_length = 0;
    out_state->length = 0.0f;
    out_state->has_merge = 0;
    out_state->merge_factor = 0.5f;
    out_state->anim1 = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->anim2 = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    out_state->has_shared_anim = 0;
    out_state->shared_anim = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
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
    bool section_found = false;

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_OBJANIMSHARED, &section_found));
    if (section_found) {
        out_state->format = CKOBJANIM_FORMAT_SHARED;
        out_state->has_shared_anim = 1;
        nmo_status_t result = nmo_ref_read(chunk, &out_state->shared_anim);
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
        result = nmo_ref_read(chunk, &out_state->entity);
        if (result != NMO_OK) return result;
        if (out_state->flags & 0x80u) {
            out_state->has_merge = 1;
            result = nmo_chunk_read_float(chunk, &out_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_ref_read(chunk, &out_state->anim1);
            if (result != NMO_OK) return result;
            result = nmo_ref_read(chunk, &out_state->anim2);
            if (result != NMO_OK) return result;
        }
        /* SHARED format has no controller data, keep raw_tail for any remainder */
        result = read_raw_tail(
            chunk, arena, (void **)&out_state->raw_tail, &out_state->raw_tail_size);
        if (result != NMO_OK) return result;
        nmo_objectanimation_check_refs(out_state, context);
        NMO_RETURN_OK();
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_OBJANIMCONTROLLERS, &section_found));
    if (section_found) {
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
        result = nmo_ref_read(chunk, &out_state->entity);
        if (result != NMO_OK) return result;
        out_state->has_length = 1;
        result = nmo_chunk_read_float(chunk, &out_state->length);
        if (result != NMO_OK) return result;
        if (out_state->flags & 0x80u) {
            out_state->has_merge = 1;
            result = nmo_chunk_read_float(chunk, &out_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_ref_read(chunk, &out_state->anim1);
            if (result != NMO_OK) return result;
            result = nmo_ref_read(chunk, &out_state->anim2);
            if (result != NMO_OK) return result;
        }
        /* CONTROLLERS format: parse controller loop */
        result = read_controllers_loop(chunk, arena, out_state);
        if (result != NMO_OK) return result;
        nmo_objectanimation_check_refs(out_state, context);
        NMO_RETURN_OK();
    }

    NMO_RETURN_IF_ERROR(nmo_animation_seek_optional(
        chunk, CK_STATESAVE_OBJANIMNEWDATA, &section_found));
    if (section_found) {
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
        result = nmo_ref_read(chunk, &out_state->entity);
        if (result != NMO_OK) return result;
        out_state->has_length = 1;
        result = nmo_chunk_read_float(chunk, &out_state->length);
        if (result != NMO_OK) return result;
        if (out_state->flags & 0x80u) {
            out_state->has_merge = 1;
            result = nmo_chunk_read_float(chunk, &out_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_ref_read(chunk, &out_state->anim1);
            if (result != NMO_OK) return result;
            result = nmo_ref_read(chunk, &out_state->anim2);
            if (result != NMO_OK) return result;
        }
        /* NEWDATA format: parse morph keys + 4 inline controllers */
        result = read_newdata_controllers(chunk, arena, out_state);
        if (result != NMO_OK) return result;
        nmo_objectanimation_check_refs(out_state, context);
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
            nmo_status_t result = read_raw_tail(
                chunk, arena, (void **)&out_state->raw_tail, &out_state->raw_tail_size);
            if (result != NMO_OK) return result;
        }
    }

    nmo_objectanimation_check_refs(out_state, context);
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
        result = nmo_ref_write(out_chunk, &in_state->shared_anim);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_vector3(out_chunk, &in_state->root_pos);
        if (result != NMO_OK) return result;
        for (int i = 0; i < 4; ++i) {
            result = nmo_chunk_write_float(out_chunk, 0.0f);
            if (result != NMO_OK) return result;
        }
        result = nmo_chunk_write_dword(out_chunk, in_state->flags);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->entity);
        if (result != NMO_OK) return result;
        if ((in_state->flags & 0x80u) != 0) {
            result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->anim1);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->anim2);
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
        result = nmo_ref_write(out_chunk, &in_state->entity);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->length);
        if (result != NMO_OK) return result;
        if ((in_state->flags & 0x80u) != 0) {
            result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->anim1);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->anim2);
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
        result = nmo_ref_write(out_chunk, &in_state->entity);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_float(out_chunk, in_state->length);
        if (result != NMO_OK) return result;
        if ((in_state->flags & 0x80u) != 0) {
            result = nmo_chunk_write_float(out_chunk, in_state->merge_factor);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->anim1);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->anim2);
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

        if (nmo_ref_serialized_id(&in_state->entity) != NMO_OBJECT_ID_NONE) {
            nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_OBJANIMENTITY);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->entity);
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
            result = nmo_ref_write(out_chunk, &in_state->anim1);
            if (result != NMO_OK) return result;
            result = nmo_ref_write(out_chunk, &in_state->anim2);
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
    if (!out_state || !chunk) return NMO_ERR_INVALID_ARGUMENT;
    nmo_animation_state_t decoded;
    nmo_status_t result = nmo_animation_create(&decoded, NULL, context);
    if (result != NMO_OK) return result;
    result = nmo_animation_deserialize_internal(chunk, context, &decoded);
    if (result != NMO_OK) {
        nmo_animation_destroy(&decoded, NULL, context);
        return result;
    }
    nmo_animation_destroy(out_state, NULL, context);
    *out_state = decoded;
    return NMO_OK;
}

nmo_status_t nmo_animation_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_animation_state_t *in_state = (const nmo_animation_state_t *)instance;
    if (!in_state || !out_chunk || !out_chunk->arena) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (!staged) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_animation_serialize_internal(
        in_state, staged, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

nmo_status_t nmo_keyedanimation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_keyedanimation_state_t *out_state = (nmo_keyedanimation_state_t *)instance;
    if (!out_state || !chunk) return NMO_ERR_INVALID_ARGUMENT;
    nmo_keyedanimation_state_t decoded;
    nmo_status_t result = nmo_keyedanimation_deserialize_internal(
        chunk, context, &decoded);
    if (result != NMO_OK) {
        nmo_keyedanimation_destroy(&decoded, NULL, context);
        return result;
    }
    nmo_keyedanimation_destroy(out_state, NULL, context);
    *out_state = decoded;
    return NMO_OK;
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
    if (!in_state || !out_chunk || !out_chunk->arena) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (!staged) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_keyedanimation_serialize_internal(
        in_state, staged, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}

nmo_status_t nmo_objectanimation_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_objectanimation_state_t *out_state = (nmo_objectanimation_state_t *)instance;
    if (!out_state || !chunk) return NMO_ERR_INVALID_ARGUMENT;
    nmo_objectanimation_state_t decoded;
    nmo_status_t result = nmo_objectanimation_deserialize_internal(
        chunk, context, &decoded);
    if (result != NMO_OK) {
        nmo_objectanimation_destroy(&decoded, NULL, context);
        return result;
    }
    nmo_objectanimation_destroy(out_state, NULL, context);
    *out_state = decoded;
    return NMO_OK;
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
    if (!in_state || !out_chunk || !out_chunk->arena) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t result = nmo_objectanimation_validate(in_state, NULL, NULL);
    if (result != NMO_OK) return result;
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (!staged) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    result = nmo_objectanimation_serialize_internal(
        in_state, staged, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    return NMO_OK;
}
