/**
 * @file test_arena_array.c
 * @brief Comprehensive unit tests for nmo_arena_array
 * 
 * Test Coverage:
 * - Buffer initialization (init, alloc)
 * - Basic operations (append, get, set, clear)
 * - Capacity management (reserve, ensure_space, growth)
 * - Array operations (append_array)
 * - Data operations (set_data, clone)
 * - Edge cases (NULL parameters, zero sizes, out of bounds)
 * - Type safety (typed macros)
 * - Memory integrity (data preservation across operations)
 */

#include "nmo.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Test fixture */
static nmo_arena_t *g_arena = NULL;

static void setup_buffer_test(void) {
    g_arena = nmo_arena_create(NULL, 4096);
}

static void teardown_buffer_test(void) {
    if (g_arena) {
        nmo_arena_destroy(g_arena);
        g_arena = NULL;
    }
}

typedef struct tracked_value {
    uint32_t id;
} tracked_value_t;

static void tracked_value_dispose(void *element, void *user_data) {
    if (element == NULL || user_data == NULL) {
        return;
    }
    const tracked_value_t *value = (const tracked_value_t *)element;
    uint32_t *total = (uint32_t *)user_data;
    *total += value->id;
}

/* ============================================================================
 * Initialization Tests
 * ========================================================================== */

TEST(buffer, init_basic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_result_t result = nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);
    
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NULL(buffer.data);
    ASSERT_EQ(buffer.count, 0);
    ASSERT_EQ(buffer.capacity, 0);
    ASSERT_EQ(buffer.element_size, sizeof(uint32_t));
    /* Arena is in union, can't directly test it */
    // ASSERT_EQ(buffer.mem.arena, arena);

    nmo_arena_destroy(arena);
}

TEST(buffer, init_with_capacity) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_result_t result = nmo_arena_array_init(&buffer, sizeof(uint32_t), 16, arena);
    
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(buffer.data);
    ASSERT_EQ(buffer.count, 0);
    ASSERT_EQ(buffer.capacity, 16);
    ASSERT_EQ(buffer.element_size, sizeof(uint32_t));

    nmo_arena_destroy(arena);
}

TEST(buffer, init_different_element_sizes) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* uint8_t buffer */
    nmo_arena_array_t buffer1;
    nmo_result_t result = nmo_arena_array_init(&buffer1, sizeof(uint8_t), 8, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(buffer1.element_size, sizeof(uint8_t));

    /* uint64_t buffer */
    nmo_arena_array_t buffer2;
    result = nmo_arena_array_init(&buffer2, sizeof(uint64_t), 8, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(buffer2.element_size, sizeof(uint64_t));

    /* struct buffer */
    typedef struct { int x, y, z; } point3d_t;
    nmo_arena_array_t buffer3;
    result = nmo_arena_array_init(&buffer3, sizeof(point3d_t), 8, arena);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(buffer3.element_size, sizeof(point3d_t));

    nmo_arena_destroy(arena);
}

TEST(buffer, alloc_basic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_result_t result = nmo_arena_array_alloc(&buffer, sizeof(uint32_t), 10, arena);
    
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(buffer.data);
    ASSERT_EQ(buffer.count, 10);
    ASSERT_EQ(buffer.capacity, 10);
    ASSERT_EQ(buffer.element_size, sizeof(uint32_t));

    nmo_arena_destroy(arena);
}

TEST(buffer, alloc_zero_count) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_result_t result = nmo_arena_array_alloc(&buffer, sizeof(uint32_t), 0, arena);
    
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NULL(buffer.data);
    ASSERT_EQ(buffer.count, 0);
    ASSERT_EQ(buffer.capacity, 0);

    nmo_arena_destroy(arena);
}

TEST(buffer, alloc_large_count) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_result_t result = nmo_arena_array_alloc(&buffer, sizeof(uint32_t), 1000, arena);
    
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(buffer.data);
    ASSERT_EQ(buffer.count, 1000);
    ASSERT_EQ(buffer.capacity, 1000);

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Append Operations Tests
 * ========================================================================== */

TEST(buffer, append_single_element) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);

    uint32_t value = 42;
    nmo_result_t result = nmo_arena_array_append(&buffer, &value);
    
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(buffer.count, 1);
    ASSERT_TRUE(buffer.capacity >= 1);
    
    uint32_t *data = (uint32_t *)buffer.data;
    ASSERT_EQ(data[0], 42);

    nmo_arena_destroy(arena);
}

