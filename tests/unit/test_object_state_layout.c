/**
 * @file test_object_state_layout.c
 * @brief Layout-driven lifecycle, copy, equals and hash of object states
 */

#include "../test_framework.h"
#include "core/nmo_arena.h"
#include "core/nmo_array.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "object/nmo_ref.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_grid_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_layer_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_patchmesh_schemas.h"
#include "object/builtin/nmo_place_schemas.h"
#include "object/builtin/nmo_scene_schemas.h"
#include "object/builtin/nmo_sound_schemas.h"
#include "object/builtin/nmo_spritetext_schemas.h"
#include "object/builtin/nmo_synchro_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"
#include "object/builtin/nmo_object_schemas.h"
#include "object/nmo_object_type_common.h"
#include <stddef.h>
#include <string.h>

TEST(object_state_layout, place_copy_equals_hash) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_place_state_t source;
    nmo_place_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(sizeof(nmo_place_portal_entry_t), source.portals.element_size);
    ASSERT_EQ(sizeof(nmo_ref_t), source.references.element_size);

    source.has_camera = 1;
    source.camera = nmo_ref_from_raw(12);
    source.has_portals = 1;
    const nmo_place_portal_entry_t portal = {
        .place = nmo_ref_from_raw(20),
        .portal = nmo_ref_from_raw(21),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.portals, &portal));
    const nmo_ref_t reference = nmo_ref_from_raw(30);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.references, &reference));

    ASSERT_FALSE(nmo_place_vtable.equals(&source, &copied));
    ASSERT_EQ(NMO_OK, nmo_place_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_TRUE(nmo_place_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_place_vtable.hash(&source), nmo_place_vtable.hash(&copied));
    ASSERT_EQ(1u, copied.portals.count);
    ASSERT_TRUE(copied.portals.data != source.portals.data);
    ASSERT_EQ(12u, copied.camera.raw_id);

    nmo_place_portal_entry_t *entry =
        NMO_ARRAY_DATA(nmo_place_portal_entry_t, &copied.portals);
    entry->portal = nmo_ref_from_raw(22);
    ASSERT_FALSE(nmo_place_vtable.equals(&source, &copied));

    nmo_place_vtable.destroy(&source, NULL, NULL);
    nmo_place_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, copy_into_shallow_alias_detaches_arrays) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_group_state_t source;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.create(&source, NULL, NULL));
    const nmo_ref_t member = nmo_ref_from_raw(40);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.object_ids, &member));
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.base.scripts, &member));
    source.has_group_data = 1;

    nmo_group_state_t alias = source;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.copy(&source, &alias, NULL, arena));
    ASSERT_TRUE(alias.object_ids.data != source.object_ids.data);
    ASSERT_TRUE(alias.base.scripts.data != source.base.scripts.data);
    ASSERT_TRUE(nmo_group_vtable.equals(&source, &alias));
    ASSERT_EQ(nmo_group_vtable.hash(&source), nmo_group_vtable.hash(&alias));

    nmo_group_vtable.destroy(&alias, NULL, NULL);
    ASSERT_EQ(1u, source.object_ids.count);
    ASSERT_EQ(40u, NMO_ARRAY_DATA(nmo_ref_t, &source.object_ids)[0].raw_id);
    nmo_group_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, layer_defaults_and_square_data_copy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_layer_state_t source;
    nmo_layer_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_layer_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_layer_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(1, source.type);
    ASSERT_EQ(1u, source.flags);
    ASSERT_EQ(1u, source.has_layer_data);

    uint8_t square[4] = {1, 2, 3, 4};
    source.has_square_data = 1;
    source.square_data = square;
    source.square_data_size = sizeof(square);
    source.color_rgba = 0x11223344u;

    ASSERT_EQ(NMO_OK, nmo_layer_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_TRUE(copied.square_data != source.square_data);
    ASSERT_EQ(sizeof(square), copied.square_data_size);
    ASSERT_EQ(0, memcmp(copied.square_data, square, sizeof(square)));
    ASSERT_EQ(0x11223344u, copied.color_rgba);

    source.square_data = NULL;
    source.square_data_size = 1;
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_layer_vtable.copy(&source, &copied, NULL, arena));

    nmo_layer_vtable.destroy(&source, NULL, NULL);
    nmo_layer_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, targetlight_defaults_and_value_copy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_targetlight_state_t source;
    nmo_targetlight_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_targetlight_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_targetlight_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(NMO_REF_NONE, source.target.state);
    ASSERT_TRUE(nmo_targetlight_vtable.equals(&source, &copied));

    source.has_target = 1;
    source.target = nmo_ref_from_raw(50);
    source.base.light_power = 2.5f;
    ASSERT_FALSE(nmo_targetlight_vtable.equals(&source, &copied));
    ASSERT_EQ(NMO_OK,
              nmo_targetlight_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_TRUE(nmo_targetlight_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_targetlight_vtable.hash(&source),
              nmo_targetlight_vtable.hash(&copied));
    ASSERT_EQ(50u, copied.target.raw_id);

    nmo_targetlight_vtable.destroy(&source, NULL, NULL);
    nmo_targetlight_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, spritetext_strings_copy_by_content) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_spritetext_state_t source;
    nmo_spritetext_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_spritetext_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_spritetext_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(0xFFFFFFFFu, source.font_color);

    char text[] = "Hello";
    source.text_content = text;
    source.font.font_name = "Arial";
    source.font.size = 12;
    ASSERT_EQ(NMO_OK,
              nmo_spritetext_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_TRUE(copied.text_content != source.text_content);
    ASSERT_EQ(0, strcmp("Hello", copied.text_content));
    ASSERT_EQ(0, strcmp("Arial", copied.font.font_name));
    ASSERT_EQ(12, copied.font.size);
    ASSERT_TRUE(nmo_spritetext_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_spritetext_vtable.hash(&source),
              nmo_spritetext_vtable.hash(&copied));

    text[0] = 'J';
    ASSERT_FALSE(nmo_spritetext_vtable.equals(&source, &copied));
    source.text_content = NULL;
    ASSERT_FALSE(nmo_spritetext_vtable.equals(&source, &copied));

    nmo_spritetext_vtable.destroy(&source, NULL, NULL);
    nmo_spritetext_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, parameteroperation_chunks_copy_by_content) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_parameteroperation_state_t source;
    nmo_parameteroperation_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(1u, source.has_in1);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    nmo_chunk_start_write(chunk);
    nmo_chunk_write_dword(chunk, 0xDEADBEEFu);
    nmo_chunk_close(chunk);
    source.in1.ref = nmo_ref_from_raw(60);
    source.in1.chunk = chunk;

    ASSERT_EQ(NMO_OK,
              nmo_parameteroperation_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_NOT_NULL(copied.in1.chunk);
    ASSERT_TRUE(copied.in1.chunk != source.in1.chunk);
    ASSERT_NULL(copied.in2.chunk);
    ASSERT_EQ(60u, copied.in1.ref.raw_id);
    ASSERT_TRUE(nmo_parameteroperation_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_parameteroperation_vtable.hash(&source),
              nmo_parameteroperation_vtable.hash(&copied));

    copied.in1.chunk = NULL;
    ASSERT_FALSE(nmo_parameteroperation_vtable.equals(&source, &copied));

    nmo_parameteroperation_vtable.destroy(&source, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, wavesound_loop_mode_word_is_copied_not_compared) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_wavesound_state_t source;
    nmo_wavesound_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(NMO_OBJECT_ID_NONE, source.attached_object.raw_id);

    source.base.file_name = "ping.wav";
    source.wave_file_name = "pong.wav";
    source.legacy_data2_words[0] = 0x12345678u;
    source.legacy_data2_words[8] = 7u;
    source.legacy_data2_words[19] = 0x87654321u;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_EQ(7u, copied.legacy_data2_words[8]);
    ASSERT_EQ(0x87654321u, copied.legacy_data2_words[19]);
    ASSERT_EQ(0, strcmp("pong.wav", copied.wave_file_name));
    ASSERT_TRUE(copied.wave_file_name != source.wave_file_name);
    ASSERT_TRUE(nmo_wavesound_vtable.equals(&source, &copied));

    copied.legacy_data2_words[8] = 0u;
    ASSERT_TRUE(nmo_wavesound_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_wavesound_vtable.hash(&source),
              nmo_wavesound_vtable.hash(&copied));
    copied.legacy_data2_words[9] ^= 1u;
    ASSERT_FALSE(nmo_wavesound_vtable.equals(&source, &copied));
    copied.legacy_data2_words[9] ^= 1u;
    copied.legacy_data2_words[7] ^= 1u;
    ASSERT_FALSE(nmo_wavesound_vtable.equals(&source, &copied));

    nmo_wavesound_vtable.destroy(&source, NULL, NULL);
    nmo_wavesound_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, midisound_file_origin_is_compared) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_midisound_state_t source;
    nmo_midisound_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_midisound_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_midisound_vtable.create(&copied, NULL, NULL));

    source.has_midi_file_name = 1;
    source.midi_file_name = "song.mid";
    source.midi_file_name_from_file = 1;
    ASSERT_EQ(NMO_OK, nmo_midisound_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_EQ(1u, copied.midi_file_name_from_file);
    ASSERT_TRUE(nmo_midisound_vtable.equals(&source, &copied));
    copied.midi_file_name_from_file = 0;
    ASSERT_FALSE(nmo_midisound_vtable.equals(&source, &copied));

    nmo_midisound_vtable.destroy(&source, NULL, NULL);
    nmo_midisound_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, synchro_reference_arrays_copy_and_compare) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_synchro_state_t source;
    nmo_synchro_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_synchro_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_synchro_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(sizeof(nmo_ref_t), source.arrived_ids.element_size);

    source.max_waiters = 3;
    const nmo_ref_t arrived = nmo_ref_from_raw(40);
    const nmo_ref_t passed = nmo_ref_from_raw(41);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.arrived_ids, &arrived));
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.passed_ids, &passed));
    ASSERT_EQ(NMO_OK, nmo_synchro_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_EQ(3, copied.max_waiters);
    ASSERT_TRUE(copied.arrived_ids.data != source.arrived_ids.data);
    ASSERT_TRUE(nmo_synchro_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_synchro_vtable.hash(&source), nmo_synchro_vtable.hash(&copied));

    /* The same references in the other array are a different state. */
    nmo_array_t swapped = copied.arrived_ids;
    copied.arrived_ids = copied.passed_ids;
    copied.passed_ids = swapped;
    ASSERT_FALSE(nmo_synchro_vtable.equals(&source, &copied));

    nmo_synchro_vtable.destroy(&source, NULL, NULL);
    nmo_synchro_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, parameter_payload_lanes_copy_by_content) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_parameter_state_t source;
    nmo_parameter_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_parameter_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameter_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(CKPARAM_MODE_NONE, source.mode);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, source.object_ref.raw_id);

    const uint8_t payload[] = {1u, 2u, 3u};
    for (size_t i = 0; i < sizeof(payload); ++i) {
        ASSERT_EQ(NMO_OK, nmo_array_append(&source.buffer_data, &payload[i]));
    }
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    nmo_chunk_start_write(chunk);
    nmo_chunk_write_dword(chunk, 0xCAFEF00Du);
    nmo_chunk_close(chunk);
    source.subchunk = chunk;
    source.manager_value = 9;

    ASSERT_EQ(NMO_OK, nmo_parameter_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_EQ(3u, copied.buffer_data.count);
    ASSERT_TRUE(copied.buffer_data.data != source.buffer_data.data);
    ASSERT_NOT_NULL(copied.subchunk);
    ASSERT_TRUE(copied.subchunk != source.subchunk);
    ASSERT_TRUE(nmo_parameter_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_parameter_vtable.hash(&source), nmo_parameter_vtable.hash(&copied));

    NMO_ARRAY_DATA(uint8_t, &copied.buffer_data)[1] = 9u;
    ASSERT_FALSE(nmo_parameter_vtable.equals(&source, &copied));

    nmo_parameter_vtable.destroy(&source, NULL, NULL);
    nmo_parameter_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, bodypart_joint_defaults_and_copy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_bodypart_state_t source;
    nmo_bodypart_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_bodypart_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_bodypart_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(7u, source.rotation_joint.flags);

    source.has_character = 1;
    source.character = nmo_ref_from_raw(70);
    source.has_rotation_joint = 1;
    source.rotation_joint.flags = 5u;
    source.rotation_joint.max.y = 1.5f;
    ASSERT_EQ(NMO_OK, nmo_bodypart_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_EQ(70u, copied.character.raw_id);
    ASSERT_EQ(5u, copied.rotation_joint.flags);
    ASSERT_TRUE(nmo_bodypart_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_bodypart_vtable.hash(&source), nmo_bodypart_vtable.hash(&copied));

    copied.rotation_joint.max.y = 2.0f;
    ASSERT_FALSE(nmo_bodypart_vtable.equals(&source, &copied));

    nmo_bodypart_vtable.destroy(&source, NULL, NULL);
    nmo_bodypart_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

static nmo_chunk_t *make_chunk(nmo_arena_t *arena, uint32_t word)
{
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    if (chunk == NULL) return NULL;
    nmo_chunk_start_write(chunk);
    nmo_chunk_write_dword(chunk, word);
    nmo_chunk_close(chunk);
    return chunk;
}

TEST(object_state_layout, parameterout_destinations_copy_by_content) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_parameterout_state_t source;
    nmo_parameterout_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(NMO_OBJECT_ID_NONE, source.owner.raw_id);

    nmo_ref_t destinations[2] = {nmo_ref_from_raw(81), nmo_ref_from_raw(82)};
    source.destination_ids = destinations;
    source.destination_count = 2;
    source.has_destinations = 1;
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_EQ(2u, copied.destination_count);
    ASSERT_TRUE(copied.destination_ids != source.destination_ids);
    ASSERT_EQ(82u, copied.destination_ids[1].raw_id);
    ASSERT_TRUE(nmo_parameterout_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_parameterout_vtable.hash(&source),
              nmo_parameterout_vtable.hash(&copied));

    copied.destination_ids[1] = nmo_ref_from_raw(83);
    ASSERT_FALSE(nmo_parameterout_vtable.equals(&source, &copied));
    copied.destination_ids[1] = nmo_ref_from_raw(82);
    copied.destination_count = 1;
    ASSERT_FALSE(nmo_parameterout_vtable.equals(&source, &copied));

    nmo_parameterout_vtable.destroy(&source, NULL, NULL);
    nmo_parameterout_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, scene_descriptors_copy_with_their_chunks) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_scene_state_t source;
    nmo_scene_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(sizeof(nmo_scene_object_desc_t), source.object_descs.element_size);

    const nmo_scene_object_desc_t desc = {
        .ref = nmo_ref_from_raw(90),
        .initial_value = make_chunk(arena, 0xA1u),
        .reserved = NULL,
        .flags = 0x18u,
    };
    ASSERT_NOT_NULL(desc.initial_value);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.object_descs, &desc));
    source.fog_end = 12.5f;
    source.starting_camera = nmo_ref_from_raw(91);

    ASSERT_EQ(NMO_OK, nmo_scene_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_EQ(1u, copied.object_descs.count);
    const nmo_scene_object_desc_t *copy_desc =
        NMO_ARRAY_DATA(nmo_scene_object_desc_t, &copied.object_descs);
    ASSERT_EQ(90u, copy_desc->ref.raw_id);
    ASSERT_EQ(0x18u, copy_desc->flags);
    ASSERT_NOT_NULL(copy_desc->initial_value);
    ASSERT_TRUE(copy_desc->initial_value != desc.initial_value);
    ASSERT_NULL(copy_desc->reserved);
    ASSERT_EQ(91u, copied.starting_camera.raw_id);
    ASSERT_TRUE(copied.object_descs.data != source.object_descs.data);

    nmo_scene_vtable.destroy(&source, NULL, NULL);
    nmo_scene_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, grid_defaults_and_layers_copy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_grid_state_t source;
    nmo_grid_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_grid_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_grid_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(10.0f, source.base.world_matrix[5]);
    ASSERT_EQ(1u, source.has_grid_data);

    const nmo_grid_layer_t layer = {
        .ref = nmo_ref_from_raw(95),
        .chunk = make_chunk(arena, 0xB2u),
    };
    ASSERT_NOT_NULL(layer.chunk);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.layers, &layer));
    source.width = 4;
    source.length = 6;
    ASSERT_EQ(NMO_OK, nmo_grid_vtable.copy(&source, &copied, NULL, arena));
    const nmo_grid_layer_t *copy_layer =
        NMO_ARRAY_DATA(nmo_grid_layer_t, &copied.layers);
    ASSERT_EQ(1u, copied.layers.count);
    ASSERT_EQ(95u, copy_layer->ref.raw_id);
    ASSERT_TRUE(copy_layer->chunk != layer.chunk);
    ASSERT_EQ(4, copied.width);
    ASSERT_EQ(6, copied.length);

    nmo_grid_vtable.destroy(&source, NULL, NULL);
    nmo_grid_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, character_parts_and_animations_copy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_character_state_t source;
    nmo_character_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(&copied, NULL, NULL));

    const nmo_character_part_t part = {
        .ref = nmo_ref_from_raw(100),
        .chunk = make_chunk(arena, 0xC3u),
    };
    ASSERT_NOT_NULL(part.chunk);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.body_parts, &part));
    const nmo_ref_t animation = nmo_ref_from_raw(101);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.animations, &animation));
    source.root_body_part = nmo_ref_from_raw(102);

    ASSERT_EQ(NMO_OK, nmo_character_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_EQ(1u, copied.body_parts.count);
    ASSERT_EQ(1u, copied.animations.count);
    ASSERT_TRUE(copied.animations.data != source.animations.data);
    ASSERT_TRUE(NMO_ARRAY_DATA(nmo_character_part_t, &copied.body_parts)->chunk !=
                part.chunk);
    ASSERT_EQ(102u, copied.root_body_part.raw_id);

    nmo_character_vtable.destroy(&source, NULL, NULL);
    nmo_character_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, beobject_attributes_copy_with_strings_and_chunks) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_beobject_state_t source;
    nmo_beobject_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&copied, NULL, NULL));

    nmo_chunk_t *payload = make_chunk(arena, 0xD4u);
    ASSERT_NOT_NULL(payload);
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(
        &source.attributes, 110, 7u, payload));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(&source.scripts, 112));
    source.priority = 5;

    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.copy(&source, &copied, NULL, arena));
    const nmo_beobject_attribute_t *attribute =
        NMO_ARRAY_DATA(nmo_beobject_attribute_t, &copied.attributes);
    ASSERT_EQ(1u, copied.attributes.count);
    ASSERT_EQ(7u, attribute->type_id);
    ASSERT_TRUE(attribute->chunk != payload);
    ASSERT_EQ(1u, copied.scripts.count);
    ASSERT_TRUE(copied.scripts.data != source.scripts.data);
    ASSERT_EQ(5, copied.priority);
    ASSERT_TRUE(nmo_beobject_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_beobject_vtable.hash(&source), nmo_beobject_vtable.hash(&copied));
    nmo_beobject_vtable.destroy(&source, NULL, NULL);
    nmo_beobject_vtable.destroy(&copied, NULL, NULL);

    /* The legacy lane is exclusive with the modern one, so it is a state of its own. */
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&copied, NULL, NULL));
    const nmo_beobject_legacy_attribute_t legacy = {
        .compatible_class_id = 3,
        .name = "Tag",
        .category = "Misc",
        .parameter = nmo_ref_from_raw(111),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.legacy_attributes, &legacy));
    source.has_data_section = 1;
    source.data_is_legacy = 1;
    source.legacy_data_words[2] = 0x55u;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.copy(&source, &copied, NULL, arena));
    const nmo_beobject_legacy_attribute_t *legacy_copy =
        NMO_ARRAY_DATA(nmo_beobject_legacy_attribute_t, &copied.legacy_attributes);
    ASSERT_EQ(1u, copied.legacy_attributes.count);
    ASSERT_EQ(0, strcmp("Tag", legacy_copy->name));
    ASSERT_TRUE(legacy_copy->name != legacy.name);
    ASSERT_EQ(0, strcmp("Misc", legacy_copy->category));
    ASSERT_EQ(0x55u, copied.legacy_data_words[2]);
    ASSERT_TRUE(nmo_beobject_vtable.equals(&source, &copied));
    NMO_ARRAY_DATA(nmo_beobject_legacy_attribute_t, &copied.legacy_attributes)[0]
        .category = "Other";
    ASSERT_FALSE(nmo_beobject_vtable.equals(&source, &copied));

    nmo_beobject_vtable.destroy(&source, NULL, NULL);
    nmo_beobject_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

