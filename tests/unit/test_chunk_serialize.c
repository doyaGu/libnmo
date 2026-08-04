/**
 * @file test_chunk_serialize.c
 * @brief Unit tests for chunk serialization
 */

#include "../test_framework.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_chunk_context.h"
#include "core/nmo_arena.h"

/**
 * Test basic chunk serialization and deserialization
 */
TEST(chunk_serialize, serialize_and_deserialize) {
    /* Create arena */
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024*1024);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    chunk->class_id = 0x12345678;
    chunk->chunk_version = NMO_CHUNK_VERSION1;

    /* Write some test data */
    int result = nmo_chunk_write_int(chunk, 42);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_write_float(chunk, 3.14f);
    ASSERT_EQ(result, NMO_OK);
    nmo_chunk_close(chunk);

    /* Serialize chunk */
    void *data = NULL;
    size_t size = 0;
    nmo_status_t res = nmo_chunk_serialize_version1(chunk, &data, &size, arena);
    ASSERT_EQ(res, NMO_OK);
    ASSERT_NOT_NULL(data);
    ASSERT_GT(size, 0);

    /* VERSION1 layout should carry the full class ID */
    uint32_t *words = (uint32_t *) data;
    ASSERT_EQ(words[1], 0x12345678);

    /* Deserialize chunk using nmo_chunk_parse (for VERSION1 format) */
    nmo_chunk_t *read_chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(read_chunk);
    
    res = nmo_chunk_parse(read_chunk, data, size);
    ASSERT_EQ(res, NMO_OK);
    ASSERT_NOT_NULL(read_chunk);
    ASSERT_EQ(read_chunk->chunk_version, NMO_CHUNK_VERSION1);

    /* Verify metadata */
    ASSERT_EQ(read_chunk->class_id, 0x12345678);

    ASSERT_EQ(NMO_OK, nmo_chunk_start_read(read_chunk));

    /* Read and verify data */
    int32_t int_val = 0;
    nmo_status_t parse_result = nmo_chunk_read_int(read_chunk, &int_val);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_EQ(int_val, 42);

    float float_val = 0.0f;
    parse_result = nmo_chunk_read_float(read_chunk, &float_val);
    ASSERT_EQ(parse_result, NMO_OK);
    ASSERT_FLOAT_EQ(float_val, 3.14f, 0.001f);

    /* Cleanup */
    nmo_arena_destroy(arena);
}

/**
 * Test empty chunk serialization
 */
TEST(chunk_serialize, empty_chunk) {
    /* Create arena */
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024*1024);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(NMO_OK, nmo_chunk_start_write(chunk));
    chunk->class_id = 0x00000001;
    chunk->chunk_version = NMO_CHUNK_VERSION1;
    nmo_chunk_close(chunk);

    /* Verify empty */
    ASSERT_EQ(chunk->data.count, 0);

    /* Cleanup */
    nmo_arena_destroy(arena);
}

TEST(chunk_serialize, parse_failure_preserves_chunk_state) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    chunk->class_id = 0xABCDEF01u;
    chunk->data_version = 9;
    chunk->chunk_version = NMO_CHUNK_VERSION2;
    ASSERT_EQ(NMO_OK, nmo_arena_array_resize(&chunk->data, 1));
    uint32_t *original_data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(original_data);
    original_data[0] = 0x12345678u;
    const uint32_t original_raw[] = { 0xCAFEBABEu };
    chunk->raw_data = original_raw;
    chunk->raw_size = sizeof(original_raw);
    nmo_chunk_file_context_t file_context = {0};
    nmo_chunk_set_file_context(chunk, &file_context);

    const uint32_t truncated[] = {
        ((uint32_t)NMO_CHUNK_VERSION4 << 16) | (0x42u << 8),
        1,
    };
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
        nmo_chunk_parse(chunk, truncated, sizeof(truncated)));

    ASSERT_EQ(0xABCDEF01u, chunk->class_id);
    ASSERT_EQ(9u, chunk->data_version);
    ASSERT_EQ(NMO_CHUNK_VERSION2, chunk->chunk_version);
    ASSERT_TRUE((chunk->chunk_options & NMO_CHUNK_OPTION_FILE) != 0);
    ASSERT_EQ(&file_context, nmo_chunk_get_file_context(chunk));
    ASSERT_EQ(original_data, chunk->data.data);
    ASSERT_EQ(1u, chunk->data.count);
    ASSERT_EQ(0x12345678u, original_data[0]);
    ASSERT_EQ(original_raw, chunk->raw_data);
    ASSERT_EQ(sizeof(original_raw), chunk->raw_size);

    nmo_arena_destroy(arena);
}

TEST(chunk_serialize, parse_rejects_overflowing_sequence_count) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    const uint32_t malformed[] = {
        ((uint32_t)NMO_CHUNK_VERSION4 << 16) |
            ((uint32_t)NMO_CHUNK_OPTION_CHN << 24),
        1,
        UINT32_MAX,
        2,
        UINT32_MAX,
        0,
    };
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
        nmo_chunk_parse(chunk, malformed, sizeof(malformed)));
    ASSERT_EQ(0u, chunk->data.count);
    ASSERT_EQ(0u, chunk->chunk_refs.count);

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_serialize, serialize_and_deserialize);
    REGISTER_TEST(chunk_serialize, empty_chunk);
    REGISTER_TEST(chunk_serialize, parse_failure_preserves_chunk_state);
    REGISTER_TEST(chunk_serialize, parse_rejects_overflowing_sequence_count);
TEST_MAIN_END()
