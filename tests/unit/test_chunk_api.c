/**
 * @file test_chunk_api.c
 * @brief Tests for high-level chunk API
 */

#include "../test_framework.h"
#include <string.h>
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"

typedef struct chunk_api_fail_allocator_state {
    size_t allocation_count;
    size_t allowed_allocations;
} chunk_api_fail_allocator_state_t;

static void *chunk_api_fail_alloc(
    void *user_data, size_t size, size_t alignment)
{
    chunk_api_fail_allocator_state_t *state =
        (chunk_api_fail_allocator_state_t *)user_data;
    if (state->allocation_count >= state->allowed_allocations) return NULL;
    state->allocation_count++;
    nmo_allocator_t allocator = nmo_allocator_default();
    return nmo_alloc(&allocator, size, alignment);
}

static void chunk_api_fail_free(void *user_data, void *ptr)
{
    (void)user_data;
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_free(&allocator, ptr);
}

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

TEST(chunk_api, string_truncated_payload_keeps_position) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);

    /* Declared length is 8 bytes (2 DWORD payload), but only 1 DWORD is present. */
    result = nmo_chunk_write_dword(chunk, 8u);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 0xAABBCCDDu);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    char* out = (char*)1;
    size_t len = nmo_chunk_read_string(chunk, &out);
    ASSERT_EQ(len, 0u);
    ASSERT_NULL(out);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

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

TEST(chunk_api, buffer_truncated_payload) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);

    /* Declared byte size is 8, but only 4 bytes are written. */
    result = nmo_chunk_write_dword(chunk, 8);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 0xAABBCCDDu);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result, NMO_OK);

    void* out_data = (void*)1;
    size_t out_size = 123;
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);
    result = nmo_chunk_read_buffer(chunk, &out_data, &out_size);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);
    ASSERT_NULL(out_data);
    ASSERT_EQ(0u, out_size);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, buffer_fill_errors_keep_position) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_write_dword(chunk, 8u);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 0xAABBCCDDu);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 0x11223344u);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result, NMO_OK);

    uint8_t small[4];
    size_t read_size = nmo_chunk_read_and_fill_buffer(chunk, small, sizeof(small));
    ASSERT_EQ(read_size, 0u);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, buffer_null_data_writes_zero_size) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_write_buffer(chunk, NULL, 16);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result, NMO_OK);

    void* out_data = (void*)1;
    size_t out_size = 123;
    result = nmo_chunk_read_buffer(chunk, &out_data, &out_size);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(out_size, 0);
    ASSERT_NULL(out_data);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, buffer_no_size_rejects_null_data_with_size) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_write_buffer_no_size(chunk, NULL, 4);
    ASSERT_EQ(result, NMO_ERR_INVALID_ARGUMENT);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, buffer_writes_reject_size_overflow_atomically) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0x12345678u));

    const uint8_t byte = 0xAA;
#if SIZE_MAX > UINT32_MAX
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_write_buffer(chunk, &byte, SIZE_MAX));
    ASSERT_EQ(1u, nmo_chunk_get_position(chunk));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(chunk));
#endif
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_write_buffer_no_size(chunk, &byte, SIZE_MAX));
    ASSERT_EQ(1u, nmo_chunk_get_position(chunk));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(chunk));

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

TEST(chunk_api, sequence_rejects_negative_count) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);
    nmo_chunk_write_int(chunk, -1);
    nmo_chunk_close(chunk);

    nmo_chunk_start_read(chunk);
    size_t count = 123;
    nmo_status_t result = nmo_chunk_read_object_sequence_start(chunk, &count);

    ASSERT_EQ(result, NMO_ERR_INVALID_FORMAT);
    ASSERT_EQ(count, 0);
    ASSERT_EQ(0u, nmo_chunk_get_position(chunk));

    nmo_arena_destroy(arena);
}

TEST(chunk_api, sequence_rejects_truncated_items) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_object_id(chunk, 10));
    nmo_chunk_close(chunk);

    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));
    size_t count = 123;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
        nmo_chunk_read_object_sequence_start(chunk, &count));
    ASSERT_EQ(0u, count);
    ASSERT_EQ(0u, nmo_chunk_get_position(chunk));

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

