#include "lua_bindings_internal.h"

#include "behavior/nmo_behavior_view.h"
#include "behavior/nmo_script_trace_view.h"
#include "behavior/nmo_script_view.h"
#include "core/nmo_guid.h"

#include "lauxlib.h"

#include <string.h>

static void nmo_lua_behavior_push_script_view(lua_State *state,
                                              const nmo_script_view_t *view)
{
    lua_createtable(state, 0, 5);

    lua_pushinteger(state, (lua_Integer)view->script_id);
    lua_setfield(state, -2, "script_id");
    lua_pushinteger(state, (lua_Integer)view->owner_id);
    lua_setfield(state, -2, "owner_id");
    if (view->script_name != NULL) {
        lua_pushstring(state, view->script_name);
    } else {
        lua_pushnil(state);
    }
    lua_setfield(state, -2, "script_name");
    if (view->owner_name != NULL) {
        lua_pushstring(state, view->owner_name);
    } else {
        lua_pushnil(state);
    }
    lua_setfield(state, -2, "owner_name");
    lua_pushinteger(state, (lua_Integer)view->owner_class_id);
    lua_setfield(state, -2, "owner_class_id");
}

static void nmo_lua_behavior_push_view(lua_State *state,
                                       const nmo_behavior_view_t *view)
{
    lua_createtable(state, 0, 21);

    lua_pushinteger(state, (lua_Integer)view->behavior_id);
    lua_setfield(state, -2, "behavior_id");
    lua_pushinteger(state, (lua_Integer)view->class_id);
    lua_setfield(state, -2, "class_id");
    if (view->name != NULL) {
        lua_pushstring(state, view->name);
    } else {
        lua_pushnil(state);
    }
    lua_setfield(state, -2, "name");
    lua_pushinteger(state, (lua_Integer)view->flags);
    lua_setfield(state, -2, "flags");
    lua_pushboolean(state, view->is_building_block ? 1 : 0);
    lua_setfield(state, -2, "is_building_block");
    lua_pushboolean(state, view->has_target_parameter ? 1 : 0);
    lua_setfield(state, -2, "has_target_parameter");
    lua_pushinteger(state, (lua_Integer)view->target_parameter_id);
    lua_setfield(state, -2, "target_parameter_id");
    lua_pushinteger(state, (lua_Integer)view->sub_behavior_count);
    lua_setfield(state, -2, "sub_behavior_count");
    lua_pushinteger(state, (lua_Integer)view->link_count);
    lua_setfield(state, -2, "link_count");
    lua_pushinteger(state, (lua_Integer)view->operation_count);
    lua_setfield(state, -2, "operation_count");
    lua_pushinteger(state, (lua_Integer)view->input_count);
    lua_setfield(state, -2, "input_count");
    lua_pushinteger(state, (lua_Integer)view->output_count);
    lua_setfield(state, -2, "output_count");
    lua_pushinteger(state, (lua_Integer)view->in_parameter_count);
    lua_setfield(state, -2, "in_parameter_count");
    lua_pushinteger(state, (lua_Integer)view->out_parameter_count);
    lua_setfield(state, -2, "out_parameter_count");
    lua_pushinteger(state, (lua_Integer)view->local_parameter_count);
    lua_setfield(state, -2, "local_parameter_count");
    lua_pushboolean(state, view->owner_index_available ? 1 : 0);
    lua_setfield(state, -2, "owner_index_available");
    lua_pushboolean(state, view->edit_ready ? 1 : 0);
    lua_setfield(state, -2, "edit_ready");
    lua_pushinteger(state, (lua_Integer)view->edit_graph_status);
    lua_setfield(state, -2, "edit_graph_status");
    lua_pushboolean(state, view->has_interface ? 1 : 0);
    lua_setfield(state, -2, "has_interface");
    lua_pushboolean(state, view->interface_available ? 1 : 0);
    lua_setfield(state, -2, "interface_available");
    lua_pushinteger(state, (lua_Integer)view->interface_status);
    lua_setfield(state, -2, "interface_status");
}

