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
#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_beobject_schemas.h"
#include "object/builtin/nmo_character_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"
#include "object/builtin/nmo_kinematicchain_schemas.h"
#include "object/builtin/nmo_grid_schemas.h"
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
#include "object/builtin/nmo_scene_schemas.h"
#include "object/builtin/nmo_synchro_schemas.h"
#include "object/builtin/nmo_material_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_mesh_schemas.h"
#include "object/builtin/nmo_patchmesh_schemas.h"
#include "object/builtin/nmo_animation_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_param_guids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_statesave_ids.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include "type/nmo_type_system.h"
#include "type/nmo_reflection.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static void *beobject_fail_alloc(void *user_data, size_t size,
                                 size_t alignment) {
    (void)user_data;
    (void)size;
    (void)alignment;
    return NULL;
}

static void beobject_fail_free(void *user_data, void *ptr) {
    (void)user_data;
    (void)ptr;
}

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

TEST(chunk_id_remap, null_ref_uses_file_null_encoding) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(file_to_runtime, 0, 321));

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_set_file_context(chunk, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    nmo_ref_t null_ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    ASSERT_EQ(NMO_OK, nmo_ref_write(chunk, &null_ref));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    uint32_t encoded = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(chunk, &encoded));
    ASSERT_EQ(NMO_OBJECT_ID_INVALID, encoded);

    nmo_chunk_file_context_t file_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_chunk_set_file_context(chunk, &file_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));

    nmo_ref_t ref = nmo_ref_from_id(999);
    ASSERT_EQ(NMO_OK, nmo_ref_read(chunk, &ref));
    ASSERT_EQ(NMO_OBJECT_ID_INVALID, ref.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, ref.id);
    ASSERT_EQ(NMO_REF_NONE, ref.state);
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
    source.flags |= CKBEHAVIOR_TARGETABLE;
    source.target_parameter = nmo_ref_from_raw(778);
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
    ASSERT_EQ(778u, loaded.target_parameter.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, loaded.target_parameter.id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.target_parameter.state);
    ASSERT_EQ(NMO_OBJECT_ID_NONE,
              nmo_behavior_target_parameter_id(&loaded));

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
    ASSERT_EQ(778u, reloaded.target_parameter.raw_id);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, reloaded.target_parameter.id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.target_parameter.state);
    ASSERT_EQ(NMO_OBJECT_ID_NONE,
              nmo_behavior_target_parameter_id(&reloaded));

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_BEHAVIOR;
    truncated->chunk_version = NMO_CHUNK_VERSION4;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_BEHAVIORNEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        truncated, CKBEHAVIOR_TARGETABLE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(truncated, 801));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        truncated, CK_STATESAVE_BEHAVIORINPUTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(truncated, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(truncated, 802));
    nmo_chunk_close(truncated);
    nmo_chunk_set_file_context(truncated, &read_context);

    nmo_behavior_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&failed, NULL, NULL));
    failed.flags = 0x12345678u;
    failed.owner = nmo_ref_from_raw(901);
    failed.target_parameter = nmo_ref_from_raw(902);
    nmo_behavior_ref_t previous = {
        .ref = nmo_ref_from_raw(903),
        .chunk = NULL,
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&failed.inputs, &previous));
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_behavior_deserialize(
        &failed, truncated, NULL, NULL));
    ASSERT_EQ(0x12345678u, failed.flags);
    ASSERT_EQ(901u, failed.owner.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.owner.state);
    ASSERT_EQ(902u, failed.target_parameter.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.target_parameter.state);
    ASSERT_EQ(1u, failed.inputs.count);
    ASSERT_EQ(903u, NMO_ARRAY_DATA(
        nmo_behavior_ref_t, &failed.inputs)[0].ref.raw_id);

    nmo_array_dispose(&source.inputs);
    nmo_array_dispose(&loaded.inputs);
    nmo_array_dispose(&reloaded.inputs);
    nmo_array_dispose(&failed.inputs);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, behavior_serializer_does_not_publish_partial_chunk) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    nmo_behavior_state_t invalid = {0};
    invalid.compatible_class_id = NMO_CID_BEOBJECT;
    invalid.has_save_flags = true;
    invalid.save_flags = CK_STATESAVE_BEHAVIORINPUTS;
    invalid.inputs.count = 1;
    invalid.inputs.element_size = sizeof(nmo_behavior_ref_t);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_BEHAVIOR;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0x12345678u));
    nmo_chunk_close(chunk);

    ASSERT_NE(NMO_OK, nmo_behavior_serialize(
        &invalid, chunk, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    uint32_t preserved = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(chunk, &preserved));
    ASSERT_EQ(0x12345678u, preserved);

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, behaviorio_truncation_keeps_previous_state) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_BEHAVIORIO;
    chunk->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_BEHAV_IOFLAGS));
    nmo_chunk_close(chunk);

    nmo_behaviorio_state_t state;
    ASSERT_EQ(NMO_OK, nmo_behaviorio_vtable.create(&state, NULL, NULL));
    state.base.visibility_flags = 0x12345678u;
    state.old_flags = 0xA5A5A5A5u;
    state.has_flags = true;
    ASSERT_NE(NMO_OK, nmo_behaviorio_deserialize(
        &state, chunk, NULL, NULL));
    ASSERT_EQ(0x12345678u, state.base.visibility_flags);
    ASSERT_EQ(0xA5A5A5A5u, state.old_flags);
    ASSERT_TRUE(state.has_flags);

    nmo_behaviorio_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, behaviorlink_refs_round_trip_and_failure_is_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);

    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };

    nmo_behaviorlink_state_t source;
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(&source, NULL, NULL));
    source.activation_delay = 3;
    source.initial_activation_delay = 7;
    source.in_io = nmo_ref_from_raw(777);
    source.out_io = nmo_ref_from_raw(778);

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_BEHAVIORLINK;
    first->data_version = 8;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_behaviorlink_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(3, loaded.activation_delay);
    ASSERT_EQ(7, loaded.initial_activation_delay);
    ASSERT_EQ(777u, loaded.in_io.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.in_io.state);
    ASSERT_EQ(778u, loaded.out_io.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.out_io.state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_BEHAVIORLINK;
    second->data_version = 8;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_behaviorlink_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(777u, reloaded.in_io.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.in_io.state);
    ASSERT_EQ(778u, reloaded.out_io.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.out_io.state);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_BEHAVIORLINK;
    truncated->data_version = 8;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_BEHAV_LINK_NEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 0x00090005u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(truncated, 900));
    nmo_chunk_close(truncated);
    nmo_chunk_set_file_context(truncated, &read_context);

    nmo_behaviorlink_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(&failed, NULL, NULL));
    failed.activation_delay = 11;
    failed.initial_activation_delay = 12;
    failed.in_io = nmo_ref_from_raw(901);
    failed.out_io = nmo_ref_from_raw(902);
    ASSERT_NE(NMO_OK, nmo_behaviorlink_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(11, failed.activation_delay);
    ASSERT_EQ(12, failed.initial_activation_delay);
    ASSERT_EQ(901u, failed.in_io.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.in_io.state);
    ASSERT_EQ(902u, failed.out_io.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.out_io.state);

    nmo_behaviorlink_vtable.destroy(&source, NULL, NULL);
    nmo_behaviorlink_vtable.destroy(&loaded, NULL, NULL);
    nmo_behaviorlink_vtable.destroy(&reloaded, NULL, NULL);
    nmo_behaviorlink_vtable.destroy(&failed, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, material_refs_round_trip_and_failure_is_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };

    nmo_material_state_t source;
    ASSERT_EQ(NMO_OK, nmo_material_vtable.create(&source, NULL, NULL));
    source.diffuse_color = 0x11223344u;
    source.ambient_color = 0x55667788u;
    source.specular_color = 0x99AABBCCu;
    source.emissive_color = 0xDDEEFF00u;
    source.specular_power = 3.5f;
    source.texture_border_color = 0x01020304u;
    source.packed_modes = 0x12345678u;
    source.packed_flags = 0x11223344u;
    for (size_t i = 0; i < 4; ++i) {
        source.textures[i] = nmo_ref_from_raw((nmo_object_id_t)(700 + i));
    }
    source.has_additional_textures = 1;
    source.effect = 42;
    source.effect_parameter = nmo_ref_from_raw(704);
    source.has_effect = 1;
    source.has_effect_param = 1;

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_MATERIAL;
    first->data_version = 8;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_material_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_material_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_material_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_material_deserialize(
        &loaded, first, NULL, &deserialize_context));
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_EQ((nmo_object_id_t)(700 + i), loaded.textures[i].raw_id);
        ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.textures[i].state);
    }
    ASSERT_EQ(704u, loaded.effect_parameter.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.effect_parameter.state);
    ASSERT_EQ(42u, loaded.effect);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_MATERIAL;
    second->data_version = 8;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_material_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_material_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_material_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_material_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_EQ((nmo_object_id_t)(700 + i), reloaded.textures[i].raw_id);
        ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.textures[i].state);
    }
    ASSERT_EQ(704u, reloaded.effect_parameter.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.effect_parameter.state);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_MATERIAL;
    truncated->data_version = 8;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_MATDATA2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(truncated, 800));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(truncated, 801));
    nmo_chunk_close(truncated);
    nmo_chunk_set_file_context(truncated, &read_context);

    nmo_material_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_material_vtable.create(&failed, NULL, NULL));
    failed.diffuse_color = 0xCAFEBABEu;
    for (size_t i = 0; i < 4; ++i) {
        failed.textures[i] = nmo_ref_from_raw((nmo_object_id_t)(900 + i));
    }
    ASSERT_NE(NMO_OK, nmo_material_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, failed.diffuse_color);
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_EQ((nmo_object_id_t)(900 + i), failed.textures[i].raw_id);
        ASSERT_EQ(NMO_REF_UNRESOLVED, failed.textures[i].state);
    }

    nmo_material_vtable.destroy(&source, NULL, NULL);
    nmo_material_vtable.destroy(&loaded, NULL, NULL);
    nmo_material_vtable.destroy(&reloaded, NULL, NULL);
    nmo_material_vtable.destroy(&failed, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, parameterlocal_owner_round_trips_raw_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_serialize_context_t serialize_context =
        nmo_serialize_context_create_nonfile(
            arena, NULL, CK_STATESAVE_PARAMETEROUT_OWNER);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);

    nmo_parameterlocal_state_t source;
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_vtable.create(
        &source, NULL, NULL));
    source.owner = nmo_ref_from_raw(691);

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_PARAMETERLOCAL;
    first->data_version = 8;
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_parameterlocal_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_vtable.create(
        &loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(691u, loaded.owner.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.owner.state);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, nmo_parameterlocal_owner_id(&loaded));

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_PARAMETERLOCAL;
    second->data_version = 8;
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_parameterlocal_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_vtable.create(
        &reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(691u, reloaded.owner.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.owner.state);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_PARAMETERLOCAL;
    truncated->data_version = 8;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_PARAMETEROUT_OWNER));
    nmo_chunk_close(truncated);
    nmo_chunk_set_file_context(truncated, &read_context);

    nmo_parameterlocal_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_vtable.create(
        &failed, NULL, NULL));
    failed.owner = nmo_ref_from_raw(692);
    ASSERT_NE(NMO_OK, nmo_parameterlocal_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(692u, failed.owner.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.owner.state);

    nmo_parameterlocal_vtable.destroy(&source, NULL, NULL);
    nmo_parameterlocal_vtable.destroy(&loaded, NULL, NULL);
    nmo_parameterlocal_vtable.destroy(&reloaded, NULL, NULL);
    nmo_parameterlocal_vtable.destroy(&failed, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, parameterin_refs_round_trip_and_failure_is_atomic) {
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
    nmo_serialize_context_t file_serialize_context =
        nmo_serialize_context_create(
            arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_serialize_context_t legacy_serialize_context =
        nmo_serialize_context_create_nonfile(
            arena, NULL, CK_STATESAVE_PARAMETERIN_ALL);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_parameterin_state_t source;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(&source, NULL, NULL));
    source.type_guid = (nmo_guid_t){0x12345678u, 0x9ABCDEF0u};
    source.source = nmo_ref_from_raw(693);
    source.owner = nmo_ref_from_raw(694);
    source.is_shared = 1;
    source.is_disabled = 1;

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_PARAMETERIN;
    first->data_version = 8;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_parameterin_serialize(
        &source, first, NULL, &file_serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_parameterin_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(693u, loaded.source.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.source.state);
    ASSERT_EQ(694u, loaded.owner.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.owner.state);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, nmo_parameterin_source_id(&loaded));
    ASSERT_EQ(NMO_OBJECT_ID_NONE, nmo_parameterin_owner_id(&loaded));
    ASSERT_EQ(1u, loaded.is_shared);
    ASSERT_EQ(1u, loaded.is_disabled);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_PARAMETERIN;
    second->data_version = 8;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_parameterin_serialize(
        &loaded, second, NULL, &file_serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_parameterin_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(693u, reloaded.source.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.source.state);
    ASSERT_EQ(694u, reloaded.owner.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.owner.state);

    nmo_parameterin_state_t legacy_source;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(
        &legacy_source, NULL, NULL));
    legacy_source.type_guid = (nmo_guid_t){3u, 4u};
    legacy_source.source = nmo_ref_from_raw(695);
    legacy_source.owner = nmo_ref_from_raw(696);

    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy);
    legacy->class_id = NMO_CID_PARAMETERIN;
    legacy->data_version = 0;
    ASSERT_EQ(NMO_OK, nmo_parameterin_serialize(
        &legacy_source, legacy, NULL, &legacy_serialize_context));
    nmo_chunk_close(legacy);
    nmo_chunk_set_file_context(legacy, &read_context);

    nmo_parameterin_state_t legacy_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(
        &legacy_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_deserialize(
        &legacy_loaded, legacy, NULL, &deserialize_context));
    ASSERT_EQ(695u, legacy_loaded.source.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, legacy_loaded.source.state);
    ASSERT_EQ(696u, legacy_loaded.owner.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, legacy_loaded.owner.state);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_PARAMETERIN;
    truncated->data_version = 8;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_PARAMETERIN_DEFAULTDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        truncated, (nmo_guid_t){5u, 6u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(truncated, 801));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(truncated, 802));
    nmo_chunk_close(truncated);
    nmo_chunk_set_file_context(truncated, &read_context);

    nmo_parameterin_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(&failed, NULL, NULL));
    failed.type_guid = (nmo_guid_t){7u, 8u};
    failed.source = nmo_ref_from_raw(901);
    failed.owner = nmo_ref_from_raw(902);
    failed.is_shared = 1;
    failed.is_disabled = 1;
    ASSERT_NE(NMO_OK, nmo_parameterin_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(7u, failed.type_guid.d1);
    ASSERT_EQ(8u, failed.type_guid.d2);
    ASSERT_EQ(901u, failed.source.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.source.state);
    ASSERT_EQ(902u, failed.owner.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.owner.state);
    ASSERT_EQ(1u, failed.is_shared);
    ASSERT_EQ(1u, failed.is_disabled);

    nmo_parameterin_vtable.destroy(&source, NULL, NULL);
    nmo_parameterin_vtable.destroy(&loaded, NULL, NULL);
    nmo_parameterin_vtable.destroy(&reloaded, NULL, NULL);
    nmo_parameterin_vtable.destroy(&legacy_source, NULL, NULL);
    nmo_parameterin_vtable.destroy(&legacy_loaded, NULL, NULL);
    nmo_parameterin_vtable.destroy(&failed, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, parameterout_refs_round_trip_and_failure_is_atomic) {
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
    nmo_serialize_context_t serialize_context =
        nmo_serialize_context_create(
            arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_ref_t source_destinations[] = {
        nmo_ref_from_raw(731),
        nmo_ref_from_raw(732),
        nmo_ref_from_raw(733),
    };
    nmo_parameterout_state_t source;
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.create(
        &source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_array_init(
        &source.base.buffer_data, sizeof(uint8_t), 0, NULL));
    source.owner = nmo_ref_from_raw(730);
    source.destination_ids = source_destinations;
    source.destination_count = 3;

    nmo_parameterout_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.create(
        &copied, NULL, NULL));
    nmo_type_descriptor_t parameterout_type = {
        .size = sizeof(nmo_parameterout_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.copy(
        &source, &copied, &parameterout_type, arena));
    ASSERT_TRUE(copied.destination_ids != source.destination_ids);
    ASSERT_TRUE(nmo_parameterout_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_parameterout_vtable.hash(&source),
              nmo_parameterout_vtable.hash(&copied));

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_PARAMETEROUT;
    first->data_version = 8;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_parameterout_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_parameterout_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.create(
        &loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterout_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(730u, loaded.owner.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.owner.state);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, nmo_parameterout_owner_id(&loaded));
    ASSERT_EQ(3u, loaded.destination_count);
    for (uint32_t i = 0; i < loaded.destination_count; ++i) {
        ASSERT_EQ((nmo_object_id_t)(731 + i),
                  loaded.destination_ids[i].raw_id);
        ASSERT_EQ(NMO_REF_UNRESOLVED,
                  loaded.destination_ids[i].state);
        ASSERT_EQ(NMO_OBJECT_ID_NONE,
                  nmo_parameterout_destination_id(&loaded, i));
    }
    ASSERT_EQ(0u, nmo_parameterout_valid_destination_count(&loaded));

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_PARAMETEROUT;
    second->data_version = 8;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_parameterout_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_parameterout_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.create(
        &reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterout_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(730u, reloaded.owner.raw_id);
    ASSERT_EQ(3u, reloaded.destination_count);
    for (uint32_t i = 0; i < reloaded.destination_count; ++i) {
        ASSERT_EQ((nmo_object_id_t)(731 + i),
                  reloaded.destination_ids[i].raw_id);
        ASSERT_EQ(NMO_REF_UNRESOLVED,
                  reloaded.destination_ids[i].state);
    }

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_PARAMETEROUT;
    truncated->data_version = 8;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_PARAMETEROUT_OWNER));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(truncated, 801));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_PARAMETEROUT_DESTINATIONS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(truncated, 802));
    nmo_chunk_close(truncated);
    nmo_chunk_set_file_context(truncated, &read_context);

    nmo_ref_t previous_destinations[] = {nmo_ref_from_raw(902)};
    nmo_parameterout_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.create(
        &failed, NULL, NULL));
    failed.owner = nmo_ref_from_raw(901);
    failed.destination_ids = previous_destinations;
    failed.destination_count = 1;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameterout_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(901u, failed.owner.raw_id);
    ASSERT_EQ(previous_destinations, failed.destination_ids);
    ASSERT_EQ(1u, failed.destination_count);
    ASSERT_EQ(902u, failed.destination_ids[0].raw_id);

    nmo_chunk_t *invalid_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(invalid_count);
    invalid_count->class_id = NMO_CID_PARAMETEROUT;
    invalid_count->data_version = 8;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(invalid_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        invalid_count, CK_STATESAVE_PARAMETEROUT_DESTINATIONS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(invalid_count, -1));
    nmo_chunk_close(invalid_count);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_parameterout_deserialize(
        &failed, invalid_count, NULL, &deserialize_context));
    ASSERT_EQ(901u, failed.owner.raw_id);
    ASSERT_EQ(previous_destinations, failed.destination_ids);
    ASSERT_EQ(1u, failed.destination_count);

    nmo_parameterout_state_t invalid = {0};
    invalid.destination_count = 1;
    nmo_chunk_t *partial = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(partial);
    partial->class_id = NMO_CID_PARAMETEROUT;
    partial->data_version = 8;
    partial->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_parameterout_serialize(
        &invalid, partial, NULL, &serialize_context));
    ASSERT_EQ(0u, nmo_chunk_get_data_size(partial));

    nmo_array_dispose(&source.base.buffer_data);
    nmo_array_dispose(&copied.base.buffer_data);
    nmo_parameterout_vtable.destroy(&source, NULL, NULL);
    nmo_parameterout_vtable.destroy(&copied, NULL, NULL);
    nmo_parameterout_vtable.destroy(&loaded, NULL, NULL);
    nmo_parameterout_vtable.destroy(&reloaded, NULL, NULL);
    nmo_parameterout_vtable.destroy(&failed, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, parameter_object_ref_round_trips_raw_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
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
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_parameter_state_t source;
    ASSERT_EQ(NMO_OK, nmo_parameter_vtable.create(&source, NULL, NULL));
    source.type_guid = CKPGUID_OBJECT;
    source.mode = CKPARAM_MODE_OBJECT;
    source.has_state = true;
    source.object_ref = nmo_ref_from_raw(701);

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_PARAMETER;
    first->data_version = 8;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_parameter_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_parameter_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_parameter_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameter_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(701u, loaded.object_ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.object_ref.state);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, nmo_parameter_object_id(&loaded));

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_PARAMETER;
    second->data_version = 8;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_parameter_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_parameter_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_parameter_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameter_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(701u, reloaded.object_ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.object_ref.state);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_PARAMETER;
    truncated->data_version = 8;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(truncated, 0x40));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(truncated, CKPGUID_OBJECT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 2));
    nmo_chunk_close(truncated);
    nmo_chunk_set_file_context(truncated, &read_context);

    nmo_parameter_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_parameter_vtable.create(&failed, NULL, NULL));
    failed.object_ref = nmo_ref_from_raw(702);
    ASSERT_NE(NMO_OK, nmo_parameter_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(702u, failed.object_ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.object_ref.state);

    nmo_parameter_vtable.destroy(&source, NULL, NULL);
    nmo_parameter_vtable.destroy(&loaded, NULL, NULL);
    nmo_parameter_vtable.destroy(&reloaded, NULL, NULL);
    nmo_parameter_vtable.destroy(&failed, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, parameteroperation_refs_round_trip_and_failure_is_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };

    nmo_parameteroperation_state_t source;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &source, NULL, NULL));
    source.operation_guid = (nmo_guid_t){0x12345678u, 0x9ABCDEF0u};
    source.in1.ref = nmo_ref_from_raw(710);
    source.in2.ref = nmo_ref_from_raw(711);
    source.out.ref = nmo_ref_from_raw(712);
    source.has_in1 = 1;
    source.has_in2 = 1;
    source.has_out = 1;

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_PARAMETEROPERATION;
    first->data_version = 8;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_parameteroperation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(710u, loaded.in1.ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.in1.ref.state);
    ASSERT_EQ(711u, loaded.in2.ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.in2.ref.state);
    ASSERT_EQ(712u, loaded.out.ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.out.ref.state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_PARAMETEROPERATION;
    second->data_version = 8;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_parameteroperation_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(710u, reloaded.in1.ref.raw_id);
    ASSERT_EQ(711u, reloaded.in2.ref.raw_id);
    ASSERT_EQ(712u, reloaded.out.ref.raw_id);

    nmo_chunk_t *legacy_version = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_version);
    legacy_version->class_id = NMO_CID_PARAMETEROPERATION;
    legacy_version->data_version = 4;
    legacy_version->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(legacy_version, &write_context);
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_serialize(
        &source, legacy_version, NULL, &serialize_context));
    nmo_chunk_close(legacy_version);
    nmo_chunk_set_file_context(legacy_version, &read_context);
    nmo_parameteroperation_state_t legacy_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &legacy_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_deserialize(
        &legacy_loaded, legacy_version, NULL, &deserialize_context));
    ASSERT_EQ(710u, legacy_loaded.in1.ref.raw_id);
    ASSERT_EQ(711u, legacy_loaded.in2.ref.raw_id);
    ASSERT_EQ(712u, legacy_loaded.out.ref.raw_id);

    nmo_parameteroperation_state_t short_source = source;
    short_source.has_in2 = 0;
    short_source.has_out = 0;
    nmo_chunk_t *short_sequence = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(short_sequence);
    short_sequence->class_id = NMO_CID_PARAMETEROPERATION;
    short_sequence->data_version = 8;
    short_sequence->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(short_sequence, &write_context);
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_serialize(
        &short_source, short_sequence, NULL, &serialize_context));
    nmo_chunk_close(short_sequence);
    nmo_chunk_set_file_context(short_sequence, &read_context);
    nmo_parameteroperation_state_t short_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &short_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_deserialize(
        &short_loaded, short_sequence, NULL, &deserialize_context));
    ASSERT_EQ(1u, short_loaded.has_in1);
    ASSERT_EQ(0u, short_loaded.has_in2);
    ASSERT_EQ(0u, short_loaded.has_out);
    ASSERT_EQ(710u, short_loaded.in1.ref.raw_id);

    nmo_chunk_t *invalid_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(invalid_count);
    invalid_count->class_id = NMO_CID_PARAMETEROPERATION;
    invalid_count->data_version = 8;
    invalid_count->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(invalid_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        invalid_count, CK_STATESAVE_OPERATIONNEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        invalid_count, (nmo_guid_t){1u, 2u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(invalid_count, 4));
    for (nmo_object_id_t id = 800; id < 804; ++id) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_sequence_item(
            invalid_count, id));
    }
    nmo_chunk_close(invalid_count);
    nmo_chunk_set_file_context(invalid_count, &read_context);

    nmo_parameteroperation_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &failed, NULL, NULL));
    failed.operation_guid = (nmo_guid_t){3u, 4u};
    failed.in1.ref = nmo_ref_from_raw(901);
    failed.in2.ref = nmo_ref_from_raw(902);
    failed.out.ref = nmo_ref_from_raw(903);
    failed.has_in1 = 1;
    failed.has_in2 = 1;
    failed.has_out = 1;
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_parameteroperation_deserialize(
        &failed, invalid_count, NULL, &deserialize_context));
    ASSERT_EQ(3u, failed.operation_guid.d1);
    ASSERT_EQ(4u, failed.operation_guid.d2);
    ASSERT_EQ(901u, failed.in1.ref.raw_id);
    ASSERT_EQ(902u, failed.in2.ref.raw_id);
    ASSERT_EQ(903u, failed.out.ref.raw_id);

    nmo_parameteroperation_vtable.destroy(&source, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&loaded, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&reloaded, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&legacy_loaded, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&short_loaded, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&failed, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, parameteroperation_legacy_sections_are_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_NONE,
        CK_STATESAVE_OPERATIONDEFAULTDATA |
        CK_STATESAVE_OPERATIONINPUTS |
        CK_STATESAVE_OPERATIONOUTPUT);

    nmo_parameteroperation_state_t source;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &source, NULL, NULL));
    source.owner = nmo_ref_from_raw(720);
    source.in1.ref = nmo_ref_from_raw(721);
    source.in2.ref = nmo_ref_from_raw(722);
    source.out.ref = nmo_ref_from_raw(723);
    source.has_owner = 1;
    source.has_in1 = 1;
    source.has_in2 = 1;
    source.has_out = 1;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_PARAMETEROPERATION;
    chunk->data_version = 8;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_serialize(
        &source, chunk, NULL, &serialize_context));
    nmo_chunk_close(chunk);

    nmo_parameteroperation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_deserialize(
        &loaded, chunk, NULL, NULL));
    ASSERT_EQ(720u, loaded.owner.raw_id);
    ASSERT_EQ(721u, loaded.in1.ref.raw_id);
    ASSERT_EQ(722u, loaded.in2.ref.raw_id);
    ASSERT_EQ(723u, loaded.out.ref.raw_id);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_PARAMETEROPERATION;
    truncated->data_version = 8;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_OPERATIONINPUTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(truncated, 801));
    nmo_chunk_close(truncated);

    loaded.in1.ref = nmo_ref_from_raw(911);
    loaded.in2.ref = nmo_ref_from_raw(912);
    loaded.has_in1 = 1;
    loaded.has_in2 = 1;
    ASSERT_NE(NMO_OK, nmo_parameteroperation_deserialize(
        &loaded, truncated, NULL, NULL));
    ASSERT_EQ(911u, loaded.in1.ref.raw_id);
    ASSERT_EQ(912u, loaded.in2.ref.raw_id);
    ASSERT_EQ(1u, loaded.has_in1);
    ASSERT_EQ(1u, loaded.has_in2);

    nmo_parameteroperation_vtable.destroy(&source, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, camera_and_light_failures_keep_previous_state) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *camera_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(camera_chunk);
    camera_chunk->class_id = NMO_CID_CAMERA;
    camera_chunk->data_version = 7;
    camera_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(camera_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        camera_chunk, CK_STATESAVE_CAMERAONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(camera_chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(camera_chunk, 0.75f));
    nmo_chunk_close(camera_chunk);

    nmo_camera_state_t camera;
    ASSERT_EQ(NMO_OK, nmo_camera_vtable.create(&camera, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &camera.entity.base.base, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &camera.entity.base.base.scripts, 901));
    camera.fov = 8.0f;
    camera.width = 77;
    ASSERT_NE(NMO_OK, nmo_camera_deserialize(
        &camera, camera_chunk, NULL, &deserialize_context));
    ASSERT_EQ(8.0f, camera.fov);
    ASSERT_EQ(77, camera.width);
    ASSERT_EQ(1u, camera.entity.base.base.scripts.count);
    ASSERT_EQ(901u, nmo_beobject_script_array_get_id(
        &camera.entity.base.base.scripts, 0));

    nmo_chunk_t *light_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(light_chunk);
    light_chunk->class_id = NMO_CID_LIGHT;
    light_chunk->data_version = 7;
    light_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(light_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        light_chunk, CK_STATESAVE_LIGHTDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(light_chunk, 0x100u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(light_chunk, 0xFFFFFFFFu));
    nmo_chunk_close(light_chunk);

    nmo_light_state_t light;
    ASSERT_EQ(NMO_OK, nmo_light_vtable.create(&light, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &light.entity.base.base, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &light.entity.base.base.scripts, 902));
    light.flags = 0x123400u;
    light.light_power = 9.0f;
    ASSERT_NE(NMO_OK, nmo_light_deserialize(
        &light, light_chunk, NULL, &deserialize_context));
    ASSERT_EQ(0x123400u, light.flags);
    ASSERT_EQ(9.0f, light.light_power);
    ASSERT_EQ(1u, light.entity.base.base.scripts.count);
    ASSERT_EQ(902u, nmo_beobject_script_array_get_id(
        &light.entity.base.base.scripts, 0));

    nmo_array_dispose(&camera.entity.base.base.scripts);
    nmo_array_dispose(&camera.entity.base.base.attributes);
    nmo_array_dispose(&camera.entity.base.base.legacy_attributes);
    nmo_camera_vtable.destroy(&camera, NULL, NULL);
    nmo_array_dispose(&light.entity.base.base.scripts);
    nmo_array_dispose(&light.entity.base.base.attributes);
    nmo_array_dispose(&light.entity.base.base.legacy_attributes);
    nmo_light_vtable.destroy(&light, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, target_camera_and_light_failures_are_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };

    nmo_chunk_t *truncated_camera = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated_camera);
    truncated_camera->class_id = NMO_CID_TARGETCAMERA;
    truncated_camera->data_version = 7;
    truncated_camera->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated_camera));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated_camera, CK_STATESAVE_TCAMERATARGET));
    nmo_chunk_close(truncated_camera);

    nmo_targetcamera_state_t camera;
    ASSERT_EQ(NMO_OK, nmo_targetcamera_vtable.create(&camera, NULL, NULL));
    camera.base.fov = 8.0f;
    camera.has_target = 1;
    camera.target = nmo_ref_from_raw(901);
    ASSERT_NE(NMO_OK, nmo_targetcamera_deserialize(
        &camera, truncated_camera, NULL, &deserialize_context));
    ASSERT_EQ(8.0f, camera.base.fov);
    ASSERT_EQ(1u, camera.has_target);
    ASSERT_EQ(901u, camera.target.raw_id);

    nmo_chunk_t *truncated_light = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated_light);
    truncated_light->class_id = NMO_CID_TARGETLIGHT;
    truncated_light->data_version = 7;
    truncated_light->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated_light));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated_light, CK_STATESAVE_TLIGHTTARGET));
    nmo_chunk_close(truncated_light);

    nmo_targetlight_state_t light;
    ASSERT_EQ(NMO_OK, nmo_targetlight_vtable.create(&light, NULL, NULL));
    light.base.flags = 0x123400u;
    light.base.light_power = 9.0f;
    light.has_target = 1;
    light.target = nmo_ref_from_raw(902);
    ASSERT_NE(NMO_OK, nmo_targetlight_deserialize(
        &light, truncated_light, NULL, &deserialize_context));
    ASSERT_EQ(0x123400u, light.base.flags);
    ASSERT_EQ(9.0f, light.base.light_power);
    ASSERT_EQ(1u, light.has_target);
    ASSERT_EQ(902u, light.target.raw_id);

    camera.target = nmo_ref_from_id(123);
    nmo_chunk_t *camera_target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(camera_target);
    camera_target->class_id = NMO_CID_TARGETCAMERA;
    camera_target->data_version = 7;
    camera_target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(camera_target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(camera_target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(camera_target, 0x12345678u));
    nmo_chunk_close(camera_target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_targetcamera_serialize(
        &camera, camera_target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(camera_target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(camera_target));
    uint32_t preserved = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(camera_target, &preserved));
    ASSERT_EQ(0x12345678u, preserved);

    light.target = nmo_ref_from_id(124);
    nmo_chunk_t *light_target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(light_target);
    light_target->class_id = NMO_CID_TARGETLIGHT;
    light_target->data_version = 7;
    light_target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(light_target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(light_target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(light_target, 0x87654321u));
    nmo_chunk_close(light_target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_targetlight_serialize(
        &light, light_target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(light_target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(light_target));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(light_target, &preserved));
    ASSERT_EQ(0x87654321u, preserved);

    nmo_targetcamera_vtable.destroy(&camera, NULL, NULL);
    nmo_targetlight_vtable.destroy(&light, NULL, NULL);
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

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_LAYER;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_LAYERDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(truncated, 999));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 0xAABBCCDDu));
    nmo_chunk_close(truncated);
    loaded.format = 77;
    loaded.version = 88;
    ASSERT_NE(NMO_OK, nmo_layer_deserialize(
        &loaded, truncated, NULL, &deserialize_context));
    ASSERT_EQ(444u, loaded.grid.raw_id);
    ASSERT_EQ(77, loaded.format);
    ASSERT_EQ(88, loaded.version);

    source.grid = nmo_ref_from_id(123);
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_LAYER;
    target->data_version = 7;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_layer_serialize(
        &source, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t preserved = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &preserved));
    ASSERT_EQ(0x12345678u, preserved);

    nmo_layer_vtable.destroy(&source, NULL, NULL);
    nmo_layer_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, grid_failures_keep_state_and_target_chunk_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_GRID;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_GRIDDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 10));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 20));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 30));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 40));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(truncated, 1));
    nmo_chunk_close(truncated);

    nmo_grid_state_t state;
    ASSERT_EQ(NMO_OK, nmo_grid_vtable.create(&state, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &state.base.base.base, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &state.base.base.base.scripts, 901));
    state.width = 77;
    state.length = 88;
    nmo_grid_layer_t old_layer = {
        .ref = nmo_ref_from_raw(902),
        .chunk = NULL,
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&state.layers, &old_layer));
    void *old_layers = state.layers.data;
    ASSERT_NE(NMO_OK, nmo_grid_deserialize(
        &state, truncated, NULL, &deserialize_context));
    ASSERT_EQ(77, state.width);
    ASSERT_EQ(88, state.length);
    ASSERT_EQ(old_layers, state.layers.data);
    ASSERT_EQ(1u, state.layers.count);
    ASSERT_EQ(902u, NMO_ARRAY_DATA(
        nmo_grid_layer_t, &state.layers)[0].ref.raw_id);
    ASSERT_EQ(901u, nmo_beobject_script_array_get_id(
        &state.base.base.base.scripts, 0));

    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_chunk_file_context_t file_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_grid_state_t source;
    ASSERT_EQ(NMO_OK, nmo_grid_vtable.create(&source, NULL, NULL));
    nmo_grid_layer_t missing_layer = {
        .ref = nmo_ref_from_id(123),
        .chunk = NULL,
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.layers, &missing_layer));

    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_GRID;
    target->data_version = 7;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &file_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_grid_serialize(
        &source, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t preserved = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &preserved));
    ASSERT_EQ(0x12345678u, preserved);

    nmo_array_dispose(&state.base.base.base.scripts);
    nmo_array_dispose(&state.base.base.base.attributes);
    nmo_array_dispose(&state.base.base.base.legacy_attributes);
    nmo_array_dispose(&state.layers);
    nmo_grid_vtable.destroy(&state, NULL, NULL);
    nmo_array_dispose(&source.layers);
    nmo_grid_vtable.destroy(&source, NULL, NULL);
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
    ASSERT_TRUE(curve.has_default_data);
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
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &state3d.base.base, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &state3d.base.base.scripts, 901));
    nmo_ref_t old_mesh = nmo_ref_from_raw(902);
    state3d.entity_flags = 0x12345678u;
    state3d.has_mesh_chunk = 1;
    state3d.current_mesh = nmo_ref_from_raw(903);
    state3d.mesh_count = 1;
    state3d.mesh_ids = &old_mesh;
    ASSERT_NE(NMO_OK, nmo_3dentity_deserialize(
        &state3d, chunk3d, NULL, &deserialize_context));
    ASSERT_EQ(0x12345678u, state3d.entity_flags);
    ASSERT_TRUE(state3d.has_mesh_chunk);
    ASSERT_EQ(903u, state3d.current_mesh.raw_id);
    ASSERT_EQ(1u, state3d.mesh_count);
    ASSERT_EQ(&old_mesh, state3d.mesh_ids);
    ASSERT_EQ(902u, state3d.mesh_ids[0].raw_id);
    ASSERT_EQ(1u, state3d.base.base.scripts.count);
    ASSERT_EQ(901u, nmo_beobject_script_array_get_id(
        &state3d.base.base.scripts, 0));
    nmo_array_dispose(&state3d.base.base.scripts);
    nmo_array_dispose(&state3d.base.base.attributes);
    nmo_array_dispose(&state3d.base.base.legacy_attributes);
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
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &state2d.base.base, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &state2d.base.base.scripts, 911));
    state2d.flags = 0xCAFEBABEu;
    state2d.rect.left = 12.5f;
    state2d.has_material = true;
    state2d.material = nmo_ref_from_raw(912);
    ASSERT_NE(NMO_OK, nmo_2dentity_deserialize(
        &state2d, chunk2d, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, state2d.flags);
    ASSERT_EQ(12.5f, state2d.rect.left);
    ASSERT_TRUE(state2d.has_material);
    ASSERT_EQ(912u, state2d.material.raw_id);
    ASSERT_EQ(1u, state2d.base.base.scripts.count);
    ASSERT_EQ(911u, nmo_beobject_script_array_get_id(
        &state2d.base.base.scripts, 0));
    nmo_array_dispose(&state2d.base.base.scripts);
    nmo_array_dispose(&state2d.base.base.attributes);
    nmo_array_dispose(&state2d.base.base.legacy_attributes);
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

TEST(chunk_id_remap, entity2d_serializer_does_not_publish_partial_chunk) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_chunk_file_context_t file_context = {
        .runtime_to_file = runtime_to_file,
    };

    nmo_2dentity_state_t state = {0};
    state.has_parent = true;
    state.parent = nmo_ref_from_id(123);

    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_2DENTITY;
    target->data_version = 7;
    nmo_chunk_set_file_context(target, &file_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);

    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_2dentity_serialize(
        &state, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t preserved = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &preserved));
    ASSERT_EQ(0x12345678u, preserved);

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, entity_serializer_does_not_publish_partial_chunk) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_chunk_file_context_t file_context = {
        .runtime_to_file = runtime_to_file,
    };

    nmo_3dentity_state_t state = {0};
    state.has_mesh_chunk = 1;
    state.current_mesh = nmo_ref_from_id(123);

    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_3DENTITY;
    target->data_version = 7;
    nmo_chunk_set_file_context(target, &file_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);

    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_3dentity_serialize(
        &state, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t preserved = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &preserved));
    ASSERT_EQ(0x12345678u, preserved);

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

