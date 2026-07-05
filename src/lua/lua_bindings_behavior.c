#include "lua_bindings_internal.h"

#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_behavior_execute.h"
#include "behavior/nmo_behavior_query.h"
#include "behavior/nmo_behavior_view.h"
#include "behavior/nmo_script_edit_graph.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"

#include "lauxlib.h"

#include <stdlib.h>
#include <string.h>

typedef struct nmo_lua_dump_buffer {
    char *data;
    size_t size;
    size_t capacity;
} nmo_lua_dump_buffer_t;

typedef struct nmo_lua_behavior_execute_action {
    char *dumped_chunk;
    size_t dumped_size;
    nmo_edit_report_t *edit_report;
    bool dry_run;
    uint32_t validation_flags;
} nmo_lua_behavior_execute_action_t;

typedef struct nmo_lua_behavior_workspace_scope {
    nmo_document_t *document;
    nmo_workspace_t *workspace;
} nmo_lua_behavior_workspace_scope_t;

static void nmo_lua_behavior_push_script_view(lua_State *state,
                                              const nmo_behavior_script_view_t *view)
{
    lua_createtable(state, 0, 5);

    nmo_lua_set_integer_field(state, "script_id", (lua_Integer)view->script_id);
    nmo_lua_set_integer_field(state, "owner_id", (lua_Integer)view->owner_id);
    nmo_lua_set_optional_string_field(state, "script_name", view->script_name);
    nmo_lua_set_optional_string_field(state, "owner_name", view->owner_name);
    nmo_lua_set_integer_field(
        state, "owner_class_id", (lua_Integer)view->owner_class_id);
}

static nmo_status_t nmo_lua_behavior_query_count_scripts(
    nmo_document_t *document,
    size_t *out_count)
{
    return document != NULL && out_count != NULL
        ? nmo_behavior_query_count_scripts(document, out_count)
        : NMO_ERR_INVALID_ARGUMENT;
}

static nmo_status_t nmo_lua_behavior_query_script_at(
    nmo_document_t *document,
    size_t index,
    nmo_behavior_script_view_t *out_view)
{
    return document != NULL && out_view != NULL
        ? nmo_behavior_query_script_at(document, index, out_view)
        : NMO_ERR_INVALID_ARGUMENT;
}

static nmo_status_t nmo_lua_behavior_query_script_from_id(
    nmo_document_t *document,
    nmo_object_id_t script_id,
    nmo_behavior_script_view_t *out_view)
{
    return document != NULL && out_view != NULL
        ? nmo_behavior_query_script_from_script_id(document, script_id, out_view)
        : NMO_ERR_INVALID_ARGUMENT;
}

static nmo_session_t *nmo_lua_behavior_execution_session(
    nmo_behavior_execution_t *execution)
{
    nmo_workspace_t *workspace =
        nmo_behavior_execution_workspace(execution);
    return workspace != NULL ? nmo_workspace_internal_session(workspace) : NULL;
}

static void nmo_lua_behavior_push_view(lua_State *state,
                                       const nmo_behavior_view_t *view)
{
    lua_createtable(state, 0, 22);

    nmo_lua_set_integer_field(state, "behavior_id", (lua_Integer)view->behavior_id);
    nmo_lua_set_integer_field(state, "class_id", (lua_Integer)view->class_id);
    nmo_lua_set_optional_string_field(state, "name", view->name);
    nmo_lua_set_integer_field(state, "flags", (lua_Integer)view->flags);
    nmo_lua_set_boolean_field(state, "is_building_block", view->is_building_block);
    nmo_lua_set_boolean_field(
        state, "has_target_parameter", view->has_target_parameter);
    nmo_lua_set_integer_field(
        state, "target_parameter_id", (lua_Integer)view->target_parameter_id);
    nmo_lua_set_integer_field(
        state, "sub_behavior_count", (lua_Integer)view->sub_behavior_count);
    nmo_lua_set_integer_field(state, "link_count", (lua_Integer)view->link_count);
    nmo_lua_set_integer_field(
        state, "operation_count", (lua_Integer)view->operation_count);
    nmo_lua_set_integer_field(state, "input_count", (lua_Integer)view->input_count);
    nmo_lua_set_integer_field(state, "output_count", (lua_Integer)view->output_count);
    nmo_lua_set_integer_field(
        state, "in_parameter_count", (lua_Integer)view->in_parameter_count);
    nmo_lua_set_integer_field(
        state, "out_parameter_count", (lua_Integer)view->out_parameter_count);
    nmo_lua_set_integer_field(
        state, "local_parameter_count", (lua_Integer)view->local_parameter_count);
    nmo_lua_set_boolean_field(
        state, "owner_index_available", view->owner_index_available);
    nmo_lua_set_boolean_field(state, "edit_ready", view->edit_ready);
    nmo_lua_set_integer_field(
        state, "edit_graph_status", (lua_Integer)view->edit_graph_status);
    nmo_lua_set_boolean_field(state, "has_interface", view->has_interface);
    nmo_lua_set_boolean_field(
        state, "interface_available", view->interface_available);
    nmo_lua_set_integer_field(
        state, "interface_status", (lua_Integer)view->interface_status);
    if (view->interface_available) {
        nmo_lua_push_interface_view(state, &view->interface_view);
    } else {
        lua_pushnil(state);
    }
    lua_setfield(state, -2, "interface");
}

static void nmo_lua_behavior_push_boundary_view(
    lua_State *state,
    const nmo_behavior_boundary_view_t *view)
{
    lua_createtable(state, 0, 8);

    nmo_lua_set_integer_field(state, "behavior_id", (lua_Integer)view->behavior_id);
    nmo_lua_set_integer_field(
        state, "internal_node_count", (lua_Integer)view->internal_node_count);
    nmo_lua_set_integer_field(
        state, "control_in_count", (lua_Integer)view->control_in_count);
    nmo_lua_set_integer_field(
        state, "control_out_count", (lua_Integer)view->control_out_count);
    nmo_lua_set_integer_field(
        state, "parameter_in_count", (lua_Integer)view->parameter_in_count);
    nmo_lua_set_integer_field(
        state, "parameter_out_count", (lua_Integer)view->parameter_out_count);
    nmo_lua_set_integer_field(state, "broken_links", (lua_Integer)view->broken_links);
    nmo_lua_set_integer_field(state, "missing_nodes", (lua_Integer)view->missing_nodes);
}

static void nmo_lua_behavior_push_guid_string(lua_State *state, nmo_guid_t guid)
{
    char guid_buffer[32];
    if (nmo_guid_is_null(guid)) {
        lua_pushnil(state);
        return;
    }

    if (nmo_guid_format(guid, guid_buffer, sizeof(guid_buffer)) <= 0) {
        lua_pushnil(state);
        return;
    }

    lua_pushstring(state, guid_buffer);
}

static void nmo_lua_behavior_push_graph_endpoint(
    lua_State *state,
    const nmo_script_edit_endpoint_t *endpoint)
{
    lua_createtable(state, 0, 4);
    nmo_lua_set_integer_field(state, "object_id", (lua_Integer)endpoint->object_id);
    nmo_lua_set_integer_field(
        state, "owner_behavior_id", (lua_Integer)endpoint->owner_behavior_id);
    nmo_lua_set_integer_field(
        state, "owner_index", (lua_Integer)endpoint->owner_index);
    nmo_lua_set_integer_field(state, "kind", (lua_Integer)endpoint->kind);
}


static void nmo_lua_behavior_push_graph_node(
    lua_State *state,
    const nmo_script_edit_node_t *node)
{
    lua_createtable(state, 0, 10);
    nmo_lua_set_integer_field(state, "object_id", (lua_Integer)node->object_id);
    nmo_lua_set_integer_field(state, "kind", (lua_Integer)node->kind);
    nmo_lua_set_optional_string_field(state, "name", node->name);
    nmo_lua_set_integer_field(state, "class_id", (lua_Integer)node->class_id);
    nmo_lua_set_optional_string_field(state, "class_name", node->class_name);
    nmo_lua_set_integer_field(state, "depth", (lua_Integer)node->depth);
    nmo_lua_set_integer_field(
        state, "parent_behavior_id", (lua_Integer)node->parent_behavior_id);
    nmo_lua_set_integer_field(
        state, "owner_behavior_id", (lua_Integer)node->owner_behavior_id);
    nmo_lua_set_integer_field(
        state, "owner_slot_index", (lua_Integer)node->owner_slot_index);
    nmo_lua_set_integer_field(
        state, "owner_slot_kind", (lua_Integer)node->owner_slot_kind);
}

static void nmo_lua_behavior_push_graph_control_edge(
    lua_State *state,
    const nmo_script_edit_control_edge_t *edge)
{
    lua_createtable(state, 0, 5);
    nmo_lua_set_integer_field(state, "link_id", (lua_Integer)edge->link_id);
    nmo_lua_behavior_push_graph_endpoint(state, &edge->source);
    lua_setfield(state, -2, "source");
    nmo_lua_behavior_push_graph_endpoint(state, &edge->target);
    lua_setfield(state, -2, "target");
    nmo_lua_set_integer_field(
        state, "activation_delay", (lua_Integer)edge->activation_delay);
    nmo_lua_set_integer_field(
        state,
        "initial_activation_delay",
        (lua_Integer)edge->initial_activation_delay);
}

static void nmo_lua_behavior_push_graph_data_edge(
    lua_State *state,
    const nmo_script_edit_data_edge_t *edge)
{
    lua_createtable(state, 0, 6);
    nmo_lua_set_integer_field(
        state, "source_parameter_id", (lua_Integer)edge->source_parameter_id);
    nmo_lua_set_integer_field(
        state, "target_parameter_id", (lua_Integer)edge->target_parameter_id);
    nmo_lua_set_integer_field(
        state, "source_owner_id", (lua_Integer)edge->source_owner_id);
    nmo_lua_set_integer_field(
        state, "target_owner_id", (lua_Integer)edge->target_owner_id);
    nmo_lua_behavior_push_guid_string(state, edge->type_guid);
    lua_setfield(state, -2, "type_guid");
    nmo_lua_set_boolean_field(state, "shared", edge->shared);
}

static void nmo_lua_behavior_push_ref_edge(lua_State *state,
                                           const nmo_ref_edge_t *edge)
{
    lua_createtable(state, 0, 5);
    nmo_lua_set_integer_field(state, "from", (lua_Integer)edge->from);
    nmo_lua_set_integer_field(state, "to", (lua_Integer)edge->to);
    nmo_lua_set_integer_field(state, "kind", (lua_Integer)edge->kind);
    nmo_lua_set_optional_string_field(state, "field_path", edge->field_path);
    nmo_lua_set_integer_field(state, "index", (lua_Integer)edge->index);
}

