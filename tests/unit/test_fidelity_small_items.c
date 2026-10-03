/**
 * @file test_fidelity_small_items.c
 * @brief Small details found when comparing saved data with what the engine reads
 */

#include "test_framework.h"

#include "core/nmo_arena.h"
#include "core/nmo_hash.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_layer_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_spritetext_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_statesave_ids.h"

#include <string.h>

#define CONTROLLER_LINPOS 0x637c4301u

TEST(fidelity_small_items, sprite_text_font_matches_native_save_order)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    /* CK2_3D.dll 0x100621FF writes size, weight, italic, underline. */
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_SPRITEFONT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(chunk, "Arial"));
    const int32_t expected[] = {24, 700, 1, 0};
    for (size_t i = 0; i < 4u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, expected[i]));
    }
    nmo_chunk_close(chunk);

    nmo_spritetext_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_spritetext_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_spritetext_deserialize(&loaded, chunk, NULL, &des_ctx));
    ASSERT_EQ(24, loaded.font.size);
    ASSERT_EQ(700, loaded.font.weight);
    ASSERT_EQ(1, loaded.font.italic);
    ASSERT_EQ(0, loaded.font.underline);

    nmo_chunk_t *saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(saved);
    ASSERT_EQ(NMO_OK, nmo_spritetext_serialize(&loaded, saved, NULL, &ser_ctx));
    nmo_chunk_close(saved);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(saved, CK_STATESAVE_SPRITEFONT));
    char *name = NULL;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_string_checked(saved, &name, NULL));
    ASSERT_STR_EQ("Arial", name);
    for (size_t i = 0; i < 4u; ++i) {
        int32_t actual = 0;
        ASSERT_EQ(NMO_OK, nmo_chunk_read_int(saved, &actual));
        ASSERT_EQ(expected[i], actual);
    }
    nmo_spritetext_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(fidelity_small_items, sprite_text_save_uses_only_the_2d_entity_base)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_spritetext_state_t source;
    ASSERT_EQ(NMO_OK, nmo_spritetext_vtable.create(&source, NULL, NULL));
    source.base.has_transparency = true;
    source.base.transparent_color = 0x12345678u;
    source.base.has_slot = true;
    source.base.current_slot = 3u;
    source.base.has_save_options = true;

    for (size_t file_mode = 0; file_mode < 2u; ++file_mode) {
        nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
            arena, NULL, file_mode ? NMO_SERIALIZE_FLAG_FILE_MODE : 0u,
            CK_STATESAVE_SPRITETEXTALL);
        nmo_chunk_t *chunk = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(chunk);
        ASSERT_EQ(NMO_OK, nmo_spritetext_serialize(&source, chunk, NULL, &ser_ctx));
        nmo_chunk_close(chunk);
        ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITETRANSPARENT));
        ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITECURRENTIMAGE));
        ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITEFORMAT));
        ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(chunk, CK_STATESAVE_SPRITEFONT));
    }
    nmo_spritetext_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(fidelity_small_items, legacy_bodypart_joint_uses_integer_booleans)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(
        arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->data_version = 4u;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, CK_STATESAVE_BODYPARTROTJOINT));
    /* Integer 0x80000000 is true, although its float representation is -0. */
    const uint32_t words[] = {0x80000000u, 1u, 0u, 1u, 0u, 0x80000000u, 0u, 0x80000000u, 0u};
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 72u));
    for (size_t i = 0; i < 9u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, words[i]));
    }
    for (size_t i = 0; i < 9u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, (float)i + 0.25f));
    }
    nmo_chunk_close(chunk);
    nmo_bodypart_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_bodypart_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_bodypart_deserialize(&loaded, chunk, NULL, &des_ctx));
    ASSERT_EQ(0x80000121u, loaded.rotation_joint.flags);
    ASSERT_EQ(0.25f, loaded.rotation_joint.min.x);
    ASSERT_EQ(4.25f, loaded.rotation_joint.max.y);
    ASSERT_EQ(8.25f, loaded.rotation_joint.damping.z);

    nmo_chunk_t *saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(saved);
    saved->data_version = 4u;
    ASSERT_EQ(NMO_OK, nmo_bodypart_serialize(&loaded, saved, NULL, &ser_ctx));
    nmo_chunk_close(saved);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(saved, CK_STATESAVE_BODYPARTROTJOINT));
    uint32_t size = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(saved, &size));
    ASSERT_EQ(72u, size);
    const uint32_t canonical[] = {1u, 1u, 0u, 0u, 0u, 1u, 0u, 1u, 0u};
    for (size_t i = 0; i < 9u; ++i) {
        uint32_t actual = 0;
        ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(saved, &actual));
        ASSERT_EQ(canonical[i], actual);
    }
    nmo_bodypart_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(fidelity_small_items, empty_controller_is_written_with_a_key_count)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    nmo_objanim_controller_t controller = {
        .type = CONTROLLER_LINPOS, .key_count = 0u, .data_size = 0u, .data = NULL};
    nmo_objectanimation_state_t source;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(&source, NULL, NULL));
    source.format = CKOBJANIM_FORMAT_CONTROLLERS;
    source.controller_count = 1u;
    source.controllers = &controller;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_serialize(&source, chunk, NULL, &ser_ctx));
    nmo_chunk_close(chunk);

    /* The engine reads a key count from every blob: {type, 1, 0}. */
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(&loaded, chunk, NULL, &des_ctx));
    ASSERT_EQ(1u, loaded.controller_count);
    ASSERT_EQ(CONTROLLER_LINPOS, loaded.controllers[0].type);
    ASSERT_EQ(sizeof(uint32_t), loaded.controllers[0].data_size);
    uint32_t count = 1u;
    memcpy(&count, loaded.controllers[0].data, sizeof(count));
    ASSERT_EQ(0u, count);

    nmo_objectanimation_vtable.destroy(&source, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(fidelity_small_items, new_mesh_starts_visible_with_render_channels)
{
    nmo_mesh_state_t mesh;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&mesh, NULL, NULL));
    ASSERT_EQ(0x0Au, mesh.flags);
    nmo_mesh_vtable.destroy(&mesh, NULL, NULL);
}

