#include "lua_bindings_internal.h"

#include "behavior/nmo_edit_plan.h"
#include "core/nmo_guid.h"
#include "lua/nmo_lua_fold_map_parser.h"
#include "lua/nmo_lua_runtime.h"

#include "lauxlib.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

const nmo_lua_handle_descriptor_t NMO_LUA_EDIT_PLAN_HANDLE_DESCRIPTOR = {
    .metatable_name = "nmo.edit_plan",
    .debug_name = "edit_plan",
};

typedef struct nmo_lua_plan_function_entry {
    const char *name;
    lua_CFunction fn;
} nmo_lua_plan_function_entry_t;

static void nmo_lua_plan_set_functions(
    lua_State *state,
    const nmo_lua_plan_function_entry_t *entries,
    size_t count)
{
    size_t i = 0u;
    for (i = 0u; i < count; ++i) {
        lua_pushcfunction(state, entries[i].fn);
        lua_setfield(state, -2, entries[i].name);
    }
}

static void nmo_lua_plan_release(void *resource, void *user_data)
{
    (void)user_data;
    nmo_edit_plan_destroy((nmo_edit_plan_t *)resource);
}

static nmo_status_t nmo_lua_push_edit_plan_handle(
    lua_State *state,
    nmo_edit_plan_t *plan)
{
    return nmo_lua_push_owned_handle(
        state,
        &NMO_LUA_EDIT_PLAN_HANDLE_DESCRIPTOR,
        plan,
        nmo_lua_plan_release,
        NULL,
        NULL);
}

static nmo_status_t nmo_lua_check_edit_plan_handle(
    lua_State *state,
    int index,
    nmo_edit_plan_t **out_plan)
{
    void *resource = NULL;
    nmo_status_t status = nmo_lua_handle_check(
        state,
        index,
        &NMO_LUA_EDIT_PLAN_HANDLE_DESCRIPTOR,
        NULL,
        &resource);
    if (status != NMO_OK) {
        return status;
    }
    *out_plan = (nmo_edit_plan_t *)resource;
    return NMO_OK;
}

static int nmo_lua_plan_parse_manager_entry_policy(
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

static int nmo_lua_plan_check_manager_entry_fields(lua_State *state,
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

static int nmo_lua_plan_parse_manager_entry_options(
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
    int field_rc = nmo_lua_plan_check_manager_entry_fields(state, index);
    if (field_rc != 0) {
        return field_rc;
    }
    lua_getfield(state, index, "policy");
    if (!lua_isnil(state, -1)) {
        int rc = nmo_lua_plan_parse_manager_entry_policy(
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

static int nmo_lua_plan_parse_parameter_write_options(
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
        int rc = nmo_lua_plan_parse_manager_entry_options(
            state, lua_gettop(state), &out_options->manager_entry);
        if (rc != 0) {
            return rc;
        }
    }
    lua_pop(state, 1);
    return 0;
}

static int nmo_lua_plan_new(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_edit_plan_create(&plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to create edit plan");
    }

    status = nmo_lua_push_edit_plan_handle(state, plan);
    if (status != NMO_OK) {
        nmo_edit_plan_destroy(plan);
        return nmo_lua_raise_last_error(state, status, "Failed to push edit plan");
    }
    return 1;
}

static int nmo_lua_plan_count(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    lua_pushinteger(state, (lua_Integer)nmo_edit_plan_count(plan));
    return 1;
}

static int nmo_lua_plan_add_node(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    const char *guid_text = luaL_checkstring(state, 3);
    const char *name = luaL_checkstring(state, 4);
    nmo_add_node_options_t options = {0};
    options.manager_entry = nmo_manager_entry_options_default();
    bool has_options = false;
    nmo_guid_t guid = nmo_guid_parse(guid_text);
    if (nmo_guid_is_null(guid)) {
        return luaL_error(state, "invalid building block GUID");
    }
    if (!lua_isnoneornil(state, 5)) {
        luaL_checktype(state, 5, LUA_TTABLE);
        lua_getfield(state, 5, "manager_entry");
        if (!lua_isnil(state, -1)) {
            int rc = nmo_lua_plan_parse_manager_entry_options(
                state, lua_gettop(state), &options.manager_entry);
            if (rc != 0) {
                return rc;
            }
        }
        lua_pop(state, 1);
        has_options = true;
    }

    status = nmo_edit_plan_add_node_ex(
        plan, behavior_id, guid, name, has_options ? &options : NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add node op");
    }
    return 0;
}

static int nmo_lua_plan_add_io(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    const char *kind_text = luaL_checkstring(state, 3);
    const char *name = luaL_checkstring(state, 4);
    nmo_script_edit_io_kind_t kind = NMO_SCRIPT_EDIT_IO_INPUT;
    if (strcmp(kind_text, "input") == 0) {
        kind = NMO_SCRIPT_EDIT_IO_INPUT;
    } else if (strcmp(kind_text, "output") == 0) {
        kind = NMO_SCRIPT_EDIT_IO_OUTPUT;
    } else {
        return luaL_error(state, "io kind must be 'input' or 'output'");
    }

    status = nmo_edit_plan_add_io(plan, behavior_id, kind, name);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add io op");
    }
    return 0;
}

static int nmo_lua_plan_rename_io(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t io_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    const char *name = luaL_checkstring(state, 3);

    status = nmo_edit_plan_add_rename_io(plan, io_id, name);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add rename io op");
    }
    return 0;
}

static int nmo_lua_plan_remove_io(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t io_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    bool detach_links = lua_toboolean(state, 3) != 0;

    status = nmo_edit_plan_add_remove_io(plan, io_id, detach_links);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add remove io op");
    }
    return 0;
}

static int nmo_lua_plan_remove_node(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t parent_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_object_id_t node_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    uint32_t delete_flags = (uint32_t)luaL_optinteger(state, 4, 0);

    status = nmo_edit_plan_add_remove_node(
        plan, parent_id, node_id, delete_flags);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to add remove node op");
    }
    return 0;
}

