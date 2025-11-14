/**
 * @file test_chunk_16bit.c
 * @brief Phase 6: Tests for 16-bit special format chunk operations
 */

#include "test_framework.h"
#include "core/nmo_arena.h"
#include "format/nmo_chunk_writer.h"
#include "format/nmo_chunk_parser.h"

/**
 * Test: Basic dword_as_words write and read operations
 */
TEST(chunk_16bit, dword_as_words_basic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);
    nmo_chunk_writer_start(writer, 0, NMO_CHUNK_VERSION_4);

    // Test various 32-bit values
    uint32_t test_values[] = {
        0x00000000, 0x0000FFFF, 0xFFFF0000,
        0xFFFFFFFF, 0x12345678, 0xABCDEF01,
    };
    size_t test_count = sizeof(test_values) / sizeof(test_values[0]);

    for (size_t i = 0; i < test_count; i++) {
        ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_dword_as_words(writer, test_values[i]));
    }

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(test_count * 2, chunk->data_size);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    for (size_t i = 0; i < test_count; i++) {
        uint32_t value;
        ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_dword_as_words(parser, &value));
        ASSERT_EQ(test_values[i], value);
    }

    ASSERT_TRUE(nmo_chunk_parser_at_end(parser));
    nmo_arena_destroy(arena);
}

/**
 * Test: Boundary values for dword_as_words
 */
TEST(chunk_16bit, dword_as_words_boundary) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);
    nmo_chunk_writer_start(writer, 0, NMO_CHUNK_VERSION_4);

    uint32_t boundary_values[] = {
        0x00000001, 0x7FFFFFFF, 0x80000000,
        0xFFFFFFFF, 0x12340000, 0x00005678,
    };
    size_t test_count = sizeof(boundary_values) / sizeof(boundary_values[0]);

    for (size_t i = 0; i < test_count; i++) {
        ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_dword_as_words(writer, boundary_values[i]));
    }

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    for (size_t i = 0; i < test_count; i++) {
        uint32_t value;
        ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_dword_as_words(parser, &value));
        ASSERT_EQ(boundary_values[i], value);
    }

    nmo_arena_destroy(arena);
}

/**
 * Test: Array helper for dword_as_words
 */
TEST(chunk_16bit, dword_as_words_array_helper) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);
    nmo_chunk_writer_start(writer, 0, NMO_CHUNK_VERSION_4);

    uint32_t values[] = {
        0x00010002, 0x7FFF8000, 0xDEADBEEF, 0xCAFEBABE,
        0xFFFFFFFF, 0x12345678, 0x0000FFFF, 0xF00DFACE
    };
    size_t value_count = sizeof(values) / sizeof(values[0]);

    ASSERT_EQ(NMO_OK,
              nmo_chunk_writer_write_array_dword_as_words(writer, values, value_count));

    // Ensure invalid arguments are caught before finalizing
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_chunk_writer_write_array_dword_as_words(writer, NULL, value_count));
    ASSERT_EQ(NMO_OK,
              nmo_chunk_writer_write_array_dword_as_words(writer, values, 0));

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(value_count * 2, chunk->data_size);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    uint32_t *decoded = (uint32_t *) nmo_arena_alloc(arena,
                                                     value_count * sizeof(uint32_t),
                                                     sizeof(uint32_t));
    ASSERT_NOT_NULL(decoded);

    ASSERT_EQ(NMO_OK,
              nmo_chunk_parser_read_dword_array_as_words(parser, decoded, value_count));
    ASSERT_EQ(NMO_OK,
              nmo_chunk_parser_read_dword_array_as_words(parser, NULL, 0));

    for (size_t i = 0; i < value_count; ++i) {
        ASSERT_EQ(values[i], decoded[i]);
    }

    ASSERT_TRUE(nmo_chunk_parser_at_end(parser));
    nmo_arena_destroy(arena);
}

/**
 * Test: buffer_nosize_lendian16 with various buffer sizes
 */
TEST(chunk_16bit, buffer_nosize_lendian16_basic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);
    nmo_chunk_writer_start(writer, 0, NMO_CHUNK_VERSION_4);

    uint16_t test_data[] = {
        0x0001, 0x0002, 0x0003, 0x0004,
        0x1234, 0x5678, 0xABCD, 0xEF01,
        0xFFFF, 0x0000, 0x7FFF, 0x8000,
    };
    size_t value_count = sizeof(test_data) / sizeof(test_data[0]);

    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_buffer_nosize_lendian16(writer, value_count, test_data));

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(value_count, chunk->data_size);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    uint16_t read_data[12];
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_buffer_nosize_lendian16(parser, value_count, read_data));

    for (size_t i = 0; i < value_count; i++) {
        ASSERT_EQ(test_data[i], read_data[i]);
    }

    nmo_arena_destroy(arena);
}

/**
 * Test: buffer_nosize_lendian16 with single value
 */
TEST(chunk_16bit, buffer_nosize_lendian16_single) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);
    nmo_chunk_writer_start(writer, 0, NMO_CHUNK_VERSION_4);

    uint16_t single_value = 0xABCD;
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_buffer_nosize_lendian16(writer, 1, &single_value));

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(1, chunk->data_size);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    uint16_t read_value;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_buffer_nosize_lendian16(parser, 1, &read_value));
    ASSERT_EQ(single_value, read_value);

    nmo_arena_destroy(arena);
}