TEST(fidelity_small_items, sprite_text_load_keeps_the_ratio_offset_flag)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    /* A new sprite text starts without the flag the 2D entity constructor sets. */
    nmo_spritetext_state_t source;
    ASSERT_EQ(NMO_OK, nmo_spritetext_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(0u, source.base.entity.flags & CK_2DENTITY_RATIOOFFSET);

    /* A file may still have it set, and Load takes the flags of the file. */
    source.base.entity.flags |= CK_2DENTITY_RATIOOFFSET;
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_spritetext_serialize(&source, chunk, NULL, &ser_ctx));
    nmo_chunk_close(chunk);

    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_spritetext_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_spritetext_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_spritetext_deserialize(&loaded, chunk, NULL, &des_ctx));
    ASSERT_TRUE((loaded.base.entity.flags & CK_2DENTITY_RATIOOFFSET) != 0u);

    nmo_spritetext_vtable.destroy(&source, NULL, NULL);
    nmo_spritetext_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(fidelity_small_items, replacing_a_bitmap_replaces_the_whole_image)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 1 << 20);
    ASSERT_NOT_NULL(arena);

    nmo_texture_state_t texture;
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(&texture, NULL, NULL));
    texture.slot_count = 3u;
    texture.is_cubemap = 1u;
    texture.has_movie_filename = 1u;
    texture.has_user_mipmaps = 1u;
    texture.user_mipmap_count = 2u;
    texture.has_save_format = 1u;

    const uint8_t pixels[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255,
                                       0, 0, 255, 255, 255, 255, 255, 255};
    ASSERT_EQ(NMO_OK, nmo_texture_replace_bitmap(&texture, arena, pixels, 2u, 2u));
    ASSERT_EQ(1u, texture.slot_count);
    ASSERT_EQ(0u, texture.is_cubemap);
    ASSERT_EQ(0u, texture.has_movie_filename);
    ASSERT_EQ(0u, texture.has_user_mipmaps);
    ASSERT_EQ(0u, texture.user_mipmap_count);
    ASSERT_EQ(0u, texture.has_save_format);
    ASSERT_EQ(CKTEXTURE_BITMAP_READER, texture.bitmap_kind);
    ASSERT_NOT_NULL(texture.reader_slots);
    ASSERT_TRUE(texture.reader_slots[0].data_size > 0u);

    nmo_arena_destroy(arena);
}

