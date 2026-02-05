/**
 * @file test_plugin_tracking.c
 * @brief Integration tests for plugin tracking functionality
 * 
 * Tests plugin registration, unregistration, and type-to-plugin mapping.
 * Validates plugin_map and type_to_plugin hash tables work correctly.
 * 
 * Phase 5.6 - Task T5.6.6
 */

#include "type/nmo_type_system.h"
#include "app/nmo_plugin.h"
#include "test_framework.h"
#include <string.h>

/* Test plugin GUIDs */
static const nmo_guid_t PLUGIN_GUID_1 = {0x12345678, 0x9ABCDEF0};
static const nmo_guid_t PLUGIN_GUID_2 = {0x11111111, 0x22222222};
static const nmo_guid_t PLUGIN_GUID_3 = {0xAAAAAAAA, 0xBBBBBBBB};

/* Test type GUIDs */
static const nmo_guid_t TYPE_GUID_CUSTOM_1 = {0x33333333, 0x44444444};
static const nmo_guid_t TYPE_GUID_CUSTOM_2 = {0x55555555, 0x66666666};
static const nmo_guid_t TYPE_GUID_CUSTOM_3 = {0x77777777, 0x88888888};

/* ============================================================================
 * Test: Plugin Registration and Retrieval
 * ============================================================================ */

TEST(plugin_tracking, register_and_get_plugin) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Create test plugin */
    nmo_plugin_t plugin1 = {0};
    plugin1.guid = PLUGIN_GUID_1;
    plugin1.name = "Test Plugin 1";
    plugin1.version = 0x00010000;
    plugin1.category = 0x01;
    
    /* Register plugin */
    nmo_status_t result = nmo_type_registry_register_plugin(registry, &plugin1);
    ASSERT_EQ(NMO_OK, result);
    
    /* Retrieve plugin */
    const nmo_plugin_t *retrieved = nmo_type_registry_get_plugin(registry, PLUGIN_GUID_1);
    ASSERT_NE(NULL, retrieved);
    ASSERT_TRUE(nmo_guid_equals(PLUGIN_GUID_1, retrieved->guid));
    ASSERT_STR_EQ("Test Plugin 1", retrieved->name);
    ASSERT_EQ(0x00010000, retrieved->version);
    
    /* Query non-existent plugin */
    const nmo_plugin_t *not_found = nmo_type_registry_get_plugin(registry, PLUGIN_GUID_2);
    ASSERT_EQ(NULL, not_found);
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Multiple Plugin Registration
 * ============================================================================ */

TEST(plugin_tracking, register_multiple_plugins) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register three plugins */
    nmo_plugin_t plugins[3] = {
        {.guid = PLUGIN_GUID_1, .name = "Plugin 1", .version = 0x00010000, .category = 0x01},
        {.guid = PLUGIN_GUID_2, .name = "Plugin 2", .version = 0x00020000, .category = 0x02},
        {.guid = PLUGIN_GUID_3, .name = "Plugin 3", .version = 0x00030000, .category = 0x03}
    };
    
    for (int i = 0; i < 3; i++) {
        nmo_status_t result = nmo_type_registry_register_plugin(registry, &plugins[i]);
        ASSERT_EQ(NMO_OK, result);
    }
    
    /* Verify all plugins are retrievable */
    const nmo_plugin_t *p1 = nmo_type_registry_get_plugin(registry, PLUGIN_GUID_1);
    const nmo_plugin_t *p2 = nmo_type_registry_get_plugin(registry, PLUGIN_GUID_2);
    const nmo_plugin_t *p3 = nmo_type_registry_get_plugin(registry, PLUGIN_GUID_3);
    
    ASSERT_NE(NULL, p1);
    ASSERT_NE(NULL, p2);
    ASSERT_NE(NULL, p3);
    ASSERT_STR_EQ("Plugin 1", p1->name);
    ASSERT_STR_EQ("Plugin 2", p2->name);
    ASSERT_STR_EQ("Plugin 3", p3->name);
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Duplicate Plugin Registration (Idempotent)
 * ============================================================================ */

TEST(plugin_tracking, duplicate_registration_is_noop) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    nmo_plugin_t plugin = {0};
    plugin.guid = PLUGIN_GUID_1;
    plugin.name = "Test Plugin";
    plugin.version = 0x00010000;
    plugin.category = 0x01;
    
    /* Register once */
    nmo_status_t result1 = nmo_type_registry_register_plugin(registry, &plugin);
    ASSERT_EQ(NMO_OK, result1);
    
    /* Register again (should be no-op) */
    nmo_status_t result2 = nmo_type_registry_register_plugin(registry, &plugin);
    ASSERT_EQ(NMO_OK, result2);
    
    /* Verify plugin is still retrievable */
    const nmo_plugin_t *retrieved = nmo_type_registry_get_plugin(registry, PLUGIN_GUID_1);
    ASSERT_NE(NULL, retrieved);
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Type-to-Plugin Association
 * ============================================================================ */