static void nmo_lua_behavior_push_graph_node_array(
    lua_State *state,
    const nmo_script_edit_node_t *nodes,
    size_t count)
{
    size_t i = 0u;
    lua_createtable(state, (int)count, 0);
    for (i = 0u; i < count; ++i) {
        nmo_lua_behavior_push_graph_node(state, &nodes[i]);
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_behavior_push_graph_control_edge_array(
    lua_State *state,
    const nmo_script_edit_control_edge_t *edges,
    size_t count)
{
    size_t i = 0u;
    lua_createtable(state, (int)count, 0);
    for (i = 0u; i < count; ++i) {
        nmo_lua_behavior_push_graph_control_edge(state, &edges[i]);
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_behavior_push_graph_data_edge_array(
    lua_State *state,
    const nmo_script_edit_data_edge_t *edges,
    size_t count)
{
    size_t i = 0u;
    lua_createtable(state, (int)count, 0);
    for (i = 0u; i < count; ++i) {
        nmo_lua_behavior_push_graph_data_edge(state, &edges[i]);
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_behavior_push_ref_edge_array(lua_State *state,
                                                 const nmo_ref_edge_t *edges,
                                                 size_t count)
{
    size_t i = 0u;
    lua_createtable(state, (int)count, 0);
    for (i = 0u; i < count; ++i) {
        nmo_lua_behavior_push_ref_edge(state, &edges[i]);
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}


static nmo_status_t nmo_lua_behavior_build_graph_from_args(
    lua_State *state,
    int workspace_index,
    int root_index,
    int depth_index,
    nmo_script_edit_graph_t **out_graph)
{
    nmo_workspace_t *workspace = NULL;
    nmo_object_id_t root_behavior_id = 0u;
    uint32_t max_depth = UINT32_MAX;
    nmo_status_t status =
        nmo_lua_check_workspace_handle(state, workspace_index, &workspace, NULL, NULL);
    if (status != NMO_OK) {
        return status;
    }

    root_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, root_index);
    if (root_behavior_id == 0u) {
        return NMO_ERR_NOT_FOUND;
    }
    if (!lua_isnoneornil(state, depth_index)) {
        max_depth = (uint32_t)luaL_checkinteger(state, depth_index);
    }

    return nmo_script_edit_graph_build(workspace,
                                       root_behavior_id,
                                       max_depth,
                                       out_graph);
}

static const char *nmo_lua_behavior_step_kind_name(
    nmo_behavior_trace_step_kind_t kind)
{
    switch (kind) {
        case NMO_BEHAVIOR_TRACE_STEP_KIND_SHARED_SOURCE:
            return "shared_source";
        case NMO_BEHAVIOR_TRACE_STEP_KIND_DIRECT_SOURCE:
            return "direct_source";
        case NMO_BEHAVIOR_TRACE_STEP_KIND_START:
        default:
            return "start";
    }
}

static void nmo_lua_behavior_push_trace_chain_view(
    lua_State *state,
    const nmo_behavior_trace_chain_view_t *view)
{
    size_t i = 0u;

    lua_createtable(state, (int)view->step_count, 0);
    for (i = 0u; i < view->step_count; ++i) {
        lua_createtable(state, 0, 4);
        nmo_lua_set_integer_field(state, "id", (lua_Integer)view->steps[i].id);
        lua_pushstring(state,
                       nmo_lua_behavior_step_kind_name(view->steps[i].step_kind));
        lua_setfield(state, -2, "step_type");
        nmo_lua_set_integer_field(
            state, "owner_id", (lua_Integer)view->steps[i].owner_id);
        nmo_lua_set_integer_field(
            state, "class_id", (lua_Integer)view->steps[i].class_id);
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_behavior_push_script_tree_view(
    lua_State *state,
    const nmo_behavior_tree_view_t *view)
{
    size_t i = 0u;

    lua_createtable(state, (int)view->node_count, 0);
    for (i = 0u; i < view->node_count; ++i) {
        lua_createtable(state, 0, 5);
        nmo_lua_set_integer_field(
            state, "behavior_id", (lua_Integer)view->nodes[i].behavior_id);
        nmo_lua_set_integer_field(
            state, "depth", (lua_Integer)view->nodes[i].depth);
        nmo_lua_set_boolean_field(
            state, "is_building_block", view->nodes[i].is_building_block);
        nmo_lua_set_optional_string_field(state, "name", view->nodes[i].name);
        nmo_lua_set_integer_field(
            state, "class_id", (lua_Integer)view->nodes[i].class_id);
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_behavior_reset_empty_edit_report(
    nmo_edit_report_t *report,
    bool dry_run)
{
    if (report == NULL) {
        return;
    }
    nmo_edit_report_dispose(report);
    (void)nmo_edit_report_init(report);
    report->ok = true;
    report->dry_run = dry_run;
    report->status = NMO_OK;
    report->validation.final_status = NMO_OK;
}

static void nmo_lua_behavior_move_edit_report(
    nmo_edit_report_t *dst,
    nmo_edit_report_t *src)
{
    if (dst == NULL || src == NULL) {
        return;
    }
    nmo_edit_report_dispose(dst);
    *dst = *src;
    memset(src, 0, sizeof(*src));
}

static int nmo_lua_behavior_push_pending_handle(
    lua_State *state,
    const nmo_edit_plan_t *plan,
    nmo_edit_op_kind_t kind)
{
    const char *handle_name = nmo_lua_edit_op_result_handle_name(kind);
    if (handle_name == NULL) {
        return 0;
    }
    lua_createtable(state, 0, 2);
    nmo_lua_set_integer_field(
        state, "operation", (lua_Integer)nmo_edit_plan_count(plan));
    nmo_lua_set_string_field(state, "handle", handle_name);
    return 1;
}

static int nmo_lua_behavior_traceback(lua_State *state)
{
    const char *message = lua_tostring(state, 1);
    if (message == NULL) {
        if (!lua_isnoneornil(state, 1) && luaL_callmeta(state, 1, "__tostring")) {
            message = lua_tostring(state, -1);
        } else {
            lua_pushliteral(state, "(error object is not a string)");
            message = lua_tostring(state, -1);
        }
    }

    luaL_traceback(state, state, message, 1);
    return 1;
}

static int nmo_lua_behavior_dump_writer(lua_State *state,
                                        const void *chunk,
                                        size_t size,
                                        void *user_data)
{
    nmo_lua_dump_buffer_t *buffer = (nmo_lua_dump_buffer_t *)user_data;
    size_t required = 0u;
    char *new_data = NULL;

    (void)state;

    if (buffer == NULL || (chunk == NULL && size != 0u)) {
        return 1;
    }

    required = buffer->size + size;
    if (required > buffer->capacity) {
        size_t new_capacity = buffer->capacity == 0u ? 256u : buffer->capacity;
        while (new_capacity < required) {
            new_capacity *= 2u;
        }

        new_data = (char *)realloc(buffer->data, new_capacity);
        if (new_data == NULL) {
            return 1;
        }
        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }

    if (size != 0u) {
        memcpy(buffer->data + buffer->size, chunk, size);
        buffer->size += size;
    }
    return 0;
}

static void nmo_lua_behavior_dump_buffer_clear(nmo_lua_dump_buffer_t *buffer)
{
    if (buffer != NULL) {
        free(buffer->data);
        buffer->data = NULL;
        buffer->size = 0u;
        buffer->capacity = 0u;
    }
}

static nmo_status_t nmo_lua_behavior_execute_lua_callback(
    nmo_behavior_execution_t *executor,
    const nmo_lua_behavior_execute_action_t *action)
{
    lua_State *state = NULL;
    int traceback_index = 0;
    nmo_context_t *context = NULL;
    nmo_session_t *session = NULL;
    nmo_lua_runtime_t *runtime = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_lua_handle_scope_t *context_scope = NULL;
    nmo_lua_handle_scope_t *session_scope = NULL;
    nmo_lua_handle_scope_t *runtime_scope = NULL;
    nmo_lua_handle_scope_t *tx_scope = NULL;
    nmo_lua_script_edit_tx_handle_data_t tx_data = {0};
    nmo_edit_plan_t *callback_plan = NULL;
    nmo_status_t status = NMO_OK;

    if (executor == NULL || action == NULL || action->dumped_chunk == NULL ||
        action->dumped_size == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Lua executor callback requires dumped Lua bytecode");
    }

    state = nmo_lua_runtime_state(nmo_behavior_execution_lua_runtime(executor));
    if (state == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Behavior execution has no active Lua state");
    }
    context = nmo_behavior_execution_context(executor);
    session = nmo_lua_behavior_execution_session(executor);
    runtime = nmo_behavior_execution_lua_runtime(executor);
    workspace = nmo_behavior_execution_workspace(executor);
    tx = nmo_behavior_execution_transaction(executor);
    if (context == NULL || session == NULL || runtime == NULL ||
        workspace == NULL || tx == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Script executor callback is missing active state");
    }

    lua_settop(state, 0);
    lua_pushcfunction(state, nmo_lua_behavior_traceback);
    traceback_index = lua_gettop(state);

    if (luaL_loadbuffer(state,
                        action->dumped_chunk,
                        action->dumped_size,
                        "behavior.execute") != LUA_OK) {
        const char *message = lua_tostring(state, -1);
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Lua executor callback load failed: %s",
                         message != NULL ? message : "unknown load error");
    }

    context_scope = nmo_lua_handle_scope_create();
    session_scope = nmo_lua_handle_scope_create();
    runtime_scope = nmo_lua_handle_scope_create();
    tx_scope = nmo_lua_handle_scope_create();
    if (context_scope == NULL || session_scope == NULL || runtime_scope == NULL ||
        tx_scope == NULL) {
        lua_settop(state, 0);
        status = NMO_ERR_NOMEM;
        goto cleanup;
    }

    status = nmo_lua_push_borrowed_handle(state,
                                          &NMO_LUA_CONTEXT_HANDLE_DESCRIPTOR,
                                          context,
                                          context_scope,
                                          NULL);
    if (status != NMO_OK) {
        lua_settop(state, 0);
        goto cleanup;
    }
    status = nmo_lua_push_borrowed_handle(state,
                                          &NMO_LUA_SESSION_HANDLE_DESCRIPTOR,
                                          session,
                                          session_scope,
                                          context_scope);
    if (status != NMO_OK) {
        lua_settop(state, 0);
        goto cleanup;
    }
    status = nmo_lua_push_borrowed_handle(state,
                                          &NMO_LUA_RUNTIME_HANDLE_DESCRIPTOR,
                                          runtime,
                                          runtime_scope,
                                          session_scope);
    if (status != NMO_OK) {
        lua_settop(state, 0);
        goto cleanup;
    }
    status = nmo_edit_plan_create(&callback_plan);
    if (status != NMO_OK) {
        lua_settop(state, 0);
        goto cleanup;
    }
    tx_data.tx = tx;
    tx_data.workspace = workspace;
    tx_data.plan = callback_plan;
    tx_data.finished = false;
    tx_data.dry_run_report = action->dry_run;
    tx_data.validation_flags = action->validation_flags;
    status = nmo_lua_push_borrowed_handle(state,
                                          &NMO_LUA_SCRIPT_EDIT_TX_HANDLE_DESCRIPTOR,
                                          &tx_data,
                                          tx_scope,
                                          session_scope);
    if (status != NMO_OK) {
        lua_settop(state, 0);
        goto cleanup;
    }

    if (lua_pcall(state, 4, 0, traceback_index) != LUA_OK) {
        const char *message = lua_tostring(state, -1);
        lua_settop(state, 0);
        status = NMO_ERR_VALIDATION_FAILED;
        NMO_SET_LAST_ERROR(status, NMO_SEVERITY_ERROR,
                           "Lua executor callback failed: %s",
                           message != NULL ? message : "unknown execution error");
        goto cleanup;
    }

    lua_settop(state, 0);
    if (tx_data.executed) {
        if (action->edit_report != NULL) {
            if (tx_data.has_report) {
                nmo_lua_behavior_move_edit_report(
                    action->edit_report, &tx_data.report);
                tx_data.has_report = false;
            } else {
                nmo_lua_behavior_reset_empty_edit_report(
                    action->edit_report, action->dry_run);
            }
        }
        status = NMO_OK;
    } else {
        nmo_edit_executor_options_t edit_options =
            nmo_edit_executor_options_default();
        edit_options.dry_run = action->dry_run;
        edit_options.validation_flags = action->validation_flags;
        status = nmo_edit_executor_execute_transaction(
            tx, callback_plan, &edit_options, action->edit_report);
    }

cleanup:
    if (tx_data.has_report) {
        nmo_edit_report_dispose(&tx_data.report);
        tx_data.has_report = false;
    }
    if (callback_plan != NULL && callback_plan == tx_data.plan) {
        nmo_edit_plan_destroy(callback_plan);
    }
    if (tx_scope != NULL) {
        nmo_lua_handle_scope_invalidate(tx_scope);
        nmo_lua_handle_scope_release(tx_scope);
    }
    if (runtime_scope != NULL) {
        nmo_lua_handle_scope_invalidate(runtime_scope);
        nmo_lua_handle_scope_release(runtime_scope);
    }
    if (session_scope != NULL) {
        nmo_lua_handle_scope_invalidate(session_scope);
        nmo_lua_handle_scope_release(session_scope);
    }
    if (context_scope != NULL) {
        nmo_lua_handle_scope_invalidate(context_scope);
        nmo_lua_handle_scope_release(context_scope);
    }
    return status;
}

static nmo_status_t nmo_lua_behavior_execute_action_fn(nmo_behavior_execution_t *executor,
                                                       void *user_data)
{
    return nmo_lua_behavior_execute_lua_callback(
        executor, (const nmo_lua_behavior_execute_action_t *)user_data);
}

static nmo_status_t nmo_lua_behavior_parse_guid_arg(lua_State *state,
                                                    int index,
                                                    nmo_guid_t *out_guid)
{
    const char *guid_text = luaL_checkstring(state, index);
    nmo_guid_t guid = nmo_guid_parse(guid_text);
    if (nmo_guid_is_null(guid)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid GUID string '%s'", guid_text);
    }
    *out_guid = guid;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_lua_behavior_parse_io_kind(lua_State *state,
                                                   int index,
                                                   nmo_script_edit_io_kind_t *out_kind)
{
    const char *kind_text = luaL_checkstring(state, index);
    if (strcmp(kind_text, "input") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_IO_INPUT;
        NMO_RETURN_OK();
    }
    if (strcmp(kind_text, "output") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_IO_OUTPUT;
        NMO_RETURN_OK();
    }
    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                     "Invalid io kind '%s'", kind_text);
}

static nmo_status_t nmo_lua_behavior_parse_parameter_kind(
    lua_State *state,
    int index,
    nmo_script_edit_parameter_kind_t *out_kind)
{
    const char *kind_text = luaL_checkstring(state, index);
    if (strcmp(kind_text, "in") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_IN;
        NMO_RETURN_OK();
    }
    if (strcmp(kind_text, "out") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_OUT;
        NMO_RETURN_OK();
    }
    if (strcmp(kind_text, "local") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_LOCAL;
        NMO_RETURN_OK();
    }
    if (strcmp(kind_text, "shared") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_SHARED;
        NMO_RETURN_OK();
    }
    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                     "Invalid parameter kind '%s'", kind_text);
}

static nmo_status_t nmo_lua_behavior_parse_interface_mode(
    lua_State *state,
    int index,
    nmo_script_edit_interface_mode_t *out_mode)
{
    const char *mode_text = luaL_checkstring(state, index);
    if (strcmp(mode_text, "preserve") == 0) {
        *out_mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE;
        NMO_RETURN_OK();
    }
    if (strcmp(mode_text, "canonicalize") == 0) {
        *out_mode = NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE;
        NMO_RETURN_OK();
    }
    if (strcmp(mode_text, "remove") == 0) {
        *out_mode = NMO_SCRIPT_EDIT_INTERFACE_REMOVE;
        NMO_RETURN_OK();
    }
    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                     "Invalid interface mode '%s'", mode_text);
}

static uint32_t nmo_lua_behavior_optional_flags(lua_State *state,
                                                int index,
                                                uint32_t default_value)
{
    if (lua_isnoneornil(state, index)) {
        return default_value;
    }
    return (uint32_t)luaL_checkinteger(state, index);
}

static nmo_object_id_t nmo_lua_behavior_optional_object_id(lua_State *state,
                                                           int index)
{
    if (lua_isnoneornil(state, index)) {
        return 0u;
    }
    return (nmo_object_id_t)luaL_checkinteger(state, index);
}

typedef struct nmo_lua_behavior_pending_ref {
    nmo_object_id_t id;
    size_t operation_index;
    const char *handle_name;
    bool has_ref;
} nmo_lua_behavior_pending_ref_t;

static nmo_status_t nmo_lua_behavior_parse_object_ref(
    lua_State *state,
    int index,
    bool optional,
    nmo_lua_behavior_pending_ref_t *out_ref)
{
    if (out_ref == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    memset(out_ref, 0, sizeof(*out_ref));
    if (optional && lua_isnoneornil(state, index)) {
        NMO_RETURN_OK();
    }
    if (lua_istable(state, index)) {
        lua_getfield(state, index, "operation");
        lua_Integer operation = luaL_checkinteger(state, -1);
        lua_pop(state, 1);
        lua_getfield(state, index, "handle");
        const char *handle = luaL_checkstring(state, -1);
        lua_pop(state, 1);
        if (operation <= 0 || handle == NULL || handle[0] == '\0') {
            return NMO_ERR_INVALID_ARGUMENT;
        }
        out_ref->operation_index = (size_t)(operation - 1);
        out_ref->handle_name = handle;
        out_ref->has_ref = true;
        NMO_RETURN_OK();
    }
    out_ref->id = (nmo_object_id_t)luaL_checkinteger(state, index);
    NMO_RETURN_OK();
}

static int nmo_lua_behavior_parse_manager_entry_policy(
    lua_State *state,
    int index,
    nmo_manager_entry_policy_t *out_policy)
{
    const char *policy = luaL_checkstring(state, index);
    if (strcmp(policy, "require_existing") == 0) {
        *out_policy = NMO_MANAGER_ENTRY_POLICY_REQUIRE_EXISTING;
        return 0;
    }
    if (strcmp(policy, "create_missing") == 0) {
        *out_policy = NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING;
        return 0;
    }
    return luaL_error(
        state,
        "manager_entry.policy must be 'require_existing' or 'create_missing'");
}

static int nmo_lua_behavior_check_manager_entry_fields(lua_State *state,
                                                       int index)
{
    static const char *const allowed[] = {
        "policy", "schema", "manager_guid", "key", "create", NULL
    };
    index = lua_absindex(state, index);
    lua_pushnil(state);
    while (lua_next(state, index) != 0) {
        const char *key = lua_tostring(state, -2);
        bool known = false;
        for (size_t i = 0; allowed[i] != NULL; ++i) {
            if (key != NULL && strcmp(key, allowed[i]) == 0) {
                known = true;
                break;
            }
        }
        if (!known) {
            return luaL_error(state,
                              "Unknown manager_entry field '%s'",
                              key != NULL ? key : "(non-string)");
        }
        lua_pop(state, 1);
    }
    return 0;
}

static int nmo_lua_behavior_parse_manager_entry_options(
    lua_State *state,
    int index,
    nmo_manager_entry_options_t *out_options)
{
    if (out_options == NULL) {
        return luaL_error(state, "invalid manager entry options output");
    }
    *out_options = nmo_manager_entry_options_default();
    if (lua_isnoneornil(state, index)) {
        return 0;
    }
    luaL_checktype(state, index, LUA_TTABLE);
    int field_rc = nmo_lua_behavior_check_manager_entry_fields(state, index);
    if (field_rc != 0) {
        return field_rc;
    }
    lua_getfield(state, index, "policy");
    if (!lua_isnil(state, -1)) {
        int rc = nmo_lua_behavior_parse_manager_entry_policy(
            state, lua_gettop(state), &out_options->policy);
        if (rc != 0) {
            return rc;
        }
    }
    lua_pop(state, 1);
    lua_getfield(state, index, "schema");
    if (!lua_isnil(state, -1)) {
        const char *schema = luaL_checkstring(state, -1);
        if (strcmp(schema, "auto") == 0) {
            out_options->schema = NMO_MANAGER_ENTRY_SCHEMA_AUTO;
        } else if (strcmp(schema, "message") == 0) {
            out_options->schema = NMO_MANAGER_ENTRY_SCHEMA_MESSAGE;
        } else if (strcmp(schema, "attribute") == 0) {
            out_options->schema = NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE;
        } else {
            return luaL_error(
                state,
                "manager_entry.schema must be 'auto', 'message', or 'attribute'");
        }
    }
    lua_pop(state, 1);
    lua_getfield(state, index, "manager_guid");
    if (!lua_isnil(state, -1)) {
        out_options->manager_guid = nmo_guid_parse(luaL_checkstring(state, -1));
        if (nmo_guid_is_null(out_options->manager_guid)) {
            return luaL_error(state, "manager_entry.manager_guid must be a GUID");
        }
    }
    lua_pop(state, 1);
    lua_getfield(state, index, "key");
    if (!lua_isnil(state, -1)) {
        out_options->key = luaL_checkstring(state, -1);
    }
    lua_pop(state, 1);
    lua_getfield(state, index, "create");
    if (!lua_isnil(state, -1)) {
        luaL_checktype(state, -1, LUA_TTABLE);
        out_options->create.enabled = true;
        lua_getfield(state, -1, "attribute_type_guid");
        if (!lua_isnil(state, -1)) {
            out_options->create.attribute_type_guid =
                nmo_guid_parse(luaL_checkstring(state, -1));
            if (nmo_guid_is_null(out_options->create.attribute_type_guid)) {
                return luaL_error(
                    state,
                    "manager_entry.create.attribute_type_guid must be a GUID");
            }
        }
        lua_pop(state, 1);
        lua_getfield(state, -1, "category");
        if (!lua_isnil(state, -1)) {
            out_options->create.category = luaL_checkstring(state, -1);
        }
        lua_pop(state, 1);
        lua_getfield(state, -1, "compatible_class_id");
        if (!lua_isnil(state, -1)) {
            out_options->create.has_compatible_class_id = true;
            out_options->create.compatible_class_id =
                (uint32_t)luaL_checkinteger(state, -1);
        }
        lua_pop(state, 1);
        lua_getfield(state, -1, "flags");
        if (!lua_isnil(state, -1)) {
            out_options->create.has_flags = true;
            out_options->create.flags = (uint32_t)luaL_checkinteger(state, -1);
        }
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
    return 0;
}

static int nmo_lua_behavior_parse_parameter_write_options(
    lua_State *state,
    int index,
    nmo_parameter_write_options_t *out_options,
    bool *out_has_options)
{
    if (out_options == NULL || out_has_options == NULL) {
        return luaL_error(state, "invalid parameter write options output");
    }
    memset(out_options, 0, sizeof(*out_options));
    out_options->manager_entry = nmo_manager_entry_options_default();
    *out_has_options = false;
    if (lua_isnoneornil(state, index)) {
        return 0;
    }
    luaL_checktype(state, index, LUA_TTABLE);
    *out_has_options = true;

    lua_getfield(state, index, "resize");
    if (!lua_isnil(state, -1)) {
        out_options->resize = lua_toboolean(state, -1) != 0;
    }
    lua_pop(state, 1);

    lua_getfield(state, index, "manager_entry");
    if (!lua_isnil(state, -1)) {
        int rc = nmo_lua_behavior_parse_manager_entry_options(
            state, lua_gettop(state), &out_options->manager_entry);
        if (rc != 0) {
            return rc;
        }
    }
    lua_pop(state, 1);
    return 0;
}

static int nmo_lua_behavior_invalidate_tx_handle(lua_State *state,
                                                 int index,
                                                 nmo_lua_script_edit_tx_handle_data_t *handle)
{
    nmo_lua_handle_scope_t *tx_scope = NULL;
    nmo_status_t status = nmo_lua_handle_get_scope(state,
                                                   index,
                                                   &NMO_LUA_SCRIPT_EDIT_TX_HANDLE_DESCRIPTOR,
                                                   &tx_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle scope");
    }

    handle->tx = NULL;
    handle->finished = true;
    nmo_lua_handle_scope_invalidate(tx_scope);
    return 0;
}

static nmo_status_t nmo_lua_behavior_check_active_edit_handle(
    lua_State *state,
    int index,
    nmo_lua_script_edit_tx_handle_data_t **out_handle)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_status_t status =
        nmo_lua_check_script_edit_tx_handle(state, index, &handle);
    if (status != NMO_OK) {
        return status;
    }
    if (handle->executed || handle->plan == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua script edit transaction handle is closed");
    }
    if (out_handle != NULL) {
        *out_handle = handle;
    }
    NMO_RETURN_OK();
}

static int nmo_lua_behavior_script_count(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    size_t count = 0;
    status = nmo_lua_behavior_query_count_scripts(document, &count);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to count scripts");
    }

    lua_pushinteger(state, (lua_Integer)count);
    return 1;
}

static int nmo_lua_behavior_script_at(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    lua_Integer lua_index = luaL_checkinteger(state, 2);
    if (lua_index < 1) {
        return luaL_error(state, "script index must be 1-based");
    }

    nmo_behavior_script_view_t view = {0};
    status = nmo_lua_behavior_query_script_at(document, (size_t)(lua_index - 1), &view);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to inspect script");
    }

    nmo_lua_behavior_push_script_view(state, &view);
    return 1;
}

static int nmo_lua_behavior_script_from_id(lua_State *state)
{
    nmo_document_t *document = NULL;
    size_t count = 0u;
    size_t index = 0u;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    nmo_object_id_t script_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_behavior_script_view_t view = {0};
    status = nmo_lua_behavior_query_script_from_id(document, script_id, &view);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK && script_id != 0u) {
        status = nmo_lua_behavior_query_count_scripts(document, &count);
        if (status != NMO_OK) {
            return nmo_lua_raise_last_error(state, status, "Failed to inspect script");
        }

        for (index = 0u; index < count; ++index) {
            status = nmo_lua_behavior_query_script_at(document, index, &view);
            if (status != NMO_OK) {
                return nmo_lua_raise_last_error(state, status, "Failed to inspect script");
            }
            if (view.script_id == script_id) {
                status = NMO_OK;
                break;
            }
        }
    }
    if (status == NMO_ERR_NOT_FOUND || (status == NMO_OK && view.script_id != script_id)) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to inspect script");
    }

    nmo_lua_behavior_push_script_view(state, &view);
    return 1;
}

static int nmo_lua_behavior_view(lua_State *state)
{
    nmo_workspace_t *workspace = NULL;
    nmo_status_t status =
        nmo_lua_check_workspace_handle(state, 1, &workspace, NULL, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }

    nmo_object_id_t behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_behavior_view_t view = {0};
    status = nmo_behavior_view_from_behavior(workspace, behavior_id, &view);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to inspect behavior");
    }

    nmo_lua_behavior_push_view(state, &view);
    return 1;
}

static int nmo_lua_behavior_inspect(lua_State *state)
{
    nmo_workspace_t *workspace = NULL;
    nmo_object_id_t behavior_id = 0u;
    uint32_t boundary_depth = UINT32_MAX;
    uint32_t tree_depth = UINT32_MAX;
    nmo_behavior_view_t behavior_view = {0};
    nmo_behavior_boundary_view_t boundary_view = {0};
    nmo_behavior_tree_view_t tree_view = {0};
    nmo_status_t status =
        nmo_lua_check_workspace_handle(state, 1, &workspace, NULL, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }

    behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    if (!lua_isnoneornil(state, 3)) {
        boundary_depth = (uint32_t)luaL_checkinteger(state, 3);
    }
    if (!lua_isnoneornil(state, 4)) {
        tree_depth = (uint32_t)luaL_checkinteger(state, 4);
    }

    status = nmo_behavior_view_from_behavior(workspace, behavior_id, &behavior_view);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to inspect behavior");
    }

    status = nmo_behavior_view_describe_boundary(workspace,
                                                 behavior_id,
                                                 boundary_depth,
                                                 &boundary_view);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to inspect behavior boundary");
    }

    memset(&tree_view, 0, sizeof(tree_view));
    status = nmo_behavior_trace_script_tree(
        workspace, behavior_id, tree_depth, &tree_view);
    if (status != NMO_OK) {
        nmo_behavior_tree_view_destroy(&tree_view);
        return nmo_lua_raise_last_error(state, status, "Failed to inspect script tree");
    }

    lua_createtable(state, 0, 4);
    nmo_lua_behavior_push_view(state, &behavior_view);
    lua_setfield(state, -2, "view");
    nmo_lua_behavior_push_boundary_view(state, &boundary_view);
    lua_setfield(state, -2, "boundary");
    if (behavior_view.interface_available) {
        nmo_lua_push_interface_view(state, &behavior_view.interface_view);
    } else {
        lua_pushnil(state);
    }
    lua_setfield(state, -2, "interface");
    nmo_lua_behavior_push_script_tree_view(state, &tree_view);
    lua_setfield(state, -2, "tree");
    nmo_behavior_tree_view_destroy(&tree_view);
    return 1;
}

static int nmo_lua_behavior_graph(lua_State *state)
{
    nmo_script_edit_graph_t *graph = NULL;
    nmo_arena_t *arena = NULL;
    const nmo_script_edit_node_t *nodes = NULL;
    const nmo_script_edit_control_edge_t *control_edges = NULL;
    const nmo_script_edit_data_edge_t *data_edges = NULL;
    const nmo_ref_edge_t *external_refs = NULL;
    size_t node_count = 0u;
    size_t control_edge_count = 0u;
    size_t data_edge_count = 0u;
    size_t external_ref_count = 0u;
    size_t broken_ref_count = 0u;
    nmo_status_t status =
        nmo_lua_behavior_build_graph_from_args(state, 1, 2, 3, &graph);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to build behavior graph");
    }

    arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        nmo_script_edit_graph_destroy(graph);
        return nmo_lua_raise_last_error(state, NMO_ERR_NOMEM, "Failed to allocate graph arena");
    }

    nodes = nmo_script_edit_graph_nodes(graph, &node_count);
    control_edges = nmo_script_edit_graph_control_edges(graph, &control_edge_count);
    data_edges = nmo_script_edit_graph_data_edges(graph, &data_edge_count);
    status = nmo_script_edit_graph_reference_validation_status(graph, &broken_ref_count);
    if (status == NMO_OK) {
        status = nmo_script_edit_graph_get_external_refs(graph,
                                                         arena,
                                                         &external_refs,
                                                         &external_ref_count);
    }
    if (status != NMO_OK) {
        nmo_arena_destroy(arena);
        nmo_script_edit_graph_destroy(graph);
        return nmo_lua_raise_last_error(state, status, "Failed to inspect behavior graph");
    }

    lua_createtable(state, 0, 7);
    nmo_lua_set_integer_field(
        state,
        "root_behavior_id",
        (lua_Integer)nmo_script_edit_graph_root_behavior_id(graph));
    nmo_lua_set_boolean_field(
        state, "edit_ready", nmo_script_edit_graph_edit_ready(graph));
    nmo_lua_set_boolean_field(
        state,
        "owner_index_available",
        nmo_script_edit_graph_owner_index_available(graph));
    nmo_lua_set_integer_field(
        state, "broken_ref_count", (lua_Integer)broken_ref_count);
    nmo_lua_behavior_push_graph_node_array(state, nodes, node_count);
    lua_setfield(state, -2, "nodes");
    nmo_lua_behavior_push_graph_control_edge_array(state, control_edges, control_edge_count);
    lua_setfield(state, -2, "control_edges");
    nmo_lua_behavior_push_graph_data_edge_array(state, data_edges, data_edge_count);
    lua_setfield(state, -2, "data_edges");
    nmo_lua_behavior_push_ref_edge_array(state, external_refs, external_ref_count);
    lua_setfield(state, -2, "external_refs");

    nmo_arena_destroy(arena);
    nmo_script_edit_graph_destroy(graph);
    return 1;
}

