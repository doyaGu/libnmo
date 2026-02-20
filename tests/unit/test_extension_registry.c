/**
 * @file test_extension_registry.c
 * @brief Unit tests for the Extension registry
 */

#include "test_framework.h"
#include "extension/nmo_extension_registry.h"
#include "extension/nmo_extension_abi.h"
#include "type/nmo_type_system.h"
#include "format/nmo_manager.h"
#include "format/nmo_manager_registry.h"
#include "core/nmo_arena.h"

/* ============================================================================
 * Test Helpers
 * ============================================================================ */

static nmo_arena_t *g_arena = NULL;
static nmo_type_registry_t *g_type_registry = NULL;
static nmo_manager_registry_t *g_manager_registry = NULL;

static void setup_registries(void) {
    g_arena = nmo_arena_create(NULL, 0);
    g_type_registry = nmo_type_registry_create(g_arena);
    g_manager_registry = nmo_manager_registry_create(g_arena);
}

static void teardown_registries(void) {
    nmo_manager_registry_destroy(g_manager_registry);
    nmo_type_registry_destroy(g_type_registry);
    nmo_arena_destroy(g_arena);

    g_manager_registry = NULL;
    g_type_registry = NULL;
    g_arena = NULL;
}

/* ============================================================================
 * Test Plugin Descriptors
 * ============================================================================ */

static nmo_status_t dummy_init(const nmo_extension_host_t *host, void *host_user) {
    (void)host;
    (void)host_user;
    return NMO_OK;
}

static int g_ext_manager_preload_hits = 0;

static int ext_manager_pre_load(void *session, void *user_data) {
    (void)session;
    int *hits = (int *)user_data;
    if (hits != NULL) {
        (*hits)++;
    }
    return NMO_OK;
}

