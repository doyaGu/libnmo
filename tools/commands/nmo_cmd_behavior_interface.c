/**
 * @file nmo_cmd_behavior_interface.c
 * @brief CLI behavior interface sub-action implementations
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
#include "edit/nmo_behavior_edit.h"
#include "runtime/nmo_context.h"
#include "core/nmo_array.h"
#include "core/nmo_parse.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_interface_edit.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_system.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Interface read-only helpers (text/JSON output)
 * ================================================================ */

static const char *iface_endpoint_type_name(uint32_t type) {
    switch (type) {
    case NMO_INTERFACE_ENDPOINT_POUT_SHORTCUT: return "pout_shortcut";
    case NMO_INTERFACE_ENDPOINT_PIN:           return "pin";
    case NMO_INTERFACE_ENDPOINT_POUT:          return "pout";
    case NMO_INTERFACE_ENDPOINT_PLOCAL:        return "plocal";
    case NMO_INTERFACE_ENDPOINT_TARGET_PIN:    return "target_pin";
    case NMO_INTERFACE_ENDPOINT_BIN:           return "bin";
    case NMO_INTERFACE_ENDPOINT_BOUT:          return "bout";
    case NMO_INTERFACE_ENDPOINT_START_BIN:     return "start_bin";
    default:                                    return "?";
    }
}

static const char *iface_root_kind_name(const nmo_interface_data_t *idata) {
    return (idata &&
            (idata->format_flags & NMO_INTERFACE_FORMAT_SECTIONED) &&
            (idata->format_flags & NMO_INTERFACE_FORMAT_ROOT_GRAPH))
        ? "graph"
        : "script";
}

static bool iface_is_sectioned(const nmo_interface_data_t *idata) {
    return idata && (idata->format_flags & NMO_INTERFACE_FORMAT_SECTIONED);
}

static bool iface_color_is_present(const nmo_interface_data_t *idata) {
    return idata && (idata->format_flags & NMO_INTERFACE_FORMAT_COLOR_PRESENT);
}

static const char *iface_param_style_name(uint32_t style) {
    if (style & NMO_INTERFACE_PARAM_STYLE_COLLAPSED) return " [collapsed]";
    if (style & NMO_INTERFACE_PARAM_STYLE_NAMEVALUE) return " [name+value]";
    if (style & NMO_INTERFACE_PARAM_STYLE_VALUE)     return " [value]";
    if (style & NMO_INTERFACE_PARAM_STYLE_NAME)      return " [name]";
    return "";
}

/* Array of records rendered in text by each item's own raw lines. */
static nmo_cli_record_array_t *iface_inline_array(nmo_cli_record_t *rec,
                                                  const char *key) {
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, key, NULL);
    if (arr != NULL) {
        nmo_cli_record_array_omit_heading(arr);
        nmo_cli_record_array_inline_items(arr);
    }
    return arr;
}

static nmo_cli_record_t *iface_array_item(nmo_cli_record_array_t *arr) {
    nmo_cli_record_t *item = nmo_cli_record_new();
    return nmo_cli_record_array_add(arr, item) ? item : NULL;
}

static bool iface_add_endpoint(nmo_cli_record_t *rec, const char *key,
                               const nmo_interface_endpoint_t *ep) {
    nmo_cli_record_t *eo = nmo_cli_record_object(rec, key);
    bool ok = eo != NULL;
    ok = ok && nmo_cli_record_uint(eo, "id", NULL, ep->id);
    ok = ok && nmo_cli_record_int(eo, "index", NULL, ep->index);
    ok = ok && nmo_cli_record_uint(eo, "type", NULL, ep->type);
    ok = ok && nmo_cli_record_str(eo, "type_name", NULL,
                                  iface_endpoint_type_name(ep->type));
    return ok;
}

static bool iface_add_link(nmo_cli_record_array_t *arr, const nmo_interface_link_t *lk) {
    nmo_cli_record_t *lo = iface_array_item(arr);
    bool ok = lo != NULL;
    ok = ok && nmo_cli_record_uint(lo, "type", NULL, lk->type);
    ok = ok && nmo_cli_record_bool(lo, "highlight", NULL, lk->highlight);
    ok = ok && nmo_cli_record_uint(lo, "link_id", NULL, lk->link_id);
    ok = ok && iface_add_endpoint(lo, "start", &lk->start);
    ok = ok && iface_add_endpoint(lo, "end", &lk->end);
    ok = ok && nmo_cli_record_uint(lo, "point_count", NULL, lk->point_count);
    ok = ok && nmo_cli_record_raw_fmt(lo, "  #%u %s%s  %u:%d:%s -> %u:%d:%s",
                                      lk->link_id,
                                      lk->type == 1 ? "behavior" : lk->type == 2 ? "param" : "?",
                                      lk->highlight ? " hl" : "",
                                      lk->start.id, lk->start.index,
                                      iface_endpoint_type_name(lk->start.type),
                                      lk->end.id, lk->end.index,
                                      iface_endpoint_type_name(lk->end.type));
    if (ok && lk->point_count > 0) {
        ok = nmo_cli_record_raw_fmt(lo, "  pts=%zu [", lk->point_count);
        nmo_cli_record_array_t *pts = nmo_cli_record_array(lo, "points", NULL);
        ok = ok && pts != NULL;
        for (size_t pi = 0; ok && pi < lk->point_count; pi++) {
            double h = (double)lk->points[pi * 2];
            double v = (double)lk->points[pi * 2 + 1];
            nmo_cli_record_t *pt = iface_array_item(pts);
            ok = pt != NULL &&
                 nmo_cli_record_real(pt, "h", NULL, h, NULL) &&
                 nmo_cli_record_real(pt, "v", NULL, v, NULL);
            if (ok && pi < 4) {
                ok = nmo_cli_record_raw_fmt(lo, "%s(%.0f,%.0f)", pi > 0 ? ", " : "", h, v);
            }
        }
        ok = ok && nmo_cli_record_raw(lo, lk->point_count > 4 ? ", ...]" : "]");
    }
    return ok && nmo_cli_record_raw(lo, "\n");
}