TEST(buffer, append_multiple_elements) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);

    for (uint32_t i = 0; i < 10; i++) {
        nmo_result_t result = nmo_arena_array_append(&buffer, &i);
        ASSERT_EQ(result.code, NMO_OK);
    }
    
    ASSERT_EQ(buffer.count, 10);
    
    uint32_t *data = (uint32_t *)buffer.data;
    for (uint32_t i = 0; i < 10; i++) {
        ASSERT_EQ(data[i], i);
    }

    nmo_arena_destroy(arena);
}

TEST(buffer, append_triggers_growth) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 2, arena);
    
    ASSERT_EQ(buffer.capacity, 2);

    /* Append 3 elements - should trigger growth */
    uint32_t value = 1;
    nmo_arena_array_append(&buffer, &value);
    nmo_arena_array_append(&buffer, &value);
    nmo_arena_array_append(&buffer, &value);

    ASSERT_EQ(buffer.count, 3);
    ASSERT_TRUE(buffer.capacity >= 3);

    nmo_arena_destroy(arena);
}

TEST(buffer, append_array_basic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);

    uint32_t values[] = {10, 20, 30, 40, 50};
    nmo_result_t result = nmo_arena_array_append_array(&buffer, values, 5);
    
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(buffer.count, 5);
    
    uint32_t *data = (uint32_t *)buffer.data;
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(data[i], values[i]);
    }

    nmo_arena_destroy(arena);
}

TEST(buffer, append_array_empty) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);

    uint32_t values[] = {1, 2, 3};
    nmo_result_t result = nmo_arena_array_append_array(&buffer, values, 0);
    
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(buffer.count, 0);

    nmo_arena_destroy(arena);
}

TEST(buffer, append_array_multiple_times) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);

    uint32_t values1[] = {1, 2, 3};
    uint32_t values2[] = {4, 5, 6};
    
    nmo_arena_array_append_array(&buffer, values1, 3);
    nmo_arena_array_append_array(&buffer, values2, 3);
    
    ASSERT_EQ(buffer.count, 6);
    
    uint32_t *data = (uint32_t *)buffer.data;
    ASSERT_EQ(data[0], 1);
    ASSERT_EQ(data[2], 3);
    ASSERT_EQ(data[3], 4);
    ASSERT_EQ(data[5], 6);

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Access Operations Tests
 * ========================================================================== */

TEST(buffer, get_valid_index) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_alloc(&buffer, sizeof(uint32_t), 5, arena);
    
    uint32_t *data = (uint32_t *)buffer.data;
    for (int i = 0; i < 5; i++) {
        data[i] = i * 10;
    }

    for (int i = 0; i < 5; i++) {
        uint32_t *val = (uint32_t *)nmo_arena_array_get(&buffer, i);
        ASSERT_NOT_NULL(val);
        ASSERT_EQ(*val, i * 10);
    }

    nmo_arena_destroy(arena);
}

TEST(buffer, get_out_of_bounds) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_alloc(&buffer, sizeof(uint32_t), 5, arena);

    /* Beyond count */
    void *val = nmo_arena_array_get(&buffer, 10);
    ASSERT_NULL(val);

    nmo_arena_destroy(arena);
}

TEST(buffer, get_empty_buffer) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);

    void *val = nmo_arena_array_get(&buffer, 0);
    ASSERT_NULL(val);

    nmo_arena_destroy(arena);
}

TEST(buffer, set_valid_index) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_alloc(&buffer, sizeof(uint32_t), 5, arena);
    
    uint32_t new_value = 999;
    nmo_result_t result = nmo_arena_array_set(&buffer, 2, &new_value);
    
    ASSERT_EQ(result.code, NMO_OK);
    
    uint32_t *data = (uint32_t *)buffer.data;
    ASSERT_EQ(data[2], 999);

    nmo_arena_destroy(arena);
}

TEST(buffer, set_out_of_bounds) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_alloc(&buffer, sizeof(uint32_t), 5, arena);
    
    uint32_t new_value = 999;
    nmo_result_t result = nmo_arena_array_set(&buffer, 10, &new_value);
    
    ASSERT_NE(result.code, NMO_OK);

    nmo_arena_destroy(arena);
}

TEST(buffer, set_all_elements) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_alloc(&buffer, sizeof(uint32_t), 10, arena);
    
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t value = i * i;
        nmo_arena_array_set(&buffer, i, &value);
    }
    
    uint32_t *data = (uint32_t *)buffer.data;
    for (uint32_t i = 0; i < 10; i++) {
        ASSERT_EQ(data[i], i * i);
    }

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Capacity Management Tests
 * ========================================================================== */

TEST(buffer, reserve_increases_capacity) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);
    
    ASSERT_EQ(buffer.capacity, 0);
    
    nmo_result_t result = nmo_arena_array_reserve(&buffer, 20);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(buffer.capacity, 20);
    ASSERT_NOT_NULL(buffer.data);

    nmo_arena_destroy(arena);
}

