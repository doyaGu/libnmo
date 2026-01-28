/**
 * @file test_chunk_writer_reserve.c
 * @brief Unit tests for the chunk writer reserve/patch API (Phase 2.2)
 *
 * Tests the reserve-and-patch buffer mode that allows writing placeholders
 * and filling in the actual values later. This is crucial for forward
 * dependencies like chunk sizes that aren't known until after serialization.
 */

#include "format/nmo_chunk_writer.h"
#include "core/nmo_arena.h"
#include "test_framework.h"

/* ============================================================================
 * Reserve U32 Tests
 * ============================================================================ */

TEST(chunk_writer_reserve, reserve_u32_basic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Reserve a u32 placeholder */
    nmo_patch_token_t token = nmo_chunk_writer_reserve_u32(w);
    ASSERT_TRUE(nmo_patch_token_valid(token));
    ASSERT_EQ(token.size, 1);

    /* Position should have advanced */
    size_t pos_after = nmo_chunk_writer_tell(w);
    ASSERT_EQ(pos_after, 1);  /* 1 DWORD reserved */

    /* Patch the reserved position */
    ASSERT_EQ(nmo_chunk_writer_patch_u32(w, token, 0xDEADBEEF), NMO_OK);

    /* Finalize and verify */
    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(w);
    ASSERT_NOT_NULL(chunk);

    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_EQ(data[0], 0xDEADBEEF);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(chunk_writer_reserve, reserve_u32_multiple) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Reserve multiple placeholders */
    nmo_patch_token_t token1 = nmo_chunk_writer_reserve_u32(w);
    nmo_patch_token_t token2 = nmo_chunk_writer_reserve_u32(w);
    nmo_patch_token_t token3 = nmo_chunk_writer_reserve_u32(w);

    ASSERT_TRUE(nmo_patch_token_valid(token1));
    ASSERT_TRUE(nmo_patch_token_valid(token2));
    ASSERT_TRUE(nmo_patch_token_valid(token3));

    /* Positions should be sequential */
    ASSERT_EQ(token1.offset, 0);
    ASSERT_EQ(token2.offset, 1);
    ASSERT_EQ(token3.offset, 2);

    /* Patch in reverse order to verify independence */
    ASSERT_EQ(nmo_chunk_writer_patch_u32(w, token3, 0x33333333), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_patch_u32(w, token1, 0x11111111), NMO_OK);
    ASSERT_EQ(nmo_chunk_writer_patch_u32(w, token2, 0x22222222), NMO_OK);

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(w);
    ASSERT_NOT_NULL(chunk);

    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_EQ(data[0], 0x11111111);
    ASSERT_EQ(data[1], 0x22222222);
    ASSERT_EQ(data[2], 0x33333333);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(chunk_writer_reserve, reserve_mixed_with_writes) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Write header marker */
    nmo_chunk_writer_write_dword(w, 0xCAFEBABE);

    /* Reserve size placeholder */
    size_t size_start = nmo_chunk_writer_tell(w);
    nmo_patch_token_t size_token = nmo_chunk_writer_reserve_u32(w);

    /* Write payload */
    nmo_chunk_writer_write_dword(w, 0x00000001);
    nmo_chunk_writer_write_dword(w, 0x00000002);
    nmo_chunk_writer_write_dword(w, 0x00000003);

    /* Calculate and patch size */
    size_t payload_size = nmo_chunk_writer_tell(w) - size_start - 1;  /* -1 for size field itself */
    ASSERT_EQ(nmo_chunk_writer_patch_u32(w, size_token, (uint32_t)payload_size), NMO_OK);

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(w);
    ASSERT_NOT_NULL(chunk);

    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    ASSERT_EQ(data[0], 0xCAFEBABE);  /* Header marker */
    ASSERT_EQ(data[1], 3);           /* Payload size = 3 DWORDs */
    ASSERT_EQ(data[2], 0x00000001);  /* Payload */
    ASSERT_EQ(data[3], 0x00000002);
    ASSERT_EQ(data[4], 0x00000003);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Reserve U64 Tests
 * ============================================================================ */

TEST(chunk_writer_reserve, reserve_u64_basic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Reserve a u64 placeholder */
    nmo_patch_token_t token = nmo_chunk_writer_reserve_u64(w);
    ASSERT_TRUE(nmo_patch_token_valid(token));
    ASSERT_EQ(token.size, 2);

    /* Position should have advanced by 2 DWORDs */
    ASSERT_EQ(nmo_chunk_writer_tell(w), 2);

    /* Patch with 64-bit value */
    ASSERT_EQ(nmo_chunk_writer_patch_u64(w, token, 0x123456789ABCDEF0ULL), NMO_OK);

    nmo_chunk_t *chunk = nmo_chunk_writer_finalize(w);
    ASSERT_NOT_NULL(chunk);

    uint32_t *data = NMO_ARENA_ARRAY_DATA(uint32_t, &chunk->data);
    /* 64-bit stored as low:high DWORDs (little-endian) */
    ASSERT_EQ(data[0], 0x9ABCDEF0);  /* Low 32 bits */
    ASSERT_EQ(data[1], 0x12345678);  /* High 32 bits */

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Reserve DWORDs Tests
 * ============================================================================ */

TEST(chunk_writer_reserve, reserve_dwords_variable) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Reserve 4 DWORDs */
    nmo_patch_token_t token = nmo_chunk_writer_reserve_dwords(w, 4);
    ASSERT_TRUE(nmo_patch_token_valid(token));
    ASSERT_EQ(token.size, 4);
    ASSERT_EQ(nmo_chunk_writer_tell(w), 4);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(chunk_writer_reserve, reserve_dwords_max_limit) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* 0 DWORDs should fail */
    nmo_patch_token_t token0 = nmo_chunk_writer_reserve_dwords(w, 0);
    ASSERT_FALSE(nmo_patch_token_valid(token0));

    /* 5+ DWORDs should fail (max is 4) */
    nmo_patch_token_t token5 = nmo_chunk_writer_reserve_dwords(w, 5);
    ASSERT_FALSE(nmo_patch_token_valid(token5));

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Error Handling Tests
 * ============================================================================ */

TEST(chunk_writer_reserve, patch_invalid_token) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Invalid token should fail */
    nmo_patch_token_t invalid = NMO_PATCH_TOKEN_INVALID;
    ASSERT_EQ(nmo_chunk_writer_patch_u32(w, invalid, 0x12345678), NMO_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(nmo_chunk_writer_patch_u64(w, invalid, 0x12345678ULL), NMO_ERR_INVALID_ARGUMENT);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(chunk_writer_reserve, patch_wrong_size) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    /* Reserve u32 but try to patch as u64 - should fail */
    nmo_patch_token_t token32 = nmo_chunk_writer_reserve_u32(w);
    ASSERT_EQ(nmo_chunk_writer_patch_u64(w, token32, 0x12345678ULL), NMO_ERR_INVALID_ARGUMENT);

    /* Reserve u64 but try to patch as u32 - should fail */
    nmo_patch_token_t token64 = nmo_chunk_writer_reserve_u64(w);
    ASSERT_EQ(nmo_chunk_writer_patch_u32(w, token64, 0x12345678), NMO_ERR_INVALID_ARGUMENT);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

TEST(chunk_writer_reserve, null_writer) {
    /* Reserve on NULL writer should return invalid token */
    nmo_patch_token_t token = nmo_chunk_writer_reserve_u32(NULL);
    ASSERT_FALSE(nmo_patch_token_valid(token));

    token = nmo_chunk_writer_reserve_u64(NULL);
    ASSERT_FALSE(nmo_patch_token_valid(token));

    /* Patch on NULL writer should fail */
    nmo_patch_token_t valid_token = { .offset = 0, .size = 1, .valid = 1 };
    ASSERT_EQ(nmo_chunk_writer_patch_u32(NULL, valid_token, 0), NMO_ERR_INVALID_ARGUMENT);

    /* Tell on NULL should return 0 */
    ASSERT_EQ(nmo_chunk_writer_tell(NULL), 0);
}

/* ============================================================================
 * Tell Tests
 * ============================================================================ */

TEST(chunk_writer_reserve, tell_tracks_position) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_chunk_writer_t *w = nmo_chunk_writer_create(arena);
    ASSERT_NOT_NULL(w);

    nmo_chunk_writer_start(w, 0x1234, 7);

    ASSERT_EQ(nmo_chunk_writer_tell(w), 0);

    nmo_chunk_writer_write_dword(w, 1);
    ASSERT_EQ(nmo_chunk_writer_tell(w), 1);

    nmo_chunk_writer_write_dword(w, 2);
    ASSERT_EQ(nmo_chunk_writer_tell(w), 2);

    nmo_chunk_writer_reserve_u32(w);
    ASSERT_EQ(nmo_chunk_writer_tell(w), 3);

    nmo_chunk_writer_reserve_u64(w);
    ASSERT_EQ(nmo_chunk_writer_tell(w), 5);

    nmo_chunk_writer_destroy(w);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test Registration
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* Reserve U32 Tests */
    REGISTER_TEST(chunk_writer_reserve, reserve_u32_basic);
    REGISTER_TEST(chunk_writer_reserve, reserve_u32_multiple);
    REGISTER_TEST(chunk_writer_reserve, reserve_mixed_with_writes);

    /* Reserve U64 Tests */
    REGISTER_TEST(chunk_writer_reserve, reserve_u64_basic);

    /* Reserve DWORDs Tests */
    REGISTER_TEST(chunk_writer_reserve, reserve_dwords_variable);
    REGISTER_TEST(chunk_writer_reserve, reserve_dwords_max_limit);

    /* Error Handling Tests */
    REGISTER_TEST(chunk_writer_reserve, patch_invalid_token);
    REGISTER_TEST(chunk_writer_reserve, patch_wrong_size);
    REGISTER_TEST(chunk_writer_reserve, null_writer);

    /* Tell Tests */
    REGISTER_TEST(chunk_writer_reserve, tell_tracks_position);
TEST_MAIN_END()
