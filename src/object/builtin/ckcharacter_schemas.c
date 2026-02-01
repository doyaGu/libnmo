/**
 * @file ckcharacter_schemas.c
 * @brief CKCharacter and CKBodyPart schema implementation
 */

#include "object/nmo_ckcharacter_schemas.h"
#include "object/nmo_schema_registry.h"
#include "object/nmo_schema_builder.h"
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
    nmo_arena_t *arena,
    nmo_ckcharacter_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcharacter_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_result_t result = nmo_ck3dentity_deserialize(chunk, arena, &out_state->base);
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
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckcharacter_serialize"));
    }

    nmo_result_t result = nmo_ck3dentity_serialize(&in_state->base, out_chunk, arena);
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
            result = nmo_chunk_write_sub_chunk(out_chunk, sub);
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
    nmo_arena_t *arena,
    nmo_ckbodypart_state_t *out_state)
{
    if (!chunk || !out_state) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbodypart_deserialize"));
    }

    memset(out_state, 0, sizeof(*out_state));

    nmo_ck3dobject_deserialize_fn base_deserialize = nmo_get_ck3dobject_deserialize();
    if (base_deserialize) {
        nmo_result_t result = base_deserialize(chunk, arena, &out_state->base);
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
    nmo_arena_t *arena)
{
    if (!in_state || !out_chunk) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_ckbodypart_serialize"));
    }

    nmo_ck3dobject_serialize_fn base_serialize = nmo_get_ck3dobject_serialize();
    if (base_serialize) {
        nmo_result_t result = base_serialize(&in_state->base, out_chunk, arena);
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

static nmo_result_t nmo_ckcharacter_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_ckcharacter_deserialize_internal(chunk, arena, (nmo_ckcharacter_state_t *)out_ptr);
}

static nmo_result_t nmo_ckcharacter_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckcharacter_serialize_internal((const nmo_ckcharacter_state_t *)in_ptr, chunk, arena);
}

static nmo_result_t nmo_ckbodypart_vtable_read(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    void *out_ptr)
{
    (void)type;
    return nmo_ckbodypart_deserialize_internal(chunk, arena, (nmo_ckbodypart_state_t *)out_ptr);
}

static nmo_result_t nmo_ckbodypart_vtable_write(
    const nmo_schema_type_t *type,
    nmo_chunk_t *chunk,
    const void *in_ptr,
    nmo_arena_t *arena)
{
    (void)type;
    return nmo_ckbodypart_serialize_internal((const nmo_ckbodypart_state_t *)in_ptr, chunk, arena);
}

static const nmo_schema_vtable_t nmo_ckcharacter_vtable = {
    .read = nmo_ckcharacter_vtable_read,
    .write = nmo_ckcharacter_vtable_write,
    .validate = NULL
};

static const nmo_schema_vtable_t nmo_ckbodypart_vtable = {
    .read = nmo_ckbodypart_vtable_read,
    .write = nmo_ckbodypart_vtable_write,
    .validate = NULL
};

nmo_result_t nmo_register_ckcharacter_schemas(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena)
{
    if (!registry || !arena) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments to nmo_register_ckcharacter_schemas"));
    }

    const nmo_schema_type_t *uint32_type = nmo_schema_registry_find_by_name(registry, "u32");
    if (!uint32_type) {
        return nmo_result_error(NMO_ERROR(arena, NMO_ERR_NOT_FOUND,
            NMO_SEVERITY_ERROR, "Required types not found in registry"));
    }

    nmo_schema_builder_t character_builder = nmo_builder_struct(arena, "CKCharacterState",
                                                                sizeof(nmo_ckcharacter_state_t),
                                                                alignof(nmo_ckcharacter_state_t));
    nmo_builder_add_field_ex(&character_builder, "body_part_count", uint32_type,
                            offsetof(nmo_ckcharacter_state_t, body_part_count), 0);
    nmo_builder_add_field_ex(&character_builder, "animation_count", uint32_type,
                            offsetof(nmo_ckcharacter_state_t, animation_count), 0);
    nmo_builder_set_vtable(&character_builder, &nmo_ckcharacter_vtable);
    nmo_result_t result = nmo_builder_build(&character_builder, registry);
    if (result.code != NMO_OK) return result;

    nmo_schema_builder_t bodypart_builder = nmo_builder_struct(arena, "CKBodyPartState",
                                                               sizeof(nmo_ckbodypart_state_t),
                                                               alignof(nmo_ckbodypart_state_t));
    nmo_builder_add_field_ex(&bodypart_builder, "character_id", uint32_type,
                            offsetof(nmo_ckbodypart_state_t, character_id), 0);
    nmo_builder_set_vtable(&bodypart_builder, &nmo_ckbodypart_vtable);

    return nmo_builder_build(&bodypart_builder, registry);
}

nmo_result_t nmo_ckcharacter_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckcharacter_state_t *out_state)
{
    return nmo_ckcharacter_deserialize_internal(chunk, arena, out_state);
}

nmo_result_t nmo_ckcharacter_serialize(
    const nmo_ckcharacter_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    return nmo_ckcharacter_serialize_internal(in_state, out_chunk, arena);
}

nmo_result_t nmo_ckbodypart_deserialize(
    nmo_chunk_t *chunk,
    nmo_arena_t *arena,
    nmo_ckbodypart_state_t *out_state)
{
    return nmo_ckbodypart_deserialize_internal(chunk, arena, out_state);
}

nmo_result_t nmo_ckbodypart_serialize(
    const nmo_ckbodypart_state_t *in_state,
    nmo_chunk_t *out_chunk,
    nmo_arena_t *arena)
{
    return nmo_ckbodypart_serialize_internal(in_state, out_chunk, arena);
}
