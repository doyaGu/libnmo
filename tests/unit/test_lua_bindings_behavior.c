#include "test_framework.h"

#include "behavior/nmo_script_view.h"
#include "core/nmo_guid.h"
#include "lua/nmo_lua_bindings.h"
#include "lua/nmo_lua_runtime.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "type/nmo_type_guids.h"
#include "behavior/nmo_script_walker.h"
#include "core/nmo_array.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_parameterin_schemas.h"

#include <stdio.h>

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
        if (message != NULL) {
            fprintf(stderr, "Lua failure: %s\n", message);
        }
    }
    ASSERT_EQ(NMO_OK, status);
}

typedef struct find_trace_pin_ctx {
    nmo_session_t *session;
    nmo_object_id_t found_direct;
} find_trace_pin_ctx_t;

static bool find_trace_pin_visitor(nmo_object_id_t behavior_id,
                                   const nmo_behavior_state_t *state,
                                   uint32_t depth,
                                   bool is_bb,
                                   void *user_data)
{
    find_trace_pin_ctx_t *ctx = (find_trace_pin_ctx_t *)user_data;
    nmo_object_repository_t *repo = NULL;
    const nmo_object_id_t *pin_ids = NULL;
    size_t i = 0u;

    (void)behavior_id;
    (void)depth;
    (void)is_bb;

    if (ctx == NULL || ctx->session == NULL || state == NULL) {
        return true;
    }

    repo = nmo_session_get_repository(ctx->session);
    pin_ids = (const nmo_object_id_t *)state->in_parameters.data;
    for (i = 0u; pin_ids != NULL && i < state->in_parameters.count; ++i) {
        nmo_object_t *obj = NULL;
        const nmo_parameterin_state_t *pin = NULL;

        if (pin_ids[i] == 0u) {
            continue;
        }
        obj = nmo_object_repository_find_by_id(repo, pin_ids[i]);
        if (obj == NULL) {
            continue;
        }
        pin = (const nmo_parameterin_state_t *)nmo_object_get_state(obj);
        if (pin != NULL && pin->source_id != 0u && !pin->is_shared) {
            ctx->found_direct = pin_ids[i];
            return false;
        }
    }

    return true;
}

static uint32_t find_traceable_parameter_id(const char *path)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_array_t scripts;
    find_trace_pin_ctx_t finder = {0};
    const nmo_script_entry_t *entries = NULL;
    size_t i = 0u;

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    if (ctx == NULL) {
        return 0u;
    }
    session = nmo_session_load(ctx, path);
    if (session == NULL) {
        nmo_context_release(ctx);
        return 0u;
    }

    nmo_array_init(&scripts, sizeof(nmo_script_entry_t), 16, NULL);
    if (nmo_script_walker_find_scripts(ctx, session, &scripts) != NMO_OK) {
        nmo_array_dispose(&scripts);
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 0u;
    }
    entries = (const nmo_script_entry_t *)scripts.data;
    finder.session = session;

    for (i = 0u; i < scripts.count; ++i) {
        nmo_status_t status = nmo_script_walker_walk(ctx,
                                                     session,
                                                     entries[i].script_id,
                                                     find_trace_pin_visitor,
                                                     &finder);
        if (status != NMO_OK) {
            finder.found_direct = 0u;
            break;
        }
        if (finder.found_direct != 0u) {
            break;
        }
    }

    nmo_array_dispose(&scripts);
    nmo_session_destroy(session);
    nmo_context_release(ctx);
    return finder.found_direct;
}

