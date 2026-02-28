/**
 * @file test_extension_diagnostics.c
 * @brief Unit tests for extension dependency diagnostics
 */

#include "test_framework.h"
#include "extension/nmo_extension_diagnostics.h"
#include "extension/nmo_extension_registry.h"
#include "extension/nmo_extension_abi.h"
#include "type/nmo_type_system.h"
#include "format/nmo_manager_registry.h"
#include "core/nmo_arena.h"

static nmo_arena_t *g_arena = NULL;
static nmo_type_registry_t *g_type_registry = NULL;
static nmo_manager_registry_t *g_manager_registry = NULL;

static nmo_status_t dummy_init(const nmo_extension_host_t *host, void *host_user) {
    (void)host;
    (void)host_user;
    return NMO_OK;
}

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

static nmo_extension_plugin_t create_plugin(
    nmo_guid_t guid,
    nmo_plugin_category_t category,
    uint32_t version,
    const char *name)
{
    nmo_extension_plugin_t plugin = {0};
    plugin.abi_version = NMO_EXTENSION_ABI_VERSION;
    plugin.struct_size = sizeof(nmo_extension_plugin_t);
    plugin.guid = guid;
    plugin.version = version;
    plugin.category = category;
    plugin.name = name;
    plugin.init = dummy_init;
    plugin.shutdown = NULL;
    return plugin;
}

TEST(extension_diagnostics, dependency_category_and_version) {
    setup_registries();

    nmo_extension_registry_t *registry = nmo_extension_registry_create(
        NULL, g_type_registry, g_manager_registry);
    ASSERT_NOT_NULL(registry);

    nmo_guid_t guid = NMO_GUID(0x12344321, 0xAABBCCDD);
    nmo_extension_plugin_t plugin = create_plugin(
        guid, NMO_PLUGIN_BEHAVIOR_DLL, 0x010200, "BehaviorPlugin");

    nmo_status_t status = nmo_extension_registry_register_static(registry, &plugin, 1);
    ASSERT_EQ(NMO_OK, status);

    nmo_extension_dependency_result_t result = {0};

    int satisfied = nmo_extension_check_dependency(
        registry, NMO_PLUGIN_BEHAVIOR_DLL, guid, 0x010100, &result);
    ASSERT_TRUE(satisfied);
    ASSERT_TRUE(result.satisfied);
    ASSERT_EQ(0x010100, result.required_version);
    ASSERT_EQ(0x010200, result.found_version);

    satisfied = nmo_extension_check_dependency(
        registry, NMO_PLUGIN_MANAGER_DLL, guid, 0, &result);
    ASSERT_FALSE(satisfied);
    ASSERT_FALSE(result.satisfied);
    ASSERT_EQ(0x010200, result.found_version);

    satisfied = nmo_extension_check_dependency(
        registry, NMO_PLUGIN_BEHAVIOR_DLL, guid, 0x020000, &result);
    ASSERT_FALSE(satisfied);
    ASSERT_FALSE(result.satisfied);
    ASSERT_EQ(0x020000, result.required_version);
    ASSERT_EQ(0x010200, result.found_version);

    nmo_extension_registry_destroy(registry);
    teardown_registries();
}

TEST(extension_diagnostics, wildcard_category_and_batch_defaults) {
    setup_registries();

    nmo_extension_registry_t *registry = nmo_extension_registry_create(
        NULL, g_type_registry, g_manager_registry);
    ASSERT_NOT_NULL(registry);

    nmo_guid_t behavior_guid = NMO_GUID(0xBEEF0001, 0x00000010);
    nmo_guid_t manager_guid = NMO_GUID(0xBEEF0002, 0x00000020);
    nmo_guid_t missing_guid = NMO_GUID(0xDEAD0003, 0x00000030);

    nmo_extension_plugin_t plugins[2];
    plugins[0] = create_plugin(behavior_guid, NMO_PLUGIN_BEHAVIOR_DLL, 0x010000, "Behavior");
    plugins[1] = create_plugin(manager_guid, NMO_PLUGIN_MANAGER_DLL, 0x010000, "Manager");

    nmo_status_t status = nmo_extension_registry_register_static(registry, plugins, 2);
    ASSERT_EQ(NMO_OK, status);

    nmo_extension_dependency_result_t single = {0};
    int satisfied = nmo_extension_check_dependency(
        registry, NMO_PLUGIN_CUSTOM_DLL, behavior_guid, 0, &single);
    ASSERT_TRUE(satisfied);
    ASSERT_TRUE(single.satisfied);

    nmo_guid_t guids[] = { behavior_guid, manager_guid, missing_guid };
    uint32_t versions[] = { 0, 0, 0 };
    nmo_extension_dependency_result_t results[3] = {0};

    size_t unsatisfied = nmo_extension_check_dependencies(
        registry, NULL, guids, versions, 3, results);
    ASSERT_EQ(1, (int)unsatisfied);
    ASSERT_TRUE(results[0].satisfied);
    ASSERT_TRUE(results[1].satisfied);
    ASSERT_FALSE(results[2].satisfied);
    ASSERT_EQ(0, results[2].found_version);

    nmo_extension_registry_destroy(registry);
    teardown_registries();
}

TEST(extension_diagnostics, null_registry_reports_unsatisfied) {
    nmo_guid_t guid = NMO_GUID(0xCAFEBABE, 0x00000001);
    nmo_extension_dependency_result_t result = {0};

    int satisfied = nmo_extension_check_dependency(
        NULL, NMO_PLUGIN_MANAGER_DLL, guid, 0x010000, &result);
    ASSERT_FALSE(satisfied);
    ASSERT_FALSE(result.satisfied);
    ASSERT_EQ(0x010000, result.required_version);
    ASSERT_EQ(0, result.found_version);
    ASSERT_EQ(guid.d1, result.guid.d1);
    ASSERT_EQ(guid.d2, result.guid.d2);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(extension_diagnostics, dependency_category_and_version);
    REGISTER_TEST(extension_diagnostics, wildcard_category_and_batch_defaults);
    REGISTER_TEST(extension_diagnostics, null_registry_reports_unsatisfied);
TEST_MAIN_END()