TEST(buffer, reserve_preserves_data) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 2, arena);
    
    uint32_t v1 = 10, v2 = 20;
    nmo_arena_array_append(&buffer, &v1);
    nmo_arena_array_append(&buffer, &v2);
    
    nmo_arena_array_reserve(&buffer, 10);
    
    ASSERT_EQ(buffer.count, 2);
    uint32_t *data = (uint32_t *)buffer.data;
    ASSERT_EQ(data[0], 10);
    ASSERT_EQ(data[1], 20);

    nmo_arena_destroy(arena);
}

TEST(buffer, reserve_does_not_shrink) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 20, arena);
    
    ASSERT_EQ(buffer.capacity, 20);
    
    nmo_arena_array_reserve(&buffer, 5);
    ASSERT_EQ(buffer.capacity, 20); /* Should not shrink */

    nmo_arena_destroy(arena);
}

TEST(buffer, ensure_space_basic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);
    
    nmo_result_t result = nmo_arena_array_ensure_space(&buffer, 5);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_TRUE(buffer.capacity >= 5);

    nmo_arena_destroy(arena);
}

TEST(buffer, ensure_space_with_existing_data) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 2, arena);
    
    uint32_t v1 = 100, v2 = 200;
    nmo_arena_array_append(&buffer, &v1);
    nmo_arena_array_append(&buffer, &v2);
    
    /* Buffer is full (count=2, capacity=2), ensure space for 3 more */
    nmo_arena_array_ensure_space(&buffer, 3);
    
    ASSERT_TRUE(buffer.capacity >= 5);
    ASSERT_EQ(buffer.count, 2);
    
    /* Verify data preserved */
    uint32_t *data = (uint32_t *)buffer.data;
    ASSERT_EQ(data[0], 100);
    ASSERT_EQ(data[1], 200);

    nmo_arena_destroy(arena);
}

TEST(buffer, ensure_space_exponential_growth) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 4, arena);
    
    size_t old_capacity = buffer.capacity;
    
    /* Fill to capacity */
    for (int i = 0; i < 4; i++) {
        uint32_t val = i;
        nmo_arena_array_append(&buffer, &val);
    }
    
    /* Ensure space for 1 more - should double */
    nmo_arena_array_ensure_space(&buffer, 1);
    
    ASSERT_TRUE(buffer.capacity >= old_capacity * 2);

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Clear Operation Tests
 * ========================================================================== */

TEST(buffer, clear_resets_count) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 10, arena);
    
    for (int i = 0; i < 5; i++) {
        uint32_t val = i;
        nmo_arena_array_append(&buffer, &val);
    }
    
    ASSERT_EQ(buffer.count, 5);
    
    nmo_arena_array_clear(&buffer);
    
    ASSERT_EQ(buffer.count, 0);
    ASSERT_TRUE(buffer.capacity >= 10); /* Capacity unchanged */
    ASSERT_NOT_NULL(buffer.data); /* Data pointer unchanged */

    nmo_arena_destroy(arena);
}

TEST(buffer, clear_empty_buffer) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);
    
    nmo_arena_array_clear(&buffer);
    
    ASSERT_EQ(buffer.count, 0);

    nmo_arena_destroy(arena);
}

TEST(buffer, clear_and_reuse) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 5, arena);
    
    uint32_t val = 123;
    nmo_arena_array_append(&buffer, &val);
    nmo_arena_array_clear(&buffer);
    
    val = 456;
    nmo_arena_array_append(&buffer, &val);
    
    ASSERT_EQ(buffer.count, 1);
    uint32_t *data = (uint32_t *)buffer.data;
    ASSERT_EQ(data[0], 456);

    nmo_arena_destroy(arena);
}

