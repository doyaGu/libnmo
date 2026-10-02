/**
 * @file nmo_cmd_behavior_interface_link.c
 * @brief nmo behavior interface link edits: routing points and highlight of links.
 */

#include "nmo_cmd_behavior_interface_internal.h"

typedef enum iface_link_op {
    IFACE_LINK_ADD_POINT,
    IFACE_LINK_CLEAR_POINTS,
    IFACE_LINK_REMOVE_POINT,
    IFACE_LINK_MOVE_POINT,
    IFACE_LINK_SET_HIGHLIGHT
} iface_link_op_t;

typedef struct iface_link_args {
    iface_link_op_t op;
    uint32_t target_id;
    uint32_t link_id;
    uint32_t point_index;
    float h;
    float v;
    bool highlight;
} iface_link_args_t;

static const char *iface_link_command_name(iface_link_op_t op)
{
    switch (op) {
    case IFACE_LINK_ADD_POINT:     return "behavior.interface.add-point";
    case IFACE_LINK_CLEAR_POINTS:  return "behavior.interface.clear-points";
    case IFACE_LINK_REMOVE_POINT:  return "behavior.interface.remove-point";
    case IFACE_LINK_MOVE_POINT:    return "behavior.interface.move-point";
    case IFACE_LINK_SET_HIGHLIGHT: return "behavior.interface.set-link-highlight";
    default:                       return "behavior.interface.link";
    }
}

