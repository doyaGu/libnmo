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
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_layer_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_place_schemas.h"
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
    REGISTER_TEST(object_state_layout, counted_and_record_members_of_both_widths);
TEST_MAIN_END()
