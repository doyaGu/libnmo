/**
 * @file test_custom_managers.c
 * @brief Unit tests for Phase 6.6: Custom Manager Support
 */

#include "test_framework.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "type/type_system.h"
#include <string.h>

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

static nmo_arena_t *arena = NULL;
static nmo_type_registry_t *registry = NULL;

/* Test manager and type GUIDs */
static nmo_guid_t manager_guid1 = {0x30000001, 0x00000001};
static nmo_guid_t manager_guid2 = {0x30000002, 0x00000002};
static nmo_guid_t type_guid1 = {0x40000001, 0x00000001};
static nmo_guid_t type_guid2 = {0x40000002, 0x00000002};

/* Mock serialization callbacks */
static int serialize_call_count = 0;
static int deserialize_call_count = 0;

static nmo_status_t mock_serialize(
    const void *instance,
    struct nmo_chunk *chunk,
    void *manager_context) {
    (void)instance;
    (void)chunk;
    (void)manager_context;
    serialize_call_count++;
    NMO_RETURN_OK();
}

static nmo_status_t mock_deserialize(
    void *instance,
    struct nmo_chunk *chunk,
    void *manager_context) {
    (void)instance;
    (void)chunk;
    (void)manager_context;
    deserialize_call_count++;
    NMO_RETURN_OK();
}

static void setup(void) {
    arena = nmo_arena_create(NULL, 8192);
    ASSERT_NE(NULL, arena);
    
    registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    serialize_call_count = 0;
    deserialize_call_count = 0;
}

static void teardown(void) {
    if (registry) {
        nmo_type_registry_destroy(registry);
        registry = NULL;
    }
    if (arena) {
        nmo_arena_destroy(arena);
        arena = NULL;
    }
}

/* ============================================================================
 * Manager Registration Tests
 * ============================================================================ */

TEST(custom_managers, register_manager) {
    setup();
    
    nmo_status_t result = nmo_type_registry_register_saver_manager(
        registry, manager_guid1, "TestManager", 
        mock_serialize, mock_deserialize, NULL);
    ASSERT_EQ(NMO_OK, result);
    
    // Verify manager count
    size_t count = nmo_type_registry_get_manager_count(registry);
    ASSERT_EQ(1, count);
    
    teardown();
}

TEST(custom_managers, register_multiple_managers) {
    setup();
    
    nmo_status_t result1 = nmo_type_registry_register_saver_manager(
        registry, manager_guid1, "Manager1", 
        mock_serialize, mock_deserialize, NULL);
    ASSERT_EQ(NMO_OK, result1);
    
    nmo_status_t result2 = nmo_type_registry_register_saver_manager(
        registry, manager_guid2, "Manager2", 
        mock_serialize, mock_deserialize, NULL);
    ASSERT_EQ(NMO_OK, result2);
    
    size_t count = nmo_type_registry_get_manager_count(registry);
    ASSERT_EQ(2, count);
    
    teardown();
}

TEST(custom_managers, register_duplicate_guid_fails) {
    setup();
    
    nmo_status_t result1 = nmo_type_registry_register_saver_manager(
        registry, manager_guid1, "Manager1", 
        mock_serialize, mock_deserialize, NULL);
    ASSERT_EQ(NMO_OK, result1);
    
    // Try to register same GUID again
    nmo_status_t result2 = nmo_type_registry_register_saver_manager(
        registry, manager_guid1, "Manager2", 
        mock_serialize, mock_deserialize, NULL);
    ASSERT_NE(NMO_OK, result2);
    
    teardown();
}

TEST(custom_managers, register_null_callbacks_fails) {
    setup();
    
    // NULL serialize
    nmo_status_t result1 = nmo_type_registry_register_saver_manager(
        registry, manager_guid1, "Manager1", 
        NULL, mock_deserialize, NULL);
    ASSERT_NE(NMO_OK, result1);
    
    // NULL deserialize
    nmo_status_t result2 = nmo_type_registry_register_saver_manager(
        registry, manager_guid1, "Manager1", 
        mock_serialize, NULL, NULL);
    ASSERT_NE(NMO_OK, result2);
    
    teardown();
}

TEST(custom_managers, get_manager_by_guid) {
    setup();
    
    nmo_status_t result = nmo_type_registry_register_saver_manager(
        registry, manager_guid1, "TestManager", 
        mock_serialize, mock_deserialize, NULL);
    ASSERT_EQ(NMO_OK, result);
    
    const nmo_saver_manager_t *manager = 
        nmo_type_registry_get_saver_manager(registry, manager_guid1);
    ASSERT_NE(NULL, manager);
    ASSERT_TRUE(nmo_guid_equals(manager->guid, manager_guid1));
    ASSERT_NE(NULL, manager->serialize);
    ASSERT_NE(NULL, manager->deserialize);
    
    teardown();
}

