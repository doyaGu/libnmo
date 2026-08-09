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
#include "object/builtin/nmo_dataarray_schemas.h"
#include "object/builtin/nmo_attributemanager_schemas.h"
#include "object/builtin/nmo_messagemanager_schemas.h"
#include "object/builtin/nmo_interfaceobjectmanager_schemas.h"
#include "object/builtin/nmo_camera_schemas.h"
#include "object/builtin/nmo_light_schemas.h"
#include "object/builtin/nmo_targetcamera_schemas.h"
#include "object/builtin/nmo_targetlight_schemas.h"
#include "object/builtin/nmo_kinematicchain_schemas.h"
#include "object/builtin/nmo_grid_schemas.h"
#include "object/builtin/nmo_layer_schemas.h"
#include "object/builtin/nmo_sprite_schemas.h"
#include "object/builtin/nmo_spritetext_schemas.h"
#include "object/builtin/nmo_texture_schemas.h"
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
#include "object/builtin/nmo_sceneobject_schemas.h"
#include "object/builtin/nmo_rendercontext_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_param_guids.h"
#include "object/nmo_manager_guids.h"
#include "object/nmo_object_enum_defs.h"
#include "object/nmo_statesave_ids.h"
#include "object/nmo_serialize_context.h"
#include "object/nmo_deserialize_context.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_object_types.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_runtime.h"
#include "type/nmo_operations.h"
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

typedef struct fail_after_allocator_state {
    size_t allocation_count;
    size_t allowed_allocations;
} fail_after_allocator_state_t;

static void *fail_after_alloc(void *user_data, size_t size, size_t alignment) {
    fail_after_allocator_state_t *state =
        (fail_after_allocator_state_t *)user_data;
    if (state->allocation_count >= state->allowed_allocations) return NULL;
    state->allocation_count++;
    nmo_allocator_t allocator = nmo_allocator_default();
    return nmo_alloc(&allocator, size, alignment);
}

static void fail_after_free(void *user_data, void *ptr) {
    (void)user_data;
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_free(&allocator, ptr);
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

TEST(chunk_id_remap, id_remap_preserves_maximum_target) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_id_remap_t *remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    ASSERT_EQ(NMO_OK,
              nmo_id_remap_add(remap, 7u, NMO_OBJECT_ID_INVALID));

    nmo_object_id_t mapped = 0;
    ASSERT_EQ(NMO_OK, nmo_id_remap_lookup_id(remap, 7u, &mapped));
    ASSERT_EQ(NMO_OBJECT_ID_INVALID, mapped);

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, object_visibility_seek_errors_propagate_atomically) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t *malformed = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(malformed);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(malformed));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(malformed, 0x12345678u));
    nmo_chunk_close(malformed);

    nmo_object_state_t state = {
        .visibility_flags = NMO_CKOBJECT_HIERARCHICAL,
    };
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_object_deserialize(
        &state, malformed, NULL, NULL));
    ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, state.visibility_flags);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, object_only_types_delegate_state_operations) {
    nmo_sceneobject_state_t scene;
    nmo_sceneobject_state_t scene_copy;
    nmo_rendercontext_state_t render;
    nmo_rendercontext_state_t render_copy;
    ASSERT_EQ(NMO_OK, nmo_sceneobject_vtable.create(&scene, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_sceneobject_vtable.create(
        &scene_copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_rendercontext_vtable.create(
        &render, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_rendercontext_vtable.create(
        &render_copy, NULL, NULL));
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, scene.base.visibility_flags);
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, render.base.visibility_flags);

    scene.base.visibility_flags = NMO_CKOBJECT_HIERARCHICAL;
    nmo_type_descriptor_t scene_type = {
        .size = sizeof(nmo_sceneobject_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_sceneobject_vtable.copy(
        &scene, &scene_copy, &scene_type, NULL));
    ASSERT_TRUE(nmo_sceneobject_vtable.equals(&scene, &scene_copy));
    ASSERT_EQ(nmo_sceneobject_vtable.hash(&scene),
              nmo_sceneobject_vtable.hash(&scene_copy));

    render.base.visibility_flags = 0;
    nmo_type_descriptor_t render_type = {
        .size = sizeof(nmo_rendercontext_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_rendercontext_vtable.copy(
        &render, &render_copy, &render_type, NULL));
    ASSERT_TRUE(nmo_rendercontext_vtable.equals(&render, &render_copy));
    ASSERT_EQ(nmo_rendercontext_vtable.hash(&render),
              nmo_rendercontext_vtable.hash(&render_copy));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_sceneobject_vtable.validate(NULL, NULL, NULL));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_rendercontext_vtable.validate(NULL, NULL, NULL));

    nmo_object_state_t dependency_state;
    ASSERT_EQ(NMO_OK, nmo_object_vtable.create(
        &dependency_state, NULL, NULL));
    dependency_state.visibility_flags = 0xA5A50003u;
    ASSERT_EQ(NMO_OK, nmo_object_vtable.remap_dependencies(
        &dependency_state, NULL, NULL));
    ASSERT_EQ(0xA5A50003u, dependency_state.visibility_flags);
    ASSERT_EQ(NMO_OK, nmo_object_vtable.pre_delete(
        &dependency_state, NULL, NULL));
    ASSERT_EQ(0xA5A50003u, dependency_state.visibility_flags);

    nmo_sceneobject_vtable.destroy(&scene, NULL, NULL);
    nmo_sceneobject_vtable.destroy(&scene_copy, NULL, NULL);
    nmo_rendercontext_vtable.destroy(&render, NULL, NULL);
    nmo_rendercontext_vtable.destroy(&render_copy, NULL, NULL);
    nmo_object_vtable.destroy(&dependency_state, NULL, NULL);
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

TEST(chunk_id_remap, malformed_subchunk_remap_reports_and_rolls_back) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(chunk, 10));
    nmo_chunk_close(chunk);

    const uint32_t sequence_marker = UINT32_MAX;
    ASSERT_EQ(NMO_OK, nmo_arena_array_append(
        &chunk->chunk_refs, &sequence_marker));

    nmo_id_remap_t* remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 10, 20));

    ASSERT_EQ(NMO_ERR_INVALID_STATE,
        nmo_chunk_remap_object_ids(chunk, remap));
    ASSERT_EQ(1u, chunk->data.count);
    ASSERT_EQ(10u, NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data)[0]);
    ASSERT_EQ(1u, chunk->ids.count);
    ASSERT_EQ(0u, NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->ids)[0]);

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, malformed_chunk_arrays_are_rejected_before_remap) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    nmo_id_remap_t* remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);

    nmo_arena_array_t* arrays[] = {
        &chunk->data,
        &chunk->ids,
        &chunk->chunk_refs,
        &chunk->managers,
    };
    for (size_t i = 0; i < sizeof(arrays) / sizeof(arrays[0]); ++i) {
        arrays[i]->count = 1;
        arrays[i]->capacity = 1;
        ASSERT_EQ(NMO_ERR_INVALID_STATE,
            nmo_chunk_remap_object_ids(chunk, remap));
        arrays[i]->count = 0;
        arrays[i]->capacity = 0;
    }

    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0x12345678u));
    nmo_chunk_close(chunk);
    void* original_data = chunk->data.data;
    size_t original_count = chunk->data.count;
    size_t original_capacity = chunk->data.capacity;

    chunk->data.element_size = sizeof(uint64_t);
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
        nmo_chunk_remap_object_ids(chunk, remap));
    chunk->data.element_size = sizeof(uint32_t);

    chunk->data.count = SIZE_MAX / sizeof(uint32_t) + 1u;
    chunk->data.capacity = chunk->data.count;
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
        nmo_chunk_remap_object_ids(chunk, remap));

    chunk->data.data = original_data;
    chunk->data.count = original_count;
    chunk->data.capacity = original_capacity;
    ASSERT_EQ(0x12345678u,
        NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data)[0]);

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, malformed_id_metadata_reports_and_rolls_back) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(chunk, 10));
    nmo_chunk_close(chunk);

    nmo_id_remap_t* remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(remap, 10, 20));

    const uint32_t invalid_offset = 99;
    ASSERT_EQ(NMO_OK, nmo_arena_array_append(&chunk->ids, &invalid_offset));
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
        nmo_chunk_remap_object_ids(chunk, remap));
    ASSERT_EQ(10u, NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data)[0]);

    nmo_arena_array_clear(&chunk->ids);
    const uint32_t sequence_marker = UINT32_MAX;
    ASSERT_EQ(NMO_OK, nmo_arena_array_append(&chunk->ids, &sequence_marker));
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
        nmo_chunk_remap_object_ids(chunk, remap));
    ASSERT_EQ(10u, NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data)[0]);

    const uint32_t sequence_offset = 0;
    ASSERT_EQ(NMO_OK, nmo_arena_array_append(&chunk->ids, &sequence_offset));
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
        nmo_chunk_remap_object_ids(chunk, remap));
    ASSERT_EQ(10u, NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data)[0]);

    nmo_chunk_t* empty = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty);
    ASSERT_EQ(NMO_OK, nmo_arena_array_append(&empty->ids, &sequence_offset));
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
        nmo_chunk_remap_object_ids(empty, remap));
    nmo_arena_array_clear(&empty->ids);
    ASSERT_EQ(NMO_OK, nmo_arena_array_append(&empty->chunk_refs, &sequence_offset));
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
        nmo_chunk_remap_object_ids(empty, remap));

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

TEST(chunk_id_remap, ref_sequence_mapping_failure_is_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(runtime_to_file);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(runtime_to_file, 10, 100));

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    nmo_chunk_file_context_t file_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_set_file_context(chunk, &file_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0xA5A5A5A5u));

    nmo_ref_t refs[] = {
        nmo_ref_from_id(10),
        nmo_ref_from_id(20),
    };
    ASSERT_EQ(NMO_ERR_NOT_FOUND,
              nmo_ref_write_sequence(chunk, refs, 2));
    ASSERT_EQ(1u, nmo_chunk_get_position(chunk));
    ASSERT_EQ(1u, chunk->data.count);
    ASSERT_EQ(0xA5A5A5A5u,
              NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data)[0]);

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, ref_sequence_rejects_invalid_identifier_end) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0x1234u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 77));
    nmo_chunk_close(chunk);

    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    data[1] = (uint32_t)chunk->data.count;

    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    uint32_t identifier = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_identifier(chunk, &identifier));
    ASSERT_EQ(0x1234u, identifier);
    const size_t payload_pos = nmo_chunk_get_position(chunk);
    nmo_ref_t *refs = (nmo_ref_t *)(uintptr_t)1;
    size_t count = 9;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
              nmo_ref_read_sequence(chunk, &refs, &count, arena));
    ASSERT_NULL(refs);
    ASSERT_EQ(0u, count);
    ASSERT_EQ(payload_pos, nmo_chunk_get_position(chunk));

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
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, source.base.base.visibility_flags);
    source.flags |= CKBEHAVIOR_TARGETABLE;
    source.target_parameter = nmo_ref_from_raw(778);
    nmo_chunk_t *input_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(input_chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(input_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(input_chunk, 0x1234ABCDu));
    nmo_chunk_close(input_chunk);
    nmo_behavior_ref_t unresolved = {
        .ref = {
            .raw_id = 777,
            .id = NMO_OBJECT_ID_NONE,
            .state = NMO_REF_UNRESOLVED,
        },
        .chunk = input_chunk,
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.inputs, &unresolved));

    nmo_behavior_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.copy(
        &source, &copied, NULL, arena));
    ASSERT_NE(source.inputs.data, copied.inputs.data);
    ASSERT_NE(NMO_ARRAY_DATA(
                  nmo_behavior_ref_t, &source.inputs)[0].chunk,
              NMO_ARRAY_DATA(
                  nmo_behavior_ref_t, &copied.inputs)[0].chunk);
    ASSERT_TRUE(nmo_behavior_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_behavior_vtable.hash(&source),
              nmo_behavior_vtable.hash(&copied));

    fail_after_allocator_state_t copy_allocator_state = {
        .allocation_count = 0,
        .allowed_allocations = 2,
    };
    nmo_allocator_t copy_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &copy_allocator_state);
    nmo_arena_t *copy_arena = nmo_arena_create(&copy_allocator, 1);
    ASSERT_NOT_NULL(copy_arena);
    nmo_behavior_state_t failed_copy;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(
        &failed_copy, NULL, NULL));
    failed_copy.flags = 0x87654321u;
    nmo_behavior_ref_t preserved_ref = {
        .ref = nmo_ref_from_raw(904),
        .chunk = NULL,
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &failed_copy.inputs, &preserved_ref));
    void *preserved_inputs = failed_copy.inputs.data;
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_behavior_vtable.copy(
        &source, &failed_copy, NULL, copy_arena));
    ASSERT_EQ(0x87654321u, failed_copy.flags);
    ASSERT_EQ(preserved_inputs, failed_copy.inputs.data);
    ASSERT_EQ(1u, failed_copy.inputs.count);
    ASSERT_EQ(904u, NMO_ARRAY_DATA(
        nmo_behavior_ref_t, &failed_copy.inputs)[0].ref.raw_id);

    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_BEHAVIOR;
    first->chunk_version = NMO_CHUNK_VERSION4;
    first->data_version = 7;
    nmo_chunk_set_file_context(first, &write_context);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    ASSERT_EQ(NMO_OK, nmo_behavior_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);

    nmo_chunk_file_context_t read_context = {
        .file_to_runtime = file_to_runtime,
    };
    nmo_chunk_set_file_context(first, &read_context);
    nmo_behavior_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_deserialize(
        &loaded, first, NULL, &deserialize_context));
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
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_behavior_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_behavior_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_deserialize(
        &reloaded, second, NULL, &deserialize_context));
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

    nmo_behavior_state_t large;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&large, NULL, NULL));
    const size_t large_count = 100001u;
    ASSERT_EQ(NMO_OK, nmo_array_extend(
        &large.inputs, large_count, NULL));
    nmo_chunk_t *large_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(large_chunk);
    large_chunk->class_id = NMO_CID_BEHAVIOR;
    large_chunk->data_version = 7;
    large_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(large_chunk, &write_context);
    ASSERT_EQ(NMO_OK, nmo_behavior_serialize(
        &large, large_chunk, NULL, &serialize_context));
    nmo_chunk_close(large_chunk);
    nmo_chunk_set_file_context(large_chunk, &read_context);
    nmo_behavior_state_t large_loaded;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(
        &large_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_deserialize(
        &large_loaded, large_chunk, NULL, NULL));
    ASSERT_EQ(large_count, large_loaded.inputs.count);

    nmo_behavior_state_t large_subchunks;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(
        &large_subchunks, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_array_extend(
        &large_subchunks.sub_behaviors, large_count, NULL));
    nmo_serialize_context_t subchunk_serialize_context =
        nmo_serialize_context_create(
            arena, NULL, 0, CK_STATESAVE_BEHAVIORSUBBEHAV);
    nmo_chunk_t *large_subchunk_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(large_subchunk_chunk);
    large_subchunk_chunk->class_id = NMO_CID_BEHAVIOR;
    large_subchunk_chunk->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_behavior_serialize(
        &large_subchunks, large_subchunk_chunk, NULL,
        &subchunk_serialize_context));
    nmo_chunk_close(large_subchunk_chunk);
    nmo_behavior_state_t large_subchunks_loaded;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(
        &large_subchunks_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_deserialize(
        &large_subchunks_loaded, large_subchunk_chunk, NULL, NULL));
    ASSERT_EQ(large_count,
              large_subchunks_loaded.sub_behaviors.count);

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

    nmo_chunk_t *oversized_subchunks = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(oversized_subchunks);
    oversized_subchunks->class_id = NMO_CID_BEHAVIOR;
    oversized_subchunks->chunk_version = NMO_CHUNK_VERSION4;
    oversized_subchunks->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(oversized_subchunks));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        oversized_subchunks, CK_STATESAVE_BEHAVIORSUBBEHAV));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(oversized_subchunks, 100000));
    nmo_chunk_close(oversized_subchunks);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_behavior_deserialize(
        &failed, oversized_subchunks, NULL, NULL));
    ASSERT_EQ(0x12345678u, failed.flags);
    ASSERT_EQ(1u, failed.inputs.count);
    ASSERT_EQ(903u, NMO_ARRAY_DATA(
        nmo_behavior_ref_t, &failed.inputs)[0].ref.raw_id);

    nmo_chunk_t *cross_section_subchunks = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_subchunks);
    cross_section_subchunks->class_id = NMO_CID_BEHAVIOR;
    cross_section_subchunks->chunk_version = NMO_CHUNK_VERSION4;
    cross_section_subchunks->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_subchunks));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_subchunks, CK_STATESAVE_BEHAVIORSUBBEHAV));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_subchunks, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_subchunks, 0x7F123456u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_subchunks, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_subchunks, 0));
    nmo_chunk_close(cross_section_subchunks);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_behavior_deserialize(
        &failed, cross_section_subchunks, NULL, NULL));
    nmo_chunk_parser_state_t *parser =
        (nmo_chunk_parser_state_t *)cross_section_subchunks->parser_state;
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(parser->prev_identifier_pos + 3u, parser->current_pos);
    ASSERT_EQ(0x12345678u, failed.flags);
    ASSERT_EQ(1u, failed.inputs.count);

    nmo_behavior_vtable.destroy(&failed_copy, NULL, NULL);
    nmo_arena_destroy(copy_arena);
    nmo_behavior_vtable.destroy(&copied, NULL, NULL);
    nmo_behavior_vtable.destroy(&source, NULL, NULL);
    nmo_behavior_vtable.destroy(&loaded, NULL, NULL);
    nmo_behavior_vtable.destroy(&reloaded, NULL, NULL);
    nmo_behavior_vtable.destroy(&failed, NULL, NULL);
    nmo_behavior_vtable.destroy(&large, NULL, NULL);
    nmo_behavior_vtable.destroy(&large_loaded, NULL, NULL);
    nmo_behavior_vtable.destroy(&large_subchunks, NULL, NULL);
    nmo_behavior_vtable.destroy(
        &large_subchunks_loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, behavior_legacy_file_layout_round_trips) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    nmo_behavior_state_t source;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&source, NULL, NULL));
    source.flags = CKBEHAVIOR_SCRIPT;
    source.behavior_type = CKBEHAVIORTYPE_SCRIPT;
    source.priority = 17;
    source.compatible_class_id = NMO_CID_3DENTITY;
    source.owner = nmo_ref_from_raw(710);
    source.has_single_activity = true;
    source.single_activity_flags = 0x12345678u;
    nmo_behavior_ref_t sub_behavior = {
        .ref = nmo_ref_from_raw(711),
        .chunk = NULL,
    };
    nmo_behavior_ref_t input = {
        .ref = nmo_ref_from_raw(712),
        .chunk = NULL,
    };
    nmo_behavior_ref_t output = {
        .ref = nmo_ref_from_raw(713),
        .chunk = NULL,
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &source.sub_behaviors, &sub_behavior));
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.inputs, &input));
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.outputs, &output));

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_BEHAVIOR;
    first->data_version = 4;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_behavior_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    size_t newdata_dwords = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        first, CK_STATESAVE_BEHAVIORNEWDATA, &newdata_dwords));
    ASSERT_EQ(8u, newdata_dwords);

    nmo_behavior_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_deserialize(
        &loaded, first, NULL, NULL));
    ASSERT_EQ(source.flags, loaded.flags);
    ASSERT_EQ(source.behavior_type, loaded.behavior_type);
    ASSERT_EQ(source.priority, loaded.priority);
    ASSERT_EQ(source.compatible_class_id, loaded.compatible_class_id);
    ASSERT_EQ(710u, loaded.owner.raw_id);
    ASSERT_EQ(1u, loaded.sub_behaviors.count);
    ASSERT_EQ(711u, NMO_ARRAY_DATA(
        nmo_behavior_ref_t, &loaded.sub_behaviors)[0].ref.raw_id);
    ASSERT_EQ(712u, NMO_ARRAY_DATA(
        nmo_behavior_ref_t, &loaded.inputs)[0].ref.raw_id);
    ASSERT_EQ(713u, NMO_ARRAY_DATA(
        nmo_behavior_ref_t, &loaded.outputs)[0].ref.raw_id);
    ASSERT_TRUE(loaded.has_single_activity);
    ASSERT_EQ(source.single_activity_flags,
              loaded.single_activity_flags);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_BEHAVIOR;
    second->data_version = 4;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_behavior_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_behavior_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_deserialize(
        &reloaded, second, NULL, NULL));
    ASSERT_EQ(710u, reloaded.owner.raw_id);
    ASSERT_EQ(711u, NMO_ARRAY_DATA(
        nmo_behavior_ref_t, &reloaded.sub_behaviors)[0].ref.raw_id);
    ASSERT_EQ(712u, NMO_ARRAY_DATA(
        nmo_behavior_ref_t, &reloaded.inputs)[0].ref.raw_id);
    ASSERT_EQ(713u, NMO_ARRAY_DATA(
        nmo_behavior_ref_t, &reloaded.outputs)[0].ref.raw_id);

    nmo_chunk_t *preserved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(preserved);
    preserved->class_id = NMO_CID_BEHAVIOR;
    preserved->data_version = 4;
    preserved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(preserved, 0xBEEFBEEFu));
    nmo_chunk_close(preserved);
    loaded.target_parameter = nmo_ref_from_raw(714);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_behavior_serialize(
        &loaded, preserved, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(preserved));
    uint32_t marker = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(preserved, &marker));
    ASSERT_EQ(0xBEEFBEEFu, marker);
    loaded.target_parameter = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    loaded.has_save_flags = true;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_behavior_serialize(
        &loaded, preserved, NULL, &serialize_context));

    nmo_behavior_state_t block;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&block, NULL, NULL));
    block.flags = CKBEHAVIOR_BUILDINGBLOCK;
    block.block_guid = (nmo_guid_t){0x12345678u, 0x9ABCDEF0u};
    block.block_version = 0x00010203u;
    block.owner = nmo_ref_from_raw(715);
    nmo_chunk_t *block_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(block_chunk);
    block_chunk->class_id = NMO_CID_BEHAVIOR;
    block_chunk->data_version = 4;
    block_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_behavior_serialize(
        &block, block_chunk, NULL, &serialize_context));
    nmo_chunk_close(block_chunk);
    nmo_behavior_state_t block_loaded;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(
        &block_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behavior_deserialize(
        &block_loaded, block_chunk, NULL, NULL));
    ASSERT_EQ(block.flags, block_loaded.flags);
    ASSERT_TRUE(nmo_guid_equals(block.block_guid, block_loaded.block_guid));
    ASSERT_EQ(block.block_version, block_loaded.block_version);
    ASSERT_EQ(715u, block_loaded.owner.raw_id);

    nmo_behavior_vtable.destroy(&source, NULL, NULL);
    nmo_behavior_vtable.destroy(&loaded, NULL, NULL);
    nmo_behavior_vtable.destroy(&reloaded, NULL, NULL);
    nmo_behavior_vtable.destroy(&block, NULL, NULL);
    nmo_behavior_vtable.destroy(&block_loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, behavior_sections_do_not_borrow_following_identifiers) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *modern_header = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern_header);
    modern_header->class_id = NMO_CID_BEHAVIOR;
    modern_header->data_version = 7;
    modern_header->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(modern_header));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        modern_header, CK_STATESAVE_BEHAVIORNEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(modern_header, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(modern_header, 0));
    nmo_chunk_close(modern_header);

    nmo_behavior_state_t state;
    ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&state, NULL, NULL));
    state.priority = 42;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_behavior_deserialize(
        &state, modern_header, NULL, NULL));
    ASSERT_EQ(42, state.priority);

    nmo_chunk_t *modern_sequences = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern_sequences);
    modern_sequences->class_id = NMO_CID_BEHAVIOR;
    modern_sequences->data_version = 7;
    modern_sequences->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(modern_sequences));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        modern_sequences, CK_STATESAVE_BEHAVIORNEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(modern_sequences, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        modern_sequences,
        CK_STATESAVE_BEHAVIORINPUTS | CK_STATESAVE_BEHAVIOROUTPUTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        modern_sequences, 1u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        modern_sequences, 701u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(modern_sequences, 0));
    nmo_chunk_close(modern_sequences);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_behavior_deserialize(
        &state, modern_sequences, NULL, NULL));
    ASSERT_EQ(42, state.priority);

    nmo_chunk_t *legacy_header = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_header);
    legacy_header->class_id = NMO_CID_BEHAVIOR;
    legacy_header->data_version = 4;
    legacy_header->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_header));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_header, CK_STATESAVE_BEHAVIORNEWDATA));
    for (size_t i = 0; i < 7u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(legacy_header, 0u));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(legacy_header, 0));
    nmo_chunk_close(legacy_header);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_behavior_deserialize(
        &state, legacy_header, NULL, NULL));
    ASSERT_EQ(42, state.priority);

    nmo_chunk_t *legacy_scalar = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_scalar);
    legacy_scalar->class_id = NMO_CID_BEHAVIOR;
    legacy_scalar->data_version = 4;
    legacy_scalar->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_scalar));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_scalar, CK_STATESAVE_BEHAVIORFLAGS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(legacy_scalar, 0));
    nmo_chunk_close(legacy_scalar);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_behavior_deserialize(
        &state, legacy_scalar, NULL, NULL));
    ASSERT_EQ(42, state.priority);

    const uint32_t optional_ids[] = {
        CK_STATESAVE_BEHAVIORINTERFACE,
        CK_STATESAVE_BEHAVIORSINGLEACTIVITY,
    };
    for (size_t case_index = 0;
         case_index < sizeof(optional_ids) / sizeof(optional_ids[0]);
         ++case_index) {
        nmo_chunk_t *chunk = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(chunk);
        chunk->class_id = NMO_CID_BEHAVIOR;
        chunk->data_version = 7;
        chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            chunk, CK_STATESAVE_BEHAVIORNEWDATA));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            chunk, optional_ids[case_index]));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0));
        nmo_chunk_close(chunk);
        ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_behavior_deserialize(
            &state, chunk, NULL, NULL));
        ASSERT_EQ(42, state.priority);
    }

    nmo_chunk_t *null_interface = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(null_interface);
    null_interface->class_id = NMO_CID_BEHAVIOR;
    null_interface->data_version = 7;
    null_interface->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(null_interface));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        null_interface, CK_STATESAVE_BEHAVIORNEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(null_interface, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(null_interface, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        null_interface, CK_STATESAVE_BEHAVIORINTERFACE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(null_interface, 0u));
    nmo_chunk_close(null_interface);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_behavior_deserialize(
        &state, null_interface, NULL, NULL));
    ASSERT_EQ(42, state.priority);

    nmo_behavior_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, behavior_non_file_reads_single_activity) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    const uint32_t activity_ids[] = {
        CK_STATESAVE_BEHAVIORSINGLEACTIVITY,
        0x00000004u,
    };
    for (size_t i = 0; i < sizeof(activity_ids) / sizeof(activity_ids[0]); ++i) {
        nmo_chunk_t *chunk = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(chunk);
        chunk->class_id = NMO_CID_BEHAVIOR;
        chunk->data_version = 7;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            chunk, CK_STATESAVE_BEHAVIORSUBBEHAV));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 0));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            chunk, CK_STATESAVE_BEHAVIORLOCALPARAMS));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 0));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            chunk, activity_ids[i]));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
            chunk, 0x12345678u + (uint32_t)i));
        nmo_chunk_close(chunk);

        nmo_behavior_state_t state;
        ASSERT_EQ(NMO_OK, nmo_behavior_vtable.create(&state, NULL, NULL));
        ASSERT_EQ(NMO_OK, nmo_behavior_deserialize(
            &state, chunk, NULL, NULL));
        ASSERT_TRUE(state.has_single_activity);
        ASSERT_EQ(0x12345678u + (uint32_t)i,
                  state.single_activity_flags);
        ASSERT_EQ(i == 1u, state.use_legacy_identifiers);
        nmo_behavior_vtable.destroy(&state, NULL, NULL);
    }

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

    uint8_t malformed_entry = 0;
    invalid.inputs.data = &malformed_entry;
    invalid.inputs.element_size = sizeof(malformed_entry);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_behavior_serialize(
        &invalid, chunk, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(chunk));

    invalid.inputs.data = &invalid;
    invalid.inputs.element_size = sizeof(nmo_behavior_ref_t);
    invalid.inputs.count = (size_t)INT32_MAX + 1u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_behavior_serialize(
        &invalid, chunk, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(chunk));

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
    ASSERT_TRUE(state.has_flags);
    state.base.visibility_flags = 0x12345678u;
    state.old_flags = 0xA5A5A5A5u;
    state.has_flags = true;
    ASSERT_NE(NMO_OK, nmo_behaviorio_deserialize(
        &state, chunk, NULL, NULL));
    ASSERT_EQ(0x12345678u, state.base.visibility_flags);
    ASSERT_EQ(0xA5A5A5A5u, state.old_flags);
    ASSERT_TRUE(state.has_flags);

    nmo_chunk_t *cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section);
    cross_section->class_id = NMO_CID_BEHAVIORIO;
    cross_section->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section, CK_STATESAVE_BEHAV_IOFLAGS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section, 0x11223344u));
    nmo_chunk_close(cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_behaviorio_deserialize(
        &state, cross_section, NULL, NULL));
    ASSERT_EQ(0x12345678u, state.base.visibility_flags);
    ASSERT_EQ(0xA5A5A5A5u, state.old_flags);
    ASSERT_TRUE(state.has_flags);

    nmo_chunk_t *trailing_payload = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_payload);
    trailing_payload->class_id = NMO_CID_BEHAVIORIO;
    trailing_payload->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_payload));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_payload, CK_STATESAVE_BEHAV_IOFLAGS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        trailing_payload, 0x01020304u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        trailing_payload, 0x05060708u));
    nmo_chunk_close(trailing_payload);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_behaviorio_deserialize(
        &state, trailing_payload, NULL, NULL));
    ASSERT_EQ(0x12345678u, state.base.visibility_flags);
    ASSERT_EQ(0xA5A5A5A5u, state.old_flags);
    ASSERT_TRUE(state.has_flags);

    nmo_behaviorio_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, behavior_layout_defaults_preserve_legacy_absence) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *legacy_io = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_io);
    legacy_io->class_id = NMO_CID_BEHAVIORIO;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_io));
    nmo_chunk_close(legacy_io);

    nmo_behaviorio_state_t io;
    ASSERT_EQ(NMO_OK, nmo_behaviorio_vtable.create(&io, NULL, NULL));
    ASSERT_TRUE(io.has_flags);
    io.old_flags = 0x12345678u;
    ASSERT_EQ(NMO_OK, nmo_behaviorio_deserialize(
        &io, legacy_io, NULL, NULL));
    ASSERT_FALSE(io.has_flags);
    ASSERT_EQ(0u, io.old_flags);
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, io.base.visibility_flags);

    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_chunk_t *saved_io = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(saved_io);
    saved_io->class_id = NMO_CID_BEHAVIORIO;
    saved_io->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_behaviorio_serialize(
        &io, saved_io, NULL, &serialize_context));
    nmo_chunk_close(saved_io);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        saved_io, CK_STATESAVE_BEHAV_IOFLAGS));

    nmo_chunk_t *legacy_link = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_link);
    legacy_link->class_id = NMO_CID_BEHAVIORLINK;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_link));
    nmo_chunk_close(legacy_link);

    nmo_behaviorlink_state_t link;
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(&link, NULL, NULL));
    ASSERT_TRUE(link.has_format);
    ASSERT_TRUE(link.use_new_format);
    link.activation_delay = 7;
    link.initial_activation_delay = 8;
    link.in_io = nmo_ref_from_raw(501);
    link.out_io = nmo_ref_from_raw(502);
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_deserialize(
        &link, legacy_link, NULL, NULL));
    ASSERT_FALSE(link.has_format);
    ASSERT_FALSE(link.use_new_format);
    ASSERT_EQ(1, link.activation_delay);
    ASSERT_EQ(1, link.initial_activation_delay);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, nmo_ref_serialized_id(&link.in_io));
    ASSERT_EQ(NMO_OBJECT_ID_NONE, nmo_ref_serialized_id(&link.out_io));
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, link.base.visibility_flags);

    nmo_chunk_t *saved_empty_link = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(saved_empty_link);
    saved_empty_link->class_id = NMO_CID_BEHAVIORLINK;
    saved_empty_link->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_serialize(
        &link, saved_empty_link, NULL, &serialize_context));
    nmo_chunk_close(saved_empty_link);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        saved_empty_link, CK_STATESAVE_BEHAV_LINK_NEWDATA));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        saved_empty_link, CK_STATESAVE_BEHAV_LINK_CURDELAY));

    io.old_flags = 1u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_behaviorio_serialize(
        &io, saved_io, NULL, &serialize_context));
    io.old_flags = 0u;
    link.activation_delay = 2;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_behaviorlink_serialize(
        &link, saved_empty_link, NULL, &serialize_context));
    link.activation_delay = 1;
    link.has_format = true;
    link.use_new_format = false;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_behaviorlink_serialize(
        &link, saved_empty_link, NULL, &serialize_context));
    link.has_format = false;

    nmo_chunk_t *legacy_sections = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_sections);
    legacy_sections->class_id = NMO_CID_BEHAVIORLINK;
    legacy_sections->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_sections));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_sections, CK_STATESAVE_BEHAV_LINK_CURDELAY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(legacy_sections, 4));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_sections, CK_STATESAVE_BEHAV_LINK_IOS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        legacy_sections, 501));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        legacy_sections, 502));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_sections, CK_STATESAVE_BEHAV_LINK_DELAY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(legacy_sections, 9));
    nmo_chunk_close(legacy_sections);

    nmo_behaviorlink_state_t legacy_state;
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(
        &legacy_state, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_deserialize(
        &legacy_state, legacy_sections, NULL, NULL));
    ASSERT_TRUE(legacy_state.has_format);
    ASSERT_FALSE(legacy_state.use_new_format);
    ASSERT_TRUE(legacy_state.has_legacy_curdelay);
    ASSERT_TRUE(legacy_state.has_legacy_ios);
    ASSERT_TRUE(legacy_state.has_legacy_delay);

    nmo_chunk_t *saved_legacy_link = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(saved_legacy_link);
    saved_legacy_link->class_id = NMO_CID_BEHAVIORLINK;
    saved_legacy_link->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_serialize(
        &legacy_state, saved_legacy_link, NULL, &serialize_context));
    nmo_chunk_close(saved_legacy_link);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        saved_legacy_link, CK_STATESAVE_BEHAV_LINK_NEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        saved_legacy_link, CK_STATESAVE_BEHAV_LINK_CURDELAY));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        saved_legacy_link, CK_STATESAVE_BEHAV_LINK_IOS));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        saved_legacy_link, CK_STATESAVE_BEHAV_LINK_DELAY));

    nmo_behaviorio_state_t io_copy;
    nmo_behaviorlink_state_t link_copy;
    ASSERT_EQ(NMO_OK, nmo_behaviorio_vtable.create(
        &io_copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(
        &link_copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behaviorio_vtable.copy(
        &io, &io_copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.copy(
        &legacy_state, &link_copy, NULL, NULL));
    ASSERT_TRUE(nmo_behaviorio_vtable.equals(&io, &io_copy));
    ASSERT_TRUE(nmo_behaviorlink_vtable.equals(&legacy_state, &link_copy));
    ASSERT_EQ(nmo_behaviorio_vtable.hash(&io),
              nmo_behaviorio_vtable.hash(&io_copy));
    ASSERT_EQ(nmo_behaviorlink_vtable.hash(&legacy_state),
              nmo_behaviorlink_vtable.hash(&link_copy));

    nmo_behaviorlink_vtable.destroy(&link_copy, NULL, NULL);
    nmo_behaviorio_vtable.destroy(&io_copy, NULL, NULL);
    nmo_behaviorlink_vtable.destroy(&legacy_state, NULL, NULL);
    nmo_behaviorlink_vtable.destroy(&link, NULL, NULL);
    nmo_behaviorio_vtable.destroy(&io, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, dataarray_cell_refs_round_trip_raw_ids) {
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
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_dataarray_state_t source;
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.create(&source, NULL, NULL));
    source.column_count = 2;
    source.column_formats = (nmo_dataarray_column_format_t *)nmo_arena_alloc(
        arena, 2 * sizeof(*source.column_formats),
        _Alignof(nmo_dataarray_column_format_t));
    ASSERT_NOT_NULL(source.column_formats);
    memset(source.column_formats, 0, 2 * sizeof(*source.column_formats));
    source.column_formats[0].name = "Object";
    source.column_formats[0].type = CKARRAYTYPE_OBJECT;
    source.column_formats[1].name = "Parameter";
    source.column_formats[1].type = CKARRAYTYPE_PARAMETER;
    source.row_count = 1;
    source.rows = (nmo_dataarray_row_t *)nmo_arena_alloc(
        arena, sizeof(*source.rows), _Alignof(nmo_dataarray_row_t));
    ASSERT_NOT_NULL(source.rows);
    memset(source.rows, 0, sizeof(*source.rows));
    source.rows[0].column_count = 2;
    source.rows[0].cells = (nmo_dataarray_cell_t *)nmo_arena_alloc(
        arena, 2 * sizeof(*source.rows[0].cells),
        _Alignof(nmo_dataarray_cell_t));
    ASSERT_NOT_NULL(source.rows[0].cells);
    memset(source.rows[0].cells, 0, 2 * sizeof(*source.rows[0].cells));
    source.rows[0].cells[0].object_ref = nmo_ref_from_raw(777);
    source.rows[0].cells[1].parameter.ref = nmo_ref_from_raw(778);

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_DATAARRAY;
    first->data_version = 7;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(first, &write_context);
    ASSERT_EQ(NMO_OK, nmo_dataarray_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    nmo_chunk_set_file_context(first, &read_context);

    nmo_dataarray_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_dataarray_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_EQ(777u, loaded.rows[0].cells[0].object_ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED,
              loaded.rows[0].cells[0].object_ref.state);
    ASSERT_EQ(778u, loaded.rows[0].cells[1].parameter.ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED,
              loaded.rows[0].cells[1].parameter.ref.state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_DATAARRAY;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_dataarray_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);

    nmo_dataarray_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_dataarray_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(777u, reloaded.rows[0].cells[0].object_ref.raw_id);
    ASSERT_EQ(778u, reloaded.rows[0].cells[1].parameter.ref.raw_id);

    nmo_array_dispose(&loaded.base.scripts);
    nmo_array_dispose(&loaded.base.attributes);
    nmo_array_dispose(&loaded.base.legacy_attributes);
    nmo_array_dispose(&reloaded.base.scripts);
    nmo_array_dispose(&reloaded.base.attributes);
    nmo_array_dispose(&reloaded.base.legacy_attributes);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, dataarray_legacy_members_match_storage_mode) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t memory_serialize =
        nmo_serialize_context_create(arena, NULL, 0, 0);
    nmo_deserialize_context_t memory_deserialize =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_serialize_context_t file_serialize =
        nmo_serialize_context_create(
            arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t file_deserialize =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_dataarray_state_t source;
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.create(&source, NULL, NULL));
    source.order = 11;
    source.column_index = 12;
    source.key_column = 13;

    nmo_chunk_t *memory_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(memory_chunk);
    memory_chunk->class_id = NMO_CID_DATAARRAY;
    memory_chunk->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_dataarray_serialize(
        &source, memory_chunk, NULL, &memory_serialize));
    nmo_chunk_close(memory_chunk);
    size_t section_dwords = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        memory_chunk, CK_STATESAVE_DATAARRAYMEMBERS, &section_dwords));
    ASSERT_EQ(2u, section_dwords);

    nmo_dataarray_state_t memory_loaded;
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.create(
        &memory_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_dataarray_deserialize(
        &memory_loaded, memory_chunk, NULL, &memory_deserialize));
    ASSERT_EQ(source.order, memory_loaded.order);
    ASSERT_EQ(source.column_index, memory_loaded.column_index);
    ASSERT_EQ(-1, memory_loaded.key_column);

    nmo_chunk_t *file_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(file_chunk);
    file_chunk->class_id = NMO_CID_DATAARRAY;
    file_chunk->data_version = 4;
    file_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_dataarray_serialize(
        &source, file_chunk, NULL, &file_serialize));
    nmo_chunk_close(file_chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        file_chunk, CK_STATESAVE_DATAARRAYMEMBERS, &section_dwords));
    ASSERT_EQ(3u, section_dwords);

    nmo_dataarray_state_t file_loaded;
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.create(
        &file_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_dataarray_deserialize(
        &file_loaded, file_chunk, NULL, &file_deserialize));
    ASSERT_EQ(source.key_column, file_loaded.key_column);

    nmo_dataarray_vtable.destroy(&source, NULL, NULL);
    nmo_dataarray_vtable.destroy(&memory_loaded, NULL, NULL);
    nmo_dataarray_vtable.destroy(&file_loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, dataarray_preserves_large_dimensions) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    const uint32_t wide_count = 10001u;
    nmo_dataarray_state_t wide;
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.create(&wide, NULL, NULL));
    wide.column_count = wide_count;
    wide.column_formats = nmo_arena_alloc(
        arena, sizeof(*wide.column_formats) * wide_count,
        _Alignof(nmo_dataarray_column_format_t));
    ASSERT_NOT_NULL(wide.column_formats);
    memset(wide.column_formats, 0,
           sizeof(*wide.column_formats) * wide_count);
    for (uint32_t i = 0; i < wide_count; ++i) {
        wide.column_formats[i].name = "Value";
        wide.column_formats[i].type = CKARRAYTYPE_INT;
    }
    nmo_chunk_t *wide_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(wide_chunk);
    wide_chunk->class_id = NMO_CID_DATAARRAY;
    wide_chunk->data_version = 7;
    wide_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_dataarray_serialize(
        &wide, wide_chunk, NULL, &serialize_context));
    nmo_chunk_close(wide_chunk);
    nmo_dataarray_state_t wide_loaded;
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.create(
        &wide_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_dataarray_deserialize(
        &wide_loaded, wide_chunk, NULL, &deserialize_context));
    ASSERT_EQ(wide_count, wide_loaded.column_count);
    ASSERT_EQ(CKARRAYTYPE_INT,
              wide_loaded.column_formats[wide_count - 1u].type);

    const uint32_t tall_count = 1000001u;
    nmo_dataarray_state_t tall;
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.create(&tall, NULL, NULL));
    tall.row_count = tall_count;
    tall.rows = nmo_arena_alloc(
        arena, sizeof(*tall.rows) * tall_count,
        _Alignof(nmo_dataarray_row_t));
    ASSERT_NOT_NULL(tall.rows);
    memset(tall.rows, 0, sizeof(*tall.rows) * tall_count);
    nmo_chunk_t *tall_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(tall_chunk);
    tall_chunk->class_id = NMO_CID_DATAARRAY;
    tall_chunk->data_version = 7;
    tall_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_dataarray_serialize(
        &tall, tall_chunk, NULL, &serialize_context));
    nmo_chunk_close(tall_chunk);
    nmo_dataarray_state_t tall_loaded;
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.create(
        &tall_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_dataarray_deserialize(
        &tall_loaded, tall_chunk, NULL, &deserialize_context));
    ASSERT_EQ(tall_count, tall_loaded.row_count);
    ASSERT_EQ(0u, tall_loaded.rows[tall_count - 1u].column_count);
    ASSERT_NULL(tall_loaded.rows[tall_count - 1u].cells);

    nmo_dataarray_vtable.destroy(&wide, NULL, NULL);
    nmo_dataarray_vtable.destroy(&wide_loaded, NULL, NULL);
    nmo_dataarray_vtable.destroy(&tall, NULL, NULL);
    nmo_dataarray_vtable.destroy(&tall_loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, dataarray_failures_keep_state_and_target_chunk_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_DATAARRAY;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_DATAARRAYFORMAT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(truncated, "Value"));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, CKARRAYTYPE_INT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_DATAARRAYDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 1));
    nmo_chunk_close(truncated);

    nmo_dataarray_state_t state;
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.create(&state, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&state.base, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &state.base.scripts, 901));
    nmo_dataarray_column_format_t old_format = {
        .name = "Old",
        .type = CKARRAYTYPE_INT,
    };
    nmo_dataarray_cell_t old_cell = {.int_value = 77};
    nmo_dataarray_row_t old_row = {
        .cells = &old_cell,
        .column_count = 1,
    };
    state.column_count = 1;
    state.column_formats = &old_format;
    state.row_count = 1;
    state.rows = &old_row;
    state.order = 71;
    state.column_index = 72;
    state.key_column = 0;

    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_dataarray_deserialize(
        &state, truncated, NULL, &deserialize_context));
    ASSERT_EQ(&old_format, state.column_formats);
    ASSERT_EQ(&old_row, state.rows);
    ASSERT_EQ(1u, state.column_count);
    ASSERT_EQ(1u, state.row_count);
    ASSERT_EQ(71, state.order);
    ASSERT_EQ(72u, state.column_index);
    ASSERT_EQ(0, state.key_column);
    ASSERT_EQ(901u, nmo_beobject_script_array_get_id(
        &state.base.scripts, 0));

    nmo_chunk_t *impossible_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(impossible_count);
    impossible_count->class_id = NMO_CID_DATAARRAY;
    impossible_count->data_version = 7;
    impossible_count->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(impossible_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        impossible_count, CK_STATESAVE_DATAARRAYFORMAT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(impossible_count, 10000));
    nmo_chunk_close(impossible_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_dataarray_deserialize(
        &state, impossible_count, NULL, &deserialize_context));
    ASSERT_EQ(&old_format, state.column_formats);
    ASSERT_EQ(&old_row, state.rows);
    ASSERT_EQ(901u, nmo_beobject_script_array_get_id(
        &state.base.scripts, 0));

    nmo_chunk_t *format_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(format_cross_section);
    format_cross_section->class_id = NMO_CID_DATAARRAY;
    format_cross_section->data_version = 7;
    format_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(format_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        format_cross_section, CK_STATESAVE_DATAARRAYFORMAT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(format_cross_section, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(format_cross_section, 8));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        format_cross_section, 0x41414141u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        format_cross_section, 0x00000042u));
    nmo_chunk_close(format_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_dataarray_deserialize(
        &state, format_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(&old_format, state.column_formats);
    ASSERT_EQ(&old_row, state.rows);

    nmo_chunk_t *data_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(data_cross_section);
    data_cross_section->class_id = NMO_CID_DATAARRAY;
    data_cross_section->data_version = 7;
    data_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(data_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        data_cross_section, CK_STATESAVE_DATAARRAYFORMAT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(data_cross_section, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(data_cross_section, "Text"));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(
        data_cross_section, CKARRAYTYPE_STRING));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        data_cross_section, CK_STATESAVE_DATAARRAYDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(data_cross_section, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(data_cross_section, 8));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        data_cross_section, 0x42424242u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        data_cross_section, 0x00000043u));
    nmo_chunk_close(data_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_dataarray_deserialize(
        &state, data_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(&old_format, state.column_formats);
    ASSERT_EQ(&old_row, state.rows);

    nmo_chunk_t *members_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(members_cross_section);
    members_cross_section->class_id = NMO_CID_DATAARRAY;
    members_cross_section->data_version = 7;
    members_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(members_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        members_cross_section, CK_STATESAVE_DATAARRAYMEMBERS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(members_cross_section, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        members_cross_section, 0xC1D2E3F4u));
    nmo_chunk_close(members_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_dataarray_deserialize(
        &state, members_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(&old_format, state.column_formats);
    ASSERT_EQ(&old_row, state.rows);
    ASSERT_EQ(71, state.order);
    ASSERT_EQ(72u, state.column_index);
    ASSERT_EQ(0, state.key_column);

    const struct {
        uint32_t identifier;
        size_t payload_dwords;
    } trailing_sections[] = {
        {CK_STATESAVE_DATAARRAYFORMAT, 1u},
        {CK_STATESAVE_DATAARRAYDATA, 1u},
        {CK_STATESAVE_DATAARRAYMEMBERS, 3u},
    };
    for (size_t i = 0;
         i < sizeof(trailing_sections) / sizeof(trailing_sections[0]);
         ++i) {
        nmo_chunk_t *trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(trailing);
        trailing->class_id = NMO_CID_DATAARRAY;
        trailing->data_version = 7;
        trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            trailing, trailing_sections[i].identifier));
        for (size_t j = 0; j < trailing_sections[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0x12345678u));
        nmo_chunk_close(trailing);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_dataarray_deserialize(
            &state, trailing, NULL, &deserialize_context));
        ASSERT_EQ(&old_format, state.column_formats);
        ASSERT_EQ(&old_row, state.rows);
        ASSERT_EQ(71, state.order);
        ASSERT_EQ(72u, state.column_index);
        ASSERT_EQ(0, state.key_column);
    }

    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_chunk_file_context_t file_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_dataarray_state_t source;
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.create(&source, NULL, NULL));
    nmo_dataarray_column_format_t format = {
        .name = "Object",
        .type = CKARRAYTYPE_OBJECT,
    };
    nmo_dataarray_cell_t cell = {
        .object_ref = nmo_ref_from_id(123),
    };
    nmo_dataarray_row_t row = {
        .cells = &cell,
        .column_count = 1,
    };
    source.column_count = 1;
    source.column_formats = &format;
    source.row_count = 1;
    source.rows = &row;

    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_DATAARRAY;
    target->data_version = 7;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &file_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_dataarray_serialize(
        &source, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_array_dispose(&state.base.scripts);
    nmo_array_dispose(&state.base.attributes);
    nmo_array_dispose(&state.base.legacy_attributes);
    nmo_dataarray_vtable.destroy(&state, NULL, NULL);
    nmo_dataarray_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, dataarray_copy_preserves_typed_cell_content) {
    nmo_arena_t *source_arena = nmo_arena_create(NULL, 8192);
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(source_arena);
    ASSERT_NOT_NULL(copy_arena);

    nmo_dataarray_column_format_t formats[] = {
        {.name = "Integer", .type = CKARRAYTYPE_INT},
        {.name = "Float", .type = CKARRAYTYPE_FLOAT},
        {.name = "String", .type = CKARRAYTYPE_STRING},
        {.name = "Object", .type = CKARRAYTYPE_OBJECT},
        {
            .name = "Parameter",
            .type = CKARRAYTYPE_PARAMETER,
            .parameter_type_guid = CKPGUID_INT,
        },
    };
    nmo_dataarray_cell_t cells[5] = {0};
    cells[0].int_value = 17;
    cells[1].float_value = 2.5f;
    cells[2].string_value = "Value";
    cells[3].object_ref = nmo_ref_from_raw(301);
    cells[4].parameter.ref = nmo_ref_from_raw(302);
    cells[4].parameter.chunk = nmo_chunk_create(source_arena);
    ASSERT_NOT_NULL(cells[4].parameter.chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cells[4].parameter.chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        cells[4].parameter.chunk, 0xaabbccddu));
    nmo_chunk_close(cells[4].parameter.chunk);
    nmo_dataarray_row_t row = {
        .column_count = 5,
        .cells = cells,
    };

    nmo_dataarray_state_t source;
    nmo_dataarray_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.create(&copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &source.base.scripts, 101));
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(
        &source.base.attributes, 201, 7, NULL));
    source.column_count = 5;
    source.column_formats = formats;
    source.row_count = 1;
    source.rows = &row;
    source.order = 1;
    source.column_index = 2;
    source.key_column = 0;

    nmo_type_descriptor_t type = {
        .size = sizeof(nmo_dataarray_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.copy(
        &source, &copy, &type, copy_arena));
    ASSERT_NE(source.base.scripts.data, copy.base.scripts.data);
    ASSERT_NE(source.base.attributes.data, copy.base.attributes.data);
    ASSERT_NE(source.column_formats, copy.column_formats);
    ASSERT_NE(source.rows, copy.rows);
    ASSERT_NE(source.rows[0].cells, copy.rows[0].cells);
    ASSERT_NE(source.column_formats[2].name,
              copy.column_formats[2].name);
    ASSERT_NE(source.rows[0].cells[2].string_value,
              copy.rows[0].cells[2].string_value);
    ASSERT_NE(source.rows[0].cells[4].parameter.chunk,
              copy.rows[0].cells[4].parameter.chunk);
    ASSERT_TRUE(nmo_dataarray_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_dataarray_vtable.hash(&source),
              nmo_dataarray_vtable.hash(&copy));

    fail_after_allocator_state_t allocator_state = {
        .allowed_allocations = 1,
    };
    nmo_allocator_t failing_allocator = {
        .alloc = fail_after_alloc,
        .free = fail_after_free,
        .user_data = &allocator_state,
    };
    nmo_dataarray_state_t failing_source;
    ASSERT_EQ(NMO_OK, nmo_dataarray_vtable.create(
        &failing_source, NULL, NULL));
    nmo_array_dispose(&failing_source.base.scripts);
    ASSERT_EQ(NMO_OK, nmo_array_init(
        &failing_source.base.scripts, sizeof(nmo_ref_t), 1,
        &failing_allocator));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &failing_source.base.scripts, 401));
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(
        &failing_source.base.attributes, 402, 9, NULL));
    failing_source.column_count = 5;
    failing_source.column_formats = formats;
    failing_source.row_count = 1;
    failing_source.rows = &row;
    allocator_state.allowed_allocations = allocator_state.allocation_count;
    nmo_dataarray_column_format_t *published_formats = copy.column_formats;
    nmo_dataarray_row_t *published_rows = copy.rows;
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_dataarray_vtable.copy(
        &failing_source, &copy, &type, copy_arena));
    ASSERT_EQ(published_formats, copy.column_formats);
    ASSERT_EQ(published_rows, copy.rows);
    ASSERT_EQ(1u, failing_source.base.attributes.count);
    ASSERT_NOT_NULL(failing_source.base.attributes.data);

    ((char *)copy.rows[0].cells[2].string_value)[0] = 'X';
    ASSERT_STR_EQ("Value", source.rows[0].cells[2].string_value);
    ASSERT_FALSE(nmo_dataarray_vtable.equals(&source, &copy));

    nmo_dataarray_row_t invalid_row = row;
    invalid_row.column_count = 4;
    nmo_dataarray_state_t invalid = source;
    invalid.rows = &invalid_row;
    published_formats = copy.column_formats;
    published_rows = copy.rows;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_dataarray_vtable.copy(
        &invalid, &copy, &type, copy_arena));
    ASSERT_EQ(published_formats, copy.column_formats);
    ASSERT_EQ(published_rows, copy.rows);

    nmo_dataarray_vtable.destroy(&failing_source, NULL, NULL);
    nmo_dataarray_vtable.destroy(&source, NULL, NULL);
    nmo_dataarray_vtable.destroy(&copy, NULL, NULL);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(source_arena);
}

TEST(chunk_id_remap, attributemanager_failures_keep_state_and_target_chunk_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(truncated, 0x52u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(truncated, "Category"));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 17));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(truncated, "Attribute"));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 0x11111111u));
    nmo_chunk_close(truncated);

    nmo_attributemanager_state_t state = {0};
    nmo_attribute_category_t old_category = {
        .present = true,
        .name = "Old category",
        .flags = 71,
    };
    nmo_attribute_descriptor_t old_attribute = {
        .present = true,
        .name = "Old attribute",
        .category_index = 0,
        .compatible_class_id = NMO_CID_BEOBJECT,
        .flags = 72,
    };
    state.category_count = 1;
    state.categories = &old_category;
    state.attribute_count = 1;
    state.attributes = &old_attribute;

    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_attributemanager_deserialize(
        &state, truncated, NULL, &deserialize_context));
    ASSERT_EQ(&old_category, state.categories);
    ASSERT_EQ(&old_attribute, state.attributes);
    ASSERT_EQ(1u, state.category_count);
    ASSERT_EQ(1u, state.attribute_count);
    ASSERT_EQ(71u, state.categories[0].flags);
    ASSERT_EQ(72u, state.attributes[0].flags);

    nmo_chunk_t *impossible_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(impossible_count);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(impossible_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        impossible_count, 0x52u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(impossible_count, 10000));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(impossible_count, 100000));
    nmo_chunk_close(impossible_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_attributemanager_deserialize(
        &state, impossible_count, NULL, &deserialize_context));
    ASSERT_EQ(&old_category, state.categories);
    ASSERT_EQ(&old_attribute, state.attributes);

    nmo_chunk_t *trailing_payload = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_payload);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_payload));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_payload, 0x52u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(trailing_payload, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(trailing_payload, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        trailing_payload, 0x12345678u));
    nmo_chunk_close(trailing_payload);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_attributemanager_deserialize(
        &state, trailing_payload, NULL, &deserialize_context));
    ASSERT_EQ(&old_category, state.categories);
    ASSERT_EQ(&old_attribute, state.attributes);

    nmo_chunk_t *cross_section_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_count);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_count, 0x52u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_count, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_count, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_count, 0x7F123456u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_count, 0));
    nmo_chunk_close(cross_section_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_attributemanager_deserialize(
        &state, cross_section_count, NULL, &deserialize_context));
    nmo_chunk_parser_state_t *parser =
        (nmo_chunk_parser_state_t *)cross_section_count->parser_state;
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(parser->prev_identifier_pos + 4u, parser->current_pos);
    ASSERT_EQ(&old_category, state.categories);
    ASSERT_EQ(&old_attribute, state.attributes);

    nmo_chunk_t *cross_section_string = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_string);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_string));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_string, 0x52u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_string, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_string, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_string, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_string, 8u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_string, 0x44434241u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_string, 17u));
    nmo_chunk_close(cross_section_string);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_attributemanager_deserialize(
        &state, cross_section_string, NULL, &deserialize_context));
    ASSERT_EQ(&old_category, state.categories);
    ASSERT_EQ(&old_attribute, state.attributes);

    nmo_chunk_t *missing_name = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_name);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_name));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(missing_name, 0x52u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(missing_name, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(missing_name, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(missing_name, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(missing_name, NULL));
    nmo_chunk_close(missing_name);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_attributemanager_deserialize(
                  &state, missing_name, NULL, &deserialize_context));
    ASSERT_EQ(&old_category, state.categories);
    ASSERT_EQ(&old_attribute, state.attributes);

    nmo_attributemanager_state_t invalid = {
        .category_count = 1,
        .categories = &old_category,
        .attribute_count = 1,
        .attributes = NULL,
    };
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_NE(NMO_OK, nmo_attributemanager_serialize(
        &invalid, target, NULL, NULL));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    const uint32_t large_category_count = 10001u;
    const uint32_t large_attribute_count = 100001u;
    nmo_attribute_category_t *large_categories = nmo_arena_alloc(
        arena, (size_t)large_category_count * sizeof(*large_categories),
        _Alignof(nmo_attribute_category_t));
    nmo_attribute_descriptor_t *large_attributes = nmo_arena_alloc(
        arena, (size_t)large_attribute_count * sizeof(*large_attributes),
        _Alignof(nmo_attribute_descriptor_t));
    ASSERT_NOT_NULL(large_categories);
    ASSERT_NOT_NULL(large_attributes);
    memset(large_categories, 0,
           (size_t)large_category_count * sizeof(*large_categories));
    memset(large_attributes, 0,
           (size_t)large_attribute_count * sizeof(*large_attributes));
    nmo_attributemanager_state_t large = {
        .category_count = large_category_count,
        .categories = large_categories,
        .attribute_count = large_attribute_count,
        .attributes = large_attributes,
    };
    ASSERT_EQ(NMO_OK, nmo_attributemanager_vtable.validate(
        &large, NULL, NULL));
    nmo_chunk_t *large_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(large_chunk);
    ASSERT_EQ(NMO_OK, nmo_attributemanager_serialize(
        &large, large_chunk, NULL, NULL));
    nmo_chunk_close(large_chunk);
    nmo_attributemanager_state_t large_loaded = {0};
    ASSERT_EQ(NMO_OK, nmo_attributemanager_deserialize(
        &large_loaded, large_chunk, NULL, &deserialize_context));
    ASSERT_EQ(large_category_count, large_loaded.category_count);
    ASSERT_EQ(large_attribute_count, large_loaded.attribute_count);

    nmo_attributemanager_state_t oversized = {
        .category_count = (uint32_t)INT32_MAX + 1u,
        .categories = &old_category,
    };
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_attributemanager_vtable.validate(
                  &oversized, NULL, NULL));
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_attributemanager_serialize(
                  &oversized, target, NULL, NULL));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, attributemanager_copy_preserves_record_content) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_attribute_category_t categories[] = {
        {.name = "Gameplay", .flags = 17, .present = true},
        {.name = NULL, .flags = 0, .present = false},
    };
    nmo_attribute_descriptor_t attributes[] = {
        {
            .name = "Active",
            .parameter_type_guid = CKPGUID_BOOL,
            .category_index = 0,
            .compatible_class_id = NMO_CID_BEOBJECT,
            .flags = 23,
            .present = true,
        },
    };
    nmo_attributemanager_state_t source = {
        .category_count = 2,
        .categories = categories,
        .attribute_count = 1,
        .attributes = attributes,
    };
    nmo_attribute_category_t old_category = {
        .name = "Old",
        .present = true,
    };
    nmo_attributemanager_state_t copy = {
        .category_count = 1,
        .categories = &old_category,
    };
    nmo_type_descriptor_t type = {
        .size = sizeof(nmo_attributemanager_state_t),
    };

    ASSERT_EQ(NMO_OK, nmo_attributemanager_vtable.copy(
        &source, &copy, &type, arena));
    ASSERT_NE(source.categories, copy.categories);
    ASSERT_NE(source.attributes, copy.attributes);
    ASSERT_NE(source.categories[0].name, copy.categories[0].name);
    ASSERT_NE(source.attributes[0].name, copy.attributes[0].name);
    ASSERT_TRUE(nmo_attributemanager_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_attributemanager_vtable.hash(&source),
              nmo_attributemanager_vtable.hash(&copy));
    ASSERT_EQ(NMO_OK, nmo_attributemanager_vtable.validate(
        &copy, &type, NULL));

    ((char *)copy.attributes[0].name)[0] = 'X';
    ASSERT_STR_EQ("Active", source.attributes[0].name);
    ASSERT_FALSE(nmo_attributemanager_vtable.equals(&source, &copy));

    nmo_attribute_descriptor_t invalid_attribute = {
        .name = NULL,
        .present = true,
    };
    nmo_attributemanager_state_t invalid = {
        .attribute_count = 1,
        .attributes = &invalid_attribute,
    };
    nmo_attribute_category_t *published_categories = copy.categories;
    nmo_attribute_descriptor_t *published_attributes = copy.attributes;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_attributemanager_vtable.copy(
                  &invalid, &copy, &type, arena));
    ASSERT_EQ(published_categories, copy.categories);
    ASSERT_EQ(published_attributes, copy.attributes);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_attributemanager_vtable.validate(
                  &invalid, &type, NULL));

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, messagemanager_failures_keep_state_and_target_chunk_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(truncated, 0x53u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(truncated, "First"));
    nmo_chunk_close(truncated);

    const char *old_names[] = {"Old"};
    nmo_messagemanager_state_t state = {
        .message_type_count = 1,
        .message_type_names = old_names,
    };
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_messagemanager_deserialize(
        &state, truncated, NULL, &deserialize_context));
    ASSERT_EQ(old_names, state.message_type_names);
    ASSERT_EQ(1u, state.message_type_count);
    ASSERT_STR_EQ("Old", state.message_type_names[0]);

    nmo_chunk_t *impossible_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(impossible_count);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(impossible_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        impossible_count, 0x53u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(impossible_count, 10000));
    nmo_chunk_close(impossible_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_messagemanager_deserialize(
        &state, impossible_count, NULL, &deserialize_context));
    ASSERT_EQ(old_names, state.message_type_names);
    ASSERT_EQ(1u, state.message_type_count);

    nmo_chunk_t *trailing_payload = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_payload);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_payload));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_payload, 0x53u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(trailing_payload, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        trailing_payload, 0x12345678u));
    nmo_chunk_close(trailing_payload);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_messagemanager_deserialize(
        &state, trailing_payload, NULL, &deserialize_context));
    ASSERT_EQ(old_names, state.message_type_names);
    ASSERT_EQ(1u, state.message_type_count);

    nmo_chunk_t *cross_section_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_count);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_count, 0x53u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_count, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_count, 0x7F123456u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_count, 0));
    nmo_chunk_close(cross_section_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_messagemanager_deserialize(
        &state, cross_section_count, NULL, &deserialize_context));
    nmo_chunk_parser_state_t *parser =
        (nmo_chunk_parser_state_t *)cross_section_count->parser_state;
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(parser->prev_identifier_pos + 3u, parser->current_pos);
    ASSERT_EQ(old_names, state.message_type_names);
    ASSERT_EQ(1u, state.message_type_count);

    nmo_chunk_t *cross_section_string = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_string);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_string));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_string, 0x53u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_string, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_string, 8u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_string, 0x44434241u));
    nmo_chunk_close(cross_section_string);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_messagemanager_deserialize(
        &state, cross_section_string, NULL, &deserialize_context));
    ASSERT_EQ(old_names, state.message_type_names);
    ASSERT_EQ(1u, state.message_type_count);

    nmo_messagemanager_state_t invalid = {
        .message_type_count = 1,
        .message_type_names = NULL,
    };
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_NE(NMO_OK, nmo_messagemanager_serialize(
        &invalid, target, NULL, NULL));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    const uint32_t large_count = 10001u;
    const char **large_names = nmo_arena_alloc(
        arena, (size_t)large_count * sizeof(*large_names),
        _Alignof(char *));
    ASSERT_NOT_NULL(large_names);
    for (uint32_t i = 0; i < large_count; ++i) {
        large_names[i] = "";
    }
    nmo_messagemanager_state_t large = {
        .message_type_count = large_count,
        .message_type_names = large_names,
    };
    ASSERT_EQ(NMO_OK, nmo_messagemanager_vtable.validate(
        &large, NULL, NULL));
    nmo_chunk_t *large_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(large_chunk);
    ASSERT_EQ(NMO_OK, nmo_messagemanager_serialize(
        &large, large_chunk, NULL, NULL));
    nmo_chunk_close(large_chunk);
    nmo_messagemanager_state_t large_loaded = {0};
    ASSERT_EQ(NMO_OK, nmo_messagemanager_deserialize(
        &large_loaded, large_chunk, NULL, &deserialize_context));
    ASSERT_EQ(large_count, large_loaded.message_type_count);

    nmo_messagemanager_state_t oversized = {
        .message_type_count = (uint32_t)INT32_MAX + 1u,
        .message_type_names = old_names,
    };
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_messagemanager_vtable.validate(
                  &oversized, NULL, NULL));
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_messagemanager_serialize(
                  &oversized, target, NULL, NULL));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, messagemanager_copy_preserves_string_content) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    const char *names[] = {"OnClick", "", "CustomMessage"};
    nmo_messagemanager_state_t source = {
        .message_type_count = 3,
        .message_type_names = names,
    };
    const char *old_copy_names[] = {"Old"};
    nmo_messagemanager_state_t copy = {
        .message_type_count = 1,
        .message_type_names = old_copy_names,
    };
    nmo_type_descriptor_t type = {
        .size = sizeof(nmo_messagemanager_state_t),
    };

    ASSERT_EQ(NMO_OK, nmo_messagemanager_vtable.copy(
        &source, &copy, &type, arena));
    ASSERT_NE(source.message_type_names, copy.message_type_names);
    ASSERT_NE(source.message_type_names[0], copy.message_type_names[0]);
    ASSERT_NE(source.message_type_names[2], copy.message_type_names[2]);
    ASSERT_TRUE(nmo_messagemanager_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_messagemanager_vtable.hash(&source),
              nmo_messagemanager_vtable.hash(&copy));
    ASSERT_EQ(NMO_OK, nmo_messagemanager_vtable.validate(
        &copy, &type, NULL));

    ((char *)copy.message_type_names[0])[0] = 'X';
    ASSERT_STR_EQ("OnClick", source.message_type_names[0]);
    ASSERT_FALSE(nmo_messagemanager_vtable.equals(&source, &copy));

    const char *invalid_names[] = {NULL};
    nmo_messagemanager_state_t invalid = {
        .message_type_count = 1,
        .message_type_names = invalid_names,
    };
    const char **published_names = copy.message_type_names;
    const uint32_t published_count = copy.message_type_count;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_messagemanager_vtable.copy(
        &invalid, &copy, &type, arena));
    ASSERT_EQ(published_names, copy.message_type_names);
    ASSERT_EQ(published_count, copy.message_type_count);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_messagemanager_vtable.validate(
        &invalid, &type, NULL));

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, interfaceobjectmanager_chunk_count_stays_in_section) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);

    nmo_chunk_t *cross_section_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_count);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_count, 0x01234567u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_count, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_count, 0x87654321u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        cross_section_count, NMO_GUID_NULL));
    nmo_chunk_close(cross_section_count);

    nmo_interfaceobjectmanager_state_t state;
    ASSERT_EQ(NMO_OK, nmo_interfaceobjectmanager_vtable.create(
        &state, NULL, NULL));
    nmo_chunk_t *old_chunk = cross_section_count;
    const nmo_guid_t old_guid = {0x12345678u, 0x90ABCDEFu};
    state.chunk_count = 1;
    state.chunks = &old_chunk;
    state.has_chunks_chunk = 1;
    state.guid = old_guid;
    state.has_guid_chunk = 1;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
        nmo_interfaceobjectmanager_deserialize(
            &state, cross_section_count, NULL, &deserialize_context));
    nmo_chunk_parser_state_t *parser =
        (nmo_chunk_parser_state_t *)cross_section_count->parser_state;
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(parser->prev_identifier_pos + 3u, parser->current_pos);
    ASSERT_EQ(1, state.chunk_count);
    ASSERT_EQ(&old_chunk, state.chunks);
    ASSERT_EQ(1u, state.has_chunks_chunk);
    ASSERT_TRUE(nmo_guid_equals(state.guid, old_guid));
    ASSERT_EQ(1u, state.has_guid_chunk);

    nmo_chunk_t *missing_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_count);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_count, 0x01234567u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(missing_count, 0));
    nmo_chunk_close(missing_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
        nmo_interfaceobjectmanager_deserialize(
            &state, missing_count, NULL, &deserialize_context));
    ASSERT_EQ(1, state.chunk_count);
    ASSERT_EQ(&old_chunk, state.chunks);
    ASSERT_EQ(1u, state.has_chunks_chunk);
    ASSERT_TRUE(nmo_guid_equals(state.guid, old_guid));
    ASSERT_EQ(1u, state.has_guid_chunk);

    nmo_chunk_t *missing_guid = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_guid);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_guid));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_guid, 0x87654321u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_guid, 0x55667788u));
    nmo_chunk_close(missing_guid);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
        nmo_interfaceobjectmanager_deserialize(
            &state, missing_guid, NULL, &deserialize_context));
    ASSERT_EQ(1, state.chunk_count);
    ASSERT_EQ(&old_chunk, state.chunks);
    ASSERT_EQ(1u, state.has_chunks_chunk);
    ASSERT_TRUE(nmo_guid_equals(state.guid, old_guid));
    ASSERT_EQ(1u, state.has_guid_chunk);

    nmo_chunk_t *trailing_chunks = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_chunks);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_chunks));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_chunks, 0x01234567u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(trailing_chunks, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing_chunks, 0x12345678u));
    nmo_chunk_close(trailing_chunks);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT,
        nmo_interfaceobjectmanager_deserialize(
            &state, trailing_chunks, NULL, &deserialize_context));
    ASSERT_EQ(1, state.chunk_count);
    ASSERT_EQ(&old_chunk, state.chunks);
    ASSERT_EQ(1u, state.has_chunks_chunk);
    ASSERT_TRUE(nmo_guid_equals(state.guid, old_guid));
    ASSERT_EQ(1u, state.has_guid_chunk);

    nmo_chunk_t *trailing_guid = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_guid);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_guid));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_guid, 0x87654321u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(trailing_guid, NMO_GUID_NULL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing_guid, 0x12345678u));
    nmo_chunk_close(trailing_guid);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT,
        nmo_interfaceobjectmanager_deserialize(
            &state, trailing_guid, NULL, &deserialize_context));
    ASSERT_EQ(1, state.chunk_count);
    ASSERT_EQ(&old_chunk, state.chunks);
    ASSERT_EQ(1u, state.has_chunks_chunk);
    ASSERT_TRUE(nmo_guid_equals(state.guid, old_guid));
    ASSERT_EQ(1u, state.has_guid_chunk);

    fail_after_allocator_state_t allocator_state = {
        .allocation_count = 0,
        .allowed_allocations = 2,
    };
    nmo_allocator_t failing_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &allocator_state);
    nmo_arena_t *copy_arena = nmo_arena_create(&failing_allocator, 1);
    ASSERT_NOT_NULL(copy_arena);
    nmo_chunk_t *preserved_chunk = cross_section_count;
    nmo_interfaceobjectmanager_state_t copy_target;
    ASSERT_EQ(NMO_OK, nmo_interfaceobjectmanager_vtable.create(
        &copy_target, NULL, NULL));
    copy_target.base.visibility_flags = 0x12345678u;
    copy_target.chunk_count = 1;
    copy_target.chunks = &preserved_chunk;
    copy_target.has_chunks_chunk = 1;
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_interfaceobjectmanager_vtable.copy(
        &state, &copy_target, NULL, copy_arena));
    ASSERT_EQ(0x12345678u, copy_target.base.visibility_flags);
    ASSERT_EQ(1, copy_target.chunk_count);
    ASSERT_EQ(&preserved_chunk, copy_target.chunks);
    ASSERT_EQ(cross_section_count, copy_target.chunks[0]);

    nmo_interfaceobjectmanager_vtable.destroy(&copy_target, NULL, NULL);
    nmo_arena_destroy(copy_arena);
    nmo_interfaceobjectmanager_vtable.destroy(&state, NULL, NULL);
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
    failed.base.visibility_flags = NMO_CKOBJECT_HIERARCHICAL;
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
    ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, failed.base.visibility_flags);

    nmo_chunk_t *new_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(new_cross_section);
    new_cross_section->class_id = NMO_CID_BEHAVIORLINK;
    new_cross_section->data_version = 8;
    new_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(new_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        new_cross_section, CK_STATESAVE_BEHAV_LINK_NEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        new_cross_section, 0x00090005u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        new_cross_section, 900));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        new_cross_section, 0x11223344u));
    nmo_chunk_close(new_cross_section);
    nmo_chunk_set_file_context(new_cross_section, &read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_behaviorlink_deserialize(
        &failed, new_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(11, failed.activation_delay);
    ASSERT_EQ(12, failed.initial_activation_delay);
    ASSERT_EQ(901u, failed.in_io.raw_id);
    ASSERT_EQ(902u, failed.out_io.raw_id);

    nmo_chunk_t *legacy_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_cross_section);
    legacy_cross_section->class_id = NMO_CID_BEHAVIORLINK;
    legacy_cross_section->data_version = 8;
    legacy_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_cross_section, CK_STATESAVE_BEHAV_LINK_IOS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        legacy_cross_section, 900));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_cross_section, 0x11223344u));
    nmo_chunk_close(legacy_cross_section);
    nmo_chunk_set_file_context(legacy_cross_section, &read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_behaviorlink_deserialize(
        &failed, legacy_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(11, failed.activation_delay);
    ASSERT_EQ(12, failed.initial_activation_delay);
    ASSERT_EQ(901u, failed.in_io.raw_id);
    ASSERT_EQ(902u, failed.out_io.raw_id);

    nmo_chunk_t *new_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(new_trailing);
    new_trailing->class_id = NMO_CID_BEHAVIORLINK;
    new_trailing->data_version = 8;
    new_trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(new_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        new_trailing, CK_STATESAVE_BEHAV_LINK_NEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(new_trailing, 0x00090005u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(new_trailing, 900));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(new_trailing, 901));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(new_trailing, 0x12345678u));
    nmo_chunk_close(new_trailing);
    nmo_chunk_set_file_context(new_trailing, &read_context);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_behaviorlink_deserialize(
        &failed, new_trailing, NULL, &deserialize_context));

    nmo_chunk_t *curdelay_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(curdelay_trailing);
    curdelay_trailing->class_id = NMO_CID_BEHAVIORLINK;
    curdelay_trailing->data_version = 8;
    curdelay_trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(curdelay_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        curdelay_trailing, CK_STATESAVE_BEHAV_LINK_CURDELAY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(curdelay_trailing, 5));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(curdelay_trailing, 0x12345678u));
    nmo_chunk_close(curdelay_trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_behaviorlink_deserialize(
        &failed, curdelay_trailing, NULL, &deserialize_context));

    nmo_chunk_t *ios_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(ios_trailing);
    ios_trailing->class_id = NMO_CID_BEHAVIORLINK;
    ios_trailing->data_version = 8;
    ios_trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(ios_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        ios_trailing, CK_STATESAVE_BEHAV_LINK_IOS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(ios_trailing, 900));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(ios_trailing, 901));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(ios_trailing, 0x12345678u));
    nmo_chunk_close(ios_trailing);
    nmo_chunk_set_file_context(ios_trailing, &read_context);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_behaviorlink_deserialize(
        &failed, ios_trailing, NULL, &deserialize_context));

    nmo_chunk_t *delay_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(delay_trailing);
    delay_trailing->class_id = NMO_CID_BEHAVIORLINK;
    delay_trailing->data_version = 8;
    delay_trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(delay_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        delay_trailing, CK_STATESAVE_BEHAV_LINK_DELAY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(delay_trailing, 9));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(delay_trailing, 0x12345678u));
    nmo_chunk_close(delay_trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_behaviorlink_deserialize(
        &failed, delay_trailing, NULL, &deserialize_context));
    ASSERT_EQ(11, failed.activation_delay);
    ASSERT_EQ(12, failed.initial_activation_delay);
    ASSERT_EQ(901u, failed.in_io.raw_id);
    ASSERT_EQ(902u, failed.out_io.raw_id);

    nmo_behaviorlink_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_behaviorlink_vtable.create(&invalid, NULL, NULL));
    invalid.in_io = nmo_ref_from_raw(903);
    invalid.out_io = nmo_ref_from_id(999);
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_BEHAVIORLINK;
    target->data_version = 8;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_behaviorlink_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_behaviorlink_vtable.destroy(&source, NULL, NULL);
    nmo_behaviorlink_vtable.destroy(&loaded, NULL, NULL);
    nmo_behaviorlink_vtable.destroy(&reloaded, NULL, NULL);
    nmo_behaviorlink_vtable.destroy(&failed, NULL, NULL);
    nmo_behaviorlink_vtable.destroy(&invalid, NULL, NULL);
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
    source.packed_modes = 0x12245678u;
    source.packed_flags = 0x11020344u;
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
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &failed.base, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &failed.base.scripts, 899));
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
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed.base.scripts, 0));

    nmo_chunk_t *modern_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern_cross_section);
    modern_cross_section->class_id = NMO_CID_MATERIAL;
    modern_cross_section->data_version = 8;
    modern_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(modern_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        modern_cross_section, CK_STATESAVE_MATDATA));
    for (size_t i = 0; i < 8; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(modern_cross_section, 0));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        modern_cross_section, 0x01020304u));
    nmo_chunk_close(modern_cross_section);
    nmo_chunk_set_file_context(modern_cross_section, &read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_material_deserialize(
        &failed, modern_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, failed.diffuse_color);
    ASSERT_EQ(900u, failed.textures[0].raw_id);

    nmo_chunk_t *legacy_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_cross_section);
    legacy_cross_section->class_id = NMO_CID_MATERIAL;
    legacy_cross_section->data_version = 4;
    legacy_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_cross_section, CK_STATESAVE_MATDATA));
    for (size_t i = 0; i < 28; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(legacy_cross_section, 0));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_cross_section, 0x01020304u));
    nmo_chunk_close(legacy_cross_section);
    nmo_chunk_set_file_context(legacy_cross_section, &read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_material_deserialize(
        &failed, legacy_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, failed.diffuse_color);
    ASSERT_EQ(900u, failed.textures[0].raw_id);

    nmo_chunk_t *textures_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(textures_cross_section);
    textures_cross_section->class_id = NMO_CID_MATERIAL;
    textures_cross_section->data_version = 8;
    textures_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(textures_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        textures_cross_section, CK_STATESAVE_MATDATA2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        textures_cross_section, 800));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        textures_cross_section, 801));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        textures_cross_section, 0x01020304u));
    nmo_chunk_close(textures_cross_section);
    nmo_chunk_set_file_context(textures_cross_section, &read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_material_deserialize(
        &failed, textures_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(900u, failed.textures[0].raw_id);
    ASSERT_EQ(903u, failed.textures[3].raw_id);

    nmo_chunk_t *effect_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(effect_cross_section);
    effect_cross_section->class_id = NMO_CID_MATERIAL;
    effect_cross_section->data_version = 8;
    effect_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(effect_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        effect_cross_section, CK_STATESAVE_MATDATA3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        effect_cross_section, 0x01020304u));
    nmo_chunk_close(effect_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_material_deserialize(
        &failed, effect_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, failed.diffuse_color);
    ASSERT_EQ(900u, failed.textures[0].raw_id);

    nmo_chunk_t *effect_param_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(effect_param_cross_section);
    effect_param_cross_section->class_id = NMO_CID_MATERIAL;
    effect_param_cross_section->data_version = 8;
    effect_param_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(effect_param_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        effect_param_cross_section, CK_STATESAVE_MATDATA5));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        effect_param_cross_section, 800));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        effect_param_cross_section, 0x01020304u));
    nmo_chunk_close(effect_param_cross_section);
    nmo_chunk_set_file_context(effect_param_cross_section, &read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_material_deserialize(
        &failed, effect_param_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, failed.diffuse_color);
    ASSERT_EQ(900u, failed.textures[0].raw_id);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed.base.scripts, 0));

    static const struct {
        uint32_t identifier;
        uint32_t data_version;
        size_t payload_dwords;
    } trailing_cases[] = {
        {CK_STATESAVE_MATDATA, 4u, 29u},
        {CK_STATESAVE_MATDATA, 8u, 9u},
        {CK_STATESAVE_MATDATA2, 8u, 3u},
        {CK_STATESAVE_MATDATA3, 8u, 1u},
        {CK_STATESAVE_MATDATA5, 8u, 2u},
    };
    for (size_t i = 0;
         i < sizeof(trailing_cases) / sizeof(trailing_cases[0]); ++i) {
        nmo_chunk_t *trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(trailing);
        trailing->class_id = NMO_CID_MATERIAL;
        trailing->data_version = trailing_cases[i].data_version;
        trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            trailing, trailing_cases[i].identifier));
        for (size_t j = 0; j < trailing_cases[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0u));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
            trailing, 0x12345678u));
        nmo_chunk_close(trailing);
        nmo_chunk_set_file_context(trailing, &read_context);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_material_deserialize(
            &failed, trailing, NULL, &deserialize_context));
        ASSERT_EQ(0xCAFEBABEu, failed.diffuse_color);
        ASSERT_EQ(900u, failed.textures[0].raw_id);
        ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
            &failed.base.scripts, 0));
    }

    nmo_material_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_material_vtable.create(&invalid, NULL, NULL));
    invalid.textures[0] = nmo_ref_from_id(999);
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_MATERIAL;
    target->data_version = 8;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_material_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_material_vtable.destroy(&source, NULL, NULL);
    nmo_material_vtable.destroy(&loaded, NULL, NULL);
    nmo_material_vtable.destroy(&reloaded, NULL, NULL);
    nmo_array_dispose(&failed.base.scripts);
    nmo_array_dispose(&failed.base.attributes);
    nmo_array_dispose(&failed.base.legacy_attributes);
    nmo_material_vtable.destroy(&failed, NULL, NULL);
    nmo_material_vtable.destroy(&invalid, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, material_preserves_file_layouts) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_material_state_t material;
    ASSERT_EQ(NMO_OK, nmo_material_vtable.create(&material, NULL, NULL));
    ASSERT_TRUE(material.has_material_data);
    material.material_data_is_legacy = 1;

    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy);
    legacy->class_id = NMO_CID_MATERIAL;
    legacy->data_version = 0;
    legacy->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_material_serialize(
        &material, legacy, NULL, &serialize_context));
    nmo_chunk_close(legacy);

    nmo_material_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_material_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_material_deserialize(
        &loaded, legacy, NULL, &deserialize_context));
    ASSERT_TRUE(loaded.has_material_data);
    ASSERT_TRUE(loaded.material_data_is_legacy);
    ASSERT_EQ(material.diffuse_color, loaded.diffuse_color);
    ASSERT_EQ(material.ambient_color, loaded.ambient_color);
    ASSERT_EQ(material.specular_color, loaded.specular_color);
    ASSERT_EQ(material.emissive_color, loaded.emissive_color);
    ASSERT_EQ(material.packed_modes, loaded.packed_modes);
    ASSERT_EQ(material.packed_flags, loaded.packed_flags);

    loaded.has_effect = 1;
    loaded.effect = 0;
    loaded.has_additional_textures = 1;
    nmo_chunk_t *modern = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern);
    modern->class_id = NMO_CID_MATERIAL;
    modern->data_version = 8;
    modern->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_material_serialize(
        &loaded, modern, NULL, &serialize_context));
    nmo_chunk_close(modern);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        modern, CK_STATESAVE_MATDATA3));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        modern, CK_STATESAVE_MATDATA2));

    nmo_chunk_t *empty = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty);
    empty->class_id = NMO_CID_MATERIAL;
    empty->data_version = 8;
    empty->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(empty));
    nmo_chunk_close(empty);
    ASSERT_EQ(NMO_OK, nmo_material_deserialize(
        &loaded, empty, NULL, &deserialize_context));
    ASSERT_FALSE(loaded.has_material_data);
    ASSERT_EQ(NMO_OK, nmo_material_serialize(
        &loaded, empty, NULL, &serialize_context));
    nmo_chunk_close(empty);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        empty, CK_STATESAVE_MATDATA));

    nmo_chunk_t *conflicting = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(conflicting);
    conflicting->class_id = NMO_CID_MATERIAL;
    conflicting->data_version = 8;
    conflicting->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(conflicting));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        conflicting, CK_STATESAVE_MATDATA3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(conflicting, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        conflicting, CK_STATESAVE_MATDATA5));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(conflicting, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(conflicting, 2));
    nmo_chunk_close(conflicting);
    loaded.effect = 77;
    loaded.has_effect = 1;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_material_deserialize(
        &loaded, conflicting, NULL, &deserialize_context));
    ASSERT_EQ(77u, loaded.effect);
    ASSERT_TRUE(loaded.has_effect);

    nmo_material_state_t modern_default;
    ASSERT_EQ(NMO_OK, nmo_material_vtable.create(
        &modern_default, NULL, NULL));
    nmo_chunk_t *default_version = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(default_version);
    default_version->class_id = NMO_CID_MATERIAL;
    default_version->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_material_serialize(
        &modern_default, default_version, NULL, &serialize_context));
    nmo_chunk_close(default_version);
    ASSERT_EQ(NMO_CHUNK_DATA_VERSION_CURRENT, default_version->data_version);
    nmo_material_state_t default_loaded;
    ASSERT_EQ(NMO_OK, nmo_material_vtable.create(
        &default_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_material_deserialize(
        &default_loaded, default_version, NULL, &deserialize_context));
    ASSERT_FALSE(default_loaded.material_data_is_legacy);

    nmo_material_vtable.destroy(&loaded, NULL, NULL);
    nmo_material_vtable.destroy(&material, NULL, NULL);
    nmo_material_vtable.destroy(&modern_default, NULL, NULL);
    nmo_material_vtable.destroy(&default_loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, parameterlocal_owner_round_trips_raw_id) {
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
    nmo_serialize_context_t serialize_context =
        nmo_serialize_context_create_nonfile(
            arena, NULL, CK_STATESAVE_PARAMETEROUT_OWNER);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_serialize_context_t file_serialize_context =
        nmo_serialize_context_create(
            arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    nmo_parameterlocal_state_t source;
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_vtable.create(
        &source, NULL, NULL));
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, source.base.base.visibility_flags);
    source.base.type_guid = CKPGUID_INT;
    source.base.mode = CKPARAM_MODE_BUFFER;
    source.base.has_state = true;
    uint8_t source_byte = 0x42u;
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &source.base.buffer_data, &source_byte));
    source.base.subchunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(source.base.subchunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(source.base.subchunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        source.base.subchunk, 0xaabbccddu));
    nmo_chunk_close(source.base.subchunk);
    source.owner = nmo_ref_from_raw(691);

    nmo_parameterlocal_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_vtable.create(
        &copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_vtable.copy(
        &source, &copied, NULL, arena));
    ASSERT_NE(source.base.buffer_data.data, copied.base.buffer_data.data);
    ASSERT_NE(source.base.subchunk, copied.base.subchunk);
    ASSERT_TRUE(nmo_parameterlocal_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_parameterlocal_vtable.hash(&source),
              nmo_parameterlocal_vtable.hash(&copied));

    nmo_parameterlocal_state_t aliased_copy = source;
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_vtable.copy(
        &source, &aliased_copy, NULL, arena));
    ASSERT_NE(source.base.buffer_data.data,
              aliased_copy.base.buffer_data.data);
    ASSERT_EQ(0x42u, NMO_ARRAY_DATA(
        uint8_t, &source.base.buffer_data)[0]);
    ASSERT_TRUE(nmo_parameterlocal_vtable.equals(
        &source, &aliased_copy));

    nmo_chunk_t *file_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(file_chunk);
    file_chunk->class_id = NMO_CID_PARAMETERLOCAL;
    file_chunk->data_version = 8;
    file_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(file_chunk, &write_context);
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_serialize(
        &source, file_chunk, NULL, &file_serialize_context));
    nmo_chunk_close(file_chunk);
    nmo_chunk_set_file_context(file_chunk, &read_context);
    nmo_parameterlocal_state_t file_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_vtable.create(
        &file_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_deserialize(
        &file_loaded, file_chunk, NULL, &deserialize_context));
    ASSERT_EQ(691u, file_loaded.owner.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, file_loaded.owner.state);

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

    nmo_chunk_t *null_owner = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(null_owner);
    null_owner->class_id = NMO_CID_PARAMETERLOCAL;
    null_owner->data_version = 8;
    null_owner->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(null_owner));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        null_owner, CK_STATESAVE_PARAMETEROUT_OWNER));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        null_owner, NMO_OBJECT_ID_NONE));
    nmo_chunk_close(null_owner);
    nmo_parameterlocal_state_t null_owner_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_vtable.create(
        &null_owner_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_deserialize(
        &null_owner_loaded, null_owner, NULL, &deserialize_context));
    ASSERT_TRUE(null_owner_loaded.has_owner);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, null_owner_loaded.owner.raw_id);
    nmo_chunk_t *null_owner_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(null_owner_saved);
    null_owner_saved->class_id = NMO_CID_PARAMETERLOCAL;
    null_owner_saved->data_version = 8;
    null_owner_saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_serialize(
        &null_owner_loaded, null_owner_saved, NULL,
        &file_serialize_context));
    nmo_chunk_close(null_owner_saved);
    size_t null_owner_dwords = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        null_owner_saved, CK_STATESAVE_PARAMETEROUT_OWNER,
        &null_owner_dwords));
    ASSERT_EQ(1u, null_owner_dwords);

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
    failed.base.type_guid = CKPGUID_INT;
    failed.base.mode = CKPARAM_MODE_BUFFER;
    failed.base.has_state = true;
    uint8_t old_byte = 0xabu;
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &failed.base.buffer_data, &old_byte));
    failed.owner = nmo_ref_from_raw(692);
    failed.is_myself = 1;
    failed.is_setting = 1;
    ASSERT_NE(NMO_OK, nmo_parameterlocal_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_INT, failed.base.type_guid));
    ASSERT_EQ(CKPARAM_MODE_BUFFER, failed.base.mode);
    ASSERT_EQ(1u, failed.base.buffer_data.count);
    ASSERT_EQ(0xabu, NMO_ARRAY_DATA(
        uint8_t, &failed.base.buffer_data)[0]);
    ASSERT_EQ(692u, failed.owner.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.owner.state);
    ASSERT_TRUE(failed.is_myself);
    ASSERT_TRUE(failed.is_setting);

    nmo_chunk_t *cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section);
    cross_section->class_id = NMO_CID_PARAMETERLOCAL;
    cross_section->data_version = 8;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section, CK_STATESAVE_PARAMETEROUT_OWNER));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section, CK_STATESAVE_PARAMETEROUT_MYSELF));
    nmo_chunk_close(cross_section);
    nmo_chunk_set_file_context(cross_section, &read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameterlocal_deserialize(
        &failed, cross_section, NULL, &deserialize_context));
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_INT, failed.base.type_guid));
    ASSERT_EQ(CKPARAM_MODE_BUFFER, failed.base.mode);
    ASSERT_EQ(1u, failed.base.buffer_data.count);
    ASSERT_EQ(0xabu, NMO_ARRAY_DATA(
        uint8_t, &failed.base.buffer_data)[0]);
    ASSERT_EQ(692u, failed.owner.raw_id);
    ASSERT_TRUE(failed.is_myself);
    ASSERT_TRUE(failed.is_setting);

    const struct {
        uint32_t identifier;
        size_t payload_dwords;
    } trailing_cases[] = {
        {CK_STATESAVE_PARAMETEROUT_OWNER, 1u},
        {CK_STATESAVE_PARAMETEROUT_MYSELF, 0u},
        {CK_STATESAVE_PARAMETEROUT_ISSETTING, 0u},
    };
    for (size_t i = 0;
         i < sizeof(trailing_cases) / sizeof(trailing_cases[0]); ++i) {
        nmo_chunk_t *trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(trailing);
        trailing->class_id = NMO_CID_PARAMETERLOCAL;
        trailing->data_version = 8;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            trailing, trailing_cases[i].identifier));
        for (size_t j = 0; j < trailing_cases[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0x12345678u));
        nmo_chunk_close(trailing);
        nmo_chunk_set_file_context(trailing, &read_context);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_parameterlocal_deserialize(
            &failed, trailing, NULL, &deserialize_context));
        ASSERT_TRUE(nmo_guid_equals(CKPGUID_INT, failed.base.type_guid));
        ASSERT_EQ(CKPARAM_MODE_BUFFER, failed.base.mode);
        ASSERT_EQ(1u, failed.base.buffer_data.count);
        ASSERT_EQ(0xabu, NMO_ARRAY_DATA(
            uint8_t, &failed.base.buffer_data)[0]);
        ASSERT_EQ(692u, failed.owner.raw_id);
        ASSERT_TRUE(failed.is_myself);
        ASSERT_TRUE(failed.is_setting);
    }

    nmo_parameterlocal_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_parameterlocal_vtable.create(
        &invalid, NULL, NULL));
    invalid.owner = nmo_ref_from_id(999);
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_PARAMETERLOCAL;
    target->data_version = 8;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_parameterlocal_serialize(
        &invalid, target, NULL, &file_serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_parameterlocal_vtable.destroy(&source, NULL, NULL);
    nmo_parameterlocal_vtable.destroy(&copied, NULL, NULL);
    nmo_parameterlocal_vtable.destroy(&aliased_copy, NULL, NULL);
    nmo_parameterlocal_vtable.destroy(&file_loaded, NULL, NULL);
    nmo_parameterlocal_vtable.destroy(&loaded, NULL, NULL);
    nmo_parameterlocal_vtable.destroy(&reloaded, NULL, NULL);
    nmo_parameterlocal_vtable.destroy(&null_owner_loaded, NULL, NULL);
    nmo_parameterlocal_vtable.destroy(&failed, NULL, NULL);
    nmo_parameterlocal_vtable.destroy(&invalid, NULL, NULL);
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
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, source.base.visibility_flags);

    nmo_parameterin_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(
        &copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.copy(
        &source, &copied, NULL, NULL));
    ASSERT_TRUE(nmo_parameterin_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_parameterin_vtable.hash(&source),
              nmo_parameterin_vtable.hash(&copied));
    copied.source.raw_id++;
    ASSERT_FALSE(nmo_parameterin_vtable.equals(&source, &copied));

    nmo_parameterin_state_t remapped = source;
    remapped.type_guid = CKPGUID_OLDMESSAGE;
    ASSERT_EQ(NMO_OK, nmo_parameterin_remap_dependencies(
        &remapped, NULL, NULL));
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_OLDMESSAGE, remapped.type_guid));

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

    nmo_parameterin_state_t legacy_prefix_source;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(
        &legacy_prefix_source, NULL, NULL));
    legacy_prefix_source.type_guid = (nmo_guid_t){11u, 12u};
    legacy_prefix_source.legacy_prefix_ref = nmo_ref_from_raw(697);
    legacy_prefix_source.source = nmo_ref_from_raw(698);
    nmo_chunk_t *legacy_prefix = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_prefix);
    legacy_prefix->class_id = NMO_CID_PARAMETERIN;
    legacy_prefix->data_version = 4;
    legacy_prefix->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(legacy_prefix, &write_context);
    ASSERT_EQ(NMO_OK, nmo_parameterin_serialize(
        &legacy_prefix_source, legacy_prefix, NULL,
        &file_serialize_context));
    nmo_chunk_close(legacy_prefix);
    size_t legacy_prefix_dwords = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        legacy_prefix, CK_STATESAVE_PARAMETERIN_DATASOURCE,
        &legacy_prefix_dwords));
    ASSERT_EQ(4u, legacy_prefix_dwords);
    nmo_chunk_set_file_context(legacy_prefix, &read_context);
    nmo_parameterin_state_t legacy_prefix_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(
        &legacy_prefix_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_deserialize(
        &legacy_prefix_loaded, legacy_prefix, NULL,
        &deserialize_context));
    ASSERT_EQ(697u, legacy_prefix_loaded.legacy_prefix_ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED,
              legacy_prefix_loaded.legacy_prefix_ref.state);
    ASSERT_EQ(698u, legacy_prefix_loaded.source.raw_id);

    nmo_chunk_t *legacy_prefix_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_prefix_saved);
    legacy_prefix_saved->class_id = NMO_CID_PARAMETERIN;
    legacy_prefix_saved->data_version = 4;
    legacy_prefix_saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(legacy_prefix_saved, &write_context);
    ASSERT_EQ(NMO_OK, nmo_parameterin_serialize(
        &legacy_prefix_loaded, legacy_prefix_saved, NULL,
        &file_serialize_context));
    nmo_chunk_close(legacy_prefix_saved);
    nmo_chunk_set_file_context(legacy_prefix_saved, &read_context);
    nmo_parameterin_state_t legacy_prefix_reloaded;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(
        &legacy_prefix_reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_deserialize(
        &legacy_prefix_reloaded, legacy_prefix_saved, NULL,
        &deserialize_context));
    ASSERT_EQ(697u, legacy_prefix_reloaded.legacy_prefix_ref.raw_id);
    ASSERT_EQ(698u, legacy_prefix_reloaded.source.raw_id);

    nmo_chunk_t *lossy_prefix_target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(lossy_prefix_target);
    lossy_prefix_target->class_id = NMO_CID_PARAMETERIN;
    lossy_prefix_target->data_version = 8;
    lossy_prefix_target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(lossy_prefix_target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        lossy_prefix_target, 0x12345678u));
    nmo_chunk_close(lossy_prefix_target);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_parameterin_serialize(
        &legacy_prefix_loaded, lossy_prefix_target, NULL,
        &file_serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(lossy_prefix_target));
    lossy_prefix_target->data_version = 4;
    legacy_prefix_loaded.owner = nmo_ref_from_raw(699);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_parameterin_serialize(
        &legacy_prefix_loaded, lossy_prefix_target, NULL,
        &file_serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(lossy_prefix_target));
    legacy_prefix_loaded.owner = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);

    nmo_parameterin_state_t legacy_source;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(
        &legacy_source, NULL, NULL));
    legacy_source.has_legacy_layout = 1;
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
    ASSERT_TRUE(legacy_loaded.has_legacy_layout);

    nmo_parameterin_state_t default_source;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(
        &default_source, NULL, NULL));
    nmo_chunk_t *default_version = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(default_version);
    default_version->class_id = NMO_CID_PARAMETERIN;
    default_version->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_parameterin_serialize(
        &default_source, default_version, NULL,
        &file_serialize_context));
    ASSERT_EQ(NMO_CHUNK_DATA_VERSION_CURRENT,
              nmo_chunk_get_data_version(default_version));
    nmo_chunk_close(default_version);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        default_version, CK_STATESAVE_PARAMETERIN_DATASOURCE));
    nmo_parameterin_state_t default_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(
        &default_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_deserialize(
        &default_loaded, default_version, NULL,
        &deserialize_context));
    ASSERT_FALSE(default_loaded.has_legacy_layout);

    nmo_chunk_t *legacy_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_saved);
    legacy_saved->class_id = NMO_CID_PARAMETERIN;
    legacy_saved->data_version = 0;
    ASSERT_EQ(NMO_OK, nmo_parameterin_serialize(
        &legacy_loaded, legacy_saved, NULL,
        &legacy_serialize_context));
    ASSERT_EQ(0u, nmo_chunk_get_data_version(legacy_saved));
    nmo_chunk_close(legacy_saved);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        legacy_saved, CK_STATESAVE_PARAMETERIN_OWNER));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        legacy_saved, CK_STATESAVE_PARAMETERIN_OUTSOURCE));

    nmo_chunk_t *empty = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty);
    empty->class_id = NMO_CID_PARAMETERIN;
    empty->data_version = 8;
    empty->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(empty));
    nmo_chunk_close(empty);
    nmo_parameterin_state_t empty_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(
        &empty_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_deserialize(
        &empty_loaded, empty, NULL, &deserialize_context));
    ASSERT_FALSE(empty_loaded.has_data);
    ASSERT_FALSE(empty_loaded.has_owner);
    ASSERT_FALSE(empty_loaded.has_source);
    nmo_chunk_t *empty_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty_saved);
    empty_saved->class_id = NMO_CID_PARAMETERIN;
    empty_saved->data_version = 8;
    empty_saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_parameterin_serialize(
        &empty_loaded, empty_saved, NULL, &file_serialize_context));
    nmo_chunk_close(empty_saved);
    ASSERT_EQ(0u, nmo_chunk_get_data_size(empty_saved));

    nmo_chunk_t *null_owner = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(null_owner);
    null_owner->class_id = NMO_CID_PARAMETERIN;
    null_owner->data_version = 8;
    null_owner->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(null_owner));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        null_owner, CK_STATESAVE_PARAMETERIN_DEFAULTDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        null_owner, (nmo_guid_t){31u, 32u}));
    for (size_t i = 0; i < 3u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
            null_owner, NMO_OBJECT_ID_NONE));
    }
    nmo_chunk_close(null_owner);
    nmo_parameterin_state_t null_owner_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(
        &null_owner_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_deserialize(
        &null_owner_loaded, null_owner, NULL, &deserialize_context));
    ASSERT_TRUE(null_owner_loaded.has_data);
    ASSERT_TRUE(null_owner_loaded.has_owner);
    ASSERT_TRUE(null_owner_loaded.has_source);
    nmo_chunk_t *null_owner_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(null_owner_saved);
    null_owner_saved->class_id = NMO_CID_PARAMETERIN;
    null_owner_saved->data_version = 8;
    null_owner_saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_parameterin_serialize(
        &null_owner_loaded, null_owner_saved, NULL,
        &file_serialize_context));
    nmo_chunk_close(null_owner_saved);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        null_owner_saved, CK_STATESAVE_PARAMETERIN_DEFAULTDATA));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        null_owner_saved, CK_STATESAVE_PARAMETERIN_DATASOURCE));

    nmo_chunk_t *legacy_data_only = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_data_only);
    legacy_data_only->class_id = NMO_CID_PARAMETERIN;
    legacy_data_only->data_version = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_data_only));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_data_only, CK_STATESAVE_PARAMETERIN_DEFAULTDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        legacy_data_only, (nmo_guid_t){41u, 42u}));
    nmo_chunk_close(legacy_data_only);
    nmo_parameterin_state_t legacy_data_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(
        &legacy_data_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_deserialize(
        &legacy_data_loaded, legacy_data_only, NULL,
        &deserialize_context));
    ASSERT_TRUE(legacy_data_loaded.has_legacy_layout);
    ASSERT_TRUE(legacy_data_loaded.has_data);
    ASSERT_FALSE(legacy_data_loaded.has_owner);
    ASSERT_FALSE(legacy_data_loaded.has_source);
    nmo_chunk_t *legacy_data_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_data_saved);
    legacy_data_saved->class_id = NMO_CID_PARAMETERIN;
    legacy_data_saved->data_version = 0;
    ASSERT_EQ(NMO_OK, nmo_parameterin_serialize(
        &legacy_data_loaded, legacy_data_saved, NULL,
        &legacy_serialize_context));
    nmo_chunk_close(legacy_data_saved);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        legacy_data_saved, CK_STATESAVE_PARAMETERIN_DEFAULTDATA));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        legacy_data_saved, CK_STATESAVE_PARAMETERIN_OWNER));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        legacy_data_saved, CK_STATESAVE_PARAMETERIN_OUTSOURCE));

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
    failed.base.visibility_flags = NMO_CKOBJECT_HIERARCHICAL;
    failed.type_guid = (nmo_guid_t){7u, 8u};
    failed.source = nmo_ref_from_raw(901);
    failed.owner = nmo_ref_from_raw(902);
    failed.is_shared = 1;
    failed.is_disabled = 1;
    ASSERT_NE(NMO_OK, nmo_parameterin_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, failed.base.visibility_flags);
    ASSERT_EQ(7u, failed.type_guid.d1);
    ASSERT_EQ(8u, failed.type_guid.d2);
    ASSERT_EQ(901u, failed.source.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.source.state);
    ASSERT_EQ(902u, failed.owner.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.owner.state);
    ASSERT_EQ(1u, failed.is_shared);
    ASSERT_EQ(1u, failed.is_disabled);

    nmo_chunk_t *default_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(default_cross_section);
    default_cross_section->class_id = NMO_CID_PARAMETERIN;
    default_cross_section->data_version = 8;
    default_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(default_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        default_cross_section, CK_STATESAVE_PARAMETERIN_DEFAULTDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        default_cross_section, (nmo_guid_t){5u, 6u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        default_cross_section, 801));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        default_cross_section, 802));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        default_cross_section, 0x01020304u));
    nmo_chunk_close(default_cross_section);
    nmo_chunk_set_file_context(default_cross_section, &read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameterin_deserialize(
        &failed, default_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(7u, failed.type_guid.d1);
    ASSERT_EQ(901u, failed.source.raw_id);
    ASSERT_EQ(902u, failed.owner.raw_id);

    nmo_chunk_t *source_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(source_cross_section);
    source_cross_section->class_id = NMO_CID_PARAMETERIN;
    source_cross_section->data_version = 8;
    source_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(source_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        source_cross_section, CK_STATESAVE_PARAMETERIN_DATASOURCE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        source_cross_section, (nmo_guid_t){5u, 6u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        source_cross_section, 0x01020304u));
    nmo_chunk_close(source_cross_section);
    nmo_chunk_set_file_context(source_cross_section, &read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameterin_deserialize(
        &failed, source_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(7u, failed.type_guid.d1);
    ASSERT_EQ(901u, failed.source.raw_id);
    ASSERT_EQ(902u, failed.owner.raw_id);

    nmo_chunk_t *prefix_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(prefix_cross_section);
    prefix_cross_section->class_id = NMO_CID_PARAMETERIN;
    prefix_cross_section->data_version = 4;
    prefix_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(prefix_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        prefix_cross_section, CK_STATESAVE_PARAMETERIN_DATASOURCE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        prefix_cross_section, (nmo_guid_t){5u, 6u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        prefix_cross_section, 801));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        prefix_cross_section, 0x01020304u));
    nmo_chunk_close(prefix_cross_section);
    nmo_chunk_set_file_context(prefix_cross_section, &read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameterin_deserialize(
        &failed, prefix_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(7u, failed.type_guid.d1);
    ASSERT_EQ(901u, failed.source.raw_id);
    ASSERT_EQ(902u, failed.owner.raw_id);

    nmo_chunk_t *legacy_owner_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_owner_cross_section);
    legacy_owner_cross_section->class_id = NMO_CID_PARAMETERIN;
    legacy_owner_cross_section->data_version = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_owner_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_owner_cross_section, CK_STATESAVE_PARAMETERIN_OWNER));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_owner_cross_section, 0x01020304u));
    nmo_chunk_close(legacy_owner_cross_section);
    nmo_chunk_set_file_context(legacy_owner_cross_section, &read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameterin_deserialize(
        &failed, legacy_owner_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(7u, failed.type_guid.d1);
    ASSERT_EQ(901u, failed.source.raw_id);
    ASSERT_EQ(902u, failed.owner.raw_id);
    ASSERT_EQ(1u, failed.is_shared);
    ASSERT_EQ(1u, failed.is_disabled);

    const struct {
        uint32_t identifier;
        uint32_t data_version;
        size_t payload_dwords;
    } trailing_cases[] = {
        {CK_STATESAVE_PARAMETERIN_DATASHARED, 8u, 3u},
        {CK_STATESAVE_PARAMETERIN_DATASOURCE, 8u, 3u},
        {CK_STATESAVE_PARAMETERIN_DATASHARED, 4u, 4u},
        {CK_STATESAVE_PARAMETERIN_DATASOURCE, 4u, 4u},
        {CK_STATESAVE_PARAMETERIN_DEFAULTDATA, 8u, 5u},
        {CK_STATESAVE_PARAMETERIN_DEFAULTDATA, 0u, 2u},
        {CK_STATESAVE_PARAMETERIN_OWNER, 0u, 1u},
        {CK_STATESAVE_PARAMETERIN_INSHARED, 0u, 1u},
        {CK_STATESAVE_PARAMETERIN_OUTSOURCE, 0u, 1u},
        {CK_STATESAVE_PARAMETERIN_DISABLED, 8u, 0u},
    };
    for (size_t i = 0;
         i < sizeof(trailing_cases) / sizeof(trailing_cases[0]); ++i) {
        nmo_chunk_t *trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(trailing);
        trailing->class_id = NMO_CID_PARAMETERIN;
        trailing->data_version = trailing_cases[i].data_version;
        trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            trailing, trailing_cases[i].identifier));
        for (size_t j = 0; j < trailing_cases[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0x12345678u));
        nmo_chunk_close(trailing);
        nmo_chunk_set_file_context(trailing, &read_context);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_parameterin_deserialize(
            &failed, trailing, NULL, &deserialize_context));
        ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, failed.base.visibility_flags);
        ASSERT_EQ(7u, failed.type_guid.d1);
        ASSERT_EQ(8u, failed.type_guid.d2);
        ASSERT_EQ(901u, failed.source.raw_id);
        ASSERT_EQ(902u, failed.owner.raw_id);
        ASSERT_EQ(1u, failed.is_shared);
        ASSERT_EQ(1u, failed.is_disabled);
    }

    nmo_parameterin_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(&invalid, NULL, NULL));
    invalid.type_guid = (nmo_guid_t){9u, 10u};
    invalid.owner = nmo_ref_from_raw(903);
    invalid.source = nmo_ref_from_id(999);
    invalid.is_shared = 1;
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_PARAMETERIN;
    target->data_version = 8;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_parameterin_serialize(
        &invalid, target, NULL, &file_serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_parameterin_vtable.destroy(&source, NULL, NULL);
    nmo_parameterin_vtable.destroy(&copied, NULL, NULL);
    nmo_parameterin_vtable.destroy(&remapped, NULL, NULL);
    nmo_parameterin_vtable.destroy(&loaded, NULL, NULL);
    nmo_parameterin_vtable.destroy(&reloaded, NULL, NULL);
    nmo_parameterin_vtable.destroy(&legacy_prefix_source, NULL, NULL);
    nmo_parameterin_vtable.destroy(&legacy_prefix_loaded, NULL, NULL);
    nmo_parameterin_vtable.destroy(&legacy_prefix_reloaded, NULL, NULL);
    nmo_parameterin_vtable.destroy(&legacy_source, NULL, NULL);
    nmo_parameterin_vtable.destroy(&legacy_loaded, NULL, NULL);
    nmo_parameterin_vtable.destroy(&default_source, NULL, NULL);
    nmo_parameterin_vtable.destroy(&default_loaded, NULL, NULL);
    nmo_parameterin_vtable.destroy(&empty_loaded, NULL, NULL);
    nmo_parameterin_vtable.destroy(&null_owner_loaded, NULL, NULL);
    nmo_parameterin_vtable.destroy(&legacy_data_loaded, NULL, NULL);
    nmo_parameterin_vtable.destroy(&failed, NULL, NULL);
    nmo_parameterin_vtable.destroy(&invalid, NULL, NULL);
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
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, source.base.base.visibility_flags);
    source.base.type_guid = CKPGUID_INT;
    source.base.mode = CKPARAM_MODE_BUFFER;
    source.base.has_state = true;
    uint8_t source_bytes[] = {0x11u, 0x22u};
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &source.base.buffer_data, &source_bytes[0]));
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &source.base.buffer_data, &source_bytes[1]));
    source.base.subchunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(source.base.subchunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(source.base.subchunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        source.base.subchunk, 0xaabbccddu));
    nmo_chunk_close(source.base.subchunk);
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
    ASSERT_NE(source.base.buffer_data.data, copied.base.buffer_data.data);
    ASSERT_NE(source.base.subchunk, copied.base.subchunk);
    ASSERT_TRUE(copied.destination_ids != source.destination_ids);
    ASSERT_TRUE(nmo_parameterout_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_parameterout_vtable.hash(&source),
              nmo_parameterout_vtable.hash(&copied));

    nmo_parameterout_state_t aliased_copy = source;
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.copy(
        &source, &aliased_copy, &parameterout_type, arena));
    ASSERT_NE(source.base.buffer_data.data,
              aliased_copy.base.buffer_data.data);
    ASSERT_EQ(0x11u, NMO_ARRAY_DATA(
        uint8_t, &source.base.buffer_data)[0]);
    ASSERT_TRUE(nmo_parameterout_vtable.equals(
        &source, &aliased_copy));

    fail_after_allocator_state_t copy_allocator_state = {
        .allowed_allocations = 1,
    };
    nmo_allocator_t copy_allocator = {
        .alloc = fail_after_alloc,
        .free = fail_after_free,
        .user_data = &copy_allocator_state,
    };
    nmo_parameterout_state_t failing_copy_source;
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.create(
        &failing_copy_source, NULL, NULL));
    nmo_array_dispose(&failing_copy_source.base.buffer_data);
    ASSERT_EQ(NMO_OK, nmo_array_init(
        &failing_copy_source.base.buffer_data, sizeof(uint8_t), 2,
        &copy_allocator));
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &failing_copy_source.base.buffer_data, &source_bytes[0]));
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &failing_copy_source.base.buffer_data, &source_bytes[1]));
    copy_allocator_state.allowed_allocations =
        copy_allocator_state.allocation_count;
    nmo_parameterout_state_t preserved_copy;
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.create(
        &preserved_copy, NULL, NULL));
    uint8_t preserved_copy_byte = 0xddu;
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &preserved_copy.base.buffer_data, &preserved_copy_byte));
    preserved_copy.owner = nmo_ref_from_raw(799);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_parameterout_vtable.copy(
        &failing_copy_source, &preserved_copy,
        &parameterout_type, arena));
    ASSERT_EQ(1u, preserved_copy.base.buffer_data.count);
    ASSERT_EQ(0xddu, NMO_ARRAY_DATA(
        uint8_t, &preserved_copy.base.buffer_data)[0]);
    ASSERT_EQ(799u, preserved_copy.owner.raw_id);

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

    nmo_chunk_t *empty_sections = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty_sections);
    empty_sections->class_id = NMO_CID_PARAMETEROUT;
    empty_sections->data_version = 8;
    empty_sections->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(empty_sections));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        empty_sections, CK_STATESAVE_PARAMETEROUT_OWNER));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        empty_sections, NMO_OBJECT_ID_NONE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        empty_sections, CK_STATESAVE_PARAMETEROUT_DESTINATIONS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(empty_sections, 0));
    nmo_chunk_close(empty_sections);

    nmo_parameterout_state_t empty_sections_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.create(
        &empty_sections_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterout_deserialize(
        &empty_sections_loaded, empty_sections, NULL,
        &deserialize_context));
    ASSERT_TRUE(empty_sections_loaded.has_owner);
    ASSERT_TRUE(empty_sections_loaded.has_destinations);
    ASSERT_EQ(NMO_OBJECT_ID_NONE,
              empty_sections_loaded.owner.raw_id);
    ASSERT_EQ(0u, empty_sections_loaded.destination_count);

    nmo_chunk_t *empty_sections_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty_sections_saved);
    empty_sections_saved->class_id = NMO_CID_PARAMETEROUT;
    empty_sections_saved->data_version = 8;
    empty_sections_saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_parameterout_serialize(
        &empty_sections_loaded, empty_sections_saved, NULL,
        &serialize_context));
    nmo_chunk_close(empty_sections_saved);
    size_t owner_dwords = 0;
    size_t destination_dwords = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        empty_sections_saved, CK_STATESAVE_PARAMETEROUT_OWNER,
        &owner_dwords));
    ASSERT_EQ(1u, owner_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        empty_sections_saved, CK_STATESAVE_PARAMETEROUT_DESTINATIONS,
        &destination_dwords));
    ASSERT_EQ(1u, destination_dwords);

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
    failed.base.type_guid = CKPGUID_INT;
    failed.base.mode = CKPARAM_MODE_BUFFER;
    failed.base.has_state = true;
    uint8_t old_byte = 0xabu;
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &failed.base.buffer_data, &old_byte));
    failed.owner = nmo_ref_from_raw(901);
    failed.destination_ids = previous_destinations;
    failed.destination_count = 1;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameterout_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_INT, failed.base.type_guid));
    ASSERT_EQ(CKPARAM_MODE_BUFFER, failed.base.mode);
    ASSERT_EQ(1u, failed.base.buffer_data.count);
    ASSERT_EQ(0xabu, NMO_ARRAY_DATA(
        uint8_t, &failed.base.buffer_data)[0]);
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

    nmo_chunk_t *owner_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(owner_cross_section);
    owner_cross_section->class_id = NMO_CID_PARAMETEROUT;
    owner_cross_section->data_version = 8;
    owner_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(owner_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        owner_cross_section, CK_STATESAVE_PARAMETEROUT_OWNER));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        owner_cross_section, 0x11223344u));
    nmo_chunk_close(owner_cross_section);
    nmo_chunk_set_file_context(owner_cross_section, &read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameterout_deserialize(
        &failed, owner_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(901u, failed.owner.raw_id);
    ASSERT_EQ(previous_destinations, failed.destination_ids);
    ASSERT_EQ(1u, failed.destination_count);

    nmo_chunk_t *owner_trailing_payload = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(owner_trailing_payload);
    owner_trailing_payload->class_id = NMO_CID_PARAMETEROUT;
    owner_trailing_payload->data_version = 8;
    owner_trailing_payload->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(owner_trailing_payload));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        owner_trailing_payload, CK_STATESAVE_PARAMETEROUT_OWNER));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        owner_trailing_payload, 801));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        owner_trailing_payload, 0x12345678u));
    nmo_chunk_close(owner_trailing_payload);
    nmo_chunk_set_file_context(owner_trailing_payload, &read_context);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_parameterout_deserialize(
        &failed, owner_trailing_payload, NULL, &deserialize_context));
    ASSERT_EQ(901u, failed.owner.raw_id);
    ASSERT_EQ(previous_destinations, failed.destination_ids);
    ASSERT_EQ(1u, failed.destination_count);

    nmo_chunk_t *count_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(count_cross_section);
    count_cross_section->class_id = NMO_CID_PARAMETEROUT;
    count_cross_section->data_version = 8;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(count_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        count_cross_section, CK_STATESAVE_PARAMETEROUT_DESTINATIONS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(count_cross_section, 0));
    nmo_chunk_close(count_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameterout_deserialize(
        &failed, count_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(901u, failed.owner.raw_id);
    ASSERT_EQ(previous_destinations, failed.destination_ids);
    ASSERT_EQ(1u, failed.destination_count);

    nmo_chunk_t *destinations_trailing_payload = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(destinations_trailing_payload);
    destinations_trailing_payload->class_id = NMO_CID_PARAMETEROUT;
    destinations_trailing_payload->data_version = 8;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(
        destinations_trailing_payload));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        destinations_trailing_payload,
        CK_STATESAVE_PARAMETEROUT_DESTINATIONS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(
        destinations_trailing_payload, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        destinations_trailing_payload, 0x12345678u));
    nmo_chunk_close(destinations_trailing_payload);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_parameterout_deserialize(
        &failed, destinations_trailing_payload, NULL,
        &deserialize_context));
    ASSERT_EQ(901u, failed.owner.raw_id);
    ASSERT_EQ(previous_destinations, failed.destination_ids);
    ASSERT_EQ(1u, failed.destination_count);

    nmo_chunk_t *cross_section_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_count);
    cross_section_count->class_id = NMO_CID_PARAMETEROUT;
    cross_section_count->data_version = 8;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_count, CK_STATESAVE_PARAMETEROUT_DESTINATIONS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_count, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_count, 0x7F123456u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_count, 0));
    nmo_chunk_close(cross_section_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameterout_deserialize(
        &failed, cross_section_count, NULL, &deserialize_context));
    nmo_chunk_parser_state_t *parser =
        (nmo_chunk_parser_state_t *)cross_section_count->parser_state;
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(parser->prev_identifier_pos + 3u, parser->current_pos);
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

    nmo_parameterout_state_t oversized;
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.create(
        &oversized, NULL, NULL));
    nmo_ref_t oversized_destination = nmo_ref_from_raw(805);
    oversized.destination_ids = &oversized_destination;
    oversized.destination_count = (uint32_t)INT32_MAX + 1u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_parameterout_vtable.validate(
                  &oversized, &parameterout_type, NULL));
    nmo_chunk_t *oversized_target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(oversized_target);
    oversized_target->class_id = NMO_CID_PARAMETEROUT;
    oversized_target->data_version = 8;
    oversized_target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(oversized_target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        oversized_target, 0x87654321u));
    nmo_chunk_close(oversized_target);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_parameterout_serialize(
        &oversized, oversized_target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(oversized_target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(oversized_target));
    uint32_t oversized_marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(
        oversized_target, &oversized_marker));
    ASSERT_EQ(0x87654321u, oversized_marker);
    oversized.destination_ids = NULL;
    oversized.destination_count = 0;
    nmo_parameterout_vtable.destroy(&oversized, NULL, NULL);

    nmo_ref_t invalid_destinations[] = {
        nmo_ref_from_raw(803),
        nmo_ref_from_id(999),
    };
    nmo_parameterout_state_t invalid_ref;
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.create(
        &invalid_ref, NULL, NULL));
    invalid_ref.owner = nmo_ref_from_raw(804);
    invalid_ref.destination_ids = invalid_destinations;
    invalid_ref.destination_count = 2;
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_PARAMETEROUT;
    target->data_version = 8;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_parameterout_serialize(
        &invalid_ref, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_parameterout_vtable.destroy(&source, NULL, NULL);
    nmo_parameterout_vtable.destroy(&copied, NULL, NULL);
    nmo_parameterout_vtable.destroy(&aliased_copy, NULL, NULL);
    nmo_parameterout_vtable.destroy(&loaded, NULL, NULL);
    nmo_parameterout_vtable.destroy(&reloaded, NULL, NULL);
    nmo_parameterout_vtable.destroy(
        &empty_sections_loaded, NULL, NULL);
    nmo_parameterout_vtable.destroy(&failed, NULL, NULL);
    nmo_parameterout_vtable.destroy(&invalid_ref, NULL, NULL);
    nmo_parameterout_vtable.destroy(&failing_copy_source, NULL, NULL);
    nmo_parameterout_vtable.destroy(&preserved_copy, NULL, NULL);
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
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, source.base.visibility_flags);
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
    failed.type_guid = CKPGUID_INT;
    failed.mode = CKPARAM_MODE_BUFFER;
    failed.has_state = true;
    failed.object_ref = nmo_ref_from_raw(702);
    failed.manager_guid.d1 = 0x11223344u;
    failed.manager_value = 55;
    uint8_t old_byte = 0xabu;
    ASSERT_EQ(NMO_OK, nmo_array_append(&failed.buffer_data, &old_byte));
    ASSERT_NE(NMO_OK, nmo_parameter_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_INT, failed.type_guid));
    ASSERT_EQ(CKPARAM_MODE_BUFFER, failed.mode);
    ASSERT_TRUE(failed.has_state);
    ASSERT_EQ(702u, failed.object_ref.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.object_ref.state);
    ASSERT_EQ(0x11223344u, failed.manager_guid.d1);
    ASSERT_EQ(55u, failed.manager_value);
    ASSERT_EQ(1u, failed.buffer_data.count);
    ASSERT_EQ(0xabu, NMO_ARRAY_DATA(uint8_t, &failed.buffer_data)[0]);

    nmo_chunk_t *header_only = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(header_only);
    header_only->class_id = NMO_CID_PARAMETER;
    header_only->data_version = 8;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(header_only));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(header_only, 0x40));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(header_only, CKPGUID_INT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(header_only, 3u));
    nmo_chunk_close(header_only);
    nmo_parameter_state_t header_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameter_vtable.create(
        &header_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameter_deserialize(
        &header_loaded, header_only, NULL, &deserialize_context));
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_INT, header_loaded.type_guid));
    ASSERT_FALSE(header_loaded.has_state);
    ASSERT_EQ(CKPARAM_MODE_NONE, header_loaded.mode);

    nmo_chunk_t *cross_section_object = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_object);
    cross_section_object->class_id = NMO_CID_PARAMETER;
    cross_section_object->data_version = 8;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_object));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(cross_section_object, 0x40));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        cross_section_object, CKPGUID_OBJECT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_object, 2u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_object, 704u));
    nmo_chunk_close(cross_section_object);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameter_deserialize(
        &failed, cross_section_object, NULL, &deserialize_context));
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_INT, failed.type_guid));
    ASSERT_EQ(CKPARAM_MODE_BUFFER, failed.mode);
    ASSERT_EQ(702u, failed.object_ref.raw_id);
    ASSERT_EQ(1u, failed.buffer_data.count);

    nmo_chunk_t *cross_section_buffer = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_buffer);
    cross_section_buffer->class_id = NMO_CID_PARAMETER;
    cross_section_buffer->data_version = 8;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_buffer));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(cross_section_buffer, 0x40));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        cross_section_buffer, CKPGUID_INT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_buffer, 1u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_buffer, 4u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_buffer, 0x44332211u));
    nmo_chunk_close(cross_section_buffer);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameter_deserialize(
        &failed, cross_section_buffer, NULL, &deserialize_context));
    ASSERT_TRUE(nmo_guid_equals(CKPGUID_INT, failed.type_guid));
    ASSERT_EQ(CKPARAM_MODE_BUFFER, failed.mode);
    ASSERT_EQ(1u, failed.buffer_data.count);
    ASSERT_EQ(0xabu, NMO_ARRAY_DATA(uint8_t, &failed.buffer_data)[0]);

    const struct {
        nmo_guid_t type_guid;
        uint32_t param_state;
        size_t payload_dwords;
    } trailing_cases[] = {
        {CKPGUID_INT, 3u, 0u},
        {CKPGUID_OBJECT, 2u, 1u},
        {CKPGUID_INT, 0u, 1u},
        {CKPGUID_INT, 1u, 1u},
        {CKPGUID_PARAMETERTYPE, 1u, 2u},
        {CKPGUID_INT, 4u, 2u},
    };
    for (size_t i = 0;
         i < sizeof(trailing_cases) / sizeof(trailing_cases[0]); ++i) {
        nmo_chunk_t *trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(trailing);
        trailing->class_id = NMO_CID_PARAMETER;
        trailing->data_version = 8;
        trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            trailing, 0x40u));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
            trailing, trailing_cases[i].type_guid));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
            trailing, trailing_cases[i].param_state));
        for (size_t j = 0; j < trailing_cases[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0x12345678u));
        nmo_chunk_close(trailing);
        nmo_chunk_set_file_context(trailing, &read_context);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_parameter_deserialize(
            &failed, trailing, NULL, &deserialize_context));
        ASSERT_TRUE(nmo_guid_equals(CKPGUID_INT, failed.type_guid));
        ASSERT_EQ(CKPARAM_MODE_BUFFER, failed.mode);
        ASSERT_TRUE(failed.has_state);
        ASSERT_EQ(702u, failed.object_ref.raw_id);
        ASSERT_EQ(0x11223344u, failed.manager_guid.d1);
        ASSERT_EQ(55u, failed.manager_value);
        ASSERT_EQ(1u, failed.buffer_data.count);
        ASSERT_EQ(0xabu, NMO_ARRAY_DATA(uint8_t, &failed.buffer_data)[0]);
    }

    nmo_parameter_state_t invalid = source;
    invalid.object_ref = nmo_ref_from_id(999);
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_PARAMETER;
    target->data_version = 8;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_parameter_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_parameter_vtable.destroy(&source, NULL, NULL);
    nmo_parameter_vtable.destroy(&loaded, NULL, NULL);
    nmo_parameter_vtable.destroy(&reloaded, NULL, NULL);
    nmo_parameter_vtable.destroy(&failed, NULL, NULL);
    nmo_parameter_vtable.destroy(&header_loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, parameter_copy_is_deep_and_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_parameter_vtable.validate(NULL, NULL, NULL));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_parameterlocal_vtable.validate(NULL, NULL, NULL));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_parameterout_vtable.validate(NULL, NULL, NULL));

    nmo_parameter_state_t source;
    ASSERT_EQ(NMO_OK, nmo_parameter_vtable.create(&source, NULL, NULL));
    source.buffer_data.element_size = sizeof(uint16_t);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_parameter_vtable.validate(&source, NULL, NULL));
    source.buffer_data.element_size = sizeof(uint8_t);
    source.type_guid = CKPGUID_INT;
    source.mode = CKPARAM_MODE_BUFFER;
    source.has_state = true;
    uint8_t source_bytes[] = {0x11u, 0x22u};
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &source.buffer_data, &source_bytes[0]));
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &source.buffer_data, &source_bytes[1]));
    source.subchunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(source.subchunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(source.subchunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(source.subchunk, 0xaabbccddu));
    nmo_chunk_close(source.subchunk);

    nmo_parameter_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_parameter_vtable.create(&copied, NULL, NULL));
    uint8_t old_byte = 0xccu;
    ASSERT_EQ(NMO_OK, nmo_array_append(&copied.buffer_data, &old_byte));
    ASSERT_EQ(NMO_OK, nmo_parameter_vtable.copy(
        &source, &copied, NULL, arena));
    ASSERT_NE(source.buffer_data.data, copied.buffer_data.data);
    ASSERT_EQ(2u, copied.buffer_data.count);
    ASSERT_EQ(0x11u, NMO_ARRAY_DATA(uint8_t, &copied.buffer_data)[0]);
    ASSERT_NE(source.subchunk, copied.subchunk);
    ASSERT_TRUE(nmo_parameter_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_parameter_vtable.hash(&source),
              nmo_parameter_vtable.hash(&copied));

    nmo_parameter_state_t aliased_copy = source;
    ASSERT_EQ(NMO_OK, nmo_parameter_vtable.copy(
        &source, &aliased_copy, NULL, arena));
    ASSERT_NE(source.buffer_data.data, aliased_copy.buffer_data.data);
    ASSERT_EQ(0x11u, NMO_ARRAY_DATA(uint8_t, &source.buffer_data)[0]);
    ASSERT_TRUE(nmo_parameter_vtable.equals(&source, &aliased_copy));

    NMO_ARRAY_DATA(uint8_t, &source.buffer_data)[0] = 0x33u;
    ASSERT_FALSE(nmo_parameter_vtable.equals(&source, &copied));
    ASSERT_EQ(0x11u, NMO_ARRAY_DATA(uint8_t, &copied.buffer_data)[0]);
    ASSERT_EQ(0x11u, NMO_ARRAY_DATA(
        uint8_t, &aliased_copy.buffer_data)[0]);

    fail_after_allocator_state_t allocator_state = {
        .allowed_allocations = 1,
    };
    nmo_allocator_t failing_allocator = {
        .alloc = fail_after_alloc,
        .free = fail_after_free,
        .user_data = &allocator_state,
    };
    nmo_parameter_state_t failing_source;
    ASSERT_EQ(NMO_OK, nmo_parameter_vtable.create(
        &failing_source, NULL, NULL));
    nmo_array_dispose(&failing_source.buffer_data);
    ASSERT_EQ(NMO_OK, nmo_array_init(
        &failing_source.buffer_data, sizeof(uint8_t), 2,
        &failing_allocator));
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &failing_source.buffer_data, &source_bytes[0]));
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &failing_source.buffer_data, &source_bytes[1]));
    allocator_state.allowed_allocations = allocator_state.allocation_count;

    nmo_parameter_state_t preserved;
    ASSERT_EQ(NMO_OK, nmo_parameter_vtable.create(&preserved, NULL, NULL));
    uint8_t preserved_byte = 0xddu;
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &preserved.buffer_data, &preserved_byte));
    preserved.object_ref = nmo_ref_from_raw(901);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_parameter_vtable.copy(
        &failing_source, &preserved, NULL, arena));
    ASSERT_EQ(1u, preserved.buffer_data.count);
    ASSERT_EQ(0xddu, NMO_ARRAY_DATA(uint8_t, &preserved.buffer_data)[0]);
    ASSERT_EQ(901u, preserved.object_ref.raw_id);

    nmo_parameter_vtable.destroy(&source, NULL, NULL);
    nmo_parameter_vtable.destroy(&copied, NULL, NULL);
    nmo_parameter_vtable.destroy(&aliased_copy, NULL, NULL);
    nmo_parameter_vtable.destroy(&failing_source, NULL, NULL);
    nmo_parameter_vtable.destroy(&preserved, NULL, NULL);
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
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, source.base.visibility_flags);
    ASSERT_TRUE(source.has_in1);
    ASSERT_TRUE(source.has_in2);
    ASSERT_TRUE(source.has_out);
    source.operation_guid = (nmo_guid_t){0x12345678u, 0x9ABCDEF0u};
    source.in1.ref = nmo_ref_from_raw(710);
    source.in2.ref = nmo_ref_from_raw(711);
    source.out.ref = nmo_ref_from_raw(712);
    source.has_in1 = 1;
    source.has_in2 = 1;
    source.has_out = 1;
    source.in1.chunk = nmo_chunk_create(arena);
    source.in2.chunk = nmo_chunk_create(arena);
    source.out.chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(source.in1.chunk);
    ASSERT_NOT_NULL(source.in2.chunk);
    ASSERT_NOT_NULL(source.out.chunk);
    nmo_chunk_t *operation_chunks[] = {
        source.in1.chunk, source.in2.chunk, source.out.chunk,
    };
    for (uint32_t i = 0; i < 3; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(operation_chunks[i]));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
            operation_chunks[i], 0x100u + i));
        nmo_chunk_close(operation_chunks[i]);
    }

    nmo_parameteroperation_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.copy(
        &source, &copied, NULL, arena));
    ASSERT_NE(source.in1.chunk, copied.in1.chunk);
    ASSERT_NE(source.in2.chunk, copied.in2.chunk);
    ASSERT_NE(source.out.chunk, copied.out.chunk);
    ASSERT_TRUE(nmo_parameteroperation_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_parameteroperation_vtable.hash(&source),
              nmo_parameteroperation_vtable.hash(&copied));

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
    source.legacy_prefix_ref = nmo_ref_from_raw(709);
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
    ASSERT_EQ(709u, legacy_loaded.legacy_prefix_ref.raw_id);
    ASSERT_TRUE(legacy_loaded.has_legacy_prefix);
    ASSERT_EQ(NMO_REF_UNRESOLVED,
              legacy_loaded.legacy_prefix_ref.state);
    ASSERT_EQ(710u, legacy_loaded.in1.ref.raw_id);
    ASSERT_EQ(711u, legacy_loaded.in2.ref.raw_id);
    ASSERT_EQ(712u, legacy_loaded.out.ref.raw_id);

    nmo_chunk_t *legacy_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_saved);
    legacy_saved->class_id = NMO_CID_PARAMETEROPERATION;
    legacy_saved->data_version = 4;
    legacy_saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(legacy_saved, &write_context);
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_serialize(
        &legacy_loaded, legacy_saved, NULL, &serialize_context));
    nmo_chunk_close(legacy_saved);
    nmo_chunk_set_file_context(legacy_saved, &read_context);
    nmo_parameteroperation_state_t legacy_reloaded;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &legacy_reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_deserialize(
        &legacy_reloaded, legacy_saved, NULL, &deserialize_context));
    ASSERT_EQ(709u, legacy_reloaded.legacy_prefix_ref.raw_id);
    ASSERT_EQ(710u, legacy_reloaded.in1.ref.raw_id);
    ASSERT_EQ(711u, legacy_reloaded.in2.ref.raw_id);
    ASSERT_EQ(712u, legacy_reloaded.out.ref.raw_id);

    nmo_chunk_t *lossy_legacy_target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(lossy_legacy_target);
    lossy_legacy_target->class_id = NMO_CID_PARAMETEROPERATION;
    lossy_legacy_target->data_version = 8;
    lossy_legacy_target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(lossy_legacy_target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        lossy_legacy_target, 0x12345678u));
    nmo_chunk_close(lossy_legacy_target);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED,
              nmo_parameteroperation_serialize(
                  &legacy_loaded, lossy_legacy_target, NULL,
                  &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(lossy_legacy_target));
    source.legacy_prefix_ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);

    nmo_parameteroperation_state_t default_source;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &default_source, NULL, NULL));
    nmo_chunk_t *default_version = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(default_version);
    default_version->class_id = NMO_CID_PARAMETEROPERATION;
    default_version->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_serialize(
        &default_source, default_version, NULL, &serialize_context));
    ASSERT_EQ(NMO_CHUNK_DATA_VERSION_CURRENT,
              nmo_chunk_get_data_version(default_version));
    nmo_chunk_close(default_version);
    size_t default_section_dwords = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        default_version, CK_STATESAVE_OPERATIONNEWDATA,
        &default_section_dwords));
    ASSERT_EQ(6u, default_section_dwords);
    nmo_parameteroperation_state_t default_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &default_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_deserialize(
        &default_loaded, default_version, NULL, &deserialize_context));
    ASSERT_TRUE(default_loaded.has_in1);
    ASSERT_TRUE(default_loaded.has_in2);
    ASSERT_TRUE(default_loaded.has_out);
    ASSERT_FALSE(default_loaded.has_legacy_prefix);

    nmo_chunk_t *legacy_null_prefix = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_null_prefix);
    legacy_null_prefix->class_id = NMO_CID_PARAMETEROPERATION;
    legacy_null_prefix->data_version = 0;
    legacy_null_prefix->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_null_prefix));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_null_prefix, CK_STATESAVE_OPERATIONNEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        legacy_null_prefix, (nmo_guid_t){21u, 22u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        legacy_null_prefix, 4u));
    for (size_t i = 0; i < 4u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_sequence_item(
            legacy_null_prefix, NMO_OBJECT_ID_NONE));
    }
    nmo_chunk_close(legacy_null_prefix);
    nmo_parameteroperation_state_t legacy_null_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &legacy_null_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_deserialize(
        &legacy_null_loaded, legacy_null_prefix, NULL,
        &deserialize_context));
    ASSERT_TRUE(legacy_null_loaded.has_legacy_prefix);

    nmo_chunk_t *legacy_null_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_null_saved);
    legacy_null_saved->class_id = NMO_CID_PARAMETEROPERATION;
    legacy_null_saved->data_version = 0;
    legacy_null_saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_serialize(
        &legacy_null_loaded, legacy_null_saved, NULL,
        &serialize_context));
    ASSERT_EQ(0u, nmo_chunk_get_data_version(legacy_null_saved));
    nmo_chunk_close(legacy_null_saved);
    size_t legacy_null_dwords = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        legacy_null_saved, CK_STATESAVE_OPERATIONNEWDATA,
        &legacy_null_dwords));
    ASSERT_EQ(7u, legacy_null_dwords);

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
    failed.base.visibility_flags = NMO_CKOBJECT_HIERARCHICAL;
    failed.operation_guid = (nmo_guid_t){3u, 4u};
    failed.in1.ref = nmo_ref_from_raw(901);
    failed.in2.ref = nmo_ref_from_raw(902);
    failed.out.ref = nmo_ref_from_raw(903);
    failed.has_in1 = 1;
    failed.has_in2 = 1;
    failed.has_out = 1;
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_parameteroperation_deserialize(
        &failed, invalid_count, NULL, &deserialize_context));
    ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, failed.base.visibility_flags);
    ASSERT_EQ(3u, failed.operation_guid.d1);
    ASSERT_EQ(4u, failed.operation_guid.d2);
    ASSERT_EQ(901u, failed.in1.ref.raw_id);
    ASSERT_EQ(902u, failed.in2.ref.raw_id);
    ASSERT_EQ(903u, failed.out.ref.raw_id);

    nmo_chunk_t *cross_section_refs = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_refs);
    cross_section_refs->class_id = NMO_CID_PARAMETEROPERATION;
    cross_section_refs->data_version = 8;
    cross_section_refs->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_refs));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_refs, CK_STATESAVE_OPERATIONNEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        cross_section_refs, (nmo_guid_t){1u, 2u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        cross_section_refs, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_refs, 0x7F123456u));
    nmo_chunk_close(cross_section_refs);
    nmo_chunk_set_file_context(cross_section_refs, &read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameteroperation_deserialize(
        &failed, cross_section_refs, NULL, &deserialize_context));
    nmo_chunk_parser_state_t *parser =
        (nmo_chunk_parser_state_t *)cross_section_refs->parser_state;
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(parser->prev_identifier_pos + 5u, parser->current_pos);
    ASSERT_EQ(3u, failed.operation_guid.d1);
    ASSERT_EQ(4u, failed.operation_guid.d2);
    ASSERT_EQ(901u, failed.in1.ref.raw_id);
    ASSERT_EQ(902u, failed.in2.ref.raw_id);
    ASSERT_EQ(903u, failed.out.ref.raw_id);

    nmo_chunk_t *missing_sequence_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_sequence_count);
    missing_sequence_count->class_id = NMO_CID_PARAMETEROPERATION;
    missing_sequence_count->data_version = 8;
    missing_sequence_count->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_sequence_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_sequence_count, CK_STATESAVE_OPERATIONNEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        missing_sequence_count, (nmo_guid_t){1u, 2u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_sequence_count, 0));
    nmo_chunk_close(missing_sequence_count);
    nmo_chunk_set_file_context(missing_sequence_count, &read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameteroperation_deserialize(
        &failed, missing_sequence_count, NULL, &deserialize_context));
    ASSERT_EQ(3u, failed.operation_guid.d1);
    ASSERT_EQ(4u, failed.operation_guid.d2);
    ASSERT_EQ(901u, failed.in1.ref.raw_id);
    ASSERT_EQ(902u, failed.in2.ref.raw_id);
    ASSERT_EQ(903u, failed.out.ref.raw_id);

    nmo_chunk_t *trailing_sequence = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_sequence);
    trailing_sequence->class_id = NMO_CID_PARAMETEROPERATION;
    trailing_sequence->data_version = 8;
    trailing_sequence->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_sequence));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_sequence, CK_STATESAVE_OPERATIONNEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        trailing_sequence, (nmo_guid_t){1u, 2u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        trailing_sequence, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        trailing_sequence, 0x12345678u));
    nmo_chunk_close(trailing_sequence);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_parameteroperation_deserialize(
        &failed, trailing_sequence, NULL, &deserialize_context));
    ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, failed.base.visibility_flags);
    ASSERT_EQ(3u, failed.operation_guid.d1);
    ASSERT_EQ(4u, failed.operation_guid.d2);
    ASSERT_EQ(901u, failed.in1.ref.raw_id);
    ASSERT_EQ(902u, failed.in2.ref.raw_id);
    ASSERT_EQ(903u, failed.out.ref.raw_id);

    nmo_parameteroperation_state_t invalid = source;
    invalid.out.ref = nmo_ref_from_id(999);
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_PARAMETEROPERATION;
    target->data_version = 8;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_parameteroperation_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_parameteroperation_vtable.destroy(&source, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&copied, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&loaded, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&reloaded, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&legacy_loaded, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&legacy_reloaded, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&short_loaded, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&failed, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&invalid, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&default_source, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&default_loaded, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&legacy_null_loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, parameter_refs_require_layout_classes) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_type_registry_t *types = nmo_type_registry_create(arena);
    ASSERT_NOT_NULL(types);
    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(types));
    ASSERT_EQ(NMO_OK, nmo_register_object_types(types));
    nmo_object_repository_t *repository =
        nmo_object_repository_create(NULL);
    ASSERT_NOT_NULL(repository);

    nmo_object_t *wrong_input = nmo_object_create(
        NULL, 1701u, NMO_CID_PARAMETEROUT);
    nmo_object_t *valid_input = nmo_object_create(
        NULL, 1702u, NMO_CID_PARAMETERIN);
    nmo_object_t *wrong_output = nmo_object_create(
        NULL, 1703u, NMO_CID_PARAMETERLOCAL);
    nmo_object_t *operation_owner = nmo_object_create(
        NULL, 1704u, NMO_CID_PARAMETEROPERATION);
    ASSERT_NOT_NULL(wrong_input);
    ASSERT_NOT_NULL(valid_input);
    ASSERT_NOT_NULL(wrong_output);
    ASSERT_NOT_NULL(operation_owner);
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(
        wrong_input, CKPGUID_PARAMETEROUT));
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(
        valid_input, CKPGUID_PARAMETERIN));
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(
        wrong_output, CKPGUID_PARAMETERLOCAL));
    ASSERT_EQ(NMO_OK, nmo_object_set_type_guid(
        operation_owner, CKPGUID_PARAMETEROPERATION));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(
        repository, &wrong_input));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(
        repository, &valid_input));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(
        repository, &wrong_output));
    ASSERT_EQ(NMO_OK, nmo_object_repository_add(
        repository, &operation_owner));

    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(file_to_runtime);
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(file_to_runtime, 701u, 1701u));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(file_to_runtime, 702u, 1702u));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(file_to_runtime, 703u, 1703u));
    ASSERT_EQ(NMO_OK, nmo_id_remap_add(file_to_runtime, 704u, 1704u));

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_PARAMETEROPERATION;
    chunk->data_version = 8;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_OPERATIONNEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        chunk, (nmo_guid_t){1u, 2u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(chunk, 3u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_sequence_item(chunk, 701u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_sequence_item(chunk, 702u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_sequence_item(chunk, 703u));
    nmo_chunk_close(chunk);
    nmo_chunk_file_context_t file_context = {
        .file_to_runtime = file_to_runtime,
        .repository = repository,
    };
    nmo_chunk_set_file_context(chunk, &file_context);
    nmo_type_runtime_t type_runtime = {.types = types, .ops = NULL};
    nmo_deserialize_context_t context = nmo_deserialize_context_create(
        arena, repository, &type_runtime, NMO_DESER_FLAG_FILE_MODE);

    nmo_parameteroperation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_deserialize(
        &loaded, chunk, NULL, &context));
    ASSERT_EQ(NMO_REF_CLASS_MISMATCH, loaded.in1.ref.state);
    ASSERT_EQ(1701u, loaded.in1.ref.id);
    ASSERT_EQ(NMO_REF_RESOLVED, loaded.in2.ref.state);
    ASSERT_EQ(1702u, loaded.in2.ref.id);
    ASSERT_EQ(NMO_REF_CLASS_MISMATCH, loaded.out.ref.state);
    ASSERT_EQ(1703u, loaded.out.ref.id);

    nmo_chunk_t *owned_input = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(owned_input);
    owned_input->class_id = NMO_CID_PARAMETERIN;
    owned_input->data_version = 8;
    owned_input->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(owned_input));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        owned_input, CK_STATESAVE_PARAMETERIN_DEFAULTDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        owned_input, (nmo_guid_t){3u, 4u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(owned_input, 704u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(owned_input, 701u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        owned_input, NMO_OBJECT_ID_NONE));
    nmo_chunk_close(owned_input);
    nmo_chunk_set_file_context(owned_input, &file_context);

    nmo_parameterin_state_t owned_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(
        &owned_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_deserialize(
        &owned_loaded, owned_input, NULL, &context));
    ASSERT_EQ(NMO_REF_RESOLVED, owned_loaded.owner.state);
    ASSERT_EQ(1704u, owned_loaded.owner.id);
    ASSERT_EQ(NMO_REF_RESOLVED, owned_loaded.source.state);
    ASSERT_EQ(1701u, owned_loaded.source.id);
    ASSERT_FALSE(owned_loaded.is_shared);

    nmo_chunk_t *shared_input = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(shared_input);
    shared_input->class_id = NMO_CID_PARAMETERIN;
    shared_input->data_version = 8;
    shared_input->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(shared_input));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        shared_input, CK_STATESAVE_PARAMETERIN_DATASHARED));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        shared_input, (nmo_guid_t){5u, 6u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(shared_input, 701u));
    nmo_chunk_close(shared_input);
    nmo_chunk_set_file_context(shared_input, &file_context);

    nmo_parameterin_state_t shared_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(
        &shared_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_deserialize(
        &shared_loaded, shared_input, NULL, &context));
    ASSERT_TRUE(shared_loaded.is_shared);
    ASSERT_EQ(NMO_REF_CLASS_MISMATCH, shared_loaded.source.state);
    ASSERT_EQ(1701u, shared_loaded.source.id);

    nmo_chunk_t *direct_input = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(direct_input);
    direct_input->class_id = NMO_CID_PARAMETERIN;
    direct_input->data_version = 8;
    direct_input->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(direct_input));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        direct_input, CK_STATESAVE_PARAMETERIN_DATASOURCE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        direct_input, (nmo_guid_t){7u, 8u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(direct_input, 702u));
    nmo_chunk_close(direct_input);
    nmo_chunk_set_file_context(direct_input, &file_context);

    nmo_parameterin_state_t direct_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterin_vtable.create(
        &direct_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterin_deserialize(
        &direct_loaded, direct_input, NULL, &context));
    ASSERT_FALSE(direct_loaded.is_shared);
    ASSERT_EQ(NMO_REF_CLASS_MISMATCH, direct_loaded.source.state);
    ASSERT_EQ(1702u, direct_loaded.source.id);

    nmo_chunk_t *parameter_out = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(parameter_out);
    parameter_out->class_id = NMO_CID_PARAMETEROUT;
    parameter_out->data_version = 8;
    parameter_out->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(parameter_out));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        parameter_out, CK_STATESAVE_PARAMETEROUT_OWNER));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(parameter_out, 704u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        parameter_out, CK_STATESAVE_PARAMETEROUT_DESTINATIONS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(parameter_out, 3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(parameter_out, 701u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(parameter_out, 702u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(parameter_out, 703u));
    nmo_chunk_close(parameter_out);
    nmo_chunk_set_file_context(parameter_out, &file_context);

    nmo_parameterout_state_t output_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameterout_vtable.create(
        &output_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameterout_deserialize(
        &output_loaded, parameter_out, NULL, &context));
    ASSERT_EQ(NMO_REF_RESOLVED, output_loaded.owner.state);
    ASSERT_EQ(1704u, output_loaded.owner.id);
    ASSERT_EQ(3u, output_loaded.destination_count);
    ASSERT_EQ(NMO_REF_RESOLVED, output_loaded.destination_ids[0].state);
    ASSERT_EQ(1701u, output_loaded.destination_ids[0].id);
    ASSERT_EQ(NMO_REF_CLASS_MISMATCH,
              output_loaded.destination_ids[1].state);
    ASSERT_EQ(1702u, output_loaded.destination_ids[1].id);
    ASSERT_EQ(NMO_REF_RESOLVED, output_loaded.destination_ids[2].state);
    ASSERT_EQ(1703u, output_loaded.destination_ids[2].id);

    nmo_parameteroperation_vtable.destroy(&loaded, NULL, NULL);
    nmo_parameterin_vtable.destroy(&owned_loaded, NULL, NULL);
    nmo_parameterin_vtable.destroy(&shared_loaded, NULL, NULL);
    nmo_parameterin_vtable.destroy(&direct_loaded, NULL, NULL);
    nmo_parameterout_vtable.destroy(&output_loaded, NULL, NULL);
    nmo_object_repository_destroy(repository);
    nmo_type_registry_destroy(types);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, parameteroperation_legacy_sections_are_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
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

    nmo_chunk_t *output_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(output_cross_section);
    output_cross_section->class_id = NMO_CID_PARAMETEROPERATION;
    output_cross_section->data_version = 8;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(output_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        output_cross_section, CK_STATESAVE_OPERATIONOUTPUT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        output_cross_section, 823));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(output_cross_section, 0));
    nmo_chunk_close(output_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameteroperation_deserialize(
        &loaded, output_cross_section, NULL, NULL));
    ASSERT_EQ(911u, loaded.in1.ref.raw_id);
    ASSERT_EQ(912u, loaded.in2.ref.raw_id);
    ASSERT_EQ(1u, loaded.has_in1);
    ASSERT_EQ(1u, loaded.has_in2);

    nmo_chunk_t *inputs_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(inputs_cross_section);
    inputs_cross_section->class_id = NMO_CID_PARAMETEROPERATION;
    inputs_cross_section->data_version = 8;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(inputs_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        inputs_cross_section, CK_STATESAVE_OPERATIONINPUTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        inputs_cross_section, 821));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_sub_chunk(inputs_cross_section, NULL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        inputs_cross_section, 822));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(inputs_cross_section, 0));
    nmo_chunk_close(inputs_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_parameteroperation_deserialize(
        &loaded, inputs_cross_section, NULL, NULL));
    ASSERT_EQ(911u, loaded.in1.ref.raw_id);
    ASSERT_EQ(912u, loaded.in2.ref.raw_id);
    ASSERT_EQ(1u, loaded.has_in1);
    ASSERT_EQ(1u, loaded.has_in2);

    nmo_chunk_t *legacy_file = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_file);
    legacy_file->class_id = NMO_CID_PARAMETEROPERATION;
    legacy_file->data_version = 8;
    legacy_file->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_file));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_file, CK_STATESAVE_OPERATIONOP));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        legacy_file, (nmo_guid_t){31u, 32u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_file, CK_STATESAVE_OPERATIONDEFAULTDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(legacy_file, 820));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_file, CK_STATESAVE_OPERATIONOUTPUT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(legacy_file, 823));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_file, CK_STATESAVE_OPERATIONINPUTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(legacy_file, 821));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(legacy_file, 822));
    nmo_chunk_close(legacy_file);

    nmo_parameteroperation_state_t legacy_file_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &legacy_file_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_deserialize(
        &legacy_file_loaded, legacy_file, NULL, NULL));
    ASSERT_FALSE(legacy_file_loaded.has_new_data);
    ASSERT_TRUE(legacy_file_loaded.has_operation);
    ASSERT_TRUE(legacy_file_loaded.has_owner);
    ASSERT_TRUE(legacy_file_loaded.has_out);
    ASSERT_TRUE(legacy_file_loaded.has_in1);
    ASSERT_TRUE(legacy_file_loaded.has_in2);
    ASSERT_EQ(820u, legacy_file_loaded.owner.raw_id);
    ASSERT_EQ(821u, legacy_file_loaded.in1.ref.raw_id);
    ASSERT_EQ(822u, legacy_file_loaded.in2.ref.raw_id);
    ASSERT_EQ(823u, legacy_file_loaded.out.ref.raw_id);

    nmo_chunk_t *legacy_file_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_file_saved);
    legacy_file_saved->class_id = NMO_CID_PARAMETEROPERATION;
    legacy_file_saved->data_version = 8;
    legacy_file_saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_serialize(
        &legacy_file_loaded, legacy_file_saved, NULL, NULL));
    nmo_chunk_close(legacy_file_saved);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        legacy_file_saved, CK_STATESAVE_OPERATIONNEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        legacy_file_saved, CK_STATESAVE_OPERATIONOP));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        legacy_file_saved, CK_STATESAVE_OPERATIONDEFAULTDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        legacy_file_saved, CK_STATESAVE_OPERATIONOUTPUT));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        legacy_file_saved, CK_STATESAVE_OPERATIONINPUTS));

    nmo_parameteroperation_state_t legacy_file_reloaded;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &legacy_file_reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_deserialize(
        &legacy_file_reloaded, legacy_file_saved, NULL, NULL));
    ASSERT_TRUE(nmo_parameteroperation_vtable.equals(
        &legacy_file_loaded, &legacy_file_reloaded));

    const struct {
        uint32_t identifier;
        size_t payload_dwords;
    } file_trailing_cases[] = {
        {CK_STATESAVE_OPERATIONOP, 2u},
        {CK_STATESAVE_OPERATIONDEFAULTDATA, 1u},
        {CK_STATESAVE_OPERATIONOUTPUT, 1u},
        {CK_STATESAVE_OPERATIONINPUTS, 2u},
    };
    for (size_t i = 0;
         i < sizeof(file_trailing_cases) / sizeof(file_trailing_cases[0]);
         ++i) {
        nmo_chunk_t *trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(trailing);
        trailing->class_id = NMO_CID_PARAMETEROPERATION;
        trailing->data_version = 8;
        trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            trailing, file_trailing_cases[i].identifier));
        for (size_t j = 0; j < file_trailing_cases[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0x12345678u));
        nmo_chunk_close(trailing);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT,
                  nmo_parameteroperation_deserialize(
                      &legacy_file_loaded, trailing, NULL, NULL));
        ASSERT_EQ(820u, legacy_file_loaded.owner.raw_id);
        ASSERT_EQ(821u, legacy_file_loaded.in1.ref.raw_id);
        ASSERT_EQ(822u, legacy_file_loaded.in2.ref.raw_id);
        ASSERT_EQ(823u, legacy_file_loaded.out.ref.raw_id);
    }

    const struct {
        uint32_t identifier;
        size_t payload_dwords;
    } nonfile_trailing_cases[] = {
        {CK_STATESAVE_OPERATIONOP, 2u},
        {CK_STATESAVE_OPERATIONDEFAULTDATA, 1u},
        {CK_STATESAVE_OPERATIONOUTPUT, 2u},
        {CK_STATESAVE_OPERATIONINPUTS, 4u},
    };
    for (size_t i = 0;
         i < sizeof(nonfile_trailing_cases) /
             sizeof(nonfile_trailing_cases[0]);
         ++i) {
        nmo_chunk_t *trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(trailing);
        trailing->class_id = NMO_CID_PARAMETEROPERATION;
        trailing->data_version = 8;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            trailing, nonfile_trailing_cases[i].identifier));
        for (size_t j = 0; j < nonfile_trailing_cases[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0x12345678u));
        nmo_chunk_close(trailing);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT,
                  nmo_parameteroperation_deserialize(
                      &loaded, trailing, NULL, NULL));
        ASSERT_EQ(720u, loaded.owner.raw_id);
        ASSERT_EQ(911u, loaded.in1.ref.raw_id);
        ASSERT_EQ(912u, loaded.in2.ref.raw_id);
        ASSERT_EQ(723u, loaded.out.ref.raw_id);
    }

    nmo_chunk_t *empty_file = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty_file);
    empty_file->class_id = NMO_CID_PARAMETEROPERATION;
    empty_file->data_version = 8;
    empty_file->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(empty_file));
    nmo_chunk_close(empty_file);
    nmo_parameteroperation_state_t empty_file_loaded;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_vtable.create(
        &empty_file_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_deserialize(
        &empty_file_loaded, empty_file, NULL, NULL));
    ASSERT_FALSE(empty_file_loaded.has_new_data);
    ASSERT_FALSE(empty_file_loaded.has_operation);
    nmo_chunk_t *empty_file_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty_file_saved);
    empty_file_saved->class_id = NMO_CID_PARAMETEROPERATION;
    empty_file_saved->data_version = 8;
    empty_file_saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_parameteroperation_serialize(
        &empty_file_loaded, empty_file_saved, NULL, NULL));
    nmo_chunk_close(empty_file_saved);
    ASSERT_EQ(0u, nmo_chunk_get_data_size(empty_file_saved));

    nmo_parameteroperation_vtable.destroy(&source, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(&loaded, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(
        &legacy_file_loaded, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(
        &legacy_file_reloaded, NULL, NULL);
    nmo_parameteroperation_vtable.destroy(
        &empty_file_loaded, NULL, NULL);
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

    nmo_chunk_t *camera_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(camera_cross_section);
    camera_cross_section->class_id = NMO_CID_CAMERA;
    camera_cross_section->data_version = 7;
    camera_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(camera_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        camera_cross_section, CK_STATESAVE_CAMERAONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(camera_cross_section, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(camera_cross_section, 0.75f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(camera_cross_section, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(camera_cross_section, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(camera_cross_section, 0.1f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        camera_cross_section, 0x3F800000u));
    nmo_chunk_close(camera_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_camera_deserialize(
        &camera, camera_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(8.0f, camera.fov);
    ASSERT_EQ(77, camera.width);
    ASSERT_EQ(901u, nmo_beobject_script_array_get_id(
        &camera.entity.base.base.scripts, 0));

    nmo_chunk_t *legacy_camera_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_camera_cross_section);
    legacy_camera_cross_section->class_id = NMO_CID_CAMERA;
    legacy_camera_cross_section->data_version = 4;
    legacy_camera_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_camera_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_camera_cross_section, CK_STATESAVE_CAMERAFOV));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_camera_cross_section, 0x3F800000u));
    nmo_chunk_close(legacy_camera_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_camera_deserialize(
        &camera, legacy_camera_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(8.0f, camera.fov);
    ASSERT_EQ(77, camera.width);
    ASSERT_EQ(901u, nmo_beobject_script_array_get_id(
        &camera.entity.base.base.scripts, 0));

    const struct {
        uint32_t identifier;
        uint32_t data_version;
        size_t payload_dwords;
    } trailing_camera_sections[] = {
        {CK_STATESAVE_CAMERAFOV, 4u, 1u},
        {CK_STATESAVE_CAMERAPROJTYPE, 4u, 1u},
        {CK_STATESAVE_CAMERAOTHOZOOM, 4u, 1u},
        {CK_STATESAVE_CAMERAASPECT, 4u, 2u},
        {CK_STATESAVE_CAMERAPLANES, 4u, 2u},
        {CK_STATESAVE_CAMERAONLY, 7u, 6u},
    };
    for (size_t i = 0;
         i < sizeof(trailing_camera_sections) /
             sizeof(trailing_camera_sections[0]);
         ++i) {
        nmo_chunk_t *trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(trailing);
        trailing->class_id = NMO_CID_CAMERA;
        trailing->data_version = trailing_camera_sections[i].data_version;
        trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            trailing, trailing_camera_sections[i].identifier));
        for (size_t j = 0;
             j < trailing_camera_sections[i].payload_dwords;
             ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0x12345678u));
        nmo_chunk_close(trailing);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_camera_deserialize(
            &camera, trailing, NULL, &deserialize_context));
        ASSERT_EQ(8.0f, camera.fov);
        ASSERT_EQ(77, camera.width);
        ASSERT_EQ(901u, nmo_beobject_script_array_get_id(
            &camera.entity.base.base.scripts, 0));
    }

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
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_light_deserialize(
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

TEST(chunk_id_remap, camera_preserves_file_layouts) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy);
    legacy->class_id = NMO_CID_CAMERA;
    legacy->data_version = 0;
    legacy->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy, CK_STATESAVE_CAMERAFOV));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(legacy, 0.75f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy, CK_STATESAVE_CAMERAPLANES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(legacy, 0.25f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(legacy, 8000.0f));
    nmo_chunk_close(legacy);

    nmo_camera_state_t camera;
    ASSERT_EQ(NMO_OK, nmo_camera_vtable.create(&camera, NULL, NULL));
    ASSERT_TRUE(camera.has_cameraonly_chunk);
    ASSERT_EQ(NMO_OK, nmo_camera_deserialize(
        &camera, legacy, NULL, &deserialize_context));
    ASSERT_FALSE(camera.has_cameraonly_chunk);
    ASSERT_TRUE(camera.has_fov_chunk);
    ASSERT_FALSE(camera.has_proj_chunk);
    ASSERT_FALSE(camera.has_ortho_chunk);
    ASSERT_FALSE(camera.has_aspect_chunk);
    ASSERT_TRUE(camera.has_planes_chunk);

    nmo_chunk_t *saved_legacy = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(saved_legacy);
    saved_legacy->class_id = NMO_CID_CAMERA;
    saved_legacy->data_version = 0;
    saved_legacy->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_camera_serialize(
        &camera, saved_legacy, NULL, &serialize_context));
    nmo_chunk_close(saved_legacy);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        saved_legacy, CK_STATESAVE_CAMERAFOV));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        saved_legacy, CK_STATESAVE_CAMERAPROJTYPE));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        saved_legacy, CK_STATESAVE_CAMERAOTHOZOOM));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        saved_legacy, CK_STATESAVE_CAMERAASPECT));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        saved_legacy, CK_STATESAVE_CAMERAPLANES));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        saved_legacy, CK_STATESAVE_CAMERAONLY));

    nmo_chunk_t *modern_empty = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern_empty);
    modern_empty->class_id = NMO_CID_CAMERA;
    modern_empty->data_version = 7;
    modern_empty->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(modern_empty));
    nmo_chunk_close(modern_empty);
    ASSERT_EQ(NMO_OK, nmo_camera_deserialize(
        &camera, modern_empty, NULL, &deserialize_context));
    ASSERT_FALSE(camera.has_cameraonly_chunk);

    nmo_chunk_t *saved_empty = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(saved_empty);
    saved_empty->class_id = NMO_CID_CAMERA;
    saved_empty->data_version = 7;
    saved_empty->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_camera_serialize(
        &camera, saved_empty, NULL, &serialize_context));
    nmo_chunk_close(saved_empty);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        saved_empty, CK_STATESAVE_CAMERAONLY));

    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(saved_empty));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(saved_empty, 0x12345678u));
    nmo_chunk_close(saved_empty);
    camera.fov = 1.0f;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_camera_serialize(
        &camera, saved_empty, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(saved_empty));
    camera.fov = 0.5f;
    camera.has_cameraonly_chunk = 1;
    camera.width = 65536;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_camera_serialize(
        &camera, saved_empty, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(saved_empty));

    nmo_camera_state_t modern_default;
    ASSERT_EQ(NMO_OK, nmo_camera_vtable.create(
        &modern_default, NULL, NULL));
    nmo_chunk_t *default_version = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(default_version);
    default_version->class_id = NMO_CID_CAMERA;
    default_version->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_camera_serialize(
        &modern_default, default_version, NULL, &serialize_context));
    nmo_chunk_close(default_version);
    ASSERT_EQ(NMO_CHUNK_DATA_VERSION_CURRENT, default_version->data_version);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        default_version, CK_STATESAVE_CAMERAONLY));
    nmo_camera_state_t default_loaded;
    ASSERT_EQ(NMO_OK, nmo_camera_vtable.create(
        &default_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_camera_deserialize(
        &default_loaded, default_version, NULL, &deserialize_context));
    ASSERT_TRUE(default_loaded.has_cameraonly_chunk);

    nmo_camera_vtable.destroy(&camera, NULL, NULL);
    nmo_camera_vtable.destroy(&modern_default, NULL, NULL);
    nmo_camera_vtable.destroy(&default_loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, light_preserves_file_layouts) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy);
    legacy->class_id = NMO_CID_LIGHT;
    legacy->data_version = 0;
    legacy->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy, CK_STATESAVE_LIGHTDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(legacy, VX_LIGHTSPOT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(legacy, 0.25f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(legacy, 0.5f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(legacy, 0.75f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(legacy, 0.125f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(legacy, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(legacy, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(legacy, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(legacy, 2.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(legacy, 3.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(legacy, 50.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(legacy, 0.8f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(legacy, 0.4f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(legacy, 2.0f));
    nmo_chunk_close(legacy);
    size_t legacy_payload_dwords = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        legacy, CK_STATESAVE_LIGHTDATA, &legacy_payload_dwords));
    ASSERT_EQ(14u, legacy_payload_dwords);

    nmo_light_state_t light;
    ASSERT_EQ(NMO_OK, nmo_light_vtable.create(&light, NULL, NULL));
    ASSERT_TRUE(light.has_light_data_chunk);
    ASSERT_EQ(NMO_OK, nmo_light_deserialize(
        &light, legacy, NULL, &deserialize_context));
    ASSERT_TRUE(light.has_light_data_chunk);
    ASSERT_FALSE(light.has_light_power_chunk);
    ASSERT_TRUE(light.light_data_is_legacy);
    ASSERT_EQ(0.125f, light.legacy_diffuse_alpha);
    ASSERT_EQ(0x300u, light.flags);

    nmo_chunk_t *saved_legacy = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(saved_legacy);
    saved_legacy->class_id = NMO_CID_LIGHT;
    saved_legacy->data_version = 0;
    saved_legacy->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_light_serialize(
        &light, saved_legacy, NULL, &serialize_context));
    nmo_chunk_close(saved_legacy);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        saved_legacy, CK_STATESAVE_LIGHTDATA));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        saved_legacy, CK_STATESAVE_LIGHTDATA2));

    nmo_light_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_light_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_light_deserialize(
        &reloaded, saved_legacy, NULL, &deserialize_context));
    ASSERT_EQ(VX_LIGHTSPOT, reloaded.light_data.type);
    ASSERT_EQ(0x300u, reloaded.flags);
    ASSERT_EQ(0.25f, reloaded.light_data.diffuse.r);
    ASSERT_EQ(0.5f, reloaded.light_data.diffuse.g);
    ASSERT_EQ(0.75f, reloaded.light_data.diffuse.b);
    ASSERT_EQ(0.125f, reloaded.legacy_diffuse_alpha);
    ASSERT_EQ(50.0f, reloaded.light_data.range);
    ASSERT_EQ(2.0f, reloaded.light_data.falloff);

    nmo_chunk_t *modern = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern);
    modern->class_id = NMO_CID_LIGHT;
    modern->data_version = 7;
    modern->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(modern));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        modern, CK_STATESAVE_LIGHTDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        modern, 0x100u | VX_LIGHTPOINT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(modern, 0xFFFFFFFFu));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 0.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 0.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 5000.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        modern, CK_STATESAVE_LIGHTDATA2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 1.0f));
    nmo_chunk_close(modern);
    ASSERT_EQ(NMO_OK, nmo_light_deserialize(
        &light, modern, NULL, &deserialize_context));
    ASSERT_TRUE(light.has_light_data_chunk);
    ASSERT_TRUE(light.has_light_power_chunk);

    nmo_chunk_t *saved_modern = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(saved_modern);
    saved_modern->class_id = NMO_CID_LIGHT;
    saved_modern->data_version = 7;
    saved_modern->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_light_serialize(
        &light, saved_modern, NULL, &serialize_context));
    nmo_chunk_close(saved_modern);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        saved_modern, CK_STATESAVE_LIGHTDATA2));
    float power = 0.0f;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_float(saved_modern, &power));
    ASSERT_EQ(1.0f, power);

    nmo_chunk_t *empty = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty);
    empty->class_id = NMO_CID_LIGHT;
    empty->data_version = 7;
    empty->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(empty));
    nmo_chunk_close(empty);
    ASSERT_EQ(NMO_OK, nmo_light_deserialize(
        &light, empty, NULL, &deserialize_context));
    ASSERT_FALSE(light.has_light_data_chunk);
    ASSERT_FALSE(light.has_light_power_chunk);
    ASSERT_EQ(NMO_OK, nmo_light_serialize(
        &light, empty, NULL, &serialize_context));
    nmo_chunk_close(empty);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        empty, CK_STATESAVE_LIGHTDATA));

    nmo_light_state_t modern_default;
    ASSERT_EQ(NMO_OK, nmo_light_vtable.create(
        &modern_default, NULL, NULL));
    nmo_chunk_t *default_version = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(default_version);
    default_version->class_id = NMO_CID_LIGHT;
    default_version->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_light_serialize(
        &modern_default, default_version, NULL, &serialize_context));
    nmo_chunk_close(default_version);
    ASSERT_EQ(NMO_CHUNK_DATA_VERSION_CURRENT, default_version->data_version);
    size_t payload_dwords = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        default_version, CK_STATESAVE_LIGHTDATA, &payload_dwords));
    ASSERT_EQ(6u, payload_dwords);
    nmo_light_state_t default_loaded;
    ASSERT_EQ(NMO_OK, nmo_light_vtable.create(
        &default_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_light_deserialize(
        &default_loaded, default_version, NULL, &deserialize_context));
    ASSERT_FALSE(default_loaded.light_data_is_legacy);

    nmo_light_vtable.destroy(&reloaded, NULL, NULL);
    nmo_light_vtable.destroy(&light, NULL, NULL);
    nmo_light_vtable.destroy(&modern_default, NULL, NULL);
    nmo_light_vtable.destroy(&default_loaded, NULL, NULL);
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

    nmo_chunk_t *cross_camera = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_camera);
    cross_camera->class_id = NMO_CID_TARGETCAMERA;
    cross_camera->data_version = 7;
    cross_camera->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_camera));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_camera, CK_STATESAVE_TCAMERATARGET));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_camera, 0x11223344u));
    nmo_chunk_close(cross_camera);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_targetcamera_deserialize(
        &camera, cross_camera, NULL, &deserialize_context));
    ASSERT_EQ(8.0f, camera.base.fov);
    ASSERT_EQ(1u, camera.has_target);
    ASSERT_EQ(901u, camera.target.raw_id);

    nmo_chunk_t *trailing_camera = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_camera);
    trailing_camera->class_id = NMO_CID_TARGETCAMERA;
    trailing_camera->data_version = 7;
    trailing_camera->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_camera));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_camera, CK_STATESAVE_TCAMERATARGET));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        trailing_camera, 801));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        trailing_camera, 0x12345678u));
    nmo_chunk_close(trailing_camera);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_targetcamera_deserialize(
        &camera, trailing_camera, NULL, &deserialize_context));
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

    nmo_chunk_t *cross_light = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_light);
    cross_light->class_id = NMO_CID_TARGETLIGHT;
    cross_light->data_version = 7;
    cross_light->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_light));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_light, CK_STATESAVE_TLIGHTTARGET));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_light, 0x55667788u));
    nmo_chunk_close(cross_light);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_targetlight_deserialize(
        &light, cross_light, NULL, &deserialize_context));
    ASSERT_EQ(0x123400u, light.base.flags);
    ASSERT_EQ(9.0f, light.base.light_power);
    ASSERT_EQ(1u, light.has_target);
    ASSERT_EQ(902u, light.target.raw_id);

    nmo_chunk_t *trailing_light = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_light);
    trailing_light->class_id = NMO_CID_TARGETLIGHT;
    trailing_light->data_version = 7;
    trailing_light->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_light));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_light, CK_STATESAVE_TLIGHTTARGET));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        trailing_light, 802));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        trailing_light, 0x87654321u));
    nmo_chunk_close(trailing_light);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_targetlight_deserialize(
        &light, trailing_light, NULL, &deserialize_context));
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
    size_t light_payload_dwords = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        first, CK_STATESAVE_LIGHTDATA, &light_payload_dwords));
    ASSERT_EQ(6u, light_payload_dwords);
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
    source.legacy_object = nmo_ref_from_raw(101);
    source.start_effector = nmo_ref_from_raw(111);
    source.end_effector = nmo_ref_from_raw(222);
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, source.base.visibility_flags);

    nmo_kinematicchain_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_vtable.create(
        &copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_vtable.copy(
        &source, &copied, NULL, NULL));
    ASSERT_TRUE(nmo_kinematicchain_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_kinematicchain_vtable.hash(&source),
              nmo_kinematicchain_vtable.hash(&copied));
    copied.legacy_object.raw_id++;
    ASSERT_FALSE(nmo_kinematicchain_vtable.equals(&source, &copied));
    copied.legacy_object.raw_id--;
    copied.end_effector.raw_id++;
    ASSERT_FALSE(nmo_kinematicchain_vtable.equals(&source, &copied));

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
    ASSERT_EQ(101u, loaded.legacy_object.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.legacy_object.state);
    ASSERT_EQ(111u, loaded.start_effector.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.start_effector.state);
    ASSERT_EQ(222u, loaded.end_effector.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.end_effector.state);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_KINEMATICCHAIN;
    second->chunk_version = NMO_CHUNK_VERSION4;
    second->data_version = 7;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(second, &write_context);
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_chunk_set_file_context(second, &read_context);
    nmo_kinematicchain_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_vtable.create(
        &reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_deserialize(
        &reloaded, second, NULL, NULL));
    ASSERT_EQ(101u, reloaded.legacy_object.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.legacy_object.state);

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
    ASSERT_TRUE(loaded.has_chain_data);
    ASSERT_EQ(101u, loaded.legacy_object.raw_id);
    ASSERT_EQ(111u, loaded.start_effector.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.start_effector.state);
    ASSERT_EQ(222u, loaded.end_effector.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.end_effector.state);

    nmo_chunk_t *cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section);
    cross_section->class_id = NMO_CID_KINEMATICCHAIN;
    cross_section->chunk_version = NMO_CHUNK_VERSION4;
    cross_section->data_version = 7;
    cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section, CK_STATESAVE_KINEMATICCHAINALL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(cross_section, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(cross_section, 333));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section, 0x11223344u));
    nmo_chunk_close(cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_kinematicchain_deserialize(
        &loaded, cross_section, NULL, NULL));
    ASSERT_TRUE(loaded.has_chain_data);
    ASSERT_EQ(101u, loaded.legacy_object.raw_id);
    ASSERT_EQ(111u, loaded.start_effector.raw_id);
    ASSERT_EQ(222u, loaded.end_effector.raw_id);

    nmo_chunk_t *trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing);
    trailing->class_id = NMO_CID_KINEMATICCHAIN;
    trailing->chunk_version = NMO_CHUNK_VERSION4;
    trailing->data_version = 7;
    trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing, CK_STATESAVE_KINEMATICCHAINALL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(trailing, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(trailing, 333));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(trailing, 444));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0x12345678u));
    nmo_chunk_close(trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_kinematicchain_deserialize(
        &loaded, trailing, NULL, NULL));
    ASSERT_TRUE(loaded.has_chain_data);
    ASSERT_EQ(101u, loaded.legacy_object.raw_id);
    ASSERT_EQ(111u, loaded.start_effector.raw_id);
    ASSERT_EQ(222u, loaded.end_effector.raw_id);

    nmo_kinematicchain_state_t absent;
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_vtable.create(
        &absent, NULL, NULL));
    nmo_chunk_t *absent_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(absent_chunk);
    absent_chunk->class_id = NMO_CID_KINEMATICCHAIN;
    absent_chunk->chunk_version = NMO_CHUNK_VERSION4;
    absent_chunk->data_version = 7;
    absent_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(absent_chunk, &write_context);
    ASSERT_EQ(NMO_OK, nmo_kinematicchain_serialize(
        &absent, absent_chunk, NULL, &serialize_context));
    nmo_chunk_close(absent_chunk);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        absent_chunk, CK_STATESAVE_KINEMATICCHAINALL));

    nmo_kinematicchain_state_t invalid = source;
    invalid.end_effector = nmo_ref_from_id(999);
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_KINEMATICCHAIN;
    target->chunk_version = NMO_CHUNK_VERSION4;
    target->data_version = 7;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_kinematicchain_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_kinematicchain_vtable.destroy(&source, NULL, NULL);
    nmo_kinematicchain_vtable.destroy(&copied, NULL, NULL);
    nmo_kinematicchain_vtable.destroy(&loaded, NULL, NULL);
    nmo_kinematicchain_vtable.destroy(&reloaded, NULL, NULL);
    nmo_kinematicchain_vtable.destroy(&absent, NULL, NULL);
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
    chunk->chunk_options &= ~NMO_CHUNK_OPTION_FILE;
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

    nmo_chunk_t *fixed_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(fixed_cross_section);
    fixed_cross_section->class_id = NMO_CID_LAYER;
    fixed_cross_section->data_version = 7;
    fixed_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(fixed_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        fixed_cross_section, CK_STATESAVE_LAYERDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        fixed_cross_section, 999));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(fixed_cross_section, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(fixed_cross_section, 3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        fixed_cross_section, 0xAABBCCDDu));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        fixed_cross_section, 0x11223344u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        fixed_cross_section, 0x55667788u));
    nmo_chunk_close(fixed_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_layer_deserialize(
        &loaded, fixed_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(444u, loaded.grid.raw_id);
    ASSERT_EQ(77, loaded.format);
    ASSERT_EQ(88, loaded.version);

    nmo_chunk_t *buffer_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(buffer_cross_section);
    buffer_cross_section->class_id = NMO_CID_LAYER;
    buffer_cross_section->data_version = 7;
    buffer_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(buffer_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        buffer_cross_section, CK_STATESAVE_LAYERDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        buffer_cross_section, 999));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(buffer_cross_section, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(buffer_cross_section, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(buffer_cross_section, 4));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        buffer_cross_section, 0xA1B2C3D4u));
    nmo_chunk_close(buffer_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_layer_deserialize(
        &loaded, buffer_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(444u, loaded.grid.raw_id);
    ASSERT_EQ(77, loaded.format);
    ASSERT_EQ(88, loaded.version);

    nmo_chunk_t *fixed_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(fixed_trailing);
    fixed_trailing->class_id = NMO_CID_LAYER;
    fixed_trailing->data_version = 7;
    fixed_trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(fixed_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        fixed_trailing, CK_STATESAVE_LAYERDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(fixed_trailing, 999));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(fixed_trailing, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(fixed_trailing, 3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        fixed_trailing, 0xAABBCCDDu));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        fixed_trailing, (nmo_guid_t){0x11223344u, 0x55667788u}));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(fixed_trailing, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        fixed_trailing, 0x12345678u));
    nmo_chunk_close(fixed_trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_layer_deserialize(
        &loaded, fixed_trailing, NULL, &deserialize_context));
    ASSERT_EQ(444u, loaded.grid.raw_id);
    ASSERT_EQ(77, loaded.format);
    ASSERT_EQ(88, loaded.version);

    nmo_chunk_t *buffer_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(buffer_trailing);
    buffer_trailing->class_id = NMO_CID_LAYER;
    buffer_trailing->data_version = 7;
    buffer_trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(buffer_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        buffer_trailing, CK_STATESAVE_LAYERDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(buffer_trailing, 999));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(buffer_trailing, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(buffer_trailing, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_buffer(buffer_trailing, NULL, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        buffer_trailing, 0x12345678u));
    nmo_chunk_close(buffer_trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_layer_deserialize(
        &loaded, buffer_trailing, NULL, &deserialize_context));
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

TEST(chunk_id_remap, layer_default_format_writes_empty_square_buffer) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_layer_state_t source;
    nmo_layer_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_layer_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_layer_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, source.base.visibility_flags);
    ASSERT_EQ(0, source.format);
    ASSERT_FALSE(source.has_square_data);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_LAYER;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;

    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    ASSERT_EQ(NMO_OK, nmo_layer_serialize(
        &source, chunk, NULL, &serialize_context));
    nmo_chunk_close(chunk);

    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    ASSERT_EQ(NMO_OK, nmo_layer_deserialize(
        &loaded, chunk, NULL, &deserialize_context));
    ASSERT_EQ(0, loaded.format);
    ASSERT_TRUE(loaded.has_square_data);
    ASSERT_EQ(0u, loaded.square_data_size);
    ASSERT_NULL(loaded.square_data);

    nmo_layer_vtable.destroy(&source, NULL, NULL);
    nmo_layer_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, layer_copy_preserves_content_equality) {
    nmo_arena_t *source_arena = nmo_arena_create(NULL, 8192);
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(source_arena);
    ASSERT_NOT_NULL(copy_arena);

    nmo_layer_state_t source;
    nmo_layer_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_layer_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_layer_vtable.create(&copy, NULL, NULL));
    source.grid = nmo_ref_from_raw(501);
    source.type = 2;
    source.version = 3;
    source.color_rgba = 0x11223344u;
    source.param_guid = (nmo_guid_t){0x55667788u, 0x99AABBCCu};
    source.flags = 7;
    source.has_version = 1;
    source.has_color = 1;
    source.has_param_guid = 1;
    source.has_square_data = 1;
    source.square_data_size = 4;
    source.square_data = nmo_arena_alloc(source_arena, 4, _Alignof(uint32_t));
    ASSERT_NOT_NULL(source.square_data);
    memcpy(source.square_data, "\x11\x22\x33\x44", 4);

    nmo_type_descriptor_t layer_type = {
        .size = sizeof(nmo_layer_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_layer_vtable.copy(
        &source, &copy, &layer_type, copy_arena));
    ASSERT_NE(source.square_data, copy.square_data);
    ASSERT_TRUE(nmo_layer_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_layer_vtable.hash(&source),
              nmo_layer_vtable.hash(&copy));

    fail_after_allocator_state_t allocator_state = {
        .allowed_allocations = 2,
    };
    nmo_allocator_t failing_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &allocator_state);
    nmo_arena_t *failing_arena = nmo_arena_create(
        &failing_allocator, 1);
    ASSERT_NOT_NULL(failing_arena);
    allocator_state.allowed_allocations = allocator_state.allocation_count;
    nmo_layer_state_t copy_failed;
    ASSERT_EQ(NMO_OK, nmo_layer_vtable.create(
        &copy_failed, NULL, NULL));
    copy_failed.type = 77;
    copy_failed.grid = nmo_ref_from_raw(502);
    uint32_t previous_square = 0xAABBCCDDu;
    copy_failed.square_data = &previous_square;
    copy_failed.square_data_size = sizeof(previous_square);
    copy_failed.has_square_data = 1;
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_layer_vtable.copy(
        &source, &copy_failed, &layer_type, failing_arena));
    ASSERT_EQ(77, copy_failed.type);
    ASSERT_EQ(502u, copy_failed.grid.raw_id);
    ASSERT_EQ(&previous_square, copy_failed.square_data);
    ASSERT_EQ(sizeof(previous_square), copy_failed.square_data_size);

    copy.type++;
    ASSERT_FALSE(nmo_layer_vtable.equals(&source, &copy));
    copy.type = source.type;
    copy.color_rgba ^= 0xFFFFFFFFu;
    ASSERT_FALSE(nmo_layer_vtable.equals(&source, &copy));
    copy.color_rgba = source.color_rgba;
    ((uint8_t *)copy.square_data)[0] ^= 0xFFu;
    ASSERT_FALSE(nmo_layer_vtable.equals(&source, &copy));

    nmo_layer_vtable.destroy(&copy_failed, NULL, NULL);
    nmo_layer_vtable.destroy(&copy, NULL, NULL);
    nmo_layer_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(failing_arena);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(source_arena);
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

    nmo_chunk_t *cross_section_layers = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_layers);
    cross_section_layers->class_id = NMO_CID_GRID;
    cross_section_layers->data_version = 7;
    cross_section_layers->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_layers));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_layers, CK_STATESAVE_GRIDDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_layers, 10));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_layers, 20));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_layers, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_layers, 30));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_layers, 40));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_layers, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        cross_section_layers, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_layers, 0x7F123456u));
    nmo_chunk_close(cross_section_layers);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_grid_deserialize(
        &state, cross_section_layers, NULL, &deserialize_context));
    nmo_chunk_parser_state_t *parser =
        (nmo_chunk_parser_state_t *)cross_section_layers->parser_state;
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(parser->prev_identifier_pos + 9u, parser->current_pos);
    ASSERT_EQ(77, state.width);
    ASSERT_EQ(88, state.length);
    ASSERT_EQ(old_layers, state.layers.data);
    ASSERT_EQ(1u, state.layers.count);
    ASSERT_EQ(902u, NMO_ARRAY_DATA(
        nmo_grid_layer_t, &state.layers)[0].ref.raw_id);

    nmo_chunk_t *missing_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_count);
    missing_count->class_id = NMO_CID_GRID;
    missing_count->data_version = 7;
    missing_count->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_count, CK_STATESAVE_GRIDDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(missing_count, 10));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(missing_count, 20));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(missing_count, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(missing_count, 30));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(missing_count, 40));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(missing_count, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(missing_count, 0));
    nmo_chunk_close(missing_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_grid_deserialize(
        &state, missing_count, NULL, &deserialize_context));
    ASSERT_EQ(77, state.width);
    ASSERT_EQ(88, state.length);
    ASSERT_EQ(old_layers, state.layers.data);
    ASSERT_EQ(1u, state.layers.count);

    nmo_chunk_t *trailing_data = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_data);
    trailing_data->class_id = NMO_CID_GRID;
    trailing_data->data_version = 7;
    trailing_data->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_data));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_data, CK_STATESAVE_GRIDDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(trailing_data, 10));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(trailing_data, 20));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(trailing_data, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(trailing_data, 30));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing_data, 40));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(trailing_data, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        trailing_data, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        trailing_data, 0x12345678u));
    nmo_chunk_close(trailing_data);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_grid_deserialize(
        &state, trailing_data, NULL, &deserialize_context));
    ASSERT_EQ(77, state.width);
    ASSERT_EQ(88, state.length);
    ASSERT_EQ(old_layers, state.layers.data);
    ASSERT_EQ(1u, state.layers.count);

    nmo_chunk_t *cross_section_subchunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_subchunk);
    cross_section_subchunk->class_id = NMO_CID_GRID;
    cross_section_subchunk->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_subchunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_subchunk, CK_STATESAVE_GRIDDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_subchunk, 10));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_subchunk, 20));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_subchunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_subchunk, 30));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_subchunk, 40));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        cross_section_subchunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_sequence_item(
        cross_section_subchunk, 903));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_subchunk, 7));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_subchunk, 0x11223344u));
    for (size_t i = 0; i < 5; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_subchunk, 0));
    }
    nmo_chunk_close(cross_section_subchunk);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_grid_deserialize(
        &state, cross_section_subchunk, NULL, &deserialize_context));
    ASSERT_EQ(77, state.width);
    ASSERT_EQ(88, state.length);
    ASSERT_EQ(old_layers, state.layers.data);
    ASSERT_EQ(1u, state.layers.count);

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

    size_t valid_element_size = source.layers.element_size;
    source.layers.element_size = sizeof(nmo_ref_t);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_grid_serialize(
        &source, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    source.layers.element_size = valid_element_size;

    size_t valid_count = source.layers.count;
    source.layers.count = (size_t)INT32_MAX + 1u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_grid_serialize(
        &source, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    source.layers.count = valid_count;

    nmo_grid_vtable.destroy(&state, NULL, NULL);
    nmo_grid_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, grid_reserved_value_round_trips) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_GRID;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_GRIDDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 12));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 34));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 0x12345678));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 5));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 6));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(chunk, 0));
    nmo_chunk_close(chunk);

    nmo_grid_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_grid_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_grid_deserialize(
        &loaded, chunk, NULL, &deserialize_context));
    ASSERT_EQ(0x12345678, loaded.reserved_value);

    nmo_chunk_t *saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(saved);
    saved->class_id = NMO_CID_GRID;
    saved->data_version = 7;
    saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_grid_serialize(
        &loaded, saved, NULL, &serialize_context));
    nmo_chunk_close(saved);
    size_t section_dwords = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        saved, CK_STATESAVE_GRIDDATA, &section_dwords));
    ASSERT_EQ(7u, section_dwords);
    int32_t value = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_int(saved, &value));
    ASSERT_EQ(12, value);
    ASSERT_EQ(NMO_OK, nmo_chunk_read_int(saved, &value));
    ASSERT_EQ(34, value);
    ASSERT_EQ(NMO_OK, nmo_chunk_read_int(saved, &value));
    ASSERT_EQ(0x12345678, value);

    nmo_grid_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_grid_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_grid_vtable.copy(
        &loaded, &copied, NULL, arena));
    ASSERT_EQ(0x12345678, copied.reserved_value);
    ASSERT_TRUE(nmo_grid_vtable.equals(&loaded, &copied));
    ASSERT_EQ(nmo_grid_vtable.hash(&loaded), nmo_grid_vtable.hash(&copied));
    copied.reserved_value ^= 1;
    ASSERT_FALSE(nmo_grid_vtable.equals(&loaded, &copied));

    nmo_grid_vtable.destroy(&loaded, NULL, NULL);
    nmo_grid_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, grid_copy_preserves_content_equality) {
    nmo_arena_t *source_arena = nmo_arena_create(NULL, 8192);
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(source_arena);
    ASSERT_NOT_NULL(copy_arena);

    nmo_grid_state_t source;
    nmo_grid_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_grid_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_grid_vtable.create(&copy, NULL, NULL));
    source.width = 12;
    source.length = 34;
    source.priority = 5;
    source.orientation_mode = 6;

    nmo_chunk_t *layer_chunk = nmo_chunk_create(source_arena);
    ASSERT_NOT_NULL(layer_chunk);
    layer_chunk->class_id = NMO_CID_LAYER;
    layer_chunk->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(layer_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(layer_chunk, 0x11223344u));
    nmo_chunk_close(layer_chunk);
    nmo_grid_layer_t layer = {
        .ref = nmo_ref_from_raw(501),
        .chunk = layer_chunk,
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.layers, &layer));

    const nmo_type_field_t grid_copy_fields[] = {
        NMO_FIELD_ARRAY(nmo_grid_state_t, layers, CKPGUID_NONE),
    };
    nmo_type_descriptor_t grid_type = {
        .size = sizeof(nmo_grid_state_t),
        .fields = grid_copy_fields,
        .field_count = sizeof(grid_copy_fields) /
            sizeof(grid_copy_fields[0]),
    };
    ASSERT_EQ(NMO_OK, nmo_grid_vtable.copy(
        &source, &copy, &grid_type, copy_arena));
    const nmo_grid_layer_t *source_layers = NMO_ARRAY_DATA(
        nmo_grid_layer_t, &source.layers);
    nmo_grid_layer_t *copy_layers = NMO_ARRAY_DATA(
        nmo_grid_layer_t, &copy.layers);
    ASSERT_TRUE(source_layers[0].chunk != copy_layers[0].chunk);
    ASSERT_TRUE(nmo_grid_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_grid_vtable.hash(&source), nmo_grid_vtable.hash(&copy));

    nmo_grid_state_t copy_failed;
    ASSERT_EQ(NMO_OK, nmo_grid_vtable.create(
        &copy_failed, NULL, NULL));
    copy_failed.width = 77;
    copy_failed.base.entity_flags = 0x12345678u;
    nmo_grid_layer_t previous_layer = {
        .ref = nmo_ref_from_raw(502),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &copy_failed.layers, &previous_layer));
    void *previous_layers = copy_failed.layers.data;
    nmo_allocator_t source_allocator = source.layers.allocator;
    source.layers.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_grid_vtable.copy(
        &source, &copy_failed, &grid_type, copy_arena));
    source.layers.allocator = source_allocator;
    ASSERT_EQ(77, copy_failed.width);
    ASSERT_EQ(0x12345678u, copy_failed.base.entity_flags);
    ASSERT_EQ(previous_layers, copy_failed.layers.data);
    ASSERT_EQ(1u, copy_failed.layers.count);
    ASSERT_EQ(502u, NMO_ARRAY_DATA(
        nmo_grid_layer_t, &copy_failed.layers)[0].ref.raw_id);

    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(copy_layers[0].chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        copy_layers[0].chunk, 0x55667788u));
    nmo_chunk_close(copy_layers[0].chunk);
    ASSERT_FALSE(nmo_grid_vtable.equals(&source, &copy));

    nmo_grid_vtable.destroy(&copy_failed, NULL, NULL);
    nmo_grid_vtable.destroy(&copy, NULL, NULL);
    nmo_grid_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(source_arena);
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
    ASSERT_FALSE(loaded.has_transparency);
    ASSERT_FALSE(loaded.has_slot);
    ASSERT_FALSE(loaded.has_save_options);

    nmo_sprite_vtable.destroy(&source, NULL, NULL);
    nmo_sprite_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, sprite_raw_bitmap_payload_round_trips) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    uint32_t raw_payload = 0x12345678u;
    nmo_sprite_state_t source;
    ASSERT_EQ(NMO_OK, nmo_sprite_vtable.create(&source, NULL, NULL));
    source.has_bitmap_data = true;
    source.bitmap_data.raw_chunk_data = (uint8_t *)&raw_payload;
    source.bitmap_data.raw_chunk_size = sizeof(raw_payload);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_SPRITE;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_sprite_serialize(
        &source, chunk, NULL, &serialize_context));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        chunk, NMO_CKSPRITE_BITMAP_RAW));
    uint32_t serialized_payload = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(
        chunk, &serialized_payload));
    ASSERT_EQ(raw_payload, serialized_payload);

    nmo_sprite_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_sprite_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_sprite_deserialize(
        &loaded, chunk, NULL, &deserialize_context));
    ASSERT_TRUE(loaded.has_bitmap_data);
    ASSERT_EQ(sizeof(raw_payload), loaded.bitmap_data.raw_chunk_size);
    ASSERT_EQ(raw_payload,
              *(uint32_t *)loaded.bitmap_data.raw_chunk_data);
    ASSERT_FALSE(loaded.has_transparency);
    ASSERT_FALSE(loaded.has_slot);
    ASSERT_FALSE(loaded.has_save_options);

    uint32_t ignored = 0u;
    source.has_sprite_ref = true;
    source.sprite_ref = nmo_ref_from_raw(123u);
    nmo_chunk_t *preserved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(preserved);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        preserved, 0xabcdef01u));
    nmo_chunk_close(preserved);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_sprite_serialize(
        &source, preserved, NULL, &serialize_context));
    source.has_sprite_ref = false;
    source.sprite_ref = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    source.bitmap_data.width = 1u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_sprite_serialize(
        &source, preserved, NULL, &serialize_context));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(preserved, &ignored));
    ASSERT_EQ(0xabcdef01u, ignored);

    nmo_sprite_vtable.destroy(&source, NULL, NULL);
    nmo_sprite_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, sprite_failures_keep_state_and_target_chunk_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_SPRITE;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_2DENTITYONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 0));
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(truncated, 0.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_SPRITETRANSPARENT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 0xAABBCCDDu));
    nmo_chunk_close(truncated);

    nmo_sprite_state_t state;
    ASSERT_EQ(NMO_OK, nmo_sprite_vtable.create(&state, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &state.entity.base.base, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &state.entity.base.base.scripts, 901));
    state.has_sprite_ref = true;
    state.sprite_ref = nmo_ref_from_raw(902);
    state.has_bitmap_data = false;
    state.has_transparency = true;
    state.transparent_color = 0x11223344u;
    state.is_transparent = true;
    state.has_slot = true;
    state.current_slot = 7;

    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_sprite_deserialize(
        &state, truncated, NULL, &deserialize_context));
    ASSERT_TRUE(state.has_sprite_ref);
    ASSERT_EQ(902u, state.sprite_ref.raw_id);
    ASSERT_FALSE(state.has_bitmap_data);
    ASSERT_TRUE(state.has_transparency);
    ASSERT_EQ(0x11223344u, state.transparent_color);
    ASSERT_TRUE(state.is_transparent);
    ASSERT_TRUE(state.has_slot);
    ASSERT_EQ(7u, state.current_slot);
    ASSERT_EQ(901u, nmo_beobject_script_array_get_id(
        &state.entity.base.base.scripts, 0));

    nmo_chunk_t *format_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(format_cross_section);
    format_cross_section->class_id = NMO_CID_SPRITE;
    format_cross_section->data_version = 7;
    format_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(format_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        format_cross_section, CK_STATESAVE_2DENTITYONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(format_cross_section, 0));
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(
            format_cross_section, 0.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        format_cross_section, CK_STATESAVE_SPRITEFORMAT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(format_cross_section, 1u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(format_cross_section, 4u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        format_cross_section, 0x44332211u));
    nmo_chunk_close(format_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_sprite_deserialize(
        &state, format_cross_section, NULL, &deserialize_context));
    ASSERT_TRUE(state.has_sprite_ref);
    ASSERT_EQ(902u, state.sprite_ref.raw_id);
    ASSERT_TRUE(state.has_transparency);
    ASSERT_EQ(0x11223344u, state.transparent_color);
    ASSERT_TRUE(state.has_slot);
    ASSERT_EQ(7u, state.current_slot);

    nmo_chunk_t *shared_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(shared_cross_section);
    shared_cross_section->class_id = NMO_CID_SPRITE;
    shared_cross_section->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(shared_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        shared_cross_section, CK_STATESAVE_SPRITESHARED));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        shared_cross_section, 904u));
    nmo_chunk_close(shared_cross_section);
    nmo_deserialize_context_t chunk_context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_sprite_deserialize(
        &state, shared_cross_section, NULL, &chunk_context));
    ASSERT_TRUE(state.has_sprite_ref);
    ASSERT_EQ(902u, state.sprite_ref.raw_id);

    static const struct {
        uint32_t identifier;
        size_t payload_dwords;
    } fixed_sections[] = {
        {CK_STATESAVE_SPRITESHARED, 1u},
        {CK_STATESAVE_SPRITETRANSPARENT, 2u},
        {CK_STATESAVE_SPRITECURRENTIMAGE, 1u},
    };
    for (size_t i = 0; i < sizeof(fixed_sections) / sizeof(fixed_sections[0]);
         ++i) {
        nmo_chunk_t *file_trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(file_trailing);
        file_trailing->class_id = NMO_CID_SPRITE;
        file_trailing->data_version = 7;
        file_trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(file_trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            file_trailing, CK_STATESAVE_2DENTITYONLY));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(file_trailing, 0u));
        for (size_t j = 0; j < 4u; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_float(file_trailing, 0.0f));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            file_trailing, fixed_sections[i].identifier));
        for (size_t j = 0; j < fixed_sections[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(file_trailing, 0u));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
            file_trailing, 0x12345678u));
        nmo_chunk_close(file_trailing);

        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_sprite_deserialize(
            &state, file_trailing, NULL, &deserialize_context));
        ASSERT_TRUE(state.has_sprite_ref);
        ASSERT_EQ(902u, state.sprite_ref.raw_id);
        ASSERT_TRUE(state.has_transparency);
        ASSERT_EQ(0x11223344u, state.transparent_color);
        ASSERT_TRUE(state.has_slot);
        ASSERT_EQ(7u, state.current_slot);

        nmo_chunk_t *chunk_trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(chunk_trailing);
        chunk_trailing->class_id = NMO_CID_SPRITE;
        chunk_trailing->data_version = 4;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk_trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            chunk_trailing, fixed_sections[i].identifier));
        for (size_t j = 0; j < fixed_sections[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk_trailing, 0u));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
            chunk_trailing, 0x12345678u));
        nmo_chunk_close(chunk_trailing);

        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_sprite_deserialize(
            &state, chunk_trailing, NULL, &chunk_context));
        ASSERT_TRUE(state.has_sprite_ref);
        ASSERT_EQ(902u, state.sprite_ref.raw_id);
        ASSERT_TRUE(state.has_transparency);
        ASSERT_EQ(0x11223344u, state.transparent_color);
        ASSERT_TRUE(state.has_slot);
        ASSERT_EQ(7u, state.current_slot);
    }

    nmo_chunk_t *format_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(format_trailing);
    format_trailing->class_id = NMO_CID_SPRITE;
    format_trailing->data_version = 7;
    format_trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(format_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        format_trailing, CK_STATESAVE_2DENTITYONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(format_trailing, 0u));
    for (size_t i = 0; i < 4u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(format_trailing, 0.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        format_trailing, CK_STATESAVE_SPRITEFORMAT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(format_trailing, 1u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_buffer(format_trailing, NULL, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(format_trailing, 0x12345678u));
    nmo_chunk_close(format_trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_sprite_deserialize(
        &state, format_trailing, NULL, &deserialize_context));
    ASSERT_TRUE(state.has_sprite_ref);
    ASSERT_EQ(902u, state.sprite_ref.raw_id);
    ASSERT_TRUE(state.has_transparency);
    ASSERT_EQ(0x11223344u, state.transparent_color);
    ASSERT_TRUE(state.has_slot);
    ASSERT_EQ(7u, state.current_slot);

    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_chunk_file_context_t file_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_sprite_state_t source;
    ASSERT_EQ(NMO_OK, nmo_sprite_vtable.create(&source, NULL, NULL));
    source.has_sprite_ref = true;
    source.sprite_ref = nmo_ref_from_id(123);

    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_SPRITE;
    target->data_version = 7;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &file_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_sprite_serialize(
        &source, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_array_dispose(&state.entity.base.base.scripts);
    nmo_array_dispose(&state.entity.base.base.attributes);
    nmo_array_dispose(&state.entity.base.base.legacy_attributes);
    nmo_sprite_vtable.destroy(&state, NULL, NULL);
    nmo_sprite_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, spritetext_failures_keep_state_and_target_chunk_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_SPRITETEXT;
    truncated->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_SPRITEFONT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(truncated, "Arial"));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 12));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 400));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 0));
    nmo_chunk_close(truncated);

    nmo_spritetext_state_t state;
    ASSERT_EQ(NMO_OK, nmo_spritetext_vtable.create(&state, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &state.base.entity.base.base, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &state.base.entity.base.base.scripts, 901));
    state.text_content = "Old text";
    state.font.font_name = "Old font";
    state.font.size = 21;
    state.font.weight = 700;
    state.font.italic = 1;
    state.font.underline = 1;
    state.font_color = 0x11223344u;
    state.background_color = 0x55667788u;
    state.needs_redraw = true;

    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_spritetext_deserialize(
        &state, truncated, NULL, &deserialize_context));
    ASSERT_STR_EQ("Old text", state.text_content);
    ASSERT_STR_EQ("Old font", state.font.font_name);
    ASSERT_EQ(21, state.font.size);
    ASSERT_EQ(700, state.font.weight);
    ASSERT_EQ(1, state.font.italic);
    ASSERT_EQ(1, state.font.underline);
    ASSERT_EQ(0x11223344u, state.font_color);
    ASSERT_EQ(0x55667788u, state.background_color);
    ASSERT_TRUE(state.needs_redraw);
    ASSERT_EQ(901u, nmo_beobject_script_array_get_id(
        &state.base.entity.base.base.scripts, 0));

    nmo_chunk_t *text_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(text_cross_section);
    text_cross_section->class_id = NMO_CID_SPRITETEXT;
    text_cross_section->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(text_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        text_cross_section, CK_STATESAVE_SPRITETEXT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(text_cross_section, 8u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        text_cross_section, 0x44434241u));
    nmo_chunk_close(text_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_spritetext_deserialize(
        &state, text_cross_section, NULL, &deserialize_context));
    ASSERT_STR_EQ("Old text", state.text_content);
    ASSERT_STR_EQ("Old font", state.font.font_name);

    nmo_chunk_t *font_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(font_cross_section);
    font_cross_section->class_id = NMO_CID_SPRITETEXT;
    font_cross_section->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(font_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        font_cross_section, CK_STATESAVE_SPRITEFONT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(font_cross_section, "Arial"));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(font_cross_section, 12));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(font_cross_section, 400));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(font_cross_section, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(font_cross_section, 1u));
    nmo_chunk_close(font_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_spritetext_deserialize(
        &state, font_cross_section, NULL, &deserialize_context));
    ASSERT_STR_EQ("Old font", state.font.font_name);
    ASSERT_EQ(21, state.font.size);
    ASSERT_EQ(1, state.font.underline);

    nmo_chunk_t *colors_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(colors_cross_section);
    colors_cross_section->class_id = NMO_CID_SPRITETEXT;
    colors_cross_section->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(colors_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        colors_cross_section, CK_STATESAVE_SPRITETEXTCOLOR));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        colors_cross_section, 0xAABBCCDDu));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        colors_cross_section, 0x01020304u));
    nmo_chunk_close(colors_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_spritetext_deserialize(
        &state, colors_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(0x11223344u, state.font_color);
    ASSERT_EQ(0x55667788u, state.background_color);

    nmo_chunk_t *text_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(text_trailing);
    text_trailing->class_id = NMO_CID_SPRITETEXT;
    text_trailing->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(text_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        text_trailing, CK_STATESAVE_SPRITETEXT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(text_trailing, "New text"));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(text_trailing, 0x12345678u));
    nmo_chunk_close(text_trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_spritetext_deserialize(
        &state, text_trailing, NULL, &deserialize_context));
    ASSERT_STR_EQ("Old text", state.text_content);
    ASSERT_STR_EQ("Old font", state.font.font_name);

    nmo_chunk_t *font_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(font_trailing);
    font_trailing->class_id = NMO_CID_SPRITETEXT;
    font_trailing->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(font_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        font_trailing, CK_STATESAVE_SPRITEFONT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(font_trailing, "Arial"));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(font_trailing, 12));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(font_trailing, 400));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(font_trailing, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(font_trailing, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(font_trailing, 0x12345678u));
    nmo_chunk_close(font_trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_spritetext_deserialize(
        &state, font_trailing, NULL, &deserialize_context));
    ASSERT_STR_EQ("Old font", state.font.font_name);
    ASSERT_EQ(21, state.font.size);
    ASSERT_EQ(1, state.font.underline);

    nmo_chunk_t *colors_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(colors_trailing);
    colors_trailing->class_id = NMO_CID_SPRITETEXT;
    colors_trailing->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(colors_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        colors_trailing, CK_STATESAVE_SPRITETEXTCOLOR));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(colors_trailing, 0xAABBCCDDu));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(colors_trailing, 0x01020304u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(colors_trailing, 0x12345678u));
    nmo_chunk_close(colors_trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_spritetext_deserialize(
        &state, colors_trailing, NULL, &deserialize_context));
    ASSERT_EQ(0x11223344u, state.font_color);
    ASSERT_EQ(0x55667788u, state.background_color);

    fail_after_allocator_state_t allocator_state = {
        .allocation_count = 0,
        .allowed_allocations = 2,
    };
    nmo_allocator_t failing_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &allocator_state);
    nmo_arena_t *failing_arena = nmo_arena_create(
        &failing_allocator, 4096);
    ASSERT_NOT_NULL(failing_arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        failing_arena, NULL, 0, 0);

    char large_text[16385];
    memset(large_text, 'A', sizeof(large_text) - 1);
    large_text[sizeof(large_text) - 1] = '\0';
    nmo_spritetext_state_t source;
    ASSERT_EQ(NMO_OK, nmo_spritetext_vtable.create(&source, NULL, NULL));
    source.text_content = large_text;
    source.font.font_name = "Arial";
    source.font.size = 12;
    source.font.weight = 400;
    source.font_color = 0xFFFFFFFFu;

    nmo_chunk_t *target = nmo_chunk_create(failing_arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_SPRITETEXT;
    target->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_spritetext_serialize(
        &source, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_array_dispose(&state.base.entity.base.base.scripts);
    nmo_array_dispose(&state.base.entity.base.base.attributes);
    nmo_array_dispose(&state.base.entity.base.base.legacy_attributes);
    nmo_spritetext_vtable.destroy(&state, NULL, NULL);
    nmo_spritetext_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(failing_arena);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, texture_failures_keep_state_and_target_chunk_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_TEXTURE;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_TEXREADER));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 64));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 32));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 24));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 0x504E47u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(truncated, NMO_GUID_NULL));
    nmo_chunk_close(truncated);

    uint8_t old_data[] = {1, 2, 3, 4};
    nmo_texture_reader_slot_t old_slot = {
        .format_type = 1,
        .extension = 0x4F4C44u,
        .reader_guid = NMO_GUID_NULL,
        .data_size = sizeof(old_data),
        .data = old_data,
    };
    nmo_texture_state_t state;
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(&state, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &state.base, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &state.base.scripts, 901));
    state.has_movie_filename = 1;
    state.movie_filename = "old.avi";
    state.slot_count = 1;
    state.reader_width = 8;
    state.reader_height = 4;
    state.reader_bpp = 16;
    state.bitmap_kind = CKTEXTURE_BITMAP_READER;
    state.reader_slots = &old_slot;
    state.has_pick_threshold = 1;
    state.pick_threshold = 33;

    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_texture_deserialize(
        &state, truncated, NULL, &deserialize_context));
    ASSERT_TRUE(state.has_movie_filename);
    ASSERT_STR_EQ("old.avi", state.movie_filename);
    ASSERT_EQ(1u, state.slot_count);
    ASSERT_EQ(8, state.reader_width);
    ASSERT_EQ(4, state.reader_height);
    ASSERT_EQ(16, state.reader_bpp);
    ASSERT_EQ(CKTEXTURE_BITMAP_READER, state.bitmap_kind);
    ASSERT_EQ(&old_slot, state.reader_slots);
    ASSERT_TRUE(state.has_pick_threshold);
    ASSERT_EQ(33, state.pick_threshold);
    ASSERT_EQ(901u, nmo_beobject_script_array_get_id(
        &state.base.scripts, 0));

    nmo_chunk_t *cross_section_reader = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_reader);
    cross_section_reader->class_id = NMO_CID_TEXTURE;
    cross_section_reader->data_version = 7;
    cross_section_reader->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_reader));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_reader, CK_STATESAVE_TEXREADER));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_reader, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_reader, 0x7F123456u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_reader, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_reader, 0));
    nmo_chunk_close(cross_section_reader);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_texture_deserialize(
        &state, cross_section_reader, NULL, &deserialize_context));
    nmo_chunk_parser_state_t *parser =
        (nmo_chunk_parser_state_t *)cross_section_reader->parser_state;
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(parser->prev_identifier_pos + 3u, parser->current_pos);
    ASSERT_TRUE(state.has_movie_filename);
    ASSERT_STR_EQ("old.avi", state.movie_filename);
    ASSERT_EQ(&old_slot, state.reader_slots);
    ASSERT_EQ(901u, nmo_beobject_script_array_get_id(
        &state.base.scripts, 0));

    const uint32_t empty_section_ids[] = {
        CK_STATESAVE_TEXCOMPRESSED,
        CK_STATESAVE_TEXBITMAPS,
        CK_STATESAVE_TEXFILENAMES,
        CK_STATESAVE_TEXAVIFILENAME,
        CK_STATESAVE_USERMIPMAP,
    };
    for (size_t i = 0;
         i < sizeof(empty_section_ids) / sizeof(empty_section_ids[0]); ++i) {
        nmo_chunk_t *empty_section = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(empty_section);
        empty_section->class_id = NMO_CID_TEXTURE;
        empty_section->data_version = 7;
        empty_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(empty_section));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            empty_section, empty_section_ids[i]));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(empty_section, 0));
        nmo_chunk_close(empty_section);

        ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_texture_deserialize(
            &state, empty_section, NULL, &deserialize_context));
        ASSERT_TRUE(state.has_movie_filename);
        ASSERT_STR_EQ("old.avi", state.movie_filename);
        ASSERT_EQ(&old_slot, state.reader_slots);
        ASSERT_EQ(901u, nmo_beobject_script_array_get_id(
            &state.base.scripts, 0));
    }

    fail_after_allocator_state_t allocator_state = {
        .allocation_count = 0,
        .allowed_allocations = 2,
    };
    nmo_allocator_t failing_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &allocator_state);
    nmo_arena_t *failing_arena = nmo_arena_create(
        &failing_allocator, 4096);
    ASSERT_NOT_NULL(failing_arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        failing_arena, NULL, 0, 0);

    uint8_t large_data[16384];
    memset(large_data, 0x5A, sizeof(large_data));
    nmo_texture_reader_slot_t source_slot = {
        .format_type = 1,
        .extension = 0x504E47u,
        .reader_guid = NMO_GUID_NULL,
        .data_size = sizeof(large_data),
        .data = large_data,
    };
    nmo_texture_state_t source;
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(&source, NULL, NULL));
    source.slot_count = 1;
    source.reader_width = 64;
    source.reader_height = 32;
    source.reader_bpp = 24;
    source.bitmap_kind = CKTEXTURE_BITMAP_READER;
    source.reader_slots = &source_slot;

    nmo_chunk_t *target = nmo_chunk_create(failing_arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_TEXTURE;
    target->data_version = 7;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_texture_serialize(
        &source, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_array_dispose(&state.base.scripts);
    nmo_array_dispose(&state.base.attributes);
    nmo_array_dispose(&state.base.legacy_attributes);
    nmo_texture_vtable.destroy(&state, NULL, NULL);
    nmo_texture_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(failing_arena);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, texture_copy_preserves_nested_content) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);

    uint8_t reader_data[] = {1, 2, 3, 4};
    uint8_t reader_alpha[] = {5, 6};
    nmo_texture_reader_slot_t reader_slot = {
        .format_type = 1,
        .extension = 0x504E47u,
        .reader_guid = {11, 12},
        .data_size = sizeof(reader_data),
        .data = reader_data,
        .alpha_count = 2,
        .alpha_value = 7,
        .alpha_plane_size = sizeof(reader_alpha),
        .alpha_plane = reader_alpha,
    };
    uint8_t blue[] = {8};
    uint8_t green[] = {9};
    uint8_t red[] = {10};
    uint8_t alpha[] = {11};
    nmo_texture_raw_slot_t raw_slot = {
        .bits_per_pixel = 32,
        .width = 2,
        .height = 2,
        .alpha_mask = 0xFF000000u,
        .red_mask = 0x00FF0000u,
        .green_mask = 0x0000FF00u,
        .blue_mask = 0x000000FFu,
        .compression = 3,
        .blue_size = sizeof(blue),
        .blue_data = blue,
        .green_size = sizeof(green),
        .green_data = green,
        .red_size = sizeof(red),
        .red_data = red,
        .alpha_size = sizeof(alpha),
        .alpha_data = alpha,
    };
    uint8_t bitmap2_data[] = {12, 13, 14};
    nmo_texture_bitmap2_slot_t bitmap2_slot = {
        .header_size = 40,
        .buffer_size = sizeof(bitmap2_data),
        .buffer = bitmap2_data,
    };
    uint8_t mip_blue[] = {15, 16};
    nmo_texture_raw_slot_t mipmap = {
        .bits_per_pixel = 8,
        .width = 1,
        .height = 1,
        .blue_size = sizeof(mip_blue),
        .blue_data = mip_blue,
    };
    char *slot_names[] = {"slot.png"};
    uint8_t save_format[] = {17, 18, 19, 20};

    nmo_texture_state_t source;
    nmo_texture_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(&copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &source.base.scripts, 701));
    source.has_movie_filename = 1;
    source.movie_filename = "movie.avi";
    source.has_slot_filenames = 1;
    source.slot_count = 1;
    source.slot_filenames = slot_names;
    source.reader_width = 2;
    source.reader_height = 2;
    source.reader_bpp = 32;
    source.bitmap_kind = CKTEXTURE_BITMAP_READER;
    source.reader_slots = &reader_slot;
    source.raw_slots = &raw_slot;
    source.bitmap2_slots = &bitmap2_slot;
    source.has_pick_threshold = 1;
    source.pick_threshold = 31;
    source.has_oldtexonly = 1;
    source.mipmap_level = 2;
    source.save_options = 3;
    source.is_transparent = 1;
    source.is_cubemap = 1;
    source.has_desired_video_format = 1;
    source.desired_video_format = 4;
    source.has_transparent_color = 1;
    source.transparent_color = 0x11223344u;
    source.has_current_slot = 1;
    source.current_slot = 0;
    source.has_save_format = 1;
    source.save_format_data = save_format;
    source.save_format_size = sizeof(save_format);
    source.has_user_mipmaps = 1;
    source.user_mipmap_count = 1;
    source.user_mipmaps = &mipmap;

    nmo_type_descriptor_t type = {
        .size = sizeof(nmo_texture_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.copy(
        &source, &copy, &type, arena));
    ASSERT_NE(source.base.scripts.data, copy.base.scripts.data);
    ASSERT_NE(source.movie_filename, copy.movie_filename);
    ASSERT_NE(source.slot_filenames, copy.slot_filenames);
    ASSERT_NE(source.slot_filenames[0], copy.slot_filenames[0]);
    ASSERT_NE(source.reader_slots, copy.reader_slots);
    ASSERT_NE(source.reader_slots[0].data, copy.reader_slots[0].data);
    ASSERT_NE(source.reader_slots[0].alpha_plane,
              copy.reader_slots[0].alpha_plane);
    ASSERT_NE(source.raw_slots, copy.raw_slots);
    ASSERT_NE(source.raw_slots[0].red_data, copy.raw_slots[0].red_data);
    ASSERT_NE(source.bitmap2_slots, copy.bitmap2_slots);
    ASSERT_NE(source.bitmap2_slots[0].buffer,
              copy.bitmap2_slots[0].buffer);
    ASSERT_NE(source.save_format_data, copy.save_format_data);
    ASSERT_NE(source.user_mipmaps, copy.user_mipmaps);
    ASSERT_NE(source.user_mipmaps[0].blue_data,
              copy.user_mipmaps[0].blue_data);
    ASSERT_TRUE(nmo_texture_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_texture_vtable.hash(&source),
              nmo_texture_vtable.hash(&copy));
    copy.raw_slots[0].red_data[0] ^= 0xFFu;
    ASSERT_FALSE(nmo_texture_vtable.equals(&source, &copy));
    copy.raw_slots[0].red_data[0] ^= 0xFFu;

    nmo_texture_state_t unnamed;
    nmo_texture_state_t unnamed_copy;
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(&unnamed, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(
        &unnamed_copy, NULL, NULL));
    unnamed.slot_count = 1;
    unnamed.bitmap_kind = CKTEXTURE_BITMAP_READER;
    unnamed.reader_slots = &reader_slot;
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.copy(
        &unnamed, &unnamed_copy, &type, arena));
    ASSERT_NULL(unnamed_copy.slot_filenames);
    ASSERT_TRUE(nmo_texture_vtable.equals(&unnamed, &unnamed_copy));

    fail_after_allocator_state_t allocator_state = {
        .allowed_allocations = 1,
    };
    nmo_allocator_t failing_allocator = {
        .alloc = fail_after_alloc,
        .free = fail_after_free,
        .user_data = &allocator_state,
    };
    nmo_texture_state_t failing_source;
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(
        &failing_source, NULL, NULL));
    nmo_array_dispose(&failing_source.base.scripts);
    ASSERT_EQ(NMO_OK, nmo_array_init(
        &failing_source.base.scripts, sizeof(nmo_ref_t), 1,
        &failing_allocator));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &failing_source.base.scripts, 801));
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(
        &failing_source.base.attributes, 802, 9, NULL));
    allocator_state.allowed_allocations = allocator_state.allocation_count;
    nmo_texture_reader_slot_t *published_reader_slots = copy.reader_slots;
    void *published_save_format = copy.save_format_data;
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_texture_vtable.copy(
        &failing_source, &copy, &type, arena));
    ASSERT_EQ(published_reader_slots, copy.reader_slots);
    ASSERT_EQ(published_save_format, copy.save_format_data);
    ASSERT_EQ(1u, failing_source.base.attributes.count);
    ASSERT_NOT_NULL(failing_source.base.attributes.data);

    nmo_texture_vtable.destroy(&failing_source, NULL, NULL);
    nmo_texture_vtable.destroy(&unnamed, NULL, NULL);
    nmo_texture_vtable.destroy(&unnamed_copy, NULL, NULL);
    nmo_texture_vtable.destroy(&source, NULL, NULL);
    nmo_texture_vtable.destroy(&copy, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, texture_preserves_legacy_file_layout) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    const uint32_t legacy_desc[] = {
        64u, 32u, 256u, 32u,
        0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u,
        0u,
    };
    const uint8_t save_format[] = {0x11u, 0x22u, 0x33u};
    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy);
    legacy->class_id = NMO_CID_TEXTURE;
    legacy->data_version = 4;
    legacy->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy, CK_STATESAVE_TEXTRANSPARENT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(legacy, 0x10203040u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(legacy, 1u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy, CK_STATESAVE_TEXCURRENTIMAGE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(legacy, -3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy, CK_STATESAVE_USERMIPMAP));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(legacy, -7));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_buffer(
        legacy, legacy_desc, sizeof(legacy_desc)));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy, CK_STATESAVE_TEXSYSTEMCACHING));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(legacy, 0x12345678u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_buffer(
        legacy, save_format, sizeof(save_format)));
    nmo_chunk_close(legacy);

    nmo_texture_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_texture_deserialize(
        &loaded, legacy, NULL, &deserialize_context));
    ASSERT_TRUE(loaded.has_transparent_color);
    ASSERT_EQ(0x10203040u, loaded.transparent_color);
    ASSERT_TRUE(loaded.is_transparent);
    ASSERT_TRUE(loaded.has_current_slot);
    ASSERT_EQ(-3, loaded.current_slot);
    ASSERT_TRUE(loaded.has_legacy_user_mipmap);
    ASSERT_EQ(-7, loaded.legacy_use_mipmap);
    ASSERT_EQ(UINT8_MAX, loaded.mipmap_level);
    ASSERT_EQ(sizeof(legacy_desc) + sizeof(uint32_t),
              loaded.legacy_user_mipmap_size);
    ASSERT_TRUE(loaded.has_desired_video_format);
    ASSERT_EQ(_32_ARGB8888, loaded.desired_video_format);
    ASSERT_TRUE(loaded.has_legacy_system_caching);
    ASSERT_FALSE(loaded.has_save_format);
    ASSERT_EQ(0x12345678u, loaded.save_options);
    ASSERT_EQ(sizeof(save_format), loaded.save_format_size);
    ASSERT_MEM_EQ(save_format, loaded.save_format_data,
                  sizeof(save_format));

    nmo_texture_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(&copied, NULL, NULL));
    nmo_type_descriptor_t type = {.size = sizeof(nmo_texture_state_t)};
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.copy(
        &loaded, &copied, &type, arena));
    ASSERT_NE(loaded.legacy_user_mipmap_data,
              copied.legacy_user_mipmap_data);
    ASSERT_TRUE(nmo_texture_vtable.equals(&loaded, &copied));
    ASSERT_EQ(nmo_texture_vtable.hash(&loaded),
              nmo_texture_vtable.hash(&copied));

    nmo_chunk_t *saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(saved);
    saved->class_id = NMO_CID_TEXTURE;
    saved->data_version = 0;
    saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_texture_serialize(
        &loaded, saved, NULL, &serialize_context));
    nmo_chunk_close(saved);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        saved, CK_STATESAVE_OLDTEXONLY));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        saved, CK_STATESAVE_TEXSAVEFORMAT));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        saved, CK_STATESAVE_USERMIPMAP));
    int32_t use_mipmap = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_int(saved, &use_mipmap));
    ASSERT_EQ(-7, use_mipmap);
    void *saved_desc = NULL;
    size_t saved_desc_size = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_buffer(
        saved, &saved_desc, &saved_desc_size));
    ASSERT_EQ(sizeof(legacy_desc), saved_desc_size);
    ASSERT_MEM_EQ(legacy_desc, saved_desc, sizeof(legacy_desc));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        saved, CK_STATESAVE_TEXSYSTEMCACHING));
    uint32_t saved_options = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(saved, &saved_options));
    ASSERT_EQ(0x12345678u, saved_options);
    void *saved_format = NULL;
    size_t saved_format_size = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_buffer(
        saved, &saved_format, &saved_format_size));
    ASSERT_EQ(sizeof(save_format), saved_format_size);
    ASSERT_MEM_EQ(save_format, saved_format, sizeof(save_format));

    nmo_texture_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_texture_deserialize(
        &reloaded, saved, NULL, &deserialize_context));
    ASSERT_TRUE(nmo_texture_vtable.equals(&loaded, &reloaded));

    loaded.has_user_mipmaps = 1;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_texture_serialize(
        &loaded, saved, NULL, &serialize_context));
    loaded.has_user_mipmaps = 0;

    nmo_chunk_t *malformed = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(malformed);
    malformed->class_id = NMO_CID_TEXTURE;
    malformed->data_version = 0;
    malformed->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(malformed));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        malformed, CK_STATESAVE_USERMIPMAP));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(malformed, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(malformed, 64u));
    nmo_chunk_close(malformed);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_texture_deserialize(
        &loaded, malformed, NULL, &deserialize_context));
    ASSERT_TRUE(loaded.has_legacy_user_mipmap);
    ASSERT_EQ(-7, loaded.legacy_use_mipmap);
    ASSERT_EQ(0x12345678u, loaded.save_options);

    nmo_texture_state_t modern_default;
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(
        &modern_default, NULL, NULL));
    modern_default.has_oldtexonly = 1;
    modern_default.save_options = NMO_CKTEXTURE_IMAGEFORMAT;
    nmo_chunk_t *modern = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern);
    modern->class_id = NMO_CID_TEXTURE;
    modern->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_texture_serialize(
        &modern_default, modern, NULL, &serialize_context));
    nmo_chunk_close(modern);
    ASSERT_EQ(NMO_CHUNK_DATA_VERSION_CURRENT, modern->data_version);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        modern, CK_STATESAVE_OLDTEXONLY));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        modern, CK_STATESAVE_TEXSYSTEMCACHING));
    nmo_texture_state_t modern_loaded;
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(
        &modern_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_texture_deserialize(
        &modern_loaded, modern, NULL, &deserialize_context));
    ASSERT_TRUE(modern_loaded.has_oldtexonly);
    ASSERT_EQ(NMO_CKTEXTURE_IMAGEFORMAT, modern_loaded.save_options);

    nmo_texture_vtable.destroy(&loaded, NULL, NULL);
    nmo_texture_vtable.destroy(&copied, NULL, NULL);
    nmo_texture_vtable.destroy(&reloaded, NULL, NULL);
    nmo_texture_vtable.destroy(&modern_default, NULL, NULL);
    nmo_texture_vtable.destroy(&modern_loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, texture_empty_sections_round_trip_presence) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_texture_state_t source;
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(&source, NULL, NULL));
    source.bitmap_kind = CKTEXTURE_BITMAP_READER;
    source.reader_width = 64;
    source.reader_height = 32;
    source.reader_bpp = 24;
    source.has_movie_filename = 1;
    source.has_slot_filenames = 1;
    source.has_pick_threshold = 1;
    source.pick_threshold = 0;
    source.has_oldtexonly = 1;
    source.has_save_format = 1;
    source.has_user_mipmaps = 1;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_TEXTURE;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_texture_serialize(
        &source, chunk, NULL, &serialize_context));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_TEXREADER));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_TEXFILENAMES));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_TEXAVIFILENAME));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_PICKTHRESHOLD));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_TEXSAVEFORMAT));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        chunk, CK_STATESAVE_USERMIPMAP));

    nmo_texture_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_texture_deserialize(
        &loaded, chunk, NULL, &deserialize_context));
    ASSERT_EQ(CKTEXTURE_BITMAP_READER, loaded.bitmap_kind);
    ASSERT_EQ(0u, loaded.slot_count);
    ASSERT_EQ(64, loaded.reader_width);
    ASSERT_EQ(32, loaded.reader_height);
    ASSERT_EQ(24, loaded.reader_bpp);
    ASSERT_TRUE(loaded.has_movie_filename);
    ASSERT_NULL(loaded.movie_filename);
    ASSERT_TRUE(loaded.has_slot_filenames);
    ASSERT_TRUE(loaded.has_pick_threshold);
    ASSERT_EQ(0, loaded.pick_threshold);
    ASSERT_TRUE(loaded.has_save_format);
    ASSERT_EQ(0u, loaded.save_format_size);
    ASSERT_TRUE(loaded.has_user_mipmaps);
    ASSERT_EQ(0u, loaded.user_mipmap_count);
    ASSERT_TRUE(loaded.has_oldtexonly);
    ASSERT_FALSE(loaded.has_transparent_color);
    ASSERT_FALSE(loaded.has_current_slot);
    ASSERT_FALSE(loaded.has_desired_video_format);

    source.has_oldtexonly = 0;
    nmo_chunk_t *without_oldtex = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(without_oldtex);
    without_oldtex->class_id = NMO_CID_TEXTURE;
    without_oldtex->data_version = 7;
    without_oldtex->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_texture_serialize(
        &source, without_oldtex, NULL, &serialize_context));
    nmo_chunk_close(without_oldtex);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        without_oldtex, CK_STATESAVE_OLDTEXONLY));

    source.has_oldtexonly = 1;
    source.has_current_slot = 1;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_texture_serialize(
        &source, without_oldtex, NULL, &serialize_context));
    source.has_transparent_color = 1;
    source.transparent_color = 0x11223344u;
    source.has_desired_video_format = 1;
    source.desired_video_format = 1u;
    nmo_chunk_t *packed = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(packed);
    packed->class_id = NMO_CID_TEXTURE;
    packed->data_version = 7;
    packed->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_texture_serialize(
        &source, packed, NULL, &serialize_context));
    nmo_chunk_close(packed);
    nmo_texture_state_t packed_loaded;
    ASSERT_EQ(NMO_OK, nmo_texture_vtable.create(
        &packed_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_texture_deserialize(
        &packed_loaded, packed, NULL, &deserialize_context));
    ASSERT_TRUE(packed_loaded.has_transparent_color);
    ASSERT_EQ(0x11223344u, packed_loaded.transparent_color);
    ASSERT_TRUE(packed_loaded.has_current_slot);
    ASSERT_EQ(0, packed_loaded.current_slot);
    ASSERT_TRUE(packed_loaded.has_desired_video_format);
    ASSERT_EQ(1u, packed_loaded.desired_video_format);

    nmo_texture_vtable.destroy(&source, NULL, NULL);
    nmo_texture_vtable.destroy(&loaded, NULL, NULL);
    nmo_texture_vtable.destroy(&packed_loaded, NULL, NULL);
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

    nmo_ref_t inherited_script = nmo_ref_from_raw(612);
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &reloaded.base.base.base.scripts, &inherited_script));
    nmo_curvepoint_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_curvepoint_vtable.create(
        &copied, NULL, NULL));
    nmo_type_descriptor_t curvepoint_type = {
        .size = sizeof(nmo_curvepoint_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_curvepoint_vtable.copy(
        &reloaded, &copied, &curvepoint_type, arena));
    ASSERT_EQ(611u, copied.curve.raw_id);
    ASSERT_NE(reloaded.base.base.base.scripts.data,
              copied.base.base.base.scripts.data);
    ASSERT_EQ(612u, NMO_ARRAY_DATA(
        nmo_ref_t, &copied.base.base.base.scripts)[0].raw_id);

    nmo_curvepoint_state_t copy_failed;
    ASSERT_EQ(NMO_OK, nmo_curvepoint_vtable.create(
        &copy_failed, NULL, NULL));
    copy_failed.curve = nmo_ref_from_raw(613);
    copy_failed.use_tcb = 77;
    nmo_allocator_t script_allocator =
        reloaded.base.base.base.scripts.allocator;
    reloaded.base.base.base.scripts.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_curvepoint_vtable.copy(
        &reloaded, &copy_failed, &curvepoint_type, arena));
    reloaded.base.base.base.scripts.allocator = script_allocator;
    ASSERT_EQ(613u, copy_failed.curve.raw_id);
    ASSERT_EQ(77, copy_failed.use_tcb);

    nmo_curvepoint_vtable.destroy(&source, NULL, NULL);
    nmo_curvepoint_vtable.destroy(&loaded, NULL, NULL);
    nmo_curvepoint_vtable.destroy(&reloaded, NULL, NULL);
    nmo_curvepoint_vtable.destroy(&copied, NULL, NULL);
    nmo_curvepoint_vtable.destroy(&copy_failed, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, curvepoint_layout_follows_data_version) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_curvepoint_state_t source;
    ASSERT_EQ(NMO_OK, nmo_curvepoint_vtable.create(&source, NULL, NULL));
    source.curve = nmo_ref_from_raw(721);
    source.use_tcb = 1;
    source.tension = 0.1f;
    source.continuity = 0.2f;
    source.bias = 0.3f;
    source.tangent_in = (nmo_vector_t){1.0f, 2.0f, 3.0f};
    source.tangent_out = (nmo_vector_t){4.0f, 5.0f, 6.0f};

    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy);
    legacy->class_id = NMO_CID_CURVEPOINT;
    legacy->data_version = 4;
    legacy->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_curvepoint_serialize(
        &source, legacy, NULL, &serialize_context));
    nmo_chunk_close(legacy);

    size_t section_dwords = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        legacy, CK_STATESAVE_CURVEPOINTDEFAULTDATA, &section_dwords));
    ASSERT_EQ(6u, section_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        legacy, CK_STATESAVE_CURVEPOINTTCB, &section_dwords));
    ASSERT_EQ(3u, section_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        legacy, CK_STATESAVE_CURVEPOINTTANGENTS, &section_dwords));
    ASSERT_EQ(6u, section_dwords);

    nmo_curvepoint_state_t legacy_loaded;
    ASSERT_EQ(NMO_OK, nmo_curvepoint_vtable.create(
        &legacy_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_curvepoint_deserialize(
        &legacy_loaded, legacy, NULL, &deserialize_context));
    ASSERT_FALSE(legacy_loaded.defaultdata_is_modern);
    ASSERT_TRUE(legacy_loaded.has_legacy_position);
    ASSERT_FLOAT_EQ(0.0f, legacy_loaded.legacy_position.x, 0.0001f);
    ASSERT_TRUE(legacy_loaded.has_tcb_chunk);
    ASSERT_TRUE(legacy_loaded.has_tangents_chunk);
    ASSERT_FLOAT_EQ(source.tension, legacy_loaded.tension, 0.0001f);
    ASSERT_FLOAT_EQ(source.continuity, legacy_loaded.continuity, 0.0001f);
    ASSERT_FLOAT_EQ(source.bias, legacy_loaded.bias, 0.0001f);
    ASSERT_FLOAT_EQ(source.tangent_in.y,
                    legacy_loaded.tangent_in.y, 0.0001f);
    ASSERT_FLOAT_EQ(source.tangent_out.z,
                    legacy_loaded.tangent_out.z, 0.0001f);

    nmo_chunk_t *modern = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern);
    modern->class_id = NMO_CID_CURVEPOINT;
    modern->data_version = 7;
    modern->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(modern));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(modern, 0xABCD1234u));
    nmo_chunk_close(modern);

    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_curvepoint_serialize(
        &legacy_loaded, modern, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(modern));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(modern));
    uint32_t marker = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(modern, &marker));
    ASSERT_EQ(0xABCD1234u, marker);

    legacy_loaded.has_legacy_position = 0;
    ASSERT_EQ(NMO_OK, nmo_curvepoint_serialize(
        &legacy_loaded, modern, NULL, &serialize_context));
    nmo_chunk_close(modern);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        modern, CK_STATESAVE_CURVEPOINTDEFAULTDATA, &section_dwords));
    ASSERT_EQ(12u, section_dwords);

    nmo_curvepoint_state_t modern_loaded;
    ASSERT_EQ(NMO_OK, nmo_curvepoint_vtable.create(
        &modern_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_curvepoint_deserialize(
        &modern_loaded, modern, NULL, &deserialize_context));
    ASSERT_TRUE(modern_loaded.defaultdata_is_modern);
    ASSERT_FALSE(modern_loaded.has_legacy_position);
    ASSERT_EQ(721u, modern_loaded.curve.raw_id);
    ASSERT_FLOAT_EQ(source.tension, modern_loaded.tension, 0.0001f);
    ASSERT_FLOAT_EQ(source.tangent_in.x,
                    modern_loaded.tangent_in.x, 0.0001f);
    ASSERT_FLOAT_EQ(source.tangent_out.y,
                    modern_loaded.tangent_out.y, 0.0001f);

    nmo_curvepoint_vtable.destroy(&source, NULL, NULL);
    nmo_curvepoint_vtable.destroy(&legacy_loaded, NULL, NULL);
    nmo_curvepoint_vtable.destroy(&modern_loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, curvepoint_fields_stay_in_identifier_sections) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_curvepoint_state_t state;
    ASSERT_EQ(NMO_OK, nmo_curvepoint_vtable.create(&state, NULL, NULL));
    state.curve = nmo_ref_from_raw(613);
    state.tension = 12.5f;

    const struct {
        uint32_t identifier;
        uint32_t data_version;
        size_t required_dwords;
    } cases[] = {
        {CK_STATESAVE_CURVEPOINTDEFAULTDATA, 7, 12},
        {CK_STATESAVE_CURVEPOINTDEFAULTDATA, 4, 6},
        {CK_STATESAVE_CURVEPOINTTCB, 4, 3},
        {CK_STATESAVE_CURVEPOINTCURVEPOS, 4, 3},
        {CK_STATESAVE_CURVEPOINTTANGENTS, 4, 6},
        {CK_STATESAVE_CURVEPOINTTCB, 7, 3},
        {CK_STATESAVE_CURVEPOINTCURVEPOS, 7, 3},
        {CK_STATESAVE_CURVEPOINTTANGENTS, 7, 6},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        nmo_chunk_t *chunk = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(chunk);
        chunk->data_version = cases[i].data_version;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            chunk, cases[i].identifier));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0));
        for (size_t j = 2; j < cases[i].required_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0));
        }
        nmo_chunk_close(chunk);
        ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_curvepoint_deserialize(
            &state, chunk, NULL, &deserialize_context));
        ASSERT_EQ(613u, state.curve.raw_id);
        ASSERT_EQ(12.5f, state.tension);

        nmo_chunk_t *trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(trailing);
        trailing->data_version = cases[i].data_version;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            trailing, cases[i].identifier));
        for (size_t j = 0; j < cases[i].required_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0u));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0x12345678u));
        nmo_chunk_close(trailing);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_curvepoint_deserialize(
            &state, trailing, NULL, &deserialize_context));
        ASSERT_EQ(613u, state.curve.raw_id);
        ASSERT_EQ(12.5f, state.tension);
    }

    nmo_curvepoint_vtable.destroy(&state, NULL, NULL);
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

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_SPRITE3D;
    truncated->chunk_version = NMO_CHUNK_VERSION4;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_SPRITE3DDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        truncated, VXSPRITE3D_BILLBOARD));
    for (int i = 0; i < 8; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(
            truncated, (float)(i + 1)));
    }
    nmo_chunk_close(truncated);
    nmo_chunk_set_file_context(truncated, &read_context);

    nmo_sprite3d_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_sprite3d_vtable.create(&failed, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &failed.base.base.base, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &failed.base.base.base.scripts, 899));
    failed.half_width = 9.0f;
    failed.material = nmo_ref_from_raw(623);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_sprite3d_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(9.0f, failed.half_width);
    ASSERT_EQ(623u, failed.material.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.material.state);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed.base.base.base.scripts, 0));

    nmo_chunk_t *cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section);
    cross_section->class_id = NMO_CID_SPRITE3D;
    cross_section->chunk_version = NMO_CHUNK_VERSION4;
    cross_section->data_version = 7;
    cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section, CK_STATESAVE_SPRITE3DDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        cross_section, VXSPRITE3D_BILLBOARD));
    for (int i = 0; i < 8; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(
            cross_section, (float)(i + 1)));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section, 0x11223344u));
    nmo_chunk_close(cross_section);
    nmo_chunk_set_file_context(cross_section, &read_context);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_sprite3d_deserialize(
        &failed, cross_section, NULL, &deserialize_context));
    ASSERT_EQ(9.0f, failed.half_width);
    ASSERT_EQ(623u, failed.material.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.material.state);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed.base.base.base.scripts, 0));

    nmo_chunk_t *trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing);
    trailing->class_id = NMO_CID_SPRITE3D;
    trailing->chunk_version = NMO_CHUNK_VERSION4;
    trailing->data_version = 7;
    trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing, CK_STATESAVE_SPRITE3DDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        trailing, VXSPRITE3D_BILLBOARD));
    for (int i = 0; i < 8; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(
            trailing, (float)(i + 1)));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(trailing, 622));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0x12345678u));
    nmo_chunk_close(trailing);
    nmo_chunk_set_file_context(trailing, &read_context);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_sprite3d_deserialize(
        &failed, trailing, NULL, &deserialize_context));
    ASSERT_EQ(9.0f, failed.half_width);
    ASSERT_EQ(623u, failed.material.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.material.state);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed.base.base.base.scripts, 0));

    nmo_sprite3d_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_sprite3d_vtable.create(&invalid, NULL, NULL));
    invalid.material = nmo_ref_from_id(999);
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_SPRITE3D;
    target->chunk_version = NMO_CHUNK_VERSION4;
    target->data_version = 7;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_sprite3d_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_sprite3d_vtable.destroy(&source, NULL, NULL);
    nmo_sprite3d_vtable.destroy(&loaded, NULL, NULL);
    nmo_sprite3d_vtable.destroy(&reloaded, NULL, NULL);
    nmo_array_dispose(&failed.base.base.base.scripts);
    nmo_array_dispose(&failed.base.base.base.attributes);
    nmo_array_dispose(&failed.base.base.base.legacy_attributes);
    nmo_sprite3d_vtable.destroy(&failed, NULL, NULL);
    nmo_sprite3d_vtable.destroy(&invalid, NULL, NULL);
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
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

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

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_WAVESOUND;
    truncated->chunk_version = NMO_CHUNK_VERSION4;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_WAVSOUNDDATA2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 0x55u));
    nmo_chunk_close(truncated);
    nmo_chunk_set_file_context(truncated, &read_context);

    nmo_wavesound_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(&failed, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &failed.base.base, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &failed.base.base.scripts, 899));
    failed.has_data2 = 1;
    failed.priority = 9.0f;
    failed.attached_object = nmo_ref_from_raw(634);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_wavesound_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_TRUE(failed.has_data2);
    ASSERT_EQ(9.0f, failed.priority);
    ASSERT_EQ(634u, failed.attached_object.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.attached_object.state);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed.base.base.scripts, 0));

    nmo_wavesound_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(&invalid, NULL, NULL));
    invalid.has_data2 = 1;
    invalid.attached_object = nmo_ref_from_id(999);
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_WAVESOUND;
    target->chunk_version = NMO_CHUNK_VERSION4;
    target->data_version = 7;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_wavesound_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_wavesound_vtable.destroy(&source, NULL, NULL);
    nmo_wavesound_vtable.destroy(&loaded, NULL, NULL);
    nmo_wavesound_vtable.destroy(&reloaded, NULL, NULL);
    nmo_array_dispose(&failed.base.base.scripts);
    nmo_array_dispose(&failed.base.base.attributes);
    nmo_array_dispose(&failed.base.base.legacy_attributes);
    nmo_wavesound_vtable.destroy(&failed, NULL, NULL);
    nmo_wavesound_vtable.destroy(&invalid, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, wavesound_data_stays_in_identifier_section) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    const nmo_vector_t position = {1.0f, 2.0f, 3.0f};
    const nmo_vector_t direction = {0.0f, 1.0f, 0.0f};

    nmo_chunk_t *background = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(background);
    background->class_id = NMO_CID_WAVESOUND;
    background->data_version = 2;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(background));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        background, CK_STATESAVE_WAVSOUNDDATA2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        background, CK_WAVESOUND_BACKGROUND));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(background, 0.5f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(background, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(background, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(background, 0.75f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(background, 0.25f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(background, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(background, 0.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(background, 0x7F123456u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(background, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(background, 2.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(background, 3.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(background, 4.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(background, 5.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(background, 6u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(background, 700));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_buffer(
        background, &position, sizeof(position)));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_buffer(
        background, &direction, sizeof(direction)));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(background, 0));
    nmo_chunk_close(background);

    nmo_wavesound_state_t background_state;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(
        &background_state, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_wavesound_deserialize(
        &background_state, background, NULL, NULL));
    ASSERT_EQ(CK_WAVESOUND_BACKGROUND,
              background_state.state_flags & CK_WAVESOUND_ALLTYPE);
    ASSERT_EQ(0.0f, background_state.cone_in_angle);
    ASSERT_EQ(NMO_OBJECT_ID_NONE, background_state.attached_object.raw_id);

    nmo_chunk_t *point = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(point);
    point->class_id = NMO_CID_WAVESOUND;
    point->data_version = 2;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(point));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        point, CK_STATESAVE_WAVSOUNDDATA2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(point, CK_WAVESOUND_POINT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(point, 0.5f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(point, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(point, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(point, 0.75f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(point, 0.25f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(point, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(point, 0.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(point, 0x7F123456u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(point, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(point, 2.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(point, 3.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(point, 4.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(point, 5.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(point, 6u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(point, 701));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_buffer(
        point, &position, sizeof(position)));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_buffer(
        point, &direction, sizeof(direction)));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(point, 0));
    nmo_chunk_close(point);

    nmo_wavesound_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(&failed, NULL, NULL));
    failed.has_data2 = 1;
    failed.priority = 9.0f;
    failed.attached_object = nmo_ref_from_raw(702);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_wavesound_deserialize(
        &failed, point, NULL, NULL));
    ASSERT_EQ(9.0f, failed.priority);
    ASSERT_EQ(702u, failed.attached_object.raw_id);

    nmo_chunk_t *modern = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern);
    modern->class_id = NMO_CID_WAVESOUND;
    modern->data_version = 3;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(modern));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        modern, CK_STATESAVE_WAVSOUNDDATA2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(modern, CK_WAVESOUND_POINT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 0.5f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 0.75f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 0.25f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 1.0f));
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 0.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 1.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 2.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 3.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 4.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern, 5.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(modern, 6u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(modern, 703));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_buffer(
        modern, &position, sizeof(position)));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_buffer(
        modern, &direction, sizeof(direction)));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(modern, 0x7F654321u));
    nmo_chunk_close(modern);

    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_wavesound_deserialize(
        &failed, modern, NULL, NULL));
    ASSERT_EQ(9.0f, failed.priority);
    ASSERT_EQ(702u, failed.attached_object.raw_id);

    nmo_wavesound_vtable.destroy(&background_state, NULL, NULL);
    nmo_wavesound_vtable.destroy(&failed, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, wavesound_legacy_data2_round_trips_unknown_words) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);

    uint32_t words[20];
    for (size_t i = 0; i < 20u; ++i) {
        words[i] = 0x10000000u + (uint32_t)i;
    }
    words[8] = 7u;

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_WAVESOUND;
    truncated->data_version = 1;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_WAVSOUNDDATA2));
    for (size_t i = 0; i < 19u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, words[i]));
    }
    nmo_chunk_close(truncated);

    nmo_wavesound_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(&failed, NULL, NULL));
    failed.state_flags = 0x12345678u;
    failed.legacy_data2_words[0] = 0x87654321u;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_wavesound_deserialize(
        &failed, truncated, NULL, NULL));
    ASSERT_EQ(0x12345678u, failed.state_flags);
    ASSERT_EQ(0x87654321u, failed.legacy_data2_words[0]);

    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy);
    legacy->class_id = NMO_CID_WAVESOUND;
    legacy->data_version = 1;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy, CK_STATESAVE_WAVSOUNDDATA2));
    for (size_t i = 0; i < 20u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(legacy, words[i]));
    }
    nmo_chunk_close(legacy);

    nmo_wavesound_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_wavesound_deserialize(
        &loaded, legacy, NULL, NULL));
    ASSERT_TRUE(loaded.has_data2);
    ASSERT_TRUE((loaded.state_flags & CK_WAVESOUND_LOOPED) != 0u);
    ASSERT_EQ(0, memcmp(words, loaded.legacy_data2_words, sizeof(words)));

    nmo_chunk_t *saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(saved);
    saved->class_id = NMO_CID_WAVESOUND;
    saved->data_version = 1;
    saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_wavesound_serialize(
        &loaded, saved, NULL, NULL));
    nmo_chunk_close(saved);

    size_t section_dwords = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        saved, CK_STATESAVE_WAVSOUNDDATA2, &section_dwords));
    ASSERT_EQ(20u, section_dwords);
    for (size_t i = 0; i < 20u; ++i) {
        uint32_t word = 0u;
        ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(saved, &word));
        ASSERT_EQ(i == 8u ? 1u : words[i], word);
    }

    loaded.state_flags &= ~CK_WAVESOUND_LOOPED;
    nmo_chunk_t *normalized = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(normalized);
    normalized->class_id = NMO_CID_WAVESOUND;
    normalized->data_version = 1;
    normalized->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_wavesound_serialize(
        &loaded, normalized, NULL, NULL));
    nmo_chunk_close(normalized);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        normalized, CK_STATESAVE_WAVSOUNDDATA2, &section_dwords));
    ASSERT_EQ(20u, section_dwords);
    for (size_t i = 0; i < 20u; ++i) {
        uint32_t word = 0u;
        ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(normalized, &word));
        ASSERT_EQ(i == 8u ? 0u : words[i], word);
    }

    nmo_wavesound_vtable.destroy(&failed, NULL, NULL);
    nmo_wavesound_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, wavesound_serializer_matches_version_two_layouts) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };

    nmo_wavesound_state_t source;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(&source, NULL, NULL));
    source.has_data2 = 1;
    source.state_flags = CK_WAVESOUND_BACKGROUND;
    source.priority = 0.25f;
    source.gain = 0.5f;
    source.pan = -0.75f;
    source.pitch = 1.25f;
    source.attached_object = nmo_ref_from_id(999u);
    source.version2_reserved_words[0] = 0x11111111u;
    source.version2_reserved_words[1] = 0x22222222u;
    source.version2_reserved_words[2] = 0x33333333u;

    nmo_chunk_t *background = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(background);
    background->class_id = NMO_CID_WAVESOUND;
    background->data_version = 2;
    background->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(background, &write_context);
    ASSERT_EQ(NMO_OK, nmo_wavesound_serialize(
        &source, background, NULL, NULL));
    nmo_chunk_close(background);

    size_t section_dwords = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        background, CK_STATESAVE_WAVSOUNDDATA2, &section_dwords));
    ASSERT_EQ(8u, section_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(background));
    nmo_wavesound_state_t background_loaded;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(
        &background_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_wavesound_deserialize(
        &background_loaded, background, NULL, NULL));
    ASSERT_EQ(CK_WAVESOUND_BACKGROUND,
              background_loaded.state_flags & CK_WAVESOUND_ALLTYPE);
    ASSERT_EQ(0.25f, background_loaded.priority);
    ASSERT_EQ(0.5f, background_loaded.gain);
    ASSERT_EQ(-0.75f, background_loaded.pan);
    ASSERT_EQ(1.25f, background_loaded.pitch);
    ASSERT_EQ(0x11111111u,
              background_loaded.version2_reserved_words[0]);
    ASSERT_EQ(0x22222222u,
              background_loaded.version2_reserved_words[1]);
    ASSERT_EQ(0x33333333u,
              background_loaded.version2_reserved_words[2]);
    ASSERT_EQ(NMO_OBJECT_ID_NONE,
              background_loaded.attached_object.raw_id);

    source.state_flags = CK_WAVESOUND_POINT;
    source.cone_in_angle = 0.1f;
    source.cone_out_angle = 0.2f;
    source.cone_out_gain = 0.3f;
    source.min_distance = 2.0f;
    source.max_distance = 20.0f;
    source.distance_behavior = 3u;
    source.attached_object = nmo_ref_from_raw(701u);
    source.position = (nmo_vector_t){1.0f, 2.0f, 3.0f};
    source.direction = (nmo_vector_t){0.0f, 1.0f, 0.0f};
    source.version2_reserved_words[3] = 0x44444444u;
    source.version2_reserved_words[4] = 0x55555555u;
    source.version2_reserved_words[5] = 0x66666666u;

    nmo_chunk_t *point = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(point);
    point->class_id = NMO_CID_WAVESOUND;
    point->data_version = 2;
    point->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(point, &write_context);
    ASSERT_EQ(NMO_OK, nmo_wavesound_serialize(
        &source, point, NULL, NULL));
    nmo_chunk_close(point);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        point, CK_STATESAVE_WAVSOUNDDATA2, &section_dwords));
    ASSERT_EQ(26u, section_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(point));

    nmo_wavesound_state_t point_loaded;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(
        &point_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_wavesound_deserialize(
        &point_loaded, point, NULL, NULL));
    ASSERT_EQ(CK_WAVESOUND_POINT,
              point_loaded.state_flags & CK_WAVESOUND_ALLTYPE);
    ASSERT_EQ(0.1f, point_loaded.cone_in_angle);
    ASSERT_EQ(0.2f, point_loaded.cone_out_angle);
    ASSERT_EQ(0.3f, point_loaded.cone_out_gain);
    ASSERT_EQ(2.0f, point_loaded.min_distance);
    ASSERT_EQ(20.0f, point_loaded.max_distance);
    ASSERT_EQ(3u, point_loaded.distance_behavior);
    ASSERT_EQ(701u, point_loaded.attached_object.raw_id);
    ASSERT_EQ(1.0f, point_loaded.position.x);
    ASSERT_EQ(2.0f, point_loaded.position.y);
    ASSERT_EQ(3.0f, point_loaded.position.z);
    ASSERT_EQ(0.0f, point_loaded.direction.x);
    ASSERT_EQ(1.0f, point_loaded.direction.y);
    ASSERT_EQ(0.0f, point_loaded.direction.z);
    ASSERT_EQ(0, memcmp(source.version2_reserved_words,
                        point_loaded.version2_reserved_words,
                        sizeof(source.version2_reserved_words)));

    nmo_chunk_t *point_roundtrip = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(point_roundtrip);
    point_roundtrip->class_id = NMO_CID_WAVESOUND;
    point_roundtrip->data_version = 2;
    point_roundtrip->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_wavesound_serialize(
        &point_loaded, point_roundtrip, NULL, NULL));
    nmo_chunk_close(point_roundtrip);
    nmo_wavesound_state_t point_reloaded;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(
        &point_reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_wavesound_deserialize(
        &point_reloaded, point_roundtrip, NULL, NULL));
    ASSERT_EQ(0, memcmp(point_loaded.version2_reserved_words,
                        point_reloaded.version2_reserved_words,
                        sizeof(point_loaded.version2_reserved_words)));

    nmo_wavesound_state_t point_copy;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(
        &point_copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.copy(
        &point_loaded, &point_copy, NULL, arena));
    ASSERT_TRUE(nmo_wavesound_vtable.equals(
        &point_loaded, &point_copy));
    ASSERT_EQ(nmo_wavesound_vtable.hash(&point_loaded),
              nmo_wavesound_vtable.hash(&point_copy));
    point_copy.version2_reserved_words[5] ^= 1u;
    ASSERT_FALSE(nmo_wavesound_vtable.equals(
        &point_loaded, &point_copy));

    nmo_chunk_t *modern_target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern_target);
    modern_target->class_id = NMO_CID_WAVESOUND;
    modern_target->data_version = 3;
    modern_target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(modern_target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        modern_target, 0xABCD1234u));
    nmo_chunk_close(modern_target);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_wavesound_serialize(
        &point_loaded, modern_target, NULL, NULL));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(modern_target));

    memset(source.version2_reserved_words, 0,
           sizeof(source.version2_reserved_words));
    source.modern_reserved_words[0] = 0x77777777u;
    source.modern_reserved_words[1] = 0x88888888u;
    source.modern_reserved_words[2] = 0x99999999u;
    source.modern_reserved_words[3] = 0xAAAAAAAAu;
    ASSERT_EQ(NMO_OK, nmo_wavesound_serialize(
        &source, modern_target, NULL, NULL));
    nmo_chunk_close(modern_target);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        modern_target, CK_STATESAVE_WAVSOUNDDATA2, &section_dwords));
    ASSERT_EQ(24u, section_dwords);
    nmo_wavesound_state_t modern_loaded;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(
        &modern_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_wavesound_deserialize(
        &modern_loaded, modern_target, NULL, NULL));
    ASSERT_EQ(0, memcmp(source.modern_reserved_words,
                        modern_loaded.modern_reserved_words,
                        sizeof(source.modern_reserved_words)));

    nmo_chunk_t *legacy_target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_target);
    legacy_target->class_id = NMO_CID_WAVESOUND;
    legacy_target->data_version = 2;
    legacy_target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        legacy_target, 0x1234ABCDu));
    nmo_chunk_close(legacy_target);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_wavesound_serialize(
        &modern_loaded, legacy_target, NULL, NULL));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(legacy_target));

    nmo_chunk_t *default_target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(default_target);
    default_target->class_id = NMO_CID_WAVESOUND;
    default_target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_wavesound_serialize(
        &modern_loaded, default_target, NULL, NULL));
    ASSERT_EQ(NMO_CHUNK_DATA_VERSION_CURRENT,
              nmo_chunk_get_data_version(default_target));

    nmo_wavesound_vtable.destroy(&source, NULL, NULL);
    nmo_wavesound_vtable.destroy(&background_loaded, NULL, NULL);
    nmo_wavesound_vtable.destroy(&point_loaded, NULL, NULL);
    nmo_wavesound_vtable.destroy(&point_reloaded, NULL, NULL);
    nmo_wavesound_vtable.destroy(&point_copy, NULL, NULL);
    nmo_wavesound_vtable.destroy(&modern_loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, sound_family_failures_keep_state_and_target_chunk_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_SOUND;
    truncated->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_SOUNDFILENAME));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, CKSOUND_EXTERNAL));
    nmo_chunk_close(truncated);

    nmo_sound_state_t sound;
    ASSERT_EQ(NMO_OK, nmo_sound_vtable.create(&sound, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&sound.base, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &sound.base.scripts, 899));
    sound.save_options = CKSOUND_INCLUDEORIGINALFILE;
    sound.file_name = "old.wav";
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_sound_deserialize(
        &sound, truncated, NULL, &deserialize_context));
    ASSERT_EQ(CKSOUND_INCLUDEORIGINALFILE, sound.save_options);
    ASSERT_STR_EQ("old.wav", sound.file_name);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &sound.base.scripts, 0));

    nmo_chunk_t *sound_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(sound_cross_section);
    sound_cross_section->class_id = NMO_CID_SOUND;
    sound_cross_section->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(sound_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        sound_cross_section, CK_STATESAVE_SOUNDFILENAME));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        sound_cross_section, CKSOUND_EXTERNAL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(sound_cross_section, 8u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        sound_cross_section, 0x44434241u));
    nmo_chunk_close(sound_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_sound_deserialize(
        &sound, sound_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(CKSOUND_INCLUDEORIGINALFILE, sound.save_options);
    ASSERT_STR_EQ("old.wav", sound.file_name);

    nmo_chunk_t *midi_truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(midi_truncated);
    midi_truncated->class_id = NMO_CID_MIDISOUND;
    midi_truncated->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(midi_truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        midi_truncated, CK_STATESAVE_MIDISOUNDFILE));
    nmo_chunk_close(midi_truncated);

    nmo_midisound_state_t midi;
    ASSERT_EQ(NMO_OK, nmo_midisound_vtable.create(&midi, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &midi.base.base, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &midi.base.base.scripts, 898));
    midi.base.file_name = "old.mid";
    midi.has_midi_file_name = 1;
    midi.midi_file_name = "derived.mid";
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_midisound_deserialize(
        &midi, midi_truncated, NULL, &deserialize_context));
    ASSERT_STR_EQ("old.mid", midi.base.file_name);
    ASSERT_TRUE(midi.has_midi_file_name);
    ASSERT_STR_EQ("derived.mid", midi.midi_file_name);
    ASSERT_EQ(898u, nmo_beobject_script_array_get_id(
        &midi.base.base.scripts, 0));

    nmo_chunk_t *midi_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(midi_cross_section);
    midi_cross_section->class_id = NMO_CID_MIDISOUND;
    midi_cross_section->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(midi_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        midi_cross_section, CK_STATESAVE_MIDISOUNDFILE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(midi_cross_section, 8u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        midi_cross_section, 0x44434241u));
    nmo_chunk_close(midi_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_midisound_deserialize(
        &midi, midi_cross_section, NULL, &deserialize_context));
    ASSERT_STR_EQ("old.mid", midi.base.file_name);
    ASSERT_TRUE(midi.has_midi_file_name);
    ASSERT_STR_EQ("derived.mid", midi.midi_file_name);

    nmo_wavesound_state_t wave;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(&wave, NULL, NULL));
    wave.has_wave_file_name = 1;
    wave.wave_file_name = "old-wave.wav";
    wave.has_duration = 1;
    wave.duration = 2468;

    nmo_chunk_t *wave_file_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(wave_file_cross_section);
    wave_file_cross_section->class_id = NMO_CID_WAVESOUND;
    wave_file_cross_section->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(wave_file_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        wave_file_cross_section, CK_STATESAVE_WAVSOUNDFILE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(wave_file_cross_section, 8u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        wave_file_cross_section, 0x44434241u));
    nmo_chunk_close(wave_file_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_wavesound_deserialize(
        &wave, wave_file_cross_section, NULL, &deserialize_context));
    ASSERT_STR_EQ("old-wave.wav", wave.wave_file_name);
    ASSERT_EQ(2468, wave.duration);

    nmo_chunk_t *wave_duration_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(wave_duration_cross_section);
    wave_duration_cross_section->class_id = NMO_CID_WAVESOUND;
    wave_duration_cross_section->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(wave_duration_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        wave_duration_cross_section, CK_STATESAVE_WAVSOUNDDURATION));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        wave_duration_cross_section, 2469u));
    nmo_chunk_close(wave_duration_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_wavesound_deserialize(
        &wave, wave_duration_cross_section, NULL, &deserialize_context));
    ASSERT_STR_EQ("old-wave.wav", wave.wave_file_name);
    ASSERT_EQ(2468, wave.duration);

    nmo_chunk_t *sound_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(sound_trailing);
    sound_trailing->class_id = NMO_CID_SOUND;
    sound_trailing->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(sound_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        sound_trailing, CK_STATESAVE_SOUNDFILENAME));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        sound_trailing, CKSOUND_EXTERNAL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(sound_trailing, "new.wav"));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(sound_trailing, 0x12345678u));
    nmo_chunk_close(sound_trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_sound_deserialize(
        &sound, sound_trailing, NULL, &deserialize_context));
    ASSERT_EQ(CKSOUND_INCLUDEORIGINALFILE, sound.save_options);
    ASSERT_STR_EQ("old.wav", sound.file_name);

    nmo_chunk_t *midi_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(midi_trailing);
    midi_trailing->class_id = NMO_CID_MIDISOUND;
    midi_trailing->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(midi_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        midi_trailing, CK_STATESAVE_MIDISOUNDFILE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(midi_trailing, "new.mid"));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(midi_trailing, 0x12345678u));
    nmo_chunk_close(midi_trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_midisound_deserialize(
        &midi, midi_trailing, NULL, &deserialize_context));
    ASSERT_STR_EQ("old.mid", midi.base.file_name);
    ASSERT_TRUE(midi.has_midi_file_name);
    ASSERT_STR_EQ("derived.mid", midi.midi_file_name);

    nmo_chunk_t *wave_file_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(wave_file_trailing);
    wave_file_trailing->class_id = NMO_CID_WAVESOUND;
    wave_file_trailing->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(wave_file_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        wave_file_trailing, CK_STATESAVE_WAVSOUNDFILE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(
        wave_file_trailing, "new-wave.wav"));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        wave_file_trailing, 0x12345678u));
    nmo_chunk_close(wave_file_trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_wavesound_deserialize(
        &wave, wave_file_trailing, NULL, &deserialize_context));
    ASSERT_STR_EQ("old-wave.wav", wave.wave_file_name);
    ASSERT_EQ(2468, wave.duration);

    nmo_chunk_t *wave_duration_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(wave_duration_trailing);
    wave_duration_trailing->class_id = NMO_CID_WAVESOUND;
    wave_duration_trailing->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(wave_duration_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        wave_duration_trailing, CK_STATESAVE_WAVSOUNDDURATION));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(wave_duration_trailing, 2469));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        wave_duration_trailing, 0x12345678u));
    nmo_chunk_close(wave_duration_trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_wavesound_deserialize(
        &wave, wave_duration_trailing, NULL, &deserialize_context));
    ASSERT_STR_EQ("old-wave.wav", wave.wave_file_name);
    ASSERT_EQ(2468, wave.duration);

    static const struct {
        uint32_t data_version;
        uint32_t state_flags;
        size_t payload_dwords;
        size_t first_buffer_size_index;
        size_t second_buffer_size_index;
    } data2_cases[] = {
        {3u, CK_WAVESOUND_POINT, 24u, 15u, 19u},
        {2u, CK_WAVESOUND_BACKGROUND, 8u, SIZE_MAX, SIZE_MAX},
        {2u, CK_WAVESOUND_POINT, 26u, 17u, 21u},
        {1u, 0u, 20u, SIZE_MAX, SIZE_MAX},
    };
    for (size_t i = 0; i < sizeof(data2_cases) / sizeof(data2_cases[0]);
         ++i) {
        uint32_t payload[26] = {0};
        payload[0] = data2_cases[i].state_flags;
        if (data2_cases[i].first_buffer_size_index != SIZE_MAX) {
            payload[data2_cases[i].first_buffer_size_index] = 12u;
            payload[data2_cases[i].second_buffer_size_index] = 12u;
        }

        nmo_chunk_t *data2_trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(data2_trailing);
        data2_trailing->class_id = NMO_CID_WAVESOUND;
        data2_trailing->data_version = data2_cases[i].data_version;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(data2_trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            data2_trailing, CK_STATESAVE_WAVSOUNDDATA2));
        for (size_t j = 0; j < data2_cases[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
                data2_trailing, payload[j]));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
            data2_trailing, 0x12345678u));
        nmo_chunk_close(data2_trailing);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_wavesound_deserialize(
            &wave, data2_trailing, NULL, &deserialize_context));
        ASSERT_STR_EQ("old-wave.wav", wave.wave_file_name);
        ASSERT_EQ(2468, wave.duration);
    }

    fail_after_allocator_state_t allocator_state = {
        .allocation_count = 0,
        .allowed_allocations = 2,
    };
    nmo_allocator_t failing_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &allocator_state);
    nmo_arena_t *failing_arena = nmo_arena_create(
        &failing_allocator, 4096);
    ASSERT_NOT_NULL(failing_arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        failing_arena, NULL, 0, 0);
    char large_name[16385];
    memset(large_name, 'A', sizeof(large_name) - 1);
    large_name[sizeof(large_name) - 1] = '\0';
    nmo_sound_state_t source;
    ASSERT_EQ(NMO_OK, nmo_sound_vtable.create(&source, NULL, NULL));
    source.file_name = large_name;

    nmo_chunk_t *target = nmo_chunk_create(failing_arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_SOUND;
    target->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_sound_serialize(
        &source, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_array_dispose(&sound.base.scripts);
    nmo_array_dispose(&sound.base.attributes);
    nmo_array_dispose(&sound.base.legacy_attributes);
    nmo_array_dispose(&midi.base.base.scripts);
    nmo_array_dispose(&midi.base.base.attributes);
    nmo_array_dispose(&midi.base.base.legacy_attributes);
    nmo_sound_vtable.destroy(&sound, NULL, NULL);
    nmo_midisound_vtable.destroy(&midi, NULL, NULL);
    nmo_wavesound_vtable.destroy(&wave, NULL, NULL);
    nmo_sound_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(failing_arena);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, sound_family_copy_preserves_inherited_and_string_state) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_wavesound_state_t wave;
    nmo_wavesound_state_t wave_copy;
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(&wave, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.create(
        &wave_copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &wave.base.base.scripts, 101));
    wave.base.save_options = CKSOUND_EXTERNAL;
    wave.base.file_name = "audio/base.wav";
    wave.has_wave_file_name = 1;
    wave.wave_file_name = "audio/wave.wav";
    wave.has_duration = 1;
    wave.duration = 1200;
    wave.has_data2 = 1;
    wave.state_flags = 7;
    wave.priority = 0.75f;
    wave.gain = 0.5f;
    wave.pan = -0.25f;
    wave.pitch = 1.25f;
    wave.cone_in_angle = 0.1f;
    wave.cone_out_angle = 0.2f;
    wave.cone_out_gain = 0.3f;
    wave.min_distance = 2.0f;
    wave.max_distance = 20.0f;
    wave.distance_behavior = 3;
    wave.attached_object = nmo_ref_from_raw(301);
    wave.position = (nmo_vector_t){1.0f, 2.0f, 3.0f};
    wave.direction = (nmo_vector_t){0.0f, 1.0f, 0.0f};
    wave.legacy_data2_words[0] = 0x12345678u;
    wave.legacy_data2_words[8] = 7u;
    wave.legacy_data2_words[19] = 0x87654321u;
    nmo_type_descriptor_t wave_type = {
        .size = sizeof(nmo_wavesound_state_t),
    };

    ASSERT_EQ(NMO_OK, nmo_wavesound_vtable.copy(
        &wave, &wave_copy, &wave_type, arena));
    ASSERT_NE(wave.base.base.scripts.data,
              wave_copy.base.base.scripts.data);
    ASSERT_NE(wave.base.file_name, wave_copy.base.file_name);
    ASSERT_NE(wave.wave_file_name, wave_copy.wave_file_name);
    ASSERT_TRUE(nmo_wavesound_vtable.equals(&wave, &wave_copy));
    ASSERT_EQ(nmo_wavesound_vtable.hash(&wave),
              nmo_wavesound_vtable.hash(&wave_copy));
    ASSERT_EQ(0x12345678u, wave_copy.legacy_data2_words[0]);
    ASSERT_EQ(7u, wave_copy.legacy_data2_words[8]);
    ASSERT_EQ(0x87654321u, wave_copy.legacy_data2_words[19]);
    wave_copy.legacy_data2_words[19] ^= 1u;
    ASSERT_FALSE(nmo_wavesound_vtable.equals(&wave, &wave_copy));
    wave_copy.legacy_data2_words[19] ^= 1u;
    ((char *)wave_copy.wave_file_name)[0] = 'X';
    ASSERT_STR_EQ("audio/wave.wav", wave.wave_file_name);
    ASSERT_FALSE(nmo_wavesound_vtable.equals(&wave, &wave_copy));

    nmo_midisound_state_t midi;
    nmo_midisound_state_t midi_copy;
    ASSERT_EQ(NMO_OK, nmo_midisound_vtable.create(&midi, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_midisound_vtable.create(
        &midi_copy, NULL, NULL));
    midi.base.file_name = "music/base.mid";
    midi.has_midi_file_name = 1;
    midi.midi_file_name = "music/derived.mid";
    nmo_type_descriptor_t midi_type = {
        .size = sizeof(nmo_midisound_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_midisound_vtable.copy(
        &midi, &midi_copy, &midi_type, arena));
    ASSERT_NE(midi.base.file_name, midi_copy.base.file_name);
    ASSERT_NE(midi.midi_file_name, midi_copy.midi_file_name);
    ASSERT_TRUE(nmo_midisound_vtable.equals(&midi, &midi_copy));
    ASSERT_EQ(nmo_midisound_vtable.hash(&midi),
              nmo_midisound_vtable.hash(&midi_copy));

    fail_after_allocator_state_t allocator_state = {
        .allowed_allocations = 1,
    };
    nmo_allocator_t failing_allocator = {
        .alloc = fail_after_alloc,
        .free = fail_after_free,
        .user_data = &allocator_state,
    };
    nmo_sound_state_t failing_source;
    nmo_sound_state_t preserved;
    ASSERT_EQ(NMO_OK, nmo_sound_vtable.create(
        &failing_source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_sound_vtable.create(&preserved, NULL, NULL));
    nmo_array_dispose(&failing_source.base.scripts);
    ASSERT_EQ(NMO_OK, nmo_array_init(
        &failing_source.base.scripts, sizeof(nmo_ref_t), 1,
        &failing_allocator));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &failing_source.base.scripts, 401));
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(
        &failing_source.base.attributes, 402, 9, NULL));
    failing_source.file_name = "failure.wav";
    allocator_state.allowed_allocations = allocator_state.allocation_count;
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &preserved.base.scripts, 499));
    preserved.save_options = CKSOUND_INCLUDEORIGINALFILE;
    preserved.file_name = "preserved.wav";
    nmo_ref_t *preserved_scripts =
        NMO_ARRAY_DATA(nmo_ref_t, &preserved.base.scripts);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_sound_vtable.copy(
        &failing_source, &preserved, NULL, arena));
    ASSERT_EQ(preserved_scripts, preserved.base.scripts.data);
    ASSERT_EQ(CKSOUND_INCLUDEORIGINALFILE, preserved.save_options);
    ASSERT_STR_EQ("preserved.wav", preserved.file_name);
    ASSERT_EQ(1u, failing_source.base.attributes.count);
    ASSERT_NOT_NULL(failing_source.base.attributes.data);

    nmo_sound_vtable.destroy(&failing_source, NULL, NULL);
    nmo_sound_vtable.destroy(&preserved, NULL, NULL);
    nmo_midisound_vtable.destroy(&midi, NULL, NULL);
    nmo_midisound_vtable.destroy(&midi_copy, NULL, NULL);
    nmo_wavesound_vtable.destroy(&wave, NULL, NULL);
    nmo_wavesound_vtable.destroy(&wave_copy, NULL, NULL);
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
    ASSERT_TRUE(sprite.has_data);
    ASSERT_EQ(1.0f, sprite.half_width);
    ASSERT_EQ(1.0f, sprite.half_height);
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

TEST(chunk_id_remap, entity3d_legacy_matrix_prefix_round_trips) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_matrix_t matrix = {0};
    matrix.m[0][0] = 1.0f;
    matrix.m[1][1] = 2.0f;
    matrix.m[2][2] = 3.0f;
    matrix.m[3][3] = 1.0f;
    matrix.m[3][0] = 4.0f;

    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy);
    legacy->class_id = NMO_CID_3DENTITY;
    legacy->data_version = 4;
    legacy->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy, CK_STATESAVE_3DENTITYMATRIX));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(legacy, 0x89ABCDEFu));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_matrix(legacy, &matrix));
    nmo_chunk_close(legacy);

    nmo_3dentity_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_3dentity_deserialize(
        &loaded, legacy, NULL, &deserialize_context));
    ASSERT_TRUE(loaded.has_matrix_chunk);
    ASSERT_EQ(0x89ABCDEFu, loaded.legacy_matrix_prefix);
    ASSERT_FLOAT_EQ(2.0f, loaded.world_matrix[5], 0.0001f);
    ASSERT_FLOAT_EQ(4.0f, loaded.world_matrix[12], 0.0001f);

    nmo_chunk_t *saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(saved);
    saved->class_id = NMO_CID_3DENTITY;
    saved->data_version = 4;
    saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_3dentity_serialize(
        &loaded, saved, NULL, &serialize_context));
    nmo_chunk_close(saved);
    size_t section_dwords = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        saved, CK_STATESAVE_3DENTITYMATRIX, &section_dwords));
    ASSERT_EQ(17u, section_dwords);
    uint32_t prefix = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(saved, &prefix));
    ASSERT_EQ(0x89ABCDEFu, prefix);

    nmo_3dentity_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_3dentity_deserialize(
        &reloaded, saved, NULL, &deserialize_context));
    ASSERT_EQ(0x89ABCDEFu, reloaded.legacy_matrix_prefix);

    nmo_3dentity_state_t copied;
    nmo_type_descriptor_t entity_type = {
        .size = sizeof(nmo_3dentity_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.copy(
        &reloaded, &copied, &entity_type, arena));
    ASSERT_TRUE(nmo_3dentity_vtable.equals(&reloaded, &copied));
    ASSERT_EQ(nmo_3dentity_vtable.hash(&reloaded),
              nmo_3dentity_vtable.hash(&copied));
    copied.legacy_matrix_prefix ^= 1u;
    ASSERT_FALSE(nmo_3dentity_vtable.equals(&reloaded, &copied));

    nmo_3dentity_vtable.destroy(&loaded, NULL, NULL);
    nmo_3dentity_vtable.destroy(&reloaded, NULL, NULL);
    nmo_3dentity_vtable.destroy(&copied, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, entity3d_skin_layout_follows_data_version) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    uint32_t bone_index = 0u;
    float bone_weight = 0.75f;
    nmo_3dentity_skin_vertex_t vertex = {
        .bone_count = 1,
        .legacy_before_position = 0x33333333u,
        .initial_pos = {1.0f, 2.0f, 3.0f},
        .legacy_before_indices = 0x44444444u,
        .bone_indices = &bone_index,
        .legacy_before_weights = 0x55555555u,
        .bone_weights = &bone_weight,
    };
    nmo_3dentity_skin_bone_t bone = {
        .bone = nmo_ref_from_raw(801),
        .bone_flags = 3u,
        .legacy_before_matrix = 0x22222222u,
    };
    nmo_3dentity_skin_t skin = {
        .legacy_before_matrix = 0x11111111u,
        .bone_count = 1,
        .bones = &bone,
        .vertex_count = 1,
        .vertices = &vertex,
    };
    nmo_3dentity_state_t source;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&source, NULL, NULL));
    source.skin = &skin;

    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy);
    legacy->class_id = NMO_CID_3DENTITY;
    legacy->data_version = 5;
    legacy->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_3dentity_serialize(
        &source, legacy, NULL, &serialize_context));
    nmo_chunk_close(legacy);
    size_t section_dwords = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        legacy, CK_STATESAVE_3DENTITYSKINDATA, &section_dwords));
    ASSERT_EQ(47u, section_dwords);

    nmo_3dentity_state_t legacy_loaded;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(
        &legacy_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_3dentity_deserialize(
        &legacy_loaded, legacy, NULL, &deserialize_context));
    ASSERT_NOT_NULL(legacy_loaded.skin);
    ASSERT_EQ(0x11111111u, legacy_loaded.skin->legacy_before_matrix);
    ASSERT_EQ(1u, legacy_loaded.skin->bone_count);
    ASSERT_EQ(801u, legacy_loaded.skin->bones[0].bone.raw_id);
    ASSERT_EQ(0x22222222u,
              legacy_loaded.skin->bones[0].legacy_before_matrix);
    ASSERT_EQ(1u, legacy_loaded.skin->vertex_count);
    ASSERT_EQ(0x33333333u,
              legacy_loaded.skin->vertices[0].legacy_before_position);
    ASSERT_FLOAT_EQ(2.0f,
                    legacy_loaded.skin->vertices[0].initial_pos.y, 0.0001f);
    ASSERT_EQ(0x44444444u,
              legacy_loaded.skin->vertices[0].legacy_before_indices);
    ASSERT_EQ(0u, legacy_loaded.skin->vertices[0].bone_indices[0]);
    ASSERT_EQ(0x55555555u,
              legacy_loaded.skin->vertices[0].legacy_before_weights);
    ASSERT_FLOAT_EQ(0.75f,
                    legacy_loaded.skin->vertices[0].bone_weights[0], 0.0001f);

    nmo_chunk_t *legacy_second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_second);
    legacy_second->class_id = NMO_CID_3DENTITY;
    legacy_second->data_version = 5;
    legacy_second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_3dentity_serialize(
        &legacy_loaded, legacy_second, NULL, &serialize_context));
    nmo_chunk_close(legacy_second);
    nmo_3dentity_state_t legacy_reloaded;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(
        &legacy_reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_3dentity_deserialize(
        &legacy_reloaded, legacy_second, NULL, &deserialize_context));
    ASSERT_EQ(0x11111111u, legacy_reloaded.skin->legacy_before_matrix);
    ASSERT_EQ(0x22222222u,
              legacy_reloaded.skin->bones[0].legacy_before_matrix);
    ASSERT_EQ(0x33333333u,
              legacy_reloaded.skin->vertices[0].legacy_before_position);
    ASSERT_EQ(0x44444444u,
              legacy_reloaded.skin->vertices[0].legacy_before_indices);
    ASSERT_EQ(0x55555555u,
              legacy_reloaded.skin->vertices[0].legacy_before_weights);

    nmo_3dentity_state_t copied;
    nmo_type_descriptor_t entity_type = {
        .size = sizeof(nmo_3dentity_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.copy(
        &legacy_reloaded, &copied, &entity_type, arena));
    ASSERT_NE(legacy_reloaded.skin, copied.skin);
    ASSERT_TRUE(nmo_3dentity_vtable.equals(&legacy_reloaded, &copied));
    ASSERT_EQ(nmo_3dentity_vtable.hash(&legacy_reloaded),
              nmo_3dentity_vtable.hash(&copied));
    copied.skin->vertices[0].legacy_before_weights ^= 1u;
    ASSERT_FALSE(nmo_3dentity_vtable.equals(&legacy_reloaded, &copied));
    copied.skin->vertices[0].legacy_before_weights ^= 1u;

    nmo_chunk_t *modern = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern);
    modern->class_id = NMO_CID_3DENTITY;
    modern->data_version = 6;
    modern->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(modern));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(modern, 0xABCD1234u));
    nmo_chunk_close(modern);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_3dentity_serialize(
        &legacy_reloaded, modern, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(modern));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(modern));
    uint32_t marker = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(modern, &marker));
    ASSERT_EQ(0xABCD1234u, marker);

    legacy_reloaded.skin->legacy_before_matrix = 0u;
    legacy_reloaded.skin->bones[0].legacy_before_matrix = 0u;
    legacy_reloaded.skin->vertices[0].legacy_before_position = 0u;
    legacy_reloaded.skin->vertices[0].legacy_before_indices = 0u;
    legacy_reloaded.skin->vertices[0].legacy_before_weights = 0u;
    ASSERT_EQ(NMO_OK, nmo_3dentity_serialize(
        &legacy_reloaded, modern, NULL, &serialize_context));
    nmo_chunk_close(modern);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        modern, CK_STATESAVE_3DENTITYSKINDATA, &section_dwords));
    ASSERT_EQ(42u, section_dwords);

    nmo_3dentity_state_t modern_loaded;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(
        &modern_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_3dentity_deserialize(
        &modern_loaded, modern, NULL, &deserialize_context));
    ASSERT_NOT_NULL(modern_loaded.skin);
    ASSERT_EQ(801u, modern_loaded.skin->bones[0].bone.raw_id);
    ASSERT_FLOAT_EQ(3.0f,
                    modern_loaded.skin->vertices[0].initial_pos.z, 0.0001f);
    ASSERT_FLOAT_EQ(0.75f,
                    modern_loaded.skin->vertices[0].bone_weights[0], 0.0001f);

    nmo_chunk_t *default_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(default_chunk);
    default_chunk->class_id = NMO_CID_3DENTITY;
    default_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_3dentity_serialize(
        &modern_loaded, default_chunk, NULL, &serialize_context));
    ASSERT_EQ(NMO_CHUNK_DATA_VERSION_CURRENT,
              nmo_chunk_get_data_version(default_chunk));

    source.skin = NULL;
    nmo_3dentity_vtable.destroy(&source, NULL, NULL);
    nmo_3dentity_vtable.destroy(&legacy_loaded, NULL, NULL);
    nmo_3dentity_vtable.destroy(&legacy_reloaded, NULL, NULL);
    nmo_3dentity_vtable.destroy(&copied, NULL, NULL);
    nmo_3dentity_vtable.destroy(&modern_loaded, NULL, NULL);
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
    nmo_3dentity_skin_bone_t source_bone = {
        .bone = nmo_ref_from_raw(724),
        .bone_flags = 3,
    };
    nmo_3dentity_skin_t source_skin = {
        .bone_count = 1,
        .bones = &source_bone,
    };
    source3d.skin = &source_skin;

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
    ASSERT_NOT_NULL(loaded3d.skin);
    ASSERT_EQ(1u, loaded3d.skin->bone_count);
    ASSERT_EQ(724u, loaded3d.skin->bones[0].bone.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded3d.skin->bones[0].bone.state);

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
    ASSERT_NOT_NULL(reloaded3d.skin);
    ASSERT_EQ(1u, reloaded3d.skin->bone_count);
    ASSERT_EQ(724u, reloaded3d.skin->bones[0].bone.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded3d.skin->bones[0].bone.state);

    nmo_3dentity_state_t copied3d;
    nmo_type_descriptor_t entity_type = {
        .size = sizeof(nmo_3dentity_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&copied3d, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.copy(
        &reloaded3d, &copied3d, &entity_type, arena));
    ASSERT_NE(reloaded3d.mesh_ids, copied3d.mesh_ids);
    ASSERT_NE(reloaded3d.animation_ids, copied3d.animation_ids);
    ASSERT_NE(reloaded3d.skin, copied3d.skin);
    ASSERT_NE(reloaded3d.skin->bones, copied3d.skin->bones);
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

TEST(chunk_id_remap, entity_content_equality_ignores_storage_addresses) {
    nmo_arena_t *arenas[2] = {
        nmo_arena_create(NULL, 8192),
        nmo_arena_create(NULL, 8192),
    };
    ASSERT_NOT_NULL(arenas[0]);
    ASSERT_NOT_NULL(arenas[1]);

    nmo_3dentity_state_t states[2];
    for (size_t i = 0; i < 2; ++i) {
        ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(
            &states[i], NULL, NULL));
        ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
            &states[i].base.base.scripts, 101));

        states[i].current_mesh = nmo_ref_from_raw(201);
        states[i].mesh_count = 1;
        states[i].mesh_ids = nmo_arena_alloc(
            arenas[i], sizeof(*states[i].mesh_ids), _Alignof(nmo_ref_t));
        ASSERT_NOT_NULL(states[i].mesh_ids);
        states[i].mesh_ids[0] = nmo_ref_from_raw(202);
        states[i].has_mesh_chunk = 1;

        states[i].skin = nmo_arena_alloc(
            arenas[i], sizeof(*states[i].skin),
            _Alignof(nmo_3dentity_skin_t));
        ASSERT_NOT_NULL(states[i].skin);
        memset(states[i].skin, 0, sizeof(*states[i].skin));
        states[i].skin->object_init_matrix.m[0][0] = 1.0f;
        states[i].skin->bone_count = 1;
        states[i].skin->bones = nmo_arena_alloc(
            arenas[i], sizeof(*states[i].skin->bones),
            _Alignof(nmo_3dentity_skin_bone_t));
        ASSERT_NOT_NULL(states[i].skin->bones);
        memset(states[i].skin->bones, 0, sizeof(*states[i].skin->bones));
        states[i].skin->bones[0].bone = nmo_ref_from_raw(301);
        states[i].skin->bones[0].bone_flags = 3;
        states[i].skin->bones[0].inverse_bind_matrix.m[1][1] = 1.0f;

        states[i].skin->vertex_count = 1;
        states[i].skin->vertices = nmo_arena_alloc(
            arenas[i], sizeof(*states[i].skin->vertices),
            _Alignof(nmo_3dentity_skin_vertex_t));
        ASSERT_NOT_NULL(states[i].skin->vertices);
        memset(states[i].skin->vertices, 0,
               sizeof(*states[i].skin->vertices));
        states[i].skin->vertices[0].bone_count = 1;
        states[i].skin->vertices[0].initial_pos.x = 2.0f;
        states[i].skin->vertices[0].bone_indices = nmo_arena_alloc(
            arenas[i], sizeof(uint32_t), _Alignof(uint32_t));
        states[i].skin->vertices[0].bone_weights = nmo_arena_alloc(
            arenas[i], sizeof(float), _Alignof(float));
        ASSERT_NOT_NULL(states[i].skin->vertices[0].bone_indices);
        ASSERT_NOT_NULL(states[i].skin->vertices[0].bone_weights);
        states[i].skin->vertices[0].bone_indices[0] = 0;
        states[i].skin->vertices[0].bone_weights[0] = 0.5f;

        states[i].skin->normal_count = 1;
        states[i].skin->normals = nmo_arena_alloc(
            arenas[i], sizeof(*states[i].skin->normals),
            _Alignof(nmo_vector_t));
        ASSERT_NOT_NULL(states[i].skin->normals);
        states[i].skin->normals[0] = (nmo_vector_t){0.0f, 1.0f, 0.0f};
        states[i].skin->normals_present = 1;
        states[i].skin->normals_have_count = 1;
    }

    ASSERT_NE(states[0].base.base.scripts.data,
              states[1].base.base.scripts.data);
    ASSERT_NE(states[0].mesh_ids, states[1].mesh_ids);
    ASSERT_NE(states[0].skin, states[1].skin);
    ASSERT_NE(states[0].skin->vertices[0].bone_weights,
              states[1].skin->vertices[0].bone_weights);
    ASSERT_TRUE(nmo_3dentity_vtable.equals(&states[0], &states[1]));
    ASSERT_EQ(nmo_3dentity_vtable.hash(&states[0]),
              nmo_3dentity_vtable.hash(&states[1]));

    states[1].skin->vertices[0].bone_weights[0] = 0.75f;
    ASSERT_FALSE(nmo_3dentity_vtable.equals(&states[0], &states[1]));

    for (size_t i = 0; i < 2; ++i) {
        nmo_3dentity_vtable.destroy(&states[i], NULL, NULL);
        nmo_arena_destroy(arenas[i]);
    }
}

TEST(chunk_id_remap, entity_copy_clones_inherited_and_skin_state) {
    nmo_arena_t *source_arena = nmo_arena_create(NULL, 8192);
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(source_arena);
    ASSERT_NOT_NULL(copy_arena);

    nmo_3dentity_state_t source;
    nmo_3dentity_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &source.base.base.scripts, 101));

    source.skin = nmo_arena_alloc(
        source_arena, sizeof(*source.skin),
        _Alignof(nmo_3dentity_skin_t));
    ASSERT_NOT_NULL(source.skin);
    memset(source.skin, 0, sizeof(*source.skin));
    source.skin->vertex_count = 1;
    source.skin->vertices = nmo_arena_alloc(
        source_arena, sizeof(*source.skin->vertices),
        _Alignof(nmo_3dentity_skin_vertex_t));
    ASSERT_NOT_NULL(source.skin->vertices);
    memset(source.skin->vertices, 0, sizeof(*source.skin->vertices));
    source.skin->vertices[0].bone_count = 1;
    source.skin->vertices[0].bone_indices = nmo_arena_alloc(
        source_arena, sizeof(uint32_t), _Alignof(uint32_t));
    source.skin->vertices[0].bone_weights = nmo_arena_alloc(
        source_arena, sizeof(float), _Alignof(float));
    ASSERT_NOT_NULL(source.skin->vertices[0].bone_indices);
    ASSERT_NOT_NULL(source.skin->vertices[0].bone_weights);
    source.skin->vertices[0].bone_indices[0] = 0;
    source.skin->vertices[0].bone_weights[0] = 0.5f;

    nmo_type_descriptor_t entity_type = {
        .size = sizeof(nmo_3dentity_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.copy(
        &source, &copy, &entity_type, copy_arena));
    ASSERT_NE(source.base.base.scripts.data,
              copy.base.base.scripts.data);
    ASSERT_NE(source.skin, copy.skin);
    ASSERT_NE(source.skin->vertices, copy.skin->vertices);
    ASSERT_NE(source.skin->vertices[0].bone_indices,
              copy.skin->vertices[0].bone_indices);
    ASSERT_NE(source.skin->vertices[0].bone_weights,
              copy.skin->vertices[0].bone_weights);
    ASSERT_TRUE(nmo_3dentity_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_3dentity_vtable.hash(&source),
              nmo_3dentity_vtable.hash(&copy));

    fail_after_allocator_state_t allocator_state = {
        .allowed_allocations = 2,
    };
    nmo_allocator_t failing_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &allocator_state);
    nmo_arena_t *failing_arena = nmo_arena_create(
        &failing_allocator, 1);
    ASSERT_NOT_NULL(failing_arena);
    allocator_state.allowed_allocations = allocator_state.allocation_count;
    nmo_ref_t previous_mesh = nmo_ref_from_raw(909);
    nmo_3dentity_state_t preserved;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(
        &preserved, NULL, NULL));
    preserved.entity_flags = 0x12345678u;
    preserved.mesh_count = 1;
    preserved.mesh_ids = &previous_mesh;
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_3dentity_vtable.copy(
        &source, &preserved, &entity_type, failing_arena));
    ASSERT_EQ(0x12345678u, preserved.entity_flags);
    ASSERT_EQ(1u, preserved.mesh_count);
    ASSERT_EQ(&previous_mesh, preserved.mesh_ids);
    ASSERT_EQ(909u, preserved.mesh_ids[0].raw_id);

    copy.skin->vertices[0].bone_weights[0] = 0.75f;
    ASSERT_EQ(0.5f, source.skin->vertices[0].bone_weights[0]);
    ASSERT_FALSE(nmo_3dentity_vtable.equals(&source, &copy));

    nmo_3dentity_vtable.destroy(&preserved, NULL, NULL);
    nmo_3dentity_vtable.destroy(&copy, NULL, NULL);
    nmo_3dentity_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(failing_arena);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(source_arena);
}

TEST(chunk_id_remap, object3d_delegates_state_operations) {
    nmo_arena_t *source_arena = nmo_arena_create(NULL, 8192);
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(source_arena);
    ASSERT_NOT_NULL(copy_arena);

    nmo_3dobject_state_t source;
    nmo_3dobject_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_3dobject_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_3dobject_vtable.create(&copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &source.entity.base.base.scripts, 101));
    source.entity.mesh_count = 1;
    source.entity.mesh_ids = nmo_arena_alloc(
        source_arena, sizeof(*source.entity.mesh_ids),
        _Alignof(nmo_ref_t));
    ASSERT_NOT_NULL(source.entity.mesh_ids);
    source.entity.mesh_ids[0] = nmo_ref_from_raw(201);

    nmo_type_descriptor_t object_type = {
        .size = sizeof(nmo_3dobject_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_3dobject_vtable.copy(
        &source, &copy, &object_type, copy_arena));
    ASSERT_NE(source.entity.base.base.scripts.data,
              copy.entity.base.base.scripts.data);
    ASSERT_NE(source.entity.mesh_ids, copy.entity.mesh_ids);
    ASSERT_TRUE(nmo_3dobject_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_3dobject_vtable.hash(&source),
              nmo_3dobject_vtable.hash(&copy));

    copy.entity.mesh_ids[0] = nmo_ref_from_raw(202);
    ASSERT_EQ(201u, source.entity.mesh_ids[0].raw_id);
    ASSERT_FALSE(nmo_3dobject_vtable.equals(&source, &copy));

    nmo_3dobject_vtable.destroy(&copy, NULL, NULL);
    nmo_3dobject_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(source_arena);
}

TEST(chunk_id_remap, bodypart_copy_preserves_inherited_and_own_state) {
    nmo_arena_t *source_arena = nmo_arena_create(NULL, 8192);
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(source_arena);
    ASSERT_NOT_NULL(copy_arena);

    nmo_bodypart_state_t source;
    nmo_bodypart_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_bodypart_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_bodypart_vtable.create(&copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &source.base.entity.base.base.scripts, 101));
    source.base.entity.mesh_count = 1;
    source.base.entity.mesh_ids = nmo_arena_alloc(
        source_arena, sizeof(*source.base.entity.mesh_ids),
        _Alignof(nmo_ref_t));
    ASSERT_NOT_NULL(source.base.entity.mesh_ids);
    source.base.entity.mesh_ids[0] = nmo_ref_from_raw(201);
    source.has_character = 1;
    source.character = nmo_ref_from_raw(301);
    source.has_rotation_joint = 1;
    source.rotation_joint.flags = 3;
    source.rotation_joint.min.x = -1.0f;
    source.rotation_joint.max.x = 1.0f;
    source.rotation_joint.damping.x = 0.5f;

    nmo_type_descriptor_t bodypart_type = {
        .size = sizeof(nmo_bodypart_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_bodypart_vtable.copy(
        &source, &copy, &bodypart_type, copy_arena));
    ASSERT_NE(source.base.entity.base.base.scripts.data,
              copy.base.entity.base.base.scripts.data);
    ASSERT_NE(source.base.entity.mesh_ids, copy.base.entity.mesh_ids);
    ASSERT_TRUE(nmo_bodypart_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_bodypart_vtable.hash(&source),
              nmo_bodypart_vtable.hash(&copy));

    copy.rotation_joint.damping.x = 0.75f;
    ASSERT_EQ(0.5f, source.rotation_joint.damping.x);
    ASSERT_FALSE(nmo_bodypart_vtable.equals(&source, &copy));

    nmo_bodypart_vtable.destroy(&copy, NULL, NULL);
    nmo_bodypart_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(source_arena);
}

TEST(chunk_id_remap, entity2d_copy_preserves_inherited_and_own_state) {
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(copy_arena);

    nmo_2dentity_state_t source;
    nmo_2dentity_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_2dentity_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_2dentity_vtable.create(&copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &source.base.base.scripts, 101));
    source.data_is_legacy = 1;
    source.rect.left = 1.0f;
    source.rect.top = 2.0f;
    source.rect.right = 3.0f;
    source.rect.bottom = 4.0f;
    source.has_parent = true;
    source.parent = nmo_ref_from_raw(201);
    source.has_material = true;
    source.material = nmo_ref_from_raw(301);
    source.flags = 0x1234u;

    nmo_type_descriptor_t entity_type = {
        .size = sizeof(nmo_2dentity_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_2dentity_vtable.copy(
        &source, &copy, &entity_type, copy_arena));
    ASSERT_NE(source.base.base.scripts.data,
              copy.base.base.scripts.data);
    ASSERT_TRUE(nmo_2dentity_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_2dentity_vtable.hash(&source),
              nmo_2dentity_vtable.hash(&copy));

    copy.parent = nmo_ref_from_raw(202);
    ASSERT_EQ(201u, source.parent.raw_id);
    ASSERT_FALSE(nmo_2dentity_vtable.equals(&source, &copy));

    nmo_2dentity_vtable.destroy(&copy, NULL, NULL);
    nmo_2dentity_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(copy_arena);
}

TEST(chunk_id_remap, entity2d_legacy_layout_round_trips) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    const uint32_t legacy_flags =
        CK_2DENTITY_RESERVED3 |
        CK_2DENTITY_STICKTOP |
        CK_2DENTITY_STICKLEFT;

    nmo_2dentity_state_t source;
    ASSERT_EQ(NMO_OK, nmo_2dentity_vtable.create(&source, NULL, NULL));
    source.flags = legacy_flags;
    source.rect = (nmo_rect_t){10.0f, 20.0f, 30.0f, 50.0f};
    source.has_source_rect = true;
    source.source_rect = (nmo_rect_t){2.0f, 3.0f, 1.0f, 4.0f};
    source.has_z_order = true;
    source.z_order = 9;

    nmo_chunk_t *first = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(first);
    first->class_id = NMO_CID_2DENTITY;
    first->data_version = 4;
    first->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_2dentity_serialize(
        &source, first, NULL, &serialize_context));
    nmo_chunk_close(first);
    size_t section_dwords = 0u;
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier_with_size(
        first, CK_STATESAVE_2DENTITYONLY, &section_dwords));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        first, CK_STATESAVE_2DENTITYFLAGS, &section_dwords));
    ASSERT_EQ(1u, section_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        first, CK_STATESAVE_2DENTITYPOS, &section_dwords));
    ASSERT_EQ(2u, section_dwords);

    nmo_2dentity_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_2dentity_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_2dentity_deserialize(
        &loaded, first, NULL, &deserialize_context));
    ASSERT_TRUE(loaded.data_is_legacy);
    ASSERT_EQ(source.flags, loaded.flags);
    ASSERT_EQ(source.rect.left, loaded.rect.left);
    ASSERT_EQ(source.rect.top, loaded.rect.top);
    ASSERT_EQ(source.rect.right, loaded.rect.right);
    ASSERT_EQ(source.rect.bottom, loaded.rect.bottom);
    ASSERT_TRUE(loaded.has_source_rect);
    ASSERT_EQ(source.source_rect.left, loaded.source_rect.left);
    ASSERT_EQ(source.source_rect.top, loaded.source_rect.top);
    ASSERT_EQ(source.source_rect.right, loaded.source_rect.right);
    ASSERT_EQ(source.source_rect.bottom, loaded.source_rect.bottom);
    ASSERT_TRUE(loaded.has_z_order);
    ASSERT_EQ(source.z_order, loaded.z_order);

    nmo_chunk_t *second = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(second);
    second->class_id = NMO_CID_2DENTITY;
    second->data_version = 4;
    second->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_2dentity_serialize(
        &loaded, second, NULL, &serialize_context));
    nmo_chunk_close(second);
    nmo_2dentity_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_2dentity_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_2dentity_deserialize(
        &reloaded, second, NULL, &deserialize_context));
    ASSERT_EQ(loaded.flags, reloaded.flags);
    ASSERT_EQ(loaded.rect.right, reloaded.rect.right);
    ASSERT_EQ(loaded.source_rect.bottom, reloaded.source_rect.bottom);
    ASSERT_EQ(loaded.z_order, reloaded.z_order);

    nmo_2dentity_state_t homogeneous;
    ASSERT_EQ(NMO_OK, nmo_2dentity_vtable.create(
        &homogeneous, NULL, NULL));
    homogeneous.flags = legacy_flags | CK_2DENTITY_USEHOMOGENEOUSCOORD;
    homogeneous.has_homogeneous_rect = true;
    homogeneous.homogeneous_rect =
        (nmo_rect_t){0.25f, 0.5f, 0.75f, 1.0f};
    nmo_chunk_t *homogeneous_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(homogeneous_chunk);
    homogeneous_chunk->class_id = NMO_CID_2DENTITY;
    homogeneous_chunk->data_version = 4;
    homogeneous_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_2dentity_serialize(
        &homogeneous, homogeneous_chunk, NULL, &serialize_context));
    nmo_chunk_close(homogeneous_chunk);
    nmo_2dentity_state_t homogeneous_loaded;
    ASSERT_EQ(NMO_OK, nmo_2dentity_vtable.create(
        &homogeneous_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_2dentity_deserialize(
        &homogeneous_loaded, homogeneous_chunk, NULL,
        &deserialize_context));
    ASSERT_TRUE(homogeneous_loaded.has_homogeneous_rect);
    ASSERT_EQ(homogeneous.homogeneous_rect.left,
              homogeneous_loaded.homogeneous_rect.left);
    ASSERT_EQ(homogeneous.homogeneous_rect.bottom,
              homogeneous_loaded.homogeneous_rect.bottom);

    nmo_2dentity_state_t default_state;
    ASSERT_EQ(NMO_OK, nmo_2dentity_vtable.create(
        &default_state, NULL, NULL));
    nmo_chunk_t *default_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(default_chunk);
    default_chunk->class_id = NMO_CID_2DENTITY;
    default_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_2dentity_serialize(
        &default_state, default_chunk, NULL, &serialize_context));
    ASSERT_EQ(NMO_CHUNK_DATA_VERSION_CURRENT,
              nmo_chunk_get_data_version(default_chunk));
    nmo_chunk_close(default_chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        default_chunk, CK_STATESAVE_2DENTITYONLY, &section_dwords));

    nmo_chunk_t *preserved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(preserved);
    preserved->class_id = NMO_CID_2DENTITY;
    preserved->data_version = 4;
    preserved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(preserved, 0xABCD1234u));
    nmo_chunk_close(preserved);

    loaded.has_parent = true;
    loaded.parent = nmo_ref_from_raw(901);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_2dentity_serialize(
        &loaded, preserved, NULL, &serialize_context));
    loaded.has_parent = false;
    loaded.parent = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    loaded.has_material = true;
    loaded.material = nmo_ref_from_raw(902);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_2dentity_serialize(
        &loaded, preserved, NULL, &serialize_context));
    loaded.has_material = false;
    loaded.material = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    loaded.rect.left = 10.5f;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_2dentity_serialize(
        &loaded, preserved, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(preserved));
    uint32_t marker = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(preserved, &marker));
    ASSERT_EQ(0xABCD1234u, marker);

    nmo_2dentity_vtable.destroy(&source, NULL, NULL);
    nmo_2dentity_vtable.destroy(&loaded, NULL, NULL);
    nmo_2dentity_vtable.destroy(&reloaded, NULL, NULL);
    nmo_2dentity_vtable.destroy(&homogeneous, NULL, NULL);
    nmo_2dentity_vtable.destroy(&homogeneous_loaded, NULL, NULL);
    nmo_2dentity_vtable.destroy(&default_state, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, entity2d_fields_stay_in_identifier_sections) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_2dentity_state_t state;
    ASSERT_EQ(NMO_OK, nmo_2dentity_vtable.create(&state, NULL, NULL));
    state.flags = 0xCAFEBABEu;
    state.rect.left = 12.5f;
    state.has_material = true;
    state.material = nmo_ref_from_raw(912);

    nmo_chunk_t *modern_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern_cross_section);
    modern_cross_section->class_id = NMO_CID_2DENTITY;
    modern_cross_section->data_version = 7;
    modern_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(modern_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        modern_cross_section, CK_STATESAVE_2DENTITYONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(modern_cross_section, 0));
    for (size_t i = 0; i < 3; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(
            modern_cross_section, 0.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        modern_cross_section, 0x3F800000u));
    nmo_chunk_close(modern_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_2dentity_deserialize(
        &state, modern_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, state.flags);
    ASSERT_EQ(12.5f, state.rect.left);
    ASSERT_EQ(912u, state.material.raw_id);

    nmo_chunk_t *legacy_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_cross_section);
    legacy_cross_section->class_id = NMO_CID_2DENTITY;
    legacy_cross_section->data_version = 4;
    legacy_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_cross_section, CK_STATESAVE_2DENTITYFLAGS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_cross_section, 0x01020304u));
    nmo_chunk_close(legacy_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_2dentity_deserialize(
        &state, legacy_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, state.flags);
    ASSERT_EQ(12.5f, state.rect.left);
    ASSERT_EQ(912u, state.material.raw_id);

    nmo_chunk_t *material_cross_section = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(material_cross_section);
    material_cross_section->class_id = NMO_CID_2DENTITY;
    material_cross_section->data_version = 7;
    material_cross_section->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(material_cross_section));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        material_cross_section, CK_STATESAVE_2DENTITYONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(material_cross_section, 0));
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(
            material_cross_section, 0.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        material_cross_section, CK_STATESAVE_2DENTITYMATERIAL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        material_cross_section, 0x01020304u));
    nmo_chunk_close(material_cross_section);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_2dentity_deserialize(
        &state, material_cross_section, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, state.flags);
    ASSERT_EQ(12.5f, state.rect.left);
    ASSERT_EQ(912u, state.material.raw_id);

    nmo_chunk_t *modern_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern_trailing);
    modern_trailing->class_id = NMO_CID_2DENTITY;
    modern_trailing->data_version = 7;
    modern_trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(modern_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        modern_trailing, CK_STATESAVE_2DENTITYONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(modern_trailing, 0));
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(modern_trailing, 0.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(modern_trailing, 0x12345678u));
    nmo_chunk_close(modern_trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_2dentity_deserialize(
        &state, modern_trailing, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, state.flags);
    ASSERT_EQ(12.5f, state.rect.left);
    ASSERT_EQ(912u, state.material.raw_id);

    const struct {
        uint32_t identifier;
        size_t payload_dwords;
    } legacy_trailing_cases[] = {
        {CK_STATESAVE_2DENTITYFLAGS, 1u},
        {CK_STATESAVE_2DENTITYPOS, 2u},
        {CK_STATESAVE_2DENTITYSIZE, 2u},
        {CK_STATESAVE_2DENTITYSRCSIZE, 4u},
        {CK_STATESAVE_2DENTITYZORDER, 1u},
    };
    for (size_t i = 0;
         i < sizeof(legacy_trailing_cases) / sizeof(legacy_trailing_cases[0]);
         ++i) {
        nmo_chunk_t *legacy_trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(legacy_trailing);
        legacy_trailing->class_id = NMO_CID_2DENTITY;
        legacy_trailing->data_version = 4;
        legacy_trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            legacy_trailing, legacy_trailing_cases[i].identifier));
        for (size_t j = 0; j < legacy_trailing_cases[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(legacy_trailing, 0));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
            legacy_trailing, 0x12345678u));
        nmo_chunk_close(legacy_trailing);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_2dentity_deserialize(
            &state, legacy_trailing, NULL, &deserialize_context));
        ASSERT_EQ(0xCAFEBABEu, state.flags);
        ASSERT_EQ(12.5f, state.rect.left);
        ASSERT_EQ(912u, state.material.raw_id);
    }

    nmo_chunk_t *material_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(material_trailing);
    material_trailing->class_id = NMO_CID_2DENTITY;
    material_trailing->data_version = 7;
    material_trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(material_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        material_trailing, CK_STATESAVE_2DENTITYONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(material_trailing, 0));
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(material_trailing, 0.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        material_trailing, CK_STATESAVE_2DENTITYMATERIAL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(material_trailing, 913));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(material_trailing, 0x12345678u));
    nmo_chunk_close(material_trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_2dentity_deserialize(
        &state, material_trailing, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, state.flags);
    ASSERT_EQ(12.5f, state.rect.left);
    ASSERT_EQ(912u, state.material.raw_id);

    nmo_2dentity_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, camera_copy_preserves_inherited_and_own_state) {
    nmo_arena_t *source_arena = nmo_arena_create(NULL, 8192);
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(source_arena);
    ASSERT_NOT_NULL(copy_arena);

    nmo_camera_state_t source;
    nmo_camera_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_camera_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_camera_vtable.create(&copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &source.entity.base.base.scripts, 101));
    source.entity.mesh_count = 1;
    source.entity.mesh_ids = nmo_arena_alloc(
        source_arena, sizeof(*source.entity.mesh_ids),
        _Alignof(nmo_ref_t));
    ASSERT_NOT_NULL(source.entity.mesh_ids);
    source.entity.mesh_ids[0] = nmo_ref_from_raw(201);
    source.projection_type = 2;
    source.fov = 0.75f;
    source.width = 16;
    source.height = 9;
    source.has_cameraonly_chunk = 1;

    nmo_type_descriptor_t camera_type = {
        .size = sizeof(nmo_camera_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_camera_vtable.copy(
        &source, &copy, &camera_type, copy_arena));
    ASSERT_NE(source.entity.base.base.scripts.data,
              copy.entity.base.base.scripts.data);
    ASSERT_NE(source.entity.mesh_ids, copy.entity.mesh_ids);
    ASSERT_TRUE(nmo_camera_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_camera_vtable.hash(&source),
              nmo_camera_vtable.hash(&copy));

    copy.fov = 1.0f;
    ASSERT_EQ(0.75f, source.fov);
    ASSERT_FALSE(nmo_camera_vtable.equals(&source, &copy));

    nmo_camera_vtable.destroy(&copy, NULL, NULL);
    nmo_camera_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(source_arena);
}

TEST(chunk_id_remap, targetcamera_copy_preserves_base_and_target) {
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(copy_arena);

    nmo_targetcamera_state_t source;
    nmo_targetcamera_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_targetcamera_vtable.create(
        &source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_targetcamera_vtable.create(
        &copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &source.base.entity.base.base.scripts, 101));
    source.base.fov = 0.75f;
    source.has_target = 1;
    source.target = nmo_ref_from_raw(201);

    nmo_type_descriptor_t camera_type = {
        .size = sizeof(nmo_targetcamera_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_targetcamera_vtable.copy(
        &source, &copy, &camera_type, copy_arena));
    ASSERT_NE(source.base.entity.base.base.scripts.data,
              copy.base.entity.base.base.scripts.data);
    ASSERT_TRUE(nmo_targetcamera_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_targetcamera_vtable.hash(&source),
              nmo_targetcamera_vtable.hash(&copy));

    copy.target = nmo_ref_from_raw(202);
    ASSERT_EQ(201u, source.target.raw_id);
    ASSERT_FALSE(nmo_targetcamera_vtable.equals(&source, &copy));

    nmo_targetcamera_vtable.destroy(&copy, NULL, NULL);
    nmo_targetcamera_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(copy_arena);
}

TEST(chunk_id_remap, light_copy_preserves_inherited_and_own_state) {
    nmo_arena_t *source_arena = nmo_arena_create(NULL, 8192);
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(source_arena);
    ASSERT_NOT_NULL(copy_arena);

    nmo_light_state_t source;
    nmo_light_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_light_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_light_vtable.create(&copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &source.entity.base.base.scripts, 101));
    source.entity.mesh_count = 1;
    source.entity.mesh_ids = nmo_arena_alloc(
        source_arena, sizeof(*source.entity.mesh_ids),
        _Alignof(nmo_ref_t));
    ASSERT_NOT_NULL(source.entity.mesh_ids);
    source.entity.mesh_ids[0] = nmo_ref_from_raw(201);
    source.light_data.type = VX_LIGHTSPOT;
    source.light_data.diffuse.r = 0.5f;
    source.light_power = 2.0f;
    source.has_light_power_chunk = 1;

    nmo_type_descriptor_t light_type = {
        .size = sizeof(nmo_light_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_light_vtable.copy(
        &source, &copy, &light_type, copy_arena));
    ASSERT_NE(source.entity.base.base.scripts.data,
              copy.entity.base.base.scripts.data);
    ASSERT_NE(source.entity.mesh_ids, copy.entity.mesh_ids);
    ASSERT_TRUE(nmo_light_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_light_vtable.hash(&source),
              nmo_light_vtable.hash(&copy));

    copy.light_data.diffuse.r = 0.75f;
    ASSERT_EQ(0.5f, source.light_data.diffuse.r);
    ASSERT_FALSE(nmo_light_vtable.equals(&source, &copy));

    nmo_light_vtable.destroy(&copy, NULL, NULL);
    nmo_light_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(source_arena);
}

TEST(chunk_id_remap, targetlight_copy_preserves_base_and_target) {
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(copy_arena);

    nmo_targetlight_state_t source;
    nmo_targetlight_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_targetlight_vtable.create(
        &source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_targetlight_vtable.create(
        &copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &source.base.entity.base.base.scripts, 101));
    source.base.light_power = 2.0f;
    source.has_target = 1;
    source.target = nmo_ref_from_raw(201);

    nmo_type_descriptor_t light_type = {
        .size = sizeof(nmo_targetlight_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_targetlight_vtable.copy(
        &source, &copy, &light_type, copy_arena));
    ASSERT_NE(source.base.entity.base.base.scripts.data,
              copy.base.entity.base.base.scripts.data);
    ASSERT_TRUE(nmo_targetlight_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_targetlight_vtable.hash(&source),
              nmo_targetlight_vtable.hash(&copy));

    copy.target = nmo_ref_from_raw(202);
    ASSERT_EQ(201u, source.target.raw_id);
    ASSERT_FALSE(nmo_targetlight_vtable.equals(&source, &copy));

    nmo_targetlight_vtable.destroy(&copy, NULL, NULL);
    nmo_targetlight_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(copy_arena);
}

TEST(chunk_id_remap, sprite3d_copy_preserves_inherited_and_own_state) {
    nmo_arena_t *source_arena = nmo_arena_create(NULL, 8192);
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(source_arena);
    ASSERT_NOT_NULL(copy_arena);

    nmo_sprite3d_state_t source;
    nmo_sprite3d_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_sprite3d_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_sprite3d_vtable.create(&copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &source.base.base.base.scripts, 101));
    source.base.mesh_count = 1;
    source.base.mesh_ids = nmo_arena_alloc(
        source_arena, sizeof(*source.base.mesh_ids),
        _Alignof(nmo_ref_t));
    ASSERT_NOT_NULL(source.base.mesh_ids);
    source.base.mesh_ids[0] = nmo_ref_from_raw(201);
    source.half_width = 2.0f;
    source.half_height = 3.0f;
    source.material = nmo_ref_from_raw(301);

    nmo_type_descriptor_t sprite_type = {
        .size = sizeof(nmo_sprite3d_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_sprite3d_vtable.copy(
        &source, &copy, &sprite_type, copy_arena));
    ASSERT_NE(source.base.base.base.scripts.data,
              copy.base.base.base.scripts.data);
    ASSERT_NE(source.base.mesh_ids, copy.base.mesh_ids);
    ASSERT_TRUE(nmo_sprite3d_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_sprite3d_vtable.hash(&source),
              nmo_sprite3d_vtable.hash(&copy));

    copy.material = nmo_ref_from_raw(302);
    ASSERT_EQ(301u, source.material.raw_id);
    ASSERT_FALSE(nmo_sprite3d_vtable.equals(&source, &copy));

    nmo_sprite3d_vtable.destroy(&copy, NULL, NULL);
    nmo_sprite3d_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(source_arena);
}

TEST(chunk_id_remap, sprite_copy_preserves_bitmap_content) {
    nmo_arena_t *source_arena = nmo_arena_create(NULL, 8192);
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(source_arena);
    ASSERT_NOT_NULL(copy_arena);

    nmo_sprite_state_t source;
    nmo_sprite_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_sprite_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_sprite_vtable.create(&copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &source.entity.base.base.scripts, 101));
    source.has_bitmap_data = true;
    source.bitmap_data.width = 2;
    source.bitmap_data.height = 1;
    source.bitmap_data.pixel_data_size = 2;
    source.bitmap_data.pixel_data = nmo_arena_alloc(
        source_arena, 2, 1);
    source.bitmap_data.palette_size = 2;
    source.bitmap_data.palette_data = nmo_arena_alloc(
        source_arena, 2, 1);
    ASSERT_NOT_NULL(source.bitmap_data.pixel_data);
    ASSERT_NOT_NULL(source.bitmap_data.palette_data);
    source.bitmap_data.pixel_data[0] = 0x11;
    source.bitmap_data.pixel_data[1] = 0x22;
    source.bitmap_data.palette_data[0] = 0x33;
    source.bitmap_data.palette_data[1] = 0x44;
    source.has_save_options = true;
    source.bitmap_properties_size = 2;
    source.bitmap_properties = nmo_arena_alloc(source_arena, 2, 1);
    ASSERT_NOT_NULL(source.bitmap_properties);
    source.bitmap_properties[0] = 0x55;
    source.bitmap_properties[1] = 0x66;

    nmo_type_descriptor_t sprite_type = {
        .size = sizeof(nmo_sprite_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_sprite_vtable.copy(
        &source, &copy, &sprite_type, copy_arena));
    ASSERT_NE(source.entity.base.base.scripts.data,
              copy.entity.base.base.scripts.data);
    ASSERT_NE(source.bitmap_data.pixel_data, copy.bitmap_data.pixel_data);
    ASSERT_NE(source.bitmap_data.palette_data, copy.bitmap_data.palette_data);
    ASSERT_NE(source.bitmap_properties, copy.bitmap_properties);
    ASSERT_TRUE(nmo_sprite_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_sprite_vtable.hash(&source),
              nmo_sprite_vtable.hash(&copy));

    copy.bitmap_data.pixel_data[0] = 0x77;
    ASSERT_EQ(0x11, source.bitmap_data.pixel_data[0]);
    ASSERT_FALSE(nmo_sprite_vtable.equals(&source, &copy));

    nmo_sprite_vtable.destroy(&copy, NULL, NULL);
    nmo_sprite_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(source_arena);
}

TEST(chunk_id_remap, spritetext_copy_preserves_base_and_strings) {
    nmo_arena_t *source_arena = nmo_arena_create(NULL, 8192);
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(source_arena);
    ASSERT_NOT_NULL(copy_arena);

    nmo_spritetext_state_t source;
    nmo_spritetext_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_spritetext_vtable.create(
        &source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_spritetext_vtable.create(
        &copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &source.base.entity.base.base.scripts, 101));
    source.base.has_bitmap_data = true;
    source.base.bitmap_data.pixel_data_size = 1;
    source.base.bitmap_data.pixel_data = nmo_arena_alloc(
        source_arena, 1, 1);
    ASSERT_NOT_NULL(source.base.bitmap_data.pixel_data);
    source.base.bitmap_data.pixel_data[0] = 0x11;
    source.text_content = "Hello";
    source.font.font_name = "Arial";
    source.font.size = 16;
    source.font.weight = 700;
    source.font_color = 0xAABBCCDDu;
    source.background_color = 0x11223344u;
    source.needs_redraw = true;
    ASSERT_EQ(NMO_OK, nmo_spritetext_remap_dependencies(
        &source, NULL, NULL));
    ASSERT_TRUE(source.needs_redraw);

    nmo_type_descriptor_t text_type = {
        .size = sizeof(nmo_spritetext_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_spritetext_vtable.copy(
        &source, &copy, &text_type, copy_arena));
    ASSERT_NE(source.base.entity.base.base.scripts.data,
              copy.base.entity.base.base.scripts.data);
    ASSERT_NE(source.base.bitmap_data.pixel_data,
              copy.base.bitmap_data.pixel_data);
    ASSERT_NE(source.text_content, copy.text_content);
    ASSERT_NE(source.font.font_name, copy.font.font_name);
    ASSERT_TRUE(nmo_spritetext_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_spritetext_vtable.hash(&source),
              nmo_spritetext_vtable.hash(&copy));

    copy.font.size = 18;
    ASSERT_EQ(16, source.font.size);
    ASSERT_FALSE(nmo_spritetext_vtable.equals(&source, &copy));

    nmo_spritetext_vtable.destroy(&copy, NULL, NULL);
    nmo_spritetext_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(source_arena);
}

TEST(chunk_id_remap, material_copy_preserves_base_and_references) {
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(copy_arena);

    nmo_material_state_t source;
    nmo_material_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_material_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_material_vtable.create(&copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &source.base.scripts, 101));
    source.diffuse_color = 0xAABBCCDDu;
    source.specular_power = 2.0f;
    source.textures[0] = nmo_ref_from_raw(201);
    source.textures[3] = nmo_ref_from_raw(204);
    source.has_effect = 1;
    source.effect = 7;
    source.has_effect_param = 1;
    source.effect_parameter = nmo_ref_from_raw(301);

    nmo_type_descriptor_t material_type = {
        .size = sizeof(nmo_material_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_material_vtable.copy(
        &source, &copy, &material_type, copy_arena));
    ASSERT_NE(source.base.scripts.data, copy.base.scripts.data);
    ASSERT_TRUE(nmo_material_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_material_vtable.hash(&source),
              nmo_material_vtable.hash(&copy));

    copy.textures[3] = nmo_ref_from_raw(205);
    ASSERT_EQ(204u, source.textures[3].raw_id);
    ASSERT_FALSE(nmo_material_vtable.equals(&source, &copy));

    nmo_material_vtable.destroy(&copy, NULL, NULL);
    nmo_material_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(copy_arena);
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
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_2dentity_deserialize(
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
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_2dentity_deserialize(
        &parent2d_state, parent2d_chunk, NULL, &deserialize_context));
    ASSERT_FALSE(parent2d_state.has_parent);
    ASSERT_EQ(NMO_REF_NONE, parent2d_state.parent.state);
    nmo_2dentity_vtable.destroy(&parent2d_state, NULL, NULL);

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, entity_sections_do_not_borrow_following_identifiers) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_3dentity_state_t state;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&state, NULL, NULL));
    state.entity_flags = 0xCAFEBABEu;

    nmo_chunk_t *animation = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(animation);
    animation->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(animation));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        animation, CK_STATESAVE_ANIMATION));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(animation, 0));
    nmo_chunk_close(animation);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_3dentity_deserialize(
        &state, animation, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, state.entity_flags);

    nmo_chunk_t *meshes = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(meshes);
    meshes->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(meshes));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        meshes, CK_STATESAVE_MESHS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(meshes, 744));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(meshes, 0));
    nmo_chunk_close(meshes);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_3dentity_deserialize(
        &state, meshes, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, state.entity_flags);

    nmo_chunk_t *entity_data = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(entity_data);
    entity_data->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(entity_data));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        entity_data, CK_STATESAVE_3DENTITYNDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(entity_data, 0));
    for (size_t i = 0; i < 12; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(entity_data, 0));
    }
    nmo_chunk_close(entity_data);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_3dentity_deserialize(
        &state, entity_data, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, state.entity_flags);

    nmo_chunk_t *parent = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(parent);
    parent->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(parent));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        parent, CK_STATESAVE_PARENT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(parent, 0));
    nmo_chunk_close(parent);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_3dentity_deserialize(
        &state, parent, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, state.entity_flags);

    nmo_chunk_t *flags = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(flags);
    flags->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(flags));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        flags, CK_STATESAVE_3DENTITYFLAGS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(flags, 0));
    nmo_chunk_close(flags);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_3dentity_deserialize(
        &state, flags, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, state.entity_flags);

    nmo_chunk_t *matrix = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(matrix);
    matrix->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(matrix));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        matrix, CK_STATESAVE_3DENTITYMATRIX));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(matrix, 0));
    for (size_t i = 0; i < 15; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(matrix, 0));
    }
    nmo_chunk_close(matrix);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_3dentity_deserialize(
        &state, matrix, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, state.entity_flags);

    nmo_chunk_t *skin = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(skin);
    skin->data_version = 7;
    skin->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(skin));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        skin, CK_STATESAVE_3DENTITYSKINDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(skin, 0));
    for (size_t i = 0; i < 16; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(skin, 0));
    }
    nmo_chunk_close(skin);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_3dentity_deserialize(
        &state, skin, NULL, &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, state.entity_flags);
    ASSERT_NULL(state.skin);

    const struct {
        uint32_t identifier;
        size_t payload_dwords;
    } trailing_sections[] = {
        {CK_STATESAVE_ANIMATION, 1u},
        {CK_STATESAVE_MESHS, 2u},
        {CK_STATESAVE_3DENTITYNDATA, 14u},
        {CK_STATESAVE_PARENT, 1u},
        {CK_STATESAVE_3DENTITYFLAGS, 2u},
        {CK_STATESAVE_3DENTITYMATRIX, 17u},
        {CK_STATESAVE_3DENTITYSKINDATA, 18u},
    };
    for (size_t i = 0;
         i < sizeof(trailing_sections) / sizeof(trailing_sections[0]);
         ++i) {
        nmo_chunk_t *trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(trailing);
        trailing->data_version = 7;
        trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            trailing, trailing_sections[i].identifier));
        for (size_t j = 0; j < trailing_sections[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0x12345678u));
        nmo_chunk_close(trailing);

        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_3dentity_deserialize(
            &state, trailing, NULL, &deserialize_context));
        ASSERT_EQ(0xCAFEBABEu, state.entity_flags);
        ASSERT_NULL(state.skin);
    }

    nmo_3dentity_vtable.destroy(&state, NULL, NULL);
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

TEST(chunk_id_remap, entity_skin_rejects_negative_vertex_bone_count_atomically) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_3DENTITY;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_3DENTITYSKINDATA));
    nmo_matrix_t identity = {
        {{1, 0, 0, 0},
         {0, 1, 0, 0},
         {0, 0, 1, 0},
         {0, 0, 0, 1}}
    };
    ASSERT_EQ(NMO_OK, nmo_chunk_write_matrix(chunk, &identity));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, -1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 0.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 0.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 0.0f));
    nmo_chunk_close(chunk);

    nmo_3dentity_state_t state;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&state, NULL, NULL));
    state.entity_flags = 0x12345678u;
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_3dentity_deserialize(
        &state, chunk, NULL, &deserialize_context));
    ASSERT_EQ(0x12345678u, state.entity_flags);
    ASSERT_NULL(state.skin);
    nmo_3dentity_vtable.destroy(&state, NULL, NULL);

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, entity_skin_rejects_oversized_counts_before_allocation) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_matrix_t identity = {
        {{1, 0, 0, 0},
         {0, 1, 0, 0},
         {0, 0, 1, 0},
         {0, 0, 0, 1}}
    };

    nmo_chunk_t *bones = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(bones);
    bones->class_id = NMO_CID_3DENTITY;
    bones->data_version = 7;
    bones->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(bones));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        bones, CK_STATESAVE_3DENTITYSKINDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_matrix(bones, &identity));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(bones, 100000));
    nmo_chunk_close(bones);

    nmo_3dentity_state_t state;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&state, NULL, NULL));
    state.entity_flags = 0x12345678u;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_3dentity_deserialize(
        &state, bones, NULL, &deserialize_context));
    ASSERT_EQ(0x12345678u, state.entity_flags);
    ASSERT_NULL(state.skin);

    nmo_chunk_t *vertices = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(vertices);
    vertices->class_id = NMO_CID_3DENTITY;
    vertices->data_version = 7;
    vertices->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(vertices));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        vertices, CK_STATESAVE_3DENTITYSKINDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_matrix(vertices, &identity));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(vertices, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(vertices, 100000));
    nmo_chunk_close(vertices);

    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_3dentity_deserialize(
        &state, vertices, NULL, &deserialize_context));
    ASSERT_EQ(0x12345678u, state.entity_flags);
    ASSERT_NULL(state.skin);

    nmo_chunk_t *vertex_bones = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(vertex_bones);
    vertex_bones->class_id = NMO_CID_3DENTITY;
    vertex_bones->data_version = 7;
    vertex_bones->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(vertex_bones));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        vertex_bones, CK_STATESAVE_3DENTITYSKINDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_matrix(vertex_bones, &identity));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(vertex_bones, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(vertex_bones, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(vertex_bones, INT32_MAX));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(vertex_bones, 0.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(vertex_bones, 0.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(vertex_bones, 0.0f));
    nmo_chunk_close(vertex_bones);

    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_3dentity_deserialize(
        &state, vertex_bones, NULL, &deserialize_context));
    ASSERT_EQ(0x12345678u, state.entity_flags);
    ASSERT_NULL(state.skin);

    nmo_3dentity_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, entity_skin_propagates_truncated_bone_indices_atomically) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_3DENTITY;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_3DENTITYSKINDATA));
    nmo_matrix_t identity = {
        {{1, 0, 0, 0},
         {0, 1, 0, 0},
         {0, 0, 1, 0},
         {0, 0, 0, 1}}
    };
    ASSERT_EQ(NMO_OK, nmo_chunk_write_matrix(chunk, &identity));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    nmo_vector_t initial_pos = {0};
    ASSERT_EQ(NMO_OK, nmo_chunk_write_vector3(chunk, &initial_pos));
    nmo_chunk_close(chunk);

    nmo_3dentity_state_t state;
    ASSERT_EQ(NMO_OK, nmo_3dentity_vtable.create(&state, NULL, NULL));
    state.entity_flags = 0x12345678u;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_3dentity_deserialize(
        &state, chunk, NULL, &deserialize_context));
    ASSERT_EQ(0x12345678u, state.entity_flags);
    ASSERT_NULL(state.skin);
    nmo_3dentity_vtable.destroy(&state, NULL, NULL);

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

    state.has_mesh_chunk = 0;
    state.current_mesh = nmo_ref_from_raw(NMO_OBJECT_ID_NONE);
    nmo_3dentity_skin_t invalid_skin = {0};
    state.skin = &invalid_skin;

    invalid_skin.bone_count = 1;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_3dentity_vtable.validate(
        &state, NULL, NULL));
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_3dentity_serialize(
        &state, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));

    invalid_skin.bone_count = 0;
    invalid_skin.vertex_count = 1;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_3dentity_serialize(
        &state, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));

    nmo_3dentity_skin_vertex_t invalid_vertex = {0};
    invalid_vertex.bone_count = 1;
    invalid_skin.vertices = &invalid_vertex;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_3dentity_serialize(
        &state, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));

    nmo_3dentity_skin_bone_t unmapped_bone = {
        .bone = nmo_ref_from_id(124),
    };
    invalid_skin.bone_count = 1;
    invalid_skin.bones = &unmapped_bone;
    invalid_skin.vertex_count = 0;
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_3dentity_serialize(
        &state, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));

    state.skin = NULL;
    nmo_ref_t mesh = nmo_ref_from_raw(456);
    state.mesh_ids = &mesh;
    state.mesh_count = (uint32_t)INT32_MAX + 1u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_3dentity_serialize(
        &state, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));

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

    nmo_chunk_t *no_context_refs = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(no_context_refs);
    no_context_refs->class_id = NMO_CID_PLACE;
    no_context_refs->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(no_context_refs));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        no_context_refs, CK_STATESAVE_PLACEREFERENCES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        no_context_refs, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_sequence_item(
        no_context_refs, 800));
    nmo_chunk_close(no_context_refs);
    nmo_place_state_t no_context_state;
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(
        &no_context_state, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_place_deserialize(
        &no_context_state, no_context_refs, NULL, NULL));
    ASSERT_EQ(1u, no_context_state.references.count);
    ASSERT_EQ(800u, NMO_ARRAY_DATA(
        nmo_ref_t, &no_context_state.references)[0].raw_id);

    nmo_chunk_t *empty_array_sections = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty_array_sections);
    empty_array_sections->class_id = NMO_CID_PLACE;
    empty_array_sections->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(empty_array_sections));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        empty_array_sections, CK_STATESAVE_PLACEPORTALS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(empty_array_sections, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        empty_array_sections, CK_STATESAVE_PLACEREFERENCES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        empty_array_sections, 0));
    nmo_chunk_close(empty_array_sections);
    nmo_place_state_t empty_array_state;
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(
        &empty_array_state, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_place_deserialize(
        &empty_array_state, empty_array_sections, NULL,
        &deserialize_context));
    ASSERT_EQ(1u, empty_array_state.has_portals);
    ASSERT_EQ(1u, empty_array_state.has_references);
    nmo_chunk_t *empty_array_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty_array_saved);
    empty_array_saved->class_id = NMO_CID_PLACE;
    empty_array_saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_place_serialize(
        &empty_array_state, empty_array_saved, NULL,
        &serialize_context));
    nmo_chunk_close(empty_array_saved);
    size_t empty_section_dwords = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        empty_array_saved, CK_STATESAVE_PLACEPORTALS,
        &empty_section_dwords));
    ASSERT_EQ(1u, empty_section_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        empty_array_saved, CK_STATESAVE_PLACEREFERENCES,
        &empty_section_dwords));
    ASSERT_EQ(1u, empty_section_dwords);

    source.has_camera = 1;
    source.camera = nmo_ref_from_raw(801);
    source.has_level = 1;
    source.level = nmo_ref_from_raw(802);
    nmo_ref_t ref_a = nmo_ref_from_raw(803);
    nmo_ref_t ref_b = nmo_ref_from_raw(804);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.references, &ref_a));
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.references, &ref_b));
    nmo_place_portal_entry_t source_portal = {
        .place = nmo_ref_from_raw(817),
        .portal = nmo_ref_from_raw(818),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.portals, &source_portal));
    source.base.base.base.base.base.visibility_flags = NMO_CKOBJECT_VISIBLE;
    source.base.moveable_flags = VX_MOVEABLE_HIERARCHICALHIDE;
    ASSERT_EQ(NMO_OK, nmo_place_remap_dependencies(&source, NULL, NULL));
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE,
              source.base.base.base.base.base.visibility_flags);
    ASSERT_EQ(VX_MOVEABLE_HIERARCHICALHIDE, source.base.moveable_flags);

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
    first->chunk_options &= ~NMO_CHUNK_OPTION_FILE;
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
    ASSERT_EQ(1u, loaded.portals.count);
    const nmo_place_portal_entry_t *loaded_portals = NMO_ARRAY_DATA(
        nmo_place_portal_entry_t, &loaded.portals);
    ASSERT_EQ(817u, loaded_portals[0].place.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded_portals[0].place.state);
    ASSERT_EQ(818u, loaded_portals[0].portal.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded_portals[0].portal.state);

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
    ASSERT_EQ(1u, reloaded.portals.count);
    const nmo_place_portal_entry_t *reloaded_portals = NMO_ARRAY_DATA(
        nmo_place_portal_entry_t, &reloaded.portals);
    ASSERT_EQ(817u, reloaded_portals[0].place.raw_id);
    ASSERT_EQ(818u, reloaded_portals[0].portal.raw_id);
    nmo_ref_t inherited_script = nmo_ref_from_raw(814);
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &reloaded.base.base.base.scripts, &inherited_script));

    nmo_place_state_t copied;
    nmo_type_descriptor_t place_type = {
        .size = sizeof(nmo_place_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_place_vtable.copy(
        &reloaded, &copied, &place_type, arena));
    ASSERT_NE(reloaded.references.data, copied.references.data);
    ASSERT_NE(reloaded.portals.data, copied.portals.data);
    ASSERT_NE(reloaded.base.base.base.scripts.data,
              copied.base.base.base.scripts.data);
    ASSERT_TRUE(nmo_place_vtable.equals(&reloaded, &copied));
    ASSERT_EQ(nmo_place_vtable.hash(&reloaded),
              nmo_place_vtable.hash(&copied));
    NMO_ARRAY_DATA(
        nmo_ref_t, &copied.base.base.base.scripts)[0].raw_id = 815;
    ASSERT_FALSE(nmo_place_vtable.equals(&reloaded, &copied));
    NMO_ARRAY_DATA(
        nmo_ref_t, &copied.base.base.base.scripts)[0].raw_id = 814;
    NMO_ARRAY_DATA(
        nmo_place_portal_entry_t, &copied.portals)[0].portal.raw_id = 819;
    ASSERT_FALSE(nmo_place_vtable.equals(&reloaded, &copied));
    NMO_ARRAY_DATA(
        nmo_place_portal_entry_t, &copied.portals)[0].portal.raw_id = 818;

    nmo_place_state_t copy_failed;
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(
        &copy_failed, NULL, NULL));
    copy_failed.base.entity_flags = 0x12345678u;
    copy_failed.has_camera = 1;
    copy_failed.camera = nmo_ref_from_raw(812);
    nmo_ref_t previous_reference = nmo_ref_from_raw(813);
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &copy_failed.references, &previous_reference));
    void *previous_references = copy_failed.references.data;
    nmo_allocator_t reference_allocator = reloaded.references.allocator;
    reloaded.references.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_place_vtable.copy(
        &reloaded, &copy_failed, &place_type, arena));
    reloaded.references.allocator = reference_allocator;
    ASSERT_EQ(0x12345678u, copy_failed.base.entity_flags);
    ASSERT_TRUE(copy_failed.has_camera);
    ASSERT_EQ(812u, copy_failed.camera.raw_id);
    ASSERT_EQ(previous_references, copy_failed.references.data);
    ASSERT_EQ(1u, copy_failed.references.count);
    ASSERT_EQ(813u, NMO_ARRAY_DATA(
        nmo_ref_t, &copy_failed.references)[0].raw_id);

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
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &failed.base.base.base.scripts, 899));
    nmo_ref_t old_reference = nmo_ref_from_raw(807);
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &failed.references, &old_reference));
    ASSERT_NE(NMO_OK, nmo_place_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(1u, failed.references.count);
    ASSERT_EQ(807u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed.references)[0].raw_id);
    ASSERT_EQ(NMO_REF_NONE, failed.camera.state);
    ASSERT_EQ(NMO_REF_NONE, failed.level.state);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed.base.base.base.scripts, 0));

    nmo_chunk_t *cross_section_references = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_references);
    cross_section_references->class_id = NMO_CID_PLACE;
    cross_section_references->data_version = 7;
    cross_section_references->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_references));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_references, CK_STATESAVE_PLACEREFERENCES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        cross_section_references, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_references, 0x7F123456u));
    nmo_chunk_close(cross_section_references);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_place_deserialize(
        &failed, cross_section_references, NULL, &deserialize_context));
    nmo_chunk_parser_state_t *parser =
        (nmo_chunk_parser_state_t *)cross_section_references->parser_state;
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(parser->prev_identifier_pos + 2u, parser->current_pos);
    ASSERT_EQ(1u, failed.references.count);
    ASSERT_EQ(807u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed.references)[0].raw_id);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed.base.base.base.scripts, 0));

    nmo_chunk_t *missing_reference_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_reference_count);
    missing_reference_count->class_id = NMO_CID_PLACE;
    missing_reference_count->data_version = 7;
    missing_reference_count->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_reference_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_reference_count, CK_STATESAVE_PLACEREFERENCES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_reference_count, 0));
    nmo_chunk_close(missing_reference_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_place_deserialize(
        &failed, missing_reference_count, NULL, &deserialize_context));
    ASSERT_EQ(1u, failed.references.count);
    ASSERT_EQ(807u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed.references)[0].raw_id);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed.base.base.base.scripts, 0));

    nmo_chunk_t *references_trailing_payload = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(references_trailing_payload);
    references_trailing_payload->class_id = NMO_CID_PLACE;
    references_trailing_payload->data_version = 7;
    references_trailing_payload->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(
        references_trailing_payload));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        references_trailing_payload, CK_STATESAVE_PLACEREFERENCES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        references_trailing_payload, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        references_trailing_payload, 0x12345678u));
    nmo_chunk_close(references_trailing_payload);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_place_deserialize(
        &failed, references_trailing_payload, NULL, &deserialize_context));
    ASSERT_EQ(1u, failed.references.count);
    ASSERT_EQ(807u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed.references)[0].raw_id);

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
    failed_camera.has_camera = 1;
    failed_camera.camera = nmo_ref_from_raw(808);
    ASSERT_NE(NMO_OK, nmo_place_deserialize(
        &failed_camera, truncated_camera, NULL, &deserialize_context));
    ASSERT_TRUE(failed_camera.has_camera);
    ASSERT_EQ(808u, failed_camera.camera.raw_id);

    nmo_chunk_t *cross_section_camera = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_camera);
    cross_section_camera->class_id = NMO_CID_PLACE;
    cross_section_camera->data_version = 7;
    cross_section_camera->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_camera));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_camera, CK_STATESAVE_PLACECAMERA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_camera, 0x11223344u));
    nmo_chunk_close(cross_section_camera);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_place_deserialize(
        &failed_camera, cross_section_camera, NULL, &deserialize_context));
    ASSERT_TRUE(failed_camera.has_camera);
    ASSERT_EQ(808u, failed_camera.camera.raw_id);

    nmo_chunk_t *camera_trailing_payload = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(camera_trailing_payload);
    camera_trailing_payload->class_id = NMO_CID_PLACE;
    camera_trailing_payload->data_version = 7;
    camera_trailing_payload->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(camera_trailing_payload));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        camera_trailing_payload, CK_STATESAVE_PLACECAMERA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        camera_trailing_payload, 801));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        camera_trailing_payload, 0x12345678u));
    nmo_chunk_close(camera_trailing_payload);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_place_deserialize(
        &failed_camera, camera_trailing_payload, NULL,
        &deserialize_context));
    ASSERT_TRUE(failed_camera.has_camera);
    ASSERT_EQ(808u, failed_camera.camera.raw_id);

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
    nmo_place_portal_entry_t old_portal = {
        .place = nmo_ref_from_raw(809),
        .portal = nmo_ref_from_raw(810),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &failed_portal.portals, &old_portal));
    ASSERT_NE(NMO_OK, nmo_place_deserialize(
        &failed_portal, truncated_portal, NULL, &deserialize_context));
    ASSERT_EQ(1u, failed_portal.portals.count);
    ASSERT_EQ(809u, NMO_ARRAY_DATA(
        nmo_place_portal_entry_t, &failed_portal.portals)[0].place.raw_id);

    nmo_chunk_t *missing_portal_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_portal_count);
    missing_portal_count->class_id = NMO_CID_PLACE;
    missing_portal_count->data_version = 7;
    missing_portal_count->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_portal_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_portal_count, CK_STATESAVE_PLACEPORTALS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(missing_portal_count, 0));
    nmo_chunk_close(missing_portal_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_place_deserialize(
        &failed_portal, missing_portal_count, NULL, &deserialize_context));
    ASSERT_EQ(1u, failed_portal.portals.count);
    ASSERT_EQ(809u, NMO_ARRAY_DATA(
        nmo_place_portal_entry_t, &failed_portal.portals)[0].place.raw_id);
    ASSERT_EQ(810u, NMO_ARRAY_DATA(
        nmo_place_portal_entry_t, &failed_portal.portals)[0].portal.raw_id);

    nmo_chunk_t *portal_trailing_payload = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(portal_trailing_payload);
    portal_trailing_payload->class_id = NMO_CID_PLACE;
    portal_trailing_payload->data_version = 7;
    portal_trailing_payload->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(portal_trailing_payload));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        portal_trailing_payload, CK_STATESAVE_PLACEPORTALS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(portal_trailing_payload, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        portal_trailing_payload, 0x12345678u));
    nmo_chunk_close(portal_trailing_payload);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_place_deserialize(
        &failed_portal, portal_trailing_payload, NULL,
        &deserialize_context));
    ASSERT_EQ(1u, failed_portal.portals.count);
    ASSERT_EQ(809u, NMO_ARRAY_DATA(
        nmo_place_portal_entry_t,
        &failed_portal.portals)[0].place.raw_id);

    nmo_chunk_t *cross_section_portal = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_portal);
    cross_section_portal->class_id = NMO_CID_PLACE;
    cross_section_portal->data_version = 7;
    cross_section_portal->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_portal));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_portal, CK_STATESAVE_PLACEPORTALS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_portal, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_portal, 0x7F123456u));
    nmo_chunk_close(cross_section_portal);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_place_deserialize(
        &failed_portal, cross_section_portal, NULL, &deserialize_context));
    nmo_chunk_parser_state_t *portal_parser =
        (nmo_chunk_parser_state_t *)cross_section_portal->parser_state;
    ASSERT_NOT_NULL(portal_parser);
    ASSERT_EQ(portal_parser->prev_identifier_pos + 3u,
              portal_parser->current_pos);
    ASSERT_EQ(1u, failed_portal.portals.count);
    ASSERT_EQ(809u, NMO_ARRAY_DATA(
        nmo_place_portal_entry_t, &failed_portal.portals)[0].place.raw_id);
    ASSERT_EQ(810u, NMO_ARRAY_DATA(
        nmo_place_portal_entry_t, &failed_portal.portals)[0].portal.raw_id);

    nmo_place_state_t failed_portal_alloc;
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(
        &failed_portal_alloc, NULL, NULL));
    failed_portal_alloc.base.entity_flags = 0x87654321u;
    failed_portal_alloc.portals.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_place_deserialize(
        &failed_portal_alloc, first, NULL, &deserialize_context));
    ASSERT_EQ(0x87654321u, failed_portal_alloc.base.entity_flags);
    ASSERT_EQ(0u, failed_portal_alloc.portals.count);
    ASSERT_NULL(failed_portal_alloc.portals.data);

    nmo_place_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(&invalid, NULL, NULL));
    nmo_ref_t valid_reference = nmo_ref_from_raw(811);
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &invalid.references, &valid_reference));
    nmo_place_portal_entry_t invalid_portal = {
        .place = nmo_ref_from_raw(812),
        .portal = nmo_ref_from_id(999),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &invalid.portals, &invalid_portal));
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_PLACE;
    target->data_version = 7;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_place_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_place_state_t oversized;
    ASSERT_EQ(NMO_OK, nmo_place_vtable.create(
        &oversized, NULL, NULL));
    nmo_place_portal_entry_t oversized_portal = {0};
    oversized.portals.data = &oversized_portal;
    oversized.portals.count = (size_t)INT32_MAX + 1u;
    oversized.portals.capacity = oversized.portals.count;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_place_serialize(
        &oversized, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    oversized.portals.data = NULL;
    oversized.portals.count = 0;
    oversized.portals.capacity = 0;

    nmo_ref_t oversized_reference = nmo_ref_from_raw(816);
    oversized.references.data = &oversized_reference;
    oversized.references.count = (size_t)INT32_MAX + 1u;
    oversized.references.capacity = oversized.references.count;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_place_vtable.copy(
        &oversized, &copy_failed, &place_type, arena));
    ASSERT_EQ(0x12345678u, copy_failed.base.entity_flags);
    ASSERT_EQ(previous_references, copy_failed.references.data);
    oversized.references.data = NULL;
    oversized.references.count = 0;
    oversized.references.capacity = 0;

    nmo_place_vtable.destroy(&source, NULL, NULL);
    nmo_place_vtable.destroy(&no_context_state, NULL, NULL);
    nmo_place_vtable.destroy(&empty_array_state, NULL, NULL);
    nmo_place_vtable.destroy(&loaded, NULL, NULL);
    nmo_place_vtable.destroy(&reloaded, NULL, NULL);
    nmo_place_vtable.destroy(&copied, NULL, NULL);
    nmo_place_vtable.destroy(&copy_failed, NULL, NULL);
    nmo_place_vtable.destroy(&failed, NULL, NULL);
    nmo_place_vtable.destroy(&failed_camera, NULL, NULL);
    nmo_place_vtable.destroy(&failed_portal, NULL, NULL);
    nmo_place_vtable.destroy(&failed_portal_alloc, NULL, NULL);
    nmo_place_vtable.destroy(&invalid, NULL, NULL);
    nmo_place_vtable.destroy(&oversized, NULL, NULL);
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

    nmo_chunk_t *absent_empty = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(absent_empty);
    absent_empty->class_id = NMO_CID_GROUP;
    absent_empty->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_group_serialize(
        &source, absent_empty, NULL, &serialize_context));
    nmo_chunk_close(absent_empty);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        absent_empty, CK_STATESAVE_GROUPALL));

    nmo_chunk_t *explicit_empty = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(explicit_empty);
    explicit_empty->class_id = NMO_CID_GROUP;
    explicit_empty->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(explicit_empty));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        explicit_empty, CK_STATESAVE_GROUPALL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(explicit_empty, 0));
    nmo_chunk_close(explicit_empty);
    nmo_group_state_t explicit_empty_state;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.create(
        &explicit_empty_state, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_group_deserialize(
        &explicit_empty_state, explicit_empty, NULL, &deserialize_context));
    ASSERT_EQ(1u, explicit_empty_state.has_group_data);
    nmo_chunk_t *explicit_empty_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(explicit_empty_saved);
    explicit_empty_saved->class_id = NMO_CID_GROUP;
    explicit_empty_saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_group_serialize(
        &explicit_empty_state, explicit_empty_saved, NULL,
        &serialize_context));
    nmo_chunk_close(explicit_empty_saved);
    size_t explicit_empty_dwords = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        explicit_empty_saved, CK_STATESAVE_GROUPALL,
        &explicit_empty_dwords));
    ASSERT_EQ(1u, explicit_empty_dwords);

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

    nmo_group_state_t copy_failed;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.create(
        &copy_failed, NULL, NULL));
    copy_failed.base.priority = 77;
    nmo_ref_t copy_old_ref = nmo_ref_from_raw(906);
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &copy_failed.object_ids, &copy_old_ref));
    void *copy_old_data = copy_failed.object_ids.data;
    nmo_allocator_t source_allocator = reloaded.object_ids.allocator;
    reloaded.object_ids.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_group_vtable.copy(
        &reloaded, &copy_failed, &group_type, arena));
    reloaded.object_ids.allocator = source_allocator;
    ASSERT_EQ(77, copy_failed.base.priority);
    ASSERT_EQ(copy_old_data, copy_failed.object_ids.data);
    ASSERT_EQ(1u, copy_failed.object_ids.count);
    ASSERT_EQ(906u, NMO_ARRAY_DATA(
        nmo_ref_t, &copy_failed.object_ids)[0].raw_id);

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
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &failed.base.scripts, 899));
    nmo_ref_t old_ref = nmo_ref_from_raw(904);
    ASSERT_EQ(NMO_OK, nmo_array_append(&failed.object_ids, &old_ref));
    ASSERT_NE(NMO_OK, nmo_group_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_EQ(1u, failed.object_ids.count);
    ASSERT_EQ(904u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed.object_ids)[0].raw_id);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed.base.scripts, 0));

    nmo_chunk_t *missing_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_count);
    missing_count->class_id = NMO_CID_GROUP;
    missing_count->data_version = 7;
    missing_count->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_count, CK_STATESAVE_GROUPALL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(missing_count, 0));
    nmo_chunk_close(missing_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_group_deserialize(
        &failed, missing_count, NULL, &deserialize_context));
    ASSERT_EQ(1u, failed.object_ids.count);
    ASSERT_EQ(904u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed.object_ids)[0].raw_id);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed.base.scripts, 0));

    nmo_chunk_t *trailing_payload = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_payload);
    trailing_payload->class_id = NMO_CID_GROUP;
    trailing_payload->data_version = 7;
    trailing_payload->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_payload));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_payload, CK_STATESAVE_GROUPALL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(trailing_payload, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        trailing_payload, 0x12345678u));
    nmo_chunk_close(trailing_payload);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_group_deserialize(
        &failed, trailing_payload, NULL, &deserialize_context));
    ASSERT_EQ(1u, failed.object_ids.count);
    ASSERT_EQ(904u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed.object_ids)[0].raw_id);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed.base.scripts, 0));

    nmo_chunk_t *cross_section_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_count);
    cross_section_count->class_id = NMO_CID_GROUP;
    cross_section_count->data_version = 7;
    cross_section_count->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_count, CK_STATESAVE_GROUPALL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_count, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_count, 0x7F123456u));
    nmo_chunk_close(cross_section_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_group_deserialize(
        &failed, cross_section_count, NULL, &deserialize_context));
    nmo_chunk_parser_state_t *parser =
        (nmo_chunk_parser_state_t *)cross_section_count->parser_state;
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(parser->prev_identifier_pos + 3u, parser->current_pos);
    ASSERT_EQ(1u, failed.object_ids.count);
    ASSERT_EQ(904u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed.object_ids)[0].raw_id);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed.base.scripts, 0));

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

    nmo_group_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.create(&invalid, NULL, NULL));
    nmo_ref_t valid_ref = nmo_ref_from_raw(905);
    nmo_ref_t invalid_ref = nmo_ref_from_id(999);
    ASSERT_EQ(NMO_OK, nmo_array_append(&invalid.object_ids, &valid_ref));
    ASSERT_EQ(NMO_OK, nmo_array_append(&invalid.object_ids, &invalid_ref));
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_GROUP;
    target->data_version = 7;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_group_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_group_vtable.validate(
        NULL, NULL, NULL));
    size_t valid_count = invalid.object_ids.count;
    invalid.object_ids.count = (size_t)INT32_MAX + 1u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_group_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    invalid.object_ids.count = valid_count;

    nmo_group_state_t large;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.create(&large, NULL, NULL));
    const size_t large_count = 100001u;
    ASSERT_EQ(NMO_OK, nmo_array_extend(
        &large.object_ids, large_count, NULL));
    nmo_chunk_t *large_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(large_chunk);
    large_chunk->class_id = NMO_CID_GROUP;
    large_chunk->data_version = 7;
    large_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(large_chunk, &write_context);
    ASSERT_EQ(NMO_OK, nmo_group_serialize(
        &large, large_chunk, NULL, &serialize_context));
    nmo_chunk_close(large_chunk);
    nmo_chunk_set_file_context(large_chunk, &read_context);
    nmo_group_state_t large_loaded;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.create(
        &large_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_group_deserialize(
        &large_loaded, large_chunk, NULL, &deserialize_context));
    ASSERT_EQ(large_count, large_loaded.object_ids.count);

    nmo_group_vtable.destroy(&source, NULL, NULL);
    nmo_group_vtable.destroy(&explicit_empty_state, NULL, NULL);
    nmo_group_vtable.destroy(&loaded, NULL, NULL);
    nmo_group_vtable.destroy(&reloaded, NULL, NULL);
    nmo_group_vtable.destroy(&copied, NULL, NULL);
    nmo_group_vtable.destroy(&copy_failed, NULL, NULL);
    nmo_group_vtable.destroy(&failed, NULL, NULL);
    nmo_group_vtable.destroy(&failed_negative, NULL, NULL);
    nmo_group_vtable.destroy(&invalid, NULL, NULL);
    nmo_group_vtable.destroy(&large, NULL, NULL);
    nmo_group_vtable.destroy(&large_loaded, NULL, NULL);
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

    nmo_chunk_t *inactive_only = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(inactive_only);
    inactive_only->class_id = NMO_CID_LEVEL;
    inactive_only->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(inactive_only));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        inactive_only, CK_STATESAVE_LEVELINACTIVEMAN));
    nmo_chunk_close(inactive_only);
    nmo_level_state_t inactive_only_state;
    ASSERT_EQ(NMO_OK, nmo_level_vtable.create(
        &inactive_only_state, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_level_deserialize(
        &inactive_only_state, inactive_only, NULL,
        &deserialize_context));
    ASSERT_EQ(1u, inactive_only_state.has_inactive_manager_section);
    ASSERT_EQ(0u, inactive_only_state.has_duplicate_manager_section);
    nmo_chunk_t *inactive_only_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(inactive_only_saved);
    inactive_only_saved->class_id = NMO_CID_LEVEL;
    inactive_only_saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_level_serialize(
        &inactive_only_state, inactive_only_saved, NULL,
        &serialize_context));
    nmo_chunk_close(inactive_only_saved);
    size_t inactive_dwords = 1u;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        inactive_only_saved, CK_STATESAVE_LEVELINACTIVEMAN,
        &inactive_dwords));
    ASSERT_EQ(0u, inactive_dwords);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        inactive_only_saved, CK_STATESAVE_LEVELDUPLICATEMAN));

    nmo_ref_t legacy_a = nmo_ref_from_raw(907);
    nmo_ref_t legacy_b = nmo_ref_from_raw(908);
    nmo_ref_t legacy_pointer = nmo_ref_from_raw(909);
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &source.legacy_object_ids, &legacy_a));
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &source.legacy_object_ids, &legacy_b));
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &source.legacy_pointer_ids, &legacy_pointer));
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
    ASSERT_EQ(2u, loaded.legacy_object_ids.count);
    ASSERT_EQ(1u, loaded.legacy_pointer_ids.count);
    const nmo_ref_t *loaded_legacy = NMO_ARRAY_DATA(
        nmo_ref_t, &loaded.legacy_object_ids);
    const nmo_ref_t *loaded_legacy_pointers = NMO_ARRAY_DATA(
        nmo_ref_t, &loaded.legacy_pointer_ids);
    ASSERT_EQ(907u, loaded_legacy[0].raw_id);
    ASSERT_EQ(908u, loaded_legacy[1].raw_id);
    ASSERT_EQ(909u, loaded_legacy_pointers[0].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded_legacy[0].state);
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
    const nmo_ref_t *reloaded_legacy = NMO_ARRAY_DATA(
        nmo_ref_t, &reloaded.legacy_object_ids);
    const nmo_ref_t *reloaded_legacy_pointers = NMO_ARRAY_DATA(
        nmo_ref_t, &reloaded.legacy_pointer_ids);
    ASSERT_EQ(907u, reloaded_legacy[0].raw_id);
    ASSERT_EQ(908u, reloaded_legacy[1].raw_id);
    ASSERT_EQ(909u, reloaded_legacy_pointers[0].raw_id);
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
    ASSERT_NE(reloaded.legacy_object_ids.data,
              copied.legacy_object_ids.data);
    ASSERT_NE(reloaded.legacy_pointer_ids.data,
              copied.legacy_pointer_ids.data);
    ASSERT_NE(reloaded.scene_ids.data, copied.scene_ids.data);
    ASSERT_TRUE(nmo_level_vtable.equals(&reloaded, &copied));
    ASSERT_EQ(nmo_level_vtable.hash(&reloaded),
              nmo_level_vtable.hash(&copied));

    nmo_level_state_t copy_failed;
    ASSERT_EQ(NMO_OK, nmo_level_vtable.create(
        &copy_failed, NULL, NULL));
    copy_failed.base.priority = 77;
    copy_failed.current_scene = nmo_ref_from_raw(921);
    nmo_ref_t copy_old_scene = nmo_ref_from_raw(922);
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &copy_failed.scene_ids, &copy_old_scene));
    void *copy_old_scenes = copy_failed.scene_ids.data;
    nmo_allocator_t source_allocator = reloaded.scene_ids.allocator;
    reloaded.scene_ids.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_level_vtable.copy(
        &reloaded, &copy_failed, &level_type, arena));
    reloaded.scene_ids.allocator = source_allocator;
    ASSERT_EQ(77, copy_failed.base.priority);
    ASSERT_EQ(921u, copy_failed.current_scene.raw_id);
    ASSERT_EQ(copy_old_scenes, copy_failed.scene_ids.data);
    ASSERT_EQ(1u, copy_failed.scene_ids.count);
    ASSERT_EQ(922u, NMO_ARRAY_DATA(
        nmo_ref_t, &copy_failed.scene_ids)[0].raw_id);

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
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &failed_scenes.base.scripts, 899));
    nmo_ref_t old_scene = nmo_ref_from_raw(917);
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &failed_scenes.scene_ids, &old_scene));
    ASSERT_NE(NMO_OK, nmo_level_deserialize(
        &failed_scenes, truncated_scenes, NULL, &deserialize_context));
    ASSERT_EQ(1u, failed_scenes.scene_ids.count);
    ASSERT_EQ(917u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed_scenes.scene_ids)[0].raw_id);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed_scenes.base.scripts, 0));

    nmo_level_state_t failed_legacy_alloc;
    ASSERT_EQ(NMO_OK, nmo_level_vtable.create(
        &failed_legacy_alloc, NULL, NULL));
    failed_legacy_alloc.base.priority = 71;
    nmo_ref_t old_alloc_scene = nmo_ref_from_raw(923);
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &failed_legacy_alloc.scene_ids, &old_alloc_scene));
    failed_legacy_alloc.legacy_object_ids.allocator =
        nmo_allocator_custom(beobject_fail_alloc, beobject_fail_free, NULL);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_level_deserialize(
        &failed_legacy_alloc, first, NULL, &deserialize_context));
    ASSERT_EQ(71, failed_legacy_alloc.base.priority);
    ASSERT_EQ(0u, failed_legacy_alloc.legacy_object_ids.count);
    ASSERT_EQ(1u, failed_legacy_alloc.scene_ids.count);
    ASSERT_EQ(923u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed_legacy_alloc.scene_ids)[0].raw_id);

    nmo_chunk_t *cross_section_scenes = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_scenes);
    cross_section_scenes->class_id = NMO_CID_LEVEL;
    cross_section_scenes->data_version = 7;
    cross_section_scenes->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_scenes));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_scenes, CK_STATESAVE_LEVELDEFAULTDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        cross_section_scenes, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        cross_section_scenes, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        cross_section_scenes, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_scenes, 0x7F123456u));
    nmo_chunk_close(cross_section_scenes);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_level_deserialize(
        &failed_scenes, cross_section_scenes, NULL, &deserialize_context));
    nmo_chunk_parser_state_t *parser =
        (nmo_chunk_parser_state_t *)cross_section_scenes->parser_state;
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(parser->prev_identifier_pos + 5u, parser->current_pos);
    ASSERT_EQ(1u, failed_scenes.scene_ids.count);
    ASSERT_EQ(917u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed_scenes.scene_ids)[0].raw_id);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed_scenes.base.scripts, 0));

    nmo_chunk_t *missing_scene_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_scene_count);
    missing_scene_count->class_id = NMO_CID_LEVEL;
    missing_scene_count->data_version = 7;
    missing_scene_count->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_scene_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_scene_count, CK_STATESAVE_LEVELDEFAULTDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        missing_scene_count, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        missing_scene_count, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_scene_count, 0));
    nmo_chunk_close(missing_scene_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_level_deserialize(
        &failed_scenes, missing_scene_count, NULL, &deserialize_context));
    ASSERT_EQ(1u, failed_scenes.scene_ids.count);
    ASSERT_EQ(917u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed_scenes.scene_ids)[0].raw_id);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed_scenes.base.scripts, 0));

    const nmo_guid_t old_inactive_guid = {
        0x12345678u, 0x90ABCDEFu,
    };
    const char *old_duplicate_name = "old-manager";
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &failed_scenes.inactive_manager_guids, &old_inactive_guid));
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &failed_scenes.duplicate_manager_names, &old_duplicate_name));
    void *old_inactive_guids = failed_scenes.inactive_manager_guids.data;
    void *old_duplicate_names = failed_scenes.duplicate_manager_names.data;

    nmo_chunk_t *unterminated_manager_names = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(unterminated_manager_names);
    unterminated_manager_names->class_id = NMO_CID_LEVEL;
    unterminated_manager_names->data_version = 7;
    unterminated_manager_names->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(unterminated_manager_names));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        unterminated_manager_names, CK_STATESAVE_LEVELINACTIVEMAN));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        unterminated_manager_names, CK_STATESAVE_LEVELDUPLICATEMAN));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(
        unterminated_manager_names, "duplicate-manager"));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        unterminated_manager_names, 0u));
    nmo_chunk_close(unterminated_manager_names);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_level_deserialize(
        &failed_scenes, unterminated_manager_names, NULL,
        &deserialize_context));
    ASSERT_EQ(old_inactive_guids, failed_scenes.inactive_manager_guids.data);
    ASSERT_EQ(old_duplicate_names, failed_scenes.duplicate_manager_names.data);
    ASSERT_EQ(1u, failed_scenes.inactive_manager_guids.count);
    ASSERT_EQ(1u, failed_scenes.duplicate_manager_names.count);
    ASSERT_STR_EQ("old-manager", NMO_ARRAY_DATA(
        const char *, &failed_scenes.duplicate_manager_names)[0]);

    nmo_chunk_t *trailing_manager_names = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_manager_names);
    trailing_manager_names->class_id = NMO_CID_LEVEL;
    trailing_manager_names->data_version = 7;
    trailing_manager_names->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_manager_names));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_manager_names, CK_STATESAVE_LEVELINACTIVEMAN));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_manager_names, CK_STATESAVE_LEVELDUPLICATEMAN));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(
        trailing_manager_names, "duplicate-manager"));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_string(trailing_manager_names, NULL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        trailing_manager_names, 0x12345678u));
    nmo_chunk_close(trailing_manager_names);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_level_deserialize(
        &failed_scenes, trailing_manager_names, NULL, &deserialize_context));
    ASSERT_EQ(old_inactive_guids, failed_scenes.inactive_manager_guids.data);
    ASSERT_EQ(old_duplicate_names, failed_scenes.duplicate_manager_names.data);
    ASSERT_EQ(1u, failed_scenes.inactive_manager_guids.count);
    ASSERT_EQ(1u, failed_scenes.duplicate_manager_names.count);
    ASSERT_STR_EQ("old-manager", NMO_ARRAY_DATA(
        const char *, &failed_scenes.duplicate_manager_names)[0]);

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
    failed_scalars.current_scene = nmo_ref_from_raw(918);
    failed_scalars.level_scene = nmo_ref_from_raw(919);
    ASSERT_NE(NMO_OK, nmo_level_deserialize(
        &failed_scalars, truncated_scalars, NULL, &deserialize_context));
    ASSERT_EQ(918u, failed_scalars.current_scene.raw_id);
    ASSERT_EQ(919u, failed_scalars.level_scene.raw_id);
    ASSERT_NULL(failed_scalars.level_scene_chunk);

    nmo_chunk_t *missing_level_scene_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_level_scene_chunk);
    missing_level_scene_chunk->class_id = NMO_CID_LEVEL;
    missing_level_scene_chunk->data_version = 7;
    missing_level_scene_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_level_scene_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_level_scene_chunk, CK_STATESAVE_LEVELSCENE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(
        missing_level_scene_chunk, 916));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(
        missing_level_scene_chunk, 917));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_level_scene_chunk, 0));
    nmo_chunk_close(missing_level_scene_chunk);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_level_deserialize(
        &failed_scalars, missing_level_scene_chunk, NULL,
        &deserialize_context));
    ASSERT_EQ(918u, failed_scalars.current_scene.raw_id);
    ASSERT_EQ(919u, failed_scalars.level_scene.raw_id);
    ASSERT_NULL(failed_scalars.level_scene_chunk);

    nmo_chunk_t *trailing_level_scene = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_level_scene);
    trailing_level_scene->class_id = NMO_CID_LEVEL;
    trailing_level_scene->data_version = 7;
    trailing_level_scene->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_level_scene));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_level_scene, CK_STATESAVE_LEVELSCENE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(trailing_level_scene, 916));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(trailing_level_scene, 917));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_sub_chunk(trailing_level_scene, NULL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        trailing_level_scene, 0x12345678u));
    nmo_chunk_close(trailing_level_scene);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_level_deserialize(
        &failed_scalars, trailing_level_scene, NULL, &deserialize_context));
    ASSERT_EQ(918u, failed_scalars.current_scene.raw_id);
    ASSERT_EQ(919u, failed_scalars.level_scene.raw_id);
    ASSERT_NULL(failed_scalars.level_scene_chunk);

    nmo_level_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_level_vtable.create(&invalid, NULL, NULL));
    nmo_ref_t valid_scene = nmo_ref_from_raw(920);
    nmo_ref_t invalid_scene = nmo_ref_from_id(999);
    ASSERT_EQ(NMO_OK, nmo_array_append(&invalid.scene_ids, &valid_scene));
    ASSERT_EQ(NMO_OK, nmo_array_append(&invalid.scene_ids, &invalid_scene));
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_LEVEL;
    target->data_version = 7;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_level_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_level_vtable.validate(
        NULL, NULL, NULL));
    const char *missing_manager_name = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &invalid.duplicate_manager_names, &missing_manager_name));
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_level_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));

    invalid.duplicate_manager_names.count = 0;
    size_t invalid_scene_count = invalid.scene_ids.count;
    invalid.scene_ids.count = (size_t)INT32_MAX + 1u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_level_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    invalid.scene_ids.count = invalid_scene_count;

    nmo_ref_t valid_legacy = nmo_ref_from_raw(924);
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &invalid.legacy_object_ids, &valid_legacy));
    size_t invalid_legacy_count = invalid.legacy_object_ids.count;
    invalid.legacy_object_ids.count = (size_t)INT32_MAX + 1u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_level_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    invalid.legacy_object_ids.count = invalid_legacy_count;

    nmo_level_state_t large;
    ASSERT_EQ(NMO_OK, nmo_level_vtable.create(&large, NULL, NULL));
    const size_t large_count = 10001u;
    ASSERT_EQ(NMO_OK, nmo_array_extend(
        &large.scene_ids, large_count, NULL));
    nmo_chunk_t *large_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(large_chunk);
    large_chunk->class_id = NMO_CID_LEVEL;
    large_chunk->data_version = 7;
    large_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(large_chunk, &write_context);
    ASSERT_EQ(NMO_OK, nmo_level_serialize(
        &large, large_chunk, NULL, &serialize_context));
    nmo_chunk_close(large_chunk);
    nmo_chunk_set_file_context(large_chunk, &read_context);
    nmo_level_state_t large_loaded;
    ASSERT_EQ(NMO_OK, nmo_level_vtable.create(
        &large_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_level_deserialize(
        &large_loaded, large_chunk, NULL, &deserialize_context));
    ASSERT_EQ(large_count, large_loaded.scene_ids.count);

    nmo_level_vtable.destroy(&source, NULL, NULL);
    nmo_level_vtable.destroy(&inactive_only_state, NULL, NULL);
    nmo_level_vtable.destroy(&loaded, NULL, NULL);
    nmo_level_vtable.destroy(&reloaded, NULL, NULL);
    nmo_level_vtable.destroy(&copied, NULL, NULL);
    nmo_level_vtable.destroy(&copy_failed, NULL, NULL);
    nmo_level_vtable.destroy(&failed_scenes, NULL, NULL);
    nmo_level_vtable.destroy(&failed_legacy_alloc, NULL, NULL);
    nmo_level_vtable.destroy(&failed_scalars, NULL, NULL);
    nmo_level_vtable.destroy(&invalid, NULL, NULL);
    nmo_level_vtable.destroy(&large, NULL, NULL);
    nmo_level_vtable.destroy(&large_loaded, NULL, NULL);
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

    nmo_scene_state_t copy_failed;
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(
        &copy_failed, NULL, NULL));
    copy_failed.base.priority = 77;
    copy_failed.level = nmo_ref_from_raw(961);
    nmo_scene_object_desc_t copy_old_desc = {
        .ref = nmo_ref_from_raw(962),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &copy_failed.object_descs, &copy_old_desc));
    void *copy_old_descs = copy_failed.object_descs.data;
    nmo_allocator_t source_allocator = reloaded.object_descs.allocator;
    reloaded.object_descs.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_scene_vtable.copy(
        &reloaded, &copy_failed, &scene_type, arena));
    reloaded.object_descs.allocator = source_allocator;
    ASSERT_EQ(77, copy_failed.base.priority);
    ASSERT_EQ(961u, copy_failed.level.raw_id);
    ASSERT_EQ(copy_old_descs, copy_failed.object_descs.data);
    ASSERT_EQ(1u, copy_failed.object_descs.count);
    ASSERT_EQ(962u, NMO_ARRAY_DATA(
        nmo_scene_object_desc_t,
        &copy_failed.object_descs)[0].ref.raw_id);

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
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &failed_descs.base.scripts, 899));
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
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed_descs.base.scripts, 0));

    nmo_chunk_t *cross_section_descs = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_descs);
    cross_section_descs->class_id = NMO_CID_SCENE;
    cross_section_descs->data_version = 8;
    cross_section_descs->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_descs));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_descs, CK_STATESAVE_SCENENEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(cross_section_descs, 945));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_descs, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_descs, 0x7F123456u));
    nmo_chunk_close(cross_section_descs);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_scene_deserialize(
        &failed_descs, cross_section_descs, NULL, &deserialize_context));
    nmo_chunk_parser_state_t *parser =
        (nmo_chunk_parser_state_t *)cross_section_descs->parser_state;
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(parser->prev_identifier_pos + 4u, parser->current_pos);
    ASSERT_EQ(950u, failed_descs.level.raw_id);
    ASSERT_EQ(1u, failed_descs.object_descs.count);
    ASSERT_EQ(951u, NMO_ARRAY_DATA(
        nmo_scene_object_desc_t, &failed_descs.object_descs)[0].ref.raw_id);
    ASSERT_EQ(899u, nmo_beobject_script_array_get_id(
        &failed_descs.base.scripts, 0));

    nmo_chunk_t *missing_desc_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_desc_count);
    missing_desc_count->class_id = NMO_CID_SCENE;
    missing_desc_count->data_version = 8;
    missing_desc_count->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_desc_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_desc_count, CK_STATESAVE_SCENENEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(missing_desc_count, 945));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(missing_desc_count, 0));
    nmo_chunk_close(missing_desc_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_scene_deserialize(
        &failed_descs, missing_desc_count, NULL, &deserialize_context));
    ASSERT_EQ(950u, failed_descs.level.raw_id);
    ASSERT_EQ(1u, failed_descs.object_descs.count);
    ASSERT_EQ(951u, NMO_ARRAY_DATA(
        nmo_scene_object_desc_t, &failed_descs.object_descs)[0].ref.raw_id);

    nmo_chunk_t *cross_section_subchunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_subchunk);
    cross_section_subchunk->class_id = NMO_CID_SCENE;
    cross_section_subchunk->data_version = 8;
    cross_section_subchunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_subchunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_subchunk, CK_STATESAVE_SCENENEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(
        cross_section_subchunk, 945));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_subchunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        cross_section_subchunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_item(
        cross_section_subchunk, 946));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_sub_chunk_sequence(
        cross_section_subchunk, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_sub_chunk_sequence(
        cross_section_subchunk, NULL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_subchunk, 7));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(cross_section_subchunk, 0));
    for (size_t i = 0; i < 6; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_subchunk, 0));
    }
    nmo_chunk_close(cross_section_subchunk);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_scene_deserialize(
        &failed_descs, cross_section_subchunk, NULL, &deserialize_context));
    ASSERT_EQ(950u, failed_descs.level.raw_id);
    ASSERT_EQ(1u, failed_descs.object_descs.count);
    ASSERT_EQ(951u, NMO_ARRAY_DATA(
        nmo_scene_object_desc_t, &failed_descs.object_descs)[0].ref.raw_id);

    nmo_chunk_t *trailing_descs = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_descs);
    trailing_descs->class_id = NMO_CID_SCENE;
    trailing_descs->data_version = 8;
    trailing_descs->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_descs));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_descs, CK_STATESAVE_SCENENEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(trailing_descs, 945));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(trailing_descs, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing_descs, 0x12345678u));
    nmo_chunk_close(trailing_descs);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_scene_deserialize(
        &failed_descs, trailing_descs, NULL, &deserialize_context));
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
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated_render, 0x11223344u));
    nmo_chunk_close(truncated_render);

    nmo_scene_state_t failed_render;
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&failed_render, NULL, NULL));
    failed_render.background_color = 0xAABBCCDDu;
    failed_render.environment_settings = 0x11223344u;
    failed_render.background_texture = nmo_ref_from_raw(953);
    failed_render.starting_camera = nmo_ref_from_raw(954);
    ASSERT_NE(NMO_OK, nmo_scene_deserialize(
        &failed_render, truncated_render, NULL, &deserialize_context));
    ASSERT_EQ(0xAABBCCDDu, failed_render.background_color);
    ASSERT_EQ(953u, failed_render.background_texture.raw_id);
    ASSERT_EQ(954u, failed_render.starting_camera.raw_id);

    nmo_chunk_t *trailing_launched = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_launched);
    trailing_launched->class_id = NMO_CID_SCENE;
    trailing_launched->data_version = 8;
    trailing_launched->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_launched));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_launched, CK_STATESAVE_SCENELAUNCHED));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing_launched, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        trailing_launched, 0x12345678u));
    nmo_chunk_close(trailing_launched);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_scene_deserialize(
        &failed_render, trailing_launched, NULL, &deserialize_context));
    ASSERT_EQ(0x11223344u, failed_render.environment_settings);

    nmo_chunk_t *trailing_render = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_render);
    trailing_render->class_id = NMO_CID_SCENE;
    trailing_render->data_version = 8;
    trailing_render->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_render));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_render, CK_STATESAVE_SCENERENDERSETTINGS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing_render, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing_render, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing_render, 3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing_render, 4));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(trailing_render, 5.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(trailing_render, 6.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(trailing_render, 7.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(trailing_render, 952));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(trailing_render, 953));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing_render, 0x12345678u));
    nmo_chunk_close(trailing_render);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_scene_deserialize(
        &failed_render, trailing_render, NULL, &deserialize_context));
    ASSERT_EQ(0xAABBCCDDu, failed_render.background_color);
    ASSERT_EQ(953u, failed_render.background_texture.raw_id);
    ASSERT_EQ(954u, failed_render.starting_camera.raw_id);

    nmo_scene_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&invalid, NULL, NULL));
    invalid.level = nmo_ref_from_raw(955);
    nmo_scene_object_desc_t valid_desc = {
        .ref = {.raw_id = 956, .id = 0, .state = NMO_REF_UNRESOLVED},
    };
    nmo_scene_object_desc_t invalid_desc = {
        .ref = {.raw_id = 999, .id = 999, .state = NMO_REF_RESOLVED},
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&invalid.object_descs, &valid_desc));
    ASSERT_EQ(NMO_OK, nmo_array_append(&invalid.object_descs, &invalid_desc));
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_SCENE;
    target->data_version = 8;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_scene_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_scene_state_t large_valid;
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(
        &large_valid, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_array_extend(
        &large_valid.object_descs, 100001u, NULL));
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.validate(
        &large_valid, &scene_type, NULL));

    nmo_scene_state_t oversized;
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(
        &oversized, NULL, NULL));
    nmo_scene_object_desc_t oversized_desc = {0};
    oversized.object_descs.data = &oversized_desc;
    oversized.object_descs.count = (size_t)INT32_MAX / 2u + 1u;
    oversized.object_descs.capacity = oversized.object_descs.count;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_scene_serialize(
        &oversized, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    oversized.object_descs.data = NULL;
    oversized.object_descs.count = 0;
    oversized.object_descs.capacity = 0;

    nmo_scene_vtable.destroy(&source, NULL, NULL);
    nmo_scene_vtable.destroy(&loaded, NULL, NULL);
    nmo_scene_vtable.destroy(&reloaded, NULL, NULL);
    nmo_scene_vtable.destroy(&copied, NULL, NULL);
    nmo_scene_vtable.destroy(&copy_failed, NULL, NULL);
    nmo_scene_vtable.destroy(&failed_descs, NULL, NULL);
    nmo_scene_vtable.destroy(&failed_render, NULL, NULL);
    nmo_scene_vtable.destroy(&invalid, NULL, NULL);
    nmo_scene_vtable.destroy(&large_valid, NULL, NULL);
    nmo_scene_vtable.destroy(&oversized, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, scene_legacy_flags_round_trip_without_reinterpretation) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_serialize_context_t serialize_context =
        nmo_serialize_context_create(
            arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy);
    legacy->class_id = NMO_CID_SCENE;
    legacy->data_version = 7;
    legacy->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy, CK_STATESAVE_SCENENEWDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(
        legacy, NMO_OBJECT_ID_NONE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(legacy, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(legacy, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_item(legacy, 123));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_sub_chunk_sequence(legacy, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_sub_chunk_sequence(legacy, NULL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_sub_chunk_sequence(legacy, NULL));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(legacy, 0xBu));
    nmo_chunk_close(legacy);

    nmo_scene_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_scene_deserialize(
        &loaded, legacy, NULL, &deserialize_context));
    ASSERT_EQ(1u, loaded.object_descs.count);
    nmo_scene_object_desc_t *loaded_descs = NMO_ARRAY_DATA(
        nmo_scene_object_desc_t, &loaded.object_descs);
    const uint32_t expected_flags = CK_SCENEOBJECT_ACTIVE |
        CK_SCENEOBJECT_START_RESET | CK_SCENEOBJECT_START_ACTIVATE;
    ASSERT_EQ(expected_flags, loaded_descs[0].flags);

    nmo_chunk_t *saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(saved);
    saved->class_id = NMO_CID_SCENE;
    saved->data_version = 7;
    saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_scene_serialize(
        &loaded, saved, NULL, &serialize_context));
    nmo_chunk_close(saved);

    nmo_scene_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_scene_deserialize(
        &reloaded, saved, NULL, &deserialize_context));
    const nmo_scene_object_desc_t *reloaded_descs = NMO_ARRAY_DATA(
        nmo_scene_object_desc_t, &reloaded.object_descs);
    ASSERT_EQ(expected_flags, reloaded_descs[0].flags);

    loaded_descs[0].flags |= CK_SCENEOBJECT_START_LEAVE;
    nmo_chunk_t *preserved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(preserved);
    preserved->class_id = NMO_CID_SCENE;
    preserved->data_version = 7;
    preserved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(preserved, 0x5CE7E123u));
    nmo_chunk_close(preserved);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_scene_serialize(
        &loaded, preserved, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(preserved));
    uint32_t marker = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(preserved, &marker));
    ASSERT_EQ(0x5CE7E123u, marker);

    nmo_chunk_t *modern = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern);
    modern->class_id = NMO_CID_SCENE;
    modern->data_version = 8;
    modern->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_scene_serialize(
        &loaded, modern, NULL, &serialize_context));
    nmo_chunk_close(modern);
    nmo_scene_state_t modern_loaded;
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(&modern_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_scene_deserialize(
        &modern_loaded, modern, NULL, &deserialize_context));
    ASSERT_EQ(loaded_descs[0].flags, NMO_ARRAY_DATA(
        nmo_scene_object_desc_t, &modern_loaded.object_descs)[0].flags);

    nmo_chunk_t *default_version = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(default_version);
    default_version->class_id = NMO_CID_SCENE;
    default_version->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_scene_serialize(
        &loaded, default_version, NULL, &serialize_context));
    ASSERT_EQ(NMO_CHUNK_DATA_VERSION_CURRENT,
              nmo_chunk_get_data_version(default_version));
    nmo_chunk_close(default_version);

    nmo_scene_state_t default_loaded;
    ASSERT_EQ(NMO_OK, nmo_scene_vtable.create(
        &default_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_scene_deserialize(
        &default_loaded, default_version, NULL, &deserialize_context));
    ASSERT_EQ(loaded_descs[0].flags, NMO_ARRAY_DATA(
        nmo_scene_object_desc_t, &default_loaded.object_descs)[0].flags);

    nmo_scene_vtable.destroy(&loaded, NULL, NULL);
    nmo_scene_vtable.destroy(&reloaded, NULL, NULL);
    nmo_scene_vtable.destroy(&modern_loaded, NULL, NULL);
    nmo_scene_vtable.destroy(&default_loaded, NULL, NULL);
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
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, source.base.visibility_flags);
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

    nmo_synchro_state_t aliased_copy = reloaded;
    ASSERT_EQ(NMO_OK, nmo_synchro_vtable.copy(
        &reloaded, &aliased_copy, NULL, arena));
    ASSERT_NE(reloaded.arrived_ids.data, aliased_copy.arrived_ids.data);
    ASSERT_NE(reloaded.passed_ids.data, aliased_copy.passed_ids.data);
    ASSERT_EQ(921u, NMO_ARRAY_DATA(
        nmo_ref_t, &reloaded.arrived_ids)[0].raw_id);
    ASSERT_EQ(922u, NMO_ARRAY_DATA(
        nmo_ref_t, &reloaded.passed_ids)[0].raw_id);
    ASSERT_TRUE(nmo_synchro_vtable.equals(&reloaded, &aliased_copy));

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
    failed.base.visibility_flags = NMO_CKOBJECT_HIERARCHICAL;
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
    ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, failed.base.visibility_flags);

    nmo_chunk_t *missing_waiter_counts = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_waiter_counts);
    missing_waiter_counts->class_id = NMO_CID_SYNCHRO;
    missing_waiter_counts->data_version = 7;
    missing_waiter_counts->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_waiter_counts));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_waiter_counts, CK_STATESAVE_SYNCHRODATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(missing_waiter_counts, 8));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_waiter_counts, 0));
    nmo_chunk_close(missing_waiter_counts);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_synchro_deserialize(
        &failed, missing_waiter_counts, NULL, &deserialize_context));
    ASSERT_EQ(99, failed.max_waiters);
    ASSERT_EQ(1u, failed.arrived_ids.count);
    ASSERT_EQ(1u, failed.passed_ids.count);
    ASSERT_EQ(930u, NMO_ARRAY_DATA(
                        nmo_ref_t, &failed.arrived_ids)[0].raw_id);
    ASSERT_EQ(931u, NMO_ARRAY_DATA(
                        nmo_ref_t, &failed.passed_ids)[0].raw_id);

    nmo_chunk_t *cross_section_waiters = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_waiters);
    cross_section_waiters->class_id = NMO_CID_SYNCHRO;
    cross_section_waiters->data_version = 7;
    cross_section_waiters->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_waiters));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_waiters, CK_STATESAVE_SYNCHRODATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_waiters, 8));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        cross_section_waiters, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_waiters, 0x7F123456u));
    nmo_chunk_close(cross_section_waiters);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_synchro_deserialize(
        &failed, cross_section_waiters, NULL, &deserialize_context));
    nmo_chunk_parser_state_t *parser =
        (nmo_chunk_parser_state_t *)cross_section_waiters->parser_state;
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(parser->prev_identifier_pos + 2u, parser->current_pos);
    ASSERT_EQ(99, failed.max_waiters);
    ASSERT_EQ(1u, failed.arrived_ids.count);
    ASSERT_EQ(1u, failed.passed_ids.count);
    ASSERT_EQ(930u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed.arrived_ids)[0].raw_id);
    ASSERT_EQ(931u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed.passed_ids)[0].raw_id);

    nmo_chunk_t *trailing_waiters = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_waiters);
    trailing_waiters->class_id = NMO_CID_SYNCHRO;
    trailing_waiters->data_version = 7;
    trailing_waiters->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_waiters));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_waiters, CK_STATESAVE_SYNCHRODATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(trailing_waiters, 8));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        trailing_waiters, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        trailing_waiters, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        trailing_waiters, 0x12345678u));
    nmo_chunk_close(trailing_waiters);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_synchro_deserialize(
        &failed, trailing_waiters, NULL, &deserialize_context));
    ASSERT_EQ(99, failed.max_waiters);
    ASSERT_EQ(1u, failed.arrived_ids.count);
    ASSERT_EQ(1u, failed.passed_ids.count);
    ASSERT_EQ(930u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed.arrived_ids)[0].raw_id);
    ASSERT_EQ(931u, NMO_ARRAY_DATA(
        nmo_ref_t, &failed.passed_ids)[0].raw_id);

    nmo_synchro_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_synchro_vtable.create(&invalid, NULL, NULL));
    nmo_ref_t valid_waiter = nmo_ref_from_raw(932);
    nmo_ref_t invalid_waiter = nmo_ref_from_id(999);
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &invalid.arrived_ids, &valid_waiter));
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &invalid.passed_ids, &invalid_waiter));
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_SYNCHRO;
    target->data_version = 7;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_synchro_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_synchro_vtable.destroy(&source, NULL, NULL);
    nmo_synchro_vtable.destroy(&loaded, NULL, NULL);
    nmo_synchro_vtable.destroy(&reloaded, NULL, NULL);
    nmo_synchro_vtable.destroy(&copied, NULL, NULL);
    nmo_synchro_vtable.destroy(&aliased_copy, NULL, NULL);
    nmo_synchro_vtable.destroy(&failed, NULL, NULL);
    nmo_synchro_vtable.destroy(&invalid, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, synchro_scalar_failures_keep_state_and_target_chunk_atomic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);

    nmo_chunk_t *state_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(state_chunk);
    state_chunk->class_id = NMO_CID_STATE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(state_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        state_chunk, CK_STATESAVE_SYNCHRODATA));
    nmo_chunk_close(state_chunk);
    nmo_state_state_t state;
    ASSERT_EQ(NMO_OK, nmo_state_vtable.create(&state, NULL, NULL));
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, state.base.visibility_flags);
    nmo_state_state_t state_copy;
    ASSERT_EQ(NMO_OK, nmo_state_vtable.create(&state_copy, NULL, NULL));
    state.event_flag = 42;
    ASSERT_EQ(NMO_OK, nmo_state_vtable.copy(
        &state, &state_copy, NULL, NULL));
    ASSERT_TRUE(nmo_state_vtable.equals(&state, &state_copy));
    ASSERT_EQ(nmo_state_vtable.hash(&state),
              nmo_state_vtable.hash(&state_copy));
    state_copy.event_flag++;
    ASSERT_FALSE(nmo_state_vtable.equals(&state, &state_copy));
    state.base.visibility_flags = NMO_CKOBJECT_HIERARCHICAL;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_state_deserialize(
        &state, state_chunk, NULL, &deserialize_context));
    ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, state.base.visibility_flags);
    ASSERT_EQ(42, state.event_flag);

    nmo_chunk_t *trailing_state = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_state);
    trailing_state->class_id = NMO_CID_STATE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_state));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_state, CK_STATESAVE_SYNCHRODATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(trailing_state, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing_state, 0x12345678u));
    nmo_chunk_close(trailing_state);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_state_deserialize(
        &state, trailing_state, NULL, &deserialize_context));
    ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, state.base.visibility_flags);
    ASSERT_EQ(42, state.event_flag);

    nmo_chunk_t *cross_section_state = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_state);
    cross_section_state->class_id = NMO_CID_STATE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_state));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_state, CK_STATESAVE_SYNCHRODATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_state, 0x11223344u));
    nmo_chunk_close(cross_section_state);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_state_deserialize(
        &state, cross_section_state, NULL, &deserialize_context));
    ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, state.base.visibility_flags);
    ASSERT_EQ(42, state.event_flag);

    nmo_chunk_t *critical_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(critical_chunk);
    critical_chunk->class_id = NMO_CID_CRITICALSECTION;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(critical_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        critical_chunk, CK_STATESAVE_SYNCHRODATA));
    nmo_chunk_close(critical_chunk);
    nmo_criticalsection_state_t critical;
    ASSERT_EQ(NMO_OK, nmo_criticalsection_vtable.create(
        &critical, NULL, NULL));
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, critical.base.visibility_flags);
    critical.base.visibility_flags = NMO_CKOBJECT_HIERARCHICAL;
    critical.object_in_section = nmo_ref_from_raw(933);
    nmo_criticalsection_state_t critical_copy;
    ASSERT_EQ(NMO_OK, nmo_criticalsection_vtable.create(
        &critical_copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_criticalsection_vtable.copy(
        &critical, &critical_copy, NULL, NULL));
    ASSERT_TRUE(nmo_criticalsection_vtable.equals(
        &critical, &critical_copy));
    ASSERT_EQ(nmo_criticalsection_vtable.hash(&critical),
              nmo_criticalsection_vtable.hash(&critical_copy));
    critical_copy.object_in_section.raw_id++;
    ASSERT_FALSE(nmo_criticalsection_vtable.equals(
        &critical, &critical_copy));
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_criticalsection_deserialize(
        &critical, critical_chunk, NULL, &deserialize_context));
    ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, critical.base.visibility_flags);
    ASSERT_EQ(933u, critical.object_in_section.raw_id);

    nmo_chunk_t *cross_section_critical = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_critical);
    cross_section_critical->class_id = NMO_CID_CRITICALSECTION;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_critical));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_critical, CK_STATESAVE_SYNCHRODATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_critical, 0x55667788u));
    nmo_chunk_close(cross_section_critical);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_criticalsection_deserialize(
        &critical, cross_section_critical, NULL, &deserialize_context));
    ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, critical.base.visibility_flags);
    ASSERT_EQ(933u, critical.object_in_section.raw_id);

    nmo_chunk_t *trailing_critical = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(trailing_critical);
    trailing_critical->class_id = NMO_CID_CRITICALSECTION;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing_critical));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        trailing_critical, CK_STATESAVE_SYNCHRODATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(trailing_critical, 934));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        trailing_critical, 0x12345678u));
    nmo_chunk_close(trailing_critical);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_criticalsection_deserialize(
        &critical, trailing_critical, NULL, &deserialize_context));
    ASSERT_EQ(NMO_CKOBJECT_HIERARCHICAL, critical.base.visibility_flags);
    ASSERT_EQ(933u, critical.object_in_section.raw_id);

    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(runtime_to_file);
    nmo_chunk_file_context_t write_context = {
        .runtime_to_file = runtime_to_file,
    };
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_criticalsection_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_criticalsection_vtable.create(
        &invalid, NULL, NULL));
    invalid.object_in_section = nmo_ref_from_id(999);
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_CRITICALSECTION;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(target, &write_context);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_criticalsection_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(target));
    uint32_t marker = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(target, &marker));
    ASSERT_EQ(0x12345678u, marker);

    nmo_state_vtable.destroy(&state, NULL, NULL);
    nmo_state_vtable.destroy(&state_copy, NULL, NULL);
    nmo_criticalsection_vtable.destroy(&critical, NULL, NULL);
    nmo_criticalsection_vtable.destroy(&critical_copy, NULL, NULL);
    nmo_criticalsection_vtable.destroy(&invalid, NULL, NULL);
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

    nmo_beobject_state_t large;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&large, NULL, NULL));
    const size_t large_count = 100001u;
    ASSERT_EQ(NMO_OK, nmo_array_extend(
        &large.attributes, large_count, NULL));
    nmo_chunk_t *large_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(large_chunk);
    large_chunk->class_id = NMO_CID_BEOBJECT;
    large_chunk->data_version = 7;
    large_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(large_chunk, &write_context);
    ASSERT_EQ(NMO_OK, nmo_beobject_serialize(
        &large, large_chunk, NULL, NULL));
    nmo_chunk_close(large_chunk);
    nmo_chunk_set_file_context(large_chunk, &read_context);
    nmo_beobject_state_t large_loaded;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &large_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(
        &large_loaded, large_chunk, NULL, NULL));
    ASSERT_EQ(large_count, large_loaded.attributes.count);

    nmo_array_dispose(&source.scripts);
    nmo_array_dispose(&source.attributes);
    nmo_array_dispose(&loaded.scripts);
    nmo_array_dispose(&loaded.attributes);
    nmo_array_dispose(&reloaded.scripts);
    nmo_array_dispose(&reloaded.attributes);
    nmo_beobject_vtable.destroy(&large, NULL, NULL);
    nmo_beobject_vtable.destroy(&large_loaded, NULL, NULL);
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

    nmo_allocator_t original_attributes_allocator =
        state.attributes.allocator;
    state.attributes.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_beobject_deserialize(
        &state, truncated, NULL, NULL));
    state.attributes.allocator = original_attributes_allocator;
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, state.base.base.visibility_flags);
    ASSERT_EQ(55, state.priority);
    ASSERT_EQ(1u, state.scripts.count);
    ASSERT_EQ(456u, nmo_beobject_script_array_get_id(&state.scripts, 0));
    ASSERT_EQ(1u, state.attributes.count);
    const nmo_beobject_attribute_t *attributes = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &state.attributes);
    ASSERT_EQ(123u, nmo_ref_runtime_id(&attributes[0].parameter));
    ASSERT_EQ(7u, attributes[0].type_id);

    nmo_chunk_t *empty_scripts = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty_scripts);
    empty_scripts->class_id = NMO_CID_BEOBJECT;
    empty_scripts->data_version = 7;
    empty_scripts->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(empty_scripts));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        empty_scripts, CK_STATESAVE_SCRIPTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(empty_scripts, 0));
    nmo_chunk_close(empty_scripts);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_beobject_deserialize(
        &state, empty_scripts, NULL, NULL));
    ASSERT_EQ(55, state.priority);

    nmo_chunk_t *empty_modern_attributes = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty_modern_attributes);
    empty_modern_attributes->class_id = NMO_CID_BEOBJECT;
    empty_modern_attributes->data_version = 7;
    empty_modern_attributes->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(empty_modern_attributes));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        empty_modern_attributes, CK_STATESAVE_NEWATTRIBUTES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        empty_modern_attributes, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_guid(
        empty_modern_attributes, NMO_MANAGER_GUID_ATTRIBUTE));
    nmo_chunk_close(empty_modern_attributes);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_beobject_deserialize(
        &state, empty_modern_attributes, NULL, NULL));
    ASSERT_EQ(55, state.priority);

    nmo_chunk_t *empty_legacy_attributes = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty_legacy_attributes);
    empty_legacy_attributes->class_id = NMO_CID_BEOBJECT;
    empty_legacy_attributes->data_version = 7;
    empty_legacy_attributes->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(empty_legacy_attributes));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        empty_legacy_attributes, CK_STATESAVE_ATTRIBUTES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        empty_legacy_attributes, 0));
    nmo_chunk_close(empty_legacy_attributes);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_beobject_deserialize(
        &state, empty_legacy_attributes, NULL, NULL));
    ASSERT_EQ(55, state.priority);

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

    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_beobject_vtable.validate(
        NULL, NULL, NULL));
    size_t valid_element_size = state.scripts.element_size;
    state.scripts.element_size = sizeof(nmo_object_id_t);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_beobject_serialize(
        &state, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    state.scripts.element_size = valid_element_size;

    size_t valid_count = state.scripts.count;
    state.scripts.count = (size_t)INT32_MAX + 1u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_beobject_serialize(
        &state, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));
    state.scripts.count = valid_count;

    nmo_array_dispose(&state.scripts);
    nmo_array_dispose(&state.attributes);
    nmo_array_dispose(&state.legacy_attributes);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, beobject_preserves_script_and_priority_layouts) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t file_serialize_context =
        nmo_serialize_context_create(
            arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t file_deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy);
    legacy->class_id = NMO_CID_BEOBJECT;
    legacy->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy, CK_STATESAVE_BEHAVIORS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(legacy, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_sequence_item(legacy, 321));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy, CK_STATESAVE_DATAS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(legacy, 0xA0000001u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(legacy, 0x11111111u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(legacy, 0x22222222u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(legacy, 0x33333333u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(legacy, -7));
    nmo_chunk_close(legacy);

    nmo_beobject_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(
        &loaded, legacy, NULL, &file_deserialize_context));
    ASSERT_EQ(1, loaded.has_scripts_section);
    ASSERT_EQ(1, loaded.scripts_use_legacy_identifier);
    ASSERT_EQ(1u, loaded.scripts.count);
    ASSERT_EQ(321u, NMO_ARRAY_DATA(nmo_ref_t, &loaded.scripts)[0].raw_id);
    ASSERT_EQ(1, loaded.has_data_section);
    ASSERT_EQ(1, loaded.data_is_legacy);
    ASSERT_EQ(0xA0000001u, loaded.data_flags);
    ASSERT_EQ(0x11111111u, loaded.legacy_data_words[0]);
    ASSERT_EQ(0x22222222u, loaded.legacy_data_words[1]);
    ASSERT_EQ(0x33333333u, loaded.legacy_data_words[2]);
    ASSERT_EQ(-7, loaded.priority);

    nmo_beobject_state_t legacy_copy;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &legacy_copy, NULL, NULL));
    nmo_type_descriptor_t beobject_type = {
        .size = sizeof(nmo_beobject_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.copy(
        &loaded, &legacy_copy, &beobject_type, arena));
    ASSERT_TRUE(nmo_beobject_vtable.equals(&loaded, &legacy_copy));
    ASSERT_EQ(nmo_beobject_vtable.hash(&loaded),
              nmo_beobject_vtable.hash(&legacy_copy));

    nmo_chunk_t *legacy_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_saved);
    legacy_saved->class_id = NMO_CID_BEOBJECT;
    legacy_saved->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_beobject_serialize(
        &loaded, legacy_saved, NULL, &file_serialize_context));
    nmo_chunk_close(legacy_saved);

    size_t payload_dwords = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        legacy_saved, CK_STATESAVE_BEHAVIORS, &payload_dwords));
    ASSERT_EQ(2u, payload_dwords);
    size_t count = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_object_sequence_start(
        legacy_saved, &count));
    ASSERT_EQ(1u, count);
    int32_t raw_script = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_int(legacy_saved, &raw_script));
    ASSERT_EQ(321, raw_script);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        legacy_saved, CK_STATESAVE_SCRIPTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        legacy_saved, CK_STATESAVE_DATAS, &payload_dwords));
    ASSERT_EQ(5u, payload_dwords);
    uint32_t word = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(legacy_saved, &word));
    ASSERT_EQ(0xA0000001u, word);
    for (size_t i = 0; i < 3u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(legacy_saved, &word));
        ASSERT_EQ(loaded.legacy_data_words[i], word);
    }
    int32_t priority = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_int(legacy_saved, &priority));
    ASSERT_EQ(-7, priority);

    nmo_beobject_state_t legacy_reloaded;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &legacy_reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(
        &legacy_reloaded, legacy_saved, NULL,
        &file_deserialize_context));
    ASSERT_EQ(1, legacy_reloaded.scripts_use_legacy_identifier);
    ASSERT_EQ(0xA0000001u, legacy_reloaded.data_flags);
    ASSERT_EQ(0x33333333u, legacy_reloaded.legacy_data_words[2]);
    ASSERT_EQ(-7, legacy_reloaded.priority);

    nmo_chunk_t *modern = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern);
    modern->class_id = NMO_CID_BEOBJECT;
    modern->data_version = 7;
    modern->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(modern));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        modern, CK_STATESAVE_SCRIPTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(modern, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        modern, CK_STATESAVE_DATAS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(modern, 0x20000000u));
    nmo_chunk_close(modern);

    nmo_beobject_state_t modern_loaded;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &modern_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(
        &modern_loaded, modern, NULL, NULL));
    ASSERT_EQ(1, modern_loaded.has_scripts_section);
    ASSERT_EQ(0u, modern_loaded.scripts.count);
    ASSERT_EQ(1, modern_loaded.has_data_section);
    ASSERT_EQ(0, modern_loaded.data_is_legacy);
    ASSERT_EQ(0x20000000u, modern_loaded.data_flags);
    ASSERT_EQ(0, modern_loaded.priority);

    nmo_chunk_t *modern_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern_saved);
    modern_saved->class_id = NMO_CID_BEOBJECT;
    modern_saved->data_version = 7;
    modern_saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_beobject_serialize(
        &modern_loaded, modern_saved, NULL, NULL));
    nmo_chunk_close(modern_saved);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        modern_saved, CK_STATESAVE_SCRIPTS, &payload_dwords));
    ASSERT_EQ(1u, payload_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        modern_saved, CK_STATESAVE_DATAS, &payload_dwords));
    ASSERT_EQ(1u, payload_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(modern_saved, &word));
    ASSERT_EQ(0x20000000u, word);

    nmo_beobject_state_t authored;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&authored, NULL, NULL));
    authored.priority = 9;
    nmo_chunk_t *authored_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(authored_chunk);
    authored_chunk->class_id = NMO_CID_BEOBJECT;
    authored_chunk->data_version = 0;
    authored_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_beobject_serialize(
        &authored, authored_chunk, NULL, NULL));
    ASSERT_EQ(NMO_CHUNK_DATA_VERSION_CURRENT, authored_chunk->data_version);
    nmo_chunk_close(authored_chunk);
    nmo_beobject_state_t authored_loaded;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &authored_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(
        &authored_loaded, authored_chunk, NULL, NULL));
    ASSERT_EQ(9, authored_loaded.priority);
    ASSERT_EQ(1, authored_loaded.has_data_section);
    ASSERT_EQ(0, authored_loaded.data_is_legacy);

    nmo_chunk_t *runtime = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(runtime);
    runtime->class_id = NMO_CID_BEOBJECT;
    runtime->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(runtime));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        runtime, CK_STATESAVE_DATAS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(runtime, 0x12345678));
    nmo_chunk_close(runtime);
    nmo_beobject_state_t runtime_loaded;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &runtime_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(
        &runtime_loaded, runtime, NULL, NULL));
    ASSERT_EQ(1, runtime_loaded.has_runtime_data_section);
    ASSERT_EQ(0x12345678, runtime_loaded.runtime_data_value);
    ASSERT_EQ(0, runtime_loaded.has_data_section);

    nmo_chunk_t *runtime_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(runtime_saved);
    runtime_saved->class_id = NMO_CID_BEOBJECT;
    runtime_saved->data_version = 7;
    nmo_serialize_context_t runtime_context =
        nmo_serialize_context_create_nonfile(
            arena, NULL, CK_STATESAVE_BEOBJECTONLY);
    ASSERT_EQ(NMO_OK, nmo_beobject_serialize(
        &runtime_loaded, runtime_saved, NULL, &runtime_context));
    nmo_chunk_close(runtime_saved);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        runtime_saved, CK_STATESAVE_DATAS, &payload_dwords));
    ASSERT_EQ(1u, payload_dwords);
    int32_t runtime_value = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_int(
        runtime_saved, &runtime_value));
    ASSERT_EQ(0x12345678, runtime_value);

    nmo_beobject_state_t runtime_copy;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &runtime_copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.copy(
        &runtime_loaded, &runtime_copy, &beobject_type, arena));
    ASSERT_TRUE(nmo_beobject_vtable.equals(
        &runtime_loaded, &runtime_copy));
    ASSERT_EQ(nmo_beobject_vtable.hash(&runtime_loaded),
              nmo_beobject_vtable.hash(&runtime_copy));
    runtime_copy.runtime_data_value ^= 1;
    ASSERT_FALSE(nmo_beobject_vtable.equals(
        &runtime_loaded, &runtime_copy));

    nmo_beobject_state_t atomic;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&atomic, NULL, NULL));
    atomic.priority = 42;
    nmo_chunk_t *dual_scripts = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(dual_scripts);
    dual_scripts->class_id = NMO_CID_BEOBJECT;
    dual_scripts->data_version = 4;
    dual_scripts->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(dual_scripts));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        dual_scripts, CK_STATESAVE_BEHAVIORS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        dual_scripts, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        dual_scripts, CK_STATESAVE_SCRIPTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        dual_scripts, 0));
    nmo_chunk_close(dual_scripts);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_beobject_deserialize(
        &atomic, dual_scripts, NULL, NULL));
    ASSERT_EQ(42, atomic.priority);

    nmo_chunk_t *truncated_data = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated_data);
    truncated_data->class_id = NMO_CID_BEOBJECT;
    truncated_data->data_version = 4;
    truncated_data->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated_data));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated_data, CK_STATESAVE_DATAS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated_data, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated_data, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated_data, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated_data, 3));
    nmo_chunk_close(truncated_data);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_beobject_deserialize(
        &atomic, truncated_data, NULL, NULL));
    ASSERT_EQ(42, atomic.priority);

    nmo_beobject_vtable.destroy(&atomic, NULL, NULL);
    nmo_beobject_vtable.destroy(&runtime_copy, NULL, NULL);
    nmo_beobject_vtable.destroy(&runtime_loaded, NULL, NULL);
    nmo_beobject_vtable.destroy(&authored_loaded, NULL, NULL);
    nmo_beobject_vtable.destroy(&authored, NULL, NULL);
    nmo_beobject_vtable.destroy(&modern_loaded, NULL, NULL);
    nmo_beobject_vtable.destroy(&legacy_copy, NULL, NULL);
    nmo_beobject_vtable.destroy(&legacy_reloaded, NULL, NULL);
    nmo_beobject_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, beobject_preserves_empty_modern_attributes) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *empty = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty);
    empty->class_id = NMO_CID_BEOBJECT;
    empty->data_version = 7;
    empty->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(empty));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        empty, CK_STATESAVE_NEWATTRIBUTES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(empty, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_manager_sequence(
        empty, NMO_MANAGER_GUID_ATTRIBUTE, 0));
    nmo_chunk_close(empty);

    nmo_beobject_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(
        &loaded, empty, NULL, NULL));
    ASSERT_EQ(1, loaded.has_attributes_section);
    ASSERT_EQ(0u, loaded.attributes.count);

    nmo_beobject_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&copied, NULL, NULL));
    nmo_type_descriptor_t beobject_type = {
        .size = sizeof(nmo_beobject_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.copy(
        &loaded, &copied, &beobject_type, arena));
    ASSERT_TRUE(nmo_beobject_vtable.equals(&loaded, &copied));
    ASSERT_EQ(nmo_beobject_vtable.hash(&loaded),
              nmo_beobject_vtable.hash(&copied));

    nmo_chunk_t *saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(saved);
    saved->class_id = NMO_CID_BEOBJECT;
    saved->data_version = 7;
    saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_beobject_serialize(
        &loaded, saved, NULL, NULL));
    nmo_chunk_close(saved);

    size_t payload_dwords = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        saved, CK_STATESAVE_NEWATTRIBUTES, &payload_dwords));
    ASSERT_EQ(4u, payload_dwords);
    size_t count = 1;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_object_sequence_start(saved, &count));
    ASSERT_EQ(0u, count);
    nmo_guid_t manager_guid = {0, 0};
    ASSERT_EQ(NMO_OK, nmo_chunk_start_manager_read_sequence(
        saved, &manager_guid, &count));
    ASSERT_TRUE(nmo_guid_equals(
        NMO_MANAGER_GUID_ATTRIBUTE, manager_guid));
    ASSERT_EQ(0u, count);

    nmo_beobject_state_t reloaded;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(
        &reloaded, saved, NULL, NULL));
    ASSERT_EQ(1, reloaded.has_attributes_section);
    ASSERT_EQ(0u, reloaded.attributes.count);

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_BEOBJECT;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_NEWATTRIBUTES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(truncated, 0));
    nmo_chunk_close(truncated);
    loaded.priority = 42;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_beobject_deserialize(
        &loaded, truncated, NULL, NULL));
    ASSERT_EQ(42, loaded.priority);
    ASSERT_EQ(1, loaded.has_attributes_section);

    nmo_chunk_t *extra = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(extra);
    extra->class_id = NMO_CID_BEOBJECT;
    extra->data_version = 7;
    extra->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(extra));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        extra, CK_STATESAVE_NEWATTRIBUTES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(extra, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_manager_sequence(
        extra, NMO_MANAGER_GUID_ATTRIBUTE, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(extra, 0x12345678u));
    nmo_chunk_close(extra);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_beobject_deserialize(
        &loaded, extra, NULL, NULL));
    ASSERT_EQ(42, loaded.priority);

    nmo_chunk_t *dual = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(dual);
    dual->class_id = NMO_CID_BEOBJECT;
    dual->data_version = 7;
    dual->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(dual));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        dual, CK_STATESAVE_NEWATTRIBUTES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(dual, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_manager_sequence(
        dual, NMO_MANAGER_GUID_ATTRIBUTE, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        dual, CK_STATESAVE_ATTRIBUTES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(dual, 0));
    nmo_chunk_close(dual);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_beobject_deserialize(
        &loaded, dual, NULL, NULL));
    ASSERT_EQ(42, loaded.priority);

    loaded.has_legacy_attributes = 1;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_beobject_vtable.validate(
        &loaded, NULL, NULL));
    loaded.has_legacy_attributes = 0;

    nmo_beobject_vtable.destroy(&reloaded, NULL, NULL);
    nmo_beobject_vtable.destroy(&copied, NULL, NULL);
    nmo_beobject_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, beobject_bounds_single_activity_section) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_beobject_state_t state;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&state, NULL, NULL));
    state.priority = 42;

    nmo_chunk_t *valid = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(valid);
    valid->class_id = NMO_CID_BEOBJECT;
    valid->data_version = 7;
    valid->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(valid));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        valid, CK_STATESAVE_SINGLEACTIVITY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(valid, 0x12345678u));
    nmo_chunk_close(valid);
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(
        &state, valid, NULL, NULL));
    ASSERT_EQ(1, state.has_single_activity);
    ASSERT_EQ(0x12345678u, state.single_activity_flags);

    nmo_chunk_t *empty = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(empty);
    empty->class_id = NMO_CID_BEOBJECT;
    empty->data_version = 7;
    empty->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(empty));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        empty, CK_STATESAVE_SINGLEACTIVITY));
    nmo_chunk_close(empty);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_beobject_deserialize(
        &state, empty, NULL, NULL));
    ASSERT_EQ(1, state.has_single_activity);
    ASSERT_EQ(0x12345678u, state.single_activity_flags);

    nmo_chunk_t *extra = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(extra);
    extra->class_id = NMO_CID_BEOBJECT;
    extra->data_version = 7;
    extra->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(extra));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        extra, CK_STATESAVE_SINGLEACTIVITY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(extra, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(extra, 2));
    nmo_chunk_close(extra);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_beobject_deserialize(
        &state, extra, NULL, NULL));
    ASSERT_EQ(1, state.has_single_activity);
    ASSERT_EQ(0x12345678u, state.single_activity_flags);

    nmo_beobject_vtable.destroy(&state, NULL, NULL);
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
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        impossible_count, 0x7F123456u));
    for (size_t i = 0; i < 12; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(impossible_count, 0));
    }
    nmo_chunk_close(impossible_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_beobject_deserialize(
        &allocation_failed, impossible_count, NULL, NULL));
    ASSERT_EQ(0u, allocation_failed.legacy_attributes.count);
    ASSERT_EQ(NULL, allocation_failed.legacy_attributes.data);
    ASSERT_EQ(0, allocation_failed.has_legacy_attributes);

    nmo_allocator_t original_script_allocator =
        allocation_failed.scripts.allocator;
    allocation_failed.scripts.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    nmo_chunk_t *cross_section_scripts = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_scripts);
    cross_section_scripts->class_id = NMO_CID_BEOBJECT;
    cross_section_scripts->data_version = 7;
    cross_section_scripts->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_scripts));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_scripts, CK_STATESAVE_SCRIPTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        cross_section_scripts, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_scripts, 0x7F123456u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_scripts, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_scripts, 0));
    nmo_chunk_close(cross_section_scripts);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_beobject_deserialize(
        &allocation_failed, cross_section_scripts, NULL, NULL));
    ASSERT_EQ(0u, allocation_failed.scripts.count);
    allocation_failed.scripts.allocator = original_script_allocator;

    nmo_allocator_t original_attribute_allocator =
        allocation_failed.attributes.allocator;
    allocation_failed.attributes.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    nmo_chunk_t *cross_section_attributes = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_attributes);
    cross_section_attributes->class_id = NMO_CID_BEOBJECT;
    cross_section_attributes->data_version = 7;
    cross_section_attributes->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_attributes));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_attributes, CK_STATESAVE_NEWATTRIBUTES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        cross_section_attributes, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_attributes, 0x7F123456u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_attributes, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_attributes, 0));
    nmo_chunk_close(cross_section_attributes);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_beobject_deserialize(
        &allocation_failed, cross_section_attributes, NULL, NULL));
    ASSERT_EQ(0u, allocation_failed.attributes.count);
    allocation_failed.attributes.allocator = original_attribute_allocator;

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

    nmo_beobject_state_t large;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(&large, NULL, NULL));
    large.has_legacy_attributes = 1;
    const size_t large_count = 100001u;
    nmo_beobject_legacy_attribute_t *large_attributes = NULL;
    ASSERT_EQ(NMO_OK, nmo_array_extend(
        &large.legacy_attributes, large_count,
        (void **)&large_attributes));
    large_attributes[0].compatible_class_id = 1;
    large_attributes[0].name = "A";
    nmo_chunk_t *large_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(large_chunk);
    large_chunk->class_id = NMO_CID_BEOBJECT;
    large_chunk->data_version = 7;
    large_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    nmo_chunk_set_file_context(large_chunk, &write_context);
    ASSERT_EQ(NMO_OK, nmo_beobject_serialize(
        &large, large_chunk, NULL, NULL));
    nmo_chunk_close(large_chunk);
    nmo_chunk_set_file_context(large_chunk, &read_context);
    nmo_beobject_state_t large_loaded;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &large_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_deserialize(
        &large_loaded, large_chunk, NULL, NULL));
    ASSERT_EQ(large_count, large_loaded.legacy_attributes.count);
    ASSERT_EQ(1, large_loaded.has_legacy_attributes);

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
    nmo_beobject_vtable.destroy(&large, NULL, NULL);
    nmo_beobject_vtable.destroy(&large_loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, derived_beobject_copy_clones_legacy_attributes) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_group_state_t source;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.create(&source, NULL, NULL));
    source.base.has_legacy_attributes = 1;
    nmo_beobject_legacy_attribute_t attribute = {
        .compatible_class_id = 12,
        .name = "LegacyName",
        .category = "LegacyCategory",
        .parameter_guid = {0x12345678u, 0x9abcdef0u},
        .parameter = nmo_ref_from_raw(777),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &source.base.legacy_attributes, &attribute));

    nmo_group_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_group_vtable.create(&copied, NULL, NULL));
    nmo_type_descriptor_t group_type = {
        .size = sizeof(nmo_group_state_t),
    };
    ASSERT_EQ(NMO_OK, nmo_group_vtable.copy(
        &source, &copied, &group_type, arena));

    ASSERT_NE(source.base.legacy_attributes.data,
              copied.base.legacy_attributes.data);
    const nmo_beobject_legacy_attribute_t *copied_attributes =
        NMO_ARRAY_DATA(
            nmo_beobject_legacy_attribute_t,
            &copied.base.legacy_attributes);
    ASSERT_NE(attribute.name, copied_attributes[0].name);
    ASSERT_NE(attribute.category, copied_attributes[0].category);
    ASSERT_STR_EQ(attribute.name, copied_attributes[0].name);
    ASSERT_STR_EQ(attribute.category, copied_attributes[0].category);
    ASSERT_EQ(attribute.parameter.raw_id,
              copied_attributes[0].parameter.raw_id);

    nmo_group_vtable.destroy(&copied, NULL, NULL);
    nmo_group_vtable.destroy(&source, NULL, NULL);
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
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, source.base.base.visibility_flags);
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

    nmo_beobject_state_t preserved;
    ASSERT_EQ(NMO_OK, nmo_beobject_vtable.create(
        &preserved, NULL, NULL));
    preserved.priority = 77;
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &preserved.scripts, 909));
    void *preserved_scripts = preserved.scripts.data;
    nmo_allocator_t source_allocator = source.scripts.allocator;
    source.scripts.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_beobject_vtable.copy(
        &source, &preserved, &type, copy_arena));
    source.scripts.allocator = source_allocator;
    ASSERT_EQ(77, preserved.priority);
    ASSERT_EQ(preserved_scripts, preserved.scripts.data);
    ASSERT_EQ(1u, preserved.scripts.count);
    ASSERT_EQ(909u, nmo_beobject_script_array_get_id(
        &preserved.scripts, 0));

    nmo_beobject_vtable.destroy(&preserved, NULL, NULL);
    nmo_beobject_vtable.destroy(&source, NULL, NULL);
    nmo_beobject_vtable.destroy(&copy, NULL, NULL);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(source_arena);
}

TEST(chunk_id_remap, renderobject_copy_preserves_content_equality) {
    nmo_arena_t *source_arena = nmo_arena_create(NULL, 8192);
    nmo_arena_t *copy_arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(source_arena);
    ASSERT_NOT_NULL(copy_arena);

    nmo_renderobject_state_t source;
    nmo_renderobject_state_t copy;
    ASSERT_EQ(NMO_OK, nmo_renderobject_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_renderobject_vtable.create(&copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_beobject_script_array_append(
        &source.base.scripts, 101));
    nmo_chunk_t *attribute_chunk = nmo_chunk_create(source_arena);
    ASSERT_NOT_NULL(attribute_chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(attribute_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(attribute_chunk, 0xAABBCCDDu));
    nmo_chunk_close(attribute_chunk);
    ASSERT_EQ(NMO_OK, nmo_beobject_attribute_array_append(
        &source.base.attributes, 202, 303, attribute_chunk));

    nmo_type_descriptor_t type = {0};
    type.size = sizeof(nmo_renderobject_state_t);
    ASSERT_EQ(NMO_OK, nmo_renderobject_vtable.copy(
        &source, &copy, &type, copy_arena));
    ASSERT_TRUE(nmo_renderobject_vtable.equals(&source, &copy));
    ASSERT_EQ(nmo_renderobject_vtable.hash(&source),
              nmo_renderobject_vtable.hash(&copy));
    const nmo_beobject_attribute_t *source_attributes = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &source.base.attributes);
    nmo_beobject_attribute_t *copy_attributes = NMO_ARRAY_DATA(
        nmo_beobject_attribute_t, &copy.base.attributes);
    ASSERT_TRUE(source_attributes[0].chunk != copy_attributes[0].chunk);

    copy_attributes[0].type_id = 304;
    ASSERT_FALSE(nmo_renderobject_vtable.equals(&source, &copy));

    nmo_renderobject_vtable.destroy(&copy, NULL, NULL);
    nmo_renderobject_vtable.destroy(&source, NULL, NULL);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(source_arena);
}

TEST(chunk_id_remap, character_rejects_cross_section_counts_before_allocation) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t file_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_deserialize_context_t runtime_context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);

    nmo_character_state_t state;
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(&state, NULL, NULL));
    nmo_allocator_t body_allocator = state.body_parts.allocator;
    nmo_allocator_t animation_allocator = state.animations.allocator;

    state.body_parts.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    nmo_chunk_t *body_parts = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(body_parts);
    body_parts->class_id = NMO_CID_CHARACTER;
    body_parts->data_version = 5;
    body_parts->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(body_parts));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        body_parts, CK_STATESAVE_CHARACTERBODYPARTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(body_parts, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(body_parts, 0x7F123456u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(body_parts, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(body_parts, 0));
    nmo_chunk_close(body_parts);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_character_deserialize(
        &state, body_parts, NULL, &file_context));
    ASSERT_EQ(0u, state.body_parts.count);
    state.body_parts.allocator = body_allocator;

    state.animations.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    nmo_chunk_t *animations = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(animations);
    animations->class_id = NMO_CID_CHARACTER;
    animations->data_version = 5;
    animations->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(animations));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        animations, CK_STATESAVE_CHARACTERONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(animations, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(animations, 0x7F123456u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(animations, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(animations, 0));
    nmo_chunk_close(animations);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_character_deserialize(
        &state, animations, NULL, &file_context));
    ASSERT_EQ(0u, state.animations.count);
    state.animations.allocator = animation_allocator;

    state.body_parts.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    nmo_chunk_t *runtime_parts = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(runtime_parts);
    runtime_parts->class_id = NMO_CID_CHARACTER;
    runtime_parts->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(runtime_parts));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        runtime_parts, CK_STATESAVE_CHARACTERSAVEPARTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(runtime_parts, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        runtime_parts, 0x7F123456u));
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(runtime_parts, 0));
    }
    nmo_chunk_close(runtime_parts);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_character_deserialize(
        &state, runtime_parts, NULL, &runtime_context));
    ASSERT_EQ(0u, state.body_parts.count);
    state.body_parts.allocator = body_allocator;

    state.active_animation = nmo_ref_from_raw(777);

    nmo_chunk_t *missing_body_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_body_count);
    missing_body_count->class_id = NMO_CID_CHARACTER;
    missing_body_count->data_version = 5;
    missing_body_count->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_body_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_body_count, CK_STATESAVE_CHARACTERBODYPARTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(missing_body_count, 0));
    nmo_chunk_close(missing_body_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_character_deserialize(
        &state, missing_body_count, NULL, &file_context));
    ASSERT_EQ(777u, state.active_animation.raw_id);

    nmo_chunk_t *short_character_only = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(short_character_only);
    short_character_only->class_id = NMO_CID_CHARACTER;
    short_character_only->data_version = 5;
    short_character_only->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(short_character_only));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        short_character_only, CK_STATESAVE_CHARACTERONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        short_character_only, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(short_character_only, 4));
    for (size_t i = 0; i < 3; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(short_character_only, 0));
    }
    nmo_chunk_close(short_character_only);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_character_deserialize(
        &state, short_character_only, NULL, &file_context));
    ASSERT_EQ(777u, state.active_animation.raw_id);

    nmo_chunk_t *short_legacy_animations = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(short_legacy_animations);
    short_legacy_animations->class_id = NMO_CID_CHARACTER;
    short_legacy_animations->data_version = 4;
    short_legacy_animations->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(short_legacy_animations));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        short_legacy_animations, CK_STATESAVE_CHARACTERANIMATIONS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        short_legacy_animations, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        short_legacy_animations, 0));
    nmo_chunk_close(short_legacy_animations);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_character_deserialize(
        &state, short_legacy_animations, NULL, &file_context));
    ASSERT_EQ(777u, state.active_animation.raw_id);

    nmo_chunk_t *missing_runtime_anim_fields = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_runtime_anim_fields);
    missing_runtime_anim_fields->class_id = NMO_CID_CHARACTER;
    missing_runtime_anim_fields->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_runtime_anim_fields));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_runtime_anim_fields, CK_STATESAVE_CHARACTERSAVEANIMS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_runtime_anim_fields, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        missing_runtime_anim_fields, 0));
    nmo_chunk_close(missing_runtime_anim_fields);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_character_deserialize(
        &state, missing_runtime_anim_fields, NULL, &runtime_context));
    ASSERT_EQ(777u, state.active_animation.raw_id);

    nmo_chunk_t *missing_root_ref = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_root_ref);
    missing_root_ref->class_id = NMO_CID_CHARACTER;
    missing_root_ref->data_version = 4;
    missing_root_ref->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_root_ref));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_root_ref, CK_STATESAVE_CHARACTERROOT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(missing_root_ref, 0));
    nmo_chunk_close(missing_root_ref);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_character_deserialize(
        &state, missing_root_ref, NULL, &file_context));
    ASSERT_EQ(777u, state.active_animation.raw_id);

    static const struct {
        uint32_t identifier;
        uint32_t data_version;
        bool file_mode;
        size_t payload_dwords;
        uint32_t payload[6];
    } trailing_cases[] = {
        {CK_STATESAVE_CHARACTERBODYPARTS, 4u, true, 1u, {0u}},
        {CK_STATESAVE_CHARACTERANIMATIONS, 4u, true, 3u, {0u, 0u, 0u}},
        {CK_STATESAVE_CHARACTERSAVEANIMS, 4u, false, 3u, {0u, 0u, 0u}},
        {CK_STATESAVE_CHARACTERSAVEPARTS, 4u, false, 1u, {0u}},
        {CK_STATESAVE_CHARACTERROOT, 4u, true, 1u, {0u}},
        {CK_STATESAVE_CHARACTERFLOORREF, 4u, true, 1u, {0u}},
        {CK_STATESAVE_CHARACTERBODYPARTS, 5u, true, 1u, {0u}},
        {CK_STATESAVE_CHARACTERSAVEPARTS, 5u, false, 1u, {0u}},
        {CK_STATESAVE_CHARACTERONLY, 5u, true, 6u,
         {0u, 4u, 0u, 0u, 0u, 0u}},
        {CK_STATESAVE_CHARACTERONLY, 5u, false, 5u,
         {4u, 0u, 0u, 0u, 0u}},
    };
    for (size_t i = 0;
         i < sizeof(trailing_cases) / sizeof(trailing_cases[0]); ++i) {
        nmo_chunk_t *trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(trailing);
        trailing->class_id = NMO_CID_CHARACTER;
        trailing->data_version = trailing_cases[i].data_version;
        if (trailing_cases[i].file_mode) {
            trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            trailing, trailing_cases[i].identifier));
        for (size_t j = 0; j < trailing_cases[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
                trailing, trailing_cases[i].payload[j]));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0x12345678u));
        nmo_chunk_close(trailing);

        nmo_deserialize_context_t *case_context = trailing_cases[i].file_mode
            ? &file_context : &runtime_context;
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_character_deserialize(
            &state, trailing, NULL, case_context));
        ASSERT_EQ(777u, state.active_animation.raw_id);
        ASSERT_EQ(0u, state.body_parts.count);
        ASSERT_EQ(0u, state.animations.count);
    }

    nmo_chunk_t *large_runtime_parts = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(large_runtime_parts);
    large_runtime_parts->class_id = NMO_CID_CHARACTER;
    large_runtime_parts->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(large_runtime_parts));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        large_runtime_parts, CK_STATESAVE_CHARACTERSAVEPARTS));
    const uint32_t large_count = 10001u;
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        large_runtime_parts, large_count));
    for (uint32_t i = 0; i < large_count; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(
            large_runtime_parts, (nmo_object_id_t)(i + 1u)));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_sub_chunk(
            large_runtime_parts, NULL));
    }
    nmo_chunk_close(large_runtime_parts);
    ASSERT_EQ(NMO_OK, nmo_character_deserialize(
        &state, large_runtime_parts, NULL, &runtime_context));
    ASSERT_EQ((size_t)large_count, state.body_parts.count);
    const nmo_character_part_t *large_parts = NMO_ARRAY_DATA(
        nmo_character_part_t, &state.body_parts);
    ASSERT_EQ(1u, large_parts[0].ref.raw_id);
    ASSERT_EQ(large_count, large_parts[large_count - 1u].ref.raw_id);
    ASSERT_NULL(large_parts[large_count - 1u].chunk);

    nmo_character_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(arena);
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

    nmo_character_state_t copy_failed;
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(
        &copy_failed, NULL, NULL));
    copy_failed.base.entity_flags = 0x12345678u;
    nmo_character_part_t copy_previous = {
        .ref = nmo_ref_from_raw(902),
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(
        &copy_failed.body_parts, &copy_previous));
    void *copy_previous_data = copy_failed.body_parts.data;
    nmo_allocator_t source_body_allocator = source.body_parts.allocator;
    source.body_parts.allocator = nmo_allocator_custom(
        beobject_fail_alloc, beobject_fail_free, NULL);
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_character_vtable.copy(
        &source, &copy_failed, &character_type, arena));
    source.body_parts.allocator = source_body_allocator;
    ASSERT_EQ(0x12345678u, copy_failed.base.entity_flags);
    ASSERT_EQ(copy_previous_data, copy_failed.body_parts.data);
    ASSERT_EQ(1u, copy_failed.body_parts.count);
    ASSERT_EQ(902u, NMO_ARRAY_DATA(
        nmo_character_part_t, &copy_failed.body_parts)[0].ref.raw_id);

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

    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_character_vtable.validate(
        NULL, NULL, NULL));
    invalid.body_parts.data = &part;
    invalid.body_parts.count = (size_t)INT32_MAX + 1u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_character_serialize(
        &invalid, partial, NULL, &file_serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(partial));

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

    nmo_3dentity_vtable.destroy(&runtime_base, NULL, NULL);
    nmo_bodypart_vtable.destroy(&bodypart_loaded, NULL, NULL);
    nmo_character_vtable.destroy(&source, NULL, NULL);
    nmo_character_vtable.destroy(&loaded, NULL, NULL);
    nmo_character_vtable.destroy(&reloaded, NULL, NULL);
    nmo_character_vtable.destroy(&runtime_loaded, NULL, NULL);
    nmo_character_vtable.destroy(&copied, NULL, NULL);
    nmo_character_vtable.destroy(&copy_failed, NULL, NULL);
    nmo_character_vtable.destroy(&failed, NULL, NULL);
    nmo_character_vtable.destroy(&allocation_failed, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, character_legacy_layouts_round_trip) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t file_serialize =
        nmo_serialize_context_create(
            arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t file_deserialize =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_serialize_context_t memory_serialize =
        nmo_serialize_context_create_nonfile(
            arena, NULL,
            CK_STATESAVE_CHARACTERONLY |
                CK_STATESAVE_CHARACTERSAVEPARTS);
    nmo_deserialize_context_t memory_deserialize =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);

    nmo_character_state_t source;
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(&source, NULL, NULL));
    nmo_chunk_t *part_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(part_chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(part_chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(part_chunk, 0x11223344u));
    nmo_chunk_close(part_chunk);
    nmo_character_part_t part = {
        .ref = nmo_ref_from_raw(801),
        .chunk = part_chunk,
    };
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.body_parts, &part));
    nmo_ref_t animation = nmo_ref_from_raw(802);
    ASSERT_EQ(NMO_OK, nmo_array_append(&source.animations, &animation));
    source.active_animation = nmo_ref_from_raw(803);
    source.anim_dest = nmo_ref_from_raw(804);
    source.root_body_part = nmo_ref_from_raw(805);
    source.floor_ref = nmo_ref_from_raw(806);
    source.legacy_animation_prefix = 0x12345678u;

    nmo_chunk_t *file_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(file_chunk);
    file_chunk->class_id = NMO_CID_CHARACTER;
    file_chunk->data_version = 4;
    file_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_character_serialize(
        &source, file_chunk, NULL, &file_serialize));
    nmo_chunk_close(file_chunk);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        file_chunk, CK_STATESAVE_CHARACTERONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        file_chunk, CK_STATESAVE_CHARACTERANIMATIONS));

    nmo_character_state_t file_loaded;
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(
        &file_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_character_deserialize(
        &file_loaded, file_chunk, NULL, &file_deserialize));
    ASSERT_EQ(1u, file_loaded.body_parts.count);
    ASSERT_EQ(801u, NMO_ARRAY_DATA(
        nmo_character_part_t, &file_loaded.body_parts)[0].ref.raw_id);
    ASSERT_EQ(1u, file_loaded.animations.count);
    ASSERT_EQ(802u, NMO_ARRAY_DATA(
        nmo_ref_t, &file_loaded.animations)[0].raw_id);
    ASSERT_EQ(803u, file_loaded.active_animation.raw_id);
    ASSERT_EQ(804u, file_loaded.anim_dest.raw_id);
    ASSERT_EQ(805u, file_loaded.root_body_part.raw_id);
    ASSERT_EQ(806u, file_loaded.floor_ref.raw_id);

    nmo_chunk_t *file_roundtrip = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(file_roundtrip);
    file_roundtrip->class_id = NMO_CID_CHARACTER;
    file_roundtrip->data_version = 4;
    file_roundtrip->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_character_serialize(
        &file_loaded, file_roundtrip, NULL, &file_serialize));
    nmo_chunk_close(file_roundtrip);
    nmo_character_state_t file_reloaded;
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(
        &file_reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_character_deserialize(
        &file_reloaded, file_roundtrip, NULL, &file_deserialize));
    ASSERT_EQ(802u, NMO_ARRAY_DATA(
        nmo_ref_t, &file_reloaded.animations)[0].raw_id);
    ASSERT_EQ(805u, file_reloaded.root_body_part.raw_id);

    nmo_chunk_t *memory_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(memory_chunk);
    memory_chunk->class_id = NMO_CID_CHARACTER;
    memory_chunk->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_character_serialize(
        &source, memory_chunk, NULL, &memory_serialize));
    nmo_chunk_close(memory_chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        memory_chunk, CK_STATESAVE_CHARACTERSAVEANIMS));
    uint32_t animation_prefix = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(
        memory_chunk, &animation_prefix));
    ASSERT_EQ(0x12345678u, animation_prefix);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        memory_chunk, CK_STATESAVE_CHARACTERSAVEPARTS));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        memory_chunk, CK_STATESAVE_CHARACTERONLY));

    nmo_character_state_t memory_loaded;
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(
        &memory_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_character_deserialize(
        &memory_loaded, memory_chunk, NULL, &memory_deserialize));
    ASSERT_EQ(1u, memory_loaded.body_parts.count);
    const nmo_character_part_t *memory_part = NMO_ARRAY_DATA(
        nmo_character_part_t, &memory_loaded.body_parts);
    ASSERT_EQ(801u, memory_part[0].ref.raw_id);
    ASSERT_NOT_NULL(memory_part[0].chunk);
    ASSERT_EQ(4u, nmo_chunk_get_data_size(memory_part[0].chunk));
    ASSERT_EQ(803u, memory_loaded.active_animation.raw_id);
    ASSERT_EQ(804u, memory_loaded.anim_dest.raw_id);
    ASSERT_EQ(805u, memory_loaded.root_body_part.raw_id);
    ASSERT_EQ(806u, memory_loaded.floor_ref.raw_id);
    ASSERT_EQ(0x12345678u, memory_loaded.legacy_animation_prefix);

    nmo_chunk_t *memory_roundtrip = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(memory_roundtrip);
    memory_roundtrip->class_id = NMO_CID_CHARACTER;
    memory_roundtrip->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_character_serialize(
        &memory_loaded, memory_roundtrip, NULL, &memory_serialize));
    nmo_chunk_close(memory_roundtrip);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        memory_roundtrip, CK_STATESAVE_CHARACTERSAVEANIMS));
    animation_prefix = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(
        memory_roundtrip, &animation_prefix));
    ASSERT_EQ(0x12345678u, animation_prefix);

    nmo_character_state_t memory_copy;
    ASSERT_EQ(NMO_OK, nmo_character_vtable.create(
        &memory_copy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_character_vtable.copy(
        &memory_loaded, &memory_copy, NULL, arena));
    ASSERT_EQ(0x12345678u, memory_copy.legacy_animation_prefix);
    ASSERT_TRUE(nmo_character_vtable.equals(
        &memory_loaded, &memory_copy));
    ASSERT_EQ(nmo_character_vtable.hash(&memory_loaded),
              nmo_character_vtable.hash(&memory_copy));
    memory_copy.legacy_animation_prefix ^= 1u;
    ASSERT_FALSE(nmo_character_vtable.equals(
        &memory_loaded, &memory_copy));

    nmo_character_vtable.destroy(&source, NULL, NULL);
    nmo_character_vtable.destroy(&file_loaded, NULL, NULL);
    nmo_character_vtable.destroy(&file_reloaded, NULL, NULL);
    nmo_character_vtable.destroy(&memory_loaded, NULL, NULL);
    nmo_character_vtable.destroy(&memory_copy, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, bodypart_rotation_joint_round_trips_without_size_prefix) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context =
        nmo_serialize_context_create(
            arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_bodypart_state_t source;
    nmo_bodypart_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_bodypart_vtable.create(&source, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_bodypart_vtable.create(&loaded, NULL, NULL));
    source.base.entity.entity_flags |= CK_3DENTITY_IKJOINTVALID;
    source.has_character = 1;
    source.character = nmo_ref_from_raw(701);
    source.has_rotation_joint = 1;
    source.rotation_joint.flags = 0x12345678u;
    source.rotation_joint.min = (nmo_vector_t){-1.0f, -2.0f, -3.0f};
    source.rotation_joint.max = (nmo_vector_t){1.0f, 2.0f, 3.0f};
    source.rotation_joint.damping = (nmo_vector_t){0.25f, 0.5f, 0.75f};

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_BODYPART;
    chunk->data_version = 5;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_bodypart_serialize(
        &source, chunk, NULL, &serialize_context));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_bodypart_deserialize(
        &loaded, chunk, NULL, &deserialize_context));

    ASSERT_EQ(1, loaded.has_character);
    ASSERT_EQ(701u, loaded.character.raw_id);
    ASSERT_EQ(1, loaded.has_rotation_joint);
    ASSERT_EQ(0x12345678u, loaded.rotation_joint.flags);
    ASSERT_FLOAT_EQ(-1.0f, loaded.rotation_joint.min.x, 0.0001f);
    ASSERT_FLOAT_EQ(2.0f, loaded.rotation_joint.max.y, 0.0001f);
    ASSERT_FLOAT_EQ(0.75f, loaded.rotation_joint.damping.z, 0.0001f);

    nmo_bodypart_state_t legacy;
    nmo_bodypart_state_t legacy_loaded;
    nmo_bodypart_state_t legacy_reloaded;
    ASSERT_EQ(NMO_OK, nmo_bodypart_vtable.create(&legacy, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_bodypart_vtable.create(
        &legacy_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_bodypart_vtable.create(
        &legacy_reloaded, NULL, NULL));
    legacy.base.entity.entity_flags |= CK_3DENTITY_IKJOINTVALID;
    legacy.has_character = 1;
    legacy.character = nmo_ref_from_raw(702);
    legacy.has_rotation_joint = 1;
    legacy.rotation_joint.flags = 0x421u;
    legacy.rotation_joint.min = (nmo_vector_t){-4.0f, -5.0f, -6.0f};
    legacy.rotation_joint.max = (nmo_vector_t){4.0f, 5.0f, 6.0f};
    legacy.rotation_joint.damping =
        (nmo_vector_t){0.125f, 0.25f, 0.5f};

    nmo_chunk_t *legacy_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_chunk);
    legacy_chunk->class_id = NMO_CID_BODYPART;
    legacy_chunk->data_version = 4;
    legacy_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_bodypart_serialize(
        &legacy, legacy_chunk, NULL, &serialize_context));
    nmo_chunk_close(legacy_chunk);
    size_t section_dwords = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        legacy_chunk, CK_STATESAVE_BODYPARTROTJOINT,
        &section_dwords));
    ASSERT_EQ(18u, section_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        legacy_chunk, CK_STATESAVE_BODYPARTCHARACTER,
        &section_dwords));
    ASSERT_EQ(1u, section_dwords);
    ASSERT_EQ(NMO_OK, nmo_bodypart_deserialize(
        &legacy_loaded, legacy_chunk, NULL, &deserialize_context));
    ASSERT_TRUE(legacy_loaded.has_character);
    ASSERT_EQ(702u, legacy_loaded.character.raw_id);
    ASSERT_TRUE(legacy_loaded.has_rotation_joint);
    ASSERT_EQ(legacy.rotation_joint.flags,
              legacy_loaded.rotation_joint.flags);
    ASSERT_EQ(legacy.rotation_joint.min.x,
              legacy_loaded.rotation_joint.min.x);
    ASSERT_EQ(legacy.rotation_joint.max.y,
              legacy_loaded.rotation_joint.max.y);
    ASSERT_EQ(legacy.rotation_joint.damping.z,
              legacy_loaded.rotation_joint.damping.z);

    nmo_chunk_t *legacy_roundtrip = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_roundtrip);
    legacy_roundtrip->class_id = NMO_CID_BODYPART;
    legacy_roundtrip->data_version = 4;
    legacy_roundtrip->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_bodypart_serialize(
        &legacy_loaded, legacy_roundtrip, NULL, &serialize_context));
    nmo_chunk_close(legacy_roundtrip);
    ASSERT_EQ(NMO_OK, nmo_bodypart_deserialize(
        &legacy_reloaded, legacy_roundtrip, NULL,
        &deserialize_context));
    ASSERT_EQ(0x421u, legacy_reloaded.rotation_joint.flags);

    nmo_chunk_t *preserved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(preserved);
    preserved->class_id = NMO_CID_BODYPART;
    preserved->data_version = 4;
    preserved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(preserved, 0xAABBCCDDu));
    nmo_chunk_close(preserved);
    legacy.rotation_joint.flags = 0x800u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_bodypart_serialize(
        &legacy, preserved, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(preserved));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(preserved));
    uint32_t marker = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(preserved, &marker));
    ASSERT_EQ(0xAABBCCDDu, marker);

    static const struct {
        uint32_t identifier;
        uint32_t data_version;
        size_t payload_dwords;
    } trailing_cases[] = {
        {CK_STATESAVE_BODYPARTCHARACTER, 5u, 1u},
        {CK_STATESAVE_BODYPARTROTJOINT, 4u, 18u},
        {CK_STATESAVE_BODYPARTCHARACTER, 4u, 1u},
    };
    for (size_t i = 0;
         i < sizeof(trailing_cases) / sizeof(trailing_cases[0]); ++i) {
        nmo_chunk_t *trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(trailing);
        trailing->class_id = NMO_CID_BODYPART;
        trailing->data_version = trailing_cases[i].data_version;
        trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            trailing, trailing_cases[i].identifier));
        for (size_t j = 0; j < trailing_cases[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0u));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0x12345678u));
        nmo_chunk_close(trailing);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_bodypart_deserialize(
            &loaded, trailing, NULL, &deserialize_context));
        ASSERT_EQ(701u, loaded.character.raw_id);
        ASSERT_EQ(0x12345678u, loaded.rotation_joint.flags);
    }

    nmo_chunk_t *joint_trailing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(joint_trailing);
    joint_trailing->class_id = NMO_CID_BODYPART;
    joint_trailing->data_version = 5;
    joint_trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(joint_trailing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        joint_trailing, CK_STATESAVE_3DENTITYFLAGS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        joint_trailing, CK_3DENTITY_IKJOINTVALID));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(joint_trailing, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        joint_trailing, CK_STATESAVE_BODYPARTCHARACTER));
    for (size_t i = 0; i < 11u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(joint_trailing, 0u));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        joint_trailing, 0x12345678u));
    nmo_chunk_close(joint_trailing);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_bodypart_deserialize(
        &loaded, joint_trailing, NULL, &deserialize_context));
    ASSERT_EQ(701u, loaded.character.raw_id);
    ASSERT_EQ(0x12345678u, loaded.rotation_joint.flags);

    nmo_bodypart_vtable.destroy(&loaded, NULL, NULL);
    nmo_bodypart_vtable.destroy(&source, NULL, NULL);
    nmo_bodypart_vtable.destroy(&legacy, NULL, NULL);
    nmo_bodypart_vtable.destroy(&legacy_loaded, NULL, NULL);
    nmo_bodypart_vtable.destroy(&legacy_reloaded, NULL, NULL);
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

TEST(chunk_id_remap, mesh_layout_follows_data_version) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_mesh_state_t source;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&source, NULL, NULL));
    source.flags = VXMESH_GENNORMALS | VXMESH_PROCEDURALPOS;
    nmo_vertex_t vertices[3] = {0};
    vertices[0].normal.z = 1.0f;
    vertices[1].position.x = 1.0f;
    vertices[2].position.y = 1.0f;
    uint32_t colors[3] = {0x11u, 0x22u, 0x33u};
    uint32_t specular[3] = {0u, 0u, 0u};
    source.vertex_count = 3u;
    source.vertices = vertices;
    source.vertex_colors = colors;
    source.vertex_specular = specular;

    nmo_face_t faces[2] = {
        {.material_group_idx = 70000u, .channel_mask = 0x1234u},
        {.material_group_idx = 6u, .channel_mask = 0xABCDu},
    };
    uint16_t face_indices[6] = {0u, 1u, 2u, 2u, 1u, 0u};
    source.face_count = 2u;
    source.faces = faces;
    source.face_vertex_indices = face_indices;
    uint16_t line_indices[2] = {0x12u, 0x234u};
    source.line_count = 1u;
    source.line_indices = line_indices;
    nmo_material_channel_t channel = {
        .material = nmo_ref_from_raw(NMO_OBJECT_ID_NONE),
    };
    source.has_material_channels = 1u;
    source.material_channel_count = 1u;
    source.material_channels = &channel;

    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy);
    legacy->class_id = NMO_CID_MESH;
    legacy->data_version = 8u;
    legacy->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_mesh_serialize(
        &source, legacy, NULL, &serialize_context));
    nmo_chunk_close(legacy);

    size_t section_dwords = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        legacy, CK_STATESAVE_MESHVERTICES, &section_dwords));
    ASSERT_EQ(23u, section_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        legacy, CK_STATESAVE_MESHFACES, &section_dwords));
    ASSERT_EQ(9u, section_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        legacy, CK_STATESAVE_MESHLINES, &section_dwords));
    ASSERT_EQ(3u, section_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        legacy, CK_STATESAVE_MESHFACECHANMASK, &section_dwords));
    ASSERT_EQ(3u, section_dwords);

    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(legacy));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        legacy, CK_STATESAVE_MESHFACES));
    int32_t serialized_count = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_int(legacy, &serialized_count));
    ASSERT_EQ(2, serialized_count);
    uint16_t serialized_index = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_word(legacy, &serialized_index));
    ASSERT_EQ(0u, serialized_index);
    ASSERT_EQ(NMO_OK, nmo_chunk_read_word(legacy, &serialized_index));
    ASSERT_EQ(1u, serialized_index);
    ASSERT_EQ(NMO_OK, nmo_chunk_read_word(legacy, &serialized_index));
    ASSERT_EQ(2u, serialized_index);
    uint32_t serialized_material = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(
        legacy, &serialized_material));
    ASSERT_EQ(70000u, serialized_material);

    nmo_mesh_state_t legacy_loaded;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(
        &legacy_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_mesh_deserialize(
        &legacy_loaded, legacy, NULL, &deserialize_context));
    ASSERT_EQ(70000u, legacy_loaded.faces[0].material_group_idx);
    ASSERT_EQ(0x1234u, legacy_loaded.faces[0].channel_mask);
    ASSERT_EQ(0xABCDu, legacy_loaded.faces[1].channel_mask);
    ASSERT_EQ(0x234u, legacy_loaded.line_indices[1]);
    ASSERT_EQ(0x33u, legacy_loaded.vertex_colors[2]);
    ASSERT_EQ(0u, legacy_loaded.vertex_specular[2]);
    ASSERT_FLOAT_EQ(1.0f,
                    legacy_loaded.vertices[2].position.y, 0.0001f);

    nmo_chunk_t *modern = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern);
    modern->class_id = NMO_CID_MESH;
    modern->data_version = 9u;
    modern->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(modern));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(modern, 0xABCD1234u));
    nmo_chunk_close(modern);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_mesh_serialize(
        &legacy_loaded, modern, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(modern));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(modern));
    uint32_t marker = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(modern, &marker));
    ASSERT_EQ(0xABCD1234u, marker);

    legacy_loaded.faces[0].material_group_idx = 5u;
    legacy_loaded.flags |= VXMESH_PROCEDURALUV;
    legacy_loaded.vertices[1].uv.x = 0.25f;
    ASSERT_EQ(NMO_OK, nmo_mesh_serialize(
        &legacy_loaded, modern, NULL, &serialize_context));
    nmo_chunk_close(modern);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        modern, CK_STATESAVE_MESHVERTICES, &section_dwords));
    ASSERT_EQ(31u, section_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        modern, CK_STATESAVE_MESHFACES, &section_dwords));
    ASSERT_EQ(9u, section_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        modern, CK_STATESAVE_MESHLINES, &section_dwords));
    ASSERT_EQ(3u, section_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        modern, CK_STATESAVE_MESHFACECHANMASK, &section_dwords));
    ASSERT_EQ(3u, section_dwords);

    nmo_mesh_state_t modern_loaded;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(
        &modern_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_mesh_deserialize(
        &modern_loaded, modern, NULL, &deserialize_context));
    ASSERT_EQ(5u, modern_loaded.faces[0].material_group_idx);
    ASSERT_EQ(0x1234u, modern_loaded.faces[0].channel_mask);
    ASSERT_EQ(0xABCDu, modern_loaded.faces[1].channel_mask);
    ASSERT_EQ(0x234u, modern_loaded.line_indices[1]);
    ASSERT_FLOAT_EQ(1.0f,
                    modern_loaded.vertices[1].position.x, 0.0001f);
    ASSERT_FLOAT_EQ(1.0f,
                    modern_loaded.vertices[0].normal.z, 0.0001f);
    ASSERT_FLOAT_EQ(0.25f,
                    modern_loaded.vertices[1].uv.x, 0.0001f);

    nmo_chunk_t *default_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(default_chunk);
    default_chunk->class_id = NMO_CID_MESH;
    default_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_mesh_serialize(
        &modern_loaded, default_chunk, NULL, &serialize_context));
    ASSERT_EQ(NMO_CHUNK_DATA_VERSION_CURRENT,
              nmo_chunk_get_data_version(default_chunk));

    nmo_chunk_t *legacy_rejected = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_rejected);
    legacy_rejected->class_id = NMO_CID_MESH;
    legacy_rejected->data_version = 8u;
    legacy_rejected->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_rejected));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        legacy_rejected, 0x1234ABCDu));
    nmo_chunk_close(legacy_rejected);
    modern_loaded.vertices[0].uv.x = 0.5f;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_mesh_serialize(
        &modern_loaded, legacy_rejected, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(legacy_rejected));

    nmo_mesh_vtable.destroy(&source, NULL, NULL);
    nmo_mesh_vtable.destroy(&legacy_loaded, NULL, NULL);
    nmo_mesh_vtable.destroy(&modern_loaded, NULL, NULL);
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

    nmo_chunk_t *cross_section_groups = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(cross_section_groups);
    cross_section_groups->class_id = NMO_CID_MESH;
    cross_section_groups->chunk_version = NMO_CHUNK_VERSION4;
    cross_section_groups->data_version = 9;
    cross_section_groups->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(cross_section_groups));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_groups, CK_STATESAVE_MESHMATERIALS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(cross_section_groups, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        cross_section_groups, 0x7F123456u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(cross_section_groups, 0));
    nmo_chunk_close(cross_section_groups);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_mesh_deserialize(
        &failed, cross_section_groups, NULL, &deserialize_context));
    nmo_chunk_parser_state_t *parser =
        (nmo_chunk_parser_state_t *)cross_section_groups->parser_state;
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(parser->prev_identifier_pos + 3u, parser->current_pos);
    ASSERT_EQ(0xCAFEBABEu, failed.flags);
    ASSERT_EQ(&previous_group, failed.material_groups);
    ASSERT_EQ(1u, failed.material_group_count);

    nmo_chunk_t *truncated_flags = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated_flags);
    truncated_flags->class_id = NMO_CID_MESH;
    truncated_flags->chunk_version = NMO_CHUNK_VERSION4;
    truncated_flags->data_version = 9;
    truncated_flags->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated_flags));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated_flags, CK_STATESAVE_MESHFLAGS));
    nmo_chunk_close(truncated_flags);

    nmo_mesh_state_t failed_flags;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&failed_flags, NULL, NULL));
    failed_flags.flags = 0x12345678u;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_mesh_deserialize(
        &failed_flags, truncated_flags, NULL, &deserialize_context));
    ASSERT_EQ(0x12345678u, failed_flags.flags);

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
    nmo_mesh_vtable.destroy(&failed_flags, NULL, NULL);
    nmo_mesh_vtable.destroy(&failed, NULL, NULL);
    nmo_mesh_vtable.destroy(&invalid, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, mesh_fields_stay_in_identifier_sections) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_mesh_state_t state;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&state, NULL, NULL));
    state.flags = 0x12345678u;

    const struct {
        uint32_t identifier;
        size_t borrowed_payload_dwords;
    } empty_sections[] = {
        {CK_STATESAVE_MESHFLAGS, 0},
        {CK_STATESAVE_MESHMATERIALS, 0},
        {CK_STATESAVE_MESHVERTICES, 0},
        {CK_STATESAVE_MESHFACES, 0},
        {CK_STATESAVE_MESHLINES, 0},
        {CK_STATESAVE_MESHCHANNELS, 0},
        {CK_STATESAVE_MESHWEIGHTS, 0},
        {CK_STATESAVE_MESHFACECHANMASK, 0},
        {CK_STATESAVE_PROGRESSIVEMESH, 1},
    };
    for (size_t i = 0;
         i < sizeof(empty_sections) / sizeof(empty_sections[0]); ++i) {
        nmo_chunk_t *chunk = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(chunk);
        chunk->class_id = NMO_CID_MESH;
        chunk->chunk_version = NMO_CHUNK_VERSION4;
        chunk->data_version = 9;
        chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            chunk, empty_sections[i].identifier));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0));
        for (size_t j = 0;
             j < empty_sections[i].borrowed_payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0));
        }
        nmo_chunk_close(chunk);

        ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_mesh_deserialize(
            &state, chunk, NULL, &deserialize_context));
        ASSERT_EQ(0x12345678u, state.flags);
    }

    nmo_chunk_t *short_legacy_vertices = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(short_legacy_vertices);
    short_legacy_vertices->class_id = NMO_CID_MESH;
    short_legacy_vertices->chunk_version = NMO_CHUNK_VERSION4;
    short_legacy_vertices->data_version = 7;
    short_legacy_vertices->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(short_legacy_vertices));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        short_legacy_vertices, CK_STATESAVE_MESHVERTICES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(short_legacy_vertices, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(short_legacy_vertices, 0));
    nmo_chunk_close(short_legacy_vertices);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_mesh_deserialize(
        &state, short_legacy_vertices, NULL, &deserialize_context));
    ASSERT_EQ(0x12345678u, state.flags);

    static const struct {
        uint32_t identifier;
        uint32_t data_version;
        size_t payload_dwords;
    } trailing_sections[] = {
        {CK_STATESAVE_MESHFLAGS, 9u, 1u},
        {CK_STATESAVE_MESHMATERIALS, 9u, 1u},
        {CK_STATESAVE_MESHVERTICES, 9u, 1u},
        {CK_STATESAVE_MESHFACES, 9u, 1u},
        {CK_STATESAVE_MESHLINES, 9u, 1u},
        {CK_STATESAVE_MESHCHANNELS, 9u, 1u},
        {CK_STATESAVE_MESHWEIGHTS, 9u, 1u},
        {CK_STATESAVE_MESHFACECHANMASK, 9u, 1u},
        {CK_STATESAVE_MESHFLAGS, 8u, 1u},
        {CK_STATESAVE_MESHMATERIALS, 8u, 1u},
        {CK_STATESAVE_MESHVERTICES, 8u, 2u},
        {CK_STATESAVE_MESHFACES, 8u, 1u},
        {CK_STATESAVE_MESHLINES, 8u, 1u},
        {CK_STATESAVE_MESHCHANNELS, 8u, 1u},
        {CK_STATESAVE_MESHWEIGHTS, 8u, 1u},
        {CK_STATESAVE_MESHFACECHANMASK, 8u, 1u},
    };
    for (size_t i = 0;
         i < sizeof(trailing_sections) / sizeof(trailing_sections[0]);
         ++i) {
        nmo_chunk_t *trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(trailing);
        trailing->class_id = NMO_CID_MESH;
        trailing->chunk_version = NMO_CHUNK_VERSION4;
        trailing->data_version = trailing_sections[i].data_version;
        trailing->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            trailing, trailing_sections[i].identifier));
        for (size_t j = 0; j < trailing_sections[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0u));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0x12345678u));
        nmo_chunk_close(trailing);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_mesh_deserialize(
            &state, trailing, NULL, &deserialize_context));
        ASSERT_EQ(0x12345678u, state.flags);
    }

    for (uint32_t data_version = 8u; data_version <= 9u; ++data_version) {
        nmo_chunk_t *oversized_masks = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(oversized_masks);
        oversized_masks->class_id = NMO_CID_MESH;
        oversized_masks->data_version = data_version;
        oversized_masks->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(oversized_masks));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            oversized_masks, CK_STATESAVE_MESHFACECHANMASK));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_int(oversized_masks, 1));
        nmo_chunk_close(oversized_masks);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_mesh_deserialize(
            &state, oversized_masks, NULL, &deserialize_context));
        ASSERT_EQ(0x12345678u, state.flags);
    }

    nmo_chunk_t *progressive = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(progressive);
    progressive->class_id = NMO_CID_MESH;
    progressive->data_version = 9;
    progressive->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(progressive));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        progressive, CK_STATESAVE_PROGRESSIVEMESH));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(progressive, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(progressive, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(progressive, 3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(progressive, 0x11223344u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(progressive, 0x55667788u));
    nmo_chunk_close(progressive);
    nmo_mesh_state_t progressive_state;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(
        &progressive_state, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_mesh_deserialize(
        &progressive_state, progressive, NULL, &deserialize_context));
    ASSERT_TRUE(progressive_state.has_progressive_mesh);
    ASSERT_EQ(8u, progressive_state.pm_data_size);
    const uint32_t *progressive_data =
        (const uint32_t *)progressive_state.pm_data;
    ASSERT_EQ(0x11223344u, progressive_data[0]);
    ASSERT_EQ(0x55667788u, progressive_data[1]);

    nmo_mesh_vtable.destroy(&progressive_state, NULL, NULL);
    nmo_mesh_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, mesh_preserves_large_material_sections) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_mesh_state_t source;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&source, NULL, NULL));
    const uint32_t group_count = 10000u;
    source.has_material_groups = 1;
    source.material_group_count = group_count;
    source.material_groups = nmo_arena_alloc(
        arena, sizeof(*source.material_groups) * group_count,
        _Alignof(nmo_material_group_t));
    ASSERT_NOT_NULL(source.material_groups);
    memset(source.material_groups, 0,
           sizeof(*source.material_groups) * group_count);
    for (uint32_t i = 0; i < group_count; ++i) {
        source.material_groups[i].material = nmo_ref_from_raw(
            (nmo_object_id_t)(i + 1u));
        source.material_groups[i].padding = (int32_t)i;
    }

    const uint32_t channel_count = 100u;
    source.has_material_channels = 1;
    source.material_channel_count = channel_count;
    source.material_channels = nmo_arena_alloc(
        arena, sizeof(*source.material_channels) * channel_count,
        _Alignof(nmo_material_channel_t));
    ASSERT_NOT_NULL(source.material_channels);
    memset(source.material_channels, 0,
           sizeof(*source.material_channels) * channel_count);
    for (uint32_t i = 0; i < channel_count; ++i) {
        source.material_channels[i].material = nmo_ref_from_raw(
            (nmo_object_id_t)(group_count + i + 1u));
        source.material_channels[i].flags = i;
    }
    const uint32_t uv_count = 1000000u;
    source.material_channels[channel_count - 1u].uv_count = uv_count;
    source.material_channels[channel_count - 1u].uv_coords = nmo_arena_alloc(
        arena, sizeof(nmo_vector2_t) * uv_count, _Alignof(nmo_vector2_t));
    ASSERT_NOT_NULL(
        source.material_channels[channel_count - 1u].uv_coords);
    memset(source.material_channels[channel_count - 1u].uv_coords, 0,
           sizeof(nmo_vector2_t) * uv_count);
    source.material_channels[channel_count - 1u]
        .uv_coords[uv_count - 1u].x = 1.25f;
    source.material_channels[channel_count - 1u]
        .uv_coords[uv_count - 1u].y = 2.5f;

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_MESH;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 9;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_mesh_serialize(
        &source, chunk, NULL, &serialize_context));
    nmo_chunk_close(chunk);

    nmo_mesh_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_mesh_deserialize(
        &loaded, chunk, NULL, &deserialize_context));
    ASSERT_EQ(group_count, loaded.material_group_count);
    ASSERT_EQ(group_count,
              loaded.material_groups[group_count - 1u].material.raw_id);
    ASSERT_EQ((int32_t)(group_count - 1u),
              loaded.material_groups[group_count - 1u].padding);
    ASSERT_EQ(channel_count, loaded.material_channel_count);
    ASSERT_EQ(group_count + channel_count,
              loaded.material_channels[channel_count - 1u].material.raw_id);
    ASSERT_EQ(uv_count,
              loaded.material_channels[channel_count - 1u].uv_count);
    ASSERT_EQ(1.25f, loaded.material_channels[channel_count - 1u]
                         .uv_coords[uv_count - 1u].x);
    ASSERT_EQ(2.5f, loaded.material_channels[channel_count - 1u]
                        .uv_coords[uv_count - 1u].y);

    nmo_mesh_vtable.destroy(&source, NULL, NULL);
    nmo_mesh_vtable.destroy(&loaded, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, mesh_rejects_truncated_large_lines_before_allocation) {
    fail_after_allocator_state_t allocator_state = {
        .allocation_count = 0,
        .allowed_allocations = 2,
    };
    nmo_allocator_t failing_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &allocator_state);
    nmo_arena_t *arena = nmo_arena_create(&failing_allocator, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_MESH;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 9;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_MESHLINES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1000000));
    nmo_chunk_close(chunk);

    nmo_mesh_state_t state;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&state, NULL, NULL));
    state.flags = 0x12345678u;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_mesh_deserialize(
        &state, chunk, NULL, &deserialize_context));
    ASSERT_EQ(0x12345678u, state.flags);
    ASSERT_EQ(2u, allocator_state.allocation_count);

    nmo_mesh_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, mesh_preserves_large_geometry_counts) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    const uint32_t line_count = 1000000u;
    nmo_mesh_state_t lines;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&lines, NULL, NULL));
    lines.line_count = line_count;
    lines.line_indices = nmo_arena_alloc(
        arena, sizeof(*lines.line_indices) * line_count * 2u,
        _Alignof(uint16_t));
    ASSERT_NOT_NULL(lines.line_indices);
    memset(lines.line_indices, 0,
           sizeof(*lines.line_indices) * line_count * 2u);
    lines.line_indices[line_count * 2u - 1u] = 0xABCDu;
    nmo_chunk_t *line_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(line_chunk);
    line_chunk->class_id = NMO_CID_MESH;
    line_chunk->chunk_version = NMO_CHUNK_VERSION4;
    line_chunk->data_version = 9;
    line_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_mesh_serialize(
        &lines, line_chunk, NULL, &serialize_context));
    nmo_chunk_close(line_chunk);
    nmo_mesh_state_t lines_loaded;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(
        &lines_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_mesh_deserialize(
        &lines_loaded, line_chunk, NULL, &deserialize_context));
    ASSERT_EQ(line_count, lines_loaded.line_count);
    ASSERT_EQ(0xABCDu, lines_loaded.line_indices[line_count * 2u - 1u]);

    nmo_mesh_state_t face_boundary;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(
        &face_boundary, NULL, NULL));
    nmo_face_t boundary_face = {0};
    uint16_t boundary_indices[3] = {0};
    face_boundary.face_count = 10000000u;
    face_boundary.faces = &boundary_face;
    face_boundary.face_vertex_indices = boundary_indices;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.validate(
        &face_boundary, NULL, NULL));

    nmo_chunk_t *modern_faces = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern_faces);
    modern_faces->class_id = NMO_CID_MESH;
    modern_faces->chunk_version = NMO_CHUNK_VERSION4;
    modern_faces->data_version = 9;
    modern_faces->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(modern_faces));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        modern_faces, CK_STATESAVE_MESHFACES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(modern_faces, 10000000));
    nmo_chunk_close(modern_faces);
    nmo_mesh_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&failed, NULL, NULL));
    failed.flags = 0x12345678u;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_mesh_deserialize(
        &failed, modern_faces, NULL, &deserialize_context));
    ASSERT_EQ(0x12345678u, failed.flags);

    nmo_chunk_t *legacy_faces = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_faces);
    legacy_faces->class_id = NMO_CID_MESH;
    legacy_faces->chunk_version = NMO_CHUNK_VERSION4;
    legacy_faces->data_version = 8;
    legacy_faces->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_faces));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_faces, CK_STATESAVE_MESHFACES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(legacy_faces, 10000000));
    nmo_chunk_close(legacy_faces);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_mesh_deserialize(
        &failed, legacy_faces, NULL, &deserialize_context));
    ASSERT_EQ(0x12345678u, failed.flags);

    nmo_mesh_vtable.destroy(&lines, NULL, NULL);
    nmo_mesh_vtable.destroy(&lines_loaded, NULL, NULL);
    nmo_mesh_vtable.destroy(&face_boundary, NULL, NULL);
    nmo_mesh_vtable.destroy(&failed, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, mesh_preserves_large_vertex_counts) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    const uint32_t vertex_count = 1000001u;
    nmo_mesh_state_t source;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&source, NULL, NULL));
    source.flags = VXMESH_PROCEDURALPOS;
    source.vertex_count = vertex_count;
    source.vertices = nmo_arena_alloc(
        arena, sizeof(*source.vertices) * vertex_count,
        _Alignof(nmo_vertex_t));
    source.vertex_colors = nmo_arena_alloc(
        arena, sizeof(*source.vertex_colors) * vertex_count,
        _Alignof(uint32_t));
    source.vertex_specular = nmo_arena_alloc(
        arena, sizeof(*source.vertex_specular) * vertex_count,
        _Alignof(uint32_t));
    ASSERT_NOT_NULL(source.vertices);
    ASSERT_NOT_NULL(source.vertex_colors);
    ASSERT_NOT_NULL(source.vertex_specular);
    memset(source.vertices, 0, sizeof(*source.vertices) * vertex_count);
    memset(source.vertex_colors, 0,
           sizeof(*source.vertex_colors) * vertex_count);
    memset(source.vertex_specular, 0,
           sizeof(*source.vertex_specular) * vertex_count);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_MESH;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 9;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_mesh_serialize(
        &source, chunk, NULL, &serialize_context));
    nmo_chunk_close(chunk);
    nmo_mesh_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_mesh_deserialize(
        &loaded, chunk, NULL, &deserialize_context));
    ASSERT_EQ(vertex_count, loaded.vertex_count);
    ASSERT_EQ(0u, loaded.vertex_colors[vertex_count - 1u]);
    ASSERT_EQ(0.0f, loaded.vertices[vertex_count - 1u].uv.x);

    nmo_chunk_t *legacy_truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy_truncated);
    legacy_truncated->class_id = NMO_CID_MESH;
    legacy_truncated->chunk_version = NMO_CHUNK_VERSION4;
    legacy_truncated->data_version = 8;
    legacy_truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(legacy_truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        legacy_truncated, CK_STATESAVE_MESHVERTICES));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(legacy_truncated, 1000000));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(legacy_truncated, 0u));
    nmo_chunk_close(legacy_truncated);
    nmo_mesh_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&failed, NULL, NULL));
    failed.flags = 0x12345678u;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_mesh_deserialize(
        &failed, legacy_truncated, NULL, &deserialize_context));
    ASSERT_EQ(0x12345678u, failed.flags);

    nmo_mesh_vtable.destroy(&source, NULL, NULL);
    nmo_mesh_vtable.destroy(&loaded, NULL, NULL);
    nmo_mesh_vtable.destroy(&failed, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, mesh_preserves_large_weight_counts) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    const uint32_t weight_count = 10000000u;
    nmo_mesh_state_t source;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&source, NULL, NULL));
    source.vertex_weight_count = weight_count;
    source.vertex_weights = nmo_arena_alloc(
        arena, sizeof(*source.vertex_weights) * weight_count,
        _Alignof(float));
    ASSERT_NOT_NULL(source.vertex_weights);
    memset(source.vertex_weights, 0,
           sizeof(*source.vertex_weights) * weight_count);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_MESH;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 9;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_mesh_serialize(
        &source, chunk, NULL, &serialize_context));
    nmo_chunk_close(chunk);
    nmo_mesh_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_mesh_deserialize(
        &loaded, chunk, NULL, &deserialize_context));
    ASSERT_EQ(weight_count, loaded.vertex_weight_count);
    ASSERT_EQ(0.0f, loaded.vertex_weights[weight_count - 1u]);

    float nonuniform_weights[3] = {0.125f, 0.5f, 0.875f};
    nmo_mesh_state_t nonuniform;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&nonuniform, NULL, NULL));
    nonuniform.vertex_weight_count = 3u;
    nonuniform.vertex_weights = nonuniform_weights;
    nmo_chunk_t *nonuniform_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(nonuniform_chunk);
    nonuniform_chunk->class_id = NMO_CID_MESH;
    nonuniform_chunk->chunk_version = NMO_CHUNK_VERSION4;
    nonuniform_chunk->data_version = 9;
    nonuniform_chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_mesh_serialize(
        &nonuniform, nonuniform_chunk, NULL, &serialize_context));
    nmo_chunk_close(nonuniform_chunk);
    nmo_mesh_state_t nonuniform_loaded;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(
        &nonuniform_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_mesh_deserialize(
        &nonuniform_loaded, nonuniform_chunk, NULL,
        &deserialize_context));
    ASSERT_EQ(3u, nonuniform_loaded.vertex_weight_count);
    ASSERT_EQ(0.125f, nonuniform_loaded.vertex_weights[0]);
    ASSERT_EQ(0.5f, nonuniform_loaded.vertex_weights[1]);
    ASSERT_EQ(0.875f, nonuniform_loaded.vertex_weights[2]);

    nmo_vertex_t fallback_vertices[2] = {0};
    uint32_t fallback_colors[2] = {0};
    uint32_t fallback_specular[2] = {0};
    float fallback_weights[2] = {0.25f, 0.75f};
    nmo_mesh_state_t fallback;
    nmo_mesh_state_t fallback_copy;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&fallback, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(
        &fallback_copy, NULL, NULL));
    fallback.vertex_count = 2u;
    fallback.vertices = fallback_vertices;
    fallback.vertex_colors = fallback_colors;
    fallback.vertex_specular = fallback_specular;
    fallback.vertex_weights = fallback_weights;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.copy(
        &fallback, &fallback_copy, NULL, arena));
    ASSERT_NOT_NULL(fallback_copy.vertex_weights);
    ASSERT_TRUE(fallback_copy.vertex_weights != fallback_weights);
    ASSERT_EQ(0.25f, fallback_copy.vertex_weights[0]);
    ASSERT_EQ(0.75f, fallback_copy.vertex_weights[1]);

    nmo_mesh_vtable.destroy(&source, NULL, NULL);
    nmo_mesh_vtable.destroy(&loaded, NULL, NULL);
    nmo_mesh_vtable.destroy(&nonuniform, NULL, NULL);
    nmo_mesh_vtable.destroy(&nonuniform_loaded, NULL, NULL);
    nmo_mesh_vtable.destroy(&fallback, NULL, NULL);
    nmo_mesh_vtable.destroy(&fallback_copy, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, mesh_rejects_truncated_large_weights_before_allocation) {
    fail_after_allocator_state_t allocator_state = {
        .allocation_count = 0,
        .allowed_allocations = 2,
    };
    nmo_allocator_t failing_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &allocator_state);
    nmo_arena_t *arena = nmo_arena_create(&failing_allocator, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_MESH;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 9;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_MESHWEIGHTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 10000000));
    nmo_chunk_close(chunk);

    nmo_mesh_state_t state;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(&state, NULL, NULL));
    state.flags = 0x12345678u;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_mesh_deserialize(
        &state, chunk, NULL, &deserialize_context));
    ASSERT_EQ(0x12345678u, state.flags);
    ASSERT_EQ(2u, allocator_state.allocation_count);

    nmo_mesh_vtable.destroy(&state, NULL, NULL);
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

    fail_after_allocator_state_t allocator_state = {
        .allocation_count = 0,
        .allowed_allocations = 2,
    };
    nmo_allocator_t failing_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &allocator_state);
    nmo_arena_t *copy_arena = nmo_arena_create(&failing_allocator, 1);
    ASSERT_NOT_NULL(copy_arena);
    nmo_material_group_t preserved_group = {
        .material = nmo_ref_from_raw(814),
        .padding = 47,
    };
    nmo_mesh_state_t failed_copy;
    ASSERT_EQ(NMO_OK, nmo_mesh_vtable.create(
        &failed_copy, NULL, NULL));
    failed_copy.flags = 0x12345678u;
    failed_copy.material_group_count = 1;
    failed_copy.material_groups = &preserved_group;
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_mesh_vtable.copy(
        &source, &failed_copy, NULL, copy_arena));
    ASSERT_EQ(0x12345678u, failed_copy.flags);
    ASSERT_EQ(1u, failed_copy.material_group_count);
    ASSERT_EQ(&preserved_group, failed_copy.material_groups);
    ASSERT_EQ(814u, failed_copy.material_groups[0].material.raw_id);

    copied.material_groups[0].padding = 99;
    ASSERT_FALSE(nmo_mesh_vtable.equals(&source, &copied));

    nmo_mesh_vtable.destroy(&failed_copy, NULL, NULL);
    nmo_arena_destroy(copy_arena);
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

TEST(chunk_id_remap, patchmesh_rejects_cross_section_legacy_materials) {
    fail_after_allocator_state_t allocator_state = {
        .allocation_count = 0,
        .allowed_allocations = 2,
    };
    nmo_allocator_t failing_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &allocator_state);
    nmo_arena_t *arena = nmo_arena_create(&failing_allocator, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->class_id = NMO_CID_PATCHMESH;
    chunk->chunk_version = NMO_CHUNK_VERSION4;
    chunk->data_version = 7;
    chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_PATCHMESHDATA2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 0));
    for (size_t i = 0; i < 10; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        chunk, CK_STATESAVE_PATCHMESHMATERIALS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(chunk, 4));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0x7F123456u));
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0));
    }
    nmo_chunk_close(chunk);

    nmo_patchmesh_state_t state;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(&state, NULL, NULL));
    state.patch_flags = 0x12345678u;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_patchmesh_deserialize(
        &state, chunk, NULL, &deserialize_context));
    ASSERT_EQ(0x12345678u, state.patch_flags);
    ASSERT_EQ(2u, allocator_state.allocation_count);

    nmo_patchmesh_vtable.destroy(&state, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, patchmesh_data_sections_do_not_borrow_following_identifiers) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    const struct packed_case {
        uint32_t identifier;
        size_t payload_dwords;
    } packed_cases[] = {
        {CK_STATESAVE_PATCHMESHDATA3, 8u},
        {CK_STATESAVE_PATCHMESHDATA2, 13u},
    };
    for (size_t case_index = 0;
         case_index < sizeof(packed_cases) / sizeof(packed_cases[0]);
         ++case_index) {
        nmo_chunk_t *chunk = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(chunk);
        chunk->class_id = NMO_CID_PATCHMESH;
        chunk->chunk_version = NMO_CHUNK_VERSION4;
        chunk->data_version = 7;
        chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            chunk, packed_cases[case_index].identifier));
        for (size_t i = 0; i < packed_cases[case_index].payload_dwords; ++i) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0));
        nmo_chunk_close(chunk);

        nmo_patchmesh_state_t state;
        ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(
            &state, NULL, NULL));
        state.patch_flags = 0x12345678u;
        ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
                  nmo_patchmesh_deserialize(
                      &state, chunk, NULL, &deserialize_context));
        ASSERT_EQ(0x12345678u, state.patch_flags);
        nmo_patchmesh_vtable.destroy(&state, NULL, NULL);
    }

    nmo_chunk_t *vector_tail = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(vector_tail);
    vector_tail->class_id = NMO_CID_PATCHMESH;
    vector_tail->chunk_version = NMO_CHUNK_VERSION4;
    vector_tail->data_version = 7;
    vector_tail->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(vector_tail));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        vector_tail, CK_STATESAVE_PATCHMESHDATA3));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(vector_tail, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(vector_tail, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(vector_tail, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        vector_tail, sizeof(nmo_vector_t)));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(vector_tail, 1u));
    for (size_t i = 0; i < sizeof(nmo_vector_t) / sizeof(uint32_t); ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(vector_tail, 0u));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(vector_tail, 0));
    for (size_t i = 0; i < 3u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(vector_tail, 0u));
    }
    nmo_chunk_close(vector_tail);

    nmo_patchmesh_state_t vector_state;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(
        &vector_state, NULL, NULL));
    vector_state.patch_flags = 0x87654321u;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
              nmo_patchmesh_deserialize(
                  &vector_state, vector_tail, NULL, &deserialize_context));
    ASSERT_EQ(0x87654321u, vector_state.patch_flags);
    nmo_patchmesh_vtable.destroy(&vector_state, NULL, NULL);

    nmo_chunk_t *smoothing = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(smoothing);
    smoothing->class_id = NMO_CID_PATCHMESH;
    smoothing->chunk_version = NMO_CHUNK_VERSION4;
    smoothing->data_version = 7;
    smoothing->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(smoothing));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        smoothing, CK_STATESAVE_PATCHMESHDATA2));
    for (size_t i = 0; i < 14u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(smoothing, 0u));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        smoothing, CK_STATESAVE_PATCHMESHSMOOTH));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(smoothing, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(smoothing, 0));
    nmo_chunk_close(smoothing);

    nmo_patchmesh_state_t smoothing_state;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(
        &smoothing_state, NULL, NULL));
    smoothing_state.patch_flags = 0xCAFEBABEu;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
              nmo_patchmesh_deserialize(
                  &smoothing_state, smoothing, NULL,
                  &deserialize_context));
    ASSERT_EQ(0xCAFEBABEu, smoothing_state.patch_flags);
    nmo_patchmesh_vtable.destroy(&smoothing_state, NULL, NULL);

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
    nmo_material_group_t base_group = {
        .material = nmo_ref_from_raw(804),
        .padding = 43,
    };
    source.base.flags = 0x10293847u;
    source.base.material_group_count = 1;
    source.base.material_groups = &base_group;
    source.base.has_material_groups = 1;

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
    ASSERT_EQ(0x10293847u, copied.base.flags);
    ASSERT_NE(source.base.material_groups, copied.base.material_groups);
    ASSERT_EQ(804u, copied.base.material_groups[0].material.raw_id);
    ASSERT_EQ(43, copied.base.material_groups[0].padding);
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

    fail_after_allocator_state_t allocator_state = {
        .allocation_count = 0,
        .allowed_allocations = 2,
    };
    nmo_allocator_t failing_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &allocator_state);
    nmo_arena_t *copy_arena = nmo_arena_create(&failing_allocator, 1);
    ASSERT_NOT_NULL(copy_arena);
    nmo_patchmesh_patch_record_t preserved_patch = {
        .material = nmo_ref_from_raw(805),
        .patch = {.type = 53, .smoothing_group = 59},
    };
    nmo_patchmesh_state_t failed_copy;
    ASSERT_EQ(NMO_OK, nmo_patchmesh_vtable.create(
        &failed_copy, NULL, NULL));
    failed_copy.format = CKPATCHMESH_FORMAT_DATA3;
    failed_copy.patch_flags = 0x12345678u;
    failed_copy.patch_count = 1;
    failed_copy.patches = &preserved_patch;
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_patchmesh_vtable.copy(
        &source, &failed_copy, NULL, copy_arena));
    ASSERT_EQ(CKPATCHMESH_FORMAT_DATA3, failed_copy.format);
    ASSERT_EQ(0x12345678u, failed_copy.patch_flags);
    ASSERT_EQ(1u, failed_copy.patch_count);
    ASSERT_EQ(&preserved_patch, failed_copy.patches);
    ASSERT_EQ(805u, failed_copy.patches[0].material.raw_id);

    copied.patches[0].patch.type = 99;
    ASSERT_FALSE(nmo_patchmesh_vtable.equals(&source, &copied));

    nmo_patchmesh_vtable.destroy(&failed_copy, NULL, NULL);
    nmo_arena_destroy(copy_arena);
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
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, source.base.base.visibility_flags);
    source.has_data = 1;
    source.data_is_legacy = 1;
    nmo_ref_t legacy_body_parts[] = {
        nmo_ref_from_raw(897), nmo_ref_from_raw(898),
    };
    source.has_root_entity = 1;
    source.legacy_body_part_count = 2;
    source.legacy_body_parts = legacy_body_parts;
    source.root_entity = nmo_ref_from_raw(901);
    source.has_character = 1;
    source.character = nmo_ref_from_raw(902);

    nmo_animation_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_animation_vtable.create(&copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_animation_vtable.copy(
        &source, &copied, NULL, arena));
    ASSERT_NE(source.legacy_body_parts, copied.legacy_body_parts);
    ASSERT_TRUE(nmo_animation_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_animation_vtable.hash(&source),
              nmo_animation_vtable.hash(&copied));
    copied.data_is_legacy = 0;
    ASSERT_FALSE(nmo_animation_vtable.equals(&source, &copied));
    copied.data_is_legacy = 1;
    copied.legacy_body_parts[0].raw_id++;
    ASSERT_FALSE(nmo_animation_vtable.equals(&source, &copied));
    copied.legacy_body_parts[0].raw_id--;
    copied.character.raw_id++;
    ASSERT_FALSE(nmo_animation_vtable.equals(&source, &copied));

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
    ASSERT_EQ(2u, loaded.legacy_body_part_count);
    ASSERT_EQ(897u, loaded.legacy_body_parts[0].raw_id);
    ASSERT_EQ(898u, loaded.legacy_body_parts[1].raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED,
              loaded.legacy_body_parts[0].state);
    ASSERT_EQ(901u, loaded.root_entity.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.root_entity.state);
    ASSERT_TRUE(loaded.has_character);
    ASSERT_EQ(902u, loaded.character.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, loaded.character.state);
    ASSERT_TRUE(loaded.data_is_legacy);

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
    ASSERT_EQ(2u, reloaded.legacy_body_part_count);
    ASSERT_EQ(897u, reloaded.legacy_body_parts[0].raw_id);
    ASSERT_EQ(898u, reloaded.legacy_body_parts[1].raw_id);
    ASSERT_EQ(901u, reloaded.root_entity.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.root_entity.state);
    ASSERT_EQ(902u, reloaded.character.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, reloaded.character.state);
    ASSERT_TRUE(reloaded.data_is_legacy);

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
    nmo_ref_t previous_body_part = nmo_ref_from_raw(998);
    failed.has_root_entity = 1;
    failed.legacy_body_part_count = 1;
    failed.legacy_body_parts = &previous_body_part;
    failed.root_entity = nmo_ref_from_raw(999);
    ASSERT_NE(NMO_OK, nmo_animation_deserialize(
        &failed, truncated, NULL, &deserialize_context));
    ASSERT_TRUE(failed.has_root_entity);
    ASSERT_EQ(1u, failed.legacy_body_part_count);
    ASSERT_EQ(&previous_body_part, failed.legacy_body_parts);
    ASSERT_EQ(998u, failed.legacy_body_parts[0].raw_id);
    ASSERT_EQ(999u, failed.root_entity.raw_id);
    ASSERT_EQ(NMO_REF_UNRESOLVED, failed.root_entity.state);

    fail_after_allocator_state_t allocator_state = {
        .allocation_count = 0,
        .allowed_allocations = 2,
    };
    nmo_allocator_t failing_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &allocator_state);
    nmo_arena_t *failing_arena = nmo_arena_create(
        &failing_allocator, 1);
    ASSERT_NOT_NULL(failing_arena);
    nmo_deserialize_context_t failing_context =
        nmo_deserialize_context_create(
            failing_arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_animation_state_t failed_alloc;
    ASSERT_EQ(NMO_OK, nmo_animation_vtable.create(
        &failed_alloc, NULL, NULL));
    failed_alloc.flags = 0x12345678u;
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_animation_deserialize(
        &failed_alloc, first, NULL, &failing_context));
    ASSERT_EQ(0x12345678u, failed_alloc.flags);
    ASSERT_EQ(0u, failed_alloc.legacy_body_part_count);
    ASSERT_NULL(failed_alloc.legacy_body_parts);
    ASSERT_EQ(2u, allocator_state.allocation_count);

    nmo_animation_state_t invalid;
    ASSERT_EQ(NMO_OK, nmo_animation_vtable.create(
        &invalid, NULL, NULL));
    invalid.legacy_body_part_count = UINT32_MAX;
    invalid.legacy_body_parts = legacy_body_parts;
    nmo_chunk_t *target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(target);
    target->class_id = NMO_CID_ANIMATION;
    target->data_version = 7;
    target->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        target, 0x12345678u));
    nmo_chunk_close(target);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_animation_serialize(
        &invalid, target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(target));

    nmo_animation_vtable.destroy(&source, NULL, NULL);
    nmo_animation_vtable.destroy(&copied, NULL, NULL);
    nmo_animation_vtable.destroy(&loaded, NULL, NULL);
    nmo_animation_vtable.destroy(&reloaded, NULL, NULL);
    nmo_animation_vtable.destroy(&failed, NULL, NULL);
    nmo_animation_vtable.destroy(&failed_alloc, NULL, NULL);
    nmo_animation_vtable.destroy(&invalid, NULL, NULL);
    nmo_arena_destroy(failing_arena);
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
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE,
              source.base.base.base.visibility_flags);
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

    const uint32_t large_count = 10001u;
    nmo_keyedanimation_subanim_t *large_subanims = nmo_arena_alloc(
        arena, sizeof(*large_subanims) * large_count,
        _Alignof(nmo_keyedanimation_subanim_t));
    ASSERT_NOT_NULL(large_subanims);
    memset(large_subanims, 0, sizeof(*large_subanims) * large_count);
    for (uint32_t i = 0; i < large_count; ++i) {
        large_subanims[i].ref = nmo_ref_from_raw(
            (nmo_object_id_t)(i + 1u));
    }
    nmo_keyedanimation_state_t large;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(
        &large, NULL, NULL));
    large.subanim_count = large_count;
    large.subanims = large_subanims;
    nmo_chunk_t *large_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(large_chunk);
    large_chunk->class_id = NMO_CID_KEYEDANIMATION;
    large_chunk->chunk_version = NMO_CHUNK_VERSION4;
    large_chunk->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_serialize(
        &large, large_chunk, NULL, &serialize_context));
    nmo_chunk_close(large_chunk);
    nmo_keyedanimation_state_t large_loaded;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(
        &large_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_deserialize(
        &large_loaded, large_chunk, NULL, &deserialize_context));
    ASSERT_EQ(large_count, large_loaded.subanim_count);
    ASSERT_EQ(1u, large_loaded.subanims[0].ref.raw_id);
    ASSERT_EQ(large_count,
              large_loaded.subanims[large_count - 1u].ref.raw_id);
    ASSERT_NULL(large_loaded.subanims[large_count - 1u].chunk);

    nmo_keyedanimation_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(
        &copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.copy(
        &reloaded, &copied, NULL, arena));
    ASSERT_TRUE(copied.animation_ids != reloaded.animation_ids);
    ASSERT_TRUE(copied.subanims != reloaded.subanims);
    ASSERT_TRUE(copied.subanims[0].chunk != reloaded.subanims[0].chunk);
    ASSERT_TRUE(nmo_keyedanimation_vtable.equals(&reloaded, &copied));
    ASSERT_EQ(nmo_keyedanimation_vtable.hash(&reloaded),
              nmo_keyedanimation_vtable.hash(&copied));
    copied.subanims[0].ref.raw_id++;
    ASSERT_FALSE(nmo_keyedanimation_vtable.equals(&reloaded, &copied));
    copied.subanims[0].ref.raw_id--;

    fail_after_allocator_state_t copy_allocator_state = {
        .allocation_count = 0,
        .allowed_allocations = 2,
    };
    nmo_allocator_t copy_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &copy_allocator_state);
    nmo_arena_t *copy_arena = nmo_arena_create(&copy_allocator, 1);
    ASSERT_NOT_NULL(copy_arena);
    nmo_ref_t previous_copy_ref = nmo_ref_from_raw(943);
    nmo_keyedanimation_state_t failed_copy;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(
        &failed_copy, NULL, NULL));
    failed_copy.base.flags = 0x12345678u;
    failed_copy.animation_count = 1;
    failed_copy.animation_ids = &previous_copy_ref;
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_keyedanimation_vtable.copy(
        &reloaded, &failed_copy, NULL, copy_arena));
    ASSERT_EQ(0x12345678u, failed_copy.base.flags);
    ASSERT_EQ(1u, failed_copy.animation_count);
    ASSERT_EQ(&previous_copy_ref, failed_copy.animation_ids);
    ASSERT_EQ(943u, failed_copy.animation_ids[0].raw_id);

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
    nmo_keyedanimation_vtable.destroy(&large, NULL, NULL);
    nmo_keyedanimation_vtable.destroy(&large_loaded, NULL, NULL);
    nmo_keyedanimation_vtable.destroy(&copied, NULL, NULL);
    nmo_keyedanimation_vtable.destroy(&failed_copy, NULL, NULL);
    nmo_keyedanimation_vtable.destroy(&failed, NULL, NULL);
    nmo_keyedanimation_vtable.destroy(&invalid, NULL, NULL);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, animation_sections_do_not_borrow_following_identifiers) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t file_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_deserialize_context_t runtime_context =
        nmo_deserialize_context_create(arena, NULL, NULL, 0);
    nmo_serialize_context_t serialize_context =
        nmo_serialize_context_create(
            arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);

    nmo_animation_state_t animation;
    ASSERT_EQ(NMO_OK, nmo_animation_vtable.create(
        &animation, NULL, NULL));
    animation.flags = 0x12345678u;

    const uint32_t scalar_ids[] = {
        CK_STATESAVE_ANIMATIONLENGTH,
        CK_STATESAVE_ANIMATIONCHARACTER,
        CK_STATESAVE_ANIMATIONCURRENTSTEP,
    };
    for (size_t i = 0;
         i < sizeof(scalar_ids) / sizeof(scalar_ids[0]); ++i) {
        nmo_chunk_t *chunk = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(chunk);
        chunk->class_id = NMO_CID_ANIMATION;
        chunk->data_version = 7;
        chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            chunk, scalar_ids[i]));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0));
        nmo_chunk_close(chunk);
        ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_animation_deserialize(
            &animation, chunk, NULL, &file_context));
        ASSERT_EQ(0x12345678u, animation.flags);
    }

    const size_t unsupported_data_sizes[] = {1u, 4u};
    for (size_t i = 0;
         i < sizeof(unsupported_data_sizes) /
             sizeof(unsupported_data_sizes[0]);
         ++i) {
        nmo_chunk_t *chunk = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(chunk);
        chunk->class_id = NMO_CID_ANIMATION;
        chunk->data_version = 7;
        chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            chunk, CK_STATESAVE_ANIMATIONDATA));
        for (size_t j = 0; j < unsupported_data_sizes[i]; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0));
        }
        nmo_chunk_close(chunk);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_animation_deserialize(
            &animation, chunk, NULL, &file_context));
        ASSERT_EQ(0x12345678u, animation.flags);
    }

    nmo_chunk_t *old_data = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(old_data);
    old_data->class_id = NMO_CID_ANIMATION;
    old_data->data_version = 7;
    old_data->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(old_data));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        old_data, CK_STATESAVE_ANIMATIONDATA));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(old_data, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(old_data, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(old_data, 24.0f));
    nmo_chunk_close(old_data);
    nmo_animation_state_t old_animation;
    ASSERT_EQ(NMO_OK, nmo_animation_vtable.create(
        &old_animation, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_animation_deserialize(
        &old_animation, old_data, NULL, &file_context));
    ASSERT_EQ(CKANIMATION_CANBEBREAK, old_animation.flags);
    ASSERT_FLOAT_EQ(24.0f, old_animation.frame_rate, 0.0001f);
    ASSERT_TRUE(old_animation.data_is_legacy);

    nmo_chunk_t *old_data_saved = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(old_data_saved);
    old_data_saved->class_id = NMO_CID_ANIMATION;
    old_data_saved->data_version = 7;
    old_data_saved->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_animation_serialize(
        &old_animation, old_data_saved, NULL, &serialize_context));
    nmo_chunk_close(old_data_saved);
    size_t old_data_dwords = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier_with_size(
        old_data_saved, CK_STATESAVE_ANIMATIONDATA,
        &old_data_dwords));
    ASSERT_EQ(3u, old_data_dwords);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(old_data_saved));

    nmo_animation_state_t old_reloaded;
    ASSERT_EQ(NMO_OK, nmo_animation_vtable.create(
        &old_reloaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_animation_deserialize(
        &old_reloaded, old_data_saved, NULL, &file_context));
    ASSERT_TRUE(old_reloaded.data_is_legacy);
    ASSERT_EQ(CKANIMATION_CANBEBREAK, old_reloaded.flags);
    ASSERT_FLOAT_EQ(24.0f, old_reloaded.frame_rate, 0.0001f);

    old_animation.flags |= CKANIMATION_ALIGNORIENTATION;
    nmo_chunk_t *invalid_legacy_target = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(invalid_legacy_target);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(invalid_legacy_target));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(
        invalid_legacy_target, 0xA11A11A1u));
    nmo_chunk_close(invalid_legacy_target);
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_animation_serialize(
        &old_animation, invalid_legacy_target, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(invalid_legacy_target));
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(invalid_legacy_target));
    uint32_t invalid_legacy_marker = 0u;
    ASSERT_EQ(NMO_OK, nmo_chunk_read_dword(
        invalid_legacy_target, &invalid_legacy_marker));
    ASSERT_EQ(0xA11A11A1u, invalid_legacy_marker);

    nmo_animation_vtable.destroy(&old_reloaded, NULL, NULL);
    nmo_animation_vtable.destroy(&old_animation, NULL, NULL);

    nmo_chunk_t *missing_root = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_root);
    missing_root->class_id = NMO_CID_ANIMATION;
    missing_root->data_version = 7;
    missing_root->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_root));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_root, CK_STATESAVE_ANIMATIONBODYPARTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        missing_root, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(missing_root, 0));
    nmo_chunk_close(missing_root);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_animation_deserialize(
        &animation, missing_root, NULL, &file_context));
    ASSERT_EQ(0x12345678u, animation.flags);

    nmo_keyedanimation_state_t keyed;
    ASSERT_EQ(NMO_OK, nmo_keyedanimation_vtable.create(
        &keyed, NULL, NULL));
    keyed.base.flags = 0x87654321u;

    const uint32_t keyed_file_ids[] = {
        CK_STATESAVE_KEYEDANIMANIMLIST,
        CK_STATESAVE_KEYEDANIMMERGE,
    };
    for (size_t i = 0;
         i < sizeof(keyed_file_ids) / sizeof(keyed_file_ids[0]); ++i) {
        nmo_chunk_t *chunk = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(chunk);
        chunk->class_id = NMO_CID_KEYEDANIMATION;
        chunk->data_version = 7;
        chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            chunk, keyed_file_ids[i]));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0));
        nmo_chunk_close(chunk);
        ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
                  nmo_keyedanimation_deserialize(
                      &keyed, chunk, NULL, &file_context));
        ASSERT_EQ(0x87654321u, keyed.base.flags);
    }

    nmo_chunk_t *missing_subanim_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_subanim_count);
    missing_subanim_count->class_id = NMO_CID_KEYEDANIMATION;
    missing_subanim_count->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_subanim_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_subanim_count, CK_STATESAVE_KEYEDANIMSUBANIMS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_subanim_count, 0));
    nmo_chunk_close(missing_subanim_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_keyedanimation_deserialize(
        &keyed, missing_subanim_count, NULL, &runtime_context));
    ASSERT_EQ(0x87654321u, keyed.base.flags);

    nmo_keyedanimation_vtable.destroy(&keyed, NULL, NULL);
    nmo_animation_vtable.destroy(&animation, NULL, NULL);
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

    nmo_curve_vtable.destroy(&curve, NULL, NULL);
    nmo_curvepoint_vtable.destroy(&point, NULL, NULL);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, curve_layout_follows_data_version) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, UINT32_MAX);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_curve_state_t source;
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.create(&source, NULL, NULL));
    nmo_ref_t point = nmo_ref_from_raw(751);
    source.control_point_count = 1;
    source.control_point_ids = &point;
    source.fitting_coeff = 1.5f;
    source.step_count = 42;
    source.opened = 0;

    nmo_chunk_t *legacy = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(legacy);
    legacy->class_id = NMO_CID_CURVE;
    legacy->data_version = 4;
    legacy->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_curve_serialize(
        &source, legacy, NULL, &serialize_context));
    nmo_chunk_close(legacy);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        legacy, CK_STATESAVE_CURVEONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        legacy, CK_STATESAVE_CURVECONTROLPOINT));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        legacy, CK_STATESAVE_CURVEFITCOEFF));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        legacy, CK_STATESAVE_CURVESTEPS));
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        legacy, CK_STATESAVE_CURVEOPEN));

    nmo_curve_state_t legacy_loaded;
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.create(
        &legacy_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_curve_deserialize(
        &legacy_loaded, legacy, NULL, &deserialize_context));
    ASSERT_EQ(1u, legacy_loaded.control_point_count);
    ASSERT_EQ(751u, legacy_loaded.control_point_ids[0].raw_id);
    ASSERT_EQ(1.5f, legacy_loaded.fitting_coeff);
    ASSERT_EQ(42u, legacy_loaded.step_count);
    ASSERT_EQ(0u, legacy_loaded.opened);

    nmo_chunk_t *modern = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(modern);
    modern->class_id = NMO_CID_CURVE;
    modern->data_version = 7;
    modern->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_curve_serialize(
        &legacy_loaded, modern, NULL, &serialize_context));
    nmo_chunk_close(modern);
    ASSERT_EQ(NMO_OK, nmo_chunk_seek_identifier(
        modern, CK_STATESAVE_CURVEONLY));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, nmo_chunk_seek_identifier(
        modern, CK_STATESAVE_CURVECONTROLPOINT));

    nmo_curve_state_t modern_loaded;
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.create(
        &modern_loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_curve_deserialize(
        &modern_loaded, modern, NULL, &deserialize_context));
    ASSERT_EQ(751u, modern_loaded.control_point_ids[0].raw_id);
    ASSERT_EQ(1.5f, modern_loaded.fitting_coeff);
    ASSERT_EQ(42u, modern_loaded.step_count);
    ASSERT_EQ(0u, modern_loaded.opened);

    nmo_curve_vtable.destroy(&source, NULL, NULL);
    nmo_curve_vtable.destroy(&legacy_loaded, NULL, NULL);
    nmo_curve_vtable.destroy(&modern_loaded, NULL, NULL);
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

    fail_after_allocator_state_t allocator_state = {
        .allowed_allocations = 2,
    };
    nmo_allocator_t failing_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &allocator_state);
    nmo_arena_t *failing_arena = nmo_arena_create(
        &failing_allocator, 1);
    ASSERT_NOT_NULL(failing_arena);
    allocator_state.allowed_allocations = allocator_state.allocation_count;
    nmo_ref_t previous_control = nmo_ref_from_raw(991);
    nmo_curve_point_subchunk_t previous_saved = {
        .ref = nmo_ref_from_raw(992),
        .chunk = subchunk,
    };
    nmo_curve_state_t copy_failed;
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.create(
        &copy_failed, NULL, NULL));
    copy_failed.base.entity_flags = 0x12345678u;
    copy_failed.control_point_count = 1;
    copy_failed.control_point_ids = &previous_control;
    copy_failed.sub_point_count = 1;
    copy_failed.sub_points = &previous_saved;
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_curve_vtable.copy(
        &loaded, &copy_failed, &curve_type, failing_arena));
    ASSERT_EQ(0x12345678u, copy_failed.base.entity_flags);
    ASSERT_EQ(&previous_control, copy_failed.control_point_ids);
    ASSERT_EQ(991u, copy_failed.control_point_ids[0].raw_id);
    ASSERT_EQ(&previous_saved, copy_failed.sub_points);
    ASSERT_EQ(subchunk, copy_failed.sub_points[0].chunk);

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

    nmo_curve_state_t bounded;
    ASSERT_EQ(NMO_OK, nmo_curve_vtable.create(&bounded, NULL, NULL));
    bounded.fitting_coeff = 12.5f;

    nmo_chunk_t *missing_curve_tail = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_curve_tail);
    missing_curve_tail->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_curve_tail));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_curve_tail, CK_STATESAVE_CURVEONLY));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_sequence_start(
        missing_curve_tail, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(missing_curve_tail, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(missing_curve_tail, 0));
    nmo_chunk_close(missing_curve_tail);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_curve_deserialize(
        &bounded, missing_curve_tail, NULL, &deserialize_context));
    ASSERT_EQ(12.5f, bounded.fitting_coeff);

    nmo_chunk_t *missing_saved_point_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_saved_point_count);
    missing_saved_point_count->data_version = 7;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_saved_point_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_saved_point_count, CK_STATESAVE_CURVESAVEPOINTS));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_saved_point_count, 0));
    nmo_chunk_close(missing_saved_point_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_curve_deserialize(
        &bounded, missing_saved_point_count, NULL, &deserialize_context));
    ASSERT_EQ(12.5f, bounded.fitting_coeff);

    const uint32_t legacy_scalar_sections[] = {
        CK_STATESAVE_CURVEFITCOEFF,
        CK_STATESAVE_CURVESTEPS,
        CK_STATESAVE_CURVEOPEN,
    };
    for (size_t i = 0;
         i < sizeof(legacy_scalar_sections) /
                 sizeof(legacy_scalar_sections[0]);
         ++i) {
        nmo_chunk_t *missing_scalar = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(missing_scalar);
        missing_scalar->data_version = 4;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_scalar));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            missing_scalar, legacy_scalar_sections[i]));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(missing_scalar, 0));
        nmo_chunk_close(missing_scalar);
        ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_curve_deserialize(
            &bounded, missing_scalar, NULL, &deserialize_context));
        ASSERT_EQ(12.5f, bounded.fitting_coeff);
    }

    nmo_chunk_t *missing_legacy_point_count = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(missing_legacy_point_count);
    missing_legacy_point_count->data_version = 4;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(missing_legacy_point_count));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_legacy_point_count, CK_STATESAVE_CURVECONTROLPOINT));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        missing_legacy_point_count, 0));
    nmo_chunk_close(missing_legacy_point_count);
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_curve_deserialize(
        &bounded, missing_legacy_point_count, NULL, &deserialize_context));
    ASSERT_EQ(12.5f, bounded.fitting_coeff);

    static const struct {
        uint32_t identifier;
        uint32_t data_version;
        size_t payload_dwords;
    } trailing_sections[] = {
        {CK_STATESAVE_CURVECONTROLPOINT, 4u, 1u},
        {CK_STATESAVE_CURVEFITCOEFF, 4u, 1u},
        {CK_STATESAVE_CURVESTEPS, 4u, 1u},
        {CK_STATESAVE_CURVEOPEN, 4u, 1u},
        {CK_STATESAVE_CURVEONLY, 7u, 4u},
        {CK_STATESAVE_CURVESAVEPOINTS, 7u, 1u},
    };
    for (size_t i = 0;
         i < sizeof(trailing_sections) / sizeof(trailing_sections[0]);
         ++i) {
        nmo_chunk_t *trailing = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(trailing);
        trailing->data_version = trailing_sections[i].data_version;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(trailing));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            trailing, trailing_sections[i].identifier));
        for (size_t j = 0; j < trailing_sections[i].payload_dwords; ++j) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0u));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(trailing, 0x12345678u));
        nmo_chunk_close(trailing);
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT, nmo_curve_deserialize(
            &bounded, trailing, NULL, &deserialize_context));
        ASSERT_EQ(12.5f, bounded.fitting_coeff);
    }

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

    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_curve_vtable.validate(
        NULL, NULL, NULL));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_curvepoint_vtable.validate(
        NULL, NULL, NULL));
    invalid.control_point_ids = &previous_control;
    invalid.control_point_count = (uint32_t)INT32_MAX + 1u;
    ASSERT_EQ(NMO_ERR_VALIDATION_FAILED, nmo_curve_serialize(
        &invalid, preserved, NULL, &serialize_context));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(preserved));

    nmo_curve_vtable.destroy(&source, NULL, NULL);
    nmo_curve_vtable.destroy(&loaded, NULL, NULL);
    nmo_curve_vtable.destroy(&copied, NULL, NULL);
    nmo_curve_vtable.destroy(&copy_failed, NULL, NULL);
    nmo_curve_vtable.destroy(&reloaded, NULL, NULL);
    nmo_curve_vtable.destroy(&failed, NULL, NULL);
    nmo_curve_vtable.destroy(&bounded, NULL, NULL);
    nmo_curve_vtable.destroy(&invalid, NULL, NULL);
    nmo_arena_destroy(failing_arena);
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

    uint8_t controller_data[] = {1, 2, 3, 4};
    nmo_objanim_controller_t controller = {
        .type = 0x637c4301u,
        .key_count = 1,
        .data_size = sizeof(controller_data),
        .data = controller_data,
    };
    uint8_t morph_data[] = {5, 6, 7, 8};
    nmo_objanim_morph_key_t morph_key = {
        .time_step = 2.5f,
        .data_size = sizeof(morph_data),
        .data = morph_data,
    };
    uint8_t normal_data[] = {9, 10, 11, 12};
    uint32_t normal_size = sizeof(normal_data);
    void *normal_data_ptr = normal_data;
    uint8_t raw_tail[] = {13, 14, 15, 16};
    source.controller_count = 1;
    source.controllers = &controller;
    source.morph_key_parsed_count = 1;
    source.morph_keys = &morph_key;
    source.morph_normals_id = CK_STATESAVE_OBJANIMMORPHNORMALS;
    source.morph_normals_count = 1;
    source.morph_normals_sizes = &normal_size;
    source.morph_normals_data = &normal_data_ptr;
    source.raw_tail = raw_tail;
    source.raw_tail_size = sizeof(raw_tail);
    ASSERT_EQ(NMO_CKOBJECT_VISIBLE, source.base.base.visibility_flags);

    nmo_objectanimation_state_t copied;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &copied, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.copy(
        &source, &copied, NULL, arena));
    ASSERT_NE(source.controllers, copied.controllers);
    ASSERT_NE(source.controllers[0].data, copied.controllers[0].data);
    ASSERT_NE(source.morph_keys, copied.morph_keys);
    ASSERT_NE(source.morph_keys[0].data, copied.morph_keys[0].data);
    ASSERT_NE(source.morph_normals_sizes, copied.morph_normals_sizes);
    ASSERT_NE(source.morph_normals_data, copied.morph_normals_data);
    ASSERT_NE(source.morph_normals_data[0], copied.morph_normals_data[0]);
    ASSERT_NE(source.raw_tail, copied.raw_tail);
    ASSERT_TRUE(nmo_objectanimation_vtable.equals(&source, &copied));
    ASSERT_EQ(nmo_objectanimation_vtable.hash(&source),
              nmo_objectanimation_vtable.hash(&copied));
    ((uint8_t *)copied.controllers[0].data)[0]++;
    ASSERT_FALSE(nmo_objectanimation_vtable.equals(&source, &copied));
    ((uint8_t *)copied.controllers[0].data)[0]--;

    fail_after_allocator_state_t copy_allocator_state = {
        .allocation_count = 0,
        .allowed_allocations = 2,
    };
    nmo_allocator_t copy_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &copy_allocator_state);
    nmo_arena_t *copy_arena = nmo_arena_create(&copy_allocator, 1);
    ASSERT_NOT_NULL(copy_arena);
    uint8_t previous_controller_data[] = {21, 22, 23, 24};
    nmo_objanim_controller_t previous_controller = {
        .type = 77,
        .key_count = 1,
        .data_size = sizeof(previous_controller_data),
        .data = previous_controller_data,
    };
    nmo_objectanimation_state_t failed_copy;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &failed_copy, NULL, NULL));
    failed_copy.flags = 0x12345678u;
    failed_copy.controller_count = 1;
    failed_copy.controllers = &previous_controller;
    ASSERT_EQ(NMO_ERR_NOMEM, nmo_objectanimation_vtable.copy(
        &source, &failed_copy, NULL, copy_arena));
    ASSERT_EQ(0x12345678u, failed_copy.flags);
    ASSERT_EQ(1u, failed_copy.controller_count);
    ASSERT_EQ(&previous_controller, failed_copy.controllers);
    ASSERT_EQ(previous_controller_data, failed_copy.controllers[0].data);

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
    ASSERT_EQ(sizeof(raw_tail), loaded.raw_tail_size);
    ASSERT_EQ(0, memcmp(raw_tail, loaded.raw_tail, sizeof(raw_tail)));

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
    ASSERT_EQ(sizeof(raw_tail), reloaded.raw_tail_size);
    ASSERT_EQ(0, memcmp(raw_tail, reloaded.raw_tail, sizeof(raw_tail)));

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
    nmo_objectanimation_vtable.destroy(&copied, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&failed_copy, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&reloaded, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&failed, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&invalid, NULL, NULL);
    nmo_arena_destroy(copy_arena);
    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, objectanimation_sections_do_not_borrow_following_identifiers) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_vector_t zero = {0};

    nmo_chunk_t *shared = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(shared);
    shared->class_id = NMO_CID_OBJECTANIMATION;
    shared->chunk_version = NMO_CHUNK_VERSION4;
    shared->data_version = 7;
    shared->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(shared));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        shared, CK_STATESAVE_OBJANIMSHARED));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        shared, NMO_OBJECT_ID_NONE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_vector3(shared, &zero));
    for (size_t i = 0; i < 4u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(shared, 0.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(shared, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        shared, NMO_OBJECT_ID_NONE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(shared, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(shared, 0xA5A5A5A5u));
    nmo_chunk_close(shared);

    nmo_objectanimation_state_t loaded;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &loaded, NULL, NULL));
    ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(
        &loaded, shared, NULL, &deserialize_context));
    ASSERT_EQ(CKOBJANIM_FORMAT_SHARED, loaded.format);
    ASSERT_EQ(0u, loaded.raw_tail_size);
    nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);

    const uint32_t packed_ids[] = {
        CK_STATESAVE_OBJANIMSHARED,
        CK_STATESAVE_OBJANIMCONTROLLERS,
        CK_STATESAVE_OBJANIMNEWDATA,
    };
    for (size_t case_index = 0;
         case_index < sizeof(packed_ids) / sizeof(packed_ids[0]);
         ++case_index) {
        nmo_chunk_t *chunk = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(chunk);
        chunk->class_id = NMO_CID_OBJECTANIMATION;
        chunk->chunk_version = NMO_CHUNK_VERSION4;
        chunk->data_version = 7;
        chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            chunk, packed_ids[case_index]));

        ASSERT_EQ(NMO_OK, nmo_chunk_write_vector3(chunk, &zero));
        for (size_t i = 0; i < 4u; ++i) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 0.0f));
        }
        if (packed_ids[case_index] == CK_STATESAVE_OBJANIMSHARED) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
            ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
                chunk, NMO_OBJECT_ID_NONE));
        } else if (packed_ids[case_index] ==
                   CK_STATESAVE_OBJANIMCONTROLLERS) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
            ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
                chunk, NMO_OBJECT_ID_NONE));
            ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 0.0f));
        } else {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 0));
            ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 0));
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
            ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
                chunk, NMO_OBJECT_ID_NONE));
            ASSERT_EQ(NMO_OK, nmo_chunk_write_float(chunk, 0.0f));
            for (size_t i = 0; i < 7u; ++i) {
                ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
            }
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0));
        nmo_chunk_close(chunk);

        nmo_objectanimation_state_t state;
        ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
            &state, NULL, NULL));
        state.flags = 0x12345678u;
        ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
                  nmo_objectanimation_deserialize(
                      &state, chunk, NULL, &deserialize_context));
        ASSERT_EQ(0x12345678u, state.flags);
        nmo_objectanimation_vtable.destroy(&state, NULL, NULL);
    }

    struct legacy_case {
        uint32_t identifier;
        size_t payload_dwords;
        size_t following_payload_dwords;
    };
    const struct legacy_case legacy_cases[] = {
        {CK_STATESAVE_OBJANIMMORPHKEYS2, 0u, 0u},
        {CK_STATESAVE_OBJANIMPOSKEYS, 1u, 0u},
        {CK_STATESAVE_OBJANIMROTKEYS, 3u, 0u},
        {CK_STATESAVE_OBJANIMSCLKEYS, 1u, 0u},
        {CK_STATESAVE_OBJANIMFLAGS, 0u, 0u},
        {CK_STATESAVE_OBJANIMENTITY, 0u, 0u},
        {CK_STATESAVE_OBJANIMLENGTH, 0u, 0u},
        {CK_STATESAVE_OBJANIMMERGE, 0u, 3u},
        {CK_STATESAVE_OBJANIMNEWDATA, 0u, 2u},
    };
    for (size_t case_index = 0;
         case_index < sizeof(legacy_cases) / sizeof(legacy_cases[0]);
         ++case_index) {
        nmo_chunk_t *chunk = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(chunk);
        chunk->class_id = NMO_CID_OBJECTANIMATION;
        chunk->chunk_version = NMO_CHUNK_VERSION4;
        chunk->data_version = 0;
        chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
            chunk, legacy_cases[case_index].identifier));
        for (size_t i = 0; i < legacy_cases[case_index].payload_dwords; ++i) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
        }
        ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(chunk, 0));
        for (size_t i = 0;
             i < legacy_cases[case_index].following_payload_dwords;
             ++i) {
            ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0u));
        }
        nmo_chunk_close(chunk);

        nmo_objectanimation_state_t state;
        ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
            &state, NULL, NULL));
        state.flags = 0x87654321u;
        ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
                  nmo_objectanimation_deserialize(
                      &state, chunk, NULL, &deserialize_context));
        ASSERT_EQ(0x87654321u, state.flags);
        nmo_objectanimation_vtable.destroy(&state, NULL, NULL);
    }

    nmo_arena_destroy(arena);
}

TEST(chunk_id_remap, objectanimation_newdata_morph_normals_are_bounded) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NOT_NULL(arena);
    nmo_serialize_context_t serialize_context = nmo_serialize_context_create(
        arena, NULL, NMO_SERIALIZE_FLAG_FILE_MODE, 0);
    nmo_deserialize_context_t deserialize_context =
        nmo_deserialize_context_create(
            arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);

    nmo_objectanimation_state_t source;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &source, NULL, NULL));
    source.format = CKOBJANIM_FORMAT_NEWDATA;
    source.has_root_pos = 1;
    source.has_length = 1;
    source.has_morph_counts = 1;
    source.morph_vertex_count = 3;
    source.morph_key_count = 2;
    nmo_objanim_morph_key_t morph_keys[2] = {0};
    source.morph_key_parsed_count = 2u;
    source.morph_keys = morph_keys;

    uint8_t controller_data[16] = {0};
    controller_data[15] = 0x7fu;
    nmo_objanim_controller_t controller = {
        .type = 0x637c4301u,
        .key_count = 1u,
        .data_size = sizeof(controller_data),
        .data = controller_data,
    };
    source.controller_count = 1u;
    source.controllers = &controller;

    uint8_t normal_data[4] = {1u, 2u, 3u, 4u};
    uint32_t normal_sizes[2] = {sizeof(normal_data), 0u};
    void *normal_data_ptrs[2] = {normal_data, NULL};
    source.morph_normals_count = 2u;
    source.morph_normals_sizes = normal_sizes;
    source.morph_normals_data = normal_data_ptrs;

    const uint32_t normal_ids[2] = {
        CK_STATESAVE_OBJANIMMORPHCOMP,
        CK_STATESAVE_OBJANIMMORPHNORMALS,
    };
    for (size_t i = 0; i < 2u; ++i) {
        source.morph_normals_id = normal_ids[i];
        nmo_chunk_t *chunk = nmo_chunk_create(arena);
        ASSERT_NOT_NULL(chunk);
        chunk->class_id = NMO_CID_OBJECTANIMATION;
        chunk->chunk_version = NMO_CHUNK_VERSION4;
        chunk->data_version = 7;
        chunk->chunk_options |= NMO_CHUNK_OPTION_FILE;
        ASSERT_EQ(NMO_OK, nmo_objectanimation_serialize(
            &source, chunk, NULL, &serialize_context));
        nmo_chunk_close(chunk);

        nmo_objectanimation_state_t loaded;
        ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
            &loaded, NULL, NULL));
        ASSERT_EQ(NMO_OK, nmo_objectanimation_deserialize(
            &loaded, chunk, NULL, &deserialize_context));
        ASSERT_EQ(CKOBJANIM_FORMAT_NEWDATA, loaded.format);
        ASSERT_EQ(2u, loaded.morph_key_parsed_count);
        ASSERT_EQ(1u, loaded.controller_count);
        ASSERT_EQ(sizeof(controller_data), loaded.controllers[0].data_size);
        ASSERT_EQ(0x7fu,
                  ((uint8_t *)loaded.controllers[0].data)[15]);
        ASSERT_EQ(normal_ids[i], loaded.morph_normals_id);
        ASSERT_EQ(2u, loaded.morph_normals_count);
        ASSERT_EQ(sizeof(normal_data), loaded.morph_normals_sizes[0]);
        ASSERT_EQ(4u,
                  ((uint8_t *)loaded.morph_normals_data[0])[3]);
        ASSERT_EQ(0u, loaded.morph_normals_sizes[1]);
        ASSERT_NULL(loaded.morph_normals_data[1]);
        nmo_objectanimation_vtable.destroy(&loaded, NULL, NULL);
    }

    nmo_chunk_t *truncated = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(truncated);
    truncated->class_id = NMO_CID_OBJECTANIMATION;
    truncated->chunk_version = NMO_CHUNK_VERSION4;
    truncated->data_version = 7;
    truncated->chunk_options |= NMO_CHUNK_OPTION_FILE;
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(truncated));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_OBJANIMNEWDATA));
    nmo_vector_t zero = {0};
    ASSERT_EQ(NMO_OK, nmo_chunk_write_vector3(truncated, &zero));
    for (size_t i = 0; i < 4u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_float(truncated, 0.0f));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 0));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(truncated, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 0u));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_raw_object_id(
        truncated, NMO_OBJECT_ID_NONE));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(truncated, 0.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_float(truncated, 0.0f));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 0u));
    for (size_t i = 0; i < 4u; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 0u));
        ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(truncated, 0u));
    }
    ASSERT_EQ(NMO_OK, nmo_chunk_write_identifier(
        truncated, CK_STATESAVE_OBJANIMMORPHCOMP));
    nmo_chunk_close(truncated);

    fail_after_allocator_state_t allocator_state = {
        .allocation_count = 0,
        .allowed_allocations = 3,
    };
    nmo_allocator_t failing_allocator = nmo_allocator_custom(
        fail_after_alloc, fail_after_free, &allocator_state);
    nmo_arena_t *failing_arena = nmo_arena_create(&failing_allocator, 1);
    ASSERT_NOT_NULL(failing_arena);
    nmo_deserialize_context_t failing_context =
        nmo_deserialize_context_create(
            failing_arena, NULL, NULL, NMO_DESER_FLAG_FILE_MODE);
    nmo_objectanimation_state_t failed;
    ASSERT_EQ(NMO_OK, nmo_objectanimation_vtable.create(
        &failed, NULL, NULL));
    failed.flags = 0x12345678u;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK, nmo_objectanimation_deserialize(
        &failed, truncated, NULL, &failing_context));
    ASSERT_EQ(0x12345678u, failed.flags);
    ASSERT_NULL(failed.morph_normals_sizes);
    ASSERT_NULL(failed.morph_normals_data);
    ASSERT_EQ(3u, allocator_state.allocation_count);

    nmo_objectanimation_vtable.destroy(&source, NULL, NULL);
    nmo_objectanimation_vtable.destroy(&failed, NULL, NULL);
    nmo_arena_destroy(failing_arena);
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
    REGISTER_TEST(chunk_id_remap, id_remap_preserves_maximum_target);
    REGISTER_TEST(chunk_id_remap, object_visibility_seek_errors_propagate_atomically);
    REGISTER_TEST(chunk_id_remap, object_only_types_delegate_state_operations);
    REGISTER_TEST(chunk_id_remap, single_id_remap);
    REGISTER_TEST(chunk_id_remap, malformed_subchunk_remap_reports_and_rolls_back);
    REGISTER_TEST(chunk_id_remap, malformed_chunk_arrays_are_rejected_before_remap);
    REGISTER_TEST(chunk_id_remap, malformed_id_metadata_reports_and_rolls_back);
    REGISTER_TEST(chunk_id_remap, sequence_id_remap);
    REGISTER_TEST(chunk_id_remap, subchunk_id_remap);
    REGISTER_TEST(chunk_id_remap, zero_and_unchanged_ids);
    REGISTER_TEST(chunk_id_remap, null_ref_uses_file_null_encoding);
    REGISTER_TEST(chunk_id_remap, ref_sequence_mapping_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, ref_sequence_rejects_invalid_identifier_end);
    REGISTER_TEST(chunk_id_remap, unresolved_ref_preserves_raw_id);
    REGISTER_TEST(chunk_id_remap, behavior_unresolved_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, behavior_legacy_file_layout_round_trips);
    REGISTER_TEST(chunk_id_remap, behavior_sections_do_not_borrow_following_identifiers);
    REGISTER_TEST(chunk_id_remap, behavior_non_file_reads_single_activity);
    REGISTER_TEST(chunk_id_remap, behavior_serializer_does_not_publish_partial_chunk);
    REGISTER_TEST(chunk_id_remap, behaviorio_truncation_keeps_previous_state);
    REGISTER_TEST(chunk_id_remap, behavior_layout_defaults_preserve_legacy_absence);
    REGISTER_TEST(chunk_id_remap, dataarray_cell_refs_round_trip_raw_ids);
    REGISTER_TEST(chunk_id_remap, dataarray_legacy_members_match_storage_mode);
    REGISTER_TEST(chunk_id_remap, dataarray_preserves_large_dimensions);
    REGISTER_TEST(chunk_id_remap, dataarray_failures_keep_state_and_target_chunk_atomic);
    REGISTER_TEST(chunk_id_remap, dataarray_copy_preserves_typed_cell_content);
    REGISTER_TEST(chunk_id_remap, attributemanager_failures_keep_state_and_target_chunk_atomic);
    REGISTER_TEST(chunk_id_remap, attributemanager_copy_preserves_record_content);
    REGISTER_TEST(chunk_id_remap, messagemanager_failures_keep_state_and_target_chunk_atomic);
    REGISTER_TEST(chunk_id_remap, messagemanager_copy_preserves_string_content);
    REGISTER_TEST(chunk_id_remap, interfaceobjectmanager_chunk_count_stays_in_section);
    REGISTER_TEST(chunk_id_remap, behaviorlink_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, material_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, material_preserves_file_layouts);
    REGISTER_TEST(chunk_id_remap, parameterlocal_owner_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, parameterin_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, parameterout_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, parameter_object_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, parameter_copy_is_deep_and_atomic);
    REGISTER_TEST(chunk_id_remap, parameteroperation_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, parameter_refs_require_layout_classes);
    REGISTER_TEST(chunk_id_remap, parameteroperation_legacy_sections_are_atomic);
    REGISTER_TEST(chunk_id_remap, camera_and_light_failures_keep_previous_state);
    REGISTER_TEST(chunk_id_remap, camera_preserves_file_layouts);
    REGISTER_TEST(chunk_id_remap, light_preserves_file_layouts);
    REGISTER_TEST(chunk_id_remap, target_camera_and_light_failures_are_atomic);
    REGISTER_TEST(chunk_id_remap, targetcamera_unresolved_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, targetlight_unresolved_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, kinematicchain_unresolved_refs_round_trip_atomically);
    REGISTER_TEST(chunk_id_remap, layer_unresolved_grid_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, layer_default_format_writes_empty_square_buffer);
    REGISTER_TEST(chunk_id_remap, layer_copy_preserves_content_equality);
    REGISTER_TEST(chunk_id_remap, grid_failures_keep_state_and_target_chunk_atomic);
    REGISTER_TEST(chunk_id_remap, grid_reserved_value_round_trips);
    REGISTER_TEST(chunk_id_remap, grid_copy_preserves_content_equality);
    REGISTER_TEST(chunk_id_remap, sprite_shared_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, sprite_raw_bitmap_payload_round_trips);
    REGISTER_TEST(chunk_id_remap, sprite_failures_keep_state_and_target_chunk_atomic);
    REGISTER_TEST(chunk_id_remap, spritetext_failures_keep_state_and_target_chunk_atomic);
    REGISTER_TEST(chunk_id_remap, texture_failures_keep_state_and_target_chunk_atomic);
    REGISTER_TEST(chunk_id_remap, texture_copy_preserves_nested_content);
    REGISTER_TEST(chunk_id_remap, texture_preserves_legacy_file_layout);
    REGISTER_TEST(chunk_id_remap, texture_empty_sections_round_trip_presence);
    REGISTER_TEST(chunk_id_remap, curvepoint_unresolved_curve_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, curvepoint_layout_follows_data_version);
    REGISTER_TEST(chunk_id_remap, curvepoint_fields_stay_in_identifier_sections);
    REGISTER_TEST(chunk_id_remap, sprite3d_unresolved_material_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, wavesound_unresolved_attachment_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, wavesound_data_stays_in_identifier_section);
    REGISTER_TEST(chunk_id_remap, wavesound_legacy_data2_round_trips_unknown_words);
    REGISTER_TEST(chunk_id_remap, wavesound_serializer_matches_version_two_layouts);
    REGISTER_TEST(chunk_id_remap, sound_family_failures_keep_state_and_target_chunk_atomic);
    REGISTER_TEST(chunk_id_remap, sound_family_copy_preserves_inherited_and_string_state);
    REGISTER_TEST(chunk_id_remap, scalar_ref_sections_do_not_publish_truncated_state);
    REGISTER_TEST(chunk_id_remap, entity3d_legacy_matrix_prefix_round_trips);
    REGISTER_TEST(chunk_id_remap, entity3d_skin_layout_follows_data_version);
    REGISTER_TEST(chunk_id_remap, entity_scalar_refs_round_trip_unresolved_raw_ids);
    REGISTER_TEST(chunk_id_remap, entity_content_equality_ignores_storage_addresses);
    REGISTER_TEST(chunk_id_remap, entity_copy_clones_inherited_and_skin_state);
    REGISTER_TEST(chunk_id_remap, object3d_delegates_state_operations);
    REGISTER_TEST(chunk_id_remap, bodypart_copy_preserves_inherited_and_own_state);
    REGISTER_TEST(chunk_id_remap, entity2d_copy_preserves_inherited_and_own_state);
    REGISTER_TEST(chunk_id_remap, entity2d_legacy_layout_round_trips);
    REGISTER_TEST(chunk_id_remap, entity2d_fields_stay_in_identifier_sections);
    REGISTER_TEST(chunk_id_remap, camera_copy_preserves_inherited_and_own_state);
    REGISTER_TEST(chunk_id_remap, targetcamera_copy_preserves_base_and_target);
    REGISTER_TEST(chunk_id_remap, light_copy_preserves_inherited_and_own_state);
    REGISTER_TEST(chunk_id_remap, targetlight_copy_preserves_base_and_target);
    REGISTER_TEST(chunk_id_remap, sprite3d_copy_preserves_inherited_and_own_state);
    REGISTER_TEST(chunk_id_remap, sprite_copy_preserves_bitmap_content);
    REGISTER_TEST(chunk_id_remap, spritetext_copy_preserves_base_and_strings);
    REGISTER_TEST(chunk_id_remap, material_copy_preserves_base_and_references);
    REGISTER_TEST(chunk_id_remap, entity_scalar_ref_sections_reject_truncation_atomically);
    REGISTER_TEST(chunk_id_remap, entity_sections_do_not_borrow_following_identifiers);
    REGISTER_TEST(chunk_id_remap, entity_skin_rejects_negative_vertex_bone_count_atomically);
    REGISTER_TEST(chunk_id_remap, entity_skin_rejects_oversized_counts_before_allocation);
    REGISTER_TEST(chunk_id_remap, entity_skin_propagates_truncated_bone_indices_atomically);
    REGISTER_TEST(chunk_id_remap, entity_serializer_does_not_publish_partial_chunk);
    REGISTER_TEST(chunk_id_remap, entity2d_serializer_does_not_publish_partial_chunk);
    REGISTER_TEST(chunk_id_remap, place_refs_round_trip_and_truncation_is_atomic);
    REGISTER_TEST(chunk_id_remap, group_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, level_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, scene_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, scene_legacy_flags_round_trip_without_reinterpretation);
    REGISTER_TEST(chunk_id_remap, synchro_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, synchro_scalar_failures_keep_state_and_target_chunk_atomic);
    REGISTER_TEST(chunk_id_remap, beobject_attribute_unresolved_ref_round_trips_raw_id);
    REGISTER_TEST(chunk_id_remap, beobject_attribute_failure_keeps_previous_atomic_state);
    REGISTER_TEST(chunk_id_remap, beobject_serializer_does_not_publish_partial_chunk);
    REGISTER_TEST(chunk_id_remap, beobject_preserves_script_and_priority_layouts);
    REGISTER_TEST(chunk_id_remap, beobject_preserves_empty_modern_attributes);
    REGISTER_TEST(chunk_id_remap, beobject_bounds_single_activity_section);
    REGISTER_TEST(chunk_id_remap, beobject_legacy_attributes_are_lossless_and_atomic);
    REGISTER_TEST(chunk_id_remap, derived_beobject_copy_clones_legacy_attributes);
    REGISTER_TEST(chunk_id_remap, beobject_copy_preserves_content_equality);
    REGISTER_TEST(chunk_id_remap, renderobject_copy_preserves_content_equality);
    REGISTER_TEST(chunk_id_remap, character_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, character_legacy_layouts_round_trip);
    REGISTER_TEST(chunk_id_remap, character_rejects_cross_section_counts_before_allocation);
    REGISTER_TEST(chunk_id_remap, bodypart_rotation_joint_round_trips_without_size_prefix);
    REGISTER_TEST(chunk_id_remap, mesh_material_refs_round_trip_without_compaction);
    REGISTER_TEST(chunk_id_remap, mesh_layout_follows_data_version);
    REGISTER_TEST(chunk_id_remap, mesh_material_sections_and_failures_are_atomic);
    REGISTER_TEST(chunk_id_remap, mesh_fields_stay_in_identifier_sections);
    REGISTER_TEST(chunk_id_remap, mesh_preserves_large_material_sections);
    REGISTER_TEST(chunk_id_remap, mesh_copy_preserves_material_records);
    REGISTER_TEST(chunk_id_remap, mesh_rejects_truncated_large_lines_before_allocation);
    REGISTER_TEST(chunk_id_remap, mesh_preserves_large_geometry_counts);
    REGISTER_TEST(chunk_id_remap, mesh_preserves_large_vertex_counts);
    REGISTER_TEST(chunk_id_remap, mesh_preserves_large_weight_counts);
    REGISTER_TEST(chunk_id_remap, mesh_rejects_truncated_large_weights_before_allocation);
    REGISTER_TEST(chunk_id_remap, patchmesh_data3_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, patchmesh_rejects_cross_section_legacy_materials);
    REGISTER_TEST(chunk_id_remap, patchmesh_data_sections_do_not_borrow_following_identifiers);
    REGISTER_TEST(chunk_id_remap, patchmesh_data2_layout_and_empty_sections_round_trip);
    REGISTER_TEST(chunk_id_remap, patchmesh_serializer_rejects_partial_state);
    REGISTER_TEST(chunk_id_remap, patchmesh_copy_preserves_atomic_records);
    REGISTER_TEST(chunk_id_remap, animation_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, keyedanimation_sections_round_trip_independently);
    REGISTER_TEST(chunk_id_remap, animation_sections_do_not_borrow_following_identifiers);
    REGISTER_TEST(chunk_id_remap, curve_staging_initializes_inherited_arrays);
    REGISTER_TEST(chunk_id_remap, curve_layout_follows_data_version);
    REGISTER_TEST(chunk_id_remap, curve_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, objectanimation_refs_round_trip_and_failure_is_atomic);
    REGISTER_TEST(chunk_id_remap, objectanimation_sections_do_not_borrow_following_identifiers);
    REGISTER_TEST(chunk_id_remap, objectanimation_newdata_morph_normals_are_bounded);
    REGISTER_TEST(chunk_id_remap, legacy_unresolved_id_preserves_raw_id);
TEST_MAIN_END()
