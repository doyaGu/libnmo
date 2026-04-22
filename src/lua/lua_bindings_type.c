#include "lua_bindings_internal.h"

#include "core/nmo_guid.h"
#include "type/nmo_type_query.h"
#include "type/nmo_type_string.h"
#include "type/nmo_type_system.h"
#include "type/nmo_type_view.h"

#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"

static nmo_status_t nmo_lua_type_get_registry_from_context(
    lua_State *state,
    int index,
    nmo_type_registry_t **out_registry)
{
    nmo_context_t *context = NULL;
    nmo_status_t status = nmo_lua_check_context_handle(state, index, &context, NULL);
    if (status != NMO_OK) {
        return status;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(context);
    if (registry == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua context has no type registry");
    }

    *out_registry = registry;
    NMO_RETURN_OK();
}

static nmo_status_t nmo_lua_type_get_registry_from_object(
    lua_State *state,
    int index,
    nmo_object_t **out_object,
    nmo_type_registry_t **out_registry)
{
    nmo_lua_object_handle_data_t *handle = NULL;
    nmo_object_t *object = NULL;
    nmo_status_t status =
        nmo_lua_check_object_handle(state, index, &handle, &object);
    if (status != NMO_OK) {
        return status;
    }

    nmo_context_t *context = nmo_session_get_context(handle->session);
    if (context == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua object handle session has no context");
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(context);
    if (registry == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua object handle context has no type registry");
    }

    if (out_object != NULL) {
        *out_object = object;
    }
    if (out_registry != NULL) {
        *out_registry = registry;
    }

    NMO_RETURN_OK();
}

static nmo_status_t nmo_lua_type_parse_guid_arg(
    lua_State *state,
    int index,
    nmo_guid_t *out_guid)
{
    const char *guid_text = luaL_checkstring(state, index);
    nmo_guid_t guid = nmo_guid_parse(guid_text);
    if (nmo_guid_is_null(guid)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Invalid type GUID string '%s'", guid_text);
    }

    *out_guid = guid;
    NMO_RETURN_OK();
}

static void nmo_lua_type_push_guid_string(lua_State *state, nmo_guid_t guid)
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

static void nmo_lua_type_push_view(lua_State *state, const nmo_type_view_t *view)
{
    lua_createtable(state, 0, 13);

    nmo_lua_type_push_guid_string(state, view->guid);
    lua_setfield(state, -2, "guid");

    nmo_lua_type_push_guid_string(state, view->base_guid);
    lua_setfield(state, -2, "base_guid");

    lua_pushinteger(state, (lua_Integer)view->type_id);
    lua_setfield(state, -2, "type_id");

    lua_pushinteger(state, (lua_Integer)view->class_id);
    lua_setfield(state, -2, "class_id");

    lua_pushinteger(state, (lua_Integer)view->category);
    lua_setfield(state, -2, "category");

    lua_pushinteger(state, (lua_Integer)view->flags);
    lua_setfield(state, -2, "flags");

    lua_pushinteger(state, (lua_Integer)view->size);
    lua_setfield(state, -2, "size");

    lua_pushinteger(state, (lua_Integer)view->alignment);
    lua_setfield(state, -2, "alignment");

    lua_pushinteger(state, (lua_Integer)view->field_count);
    lua_setfield(state, -2, "field_count");

    if (view->name != NULL) {
        lua_pushstring(state, view->name);
    } else {
        lua_pushnil(state);
    }
    lua_setfield(state, -2, "name");

    if (view->description != NULL) {
        lua_pushstring(state, view->description);
    } else {
        lua_pushnil(state);
    }
    lua_setfield(state, -2, "description");

    lua_pushboolean(state, view->has_reflection ? 1 : 0);
    lua_setfield(state, -2, "has_reflection");

    lua_pushboolean(state, view->ui_visible ? 1 : 0);
    lua_setfield(state, -2, "ui_visible");
}

static int nmo_lua_type_view_from_guid(lua_State *state)
{
    nmo_type_registry_t *registry = NULL;
    nmo_guid_t guid = NMO_GUID_NULL;
    nmo_type_view_t view = {0};

    nmo_status_t status =
        nmo_lua_type_get_registry_from_context(state, 1, &registry);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid context handle");
    }

    status = nmo_lua_type_parse_guid_arg(state, 2, &guid);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid type GUID");
    }

    status = nmo_type_view_from_guid(registry, guid, &view);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to resolve type view");
    }

    nmo_lua_type_push_view(state, &view);
    return 1;
}

static int nmo_lua_type_view_from_class_id(lua_State *state)
{
    nmo_type_registry_t *registry = NULL;
    nmo_type_view_t view = {0};

    nmo_status_t status =
        nmo_lua_type_get_registry_from_context(state, 1, &registry);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid context handle");
    }

    nmo_class_id_t class_id = (nmo_class_id_t)luaL_checkinteger(state, 2);
    status = nmo_type_view_from_class_id(registry, class_id, &view);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to resolve class type view");
    }

    nmo_lua_type_push_view(state, &view);
    return 1;
}

static int nmo_lua_type_view_from_object(lua_State *state)
{
    nmo_object_t *object = NULL;
    nmo_type_registry_t *registry = NULL;
    nmo_type_view_t view = {0};

    nmo_status_t status =
        nmo_lua_type_get_registry_from_object(state, 1, &object, &registry);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object handle");
    }

    status = nmo_type_view_from_object(registry, object, &view);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to resolve object type view");
    }

    nmo_lua_type_push_view(state, &view);
    return 1;
}