static int nmo_lua_plan_add_behavior_link(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t parent_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_object_id_t from_io_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    nmo_object_id_t to_io_id = (nmo_object_id_t)luaL_checkinteger(state, 4);
    uint32_t delay = (uint32_t)luaL_optinteger(state, 5, 0);

    status = nmo_edit_plan_add_behavior_link(
        plan, parent_id, from_io_id, to_io_id, delay);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add behavior link op");
    }
    return 0;
}

static int nmo_lua_plan_rewire_behavior_link(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t link_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_object_id_t from_io_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    nmo_object_id_t to_io_id = (nmo_object_id_t)luaL_checkinteger(state, 4);

    status = nmo_edit_plan_add_rewire_behavior_link(
        plan, link_id, from_io_id, to_io_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add rewire behavior link op");
    }
    return 0;
}

static int nmo_lua_plan_set_behavior_link_delay(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t link_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    uint32_t delay = (uint32_t)luaL_checkinteger(state, 3);

    status = nmo_edit_plan_add_set_behavior_link_delay(plan, link_id, delay);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add behavior link delay op");
    }
    return 0;
}

static int nmo_lua_plan_remove_behavior_link(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t parent_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_object_id_t link_id = (nmo_object_id_t)luaL_checkinteger(state, 3);

    status = nmo_edit_plan_add_remove_behavior_link(plan, parent_id, link_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add remove behavior link op");
    }
    return 0;
}

static bool nmo_lua_plan_parse_parameter_kind(
    const char *text,
    nmo_script_edit_parameter_kind_t *out_kind)
{
    if (strcmp(text, "input") == 0 || strcmp(text, "in") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_IN;
        return true;
    }
    if (strcmp(text, "output") == 0 || strcmp(text, "out") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_OUT;
        return true;
    }
    if (strcmp(text, "local") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_LOCAL;
        return true;
    }
    if (strcmp(text, "shared") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_SHARED;
        return true;
    }
    return false;
}

static int nmo_lua_plan_add_parameter(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t owner_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    const char *kind_text = luaL_checkstring(state, 3);
    const char *guid_text = luaL_checkstring(state, 4);
    const char *name = luaL_checkstring(state, 5);
    nmo_script_edit_parameter_kind_t kind = NMO_SCRIPT_EDIT_PARAM_IN;
    if (!nmo_lua_plan_parse_parameter_kind(kind_text, &kind)) {
        return luaL_error(
            state, "parameter kind must be 'input', 'output', 'local', or 'shared'");
    }
    nmo_guid_t type_guid = nmo_guid_parse(guid_text);
    if (nmo_guid_is_null(type_guid)) {
        return luaL_error(state, "invalid parameter type GUID");
    }

    status = nmo_edit_plan_add_parameter(plan, owner_id, kind, type_guid, name);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add parameter op");
    }
    return 0;
}

