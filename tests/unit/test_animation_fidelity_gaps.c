/**
 * @file test_animation_fidelity_gaps.c
 * @brief Animation details found when comparing saved data with what the engine reads
 */

#include "test_framework.h"

#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_statesave_ids.h"

#include <string.h>

#define CONTROLLER_LINPOS 0x637c4301u
#define UNKNOWN_IDENTIFIER 0x00700000u

static bool same_payload(const nmo_chunk_t *a, const nmo_chunk_t *b)
{
    return a->data.count == b->data.count &&
           memcmp(a->data.data, b->data.data,
                  a->data.count * sizeof(uint32_t)) == 0;
}

static bool payload_has_dword(const nmo_chunk_t *chunk, uint32_t value)
{
    const uint32_t *data = chunk->data.data;
    for (size_t i = 0; i < chunk->data.count; ++i) {
        if (data[i] == value) return true;
    }
    return false;
}

static uint32_t float_bits(float value)
{
    uint32_t bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static nmo_chunk_t *write_object_animation(
    nmo_arena_t *arena,
    const nmo_objectanimation_state_t *state,
    nmo_status_t *out_status)
{
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    chunk->class_id = NMO_CID_OBJECTANIMATION;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    *out_status = nmo_objectanimation_serialize(state, chunk, NULL, &ser_ctx);
    nmo_chunk_close(chunk);
    return chunk;
}

static nmo_status_t read_object_animation(
    nmo_arena_t *arena,
    nmo_chunk_t *chunk,
    nmo_objectanimation_state_t *loaded)
{
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_objectanimation_vtable.create(loaded, NULL, NULL);
    nmo_chunk_start_read(chunk);
    return nmo_objectanimation_deserialize(loaded, chunk, NULL, &des_ctx);
}

TEST(animation_fidelity_gaps, four_floats_after_the_root_vector_are_kept)
{
    /* RCKObjectAnimation::Load (0x10058c51) reads four floats after the root
     * vector of the SHARED, CONTROLLERS and NEWDATA sections and drops them. */
    const nmo_objectanimation_format_t formats[] = {
        CKOBJANIM_FORMAT_SHARED, CKOBJANIM_FORMAT_CONTROLLERS,
        CKOBJANIM_FORMAT_NEWDATA};
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
        ASSERT_NOT_NULL(arena);

        nmo_objectanimation_state_t source;
        ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(&source, NULL, NULL));
        source.format = formats[i];
        source.has_root_pos = 1;
        source.root_pos = (nmo_vector_t){1.0f, 2.0f, 3.0f};
        source.root_extra = (nmo_vector4_t){0.25f, -0.5f, 0.75f, 1.5f};
        if (formats[i] == CKOBJANIM_FORMAT_SHARED) {
            source.has_shared_anim = 1;
            source.shared_anim = nmo_ref_from_id(7u);
        } else {
            source.has_length = 1;
            source.length = 12.0f;
        }
        if (formats[i] == CKOBJANIM_FORMAT_NEWDATA) {
            source.has_morph_counts = 1;
        }

        nmo_status_t status = NMO_OK;
        nmo_chunk_t *first = write_object_animation(arena, &source, &status);
        ASSERT_EQ(NMO_OK, status);
        ASSERT_TRUE(payload_has_dword(first, float_bits(-0.5f)));
        ASSERT_TRUE(payload_has_dword(first, float_bits(1.5f)));

        nmo_objectanimation_state_t loaded;
        ASSERT_EQ(NMO_OK, read_object_animation(arena, first, &loaded));
        ASSERT_EQ(formats[i], loaded.format);
        ASSERT_FLOAT_EQ(1.0f, loaded.root_pos.x, 0.0f);
        ASSERT_FLOAT_EQ(0.25f, loaded.root_extra.x, 0.0f);
        ASSERT_FLOAT_EQ(-0.5f, loaded.root_extra.y, 0.0f);
        ASSERT_FLOAT_EQ(0.75f, loaded.root_extra.z, 0.0f);
        ASSERT_FLOAT_EQ(1.5f, loaded.root_extra.w, 0.0f);
        ASSERT_TRUE(nmo_objectanimation_vtable.equals(&source, &loaded));
        ASSERT_EQ(nmo_objectanimation_vtable.hash(&source),
                  nmo_objectanimation_vtable.hash(&loaded));

        /* The floats take part in comparison, hashing and copying. */
        nmo_objectanimation_state_t copied;
        ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(&copied, NULL, NULL));
        ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.copy(
            &loaded, &copied, NULL, arena));
        ASSERT_FLOAT_EQ(1.5f, copied.root_extra.w, 0.0f);
        copied.root_extra.w = 2.0f;
        ASSERT_FALSE(nmo_objectanimation_vtable.equals(&loaded, &copied));
        ASSERT_NE(nmo_objectanimation_vtable.hash(&loaded),
                  nmo_objectanimation_vtable.hash(&copied));

        nmo_chunk_t *second = write_object_animation(arena, &loaded, &status);
        ASSERT_EQ(NMO_OK, status);
        ASSERT_TRUE(same_payload(first, second));

        nmo_objectanimation_vtable.destroy(&source, NULL, NULL);
        nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
        nmo_objectanimation_vtable.destroy(&copied, NULL, NULL);
        nmo_arena_destroy(arena);
    }
}

