/**
 * @file ckcharacter_schemas.c
 * @brief CKCharacter and CKBodyPart schema implementation
 */

#include "object/builtin/nmo_character_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "type/nmo_reflection.h"
#include "object/nmo_object_struct_guids.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include "object/nmo_object_repository.h"
#include <string.h>

static void character_part_dispose(void *element, void *user_data)
{
    (void)user_data;
    nmo_character_part_t *part = (nmo_character_part_t *)element;
    if (part != NULL && part->chunk != NULL) {
        nmo_chunk_destroy(part->chunk);
        part->chunk = NULL;
    }
}

static void character_parts_set_lifecycle(nmo_array_t *parts)
{
    nmo_container_lifecycle_t lifecycle = NMO_CONTAINER_LIFECYCLE_INIT;
    lifecycle.dispose = character_part_dispose;
    nmo_array_set_lifecycle(parts, &lifecycle);
}

NMO_DEFINE_OBJECT_LIFECYCLE(
    character,
    nmo_character_state_t,
    do {
        nmo_status_t result = nmo_3dentity_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
        result = nmo_array_init(
            &state->body_parts, sizeof(nmo_character_part_t), 0, NULL);
        if (result != NMO_OK) {
            nmo_3dentity_vtable.destroy(&state->base, NULL, context);
            return result;
        }
        character_parts_set_lifecycle(&state->body_parts);
        result = nmo_array_init(
            &state->animations, sizeof(nmo_ref_t), 0, NULL);
        if (result != NMO_OK) {
            nmo_array_dispose(&state->body_parts);
            nmo_3dentity_vtable.destroy(&state->base, NULL, context);
            return result;
        }
    } while (0),
    do {
        nmo_array_dispose(&state->body_parts);
        nmo_array_dispose(&state->animations);
        nmo_3dentity_vtable.destroy(&state->base, NULL, context);
    } while (0))
NMO_DEFINE_OBJECT_LIFECYCLE(
    bodypart,
    nmo_bodypart_state_t,
    do {
        nmo_status_t result = nmo_3dobject_vtable.create(
            &state->base, NULL, context);
        if (result != NMO_OK) return result;
    } while (0),
    nmo_3dobject_vtable.destroy(&state->base, NULL, context))

static void character_copy_base_allocators(
    nmo_3dentity_state_t *destination,
    const nmo_3dentity_state_t *source)
{
    nmo_beobject_state_t *destination_base = &destination->base.base;
    const nmo_beobject_state_t *source_base = &source->base.base;
    destination_base->scripts.allocator = source_base->scripts.allocator;
    destination_base->attributes.allocator = source_base->attributes.allocator;
    destination_base->legacy_attributes.allocator =
        source_base->legacy_attributes.allocator;
}

static nmo_status_t nmo_character_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context);

static nmo_status_t read_exact_sized_buffer(
    nmo_chunk_t *chunk,
    void *buffer,
    size_t expected_size)
{
    size_t actual_size = 0;
    nmo_status_t result = nmo_chunk_read_and_fill_buffer_checked(
        chunk, buffer, expected_size, &actual_size);
    if (result != NMO_OK) return result;
    return actual_size == expected_size ? NMO_OK : NMO_ERR_INVALID_FORMAT;
}

static int nmo_character_is_file_mode_deser(const nmo_chunk_t *chunk, void *context) {
    const nmo_deserialize_context_t *deser_ctx = nmo_deserialize_context_get(context);
    return (chunk && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE)) ||
        (deser_ctx != NULL && (deser_ctx->flags & NMO_DESER_FLAG_FILE_MODE) != 0);
}

static int nmo_character_is_file_mode_ser(const nmo_chunk_t *chunk, void *context) {
    const nmo_serialize_context_t *ser_ctx = nmo_serialize_context_try(context);
    return (chunk && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE)) ||
        (ser_ctx != NULL && (ser_ctx->flags & NMO_SERIALIZE_FLAG_FILE_MODE) != 0);
}

static size_t nmo_character_identifier_remaining_dwords(
    const nmo_chunk_t *chunk)
{
    if (!chunk || !chunk->parser_state) return 0;

    const nmo_chunk_parser_state_t *state =
        (const nmo_chunk_parser_state_t *)chunk->parser_state;
    const uint32_t *data =
        NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    size_t next_pos = chunk->data.count;
    if (state->prev_identifier_pos + 1u < chunk->data.count) {
        const uint32_t candidate = data[state->prev_identifier_pos + 1u];
        if (candidate != 0 && candidate <= chunk->data.count) {
            next_pos = candidate;
        }
    }
    if (next_pos < state->current_pos) return 0;
    return next_pos - state->current_pos;
}

static nmo_status_t read_ref_sequence(
    nmo_chunk_t *chunk,
    nmo_array_t *out_refs)
{
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result != NMO_OK) return result;
    if (count > UINT32_MAX) {
        return NMO_ERR_INVALID_FORMAT;
    }
    if (count > nmo_character_identifier_remaining_dwords(chunk)) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    nmo_array_t decoded;
    result = nmo_array_init(
        &decoded, sizeof(nmo_ref_t), count, &out_refs->allocator);
    if (result != NMO_OK) return result;
    nmo_ref_t *refs = NULL;
    result = nmo_array_extend(&decoded, count, (void **)&refs);
    if (result != NMO_OK) {
        nmo_array_dispose(&decoded);
        return result;
    }
    for (size_t i = 0; i < count; ++i) {
        result = nmo_ref_read(chunk, &refs[i]);
        if (result != NMO_OK) {
            nmo_array_dispose(&decoded);
            return result;
        }
    }
    result = nmo_array_swap(out_refs, &decoded);
    nmo_array_dispose(&decoded);
    return result;
}