TEST(chunk_api, navigation_read_bounds) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);
    nmo_chunk_write_int(chunk, 10);
    nmo_chunk_write_int(chunk, 20);
    nmo_chunk_close(chunk);

    nmo_chunk_start_read(chunk);
    nmo_status_t result = nmo_chunk_skip(chunk, 3);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0);

    result = nmo_chunk_goto(chunk, 3);
    ASSERT_EQ(result, NMO_ERR_INVALID_OFFSET);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, sequence_write_rejects_unencodable_count) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0x12345678u));

    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_write_object_sequence_start(
            chunk, (size_t)INT32_MAX + 1u));
    ASSERT_EQ(1u, nmo_chunk_get_position(chunk));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(chunk));
    ASSERT_EQ(0u, nmo_chunk_get_id_count(chunk));

    nmo_arena_destroy(arena);
}

TEST(chunk_api, check_size_rounds_and_rejects_overflow) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));

    nmo_chunk_parser_state_t* state = nmo_chunk_get_parser_state(chunk);
    ASSERT_NOT_NULL(state);
    ASSERT_EQ(NMO_OK, nmo_chunk_check_size(chunk, 1));
    ASSERT_TRUE(state->data_size >= 1u);

#if SIZE_MAX > UINT32_MAX
    const size_t original_data_size = state->data_size;
    state->current_pos = UINT32_MAX;
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_check_size(chunk, sizeof(uint32_t)));
    ASSERT_EQ((size_t)UINT32_MAX, state->current_pos);
    ASSERT_EQ(original_data_size, state->data_size);
    ASSERT_EQ(0u, chunk->data.count);
#endif

    state->current_pos = SIZE_MAX - 1u;
    ASSERT_EQ(NMO_ERR_INVALID_OFFSET,
        nmo_chunk_check_size(chunk, 8));
    ASSERT_EQ(SIZE_MAX - 1u, state->current_pos);

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
    nmo_chunk_write_identifier(chunk, 0xAAAA);
    nmo_chunk_write_int(chunk, 20);
    nmo_chunk_write_int(chunk, 30);
    nmo_chunk_write_identifier(chunk, 0xBBBB);
    nmo_chunk_write_int(chunk, 40);
    nmo_chunk_close(chunk);
    
    // Seek to identifiers
    nmo_chunk_start_read(chunk);
    
    // Seek to first identifier
    int32_t val;
    nmo_status_t result = nmo_chunk_seek_identifier(chunk, 0xAAAA);
    ASSERT_EQ(result, NMO_OK);
    nmo_chunk_read_int(chunk, &val);
    ASSERT_EQ(val, 20);
    
    // Seek to second identifier
    result = nmo_chunk_seek_identifier(chunk, 0xBBBB);
    ASSERT_EQ(result, NMO_OK);
    nmo_chunk_read_int(chunk, &val);
    ASSERT_EQ(val, 40);
    
    // Try to seek non-existent identifier
    result = nmo_chunk_seek_identifier(chunk, 0xCCCC);
    ASSERT_NE(result, NMO_OK);
    
    nmo_arena_destroy(arena);
}