static int nmo_lua_plan_connect_parameter(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t source_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_object_id_t target_id = (nmo_object_id_t)luaL_checkinteger(state, 3);

    status = nmo_edit_plan_add_connect_parameter(plan, source_id, target_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add connect parameter op");
    }
    return 0;
}

static int nmo_lua_plan_connect_parameter_to_handle(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t source_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    lua_Integer lua_operation_index = luaL_checkinteger(state, 3);
    const char *handle_name = luaL_checkstring(state, 4);
    if (lua_operation_index <= 0) {
        return luaL_error(state, "operation index is 1-based and must be positive");
    }

    status = nmo_edit_plan_add_connect_parameter_to_handle(
        plan, source_id, (size_t)(lua_operation_index - 1), handle_name);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add connect parameter handle op");
    }
    return 0;
}

static int nmo_lua_plan_disconnect_parameter(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t target_id = (nmo_object_id_t)luaL_checkinteger(state, 2);

    status = nmo_edit_plan_add_disconnect_parameter(plan, target_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add disconnect parameter op");
    }
    return 0;
}

static int nmo_lua_plan_remove_parameter(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    bool detach = lua_toboolean(state, 3) != 0;

    status = nmo_edit_plan_add_remove_parameter(plan, parameter_id, detach);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add remove parameter op");
    }
    return 0;
}

static nmo_object_id_t nmo_lua_plan_optional_object_id(lua_State *state,
                                                       int index,
                                                       uint32_t *slot_flags,
                                                       uint32_t slot_flag)
{
    if (lua_isnoneornil(state, index)) {
        return 0u;
    }
    if (slot_flags != NULL) {
        *slot_flags |= slot_flag;
    }
    return (nmo_object_id_t)luaL_checkinteger(state, index);
}

static int nmo_lua_plan_add_operation(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t parent_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    const char *guid_text = luaL_checkstring(state, 3);
    nmo_guid_t operation_guid = nmo_guid_parse(guid_text);
    if (nmo_guid_is_null(operation_guid)) {
        return luaL_error(state, "invalid operation GUID");
    }
    nmo_object_id_t in1_id =
        nmo_lua_plan_optional_object_id(state, 4, NULL, 0u);
    nmo_object_id_t in2_id =
        nmo_lua_plan_optional_object_id(state, 5, NULL, 0u);
    nmo_object_id_t out_id =
        nmo_lua_plan_optional_object_id(state, 6, NULL, 0u);

    status = nmo_edit_plan_add_operation(
        plan, parent_id, operation_guid, in1_id, in2_id, out_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add operation op");
    }
    return 0;
}

static int nmo_lua_plan_rewire_operation(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t operation_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    uint32_t slot_flags = 0u;
    nmo_object_id_t in1_id = nmo_lua_plan_optional_object_id(
        state, 3, &slot_flags, NMO_SCRIPT_EDIT_OP_SLOT_IN1);
    nmo_object_id_t in2_id = nmo_lua_plan_optional_object_id(
        state, 4, &slot_flags, NMO_SCRIPT_EDIT_OP_SLOT_IN2);
    nmo_object_id_t out_id = nmo_lua_plan_optional_object_id(
        state, 5, &slot_flags, NMO_SCRIPT_EDIT_OP_SLOT_OUT);
    if (slot_flags == 0u) {
        return luaL_error(
            state, "rewire_operation requires at least one parameter slot");
    }

    status = nmo_edit_plan_add_rewire_operation(
        plan, operation_id, slot_flags, in1_id, in2_id, out_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add rewire operation op");
    }
    return 0;
}

static int nmo_lua_plan_remove_operation(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t operation_id = (nmo_object_id_t)luaL_checkinteger(state, 2);

    status = nmo_edit_plan_add_remove_operation(plan, operation_id);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add remove operation op");
    }
    return 0;
}

