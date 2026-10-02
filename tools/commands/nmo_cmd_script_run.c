/**
 * @file nmo_cmd_script_run.c
 * @brief nmo script run: run a Lua script against a file through the behavior execution.
 */

#include "nmo_cmd_script_internal.h"

typedef struct script_run_args {
    const char *script_path;
    const char *input_path;
    bool dry_run;
    nmo_behavior_execution_t *execution;
    nmo_edit_plan_t *pending_plan;
    nmo_edit_report_t edit_report;
    bool edit_report_ready;
} script_run_args_t;

static script_run_args_t *g_script_run_args = NULL;

static void script_run_reset_args(script_run_args_t *args)
{
    if (args == NULL) {
        return;
    }

    nmo_edit_plan_destroy(args->pending_plan);
    if (args->edit_report_ready) {
        nmo_edit_report_dispose(&args->edit_report);
    }

    args->execution = NULL;
    args->pending_plan = NULL;
    args->edit_report_ready = false;
}

static script_run_args_t *script_run_current_args(lua_State *state)
{
    (void)state;
    if (g_script_run_args == NULL || g_script_run_args->execution == NULL) {
        luaL_error(state, "behavior execution state is unavailable");
        return NULL;
    }
    return g_script_run_args;
}

static nmo_behavior_state_t *script_run_find_behavior_state(
    nmo_workspace_t *workspace,
    nmo_object_id_t behavior_id)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;

    if (workspace == NULL || behavior_id == 0u) {
        return NULL;
    }

    repo = nmo_tool_owner_repository(workspace);
    object = repo ? nmo_object_repository_find_by_id(repo, behavior_id) : NULL;
    if (object == NULL || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
        return NULL;
    }

    return (nmo_behavior_state_t *)nmo_object_get_state(object);
}

static int script_run_lua_root_script_id(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_workspace_t *workspace = script_execution_workspace(args->execution);
    nmo_document_t *document = nmo_workspace_get_document(workspace);
    nmo_behavior_script_view_t view = {0};
    nmo_status_t status = NMO_OK;

    if (document == NULL) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to resolve document");
    }
    status = nmo_behavior_query_script_at(document, 0u, &view);
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to resolve root script");
    }

    lua_pushinteger(state, (lua_Integer)view.script_id);
    return 1;
}

static int script_run_lua_io_at(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_workspace_t *workspace = script_execution_workspace(args->execution);
    nmo_object_id_t behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    const char *kind_text = luaL_checkstring(state, 2);
    lua_Integer lua_index = luaL_checkinteger(state, 3);
    const nmo_array_t *ports = NULL;
    nmo_behavior_state_t *state_data = NULL;

    if (lua_index < 1) {
        return luaL_error(state, "io index must be 1-based");
    }

    state_data = script_run_find_behavior_state(workspace, behavior_id);
    if (state_data == NULL) {
        return luaL_error(state, "failed to resolve behavior state");
    }

    if (strcmp(kind_text, "input") == 0) {
        ports = &state_data->inputs;
    } else if (strcmp(kind_text, "output") == 0) {
        ports = &state_data->outputs;
    } else {
        return luaL_error(state, "io kind must be 'input' or 'output'");
    }

    if ((size_t)(lua_index - 1) >= ports->count) {
        lua_pushnil(state);
        return 1;
    }

    lua_pushinteger(
        state,
        (lua_Integer)nmo_behavior_ref_array_get_id(
            ports, (size_t)(lua_index - 1)));
    return 1;
}

static int script_run_lua_interface_sub_at(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_workspace_t *workspace = script_execution_workspace(args->execution);
    nmo_object_id_t behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    lua_Integer lua_index = luaL_checkinteger(state, 2);
    nmo_behavior_state_t *state_data = NULL;

    if (lua_index < 1) {
        return luaL_error(state, "sub-behavior index must be 1-based");
    }

    state_data = script_run_find_behavior_state(workspace, behavior_id);
    if (state_data == NULL || state_data->interface_data == NULL) {
        lua_pushnil(state);
        return 1;
    }

    if ((size_t)(lua_index - 1) >= state_data->interface_data->sub_count) {
        lua_pushnil(state);
        return 1;
    }

    lua_pushinteger(
        state,
        (lua_Integer)state_data->interface_data->subs[(size_t)(lua_index - 1)].behavior_id);
    return 1;
}