TEST(chunk_api, identifier_write_validation_is_atomic) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(nmo_chunk_start_write(chunk), NMO_OK);
    ASSERT_EQ(nmo_chunk_write_identifier(chunk, 0xAAAA), NMO_OK);
    ASSERT_EQ(nmo_chunk_write_int(chunk, 1234), NMO_OK);

    nmo_chunk_parser_state_t* state = nmo_chunk_get_parser_state(chunk);
    ASSERT_NOT_NULL(state);
    uint32_t* data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);
    ASSERT_EQ(chunk->data.count, 3);
    ASSERT_EQ(data[1], 0);

    const size_t valid_pos = state->current_pos;
    const size_t valid_prev_identifier_pos = state->prev_identifier_pos;
    state->current_pos = (size_t)UINT32_MAX - 1u;
    ASSERT_EQ(nmo_chunk_write_identifier(chunk, 0xBBBB), NMO_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(state->current_pos, (size_t)UINT32_MAX - 1u);
    ASSERT_EQ(state->prev_identifier_pos, valid_prev_identifier_pos);
    ASSERT_EQ(chunk->data.count, 3);
    ASSERT_EQ(data[1], 0);

    state->current_pos = valid_pos;
    state->prev_identifier_pos = chunk->data.count - 1u;
    ASSERT_EQ(nmo_chunk_write_identifier(chunk, 0xBBBB), NMO_ERR_INVALID_STATE);
    ASSERT_EQ(state->current_pos, valid_pos);
    ASSERT_EQ(state->prev_identifier_pos, chunk->data.count - 1u);
    ASSERT_EQ(chunk->data.count, 3);
    ASSERT_EQ(data[1], 0);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, read_identifier_eof) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_start_write(chunk);
    nmo_chunk_write_identifier(chunk, 0xAAAA);
    nmo_chunk_write_int(chunk, 1234);
    nmo_chunk_close(chunk);

    nmo_chunk_start_read(chunk);
    nmo_status_t result = nmo_chunk_seek_identifier(chunk, 0xAAAA);
    ASSERT_EQ(result, NMO_OK);

    int32_t value = 0;
    result = nmo_chunk_read_int(chunk, &value);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(value, 1234);

    uint32_t identifier = 0;
    result = nmo_chunk_read_identifier(chunk, &identifier);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, manager_sequence) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Start write mode
    nmo_status_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);
    
    // Start manager sequence
    nmo_guid_t mgr_guid = {0x12345678, 0x9ABCDEF0};
    result = nmo_chunk_start_manager_sequence(chunk, mgr_guid, 3);
    ASSERT_EQ(result, NMO_OK);
    // Note: Can't directly access chunk_options, but functionality is verified by successful manager operations
    
    uint32_t entry_values[3] = {0xAABBCCDD, 0x11223344, 0x55667788};
    for (int i = 0; i < 3; ++i) {
        result = nmo_chunk_write_dword(chunk, entry_values[i]);
        ASSERT_EQ(result, NMO_OK);
    }
    
    // Start read mode
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result, NMO_OK);
    
    // Read manager sequence header
    nmo_guid_t read_guid;
    size_t count = 0;
    result = nmo_chunk_start_manager_read_sequence(chunk, &read_guid, &count);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(read_guid.d1, mgr_guid.d1);
    ASSERT_EQ(read_guid.d2, mgr_guid.d2);
    ASSERT_EQ(count, 3u);

    // Read manager sequence values
    uint32_t value;
    for (int i = 0; i < 3; ++i) {
        result = nmo_chunk_read_dword(chunk, &value);
        ASSERT_EQ(result, NMO_OK);
        ASSERT_EQ(value, entry_values[i]);
    }
    
    nmo_arena_destroy(arena);
}

TEST(chunk_api, manager_sequence_truncated_guid_keeps_position) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_write_dword(chunk, 3u); /* count only, missing guid */
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_guid_t manager_guid = {0xAAAAAAAAu, 0xBBBBBBBBu};
    size_t count = 123;
    result = nmo_chunk_start_manager_read_sequence(chunk, &manager_guid, &count);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);
    ASSERT_EQ(0u, manager_guid.d1);
    ASSERT_EQ(0u, manager_guid.d2);
    ASSERT_EQ(0u, count);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, sub_chunks) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);
    
    // Create sub-chunk
    nmo_chunk_t* sub = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(sub);
    nmo_status_t result = nmo_chunk_start_write(sub);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(sub, 0x12345678);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_string(sub, "SubChunkData");
    ASSERT_EQ(result, NMO_OK);
    
    // Create parent chunk
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);
    
    // Start sub-chunk sequence
    result = nmo_chunk_start_sub_chunk_sequence(chunk, 2);
    ASSERT_EQ(result, NMO_OK);
    // Note: Can't directly access chunk_options, but functionality is verified by successful sub-chunk operations
    
    // Write sub-chunks
    result = nmo_chunk_write_sub_chunk(chunk, sub);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_sub_chunk(chunk, sub);
    ASSERT_EQ(result, NMO_OK);
    
    // Start read mode
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result, NMO_OK);
    
    // Read sub-chunk count
    uint32_t count;
    result = nmo_chunk_read_dword(chunk, &count);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(count, 2);
    
    // Read first sub-chunk
    nmo_chunk_t* read_sub;
    result = nmo_chunk_read_sub_chunk(chunk, &read_sub);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_start_read(read_sub);
    ASSERT_EQ(result, NMO_OK);
    uint32_t dword;
    result = nmo_chunk_read_dword(read_sub, &dword);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(dword, 0x12345678);
    char* str;
    size_t str_len = nmo_chunk_read_string(read_sub, &str);
    ASSERT_GT(str_len, 0);
    ASSERT_STR_EQ(str, "SubChunkData");
    
    // Read second sub-chunk
    result = nmo_chunk_read_sub_chunk(chunk, &read_sub);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_start_read(read_sub);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_read_dword(read_sub, &dword);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(dword, 0x12345678);
    
    nmo_arena_destroy(arena);
}

