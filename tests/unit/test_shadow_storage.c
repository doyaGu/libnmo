/**
 * @file test_shadow_storage.c
 * @brief Unit tests for the shadow blob preservation mechanism (Phase 1.2)
 */

#include "object/nmo_shadow_storage.h"
#include "core/nmo_allocator.h"
#include "core/nmo_arena.h"
#include "test_framework.h"
#include <string.h>

typedef struct shadow_fail_allocator {
    nmo_allocator_t base;
    bool fail_allocations;
} shadow_fail_allocator_t;

static void *shadow_fail_alloc(void *user_data, size_t size, size_t alignment) {
    shadow_fail_allocator_t *ctx = (shadow_fail_allocator_t *)user_data;
    if (ctx->fail_allocations) {
        return NULL;
    }
    return nmo_alloc(&ctx->base, size, alignment);
}

static void shadow_fail_free(void *user_data, void *ptr) {
    shadow_fail_allocator_t *ctx = (shadow_fail_allocator_t *)user_data;
    nmo_free(&ctx->base, ptr);
}

TEST(shadow_storage, create_and_destroy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_shadow_storage_t *storage = nmo_shadow_storage_create(arena);
    ASSERT_NOT_NULL(storage);

    /* Initial state should be empty */
    ASSERT_FALSE(nmo_shadow_has_included_files(storage));
    ASSERT_EQ(nmo_shadow_chunk_tail_count(storage), 0u);

    nmo_shadow_storage_destroy(storage);
    nmo_arena_destroy(arena);
}

TEST(shadow_storage, capture_included_files) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_shadow_storage_t *storage = nmo_shadow_storage_create(arena);
    ASSERT_NOT_NULL(storage);

    const uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    size_t test_size = sizeof(test_data);

    /* Capture included files blob */
    ASSERT_EQ(nmo_shadow_capture_included_files(storage, test_data, test_size), NMO_OK);
    ASSERT_TRUE(nmo_shadow_has_included_files(storage));

    /* Retrieve and verify */
    size_t out_size = 0;
    const void *retrieved = nmo_shadow_get_included_files(storage, &out_size);
    ASSERT_NOT_NULL(retrieved);
    ASSERT_EQ(out_size, test_size);
    ASSERT_EQ(memcmp(retrieved, test_data, test_size), 0);

    nmo_shadow_storage_destroy(storage);
    nmo_arena_destroy(arena);
}

TEST(shadow_storage, capture_chunk_tail) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_shadow_storage_t *storage = nmo_shadow_storage_create(arena);
    ASSERT_NOT_NULL(storage);

    const uint8_t tail1[] = {0xAA, 0xBB, 0xCC};
    const uint8_t tail2[] = {0x11, 0x22, 0x33, 0x44, 0x55};

    /* Capture chunk tails for two different chunks */
    ASSERT_EQ(nmo_shadow_capture_chunk_tail(storage, 100, tail1, sizeof(tail1)), NMO_OK);
    ASSERT_EQ(nmo_shadow_capture_chunk_tail(storage, 200, tail2, sizeof(tail2)), NMO_OK);

    ASSERT_EQ(nmo_shadow_chunk_tail_count(storage), 2u);

    /* Retrieve and verify tail1 */
    size_t size1 = 0;
    const void *data1 = nmo_shadow_get_chunk_tail(storage, 100, &size1);
    ASSERT_NOT_NULL(data1);
    ASSERT_EQ(size1, sizeof(tail1));
    ASSERT_EQ(memcmp(data1, tail1, size1), 0);

    /* Retrieve and verify tail2 */
    size_t size2 = 0;
    const void *data2 = nmo_shadow_get_chunk_tail(storage, 200, &size2);
    ASSERT_NOT_NULL(data2);
    ASSERT_EQ(size2, sizeof(tail2));
    ASSERT_EQ(memcmp(data2, tail2, size2), 0);

    /* Non-existent chunk should return NULL */
    size_t size_none = 99;
    const void *data_none = nmo_shadow_get_chunk_tail(storage, 999, &size_none);
    ASSERT_NULL(data_none);
    ASSERT_EQ(size_none, 0u);

    nmo_shadow_storage_destroy(storage);
    nmo_arena_destroy(arena);
}

