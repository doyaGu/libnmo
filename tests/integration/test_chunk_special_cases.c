/**
 * @file test_chunk_special_cases.c
 * @brief Integration coverage for 16-bit chunk helpers and identifier flows
 */

#include "../test_framework.h"
#include "core/nmo_arena.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_parser.h"
#include "format/nmo_chunk_writer.h"

/**
 * Validate end-to-end round trip for the array/buffer special encodings.
 */
TEST(chunk_special_cases, array_helpers_round_trip) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);
    nmo_chunk_writer_start(writer, 0x50000001, NMO_CHUNK_VERSION_4);

    const uint32_t dword_values[] = {
        0x01020304, 0x7FFF8000, 0xFACEBEEF, 0x11223344,
        0x55667788, 0xDEADBEEF, 0x00FF00FF, 0xC001D00D
    };
    const size_t dword_count = sizeof(dword_values) / sizeof(dword_values[0]);

    ASSERT_EQ(NMO_OK,
              nmo_chunk_writer_write_array_dword_as_words(writer,
                                                            dword_values,
                                                            dword_count));

    const uint16_t samples[] = {0xAAAA, 0xBBBB, 0xCCCC, 0x1111, 0x2222, 0x3333};
    const size_t sample_count = sizeof(samples) / sizeof(samples[0]);

    ASSERT_EQ(NMO_OK,
              nmo_chunk_writer_write_buffer_nosize_lendian16(writer,
                                                              sample_count,
                                                              samples));

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(dword_count * 2 + sample_count, chunk->data_size);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);

    uint32_t decoded[dword_count];
    ASSERT_EQ(NMO_OK,
              nmo_chunk_parser_read_dword_array_as_words(parser,
                                                         decoded,
                                                         dword_count));

    uint16_t restored[sample_count];
    ASSERT_EQ(NMO_OK,
              nmo_chunk_parser_read_buffer_nosize_lendian16(parser,
                                                             sample_count,
                                                             restored));

    for (size_t i = 0; i < dword_count; ++i) {
        ASSERT_EQ(dword_values[i], decoded[i]);
    }

    for (size_t i = 0; i < sample_count; ++i) {
        ASSERT_EQ(samples[i], restored[i]);
    }

    ASSERT_TRUE(nmo_chunk_parser_at_end(parser));
    nmo_arena_destroy(arena);
}

/**
 * Ensure chunk clone + identifier seek logic stay in sync with the new helpers.
 */
TEST(chunk_special_cases, identifier_navigation_with_clone) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32 * 1024);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);
    nmo_chunk_writer_start(writer, 0x50000002, NMO_CHUNK_VERSION_4);

    const uint32_t section_a_id = 0x1000;
    const uint32_t section_b_id = 0x2000;

    const uint32_t times[] = {0, 10, 20, 30};
    const uint16_t values[] = {1000, 2000, 1500, 1750};

    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_identifier(writer, section_a_id));
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_array_dword_as_words(writer,
                                                                  times,
                                                                  sizeof(times) / sizeof(times[0])));
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_buffer_nosize_lendian16(writer,
                                                                     sizeof(values) / sizeof(values[0]),
                                                                     values));

    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_identifier(writer, section_b_id));
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_dword(writer, 0xCAFEBABE));
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_int(writer, -42));

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);

    nmo_chunk_t *clone = nmo_chunk_clone(chunk, arena);
    ASSERT_NOT_NULL(clone);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(clone);
    ASSERT_NOT_NULL(parser);

    size_t section_size = 0;
    ASSERT_EQ(NMO_OK,
              nmo_chunk_parser_seek_identifier_with_size(parser,
                                                          section_a_id,
                                                          &section_size));

    const size_t expected_a_size = (sizeof(times) / sizeof(times[0])) * 2
                                 + (sizeof(values) / sizeof(values[0]));
    ASSERT_EQ(expected_a_size, section_size);

    uint32_t decoded_times[sizeof(times) / sizeof(times[0])];
    uint16_t decoded_values[sizeof(values) / sizeof(values[0])];

    ASSERT_EQ(NMO_OK,
              nmo_chunk_parser_read_dword_array_as_words(parser,
                                                         decoded_times,
                                                         sizeof(times) / sizeof(times[0])));
    ASSERT_EQ(NMO_OK,
              nmo_chunk_parser_read_buffer_nosize_lendian16(parser,
                                                             sizeof(values) / sizeof(values[0]),
                                                             decoded_values));

    for (size_t i = 0; i < sizeof(times) / sizeof(times[0]); ++i) {
        ASSERT_EQ(times[i], decoded_times[i]);
        ASSERT_EQ(values[i], decoded_values[i]);
    }

    size_t section_b_size = 0;
    ASSERT_EQ(NMO_OK,
              nmo_chunk_parser_seek_identifier_with_size(parser,
                                                          section_b_id,
                                                          &section_b_size));
    ASSERT_EQ(2u, section_b_size);  // dword + int

    uint32_t tag = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_dword(parser, &tag));
    ASSERT_EQ(0xCAFEBABE, tag);

    int32_t sentinel = 0;
    ASSERT_EQ(NMO_OK, nmo_chunk_parser_read_int(parser, &sentinel));
    ASSERT_EQ(-42, sentinel);

    ASSERT_TRUE(nmo_chunk_parser_at_end(parser));
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_special_cases, array_helpers_round_trip);
    REGISTER_TEST(chunk_special_cases, identifier_navigation_with_clone);
TEST_MAIN_END()
