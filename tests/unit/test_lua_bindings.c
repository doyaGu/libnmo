#include "test_framework.h"

#include "core/nmo_guid.h"
#include "core/nmo_error.h"
#include "lua/nmo_lua_bindings.h"
#include "lua/nmo_lua_handles.h"
#include "lua/nmo_lua_runtime.h"
#include "lua/nmo_lua_value.h"
#include "type/nmo_type_guids.h"

#include <stdlib.h>
#include <stdio.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

typedef struct test_resource {
    int value;
} test_resource_t;

static const nmo_lua_handle_descriptor_t TEST_SESSION_HANDLE = {
    .metatable_name = "nmo.test.session",
    .debug_name = "session"
};

static const nmo_lua_handle_descriptor_t TEST_OBJECT_HANDLE = {
    .metatable_name = "nmo.test.object",
    .debug_name = "object"
};

static int g_release_call_count = 0;
static int g_release_last_value = 0;

static void release_test_resource(void *resource, void *user_data)
{
    test_resource_t *typed = (test_resource_t *)resource;
    int *release_count = (int *)user_data;
    if (typed != NULL) {
        g_release_last_value = typed->value;
    }
    if (release_count != NULL) {
        (*release_count) += 1;
    }
}

static lua_State *create_test_state(void)
{
    lua_State *state = luaL_newstate();
    if (state != NULL) {
        luaL_openlibs(state);
    }
    return state;
}

static void format_guid_literal(nmo_guid_t guid, char *buffer, size_t buffer_size)
{
    ASSERT_TRUE(buffer != NULL);
    ASSERT_TRUE(buffer_size >= 32);
    ASSERT_TRUE(nmo_guid_format(guid, buffer, buffer_size) > 0);
}

static void assert_lua_ok(nmo_lua_runtime_t *runtime, const char *script)
{
    nmo_status_t status = nmo_lua_runtime_execute_string(runtime, script);
    if (status != NMO_OK) {
        const char *message = nmo_last_error_message();
        if (message != NULL && message[0] != '\0') {
            fprintf(stderr, "Lua failure: %s\n", message);
        }
    }
    ASSERT_EQ(NMO_OK, status);
}

TEST(lua_bindings, owned_handle_releases_resource_on_gc) {
    lua_State *state = create_test_state();
    ASSERT_NOT_NULL(state);

    test_resource_t *resource = (test_resource_t *)malloc(sizeof(*resource));
    ASSERT_NOT_NULL(resource);
    resource->value = 42;
    g_release_call_count = 0;
    g_release_last_value = 0;

    nmo_lua_handle_scope_t *session_scope = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_lua_push_owned_handle(state,
                                        &TEST_SESSION_HANDLE,
                                        resource,
                                        release_test_resource,
                                        &g_release_call_count,
                                        &session_scope));
    ASSERT_NOT_NULL(session_scope);
    ASSERT_TRUE(nmo_lua_handle_scope_is_alive(session_scope));

    lua_setglobal(state, "session");
    lua_pushnil(state);
    lua_setglobal(state, "session");
    lua_gc(state, LUA_GCCOLLECT, 0);

    ASSERT_EQ(1, g_release_call_count);
    ASSERT_EQ(42, g_release_last_value);
    ASSERT_FALSE(nmo_lua_handle_scope_is_alive(session_scope));

    nmo_lua_handle_scope_release(session_scope);
    lua_close(state);
}