static const nmo_type_field_t nmo_character_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_character_state_t, base),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD_ARRAY(
        nmo_character_state_t, body_parts,
        NMO_GUID_STRUCT_CKCHARACTERSUBPART),
    NMO_FIELD_REF_RECORD_ARRAY(nmo_character_state_t, animations),
    NMO_FIELD_NAMED("active_animation", offsetof(nmo_character_state_t, active_animation),
                    sizeof(nmo_ref_t), CKPGUID_ID,
                    NMO_FIELD_REFERENCE | NMO_FIELD_REF_RECORD,
                    NMO_SEMANTIC_OBJECT_REF),
    NMO_FIELD_NAMED("anim_dest", offsetof(nmo_character_state_t, anim_dest),
                    sizeof(nmo_ref_t), CKPGUID_ID,
                    NMO_FIELD_REFERENCE | NMO_FIELD_REF_RECORD,
                    NMO_SEMANTIC_OBJECT_REF),
    NMO_FIELD_NAMED("root_body_part", offsetof(nmo_character_state_t, root_body_part),
                    sizeof(nmo_ref_t), CKPGUID_ID,
                    NMO_FIELD_REFERENCE | NMO_FIELD_REF_RECORD,
                    NMO_SEMANTIC_OBJECT_REF),
    NMO_FIELD_NAMED("floor_ref", offsetof(nmo_character_state_t, floor_ref),
                    sizeof(nmo_ref_t), CKPGUID_ID,
                    NMO_FIELD_REFERENCE | NMO_FIELD_REF_RECORD,
                    NMO_SEMANTIC_OBJECT_REF)
};

static const nmo_type_field_t nmo_bodypart_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_bodypart_state_t, base),
                    sizeof(nmo_3dobject_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_bodypart_state_t, has_character, CKPGUID_UINT8),
    NMO_FIELD_NAMED("character", offsetof(nmo_bodypart_state_t, character),
                    sizeof(nmo_ref_t), CKPGUID_ID,
                    NMO_FIELD_REFERENCE | NMO_FIELD_REF_RECORD,
                    NMO_SEMANTIC_OBJECT_REF),
    NMO_FIELD(nmo_bodypart_state_t, has_rotation_joint, CKPGUID_UINT8),
    NMO_FIELD_NAMED("rotation_joint", offsetof(nmo_bodypart_state_t, rotation_joint),
                    sizeof(nmo_ik_joint_t), NMO_GUID_STRUCT_CKIKJOINT,
                    NMO_FIELD_REQUIRED, 0)
};

static nmo_status_t nmo_character_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    const nmo_character_state_t *s = src;
    nmo_character_state_t *d = dst;
    (void)type;
    if (s == NULL || d == NULL || arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    NMO_RETURN_IF_ERROR(nmo_character_validate(s, NULL, NULL));

    nmo_character_state_t copied;
    nmo_status_t result = nmo_character_create(&copied, NULL, NULL);
    if (result != NMO_OK) return result;

    nmo_type_descriptor_t base_type = {
        .size = sizeof(nmo_3dentity_state_t),
    };
    result = nmo_3dentity_vtable.copy(
        &s->base, &copied.base, &base_type, arena);
    if (result != NMO_OK) goto fail;
    copied.active_animation = s->active_animation;
    copied.anim_dest = s->anim_dest;
    copied.root_body_part = s->root_body_part;
    copied.floor_ref = s->floor_ref;

    nmo_array_dispose(&copied.body_parts);
    result = nmo_array_init(
        &copied.body_parts, sizeof(nmo_character_part_t),
        s->body_parts.count, &s->body_parts.allocator);
    if (result != NMO_OK) goto fail;
    character_parts_set_lifecycle(&copied.body_parts);
    nmo_character_part_t *dst_parts = NULL;
    result = nmo_array_extend(
        &copied.body_parts, s->body_parts.count, (void **)&dst_parts);
    if (result != NMO_OK) goto fail;
    const nmo_character_part_t *src_parts = NMO_ARRAY_DATA(
        nmo_character_part_t, &s->body_parts);
    for (size_t i = 0; i < s->body_parts.count; ++i) {
        dst_parts[i].ref = src_parts[i].ref;
        result = nmo_object_copy_chunk(
            arena, &dst_parts[i].chunk, src_parts[i].chunk);
        if (result != NMO_OK) goto fail;
    }
    nmo_array_dispose(&copied.animations);
    result = nmo_array_clone(
        &s->animations, &copied.animations, &s->animations.allocator);
    if (result != NMO_OK) goto fail;

    nmo_beobject_state_t *target_base = &d->base.base.base;
    const nmo_beobject_state_t *source_base = &s->base.base.base;
    if (target_base->scripts.data == source_base->scripts.data) {
        memset(&target_base->scripts, 0, sizeof(target_base->scripts));
    }
    if (target_base->attributes.data == source_base->attributes.data) {
        memset(&target_base->attributes, 0, sizeof(target_base->attributes));
    }
    if (target_base->legacy_attributes.data ==
        source_base->legacy_attributes.data) {
        memset(&target_base->legacy_attributes, 0,
               sizeof(target_base->legacy_attributes));
    }
    if (d->body_parts.data == s->body_parts.data) {
        memset(&d->body_parts, 0, sizeof(d->body_parts));
    }
    if (d->animations.data == s->animations.data) {
        memset(&d->animations, 0, sizeof(d->animations));
    }
    nmo_character_destroy(d, NULL, NULL);
    *d = copied;
    return NMO_OK;

fail:
    nmo_character_destroy(&copied, NULL, NULL);
    return result;
}