static void nmo_lua_behavior_push_boundary_view(
    lua_State *state,
    const nmo_behavior_boundary_view_t *view)
{
    lua_createtable(state, 0, 8);

    lua_pushinteger(state, (lua_Integer)view->behavior_id);
    lua_setfield(state, -2, "behavior_id");
    lua_pushinteger(state, (lua_Integer)view->internal_node_count);
    lua_setfield(state, -2, "internal_node_count");
    lua_pushinteger(state, (lua_Integer)view->control_in_count);
    lua_setfield(state, -2, "control_in_count");
    lua_pushinteger(state, (lua_Integer)view->control_out_count);
    lua_setfield(state, -2, "control_out_count");
    lua_pushinteger(state, (lua_Integer)view->parameter_in_count);
    lua_setfield(state, -2, "parameter_in_count");
    lua_pushinteger(state, (lua_Integer)view->parameter_out_count);
    lua_setfield(state, -2, "parameter_out_count");
    lua_pushinteger(state, (lua_Integer)view->broken_links);
    lua_setfield(state, -2, "broken_links");
    lua_pushinteger(state, (lua_Integer)view->missing_nodes);
    lua_setfield(state, -2, "missing_nodes");
}

static const char *nmo_lua_behavior_step_kind_name(
    nmo_script_trace_step_kind_t kind)
{
    switch (kind) {
        case NMO_SCRIPT_TRACE_STEP_SHARED_SOURCE:
            return "shared_source";
        case NMO_SCRIPT_TRACE_STEP_DIRECT_SOURCE:
            return "direct_source";
        case NMO_SCRIPT_TRACE_STEP_START:
        default:
            return "start";
    }
}

