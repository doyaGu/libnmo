/**
 * @file nmo_cmd_script.c
 * @brief Helpers the script commands share: workspaces, edit plans, reports, interface modes.
 */

#include "nmo_cmd_script_internal.h"

nmo_workspace_t *script_execution_workspace(
    nmo_behavior_execution_t *execution)
{
    return nmo_behavior_execution_workspace(execution);
}

nmo_status_t behavior_execute_cli_action_trampoline(
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

int behavior_execute_cli_run_write_command(
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
bool script_record_str_or_null(nmo_cli_record_t *rec,
                               const char *key,
                               const char *value)
{
    return value != NULL ? nmo_cli_record_str(rec, key, NULL, value)
                         : nmo_cli_record_null(rec, key, NULL, NULL);
}

bool script_edit_report_json(yyjson_mut_doc *doc,
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
nmo_cli_record_t *script_report_new(nmo_cmd_ctx_t *ctx,
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
int script_report_emit(nmo_cmd_ctx_t *ctx,
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

const char *script_manager_entry_policy_name(
    nmo_manager_entry_policy_t policy)
{
    return policy == NMO_MANAGER_ENTRY_POLICY_CREATE_MISSING
               ? "create_missing"
               : "require_existing";
}

const char *script_manager_entry_schema_name(
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

bool script_parse_manager_entry_policy_cli(
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

bool script_parse_manager_entry_schema_cli(
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

bool script_add_manager_entry(
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

const char *script_interface_mode_string(
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

bool parse_script_interface_mode(
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

bool script_workspace_interface_references_behavior(
    nmo_workspace_t *workspace,
    nmo_object_id_t parent_behavior_id,
    nmo_object_id_t behavior_id)
{
    return nmo_tool_owner_interface_references_behavior(
        workspace, parent_behavior_id, behavior_id);
}

nmo_object_id_t script_interface_root_for_object_workspace(
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

nmo_status_t script_execute_edit_plan(
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

nmo_object_id_t script_common_result_id(
    const script_command_common_t *common,
    size_t operation_index)
{
    if (!common || !common->edit_report.operations ||
        operation_index >= common->edit_report.operation_count) {
        return 0u;
    }
    return common->edit_report.operations[operation_index].result_id;
}
