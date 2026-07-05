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

static nmo_status_t nmo_lua_type_get_registry_and_type_from_guid(
    lua_State *state,
    int context_index,
    int guid_index,
    uint32_t category_mask,
    nmo_type_registry_t **out_registry,
    const nmo_type_descriptor_t **out_type)
{
    nmo_type_registry_t *registry = NULL;
    nmo_guid_t guid = NMO_GUID_NULL;
    const nmo_type_descriptor_t *type = NULL;
    nmo_status_t status =
        nmo_lua_type_get_registry_from_context(state, context_index, &registry);
    if (status != NMO_OK) {
        return status;
    }

    status = nmo_lua_type_parse_guid_arg(state, guid_index, &guid);
    if (status != NMO_OK) {
        return status;
    }

    type = nmo_type_query_find_by_guid(registry, guid);
    if (type == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND, NMO_SEVERITY_ERROR,
                         "Type GUID is not registered");
    }
    if (category_mask != 0u && !(type->category & category_mask)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Type category does not match the requested converter");
    }

    if (out_registry != NULL) {
        *out_registry = registry;
    }
    if (out_type != NULL) {
        *out_type = type;
    }
    NMO_RETURN_OK();
}

static nmo_status_t nmo_lua_type_check_optional_session(
    lua_State *state,
    int index,
    nmo_session_t **out_session)
{
    if (out_session == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Output session pointer must be non-null");
    }

    if (lua_gettop(state) < index || lua_isnoneornil(state, index)) {
        *out_session = NULL;
        NMO_RETURN_OK();
    }

    return nmo_lua_check_session_handle(state, index, out_session, NULL);
}

static nmo_status_t nmo_lua_type_collect_float_array(lua_State *state,
                                                     int index,
                                                     size_t expected_count,
                                                     float *out_values)
{
    int table_index = lua_absindex(state, index);
    size_t i = 0u;
    if (!lua_istable(state, table_index) || out_values == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Expected numeric array table");
    }

    if ((size_t)lua_rawlen(state, table_index) != expected_count) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Expected array of %zu numeric values", expected_count);
    }

    for (i = 0u; i < expected_count; ++i) {
        lua_geti(state, table_index, (lua_Integer)i + 1);
        if (!lua_isnumber(state, -1)) {
            lua_pop(state, 1);
            NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                             "Expected array of %zu numeric values", expected_count);
        }
        out_values[i] = (float)lua_tonumber(state, -1);
        lua_pop(state, 1);
    }

    NMO_RETURN_OK();
}

static void nmo_lua_type_push_float_array(lua_State *state,
                                          const float *values,
                                          size_t count)
{
    size_t i = 0u;
    lua_createtable(state, (int)count, 0);
    for (i = 0u; i < count; ++i) {
        lua_pushnumber(state, values[i]);
        lua_rawseti(state, -2, (lua_Integer)i + 1);
    }
}

static void nmo_lua_type_push_matrix_array(lua_State *state,
                                           const nmo_matrix_t *matrix)
{
    int row = 0;
    int col = 0;
    int index = 1;
    lua_createtable(state, 16, 0);
    for (row = 0; row < 4; ++row) {
        for (col = 0; col < 4; ++col) {
            lua_pushnumber(state, matrix->m[row][col]);
            lua_rawseti(state, -2, index++);
        }
    }
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

static void nmo_lua_type_set_integer_field(
    lua_State *state,
    const char *field_name,
    lua_Integer value)
{
    lua_pushinteger(state, value);
    lua_setfield(state, -2, field_name);
}

static void nmo_lua_type_set_boolean_field(
    lua_State *state,
    const char *field_name,
    bool value)
{
    lua_pushboolean(state, value ? 1 : 0);
    lua_setfield(state, -2, field_name);
}

static void nmo_lua_type_set_optional_string_field(
    lua_State *state,
    const char *field_name,
    const char *value)
{
    if (value != NULL) {
        lua_pushstring(state, value);
    } else {
        lua_pushnil(state);
    }
    lua_setfield(state, -2, field_name);
}

static void nmo_lua_type_push_view(lua_State *state, const nmo_type_view_t *view)
{
    lua_createtable(state, 0, 13);

    nmo_lua_type_set_guid_field(state, "guid", view->guid);
    nmo_lua_type_set_guid_field(state, "base_guid", view->base_guid);
    nmo_lua_type_set_integer_field(state, "type_id", (lua_Integer)view->type_id);
    nmo_lua_type_set_integer_field(state, "class_id", (lua_Integer)view->class_id);
    nmo_lua_type_set_integer_field(state, "category", (lua_Integer)view->category);
    nmo_lua_type_set_integer_field(state, "flags", (lua_Integer)view->flags);
    nmo_lua_type_set_integer_field(state, "size", (lua_Integer)view->size);
    nmo_lua_type_set_integer_field(state, "alignment", (lua_Integer)view->alignment);
    nmo_lua_type_set_integer_field(state, "field_count", (lua_Integer)view->field_count);
    nmo_lua_type_set_optional_string_field(state, "name", view->name);
    nmo_lua_type_set_optional_string_field(state, "description", view->description);
    nmo_lua_type_set_boolean_field(state, "has_reflection", view->has_reflection);
    nmo_lua_type_set_boolean_field(state, "ui_visible", view->ui_visible);
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

static int nmo_lua_type_float_to_string(lua_State *state)
{
    char buffer[64];
    float value = (float)luaL_checknumber(state, 1);
    nmo_status_t status = nmo_float_to_string(&value, buffer, sizeof(buffer));
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to convert float to string");
    }
    lua_pushstring(state, buffer);
    return 1;
}

static int nmo_lua_type_float_from_string(lua_State *state)
{
    float value = 0.0f;
    const char *text = luaL_checkstring(state, 1);
    nmo_status_t status = nmo_float_from_string(&value, text);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to parse float string");
    }
    lua_pushnumber(state, value);
    return 1;
}