TEST(custom_managers, get_nonexistent_manager) {
    setup();
    
    nmo_guid_t fake_guid = {0x99999999, 0x99999999};
    const nmo_saver_manager_t *manager = 
        nmo_type_registry_get_saver_manager(registry, fake_guid);
    ASSERT_EQ(NULL, manager);
    
    teardown();
}

TEST(custom_managers, unregister_manager) {
    setup();
    
    nmo_status_t result = nmo_type_registry_register_saver_manager(
        registry, manager_guid1, "TestManager", 
        mock_serialize, mock_deserialize, NULL);
    ASSERT_EQ(NMO_OK, result);
    
    size_t before = nmo_type_registry_get_manager_count(registry);
    ASSERT_EQ(1, before);
    
    result = nmo_type_registry_unregister_saver_manager(registry, manager_guid1);
    ASSERT_EQ(NMO_OK, result);
    
    size_t after = nmo_type_registry_get_manager_count(registry);
    ASSERT_EQ(0, after);
    
    teardown();
}

TEST(custom_managers, unregister_nonexistent_manager_fails) {
    setup();
    
    nmo_guid_t fake_guid = {0x99999999, 0x99999999};
    nmo_status_t result = nmo_type_registry_unregister_saver_manager(registry, fake_guid);
    ASSERT_NE(NMO_OK, result);
    
    teardown();
}

/* ============================================================================
 * Type-Manager Association Tests
 * ============================================================================ */

TEST(custom_managers, associate_type_with_manager) {
    setup();
    
    // Register type
    nmo_type_descriptor_t type_desc = {0};
    type_desc.guid = type_guid1;
    type_desc.name = "TestType";
    type_desc.size = 4;
    type_desc.alignment = 4;
    type_desc.category = NMO_TYPE_CATEGORY_SCALAR;
    type_desc.valid = true;
    type_desc.base_type = NMO_GUID_NULL;
    type_desc.saver_manager = NMO_MANAGER_INDEX_INVALID;
    
    nmo_status_t result = nmo_type_registry_register(registry, &type_desc);
    ASSERT_EQ(NMO_OK, result);
    
    // Register manager
    result = nmo_type_registry_register_saver_manager(
        registry, manager_guid1, "TestManager", 
        mock_serialize, mock_deserialize, NULL);
    ASSERT_EQ(NMO_OK, result);
    
    // Associate type with manager
    result = nmo_type_registry_set_type_manager(registry, type_guid1, manager_guid1);
    ASSERT_EQ(NMO_OK, result);
    
    // Verify association
    const nmo_saver_manager_t *manager = 
        nmo_type_registry_get_type_manager(registry, type_guid1);
    ASSERT_NE(NULL, manager);
    ASSERT_TRUE(nmo_guid_equals(manager->guid, manager_guid1));
    
    teardown();
}

TEST(custom_managers, set_manager_for_nonexistent_type_fails) {
    setup();
    
    // Register manager
    nmo_status_t result = nmo_type_registry_register_saver_manager(
        registry, manager_guid1, "TestManager", 
        mock_serialize, mock_deserialize, NULL);
    ASSERT_EQ(NMO_OK, result);
    
    // Try to associate nonexistent type
    nmo_guid_t fake_type = {0x99999999, 0x99999999};
    result = nmo_type_registry_set_type_manager(registry, fake_type, manager_guid1);
    ASSERT_NE(NMO_OK, result);
    
    teardown();
}

TEST(custom_managers, set_nonexistent_manager_fails) {
    setup();
    
    // Register type
    nmo_type_descriptor_t type_desc = {0};
    type_desc.guid = type_guid1;
    type_desc.name = "TestType";
    type_desc.size = 4;
    type_desc.alignment = 4;
    type_desc.category = NMO_TYPE_CATEGORY_SCALAR;
    type_desc.valid = true;
    type_desc.base_type = NMO_GUID_NULL;
    type_desc.saver_manager = NMO_MANAGER_INDEX_INVALID;
    
    nmo_status_t result = nmo_type_registry_register(registry, &type_desc);
    ASSERT_EQ(NMO_OK, result);
    
    // Try to associate with nonexistent manager
    nmo_guid_t fake_manager = {0x99999999, 0x99999999};
    result = nmo_type_registry_set_type_manager(registry, type_guid1, fake_manager);
    ASSERT_NE(NMO_OK, result);
    
    teardown();
}