static bool iface_add_param_slots(nmo_cli_record_t *rec, const char *key,
                                  const nmo_interface_param_t *params,
                                  size_t count, bool shared) {
    nmo_cli_record_array_t *arr = iface_inline_array(rec, key);
    bool ok = arr != NULL;
    for (size_t pi = 0; ok && pi < count; pi++) {
        const nmo_interface_param_t *pm = &params[pi];
        nmo_cli_record_t *item = iface_array_item(arr);
        ok = item != NULL;
        ok = ok && nmo_cli_record_int(item, "h_pos", NULL, pm->h_pos);
        ok = ok && nmo_cli_record_int(item, "v_pos", NULL, pm->v_pos);
        ok = ok && nmo_cli_record_uint(item, "style", NULL, pm->style);
        ok = ok && nmo_cli_record_raw_fmt(item, " (%d,%d)%s", pm->h_pos, pm->v_pos,
                                          iface_param_style_name(pm->style));
        if (ok && shared) {
            ok = nmo_cli_record_uint(item, "source_id", NULL, pm->source_id);
            if (ok && pm->source_id) {
                ok = nmo_cli_record_raw_fmt(item, "->%u", pm->source_id);
            }
        }
    }
    return ok;
}

static bool iface_add_port_list(nmo_cli_record_t *rec, const char *key,
                                const int32_t *values, size_t count) {
    int64_t *wide = count > 0 ? (int64_t *)malloc(count * sizeof(*wide)) : NULL;
    bool ok = count == 0 || wide != NULL;
    for (size_t i = 0; ok && i < count; i++) {
        wide[i] = values[i];
    }
    ok = ok && nmo_cli_record_int_list(rec, key, NULL, wide, count, NULL);
    free(wide);
    ok = ok && nmo_cli_record_raw_fmt(rec, "  %s (%zu):", key, count);
    for (size_t i = 0; ok && i < count; i++) {
        ok = nmo_cli_record_raw_fmt(rec, " %d", values[i]);
    }
    return ok && nmo_cli_record_raw(rec, "\n");
}

