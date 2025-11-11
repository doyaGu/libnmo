/**
 * @file test_allocator.c
 * @brief Unit tests for memory allocator
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(allocator, create_and_release) {
    nmo_allocator *alloc = nmo_allocator_create_default();
    ASSERT_NOT_NULL(alloc);
    nmo_allocator_release(alloc);
}

TEST(allocator, allocate_and_free) {
    nmo_allocator *alloc = nmo_allocator_create_default();
    ASSERT_NOT_NULL(alloc);

    void *ptr = nmo_allocator_malloc(alloc, 256);
    ASSERT_NOT_NULL(ptr);

    nmo_allocator_free(alloc, ptr);
    nmo_allocator_release(alloc);
}

TEST(allocator, realloc) {
    nmo_allocator *alloc = nmo_allocator_create_default();
    ASSERT_NOT_NULL(alloc);

    void *ptr = nmo_allocator_malloc(alloc, 128);
    ASSERT_NOT_NULL(ptr);

    void *new_ptr = nmo_allocator_realloc(alloc, ptr, 256);
    ASSERT_NOT_NULL(new_ptr);

    nmo_allocator_free(alloc, new_ptr);
    nmo_allocator_release(alloc);
}

int main(void) {
    return 0;
}
