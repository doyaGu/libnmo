/**
 * @file test_manager_ints.c
 * @brief Test for manager int system implementation
 *
 * Tests WriteManagerInt and ReadManagerInt functions
 * to ensure proper behavior matching CKStateChunk.
 */

#include "format/nmo_chunk_writer.h"
#include "format/nmo_chunk_parser.h"
#include "format/nmo_chunk.h"
#include "core/nmo_arena.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>

TEST(manager_ints, write_and_read_manager_ints) {
    // Create arena
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    // Create writer
    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    // Start chunk
    nmo_chunk_writer_start(writer, 0x12345678, 7);

    // Write manager int 1: GUID {0x1111, 0x2222}, value 100
    nmo_guid_t manager1 = {0x1111, 0x2222};
    int result = nmo_chunk_writer_write_manager_int(writer, manager1, 100);
    ASSERT_EQ(result, NMO_OK);

    // Write some regular data
    result = nmo_chunk_writer_write_dword(writer, 0xAAAA);
    ASSERT_EQ(result, NMO_OK);

    // Write manager int 2: GUID {0x3333, 0x4444}, value 200
    nmo_guid_t manager2 = {0x3333, 0x4444};
    result = nmo_chunk_writer_write_manager_int(writer, manager2, 200);
    ASSERT_EQ(result, NMO_OK);

    // Write more data
    result = nmo_chunk_writer_write_dword(writer, 0xBBBB);
    ASSERT_EQ(result, NMO_OK);

    // Write manager int 3: GUID {0x5555, 0x6666}, value -50
    nmo_guid_t manager3 = {0x5555, 0x6666};
    result = nmo_chunk_writer_write_manager_int(writer, manager3, -50);
    ASSERT_EQ(result, NMO_OK);

    // Finalize chunk
    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Verify manager tracking
    ASSERT_EQ(chunk->manager_count, 3);

    // Create parser
    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Read manager int 1
    nmo_guid_t read_guid = {0, 0};
    int32_t value = nmo_chunk_parser_read_manager_int(parser, &read_guid);
    ASSERT_EQ(value, 100);
    ASSERT_EQ(read_guid.d1, 0x1111);
    ASSERT_EQ(read_guid.d2, 0x2222);

    // Read data 1
    uint32_t data = 0;
    result = nmo_chunk_parser_read_dword(parser, &data);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(data, 0xAAAA);

    // Read manager int 2
    value = nmo_chunk_parser_read_manager_int(parser, &read_guid);
    ASSERT_EQ(value, 200);
    ASSERT_EQ(read_guid.d1, 0x3333);
    ASSERT_EQ(read_guid.d2, 0x4444);

    // Read data 2
    result = nmo_chunk_parser_read_dword(parser, &data);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(data, 0xBBBB);

    // Read manager int 3 without GUID
    value = nmo_chunk_parser_read_manager_int(parser, NULL);
    ASSERT_EQ(value, -50);

    // Cleanup
    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(manager_ints, write_and_read_manager_ints);
TEST_MAIN_END()
