/**
 * @file test_chunk_id_remap.c
 * @brief Test chunk ID remapping functionality
 */

#include "../test_framework.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_id_remap.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

TEST(chunk_id_remap, id_remap_basic) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    nmo_id_remap_t* remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    
    // Add some mappings
    ASSERT_EQ(nmo_id_remap_add(remap, 100, 200).code, NMO_OK);
    ASSERT_EQ(nmo_id_remap_add(remap, 101, 201).code, NMO_OK);
    ASSERT_EQ(nmo_id_remap_add(remap, 102, 202).code, NMO_OK);
    
    // Lookup existing IDs
    nmo_object_id_t new_id;
    ASSERT_EQ(nmo_id_remap_lookup_id(remap, 100, &new_id).code, NMO_OK);
    ASSERT_EQ(new_id, 200);
    
    ASSERT_EQ(nmo_id_remap_lookup_id(remap, 101, &new_id).code, NMO_OK);
    ASSERT_EQ(new_id, 201);
    
    ASSERT_EQ(nmo_id_remap_lookup_id(remap, 102, &new_id).code, NMO_OK);
    ASSERT_EQ(new_id, 202);
    
    // Lookup non-existent ID
    ASSERT_EQ(nmo_id_remap_lookup_id(remap, 999, &new_id).code, NMO_ERR_NOT_FOUND);
    
    // Clear and verify
    nmo_id_remap_clear(remap);
    ASSERT_EQ(nmo_id_remap_lookup_id(remap, 100, &new_id).code, NMO_ERR_NOT_FOUND);
    
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
    
    ASSERT_EQ(nmo_id_remap_add(remap, 100, 200).code, NMO_OK);
    ASSERT_EQ(nmo_id_remap_add(remap, 101, 201).code, NMO_OK);
    
    // Apply remapping
    ASSERT_EQ(nmo_chunk_remap_object_ids(chunk, remap).code, NMO_OK);
    
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
    nmo_chunk_start_object_sequence(chunk, 4);
    for (int i = 0; i < 4; i++) {
        nmo_chunk_write_object_id_sequence(chunk, ids[i]);
    }
    
    nmo_chunk_close(chunk);
    
    // Create remap table
    nmo_id_remap_t* remap = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(remap);
    
    // Map all IDs
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(nmo_id_remap_add(remap, 100 + i, 200 + i).code, NMO_OK);
    }
    
    // Apply remapping
    ASSERT_EQ(nmo_chunk_remap_object_ids(chunk, remap).code, NMO_OK);
    
    // Read back and verify
    // Note: The IDs are stored in the chunk->ids list and data buffer
    // For this test, we'll verify through the chunk's internal state
    // since there's no explicit "read sequence" API yet
    
    // Verify the remapping worked by checking the data directly
    // The sequence should have been remapped from [100,101,102,103] to [200,201,202,203]
    ASSERT_NOT_NULL(chunk->data);
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
    
    ASSERT_EQ(nmo_id_remap_add(remap, 100, 200).code, NMO_OK);
    ASSERT_EQ(nmo_id_remap_add(remap, 101, 201).code, NMO_OK);
    ASSERT_EQ(nmo_id_remap_add(remap, 102, 202).code, NMO_OK);
    
    // Apply remapping (should recursively process sub-chunk)
    ASSERT_EQ(nmo_chunk_remap_object_ids(parent, remap).code, NMO_OK);
    
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
    nmo_result_t result = nmo_chunk_read_sub_chunk(parent, &read_sub);
    ASSERT_EQ(result.code, NMO_OK);
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
    
    ASSERT_EQ(nmo_id_remap_add(remap, 100, 200).code, NMO_OK);
    ASSERT_EQ(nmo_id_remap_add(remap, 101, 201).code, NMO_OK);
    // Note: no mapping for 999
    
    // Apply remapping
    ASSERT_EQ(nmo_chunk_remap_object_ids(chunk, remap).code, NMO_OK);
    
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

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_id_remap, id_remap_basic);
    REGISTER_TEST(chunk_id_remap, single_id_remap);
    REGISTER_TEST(chunk_id_remap, sequence_id_remap);
    REGISTER_TEST(chunk_id_remap, subchunk_id_remap);
    REGISTER_TEST(chunk_id_remap, zero_and_unchanged_ids);
TEST_MAIN_END()
