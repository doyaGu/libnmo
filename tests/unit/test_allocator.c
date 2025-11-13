/**
 * @file test_allocator.c
 * @brief Unit tests for memory allocator
 */

#include "../test_framework.h"
#include "nmo.h"

/* Test definitions */
TEST(allocator, create_and_release) {
    nmo_allocator_t allocator = nmo_allocator_default();
    // The default allocator doesn't need explicit cleanup since it's a stack-allocated struct
    // Just verify it was created successfully
    ASSERT_TRUE(allocator.alloc != NULL);
    ASSERT_TRUE(allocator.free != NULL);
}

TEST(allocator, allocate_and_free) {
    nmo_allocator_t allocator = nmo_allocator_default();
    
    void *ptr = nmo_alloc(&allocator, 256, 8);
    ASSERT_NOT_NULL(ptr);
    
    nmo_free(&allocator, ptr);
}

TEST(allocator, realloc) {
    nmo_allocator_t allocator = nmo_allocator_default();
    
    void *ptr = nmo_alloc(&allocator, 128, 8);
    ASSERT_NOT_NULL(ptr);
    
    // Note: The current API doesn't have a realloc function
    // We'll simulate realloc by allocating new memory and copying
    void *new_ptr = nmo_alloc(&allocator, 256, 8);
    ASSERT_NOT_NULL(new_ptr);
    
    // Copy old data to new location (simplified for test)
    memcpy(new_ptr, ptr, 128);
    
    nmo_free(&allocator, ptr);
    nmo_free(&allocator, new_ptr);
}

/* Error condition tests */

TEST(allocator, null_allocator) {
    // Test allocation with NULL allocator
    void *ptr = nmo_alloc(NULL, 256, 8);
    ASSERT_NULL(ptr);  // Should return NULL for NULL allocator
    
    // Test freeing with NULL allocator (should not crash)
    nmo_free(NULL, ptr);  // Should handle gracefully
}

TEST(allocator, zero_size_allocation) {
    nmo_allocator_t allocator = nmo_allocator_default();
    
    // Test zero size allocation
    void *ptr = nmo_alloc(&allocator, 0, 8);
    // Behavior may vary - either NULL or a minimal allocation
    // We just verify it doesn't crash
    if (ptr != NULL) {
        nmo_free(&allocator, ptr);
    }
}

TEST(allocator, invalid_alignment) {
    nmo_allocator_t allocator = nmo_allocator_default();
    
    // Test non-power-of-2 alignment
    void *ptr = nmo_alloc(&allocator, 256, 3);  // 3 is not power of 2
    // Behavior may vary - either NULL or rounded up
    // We just verify it doesn't crash
    if (ptr != NULL) {
        nmo_free(&allocator, ptr);
    }
    
    // Test zero alignment
    ptr = nmo_alloc(&allocator, 256, 0);  // 0 alignment
    // Behavior may vary - either NULL or default alignment
    // We just verify it doesn't crash
    if (ptr != NULL) {
        nmo_free(&allocator, ptr);
    }
}

TEST(allocator, null_pointer_free) {
    nmo_allocator_t allocator = nmo_allocator_default();
    
    // Test freeing NULL pointer (should not crash)
    nmo_free(&allocator, NULL);  // Should handle gracefully
}

TEST(allocator, custom_allocator_null_functions) {
    // Test creating custom allocator with NULL functions
    nmo_allocator_t allocator = nmo_allocator_custom(NULL, NULL, NULL);
    
    // Try to allocate - should fail gracefully
    void *ptr = nmo_alloc(&allocator, 256, 8);
    ASSERT_NULL(ptr);  // Should return NULL for NULL alloc function
    
    // Try to free - should not crash
    nmo_free(&allocator, ptr);
}

TEST(allocator, large_allocation_failure) {
    nmo_allocator_t allocator = nmo_allocator_default();
    
    // Test extremely large allocation that should fail
    void *ptr = nmo_alloc(&allocator, SIZE_MAX, 8);
    ASSERT_NULL(ptr);  // Should return NULL for allocation that's too large
    
    // Test large but reasonable allocation
    const size_t large_size = 1024 * 1024 * 1024;  // 1GB
    ptr = nmo_alloc(&allocator, large_size, 8);
    // This may succeed or fail depending on system memory
    // We just verify it doesn't crash
    if (ptr != NULL) {
        nmo_free(&allocator, ptr);
    }
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(allocator, create_and_release);
    REGISTER_TEST(allocator, allocate_and_free);
    REGISTER_TEST(allocator, realloc);
    REGISTER_TEST(allocator, null_allocator);
    REGISTER_TEST(allocator, zero_size_allocation);
    REGISTER_TEST(allocator, invalid_alignment);
    REGISTER_TEST(allocator, null_pointer_free);
    REGISTER_TEST(allocator, custom_allocator_null_functions);
    REGISTER_TEST(allocator, large_allocation_failure);
TEST_MAIN_END()
