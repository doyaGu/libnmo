/**
 * @file nmo_cmd_behavior_interface_layout.c
 * @brief nmo behavior interface layout edits: positions, folding, colors, sizes, viewport, graph IO and translation.
 */

#include "nmo_cmd_behavior_interface_internal.h"

static bool iface_parse_i32_arg(const char *text, int32_t *out_value)
{
    if (nmo_parse_i32_range(text, INT32_MIN, INT32_MAX, out_value) != NMO_OK) {
        fprintf(stderr, "Error: Invalid integer '%s'\n", text ? text : "");
        return false;
    }
    return true;
}

typedef struct iface_set_pos_args {
    uint32_t target_id;
    uint32_t beh_id;
    float h;
    float v;
} iface_set_pos_args_t;

typedef struct iface_fold_args {
    uint32_t target_id;
    uint32_t beh_id;
    bool fold;
} iface_fold_args_t;

typedef struct iface_set_color_args {
    uint32_t target_id;
    uint32_t color;
    bool color_persisted;
    const char *warning;
} iface_set_color_args_t;

typedef struct iface_canonicalize_args {
    uint32_t target_id;
    bool sectioned_layout;
    bool sectioned_root_is_graph;
    bool color_persisted;
    const char *root_kind;
} iface_canonicalize_args_t;

typedef struct iface_move_op_args {
    uint32_t target_id;
    uint32_t op_id;
    float h;
    float v;
} iface_move_op_args_t;

typedef enum iface_param_op {
    IFACE_PARAM_MOVE,
    IFACE_PARAM_SET_STYLE
} iface_param_op_t;

typedef struct iface_param_args {
    iface_param_op_t op;
    uint32_t target_id;
    bool has_body_id;
    uint32_t body_id;
    uint32_t param_index;
    bool shared;
    int32_t h;
    int32_t v;
    uint32_t style;
} iface_param_args_t;

typedef enum iface_sub_size_op {
    IFACE_SUB_RESIZE,
    IFACE_SUB_SET_EXPAND
} iface_sub_size_op_t;

typedef struct iface_sub_size_args {
    iface_sub_size_op_t op;
    uint32_t target_id;
    uint32_t beh_id;
    float w;
    float h;
} iface_sub_size_args_t;

typedef enum iface_layout_op {
    IFACE_LAYOUT_SET_VIEWPORT,
    IFACE_LAYOUT_TRANSLATE
} iface_layout_op_t;

typedef struct iface_layout_args {
    iface_layout_op_t op;
    uint32_t target_id;
    float a;
    float b;
    float c;
} iface_layout_args_t;

typedef struct iface_graph_io_args {
    uint32_t target_id;
    bool has_body_id;
    uint32_t body_id;
    bool has_inward_inputs;
    int32_t inward_inputs[64];
    size_t inward_input_count;
    bool has_inward_outputs;
    int32_t inward_outputs[64];
    size_t inward_output_count;
    bool has_outward_inputs;
    int32_t outward_inputs[64];
    size_t outward_input_count;
    bool has_outward_outputs;
    int32_t outward_outputs[64];
    size_t outward_output_count;
    int arrays_set;
    uint32_t resolved_body_id;
} iface_graph_io_args_t;

static void translate_body(nmo_interface_body_t *body, float dx, float dy);