TEST(fidelity_small_items, layer_edit_is_kept_in_a_newer_layout)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    /* A layer loaded from version 1 has no flags in its layout. */
    nmo_layer_state_t source;
    ASSERT_EQ(NMO_OK, nmo_layer_vtable.create(&source, NULL, NULL));
    source.has_layer_data = 1;
    source.format = 1;
    source.has_version = 1;
    source.version = 1;
    source.has_color = 1;
    source.color_rgba = 0x11223344u;
    source.has_flags = 1;
    source.flags = 6u;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_layer_serialize(&source, chunk, NULL, &ser_ctx));
    nmo_chunk_close(chunk);

    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    nmo_deserialize_context_t des_ctx = nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_layer_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_layer_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_layer_deserialize(&loaded, chunk, NULL, &des_ctx));
    ASSERT_TRUE(loaded.has_flags);
    ASSERT_EQ(6u, loaded.flags);
    ASSERT_EQ(0x11223344u, loaded.color_rgba);

    nmo_layer_vtable.destroy(&source, NULL, NULL);
    nmo_layer_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(fidelity_small_items, light_keeps_the_type_byte_and_alpha_the_file_holds)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t ser_ctx = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    /* A file with type byte 9 (the engine reads it as a point light) and an
     * alpha of 0x40 (the engine writes 0xFF). */
    nmo_light_state_t source;
    ASSERT_EQ(NMO_OK, nmo_light_vtable.create(&source, NULL, NULL));
    source.light_data.type = VX_LIGHTPOINT;
    source.has_raw_light_data = 1;
    source.raw_type_dword = 0x00000209u;
    source.raw_diffuse_argb = 0x40112233u;
    nmo_color_from_argb32(source.raw_diffuse_argb, &source.light_data.diffuse);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_light_serialize(&source, chunk, NULL, &ser_ctx));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    size_t section = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(chunk, CK_STATESAVE_LIGHTDATA, &section));
    uint32_t packed = 0, argb = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(chunk, &packed));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(chunk, &argb));
    ASSERT_EQ(9u, packed & 0xFFu);
    ASSERT_EQ(0x40112233u, argb);

    /* Once the colour is edited the engine's rule applies again. */
    source.light_data.diffuse.r = 0.5f;
    nmo_chunk_t *edited = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(edited);
    edited->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(edited));
    ASSERT_EQ(NMO_OK, nmo_light_serialize(&source, edited, NULL, &ser_ctx));
    nmo_chunk_close(edited);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(edited));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(edited, CK_STATESAVE_LIGHTDATA, &section));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(edited, &packed));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(edited, &argb));
    ASSERT_EQ(0xFFu, argb >> 24);

    nmo_light_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(fidelity_small_items, chunks_older_than_version_four_use_the_old_object_encodings)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    /* an object id: flag, two dwords to skip, the id; a null one: flag 0 */
    const uint32_t words[] = {1u, 0xAAAAAAAAu, 0xBBBBBBBBu, 77u, 0u,
    /* an array: lead dword, four to skip, count, plain ids; then an empty one: lead 0 */
                              1u, 9u, 9u, 9u, 9u, 2u, 5u, 6u, 0u};
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, words[i]));
    }
    nmo_chunk_close(chunk);
    chunk->chunk_version = 3;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));

    nmo_object_id_t id = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_object_id(chunk, &id));
    ASSERT_EQ(77u, id);
    ASSERT_EQ(NMO_OK, nmo_chunk_read_object_id(chunk, &id));
    ASSERT_EQ(NMO_OBJECT_ID_NONE, id);

    nmo_object_id_t *ids = NULL;
    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_object_id_array(chunk, &ids, &count, arena));
    ASSERT_EQ(2u, count);
    ASSERT_EQ(5u, ids[0]);
    ASSERT_EQ(6u, ids[1]);
    ASSERT_EQ(NMO_OK, nmo_chunk_read_object_id_array(chunk, &ids, &count, arena));
    ASSERT_EQ(0u, count);
    nmo_arena_destroy(arena);
}

TEST(fidelity_small_items, murmur3_32_matches_the_reference_vectors)
{
    /* Published MurmurHash3_x86_32 vectors. The block loop used to start at the
     * end of the data, so it read past it and hashed other memory. */
    ASSERT_EQ(0u, nmo_murmur3_32("", 0, 0));
    ASSERT_EQ(0x514E28B7u, nmo_murmur3_32("", 0, 1));
    ASSERT_EQ(0x248BFA47u, nmo_murmur3_32("hello", 5, 0));
    ASSERT_EQ(0x2E4FF723u, nmo_murmur3_32(
        "The quick brown fox jumps over the lazy dog", 43, 0));
    /* An unaligned start gives the same value as an aligned copy. */
    char padded[16] = {0};
    memcpy(padded + 1, "hello", 5);
    ASSERT_EQ(0x248BFA47u, nmo_murmur3_32(padded + 1, 5, 0));
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(fidelity_small_items, sprite_text_font_matches_native_save_order);
    REGISTER_TEST(fidelity_small_items, sprite_text_save_uses_only_the_2d_entity_base);
    REGISTER_TEST(fidelity_small_items, legacy_bodypart_joint_uses_integer_booleans);
    REGISTER_TEST(fidelity_small_items, murmur3_32_matches_the_reference_vectors);
    REGISTER_TEST(fidelity_small_items, chunks_older_than_version_four_use_the_old_object_encodings);
    REGISTER_TEST(fidelity_small_items, light_keeps_the_type_byte_and_alpha_the_file_holds);
    REGISTER_TEST(fidelity_small_items, layer_edit_is_kept_in_a_newer_layout);
    REGISTER_TEST(fidelity_small_items, empty_controller_is_written_with_a_key_count);
    REGISTER_TEST(fidelity_small_items, new_mesh_starts_visible_with_render_channels);
    REGISTER_TEST(fidelity_small_items, sprite_text_load_keeps_the_ratio_offset_flag);
    REGISTER_TEST(fidelity_small_items, replacing_a_bitmap_replaces_the_whole_image);
TEST_MAIN_END()
