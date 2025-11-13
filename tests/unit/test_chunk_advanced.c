/**
 * @file test_chunk_advanced.c
 * @brief Unit tests for Phase 6: Advanced Chunk Features
 *
 * Tests:
 * 1. 16-bit endian conversion (read/write)
 * 2. Math type read/write round-trip
 * 3. Chunk cloning (deep copy)
 * 4. SeekIdentifierAndReturnSize
 * 5. Edge cases
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

TEST(chunk_advanced, lendian16_array) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    // Create writer
    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    // Start chunk with class ID
    nmo_chunk_writer_start(writer, 0x1001, NMO_CHUNK_VERSION_4);

    // Test data: array of uint16_t values
    uint16_t test_data[] = {0x1234, 0x5678, 0xABCD, 0xEF00};
    int element_count = 4;
    int element_size = sizeof(uint16_t);

    // Write with 16-bit endian conversion
    int result = nmo_chunk_writer_write_array_lendian16(writer, element_count, element_size, test_data);
    ASSERT_EQ(result, NMO_OK);

    // Finalize writer
    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Create parser to read back
    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    // Read back with 16-bit endian conversion
    void *read_data = NULL;
    int read_count = nmo_chunk_parser_read_array_lendian16(parser, &read_data, arena);
    ASSERT_EQ(read_count, element_count);
    ASSERT_NOT_NULL(read_data);

    // Verify data (should be byte-swapped back to original)
    uint16_t *read_array = (uint16_t *)read_data;
    for (int i = 0; i < element_count; i++) {
        ASSERT_EQ(read_array[i], test_data[i]);
    }

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_advanced, lendian16_buffer) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);

    nmo_chunk_writer_start(writer, 0x1002, NMO_CHUNK_VERSION_4);

    // Test data: buffer of uint16_t values
    uint16_t test_buffer[] = {0xDEAD, 0xBEEF, 0xCAFE, 0xBABE};
    size_t buffer_size = sizeof(test_buffer);

    // Write with 16-bit endian conversion
    int result = nmo_chunk_writer_write_buffer_lendian16(writer, buffer_size, test_buffer);
    ASSERT_EQ(result, NMO_OK);

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Read back
    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    uint16_t read_buffer[4];
    result = nmo_chunk_parser_read_buffer_lendian16(parser, buffer_size, read_buffer);
    ASSERT_EQ(result, NMO_OK);

    // Verify data
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ(read_buffer[i], test_buffer[i]);
    }

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_advanced, math_vector) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    nmo_chunk_writer_start(writer, 0x2001, NMO_CHUNK_VERSION_4);

    // Test Vector2
    nmo_vector2_t v2 = {1.5f, 2.5f};
    int result = nmo_chunk_writer_write_vector2(writer, &v2);
    ASSERT_EQ(result, NMO_OK);

    // Test Vector (3D)
    nmo_vector_t v3 = {3.0f, 4.0f, 5.0f};
    result = nmo_chunk_writer_write_vector(writer, &v3);
    ASSERT_EQ(result, NMO_OK);

    // Test Vector4
    nmo_vector4_t v4 = {6.0f, 7.0f, 8.0f, 9.0f};
    result = nmo_chunk_writer_write_vector4(writer, &v4);
    ASSERT_EQ(result, NMO_OK);

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Read back
    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);

    nmo_vector2_t read_v2;
    result = nmo_chunk_parser_read_vector2(parser, &read_v2);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_TRUE(read_v2.x == v2.x && read_v2.y == v2.y);

    nmo_vector_t read_v3;
    result = nmo_chunk_parser_read_vector(parser, &read_v3);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_TRUE(read_v3.x == v3.x && read_v3.y == v3.y && read_v3.z == v3.z);

    nmo_vector4_t read_v4;
    result = nmo_chunk_parser_read_vector4(parser, &read_v4);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_TRUE(read_v4.x == v4.x && read_v4.y == v4.y &&
                read_v4.z == v4.z && read_v4.w == v4.w);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_advanced, math_matrix) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    nmo_chunk_writer_start(writer, 0x2002, NMO_CHUNK_VERSION_4);

    // Test matrix
    nmo_matrix_t mat;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            mat.m[i][j] = (float)(i * 4 + j);
        }
    }

    int result = nmo_chunk_writer_write_matrix(writer, &mat);
    ASSERT_EQ(result, NMO_OK);

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Read back
    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    nmo_matrix_t read_mat;
    result = nmo_chunk_parser_read_matrix(parser, &read_mat);
    ASSERT_EQ(result, NMO_OK);

    // Verify
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            ASSERT_TRUE(read_mat.m[i][j] == mat.m[i][j]);
        }
    }

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_advanced, math_quaternion) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    nmo_chunk_writer_start(writer, 0x2003, NMO_CHUNK_VERSION_4);

    nmo_quaternion_t quat = {0.707f, 0.0f, 0.707f, 0.0f};
    int result = nmo_chunk_writer_write_quaternion(writer, &quat);
    ASSERT_EQ(result, NMO_OK);

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    nmo_quaternion_t read_quat;
    result = nmo_chunk_parser_read_quaternion(parser, &read_quat);
    ASSERT_EQ(result, NMO_OK);

    ASSERT_TRUE(read_quat.x == quat.x && read_quat.y == quat.y &&
                read_quat.z == quat.z && read_quat.w == quat.w);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_advanced, chunk_clone) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NOT_NULL(arena);

    // Create source chunk with data
    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    nmo_chunk_writer_start(writer, 0x3001, NMO_CHUNK_VERSION_4);

    // Write some data
    uint32_t data[] = {100, 200, 300, 400};
    nmo_chunk_writer_write_dword(writer, data[0]);
    nmo_chunk_writer_write_dword(writer, data[1]);
    nmo_chunk_writer_write_dword(writer, data[2]);
    nmo_chunk_writer_write_dword(writer, data[3]);

    // Add sub-chunk using proper API
    nmo_chunk_writer_start_subchunk_sequence(writer, 1);

    // Create a sub-chunk
    nmo_chunk_writer_t *sub_writer = nmo_chunk_writer_create(arena);
    nmo_chunk_writer_start(sub_writer, 0x3002, NMO_CHUNK_VERSION_4);
    nmo_chunk_writer_write_dword(sub_writer, 999);
    nmo_chunk_t *sub_chunk = nmo_chunk_writer_finalize(sub_writer);

    // Write sub-chunk to parent
    nmo_chunk_writer_write_subchunk(writer, sub_chunk);

    nmo_chunk_t *src = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(src);
    ASSERT_EQ(src->chunk_count, 1);

    // Clone the chunk
    nmo_chunk_t *clone = nmo_chunk_clone(src, arena);
    ASSERT_NOT_NULL(clone);

    // Verify basic fields
    ASSERT_EQ(clone->class_id, src->class_id);
    ASSERT_EQ(clone->data_size, src->data_size);

    // Verify data content
    for (size_t i = 0; i < src->data_size; i++) {
        ASSERT_EQ(clone->data[i], src->data[i]);
    }

    // Verify sub-chunks
    ASSERT_EQ(clone->chunk_count, src->chunk_count);
    ASSERT_NOT_NULL(clone->chunks);
    ASSERT_TRUE(clone->chunks[0] != src->chunks[0]);
    ASSERT_EQ(clone->chunks[0]->class_id, 0x3002);

    // Verify it's a deep copy (different pointers)
    ASSERT_TRUE(clone->data != src->data);

    nmo_arena_destroy(arena);
}

TEST(chunk_advanced, seek_identifier_with_size) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    nmo_chunk_writer_start(writer, 0x4001, NMO_CHUNK_VERSION_4);

    // Write data with identifiers
    nmo_chunk_writer_write_identifier(writer, 0x1000);
    nmo_chunk_writer_write_dword(writer, 100);
    nmo_chunk_writer_write_dword(writer, 200);

    nmo_chunk_writer_write_identifier(writer, 0x2000);
    nmo_chunk_writer_write_dword(writer, 300);

    nmo_chunk_writer_write_identifier(writer, 0x3000);
    nmo_chunk_writer_write_dword(writer, 400);
    nmo_chunk_writer_write_dword(writer, 500);
    nmo_chunk_writer_write_dword(writer, 600);

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Test seeking with size
    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);

    // Seek to first identifier
    size_t size1;
    int result = nmo_chunk_parser_seek_identifier_with_size(parser, 0x1000, &size1);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(size1, 2);

    // Seek to second identifier
    size_t size2;
    result = nmo_chunk_parser_seek_identifier_with_size(parser, 0x2000, &size2);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(size2, 1);

    // Seek to third identifier
    size_t size3;
    result = nmo_chunk_parser_seek_identifier_with_size(parser, 0x3000, &size3);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(size3, 3);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_advanced, edge_empty_array) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    nmo_chunk_writer_start(writer, 0x5001, NMO_CHUNK_VERSION_4);

    // Write empty array
    int result = nmo_chunk_writer_write_array_lendian16(writer, 0, 4, NULL);
    ASSERT_EQ(result, NMO_OK);

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Read back
    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    void *data = NULL;
    int count = nmo_chunk_parser_read_array_lendian16(parser, &data, arena);
    ASSERT_EQ(count, 0);
    ASSERT_NULL(data);

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST(chunk_advanced, edge_odd_buffer) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    nmo_chunk_writer_start(writer, 0x5002, NMO_CHUNK_VERSION_4);

    // Write buffer with odd size (7 bytes - not aligned to 16-bit)
    uint8_t test_data[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    int result = nmo_chunk_writer_write_buffer_nosize(writer, sizeof(test_data), test_data);
    ASSERT_EQ(result, NMO_OK);

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    // Read back
    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    uint8_t read_data[7];
    result = nmo_chunk_parser_read_buffer_nosize(parser, sizeof(read_data), read_data);
    ASSERT_EQ(result, NMO_OK);

    // Verify
    for (size_t i = 0; i < sizeof(test_data); i++) {
        ASSERT_EQ(read_data[i], test_data[i]);
    }

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_advanced, lendian16_array);
    REGISTER_TEST(chunk_advanced, lendian16_buffer);
    REGISTER_TEST(chunk_advanced, math_vector);
    REGISTER_TEST(chunk_advanced, math_matrix);
    REGISTER_TEST(chunk_advanced, math_quaternion);
    REGISTER_TEST(chunk_advanced, chunk_clone);
    REGISTER_TEST(chunk_advanced, seek_identifier_with_size);
    REGISTER_TEST(chunk_advanced, edge_empty_array);
    REGISTER_TEST(chunk_advanced, edge_odd_buffer);
TEST_MAIN_END()
