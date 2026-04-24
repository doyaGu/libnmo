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
#include "behavior/nmo_behavior_edit.h"
#include "runtime/nmo_context.h"
#include "session/nmo_session.h"
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

static void iface_print_body_links(FILE *out, const nmo_interface_body_t *body) {
    for (size_t li = 0; li < body->link_count; li++) {
        const nmo_interface_link_t *lk = &body->links[li];
        fprintf(out, "  #%u %s%s  %u:%d:%s -> %u:%d:%s",
                lk->link_id,
                lk->type == 1 ? "behavior" : lk->type == 2 ? "param" : "?",
                lk->highlight ? " hl" : "",
                lk->start.id, lk->start.index, iface_endpoint_type_name(lk->start.type),
                lk->end.id, lk->end.index, iface_endpoint_type_name(lk->end.type));
        if (lk->point_count > 0) {
            fprintf(out, "  pts=%zu [", lk->point_count);
            for (size_t pi = 0; pi < lk->point_count && pi < 4; pi++) {
                if (pi > 0) fprintf(out, ", ");
                fprintf(out, "(%.0f,%.0f)", lk->points[pi * 2], lk->points[pi * 2 + 1]);
            }
            if (lk->point_count > 4) fprintf(out, ", ...");
            fprintf(out, "]");
        }
        fprintf(out, "\n");
    }
}

static void iface_print_body_operations(FILE *out, const nmo_interface_body_t *body) {
    for (size_t oi = 0; oi < body->operation_count; oi++) {
        const nmo_interface_operation_t *op = &body->operations[oi];
        fprintf(out, "  id=%u pos=(%.1f, %.1f)\n", op->id, op->h_pos, op->v_pos);
    }
}

static void iface_print_body_comments(FILE *out, const nmo_interface_body_t *body) {
    for (size_t ci = 0; ci < body->comment_count; ci++) {
        const nmo_interface_comment_t *cm = &body->comments[ci];
        fprintf(out, "  rect=(%.0f,%.0f,%.0f,%.0f)", cm->left, cm->top, cm->right, cm->bottom);
        if (cm->style_flags) fprintf(out, " flags=0x%X", cm->style_flags);
        fprintf(out, "\n    \"%s\"\n", cm->text ? cm->text : "");
    }
}

static const char *iface_param_style_name(uint32_t style) {
    if (style & NMO_INTERFACE_PARAM_STYLE_COLLAPSED) return " [collapsed]";
    if (style & NMO_INTERFACE_PARAM_STYLE_NAMEVALUE) return " [name+value]";
    if (style & NMO_INTERFACE_PARAM_STYLE_VALUE)     return " [value]";
    if (style & NMO_INTERFACE_PARAM_STYLE_NAME)      return " [name]";
    return "";
}

static void iface_print_body_params(FILE *out, const nmo_interface_body_t *body) {
    if (!body->has_params) {
        fprintf(out, "  (not parsed)\n");
        return;
    }
    const nmo_interface_param_set_t *ps = &body->params;
    fprintf(out, "  Local (%zu):", ps->local_count);
    for (size_t pi = 0; pi < ps->local_count; pi++)
        fprintf(out, " (%d,%d)%s", ps->locals[pi].h_pos, ps->locals[pi].v_pos,
                iface_param_style_name(ps->locals[pi].style));
    fprintf(out, "\n  Shared (%zu):", ps->shared_count);
    for (size_t pi = 0; pi < ps->shared_count; pi++) {
        fprintf(out, " (%d,%d)%s", ps->shared[pi].h_pos, ps->shared[pi].v_pos,
                iface_param_style_name(ps->shared[pi].style));
        if (ps->shared[pi].source_id)
            fprintf(out, "->%u", ps->shared[pi].source_id);
    }
    fprintf(out, "\n");
}

static void iface_print_body_graph_io(FILE *out, const nmo_interface_graph_io_t *gio) {
    fprintf(out, "  inward_inputs (%zu):", gio->inward_input_count);
    for (size_t i = 0; i < gio->inward_input_count; i++)
        fprintf(out, " %d", gio->inward_inputs[i]);
    fprintf(out, "\n  outward_inputs (%zu):", gio->outward_input_count);
    for (size_t i = 0; i < gio->outward_input_count; i++)
        fprintf(out, " %d", gio->outward_inputs[i]);
    fprintf(out, "\n  inward_outputs (%zu):", gio->inward_output_count);
    for (size_t i = 0; i < gio->inward_output_count; i++)
        fprintf(out, " %d", gio->inward_outputs[i]);
    fprintf(out, "\n  outward_outputs (%zu):", gio->outward_output_count);
    for (size_t i = 0; i < gio->outward_output_count; i++)
        fprintf(out, " %d", gio->outward_outputs[i]);
    fprintf(out, "\n");
}

static void iface_json_add_endpoint(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                    const char *key, const nmo_interface_endpoint_t *ep) {
    yyjson_mut_val *eo = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, eo, "id", ep->id);
    yyjson_mut_obj_add_int(doc, eo, "index", ep->index);
    yyjson_mut_obj_add_uint(doc, eo, "type", ep->type);
    nmo_cli_json_add_str_safe(doc, eo, "type_name", iface_endpoint_type_name(ep->type));
    yyjson_mut_obj_add_val(doc, obj, key, eo);
}