TEST(custom_managers, clear_type_manager_association) {
    setup();
    
    // Register type
    nmo_type_descriptor_t type_desc = {0};
    type_desc.guid = type_guid1;
    type_desc.name = "TestType";
    type_desc.size = 4;
    type_desc.alignment = 4;
    type_desc.category = NMO_TYPE_CATEGORY_SCALAR;
    type_desc.valid = true;
    type_desc.base_type = NMO_GUID_NULL;
    type_desc.saver_manager = NMO_MANAGER_INDEX_INVALID;
    
    nmo_status_t result = nmo_type_registry_register(registry, &type_desc);
    ASSERT_EQ(NMO_OK, result);
    
    // Register and associate manager
    result = nmo_type_registry_register_saver_manager(
        registry, manager_guid1, "TestManager", 
        mock_serialize, mock_deserialize, NULL);
    ASSERT_EQ(NMO_OK, result);
    
    result = nmo_type_registry_set_type_manager(registry, type_guid1, manager_guid1);
    ASSERT_EQ(NMO_OK, result);
    
    // Verify association exists
    const nmo_saver_manager_t *manager1 = 
        nmo_type_registry_get_type_manager(registry, type_guid1);
    ASSERT_NE(NULL, manager1);
    
    // Clear association
    result = nmo_type_registry_clear_type_manager(registry, type_guid1);
    ASSERT_EQ(NMO_OK, result);
    
    // Verify association cleared
    const nmo_saver_manager_t *manager2 = 
        nmo_type_registry_get_type_manager(registry, type_guid1);
    ASSERT_EQ(NULL, manager2);
    
    teardown();
}

TEST(custom_managers, get_type_manager_no_association) {
    setup();
    
    // Register type without manager
    nmo_type_descriptor_t type_desc = {0};
    type_desc.guid = type_guid1;
    type_desc.name = "TestType";
    type_desc.size = 4;
    type_desc.alignment = 4;
    type_desc.category = NMO_TYPE_CATEGORY_SCALAR;
    type_desc.valid = true;
    type_desc.base_type = NMO_GUID_NULL;
    type_desc.saver_manager = NMO_MANAGER_INDEX_INVALID;
    
    nmo_status_t result = nmo_type_registry_register(registry, &type_desc);
    ASSERT_EQ(NMO_OK, result);
    
    // Should return NULL
    const nmo_saver_manager_t *manager = 
        nmo_type_registry_get_type_manager(registry, type_guid1);
    ASSERT_EQ(NULL, manager);
    
    teardown();
}

TEST(custom_managers, unregister_manager_clears_type_associations) {
    setup();
    
    // Register type
    nmo_type_descriptor_t type_desc = {0};
    type_desc.guid = type_guid1;
    type_desc.name = "TestType";
    type_desc.size = 4;
    type_desc.alignment = 4;
    type_desc.category = NMO_TYPE_CATEGORY_SCALAR;
    type_desc.valid = true;
    type_desc.base_type = NMO_GUID_NULL;
    type_desc.saver_manager = NMO_MANAGER_INDEX_INVALID;
    
    nmo_status_t result = nmo_type_registry_register(registry, &type_desc);
    ASSERT_EQ(NMO_OK, result);
    
    // Register and associate manager
    result = nmo_type_registry_register_saver_manager(
        registry, manager_guid1, "TestManager", 
        mock_serialize, mock_deserialize, NULL);
    ASSERT_EQ(NMO_OK, result);
    
    result = nmo_type_registry_set_type_manager(registry, type_guid1, manager_guid1);
    ASSERT_EQ(NMO_OK, result);
    
    // Unregister manager
    result = nmo_type_registry_unregister_saver_manager(registry, manager_guid1);
    ASSERT_EQ(NMO_OK, result);
    
    // Type should no longer have manager association
    const nmo_saver_manager_t *manager = 
        nmo_type_registry_get_type_manager(registry, type_guid1);
    ASSERT_EQ(NULL, manager);
    
    teardown();
}

/* ============================================================================
 * NULL Safety Tests
 * ============================================================================ */

TEST(custom_managers, null_registry_safety) {
    // All operations should handle NULL registry gracefully
    nmo_status_t result;
    
    result = nmo_type_registry_register_saver_manager(
        NULL, manager_guid1, "Test", mock_serialize, mock_deserialize, NULL);
    ASSERT_NE(NMO_OK, result);
    
    result = nmo_type_registry_unregister_saver_manager(NULL, manager_guid1);
    ASSERT_NE(NMO_OK, result);
    
    const nmo_saver_manager_t *manager = nmo_type_registry_get_saver_manager(NULL, manager_guid1);
    ASSERT_EQ(NULL, manager);
    
    result = nmo_type_registry_set_type_manager(NULL, type_guid1, manager_guid1);
    ASSERT_NE(NMO_OK, result);
    
    manager = nmo_type_registry_get_type_manager(NULL, type_guid1);
    ASSERT_EQ(NULL, manager);
    
    result = nmo_type_registry_clear_type_manager(NULL, type_guid1);
    ASSERT_NE(NMO_OK, result);
    
    size_t count = nmo_type_registry_get_manager_count(NULL);
    ASSERT_EQ(0, count);
}

