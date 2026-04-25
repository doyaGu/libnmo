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
        "plan.add_node(p, 3, '055B29FE-662D5CA0', 'Lua Plan 2D Text')\n"
        "plan.add_io(p, 3, 'input', 'Lua Plan In')\n"
        "assert(plan.count(p) == 2)\n"
        "plan.rename_io(p, 42, 'Lua Renamed IO')\n"
        "plan.remove_io(p, 42, true)\n"
        "plan.remove_node(p, 3, 43, 0)\n"
        "plan.add_behavior_link(p, 6, 5, 2, 3)\n"
        "plan.rewire_behavior_link(p, 75, 78, 25)\n"
        "plan.set_behavior_link_delay(p, 75, 4)\n"
        "plan.remove_behavior_link(p, 6, 75)\n"
        "plan.add_parameter(p, 6, 'local', '5A5716FD-44E276D7', 'Lua Local Int')\n"
        "plan.connect_parameter(p, 7, 8)\n"
        "plan.disconnect_parameter(p, 8)\n"
        "plan.remove_parameter(p, 18, false)\n"
        "plan.interface_policy(p, 3, 'canonicalize')\n"
        "plan.set_parameter_value(p, 5, '1.25')\n"
        "plan.set_parameter_bytes(p, 64, string.char(0x2A, 0, 0, 0))\n"
        "plan.set_data_cell(p, 2261, 0, 1, '0.75')\n"
        "assert(plan.count(p) == 17)\n");

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
        "assert(#report.created_objects == 1)\n"
        "assert(report.validation.final_status == 0)\n"
        "assert(report.diff.changed_object_count == 1)\n"
        "assert(report.diff.created_object_count == 1)\n"
        "assert(report.diff.deleted_object_count == 0)\n"
        "assert(type(report.semantic_risks) == 'table')\n"
        "assert(#report.semantic_risks == report.diff.semantic_risk_count)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_plan, plan_module_executes_rename_io_dry_run)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local context = require('nmo.context')\n"
        "local document = require('nmo.document')\n"
        "local workspace_mod = require('nmo.workspace')\n"
        "local plan = require('nmo.plan')\n"
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "local p = plan.new()\n"
        "plan.rename_io(p, 2, 'Lua Plan Start')\n"
        "local report = plan.execute(p, ws, { dry_run = true })\n"
        "assert(report.ok == true)\n"
        "assert(report.dry_run == true)\n"
        "assert(report.operation_count == 1)\n"
        "assert(report.operations[1].op == 'rename_io')\n"
        "assert(report.operations[1].primary_id == 2)\n"
        "assert(#report.changed_objects == 1)\n"
        "assert(report.changed_objects[1].object_id == 2)\n"
        "assert(report.diff.changed_object_count == 1)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_plan, plan_module_executes_behavior_link_dry_run)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local context = require('nmo.context')\n"
        "local document = require('nmo.document')\n"
        "local workspace_mod = require('nmo.workspace')\n"
        "local plan = require('nmo.plan')\n"
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Nop.cmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "local p = plan.new()\n"
        "plan.add_behavior_link(p, 6, 5, 2, 3)\n"
        "local report = plan.execute(p, ws, { dry_run = true })\n"
        "assert(report.ok == true)\n"
        "assert(report.dry_run == true)\n"
        "assert(report.operation_count == 1)\n"
        "assert(report.operations[1].op == 'add_behavior_link')\n"
        "assert(report.operations[1].primary_id == 6)\n"
        "assert(report.operations[1].result_id ~= 0)\n"
        "assert(#report.created_objects == 1)\n"
        "assert(report.diff.created_object_count == 1)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_plan, plan_module_executes_add_parameter_dry_run)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local context = require('nmo.context')\n"
        "local document = require('nmo.document')\n"
        "local workspace_mod = require('nmo.workspace')\n"
        "local plan = require('nmo.plan')\n"
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Nop.cmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "local p = plan.new()\n"
        "plan.add_parameter(p, 6, 'local', '5A5716FD-44E276D7', 'Lua Local Int')\n"
        "local report = plan.execute(p, ws, { dry_run = true })\n"
        "assert(report.ok == true)\n"
        "assert(report.dry_run == true)\n"
        "assert(report.operation_count == 1)\n"
        "assert(report.operations[1].op == 'add_parameter')\n"
        "assert(report.operations[1].primary_id == 6)\n"
        "assert(report.operations[1].result_id ~= 0)\n"
        "assert(#report.created_objects == 1)\n"
        "assert(report.diff.created_object_count == 1)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_plan, plan_module_executes_parameter_value_dry_run)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local context = require('nmo.context')\n"
        "local document = require('nmo.document')\n"
        "local workspace_mod = require('nmo.workspace')\n"
        "local plan = require('nmo.plan')\n"
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/MenuLevel.nmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "local p = plan.new()\n"
        "plan.set_parameter_value(p, 5, '1.25')\n"
        "local report = plan.execute(p, ws, { dry_run = true })\n"
        "assert(report.ok == true)\n"
        "assert(report.dry_run == true)\n"
        "assert(report.operation_count == 1)\n"
        "assert(report.operations[1].op == 'set_parameter_value')\n"
        "assert(report.operations[1].primary_id == 5)\n"
        "assert(#report.changed_objects == 1)\n"
        "assert(report.changed_objects[1].object_id == 5)\n"
        "assert(report.diff.changed_object_count == 1)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings_plan, plan_module_builds_edit_plan);
    REGISTER_TEST(lua_bindings_plan, plan_module_executes_dry_run);
    REGISTER_TEST(lua_bindings_plan, plan_module_executes_rename_io_dry_run);
    REGISTER_TEST(lua_bindings_plan, plan_module_executes_behavior_link_dry_run);
    REGISTER_TEST(lua_bindings_plan, plan_module_executes_add_parameter_dry_run);
    REGISTER_TEST(lua_bindings_plan, plan_module_executes_parameter_value_dry_run);
TEST_MAIN_END()
