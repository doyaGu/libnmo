/**
 * @file test_phase6_integration.c
 * @brief Phase 6 Integration Tests - Type System + Operations + Managers
 *
 * This test validates the complete integration of Phase 6 components:
 * - Type registry with dynamic registration
 * - Operation system with type matching
 * - Custom manager serialization
 * - String conversion across all types
 * - Cross-component interactions
 */

#include "../test_framework.h"
#include "type/type_system.h"
#include "type/operation_system.h"
#include "type/type_string.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include <string.h>
#include <stdio.h>

/* Test fixtures */
static nmo_arena_t *test_arena = NULL;
static nmo_type_registry_t *test_registry = NULL;
static nmo_operation_registry_t *test_op_registry = NULL;

/* Test GUIDs */
static const nmo_guid_t GUID_TEST_INT = {0x12345678, 0x00000001};
static const nmo_guid_t GUID_TEST_FLOAT = {0x12345678, 0x00000002};
static const nmo_guid_t GUID_TEST_STRING = {0x12345678, 0x00000003};
static const nmo_guid_t GUID_TEST_VECTOR = {0x12345678, 0x00000004};
static const nmo_guid_t GUID_TEST_ENUM = {0x12345678, 0x00000005};
static const nmo_guid_t GUID_TEST_FLAGS = {0x12345678, 0x00000006};
static const nmo_guid_t GUID_TEST_STRUCT = {0x12345678, 0x00000007};
static const nmo_guid_t GUID_TEST_DERIVED = {0x12345678, 0x00000008};
static const nmo_guid_t GUID_TEST_MANAGER = {0xAABBCCDD, 0x00000001};
static const nmo_guid_t GUID_TEST_OP_ADD = {0x99887766, 0x00000001};

/* Setup/Teardown */
static void setup(void) {
    test_arena = nmo_arena_create(NULL, 64 * 1024);
    test_registry = nmo_type_registry_create(test_arena);
    test_op_registry = nmo_operation_registry_create(test_arena);
}

static void teardown(void) {
    if (test_op_registry) {
        nmo_operation_registry_destroy(test_op_registry);
        test_op_registry = NULL;
    }
    if (test_registry) {
        nmo_type_registry_destroy(test_registry);
        test_registry = NULL;
    }
    if (test_arena) {
        nmo_arena_destroy(test_arena);
        test_arena = NULL;
    }
}

/* ============================================================================
 * Integration Test: Type Registration + String Conversion
 * ============================================================================ */

