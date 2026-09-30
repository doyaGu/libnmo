/**
 * @file test_fidelity_small_items.c
 * @brief Small details found when comparing saved data with what the engine reads
 */

#include "test_framework.h"

#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_spritetext_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_serialize_context.h"

#include <string.h>

#define CONTROLLER_LINPOS 0x637c4301u

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

TEST_MAIN_BEGIN()
    REGISTER_TEST(fidelity_small_items, empty_controller_is_written_with_a_key_count);
    REGISTER_TEST(fidelity_small_items, new_mesh_starts_visible_with_render_channels);
    REGISTER_TEST(fidelity_small_items, sprite_text_load_keeps_the_ratio_offset_flag);
    REGISTER_TEST(fidelity_small_items, replacing_a_bitmap_replaces_the_whole_image);
TEST_MAIN_END()
