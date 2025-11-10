/**
 * @file test_chunk_core.c
 * @brief Unit tests for CKStateChunk core serialization
 */

#include "format/nmo_chunk.h"
#include "core/nmo_arena.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Test basic chunk creation */
static void test_chunk_create(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 0);
    assert(arena != NULL);

    nmo_chunk* chunk = nmo_chunk_create(arena);
    assert(chunk != NULL);
    assert(chunk->chunk_version == NMO_CHUNK_VERSION_4);
    assert(chunk->owns_data == 1);
    assert(chunk->arena == arena);
    assert(chunk->data == NULL);
    assert(chunk->data_size == 0);

    nmo_arena_destroy(arena);
    printf("PASS: test_chunk_create\n");
}

/* Test chunk serialization with empty data */
static void test_chunk_serialize_empty(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 0);
    assert(arena != NULL);

    nmo_chunk* chunk = nmo_chunk_create(arena);
    assert(chunk != NULL);

    /* Set some basic properties */
    chunk->data_version = 1;
    chunk->chunk_class_id = 42;

    void* out_data = NULL;
    size_t out_size = 0;

    nmo_result result = nmo_chunk_serialize(chunk, &out_data, &out_size, arena);
    assert(result.code == NMO_OK);
    assert(out_data != NULL);
    assert(out_size == 8); /* version_info (4) + chunk_size (4) */

    nmo_arena_destroy(arena);
    printf("PASS: test_chunk_serialize_empty\n");
}

/* Test chunk serialization/deserialization round-trip */
static void test_chunk_roundtrip(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 0);
    assert(arena != NULL);

    /* Create chunk with some data */
    nmo_chunk* chunk = nmo_chunk_create(arena);
    assert(chunk != NULL);

    chunk->data_version = 5;
    chunk->chunk_class_id = 123;
    chunk->chunk_options = NMO_CHUNK_OPTION_IDS;

    /* Allocate and fill data buffer (2 DWORDs) */
    chunk->data_size = 2;
    chunk->data_capacity = 2;
    chunk->data = nmo_arena_alloc(arena, 2 * sizeof(uint32_t), 4);
    assert(chunk->data != NULL);
    chunk->data[0] = 0xDEADBEEF;
    chunk->data[1] = 0xCAFEBABE;

    /* Add some object IDs */
    chunk->id_count = 3;
    chunk->id_capacity = 3;
    chunk->ids = nmo_arena_alloc(arena, 3 * sizeof(uint32_t), 4);
    assert(chunk->ids != NULL);
    chunk->ids[0] = 100;
    chunk->ids[1] = 200;
    chunk->ids[2] = 300;

    /* Serialize */
    void* serialized_data = NULL;
    size_t serialized_size = 0;
    nmo_result result = nmo_chunk_serialize(chunk, &serialized_data, &serialized_size, arena);
    assert(result.code == NMO_OK);
    assert(serialized_data != NULL);
    assert(serialized_size > 0);

    /* Expected size:
     * 4 (version_info) + 4 (chunk_size) + 8 (data) + 4 (id_count) + 12 (ids) = 32 bytes
     */
    assert(serialized_size == 32);

    /* Deserialize */
    nmo_chunk* deserialized = NULL;
    result = nmo_chunk_deserialize(serialized_data, serialized_size, arena, &deserialized);
    assert(result.code == NMO_OK);
    assert(deserialized != NULL);

    /* Verify deserialized data */
    assert(deserialized->data_version == 5);
    assert(deserialized->chunk_class_id == 123);
    assert(deserialized->chunk_version == NMO_CHUNK_VERSION_4);
    assert(deserialized->chunk_options == NMO_CHUNK_OPTION_IDS);
    assert(deserialized->data_size == 2);
    assert(deserialized->data != NULL);
    assert(deserialized->data[0] == 0xDEADBEEF);
    assert(deserialized->data[1] == 0xCAFEBABE);
    assert(deserialized->id_count == 3);
    assert(deserialized->ids != NULL);
    assert(deserialized->ids[0] == 100);
    assert(deserialized->ids[1] == 200);
    assert(deserialized->ids[2] == 300);

    nmo_arena_destroy(arena);
    printf("PASS: test_chunk_roundtrip\n");
}