static void nmo_lua_behavior_push_trace_chain_view(
    lua_State *state,
    const nmo_script_trace_chain_view_t *view)
{
    size_t i = 0u;

    lua_createtable(state, (int)view->step_count, 0);
    for (i = 0u; i < view->step_count; ++i) {
        lua_createtable(state, 0, 4);
        lua_pushinteger(state, (lua_Integer)view->steps[i].id);
        lua_setfield(state, -2, "id");
        lua_pushstring(state,
                       nmo_lua_behavior_step_kind_name(view->steps[i].step_kind));
        lua_setfield(state, -2, "step_type");
        lua_pushinteger(state, (lua_Integer)view->steps[i].owner_id);
        lua_setfield(state, -2, "owner_id");
        lua_pushinteger(state, (lua_Integer)view->steps[i].class_id);
        lua_setfield(state, -2, "class_id");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_behavior_push_script_tree_view(
    lua_State *state,
    const nmo_script_tree_view_t *view)
{
    size_t i = 0u;

    lua_createtable(state, (int)view->node_count, 0);
    for (i = 0u; i < view->node_count; ++i) {
        lua_createtable(state, 0, 5);
        lua_pushinteger(state, (lua_Integer)view->nodes[i].behavior_id);
        lua_setfield(state, -2, "behavior_id");
        lua_pushinteger(state, (lua_Integer)view->nodes[i].depth);
        lua_setfield(state, -2, "depth");
        lua_pushboolean(state, view->nodes[i].is_building_block ? 1 : 0);
        lua_setfield(state, -2, "is_building_block");
        if (view->nodes[i].name != NULL) {
            lua_pushstring(state, view->nodes[i].name);
        } else {
            lua_pushnil(state);
        }
        lua_setfield(state, -2, "name");
        lua_pushinteger(state, (lua_Integer)view->nodes[i].class_id);
        lua_setfield(state, -2, "class_id");
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_behavior_push_report(lua_State *state,
                                         const nmo_script_edit_report_t *report)
{
    lua_createtable(state, 0, 8);
    lua_pushinteger(state, (lua_Integer)report->created_objects);
    lua_setfield(state, -2, "created_objects");
    lua_pushinteger(state, (lua_Integer)report->deleted_objects);
    lua_setfield(state, -2, "deleted_objects");
    lua_pushinteger(state, (lua_Integer)report->changed_objects);
    lua_setfield(state, -2, "changed_objects");
    lua_pushinteger(state, (lua_Integer)report->moved_links);
    lua_setfield(state, -2, "moved_links");
    lua_pushinteger(state, (lua_Integer)report->rewired_parameters);
    lua_setfield(state, -2, "rewired_parameters");
    lua_pushinteger(state, (lua_Integer)report->interface_changes);
    lua_setfield(state, -2, "interface_changes");
    lua_pushinteger(state, (lua_Integer)report->warnings);
    lua_setfield(state, -2, "warnings");
    lua_pushinteger(state, (lua_Integer)report->errors);
    lua_setfield(state, -2, "errors");
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

static int nmo_lua_behavior_script_count(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    size_t count = 0;
    status = nmo_script_view_count(session, &count);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to count scripts");
    }

    lua_pushinteger(state, (lua_Integer)count);
    return 1;
}

static int nmo_lua_behavior_script_at(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    lua_Integer lua_index = luaL_checkinteger(state, 2);
    if (lua_index < 1) {
        return luaL_error(state, "script index must be 1-based");
    }

    nmo_script_view_t view = {0};
    status = nmo_script_view_at(session, (size_t)(lua_index - 1), &view);
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
    nmo_session_t *session = NULL;
    size_t count = 0u;
    size_t index = 0u;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    nmo_object_id_t script_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_script_view_t view = {0};
    status = nmo_script_view_from_script_id(session, script_id, &view);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK && script_id != 0u) {
        status = nmo_script_view_count(session, &count);
        if (status != NMO_OK) {
            return nmo_lua_raise_last_error(state, status, "Failed to inspect script");
        }

        for (index = 0u; index < count; ++index) {
            status = nmo_script_view_at(session, index, &view);
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
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    nmo_object_id_t behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_behavior_view_t view = {0};
    status = nmo_behavior_view_from_behavior(session, behavior_id, &view);
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

static int nmo_lua_behavior_describe_boundary(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    nmo_object_id_t behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    uint32_t max_depth = UINT32_MAX;
    if (!lua_isnoneornil(state, 3)) {
        max_depth = (uint32_t)luaL_checkinteger(state, 3);
    }

    nmo_behavior_boundary_view_t view = {0};
    status = nmo_behavior_view_describe_boundary(session, behavior_id, max_depth, &view);
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
    nmo_session_t *session = NULL;
    nmo_context_t *context = NULL;
    nmo_object_id_t parameter_id = 0u;
    uint32_t max_depth = 32u;
    nmo_script_trace_chain_view_t view;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    if (!lua_isnoneornil(state, 3)) {
        max_depth = (uint32_t)luaL_checkinteger(state, 3);
    }
    context = nmo_session_get_context(session);
    if (context == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_INVALID_STATE,
                                        "Lua session handle has no context");
    }

    memset(&view, 0, sizeof(view));
    status = nmo_script_trace_parameter_chain(
        context, session, parameter_id, max_depth, &view);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        nmo_script_trace_chain_view_destroy(&view);
        return nmo_lua_raise_last_error(state, status, "Failed to trace parameter chain");
    }

    nmo_lua_behavior_push_trace_chain_view(state, &view);
    nmo_script_trace_chain_view_destroy(&view);
    return 1;
}

static int nmo_lua_behavior_script_tree(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_context_t *context = NULL;
    nmo_object_id_t root_behavior_id = 0u;
    uint32_t max_depth = UINT32_MAX;
    nmo_script_tree_view_t view;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    root_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    if (!lua_isnoneornil(state, 3)) {
        max_depth = (uint32_t)luaL_checkinteger(state, 3);
    }
    context = nmo_session_get_context(session);
    if (context == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_INVALID_STATE,
                                        "Lua session handle has no context");
    }

    memset(&view, 0, sizeof(view));
    status = nmo_script_trace_script_tree(
        context, session, root_behavior_id, max_depth, &view);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        nmo_script_tree_view_destroy(&view);
        return nmo_lua_raise_last_error(state, status, "Failed to inspect script tree");
    }

    nmo_lua_behavior_push_script_tree_view(state, &view);
    nmo_script_tree_view_destroy(&view);
    return 1;
}

static int nmo_lua_behavior_begin_edit(lua_State *state)
{
    nmo_context_t *context = NULL;
    nmo_session_t *session = NULL;
    nmo_lua_handle_scope_t *session_scope = NULL;
    nmo_status_t status =
        nmo_lua_check_context_handle(state, 1, &context, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid context handle");
    }

    status = nmo_lua_check_session_handle(state, 2, &session, &session_scope);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    const char *label = luaL_optstring(state, 3, "lua-script-edit");
    nmo_script_edit_tx_t *tx = NULL;
    status = nmo_script_edit_begin(context, session, label, &tx);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to begin script edit");
    }

    status = nmo_lua_push_script_edit_tx_handle(state, tx, session_scope);
    if (status != NMO_OK) {
        nmo_script_edit_rollback(tx);
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

    const nmo_script_edit_report_t *report = nmo_script_edit_report(handle->tx);
    if (report == NULL) {
        return luaL_error(state, "script edit transaction report is unavailable");
    }

    nmo_lua_behavior_push_report(state, report);
    return 1;
}

static int nmo_lua_behavior_rollback(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    nmo_script_edit_rollback(handle->tx);
    return nmo_lua_behavior_invalidate_tx_handle(state, 1, handle);
}

static int nmo_lua_behavior_validate(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    uint32_t flags = NMO_SCRIPT_EDIT_VALIDATE_REFERENCES |
                     NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX |
                     NMO_SCRIPT_EDIT_VALIDATE_INTERFACE |
                     NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    flags = nmo_lua_behavior_optional_flags(state, 2, flags);
    status = nmo_script_edit_validate(handle->tx, flags);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Script validation failed");
    }
    return 0;
}

static int nmo_lua_behavior_mark(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    uint32_t flags = 0u;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    flags = (uint32_t)luaL_checkinteger(state, 2);
    nmo_script_edit_mark(handle->tx, flags);
    return 0;
}

static int nmo_lua_behavior_validate_interface_refs(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t behavior_id = 0u;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_script_edit_validate_interface_refs(handle->tx, behavior_id);
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
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_lua_behavior_parse_interface_mode(state, 3, &mode);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid interface mode");
    }

    status = nmo_script_edit_apply_interface_policy(handle->tx, behavior_id, mode);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to apply interface policy");
    }
    return 0;
}