static yyjson_mut_val *iface_json_body(yyjson_mut_doc *doc, const nmo_interface_body_t *body) {
    yyjson_mut_val *bo = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, bo, "has_body", body->has_body);
    if (!body->has_body) return bo;

    /* Links */
    {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t li = 0; li < body->link_count; li++) {
            const nmo_interface_link_t *lk = &body->links[li];
            yyjson_mut_val *lo = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, lo, "type", lk->type);
            yyjson_mut_obj_add_bool(doc, lo, "highlight", lk->highlight);
            yyjson_mut_obj_add_uint(doc, lo, "link_id", lk->link_id);
            iface_json_add_endpoint(doc, lo, "start", &lk->start);
            iface_json_add_endpoint(doc, lo, "end", &lk->end);
            yyjson_mut_obj_add_uint(doc, lo, "point_count", (uint64_t)lk->point_count);
            if (lk->point_count > 0) {
                yyjson_mut_val *pts = yyjson_mut_arr(doc);
                for (size_t pi = 0; pi < lk->point_count; pi++) {
                    yyjson_mut_val *pt = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_real(doc, pt, "h", (double)lk->points[pi * 2]);
                    yyjson_mut_obj_add_real(doc, pt, "v", (double)lk->points[pi * 2 + 1]);
                    yyjson_mut_arr_add_val(pts, pt);
                }
                yyjson_mut_obj_add_val(doc, lo, "points", pts);
            }
            yyjson_mut_arr_add_val(arr, lo);
        }
        yyjson_mut_obj_add_val(doc, bo, "links", arr);
    }

    /* Operations */
    {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t oi = 0; oi < body->operation_count; oi++) {
            const nmo_interface_operation_t *op = &body->operations[oi];
            yyjson_mut_val *oo = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, oo, "id", op->id);
            yyjson_mut_obj_add_real(doc, oo, "h_pos", (double)op->h_pos);
            yyjson_mut_obj_add_real(doc, oo, "v_pos", (double)op->v_pos);
            yyjson_mut_arr_add_val(arr, oo);
        }
        yyjson_mut_obj_add_val(doc, bo, "operations", arr);
    }

    /* Comments */
    {
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t ci = 0; ci < body->comment_count; ci++) {
            const nmo_interface_comment_t *cm = &body->comments[ci];
            yyjson_mut_val *co = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_real(doc, co, "left", (double)cm->left);
            yyjson_mut_obj_add_real(doc, co, "top", (double)cm->top);
            yyjson_mut_obj_add_real(doc, co, "right", (double)cm->right);
            yyjson_mut_obj_add_real(doc, co, "bottom", (double)cm->bottom);
            if (cm->text)
                nmo_cli_json_add_str_safe(doc, co, "text", cm->text);
            else
                yyjson_mut_obj_add_null(doc, co, "text");
            yyjson_mut_obj_add_uint(doc, co, "style_flags", cm->style_flags);
            yyjson_mut_arr_add_val(arr, co);
        }
        yyjson_mut_obj_add_val(doc, bo, "comments", arr);
    }

    /* Params */
    {
        yyjson_mut_val *po = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, po, "has_params", body->has_params);
        if (body->has_params) {
            const nmo_interface_param_set_t *ps = &body->params;
            yyjson_mut_obj_add_uint(doc, po, "local_count", (uint64_t)ps->local_count);
            yyjson_mut_obj_add_uint(doc, po, "shared_count", (uint64_t)ps->shared_count);
            {
                yyjson_mut_val *arr = yyjson_mut_arr(doc);
                for (size_t pi = 0; pi < ps->local_count; pi++) {
                    yyjson_mut_val *p = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_int(doc, p, "h_pos", ps->locals[pi].h_pos);
                    yyjson_mut_obj_add_int(doc, p, "v_pos", ps->locals[pi].v_pos);
                    yyjson_mut_obj_add_uint(doc, p, "style", ps->locals[pi].style);
                    yyjson_mut_arr_add_val(arr, p);
                }
                yyjson_mut_obj_add_val(doc, po, "locals", arr);
            }
            {
                yyjson_mut_val *arr = yyjson_mut_arr(doc);
                for (size_t pi = 0; pi < ps->shared_count; pi++) {
                    yyjson_mut_val *p = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_int(doc, p, "h_pos", ps->shared[pi].h_pos);
                    yyjson_mut_obj_add_int(doc, p, "v_pos", ps->shared[pi].v_pos);
                    yyjson_mut_obj_add_uint(doc, p, "style", ps->shared[pi].style);
                    yyjson_mut_obj_add_uint(doc, p, "source_id", ps->shared[pi].source_id);
                    yyjson_mut_arr_add_val(arr, p);
                }
                yyjson_mut_obj_add_val(doc, po, "shared", arr);
            }
        }
        yyjson_mut_obj_add_val(doc, bo, "params", po);
    }

    /* Graph IO */
    if (body->has_graph_io && body->graph_io) {
        const nmo_interface_graph_io_t *gio = body->graph_io;
        yyjson_mut_val *go = yyjson_mut_obj(doc);
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            for (size_t i = 0; i < gio->inward_input_count; i++)
                yyjson_mut_arr_add_int(doc, arr, gio->inward_inputs[i]);
            yyjson_mut_obj_add_val(doc, go, "inward_inputs", arr);
        }
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            for (size_t i = 0; i < gio->outward_input_count; i++)
                yyjson_mut_arr_add_int(doc, arr, gio->outward_inputs[i]);
            yyjson_mut_obj_add_val(doc, go, "outward_inputs", arr);
        }
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            for (size_t i = 0; i < gio->inward_output_count; i++)
                yyjson_mut_arr_add_int(doc, arr, gio->inward_outputs[i]);
            yyjson_mut_obj_add_val(doc, go, "inward_outputs", arr);
        }
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            for (size_t i = 0; i < gio->outward_output_count; i++)
                yyjson_mut_arr_add_int(doc, arr, gio->outward_outputs[i]);
            yyjson_mut_obj_add_val(doc, go, "outward_outputs", arr);
        }
        yyjson_mut_obj_add_val(doc, bo, "graph_io", go);
    }

    /* Section presence flags */
    yyjson_mut_obj_add_bool(doc, bo, "has_links_section", body->has_links_section);
    yyjson_mut_obj_add_bool(doc, bo, "has_operations_section", body->has_operations_section);
    yyjson_mut_obj_add_bool(doc, bo, "has_comments_section", body->has_comments_section);
    yyjson_mut_obj_add_bool(doc, bo, "has_unknown_flag_section", body->has_unknown_flag_section);
    if (body->has_unknown_flag_section)
        yyjson_mut_obj_add_int(doc, bo, "unknown_flag", body->unknown_flag);

    return bo;
}

