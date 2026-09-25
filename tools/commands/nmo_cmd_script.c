/**
 * @file nmo_cmd_script.c
 * @brief CLI script graph command implementation.
 */

#include "nmo_cmd_script.h"

#include "../nmo_cli_json.h"
#include "../nmo_edit_report_json.h"
#include "../nmo_cli_write.h"
#include "../nmo_cmd_core.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"
#include "../nmo_tool_owner.h"
#include "../nmo_tool_session.h"

#include "behavior/nmo_behavior_view.h"
#include "behavior/nmo_behavior_execute.h"
#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_behavior_query.h"
#include "behavior/nmo_edit_plan.h"
#include "behavior/nmo_script_edit.h"
#include "behavior/nmo_script_edit_graph.h"
#include "core/nmo_array.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_interface_view.h"
#include "lua/nmo_lua_fold_map_parser.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "runtime/nmo_context.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_type_system.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lauxlib.h"

/* Task 2 freezes the future script write option spellings in
 * tests/fixtures/script_edit_reports.md. Keep CLI long options aligned with
 * Lua option-table fields through the direct kebab-case -> snake_case mapping.
 */
static const char *script_interface_mode_string(
    nmo_script_edit_interface_mode_t mode);
static nmo_object_id_t script_interface_root_for_object_workspace(
    nmo_workspace_t *workspace,
    nmo_object_id_t object_id);
static bool script_workspace_interface_references_behavior(
    nmo_workspace_t *workspace,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t node_id);

static bool parse_script_graph_args(int argc,
                                    char **argv,
                                    bool expect_file_operand,
                                    nmo_core_object_selector_t *out_selector,
                                    const char **out_file,
                                    bool *out_dot,
                                    uint32_t *out_depth)
{
    static const nmo_opt_def_t opts[] = {
        {"--dot", NULL, NMO_OPT_FLAG, "Emit DOT graph output"},
        {"--depth", "-d", NMO_OPT_UINT, "Recursion depth (default: unlimited)"},
        {"--id", "-i", NMO_OPT_UINT, "Script root behavior object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Script root behavior name"},
    };
    enum { OPT_DOT, OPT_DEPTH, OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t result = {
        .vals = vals,
        .pos_args = pos,
        .pos_capacity = 16
    };
    bool has_selector_opt = false;
    const char *positional_id = NULL;
    const char *file_path = NULL;

    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &result) < 0) {
        return false;
    }

    has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    if (expect_file_operand) {
        if ((has_selector_opt && result.pos_count < 1) ||
            (!has_selector_opt && result.pos_count < 2)) {
            return false;
        }
        positional_id = has_selector_opt ? NULL : result.pos_args[0];
        file_path = result.pos_args[result.pos_count - 1];
    } else if (has_selector_opt) {
        if (result.pos_count != 0) {
            return false;
        }
    } else {
        if (result.pos_count != 1) {
            return false;
        }
        positional_id = result.pos_args[0];
    }

    if (out_selector) {
        *out_selector = (nmo_core_object_selector_t){
            .has_id = vals[OPT_ID].present,
            .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
            .positional_id = positional_id,
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .required_base_class = NMO_CID_BEHAVIOR,
            .selector_label = "Script root",
            .type_label = "CKBehavior"
        };
    }
    if (out_file) {
        *out_file = file_path;
    }
    if (out_dot) {
        *out_dot = vals[OPT_DOT].val.flag;
    }
    if (out_depth) {
        *out_depth = vals[OPT_DEPTH].present ? vals[OPT_DEPTH].val.u : UINT32_MAX;
    }

    return true;
}

static const char *node_kind_name(nmo_script_edit_node_kind_t kind)
{
    switch (kind) {
    case NMO_SCRIPT_EDIT_NODE_BEHAVIOR:
        return "behavior";
    case NMO_SCRIPT_EDIT_NODE_IO:
        return "io";
    case NMO_SCRIPT_EDIT_NODE_PARAMETER:
        return "parameter";
    case NMO_SCRIPT_EDIT_NODE_OPERATION:
        return "operation";
    case NMO_SCRIPT_EDIT_NODE_LINK:
        return "link";
    default:
        return "unknown";
    }
}

/* DOT label text, escaped for a double-quoted DOT string; free() it. */
static char *dot_label_dup(const char *label)
{
    size_t len = label ? strlen(label) : 0;
    char *out = (char *)malloc(len * 2u + 1u);
    if (!out) {
        return NULL;
    }
    char *w = out;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)label[i];
        if (c == '"' || c == '\\') {
            *w++ = '\\';
            *w++ = (char)c;
        } else if (c == '\n' || c == '\r') {
            *w++ = '\\';
            *w++ = 'n';
        } else if (c == '\t') {
            *w++ = '\\';
            *w++ = 't';
        } else if (isprint(c)) {
            *w++ = (char)c;
        } else {
            *w++ = '?';
        }
    }
    *w = '\0';
    return out;
}

typedef struct script_command_common {
    bool dry_run;
    nmo_edit_report_t edit_report;
} script_command_common_t;

typedef nmo_status_t (*behavior_execute_cli_action_fn)(
    nmo_behavior_execution_t *execution,
    void *user_data);

typedef struct behavior_execute_cli_action_state {
    behavior_execute_cli_action_fn action;
    void *action_user_data;
} behavior_execute_cli_action_state_t;

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

static nmo_workspace_t *script_execution_workspace(
    nmo_behavior_execution_t *execution)
{
    return nmo_behavior_execution_workspace(execution);
}

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

static nmo_status_t behavior_execute_cli_action_trampoline(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    behavior_execute_cli_action_state_t *state =
        (behavior_execute_cli_action_state_t *)user_data;
    nmo_status_t status = NMO_OK;

    if (state == NULL || state->action == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing behavior execute CLI action");
    }

    status = state->action(executor, state->action_user_data);
    if (status != NMO_OK) {
        nmo_error_code_t error_code = nmo_last_error_code();
        /* Copy the message first: setting the error below overwrites it. */
        const char *message = nmo_last_error_message();
        char *error_message =
            (message && message[0] != '\0') ? nmo_tool_strdup(message) : NULL;
        nmo_last_error_setf(
            error_code != NMO_OK ? error_code : (nmo_error_code_t)status,
            NMO_SEVERITY_ERROR,
            __FILE__,
            __LINE__,
            "%s",
            error_message ? error_message : nmo_error_string(status));
        free(error_message);
    }
    return status;
}

