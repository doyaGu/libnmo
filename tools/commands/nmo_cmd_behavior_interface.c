/**
 * @file nmo_cmd_behavior_interface.c
 * @brief nmo behavior interface: the sub-action table and the helpers its edit commands share (target selection, the edit transaction, reports).
 */

#include "nmo_cmd_behavior_interface_internal.h"

const char *iface_root_kind_name(const nmo_interface_data_t *idata) {
    return (idata &&
            (idata->format_flags & NMO_INTERFACE_FORMAT_SECTIONED) &&
            (idata->format_flags & NMO_INTERFACE_FORMAT_ROOT_GRAPH))
        ? "graph"
        : "script";
}

bool iface_is_sectioned(const nmo_interface_data_t *idata) {
    return idata && (idata->format_flags & NMO_INTERFACE_FORMAT_SECTIONED);
}

bool iface_color_is_present(const nmo_interface_data_t *idata) {
    return idata && (idata->format_flags & NMO_INTERFACE_FORMAT_COLOR_PRESENT);
}

/* ================================================================
 * Interface edit: shared helpers
 * ================================================================ */

/*
 * Every interface edit runs in one script edit transaction, opened by
 * iface_resolve_then_mutate around the mutator: the layout is snapshotted when the
 * mutator opens it, and a failed or dry-run edit is rolled back instead of leaving
 * the session half edited.
 */
static nmo_script_edit_tx_t *iface_edit_tx;

nmo_interface_data_t *iface_edit_get_data(
    nmo_cmd_ctx_t *c, uint32_t target_id,
    nmo_object_t **out_obj)
{
    nmo_object_repository_t *repo = nmo_tool_owner_repository(c->workspace);
    if (nmo_tool_owner_ensure_behavior_acceleration(c->workspace) != NMO_OK) {
        fprintf(stderr, "Error: Failed to build behavior acceleration\n");
        return NULL;
    }
    nmo_object_t *beh = nmo_object_repository_find_by_id(repo, target_id);
    if (!beh) {
        fprintf(stderr, "Error: Object %u not found\n", target_id);
        return NULL;
    }
    if (!is_behavior_class(c->registry, nmo_object_get_class_id(beh))) {
        fprintf(stderr, "Error: Object %u is not a CKBehavior\n", target_id);
        return NULL;
    }
    nmo_behavior_state_t *bs = (nmo_behavior_state_t *)nmo_object_get_state(beh);
    if (!bs || !bs->interface_data) {
        fprintf(stderr, "Error: Behavior %u has no interface data\n", target_id);
        nmo_cmd_behavior_print_interface_diagnostics(stderr, c->workspace);
        return NULL;
    }
    nmo_interface_data_t *data = NULL;
    nmo_status_t open_status = iface_edit_tx != NULL
        ? nmo_script_edit_open_interface(iface_edit_tx, target_id, &data, NULL)
        : NMO_ERR_INVALID_STATE;
    if (open_status != NMO_OK) {
        fprintf(stderr, "Error: Cannot open the interface of behavior %u: %s\n",
                target_id, nmo_error_string(open_status));
        return NULL;
    }
    if (out_obj) *out_obj = beh;
    return data;
}

bool iface_validate_behavior_id(nmo_cmd_ctx_t *c, uint32_t beh_id) {
    nmo_object_repository_t *repo = nmo_tool_owner_repository(c->workspace);
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, beh_id);
    if (!obj) {
        fprintf(stderr, "Warning: Behavior %u not found in repository (may have been deleted)\n", beh_id);
        return false;
    }
    return true;
}

bool iface_parse_f32_arg(const char *text, float *out_value)
{
    if (nmo_parse_f32(text, out_value) != NMO_OK) {
        fprintf(stderr, "Error: Invalid float '%s'\n", text ? text : "");
        return false;
    }
    return true;
}

