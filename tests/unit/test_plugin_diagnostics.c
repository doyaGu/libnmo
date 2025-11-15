/**
 * @file test_plugin_diagnostics.c
 * @brief Unit tests for plugin dependency diagnostics.
 */

#include "../test_framework.h"
#include "app/nmo_context.h"
#include "app/nmo_session.h"
#include "app/nmo_plugin.h"
#include "format/nmo_header1.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"
#include <stdalign.h>

static const nmo_guid_t TEST_GUID_A = {0x12345678u, 0x9ABCDEF0u};
static const nmo_guid_t TEST_GUID_B = {0xCAFEBABEu, 0x0BADF00Du};

static nmo_plugin_t make_plugin(const char *name, nmo_guid_t guid, uint32_t version) {
    nmo_plugin_t plugin = {
        .name = name,
        .version = version,
        .guid = guid,
        .category = NMO_PLUGIN_MANAGER_DLL,
        .init = NULL,
        .shutdown = NULL,
        .register_managers = NULL
    };
    return plugin;
}

TEST(plugin_diagnostics, missing_plugin_sets_status_flags) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_arena_t *arena = nmo_session_get_arena(session);
    ASSERT_NOT_NULL(arena);

    nmo_plugin_dep_t *deps = (nmo_plugin_dep_t *) nmo_arena_alloc(
        arena,
        sizeof(nmo_plugin_dep_t),
        alignof(nmo_plugin_dep_t));
    ASSERT_NOT_NULL(deps);
    deps[0].guid = TEST_GUID_A;
    deps[0].category = NMO_PLUGIN_MANAGER_DLL;
    deps[0].version = 5;

    ASSERT_EQ(NMO_OK, nmo_session_set_plugin_dependencies(session, deps, 1));

    const nmo_session_plugin_diagnostics_t *diag = nmo_session_get_plugin_diagnostics(session);
    ASSERT_NOT_NULL(diag);
    ASSERT_EQ(1u, diag->entry_count);
    ASSERT_NOT_NULL(diag->entries);
    ASSERT_EQ(1u, diag->missing_count);
    ASSERT_EQ(0u, diag->outdated_count);

    const nmo_session_plugin_dependency_status_t *entry = &diag->entries[0];
    ASSERT_TRUE((entry->status_flags & NMO_SESSION_PLUGIN_DEP_STATUS_MISSING) != 0);
    ASSERT_EQ(deps[0].version, entry->required_version);
    ASSERT_EQ(NMO_PLUGIN_MANAGER_DLL, entry->category);
    ASSERT_EQ(0u, entry->resolved_version);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(plugin_diagnostics, outdated_plugin_marks_version) {
    nmo_context_desc_t desc = {0};
    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    nmo_plugin_manager_t *plugin_manager = nmo_context_get_plugin_manager(ctx);
    ASSERT_NOT_NULL(plugin_manager);

    const uint32_t registered_version = 2;
    nmo_plugin_t plugin = make_plugin("DiagTestPlugin", TEST_GUID_B, registered_version);
    nmo_plugin_registration_desc_t reg_desc = {
        .plugins = &plugin,
        .plugin_count = 1
    };
    ASSERT_EQ(NMO_OK, nmo_plugin_manager_register(plugin_manager, &reg_desc));

    nmo_session_t *session = nmo_session_create(ctx);
    ASSERT_NOT_NULL(session);

    nmo_arena_t *arena = nmo_session_get_arena(session);
    ASSERT_NOT_NULL(arena);

    nmo_plugin_dep_t *deps = (nmo_plugin_dep_t *) nmo_arena_alloc(
        arena,
        sizeof(nmo_plugin_dep_t),
        alignof(nmo_plugin_dep_t));
    ASSERT_NOT_NULL(deps);
    deps[0].guid = TEST_GUID_B;
    deps[0].category = NMO_PLUGIN_MANAGER_DLL;
    deps[0].version = registered_version + 3;

    ASSERT_EQ(NMO_OK, nmo_session_set_plugin_dependencies(session, deps, 1));

    const nmo_session_plugin_diagnostics_t *diag = nmo_session_get_plugin_diagnostics(session);
    ASSERT_NOT_NULL(diag);
    ASSERT_EQ(1u, diag->entry_count);
    ASSERT_NOT_NULL(diag->entries);

    const nmo_session_plugin_dependency_status_t *entry = &diag->entries[0];
    ASSERT_TRUE((entry->status_flags & NMO_SESSION_PLUGIN_DEP_STATUS_VERSION_TOO_OLD) != 0);
    ASSERT_EQ(deps[0].version, entry->required_version);
    ASSERT_EQ(registered_version, entry->resolved_version);
    ASSERT_TRUE(entry->resolved_name != NULL);
    ASSERT_TRUE(strcmp(entry->resolved_name, plugin.name) == 0);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(plugin_diagnostics, missing_plugin_sets_status_flags);
    REGISTER_TEST(plugin_diagnostics, outdated_plugin_marks_version);
TEST_MAIN_END()