static void iface_print_body_text(FILE *out, const nmo_interface_body_t *body,
                                  const char *label, bool colorize) {
    if (!body->has_body) {
        fprintf(out, "  (header only)\n");
        return;
    }
    if (body->link_count > 0) {
        char heading[128];
        snprintf(heading, sizeof(heading), "%s Links (%zu)", label, body->link_count);
        nmo_cli_print_heading(out, heading, colorize);
        iface_print_body_links(out, body);
    }
    if (body->operation_count > 0) {
        char heading[128];
        snprintf(heading, sizeof(heading), "%s Operations (%zu)", label, body->operation_count);
        nmo_cli_print_heading(out, heading, colorize);
        iface_print_body_operations(out, body);
    }
    if (body->comment_count > 0) {
        char heading[128];
        snprintf(heading, sizeof(heading), "%s Comments (%zu)", label, body->comment_count);
        nmo_cli_print_heading(out, heading, colorize);
        iface_print_body_comments(out, body);
    }
    if (body->has_params) {
        char heading[128];
        snprintf(heading, sizeof(heading), "%s Parameters", label);
        nmo_cli_print_heading(out, heading, colorize);
        iface_print_body_params(out, body);
    }
    if (body->has_graph_io && body->graph_io) {
        char heading[128];
        snprintf(heading, sizeof(heading), "%s Graph IO", label);
        nmo_cli_print_heading(out, heading, colorize);
        iface_print_body_graph_io(out, body->graph_io);
    }
    if (body->has_unknown_flag_section) {
        fprintf(out, "  unknown_flag: %d\n", body->unknown_flag);
    }
}

/* ================================================================
 * Interface edit: shared helpers
 * ================================================================ */

static nmo_interface_data_t *iface_edit_get_data(
    nmo_cmd_ctx_t *c, uint32_t target_id,
    nmo_object_t **out_obj)
{
    nmo_object_repository_t *repo = nmo_session_get_repository(c->session);
    if (nmo_session_ensure_behavior_acceleration(c->session) != NMO_OK) {
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
        nmo_cmd_behavior_print_interface_diagnostics(stderr, c->session);
        return NULL;
    }
    if (out_obj) *out_obj = beh;
    return bs->interface_data;
}

