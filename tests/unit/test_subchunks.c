/**
 * @file test_subchunks.c
 * @brief Test for sub-chunk support implementation
 *
 * Tests StartSubChunkSequence, WriteSubChunkSequence, StartReadSequence, and ReadSubChunk
 * to ensure proper behavior matching CKStateChunk.
 */

#include "format/nmo_chunk_writer.h"
#include "format/nmo_chunk_parser.h"
#include "format/nmo_chunk.h"
#include "core/nmo_arena.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST(subchunks, create_and_write_subchunks) {
    // Create arena for parent chunk
    nmo_arena_t* parent_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(parent_arena);

    // Create arena for sub-chunks
    nmo_arena_t* sub_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(sub_arena);

    // ===== Create Sub-Chunk 1 =====
    nmo_chunk_writer_t* sub_writer1 = nmo_chunk_writer_create(sub_arena);
    ASSERT_NOT_NULL(sub_writer1);

    nmo_chunk_writer_start(sub_writer1, 0xAABBCCDD, 7);

    // Write some data to sub-chunk 1
    int result = nmo_chunk_writer_write_int(sub_writer1, 1000);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_writer_write_int(sub_writer1, 2000);
    ASSERT_EQ(result, NMO_OK);

    // Write object IDs to sub-chunk 1
    result = nmo_chunk_writer_write_object_id(sub_writer1, 5001);
    ASSERT_EQ(result, NMO_OK);

    nmo_chunk_t* sub1 = nmo_chunk_writer_finalize(sub_writer1);
    ASSERT_NOT_NULL(sub1);

    // ===== Create Sub-Chunk 2 =====
    nmo_chunk_writer_t* sub_writer2 = nmo_chunk_writer_create(sub_arena);
    ASSERT_NOT_NULL(sub_writer2);

    nmo_chunk_writer_start(sub_writer2, 0x11223344, 7);

    // Write some data to sub-chunk 2
    result = nmo_chunk_writer_write_int(sub_writer2, 3000);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_writer_write_float(sub_writer2, 42.5f);
    ASSERT_EQ(result, NMO_OK);

    nmo_chunk_t* sub2 = nmo_chunk_writer_finalize(sub_writer2);
    ASSERT_NOT_NULL(sub2);

    // ===== Create Parent Chunk with Sub-Chunks =====
    nmo_chunk_writer_t* parent_writer = nmo_chunk_writer_create(parent_arena);
    ASSERT_NOT_NULL(parent_writer);

    nmo_chunk_writer_start(parent_writer, 0x12345678, 7);

    // Write some data before sub-chunks
    result = nmo_chunk_writer_write_int(parent_writer, 999);
    ASSERT_EQ(result, NMO_OK);

    // Start sub-chunk sequence (2 sub-chunks)
    result = nmo_chunk_writer_start_subchunk_sequence(parent_writer, 2);
    ASSERT_EQ(result, NMO_OK);

    // Write sub-chunk 1
    result = nmo_chunk_writer_write_subchunk(parent_writer, sub1);
    ASSERT_EQ(result, NMO_OK);

    // Write sub-chunk 2
    result = nmo_chunk_writer_write_subchunk(parent_writer, sub2);
    ASSERT_EQ(result, NMO_OK);

    // Write some data after sub-chunks
    result = nmo_chunk_writer_write_int(parent_writer, 888);
    ASSERT_EQ(result, NMO_OK);

    // Finalize parent chunk
    nmo_chunk_t* parent = nmo_chunk_writer_finalize(parent_writer);
    ASSERT_NOT_NULL(parent);

    // Verify CHN option flag and chunk ref IntList layout
    ASSERT_TRUE((parent->chunk_options & NMO_CHUNK_OPTION_CHN) != 0);
    ASSERT_NOT_NULL(parent->chunk_refs);
    ASSERT_EQ(4u, parent->chunk_ref_count);
    ASSERT_EQ(0xFFFFFFFFu, parent->chunk_refs[0]);  // Sentinel before packed list
    ASSERT_EQ(0u, parent->chunk_refs[1]);          // Sequence header offset
    ASSERT_NE(0xFFFFFFFFu, parent->chunk_refs[2]);
    ASSERT_NE(0xFFFFFFFFu, parent->chunk_refs[3]);
    ASSERT_LT(parent->chunk_refs[2], parent->data_size);
    ASSERT_LT(parent->chunk_refs[3], parent->data_size);
    ASSERT_GT(parent->chunk_refs[3], parent->chunk_refs[2]);

    // Cleanup
    nmo_chunk_writer_destroy(parent_writer);
    nmo_chunk_writer_destroy(sub_writer1);
    nmo_chunk_writer_destroy(sub_writer2);
    nmo_arena_destroy(parent_arena);
    nmo_arena_destroy(sub_arena);
}