static int nmo_lua_plan_replace_bb(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_behavior_replace_bb_desc_t desc = {0};
    desc.behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    const char *guid_text = luaL_checkstring(state, 3);
    desc.name = luaL_optstring(state, 4, NULL);
    desc.block_guid = nmo_guid_parse(guid_text);
    desc.block_version = (uint32_t)luaL_optinteger(state, 5, 65536);
    if (nmo_guid_is_null(desc.block_guid)) {
        return luaL_error(state, "invalid building block GUID");
    }
    if (lua_istable(state, 6)) {
        lua_getfield(state, 6, "preserve_links");
        if (!lua_isnil(state, -1)) {
            desc.preserve_links = lua_toboolean(state, -1) != 0;
        }
        lua_pop(state, 1);
        lua_getfield(state, 6, "preserve_params");
        if (!lua_isnil(state, -1)) {
            desc.preserve_params = lua_toboolean(state, -1) != 0;
        }
        lua_pop(state, 1);
    }

    status = nmo_edit_plan_add_replace_bb(plan, &desc);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add replace-bb op");
    }
    return 0;
}

static void nmo_lua_plan_free_fold_inputs(nmo_object_id_t *node_ids,
                                          nmo_behavior_fold_map_t *input_maps,
                                          nmo_behavior_fold_map_t *output_maps,
                                          nmo_behavior_fold_map_t *parameter_maps)
{
    free(node_ids);
    free(input_maps);
    free(output_maps);
    free(parameter_maps);
}

static int nmo_lua_plan_fold(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_behavior_fold_desc_t desc = {0};
    nmo_behavior_fold_map_t *input_maps = NULL;
    nmo_behavior_fold_map_t *output_maps = NULL;
    nmo_behavior_fold_map_t *parameter_maps = NULL;
    const char *error = NULL;
    desc.parent_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    luaL_checktype(state, 3, LUA_TTABLE);
    size_t node_count = lua_rawlen(state, 3);
    if (node_count == 0u) {
        return luaL_error(state, "fold requires at least one node id");
    }
    nmo_object_id_t *node_ids =
        (nmo_object_id_t *)calloc(node_count, sizeof(*node_ids));
    if (node_ids == NULL) {
        return nmo_lua_raise_last_error(state, NMO_ERR_NOMEM,
                                        "Failed to allocate fold node ids");
    }
    for (size_t i = 0; i < node_count; ++i) {
        lua_rawgeti(state, 3, (lua_Integer)i + 1);
        lua_Integer node_id = luaL_checkinteger(state, -1);
        lua_pop(state, 1);
        if (node_id <= 0) {
            free(node_ids);
            return luaL_error(state, "fold node ids must be positive");
        }
        node_ids[i] = (nmo_object_id_t)node_id;
    }
    desc.node_ids = node_ids;
    desc.node_count = node_count;

    const char *guid_text = luaL_checkstring(state, 4);
    desc.block_guid = nmo_guid_parse(guid_text);
    desc.name = luaL_checkstring(state, 5);
    desc.block_version = 65536u;
    desc.interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE;
    if (nmo_guid_is_null(desc.block_guid)) {
        free(node_ids);
        return luaL_error(state, "invalid building block GUID");
    }
    if (lua_istable(state, 6)) {
        int options_index = lua_absindex(state, 6);
        lua_getfield(state, 6, "anchor");
        if (!lua_isnil(state, -1)) {
            desc.anchor_id = (nmo_object_id_t)luaL_checkinteger(state, -1);
        }
        lua_pop(state, 1);
        lua_getfield(state, 6, "version");
        if (!lua_isnil(state, -1)) {
            desc.block_version = (uint32_t)luaL_checkinteger(state, -1);
        }
        lua_pop(state, 1);
        lua_getfield(state, 6, "preserve_boundary");
        if (!lua_isnil(state, -1)) {
            desc.preserve_boundary = lua_toboolean(state, -1) != 0;
        }
        lua_pop(state, 1);
        lua_getfield(state, 6, "preserve_links");
        if (!lua_isnil(state, -1)) {
            desc.preserve_links = lua_toboolean(state, -1) != 0;
        }
        lua_pop(state, 1);
        lua_getfield(state, 6, "preserve_params");
        if (!lua_isnil(state, -1)) {
            desc.preserve_params = lua_toboolean(state, -1) != 0;
        }
        lua_pop(state, 1);
        lua_getfield(state, 6, "interface");
        if (!lua_isnil(state, -1)) {
            const char *mode = luaL_checkstring(state, -1);
            if (strcmp(mode, "preserve") == 0) {
                desc.interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE;
            } else if (strcmp(mode, "canonicalize") == 0) {
                desc.interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_CANONICALIZE;
            } else if (strcmp(mode, "remove") == 0) {
                desc.interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_REMOVE;
            } else {
                free(node_ids);
                return luaL_error(state, "invalid fold interface mode");
            }
        }
        lua_pop(state, 1);
        if (!nmo_lua_fold_map_parse(
                state, options_index, "inputs", NMO_BEHAVIOR_FOLD_MAP_INPUT,
                &input_maps, &desc.input_map_count, &error) ||
            !nmo_lua_fold_map_parse(
                state, options_index, "outputs", NMO_BEHAVIOR_FOLD_MAP_OUTPUT,
                &output_maps, &desc.output_map_count, &error) ||
            !nmo_lua_fold_map_parse(
                state, options_index, "parameters",
                NMO_BEHAVIOR_FOLD_MAP_PARAMETER, &parameter_maps,
                &desc.parameter_map_count, &error)) {
            nmo_lua_plan_free_fold_inputs(
                node_ids, input_maps, output_maps, parameter_maps);
            return luaL_error(state, "invalid fold map field '%s'",
                              error != NULL ? error : "unknown");
        }
    }
    if (desc.preserve_boundary) {
        desc.preserve_links = true;
        desc.preserve_params = true;
    }
    desc.input_maps = input_maps;
    desc.output_maps = output_maps;
    desc.parameter_maps = parameter_maps;

    status = nmo_edit_plan_add_fold(plan, &desc);
    nmo_lua_plan_free_fold_inputs(
        node_ids, input_maps, output_maps, parameter_maps);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add fold op");
    }
    return 0;
}

