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

TEST(lua_public_workflow, canonical_lua_workflow_uses_context_document_workspace)
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
        "local session = require('nmo.session')\n"
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Nop.cmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "local script = behavior.script_at(doc, 1)\n"
        "assert(script ~= nil)\n"
        "assert(object.count(doc) > 0)\n"
        "assert(behavior.view(ws, script.script_id) ~= nil)\n"
        "assert(format.interface_view(doc, script.script_id) ~= nil)\n"
        "assert(behavior.graph.build(ws, script.script_id) ~= nil)\n"
        "assert(session.load_file == nil)\n"
        "assert(session.query_first == nil)\n"
        "assert(behavior.raw_graph == nil)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_public_workflow,
                  canonical_lua_workflow_uses_context_document_workspace);
TEST_MAIN_END()