TEST(buffer, lifecycle_dispose_callbacks) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    ASSERT_EQ(NMO_OK, nmo_arena_array_init(&buffer, sizeof(tracked_value_t), 0, arena).code);

    uint32_t disposed_total = 0;
    nmo_container_lifecycle_t lifecycle = {
        .dispose = tracked_value_dispose,
        .user_data = &disposed_total
    };
    nmo_arena_array_set_lifecycle(&buffer, &lifecycle);

    tracked_value_t v1 = {1}, v2 = {2}, v3 = {3}, v4 = {7}, replacement = {5};

    ASSERT_EQ(NMO_OK, nmo_arena_array_append(&buffer, &v1).code);
    ASSERT_EQ(NMO_OK, nmo_arena_array_append(&buffer, &v2).code);
    ASSERT_EQ(NMO_OK, nmo_arena_array_append(&buffer, &v3).code);

    ASSERT_EQ(NMO_OK, nmo_arena_array_set(&buffer, 1, &replacement).code);
    ASSERT_EQ(2u, disposed_total);

    ASSERT_EQ(NMO_OK, nmo_arena_array_remove(&buffer, 0, NULL).code);
    ASSERT_EQ(3u, disposed_total);

    ASSERT_EQ(NMO_OK, nmo_arena_array_append(&buffer, &v4).code);
    ASSERT_EQ(NMO_OK, nmo_arena_array_pop(&buffer, NULL).code);
    ASSERT_EQ(10u, disposed_total);

    nmo_arena_array_clear(&buffer);
    ASSERT_EQ(18u, disposed_total);

    nmo_arena_array_set_lifecycle(&buffer, NULL);
    ASSERT_EQ(NMO_OK, nmo_arena_array_append(&buffer, &v1).code);
    ASSERT_EQ(NMO_OK, nmo_arena_array_pop(&buffer, NULL).code);
    ASSERT_EQ(18u, disposed_total);

    nmo_arena_array_dispose(&buffer);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Set Data Operation Tests
 * ========================================================================== */

TEST(buffer, set_data_basic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);
    
    uint32_t *external_data = (uint32_t *)nmo_arena_alloc(arena, sizeof(uint32_t) * 5, sizeof(uint32_t));
    for (int i = 0; i < 5; i++) {
        external_data[i] = i * 100;
    }
    
    nmo_result_t result = nmo_arena_array_set_data(&buffer, external_data, 5);
    
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(buffer.data, external_data);
    ASSERT_EQ(buffer.count, 5);
    ASSERT_EQ(buffer.capacity, 5);
    
    uint32_t *data = (uint32_t *)buffer.data;
    ASSERT_EQ(data[0], 0);
    ASSERT_EQ(data[4], 400);

    nmo_arena_destroy(arena);
}

TEST(buffer, set_data_replaces_existing) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_alloc(&buffer, sizeof(uint32_t), 10, arena);
    
    uint32_t *new_data = (uint32_t *)nmo_arena_alloc(arena, sizeof(uint32_t) * 3, sizeof(uint32_t));
    new_data[0] = 111;
    new_data[1] = 222;
    new_data[2] = 333;
    
    nmo_arena_array_set_data(&buffer, new_data, 3);
    
    ASSERT_EQ(buffer.count, 3);
    ASSERT_EQ(buffer.data, new_data);

    nmo_arena_destroy(arena);
}

TEST(buffer, set_data_zero_count) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);
    
    uint32_t *data = (uint32_t *)nmo_arena_alloc(arena, sizeof(uint32_t), sizeof(uint32_t));
    nmo_result_t result = nmo_arena_array_set_data(&buffer, data, 0);
    
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(buffer.count, 0);

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Clone Operation Tests
 * ========================================================================== */

TEST(buffer, clone_basic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t src;
    nmo_arena_array_alloc(&src, sizeof(uint32_t), 5, arena);
    
    uint32_t *src_data = (uint32_t *)src.data;
    for (int i = 0; i < 5; i++) {
        src_data[i] = i * 10;
    }

    nmo_arena_array_t dest;
    nmo_result_t result = nmo_arena_array_clone(&src, &dest, arena);
    
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(dest.count, src.count);
    ASSERT_EQ(dest.element_size, src.element_size);
    ASSERT_NE(dest.data, src.data); /* Different memory */
    
    uint32_t *dest_data = (uint32_t *)dest.data;
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(dest_data[i], src_data[i]);
    }

    nmo_arena_destroy(arena);
}

TEST(buffer, clone_empty_buffer) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t src;
    nmo_arena_array_init(&src, sizeof(uint32_t), 0, arena);

    nmo_arena_array_t dest;
    nmo_result_t result = nmo_arena_array_clone(&src, &dest, arena);
    
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(dest.count, 0);
    ASSERT_EQ(dest.element_size, sizeof(uint32_t));

    nmo_arena_destroy(arena);
}

TEST(buffer, clone_preserves_independence) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t src;
    nmo_arena_array_alloc(&src, sizeof(uint32_t), 3, arena);
    
    uint32_t *src_data = (uint32_t *)src.data;
    src_data[0] = 100;
    src_data[1] = 200;
    src_data[2] = 300;

    nmo_arena_array_t dest;
    nmo_arena_array_clone(&src, &dest, arena);
    
    /* Modify source */
    src_data[0] = 999;
    
    /* Destination should be unchanged */
    uint32_t *dest_data = (uint32_t *)dest.data;
    ASSERT_EQ(dest_data[0], 100);
    ASSERT_EQ(dest_data[1], 200);
    ASSERT_EQ(dest_data[2], 300);

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Typed Macro Tests
 * ========================================================================== */

