/**
 * @file test_manager_registry.c
 * @brief Test suite for manager registry (Phase 11)
 */

#include "format/nmo_manager_registry.h"
#include "format/nmo_manager.h"
#include "core/nmo_guid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s - %s\n", __func__, message); \
            tests_failed++; \
            return; \
        } \
    } while(0)

#define TEST_PASS() \
    do { \
        printf("PASS: %s\n", __func__); \
        tests_passed++; \
    } while(0)

/**
 * Test creating and destroying registry
 */
static void test_registry_create_destroy(void) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    TEST_ASSERT(registry != NULL, "Failed to create registry");

    uint32_t count = nmo_manager_registry_get_count(registry);
    TEST_ASSERT(count == 0, "New registry should be empty");

    nmo_manager_registry_destroy(registry);

    TEST_PASS();
}

/**
 * Test registering a single manager
 */
static void test_register_single_manager(void) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    TEST_ASSERT(registry != NULL, "Failed to create registry");

    nmo_guid guid = {0x12345678, 0x9ABCDEF0};
    nmo_manager* manager = nmo_manager_create(guid, "TestManager", NMO_PLUGIN_MANAGER_DLL);
    TEST_ASSERT(manager != NULL, "Failed to create manager");

    nmo_result_t result = nmo_manager_registry_register(registry, 1, manager);
    TEST_ASSERT(result.code == NMO_OK, "Failed to register manager");

    uint32_t count = nmo_manager_registry_get_count(registry);
    TEST_ASSERT(count == 1, "Registry should have 1 manager");

    int contains = nmo_manager_registry_contains(registry, 1);
    TEST_ASSERT(contains == 1, "Registry should contain manager ID 1");

    nmo_manager_registry_destroy(registry);

    TEST_PASS();
}

/**
 * Test registering multiple managers
 */
static void test_register_multiple_managers(void) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    TEST_ASSERT(registry != NULL, "Failed to create registry");

    const size_t manager_count = 10;
    for (size_t i = 0; i < manager_count; i++) {
        nmo_guid guid = {(uint32_t)i, (uint32_t)(i * 2)};
        nmo_manager* manager = nmo_manager_create(guid, "TestManager", NMO_PLUGIN_MANAGER_DLL);
        TEST_ASSERT(manager != NULL, "Failed to create manager");

        nmo_result_t result = nmo_manager_registry_register(registry, (uint32_t)i, manager);
        TEST_ASSERT(result.code == NMO_OK, "Failed to register manager");
    }

    uint32_t count = nmo_manager_registry_get_count(registry);
    TEST_ASSERT(count == manager_count, "Registry should have 10 managers");

    for (size_t i = 0; i < manager_count; i++) {
        int contains = nmo_manager_registry_contains(registry, (uint32_t)i);
        TEST_ASSERT(contains == 1, "Registry should contain all managers");
    }

    nmo_manager_registry_destroy(registry);

    TEST_PASS();
}

/**
 * Test manager lookup
 */
static void test_manager_lookup(void) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    TEST_ASSERT(registry != NULL, "Failed to create registry");

    nmo_guid guid1 = {0x11111111, 0x22222222};
    nmo_manager* manager1 = nmo_manager_create(guid1, "Manager1", NMO_PLUGIN_MANAGER_DLL);
    TEST_ASSERT(manager1 != NULL, "Failed to create manager1");

    nmo_guid guid2 = {0x33333333, 0x44444444};
    nmo_manager* manager2 = nmo_manager_create(guid2, "Manager2", NMO_PLUGIN_BEHAVIOR_DLL);
    TEST_ASSERT(manager2 != NULL, "Failed to create manager2");

    nmo_manager_registry_register(registry, 100, manager1);
    nmo_manager_registry_register(registry, 200, manager2);

    nmo_manager* found1 = (nmo_manager*)nmo_manager_registry_get(registry, 100);
    TEST_ASSERT(found1 == manager1, "Should find manager1");

    nmo_manager* found2 = (nmo_manager*)nmo_manager_registry_get(registry, 200);
    TEST_ASSERT(found2 == manager2, "Should find manager2");

    nmo_manager* not_found = (nmo_manager*)nmo_manager_registry_get(registry, 999);
    TEST_ASSERT(not_found == NULL, "Should not find non-existent manager");

    nmo_manager_registry_destroy(registry);

    TEST_PASS();
}

/**
 * Test unregistering managers
 */
static void test_unregister_manager(void) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    TEST_ASSERT(registry != NULL, "Failed to create registry");

    nmo_guid guid = {0x55555555, 0x66666666};
    nmo_manager* manager = nmo_manager_create(guid, "TestManager", NMO_PLUGIN_MANAGER_DLL);
    TEST_ASSERT(manager != NULL, "Failed to create manager");

    nmo_manager_registry_register(registry, 42, manager);

    uint32_t count = nmo_manager_registry_get_count(registry);
    TEST_ASSERT(count == 1, "Registry should have 1 manager");

    nmo_result_t result = nmo_manager_registry_unregister(registry, 42);
    TEST_ASSERT(result.code == NMO_OK, "Failed to unregister manager");

    count = nmo_manager_registry_get_count(registry);
    TEST_ASSERT(count == 0, "Registry should be empty after unregister");

    int contains = nmo_manager_registry_contains(registry, 42);
    TEST_ASSERT(contains == 0, "Registry should not contain unregistered manager");

    nmo_manager_registry_destroy(registry);

    TEST_PASS();
}

/**
 * Test iteration over managers
 */