static int nmo_lua_behavior_commit(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    status = nmo_script_edit_commit(handle->tx);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to commit script edit");
    }
    return nmo_lua_behavior_invalidate_tx_handle(state, 1, handle);
}

static int nmo_lua_behavior_add_node(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_guid_t bb_guid = NMO_GUID_NULL;
    nmo_object_id_t parent_behavior_id = 0u;
    nmo_object_id_t node_id = 0u;
    const char *name = NULL;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    parent_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_lua_behavior_parse_guid_arg(state, 3, &bb_guid);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid building-block GUID");
    }
    name = luaL_checkstring(state, 4);

    status = nmo_script_edit_add_node(handle->tx,
                                      parent_behavior_id,
                                      bb_guid,
                                      name,
                                      &node_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add behavior node");
    }

    lua_pushinteger(state, (lua_Integer)node_id);
    return 1;
}

static int nmo_lua_behavior_remove_node(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t parent_behavior_id = 0u;
    nmo_object_id_t node_id = 0u;
    uint32_t delete_flags = 0u;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    parent_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    node_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    delete_flags = nmo_lua_behavior_optional_flags(state, 4, 0u);
    status = nmo_script_edit_remove_node(handle->tx,
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
    nmo_object_id_t io_id = 0u;
    const char *name = NULL;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_lua_behavior_parse_io_kind(state, 3, &kind);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid io kind");
    }
    name = luaL_checkstring(state, 4);

    status = nmo_script_edit_add_io(handle->tx, behavior_id, kind, name, &io_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add io");
    }
    lua_pushinteger(state, (lua_Integer)io_id);
    return 1;
}

