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
    chunk->chunk_options = 0x07;      // IDS|CHN|MAN (NOT FILE - IDS aren't serialized in file mode)

    // Add some data
    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 2);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);
    data[0] = 0xAABBCCDD;
    data[1] = 0x11223344;

    // Add IDs to trigger CHNK_OPTION_IDS
    resize_result = nmo_arena_array_resize(&chunk->ids, 1);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *ids = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->ids);
    ASSERT_NOT_NULL(ids);
    ids[0] = 0x99887766;

    // Serialize
    void *buffer = NULL;
    size_t buffer_size = 0;
    nmo_status_t result = nmo_chunk_serialize(chunk, &buffer, &buffer_size, arena);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_NOT_NULL(buffer);
    ASSERT_GT(buffer_size, 0);

    // Deserialize
    nmo_chunk_t *chunk2 = NULL;
    result = nmo_chunk_deserialize(buffer, buffer_size, arena, &chunk2);
    ASSERT_EQ(result, NMO_OK);
    ASSERT_NOT_NULL(chunk2);

    // Verify all fields match
    ASSERT_EQ(chunk2->data_version, 0x12);
    ASSERT_EQ(chunk2->chunk_class_id, 0x34);
    ASSERT_EQ(chunk2->chunk_version, 7);

    // Check that options were set correctly (should include CHNK_OPTION_IDS)
    ASSERT_NE((chunk2->chunk_options & NMO_CHUNK_OPTION_IDS), 0);

    // Verify data
    ASSERT_EQ(chunk2->data.count, 2);
    uint32_t *data2 = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk2->data);
    ASSERT_NOT_NULL(data2);
    ASSERT_EQ(data2[0], 0xAABBCCDD);
    ASSERT_EQ(data2[1], 0x11223344);

    // Verify IDs
    ASSERT_EQ(chunk2->ids.count, 1);
    uint32_t *ids2 = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk2->ids);
    ASSERT_NOT_NULL(ids2);
    ASSERT_EQ(ids2[0], 0x99887766);

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
    nmo_status_t resize_result = nmo_arena_array_resize(&chunk->data, 3);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_NOT_NULL(data);
    data[0] = 100;
    data[1] = 200;
    data[2] = 300;

    // Add IDs
    resize_result = nmo_arena_array_resize(&chunk->ids, 2);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *ids = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->ids);
    ASSERT_NOT_NULL(ids);
    ids[0] = 1001;
    ids[1] = 1002;

    // Add managers
    resize_result = nmo_arena_array_resize(&chunk->managers, 1);
    ASSERT_EQ(resize_result, NMO_OK);
    uint32_t *managers = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->managers);
    ASSERT_NOT_NULL(managers);
    managers[0] = 999;

    // Set chunk options to indicate presence of IDs and managers
    chunk->chunk_options = NMO_CHUNK_OPTION_IDS | NMO_CHUNK_OPTION_MAN;

    // Serialize and deserialize
    void *buffer = NULL;
    size_t buffer_size = 0;
    nmo_status_t result = nmo_chunk_serialize(chunk, &buffer, &buffer_size, arena);
    ASSERT_EQ(result, NMO_OK);

    nmo_chunk_t *chunk2 = NULL;
    result = nmo_chunk_deserialize(buffer, buffer_size, arena, &chunk2);
    ASSERT_EQ(result, NMO_OK);

    // Verify all data
    ASSERT_EQ(chunk2->data_version, 5);
    ASSERT_EQ(chunk2->chunk_class_id, 0x42);
    ASSERT_EQ(chunk2->chunk_version, 7);

    ASSERT_EQ(chunk2->data.count, 3);
    uint32_t *data2 = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk2->data);
    ASSERT_NOT_NULL(data2);
    ASSERT_EQ(data2[0], 100);
    ASSERT_EQ(data2[1], 200);
    ASSERT_EQ(data2[2], 300);

    ASSERT_EQ(chunk2->ids.count, 2);
    uint32_t *ids2 = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk2->ids);
    ASSERT_NOT_NULL(ids2);
    ASSERT_EQ(ids2[0], 1001);
    ASSERT_EQ(ids2[1], 1002);

    ASSERT_EQ(chunk2->managers.count, 1);
    uint32_t *managers2 = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk2->managers);
    ASSERT_NOT_NULL(managers2);
    ASSERT_EQ(managers2[0], 999);

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
    nmo_status_t result = nmo_chunk_serialize(chunk, &buffer, &buffer_size, arena);
    ASSERT_EQ(result, NMO_OK);

    nmo_chunk_t *chunk2 = NULL;
    result = nmo_chunk_deserialize(buffer, buffer_size, arena, &chunk2);
    ASSERT_EQ(result, NMO_OK);

    // Verify
    ASSERT_EQ(chunk2->data_version, 1);
    ASSERT_EQ(chunk2->chunk_class_id, 0xFF);
    ASSERT_EQ(chunk2->chunk_version, 7);
    ASSERT_EQ(chunk2->data.count, 0);
    ASSERT_EQ(chunk2->ids.count, 0);
    ASSERT_EQ(chunk2->managers.count, 0);

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
            nmo_status_t result = nmo_chunk_serialize(chunk, &buffer, &buffer_size, arena);
            ASSERT_EQ(result, NMO_OK);

            nmo_chunk_t *chunk2 = NULL;
            result = nmo_chunk_deserialize(buffer, buffer_size, arena, &chunk2);
            ASSERT_EQ(result, NMO_OK);

            ASSERT_EQ(chunk2->data_version, dv);
            ASSERT_EQ(chunk2->chunk_class_id, cid);
        }
    }

    nmo_arena_destroy(arena);
}

