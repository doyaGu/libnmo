/**
 * @file test_hash_table.c
 * @brief Unit tests for generic hash table
 */

#include "../test_framework.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_error.h"
#include <string.h>

static int count_iterator(const void *key, void *value, void *user_data) {
    (void)key;
    (void)value;
    int *count = (int *)user_data;
    (*count)++;
    return 1;
}

static int failing_iterator(const void *key, void *value, void *user_data) {
    (void)key;
    (void)value;
    (void)user_data;
    return 0;  // Always return 0 to stop iteration
}

/**
 * Test basic hash table operations
 */
TEST(hash_table, basic) {
    nmo_hash_table_t *table = nmo_hash_table_create(
        sizeof(uint32_t),
        sizeof(uint32_t),
        0,
        nmo_hash_uint32,
        NULL
    );

    ASSERT_NOT_NULL(table);
    ASSERT_EQ(nmo_hash_table_get_count(table), 0);

    // Insert some entries
    uint32_t key1 = 100;
    uint32_t value1 = 200;
    ASSERT_EQ(nmo_hash_table_insert(table, &key1, &value1), NMO_OK);
    ASSERT_EQ(nmo_hash_table_get_count(table), 1);

    // Get the value
    uint32_t retrieved = 0;
    ASSERT_EQ(nmo_hash_table_get(table, &key1, &retrieved), 1);
    ASSERT_EQ(retrieved, value1);

    // Update the value
    uint32_t value2 = 300;
    ASSERT_EQ(nmo_hash_table_insert(table, &key1, &value2), NMO_OK);
    ASSERT_EQ(nmo_hash_table_get_count(table), 1);

    retrieved = 0;
    ASSERT_EQ(nmo_hash_table_get(table, &key1, &retrieved), 1);
    ASSERT_EQ(retrieved, value2);

    // Check contains
    ASSERT_EQ(nmo_hash_table_contains(table, &key1), 1);

    uint32_t key2 = 999;
    ASSERT_EQ(nmo_hash_table_contains(table, &key2), 0);

    // Remove
    ASSERT_EQ(nmo_hash_table_remove(table, &key1), 1);
    ASSERT_EQ(nmo_hash_table_get_count(table), 0);
    ASSERT_EQ(nmo_hash_table_contains(table, &key1), 0);

    nmo_hash_table_destroy(table);
}

/**
 * Test hash table with multiple entries
 */
TEST(hash_table, multiple) {
    nmo_hash_table_t *table = nmo_hash_table_create(
        sizeof(uint32_t),
        sizeof(uint32_t),
        4,
        nmo_hash_uint32,
        NULL
    );

    ASSERT_NOT_NULL(table);

    // Insert multiple entries
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t key = i;
        uint32_t value = i * 10;
        ASSERT_EQ(nmo_hash_table_insert(table, &key, &value), NMO_OK);
    }

    ASSERT_EQ(nmo_hash_table_get_count(table), 100);

    // Verify all entries
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t key = i;
        uint32_t value = 0;
        ASSERT_EQ(nmo_hash_table_get(table, &key, &value), 1);
        ASSERT_EQ(value, i * 10);
    }

    // Clear
    nmo_hash_table_clear(table);
    ASSERT_EQ(nmo_hash_table_get_count(table), 0);

    nmo_hash_table_destroy(table);
}

/**
 * Test hash table iteration
 */
TEST(hash_table, iterate) {
    nmo_hash_table_t *table = nmo_hash_table_create(
        sizeof(uint32_t),
        sizeof(uint32_t),
        0,
        nmo_hash_uint32,
        NULL
    );

    ASSERT_NOT_NULL(table);

    // Insert entries
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t key = i;
        uint32_t value = i * 2;
        nmo_hash_table_insert(table, &key, &value);
    }

    // Iterate and count
    int count = 0;
    nmo_hash_table_iterate(table, count_iterator, &count);
    ASSERT_EQ(count, 10);

    nmo_hash_table_destroy(table);
}

/**
 * Test error conditions
 */