int iface_mark_changed(nmo_cmd_ctx_t *c, nmo_object_id_t target_id)
{
    (void)c;
    nmo_status_t st = iface_edit_tx != NULL
        ? nmo_script_edit_interface_changed(iface_edit_tx, target_id)
        : NMO_ERR_INVALID_STATE;
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Failed to mark interface edit: %s\n", nmo_error_string(st));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

/*
 * Write reports open with the JSON "dry_run" flag and the text "[dry-run] "
 * prefix, and close with the output path.
 */
nmo_cli_record_t *iface_report_new(bool dry_run)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_bool(rec, "dry_run", NULL, dry_run);
    ok = ok && (!dry_run || nmo_cli_record_raw(rec, "[dry-run] "));
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

int iface_report_emit(
    nmo_cmd_ctx_t *c,
    nmo_cli_record_t *rec,
    bool ok,
    bool dry_run,
    const char *output_path,
    const char *command)
{
    ok = ok && rec != NULL;
    if (ok && !dry_run && output_path != NULL) {
        ok = nmo_cli_record_str(rec, "output", NULL, output_path) &&
             nmo_cli_record_raw_fmt(rec, "Saved to: %s\n", output_path);
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    return nmo_cmd_ctx_emit_record(c, rec, command, 0, false);
}

bool iface_option_consumes_next_arg(const char *arg)
{
    if (arg == NULL || arg[0] != '-') {
        return false;
    }
    if (strchr(arg, '=') != NULL) {
        return false;
    }
    return strcmp(arg, "--output") == 0 ||
           strcmp(arg, "-o") == 0 ||
           strcmp(arg, "--body") == 0 ||
           strcmp(arg, "--text") == 0 ||
           strcmp(arg, "-t") == 0 ||
           strcmp(arg, "--rect") == 0 ||
           strcmp(arg, "-r") == 0 ||
           strcmp(arg, "--style") == 0 ||
           strcmp(arg, "-s") == 0 ||
           strcmp(arg, "--param-index") == 0 ||
           strcmp(arg, "--in-in") == 0 ||
           strcmp(arg, "--in-out") == 0 ||
           strcmp(arg, "--out-in") == 0 ||
           strcmp(arg, "--out-out") == 0;
}

int iface_strip_target_selector_args(
    int argc,
    char **argv,
    int *out_argc,
    char **out_argv,
    size_t out_capacity,
    nmo_core_object_selector_t *out_selector)
{
    if (argv == NULL || out_argc == NULL || out_argv == NULL ||
        out_selector == NULL || (size_t)argc > out_capacity) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    *out_argc = 0;
    *out_selector = (nmo_core_object_selector_t){
        .required_base_class = NMO_CID_BEHAVIOR,
        .selector_label = "Behavior",
        .type_label = "CKBehavior",
    };

    for (int i = 0; i < argc; ++i) {
        const char *arg = argv[i];
        const char *id_value = NULL;
        const char *name_value = NULL;

        if (iface_option_consumes_next_arg(arg)) {
            if (i + 1 >= argc) {
                out_argv[(*out_argc)++] = argv[i];
                continue;
            }
            if ((size_t)(*out_argc + 2) > out_capacity) {
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            out_argv[(*out_argc)++] = argv[i];
            out_argv[(*out_argc)++] = argv[++i];
            continue;
        }

        if (strcmp(arg, "--id") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --id requires a value\n");
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            id_value = argv[++i];
        } else if (strncmp(arg, "--id=", 5) == 0) {
            id_value = arg + 5;
        } else if (strcmp(arg, "--name") == 0 || strcmp(arg, "-n") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --name requires a value\n");
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            name_value = argv[++i];
        } else if (strncmp(arg, "--name=", 7) == 0) {
            name_value = arg + 7;
        } else {
            out_argv[(*out_argc)++] = argv[i];
            continue;
        }

        if (out_selector->has_id || out_selector->name != NULL) {
            fprintf(stderr, "Error: Use only one behavior selector\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        if (id_value != NULL) {
            uint32_t parsed_id = 0;
            if (!nmo_tool_parse_u32(id_value, &parsed_id)) {
                fprintf(stderr, "Error: Invalid Behavior ID '%s'\n", id_value);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            out_selector->has_id = true;
            out_selector->id = parsed_id;
        } else {
            out_selector->name = name_value;
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

int iface_prepare_target_selector(
    const nmo_core_object_selector_t *option_selector,
    const nmo_opt_result_t *parse_result,
    int positional_tail_count,
    const char *usage,
    nmo_core_object_selector_t *out_selector,
    int *out_value_offset,
    const char **out_file_path)
{
    if (option_selector == NULL || parse_result == NULL || usage == NULL ||
        out_selector == NULL || out_value_offset == NULL || out_file_path == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    bool has_selector_opt =
        option_selector->has_id ||
        (option_selector->name != NULL && option_selector->name[0] != '\0');
    size_t required_pos_count =
        (size_t)positional_tail_count + 1u + (has_selector_opt ? 0u : 1u);
    if (parse_result->pos_count < required_pos_count) {
        fprintf(stderr, "%s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    *out_selector = *option_selector;
    *out_value_offset = 0;
    if (!has_selector_opt) {
        out_selector->positional_id = parse_result->pos_args[0];
        *out_value_offset = 1;
    }
    *out_file_path = parse_result->pos_args[parse_result->pos_count - 1];
    return NMO_CLI_EXIT_SUCCESS;
}

int iface_resolve_then_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    iface_resolving_write_args_t *args =
        (iface_resolving_write_args_t *)user_data;
    if (args == NULL || args->selector == NULL || args->target_id == NULL ||
        args->mutate == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_t *target = NULL;
    nmo_object_id_t target_id = 0;
    int rc = nmo_core_resolve_one_object(c, args->selector, &target, &target_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        if (args->usage != NULL) {
            fprintf(stderr, "%s\n", args->usage);
        }
        return rc;
    }

    (void)target;
    *args->target_id = (uint32_t)target_id;

    if (nmo_tool_owner_ensure_behavior_acceleration(c->workspace) != NMO_OK) {
        fprintf(stderr, "Error: Failed to build behavior acceleration\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    nmo_status_t begin_status = nmo_script_edit_begin(c->workspace, "behavior interface edit",
                                                      &iface_edit_tx);
    if (begin_status != NMO_OK) {
        iface_edit_tx = NULL;
        fprintf(stderr, "Error: Cannot start the interface edit: %s\n",
                nmo_error_string(begin_status));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = args->mutate(c, dry_run, output_path, args->payload);
    nmo_script_edit_tx_t *tx = iface_edit_tx;
    iface_edit_tx = NULL;
    if (rc != NMO_CLI_EXIT_SUCCESS || dry_run) {
        /* A dry run reports what the edit would do and leaves the session alone. */
        nmo_script_edit_rollback(tx);
        return rc;
    }
    nmo_status_t commit_status = nmo_script_edit_commit(tx);
    if (commit_status != NMO_OK) {
        fprintf(stderr, "Error: Failed to commit the interface edit: %s\n",
                nmo_error_string(commit_status));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
}

int iface_resolved_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    iface_resolving_write_args_t *args =
        (iface_resolving_write_args_t *)user_data;
    if (args == NULL || args->report == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return args->report(c, dry_run, output_path, args->payload);
}

int iface_run_resolved_write_command(
    const char *file_path,
    const char *output_path,
    bool dry_run,
    const nmo_cli_global_opts_t *global,
    const nmo_cli_write_spec_t *spec,
    const nmo_core_object_selector_t *selector,
    uint32_t *target_id,
    void *payload,
    nmo_cli_write_mutate_fn mutate,
    nmo_cli_write_report_fn report,
    const char *usage)
{
    iface_resolving_write_args_t resolving_args = {
        .selector = selector,
        .target_id = target_id,
        .payload = payload,
        .mutate = mutate,
        .report = report,
        .usage = usage,
    };
    return nmo_cli_run_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        spec,
        iface_resolve_then_mutate,
        report != NULL ? iface_resolved_report : NULL,
        &resolving_args);
}

/* ================================================================
 * Sub-action table
 * ================================================================ */

const nmo_cli_action_t nmo_behavior_interface_sub_actions[] = {
    {"show",           NULL, "Show interface layout data",  nmo_cmd_behavior_iface_show,           NULL, NULL, 0, NULL, NMO_REPL_ACTION_READ_SESSION},
    {"set-pos",        NULL, "Move behavior position",      nmo_cmd_behavior_iface_set_pos,        NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"fold",           NULL, "Fold behavior",               nmo_cmd_behavior_iface_fold,           NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"unfold",         NULL, "Unfold behavior",             nmo_cmd_behavior_iface_unfold,         NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"set-color",      NULL, "Set script color",            nmo_cmd_behavior_iface_set_color,      NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"canonicalize",   NULL, "Rewrite interface chunk canonically", nmo_cmd_behavior_iface_canonicalize, NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"add-comment",    NULL, "Add layout comment",          nmo_cmd_behavior_iface_add_comment,    NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"remove-comment",    NULL, "Remove layout comment",       nmo_cmd_behavior_iface_remove_comment,    NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    /* comment edits */
    {"set-comment-text",  NULL, "Set comment text",             nmo_cmd_behavior_iface_set_comment_text,  NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"move-comment",      NULL, "Move/resize comment",          nmo_cmd_behavior_iface_move_comment,      NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"set-comment-style", NULL, "Set comment style flags",      nmo_cmd_behavior_iface_set_comment_style, NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    /* link edits */
    {"add-point",         NULL, "Add link routing point",       nmo_cmd_behavior_iface_add_point,         NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"clear-points",      NULL, "Clear link routing points",    nmo_cmd_behavior_iface_clear_points,      NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"remove-point",      NULL, "Remove link routing point",    nmo_cmd_behavior_iface_remove_point,      NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"move-point",        NULL, "Move link routing point",      nmo_cmd_behavior_iface_move_point,        NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"set-link-highlight",NULL, "Toggle link highlight",        nmo_cmd_behavior_iface_set_link_highlight,NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    /* element edits */
    {"move-op",           NULL, "Move operation position",      nmo_cmd_behavior_iface_move_op,           NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"move-param",        NULL, "Move parameter grid position", nmo_cmd_behavior_iface_move_param,        NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"set-param-style",   NULL, "Set parameter style",          nmo_cmd_behavior_iface_set_param_style,   NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    /* structure edits */
    {"resize",            NULL, "Resize sub-behavior",          nmo_cmd_behavior_iface_resize,            NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"set-expand",        NULL, "Set expand size",              nmo_cmd_behavior_iface_set_expand,        NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"set-viewport",      NULL, "Set editor viewport",          nmo_cmd_behavior_iface_set_viewport,      NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    {"set-graph-io",      NULL, "Set graph IO port ordering",   nmo_cmd_behavior_iface_set_graph_io,      NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
    /* bulk */
    {"translate",         NULL, "Translate all positions",       nmo_cmd_behavior_iface_translate,         NULL, NULL, 0, NULL, NMO_REPL_ACTION_MUTATE_FILE_ONLY},
};

_Static_assert(
    sizeof(nmo_behavior_interface_sub_actions) / sizeof(nmo_behavior_interface_sub_actions[0])
        == NMO_BEHAVIOR_INTERFACE_SUB_ACTION_COUNT,
    "sub-action table size must match NMO_BEHAVIOR_INTERFACE_SUB_ACTION_COUNT");
