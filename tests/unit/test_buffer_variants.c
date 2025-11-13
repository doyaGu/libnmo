/**
 * @file test_buffer_variants.c
 * @brief Test for buffer operation variants
 *
 * Tests WriteBufferNoSize, ReadBufferNoSize, LockWriteBuffer, and LockReadBuffer
 * to ensure proper behavior matching CKStateChunk.
 */

#include "../test_framework.h"
#include "format/nmo_chunk_writer.h"
#include "format/nmo_chunk_parser.h"
#include "format/nmo_chunk.h"
#include "core/nmo_arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST(buffer_variants, write_read_buffer_nosize) {
    // Create arena
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    nmo_chunk_writer_start(writer, 0x12345678, 7);

    // Write some data before buffer
    int result = nmo_chunk_writer_write_int(writer, 111);
    ASSERT_EQ(result, NMO_OK);

    // Write buffer without size (7 bytes)
    uint8_t test_data[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11};
    result = nmo_chunk_writer_write_buffer_nosize(writer, sizeof(test_data), test_data);
    ASSERT_EQ(result, NMO_OK);

    // Write some data after buffer
    result = nmo_chunk_writer_write_int(writer, 222);
    ASSERT_EQ(result, NMO_OK);

    // Finalize chunk
    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Parse back
    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Read int before buffer
    int32_t value = 0;
    result = nmo_chunk_parser_read_int(parser, &value);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(value, 111);

    // Read buffer without size (must provide size)
    uint8_t read_data[sizeof(test_data)];
    result = nmo_chunk_parser_read_buffer_nosize(parser, sizeof(read_data), read_data);
    ASSERT_EQ(result, NMO_OK);

    // Verify buffer content
    ASSERT_EQ(memcmp(test_data, read_data, sizeof(test_data)), 0);

    // Manually advance cursor past the buffer (7 bytes -> 2 DWORDs)
    // read_buffer_nosize may not advance the cursor
    nmo_chunk_parser_skip(parser, 2);

    // Read int after buffer
    result = nmo_chunk_parser_read_int(parser, &value);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(value, 222);

    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

TEST(buffer_variants, lock_write_read_buffer) {
    // Create arena
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    nmo_chunk_writer_start(writer, 0xABCDEF00, 7);

    // Write marker before locked buffer
    int result = nmo_chunk_writer_write_int(writer, 333);
    ASSERT_EQ(result, NMO_OK);

    // Lock write buffer (5 DWORDs)
    uint32_t* write_ptr = nmo_chunk_writer_lock_write_buffer(writer, 5);
    ASSERT_NOT_NULL(write_ptr);

    // Write directly to locked buffer
    write_ptr[0] = 0x11111111;
    write_ptr[1] = 0x22222222;
    write_ptr[2] = 0x33333333;
    write_ptr[3] = 0x44444444;
    write_ptr[4] = 0x55555555;

    // Write marker after locked buffer
    result = nmo_chunk_writer_write_int(writer, 444);
    ASSERT_EQ(result, NMO_OK);

    // Finalize chunk
    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Parse back
    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Read marker before locked buffer
    int32_t value = 0;
    result = nmo_chunk_parser_read_int(parser, &value);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(value, 333);

    // Lock read buffer
    const uint32_t* read_ptr = nmo_chunk_parser_lock_read_buffer(parser);
    ASSERT_NOT_NULL(read_ptr);

    // Read directly from locked buffer and verify
    ASSERT_EQ(read_ptr[0], 0x11111111);
    ASSERT_EQ(read_ptr[1], 0x22222222);
    ASSERT_EQ(read_ptr[2], 0x33333333);
    ASSERT_EQ(read_ptr[3], 0x44444444);
    ASSERT_EQ(read_ptr[4], 0x55555555);

    // Manually advance cursor (since we used lock)
    // We need to read the data normally to advance cursor
    uint32_t locked_data[5];
    result = nmo_chunk_parser_read_buffer_nosize(parser, 5 * sizeof(uint32_t), locked_data);
    ASSERT_EQ(result, NMO_OK);

    // Read marker after locked buffer
    result = nmo_chunk_parser_read_int(parser, &value);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(value, 444);

    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

TEST(buffer_variants, edge_cases) {
    // Create arena
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    nmo_chunk_writer_start(writer, 0x99999999, 7);

    // Test zero-size buffer
    int result = nmo_chunk_writer_write_buffer_nosize(writer, 0, NULL);
    ASSERT_EQ(result, NMO_OK);

    // Test NULL buffer
    result = nmo_chunk_writer_write_buffer_nosize(writer, 10, NULL);
    ASSERT_EQ(result, NMO_OK);

    // Write a marker
    result = nmo_chunk_writer_write_int(writer, 555);
    ASSERT_EQ(result, NMO_OK);

    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Read marker (should be first)
    int32_t value = 0;
    result = nmo_chunk_parser_read_int(parser, &value);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(value, 555);

    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(buffer_variants, write_read_buffer_nosize);
    REGISTER_TEST(buffer_variants, lock_write_read_buffer);
    REGISTER_TEST(buffer_variants, edge_cases);
TEST_MAIN_END()
