#include "lua_bindings_internal.h"

#include "format/nmo_interface_view.h"

#include "lauxlib.h"

static void nmo_lua_push_interface_body_view(lua_State *state,
                                             const nmo_interface_body_view_t *body)
{
    lua_createtable(state, 0, 16);

    lua_pushboolean(state, body->has_body ? 1 : 0);
    lua_setfield(state, -2, "has_body");
    lua_pushinteger(state, (lua_Integer)body->link_count);
    lua_setfield(state, -2, "link_count");
    lua_pushinteger(state, (lua_Integer)body->operation_count);
    lua_setfield(state, -2, "operation_count");
    lua_pushinteger(state, (lua_Integer)body->comment_count);
    lua_setfield(state, -2, "comment_count");
    lua_pushinteger(state, (lua_Integer)body->local_param_count);
    lua_setfield(state, -2, "local_param_count");
    lua_pushinteger(state, (lua_Integer)body->shared_param_count);
    lua_setfield(state, -2, "shared_param_count");
    lua_pushboolean(state, body->has_params ? 1 : 0);
    lua_setfield(state, -2, "has_params");
    lua_pushboolean(state, body->has_graph_io ? 1 : 0);
    lua_setfield(state, -2, "has_graph_io");
    lua_pushboolean(state, body->has_links_section ? 1 : 0);
    lua_setfield(state, -2, "has_links_section");
    lua_pushboolean(state, body->has_operations_section ? 1 : 0);
    lua_setfield(state, -2, "has_operations_section");
    lua_pushboolean(state, body->has_comments_section ? 1 : 0);
    lua_setfield(state, -2, "has_comments_section");
    lua_pushboolean(state, body->has_unknown_flag_section ? 1 : 0);
    lua_setfield(state, -2, "has_unknown_flag_section");
    lua_pushinteger(state, (lua_Integer)body->unknown_flag);
    lua_setfield(state, -2, "unknown_flag");
    lua_pushinteger(state, (lua_Integer)body->inward_input_count);
    lua_setfield(state, -2, "inward_input_count");
    lua_pushinteger(state, (lua_Integer)body->outward_input_count);
    lua_setfield(state, -2, "outward_input_count");
    lua_pushinteger(state, (lua_Integer)body->inward_output_count);
    lua_setfield(state, -2, "inward_output_count");
    lua_pushinteger(state, (lua_Integer)body->outward_output_count);
    lua_setfield(state, -2, "outward_output_count");
}

static void nmo_lua_push_interface_view(lua_State *state,
                                        const nmo_interface_view_t *view)
{
    lua_createtable(state, 0, 10);

    lua_pushinteger(state, (lua_Integer)view->owner_behavior_id);
    lua_setfield(state, -2, "owner_behavior_id");
    lua_pushinteger(state, (lua_Integer)view->behavior_id);
    lua_setfield(state, -2, "behavior_id");
    lua_pushboolean(state, view->is_root ? 1 : 0);
    lua_setfield(state, -2, "is_root");
    lua_pushinteger(state, (lua_Integer)view->version);
    lua_setfield(state, -2, "version");
    lua_pushinteger(state, (lua_Integer)view->format_flags);
    lua_setfield(state, -2, "format_flags");
    lua_pushinteger(state, (lua_Integer)view->flags);
    lua_setfield(state, -2, "flags");
    lua_pushinteger(state, (lua_Integer)view->depth);
    lua_setfield(state, -2, "depth");
    lua_pushinteger(state, (lua_Integer)view->sub_behavior_count);
    lua_setfield(state, -2, "sub_behavior_count");
    lua_pushboolean(state, view->extra_present ? 1 : 0);
    lua_setfield(state, -2, "extra_present");
    lua_pushinteger(state, (lua_Integer)view->extra_entry_count);
    lua_setfield(state, -2, "extra_entry_count");
    lua_pushboolean(state, view->has_snapshot ? 1 : 0);
    lua_setfield(state, -2, "has_snapshot");

    nmo_lua_push_interface_body_view(state, &view->body);
    lua_setfield(state, -2, "body");
}

static int nmo_lua_format_interface_view(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    nmo_object_id_t owner_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_interface_view_t view = {0};
    status = nmo_interface_view_from_behavior(session, owner_behavior_id, &view);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to inspect interface");
    }

    nmo_lua_push_interface_view(state, &view);
    return 1;
}

static int nmo_lua_format_find_interface_behavior(lua_State *state)
{
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_session_handle(state, 1, &session, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    nmo_object_id_t owner_behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_object_id_t behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    nmo_interface_view_t view = {0};
    if (behavior_id == owner_behavior_id) {
        status = nmo_interface_view_from_behavior(session, owner_behavior_id, &view);
    } else {
        status = nmo_interface_view_find_behavior(session,
                                                  owner_behavior_id,
                                                  behavior_id,
                                                  &view);
    }
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to inspect nested interface behavior");
    }

    nmo_lua_push_interface_view(state, &view);
    return 1;
}

static int nmo_lua_open_format_module(lua_State *state)
{
    lua_createtable(state, 0, 2);

    lua_pushcfunction(state, nmo_lua_format_interface_view);
    lua_setfield(state, -2, "interface_view");

    lua_pushcfunction(state, nmo_lua_format_find_interface_behavior);
    lua_setfield(state, -2, "find_behavior");

    return 1;
}

nmo_status_t nmo_lua_register_format_bindings(nmo_lua_runtime_t *runtime)
{
    const nmo_lua_module_t module = {
        .name = "nmo.format",
        .open_fn = nmo_lua_open_format_module
    };

    return nmo_lua_runtime_register_module(runtime, &module);
}
