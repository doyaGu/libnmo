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

TEST(lua_bindings_behavior, behavior_module_uses_document_and_workspace_handles)
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
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Nop.cmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "assert(behavior.script_count(doc) > 0)\n"
        "local script = behavior.script_at(doc, 1)\n"
        "assert(script ~= nil)\n"
        "local by_id = behavior.script_from_id(doc, script.script_id)\n"
        "assert(by_id ~= nil)\n"
        "local view = behavior.view(ws, script.script_id)\n"
        "assert(view ~= nil)\n"
        "assert(view.behavior_id == script.script_id)\n"
        "local inspect = behavior.inspect(ws, script.script_id, 1, 1)\n"
        "assert(type(inspect) == 'table')\n"
        "assert(type(inspect.view) == 'table')\n"
        "assert(type(inspect.boundary) == 'table')\n"
        "local boundary = behavior.describe_boundary(ws, script.script_id, 1)\n"
        "assert(type(boundary) == 'table')\n"
        "local tree = behavior.script_tree(ws, script.script_id, 1)\n"
        "assert(type(tree) == 'table')\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_behavior, behavior_module_exposes_nested_graph_helpers_and_no_raw_graph)
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
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/base.cmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "assert(type(behavior.graph) == 'table')\n"
        "assert(behavior.raw_graph == nil)\n"
        "local graph = behavior.graph.build(ws, 237)\n"
        "assert(type(graph) == 'table')\n"
        "assert(type(graph.nodes) == 'table')\n"
        "assert(#graph.nodes > 0)\n"
        "local owner = behavior.graph.find_owner(ws, 237, graph.nodes[1].object_id)\n"
        "assert(owner == nil or type(owner.object_id) == 'number')\n"
        "local outgoing = behavior.graph.outgoing_control(ws, 237, graph.nodes[1].owner_behavior_id)\n"
        "assert(outgoing == nil or type(outgoing) == 'table')\n"
        "assert(type(behavior.graph.validate_operation(ws, 237, { kind = 'validate' })) == 'boolean')\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_behavior, begin_edit_requires_workspace_and_returns_tx)
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
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/base.cmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "local tx = behavior.begin_edit(ws, 'lua behavior tx')\n"
        "assert(tx ~= nil)\n"
        "local report = behavior.report(tx)\n"
        "assert(type(report.operations) == 'table')\n"
        "assert(#report.operations == 0)\n"
        "behavior.rollback(tx)\n"
        "local ok, err = pcall(function() behavior.begin_edit(doc, 'bad') end)\n"
        "assert(ok == false)\n"
        "assert(string.find(err, 'workspace', 1, true) ~= nil)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_behavior, behavior_edit_reports_pending_plan_and_commit_schema_v2)
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
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/base.cmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "local root = assert(behavior.script_at(doc, 1)).script_id\n"
        "local before = behavior.view(ws, root).input_count\n"
        "local tx = behavior.begin_edit(ws, 'lua behavior plan tx')\n"
        "local io = assert(behavior.add_io(tx, root, 'input', 'Lua Pending In'))\n"
        "assert(type(io) == 'table')\n"
        "assert(io.operation == 1)\n"
        "assert(io.handle == 'io')\n"
        "assert(behavior.view(ws, root).input_count == before)\n"
        "local pending = behavior.report(tx)\n"
        "assert(type(pending.operations) == 'table')\n"
        "assert(#pending.operations == 1)\n"
        "assert(pending.pending == true)\n"
        "assert(pending.operations[1].index == 1)\n"
        "assert(pending.operations[1].op == 'add_io')\n"
        "assert(pending.operations[1].kind == 'add_io')\n"
        "assert(pending.operations[1].status_name == 'Success')\n"
        "assert(type(pending.operations[1].handles) == 'table')\n"
        "assert(type(pending.created_objects) == 'table')\n"
        "assert(#pending.created_objects == 0)\n"
        "assert(pending.validation.final_status == 0)\n"
        "assert(pending.diff.created_object_count == 0)\n"
        "assert(pending.diff.semantic_risk_count == #pending.semantic_risks)\n"
        "local committed = behavior.commit(tx)\n"
        "assert(committed.ok == true)\n"
        "assert(committed.dry_run == false)\n"
        "assert(type(committed.operations) == 'table')\n"
        "assert(#committed.operations == 1)\n"
        "assert(committed.operations[1].index == 1)\n"
        "assert(committed.operations[1].op == 'add_io')\n"
        "assert(committed.operations[1].kind == 'add_io')\n"
        "assert(committed.operations[1].status_name == 'Success')\n"
        "assert(committed.operations[1].handles[1].name == 'io')\n"
        "assert(committed.operations[1].handles[1].object_id ~= nil)\n"
        "assert(type(committed.created_objects) == 'table')\n"
        "assert(#committed.created_objects >= 1)\n"
        "assert(committed.created_objects[1].object_id ~= nil)\n"
        "assert(committed.diff.created_object_count >= 1)\n"
        "assert(#committed.semantic_risks == committed.diff.semantic_risk_count)\n"
        "assert(behavior.view(ws, root).input_count == before + 1)\n"
        "local after = behavior.report(tx)\n"
        "assert(after.ok == true)\n"
        "assert(#after.operations == 1)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_behavior, behavior_edit_rollback_discards_pending_plan)
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
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/base.cmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "local root = assert(behavior.script_at(doc, 1)).script_id\n"
        "local before = behavior.view(ws, root).input_count\n"
        "local tx = behavior.begin_edit(ws, 'lua behavior rollback tx')\n"
        "assert(behavior.add_io(tx, root, 'input', 'Lua Rollback In'))\n"
        "assert(#behavior.report(tx).operations == 1)\n"
        "behavior.rollback(tx)\n"
        "assert(behavior.view(ws, root).input_count == before)\n"
        "local ok, err = pcall(function() behavior.report(tx) end)\n"
        "assert(ok == false)\n"
        "assert(string.find(err, 'stale', 1, true) ~= nil)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_behavior, behavior_edit_chains_pending_parameter_handles)
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
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Nop.cmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "local root = assert(behavior.script_at(doc, 1)).script_id\n"
        "local tx = behavior.begin_edit(ws, 'lua behavior chained handles')\n"
        "local p1 = assert(behavior.add_parameter(tx, root, 'local', '47884C3F-432C2C20', 'Lua Param A'))\n"
        "local p2 = assert(behavior.add_parameter(tx, root, 'local', '47884C3F-432C2C20', 'Lua Param B'))\n"
        "local p3 = assert(behavior.add_parameter(tx, root, 'local', '47884C3F-432C2C20', 'Lua Param Out'))\n"
        "assert(type(p1) == 'table' and p1.handle == 'parameter')\n"
        "behavior.set_parameter_value(tx, p1, '1.5')\n"
        "behavior.set_parameter_bytes(tx, p2, string.char(0, 0, 128, 63))\n"
        "local op = assert(behavior.add_operation(tx, root, '33CC6B49-3589282B', p1, p2, p3))\n"
        "assert(type(op) == 'table' and op.handle == 'operation')\n"
        "local pending = behavior.report(tx)\n"
        "assert(#pending.operations == 6)\n"
        "local report = behavior.commit(tx)\n"
        "assert(report.ok == true)\n"
        "assert(#report.operations == 6)\n"
        "assert(report.operations[6].op == 'add_operation')\n"
        "assert(report.operations[6].kind == 'add_operation')\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_behavior, behavior_execute_returns_edit_report_schema_v2)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local behavior = require('nmo.behavior')\n"
        "local report = behavior.execute('" NMO_TEST_DATA_FILE("Nop.cmo") "', nil, { dry_run = true }, function(ctx, session, runtime, tx)\n"
        "  local behavior = require('nmo.behavior')\n"
        "  assert(behavior.add_io(tx, 6, 'input', 'Lua Execute In'))\n"
        "end)\n"
        "assert(report.ok == true)\n"
        "assert(report.dry_run == true)\n"
        "assert(report.operation_count == nil)\n"
        "assert(type(report.operations) == 'table')\n"
        "assert(#report.operations == 1)\n"
        "assert(report.operations[1].index == 1)\n"
        "assert(report.operations[1].op == 'add_io')\n"
        "assert(report.operations[1].kind == 'add_io')\n"
        "assert(report.operations[1].status_name == 'Success')\n"
        "assert(report.operations[1].handles[1].object_id ~= nil)\n"
        "assert(type(report.created_objects) == 'table')\n"
        "assert(report.created_objects[1].object_id ~= nil)\n"
        "assert(report.diff.created_object_count == #report.created_objects)\n"
        "assert(type(report.semantic_risks) == 'table')\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_behavior, behavior_add_node_accepts_manager_entry_policy)
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
        "local ctx = context.create()\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Nop.cmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "local tx = behavior.begin_edit(ws, 'lua behavior add node policy')\n"
        "assert(behavior.add_node(tx, 6, 'A20E8D5B-DF002150', 'Lua Behavior Send Message', { manager_entry = { policy = 'create_missing', manager = 'message' } }))\n"
        "local report = behavior.report(tx)\n"
        "assert(report.pending == true)\n"
        "assert(#report.operations == 1)\n"
        "assert(report.operations[1].op == 'add_node')\n"
        "behavior.rollback(tx)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_behavior, behavior_set_parameter_value_accepts_manager_entry_policy)
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
        "local ctx = context.create({ data_dir = '" NMO_TEST_DATA_DIR "' })\n"
        "local doc = document.load_file(ctx, '" NMO_TEST_DATA_FILE("Nop.cmo") "')\n"
        "local ws = workspace_mod.create(ctx, doc)\n"
        "local script = behavior.script_at(doc, 1)\n"
        "local tx = behavior.begin_edit(ws, 'lua behavior message value policy')\n"
        "assert(behavior.add_node(tx, script.script_id, 'A20E8D5B-DF002150', 'Lua Behavior Send Message', { manager_entry = { policy = 'create_missing', manager = 'message' } }))\n"
        "behavior.set_parameter_value(tx, { operation = 1, handle = 'input_param:Message' }, 'LuaBehaviorCreatedMessage', { manager_entry = { policy = 'create_missing', manager = 'message' } })\n"
        "local report = behavior.commit(tx)\n"
        "assert(report.ok == true)\n"
        "assert(report.dry_run == false)\n"
        "assert(#report.operations == 2)\n"
        "assert(report.operations[2].op == 'set_parameter_value')\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_behavior, behavior_execute_rollback_discards_queued_plan)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local behavior = require('nmo.behavior')\n"
        "local report = behavior.execute('" NMO_TEST_DATA_FILE("Nop.cmo") "', nil, { dry_run = true }, function(ctx, session, runtime, tx)\n"
        "  local behavior = require('nmo.behavior')\n"
        "  assert(behavior.add_io(tx, 6, 'input', 'Lua Execute Rollback In'))\n"
        "  behavior.rollback(tx)\n"
        "end)\n"
        "assert(report.ok == true)\n"
        "assert(report.dry_run == true)\n"
        "assert(type(report.operations) == 'table')\n"
        "assert(#report.operations == 0)\n"
        "assert(type(report.created_objects) == 'table')\n"
        "assert(#report.created_objects == 0)\n"
        "assert(report.diff.created_object_count == 0)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings_behavior,
                  behavior_module_uses_document_and_workspace_handles);
    REGISTER_TEST(lua_bindings_behavior,
                  behavior_module_exposes_nested_graph_helpers_and_no_raw_graph);
    REGISTER_TEST(lua_bindings_behavior,
                  begin_edit_requires_workspace_and_returns_tx);
    REGISTER_TEST(lua_bindings_behavior,
                  behavior_edit_reports_pending_plan_and_commit_schema_v2);
    REGISTER_TEST(lua_bindings_behavior,
                  behavior_edit_rollback_discards_pending_plan);
    REGISTER_TEST(lua_bindings_behavior,
                  behavior_edit_chains_pending_parameter_handles);
    REGISTER_TEST(lua_bindings_behavior,
                  behavior_execute_returns_edit_report_schema_v2);
    REGISTER_TEST(lua_bindings_behavior,
                  behavior_add_node_accepts_manager_entry_policy);
    REGISTER_TEST(lua_bindings_behavior,
                  behavior_set_parameter_value_accepts_manager_entry_policy);
    REGISTER_TEST(lua_bindings_behavior,
                  behavior_execute_rollback_discards_queued_plan);
TEST_MAIN_END()
