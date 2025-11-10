/**
 * @file test_arena.c
 * @brief Unit tests for arena allocator
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(arena, create_and_reset) {
    nmo_arena *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_reset(arena);
    nmo_arena_destroy(arena);
}

TEST(arena, allocate) {
    nmo_arena *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    void *ptr = nmo_arena_allocate(arena, 256);
    ASSERT_NOT_NULL(ptr);

    nmo_arena_destroy(arena);
}

TEST(arena, multiple_allocations) {
    nmo_arena *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    void *ptr1 = nmo_arena_allocate(arena, 128);
    void *ptr2 = nmo_arena_allocate(arena, 128);
    void *ptr3 = nmo_arena_allocate(arena, 128);

    ASSERT_NOT_NULL(ptr1);
    ASSERT_NOT_NULL(ptr2);
    ASSERT_NOT_NULL(ptr3);

    nmo_arena_destroy(arena);
}

int main(void) {
    return 0;
}
