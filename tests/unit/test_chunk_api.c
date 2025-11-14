/**
 * @file test_chunk_api.c
 * @brief Tests for high-level chunk API
 */

#include "../test_framework.h"
#include <string.h>
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include "core/nmo_arena.h"

// Test: Basic write/read primitives
TEST(chunk_api, primitives) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Write
    nmo_chunk_start_write(chunk);
    nmo_chunk_write_byte(chunk, 0x42);
    nmo_chunk_write_word(chunk, 0x1234);
    nmo_chunk_write_int(chunk, 42);
    nmo_chunk_write_dword(chunk, 0xDEADBEEF);
    nmo_chunk_write_float(chunk, 3.14f);
    nmo_chunk_close(chunk);
    
    // Read
    nmo_chunk_start_read(chunk);
    uint8_t b;
    uint16_t w;
    int32_t i;
    uint32_t d;
    float f;
    
    nmo_chunk_read_byte(chunk, &b);
    nmo_chunk_read_word(chunk, &w);
    nmo_chunk_read_int(chunk, &i);
    nmo_chunk_read_dword(chunk, &d);
    nmo_chunk_read_float(chunk, &f);
    
    // Verify
    ASSERT_EQ(b, 0x42);
    ASSERT_EQ(w, 0x1234);
    ASSERT_EQ(i, 42);
    ASSERT_EQ(d, 0xDEADBEEF);
    ASSERT_IN_RANGE_FLOAT(f, 3.13f, 3.15f, 0.01f); // Float comparison tolerance
    
    nmo_arena_destroy(arena);
}

// Test: String write/read
TEST(chunk_api, string) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Write
    nmo_chunk_start_write(chunk);
    nmo_chunk_write_string(chunk, "Hello, World!");
    nmo_chunk_write_string(chunk, "");
    nmo_chunk_write_string(chunk, NULL);
    nmo_chunk_write_string(chunk, "Test");
    nmo_chunk_close(chunk);
    
    // Read
    nmo_chunk_start_read(chunk);
    char* s1;
    char* s2;
    char* s3;
    char* s4;
    
    size_t len1 = nmo_chunk_read_string(chunk, &s1);
    size_t len2 = nmo_chunk_read_string(chunk, &s2);
    size_t len3 = nmo_chunk_read_string(chunk, &s3);
    size_t len4 = nmo_chunk_read_string(chunk, &s4);
    
    // Verify
    ASSERT_EQ(len1, 13);
    ASSERT_STR_EQ(s1, "Hello, World!");
    ASSERT_EQ(len2, 0);  // Empty string "" returns length 0
    ASSERT_NOT_NULL(s2);
    ASSERT_EQ(s2[0], '\0');  // But pointer is not NULL
    ASSERT_EQ(len3, 0);
    ASSERT_NULL(s3);  // NULL string returns NULL pointer
    ASSERT_EQ(len4, 4);
    ASSERT_STR_EQ(s4, "Test");
    
    nmo_arena_destroy(arena);
}

// Test: Buffer write/read
TEST(chunk_api, buffer) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Test data
    uint8_t data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    
    // Write
    nmo_chunk_start_write(chunk);
    nmo_chunk_write_buffer(chunk, data, sizeof(data));
    nmo_chunk_write_buffer(chunk, NULL, 0);
    nmo_chunk_close(chunk);
    
    // Read
    nmo_chunk_start_read(chunk);
    void* buf1;
    size_t size1;
    void* buf2;
    size_t size2;
    
    nmo_chunk_read_buffer(chunk, &buf1, &size1);
    nmo_chunk_read_buffer(chunk, &buf2, &size2);
    
    // Verify
    ASSERT_EQ(size1, sizeof(data));
    ASSERT_MEM_EQ(buf1, data, size1);
    ASSERT_EQ(size2, 0);
    ASSERT_NULL(buf2);
    
    nmo_arena_destroy(arena);
}