static int nmo_lua_plan_set_parameter_value(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    const char *value = luaL_checkstring(state, 3);
    nmo_parameter_write_options_t options;
    bool has_options = false;
    int option_rc = nmo_lua_plan_parse_parameter_write_options(
        state, 4, &options, &has_options);
    if (option_rc != 0) {
        return option_rc;
    }

    status = nmo_edit_plan_add_set_parameter_value(
        plan, parameter_id, value, has_options ? &options : NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add set parameter value op");
    }
    return 0;
}

static int nmo_lua_plan_set_parameter_value_from_handle(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    lua_Integer lua_operation_index = luaL_checkinteger(state, 2);
    const char *handle_name = luaL_checkstring(state, 3);
    const char *value = luaL_checkstring(state, 4);
    nmo_parameter_write_options_t options;
    bool has_options = false;
    if (lua_operation_index <= 0) {
        return luaL_error(state, "operation index is 1-based and must be positive");
    }
    int option_rc = nmo_lua_plan_parse_parameter_write_options(
        state, 5, &options, &has_options);
    if (option_rc != 0) {
        return option_rc;
    }

    status = nmo_edit_plan_add_set_parameter_value_from_handle(
        plan,
        (size_t)(lua_operation_index - 1),
        handle_name,
        value,
        has_options ? &options : NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add set parameter value handle op");
    }
    return 0;
}

static int nmo_lua_plan_set_parameter_bytes(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    size_t byte_count = 0u;
    const char *bytes = luaL_checklstring(state, 3, &byte_count);
    nmo_parameter_write_options_t options = {0};
    bool has_options = false;
    int option_rc = nmo_lua_plan_parse_parameter_write_options(
        state, 4, &options, &has_options);
    if (option_rc != 0) {
        return option_rc;
    }

    status = nmo_edit_plan_add_set_parameter_bytes(
        plan,
        parameter_id,
        (const uint8_t *)bytes,
        byte_count,
        has_options ? &options : NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add set parameter bytes op");
    }
    return 0;
}

static int nmo_lua_plan_set_parameter_bytes_from_handle(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    lua_Integer lua_operation_index = luaL_checkinteger(state, 2);
    const char *handle_name = luaL_checkstring(state, 3);
    size_t byte_count = 0u;
    const char *bytes = luaL_checklstring(state, 4, &byte_count);
    nmo_parameter_write_options_t options;
    bool has_options = false;
    if (lua_operation_index <= 0) {
        return luaL_error(state, "operation index is 1-based and must be positive");
    }
    int option_rc = nmo_lua_plan_parse_parameter_write_options(
        state, 5, &options, &has_options);
    if (option_rc != 0) {
        return option_rc;
    }

    status = nmo_edit_plan_add_set_parameter_bytes_from_handle(
        plan,
        (size_t)(lua_operation_index - 1),
        handle_name,
        (const uint8_t *)bytes,
        byte_count,
        has_options ? &options : NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add set parameter bytes handle op");
    }
    return 0;
}