static int behavior_execute_cli_run_write_command(
    const char *input_path,
    const char *output_path,
    bool dry_run,
    const nmo_cli_global_opts_t *global,
    const nmo_cli_write_spec_t *spec,
    const char *label,
    uint32_t validation_flags,
    behavior_execute_cli_action_fn action,
    nmo_cli_write_report_fn report,
    void *user_data,
    script_command_common_t *common)
{
    nmo_cmd_ctx_t ctx;
    nmo_context_t *executor_ctx = NULL;
    nmo_behavior_execute_options_t options = nmo_behavior_execute_options_default();
    behavior_execute_cli_action_state_t state = {
        .action = action,
        .action_user_data = user_data,
    };
    nmo_status_t status = NMO_OK;
    int rc = NMO_CLI_EXIT_SUCCESS;

    if (input_path == NULL || spec == NULL || action == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (spec->output_required_unless_dry_run && !dry_run && output_path == NULL) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (common != NULL) {
        memset(common, 0, sizeof(*common));
        common->dry_run = dry_run;
        (void)nmo_edit_report_init(&common->edit_report);
    }

    rc = nmo_cmd_ctx_init_no_file(&ctx, global);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    ctx.file_path = input_path;

    if (!nmo_tool_open_context(&executor_ctx, NULL)) {
        fprintf(stderr, "Error: Failed to create libnmo context\n");
        rc = NMO_CLI_EXIT_INTERNAL_ERROR;
        goto cleanup;
    }
    ctx.ctx = executor_ctx;
    ctx.registry = nmo_context_get_type_registry(executor_ctx);

    options.label = label;
    options.dry_run = dry_run;
    options.validation_flags = validation_flags;
    status = nmo_behavior_execute(executor_ctx,
                                         input_path,
                                         output_path,
                                         &options,
                                         behavior_execute_cli_action_trampoline,
                                         &state,
                                         NULL);
    if (status != NMO_OK) {
        const char *message = nmo_last_error_message();
        fprintf(stderr, "Error: %s\n",
                (message != NULL && message[0] != '\0')
                    ? message
                    : nmo_error_string(status));
        rc = NMO_CLI_EXIT_INTERNAL_ERROR;
        goto cleanup;
    }

    if (report != NULL) {
        rc = report(&ctx, dry_run, output_path, user_data);
        goto cleanup;
    }

cleanup:
    if (common != NULL) {
        nmo_edit_report_dispose(&common->edit_report);
    }
    if (executor_ctx != NULL) {
        nmo_context_release(executor_ctx);
        ctx.ctx = NULL;
        ctx.registry = NULL;
    }
    return nmo_cmd_ctx_done(&ctx, rc);
}

/* JSON only: `value` as a string, or null when it is NULL. */
static bool script_record_str_or_null(nmo_cli_record_t *rec,
                                      const char *key,
                                      const char *value)
{
    return value != NULL ? nmo_cli_record_str(rec, key, NULL, value)
                         : nmo_cli_record_null(rec, key, NULL, NULL);
}

static bool script_edit_report_json(yyjson_mut_doc *doc,
                                    yyjson_mut_val *obj,
                                    const void *data)
{
    const script_command_common_t *common = (const script_command_common_t *)data;
    nmo_cli_edit_report_add_schema_v2_json(doc, obj, &common->edit_report,
                                           common->dry_run);
    return true;
}

/*
 * A script edit command report, opened with the schema v2 edit report.
 * `common` must outlive the record. NULL on allocation failure.
 */
static nmo_cli_record_t *script_report_new(nmo_cmd_ctx_t *ctx,
                                           script_command_common_t *common,
                                           bool dry_run,
                                           const char *output_path)
{
    if (ctx->is_json && !dry_run && output_path != NULL &&
        common->edit_report.output_path == NULL) {
        (void)nmo_edit_report_set_output_path(&common->edit_report, output_path);
    }
    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (rec != NULL && !nmo_cli_record_json(rec, script_edit_report_json, common)) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

/* Close a script command report with where it was saved, then write it. */
static int script_report_emit(nmo_cmd_ctx_t *ctx,
                              nmo_cli_record_t *rec,
                              bool ok,
                              bool dry_run,
                              const char *output_path,
                              const char *cmd_name)
{
    if (!dry_run && output_path) {
        ok = ok && nmo_cli_record_str(rec, "output", NULL, output_path) &&
             nmo_cli_record_raw_fmt(rec, "Saved to: %s\n", output_path);
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    return nmo_cmd_ctx_emit_record(ctx, rec, cmd_name, 0, false);
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

static bool script_graph_add_item(nmo_cli_record_array_t *arr,
                                  nmo_cli_record_t *item,
                                  bool ok)
{
    if (!ok) {
        nmo_cli_record_free(item);
        return false;
    }
    return nmo_cli_record_array_add(arr, item);
}

static bool script_graph_add_endpoint(nmo_cli_record_t *parent,
                                      const char *key,
                                      const nmo_script_edit_endpoint_t *endpoint)
{
    nmo_cli_record_t *value = nmo_cli_record_object(parent, key);
    return value != NULL &&
           nmo_cli_record_uint(value, "object_id", NULL, endpoint->object_id) &&
           nmo_cli_record_uint(value, "owner_behavior_id", NULL,
                               endpoint->owner_behavior_id) &&
           nmo_cli_record_int(value, "owner_index", NULL, endpoint->owner_index) &&
           nmo_cli_record_uint(value, "kind", NULL, endpoint->kind);
}

/* JSON: "nodes", "control_edges" and "data_edges". Absent from text. */
static bool script_graph_add_elements(nmo_cli_record_t *rec,
                                      const nmo_script_edit_graph_t *graph)
{
    size_t node_count = 0;
    size_t control_edge_count = 0;
    size_t data_edge_count = 0;
    const nmo_script_edit_node_t *nodes =
        nmo_script_edit_graph_nodes(graph, &node_count);
    const nmo_script_edit_control_edge_t *control_edges =
        nmo_script_edit_graph_control_edges(graph, &control_edge_count);
    const nmo_script_edit_data_edge_t *data_edges =
        nmo_script_edit_graph_data_edges(graph, &data_edge_count);

    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, "nodes", NULL);
    bool ok = arr != NULL;
    for (size_t i = 0; ok && i < node_count; ++i) {
        const nmo_script_edit_node_t *node = &nodes[i];
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = script_graph_add_item(arr, item, item != NULL &&
            nmo_cli_record_uint(item, "object_id", NULL, node->object_id) &&
            nmo_cli_record_str(item, "kind", NULL, node_kind_name(node->kind)) &&
            nmo_cli_record_str_opt(item, "name", NULL, node->name, NULL) &&
            nmo_cli_record_str_opt(item, "class_name", NULL, node->class_name, NULL) &&
            nmo_cli_record_uint(item, "class_id", NULL, node->class_id) &&
            nmo_cli_record_uint(item, "depth", NULL, node->depth) &&
            nmo_cli_record_uint(item, "parent_behavior_id", NULL,
                                node->parent_behavior_id) &&
            nmo_cli_record_uint(item, "owner_behavior_id", NULL,
                                node->owner_behavior_id) &&
            nmo_cli_record_int(item, "owner_slot_index", NULL,
                               node->owner_slot_index) &&
            nmo_cli_record_uint(item, "owner_slot_kind", NULL,
                                node->owner_slot_kind));
    }

    arr = ok ? nmo_cli_record_array(rec, "control_edges", NULL) : NULL;
    ok = arr != NULL;
    for (size_t i = 0; ok && i < control_edge_count; ++i) {
        const nmo_script_edit_control_edge_t *edge = &control_edges[i];
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = script_graph_add_item(arr, item, item != NULL &&
            nmo_cli_record_uint(item, "link_id", NULL, edge->link_id) &&
            script_graph_add_endpoint(item, "source", &edge->source) &&
            script_graph_add_endpoint(item, "target", &edge->target) &&
            nmo_cli_record_int(item, "activation_delay", NULL,
                               edge->activation_delay) &&
            nmo_cli_record_int(item, "initial_activation_delay", NULL,
                               edge->initial_activation_delay));
    }

    arr = ok ? nmo_cli_record_array(rec, "data_edges", NULL) : NULL;
    ok = arr != NULL;
    for (size_t i = 0; ok && i < data_edge_count; ++i) {
        const nmo_script_edit_data_edge_t *edge = &data_edges[i];
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = script_graph_add_item(arr, item, item != NULL &&
            nmo_cli_record_uint(item, "source_parameter_id", NULL,
                                edge->source_parameter_id) &&
            nmo_cli_record_uint(item, "target_parameter_id", NULL,
                                edge->target_parameter_id) &&
            nmo_cli_record_uint(item, "source_owner_id", NULL, edge->source_owner_id) &&
            nmo_cli_record_uint(item, "target_owner_id", NULL, edge->target_owner_id) &&
            nmo_cli_record_str_fmt(item, "type_guid", NULL, "%08X-%08X",
                                   edge->type_guid.d1, edge->type_guid.d2) &&
            nmo_cli_record_bool(item, "shared", NULL, edge->shared));
    }
    return ok;
}

/* The graph summary: counts and reference validation. */
static bool script_graph_add_summary(nmo_cli_record_t *rec,
                                     const nmo_script_edit_graph_t *graph,
                                     nmo_object_id_t behavior_id)
{
    size_t control_edge_count = 0;
    size_t data_edge_count = 0;
    size_t broken_ref_count = 0;
    bool edit_ready = nmo_script_edit_graph_edit_ready(graph);
    bool owner_index = nmo_script_edit_graph_owner_index_available(graph);
    nmo_script_edit_graph_control_edges(graph, &control_edge_count);
    nmo_script_edit_graph_data_edges(graph, &data_edge_count);
    nmo_status_t ref_status = nmo_script_edit_graph_reference_validation_status(
        graph, &broken_ref_count);

    bool ok = nmo_cli_record_uint(rec, "root_behavior_id", NULL,
                                  nmo_script_edit_graph_root_behavior_id(graph)) &&
              nmo_cli_record_text_fmt(rec, "Script Graph", "%u", behavior_id) &&
              nmo_cli_record_bool(rec, "edit_ready", "Edit ready", edit_ready) &&
              nmo_cli_record_set_text(rec, edit_ready ? "yes" : "no") &&
              nmo_cli_record_bool(rec, "owner_index_available", "Owner index",
                                  owner_index) &&
              nmo_cli_record_set_text(rec, owner_index ? "available" : "missing") &&
              nmo_cli_record_uint(rec, "node_count", "Nodes",
                                  (uint64_t)nmo_script_edit_graph_node_count(graph)) &&
              nmo_cli_record_text_fmt(rec, "Control edges", "%zu", control_edge_count) &&
              nmo_cli_record_text_fmt(rec, "Data edges", "%zu", data_edge_count);

    nmo_cli_record_t *validation =
        ok ? nmo_cli_record_object(rec, "reference_validation") : NULL;
    return validation != NULL &&
           nmo_cli_record_int(validation, "status", NULL, ref_status) &&
           nmo_cli_record_str(validation, "status_name", NULL,
                              nmo_error_string(ref_status)) &&
           nmo_cli_record_uint(validation, "broken_count", NULL,
                               (uint64_t)broken_ref_count) &&
           nmo_cli_record_text_fmt(validation, "Reference validation", "%s (%zu broken)",
                                   nmo_error_string(ref_status), broken_ref_count);
}

/* DOT digraph of the graph: its nodes, then control and data edges. */
static bool script_graph_add_dot(nmo_cli_record_t *rec,
                                 const nmo_script_edit_graph_t *graph,
                                 nmo_object_id_t behavior_id)
{
    size_t node_count = 0;
    size_t control_edge_count = 0;
    size_t data_edge_count = 0;
    const nmo_script_edit_node_t *nodes =
        nmo_script_edit_graph_nodes(graph, &node_count);
    const nmo_script_edit_control_edge_t *control_edges =
        nmo_script_edit_graph_control_edges(graph, &control_edge_count);
    const nmo_script_edit_data_edge_t *data_edges =
        nmo_script_edit_graph_data_edges(graph, &data_edge_count);

    bool ok = nmo_cli_record_raw_fmt(rec, "digraph script_%u {\n", behavior_id);
    for (size_t i = 0; ok && i < node_count; ++i) {
        const nmo_script_edit_node_t *node = &nodes[i];
        char *label = NULL;
        if (node->name && node->name[0] != '\0') {
            label = dot_label_dup(node->name);
        } else {
            char *fallback = nmo_tool_strdup_fmt("%s #%u", node_kind_name(node->kind),
                                                 node->object_id);
            label = fallback ? dot_label_dup(fallback) : NULL;
            free(fallback);
        }
        ok = label != NULL &&
             nmo_cli_record_raw_fmt(rec, "  n%u [label=\"%s\"];\n",
                                    node->object_id, label);
        free(label);
    }
    for (size_t i = 0; ok && i < control_edge_count; ++i) {
        ok = nmo_cli_record_raw_fmt(rec, "  n%u -> n%u [label=\"ctrl:%u\"];\n",
                                    control_edges[i].source.object_id,
                                    control_edges[i].target.object_id,
                                    control_edges[i].link_id);
    }
    for (size_t i = 0; ok && i < data_edge_count; ++i) {
        ok = nmo_cli_record_raw_fmt(rec, "  n%u -> n%u [style=dashed,label=\"data\"];\n",
                                    data_edges[i].source_parameter_id,
                                    data_edges[i].target_parameter_id);
    }
    return ok && nmo_cli_record_raw(rec, "}\n");
}

static int script_graph_run(nmo_cmd_ctx_t *ctx,
                            const nmo_core_object_selector_t *selector,
                            bool emit_dot,
                            uint32_t depth,
                            bool close_ctx,
                            const char *usage)
{
    nmo_cmd_ctx_t c = *ctx;
    nmo_object_t *behavior = NULL;
    nmo_object_id_t behavior_id = 0;
    nmo_script_edit_graph_t *graph = NULL;
    int exit_code = NMO_CLI_EXIT_SUCCESS;
    int rc = 0;

    rc = nmo_core_resolve_one_object(&c, selector, &behavior, &behavior_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: %s\n", usage);
        exit_code = rc;
        goto cleanup;
    }

    rc = (int)nmo_script_edit_graph_build(c.workspace, behavior_id,
                                          depth, &graph);
    if (rc != NMO_OK) {
        const char *detail = nmo_last_error_message();
        if (detail && detail[0] != '\0') {
            fprintf(stderr, "Error: %s\n", detail);
        } else {
            fprintf(stderr, "Error: Failed to build script edit graph\n");
        }
        exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
        goto cleanup;
    }
    (void)behavior;

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (emit_dot) {
        /* DOT is written as is, in every output format */
        if (rec && script_graph_add_dot(rec, graph, behavior_id)) {
            nmo_cli_record_print_kv(rec, c.out, 0, false);
        }
        nmo_cli_record_free(rec);
        goto cleanup;
    }

    bool ok = rec != NULL &&
              script_graph_add_summary(rec, graph, behavior_id) &&
              script_graph_add_elements(rec, graph);
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    exit_code = nmo_cmd_ctx_emit_record(&c, rec, "script.graph", 0, false);

cleanup:
    nmo_script_edit_graph_destroy(graph);
    return close_ctx ? nmo_cmd_ctx_done(&c, exit_code) : exit_code;
}

int nmo_cmd_script_graph(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    nmo_core_object_selector_t selector = {0};
    const char *file_path = NULL;
    bool emit_dot = false;
    uint32_t depth = UINT32_MAX;
    const char *usage =
        "nmo script graph [--depth N] [--dot] [--id <id> | --name <name> | <id>] <file>";
    nmo_cmd_ctx_t ctx;
    int rc = 0;

    if (!parse_script_graph_args(argc, argv, true, &selector, &file_path,
                                 &emit_dot, &depth)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    (void)file_path;

    rc = nmo_cmd_ctx_init(&ctx, argc, argv, global);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    return script_graph_run(&ctx, &selector, emit_dot, depth, true, usage);
}

int nmo_cmd_script_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    nmo_core_object_selector_t selector = {0};
    const char *file_path = NULL;
    bool emit_dot = false;
    uint32_t depth = UINT32_MAX;
    const char *usage =
        "script graph [--depth N] [--dot] [--id <id> | --name <name> | <id>]";

    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: script graph ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "graph") != 0 && strcmp(argv[0], "g") != 0) {
        fprintf(stderr, "Unsupported script read action in session: %s\n", argv[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!parse_script_graph_args(argc - 1, argv + 1, false, &selector, &file_path,
                                 &emit_dot, &depth)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    (void)file_path;

    return script_graph_run(ctx, &selector, emit_dot, depth, false, usage);
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
        {"--output", "-o", NMO_OPT_STRING, "Output file"},
        {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
    };
    enum { OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t result = {
        .vals = vals,
        .pos_args = pos,
        .pos_capacity = 16
    };
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
                                   vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
                                   vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
                                   global,
                                   &spec,
                                   script_run_mutate,
                                   script_run_report,
                                   &args);
    script_run_reset_args(&args);
    return rc;
}

typedef struct script_node_add_args {
    script_command_common_t common;
    uint32_t parent_id;
    nmo_guid_t bb_guid;
    const char *name;
    nmo_manager_entry_options_t manager_entry;
    bool has_manager_entry;
    nmo_object_id_t node_id;
} script_node_add_args_t;

typedef struct script_node_remove_args {
    script_command_common_t common;
    uint32_t parent_id;
    uint32_t node_id;
    nmo_script_edit_interface_mode_t interface_mode;
} script_node_remove_args_t;

typedef struct script_io_add_args {
    script_command_common_t common;
    uint32_t behavior_id;
    nmo_script_edit_io_kind_t kind;
    const char *name;
    nmo_object_id_t io_id;
} script_io_add_args_t;

typedef struct script_io_rename_args {
    script_command_common_t common;
    uint32_t io_id;
    const char *name;
} script_io_rename_args_t;

typedef struct script_io_remove_args {
    script_command_common_t common;
    uint32_t io_id;
    nmo_script_edit_interface_mode_t interface_mode;
} script_io_remove_args_t;

typedef struct script_link_add_args {
    script_command_common_t common;
    uint32_t parent_id;
    uint32_t from_id;
    uint32_t to_id;
    uint32_t delay;
    nmo_object_id_t link_id;
} script_link_add_args_t;

typedef struct script_link_rewire_args {
    script_command_common_t common;
    uint32_t link_id;
    uint32_t from_id;
    uint32_t to_id;
} script_link_rewire_args_t;

typedef struct script_link_set_delay_args {
    script_command_common_t common;
    uint32_t link_id;
    uint32_t delay;
} script_link_set_delay_args_t;

typedef struct script_link_remove_args {
    script_command_common_t common;
    uint32_t parent_id;
    uint32_t link_id;
    nmo_script_edit_interface_mode_t interface_mode;
} script_link_remove_args_t;

typedef struct script_param_add_args {
    script_command_common_t common;
    uint32_t owner_id;
    const char *kind;
    const char *type_name;
    const char *name;
    nmo_object_id_t param_id;
} script_param_add_args_t;

typedef struct script_param_set_args {
    script_command_common_t common;
    uint32_t param_id;
    const char *value_str;
    nmo_manager_entry_options_t manager_entry;
    bool has_manager_entry;
    char *old_value;
    char *new_value;
} script_param_set_args_t;

typedef struct script_param_connect_args {
    script_command_common_t common;
    uint32_t source_id;
    uint32_t target_id;
} script_param_connect_args_t;

typedef struct script_param_disconnect_args {
    script_command_common_t common;
    uint32_t target_id;
} script_param_disconnect_args_t;

typedef struct script_param_remove_args {
    script_command_common_t common;
    uint32_t param_id;
    bool detach;
    nmo_script_edit_interface_mode_t interface_mode;
} script_param_remove_args_t;

typedef struct script_op_add_args {
    script_command_common_t common;
    uint32_t parent_id;
    nmo_guid_t op_guid;
    uint32_t in1_id;
    uint32_t in2_id;
    uint32_t out_id;
    nmo_object_id_t op_id;
} script_op_add_args_t;

typedef struct script_op_rewire_args {
    script_command_common_t common;
    uint32_t op_id;
    uint32_t slot_flags;
    uint32_t in1_id;
    uint32_t in2_id;
    uint32_t out_id;
} script_op_rewire_args_t;

typedef struct script_op_remove_args {
    script_command_common_t common;
    uint32_t op_id;
    nmo_script_edit_interface_mode_t interface_mode;
} script_op_remove_args_t;

static const char *script_manager_entry_policy_name(
    nmo_manager_entry_policy_t policy)
{
    return policy == NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING
               ? "create_missing"
               : "require_existing";
}

static const char *script_manager_entry_schema_name(
    nmo_manager_entry_schema_t schema)
{
    switch (schema) {
        case NMO_MANAGER_ENTRY_SCHEMA_MESSAGE:
            return "message";
        case NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE:
            return "attribute";
        case NMO_MANAGER_ENTRY_SCHEMA_AUTO:
        default:
            return "auto";
    }
}

static bool script_parse_manager_entry_policy_cli(
    const char *text,
    nmo_manager_entry_policy_t *out_policy)
{
    if (text == NULL || out_policy == NULL) {
        return false;
    }
    if (strcmp(text, "require-existing") == 0 ||
        strcmp(text, "require_existing") == 0) {
        *out_policy = NMO_MANAGER_ENTRY_POLICY_REQUIRE_EXISTING;
        return true;
    }
    if (strcmp(text, "create-missing") == 0 ||
        strcmp(text, "create_missing") == 0) {
        *out_policy = NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING;
        return true;
    }
    return false;
}

static bool script_parse_manager_entry_schema_cli(
    const char *text,
    nmo_manager_entry_schema_t *out_schema)
{
    if (text == NULL || out_schema == NULL) {
        return false;
    }
    if (strcmp(text, "auto") == 0) {
        *out_schema = NMO_MANAGER_ENTRY_SCHEMA_AUTO;
        return true;
    }
    if (strcmp(text, "message") == 0) {
        *out_schema = NMO_MANAGER_ENTRY_SCHEMA_MESSAGE;
        return true;
    }
    if (strcmp(text, "attribute") == 0) {
        *out_schema = NMO_MANAGER_ENTRY_SCHEMA_ATTRIBUTE;
        return true;
    }
    return false;
}

static bool script_add_manager_entry(
    nmo_cli_record_t *rec,
    const nmo_manager_entry_options_t *manager_entry)
{
    nmo_cli_record_t *entry = nmo_cli_record_object(rec, "manager_entry");
    bool ok = entry != NULL &&
              nmo_cli_record_str(entry, "policy", NULL,
                                 script_manager_entry_policy_name(manager_entry->policy)) &&
              nmo_cli_record_str(entry, "schema", NULL,
                                 script_manager_entry_schema_name(manager_entry->schema));
    if (!nmo_guid_is_null(manager_entry->manager_guid)) {
        ok = ok && nmo_cli_record_guid(entry, "manager_guid", NULL,
                                       manager_entry->manager_guid);
    }
    return ok && script_record_str_or_null(entry, "key", manager_entry->key);
}

static char *script_format_parameter_value_with_registry(
    const nmo_type_registry_t *registry,
    nmo_workspace_t *workspace,
    nmo_object_id_t param_id)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;
    const nmo_parameter_state_t *state = NULL;

    if (!workspace || !registry || param_id == 0u) {
        return NULL;
    }

    repo = nmo_tool_owner_repository(workspace);
    object = repo ? nmo_object_repository_find_by_id(repo, param_id) : NULL;
    state = object ? nmo_parameter_get_state(object) : NULL;
    if (!state) {
        return NULL;
    }

    return nmo_core_param_value_dup(state, registry, workspace);
}

static void script_param_set_args_cleanup(script_param_set_args_t *args)
{
    if (!args) {
        return;
    }
    free(args->old_value);
    free(args->new_value);
    args->old_value = NULL;
    args->new_value = NULL;
}

static bool script_parse_parameter_kind(
    const char *text,
    nmo_script_edit_parameter_kind_t *out_kind)
{
    if (!text || !out_kind) {
        return false;
    }
    if (strcmp(text, "in") == 0 || strcmp(text, "input") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_IN;
        return true;
    }
    if (strcmp(text, "out") == 0 || strcmp(text, "output") == 0) {
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

static bool script_try_resolve_parameter_type_name(
    const nmo_type_registry_t *registry,
    const char *type_name,
    nmo_guid_t *out_guid)
{
    const char *lookup_name = type_name;

    if (!registry || !type_name || !out_guid) {
        return false;
    }

    if (nmo_type_registry_name_to_guid(registry, lookup_name, out_guid) == NMO_OK) {
        return true;
    }

    if (strncmp(type_name, "CKPGUID_", 8) == 0) {
        lookup_name = type_name + 8;
        if (lookup_name[0] != '\0') {
            char *alias = nmo_tool_strdup(lookup_name);
            if (!alias) {
                return false;
            }
            for (char *p = alias; *p; ++p) {
                *p = (char)tolower((unsigned char)*p);
            }
            bool found =
                nmo_type_registry_name_to_guid(registry, alias, out_guid) == NMO_OK ||
                nmo_type_registry_name_to_guid(registry, lookup_name, out_guid) == NMO_OK;
            free(alias);
            if (found) {
                return true;
            }
        }
    }

    *out_guid = nmo_guid_parse(type_name);
    return !nmo_guid_is_null(*out_guid);
}

static const char *script_interface_mode_string(
    nmo_script_edit_interface_mode_t mode)
{
    switch (mode) {
    case NMO_SCRIPT_EDIT_INTERFACE_PRESERVE:
        return "preserve";
    case NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE:
        return "canonicalize";
    case NMO_SCRIPT_EDIT_INTERFACE_REMOVE:
        return "remove";
    }
    return "unknown";
}

static bool parse_script_interface_mode(
    const char *text,
    nmo_script_edit_interface_mode_t *out_mode)
{
    if (!text || !out_mode) {
        return false;
    }
    if (strcmp(text, "preserve") == 0) {
        *out_mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE;
        return true;
    }
    if (strcmp(text, "canonicalize") == 0) {
        *out_mode = NMO_SCRIPT_EDIT_INTERFACE_CANONICALIZE;
        return true;
    }
    if (strcmp(text, "remove") == 0) {
        *out_mode = NMO_SCRIPT_EDIT_INTERFACE_REMOVE;
        return true;
    }
    return false;
}

static bool script_workspace_interface_references_behavior(
    nmo_workspace_t *workspace,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t behavior_id);

static bool script_workspace_interface_references_behavior(
    nmo_workspace_t *workspace,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t behavior_id)
{
    return nmo_tool_owner_interface_references_behavior(
        workspace, parent_behavior_id, behavior_id);
}

static nmo_object_id_t script_interface_root_for_object_workspace(
    nmo_workspace_t *workspace,
    nmo_object_id_t object_id)
{
    const nmo_behavior_index_t *index = NULL;
    const nmo_port_owner_t *owner = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_object_id_t behavior_id = 0u;
    bool found_parent = false;

    if (!workspace || object_id == 0u) {
        return 0u;
    }
    if (nmo_tool_owner_ensure_behavior_acceleration(workspace) != NMO_OK) {
        return 0u;
    }

    index = nmo_tool_owner_behavior_index(workspace);
    owner = index ? nmo_behavior_index_find(index, object_id) : NULL;
    if (!owner) {
        return 0u;
    }

    behavior_id = owner->owner_id;
    repo = nmo_tool_owner_repository(workspace);
    if (!repo) {
        return behavior_id;
    }

    do {
        found_parent = false;
        for (size_t i = 0; i < nmo_object_repository_get_count(repo); ++i) {
            nmo_object_t *object = nmo_object_repository_get_by_index(repo, i);
            nmo_behavior_state_t *state = NULL;
            nmo_object_id_t parent_id = 0u;

            if (!object || nmo_object_get_class_id(object) != NMO_CID_BEHAVIOR) {
                continue;
            }

            state = (nmo_behavior_state_t *)nmo_object_get_state(object);
            if (!state || !nmo_behavior_ref_array_find(
                    &state->sub_behaviors, behavior_id, NULL)) {
                continue;
            }

            parent_id = nmo_object_get_id(object);
            if (parent_id != 0u && parent_id != behavior_id) {
                behavior_id = parent_id;
                found_parent = true;
            }
            break;
        }
    } while (found_parent);

    return behavior_id;
}

static nmo_status_t script_execute_edit_plan(
    nmo_behavior_execution_t *executor,
    script_command_common_t *common,
    nmo_edit_plan_t *plan)
{
    if (!executor || !common || !plan) {
        return NMO_ERR_INVALID_ARGUMENT;
    }
    nmo_edit_executor_options_t options = nmo_edit_executor_options_default();
    options.dry_run = common->dry_run;
    options.validation_flags = 0u;
    return nmo_edit_executor_execute_transaction(
        nmo_behavior_execution_transaction(executor),
        plan,
        &options,
        &common->edit_report);
}

static nmo_object_id_t script_common_result_id(
    const script_command_common_t *common,
    size_t operation_index)
{
    if (!common || !common->edit_report.operations ||
        operation_index >= common->edit_report.operation_count) {
        return 0u;
    }
    return common->edit_report.operations[operation_index].result_id;
}

static nmo_status_t script_node_add_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_node_add_args_t *args = (script_node_add_args_t *)user_data;
    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script node add arguments");
    }

    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        nmo_add_node_options_t options = {
            .manager_entry = args->manager_entry,
        };
        rc = nmo_edit_plan_add_node_ex(
            plan, args->parent_id, args->bb_guid, args->name,
            args->has_manager_entry ? &options : NULL);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    args->node_id = script_common_result_id(&args->common, 0);
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_node_add_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_node_add_args_t *args = (script_node_add_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "parent_id", NULL, args->parent_id) &&
              nmo_cli_record_uint(rec, "node_id", NULL, args->node_id);
    if (args->has_manager_entry) {
        ok = ok && script_add_manager_entry(rec, &args->manager_entry);
    }
    ok = ok && nmo_cli_record_raw_fmt(rec, "Created script node #%u in behavior #%u\n",
                                      args->node_id, args->parent_id);
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.node.add");
}

static nmo_status_t script_node_remove_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_node_remove_args_t *args = (script_node_remove_args_t *)user_data;
    nmo_workspace_t *workspace = NULL;
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = NMO_OK;

    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script node remove arguments");
    }
    workspace = script_execution_workspace(executor);

    if (args->interface_mode == NMO_SCRIPT_EDIT_INTERFACE_PRESERVE &&
        script_workspace_interface_references_behavior(workspace,
                                                       args->parent_id,
                                                       args->node_id)) {
        NMO_RETURN_ERROR(NMO_ERR_VALIDATION_FAILED, NMO_SEVERITY_ERROR,
                         "Failed to apply script interface policy");
    }

    rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_remove_node(
            plan, args->parent_id, args->node_id, 0u);
    }
    if (rc == NMO_OK &&
        args->interface_mode != NMO_SCRIPT_EDIT_INTERFACE_PRESERVE) {
        rc = nmo_edit_plan_add_interface_policy(
            plan, args->parent_id, args->interface_mode);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_node_remove_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_node_remove_args_t *args = (script_node_remove_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "parent_id", NULL, args->parent_id) &&
              nmo_cli_record_uint(rec, "node_id", NULL, args->node_id) &&
              script_record_str_or_null(rec, "interface_mode",
                                        script_interface_mode_string(args->interface_mode)) &&
              nmo_cli_record_raw_fmt(rec, "Removed script node #%u from behavior #%u\n",
                                     args->node_id, args->parent_id) &&
              nmo_cli_record_raw_fmt(rec, "Interface mode: %s\n",
                                     script_interface_mode_string(args->interface_mode));
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.node.remove");
}

static nmo_status_t script_io_add_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_io_add_args_t *args = (script_io_add_args_t *)user_data;
    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script io add arguments");
    }

    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_io(
            plan, args->behavior_id, args->kind, args->name);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    args->io_id = script_common_result_id(&args->common, 0);
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_io_add_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_io_add_args_t *args = (script_io_add_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "behavior_id", NULL, args->behavior_id) &&
              nmo_cli_record_uint(rec, "io_id", NULL, args->io_id) &&
              nmo_cli_record_raw_fmt(rec, "Created IO #%u on behavior #%u\n",
                                     args->io_id, args->behavior_id);
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.io.add");
}

