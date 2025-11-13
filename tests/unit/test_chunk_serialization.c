/**
 * @file test_chunk_serialization.c
 * @brief Unit tests for CKStateChunk serialization/deserialization
 *
 * This test verifies the fix for the Version Info packing bug described
 * in IMPLEMENTATION_PLAN.md Phase 1.
 */

#include "../test_framework.h"
#include "format/nmo_chunk.h"
#include "core/nmo_arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/**
 * Test that Version Info is packed and unpacked correctly
 *
 * The bug was that ChunkOptions and ChunkClassID were being placed in the wrong bytes.
 * According to VIRTOOLS_FILE_FORMAT_SPEC.md:
 *
 * Bits [0-7]:   DataVersion    (8 bits)
 * Bits [8-15]:  ChunkClassID   (8 bits)
 * Bits [16-23]: ChunkVersion   (8 bits)
 * Bits [24-31]: ChunkOptions   (8 bits)
 */
TEST(chunk_serialization, version_info_packing) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    // Create a chunk with specific version values
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    chunk->data_version = 0x12;       // Custom data version
    chunk->chunk_class_id = 0x34;     // Class ID
    chunk->chunk_version = 7;         // CHUNK_VERSION4
    chunk->chunk_options = 0x0F;      // Some options (will be OR'd with actual flags)

    // Add some data
    chunk->data_size = 2;
    chunk->data = (uint32_t *)nmo_arena_alloc(arena, 8, 4);
    chunk->data[0] = 0xAABBCCDD;
    chunk->data[1] = 0x11223344;

    // Add IDs to trigger CHNK_OPTION_IDS
    chunk->id_count = 1;
    chunk->ids = (uint32_t *)nmo_arena_alloc(arena, 4, 4);
    chunk->ids[0] = 0x99887766;

    // Serialize
    void *buffer = NULL;
    size_t buffer_size = 0;
    nmo_result_t result = nmo_chunk_serialize(chunk, &buffer, &buffer_size, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(buffer);
    ASSERT_GT(buffer_size, 0);

    // Deserialize
    nmo_chunk_t *chunk2 = NULL;
    result = nmo_chunk_deserialize(buffer, buffer_size, arena, &chunk2);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(chunk2);

    // Verify all fields match
    ASSERT_EQ(chunk2->data_version, 0x12);
    ASSERT_EQ(chunk2->chunk_class_id, 0x34);
    ASSERT_EQ(chunk2->chunk_version, 7);

    // Check that options were set correctly (should include CHNK_OPTION_IDS)
    ASSERT_NE((chunk2->chunk_options & NMO_CHUNK_OPTION_IDS), 0);

    // Verify data
    ASSERT_EQ(chunk2->data_size, 2);
    ASSERT_EQ(chunk2->data[0], 0xAABBCCDD);
    ASSERT_EQ(chunk2->data[1], 0x11223344);

    // Verify IDs
    ASSERT_EQ(chunk2->id_count, 1);
    ASSERT_EQ(chunk2->ids[0], 0x99887766);

    nmo_arena_destroy(arena);
}

/**
 * Test serialization with all optional lists
 */