static int nmo_lua_plan_set_data_cell(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t dataarray_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    lua_Integer row_arg = luaL_checkinteger(state, 3);
    lua_Integer col_arg = luaL_checkinteger(state, 4);
    const char *value = luaL_checkstring(state, 5);
    if (row_arg < 0 || col_arg < 0) {
        return luaL_error(state, "row and col must be non-negative");
    }
    uint32_t row = (uint32_t)row_arg;
    uint32_t col = (uint32_t)col_arg;

    status = nmo_edit_plan_add_data_cell(plan, dataarray_id, row, col, value);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add set data cell op");
    }
    return 0;
}

static nmo_probe_selector_mode_t nmo_lua_plan_probe_mode_from_string(
    const char *mode)
{
    if (strcmp(mode, "auto") == 0) {
        return NMO_PROBE_SELECTOR_MODE_AUTO;
    }
    if (strcmp(mode, "explicit_node") == 0) {
        return NMO_PROBE_SELECTOR_MODE_EXPLICIT_NODE;
    }
    if (strcmp(mode, "explicit_link") == 0) {
        return NMO_PROBE_SELECTOR_MODE_EXPLICIT_LINK;
    }
    if (strcmp(mode, "explicit_operation") == 0) {
        return NMO_PROBE_SELECTOR_MODE_EXPLICIT_OPERATION;
    }
    if (strcmp(mode, "explicit_data_cell") == 0) {
        return NMO_PROBE_SELECTOR_MODE_EXPLICIT_DATA_CELL;
    }
    if (strcmp(mode, "explicit") == 0) {
        return NMO_PROBE_SELECTOR_MODE_EXPLICIT;
    }
    return NMO_PROBE_SELECTOR_MODE_UNSPECIFIED;
}

static nmo_probe_selector_status_t nmo_lua_plan_probe_status_from_string(
    const char *status)
{
    if (strcmp(status, "selected") == 0) {
        return NMO_PROBE_SELECTOR_STATUS_SELECTED;
    }
    if (strcmp(status, "none") == 0) {
        return NMO_PROBE_SELECTOR_STATUS_NONE;
    }
    if (strcmp(status, "ambiguous") == 0) {
        return NMO_PROBE_SELECTOR_STATUS_AMBIGUOUS;
    }
    if (strcmp(status, "unsafe") == 0) {
        return NMO_PROBE_SELECTOR_STATUS_UNSAFE;
    }
    return NMO_PROBE_SELECTOR_STATUS_UNSPECIFIED;
}

static nmo_probe_candidate_role_t nmo_lua_plan_probe_role_from_string(
    const char *role)
{
    if (strcmp(role, "message") == 0) {
        return NMO_PROBE_CANDIDATE_MESSAGE;
    }
    if (strcmp(role, "sender") == 0) {
        return NMO_PROBE_CANDIDATE_MESSAGE_SENDER;
    }
    if (strcmp(role, "waiter") == 0) {
        return NMO_PROBE_CANDIDATE_MESSAGE_WAITER;
    }
    if (strcmp(role, "receiver") == 0) {
        return NMO_PROBE_CANDIDATE_MESSAGE_RECEIVER;
    }
    if (strcmp(role, "data_writer") == 0) {
        return NMO_PROBE_CANDIDATE_DATA_WRITER;
    }
    if (strcmp(role, "data_write_operation") == 0) {
        return NMO_PROBE_CANDIDATE_DATA_WRITE_OPERATION;
    }
    if (strcmp(role, "data_write_link") == 0) {
        return NMO_PROBE_CANDIDATE_DATA_WRITE_LINK;
    }
    return NMO_PROBE_CANDIDATE_UNKNOWN;
}

static nmo_object_id_t nmo_lua_plan_optional_object_id_field(
    lua_State *state,
    int table_index,
    const char *name)
{
    lua_getfield(state, table_index, name);
    nmo_object_id_t id = lua_isnil(state, -1)
        ? 0u
        : (nmo_object_id_t)luaL_checkinteger(state, -1);
    lua_pop(state, 1);
    return id;
}

