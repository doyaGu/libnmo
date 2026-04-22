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

TEST(lua_bindings_format, format_module_exposes_interface_views)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local session = require('nmo.session')\n"
        "local behavior = require('nmo.behavior')\n"
        "local format = require('nmo.format')\n"
        "local ctx = session.create_context()\n"
        "local s = session.load_file(ctx, '" NMO_TEST_DATA_FILE("Nop.cmo") "')\n"
        "local script_view = behavior.script_at(s, 1)\n"
        "assert(script_view ~= nil)\n"
        "local iface = format.interface_view(s, script_view.script_id)\n"
        "assert(iface ~= nil)\n"
        "assert(iface.owner_behavior_id == script_view.script_id)\n"
        "assert(iface.behavior_id ~= nil)\n"
        "assert(type(iface.is_root) == 'boolean')\n"
        "assert(iface.body ~= nil)\n"
        "assert(type(iface.body.has_body) == 'boolean')\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_format, format_module_exposes_find_behavior_and_errors)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    ASSERT_EQ(
        NMO_OK,
        nmo_lua_runtime_execute_string(
            runtime,
            "local session = require('nmo.session')\n"
            "local behavior = require('nmo.behavior')\n"
            "local format = require('nmo.format')\n"
            "local ctx = session.create_context()\n"
            "local s = session.load_file(ctx, '" NMO_TEST_DATA_FILE("Nop.cmo") "')\n"
            "local script_view = behavior.script_at(s, 1)\n"
            "assert(script_view ~= nil)\n"
            "local root = format.find_behavior(s, script_view.script_id, script_view.script_id)\n"
            "assert(root ~= nil)\n"
            "assert(root.is_root == true)\n"
            "assert(format.find_behavior(s, script_view.script_id, 999999) == nil)\n"
            "local ok, err = pcall(function()\n"
            "  format.find_behavior(nil, script_view.script_id, script_view.script_id)\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'session', 1, true) ~= nil)\n"));

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings_format,
                  format_module_exposes_interface_views);
    REGISTER_TEST(lua_bindings_format,
                  format_module_exposes_find_behavior_and_errors);
TEST_MAIN_END()