TEST(lua_bindings, borrowed_handle_rejects_cross_scope_and_stale_access) {
    lua_State *state = create_test_state();
    ASSERT_NOT_NULL(state);

    test_resource_t *session_resource = (test_resource_t *)malloc(sizeof(*session_resource));
    test_resource_t *object_resource = (test_resource_t *)malloc(sizeof(*object_resource));
    test_resource_t *other_session_resource =
        (test_resource_t *)malloc(sizeof(*other_session_resource));
    ASSERT_NOT_NULL(session_resource);
    ASSERT_NOT_NULL(object_resource);
    ASSERT_NOT_NULL(other_session_resource);

    nmo_lua_handle_scope_t *session_scope = NULL;
    nmo_lua_handle_scope_t *other_session_scope = NULL;
    g_release_call_count = 0;

    ASSERT_EQ(NMO_OK,
              nmo_lua_push_owned_handle(state,
                                        &TEST_SESSION_HANDLE,
                                        session_resource,
                                        release_test_resource,
                                        &g_release_call_count,
                                        &session_scope));
    ASSERT_EQ(NMO_OK,
              nmo_lua_push_owned_handle(state,
                                        &TEST_SESSION_HANDLE,
                                        other_session_resource,
                                        release_test_resource,
                                        &g_release_call_count,
                                        &other_session_scope));
    ASSERT_NOT_NULL(session_scope);
    ASSERT_NOT_NULL(other_session_scope);

    lua_pop(state, 2);

    ASSERT_EQ(NMO_OK,
              nmo_lua_push_borrowed_handle(state,
                                           &TEST_OBJECT_HANDLE,
                                           object_resource,
                                           session_scope,
                                           session_scope));

    void *checked_resource = NULL;
    ASSERT_EQ(NMO_OK,
              nmo_lua_handle_check(state,
                                   -1,
                                   &TEST_OBJECT_HANDLE,
                                   session_scope,
                                   &checked_resource));
    ASSERT_EQ(object_resource, checked_resource);

    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_lua_handle_check(state,
                                   -1,
                                   &TEST_OBJECT_HANDLE,
                                   other_session_scope,
                                   &checked_resource));

    nmo_lua_handle_scope_invalidate(session_scope);
    ASSERT_EQ(NMO_ERR_INVALID_STATE,
              nmo_lua_handle_check(state,
                                   -1,
                                   &TEST_OBJECT_HANDLE,
                                   session_scope,
                                   &checked_resource));

    lua_pop(state, 1);
    nmo_lua_handle_scope_release(session_scope);
    nmo_lua_handle_scope_release(other_session_scope);
    lua_close(state);
}

TEST(lua_bindings, scalar_and_nullable_table_value_conversion_roundtrip) {
    lua_State *state = create_test_state();
    ASSERT_NOT_NULL(state);

    nmo_lua_value_t value = {
        .kind = NMO_LUA_VALUE_INTEGER,
        .as.integer_value = 123
    };
    nmo_lua_value_t out = {0};
    nmo_lua_value_t nil_out = {0};

    nmo_lua_value_push(state, &value);
    ASSERT_EQ(NMO_OK, nmo_lua_value_read(state, -1, &out));
    ASSERT_EQ(NMO_LUA_VALUE_INTEGER, out.kind);
    ASSERT_EQ(123, (int)out.as.integer_value);
    lua_pop(state, 1);

    lua_createtable(state, 0, 2);
    lua_pushinteger(state, 7);
    lua_setfield(state, -2, "count");
    lua_pushnil(state);
    lua_setfield(state, -2, "label");

    ASSERT_EQ(NMO_OK,
              nmo_lua_value_get_field(state,
                                      -1,
                                      "count",
                                      NMO_LUA_VALUE_INTEGER,
                                      false,
                                      &out));
    ASSERT_EQ(NMO_LUA_VALUE_INTEGER, out.kind);
    ASSERT_EQ(7, (int)out.as.integer_value);

    ASSERT_EQ(NMO_OK,
              nmo_lua_value_get_field(state,
                                      -1,
                                      "label",
                                      NMO_LUA_VALUE_STRING,
                                      true,
                                      &nil_out));
    ASSERT_EQ(NMO_LUA_VALUE_NIL, nil_out.kind);

    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT,
              nmo_lua_value_get_field(state,
                                      -1,
                                      "count",
                                      NMO_LUA_VALUE_STRING,
                                      false,
                                      &out));

    lua_pop(state, 1);
    lua_close(state);
}

