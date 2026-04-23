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

TEST(lua_bindings, platform_modules_register_canonical_surface)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "assert(require('nmo.core') ~= nil)\n"
        "assert(require('nmo.context') ~= nil)\n"
        "assert(require('nmo.document') ~= nil)\n"
        "assert(require('nmo.workspace') ~= nil)\n"
        "assert(require('nmo.object') ~= nil)\n"
        "assert(require('nmo.behavior') ~= nil)\n"
        "assert(require('nmo.session') ~= nil)\n"
        "assert(require('nmo.runtime') ~= nil)\n"
        "assert(require('nmo.type') ~= nil)\n"
        "assert(require('nmo.format') ~= nil)\n"
        "local ok = pcall(function() return require('nmo.app') end)\n"
        "assert(ok == false)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings, canonical_workflow_roundtrips_context_document_workspace_object)
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
        "local behavior = require('nmo.behavior')\n"
        "local format = require('nmo.format')\n"
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Nop.cmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "local script = behavior.script_at(doc, 1)\n"
        "local iface = format.interface_view(doc, script.script_id)\n"
        "assert(iface ~= nil)\n"
        "local graph = behavior.graph.build(ws, script.script_id)\n"
        "assert(graph ~= nil)\n"
        "assert(object.count(doc) >= 1)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings, platform_modules_register_canonical_surface);
    REGISTER_TEST(lua_bindings, canonical_workflow_roundtrips_context_document_workspace_object);
TEST_MAIN_END()