static int iface_link_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    iface_link_args_t *args = (iface_link_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_t *beh_obj = NULL;
    nmo_interface_data_t *idata = iface_edit_get_data(c, args->target_id, &beh_obj);
    if (idata == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_interface_link_t *link = nmo_interface_find_link(idata, args->link_id);
    if (link == NULL) {
        fprintf(stderr, "Error: Link %u not found in interface data\n", args->link_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = NMO_OK;
    switch (args->op) {
    case IFACE_LINK_ADD_POINT: {
        nmo_arena_t *arena = nmo_object_get_storage_arena(beh_obj);
        st = nmo_interface_link_add_point(link, arena, args->h, args->v);
        break;
    }
    case IFACE_LINK_CLEAR_POINTS:
        nmo_interface_link_clear_points(link);
        break;
    case IFACE_LINK_REMOVE_POINT:
        st = nmo_interface_link_remove_point(link, (size_t)args->point_index);
        break;
    case IFACE_LINK_MOVE_POINT:
        if ((size_t)args->point_index >= link->point_count) {
            fprintf(stderr, "Error: Point index %u out of range (count=%zu)\n",
                    args->point_index, link->point_count);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        link->points[args->point_index * 2] = args->h;
        link->points[args->point_index * 2 + 1] = args->v;
        break;
    case IFACE_LINK_SET_HIGHLIGHT:
        link->highlight = args->highlight;
        break;
    default:
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (st != NMO_OK) {
        fprintf(stderr, "Error: %s\n", nmo_error_string(st));
        return args->op == IFACE_LINK_ADD_POINT
            ? NMO_CLI_EXIT_IO_ERROR
            : NMO_CLI_EXIT_ARG_ERROR;
    }

    if (dry_run) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    return iface_mark_changed(c, args->target_id);
}

static int iface_link_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    iface_link_args_t *args = (iface_link_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = iface_report_new(dry_run);
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_uint(rec, "target_id", NULL, args->target_id);
    ok = ok && nmo_cli_record_uint(rec, "link_id", NULL, args->link_id);
    if (args->op == IFACE_LINK_REMOVE_POINT || args->op == IFACE_LINK_MOVE_POINT) {
        ok = ok && nmo_cli_record_uint(rec, "point_index", NULL, args->point_index);
    }
    switch (args->op) {
    case IFACE_LINK_ADD_POINT:
        ok = ok && nmo_cli_record_raw_fmt(rec, "Added point (%.1f, %.1f) to link %u\n",
                                          (double)args->h, (double)args->v, args->link_id);
        break;
    case IFACE_LINK_CLEAR_POINTS:
        ok = ok && nmo_cli_record_raw_fmt(rec, "Cleared all routing points from link %u\n",
                                          args->link_id);
        break;
    case IFACE_LINK_REMOVE_POINT:
        ok = ok && nmo_cli_record_raw_fmt(rec, "Removed point %u from link %u\n",
                                          args->point_index, args->link_id);
        break;
    case IFACE_LINK_MOVE_POINT:
        ok = ok && nmo_cli_record_raw_fmt(rec, "Moved point %u of link %u to (%.1f, %.1f)\n",
                                          args->point_index, args->link_id,
                                          (double)args->h, (double)args->v);
        break;
    case IFACE_LINK_SET_HIGHLIGHT:
        ok = ok && nmo_cli_record_raw_fmt(rec, "Set link %u highlight %s\n",
                                          args->link_id, args->highlight ? "on" : "off");
        break;
    default:
        ok = false;
        break;
    }
    return iface_report_emit(c, rec, ok, dry_run, output_path,
                             iface_link_command_name(args->op));
}

/* ================================================================
 * Interface edit: link operations
 * ================================================================ */

int nmo_cmd_behavior_iface_add_point(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos_arr);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    const char *usage =
        "Usage: nmo behavior interface add-point [--id <id> | --name <name> | <id>] <link_id> <h> <v> <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 3, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }
    uint32_t link_id;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset], &link_id)) {
        fprintf(stderr, "Error: Invalid link ID '%s'\n", r.pos_args[value_offset]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    float h = 0.0f;
    if (!iface_parse_f32_arg(r.pos_args[value_offset + 1], &h)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float v = 0.0f;
    if (!iface_parse_f32_arg(r.pos_args[value_offset + 2], &v)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    iface_link_args_t args = {
        .op = IFACE_LINK_ADD_POINT,
        .link_id = link_id,
        .h = h,
        .v = v,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.add-point",
        .output_required_unless_dry_run = true,
    };
    return iface_run_resolved_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        &spec,
        &target_selector,
        &args.target_id,
        &args,
        iface_link_mutate,
        iface_link_report,
        usage);
}

int nmo_cmd_behavior_iface_clear_points(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos_arr);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    const char *usage =
        "Usage: nmo behavior interface clear-points [--id <id> | --name <name> | <id>] <link_id> <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 1, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }
    uint32_t link_id;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset], &link_id)) {
        fprintf(stderr, "Error: Invalid link ID '%s'\n", r.pos_args[value_offset]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    iface_link_args_t args = {
        .op = IFACE_LINK_CLEAR_POINTS,
        .link_id = link_id,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.clear-points",
        .output_required_unless_dry_run = true,
    };
    return iface_run_resolved_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        &spec,
        &target_selector,
        &args.target_id,
        &args,
        iface_link_mutate,
        iface_link_report,
        usage);
}

int nmo_cmd_behavior_iface_remove_point(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos_arr);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    const char *usage =
        "Usage: nmo behavior interface remove-point [--id <id> | --name <name> | <id>] <link_id> <point_index> <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 2, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }
    uint32_t link_id;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset], &link_id)) {
        fprintf(stderr, "Error: Invalid link ID '%s'\n", r.pos_args[value_offset]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t point_index;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset + 1], &point_index)) {
        fprintf(stderr, "Error: Invalid point index '%s'\n", r.pos_args[value_offset + 1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    iface_link_args_t args = {
        .op = IFACE_LINK_REMOVE_POINT,
        .link_id = link_id,
        .point_index = point_index,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.remove-point",
        .output_required_unless_dry_run = true,
    };
    return iface_run_resolved_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        &spec,
        &target_selector,
        &args.target_id,
        &args,
        iface_link_mutate,
        iface_link_report,
        usage);
}

int nmo_cmd_behavior_iface_move_point(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos_arr);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    const char *usage =
        "Usage: nmo behavior interface move-point [--id <id> | --name <name> | <id>] <link_id> <point_index> <h> <v> <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 4, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }
    uint32_t link_id;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset], &link_id)) {
        fprintf(stderr, "Error: Invalid link ID '%s'\n", r.pos_args[value_offset]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t point_index;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset + 1], &point_index)) {
        fprintf(stderr, "Error: Invalid point index '%s'\n", r.pos_args[value_offset + 1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    float h = 0.0f;
    if (!iface_parse_f32_arg(r.pos_args[value_offset + 2], &h)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float v = 0.0f;
    if (!iface_parse_f32_arg(r.pos_args[value_offset + 3], &v)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    iface_link_args_t args = {
        .op = IFACE_LINK_MOVE_POINT,
        .link_id = link_id,
        .point_index = point_index,
        .h = h,
        .v = v,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.move-point",
        .output_required_unless_dry_run = true,
    };
    return iface_run_resolved_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        &spec,
        &target_selector,
        &args.target_id,
        &args,
        iface_link_mutate,
        iface_link_report,
        usage);
}

int nmo_cmd_behavior_iface_set_link_highlight(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos_arr);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    const char *usage =
        "Usage: nmo behavior interface set-link-highlight [--id <id> | --name <name> | <id>] <link_id> on|off <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 2, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }
    uint32_t link_id;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset], &link_id)) {
        fprintf(stderr, "Error: Invalid link ID '%s'\n", r.pos_args[value_offset]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    bool highlight;
    if (strcmp(r.pos_args[value_offset + 1], "on") == 0) {
        highlight = true;
    } else if (strcmp(r.pos_args[value_offset + 1], "off") == 0) {
        highlight = false;
    } else {
        fprintf(stderr, "Error: Expected 'on' or 'off', got '%s'\n",
                r.pos_args[value_offset + 1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    iface_link_args_t args = {
        .op = IFACE_LINK_SET_HIGHLIGHT,
        .link_id = link_id,
        .highlight = highlight,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.set-link-highlight",
        .output_required_unless_dry_run = true,
    };
    return iface_run_resolved_write_command(
        file_path,
        output_path,
        dry_run,
        global,
        &spec,
        &target_selector,
        &args.target_id,
        &args,
        iface_link_mutate,
        iface_link_report,
        usage);
}