TEST(animation_fidelity_gaps, animation_without_keyframe_section_keeps_the_default_length)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    /* Version 1 and later: no section leaves the constructor's keyframe data
     * in place, which is 100 frames long. */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    chunk->class_id = NMO_CID_OBJECTANIMATION;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, UNKNOWN_IDENTIFIER));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0x11223344u));
    nmo_chunk_close(chunk);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, read_object_animation(arena, chunk, &loaded));
    ASSERT_EQ(CKOBJANIM_FORMAT_NONE, loaded.format);
    ASSERT_EQ(0, loaded.has_length);
    ASSERT_FLOAT_EQ(100.0f, loaded.length, 0.0f);
    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);

    /* Version 0: the keyframe data is cleared first (length 0) and only the
     * length section sets it again. */
    chunk = nmo_chunk_create(arena);
    chunk->class_id = NMO_CID_OBJECTANIMATION;
    chunk->data_version = 0;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_OBJANIMFLAGS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, read_object_animation(arena, chunk, &loaded));
    ASSERT_EQ(CKOBJANIM_FORMAT_LEGACY, loaded.format);
    ASSERT_EQ(0, loaded.has_length);
    ASSERT_FLOAT_EQ(0.0f, loaded.length, 0.0f);
    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);

    chunk = nmo_chunk_create(arena);
    chunk->class_id = NMO_CID_OBJECTANIMATION;
    chunk->data_version = 0;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_OBJANIMLENGTH));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 42.0f));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, read_object_animation(arena, chunk, &loaded));
    ASSERT_EQ(1, loaded.has_length);
    ASSERT_FLOAT_EQ(42.0f, loaded.length, 0.0f);
    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);

    nmo_arena_destroy(arena);
}

/* Write the sections of an animation chunk without keyframe section. */
static nmo_chunk_t *write_unread_sections_chunk(
    nmo_arena_t *arena,
    const uint32_t *identifiers,
    size_t count)
{
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    chunk->class_id = NMO_CID_OBJECTANIMATION;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_start_write(chunk);
    for (size_t i = 0; i < count; ++i) {
        nmo_chunk_write_identifier(chunk, identifiers[i]);
        if (identifiers[i] == UNKNOWN_IDENTIFIER ||
            identifiers[i] == UNKNOWN_IDENTIFIER + 1u) {
            nmo_chunk_write_dword(chunk, 0x11223344u + identifiers[i]);
            nmo_chunk_write_dword(chunk, 0x55667788u);
        }
    }
    nmo_chunk_close(chunk);
    return chunk;
}