static nmo_status_t nmo_character_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_character_state_t *s = instance;
    if (s == NULL) return NMO_ERR_INVALID_ARGUMENT;
    NMO_RETURN_IF_ERROR(nmo_3dentity_vtable.validate(
        &s->base, NULL, context));
    NMO_VALIDATE_COUNT(s->body_parts.data, s->body_parts.count, "body_parts");
    NMO_VALIDATE_COUNT(s->animations.data, s->animations.count, "animations");
    if (s->body_parts.count > (size_t)INT32_MAX ||
        s->animations.count > (size_t)INT32_MAX) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    if ((s->body_parts.element_size != 0 &&
         s->body_parts.element_size != sizeof(nmo_character_part_t)) ||
        (s->body_parts.count > 0 &&
         s->body_parts.element_size != sizeof(nmo_character_part_t)) ||
        (s->animations.element_size != 0 &&
         s->animations.element_size != sizeof(nmo_ref_t)) ||
        (s->animations.count > 0 &&
         s->animations.element_size != sizeof(nmo_ref_t))) {
        return NMO_ERR_VALIDATION_FAILED;
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_bodypart_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    (void)type;
    if (src == NULL || dst == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_bodypart_state_t *source = src;
    nmo_bodypart_state_t *target = dst;
    nmo_type_descriptor_t base_type = {
        .size = sizeof(nmo_3dobject_state_t),
    };
    NMO_RETURN_IF_ERROR(nmo_3dobject_vtable.copy(
        &source->base, &target->base, &base_type, arena));
    target->has_character = source->has_character;
    target->character = source->character;
    target->has_rotation_joint = source->has_rotation_joint;
    target->rotation_joint = source->rotation_joint;
    return NMO_OK;
}

static nmo_status_t nmo_bodypart_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    if (instance == NULL) return NMO_ERR_INVALID_ARGUMENT;
    const nmo_bodypart_state_t *state = instance;
    return nmo_3dobject_vtable.validate(&state->base, NULL, context);
}

nmo_status_t nmo_character_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_character_validate(instance, type, context);
}

nmo_status_t nmo_character_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_character_remap_dependencies");
    }

    nmo_character_state_t *state = (nmo_character_state_t *)instance;

    nmo_status_t result = nmo_3dentity_remap_dependencies(&state->base, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    /* Keep unresolved and duplicate references for lossless save. */
    return nmo_character_validate(state, NULL, NULL);
}

nmo_status_t nmo_bodypart_prepare_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    return nmo_bodypart_validate(instance, type, context);
}

nmo_status_t nmo_bodypart_remap_dependencies(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;

    if (!instance) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_bodypart_remap_dependencies");
    }

    nmo_bodypart_state_t *state = (nmo_bodypart_state_t *)instance;

    nmo_status_t result = nmo_3dobject_remap_dependencies(&state->base, NULL, context);
    if (result != NMO_OK) {
        return result;
    }

    /* Preserve unresolved character references and optional sections. */
    return nmo_bodypart_validate(state, NULL, NULL);
}

static nmo_status_t nmo_character_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_character_pre_delete");
    }
    nmo_character_state_t *state = (nmo_character_state_t *)instance;
    nmo_array_clear(&state->body_parts);
    nmo_array_clear(&state->animations);
    state->active_animation = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->anim_dest = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->root_body_part = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    state->floor_ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    NMO_RETURN_OK();
}

static nmo_status_t nmo_character_enumerate_refs(
    const void *instance,
    const nmo_type_descriptor_t *type,
    nmo_type_ref_visitor_fn visitor,
    void *user_data)
{
    (void)type;
    const nmo_character_state_t *state = instance;
    if (state == NULL || visitor == NULL) return NMO_OK;
    nmo_status_t result = nmo_character_validate(state, NULL, NULL);
    if (result != NMO_OK) return result;

    const nmo_character_part_t *parts = NMO_ARRAY_DATA(
        nmo_character_part_t, &state->body_parts);
    for (size_t i = 0; i < state->body_parts.count; ++i) {
        const nmo_object_id_t id = nmo_ref_runtime_id(&parts[i].ref);
        if (id != NMO_OBJECT_ID_NONE &&
            !visitor(user_data, id, 0, "body_parts", (uint32_t)i)) {
            return NMO_OK;
        }
    }
    const nmo_ref_t *animations = NMO_ARRAY_DATA(
        nmo_ref_t, &state->animations);
    for (size_t i = 0; i < state->animations.count; ++i) {
        const nmo_object_id_t id = nmo_ref_runtime_id(&animations[i]);
        if (id != NMO_OBJECT_ID_NONE &&
            !visitor(user_data, id, 0, "animations", (uint32_t)i)) {
            return NMO_OK;
        }
    }
    const struct {
        const nmo_ref_t *ref;
        const char *name;
    } scalar_refs[] = {
        {&state->active_animation, "active_animation"},
        {&state->anim_dest, "anim_dest"},
        {&state->root_body_part, "root_body_part"},
        {&state->floor_ref, "floor_ref"},
    };
    for (size_t i = 0; i < sizeof(scalar_refs) / sizeof(scalar_refs[0]); ++i) {
        const nmo_object_id_t id = nmo_ref_runtime_id(scalar_refs[i].ref);
        if (id != NMO_OBJECT_ID_NONE &&
            !visitor(user_data, id, 0, scalar_refs[i].name, 0)) {
            break;
        }
    }
    return NMO_OK;
}