static void test_manager_iteration(void) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    TEST_ASSERT(registry != NULL, "Failed to create registry");

    uint32_t expected_ids[] = {10, 20, 30, 40, 50};
    const size_t manager_count = sizeof(expected_ids) / sizeof(expected_ids[0]);

    for (size_t i = 0; i < manager_count; i++) {
        nmo_guid guid = {expected_ids[i], expected_ids[i] * 2};
        nmo_manager* manager = nmo_manager_create(guid, "TestManager", NMO_PLUGIN_MANAGER_DLL);
        TEST_ASSERT(manager != NULL, "Failed to create manager");

        nmo_manager_registry_register(registry, expected_ids[i], manager);
    }

    uint32_t count = nmo_manager_registry_get_count(registry);
    TEST_ASSERT(count == manager_count, "Registry should have correct count");

    /* Verify all IDs are accessible */
    int found[5] = {0};
    for (uint32_t i = 0; i < count; i++) {
        uint32_t manager_id = nmo_manager_registry_get_id_at(registry, i);

        for (size_t j = 0; j < manager_count; j++) {
            if (manager_id == expected_ids[j]) {
                found[j] = 1;
                break;
            }
        }
    }

    for (size_t i = 0; i < manager_count; i++) {
        TEST_ASSERT(found[i] == 1, "All managers should be found during iteration");
    }

    nmo_manager_registry_destroy(registry);

    TEST_PASS();
}

/**
 * Test clearing all managers
 */
static void test_clear_all_managers(void) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    TEST_ASSERT(registry != NULL, "Failed to create registry");

    /* Add multiple managers */
    for (uint32_t i = 0; i < 5; i++) {
        nmo_guid guid = {i, i * 2};
        nmo_manager* manager = nmo_manager_create(guid, "TestManager", NMO_PLUGIN_MANAGER_DLL);
        TEST_ASSERT(manager != NULL, "Failed to create manager");

        nmo_manager_registry_register(registry, i, manager);
    }

    uint32_t count = nmo_manager_registry_get_count(registry);
    TEST_ASSERT(count == 5, "Registry should have 5 managers");

    nmo_result_t result = nmo_manager_registry_clear(registry);
    TEST_ASSERT(result.code == NMO_OK, "Failed to clear registry");

    count = nmo_manager_registry_get_count(registry);
    TEST_ASSERT(count == 0, "Registry should be empty after clear");

    nmo_manager_registry_destroy(registry);

    TEST_PASS();
}

/**
 * Test hash table resizing
 */
static void test_registry_resize(void) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    TEST_ASSERT(registry != NULL, "Failed to create registry");

    /* Add many managers to trigger resize */
    const size_t manager_count = 50;
    for (size_t i = 0; i < manager_count; i++) {
        nmo_guid guid = {(uint32_t)i, (uint32_t)(i * 2)};
        nmo_manager* manager = nmo_manager_create(guid, "TestManager", NMO_PLUGIN_MANAGER_DLL);
        TEST_ASSERT(manager != NULL, "Failed to create manager");

        nmo_result_t result = nmo_manager_registry_register(registry, (uint32_t)i, manager);
        TEST_ASSERT(result.code == NMO_OK, "Failed to register manager");
    }

    uint32_t count = nmo_manager_registry_get_count(registry);
    TEST_ASSERT(count == manager_count, "Registry should have all managers after resize");

    /* Verify all managers are still accessible */
    for (size_t i = 0; i < manager_count; i++) {
        int contains = nmo_manager_registry_contains(registry, (uint32_t)i);
        TEST_ASSERT(contains == 1, "All managers should be accessible after resize");
    }

    nmo_manager_registry_destroy(registry);

    TEST_PASS();
}

/**
 * Test error handling
 */
static void test_error_handling(void) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    TEST_ASSERT(registry != NULL, "Failed to create registry");

    /* Test registering NULL manager */
    nmo_result_t result = nmo_manager_registry_register(registry, 1, NULL);
    TEST_ASSERT(result.code != NMO_OK, "Should fail to register NULL manager");

    /* Test registering duplicate ID */
    nmo_guid guid = {0x77777777, 0x88888888};
    nmo_manager* manager1 = nmo_manager_create(guid, "Manager1", NMO_PLUGIN_MANAGER_DLL);
    TEST_ASSERT(manager1 != NULL, "Failed to create manager1");

    nmo_manager_registry_register(registry, 123, manager1);

    nmo_manager* manager2 = nmo_manager_create(guid, "Manager2", NMO_PLUGIN_MANAGER_DLL);
    TEST_ASSERT(manager2 != NULL, "Failed to create manager2");

    result = nmo_manager_registry_register(registry, 123, manager2);
    TEST_ASSERT(result.code != NMO_OK, "Should fail to register duplicate ID");

    /* Cleanup manager2 since it wasn't registered */
    nmo_manager_destroy(manager2);

    /* Test unregistering non-existent manager */
    result = nmo_manager_registry_unregister(registry, 999);
    TEST_ASSERT(result.code != NMO_OK, "Should fail to unregister non-existent manager");

    /* Test NULL registry operations */
    uint32_t count = nmo_manager_registry_get_count(NULL);
    TEST_ASSERT(count == 0, "get_count on NULL should return 0");

    void* manager = nmo_manager_registry_get(NULL, 1);
    TEST_ASSERT(manager == NULL, "get on NULL should return NULL");

    nmo_manager_registry_destroy(registry);

    TEST_PASS();
}

int main(void) {
    printf("Running manager registry tests...\n\n");

    test_registry_create_destroy();
    test_register_single_manager();
    test_register_multiple_managers();
    test_manager_lookup();
    test_unregister_manager();
    test_manager_iteration();
    test_clear_all_managers();
    test_registry_resize();
    test_error_handling();

    printf("\nAll manager registry tests completed!\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return (tests_failed == 0) ? 0 : 1;
}