static int nmo_lua_plan_set_probe_selector_analysis(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }
    luaL_checktype(state, 2, LUA_TTABLE);

    nmo_probe_selector_result_t analysis;
    nmo_probe_selector_result_init(&analysis);
    lua_getfield(state, 2, "mode");
    analysis.mode = nmo_lua_plan_probe_mode_from_string(
        luaL_checkstring(state, -1));
    lua_pop(state, 1);
    lua_getfield(state, 2, "status");
    analysis.status = nmo_lua_plan_probe_status_from_string(
        luaL_checkstring(state, -1));
    lua_pop(state, 1);
    if (analysis.mode == NMO_PROBE_SELECTOR_MODE_UNSPECIFIED ||
        analysis.status == NMO_PROBE_SELECTOR_STATUS_UNSPECIFIED) {
        return luaL_error(state, "probe selector analysis requires valid mode and status");
    }
    lua_getfield(state, 2, "rejection_code");
    if (!lua_isnil(state, -1)) {
        snprintf(analysis.rejection_code,
                 sizeof(analysis.rejection_code),
                 "%s",
                 luaL_checkstring(state, -1));
    }
    lua_pop(state, 1);
    analysis.selected_node_id =
        nmo_lua_plan_optional_object_id_field(state, 2, "selected_node_id");
    analysis.selected_link_id =
        nmo_lua_plan_optional_object_id_field(state, 2, "selected_link_id");
    analysis.selected_operation_id =
        nmo_lua_plan_optional_object_id_field(state, 2, "selected_operation_id");

    lua_getfield(state, 2, "candidates");
    if (!lua_isnil(state, -1)) {
        luaL_checktype(state, -1, LUA_TTABLE);
        int candidates_index = lua_gettop(state);
        lua_Integer count = luaL_len(state, candidates_index);
        for (lua_Integer i = 1; i <= count; ++i) {
            lua_rawgeti(state, candidates_index, i);
            luaL_checktype(state, -1, LUA_TTABLE);
            int candidate_index = lua_gettop(state);
            nmo_probe_selector_candidate_t candidate = {0};
            candidate.node_id =
                nmo_lua_plan_optional_object_id_field(state, candidate_index, "node_id");
            candidate.parent_id =
                nmo_lua_plan_optional_object_id_field(state, candidate_index, "parent_id");
            candidate.boundary_behavior_id =
                nmo_lua_plan_optional_object_id_field(state, candidate_index, "boundary_behavior_id");
            candidate.link_id =
                nmo_lua_plan_optional_object_id_field(state, candidate_index, "link_id");
            candidate.operation_id =
                nmo_lua_plan_optional_object_id_field(state, candidate_index, "operation_id");
            candidate.source_parameter_id =
                nmo_lua_plan_optional_object_id_field(state, candidate_index, "source_parameter_id");
            candidate.value_parameter_id =
                nmo_lua_plan_optional_object_id_field(state, candidate_index, "value_parameter_id");
            candidate.dataarray_id =
                nmo_lua_plan_optional_object_id_field(state, candidate_index, "dataarray_id");
            lua_getfield(state, candidate_index, "role");
            if (!lua_isnil(state, -1)) {
                candidate.role = nmo_lua_plan_probe_role_from_string(
                    luaL_checkstring(state, -1));
            }
            lua_pop(state, 1);
            lua_getfield(state, candidate_index, "rejection_code");
            if (!lua_isnil(state, -1)) {
                snprintf(candidate.rejection_code,
                         sizeof(candidate.rejection_code),
                         "%s",
                         luaL_checkstring(state, -1));
            }
            lua_pop(state, 1);
            status = nmo_probe_selector_result_add_candidate(
                &analysis, &candidate);
            lua_pop(state, 1);
            if (status != NMO_OK) {
                nmo_probe_analysis_dispose(&analysis);
                return nmo_lua_raise_last_error(
                    state, status, "Failed to add probe candidate");
            }
        }
    }
    lua_pop(state, 1);

    status = nmo_edit_plan_set_probe_selector_analysis(plan, &analysis);
    nmo_probe_analysis_dispose(&analysis);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to set probe selector analysis");
    }
    return 0;
}

static int nmo_lua_plan_interface_policy(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }

    nmo_object_id_t behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    const char *mode_text = luaL_checkstring(state, 3);
    nmo_script_edit_interface_mode_t mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE;
    if (strcmp(mode_text, "preserve") == 0) {
        mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE;
    } else if (strcmp(mode_text, "canonicalize") == 0) {
        mode = NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE;
    } else if (strcmp(mode_text, "remove") == 0) {
        mode = NMO_SCRIPT_EDIT_INTERFACE_REMOVE;
    } else {
        return luaL_error(
            state,
            "interface mode must be 'preserve', 'canonicalize', or 'remove'");
    }

    status = nmo_edit_plan_add_interface_policy(plan, behavior_id, mode);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(
            state, status, "Failed to add interface policy op");
    }
    return 0;
}

