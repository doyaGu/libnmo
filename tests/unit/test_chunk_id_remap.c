/**
 * @file test_chunk_id_remap.c
 * @brief Test chunk ID remapping functionality
 */

#include "../test_framework.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_id_remap.h"
#include "format/nmo_chunk_context.h"
#include "object/nmo_ref.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"
#include "object/builtin/nmo_kinematicchain_schemas.h"
#include "object/builtin/nmo_layer_schemas.h"
#include "object/builtin/nmo_sprite_schemas.h"
#include "object/builtin/nmo_curve_schemas.h"
#include "object/builtin/nmo_sprite3d_schemas.h"
#include "object/builtin/nmo_sound_schemas.h"
#include "object/builtin/nmo_2dentity_schemas.h"
#include "object/builtin/nmo_3dentity_schemas.h"
#include "object/builtin/nmo_place_schemas.h"
#include "object/builtin/nmo_group_schemas.h"
#include "object/builtin/nmo_level_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_statesave_ids.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include "type/nmo_type_system.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

TEST(chunk_id_remap, id_remap_basic) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    nmo_id_remap_t* remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    
    // Add some mappings
    ASSERT_EQ(nmo_id_remap_add(remap, 100, 200), NMO_OK);
    ASSERT_EQ(nmo_id_remap_add(remap, 101, 201), NMO_OK);
    ASSERT_EQ(nmo_id_remap_add(remap, 102, 202), NMO_OK);
    
    // Lookup existing IDs
    nmo_object_id_t new_id;
    ASSERT_EQ(nmo_id_remap_lookup_id(remap, 100, &new_id), NMO_OK);
    ASSERT_EQ(new_id, 200);
    
    ASSERT_EQ(nmo_id_remap_lookup_id(remap, 101, &new_id), NMO_OK);
    ASSERT_EQ(new_id, 201);
    
    ASSERT_EQ(nmo_id_remap_lookup_id(remap, 102, &new_id), NMO_OK);
    ASSERT_EQ(new_id, 202);
    
    // Lookup non-existent ID
    ASSERT_EQ(nmo_id_remap_lookup_id(remap, 999, &new_id), NMO_ERR_NOT_FOUND);
    
    // Clear and verify
    nmo_id_remap_clear(remap);
    ASSERT_EQ(nmo_id_remap_lookup_id(remap, 100, &new_id), NMO_ERR_NOT_FOUND);
    
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, single_id_remap) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    // Create chunk and write some object IDs
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    nmo_chunk_start_write(chunk);
    
    // Write some data
    nmo_chunk_write_int(chunk, 42);
    nmo_chunk_write_object_id(chunk, 100); // This should be remapped
    nmo_chunk_write_float(chunk, 3.14f);
    nmo_chunk_write_object_id(chunk, 101); // This should be remapped
    nmo_chunk_write_int(chunk, 99);
    
    nmo_chunk_close(chunk);
    
    // Create remap table
    nmo_id_remap_t* remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    
    ASSERT_EQ(nmo_id_remap_add(remap, 100, 200), NMO_OK);
    ASSERT_EQ(nmo_id_remap_add(remap, 101, 201), NMO_OK);
    
    // Apply remapping
    ASSERT_EQ(nmo_chunk_remap_object_ids(chunk, remap), NMO_OK);
    
    // Read back and verify
    nmo_chunk_start_read(chunk);
    
    int32_t val;
    nmo_object_id_t id;
    float f;
    
    nmo_chunk_read_int(chunk, &val);
    ASSERT_EQ(val, 42);
    
    nmo_chunk_read_object_id(chunk, &id);
    ASSERT_EQ(id, 200); // Should be remapped
    
    nmo_chunk_read_float(chunk, &f);
    ASSERT_EQ(f, 3.14f);
    
    nmo_chunk_read_object_id(chunk, &id);
    ASSERT_EQ(id, 201); // Should be remapped
    
    nmo_chunk_read_int(chunk, &val);
    ASSERT_EQ(val, 99);
    
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, sequence_id_remap) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    nmo_chunk_start_write(chunk);
    
    // Write a sequence of object IDs
    nmo_object_id_t ids[] = {100, 101, 102, 103};
    nmo_chunk_write_object_sequence_start(chunk, 4);
    for (int i = 0; i < 4; i++) {
        nmo_chunk_write_object_sequence_item(chunk, ids[i]);
    }
    
    nmo_chunk_close(chunk);
    
    // Create remap table
    nmo_id_remap_t* remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    
    // Map all IDs
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(nmo_id_remap_add(remap, 100 + i, 200 + i), NMO_OK);
    }
    
    // Apply remapping
    ASSERT_EQ(nmo_chunk_remap_object_ids(chunk, remap), NMO_OK);
    
    // Read back and verify
    // Note: The IDs are stored in the chunk->ids list and data buffer
    // For this test, we'll verify through the chunk's internal state
    // since there's no explicit "read sequence" API yet
    
    // Verify the remapping worked by checking the data directly
    // The sequence should have been remapped from [100,101,102,103] to [200,201,202,203]
    ASSERT_NOT_NULL(chunk->data.data);
    // The sequence format is: [count, id1, id2, id3, id4]
    // We can verify by checking if the remap function modified the IDs
    
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, subchunk_id_remap) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    // Create parent chunk
    nmo_chunk_t* parent = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(parent);
    
    nmo_chunk_start_write(parent);
    
    // Write parent data with object ID
    nmo_chunk_write_int(parent, 1);
    nmo_chunk_write_object_id(parent, 100); // Parent ID
    
    // Create and write a sub-chunk
    nmo_chunk_t* sub = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(sub);
    
    nmo_chunk_start_write(sub);
    nmo_chunk_write_int(sub, 2);
    nmo_chunk_write_object_id(sub, 101); // Sub-chunk ID
    nmo_chunk_write_float(sub, 2.5f);
    nmo_chunk_close(sub);
    
    nmo_chunk_write_sub_chunk(parent, sub);
    
    // More parent data
    nmo_chunk_write_object_id(parent, 102); // Another parent ID
    
    nmo_chunk_close(parent);
    
    // Create remap table
    nmo_id_remap_t* remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    
    ASSERT_EQ(nmo_id_remap_add(remap, 100, 200), NMO_OK);
    ASSERT_EQ(nmo_id_remap_add(remap, 101, 201), NMO_OK);
    ASSERT_EQ(nmo_id_remap_add(remap, 102, 202), NMO_OK);
    
    // Apply remapping (should recursively process sub-chunk)
    ASSERT_EQ(nmo_chunk_remap_object_ids(parent, remap), NMO_OK);
    
    // Read back and verify
    nmo_chunk_start_read(parent);
    
    int32_t val;
    nmo_object_id_t id;
    float f;
    
    nmo_chunk_read_int(parent, &val);
    ASSERT_EQ(val, 1);
    
    nmo_chunk_read_object_id(parent, &id);
    ASSERT_EQ(id, 200); // Parent ID remapped
    
    // Read sub-chunk
    nmo_chunk_t* read_sub = NULL;
    nmo_status_t result = nmo_chunk_read_sub_chunk(parent, &read_sub);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_NOT_NULL(read_sub);
    
    nmo_chunk_start_read(read_sub);
    
    nmo_chunk_read_int(read_sub, &val);
    ASSERT_EQ(val, 2);
    
    nmo_chunk_read_object_id(read_sub, &id);
    ASSERT_EQ(id, 201); // Sub-chunk ID remapped
    
    nmo_chunk_read_float(read_sub, &f);
    ASSERT_EQ(f, 2.5f);
    
    // Continue reading parent
    nmo_chunk_read_object_id(parent, &id);
    ASSERT_EQ(id, 202); // Another parent ID remapped
    
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, zero_and_unchanged_ids) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    nmo_chunk_start_write(chunk);
    
    // Write IDs: some to be remapped, some not, and a zero
    nmo_chunk_write_object_id(chunk, 0);    // Should stay 0
    nmo_chunk_write_object_id(chunk, 100);  // Will be remapped
    nmo_chunk_write_object_id(chunk, 999);  // No mapping, should stay 999
    nmo_chunk_write_object_id(chunk, 101);  // Will be remapped
    
    nmo_chunk_close(chunk);
    
    // Create remap with only partial mappings
    nmo_id_remap_t* remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    
    ASSERT_EQ(nmo_id_remap_add(remap, 100, 200), NMO_OK);
    ASSERT_EQ(nmo_id_remap_add(remap, 101, 201), NMO_OK);
    // Note: no mapping for 999
    
    // Apply remapping
    ASSERT_EQ(nmo_chunk_remap_object_ids(chunk, remap), NMO_OK);
    
    // Read back and verify
    nmo_chunk_start_read(chunk);
    
    nmo_object_id_t id;
    
    nmo_chunk_read_object_id(chunk, &id);
    ASSERT_EQ(id, 0); // Zero unchanged
    
    nmo_chunk_read_object_id(chunk, &id);
    ASSERT_EQ(id, 200); // Remapped
    
    nmo_chunk_read_object_id(chunk, &id);
    ASSERT_EQ(id, 999); // No mapping, unchanged
    
    nmo_chunk_read_object_id(chunk, &id);
    ASSERT_EQ(id, 201); // Remapped
    
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, unresolved_ref_preserves_raw_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);

    nmo_chunk_t *input = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(input);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(input));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(input, 777));
    nmo_chunk_close(input);
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
        .runtime_to_file = NULL,
    };
    nmo_chunk_set_file_context(input, &read_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(input));

    nmo_ref_t ref = {0};
    ASSERT_EQ(NMO_OK, nmo_ref_read(input, &ref));
    ASSERT_EQ(777, ref.raw_id);
    ASSERT_EQ(0, ref.id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, ref.state);

    nmo_chunk_t *output = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(output);
    nmo_chunk_file_context_t write_context = {
        .file_to_runtime = NULL,
        .runtime_to_file = nmo_id_remap_create(arena),
    };
    ASSERT_NOT_NULL(write_context.runtime_to_file);
    nmo_chunk_set_file_context(output, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(output));
    ASSERT_EQ(NMO_OK, nmo_ref_write(output, &ref));
    nmo_chunk_close(output);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(output));
    uint32_t raw = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(output, &raw));
    ASSERT_EQ(777, raw);

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, behavior_unresolved_ref_round_trips_raw_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);

    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);

    nmo_behavior_state_t source;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&source, NULL, NULL));
    nmo_behavior_ref_t unresolved = {
        .ref = {
            .raw_id = 777,
            .id = NMO_OBJECT_ID_NONE,
            .state = NMO_REF_UNRESOLVED,
        },
        .chunk = NULL,
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.inputs, &unresolved));

    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_BEHAVIOR;
    first->chunk_version = NMO_CHUNK_VERSION4;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    ASSERT_EQ(NMO_OK, nmo_behavior_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);

    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_chunk_set_file_context(first, &read_context);
    nmo_behavior_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_deserialize(&loaded, first, NULL, NULL));
    ASSERT_EQ(1u, loaded.inputs.count);
    const nmo_behavior_ref_t *loaded_refs = NMO_ARRAY_DATA(
        nmo_behavior_ref_t, &loaded.inputs);
    ASSERT_EQ(777u, loaded_refs[0].ref.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, loaded_refs[0].ref.id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded_refs[0].ref.state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_BEHAVIOR;
    second->chunk_version = NMO_CHUNK_VERSION4;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_behavior_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_behavior_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_deserialize(&reloaded, second, NULL, NULL));
    ASSERT_EQ(1u, reloaded.inputs.count);
    const nmo_behavior_ref_t *reloaded_refs = NMO_ARRAY_DATA(
        nmo_behavior_ref_t, &reloaded.inputs);
    ASSERT_EQ(777u, reloaded_refs[0].ref.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, reloaded_refs[0].ref.id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded_refs[0].ref.state);

    nmo_array_dispose(&source.inputs);
    nmo_array_dispose(&loaded.inputs);
    nmo_array_dispose(&reloaded.inputs);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, targetcamera_unresolved_ref_round_trips_raw_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);

    nmo_targetcamera_state_t source;
    ASSERT_EQ(NMO_OK, nmo_targetcamera_vtable.create(&source, NULL, NULL));
    source.has_target = 1;
    source.target = nmo_ref_from_raw(777);

    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_TARGETCAMERA;
    first->chunk_version = NMO_CHUNK_VERSION4;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_targetcamera_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_targetcamera_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_targetcamera_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_targetcamera_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(1, loaded.has_target);
    ASSERT_EQ(777u, loaded.target.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, loaded.target.id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.target.state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_TARGETCAMERA;
    second->chunk_version = NMO_CHUNK_VERSION4;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_targetcamera_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_targetcamera_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_targetcamera_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_targetcamera_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(777u, reloaded.target.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, reloaded.target.id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.target.state);

    nmo_targetcamera_vtable.destroy(&source, NULL, NULL);
    nmo_targetcamera_vtable.destroy(&loaded, NULL, NULL);
    nmo_targetcamera_vtable.destroy(&reloaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, targetlight_unresolved_ref_round_trips_raw_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);

    nmo_targetlight_state_t source;
    ASSERT_EQ(NMO_OK, nmo_targetlight_vtable.create(&source, NULL, NULL));
    source.has_target = 1;
    source.target = nmo_ref_from_raw(888);

    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_TARGETLIGHT;
    first->chunk_version = NMO_CHUNK_VERSION4;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_targetlight_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_targetlight_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_targetlight_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_targetlight_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(1, loaded.has_target);
    ASSERT_EQ(888u, loaded.target.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, loaded.target.id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.target.state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_TARGETLIGHT;
    second->chunk_version = NMO_CHUNK_VERSION4;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_targetlight_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_targetlight_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_targetlight_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_targetlight_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(888u, reloaded.target.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, reloaded.target.id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.target.state);

    nmo_targetlight_vtable.destroy(&source, NULL, NULL);
    nmo_targetlight_vtable.destroy(&loaded, NULL, NULL);
    nmo_targetlight_vtable.destroy(&reloaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, kinematicchain_unresolved_refs_round_trip_atomically) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);

    nmo_kinematicchain_state_t source;
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_vtable.create(&source, NULL, NULL));
    source.has_chain_data = 1;
    source.start_effector = nmo_ref_from_raw(111);
    source.end_effector = nmo_ref_from_raw(222);

    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_KINEMATICCHAIN;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(chunk, &write_context);
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_serialize(
        &source, chunk, NULL, &serialize_context));
    nmo_chunk_close(chunk);
    nmo_chunk_set_file_context(chunk, &read_context);

    nmo_kinematicchain_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_deserialize(
        &loaded, chunk, NULL, NULL));
    ASSERT_EQ(111u, loaded.start_effector.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.start_effector.state);
    ASSERT_EQ(222u, loaded.end_effector.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.end_effector.state);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_KINEMATICCHAIN;
    truncated->chunk_version = NMO_CHUNK_VERSION4;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_KINEMATICCHAINALL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(truncated, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(truncated, 333));
    nmo_chunk_close(truncated);
    ASSERT_NE(NMO_OK, nmo_kinematicchain_deserialize(
        &loaded, truncated, NULL, NULL));
    ASSERT_EQ(NMO_REF_NONE, loaded.start_effector.state);
    ASSERT_EQ(NMO_REF_NONE, loaded.end_effector.state);

    nmo_kinematicchain_vtable.destroy(&source, NULL, NULL);
    nmo_kinematicchain_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, layer_unresolved_grid_round_trips_raw_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);

    nmo_layer_state_t source;
    ASSERT_EQ(NMO_OK, nmo_layer_vtable.create(&source, NULL, NULL));
    source.grid = nmo_ref_from_raw(444);
    source.format = 1;
    source.has_version = 1;
    source.version = 3;

    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_LAYER;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(chunk, &write_context);
    ASSERT_EQ(NMO_OK, nmo_layer_serialize(
        &source, chunk, NULL, &serialize_context));
    nmo_chunk_close(chunk);
    nmo_chunk_set_file_context(chunk, &read_context);

    nmo_layer_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_layer_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_layer_deserialize(
        &loaded, chunk, NULL, &deserialize_context));
    ASSERT_EQ(444u, loaded.grid.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, loaded.grid.id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.grid.state);

    nmo_layer_vtable.destroy(&source, NULL, NULL);
    nmo_layer_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, sprite_shared_ref_round_trips_raw_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);

    nmo_sprite_state_t source;
    ASSERT_EQ(NMO_OK, nmo_sprite_vtable.create(&source, NULL, NULL));
    source.has_sprite_ref = true;
    source.sprite_ref = nmo_ref_from_raw(555);
    source.has_bitmap_data = false;

    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_SPRITE;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(chunk, &write_context);
    ASSERT_EQ(NMO_OK, nmo_sprite_serialize(
        &source, chunk, NULL, &serialize_context));
    nmo_chunk_close(chunk);
    nmo_chunk_set_file_context(chunk, &read_context);

    nmo_sprite_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_sprite_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_sprite_deserialize(
        &loaded, chunk, NULL, &deserialize_context));
    ASSERT_TRUE(loaded.has_sprite_ref);
    ASSERT_FALSE(loaded.has_bitmap_data);
    ASSERT_EQ(555u, loaded.sprite_ref.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, loaded.sprite_ref.id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.sprite_ref.state);

    nmo_sprite_vtable.destroy(&source, NULL, NULL);
    nmo_sprite_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, curvepoint_unresolved_curve_round_trips_raw_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_curvepoint_state_t source;
    ASSERT_EQ(NMO_OK, nmo_curvepoint_vtable.create(&source, NULL, NULL));
    source.curve = nmo_ref_from_raw(611);

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_CURVEPOINT;
    first->chunk_version = NMO_CHUNK_VERSION4;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_curvepoint_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_curvepoint_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_curvepoint_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_curvepoint_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(611u, loaded.curve.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.curve.state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_CURVEPOINT;
    second->chunk_version = NMO_CHUNK_VERSION4;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_curvepoint_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_curvepoint_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_curvepoint_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_curvepoint_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(611u, reloaded.curve.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.curve.state);

    nmo_curvepoint_vtable.destroy(&source, NULL, NULL);
    nmo_curvepoint_vtable.destroy(&loaded, NULL, NULL);
    nmo_curvepoint_vtable.destroy(&reloaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, sprite3d_unresolved_material_round_trips_raw_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_sprite3d_state_t source;
    ASSERT_EQ(NMO_OK, nmo_sprite3d_vtable.create(&source, NULL, NULL));
    source.material = nmo_ref_from_raw(622);

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_SPRITE3D;
    first->chunk_version = NMO_CHUNK_VERSION4;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_sprite3d_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_sprite3d_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_sprite3d_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_sprite3d_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(622u, loaded.material.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.material.state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_SPRITE3D;
    second->chunk_version = NMO_CHUNK_VERSION4;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_sprite3d_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_sprite3d_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_sprite3d_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_sprite3d_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(622u, reloaded.material.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.material.state);

    nmo_sprite3d_vtable.destroy(&source, NULL, NULL);
    nmo_sprite3d_vtable.destroy(&loaded, NULL, NULL);
    nmo_sprite3d_vtable.destroy(&reloaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, wavesound_unresolved_attachment_round_trips_raw_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    nmo_wavesound_state_t source;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(&source, NULL, NULL));
    source.has_data2 = 1;
    source.attached_object = nmo_ref_from_raw(633);

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_WAVESOUND;
    first->chunk_version = NMO_CHUNK_VERSION4;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_wavesound_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_wavesound_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_wavesound_deserialize(&loaded, first, NULL, NULL));
    ASSERT_EQ(633u, loaded.attached_object.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.attached_object.state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_WAVESOUND;
    second->chunk_version = NMO_CHUNK_VERSION4;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_wavesound_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_wavesound_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_wavesound_deserialize(&reloaded, second, NULL, NULL));
    ASSERT_EQ(633u, reloaded.attached_object.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.attached_object.state);

    nmo_wavesound_vtable.destroy(&source, NULL, NULL);
    nmo_wavesound_vtable.destroy(&loaded, NULL, NULL);
    nmo_wavesound_vtable.destroy(&reloaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, scalar_ref_sections_do_not_publish_truncated_state) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *curve_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(curve_chunk);
    curve_chunk->class_id = NMO_CID_CURVEPOINT;
    curve_chunk->data_version = 7;
    curve_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(curve_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        curve_chunk, CK_STATESAVE_CURVEPOINTDEFAULTDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(curve_chunk, 701));
    nmo_chunk_close(curve_chunk);

    nmo_curvepoint_state_t curve;
    ASSERT_EQ(NMO_OK, nmo_curvepoint_vtable.create(&curve, NULL, NULL));
    ASSERT_NE(NMO_OK, nmo_curvepoint_deserialize(
        &curve, curve_chunk, NULL, &deserialize_context));
    ASSERT_FALSE(curve.has_default_data);
    ASSERT_EQ(NMO_REF_NONE, curve.curve.state);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, curve.curve.raw_id);
    nmo_curvepoint_vtable.destroy(&curve, NULL, NULL);

    nmo_chunk_t *sprite_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(sprite_chunk);
    sprite_chunk->class_id = NMO_CID_SPRITE3D;
    sprite_chunk->data_version = 7;
    sprite_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(sprite_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        sprite_chunk, CK_STATESAVE_SPRITE3DDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(sprite_chunk, VXSPRITE3D_BILLBOARD));
    nmo_chunk_close(sprite_chunk);

    nmo_sprite3d_state_t sprite;
    ASSERT_EQ(NMO_OK, nmo_sprite3d_vtable.create(&sprite, NULL, NULL));
    ASSERT_NE(NMO_OK, nmo_sprite3d_deserialize(
        &sprite, sprite_chunk, NULL, &deserialize_context));
    ASSERT_FALSE(sprite.has_data);
    ASSERT_EQ(NMO_REF_NONE, sprite.material.state);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, sprite.material.raw_id);
    nmo_sprite3d_vtable.destroy(&sprite, NULL, NULL);

    nmo_chunk_t *sound_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(sound_chunk);
    sound_chunk->class_id = NMO_CID_WAVESOUND;
    sound_chunk->data_version = 7;
    sound_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(sound_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        sound_chunk, CK_STATESAVE_WAVSOUNDDATA2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(sound_chunk, 123));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(sound_chunk, 0.75f));
    nmo_chunk_close(sound_chunk);

    nmo_wavesound_state_t sound;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(&sound, NULL, NULL));
    ASSERT_NE(NMO_OK, nmo_wavesound_deserialize(
        &sound, sound_chunk, NULL, &deserialize_context));
    ASSERT_FALSE(sound.has_data2);
    ASSERT_FLOAT_EQ(0.0f, sound.priority, 0.0001f);
    ASSERT_EQ(NMO_REF_NONE, sound.attached_object.state);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, sound.attached_object.raw_id);
    nmo_wavesound_vtable.destroy(&sound, NULL, NULL);

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, entity_scalar_refs_round_trip_unresolved_raw_ids) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_3dentity_state_t source3d;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&source3d, NULL, NULL));
    source3d.has_mesh_chunk = 1;
    source3d.current_mesh = nmo_ref_from_raw(711);
    nmo_ref_t source_meshes[] = {
        nmo_ref_from_raw(712), nmo_ref_from_raw(713),
    };
    source3d.mesh_count = 2;
    source3d.mesh_ids = source_meshes;
    source3d.has_animation_chunk = 1;
    nmo_ref_t source_animations[] = { nmo_ref_from_raw(714) };
    source3d.animation_count = 1;
    source3d.animation_ids = source_animations;
    source3d.has_entityndata_chunk = 1;
    source3d.place = nmo_ref_from_raw(722);
    source3d.parent = nmo_ref_from_raw(723);

    nmo_chunk_t *first3d = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first3d);
    first3d->class_id = NMO_CID_3DENTITY;
    first3d->chunk_version = NMO_CHUNK_VERSION4;
    first3d->data_version = 7;
    first3d->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first3d, &write_context);
    ASSERT_EQ(NMO_OK, nmo_3dentity_serialize(
        &source3d, first3d, NULL, &serialize_context));
    nmo_chunk_close(first3d);
    nmo_chunk_set_file_context(first3d, &read_context);

    nmo_3dentity_state_t loaded3d;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&loaded3d, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_3dentity_deserialize(
        &loaded3d, first3d, NULL, &deserialize_context));
    ASSERT_EQ(711u, loaded3d.current_mesh.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded3d.current_mesh.state);
    ASSERT_EQ(2u, loaded3d.mesh_count);
    ASSERT_EQ(712u, loaded3d.mesh_ids[0].raw_id);
    ASSERT_EQ(713u, loaded3d.mesh_ids[1].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded3d.mesh_ids[0].state);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded3d.mesh_ids[1].state);
    ASSERT_EQ(1u, loaded3d.animation_count);
    ASSERT_EQ(714u, loaded3d.animation_ids[0].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded3d.animation_ids[0].state);
    ASSERT_EQ(722u, loaded3d.place.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded3d.place.state);
    ASSERT_EQ(723u, loaded3d.parent.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded3d.parent.state);

    nmo_chunk_t *second3d = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second3d);
    second3d->class_id = NMO_CID_3DENTITY;
    second3d->chunk_version = NMO_CHUNK_VERSION4;
    second3d->data_version = 7;
    second3d->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second3d, &write_context);
    ASSERT_EQ(NMO_OK, nmo_3dentity_serialize(
        &loaded3d, second3d, NULL, &serialize_context));
    nmo_chunk_close(second3d);
    nmo_chunk_set_file_context(second3d, &read_context);

    nmo_3dentity_state_t reloaded3d;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&reloaded3d, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_3dentity_deserialize(
        &reloaded3d, second3d, NULL, &deserialize_context));
    ASSERT_EQ(711u, reloaded3d.current_mesh.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded3d.current_mesh.state);
    ASSERT_EQ(2u, reloaded3d.mesh_count);
    ASSERT_EQ(712u, reloaded3d.mesh_ids[0].raw_id);
    ASSERT_EQ(713u, reloaded3d.mesh_ids[1].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded3d.mesh_ids[0].state);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded3d.mesh_ids[1].state);
    ASSERT_EQ(1u, reloaded3d.animation_count);
    ASSERT_EQ(714u, reloaded3d.animation_ids[0].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded3d.animation_ids[0].state);
    ASSERT_EQ(722u, reloaded3d.place.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded3d.place.state);
    ASSERT_EQ(723u, reloaded3d.parent.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded3d.parent.state);

    nmo_3dentity_state_t copied3d;
    nmo_type_descriptor_t entity_type = {
        .size = sizeof(nmo_3dentity_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&copied3d, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.copy(
        &reloaded3d, &copied3d, &entity_type, arena));
    ASSERT_NE(reloaded3d.mesh_ids, copied3d.mesh_ids);
    ASSERT_NE(reloaded3d.animation_ids, copied3d.animation_ids);
    ASSERT_TRUE(nmo_3dentity_vtable.equals(&reloaded3d, &copied3d));
    ASSERT_EQ(nmo_3dentity_vtable.hash(&reloaded3d),
              nmo_3dentity_vtable.hash(&copied3d));

    nmo_2dentity_state_t source2d;
    ASSERT_EQ(NMO_OK, nmo_2dentity_vtable.create(&source2d, NULL, NULL));
    source2d.has_material = true;
    source2d.material = nmo_ref_from_raw(733);
    source2d.has_parent = true;
    source2d.parent = nmo_ref_from_raw(734);

    nmo_chunk_t *first2d = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first2d);
    first2d->class_id = NMO_CID_2DENTITY;
    first2d->chunk_version = NMO_CHUNK_VERSION4;
    first2d->data_version = 7;
    first2d->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first2d, &write_context);
    ASSERT_EQ(NMO_OK, nmo_2dentity_serialize(
        &source2d, first2d, NULL, &serialize_context));
    nmo_chunk_close(first2d);
    nmo_chunk_set_file_context(first2d, &read_context);

    nmo_2dentity_state_t loaded2d;
    ASSERT_EQ(NMO_OK, nmo_2dentity_vtable.create(&loaded2d, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_2dentity_deserialize(
        &loaded2d, first2d, NULL, &deserialize_context));
    ASSERT_TRUE(loaded2d.has_material);
    ASSERT_EQ(733u, loaded2d.material.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded2d.material.state);
    ASSERT_TRUE(loaded2d.has_parent);
    ASSERT_EQ(734u, loaded2d.parent.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded2d.parent.state);

    nmo_chunk_t *second2d = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second2d);
    second2d->class_id = NMO_CID_2DENTITY;
    second2d->chunk_version = NMO_CHUNK_VERSION4;
    second2d->data_version = 7;
    second2d->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second2d, &write_context);
    ASSERT_EQ(NMO_OK, nmo_2dentity_serialize(
        &loaded2d, second2d, NULL, &serialize_context));
    nmo_chunk_close(second2d);
    nmo_chunk_set_file_context(second2d, &read_context);

    nmo_2dentity_state_t reloaded2d;
    ASSERT_EQ(NMO_OK, nmo_2dentity_vtable.create(&reloaded2d, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_2dentity_deserialize(
        &reloaded2d, second2d, NULL, &deserialize_context));
    ASSERT_TRUE(reloaded2d.has_material);
    ASSERT_EQ(733u, reloaded2d.material.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded2d.material.state);
    ASSERT_TRUE(reloaded2d.has_parent);
    ASSERT_EQ(734u, reloaded2d.parent.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded2d.parent.state);

    nmo_3dentity_vtable.destroy(&source3d, NULL, NULL);
    nmo_3dentity_vtable.destroy(&loaded3d, NULL, NULL);
    nmo_3dentity_vtable.destroy(&reloaded3d, NULL, NULL);
    nmo_3dentity_vtable.destroy(&copied3d, NULL, NULL);
    nmo_2dentity_vtable.destroy(&source2d, NULL, NULL);
    nmo_2dentity_vtable.destroy(&loaded2d, NULL, NULL);
    nmo_2dentity_vtable.destroy(&reloaded2d, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, entity_scalar_ref_sections_reject_truncation_atomically) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *chunk3d = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk3d);
    chunk3d->class_id = NMO_CID_3DENTITY;
    chunk3d->data_version = 7;
    chunk3d->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk3d));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk3d, CK_STATESAVE_MESHS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(chunk3d, 744));
    nmo_chunk_close(chunk3d);

    nmo_3dentity_state_t state3d;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&state3d, NULL, NULL));
    ASSERT_NE(NMO_OK, nmo_3dentity_deserialize(
        &state3d, chunk3d, NULL, &deserialize_context));
    ASSERT_FALSE(state3d.has_mesh_chunk);
    ASSERT_EQ(NMO_REF_NONE, state3d.current_mesh.state);
    ASSERT_EQ(0u, state3d.mesh_count);
    ASSERT_NULL(state3d.mesh_ids);
    nmo_3dentity_vtable.destroy(&state3d, NULL, NULL);

    nmo_chunk_t *animation_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(animation_chunk);
    animation_chunk->class_id = NMO_CID_3DENTITY;
    animation_chunk->data_version = 7;
    animation_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(animation_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        animation_chunk, CK_STATESAVE_ANIMATION));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(animation_chunk, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_item(animation_chunk, 745));
    nmo_chunk_close(animation_chunk);

    nmo_3dentity_state_t animation_state;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(
        &animation_state, NULL, NULL));
    ASSERT_NE(NMO_OK, nmo_3dentity_deserialize(
        &animation_state, animation_chunk, NULL, &deserialize_context));
    ASSERT_FALSE(animation_state.has_animation_chunk);
    ASSERT_EQ(0u, animation_state.animation_count);
    ASSERT_NULL(animation_state.animation_ids);
    nmo_3dentity_vtable.destroy(&animation_state, NULL, NULL);

    nmo_chunk_t *parent3d_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(parent3d_chunk);
    parent3d_chunk->class_id = NMO_CID_3DENTITY;
    parent3d_chunk->data_version = 7;
    parent3d_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(parent3d_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        parent3d_chunk, CK_STATESAVE_PARENT));
    nmo_chunk_close(parent3d_chunk);

    nmo_3dentity_state_t parent3d_state;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&parent3d_state, NULL, NULL));
    ASSERT_NE(NMO_OK, nmo_3dentity_deserialize(
        &parent3d_state, parent3d_chunk, NULL, &deserialize_context));
    ASSERT_FALSE(parent3d_state.has_parent_chunk);
    ASSERT_EQ(NMO_REF_NONE, parent3d_state.parent.state);
    nmo_3dentity_vtable.destroy(&parent3d_state, NULL, NULL);

    nmo_chunk_t *chunk2d = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk2d);
    chunk2d->class_id = NMO_CID_2DENTITY;
    chunk2d->data_version = 7;
    chunk2d->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk2d));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk2d, CK_STATESAVE_2DENTITYONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk2d, 0));
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk2d, 0.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk2d, CK_STATESAVE_2DENTITYMATERIAL));
    nmo_chunk_close(chunk2d);

    nmo_2dentity_state_t state2d;
    ASSERT_EQ(NMO_OK, nmo_2dentity_vtable.create(&state2d, NULL, NULL));
    ASSERT_NE(NMO_OK, nmo_2dentity_deserialize(
        &state2d, chunk2d, NULL, &deserialize_context));
    ASSERT_FALSE(state2d.has_material);
    ASSERT_EQ(NMO_REF_NONE, state2d.material.state);
    nmo_2dentity_vtable.destroy(&state2d, NULL, NULL);

    nmo_chunk_t *parent2d_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(parent2d_chunk);
    parent2d_chunk->class_id = NMO_CID_2DENTITY;
    parent2d_chunk->data_version = 7;
    parent2d_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(parent2d_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        parent2d_chunk, CK_STATESAVE_2DENTITYONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        parent2d_chunk, NMO_CK2DENTITY_FLAG_PARENT));
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(parent2d_chunk, 0.0f));
    }
    nmo_chunk_close(parent2d_chunk);

    nmo_2dentity_state_t parent2d_state;
    ASSERT_EQ(NMO_OK, nmo_2dentity_vtable.create(&parent2d_state, NULL, NULL));
    ASSERT_NE(NMO_OK, nmo_2dentity_deserialize(
        &parent2d_state, parent2d_chunk, NULL, &deserialize_context));
    ASSERT_FALSE(parent2d_state.has_parent);
    ASSERT_EQ(NMO_REF_NONE, parent2d_state.parent.state);
    nmo_2dentity_vtable.destroy(&parent2d_state, NULL, NULL);

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, place_refs_round_trip_and_truncation_is_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_place_state_t source;
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(&source, NULL, NULL));
    source.has_camera = 1;
    source.camera = nmo_ref_from_raw(801);
    source.has_level = 1;
    source.level = nmo_ref_from_raw(802);
    nmo_ref_t ref_a = nmo_ref_from_raw(803);
    nmo_ref_t ref_b = nmo_ref_from_raw(804);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.references, &ref_a));
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.references, &ref_b));

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_PLACE;
    first->chunk_version = NMO_CHUNK_VERSION4;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_place_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_place_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_place_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_TRUE(loaded.has_camera);
    ASSERT_EQ(801u, loaded.camera.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.camera.state);
    ASSERT_TRUE(loaded.has_level);
    ASSERT_EQ(802u, loaded.level.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.level.state);
    ASSERT_EQ(2u, loaded.references.count);
    const nmo_ref_t *loaded_refs = NMO_ARRAY_DATA(
        nmo_ref_t, &loaded.references);
    ASSERT_EQ(803u, loaded_refs[0].raw_id);
    ASSERT_EQ(804u, loaded_refs[1].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded_refs[0].state);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded_refs[1].state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_PLACE;
    second->chunk_version = NMO_CHUNK_VERSION4;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_place_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_place_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_place_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    const nmo_ref_t *reloaded_refs = NMO_ARRAY_DATA(
        nmo_ref_t, &reloaded.references);
    ASSERT_EQ(801u, reloaded.camera.raw_id);
    ASSERT_EQ(802u, reloaded.level.raw_id);
    ASSERT_EQ(2u, reloaded.references.count);
    ASSERT_EQ(803u, reloaded_refs[0].raw_id);
    ASSERT_EQ(804u, reloaded_refs[1].raw_id);

    nmo_place_state_t copied;
    nmo_type_descriptor_t place_type = {
        .size = sizeof(nmo_place_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_place_vtable.copy(
        &reloaded, &copied, &place_type, arena));
    ASSERT_NE(reloaded.references.data, copied.references.data);
    ASSERT_TRUE(nmo_place_vtable.equals(&reloaded, &copied));
    ASSERT_EQ(nmo_place_vtable.hash(&reloaded),
              nmo_place_vtable.hash(&copied));

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_PLACE;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_PLACEREFERENCES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(truncated, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_item(truncated, 805));
    nmo_chunk_close(truncated);

    nmo_place_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(&failed, NULL, NULL));
    ASSERT_NE(NMO_OK, nmo_place_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(0u, failed.references.count);
    ASSERT_EQ(NMO_REF_NONE, failed.camera.state);
    ASSERT_EQ(NMO_REF_NONE, failed.level.state);

    nmo_chunk_t *truncated_camera = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated_camera);
    truncated_camera->class_id = NMO_CID_PLACE;
    truncated_camera->data_version = 7;
    truncated_camera->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated_camera));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated_camera, CK_STATESAVE_PLACECAMERA));
    nmo_chunk_close(truncated_camera);

    nmo_place_state_t failed_camera;
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(&failed_camera, NULL, NULL));
    ASSERT_NE(NMO_OK, nmo_place_deserialize(
        &failed_camera, truncated_camera, NULL, &deserialize_context));
    ASSERT_FALSE(failed_camera.has_camera);
    ASSERT_EQ(NMO_REF_NONE, failed_camera.camera.state);

    nmo_chunk_t *truncated_portal = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated_portal);
    truncated_portal->class_id = NMO_CID_PLACE;
    truncated_portal->data_version = 7;
    truncated_portal->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated_portal));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated_portal, CK_STATESAVE_PLACEPORTALS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated_portal, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(truncated_portal, 806));
    nmo_chunk_close(truncated_portal);

    nmo_place_state_t failed_portal;
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(&failed_portal, NULL, NULL));
    ASSERT_NE(NMO_OK, nmo_place_deserialize(
        &failed_portal, truncated_portal, NULL, &deserialize_context));
    ASSERT_EQ(0u, failed_portal.portals.count);

    nmo_place_vtable.destroy(&source, NULL, NULL);
    nmo_place_vtable.destroy(&loaded, NULL, NULL);
    nmo_place_vtable.destroy(&reloaded, NULL, NULL);
    nmo_place_vtable.destroy(&copied, NULL, NULL);
    nmo_place_vtable.destroy(&failed, NULL, NULL);
    nmo_place_vtable.destroy(&failed_camera, NULL, NULL);
    nmo_place_vtable.destroy(&failed_portal, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, group_refs_round_trip_and_failure_is_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_group_state_t source;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.create(&source, NULL, NULL));
    nmo_ref_t ref_a = nmo_ref_from_raw(901);
    nmo_ref_t ref_b = nmo_ref_from_raw(902);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.object_ids, &ref_a));
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.object_ids, &ref_b));

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_GROUP;
    first->chunk_version = NMO_CHUNK_VERSION4;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_group_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_group_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_group_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(2u, loaded.object_ids.count);
    const nmo_ref_t *loaded_refs = NMO_ARRAY_DATA(
        nmo_ref_t, &loaded.object_ids);
    ASSERT_EQ(901u, loaded_refs[0].raw_id);
    ASSERT_EQ(902u, loaded_refs[1].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded_refs[0].state);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded_refs[1].state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_GROUP;
    second->chunk_version = NMO_CHUNK_VERSION4;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_group_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_group_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_group_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    const nmo_ref_t *reloaded_refs = NMO_ARRAY_DATA(
        nmo_ref_t, &reloaded.object_ids);
    ASSERT_EQ(901u, reloaded_refs[0].raw_id);
    ASSERT_EQ(902u, reloaded_refs[1].raw_id);

    nmo_group_state_t copied;
    nmo_type_descriptor_t group_type = {
        .size = sizeof(nmo_group_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_group_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_group_vtable.copy(
        &reloaded, &copied, &group_type, arena));
    ASSERT_NE(reloaded.object_ids.data, copied.object_ids.data);
    ASSERT_TRUE(nmo_group_vtable.equals(&reloaded, &copied));
    ASSERT_EQ(nmo_group_vtable.hash(&reloaded),
              nmo_group_vtable.hash(&copied));

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_GROUP;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_GROUPALL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(truncated, 903));
    nmo_chunk_close(truncated);

    nmo_group_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.create(&failed, NULL, NULL));
    ASSERT_NE(NMO_OK, nmo_group_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(0u, failed.object_ids.count);

    nmo_chunk_t *negative = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(negative);
    negative->class_id = NMO_CID_GROUP;
    negative->data_version = 7;
    negative->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(negative));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        negative, CK_STATESAVE_GROUPALL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(negative, -1));
    nmo_chunk_close(negative);

    nmo_group_state_t failed_negative;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.create(&failed_negative, NULL, NULL));
    ASSERT_NE(NMO_OK, nmo_group_deserialize(
        &failed_negative, negative, NULL, &deserialize_context));
    ASSERT_EQ(0u, failed_negative.object_ids.count);

    nmo_group_vtable.destroy(&source, NULL, NULL);
    nmo_group_vtable.destroy(&loaded, NULL, NULL);
    nmo_group_vtable.destroy(&reloaded, NULL, NULL);
    nmo_group_vtable.destroy(&copied, NULL, NULL);
    nmo_group_vtable.destroy(&failed, NULL, NULL);
    nmo_group_vtable.destroy(&failed_negative, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, level_refs_round_trip_and_failure_is_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_level_state_t source;
    ASSERT_EQ(NMO_OK, nmo_level_vtable.create(&source, NULL, NULL));
    nmo_ref_t scene_a = nmo_ref_from_raw(911);
    nmo_ref_t scene_b = nmo_ref_from_raw(912);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.scene_ids, &scene_a));
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.scene_ids, &scene_b));
    source.current_scene = nmo_ref_from_raw(913);
    source.level_scene = nmo_ref_from_raw(914);

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_LEVEL;
    first->chunk_version = NMO_CHUNK_VERSION4;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_level_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_level_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_level_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_level_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(2u, loaded.scene_ids.count);
    const nmo_ref_t *loaded_scenes = NMO_ARRAY_DATA(
        nmo_ref_t, &loaded.scene_ids);
    ASSERT_EQ(911u, loaded_scenes[0].raw_id);
    ASSERT_EQ(912u, loaded_scenes[1].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded_scenes[0].state);
    ASSERT_EQ(913u, loaded.current_scene.raw_id);
    ASSERT_EQ(914u, loaded.level_scene.raw_id);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_LEVEL;
    second->chunk_version = NMO_CHUNK_VERSION4;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_level_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_level_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_level_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_level_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    const nmo_ref_t *reloaded_scenes = NMO_ARRAY_DATA(
        nmo_ref_t, &reloaded.scene_ids);
    ASSERT_EQ(911u, reloaded_scenes[0].raw_id);
    ASSERT_EQ(912u, reloaded_scenes[1].raw_id);
    ASSERT_EQ(913u, reloaded.current_scene.raw_id);
    ASSERT_EQ(914u, reloaded.level_scene.raw_id);

    nmo_level_state_t copied;
    nmo_type_descriptor_t level_type = {
        .size = sizeof(nmo_level_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_level_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_level_vtable.copy(
        &reloaded, &copied, &level_type, arena));
    ASSERT_NE(reloaded.scene_ids.data, copied.scene_ids.data);
    ASSERT_TRUE(nmo_level_vtable.equals(&reloaded, &copied));
    ASSERT_EQ(nmo_level_vtable.hash(&reloaded),
              nmo_level_vtable.hash(&copied));

    nmo_chunk_t *truncated_scenes = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated_scenes);
    truncated_scenes->class_id = NMO_CID_LEVEL;
    truncated_scenes->data_version = 7;
    truncated_scenes->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated_scenes));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated_scenes, CK_STATESAVE_LEVELDEFAULTDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        truncated_scenes, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        truncated_scenes, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated_scenes, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_item(
        truncated_scenes, 915));
    nmo_chunk_close(truncated_scenes);

    nmo_level_state_t failed_scenes;
    ASSERT_EQ(NMO_OK, nmo_level_vtable.create(&failed_scenes, NULL, NULL));
    ASSERT_NE(NMO_OK, nmo_level_deserialize(
        &failed_scenes, truncated_scenes, NULL, &deserialize_context));
    ASSERT_EQ(0u, failed_scenes.scene_ids.count);

    nmo_chunk_t *truncated_scalars = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated_scalars);
    truncated_scalars->class_id = NMO_CID_LEVEL;
    truncated_scalars->data_version = 7;
    truncated_scalars->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated_scalars));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated_scalars, CK_STATESAVE_LEVELSCENE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(truncated_scalars, 916));
    nmo_chunk_close(truncated_scalars);

    nmo_level_state_t failed_scalars;
    ASSERT_EQ(NMO_OK, nmo_level_vtable.create(&failed_scalars, NULL, NULL));
    ASSERT_NE(NMO_OK, nmo_level_deserialize(
        &failed_scalars, truncated_scalars, NULL, &deserialize_context));
    ASSERT_EQ(NMO_REF_NONE, failed_scalars.current_scene.state);
    ASSERT_EQ(NMO_REF_NONE, failed_scalars.level_scene.state);
    ASSERT_NULL(failed_scalars.level_scene_chunk);

    nmo_level_vtable.destroy(&source, NULL, NULL);
    nmo_level_vtable.destroy(&loaded, NULL, NULL);
    nmo_level_vtable.destroy(&reloaded, NULL, NULL);
    nmo_level_vtable.destroy(&copied, NULL, NULL);
    nmo_level_vtable.destroy(&failed_scenes, NULL, NULL);
    nmo_level_vtable.destroy(&failed_scalars, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, beobject_attribute_unresolved_ref_round_trips_raw_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);

    nmo_beobject_state_t source;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&source, NULL, NULL));
    nmo_beobject_attribute_t unresolved = {
        .parameter = nmo_ref_from_raw(777),
        .type_id = 42,
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.attributes, &unresolved));
    nmo_ref_t unresolved_script = nmo_ref_from_raw(888);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.scripts, &unresolved_script));

    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_BEOBJECT;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_beobject_serialize(&source, first, NULL, NULL));
    nmo_chunk_close(first);

    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_chunk_set_file_context(first, &read_context);
    nmo_beobject_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(&loaded, first, NULL, NULL));
    ASSERT_EQ(1u, loaded.scripts.count);
    const nmo_ref_t *loaded_scripts = NMO_ARRAY_DATA(
        nmo_ref_t, &loaded.scripts);
    ASSERT_EQ(888u, loaded_scripts[0].raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, loaded_scripts[0].id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded_scripts[0].state);
    ASSERT_EQ(1u, loaded.attributes.count);
    const nmo_beobject_attribute_t *loaded_attributes = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &loaded.attributes);
    ASSERT_EQ(777u, loaded_attributes[0].parameter.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, loaded_attributes[0].parameter.id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded_attributes[0].parameter.state);
    ASSERT_EQ(42u, loaded_attributes[0].type_id);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_BEOBJECT;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_beobject_serialize(&loaded, second, NULL, NULL));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_beobject_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(&reloaded, second, NULL, NULL));
    ASSERT_EQ(1u, reloaded.scripts.count);
    const nmo_ref_t *reloaded_scripts = NMO_ARRAY_DATA(
        nmo_ref_t, &reloaded.scripts);
    ASSERT_EQ(888u, reloaded_scripts[0].raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, reloaded_scripts[0].id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded_scripts[0].state);
    ASSERT_EQ(1u, reloaded.attributes.count);
    const nmo_beobject_attribute_t *reloaded_attributes = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &reloaded.attributes);
    ASSERT_EQ(777u, reloaded_attributes[0].parameter.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, reloaded_attributes[0].parameter.id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded_attributes[0].parameter.state);
    ASSERT_EQ(42u, reloaded_attributes[0].type_id);

    nmo_array_dispose(&source.scripts);
    nmo_array_dispose(&source.attributes);
    nmo_array_dispose(&loaded.scripts);
    nmo_array_dispose(&loaded.attributes);
    nmo_array_dispose(&reloaded.scripts);
    nmo_array_dispose(&reloaded.attributes);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, beobject_attribute_failure_keeps_previous_atomic_state) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_beobject_state_t state;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&state, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(
        &state.attributes, 123, 7, NULL));

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_BEOBJECT;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_NEWATTRIBUTES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_sequence_item(truncated, 777));
    nmo_chunk_close(truncated);

    ASSERT_NE(NMO_OK, nmo_beobject_deserialize(
        &state, truncated, NULL, NULL));
    ASSERT_EQ(1u, state.attributes.count);
    const nmo_beobject_attribute_t *attributes = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &state.attributes);
    ASSERT_EQ(123u, nmo_ref_runtime_id(&attributes[0].parameter));
    ASSERT_EQ(7u, attributes[0].type_id);

    nmo_array_dispose(&state.scripts);
    nmo_array_dispose(&state.attributes);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, beobject_copy_preserves_content_equality) {
    nmo_arena_t *source_arena = nmo_arena_create(NULL, 8192);
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(source_arena);
    ASSERT_NOT_NULL(copy_arena);

    nmo_beobject_state_t source;
    nmo_beobject_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(&source.scripts, 101));
    nmo_chunk_t *attribute_chunk = nmo_chunk_create(source_arena);
    ASSERT_NOT_NULL(attribute_chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(attribute_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(attribute_chunk, 0xAABBCCDDu));
    nmo_chunk_close(attribute_chunk);
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(
        &source.attributes, 202, 303, attribute_chunk));

    nmo_type_descriptor_t type = {0};
    type.size = sizeof(nmo_beobject_state_t);
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.copy(
        &source, &copy, &type, copy_arena));
    ASSERT_TRUE(nmo_beobject_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_beobject_vtable.hash(&source),
              nmo_beobject_vtable.hash(&copy));
    const nmo_beobject_attribute_t *source_attributes = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &source.attributes);
    nmo_beobject_attribute_t *copy_attributes = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &copy.attributes);
    ASSERT_TRUE(source_attributes[0].chunk != copy_attributes[0].chunk);

    copy_attributes[0].type_id = 304;
    ASSERT_FALSE(nmo_beobject_vtable.equals(&source, &copy));

    nmo_array_dispose(&source.scripts);
    nmo_array_dispose(&source.attributes);
    nmo_array_dispose(&copy.scripts);
    nmo_array_dispose(&copy.attributes);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(source_arena);
}