static int nmo_lua_behavior_graph_find_owner(lua_State *state)
{
    nmo_script_edit_graph_t *graph = NULL;
    nmo_object_id_t object_id = 0u;
    nmo_script_edit_endpoint_t owner = {0};
    nmo_status_t status =
        nmo_lua_behavior_build_graph_from_args(state, 1, 2, 4, &graph);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to build behavior graph");
    }

    object_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    status = nmo_script_edit_graph_find_owner(graph, object_id, &owner);
    nmo_script_edit_graph_destroy(graph);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to resolve graph owner");
    }

    nmo_lua_behavior_push_graph_endpoint(state, &owner);
    return 1;
}

static int nmo_lua_behavior_graph_incoming_control(lua_State *state)
{
    nmo_script_edit_graph_t *graph = NULL;
    nmo_object_id_t behavior_id = 0u;
    nmo_arena_t *arena = NULL;
    const nmo_script_edit_control_edge_t *edges = NULL;
    size_t count = 0u;
    nmo_status_t status =
        nmo_lua_behavior_build_graph_from_args(state, 1, 2, 4, &graph);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to build behavior graph");
    }

    behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        nmo_script_edit_graph_destroy(graph);
        return nmo_lua_raise_last_error(state, NMO_ERR_NOMEM, "Failed to allocate graph arena");
    }

    status = nmo_script_edit_graph_get_incoming_control(graph, behavior_id, arena, &edges, &count);
    if (status == NMO_ERR_NOT_FOUND) {
        nmo_arena_destroy(arena);
        nmo_script_edit_graph_destroy(graph);
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        nmo_arena_destroy(arena);
        nmo_script_edit_graph_destroy(graph);
        return nmo_lua_raise_last_error(state, status, "Failed to inspect incoming control edges");
    }

    nmo_lua_behavior_push_graph_control_edge_array(state, edges, count);
    nmo_arena_destroy(arena);
    nmo_script_edit_graph_destroy(graph);
    return 1;
}