static nmo_status_t script_run_ensure_pending_plan(script_run_args_t *args)
{
    if (args == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (args->pending_plan != NULL) {
        return NMO_OK;
    }
    return nmo_edit_plan_create(&args->pending_plan);
}

static nmo_status_t script_run_execute_pending_plan(script_run_args_t *args)
{
    nmo_status_t status = NMO_OK;

    if (args == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    if (args->edit_report_ready) {
        return NMO_OK;
    }
    status = nmo_edit_report_init(&args->edit_report);
    if (status != NMO_OK) {
        return status;
    }
    args->edit_report_ready = true;

    if (args->pending_plan == NULL ||
        nmo_edit_plan_count(args->pending_plan) == 0u) {
        args->edit_report.ok = true;
        args->edit_report.dry_run = args->dry_run;
        args->edit_report.status = NMO_OK;
        args->edit_report.validation.final_status = NMO_OK;
        return NMO_OK;
    }

    nmo_edit_executor_options_t options = nmo_edit_executor_options_default();
    options.dry_run = args->dry_run;
    options.validation_flags = 0u;
    status = nmo_edit_executor_execute_transaction(
        nmo_behavior_execution_transaction(args->execution),
        args->pending_plan,
        &options,
        &args->edit_report);
    if (status != NMO_OK) {
        nmo_edit_report_dispose(&args->edit_report);
        args->edit_report_ready = false;
    }
    return status;
}

static lua_Integer script_run_pending_operation_index(
    const script_run_args_t *args)
{
    if (args == NULL || args->pending_plan == NULL) {
        return 0;
    }
    return (lua_Integer)nmo_edit_plan_count(args->pending_plan);
}

static int script_run_lua_add_io(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    const char *kind_text = luaL_checkstring(state, 2);
    const char *name = luaL_checkstring(state, 3);
    nmo_script_edit_io_kind_t kind = NMO_SCRIPT_EDIT_IO_INPUT;
    nmo_status_t status = NMO_OK;

    if (strcmp(kind_text, "input") == 0) {
        kind = NMO_SCRIPT_EDIT_IO_INPUT;
    } else if (strcmp(kind_text, "output") == 0) {
        kind = NMO_SCRIPT_EDIT_IO_OUTPUT;
    } else {
        return luaL_error(state, "io kind must be 'input' or 'output'");
    }

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_io(args->pending_plan, behavior_id, kind, name);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script io");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_add_node(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t parent_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    const char *guid_text = luaL_checkstring(state, 2);
    const char *name = luaL_optstring(state, 3, NULL);
    nmo_add_node_options_t options = {0};
    options.manager_entry = nmo_manager_entry_options_default();
    bool has_options = false;
    nmo_guid_t bb_guid = nmo_guid_parse(guid_text);
    nmo_status_t status = NMO_OK;

    if (nmo_guid_is_null(bb_guid)) {
        return luaL_error(state, "invalid building block GUID");
    }
    if (!lua_isnoneornil(state, 4)) {
        luaL_checktype(state, 4, LUA_TTABLE);
        lua_getfield(state, 4, "manager_entry");
        if (!lua_isnil(state, -1)) {
            luaL_checktype(state, lua_gettop(state), LUA_TTABLE);
            int manager_entry_index = lua_gettop(state);
            static const char *const allowed_manager_entry_fields[] = {
                "policy", "schema", "manager_guid", "key", NULL
            };
            lua_pushnil(state);
            while (lua_next(state, manager_entry_index) != 0) {
                const char *key = lua_tostring(state, -2);
                bool known = false;
                for (size_t i = 0;
                     allowed_manager_entry_fields[i] != NULL;
                     ++i) {
                    if (key != NULL &&
                        strcmp(key, allowed_manager_entry_fields[i]) == 0) {
                        known = true;
                        break;
                    }
                }
                if (!known) {
                    return luaL_error(
                        state,
                        "Unknown manager_entry field '%s'",
                        key != NULL ? key : "(non-string)");
                }
                lua_pop(state, 1);
            }
            lua_getfield(state, -1, "policy");
            if (!lua_isnil(state, -1)) {
                const char *policy = luaL_checkstring(state, -1);
                if (strcmp(policy, "require_existing") == 0) {
                    options.manager_entry.policy =
                        NMO_MANAGER_ENTRY_POLICY_REQUIRE_EXISTING;
                } else if (strcmp(policy, "create_missing") == 0) {
                    options.manager_entry.policy =
                        NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING;
                } else {
                    return luaL_error(
                        state,
                        "manager_entry.policy must be 'require_existing' or 'create_missing'");
                }
            }
            lua_pop(state, 1);
            lua_getfield(state, -1, "schema");
            if (!lua_isnil(state, -1)) {
                const char *schema = luaL_checkstring(state, -1);
                if (strcmp(schema, "auto") == 0) {
                    options.manager_entry.schema = NMO_MANAGER_ENTRY_SCHEMA_AUTO;
                } else if (strcmp(schema, "message") == 0) {
                    options.manager_entry.schema = NMO_MANAGER_ENTRY_SCHEMA_MESSAGE;
                } else if (strcmp(schema, "attribute") == 0) {
                    options.manager_entry.schema = NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE;
                } else {
                    return luaL_error(
                        state,
                        "manager_entry.schema must be 'auto', 'message', or 'attribute'");
                }
            }
            lua_pop(state, 1);
            lua_getfield(state, -1, "manager_guid");
            if (!lua_isnil(state, -1)) {
                options.manager_entry.manager_guid =
                    nmo_guid_parse(luaL_checkstring(state, -1));
                if (nmo_guid_is_null(options.manager_entry.manager_guid)) {
                    return luaL_error(
                        state,
                        "manager_entry.manager_guid must be a GUID");
                }
            }
            lua_pop(state, 1);
            lua_getfield(state, -1, "key");
            if (!lua_isnil(state, -1)) {
                options.manager_entry.key = luaL_checkstring(state, -1);
            }
            lua_pop(state, 1);
        }
        lua_pop(state, 1);
        has_options = true;
    }

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_node_ex(
            args->pending_plan, parent_id, bb_guid, name,
            has_options ? &options : NULL);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script node");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_parse_manager_entry_policy(
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

static int script_run_lua_check_manager_entry_fields(lua_State *state,
                                                     int index)
{
    static const char *const allowed[] = {
        "policy", "schema", "manager_guid", "key", NULL
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

static int script_run_lua_parse_parameter_write_options(
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
        luaL_checktype(state, lua_gettop(state), LUA_TTABLE);
        int field_rc = script_run_lua_check_manager_entry_fields(
            state, lua_gettop(state));
        if (field_rc != 0) {
            return field_rc;
        }
        lua_getfield(state, -1, "policy");
        if (!lua_isnil(state, -1)) {
            int rc = script_run_lua_parse_manager_entry_policy(
                state, lua_gettop(state), &out_options->manager_entry.policy);
            if (rc != 0) {
                return rc;
            }
        }
        lua_pop(state, 1);
        lua_getfield(state, -1, "schema");
        if (!lua_isnil(state, -1)) {
            const char *schema = luaL_checkstring(state, -1);
            if (strcmp(schema, "auto") == 0) {
                out_options->manager_entry.schema = NMO_MANAGER_ENTRY_SCHEMA_AUTO;
            } else if (strcmp(schema, "message") == 0) {
                out_options->manager_entry.schema = NMO_MANAGER_ENTRY_SCHEMA_MESSAGE;
            } else if (strcmp(schema, "attribute") == 0) {
                out_options->manager_entry.schema = NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE;
            } else {
                return luaL_error(
                    state,
                    "manager_entry.schema must be 'auto', 'message', or 'attribute'");
            }
        }
        lua_pop(state, 1);
        lua_getfield(state, -1, "manager_guid");
        if (!lua_isnil(state, -1)) {
            out_options->manager_entry.manager_guid =
                nmo_guid_parse(luaL_checkstring(state, -1));
            if (nmo_guid_is_null(out_options->manager_entry.manager_guid)) {
                return luaL_error(
                    state,
                    "manager_entry.manager_guid must be a GUID");
            }
        }
        lua_pop(state, 1);
        lua_getfield(state, -1, "key");
        if (!lua_isnil(state, -1)) {
            out_options->manager_entry.key = luaL_checkstring(state, -1);
        }
        lua_pop(state, 1);
    }
    lua_pop(state, 1);
    return 0;
}

static int script_run_lua_remove_io(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_workspace_t *workspace = script_execution_workspace(args->execution);
    nmo_object_id_t io_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    const char *mode_text = luaL_optstring(state, 2, "preserve");
    nmo_script_edit_interface_mode_t mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE;
    nmo_object_id_t interface_behavior_id = 0u;
    nmo_status_t status = NMO_OK;

    if (strcmp(mode_text, "preserve") == 0) {
        mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE;
    } else if (strcmp(mode_text, "canonicalize") == 0) {
        mode = NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE;
    } else if (strcmp(mode_text, "remove") == 0) {
        mode = NMO_SCRIPT_EDIT_INTERFACE_REMOVE;
    } else {
        return luaL_error(state,
                          "interface mode must be 'preserve', 'canonicalize', or 'remove'");
    }

    interface_behavior_id = script_interface_root_for_object_workspace(workspace, io_id);
    if (mode != NMO_SCRIPT_EDIT_INTERFACE_PRESERVE && interface_behavior_id == 0u) {
        return luaL_error(state, "failed to resolve script interface root");
    }

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_remove_io(args->pending_plan, io_id, false);
    }
    if (status == NMO_OK && mode != NMO_SCRIPT_EDIT_INTERFACE_PRESERVE) {
        status = nmo_edit_plan_add_interface_policy(
            args->pending_plan, interface_behavior_id, mode);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script io removal");
    }

    return 0;
}

static int script_run_lua_rename_io(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t io_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    const char *name = luaL_checkstring(state, 2);
    nmo_status_t status = NMO_OK;

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_rename_io(args->pending_plan, io_id, name);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script io rename");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_remove_node(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_workspace_t *workspace = script_execution_workspace(args->execution);
    nmo_object_id_t parent_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    nmo_object_id_t node_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    const char *mode_text = luaL_optstring(state, 3, "preserve");
    nmo_script_edit_interface_mode_t mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE;
    nmo_status_t status = NMO_OK;

    if (strcmp(mode_text, "preserve") == 0) {
        mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE;
    } else if (strcmp(mode_text, "canonicalize") == 0) {
        mode = NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE;
    } else if (strcmp(mode_text, "remove") == 0) {
        mode = NMO_SCRIPT_EDIT_INTERFACE_REMOVE;
    } else {
        return luaL_error(state,
                          "interface mode must be 'preserve', 'canonicalize', or 'remove'");
    }

    if (mode == NMO_SCRIPT_EDIT_INTERFACE_PRESERVE &&
        script_workspace_interface_references_behavior(workspace, parent_id, node_id)) {
        return luaL_error(state, "Failed to apply script interface policy");
    }

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_remove_node(
            args->pending_plan, parent_id, node_id, 0u);
    }
    if (status == NMO_OK && mode != NMO_SCRIPT_EDIT_INTERFACE_PRESERVE) {
        status = nmo_edit_plan_add_interface_policy(
            args->pending_plan, parent_id, mode);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script node removal");
    }

    return 0;
}

typedef struct script_run_lua_pending_ref {
    nmo_object_id_t id;
    size_t operation_index;
    const char *handle_name;
    bool has_ref;
} script_run_lua_pending_ref_t;

static void script_run_lua_check_pending_ref(lua_State *state,
                                             int index,
                                             script_run_lua_pending_ref_t *out_ref)
{
    memset(out_ref, 0, sizeof(*out_ref));
    if (lua_istable(state, index)) {
        lua_getfield(state, index, "operation");
        lua_Integer operation = luaL_checkinteger(state, -1);
        lua_pop(state, 1);
        lua_getfield(state, index, "handle");
        const char *handle = luaL_checkstring(state, -1);
        lua_pop(state, 1);
        if (operation <= 0 || handle == NULL || handle[0] == '\0') {
            luaL_error(state, "operation handle references require positive operation and non-empty handle");
        }
        out_ref->operation_index = (size_t)(operation - 1);
        out_ref->handle_name = handle;
        out_ref->has_ref = true;
        return;
    }
    out_ref->id = (nmo_object_id_t)luaL_checkinteger(state, index);
}

static nmo_edit_handle_ref_t script_run_lua_edit_handle_ref(
    const script_run_lua_pending_ref_t *ref)
{
    return (nmo_edit_handle_ref_t){
        .has_ref = ref != NULL && ref->has_ref,
        .operation_index = ref != NULL ? ref->operation_index : 0u,
        .handle_name = ref != NULL ? ref->handle_name : NULL,
    };
}

static nmo_edit_handle_ref_t script_edit_handle_ref(
    size_t operation_index,
    const char *handle_name)
{
    return (nmo_edit_handle_ref_t){
        .has_ref = true,
        .operation_index = operation_index,
        .handle_name = handle_name,
    };
}

static int script_run_lua_add_behavior_link(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t parent_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    script_run_lua_pending_ref_t from_io = {0};
    script_run_lua_pending_ref_t to_io = {0};
    uint32_t activation_delay = (uint32_t)luaL_optinteger(state, 4, 0);
    nmo_status_t status = NMO_OK;

    script_run_lua_check_pending_ref(state, 2, &from_io);
    script_run_lua_check_pending_ref(state, 3, &to_io);

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        nmo_edit_handle_ref_t from_ref =
            script_run_lua_edit_handle_ref(&from_io);
        nmo_edit_handle_ref_t to_ref =
            script_run_lua_edit_handle_ref(&to_io);
        status = nmo_edit_plan_add_behavior_link(
            args->pending_plan,
            parent_id,
            from_io.id,
            from_io.has_ref ? &from_ref : NULL,
            to_io.id,
            to_io.has_ref ? &to_ref : NULL,
            activation_delay);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script behavior link");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_rewire_behavior_link(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t link_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    nmo_object_id_t from_io_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_object_id_t to_io_id = (nmo_object_id_t)luaL_checkinteger(state, 3);
    nmo_status_t status = NMO_OK;

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_rewire_behavior_link(
            args->pending_plan, link_id, from_io_id, to_io_id);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script behavior link rewire");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_set_behavior_link_delay(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t link_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    uint32_t activation_delay = (uint32_t)luaL_checkinteger(state, 2);
    nmo_status_t status = NMO_OK;

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_set_behavior_link_delay(
            args->pending_plan, link_id, activation_delay);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script behavior link delay");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_remove_behavior_link(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t parent_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    nmo_object_id_t link_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_status_t status = NMO_OK;

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_remove_behavior_link(
            args->pending_plan, parent_id, link_id);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script behavior link removal");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static bool script_run_parse_parameter_kind(
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

static int script_run_lua_add_parameter(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t owner_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    const char *kind_text = luaL_checkstring(state, 2);
    const char *type_guid_text = luaL_checkstring(state, 3);
    const char *name = luaL_checkstring(state, 4);
    nmo_script_edit_parameter_kind_t kind = NMO_SCRIPT_EDIT_PARAM_IN;
    nmo_guid_t type_guid = nmo_guid_parse(type_guid_text);
    nmo_status_t status = NMO_OK;

    if (!script_run_parse_parameter_kind(kind_text, &kind)) {
        return luaL_error(
            state, "parameter kind must be 'input', 'output', 'local', or 'shared'");
    }
    if (nmo_guid_is_null(type_guid)) {
        return luaL_error(state, "invalid parameter type GUID");
    }

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_parameter(
            args->pending_plan, owner_id, kind, type_guid, name);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script parameter");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_connect_parameter(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t source_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    nmo_object_id_t target_id = (nmo_object_id_t)luaL_checkinteger(state, 2);
    nmo_status_t status = NMO_OK;

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_connect_parameter(
            args->pending_plan, source_id, target_id, NULL);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script parameter connection");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_connect_parameter_to_handle(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t source_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    lua_Integer operation_index = luaL_checkinteger(state, 2);
    const char *handle_name = luaL_checkstring(state, 3);
    nmo_status_t status = NMO_OK;

    if (operation_index <= 0) {
        return luaL_error(state, "operation index is 1-based and must be positive");
    }

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        nmo_edit_handle_ref_t target_ref =
            script_edit_handle_ref((size_t)(operation_index - 1), handle_name);
        status = nmo_edit_plan_add_connect_parameter(
            args->pending_plan,
            source_id,
            0u,
            &target_ref);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script parameter handle connection");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_disconnect_parameter(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t target_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    nmo_status_t status = NMO_OK;

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_disconnect_parameter(
            args->pending_plan, target_id);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script parameter disconnection");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_remove_parameter(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    bool detach = lua_toboolean(state, 2) != 0;
    nmo_status_t status = NMO_OK;

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_remove_parameter(
            args->pending_plan, parameter_id, detach);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script parameter removal");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static nmo_object_id_t script_run_lua_optional_object_id(lua_State *state,
                                                         int index)
{
    if (lua_isnoneornil(state, index)) {
        return 0u;
    }
    return (nmo_object_id_t)luaL_checkinteger(state, index);
}

static nmo_object_id_t script_run_lua_optional_operation_slot(
    lua_State *state,
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

static int script_run_lua_add_operation(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t parent_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    const char *operation_guid_text = luaL_checkstring(state, 2);
    nmo_guid_t operation_guid = nmo_guid_parse(operation_guid_text);
    nmo_object_id_t in1_id = 0u;
    nmo_object_id_t in2_id = 0u;
    nmo_object_id_t out_id = 0u;
    nmo_status_t status = NMO_OK;

    if (nmo_guid_is_null(operation_guid)) {
        return luaL_error(state, "invalid operation GUID");
    }

    in1_id = script_run_lua_optional_object_id(state, 3);
    in2_id = script_run_lua_optional_object_id(state, 4);
    out_id = script_run_lua_optional_object_id(state, 5);

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_operation(
            args->pending_plan,
            parent_id,
            operation_guid,
            in1_id,
            NULL,
            in2_id,
            NULL,
            out_id,
            NULL);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script operation");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_rewire_operation(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t operation_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    uint32_t slot_flags = 0u;
    nmo_object_id_t in1_id = 0u;
    nmo_object_id_t in2_id = 0u;
    nmo_object_id_t out_id = 0u;
    nmo_status_t status = NMO_OK;

    in1_id = script_run_lua_optional_operation_slot(
        state, 2, &slot_flags, NMO_SCRIPT_EDIT_OP_SLOT_IN1);
    in2_id = script_run_lua_optional_operation_slot(
        state, 3, &slot_flags, NMO_SCRIPT_EDIT_OP_SLOT_IN2);
    out_id = script_run_lua_optional_operation_slot(
        state, 4, &slot_flags, NMO_SCRIPT_EDIT_OP_SLOT_OUT);
    if (slot_flags == 0u) {
        return luaL_error(
            state, "rewire_operation requires at least one parameter slot");
    }

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_rewire_operation(
            args->pending_plan,
            operation_id,
            slot_flags,
            in1_id,
            NULL,
            in2_id,
            NULL,
            out_id,
            NULL);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script operation rewire");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_remove_operation(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t operation_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    nmo_status_t status = NMO_OK;

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_remove_operation(
            args->pending_plan, operation_id);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script operation removal");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_replace_bb(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_behavior_replace_bb_desc_t desc = {0};
    const char *guid_text = luaL_checkstring(state, 2);
    nmo_status_t status = NMO_OK;

    desc.behavior_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    desc.name = luaL_optstring(state, 3, NULL);
    desc.block_guid = nmo_guid_parse(guid_text);
    desc.block_version = (uint32_t)luaL_optinteger(state, 4, 65536);
    if (nmo_guid_is_null(desc.block_guid)) {
        return luaL_error(state, "invalid building block GUID");
    }
    if (lua_istable(state, 5)) {
        lua_getfield(state, 5, "preserve_links");
        if (!lua_isnil(state, -1)) {
            desc.preserve_links = lua_toboolean(state, -1) != 0;
        }
        lua_pop(state, 1);
        lua_getfield(state, 5, "preserve_params");
        if (!lua_isnil(state, -1)) {
            desc.preserve_params = lua_toboolean(state, -1) != 0;
        }
        lua_pop(state, 1);
    }

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_replace_bb(args->pending_plan, &desc);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script replace-bb");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static void script_run_lua_free_fold_inputs(
    nmo_object_id_t *node_ids,
    nmo_behavior_fold_map_t *input_maps,
    nmo_behavior_fold_map_t *output_maps,
    nmo_behavior_fold_map_t *parameter_maps)
{
    free(node_ids);
    free(input_maps);
    free(output_maps);
    free(parameter_maps);
}

static int script_run_lua_fold(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_behavior_fold_desc_t desc = {0};
    nmo_behavior_fold_map_t *input_maps = NULL;
    nmo_behavior_fold_map_t *output_maps = NULL;
    nmo_behavior_fold_map_t *parameter_maps = NULL;
    const char *error = NULL;
    nmo_status_t status = NMO_OK;

    desc.parent_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    luaL_checktype(state, 2, LUA_TTABLE);
    size_t node_count = lua_rawlen(state, 2);
    if (node_count == 0u) {
        return luaL_error(state, "fold requires at least one node id");
    }
    nmo_object_id_t *node_ids =
        (nmo_object_id_t *)calloc(node_count, sizeof(*node_ids));
    if (node_ids == NULL) {
        return luaL_error(state, "failed to allocate fold node ids");
    }
    for (size_t i = 0; i < node_count; ++i) {
        lua_rawgeti(state, 2, (lua_Integer)i + 1);
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
    desc.block_guid = nmo_guid_parse(luaL_checkstring(state, 3));
    desc.name = luaL_checkstring(state, 4);
    desc.block_version = 65536u;
    desc.interface_mode = NMO_BEHAVIOR_FOLD_INTERFACE_PRESERVE;
    if (nmo_guid_is_null(desc.block_guid)) {
        free(node_ids);
        return luaL_error(state, "invalid building block GUID");
    }
    if (lua_istable(state, 5)) {
        int options_index = lua_absindex(state, 5);
        lua_getfield(state, 5, "anchor");
        if (!lua_isnil(state, -1)) {
            desc.anchor_id = (nmo_object_id_t)luaL_checkinteger(state, -1);
        }
        lua_pop(state, 1);
        lua_getfield(state, 5, "version");
        if (!lua_isnil(state, -1)) {
            desc.block_version = (uint32_t)luaL_checkinteger(state, -1);
        }
        lua_pop(state, 1);
        lua_getfield(state, 5, "preserve_boundary");
        if (!lua_isnil(state, -1)) {
            desc.preserve_boundary = lua_toboolean(state, -1) != 0;
        }
        lua_pop(state, 1);
        lua_getfield(state, 5, "preserve_links");
        if (!lua_isnil(state, -1)) {
            desc.preserve_links = lua_toboolean(state, -1) != 0;
        }
        lua_pop(state, 1);
        lua_getfield(state, 5, "preserve_params");
        if (!lua_isnil(state, -1)) {
            desc.preserve_params = lua_toboolean(state, -1) != 0;
        }
        lua_pop(state, 1);
        lua_getfield(state, 5, "interface");
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
            script_run_lua_free_fold_inputs(
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

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_fold(args->pending_plan, &desc);
    }
    script_run_lua_free_fold_inputs(
        node_ids, input_maps, output_maps, parameter_maps);
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script fold");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_set_parameter_value(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    const char *value = luaL_checkstring(state, 2);
    nmo_parameter_write_options_t options;
    bool has_options = false;
    nmo_status_t status = NMO_OK;

    int option_rc = script_run_lua_parse_parameter_write_options(
        state, 3, &options, &has_options);
    if (option_rc != 0) {
        return option_rc;
    }

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_set_parameter_value(
            args->pending_plan,
            parameter_id,
            NULL,
            value,
            has_options ? &options : NULL);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script parameter value");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_set_parameter_value_from_handle(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    lua_Integer operation_index = luaL_checkinteger(state, 1);
    const char *handle_name = luaL_checkstring(state, 2);
    const char *value = luaL_checkstring(state, 3);
    nmo_parameter_write_options_t options;
    bool has_options = false;
    nmo_status_t status = NMO_OK;

    if (operation_index <= 0) {
        return luaL_error(state, "operation index is 1-based and must be positive");
    }
    int option_rc = script_run_lua_parse_parameter_write_options(
        state, 4, &options, &has_options);
    if (option_rc != 0) {
        return option_rc;
    }

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        nmo_edit_handle_ref_t parameter_ref =
            script_edit_handle_ref((size_t)(operation_index - 1), handle_name);
        status = nmo_edit_plan_add_set_parameter_value(
            args->pending_plan,
            0u,
            &parameter_ref,
            value,
            has_options ? &options : NULL);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script parameter handle value");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_set_parameter_bytes(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t parameter_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    size_t byte_count = 0u;
    const char *bytes = luaL_checklstring(state, 2, &byte_count);
    nmo_parameter_write_options_t options;
    bool has_options = false;
    nmo_status_t status = NMO_OK;

    int option_rc = script_run_lua_parse_parameter_write_options(
        state, 3, &options, &has_options);
    if (option_rc != 0) {
        return option_rc;
    }

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_set_parameter_bytes(
            args->pending_plan,
            parameter_id,
            NULL,
            (const uint8_t *)bytes,
            byte_count,
            has_options ? &options : NULL);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script parameter bytes");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_set_parameter_bytes_from_handle(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    lua_Integer operation_index = luaL_checkinteger(state, 1);
    const char *handle_name = luaL_checkstring(state, 2);
    size_t byte_count = 0u;
    const char *bytes = luaL_checklstring(state, 3, &byte_count);
    nmo_parameter_write_options_t options;
    bool has_options = false;
    nmo_status_t status = NMO_OK;

    if (operation_index <= 0) {
        return luaL_error(state, "operation index is 1-based and must be positive");
    }
    int option_rc = script_run_lua_parse_parameter_write_options(
        state, 4, &options, &has_options);
    if (option_rc != 0) {
        return option_rc;
    }

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        nmo_edit_handle_ref_t parameter_ref =
            script_edit_handle_ref((size_t)(operation_index - 1), handle_name);
        status = nmo_edit_plan_add_set_parameter_bytes(
            args->pending_plan,
            0u,
            &parameter_ref,
            (const uint8_t *)bytes,
            byte_count,
            has_options ? &options : NULL);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script parameter handle bytes");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_set_data_cell(lua_State *state)
{
    script_run_args_t *args = script_run_current_args(state);
    nmo_object_id_t dataarray_id = (nmo_object_id_t)luaL_checkinteger(state, 1);
    lua_Integer row_arg = luaL_checkinteger(state, 2);
    lua_Integer col_arg = luaL_checkinteger(state, 3);
    const char *value = luaL_checkstring(state, 4);
    nmo_status_t status = NMO_OK;

    if (row_arg < 0 || col_arg < 0) {
        return luaL_error(state, "row and col must be non-negative");
    }

    status = script_run_ensure_pending_plan(args);
    if (status == NMO_OK) {
        status = nmo_edit_plan_add_data_cell(
            args->pending_plan,
            dataarray_id,
            (uint32_t)row_arg,
            (uint32_t)col_arg,
            value);
    }
    if (status != NMO_OK) {
        return luaL_error(state, "%s",
                          nmo_last_error_message() != NULL
                              ? nmo_last_error_message()
                              : "failed to enqueue script data cell");
    }

    lua_pushinteger(state, script_run_pending_operation_index(args));
    return 1;
}

static int script_run_lua_open_executor_module(lua_State *state)
{
    lua_createtable(state, 0, 24);

    lua_pushcfunction(state, script_run_lua_root_script_id);
    lua_setfield(state, -2, "root_script_id");

    lua_pushcfunction(state, script_run_lua_io_at);
    lua_setfield(state, -2, "io_at");

    lua_pushcfunction(state, script_run_lua_interface_sub_at);
    lua_setfield(state, -2, "interface_sub_at");

    lua_pushcfunction(state, script_run_lua_add_io);
    lua_setfield(state, -2, "add_io");

    lua_pushcfunction(state, script_run_lua_add_node);
    lua_setfield(state, -2, "add_node");

    lua_pushcfunction(state, script_run_lua_remove_io);
    lua_setfield(state, -2, "remove_io");

    lua_pushcfunction(state, script_run_lua_rename_io);
    lua_setfield(state, -2, "rename_io");

    lua_pushcfunction(state, script_run_lua_remove_node);
    lua_setfield(state, -2, "remove_node");

    lua_pushcfunction(state, script_run_lua_add_behavior_link);
    lua_setfield(state, -2, "add_behavior_link");

    lua_pushcfunction(state, script_run_lua_rewire_behavior_link);
    lua_setfield(state, -2, "rewire_behavior_link");

    lua_pushcfunction(state, script_run_lua_set_behavior_link_delay);
    lua_setfield(state, -2, "set_behavior_link_delay");

    lua_pushcfunction(state, script_run_lua_remove_behavior_link);
    lua_setfield(state, -2, "remove_behavior_link");

    lua_pushcfunction(state, script_run_lua_add_parameter);
    lua_setfield(state, -2, "add_parameter");

    lua_pushcfunction(state, script_run_lua_connect_parameter);
    lua_setfield(state, -2, "connect_parameter");

    lua_pushcfunction(state, script_run_lua_connect_parameter_to_handle);
    lua_setfield(state, -2, "connect_parameter_to_handle");

    lua_pushcfunction(state, script_run_lua_disconnect_parameter);
    lua_setfield(state, -2, "disconnect_parameter");

    lua_pushcfunction(state, script_run_lua_remove_parameter);
    lua_setfield(state, -2, "remove_parameter");

    lua_pushcfunction(state, script_run_lua_add_operation);
    lua_setfield(state, -2, "add_operation");

    lua_pushcfunction(state, script_run_lua_rewire_operation);
    lua_setfield(state, -2, "rewire_operation");

    lua_pushcfunction(state, script_run_lua_remove_operation);
    lua_setfield(state, -2, "remove_operation");

    lua_pushcfunction(state, script_run_lua_replace_bb);
    lua_setfield(state, -2, "replace_bb");

    lua_pushcfunction(state, script_run_lua_fold);
    lua_setfield(state, -2, "fold");

    lua_pushcfunction(state, script_run_lua_set_parameter_value);
    lua_setfield(state, -2, "set_parameter_value");

    lua_pushcfunction(state, script_run_lua_set_parameter_value_from_handle);
    lua_setfield(state, -2, "set_parameter_value_from_handle");

    lua_pushcfunction(state, script_run_lua_set_parameter_bytes);
    lua_setfield(state, -2, "set_parameter_bytes");

    lua_pushcfunction(state, script_run_lua_set_parameter_bytes_from_handle);
    lua_setfield(state, -2, "set_parameter_bytes_from_handle");

    lua_pushcfunction(state, script_run_lua_set_data_cell);
    lua_setfield(state, -2, "set_data_cell");

    return 1;
}

static nmo_status_t script_run_read_file(const char *path,
                                         char **out_text)
{
    FILE *fp = NULL;
    long size = 0;
    char *text = NULL;
    size_t bytes_read = 0u;

    if (path == NULL || out_text == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Script file path must be non-null");
    }

    *out_text = NULL;
    fp = fopen(path, "rb");
    if (fp == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_CANT_OPEN_FILE, NMO_SEVERITY_ERROR,
                         "Failed to open Lua script file: %s", path);
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        NMO_RETURN_ERROR(NMO_ERR_CANT_READ_FILE, NMO_SEVERITY_ERROR,
                         "Failed to seek Lua script file: %s", path);
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        NMO_RETURN_ERROR(NMO_ERR_CANT_READ_FILE, NMO_SEVERITY_ERROR,
                         "Failed to size Lua script file: %s", path);
    }
    rewind(fp);

    text = (char *)malloc((size_t)size + 1u);
    if (text == NULL) {
        fclose(fp);
        NMO_RETURN_ERROR(NMO_ERR_NOMEM, NMO_SEVERITY_ERROR,
                         "Failed to allocate Lua script buffer");
    }

    bytes_read = fread(text, 1, (size_t)size, fp);
    fclose(fp);
    if (bytes_read != (size_t)size) {
        free(text);
        NMO_RETURN_ERROR(NMO_ERR_CANT_READ_FILE, NMO_SEVERITY_ERROR,
                         "Failed to read Lua script file: %s", path);
    }
    text[bytes_read] = '\0';
    *out_text = text;
    NMO_RETURN_OK();
}

static nmo_status_t script_run_executor_action(nmo_behavior_execution_t *executor,
                                               void *user_data)
{
    static const nmo_lua_module_t executor_module = {
        .name = "nmo._executor",
        .open_fn = script_run_lua_open_executor_module
    };
    script_run_args_t *args = (script_run_args_t *)user_data;
    nmo_lua_runtime_t *runtime = NULL;
    char *script_text = NULL;
    nmo_status_t status = NMO_OK;

    if (args == NULL || args->script_path == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script-run action arguments");
    }

    status = script_run_read_file(args->script_path, &script_text);
    if (status != NMO_OK) {
        return status;
    }

    runtime = nmo_behavior_execution_lua_runtime(executor);
    status = nmo_lua_runtime_register_module(runtime, &executor_module);
    if (status != NMO_OK) {
        free(script_text);
        return status;
    }

    if (g_script_run_args != NULL) {
        free(script_text);
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Nested behavior execute runs are not supported");
    }

    args->execution = executor;
    g_script_run_args = args;
    status = nmo_lua_runtime_execute_string(runtime, script_text);
    g_script_run_args = NULL;
    args->execution = NULL;
    free(script_text);
    if (status != NMO_OK) {
        return status;
    }

    args->execution = executor;
    status = script_run_execute_pending_plan(args);
    if (status != NMO_OK) {
        args->execution = NULL;
        return status;
    }
    args->execution = NULL;
    return NMO_OK;
}

static bool script_run_should_save(bool dry_run,
                                   const char *output_path,
                                   void *user_data)
{
    (void)dry_run;
    (void)output_path;
    (void)user_data;
    return false;
}

static int script_run_mutate(nmo_cmd_ctx_t *ctx,
                             bool dry_run,
                             const char *output_path,
                             void *user_data)
{
    script_run_args_t *args = (script_run_args_t *)user_data;
    nmo_behavior_execute_options_t options = nmo_behavior_execute_options_default();
    nmo_status_t status = NMO_OK;

    if (ctx == NULL || args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    script_run_reset_args(args);
    args->dry_run = dry_run;
    args->input_path = ctx->file_path;

    options.label = "cli-script-run";
    options.dry_run = dry_run;

    status = nmo_behavior_execute(ctx->ctx,
                                         ctx->file_path,
                                         output_path,
                                         &options,
                                         script_run_executor_action,
                                         args,
                                         NULL);
    if (status != NMO_OK) {
        const char *message = nmo_last_error_message();
        fprintf(stderr, "Error: %s\n",
                (message != NULL && message[0] != '\0')
                    ? message
                    : nmo_error_string(status));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static bool script_run_edit_report_json(yyjson_mut_doc *doc,
                                        yyjson_mut_val *obj,
                                        const void *data)
{
    const script_run_args_t *args = (const script_run_args_t *)data;
    nmo_cli_edit_report_add_schema_v2_json(
        doc, obj, args->edit_report_ready ? &args->edit_report : NULL,
        args->dry_run);
    return true;
}

static int script_run_report(nmo_cmd_ctx_t *ctx,
                             bool dry_run,
                             const char *output_path,
                             void *user_data)
{
    script_run_args_t *args = (script_run_args_t *)user_data;
    nmo_status_t final_status = NMO_OK;

    if (ctx == NULL || args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (ctx->is_json && args->edit_report_ready && !dry_run && output_path != NULL &&
        args->edit_report.output_path == NULL) {
        (void)nmo_edit_report_set_output_path(&args->edit_report, output_path);
    }
    if (args->edit_report_ready) {
        final_status = args->edit_report.validation.final_status;
        if (final_status == NMO_OK && args->edit_report.status != NMO_OK) {
            final_status = args->edit_report.status;
        }
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_json(rec, script_run_edit_report_json, args) &&
              script_record_str_or_null(rec, "script_file", args->script_path) &&
              nmo_cli_record_raw_fmt(rec, "Script: %s\n", args->script_path) &&
              nmo_cli_record_raw_fmt(rec, "Operations: %zu\n",
                                     args->edit_report_ready ?
                                         args->edit_report.operation_count : 0u) &&
              nmo_cli_record_raw_fmt(rec, "Final status: %s\n",
                                     nmo_error_string(final_status));
    if (dry_run) {
        ok = ok && nmo_cli_record_raw(rec, "Dry-run: yes\n");
    }
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.run");
}

int nmo_cmd_script_run(int argc,
                       char **argv,
                       const nmo_cli_global_opts_t *global)
{
    static const nmo_cli_write_spec_t spec = {
        .command_name = "script.run",
        .output_required_unless_dry_run = true,
        .should_save = script_run_should_save,
    };
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t result = NMO_OPT_RESULT(vals, pos);
    script_run_args_t args = {0};
    int rc = 0;

    if (argc < 2 || argv == NULL || argv[1] == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &result) < 0 ||
        result.pos_count != 2) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    args.script_path = result.pos_args[0];
    rc = nmo_cli_run_write_command(result.pos_args[1],
                                   nmo_opt_str(&vals[OPT_OUTPUT]),
                                   nmo_opt_flag(&vals[OPT_DRY_RUN]),
                                   global,
                                   &spec,
                                   script_run_mutate,
                                   script_run_report,
                                   &args);
    script_run_reset_args(&args);
    return rc;
}