static nmo_status_t script_io_rename_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_io_rename_args_t *args = (script_io_rename_args_t *)user_data;
    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script io rename arguments");
    }

    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_rename_io(plan, args->io_id, args->name);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_io_rename_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_io_rename_args_t *args = (script_io_rename_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "io_id", NULL, args->io_id) &&
              nmo_cli_record_raw_fmt(rec, "Renamed IO #%u\n", args->io_id);
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.io.rename");
}

static nmo_status_t script_io_remove_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_io_remove_args_t *args = (script_io_remove_args_t *)user_data;
    nmo_workspace_t *workspace = NULL;
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = NMO_OK;
    nmo_object_id_t interface_behavior_id = 0u;

    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script io remove arguments");
    }
    workspace = script_execution_workspace(executor);
    interface_behavior_id = script_interface_root_for_object_workspace(workspace,
                                                                       args->io_id);
    if (interface_behavior_id == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Failed to resolve script interface root");
    }

    rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_remove_io(plan, args->io_id, false);
    }
    if (rc == NMO_OK &&
        args->interface_mode != NMO_SCRIPT_EDIT_INTERFACE_PRESERVE) {
        rc = nmo_edit_plan_add_interface_policy(
            plan, interface_behavior_id, args->interface_mode);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_io_remove_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_io_remove_args_t *args = (script_io_remove_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "io_id", NULL, args->io_id) &&
              script_record_str_or_null(rec, "interface_mode",
                                        script_interface_mode_string(args->interface_mode)) &&
              nmo_cli_record_raw_fmt(rec, "Removed IO #%u\n", args->io_id) &&
              nmo_cli_record_raw_fmt(rec, "Interface mode: %s\n",
                                     script_interface_mode_string(args->interface_mode));
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.io.remove");
}

