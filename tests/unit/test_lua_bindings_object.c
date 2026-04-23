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

TEST(lua_bindings_object, object_module_exposes_document_scoped_queries_and_edges)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local context = require('nmo.context')\n"
        "local document = require('nmo.document')\n"
        "local object = require('nmo.object')\n"
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
        "assert(object.count(doc) == 18)\n"
        "local first = object.at(doc, 1)\n"
        "assert(first ~= nil)\n"
        "local cam = object.find_object_by_name(doc, 'InGameCam')\n"
        "assert(cam ~= nil)\n"
        "assert(object.name(cam) == 'InGameCam')\n"
        "assert(object.outgoing_edge_count(cam) == 2)\n"
        "assert(object.incoming_edge_count(cam) == 1)\n"
        "assert(object.has_outgoing_edges(cam) == true)\n"
        "assert(object.has_incoming_edges(cam) == true)\n"
        "assert(type(object.outgoing_edges(cam)) == 'table')\n"
        "assert(type(object.incoming_edges(cam)) == 'table')\n"
        "assert(type(object.all_edges(doc)) == 'table')\n"
        "assert(type(object.total_edge_count(doc)) == 'number')\n"
        "assert(type(object.broken_edge_count(doc)) == 'number')\n"
        "local q = object.query_first(doc, { name = 'InGameCam', name_mode = 'exact' })\n"
        "assert(q ~= nil)\n"
        "local collected = object.query_collect(doc, { class_id = object.class_id(cam) })\n"
        "assert(type(collected) == 'table')\n"
        "local info = object.query_collect_info(doc, { class_id = object.class_id(cam) })\n"
        "assert(type(info) == 'table')\n"
        "assert(type(info.objects) == 'table')\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_object, object_module_exposes_workspace_scoped_mutations)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local context = require('nmo.context')\n"
        "local session = require('nmo.session')\n"
        "local workspace_mod = require('nmo.workspace')\n"
        "local object = require('nmo.object')\n"
        "local ctx = context.create()\n"
        "local advanced = session.create(ctx)\n"
        "local ws = workspace_mod.create(ctx, require('nmo.document').load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "'))\n"
        "assert(ws ~= nil)\n"
        "local created = object.create_object(ws, 1, 'Lua Created Object')\n"
        "assert(created ~= nil)\n"
        "assert(object.name(created) == 'Lua Created Object')\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_object, object_module_reports_deterministic_errors)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local context = require('nmo.context')\n"
        "local document = require('nmo.document')\n"
        "local workspace_mod = require('nmo.workspace')\n"
        "local object = require('nmo.object')\n"
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "local ok, err = pcall(function() object.at(doc, 0) end)\n"
        "assert(ok == false)\n"
        "assert(string.find(err, '1-based', 1, true) ~= nil)\n"
        "ok, err = pcall(function() object.query_collect(doc, 'bad') end)\n"
        "assert(ok == false)\n"
        "assert(string.find(err, 'query', 1, true) ~= nil)\n"
        "ok, err = pcall(function() object.create_object(doc, 1, 'Bad') end)\n"
        "assert(ok == false)\n"
        "assert(string.find(err, 'workspace', 1, true) ~= nil)\n"
        "ok, err = pcall(function() object.copy_objects(ws, {}) end)\n"
        "assert(ok == false)\n"
        "assert(type(err) == 'string')\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings_object,
                  object_module_exposes_document_scoped_queries_and_edges);
    REGISTER_TEST(lua_bindings_object,
                  object_module_exposes_workspace_scoped_mutations);
    REGISTER_TEST(lua_bindings_object,
                  object_module_reports_deterministic_errors);
TEST_MAIN_END()
