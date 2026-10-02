/**
 * @file nmo_cmd_behavior_interface_comment.c
 * @brief nmo behavior interface comment edits: add, remove, set text, move and style comments.
 */

#include "nmo_cmd_behavior_interface_internal.h"

static bool iface_parse_rect_arg(
    const char *text,
    float *left,
    float *top,
    float *right,
    float *bottom)
{
    float values[4] = {0};
    if (nmo_parse_f32_tuple(text, values, 4) != NMO_OK) {
        fprintf(stderr, "Error: Invalid --rect format '%s', expected L,T,R,B\n",
                text ? text : "");
        return false;
    }
    *left = values[0];
    *top = values[1];
    *right = values[2];
    *bottom = values[3];
    return true;
}

typedef enum iface_comment_op {
    IFACE_COMMENT_ADD,
    IFACE_COMMENT_REMOVE,
    IFACE_COMMENT_SET_TEXT,
    IFACE_COMMENT_MOVE,
    IFACE_COMMENT_SET_STYLE
} iface_comment_op_t;

typedef struct iface_comment_args {
    iface_comment_op_t op;
    uint32_t target_id;
    bool has_body_id;
    uint32_t body_id;
    uint32_t index;
    const char *text;
    float left;
    float top;
    float right;
    float bottom;
    uint32_t style;
    size_t result_index;
} iface_comment_args_t;

static const char *iface_comment_command_name(iface_comment_op_t op)
{
    switch (op) {
    case IFACE_COMMENT_ADD:       return "behavior.interface.add-comment";
    case IFACE_COMMENT_REMOVE:    return "behavior.interface.remove-comment";
    case IFACE_COMMENT_SET_TEXT:  return "behavior.interface.set-comment-text";
    case IFACE_COMMENT_MOVE:      return "behavior.interface.move-comment";
    case IFACE_COMMENT_SET_STYLE: return "behavior.interface.set-comment-style";
    default:                      return "behavior.interface.comment";
    }
}

