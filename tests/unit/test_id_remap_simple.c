/**
 * @file test_id_remap_simple.c
 * @brief Simple tests for ID remapping
 */

#include <stdio.h>
#include "session/nmo_id_remap.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_writer.h"
#include "core/nmo_arena.h"

// Test: Create and use ID remap table
static void test_id_remap_basic(void) {
    nmo_id_remap_t* remap = nmo_id_remap_create(16);
    if (remap == NULL) {
        printf("FAIL: test_id_remap_basic - create failed\n");
        return;
    }

    // Add mappings
    nmo_id_remap_add_mapping(remap, 1001, 2001);
    nmo_id_remap_add_mapping(remap, 1002, 2002);
    nmo_id_remap_add_mapping(remap, 1003, 2003);

    // Check count
    if (nmo_id_remap_get_count(remap) != 3) {
        printf("FAIL: test_id_remap_basic - count incorrect\n");
        nmo_id_remap_destroy(remap);
        return;
    }

    // Get mappings
    uint32_t new_id;
    nmo_result_t result = nmo_id_remap_get_mapping(remap, 1001, &new_id);
    if (result.code != NMO_OK || new_id != 2001) {
        printf("FAIL: test_id_remap_basic - get mapping 1001 failed\n");
        nmo_id_remap_destroy(remap);
        return;
    }

    result = nmo_id_remap_get_mapping(remap, 1002, &new_id);
    if (result.code != NMO_OK || new_id != 2002) {
        printf("FAIL: test_id_remap_basic - get mapping 1002 failed\n");
        nmo_id_remap_destroy(remap);
        return;
    }

    // Check non-existent mapping
    result = nmo_id_remap_get_mapping(remap, 9999, &new_id);
    if (result.code == NMO_OK) {
        printf("FAIL: test_id_remap_basic - non-existent mapping should fail\n");
        nmo_id_remap_destroy(remap);
        return;
    }

    nmo_id_remap_destroy(remap);
    printf("PASS: test_id_remap_basic\n");
}

// Test: Chunk ID remapping
static void test_chunk_remap(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 8192);
    if (arena == NULL) {
        printf("FAIL: test_chunk_remap - arena creation failed\n");
        return;
    }

    // Create chunk with object IDs
    nmo_chunk_writer* writer = nmo_chunk_writer_create(arena);
    if (writer == NULL) {
        printf("FAIL: test_chunk_remap - writer creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_writer_start(writer, 100, NMO_CHUNK_VERSION_4);
    nmo_chunk_writer_start_object_sequence(writer, 3);
    nmo_chunk_writer_write_object_id(writer, 1001);
    nmo_chunk_writer_write_object_id(writer, 1002);
    nmo_chunk_writer_write_object_id(writer, 1003);

    nmo_chunk* chunk = nmo_chunk_writer_finalize(writer);
    if (chunk == NULL) {
        printf("FAIL: test_chunk_remap - finalize failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Create remap table
    nmo_id_remap_t* remap = nmo_id_remap_create(16);
    if (remap == NULL) {
        printf("FAIL: test_chunk_remap - remap creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    nmo_id_remap_add_mapping(remap, 1001, 5001);
    nmo_id_remap_add_mapping(remap, 1002, 5002);
    nmo_id_remap_add_mapping(remap, 1003, 5003);

    // Remap chunk IDs
    int result = nmo_chunk_remap_ids(chunk, remap);
    if (result != NMO_OK) {
        printf("FAIL: test_chunk_remap - remap failed\n");
        nmo_id_remap_destroy(remap);
        nmo_arena_destroy(arena);
        return;
    }

    // Verify remapped IDs
    if (chunk->id_count != 3) {
        printf("FAIL: test_chunk_remap - ID count incorrect\n");
        nmo_id_remap_destroy(remap);
        nmo_arena_destroy(arena);
        return;
    }

    if (chunk->ids[0] != 5001 || chunk->ids[1] != 5002 || chunk->ids[2] != 5003) {
        printf("FAIL: test_chunk_remap - remapped IDs incorrect\n");
        nmo_id_remap_destroy(remap);
        nmo_arena_destroy(arena);
        return;
    }

    nmo_id_remap_destroy(remap);
    nmo_arena_destroy(arena);
    printf("PASS: test_chunk_remap\n");
}

// Test: Remap table growth
static void test_remap_growth(void) {
    nmo_id_remap_t* remap = nmo_id_remap_create(4);  // Small initial size
    if (remap == NULL) {
        printf("FAIL: test_remap_growth - create failed\n");
        return;
    }

    // Add many mappings to trigger resize
    for (uint32_t i = 0; i < 100; i++) {
        nmo_id_remap_add_mapping(remap, 1000 + i, 2000 + i);
    }

    // Verify count
    if (nmo_id_remap_get_count(remap) != 100) {
        printf("FAIL: test_remap_growth - count incorrect\n");
        nmo_id_remap_destroy(remap);
        return;
    }

    // Verify some mappings
    uint32_t new_id;
    nmo_result_t result = nmo_id_remap_get_mapping(remap, 1050, &new_id);
    if (result.code != NMO_OK || new_id != 2050) {
        printf("FAIL: test_remap_growth - mapping verification failed\n");
        nmo_id_remap_destroy(remap);
        return;
    }

    nmo_id_remap_destroy(remap);
    printf("PASS: test_remap_growth\n");
}

int main(void) {
    printf("Running ID remap tests...\n\n");

    test_id_remap_basic();
    test_chunk_remap();
    test_remap_growth();

    printf("\nAll ID remap tests completed!\n");
    return 0;
}