/* ============================================================================
 * Integration Tests
 * ============================================================================ */

TEST(custom_managers, multiple_types_one_manager) {
    setup();
    
    // Register manager
    nmo_status_t result = nmo_type_registry_register_saver_manager(
        registry, manager_guid1, "SharedManager", 
        mock_serialize, mock_deserialize, NULL);
    ASSERT_EQ(NMO_OK, result);
    
    // Register two types
    nmo_type_descriptor_t type1 = {0};
    type1.guid = type_guid1;
    type1.name = "Type1";
    type1.size = 4;
    type1.alignment = 4;
    type1.category = NMO_TYPE_CATEGORY_SCALAR;
    type1.valid = true;
    type1.base_type = NMO_GUID_NULL;
    type1.saver_manager = NMO_MANAGER_INDEX_INVALID;
    
    nmo_type_descriptor_t type2 = {0};
    type2.guid = type_guid2;
    type2.name = "Type2";
    type2.size = 8;
    type2.alignment = 4;
    type2.category = NMO_TYPE_CATEGORY_STRUCT;
    type2.valid = true;
    type2.base_type = NMO_GUID_NULL;
    type2.saver_manager = NMO_MANAGER_INDEX_INVALID;
    
    result = nmo_type_registry_register(registry, &type1);
    ASSERT_EQ(NMO_OK, result);
    result = nmo_type_registry_register(registry, &type2);
    ASSERT_EQ(NMO_OK, result);
    
    // Associate both with same manager
    result = nmo_type_registry_set_type_manager(registry, type_guid1, manager_guid1);
    ASSERT_EQ(NMO_OK, result);
    result = nmo_type_registry_set_type_manager(registry, type_guid2, manager_guid1);
    ASSERT_EQ(NMO_OK, result);
    
    // Verify both associations
    const nmo_saver_manager_t *mgr1 = nmo_type_registry_get_type_manager(registry, type_guid1);
    const nmo_saver_manager_t *mgr2 = nmo_type_registry_get_type_manager(registry, type_guid2);
    ASSERT_NE(NULL, mgr1);
    ASSERT_NE(NULL, mgr2);
    ASSERT_TRUE(nmo_guid_equals(mgr1->guid, manager_guid1));
    ASSERT_TRUE(nmo_guid_equals(mgr2->guid, manager_guid1));
    
    teardown();
}

TEST(custom_managers, manager_context_preserved) {
    setup();
    
    int context_value = 42;
    
    nmo_status_t result = nmo_type_registry_register_saver_manager(
        registry, manager_guid1, "ContextManager", 
        mock_serialize, mock_deserialize, &context_value);
    ASSERT_EQ(NMO_OK, result);
    
    const nmo_saver_manager_t *manager = 
        nmo_type_registry_get_saver_manager(registry, manager_guid1);
    ASSERT_NE(NULL, manager);
    ASSERT_NE(NULL, manager->context);
    ASSERT_EQ(42, *(int*)manager->context);
    
    teardown();
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(custom_managers, register_manager);
    REGISTER_TEST(custom_managers, register_multiple_managers);
    REGISTER_TEST(custom_managers, register_duplicate_guid_fails);
    REGISTER_TEST(custom_managers, register_null_callbacks_fails);
    REGISTER_TEST(custom_managers, get_manager_by_guid);
    REGISTER_TEST(custom_managers, get_nonexistent_manager);
    REGISTER_TEST(custom_managers, unregister_manager);
    REGISTER_TEST(custom_managers, unregister_nonexistent_manager_fails);
    
    REGISTER_TEST(custom_managers, associate_type_with_manager);
    REGISTER_TEST(custom_managers, set_manager_for_nonexistent_type_fails);
    REGISTER_TEST(custom_managers, set_nonexistent_manager_fails);
    REGISTER_TEST(custom_managers, clear_type_manager_association);
    REGISTER_TEST(custom_managers, get_type_manager_no_association);
    REGISTER_TEST(custom_managers, unregister_manager_clears_type_associations);
    
    REGISTER_TEST(custom_managers, null_registry_safety);
    
    REGISTER_TEST(custom_managers, multiple_types_one_manager);
    REGISTER_TEST(custom_managers, manager_context_preserved);
TEST_MAIN_END()
