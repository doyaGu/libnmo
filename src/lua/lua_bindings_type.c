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

    nmo_context_t *context =
        handle != NULL && handle->document != NULL
        ? nmo_document_get_context(handle->document)
        : NULL;
    if (context == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Lua object handle document has no context");
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

static nmo_status_t nmo_lua_type_resolve_object_state_view(
    lua_State *state,
    int object_index,
    int ancestor_guid_index,
    nmo_type_registry_t **out_registry,
    const nmo_type_descriptor_t **out_type,
    void **out_state)
{
    nmo_object_t *object = NULL;
    nmo_type_registry_t *registry = NULL;
    const nmo_type_descriptor_t *type = NULL;
    void *state_view = NULL;
    nmo_status_t status =
        nmo_lua_type_get_registry_from_object(state, object_index, &object, &registry);
    if (status != NMO_OK) {
        return status;
    }

    if (!lua_isnoneornil(state, ancestor_guid_index)) {
        nmo_guid_t ancestor_guid = NMO_GUID_NULL;
        status = nmo_lua_type_parse_guid_arg(state, ancestor_guid_index, &ancestor_guid);
        if (status != NMO_OK) {
            return status;
        }

        type = nmo_type_query_find_by_guid(registry, ancestor_guid);
        if (type == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                             "Ancestor type GUID is not registered");
        }

        state_view =
            nmo_type_query_object_get_ancestor_state_by_guid(registry, object, ancestor_guid);
        if (state_view == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                             "Object does not expose the requested ancestor state");
        }
    } else {
        nmo_guid_t explicit_guid = nmo_object_get_type_guid(object);
        if (!nmo_guid_is_null(explicit_guid)) {
            type = nmo_type_query_find_by_guid(registry, explicit_guid);
        }
        if (type == NULL) {
            type = nmo_type_registry_find_by_class_id_inherited(
                registry, nmo_object_get_class_id(object));
        }
        if (type == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                             "Object type is not registered");
        }

        state_view = nmo_object_get_state(object);
        if (state_view == NULL) {
            NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                             "Object has no allocated state");
        }
    }

    if (out_registry != NULL) {
        *out_registry = registry;
    }
    if (out_type != NULL) {
        *out_type = type;
    }
    if (out_state != NULL) {
        *out_state = state_view;
    }

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

static void nmo_lua_type_set_guid_field(
    lua_State *state,
    const char *field_name,
    nmo_guid_t guid)
{
    nmo_lua_type_push_guid_string(state, guid);
    lua_setfield(state, -2, field_name);
}

static void nmo_lua_type_push_view(lua_State *state, const nmo_type_view_t *view)
{
    lua_createtable(state, 0, 13);

    nmo_lua_type_set_guid_field(state, "guid", view->guid);
    nmo_lua_type_set_guid_field(state, "base_guid", view->base_guid);
    nmo_lua_set_integer_field(state, "type_id", (lua_Integer)view->type_id);
    nmo_lua_set_integer_field(state, "class_id", (lua_Integer)view->class_id);
    nmo_lua_set_integer_field(state, "category", (lua_Integer)view->category);
    nmo_lua_set_integer_field(state, "flags", (lua_Integer)view->flags);
    nmo_lua_set_integer_field(state, "size", (lua_Integer)view->size);
    nmo_lua_set_integer_field(state, "alignment", (lua_Integer)view->alignment);
    nmo_lua_set_integer_field(state, "field_count", (lua_Integer)view->field_count);
    nmo_lua_set_optional_string_field(state, "name", view->name);
    nmo_lua_set_optional_string_field(state, "description", view->description);
    nmo_lua_set_boolean_field(state, "has_reflection", view->has_reflection);
    nmo_lua_set_boolean_field(state, "ui_visible", view->ui_visible);
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

static int nmo_lua_type_class_parent(lua_State *state)
{
    nmo_type_registry_t *registry = NULL;
    nmo_status_t status =
        nmo_lua_type_get_registry_from_context(state, 1, &registry);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid context handle");
    }

    nmo_class_id_t class_id = (nmo_class_id_t)luaL_checkinteger(state, 2);
    nmo_class_id_t parent_id =
        nmo_type_query_class_get_parent(registry, class_id);
    if (parent_id == 0) {
        lua_pushnil(state);
    } else {
        lua_pushinteger(state, (lua_Integer)parent_id);
    }
    return 1;
}