TEST(chunk_serialization, rejects_invalid_public_array_state) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_chunk_t *chunk = nmo_chunk_create(arena);
    ASSERT_NOT_NULL(chunk);

#if SIZE_MAX > UINT32_MAX
    nmo_arena_array_t *arrays[] = {
        &chunk->data,
        &chunk->ids,
        &chunk->chunk_refs,
        &chunk->managers,
    };
    for (size_t i = 0; i < sizeof(arrays) / sizeof(arrays[0]); ++i) {
        arrays[i]->count = (size_t)UINT32_MAX + 1u;
        arrays[i]->data = (void *)(uintptr_t)1;

        void *output = (void *)(uintptr_t)1;
        size_t output_size = 123;
        ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
            nmo_chunk_serialize(chunk, &output, &output_size, arena));
        ASSERT_NULL(output);
        ASSERT_EQ(0u, output_size);

        output = (void *)(uintptr_t)1;
        output_size = 123;
        ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
            nmo_chunk_serialize_version1(
                chunk, &output, &output_size, arena));
        ASSERT_NULL(output);
        ASSERT_EQ(0u, output_size);

        arrays[i]->count = 0;
        arrays[i]->data = NULL;
    }
#endif

    chunk->data.count = 1;
    void *output = (void *)(uintptr_t)1;
    size_t output_size = 123;
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
        nmo_chunk_serialize(chunk, &output, &output_size, arena));
    ASSERT_NULL(output);
    ASSERT_EQ(0u, output_size);

    output = (void *)(uintptr_t)1;
    output_size = 123;
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
        nmo_chunk_serialize_version1(chunk, &output, &output_size, arena));
    ASSERT_NULL(output);
    ASSERT_EQ(0u, output_size);

    nmo_arena_destroy(arena);
}

TEST(chunk_serialization, rejects_truncated_counts_before_allocation) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    const uint32_t data_payload[] = {
        (uint32_t)NMO_CHUNK_VERSION4 << 16,
        UINT32_MAX,
    };
    nmo_chunk_t *chunk = (nmo_chunk_t *)(uintptr_t)1;
    ASSERT_EQ(NMO_ERR_BUFFER_OVERRUN,
        nmo_chunk_deserialize(
            data_payload, sizeof(data_payload), arena, &chunk));
    ASSERT_NULL(chunk);

    const uint32_t option_flags[] = {
        NMO_CHUNK_OPTION_IDS,
        NMO_CHUNK_OPTION_CHN,
        NMO_CHUNK_OPTION_MAN,
    };
    for (size_t i = 0;
         i < sizeof(option_flags) / sizeof(option_flags[0]); ++i) {
        const uint32_t optional_payload[] = {
            ((uint32_t)NMO_CHUNK_VERSION4 << 16) |
                (option_flags[i] << 24),
            0,
            UINT32_MAX,
        };
        chunk = (nmo_chunk_t *)(uintptr_t)1;
        ASSERT_EQ(NMO_ERR_BUFFER_OVERRUN,
            nmo_chunk_deserialize(
                optional_payload, sizeof(optional_payload), arena, &chunk));
        ASSERT_NULL(chunk);
    }

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(chunk_serialization, version_info_packing);
    REGISTER_TEST(chunk_serialization, full_serialization);
    REGISTER_TEST(chunk_serialization, empty_chunk);
    REGISTER_TEST(chunk_serialization, bit_pattern_integrity);
    REGISTER_TEST(chunk_serialization, rejects_invalid_public_array_state);
    REGISTER_TEST(chunk_serialization, rejects_truncated_counts_before_allocation);
TEST_MAIN_END()