TEST(chunk_api, sub_chunk_truncated_header) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);

    /* total_size says 5 DWORD payload follows, but it is absent. */
    result = nmo_chunk_write_dword(chunk, 5u);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_chunk_t* sub = (nmo_chunk_t*)1;
    result = nmo_chunk_read_sub_chunk(chunk, &sub);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_NULL(sub);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, sub_chunk_invalid_manager_count_keeps_position) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);

    /* total_size=7 => class/version/data/file/id/chunk/manager (no payload). */
    result = nmo_chunk_write_dword(chunk, 7u);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 0x10u); /* class */
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 0u); /* version_info */
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 0u); /* data_size */
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 0u); /* file_flag */
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 0u); /* id_count */
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 0u); /* chunk_count */
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 1u); /* invalid manager_count (expected 0) */
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_chunk_t* sub = (nmo_chunk_t*)1;
    result = nmo_chunk_read_sub_chunk(chunk, &sub);
    ASSERT_EQ(result, NMO_ERR_INVALID_FORMAT);
    ASSERT_NULL(sub);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, sub_chunk_reads_manager_count_independent_of_parent_version) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* sub = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(sub);

    nmo_status_t result = nmo_chunk_start_write(sub);
    ASSERT_EQ(result, NMO_OK);

    nmo_guid_t manager_guid = {0x12345678u, 0x9ABCDEF0u};
    result = nmo_chunk_write_manager_int(sub, manager_guid, 42u);
    ASSERT_EQ(result, NMO_OK);

    nmo_chunk_t* parent = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(parent);

    result = nmo_chunk_start_write(parent);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_start_sub_chunk_sequence(parent, 1);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_write_sub_chunk(parent, sub);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_start_read(parent);
    ASSERT_EQ(result, NMO_OK);

    uint32_t count = 0;
    result = nmo_chunk_read_dword(parent, &count);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(count, 1u);

    parent->chunk_version = 4u;

    nmo_chunk_t* read_sub = NULL;
    result = nmo_chunk_read_sub_chunk(parent, &read_sub);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_NOT_NULL(read_sub);
    ASSERT_EQ(read_sub->managers.count, 1u);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, arrays) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Start write mode
    nmo_status_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);
    
    // Write int array
    int int_array[] = {1, 2, 3, 4, 5};
    result = nmo_chunk_write_array(chunk, int_array, 5, sizeof(int));
    ASSERT_EQ(result, NMO_OK);
    
    // Write float array
    float float_array[] = {1.5f, 2.5f, 3.5f};
    result = nmo_chunk_write_array(chunk, float_array, 3, sizeof(float));
    ASSERT_EQ(result, NMO_OK);
    
    // Start read mode
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result, NMO_OK);
    
    // Read int array
    void* read_array;
    size_t count, elem_size;
    result = nmo_chunk_read_array(chunk, &read_array, &count, &elem_size);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(count, 5);
    ASSERT_EQ(elem_size, sizeof(int));
    int* int_ptr = (int*)read_array;
    for (size_t i = 0; i < 5; i++) {
        ASSERT_EQ(int_ptr[i], (int)(i + 1));
    }
    
    // Read float array
    result = nmo_chunk_read_array(chunk, &read_array, &count, &elem_size);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(count, 3);
    ASSERT_EQ(elem_size, sizeof(float));
    float* float_ptr = (float*)read_array;
    ASSERT_FLOAT_EQ(float_ptr[0], 1.5f, 0.001f);
    ASSERT_FLOAT_EQ(float_ptr[1], 2.5f, 0.001f);
    ASSERT_FLOAT_EQ(float_ptr[2], 3.5f, 0.001f);
    
    nmo_arena_destroy(arena);
}