static int nmo_lua_behavior_graph_outgoing_control(lua_State *state)
{
    nmo_script_edit_graph_t *graph = NULL;
    nmo_object_id_t behavior_id = 0u;
    nmo_arena_t *arena = NULL;
    const nmo_script_edit_control_edge_t *edges = NULL;
    size_t count = 0u;
    nmo_status_t status =
        nmo_lua_behavior_build_graph_from_args(state, 1, 2, 4, &graph);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to build behavior graph");
    }

    behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        nmo_script_edit_graph_destroy(graph);
        return nmo_lua_raise_last_error(state, NMO_ERR_NOMEM, "Failed to allocate graph arena");
    }

    status = nmo_script_edit_graph_get_outgoing_control(graph, behavior_id, arena, &edges, &count);
    if (status == NMO_ERR_NOT_FOUND) {
        nmo_arena_destroy(arena);
        nmo_script_edit_graph_destroy(graph);
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        nmo_arena_destroy(arena);
        nmo_script_edit_graph_destroy(graph);
        return nmo_lua_raise_last_error(state, status, "Failed to inspect outgoing control edges");
    }

    nmo_lua_behavior_push_graph_control_edge_array(state, edges, count);
    nmo_arena_destroy(arena);
    nmo_script_edit_graph_destroy(graph);
    return 1;
}

