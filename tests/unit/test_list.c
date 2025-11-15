/**
 * @file test_list.c
 * @brief Unit tests for arena-backed list.
 */

#include "../test_framework.h"
#include "core/nmo_list.h"
#include <stdint.h>
#include <string.h>

static void sum_dispose(void *element, void *user_data) {
    uint32_t *total = (uint32_t *)user_data;
    *total += *(uint32_t *)element;
}

TEST(list, push_and_pop) {
    nmo_list_t *list = nmo_list_create(NULL, sizeof(uint32_t));
    ASSERT_NOT_NULL(list);
    ASSERT_EQ(1, nmo_list_is_empty(list));

    for (uint32_t i = 0; i < 5; ++i) {
        ASSERT_NOT_NULL(nmo_list_push_back(list, &i));
    }
    ASSERT_EQ(5u, nmo_list_get_count(list));

    uint32_t expected = 0;
    nmo_list_node_t *node = nmo_list_begin(list);
    while (node) {
        uint32_t value = *(uint32_t *)nmo_list_node_get(node);
        ASSERT_EQ(expected, value);
        expected++;
        node = nmo_list_next(node);
    }

    uint32_t out = 0;
    ASSERT_EQ(1, nmo_list_pop_front(list, &out));
    ASSERT_EQ(0u, out);
    ASSERT_EQ(4u, nmo_list_get_count(list));

    ASSERT_EQ(1, nmo_list_pop_back(list, &out));
    ASSERT_EQ(4u, out);
    ASSERT_EQ(3u, nmo_list_get_count(list));

    nmo_list_destroy(list);
}

TEST(list, insert_and_remove) {
    nmo_list_t *list = nmo_list_create(NULL, sizeof(uint32_t));
    ASSERT_NOT_NULL(list);

    uint32_t one = 1, two = 2, three = 3;
    nmo_list_node_t *n1 = nmo_list_push_back(list, &one);
    nmo_list_node_t *n2 = nmo_list_insert_after(list, n1, &three);
    ASSERT_NOT_NULL(n2);
    ASSERT_EQ(2u, nmo_list_get_count(list));

    nmo_list_node_t *n3 = nmo_list_insert_before(list, n2, &two);
    ASSERT_NOT_NULL(n3);
    ASSERT_EQ(3u, nmo_list_get_count(list));

    uint32_t expected[] = {1, 2, 3};
    size_t idx = 0;
    for (nmo_list_node_t *it = nmo_list_begin(list); it; it = nmo_list_next(it)) {
        ASSERT_EQ(expected[idx], *(uint32_t *)nmo_list_node_get(it));
        idx++;
    }

    uint32_t removed = 0;
    nmo_list_remove(list, n3, &removed);
    ASSERT_EQ(2u, nmo_list_get_count(list));
    ASSERT_EQ(2u, removed);

    nmo_list_clear(list);
    ASSERT_EQ(0u, nmo_list_get_count(list));
    ASSERT_EQ(1, nmo_list_is_empty(list));

    nmo_list_destroy(list);
}

TEST(list, lifecycle_tracking) {
    nmo_list_t *list = nmo_list_create(NULL, sizeof(uint32_t));
    ASSERT_NOT_NULL(list);

    uint32_t total = 0;
    nmo_container_lifecycle_t lifecycle = {
        .dispose = sum_dispose,
        .user_data = &total
    };
    nmo_list_set_lifecycle(list, &lifecycle);

    for (uint32_t i = 1; i <= 4; ++i) {
        nmo_list_push_back(list, &i);
    }

    uint32_t tmp = 0;
    ASSERT_EQ(1, nmo_list_pop_front(list, &tmp));
    ASSERT_EQ(1u, tmp);
    ASSERT_EQ(1u, total); // disposed once

    nmo_list_clear(list);
    ASSERT_EQ(1 + 2 + 3 + 4, total);

    nmo_list_destroy(list);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(list, push_and_pop);
    REGISTER_TEST(list, insert_and_remove);
    REGISTER_TEST(list, lifecycle_tracking);
TEST_MAIN_END()