static nmo_status_t script_link_add_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_link_add_args_t *args = (script_link_add_args_t *)user_data;
    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script link add arguments");
    }

    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_behavior_link(
            plan,
            args->parent_id,
            args->from_id,
            NULL,
            args->to_id,
            NULL,
            args->delay);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    args->link_id = script_common_result_id(&args->common, 0);
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_link_add_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_link_add_args_t *args = (script_link_add_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "parent_id", NULL, args->parent_id) &&
              nmo_cli_record_uint(rec, "link_id", NULL, args->link_id) &&
              nmo_cli_record_uint(rec, "from_id", NULL, args->from_id) &&
              nmo_cli_record_uint(rec, "to_id", NULL, args->to_id) &&
              nmo_cli_record_uint(rec, "delay", NULL, args->delay) &&
              nmo_cli_record_raw_fmt(rec, "Created link #%u: #%u -> #%u in behavior #%u\n",
                                     args->link_id, args->from_id, args->to_id, args->parent_id);
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.link.add");
}

static nmo_status_t script_link_rewire_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_link_rewire_args_t *args = (script_link_rewire_args_t *)user_data;
    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script link rewire arguments");
    }

    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_rewire_behavior_link(
            plan, args->link_id, args->from_id, args->to_id);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_link_rewire_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_link_rewire_args_t *args = (script_link_rewire_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "link_id", NULL, args->link_id);
    if (args->from_id != 0u) {
        ok = ok && nmo_cli_record_uint(rec, "from_id", NULL, args->from_id);
    }
    if (args->to_id != 0u) {
        ok = ok && nmo_cli_record_uint(rec, "to_id", NULL, args->to_id);
    }
    ok = ok && nmo_cli_record_raw_fmt(rec, "Rewired link #%u\n", args->link_id);
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.link.rewire");
}

