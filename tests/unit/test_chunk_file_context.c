/**
 * @file test_chunk_file_context.c
 * @brief Validate chunk writer/parser file-context remapping.
 */

#include "test_framework.h"
#include "format/nmo_chunk_writer.h"
#include "format/nmo_chunk_parser.h"
#include "format/nmo_chunk_context.h"
#include "format/nmo_id_remap.h"
#include "core/nmo_arena.h"

TEST(chunk_file_context, round_trip_runtime_ids) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* Build remap tables */
    nmo_id_remap_t *runtime_to_file = nmo_id_remap_create(arena);
    nmo_id_remap_t *file_to_runtime = nmo_id_remap_create(arena);
    ASSERT_NOT_NULL(runtime_to_file);
    ASSERT_NOT_NULL(file_to_runtime);

    ASSERT_EQ(nmo_id_remap_add(runtime_to_file, 1001, 5).code, NMO_OK);
    ASSERT_EQ(nmo_id_remap_add(runtime_to_file, 2002, 6).code, NMO_OK);
    ASSERT_EQ(nmo_id_remap_add(file_to_runtime, 5, 1001).code, NMO_OK);
    ASSERT_EQ(nmo_id_remap_add(file_to_runtime, 6, 2002).code, NMO_OK);

    nmo_chunk_file_context_t ctx = {
        .runtime_to_file = runtime_to_file,
        .file_to_runtime = file_to_runtime
    };

    /* Write chunk with runtime IDs */
    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);
    nmo_chunk_writer_set_file_context(writer, &ctx);
    nmo_chunk_writer_start(writer, 0x11112222, NMO_CHUNK_VERSION_4);

    ASSERT_EQ(nmo_chunk_writer_write_object_id(writer, 1001), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_write_object_id(writer, 2002), NMO_OK);

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);
    ASSERT_NE(0u, chunk->chunk_options & NMO_CHUNK_OPTION_FILE);

    /* Parse and expect runtime IDs back */
    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);
    nmo_chunk_parser_set_file_context(parser, &ctx);

    nmo_object_id_t id = 0;
    nmo_result_t parse_result = nmo_chunk_parser_read_object_id(parser, &id);
    ASSERT_EQ(parse_result.code, NMO_OK);
    ASSERT_EQ(id, (nmo_object_id_t)1001);
    parse_result = nmo_chunk_parser_read_object_id(parser, &id);
    ASSERT_EQ(parse_result.code, NMO_OK);
    ASSERT_EQ(id, (nmo_object_id_t)2002);

    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

TEST(chunk_file_context, disabled_context_uses_raw_ids) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 2048);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *writer = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(writer);
    /* No context set */
    nmo_chunk_writer_start(writer, 0x3333, NMO_CHUNK_VERSION_4);
    ASSERT_EQ(nmo_chunk_writer_write_object_id(writer, 42), NMO_OK);
    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(writer);
    ASSERT_NOT_NULL(chunk);
    ASSERT_EQ(0u, chunk->chunk_options & NMO_CHUNK_OPTION_FILE);

    nmo_chunk_parser_t *parser = nmo_chunk_parser_create(chunk);
    ASSERT_NOT_NULL(parser);
    /* No context set */
    nmo_object_id_t id = 0;
    nmo_result_t parse_result = nmo_chunk_parser_read_object_id(parser, &id);
    ASSERT_EQ(parse_result.code, NMO_OK);
    ASSERT_EQ(id, (nmo_object_id_t)42);

    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_file_context, round_trip_runtime_ids);
    REGISTER_TEST(chunk_file_context, disabled_context_uses_raw_ids);
TEST_MAIN_END()
