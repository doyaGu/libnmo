/**
 * @file test_type_statistics.c
 * @brief Unit tests for Phase 6.5: Type Statistics & Visibility Control
 */

#include "test_framework.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include "type/type_system.h"
#include "type/builtin_operations.h"
#include "app/nmo_plugin.h"
#include <string.h>

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

static nmo_arena_t *arena = NULL;
static nmo_type_registry_t *registry = NULL;

/* Test type GUIDs */
static nmo_guid_t guid_test1 = {0x20000001, 0x00000000};
static nmo_guid_t guid_test2 = {0x20000002, 0x00000000};
static nmo_guid_t guid_test3 = {0x20000003, 0x00000000};
static nmo_guid_t guid_plugin = {0x30000001, 0x00000000};
static nmo_guid_t guid_plugin_type = {0x30000002, 0x00000000};

static void setup(void) {
    arena = nmo_arena_create(NULL, 8192);
    ASSERT_NE(NULL, arena);
    
    registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    // Register some builtin types for testing
    nmo_result_t result = nmo_register_builtin_types(registry);
    ASSERT_EQ(NMO_OK, result.code);
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
 * Type Count Tests
 * ============================================================================ */

TEST(type_statistics, get_type_count) {
    setup();
    
    size_t count = nmo_type_registry_get_type_count(registry);
    ASSERT_GT(count, 0);  // Should have builtin types
    
    // Should match get_stats total_types
    size_t total = 0, builtin = 0, plugin = 0;
    nmo_type_registry_get_stats(registry, &total, &builtin, &plugin);
    ASSERT_EQ(total, count);
    
    teardown();
}

TEST(type_statistics, get_builtin_count) {
    setup();
    
    size_t count = nmo_type_registry_get_builtin_count(registry);
    ASSERT_GT(count, 0);  // Should have builtin types
    
    // Should match get_stats builtin_types
    size_t total = 0, builtin = 0, plugin = 0;
    nmo_type_registry_get_stats(registry, &total, &builtin, &plugin);
    ASSERT_EQ(builtin, count);
    
    teardown();
}

TEST(type_statistics, get_plugin_count) {
    setup();
    
    size_t count = nmo_type_registry_get_plugin_count(registry);
    ASSERT_EQ(0, count);  // No plugins registered yet
    
    // Should match get_stats plugin_types
    size_t total = 0, builtin = 0, plugin = 0;
    nmo_type_registry_get_stats(registry, &total, &builtin, &plugin);
    ASSERT_EQ(plugin, count);
    
    teardown();
}

TEST(type_statistics, get_flags_count) {
    setup();
    
    size_t count = nmo_type_registry_get_flags_count(registry);
    // Builtins may or may not include flags types
    (void)count;
    
    teardown();
}

TEST(type_statistics, get_enum_count) {
    setup();
    
    size_t count = nmo_type_registry_get_enum_count(registry);
    // Builtins may or may not include enum types
    (void)count;
    
    teardown();
}

TEST(type_statistics, get_struct_count) {
    setup();
    
    size_t count = nmo_type_registry_get_struct_count(registry);
    // Builtins may or may not include struct types (Vector, Matrix, etc)
    (void)count;
    
    teardown();
}

TEST(type_statistics, get_memory_usage) {
    setup();
    
    size_t memory = nmo_type_registry_get_memory_usage(registry);
    ASSERT_GT(memory, 0);  // Should have some memory usage
    
    // Rough sanity check: should be at least size of registry struct
    ASSERT_GE(memory, sizeof(nmo_type_registry_t));
    
    // Should be less than arena size (not consuming everything)
    ASSERT_LT(memory, 1024 * 1024);
    
    teardown();
}

TEST(type_statistics, type_count_after_unregister) {
    setup();

    size_t before_total = nmo_type_registry_get_type_count(registry);
    size_t before_builtin = nmo_type_registry_get_builtin_count(registry);

    nmo_type_descriptor_t test_type = {0};
    test_type.guid = guid_test1;
    test_type.name = "TempType";
    test_type.size = 4;
    test_type.alignment = 4;
    test_type.category = NMO_TYPE_CATEGORY_SCALAR;
    test_type.valid = true;
    test_type.base_type = NMO_GUID_NULL;

    nmo_result_t result = nmo_type_registry_register(registry, &test_type);
    ASSERT_EQ(NMO_OK, result.code);

    size_t mid_total = nmo_type_registry_get_type_count(registry);
    size_t mid_builtin = nmo_type_registry_get_builtin_count(registry);
    ASSERT_EQ(before_total + 1, mid_total);
    ASSERT_EQ(before_builtin + 1, mid_builtin);

    result = nmo_type_registry_unregister(registry, guid_test1);
    ASSERT_EQ(NMO_OK, result.code);

    size_t after_total = nmo_type_registry_get_type_count(registry);
    size_t after_builtin = nmo_type_registry_get_builtin_count(registry);
    ASSERT_EQ(before_total, after_total);
    ASSERT_EQ(before_builtin, after_builtin);

    teardown();
}

TEST(type_statistics, plugin_count_after_plugin_type) {
    setup();

    size_t before = nmo_type_registry_get_plugin_count(registry);

    nmo_plugin_t plugin = {0};
    plugin.guid = guid_plugin;
    plugin.name = "StatsPlugin";
    plugin.version = 0x00010000;
    plugin.category = 0x01;

    nmo_type_descriptor_t plugin_type = {0};
    plugin_type.guid = guid_plugin_type;
    plugin_type.name = "PluginType";
    plugin_type.size = 4;
    plugin_type.alignment = 4;
    plugin_type.category = NMO_TYPE_CATEGORY_STRUCT;
    plugin_type.valid = true;
    plugin_type.base_type = NMO_GUID_NULL;
    plugin_type.creator_plugin = &plugin;

    nmo_result_t result = nmo_type_registry_register(registry, &plugin_type);
    ASSERT_EQ(NMO_OK, result.code);

    size_t after = nmo_type_registry_get_plugin_count(registry);
    ASSERT_EQ(before + 1, after);

    teardown();
}

/* ============================================================================
 * Category-based Counting Tests
 * ============================================================================ */

TEST(type_statistics, count_flags_after_registration) {
    setup();
    
    size_t before = nmo_type_registry_get_flags_count(registry);
    
    // Register a Flags type
    nmo_type_descriptor_t flags_type = {0};
    flags_type.guid = guid_test1;
    flags_type.name = "TestFlags";
    flags_type.size = 4;
    flags_type.alignment = 4;
    flags_type.category = NMO_TYPE_CATEGORY_FLAGS;
    flags_type.valid = true;
    flags_type.base_type = NMO_GUID_NULL;
    
    nmo_result_t result = nmo_type_registry_register(registry, &flags_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    size_t after = nmo_type_registry_get_flags_count(registry);
    ASSERT_EQ(before + 1, after);
    
    teardown();
}

TEST(type_statistics, count_enum_after_registration) {
    setup();
    
    size_t before = nmo_type_registry_get_enum_count(registry);
    
    // Register an Enum type
    nmo_type_descriptor_t enum_type = {0};
    enum_type.guid = guid_test2;
    enum_type.name = "TestEnum";
    enum_type.size = 4;
    enum_type.alignment = 4;
    enum_type.category = NMO_TYPE_CATEGORY_ENUM;
    enum_type.valid = true;
    enum_type.base_type = NMO_GUID_NULL;
    
    nmo_result_t result = nmo_type_registry_register(registry, &enum_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    size_t after = nmo_type_registry_get_enum_count(registry);
    ASSERT_EQ(before + 1, after);
    
    teardown();
}

TEST(type_statistics, count_struct_after_registration) {
    setup();
    
    size_t before = nmo_type_registry_get_struct_count(registry);
    
    // Register a Struct type
    nmo_type_descriptor_t struct_type = {0};
    struct_type.guid = guid_test3;
    struct_type.name = "TestStruct";
    struct_type.size = 8;
    struct_type.alignment = 4;
    struct_type.category = NMO_TYPE_CATEGORY_STRUCT;
    struct_type.valid = true;
    struct_type.base_type = NMO_GUID_NULL;
    
    nmo_result_t result = nmo_type_registry_register(registry, &struct_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    size_t after = nmo_type_registry_get_struct_count(registry);
    ASSERT_EQ(before + 1, after);
    
    teardown();
}

/* ============================================================================
 * UI Visibility Tests
 * ============================================================================ */

TEST(type_visibility, is_ui_visible_default) {
    setup();
    
    // Register a test type - should be visible by default
    nmo_type_descriptor_t test_type = {0};
    test_type.guid = guid_test1;
    test_type.name = "VisibleType";
    test_type.size = 4;
    test_type.alignment = 4;
    test_type.category = NMO_TYPE_CATEGORY_SCALAR;
    test_type.valid = true;
    test_type.base_type = NMO_GUID_NULL;
    
    nmo_result_t result = nmo_type_registry_register(registry, &test_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    bool visible = nmo_type_registry_is_ui_visible(registry, guid_test1);
    ASSERT_TRUE(visible);
    
    teardown();
}

TEST(type_visibility, is_ui_visible_by_id) {
    setup();
    
    // Register a test type
    nmo_type_descriptor_t test_type = {0};
    test_type.guid = guid_test1;
    test_type.name = "VisibleType";
    test_type.size = 4;
    test_type.alignment = 4;
    test_type.category = NMO_TYPE_CATEGORY_SCALAR;
    test_type.valid = true;
    test_type.base_type = NMO_GUID_NULL;
    
    nmo_result_t result = nmo_type_registry_register(registry, &test_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    nmo_type_id_t type_id = nmo_type_registry_guid_to_type_id(registry, guid_test1);
    ASSERT_NE(NMO_TYPE_ID_INVALID, type_id);
    
    bool visible = nmo_type_registry_is_ui_visible_by_id(registry, type_id);
    ASSERT_TRUE(visible);
    
    teardown();
}

TEST(type_visibility, set_visibility_hide) {
    setup();
    
    // Create a test type
    nmo_type_descriptor_t test_type = {0};
    test_type.guid = guid_test1;
    test_type.name = "TestType";
    test_type.size = 4;
    test_type.alignment = 4;
    test_type.category = NMO_TYPE_CATEGORY_SCALAR;
    test_type.valid = true;
    test_type.base_type = NMO_GUID_NULL;
    
    nmo_result_t result = nmo_type_registry_register(registry, &test_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    // Initially should be visible
    ASSERT_TRUE(nmo_type_registry_is_ui_visible(registry, guid_test1));
    
    // Hide it
    result = nmo_type_registry_set_ui_visibility(registry, guid_test1, false);
    ASSERT_EQ(NMO_OK, result.code);
    
    // Now should be hidden
    ASSERT_FALSE(nmo_type_registry_is_ui_visible(registry, guid_test1));
    
    teardown();
}

TEST(type_visibility, set_visibility_show) {
    setup();
    
    // Create a test type
    nmo_type_descriptor_t test_type = {0};
    test_type.guid = guid_test2;
    test_type.name = "TestType2";
    test_type.size = 4;
    test_type.alignment = 4;
    test_type.category = NMO_TYPE_CATEGORY_SCALAR;
    test_type.valid = true;
    test_type.base_type = NMO_GUID_NULL;
    
    nmo_result_t result = nmo_type_registry_register(registry, &test_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    // Hide it first
    result = nmo_type_registry_set_ui_visibility(registry, guid_test2, false);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_FALSE(nmo_type_registry_is_ui_visible(registry, guid_test2));
    
    // Show it again
    result = nmo_type_registry_set_ui_visibility(registry, guid_test2, true);
    ASSERT_EQ(NMO_OK, result.code);
    
    // Now should be visible
    ASSERT_TRUE(nmo_type_registry_is_ui_visible(registry, guid_test2));
    
    teardown();
}

TEST(type_visibility, set_visibility_toggle) {
    setup();
    
    // Create a test type
    nmo_type_descriptor_t test_type = {0};
    test_type.guid = guid_test3;
    test_type.name = "TestType3";
    test_type.size = 4;
    test_type.alignment = 4;
    test_type.category = NMO_TYPE_CATEGORY_SCALAR;
    test_type.valid = true;
    test_type.base_type = NMO_GUID_NULL;
    
    nmo_result_t result = nmo_type_registry_register(registry, &test_type);
    ASSERT_EQ(NMO_OK, result.code);
    
    // Toggle visibility multiple times
    for (int i = 0; i < 5; i++) {
        bool should_be_visible = (i % 2 == 0);
        
        result = nmo_type_registry_set_ui_visibility(registry, guid_test3, should_be_visible);
        ASSERT_EQ(NMO_OK, result.code);
        
        bool is_visible = nmo_type_registry_is_ui_visible(registry, guid_test3);
        ASSERT_EQ(should_be_visible, is_visible);
    }
    
    teardown();
}

TEST(type_visibility, hidden_type_not_found) {
    setup();
    
    // Non-existent type should return false
    nmo_guid_t fake_guid = {0xFFFFFFFF, 0xFFFFFFFF};
    bool visible = nmo_type_registry_is_ui_visible(registry, fake_guid);
    ASSERT_FALSE(visible);
    
    teardown();
}

TEST(type_visibility, set_visibility_not_found) {
    setup();
    
    // Non-existent type should return error
    nmo_guid_t fake_guid = {0xFFFFFFFF, 0xFFFFFFFF};
    nmo_result_t result = nmo_type_registry_set_ui_visibility(registry, fake_guid, true);
    ASSERT_NE(NMO_OK, result.code);
    
    teardown();
}

/* ============================================================================
 * NULL Safety Tests
 * ============================================================================ */

TEST(type_statistics, null_registry_counts) {
    ASSERT_EQ(0, nmo_type_registry_get_type_count(NULL));
    ASSERT_EQ(0, nmo_type_registry_get_builtin_count(NULL));
    ASSERT_EQ(0, nmo_type_registry_get_plugin_count(NULL));
    ASSERT_EQ(0, nmo_type_registry_get_flags_count(NULL));
    ASSERT_EQ(0, nmo_type_registry_get_enum_count(NULL));
    ASSERT_EQ(0, nmo_type_registry_get_struct_count(NULL));
    ASSERT_EQ(0, nmo_type_registry_get_memory_usage(NULL));
}

TEST(type_visibility, null_registry_visibility) {
    nmo_guid_t test_guid = {0x12345678, 0x87654321};
    
    ASSERT_FALSE(nmo_type_registry_is_ui_visible(NULL, test_guid));
    ASSERT_FALSE(nmo_type_registry_is_ui_visible_by_id(NULL, 0));
    
    nmo_result_t result = nmo_type_registry_set_ui_visibility(NULL, test_guid, true);
    ASSERT_NE(NMO_OK, result.code);
}

/* ============================================================================
 * Integration Tests
 * ============================================================================ */

TEST(type_statistics, memory_usage_grows_with_types) {
    setup();
    
    size_t before = nmo_type_registry_get_memory_usage(registry);
    
    // Register several types
    for (int i = 0; i < 10; i++) {
        nmo_guid_t guid = {0x10000000 + i, 0x20000000 + i};
        char name[32];
        snprintf(name, sizeof(name), "DynamicType%d", i);
        
        nmo_type_descriptor_t test_type = {0};
        test_type.guid = guid;
        test_type.name = name;
        test_type.size = 4;
        test_type.alignment = 4;
        test_type.category = NMO_TYPE_CATEGORY_SCALAR;
        test_type.valid = true;
        test_type.base_type = NMO_GUID_NULL;
        
        nmo_result_t result = nmo_type_registry_register(registry, &test_type);
        ASSERT_EQ(NMO_OK, result.code);
    }
    
    size_t after = nmo_type_registry_get_memory_usage(registry);
    
    // Memory usage should increase
    ASSERT_GT(after, before);
    
    teardown();
}

TEST(type_statistics, comprehensive_stats) {
    setup();
    
    // Get all statistics
    size_t total = nmo_type_registry_get_type_count(registry);
    size_t builtin = nmo_type_registry_get_builtin_count(registry);
    size_t plugin = nmo_type_registry_get_plugin_count(registry);
    size_t flags = nmo_type_registry_get_flags_count(registry);
    size_t enums = nmo_type_registry_get_enum_count(registry);
    size_t structs = nmo_type_registry_get_struct_count(registry);
    size_t memory = nmo_type_registry_get_memory_usage(registry);
    
    // Sanity checks
    ASSERT_GT(total, 0);
    ASSERT_GT(builtin, 0);
    ASSERT_EQ(0, plugin);  // No plugins yet
    ASSERT_GT(memory, 0);
    
    // Category counts should not exceed total
    ASSERT_LE(flags, total);
    ASSERT_LE(enums, total);
    ASSERT_LE(structs, total);
    
    // Builtin + plugin should equal total (approximately, due to invalid slots)
    ASSERT_LE(builtin + plugin, total + 10);  // Allow some slack for slots
    
    teardown();
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(type_statistics, get_type_count);
    REGISTER_TEST(type_statistics, get_builtin_count);
    REGISTER_TEST(type_statistics, get_plugin_count);
    REGISTER_TEST(type_statistics, get_flags_count);
    REGISTER_TEST(type_statistics, get_enum_count);
    REGISTER_TEST(type_statistics, get_struct_count);
    REGISTER_TEST(type_statistics, get_memory_usage);
    REGISTER_TEST(type_statistics, type_count_after_unregister);
    REGISTER_TEST(type_statistics, plugin_count_after_plugin_type);
    
    REGISTER_TEST(type_statistics, count_flags_after_registration);
    REGISTER_TEST(type_statistics, count_enum_after_registration);
    REGISTER_TEST(type_statistics, count_struct_after_registration);
    
    REGISTER_TEST(type_visibility, is_ui_visible_default);
    REGISTER_TEST(type_visibility, is_ui_visible_by_id);
    REGISTER_TEST(type_visibility, set_visibility_hide);
    REGISTER_TEST(type_visibility, set_visibility_show);
    REGISTER_TEST(type_visibility, set_visibility_toggle);
    REGISTER_TEST(type_visibility, hidden_type_not_found);
    REGISTER_TEST(type_visibility, set_visibility_not_found);
    
    REGISTER_TEST(type_statistics, null_registry_counts);
    REGISTER_TEST(type_visibility, null_registry_visibility);
    
    REGISTER_TEST(type_statistics, memory_usage_grows_with_types);
    REGISTER_TEST(type_statistics, comprehensive_stats);
TEST_MAIN_END()