// Test: GUID write/read
TEST(chunk_api, guid) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    nmo_guid_t guid1 = {0x12345678, 0x9ABCDEF0};
    nmo_guid_t guid2 = {0xDEADBEEF, 0xCAFEBABE};
    
    // Write
    nmo_chunk_start_write(chunk);
    nmo_chunk_write_guid(chunk, guid1);
    nmo_chunk_write_guid(chunk, guid2);
    nmo_chunk_close(chunk);
    
    // Read
    nmo_chunk_start_read(chunk);
    nmo_guid_t g1, g2;
    
    nmo_chunk_read_guid(chunk, &g1);
    nmo_chunk_read_guid(chunk, &g2);
    
    // Verify
    ASSERT_EQ(g1.d1, guid1.d1);
    ASSERT_EQ(g1.d2, guid1.d2);
    ASSERT_EQ(g2.d1, guid2.d1);
    ASSERT_EQ(g2.d2, guid2.d2);
    
    nmo_arena_destroy(arena);
}

// Test: Object ID tracking
TEST(chunk_api, object_id) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Write
    nmo_chunk_start_write(chunk);
    nmo_chunk_write_int(chunk, 999);
    nmo_chunk_write_object_id(chunk, 0); // null reference
    nmo_chunk_write_object_id(chunk, 100);
    nmo_chunk_write_object_id(chunk, 200);
    nmo_chunk_write_object_id(chunk, 100); // duplicate
    nmo_chunk_close(chunk);
    
    // Verify IDS list was created
    // Note: We can't directly access chunk->chunk_options, id_count, or ids
    // Instead, we verify by reading back the data to ensure the functionality works
    
    // Read
    nmo_chunk_start_read(chunk);
    int32_t val;
    nmo_object_id_t id0, id1, id2, id3;
    
    nmo_chunk_read_int(chunk, &val);
    nmo_chunk_read_object_id(chunk, &id0);
    nmo_chunk_read_object_id(chunk, &id1);
    nmo_chunk_read_object_id(chunk, &id2);
    nmo_chunk_read_object_id(chunk, &id3);
    
    // Verify
    ASSERT_EQ(val, 999);
    ASSERT_EQ(id0, 0);
    ASSERT_EQ(id1, 100);
    ASSERT_EQ(id2, 200);
    ASSERT_EQ(id3, 100);
    
    nmo_arena_destroy(arena);
}

// Test: Sequences
TEST(chunk_api, sequence) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Write
    nmo_chunk_start_write(chunk);
    nmo_chunk_write_object_sequence_start(chunk, 3);
    nmo_chunk_write_object_sequence_item(chunk, 10);
    nmo_chunk_write_object_sequence_item(chunk, 20);
    nmo_chunk_write_object_sequence_item(chunk, 30);
    nmo_chunk_close(chunk);
    
    // Read
    nmo_chunk_start_read(chunk);
    size_t count;
    nmo_chunk_read_object_sequence_start(chunk, &count);
    
    ASSERT_EQ(count, 3);
    
    nmo_object_id_t ids[3];
    for (size_t i = 0; i < count; i++) {
        nmo_chunk_read_object_id(chunk, &ids[i]);
    }
    
    // Verify
    ASSERT_EQ(ids[0], 10);
    ASSERT_EQ(ids[1], 20);
    ASSERT_EQ(ids[2], 30);
    
    nmo_arena_destroy(arena);
}

// Test: Navigation
TEST(chunk_api, navigation) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Write
    nmo_chunk_start_write(chunk);
    for (int i = 0; i < 10; i++) {
        nmo_chunk_write_int(chunk, i * 10);
    }
    nmo_chunk_close(chunk);
    
    // Test navigation
    nmo_chunk_start_read(chunk);
    
    // Read first
    int32_t val;
    nmo_chunk_read_int(chunk, &val);
    ASSERT_EQ(val, 0);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 1);
    
    // Skip
    nmo_chunk_skip(chunk, 2);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 3);
    nmo_chunk_read_int(chunk, &val);
    ASSERT_EQ(val, 30);
    
    // Goto
    nmo_chunk_goto(chunk, 7);
    nmo_chunk_read_int(chunk, &val);
    ASSERT_EQ(val, 70);
    
    nmo_arena_destroy(arena);
}