TEST(lua_bindings, platform_modules_open_files_and_query_objects) {
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    ASSERT_EQ(NMO_OK,
              nmo_lua_runtime_execute_string(
                  runtime,
                  "local core = require('nmo.core')\n"
                  "local session = require('nmo.session')\n"
                  "local object = require('nmo.object')\n"
                  "assert(type(core.version()) == 'string')\n"
                  "local ctx = session.create_context()\n"
                  "local file = '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "'\n"
                  "local s = session.load_file(ctx, file)\n"
                  "assert(session.object_count(s) == 18)\n"
                  "local cam = session.find_object_by_name(s, 'InGameCam')\n"
                  "assert(cam ~= nil)\n"
                  "assert(object.id(cam) == 5)\n"
                  "assert(object.name(cam) == 'InGameCam')\n"
                  "assert(object.outgoing_edge_count(cam) == 2)\n"
                  "assert(object.incoming_edge_count(cam) == 1)\n"));

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings, runtime_module_wraps_preview_destroy) {
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    ASSERT_EQ(NMO_OK,
              nmo_lua_runtime_execute_string(
                  runtime,
                  "local session = require('nmo.session')\n"
                  "local object = require('nmo.object')\n"
                  "local runtime_mod = require('nmo.runtime')\n"
                  "local ctx = session.create_context()\n"
                  "local s = session.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
                  "local cam = session.find_object_by_name(s, 'InGameCam')\n"
                  "local ids = runtime_mod.preview_destroy_ids(s, { object.id(cam) })\n"
                  "assert(type(ids) == 'table')\n"
                  "assert(#ids >= 1)\n"
                  "assert(ids[1] == 5)\n"));

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings, type_module_exposes_stable_views_and_scalar_lookups) {
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    char float_guid[32];
    char script[2048];
    format_guid_literal(CKPGUID_FLOAT, float_guid, sizeof(float_guid));

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    snprintf(script,
             sizeof(script),
             "local session = require('nmo.session')\n"
             "local object = require('nmo.object')\n"
             "local type_mod = require('nmo.type')\n"
             "local ctx = session.create_context()\n"
             "local s = session.load_file(ctx, '%s')\n"
             "local cam = session.find_object_by_name(s, 'InGameCam')\n"
             "local float_view = type_mod.view_from_guid(ctx, '%s')\n"
             "assert(float_view.guid == '%s')\n"
             "assert(float_view.name ~= nil)\n"
             "assert(float_view.size == 4)\n"
             "assert(float_view.has_reflection == false)\n"
             "assert(type_mod.guid_from_name(ctx, float_view.name) == '%s')\n"
             "local cam_view = type_mod.view_from_object(cam)\n"
             "assert(cam_view.class_id == object.class_id(cam))\n"
             "assert(cam_view.has_reflection == true)\n"
             "assert(type_mod.class_id(ctx, cam_view.name) == cam_view.class_id)\n"
             "assert(type_mod.class_name(ctx, cam_view.class_id) == cam_view.name)\n",
             NMO_TEST_DATA_FILE("Ballance/Camera.nmo"),
             float_guid,
             float_guid,
             float_guid);
    ASSERT_EQ(NMO_OK, nmo_lua_runtime_execute_string(runtime, script));

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings, type_module_roundtrips_typed_values_by_guid) {
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    char float_guid[32];
    char int_guid[32];
    char script[1024];
    format_guid_literal(CKPGUID_FLOAT, float_guid, sizeof(float_guid));
    format_guid_literal(CKPGUID_INT, int_guid, sizeof(int_guid));

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    snprintf(script,
             sizeof(script),
             "local session = require('nmo.session')\n"
             "local type_mod = require('nmo.type')\n"
             "local ctx = session.create_context()\n"
             "assert(type_mod.value_roundtrip(ctx, '%s', '1.5') == '1.5')\n"
             "assert(type_mod.value_roundtrip(ctx, '%s', '42') == '42')\n",
             float_guid,
             int_guid);
    ASSERT_EQ(NMO_OK, nmo_lua_runtime_execute_string(runtime, script));

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings, type_module_exposes_direct_view_from_class_id) {
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    ASSERT_EQ(NMO_OK,
              nmo_lua_runtime_execute_string(
                  runtime,
                  "local session = require('nmo.session')\n"
                  "local object = require('nmo.object')\n"
                  "local type_mod = require('nmo.type')\n"
                  "local ctx = session.create_context()\n"
                  "local s = session.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
                  "local cam = session.find_object_by_name(s, 'InGameCam')\n"
                  "local from_object = type_mod.view_from_object(cam)\n"
                  "local from_class = type_mod.view_from_class_id(ctx, object.class_id(cam))\n"
                  "assert(from_class.class_id == from_object.class_id)\n"
                  "assert(from_class.name == from_object.name)\n"
                  "assert(from_class.has_reflection == from_object.has_reflection)\n"));

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings, behavior_and_format_modules_expose_stable_script_summaries) {
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    ASSERT_EQ(NMO_OK,
              nmo_lua_runtime_execute_string(
                  runtime,
                  "local session = require('nmo.session')\n"
                  "local behavior = require('nmo.behavior')\n"
                  "local format = require('nmo.format')\n"
                  "local ctx = session.create_context()\n"
                  "s = session.load_file(ctx, '" NMO_TEST_DATA_FILE("Nop.cmo") "')\n"));
    ASSERT_EQ(NMO_OK,
              nmo_lua_runtime_execute_string(
                  runtime,
                  "local behavior = require('nmo.behavior')\n"
                  "local count = behavior.script_count(s)\n"
                  "assert(count > 0)\n"
                  "local script = behavior.script_at(s, 1)\n"
                  "assert(script.script_id ~= nil)\n"
                  "assert(script.owner_id ~= nil)\n"
                  "assert(script.script_name ~= nil)\n"
                  "script_id = script.script_id\n"));
    ASSERT_EQ(NMO_OK,
              nmo_lua_runtime_execute_string(
                  runtime,
                  "local behavior = require('nmo.behavior')\n"
                  "local view = behavior.view(s, script_id)\n"
                  "assert(view.behavior_id == script_id)\n"
                  "assert(view.name ~= nil)\n"
                  "assert(view.edit_ready == true)\n"
                  "local boundary = behavior.describe_boundary(s, script_id)\n"
                  "assert(boundary.behavior_id == script_id)\n"));
    ASSERT_EQ(NMO_OK,
              nmo_lua_runtime_execute_string(
                  runtime,
                  "local format = require('nmo.format')\n"
                  "local iface = format.interface_view(s, script_id)\n"
                  "assert(iface.owner_behavior_id == script_id)\n"
                  "assert(iface.behavior_id ~= nil)\n"
                  "assert(iface.is_root == true)\n"));

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings, behavior_module_exposes_direct_script_from_id) {
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local session = require('nmo.session')\n"
        "local behavior = require('nmo.behavior')\n"
        "local ctx = session.create_context()\n"
        "local s = session.load_file(ctx, '" NMO_TEST_DATA_FILE("Nop.cmo") "')\n"
        "local first = behavior.script_at(s, 1)\n"
        "assert(first ~= nil)\n"
        "local by_id = behavior.script_from_id(s, first.script_id)\n"
        "assert(by_id ~= nil)\n"
        "assert(by_id.script_id ~= nil)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings, format_module_exposes_direct_find_behavior) {
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    char script[1024];
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    snprintf(script,
             sizeof(script),
             "local session = require('nmo.session')\n"
             "local behavior = require('nmo.behavior')\n"
             "local format = require('nmo.format')\n"
             "local ctx = session.create_context()\n"
             "local s = session.load_file(ctx, '%s')\n"
             "local script_view = behavior.script_at(s, 1)\n"
             "assert(script_view ~= nil)\n"
             "local nested = format.find_behavior(s, script_view.script_id, script_view.script_id)\n"
             "assert(nested ~= nil)\n"
             "assert(nested.owner_behavior_id == script_view.script_id)\n"
             "assert(nested.behavior_id ~= nil)\n"
             "assert(nested.is_root == true)\n",
             NMO_TEST_DATA_FILE("Nop.cmo"));
    assert_lua_ok(runtime, script);

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings, behavior_module_exposes_edit_transaction_entry_points) {
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    ASSERT_EQ(NMO_OK,
              nmo_lua_runtime_execute_string(
                  runtime,
                  "local session = require('nmo.session')\n"
                  "local behavior = require('nmo.behavior')\n"
                  "local ctx = session.create_context()\n"
                  "local s = session.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/base.cmo") "')\n"
                  "local tx = behavior.begin_edit(ctx, s, 'lua binding test')\n"
                  "assert(tx ~= nil)\n"
                  "local report = behavior.report(tx)\n"
                  "assert(report.created_objects == 0)\n"
                  "assert(report.errors == 0)\n"
                  "behavior.rollback(tx)\n"));

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings, modules_report_deterministic_argument_errors) {
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    ASSERT_EQ(NMO_OK,
              nmo_lua_runtime_execute_string(
                  runtime,
                  "local session = require('nmo.session')\n"
                  "local behavior = require('nmo.behavior')\n"
                  "local format = require('nmo.format')\n"
                  "local type_mod = require('nmo.type')\n"
                  "local ctx = session.create_context()\n"
                  "local ok, err = pcall(function()\n"
                  "  type_mod.view_from_guid(ctx, 'not-a-guid')\n"
                  "end)\n"
                  "assert(ok == false)\n"
                  "assert(string.find(err, 'GUID', 1, true) ~= nil)\n"
                  "ok, err = pcall(function()\n"
                  "  behavior.script_from_id(nil, 1)\n"
                  "end)\n"
                  "assert(ok == false)\n"
                  "assert(string.find(err, 'session', 1, true) ~= nil)\n"
                  "ok, err = pcall(function()\n"
                  "  format.find_behavior(nil, 1, 2)\n"
                  "end)\n"
                  "assert(ok == false)\n"
                  "assert(string.find(err, 'session', 1, true) ~= nil)\n"
                  "ok, err = pcall(function()\n"
                  "  behavior.script_at(session.load_file(ctx, '" NMO_TEST_DATA_FILE("Nop.cmo") "'), 0)\n"
                  "end)\n"
                  "assert(ok == false)\n"
                  "assert(string.find(err, 'script index must be 1-based', 1, true) ~= nil)\n"));

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings, app_module_exposes_structured_report_helpers) {
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    ASSERT_EQ(NMO_OK,
              nmo_lua_runtime_execute_string(
                  runtime,
                  "local session = require('nmo.session')\n"
                  "local app = require('nmo.app')\n"
                  "local s1 = session.load_file(session.create_context(), '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
                  "local s2 = session.load_file(session.create_context(), '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
                  "local cmp = app.comparison_stats(s1, s2)\n"
                  "assert(cmp.match == true)\n"
                  "assert(cmp.diff_count == 0)\n"
                  "assert(cmp.objects_compared == 18)\n"
                  "local diff = app.diff_stats(s1, s2)\n"
                  "assert(diff.changed_count == 0)\n"
                  "assert(diff.removed_count == 0)\n"
                  "assert(diff.added_count == 0)\n"
                  "assert(diff.identical_count > 0)\n"));

    ASSERT_EQ(NMO_OK,
              nmo_lua_runtime_execute_string(
                  runtime,
                  "local session = require('nmo.session')\n"
                  "local object = require('nmo.object')\n"
                  "local app = require('nmo.app')\n"
                  "local s = session.load_file(session.create_context(), '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
                  "local cam = session.find_object_by_name(s, 'InGameCam')\n"
                  "local stats = app.object_summary_stats(cam)\n"
                  "assert(stats.class_id == object.class_id(cam))\n"
                  "assert(stats.class_name ~= nil)\n"
                  "assert(stats.type_name ~= nil)\n"
                  "assert(stats.has_reflection == true)\n"
                  "assert(stats.total_fields > 0)\n"
                  "assert(stats.object_ref_fields > 0)\n"));

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings, owned_handle_releases_resource_on_gc);
    REGISTER_TEST(lua_bindings, borrowed_handle_rejects_cross_scope_and_stale_access);
    REGISTER_TEST(lua_bindings, scalar_and_nullable_table_value_conversion_roundtrip);
    REGISTER_TEST(lua_bindings, platform_modules_open_files_and_query_objects);
    REGISTER_TEST(lua_bindings, runtime_module_wraps_preview_destroy);
    REGISTER_TEST(lua_bindings, type_module_exposes_stable_views_and_scalar_lookups);
    REGISTER_TEST(lua_bindings, type_module_roundtrips_typed_values_by_guid);
    REGISTER_TEST(lua_bindings, type_module_exposes_direct_view_from_class_id);
    REGISTER_TEST(lua_bindings, behavior_and_format_modules_expose_stable_script_summaries);
    REGISTER_TEST(lua_bindings, behavior_module_exposes_direct_script_from_id);
    REGISTER_TEST(lua_bindings, format_module_exposes_direct_find_behavior);
    REGISTER_TEST(lua_bindings, behavior_module_exposes_edit_transaction_entry_points);
    REGISTER_TEST(lua_bindings, modules_report_deterministic_argument_errors);
    REGISTER_TEST(lua_bindings, app_module_exposes_structured_report_helpers);
TEST_MAIN_END()