TEST(subchunks, read_subchunks) {
    // Create arena for parent chunk
    nmo_arena_t* parent_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(parent_arena);

    // Create arena for sub-chunks
    nmo_arena_t* sub_arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(sub_arena);

    // ===== Create Sub-Chunk 1 =====
    nmo_chunk_writer_t* sub_writer1 = nmo_chunk_writer_create(sub_arena);
    ASSERT_NOT_NULL(sub_writer1);

    nmo_chunk_writer_start(sub_writer1, 0xAABBCCDD, 7);

    // Write some data to sub-chunk 1
    int result = nmo_chunk_writer_write_int(sub_writer1, 1000);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_writer_write_int(sub_writer1, 2000);
    ASSERT_EQ(result, NMO_OK);

    // Write object IDs to sub-chunk 1
    result = nmo_chunk_writer_write_object_id(sub_writer1, 5001);
    ASSERT_EQ(result, NMO_OK);

    nmo_chunk_t* sub1 = nmo_chunk_writer_finalize(sub_writer1);
    ASSERT_NOT_NULL(sub1);

    // ===== Create Sub-Chunk 2 =====
    nmo_chunk_writer_t* sub_writer2 = nmo_chunk_writer_create(sub_arena);
    ASSERT_NOT_NULL(sub_writer2);

    nmo_chunk_writer_start(sub_writer2, 0x11223344, 7);

    // Write some data to sub-chunk 2
    result = nmo_chunk_writer_write_int(sub_writer2, 3000);
    ASSERT_EQ(result, NMO_OK);

    result = nmo_chunk_writer_write_float(sub_writer2, 42.5f);
    ASSERT_EQ(result, NMO_OK);

    nmo_chunk_t* sub2 = nmo_chunk_writer_finalize(sub_writer2);
    ASSERT_NOT_NULL(sub2);

    // ===== Create Parent Chunk with Sub-Chunks =====
    nmo_chunk_writer_t* parent_writer = nmo_chunk_writer_create(parent_arena);
    ASSERT_NOT_NULL(parent_writer);

    nmo_chunk_writer_start(parent_writer, 0x12345678, 7);

    // Write some data before sub-chunks
    result = nmo_chunk_writer_write_int(parent_writer, 999);
    ASSERT_EQ(result, NMO_OK);

    // Start sub-chunk sequence (2 sub-chunks)
    result = nmo_chunk_writer_start_subchunk_sequence(parent_writer, 2);
    ASSERT_EQ(result, NMO_OK);

    // Write sub-chunk 1
    result = nmo_chunk_writer_write_subchunk(parent_writer, sub1);
    ASSERT_EQ(result, NMO_OK);

    // Write sub-chunk 2
    result = nmo_chunk_writer_write_subchunk(parent_writer, sub2);
    ASSERT_EQ(result, NMO_OK);

    // Write some data after sub-chunks
    result = nmo_chunk_writer_write_int(parent_writer, 888);
    ASSERT_EQ(result, NMO_OK);

    // Finalize parent chunk
    nmo_chunk_t* parent = nmo_chunk_writer_finalize(parent_writer);
    ASSERT_NOT_NULL(parent);

    // ===== Read Back Parent Chunk =====
    nmo_chunk_parser_t* parent_parser = nmo_chunk_parser_create(parent);
    ASSERT_NOT_NULL(parent_parser);

    // Read data before sub-chunks
    int32_t value = 0;
    result = nmo_chunk_parser_read_int(parent_parser, &value);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(value, 999);

    // Start reading sub-chunk sequence
    int count = nmo_chunk_parser_start_read_sequence(parent_parser);
    ASSERT_EQ(count, 2);

    // Read sub-chunk 1
    nmo_chunk_t* read_sub1 = NULL;
    result = nmo_chunk_parser_read_subchunk(parent_parser, sub_arena, &read_sub1);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_NOT_NULL(read_sub1);
    ASSERT_EQ(read_sub1->class_id, 0xAABBCCDD);
    ASSERT_EQ(read_sub1->data_size, 3);
    ASSERT_EQ(read_sub1->id_count, 1);

    // Parse sub-chunk 1 data
    nmo_chunk_parser_t* sub_parser1 = nmo_chunk_parser_create(read_sub1);
    ASSERT_NOT_NULL(sub_parser1);

    result = nmo_chunk_parser_read_int(sub_parser1, &value);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(value, 1000);

    result = nmo_chunk_parser_read_int(sub_parser1, &value);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(value, 2000);

    nmo_object_id_t obj_id = 0;
    result = nmo_chunk_parser_read_object_id(sub_parser1, &obj_id);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(obj_id, 5001);

    nmo_chunk_parser_destroy(sub_parser1);

    // Read sub-chunk 2
    nmo_chunk_t* read_sub2 = NULL;
    result = nmo_chunk_parser_read_subchunk(parent_parser, sub_arena, &read_sub2);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_NOT_NULL(read_sub2);
    ASSERT_EQ(read_sub2->class_id, 0x11223344);
    ASSERT_EQ(read_sub2->data_size, 2);

    // Parse sub-chunk 2 data
    nmo_chunk_parser_t* sub_parser2 = nmo_chunk_parser_create(read_sub2);
    ASSERT_NOT_NULL(sub_parser2);

    result = nmo_chunk_parser_read_int(sub_parser2, &value);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(value, 3000);

    float float_value = 0.0f;
    result = nmo_chunk_parser_read_float(sub_parser2, &float_value);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_TRUE(float_value == 42.5f);

    nmo_chunk_parser_destroy(sub_parser2);

    // Read data after sub-chunks
    result = nmo_chunk_parser_read_int(parent_parser, &value);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_EQ(value, 888);

    // Cleanup
    nmo_chunk_parser_destroy(parent_parser);
    nmo_chunk_writer_destroy(parent_writer);
    nmo_chunk_writer_destroy(sub_writer1);
    nmo_chunk_writer_destroy(sub_writer2);
    nmo_arena_destroy(parent_arena);
    nmo_arena_destroy(sub_arena);
}