static int nmo_lua_behavior_rename_io(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t io_id = 0u;
    const char *name = NULL;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    io_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    name = luaL_checkstring(state, 3);
    status = nmo_script_edit_rename_io(handle->tx, io_id, name);
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
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    io_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    detach_links = lua_toboolean(state, 3);
    status = nmo_script_edit_remove_io(handle->tx, io_id, detach_links != 0);
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
    nmo_object_id_t parameter_id = 0u;
    const char *name = NULL;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
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

    status = nmo_script_edit_add_parameter(handle->tx,
                                           owner_behavior_id,
                                           kind,
                                           type_guid,
                                           name,
                                           &parameter_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add parameter");
    }
    lua_pushinteger(state, (lua_Integer)parameter_id);
    return 1;
}

static int nmo_lua_behavior_set_parameter_value(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t parameter_id = 0u;
    const char *value_text = NULL;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    value_text = luaL_checkstring(state, 3);
    status = nmo_script_edit_set_parameter_value(handle->tx, parameter_id, value_text);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to set parameter value");
    }
    return 0;
}

static int nmo_lua_behavior_set_parameter_bytes(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t parameter_id = 0u;
    size_t byte_count = 0u;
    const char *bytes = NULL;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }

    parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    bytes = luaL_checklstring(state, 3, &byte_count);
    status = nmo_script_edit_set_parameter_bytes(handle->tx,
                                                 parameter_id,
                                                 (const uint8_t *)bytes,
                                                 byte_count);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to set parameter bytes");
    }
    return 0;
}

static int nmo_lua_behavior_connect_parameter(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t source_parameter_id = 0u;
    nmo_object_id_t target_parameter_id = 0u;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    source_parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    target_parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    status = nmo_script_edit_connect_parameter(handle->tx,
                                               source_parameter_id,
                                               target_parameter_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to connect parameter");
    }
    return 0;
}

static int nmo_lua_behavior_disconnect_parameter(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t target_parameter_id = 0u;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    target_parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_script_edit_disconnect_parameter(handle->tx, target_parameter_id);
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
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    detach = lua_toboolean(state, 3);
    status = nmo_script_edit_remove_parameter(handle->tx, parameter_id, detach != 0);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to remove parameter");
    }
    return 0;
}

static int nmo_lua_behavior_add_link(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t parent_behavior_id = 0u;
    nmo_object_id_t from_io_id = 0u;
    nmo_object_id_t to_io_id = 0u;
    uint32_t activation_delay = 0u;
    nmo_object_id_t link_id = 0u;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    parent_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    from_io_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    to_io_id = (nmo_object_id_t)luaL_checkinteger(state, 4);
    activation_delay = nmo_lua_behavior_optional_flags(state, 5, 0u);
    status = nmo_script_edit_add_behavior_link(handle->tx,
                                               parent_behavior_id,
                                               from_io_id,
                                               to_io_id,
                                               activation_delay,
                                               &link_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add link");
    }
    lua_pushinteger(state, (lua_Integer)link_id);
    return 1;
}