static int nmo_lua_type_int_to_string(lua_State *state)
{
    char buffer[64];
    int32_t value = (int32_t)luaL_checkinteger(state, 1);
    bool use_hex = lua_gettop(state) >= 2 && lua_toboolean(state, 2) != 0;
    nmo_status_t status = nmo_int_to_string(&value, buffer, sizeof(buffer), use_hex);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to convert int to string");
    }
    lua_pushstring(state, buffer);
    return 1;
}

static int nmo_lua_type_int_from_string(lua_State *state)
{
    int32_t value = 0;
    const char *text = luaL_checkstring(state, 1);
    nmo_status_t status = nmo_int_from_string(&value, text);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to parse int string");
    }
    lua_pushinteger(state, (lua_Integer)value);
    return 1;
}

static int nmo_lua_type_bool_to_string(lua_State *state)
{
    char buffer[64];
    bool value = lua_toboolean(state, 1) != 0;
    nmo_status_t status = nmo_bool_to_string(&value, buffer, sizeof(buffer));
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to convert bool to string");
    }
    lua_pushstring(state, buffer);
    return 1;
}

static int nmo_lua_type_bool_from_string(lua_State *state)
{
    bool value = false;
    const char *text = luaL_checkstring(state, 1);
    nmo_status_t status = nmo_bool_from_string(&value, text);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to parse bool string");
    }
    lua_pushboolean(state, value ? 1 : 0);
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

static int nmo_lua_type_object_id_to_string(lua_State *state)
{
    char buffer[256];
    nmo_object_id_t id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    nmo_session_t *session = NULL;
    nmo_status_t status = nmo_lua_type_check_optional_session(state, 2, &session);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    status = nmo_object_id_to_string(&id, buffer, sizeof(buffer), session);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to convert object id to string");
    }
    lua_pushstring(state, buffer);
    return 1;
}

static int nmo_lua_type_object_id_from_string(lua_State *state)
{
    nmo_object_id_t id = 0u;
    const char *text = luaL_checkstring(state, 1);
    nmo_session_t *session = NULL;
    nmo_status_t status = nmo_lua_type_check_optional_session(state, 2, &session);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid session handle");
    }

    status = nmo_object_id_from_string(&id, text, session);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to parse object id string");
    }
    lua_pushinteger(state, (lua_Integer)id);
    return 1;
}

static int nmo_lua_type_enum_to_string(lua_State *state)
{
    nmo_type_registry_t *registry = NULL;
    const nmo_type_descriptor_t *type = NULL;
    char buffer[256];
    int32_t value = (int32_t)luaL_checkinteger(state, 3);
    bool use_name = lua_gettop(state) < 4 || lua_toboolean(state, 4) != 0;
    nmo_status_t status = nmo_lua_type_get_registry_and_type_from_guid(
        state, 1, 2, NMO_TYPE_CATEGORY_ENUM, &registry, &type);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid enum type");
    }

    status = nmo_enum_to_string(&value, type, registry, buffer, sizeof(buffer), use_name);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to convert enum to string");
    }
    lua_pushstring(state, buffer);
    return 1;
}

static int nmo_lua_type_enum_from_string(lua_State *state)
{
    nmo_type_registry_t *registry = NULL;
    const nmo_type_descriptor_t *type = NULL;
    int32_t value = 0;
    const char *text = luaL_checkstring(state, 3);
    nmo_status_t status = nmo_lua_type_get_registry_and_type_from_guid(
        state, 1, 2, NMO_TYPE_CATEGORY_ENUM, &registry, &type);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid enum type");
    }

    status = nmo_enum_from_string(&value, type, registry, text);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to parse enum string");
    }
    lua_pushinteger(state, (lua_Integer)value);
    return 1;
}

