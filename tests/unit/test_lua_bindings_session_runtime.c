#include "test_framework.h"

#include "lua/nmo_lua_bindings.h"
#include "lua/nmo_lua_runtime.h"

#include <stdio.h>

static void assert_lua_ok(nmo_lua_runtime_t *runtime, const char *script)
{
    nmo_status_t status = nmo_lua_runtime_execute_string(runtime, script);
    if (status != NMO_OK) {
        const char *message = nmo_last_error_message();
        if (message != NULL) {
            fprintf(stderr, "Lua failure: %s\n", message);
        }
    }
    ASSERT_EQ(NMO_OK, status);
}

TEST(lua_bindings_session_runtime, session_module_is_advanced_only_and_keeps_runtime_diagnostics)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local context = require('nmo.context')\n"
        "local document = require('nmo.document')\n"
        "local session = require('nmo.session')\n"
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo") "')\n"
        "local s = session.from_document(doc)\n"
        "assert(s ~= nil)\n"
        "assert(type(session.index_build_flags.class) == 'number')\n"
        "assert(type(session.query_index_flags.all) == 'number')\n"
        "assert(session.is_partial_load(s) == false)\n"
        "assert(session.has_materialized_load_state(s) == true)\n"
        "local stats = session.runtime_load_stats(s)\n"
        "assert(type(stats) == 'table')\n"
        "assert(type(stats.total_objects) == 'number')\n"
        "assert(type(session.plugin_diagnostics(s)) == 'table' or session.plugin_diagnostics(s) == nil)\n"
        "assert(type(session.behavior_interface_diagnostics(s)) == 'table')\n"
        "assert(type(session.included_files(s)) == 'table')\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_session_runtime, removed_canonical_session_helpers_are_absent)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local session = require('nmo.session')\n"
        "assert(session.create ~= nil)\n"
        "assert(session.from_document ~= nil)\n"
        "assert(session.from_workspace ~= nil)\n"
        "assert(session.create_context == nil)\n"
        "assert(session.load_file == nil)\n"
        "assert(session.save_file == nil)\n"
        "assert(session.object_count == nil)\n"
        "assert(session.find_object_by_name == nil)\n"
        "assert(session.query_first == nil)\n"
        "assert(session.query_collect == nil)\n"
        "assert(session.query_collect_info == nil)\n"
        "assert(session.create_object == nil)\n"
        "assert(session.copy_objects == nil)\n"
        "assert(session.copy_objects_info == nil)\n"
        "assert(session.destroy_objects == nil)\n"
        "assert(session.destroy_objects_info == nil)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_session_runtime, runtime_module_still_accepts_advanced_session_handles)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local context = require('nmo.context')\n"
        "local document = require('nmo.document')\n"
        "local session = require('nmo.session')\n"
        "local object = require('nmo.object')\n"
        "local runtime_mod = require('nmo.runtime')\n"
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
        "local s = session.from_document(doc)\n"
        "local cam = object.find_object_by_name(doc, 'InGameCam')\n"
        "local ids = runtime_mod.preview_destroy(s, { cam })\n"
        "assert(type(ids) == 'table')\n"
        "assert(#ids >= 1)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings_session_runtime,
                  session_module_is_advanced_only_and_keeps_runtime_diagnostics);
    REGISTER_TEST(lua_bindings_session_runtime,
                  removed_canonical_session_helpers_are_absent);
    REGISTER_TEST(lua_bindings_session_runtime,
                  runtime_module_still_accepts_advanced_session_handles);
TEST_MAIN_END()