static void nmo_character_post_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
}

static nmo_status_t nmo_bodypart_pre_delete(
    void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    if (instance == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid arguments to nmo_bodypart_pre_delete");
    }
    NMO_RETURN_OK();
}

static void nmo_bodypart_post_delete(
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

static const nmo_object_serialize_pass_t nmo_character_compare_passes[] = {
    {
        .class_id = NMO_CID_CHARACTER,
        .data_version = 5,
        .chunk_options = NMO_CHUNK_OPTION_FILE,
        .serialize_flags = NMO_SERIALIZE_FLAG_FILE_MODE,
        .use_context = 1,
    },
    {
        .class_id = NMO_CID_CHARACTER,
        .data_version = 5,
        .save_flags = CK_STATESAVE_CHARACTERONLY |
            CK_STATESAVE_CHARACTERSAVEPARTS,
        .use_context = 1,
    },
};

static bool nmo_character_equals(const void *a, const void *b)
{
    return nmo_object_serialized_state_equals(
        a, b, nmo_character_serialize,
        nmo_character_compare_passes,
        sizeof(nmo_character_compare_passes) /
            sizeof(nmo_character_compare_passes[0]),
        4096);
}

static uint32_t nmo_character_hash(const void *instance)
{
    return nmo_object_serialized_state_hash(
        instance, nmo_character_serialize,
        nmo_character_compare_passes,
        sizeof(nmo_character_compare_passes) /
            sizeof(nmo_character_compare_passes[0]),
        4096);
}

static bool nmo_bodypart_equals(const void *a, const void *b)
{
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    const nmo_bodypart_state_t *lhs = a;
    const nmo_bodypart_state_t *rhs = b;
    return nmo_3dobject_vtable.equals(&lhs->base, &rhs->base) &&
        lhs->has_character == rhs->has_character &&
        lhs->character.raw_id == rhs->character.raw_id &&
        lhs->character.id == rhs->character.id &&
        lhs->character.state == rhs->character.state &&
        lhs->has_rotation_joint == rhs->has_rotation_joint &&
        lhs->rotation_joint.flags == rhs->rotation_joint.flags &&
        memcmp(&lhs->rotation_joint.min, &rhs->rotation_joint.min,
               sizeof(lhs->rotation_joint.min)) == 0 &&
        memcmp(&lhs->rotation_joint.max, &rhs->rotation_joint.max,
               sizeof(lhs->rotation_joint.max)) == 0 &&
        memcmp(&lhs->rotation_joint.damping, &rhs->rotation_joint.damping,
               sizeof(lhs->rotation_joint.damping)) == 0;
}

static uint32_t nmo_bodypart_hash_bytes(
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

static uint32_t nmo_bodypart_hash(const void *instance)
{
    if (instance == NULL) return 0;
    const nmo_bodypart_state_t *state = instance;
    uint32_t hash = nmo_3dobject_vtable.hash(&state->base);
    hash = nmo_bodypart_hash_bytes(
        hash, &state->has_character, sizeof(state->has_character));
    hash = nmo_bodypart_hash_bytes(
        hash, &state->character.raw_id, sizeof(state->character.raw_id));
    hash = nmo_bodypart_hash_bytes(
        hash, &state->character.id, sizeof(state->character.id));
    hash = nmo_bodypart_hash_bytes(
        hash, &state->character.state, sizeof(state->character.state));
    hash = nmo_bodypart_hash_bytes(
        hash, &state->has_rotation_joint,
        sizeof(state->has_rotation_joint));
    hash = nmo_bodypart_hash_bytes(
        hash, &state->rotation_joint.flags,
        sizeof(state->rotation_joint.flags));
    hash = nmo_bodypart_hash_bytes(
        hash, &state->rotation_joint.min,
        sizeof(state->rotation_joint.min));
    hash = nmo_bodypart_hash_bytes(
        hash, &state->rotation_joint.max,
        sizeof(state->rotation_joint.max));
    return nmo_bodypart_hash_bytes(
        hash, &state->rotation_joint.damping,
        sizeof(state->rotation_joint.damping));
}

nmo_type_vtable_t nmo_character_vtable = {
    .prepare_dependencies = nmo_character_prepare_dependencies,
    .remap_dependencies = nmo_character_remap_dependencies,
    .pre_delete = nmo_character_pre_delete,
    .post_delete = nmo_character_post_delete,
    NMO_OBJECT_VTABLE_EX(
        nmo_character_create,
        nmo_character_destroy,
        nmo_character_serialize,
        nmo_character_deserialize,
        nmo_character_copy,
        nmo_character_validate,
        nmo_character_equals,
        nmo_character_hash,
        nmo_character_enumerate_refs)
};

nmo_type_vtable_t nmo_bodypart_vtable = {
    .prepare_dependencies = nmo_bodypart_prepare_dependencies,
    .remap_dependencies = nmo_bodypart_remap_dependencies,
    .pre_delete = nmo_bodypart_pre_delete,
    .post_delete = nmo_bodypart_post_delete,
    NMO_OBJECT_VTABLE(
        nmo_bodypart_create,
        nmo_bodypart_destroy,
        nmo_bodypart_serialize,
        nmo_bodypart_deserialize,
        nmo_bodypart_copy,
        nmo_bodypart_validate,
        nmo_bodypart_equals,
        nmo_bodypart_hash)
};

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_character_type,
    CKPGUID_CHARACTER,
    "CKCharacter",
    NMO_CID_CHARACTER,
    CKPGUID_3DENTITY,
    nmo_character_state_t,
    &nmo_character_vtable,
    nmo_character_fields)

NMO_DEFINE_OBJECT_REGISTRATION_RUNTIME_FIELDS(
    nmo_register_bodypart_type,
    CKPGUID_BODYPART,
    "CKBodyPart",
    NMO_CID_BODYPART,
    CKPGUID_OBJECT3D,
    nmo_bodypart_state_t,
    &nmo_bodypart_vtable,
    nmo_bodypart_fields)

static nmo_status_t read_part_sequence(
    nmo_chunk_t *chunk,
    nmo_array_t *out_parts)
{
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result != NMO_OK) return result;
    if (count > UINT32_MAX) {
        return NMO_ERR_INVALID_FORMAT;
    }
    if (count > nmo_character_identifier_remaining_dwords(chunk)) {
        return NMO_ERR_TRUNCATED_CHUNK;
    }
    nmo_array_t decoded;
    result = nmo_array_init(
        &decoded, sizeof(nmo_character_part_t), count,
        &out_parts->allocator);
    if (result != NMO_OK) return result;
    character_parts_set_lifecycle(&decoded);
    nmo_character_part_t *parts = NULL;
    result = nmo_array_extend(&decoded, count, (void **)&parts);
    if (result != NMO_OK) {
        nmo_array_dispose(&decoded);
        return result;
    }
    for (size_t i = 0; i < count; ++i) {
        result = nmo_ref_read(chunk, &parts[i].ref);
        if (result != NMO_OK) {
            nmo_array_dispose(&decoded);
            return result;
        }
    }
    result = nmo_array_swap(out_parts, &decoded);
    nmo_array_dispose(&decoded);
    return result;
}

