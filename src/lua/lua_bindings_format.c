#include "lua_bindings_internal.h"

#include "format/nmo_interface_view.h"

#include "lauxlib.h"

void nmo_lua_push_interface_body_view(lua_State *state,
                                      const nmo_interface_body_view_t *body)
{
    lua_createtable(state, 0, 16);

    nmo_lua_set_boolean_field(state, "has_body", body->has_body);
    nmo_lua_set_integer_field(state, "link_count", (lua_Integer)body->link_count);
    nmo_lua_set_integer_field(
        state, "operation_count", (lua_Integer)body->operation_count);
    nmo_lua_set_integer_field(state, "comment_count", (lua_Integer)body->comment_count);
    nmo_lua_set_integer_field(
        state, "local_param_count", (lua_Integer)body->local_param_count);
    nmo_lua_set_integer_field(
        state, "shared_param_count", (lua_Integer)body->shared_param_count);
    nmo_lua_set_boolean_field(state, "has_params", body->has_params);
    nmo_lua_set_boolean_field(state, "has_graph_io", body->has_graph_io);
    nmo_lua_set_boolean_field(state, "has_links_section", body->has_links_section);
    nmo_lua_set_boolean_field(
        state, "has_operations_section", body->has_operations_section);
    nmo_lua_set_boolean_field(
        state, "has_comments_section", body->has_comments_section);
    nmo_lua_set_boolean_field(
        state, "has_unknown_flag_section", body->has_unknown_flag_section);
    nmo_lua_set_integer_field(state, "unknown_flag", (lua_Integer)body->unknown_flag);
    nmo_lua_set_integer_field(
        state, "inward_input_count", (lua_Integer)body->inward_input_count);
    nmo_lua_set_integer_field(
        state, "outward_input_count", (lua_Integer)body->outward_input_count);
    nmo_lua_set_integer_field(
        state, "inward_output_count", (lua_Integer)body->inward_output_count);
    nmo_lua_set_integer_field(
        state, "outward_output_count", (lua_Integer)body->outward_output_count);
}

void nmo_lua_push_interface_view(lua_State *state,
                                 const nmo_interface_view_t *view)
{
    lua_createtable(state, 0, 10);

    nmo_lua_set_integer_field(
        state, "owner_behavior_id", (lua_Integer)view->owner_behavior_id);
    nmo_lua_set_integer_field(state, "behavior_id", (lua_Integer)view->behavior_id);
    nmo_lua_set_boolean_field(state, "is_root", view->is_root);
    nmo_lua_set_integer_field(state, "version", (lua_Integer)view->version);
    nmo_lua_set_integer_field(state, "format_flags", (lua_Integer)view->format_flags);
    nmo_lua_set_integer_field(state, "flags", (lua_Integer)view->flags);
    nmo_lua_set_integer_field(state, "depth", (lua_Integer)view->depth);
    nmo_lua_set_integer_field(
        state, "sub_behavior_count", (lua_Integer)view->sub_behavior_count);
    nmo_lua_set_boolean_field(state, "extra_present", view->extra_present);
    nmo_lua_set_integer_field(
        state, "extra_entry_count", (lua_Integer)view->extra_entry_count);
    nmo_lua_set_boolean_field(state, "has_snapshot", view->has_snapshot);

    nmo_lua_push_interface_body_view(state, &view->body);
    lua_setfield(state, -2, "body");
}

static int nmo_lua_format_interface_view(lua_State *state)
{
    nmo_document_t *document = NULL;
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    session = nmo_document_internal_session(document);
    if (session == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_INVALID_STATE,
                                        "Document has no backing session");
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
    nmo_document_t *document = NULL;
    nmo_session_t *session = NULL;
    nmo_status_t status =
        nmo_lua_check_document_handle(state, 1, &document, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid document handle");
    }

    session = nmo_document_internal_session(document);
    if (session == NULL) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_INVALID_STATE,
                                        "Document has no backing session");
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
    static const nmo_lua_function_entry_t functions[] = {
        { "interface_view", nmo_lua_format_interface_view },
        { "find_behavior", nmo_lua_format_find_interface_behavior },
    };
    const size_t function_count = sizeof(functions) / sizeof(functions[0]);

    lua_createtable(state, 0, (int)function_count);
    nmo_lua_set_functions(state, functions, function_count);
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

