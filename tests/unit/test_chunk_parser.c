/**
 * @file test_chunk_parser.c
 * @brief Unit tests for chunk parser
 */

#include "../test_framework.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_parser.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <stdio.h>

// Test: Create and destroy parser
static void test_parser_create_destroy(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 4096);
    if (arena == NULL) {
        printf("FAIL: test_parser_create_destroy - arena creation failed\n");
        return;
    }

    nmo_chunk* chunk = nmo_chunk_create(arena);
    if (chunk == NULL) {
        printf("FAIL: test_parser_create_destroy - chunk creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_parser* parser = nmo_chunk_parser_create(chunk);
    if (parser == NULL) {
        printf("FAIL: test_parser_create_destroy - parser creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
    printf("PASS: test_parser_create_destroy\n");
}

// Test: Cursor operations
static void test_parser_cursor(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 4096);
    if (arena == NULL) {
        printf("FAIL: test_parser_cursor - arena creation failed\n");
        return;
    }

    // Create chunk with some data
    nmo_chunk* chunk = nmo_chunk_create(arena);
    if (chunk == NULL) {
        printf("FAIL: test_parser_cursor - chunk creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Add 10 DWORDs of data
    chunk->data_size = 10;
    chunk->data = (uint32_t*)nmo_arena_alloc(arena, 10 * sizeof(uint32_t), sizeof(uint32_t));
    if (chunk->data == NULL) {
        printf("FAIL: test_parser_cursor - data allocation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_parser* parser = nmo_chunk_parser_create(chunk);
    if (parser == NULL) {
        printf("FAIL: test_parser_cursor - parser creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Test tell/seek/skip
    if (nmo_chunk_parser_tell(parser) != 0) {
        printf("FAIL: test_parser_cursor - initial position not 0\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    if (nmo_chunk_parser_seek(parser, 5) != NMO_OK) {
        printf("FAIL: test_parser_cursor - seek failed\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    if (nmo_chunk_parser_tell(parser) != 5) {
        printf("FAIL: test_parser_cursor - position after seek incorrect\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    if (nmo_chunk_parser_skip(parser, 3) != NMO_OK) {
        printf("FAIL: test_parser_cursor - skip failed\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    if (nmo_chunk_parser_tell(parser) != 8) {
        printf("FAIL: test_parser_cursor - position after skip incorrect\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    if (nmo_chunk_parser_remaining(parser) != 2) {
        printf("FAIL: test_parser_cursor - remaining DWORDs incorrect\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
    printf("PASS: test_parser_cursor\n");
}

// Test: Primitive reads
static void test_parser_primitives(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 4096);
    if (arena == NULL) {
        printf("FAIL: test_parser_primitives - arena creation failed\n");
        return;
    }

    nmo_chunk* chunk = nmo_chunk_create(arena);
    if (chunk == NULL) {
        printf("FAIL: test_parser_primitives - chunk creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Create test data
    chunk->data_size = 10;
    chunk->data = (uint32_t*)nmo_arena_alloc(arena, 10 * sizeof(uint32_t), sizeof(uint32_t));
    if (chunk->data == NULL) {
        printf("FAIL: test_parser_primitives - data allocation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    chunk->data[0] = 0x12345678;  // For byte/word/dword tests
    chunk->data[1] = 0xDEADBEEF;  // For int test
    float test_float = 3.14159f;
    memcpy(&chunk->data[2], &test_float, sizeof(float));  // For float test
    chunk->data[3] = 0x11111111;  // GUID part 1
    chunk->data[4] = 0x22222222;  // GUID part 2

    nmo_chunk_parser* parser = nmo_chunk_parser_create(chunk);
    if (parser == NULL) {
        printf("FAIL: test_parser_primitives - parser creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Test byte read
    uint8_t byte_val;
    if (nmo_chunk_parser_read_byte(parser, &byte_val) != NMO_OK) {
        printf("FAIL: test_parser_primitives - read_byte failed\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }
    if (byte_val != 0x78) {
        printf("FAIL: test_parser_primitives - byte value incorrect (got 0x%02X, expected 0x78)\n", byte_val);
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    // Reset for word test
    nmo_chunk_parser_seek(parser, 0);
    uint16_t word_val;
    if (nmo_chunk_parser_read_word(parser, &word_val) != NMO_OK) {
        printf("FAIL: test_parser_primitives - read_word failed\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }
    if (word_val != 0x5678) {
        printf("FAIL: test_parser_primitives - word value incorrect (got 0x%04X, expected 0x5678)\n", word_val);
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    // Reset for dword test
    nmo_chunk_parser_seek(parser, 0);
    uint32_t dword_val;
    if (nmo_chunk_parser_read_dword(parser, &dword_val) != NMO_OK) {
        printf("FAIL: test_parser_primitives - read_dword failed\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }
    if (dword_val != 0x12345678) {
        printf("FAIL: test_parser_primitives - dword value incorrect\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    // Test int read
    int32_t int_val;
    if (nmo_chunk_parser_read_int(parser, &int_val) != NMO_OK) {
        printf("FAIL: test_parser_primitives - read_int failed\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    // Test float read
    float float_val;
    if (nmo_chunk_parser_read_float(parser, &float_val) != NMO_OK) {
        printf("FAIL: test_parser_primitives - read_float failed\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }
    if (float_val < 3.14f || float_val > 3.15f) {
        printf("FAIL: test_parser_primitives - float value incorrect\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    // Test GUID read
    nmo_guid guid_val;
    if (nmo_chunk_parser_read_guid(parser, &guid_val) != NMO_OK) {
        printf("FAIL: test_parser_primitives - read_guid failed\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }
    if (guid_val.d1 != 0x11111111 || guid_val.d2 != 0x22222222) {
        printf("FAIL: test_parser_primitives - GUID value incorrect\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
    printf("PASS: test_parser_primitives\n");
}

// Test: String read
static void test_parser_string(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 4096);
    if (arena == NULL) {
        printf("FAIL: test_parser_string - arena creation failed\n");
        return;
    }

    nmo_chunk* chunk = nmo_chunk_create(arena);
    if (chunk == NULL) {
        printf("FAIL: test_parser_string - chunk creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Create string data: [length][data padded to DWORD]
    const char* test_str = "Hello";
    uint32_t str_len = (uint32_t)strlen(test_str);

    chunk->data_size = 3;  // 1 for length + 2 for "Hello" (5 bytes -> 2 DWORDs)
    chunk->data = (uint32_t*)nmo_arena_alloc(arena, 3 * sizeof(uint32_t), sizeof(uint32_t));
    if (chunk->data == NULL) {
        printf("FAIL: test_parser_string - data allocation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    chunk->data[0] = str_len;
    memcpy(&chunk->data[1], test_str, str_len);

    nmo_chunk_parser* parser = nmo_chunk_parser_create(chunk);
    if (parser == NULL) {
        printf("FAIL: test_parser_string - parser creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    char* read_str = NULL;
    if (nmo_chunk_parser_read_string(parser, &read_str, arena) != NMO_OK) {
        printf("FAIL: test_parser_string - read_string failed\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    if (read_str == NULL || strcmp(read_str, test_str) != 0) {
        printf("FAIL: test_parser_string - string value incorrect\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
    printf("PASS: test_parser_string\n");
}

// Test: Identifier navigation
static void test_parser_identifier(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 4096);
    if (arena == NULL) {
        printf("FAIL: test_parser_identifier - arena creation failed\n");
        return;
    }

    nmo_chunk* chunk = nmo_chunk_create(arena);
    if (chunk == NULL) {
        printf("FAIL: test_parser_identifier - chunk creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Create data with identifier at position 5
    chunk->data_size = 10;
    chunk->data = (uint32_t*)nmo_arena_alloc(arena, 10 * sizeof(uint32_t), sizeof(uint32_t));
    if (chunk->data == NULL) {
        printf("FAIL: test_parser_identifier - data allocation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    for (int i = 0; i < 10; i++) {
        chunk->data[i] = i;
    }
    chunk->data[5] = 0xDEADBEEF;  // Identifier

    nmo_chunk_parser* parser = nmo_chunk_parser_create(chunk);
    if (parser == NULL) {
        printf("FAIL: test_parser_identifier - parser creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Seek to identifier
    if (nmo_chunk_parser_seek_identifier(parser, 0xDEADBEEF) != NMO_OK) {
        printf("FAIL: test_parser_identifier - seek_identifier failed\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    // Should be at position 6 (after identifier)
    if (nmo_chunk_parser_tell(parser) != 6) {
        printf("FAIL: test_parser_identifier - position after seek_identifier incorrect\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
    printf("PASS: test_parser_identifier\n");
}

// Test: Bounds checking
static void test_parser_bounds(void) {
    nmo_arena* arena = nmo_arena_create(NULL, 4096);
    if (arena == NULL) {
        printf("FAIL: test_parser_bounds - arena creation failed\n");
        return;
    }

    nmo_chunk* chunk = nmo_chunk_create(arena);
    if (chunk == NULL) {
        printf("FAIL: test_parser_bounds - chunk creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // Create chunk with 1 DWORD
    chunk->data_size = 1;
    chunk->data = (uint32_t*)nmo_arena_alloc(arena, 1 * sizeof(uint32_t), sizeof(uint32_t));
    if (chunk->data == NULL) {
        printf("FAIL: test_parser_bounds - data allocation failed\n");
        nmo_arena_destroy(arena);
        return;
    }
    chunk->data[0] = 0x12345678;

    nmo_chunk_parser* parser = nmo_chunk_parser_create(chunk);
    if (parser == NULL) {
        printf("FAIL: test_parser_bounds - parser creation failed\n");
        nmo_arena_destroy(arena);
        return;
    }

    // First read should succeed
    uint32_t val;
    if (nmo_chunk_parser_read_dword(parser, &val) != NMO_OK) {
        printf("FAIL: test_parser_bounds - first read failed\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    // Second read should fail (EOF)
    if (nmo_chunk_parser_read_dword(parser, &val) != NMO_ERR_EOF) {
        printf("FAIL: test_parser_bounds - second read should have failed with EOF\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    // at_end should return true
    if (!nmo_chunk_parser_at_end(parser)) {
        printf("FAIL: test_parser_bounds - at_end should return true\n");
        nmo_chunk_parser_destroy(parser);
        nmo_arena_destroy(arena);
        return;
    }

    nmo_chunk_parser_destroy(parser);
    nmo_arena_destroy(arena);
    printf("PASS: test_parser_bounds\n");
}

int main(void) {
    printf("Running chunk parser tests...\n\n");

    test_parser_create_destroy();
    test_parser_cursor();
    test_parser_primitives();
    test_parser_string();
    test_parser_identifier();
    test_parser_bounds();

    printf("\nAll chunk parser tests completed!\n");
    return 0;
}