static int iface_comment_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    iface_comment_args_t *args = (iface_comment_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_t *beh_obj = NULL;
    nmo_interface_data_t *idata = iface_edit_get_data(c, args->target_id, &beh_obj);
    if (idata == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t body_id = idata->script.behavior_id;
    if (args->has_body_id) {
        body_id = args->body_id;
        if (!iface_validate_behavior_id(c, body_id)) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }
    args->body_id = body_id;

    nmo_interface_body_t *body = nmo_interface_find_body(idata, body_id);
    if (body == NULL) {
        fprintf(stderr, "Error: Behavior %u has no body (not found or header-only)\n", body_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (args->op != IFACE_COMMENT_ADD && (size_t)args->index >= body->comment_count) {
        fprintf(stderr, "Error: Comment index %u out of range (count=%zu)\n",
                args->index, body->comment_count);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_status_t st = NMO_OK;
    nmo_arena_t *arena = nmo_object_get_storage_arena(beh_obj);
    switch (args->op) {
    case IFACE_COMMENT_ADD:
        st = nmo_interface_body_add_comment(
            body,
            arena,
            args->text,
            args->left,
            args->top,
            args->right,
            args->bottom,
            0,
            &args->result_index);
        break;
    case IFACE_COMMENT_REMOVE:
        st = nmo_interface_body_remove_comment(body, (size_t)args->index);
        break;
    case IFACE_COMMENT_SET_TEXT:
        st = nmo_interface_body_set_comment_text(
            body, arena, (size_t)args->index, args->text);
        break;
    case IFACE_COMMENT_MOVE:
        body->comments[args->index].left = args->left;
        body->comments[args->index].top = args->top;
        body->comments[args->index].right = args->right;
        body->comments[args->index].bottom = args->bottom;
        break;
    case IFACE_COMMENT_SET_STYLE:
        if (idata->version < 0x16) {
            fprintf(stderr, "Warning: comment style_flags not written for version 0x%02X (requires >= 0x16)\n",
                    idata->version);
        }
        body->comments[args->index].style_flags = args->style;
        break;
    default:
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (st != NMO_OK) {
        fprintf(stderr, "Error: %s\n", nmo_error_string(st));
        return NMO_CLI_EXIT_IO_ERROR;
    }

    if (dry_run) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    return iface_mark_changed(c, args->target_id);
}

static int iface_comment_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    iface_comment_args_t *args = (iface_comment_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = iface_report_new(dry_run);
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_uint(rec, "target_id", NULL, args->target_id);
    ok = ok && nmo_cli_record_uint(rec, "body_id", NULL, args->body_id);
    ok = ok && nmo_cli_record_uint(rec, "index", NULL,
                                   args->op == IFACE_COMMENT_ADD
                                       ? (uint64_t)args->result_index
                                       : (uint64_t)args->index);
    switch (args->op) {
    case IFACE_COMMENT_ADD:
        ok = ok && nmo_cli_record_raw_fmt(rec, "Added comment at index %zu to behavior %u\n",
                                          args->result_index, args->body_id);
        break;
    case IFACE_COMMENT_REMOVE:
        ok = ok && nmo_cli_record_raw_fmt(rec, "Removed comment %u from behavior %u\n",
                                          args->index, args->body_id);
        break;
    case IFACE_COMMENT_SET_TEXT:
        ok = ok && nmo_cli_record_raw_fmt(rec, "Set comment %u text in behavior %u\n",
                                          args->index, args->body_id);
        break;
    case IFACE_COMMENT_MOVE:
        ok = ok && nmo_cli_record_raw_fmt(rec,
                                          "Moved comment %u to (%.0f,%.0f,%.0f,%.0f) in behavior %u\n",
                                          args->index,
                                          (double)args->left,
                                          (double)args->top,
                                          (double)args->right,
                                          (double)args->bottom,
                                          args->body_id);
        break;
    case IFACE_COMMENT_SET_STYLE:
        ok = ok && nmo_cli_record_raw_fmt(rec, "Set comment %u style to 0x%X in behavior %u\n",
                                          args->index, args->style, args->body_id);
        break;
    default:
        ok = false;
        break;
    }
    return iface_report_emit(c, rec, ok, dry_run, output_path,
                             iface_comment_command_name(args->op));
}

int nmo_cmd_behavior_iface_add_comment(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        {"--body",   NULL,    NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--text",   "-t",    NMO_OPT_STRING, "Comment text"},
        {"--rect",   "-r",    NMO_OPT_STRING, "Rectangle L,T,R,B"},
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_TEXT, OPT_RECT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos_arr);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    if (!vals[OPT_TEXT].present) {
        fprintf(stderr, "Error: --text required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_RECT].present) {
        fprintf(stderr, "Error: --rect required (L,T,R,B)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    const char *usage =
        "Usage: nmo behavior interface add-comment [--id <id> | --name <name> | <id>] [--body <beh_id>] --text \"...\" --rect L,T,R,B <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 0, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }
    (void)value_offset;

    iface_comment_args_t args = {
        .op = IFACE_COMMENT_ADD,
        .text = vals[OPT_TEXT].val.str,
    };
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &args.body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.has_body_id = true;
    }

    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    if (!iface_parse_rect_arg(vals[OPT_RECT].val.str, &left, &top, &right, &bottom)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    args.left = left;
    args.top = top;
    args.right = right;
    args.bottom = bottom;

    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.add-comment",
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
        iface_comment_mutate,
        iface_comment_report,
        usage);
}

int nmo_cmd_behavior_iface_remove_comment(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        {"--body",   NULL,    NMO_OPT_STRING, "Target behavior ID (default: script)"},
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos_arr);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    const char *usage =
        "Usage: nmo behavior interface remove-comment [--id <id> | --name <name> | <id>] <index> [--body <beh_id>] <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 1, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }

    uint32_t index_val;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset], &index_val)) {
        fprintf(stderr, "Error: Invalid index '%s'\n", r.pos_args[value_offset]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    iface_comment_args_t args = {
        .op = IFACE_COMMENT_REMOVE,
        .index = index_val,
    };
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &args.body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.has_body_id = true;
    }
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.remove-comment",
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
        iface_comment_mutate,
        iface_comment_report,
        usage);
}

/* ================================================================
 * Interface edit: comment operations
 * ================================================================ */

int nmo_cmd_behavior_iface_set_comment_text(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        {"--body",   NULL,  NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--text",   "-t",  NMO_OPT_STRING, "New comment text"},
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_TEXT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos_arr);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    if (!vals[OPT_TEXT].present) {
        fprintf(stderr, "Error: --text required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    const char *usage =
        "Usage: nmo behavior interface set-comment-text [--id <id> | --name <name> | <id>] <index> [--body <beh_id>] --text \"...\" <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 1, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }
    uint32_t index_val;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset], &index_val)) {
        fprintf(stderr, "Error: Invalid index '%s'\n", r.pos_args[value_offset]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    iface_comment_args_t args = {
        .op = IFACE_COMMENT_SET_TEXT,
        .index = index_val,
        .text = vals[OPT_TEXT].val.str,
    };
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &args.body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.has_body_id = true;
    }
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.set-comment-text",
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
        iface_comment_mutate,
        iface_comment_report,
        usage);
}

int nmo_cmd_behavior_iface_move_comment(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        {"--body",   NULL,  NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--rect",   "-r",  NMO_OPT_STRING, "Rectangle L,T,R,B"},
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_RECT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos_arr);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    if (!vals[OPT_RECT].present) {
        fprintf(stderr, "Error: --rect required (L,T,R,B)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    const char *usage =
        "Usage: nmo behavior interface move-comment [--id <id> | --name <name> | <id>] <index> [--body <beh_id>] --rect L,T,R,B <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 1, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }
    uint32_t index_val;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset], &index_val)) {
        fprintf(stderr, "Error: Invalid index '%s'\n", r.pos_args[value_offset]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    if (!iface_parse_rect_arg(vals[OPT_RECT].val.str, &left, &top, &right, &bottom)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    iface_comment_args_t args = {
        .op = IFACE_COMMENT_MOVE,
        .index = index_val,
        .left = left,
        .top = top,
        .right = right,
        .bottom = bottom,
    };
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &args.body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.has_body_id = true;
    }
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.move-comment",
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
        iface_comment_mutate,
        iface_comment_report,
        usage);
}

int nmo_cmd_behavior_iface_set_comment_style(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        {"--body",   NULL,    NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--style",  "-s",    NMO_OPT_STRING, "Style flags value"},
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_STYLE, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos_arr);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    if (!vals[OPT_STYLE].present) {
        fprintf(stderr, "Error: --style required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    const char *usage =
        "Usage: nmo behavior interface set-comment-style [--id <id> | --name <name> | <id>] <index> [--body <beh_id>] --style <flags> <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 1, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }
    uint32_t index_val;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset], &index_val)) {
        fprintf(stderr, "Error: Invalid index '%s'\n", r.pos_args[value_offset]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t style;
    if (!nmo_tool_parse_u32(vals[OPT_STYLE].val.str, &style)) {
        fprintf(stderr, "Error: Invalid --style value '%s'\n", vals[OPT_STYLE].val.str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    iface_comment_args_t args = {
        .op = IFACE_COMMENT_SET_STYLE,
        .index = index_val,
        .style = style,
    };
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &args.body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.has_body_id = true;
    }
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.set-comment-style",
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
        iface_comment_mutate,
        iface_comment_report,
        usage);
}