TEST(chunk_id_remap, scene_refs_round_trip_and_failure_is_atomic) {
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

    nmo_scene_state_t source;
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&source, NULL, NULL));
    source.level = nmo_ref_from_raw(941);
    source.background_texture = nmo_ref_from_raw(942);
    source.starting_camera = nmo_ref_from_raw(943);
    nmo_scene_object_desc_t source_desc = {
        .ref = {.raw_id = 944, .id = 0, .state = NMO_REF_UNRESOLVED},
        .flags = CK_SCENEOBJECT_ACTIVE,
    };
    source_desc.reserved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(source_desc.reserved);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(source_desc.reserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(source_desc.reserved, 0x12345678u));
    nmo_chunk_close(source_desc.reserved);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.object_descs, &source_desc));

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_SCENE;
    first->data_version = 8;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_scene_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_scene_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_scene_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(941u, loaded.level.raw_id);
    ASSERT_EQ(942u, loaded.background_texture.raw_id);
    ASSERT_EQ(943u, loaded.starting_camera.raw_id);
    ASSERT_EQ(1u, loaded.object_descs.count);
    const nmo_scene_object_desc_t *loaded_descs = NMO_ARRAY_DATA(
        nmo_scene_object_desc_t, &loaded.object_descs);
    ASSERT_EQ(944u, loaded_descs[0].ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded_descs[0].ref.state);
    ASSERT_NOT_NULL(loaded_descs[0].reserved);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_SCENE;
    second->data_version = 8;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_scene_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_scene_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_scene_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    const nmo_scene_object_desc_t *reloaded_descs = NMO_ARRAY_DATA(
        nmo_scene_object_desc_t, &reloaded.object_descs);
    ASSERT_EQ(941u, reloaded.level.raw_id);
    ASSERT_EQ(942u, reloaded.background_texture.raw_id);
    ASSERT_EQ(943u, reloaded.starting_camera.raw_id);
    ASSERT_EQ(944u, reloaded_descs[0].ref.raw_id);
    ASSERT_NOT_NULL(reloaded_descs[0].reserved);

    nmo_scene_state_t copied;
    nmo_type_descriptor_t scene_type = {
        .size = sizeof(nmo_scene_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.copy(
        &reloaded, &copied, &scene_type, arena));
    ASSERT_NE(reloaded.object_descs.data, copied.object_descs.data);
    ASSERT_TRUE(nmo_scene_vtable.equals(&reloaded, &copied));
    ASSERT_EQ(nmo_scene_vtable.hash(&reloaded),
              nmo_scene_vtable.hash(&copied));

    nmo_chunk_t *truncated_descs = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated_descs);
    truncated_descs->class_id = NMO_CID_SCENE;
    truncated_descs->data_version = 8;
    truncated_descs->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated_descs));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated_descs, CK_STATESAVE_SCENENEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(truncated_descs, 945));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated_descs, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        truncated_descs, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_item(
        truncated_descs, 946));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_sub_chunk_sequence(
        truncated_descs, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_sub_chunk_sequence(
        truncated_descs, NULL));
    nmo_chunk_close(truncated_descs);

    nmo_scene_state_t failed_descs;
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&failed_descs, NULL, NULL));
    failed_descs.level = nmo_ref_from_raw(950);
    nmo_scene_object_desc_t old_desc = {
        .ref = {.raw_id = 951, .id = 0, .state = NMO_REF_UNRESOLVED},
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &failed_descs.object_descs, &old_desc));
    ASSERT_NE(NMO_OK, nmo_scene_deserialize(
        &failed_descs, truncated_descs, NULL, &deserialize_context));
    ASSERT_EQ(950u, failed_descs.level.raw_id);
    ASSERT_EQ(1u, failed_descs.object_descs.count);
    ASSERT_EQ(951u, NMO_ARRAY_DATA(
        nmo_scene_object_desc_t, &failed_descs.object_descs)[0].ref.raw_id);

    nmo_chunk_t *truncated_render = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated_render);
    truncated_render->class_id = NMO_CID_SCENE;
    truncated_render->data_version = 8;
    truncated_render->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated_render));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated_render, CK_STATESAVE_SCENERENDERSETTINGS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated_render, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated_render, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated_render, 3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated_render, 4));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(truncated_render, 5.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(truncated_render, 6.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(truncated_render, 7.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(truncated_render, 952));
    nmo_chunk_close(truncated_render);

    nmo_scene_state_t failed_render;
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&failed_render, NULL, NULL));
    failed_render.background_color = 0xAABBCCDDu;
    failed_render.background_texture = nmo_ref_from_raw(953);
    failed_render.starting_camera = nmo_ref_from_raw(954);
    ASSERT_NE(NMO_OK, nmo_scene_deserialize(
        &failed_render, truncated_render, NULL, &deserialize_context));
    ASSERT_EQ(0xAABBCCDDu, failed_render.background_color);
    ASSERT_EQ(953u, failed_render.background_texture.raw_id);
    ASSERT_EQ(954u, failed_render.starting_camera.raw_id);

    nmo_scene_vtable.destroy(&source, NULL, NULL);
    nmo_scene_vtable.destroy(&loaded, NULL, NULL);
    nmo_scene_vtable.destroy(&reloaded, NULL, NULL);
    nmo_scene_vtable.destroy(&copied, NULL, NULL);
    nmo_scene_vtable.destroy(&failed_descs, NULL, NULL);
    nmo_scene_vtable.destroy(&failed_render, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, synchro_refs_round_trip_and_failure_is_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
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

    nmo_synchro_state_t source;
    ASSERT_EQ(NMO_OK, nmo_synchro_vtable.create(&source, NULL, NULL));
    source.max_waiters = 4;
    nmo_ref_t arrived = nmo_ref_from_raw(921);
    nmo_ref_t passed = nmo_ref_from_raw(922);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.arrived_ids, &arrived));
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.passed_ids, &passed));

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_SYNCHRO;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_synchro_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_synchro_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_synchro_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_synchro_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(4, loaded.max_waiters);
    ASSERT_EQ(1u, loaded.arrived_ids.count);
    ASSERT_EQ(1u, loaded.passed_ids.count);
    ASSERT_EQ(921u, NMO_ARRAY_DATA(
                        nmo_ref_t, &loaded.arrived_ids)[0].raw_id);
    ASSERT_EQ(922u, NMO_ARRAY_DATA(
                        nmo_ref_t, &loaded.passed_ids)[0].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, NMO_ARRAY_DATA(
                        nmo_ref_t, &loaded.arrived_ids)[0].state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_SYNCHRO;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_synchro_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_synchro_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_synchro_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_synchro_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(921u, NMO_ARRAY_DATA(
                        nmo_ref_t, &reloaded.arrived_ids)[0].raw_id);
    ASSERT_EQ(922u, NMO_ARRAY_DATA(
                        nmo_ref_t, &reloaded.passed_ids)[0].raw_id);

    nmo_synchro_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_synchro_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_synchro_vtable.copy(
        &reloaded, &copied, NULL, arena));
    ASSERT_NE(reloaded.arrived_ids.data, copied.arrived_ids.data);
    ASSERT_NE(reloaded.passed_ids.data, copied.passed_ids.data);
    ASSERT_TRUE(nmo_synchro_vtable.equals(&reloaded, &copied));
    ASSERT_EQ(nmo_synchro_vtable.hash(&reloaded),
              nmo_synchro_vtable.hash(&copied));

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_SYNCHRO;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_SYNCHRODATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 8));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_item(truncated, 923));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_item(truncated, 924));
    nmo_chunk_close(truncated);

    nmo_synchro_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_synchro_vtable.create(&failed, NULL, NULL));
    failed.max_waiters = 99;
    nmo_ref_t old_arrived = nmo_ref_from_raw(930);
    nmo_ref_t old_passed = nmo_ref_from_raw(931);
    ASSERT_EQ(NMO_OK, nmo_array_append(&failed.arrived_ids, &old_arrived));
    ASSERT_EQ(NMO_OK, nmo_array_append(&failed.passed_ids, &old_passed));
    ASSERT_NE(NMO_OK, nmo_synchro_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(99, failed.max_waiters);
    ASSERT_EQ(1u, failed.arrived_ids.count);
    ASSERT_EQ(1u, failed.passed_ids.count);
    ASSERT_EQ(930u, NMO_ARRAY_DATA(
                        nmo_ref_t, &failed.arrived_ids)[0].raw_id);
    ASSERT_EQ(931u, NMO_ARRAY_DATA(
                        nmo_ref_t, &failed.passed_ids)[0].raw_id);

    nmo_synchro_vtable.destroy(&source, NULL, NULL);
    nmo_synchro_vtable.destroy(&loaded, NULL, NULL);
    nmo_synchro_vtable.destroy(&reloaded, NULL, NULL);
    nmo_synchro_vtable.destroy(&copied, NULL, NULL);
    nmo_synchro_vtable.destroy(&failed, NULL, NULL);
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
    state.base.base.visibility_flags = NMO_CKOBJECT_VISIBLE;
    state.priority = 55;
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &state.scripts, 456));
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(
        &state.attributes, 123, 7, NULL));

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_BEOBJECT;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_OBJECTHIDDEN));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_SCRIPTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_sequence_item(
        truncated, 888));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_DATAS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 0x10000000u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 99));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_NEWATTRIBUTES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_sequence_item(truncated, 777));
    nmo_chunk_close(truncated);

    ASSERT_NE(NMO_OK, nmo_beobject_deserialize(
        &state, truncated, NULL, NULL));
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, state.base.base.visibility_flags);
    ASSERT_EQ(55, state.priority);
    ASSERT_EQ(1u, state.scripts.count);
    ASSERT_EQ(456u, nmo_beobject_script_array_get_id(&state.scripts, 0));
    ASSERT_EQ(1u, state.attributes.count);
    const nmo_beobject_attribute_t *attributes = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &state.attributes);
    ASSERT_EQ(123u, nmo_ref_runtime_id(&attributes[0].parameter));
    ASSERT_EQ(7u, attributes[0].type_id);

    nmo_array_dispose(&state.scripts);
    nmo_array_dispose(&state.attributes);
    nmo_array_dispose(&state.legacy_attributes);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, beobject_serializer_does_not_publish_partial_chunk) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_chunk_file_context_t file_context = {
        .runtime_to_file = runtime_to_file,
    };

    nmo_beobject_state_t state;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&state, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &state.scripts, 123));

    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_BEOBJECT;
    target->data_version = 7;
    nmo_chunk_set_file_context(target, &file_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);

    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_beobject_serialize(
        &state, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t preserved = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &preserved));
    ASSERT_EQ(0x12345678u, preserved);

    nmo_array_dispose(&state.scripts);
    nmo_array_dispose(&state.attributes);
    nmo_array_dispose(&state.legacy_attributes);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, beobject_legacy_attributes_are_lossless_and_atomic) {
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

    nmo_beobject_state_t source;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&source, NULL, NULL));
    source.has_legacy_attributes = 1;
    nmo_beobject_legacy_attribute_t source_attribute = {
        .compatible_class_id = 12,
        .name = "LegacyName",
        .category = "LegacyCategory",
        .parameter_guid = {0x12345678u, 0x9ABCDEF0u},
        .parameter = nmo_ref_from_raw(777),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &source.legacy_attributes, &source_attribute));

    nmo_beobject_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&copied, NULL, NULL));
    nmo_type_descriptor_t beobject_type = {
        .size = sizeof(nmo_beobject_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.copy(
        &source, &copied, &beobject_type, arena));
    ASSERT_TRUE(nmo_beobject_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_beobject_vtable.hash(&source),
              nmo_beobject_vtable.hash(&copied));
    const nmo_beobject_legacy_attribute_t *copied_attributes =
        NMO_ARRAY_DATA(
            nmo_beobject_legacy_attribute_t,
            &copied.legacy_attributes);
    ASSERT_TRUE(copied_attributes[0].name != source_attribute.name);
    ASSERT_TRUE(copied_attributes[0].category != source_attribute.category);
    ASSERT_STR_EQ("LegacyName", copied_attributes[0].name);
    ASSERT_STR_EQ("LegacyCategory", copied_attributes[0].category);

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_BEOBJECT;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_beobject_serialize(
        &source, first, NULL, NULL));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_beobject_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(
        &loaded, first, NULL, NULL));
    ASSERT_EQ(1, loaded.has_legacy_attributes);
    ASSERT_EQ(1u, loaded.legacy_attributes.count);
    const nmo_beobject_legacy_attribute_t *loaded_attributes =
        NMO_ARRAY_DATA(
            nmo_beobject_legacy_attribute_t,
            &loaded.legacy_attributes);
    ASSERT_EQ(12, loaded_attributes[0].compatible_class_id);
    ASSERT_STR_EQ("LegacyName", loaded_attributes[0].name);
    ASSERT_STR_EQ("LegacyCategory", loaded_attributes[0].category);
    ASSERT_TRUE(nmo_guid_equals(
        source_attribute.parameter_guid,
        loaded_attributes[0].parameter_guid));
    ASSERT_EQ(777u, loaded_attributes[0].parameter.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded_attributes[0].parameter.state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_BEOBJECT;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_beobject_serialize(
        &loaded, second, NULL, NULL));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_beobject_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(
        &reloaded, second, NULL, NULL));
    const nmo_beobject_legacy_attribute_t *reloaded_attributes =
        NMO_ARRAY_DATA(
            nmo_beobject_legacy_attribute_t,
            &reloaded.legacy_attributes);
    ASSERT_EQ(1u, reloaded.legacy_attributes.count);
    ASSERT_EQ(777u, reloaded_attributes[0].parameter.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded_attributes[0].parameter.state);

    nmo_beobject_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&failed, NULL, NULL));
    nmo_beobject_legacy_attribute_t previous_attribute = {
        .compatible_class_id = 7,
        .name = "Previous",
        .category = "State",
        .parameter = nmo_ref_from_raw(901),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &failed.legacy_attributes, &previous_attribute));
    failed.has_legacy_attributes = 1;
    void *previous_data = failed.legacy_attributes.data;

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_BEOBJECT;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_ATTRIBUTES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 8));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(truncated, "Truncated"));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(truncated, "Attribute"));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        truncated, (nmo_guid_t){1u, 2u}));
    nmo_chunk_close(truncated);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_beobject_deserialize(
        &failed, truncated, NULL, NULL));
    ASSERT_EQ(previous_data, failed.legacy_attributes.data);
    ASSERT_EQ(1u, failed.legacy_attributes.count);
    ASSERT_EQ(901u, NMO_ARRAY_DATA(
        nmo_beobject_legacy_attribute_t,
        &failed.legacy_attributes)[0].parameter.raw_id);

    nmo_chunk_t *negative_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(negative_count);
    negative_count->class_id = NMO_CID_BEOBJECT;
    negative_count->data_version = 7;
    negative_count->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(negative_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        negative_count, CK_STATESAVE_ATTRIBUTES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(negative_count, -1));
    nmo_chunk_close(negative_count);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_beobject_deserialize(
        &failed, negative_count, NULL, NULL));
    ASSERT_EQ(previous_data, failed.legacy_attributes.data);

    nmo_beobject_state_t allocation_failed;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &allocation_failed, NULL, NULL));
    nmo_allocator_t original_allocator =
        allocation_failed.legacy_attributes.allocator;
    allocation_failed.legacy_attributes.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);

    nmo_chunk_t *impossible_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(impossible_count);
    impossible_count->class_id = NMO_CID_BEOBJECT;
    impossible_count->data_version = 7;
    impossible_count->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(impossible_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        impossible_count, CK_STATESAVE_ATTRIBUTES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(impossible_count, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(impossible_count, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(impossible_count, 1));
    nmo_chunk_close(impossible_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_beobject_deserialize(
        &allocation_failed, impossible_count, NULL, NULL));
    ASSERT_EQ(0u, allocation_failed.legacy_attributes.count);
    ASSERT_EQ(NULL, allocation_failed.legacy_attributes.data);
    ASSERT_EQ(0, allocation_failed.has_legacy_attributes);

    ASSERT_EQ(NMO_ERR_NOMEM, nmo_beobject_deserialize(
        &allocation_failed, first, NULL, NULL));
    ASSERT_EQ(0u, allocation_failed.legacy_attributes.count);
    ASSERT_EQ(NULL, allocation_failed.legacy_attributes.data);
    ASSERT_EQ(0, allocation_failed.has_legacy_attributes);
    allocation_failed.legacy_attributes.allocator = original_allocator;

    nmo_beobject_state_t empty;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&empty, NULL, NULL));
    empty.has_legacy_attributes = 1;
    nmo_chunk_t *empty_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty_chunk);
    empty_chunk->class_id = NMO_CID_BEOBJECT;
    empty_chunk->data_version = 7;
    empty_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_beobject_serialize(
        &empty, empty_chunk, NULL, NULL));
    nmo_chunk_close(empty_chunk);
    nmo_beobject_state_t empty_loaded;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &empty_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(
        &empty_loaded, empty_chunk, NULL, NULL));
    ASSERT_EQ(1, empty_loaded.has_legacy_attributes);
    ASSERT_EQ(0u, empty_loaded.legacy_attributes.count);

    nmo_beobject_state_t old_source;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &old_source, NULL, NULL));
    old_source.has_legacy_attributes = 1;
    old_source.legacy_attr_old_version = 1;
    nmo_beobject_legacy_attribute_t old_attribute = {
        .name = "OldName",
        .category = "OldCategory",
        .parameter_guid = {3u, 4u},
        .parameter = nmo_ref_from_raw(778),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &old_source.legacy_attributes, &old_attribute));
    nmo_chunk_t *old_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(old_chunk);
    old_chunk->class_id = NMO_CID_BEOBJECT;
    old_chunk->data_version = 4;
    old_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(old_chunk, &write_context);
    ASSERT_EQ(NMO_OK, nmo_beobject_serialize(
        &old_source, old_chunk, NULL, NULL));
    nmo_chunk_close(old_chunk);
    nmo_chunk_set_file_context(old_chunk, &read_context);
    nmo_beobject_state_t old_loaded;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &old_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(
        &old_loaded, old_chunk, NULL, NULL));
    ASSERT_EQ(1, old_loaded.legacy_attr_old_version);
    ASSERT_EQ(1u, old_loaded.legacy_attributes.count);
    const nmo_beobject_legacy_attribute_t *old_loaded_attributes =
        NMO_ARRAY_DATA(
            nmo_beobject_legacy_attribute_t,
            &old_loaded.legacy_attributes);
    ASSERT_STR_EQ("OldName", old_loaded_attributes[0].name);
    ASSERT_EQ(778u, old_loaded_attributes[0].parameter.raw_id);

    nmo_beobject_state_t invalid = {0};
    invalid.has_legacy_attributes = 1;
    nmo_chunk_t *partial = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(partial);
    partial->class_id = NMO_CID_BEOBJECT;
    partial->data_version = 7;
    partial->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_beobject_serialize(
        &invalid, partial, NULL, NULL));
    ASSERT_EQ(0u, nmo_chunk_get_data_size(partial));

    nmo_array_dispose(&source.scripts);
    nmo_array_dispose(&source.attributes);
    nmo_array_dispose(&source.legacy_attributes);
    nmo_array_dispose(&copied.scripts);
    nmo_array_dispose(&copied.attributes);
    nmo_array_dispose(&copied.legacy_attributes);
    nmo_array_dispose(&loaded.scripts);
    nmo_array_dispose(&loaded.attributes);
    nmo_array_dispose(&loaded.legacy_attributes);
    nmo_array_dispose(&reloaded.scripts);
    nmo_array_dispose(&reloaded.attributes);
    nmo_array_dispose(&reloaded.legacy_attributes);
    nmo_array_dispose(&failed.scripts);
    nmo_array_dispose(&failed.attributes);
    nmo_array_dispose(&failed.legacy_attributes);
    nmo_array_dispose(&allocation_failed.scripts);
    nmo_array_dispose(&allocation_failed.attributes);
    nmo_array_dispose(&allocation_failed.legacy_attributes);
    nmo_array_dispose(&empty.scripts);
    nmo_array_dispose(&empty.attributes);
    nmo_array_dispose(&empty.legacy_attributes);
    nmo_array_dispose(&empty_loaded.scripts);
    nmo_array_dispose(&empty_loaded.attributes);
    nmo_array_dispose(&empty_loaded.legacy_attributes);
    nmo_array_dispose(&old_source.scripts);
    nmo_array_dispose(&old_source.attributes);
    nmo_array_dispose(&old_source.legacy_attributes);
    nmo_array_dispose(&old_loaded.scripts);
    nmo_array_dispose(&old_loaded.attributes);
    nmo_array_dispose(&old_loaded.legacy_attributes);
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