TEST(chunk_api, arrays_reject_inconsistent_header) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_write_dword(chunk, 0u);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 2u);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result, NMO_OK);

    void* read_array = NULL;
    size_t count = 0;
    size_t elem_size = 0;
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);
    result = nmo_chunk_read_array(chunk, &read_array, &count, &elem_size);
    ASSERT_EQ(result, NMO_ERR_INVALID_FORMAT);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_write_dword(chunk, 8u);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 0u);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result, NMO_OK);

    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);
    result = nmo_chunk_read_array(chunk, &read_array, &count, &elem_size);
    ASSERT_EQ(result, NMO_ERR_INVALID_FORMAT);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_write_dword(chunk, 8u);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 2u);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_dword(chunk, 0xAABBCCDDu);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result, NMO_OK);

    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);
    result = nmo_chunk_read_array(chunk, &read_array, &count, &elem_size);
    ASSERT_EQ(result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_EQ(nmo_chunk_get_position(chunk), 0u);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, sub_chunk_write_rejects_unencodable_payload) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* sub = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(sub);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(sub));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(sub, 0xAABBCCDDu));
    nmo_chunk_close(sub);

    nmo_chunk_t* parent = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(parent);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(parent));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(parent, 0x12345678u));
    const uint32_t options_before = parent->chunk_options;

#if SIZE_MAX > UINT32_MAX
    const size_t data_count = sub->data.count;
    sub->data.count = (size_t)UINT32_MAX + 1u;
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_write_sub_chunk(parent, sub));
    sub->data.count = data_count;
    ASSERT_EQ(1u, nmo_chunk_get_position(parent));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(parent));
    ASSERT_EQ(0u, parent->chunks.count);
    ASSERT_EQ(0u, parent->chunk_refs.count);
    ASSERT_EQ(options_before, parent->chunk_options);
#endif

    nmo_arena_destroy(arena);
}

TEST(chunk_api, sub_chunk_tracking_failure_is_atomic) {
    chunk_api_fail_allocator_state_t allocator_state = {
        .allowed_allocations = (size_t)-1,
    };
    nmo_allocator_t allocator = nmo_allocator_custom(
        chunk_api_fail_alloc, chunk_api_fail_free, &allocator_state);
    nmo_arena_t* arena = nmo_arena_create(&allocator, 256);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* sub = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(sub);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(sub));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(sub, 0xAABBCCDDu));
    nmo_chunk_close(sub);

    nmo_chunk_t* parent = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(parent);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(parent));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(parent, 0x12345678u));
    const uint32_t parent_options_before = parent->chunk_options;
    const uint32_t sub_options_before = sub->chunk_options;
    const nmo_chunk_file_context_t* sub_context_before = sub->file_context;

    ASSERT_NOT_NULL(nmo_arena_alloc(arena, 100000, 16));
    allocator_state.allowed_allocations = allocator_state.allocation_count;
    ASSERT_EQ(NMO_ERR_NOMEM,
        nmo_chunk_write_sub_chunk(parent, sub));
    ASSERT_EQ(1u, nmo_chunk_get_position(parent));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(parent));
    ASSERT_EQ(0u, parent->chunks.count);
    ASSERT_EQ(0u, parent->chunk_refs.count);
    ASSERT_EQ(parent_options_before, parent->chunk_options);
    ASSERT_EQ(sub_options_before, sub->chunk_options);
    ASSERT_TRUE(sub_context_before == sub->file_context);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, sub_chunk_sequence_header_truncation_is_atomic) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 2));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));

    size_t count = 123;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
        nmo_chunk_start_read_sub_chunk_sequence(chunk, &count));
    ASSERT_EQ(0u, count);
    ASSERT_EQ(0u, nmo_chunk_get_position(chunk));

    nmo_arena_destroy(arena);
}

TEST(chunk_api, sub_chunk_sequence_write_failure_is_atomic) {
    chunk_api_fail_allocator_state_t allocator_state = {
        .allowed_allocations = (size_t)-1,
    };
    nmo_allocator_t allocator = nmo_allocator_custom(
        chunk_api_fail_alloc, chunk_api_fail_free, &allocator_state);
    nmo_arena_t* arena = nmo_arena_create(&allocator, 256);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0x12345678u));
    const uint32_t options_before = chunk->chunk_options;

#if SIZE_MAX > UINT32_MAX
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_start_sub_chunk_sequence(chunk, SIZE_MAX));
#endif
    ASSERT_NOT_NULL(nmo_arena_alloc(arena, 100000, 16));
    allocator_state.allowed_allocations = allocator_state.allocation_count;
    ASSERT_EQ(NMO_ERR_NOMEM,
        nmo_chunk_start_sub_chunk_sequence(chunk, 1));
    ASSERT_EQ(1u, nmo_chunk_get_position(chunk));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(chunk));
    ASSERT_EQ(0u, chunk->chunk_refs.count);
    ASSERT_EQ(options_before, chunk->chunk_options);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, manager_sequence_write_failure_is_atomic) {
    chunk_api_fail_allocator_state_t allocator_state = {
        .allowed_allocations = (size_t)-1,
    };
    nmo_allocator_t allocator = nmo_allocator_custom(
        chunk_api_fail_alloc, chunk_api_fail_free, &allocator_state);
    nmo_arena_t* arena = nmo_arena_create(&allocator, 256);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0x12345678u));

    const nmo_guid_t manager_guid = {0x11111111u, 0x22222222u};