/* Adds `body` under "body"; the text sections are headed with `label`. */
static bool iface_add_body(nmo_cli_record_t *rec, const nmo_interface_body_t *body,
                           const char *label) {
    nmo_cli_record_t *bo = nmo_cli_record_object(rec, "body");
    bool ok = bo != NULL;
    ok = ok && nmo_cli_record_bool(bo, "has_body", NULL, body->has_body);
    if (!body->has_body) {
        return ok && nmo_cli_record_raw(bo, "  (header only)\n");
    }

    if (ok && body->link_count > 0) {
        ok = nmo_cli_record_title_fmt(bo, "%s Links (%zu)", label, body->link_count);
    }
    nmo_cli_record_array_t *links = iface_inline_array(bo, "links");
    ok = ok && links != NULL;
    for (size_t li = 0; ok && li < body->link_count; li++) {
        ok = iface_add_link(links, &body->links[li]);
    }

    if (ok && body->operation_count > 0) {
        ok = nmo_cli_record_title_fmt(bo, "%s Operations (%zu)", label, body->operation_count);
    }
    nmo_cli_record_array_t *ops = iface_inline_array(bo, "operations");
    ok = ok && ops != NULL;
    for (size_t oi = 0; ok && oi < body->operation_count; oi++) {
        const nmo_interface_operation_t *op = &body->operations[oi];
        nmo_cli_record_t *oo = iface_array_item(ops);
        ok = oo != NULL;
        ok = ok && nmo_cli_record_uint(oo, "id", NULL, op->id);
        ok = ok && nmo_cli_record_real(oo, "h_pos", NULL, (double)op->h_pos, NULL);
        ok = ok && nmo_cli_record_real(oo, "v_pos", NULL, (double)op->v_pos, NULL);
        ok = ok && nmo_cli_record_raw_fmt(oo, "  id=%u pos=(%.1f, %.1f)\n",
                                          op->id, (double)op->h_pos, (double)op->v_pos);
    }

    if (ok && body->comment_count > 0) {
        ok = nmo_cli_record_title_fmt(bo, "%s Comments (%zu)", label, body->comment_count);
    }
    nmo_cli_record_array_t *comments = iface_inline_array(bo, "comments");
    ok = ok && comments != NULL;
    for (size_t ci = 0; ok && ci < body->comment_count; ci++) {
        const nmo_interface_comment_t *cm = &body->comments[ci];
        nmo_cli_record_t *co = iface_array_item(comments);
        ok = co != NULL;
        ok = ok && nmo_cli_record_real(co, "left", NULL, (double)cm->left, NULL);
        ok = ok && nmo_cli_record_real(co, "top", NULL, (double)cm->top, NULL);
        ok = ok && nmo_cli_record_real(co, "right", NULL, (double)cm->right, NULL);
        ok = ok && nmo_cli_record_real(co, "bottom", NULL, (double)cm->bottom, NULL);
        ok = ok && (cm->text ? nmo_cli_record_str(co, "text", NULL, cm->text)
                             : nmo_cli_record_null(co, "text", NULL, NULL));
        ok = ok && nmo_cli_record_uint(co, "style_flags", NULL, cm->style_flags);
        ok = ok && nmo_cli_record_raw_fmt(co, "  rect=(%.0f,%.0f,%.0f,%.0f)",
                                          (double)cm->left, (double)cm->top,
                                          (double)cm->right, (double)cm->bottom);
        if (ok && cm->style_flags) {
            ok = nmo_cli_record_raw_fmt(co, " flags=0x%X", cm->style_flags);
        }
        ok = ok && nmo_cli_record_raw_fmt(co, "\n    \"%s\"\n", cm->text ? cm->text : "");
    }

    if (ok && body->has_params) {
        ok = nmo_cli_record_title_fmt(bo, "%s Parameters", label);
    }
    nmo_cli_record_t *po = ok ? nmo_cli_record_object(bo, "params") : NULL;
    ok = ok && po != NULL;
    ok = ok && nmo_cli_record_bool(po, "has_params", NULL, body->has_params);
    if (ok && body->has_params) {
        const nmo_interface_param_set_t *ps = &body->params;
        ok = nmo_cli_record_uint(po, "local_count", NULL, ps->local_count) &&
             nmo_cli_record_uint(po, "shared_count", NULL, ps->shared_count) &&
             nmo_cli_record_raw_fmt(po, "  Local (%zu):", ps->local_count) &&
             iface_add_param_slots(po, "locals", ps->locals, ps->local_count, false) &&
             nmo_cli_record_raw_fmt(po, "\n  Shared (%zu):", ps->shared_count) &&
             iface_add_param_slots(po, "shared", ps->shared, ps->shared_count, true) &&
             nmo_cli_record_raw(po, "\n");
    }

    if (ok && body->has_graph_io && body->graph_io) {
        const nmo_interface_graph_io_t *gio = body->graph_io;
        nmo_cli_record_t *go = NULL;
        ok = nmo_cli_record_title_fmt(bo, "%s Graph IO", label) &&
             (go = nmo_cli_record_object(bo, "graph_io")) != NULL &&
             iface_add_port_list(go, "inward_inputs",
                                 gio->inward_inputs, gio->inward_input_count) &&
             iface_add_port_list(go, "outward_inputs",
                                 gio->outward_inputs, gio->outward_input_count) &&
             iface_add_port_list(go, "inward_outputs",
                                 gio->inward_outputs, gio->inward_output_count) &&
             iface_add_port_list(go, "outward_outputs",
                                 gio->outward_outputs, gio->outward_output_count);
    }

    /* Section presence flags */
    ok = ok && nmo_cli_record_bool(bo, "has_links_section", NULL, body->has_links_section);
    ok = ok && nmo_cli_record_bool(bo, "has_operations_section", NULL,
                                   body->has_operations_section);
    ok = ok && nmo_cli_record_bool(bo, "has_comments_section", NULL,
                                   body->has_comments_section);
    ok = ok && nmo_cli_record_bool(bo, "has_unknown_flag_section", NULL,
                                   body->has_unknown_flag_section);
    if (ok && body->has_unknown_flag_section) {
        ok = nmo_cli_record_int(bo, "unknown_flag", NULL, body->unknown_flag) &&
             nmo_cli_record_raw_fmt(bo, "  unknown_flag: %d\n", body->unknown_flag);
    }
    return ok;
}

/* ================================================================
 * Interface edit: shared helpers
 * ================================================================ */

static nmo_interface_data_t *iface_edit_get_data(
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
    if (out_obj) *out_obj = beh;
    return bs->interface_data;
}

static bool iface_validate_behavior_id(nmo_cmd_ctx_t *c, uint32_t beh_id) {
    nmo_object_repository_t *repo = nmo_tool_owner_repository(c->workspace);
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, beh_id);
    if (!obj) {
        fprintf(stderr, "Warning: Behavior %u not found in repository (may have been deleted)\n", beh_id);
        return false;
    }
    return true;
}

static bool iface_parse_f32_arg(const char *text, float *out_value)
{
    if (nmo_parse_f32(text, out_value) != NMO_OK) {
        fprintf(stderr, "Error: Invalid float '%s'\n", text ? text : "");
        return false;
    }
    return true;
}

