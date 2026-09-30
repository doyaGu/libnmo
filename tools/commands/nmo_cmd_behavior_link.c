/**
 * @file nmo_cmd_behavior_link.c
 * @brief CLI behavior link commands: add-link, remove-link
 */

#include "nmo_cmd_behavior.h"
#include "nmo_cmd_behavior_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_edit_report_json.h"
#include "../nmo_cli_write.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

#include "edit/nmo_edit_plan.h"
#include "nmo.h"
#include "runtime/nmo_workspace.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"

#include <stdio.h>
#include <string.h>

typedef struct behavior_add_link_args {
    uint32_t parent_id;
    uint32_t from_id;
    uint32_t to_id;
    uint32_t delay;
    nmo_object_id_t link_id;
    nmo_edit_plan_t *edit_plan;
    nmo_edit_report_t edit_report;
    bool edit_report_ready;
} behavior_add_link_args_t;

typedef struct behavior_remove_link_args {
    uint32_t parent_id;
    uint32_t link_id;
    nmo_object_id_t from_id;
    nmo_object_id_t to_id;
    nmo_edit_plan_t *edit_plan;
    nmo_edit_report_t edit_report;
    bool edit_report_ready;
} behavior_remove_link_args_t;

static int behavior_link_exit_code(nmo_status_t status)
{
    return (status == NMO_ERR_INVALID_ARGUMENT || status == NMO_ERR_NOT_FOUND)
        ? NMO_CLI_EXIT_ARG_ERROR
        : NMO_CLI_EXIT_INTERNAL_ERROR;
}

/* JSON splice for the edit report; `edit_report` NULL when it was not set up. */
typedef struct behavior_link_report {
    const nmo_edit_report_t *edit_report;
    bool dry_run;
} behavior_link_report_t;

static bool behavior_link_report_json(yyjson_mut_doc *doc,
                                      yyjson_mut_val *obj,
                                      const void *data)
{
    const behavior_link_report_t *report = (const behavior_link_report_t *)data;
    nmo_cli_edit_report_add_schema_v2_json(doc, obj, report->edit_report,
                                           report->dry_run);
    return true;
}

/*
 * Start a link report: the edit report in JSON, then "[dry-run] " in text.
 * `report` is borrowed by the record and must outlive it.
 */