TEST(chunk_serialization, full_serialization) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    chunk->data_version = 5;
    chunk->chunk_class_id = 0x42;
    chunk->chunk_version = 7;

    // Add data
    chunk->data_size = 3;
    chunk->data = (uint32_t *)nmo_arena_alloc(arena, 12, 4);
    chunk->data[0] = 100;
    chunk->data[1] = 200;
    chunk->data[2] = 300;

    // Add IDs
    chunk->id_count = 2;
    chunk->ids = (uint32_t *)nmo_arena_alloc(arena, 8, 4);
    chunk->ids[0] = 1001;
    chunk->ids[1] = 1002;

    // Add managers
    chunk->manager_count = 1;
    chunk->managers = (uint32_t *)nmo_arena_alloc(arena, 4, 4);
    chunk->managers[0] = 999;

    // Set chunk options to indicate presence of IDs and managers
    chunk->chunk_options = NMO_CHUNK_OPTION_IDS | NMO_CHUNK_OPTION_MAN;

    // Serialize and deserialize
    void *buffer = NULL;
    size_t buffer_size = 0;
    nmo_result_t result = nmo_chunk_serialize(chunk, &buffer, &buffer_size, arena);
    ASSERT_EQ(result.code, NMO_OK);

    nmo_chunk_t *chunk2 = NULL;
    result = nmo_chunk_deserialize(buffer, buffer_size, arena, &chunk2);
    ASSERT_EQ(result.code, NMO_OK);

    // Verify all data
    ASSERT_EQ(chunk2->data_version, 5);
    ASSERT_EQ(chunk2->chunk_class_id, 0x42);
    ASSERT_EQ(chunk2->chunk_version, 7);

    ASSERT_EQ(chunk2->data_size, 3);
    ASSERT_EQ(chunk2->data[0], 100);
    ASSERT_EQ(chunk2->data[1], 200);
    ASSERT_EQ(chunk2->data[2], 300);

    ASSERT_EQ(chunk2->id_count, 2);
    ASSERT_EQ(chunk2->ids[0], 1001);
    ASSERT_EQ(chunk2->ids[1], 1002);

    ASSERT_EQ(chunk2->manager_count, 1);
    ASSERT_EQ(chunk2->managers[0], 999);

    // Check options flags
    ASSERT_NE((chunk2->chunk_options & NMO_CHUNK_OPTION_IDS), 0);
    ASSERT_NE((chunk2->chunk_options & NMO_CHUNK_OPTION_MAN), 0);

    nmo_arena_destroy(arena);
}

/**
 * Test empty chunk serialization
 */
TEST(chunk_serialization, empty_chunk) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

    chunk->data_version = 1;
    chunk->chunk_class_id = 0xFF;
    chunk->chunk_version = 7;

    // No data, no lists

    // Serialize and deserialize
    void *buffer = NULL;
    size_t buffer_size = 0;
    nmo_result_t result = nmo_chunk_serialize(chunk, &buffer, &buffer_size, arena);
    ASSERT_EQ(result.code, NMO_OK);

    nmo_chunk_t *chunk2 = NULL;
    result = nmo_chunk_deserialize(buffer, buffer_size, arena, &chunk2);
    ASSERT_EQ(result.code, NMO_OK);

    // Verify
    ASSERT_EQ(chunk2->data_version, 1);
    ASSERT_EQ(chunk2->chunk_class_id, 0xFF);
    ASSERT_EQ(chunk2->chunk_version, 7);
    ASSERT_EQ(chunk2->data_size, 0);
    ASSERT_EQ(chunk2->id_count, 0);
    ASSERT_EQ(chunk2->manager_count, 0);

    nmo_arena_destroy(arena);
}

/**
 * Test round-trip with specific bit patterns to catch endianness issues
 */
TEST(chunk_serialization, bit_pattern_integrity) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    // Test with all possible byte values
    for (uint8_t dv = 0; dv < 16; dv++) {
        for (uint8_t cid = 0; cid < 16; cid++) {
            nmo_chunk_t *chunk = nmo_chunk_create(arena);

            chunk->data_version = dv;
            chunk->chunk_class_id = cid;
            chunk->chunk_version = 7;

            void *buffer = NULL;
            size_t buffer_size = 0;
            nmo_result_t result = nmo_chunk_serialize(chunk, &buffer, &buffer_size, arena);
            ASSERT_EQ(result.code, NMO_OK);

            nmo_chunk_t *chunk2 = NULL;
            result = nmo_chunk_deserialize(buffer, buffer_size, arena, &chunk2);
            ASSERT_EQ(result.code, NMO_OK);

            ASSERT_EQ(chunk2->data_version, dv);
            ASSERT_EQ(chunk2->chunk_class_id, cid);
        }
    }

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_serialization, version_info_packing);
    REGISTER_TEST(chunk_serialization, full_serialization);
    REGISTER_TEST(chunk_serialization, empty_chunk);
    REGISTER_TEST(chunk_serialization, bit_pattern_integrity);
TEST_MAIN_END()