static int nmo_lua_type_flags_to_string(lua_State *state)
{
    nmo_type_registry_t *registry = NULL;
    const nmo_type_descriptor_t *type = NULL;
    char buffer[256];
    uint32_t value = (uint32_t)luaL_checkinteger(state, 3);
    bool use_names = lua_gettop(state) < 4 || lua_toboolean(state, 4) != 0;
    nmo_status_t status = nmo_lua_type_get_registry_and_type_from_guid(
        state, 1, 2, NMO_TYPE_CATEGORY_FLAGS, &registry, &type);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid flags type");
    }

    status = nmo_flags_to_string(&value, type, registry, buffer, sizeof(buffer), use_names);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to convert flags to string");
    }
    lua_pushstring(state, buffer);
    return 1;
}

static int nmo_lua_type_flags_from_string(lua_State *state)
{
    nmo_type_registry_t *registry = NULL;
    const nmo_type_descriptor_t *type = NULL;
    uint32_t value = 0u;
    const char *text = luaL_checkstring(state, 3);
    nmo_status_t status = nmo_lua_type_get_registry_and_type_from_guid(
        state, 1, 2, NMO_TYPE_CATEGORY_FLAGS, &registry, &type);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid flags type");
    }

    status = nmo_flags_from_string(&value, type, registry, text);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to parse flags string");
    }
    lua_pushinteger(state, (lua_Integer)value);
    return 1;
}

typedef nmo_status_t (*nmo_lua_type_float_array_to_string_fn)(
    const void *value,
    char *buffer,
    size_t buffer_size);

typedef nmo_status_t (*nmo_lua_type_float_array_from_string_fn)(
    void *value,
    const char *string);

static int nmo_lua_type_float_array_to_string(
    lua_State *state,
    size_t value_count,
    nmo_lua_type_float_array_to_string_fn to_string,
    const char *invalid_message,
    const char *conversion_message)
{
    char buffer[128];
    float value[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_status_t status = nmo_lua_type_collect_float_array(
        state, 1, value_count, value);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, invalid_message);
    }

    status = to_string(value, buffer, sizeof(buffer));
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, conversion_message);
    }
    lua_pushstring(state, buffer);
    return 1;
}

static int nmo_lua_type_float_array_from_string(
    lua_State *state,
    size_t value_count,
    nmo_lua_type_float_array_from_string_fn from_string,
    const char *parse_message)
{
    float value[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const char *text = luaL_checkstring(state, 1);
    nmo_status_t status = from_string(value, text);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, parse_message);
    }
    nmo_lua_type_push_float_array(state, value, value_count);
    return 1;
}

static int nmo_lua_type_vector2_to_string(lua_State *state)
{
    return nmo_lua_type_float_array_to_string(
        state, 2u, nmo_vector2_to_string,
        "Invalid vector2 value",
        "Failed to convert vector2 to string");
}

static int nmo_lua_type_vector2_from_string(lua_State *state)
{
    return nmo_lua_type_float_array_from_string(
        state, 2u, nmo_vector2_from_string,
        "Failed to parse vector2 string");
}

static int nmo_lua_type_vector_to_string(lua_State *state)
{
    return nmo_lua_type_float_array_to_string(
        state, 3u, nmo_vector_to_string,
        "Invalid vector value",
        "Failed to convert vector to string");
}

static int nmo_lua_type_vector_from_string(lua_State *state)
{
    return nmo_lua_type_float_array_from_string(
        state, 3u, nmo_vector_from_string,
        "Failed to parse vector string");
}

static int nmo_lua_type_vector4_to_string(lua_State *state)
{
    return nmo_lua_type_float_array_to_string(
        state, 4u, nmo_vector4_to_string,
        "Invalid vector4 value",
        "Failed to convert vector4 to string");
}

static int nmo_lua_type_vector4_from_string(lua_State *state)
{
    return nmo_lua_type_float_array_from_string(
        state, 4u, nmo_vector4_from_string,
        "Failed to parse vector4 string");
}

static int nmo_lua_type_quaternion_to_string(lua_State *state)
{
    return nmo_lua_type_float_array_to_string(
        state, 4u, nmo_quaternion_to_string,
        "Invalid quaternion value",
        "Failed to convert quaternion to string");
}

static int nmo_lua_type_quaternion_from_string(lua_State *state)
{
    return nmo_lua_type_float_array_from_string(
        state, 4u, nmo_quaternion_from_string,
        "Failed to parse quaternion string");
}