TEST(shadow_storage, chunk_tail_uses_arena_backing_allocator) {
    shadow_fail_allocator_t fail_ctx = {
        .base = nmo_allocator_default(),
        .fail_allocations = false,
    };
    nmo_allocator_t allocator = nmo_allocator_custom(
        shadow_fail_alloc, shadow_fail_free, &fail_ctx);
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_shadow_storage_t *storage = nmo_shadow_storage_create(arena);
    ASSERT_NOT_NULL(storage);

    const uint8_t tail[] = {0xAA, 0xBB, 0xCC, 0xDD};
    fail_ctx.fail_allocations = true;
    ASSERT_EQ(NMO_ERR_NOMEM,
              nmo_shadow_capture_chunk_tail(storage, 100, tail, sizeof(tail)));
    ASSERT_EQ(0u, nmo_shadow_chunk_tail_count(storage));

    fail_ctx.fail_allocations = false;
    nmo_shadow_storage_destroy(storage);
    nmo_arena_destroy(arena);
}

TEST(shadow_storage, overwrite_included_files) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_shadow_storage_t *storage = nmo_shadow_storage_create(arena);
    ASSERT_NOT_NULL(storage);

    const uint8_t data1[] = {0x01, 0x02};
    const uint8_t data2[] = {0xAA, 0xBB, 0xCC, 0xDD};

    /* Capture first version */
    ASSERT_EQ(nmo_shadow_capture_included_files(storage, data1, sizeof(data1)), NMO_OK);

    /* Overwrite with second version */
    ASSERT_EQ(nmo_shadow_capture_included_files(storage, data2, sizeof(data2)), NMO_OK);

    /* Should retrieve second version */
    size_t out_size = 0;
    const void *retrieved = nmo_shadow_get_included_files(storage, &out_size);
    ASSERT_NOT_NULL(retrieved);
    ASSERT_EQ(out_size, sizeof(data2));
    ASSERT_EQ(memcmp(retrieved, data2, out_size), 0);

    nmo_shadow_storage_destroy(storage);
    nmo_arena_destroy(arena);
}

TEST(shadow_storage, overwrite_included_files_reclaims_scope) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_shadow_storage_t *storage = nmo_shadow_storage_create(arena);
    ASSERT_NOT_NULL(storage);

    uint8_t first[1024];
    uint8_t second[128];
    memset(first, 0xAA, sizeof(first));
    memset(second, 0xBB, sizeof(second));

    ASSERT_EQ(nmo_shadow_capture_included_files(storage, first, sizeof(first)), NMO_OK);
    size_t used_after_first = nmo_arena_bytes_used(arena);
    ASSERT_TRUE(used_after_first >= sizeof(first));

    ASSERT_EQ(nmo_shadow_capture_included_files(storage, second, sizeof(second)), NMO_OK);
    size_t used_after_second = nmo_arena_bytes_used(arena);
    ASSERT_TRUE(used_after_second <= used_after_first);

    nmo_shadow_storage_destroy(storage);
    nmo_arena_destroy(arena);
}

TEST(shadow_storage, overwrite_chunk_tail) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_shadow_storage_t *storage = nmo_shadow_storage_create(arena);
    ASSERT_NOT_NULL(storage);

    const uint8_t tail_v1[] = {0x01};
    const uint8_t tail_v2[] = {0xFF, 0xFE, 0xFD};

    /* Capture first version for chunk 42 */
    ASSERT_EQ(nmo_shadow_capture_chunk_tail(storage, 42, tail_v1, sizeof(tail_v1)), NMO_OK);
    ASSERT_EQ(nmo_shadow_chunk_tail_count(storage), 1u);

    /* Overwrite with second version */
    ASSERT_EQ(nmo_shadow_capture_chunk_tail(storage, 42, tail_v2, sizeof(tail_v2)), NMO_OK);
    ASSERT_EQ(nmo_shadow_chunk_tail_count(storage), 1u);  /* Still only 1 entry */

    /* Should retrieve second version */
    size_t out_size = 0;
    const void *retrieved = nmo_shadow_get_chunk_tail(storage, 42, &out_size);
    ASSERT_NOT_NULL(retrieved);
    ASSERT_EQ(out_size, sizeof(tail_v2));
    ASSERT_EQ(memcmp(retrieved, tail_v2, out_size), 0);

    nmo_shadow_storage_destroy(storage);
    nmo_arena_destroy(arena);
}

