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

TEST(lua_bindings_app, app_module_exposes_structured_summary_and_diff_helpers)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local session = require('nmo.session')\n"
        "local object = require('nmo.object')\n"
        "local app = require('nmo.app')\n"
        "local s1 = session.load_file(session.create_context(), '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
        "local s2 = session.load_file(session.create_context(), '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
        "local cmp = app.comparison_stats(s1, s2)\n"
        "assert(cmp.match == true)\n"
        "assert(cmp.diff_count == 0)\n"
        "assert(cmp.objects_compared == 18)\n"
        "local cmp_full = app.comparison(s1, s2)\n"
        "assert(cmp_full.match == true)\n"
        "assert(type(cmp_full.diffs) == 'table')\n"
        "assert(#cmp_full.diffs == 0)\n"
        "local diff = app.diff_stats(s1, s2)\n"
        "assert(diff.changed_count == 0)\n"
        "assert(diff.removed_count == 0)\n"
        "assert(diff.added_count == 0)\n"
        "assert(diff.identical_count > 0)\n"
        "local diff_full = app.diff(s1, s2)\n"
        "assert(type(diff_full.changed) == 'table')\n"
        "assert(type(diff_full.renamed) == 'table')\n"
        "assert(type(diff_full.removed) == 'table')\n"
        "assert(type(diff_full.added) == 'table')\n"
        "assert(#diff_full.changed == 0)\n"
        "local cam = session.find_object_by_name(s1, 'InGameCam')\n"
        "assert(cam ~= nil)\n"
        "local stats = app.object_summary_stats(cam)\n"
        "assert(stats.class_id == object.class_id(cam))\n"
        "assert(stats.class_name ~= nil)\n"
        "assert(stats.type_name ~= nil)\n"
        "assert(stats.has_reflection == true)\n"
        "assert(stats.total_fields > 0)\n"
        "local summary = app.object_summary(cam)\n"
        "assert(summary.class_id == stats.class_id)\n"
        "assert(type(summary.fields) == 'table')\n"
        "assert(#summary.fields > 0)\n"
        "assert(summary.fields[1].name ~= nil)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_app, app_module_rejects_invalid_handles_deterministically)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    ASSERT_EQ(
        NMO_OK,
        nmo_lua_runtime_execute_string(
            runtime,
            "local session = require('nmo.session')\n"
            "local app = require('nmo.app')\n"
            "local s = session.load_file(session.create_context(), '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
            "local ok, err = pcall(function()\n"
            "  app.object_summary_stats(nil)\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'object', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  app.comparison_stats(nil, s)\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'session', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  app.diff_stats(s, nil)\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'session', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  app.object_summary(nil)\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'object', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  app.comparison(s, nil)\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'session', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  app.diff(nil, s)\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'session', 1, true) ~= nil)\n"));

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings_app,
                  app_module_exposes_structured_summary_and_diff_helpers);
    REGISTER_TEST(lua_bindings_app,
                  app_module_rejects_invalid_handles_deterministically);
TEST_MAIN_END()
