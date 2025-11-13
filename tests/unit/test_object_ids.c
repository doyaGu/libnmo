/**
 * @file test_object_ids.c
 * @brief Test for object ID system implementation
 *
 * Tests WriteObjectID and ReadObjectID functions
 * to ensure proper behavior matching CKStateChunk.
 */

#include "format/nmo_chunk_writer.h"
#include "format/nmo_chunk_parser.h"
#include "format/nmo_chunk.h"
#include "core/nmo_arena.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>

TEST(object_ids, write_and_read_object_ids) {
    // Create arena
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    // Create writer
    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    // Start chunk
    nmo_chunk_writer_start(writer, 0x12345678, 7);

    // Write object ID 1 (non-zero, should be tracked)
    int result = nmo_chunk_writer_write_object_id(writer, (nmo_object_id_t)1001);
    ASSERT_EQ(result, NMO_OK);

    // Write separator
    result = nmo_chunk_writer_write_dword(writer, 0xABCDEF00);
    ASSERT_EQ(result, NMO_OK);

    // Write object ID 2 (non-zero, should be tracked)
    result = nmo_chunk_writer_write_object_id(writer, (nmo_object_id_t)2002);
    ASSERT_EQ(result, NMO_OK);

    // Write separator
    result = nmo_chunk_writer_write_dword(writer, 0x12345678);
    ASSERT_EQ(result, NMO_OK);

    // Write object ID 0 (zero, should not be tracked)
    result = nmo_chunk_writer_write_object_id(writer, (nmo_object_id_t)0);
    ASSERT_EQ(result, NMO_OK);

    // Write separator
    result = nmo_chunk_writer_write_dword(writer, 0xDEADBEEF);
    ASSERT_EQ(result, NMO_OK);

    // Write object ID 3 (non-zero, should be tracked)
    result = nmo_chunk_writer_write_object_id(writer, (nmo_object_id_t)3003);
    ASSERT_EQ(result, NMO_OK);

    // Finalize chunk
    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Verify ID tracking: should have 3 positions (for IDs 1001, 2002, 3003)
    // but not for ID 0
    ASSERT_EQ(chunk->id_count, 3);

    // Verify tracked positions
    ASSERT_EQ(chunk->ids[0], 0);
    ASSERT_EQ(chunk->ids[1], 2);
    ASSERT_EQ(chunk->ids[2], 6);

    // Create parser
    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Read object ID 1
    nmo_object_id_t read_id = 0;
    result = nmo_chunk_parser_read_object_id(parser, &read_id);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(read_id, (nmo_object_id_t)1001);

    // Read separator 1
    uint32_t data = 0;
    result = nmo_chunk_parser_read_dword(parser, &data);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(data, 0xABCDEF00);

    // Read object ID 2
    result = nmo_chunk_parser_read_object_id(parser, &read_id);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(read_id, (nmo_object_id_t)2002);

    // Read separator 2
    result = nmo_chunk_parser_read_dword(parser, &data);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(data, 0x12345678);

    // Read object ID 0
    result = nmo_chunk_parser_read_object_id(parser, &read_id);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(read_id, (nmo_object_id_t)0);

    // Read separator 3
    result = nmo_chunk_parser_read_dword(parser, &data);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(data, 0xDEADBEEF);

    // Read object ID 3
    result = nmo_chunk_parser_read_object_id(parser, &read_id);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(read_id, (nmo_object_id_t)3003);

    // Cleanup
    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(object_ids, write_and_read_object_ids);
TEST_MAIN_END()