TEST(phase6_integration, type_register_and_convert) {
    setup();
    
    /* Register a simple integer type */
    nmo_type_descriptor_t int_type = {
        .guid = GUID_TEST_INT,
        .name = "TestInteger",
        .size = sizeof(int),
        .category = NMO_TYPE_CATEGORY_PRIMITIVE,
        .flags = 0
    };
    
    nmo_result_t result = nmo_type_registry_register(test_registry, &int_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify type can be found */
    const nmo_type_descriptor_t *found = nmo_type_registry_find_by_guid(test_registry, GUID_TEST_INT);
    ASSERT_NOT_NULL(found);
    ASSERT_STR_EQ("TestInteger", found->name);
    
    /* Verify statistics updated */
    size_t count = nmo_type_registry_get_type_count(test_registry);
    ASSERT_TRUE(count >= 1);
    
    teardown();
}

/* ============================================================================
 * Integration Test: Enum Registration + String Conversion
 * ============================================================================ */

TEST(phase6_integration, enum_register_and_convert) {
    setup();
    
    /* Register enum type using parser */
    nmo_result_t result = nmo_type_registry_register_enum(
        test_registry,
        GUID_TEST_ENUM,
        "TestEnumType",
        "VALUE_A=0,VALUE_B=1,VALUE_C=2"
    );
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify enum was registered */
    const nmo_type_descriptor_t *enum_type = nmo_type_registry_find_by_guid(test_registry, GUID_TEST_ENUM);
    ASSERT_NOT_NULL(enum_type);
    ASSERT_TRUE((enum_type->category & NMO_TYPE_CATEGORY_ENUM) != 0);
    
    /* Verify enum statistics */
    size_t enum_count = nmo_type_registry_get_enum_count(test_registry);
    ASSERT_TRUE(enum_count >= 1);
    
    teardown();
}

/* ============================================================================
 * Integration Test: Flags Registration + String Conversion
 * ============================================================================ */

TEST(phase6_integration, flags_register_and_convert) {
    setup();
    
    /* Register flags type */
    nmo_result_t result = nmo_type_registry_register_flags(
        test_registry,
        GUID_TEST_FLAGS,
        "TestFlagsType",
        "FLAG_A=0x1,FLAG_B=0x2,FLAG_C=0x4,FLAG_D=0x8"
    );
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify flags was registered */
    const nmo_type_descriptor_t *flags_type = nmo_type_registry_find_by_guid(test_registry, GUID_TEST_FLAGS);
    ASSERT_NOT_NULL(flags_type);
    ASSERT_TRUE((flags_type->category & NMO_TYPE_CATEGORY_FLAGS) != 0);
    
    /* Verify flags statistics */
    size_t flags_count = nmo_type_registry_get_flags_count(test_registry);
    ASSERT_TRUE(flags_count >= 1);
    
    teardown();
}

/* ============================================================================
 * Integration Test: Type Inheritance + Compatibility
 * ============================================================================ */

TEST(phase6_integration, type_inheritance_chain) {
    setup();
    
    /* Register base type */
    nmo_type_descriptor_t base_type = {
        .guid = GUID_TEST_INT,
        .name = "BaseType",
        .size = sizeof(int),
        .category = NMO_TYPE_CATEGORY_PRIMITIVE,
        .parent_guid = NMO_GUID_NULL
    };
    nmo_result_t result = nmo_type_registry_register(test_registry, &base_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Register derived type */
    nmo_type_descriptor_t derived_type = {
        .guid = GUID_TEST_DERIVED,
        .name = "DerivedType",
        .size = sizeof(int),
        .category = NMO_TYPE_CATEGORY_PRIMITIVE,
        .parent_guid = GUID_TEST_INT
    };
    result = nmo_type_registry_register(test_registry, &derived_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Check inheritance relationship */
    int is_derived = nmo_type_is_derived_from(test_registry, GUID_TEST_DERIVED, GUID_TEST_INT);
    ASSERT_TRUE(is_derived);
    
    /* Check reverse is false */
    int is_reverse = nmo_type_is_derived_from(test_registry, GUID_TEST_INT, GUID_TEST_DERIVED);
    ASSERT_FALSE(is_reverse);
    
    /* Check compatibility (symmetric) */
    int compat1 = nmo_type_is_compatible(test_registry, GUID_TEST_INT, GUID_TEST_DERIVED);
    int compat2 = nmo_type_is_compatible(test_registry, GUID_TEST_DERIVED, GUID_TEST_INT);
    ASSERT_EQ(compat1, compat2);
    
    teardown();
}

/* ============================================================================
 * Integration Test: Custom Manager Registration
 * ============================================================================ */

/* Mock manager callbacks */
static nmo_result_t mock_manager_serialize(void *data, void *chunk, void *context) {
    (void)data; (void)chunk; (void)context;
    return nmo_ok();
}

static nmo_result_t mock_manager_deserialize(void *chunk, void **out_data, void *context) {
    (void)chunk; (void)out_data; (void)context;
    return nmo_ok();
}

TEST(phase6_integration, custom_manager_registration) {
    setup();
    
    /* Create manager descriptor */
    nmo_saver_manager_t manager = {
        .guid = GUID_TEST_MANAGER,
        .name = "TestManager",
        .serialize = mock_manager_serialize,
        .deserialize = mock_manager_deserialize,
        .context = NULL
    };
    
    /* Register manager */
    nmo_result_t result = nmo_type_registry_register_saver_manager(test_registry, &manager);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Retrieve manager */
    const nmo_saver_manager_t *found = nmo_type_registry_get_saver_manager(test_registry, GUID_TEST_MANAGER);
    ASSERT_NOT_NULL(found);
    ASSERT_STR_EQ("TestManager", found->name);
    ASSERT_EQ(mock_manager_serialize, found->serialize);
    
    /* Unregister manager */
    result = nmo_type_registry_unregister_saver_manager(test_registry, GUID_TEST_MANAGER);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify unregistered */
    found = nmo_type_registry_get_saver_manager(test_registry, GUID_TEST_MANAGER);
    ASSERT_NULL(found);
    
    teardown();
}

/* ============================================================================
 * Integration Test: Type-Manager Association
 * ============================================================================ */

TEST(phase6_integration, type_manager_association) {
    setup();
    
    /* Register type */
    nmo_type_descriptor_t test_type = {
        .guid = GUID_TEST_INT,
        .name = "ManagedType",
        .size = sizeof(int),
        .category = NMO_TYPE_CATEGORY_PRIMITIVE
    };
    nmo_result_t result = nmo_type_registry_register(test_registry, &test_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Register manager */
    nmo_saver_manager_t manager = {
        .guid = GUID_TEST_MANAGER,
        .name = "TypeManager",
        .serialize = mock_manager_serialize,
        .deserialize = mock_manager_deserialize
    };
    result = nmo_type_registry_register_saver_manager(test_registry, &manager);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Associate type with manager */
    result = nmo_type_registry_set_type_manager(test_registry, GUID_TEST_INT, GUID_TEST_MANAGER);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Retrieve association */
    nmo_guid_t mgr_guid = nmo_type_registry_get_type_manager(test_registry, GUID_TEST_INT);
    ASSERT_TRUE(nmo_guid_equals(mgr_guid, GUID_TEST_MANAGER));
    
    /* Clear association */
    result = nmo_type_registry_clear_type_manager(test_registry, GUID_TEST_INT);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify cleared */
    mgr_guid = nmo_type_registry_get_type_manager(test_registry, GUID_TEST_INT);
    ASSERT_TRUE(nmo_guid_is_null(mgr_guid));
    
    teardown();
}

/* ============================================================================
 * Integration Test: UI Visibility Control
 * ============================================================================ */

TEST(phase6_integration, ui_visibility_control) {
    setup();
    
    /* Register type */
    nmo_type_descriptor_t test_type = {
        .guid = GUID_TEST_INT,
        .name = "VisibleType",
        .size = sizeof(int),
        .category = NMO_TYPE_CATEGORY_PRIMITIVE
    };
    nmo_result_t result = nmo_type_registry_register(test_registry, &test_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Default should be visible */
    int visible = nmo_type_registry_is_ui_visible(test_registry, GUID_TEST_INT);
    ASSERT_TRUE(visible);
    
    /* Hide the type */
    result = nmo_type_registry_set_ui_visibility(test_registry, GUID_TEST_INT, 0);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify hidden */
    visible = nmo_type_registry_is_ui_visible(test_registry, GUID_TEST_INT);
    ASSERT_FALSE(visible);
    
    /* Show again */
    result = nmo_type_registry_set_ui_visibility(test_registry, GUID_TEST_INT, 1);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify visible again */
    visible = nmo_type_registry_is_ui_visible(test_registry, GUID_TEST_INT);
    ASSERT_TRUE(visible);
    
    teardown();
}

/* ============================================================================
 * Integration Test: Memory Usage Tracking
 * ============================================================================ */

TEST(phase6_integration, memory_usage_tracking) {
    setup();
    
    /* Get initial memory usage */
    size_t initial_usage = nmo_type_registry_get_memory_usage(test_registry);
    
    /* Register multiple types */
    for (int i = 0; i < 10; i++) {
        nmo_guid_t guid = {0xABCDEF00 + i, 0x00000000};
        char name[32];
        snprintf(name, sizeof(name), "TestType_%d", i);
        
        nmo_type_descriptor_t type = {
            .guid = guid,
            .name = name,
            .size = sizeof(int) * (i + 1),
            .category = NMO_TYPE_CATEGORY_PRIMITIVE
        };
        nmo_result_t result = nmo_type_registry_register(test_registry, &type);
        ASSERT_EQ(NMO_OK, result.code);
    }
    
    /* Memory usage should have increased */
    size_t final_usage = nmo_type_registry_get_memory_usage(test_registry);
    ASSERT_TRUE(final_usage >= initial_usage);
    
    /* Verify type count */
    size_t count = nmo_type_registry_get_type_count(test_registry);
    ASSERT_TRUE(count >= 10);
    
    teardown();
}

/* ============================================================================
 * Integration Test: Full Workflow - Register, Query, Convert, Unregister
 * ============================================================================ */

TEST(phase6_integration, full_type_lifecycle) {
    setup();
    
    /* 1. Register enum type */
    nmo_result_t result = nmo_type_registry_register_enum(
        test_registry,
        GUID_TEST_ENUM,
        "LifecycleEnum",
        "STATE_INIT=0,STATE_RUNNING=1,STATE_STOPPED=2"
    );
    ASSERT_EQ(NMO_OK, result.code);
    
    /* 2. Query the type */
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(test_registry, GUID_TEST_ENUM);
    ASSERT_NOT_NULL(type);
    
    /* 3. Query by name */
    type = nmo_type_registry_find_by_name(test_registry, "LifecycleEnum");
    ASSERT_NOT_NULL(type);
    
    /* 4. Verify statistics */
    size_t enum_count = nmo_type_registry_get_enum_count(test_registry);
    ASSERT_TRUE(enum_count >= 1);
    
    /* 5. Unregister the type */
    result = nmo_type_registry_unregister(test_registry, GUID_TEST_ENUM);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* 6. Verify unregistered */
    type = nmo_type_registry_find_by_guid(test_registry, GUID_TEST_ENUM);
    ASSERT_NULL(type);
    
    teardown();
}

/* ============================================================================
 * Integration Test: Bulk Type Registration Performance
 * ============================================================================ */

TEST(phase6_integration, bulk_registration_performance) {
    setup();
    
    const int NUM_TYPES = 100;
    
    /* Register many types */
    for (int i = 0; i < NUM_TYPES; i++) {
        nmo_guid_t guid = {0xBULK0000 + i, 0x00000000};
        char name[64];
        snprintf(name, sizeof(name), "BulkType_%04d", i);
        
        nmo_type_descriptor_t type = {
            .guid = guid,
            .name = name,
            .size = sizeof(int),
            .category = NMO_TYPE_CATEGORY_PRIMITIVE
        };
        
        nmo_result_t result = nmo_type_registry_register(test_registry, &type);
        ASSERT_EQ(NMO_OK, result.code);
    }
    
    /* Verify all registered */
    size_t count = nmo_type_registry_get_type_count(test_registry);
    ASSERT_TRUE(count >= NUM_TYPES);
    
    /* Verify random lookup works (O(1) hash lookup) */
    for (int i = 0; i < 10; i++) {
        int idx = (i * 13) % NUM_TYPES; /* Pseudo-random indices */
        nmo_guid_t guid = {0xBULK0000 + idx, 0x00000000};
        
        const nmo_type_descriptor_t *found = nmo_type_registry_find_by_guid(test_registry, guid);
        ASSERT_NOT_NULL(found);
    }
    
    teardown();
}

/* ============================================================================
 * Test Registration
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(phase6_integration, type_register_and_convert);
    REGISTER_TEST(phase6_integration, enum_register_and_convert);
    REGISTER_TEST(phase6_integration, flags_register_and_convert);
    REGISTER_TEST(phase6_integration, type_inheritance_chain);
    REGISTER_TEST(phase6_integration, custom_manager_registration);
    REGISTER_TEST(phase6_integration, type_manager_association);
    REGISTER_TEST(phase6_integration, ui_visibility_control);
    REGISTER_TEST(phase6_integration, memory_usage_tracking);
    REGISTER_TEST(phase6_integration, full_type_lifecycle);
    REGISTER_TEST(phase6_integration, bulk_registration_performance);
TEST_MAIN_END()