static void find_linkable_behavior_io_ids(const char *path,
                                          uint32_t *out_root_id,
                                          uint32_t *out_from_io_id,
                                          uint32_t *out_to_io_id)
{
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_script_view_t root_script = {0};
    nmo_object_t *root_object = NULL;
    nmo_behavior_state_t *root_state = NULL;

    ASSERT_NOT_NULL(out_root_id);
    ASSERT_NOT_NULL(out_from_io_id);
    ASSERT_NOT_NULL(out_to_io_id);
    *out_root_id = 0u;
    *out_from_io_id = 0u;
    *out_to_io_id = 0u;

    ctx = nmo_context_create(&(nmo_context_desc_t){ .data_dir = "data" });
    ASSERT_NOT_NULL(ctx);
    session = nmo_session_load(ctx, path);
    ASSERT_NOT_NULL(session);

    repo = nmo_session_get_repository(session);
    ASSERT_NOT_NULL(repo);
    ASSERT_EQ(NMO_OK, nmo_script_view_at(session, 0u, &root_script));
    root_object = nmo_object_repository_find_by_id(repo, root_script.script_id);
    ASSERT_NOT_NULL(root_object);
    root_state = (nmo_behavior_state_t *)nmo_object_get_state(root_object);
    ASSERT_NOT_NULL(root_state);

    *out_root_id = root_script.script_id;
    {
        const nmo_object_id_t *sub_ids = (const nmo_object_id_t *)root_state->sub_behaviors.data;
        for (size_t i = 0; sub_ids != NULL && i < root_state->sub_behaviors.count; ++i) {
            nmo_object_t *sub_object =
                nmo_object_repository_find_by_id(repo, sub_ids[i]);
            nmo_behavior_state_t *sub_state = sub_object
                ? (nmo_behavior_state_t *)nmo_object_get_state(sub_object)
                : NULL;
            const nmo_object_id_t *io_ids = NULL;

            if (sub_state == NULL || sub_state->outputs.count == 0u) {
                continue;
            }

            io_ids = (const nmo_object_id_t *)sub_state->outputs.data;
            *out_from_io_id = io_ids[0];
            break;
        }
        for (size_t i = 0; sub_ids != NULL && i < root_state->sub_behaviors.count; ++i) {
            nmo_object_t *sub_object =
                nmo_object_repository_find_by_id(repo, sub_ids[i]);
            nmo_behavior_state_t *sub_state = sub_object
                ? (nmo_behavior_state_t *)nmo_object_get_state(sub_object)
                : NULL;
            const nmo_object_id_t *io_ids = NULL;

            if (sub_state == NULL || sub_state->inputs.count == 0u) {
                continue;
            }

            io_ids = (const nmo_object_id_t *)sub_state->inputs.data;
            *out_to_io_id = io_ids[0];
            break;
        }
    }

    ASSERT_TRUE(*out_root_id != 0u);
    ASSERT_TRUE(*out_from_io_id != 0u);
    ASSERT_TRUE(*out_to_io_id != 0u);

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

TEST(lua_bindings_behavior, behavior_module_exposes_constants_and_transaction_lifecycle)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    assert_lua_ok(
        runtime,
        "local session = require('nmo.session')\n"
        "local behavior = require('nmo.behavior')\n"
        "local ctx = session.create_context()\n"
        "local s = session.load_file(ctx, '" NMO_TEST_DATA_FILE("Ballance/base.cmo") "')\n"
        "local script = behavior.script_at(s, 1)\n"
        "assert(script ~= nil)\n"
        "assert(behavior.validation_flags.references ~= nil)\n"
        "assert(behavior.operation_slot_flags.in1 ~= nil)\n"
        "local tx = behavior.begin_edit(ctx, s, 'lua behavior tx')\n"
        "assert(tx ~= nil)\n"
        "behavior.mark(tx, behavior.validation_flags.roundtrip_ready)\n"
        "local report = behavior.report(tx)\n"
        "assert(report.errors == 0)\n"
        "behavior.rollback(tx)\n"
        "local ok, err = pcall(function() behavior.report(tx) end)\n"
        "assert(ok == false)\n"
        "assert(string.find(err, 'stale', 1, true) ~= nil)\n");

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_behavior, behavior_module_exposes_full_write_surface)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    char float_guid[32];
    char script[4096];
    uint32_t root_id = 0u;
    uint32_t from_io_id = 0u;
    uint32_t to_io_id = 0u;
    ASSERT_NOT_NULL(runtime);

    format_guid_literal(CKPGUID_FLOAT, float_guid, sizeof(float_guid));
    find_linkable_behavior_io_ids(NMO_TEST_DATA_FILE("Ballance/base.cmo"),
                                  &root_id,
                                  &from_io_id,
                                  &to_io_id);
    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    snprintf(
        script,
        sizeof(script),
        "local session = require('nmo.session')\n"
        "local behavior = require('nmo.behavior')\n"
        "local ctx = session.create_context()\n"
        "local s = session.load_file(ctx, '%s')\n"
        "local root = behavior.script_at(s, 1).script_id\n"
        "assert(root == %u)\n"
        "local tx = behavior.begin_edit(ctx, s, 'lua write surface')\n"
        "local node = behavior.add_node(tx, root, '42414C07-10000007', 'Lua Test Node')\n"
        "assert(node ~= nil)\n"
        "local in_io = behavior.add_io(tx, root, 'input', 'Lua Test In')\n"
        "local out_io = behavior.add_io(tx, root, 'output', 'Lua Test Out')\n"
        "behavior.rename_io(tx, out_io, 'Lua Test Out Renamed')\n"
        "local source_p = behavior.add_parameter(tx, root, 'out', '%s', 'Lua Source')\n"
        "local target_p = behavior.add_parameter(tx, node, 'in', '%s', 'Lua Target')\n"
        "local p1 = behavior.add_parameter(tx, root, 'local', '%s', 'Lua P1')\n"
        "local p2 = behavior.add_parameter(tx, root, 'local', '%s', 'Lua P2')\n"
        "local p3 = behavior.add_parameter(tx, root, 'local', '%s', 'Lua P3')\n"
        "behavior.set_parameter_value(tx, p1, '1.5')\n"
        "behavior.set_parameter_bytes(tx, p2, string.char(0, 0, 128, 63))\n"
        "behavior.connect_parameter(tx, source_p, target_p)\n"
        "behavior.disconnect_parameter(tx, target_p)\n"
        "local link = behavior.add_link(tx, root, %u, %u, 0)\n"
        "assert(link ~= nil)\n"
        "behavior.rewire_link(tx, link, %u, %u)\n"
        "behavior.set_link_delay(tx, link, 3)\n"
        "local op = behavior.add_operation(tx, root, '33CC6B49-3589282B', p1, p2, p3)\n"
        "assert(op ~= nil)\n"
        "behavior.rewire_operation(tx, op, behavior.operation_slot_flags.in1 + behavior.operation_slot_flags.in2 + behavior.operation_slot_flags.out, p1, p2, p3)\n"
        "behavior.validate(tx)\n"
        "assert(behavior.report(tx).errors == 0)\n"
        "behavior.remove_link(tx, root, link)\n"
        "behavior.remove_operation(tx, op)\n"
        "behavior.remove_parameter(tx, target_p, true)\n"
        "behavior.remove_parameter(tx, p3, true)\n"
        "behavior.remove_io(tx, in_io, false)\n"
        "behavior.remove_node(tx, root, node, 0)\n"
        "behavior.commit(tx)\n"
        "local ok, err = pcall(function() behavior.validate(tx) end)\n"
        "assert(ok == false)\n"
        "assert(string.find(err, 'stale', 1, true) ~= nil)\n",
        NMO_TEST_DATA_FILE("Ballance/base.cmo"),
        root_id,
        float_guid,
        float_guid,
        float_guid,
        float_guid,
        float_guid,
        from_io_id,
        to_io_id,
        from_io_id,
        to_io_id);
    assert_lua_ok(runtime, script);

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_behavior, behavior_module_exposes_interface_policy_and_errors)
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
            "local ctx = session.create_context()\n"
            "local s = session.load_file(ctx, '" NMO_TEST_DATA_FILE("BBSamples/Collisions/Prevent Collision.cmo") "')\n"
            "local tx = behavior.begin_edit(ctx, s, 'lua interface policy')\n"
            "behavior.validate_interface_refs(tx, 253)\n"
            "behavior.apply_interface_policy(tx, 253, 'canonicalize')\n"
            "behavior.rollback(tx)\n"
            "local ok, err = pcall(function()\n"
            "  behavior.add_io(behavior.begin_edit(ctx, s, 'bad kind'), 253, 'sideways', 'oops')\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'Invalid io kind', 1, true) ~= nil)\n"
            "ok, err = pcall(function()\n"
            "  local tx2 = behavior.begin_edit(ctx, s, 'bad mode')\n"
            "  behavior.apply_interface_policy(tx2, 253, 'sideways')\n"
            "end)\n"
            "assert(ok == false)\n"
            "assert(string.find(err, 'Invalid interface mode', 1, true) ~= nil)\n"));

    nmo_lua_runtime_destroy(runtime);
}