static nmo_status_t write_part_sequence(
    nmo_chunk_t *chunk,
    const nmo_array_t *parts)
{
    nmo_status_t result = nmo_chunk_write_object_sequence_start(
        chunk, parts->count);
    if (result != NMO_OK) return result;
    const nmo_character_part_t *items = NMO_ARRAY_DATA(
        nmo_character_part_t, parts);
    for (size_t i = 0; i < parts->count; ++i) {
        result = nmo_ref_write_sequence_item(chunk, &items[i].ref);
        if (result != NMO_OK) return result;
    }
    return NMO_OK;
}

static nmo_status_t write_ref_sequence(
    nmo_chunk_t *chunk,
    const nmo_array_t *refs)
{
    nmo_status_t result = nmo_chunk_write_object_sequence_start(
        chunk, refs->count);
    if (result != NMO_OK) return result;
    const nmo_ref_t *items = NMO_ARRAY_DATA(nmo_ref_t, refs);
    for (size_t i = 0; i < refs->count; ++i) {
        result = nmo_ref_write_sequence_item(chunk, &items[i]);
        if (result != NMO_OK) return result;
    }
    return NMO_OK;
}

static void nmo_character_check_ref_classes(
    nmo_character_state_t *state,
    void *context)
{
    const nmo_object_repository_t *repository =
        (const nmo_object_repository_t *)
            nmo_deserialize_context_get_repository(context);
    const nmo_type_registry_t *types =
        nmo_deserialize_context_get_type_registry(context);
    nmo_character_part_t *parts = NMO_ARRAY_DATA(
        nmo_character_part_t, &state->body_parts);
    for (size_t i = 0; i < state->body_parts.count; ++i) {
        nmo_ref_check_class(&parts[i].ref, repository, types, NMO_CID_BODYPART);
    }
    nmo_ref_t *animations = NMO_ARRAY_DATA(nmo_ref_t, &state->animations);
    for (size_t i = 0; i < state->animations.count; ++i) {
        nmo_ref_check_class(
            &animations[i], repository, types, NMO_CID_OBJECTANIMATION);
    }
    nmo_ref_check_class(
        &state->active_animation, repository, types, NMO_CID_OBJECTANIMATION);
    nmo_ref_check_class(
        &state->anim_dest, repository, types, NMO_CID_OBJECTANIMATION);
    nmo_ref_check_class(
        &state->root_body_part, repository, types, NMO_CID_BODYPART);
    nmo_ref_check_class(
        &state->floor_ref, repository, types, NMO_CID_3DENTITY);
}