#if SIZE_MAX > UINT32_MAX
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_start_manager_sequence(chunk, manager_guid, SIZE_MAX));
#endif
    ASSERT_EQ(NMO_OK,
        nmo_chunk_start_manager_sequence(chunk, manager_guid, 1));
    while (chunk->managers.count < chunk->managers.capacity) {
        ASSERT_EQ(NMO_OK,
            nmo_chunk_start_manager_sequence(chunk, manager_guid, 1));
    }
    const size_t position_before = nmo_chunk_get_position(chunk);
    const size_t data_size_before = nmo_chunk_get_data_size(chunk);
    const size_t managers_count_before = chunk->managers.count;
    const uint32_t options_before = chunk->chunk_options;
    ASSERT_NOT_NULL(nmo_arena_alloc(arena, 100000, 16));
    allocator_state.allowed_allocations = allocator_state.allocation_count;
    ASSERT_EQ(NMO_ERR_NOMEM,
        nmo_chunk_start_manager_sequence(chunk, manager_guid, 1));
    ASSERT_EQ(position_before, nmo_chunk_get_position(chunk));
    ASSERT_EQ(data_size_before, nmo_chunk_get_data_size(chunk));
    ASSERT_EQ(managers_count_before, chunk->managers.count);
    ASSERT_EQ(options_before, chunk->chunk_options);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, array_truncated_header_clears_outputs) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 8u));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));

    void* read_array = (void*)1;
    size_t count = 123;
    size_t elem_size = 456;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
        nmo_chunk_read_array(chunk, &read_array, &count, &elem_size));
    ASSERT_EQ(0u, nmo_chunk_get_position(chunk));
    ASSERT_NULL(read_array);
    ASSERT_EQ(0u, count);
    ASSERT_EQ(0u, elem_size);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, typed_array_writes_reject_unencodable_counts) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0x12345678u));

    const size_t count = (size_t)INT32_MAX + 1u;
    const int32_t int_value = 1;
    const float float_value = 1.0f;
    const uint32_t dword_value = 1;
    const uint8_t byte_value = 1;
    const char* string_value = "x";
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_write_int_array(chunk, &int_value, count));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_write_float_array(chunk, &float_value, count));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_write_dword_array(chunk, &dword_value, count));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_write_byte_array(chunk, &byte_value, count));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_write_string_array(chunk, &string_value, count));
    ASSERT_EQ(1u, nmo_chunk_get_position(chunk));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(chunk));

    nmo_arena_destroy(arena);
}

TEST(chunk_api, generic_array_write_rejects_invalid_sizes) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0x12345678u));

    const uint8_t value = 1;
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_write_array(
            chunk, &value, (size_t)INT_MAX + 1u, 1));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_write_array(
            chunk, &value, 1, (size_t)INT_MAX + 1u));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_write_array(chunk, &value, (size_t)INT_MAX, 2));
    ASSERT_EQ(1u, nmo_chunk_get_position(chunk));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(chunk));

    nmo_arena_destroy(arena);
}

TEST(chunk_api, generic_array_allocation_failure_is_atomic) {
    chunk_api_fail_allocator_state_t allocator_state = {
        .allowed_allocations = (size_t)-1,
    };
    nmo_allocator_t allocator = nmo_allocator_custom(
        chunk_api_fail_alloc, chunk_api_fail_free, &allocator_state);
    nmo_arena_t* arena = nmo_arena_create(&allocator, 256);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0x12345678u));

    uint8_t values[3000];
    memset(values, 0xA5, sizeof(values));
    allocator_state.allowed_allocations = allocator_state.allocation_count;
    ASSERT_EQ(NMO_ERR_NOMEM,
        nmo_chunk_write_array(chunk, values, sizeof(values), 1));
    ASSERT_EQ(1u, nmo_chunk_get_position(chunk));
    ASSERT_EQ(4u, nmo_chunk_get_data_size(chunk));

    nmo_arena_destroy(arena);
}

