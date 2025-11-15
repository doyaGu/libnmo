/**
 * @file test_manager_registry.c
 * @brief Test suite for manager registry (Phase 11)
 */

#include "test_framework.h"
#include "format/nmo_manager_registry.h"
#include "format/nmo_manager.h"
#include "core/nmo_guid.h"
#include "nmo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Test creating and destroying registry
 */
TEST(manager_registry, create_destroy) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    ASSERT_NOT_NULL(registry);

    uint32_t count = nmo_manager_registry_get_count(registry);
    ASSERT_EQ(0, count);

    nmo_manager_registry_destroy(registry);
}

/**
 * Test registering a single manager
 */
TEST(manager_registry, register_single_manager) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    ASSERT_NOT_NULL(registry);

    nmo_guid_t guid = nmo_guid_create(0x12345678, 0x9ABCDEF0);
    nmo_manager_t* manager = nmo_manager_create(guid, "TestManager", NMO_PLUGIN_MANAGER_DLL);
    ASSERT_NOT_NULL(manager);

    nmo_result_t result = nmo_manager_registry_register(registry, 1, manager);
    ASSERT_EQ(NMO_OK, result.code);

    uint32_t count = nmo_manager_registry_get_count(registry);
    ASSERT_EQ(1, count);

    int contains = nmo_manager_registry_contains(registry, 1);
    ASSERT_TRUE(contains);

    nmo_manager_registry_destroy(registry);
}

/**
 * Test registering multiple managers
 */
TEST(manager_registry, register_multiple_managers) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    ASSERT_NOT_NULL(registry);

    const size_t manager_count = 10;
    for (size_t i = 0; i < manager_count; i++) {
        nmo_guid_t guid = nmo_guid_create((uint32_t)i, (uint32_t)(i * 2));
        nmo_manager_t* manager = nmo_manager_create(guid, "TestManager", NMO_PLUGIN_MANAGER_DLL);
        ASSERT_NOT_NULL(manager);

        nmo_result_t result = nmo_manager_registry_register(registry, (uint32_t)i, manager);
        ASSERT_EQ(NMO_OK, result.code);
    }

    uint32_t count = nmo_manager_registry_get_count(registry);
    ASSERT_EQ(manager_count, count);

    for (size_t i = 0; i < manager_count; i++) {
        int contains = nmo_manager_registry_contains(registry, (uint32_t)i);
        ASSERT_TRUE(contains);
    }

    nmo_manager_registry_destroy(registry);
}

/**
 * Test manager lookup
 */
TEST(manager_registry, manager_lookup) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    ASSERT_NOT_NULL(registry);

    nmo_guid_t guid1 = nmo_guid_create(0x11111111, 0x22222222);
    nmo_manager_t* manager1 = nmo_manager_create(guid1, "Manager1", NMO_PLUGIN_MANAGER_DLL);
    ASSERT_NOT_NULL(manager1);

    nmo_guid_t guid2 = nmo_guid_create(0x33333333, 0x44444444);
    nmo_manager_t* manager2 = nmo_manager_create(guid2, "Manager2", NMO_PLUGIN_BEHAVIOR_DLL);
    ASSERT_NOT_NULL(manager2);

    nmo_manager_registry_register(registry, 100, manager1);
    nmo_manager_registry_register(registry, 200, manager2);

    nmo_manager_t* found1 = (nmo_manager_t*)nmo_manager_registry_get(registry, 100);
    ASSERT_EQ(manager1, found1);

    nmo_manager_t* found2 = (nmo_manager_t*)nmo_manager_registry_get(registry, 200);
    ASSERT_EQ(manager2, found2);

    nmo_manager_t* not_found = (nmo_manager_t*)nmo_manager_registry_get(registry, 999);
    ASSERT_NULL(not_found);

    nmo_manager_registry_destroy(registry);
}

/**
 * Test lookup by GUID
 */