TEST(buffer, typed_macros_basic) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);
    
    uint32_t val1 = 10, val2 = 20, val3 = 30;
    NMO_ARENA_ARRAY_APPEND(uint32_t, &buffer, val1);
    NMO_ARENA_ARRAY_APPEND(uint32_t, &buffer, val2);
    NMO_ARENA_ARRAY_APPEND(uint32_t, &buffer, val3);
    
    ASSERT_EQ(buffer.count, 3);
    
    uint32_t *p1 = NMO_ARENA_ARRAY_GET(uint32_t, &buffer, 0);
    uint32_t *p2 = NMO_ARENA_ARRAY_GET(uint32_t, &buffer, 1);
    uint32_t *p3 = nmo_arena_array_get(uint32_t, &buffer, 2);
    
    ASSERT_NOT_NULL(p1);
    ASSERT_NOT_NULL(p2);
    ASSERT_NOT_NULL(p3);
    ASSERT_EQ(*p1, 10);
    ASSERT_EQ(*p2, 20);
    ASSERT_EQ(*p3, 30);

    nmo_arena_destroy(arena);
}

TEST(buffer, typed_data_macro) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_alloc(&buffer, sizeof(uint32_t), 5, arena);
    
    uint32_t *data = NMO_ARRAY_DATA(uint32_t, &buffer);
    ASSERT_NOT_NULL(data);
    
    for (int i = 0; i < 5; i++) {
        data[i] = i * i;
    }
    
    for (int i = 0; i < 5; i++) {
        uint32_t *val = nmo_arena_array_get(uint32_t, &buffer, i);
        ASSERT_EQ(*val, i * i);
    }

    nmo_arena_destroy(arena);
}

TEST(buffer, typed_macros_with_struct) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    typedef struct {
        int x, y;
    } point_t;

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(point_t), 0, arena);
    
    point_t p1 = {10, 20};
    point_t p2 = {30, 40};
    
    nmo_arena_array_append(point_t, &buffer, p1);
    nmo_arena_array_append(point_t, &buffer, p2);

    point_t *pp1 = nmo_arena_array_get(point_t, &buffer, 0);
    point_t *pp2 = nmo_arena_array_get(point_t, &buffer, 1);
    
    ASSERT_EQ(pp1->x, 10);
    ASSERT_EQ(pp1->y, 20);
    ASSERT_EQ(pp2->x, 30);
    ASSERT_EQ(pp2->y, 40);

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Convenience Operation Tests
 * ========================================================================== */

TEST(buffer, extend_single_element) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 2048);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_result_t result = nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);
    ASSERT_EQ(result.code, NMO_OK);

    uint32_t *slot = NULL;
    result = nmo_arena_array_extend(&buffer, 1, slot);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(slot);

    *slot = 1234;
    ASSERT_EQ(buffer.count, 1);
    ASSERT_EQ(((uint32_t *)buffer.data)[0], 1234U);

    nmo_arena_destroy(arena);
}

TEST(buffer, extend_multiple_elements) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 2048);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_result_t result = nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);
    ASSERT_EQ(result.code, NMO_OK);

    uint32_t *values = NULL;
    result = nmo_arena_array_extend(&buffer, 5, values);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(buffer.count, 5);
    ASSERT_NOT_NULL(values);

    for (uint32_t i = 0; i < 5; ++i) {
        values[i] = i * 10;
    }

    uint32_t *data = (uint32_t *)buffer.data;
    for (uint32_t i = 0; i < 5; ++i) {
        ASSERT_EQ(data[i], i * 10);
    }

    nmo_arena_destroy(arena);
}

TEST(buffer, extend_zero_returns_end_pointer) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 2048);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);

    uint32_t val = 88;
    nmo_arena_array_append(&buffer, &val);

    uint32_t *end_ptr = NULL;
    nmo_result_t result = nmo_arena_array_extend(&buffer, 0, end_ptr);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_NOT_NULL(end_ptr);
    ASSERT_EQ((uintptr_t)end_ptr, (uintptr_t)((uint32_t *)buffer.data + buffer.count));

    nmo_arena_destroy(arena);
}

TEST(buffer, pop_returns_last_element) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 2048);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);

    for (uint32_t i = 0; i < 3; ++i) {
        nmo_arena_array_append(&buffer, &i);
    }

    uint32_t popped = 0;
    nmo_result_t result = nmo_arena_array_pop(&buffer, &popped);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(popped, 2U);
    ASSERT_EQ(buffer.count, 2);

    nmo_arena_destroy(arena);
}