TEST(chunk_api, typed_array_allocation_failure_is_atomic) {
    chunk_api_fail_allocator_state_t allocator_state = {
        .allowed_allocations = (size_t)-1,
    };
    nmo_allocator_t allocator = nmo_allocator_custom(
        chunk_api_fail_alloc, chunk_api_fail_free, &allocator_state);
    nmo_arena_t* arena = nmo_arena_create(&allocator, 256);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 600));
    for (int32_t i = 0; i < 600; ++i) {
        ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, i));
    }
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));

    allocator_state.allowed_allocations = allocator_state.allocation_count;
    int32_t* array = (int32_t*)1;
    size_t count = 123;
    ASSERT_EQ(NMO_ERR_NOMEM,
        nmo_chunk_read_int_array(chunk, &array, &count, arena));
    ASSERT_EQ(0u, nmo_chunk_get_position(chunk));
    ASSERT_NULL(array);
    ASSERT_EQ(0u, count);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, string_array_truncation_is_atomic) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_int(chunk, 1));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 8));
    ASSERT_EQ(NMO_OK, nmo_chunk_write_dword(chunk, 0xAABBCCDDu));
    nmo_chunk_close(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(chunk));

    char** strings = (char**)1;
    size_t count = 123;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
        nmo_chunk_read_string_array(chunk, &strings, &count, arena));
    ASSERT_EQ(0u, nmo_chunk_get_position(chunk));
    ASSERT_NULL(strings);
    ASSERT_EQ(0u, count);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, compression) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Start write mode
    nmo_status_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);
    
    // Write repetitive data (compresses well)
    for (int i = 0; i < 100; i++) {
        result = nmo_chunk_write_int(chunk, 0x12345678);
        ASSERT_EQ(result, NMO_OK);
    }
    
    size_t original_size = nmo_chunk_get_data_size(chunk) / 4; // Convert bytes to DWORDs
    
    // Compress
    result = nmo_chunk_compress(chunk, 6);
    ASSERT_EQ(result, NMO_OK);
    // Note: Can't directly access chunk_options or unpack_size
    // Verify compression worked by checking that data size changed
    size_t packed_size = nmo_chunk_get_data_size(chunk) / 4; // Convert bytes to DWORDs
    ASSERT_LT(packed_size, original_size); // Should compress
    
    // Decompress
    result = nmo_chunk_decompress(chunk);
    ASSERT_EQ(result, NMO_OK);
    // Note: Can't directly access chunk_options
    // Verify unpack worked by checking data size is restored
    ASSERT_EQ(nmo_chunk_get_data_size(chunk) / 4, original_size); // Convert bytes to DWORDs
    
    // Verify data integrity
    result = nmo_chunk_start_read(chunk);
    ASSERT_EQ(result, NMO_OK);
    for (int i = 0; i < 100; i++) {
        int32_t value;
        result = nmo_chunk_read_int(chunk, &value);
        ASSERT_EQ(result, NMO_OK);
        ASSERT_EQ(value, 0x12345678);
    }
    
    nmo_arena_destroy(arena);
}

TEST(chunk_api, compression_new_api) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 32);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);

    for (int i = 0; i < 128; ++i) {
        result = nmo_chunk_write_int(chunk, 0x11111111);
        ASSERT_EQ(result, NMO_OK);
    }

    size_t original_bytes = nmo_chunk_get_data_size(chunk);
    ASSERT_GT(original_bytes, 0U);

    result = nmo_chunk_compress(chunk, 6);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_LT(nmo_chunk_get_data_size(chunk), original_bytes);

    result = nmo_chunk_decompress(chunk);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(nmo_chunk_get_data_size(chunk), original_bytes);

    result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);
    for (int i = 0; i < 64; ++i) {
        result = nmo_chunk_write_int(chunk, i);
        ASSERT_EQ(result, NMO_OK);
    }

    size_t noisy_bytes = nmo_chunk_get_data_size(chunk);
    ASSERT_GT(noisy_bytes, 0U);

    // Use an extremely small ratio to guarantee compression is skipped
    result = nmo_chunk_compress_if_beneficial(chunk, 6, 0.01f);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(nmo_chunk_get_data_size(chunk), noisy_bytes);

    nmo_arena_destroy(arena);
}