static int nmo_lua_behavior_graph_parameter_sources(lua_State *state)
{
    nmo_script_edit_graph_t *graph = NULL;
    nmo_object_id_t parameter_id = 0u;
    nmo_arena_t *arena = NULL;
    const nmo_script_edit_data_edge_t *edges = NULL;
    size_t count = 0u;
    nmo_status_t status =
        nmo_lua_behavior_build_graph_from_args(state, 1, 2, 4, &graph);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to build behavior graph");
    }

    parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        nmo_script_edit_graph_destroy(graph);
        return nmo_lua_raise_last_error(state, NMO_ERR_NOMEM, "Failed to allocate graph arena");
    }

    status = nmo_script_edit_graph_get_parameter_sources(graph, parameter_id, arena, &edges, &count);
    if (status == NMO_ERR_NOT_FOUND) {
        nmo_arena_destroy(arena);
        nmo_script_edit_graph_destroy(graph);
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        nmo_arena_destroy(arena);
        nmo_script_edit_graph_destroy(graph);
        return nmo_lua_raise_last_error(state, status, "Failed to inspect parameter sources");
    }

    nmo_lua_behavior_push_graph_data_edge_array(state, edges, count);
    nmo_arena_destroy(arena);
    nmo_script_edit_graph_destroy(graph);
    return 1;
}

static int nmo_lua_behavior_graph_parameter_destinations(lua_State *state)
{
    nmo_script_edit_graph_t *graph = NULL;
    nmo_object_id_t parameter_id = 0u;
    nmo_arena_t *arena = NULL;
    const nmo_script_edit_data_edge_t *edges = NULL;
    size_t count = 0u;
    nmo_status_t status =
        nmo_lua_behavior_build_graph_from_args(state, 1, 2, 4, &graph);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to build behavior graph");
    }

    parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    arena = nmo_arena_create(NULL, 0);
    if (arena == NULL) {
        nmo_script_edit_graph_destroy(graph);
        return nmo_lua_raise_last_error(state, NMO_ERR_NOMEM, "Failed to allocate graph arena");
    }

    status = nmo_script_edit_graph_get_parameter_destinations(graph,
                                                              parameter_id,
                                                              arena,
                                                              &edges,
                                                              &count);
    if (status == NMO_ERR_NOT_FOUND) {
        nmo_arena_destroy(arena);
        nmo_script_edit_graph_destroy(graph);
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        nmo_arena_destroy(arena);
        nmo_script_edit_graph_destroy(graph);
        return nmo_lua_raise_last_error(state,
                                        status,
                                        "Failed to inspect parameter destinations");
    }

    nmo_lua_behavior_push_graph_data_edge_array(state, edges, count);
    nmo_arena_destroy(arena);
    nmo_script_edit_graph_destroy(graph);
    return 1;
}

static nmo_status_t nmo_lua_behavior_parse_graph_handle_kind(
    lua_State *state,
    int index,
    nmo_script_edit_handle_kind_t *out_kind)
{
    if (lua_isinteger(state, index)) {
        lua_Integer value = lua_tointeger(state, index);
        if (value < (lua_Integer)NMO_SCRIPT_EDIT_HANDLE_OBJECT_ID ||
            value > (lua_Integer)NMO_SCRIPT_EDIT_HANDLE_SLOT) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "invalid graph handle kind");
        }
        *out_kind = (nmo_script_edit_handle_kind_t)value;
        NMO_RETURN_OK();
    }

    if (!lua_isstring(state, index)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "graph handle kind must be a string or integer");
    }

    const char *kind = lua_tostring(state, index);
    if (kind == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "graph handle kind must be non-null");
    }
    if (strcmp(kind, "object_id") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_HANDLE_OBJECT_ID;
        NMO_RETURN_OK();
    }
    if (strcmp(kind, "alias") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_HANDLE_ALIAS;
        NMO_RETURN_OK();
    }
    if (strcmp(kind, "query") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_HANDLE_QUERY;
        NMO_RETURN_OK();
    }
    if (strcmp(kind, "slot") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_HANDLE_SLOT;
        NMO_RETURN_OK();
    }

    NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                     "unknown graph handle kind '%s'", kind);
}

static nmo_status_t nmo_lua_behavior_parse_graph_op_kind(
    lua_State *state,
    int index,
    nmo_script_edit_op_kind_t *out_kind)
{
    if (lua_isinteger(state, index)) {
        lua_Integer value = lua_tointeger(state, index);
        if (value < (lua_Integer)NMO_SCRIPT_EDIT_OP_NODE_ADD ||
            value > (lua_Integer)NMO_SCRIPT_EDIT_OP_VALIDATE) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "invalid graph operation kind");
        }
        *out_kind = (nmo_script_edit_op_kind_t)value;
        NMO_RETURN_OK();
    }

    if (!lua_isstring(state, index)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "graph operation kind must be a string or integer");
    }

    const char *kind = lua_tostring(state, index);
    if (kind == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "graph operation kind must be non-null");
    }
    if (strcmp(kind, "node_add") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_NODE_ADD;
    } else if (strcmp(kind, "node_remove") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_NODE_REMOVE;
    } else if (strcmp(kind, "io_add") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_IO_ADD;
    } else if (strcmp(kind, "io_rename") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_IO_RENAME;
    } else if (strcmp(kind, "io_remove") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_IO_REMOVE;
    } else if (strcmp(kind, "link_add") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_LINK_ADD;
    } else if (strcmp(kind, "link_rewire") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_LINK_REWIRE;
    } else if (strcmp(kind, "link_remove") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_LINK_REMOVE;
    } else if (strcmp(kind, "param_add") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_PARAM_ADD;
    } else if (strcmp(kind, "param_set") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_PARAM_SET;
    } else if (strcmp(kind, "param_connect") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_PARAM_CONNECT;
    } else if (strcmp(kind, "param_disconnect") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_PARAM_DISCONNECT;
    } else if (strcmp(kind, "param_remove") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_PARAM_REMOVE;
    } else if (strcmp(kind, "operation_add") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_OPERATION_ADD;
    } else if (strcmp(kind, "operation_rewire") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_OPERATION_REWIRE;
    } else if (strcmp(kind, "operation_remove") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_OPERATION_REMOVE;
    } else if (strcmp(kind, "subgraph_fold") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_SUBGRAPH_FOLD;
    } else if (strcmp(kind, "validate") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_OP_VALIDATE;
    } else {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "unknown graph operation kind '%s'", kind);
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_lua_behavior_parse_graph_handle(
    lua_State *state,
    int index,
    nmo_script_edit_handle_t *out_handle)
{
    int table_index = 0;
    bool has_kind = false;
    bool has_object_id = false;
    bool has_alias = false;
    bool has_query = false;
    bool has_owner_id = false;
    bool has_slot_index = false;
    bool has_slot_kind = false;
    nmo_status_t status = NMO_OK;

    if (out_handle == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "graph handle output must be non-null");
    }
    if (!lua_istable(state, index)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "graph handle must be a table");
    }

    memset(out_handle, 0, sizeof(*out_handle));
    table_index = lua_absindex(state, index);

    lua_getfield(state, table_index, "kind");
    if (!lua_isnoneornil(state, -1)) {
        has_kind = true;
        status = nmo_lua_behavior_parse_graph_handle_kind(state, -1, &out_handle->kind);
        lua_pop(state, 1);
        if (status != NMO_OK) {
            return status;
        }
    } else {
        lua_pop(state, 1);
    }

    lua_getfield(state, table_index, "object_id");
    if (!lua_isnoneornil(state, -1)) {
        out_handle->object_id = (nmo_object_id_t)luaL_checkinteger(state, -1);
        has_object_id = true;
    }
    lua_pop(state, 1);

    lua_getfield(state, table_index, "alias");
    if (!lua_isnoneornil(state, -1)) {
        out_handle->alias = luaL_checkstring(state, -1);
        has_alias = true;
    }
    lua_pop(state, 1);

    lua_getfield(state, table_index, "query");
    if (!lua_isnoneornil(state, -1)) {
        out_handle->query = luaL_checkstring(state, -1);
        has_query = true;
    }
    lua_pop(state, 1);

    lua_getfield(state, table_index, "owner_id");
    if (!lua_isnoneornil(state, -1)) {
        out_handle->owner_id = (nmo_object_id_t)luaL_checkinteger(state, -1);
        has_owner_id = true;
    }
    lua_pop(state, 1);

    lua_getfield(state, table_index, "slot_index");
    if (!lua_isnoneornil(state, -1)) {
        out_handle->slot_index = (int32_t)luaL_checkinteger(state, -1);
        has_slot_index = true;
    }
    lua_pop(state, 1);

    lua_getfield(state, table_index, "slot_kind");
    if (!lua_isnoneornil(state, -1)) {
        out_handle->slot_kind = (uint32_t)luaL_checkinteger(state, -1);
        has_slot_kind = true;
    }
    lua_pop(state, 1);

    if (!has_kind) {
        if (has_object_id) {
            out_handle->kind = NMO_SCRIPT_EDIT_HANDLE_OBJECT_ID;
        } else if (has_alias) {
            out_handle->kind = NMO_SCRIPT_EDIT_HANDLE_ALIAS;
        } else if (has_query) {
            out_handle->kind = NMO_SCRIPT_EDIT_HANDLE_QUERY;
        } else if (has_owner_id || has_slot_index || has_slot_kind) {
            out_handle->kind = NMO_SCRIPT_EDIT_HANDLE_SLOT;
        } else {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "graph handle table does not identify a handle kind");
        }
    }

    switch (out_handle->kind) {
    case NMO_SCRIPT_EDIT_HANDLE_OBJECT_ID:
        if (!has_object_id) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "object_id handles require object_id");
        }
        break;
    case NMO_SCRIPT_EDIT_HANDLE_ALIAS:
        if (!has_alias) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "alias handles require alias");
        }
        break;
    case NMO_SCRIPT_EDIT_HANDLE_QUERY:
        if (!has_query) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "query handles require query");
        }
        break;
    case NMO_SCRIPT_EDIT_HANDLE_SLOT:
        if (!has_owner_id || !has_slot_index || !has_slot_kind) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "slot handles require owner_id, slot_index, and slot_kind");
        }
        break;
    default:
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "unknown graph handle kind");
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_lua_behavior_parse_graph_operation(
    lua_State *state,
    int index,
    nmo_script_edit_op_t *out_op)
{
    int table_index = 0;
    nmo_status_t status = NMO_OK;

    if (out_op == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "graph operation output must be non-null");
    }
    if (!lua_istable(state, index)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "graph operation must be a table");
    }

    memset(out_op, 0, sizeof(*out_op));
    table_index = lua_absindex(state, index);

    lua_getfield(state, table_index, "kind");
    status = nmo_lua_behavior_parse_graph_op_kind(state, -1, &out_op->kind);
    lua_pop(state, 1);
    if (status != NMO_OK) {
        return status;
    }

    lua_getfield(state, table_index, "primary");
    if (!lua_isnoneornil(state, -1)) {
        status = nmo_lua_behavior_parse_graph_handle(state, -1, &out_op->primary);
        lua_pop(state, 1);
        if (status != NMO_OK) {
            return status;
        }
    } else {
        lua_pop(state, 1);
    }

    lua_getfield(state, table_index, "secondary");
    if (!lua_isnoneornil(state, -1)) {
        status = nmo_lua_behavior_parse_graph_handle(state, -1, &out_op->secondary);
        lua_pop(state, 1);
        if (status != NMO_OK) {
            return status;
        }
    } else {
        lua_pop(state, 1);
    }

    lua_getfield(state, table_index, "label");
    if (!lua_isnoneornil(state, -1)) {
        out_op->label = luaL_checkstring(state, -1);
    }
    lua_pop(state, 1);

    lua_getfield(state, table_index, "guid");
    if (!lua_isnoneornil(state, -1)) {
        status = nmo_lua_behavior_parse_guid_arg(state, -1, &out_op->guid);
        lua_pop(state, 1);
        if (status != NMO_OK) {
            return status;
        }
    } else {
        lua_pop(state, 1);
    }

    lua_getfield(state, table_index, "flags");
    if (!lua_isnoneornil(state, -1)) {
        out_op->flags = (uint32_t)luaL_checkinteger(state, -1);
    }
    lua_pop(state, 1);

    NMO_RETURN_OK();
}