static nmo_status_t register_manager_init(const nmo_extension_host_t *host, void *host_user) {
    if (host == NULL || host->register_managers == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_extension_manager_desc_t manager_desc = {0};
    manager_desc.manager_id = 701;
    manager_desc.guid = NMO_GUID(0x70170170, 0x17017017);
    manager_desc.name = "ExtRegisteredManager";
    manager_desc.category = NMO_PLUGIN_MANAGER_DLL;
    manager_desc.pre_load = ext_manager_pre_load;
    manager_desc.user_data = &g_ext_manager_preload_hits;

    nmo_guid_t plugin_guid = NMO_GUID(0xABCDEF10, 0x00000001);
    return host->register_managers(host_user, plugin_guid, &manager_desc, 1);
}

static void dummy_shutdown(const nmo_extension_host_t *host, void *host_user) {
    (void)host;
    (void)host_user;
}

static nmo_extension_plugin_t create_test_plugin(nmo_guid_t guid, const char *name) {
    nmo_extension_plugin_t plugin = {0};
    plugin.abi_version = NMO_EXTENSION_ABI_VERSION;
    plugin.struct_size = sizeof(nmo_extension_plugin_t);
    plugin.guid = guid;
    plugin.version = 0x010000;  /* v1.0.0 */
    plugin.category = NMO_PLUGIN_CUSTOM_DLL;
    plugin.name = name;
    plugin.init = dummy_init;
    plugin.shutdown = dummy_shutdown;
    return plugin;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(extension, create_destroy) {
    setup_registries();

    nmo_extension_registry_t *registry = nmo_extension_registry_create(
        NULL, g_type_registry, g_manager_registry);
    ASSERT_NOT_NULL(registry);

    size_t count = nmo_extension_registry_get_count(registry);
    ASSERT_EQ(0, (int)count);

    nmo_extension_registry_destroy(registry);

    teardown_registries();
}

TEST(extension, register_static_single) {
    setup_registries();

    nmo_extension_registry_t *registry = nmo_extension_registry_create(
        NULL, g_type_registry, g_manager_registry);
    ASSERT_NOT_NULL(registry);

    nmo_guid_t guid = NMO_GUID(0x12345678, 0x9ABCDEF0);
    nmo_extension_plugin_t plugin = create_test_plugin(guid, "TestPlugin");

    nmo_status_t status = nmo_extension_registry_register_static(registry, &plugin, 1);
    ASSERT_EQ(NMO_OK, status);

    size_t count = nmo_extension_registry_get_count(registry);
    ASSERT_EQ(1, (int)count);

    /* Find the plugin */
    const nmo_extension_plugin_info_t *info = nmo_extension_registry_find(registry, guid);
    ASSERT_NOT_NULL(info);
    ASSERT_EQ(guid.d1, info->guid.d1);
    ASSERT_EQ(guid.d2, info->guid.d2);
    ASSERT_STR_EQ("TestPlugin", info->name);

    nmo_extension_registry_destroy(registry);

    teardown_registries();
}

TEST(extension, duplicate_guid) {
    setup_registries();

    nmo_extension_registry_t *registry = nmo_extension_registry_create(
        NULL, g_type_registry, g_manager_registry);
    ASSERT_NOT_NULL(registry);

    nmo_guid_t guid = NMO_GUID(0xAAAAAAAA, 0xBBBBBBBB);
    nmo_extension_plugin_t plugin = create_test_plugin(guid, "TestPlugin");

    /* First registration should succeed */
    nmo_status_t status = nmo_extension_registry_register_static(registry, &plugin, 1);
    ASSERT_EQ(NMO_OK, status);

    /* Duplicate should fail */
    status = nmo_extension_registry_register_static(registry, &plugin, 1);
    ASSERT_EQ(NMO_ERR_ALREADY_EXISTS, status);

    nmo_extension_registry_destroy(registry);

    teardown_registries();
}

TEST(extension, unload_by_guid) {
    setup_registries();

    nmo_extension_registry_t *registry = nmo_extension_registry_create(
        NULL, g_type_registry, g_manager_registry);
    ASSERT_NOT_NULL(registry);

    nmo_guid_t guid = NMO_GUID(0x11111111, 0x22222222);
    nmo_extension_plugin_t plugin = create_test_plugin(guid, "UnloadTest");

    nmo_status_t status = nmo_extension_registry_register_static(registry, &plugin, 1);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(1, (int)nmo_extension_registry_get_count(registry));

    /* Unload the plugin */
    status = nmo_extension_registry_unload_by_guid(registry, guid);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(0, (int)nmo_extension_registry_get_count(registry));

    /* Unloading again should fail */
    status = nmo_extension_registry_unload_by_guid(registry, guid);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, status);

    nmo_extension_registry_destroy(registry);

    teardown_registries();
}

TEST(extension, abi_version_mismatch) {
    setup_registries();

    nmo_extension_registry_t *registry = nmo_extension_registry_create(
        NULL, g_type_registry, g_manager_registry);
    ASSERT_NOT_NULL(registry);

    nmo_extension_plugin_t plugin = {0};
    plugin.abi_version = NMO_EXTENSION_ABI_VERSION + 1;  /* Wrong version */
    plugin.struct_size = sizeof(nmo_extension_plugin_t);
    plugin.guid = NMO_GUID(0x33333333, 0x44444444);
    plugin.version = 0x010000;
    plugin.category = NMO_PLUGIN_CUSTOM_DLL;
    plugin.name = "BadVersion";
    plugin.init = dummy_init;
    plugin.shutdown = NULL;

    nmo_status_t status = nmo_extension_registry_register_static(registry, &plugin, 1);
    ASSERT_EQ(NMO_ERR_UNSUPPORTED_VERSION, status);

    nmo_extension_registry_destroy(registry);

    teardown_registries();
}

TEST(extension, abi_compatibility_check) {
    /* Test the inline compatibility check functions */

    /* Valid plugin */
    nmo_extension_plugin_t valid_plugin = {0};
    valid_plugin.abi_version = NMO_EXTENSION_ABI_VERSION;
    valid_plugin.struct_size = sizeof(nmo_extension_plugin_t);
    ASSERT_TRUE(nmo_extension_plugin_is_compatible(&valid_plugin));

    /* Wrong ABI version */
    nmo_extension_plugin_t bad_version = {0};
    bad_version.abi_version = NMO_EXTENSION_ABI_VERSION + 1;
    bad_version.struct_size = sizeof(nmo_extension_plugin_t);
    ASSERT_TRUE(!nmo_extension_plugin_is_compatible(&bad_version));

    /* Struct too small */
    nmo_extension_plugin_t too_small = {0};
    too_small.abi_version = NMO_EXTENSION_ABI_VERSION;
    too_small.struct_size = 8;  /* Too small */
    ASSERT_TRUE(!nmo_extension_plugin_is_compatible(&too_small));

    /* NULL pointer */
    ASSERT_TRUE(!nmo_extension_plugin_is_compatible(NULL));

    /* Valid host */
    nmo_extension_host_t valid_host = {0};
    valid_host.abi_version = NMO_EXTENSION_ABI_VERSION;
    valid_host.struct_size = sizeof(nmo_extension_host_t);
    ASSERT_TRUE(nmo_extension_host_is_compatible(&valid_host));
}

TEST(extension, host_register_manager_callbacks) {
    setup_registries();

    nmo_extension_registry_t *registry = nmo_extension_registry_create(
        NULL, g_type_registry, g_manager_registry);
    ASSERT_NOT_NULL(registry);

    g_ext_manager_preload_hits = 0;

    nmo_extension_plugin_t plugin = {0};
    plugin.abi_version = NMO_EXTENSION_ABI_VERSION;
    plugin.struct_size = sizeof(nmo_extension_plugin_t);
    plugin.guid = NMO_GUID(0xABCDEF10, 0x00000001);
    plugin.version = 0x010000;
    plugin.category = NMO_PLUGIN_CUSTOM_DLL;
    plugin.name = "ManagerContributionPlugin";
    plugin.init = register_manager_init;
    plugin.shutdown = dummy_shutdown;

    nmo_status_t status = nmo_extension_registry_register_static(registry, &plugin, 1);
    ASSERT_EQ(NMO_OK, status);

    nmo_manager_t *manager = (nmo_manager_t *)nmo_manager_registry_get(g_manager_registry, 701);
    ASSERT_NOT_NULL(manager);

    status = nmo_manager_invoke_pre_load(manager, NULL);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_EQ(1, g_ext_manager_preload_hits);

    status = nmo_extension_registry_unload_by_guid(registry, plugin.guid);
    ASSERT_EQ(NMO_OK, status);
    ASSERT_NULL((nmo_manager_t *)nmo_manager_registry_get(g_manager_registry, 701));

    nmo_extension_registry_destroy(registry);
    teardown_registries();
}

/* ============================================================================
 * Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(extension, create_destroy);
    REGISTER_TEST(extension, register_static_single);
    REGISTER_TEST(extension, duplicate_guid);
    REGISTER_TEST(extension, unload_by_guid);
    REGISTER_TEST(extension, abi_version_mismatch);
    REGISTER_TEST(extension, abi_compatibility_check);
    REGISTER_TEST(extension, host_register_manager_callbacks);
TEST_MAIN_END()
