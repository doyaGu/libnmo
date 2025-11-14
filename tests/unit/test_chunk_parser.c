/**
 * @file test_chunk_parser.c
 * @brief Unit tests for chunk parser
 */

#include "../test_framework.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_parser.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <stdio.h>

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
    chunk->data_size = 10;
    chunk->data = (uint32_t*)nmo_arena_alloc(arena, 10 * sizeof(uint32_t), sizeof(uint32_t));
    ASSERT_NOT_NULL(chunk->data);

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Test tell/seek/skip
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 0);

    ASSERT_EQ(nmo_chunk_parser_seek(parser, 5), NMO_OK);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 5);

    ASSERT_EQ(nmo_chunk_parser_skip(parser, 3), NMO_OK);
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
    chunk->data_size = 10;
    chunk->data = (uint32_t*)nmo_arena_alloc(arena, 10 * sizeof(uint32_t), sizeof(uint32_t));
    ASSERT_NOT_NULL(chunk->data);

    chunk->data[0] = 0x12345678;  // For byte/word/dword tests
    chunk->data[1] = 0xDEADBEEF;  // For int test
    float test_float = 3.14159f;
    memcpy(&chunk->data[2], &test_float, sizeof(float));  // For float test
    chunk->data[3] = 0x11111111;  // GUID part 1
    chunk->data[4] = 0x22222222;  // GUID part 2

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Test byte read
    uint8_t byte_val;
    ASSERT_EQ(nmo_chunk_parser_read_byte(parser, &byte_val), NMO_OK);
    ASSERT_EQ(byte_val, 0x78);

    // Reset for word test
    nmo_chunk_parser_seek(parser, 0);
    uint16_t word_val;
    ASSERT_EQ(nmo_chunk_parser_read_word(parser, &word_val), NMO_OK);
    ASSERT_EQ(word_val, 0x5678);

    // Reset for dword test
    nmo_chunk_parser_seek(parser, 0);
    uint32_t dword_val;
    ASSERT_EQ(nmo_chunk_parser_read_dword(parser, &dword_val), NMO_OK);
    ASSERT_EQ(dword_val, 0x12345678);

    // Test int read
    int32_t int_val;
    ASSERT_EQ(nmo_chunk_parser_read_int(parser, &int_val), NMO_OK);

    // Test float read
    float float_val;
    ASSERT_EQ(nmo_chunk_parser_read_float(parser, &float_val), NMO_OK);
    ASSERT_TRUE(float_val >= 3.14f && float_val <= 3.15f);

    // Test GUID read
    nmo_guid_t guid_val;
    ASSERT_EQ(nmo_chunk_parser_read_guid(parser, &guid_val), NMO_OK);
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

    // Create string data: [length][data padded to DWORD]
    const char* test_str = "Hello";
    uint32_t str_len = (uint32_t)strlen(test_str);

    chunk->data_size = 3;  // 1 for length + 2 for "Hello" (5 bytes -> 2 DWORDs)
    chunk->data = (uint32_t*)nmo_arena_alloc(arena, 3 * sizeof(uint32_t), sizeof(uint32_t));
    ASSERT_NOT_NULL(chunk->data);

    chunk->data[0] = str_len;
    memcpy(&chunk->data[1], test_str, str_len);

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    char* read_str = NULL;
    ASSERT_EQ(nmo_chunk_parser_read_string(parser, &read_str, arena), NMO_OK);
    ASSERT_NOT_NULL(read_str);
    ASSERT_TRUE(strcmp(read_str, test_str) == 0);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, object_sequence_state) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    chunk->data_size = 6;
    chunk->data = (uint32_t*)nmo_arena_alloc(arena, chunk->data_size * sizeof(uint32_t), sizeof(uint32_t));
    ASSERT_NOT_NULL(chunk->data);

    chunk->data[0] = 3;           // sequence count
    chunk->data[1] = 101;         // id #1
    chunk->data[2] = 202;         // id #2
    chunk->data[3] = 303;         // id #3
    chunk->data[4] = 0xDEADBEEF;  // sentinel after sequence
    chunk->data[5] = 0x01020304;  // trailing data for further reads

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    int count = nmo_chunk_parser_start_object_sequence(parser);
    ASSERT_EQ(3, count);

    nmo_object_id_t obj_id = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_object_id(parser, &obj_id));
    ASSERT_EQ((nmo_object_id_t)101, obj_id);
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_object_id(parser, &obj_id));
    ASSERT_EQ((nmo_object_id_t)202, obj_id);
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_object_id(parser, &obj_id));
    ASSERT_EQ((nmo_object_id_t)303, obj_id);

    uint32_t sentinel = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_dword(parser, &sentinel));
    ASSERT_EQ(0xDEADBEEF, sentinel);

    uint32_t tail = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_dword(parser, &tail));
    ASSERT_EQ(0x01020304, tail);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, manager_sequence_state) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    chunk->data_size = 6;
    chunk->data = (uint32_t*)nmo_arena_alloc(arena, chunk->data_size * sizeof(uint32_t), sizeof(uint32_t));
    ASSERT_NOT_NULL(chunk->data);

    nmo_guid_t guid = {0xAAAAAAAA, 0xBBBBBBBB};
    chunk->data[0] = 2;             // sequence count
    chunk->data[1] = guid.d1;
    chunk->data[2] = guid.d2;
    chunk->data[3] = 0x11111111;    // entry #1
    chunk->data[4] = 0x22222222;    // entry #2
    chunk->data[5] = 0x33333333;    // trailing payload

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    nmo_guid_t header_guid;
    int count = nmo_chunk_parser_start_manager_sequence(parser, &header_guid);
    ASSERT_EQ(2, count);
    ASSERT_EQ(guid.d1, header_guid.d1);
    ASSERT_EQ(guid.d2, header_guid.d2);

    int32_t value = nmo_chunk_parser_read_manager_int_sequence(parser);
    ASSERT_EQ(0x11111111, value);
    value = nmo_chunk_parser_read_manager_int_sequence(parser);
    ASSERT_EQ(0x22222222, value);

    uint32_t tail = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_dword(parser, &tail));
    ASSERT_EQ(0x33333333, tail);

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
    chunk->data_size = 10;
    chunk->data = (uint32_t*)nmo_arena_alloc(arena, 10 * sizeof(uint32_t), sizeof(uint32_t));
    ASSERT_NOT_NULL(chunk->data);
    memset(chunk->data, 0, 10 * sizeof(uint32_t));
    chunk->data[0] = 0x1D1D1D1D;
    chunk->data[1] = 4;
    chunk->data[4] = 0x2D2D2D2D;
    chunk->data[5] = 8;
    chunk->data[8] = 0x3D3D3D3D;
    chunk->data[9] = 0; // End of list

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Read the first identifier to set the initial state
    uint32_t id;
    ASSERT_EQ(nmo_chunk_parser_read_identifier(parser, &id), NMO_OK);
    ASSERT_EQ(id, 0x1D1D1D1D);

    // Seek to the third identifier (0x3D3D3D3D)
    ASSERT_EQ(nmo_chunk_parser_seek_identifier(parser, 0x3D3D3D3D), NMO_OK);
    ASSERT_EQ(nmo_chunk_parser_tell(parser), 10); // Cursor should be after the [ID, NextPos] pair

    // Try to seek a non-existent ID
    ASSERT_EQ(nmo_chunk_parser_seek_identifier(parser, 0xBADBAD), NMO_ERR_EOF);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_parser, bounds_checking) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t* chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    // Create chunk with 1 DWORD
    chunk->data_size = 1;
    chunk->data = (uint32_t*)nmo_arena_alloc(arena, 1 * sizeof(uint32_t), sizeof(uint32_t));
    ASSERT_NOT_NULL(chunk->data);
    chunk->data[0] = 0x12345678;

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // First read should succeed
    uint32_t val;
    ASSERT_EQ(nmo_chunk_parser_read_dword(parser, &val), NMO_OK);

    // Second read should fail (EOF)
    ASSERT_EQ(nmo_chunk_parser_read_dword(parser, &val), NMO_ERR_EOF);

    // at_end should return true
    ASSERT_TRUE(nmo_chunk_parser_at_end(parser));

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_parser, create_destroy);
    REGISTER_TEST(chunk_parser, cursor_operations);
    REGISTER_TEST(chunk_parser, primitive_reads);
    REGISTER_TEST(chunk_parser, string_read);
    REGISTER_TEST(chunk_parser, object_sequence_state);
    REGISTER_TEST(chunk_parser, manager_sequence_state);
    REGISTER_TEST(chunk_parser, identifier_navigation);
    REGISTER_TEST(chunk_parser, bounds_checking);
TEST_MAIN_END()