TEST(shadow_storage, reset_clears_all) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_shadow_storage_t *storage = nmo_shadow_storage_create(arena);
    ASSERT_NOT_NULL(storage);

    const uint8_t data[] = {0x12, 0x34};

    /* Capture some data */
    ASSERT_EQ(nmo_shadow_capture_included_files(storage, data, sizeof(data)), NMO_OK);
    ASSERT_EQ(nmo_shadow_capture_chunk_tail(storage, 1, data, sizeof(data)), NMO_OK);
    ASSERT_EQ(nmo_shadow_capture_chunk_tail(storage, 2, data, sizeof(data)), NMO_OK);

    ASSERT_TRUE(nmo_shadow_has_included_files(storage));
    ASSERT_EQ(nmo_shadow_chunk_tail_count(storage), 2u);

    /* Reset should clear everything */
    nmo_shadow_storage_reset(storage);

    ASSERT_FALSE(nmo_shadow_has_included_files(storage));
    ASSERT_EQ(nmo_shadow_chunk_tail_count(storage), 0u);
    ASSERT_NULL(nmo_shadow_get_included_files(storage, NULL));
    ASSERT_NULL(nmo_shadow_get_chunk_tail(storage, 1, NULL));

    nmo_shadow_storage_destroy(storage);
    nmo_arena_destroy(arena);
}

TEST(shadow_storage, reset_after_external_arena_reset_clears_state) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_shadow_storage_t *storage = nmo_shadow_storage_create(arena);
    ASSERT_NOT_NULL(storage);

    const uint8_t data[] = {0xAA, 0x55, 0x10};
    ASSERT_EQ(nmo_shadow_capture_included_files(storage, data, sizeof(data)), NMO_OK);
    ASSERT_TRUE(nmo_shadow_has_included_files(storage));

    /* Simulate external arena scope invalidation before storage reset. */
    nmo_arena_reset(arena);
    nmo_shadow_storage_reset(storage);
    ASSERT_FALSE(nmo_shadow_has_included_files(storage));

    size_t out_size = 123u;
    ASSERT_NULL(nmo_shadow_get_included_files(storage, &out_size));
    ASSERT_EQ(0u, out_size);

    /* Storage should remain reusable after the failed rewind path is handled. */
    ASSERT_EQ(nmo_shadow_capture_included_files(storage, data, sizeof(data)), NMO_OK);
    ASSERT_TRUE(nmo_shadow_has_included_files(storage));

    nmo_shadow_storage_destroy(storage);
    nmo_arena_destroy(arena);
}

TEST(shadow_storage, clear_with_null_data) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_shadow_storage_t *storage = nmo_shadow_storage_create(arena);
    ASSERT_NOT_NULL(storage);

    const uint8_t data[] = {0xAB, 0xCD};

    /* Capture some data first */
    ASSERT_EQ(nmo_shadow_capture_included_files(storage, data, sizeof(data)), NMO_OK);
    ASSERT_EQ(nmo_shadow_capture_chunk_tail(storage, 10, data, sizeof(data)), NMO_OK);

    /* Clearing with NULL/0 should remove entries */
    ASSERT_EQ(nmo_shadow_capture_included_files(storage, NULL, 0), NMO_OK);
    ASSERT_FALSE(nmo_shadow_has_included_files(storage));

    ASSERT_EQ(nmo_shadow_capture_chunk_tail(storage, 10, NULL, 0), NMO_OK);
    ASSERT_EQ(nmo_shadow_chunk_tail_count(storage), 0u);

    nmo_shadow_storage_destroy(storage);
    nmo_arena_destroy(arena);
}

/* Iteration test helper */
static int iteration_count = 0;
static uint32_t iteration_ids[10];
static size_t iteration_sizes[10];

static bool iteration_callback(uint32_t chunk_id, const void *data, size_t size, void *user) {
    (void)data;
    (void)user;
    if (iteration_count < 10) {
        iteration_ids[iteration_count] = chunk_id;
        iteration_sizes[iteration_count] = size;
        iteration_count++;
    }
    return true;  /* Continue iteration */
}

TEST(shadow_storage, iterate_chunk_tails) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_shadow_storage_t *storage = nmo_shadow_storage_create(arena);
    ASSERT_NOT_NULL(storage);

    const uint8_t tail_a[] = {0x01, 0x02};
    const uint8_t tail_b[] = {0x03, 0x04, 0x05};
    const uint8_t tail_c[] = {0x06};

    ASSERT_EQ(nmo_shadow_capture_chunk_tail(storage, 100, tail_a, sizeof(tail_a)), NMO_OK);
    ASSERT_EQ(nmo_shadow_capture_chunk_tail(storage, 200, tail_b, sizeof(tail_b)), NMO_OK);
    ASSERT_EQ(nmo_shadow_capture_chunk_tail(storage, 300, tail_c, sizeof(tail_c)), NMO_OK);

    /* Reset iteration state */
    iteration_count = 0;
    memset(iteration_ids, 0, sizeof(iteration_ids));
    memset(iteration_sizes, 0, sizeof(iteration_sizes));

    /* Iterate */
    nmo_shadow_iterate_chunk_tails(storage, iteration_callback, NULL);

    /* Should have iterated over 3 entries */
    ASSERT_EQ(iteration_count, 3);

    /* Verify all expected IDs were visited (order may vary due to hash table) */
    bool found_100 = false, found_200 = false, found_300 = false;
    for (int i = 0; i < iteration_count; i++) {
        if (iteration_ids[i] == 100) {
            found_100 = true;
            ASSERT_EQ(iteration_sizes[i], sizeof(tail_a));
        }
        if (iteration_ids[i] == 200) {
            found_200 = true;
            ASSERT_EQ(iteration_sizes[i], sizeof(tail_b));
        }
        if (iteration_ids[i] == 300) {
            found_300 = true;
            ASSERT_EQ(iteration_sizes[i], sizeof(tail_c));
        }
    }
    ASSERT_TRUE(found_100);
    ASSERT_TRUE(found_200);
    ASSERT_TRUE(found_300);

    nmo_shadow_storage_destroy(storage);
    nmo_arena_destroy(arena);
}