static int iface_set_pos_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    iface_set_pos_args_t *args = (iface_set_pos_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_interface_data_t *idata = iface_edit_get_data(c, args->target_id, NULL);
    if (idata == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!iface_validate_behavior_id(c, args->beh_id)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (idata->script.behavior_id == args->beh_id) {
        idata->script.h_pos = args->h;
        idata->script.v_pos = args->v;
    } else {
        nmo_interface_behavior_t *sub = nmo_interface_find_sub(idata, args->beh_id);
        if (sub == NULL) {
            fprintf(stderr, "Error: Behavior %u not found in interface data\n", args->beh_id);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        sub->h_pos = args->h;
        sub->v_pos = args->v;
    }

    if (dry_run) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    return iface_mark_changed(c, args->target_id);
}

static int iface_set_pos_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    iface_set_pos_args_t *args = (iface_set_pos_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = iface_report_new(dry_run);
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_uint(rec, "target_id", NULL, args->target_id);
    ok = ok && nmo_cli_record_uint(rec, "behavior_id", NULL, args->beh_id);
    ok = ok && nmo_cli_record_real(rec, "h_pos", NULL, (double)args->h, NULL);
    ok = ok && nmo_cli_record_real(rec, "v_pos", NULL, (double)args->v, NULL);
    ok = ok && nmo_cli_record_raw_fmt(rec, "Moved behavior %u to (%.1f, %.1f)\n",
                                      args->beh_id, (double)args->h, (double)args->v);
    return iface_report_emit(c, rec, ok, dry_run, output_path,
                             "behavior.interface.set-pos");
}

static int iface_fold_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    iface_fold_args_t *args = (iface_fold_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_interface_data_t *idata = iface_edit_get_data(c, args->target_id, NULL);
    if (idata == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!iface_validate_behavior_id(c, args->beh_id)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t *flags = NULL;
    if (idata->script.behavior_id == args->beh_id) {
        flags = &idata->script.flags;
    } else {
        nmo_interface_behavior_t *sub = nmo_interface_find_sub(idata, args->beh_id);
        if (sub == NULL) {
            fprintf(stderr, "Error: Behavior %u not found in interface data\n", args->beh_id);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        flags = &sub->flags;
    }

    if (args->fold) {
        *flags |= NMO_INTERFACE_FLAG_FOLDED;
    } else {
        *flags &= ~NMO_INTERFACE_FLAG_FOLDED;
    }

    if (dry_run) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    return iface_mark_changed(c, args->target_id);
}

static int iface_fold_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    iface_fold_args_t *args = (iface_fold_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = iface_report_new(dry_run);
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_bool(rec, "folded", NULL, args->fold);
    ok = ok && nmo_cli_record_uint(rec, "target_id", NULL, args->target_id);
    ok = ok && nmo_cli_record_uint(rec, "behavior_id", NULL, args->beh_id);
    ok = ok && nmo_cli_record_raw_fmt(rec, "%s behavior %u\n",
                                      args->fold ? "Folded" : "Unfolded",
                                      args->beh_id);
    return iface_report_emit(c, rec, ok, dry_run, output_path,
                             args->fold ? "behavior.interface.fold"
                                        : "behavior.interface.unfold");
}

static int iface_set_color_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    iface_set_color_args_t *args = (iface_set_color_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_interface_data_t *idata = iface_edit_get_data(c, args->target_id, NULL);
    if (idata == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    idata->script.color = args->color;
    args->color_persisted = (idata->version >= 0x14 && !iface_is_sectioned(idata));
    if (args->color_persisted) {
        idata->format_flags |= NMO_INTERFACE_FORMAT_COLOR_PRESENT;
    } else {
        idata->format_flags &= ~NMO_INTERFACE_FORMAT_COLOR_PRESENT;
    }
    args->warning = NULL;

    if (!args->color_persisted) {
        args->warning = iface_is_sectioned(idata)
            ? "color will not be written for sectioned interface layout"
            : "color will not be written for interface versions below 0x14";
    }

    if (dry_run) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    return iface_mark_changed(c, args->target_id);
}

static int iface_set_color_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    iface_set_color_args_t *args = (iface_set_color_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = iface_report_new(dry_run);
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_uint(rec, "target_id", NULL, args->target_id);
    ok = ok && nmo_cli_record_uint(rec, "color", NULL, args->color);
    ok = ok && nmo_cli_record_bool(rec, "color_persisted", NULL,
                                   args->color_persisted);
    ok = ok && nmo_cli_record_raw_fmt(rec, "Set script color to #%06X\n",
                                      (unsigned)args->color);
    if (args->warning != NULL) {
        ok = ok && nmo_cli_record_str(rec, "warning", NULL, args->warning);
        ok = ok && nmo_cli_record_raw_fmt(rec, "Warning: %s\n", args->warning);
    }
    return iface_report_emit(c, rec, ok, dry_run, output_path,
                             "behavior.interface.set-color");
}

static int iface_canonicalize_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    iface_canonicalize_args_t *args = (iface_canonicalize_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_interface_data_t *idata = iface_edit_get_data(c, args->target_id, NULL);
    if (idata == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    args->sectioned_layout = iface_is_sectioned(idata);
    args->sectioned_root_is_graph =
        args->sectioned_layout &&
        (idata->format_flags & NMO_INTERFACE_FORMAT_ROOT_GRAPH) != 0u;
    args->color_persisted = iface_color_is_present(idata);
    args->root_kind = iface_root_kind_name(idata);

    if (dry_run) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    return iface_mark_changed(c, args->target_id);
}

static int iface_canonicalize_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    iface_canonicalize_args_t *args = (iface_canonicalize_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    const char *root_kind = args->root_kind ? args->root_kind : "script";
    nmo_cli_record_t *rec = iface_report_new(dry_run);
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_uint(rec, "target_id", NULL, args->target_id);
    ok = ok && nmo_cli_record_bool(rec, "canonicalized", NULL, true);
    ok = ok && nmo_cli_record_bool(rec, "sectioned_layout", NULL,
                                   args->sectioned_layout);
    ok = ok && nmo_cli_record_bool(rec, "sectioned_root_is_graph", NULL,
                                   args->sectioned_root_is_graph);
    ok = ok && nmo_cli_record_str(rec, "root_kind", NULL, root_kind);
    ok = ok && nmo_cli_record_bool(rec, "color_persisted", NULL,
                                   args->color_persisted);
    ok = ok && nmo_cli_record_raw_fmt(rec,
                                      "Canonicalized interface chunk for behavior %u\n"
                                      "Layout: %s  Root: %s\n",
                                      args->target_id,
                                      args->sectioned_layout ? "sectioned" : "inline",
                                      root_kind);
    return iface_report_emit(c, rec, ok, dry_run, output_path,
                             "behavior.interface.canonicalize");
}

static int iface_move_op_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    iface_move_op_args_t *args = (iface_move_op_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_interface_data_t *idata = iface_edit_get_data(c, args->target_id, NULL);
    if (idata == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_interface_operation_t *op = nmo_interface_find_operation(idata, args->op_id);
    if (op == NULL) {
        fprintf(stderr, "Error: Operation %u not found in interface data\n", args->op_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    op->h_pos = args->h;
    op->v_pos = args->v;

    if (dry_run) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    return iface_mark_changed(c, args->target_id);
}

static int iface_move_op_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    iface_move_op_args_t *args = (iface_move_op_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = iface_report_new(dry_run);
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_uint(rec, "target_id", NULL, args->target_id);
    ok = ok && nmo_cli_record_uint(rec, "operation_id", NULL, args->op_id);
    ok = ok && nmo_cli_record_real(rec, "h", NULL, (double)args->h, NULL);
    ok = ok && nmo_cli_record_real(rec, "v", NULL, (double)args->v, NULL);
    ok = ok && nmo_cli_record_raw_fmt(rec, "Moved operation %u to (%.1f, %.1f)\n",
                                      args->op_id, (double)args->h, (double)args->v);
    return iface_report_emit(c, rec, ok, dry_run, output_path,
                             "behavior.interface.move-op");
}

static const char *iface_param_command_name(iface_param_op_t op)
{
    switch (op) {
    case IFACE_PARAM_MOVE:      return "behavior.interface.move-param";
    case IFACE_PARAM_SET_STYLE: return "behavior.interface.set-param-style";
    default:                    return "behavior.interface.param";
    }
}

static int iface_param_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    iface_param_args_t *args = (iface_param_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_interface_data_t *idata = iface_edit_get_data(c, args->target_id, NULL);
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
    if (!body->has_params) {
        fprintf(stderr, "Error: Behavior %u has no parameter data\n", body_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_interface_param_t *params = args->shared
        ? body->params.shared
        : body->params.locals;
    size_t count = args->shared
        ? body->params.shared_count
        : body->params.local_count;
    if ((size_t)args->param_index >= count) {
        fprintf(stderr, "Error: Parameter index %u out of range (%s count=%zu)\n",
                args->param_index, args->shared ? "shared" : "local", count);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    switch (args->op) {
    case IFACE_PARAM_MOVE:
        params[args->param_index].h_pos = args->h;
        params[args->param_index].v_pos = args->v;
        break;
    case IFACE_PARAM_SET_STYLE:
        params[args->param_index].style = args->style;
        break;
    default:
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (dry_run) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    return iface_mark_changed(c, args->target_id);
}

static int iface_param_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    iface_param_args_t *args = (iface_param_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    const char *scope = args->shared ? "shared" : "local";
    nmo_cli_record_t *rec = iface_report_new(dry_run);
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_uint(rec, "target_id", NULL, args->target_id);
    ok = ok && nmo_cli_record_uint(rec, "body_id", NULL, args->body_id);
    ok = ok && nmo_cli_record_uint(rec, "param_index", NULL, args->param_index);
    ok = ok && nmo_cli_record_bool(rec, "shared", NULL, args->shared);
    switch (args->op) {
    case IFACE_PARAM_MOVE:
        ok = ok && nmo_cli_record_raw_fmt(rec, "Moved %s param %u to (%d, %d) in behavior %u\n",
                                          scope, args->param_index,
                                          args->h, args->v, args->body_id);
        break;
    case IFACE_PARAM_SET_STYLE:
        ok = ok && nmo_cli_record_raw_fmt(rec, "Set %s param %u style to 0x%X in behavior %u\n",
                                          scope, args->param_index,
                                          args->style, args->body_id);
        break;
    default:
        ok = false;
        break;
    }
    return iface_report_emit(c, rec, ok, dry_run, output_path,
                             iface_param_command_name(args->op));
}

static const char *iface_sub_size_command_name(iface_sub_size_op_t op)
{
    return op == IFACE_SUB_RESIZE
        ? "behavior.interface.resize"
        : "behavior.interface.set-expand";
}

static int iface_sub_size_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    iface_sub_size_args_t *args = (iface_sub_size_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_interface_data_t *idata = iface_edit_get_data(c, args->target_id, NULL);
    if (idata == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!iface_validate_behavior_id(c, args->beh_id)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (args->beh_id == idata->script.behavior_id) {
        fprintf(stderr, "Error: Cannot resize script behavior (no size fields)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_interface_behavior_t *sub = nmo_interface_find_sub(idata, args->beh_id);
    if (sub == NULL) {
        fprintf(stderr, "Error: Behavior %u not found in interface data\n", args->beh_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (args->op == IFACE_SUB_RESIZE) {
        sub->h_size = args->w;
        sub->v_size = args->h;
    } else {
        sub->h_expand_size = args->w;
        sub->v_expand_size = args->h;
    }

    if (dry_run) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    return iface_mark_changed(c, args->target_id);
}

static int iface_sub_size_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    iface_sub_size_args_t *args = (iface_sub_size_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = iface_report_new(dry_run);
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_uint(rec, "target_id", NULL, args->target_id);
    ok = ok && nmo_cli_record_uint(rec, "behavior_id", NULL, args->beh_id);
    ok = ok && nmo_cli_record_real(rec, "w", NULL, (double)args->w, NULL);
    ok = ok && nmo_cli_record_real(rec, "h", NULL, (double)args->h, NULL);
    ok = ok && nmo_cli_record_raw_fmt(rec,
                                      args->op == IFACE_SUB_RESIZE
                                          ? "Resized behavior %u to (%.1f, %.1f)\n"
                                          : "Set behavior %u expand size to (%.1f, %.1f)\n",
                                      args->beh_id, (double)args->w, (double)args->h);
    return iface_report_emit(c, rec, ok, dry_run, output_path,
                             iface_sub_size_command_name(args->op));
}

static const char *iface_layout_command_name(iface_layout_op_t op)
{
    return op == IFACE_LAYOUT_SET_VIEWPORT
        ? "behavior.interface.set-viewport"
        : "behavior.interface.translate";
}

static int iface_layout_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    iface_layout_args_t *args = (iface_layout_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_interface_data_t *idata = iface_edit_get_data(c, args->target_id, NULL);
    if (idata == NULL) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (args->op == IFACE_LAYOUT_SET_VIEWPORT) {
        idata->script.h_start_pos = args->a;
        idata->script.v_start_pos = args->b;
        idata->script.v_size = args->c;
    } else {
        float dx = args->a;
        float dy = args->b;
        idata->script.h_pos += dx;
        idata->script.v_pos += dy;
        translate_body(&idata->script.body, dx, dy);
        for (size_t si = 0; si < idata->sub_count; si++) {
            nmo_interface_behavior_t *sub = &idata->subs[si];
            sub->h_pos += dx;
            sub->v_pos += dy;
            translate_body(&sub->body, dx, dy);
        }
    }

    if (dry_run) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    return iface_mark_changed(c, args->target_id);
}

static int iface_layout_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    iface_layout_args_t *args = (iface_layout_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = iface_report_new(dry_run);
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_uint(rec, "target_id", NULL, args->target_id);
    if (args->op == IFACE_LAYOUT_SET_VIEWPORT) {
        ok = ok && nmo_cli_record_raw_fmt(rec, "Set viewport to (%.1f, %.1f) height=%.1f\n",
                                          (double)args->a, (double)args->b, (double)args->c);
    } else {
        ok = ok && nmo_cli_record_raw_fmt(rec, "Translated all positions by (%.1f, %.1f)\n",
                                          (double)args->a, (double)args->b);
    }
    return iface_report_emit(c, rec, ok, dry_run, output_path,
                             iface_layout_command_name(args->op));
}

static int iface_graph_io_mutate(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    (void)output_path;
    iface_graph_io_args_t *args = (iface_graph_io_args_t *)user_data;
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
    args->resolved_body_id = body_id;

    nmo_interface_body_t *body = nmo_interface_find_body(idata, body_id);
    if (body == NULL) {
        fprintf(stderr, "Error: Behavior %u has no body (not found or header-only)\n", body_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_arena_t *arena = nmo_object_get_storage_arena(beh_obj);
    if (!body->has_graph_io || body->graph_io == NULL) {
        nmo_interface_graph_io_t *gio = (nmo_interface_graph_io_t *)nmo_arena_alloc(
            arena, sizeof(*gio), _Alignof(nmo_interface_graph_io_t));
        if (gio == NULL) {
            fprintf(stderr, "Error: Failed to allocate graph IO data\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        memset(gio, 0, sizeof(*gio));
        body->graph_io = gio;
        body->has_graph_io = true;
    }

    nmo_interface_graph_io_t *gio = body->graph_io;
    args->arrays_set = 0;
    nmo_status_t st = NMO_OK;

    if (args->has_inward_inputs) {
        st = nmo_interface_graph_io_set_array(&gio->inward_inputs, &gio->inward_input_tags, &gio->inward_input_count,
                                              arena, args->inward_inputs, args->inward_input_count);
        if (st != NMO_OK) {
            fprintf(stderr, "Error: %s\n", nmo_error_string(st));
            return NMO_CLI_EXIT_IO_ERROR;
        }
        args->arrays_set++;
    }
    if (args->has_inward_outputs) {
        st = nmo_interface_graph_io_set_array(&gio->inward_outputs, &gio->inward_output_tags, &gio->inward_output_count,
                                              arena, args->inward_outputs, args->inward_output_count);
        if (st != NMO_OK) {
            fprintf(stderr, "Error: %s\n", nmo_error_string(st));
            return NMO_CLI_EXIT_IO_ERROR;
        }
        args->arrays_set++;
    }
    if (args->has_outward_inputs) {
        st = nmo_interface_graph_io_set_array(&gio->outward_inputs, &gio->outward_input_tags, &gio->outward_input_count,
                                              arena, args->outward_inputs, args->outward_input_count);
        if (st != NMO_OK) {
            fprintf(stderr, "Error: %s\n", nmo_error_string(st));
            return NMO_CLI_EXIT_IO_ERROR;
        }
        args->arrays_set++;
    }
    if (args->has_outward_outputs) {
        st = nmo_interface_graph_io_set_array(&gio->outward_outputs, &gio->outward_output_tags, &gio->outward_output_count,
                                              arena, args->outward_outputs, args->outward_output_count);
        if (st != NMO_OK) {
            fprintf(stderr, "Error: %s\n", nmo_error_string(st));
            return NMO_CLI_EXIT_IO_ERROR;
        }
        args->arrays_set++;
    }

    if (dry_run) {
        return NMO_CLI_EXIT_SUCCESS;
    }
    return iface_mark_changed(c, args->target_id);
}

static int iface_graph_io_report(
    nmo_cmd_ctx_t *c,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    iface_graph_io_args_t *args = (iface_graph_io_args_t *)user_data;
    if (args == NULL) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = iface_report_new(dry_run);
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_uint(rec, "target_id", NULL, args->target_id);
    ok = ok && nmo_cli_record_uint(rec, "behavior_id", NULL, args->resolved_body_id);
    ok = ok && nmo_cli_record_int(rec, "arrays_set", NULL, args->arrays_set);
    ok = ok && nmo_cli_record_raw_fmt(rec, "Set %d graph IO array(s) in behavior %u\n",
                                      args->arrays_set, args->resolved_body_id);
    return iface_report_emit(c, rec, ok, dry_run, output_path,
                             "behavior.interface.set-graph-io");
}
int nmo_cmd_behavior_iface_set_pos(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    const char *usage = "Usage: nmo behavior interface set-pos [--id <id> | --name <name> | <id>] <beh_id> <h> <v> <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 3, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }

    uint32_t beh_id;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset], &beh_id)) {
        fprintf(stderr, "Error: Invalid behavior ID '%s'\n", r.pos_args[value_offset]);
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

    iface_set_pos_args_t args = {
        .beh_id = beh_id,
        .h = h,
        .v = v,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.set-pos",
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
        iface_set_pos_mutate,
        iface_set_pos_report,
        usage);
}

static int iface_cmd_fold_impl(int argc, char **argv, const nmo_cli_global_opts_t *global, bool fold) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    const char *usage = fold
        ? "Usage: nmo behavior interface fold [--id <id> | --name <name> | <id>] <beh_id> <file> -o <out>"
        : "Usage: nmo behavior interface unfold [--id <id> | --name <name> | <id>] <beh_id> <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 1, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }

    uint32_t beh_id;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset], &beh_id)) {
        fprintf(stderr, "Error: Invalid behavior ID '%s'\n", r.pos_args[value_offset]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    iface_fold_args_t args = {
        .beh_id = beh_id,
        .fold = fold,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = fold ? "behavior.interface.fold" : "behavior.interface.unfold",
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
        iface_fold_mutate,
        iface_fold_report,
        usage);
}

int nmo_cmd_behavior_iface_fold(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    return iface_cmd_fold_impl(argc, argv, global, true);
}

int nmo_cmd_behavior_iface_unfold(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    return iface_cmd_fold_impl(argc, argv, global, false);
}

int nmo_cmd_behavior_iface_set_color(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    const char *usage = "Usage: nmo behavior interface set-color [--id <id> | --name <name> | <id>] <color> <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 1, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }

    const char *color_str = r.pos_args[value_offset];
    uint32_t color_val = 0;
    if (nmo_parse_hex_color(color_str, &color_val) != NMO_OK || color_val > 0xFFFFFFu) {
        fprintf(stderr, "Error: Invalid color '%s' (expected RRGGBB or 0xRRGGBB)\n", color_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    iface_set_color_args_t args = {
        .color = color_val,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.set-color",
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
        iface_set_color_mutate,
        iface_set_color_report,
        usage);
}

int nmo_cmd_behavior_iface_canonicalize(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[4];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    const char *usage = "Usage: nmo behavior interface canonicalize [--id <id> | --name <name> | <id>] <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 0, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }
    (void)value_offset;

    iface_canonicalize_args_t args = {
        .target_id = 0,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.canonicalize",
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
        iface_canonicalize_mutate,
        iface_canonicalize_report,
        usage);
}

/* ================================================================
 * Interface edit: operation
 * ================================================================ */

int nmo_cmd_behavior_iface_move_op(int argc, char **argv, const nmo_cli_global_opts_t *global) {
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
        "Usage: nmo behavior interface move-op [--id <id> | --name <name> | <id>] <op_id> <h> <v> <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 3, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }
    uint32_t op_id;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset], &op_id)) {
        fprintf(stderr, "Error: Invalid operation ID '%s'\n", r.pos_args[value_offset]);
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

    iface_move_op_args_t args = {
        .op_id = op_id,
        .h = h,
        .v = v,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.move-op",
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
        iface_move_op_mutate,
        iface_move_op_report,
        usage);
}

/* ================================================================
 * Interface edit: parameter operations
 * ================================================================ */

int nmo_cmd_behavior_iface_move_param(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        {"--body",        NULL,  NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--param-index", NULL,  NMO_OPT_UINT,   "Parameter index"},
        {"--shared",      NULL,  NMO_OPT_FLAG,   "Target shared params instead of local"},
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_PARAM_INDEX, OPT_SHARED, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos_arr);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    if (!vals[OPT_PARAM_INDEX].present) {
        fprintf(stderr, "Error: --param-index required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    const char *usage =
        "Usage: nmo behavior interface move-param [--id <id> | --name <name> | <id>] <h> <v> <file> --param-index <N> [--shared] -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 2, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }

    int32_t h = 0;
    if (!iface_parse_i32_arg(r.pos_args[value_offset], &h)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    int32_t v = 0;
    if (!iface_parse_i32_arg(r.pos_args[value_offset + 1], &v)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t param_index = vals[OPT_PARAM_INDEX].val.u;
    bool shared = nmo_opt_flag(&vals[OPT_SHARED]);

    iface_param_args_t args = {
        .op = IFACE_PARAM_MOVE,
        .param_index = param_index,
        .shared = shared,
        .h = h,
        .v = v,
    };
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &args.body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.has_body_id = true;
    }
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.move-param",
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
        iface_param_mutate,
        iface_param_report,
        usage);
}

int nmo_cmd_behavior_iface_set_param_style(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        {"--body",        NULL,  NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--param-index", NULL,  NMO_OPT_UINT,   "Parameter index"},
        {"--shared",      NULL,  NMO_OPT_FLAG,   "Target shared params instead of local"},
        {"--style",       "-s",  NMO_OPT_STRING, "Style value"},
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_PARAM_INDEX, OPT_SHARED, OPT_STYLE, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos_arr);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    if (!vals[OPT_PARAM_INDEX].present) {
        fprintf(stderr, "Error: --param-index required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_STYLE].present) {
        fprintf(stderr, "Error: --style required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    const char *usage =
        "Usage: nmo behavior interface set-param-style [--id <id> | --name <name> | <id>] <file> --param-index <N> --style <val> [--shared] -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 0, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }
    (void)value_offset;

    uint32_t style;
    if (!nmo_tool_parse_u32(vals[OPT_STYLE].val.str, &style)) {
        fprintf(stderr, "Error: Invalid --style value '%s'\n", vals[OPT_STYLE].val.str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t param_index = vals[OPT_PARAM_INDEX].val.u;
    bool shared = nmo_opt_flag(&vals[OPT_SHARED]);

    iface_param_args_t args = {
        .op = IFACE_PARAM_SET_STYLE,
        .param_index = param_index,
        .shared = shared,
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
        .command_name = "behavior.interface.set-param-style",
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
        iface_param_mutate,
        iface_param_report,
        usage);
}

/* ================================================================
 * Interface edit: sub-behavior size
 * ================================================================ */

int nmo_cmd_behavior_iface_resize(int argc, char **argv, const nmo_cli_global_opts_t *global) {
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
        "Usage: nmo behavior interface resize [--id <id> | --name <name> | <id>] <beh_id> <w> <h> <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 3, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }
    uint32_t beh_id;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset], &beh_id)) {
        fprintf(stderr, "Error: Invalid behavior ID '%s'\n", r.pos_args[value_offset]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    float w = 0.0f;
    if (!iface_parse_f32_arg(r.pos_args[value_offset + 1], &w)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float h = 0.0f;
    if (!iface_parse_f32_arg(r.pos_args[value_offset + 2], &h)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    iface_sub_size_args_t args = {
        .op = IFACE_SUB_RESIZE,
        .beh_id = beh_id,
        .w = w,
        .h = h,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.resize",
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
        iface_sub_size_mutate,
        iface_sub_size_report,
        usage);
}

int nmo_cmd_behavior_iface_set_expand(int argc, char **argv, const nmo_cli_global_opts_t *global) {
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
        "Usage: nmo behavior interface set-expand [--id <id> | --name <name> | <id>] <beh_id> <w> <h> <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 3, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }
    uint32_t beh_id;
    if (!nmo_tool_parse_u32(r.pos_args[value_offset], &beh_id)) {
        fprintf(stderr, "Error: Invalid behavior ID '%s'\n", r.pos_args[value_offset]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    float w = 0.0f;
    if (!iface_parse_f32_arg(r.pos_args[value_offset + 1], &w)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float h = 0.0f;
    if (!iface_parse_f32_arg(r.pos_args[value_offset + 2], &h)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    iface_sub_size_args_t args = {
        .op = IFACE_SUB_SET_EXPAND,
        .beh_id = beh_id,
        .w = w,
        .h = h,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.set-expand",
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
        iface_sub_size_mutate,
        iface_sub_size_report,
        usage);
}

/* ================================================================
 * Interface edit: script viewport
 * ================================================================ */

int nmo_cmd_behavior_iface_set_viewport(int argc, char **argv, const nmo_cli_global_opts_t *global) {
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
        "Usage: nmo behavior interface set-viewport [--id <id> | --name <name> | <id>] <h> <v> <height> <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 3, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }

    float h = 0.0f;
    if (!iface_parse_f32_arg(r.pos_args[value_offset], &h)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float v = 0.0f;
    if (!iface_parse_f32_arg(r.pos_args[value_offset + 1], &v)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float height = 0.0f;
    if (!iface_parse_f32_arg(r.pos_args[value_offset + 2], &height)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    iface_layout_args_t args = {
        .op = IFACE_LAYOUT_SET_VIEWPORT,
        .a = h,
        .b = v,
        .c = height,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.set-viewport",
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
        iface_layout_mutate,
        iface_layout_report,
        usage);
}

/* ================================================================
 * Interface edit: graph IO
 * ================================================================ */

static bool parse_int32_list(const char *str, int32_t *out, size_t max_count, size_t *out_count) {
    if (str == NULL || out == NULL || out_count == NULL) {
        return false;
    }

    size_t count = 0;
    const char *start = str;
    while (*start != '\0') {
        if (count >= max_count) {
            return false;
        }

        const char *end = start;
        while (*end != '\0' && *end != ',') {
            end++;
        }

        size_t len = (size_t)(end - start);
        if (len == 0) {
            return false;
        }

        char *token = (char *)malloc(len + 1u);
        if (token == NULL) {
            return false;
        }
        memcpy(token, start, len);
        token[len] = '\0';
        nmo_status_t parse_rc = nmo_parse_i32_range(token, INT32_MIN, INT32_MAX, &out[count]);
        free(token);
        if (parse_rc != NMO_OK) {
            return false;
        }
        count++;

        if (*end == '\0') {
            break;
        }
        start = end + 1;
    }

    *out_count = count;
    return true;
}

int nmo_cmd_behavior_iface_set_graph_io(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        {"--body",    NULL,  NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--in-in",   NULL,  NMO_OPT_STRING, "Inward input array (comma-separated ints)"},
        {"--in-out",  NULL,  NMO_OPT_STRING, "Inward output array (comma-separated ints)"},
        {"--out-in",  NULL,  NMO_OPT_STRING, "Outward input array (comma-separated ints)"},
        {"--out-out", NULL,  NMO_OPT_STRING, "Outward output array (comma-separated ints)"},
        NMO_OPT_DEF_DRY_RUN,
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_IN_IN, OPT_IN_OUT, OPT_OUT_IN, OPT_OUT_OUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos_arr);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    bool dry_run = nmo_opt_flag(&vals[OPT_DRYRUN]);
    if (!vals[OPT_IN_IN].present && !vals[OPT_IN_OUT].present &&
        !vals[OPT_OUT_IN].present && !vals[OPT_OUT_OUT].present) {
        fprintf(stderr, "Error: At least one of --in-in, --in-out, --out-in, --out-out required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    const char *usage =
        "Usage: nmo behavior interface set-graph-io [--id <id> | --name <name> | <id>] <file> [--body <beh_id>] [--in-in ...] [--in-out ...] [--out-in ...] [--out-out ...] -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 0, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }
    (void)value_offset;

    iface_graph_io_args_t args = {
        .target_id = 0,
    };
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &args.body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.has_body_id = true;
    }

    if (vals[OPT_IN_IN].present) {
        args.has_inward_inputs = true;
        if (!parse_int32_list(vals[OPT_IN_IN].val.str, args.inward_inputs, 64,
                              &args.inward_input_count)) {
            fprintf(stderr, "Error: Invalid --in-in list '%s'\n", vals[OPT_IN_IN].val.str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }
    if (vals[OPT_IN_OUT].present) {
        args.has_inward_outputs = true;
        if (!parse_int32_list(vals[OPT_IN_OUT].val.str, args.inward_outputs, 64,
                              &args.inward_output_count)) {
            fprintf(stderr, "Error: Invalid --in-out list '%s'\n", vals[OPT_IN_OUT].val.str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }
    if (vals[OPT_OUT_IN].present) {
        args.has_outward_inputs = true;
        if (!parse_int32_list(vals[OPT_OUT_IN].val.str, args.outward_inputs, 64,
                              &args.outward_input_count)) {
            fprintf(stderr, "Error: Invalid --out-in list '%s'\n", vals[OPT_OUT_IN].val.str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }
    if (vals[OPT_OUT_OUT].present) {
        args.has_outward_outputs = true;
        if (!parse_int32_list(vals[OPT_OUT_OUT].val.str, args.outward_outputs, 64,
                              &args.outward_output_count)) {
            fprintf(stderr, "Error: Invalid --out-out list '%s'\n", vals[OPT_OUT_OUT].val.str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.set-graph-io",
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
        iface_graph_io_mutate,
        iface_graph_io_report,
        usage);
}

/* ================================================================
 * Interface edit: bulk translate
 * ================================================================ */

static void translate_body(nmo_interface_body_t *body, float dx, float dy) {
    if (!body->has_body) return;

    for (size_t oi = 0; oi < body->operation_count; oi++) {
        body->operations[oi].h_pos += dx;
        body->operations[oi].v_pos += dy;
    }
    for (size_t ci = 0; ci < body->comment_count; ci++) {
        body->comments[ci].left += dx;
        body->comments[ci].top += dy;
        body->comments[ci].right += dx;
        body->comments[ci].bottom += dy;
    }
    for (size_t li = 0; li < body->link_count; li++) {
        nmo_interface_link_t *lk = &body->links[li];
        for (size_t pi = 0; pi < lk->point_count; pi++) {
            lk->points[pi * 2] += dx;
            lk->points[pi * 2 + 1] += dy;
        }
    }
}

int nmo_cmd_behavior_iface_translate(int argc, char **argv, const nmo_cli_global_opts_t *global) {
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
        "Usage: nmo behavior interface translate [--id <id> | --name <name> | <id>] <dx> <dy> <file> -o <out>";

    nmo_core_object_selector_t target_selector = {0};
    int value_offset = 0;
    const char *file_path = NULL;
    int target_rc = iface_prepare_target_selector(
        &option_selector, &r, 2, usage, &target_selector, &value_offset, &file_path);
    if (target_rc != NMO_CLI_EXIT_SUCCESS) {
        return target_rc;
    }

    float dx = 0.0f;
    if (!iface_parse_f32_arg(r.pos_args[value_offset], &dx)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float dy = 0.0f;
    if (!iface_parse_f32_arg(r.pos_args[value_offset + 1], &dy)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    iface_layout_args_t args = {
        .op = IFACE_LAYOUT_TRANSLATE,
        .a = dx,
        .b = dy,
    };
    const nmo_cli_write_spec_t spec = {
        .command_name = "behavior.interface.translate",
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
        iface_layout_mutate,
        iface_layout_report,
        usage);
}
