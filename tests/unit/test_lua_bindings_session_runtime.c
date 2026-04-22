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

TEST(lua_bindings_session_runtime, session_module_exposes_full_tier1_workflow)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local session = require('nmo.session')\n"
        "local object = require('nmo.object')\n"
        "local ctx = session.create_context()\n"
        "local created = session.create(ctx)\n"
        "assert(created ~= nil)\n"
        "assert(session.object_count(created) == 0)\n"
        "local created_object = session.create_object(created, 1, 'Lua Created Object')\n"
        "assert(created_object ~= nil)\n"
        "assert(object.name(created_object) == 'Lua Created Object')\n"
        "assert(object.class_id(created_object) == 1)\n"
        "assert(session.object_count(created) == 1)\n"
        "local copied = session.copy_objects(created, { created_object })\n"
        "assert(copied == 1)\n"
        "assert(session.object_count(created) == 2)\n"
        "local copied_mixed = session.copy_objects(created, { created_object, object.id(created_object) }, 0)\n"
        "assert(copied_mixed == 2)\n"
        "assert(session.object_count(created) == 4)\n"
        "local copied_info = session.copy_objects_info(created, { object.id(session.find_object_by_name(created, 'Lua Created Object')) }, 0)\n"
        "assert(copied_info.copied_count == 1)\n"
        "assert(copied_info.affected_count >= copied_info.copied_count)\n"
        "assert(type(copied_info.manager_event_errors) == 'number')\n"
        "assert(type(copied_info.object_hook_errors) == 'number')\n"
        "assert(session.object_count(created) == 5)\n"
        "local deleted = session.destroy_objects(created, { created_object }, 0)\n"
        "assert(deleted == 1)\n"
        "local doomed = session.find_object_by_name(created, 'Lua Created Object')\n"
        "assert(doomed ~= nil)\n"
        "local destroy_info = session.destroy_objects_info(created, { doomed }, 0)\n"
        "assert(destroy_info.deleted_count == 1)\n"
        "assert(destroy_info.affected_count >= destroy_info.deleted_count)\n"
        "assert(type(destroy_info.manager_event_errors) == 'number')\n"
        "assert(type(destroy_info.object_hook_errors) == 'number')\n"
        "local ok, err = pcall(function() object.name(created_object) end)\n"
        "assert(ok == false)\n"
        "assert(string.find(err, 'stale', 1, true) ~= nil)\n"
        "local loaded = session.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
        "assert(session.object_count(loaded) == 18)\n"
        "assert(session.find_object_by_name(loaded, 'InGameCam') ~= nil)\n"
        "assert(session.find_object_by_name(loaded, 'MissingObject') == nil)\n"
        "session.save_file(loaded, 'test_lua_session_save_out.nmo')\n");

    (void)remove("test_lua_session_save_out.nmo");
    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_session_runtime, runtime_module_preview_destroy_validates_inputs)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local session = require('nmo.session')\n"
        "local object = require('nmo.object')\n"
        "local runtime_mod = require('nmo.runtime')\n"
        "local ctx = session.create_context()\n"
        "local s = session.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
        "local cam = session.find_object_by_name(s, 'InGameCam')\n"
        "assert(runtime_mod.request_flags.default == 0)\n"
        "assert(runtime_mod.request_flags.strict ~= nil)\n"
        "assert(runtime_mod.request_flags.cascade ~= nil)\n"
        "assert(runtime_mod.request_flags.safe_detach ~= nil)\n"
        "local preview_ids = runtime_mod.preview_destroy(s, { cam })\n"
        "assert(type(preview_ids) == 'table')\n"
        "assert(#preview_ids >= 1)\n"
        "local preview_id_mix = runtime_mod.preview_destroy(s, { object.id(cam) }, runtime_mod.request_flags.default)\n"
        "assert(type(preview_id_mix) == 'table')\n"
        "assert(#preview_id_mix >= 1)\n"
        "local preview_info = runtime_mod.preview_destroy_info(s, { cam }, runtime_mod.request_flags.cascade)\n"
        "assert(type(preview_info) == 'table')\n"
        "assert(type(preview_info.ids) == 'table')\n"
        "assert(preview_info.count == #preview_info.ids)\n"
        "assert(preview_info.count >= 1)\n"
        "assert(type(preview_info.ids[1]) == 'number')\n"
        "local ids = runtime_mod.preview_destroy_ids(s, { object.id(cam) })\n"
        "assert(type(ids) == 'table')\n"
        "assert(#ids >= 1)\n");

    ASSERT_EQ(
        NMO_OK,
        nmo_lua_runtime_execute_string(
            runtime,
            "local session = require('nmo.session')\n"
            "local object = require('nmo.object')\n"
            "local runtime_mod = require('nmo.runtime')\n"
            "local ctx = session.create_context()\n"
            "local s = session.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/Camera.nmo") "')\n"
            "local created = session.create(ctx)\n"
            "local created_object = session.create_object(created, 1, 'Temp')\n"
            "assert(session.destroy_objects(created, { created_object }) == 1)\n"
            "local ok, err = pcall(function()\n"
            "  runtime_mod.preview_destroy_ids(s, {})\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'at least one object id', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  runtime_mod.preview_destroy_ids(s, { 'bad' })\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'integers', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  runtime_mod.preview_destroy(s, {})\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'at least one object id', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  runtime_mod.preview_destroy(s, { s })\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'object ids', 1, true) ~= nil or string.find(err, 'object handle', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  runtime_mod.preview_destroy(created, { created_object })\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'stale', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  runtime_mod.preview_destroy(s, { object.id(session.find_object_by_name(s, 'InGameCam')) }, 'bad')\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'flags', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  session.create_object(created, 1, 'BadGuid', 'bad-guid')\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'guid', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  session.copy_objects(s, {})\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'at least one object id', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  session.copy_objects(s, { s })\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'object ids', 1, true) ~= nil or string.find(err, 'object handle', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  session.destroy_objects(s, { 'bad' })\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'integers', 1, true) ~= nil or string.find(err, 'object ids', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  session.copy_objects(s, { object.id(session.find_object_by_name(s, 'InGameCam')) }, 'bad')\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'flags', 1, true) ~= nil)\n"));

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings_session_runtime,
                  session_module_exposes_full_tier1_workflow);
    REGISTER_TEST(lua_bindings_session_runtime,
                  runtime_module_preview_destroy_validates_inputs);
TEST_MAIN_END()