static int nmo_lua_behavior_graph_resolve_handle(lua_State *state)
{
    nmo_script_edit_graph_t *graph = NULL;
    nmo_script_edit_handle_t handle = {0};
    nmo_object_id_t object_id = 0u;
    nmo_status_t status =
        nmo_lua_behavior_build_graph_from_args(state, 1, 2, 4, &graph);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to build behavior graph");
    }

    status = nmo_lua_behavior_parse_graph_handle(state, 3, &handle);
    if (status != NMO_OK) {
        nmo_script_edit_graph_destroy(graph);
        return nmo_lua_raise_last_error(state, status, "Invalid graph handle");
    }

    status = nmo_script_edit_graph_resolve_handle(graph, &handle, &object_id);
    nmo_script_edit_graph_destroy(graph);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to resolve graph handle");
    }

    lua_pushinteger(state, (lua_Integer)object_id);
    return 1;
}

static int nmo_lua_behavior_graph_validate_operation(lua_State *state)
{
    nmo_script_edit_graph_t *graph = NULL;
    nmo_script_edit_op_t op = {0};
    nmo_status_t status =
        nmo_lua_behavior_build_graph_from_args(state, 1, 2, 4, &graph);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to build behavior graph");
    }

    status = nmo_lua_behavior_parse_graph_operation(state, 3, &op);
    if (status != NMO_OK) {
        nmo_script_edit_graph_destroy(graph);
        return nmo_lua_raise_last_error(state, status, "Invalid graph operation");
    }

    status = nmo_script_edit_graph_validate_operation(graph, &op);
    nmo_script_edit_graph_destroy(graph);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state,
                                        status,
                                        "Graph operation validation failed");
    }

    lua_pushboolean(state, 1);
    return 1;
}

static int nmo_lua_behavior_execute(lua_State *state)
{
    const char *input_path = NULL;
    const char *output_path = NULL;
    nmo_behavior_execute_options_t options = nmo_behavior_execute_options_default();
    nmo_edit_report_t edit_report;
    bool edit_report_init = false;
    nmo_context_t *context = NULL;
    nmo_lua_behavior_execute_action_t action = {0};
    nmo_lua_dump_buffer_t dump_buffer = {0};
    nmo_status_t status = NMO_OK;

    input_path = luaL_checkstring(state, 1);
    if (!lua_isnoneornil(state, 2)) {
        output_path = luaL_checkstring(state, 2);
    }
    if (!lua_isnoneornil(state, 3)) {
        if (!lua_istable(state, 3)) {
            return luaL_error(state, "executor options must be a table");
        }

        lua_getfield(state, 3, "label");
        if (!lua_isnoneornil(state, -1)) {
            options.label = luaL_checkstring(state, -1);
        }
        lua_pop(state, 1);

        lua_getfield(state, 3, "dry_run");
        if (!lua_isnoneornil(state, -1)) {
            options.dry_run = lua_toboolean(state, -1) != 0;
        }
        lua_pop(state, 1);

        lua_getfield(state, 3, "validation_flags");
        if (!lua_isnoneornil(state, -1)) {
            options.validation_flags = (uint32_t)luaL_checkinteger(state, -1);
        }
        lua_pop(state, 1);
    }

    luaL_checktype(state, 4, LUA_TFUNCTION);
    if (lua_iscfunction(state, 4)) {
        return luaL_error(state, "behavior.execute callback must be a Lua function");
    }

    lua_pushvalue(state, 4);
    if (lua_dump(state, nmo_lua_behavior_dump_writer, &dump_buffer, 0) != 0) {
        lua_pop(state, 1);
        nmo_lua_behavior_dump_buffer_clear(&dump_buffer);
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_VALIDATION_FAILED,
                                        "Failed to serialize executor callback");
    }
    lua_pop(state, 1);

    action.dumped_chunk = dump_buffer.data;
    action.dumped_size = dump_buffer.size;
    action.dry_run = options.dry_run;
    action.validation_flags = options.validation_flags;

    status = nmo_edit_report_init(&edit_report);
    if (status != NMO_OK) {
        nmo_lua_behavior_dump_buffer_clear(&dump_buffer);
        return nmo_lua_raise_last_error(state, status, "Failed to create edit report");
    }
    edit_report_init = true;
    action.edit_report = &edit_report;

    context = nmo_context_create(NULL);
    if (context == NULL) {
        nmo_edit_report_dispose(&edit_report);
        nmo_lua_behavior_dump_buffer_clear(&dump_buffer);
        return nmo_lua_raise_last_error(state, NMO_ERR_NOMEM, "Failed to create executor context");
    }

    status = nmo_behavior_execute(context,
                                         input_path,
                                         output_path,
                                          &options,
                                          nmo_lua_behavior_execute_action_fn,
                                          &action,
                                          NULL);
    nmo_context_release(context);
    nmo_lua_behavior_dump_buffer_clear(&dump_buffer);
    if (status != NMO_OK) {
        if (edit_report_init) {
            nmo_edit_report_dispose(&edit_report);
        }
        return nmo_lua_raise_last_error(state, status, "Lua behavior executor failed");
    }
    if (!options.dry_run && output_path != NULL && edit_report.output_path == NULL) {
        (void)nmo_edit_report_set_output_path(&edit_report, output_path);
    }

    nmo_lua_push_edit_report(state, &edit_report);
    nmo_edit_report_dispose(&edit_report);
    return 1;
}

static int nmo_lua_behavior_describe_boundary(lua_State *state)
{
    nmo_workspace_t *workspace = NULL;
    nmo_status_t status =
        nmo_lua_check_workspace_handle(state, 1, &workspace, NULL, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }

    nmo_object_id_t behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    uint32_t max_depth = UINT32_MAX;
    if (!lua_isnoneornil(state, 3)) {
        max_depth = (uint32_t)luaL_checkinteger(state, 3);
    }

    nmo_behavior_boundary_view_t view = {0};
    status = nmo_behavior_view_describe_boundary(workspace, behavior_id, max_depth, &view);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to inspect behavior boundary");
    }

    nmo_lua_behavior_push_boundary_view(state, &view);
    return 1;
}

static int nmo_lua_behavior_trace_parameter_chain(lua_State *state)
{
    nmo_workspace_t *workspace = NULL;
    nmo_object_id_t parameter_id = 0u;
    uint32_t max_depth = 32u;
    nmo_behavior_trace_chain_view_t view;
    nmo_status_t status =
        nmo_lua_check_workspace_handle(state, 1, &workspace, NULL, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }

    parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    if (!lua_isnoneornil(state, 3)) {
        max_depth = (uint32_t)luaL_checkinteger(state, 3);
    }

    memset(&view, 0, sizeof(view));
    status = nmo_behavior_trace_parameter_chain(workspace, parameter_id, max_depth, &view);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        nmo_behavior_trace_chain_view_destroy(&view);
        return nmo_lua_raise_last_error(state, status, "Failed to trace parameter chain");
    }

    nmo_lua_behavior_push_trace_chain_view(state, &view);
    nmo_behavior_trace_chain_view_destroy(&view);
    return 1;
}

static int nmo_lua_behavior_script_tree(lua_State *state)
{
    nmo_workspace_t *workspace = NULL;
    nmo_object_id_t root_behavior_id = 0u;
    uint32_t max_depth = UINT32_MAX;
    nmo_behavior_tree_view_t view;
    nmo_status_t status =
        nmo_lua_check_workspace_handle(state, 1, &workspace, NULL, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }

    root_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    if (!lua_isnoneornil(state, 3)) {
        max_depth = (uint32_t)luaL_checkinteger(state, 3);
    }

    memset(&view, 0, sizeof(view));
    status = nmo_behavior_trace_script_tree(workspace, root_behavior_id, max_depth, &view);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        nmo_behavior_tree_view_destroy(&view);
        return nmo_lua_raise_last_error(state, status, "Failed to inspect script tree");
    }

    nmo_lua_behavior_push_script_tree_view(state, &view);
    nmo_behavior_tree_view_destroy(&view);
    return 1;
}

static int nmo_lua_behavior_begin_edit(lua_State *state)
{
    nmo_workspace_t *workspace = NULL;
    nmo_lua_handle_scope_t *workspace_scope = NULL;
    nmo_status_t status =
        nmo_lua_check_workspace_handle(state, 1, &workspace, &workspace_scope, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }

    const char *label = luaL_optstring(state, 2, "lua-script-edit");
    (void)label;
    nmo_edit_plan_t *plan = NULL;
    status = nmo_edit_plan_create(&plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to begin script edit");
    }

    status = nmo_lua_push_script_edit_tx_handle(state, workspace, plan, workspace_scope);
    if (status != NMO_OK) {
        nmo_edit_plan_destroy(plan);
        return nmo_lua_raise_last_error(state, status, "Failed to push script edit handle");
    }

    return 1;
}

static int nmo_lua_behavior_report(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    if (handle->has_report) {
        nmo_lua_push_edit_report(state, &handle->report);
        return 1;
    }
    if (handle->plan != NULL) {
        nmo_lua_push_pending_edit_plan_report(state, handle->plan);
        return 1;
    }
    return luaL_error(state, "script edit transaction report is unavailable");
}

static int nmo_lua_behavior_rollback(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    if (handle->plan != NULL) {
        nmo_edit_plan_destroy(handle->plan);
        handle->plan = NULL;
    }
    if (handle->has_report) {
        nmo_edit_report_dispose(&handle->report);
        handle->has_report = false;
    }
    handle->executed = true;
    return nmo_lua_behavior_invalidate_tx_handle(state, 1, handle);
}

static int nmo_lua_behavior_validate(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    uint32_t flags = NMO_SCRIPT_EDIT_VALIDATE_REFERENCES |
                     NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX |
                     NMO_SCRIPT_EDIT_VALIDATE_INTERFACE |
                     NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    flags = nmo_lua_behavior_optional_flags(state, 2, flags);
    nmo_edit_executor_options_t options = nmo_edit_executor_options_default();
    options.dry_run = true;
    options.validation_flags = flags;
    nmo_edit_report_t report;
    status = nmo_edit_report_init(&report);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to create validation report");
    }
    status = nmo_edit_executor_execute(handle->workspace, handle->plan, &options, &report);
    nmo_edit_report_dispose(&report);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Script validation failed");
    }
    return 0;
}

static int nmo_lua_behavior_mark(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    uint32_t flags = 0u;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    flags = (uint32_t)luaL_checkinteger(state, 2);
    (void)handle;
    (void)flags;
    return 0;
}