static nmo_status_t script_link_set_delay_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_link_set_delay_args_t *args =
        (script_link_set_delay_args_t *)user_data;
    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script link set-delay arguments");
    }

    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_set_behavior_link_delay(
            plan, args->link_id, args->delay);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_link_set_delay_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_link_set_delay_args_t *args =
        (script_link_set_delay_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "link_id", NULL, args->link_id) &&
              nmo_cli_record_uint(rec, "delay", NULL, args->delay) &&
              nmo_cli_record_raw_fmt(rec, "Set link #%u delay to %u\n",
                                     args->link_id, args->delay);
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.link.set-delay");
}

static nmo_status_t script_link_remove_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_link_remove_args_t *args = (script_link_remove_args_t *)user_data;
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = NMO_OK;

    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script link remove arguments");
    }
    rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_remove_behavior_link(
            plan, args->parent_id, args->link_id);
    }
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_interface_policy(
            plan, args->parent_id, args->interface_mode);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_link_remove_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_link_remove_args_t *args = (script_link_remove_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "parent_id", NULL, args->parent_id) &&
              nmo_cli_record_uint(rec, "link_id", NULL, args->link_id) &&
              script_record_str_or_null(rec, "interface_mode",
                                        script_interface_mode_string(args->interface_mode)) &&
              nmo_cli_record_raw_fmt(rec, "Removed link #%u from behavior #%u\n",
                                     args->link_id, args->parent_id) &&
              nmo_cli_record_raw_fmt(rec, "Interface mode: %s\n",
                                     script_interface_mode_string(args->interface_mode));
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.link.remove");
}

