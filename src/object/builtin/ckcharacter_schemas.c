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
#include <string.h>

NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(character, nmo_character_state_t)
NMO_DEFINE_OBJECT_LIFECYCLE_SIMPLE(bodypart, nmo_bodypart_state_t)

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

static nmo_status_t read_object_sequence(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_object_id_t **out_ids,
    uint32_t *out_count)
{
    size_t count = 0;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result != NMO_OK) return result;

    if (count == 0) {
        *out_ids = NULL;
        *out_count = 0;
        NMO_RETURN_OK();
    }

    *out_count = (uint32_t)count;
    *out_ids = (nmo_object_id_t *)nmo_arena_alloc(
        arena, sizeof(nmo_object_id_t) * (*out_count), _Alignof(nmo_object_id_t));
    if (!*out_ids) {
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate object ID array");
    }

    for (uint32_t i = 0; i < *out_count; ++i) {
        result = nmo_chunk_read_object_sequence_item(chunk, &(*out_ids)[i]);
        if (result != NMO_OK) {
            *out_count = i;
            return result;
        }
    }

    NMO_RETURN_OK();
}

static const nmo_type_field_t nmo_character_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_character_state_t, base),
                    sizeof(nmo_3dentity_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_character_state_t, body_part_count, CKPGUID_UINT32),
    NMO_FIELD_REF_ARRAY(nmo_character_state_t, body_part_ids),
    NMO_FIELD(nmo_character_state_t, animation_count, CKPGUID_UINT32),
    NMO_FIELD_REF_ARRAY(nmo_character_state_t, animation_ids),
    NMO_FIELD_REF(nmo_character_state_t, active_animation_id),
    NMO_FIELD_REF(nmo_character_state_t, anim_dest_id),
    NMO_FIELD_REF(nmo_character_state_t, root_body_part_id),
    NMO_FIELD_REF(nmo_character_state_t, floor_ref_id),
    NMO_FIELD(nmo_character_state_t, subpart_count, CKPGUID_UINT32),
    NMO_FIELD_ARRAY(nmo_character_state_t, subparts, NMO_GUID_STRUCT_CKCHARACTERSUBPART)
};

static const nmo_type_field_t nmo_bodypart_fields[] = {
    NMO_FIELD_NAMED("base", offsetof(nmo_bodypart_state_t, base),
                    sizeof(nmo_3dobject_state_t), CKPGUID_NONE,
                    NMO_FIELD_REQUIRED, 0),
    NMO_FIELD(nmo_bodypart_state_t, has_character, CKPGUID_UINT8),
    NMO_FIELD_REF(nmo_bodypart_state_t, character_id),
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
    NMO_RETURN_IF_ERROR(nmo_object_default_copy(src, dst, type, arena));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->body_part_ids,
                                              s->body_part_ids, sizeof(nmo_object_id_t), s->body_part_count));
    NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->animation_ids,
                                              s->animation_ids, sizeof(nmo_object_id_t), s->animation_count));
    if (s->subpart_count > 0) {
        NMO_RETURN_IF_ERROR(nmo_object_copy_array(arena, (void **)&d->subparts,
                                                  s->subparts, sizeof(nmo_character_subpart_t),
                                                  s->subpart_count));
        for (uint32_t i = 0; i < s->subpart_count; ++i) {
            nmo_chunk_t *clone = NULL;
            NMO_RETURN_IF_ERROR(nmo_object_copy_chunk(arena, &clone, s->subparts[i].chunk));
            d->subparts[i].chunk = clone;
        }
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_character_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    (void)context;
    const nmo_character_state_t *s = instance;
    NMO_VALIDATE_COUNT(s->body_part_ids, s->body_part_count, "body_part_ids");
    NMO_VALIDATE_COUNT(s->animation_ids, s->animation_count, "animation_ids");
    NMO_VALIDATE_COUNT(s->subparts, s->subpart_count, "subparts");
    NMO_RETURN_OK();
}