static int nmo_lua_behavior_validate_interface_refs(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t behavior_id = 0u;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    (void)behavior_id;
    nmo_edit_executor_options_t options = nmo_edit_executor_options_default();
    options.dry_run = true;
    options.validation_flags = NMO_SCRIPT_EDIT_VALIDATE_INTERFACE;
    nmo_edit_report_t report;
    status = nmo_edit_report_init(&report);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to create validation report");
    }
    status = nmo_edit_executor_execute(handle->workspace, handle->plan, &options, &report);
    nmo_edit_report_dispose(&report);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Interface reference validation failed");
    }
    return 0;
}

static int nmo_lua_behavior_apply_interface_policy(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t behavior_id = 0u;
    nmo_script_edit_interface_mode_t mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_lua_behavior_parse_interface_mode(state, 3, &mode);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid interface mode");
    }

    status = nmo_edit_plan_add_interface_policy(handle->plan, behavior_id, mode);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to apply interface policy");
    }
    return 0;
}

static int nmo_lua_behavior_commit(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    if (handle->has_report) {
        nmo_edit_report_dispose(&handle->report);
        handle->has_report = false;
    }
    status = nmo_edit_report_init(&handle->report);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to create edit report");
    }
    handle->has_report = true;
    nmo_edit_executor_options_t options = nmo_edit_executor_options_default();
    options.dry_run = handle->dry_run_report;
    if (handle->validation_flags != 0u) {
        options.validation_flags = handle->validation_flags;
    }
    if (handle->tx != NULL) {
        status = nmo_edit_executor_execute_transaction(
            handle->tx, handle->plan, &options, &handle->report);
    } else {
        status = nmo_edit_executor_execute(
            handle->workspace, handle->plan, &options, &handle->report);
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to commit script edit");
    }
    if (handle->tx == NULL) {
        nmo_edit_plan_destroy(handle->plan);
        handle->plan = NULL;
    }
    handle->executed = true;
    nmo_lua_push_edit_report(state, &handle->report);
    return 1;
}

static int nmo_lua_behavior_add_node(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_guid_t bb_guid = NMO_GUID_NULL;
    nmo_object_id_t parent_behavior_id = 0u;
    const char *name = NULL;
    nmo_add_node_options_t options = {0};
    options.manager_entry = nmo_manager_entry_options_default();
    bool has_options = false;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    parent_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_lua_behavior_parse_guid_arg(state, 3, &bb_guid);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid building-block GUID");
    }
    name = luaL_checkstring(state, 4);
    if (!lua_isnoneornil(state, 5)) {
        luaL_checktype(state, 5, LUA_TTABLE);
        lua_getfield(state, 5, "manager_entry");
        if (!lua_isnil(state, -1)) {
            int rc = nmo_lua_behavior_parse_manager_entry_options(
                state, lua_gettop(state), &options.manager_entry);
            if (rc != 0) {
                return rc;
            }
        }
        lua_pop(state, 1);
        has_options = true;
    }

    status = nmo_edit_plan_add_node_ex(handle->plan,
                                       parent_behavior_id,
                                       bb_guid,
                                       name,
                                       has_options ? &options : NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add behavior node");
    }

    return nmo_lua_behavior_push_pending_handle(
        state, handle->plan, NMO_EDIT_OP_ADD_NODE);
}

static int nmo_lua_behavior_remove_node(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t parent_behavior_id = 0u;
    nmo_object_id_t node_id = 0u;
    uint32_t delete_flags = 0u;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    parent_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    node_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    delete_flags = nmo_lua_behavior_optional_flags(state, 4, 0u);
    status = nmo_edit_plan_add_remove_node(handle->plan,
                                           parent_behavior_id,
                                           node_id,
                                           delete_flags);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to remove behavior node");
    }
    return 0;
}

static int nmo_lua_behavior_add_io(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t behavior_id = 0u;
    nmo_script_edit_io_kind_t kind = NMO_SCRIPT_EDIT_IO_INPUT;
    const char *name = NULL;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_lua_behavior_parse_io_kind(state, 3, &kind);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid io kind");
    }
    name = luaL_checkstring(state, 4);

    status = nmo_edit_plan_add_io(handle->plan, behavior_id, kind, name);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add io");
    }
    return nmo_lua_behavior_push_pending_handle(
        state, handle->plan, NMO_EDIT_OP_ADD_IO);
}

static int nmo_lua_behavior_rename_io(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t io_id = 0u;
    const char *name = NULL;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    io_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    name = luaL_checkstring(state, 3);
    status = nmo_edit_plan_add_rename_io(handle->plan, io_id, name);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to rename io");
    }
    return 0;
}

static int nmo_lua_behavior_remove_io(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t io_id = 0u;
    int detach_links = 0;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    io_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    detach_links = lua_toboolean(state, 3);
    status = nmo_edit_plan_add_remove_io(handle->plan, io_id, detach_links != 0);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to remove io");
    }
    return 0;
}

static int nmo_lua_behavior_add_parameter(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t owner_behavior_id = 0u;
    nmo_script_edit_parameter_kind_t kind = NMO_SCRIPT_EDIT_PARAM_LOCAL;
    nmo_guid_t type_guid = NMO_GUID_NULL;
    const char *name = NULL;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    owner_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_lua_behavior_parse_parameter_kind(state, 3, &kind);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid parameter kind");
    }
    status = nmo_lua_behavior_parse_guid_arg(state, 4, &type_guid);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid parameter type GUID");
    }
    name = luaL_checkstring(state, 5);

    status = nmo_edit_plan_add_parameter(handle->plan,
                                         owner_behavior_id,
                                         kind,
                                         type_guid,
                                         name);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add parameter");
    }
    return nmo_lua_behavior_push_pending_handle(
        state, handle->plan, NMO_EDIT_OP_ADD_PARAMETER);
}

static int nmo_lua_behavior_set_parameter_value(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_lua_behavior_pending_ref_t parameter = {0};
    const char *value_text = NULL;
    nmo_parameter_write_options_t options;
    bool has_options = false;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    status = nmo_lua_behavior_parse_object_ref(state, 2, false, &parameter);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid parameter reference");
    }
    value_text = luaL_checkstring(state, 3);
    int option_rc = nmo_lua_behavior_parse_parameter_write_options(
        state, 4, &options, &has_options);
    if (option_rc != 0) {
        return option_rc;
    }
    if (parameter.has_ref) {
        status = nmo_edit_plan_add_set_parameter_value_from_handle(
            handle->plan,
            parameter.operation_index,
            parameter.handle_name,
            value_text,
            has_options ? &options : NULL);
    } else {
        status = nmo_edit_plan_add_set_parameter_value(
            handle->plan,
            parameter.id,
            value_text,
            has_options ? &options : NULL);
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to set parameter value");
    }
    return 0;
}

static int nmo_lua_behavior_set_parameter_bytes(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_lua_behavior_pending_ref_t parameter = {0};
    size_t byte_count = 0u;
    const char *bytes = NULL;
    nmo_parameter_write_options_t options;
    bool has_options = false;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    status = nmo_lua_behavior_parse_object_ref(state, 2, false, &parameter);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid parameter reference");
    }
    bytes = luaL_checklstring(state, 3, &byte_count);
    int option_rc = nmo_lua_behavior_parse_parameter_write_options(
        state, 4, &options, &has_options);
    if (option_rc != 0) {
        return option_rc;
    }
    if (parameter.has_ref) {
        status = nmo_edit_plan_add_set_parameter_bytes_from_handle(
            handle->plan,
            parameter.operation_index,
            parameter.handle_name,
            (const uint8_t *)bytes,
            byte_count,
            has_options ? &options : NULL);
    } else {
        status = nmo_edit_plan_add_set_parameter_bytes(
            handle->plan,
            parameter.id,
            (const uint8_t *)bytes,
            byte_count,
            has_options ? &options : NULL);
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to set parameter bytes");
    }
    return 0;
}

static int nmo_lua_behavior_connect_parameter(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t source_parameter_id = 0u;
    nmo_lua_behavior_pending_ref_t target_parameter = {0};
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    source_parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_lua_behavior_parse_object_ref(state, 3, false, &target_parameter);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid target parameter reference");
    }
    if (target_parameter.has_ref) {
        status = nmo_edit_plan_add_connect_parameter_to_handle(
            handle->plan,
            source_parameter_id,
            target_parameter.operation_index,
            target_parameter.handle_name);
    } else {
        status = nmo_edit_plan_add_connect_parameter(
            handle->plan, source_parameter_id, target_parameter.id);
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to connect parameter");
    }
    return 0;
}

static int nmo_lua_behavior_disconnect_parameter(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t target_parameter_id = 0u;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    target_parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_edit_plan_add_disconnect_parameter(handle->plan, target_parameter_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to disconnect parameter");
    }
    return 0;
}

static int nmo_lua_behavior_remove_parameter(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t parameter_id = 0u;
    int detach = 0;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    detach = lua_toboolean(state, 3);
    status = nmo_edit_plan_add_remove_parameter(handle->plan, parameter_id, detach != 0);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to remove parameter");
    }
    return 0;
}

static int nmo_lua_behavior_add_link(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t parent_behavior_id = 0u;
    nmo_lua_behavior_pending_ref_t from_io = {0};
    nmo_lua_behavior_pending_ref_t to_io = {0};
    uint32_t activation_delay = 0u;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    parent_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_lua_behavior_parse_object_ref(state, 3, false, &from_io);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid source io reference");
    }
    status = nmo_lua_behavior_parse_object_ref(state, 4, false, &to_io);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid target io reference");
    }
    activation_delay = nmo_lua_behavior_optional_flags(state, 5, 0u);
    if (from_io.has_ref && to_io.has_ref) {
        status = nmo_edit_plan_add_behavior_link_from_handles(
            handle->plan,
            parent_behavior_id,
            from_io.operation_index,
            from_io.handle_name,
            to_io.operation_index,
            to_io.handle_name,
            activation_delay);
    } else if (from_io.has_ref) {
        status = nmo_edit_plan_add_behavior_link_from_handle(
            handle->plan,
            parent_behavior_id,
            from_io.operation_index,
            from_io.handle_name,
            to_io.id,
            activation_delay);
    } else if (to_io.has_ref) {
        status = nmo_edit_plan_add_behavior_link_to_handle(
            handle->plan,
            parent_behavior_id,
            from_io.id,
            to_io.operation_index,
            to_io.handle_name,
            activation_delay);
    } else {
        status = nmo_edit_plan_add_behavior_link(
            handle->plan,
            parent_behavior_id,
            from_io.id,
            to_io.id,
            activation_delay);
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add link");
    }
    return nmo_lua_behavior_push_pending_handle(
        state, handle->plan, NMO_EDIT_OP_ADD_BEHAVIOR_LINK);
}

static int nmo_lua_behavior_rewire_link(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t link_id = 0u;
    nmo_object_id_t from_io_id = 0u;
    nmo_object_id_t to_io_id = 0u;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    link_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    from_io_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    to_io_id = (nmo_object_id_t)luaL_checkinteger(state, 4);
    status = nmo_edit_plan_add_rewire_behavior_link(
        handle->plan, link_id, from_io_id, to_io_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to rewire link");
    }
    return 0;
}

static int nmo_lua_behavior_set_link_delay(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t link_id = 0u;
    uint32_t activation_delay = 0u;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    link_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    activation_delay = (uint32_t)luaL_checkinteger(state, 3);
    status = nmo_edit_plan_add_set_behavior_link_delay(
        handle->plan, link_id, activation_delay);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to set link delay");
    }
    return 0;
}