int nmo_cmd_script_node(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_cli_write_spec_t spec = {
        .command_name = "script.node",
        .output_required_unless_dry_run = true,
    };
    if (argc < 2 || !argv || !argv[1]) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[1], "add") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--parent", NULL, NMO_OPT_UINT, "Parent behavior ID"},
            {"--bb-guid", NULL, NMO_OPT_STRING, "Building block GUID"},
            {"--name", NULL, NMO_OPT_STRING, "Behavior name"},
            {"--manager-entry", NULL, NMO_OPT_STRING,
             "Manager entry policy: require-existing|create-missing"},
            {"--manager-entry-schema", NULL, NMO_OPT_STRING,
             "Manager entry schema: auto|message|attribute"},
            {"--manager-entry-guid", NULL, NMO_OPT_STRING,
             "Explicit manager GUID for manager entry lookup"},
            {"--manager-entry-key", NULL, NMO_OPT_STRING,
             "Manager entry lookup/create key"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum {
            OPT_PARENT,
            OPT_BB_GUID,
            OPT_NAME,
            OPT_MANAGER_ENTRY_POLICY,
            OPT_MANAGER_ENTRY_SCHEMA,
            OPT_MANAGER_ENTRY_GUID,
            OPT_MANAGER_ENTRY_KEY,
            OPT_OUTPUT,
            OPT_DRY_RUN,
            OPT_COUNT
        };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_node_add_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARENT].present || !vals[OPT_BB_GUID].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.parent_id = vals[OPT_PARENT].val.u;
        args.bb_guid = nmo_guid_parse(vals[OPT_BB_GUID].val.str);
        args.name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL;
        args.manager_entry = nmo_manager_entry_options_default();
        args.has_manager_entry =
            vals[OPT_MANAGER_ENTRY_POLICY].present ||
            vals[OPT_MANAGER_ENTRY_SCHEMA].present ||
            vals[OPT_MANAGER_ENTRY_GUID].present ||
            vals[OPT_MANAGER_ENTRY_KEY].present;
        if (vals[OPT_MANAGER_ENTRY_POLICY].present &&
            !script_parse_manager_entry_policy_cli(
                vals[OPT_MANAGER_ENTRY_POLICY].val.str,
                &args.manager_entry.policy)) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (vals[OPT_MANAGER_ENTRY_SCHEMA].present &&
            !script_parse_manager_entry_schema_cli(
                vals[OPT_MANAGER_ENTRY_SCHEMA].val.str,
                &args.manager_entry.schema)) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (vals[OPT_MANAGER_ENTRY_GUID].present) {
            args.manager_entry.manager_guid =
                nmo_guid_parse(vals[OPT_MANAGER_ENTRY_GUID].val.str);
            if (nmo_guid_is_null(args.manager_entry.manager_guid)) {
                return NMO_CLI_EXIT_ARG_ERROR;
            }
        }
        if (vals[OPT_MANAGER_ENTRY_KEY].present) {
            args.manager_entry.key = vals[OPT_MANAGER_ENTRY_KEY].val.str;
        }
        if (nmo_guid_is_null(args.bb_guid)) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script node add",
            nmo_behavior_execute_options_default().validation_flags,
            script_node_add_execute,
            script_node_add_report,
            &args,
            &args.common);
    }

    if (strcmp(argv[1], "remove") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--parent", NULL, NMO_OPT_UINT, "Parent behavior ID"},
            {"--node", NULL, NMO_OPT_UINT, "Node ID"},
            {"--interface", NULL, NMO_OPT_STRING,
             "Interface mode: preserve|canonicalize|remove"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_PARENT, OPT_NODE, OPT_INTERFACE, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_node_remove_args_t args = {
            .interface_mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE
        };
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARENT].present || !vals[OPT_NODE].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.parent_id = vals[OPT_PARENT].val.u;
        args.node_id = vals[OPT_NODE].val.u;
        if (vals[OPT_INTERFACE].present &&
            !parse_script_interface_mode(vals[OPT_INTERFACE].val.str,
                                         &args.interface_mode)) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script node remove",
            0u,
            script_node_remove_execute,
            script_node_remove_report,
            &args,
            &args.common);
    }

    return NMO_CLI_EXIT_ARG_ERROR;
}

int nmo_cmd_script_io(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_cli_write_spec_t spec = {
        .command_name = "script.io",
        .output_required_unless_dry_run = true,
    };
    if (argc < 2 || !argv || !argv[1]) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[1], "add") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--behavior", NULL, NMO_OPT_UINT, "Owner behavior ID"},
            {"--kind", NULL, NMO_OPT_STRING, "input|output"},
            {"--name", NULL, NMO_OPT_STRING, "IO name"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_BEHAVIOR, OPT_KIND, OPT_NAME, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_io_add_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_BEHAVIOR].present || !vals[OPT_KIND].present ||
            !vals[OPT_NAME].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.behavior_id = vals[OPT_BEHAVIOR].val.u;
        args.kind = strcmp(vals[OPT_KIND].val.str, "output") == 0
            ? NMO_SCRIPT_EDIT_IO_OUTPUT
            : NMO_SCRIPT_EDIT_IO_INPUT;
        args.name = vals[OPT_NAME].val.str;
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script io add",
            nmo_behavior_execute_options_default().validation_flags,
            script_io_add_execute,
            script_io_add_report,
            &args,
            &args.common);
    }

    if (strcmp(argv[1], "rename") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--io", NULL, NMO_OPT_UINT, "IO ID"},
            {"--name", NULL, NMO_OPT_STRING, "New IO name"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_IO, OPT_NAME, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_io_rename_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_IO].present || !vals[OPT_NAME].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.io_id = vals[OPT_IO].val.u;
        args.name = vals[OPT_NAME].val.str;
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script io rename",
            nmo_behavior_execute_options_default().validation_flags,
            script_io_rename_execute,
            script_io_rename_report,
            &args,
            &args.common);
    }

    if (strcmp(argv[1], "remove") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--io", NULL, NMO_OPT_UINT, "IO ID"},
            {"--interface", NULL, NMO_OPT_STRING,
             "Interface mode: preserve|canonicalize|remove"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_IO, OPT_INTERFACE, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_io_remove_args_t args = {
            .interface_mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE
        };
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_IO].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (vals[OPT_INTERFACE].present &&
            !parse_script_interface_mode(vals[OPT_INTERFACE].val.str,
                                         &args.interface_mode)) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.io_id = vals[OPT_IO].val.u;
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script io remove",
            0u,
            script_io_remove_execute,
            script_io_remove_report,
            &args,
            &args.common);
    }

    return NMO_CLI_EXIT_ARG_ERROR;
}

int nmo_cmd_script_link(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_cli_write_spec_t spec = {
        .command_name = "script.link",
        .output_required_unless_dry_run = true,
    };
    if (argc < 2 || !argv || !argv[1]) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[1], "add") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--parent", NULL, NMO_OPT_UINT, "Parent behavior ID"},
            {"--from", NULL, NMO_OPT_UINT, "Source IO ID"},
            {"--to", NULL, NMO_OPT_UINT, "Target IO ID"},
            {"--delay", NULL, NMO_OPT_UINT, "Activation delay"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum {
            OPT_PARENT, OPT_FROM, OPT_TO, OPT_DELAY, OPT_OUTPUT, OPT_DRY_RUN,
            OPT_COUNT
        };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_link_add_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARENT].present || !vals[OPT_FROM].present ||
            !vals[OPT_TO].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.parent_id = vals[OPT_PARENT].val.u;
        args.from_id = vals[OPT_FROM].val.u;
        args.to_id = vals[OPT_TO].val.u;
        args.delay = vals[OPT_DELAY].present ? vals[OPT_DELAY].val.u : 1u;
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script link add",
            0u,
            script_link_add_execute,
            script_link_add_report,
            &args,
            &args.common);
    }

    if (strcmp(argv[1], "rewire") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--link", NULL, NMO_OPT_UINT, "Link ID"},
            {"--from", NULL, NMO_OPT_UINT, "Source IO ID"},
            {"--to", NULL, NMO_OPT_UINT, "Target IO ID"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_LINK, OPT_FROM, OPT_TO, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_link_rewire_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_LINK].present ||
            (!vals[OPT_FROM].present && !vals[OPT_TO].present) ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.link_id = vals[OPT_LINK].val.u;
        args.from_id = vals[OPT_FROM].present ? vals[OPT_FROM].val.u : 0u;
        args.to_id = vals[OPT_TO].present ? vals[OPT_TO].val.u : 0u;
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script link rewire",
            0u,
            script_link_rewire_execute,
            script_link_rewire_report,
            &args,
            &args.common);
    }

    if (strcmp(argv[1], "set-delay") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--link", NULL, NMO_OPT_UINT, "Link ID"},
            {"--delay", NULL, NMO_OPT_UINT, "Activation delay"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_LINK, OPT_DELAY, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_link_set_delay_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_LINK].present || !vals[OPT_DELAY].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.link_id = vals[OPT_LINK].val.u;
        args.delay = vals[OPT_DELAY].val.u;
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script link set-delay",
            0u,
            script_link_set_delay_execute,
            script_link_set_delay_report,
            &args,
            &args.common);
    }

    if (strcmp(argv[1], "remove") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--parent", NULL, NMO_OPT_UINT, "Parent behavior ID"},
            {"--link", NULL, NMO_OPT_UINT, "Link ID"},
            {"--interface", NULL, NMO_OPT_STRING,
             "Interface mode: preserve|canonicalize|remove"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_PARENT, OPT_LINK, OPT_INTERFACE, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_link_remove_args_t args = {
            .interface_mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE
        };
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARENT].present || !vals[OPT_LINK].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (vals[OPT_INTERFACE].present &&
            !parse_script_interface_mode(vals[OPT_INTERFACE].val.str,
                                         &args.interface_mode)) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.parent_id = vals[OPT_PARENT].val.u;
        args.link_id = vals[OPT_LINK].val.u;
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script link remove",
            0u,
            script_link_remove_execute,
            script_link_remove_report,
            &args,
            &args.common);
    }

    return NMO_CLI_EXIT_ARG_ERROR;
}

static nmo_status_t script_param_add_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_param_add_args_t *args = (script_param_add_args_t *)user_data;
    nmo_script_edit_parameter_kind_t kind = NMO_SCRIPT_EDIT_PARAM_IN;
    const nmo_type_registry_t *registry = NULL;
    nmo_guid_t type_guid = NMO_GUID_NULL;
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = NMO_OK;
    if (!args || executor == NULL ||
        !script_parse_parameter_kind(args->kind, &kind)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script param add arguments");
    }
    registry = nmo_context_get_type_registry(nmo_behavior_execution_context(executor));

    if (!script_try_resolve_parameter_type_name(registry, args->type_name,
                                                &type_guid)) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Unknown parameter type '%s'", args->type_name);
    }

    rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_parameter(
            plan, args->owner_id, kind, type_guid, args->name);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    args->param_id = script_common_result_id(&args->common, 0);
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_param_add_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_param_add_args_t *args = (script_param_add_args_t *)user_data;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "owner_id", NULL, args->owner_id) &&
              nmo_cli_record_uint(rec, "param_id", NULL, args->param_id) &&
              script_record_str_or_null(rec, "kind", args->kind) &&
              script_record_str_or_null(rec, "type", args->type_name) &&
              script_record_str_or_null(rec, "name", args->name) &&
              nmo_cli_record_raw_fmt(rec, "Created script parameter #%u in behavior #%u\n",
                                     args->param_id, args->owner_id);
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.param.add");
}

