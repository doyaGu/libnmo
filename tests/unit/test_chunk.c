/**
 * @file test_chunk.c
 * @brief Unit tests for chunk handling
 */

#include "../test_framework.h"
#include "nmo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Test 1: Create chunk
 */
TEST(chunk, create) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test 2: Get chunk id
 */
TEST(chunk, get_id) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Default chunk ID should be 0
    uint32_t id = nmo_chunk_get_class_id(chunk);
    ASSERT_EQ(id, 0);
    
    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test 3: Chunk serialization
 */
TEST(chunk, serialization) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Serialize
    void* out_data = NULL;
    size_t out_size = 0;
    nmo_result_t result = nmo_chunk_serialize(chunk, &out_data, &out_size, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(out_data);
    ASSERT_GT(out_size, 0);
    
    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test 4: Create chunk with NULL arena
 */
TEST(chunk, create_null_arena) {
    nmo_chunk_t* chunk = nmo_chunk_create(NULL);
    ASSERT_NULL(chunk);
}

/**
 * Test 5: Serialize with NULL parameters
 */
TEST(chunk, serialize_null_params) {
    nmo_result_t result = nmo_chunk_serialize(NULL, NULL, NULL, NULL);
    ASSERT_NE(result.code, NMO_OK);
}

/**
 * Test 6: Get operations with NULL chunk
 */
TEST(chunk, get_operations_null) {
    uint32_t id = nmo_chunk_get_class_id(NULL);
    ASSERT_EQ(id, 0);
    
    size_t size = nmo_chunk_get_data_size(NULL);
    ASSERT_EQ(size, 0);
    
    size_t data_size = 0;
    const void* data = nmo_chunk_get_data(NULL, &data_size);
    ASSERT_NULL(data);
    ASSERT_EQ(data_size, 0);
}

/**
 * Test 7: Add sub-chunk with NULL parameters
 */
TEST(chunk, add_subchunk_null) {
    nmo_result_t result = nmo_chunk_add_sub_chunk(NULL, NULL);
    ASSERT_NE(result.code, NMO_OK);
    
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    result = nmo_chunk_add_sub_chunk(chunk, NULL);
    ASSERT_NE(result.code, NMO_OK);
    
    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

/**
 * Test 8: Clone with NULL parameters
 */
TEST(chunk, clone_null_params) {
    nmo_chunk_t* cloned = nmo_chunk_clone(NULL, NULL);
    ASSERT_NULL(cloned);
    
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Test with NULL arena
    cloned = nmo_chunk_clone(chunk, NULL);
    ASSERT_NULL(cloned);
    
    nmo_chunk_destroy(chunk);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk, create);
    REGISTER_TEST(chunk, get_id);
    REGISTER_TEST(chunk, serialization);
    REGISTER_TEST(chunk, create_null_arena);
    REGISTER_TEST(chunk, serialize_null_params);
    REGISTER_TEST(chunk, get_operations_null);
    REGISTER_TEST(chunk, add_subchunk_null);
    REGISTER_TEST(chunk, clone_null_params);
TEST_MAIN_END()