TEST(manager_registry, find_by_guid) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    ASSERT_NOT_NULL(registry);

    nmo_guid_t guid = nmo_guid_create(0xABCDEF12, 0x3456789A);
    nmo_manager_t* manager = nmo_manager_create(guid, "GuidManager", NMO_PLUGIN_MANAGER_DLL);
    ASSERT_NOT_NULL(manager);

    nmo_result_t result = nmo_manager_registry_register(registry, 77, manager);
    ASSERT_EQ(NMO_OK, result.code);

    nmo_manager_t* found = (nmo_manager_t*)nmo_manager_registry_find_by_guid(registry, guid);
    ASSERT_EQ(manager, found);

    nmo_guid_t missing_guid = nmo_guid_create(0x0, 0x1);
    nmo_manager_t* missing = (nmo_manager_t*)nmo_manager_registry_find_by_guid(registry, missing_guid);
    ASSERT_NULL(missing);

    nmo_manager_registry_destroy(registry);
}

/**
 * Test unregistering managers
 */
TEST(manager_registry, unregister_manager) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    ASSERT_NOT_NULL(registry);

    nmo_guid_t guid = nmo_guid_create(0x55555555, 0x66666666);
    nmo_manager_t* manager = nmo_manager_create(guid, "TestManager", NMO_PLUGIN_MANAGER_DLL);
    ASSERT_NOT_NULL(manager);

    nmo_manager_registry_register(registry, 42, manager);

    uint32_t count = nmo_manager_registry_get_count(registry);
    ASSERT_EQ(1, count);

    nmo_result_t result = nmo_manager_registry_unregister(registry, 42);
    ASSERT_EQ(NMO_OK, result.code);

    count = nmo_manager_registry_get_count(registry);
    ASSERT_EQ(0, count);

    int contains = nmo_manager_registry_contains(registry, 42);
    ASSERT_FALSE(contains);

    nmo_manager_registry_destroy(registry);
}

/**
 * Test iteration over managers
 */
TEST(manager_registry, manager_iteration) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    ASSERT_NOT_NULL(registry);

    uint32_t expected_ids[] = {10, 20, 30, 40, 50};
    const size_t manager_count = sizeof(expected_ids) / sizeof(expected_ids[0]);

    for (size_t i = 0; i < manager_count; i++) {
        nmo_guid_t guid = nmo_guid_create(expected_ids[i], expected_ids[i] * 2);
        nmo_manager_t* manager = nmo_manager_create(guid, "TestManager", NMO_PLUGIN_MANAGER_DLL);
        ASSERT_NOT_NULL(manager);

        nmo_manager_registry_register(registry, expected_ids[i], manager);
    }

    uint32_t count = nmo_manager_registry_get_count(registry);
    ASSERT_EQ(manager_count, count);

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
        ASSERT_TRUE(found[i]);
    }

    nmo_manager_registry_destroy(registry);
}

/**
 * Test clearing all managers
 */
TEST(manager_registry, clear_all_managers) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    ASSERT_NOT_NULL(registry);

    /* Add multiple managers */
    for (uint32_t i = 0; i < 5; i++) {
        nmo_guid_t guid = nmo_guid_create(i, i * 2);
        nmo_manager_t* manager = nmo_manager_create(guid, "TestManager", NMO_PLUGIN_MANAGER_DLL);
        ASSERT_NOT_NULL(manager);

        nmo_manager_registry_register(registry, i, manager);
    }

    uint32_t count = nmo_manager_registry_get_count(registry);
    ASSERT_EQ(5, count);

    nmo_result_t result = nmo_manager_registry_clear(registry);
    ASSERT_EQ(NMO_OK, result.code);

    count = nmo_manager_registry_get_count(registry);
    ASSERT_EQ(0, count);

    nmo_manager_registry_destroy(registry);
}

/**
 * Test hash table resizing
 */