TEST(animation_fidelity_gaps, unread_sections_keep_their_chain_beside_the_base_object_sections)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    /* Without a keyframe section the sections are kept for writing back. Those
     * of the base object are written by the state, so keeping them as well
     * would repeat them; a section before them must not be lost, and the
     * links of the identifier chain must still lead through every section. */
    const uint32_t visible[] = {UNKNOWN_IDENTIFIER};
    const uint32_t hidden_first[] = {CK_STATESAVE_OBJECTHIDDEN, UNKNOWN_IDENTIFIER};
    const uint32_t hidden_last[] = {UNKNOWN_IDENTIFIER, CK_STATESAVE_OBJECTHIDDEN};
    const uint32_t both[] = {
        CK_STATESAVE_OBJECTHIDDEN, CK_STATESAVE_OBJECTHIERAHIDDEN,
        UNKNOWN_IDENTIFIER, UNKNOWN_IDENTIFIER + 1u};
    const struct {
        const uint32_t *identifiers;
        size_t count;
        size_t expected_dwords;
    } cases[] = {
        {visible, 1u, 4u},
        {hidden_first, 2u, 6u},
        {hidden_last, 2u, 6u},
        {both, 4u, 12u},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        nmo_chunk_t *chunk = write_unread_sections_chunk(
            arena, cases[i].identifiers, cases[i].count);
        ASSERT_EQ(cases[i].expected_dwords, chunk->data.count);

        nmo_objectanimation_state_t loaded;
        ASSERT_EQ(NMO_OK, read_object_animation(arena, chunk, &loaded));
        ASSERT_EQ(CKOBJANIM_FORMAT_NONE, loaded.format);
        ASSERT_TRUE(loaded.raw_tail_size > 0u);

        nmo_status_t status = NMO_OK;
        nmo_chunk_t *saved = write_object_animation(arena, &loaded, &status);
        ASSERT_EQ(NMO_OK, status);
        ASSERT_EQ(cases[i].expected_dwords, saved->data.count);

        /* Every section is still reachable and the state reads back the same. */
        nmo_objectanimation_state_t reloaded;
        ASSERT_EQ(NMO_OK, read_object_animation(arena, saved, &reloaded));
        ASSERT_TRUE(nmo_objectanimation_vtable.equals(&loaded, &reloaded));
        ASSERT_EQ(loaded.base.base.visibility_flags,
                  reloaded.base.base.visibility_flags);
        ASSERT_EQ(NMO_OK, nmo_chunk_start_read(saved));
        size_t found = 0u;
        for (size_t k = 0; k < cases[i].count; ++k) {
            size_t dwords = 0u;
            if (nmo_chunk_seek_identifier_with_size(
                    saved, cases[i].identifiers[k], &dwords) == NMO_OK) {
                ++found;
            }
        }
        ASSERT_EQ(cases[i].count, found);

        nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
        nmo_objectanimation_vtable.destroy(&reloaded, NULL, NULL);
    }

    /* Bytes that are not an identifier chain are written as they are. */
    uint8_t opaque[12] = {0x44, 0x33, 0x22, 0x11, 1, 0, 0, 0, 0x88, 0x77, 0x66, 0x55};
    nmo_objectanimation_state_t authored;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(&authored, NULL, NULL));
    authored.raw_tail = opaque;
    authored.raw_tail_size = sizeof(opaque);
    nmo_status_t status = NMO_OK;
    nmo_chunk_t *saved = write_object_animation(arena, &authored, &status);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(3u, saved->data.count);
    ASSERT_EQ(0, memcmp(opaque, saved->data.data, sizeof(opaque)));
    authored.raw_tail = NULL;
    authored.raw_tail_size = 0u;
    nmo_objectanimation_vtable.destroy(&authored, NULL, NULL);

    nmo_arena_destroy(arena);
}