/* A state that exercises the member kinds with a count of each width, records
 * that have padding, and the failure paths. */
typedef struct synthetic_record {
    uint8_t tag;
    uint32_t value;
    char *name;
} synthetic_record_t;

typedef struct synthetic_state {
    nmo_object_state_t base;
    uint32_t count32;
    uint16_t *words;
    size_t count64;
    uint32_t *wide;
    nmo_array_t records;
} synthetic_state_t;

static const nmo_object_state_member_t synthetic_record_members[] = {
    NMO_STATE_VALUE(synthetic_record_t, tag),
    NMO_STATE_VALUE(synthetic_record_t, value),
    NMO_STATE_STRING(synthetic_record_t, name)
};

static const nmo_object_state_layout_t synthetic_record_layout = {
    .size = sizeof(synthetic_record_t),
    .members = synthetic_record_members,
    .member_count = sizeof(synthetic_record_members) /
        sizeof(synthetic_record_members[0]),
};

static const nmo_object_state_member_t synthetic_members[] = {
    NMO_STATE_VALUE(synthetic_state_t, count32),
    NMO_STATE_COUNTED(synthetic_state_t, words, count32, uint16_t),
    NMO_STATE_VALUE(synthetic_state_t, count64),
    NMO_STATE_COUNTED(synthetic_state_t, wide, count64, uint32_t),
    NMO_STATE_RECORDS(synthetic_state_t, records, synthetic_record_layout)
};

