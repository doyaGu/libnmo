/**
 * @file test_identifiers.c
 * @brief Test for identifier system implementation
 *
 * Tests WriteIdentifier, ReadIdentifier and SeekIdentifier functions
 * to ensure proper linked-list behavior matching CKStateChunk.
 */

#include "format/nmo_chunk_writer.h"
#include "format/nmo_chunk_parser.h"
#include "format/nmo_chunk.h"
#include "core/nmo_arena.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>

TEST(identifiers, write_and_read_identifiers) {
    // Create arena (NULL allocator = default, 4096 initial size)
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    // Create writer
    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    // Start chunk
    nmo_chunk_writer_start(writer, 0x12345678, 7);

    // Write identifier 1, then some data
    int result = nmo_chunk_writer_write_identifier(writer, 0x1111);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_writer_write_dword(writer, 0xAAAA);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_writer_write_dword(writer, 0xBBBB);
    ASSERT_EQ(result, NMO_OK);

    // Write identifier 2
    result = nmo_chunk_writer_write_identifier(writer, 0x2222);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_writer_write_dword(writer, 0xCCCC);
    ASSERT_EQ(result, NMO_OK);

    // Write identifier3
    result = nmo_chunk_writer_write_identifier(writer, 0x3333);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_writer_write_dword(writer, 0xDDDD);
    ASSERT_EQ(result, NMO_OK);

    // Finalize chunk
    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Create parser
    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Read first identifier (should be 0x1111)
    uint32_t id = 0;
    result = nmo_chunk_parser_read_identifier(parser, &id);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(id, 0x1111);

    // Read data after identifier 1
    uint32_t data = 0;
    result = nmo_chunk_parser_read_dword(parser, &data);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(data, 0xAAAA);

    result = nmo_chunk_parser_read_dword(parser, &data);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(data, 0xBBBB);

    // Seek to identifier 2
    result = nmo_chunk_parser_seek_identifier(parser, 0x2222);
    ASSERT_EQ(result, NMO_OK);

    // Read data after identifier 2
    result = nmo_chunk_parser_read_dword(parser, &data);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(data, 0xCCCC);

    // Seek to identifier 3
    result = nmo_chunk_parser_seek_identifier(parser, 0x3333);
    ASSERT_EQ(result, NMO_OK);

    // Read data after identifier 3
    result = nmo_chunk_parser_read_dword(parser, &data);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(data, 0xDDDD);

    // Try seeking to non-existent identifier (should fail)
    result = nmo_chunk_parser_seek_identifier(parser, 0x9999);
    ASSERT_EQ(result, NMO_ERR_EOF);

    // Cleanup
    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    // Note: Arena destruction has a known issue - skipping for now
    // nmo_arena_destroy(arena);
}

TEST(identifiers, seek_nonexistent_identifier) {
    // Create arena (NULL allocator = default, 4096 initial size)
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    // Create writer
    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    // Start chunk
    nmo_chunk_writer_start(writer, 0x12345678, 7);

    // Write identifier
    int result = nmo_chunk_writer_write_identifier(writer, 0x1111);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_writer_write_dword(writer, 0xAAAA);
    ASSERT_EQ(result, NMO_OK);

    // Finalize chunk
    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Create parser
    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Try seeking to non-existent identifier (should fail)
    result = nmo_chunk_parser_seek_identifier(parser, 0x9999);
    ASSERT_EQ(result, NMO_ERR_EOF);

    // Cleanup
    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    // Note: Arena destruction has a known issue - skipping for now
    // nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(identifiers, write_and_read_identifiers);
    REGISTER_TEST(identifiers, seek_nonexistent_identifier);
TEST_MAIN_END()
