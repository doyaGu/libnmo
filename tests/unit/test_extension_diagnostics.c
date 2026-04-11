/**
 * @file test_extension_diagnostics.c
 * @brief Unit tests for extension dependency diagnostics
 */

#include "test_framework.h"
#include "extension/nmo_extension_diagnostics.h"
#include "extension/nmo_extension_registry.h"
#include "extension/nmo_extension_abi.h"
#include "extension/nmo_extension_host.h"
#include "type/nmo_type_system.h"
#include "format/nmo_manager_registry.h"
#include "core/nmo_arena.h"

static nmo_arena_t *g_arena = NULL;
static nmo_type_registry_t *g_type_registry = NULL;
static nmo_manager_registry_t *g_manager_registry = NULL;
static bool g_shutdown_context_seen = false;
static nmo_guid_t g_shutdown_expected_guid = {0, 0};

static nmo_status_t dummy_init(const nmo_extension_host_t *host, void *host_user) {
    (void)host;
    (void)host_user;
    return NMO_OK;
}

static void shutdown_context_check(const nmo_extension_host_t *host, void *host_user) {
    (void)host;
    g_shutdown_context_seen = false;
    if (host_user == NULL) {
        return;
    }

    const nmo_extension_host_context_t *ctx = (const nmo_extension_host_context_t *)host_user;
    if (ctx->registry != NULL && nmo_guid_equals(ctx->plugin_guid, g_shutdown_expected_guid)) {
        g_shutdown_context_seen = true;
    }
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
        NULL, g_type_registry, NULL, NULL, g_manager_registry);
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
        NULL, g_type_registry, NULL, NULL, g_manager_registry);
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

TEST(extension_diagnostics, list_is_contiguous_and_ordered) {
    setup_registries();

    nmo_extension_registry_t *registry = nmo_extension_registry_create(
        NULL, g_type_registry, NULL, NULL, g_manager_registry);
    ASSERT_NOT_NULL(registry);

    nmo_extension_plugin_t plugins[2];
    plugins[0] = create_plugin(
        NMO_GUID(0x10000001, 0x20000001), NMO_PLUGIN_CUSTOM_DLL, 0x010000, "One");
    plugins[1] = create_plugin(
        NMO_GUID(0x10000002, 0x20000002), NMO_PLUGIN_CUSTOM_DLL, 0x010000, "Two");

    ASSERT_EQ(NMO_OK, nmo_extension_registry_register_static(registry, plugins, 2));

    size_t count = 0;
    const nmo_extension_plugin_info_t *infos = nmo_extension_registry_list(registry, &count);
    ASSERT_NOT_NULL(infos);
    ASSERT_EQ(2, (int)count);
    ASSERT_STR_EQ("One", infos[0].name);
    ASSERT_STR_EQ("Two", infos[1].name);
    ASSERT_TRUE(nmo_guid_equals(infos[0].guid, plugins[0].guid));
    ASSERT_TRUE(nmo_guid_equals(infos[1].guid, plugins[1].guid));

    nmo_extension_registry_destroy(registry);
    teardown_registries();
}

TEST(extension_diagnostics, unload_shutdown_receives_host_context) {
    setup_registries();

    nmo_extension_registry_t *registry = nmo_extension_registry_create(
        NULL, g_type_registry, NULL, NULL, g_manager_registry);
    ASSERT_NOT_NULL(registry);

    nmo_extension_plugin_t plugin = create_plugin(
        NMO_GUID(0x30303030, 0x40404040), NMO_PLUGIN_CUSTOM_DLL, 0x010000, "Ctx");
    plugin.shutdown = shutdown_context_check;
    g_shutdown_expected_guid = plugin.guid;
    g_shutdown_context_seen = false;

    ASSERT_EQ(NMO_OK, nmo_extension_registry_register_static(registry, &plugin, 1));
    ASSERT_EQ(NMO_OK, nmo_extension_registry_unload_by_guid(registry, plugin.guid));
    ASSERT_TRUE(g_shutdown_context_seen);

    nmo_extension_registry_destroy(registry);
    teardown_registries();
}

TEST(extension_diagnostics, extension_errors_refinalize_type_registry) {
    setup_registries();

    nmo_extension_registry_t *registry = nmo_extension_registry_create(
        NULL, g_type_registry, NULL, NULL, g_manager_registry);
    ASSERT_NOT_NULL(registry);

    ASSERT_EQ(NMO_OK, nmo_type_registry_finalize(g_type_registry));

    nmo_extension_plugin_t bad = create_plugin(
        NMO_GUID(0x50505050, 0x60606060), NMO_PLUGIN_CUSTOM_DLL, 0x010000, "BadAbi");
    bad.abi_version = NMO_EXTENSION_ABI_VERSION + 1;
    ASSERT_EQ(NMO_ERR_UNSUPPORTED_VERSION, nmo_extension_registry_register_static(registry, &bad, 1));
    ASSERT_TRUE(g_type_registry->finalized);

    nmo_status_t load_status = nmo_extension_registry_load_library(
        registry, "__missing_extension_for_test__.dll", NULL);
    ASSERT_NE(NMO_OK, load_status);
    ASSERT_TRUE(g_type_registry->finalized);

    nmo_status_t unload_status = nmo_extension_registry_unload_by_guid(
        registry, NMO_GUID(0x77770000, 0x88880000));
    ASSERT_EQ(NMO_ERR_NOT_FOUND, unload_status);
    ASSERT_TRUE(g_type_registry->finalized);

    nmo_extension_registry_destroy(registry);
    teardown_registries();
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(extension_diagnostics, dependency_category_and_version);
    REGISTER_TEST(extension_diagnostics, wildcard_category_and_batch_defaults);
    REGISTER_TEST(extension_diagnostics, null_registry_reports_unsatisfied);
    REGISTER_TEST(extension_diagnostics, list_is_contiguous_and_ordered);
    REGISTER_TEST(extension_diagnostics, unload_shutdown_receives_host_context);
    REGISTER_TEST(extension_diagnostics, extension_errors_refinalize_type_registry);
TEST_MAIN_END()