TEST(manager_registry, registry_resize) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    ASSERT_NOT_NULL(registry);

    /* Add many managers to trigger resize */
    const size_t manager_count = 50;
    for (size_t i = 0; i < manager_count; i++) {
        nmo_guid_t guid = nmo_guid_create((uint32_t)i, (uint32_t)(i * 2));
        nmo_manager_t* manager = nmo_manager_create(guid, "TestManager", NMO_PLUGIN_MANAGER_DLL);
        ASSERT_NOT_NULL(manager);

        nmo_result_t result = nmo_manager_registry_register(registry, (uint32_t)i, manager);
        ASSERT_EQ(NMO_OK, result.code);
    }

    uint32_t count = nmo_manager_registry_get_count(registry);
    ASSERT_EQ(manager_count, count);

    /* Verify all managers are still accessible */
    for (size_t i = 0; i < manager_count; i++) {
        int contains = nmo_manager_registry_contains(registry, (uint32_t)i);
        ASSERT_TRUE(contains);
    }

    nmo_manager_registry_destroy(registry);
}

/**
 * Test error handling
 */
TEST(manager_registry, error_handling) {
    nmo_manager_registry_t* registry = nmo_manager_registry_create();
    ASSERT_NOT_NULL(registry);

    /* Test registering NULL manager */
    nmo_result_t result = nmo_manager_registry_register(registry, 1, NULL);
    ASSERT_NE(NMO_OK, result.code);

    /* Test registering duplicate ID */
    nmo_guid_t guid = nmo_guid_create(0x77777777, 0x88888888);
    nmo_manager_t* manager1 = nmo_manager_create(guid, "Manager1", NMO_PLUGIN_MANAGER_DLL);
    ASSERT_NOT_NULL(manager1);

    nmo_manager_t* manager2 = nmo_manager_create(guid, "Manager2", NMO_PLUGIN_BEHAVIOR_DLL);
    ASSERT_NOT_NULL(manager2);

    nmo_manager_registry_register(registry, 123, manager1);

    result = nmo_manager_registry_register(registry, 123, manager2);
    ASSERT_NE(NMO_OK, result.code);

    /* Cleanup manager2 since it wasn't registered */
    nmo_manager_destroy(manager2);

    /* Test registering duplicate GUID */
    nmo_guid_t dup_guid = nmo_guid_create(0x99999999, 0xAAAAAAAA);
    nmo_manager_t* guid_manager1 = nmo_manager_create(dup_guid, "Guid1", NMO_PLUGIN_MANAGER_DLL);
    ASSERT_NOT_NULL(guid_manager1);
    nmo_manager_t* guid_manager2 = nmo_manager_create(dup_guid, "Guid2", NMO_PLUGIN_MANAGER_DLL);
    ASSERT_NOT_NULL(guid_manager2);

    result = nmo_manager_registry_register(registry, 200, guid_manager1);
    ASSERT_EQ(NMO_OK, result.code);

    result = nmo_manager_registry_register(registry, 201, guid_manager2);
    ASSERT_NE(NMO_OK, result.code);
    nmo_manager_destroy(guid_manager2);

    /* Test unregistering non-existent manager */
    result = nmo_manager_registry_unregister(registry, 999);
    ASSERT_NE(NMO_OK, result.code);

    /* Test NULL registry operations */
    uint32_t count = nmo_manager_registry_get_count(NULL);
    ASSERT_EQ(0, count);

    void* manager = nmo_manager_registry_get(NULL, 1);
    ASSERT_NULL(manager);

    nmo_manager_registry_destroy(registry);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(manager_registry, create_destroy);
    REGISTER_TEST(manager_registry, register_single_manager);
    REGISTER_TEST(manager_registry, register_multiple_managers);
    REGISTER_TEST(manager_registry, manager_lookup);
    REGISTER_TEST(manager_registry, find_by_guid);
    REGISTER_TEST(manager_registry, unregister_manager);
    REGISTER_TEST(manager_registry, manager_iteration);
    REGISTER_TEST(manager_registry, clear_all_managers);
    REGISTER_TEST(manager_registry, registry_resize);
    REGISTER_TEST(manager_registry, error_handling);
TEST_MAIN_END()
