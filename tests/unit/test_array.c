/**
 * @file test_array.c
 * @brief Unit tests for allocator-backed nmo_array
 */

#include "nmo.h"
#include "test_framework.h"

#include <stdint.h>
#include <string.h>

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

TEST(nmo_array, init_with_capacity) {
    nmo_array_t array;
    nmo_result_t result = nmo_array_init(&array, sizeof(uint32_t), 8, NULL);

    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_NOT_NULL(array.data);
    ASSERT_EQ(0u, array.count);
    ASSERT_EQ(8u, array.capacity);
    ASSERT_EQ(sizeof(uint32_t), array.element_size);

    nmo_array_dispose(&array);
}

TEST(nmo_array, alloc_sets_initial_count) {
    nmo_array_t array;
    nmo_result_t result = nmo_array_alloc(&array, sizeof(uint16_t), 5, NULL);

    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_NOT_NULL(array.data);
    ASSERT_EQ(5u, array.count);
    ASSERT_EQ(5u, array.capacity);
    ASSERT_EQ(sizeof(uint16_t), array.element_size);

    nmo_array_dispose(&array);
}

TEST(nmo_array, append_and_get) {
    nmo_array_t array;
    nmo_result_t result = nmo_array_init(&array, sizeof(uint32_t), 0, NULL);
    ASSERT_EQ(NMO_OK, result.code);

    for (uint32_t i = 0; i < 10; ++i) {
        NMO_ARRAY_APPEND(uint32_t, &array, i);
    }

    ASSERT_EQ(10u, array.count);
    ASSERT_TRUE(nmo_array_capacity(&array) >= array.count);

    uint32_t *front = nmo_array_front(&array);
    uint32_t *back = nmo_array_back(&array);
    ASSERT_NOT_NULL(front);
    ASSERT_NOT_NULL(back);
    ASSERT_EQ(0u, *front);
    ASSERT_EQ(9u, *back);

    for (uint32_t i = 0; i < array.count; ++i) {
        uint32_t *value = NMO_ARRAY_GET(uint32_t, &array, i);
        ASSERT_NOT_NULL(value);
        ASSERT_EQ(i, *value);
    }

    nmo_array_dispose(&array);
}

TEST(nmo_array, set_insert_remove_pop) {
    nmo_array_t array;
    nmo_array_init(&array, sizeof(uint32_t), 0, NULL);

    for (uint32_t i = 1; i <= 3; ++i) {
        nmo_array_append(&array, &i);
    }

    uint32_t replacement = 100;
    nmo_result_t result = nmo_array_set(&array, 1, &replacement);
    ASSERT_EQ(NMO_OK, result.code);

    uint32_t insert_value = 200;
    result = nmo_array_insert(&array, 1, &insert_value);
    ASSERT_EQ(NMO_OK, result.code);

    uint32_t removed = 0;
    result = nmo_array_remove(&array, 2, &removed);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(100u, removed);

    uint32_t popped = 0;
    result = nmo_array_pop(&array, &popped);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3u, popped);

    uint32_t *values = (uint32_t *)array.data;
    ASSERT_EQ(1u, values[0]);
    ASSERT_EQ(200u, values[1]);

    nmo_array_dispose(&array);
}

TEST(nmo_array, append_array_and_extend) {
    nmo_array_t array;
    nmo_array_init(&array, sizeof(uint32_t), 0, NULL);

    uint32_t initial[] = {1, 2, 3};
    nmo_result_t result = nmo_array_append_array(&array, initial, 3);
    ASSERT_EQ(NMO_OK, result.code);

    void *block = NULL;
    result = nmo_array_extend(&array, 2, &block);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_NOT_NULL(block);

    uint32_t *extended = (uint32_t *)block;
    extended[0] = 4;
    extended[1] = 5;

    ASSERT_EQ(5u, array.count);
    for (uint32_t i = 0; i < array.count; ++i) {
        uint32_t *value = NMO_ARRAY_GET(uint32_t, &array, i);
        ASSERT_EQ(i + 1u, *value);
    }

    nmo_array_dispose(&array);
}

TEST(nmo_array, reserve_and_ensure_space) {
    nmo_array_t array;
    nmo_array_init(&array, sizeof(uint32_t), 0, NULL);

    nmo_result_t result = nmo_array_reserve(&array, 4);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(4u, array.capacity);

    for (uint32_t i = 0; i < 4; ++i) {
        nmo_array_append(&array, &i);
    }

    result = nmo_array_ensure_space(&array, 10);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_TRUE(array.capacity >= array.count + 10);

    nmo_array_dispose(&array);
}

