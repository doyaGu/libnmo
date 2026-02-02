/**
 * @file ckcharacter_schemas.c
 * @brief CKCharacter and CKBodyPart schema implementation
 */

#include "object/nmo_ckcharacter_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_type_common.h"
#include "object/nmo_schema_interface.h"
#include "object/nmo_class_ids.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "core/nmo_error.h"
#include "core/nmo_arena.h"
#include <string.h>

#define CK_STATESAVE_BODYPARTROTJOINT   0x01000000u
#define CK_STATESAVE_BODYPARTCHARACTER  0x04000000u

#define CK_STATESAVE_CHARACTERBODYPARTS  0x00400000u
#define CK_STATESAVE_CHARACTERANIMATIONS 0x01000000u
#define CK_STATESAVE_CHARACTERROOT       0x02000000u
#define CK_STATESAVE_CHARACTERSAVEANIMS  0x04000000u
#define CK_STATESAVE_CHARACTERSAVEPARTS  0x10000000u
#define CK_STATESAVE_CHARACTERFLOORREF   0x20000000u
#define CK_STATESAVE_CHARACTERONLY       0xFFC00000u

static int nmo_chunk_is_file_mode(const nmo_chunk_t *chunk) {
    return chunk && (chunk->chunk_options & NMO_CHUNK_OPTION_FILE);
}

static nmo_result_t read_object_sequence(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_object_id_t **out_ids,
    uint32_t *out_count)
{
    size_t count = 0;
    nmo_result_t result = nmo_chunk_read_object_sequence_start(chunk, &count);
    if (result.code != NMO_OK) return result;

    if (count == 0) {
        *out_ids = NULL;
        *out_count = 0;
        return nmo_result_ok();
    }

    *out_count = (uint32_t)count;
    *out_ids = (nmo_object_id_t *)nmo_arena_alloc(
        arena, sizeof(nmo_object_id_t) * (*out_count), _Alignof(nmo_object_id_t));
    if (!*out_ids) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
            NMO_SEVERITY_ERROR, "Failed to allocate object ID array"));
    }

    for (uint32_t i = 0; i < *out_count; ++i) {
        result = nmo_chunk_read_object_sequence_item(chunk, &(*out_ids)[i]);
        if (result.code != NMO_OK) {
            *out_count = i;
            break;
        }
    }

    return nmo_result_ok();
}


/* ============================================================================
 * Vtable + registration
 * ============================================================================ */

NMO_DEFINE_OBJECT_SCHEMA(
    ckcharacter,
    nmo_ckcharacter_state_t,
    nmo_ckcharacter_serialize,
    nmo_ckcharacter_deserialize,
    NMO_GUID_CKCHARACTER,
    "CKCharacter",
    NMO_CID_CHARACTER,
    NMO_GUID_CK3DENTITY
)

NMO_DEFINE_OBJECT_SCHEMA(
    ckbodypart,
    nmo_ckbodypart_state_t,
    nmo_ckbodypart_serialize,
    nmo_ckbodypart_deserialize,
    NMO_GUID_CKBODYPART,
    "CKBodyPart",
    NMO_CID_BODYPART,
    NMO_GUID_CK3DOBJECT
)