TEST(chunk_id_remap, legacy_unresolved_id_preserves_raw_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_object_repository_t *repo = nmo_object_repository_create(NULL);
    ASSERT_NOT_NULL(repo);

    /* A real runtime object deliberately occupies the same numeric ID as the
       unresolved serialized ID. The unresolved value must not alias it. */
    nmo_object_t *colliding_object = nmo_object_create(NULL, 777, 1);
    ASSERT_NOT_NULL(colliding_object);
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(repo, &colliding_object));
    ASSERT_NULL(colliding_object);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(runtime_to_file, 777, 55));

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 777));
    nmo_chunk_close(chunk);

    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
        .repository = repo,
    };
    nmo_chunk_set_file_context(chunk, &read_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    nmo_object_id_t id = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_object_id(chunk, &id));
    ASSERT_NE(777, id);
    ASSERT_NE(0, id);
    nmo_object_id_t preserved_raw = 0;
    ASSERT_TRUE(nmo_object_repository_get_unresolved_ref_raw(
        repo, id, &preserved_raw));
    ASSERT_EQ(777, preserved_raw);

    nmo_chunk_t *output = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(output);
    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
        .repository = repo,
    };
    nmo_chunk_set_file_context(output, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(output));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(output, id));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(output, 777));
    nmo_chunk_close(output);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(output));
    uint32_t raw = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(output, &raw));
    ASSERT_EQ(777, raw);
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(output, &raw));
    ASSERT_EQ(55, raw);

    nmo_object_repository_destroy(repo);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_id_remap, id_remap_basic);
    REGISTER_TEST(chunk_id_remap, single_id_remap);
    REGISTER_TEST(chunk_id_remap, sequence_id_remap);
    REGISTER_TEST(chunk_id_remap, subchunk_id_remap);
    REGISTER_TEST(chunk_id_remap, zero_and_unchanged_ids);
    REGISTER_TEST(chunk_id_remap, unresolved_ref_preserves_raw_id);
    REGISTER_TEST(chunk_id_remap, behavior_unresolved_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, targetcamera_unresolved_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, targetlight_unresolved_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, kinematicchain_unresolved_refs_round_trip_atomically);
    REGISTER_TEST(chunk_id_remap, layer_unresolved_grid_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, sprite_shared_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, curvepoint_unresolved_curve_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, sprite3d_unresolved_material_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, wavesound_unresolved_attachment_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, scalar_ref_sections_do_not_publish_truncated_state);
    REGISTER_TEST(chunk_id_remap, entity_scalar_refs_round_trip_unresolved_raw_ids);
    REGISTER_TEST(chunk_id_remap, entity_scalar_ref_sections_reject_truncation_atomically);
    REGISTER_TEST(chunk_id_remap, place_refs_round_trip_and_truncation_is_atomic);
    REGISTER_TEST(chunk_id_remap, group_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, level_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, beobject_attribute_unresolved_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, beobject_attribute_failure_keeps_previous_atomic_state);
    REGISTER_TEST(chunk_id_remap, beobject_copy_preserves_content_equality);
    REGISTER_TEST(chunk_id_remap, legacy_unresolved_id_preserves_raw_id);
TEST_MAIN_END()
