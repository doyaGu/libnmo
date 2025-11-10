/**
 * @file test_chunk_writer.c
 * @brief Unit tests for chunk writer
 */

#include "../test_framework.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_writer.h"
#include "format/nmo_chunk_parser.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <stdio.h>

// Test: Create writer and write primitives
static void test_writer_primitives(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 8192);
    if (arena == NULL) {
        printf("FAIL: test_writer_primitives - arena creation failed\n");
        return;
    }

    nmo_chunk_writer* writer = nmo_chunk_writer_create(arena);
    if (writer == NULL) {
        printf("FAIL: test_writer_primitives - writer creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_writer_start(writer, 12345, NMO_CHUNK_VERSION_4);

    // Write primitives
    if (nmo_chunk_writer_write_byte(writer, 0x78) != NMO_OK) {
        printf("FAIL: test_writer_primitives - write_byte failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    if (nmo_chunk_writer_write_word(writer, 0x5678) != NMO_OK) {
        printf("FAIL: test_writer_primitives - write_word failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    if (nmo_chunk_writer_write_dword(writer, 0x12345678) != NMO_OK) {
        printf("FAIL: test_writer_primitives - write_dword failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    if (nmo_chunk_writer_write_int(writer, -42) != NMO_OK) {
        printf("FAIL: test_writer_primitives - write_int failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    if (nmo_chunk_writer_write_float(writer, 3.14159f) != NMO_OK) {
        printf("FAIL: test_writer_primitives - write_float failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    nmo_guid test_guid = {0x11111111, 0x22222222};
    if (nmo_chunk_writer_write_guid(writer, test_guid) != NMO_OK) {
        printf("FAIL: test_writer_primitives - write_guid failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Finalize
    nmo_chunk* chunk = nmo_chunk_writer_finalize(writer);
    if (chunk == NULL) {
        printf("FAIL: test_writer_primitives - finalize failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Verify we wrote 7 DWORDs (byte, word, dword, int, float, guid1, guid2)
    if (chunk->data_size != 7) {
        printf("FAIL: test_writer_primitives - data size incorrect (got %zu, expected 7)\n", chunk->data_size);
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
    printf("PASS: test_writer_primitives\n");
}

// Test: Write and read roundtrip
static void test_writer_roundtrip(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 8192);
    if (arena == NULL) {
        printf("FAIL: test_writer_roundtrip - arena creation failed\n");
        return;
    }

    // Write data
    nmo_chunk_writer* writer = nmo_chunk_writer_create(arena);
    if (writer == NULL) {
        printf("FAIL: test_writer_roundtrip - writer creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_writer_start(writer, 100, NMO_CHUNK_VERSION_4);

    uint32_t test_value = 0xDEADBEEF;
    nmo_chunk_writer_write_dword(writer, test_value);

    float test_float = 2.71828f;
    nmo_chunk_writer_write_float(writer, test_float);

    const char* test_str = "Hello, Virtools!";
    nmo_chunk_writer_write_string(writer, test_str);

    nmo_chunk* chunk = nmo_chunk_writer_finalize(writer);
    if (chunk == NULL) {
        printf("FAIL: test_writer_roundtrip - finalize failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Read data back
    nmo_chunk_parser* parser = nmo_chunk_parser_create(chunk);
    if (parser == NULL) {
        printf("FAIL: test_writer_roundtrip - parser creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    uint32_t read_value;
    if (nmo_chunk_parser_read_dword(parser, &read_value) != NMO_OK) {
        printf("FAIL: test_writer_roundtrip - read_dword failed\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    if (read_value != test_value) {
        printf("FAIL: test_writer_roundtrip - dword mismatch\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    float read_float;
    if (nmo_chunk_parser_read_float(parser, &read_float) != NMO_OK) {
        printf("FAIL: test_writer_roundtrip - read_float failed\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    if (read_float < 2.71f || read_float > 2.72f) {
        printf("FAIL: test_writer_roundtrip - float mismatch\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    char* read_str = NULL;
    if (nmo_chunk_parser_read_string(parser, &read_str, arena) != NMO_OK) {
        printf("FAIL: test_writer_roundtrip - read_string failed\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    if (read_str == NULL || strcmp(read_str, test_str) != 0) {
        printf("FAIL: test_writer_roundtrip - string mismatch\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_parser_destroy(parser);
    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
    printf("PASS: test_writer_roundtrip\n");
}

// Test: Object ID tracking
static void test_writer_object_ids(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 8192);
    if (arena == NULL) {
        printf("FAIL: test_writer_object_ids - arena creation failed\n");
        return;
    }

    nmo_chunk_writer* writer = nmo_chunk_writer_create(arena);
    if (writer == NULL) {
        printf("FAIL: test_writer_object_ids - writer creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_writer_start(writer, 200, NMO_CHUNK_VERSION_4);
    nmo_chunk_writer_start_object_sequence(writer, 3);

    // Write some object IDs
    nmo_chunk_writer_write_object_id(writer, 1001);
    nmo_chunk_writer_write_object_id(writer, 1002);
    nmo_chunk_writer_write_object_id(writer, 1001);  // Duplicate

    nmo_chunk* chunk = nmo_chunk_writer_finalize(writer);
    if (chunk == NULL) {
        printf("FAIL: test_writer_object_ids - finalize failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Check that IDS option is set
    if (!(chunk->chunk_options & NMO_CHUNK_OPTION_IDS)) {
        printf("FAIL: test_writer_object_ids - IDS option not set\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Check that ID list has 2 unique IDs
    if (chunk->id_count != 2) {
        printf("FAIL: test_writer_object_ids - ID count incorrect (got %zu, expected 2)\n", chunk->id_count);
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
    printf("PASS: test_writer_object_ids\n");
}

// Test: Automatic buffer growth
static void test_writer_growth(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 65536);  // Larger arena
    if (arena == NULL) {
        printf("FAIL: test_writer_growth - arena creation failed\n");
        return;
    }

    nmo_chunk_writer* writer = nmo_chunk_writer_create(arena);
    if (writer == NULL) {
        printf("FAIL: test_writer_growth - writer creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_writer_start(writer, 300, NMO_CHUNK_VERSION_4);

    // Write more than initial capacity (100 DWORDs)
    for (int i = 0; i < 200; i++) {
        if (nmo_chunk_writer_write_dword(writer, (uint32_t)i) != NMO_OK) {
            printf("FAIL: test_writer_growth - write_dword failed at iteration %d\n", i);
            nmo_arena_destroy(arena);
            return;
        }
    }

    nmo_chunk* chunk = nmo_chunk_writer_finalize(writer);
    if (chunk == NULL) {
        printf("FAIL: test_writer_growth - finalize failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Verify we wrote 200 DWORDs
    if (chunk->data_size != 200) {
        printf("FAIL: test_writer_growth - data size incorrect (got %zu, expected 200)\n", chunk->data_size);
        nmo_arena_destroy(arena);
        return;
    }

    // Verify data
    for (size_t i = 0; i < 200; i++) {
        if (chunk->data[i] != (uint32_t)i) {
            printf("FAIL: test_writer_growth - data mismatch at index %zu\n", i);
            nmo_arena_destroy(arena);
            return;
        }
    }

    nmo_chunk_writer_destroy(writer);
    nmo_arena_destroy(arena);
    printf("PASS: test_writer_growth\n");
}

int main(void) {
    printf("Running chunk writer tests...\n\n");

    test_writer_primitives();
    test_writer_roundtrip();
    test_writer_object_ids();
    test_writer_growth();

    printf("\nAll chunk writer tests completed!\n");
    return 0;
}