static nmo_cli_record_t *behavior_link_report_new(nmo_cmd_ctx_t *c,
                                                  behavior_link_report_t *report,
                                                  nmo_edit_report_t *edit_report,
                                                  bool edit_report_ready,
                                                  bool dry_run,
                                                  const char *output_path)
{
    if (c->is_json && edit_report_ready && !dry_run && output_path != NULL) {
        (void)nmo_edit_report_set_output_path(edit_report, output_path);
    }
    report->edit_report = edit_report_ready ? edit_report : NULL;
    report->dry_run = dry_run;

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL && nmo_cli_record_json(rec, behavior_link_report_json, report);
    if (ok && dry_run) {
        ok = nmo_cli_record_raw(rec, "[dry-run] ");
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

/* Finish with "output" / "Saved to:" and emit; frees `rec`. */
static int behavior_link_report_emit(nmo_cmd_ctx_t *c,
                                     nmo_cli_record_t *rec,
                                     bool ok,
                                     bool dry_run,
                                     const char *output_path,
                                     const char *cmd_name)
{
    if (!dry_run && output_path != NULL) {
        ok = ok && nmo_cli_record_str(rec, "output", NULL, output_path) &&
             nmo_cli_record_raw_fmt(rec, "Saved to: %s\n", output_path);
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    return nmo_cmd_ctx_emit_record(c, rec, cmd_name, 0, false);
}

static int behavior_add_link_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    nmo_status_t add_rc = NMO_OK;

    (void)output_path;
    behavior_add_link_args_t *args = (behavior_add_link_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    add_rc = nmo_edit_report_init(&args->edit_report);
    if (add_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to initialize behavior add-link report: %s\n",
                nmo_error_string(add_rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    args->edit_report_ready = true;

    add_rc = nmo_edit_plan_create(&args->edit_plan);
    if (add_rc == NMO_OK) {
        add_rc = nmo_edit_plan_add_behavior_link(
            args->edit_plan,
            args->parent_id,
            args->from_id,
            NULL,
            args->to_id,
            NULL,
            args->delay);
    }
    if (add_rc == NMO_OK) {
        nmo_edit_executor_options_t options =
            nmo_edit_executor_options_default();
        options.dry_run = dry_run;
        add_rc = nmo_edit_executor_execute(
            c->workspace, args->edit_plan, &options, &args->edit_report);
    }
    if (add_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to add link: %s\n",
                nmo_error_string(add_rc));
        nmo_edit_plan_destroy(args->edit_plan);
        args->edit_plan = NULL;
        nmo_edit_report_dispose(&args->edit_report);
        args->edit_report_ready = false;
        return behavior_link_exit_code(add_rc);
    }

    if (args->edit_report.operation_count > 0u) {
        args->link_id = args->edit_report.operations[0].result_id;
    }
    nmo_edit_plan_destroy(args->edit_plan);
    args->edit_plan = NULL;
    return NMO_CLI_EXIT_SUCCESS;
}

static int behavior_add_link_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    behavior_add_link_args_t *args = (behavior_add_link_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    behavior_link_report_t report;
    nmo_cli_record_t *rec = behavior_link_report_new(
        c, &report, &args->edit_report, args->edit_report_ready, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "link_id", NULL, (uint64_t)args->link_id) &&
              nmo_cli_record_uint(rec, "parent_id", NULL, (uint64_t)args->parent_id) &&
              nmo_cli_record_uint(rec, "from_id", NULL, (uint64_t)args->from_id) &&
              nmo_cli_record_uint(rec, "to_id", NULL, (uint64_t)args->to_id) &&
              nmo_cli_record_uint(rec, "delay", NULL, (uint64_t)args->delay) &&
              nmo_cli_record_raw_fmt(rec,
                                     "Created link #%u: #%u -> #%u (delay: %u) in behavior #%u\n",
                                     args->link_id, args->from_id, args->to_id,
                                     args->delay, args->parent_id);
    int rc = behavior_link_report_emit(c, rec, ok, dry_run, output_path,
                                       "behavior.add-link");
    if (args->edit_report_ready) {
        nmo_edit_report_dispose(&args->edit_report);
        args->edit_report_ready = false;
    }
    return rc;
}

static int behavior_remove_link_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    behavior_remove_link_args_t *args = (behavior_remove_link_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_repository_t *repo = nmo_tool_owner_repository(c->workspace);
    nmo_object_t *link_obj = repo ? nmo_object_repository_find_by_id(repo, args->link_id) : NULL;
    if (link_obj != NULL) {
        const nmo_behaviorlink_state_t *link_state =
            (const nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj);
        if (link_state != NULL) {
            args->from_id = nmo_behaviorlink_in_io_id(link_state);
            args->to_id = nmo_behaviorlink_out_io_id(link_state);
        }
    }

    nmo_status_t rm_rc = nmo_edit_report_init(&args->edit_report);
    if (rm_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to initialize behavior remove-link report: %s\n",
                nmo_error_string(rm_rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    args->edit_report_ready = true;

    rm_rc = nmo_edit_plan_create(&args->edit_plan);
    if (rm_rc == NMO_OK) {
        rm_rc = nmo_edit_plan_add_remove_behavior_link(
            args->edit_plan,
            args->parent_id,
            args->link_id);
    }
    if (rm_rc == NMO_OK) {
        nmo_edit_executor_options_t options =
            nmo_edit_executor_options_default();
        options.dry_run = dry_run;
        rm_rc = nmo_edit_executor_execute(
            c->workspace, args->edit_plan, &options, &args->edit_report);
    }
    if (rm_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to remove link: %s\n",
                nmo_error_string(rm_rc));
        nmo_edit_plan_destroy(args->edit_plan);
        args->edit_plan = NULL;
        nmo_edit_report_dispose(&args->edit_report);
        args->edit_report_ready = false;
        return behavior_link_exit_code(rm_rc);
    }

    nmo_edit_plan_destroy(args->edit_plan);
    args->edit_plan = NULL;
    return NMO_CLI_EXIT_SUCCESS;
}

static int behavior_remove_link_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    behavior_remove_link_args_t *args = (behavior_remove_link_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    behavior_link_report_t report;
    nmo_cli_record_t *rec = behavior_link_report_new(
        c, &report, &args->edit_report, args->edit_report_ready, dry_run, output_path);
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "link_id", NULL, (uint64_t)args->link_id) &&
              nmo_cli_record_uint(rec, "parent_id", NULL, (uint64_t)args->parent_id) &&
              nmo_cli_record_uint(rec, "from_id", NULL, (uint64_t)args->from_id) &&
              nmo_cli_record_uint(rec, "to_id", NULL, (uint64_t)args->to_id) &&
              nmo_cli_record_raw_fmt(rec, "Removed link #%u (#%u -> #%u) from behavior #%u\n",
                                     args->link_id, args->from_id, args->to_id,
                                     args->parent_id);
    int rc = behavior_link_report_emit(c, rec, ok, dry_run, output_path,
                                       "behavior.remove-link");
    if (args->edit_report_ready) {
        nmo_edit_report_dispose(&args->edit_report);
        args->edit_report_ready = false;
    }
    return rc;
}

/* ============================================================================
 * behavior add-link
 *
 *   nmo behavior add-link --parent <beh-id> --from <io-id> --to <io-id>
 *       [--delay <n>] <file> -o <output> [--dry-run]
 * ============================================================================ */

int nmo_cmd_behavior_add_link(int argc, char **argv,
                              const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--parent",  "-p", NMO_OPT_UINT,   "Parent behavior ID (required)"},
        {"--from",    NULL,  NMO_OPT_UINT,   "Source IO port ID (required)"},
        {"--to",      NULL,  NMO_OPT_UINT,   "Target IO port ID (required)"},
        {"--delay",   "-d", NMO_OPT_UINT,   "Activation delay in frames (default: 1)"},
        NMO_OPT_DEF_WRITE_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_PARENT, OPT_FROM, OPT_TO, OPT_DELAY, OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    if (!vals[OPT_PARENT].present) {
        fprintf(stderr, "Error: --parent is required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_FROM].present) {
        fprintf(stderr, "Error: --from is required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_TO].present) {
        fprintf(stderr, "Error: --to is required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t parent_id = vals[OPT_PARENT].val.u;
    uint32_t from_id   = vals[OPT_FROM].val.u;
    uint32_t to_id     = vals[OPT_TO].val.u;
    uint32_t delay     = nmo_opt_uint_or(&vals[OPT_DELAY], 1);
    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);

    const char *file_path = r.pos_count > 0 ? r.pos_args[r.pos_count - 1] : NULL;
    if (!file_path) {
        fprintf(stderr, "Error: No input file specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    behavior_add_link_args_t args = {
        .parent_id = parent_id,
        .from_id = from_id,
        .to_id = to_id,
        .delay = delay,
        .link_id = 0,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.add-link",
        .output_required_unless_dry_run = true,
    };
    return nmo_cli_run_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        &spec,
        behavior_add_link_mutate,
        behavior_add_link_report,
        &args);
}

/* ============================================================================
 * behavior remove-link
 *
 *   nmo behavior remove-link <link-id> --parent <beh-id> <file>
 *       -o <output> [--dry-run]
 * ============================================================================ */

int nmo_cmd_behavior_remove_link(int argc, char **argv,
                                 const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--parent",  "-p", NMO_OPT_UINT,   "Parent behavior ID (required)"},
        NMO_OPT_DEF_WRITE_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_PARENT, OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    if (!vals[OPT_PARENT].present) {
        fprintf(stderr, "Error: --parent is required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* First positional arg is link ID, last is file */
    if (r.pos_count < 2) {
        fprintf(stderr, "Error: Usage: nmo behavior remove-link <link-id> --parent <beh-id> <file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t link_id_val = 0;
    if (!nmo_tool_parse_u32(r.pos_args[0], &link_id_val)) {
        fprintf(stderr, "Error: Invalid link ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t parent_id  = vals[OPT_PARENT].val.u;
    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);

    const char *file_path = r.pos_args[r.pos_count - 1];

    behavior_remove_link_args_t args = {
        .parent_id = parent_id,
        .link_id = link_id_val,
        .from_id = 0,
        .to_id = 0,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.remove-link",
        .output_required_unless_dry_run = true,
    };
    return nmo_cli_run_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        &spec,
        behavior_remove_link_mutate,
        behavior_remove_link_report,
        &args);
}
