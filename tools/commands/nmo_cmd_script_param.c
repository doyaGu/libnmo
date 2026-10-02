/**
 * @file nmo_cmd_script_param.c
 * @brief nmo script param and op: edit the parameters and operations of a script.
 */

#include "nmo_cmd_script_internal.h"

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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
        };
        enum { OPT_OWNER, OPT_KIND, OPT_TYPE, OPT_NAME, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
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
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
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
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
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
                nmo_opt_str(&vals[OPT_OUTPUT]),
                nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
        };
        enum { OPT_FROM, OPT_TO, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
        script_param_connect_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_FROM].present || !vals[OPT_TO].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.source_id = vals[OPT_FROM].val.u;
        args.target_id = vals[OPT_TO].val.u;
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
        };
        enum { OPT_TO, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
        script_param_disconnect_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_TO].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.target_id = vals[OPT_TO].val.u;
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
        };
        enum { OPT_PARAM, OPT_DETACH, OPT_INTERFACE, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
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
        args.detach = nmo_opt_flag(&vals[OPT_DETACH]);
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
        };
        enum { OPT_PARENT, OPT_OP_GUID, OPT_IN1, OPT_IN2, OPT_OUT, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
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
        args.in1_id = nmo_opt_uint_or(&vals[OPT_IN1], 0u);
        args.in2_id = nmo_opt_uint_or(&vals[OPT_IN2], 0u);
        args.out_id = nmo_opt_uint_or(&vals[OPT_OUT], 0u);
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
        };
        enum { OPT_OP, OPT_IN1, OPT_IN2, OPT_OUT, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
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
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
        };
        enum { OPT_OP, OPT_INTERFACE, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
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
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
