/**
 * @file test_arena.c
 * @brief Comprehensive unit tests for arena allocator
 */

#include "nmo.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST(arena, create_destroy) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_arena_destroy(arena);
}

TEST(arena, create_with_custom_allocator) {
    nmo_allocator_t allocator = nmo_allocator_default();
    nmo_arena_t *arena = nmo_arena_create(&allocator, 4096);
    ASSERT_NOT_NULL(arena);
    nmo_arena_destroy(arena);
}

TEST(arena, simple_allocation) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    void *ptr = nmo_arena_alloc(arena, 256, 1);
    ASSERT_NOT_NULL(ptr);

    nmo_arena_destroy(arena);
}

TEST(arena, multiple_allocations) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    void *ptr1 = nmo_arena_alloc(arena, 128, 1);
    void *ptr2 = nmo_arena_alloc(arena, 128, 1);
    void *ptr3 = nmo_arena_alloc(arena, 128, 1);

    ASSERT_NOT_NULL(ptr1);
    ASSERT_NOT_NULL(ptr2);
    ASSERT_NOT_NULL(ptr3);

    ASSERT_NE(ptr1, ptr2);
    ASSERT_NE(ptr2, ptr3);
    ASSERT_NE(ptr1, ptr3);

    nmo_arena_destroy(arena);
}

TEST(arena, aligned_allocation) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* Test 4-byte alignment */
    void *ptr4 = nmo_arena_alloc(arena, 10, 4);
    ASSERT_NOT_NULL(ptr4);
    ASSERT_EQ(((uintptr_t)ptr4) % 4, 0);

    /* Test 8-byte alignment */
    void *ptr8 = nmo_arena_alloc(arena, 10, 8);
    ASSERT_NOT_NULL(ptr8);
    ASSERT_EQ(((uintptr_t)ptr8) % 8, 0);

    nmo_arena_destroy(arena);
}

TEST(arena, alignment_16_bytes) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    void *ptr16 = nmo_arena_alloc(arena, 10, 16);
    ASSERT_NOT_NULL(ptr16);

    ASSERT_EQ(((uintptr_t)ptr16) % 16, 0);

    nmo_arena_destroy(arena);
}

TEST(arena, reset) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    void *ptr1 = nmo_arena_alloc(arena, 256, 1);
    ASSERT_NOT_NULL(ptr1);

    nmo_arena_reset(arena);

    void *ptr2 = nmo_arena_alloc(arena, 256, 1);
    ASSERT_NOT_NULL(ptr2);
    ASSERT_EQ(ptr1, ptr2);

    nmo_arena_destroy(arena);
}

TEST(arena, large_allocation) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* Allocate larger than initial size - should grow */
    void *ptr = nmo_arena_alloc(arena, 8192, 1);
    ASSERT_NOT_NULL(ptr);

    nmo_arena_destroy(arena);
}

TEST(arena, large_aligned_allocation_reserves_padding) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64);
    ASSERT_NOT_NULL(arena);

    const size_t allocation_size = (64 * 1024) + 1;
    uint8_t *ptr = (uint8_t *)nmo_arena_alloc(arena, allocation_size, 16);
    ASSERT_NOT_NULL(ptr);
    ASSERT_EQ(((uintptr_t)ptr) % 16, 0);

    ptr[0] = 0x5a;
    ptr[allocation_size - 1] = 0xa5;
    ASSERT_EQ(ptr[0], 0x5a);
    ASSERT_EQ(ptr[allocation_size - 1], 0xa5);

    nmo_arena_destroy(arena);
}

TEST(arena, many_small_allocations) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    for (int i = 0; i < 100; i++) {
        void *ptr = nmo_arena_alloc(arena, 32, 1);
        ASSERT_NOT_NULL(ptr);
    }

    nmo_arena_destroy(arena);
}

TEST(arena, zero_size_allocation) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* Zero-size allocation behavior is implementation-defined */
    void *ptr = nmo_arena_alloc(arena, 0, 1);
    /* Just verify it doesn't crash - ptr can be NULL or valid */
    (void)ptr; /* Suppress unused variable warning */

    nmo_arena_destroy(arena);
}

