/**
 * @file test_indexed_map.c
 * @brief Unit tests for generic indexed map
 */

#include "../test_framework.h"
#include "core/nmo_indexed_map.h"
#include "core/nmo_error.h"
#include <string.h>

/**
 * Test basic indexed map operations
 */
TEST(indexed_map, basic) {
    nmo_indexed_map_t *map = nmo_indexed_map_create(NULL, 
        sizeof(uint32_t),
        sizeof(uint32_t),
        0,
        nmo_map_hash_uint32,
        NULL
    );

    ASSERT_NOT_NULL(map);
    ASSERT_EQ(nmo_indexed_map_get_count(map), 0);

    // Insert some entries
    uint32_t key1 = 100;
    uint32_t value1 = 200;
    ASSERT_EQ(nmo_indexed_map_insert(map, &key1, &value1), NMO_OK);
    ASSERT_EQ(nmo_indexed_map_get_count(map), 1);

    // Get value by key
    uint32_t retrieved = 0;
    ASSERT_EQ(nmo_indexed_map_get(map, &key1, &retrieved), 1);
    ASSERT_EQ(retrieved, value1);

    // Get value by index
    uint32_t key_at_0 = 0;
    uint32_t value_at_0 = 0;
    ASSERT_EQ(nmo_indexed_map_get_at(map, 0, &key_at_0, &value_at_0), 1);
    ASSERT_EQ(key_at_0, key1);
    ASSERT_EQ(value_at_0, value1);

    nmo_indexed_map_destroy(map);
}

/**
 * Test indexed map with multiple entries
 */
TEST(indexed_map, multiple) {
    nmo_indexed_map_t *map = nmo_indexed_map_create(NULL, 
        sizeof(uint32_t),
        sizeof(uint32_t),
        4,
        nmo_map_hash_uint32,
        NULL
    );

    ASSERT_NOT_NULL(map);

    // Insert multiple entries
    for (uint32_t i = 0; i < 50; i++) {
        uint32_t key = i;
        uint32_t value = i * 10;
        ASSERT_EQ(nmo_indexed_map_insert(map, &key, &value), NMO_OK);
    }

    ASSERT_EQ(nmo_indexed_map_get_count(map), 50);

    // Verify all entries by key
    for (uint32_t i = 0; i < 50; i++) {
        uint32_t key = i;
        uint32_t value = 0;
        ASSERT_EQ(nmo_indexed_map_get(map, &key, &value), 1);
        ASSERT_EQ(value, i * 10);
    }

    // Verify all entries by index
    for (size_t i = 0; i < 50; i++) {
        uint32_t key = 0;
        uint32_t value = 0;
        ASSERT_EQ(nmo_indexed_map_get_at(map, i, &key, &value), 1);
        ASSERT_EQ(value, key * 10);
    }

    nmo_indexed_map_destroy(map);
}

/**
 * Test indexed map iteration
 */
static int sum_iterator(const void *key, void *value, void *user_data) {
    (void)key;
    uint32_t val = *(uint32_t *)value;
    uint32_t *sum = (uint32_t *)user_data;
    *sum += val;
    return 1;
}

static void indexed_map_key_dispose(void *element, void *user_data) {
    uint32_t *total = (uint32_t *)user_data;
    *total += *(uint32_t *)element;
}

static void indexed_map_value_dispose(void *element, void *user_data) {
    uint32_t *total = (uint32_t *)user_data;
    *total += *(uint32_t *)element;
}

TEST(indexed_map, iterate) {
    nmo_indexed_map_t *map = nmo_indexed_map_create(NULL, 
        sizeof(uint32_t),
        sizeof(uint32_t),
        0,
        nmo_map_hash_uint32,
        NULL
    );

    ASSERT_NOT_NULL(map);

    // Insert entries: keys 1-10, values 1-10
    for (uint32_t i = 1; i <= 10; i++) {
        uint32_t key = i;
        uint32_t value = i;
        nmo_indexed_map_insert(map, &key, &value);
    }

    // Iterate and sum values
    uint32_t sum = 0;
    nmo_indexed_map_iterate(map, sum_iterator, &sum);
    ASSERT_EQ(sum, 55);

    nmo_indexed_map_destroy(map);
}