TEST(buffer, remove_middle_shifts_elements) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);

    for (uint32_t i = 0; i < 5; ++i) {
        nmo_arena_array_append(&buffer, &i);
    }

    uint32_t removed = 0;
    nmo_result_t result = nmo_arena_array_remove(&buffer, 2, &removed);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(removed, 2U);
    ASSERT_EQ(buffer.count, 4);

    uint32_t *data = (uint32_t *)buffer.data;
    uint32_t expected[] = {0, 1, 3, 4};
    for (size_t i = 0; i < buffer.count; ++i) {
        ASSERT_EQ(data[i], expected[i]);
    }

    nmo_arena_destroy(arena);
}

TEST(buffer, insert_middle_shifts_elements) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);

    uint32_t values[] = {0, 2, 3};
    nmo_arena_array_append_array(&buffer, values, 3);

    uint32_t insert_value = 1;
    nmo_result_t result = nmo_arena_array_insert(&buffer, 1, &insert_value);
    ASSERT_EQ(result.code, NMO_OK);
    ASSERT_EQ(buffer.count, 4);

    uint32_t *data = (uint32_t *)buffer.data;
    uint32_t expected[] = {0, 1, 2, 3};
    for (size_t i = 0; i < buffer.count; ++i) {
        ASSERT_EQ(data[i], expected[i]);
    }

    nmo_arena_destroy(arena);
}

TEST(buffer, front_back_helpers) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 2048);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);

    uint32_t first = 10;
    uint32_t second = 20;
    nmo_arena_array_append(&buffer, &first);
    nmo_arena_array_append(&buffer, &second);

    uint32_t *front = nmo_arena_array_front(uint32_t, &buffer);
    uint32_t *back = nmo_arena_array_back(uint32_t, &buffer);
    ASSERT_NOT_NULL(front);
    ASSERT_NOT_NULL(back);
    ASSERT_EQ(*front, 10U);
    ASSERT_EQ(*back, 20U);

    nmo_arena_array_clear(&buffer);
    ASSERT_NULL(nmo_arena_array_front(&buffer));
    ASSERT_NULL(nmo_arena_array_back(&buffer));

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Edge Cases and Error Handling Tests
 * ========================================================================== */

TEST(buffer, null_buffer_parameter) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_result_t result = nmo_arena_array_init(NULL, sizeof(uint32_t), 0, arena);
    ASSERT_NE(result.code, NMO_OK);

    nmo_arena_destroy(arena);
}

TEST(buffer, null_arena_parameter) {
    nmo_arena_array_t buffer;
    nmo_result_t result = nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, NULL);
    ASSERT_NE(result.code, NMO_OK);
}

TEST(buffer, zero_element_size) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_result_t result = nmo_arena_array_init(&buffer, 0, 0, arena);
    ASSERT_NE(result.code, NMO_OK);

    nmo_arena_destroy(arena);
}

TEST(buffer, append_null_element) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);
    
    nmo_result_t result = nmo_arena_array_append(&buffer, NULL);
    ASSERT_NE(result.code, NMO_OK);

    nmo_arena_destroy(arena);
}

TEST(buffer, append_array_null_elements) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);
    
    nmo_result_t result = nmo_arena_array_append_array(&buffer, NULL, 5);
    ASSERT_NE(result.code, NMO_OK);

    nmo_arena_destroy(arena);
}

TEST(buffer, set_null_element) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_alloc(&buffer, sizeof(uint32_t), 5, arena);
    
    nmo_result_t result = nmo_arena_array_set(&buffer, 0, NULL);
    ASSERT_NE(result.code, NMO_OK);

    nmo_arena_destroy(arena);
}

TEST(buffer, set_data_null_data) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);
    
    /* NULL data with count > 0 should fail */
    nmo_result_t result = nmo_arena_array_set_data(&buffer, NULL, 5);
    ASSERT_NE(result.code, NMO_OK);

    nmo_arena_destroy(arena);
}

TEST(buffer, clone_null_source) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t dest;
    nmo_result_t result = nmo_arena_array_clone(NULL, &dest, arena);
    ASSERT_NE(result.code, NMO_OK);

    nmo_arena_destroy(arena);
}

TEST(buffer, clone_null_destination) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t src;
    nmo_arena_array_init(&src, sizeof(uint32_t), 0, arena);
    
    nmo_result_t result = nmo_arena_array_clone(&src, NULL, arena);
    ASSERT_NE(result.code, NMO_OK);

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Memory and Data Integrity Tests
 * ========================================================================== */