/**
 * Test: buffer_nosize_lendian16 with large buffer
 */
TEST(chunk_16bit, buffer_nosize_lendian16_large) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 128 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);
    nmo_chunk_writer_start(writer, 0, NMO_CHUNK_VERSION_4);

    size_t value_count = 1000;
    uint16_t *test_data = (uint16_t *)nmo_arena_alloc(arena, value_count * sizeof(uint16_t), sizeof(uint16_t));
    ASSERT_NOT_NULL(test_data);

    for (size_t i = 0; i < value_count; i++) {
        test_data[i] = (uint16_t)(i & 0xFFFF);
    }

    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_buffer_nosize_lendian16(writer, value_count, test_data));

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(value_count, chunk->data_size);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    uint16_t *read_data = (uint16_t *)nmo_arena_alloc(arena, value_count * sizeof(uint16_t), sizeof(uint16_t));
    ASSERT_NOT_NULL(read_data);

    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_buffer_nosize_lendian16(parser, value_count, read_data));

    for (size_t i = 0; i < value_count; i++) {
        ASSERT_EQ(test_data[i], read_data[i]);
    }

    nmo_arena_destroy(arena);
}

/**
 * Test: Mixing 16-bit operations with regular operations
 */
TEST(chunk_16bit, mixed_operations) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);
    nmo_chunk_writer_start(writer, 0, NMO_CHUNK_VERSION_4);

    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_dword(writer, 0x11111111));
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_dword_as_words(writer, 0x22223333));
    
    uint16_t buffer[] = {0xAAAA, 0xBBBB, 0xCCCC};
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_buffer_nosize_lendian16(writer, 3, buffer));
    
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_int(writer, -42));
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_dword_as_words(writer, 0x44445555));

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(9, chunk->data_size);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    uint32_t dword_val;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_dword(parser, &dword_val));
    ASSERT_EQ(0x11111111, dword_val);

    uint32_t dword_as_words_val;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_dword_as_words(parser, &dword_as_words_val));
    ASSERT_EQ(0x22223333, dword_as_words_val);

    uint16_t read_buffer[3];
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_buffer_nosize_lendian16(parser, 3, read_buffer));
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ(buffer[i], read_buffer[i]);
    }

    int32_t int_val;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_int(parser, &int_val));
    ASSERT_EQ(-42, int_val);

    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_dword_as_words(parser, &dword_as_words_val));
    ASSERT_EQ(0x44445555, dword_as_words_val);

    ASSERT_TRUE(nmo_chunk_parser_at_end(parser));
    nmo_arena_destroy(arena);
}

/**
 * Test: Error handling
 */
TEST(chunk_16bit, error_handling) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);
    nmo_chunk_writer_start(writer, 0, NMO_CHUNK_VERSION_4);

    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_dword_as_words(writer, 0x12345678));

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    uint32_t val;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_dword_as_words(parser, &val));
    ASSERT_EQ(0x12345678, val);

    // Try to read beyond end
    ASSERT_EQ(NMO_ERR_EOF, nmo_chunk_parser_read_dword_as_words(parser, &val));

    // Test NULL pointer checks
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_chunk_parser_read_dword_as_words(NULL, &val));
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, nmo_chunk_parser_read_dword_as_words(parser, NULL));

    nmo_arena_destroy(arena);
}

/**
 * Test: Virtools compatibility - compressed animation data format
 */
TEST(chunk_16bit, virtools_compatibility) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);
    nmo_chunk_writer_start(writer, 0, NMO_CHUNK_VERSION_4);

    // Simulate compressed animation data format
    uint32_t keyframe_count = 5;
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_dword_as_words(writer, keyframe_count));

    uint16_t times[] = {0, 10, 20, 30, 40};
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_buffer_nosize_lendian16(writer, keyframe_count, times));

    uint16_t values[] = {100, 200, 150, 175, 125};
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_buffer_nosize_lendian16(writer, keyframe_count, values));

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    uint32_t read_count;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_dword_as_words(parser, &read_count));
    ASSERT_EQ(keyframe_count, read_count);

    uint16_t read_times[5];
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_buffer_nosize_lendian16(parser, keyframe_count, read_times));
    
    uint16_t read_values[5];
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_buffer_nosize_lendian16(parser, keyframe_count, read_values));

    for (size_t i = 0; i < keyframe_count; i++) {
        ASSERT_EQ(times[i], read_times[i]);
        ASSERT_EQ(values[i], read_values[i]);
    }

    ASSERT_TRUE(nmo_chunk_parser_at_end(parser));
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_16bit, dword_as_words_basic);
    REGISTER_TEST(chunk_16bit, dword_as_words_boundary);
    REGISTER_TEST(chunk_16bit, dword_as_words_array_helper);
    REGISTER_TEST(chunk_16bit, buffer_nosize_lendian16_basic);
    REGISTER_TEST(chunk_16bit, buffer_nosize_lendian16_single);
    REGISTER_TEST(chunk_16bit, buffer_nosize_lendian16_large);
    REGISTER_TEST(chunk_16bit, mixed_operations);
    REGISTER_TEST(chunk_16bit, error_handling);
    REGISTER_TEST(chunk_16bit, virtools_compatibility);
TEST_MAIN_END()
