/**
 * @file test_chunk_writer.c
 * @brief Unit tests for chunk writer
 */

#include "test_framework.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_writer.h"
#include "format/nmo_chunk_parser.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <stdio.h>

/**
 * Test: Create writer and write primitives
 */
TEST(chunk_writer, primitives) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    nmo_chunk_writer_start(writer, 12345, NMO_CHUNK_VERSION_4);

    // Write primitives
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_byte(writer, 0x78));
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_word(writer, 0x5678));
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_dword(writer, 0x12345678));
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_int(writer, -42));
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_float(writer, 3.14159f));

    nmo_guid_t test_guid = nmo_guid_create(0x11111111, 0x22222222);
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_guid(writer, test_guid));

    // Finalize
    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Verify we wrote 7 DWORDs (byte, word, dword, int, float, guid1, guid2)
    ASSERT_EQ(7, chunk->data_size);

    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

/**
 * Test: Write and read roundtrip
 */
TEST(chunk_writer, roundtrip) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    // Write data
    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    nmo_chunk_writer_start(writer, 100, NMO_CHUNK_VERSION_4);

    uint32_t test_value = 0xDEADBEEF;
    nmo_chunk_writer_write_dword(writer, test_value);

    float test_float = 2.71828f;
    nmo_chunk_writer_write_float(writer, test_float);

    const char* test_str = "Hello, Virtools!";
    nmo_chunk_writer_write_string(writer, test_str);

    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Read data back
    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    uint32_t read_value;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_dword(parser, &read_value));
    ASSERT_EQ(test_value, read_value);

    float read_float;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_float(parser, &read_float));
    ASSERT_TRUE(read_float >= 2.71f && read_float <= 2.72f);

    char* read_str = NULL;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_string(parser, &read_str, arena));
    ASSERT_NOT_NULL(read_str);
    ASSERT_EQ(0, strcmp(read_str, test_str));

    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

/**
 * Test: Object ID tracking
 */
TEST(chunk_writer, object_ids) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    nmo_chunk_writer_start(writer, 200, NMO_CHUNK_VERSION_4);
    nmo_chunk_writer_start_object_sequence(writer, 3);

    // Write some object IDs
    nmo_chunk_writer_write_object_id(writer, 1001);
    nmo_chunk_writer_write_object_id(writer, 1002);
    nmo_chunk_writer_write_object_id(writer, 1001);  // Duplicate

    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Check that IDS option is set
    ASSERT_TRUE(chunk->chunk_options & NMO_CHUNK_OPTION_IDS);

    // ID tracking now captures sequence marker + write positions
    ASSERT_EQ(5u, chunk->id_count);
    ASSERT_EQ(0xFFFFFFFFu, chunk->ids[0]);
    ASSERT_EQ(0u, chunk->ids[1]);        // Sequence starts before count write
    ASSERT_EQ(1u, chunk->ids[2]);        // First ID location
    ASSERT_EQ(2u, chunk->ids[3]);        // Second ID location
    ASSERT_EQ(3u, chunk->ids[4]);        // Third ID location (duplicate allowed)

    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

/**
 * Test: Automatic buffer growth
 */
TEST(chunk_writer, growth) {
    nmo_arena_t* arena = nmo_arena_create(NULL, 65536);  // Larger arena
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    nmo_chunk_writer_start(writer, 300, NMO_CHUNK_VERSION_4);

    // Write more than initial capacity (100 DWORDs)
    for (int i = 0; i < 200; i++) {
        ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_dword(writer, (uint32_t)i));
    }

    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Verify we wrote 200 DWORDs
    ASSERT_EQ(200, chunk->data_size);

    // Verify data
    for (size_t i = 0; i < 200; i++) {
        ASSERT_EQ((uint32_t)i, chunk->data[i]);
    }

    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_writer, primitives);
    REGISTER_TEST(chunk_writer, roundtrip);
    REGISTER_TEST(chunk_writer, object_ids);
    REGISTER_TEST(chunk_writer, growth);
TEST_MAIN_END()
