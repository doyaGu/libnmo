/**
 * @file test_arrays.c
 * @brief Test for array support implementation
 *
 * Tests WriteArray_LEndian and ReadArray_LEndian functions
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

TEST(arrays, write_read_int_array) {
    // Create arena
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    // Create writer
    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    // Start chunk
    nmo_chunk_writer_start(writer, 0x12345678, 7);

    // Write array of integers
    int32_t int_array[] = {100, 200, 300, 400, 500};
    int result = nmo_chunk_writer_write_array_lendian(writer, 5, sizeof(int32_t), int_array);
    ASSERT_EQ(result, NMO_OK);

    // Finalize chunk
    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Create parser
    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Read int array
    void* read_array = NULL;
    int count = nmo_chunk_parser_read_array_lendian(parser, &read_array, arena);
    ASSERT_EQ(count, 5);
    ASSERT_NOT_NULL(read_array);

    int32_t* read_ints = (int32_t*)read_array;
    ASSERT_EQ(read_ints[0], 100);
    ASSERT_EQ(read_ints[1], 200);
    ASSERT_EQ(read_ints[2], 300);
    ASSERT_EQ(read_ints[3], 400);
    ASSERT_EQ(read_ints[4], 500);

    // Cleanup
    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

TEST(arrays, write_read_float_array) {
    // Create arena
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    // Create writer
    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    // Start chunk
    nmo_chunk_writer_start(writer, 0x12345678, 7);

    // Write array of floats
    float float_array[] = {1.5f, 2.5f, 3.5f};
    int result = nmo_chunk_writer_write_array_lendian(writer, 3, sizeof(float), float_array);
    ASSERT_EQ(result, NMO_OK);

    // Finalize chunk
    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Create parser
    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Read float array
    void* read_array = NULL;
    int count = nmo_chunk_parser_read_array_lendian(parser, &read_array, arena);
    ASSERT_EQ(count, 3);
    ASSERT_NOT_NULL(read_array);

    float* read_floats = (float*)read_array;
    ASSERT_FLOAT_EQ(read_floats[0], 1.5f, 0.001f);
    ASSERT_FLOAT_EQ(read_floats[1], 2.5f, 0.001f);
    ASSERT_FLOAT_EQ(read_floats[2], 3.5f, 0.001f);

    // Cleanup
    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

TEST(arrays, write_read_byte_array) {
    // Create arena
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    // Create writer
    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    // Start chunk
    nmo_chunk_writer_start(writer, 0x12345678, 7);

    // Write array of bytes (not DWORD-aligned size)
    uint8_t byte_array[] = {10, 20, 30, 40, 50, 60, 70};
    int result = nmo_chunk_writer_write_array_lendian(writer, 7, sizeof(uint8_t), byte_array);
    ASSERT_EQ(result, NMO_OK);

    // Finalize chunk
    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Create parser
    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Read byte array
    void* read_array = NULL;
    int count = nmo_chunk_parser_read_array_lendian(parser, &read_array, arena);
    ASSERT_EQ(count, 7);
    ASSERT_NOT_NULL(read_array);

    uint8_t* read_bytes = (uint8_t*)read_array;
    ASSERT_EQ(read_bytes[0], 10);
    ASSERT_EQ(read_bytes[1], 20);
    ASSERT_EQ(read_bytes[2], 30);
    ASSERT_EQ(read_bytes[3], 40);
    ASSERT_EQ(read_bytes[4], 50);
    ASSERT_EQ(read_bytes[5], 60);
    ASSERT_EQ(read_bytes[6], 70);

    // Cleanup
    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

TEST(arrays, write_read_null_array) {
    // Create arena
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    // Create writer
    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    // Start chunk
    nmo_chunk_writer_start(writer, 0x12345678, 7);

    // Write empty array (NULL data)
    int result = nmo_chunk_writer_write_array_lendian(writer, 10, sizeof(int32_t), NULL);
    ASSERT_EQ(result, NMO_OK);

    // Finalize chunk
    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Create parser
    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Read NULL array
    void* read_array = NULL;
    int count = nmo_chunk_parser_read_array_lendian(parser, &read_array, arena);
    ASSERT_EQ(count, 0);
    ASSERT_NULL(read_array);

    // Cleanup
    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

TEST(arrays, write_read_zero_count_array) {
    // Create arena
    nmo_arena_t* arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    // Create writer
    nmo_chunk_writer_t* writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    // Start chunk
    nmo_chunk_writer_start(writer, 0x12345678, 7);

    // Write empty array (zero count)
    int32_t int_array[] = {100, 200, 300};
    int result = nmo_chunk_writer_write_array_lendian(writer, 0, sizeof(int32_t), int_array);
    ASSERT_EQ(result, NMO_OK);

    // Finalize chunk
    nmo_chunk_t* chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Create parser
    nmo_chunk_parser_t* parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Read zero-count array
    void* read_array = NULL;
    int count = nmo_chunk_parser_read_array_lendian(parser, &read_array, arena);
    ASSERT_EQ(count, 0);
    ASSERT_NULL(read_array);

    // Cleanup
    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(arrays, write_read_int_array);
    REGISTER_TEST(arrays, write_read_float_array);
    REGISTER_TEST(arrays, write_read_byte_array);
    REGISTER_TEST(arrays, write_read_null_array);
    REGISTER_TEST(arrays, write_read_zero_count_array);
TEST_MAIN_END()
