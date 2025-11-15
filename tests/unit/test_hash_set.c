/**
 * @file test_hash_set.c
 * @brief Unit tests for allocator-backed hash set.
 */

#include "../test_framework.h"
#include "core/nmo_hash_set.h"
#include "core/nmo_hash.h"
#include "core/nmo_error.h"
#include <stdint.h>
#include <string.h>

static int count_set_iterator(const void *key, void *user_data) {
    (void)key;
    int *count = (int *)user_data;
    (*count)++;
    return 1;
}

static void dispose_tracker(void *element, void *user_data) {
    uint32_t *total = (uint32_t *)user_data;
    *total += *(uint32_t *)element;
}

TEST(hash_set, basic_operations) {
    nmo_hash_set_t *set = nmo_hash_set_create(
        NULL,
        sizeof(uint32_t),
        0,
        nmo_hash_uint32,
        NULL);
    ASSERT_NOT_NULL(set);

    uint32_t key = 42;
    ASSERT_EQ(NMO_OK, nmo_hash_set_insert(set, &key));
    ASSERT_EQ(1u, nmo_hash_set_get_count(set));
    ASSERT_EQ(1, nmo_hash_set_contains(set, &key));

    // Duplicate insert should report already exists
    ASSERT_EQ(NMO_ERR_ALREADY_EXISTS, nmo_hash_set_insert(set, &key));
    ASSERT_EQ(1u, nmo_hash_set_get_count(set));

    ASSERT_EQ(1, nmo_hash_set_remove(set, &key));
    ASSERT_EQ(0u, nmo_hash_set_get_count(set));
    ASSERT_EQ(0, nmo_hash_set_contains(set, &key));

    nmo_hash_set_destroy(set);
}

TEST(hash_set, growth_and_reserve) {
    nmo_hash_set_t *set = nmo_hash_set_create(
        NULL,
        sizeof(uint32_t),
        4,
        nmo_hash_uint32,
        NULL);
    ASSERT_NOT_NULL(set);

    ASSERT_EQ(NMO_OK, nmo_hash_set_reserve(set, 64));
    ASSERT_GE(nmo_hash_set_get_capacity(set), 64u);

    for (uint32_t i = 0; i < 100; ++i) {
        ASSERT_EQ(NMO_OK, nmo_hash_set_insert(set, &i));
    }
    ASSERT_EQ(100u, nmo_hash_set_get_count(set));

    for (uint32_t i = 0; i < 100; ++i) {
        ASSERT_EQ(1, nmo_hash_set_contains(set, &i));
    }

    nmo_hash_set_clear(set);
    ASSERT_EQ(0u, nmo_hash_set_get_count(set));

    nmo_hash_set_destroy(set);
}

TEST(hash_set, iterate_and_lifecycle) {
    nmo_hash_set_t *set = nmo_hash_set_create(
        NULL,
        sizeof(uint32_t),
        0,
        nmo_hash_uint32,
        NULL);
    ASSERT_NOT_NULL(set);

    uint32_t dispose_sum = 0;
    nmo_container_lifecycle_t lifecycle = {
        .dispose = dispose_tracker,
        .user_data = &dispose_sum
    };
    nmo_hash_set_set_lifecycle(set, &lifecycle);

    for (uint32_t i = 1; i <= 5; ++i) {
        ASSERT_EQ(NMO_OK, nmo_hash_set_insert(set, &i));
    }

    int count = 0;
    nmo_hash_set_iterate(set, count_set_iterator, &count);
    ASSERT_EQ(5, count);

    // Removing triggers dispose
    uint32_t key = 3;
    ASSERT_EQ(1, nmo_hash_set_remove(set, &key));
    ASSERT_EQ(3u, dispose_sum);

    nmo_hash_set_clear(set);
    ASSERT_EQ(1 + 2 + 4 + 5, dispose_sum);

    nmo_hash_set_destroy(set);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(hash_set, basic_operations);
    REGISTER_TEST(hash_set, growth_and_reserve);
    REGISTER_TEST(hash_set, iterate_and_lifecycle);
TEST_MAIN_END()