TEST(buffer, data_integrity_after_growth) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 2, arena);
    
    /* Fill initial capacity */
    uint32_t val1 = 111, val2 = 222;
    nmo_arena_array_append(&buffer, &val1);
    nmo_arena_array_append(&buffer, &val2);
    
    /* Trigger growth */
    uint32_t val3 = 333, val4 = 444;
    nmo_arena_array_append(&buffer, &val3);
    nmo_arena_array_append(&buffer, &val4);
    
    /* Verify all data preserved */
    uint32_t *data = (uint32_t *)buffer.data;
    ASSERT_EQ(data[0], 111);
    ASSERT_EQ(data[1], 222);
    ASSERT_EQ(data[2], 333);
    ASSERT_EQ(data[3], 444);

    nmo_arena_destroy(arena);
}

TEST(buffer, large_buffer_operations) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);
    
    /* Append 1000 elements */
    for (uint32_t i = 0; i < 1000; i++) {
        nmo_arena_array_append(&buffer, &i);
    }
    
    ASSERT_EQ(buffer.count, 1000);
    
    /* Verify all elements */
    for (uint32_t i = 0; i < 1000; i++) {
        uint32_t *val = (uint32_t *)nmo_arena_array_get(&buffer, i);
        ASSERT_NOT_NULL(val);
        ASSERT_EQ(*val, i);
    }

    nmo_arena_destroy(arena);
}

TEST(buffer, mixed_operations_sequence) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);
    
    /* Append some elements */
    uint32_t val = 10;
    nmo_arena_array_append(&buffer, &val);
    val = 20;
    nmo_arena_array_append(&buffer, &val);
    
    /* Reserve more space */
    nmo_arena_array_reserve(&buffer, 10);
    
    /* Append array */
    uint32_t arr[] = {30, 40, 50};
    nmo_arena_array_append_array(&buffer, arr, 3);
    
    /* Modify element */
    val = 99;
    nmo_arena_array_set(&buffer, 1, &val);
    
    /* Verify final state */
    ASSERT_EQ(buffer.count, 5);
    uint32_t *data = (uint32_t *)buffer.data;
    ASSERT_EQ(data[0], 10);
    ASSERT_EQ(data[1], 99);  /* Modified */
    ASSERT_EQ(data[2], 30);
    ASSERT_EQ(data[3], 40);
    ASSERT_EQ(data[4], 50);

    nmo_arena_destroy(arena);
}

TEST(buffer, different_element_sizes_integrity) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NOT_NULL(arena);

    /* uint8_t buffer */
    nmo_arena_array_t buffer1;
    nmo_arena_array_alloc(&buffer1, sizeof(uint8_t), 10, arena);
    uint8_t *data1 = (uint8_t *)buffer1.data;
    for (int i = 0; i < 10; i++) {
        data1[i] = (uint8_t)(i + 100);
    }

    /* uint64_t buffer */
    nmo_arena_array_t buffer2;
    nmo_arena_array_alloc(&buffer2, sizeof(uint64_t), 10, arena);
    uint64_t *data2 = (uint64_t *)buffer2.data;
    for (int i = 0; i < 10; i++) {
        data2[i] = (uint64_t)i * 1000000000ULL;
    }

    /* Verify both buffers */
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(data1[i], (uint8_t)(i + 100));
        ASSERT_EQ(data2[i], (uint64_t)i * 1000000000ULL);
    }

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Stress Tests
 * ========================================================================== */

TEST(buffer, stress_append_many_elements) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 262144); /* 256KB */
    ASSERT_NOT_NULL(arena);

    nmo_arena_array_t buffer;
    nmo_arena_array_init(&buffer, sizeof(uint32_t), 0, arena);
    
    /* Append 10000 elements */
    for (uint32_t i = 0; i < 10000; i++) {
        nmo_result_t result = nmo_arena_array_append(&buffer, &i);
        ASSERT_EQ(result.code, NMO_OK);
    }
    
    ASSERT_EQ(buffer.count, 10000);
    
    /* Spot check elements */
    uint32_t *val0 = (uint32_t *)nmo_arena_array_get(&buffer, 0);
    uint32_t *val5000 = (uint32_t *)nmo_arena_array_get(&buffer, 5000);
    uint32_t *val9999 = (uint32_t *)nmo_arena_array_get(&buffer, 9999);
    
    ASSERT_EQ(*val0, 0);
    ASSERT_EQ(*val5000, 5000);
    ASSERT_EQ(*val9999, 9999);

    nmo_arena_destroy(arena);
}