static bool iface_validate_behavior_id(nmo_cmd_ctx_t *c, uint32_t beh_id) {
    nmo_object_repository_t *repo = nmo_session_get_repository(c->session);
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

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (doc == NULL) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "target_id", (uint64_t)args->target_id);
        nmo_cli_json_add_uint_safe(doc, data, "behavior_id", (uint64_t)args->beh_id);
        yyjson_mut_obj_add_real(doc, data, "h_pos", (double)args->h);
        yyjson_mut_obj_add_real(doc, data, "v_pos", (double)args->v);
        if (!dry_run && output_path != NULL) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        nmo_cmd_ctx_json_end(c, doc, data, "behavior.interface.set-pos");
    } else {
        if (dry_run) {
            fprintf(c->out, "[dry-run] ");
        }
        fprintf(c->out, "Moved behavior %u to (%.1f, %.1f)\n",
                args->beh_id, (double)args->h, (double)args->v);
        if (!dry_run && output_path != NULL) {
            fprintf(c->out, "Saved to: %s\n", output_path);
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
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
    const char *verb = args->fold ? "Folded" : "Unfolded";
    const char *command = args->fold ? "behavior.interface.fold" : "behavior.interface.unfold";

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (doc == NULL) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_bool_safe(doc, data, "folded", args->fold);
        nmo_cli_json_add_uint_safe(doc, data, "target_id", (uint64_t)args->target_id);
        nmo_cli_json_add_uint_safe(doc, data, "behavior_id", (uint64_t)args->beh_id);
        if (!dry_run && output_path != NULL) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        nmo_cmd_ctx_json_end(c, doc, data, command);
    } else {
        if (dry_run) {
            fprintf(c->out, "[dry-run] ");
        }
        fprintf(c->out, "%s behavior %u\n", verb, args->beh_id);
        if (!dry_run && output_path != NULL) {
            fprintf(c->out, "Saved to: %s\n", output_path);
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
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

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (doc == NULL) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "target_id", (uint64_t)args->target_id);
        nmo_cli_json_add_uint_safe(doc, data, "color", (uint64_t)args->color);
        nmo_cli_json_add_bool_safe(doc, data, "color_persisted",
                                   args->color_persisted);
        if (args->warning != NULL) {
            nmo_cli_json_add_str_safe(doc, data, "warning", args->warning);
        }
        if (!dry_run && output_path != NULL) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        nmo_cmd_ctx_json_end(c, doc, data, "behavior.interface.set-color");
    } else {
        if (dry_run) {
            fprintf(c->out, "[dry-run] ");
        }
        fprintf(c->out, "Set script color to #%06X\n", (unsigned)args->color);
        if (args->warning != NULL) {
            fprintf(c->out, "Warning: %s\n", args->warning);
        }
        if (!dry_run && output_path != NULL) {
            fprintf(c->out, "Saved to: %s\n", output_path);
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
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

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (doc == NULL) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "target_id", (uint64_t)args->target_id);
        nmo_cli_json_add_bool_safe(doc, data, "canonicalized", true);
        nmo_cli_json_add_bool_safe(doc, data, "sectioned_layout",
                                   args->sectioned_layout);
        nmo_cli_json_add_bool_safe(doc, data, "sectioned_root_is_graph",
                                   args->sectioned_root_is_graph);
        nmo_cli_json_add_str_safe(doc, data, "root_kind",
                                  args->root_kind ? args->root_kind : "script");
        nmo_cli_json_add_bool_safe(doc, data, "color_persisted",
                                   args->color_persisted);
        if (!dry_run && output_path != NULL) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        nmo_cmd_ctx_json_end(c, doc, data, "behavior.interface.canonicalize");
    } else {
        if (dry_run) {
            fprintf(c->out, "[dry-run] ");
        }
        fprintf(c->out, "Canonicalized interface chunk for behavior %u\n",
                args->target_id);
        fprintf(c->out, "Layout: %s  Root: %s\n",
                args->sectioned_layout ? "sectioned" : "inline",
                args->root_kind ? args->root_kind : "script");
        if (!dry_run && output_path != NULL) {
            fprintf(c->out, "Saved to: %s\n", output_path);
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
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

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (doc == NULL) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "target_id", (uint64_t)args->target_id);
        nmo_cli_json_add_uint_safe(doc, data, "body_id", (uint64_t)args->body_id);
        if (args->op == IFACE_COMMENT_ADD) {
            nmo_cli_json_add_uint_safe(doc, data, "index", (uint64_t)args->result_index);
        } else {
            nmo_cli_json_add_uint_safe(doc, data, "index", (uint64_t)args->index);
        }
        if (!dry_run && output_path != NULL) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        nmo_cmd_ctx_json_end(c, doc, data, iface_comment_command_name(args->op));
    } else {
        if (dry_run) {
            fprintf(c->out, "[dry-run] ");
        }
        switch (args->op) {
        case IFACE_COMMENT_ADD:
            fprintf(c->out, "Added comment at index %zu to behavior %u\n",
                    args->result_index, args->body_id);
            break;
        case IFACE_COMMENT_REMOVE:
            fprintf(c->out, "Removed comment %u from behavior %u\n",
                    args->index, args->body_id);
            break;
        case IFACE_COMMENT_SET_TEXT:
            fprintf(c->out, "Set comment %u text in behavior %u\n",
                    args->index, args->body_id);
            break;
        case IFACE_COMMENT_MOVE:
            fprintf(c->out, "Moved comment %u to (%.0f,%.0f,%.0f,%.0f) in behavior %u\n",
                    args->index,
                    (double)args->left,
                    (double)args->top,
                    (double)args->right,
                    (double)args->bottom,
                    args->body_id);
            break;
        case IFACE_COMMENT_SET_STYLE:
            fprintf(c->out, "Set comment %u style to 0x%X in behavior %u\n",
                    args->index, args->style, args->body_id);
            break;
        default:
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        if (!dry_run && output_path != NULL) {
            fprintf(c->out, "Saved to: %s\n", output_path);
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
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

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (doc == NULL) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "target_id", (uint64_t)args->target_id);
        nmo_cli_json_add_uint_safe(doc, data, "link_id", (uint64_t)args->link_id);
        if (args->op == IFACE_LINK_REMOVE_POINT || args->op == IFACE_LINK_MOVE_POINT) {
            nmo_cli_json_add_uint_safe(doc, data, "point_index", (uint64_t)args->point_index);
        }
        if (!dry_run && output_path != NULL) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        nmo_cmd_ctx_json_end(c, doc, data, iface_link_command_name(args->op));
    } else {
        if (dry_run) {
            fprintf(c->out, "[dry-run] ");
        }
        switch (args->op) {
        case IFACE_LINK_ADD_POINT:
            fprintf(c->out, "Added point (%.1f, %.1f) to link %u\n",
                    (double)args->h, (double)args->v, args->link_id);
            break;
        case IFACE_LINK_CLEAR_POINTS:
            fprintf(c->out, "Cleared all routing points from link %u\n", args->link_id);
            break;
        case IFACE_LINK_REMOVE_POINT:
            fprintf(c->out, "Removed point %u from link %u\n",
                    args->point_index, args->link_id);
            break;
        case IFACE_LINK_MOVE_POINT:
            fprintf(c->out, "Moved point %u of link %u to (%.1f, %.1f)\n",
                    args->point_index, args->link_id, (double)args->h, (double)args->v);
            break;
        case IFACE_LINK_SET_HIGHLIGHT:
            fprintf(c->out, "Set link %u highlight %s\n",
                    args->link_id, args->highlight ? "on" : "off");
            break;
        default:
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        if (!dry_run && output_path != NULL) {
            fprintf(c->out, "Saved to: %s\n", output_path);
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
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

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (doc == NULL) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "target_id", (uint64_t)args->target_id);
        nmo_cli_json_add_uint_safe(doc, data, "operation_id", (uint64_t)args->op_id);
        yyjson_mut_obj_add_real(doc, data, "h", (double)args->h);
        yyjson_mut_obj_add_real(doc, data, "v", (double)args->v);
        if (!dry_run && output_path != NULL) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        nmo_cmd_ctx_json_end(c, doc, data, "behavior.interface.move-op");
    } else {
        if (dry_run) {
            fprintf(c->out, "[dry-run] ");
        }
        fprintf(c->out, "Moved operation %u to (%.1f, %.1f)\n",
                args->op_id, (double)args->h, (double)args->v);
        if (!dry_run && output_path != NULL) {
            fprintf(c->out, "Saved to: %s\n", output_path);
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
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

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (doc == NULL) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "target_id", (uint64_t)args->target_id);
        nmo_cli_json_add_uint_safe(doc, data, "body_id", (uint64_t)args->body_id);
        nmo_cli_json_add_uint_safe(doc, data, "param_index", (uint64_t)args->param_index);
        nmo_cli_json_add_bool_safe(doc, data, "shared", args->shared);
        if (!dry_run && output_path != NULL) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        nmo_cmd_ctx_json_end(c, doc, data, iface_param_command_name(args->op));
    } else {
        if (dry_run) {
            fprintf(c->out, "[dry-run] ");
        }
        switch (args->op) {
        case IFACE_PARAM_MOVE:
            fprintf(c->out, "Moved %s param %u to (%d, %d) in behavior %u\n",
                    args->shared ? "shared" : "local",
                    args->param_index,
                    args->h,
                    args->v,
                    args->body_id);
            break;
        case IFACE_PARAM_SET_STYLE:
            fprintf(c->out, "Set %s param %u style to 0x%X in behavior %u\n",
                    args->shared ? "shared" : "local",
                    args->param_index,
                    args->style,
                    args->body_id);
            break;
        default:
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        if (!dry_run && output_path != NULL) {
            fprintf(c->out, "Saved to: %s\n", output_path);
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
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

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (doc == NULL) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "target_id", (uint64_t)args->target_id);
        nmo_cli_json_add_uint_safe(doc, data, "behavior_id", (uint64_t)args->beh_id);
        yyjson_mut_obj_add_real(doc, data, "w", (double)args->w);
        yyjson_mut_obj_add_real(doc, data, "h", (double)args->h);
        if (!dry_run && output_path != NULL) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        nmo_cmd_ctx_json_end(c, doc, data, iface_sub_size_command_name(args->op));
    } else {
        if (dry_run) {
            fprintf(c->out, "[dry-run] ");
        }
        if (args->op == IFACE_SUB_RESIZE) {
            fprintf(c->out, "Resized behavior %u to (%.1f, %.1f)\n",
                    args->beh_id, (double)args->w, (double)args->h);
        } else {
            fprintf(c->out, "Set behavior %u expand size to (%.1f, %.1f)\n",
                    args->beh_id, (double)args->w, (double)args->h);
        }
        if (!dry_run && output_path != NULL) {
            fprintf(c->out, "Saved to: %s\n", output_path);
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
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

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (doc == NULL) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "target_id", (uint64_t)args->target_id);
        if (!dry_run && output_path != NULL) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        nmo_cmd_ctx_json_end(c, doc, data, iface_layout_command_name(args->op));
    } else {
        if (dry_run) {
            fprintf(c->out, "[dry-run] ");
        }
        if (args->op == IFACE_LAYOUT_SET_VIEWPORT) {
            fprintf(c->out, "Set viewport to (%.1f, %.1f) height=%.1f\n",
                    (double)args->a, (double)args->b, (double)args->c);
        } else {
            fprintf(c->out, "Translated all positions by (%.1f, %.1f)\n",
                    (double)args->a, (double)args->b);
        }
        if (!dry_run && output_path != NULL) {
            fprintf(c->out, "Saved to: %s\n", output_path);
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
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

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (doc == NULL) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "target_id", (uint64_t)args->target_id);
        nmo_cli_json_add_uint_safe(doc, data, "behavior_id", (uint64_t)args->resolved_body_id);
        yyjson_mut_obj_add_int(doc, data, "arrays_set", args->arrays_set);
        if (!dry_run && output_path != NULL) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        nmo_cmd_ctx_json_end(c, doc, data, "behavior.interface.set-graph-io");
    } else {
        if (dry_run) {
            fprintf(c->out, "[dry-run] ");
        }
        fprintf(c->out, "Set %d graph IO array(s) in behavior %u\n",
                args->arrays_set, args->resolved_body_id);
        if (!dry_run && output_path != NULL) {
            fprintf(c->out, "Saved to: %s\n", output_path);
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
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
        {"--output", "-o", NMO_OPT_STRING, "Output file path"},
        {"--dry-run", NULL, NMO_OPT_FLAG, "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o", NMO_OPT_STRING, "Output file path"},
        {"--dry-run", NULL, NMO_OPT_FLAG, "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o", NMO_OPT_STRING, "Output file path"},
        {"--dry-run", NULL, NMO_OPT_FLAG, "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o", NMO_OPT_STRING, "Output file path"},
        {"--dry-run", NULL, NMO_OPT_FLAG, "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[4];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 4 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o",    NMO_OPT_STRING, "Output file path"},
        {"--body",   NULL,    NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--text",   "-t",    NMO_OPT_STRING, "Comment text"},
        {"--rect",   "-r",    NMO_OPT_STRING, "Rectangle L,T,R,B"},
        {"--dry-run", NULL,    NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_TEXT, OPT_RECT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o",    NMO_OPT_STRING, "Output file path"},
        {"--body",   NULL,    NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--dry-run", NULL,    NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
        {"--body",   NULL,  NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--text",   "-t",  NMO_OPT_STRING, "New comment text"},
        {"--dry-run", NULL,  NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_TEXT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
        {"--body",   NULL,  NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--rect",   "-r",  NMO_OPT_STRING, "Rectangle L,T,R,B"},
        {"--dry-run", NULL,  NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_RECT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o",    NMO_OPT_STRING, "Output file path"},
        {"--body",   NULL,    NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--style",  "-s",    NMO_OPT_STRING, "Style flags value"},
        {"--dry-run", NULL,    NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_STYLE, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output",      "-o",  NMO_OPT_STRING, "Output file path"},
        {"--body",        NULL,  NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--param-index", NULL,  NMO_OPT_UINT,   "Parameter index"},
        {"--shared",      NULL,  NMO_OPT_FLAG,   "Target shared params instead of local"},
        {"--dry-run",     NULL,  NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_PARAM_INDEX, OPT_SHARED, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
    bool shared = vals[OPT_SHARED].present && vals[OPT_SHARED].val.flag;

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
        {"--output",      "-o",  NMO_OPT_STRING, "Output file path"},
        {"--body",        NULL,  NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--param-index", NULL,  NMO_OPT_UINT,   "Parameter index"},
        {"--shared",      NULL,  NMO_OPT_FLAG,   "Target shared params instead of local"},
        {"--style",       "-s",  NMO_OPT_STRING, "Style value"},
        {"--dry-run",     NULL,  NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_PARAM_INDEX, OPT_SHARED, OPT_STYLE, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
    bool shared = vals[OPT_SHARED].present && vals[OPT_SHARED].val.flag;

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
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        if (len == 0 || len >= 64) {
            return false;
        }

        char token[64];
        memcpy(token, start, len);
        token[len] = '\0';

        if (nmo_parse_i32_range(token, INT32_MIN, INT32_MAX, &out[count]) != NMO_OK) {
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
        {"--output",  "-o",  NMO_OPT_STRING, "Output file path"},
        {"--body",    NULL,  NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--in-in",   NULL,  NMO_OPT_STRING, "Inward input array (comma-separated ints)"},
        {"--in-out",  NULL,  NMO_OPT_STRING, "Inward output array (comma-separated ints)"},
        {"--out-in",  NULL,  NMO_OPT_STRING, "Outward input array (comma-separated ints)"},
        {"--out-out", NULL,  NMO_OPT_STRING, "Outward output array (comma-separated ints)"},
        {"--dry-run", NULL,  NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_IN_IN, OPT_IN_OUT, OPT_OUT_IN, OPT_OUT_OUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without saving"},
    };
    enum { OPT_OUTPUT, OPT_DRYRUN, OPT_COUNT };
    IFACE_STRIP_TARGET_SELECTOR_ARGS();
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    bool dry_run = vals[OPT_DRYRUN].present && vals[OPT_DRYRUN].val.flag;
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

static int behavior_iface_show_parse(int argc, char **argv,
                                     bool expect_file_operand,
                                     behavior_iface_show_args_t *args,
                                     const char *usage) {
    memset(args, 0, sizeof(*args));

    static const nmo_opt_def_t opts[] = {
        {"--brief", "-b", NMO_OPT_FLAG, "Brief summary output"},
        {"--json",  "-j", NMO_OPT_FLAG, "JSON output"},
        {"--id",    "-i", NMO_OPT_UINT, "Behavior object ID"},
        {"--name",  "-n", NMO_OPT_STRING, "Behavior object name"},
    };
    enum { OPT_BRIEF, OPT_JSON, OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
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

    args->brief = vals[OPT_BRIEF].present && vals[OPT_BRIEF].val.flag;
    args->selector = (nmo_core_object_selector_t){
        .has_id = vals[OPT_ID].present,
        .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
        .positional_id = positional_id,
        .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
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

    if (nmo_session_ensure_behavior_acceleration(c.session) != NMO_OK) {
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
        nmo_cmd_behavior_print_interface_diagnostics(stderr, c.session);
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR)
                         : NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *name = nmo_object_get_name(beh);

    /* --- JSON output --- */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "behavior_id", target_id);
        nmo_cli_json_add_str_safe(doc, data, "name",
                                  (name && name[0]) ? name : "");
        yyjson_mut_obj_add_uint(doc, data, "version", idata->version);
        yyjson_mut_obj_add_uint(doc, data, "format_flags", idata->format_flags);
        yyjson_mut_obj_add_bool(doc, data, "sectioned_layout",
                                iface_is_sectioned(idata));
        yyjson_mut_obj_add_bool(doc, data, "sectioned_root_is_graph",
                                iface_is_sectioned(idata) &&
                                (idata->format_flags &
                                 NMO_INTERFACE_FORMAT_ROOT_GRAPH) != 0u);
        nmo_cli_json_add_str_safe(doc, data, "root_kind",
                                  iface_root_kind_name(idata));
        yyjson_mut_obj_add_uint(doc, data, "sub_count", (uint64_t)idata->sub_count);
        nmo_cmd_behavior_add_interface_diagnostics_json(doc, data, c.session);

        /* Script header */
        {
            const nmo_interface_script_header_t *sh = &idata->script;
            yyjson_mut_val *so = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, so, "behavior_id", sh->behavior_id);
            yyjson_mut_obj_add_uint(doc, so, "flags", sh->flags);
            yyjson_mut_obj_add_uint(doc, so, "script_index", sh->script_index);
            yyjson_mut_obj_add_real(doc, so, "h_pos", (double)sh->h_pos);
            yyjson_mut_obj_add_real(doc, so, "v_pos", (double)sh->v_pos);
            yyjson_mut_obj_add_real(doc, so, "h_start_pos", (double)sh->h_start_pos);
            yyjson_mut_obj_add_real(doc, so, "v_start_pos", (double)sh->v_start_pos);
            yyjson_mut_obj_add_real(doc, so, "v_size", (double)sh->v_size);
            yyjson_mut_obj_add_uint(doc, so, "color", sh->color);
            yyjson_mut_obj_add_bool(doc, so, "color_defaulted",
                                    iface_is_sectioned(idata) &&
                                    !iface_color_is_present(idata));
            yyjson_mut_obj_add_bool(doc, so, "has_snapshot", sh->has_snapshot);
            if (sh->has_snapshot) {
                yyjson_mut_val *snap = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, snap, "width", sh->snapshot_desc.width);
                yyjson_mut_obj_add_uint(doc, snap, "height", sh->snapshot_desc.height);
                yyjson_mut_obj_add_uint(doc, snap, "size", (uint64_t)sh->snapshot_size);
                yyjson_mut_obj_add_val(doc, so, "snapshot", snap);
            }
            yyjson_mut_obj_add_val(doc, so, "body", iface_json_body(doc, &sh->body));
            yyjson_mut_obj_add_val(doc, data, "script", so);
        }

        /* Sub-behaviors */
        {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            for (size_t si = 0; si < idata->sub_count; si++) {
                const nmo_interface_behavior_t *sb = &idata->subs[si];
                yyjson_mut_val *so = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, so, "behavior_id", sb->behavior_id);
                yyjson_mut_obj_add_uint(doc, so, "flags", sb->flags);
                yyjson_mut_obj_add_uint(doc, so, "depth", sb->depth);
                yyjson_mut_obj_add_real(doc, so, "h_pos", (double)sb->h_pos);
                yyjson_mut_obj_add_real(doc, so, "v_pos", (double)sb->v_pos);
                yyjson_mut_obj_add_real(doc, so, "h_size", (double)sb->h_size);
                yyjson_mut_obj_add_real(doc, so, "v_size", (double)sb->v_size);
                yyjson_mut_obj_add_real(doc, so, "h_expand_size", (double)sb->h_expand_size);
                yyjson_mut_obj_add_real(doc, so, "v_expand_size", (double)sb->v_expand_size);
                yyjson_mut_obj_add_val(doc, so, "body", iface_json_body(doc, &sb->body));
                yyjson_mut_arr_add_val(arr, so);
            }
            yyjson_mut_obj_add_val(doc, data, "subs", arr);
        }

        /* Extra data */
        {
            const nmo_interface_extra_t *ex = &idata->extra;
            yyjson_mut_val *eo = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_bool(doc, eo, "present", ex->present);
            if (ex->present) {
                yyjson_mut_obj_add_uint(doc, eo, "version", ex->version);
                yyjson_mut_obj_add_uint(doc, eo, "entry_count", (uint64_t)ex->entry_count);
                yyjson_mut_val *arr = yyjson_mut_arr(doc);
                for (size_t ei = 0; ei < ex->entry_count; ei++) {
                    const nmo_interface_extra_entry_t *ee = &ex->entries[ei];
                    yyjson_mut_val *eobj = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_uint(doc, eobj, "type", ee->type);
                    yyjson_mut_obj_add_uint(doc, eobj, "id1", ee->id1);
                    if (ee->type == 3)
                        yyjson_mut_obj_add_uint(doc, eobj, "id2", ee->id2);
                    if (ee->type == 4)
                        yyjson_mut_obj_add_int(doc, eobj, "value", ee->value);
                    if (ee->sub_count > 0) {
                        yyjson_mut_val *sub_arr = yyjson_mut_arr(doc);
                        for (size_t si = 0; si < ee->sub_count; si++) {
                            const nmo_interface_extra_sub_t *se = &ee->sub_entries[si];
                            yyjson_mut_val *sobj = yyjson_mut_obj(doc);
                            yyjson_mut_obj_add_int(doc, sobj, "value1", se->value1);
                            yyjson_mut_obj_add_int(doc, sobj, "value2", se->value2);
                            yyjson_mut_obj_add_uint(doc, sobj, "id1", se->id1);
                            yyjson_mut_obj_add_uint(doc, sobj, "id2", se->id2);
                            if (se->data_size > 0)
                                yyjson_mut_obj_add_uint(doc, sobj, "data_size", (uint64_t)se->data_size);
                            yyjson_mut_arr_add_val(sub_arr, sobj);
                        }
                        yyjson_mut_obj_add_val(doc, eobj, "sub_entries", sub_arr);
                    }
                    yyjson_mut_arr_add_val(arr, eobj);
                }
                yyjson_mut_obj_add_val(doc, eo, "entries", arr);
            }
            yyjson_mut_obj_add_val(doc, data, "extra", eo);
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "behavior.interface");
        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS)
                         : NMO_CLI_EXIT_SUCCESS;
    }

    /* --- Brief output --- */
    if (args->brief) {
        char buf[128];
        char heading[256];
        snprintf(heading, sizeof(heading), "Interface: Behavior #%u %s",
                 target_id, (name && name[0]) ? name : "(unnamed)");
        nmo_cli_print_heading(c.out, heading, c.colorize);

        snprintf(buf, sizeof(buf), "0x%02X", idata->version);
        nmo_cli_print_kv(c.out, "Version", buf, 22, c.colorize);

        nmo_cli_print_kv(c.out, "Layout",
                         iface_is_sectioned(idata) ? "sectioned" : "inline",
                         22, c.colorize);

        nmo_cli_print_kv(c.out, "Root kind", iface_root_kind_name(idata),
                         22, c.colorize);

        if (idata->script.color) {
            snprintf(buf, sizeof(buf), "0x%08X", idata->script.color);
            nmo_cli_print_kv(c.out, "Color", buf, 22, c.colorize);
        }

        snprintf(buf, sizeof(buf), "%zu", idata->sub_count);
        nmo_cli_print_kv(c.out, "Sub-behaviors", buf, 22, c.colorize);

        /* Count totals across all bodies */
        size_t total_links = idata->script.body.link_count;
        size_t total_routing = 0;
        size_t total_ops = idata->script.body.operation_count;
        size_t total_comments = idata->script.body.comment_count;
        size_t total_local = 0, total_shared = 0;
        size_t folded_count = 0;

        for (size_t li = 0; li < idata->script.body.link_count; li++)
            total_routing += idata->script.body.links[li].point_count;
        if (idata->script.body.has_params) {
            total_local += idata->script.body.params.local_count;
            total_shared += idata->script.body.params.shared_count;
        }
        if (idata->script.flags & NMO_INTERFACE_FLAG_FOLDED)
            folded_count++;

        for (size_t si = 0; si < idata->sub_count; si++) {
            const nmo_interface_behavior_t *sb = &idata->subs[si];
            total_links += sb->body.link_count;
            total_ops += sb->body.operation_count;
            total_comments += sb->body.comment_count;
            for (size_t li = 0; li < sb->body.link_count; li++)
                total_routing += sb->body.links[li].point_count;
            if (sb->body.has_params) {
                total_local += sb->body.params.local_count;
                total_shared += sb->body.params.shared_count;
            }
            if (sb->flags & NMO_INTERFACE_FLAG_FOLDED)
                folded_count++;
        }

        snprintf(buf, sizeof(buf), "%zu", total_links);
        nmo_cli_print_kv(c.out, "Total links", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "%zu", total_routing);
        nmo_cli_print_kv(c.out, "Routing points", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "%zu", total_ops);
        nmo_cli_print_kv(c.out, "Operations", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "%zu", total_comments);
        nmo_cli_print_kv(c.out, "Comments", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "%zu local + %zu shared", total_local, total_shared);
        nmo_cli_print_kv(c.out, "Params", buf, 22, c.colorize);

        if (idata->script.has_snapshot) {
            snprintf(buf, sizeof(buf), "%ux%u (%zu bytes)",
                     idata->script.snapshot_desc.width,
                     idata->script.snapshot_desc.height,
                     idata->script.snapshot_size);
            nmo_cli_print_kv(c.out, "Snapshot", buf, 22, c.colorize);
        } else {
            nmo_cli_print_kv(c.out, "Snapshot", "(none)", 22, c.colorize);
        }

        if (idata->extra.present) {
            snprintf(buf, sizeof(buf), "v%u, %zu entries",
                     idata->extra.version, idata->extra.entry_count);
        } else {
            snprintf(buf, sizeof(buf), "(none)");
        }
        nmo_cli_print_kv(c.out, "Extra data", buf, 22, c.colorize);

        snprintf(buf, sizeof(buf), "%zu", folded_count);
        nmo_cli_print_kv(c.out, "Folded", buf, 22, c.colorize);

        return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS)
                         : NMO_CLI_EXIT_SUCCESS;
    }

    /* --- Full text output --- */
    {
        char heading[256];
        snprintf(heading, sizeof(heading), "Interface: Behavior #%u %s",
                 target_id, (name && name[0]) ? name : "(unnamed)");
        nmo_cli_print_heading(c.out, heading, c.colorize);
    }

    /* Header */
    fprintf(c.out, "  version: 0x%02X  layout: %s  root: %s\n",
            idata->version,
            iface_is_sectioned(idata) ? "sectioned" : "inline",
            iface_root_kind_name(idata));

    /* Script header */
    {
        const nmo_interface_script_header_t *sh = &idata->script;
        nmo_cli_print_heading(c.out, "Script Header", c.colorize);
        fprintf(c.out, "  behavior_id: %u  flags: 0x%X", sh->behavior_id, sh->flags);
        if (sh->flags & NMO_INTERFACE_FLAG_FOLDED) fprintf(c.out, " [folded]");
        if (sh->flags & NMO_INTERFACE_FLAG_HEADER_ONLY) fprintf(c.out, " [header-only]");
        fprintf(c.out, "\n");
        fprintf(c.out, "  pos: (%.1f, %.1f)  start: (%.1f, %.1f)  v_size: %.1f\n",
                sh->h_pos, sh->v_pos, sh->h_start_pos, sh->v_start_pos, sh->v_size);
        fprintf(c.out, "  script_index: %u\n", sh->script_index);
        if (sh->has_snapshot) {
            fprintf(c.out, "  snapshot: %ux%u (%zu bytes)\n",
                    sh->snapshot_desc.width, sh->snapshot_desc.height, sh->snapshot_size);
        }
        if (sh->color) {
            fprintf(c.out, "  color: 0x%08X\n", sh->color);
        }

        iface_print_body_text(c.out, &sh->body, "Script", c.colorize);
    }

    /* Sub-behaviors */
    if (idata->sub_count > 0) {
        char heading[128];
        snprintf(heading, sizeof(heading), "Sub-behaviors (%zu)", idata->sub_count);
        nmo_cli_print_heading(c.out, heading, c.colorize);
        for (size_t si = 0; si < idata->sub_count; si++) {
            const nmo_interface_behavior_t *sb = &idata->subs[si];
            fprintf(c.out, "  [%zu] id=%u depth=%u flags=0x%X",
                    si, sb->behavior_id, sb->depth, sb->flags);
            if (sb->flags & NMO_INTERFACE_FLAG_FOLDED) fprintf(c.out, " [folded]");
            if (sb->flags & NMO_INTERFACE_FLAG_HEADER_ONLY) fprintf(c.out, " [header-only]");
            fprintf(c.out, "\n");
            fprintf(c.out, "      pos=(%.1f,%.1f) size=(%.1f,%.1f) expand=(%.1f,%.1f)\n",
                    sb->h_pos, sb->v_pos,
                    sb->h_size, sb->v_size,
                    sb->h_expand_size, sb->v_expand_size);

            char label[64];
            snprintf(label, sizeof(label), "Sub[%zu]", si);
            iface_print_body_text(c.out, &sb->body, label, c.colorize);
        }
    }

    /* Extra data */
    if (idata->extra.present) {
        char heading[128];
        snprintf(heading, sizeof(heading), "Extra Data (v%u, %zu entries)",
                 idata->extra.version, idata->extra.entry_count);
        nmo_cli_print_heading(c.out, heading, c.colorize);
        for (size_t ei = 0; ei < idata->extra.entry_count; ei++) {
            const nmo_interface_extra_entry_t *ee = &idata->extra.entries[ei];
            fprintf(c.out, "  [%zu] type=%u id1=%u", ei, ee->type, ee->id1);
            if (ee->type == 3) fprintf(c.out, " id2=%u", ee->id2);
            if (ee->type == 4) fprintf(c.out, " value=%d", ee->value);
            if (ee->sub_count > 0) fprintf(c.out, " sub_entries=%zu", ee->sub_count);
            fprintf(c.out, "\n");
            for (size_t si = 0; si < ee->sub_count; si++) {
                const nmo_interface_extra_sub_t *se = &ee->sub_entries[si];
                fprintf(c.out, "    val1=%d val2=%d id1=%u id2=%u",
                        se->value1, se->value2, se->id1, se->id2);
                if (se->data_size > 0)
                    fprintf(c.out, " data=%zu bytes", se->data_size);
                fprintf(c.out, "\n");
            }
        }
    }

    /* Section presence (only relevant for sectioned layout) */
    if (iface_is_sectioned(idata)) {
        nmo_cli_print_heading(c.out, "Section Presence", c.colorize);
        const nmo_interface_body_t *sb = &idata->script.body;
        fprintf(c.out, "  Script: links=%s ops=%s comments=%s unknown_flag=%s\n",
                sb->has_links_section ? "yes" : "no",
                sb->has_operations_section ? "yes" : "no",
                sb->has_comments_section ? "yes" : "no",
                sb->has_unknown_flag_section ? "yes" : "no");
        for (size_t si = 0; si < idata->sub_count; si++) {
            const nmo_interface_body_t *b = &idata->subs[si].body;
            fprintf(c.out, "  Sub[%zu]: links=%s ops=%s comments=%s unknown_flag=%s\n",
                    si,
                    b->has_links_section ? "yes" : "no",
                    b->has_operations_section ? "yes" : "no",
                    b->has_comments_section ? "yes" : "no",
                    b->has_unknown_flag_section ? "yes" : "no");
        }
    }

    return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
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