static nmo_status_t nmo_bodypart_copy(
    const void *src,
    void *dst,
    const nmo_type_descriptor_t *type,
    nmo_arena_t *arena)
{
    return nmo_object_default_copy(src, dst, type, arena);
}

static nmo_status_t nmo_bodypart_validate(
    const void *instance,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)instance;
    (void)type;
    (void)context;
    NMO_RETURN_OK();
}


/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    character,
    nmo_character_state_t,
    nmo_character_serialize,
    nmo_character_deserialize,
    nmo_character_fields,
    CKPGUID_CHARACTER,
    "CKCharacter",
    NMO_CID_CHARACTER,
    CKPGUID_3DENTITY
)

NMO_DEFINE_OBJECT_SCHEMA_FIELDS_CUSTOM(
    bodypart,
    nmo_bodypart_state_t,
    nmo_bodypart_serialize,
    nmo_bodypart_deserialize,
    nmo_bodypart_fields,
    CKPGUID_BODYPART,
    "CKBodyPart",
    NMO_CID_BODYPART,
    CKPGUID_OBJECT3D
)

static nmo_status_t write_object_sequence(
    nmo_chunk_t *chunk,
    const nmo_object_id_t *ids,
    uint32_t count)
{
    nmo_status_t result = nmo_chunk_write_object_sequence_start(chunk, count);
    if (result != NMO_OK) return result;

    for (uint32_t i = 0; i < count; ++i) {
        result = nmo_chunk_write_object_sequence_item(chunk, ids[i]);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_character_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_character_state_t *out_state)
{
    nmo_arena_t *arena = nmo_deserialize_context_get_arena(context);
    if (!chunk || !out_state) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_character_deserialize");
    }

    nmo_status_t result = nmo_3dentity_deserialize(&out_state->base, chunk, NULL, context);
    if (result != NMO_OK) return result;

    uint32_t data_version = nmo_chunk_get_data_version(chunk);
    const bool is_file = nmo_character_is_file_mode_deser(chunk, context);
    const uint32_t kMaxSubparts = 10000;

    if (data_version < 5) {
        if (is_file) {
            if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERBODYPARTS) == NMO_OK) {
                result = read_object_sequence(chunk, arena,
                                              &out_state->body_part_ids,
                                              &out_state->body_part_count);
                if (result != NMO_OK) return result;
            }

            if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERANIMATIONS) == NMO_OK) {
                result = read_object_sequence(chunk, arena,
                                              &out_state->animation_ids,
                                              &out_state->animation_count);
                if (result != NMO_OK) return result;
                result = nmo_chunk_read_object_id(chunk, &out_state->active_animation_id);
                if (result != NMO_OK) return result;
                result = nmo_chunk_read_object_id(chunk, &out_state->anim_dest_id);
                if (result != NMO_OK) return result;
            }
        } else {
            if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERSAVEANIMS) == NMO_OK) {
                uint32_t unused = 0;
                result = nmo_chunk_read_dword(chunk, &unused);
                if (result != NMO_OK) return result;
                result = nmo_chunk_read_object_id(chunk, &out_state->active_animation_id);
                if (result != NMO_OK) return result;
                result = nmo_chunk_read_object_id(chunk, &out_state->anim_dest_id);
                if (result != NMO_OK) return result;
            }

            if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERSAVEPARTS) == NMO_OK) {
                uint32_t count = 0;
                result = nmo_chunk_read_dword(chunk, &count);
                if (result != NMO_OK) return result;
                if (count > kMaxSubparts) {
                    NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                                     "Subpart count exceeds maximum");
                }
                if (count > 0) {
                    out_state->subpart_count = count;
                    out_state->subparts = (nmo_character_subpart_t *)nmo_arena_alloc(
                        arena, sizeof(nmo_character_subpart_t) * count,
                        _Alignof(nmo_character_subpart_t));
                    if (!out_state->subparts) {
                        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate subpart array");
                    }

                    for (uint32_t i = 0; i < count; ++i) {
                        result = nmo_chunk_read_object_id(chunk, &out_state->subparts[i].object_id);
                        if (result != NMO_OK) return result;
                        result = nmo_chunk_read_sub_chunk(chunk, &out_state->subparts[i].chunk);
                        if (result != NMO_OK) return result;
                    }
                }
            }
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERROOT) == NMO_OK) {
            result = nmo_chunk_read_object_id(chunk, &out_state->root_body_part_id);
            if (result != NMO_OK) return result;
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERFLOORREF) == NMO_OK) {
            result = nmo_chunk_read_object_id(chunk, &out_state->floor_ref_id);
            if (result != NMO_OK) return result;
        }
    } else {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERBODYPARTS) == NMO_OK) {
            result = read_object_sequence(chunk, arena,
                                          &out_state->body_part_ids,
                                          &out_state->body_part_count);
            if (result != NMO_OK) return result;
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERSAVEPARTS) == NMO_OK) {
            size_t count = 0;
            result = nmo_chunk_start_read_sub_chunk_sequence(chunk, &count);
            if (result != NMO_OK) return result;
            if (count > 0 && out_state->body_part_count == (uint32_t)count) {
                out_state->subpart_count = (uint32_t)count;
                out_state->subparts = (nmo_character_subpart_t *)nmo_arena_alloc(
                    arena, sizeof(nmo_character_subpart_t) * out_state->subpart_count,
                    _Alignof(nmo_character_subpart_t));
                if (!out_state->subparts) {
                    NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR, "Failed to allocate subpart array");
                }

                for (uint32_t i = 0; i < out_state->subpart_count; ++i) {
                    out_state->subparts[i].object_id =
                        (out_state->body_part_ids && i < out_state->body_part_count)
                            ? out_state->body_part_ids[i]
                            : 0;
                    result = nmo_chunk_read_sub_chunk(chunk, &out_state->subparts[i].chunk);
                    if (result != NMO_OK) return result;
                }
            }
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERONLY) == NMO_OK) {
            if (is_file) {
                result = read_object_sequence(chunk, arena,
                                              &out_state->animation_ids,
                                              &out_state->animation_count);
                if (result != NMO_OK) return result;
            }
            size_t seq_count = 0;
            result = nmo_chunk_read_object_sequence_start(chunk, &seq_count);
            if (result != NMO_OK) return result;
            if (seq_count < 4) {
                NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                                 "Character sequence too small");
            }
            result = nmo_chunk_read_object_sequence_item(chunk, &out_state->active_animation_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_object_sequence_item(chunk, &out_state->anim_dest_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_object_sequence_item(chunk, &out_state->root_body_part_id);
            if (result != NMO_OK) return result;
            result = nmo_chunk_read_object_sequence_item(chunk, &out_state->floor_ref_id);
            if (result != NMO_OK) return result;
        }
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_character_serialize_internal(
    const nmo_character_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!in_state || !out_chunk) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR, "Invalid arguments to nmo_character_serialize");
    }

    nmo_status_t result = nmo_3dentity_serialize(&in_state->base, out_chunk, NULL, context);
    if (result != NMO_OK) return result;

    const uint32_t save_flags = nmo_serialize_context_get_save_flags(context);
    const bool is_file = nmo_character_is_file_mode_ser(out_chunk, context);
    if (!is_file && (save_flags & CK_STATESAVE_CHARACTERONLY) == 0) {
        NMO_RETURN_OK();
    }

    result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CHARACTERBODYPARTS);
    if (result != NMO_OK) return result;
    result = write_object_sequence(out_chunk, in_state->body_part_ids, in_state->body_part_count);
    if (result != NMO_OK) return result;

    if (!is_file && (save_flags & CK_STATESAVE_CHARACTERSAVEPARTS) != 0) {
        const uint32_t subpart_count = in_state->body_part_count;
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CHARACTERSAVEPARTS);
        if (result != NMO_OK) return result;
        result = nmo_chunk_start_sub_chunk_sequence(out_chunk, subpart_count);
        if (result != NMO_OK) return result;
        for (uint32_t i = 0; i < subpart_count; ++i) {
            nmo_chunk_t *sub = NULL;
            if (in_state->subparts && i < in_state->subpart_count) {
                sub = in_state->subparts[i].chunk;
            }
            result = nmo_chunk_write_sub_chunk_sequence(out_chunk, sub);
            if (result != NMO_OK) return result;
        }
    }

    if (is_file || (save_flags & CK_STATESAVE_CHARACTERONLY) != 0) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CHARACTERONLY);
        if (result != NMO_OK) return result;
        if (is_file) {
            result = write_object_sequence(out_chunk, in_state->animation_ids,
                                           in_state->animation_count);
            if (result != NMO_OK) return result;
        }

        result = nmo_chunk_write_object_sequence_start(out_chunk, 4);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->active_animation_id);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->anim_dest_id);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->root_body_part_id);
        if (result != NMO_OK) return result;
        result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->floor_ref_id);
        if (result != NMO_OK) return result;
    }

    NMO_RETURN_OK();
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

    if (data_version >= 5) {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BODYPARTCHARACTER) == NMO_OK) {
            out_state->has_character = 1;
            nmo_status_t result = nmo_chunk_read_object_id(chunk, &out_state->character_id);
            if (result != NMO_OK) return result;

            if ((out_state->base.entity.entity_flags & CK_3DENTITY_IKJOINTVALID) != 0) {
                out_state->has_rotation_joint = 1;
                result = nmo_chunk_read_and_fill_buffer(chunk,
                                                        &out_state->rotation_joint,
                                                        sizeof(nmo_ik_joint_t));
                if (result != NMO_OK) return result;
            }
        }
    } else {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BODYPARTROTJOINT) == NMO_OK) {
            nmo_vector_t vectors[6];
            memset(vectors, 0, sizeof(vectors));
            nmo_status_t result = nmo_chunk_read_and_fill_buffer(chunk, vectors, sizeof(vectors));
            if (result != NMO_OK) return result;

            out_state->has_rotation_joint = 1;
            out_state->rotation_joint.flags = 0;
            out_state->rotation_joint.min = vectors[3];
            out_state->rotation_joint.max = vectors[4];
            out_state->rotation_joint.damping = vectors[5];

            for (int i = 0; i < 3; ++i) {
                const float *v0 = &vectors[0].x + i;
                const float *v1 = &vectors[1].x + i;
                const float *v2 = &vectors[2].x + i;
                if (*v0 != 0.0f) out_state->rotation_joint.flags |= (1u << (i - 1));
                if (*v1 != 0.0f) out_state->rotation_joint.flags |= (16u << (i - 1));
                if (*v2 != 0.0f) out_state->rotation_joint.flags |= (256u << (i - 1));
            }
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BODYPARTCHARACTER) == NMO_OK) {
            out_state->has_character = 1;
            nmo_status_t result = nmo_chunk_read_object_id(chunk, &out_state->character_id);
            if (result != NMO_OK) return result;
        }
    }

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
        result = nmo_chunk_write_object_id(out_chunk, in_state->character_id);
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
    (void)type;
    nmo_character_state_t *out_state = (nmo_character_state_t *)instance;
    return nmo_character_deserialize_internal(chunk, context, out_state);
}

nmo_status_t nmo_character_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_character_state_t *in_state = (const nmo_character_state_t *)instance;
    return nmo_character_serialize_internal(in_state, out_chunk, context);
}

nmo_status_t nmo_bodypart_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_bodypart_state_t *out_state = (nmo_bodypart_state_t *)instance;
    return nmo_bodypart_deserialize_internal(chunk, context, out_state);
}

nmo_status_t nmo_bodypart_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_bodypart_state_t *in_state = (const nmo_bodypart_state_t *)instance;
    return nmo_bodypart_serialize_internal(in_state, out_chunk, context);
}

