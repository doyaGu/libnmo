/**
 * @file nmo_cmd_behavior_link.c
 * @brief CLI behavior link commands: add-link, remove-link
 */

#include "nmo_cmd_behavior.h"
#include "nmo_cmd_behavior_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_write.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

#include "nmo.h"
#include "session/nmo_session.h"
#include "session/nmo_session_edit.h"
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
} behavior_add_link_args_t;

typedef struct behavior_remove_link_args {
    uint32_t parent_id;
    uint32_t link_id;
    nmo_object_id_t from_id;
    nmo_object_id_t to_id;
} behavior_remove_link_args_t;

static int behavior_add_link_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    behavior_add_link_args_t *args = (behavior_add_link_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (dry_run) {
        return NMO_CLI_EXIT_SUCCESS;
    }

    nmo_session_edit_t *edit = NULL;
    int add_rc = nmo_session_edit_begin(c->session, "behavior add-link", &edit);
    if (add_rc == NMO_OK) {
        add_rc = nmo_session_edit_add_behavior_link(
            edit,
            args->parent_id,
            args->from_id,
            args->to_id,
            (int16_t)args->delay,
            &args->link_id);
    }
    if (add_rc == NMO_OK) {
        add_rc = nmo_session_edit_commit(edit);
    } else if (edit != NULL) {
        nmo_session_edit_rollback(edit);
    }
    if (add_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to add link: %s\n",
                nmo_error_string(add_rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
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
    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (doc == NULL) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "link_id", (uint64_t)args->link_id);
        nmo_cli_json_add_uint_safe(doc, data, "parent_id", (uint64_t)args->parent_id);
        nmo_cli_json_add_uint_safe(doc, data, "from_id", (uint64_t)args->from_id);
        nmo_cli_json_add_uint_safe(doc, data, "to_id", (uint64_t)args->to_id);
        nmo_cli_json_add_uint_safe(doc, data, "delay", (uint64_t)args->delay);
        if (!dry_run && output_path != NULL) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }

        nmo_cmd_ctx_json_end(c, doc, data, "behavior.add-link");
    } else {
        if (dry_run) {
            fprintf(c->out, "[dry-run] ");
        }
        fprintf(c->out, "Created link #%u: #%u -> #%u (delay: %u) in behavior #%u\n",
                args->link_id, args->from_id, args->to_id, args->delay, args->parent_id);
        if (!dry_run && output_path != NULL) {
            fprintf(c->out, "Saved to: %s\n", output_path);
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
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

    nmo_object_repository_t *repo = nmo_session_get_repository(c->session);
    nmo_object_t *link_obj = repo ? nmo_object_repository_find_by_id(repo, args->link_id) : NULL;
    if (link_obj != NULL) {
        const nmo_behaviorlink_state_t *link_state =
            (const nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj);
        if (link_state != NULL) {
            args->from_id = link_state->in_io_id;
            args->to_id = link_state->out_io_id;
        }
    }

    if (dry_run) {
        return NMO_CLI_EXIT_SUCCESS;
    }

    nmo_session_edit_t *edit = NULL;
    int rm_rc = nmo_session_edit_begin(c->session, "behavior remove-link", &edit);
    if (rm_rc == NMO_OK) {
        rm_rc = nmo_session_edit_remove_behavior_link(edit, args->parent_id, args->link_id);
    }
    if (rm_rc == NMO_OK) {
        rm_rc = nmo_session_edit_commit(edit);
    } else if (edit != NULL) {
        nmo_session_edit_rollback(edit);
    }
    if (rm_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to remove link: %s\n",
                nmo_error_string(rm_rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
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
    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (doc == NULL) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "link_id", (uint64_t)args->link_id);
        nmo_cli_json_add_uint_safe(doc, data, "parent_id", (uint64_t)args->parent_id);
        nmo_cli_json_add_uint_safe(doc, data, "from_id", (uint64_t)args->from_id);
        nmo_cli_json_add_uint_safe(doc, data, "to_id", (uint64_t)args->to_id);
        if (!dry_run && output_path != NULL) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }

        nmo_cmd_ctx_json_end(c, doc, data, "behavior.remove-link");
    } else {
        if (dry_run) {
            fprintf(c->out, "[dry-run] ");
        }
        fprintf(c->out, "Removed link #%u (#%u -> #%u) from behavior #%u\n",
                args->link_id, args->from_id, args->to_id, args->parent_id);
        if (!dry_run && output_path != NULL) {
            fprintf(c->out, "Saved to: %s\n", output_path);
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
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
        {"--output",  "-o", NMO_OPT_STRING, "Output file (required unless --dry-run)"},
        {"--dry-run", NULL,  NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_PARENT, OPT_FROM, OPT_TO, OPT_DELAY, OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
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
    uint32_t delay     = vals[OPT_DELAY].present ? vals[OPT_DELAY].val.u : 1;
    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

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
        {"--output",  "-o", NMO_OPT_STRING, "Output file (required unless --dry-run)"},
        {"--dry-run", NULL,  NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_PARENT, OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
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
    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;

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