TEST(buffer, stress_multiple_buffers) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
    ASSERT_NOT_NULL(arena);

    /* Create 100 buffers */
    nmo_arena_array_t buffers[100];
    
    for (int i = 0; i < 100; i++) {
        nmo_arena_array_init(&buffers[i], sizeof(uint32_t), 0, arena);
        
        /* Add elements to each buffer */
        for (int j = 0; j < 10; j++) {
            uint32_t val = i * 100 + j;
            nmo_arena_array_append(&buffers[i], &val);
        }
    }
    
    /* Verify all buffers */
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(buffers[i].count, 10);
        
        uint32_t *data = (uint32_t *)buffers[i].data;
        for (int j = 0; j < 10; j++) {
            ASSERT_EQ(data[j], (uint32_t)(i * 100 + j));
        }
    }

    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Main
 * ========================================================================== */

TEST_MAIN_BEGIN()
    /* Initialization Tests */
    REGISTER_TEST(buffer, init_basic);
    REGISTER_TEST(buffer, init_with_capacity);
    REGISTER_TEST(buffer, init_different_element_sizes);
    REGISTER_TEST(buffer, alloc_basic);
    REGISTER_TEST(buffer, alloc_zero_count);
    REGISTER_TEST(buffer, alloc_large_count);

    /* Append Operations Tests */
    REGISTER_TEST(buffer, append_single_element);
    REGISTER_TEST(buffer, append_multiple_elements);
    REGISTER_TEST(buffer, append_triggers_growth);
    REGISTER_TEST(buffer, append_array_basic);
    REGISTER_TEST(buffer, append_array_empty);
    REGISTER_TEST(buffer, append_array_multiple_times);

    /* Access Operations Tests */
    REGISTER_TEST(buffer, get_valid_index);
    REGISTER_TEST(buffer, get_out_of_bounds);
    REGISTER_TEST(buffer, get_empty_buffer);
    REGISTER_TEST(buffer, set_valid_index);
    REGISTER_TEST(buffer, set_out_of_bounds);
    REGISTER_TEST(buffer, set_all_elements);

    /* Capacity Management Tests */
    REGISTER_TEST(buffer, reserve_increases_capacity);
    REGISTER_TEST(buffer, reserve_preserves_data);
    REGISTER_TEST(buffer, reserve_does_not_shrink);
    REGISTER_TEST(buffer, ensure_space_basic);
    REGISTER_TEST(buffer, ensure_space_with_existing_data);
    REGISTER_TEST(buffer, ensure_space_exponential_growth);

    /* Clear Operation Tests */
    REGISTER_TEST(buffer, clear_resets_count);
    REGISTER_TEST(buffer, clear_empty_buffer);
    REGISTER_TEST(buffer, clear_and_reuse);
    REGISTER_TEST(buffer, lifecycle_dispose_callbacks);

    /* Set Data Operation Tests */
    REGISTER_TEST(buffer, set_data_basic);
    REGISTER_TEST(buffer, set_data_replaces_existing);
    REGISTER_TEST(buffer, set_data_zero_count);

    /* Clone Operation Tests */
    REGISTER_TEST(buffer, clone_basic);
    REGISTER_TEST(buffer, clone_empty_buffer);
    REGISTER_TEST(buffer, clone_preserves_independence);

    /* Typed Macro Tests */
    REGISTER_TEST(buffer, typed_macros_basic);
    REGISTER_TEST(buffer, typed_data_macro);
    REGISTER_TEST(buffer, typed_macros_with_struct);

    /* Convenience Operation Tests */
    REGISTER_TEST(buffer, extend_single_element);
    REGISTER_TEST(buffer, extend_multiple_elements);
    REGISTER_TEST(buffer, extend_zero_returns_end_pointer);
    REGISTER_TEST(buffer, pop_returns_last_element);
    REGISTER_TEST(buffer, remove_middle_shifts_elements);
    REGISTER_TEST(buffer, insert_middle_shifts_elements);
    REGISTER_TEST(buffer, front_back_helpers);

    /* Edge Cases and Error Handling Tests */
    REGISTER_TEST(buffer, null_buffer_parameter);
    REGISTER_TEST(buffer, null_arena_parameter);
    REGISTER_TEST(buffer, zero_element_size);
    REGISTER_TEST(buffer, append_null_element);
    REGISTER_TEST(buffer, append_array_null_elements);
    REGISTER_TEST(buffer, set_null_element);
    REGISTER_TEST(buffer, set_data_null_data);
    REGISTER_TEST(buffer, clone_null_source);
    REGISTER_TEST(buffer, clone_null_destination);

    /* Memory and Data Integrity Tests */
    REGISTER_TEST(buffer, data_integrity_after_growth);
    REGISTER_TEST(buffer, large_buffer_operations);
    REGISTER_TEST(buffer, mixed_operations_sequence);
    REGISTER_TEST(buffer, different_element_sizes_integrity);

    /* Stress Tests */
    REGISTER_TEST(buffer, stress_append_many_elements);
    REGISTER_TEST(buffer, stress_multiple_buffers);
TEST_MAIN_END()