/**
 * Test indexed map removal
 */
TEST(indexed_map, remove) {
    nmo_indexed_map_t *map = nmo_indexed_map_create(NULL, 
        sizeof(uint32_t),
        sizeof(uint32_t),
        0,
        nmo_map_hash_uint32,
        NULL
    );

    ASSERT_NOT_NULL(map);

    // Insert entries
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t key = i;
        uint32_t value = i * 2;
        nmo_indexed_map_insert(map, &key, &value);
    }

    ASSERT_EQ(nmo_indexed_map_get_count(map), 10);

    // Remove some entries
    uint32_t key5 = 5;
    ASSERT_EQ(nmo_indexed_map_remove(map, &key5), 1);
    ASSERT_EQ(nmo_indexed_map_get_count(map), 9);
    ASSERT_EQ(nmo_indexed_map_contains(map, &key5), 0);

    // Verify remaining entries are still accessible
    for (uint32_t i = 0; i < 10; i++) {
        if (i == 5) continue;
        uint32_t key = i;
        ASSERT_EQ(nmo_indexed_map_contains(map, &key), 1);
    }

    nmo_indexed_map_destroy(map);
}

TEST(indexed_map, lifecycle_hooks) {
    nmo_indexed_map_t *map = nmo_indexed_map_create(NULL,
        sizeof(uint32_t),
        sizeof(uint32_t),
        0,
        nmo_map_hash_uint32,
        NULL
    );

    ASSERT_NOT_NULL(map);

    uint32_t key_total = 0;
    uint32_t value_total = 0;
    nmo_container_lifecycle_t key_lifecycle = {
        .dispose = indexed_map_key_dispose,
        .user_data = &key_total
    };
    nmo_container_lifecycle_t value_lifecycle = {
        .dispose = indexed_map_value_dispose,
        .user_data = &value_total
    };
    nmo_indexed_map_set_lifecycle(map, &key_lifecycle, &value_lifecycle);

    uint32_t key1 = 1, key2 = 2, key3 = 3;
    uint32_t val1 = 10, val2 = 20, val3 = 30;
    ASSERT_EQ(NMO_OK, nmo_indexed_map_insert(map, &key1, &val1));
    ASSERT_EQ(NMO_OK, nmo_indexed_map_insert(map, &key2, &val2));
    ASSERT_EQ(NMO_OK, nmo_indexed_map_insert(map, &key3, &val3));

    uint32_t updated = 100;
    ASSERT_EQ(NMO_OK, nmo_indexed_map_insert(map, &key1, &updated));
    ASSERT_EQ(value_total, 10u);

    ASSERT_EQ(1, nmo_indexed_map_remove(map, &key2));
    ASSERT_EQ(key_total, 2u);
    ASSERT_EQ(value_total, 30u);

    nmo_indexed_map_clear(map);
    ASSERT_EQ(key_total, 6u);
    ASSERT_EQ(value_total, 160u);

    nmo_indexed_map_set_lifecycle(map, NULL, NULL);
    ASSERT_EQ(NMO_OK, nmo_indexed_map_insert(map, &key1, &val1));
    ASSERT_EQ(1, nmo_indexed_map_remove(map, &key1));
    ASSERT_EQ(key_total, 6u);
    ASSERT_EQ(value_total, 160u);

    nmo_indexed_map_destroy(map);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(indexed_map, basic);
    REGISTER_TEST(indexed_map, multiple);
    REGISTER_TEST(indexed_map, iterate);
    REGISTER_TEST(indexed_map, remove);
    REGISTER_TEST(indexed_map, lifecycle_hooks);
TEST_MAIN_END()