TEST(lua_bindings_behavior, behavior_module_exposes_trace_views)
{
    nmo_lua_runtime_t *runtime = nmo_lua_runtime_create();
    char script[2048];
    uint32_t parameter_id =
        find_traceable_parameter_id(NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"));
    ASSERT_NOT_NULL(runtime);

    ASSERT_EQ(NMO_OK, nmo_lua_register_platform_bindings(runtime));
    snprintf(
        script,
        sizeof(script),
        "local session = require('nmo.session')\n"
        "local behavior = require('nmo.behavior')\n"
        "local s = session.load_file(session.create_context(), '%s')\n"
        "local root = behavior.script_at(s, 1).script_id\n"
        "local tree = behavior.script_tree(s, root, 1)\n"
        "assert(type(tree) == 'table')\n"
        "assert(#tree >= 1)\n"
        "assert(tree[1].behavior_id == root)\n"
        "assert(tree[1].depth == 0)\n"
        "local chain = behavior.trace_parameter_chain(s, %u, 8)\n"
        "assert(type(chain) == 'table')\n"
        "assert(#chain >= 1)\n"
        "assert(chain[1].id == %u)\n"
        "assert(chain[1].step_type == 'start')\n"
        "assert(chain[1].class_id == %u)\n"
        "local shallow = behavior.trace_parameter_chain(s, %u, 1)\n"
        "assert(#shallow <= 1)\n"
        "assert(behavior.script_tree(s, 0) == nil)\n"
        "assert(behavior.trace_parameter_chain(s, 0) == nil)\n",
        NMO_TEST_DATA_FILE("Ballance/Gameplay.nmo"),
        parameter_id,
        parameter_id,
        (uint32_t)NMO_CID_PARAMETERIN,
        parameter_id);
    assert_lua_ok(runtime, script);

    nmo_lua_runtime_destroy(runtime);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(lua_bindings_behavior,
                  behavior_module_exposes_constants_and_transaction_lifecycle);
    REGISTER_TEST(lua_bindings_behavior,
                  behavior_module_exposes_full_write_surface);
    REGISTER_TEST(lua_bindings_behavior,
                  behavior_module_exposes_interface_policy_and_errors);
    REGISTER_TEST(lua_bindings_behavior,
                  behavior_module_exposes_trace_views);
TEST_MAIN_END()