static int nmo_lua_type_view_from_type_id(lua_State *state)
{
    nmo_type_registry_t *registry = NULL;
    nmo_type_view_t view = {0};
    nmo_type_id_t type_id = 0;

    nmo_status_t status =
        nmo_lua_type_get_registry_from_context(state, 1, &registry);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid context handle");
    }

    type_id = (nmo_type_id_t)luaL_checkinteger(state, 2);
    status = nmo_type_view_from_type_id(registry, type_id, &view);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to resolve type-id view");
    }

    nmo_lua_type_push_view(state, &view);
    return 1;
}

static int nmo_lua_type_class_name(lua_State *state)
{
    nmo_type_registry_t *registry = NULL;
    nmo_status_t status =
        nmo_lua_type_get_registry_from_context(state, 1, &registry);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid context handle");
    }

    nmo_class_id_t class_id = (nmo_class_id_t)luaL_checkinteger(state, 2);
    const char *name = nmo_type_query_class_name_from_id(registry, class_id);
    if (name == NULL) {
        lua_pushnil(state);
    } else {
        lua_pushstring(state, name);
    }

    return 1;
}

static int nmo_lua_type_class_id(lua_State *state)
{
    nmo_type_registry_t *registry = NULL;
    nmo_status_t status =
        nmo_lua_type_get_registry_from_context(state, 1, &registry);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid context handle");
    }

    const char *name = luaL_checkstring(state, 2);
    nmo_class_id_t class_id = nmo_type_query_class_id_from_name(registry, name);
    if (class_id == 0) {
        lua_pushnil(state);
    } else {
        lua_pushinteger(state, (lua_Integer)class_id);
    }

    return 1;
}

static int nmo_lua_type_guid_from_name(lua_State *state)
{
    nmo_type_registry_t *registry = NULL;
    nmo_status_t status =
        nmo_lua_type_get_registry_from_context(state, 1, &registry);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid context handle");
    }

    const char *name = luaL_checkstring(state, 2);
    nmo_guid_t guid = NMO_GUID_NULL;
    status = nmo_type_registry_name_to_guid(registry, name, &guid);
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to resolve type GUID");
    }

    nmo_lua_type_push_guid_string(state, guid);
    return 1;
}

static int nmo_lua_type_value_roundtrip(lua_State *state)
{
    nmo_type_registry_t *registry = NULL;
    nmo_guid_t guid = NMO_GUID_NULL;
    char stack_buffer[128];
    char value_buffer[512];
    void *value = NULL;
    const nmo_type_descriptor_t *type = NULL;

    nmo_status_t status =
        nmo_lua_type_get_registry_from_context(state, 1, &registry);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid context handle");
    }

    status = nmo_lua_type_parse_guid_arg(state, 2, &guid);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid type GUID");
    }

    type = nmo_type_query_find_by_guid(registry, guid);
    if (type == NULL || type->size == 0u) {
        return nmo_lua_raise_last_error(state,
                                        NMO_ERR_NOT_FOUND,
                                        "Type is not available for value roundtrip");
    }

    if (type->size <= sizeof(stack_buffer)) {
        value = stack_buffer;
        memset(value, 0, type->size);
    } else {
        value = calloc(1, type->size);
        if (value == NULL) {
            return nmo_lua_raise_last_error(state,
                                            NMO_ERR_NOMEM,
                                            "Failed to allocate typed value buffer");
        }
    }

    const char *source_text = luaL_checkstring(state, 3);
    status = nmo_type_value_from_string(value, type, registry, source_text);
    if (status == NMO_OK) {
        status = nmo_type_value_to_string(value, type, registry, value_buffer, sizeof(value_buffer));
    }

    if (value != stack_buffer) {
        free(value);
    }

    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Typed value roundtrip failed");
    }

    lua_pushstring(state, value_buffer);
    return 1;
}

static int nmo_lua_open_type_module(lua_State *state)
{
    lua_createtable(state, 0, 8);

    lua_pushcfunction(state, nmo_lua_type_view_from_guid);
    lua_setfield(state, -2, "view_from_guid");

    lua_pushcfunction(state, nmo_lua_type_view_from_class_id);
    lua_setfield(state, -2, "view_from_class_id");

    lua_pushcfunction(state, nmo_lua_type_view_from_type_id);
    lua_setfield(state, -2, "view_from_type_id");

    lua_pushcfunction(state, nmo_lua_type_view_from_object);
    lua_setfield(state, -2, "view_from_object");

    lua_pushcfunction(state, nmo_lua_type_class_name);
    lua_setfield(state, -2, "class_name");

    lua_pushcfunction(state, nmo_lua_type_class_id);
    lua_setfield(state, -2, "class_id");

    lua_pushcfunction(state, nmo_lua_type_guid_from_name);
    lua_setfield(state, -2, "guid_from_name");

    lua_pushcfunction(state, nmo_lua_type_value_roundtrip);
    lua_setfield(state, -2, "value_roundtrip");

    return 1;
}

nmo_status_t nmo_lua_register_type_bindings(nmo_lua_runtime_t *runtime)
{
    const nmo_lua_module_t module = {
        .name = "nmo.type",
        .open_fn = nmo_lua_open_type_module
    };

    return nmo_lua_runtime_register_module(runtime, &module);
}