TEST(animation_fidelity_gaps, repeated_controller_type_is_kept_in_controllers_format)
{
    /* CKKeyframeData::CreateController (0x1004a922) replaces the controller of
     * a slot, so the engine lets the later one win. The CONTROLLERS format
     * holds both, in the order of the file. */
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    float first_keys[4] = {0.0f, 1.0f, 2.0f, 3.0f};
    float second_keys[4] = {5.0f, 6.0f, 7.0f, 8.0f};
    nmo_objanim_controller_t controllers[2] = {
        {.type = CONTROLLER_LINPOS, .key_count = 1u,
         .data_size = sizeof(first_keys), .data = first_keys},
        {.type = CONTROLLER_LINPOS, .key_count = 1u,
         .data_size = sizeof(second_keys), .data = second_keys},
    };
    nmo_objectanimation_state_t source;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(&source, NULL, NULL));
    source.format = CKOBJANIM_FORMAT_CONTROLLERS;
    source.has_length = 1;
    source.length = 10.0f;
    source.controller_count = 2u;
    source.controllers = controllers;

    nmo_status_t status = NMO_OK;
    nmo_chunk_t *first = write_object_animation(arena, &source, &status);
    ASSERT_EQ(NMO_OK, status);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, read_object_animation(arena, first, &loaded));
    ASSERT_EQ(2u, loaded.controller_count);
    ASSERT_EQ(CONTROLLER_LINPOS, loaded.controllers[0].type);
    ASSERT_EQ(CONTROLLER_LINPOS, loaded.controllers[1].type);
    ASSERT_EQ(0, memcmp(first_keys, loaded.controllers[0].data, sizeof(first_keys)));
    ASSERT_EQ(0, memcmp(second_keys, loaded.controllers[1].data, sizeof(second_keys)));

    nmo_chunk_t *second = write_object_animation(arena, &loaded, &status);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_TRUE(same_payload(first, second));

    /* NEWDATA and LEGACY files have one fixed slot per controller type, so a
     * repeated type cannot be written there without dropping a controller. */
    source.format = CKOBJANIM_FORMAT_NEWDATA;
    source.has_morph_counts = 1;
    (void)write_object_animation(arena, &source, &status);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, status);

    nmo_objectanimation_vtable.destroy(&source, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(animation_fidelity_gaps, keyed_animation_keeps_the_sorted_flag_the_file_holds)
{
    /* RCKKeyedAnimation::Load (0x10049e10) clears 0x40 once the animations are
     * read; SetStep sets it again after sorting them (0x10048282). The engine
     * saves the flags as they are, so files hold the bit, and libnmo keeps it
     * so a loaded file is written back unchanged. */
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_keyedanimation_state_t source;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(&source, NULL, NULL));
    source.base.flags = 0x45u;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    chunk->class_id = NMO_CID_KEYEDANIMATION;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_serialize(&source, chunk, NULL, &ser_ctx));
    nmo_chunk_close(chunk);

    nmo_keyedanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_deserialize(&loaded, chunk, NULL, &des_ctx));
    ASSERT_EQ(0x45u, loaded.base.flags);

    nmo_chunk_t *again = nmo_chunk_create(arena);
    again->class_id = NMO_CID_KEYEDANIMATION;
    again->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_serialize(&loaded, again, NULL, &ser_ctx));
    nmo_chunk_close(again);
    ASSERT_TRUE(same_payload(chunk, again));

    nmo_keyedanimation_vtable.destroy(&source, NULL, NULL);
    nmo_keyedanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(animation_fidelity_gaps, old_chunk_version_object_arrays_use_the_plain_id_layout)
{
    /* XSObjectPointerArray::Load (0x2402b715) and ReadXObjectArray (0x24022ac0)
     * read a non-zero lead dword, skip 4 dwords, read a count and then plain
     * object ids when the chunk version is below 4. */
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    chunk->class_id = NMO_CID_KEYEDANIMATION;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    /* Body parts: two ids, then the root entity. */
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_ANIMATIONBODYPARTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 1u));
    for (int i = 0; i < 4; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0xdeadbeefu));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 21u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 22u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 23u));
    /* Animation list: three ids, the middle one empty. */
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_KEYEDANIMANIMLIST));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 1u));
    for (int i = 0; i < 4; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0xdeadbeefu));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 11u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 13u));
    nmo_chunk_close(chunk);
    chunk->chunk_version = 3;

    nmo_keyedanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_deserialize(&loaded, chunk, NULL, &des_ctx));
    ASSERT_EQ(2u, loaded.base.legacy_body_part_count);
    ASSERT_EQ(21u, loaded.base.legacy_body_parts[0].raw_id);
    ASSERT_EQ(22u, loaded.base.legacy_body_parts[1].raw_id);
    ASSERT_EQ(23u, loaded.base.root_entity.raw_id);
    ASSERT_EQ(3u, loaded.animation_count);
    ASSERT_EQ(11u, loaded.animation_ids[0].raw_id);
    ASSERT_EQ(NMO_REF_NONE, loaded.animation_ids[1].state);
    ASSERT_EQ(13u, loaded.animation_ids[2].raw_id);
    nmo_keyedanimation_vtable.destroy(&loaded, NULL, NULL);

    /* A zero lead dword ends the array whatever follows it. */
    chunk = nmo_chunk_create(arena);
    chunk->class_id = NMO_CID_KEYEDANIMATION;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_KEYEDANIMANIMLIST));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
    nmo_chunk_close(chunk);
    chunk->chunk_version = 3;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_deserialize(&loaded, chunk, NULL, &des_ctx));
    ASSERT_EQ(0u, loaded.animation_count);
    nmo_keyedanimation_vtable.destroy(&loaded, NULL, NULL);

    /* A count that runs past the section is refused. */
    chunk = nmo_chunk_create(arena);
    chunk->class_id = NMO_CID_KEYEDANIMATION;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_KEYEDANIMANIMLIST));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 1u));
    for (int i = 0; i < 4; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 9));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 5u));
    nmo_chunk_close(chunk);
    chunk->chunk_version = 3;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_keyedanimation_deserialize(
        &loaded, chunk, NULL, &des_ctx));
    nmo_keyedanimation_vtable.destroy(&loaded, NULL, NULL);

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(animation_fidelity_gaps, four_floats_after_the_root_vector_are_kept);
    REGISTER_TEST(animation_fidelity_gaps, animation_without_keyframe_section_keeps_the_default_length);
    REGISTER_TEST(animation_fidelity_gaps, unread_sections_keep_their_chain_beside_the_base_object_sections);
    REGISTER_TEST(animation_fidelity_gaps, repeated_controller_type_is_kept_in_controllers_format);
    REGISTER_TEST(animation_fidelity_gaps, keyed_animation_keeps_the_sorted_flag_the_file_holds);
    REGISTER_TEST(animation_fidelity_gaps, old_chunk_version_object_arrays_use_the_plain_id_layout);
TEST_MAIN_END()