TEST(subchunks, standalone_subchunk_refs) {
    nmo_arena_t* parent_arena = nmo_arena_create(NULL, 2048);
    ASSERT_NOT_NULL(parent_arena);

    nmo_arena_t* sub_arena = nmo_arena_create(NULL, 2048);
    ASSERT_NOT_NULL(sub_arena);

    // Build a simple sub-chunk
    nmo_chunk_writer_t* sub_writer = nmo_chunk_writer_create(sub_arena);
    ASSERT_NOT_NULL(sub_writer);
    nmo_chunk_writer_start(sub_writer, 0x0F0F0F0F, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_int(sub_writer, 42));
    nmo_chunk_t* sub = nmo_chunk_writer_finalize(sub_writer);
    ASSERT_NOT_NULL(sub);

    // Parent without StartSubChunkSequence should still track offsets
    nmo_chunk_writer_t* parent_writer = nmo_chunk_writer_create(parent_arena);
    ASSERT_NOT_NULL(parent_writer);
    nmo_chunk_writer_start(parent_writer, 0x01020304, 7);
    ASSERT_EQ(NMO_OK, nmo_chunk_writer_write_subchunk(parent_writer, sub));
    nmo_chunk_t* parent = nmo_chunk_writer_finalize(parent_writer);
    ASSERT_NOT_NULL(parent);

    ASSERT_TRUE((parent->chunk_options & NMO_CHUNK_OPTION_CHN) != 0);
    ASSERT_EQ(1u, parent->chunk_ref_count);
    ASSERT_NOT_NULL(parent->chunk_refs);
    ASSERT_NE(0xFFFFFFFFu, parent->chunk_refs[0]);
    ASSERT_EQ(0u, parent->chunk_refs[0]);  // First entry starts at beginning
    ASSERT_LT(parent->chunk_refs[0], parent->data_size);

    nmo_chunk_writer_destroy(parent_writer);
    nmo_chunk_writer_destroy(sub_writer);
    nmo_arena_destroy(parent_arena);
    nmo_arena_destroy(sub_arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(subchunks, create_and_write_subchunks);
    REGISTER_TEST(subchunks, read_subchunks);
    REGISTER_TEST(subchunks, standalone_subchunk_refs);
TEST_MAIN_END()