static const nmo_object_state_layout_t synthetic_layout = {
    .size = sizeof(synthetic_state_t),
    .base_vtable = &nmo_object_vtable,
    .base_size = sizeof(nmo_object_state_t),
    .members = synthetic_members,
    .member_count = sizeof(synthetic_members) / sizeof(synthetic_members[0]),
};

TEST(object_state_layout, counted_and_record_members_of_both_widths) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    synthetic_state_t source;
    synthetic_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_object_layout_create(&synthetic_layout, &source, NULL));
    ASSERT_EQ(NMO_OK, nmo_object_layout_create(&synthetic_layout, &copied, NULL));
    ASSERT_EQ(sizeof(synthetic_record_t), source.records.element_size);

    uint16_t words[3] = {1u, 2u, 3u};
    uint32_t wide[2] = {70000u, 80000u};
    source.count32 = 3;
    source.words = words;
    source.count64 = 2;
    source.wide = wide;
    synthetic_record_t record;
    memset(&record, 0xFF, sizeof(record));
    record.tag = 4u;
    record.value = 9u;
    record.name = "rec";
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.records, &record));

    ASSERT_EQ(NMO_OK, nmo_object_layout_copy(&synthetic_layout, &source, &copied, arena));
    ASSERT_TRUE(copied.words != source.words);
    ASSERT_TRUE(copied.wide != source.wide);
    ASSERT_EQ(3u, copied.words[2]);
    ASSERT_EQ(80000u, copied.wide[1]);
    ASSERT_TRUE(nmo_object_layout_equals(&synthetic_layout, &source, &copied));
    ASSERT_EQ(nmo_object_layout_hash(&synthetic_layout, &source),
              nmo_object_layout_hash(&synthetic_layout, &copied));

    /* Padding bytes of a record are not part of its value. */
    synthetic_record_t *copied_record =
        NMO_ARRAY_DATA(synthetic_record_t, &copied.records);
    memset(&copied_record->tag + 1, 0x00, offsetof(synthetic_record_t, value) - 1);
    ASSERT_TRUE(nmo_object_layout_equals(&synthetic_layout, &source, &copied));
    ASSERT_EQ(nmo_object_layout_hash(&synthetic_layout, &source),
              nmo_object_layout_hash(&synthetic_layout, &copied));
    copied_record->name = "other";
    ASSERT_FALSE(nmo_object_layout_equals(&synthetic_layout, &source, &copied));
    copied_record->name = "rec";
    copied.wide[0] ^= 1u;
    ASSERT_FALSE(nmo_object_layout_equals(&synthetic_layout, &source, &copied));

    /* Without an arena the counted buffers cannot be copied and the target stays. */
    copied.wide[0] ^= 1u;
    synthetic_state_t untouched = copied;
    ASSERT_NE(NMO_OK,
              nmo_object_layout_copy(&synthetic_layout, &source, &copied, NULL));
    ASSERT_EQ(0, memcmp(&untouched, &copied, sizeof(copied)));

    nmo_object_layout_destroy(&synthetic_layout, &source, NULL);
    nmo_object_layout_destroy(&synthetic_layout, &copied, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, entity_skin_copies_deeply) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_3dentity_state_t source;
    nmo_3dentity_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(1.0f, source.world_matrix[15]);
    ASSERT_EQ(NMO_3DENTITY_CTOR_MOVEABLE_FLAGS, source.moveable_flags);

    nmo_ref_t meshes[2] = {nmo_ref_from_raw(120), nmo_ref_from_raw(121)};
    source.mesh_ids = meshes;
    source.mesh_count = 2;
    nmo_3dentity_skin_bone_t bones[2];
    memset(bones, 0, sizeof(bones));
    bones[1].bone = nmo_ref_from_raw(122);
    bones[1].bone_flags = 3u;
    uint32_t indices[2] = {0u, 1u};
    float weights[2] = {0.25f, 0.75f};
    nmo_3dentity_skin_vertex_t vertices[2];
    memset(vertices, 0, sizeof(vertices));
    vertices[1].bone_count = 2;
    vertices[1].bone_indices = indices;
    vertices[1].bone_weights = weights;
    nmo_vector_t normals[2] = {{0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}};
    nmo_3dentity_skin_t skin;
    memset(&skin, 0, sizeof(skin));
    skin.bone_count = 2;
    skin.bones = bones;
    skin.vertex_count = 2;
    skin.vertices = vertices;
    skin.normal_count = 2;
    skin.normals = normals;
    skin.normals_present = 1;
    source.skin = &skin;

    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_NOT_NULL(copied.skin);
    ASSERT_TRUE(copied.skin != source.skin);
    ASSERT_TRUE(copied.skin->bones != bones);
    ASSERT_TRUE(copied.skin->vertices != vertices);
    ASSERT_TRUE(copied.skin->vertices[1].bone_indices != indices);
    ASSERT_TRUE(copied.skin->vertices[1].bone_weights != weights);
    ASSERT_TRUE(copied.skin->normals != normals);
    ASSERT_EQ(3u, copied.skin->bones[1].bone_flags);
    ASSERT_EQ(0.75f, copied.skin->vertices[1].bone_weights[1]);
    ASSERT_TRUE(copied.mesh_ids != meshes);
    ASSERT_EQ(121u, copied.mesh_ids[1].raw_id);
    ASSERT_TRUE(nmo_3dentity_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_3dentity_vtable.hash(&source), nmo_3dentity_vtable.hash(&copied));

    copied.skin->vertices[1].bone_weights[0] = 0.5f;
    ASSERT_FALSE(nmo_3dentity_vtable.equals(&source, &copied));
    copied.skin->vertices[1].bone_weights[0] = 0.25f;
    ASSERT_TRUE(nmo_3dentity_vtable.equals(&source, &copied));
    nmo_3dentity_skin_t *skin_copy = copied.skin;
    copied.skin = NULL;
    ASSERT_FALSE(nmo_3dentity_vtable.equals(&source, &copied));
    copied.skin = skin_copy;

    /* A vertex that claims bones it does not have is invalid and is not copied. */
    vertices[1].bone_weights = NULL;
    nmo_3dentity_state_t other;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&other, NULL, NULL));
    other.z_order = 77;
    ASSERT_NE(NMO_OK, nmo_3dentity_vtable.copy(&source, &other, NULL, arena));
    ASSERT_EQ(77, other.z_order);

    source.skin = NULL;
    source.mesh_ids = NULL;
    source.mesh_count = 0;
    nmo_3dentity_vtable.destroy(&source, NULL, NULL);
    nmo_3dentity_vtable.destroy(&copied, NULL, NULL);
    nmo_3dentity_vtable.destroy(&other, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, mesh_counts_follow_the_state) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_mesh_state_t source;
    nmo_mesh_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(0x0Au, source.flags);

    nmo_face_t faces[2];
    memset(faces, 0, sizeof(faces));
    faces[1].material_group_idx = 1u;
    uint16_t face_indices[6] = {0, 1, 2, 2, 1, 0};
    uint16_t line_indices[4] = {0, 1, 1, 2};
    nmo_vertex_t vertices[3];
    memset(vertices, 0, sizeof(vertices));
    uint32_t colors[3] = {1u, 2u, 3u};
    uint32_t specular[3] = {4u, 5u, 6u};
    float weights[3] = {0.1f, 0.2f, 0.3f};
    nmo_vector2_t uvs[2] = {{0.f, 1.f}, {1.f, 0.f}};
    nmo_material_channel_t channel;
    memset(&channel, 0, sizeof(channel));
    channel.uv_count = 2;
    channel.uv_coords = uvs;
    uint8_t progressive[3] = {7u, 8u, 9u};
    source.face_count = 2;
    source.faces = faces;
    source.face_vertex_indices = face_indices;
    source.line_count = 2;
    source.line_indices = line_indices;
    source.vertex_count = 3;
    source.vertices = vertices;
    source.vertex_colors = colors;
    source.vertex_specular = specular;
    /* No weight count: the weights are one per vertex. */
    source.vertex_weights = weights;
    source.material_channel_count = 1;
    source.material_channels = &channel;
    source.pm_data_size = 3;
    source.pm_data = progressive;

    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.copy(&source, &copied, NULL, arena));
    ASSERT_TRUE(copied.face_vertex_indices != face_indices);
    ASSERT_EQ(0, memcmp(copied.face_vertex_indices, face_indices, sizeof(face_indices)));
    ASSERT_EQ(0, memcmp(copied.line_indices, line_indices, sizeof(line_indices)));
    ASSERT_EQ(0, memcmp(copied.vertex_weights, weights, sizeof(weights)));
    ASSERT_TRUE(copied.vertex_weights != weights);
    ASSERT_EQ(0u, copied.vertex_weight_count);
    ASSERT_TRUE(copied.material_channels != &channel);
    ASSERT_TRUE(copied.material_channels[0].uv_coords != uvs);
    ASSERT_EQ(1.0f, copied.material_channels[0].uv_coords[1].x);
    ASSERT_TRUE(copied.pm_data != progressive);
    ASSERT_EQ(9u, ((const uint8_t *)copied.pm_data)[2]);
    ASSERT_EQ(1u, copied.faces[1].material_group_idx);

    source.vertex_weights = NULL;
    source.pm_data = NULL;
    source.pm_data_size = 0;
    source.material_channels = NULL;
    source.material_channel_count = 0;
    source.faces = NULL;
    source.face_vertex_indices = NULL;
    source.line_indices = NULL;
    source.vertices = NULL;
    source.vertex_colors = NULL;
    source.vertex_specular = NULL;
    nmo_mesh_vtable.destroy(&source, NULL, NULL);
    nmo_mesh_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, curve_sub_points_copy_with_their_chunks) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_curve_state_t curve;
    nmo_curve_state_t curve_copy;
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.create(&curve, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.create(&curve_copy, NULL, NULL));
    ASSERT_EQ(100u, curve.step_count);
    nmo_ref_t control_points[2] = {nmo_ref_from_raw(130), nmo_ref_from_raw(131)};
    nmo_curve_point_subchunk_t sub_points[1] = {
        {.ref = nmo_ref_from_raw(132), .chunk = make_chunk(arena, 0xE5u)},
    };
    ASSERT_NOT_NULL(sub_points[0].chunk);
    curve.control_point_count = 2;
    curve.control_point_ids = control_points;
    curve.sub_point_count = 1;
    curve.sub_points = sub_points;
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.copy(&curve, &curve_copy, NULL, arena));
    ASSERT_TRUE(curve_copy.control_point_ids != control_points);
    ASSERT_TRUE(curve_copy.sub_points != sub_points);
    ASSERT_TRUE(curve_copy.sub_points[0].chunk != sub_points[0].chunk);
    ASSERT_EQ(132u, curve_copy.sub_points[0].ref.raw_id);
    ASSERT_EQ(100u, curve_copy.step_count);
    curve.control_point_ids = NULL;
    curve.control_point_count = 0;
    curve.sub_points = NULL;
    curve.sub_point_count = 0;
    nmo_curve_vtable.destroy(&curve, NULL, NULL);
    nmo_curve_vtable.destroy(&curve_copy, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(object_state_layout, patchmesh_channels_copy_their_buffers) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_patchmesh_state_t patch;
    nmo_patchmesh_state_t patch_copy;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(&patch, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(&patch_copy, NULL, NULL));
    ASSERT_EQ(0x0Au, patch.base.flags);
    uint8_t raw_patches[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    nmo_vector2_t patch_uvs[1] = {{0.5f, 0.5f}};
    nmo_patchmesh_channel_t channel;
    memset(&channel, 0, sizeof(channel));
    channel.patch_count = 2;
    channel.patches_raw = raw_patches;
    channel.uv_count = 1;
    channel.uvs = patch_uvs;
    uint8_t edges[12] = {7, 8, 9};
    patch.channel_count = 1;
    patch.channels = &channel;
    patch.edge_count = 1;
    patch.edge_data_size = sizeof(edges);
    patch.edge_data = edges;
    ASSERT_EQ(NMO_OK,
              nmo_patchmesh_vtable.copy(&patch, &patch_copy, NULL, arena));
    ASSERT_TRUE(patch_copy.channels != &channel);
    ASSERT_TRUE(patch_copy.channels[0].patches_raw != raw_patches);
    ASSERT_EQ(0, memcmp(patch_copy.channels[0].patches_raw, raw_patches,
                        sizeof(raw_patches)));
    ASSERT_TRUE(patch_copy.channels[0].uvs != patch_uvs);
    ASSERT_TRUE(patch_copy.edge_data != edges);
    ASSERT_EQ(9u, patch_copy.edge_data[2]);
    patch.channels = NULL;
    patch.channel_count = 0;
    patch.edge_data = NULL;
    patch.edge_count = 0;
    patch.edge_data_size = 0;
    nmo_patchmesh_vtable.destroy(&patch, NULL, NULL);
    nmo_patchmesh_vtable.destroy(&patch_copy, NULL, NULL);
    nmo_arena_destroy(arena);
}

/* A record that holds a counted array of its own, reached through a pointer,
 * with the count of the outer array computed from the state. */
typedef struct nested_leaf {
    uint32_t id;
    char *label;
} nested_leaf_t;

typedef struct nested_node {
    uint32_t leaf_count;
    nested_leaf_t *leaves;
    uint16_t pair_source;
    uint16_t *pairs;
} nested_node_t;

typedef struct nested_state {
    nmo_object_state_t base;
    nested_node_t *root;
    uint32_t pair_factor;
} nested_state_t;

static const nmo_object_state_member_t nested_leaf_members[] = {
    NMO_STATE_VALUE(nested_leaf_t, id),
    NMO_STATE_STRING(nested_leaf_t, label)
};

static const nmo_object_state_layout_t nested_leaf_layout = {
    .size = sizeof(nested_leaf_t),
    .members = nested_leaf_members,
    .member_count = sizeof(nested_leaf_members) / sizeof(nested_leaf_members[0]),
};

static size_t nested_pair_count(const void *owner)
{
    return (size_t)((const nested_node_t *)owner)->pair_source * 2u;
}

static const nmo_object_state_member_t nested_node_members[] = {
    NMO_STATE_VALUE(nested_node_t, leaf_count),
    NMO_STATE_COUNTED_RECORDS(nested_node_t, leaves, leaf_count, nested_leaf_layout),
    NMO_STATE_VALUE(nested_node_t, pair_source),
    NMO_STATE_COUNTED_BY(nested_node_t, pairs, nested_pair_count, uint16_t)
};

static const nmo_object_state_layout_t nested_node_layout = {
    .size = sizeof(nested_node_t),
    .members = nested_node_members,
    .member_count = sizeof(nested_node_members) / sizeof(nested_node_members[0]),
};

static const nmo_object_state_member_t nested_members[] = {
    NMO_STATE_RECORD_PTR(nested_state_t, root, nested_node_layout),
    NMO_STATE_VALUE(nested_state_t, pair_factor)
};

static const nmo_object_state_layout_t nested_layout = {
    .size = sizeof(nested_state_t),
    .base_vtable = &nmo_object_vtable,
    .base_size = sizeof(nmo_object_state_t),
    .members = nested_members,
    .member_count = sizeof(nested_members) / sizeof(nested_members[0]),
};

TEST(object_state_layout, record_pointer_with_nested_counted_members) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nested_state_t source;
    nested_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_object_layout_create(&nested_layout, &source, NULL));
    ASSERT_EQ(NMO_OK, nmo_object_layout_create(&nested_layout, &copied, NULL));

    /* No record: equal to another state without one, and copied as none. */
    ASSERT_TRUE(nmo_object_layout_equals(&nested_layout, &source, &copied));
    ASSERT_EQ(NMO_OK, nmo_object_layout_copy(&nested_layout, &source, &copied, arena));
    ASSERT_NULL(copied.root);

    nested_leaf_t leaves[2] = {{1u, "one"}, {2u, "two"}};
    uint16_t pairs[4] = {10, 11, 12, 13};
    nested_node_t node = {
        .leaf_count = 2, .leaves = leaves, .pair_source = 2, .pairs = pairs,
    };
    source.root = &node;
    ASSERT_FALSE(nmo_object_layout_equals(&nested_layout, &source, &copied));
    ASSERT_EQ(NMO_OK, nmo_object_layout_copy(&nested_layout, &source, &copied, arena));
    ASSERT_NOT_NULL(copied.root);
    ASSERT_TRUE(copied.root != &node);
    ASSERT_TRUE(copied.root->leaves != leaves);
    ASSERT_TRUE(copied.root->leaves[1].label != leaves[1].label);
    ASSERT_EQ(0, strcmp("two", copied.root->leaves[1].label));
    ASSERT_TRUE(copied.root->pairs != pairs);
    ASSERT_EQ(13u, copied.root->pairs[3]);
    ASSERT_TRUE(nmo_object_layout_equals(&nested_layout, &source, &copied));
    ASSERT_EQ(nmo_object_layout_hash(&nested_layout, &source),
              nmo_object_layout_hash(&nested_layout, &copied));

    copied.root->pairs[3] = 14u;
    ASSERT_FALSE(nmo_object_layout_equals(&nested_layout, &source, &copied));
    copied.root->pairs[3] = 13u;
    copied.root->leaves[0].label = "uno";
    ASSERT_FALSE(nmo_object_layout_equals(&nested_layout, &source, &copied));
    ASSERT_NE(nmo_object_layout_hash(&nested_layout, &source),
              nmo_object_layout_hash(&nested_layout, &copied));

    /* The count of the pairs follows the record: a missing array is an error. */
    node.pair_source = 3;
    node.pairs = NULL;
    nested_state_t untouched = copied;
    ASSERT_NE(NMO_OK, nmo_object_layout_copy(&nested_layout, &source, &copied, arena));
    ASSERT_EQ(0, memcmp(&untouched, &copied, sizeof(copied)));

    source.root = NULL;
    nmo_object_layout_destroy(&nested_layout, &source, NULL);
    nmo_object_layout_destroy(&nested_layout, &copied, NULL);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_state_layout, place_copy_equals_hash);
    REGISTER_TEST(object_state_layout, copy_into_shallow_alias_detaches_arrays);
    REGISTER_TEST(object_state_layout, layer_defaults_and_square_data_copy);
    REGISTER_TEST(object_state_layout, targetlight_defaults_and_value_copy);
    REGISTER_TEST(object_state_layout, spritetext_strings_copy_by_content);
    REGISTER_TEST(object_state_layout, parameteroperation_chunks_copy_by_content);
    REGISTER_TEST(object_state_layout, wavesound_loop_mode_word_is_copied_not_compared);
    REGISTER_TEST(object_state_layout, midisound_file_origin_is_compared);
    REGISTER_TEST(object_state_layout, synchro_reference_arrays_copy_and_compare);
    REGISTER_TEST(object_state_layout, parameter_payload_lanes_copy_by_content);
    REGISTER_TEST(object_state_layout, bodypart_joint_defaults_and_copy);
    REGISTER_TEST(object_state_layout, parameterout_destinations_copy_by_content);
    REGISTER_TEST(object_state_layout, scene_descriptors_copy_with_their_chunks);
    REGISTER_TEST(object_state_layout, grid_defaults_and_layers_copy);
    REGISTER_TEST(object_state_layout, character_parts_and_animations_copy);
    REGISTER_TEST(object_state_layout, beobject_attributes_copy_with_strings_and_chunks);
    REGISTER_TEST(object_state_layout, counted_and_record_members_of_both_widths);
    REGISTER_TEST(object_state_layout, entity_skin_copies_deeply);
    REGISTER_TEST(object_state_layout, mesh_counts_follow_the_state);
    REGISTER_TEST(object_state_layout, curve_sub_points_copy_with_their_chunks);
    REGISTER_TEST(object_state_layout, patchmesh_channels_copy_their_buffers);
    REGISTER_TEST(object_state_layout, record_pointer_with_nested_counted_members);
TEST_MAIN_END()