static nmo_status_t script_param_set_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_param_set_args_t *args = (script_param_set_args_t *)user_data;
    const nmo_type_registry_t *registry = NULL;
    nmo_status_t rc = NMO_OK;

    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script param set arguments");
    }
    registry = nmo_context_get_type_registry(nmo_behavior_execution_context(executor));

    args->old_value =
        script_format_parameter_value_with_registry(
            registry, nmo_behavior_execution_workspace(executor), args->param_id);
    nmo_edit_plan_t *plan = NULL;
    rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        nmo_parameter_write_options_t options = {
            .manager_entry = args->manager_entry,
        };
        rc = nmo_edit_plan_add_set_parameter_value(
            plan,
            args->param_id,
            NULL,
            args->value_str,
            args->has_manager_entry ? &options : NULL);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    nmo_edit_plan_destroy(plan);
    if (rc != NMO_OK) {
        return rc;
    }

    args->new_value =
        script_format_parameter_value_with_registry(
            registry, nmo_behavior_execution_workspace(executor), args->param_id);
    return NMO_OK;
}

static int script_param_set_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_param_set_args_t *args = (script_param_set_args_t *)user_data;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "param_id", NULL, args->param_id);
    if (args->has_manager_entry) {
        ok = ok && script_add_manager_entry(rec, &args->manager_entry);
    }
    if (args->old_value) {
        ok = ok && nmo_cli_record_str(rec, "old_value", NULL, args->old_value);
    }
    if (args->new_value) {
        ok = ok && nmo_cli_record_str(rec, "new_value", NULL, args->new_value);
    }
    ok = ok && nmo_cli_record_raw_fmt(rec, "Updated script parameter #%u\n",
                                      args->param_id);
    if (args->old_value) {
        ok = ok && nmo_cli_record_raw_fmt(rec, "  Old: %s\n", args->old_value);
    }
    if (args->new_value) {
        ok = ok && nmo_cli_record_raw_fmt(rec, "  New: %s\n", args->new_value);
    }
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.param.set");
}

static nmo_status_t script_param_connect_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_param_connect_args_t *args = (script_param_connect_args_t *)user_data;
    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script param connect arguments");
    }

    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_connect_parameter(
            plan, args->source_id, args->target_id, NULL);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_param_connect_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_param_connect_args_t *args = (script_param_connect_args_t *)user_data;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "source_id", NULL, args->source_id) &&
              nmo_cli_record_uint(rec, "target_id", NULL, args->target_id) &&
              nmo_cli_record_raw_fmt(rec, "Connected parameter #%u -> #%u\n",
                                     args->source_id, args->target_id);
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.param.connect");
}

static nmo_status_t script_param_disconnect_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_param_disconnect_args_t *args =
        (script_param_disconnect_args_t *)user_data;
    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script param disconnect arguments");
    }

    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_disconnect_parameter(plan, args->target_id);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_param_disconnect_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_param_disconnect_args_t *args =
        (script_param_disconnect_args_t *)user_data;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "target_id", NULL, args->target_id) &&
              nmo_cli_record_raw_fmt(rec, "Disconnected parameter #%u\n", args->target_id);
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.param.disconnect");
}

static nmo_status_t script_param_remove_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_param_remove_args_t *args = (script_param_remove_args_t *)user_data;
    nmo_workspace_t *workspace = NULL;
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = NMO_OK;
    nmo_object_id_t interface_behavior_id = 0u;

    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script param remove arguments");
    }
    workspace = script_execution_workspace(executor);
    interface_behavior_id =
        script_interface_root_for_object_workspace(workspace, args->param_id);
    if (interface_behavior_id == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Failed to resolve script interface root");
    }

    rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_remove_parameter(
            plan, args->param_id, args->detach);
    }
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_interface_policy(
            plan, interface_behavior_id, args->interface_mode);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_param_remove_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_param_remove_args_t *args = (script_param_remove_args_t *)user_data;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "param_id", NULL, args->param_id) &&
              nmo_cli_record_bool(rec, "detach", NULL, args->detach) &&
              script_record_str_or_null(rec, "interface_mode",
                                        script_interface_mode_string(args->interface_mode)) &&
              nmo_cli_record_raw_fmt(rec, "Removed script parameter #%u\n",
                                     args->param_id) &&
              nmo_cli_record_raw_fmt(rec, "Interface mode: %s\n",
                                     script_interface_mode_string(args->interface_mode));
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.param.remove");
}

static nmo_status_t script_op_add_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_op_add_args_t *args = (script_op_add_args_t *)user_data;
    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script op add arguments");
    }

    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_operation(
            plan,
            args->parent_id,
            args->op_guid,
            args->in1_id,
            NULL,
            args->in2_id,
            NULL,
            args->out_id,
            NULL);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    args->op_id = script_common_result_id(&args->common, 0);
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_op_add_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_op_add_args_t *args = (script_op_add_args_t *)user_data;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "parent_id", NULL, args->parent_id) &&
              nmo_cli_record_uint(rec, "op_id", NULL, args->op_id) &&
              nmo_cli_record_str_fmt(rec, "operation_guid", NULL, "%08X-%08X",
                                     args->op_guid.d1, args->op_guid.d2);
    if (args->in1_id != 0u) {
        ok = ok && nmo_cli_record_uint(rec, "in1_id", NULL, args->in1_id);
    }
    if (args->in2_id != 0u) {
        ok = ok && nmo_cli_record_uint(rec, "in2_id", NULL, args->in2_id);
    }
    if (args->out_id != 0u) {
        ok = ok && nmo_cli_record_uint(rec, "out_id", NULL, args->out_id);
    }
    ok = ok && nmo_cli_record_raw_fmt(rec, "Created script operation #%u in behavior #%u\n",
                                      args->op_id, args->parent_id);
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.op.add");
}

static nmo_status_t script_op_rewire_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_op_rewire_args_t *args = (script_op_rewire_args_t *)user_data;
    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script op rewire arguments");
    }

    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_rewire_operation(
            plan,
            args->op_id,
            args->slot_flags,
            args->in1_id,
            NULL,
            args->in2_id,
            NULL,
            args->out_id,
            NULL);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_op_rewire_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_op_rewire_args_t *args = (script_op_rewire_args_t *)user_data;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "op_id", NULL, args->op_id);
    if ((args->slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u) {
        ok = ok && nmo_cli_record_uint(rec, "in1_id", NULL, args->in1_id);
    }
    if ((args->slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u) {
        ok = ok && nmo_cli_record_uint(rec, "in2_id", NULL, args->in2_id);
    }
    if ((args->slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u) {
        ok = ok && nmo_cli_record_uint(rec, "out_id", NULL, args->out_id);
    }
    ok = ok && nmo_cli_record_raw_fmt(rec, "Rewired script operation #%u\n", args->op_id);
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.op.rewire");
}

static nmo_status_t script_op_remove_execute(
    nmo_behavior_execution_t *executor,
    void *user_data)
{
    script_op_remove_args_t *args = (script_op_remove_args_t *)user_data;
    nmo_workspace_t *workspace = NULL;
    nmo_edit_plan_t *plan = NULL;
    nmo_status_t rc = NMO_OK;
    nmo_object_id_t interface_behavior_id = 0u;

    if (!args || executor == NULL) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT, NMO_SEVERITY_ERROR,
                         "Missing script op remove arguments");
    }
    workspace = script_execution_workspace(executor);
    interface_behavior_id = script_interface_root_for_object_workspace(workspace, args->op_id);
    if (interface_behavior_id == 0u) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_STATE, NMO_SEVERITY_ERROR,
                         "Failed to resolve script interface root");
    }

    rc = nmo_edit_plan_create(&plan);
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_remove_operation(plan, args->op_id);
    }
    if (rc == NMO_OK) {
        rc = nmo_edit_plan_add_interface_policy(
            plan, interface_behavior_id, args->interface_mode);
    }
    if (rc == NMO_OK) {
        rc = script_execute_edit_plan(executor, &args->common, plan);
    }
    nmo_edit_plan_destroy(plan);
    return rc;
}

static int script_op_remove_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_op_remove_args_t *args = (script_op_remove_args_t *)user_data;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = script_report_new(ctx, &args->common, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "op_id", NULL, args->op_id) &&
              script_record_str_or_null(rec, "interface_mode",
                                        script_interface_mode_string(args->interface_mode)) &&
              nmo_cli_record_raw_fmt(rec, "Removed script operation #%u\n", args->op_id) &&
              nmo_cli_record_raw_fmt(rec, "Interface mode: %s\n",
                                     script_interface_mode_string(args->interface_mode));
    return script_report_emit(ctx, rec, ok, dry_run, output_path, "script.op.remove");
}

