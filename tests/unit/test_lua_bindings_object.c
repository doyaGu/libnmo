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

TEST(lua_bindings_object, object_module_exposes_iteration_and_edge_queries)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local session = require('nmo.session')\n"
        "local object = require('nmo.object')\n"
        "local ctx = session.create_context()\n"
        "local s = session.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
        "assert(object.count(s) == session.object_count(s))\n"
        "local first = object.at(s, 1)\n"
        "assert(first ~= nil)\n"
        "assert(object.id(first) ~= nil)\n"
        "assert(object.class_id(first) ~= 0)\n"
        "local camera = session.find_object_by_name(s, 'InGameCam')\n"
        "assert(camera ~= nil)\n"
        "assert(object.name(camera) == 'InGameCam')\n"
        "assert(object.outgoing_edge_count(camera) == 2)\n"
        "assert(object.incoming_edge_count(camera) == 1)\n"
        "assert(object.has_outgoing_edges(camera) == true)\n"
        "assert(object.has_incoming_edges(camera) == true)\n"
        "local outgoing = object.outgoing_edges(camera)\n"
        "assert(type(outgoing) == 'table')\n"
        "assert(#outgoing == 2)\n"
        "assert(type(outgoing[1].to) == 'number')\n"
        "assert(type(outgoing[1].kind) == 'number')\n"
        "local incoming = object.incoming_edges(camera)\n"
        "assert(#incoming == 1)\n"
        "local all_edges = object.all_edges(s)\n"
        "assert(#all_edges == object.total_edge_count(s))\n"
        "assert(object.broken_edge_count(s) == 0)\n");
}

TEST(lua_bindings_object, object_module_exposes_class_iteration_and_deterministic_errors)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    ASSERT_EQ(
        NMO_OK,
        nmo_lua_runtime_execute_string(
            runtime,
            "local session = require('nmo.session')\n"
            "local object = require('nmo.object')\n"
            "local ctx = session.create_context()\n"
            "local s = session.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
            "local cam = session.find_object_by_name(s, 'InGameCam')\n"
            "local class_id = object.class_id(cam)\n"
            "assert(object.count_class(s, class_id) >= 1)\n"
            "assert(object.at_class(s, class_id, 1) ~= nil)\n"
            "assert(object.at(s, 9999) == nil)\n"
            "assert(object.at_class(s, class_id, 9999) == nil)\n"
            "local ok, err = pcall(function()\n"
            "  object.at(s, 0)\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, '1-based', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  object.at_class(s, class_id, 0)\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, '1-based', 1, true) ~= nil)\n"));

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings_object,
                  object_module_exposes_iteration_and_edge_queries);
    REGISTER_TEST(lua_bindings_object,
                  object_module_exposes_class_iteration_and_deterministic_errors);
TEST_MAIN_END()