static int nmo_lua_behavior_rewire_link(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t link_id = 0u;
    nmo_object_id_t from_io_id = 0u;
    nmo_object_id_t to_io_id = 0u;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    link_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    from_io_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    to_io_id = (nmo_object_id_t)luaL_checkinteger(state, 4);
    status = nmo_script_edit_rewire_behavior_link(handle->tx, link_id, from_io_id, to_io_id);
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
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    link_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    activation_delay = (uint32_t)luaL_checkinteger(state, 3);
    status = nmo_script_edit_set_behavior_link_delay(handle->tx, link_id, activation_delay);
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
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    parent_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    link_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    status = nmo_script_edit_remove_behavior_link(handle->tx, parent_behavior_id, link_id);
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
    nmo_object_id_t in1_parameter_id = 0u;
    nmo_object_id_t in2_parameter_id = 0u;
    nmo_object_id_t out_parameter_id = 0u;
    nmo_object_id_t operation_id = 0u;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    parent_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_lua_behavior_parse_guid_arg(state, 3, &operation_guid);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid operation GUID");
    }
    in1_parameter_id = nmo_lua_behavior_optional_object_id(state, 4);
    in2_parameter_id = nmo_lua_behavior_optional_object_id(state, 5);
    out_parameter_id = nmo_lua_behavior_optional_object_id(state, 6);
    status = nmo_script_edit_add_operation(handle->tx,
                                           parent_behavior_id,
                                           operation_guid,
                                           in1_parameter_id,
                                           in2_parameter_id,
                                           out_parameter_id,
                                           &operation_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add operation");
    }
    lua_pushinteger(state, (lua_Integer)operation_id);
    return 1;
}

static int nmo_lua_behavior_rewire_operation(lua_State *state)
{
    nmo_lua_script_edit_tx_handle_data_t *handle = NULL;
    nmo_object_id_t operation_id = 0u;
    uint32_t slot_flags = 0u;
    nmo_object_id_t in1_parameter_id = 0u;
    nmo_object_id_t in2_parameter_id = 0u;
    nmo_object_id_t out_parameter_id = 0u;
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    operation_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    slot_flags = (uint32_t)luaL_checkinteger(state, 3);
    in1_parameter_id = nmo_lua_behavior_optional_object_id(state, 4);
    in2_parameter_id = nmo_lua_behavior_optional_object_id(state, 5);
    out_parameter_id = nmo_lua_behavior_optional_object_id(state, 6);
    status = nmo_script_edit_rewire_operation(handle->tx,
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
    nmo_status_t status = nmo_lua_check_script_edit_tx_handle(state, 1, &handle);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid script edit handle");
    }
    operation_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    status = nmo_script_edit_remove_operation(handle->tx, operation_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to remove operation");
    }
    return 0;
}

static void nmo_lua_behavior_push_validation_flags(lua_State *state)
{
    lua_createtable(state, 0, 4);
    lua_pushinteger(state, (lua_Integer)NMO_SCRIPT_EDIT_VALIDATE_REFERENCES);
    lua_setfield(state, -2, "references");
    lua_pushinteger(state, (lua_Integer)NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX);
    lua_setfield(state, -2, "behavior_index");
    lua_pushinteger(state, (lua_Integer)NMO_SCRIPT_EDIT_VALIDATE_INTERFACE);
    lua_setfield(state, -2, "interface");
    lua_pushinteger(state, (lua_Integer)NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY);
    lua_setfield(state, -2, "roundtrip_ready");
}

static void nmo_lua_behavior_push_operation_slot_flags(lua_State *state)
{
    lua_createtable(state, 0, 3);
    lua_pushinteger(state, (lua_Integer)NMO_SCRIPT_EDIT_OP_SLOT_IN1);
    lua_setfield(state, -2, "in1");
    lua_pushinteger(state, (lua_Integer)NMO_SCRIPT_EDIT_OP_SLOT_IN2);
    lua_setfield(state, -2, "in2");
    lua_pushinteger(state, (lua_Integer)NMO_SCRIPT_EDIT_OP_SLOT_OUT);
    lua_setfield(state, -2, "out");
}