TEST(arena, allocation_data_integrity) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* Allocate and write string */
    char *str = (char*)nmo_arena_alloc(arena, 100, 1);
    ASSERT_NOT_NULL(str);
    strcpy(str, "Test string in arena");
    ASSERT_STR_EQ(str, "Test string in arena");

    /* Allocate int array */
    int *nums = (int*)nmo_arena_alloc(arena, sizeof(int) * 10, sizeof(int));
    ASSERT_NOT_NULL(nums);
    for (int i = 0; i < 10; i++) {
        nums[i] = i * 2;
    }

    /* Verify both allocations are intact */
    ASSERT_STR_EQ(str, "Test string in arena");
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(nums[i], i * 2);
    }

    nmo_arena_destroy(arena);
}

TEST(arena, mark_rewind_bytes_used) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024);
    ASSERT_NOT_NULL(arena);

    void *base = nmo_arena_alloc(arena, 128, 8);
    ASSERT_NOT_NULL(base);
    size_t used_before_mark = nmo_arena_bytes_used(arena);
    ASSERT_EQ(used_before_mark, 128u);

    nmo_arena_mark_t mark;
    ASSERT_EQ(NMO_OK, nmo_arena_mark(arena, &mark));

    void *tmp1 = nmo_arena_alloc(arena, 256, 8);
    void *tmp2 = nmo_arena_alloc(arena, 64, 8);
    ASSERT_NOT_NULL(tmp1);
    ASSERT_NOT_NULL(tmp2);
    ASSERT_TRUE(nmo_arena_bytes_used(arena) > used_before_mark);

    ASSERT_EQ(NMO_OK, nmo_arena_rewind(arena, &mark));
    ASSERT_EQ(nmo_arena_bytes_used(arena), used_before_mark);

    void *after_rewind = nmo_arena_alloc(arena, 64, 8);
    ASSERT_NOT_NULL(after_rewind);

    nmo_arena_destroy(arena);
}

TEST(arena, mark_rewind_across_chunks) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 128);
    ASSERT_NOT_NULL(arena);

    void *first = nmo_arena_alloc(arena, 96, 8);
    ASSERT_NOT_NULL(first);

    nmo_arena_mark_t outer_mark;
    ASSERT_EQ(NMO_OK, nmo_arena_mark(arena, &outer_mark));
    size_t used_at_outer_mark = nmo_arena_bytes_used(arena);

    void *spill = nmo_arena_alloc(arena, 512, 8);
    ASSERT_NOT_NULL(spill);
    ASSERT_TRUE(nmo_arena_bytes_used(arena) > used_at_outer_mark);

    nmo_arena_mark_t inner_mark;
    ASSERT_EQ(NMO_OK, nmo_arena_mark(arena, &inner_mark));
    void *tail = nmo_arena_alloc(arena, 64, 8);
    ASSERT_NOT_NULL(tail);

    ASSERT_EQ(NMO_OK, nmo_arena_rewind(arena, &inner_mark));
    ASSERT_EQ(NMO_OK, nmo_arena_rewind(arena, &outer_mark));
    ASSERT_EQ(nmo_arena_bytes_used(arena), used_at_outer_mark);

    void *reuse = nmo_arena_alloc(arena, 32, 8);
    ASSERT_NOT_NULL(reuse);

    nmo_arena_destroy(arena);
}

TEST(arena, release_mark_keeps_allocations) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 1024);
    ASSERT_NOT_NULL(arena);

    nmo_arena_mark_t mark;
    ASSERT_EQ(NMO_OK, nmo_arena_mark(arena, &mark));
    void *kept = nmo_arena_alloc(arena, 128, 8);
    ASSERT_NOT_NULL(kept);
    size_t used_after_alloc = nmo_arena_bytes_used(arena);

    ASSERT_EQ(NMO_OK, nmo_arena_release_mark(arena, &mark));
    ASSERT_EQ(used_after_alloc, nmo_arena_bytes_used(arena));

    nmo_arena_mark_t next_mark;
    ASSERT_EQ(NMO_OK, nmo_arena_mark(arena, &next_mark));
    ASSERT_NOT_NULL(nmo_arena_alloc(arena, 64, 8));
    ASSERT_EQ(NMO_OK, nmo_arena_rewind(arena, &next_mark));
    ASSERT_EQ(used_after_alloc, nmo_arena_bytes_used(arena));

    nmo_arena_destroy(arena);
}