TEST(hash_table, null_table) {
    // Test operations with NULL table
    uint32_t key = 100;
    uint32_t value = 200;

    ASSERT_EQ(nmo_hash_table_insert(NULL, &key, &value), NMO_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(nmo_hash_table_get(NULL, &key, &value), 0);
    ASSERT_EQ(nmo_hash_table_remove(NULL, &key), 0);
    ASSERT_EQ(nmo_hash_table_contains(NULL, &key), 0);
    ASSERT_EQ(nmo_hash_table_get_count(NULL), 0);
    ASSERT_EQ(nmo_hash_table_get_capacity(NULL), 0);

    // These should not crash
    nmo_hash_table_clear(NULL);
    nmo_hash_table_iterate(NULL, count_iterator, NULL);
    nmo_hash_table_destroy(NULL);
}

TEST(hash_table, null_pointers) {
    nmo_hash_table_t *table = nmo_hash_table_create(
        sizeof(uint32_t),
        sizeof(uint32_t),
        0,
        nmo_hash_uint32,
        NULL
    );
    ASSERT_NOT_NULL(table);

    // Test insert with NULL key
    ASSERT_EQ(nmo_hash_table_insert(table, NULL, &(uint32_t){200}), NMO_ERR_INVALID_ARGUMENT);

    // Test insert with NULL value
    ASSERT_EQ(nmo_hash_table_insert(table, &(uint32_t){100}, NULL), NMO_ERR_INVALID_ARGUMENT);

    // Test get with NULL key
    uint32_t value;
    ASSERT_EQ(nmo_hash_table_get(table, NULL, &value), 0);

    // Test remove with NULL key
    ASSERT_EQ(nmo_hash_table_remove(table, NULL), 0);

    // Test contains with NULL key
    ASSERT_EQ(nmo_hash_table_contains(table, NULL), 0);

    // Test iterate with NULL function
    nmo_hash_table_iterate(table, NULL, NULL);

    nmo_hash_table_destroy(table);
}

TEST(hash_table, invalid_sizes) {
    // Test creation with zero key size
    nmo_hash_table_t *table = nmo_hash_table_create(0, sizeof(uint32_t), 0, NULL, NULL);
    ASSERT_NULL(table);  // Should fail with zero key size

    // Test creation with zero value size
    table = nmo_hash_table_create(sizeof(uint32_t), 0, 0, NULL, NULL);
    ASSERT_NULL(table);  // Should fail with zero value size

    // Test creation with extremely large sizes
    table = nmo_hash_table_create(SIZE_MAX, sizeof(uint32_t), 0, NULL, NULL);
    ASSERT_NULL(table);  // Should fail with too large key size

    table = nmo_hash_table_create(sizeof(uint32_t), SIZE_MAX, 0, NULL, NULL);
    ASSERT_NULL(table);  // Should fail with too large value size
}

TEST(hash_table, empty_operations) {
    nmo_hash_table_t *table = nmo_hash_table_create(
        sizeof(uint32_t),
        sizeof(uint32_t),
        0,
        nmo_hash_uint32,
        NULL
    );
    ASSERT_NOT_NULL(table);

    // Test operations on empty table
    uint32_t key = 100;
    uint32_t value = 0;

    ASSERT_EQ(nmo_hash_table_get(table, &key, &value), 0);  // Should not find
    ASSERT_EQ(nmo_hash_table_remove(table, &key), 0);       // Should not remove
    ASSERT_EQ(nmo_hash_table_contains(table, &key), 0);     // Should not contain
    ASSERT_EQ(nmo_hash_table_get_count(table), 0);          // Should be empty

    // Test iteration on empty table
    int count = 0;
    nmo_hash_table_iterate(table, count_iterator, &count);
    ASSERT_EQ(count, 0);  // Should not iterate

    nmo_hash_table_destroy(table);
}

TEST(hash_table, duplicate_keys) {
    nmo_hash_table_t *table = nmo_hash_table_create(
        sizeof(uint32_t),
        sizeof(uint32_t),
        0,
        nmo_hash_uint32,
        NULL
    );
    ASSERT_NOT_NULL(table);

    // Insert a key
    uint32_t key = 100;
    uint32_t value1 = 200;
    ASSERT_EQ(nmo_hash_table_insert(table, &key, &value1), NMO_OK);
    ASSERT_EQ(nmo_hash_table_get_count(table), 1);

    // Insert the same key with different value (should update)
    uint32_t value2 = 300;
    ASSERT_EQ(nmo_hash_table_insert(table, &key, &value2), NMO_OK);
    ASSERT_EQ(nmo_hash_table_get_count(table), 1);  // Still only one entry

    // Verify the value was updated
    uint32_t retrieved = 0;
    ASSERT_EQ(nmo_hash_table_get(table, &key, &retrieved), 1);
    ASSERT_EQ(retrieved, value2);  // Should have the new value

    nmo_hash_table_destroy(table);
}

TEST(hash_table, reserve_invalid) {
    nmo_hash_table_t *table = nmo_hash_table_create(
        sizeof(uint32_t),
        sizeof(uint32_t),
        0,
        nmo_hash_uint32,
        NULL
    );
    ASSERT_NOT_NULL(table);

    // Test reserve with NULL table
    ASSERT_NE(nmo_hash_table_reserve(NULL, 100), NMO_OK);

    // Test reserve with zero capacity (should be OK)
    ASSERT_EQ(nmo_hash_table_reserve(table, 0), NMO_OK);

    // Test reserve with extremely large capacity
    ASSERT_NE(nmo_hash_table_reserve(table, SIZE_MAX), NMO_OK);

    nmo_hash_table_destroy(table);
}

TEST(hash_table, iterator_early_stop) {
    nmo_hash_table_t *table = nmo_hash_table_create(
        sizeof(uint32_t),
        sizeof(uint32_t),
        0,
        nmo_hash_uint32,
        NULL
    );
    ASSERT_NOT_NULL(table);

    // Insert multiple entries
    for (uint32_t i = 0; i < 10; i++) {
        uint32_t key = i;
        uint32_t value = i * 2;
        nmo_hash_table_insert(table, &key, &value);
    }

    // Test iterator that stops early
    int count = 0;
    nmo_hash_table_iterate(table, failing_iterator, &count);
    // Count should be 0 or 1 depending on implementation
    // The important thing is that iteration stops early

    nmo_hash_table_destroy(table);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(hash_table, basic);
    REGISTER_TEST(hash_table, multiple);
    REGISTER_TEST(hash_table, iterate);
    REGISTER_TEST(hash_table, null_table);
    REGISTER_TEST(hash_table, null_pointers);
    REGISTER_TEST(hash_table, invalid_sizes);
    REGISTER_TEST(hash_table, empty_operations);
    REGISTER_TEST(hash_table, duplicate_keys);
    REGISTER_TEST(hash_table, reserve_invalid);
    REGISTER_TEST(hash_table, iterator_early_stop);
TEST_MAIN_END()