TEST(shadow_storage, null_handling) {
    /* NULL storage should be handled gracefully */
    ASSERT_FALSE(nmo_shadow_has_included_files(NULL));
    ASSERT_EQ(nmo_shadow_chunk_tail_count(NULL), 0u);
    ASSERT_NULL(nmo_shadow_get_included_files(NULL, NULL));
    ASSERT_NULL(nmo_shadow_get_chunk_tail(NULL, 0, NULL));

    /* NULL arena should return NULL storage */
    ASSERT_NULL(nmo_shadow_storage_create(NULL));

    /* Operations on NULL should not crash */
    nmo_shadow_storage_destroy(NULL);
    nmo_shadow_storage_reset(NULL);
    nmo_shadow_iterate_chunk_tails(NULL, iteration_callback, NULL);

    /* Invalid args should return error */
    ASSERT_NE(nmo_shadow_capture_included_files(NULL, "test", 4), NMO_OK);
    ASSERT_NE(nmo_shadow_capture_chunk_tail(NULL, 0, "test", 4), NMO_OK);
}

TEST(shadow_storage, large_data) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024 * 1024);  /* 1MB arena */
    ASSERT_NOT_NULL(arena);

    nmo_shadow_storage_t *storage = nmo_shadow_storage_create(arena);
    ASSERT_NOT_NULL(storage);

    /* Create a large buffer (64KB) */
    const size_t large_size = 64 * 1024;
    uint8_t *large_data = (uint8_t *)malloc(large_size);
    ASSERT_NOT_NULL(large_data);

    /* Fill with pattern */
    for (size_t i = 0; i < large_size; i++) {
        large_data[i] = (uint8_t)(i & 0xFF);
    }

    /* Capture large included files blob */
    ASSERT_EQ(nmo_shadow_capture_included_files(storage, large_data, large_size), NMO_OK);

    /* Verify retrieval */
    size_t out_size = 0;
    const void *retrieved = nmo_shadow_get_included_files(storage, &out_size);
    ASSERT_NOT_NULL(retrieved);
    ASSERT_EQ(out_size, large_size);
    ASSERT_EQ(memcmp(retrieved, large_data, large_size), 0);

    /* Capture large chunk tail */
    ASSERT_EQ(nmo_shadow_capture_chunk_tail(storage, 999, large_data, large_size), NMO_OK);

    const void *tail = nmo_shadow_get_chunk_tail(storage, 999, &out_size);
    ASSERT_NOT_NULL(tail);
    ASSERT_EQ(out_size, large_size);
    ASSERT_EQ(memcmp(tail, large_data, large_size), 0);

    free(large_data);
    nmo_shadow_storage_destroy(storage);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(shadow_storage, create_and_destroy);
    REGISTER_TEST(shadow_storage, capture_included_files);
    REGISTER_TEST(shadow_storage, capture_chunk_tail);
    REGISTER_TEST(shadow_storage, chunk_tail_uses_arena_backing_allocator);
    REGISTER_TEST(shadow_storage, overwrite_included_files);
    REGISTER_TEST(shadow_storage, overwrite_included_files_reclaims_scope);
    REGISTER_TEST(shadow_storage, overwrite_chunk_tail);
    REGISTER_TEST(shadow_storage, reset_clears_all);
    REGISTER_TEST(shadow_storage, reset_after_external_arena_reset_clears_state);
    REGISTER_TEST(shadow_storage, clear_with_null_data);
    REGISTER_TEST(shadow_storage, iterate_chunk_tails);
    REGISTER_TEST(shadow_storage, null_handling);
    REGISTER_TEST(shadow_storage, large_data);
TEST_MAIN_END()