static int nmo_lua_type_matrix_to_string(lua_State *state)
{
    char buffer[256];
    float values[16] = {0};
    nmo_matrix_t matrix = {0};
    size_t i = 0u;
    nmo_status_t status = nmo_lua_type_collect_float_array(state, 1, 16u, values);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid matrix value");
    }

    for (i = 0u; i < 16u; ++i) {
        matrix.m[i / 4u][i % 4u] = values[i];
    }

    status = nmo_matrix_to_string(&matrix, buffer, sizeof(buffer));
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to convert matrix to string");
    }
    lua_pushstring(state, buffer);
    return 1;
}

static int nmo_lua_type_matrix_from_string(lua_State *state)
{
    nmo_matrix_t matrix = {0};
    const char *text = luaL_checkstring(state, 1);
    nmo_status_t status = nmo_matrix_from_string(&matrix, text);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to parse matrix string");
    }
    nmo_lua_type_push_matrix_array(state, &matrix);
    return 1;
}

static int nmo_lua_type_color_to_string(lua_State *state)
{
    return nmo_lua_type_float_array_to_string(
        state, 4u, nmo_color_to_string,
        "Invalid color value",
        "Failed to convert color to string");
}

static int nmo_lua_type_color_from_string(lua_State *state)
{
    return nmo_lua_type_float_array_from_string(
        state, 4u, nmo_color_from_string,
        "Failed to parse color string");
}

static int nmo_lua_type_string_to_string(lua_State *state)
{
    char buffer[1024];
    const char *value = NULL;
    if (!lua_isnoneornil(state, 1)) {
        value = luaL_checkstring(state, 1);
    }
    nmo_status_t status = nmo_string_to_string(&value, buffer, sizeof(buffer));
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to convert string value");
    }
    lua_pushstring(state, buffer);
    return 1;
}

static int nmo_lua_type_string_from_string(lua_State *state)
{
    const char *text = luaL_checkstring(state, 1);
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    const char *value = NULL;
    nmo_status_t status = NMO_OK;
    if (arena == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_NOMEM, "Failed to allocate string arena");
    }

    status = nmo_string_from_string(&value, text, arena);
    if (status == NMO_OK) {
        lua_pushstring(state, value != NULL ? value : "");
    }
    nmo_arena_destroy(arena);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to parse string literal");
    }
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

typedef struct nmo_lua_type_function_entry {
    const char *name;
    lua_CFunction fn;
} nmo_lua_type_function_entry_t;

static void nmo_lua_type_set_functions(
    lua_State *state,
    const nmo_lua_type_function_entry_t *entries,
    size_t count)
{
    size_t i = 0u;
    for (i = 0u; i < count; ++i) {
        lua_pushcfunction(state, entries[i].fn);
        lua_setfield(state, -2, entries[i].name);
    }
}

static int nmo_lua_open_type_module(lua_State *state)
{
    static const nmo_lua_type_function_entry_t functions[] = {
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
        { "float_to_string", nmo_lua_type_float_to_string },
        { "float_from_string", nmo_lua_type_float_from_string },
        { "int_to_string", nmo_lua_type_int_to_string },
        { "int_from_string", nmo_lua_type_int_from_string },
        { "bool_to_string", nmo_lua_type_bool_to_string },
        { "bool_from_string", nmo_lua_type_bool_from_string },
        { "string_escape", nmo_lua_type_string_escape },
        { "string_unescape", nmo_lua_type_string_unescape },
        { "object_id_to_string", nmo_lua_type_object_id_to_string },
        { "object_id_from_string", nmo_lua_type_object_id_from_string },
        { "enum_to_string", nmo_lua_type_enum_to_string },
        { "enum_from_string", nmo_lua_type_enum_from_string },
        { "flags_to_string", nmo_lua_type_flags_to_string },
        { "flags_from_string", nmo_lua_type_flags_from_string },
        { "vector2_to_string", nmo_lua_type_vector2_to_string },
        { "vector2_from_string", nmo_lua_type_vector2_from_string },
        { "vector_to_string", nmo_lua_type_vector_to_string },
        { "vector_from_string", nmo_lua_type_vector_from_string },
        { "vector4_to_string", nmo_lua_type_vector4_to_string },
        { "vector4_from_string", nmo_lua_type_vector4_from_string },
        { "quaternion_to_string", nmo_lua_type_quaternion_to_string },
        { "quaternion_from_string", nmo_lua_type_quaternion_from_string },
        { "matrix_to_string", nmo_lua_type_matrix_to_string },
        { "matrix_from_string", nmo_lua_type_matrix_from_string },
        { "color_to_string", nmo_lua_type_color_to_string },
        { "color_from_string", nmo_lua_type_color_from_string },
        { "string_to_string", nmo_lua_type_string_to_string },
        { "string_from_string", nmo_lua_type_string_from_string },
    };
    const size_t function_count = sizeof(functions) / sizeof(functions[0]);

    lua_createtable(state, 0, (int)function_count);
    nmo_lua_type_set_functions(state, functions, function_count);
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