static nmo_result_t write_object_sequence(
    nmo_chunk_t *chunk,
    const nmo_object_id_t *ids,
    uint32_t count)
{
    nmo_result_t result = nmo_chunk_write_object_sequence_start(chunk, count);
    if (result.code != NMO_OK) return result;

    for (uint32_t i = 0; i < count; ++i) {
        result = nmo_chunk_write_object_sequence_item(chunk, ids[i]);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckcharacter_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_ckcharacter_state_t *out_state)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcharacter_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_ck3dentity_deserialize(&out_state->base, chunk, NULL, context);
    if (result.code != NMO_OK) return result;

    uint32_t data_version = nmo_chunk_get_data_version(chunk);

    if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERBODYPARTS).code == NMO_OK) {
        (void)read_object_sequence(chunk, arena,
                                   &out_state->body_part_ids,
                                   &out_state->body_part_count);
    }

    if (data_version < 5) {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERANIMATIONS).code == NMO_OK) {
            (void)read_object_sequence(chunk, arena,
                                       &out_state->animation_ids,
                                       &out_state->animation_count);
            (void)nmo_chunk_read_object_id(chunk, &out_state->active_animation_id);
            (void)nmo_chunk_read_object_id(chunk, &out_state->anim_dest_id);
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERSAVEANIMS).code == NMO_OK) {
            uint32_t unused = 0;
            (void)nmo_chunk_read_dword(chunk, &unused);
            (void)nmo_chunk_read_object_id(chunk, &out_state->active_animation_id);
            (void)nmo_chunk_read_object_id(chunk, &out_state->anim_dest_id);
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERSAVEPARTS).code == NMO_OK) {
            uint32_t count = 0;
            (void)nmo_chunk_read_dword(chunk, &count);
            if (count > 0) {
                out_state->subpart_count = count;
                out_state->subparts = (nmo_ckcharacter_subpart_t *)nmo_arena_alloc(
                    arena, sizeof(nmo_ckcharacter_subpart_t) * count,
                    _Alignof(nmo_ckcharacter_subpart_t));
                if (!out_state->subparts) {
                    return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                        NMO_SEVERITY_ERROR, "Failed to allocate subpart array"));
                }

                for (uint32_t i = 0; i < count; ++i) {
                    (void)nmo_chunk_read_object_id(chunk, &out_state->subparts[i].object_id);
                    (void)nmo_chunk_read_sub_chunk(chunk, &out_state->subparts[i].chunk);
                }
            }
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERROOT).code == NMO_OK) {
            (void)nmo_chunk_read_object_id(chunk, &out_state->root_body_part_id);
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERFLOORREF).code == NMO_OK) {
            (void)nmo_chunk_read_object_id(chunk, &out_state->floor_ref_id);
        }
    } else {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERSAVEPARTS).code == NMO_OK) {
            size_t count = 0;
            (void)nmo_chunk_start_read_sub_chunk_sequence(chunk, &count);
            if (count > 0) {
                out_state->subpart_count = (uint32_t)count;
                out_state->subparts = (nmo_ckcharacter_subpart_t *)nmo_arena_alloc(
                    arena, sizeof(nmo_ckcharacter_subpart_t) * out_state->subpart_count,
                    _Alignof(nmo_ckcharacter_subpart_t));
                if (!out_state->subparts) {
                    return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOMEM,
                        NMO_SEVERITY_ERROR, "Failed to allocate subpart array"));
                }

                for (uint32_t i = 0; i < out_state->subpart_count; ++i) {
                    out_state->subparts[i].object_id = 0;
                    (void)nmo_chunk_read_sub_chunk(chunk, &out_state->subparts[i].chunk);
                }
            }
        }

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_CHARACTERONLY).code == NMO_OK) {
            if (nmo_chunk_is_file_mode(chunk)) {
                (void)read_object_sequence(chunk, arena,
                                           &out_state->animation_ids,
                                           &out_state->animation_count);
            }
            size_t seq_count = 0;
            if (nmo_chunk_read_object_sequence_start(chunk, &seq_count).code == NMO_OK && seq_count >= 4) {
                (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->active_animation_id);
                (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->anim_dest_id);
                (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->root_body_part_id);
                (void)nmo_chunk_read_object_sequence_item(chunk, &out_state->floor_ref_id);
            }
        }
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckcharacter_serialize_internal(
    const nmo_ckcharacter_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcharacter_serialize"));
    }

    nmo_result_t result = nmo_ck3dentity_serialize(&in_state->base, out_chunk, NULL, context);
    if (result.code != NMO_OK) return result;

    if (in_state->body_part_count > 0 && in_state->body_part_ids) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CHARACTERBODYPARTS);
        if (result.code != NMO_OK) return result;
        result = write_object_sequence(out_chunk, in_state->body_part_ids, in_state->body_part_count);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->subpart_count > 0 && in_state->subparts) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CHARACTERSAVEPARTS);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_start_sub_chunk_sequence(out_chunk, in_state->subpart_count);
        if (result.code != NMO_OK) return result;
        for (uint32_t i = 0; i < in_state->subpart_count; ++i) {
            nmo_chunk_t *sub = in_state->subparts[i].chunk;
            if (!sub) {
                sub = nmo_chunk_create(arena);
            }
            result = nmo_chunk_write_sub_chunk_sequence(out_chunk, sub);
            if (result.code != NMO_OK) return result;
        }
    }

    if (in_state->animation_count > 0 || in_state->active_animation_id ||
        in_state->anim_dest_id || in_state->root_body_part_id || in_state->floor_ref_id) {
        result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_CHARACTERONLY);
        if (result.code != NMO_OK) return result;
        if (nmo_chunk_is_file_mode(out_chunk)) {
            if (in_state->animation_count > 0 && in_state->animation_ids) {
                result = write_object_sequence(out_chunk, in_state->animation_ids,
                                               in_state->animation_count);
                if (result.code != NMO_OK) return result;
            } else {
                result = nmo_chunk_write_object_sequence_start(out_chunk, 0);
                if (result.code != NMO_OK) return result;
            }
        }

        result = nmo_chunk_write_object_sequence_start(out_chunk, 4);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->active_animation_id);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->anim_dest_id);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->root_body_part_id);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_sequence_item(out_chunk, in_state->floor_ref_id);
        if (result.code != NMO_OK) return result;
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckbodypart_deserialize_internal(
    nmo_chunk_t *chunk,
    void *context,
    nmo_ckbodypart_state_t *out_state)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbodypart_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    {
        nmo_result_t result = nmo_ck3dobject_deserialize(&out_state->base, chunk, NULL, context);
        if (result.code != NMO_OK) return result;
    }

    uint32_t data_version = nmo_chunk_get_data_version(chunk);

    if (data_version >= 5) {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BODYPARTCHARACTER).code == NMO_OK) {
            out_state->has_character = 1;
            (void)nmo_chunk_read_object_id(chunk, &out_state->character_id);

            size_t remaining_dwords = 0;
            size_t pos = nmo_chunk_get_position(chunk);
            size_t total = nmo_chunk_get_data_size(chunk) / 4;
            if (pos < total) {
                remaining_dwords = total - pos;
            }
            if (remaining_dwords * 4 >= sizeof(nmo_ckik_joint_t)) {
                out_state->has_rotation_joint = 1;
                (void)nmo_chunk_read_and_fill_buffer(chunk,
                                                     &out_state->rotation_joint,
                                                     sizeof(nmo_ckik_joint_t));
            }
        }
    } else {
        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BODYPARTROTJOINT).code == NMO_OK) {
            nmo_vector_t vectors[6];
            memset(vectors, 0, sizeof(vectors));
            (void)nmo_chunk_read_and_fill_buffer(chunk, vectors, sizeof(vectors));

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

        if (nmo_chunk_seek_identifier(chunk, CK_STATESAVE_BODYPARTCHARACTER).code == NMO_OK) {
            out_state->has_character = 1;
            (void)nmo_chunk_read_object_id(chunk, &out_state->character_id);
        }
    }

    return nmo_result_ok();
}

