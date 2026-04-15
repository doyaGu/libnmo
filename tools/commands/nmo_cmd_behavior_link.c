/**
 * @file nmo_cmd_behavior_link.c
 * @brief CLI behavior link commands: add-link, remove-link
 */

#include "nmo_cmd_behavior.h"
#include "nmo_cmd_behavior_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

#include "nmo.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "core/nmo_array.h"
#include "app/nmo_save.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "format/nmo_object.h"

#include <stdio.h>
#include <string.h>

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

    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_count > 0 ? r.pos_args[r.pos_count - 1] : NULL;
    if (!file_path) {
        fprintf(stderr, "Error: No input file specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);

    /* Validate parent behavior */
    nmo_object_t *parent_obj = nmo_object_repository_find_by_id(repo, parent_id);
    if (!parent_obj) {
        fprintf(stderr, "Error: Parent object #%u not found\n", parent_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }
    if (!c.registry ||
        !is_behavior_class(c.registry, nmo_object_get_class_id(parent_obj))) {
        fprintf(stderr, "Error: Object #%u is not a CKBehavior\n", parent_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Validate from/to IO objects exist */
    nmo_object_t *from_obj = nmo_object_repository_find_by_id(repo, from_id);
    if (!from_obj) {
        fprintf(stderr, "Error: Source IO object #%u not found\n", from_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }
    nmo_object_t *to_obj = nmo_object_repository_find_by_id(repo, to_id);
    if (!to_obj) {
        fprintf(stderr, "Error: Target IO object #%u not found\n", to_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Create CKBehaviorLink object */
    nmo_object_id_t link_id = 0;
    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));

    int create_rc = nmo_session_create_object(
        c.session, NMO_CID_BEHAVIORLINK, NULL,
        (nmo_guid_t){0, 0}, &link_id, &report);
    if (create_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to create CKBehaviorLink: %s\n",
                nmo_error_string(create_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Configure the link state */
    nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id);
    if (!link_obj) {
        fprintf(stderr, "Error: Created link #%u not found in repository\n", link_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_behaviorlink_state_t *link_state =
        (nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj);
    if (!link_state) {
        fprintf(stderr, "Error: Link #%u has no state\n", link_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    link_state->in_io_id = from_id;
    link_state->out_io_id = to_id;
    link_state->activation_delay = (int16_t)delay;
    link_state->initial_activation_delay = (int16_t)delay;
    link_state->use_new_format = true;
    link_state->has_format = true;

    /* Add link ID to parent behavior's sub_behavior_links array */
    nmo_behavior_state_t *beh_state =
        (nmo_behavior_state_t *)nmo_object_get_state(parent_obj);
    if (!beh_state) {
        fprintf(stderr, "Error: Parent behavior #%u has no state\n", parent_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_status_t append_rc = nmo_array_append(&beh_state->sub_behavior_links, &link_id);
    if (append_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to append link to parent: %s\n",
                nmo_error_string(append_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Save or dry-run */
    if (dry_run) {
        /* Clean up: remove from parent and destroy link */
        size_t idx;
        if (nmo_array_find(&beh_state->sub_behavior_links, &link_id, &idx) == NMO_OK)
            nmo_array_remove(&beh_state->sub_behavior_links, idx, NULL);
        nmo_session_destroy_objects(c.session, &link_id, 1, 0, NULL);
    } else {
        nmo_save_options_t save_opts = nmo_save_options_default();
        int save_rc = nmo_save_file(c.session, output_path, &save_opts);
        if (save_rc != NMO_OK) {
            fprintf(stderr, "Error saving file: %s\n", nmo_error_string(save_rc));
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
        }
    }

    /* Output */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc)
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "link_id", (uint64_t)link_id);
        nmo_cli_json_add_uint_safe(doc, data, "parent_id", (uint64_t)parent_id);
        nmo_cli_json_add_uint_safe(doc, data, "from_id", (uint64_t)from_id);
        nmo_cli_json_add_uint_safe(doc, data, "to_id", (uint64_t)to_id);
        nmo_cli_json_add_uint_safe(doc, data, "delay", (uint64_t)delay);
        if (!dry_run && output_path)
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);

        nmo_cmd_ctx_json_end(&c, doc, data, "behavior.add-link");
    } else {
        if (dry_run)
            fprintf(c.out, "[dry-run] ");
        fprintf(c.out, "Created link #%u: #%u -> #%u (delay: %u) in behavior #%u\n",
                link_id, from_id, to_id, delay, parent_id);
        if (!dry_run && output_path)
            fprintf(c.out, "Saved to: %s\n", output_path);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
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

    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    /* Open session */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);

    /* Validate link exists and is a CKBehaviorLink */
    nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id_val);
    if (!link_obj) {
        fprintf(stderr, "Error: Link object #%u not found\n", link_id_val);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }
    if (nmo_object_get_class_id(link_obj) != NMO_CID_BEHAVIORLINK) {
        fprintf(stderr, "Error: Object #%u is not a CKBehaviorLink (class_id=%u)\n",
                link_id_val, nmo_object_get_class_id(link_obj));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Get link state for reporting */
    const nmo_behaviorlink_state_t *link_state =
        (const nmo_behaviorlink_state_t *)nmo_object_get_state(link_obj);
    nmo_object_id_t from_id = link_state ? link_state->in_io_id : 0;
    nmo_object_id_t to_id   = link_state ? link_state->out_io_id : 0;

    /* Validate parent behavior */
    nmo_object_t *parent_obj = nmo_object_repository_find_by_id(repo, parent_id);
    if (!parent_obj) {
        fprintf(stderr, "Error: Parent object #%u not found\n", parent_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }
    if (!c.registry ||
        !is_behavior_class(c.registry, nmo_object_get_class_id(parent_obj))) {
        fprintf(stderr, "Error: Object #%u is not a CKBehavior\n", parent_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Remove link from parent's sub_behavior_links */
    nmo_behavior_state_t *beh_state =
        (nmo_behavior_state_t *)nmo_object_get_state(parent_obj);
    if (!beh_state) {
        fprintf(stderr, "Error: Parent behavior #%u has no state\n", parent_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_id_t link_id_search = link_id_val;
    size_t idx;
    if (nmo_array_find(&beh_state->sub_behavior_links, &link_id_search, &idx) == NMO_OK) {
        if (!dry_run)
            nmo_array_remove(&beh_state->sub_behavior_links, idx, NULL);
    } else {
        fprintf(stderr, "Warning: Link #%u not found in parent #%u's link list\n",
                link_id_val, parent_id);
    }

    /* Delete the link object */
    if (!dry_run) {
        nmo_object_id_t destroy_id = link_id_val;
        nmo_session_destroy_objects(c.session, &destroy_id, 1, 0, NULL);
    }

    /* Save or dry-run */
    if (!dry_run) {
        nmo_save_options_t save_opts = nmo_save_options_default();
        int save_rc = nmo_save_file(c.session, output_path, &save_opts);
        if (save_rc != NMO_OK) {
            fprintf(stderr, "Error saving file: %s\n", nmo_error_string(save_rc));
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
        }
    }

    /* Output */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc)
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "link_id", (uint64_t)link_id_val);
        nmo_cli_json_add_uint_safe(doc, data, "parent_id", (uint64_t)parent_id);
        nmo_cli_json_add_uint_safe(doc, data, "from_id", (uint64_t)from_id);
        nmo_cli_json_add_uint_safe(doc, data, "to_id", (uint64_t)to_id);
        if (!dry_run && output_path)
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);

        nmo_cmd_ctx_json_end(&c, doc, data, "behavior.remove-link");
    } else {
        if (dry_run)
            fprintf(c.out, "[dry-run] ");
        fprintf(c.out, "Removed link #%u (#%u -> #%u) from behavior #%u\n",
                link_id_val, from_id, to_id, parent_id);
        if (!dry_run && output_path)
            fprintf(c.out, "Saved to: %s\n", output_path);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}
