#include "lua_bindings_internal.h"

#include "format/nmo_interface_view.h"

#include "lauxlib.h"

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