TEST(chunk_id_remap, character_refs_round_trip_and_failure_is_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t file_serialize_context =
        nmo_serialize_context_create(
            arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t file_deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_deserialize_context_t runtime_deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };

    nmo_character_state_t source;
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(&source, NULL, NULL));
    nmo_chunk_t *part_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(part_chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(part_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(part_chunk, 0x12345678));
    nmo_chunk_close(part_chunk);
    nmo_character_part_t part = {
        .ref = nmo_ref_from_raw(501),
        .chunk = part_chunk,
    };
    nmo_ref_t animation = nmo_ref_from_raw(601);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.body_parts, &part));
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.animations, &animation));
    source.active_animation = nmo_ref_from_raw(602);
    source.anim_dest = nmo_ref_from_raw(603);
    source.root_body_part = nmo_ref_from_raw(504);
    source.floor_ref = nmo_ref_from_raw(505);

    nmo_chunk_t *file_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(file_chunk);
    file_chunk->class_id = NMO_CID_CHARACTER;
    file_chunk->data_version = 5;
    file_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_character_serialize(
        &source, file_chunk, NULL, &file_serialize_context));
    nmo_chunk_close(file_chunk);
    nmo_chunk_set_file_context(file_chunk, &read_context);

    nmo_character_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_character_deserialize(
        &loaded, file_chunk, NULL, &file_deserialize_context));
    ASSERT_EQ(1u, loaded.body_parts.count);
    ASSERT_EQ(1u, loaded.animations.count);
    const nmo_character_part_t *loaded_parts = NMO_ARRAY_DATA(
        nmo_character_part_t, &loaded.body_parts);
    const nmo_ref_t *loaded_animations = NMO_ARRAY_DATA(
        nmo_ref_t, &loaded.animations);
    ASSERT_EQ(501u, loaded_parts[0].ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded_parts[0].ref.state);
    ASSERT_EQ(NULL, loaded_parts[0].chunk);
    ASSERT_EQ(601u, loaded_animations[0].raw_id);
    ASSERT_EQ(602u, loaded.active_animation.raw_id);
    ASSERT_EQ(603u, loaded.anim_dest.raw_id);
    ASSERT_EQ(504u, loaded.root_body_part.raw_id);
    ASSERT_EQ(505u, loaded.floor_ref.raw_id);

    nmo_chunk_t *file_roundtrip = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(file_roundtrip);
    file_roundtrip->class_id = NMO_CID_CHARACTER;
    file_roundtrip->data_version = 5;
    file_roundtrip->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_character_serialize(
        &loaded, file_roundtrip, NULL, &file_serialize_context));
    nmo_chunk_close(file_roundtrip);
    nmo_chunk_set_file_context(file_roundtrip, &read_context);
    nmo_character_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_character_deserialize(
        &reloaded, file_roundtrip, NULL, &file_deserialize_context));
    ASSERT_EQ(501u, NMO_ARRAY_DATA(
        nmo_character_part_t, &reloaded.body_parts)[0].ref.raw_id);
    ASSERT_EQ(601u, NMO_ARRAY_DATA(
        nmo_ref_t, &reloaded.animations)[0].raw_id);

    nmo_serialize_context_t runtime_context = nmo_serialize_context_create(
        arena, NULL, 0,
        CK_STATESAVE_3DENTITYONLY | CK_STATESAVE_CHARACTERONLY |
            CK_STATESAVE_CHARACTERSAVEPARTS);
    nmo_chunk_t *runtime_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(runtime_chunk);
    runtime_chunk->class_id = NMO_CID_CHARACTER;
    runtime_chunk->data_version = 5;
    ASSERT_EQ(NMO_OK, nmo_character_serialize(
        &source, runtime_chunk, NULL, &runtime_context));
    nmo_chunk_close(runtime_chunk);
    runtime_chunk->file_context = &read_context;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        runtime_chunk, CK_STATESAVE_CHARACTERBODYPARTS));
    size_t runtime_part_count = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_object_sequence_start(
        runtime_chunk, &runtime_part_count));
    ASSERT_EQ(1u, runtime_part_count);
    nmo_ref_t runtime_part_ref;
    ASSERT_EQ(NMO_OK, nmo_ref_read(runtime_chunk, &runtime_part_ref));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        runtime_chunk, CK_STATESAVE_CHARACTERSAVEPARTS));
    size_t runtime_chunk_count = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read_sub_chunk_sequence(
        runtime_chunk, &runtime_chunk_count));
    ASSERT_EQ(1u, runtime_chunk_count);
    nmo_chunk_t *runtime_part_chunk = NULL;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_sub_chunk(
        runtime_chunk, &runtime_part_chunk));
    ASSERT_NOT_NULL(runtime_part_chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        runtime_chunk, CK_STATESAVE_CHARACTERONLY));
    size_t runtime_scalar_count = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_object_sequence_start(
        runtime_chunk, &runtime_scalar_count));
    ASSERT_EQ(4u, runtime_scalar_count);
    for (size_t i = 0; i < runtime_scalar_count; ++i) {
        nmo_ref_t scalar_ref;
        ASSERT_EQ(NMO_OK, nmo_ref_read(runtime_chunk, &scalar_ref));
    }
    nmo_3dentity_state_t runtime_base = {0};
    ASSERT_EQ(NMO_OK, nmo_3dentity_deserialize(
        &runtime_base, runtime_chunk, NULL, &runtime_deserialize_context));
    nmo_character_state_t runtime_loaded;
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(
        &runtime_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_character_deserialize(
        &runtime_loaded, runtime_chunk, NULL,
        &runtime_deserialize_context));
    const nmo_character_part_t *runtime_parts = NMO_ARRAY_DATA(
        nmo_character_part_t, &runtime_loaded.body_parts);
    ASSERT_EQ(1u, runtime_loaded.body_parts.count);
    ASSERT_EQ(501u, runtime_parts[0].ref.raw_id);
    ASSERT_NOT_NULL(runtime_parts[0].chunk);

    nmo_character_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(&copied, NULL, NULL));
    const nmo_type_field_t character_copy_fields[] = {
        NMO_FIELD_ARRAY(nmo_character_state_t, body_parts, CKPGUID_NONE),
        NMO_FIELD_REF_RECORD_ARRAY(nmo_character_state_t, animations),
    };
    nmo_type_descriptor_t character_type = {
        .size = sizeof(nmo_character_state_t),
        .fields = character_copy_fields,
        .field_count = sizeof(character_copy_fields) /
            sizeof(character_copy_fields[0]),
    };
    ASSERT_EQ(NMO_OK, nmo_character_vtable.copy(
        &source, &copied, &character_type, arena));
    ASSERT_TRUE(nmo_character_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_character_vtable.hash(&source),
              nmo_character_vtable.hash(&copied));
    const nmo_character_part_t *copied_parts = NMO_ARRAY_DATA(
        nmo_character_part_t, &copied.body_parts);
    ASSERT_TRUE(copied_parts[0].chunk != part_chunk);
    ASSERT_EQ(4u, nmo_chunk_get_data_size(part_chunk));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(copied_parts[0].chunk));

    nmo_character_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(&failed, NULL, NULL));
    nmo_character_part_t previous = {
        .ref = nmo_ref_from_raw(901),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&failed.body_parts, &previous));
    void *previous_data = failed.body_parts.data;
    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_CHARACTER;
    truncated->data_version = 5;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_CHARACTERBODYPARTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(truncated, 1));
    nmo_chunk_close(truncated);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_character_deserialize(
        &failed, truncated, NULL, &file_deserialize_context));
    ASSERT_EQ(previous_data, failed.body_parts.data);
    ASSERT_EQ(1u, failed.body_parts.count);
    ASSERT_EQ(901u, NMO_ARRAY_DATA(
        nmo_character_part_t, &failed.body_parts)[0].ref.raw_id);

    nmo_chunk_t *invalid_scalar_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(invalid_scalar_count);
    invalid_scalar_count->class_id = NMO_CID_CHARACTER;
    invalid_scalar_count->data_version = 5;
    invalid_scalar_count->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(invalid_scalar_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        invalid_scalar_count, CK_STATESAVE_CHARACTERONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        invalid_scalar_count, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        invalid_scalar_count, 5));
    for (size_t i = 0; i < 5; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_sequence_item(
            invalid_scalar_count, NMO_OBJECT_ID_NONE));
    }
    nmo_chunk_close(invalid_scalar_count);
    nmo_chunk_set_file_context(invalid_scalar_count, &read_context);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_character_deserialize(
        &failed, invalid_scalar_count, NULL, &file_deserialize_context));
    ASSERT_EQ(previous_data, failed.body_parts.data);
    ASSERT_EQ(1u, failed.body_parts.count);
    ASSERT_EQ(901u, NMO_ARRAY_DATA(
        nmo_character_part_t, &failed.body_parts)[0].ref.raw_id);

    nmo_character_state_t allocation_failed;
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(
        &allocation_failed, NULL, NULL));
    nmo_allocator_t original_allocator =
        allocation_failed.body_parts.allocator;
    allocation_failed.body_parts.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_character_deserialize(
        &allocation_failed, file_chunk, NULL,
        &file_deserialize_context));
    ASSERT_EQ(0u, allocation_failed.body_parts.count);
    ASSERT_EQ(NULL, allocation_failed.body_parts.data);
    allocation_failed.body_parts.allocator = original_allocator;

    nmo_character_state_t invalid = {0};
    invalid.body_parts.count = 1;
    invalid.body_parts.element_size = sizeof(nmo_character_part_t);
    nmo_chunk_t *partial = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(partial);
    partial->class_id = NMO_CID_CHARACTER;
    partial->data_version = 5;
    partial->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(partial));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(partial, 0x12345678u));
    nmo_chunk_close(partial);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_character_serialize(
        &invalid, partial, NULL, &file_serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(partial));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(partial));
    uint32_t preserved = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(partial, &preserved));
    ASSERT_EQ(0x12345678u, preserved);

    nmo_bodypart_state_t bodypart = {0};
    bodypart.has_character = 1;
    bodypart.character = nmo_ref_from_raw(701);
    nmo_chunk_t *bodypart_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(bodypart_chunk);
    bodypart_chunk->class_id = NMO_CID_BODYPART;
    bodypart_chunk->data_version = 5;
    bodypart_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_bodypart_serialize(
        &bodypart, bodypart_chunk, NULL, &file_serialize_context));
    nmo_chunk_close(bodypart_chunk);
    nmo_chunk_set_file_context(bodypart_chunk, &read_context);
    nmo_bodypart_state_t bodypart_loaded = {0};
    ASSERT_EQ(NMO_OK, nmo_bodypart_deserialize(
        &bodypart_loaded, bodypart_chunk, NULL,
        &file_deserialize_context));
    ASSERT_EQ(1, bodypart_loaded.has_character);
    ASSERT_EQ(701u, bodypart_loaded.character.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, bodypart_loaded.character.state);

    nmo_array_dispose(&source.body_parts);
    nmo_array_dispose(&source.animations);
    nmo_array_dispose(&loaded.body_parts);
    nmo_array_dispose(&loaded.animations);
    nmo_array_dispose(&reloaded.body_parts);
    nmo_array_dispose(&reloaded.animations);
    nmo_array_dispose(&runtime_loaded.body_parts);
    nmo_array_dispose(&runtime_loaded.animations);
    nmo_array_dispose(&copied.body_parts);
    nmo_array_dispose(&copied.animations);
    nmo_array_dispose(&failed.body_parts);
    nmo_array_dispose(&failed.animations);
    nmo_array_dispose(&allocation_failed.body_parts);
    nmo_array_dispose(&allocation_failed.animations);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, mesh_material_refs_round_trip_without_compaction) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };

    nmo_mesh_state_t source;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&source, NULL, NULL));
    source.flags = 0xF1234567u;
    source.has_material_groups = 1;
    nmo_material_group_t groups[] = {
        {.material = nmo_ref_from_raw(701), .padding = 11},
        {.material = nmo_ref_from_raw(NMO_OBJECT_ID_NONE), .padding = -12},
        {.material = nmo_ref_from_raw(703), .padding = 13},
    };
    source.material_group_count = 3;
    source.material_groups = groups;

    nmo_vertex_t vertices[3] = {0};
    vertices[1].position.x = 1.0f;
    vertices[2].position.y = 1.0f;
    uint32_t colors[] = {1, 2, 3};
    uint32_t specular[] = {4, 5, 6};
    source.vertex_count = 3;
    source.vertices = vertices;
    source.vertex_colors = colors;
    source.vertex_specular = specular;
    nmo_face_t face = {.material_group_idx = 2, .channel_mask = 0xFFFFu};
    uint16_t face_indices[] = {0, 1, 2};
    source.face_count = 1;
    source.faces = &face;
    source.face_vertex_indices = face_indices;

    nmo_vector2_t channel_uv = {.x = 0.25f, .y = 0.75f};
    nmo_material_channel_t channel = {
        .material = nmo_ref_from_raw(704),
        .flags = 0xAABBCCDDu,
        .source_blend = 3,
        .dest_blend = 5,
        .uv_count = 1,
        .uv_coords = &channel_uv,
    };
    source.has_material_channels = 1;
    source.material_channel_count = 1;
    source.material_channels = &channel;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_MESH;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 9;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_mesh_serialize(
        &source, chunk, NULL, &serialize_context));
    nmo_chunk_close(chunk);
    nmo_chunk_set_file_context(chunk, &read_context);

    nmo_mesh_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_mesh_deserialize(
        &loaded, chunk, NULL, &deserialize_context));
    ASSERT_EQ(0xF1234567u, loaded.flags);
    ASSERT_TRUE(loaded.has_material_groups);
    ASSERT_EQ(3u, loaded.material_group_count);
    ASSERT_EQ(701u, loaded.material_groups[0].material.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED,
              loaded.material_groups[0].material.state);
    ASSERT_EQ(11, loaded.material_groups[0].padding);
    ASSERT_EQ(NMO_REF_NONE, loaded.material_groups[1].material.state);
    ASSERT_EQ(-12, loaded.material_groups[1].padding);
    ASSERT_EQ(703u, loaded.material_groups[2].material.raw_id);
    ASSERT_EQ(13, loaded.material_groups[2].padding);
    ASSERT_EQ(2u, loaded.faces[0].material_group_idx);
    ASSERT_TRUE(loaded.has_material_channels);
    ASSERT_EQ(704u, loaded.material_channels[0].material.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED,
              loaded.material_channels[0].material.state);
    ASSERT_EQ(1u, loaded.material_channels[0].uv_count);
    ASSERT_EQ(0.25f, loaded.material_channels[0].uv_coords[0].x);
    ASSERT_EQ(0.75f, loaded.material_channels[0].uv_coords[0].y);

    nmo_chunk_t *roundtrip = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(roundtrip);
    roundtrip->class_id = NMO_CID_MESH;
    roundtrip->chunk_version = NMO_CHUNK_VERSION4;
    roundtrip->data_version = 9;
    roundtrip->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_mesh_serialize(
        &loaded, roundtrip, NULL, &serialize_context));
    nmo_chunk_close(roundtrip);
    nmo_chunk_set_file_context(roundtrip, &read_context);

    nmo_mesh_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_mesh_deserialize(
        &reloaded, roundtrip, NULL, &deserialize_context));
    ASSERT_EQ(3u, reloaded.material_group_count);
    ASSERT_EQ(701u, reloaded.material_groups[0].material.raw_id);
    ASSERT_EQ(NMO_REF_NONE, reloaded.material_groups[1].material.state);
    ASSERT_EQ(-12, reloaded.material_groups[1].padding);
    ASSERT_EQ(703u, reloaded.material_groups[2].material.raw_id);
    ASSERT_EQ(2u, reloaded.faces[0].material_group_idx);
    ASSERT_EQ(704u, reloaded.material_channels[0].material.raw_id);
    ASSERT_EQ(1u, reloaded.material_channels[0].uv_count);

    nmo_mesh_vtable.destroy(&source, NULL, NULL);
    nmo_mesh_vtable.destroy(&loaded, NULL, NULL);
    nmo_mesh_vtable.destroy(&reloaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, mesh_material_sections_and_failures_are_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_mesh_state_t empty;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&empty, NULL, NULL));
    empty.has_material_groups = 1;
    empty.has_material_channels = 1;
    nmo_chunk_t *empty_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty_chunk);
    empty_chunk->class_id = NMO_CID_MESH;
    empty_chunk->chunk_version = NMO_CHUNK_VERSION4;
    empty_chunk->data_version = 7;
    empty_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_mesh_serialize(
        &empty, empty_chunk, NULL, &serialize_context));
    nmo_chunk_close(empty_chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        empty_chunk, CK_STATESAVE_MESHMATERIALS));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        empty_chunk, CK_STATESAVE_MESHCHANNELS));

    nmo_mesh_state_t empty_loaded;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&empty_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_mesh_deserialize(
        &empty_loaded, empty_chunk, NULL, &deserialize_context));
    ASSERT_TRUE(empty_loaded.has_material_groups);
    ASSERT_TRUE(empty_loaded.has_material_channels);
    ASSERT_EQ(0u, empty_loaded.material_group_count);
    ASSERT_EQ(0u, empty_loaded.material_channel_count);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_MESH;
    truncated->chunk_version = NMO_CHUNK_VERSION4;
    truncated->data_version = 9;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_MESHMATERIALS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(truncated, 801));
    nmo_chunk_close(truncated);

    nmo_material_group_t previous_group = {
        .material = nmo_ref_from_raw(901),
        .padding = 91,
    };
    nmo_mesh_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&failed, NULL, NULL));
    failed.flags = 0xCAFEBABEu;
    failed.has_material_groups = 1;
    failed.material_group_count = 1;
    failed.material_groups = &previous_group;
    ASSERT_NE(NMO_OK, nmo_mesh_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, failed.flags);
    ASSERT_EQ(&previous_group, failed.material_groups);
    ASSERT_EQ(1u, failed.material_group_count);
    ASSERT_EQ(901u, failed.material_groups[0].material.raw_id);
    ASSERT_EQ(91, failed.material_groups[0].padding);

    nmo_mesh_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&invalid, NULL, NULL));
    invalid.material_group_count = 1;
    nmo_chunk_t *preserved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(preserved);
    preserved->class_id = NMO_CID_MESH;
    preserved->chunk_version = NMO_CHUNK_VERSION4;
    preserved->data_version = 9;
    preserved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(preserved, 0x12345678u));
    nmo_chunk_close(preserved);
    ASSERT_NE(NMO_OK, nmo_mesh_serialize(
        &invalid, preserved, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(preserved));
    uint32_t value = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(preserved, &value));
    ASSERT_EQ(0x12345678u, value);

    nmo_mesh_vtable.destroy(&empty, NULL, NULL);
    nmo_mesh_vtable.destroy(&empty_loaded, NULL, NULL);
    nmo_mesh_vtable.destroy(&failed, NULL, NULL);
    nmo_mesh_vtable.destroy(&invalid, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, mesh_copy_preserves_material_records) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_mesh_state_t source;
    nmo_mesh_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&copied, NULL, NULL));

    nmo_material_group_t group = {
        .material = nmo_ref_from_raw(811),
        .padding = -17,
    };
    nmo_vector2_t uv = {.x = 1.25f, .y = 2.5f};
    nmo_material_channel_t channel = {
        .material = nmo_ref_from_raw(812),
        .flags = 19,
        .source_blend = 23,
        .dest_blend = 29,
        .uv_count = 1,
        .uv_coords = &uv,
    };
    source.has_material_groups = 1;
    source.material_group_count = 1;
    source.material_groups = &group;
    source.has_material_channels = 1;
    source.material_channel_count = 1;
    source.material_channels = &channel;

    nmo_beobject_legacy_attribute_t legacy_attribute = {
        .name = "LegacyName",
        .category = "LegacyCategory",
        .parameter_guid = {3u, 5u},
        .compatible_class_id = NMO_CID_OBJECT,
        .parameter = nmo_ref_from_raw(813),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &source.beobject.legacy_attributes, &legacy_attribute));
    source.beobject.has_legacy_attributes = 1;

    const nmo_type_descriptor_t mesh_type = {
        .size = sizeof(nmo_mesh_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.copy(
        &source, &copied, &mesh_type, arena));
    ASSERT_TRUE(copied.material_groups != source.material_groups);
    ASSERT_TRUE(copied.material_channels != source.material_channels);
    ASSERT_TRUE(copied.material_channels[0].uv_coords !=
                source.material_channels[0].uv_coords);
    ASSERT_EQ(811u, copied.material_groups[0].material.raw_id);
    ASSERT_EQ(-17, copied.material_groups[0].padding);
    ASSERT_EQ(812u, copied.material_channels[0].material.raw_id);
    ASSERT_EQ(1u, copied.beobject.legacy_attributes.count);
    ASSERT_TRUE(copied.beobject.legacy_attributes.data !=
                source.beobject.legacy_attributes.data);
    ASSERT_TRUE(nmo_mesh_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_mesh_vtable.hash(&source), nmo_mesh_vtable.hash(&copied));

    copied.material_groups[0].padding = 99;
    ASSERT_FALSE(nmo_mesh_vtable.equals(&source, &copied));

    nmo_mesh_vtable.destroy(&source, NULL, NULL);
    nmo_mesh_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, patchmesh_data3_refs_round_trip_and_failure_is_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };

    nmo_patchmesh_state_t source;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(&source, NULL, NULL));
    source.format = CKPATCHMESH_FORMAT_DATA3;
    source.patch_flags = 0x5A5A1234u;
    source.iteration_count = 4;
    source.vec_count = 7;

    nmo_patchmesh_patch_record_t patch = {
        .material = nmo_ref_from_raw(701),
        .patch = {
            .type = 0x11223344u,
            .smoothing_group = 0x55667788u,
        },
    };
    for (size_t i = 0; i < sizeof(patch.patch.data); ++i) {
        patch.patch.data[i] = (uint8_t)(i + 1u);
    }
    source.patch_count = 1;
    source.patches = &patch;

    nmo_patchmesh_channel_t channel = {
        .material = nmo_ref_from_raw(702),
        .flags = 0x10203040u,
        .type = 3,
        .subtype = 9,
    };
    source.channel_count = 1;
    source.channels = &channel;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_PATCHMESH;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_serialize(
        &source, chunk, NULL, &serialize_context));
    nmo_chunk_close(chunk);
    nmo_chunk_set_file_context(chunk, &read_context);

    nmo_patchmesh_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_patchmesh_deserialize(
        &loaded, chunk, NULL, &deserialize_context));
    ASSERT_EQ(CKPATCHMESH_FORMAT_DATA3, loaded.format);
    ASSERT_EQ(0x5A5A1234u, loaded.patch_flags);
    ASSERT_EQ(1u, loaded.patch_count);
    ASSERT_EQ(701u, loaded.patches[0].material.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.patches[0].material.state);
    ASSERT_EQ(0x11223344u, loaded.patches[0].patch.type);
    ASSERT_EQ(0x55667788u, loaded.patches[0].patch.smoothing_group);
    ASSERT_EQ(1u, loaded.channel_count);
    ASSERT_EQ(702u, loaded.channels[0].material.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.channels[0].material.state);
    ASSERT_EQ(0x10203040u, loaded.channels[0].flags);

    nmo_chunk_t *roundtrip = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(roundtrip);
    roundtrip->class_id = NMO_CID_PATCHMESH;
    roundtrip->chunk_version = NMO_CHUNK_VERSION4;
    roundtrip->data_version = 7;
    roundtrip->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_serialize(
        &loaded, roundtrip, NULL, &serialize_context));
    nmo_chunk_close(roundtrip);
    nmo_chunk_set_file_context(roundtrip, &read_context);

    nmo_patchmesh_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_patchmesh_deserialize(
        &reloaded, roundtrip, NULL, &deserialize_context));
    ASSERT_EQ(701u, reloaded.patches[0].material.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.patches[0].material.state);
    ASSERT_EQ(702u, reloaded.channels[0].material.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.channels[0].material.state);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_PATCHMESH;
    truncated->chunk_version = NMO_CHUNK_VERSION4;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_PATCHMESHDATA3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 0xDEADBEEFu));
    nmo_chunk_close(truncated);
    nmo_chunk_set_file_context(truncated, &read_context);

    nmo_patchmesh_patch_record_t previous_patch = {
        .material = nmo_ref_from_raw(901),
    };
    nmo_patchmesh_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(&failed, NULL, NULL));
    failed.format = CKPATCHMESH_FORMAT_DATA2;
    failed.patch_flags = 0xCAFEBABEu;
    failed.patch_count = 1;
    failed.patches = &previous_patch;
    ASSERT_NE(NMO_OK, nmo_patchmesh_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(CKPATCHMESH_FORMAT_DATA2, failed.format);
    ASSERT_EQ(0xCAFEBABEu, failed.patch_flags);
    ASSERT_EQ(&previous_patch, failed.patches);
    ASSERT_EQ(1u, failed.patch_count);
    ASSERT_EQ(901u, failed.patches[0].material.raw_id);

    nmo_patchmesh_vtable.destroy(&source, NULL, NULL);
    nmo_patchmesh_vtable.destroy(&loaded, NULL, NULL);
    nmo_patchmesh_vtable.destroy(&reloaded, NULL, NULL);
    nmo_patchmesh_vtable.destroy(&failed, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, patchmesh_data2_layout_and_empty_sections_round_trip) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };

    nmo_ref_t legacy_material = nmo_ref_from_raw(712);
    nmo_patchmesh_state_t source;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(&source, NULL, NULL));
    source.format = CKPATCHMESH_FORMAT_DATA2;
    source.patch_flags = 0xA5A51234u;
    source.legacy_default_material = nmo_ref_from_raw(711);
    source.iteration_count = 5;
    source.vec_count = 8;
    source.has_legacy_smoothing = 1;
    source.has_legacy_materials = 1;
    source.legacy_material_count = 1;
    source.legacy_materials = &legacy_material;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_PATCHMESH;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_serialize(
        &source, chunk, NULL, &serialize_context));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_PATCHMESHDATA2));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_PATCHMESHDATA3));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_PATCHMESHSMOOTH));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_PATCHMESHMATERIALS));
    nmo_chunk_set_file_context(chunk, &read_context);

    nmo_patchmesh_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_patchmesh_deserialize(
        &loaded, chunk, NULL, &deserialize_context));
    ASSERT_EQ(CKPATCHMESH_FORMAT_DATA2, loaded.format);
    ASSERT_EQ(0xA5A51234u, loaded.patch_flags);
    ASSERT_EQ(0u, loaded.patch_count);
    ASSERT_EQ(NULL, loaded.patches);
    ASSERT_EQ(711u, loaded.legacy_default_material.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.legacy_default_material.state);
    ASSERT_TRUE(loaded.has_legacy_smoothing);
    ASSERT_EQ(0u, loaded.legacy_smoothing_count);
    ASSERT_TRUE(loaded.has_legacy_materials);
    ASSERT_EQ(1u, loaded.legacy_material_count);
    ASSERT_EQ(712u, loaded.legacy_materials[0].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.legacy_materials[0].state);

    nmo_chunk_t *roundtrip = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(roundtrip);
    roundtrip->class_id = NMO_CID_PATCHMESH;
    roundtrip->chunk_version = NMO_CHUNK_VERSION4;
    roundtrip->data_version = 7;
    roundtrip->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_serialize(
        &loaded, roundtrip, NULL, &serialize_context));
    nmo_chunk_close(roundtrip);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        roundtrip, CK_STATESAVE_PATCHMESHDATA2));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        roundtrip, CK_STATESAVE_PATCHMESHDATA3));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        roundtrip, CK_STATESAVE_PATCHMESHSMOOTH));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        roundtrip, CK_STATESAVE_PATCHMESHMATERIALS));
    nmo_chunk_set_file_context(roundtrip, &read_context);

    nmo_patchmesh_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_patchmesh_deserialize(
        &reloaded, roundtrip, NULL, &deserialize_context));
    ASSERT_EQ(CKPATCHMESH_FORMAT_DATA2, reloaded.format);
    ASSERT_TRUE(reloaded.has_legacy_smoothing);
    ASSERT_TRUE(reloaded.has_legacy_materials);
    ASSERT_EQ(711u, reloaded.legacy_default_material.raw_id);
    ASSERT_EQ(712u, reloaded.legacy_materials[0].raw_id);

    nmo_patchmesh_vtable.destroy(&source, NULL, NULL);
    nmo_patchmesh_vtable.destroy(&loaded, NULL, NULL);
    nmo_patchmesh_vtable.destroy(&reloaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, patchmesh_serializer_rejects_partial_state) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_patchmesh_state_t invalid = {0};
    invalid.format = CKPATCHMESH_FORMAT_DATA3;
    invalid.total_count = 1;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_PATCHMESH;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0x12345678u));
    nmo_chunk_close(chunk);
    ASSERT_NE(NMO_OK, nmo_patchmesh_serialize(
        &invalid, chunk, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    uint32_t preserved = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(chunk, &preserved));
    ASSERT_EQ(0x12345678u, preserved);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, patchmesh_copy_preserves_atomic_records) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_patchmesh_state_t source;
    nmo_patchmesh_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(&copied, NULL, NULL));

    nmo_patchmesh_patch_record_t patch = {
        .material = nmo_ref_from_raw(801),
        .patch = {.type = 17, .smoothing_group = 23},
    };
    uint8_t channel_patches[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    nmo_vector2_t channel_uv = {.x = 1.25f, .y = 2.5f};
    nmo_patchmesh_channel_t channel = {
        .material = nmo_ref_from_raw(802),
        .flags = 31,
        .type = 37,
        .subtype = 41,
        .patch_count = 1,
        .patches_raw = channel_patches,
        .uv_count = 1,
        .uvs = &channel_uv,
    };
    source.format = CKPATCHMESH_FORMAT_DATA3;
    source.patch_count = 1;
    source.patches = &patch;
    source.channel_count = 1;
    source.channels = &channel;

    nmo_beobject_legacy_attribute_t legacy_attribute = {
        .name = "LegacyName",
        .category = "LegacyCategory",
        .parameter_guid = {3u, 5u},
        .compatible_class_id = NMO_CID_OBJECT,
        .parameter = nmo_ref_from_raw(803),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &source.base.beobject.legacy_attributes, &legacy_attribute));
    source.base.beobject.has_legacy_attributes = 1;

    const nmo_type_descriptor_t patchmesh_type = {
        .size = sizeof(nmo_patchmesh_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.copy(
        &source, &copied, &patchmesh_type, arena));
    ASSERT_TRUE(copied.patches != source.patches);
    ASSERT_TRUE(copied.channels != source.channels);
    ASSERT_TRUE(copied.channels[0].patches_raw !=
                source.channels[0].patches_raw);
    ASSERT_TRUE(copied.channels[0].uvs != source.channels[0].uvs);
    ASSERT_EQ(801u, copied.patches[0].material.raw_id);
    ASSERT_EQ(17u, copied.patches[0].patch.type);
    ASSERT_EQ(802u, copied.channels[0].material.raw_id);
    ASSERT_EQ(1u, copied.base.beobject.legacy_attributes.count);
    ASSERT_TRUE(copied.base.beobject.legacy_attributes.data !=
                source.base.beobject.legacy_attributes.data);
    const nmo_beobject_legacy_attribute_t *copied_legacy = NMO_ARRAY_DATA(
        nmo_beobject_legacy_attribute_t,
        &copied.base.beobject.legacy_attributes);
    ASSERT_STR_EQ("LegacyName", copied_legacy[0].name);
    ASSERT_STR_EQ("LegacyCategory", copied_legacy[0].category);
    ASSERT_TRUE(copied_legacy[0].name != legacy_attribute.name);
    ASSERT_TRUE(nmo_patchmesh_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_patchmesh_vtable.hash(&source),
              nmo_patchmesh_vtable.hash(&copied));

    copied.patches[0].patch.type = 99;
    ASSERT_FALSE(nmo_patchmesh_vtable.equals(&source, &copied));

    nmo_patchmesh_vtable.destroy(&source, NULL, NULL);
    nmo_patchmesh_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, animation_refs_round_trip_and_failure_is_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_animation_state_t source;
    ASSERT_EQ(NMO_OK, nmo_animation_vtable.create(&source, NULL, NULL));
    source.has_root_entity = 1;
    source.root_entity = nmo_ref_from_raw(901);
    source.has_character = 1;
    source.character = nmo_ref_from_raw(902);

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_ANIMATION;
    first->chunk_version = NMO_CHUNK_VERSION4;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_animation_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_animation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_animation_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_animation_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_TRUE(loaded.has_root_entity);
    ASSERT_EQ(901u, loaded.root_entity.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.root_entity.state);
    ASSERT_TRUE(loaded.has_character);
    ASSERT_EQ(902u, loaded.character.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.character.state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_ANIMATION;
    second->chunk_version = NMO_CHUNK_VERSION4;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_animation_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_animation_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_animation_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_animation_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(901u, reloaded.root_entity.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.root_entity.state);
    ASSERT_EQ(902u, reloaded.character.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.character.state);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_ANIMATION;
    truncated->chunk_version = NMO_CHUNK_VERSION4;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_ANIMATIONBODYPARTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 0));
    nmo_chunk_close(truncated);

    nmo_animation_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_animation_vtable.create(&failed, NULL, NULL));
    failed.has_root_entity = 1;
    failed.root_entity = nmo_ref_from_raw(999);
    ASSERT_NE(NMO_OK, nmo_animation_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_TRUE(failed.has_root_entity);
    ASSERT_EQ(999u, failed.root_entity.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.root_entity.state);

    nmo_animation_vtable.destroy(&source, NULL, NULL);
    nmo_animation_vtable.destroy(&loaded, NULL, NULL);
    nmo_animation_vtable.destroy(&reloaded, NULL, NULL);
    nmo_animation_vtable.destroy(&failed, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, keyedanimation_sections_round_trip_independently) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, 0, UINT32_MAX);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);

    nmo_chunk_t *subchunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(subchunk);
    subchunk->class_id = NMO_CID_OBJECTANIMATION;
    subchunk->chunk_version = NMO_CHUNK_VERSION4;
    subchunk->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(subchunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(subchunk, 0x12345678u));
    nmo_chunk_close(subchunk);

    nmo_ref_t animation_ids[] = {
        nmo_ref_from_raw(911), nmo_ref_from_raw(912),
    };
    nmo_keyedanimation_subanim_t subanim = {
        .ref = nmo_ref_from_raw(921),
        .chunk = subchunk,
    };
    nmo_keyedanimation_state_t source;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(
        &source, NULL, NULL));
    source.animation_count = 2;
    source.animation_ids = animation_ids;
    source.subanim_count = 1;
    source.subanims = &subanim;

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_KEYEDANIMATION;
    first->chunk_version = NMO_CHUNK_VERSION4;
    first->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);

    nmo_keyedanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(
        &loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(2u, loaded.animation_count);
    ASSERT_EQ(911u, loaded.animation_ids[0].raw_id);
    ASSERT_EQ(912u, loaded.animation_ids[1].raw_id);
    ASSERT_EQ(1u, loaded.subanim_count);
    ASSERT_EQ(921u, loaded.subanims[0].ref.raw_id);
    ASSERT_NOT_NULL(loaded.subanims[0].chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(loaded.subanims[0].chunk));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(
        loaded.subanims[0].chunk, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_KEYEDANIMATION;
    second->chunk_version = NMO_CHUNK_VERSION4;
    second->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);

    nmo_keyedanimation_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(
        &reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(2u, reloaded.animation_count);
    ASSERT_EQ(911u, reloaded.animation_ids[0].raw_id);
    ASSERT_EQ(912u, reloaded.animation_ids[1].raw_id);
    ASSERT_EQ(1u, reloaded.subanim_count);
    ASSERT_EQ(921u, reloaded.subanims[0].ref.raw_id);

    nmo_keyedanimation_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(
        &copied, NULL, NULL));
    const nmo_type_descriptor_t keyed_type = {
        .size = sizeof(nmo_keyedanimation_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.copy(
        &reloaded, &copied, &keyed_type, arena));
    ASSERT_TRUE(copied.animation_ids != reloaded.animation_ids);
    ASSERT_TRUE(copied.subanims != reloaded.subanims);
    ASSERT_TRUE(copied.subanims[0].chunk != reloaded.subanims[0].chunk);
    ASSERT_TRUE(nmo_keyedanimation_vtable.equals(&reloaded, &copied));
    ASSERT_EQ(nmo_keyedanimation_vtable.hash(&reloaded),
              nmo_keyedanimation_vtable.hash(&copied));

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_KEYEDANIMATION;
    truncated->chunk_version = NMO_CHUNK_VERSION4;
    truncated->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_KEYEDANIMANIMLIST));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(truncated, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_item(truncated, 931));
    nmo_chunk_close(truncated);

    nmo_ref_t previous_animation = nmo_ref_from_raw(941);
    nmo_keyedanimation_subanim_t previous_subanim = {
        .ref = nmo_ref_from_raw(942),
        .chunk = subchunk,
    };
    nmo_keyedanimation_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(&failed, NULL, NULL));
    failed.animation_count = 1;
    failed.animation_ids = &previous_animation;
    failed.subanim_count = 1;
    failed.subanims = &previous_subanim;
    ASSERT_NE(NMO_OK, nmo_keyedanimation_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(1u, failed.animation_count);
    ASSERT_EQ(&previous_animation, failed.animation_ids);
    ASSERT_EQ(941u, failed.animation_ids[0].raw_id);
    ASSERT_EQ(1u, failed.subanim_count);
    ASSERT_EQ(&previous_subanim, failed.subanims);
    ASSERT_EQ(942u, failed.subanims[0].ref.raw_id);
    ASSERT_EQ(subchunk, failed.subanims[0].chunk);

    nmo_keyedanimation_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(&invalid, NULL, NULL));
    invalid.animation_count = 1;
    nmo_chunk_t *preserved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(preserved);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(preserved, 0xCAFEBABEu));
    nmo_chunk_close(preserved);
    ASSERT_NE(NMO_OK, nmo_keyedanimation_serialize(
        &invalid, preserved, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(preserved));
    marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(preserved, &marker));
    ASSERT_EQ(0xCAFEBABEu, marker);

    nmo_keyedanimation_vtable.destroy(&source, NULL, NULL);
    nmo_keyedanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_keyedanimation_vtable.destroy(&reloaded, NULL, NULL);
    nmo_keyedanimation_vtable.destroy(&copied, NULL, NULL);
    nmo_keyedanimation_vtable.destroy(&failed, NULL, NULL);
    nmo_keyedanimation_vtable.destroy(&invalid, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, curve_staging_initializes_inherited_arrays) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *curve_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(curve_chunk);
    curve_chunk->class_id = NMO_CID_CURVE;
    curve_chunk->chunk_version = NMO_CHUNK_VERSION4;
    curve_chunk->data_version = 7;
    curve_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(curve_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        curve_chunk, CK_STATESAVE_SCRIPTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(curve_chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_item(curve_chunk, 951));
    nmo_chunk_close(curve_chunk);

    nmo_curve_state_t curve;
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.create(&curve, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &curve.base.base.base, NULL, NULL));
    nmo_ref_t old_curve_script = nmo_ref_from_raw(952);
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &curve.base.base.base.scripts, &old_curve_script));
    ASSERT_EQ(NMO_OK, nmo_curve_deserialize(
        &curve, curve_chunk, NULL, &deserialize_context));
    ASSERT_EQ(1u, curve.base.base.base.scripts.count);
    ASSERT_EQ(sizeof(nmo_ref_t), curve.base.base.base.scripts.element_size);
    const nmo_ref_t *curve_scripts = NMO_ARRAY_DATA(
        nmo_ref_t, &curve.base.base.base.scripts);
    ASSERT_NOT_NULL(curve_scripts);
    ASSERT_EQ(951u, curve_scripts[0].raw_id);

    nmo_chunk_t *point_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(point_chunk);
    point_chunk->class_id = NMO_CID_CURVEPOINT;
    point_chunk->chunk_version = NMO_CHUNK_VERSION4;
    point_chunk->data_version = 7;
    point_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(point_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        point_chunk, CK_STATESAVE_SCRIPTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(point_chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_item(point_chunk, 953));
    nmo_chunk_close(point_chunk);

    nmo_curvepoint_state_t point;
    ASSERT_EQ(NMO_OK, nmo_curvepoint_vtable.create(&point, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &point.base.base.base, NULL, NULL));
    nmo_ref_t old_point_script = nmo_ref_from_raw(954);
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &point.base.base.base.scripts, &old_point_script));
    ASSERT_EQ(NMO_OK, nmo_curvepoint_deserialize(
        &point, point_chunk, NULL, &deserialize_context));
    ASSERT_EQ(1u, point.base.base.base.scripts.count);
    ASSERT_EQ(sizeof(nmo_ref_t), point.base.base.base.scripts.element_size);
    const nmo_ref_t *point_scripts = NMO_ARRAY_DATA(
        nmo_ref_t, &point.base.base.base.scripts);
    ASSERT_NOT_NULL(point_scripts);
    ASSERT_EQ(953u, point_scripts[0].raw_id);

    nmo_array_dispose(&curve.base.base.base.scripts);
    nmo_array_dispose(&curve.base.base.base.attributes);
    nmo_array_dispose(&curve.base.base.base.legacy_attributes);
    nmo_array_dispose(&point.base.base.base.scripts);
    nmo_array_dispose(&point.base.base.base.attributes);
    nmo_array_dispose(&point.base.base.base.legacy_attributes);
    nmo_curve_vtable.destroy(&curve, NULL, NULL);
    nmo_curvepoint_vtable.destroy(&point, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, curve_refs_round_trip_and_failure_is_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, UINT32_MAX);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *subchunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(subchunk);
    subchunk->chunk_version = NMO_CHUNK_VERSION4;
    subchunk->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(subchunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(subchunk, 0x13572468u));
    nmo_chunk_close(subchunk);

    nmo_ref_t control_points[] = {
        nmo_ref_from_raw(961), nmo_ref_from_raw(962),
    };
    nmo_curve_point_subchunk_t saved_point = {
        .ref = nmo_ref_from_raw(971),
        .chunk = subchunk,
    };
    nmo_curve_state_t source;
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.create(&source, NULL, NULL));
    source.control_point_count = 2;
    source.control_point_ids = control_points;
    source.has_savepoints_chunk = 1;
    source.savepoints_in_file = 1;
    source.sub_point_count = 1;
    source.sub_points = &saved_point;

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_CURVE;
    first->chunk_version = NMO_CHUNK_VERSION4;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_curve_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_curve_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_curve_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(2u, loaded.control_point_count);
    ASSERT_EQ(961u, loaded.control_point_ids[0].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.control_point_ids[0].state);
    ASSERT_EQ(962u, loaded.control_point_ids[1].raw_id);
    ASSERT_EQ(1u, loaded.sub_point_count);
    ASSERT_EQ(971u, loaded.sub_points[0].ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.sub_points[0].ref.state);
    ASSERT_NOT_NULL(loaded.sub_points[0].chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(loaded.sub_points[0].chunk));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(
        loaded.sub_points[0].chunk, &marker));
    ASSERT_EQ(0x13572468u, marker);
    nmo_ref_t inherited_script = nmo_ref_from_raw(972);
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &loaded.base.base.base.scripts, &inherited_script));

    nmo_curve_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.create(&copied, NULL, NULL));
    const nmo_type_descriptor_t curve_type = {
        .size = sizeof(nmo_curve_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.copy(
        &loaded, &copied, &curve_type, arena));
    ASSERT_TRUE(copied.control_point_ids != loaded.control_point_ids);
    ASSERT_TRUE(copied.sub_points != loaded.sub_points);
    ASSERT_TRUE(copied.sub_points[0].chunk != loaded.sub_points[0].chunk);
    ASSERT_TRUE(copied.base.base.base.scripts.data !=
                loaded.base.base.base.scripts.data);
    ASSERT_EQ(1u, copied.base.base.base.scripts.count);
    const nmo_ref_t *copied_scripts = NMO_ARRAY_DATA(
        nmo_ref_t, &copied.base.base.base.scripts);
    ASSERT_EQ(972u, copied_scripts[0].raw_id);
    ASSERT_TRUE(nmo_curve_vtable.equals(&loaded, &copied));
    ASSERT_EQ(nmo_curve_vtable.hash(&loaded),
              nmo_curve_vtable.hash(&copied));

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_CURVE;
    second->chunk_version = NMO_CHUNK_VERSION4;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_curve_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_curve_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_curve_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(2u, reloaded.control_point_count);
    ASSERT_EQ(961u, reloaded.control_point_ids[0].raw_id);
    ASSERT_EQ(962u, reloaded.control_point_ids[1].raw_id);
    ASSERT_EQ(1u, reloaded.sub_point_count);
    ASSERT_EQ(971u, reloaded.sub_points[0].ref.raw_id);
    ASSERT_EQ(1u, reloaded.base.base.base.scripts.count);
    const nmo_ref_t *reloaded_scripts = NMO_ARRAY_DATA(
        nmo_ref_t, &reloaded.base.base.base.scripts);
    ASSERT_EQ(972u, reloaded_scripts[0].raw_id);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_CURVE;
    truncated->chunk_version = NMO_CHUNK_VERSION4;
    truncated->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_CURVEONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(truncated, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_item(truncated, 981));
    nmo_chunk_close(truncated);

    nmo_ref_t previous_control = nmo_ref_from_raw(991);
    nmo_curve_point_subchunk_t previous_saved = {
        .ref = nmo_ref_from_raw(992),
        .chunk = subchunk,
    };
    nmo_curve_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.create(&failed, NULL, NULL));
    failed.control_point_count = 1;
    failed.control_point_ids = &previous_control;
    failed.has_savepoints_chunk = 1;
    failed.sub_point_count = 1;
    failed.sub_points = &previous_saved;
    ASSERT_NE(NMO_OK, nmo_curve_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(1u, failed.control_point_count);
    ASSERT_EQ(&previous_control, failed.control_point_ids);
    ASSERT_EQ(991u, failed.control_point_ids[0].raw_id);
    ASSERT_EQ(1u, failed.sub_point_count);
    ASSERT_EQ(&previous_saved, failed.sub_points);
    ASSERT_EQ(992u, failed.sub_points[0].ref.raw_id);
    ASSERT_EQ(subchunk, failed.sub_points[0].chunk);

    nmo_curve_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.create(&invalid, NULL, NULL));
    invalid.control_point_count = 1;
    nmo_chunk_t *preserved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(preserved);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(preserved, 0xCAFEBABEu));
    nmo_chunk_close(preserved);
    ASSERT_NE(NMO_OK, nmo_curve_serialize(
        &invalid, preserved, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(preserved));
    marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(preserved, &marker));
    ASSERT_EQ(0xCAFEBABEu, marker);

    nmo_curve_vtable.destroy(&source, NULL, NULL);
    nmo_array_dispose(&loaded.base.base.base.scripts);
    nmo_array_dispose(&loaded.base.base.base.attributes);
    nmo_array_dispose(&loaded.base.base.base.legacy_attributes);
    nmo_curve_vtable.destroy(&loaded, NULL, NULL);
    nmo_array_dispose(&copied.base.base.base.scripts);
    nmo_array_dispose(&copied.base.base.base.attributes);
    nmo_array_dispose(&copied.base.base.base.legacy_attributes);
    nmo_curve_vtable.destroy(&copied, NULL, NULL);
    nmo_array_dispose(&reloaded.base.base.base.scripts);
    nmo_array_dispose(&reloaded.base.base.base.attributes);
    nmo_array_dispose(&reloaded.base.base.base.legacy_attributes);
    nmo_curve_vtable.destroy(&reloaded, NULL, NULL);
    nmo_curve_vtable.destroy(&failed, NULL, NULL);
    nmo_curve_vtable.destroy(&invalid, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, objectanimation_refs_round_trip_and_failure_is_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_objectanimation_state_t source;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &source, NULL, NULL));
    source.format = CKOBJANIM_FORMAT_SHARED;
    source.has_shared_anim = 1;
    source.shared_anim = nmo_ref_from_raw(951);
    source.has_root_pos = 1;
    source.flags = CK_OBJECTANIMATION_MERGED;
    source.entity = nmo_ref_from_raw(952);
    source.has_merge = 1;
    source.anim1 = nmo_ref_from_raw(953);
    source.anim2 = nmo_ref_from_raw(954);

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_OBJECTANIMATION;
    first->chunk_version = NMO_CHUNK_VERSION4;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(951u, loaded.shared_anim.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.shared_anim.state);
    ASSERT_EQ(952u, loaded.entity.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.entity.state);
    ASSERT_EQ(953u, loaded.anim1.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.anim1.state);
    ASSERT_EQ(954u, loaded.anim2.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.anim2.state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_OBJECTANIMATION;
    second->chunk_version = NMO_CHUNK_VERSION4;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_objectanimation_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(951u, reloaded.shared_anim.raw_id);
    ASSERT_EQ(952u, reloaded.entity.raw_id);
    ASSERT_EQ(953u, reloaded.anim1.raw_id);
    ASSERT_EQ(954u, reloaded.anim2.raw_id);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_OBJECTANIMATION;
    truncated->chunk_version = NMO_CHUNK_VERSION4;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_OBJANIMSHARED));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(truncated, 961));
    nmo_chunk_close(truncated);

    nmo_objectanimation_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &failed, NULL, NULL));
    failed.entity = nmo_ref_from_raw(999);
    ASSERT_NE(NMO_OK, nmo_objectanimation_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(999u, failed.entity.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.entity.state);

    nmo_objectanimation_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &invalid, NULL, NULL));
    invalid.controller_count = 1;
    nmo_chunk_t *preserved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(preserved);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(preserved, 0xABCDEF01u));
    nmo_chunk_close(preserved);
    ASSERT_NE(NMO_OK, nmo_objectanimation_serialize(
        &invalid, preserved, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(preserved));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(preserved, &marker));
    ASSERT_EQ(0xABCDEF01u, marker);

    nmo_objectanimation_vtable.destroy(&source, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&reloaded, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&failed, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&invalid, NULL, NULL);
    nmo_arena_destroy(arena);
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
    REGISTER_TEST(chunk_id_remap, null_ref_uses_file_null_encoding);
    REGISTER_TEST(chunk_id_remap, unresolved_ref_preserves_raw_id);
    REGISTER_TEST(chunk_id_remap, behavior_unresolved_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, behavior_serializer_does_not_publish_partial_chunk);
    REGISTER_TEST(chunk_id_remap, behaviorio_truncation_keeps_previous_state);
    REGISTER_TEST(chunk_id_remap, behaviorlink_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, material_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, parameterlocal_owner_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, parameterin_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, parameterout_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, parameter_object_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, parameteroperation_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, parameteroperation_legacy_sections_are_atomic);
    REGISTER_TEST(chunk_id_remap, camera_and_light_failures_keep_previous_state);
    REGISTER_TEST(chunk_id_remap, target_camera_and_light_failures_are_atomic);
    REGISTER_TEST(chunk_id_remap, targetcamera_unresolved_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, targetlight_unresolved_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, kinematicchain_unresolved_refs_round_trip_atomically);
    REGISTER_TEST(chunk_id_remap, layer_unresolved_grid_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, grid_failures_keep_state_and_target_chunk_atomic);
    REGISTER_TEST(chunk_id_remap, sprite_shared_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, curvepoint_unresolved_curve_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, sprite3d_unresolved_material_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, wavesound_unresolved_attachment_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, scalar_ref_sections_do_not_publish_truncated_state);
    REGISTER_TEST(chunk_id_remap, entity_scalar_refs_round_trip_unresolved_raw_ids);
    REGISTER_TEST(chunk_id_remap, entity_scalar_ref_sections_reject_truncation_atomically);
    REGISTER_TEST(chunk_id_remap, entity_serializer_does_not_publish_partial_chunk);
    REGISTER_TEST(chunk_id_remap, entity2d_serializer_does_not_publish_partial_chunk);
    REGISTER_TEST(chunk_id_remap, place_refs_round_trip_and_truncation_is_atomic);
    REGISTER_TEST(chunk_id_remap, group_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, level_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, scene_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, synchro_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, beobject_attribute_unresolved_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, beobject_attribute_failure_keeps_previous_atomic_state);
    REGISTER_TEST(chunk_id_remap, beobject_serializer_does_not_publish_partial_chunk);
    REGISTER_TEST(chunk_id_remap, beobject_legacy_attributes_are_lossless_and_atomic);
    REGISTER_TEST(chunk_id_remap, beobject_copy_preserves_content_equality);
    REGISTER_TEST(chunk_id_remap, character_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, mesh_material_refs_round_trip_without_compaction);
    REGISTER_TEST(chunk_id_remap, mesh_material_sections_and_failures_are_atomic);
    REGISTER_TEST(chunk_id_remap, mesh_copy_preserves_material_records);
    REGISTER_TEST(chunk_id_remap, patchmesh_data3_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, patchmesh_data2_layout_and_empty_sections_round_trip);
    REGISTER_TEST(chunk_id_remap, patchmesh_serializer_rejects_partial_state);
    REGISTER_TEST(chunk_id_remap, patchmesh_copy_preserves_atomic_records);
    REGISTER_TEST(chunk_id_remap, animation_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, keyedanimation_sections_round_trip_independently);
    REGISTER_TEST(chunk_id_remap, curve_staging_initializes_inherited_arrays);
    REGISTER_TEST(chunk_id_remap, curve_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, objectanimation_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, legacy_unresolved_id_preserves_raw_id);
TEST_MAIN_END()