static int nmo_lua_behavior_remove_link(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t parent_behavior_id = 0u;
    nmo_object_id_t link_id = 0u;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    parent_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    link_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    status = nmo_edit_plan_add_remove_behavior_link(
        handle->plan, parent_behavior_id, link_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to remove link");
    }
    return 0;
}

static int nmo_lua_behavior_add_operation(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t parent_behavior_id = 0u;
    nmo_guid_t operation_guid = NMO_GUID_NULL;
    nmo_lua_behavior_pending_ref_t in1 = {0};
    nmo_lua_behavior_pending_ref_t in2 = {0};
    nmo_lua_behavior_pending_ref_t out = {0};
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    parent_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_lua_behavior_parse_guid_arg(state, 3, &operation_guid);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid operation GUID");
    }
    status = nmo_lua_behavior_parse_object_ref(state, 4, true, &in1);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid first input parameter reference");
    }
    status = nmo_lua_behavior_parse_object_ref(state, 5, true, &in2);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid second input parameter reference");
    }
    status = nmo_lua_behavior_parse_object_ref(state, 6, true, &out);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid output parameter reference");
    }
    if (in1.has_ref || in2.has_ref || out.has_ref) {
        status = nmo_edit_plan_add_operation_with_refs(
            handle->plan,
            parent_behavior_id,
            operation_guid,
            in1.id,
            in1.operation_index,
            in1.has_ref ? in1.handle_name : NULL,
            in2.id,
            in2.operation_index,
            in2.has_ref ? in2.handle_name : NULL,
            out.id,
            out.operation_index,
            out.has_ref ? out.handle_name : NULL);
    } else {
        status = nmo_edit_plan_add_operation(
            handle->plan,
            parent_behavior_id,
            operation_guid,
            in1.id,
            in2.id,
            out.id);
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add operation");
    }
    return nmo_lua_behavior_push_pending_handle(
        state, handle->plan, NMO_EDIT_OP_ADD_OPERATION);
}

static int nmo_lua_behavior_rewire_operation(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t operation_id = 0u;
    uint32_t slot_flags = 0u;
    nmo_object_id_t in1_parameter_id = 0u;
    nmo_object_id_t in2_parameter_id = 0u;
    nmo_object_id_t out_parameter_id = 0u;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    operation_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    slot_flags = (uint32_t)luaL_checkinteger(state, 3);
    in1_parameter_id = nmo_lua_behavior_optional_object_id(state, 4);
    in2_parameter_id = nmo_lua_behavior_optional_object_id(state, 5);
    out_parameter_id = nmo_lua_behavior_optional_object_id(state, 6);
    status = nmo_edit_plan_add_rewire_operation(handle->plan,
                                                operation_id,
                                                slot_flags,
                                                in1_parameter_id,
                                                in2_parameter_id,
                                                out_parameter_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to rewire operation");
    }
    return 0;
}

static int nmo_lua_behavior_remove_operation(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t operation_id = 0u;
    nmo_status_t status = nmo_lua_behavior_check_active_edit_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    operation_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_edit_plan_add_remove_operation(handle->plan, operation_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to remove operation");
    }
    return 0;
}

static void nmo_lua_behavior_push_validation_flags(lua_State *state)
{
    static const nmo_lua_integer_entry_t entries[] = {
        { "references", (lua_Integer)NMO_SCRIPT_EDIT_VALIDATE_REFERENCES },
        { "behavior_index", (lua_Integer)NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX },
        { "interface", (lua_Integer)NMO_SCRIPT_EDIT_VALIDATE_INTERFACE },
        { "roundtrip_ready", (lua_Integer)NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY },
    };
    nmo_lua_push_integer_table(state, entries, sizeof(entries) / sizeof(entries[0]));
}

static void nmo_lua_behavior_push_operation_slot_flags(lua_State *state)
{
    static const nmo_lua_integer_entry_t entries[] = {
        { "in1", (lua_Integer)NMO_SCRIPT_EDIT_OP_SLOT_IN1 },
        { "in2", (lua_Integer)NMO_SCRIPT_EDIT_OP_SLOT_IN2 },
        { "out", (lua_Integer)NMO_SCRIPT_EDIT_OP_SLOT_OUT },
    };
    nmo_lua_push_integer_table(state, entries, sizeof(entries) / sizeof(entries[0]));
}

static void nmo_lua_behavior_push_graph_handle_kinds(lua_State *state)
{
    static const nmo_lua_integer_entry_t entries[] = {
        { "object_id", (lua_Integer)NMO_SCRIPT_EDIT_HANDLE_OBJECT_ID },
        { "alias", (lua_Integer)NMO_SCRIPT_EDIT_HANDLE_ALIAS },
        { "query", (lua_Integer)NMO_SCRIPT_EDIT_HANDLE_QUERY },
        { "slot", (lua_Integer)NMO_SCRIPT_EDIT_HANDLE_SLOT },
    };
    nmo_lua_push_integer_table(state, entries, sizeof(entries) / sizeof(entries[0]));
}

static void nmo_lua_behavior_push_graph_op_kinds(lua_State *state)
{
    static const nmo_lua_integer_entry_t entries[] = {
        { "node_add", (lua_Integer)NMO_SCRIPT_EDIT_OP_NODE_ADD },
        { "node_remove", (lua_Integer)NMO_SCRIPT_EDIT_OP_NODE_REMOVE },
        { "io_add", (lua_Integer)NMO_SCRIPT_EDIT_OP_IO_ADD },
        { "io_rename", (lua_Integer)NMO_SCRIPT_EDIT_OP_IO_RENAME },
        { "io_remove", (lua_Integer)NMO_SCRIPT_EDIT_OP_IO_REMOVE },
        { "link_add", (lua_Integer)NMO_SCRIPT_EDIT_OP_LINK_ADD },
        { "link_rewire", (lua_Integer)NMO_SCRIPT_EDIT_OP_LINK_REWIRE },
        { "link_remove", (lua_Integer)NMO_SCRIPT_EDIT_OP_LINK_REMOVE },
        { "param_add", (lua_Integer)NMO_SCRIPT_EDIT_OP_PARAM_ADD },
        { "param_set", (lua_Integer)NMO_SCRIPT_EDIT_OP_PARAM_SET },
        { "param_connect", (lua_Integer)NMO_SCRIPT_EDIT_OP_PARAM_CONNECT },
        { "param_disconnect", (lua_Integer)NMO_SCRIPT_EDIT_OP_PARAM_DISCONNECT },
        { "param_remove", (lua_Integer)NMO_SCRIPT_EDIT_OP_PARAM_REMOVE },
        { "operation_add", (lua_Integer)NMO_SCRIPT_EDIT_OP_OPERATION_ADD },
        { "operation_rewire", (lua_Integer)NMO_SCRIPT_EDIT_OP_OPERATION_REWIRE },
        { "operation_remove", (lua_Integer)NMO_SCRIPT_EDIT_OP_OPERATION_REMOVE },
        { "subgraph_fold", (lua_Integer)NMO_SCRIPT_EDIT_OP_SUBGRAPH_FOLD },
        { "validate", (lua_Integer)NMO_SCRIPT_EDIT_OP_VALIDATE },
    };
    nmo_lua_push_integer_table(state, entries, sizeof(entries) / sizeof(entries[0]));
}

static int nmo_lua_open_behavior_module(lua_State *state)
{
    static const nmo_lua_function_entry_t graph_functions[] = {
        { "build", nmo_lua_behavior_graph },
        { "find_owner", nmo_lua_behavior_graph_find_owner },
        { "incoming_control", nmo_lua_behavior_graph_incoming_control },
        { "outgoing_control", nmo_lua_behavior_graph_outgoing_control },
        { "parameter_sources", nmo_lua_behavior_graph_parameter_sources },
        { "parameter_destinations", nmo_lua_behavior_graph_parameter_destinations },
        { "resolve_handle", nmo_lua_behavior_graph_resolve_handle },
        { "validate_operation", nmo_lua_behavior_graph_validate_operation },
    };
    static const nmo_lua_function_entry_t functions[] = {
        { "script_count", nmo_lua_behavior_script_count },
        { "script_at", nmo_lua_behavior_script_at },
        { "script_from_id", nmo_lua_behavior_script_from_id },
        { "view", nmo_lua_behavior_view },
        { "inspect", nmo_lua_behavior_inspect },
        { "describe_boundary", nmo_lua_behavior_describe_boundary },
        { "trace_parameter_chain", nmo_lua_behavior_trace_parameter_chain },
        { "script_tree", nmo_lua_behavior_script_tree },
        { "execute", nmo_lua_behavior_execute },
        { "begin_edit", nmo_lua_behavior_begin_edit },
        { "validate", nmo_lua_behavior_validate },
        { "commit", nmo_lua_behavior_commit },
        { "mark", nmo_lua_behavior_mark },
        { "validate_interface_refs", nmo_lua_behavior_validate_interface_refs },
        { "apply_interface_policy", nmo_lua_behavior_apply_interface_policy },
        { "add_node", nmo_lua_behavior_add_node },
        { "remove_node", nmo_lua_behavior_remove_node },
        { "add_io", nmo_lua_behavior_add_io },
        { "rename_io", nmo_lua_behavior_rename_io },
        { "remove_io", nmo_lua_behavior_remove_io },
        { "add_parameter", nmo_lua_behavior_add_parameter },
        { "set_parameter_value", nmo_lua_behavior_set_parameter_value },
        { "set_parameter_bytes", nmo_lua_behavior_set_parameter_bytes },
        { "connect_parameter", nmo_lua_behavior_connect_parameter },
        { "disconnect_parameter", nmo_lua_behavior_disconnect_parameter },
        { "remove_parameter", nmo_lua_behavior_remove_parameter },
        { "add_link", nmo_lua_behavior_add_link },
        { "rewire_link", nmo_lua_behavior_rewire_link },
        { "set_link_delay", nmo_lua_behavior_set_link_delay },
        { "remove_link", nmo_lua_behavior_remove_link },
        { "add_operation", nmo_lua_behavior_add_operation },
        { "rewire_operation", nmo_lua_behavior_rewire_operation },
        { "remove_operation", nmo_lua_behavior_remove_operation },
        { "report", nmo_lua_behavior_report },
        { "rollback", nmo_lua_behavior_rollback },
    };
    const size_t graph_function_count =
        sizeof(graph_functions) / sizeof(graph_functions[0]);
    const size_t function_count = sizeof(functions) / sizeof(functions[0]);

    lua_createtable(state, 0, (int)(function_count + 5u));
    nmo_lua_set_functions(state, functions, function_count);

    lua_createtable(state, 0, (int)graph_function_count);
    nmo_lua_set_functions(state, graph_functions, graph_function_count);
    lua_setfield(state, -2, "graph");

    nmo_lua_behavior_push_validation_flags(state);
    lua_setfield(state, -2, "validation_flags");

    nmo_lua_behavior_push_operation_slot_flags(state);
    lua_setfield(state, -2, "operation_slot_flags");

    nmo_lua_behavior_push_graph_handle_kinds(state);
    lua_setfield(state, -2, "graph_handle_kinds");

    nmo_lua_behavior_push_graph_op_kinds(state);
    lua_setfield(state, -2, "graph_op_kinds");

    return 1;
}

nmo_status_t nmo_lua_register_behavior_bindings(nmo_lua_runtime_t *runtime)
{
    const nmo_lua_module_t module = {
        .name = "nmo.behavior",
        .open_fn = nmo_lua_open_behavior_module
    };

    return nmo_lua_runtime_register_module(runtime, &module);
}