/* Test chunk with sub-chunks */
static void test_chunk_with_subchunks(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 0);
    assert(arena != NULL);

    /* Create parent chunk */
    nmo_chunk* parent = nmo_chunk_create(arena);
    assert(parent != NULL);

    parent->data_version = 1;
    parent->chunk_class_id = 10;
    parent->chunk_options = NMO_CHUNK_OPTION_CHN;

    /* Allocate sub-chunks array */
    parent->chunk_count = 2;
    parent->chunk_capacity = 2;
    parent->chunks = nmo_arena_alloc(arena, 2 * sizeof(nmo_chunk*), sizeof(void*));
    assert(parent->chunks != NULL);

    /* Create first sub-chunk */
    nmo_chunk* sub1 = nmo_chunk_create(arena);
    assert(sub1 != NULL);
    sub1->data_version = 2;
    sub1->chunk_class_id = 20;
    parent->chunks[0] = sub1;

    /* Create second sub-chunk */
    nmo_chunk* sub2 = nmo_chunk_create(arena);
    assert(sub2 != NULL);
    sub2->data_version = 3;
    sub2->chunk_class_id = 30;
    parent->chunks[1] = sub2;

    /* Serialize */
    void* serialized_data = NULL;
    size_t serialized_size = 0;
    nmo_result result = nmo_chunk_serialize(parent, &serialized_data, &serialized_size, arena);
    assert(result.code == NMO_OK);
    assert(serialized_data != NULL);

    /* Deserialize */
    nmo_chunk* deserialized = NULL;
    result = nmo_chunk_deserialize(serialized_data, serialized_size, arena, &deserialized);
    assert(result.code == NMO_OK);
    assert(deserialized != NULL);

    /* Verify */
    assert(deserialized->chunk_options & NMO_CHUNK_OPTION_CHN);
    assert(deserialized->chunk_count == 2);
    assert(deserialized->chunks != NULL);
    assert(deserialized->chunks[0]->data_version == 2);
    assert(deserialized->chunks[0]->chunk_class_id == 20);
    assert(deserialized->chunks[1]->data_version == 3);
    assert(deserialized->chunks[1]->chunk_class_id == 30);

    nmo_arena_destroy(arena);
    printf("PASS: test_chunk_with_subchunks\n");
}

/* Test chunk with all options */
static void test_chunk_full_options(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 0);
    assert(arena != NULL);

    /* Create chunk with all options */
    nmo_chunk* chunk = nmo_chunk_create(arena);
    assert(chunk != NULL);

    chunk->data_version = 10;
    chunk->chunk_class_id = 255;
    chunk->chunk_options = NMO_CHUNK_OPTION_IDS | NMO_CHUNK_OPTION_MAN;

    /* Add data */
    chunk->data_size = 1;
    chunk->data_capacity = 1;
    chunk->data = nmo_arena_alloc(arena, sizeof(uint32_t), 4);
    chunk->data[0] = 0x12345678;

    /* Add IDs */
    chunk->id_count = 2;
    chunk->id_capacity = 2;
    chunk->ids = nmo_arena_alloc(arena, 2 * sizeof(uint32_t), 4);
    chunk->ids[0] = 1000;
    chunk->ids[1] = 2000;

    /* Add managers */
    chunk->manager_count = 2;
    chunk->manager_capacity = 2;
    chunk->managers = nmo_arena_alloc(arena, 2 * sizeof(uint32_t), 4);
    chunk->managers[0] = 5;
    chunk->managers[1] = 10;

    /* Serialize */
    void* serialized_data = NULL;
    size_t serialized_size = 0;
    nmo_result result = nmo_chunk_serialize(chunk, &serialized_data, &serialized_size, arena);
    assert(result.code == NMO_OK);

    /* Deserialize */
    nmo_chunk* deserialized = NULL;
    result = nmo_chunk_deserialize(serialized_data, serialized_size, arena, &deserialized);
    assert(result.code == NMO_OK);

    /* Verify all data */
    assert(deserialized->data_version == 10);
    assert(deserialized->chunk_class_id == 255);
    assert(deserialized->data[0] == 0x12345678);
    assert(deserialized->id_count == 2);
    assert(deserialized->ids[0] == 1000);
    assert(deserialized->ids[1] == 2000);
    assert(deserialized->manager_count == 2);
    assert(deserialized->managers[0] == 5);
    assert(deserialized->managers[1] == 10);

    nmo_arena_destroy(arena);
    printf("PASS: test_chunk_full_options\n");
}

int main(void) {
    printf("Running CKStateChunk core tests...\n\n");

    test_chunk_create();
    test_chunk_serialize_empty();
    test_chunk_roundtrip();
    test_chunk_with_subchunks();
    test_chunk_full_options();

    printf("\nAll tests passed!\n");
    return 0;
}
