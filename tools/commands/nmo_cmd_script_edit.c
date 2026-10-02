/**
 * @file nmo_cmd_script_edit.c
 * @brief nmo script node, io and link: edit the nodes, IOs and links of a script.
 */

#include "nmo_cmd_script_internal.h"

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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
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
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
        script_node_add_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARENT].present || !vals[OPT_BB_GUID].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.parent_id = vals[OPT_PARENT].val.u;
        args.bb_guid = nmo_guid_parse(vals[OPT_BB_GUID].val.str);
        args.name = nmo_opt_str(&vals[OPT_NAME]);
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
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
        };
        enum { OPT_PARENT, OPT_NODE, OPT_INTERFACE, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
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
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
        };
        enum { OPT_BEHAVIOR, OPT_KIND, OPT_NAME, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
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
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
        };
        enum { OPT_IO, OPT_NAME, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
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
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
        };
        enum { OPT_IO, OPT_INTERFACE, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
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
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
        };
        enum {
            OPT_PARENT, OPT_FROM, OPT_TO, OPT_DELAY, OPT_OUTPUT, OPT_DRY_RUN,
            OPT_COUNT
        };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
        script_link_add_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARENT].present || !vals[OPT_FROM].present ||
            !vals[OPT_TO].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.parent_id = vals[OPT_PARENT].val.u;
        args.from_id = vals[OPT_FROM].val.u;
        args.to_id = vals[OPT_TO].val.u;
        args.delay = nmo_opt_uint_or(&vals[OPT_DELAY], 1u);
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
        };
        enum { OPT_LINK, OPT_FROM, OPT_TO, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
        script_link_rewire_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_LINK].present ||
            (!vals[OPT_FROM].present && !vals[OPT_TO].present) ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.link_id = vals[OPT_LINK].val.u;
        args.from_id = nmo_opt_uint_or(&vals[OPT_FROM], 0u);
        args.to_id = nmo_opt_uint_or(&vals[OPT_TO], 0u);
        return behavior_execute_cli_run_write_command(
            r.pos_args[0],
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
        };
        enum { OPT_LINK, OPT_DELAY, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
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
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
            NMO_OPT_DEF_OUTPUT,
            NMO_OPT_DEF_DRY_RUN,
        };
        enum { OPT_PARENT, OPT_LINK, OPT_INTERFACE, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
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
            nmo_opt_str(&vals[OPT_OUTPUT]),
            nmo_opt_flag(&vals[OPT_DRY_RUN]),
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