static nmo_status_t nmo_character_seek_optional(
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

static nmo_status_t nmo_character_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_character_state_t *out_state)
{
    if (chunk == NULL || out_state == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t result = nmo_3dentity_deserialize(
        &out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    nmo_character_state_t decoded = {0};
    result = nmo_array_init(
        &decoded.body_parts, sizeof(nmo_character_part_t), 0,
        &out_state->body_parts.allocator);
    if (result != NMO_OK) return result;
    character_parts_set_lifecycle(&decoded.body_parts);
    result = nmo_array_init(
        &decoded.animations, sizeof(nmo_ref_t), 0,
        &out_state->animations.allocator);
    if (result != NMO_OK) {
        nmo_array_dispose(&decoded.body_parts);
        return result;
    }

    const uint32_t data_version = nmo_chunk_get_data_version(chunk);
    const bool is_file = nmo_character_is_file_mode_deser(chunk, context);
    bool section_found = false;
    if (data_version < 5) {
        if (is_file) {
            result = nmo_character_seek_optional(
                chunk, CK_STATESAVE_CHARACTERBODYPARTS, &section_found);
            if (result != NMO_OK) goto fail;
            if (section_found) {
                result = read_part_sequence(chunk, &decoded.body_parts);
                if (result != NMO_OK) goto fail;
            }
            result = nmo_character_seek_optional(
                chunk, CK_STATESAVE_CHARACTERANIMATIONS, &section_found);
            if (result != NMO_OK) goto fail;
            if (section_found) {
                result = read_ref_sequence(chunk, &decoded.animations);
                if (result != NMO_OK) goto fail;
                result = nmo_ref_read(chunk, &decoded.active_animation);
                if (result != NMO_OK) goto fail;
                result = nmo_ref_read(chunk, &decoded.anim_dest);
                if (result != NMO_OK) goto fail;
            }
        } else {
            result = nmo_character_seek_optional(
                chunk, CK_STATESAVE_CHARACTERSAVEANIMS, &section_found);
            if (result != NMO_OK) goto fail;
            if (section_found) {
                uint32_t unused = 0;
                result = nmo_chunk_read_dword(chunk, &unused);
                if (result != NMO_OK) goto fail;
                result = nmo_ref_read(chunk, &decoded.active_animation);
                if (result != NMO_OK) goto fail;
                result = nmo_ref_read(chunk, &decoded.anim_dest);
                if (result != NMO_OK) goto fail;
            }
            result = nmo_character_seek_optional(
                chunk, CK_STATESAVE_CHARACTERSAVEPARTS, &section_found);
            if (result != NMO_OK) goto fail;
            if (section_found) {
                uint32_t count = 0;
                result = nmo_chunk_read_dword(chunk, &count);
                if (result != NMO_OK) goto fail;
                if ((size_t)count >
                    nmo_character_identifier_remaining_dwords(chunk) / 2u) {
                    result = NMO_ERR_TRUNCATED_CHUNK;
                    goto fail;
                }
                nmo_character_part_t *parts = NULL;
                result = nmo_array_extend(
                    &decoded.body_parts, count, (void **)&parts);
                if (result != NMO_OK) goto fail;
                for (uint32_t i = 0; i < count; ++i) {
                    result = nmo_ref_read(chunk, &parts[i].ref);
                    if (result != NMO_OK) goto fail;
                    result = nmo_chunk_read_sub_chunk(chunk, &parts[i].chunk);
                    if (result != NMO_OK) goto fail;
                }
            }
        }
        result = nmo_character_seek_optional(
            chunk, CK_STATESAVE_CHARACTERROOT, &section_found);
        if (result != NMO_OK) goto fail;
        if (section_found) {
            result = nmo_ref_read(chunk, &decoded.root_body_part);
            if (result != NMO_OK) goto fail;
        }
        result = nmo_character_seek_optional(
            chunk, CK_STATESAVE_CHARACTERFLOORREF, &section_found);
        if (result != NMO_OK) goto fail;
        if (section_found) {
            result = nmo_ref_read(chunk, &decoded.floor_ref);
            if (result != NMO_OK) goto fail;
        }
    } else {
        result = nmo_character_seek_optional(
            chunk, CK_STATESAVE_CHARACTERBODYPARTS, &section_found);
        if (result != NMO_OK) goto fail;
        if (section_found) {
            result = read_part_sequence(chunk, &decoded.body_parts);
            if (result != NMO_OK) goto fail;
        }
        result = nmo_character_seek_optional(
            chunk, CK_STATESAVE_CHARACTERSAVEPARTS, &section_found);
        if (result != NMO_OK) goto fail;
        if (section_found) {
            size_t count = 0;
            result = nmo_chunk_start_read_sub_chunk_sequence(chunk, &count);
            if (result != NMO_OK) goto fail;
            if (count != decoded.body_parts.count) {
                result = NMO_ERR_INVALID_FORMAT;
                goto fail;
            }
            nmo_character_part_t *parts = NMO_ARRAY_DATA(
                nmo_character_part_t, &decoded.body_parts);
            for (size_t i = 0; i < count; ++i) {
                result = nmo_chunk_read_sub_chunk(chunk, &parts[i].chunk);
                if (result != NMO_OK) goto fail;
            }
        }
        result = nmo_character_seek_optional(
            chunk, CK_STATESAVE_CHARACTERONLY, &section_found);
        if (result != NMO_OK) goto fail;
        if (section_found) {
            if (is_file) {
                result = read_ref_sequence(chunk, &decoded.animations);
                if (result != NMO_OK) goto fail;
            }
            size_t seq_count = 0;
            result = nmo_chunk_read_object_sequence_start(chunk, &seq_count);
            if (result != NMO_OK) goto fail;
            if (seq_count != 4) {
                result = NMO_ERR_INVALID_FORMAT;
                goto fail;
            }
            result = nmo_ref_read(
                chunk, &decoded.active_animation);
            if (result != NMO_OK) goto fail;
            result = nmo_ref_read(chunk, &decoded.anim_dest);
            if (result != NMO_OK) goto fail;
            result = nmo_ref_read(
                chunk, &decoded.root_body_part);
            if (result != NMO_OK) goto fail;
            result = nmo_ref_read(chunk, &decoded.floor_ref);
            if (result != NMO_OK) goto fail;
        }
    }

    nmo_character_check_ref_classes(&decoded, context);
    result = nmo_array_swap(&out_state->body_parts, &decoded.body_parts);
    if (result != NMO_OK) goto fail;
    result = nmo_array_swap(&out_state->animations, &decoded.animations);
    if (result != NMO_OK) {
        (void)nmo_array_swap(&out_state->body_parts, &decoded.body_parts);
        goto fail;
    }
    out_state->active_animation = decoded.active_animation;
    out_state->anim_dest = decoded.anim_dest;
    out_state->root_body_part = decoded.root_body_part;
    out_state->floor_ref = decoded.floor_ref;
    nmo_array_dispose(&decoded.body_parts);
    nmo_array_dispose(&decoded.animations);
    return NMO_OK;

fail:
    nmo_array_dispose(&decoded.body_parts);
    nmo_array_dispose(&decoded.animations);
    return result;
}

static nmo_status_t nmo_character_serialize_internal(
    const nmo_character_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (in_state == NULL || out_chunk == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_status_t result = nmo_character_validate(in_state, NULL, NULL);
    if (result != NMO_OK || in_state->body_parts.count > UINT32_MAX ||
        in_state->animations.count > UINT32_MAX) {
        return result != NMO_OK ? result : NMO_ERR_VALIDATION_FAILED;
    }

    result = nmo_3dentity_serialize(
        &in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;
    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    const bool is_file = nmo_character_is_file_mode_ser(out_chunk, context);
    if (!is_file && (save_flags & CK_STATESAVE_CHARACTERONLY) == 0) {
        return NMO_OK;
    }

    result = nmo_chunk_write_identifier(
        out_chunk, CK_STATESAVE_CHARACTERBODYPARTS);
    if (result != NMO_OK) return result;
    result = write_part_sequence(out_chunk, &in_state->body_parts);
    if (result != NMO_OK) return result;

    const nmo_character_part_t *parts = NMO_ARRAY_DATA(
        nmo_character_part_t, &in_state->body_parts);
    if (!is_file && (save_flags & CK_STATESAVE_CHARACTERSAVEPARTS) != 0) {
        result = nmo_chunk_write_identifier(
            out_chunk, CK_STATESAVE_CHARACTERSAVEPARTS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_start_sub_chunk_sequence(
            out_chunk, in_state->body_parts.count);
        if (result != NMO_OK) return result;
        for (size_t i = 0; i < in_state->body_parts.count; ++i) {
            result = nmo_chunk_write_sub_chunk_sequence(
                out_chunk, parts[i].chunk);
            if (result != NMO_OK) return result;
        }
    }

    if (is_file || (save_flags & CK_STATESAVE_CHARACTERONLY) != 0) {
        result = nmo_chunk_write_identifier(
            out_chunk, CK_STATESAVE_CHARACTERONLY);
        if (result != NMO_OK) return result;
        if (is_file) {
            result = write_ref_sequence(out_chunk, &in_state->animations);
            if (result != NMO_OK) return result;
        }
        result = nmo_chunk_write_object_sequence_start(out_chunk, 4);
        if (result != NMO_OK) return result;
        result = nmo_ref_write_sequence_item(
            out_chunk, &in_state->active_animation);
        if (result != NMO_OK) return result;
        result = nmo_ref_write_sequence_item(out_chunk, &in_state->anim_dest);
        if (result != NMO_OK) return result;
        result = nmo_ref_write_sequence_item(
            out_chunk, &in_state->root_body_part);
        if (result != NMO_OK) return result;
        return nmo_ref_write_sequence_item(out_chunk, &in_state->floor_ref);
    }
    return NMO_OK;
}

static nmo_status_t nmo_bodypart_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_bodypart_state_t *out_state)
{
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_bodypart_deserialize");
    }

    {
        nmo_status_t result = nmo_3dobject_deserialize(&out_state->base, chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    uint32_t data_version = nmo_chunk_get_data_version(chunk);
    uint8_t has_character = 0;
    uint8_t has_rotation_joint = 0;
    nmo_ref_t character = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    nmo_ik_joint_t rotation_joint = {0};
    bool section_found = false;
    nmo_status_t result = NMO_OK;

    if (data_version >= 5) {
        result = nmo_character_seek_optional(
            chunk, CK_STATESAVE_BODYPARTCHARACTER, &section_found);
        if (result != NMO_OK) return result;
        if (section_found) {
            result = nmo_ref_read(chunk, &character);
            if (result != NMO_OK) return result;
            has_character = 1;

            if ((out_state->base.entity.entity_flags & CK_3DENTITY_IKJOINTVALID) != 0) {
                result = read_exact_sized_buffer(
                    chunk, &rotation_joint, sizeof(nmo_ik_joint_t));
                if (result != NMO_OK) return result;
                has_rotation_joint = 1;
            }
        }
    } else {
        result = nmo_character_seek_optional(
            chunk, CK_STATESAVE_BODYPARTROTJOINT, &section_found);
        if (result != NMO_OK) return result;
        if (section_found) {
            nmo_vector_t vectors[6];
            memset(vectors, 0, sizeof(vectors));
            result = read_exact_sized_buffer(chunk, vectors, sizeof(vectors));
            if (result != NMO_OK) return result;

            has_rotation_joint = 1;
            rotation_joint.flags = 0;
            rotation_joint.min = vectors[3];
            rotation_joint.max = vectors[4];
            rotation_joint.damping = vectors[5];

            for (int i = 0; i < 3; ++i) {
                const float *v0 = &vectors[0].x + i;
                const float *v1 = &vectors[1].x + i;
                const float *v2 = &vectors[2].x + i;
                if (*v0 != 0.0f) rotation_joint.flags |= (1u << i);
                if (*v1 != 0.0f) rotation_joint.flags |= (16u << i);
                if (*v2 != 0.0f) rotation_joint.flags |= (256u << i);
            }
        }

        result = nmo_character_seek_optional(
            chunk, CK_STATESAVE_BODYPARTCHARACTER, &section_found);
        if (result != NMO_OK) return result;
        if (section_found) {
            result = nmo_ref_read(chunk, &character);
            if (result != NMO_OK) return result;
            has_character = 1;
        }
    }

    nmo_ref_check_class(
        &character,
        (const nmo_object_repository_t *)
            nmo_deserialize_context_get_repository(context),
        nmo_deserialize_context_get_type_registry(context),
        NMO_CID_CHARACTER);
    out_state->has_character = has_character;
    out_state->character = character;
    out_state->has_rotation_joint = has_rotation_joint;
    out_state->rotation_joint = rotation_joint;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_bodypart_serialize_internal(
    const nmo_bodypart_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_bodypart_serialize");
    }

    {
        nmo_status_t result = nmo_3dobject_serialize(&in_state->base, out_chunk, NULL, context);
        if (result != NMO_OK) return result;
    }

    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    const bool is_file = nmo_character_is_file_mode_ser(out_chunk, context);
    if (!is_file && (save_flags & CK_STATESAVE_BODYPARTONLY) == 0) {
        NMO_RETURN_OK();
    }

    {
        nmo_status_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BODYPARTCHARACTER);
        if (result != NMO_OK) return result;
        result = nmo_ref_write(out_chunk, &in_state->character);
        if (result != NMO_OK) return result;

        if ((in_state->base.entity.entity_flags & CK_3DENTITY_IKJOINTVALID) != 0) {
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                    &in_state->rotation_joint,
                                                    sizeof(nmo_ik_joint_t));
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

nmo_status_t nmo_character_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_character_state_t *out_state = (nmo_character_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_character_state_t decoded;
    nmo_status_t result = nmo_character_create(&decoded, type, context);
    if (result != NMO_OK) return result;
    decoded.body_parts.allocator = out_state->body_parts.allocator;
    decoded.animations.allocator = out_state->animations.allocator;
    character_copy_base_allocators(&decoded.base, &out_state->base);
    result = nmo_character_deserialize_internal(chunk, context, &decoded);
    if (result != NMO_OK) {
        nmo_character_destroy(&decoded, NULL, context);
        return result;
    }

    nmo_character_destroy(out_state, NULL, context);
    *out_state = decoded;
    return NMO_OK;
}

nmo_status_t nmo_character_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    const nmo_character_state_t *in_state = (const nmo_character_state_t *)instance;
    if (in_state == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_character_serialize_internal(
        in_state, staged, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    (void)type;
    return NMO_OK;
}

nmo_status_t nmo_bodypart_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    nmo_bodypart_state_t *out_state = (nmo_bodypart_state_t *)instance;
    if (out_state == NULL || chunk == NULL) return NMO_ERR_INVALID_ARGUMENT;

    nmo_bodypart_state_t decoded;
    nmo_status_t result = nmo_bodypart_create(&decoded, type, context);
    if (result != NMO_OK) return result;
    character_copy_base_allocators(
        &decoded.base.entity, &out_state->base.entity);
    result = nmo_bodypart_deserialize_internal(chunk, context, &decoded);
    if (result != NMO_OK) {
        nmo_bodypart_destroy(&decoded, NULL, context);
        return result;
    }

    nmo_bodypart_destroy(out_state, NULL, context);
    *out_state = decoded;
    return NMO_OK;
}

nmo_status_t nmo_bodypart_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    const nmo_bodypart_state_t *in_state = (const nmo_bodypart_state_t *)instance;
    if (in_state == NULL || out_chunk == NULL || out_chunk->arena == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_chunk_t *staged = nmo_chunk_create(out_chunk->arena);
    if (staged == NULL) return NMO_ERR_NOMEM;
    staged->class_id = out_chunk->class_id;
    staged->data_version = out_chunk->data_version;
    staged->chunk_version = out_chunk->chunk_version;
    staged->chunk_class_id = out_chunk->chunk_class_id;
    staged->chunk_options = out_chunk->chunk_options;
    staged->file_context = out_chunk->file_context;
    nmo_status_t result = nmo_bodypart_serialize_internal(
        in_state, staged, context);
    if (result != NMO_OK) return result;
    *out_chunk = *staged;
    (void)type;
    return NMO_OK;
}