TEST(arena, first_block_is_taken_on_first_allocation) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);
    ASSERT_EQ((size_t)0, nmo_arena_total_allocated(arena));

    ASSERT_NOT_NULL(nmo_arena_alloc(arena, 16, 8));
    ASSERT_EQ((size_t)4096, nmo_arena_total_allocated(arena));

    nmo_arena_destroy(arena);
}

TEST(arena, oversized_allocation_keeps_the_current_block) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    uint8_t *before = (uint8_t *)nmo_arena_alloc(arena, 100, 1);
    ASSERT_NOT_NULL(before);
    uint8_t *large = (uint8_t *)nmo_arena_alloc(arena, 1u << 20, 1);
    ASSERT_NOT_NULL(large);
    large[0] = 1;
    large[(1u << 20) - 1] = 2;
    /* The block it went to is its own: the next small allocation follows the
     * first one, and the blocks after it keep growing from 4 KB. */
    uint8_t *after = (uint8_t *)nmo_arena_alloc(arena, 100, 1);
    ASSERT_EQ(before + 100, after);
    ASSERT_EQ((size_t)4096 + (1u << 20), nmo_arena_total_allocated(arena));
    ASSERT_NOT_NULL(nmo_arena_alloc(arena, 4000, 1));
    ASSERT_EQ((size_t)4096 + (1u << 20) + 8192, nmo_arena_total_allocated(arena));

    nmo_arena_destroy(arena);
}

TEST(arena, blocks_grow_past_allocations_larger_than_them) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 1);
    ASSERT_NOT_NULL(arena);

    /* Allocations larger than the next block, but not large, start a block of
     * their size, and the blocks after them grow from it: a tiny first block
     * costs a few blocks, not one per allocation. */
    for (int i = 0; i < 64; ++i) {
        ASSERT_NOT_NULL(nmo_arena_alloc(arena, 64, 8));
    }
    ASSERT_TRUE(nmo_arena_total_allocated(arena) < 64u * 71u);
    ASSERT_TRUE(nmo_arena_total_allocated(arena) >= 64u * 64u);

    nmo_arena_destroy(arena);
}

TEST(arena, rewind_to_a_mark_before_the_first_block) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 256);
    ASSERT_NOT_NULL(arena);

    nmo_arena_mark_t mark;
    ASSERT_EQ(NMO_OK, nmo_arena_mark(arena, &mark));
    void *first = nmo_arena_alloc(arena, 64, 8);
    ASSERT_NOT_NULL(first);
    ASSERT_NOT_NULL(nmo_arena_alloc(arena, 4096, 8));
    ASSERT_EQ(NMO_OK, nmo_arena_rewind(arena, &mark));
    ASSERT_EQ((size_t)0, nmo_arena_bytes_used(arena));

    /* The blocks are kept and reused. */
    size_t total = nmo_arena_total_allocated(arena);
    ASSERT_EQ(first, nmo_arena_alloc(arena, 64, 8));
    ASSERT_EQ(total, nmo_arena_total_allocated(arena));

    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(arena, create_destroy);
    REGISTER_TEST(arena, create_with_custom_allocator);
    REGISTER_TEST(arena, simple_allocation);
    REGISTER_TEST(arena, multiple_allocations);
    REGISTER_TEST(arena, aligned_allocation);
    REGISTER_TEST(arena, alignment_16_bytes);
    REGISTER_TEST(arena, reset);
    REGISTER_TEST(arena, large_allocation);
    REGISTER_TEST(arena, large_aligned_allocation_reserves_padding);
    REGISTER_TEST(arena, many_small_allocations);
    REGISTER_TEST(arena, zero_size_allocation);
    REGISTER_TEST(arena, allocation_data_integrity);
    REGISTER_TEST(arena, mark_rewind_bytes_used);
    REGISTER_TEST(arena, mark_rewind_across_chunks);
    REGISTER_TEST(arena, release_mark_keeps_allocations);
    REGISTER_TEST(arena, first_block_is_taken_on_first_allocation);
    REGISTER_TEST(arena, oversized_allocation_keeps_the_current_block);
    REGISTER_TEST(arena, blocks_grow_past_allocations_larger_than_them);
    REGISTER_TEST(arena, rewind_to_a_mark_before_the_first_block);
TEST_MAIN_END()