static bool iface_parse_i32_arg(const char *text, int32_t *out_value)
{
    if (nmo_parse_i32_range(text, INT32_MIN, INT32_MAX, out_value) != NMO_OK) {
        fprintf(stderr, "Error: Invalid integer '%s'\n", text ? text : "");
        return false;
    }
    return true;
}

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

static int iface_mark_changed(nmo_cmd_ctx_t *c, nmo_object_id_t target_id)
{
    nmo_workspace_edit_t *edit = NULL;
    nmo_status_t st = nmo_workspace_edit_begin(c->workspace, "behavior interface edit", &edit);
    if (st == NMO_OK) {
        st = nmo_behavior_edit_mark_interface(edit, target_id);
    }
    if (st == NMO_OK) {
        st = nmo_workspace_edit_commit(edit);
    } else if (edit != NULL) {
        nmo_workspace_edit_rollback(edit);
    }
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
static nmo_cli_record_t *iface_report_new(bool dry_run)
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

static int iface_report_emit(
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
        st = nmo_interface_graph_io_set_array(&gio->inward_inputs, &gio->inward_input_count,
                                              arena, args->inward_inputs, args->inward_input_count);
        if (st != NMO_OK) {
            fprintf(stderr, "Error: %s\n", nmo_error_string(st));
            return NMO_CLI_EXIT_IO_ERROR;
        }
        args->arrays_set++;
    }
    if (args->has_inward_outputs) {
        st = nmo_interface_graph_io_set_array(&gio->inward_outputs, &gio->inward_output_count,
                                              arena, args->inward_outputs, args->inward_output_count);
        if (st != NMO_OK) {
            fprintf(stderr, "Error: %s\n", nmo_error_string(st));
            return NMO_CLI_EXIT_IO_ERROR;
        }
        args->arrays_set++;
    }
    if (args->has_outward_inputs) {
        st = nmo_interface_graph_io_set_array(&gio->outward_inputs, &gio->outward_input_count,
                                              arena, args->outward_inputs, args->outward_input_count);
        if (st != NMO_OK) {
            fprintf(stderr, "Error: %s\n", nmo_error_string(st));
            return NMO_CLI_EXIT_IO_ERROR;
        }
        args->arrays_set++;
    }
    if (args->has_outward_outputs) {
        st = nmo_interface_graph_io_set_array(&gio->outward_outputs, &gio->outward_output_count,
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

/* ================================================================
 * Interface edit: verb handlers (now public sub-action handlers)
 * ================================================================ */

enum { IFACE_SELECTOR_ARGV_CAP = 64 };

static bool iface_option_consumes_next_arg(const char *arg)
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

static int iface_strip_target_selector_args(
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

static int iface_prepare_target_selector(
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

typedef struct iface_resolving_write_args {
    const nmo_core_object_selector_t *selector;
    uint32_t *target_id;
    void *payload;
    nmo_cli_write_mutate_fn mutate;
    nmo_cli_write_report_fn report;
    const char *usage;
} iface_resolving_write_args_t;

static int iface_resolve_then_mutate(
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
    return args->mutate(c, dry_run, output_path, args->payload);
}

static int iface_resolved_report(
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

static int iface_run_resolved_write_command(
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

#define IFACE_STRIP_TARGET_SELECTOR_ARGS()                                      \
    char *parse_argv[IFACE_SELECTOR_ARGV_CAP];                                  \
    int parse_argc = 0;                                                         \
    nmo_core_object_selector_t option_selector = {0};                           \
    int selector_rc = iface_strip_target_selector_args(                         \
        argc, argv, &parse_argc, parse_argv, IFACE_SELECTOR_ARGV_CAP,           \
        &option_selector);                                                      \
    if (selector_rc != NMO_CLI_EXIT_SUCCESS) return selector_rc;                \
    argc = parse_argc;                                                          \
    argv = parse_argv

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

/* ================================================================
 * Interface show (read-only, the default sub-action)
 * ================================================================ */

typedef struct behavior_iface_show_args {
    bool brief;
    nmo_core_object_selector_t selector;
} behavior_iface_show_args_t;

static const char *iface_flag_marks(uint32_t flags) {
    if ((flags & NMO_INTERFACE_FLAG_FOLDED) && (flags & NMO_INTERFACE_FLAG_HEADER_ONLY))
        return " [folded] [header-only]";
    if (flags & NMO_INTERFACE_FLAG_FOLDED) return " [folded]";
    if (flags & NMO_INTERFACE_FLAG_HEADER_ONLY) return " [header-only]";
    return "";
}

static bool iface_add_diagnostics_json(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                       const void *data) {
    nmo_cmd_behavior_add_interface_diagnostics_json(doc, obj, (nmo_workspace_t *)data);
    return true;
}

static bool iface_add_script(nmo_cli_record_t *rec, const nmo_interface_data_t *idata) {
    const nmo_interface_script_header_t *sh = &idata->script;
    nmo_cli_record_t *so = nmo_cli_record_object(rec, "script");
    bool ok = so != NULL;
    ok = ok && nmo_cli_record_uint(so, "behavior_id", NULL, sh->behavior_id);
    ok = ok && nmo_cli_record_uint(so, "flags", NULL, sh->flags);
    ok = ok && nmo_cli_record_uint(so, "script_index", NULL, sh->script_index);
    ok = ok && nmo_cli_record_real(so, "h_pos", NULL, (double)sh->h_pos, NULL);
    ok = ok && nmo_cli_record_real(so, "v_pos", NULL, (double)sh->v_pos, NULL);
    ok = ok && nmo_cli_record_real(so, "h_start_pos", NULL, (double)sh->h_start_pos, NULL);
    ok = ok && nmo_cli_record_real(so, "v_start_pos", NULL, (double)sh->v_start_pos, NULL);
    ok = ok && nmo_cli_record_real(so, "v_size", NULL, (double)sh->v_size, NULL);
    ok = ok && nmo_cli_record_uint(so, "color", NULL, sh->color);
    ok = ok && nmo_cli_record_bool(so, "color_defaulted", NULL,
                                   iface_is_sectioned(idata) &&
                                   !iface_color_is_present(idata));
    ok = ok && nmo_cli_record_bool(so, "has_snapshot", NULL, sh->has_snapshot);
    if (ok && sh->has_snapshot) {
        nmo_cli_record_t *snap = nmo_cli_record_object(so, "snapshot");
        ok = snap != NULL &&
             nmo_cli_record_uint(snap, "width", NULL, sh->snapshot_desc.width) &&
             nmo_cli_record_uint(snap, "height", NULL, sh->snapshot_desc.height) &&
             nmo_cli_record_uint(snap, "size", NULL, sh->snapshot_size);
    }

    ok = ok && nmo_cli_record_title(so, "Script Header");
    ok = ok && nmo_cli_record_raw_fmt(so,
                                      "  behavior_id: %u  flags: 0x%X%s\n"
                                      "  pos: (%.1f, %.1f)  start: (%.1f, %.1f)  v_size: %.1f\n"
                                      "  script_index: %u\n",
                                      sh->behavior_id, sh->flags, iface_flag_marks(sh->flags),
                                      (double)sh->h_pos, (double)sh->v_pos,
                                      (double)sh->h_start_pos, (double)sh->v_start_pos,
                                      (double)sh->v_size, sh->script_index);
    if (ok && sh->has_snapshot) {
        ok = nmo_cli_record_raw_fmt(so, "  snapshot: %ux%u (%zu bytes)\n",
                                    sh->snapshot_desc.width, sh->snapshot_desc.height,
                                    sh->snapshot_size);
    }
    if (ok && sh->color) {
        ok = nmo_cli_record_raw_fmt(so, "  color: 0x%08X\n", sh->color);
    }
    return ok && iface_add_body(so, &sh->body, "Script");
}

static bool iface_add_subs(nmo_cli_record_t *rec, const nmo_interface_data_t *idata) {
    bool ok = true;
    if (idata->sub_count > 0) {
        ok = nmo_cli_record_title_fmt(rec, "Sub-behaviors (%zu)", idata->sub_count);
    }
    nmo_cli_record_array_t *arr = ok ? iface_inline_array(rec, "subs") : NULL;
    ok = ok && arr != NULL;
    for (size_t si = 0; ok && si < idata->sub_count; si++) {
        const nmo_interface_behavior_t *sb = &idata->subs[si];
        nmo_cli_record_t *so = iface_array_item(arr);
        ok = so != NULL;
        ok = ok && nmo_cli_record_uint(so, "behavior_id", NULL, sb->behavior_id);
        ok = ok && nmo_cli_record_uint(so, "flags", NULL, sb->flags);
        ok = ok && nmo_cli_record_uint(so, "depth", NULL, sb->depth);
        ok = ok && nmo_cli_record_real(so, "h_pos", NULL, (double)sb->h_pos, NULL);
        ok = ok && nmo_cli_record_real(so, "v_pos", NULL, (double)sb->v_pos, NULL);
        ok = ok && nmo_cli_record_real(so, "h_size", NULL, (double)sb->h_size, NULL);
        ok = ok && nmo_cli_record_real(so, "v_size", NULL, (double)sb->v_size, NULL);
        ok = ok && nmo_cli_record_real(so, "h_expand_size", NULL, (double)sb->h_expand_size, NULL);
        ok = ok && nmo_cli_record_real(so, "v_expand_size", NULL, (double)sb->v_expand_size, NULL);
        ok = ok && nmo_cli_record_raw_fmt(so,
                                          "  [%zu] id=%u depth=%u flags=0x%X%s\n"
                                          "      pos=(%.1f,%.1f) size=(%.1f,%.1f) expand=(%.1f,%.1f)\n",
                                          si, sb->behavior_id, sb->depth, sb->flags,
                                          iface_flag_marks(sb->flags),
                                          (double)sb->h_pos, (double)sb->v_pos,
                                          (double)sb->h_size, (double)sb->v_size,
                                          (double)sb->h_expand_size, (double)sb->v_expand_size);
        char *label = ok ? nmo_tool_strdup_fmt("Sub[%zu]", si) : NULL;
        ok = ok && label != NULL && iface_add_body(so, &sb->body, label);
        free(label);
    }
    return ok;
}

static bool iface_add_extra_entry(nmo_cli_record_array_t *arr, size_t ei,
                                  const nmo_interface_extra_entry_t *ee) {
    nmo_cli_record_t *eo = iface_array_item(arr);
    bool ok = eo != NULL;
    ok = ok && nmo_cli_record_uint(eo, "type", NULL, ee->type);
    ok = ok && nmo_cli_record_uint(eo, "id1", NULL, ee->id1);
    ok = ok && nmo_cli_record_raw_fmt(eo, "  [%zu] type=%u id1=%u", ei, ee->type, ee->id1);
    if (ok && ee->type == 3) {
        ok = nmo_cli_record_uint(eo, "id2", NULL, ee->id2) &&
             nmo_cli_record_raw_fmt(eo, " id2=%u", ee->id2);
    }
    if (ok && ee->type == 4) {
        ok = nmo_cli_record_int(eo, "value", NULL, ee->value) &&
             nmo_cli_record_raw_fmt(eo, " value=%d", ee->value);
    }
    if (ok && ee->sub_count > 0) {
        ok = nmo_cli_record_raw_fmt(eo, " sub_entries=%zu", ee->sub_count);
    }
    ok = ok && nmo_cli_record_raw(eo, "\n");
    if (!ok || ee->sub_count == 0) {
        return ok;
    }
    nmo_cli_record_array_t *subs = iface_inline_array(eo, "sub_entries");
    ok = subs != NULL;
    for (size_t si = 0; ok && si < ee->sub_count; si++) {
        const nmo_interface_extra_sub_t *se = &ee->sub_entries[si];
        nmo_cli_record_t *so = iface_array_item(subs);
        ok = so != NULL;
        ok = ok && nmo_cli_record_int(so, "value1", NULL, se->value1);
        ok = ok && nmo_cli_record_int(so, "value2", NULL, se->value2);
        ok = ok && nmo_cli_record_uint(so, "id1", NULL, se->id1);
        ok = ok && nmo_cli_record_uint(so, "id2", NULL, se->id2);
        ok = ok && nmo_cli_record_raw_fmt(so, "    val1=%d val2=%d id1=%u id2=%u",
                                          se->value1, se->value2, se->id1, se->id2);
        if (ok && se->data_size > 0) {
            ok = nmo_cli_record_uint(so, "data_size", NULL, se->data_size) &&
                 nmo_cli_record_raw_fmt(so, " data=%zu bytes", se->data_size);
        }
        ok = ok && nmo_cli_record_raw(so, "\n");
    }
    return ok;
}

static bool iface_add_extra(nmo_cli_record_t *rec, const nmo_interface_extra_t *ex) {
    nmo_cli_record_t *eo = nmo_cli_record_object(rec, "extra");
    bool ok = eo != NULL;
    ok = ok && nmo_cli_record_bool(eo, "present", NULL, ex->present);
    if (!ok || !ex->present) {
        return ok;
    }
    ok = nmo_cli_record_uint(eo, "version", NULL, ex->version) &&
         nmo_cli_record_uint(eo, "entry_count", NULL, ex->entry_count) &&
         nmo_cli_record_title_fmt(eo, "Extra Data (v%u, %zu entries)",
                                  ex->version, ex->entry_count);
    nmo_cli_record_array_t *arr = ok ? iface_inline_array(eo, "entries") : NULL;
    ok = ok && arr != NULL;
    for (size_t ei = 0; ok && ei < ex->entry_count; ei++) {
        ok = iface_add_extra_entry(arr, ei, &ex->entries[ei]);
    }
    return ok;
}

static bool iface_add_section_presence(nmo_cli_record_t *rec, const char *label,
                                       const nmo_interface_body_t *b) {
    return nmo_cli_record_raw_fmt(rec, "  %s: links=%s ops=%s comments=%s unknown_flag=%s\n",
                                  label,
                                  b->has_links_section ? "yes" : "no",
                                  b->has_operations_section ? "yes" : "no",
                                  b->has_comments_section ? "yes" : "no",
                                  b->has_unknown_flag_section ? "yes" : "no");
}

static nmo_cli_record_t *iface_show_record(const nmo_interface_data_t *idata,
                                           nmo_object_id_t target_id,
                                           const char *name,
                                           nmo_workspace_t *workspace) {
    bool sectioned = iface_is_sectioned(idata);
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_uint(rec, "behavior_id", NULL, target_id);
    ok = ok && nmo_cli_record_str(rec, "name", NULL, (name && name[0]) ? name : "");
    ok = ok && nmo_cli_record_uint(rec, "version", NULL, idata->version);
    ok = ok && nmo_cli_record_uint(rec, "format_flags", NULL, idata->format_flags);
    ok = ok && nmo_cli_record_bool(rec, "sectioned_layout", NULL, sectioned);
    ok = ok && nmo_cli_record_bool(rec, "sectioned_root_is_graph", NULL,
                                   sectioned &&
                                   (idata->format_flags & NMO_INTERFACE_FORMAT_ROOT_GRAPH) != 0u);
    ok = ok && nmo_cli_record_str(rec, "root_kind", NULL, iface_root_kind_name(idata));
    ok = ok && nmo_cli_record_uint(rec, "sub_count", NULL, idata->sub_count);
    ok = ok && nmo_cli_record_json(rec, iface_add_diagnostics_json, workspace);

    ok = ok && nmo_cli_record_title_fmt(rec, "Interface: Behavior #%u %s", target_id,
                                        (name && name[0]) ? name : "(unnamed)");
    ok = ok && nmo_cli_record_raw_fmt(rec, "  version: 0x%02X  layout: %s  root: %s\n",
                                      idata->version,
                                      sectioned ? "sectioned" : "inline",
                                      iface_root_kind_name(idata));
    ok = ok && iface_add_script(rec, idata);
    ok = ok && iface_add_subs(rec, idata);
    ok = ok && iface_add_extra(rec, &idata->extra);

    /* Section presence (only relevant for sectioned layout) */
    if (ok && sectioned) {
        ok = nmo_cli_record_title(rec, "Section Presence") &&
             iface_add_section_presence(rec, "Script", &idata->script.body);
        for (size_t si = 0; ok && si < idata->sub_count; si++) {
            char *label = nmo_tool_strdup_fmt("Sub[%zu]", si);
            ok = label != NULL &&
                 iface_add_section_presence(rec, label, &idata->subs[si].body);
            free(label);
        }
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

static void iface_count_body(const nmo_interface_body_t *body, size_t *links,
                             size_t *routing, size_t *ops, size_t *comments,
                             size_t *locals, size_t *shared) {
    *links += body->link_count;
    *ops += body->operation_count;
    *comments += body->comment_count;
    for (size_t li = 0; li < body->link_count; li++)
        *routing += body->links[li].point_count;
    if (body->has_params) {
        *locals += body->params.local_count;
        *shared += body->params.shared_count;
    }
}

/* Text-only summary for --brief. */
static nmo_cli_record_t *iface_brief_record(const nmo_interface_data_t *idata,
                                            nmo_object_id_t target_id,
                                            const char *name) {
    /* Count totals across all bodies */
    size_t total_links = 0, total_routing = 0, total_ops = 0, total_comments = 0;
    size_t total_local = 0, total_shared = 0;
    size_t folded_count = (idata->script.flags & NMO_INTERFACE_FLAG_FOLDED) ? 1 : 0;
    iface_count_body(&idata->script.body, &total_links, &total_routing, &total_ops,
                     &total_comments, &total_local, &total_shared);
    for (size_t si = 0; si < idata->sub_count; si++) {
        const nmo_interface_behavior_t *sb = &idata->subs[si];
        iface_count_body(&sb->body, &total_links, &total_routing, &total_ops,
                         &total_comments, &total_local, &total_shared);
        if (sb->flags & NMO_INTERFACE_FLAG_FOLDED)
            folded_count++;
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_title_fmt(rec, "Interface: Behavior #%u %s", target_id,
                                        (name && name[0]) ? name : "(unnamed)");
    ok = ok && nmo_cli_record_text_fmt(rec, "Version", "0x%02X", idata->version);
    ok = ok && nmo_cli_record_text(rec, "Layout",
                                   iface_is_sectioned(idata) ? "sectioned" : "inline");
    ok = ok && nmo_cli_record_text(rec, "Root kind", iface_root_kind_name(idata));
    if (ok && idata->script.color) {
        ok = nmo_cli_record_text_fmt(rec, "Color", "0x%08X", idata->script.color);
    }
    ok = ok && nmo_cli_record_text_fmt(rec, "Sub-behaviors", "%zu", idata->sub_count);
    ok = ok && nmo_cli_record_text_fmt(rec, "Total links", "%zu", total_links);
    ok = ok && nmo_cli_record_text_fmt(rec, "Routing points", "%zu", total_routing);
    ok = ok && nmo_cli_record_text_fmt(rec, "Operations", "%zu", total_ops);
    ok = ok && nmo_cli_record_text_fmt(rec, "Comments", "%zu", total_comments);
    ok = ok && nmo_cli_record_text_fmt(rec, "Params", "%zu local + %zu shared",
                                       total_local, total_shared);
    if (ok && idata->script.has_snapshot) {
        ok = nmo_cli_record_text_fmt(rec, "Snapshot", "%ux%u (%zu bytes)",
                                     idata->script.snapshot_desc.width,
                                     idata->script.snapshot_desc.height,
                                     idata->script.snapshot_size);
    } else if (ok) {
        ok = nmo_cli_record_text(rec, "Snapshot", "(none)");
    }
    if (ok && idata->extra.present) {
        ok = nmo_cli_record_text_fmt(rec, "Extra data", "v%u, %zu entries",
                                     idata->extra.version, idata->extra.entry_count);
    } else if (ok) {
        ok = nmo_cli_record_text(rec, "Extra data", "(none)");
    }
    ok = ok && nmo_cli_record_text_fmt(rec, "Folded", "%zu", folded_count);
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

static int behavior_iface_show_parse(int argc, char **argv,
                                     bool expect_file_operand,
                                     behavior_iface_show_args_t *args,
                                     const char *usage) {
    memset(args, 0, sizeof(*args));

    static const nmo_opt_def_t opts[] = {
        {"--brief", "-b", NMO_OPT_FLAG, "Brief summary output"},
        NMO_OPT_DEF_JSON,
        {"--id",    "-i", NMO_OPT_UINT, "Behavior object ID"},
        {"--name",  "-n", NMO_OPT_STRING, "Behavior object name"},
    };
    enum { OPT_BRIEF, OPT_JSON, OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = NULL;
    if (expect_file_operand) {
        positional_id = has_selector_opt ? NULL : (r.pos_count >= 2 ? r.pos_args[0] : NULL);
        if ((has_selector_opt && r.pos_count < 1) || (!has_selector_opt && positional_id == NULL)) {
            fprintf(stderr, "Usage: %s\n", usage);
            fprintf(stderr, "Output: use global -f json or command --json for machine-readable output.\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else if (has_selector_opt) {
        if (r.pos_count != 0) {
            fprintf(stderr, "Error: Unexpected argument '%s'\n", r.pos_args[0]);
            fprintf(stderr, "Usage: %s\n", usage);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else {
        if (r.pos_count != 1) {
            fprintf(stderr, "Usage: %s\n", usage);
            fprintf(stderr, "Output: use global -f json or command --json for machine-readable output.\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        positional_id = r.pos_args[0];
    }

    args->brief = nmo_opt_flag(&vals[OPT_BRIEF]);
    args->selector = (nmo_core_object_selector_t){
        .has_id = vals[OPT_ID].present,
        .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
        .positional_id = positional_id,
        .name = nmo_opt_str(&vals[OPT_NAME]),
        .required_base_class = NMO_CID_BEHAVIOR,
        .selector_label = "Behavior",
        .type_label = "CKBehavior",
    };
    return NMO_CLI_EXIT_SUCCESS;
}

static int behavior_iface_show_run(nmo_cmd_ctx_t *ctx,
                                   const behavior_iface_show_args_t *args,
                                   bool close_ctx,
                                   const char *usage) {
    nmo_cmd_ctx_t c = *ctx;

    if (nmo_tool_owner_ensure_behavior_acceleration(c.workspace) != NMO_OK) {
        fprintf(stderr, "Error: Failed to build behavior acceleration\n");
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_t *beh = NULL;
    nmo_object_id_t target_id = 0;
    int rc = nmo_core_resolve_one_object(&c, &args->selector, &beh, &target_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: %s\n", usage);
        return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
    }

    const nmo_behavior_state_t *bs = (const nmo_behavior_state_t *)nmo_object_get_state(beh);
    if (!bs) {
        fprintf(stderr, "Error: No state for behavior %u\n", target_id);
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR)
                         : NMO_CLI_EXIT_ARG_ERROR;
    }

    const nmo_interface_data_t *idata = bs->interface_data;
    if (!idata) {
        fprintf(stderr, "Error: Behavior %u has no interface data\n", target_id);
        nmo_cmd_behavior_print_interface_diagnostics(stderr, c.workspace);
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR)
                         : NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *name = nmo_object_get_name(beh);
    nmo_cli_record_t *rec = args->brief && !c.is_json
        ? iface_brief_record(idata, target_id, name)
        : iface_show_record(idata, target_id, name, c.workspace);
    rc = nmo_cmd_ctx_emit_record(&c, rec, "behavior.interface",
                                 args->brief ? 22 : 0, c.colorize);
    return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
}

int nmo_cmd_behavior_iface_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    behavior_iface_show_args_t args;
    const char *usage = "nmo behavior interface [--brief] [--json] [--id <id> | --name <name> | <id>] <file>";
    int rc = behavior_iface_show_parse(argc, argv, true, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    return behavior_iface_show_run(&c, &args, true, usage);
}

int nmo_cmd_behavior_interface_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv) {
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: behavior interface show [--brief] [--json] [--id <id> | --name <name> | <id>]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    int arg_offset = 0;
    if (strcmp(argv[0], "interface") == 0 || strcmp(argv[0], "iface") == 0) {
        if (argc >= 2 &&
            (strcmp(argv[1], "show") == 0 || strcmp(argv[1], "s") == 0)) {
            arg_offset = 1;
        } else if (argc >= 2 &&
                   (argv[1][0] == '-' ||
                    (argv[1][0] >= '0' && argv[1][0] <= '9'))) {
            arg_offset = 0;
        } else {
            fprintf(stderr, "Usage: behavior interface show [--brief] [--json] [--id <id> | --name <name> | <id>]\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }
    if (argc <= arg_offset ||
        (strcmp(argv[arg_offset], "show") != 0 &&
         strcmp(argv[arg_offset], "s") != 0 &&
         strcmp(argv[arg_offset], "interface") != 0 &&
         strcmp(argv[arg_offset], "iface") != 0)) {
        fprintf(stderr, "Usage: behavior interface show [--brief] [--json] [--id <id> | --name <name> | <id>]\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    behavior_iface_show_args_t args;
    const char *usage = "behavior interface show [--brief] [--json] [--id <id> | --name <name> | <id>]";
    int rc = behavior_iface_show_parse(argc - arg_offset, argv + arg_offset,
                                       false, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    return behavior_iface_show_run(ctx, &args, false, usage);
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