TEST(chunk_api, crc) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 1024 * 16);
    ASSERT_NOT_NULL(arena);
    
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    
    // Start write mode
    nmo_status_t result = nmo_chunk_start_write(chunk);
    ASSERT_EQ(result, NMO_OK);
    
    // Write test data
    result = nmo_chunk_write_int(chunk, 0x11111111);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_int(chunk, 0x22222222);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_write_int(chunk, 0x33333333);
    ASSERT_EQ(result, NMO_OK);
    
    // Compute CRC
    uint32_t crc;
    result = nmo_chunk_compute_crc(chunk, 1, &crc);
    ASSERT_EQ(result, NMO_OK);
    
    // CRC should be deterministic
    uint32_t crc2;
    result = nmo_chunk_compute_crc(chunk, 1, &crc2);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(crc, crc2);
    
    // Different data should give different CRC
    result = nmo_chunk_write_int(chunk, 0x44444444);
    ASSERT_EQ(result, NMO_OK);
    uint32_t crc3;
    result = nmo_chunk_compute_crc(chunk, 1, &crc3);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_NE(crc3, crc);
    
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_api, primitives);
    REGISTER_TEST(chunk_api, string);
    REGISTER_TEST(chunk_api, string_truncated_payload_keeps_position);
    REGISTER_TEST(chunk_api, buffer);
    REGISTER_TEST(chunk_api, buffer_truncated_payload);
    REGISTER_TEST(chunk_api, buffer_fill_errors_keep_position);
    REGISTER_TEST(chunk_api, buffer_null_data_writes_zero_size);
    REGISTER_TEST(chunk_api, buffer_no_size_rejects_null_data_with_size);
    REGISTER_TEST(chunk_api, buffer_writes_reject_size_overflow_atomically);
    REGISTER_TEST(chunk_api, guid);
    REGISTER_TEST(chunk_api, object_id);
    REGISTER_TEST(chunk_api, sequence);
    REGISTER_TEST(chunk_api, sequence_rejects_negative_count);
    REGISTER_TEST(chunk_api, sequence_rejects_truncated_items);
    REGISTER_TEST(chunk_api, sequence_write_rejects_unencodable_count);
    REGISTER_TEST(chunk_api, navigation);
    REGISTER_TEST(chunk_api, navigation_read_bounds);
    REGISTER_TEST(chunk_api, check_size_rounds_and_rejects_overflow);
    REGISTER_TEST(chunk_api, auto_expand);
    REGISTER_TEST(chunk_api, identifiers);
    REGISTER_TEST(chunk_api, identifier_write_validation_is_atomic);
    REGISTER_TEST(chunk_api, read_identifier_eof);
    REGISTER_TEST(chunk_api, manager_sequence);
    REGISTER_TEST(chunk_api, manager_sequence_truncated_guid_keeps_position);
    REGISTER_TEST(chunk_api, manager_sequence_write_failure_is_atomic);
    REGISTER_TEST(chunk_api, sub_chunks);
    REGISTER_TEST(chunk_api, sub_chunk_write_rejects_unencodable_payload);
    REGISTER_TEST(chunk_api, sub_chunk_tracking_failure_is_atomic);
    REGISTER_TEST(chunk_api, sub_chunk_sequence_header_truncation_is_atomic);
    REGISTER_TEST(chunk_api, sub_chunk_sequence_write_failure_is_atomic);
    REGISTER_TEST(chunk_api, sub_chunk_truncated_header);
    REGISTER_TEST(chunk_api, sub_chunk_invalid_manager_count_keeps_position);
    REGISTER_TEST(chunk_api, sub_chunk_reads_manager_count_independent_of_parent_version);
    REGISTER_TEST(chunk_api, arrays);
    REGISTER_TEST(chunk_api, arrays_reject_inconsistent_header);
    REGISTER_TEST(chunk_api, array_truncated_header_clears_outputs);
    REGISTER_TEST(chunk_api, typed_array_writes_reject_unencodable_counts);
    REGISTER_TEST(chunk_api, generic_array_write_rejects_invalid_sizes);
    REGISTER_TEST(chunk_api, generic_array_allocation_failure_is_atomic);
    REGISTER_TEST(chunk_api, typed_array_allocation_failure_is_atomic);
    REGISTER_TEST(chunk_api, string_array_truncation_is_atomic);
    REGISTER_TEST(chunk_api, compression);
    REGISTER_TEST(chunk_api, compression_new_api);
    REGISTER_TEST(chunk_api, crc);
TEST_MAIN_END()