static int nmo_lua_open_behavior_module(lua_State *state)
{
    lua_createtable(state, 0, 29);

    lua_pushcfunction(state, nmo_lua_behavior_script_count);
    lua_setfield(state, -2, "script_count");

    lua_pushcfunction(state, nmo_lua_behavior_script_at);
    lua_setfield(state, -2, "script_at");

    lua_pushcfunction(state, nmo_lua_behavior_script_from_id);
    lua_setfield(state, -2, "script_from_id");

    lua_pushcfunction(state, nmo_lua_behavior_view);
    lua_setfield(state, -2, "view");

    lua_pushcfunction(state, nmo_lua_behavior_describe_boundary);
    lua_setfield(state, -2, "describe_boundary");

    lua_pushcfunction(state, nmo_lua_behavior_trace_parameter_chain);
    lua_setfield(state, -2, "trace_parameter_chain");

    lua_pushcfunction(state, nmo_lua_behavior_script_tree);
    lua_setfield(state, -2, "script_tree");

    lua_pushcfunction(state, nmo_lua_behavior_begin_edit);
    lua_setfield(state, -2, "begin_edit");

    lua_pushcfunction(state, nmo_lua_behavior_validate);
    lua_setfield(state, -2, "validate");

    lua_pushcfunction(state, nmo_lua_behavior_commit);
    lua_setfield(state, -2, "commit");

    lua_pushcfunction(state, nmo_lua_behavior_mark);
    lua_setfield(state, -2, "mark");

    lua_pushcfunction(state, nmo_lua_behavior_validate_interface_refs);
    lua_setfield(state, -2, "validate_interface_refs");

    lua_pushcfunction(state, nmo_lua_behavior_apply_interface_policy);
    lua_setfield(state, -2, "apply_interface_policy");

    lua_pushcfunction(state, nmo_lua_behavior_add_node);
    lua_setfield(state, -2, "add_node");

    lua_pushcfunction(state, nmo_lua_behavior_remove_node);
    lua_setfield(state, -2, "remove_node");

    lua_pushcfunction(state, nmo_lua_behavior_add_io);
    lua_setfield(state, -2, "add_io");

    lua_pushcfunction(state, nmo_lua_behavior_rename_io);
    lua_setfield(state, -2, "rename_io");

    lua_pushcfunction(state, nmo_lua_behavior_remove_io);
    lua_setfield(state, -2, "remove_io");

    lua_pushcfunction(state, nmo_lua_behavior_add_parameter);
    lua_setfield(state, -2, "add_parameter");

    lua_pushcfunction(state, nmo_lua_behavior_set_parameter_value);
    lua_setfield(state, -2, "set_parameter_value");

    lua_pushcfunction(state, nmo_lua_behavior_set_parameter_bytes);
    lua_setfield(state, -2, "set_parameter_bytes");

    lua_pushcfunction(state, nmo_lua_behavior_connect_parameter);
    lua_setfield(state, -2, "connect_parameter");

    lua_pushcfunction(state, nmo_lua_behavior_disconnect_parameter);
    lua_setfield(state, -2, "disconnect_parameter");

    lua_pushcfunction(state, nmo_lua_behavior_remove_parameter);
    lua_setfield(state, -2, "remove_parameter");

    lua_pushcfunction(state, nmo_lua_behavior_add_link);
    lua_setfield(state, -2, "add_link");

    lua_pushcfunction(state, nmo_lua_behavior_rewire_link);
    lua_setfield(state, -2, "rewire_link");

    lua_pushcfunction(state, nmo_lua_behavior_set_link_delay);
    lua_setfield(state, -2, "set_link_delay");

    lua_pushcfunction(state, nmo_lua_behavior_remove_link);
    lua_setfield(state, -2, "remove_link");

    lua_pushcfunction(state, nmo_lua_behavior_add_operation);
    lua_setfield(state, -2, "add_operation");

    lua_pushcfunction(state, nmo_lua_behavior_rewire_operation);
    lua_setfield(state, -2, "rewire_operation");

    lua_pushcfunction(state, nmo_lua_behavior_remove_operation);
    lua_setfield(state, -2, "remove_operation");

    lua_pushcfunction(state, nmo_lua_behavior_report);
    lua_setfield(state, -2, "report");

    lua_pushcfunction(state, nmo_lua_behavior_rollback);
    lua_setfield(state, -2, "rollback");

    nmo_lua_behavior_push_validation_flags(state);
    lua_setfield(state, -2, "validation_flags");

    nmo_lua_behavior_push_operation_slot_flags(state);
    lua_setfield(state, -2, "operation_slot_flags");

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
