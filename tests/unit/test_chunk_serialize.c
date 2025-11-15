/**
 * @file test_chunk_serialize.c
 * @brief Unit tests for chunk serialization
 */

#include "../test_framework.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_writer.h"
#include "format/nmo_chunk_parser.h"
#include "core/nmo_arena.h"

/**
 * Test basic chunk serialization and deserialization
 */
TEST(chunk_serialize, serialize_and_deserialize) {
    /* Create arena */
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024*1024);
    ASSERT_NOT_NULL(arena);

    /* Create writer */
    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    /* Start chunk with class ID and legacy format version for VERSION1 layout */
    nmo_chunk_writer_start(writer, 0x12345678, NMO_CHUNK_VERSION_1);

    /* Write some test data */
    int result = nmo_chunk_writer_write_int(writer, 42);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_writer_write_float(writer, 3.14f);
    ASSERT_EQ(result, NMO_OK);

    /* Finalize writer to get chunk */
    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    /* Serialize chunk */
    void *data = NULL;
    size_t size = 0;
    nmo_result_t res = nmo_chunk_serialize_version1(chunk, &data, &size, arena);
    ASSERT_EQ(res.code, NMO_OK);
    ASSERT_NOT_NULL(data);
    ASSERT_GT(size, 0);

    /* VERSION1 layout should carry the full class ID */
    uint32_t *words = (uint32_t *) data;
    ASSERT_EQ(words[1], 0x12345678);

    /* Deserialize chunk using nmo_chunk_parse (for VERSION1 format) */
    nmo_chunk_t *read_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(read_chunk);
    
    res = nmo_chunk_parse(read_chunk, data, size);
    ASSERT_EQ(res.code, NMO_OK);
    ASSERT_NOT_NULL(read_chunk);
    ASSERT_EQ(read_chunk->chunk_version, NMO_CHUNK_VERSION_1);

    /* Verify metadata */
    ASSERT_EQ(read_chunk->class_id, 0x12345678);

    /* Create parser to read data */
    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(read_chunk);
    ASSERT_NOT_NULL(parser);

    /* Read and verify data */
    int32_t int_val = 0;
    result = nmo_chunk_parser_read_int(parser, &int_val);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(int_val, 42);

    float float_val = 0.0f;
    result = nmo_chunk_parser_read_float(parser, &float_val);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_FLOAT_EQ(float_val, 3.14f, 0.001f);

    /* Cleanup */
    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

/**
 * Test empty chunk serialization
 */
TEST(chunk_serialize, empty_chunk) {
    /* Create arena */
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024*1024);
    ASSERT_NOT_NULL(arena);

    /* Create writer */
    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    /* Start empty chunk */
    nmo_chunk_writer_start(writer, 0x00000001, NMO_CHUNK_VERSION_1);

    /* Finalize to get chunk */
    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    /* Verify empty */
    ASSERT_EQ(chunk->data_size, 0);

    /* Cleanup */
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_serialize, serialize_and_deserialize);
    REGISTER_TEST(chunk_serialize, empty_chunk);
TEST_MAIN_END()