static int nmo_lua_plan_execute(lua_State *state)
{
    nmo_edit_plan_t *plan = NULL;
    nmo_workspace_t *workspace = NULL;
    nmo_edit_report_t report;
    nmo_edit_executor_options_t options = nmo_edit_executor_options_default();
    nmo_status_t status = nmo_lua_check_edit_plan_handle(state, 1, &plan);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid edit plan handle");
    }
    status = nmo_lua_check_workspace_handle(state, 2, &workspace, NULL, NULL);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Invalid workspace handle");
    }
    if (lua_istable(state, 3)) {
        lua_getfield(state, 3, "dry_run");
        if (!lua_isnil(state, -1)) {
            options.dry_run = lua_toboolean(state, -1);
        }
        lua_pop(state, 1);
    }

    status = nmo_edit_report_init(&report);
    if (status != NMO_OK) {
        return nmo_lua_raise_last_error(state, status, "Failed to create edit report");
    }
    status = nmo_edit_executor_execute(workspace, plan, &options, &report);
    if (status != NMO_OK) {
        nmo_edit_report_dispose(&report);
        return nmo_lua_raise_last_error(state, status, "Failed to execute edit plan");
    }
    nmo_lua_push_edit_report(state, &report);
    nmo_edit_report_dispose(&report);
    return 1;
}

static int nmo_lua_open_plan_module(lua_State *state)
{
    static const nmo_lua_plan_function_entry_t functions[] = {
        { "new", nmo_lua_plan_new },
        { "count", nmo_lua_plan_count },
        { "add_node", nmo_lua_plan_add_node },
        { "add_io", nmo_lua_plan_add_io },
        { "rename_io", nmo_lua_plan_rename_io },
        { "remove_io", nmo_lua_plan_remove_io },
        { "remove_node", nmo_lua_plan_remove_node },
        { "add_behavior_link", nmo_lua_plan_add_behavior_link },
        { "rewire_behavior_link", nmo_lua_plan_rewire_behavior_link },
        { "set_behavior_link_delay", nmo_lua_plan_set_behavior_link_delay },
        { "remove_behavior_link", nmo_lua_plan_remove_behavior_link },
        { "add_parameter", nmo_lua_plan_add_parameter },
        { "connect_parameter", nmo_lua_plan_connect_parameter },
        { "connect_parameter_to_handle", nmo_lua_plan_connect_parameter_to_handle },
        { "disconnect_parameter", nmo_lua_plan_disconnect_parameter },
        { "remove_parameter", nmo_lua_plan_remove_parameter },
        { "add_operation", nmo_lua_plan_add_operation },
        { "rewire_operation", nmo_lua_plan_rewire_operation },
        { "remove_operation", nmo_lua_plan_remove_operation },
        { "replace_bb", nmo_lua_plan_replace_bb },
        { "fold", nmo_lua_plan_fold },
        { "set_parameter_value", nmo_lua_plan_set_parameter_value },
        { "set_parameter_value_from_handle", nmo_lua_plan_set_parameter_value_from_handle },
        { "set_parameter_bytes", nmo_lua_plan_set_parameter_bytes },
        { "set_parameter_bytes_from_handle", nmo_lua_plan_set_parameter_bytes_from_handle },
        { "set_data_cell", nmo_lua_plan_set_data_cell },
        { "set_probe_selector_analysis", nmo_lua_plan_set_probe_selector_analysis },
        { "interface_policy", nmo_lua_plan_interface_policy },
        { "execute", nmo_lua_plan_execute },
    };
    const size_t function_count = sizeof(functions) / sizeof(functions[0]);

    lua_createtable(state, 0, (int)function_count);
    nmo_lua_plan_set_functions(state, functions, function_count);
    return 1;
}

nmo_status_t nmo_lua_register_plan_bindings(nmo_lua_runtime_t *runtime)
{
    const nmo_lua_module_t module = {
        .name = "nmo.plan",
        .open_fn = nmo_lua_open_plan_module,
    };

    return nmo_lua_runtime_register_module(runtime, &module);
}