TEST(nmo_array, lifecycle_callbacks) {
    nmo_array_t array;
    nmo_array_init(&array, sizeof(tracked_value_t), 0, NULL);

    uint32_t disposed_total = 0;
    nmo_container_lifecycle_t lifecycle = {
        .dispose = tracked_value_dispose,
        .user_data = &disposed_total
    };
    nmo_array_set_lifecycle(&array, &lifecycle);

    for (uint32_t i = 1; i <= 3; ++i) {
        tracked_value_t value = {.id = i};
        nmo_array_append(&array, &value);
    }

    nmo_array_clear(&array);
    ASSERT_EQ(6u, disposed_total);
    ASSERT_EQ(0u, array.count);

    nmo_array_dispose(&array);
}

TEST(nmo_array, set_data_and_clone) {
    nmo_array_t array;
    nmo_array_init(&array, sizeof(uint32_t), 0, NULL);

    uint32_t values[3] = {42, 43, 44};
    size_t bytes = sizeof(values);
    uint32_t *data = nmo_alloc(&array.allocator, bytes, sizeof(uint32_t));
    ASSERT_NOT_NULL(data);
    memcpy(data, values, bytes);

    nmo_result_t result = nmo_array_set_data(&array, data, 3);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3u, array.count);
    ASSERT_EQ(3u, array.capacity);

    nmo_array_t clone;
    result = nmo_array_clone(&array, &clone, NULL);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(array.count, clone.count);
    ASSERT_TRUE(memcmp(array.data, clone.data, bytes) == 0);

    nmo_array_dispose(&clone);
    nmo_array_dispose(&array);
}

TEST(nmo_array, swap_resize_and_shrink) {
    nmo_array_t first, second;
    nmo_array_init(&first, sizeof(uint32_t), 0, NULL);
    nmo_array_init(&second, sizeof(uint32_t), 0, NULL);

    uint32_t a_values[] = {10, 20};
    uint32_t b_values[] = {1, 2, 3};
    nmo_array_append_array(&first, a_values, 2);
    nmo_array_append_array(&second, b_values, 3);

    nmo_result_t result = nmo_array_swap(&first, &second);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3u, first.count);
    ASSERT_EQ(2u, second.count);

    result = nmo_array_resize(&first, 5);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(5u, first.count);
    uint32_t *grown = NMO_ARRAY_GET(uint32_t, &first, 4);
    ASSERT_NOT_NULL(grown);
    ASSERT_EQ(0u, *grown);

    result = nmo_array_shrink_to_fit(&first);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(first.capacity, first.count);

    nmo_array_dispose(&first);
    nmo_array_dispose(&second);
}

TEST(nmo_array, invalid_arguments) {
    nmo_array_t array;
    nmo_array_init(&array, sizeof(uint32_t), 0, NULL);

    uint32_t value = 1;
    nmo_result_t result = nmo_array_set(&array, 0, &value);
    ASSERT_NE(NMO_OK, result.code);

    result = nmo_array_insert(&array, 0, NULL);
    ASSERT_NE(NMO_OK, result.code);

    result = nmo_array_remove(&array, 0, NULL);
    ASSERT_NE(NMO_OK, result.code);

    result = nmo_array_pop(&array, NULL);
    ASSERT_NE(NMO_OK, result.code);

    ASSERT_NULL(nmo_array_get(&array, 0));
    ASSERT_NULL(nmo_array_front(&array));
    ASSERT_NULL(nmo_array_back(&array));

    nmo_array_dispose(&array);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(nmo_array, init_with_capacity);
    REGISTER_TEST(nmo_array, alloc_sets_initial_count);
    REGISTER_TEST(nmo_array, append_and_get);
    REGISTER_TEST(nmo_array, set_insert_remove_pop);
    REGISTER_TEST(nmo_array, append_array_and_extend);
    REGISTER_TEST(nmo_array, reserve_and_ensure_space);
    REGISTER_TEST(nmo_array, lifecycle_callbacks);
    REGISTER_TEST(nmo_array, set_data_and_clone);
    REGISTER_TEST(nmo_array, swap_resize_and_shrink);
    REGISTER_TEST(nmo_array, invalid_arguments);
TEST_MAIN_END()