static nmo_result_t nmo_ckbodypart_serialize_internal(
    const nmo_ckbodypart_state_t *in_state,
    nmo_chunk_t *out_chunk,
    void *context)
{
    nmo_arena_t *arena = nmo_serialize_context_get_arena(context);
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbodypart_serialize"));
    }

    {
        nmo_result_t result = nmo_ck3dobject_serialize(&in_state->base, out_chunk, NULL, context);
        if (result.code != NMO_OK) return result;
    }

    if (in_state->has_character || in_state->has_rotation_joint) {
        nmo_result_t result = nmo_chunk_write_identifier(out_chunk, CK_STATESAVE_BODYPARTCHARACTER);
        if (result.code != NMO_OK) return result;
        result = nmo_chunk_write_object_id(out_chunk, in_state->character_id);
        if (result.code != NMO_OK) return result;

        if (in_state->has_rotation_joint) {
            result = nmo_chunk_write_buffer_no_size(out_chunk,
                                                    &in_state->rotation_joint,
                                                    sizeof(nmo_ckik_joint_t));
            if (result.code != NMO_OK) return result;
        }
    }

    return nmo_result_ok();
}

nmo_result_t nmo_ckcharacter_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckcharacter_state_t *out_state = (nmo_ckcharacter_state_t *)instance;
    return nmo_ckcharacter_deserialize_internal(chunk, context, out_state);
}

nmo_result_t nmo_ckcharacter_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckcharacter_state_t *in_state = (const nmo_ckcharacter_state_t *)instance;
    return nmo_ckcharacter_serialize_internal(in_state, out_chunk, context);
}

nmo_result_t nmo_ckbodypart_deserialize(
    void *instance,
    nmo_chunk_t *chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    nmo_ckbodypart_state_t *out_state = (nmo_ckbodypart_state_t *)instance;
    return nmo_ckbodypart_deserialize_internal(chunk, context, out_state);
}

nmo_result_t nmo_ckbodypart_serialize(
    const void *instance,
    nmo_chunk_t *out_chunk,
    const nmo_type_descriptor_t *type,
    void *context)
{
    (void)type;
    const nmo_ckbodypart_state_t *in_state = (const nmo_ckbodypart_state_t *)instance;
    return nmo_ckbodypart_serialize_internal(in_state, out_chunk, context);
}
