/**
 * @file test_identifier_search.c
 * @brief Unit tests for identifier search functionality
 *
 * This test verifies Phase 3 implementation: identifier seek logic matching
 * CKStateChunk::SeekIdentifier behavior.
 */

#include "../test_framework.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_parser.h"
#include "format/nmo_chunk_writer.h"
#include "core/nmo_arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/**
 * Test basic identifier search
 */
TEST(identifier_search, basic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    // Create a chunk with identifiers
    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    nmo_chunk_writer_start(writer, 0x12345678, 7);

    // Write first identifier with data
    nmo_chunk_writer_write_identifier(writer, 0x00000001);
    nmo_chunk_writer_write_int(writer, 100);
    nmo_chunk_writer_write_int(writer, 200);

    // Write second identifier with data
    nmo_chunk_writer_write_identifier(writer, 0x00000002);
    nmo_chunk_writer_write_int(writer, 300);

    // Write third identifier with data
    nmo_chunk_writer_write_identifier(writer, 0x00000003);
    nmo_chunk_writer_write_int(writer, 400);
    nmo_chunk_writer_write_int(writer, 500);
    nmo_chunk_writer_write_int(writer, 600);

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Create parser
    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Test seeking to identifier 1
    int result = nmo_chunk_parser_seek_identifier(parser, 0x00000001);
    ASSERT_EQ(result, NMO_OK);

    int32_t val1, val2;
    result = nmo_chunk_parser_read_int(parser, &val1);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_parser_read_int(parser, &val2);
    ASSERT_EQ(result, NMO_OK);

    ASSERT_EQ(val1, 100);
    ASSERT_EQ(val2, 200);

    // Test seeking to identifier 2
    result = nmo_chunk_parser_seek_identifier(parser, 0x00000002);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_parser_read_int(parser, &val1);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(val1, 300);

    // Test seeking to identifier 3
    result = nmo_chunk_parser_seek_identifier(parser, 0x00000003);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_parser_read_int(parser, &val1);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_parser_read_int(parser, &val2);
    ASSERT_EQ(result, NMO_OK);
    int32_t val3;
    result = nmo_chunk_parser_read_int(parser, &val3);
    ASSERT_EQ(result, NMO_OK);

    ASSERT_EQ(val1, 400);
    ASSERT_EQ(val2, 500);
    ASSERT_EQ(val3, 600);

    // Test seeking to non-existent identifier
    result = nmo_chunk_parser_seek_identifier(parser, 0x99999999);
    ASSERT_EQ(result, NMO_ERR_EOF);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

/**
 * Test seeking back to earlier identifier
 */
TEST(identifier_search, seek_backwards) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    // Create chunk
    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    nmo_chunk_writer_start(writer, 0x12345678, 7);

    nmo_chunk_writer_write_identifier(writer, 0x00000001);
    nmo_chunk_writer_write_int(writer, 111);

    nmo_chunk_writer_write_identifier(writer, 0x00000002);
    nmo_chunk_writer_write_int(writer, 222);

    nmo_chunk_writer_write_identifier(writer, 0x00000003);
    nmo_chunk_writer_write_int(writer, 333);

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Seek forward
    int result = nmo_chunk_parser_seek_identifier(parser, 0x00000002);
    ASSERT_EQ(result, NMO_OK);

    int32_t val;
    result = nmo_chunk_parser_read_int(parser, &val);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(val, 222);

    // Now seek back to ID 1 (this tests cycle detection logic)
    result = nmo_chunk_parser_seek_identifier(parser, 0x00000001);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_parser_read_int(parser, &val);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(val, 111);

    // Seek forward again to ID 3
    result = nmo_chunk_parser_seek_identifier(parser, 0x00000003);
    ASSERT_EQ(result, NMO_OK);
    result = nmo_chunk_parser_read_int(parser, &val);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(val, 333);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

/**
 * Test with single identifier
 */
TEST(identifier_search, single) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    nmo_chunk_writer_start(writer, 0x12345678, 7);

    nmo_chunk_writer_write_identifier(writer, 0xABCDEF01);
    nmo_chunk_writer_write_int(writer, 999);

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    int result = nmo_chunk_parser_seek_identifier(parser, 0xABCDEF01);
    ASSERT_EQ(result, NMO_OK);

    int32_t val;
    result = nmo_chunk_parser_read_int(parser, &val);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(val, 999);

    // Try to seek to non-existent identifier
    result = nmo_chunk_parser_seek_identifier(parser, 0x12345678);
    ASSERT_EQ(result, NMO_ERR_EOF);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

/**
 * Test multiple seeks to same identifier
 */
TEST(identifier_search, multiple_seeks_same_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    nmo_chunk_writer_start(writer, 0x12345678, 7);

    nmo_chunk_writer_write_identifier(writer, 0x00000001);
    nmo_chunk_writer_write_int(writer, 10);
    nmo_chunk_writer_write_int(writer, 20);

    nmo_chunk_writer_write_identifier(writer, 0x00000002);
    nmo_chunk_writer_write_int(writer, 30);

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Seek to ID 1 multiple times
    for (int i = 0; i < 3; i++) {
        int result = nmo_chunk_parser_seek_identifier(parser, 0x00000001);
        ASSERT_EQ(result, NMO_OK);

        int32_t val1, val2;
        result = nmo_chunk_parser_read_int(parser, &val1);
        ASSERT_EQ(result, NMO_OK);
        result = nmo_chunk_parser_read_int(parser, &val2);
        ASSERT_EQ(result, NMO_OK);

        ASSERT_EQ(val1, 10);
        ASSERT_EQ(val2, 20);
    }

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

/**
 * Test empty chunk
 */
TEST(identifier_search, empty_chunk) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    chunk->data_version = 1;
    chunk->chunk_version = 7;
    chunk->data_size = 0;
    chunk->data = NULL;

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    int result = nmo_chunk_parser_seek_identifier(parser, 0x00000001);
    ASSERT_TRUE(result == NMO_ERR_EOF || result == NMO_ERR_NOT_FOUND);  // Should return EOF or NOT_FOUND for empty chunk

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

/**
 * Test with many identifiers
 */
TEST(identifier_search, many_identifiers) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    nmo_chunk_writer_start(writer, 0x12345678, 7);

    // Write 10 identifiers
    const int num_ids = 10;
    for (int i = 0; i < num_ids; i++) {
        nmo_chunk_writer_write_identifier(writer, 0x1000 + i);
        nmo_chunk_writer_write_int(writer, i * 100);
    }

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Test seeking to each identifier
    for (int i = 0; i < num_ids; i++) {
        int result = nmo_chunk_parser_seek_identifier(parser, 0x1000 + i);
        ASSERT_EQ(result, NMO_OK);

        int32_t val;
        result = nmo_chunk_parser_read_int(parser, &val);
        ASSERT_EQ(result, NMO_OK);
        ASSERT_EQ(val, i * 100);
    }

    // Test seeking in reverse order
    for (int i = num_ids - 1; i >= 0; i--) {
        int result = nmo_chunk_parser_seek_identifier(parser, 0x1000 + i);
        ASSERT_EQ(result, NMO_OK);

        int32_t val;
        result = nmo_chunk_parser_read_int(parser, &val);
        ASSERT_EQ(result, NMO_OK);
        ASSERT_EQ(val, i * 100);
    }

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(identifier_search, basic);
    REGISTER_TEST(identifier_search, seek_backwards);
    REGISTER_TEST(identifier_search, single);
    REGISTER_TEST(identifier_search, multiple_seeks_same_id);
    REGISTER_TEST(identifier_search, empty_chunk);
    REGISTER_TEST(identifier_search, many_identifiers);
TEST_MAIN_END()