TEST(plugin_tracking, type_plugin_association) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 8192);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register plugin */
    nmo_plugin_t plugin = {0};
    plugin.guid = PLUGIN_GUID_1;
    plugin.name = "Custom Plugin";
    plugin.version = 0x00010000;
    plugin.category = 0x01;
    
    nmo_status_t result = nmo_type_registry_register_plugin(registry, &plugin);
    ASSERT_EQ(NMO_OK, result);
    
    /* Register type associated with plugin */
    nmo_type_descriptor_t type = {0};
    type.guid = TYPE_GUID_CUSTOM_1;
    type.name = "CustomType1";
    type.category = NMO_TYPE_CATEGORY_STRUCT;
    type.size = 32;
    type.alignment = 4;
    type.creator_plugin = &plugin;
    type.valid = true;
    
    result = nmo_type_registry_register(registry, &type);
    ASSERT_EQ(NMO_OK, result);
    
    /* Verify type is registered */
    const nmo_type_descriptor_t *retrieved_type = 
        nmo_type_registry_find_by_guid(registry, TYPE_GUID_CUSTOM_1);
    ASSERT_NE(NULL, retrieved_type);
    ASSERT_NE(NULL, retrieved_type->creator_plugin);
    ASSERT_TRUE(nmo_guid_equals(PLUGIN_GUID_1, retrieved_type->creator_plugin->guid));
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Multiple Types from Same Plugin
 * ============================================================================ */

TEST(plugin_tracking, multiple_types_same_plugin) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register plugin */
    nmo_plugin_t plugin = {0};
    plugin.guid = PLUGIN_GUID_1;
    plugin.name = "Multi-Type Plugin";
    plugin.version = 0x00010000;
    plugin.category = 0x01;
    
    nmo_status_t result = nmo_type_registry_register_plugin(registry, &plugin);
    ASSERT_EQ(NMO_OK, result);
    
    /* Register three types from same plugin */
    nmo_type_descriptor_t types[3] = {
        {
            .guid = TYPE_GUID_CUSTOM_1,
            .name = "CustomType1",
            .category = NMO_TYPE_CATEGORY_STRUCT,
            .size = 16,
            .alignment = 4,
            .creator_plugin = &plugin,
            .valid = true
        },
        {
            .guid = TYPE_GUID_CUSTOM_2,
            .name = "CustomType2",
            .category = NMO_TYPE_CATEGORY_SCALAR,
            .size = 8,
            .alignment = 8,
            .creator_plugin = &plugin,
            .valid = true
        },
        {
            .guid = TYPE_GUID_CUSTOM_3,
            .name = "CustomType3",
            .category = NMO_TYPE_CATEGORY_ENUM,
            .size = 4,
            .alignment = 4,
            .creator_plugin = &plugin,
            .valid = true
        }
    };
    
    for (int i = 0; i < 3; i++) {
        result = nmo_type_registry_register(registry, &types[i]);
        ASSERT_EQ(NMO_OK, result);
    }
    
    /* Verify all types are associated with the plugin */
    for (int i = 0; i < 3; i++) {
        const nmo_type_descriptor_t *retrieved = 
            nmo_type_registry_find_by_guid(registry, types[i].guid);
        ASSERT_NE(NULL, retrieved);
        ASSERT_NE(NULL, retrieved->creator_plugin);
        ASSERT_TRUE(nmo_guid_equals(PLUGIN_GUID_1, retrieved->creator_plugin->guid));
    }
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Plugin Unregistration (Batch Type Removal)
 * ============================================================================ */

TEST(plugin_tracking, unregister_plugin_types) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 16384);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register plugin */
    nmo_plugin_t plugin = {0};
    plugin.guid = PLUGIN_GUID_1;
    plugin.name = "Removable Plugin";
    plugin.version = 0x00010000;
    plugin.category = 0x01;
    
    nmo_status_t result = nmo_type_registry_register_plugin(registry, &plugin);
    ASSERT_EQ(NMO_OK, result);
    
    /* Register two types from this plugin */
    nmo_type_descriptor_t type1 = {0};
    type1.guid = TYPE_GUID_CUSTOM_1;
    type1.name = "PluginType1";
    type1.category = NMO_TYPE_CATEGORY_STRUCT;
    type1.size = 32;
    type1.alignment = 4;
    type1.creator_plugin = &plugin;
    type1.valid = true;
    
    nmo_type_descriptor_t type2 = {0};
    type2.guid = TYPE_GUID_CUSTOM_2;
    type2.name = "PluginType2";
    type2.category = NMO_TYPE_CATEGORY_SCALAR;
    type2.size = 16;
    type2.alignment = 4;
    type2.creator_plugin = &plugin;
    type2.valid = true;
    
    result = nmo_type_registry_register(registry, &type1);
    ASSERT_EQ(NMO_OK, result);
    result = nmo_type_registry_register(registry, &type2);
    ASSERT_EQ(NMO_OK, result);
    
    /* Verify types exist */
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, TYPE_GUID_CUSTOM_1));
    ASSERT_NE(NULL, nmo_type_registry_find_by_guid(registry, TYPE_GUID_CUSTOM_2));
    
    /* Unregister all plugin types */
    result = nmo_type_registry_unregister_plugin_types(registry, PLUGIN_GUID_1);
    ASSERT_EQ(NMO_OK, result);
    
    /* Verify types are removed (or marked invalid) */
    const nmo_type_descriptor_t *removed1 = 
        nmo_type_registry_find_by_guid(registry, TYPE_GUID_CUSTOM_1);
    const nmo_type_descriptor_t *removed2 = 
        nmo_type_registry_find_by_guid(registry, TYPE_GUID_CUSTOM_2);
    
    /* Types should either be NULL or marked invalid */
    if (removed1 != NULL) {
        ASSERT_FALSE(removed1->valid);
    }
    if (removed2 != NULL) {
        ASSERT_FALSE(removed2->valid);
    }
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Mixed Plugin Unregistration
 * ============================================================================ */