// Test: Auto-expansion
TEST(chunk_api, auto_expand) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Write many items
    nmo_chunk_start_write(chunk);
    for (int i = 0; i < 1000; i++) {
        nmo_chunk_write_int(chunk, i);
    }
    nmo_chunk_close(chunk);
    
    // Verify size using API
    ASSERT_EQ(nmo_chunk_get_data_size(chunk), 1000 * 4); // data_size is in DWORDs, get_data_size returns bytes
    
    // Read back
    nmo_chunk_start_read(chunk);
    for (int i = 0; i < 1000; i++) {
        int32_t val;
        nmo_chunk_read_int(chunk, &val);
        ASSERT_EQ(val, i);
    }
    
    nmo_arena_destroy(arena);
}

// Test: Identifiers
TEST(chunk_api, identifiers) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Write with identifiers
    nmo_chunk_start_write(chunk);
    nmo_chunk_write_int(chunk, 10);
    nmo_chunk_write_identifier(chunk, 0xAAAA);
    nmo_chunk_write_int(chunk, 20);
    nmo_chunk_write_int(chunk, 30);
    nmo_chunk_write_identifier(chunk, 0xBBBB);
    nmo_chunk_write_int(chunk, 40);
    nmo_chunk_close(chunk);
    
    // Seek to identifiers
    nmo_chunk_start_read(chunk);
    
    // Read first value
    int32_t val;
    nmo_chunk_read_int(chunk, &val);
    ASSERT_EQ(val, 10);
    
    // Seek to first identifier
    nmo_result_t result = nmo_chunk_seek_identifier(chunk, 0xAAAA);
    ASSERT_EQ(result.code, NMO_OK);
    nmo_chunk_read_int(chunk, &val);
    ASSERT_EQ(val, 20);
    
    // Seek to second identifier
    result = nmo_chunk_seek_identifier(chunk, 0xBBBB);
    ASSERT_EQ(result.code, NMO_OK);
    nmo_chunk_read_int(chunk, &val);
    ASSERT_EQ(val, 40);
    
    // Try to seek non-existent identifier
    result = nmo_chunk_seek_identifier(chunk, 0xCCCC);
    ASSERT_NE(result.code, NMO_OK);
    
    nmo_arena_destroy(arena);
}

TEST(chunk_api, manager_sequence) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Start write mode
    nmo_result_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result.code, NMO_OK);
    
    // Start manager sequence
    nmo_guid_t mgr_guid = {0x12345678, 0x9ABCDEF0};
    result = nmo_chunk_start_manager_sequence(chunk, mgr_guid, 3);
    ASSERT_EQ(result.code, NMO_OK);
    // Note: Can't directly access chunk_options, but functionality is verified by successful manager operations
    
    // Write manager ints
    result = nmo_chunk_write_manager_int(chunk, 100, 0xAABBCCDD);
    ASSERT_EQ(result.code, NMO_OK);
    result = nmo_chunk_write_manager_int(chunk, 200, 0x11223344);
    ASSERT_EQ(result.code, NMO_OK);
    result = nmo_chunk_write_manager_int(chunk, 300, 0x55667788);
    ASSERT_EQ(result.code, NMO_OK);
    
    // Start read mode
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result.code, NMO_OK);
    
    // Read manager sequence header
    nmo_guid_t read_guid;
    uint32_t count;
    result = nmo_chunk_read_guid(chunk, &read_guid);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(read_guid.d1, mgr_guid.d1);
    ASSERT_EQ(read_guid.d2, mgr_guid.d2);
    result = nmo_chunk_read_dword(chunk, &count);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(count, 3);
    
    // Read manager ints
    nmo_manager_id_t mgr_id;
    uint32_t value;
    result = nmo_chunk_read_manager_int(chunk, &mgr_id, &value);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(mgr_id, 100);
    ASSERT_EQ(value, 0xAABBCCDD);
    result = nmo_chunk_read_manager_int(chunk, &mgr_id, &value);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(mgr_id, 200);
    ASSERT_EQ(value, 0x11223344);
    result = nmo_chunk_read_manager_int(chunk, &mgr_id, &value);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(mgr_id, 300);
    ASSERT_EQ(value, 0x55667788);
    
    nmo_arena_destroy(arena);
}