static int nmo_lua_type_class_is_derived_from(lua_State *state)
{
    nmo_type_registry_t *registry = NULL;
    nmo_status_t status =
        nmo_lua_type_get_registry_from_context(state, 1, &registry);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid context handle");
    }

    nmo_class_id_t class_id = (nmo_class_id_t)luaL_checkinteger(state, 2);
    nmo_class_id_t base_id = (nmo_class_id_t)luaL_checkinteger(state, 3);
    lua_pushboolean(
        state,
        nmo_type_query_class_is_derived_from(registry, class_id, base_id) ? 1 : 0);
    return 1;
}

static int nmo_lua_type_object_is_derived_from_guid(lua_State *state)
{
    nmo_object_t *object = NULL;
    nmo_type_registry_t *registry = NULL;
    nmo_guid_t guid = NMO_GUID_NULL;
    nmo_status_t status =
        nmo_lua_type_get_registry_from_object(state, 1, &object, &registry);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object handle");
    }

    status = nmo_lua_type_parse_guid_arg(state, 2, &guid);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid type GUID");
    }

    lua_pushboolean(state,
                    nmo_type_query_object_is_derived_from_guid(registry, object, guid)
                        ? 1
                        : 0);
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

static int nmo_lua_type_string_escape(lua_State *state)
{
    const char *text = luaL_checkstring(state, 1);
    char buffer[1024];
    size_t written = nmo_string_escape(text, buffer, sizeof(buffer));
    if (written == 0u && text[0] != '\0') {
        return luaL_error(state, "Failed to escape string");
    }
    lua_pushstring(state, buffer);
    return 1;
}

static int nmo_lua_type_string_unescape(lua_State *state)
{
    const char *text = luaL_checkstring(state, 1);
    char buffer[1024];
    size_t written = nmo_string_unescape(text, buffer, sizeof(buffer));
    if (written == 0u && text[0] != '\0' && strcmp(text, "\"\"") != 0) {
        return luaL_error(state, "Failed to unescape string");
    }
    lua_pushstring(state, buffer);
    return 1;
}

static int nmo_lua_type_get_field(lua_State *state)
{
    nmo_type_registry_t *registry = NULL;
    const nmo_type_descriptor_t *type = NULL;
    void *state_view = NULL;
    char value_buffer[1024];
    const char *field_name = luaL_checkstring(state, 2);
    nmo_status_t status = nmo_lua_type_resolve_object_state_view(
        state, 1, 3, &registry, &type, &state_view);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object field view");
    }

    status = nmo_type_get_field(
        state_view, type, registry, field_name, value_buffer, sizeof(value_buffer));
    if (status == NMO_ERR_NOT_FOUND) {
        lua_pushnil(state);
        return 1;
    }
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to read object field");
    }

    lua_pushstring(state, value_buffer);
    return 1;
}

static int nmo_lua_type_set_field(lua_State *state)
{
    nmo_type_registry_t *registry = NULL;
    const nmo_type_descriptor_t *type = NULL;
    void *state_view = NULL;
    const char *field_name = luaL_checkstring(state, 2);
    const char *value_text = luaL_checkstring(state, 3);
    nmo_status_t status = nmo_lua_type_resolve_object_state_view(
        state, 1, 4, &registry, &type, &state_view);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid object field view");
    }

    status = nmo_type_set_field(state_view, type, registry, field_name, value_text);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to write object field");
    }

    return 0;
}

static int nmo_lua_open_type_module(lua_State *state)
{
    static const nmo_lua_function_entry_t functions[] = {
        { "view_from_guid", nmo_lua_type_view_from_guid },
        { "view_from_class_id", nmo_lua_type_view_from_class_id },
        { "view_from_type_id", nmo_lua_type_view_from_type_id },
        { "view_from_object", nmo_lua_type_view_from_object },
        { "class_name", nmo_lua_type_class_name },
        { "class_id", nmo_lua_type_class_id },
        { "guid_from_name", nmo_lua_type_guid_from_name },
        { "class_parent", nmo_lua_type_class_parent },
        { "class_is_derived_from", nmo_lua_type_class_is_derived_from },
        { "object_is_derived_from_guid", nmo_lua_type_object_is_derived_from_guid },
        { "value_roundtrip", nmo_lua_type_value_roundtrip },
        { "get_field", nmo_lua_type_get_field },
        { "set_field", nmo_lua_type_set_field },
        { "string_escape", nmo_lua_type_string_escape },
        { "string_unescape", nmo_lua_type_string_unescape },
    };
    const size_t function_count = sizeof(functions) / sizeof(functions[0]);

    lua_createtable(state, 0, (int)function_count);
    nmo_lua_set_functions(state, functions, function_count);
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
