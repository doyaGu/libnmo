/**
 * @file test_chunk_parser.c
 * @brief Unit tests for chunk parser
 */

#include "../test_framework.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_parser.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdint.h>

typedef struct parser_fail_allocator_state {
    int fail_allocations;
} parser_fail_allocator_state_t;

static void *parser_fail_alloc(
    void *user_data, size_t size, size_t alignment)
{
    parser_fail_allocator_state_t *state =
        (parser_fail_allocator_state_t *)user_data;
    if (state->fail_allocations) return NULL;
    nmo_allocator_t allocator = nmo_allocator_default();
    return nmo_alloc(&allocator, size, alignment);
}

static void parser_fail_free(void *user_data, void *ptr)
{
    (void)user_data;
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_free(&allocator, ptr);
}

TEST(chunk_parser, create_destroy) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, cursor_operations) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    // Create chunk with some data
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    // Add 10 DWORDs of data
    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 10);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Test tell/seek/skip
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0);

    nmo_status_t parse_result = nmo_chunk_parser_seek(parser, 5);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 5);

    parse_result = nmo_chunk_parser_skip(parser, 3);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 8);

    ASSERT_EQ(nmo_chunk_parser_remaining(parser), 2);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, primitive_reads) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    // Create test data
    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 10);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);

    data[0] = 0x12345678;  // For byte/word/dword tests
    data[1] = 0xDEADBEEF;  // For int test
    float test_float = 3.14159f;
    memcpy(&data[2], &test_float, sizeof(float));  // For float test
    data[3] = 0x11111111;  // GUID part 1
    data[4] = 0x22222222;  // GUID part 2

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Test byte read
    uint8_t byte_val;
    nmo_status_t parse_result = nmo_chunk_parser_read_byte(parser, &byte_val);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(byte_val, 0x78);

    // Reset for word test
    parse_result = nmo_chunk_parser_seek(parser, 0);
    ASSERT_EQ(parse_result, NMO_OK);
    uint16_t word_val;
    parse_result = nmo_chunk_parser_read_word(parser, &word_val);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(word_val, 0x5678);

    // Reset for dword test
    parse_result = nmo_chunk_parser_seek(parser, 0);
    ASSERT_EQ(parse_result, NMO_OK);
    uint32_t dword_val;
    parse_result = nmo_chunk_parser_read_dword(parser, &dword_val);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(dword_val, 0x12345678);

    // Test int read
    int32_t int_val;
    parse_result = nmo_chunk_parser_read_int(parser, &int_val);
    ASSERT_EQ(parse_result, NMO_OK);

    // Test float read
    float float_val;
    parse_result = nmo_chunk_parser_read_float(parser, &float_val);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_TRUE(float_val >= 3.14f && float_val <= 3.15f);

    // Test GUID read
    nmo_guid_t guid_val;
    parse_result = nmo_chunk_parser_read_guid(parser, &guid_val);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(guid_val.d1, 0x11111111);
    ASSERT_EQ(guid_val.d2, 0x22222222);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, string_read) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    // Create string data: [size][data padded to DWORD]
    // CK2 format: size includes null terminator
    const char* test_str = "Hello";
    uint32_t str_len = (uint32_t)strlen(test_str);
    uint32_t str_size = str_len + 1;  // Include null terminator

    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 3);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);

    data[0] = str_size;  // Size includes null terminator
    memcpy(&data[1], test_str, str_size);  // Copy including null terminator

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    char* read_str = (char*)(uintptr_t)1;
    nmo_status_t parse_result = nmo_chunk_parser_read_string(parser, &read_str, arena);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_NOT_NULL(read_str);
    ASSERT_TRUE(strcmp(read_str, test_str) == 0);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, string_truncated_keeps_position) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 2);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);

    data[0] = 8u;          // string size includes null terminator
    data[1] = 0xAABBCCDDu; // only 4 bytes payload provided, need 8

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0u);

    char* read_str = NULL;
    nmo_status_t parse_result = nmo_chunk_parser_read_string(parser, &read_str, arena);
    ASSERT_EQ(parse_result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_NULL(read_str);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0u);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, unterminated_string_keeps_position) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_arena_array_resize(&chunk->data, 2u));
    uint32_t* data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);
    data[0] = 4u;
    data[1] = 0x44434241u;

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);
    char* out = (char*)1;
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT,
        nmo_chunk_parser_read_string(parser, &out, arena));
    ASSERT_NULL(out);
    ASSERT_EQ(0u, nmo_chunk_parser_tell(parser));

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, object_sequence_state) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 6);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);

    data[0] = 3;           // sequence count
    data[1] = 101;         // id #1
    data[2] = 202;         // id #2
    data[3] = 303;         // id #3
    data[4] = 0xDEADBEEF;  // sentinel after sequence
    data[5] = 0x01020304;  // trailing data for further reads

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    size_t count = 0;
    nmo_status_t parse_result = nmo_chunk_parser_start_object_sequence(parser, &count);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(3u, count);

    nmo_object_id_t obj_id = 0;
    parse_result = nmo_chunk_parser_read_object_id(parser, &obj_id);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ((nmo_object_id_t)101, obj_id);
    parse_result = nmo_chunk_parser_read_object_id(parser, &obj_id);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ((nmo_object_id_t)202, obj_id);
    parse_result = nmo_chunk_parser_read_object_id(parser, &obj_id);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ((nmo_object_id_t)303, obj_id);

    uint32_t sentinel = 0;
    parse_result = nmo_chunk_parser_read_dword(parser, &sentinel);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(0xDEADBEEF, sentinel);

    uint32_t tail = 0;
    parse_result = nmo_chunk_parser_read_dword(parser, &tail);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(0x01020304, tail);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, legacy_object_id_truncated_keeps_position) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    chunk->chunk_version = NMO_CHUNK_VERSION1 - 1;

    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 1);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);
    data[0] = 1u; /* non-zero legacy flag requires 3 extra DWORDs, missing */

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0u);

    nmo_object_id_t obj_id = 0;
    nmo_status_t parse_result = nmo_chunk_parser_read_object_id(parser, &obj_id);
    ASSERT_EQ(parse_result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0u);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, manager_sequence_state) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 6);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);

    nmo_guid_t guid = {0xAAAAAAAA, 0xBBBBBBBB};
    data[0] = 2;             // sequence count
    data[1] = guid.d1;
    data[2] = guid.d2;
    data[3] = 0x11111111;    // entry #1
    data[4] = 0x22222222;    // entry #2
    data[5] = 0x33333333;    // trailing payload

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    nmo_guid_t header_guid;
    size_t count = 0;
    nmo_status_t parse_result = nmo_chunk_parser_start_manager_sequence(parser, &header_guid, &count);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(2u, count);
    ASSERT_EQ(guid.d1, header_guid.d1);
    ASSERT_EQ(guid.d2, header_guid.d2);

    int32_t value = 0;
    parse_result = nmo_chunk_parser_read_manager_int_sequence(parser, &value);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(0x11111111, value);
    parse_result = nmo_chunk_parser_read_manager_int_sequence(parser, &value);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(0x22222222, value);

    uint32_t tail = 0;
    parse_result = nmo_chunk_parser_read_dword(parser, &tail);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(0x33333333, tail);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, manager_sequence_truncated_guid_keeps_position) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 1);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);
    data[0] = 2u; /* count only; GUID is missing */

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0u);

    nmo_guid_t guid = {0u, 0u};
    size_t count = 0;
    nmo_status_t parse_result = nmo_chunk_parser_start_manager_sequence(parser, &guid, &count);
    ASSERT_EQ(parse_result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0u);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, subchunk_truncated_header_keeps_position) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 1);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);
    data[0] = 5u; /* declares payload/header dwords that are not present */

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0u);

    nmo_chunk_t *sub = (nmo_chunk_t *)1;
    nmo_status_t parse_result = nmo_chunk_parser_read_subchunk(parser, arena, &sub);
    ASSERT_EQ(parse_result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_NULL(sub);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0u);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, subchunk_invalid_manager_count_keeps_position) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 8);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);

    data[0] = 7u;   /* size_dwords */
    data[1] = 0x10; /* class_id */
    data[2] = 0u;   /* version_info */
    data[3] = 0u;   /* chunk_size */
    data[4] = 0u;   /* has_file */
    data[5] = 0u;   /* id_count */
    data[6] = 0u;   /* chunk_count */
    data[7] = 1u;   /* invalid manager_count (expected 0) */

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0u);

    nmo_chunk_t *sub = (nmo_chunk_t *)1;
    nmo_status_t parse_result = nmo_chunk_parser_read_subchunk(parser, arena, &sub);
    ASSERT_EQ(parse_result, NMO_ERR_INVALID_FORMAT);
    ASSERT_NULL(sub);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0u);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, identifier_navigation) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    // Create a data buffer with a linked list of identifiers
    // [ID, NextPos]
    // Pos 0: [0xID1, 4]
    // Pos 2: [payload]
    // Pos 4: [0xID2, 8]
    // Pos 6: [payload]
    // Pos 8: [0xID3, 0]
    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 10);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);
    memset(data, 0, 10 * sizeof(uint32_t));
    data[0] = 0x1D1D1D1D;
    data[1] = 4;
    data[4] = 0x2D2D2D2D;
    data[5] = 8;
    data[8] = 0x3D3D3D3D;
    data[9] = 0; // End of list

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Read the first identifier to set the initial state
    uint32_t id;
    nmo_status_t parse_result = nmo_chunk_parser_read_identifier(parser, &id);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(id, 0x1D1D1D1D);

    // Seek to the third identifier (0x3D3D3D3D)
    parse_result = nmo_chunk_parser_seek_identifier(parser, 0x3D3D3D3D);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 10); // Cursor should be after the [ID, NextPos] pair

    // Try to seek a non-existent ID
    parse_result = nmo_chunk_parser_seek_identifier(parser, 0xBADBAD);
    ASSERT_EQ(parse_result, NMO_ERR_TRUNCATED_CHUNK);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, bounds_checking) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    // Create chunk with 1 DWORD
    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 1);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);
    data[0] = 0x12345678;

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // First read should succeed
    uint32_t val;
    nmo_status_t parse_result = nmo_chunk_parser_read_dword(parser, &val);
    ASSERT_EQ(parse_result, NMO_OK);

    // Second read should fail (EOF)
    parse_result = nmo_chunk_parser_read_dword(parser, &val);
    ASSERT_EQ(parse_result, NMO_ERR_TRUNCATED_CHUNK);

    // at_end should return true
    ASSERT_TRUE(nmo_chunk_parser_at_end(parser));

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, array_lendian_overflow) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 2);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);

    data[0] = UINT32_MAX; // data_size_bytes
    data[1] = 1;          // element_count

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    void *array = NULL;
    size_t count = 0;
    nmo_status_t parse_result = nmo_chunk_parser_read_array_lendian(parser, &array, &count, arena);

    if (SIZE_MAX == UINT32_MAX) {
        ASSERT_EQ(parse_result, NMO_ERR_INVALID_FORMAT);
    } else {
        ASSERT_EQ(parse_result, NMO_ERR_TRUNCATED_CHUNK);
    }
    ASSERT_NULL(array);
    ASSERT_EQ(count, 0u);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0u);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, array_lendian_truncated_keeps_position) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 3);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);

    data[0] = 8u;          // data_size_bytes
    data[1] = 2u;          // element_count
    data[2] = 0xAABBCCDDu; // only 4 bytes payload provided

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0u);

    void *array = NULL;
    size_t count = 0;
    nmo_status_t parse_result = nmo_chunk_parser_read_array_lendian(parser, &array, &count, arena);
    ASSERT_EQ(parse_result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_NULL(array);
    ASSERT_EQ(count, 0u);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0u);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, array_lendian_rejects_inconsistent_header) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_arena_array_resize(&chunk->data, 2));
    uint32_t* data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);
    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    const uint32_t headers[][2] = {
        {0u, 5u},
        {16u, 0u},
    };
    for (size_t i = 0; i < sizeof(headers) / sizeof(headers[0]); ++i) {
        data[0] = headers[i][0];
        data[1] = headers[i][1];
        void* array = (void*)(uintptr_t)1;
        size_t count = 123;
        ASSERT_EQ(NMO_ERR_INVALID_FORMAT,
            nmo_chunk_parser_read_array_lendian(
                parser, &array, &count, arena));
        ASSERT_NULL(array);
        ASSERT_EQ(0u, count);
        ASSERT_EQ(0u, nmo_chunk_parser_tell(parser));
    }

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, buffer_truncated_keeps_position) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 2);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);

    data[0] = 8u;          // buffer size
    data[1] = 0xAABBCCDDu; // only 4 bytes payload

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0u);

    void *buffer = (void*)(uintptr_t)1;
    size_t size = 123;
    nmo_status_t parse_result = nmo_chunk_parser_read_buffer(parser, &buffer, &size, arena);
    ASSERT_EQ(parse_result, NMO_ERR_TRUNCATED_CHUNK);
    ASSERT_NULL(buffer);
    ASSERT_EQ(size, 0u);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0u);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, malformed_lengths_are_checked_before_allocation) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_arena_array_resize(&chunk->data, 1u));
    uint32_t* data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);
    data[0] = UINT32_MAX;

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    parser_fail_allocator_state_t allocator_state = {0};
    nmo_allocator_t allocator = nmo_allocator_custom(
        parser_fail_alloc, parser_fail_free, &allocator_state);
    nmo_arena_t* output_arena = nmo_arena_create(&allocator, 64);
    ASSERT_NOT_NULL(output_arena);
    allocator_state.fail_allocations = 1;

    char* string = (char*)(uintptr_t)1;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
        nmo_chunk_parser_read_string(parser, &string, output_arena));
    ASSERT_NULL(string);
    ASSERT_EQ(0u, nmo_chunk_parser_tell(parser));

    void* buffer = (void*)(uintptr_t)1;
    size_t buffer_size = 123;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
        nmo_chunk_parser_read_buffer(
            parser, &buffer, &buffer_size, output_arena));
    ASSERT_NULL(buffer);
    ASSERT_EQ(0u, buffer_size);
    ASSERT_EQ(0u, nmo_chunk_parser_tell(parser));

    nmo_arena_destroy(output_arena);
    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, failed_pointer_reads_clear_outputs) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    char* string = (char*)(uintptr_t)1;
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_parser_read_string(NULL, &string, arena));
    ASSERT_NULL(string);

    void* buffer = (void*)(uintptr_t)1;
    size_t buffer_size = 123;
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_parser_read_buffer(NULL, &buffer, &buffer_size, arena));
    ASSERT_NULL(buffer);
    ASSERT_EQ(0u, buffer_size);

    nmo_chunk_t* sub = (nmo_chunk_t*)(uintptr_t)1;
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
        nmo_chunk_parser_read_subchunk(NULL, arena, &sub));
    ASSERT_NULL(sub);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_arena_array_resize(&chunk->data, 1u));
    uint32_t* data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);
    data[0] = 0u;

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);
    size_t count = 123;
    ASSERT_EQ(NMO_OK,
        nmo_chunk_parser_start_read_sequence(parser, &count));
    ASSERT_EQ(0u, count);

    sub = (nmo_chunk_t*)(uintptr_t)1;
    ASSERT_EQ(NMO_ERR_TRUNCATED_CHUNK,
        nmo_chunk_parser_read_subchunk(parser, arena, &sub));
    ASSERT_NULL(sub);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_parser, create_destroy);
    REGISTER_TEST(chunk_parser, cursor_operations);
    REGISTER_TEST(chunk_parser, primitive_reads);
    REGISTER_TEST(chunk_parser, string_read);
    REGISTER_TEST(chunk_parser, string_truncated_keeps_position);
    REGISTER_TEST(chunk_parser, unterminated_string_keeps_position);
    REGISTER_TEST(chunk_parser, object_sequence_state);
    REGISTER_TEST(chunk_parser, legacy_object_id_truncated_keeps_position);
    REGISTER_TEST(chunk_parser, manager_sequence_state);
    REGISTER_TEST(chunk_parser, manager_sequence_truncated_guid_keeps_position);
    REGISTER_TEST(chunk_parser, subchunk_truncated_header_keeps_position);
    REGISTER_TEST(chunk_parser, subchunk_invalid_manager_count_keeps_position);
    REGISTER_TEST(chunk_parser, identifier_navigation);
    REGISTER_TEST(chunk_parser, bounds_checking);
    REGISTER_TEST(chunk_parser, array_lendian_overflow);
    REGISTER_TEST(chunk_parser, array_lendian_truncated_keeps_position);
    REGISTER_TEST(chunk_parser, array_lendian_rejects_inconsistent_header);
    REGISTER_TEST(chunk_parser, buffer_truncated_keeps_position);
    REGISTER_TEST(chunk_parser, malformed_lengths_are_checked_before_allocation);
    REGISTER_TEST(chunk_parser, failed_pointer_reads_clear_outputs);
TEST_MAIN_END()