TEST(chunk_api, sub_chunks) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);
    
    // Create sub-chunk
    nmo_chunk_t* sub = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(sub);
    nmo_result_t result = nmo_chunk_start_write(sub);
    ASSERT_EQ(result.code, NMO_OK);
    result = nmo_chunk_write_dword(sub, 0x12345678);
    ASSERT_EQ(result.code, NMO_OK);
    result = nmo_chunk_write_string(sub, "SubChunkData");
    ASSERT_EQ(result.code, NMO_OK);
    
    // Create parent chunk
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result.code, NMO_OK);
    
    // Start sub-chunk sequence
    result = nmo_chunk_start_sub_chunk_sequence(chunk, 2);
    ASSERT_EQ(result.code, NMO_OK);
    // Note: Can't directly access chunk_options, but functionality is verified by successful sub-chunk operations
    
    // Write sub-chunks
    result = nmo_chunk_write_sub_chunk(chunk, sub);
    ASSERT_EQ(result.code, NMO_OK);
    result = nmo_chunk_write_sub_chunk(chunk, sub);
    ASSERT_EQ(result.code, NMO_OK);
    
    // Start read mode
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result.code, NMO_OK);
    
    // Read sub-chunk count
    uint32_t count;
    result = nmo_chunk_read_dword(chunk, &count);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(count, 2);
    
    // Read first sub-chunk
    nmo_chunk_t* read_sub;
    result = nmo_chunk_read_sub_chunk(chunk, &read_sub);
    ASSERT_EQ(result.code, NMO_OK);
    result = nmo_chunk_start_read(read_sub);
    ASSERT_EQ(result.code, NMO_OK);
    uint32_t dword;
    result = nmo_chunk_read_dword(read_sub, &dword);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(dword, 0x12345678);
    char* str;
    size_t str_len = nmo_chunk_read_string(read_sub, &str);
    ASSERT_GT(str_len, 0);
    ASSERT_STR_EQ(str, "SubChunkData");
    
    // Read second sub-chunk
    result = nmo_chunk_read_sub_chunk(chunk, &read_sub);
    ASSERT_EQ(result.code, NMO_OK);
    result = nmo_chunk_start_read(read_sub);
    ASSERT_EQ(result.code, NMO_OK);
    result = nmo_chunk_read_dword(read_sub, &dword);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(dword, 0x12345678);
    
    nmo_arena_destroy(arena);
}

TEST(chunk_api, arrays) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Start write mode
    nmo_result_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result.code, NMO_OK);
    
    // Write int array
    int int_array[] = {1, 2, 3, 4, 5};
    result = nmo_chunk_write_array(chunk, int_array, 5, sizeof(int));
    ASSERT_EQ(result.code, NMO_OK);
    
    // Write float array
    float float_array[] = {1.5f, 2.5f, 3.5f};
    result = nmo_chunk_write_array(chunk, float_array, 3, sizeof(float));
    ASSERT_EQ(result.code, NMO_OK);
    
    // Start read mode
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result.code, NMO_OK);
    
    // Read int array
    void* read_array;
    size_t count, elem_size;
    result = nmo_chunk_read_array(chunk, &read_array, &count, &elem_size);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(count, 5);
    ASSERT_EQ(elem_size, sizeof(int));
    int* int_ptr = (int*)read_array;
    for (size_t i = 0; i < 5; i++) {
        ASSERT_EQ(int_ptr[i], (int)(i + 1));
    }
    
    // Read float array
    result = nmo_chunk_read_array(chunk, &read_array, &count, &elem_size);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(count, 3);
    ASSERT_EQ(elem_size, sizeof(float));
    float* float_ptr = (float*)read_array;
    ASSERT_FLOAT_EQ(float_ptr[0], 1.5f, 0.001f);
    ASSERT_FLOAT_EQ(float_ptr[1], 2.5f, 0.001f);
    ASSERT_FLOAT_EQ(float_ptr[2], 3.5f, 0.001f);
    
    nmo_arena_destroy(arena);
}