int nmo_cmd_script_param(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_cli_write_spec_t spec = {
        .command_name = "script.param",
        .output_required_unless_dry_run = true,
    };
    if (argc < 2 || !argv || !argv[1]) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[1], "add") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--owner", NULL, NMO_OPT_UINT, "Owner behavior ID"},
            {"--kind", NULL, NMO_OPT_STRING, "in|out|local|shared"},
            {"--type", NULL, NMO_OPT_STRING, "Parameter type name or GUID"},
            {"--name", NULL, NMO_OPT_STRING, "Parameter name"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_OWNER, OPT_KIND, OPT_TYPE, OPT_NAME, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_param_add_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_OWNER].present || !vals[OPT_KIND].present ||
            !vals[OPT_TYPE].present || !vals[OPT_NAME].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.owner_id = vals[OPT_OWNER].val.u;
        args.kind = vals[OPT_KIND].val.str;
        args.type_name = vals[OPT_TYPE].val.str;
        args.name = vals[OPT_NAME].val.str;
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script param add",
            0u,
            script_param_add_execute,
            script_param_add_report,
            &args,
            &args.common);
    }

    if (strcmp(argv[1], "set") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--param", NULL, NMO_OPT_UINT, "Parameter ID"},
            {"--value", NULL, NMO_OPT_STRING, "Typed parameter value"},
            {"--manager-entry", NULL, NMO_OPT_STRING,
             "Manager entry policy: require-existing|create-missing"},
            {"--manager-entry-schema", NULL, NMO_OPT_STRING,
             "Manager entry schema: auto|message|attribute"},
            {"--manager-entry-guid", NULL, NMO_OPT_STRING,
             "Explicit manager GUID for manager entry lookup"},
            {"--manager-entry-key", NULL, NMO_OPT_STRING,
             "Manager entry lookup/create key"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum {
            OPT_PARAM,
            OPT_VALUE,
            OPT_MANAGER_ENTRY_POLICY,
            OPT_MANAGER_ENTRY_SCHEMA,
            OPT_MANAGER_ENTRY_GUID,
            OPT_MANAGER_ENTRY_KEY,
            OPT_OUTPUT,
            OPT_DRY_RUN,
            OPT_COUNT
        };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_param_set_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARAM].present || !vals[OPT_VALUE].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.param_id = vals[OPT_PARAM].val.u;
        args.value_str = vals[OPT_VALUE].val.str;
        args.manager_entry = nmo_manager_entry_options_default();
        args.has_manager_entry =
            vals[OPT_MANAGER_ENTRY_POLICY].present ||
            vals[OPT_MANAGER_ENTRY_SCHEMA].present ||
            vals[OPT_MANAGER_ENTRY_GUID].present ||
            vals[OPT_MANAGER_ENTRY_KEY].present;
        if (vals[OPT_MANAGER_ENTRY_POLICY].present &&
            !script_parse_manager_entry_policy_cli(
                vals[OPT_MANAGER_ENTRY_POLICY].val.str,
                &args.manager_entry.policy)) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (vals[OPT_MANAGER_ENTRY_SCHEMA].present &&
            !script_parse_manager_entry_schema_cli(
                vals[OPT_MANAGER_ENTRY_SCHEMA].val.str,
                &args.manager_entry.schema)) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (vals[OPT_MANAGER_ENTRY_GUID].present) {
            args.manager_entry.manager_guid =
                nmo_guid_parse(vals[OPT_MANAGER_ENTRY_GUID].val.str);
            if (nmo_guid_is_null(args.manager_entry.manager_guid)) {
                return NMO_CLI_EXIT_ARG_ERROR;
            }
        }
        if (vals[OPT_MANAGER_ENTRY_KEY].present) {
            args.manager_entry.key = vals[OPT_MANAGER_ENTRY_KEY].val.str;
        }
        {
            int rc = behavior_execute_cli_run_write_command(
                r.pos_args[0],
                vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
                vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
                global,
                &spec,
                "script param set",
                0u,
                script_param_set_execute,
                script_param_set_report,
                &args,
                &args.common);
            script_param_set_args_cleanup(&args);
            return rc;
        }
    }

    if (strcmp(argv[1], "connect") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--from", NULL, NMO_OPT_UINT, "Source parameter ID"},
            {"--to", NULL, NMO_OPT_UINT, "Target ParameterIn ID"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_FROM, OPT_TO, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_param_connect_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_FROM].present || !vals[OPT_TO].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.source_id = vals[OPT_FROM].val.u;
        args.target_id = vals[OPT_TO].val.u;
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script param connect",
            0u,
            script_param_connect_execute,
            script_param_connect_report,
            &args,
            &args.common);
    }

    if (strcmp(argv[1], "disconnect") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--to", NULL, NMO_OPT_UINT, "Target ParameterIn ID"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_TO, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_param_disconnect_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_TO].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.target_id = vals[OPT_TO].val.u;
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script param disconnect",
            0u,
            script_param_disconnect_execute,
            script_param_disconnect_report,
            &args,
            &args.common);
    }

    if (strcmp(argv[1], "remove") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--param", NULL, NMO_OPT_UINT, "Parameter ID"},
            {"--detach", NULL, NMO_OPT_FLAG, "Detach data-flow references first"},
            {"--interface", NULL, NMO_OPT_STRING,
             "Interface mode: preserve|canonicalize|remove"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_PARAM, OPT_DETACH, OPT_INTERFACE, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_param_remove_args_t args = {
            .interface_mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE
        };
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARAM].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (vals[OPT_INTERFACE].present &&
            !parse_script_interface_mode(vals[OPT_INTERFACE].val.str,
                                         &args.interface_mode)) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.param_id = vals[OPT_PARAM].val.u;
        args.detach = vals[OPT_DETACH].present && vals[OPT_DETACH].val.flag;
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script param remove",
            0u,
            script_param_remove_execute,
            script_param_remove_report,
            &args,
            &args.common);
    }

    return NMO_CLI_EXIT_ARG_ERROR;
}

int nmo_cmd_script_op(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_cli_write_spec_t spec = {
        .command_name = "script.op",
        .output_required_unless_dry_run = true,
    };
    if (argc < 2 || !argv || !argv[1]) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[1], "add") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--parent", NULL, NMO_OPT_UINT, "Owner behavior ID"},
            {"--op-guid", NULL, NMO_OPT_STRING, "Operation GUID"},
            {"--in1", NULL, NMO_OPT_UINT, "Input 1 parameter ID"},
            {"--in2", NULL, NMO_OPT_UINT, "Input 2 parameter ID"},
            {"--out", NULL, NMO_OPT_UINT, "Output parameter ID"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_PARENT, OPT_OP_GUID, OPT_IN1, OPT_IN2, OPT_OUT, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_op_add_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARENT].present || !vals[OPT_OP_GUID].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.parent_id = vals[OPT_PARENT].val.u;
        args.op_guid = nmo_guid_parse(vals[OPT_OP_GUID].val.str);
        if (nmo_guid_is_null(args.op_guid)) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.in1_id = vals[OPT_IN1].present ? vals[OPT_IN1].val.u : 0u;
        args.in2_id = vals[OPT_IN2].present ? vals[OPT_IN2].val.u : 0u;
        args.out_id = vals[OPT_OUT].present ? vals[OPT_OUT].val.u : 0u;
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script op add",
            0u,
            script_op_add_execute,
            script_op_add_report,
            &args,
            &args.common);
    }

    if (strcmp(argv[1], "rewire") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--op", NULL, NMO_OPT_UINT, "Operation ID"},
            {"--in1", NULL, NMO_OPT_UINT, "Input 1 parameter ID (0 clears)"},
            {"--in2", NULL, NMO_OPT_UINT, "Input 2 parameter ID (0 clears)"},
            {"--out", NULL, NMO_OPT_UINT, "Output parameter ID (0 clears)"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_OP, OPT_IN1, OPT_IN2, OPT_OUT, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_op_rewire_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_OP].present ||
            (!vals[OPT_IN1].present && !vals[OPT_IN2].present && !vals[OPT_OUT].present) ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.op_id = vals[OPT_OP].val.u;
        if (vals[OPT_IN1].present) {
            args.slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_IN1;
            args.in1_id = vals[OPT_IN1].val.u;
        }
        if (vals[OPT_IN2].present) {
            args.slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_IN2;
            args.in2_id = vals[OPT_IN2].val.u;
        }
        if (vals[OPT_OUT].present) {
            args.slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_OUT;
            args.out_id = vals[OPT_OUT].val.u;
        }
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script op rewire",
            0u,
            script_op_rewire_execute,
            script_op_rewire_report,
            &args,
            &args.common);
    }

    if (strcmp(argv[1], "remove") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--op", NULL, NMO_OPT_UINT, "Operation ID"},
            {"--interface", NULL, NMO_OPT_STRING,
             "Interface mode: preserve|canonicalize|remove"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_OP, OPT_INTERFACE, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_op_remove_args_t args = {
            .interface_mode = NMO_SCRIPT_EDIT_INTERFACE_PRESERVE
        };
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_OP].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (vals[OPT_INTERFACE].present &&
            !parse_script_interface_mode(vals[OPT_INTERFACE].val.str,
                                         &args.interface_mode)) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.op_id = vals[OPT_OP].val.u;
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            "script op remove",
            0u,
            script_op_remove_execute,
            script_op_remove_report,
            &args,
            &args.common);
    }

    return NMO_CLI_EXIT_ARG_ERROR;
}