TEST(plugin_tracking, selective_plugin_unregistration) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 32768);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register two plugins */
    nmo_plugin_t plugin1 = {
        .guid = PLUGIN_GUID_1, 
        .name = "Plugin 1", 
        .version = 0x00010000, 
        .category = 0x01
    };
    nmo_plugin_t plugin2 = {
        .guid = PLUGIN_GUID_2, 
        .name = "Plugin 2", 
        .version = 0x00020000, 
        .category = 0x02
    };
    
    nmo_status_t result = nmo_type_registry_register_plugin(registry, &plugin1);
    ASSERT_EQ(NMO_OK, result);
    result = nmo_type_registry_register_plugin(registry, &plugin2);
    ASSERT_EQ(NMO_OK, result);
    
    /* Register types from both plugins */
    nmo_type_descriptor_t type_p1 = {
        .guid = TYPE_GUID_CUSTOM_1,
        .name = "Plugin1Type",
        .category = NMO_TYPE_CATEGORY_STRUCT,
        .size = 32,
        .alignment = 4,
        .creator_plugin = &plugin1,
        .valid = true
    };
    
    nmo_type_descriptor_t type_p2 = {
        .guid = TYPE_GUID_CUSTOM_2,
        .name = "Plugin2Type",
        .category = NMO_TYPE_CATEGORY_SCALAR,
        .size = 16,
        .alignment = 4,
        .creator_plugin = &plugin2,
        .valid = true
    };
    
    result = nmo_type_registry_register(registry, &type_p1);
    ASSERT_EQ(NMO_OK, result);
    result = nmo_type_registry_register(registry, &type_p2);
    ASSERT_EQ(NMO_OK, result);
    
    /* Unregister only plugin 1's types */
    result = nmo_type_registry_unregister_plugin_types(registry, PLUGIN_GUID_1);
    ASSERT_EQ(NMO_OK, result);
    
    /* Plugin 1's type should be removed/invalid */
    const nmo_type_descriptor_t *p1_type = 
        nmo_type_registry_find_by_guid(registry, TYPE_GUID_CUSTOM_1);
    if (p1_type != NULL) {
        ASSERT_FALSE(p1_type->valid);
    }
    
    /* Plugin 2's type should still be valid */
    const nmo_type_descriptor_t *p2_type = 
        nmo_type_registry_find_by_guid(registry, TYPE_GUID_CUSTOM_2);
    ASSERT_NE(NULL, p2_type);
    ASSERT_TRUE(p2_type->valid);
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test: Invalid Arguments
 * ============================================================================ */

TEST(plugin_tracking, invalid_arguments) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);
    
    nmo_type_registry_t *registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* NULL registry */
    nmo_plugin_t plugin = {
        .guid = PLUGIN_GUID_1,
        .name = "Test",
        .version = 0x00010000,
        .category = 0x01
    };
    nmo_status_t result = nmo_type_registry_register_plugin(NULL, &plugin);
    ASSERT_NE(NMO_OK, result);
    
    /* NULL plugin */
    result = nmo_type_registry_register_plugin(registry, NULL);
    ASSERT_NE(NMO_OK, result);
    
    /* NULL registry for get */
    const nmo_plugin_t *retrieved = nmo_type_registry_get_plugin(NULL, PLUGIN_GUID_1);
    ASSERT_EQ(NULL, retrieved);
    
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

/* ============================================================================
 * Test Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(plugin_tracking, register_and_get_plugin);
    REGISTER_TEST(plugin_tracking, register_multiple_plugins);
    REGISTER_TEST(plugin_tracking, duplicate_registration_is_noop);
    REGISTER_TEST(plugin_tracking, type_plugin_association);
    REGISTER_TEST(plugin_tracking, multiple_types_same_plugin);
    REGISTER_TEST(plugin_tracking, unregister_plugin_types);
    REGISTER_TEST(plugin_tracking, selective_plugin_unregistration);
    REGISTER_TEST(plugin_tracking, invalid_arguments);
TEST_MAIN_END()