TEST(chunk_api, compression) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Start write mode
    nmo_result_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result.code, NMO_OK);
    
    // Write repetitive data (compresses well)
    for (int i = 0; i < 100; i++) {
        result = nmo_chunk_write_int(chunk, 0x12345678);
        ASSERT_EQ(result.code, NMO_OK);
    }
    
    size_t original_size = nmo_chunk_get_data_size(chunk) / 4; // Convert bytes to DWORDs
    
    // Pack
    result = nmo_chunk_pack(chunk, 6);
    ASSERT_EQ(result.code, NMO_OK);
    // Note: Can't directly access chunk_options or unpack_size
    // Verify compression worked by checking that data size changed
    size_t packed_size = nmo_chunk_get_data_size(chunk) / 4; // Convert bytes to DWORDs
    ASSERT_LT(packed_size, original_size); // Should compress
    
    // Unpack
    result = nmo_chunk_unpack(chunk);
    ASSERT_EQ(result.code, NMO_OK);
    // Note: Can't directly access chunk_options
    // Verify unpack worked by checking data size is restored
    ASSERT_EQ(nmo_chunk_get_data_size(chunk) / 4, original_size); // Convert bytes to DWORDs
    
    // Verify data integrity
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result.code, NMO_OK);
    for (int i = 0; i < 100; i++) {
        int32_t value;
        result = nmo_chunk_read_int(chunk, &value);
        ASSERT_EQ(result.code, NMO_OK);
        ASSERT_EQ(value, 0x12345678);
    }
    
    nmo_arena_destroy(arena);
}

TEST(chunk_api, compression_new_api) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 32);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_result_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result.code, NMO_OK);

    for (int i = 0; i < 128; ++i) {
        result = nmo_chunk_write_int(chunk, 0x11111111);
        ASSERT_EQ(result.code, NMO_OK);
    }

    size_t original_bytes = nmo_chunk_get_data_size(chunk);
    ASSERT_GT(original_bytes, 0U);

    result = nmo_chunk_compress(chunk, 6);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_LT(nmo_chunk_get_data_size(chunk), original_bytes);

    result = nmo_chunk_decompress(chunk);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(nmo_chunk_get_data_size(chunk), original_bytes);

    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result.code, NMO_OK);
    for (int i = 0; i < 64; ++i) {
        result = nmo_chunk_write_int(chunk, i);
        ASSERT_EQ(result.code, NMO_OK);
    }

    size_t noisy_bytes = nmo_chunk_get_data_size(chunk);
    ASSERT_GT(noisy_bytes, 0U);

    // Use an extremely small ratio to guarantee compression is skipped
    result = nmo_chunk_compress_if_beneficial(chunk, 6, 0.01f);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(nmo_chunk_get_data_size(chunk), noisy_bytes);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, crc) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Start write mode
    nmo_result_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result.code, NMO_OK);
    
    // Write test data
    result = nmo_chunk_write_int(chunk, 0x11111111);
    ASSERT_EQ(result.code, NMO_OK);
    result = nmo_chunk_write_int(chunk, 0x22222222);
    ASSERT_EQ(result.code, NMO_OK);
    result = nmo_chunk_write_int(chunk, 0x33333333);
    ASSERT_EQ(result.code, NMO_OK);
    
    // Compute CRC
    uint32_t crc;
    result = nmo_chunk_compute_crc(chunk, 1, &crc);
    ASSERT_EQ(result.code, NMO_OK);
    
    // CRC should be deterministic
    uint32_t crc2;
    result = nmo_chunk_compute_crc(chunk, 1, &crc2);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(crc, crc2);
    
    // Different data should give different CRC
    result = nmo_chunk_write_int(chunk, 0x44444444);
    ASSERT_EQ(result.code, NMO_OK);
    uint32_t crc3;
    result = nmo_chunk_compute_crc(chunk, 1, &crc3);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NE(crc3, crc);
    
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_api, primitives);
    REGISTER_TEST(chunk_api, string);
    REGISTER_TEST(chunk_api, buffer);
    REGISTER_TEST(chunk_api, guid);
    REGISTER_TEST(chunk_api, object_id);
    REGISTER_TEST(chunk_api, sequence);
    REGISTER_TEST(chunk_api, navigation);
    REGISTER_TEST(chunk_api, auto_expand);
    REGISTER_TEST(chunk_api, identifiers);
    REGISTER_TEST(chunk_api, manager_sequence);
    REGISTER_TEST(chunk_api, sub_chunks);
    REGISTER_TEST(chunk_api, arrays);
    REGISTER_TEST(chunk_api, compression);
    REGISTER_TEST(chunk_api, compression_new_api);
    REGISTER_TEST(chunk_api, crc);
TEST_MAIN_END()
