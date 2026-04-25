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

TEST(lua_bindings_plan, plan_module_builds_edit_plan)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local plan = require('nmo.plan')\n"
        "local p = plan.new()\n"
        "assert(plan.count(p) == 0)\n"
        "plan.add_io(p, 3, 'input', 'Lua Plan In')\n"
        "assert(plan.count(p) == 1)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_plan, plan_module_executes_dry_run)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local context = require('nmo.context')\n"
        "local document = require('nmo.document')\n"
        "local workspace_mod = require('nmo.workspace')\n"
        "local behavior = require('nmo.behavior')\n"
        "local plan = require('nmo.plan')\n"
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Nop.cmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "local script = behavior.script_at(doc, 1)\n"
        "local p = plan.new()\n"
        "plan.add_io(p, script.script_id, 'input', 'Lua Plan Execute In')\n"
        "local report = plan.execute(p, ws, { dry_run = true })\n"
        "assert(report.ok == true)\n"
        "assert(report.dry_run == true)\n"
        "assert(report.operation_count == 1)\n"
        "assert(#report.operations == 1)\n"
        "assert(report.operations[1].op == 'add_io')\n"
        "assert(report.operations[1].handles[1].name == 'io')\n"
        "assert(#report.created_objects == 1)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings_plan, plan_module_builds_edit_plan);
    REGISTER_TEST(lua_bindings_plan, plan_module_executes_dry_run);
TEST_MAIN_END()
