/**
 * @file nmo_cmd_behavior_interface.c
 * @brief CLI behavior interface sub-action implementations
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
#include "core/nmo_array.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_interface_edit.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_system.h"
#include "app/nmo_save.h"

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
                yyjson_mut_obj_add_str(doc, co, "text", cm->text);
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
        return NULL;
    }
    if (out_obj) *out_obj = beh;
    return bs->interface_data;
}

static int iface_edit_save(nmo_cmd_ctx_t *c, const char *output_path)
{
    nmo_save_options_t save_opts = nmo_save_options_default();
    int save_rc = nmo_save_file(c->session, output_path, &save_opts);
    if (save_rc != NMO_OK) {
        fprintf(stderr, "Error saving file: %s\n", nmo_error_string(save_rc));
        return NMO_CLI_EXIT_IO_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
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

/* ================================================================
 * Interface edit: verb handlers (now public sub-action handlers)
 * ================================================================ */

int nmo_cmd_behavior_iface_set_pos(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o", NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 5) {
        fprintf(stderr, "Usage: nmo behavior interface set-pos <id> <beh_id> <h> <v> <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id, beh_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!nmo_tool_parse_u32(r.pos_args[1], &beh_id)) {
        fprintf(stderr, "Error: Invalid behavior ID '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char *endp;
    float h = strtof(r.pos_args[2], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[2]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float v = strtof(r.pos_args[3], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[3]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    if (!iface_validate_behavior_id(&c, beh_id)) {
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if (idata->script.behavior_id == beh_id) {
        idata->script.h_pos = h;
        idata->script.v_pos = v;
    } else {
        nmo_interface_behavior_t *sub = nmo_interface_find_sub(idata, beh_id);
        if (!sub) {
            fprintf(stderr, "Error: Behavior %u not found in interface data\n", beh_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        sub->h_pos = h;
        sub->v_pos = v;
    }

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Moved behavior %u to (%.1f, %.1f)\n", beh_id, (double)h, (double)v);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

static int iface_cmd_fold_impl(int argc, char **argv, const nmo_cli_global_opts_t *global, bool fold) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o", NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 3) {
        fprintf(stderr, "Usage: nmo behavior interface %s <id> <beh_id> <file> -o <out>\n",
                fold ? "fold" : "unfold");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id, beh_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!nmo_tool_parse_u32(r.pos_args[1], &beh_id)) {
        fprintf(stderr, "Error: Invalid behavior ID '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    if (!iface_validate_behavior_id(&c, beh_id)) {
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    uint32_t *flags = NULL;
    if (idata->script.behavior_id == beh_id) {
        flags = &idata->script.flags;
    } else {
        nmo_interface_behavior_t *sub = nmo_interface_find_sub(idata, beh_id);
        if (!sub) {
            fprintf(stderr, "Error: Behavior %u not found in interface data\n", beh_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        flags = &sub->flags;
    }

    if (fold) {
        *flags |= NMO_INTERFACE_FLAG_FOLDED;
    } else {
        *flags &= ~NMO_INTERFACE_FLAG_FOLDED;
    }

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "%s behavior %u\n", fold ? "Folded" : "Unfolded", beh_id);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
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
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 3) {
        fprintf(stderr, "Usage: nmo behavior interface set-color <id> <color> <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *color_str = r.pos_args[1];
    char *endp;
    unsigned long color_val = strtoul(color_str, &endp, 16);
    if (*endp != '\0' || color_val > 0xFFFFFF) {
        fprintf(stderr, "Error: Invalid color '%s' (expected RRGGBB or 0xRRGGBB)\n", color_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    idata->script.color = (uint32_t)color_val;

    if (idata->version < 0x14 || idata->sectioned_layout) {
        fprintf(stderr, "Warning: color will not be written "
                "(version 0x%02X%s)\n",
                idata->version,
                idata->sectioned_layout ? ", sectioned layout" : " < 0x14");
    }

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Set script color to #%06X\n", (unsigned)color_val);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

int nmo_cmd_behavior_iface_add_comment(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",    NMO_OPT_STRING, "Output file path"},
        {"--body",   NULL,    NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--text",   "-t",    NMO_OPT_STRING, "Comment text"},
        {"--rect",   "-r",    NMO_OPT_STRING, "Rectangle L,T,R,B"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_TEXT, OPT_RECT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_TEXT].present) {
        fprintf(stderr, "Error: --text required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_RECT].present) {
        fprintf(stderr, "Error: --rect required (L,T,R,B)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 2) {
        fprintf(stderr, "Usage: nmo behavior interface add-comment <id> [--body <beh_id>] "
                "--text \"...\" --rect L,T,R,B <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    float left, top, right, bottom;
    if (sscanf(vals[OPT_RECT].val.str, "%f,%f,%f,%f", &left, &top, &right, &bottom) != 4) {
        fprintf(stderr, "Error: Invalid --rect format '%s', expected L,T,R,B\n",
                vals[OPT_RECT].val.str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_object_t *beh_obj = NULL;
    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, &beh_obj);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    uint32_t body_id = idata->script.behavior_id;
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        if (!iface_validate_behavior_id(&c, body_id)) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    nmo_interface_body_t *body = nmo_interface_find_body(idata, body_id);
    if (!body) {
        fprintf(stderr, "Error: Behavior %u has no body (not found or header-only)\n", body_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_arena_t *arena = nmo_object_get_storage_arena(beh_obj);
    size_t idx = 0;
    nmo_status_t st = nmo_interface_body_add_comment(
        body, arena, vals[OPT_TEXT].val.str, left, top, right, bottom, 0, &idx);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: %s\n", nmo_error_string(st));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Added comment at index %zu to behavior %u\n", idx, body_id);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

int nmo_cmd_behavior_iface_remove_comment(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",    NMO_OPT_STRING, "Output file path"},
        {"--body",   NULL,    NMO_OPT_STRING, "Target behavior ID (default: script)"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 3) {
        fprintf(stderr, "Usage: nmo behavior interface remove-comment <id> <index> "
                "[--body <beh_id>] <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t index_val;
    if (!nmo_tool_parse_u32(r.pos_args[1], &index_val)) {
        fprintf(stderr, "Error: Invalid index '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    uint32_t body_id = idata->script.behavior_id;
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        if (!iface_validate_behavior_id(&c, body_id)) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    nmo_interface_body_t *body = nmo_interface_find_body(idata, body_id);
    if (!body) {
        fprintf(stderr, "Error: Behavior %u has no body (not found or header-only)\n", body_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if ((size_t)index_val >= body->comment_count) {
        fprintf(stderr, "Error: Comment index %u out of range (count=%zu)\n",
                index_val, body->comment_count);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_status_t st = nmo_interface_body_remove_comment(body, (size_t)index_val);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: %s\n", nmo_error_string(st));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Removed comment %u from behavior %u\n", index_val, body_id);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ================================================================
 * Interface edit: comment operations
 * ================================================================ */

int nmo_cmd_behavior_iface_set_comment_text(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
        {"--body",   NULL,  NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--text",   "-t",  NMO_OPT_STRING, "New comment text"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_TEXT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_TEXT].present) {
        fprintf(stderr, "Error: --text required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 3) {
        fprintf(stderr, "Usage: nmo behavior interface set-comment-text <id> <index> "
                "[--body <beh_id>] --text \"...\" <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t index_val;
    if (!nmo_tool_parse_u32(r.pos_args[1], &index_val)) {
        fprintf(stderr, "Error: Invalid index '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_object_t *beh_obj = NULL;
    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, &beh_obj);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    uint32_t body_id = idata->script.behavior_id;
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        if (!iface_validate_behavior_id(&c, body_id)) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    nmo_interface_body_t *body = nmo_interface_find_body(idata, body_id);
    if (!body) {
        fprintf(stderr, "Error: Behavior %u has no body (not found or header-only)\n", body_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if ((size_t)index_val >= body->comment_count) {
        fprintf(stderr, "Error: Comment index %u out of range (count=%zu)\n",
                index_val, body->comment_count);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_arena_t *arena = nmo_object_get_storage_arena(beh_obj);
    nmo_status_t st = nmo_interface_body_set_comment_text(
        body, arena, (size_t)index_val, vals[OPT_TEXT].val.str);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: %s\n", nmo_error_string(st));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Set comment %u text in behavior %u\n", index_val, body_id);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

int nmo_cmd_behavior_iface_move_comment(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
        {"--body",   NULL,  NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--rect",   "-r",  NMO_OPT_STRING, "Rectangle L,T,R,B"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_RECT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_RECT].present) {
        fprintf(stderr, "Error: --rect required (L,T,R,B)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 3) {
        fprintf(stderr, "Usage: nmo behavior interface move-comment <id> <index> "
                "[--body <beh_id>] --rect L,T,R,B <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t index_val;
    if (!nmo_tool_parse_u32(r.pos_args[1], &index_val)) {
        fprintf(stderr, "Error: Invalid index '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    float left, top, right, bottom;
    if (sscanf(vals[OPT_RECT].val.str, "%f,%f,%f,%f", &left, &top, &right, &bottom) != 4) {
        fprintf(stderr, "Error: Invalid --rect format '%s', expected L,T,R,B\n",
                vals[OPT_RECT].val.str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    uint32_t body_id = idata->script.behavior_id;
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        if (!iface_validate_behavior_id(&c, body_id)) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    nmo_interface_body_t *body = nmo_interface_find_body(idata, body_id);
    if (!body) {
        fprintf(stderr, "Error: Behavior %u has no body (not found or header-only)\n", body_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if ((size_t)index_val >= body->comment_count) {
        fprintf(stderr, "Error: Comment index %u out of range (count=%zu)\n",
                index_val, body->comment_count);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    body->comments[index_val].left = left;
    body->comments[index_val].top = top;
    body->comments[index_val].right = right;
    body->comments[index_val].bottom = bottom;

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Moved comment %u to (%.0f,%.0f,%.0f,%.0f) in behavior %u\n",
            index_val, (double)left, (double)top, (double)right, (double)bottom, body_id);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

int nmo_cmd_behavior_iface_set_comment_style(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",    NMO_OPT_STRING, "Output file path"},
        {"--body",   NULL,    NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--style",  "-s",    NMO_OPT_STRING, "Style flags value"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_STYLE, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_STYLE].present) {
        fprintf(stderr, "Error: --style required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 3) {
        fprintf(stderr, "Usage: nmo behavior interface set-comment-style <id> <index> "
                "[--body <beh_id>] --style <flags> <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t index_val;
    if (!nmo_tool_parse_u32(r.pos_args[1], &index_val)) {
        fprintf(stderr, "Error: Invalid index '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t style;
    if (!nmo_tool_parse_u32(vals[OPT_STYLE].val.str, &style)) {
        fprintf(stderr, "Error: Invalid --style value '%s'\n", vals[OPT_STYLE].val.str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    if (idata->version < 0x16) {
        fprintf(stderr, "Warning: comment style_flags not written for version 0x%02X (requires >= 0x16)\n",
                idata->version);
    }

    uint32_t body_id = idata->script.behavior_id;
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        if (!iface_validate_behavior_id(&c, body_id)) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    nmo_interface_body_t *body = nmo_interface_find_body(idata, body_id);
    if (!body) {
        fprintf(stderr, "Error: Behavior %u has no body (not found or header-only)\n", body_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if ((size_t)index_val >= body->comment_count) {
        fprintf(stderr, "Error: Comment index %u out of range (count=%zu)\n",
                index_val, body->comment_count);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    body->comments[index_val].style_flags = style;

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Set comment %u style to 0x%X in behavior %u\n", index_val, style, body_id);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ================================================================
 * Interface edit: link operations
 * ================================================================ */

int nmo_cmd_behavior_iface_add_point(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 5) {
        fprintf(stderr, "Usage: nmo behavior interface add-point <id> <link_id> <h> <v> <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t link_id;
    if (!nmo_tool_parse_u32(r.pos_args[1], &link_id)) {
        fprintf(stderr, "Error: Invalid link ID '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char *endp;
    float h = strtof(r.pos_args[2], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[2]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float v = strtof(r.pos_args[3], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[3]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_object_t *beh_obj = NULL;
    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, &beh_obj);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    nmo_interface_link_t *link = nmo_interface_find_link(idata, link_id);
    if (!link) {
        fprintf(stderr, "Error: Link %u not found in interface data\n", link_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_arena_t *arena = nmo_object_get_storage_arena(beh_obj);
    nmo_status_t st = nmo_interface_link_add_point(link, arena, h, v);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: %s\n", nmo_error_string(st));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Added point (%.1f, %.1f) to link %u\n", (double)h, (double)v, link_id);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

int nmo_cmd_behavior_iface_clear_points(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 3) {
        fprintf(stderr, "Usage: nmo behavior interface clear-points <id> <link_id> <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t link_id;
    if (!nmo_tool_parse_u32(r.pos_args[1], &link_id)) {
        fprintf(stderr, "Error: Invalid link ID '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    nmo_interface_link_t *link = nmo_interface_find_link(idata, link_id);
    if (!link) {
        fprintf(stderr, "Error: Link %u not found in interface data\n", link_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_interface_link_clear_points(link);

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Cleared all routing points from link %u\n", link_id);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

int nmo_cmd_behavior_iface_remove_point(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 4) {
        fprintf(stderr, "Usage: nmo behavior interface remove-point <id> <link_id> <point_index> <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t link_id;
    if (!nmo_tool_parse_u32(r.pos_args[1], &link_id)) {
        fprintf(stderr, "Error: Invalid link ID '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t point_index;
    if (!nmo_tool_parse_u32(r.pos_args[2], &point_index)) {
        fprintf(stderr, "Error: Invalid point index '%s'\n", r.pos_args[2]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    nmo_interface_link_t *link = nmo_interface_find_link(idata, link_id);
    if (!link) {
        fprintf(stderr, "Error: Link %u not found in interface data\n", link_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_status_t st = nmo_interface_link_remove_point(link, (size_t)point_index);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: %s\n", nmo_error_string(st));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Removed point %u from link %u\n", point_index, link_id);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

int nmo_cmd_behavior_iface_move_point(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 6) {
        fprintf(stderr, "Usage: nmo behavior interface move-point <id> <link_id> <point_index> <h> <v> <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t link_id;
    if (!nmo_tool_parse_u32(r.pos_args[1], &link_id)) {
        fprintf(stderr, "Error: Invalid link ID '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t point_index;
    if (!nmo_tool_parse_u32(r.pos_args[2], &point_index)) {
        fprintf(stderr, "Error: Invalid point index '%s'\n", r.pos_args[2]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char *endp;
    float h = strtof(r.pos_args[3], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[3]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float v = strtof(r.pos_args[4], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[4]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    nmo_interface_link_t *link = nmo_interface_find_link(idata, link_id);
    if (!link) {
        fprintf(stderr, "Error: Link %u not found in interface data\n", link_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if ((size_t)point_index >= link->point_count) {
        fprintf(stderr, "Error: Point index %u out of range (count=%zu)\n",
                point_index, link->point_count);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    link->points[point_index * 2] = h;
    link->points[point_index * 2 + 1] = v;

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Moved point %u of link %u to (%.1f, %.1f)\n",
            point_index, link_id, (double)h, (double)v);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

int nmo_cmd_behavior_iface_set_link_highlight(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 4) {
        fprintf(stderr, "Usage: nmo behavior interface set-link-highlight <id> <link_id> on|off <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t link_id;
    if (!nmo_tool_parse_u32(r.pos_args[1], &link_id)) {
        fprintf(stderr, "Error: Invalid link ID '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    bool highlight;
    if (strcmp(r.pos_args[2], "on") == 0) {
        highlight = true;
    } else if (strcmp(r.pos_args[2], "off") == 0) {
        highlight = false;
    } else {
        fprintf(stderr, "Error: Expected 'on' or 'off', got '%s'\n", r.pos_args[2]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    nmo_interface_link_t *link = nmo_interface_find_link(idata, link_id);
    if (!link) {
        fprintf(stderr, "Error: Link %u not found in interface data\n", link_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    link->highlight = highlight;

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Set link %u highlight %s\n", link_id, highlight ? "on" : "off");
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ================================================================
 * Interface edit: operation
 * ================================================================ */

int nmo_cmd_behavior_iface_move_op(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 5) {
        fprintf(stderr, "Usage: nmo behavior interface move-op <id> <op_id> <h> <v> <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t op_id;
    if (!nmo_tool_parse_u32(r.pos_args[1], &op_id)) {
        fprintf(stderr, "Error: Invalid operation ID '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char *endp;
    float h = strtof(r.pos_args[2], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[2]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float v = strtof(r.pos_args[3], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[3]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    nmo_interface_operation_t *op = nmo_interface_find_operation(idata, op_id);
    if (!op) {
        fprintf(stderr, "Error: Operation %u not found in interface data\n", op_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    op->h_pos = h;
    op->v_pos = v;

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Moved operation %u to (%.1f, %.1f)\n", op_id, (double)h, (double)v);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
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
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_PARAM_INDEX, OPT_SHARED, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_PARAM_INDEX].present) {
        fprintf(stderr, "Error: --param-index required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 4) {
        fprintf(stderr, "Usage: nmo behavior interface move-param <id> <h> <v> <file> "
                "--param-index <N> [--shared] -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char *endp;
    long h_long = strtol(r.pos_args[1], &endp, 10);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid integer '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    long v_long = strtol(r.pos_args[2], &endp, 10);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid integer '%s'\n", r.pos_args[2]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    int32_t h = (int32_t)h_long;
    int32_t v = (int32_t)v_long;

    uint32_t param_index = vals[OPT_PARAM_INDEX].val.u;
    bool shared = vals[OPT_SHARED].present && vals[OPT_SHARED].val.flag;

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    uint32_t body_id = idata->script.behavior_id;
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        if (!iface_validate_behavior_id(&c, body_id)) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    nmo_interface_body_t *body = nmo_interface_find_body(idata, body_id);
    if (!body) {
        fprintf(stderr, "Error: Behavior %u has no body (not found or header-only)\n", body_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if (!body->has_params) {
        fprintf(stderr, "Error: Behavior %u has no parameter data\n", body_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_interface_param_t *params;
    size_t count;
    if (shared) {
        params = body->params.shared;
        count = body->params.shared_count;
    } else {
        params = body->params.locals;
        count = body->params.local_count;
    }

    if ((size_t)param_index >= count) {
        fprintf(stderr, "Error: Parameter index %u out of range (%s count=%zu)\n",
                param_index, shared ? "shared" : "local", count);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    params[param_index].h_pos = h;
    params[param_index].v_pos = v;

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Moved %s param %u to (%d, %d) in behavior %u\n",
            shared ? "shared" : "local", param_index, h, v, body_id);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

int nmo_cmd_behavior_iface_set_param_style(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output",      "-o",  NMO_OPT_STRING, "Output file path"},
        {"--body",        NULL,  NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--param-index", NULL,  NMO_OPT_UINT,   "Parameter index"},
        {"--shared",      NULL,  NMO_OPT_FLAG,   "Target shared params instead of local"},
        {"--style",       "-s",  NMO_OPT_STRING, "Style value"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_PARAM_INDEX, OPT_SHARED, OPT_STYLE, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_PARAM_INDEX].present) {
        fprintf(stderr, "Error: --param-index required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_STYLE].present) {
        fprintf(stderr, "Error: --style required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 2) {
        fprintf(stderr, "Usage: nmo behavior interface set-param-style <id> <file> "
                "--param-index <N> --style <val> [--shared] -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t style;
    if (!nmo_tool_parse_u32(vals[OPT_STYLE].val.str, &style)) {
        fprintf(stderr, "Error: Invalid --style value '%s'\n", vals[OPT_STYLE].val.str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t param_index = vals[OPT_PARAM_INDEX].val.u;
    bool shared = vals[OPT_SHARED].present && vals[OPT_SHARED].val.flag;

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    uint32_t body_id = idata->script.behavior_id;
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        if (!iface_validate_behavior_id(&c, body_id)) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    nmo_interface_body_t *body = nmo_interface_find_body(idata, body_id);
    if (!body) {
        fprintf(stderr, "Error: Behavior %u has no body (not found or header-only)\n", body_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if (!body->has_params) {
        fprintf(stderr, "Error: Behavior %u has no parameter data\n", body_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_interface_param_t *params;
    size_t count;
    if (shared) {
        params = body->params.shared;
        count = body->params.shared_count;
    } else {
        params = body->params.locals;
        count = body->params.local_count;
    }

    if ((size_t)param_index >= count) {
        fprintf(stderr, "Error: Parameter index %u out of range (%s count=%zu)\n",
                param_index, shared ? "shared" : "local", count);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    params[param_index].style = style;

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Set %s param %u style to 0x%X in behavior %u\n",
            shared ? "shared" : "local", param_index, style, body_id);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ================================================================
 * Interface edit: sub-behavior size
 * ================================================================ */

int nmo_cmd_behavior_iface_resize(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 5) {
        fprintf(stderr, "Usage: nmo behavior interface resize <id> <beh_id> <w> <h> <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t beh_id;
    if (!nmo_tool_parse_u32(r.pos_args[1], &beh_id)) {
        fprintf(stderr, "Error: Invalid behavior ID '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char *endp;
    float w = strtof(r.pos_args[2], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[2]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float h = strtof(r.pos_args[3], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[3]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    if (!iface_validate_behavior_id(&c, beh_id)) {
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if (beh_id == idata->script.behavior_id) {
        fprintf(stderr, "Error: Cannot resize script behavior (no size fields)\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_interface_behavior_t *sub = nmo_interface_find_sub(idata, beh_id);
    if (!sub) {
        fprintf(stderr, "Error: Behavior %u not found in interface data\n", beh_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    sub->h_size = w;
    sub->v_size = h;

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Resized behavior %u to (%.1f, %.1f)\n", beh_id, (double)w, (double)h);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

int nmo_cmd_behavior_iface_set_expand(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 5) {
        fprintf(stderr, "Usage: nmo behavior interface set-expand <id> <beh_id> <w> <h> <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    uint32_t beh_id;
    if (!nmo_tool_parse_u32(r.pos_args[1], &beh_id)) {
        fprintf(stderr, "Error: Invalid behavior ID '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char *endp;
    float w = strtof(r.pos_args[2], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[2]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float h = strtof(r.pos_args[3], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[3]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    if (!iface_validate_behavior_id(&c, beh_id)) {
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if (beh_id == idata->script.behavior_id) {
        fprintf(stderr, "Error: Cannot set expand size for script behavior (no size fields)\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_interface_behavior_t *sub = nmo_interface_find_sub(idata, beh_id);
    if (!sub) {
        fprintf(stderr, "Error: Behavior %u not found in interface data\n", beh_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    sub->h_expand_size = w;
    sub->v_expand_size = h;

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Set expand size of behavior %u to (%.1f, %.1f)\n", beh_id, (double)w, (double)h);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ================================================================
 * Interface edit: script viewport
 * ================================================================ */

int nmo_cmd_behavior_iface_set_viewport(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output", "-o",  NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 5) {
        fprintf(stderr, "Usage: nmo behavior interface set-viewport <id> <h> <v> <height> <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char *endp;
    float h = strtof(r.pos_args[1], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float v = strtof(r.pos_args[2], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[2]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float height = strtof(r.pos_args[3], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[3]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    idata->script.h_start_pos = h;
    idata->script.v_start_pos = v;
    idata->script.v_size = height;

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Set viewport to (%.1f, %.1f) height=%.1f\n",
            (double)h, (double)v, (double)height);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ================================================================
 * Interface edit: graph IO
 * ================================================================ */

static size_t parse_int32_list(const char *str, int32_t *out, size_t max_count) {
    size_t count = 0;
    const char *s = str;
    while (*s && count < max_count) {
        char *endp;
        long val = strtol(s, &endp, 10);
        if (endp == s) break;
        out[count++] = (int32_t)val;
        if (*endp == ',') s = endp + 1;
        else break;
    }
    return count;
}

int nmo_cmd_behavior_iface_set_graph_io(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--output",  "-o",  NMO_OPT_STRING, "Output file path"},
        {"--body",    NULL,  NMO_OPT_STRING, "Target behavior ID (default: script)"},
        {"--in-in",   NULL,  NMO_OPT_STRING, "Inward input array (comma-separated ints)"},
        {"--in-out",  NULL,  NMO_OPT_STRING, "Inward output array (comma-separated ints)"},
        {"--out-in",  NULL,  NMO_OPT_STRING, "Outward input array (comma-separated ints)"},
        {"--out-out", NULL,  NMO_OPT_STRING, "Outward output array (comma-separated ints)"},
    };
    enum { OPT_OUTPUT, OPT_BODY, OPT_IN_IN, OPT_IN_OUT, OPT_OUT_IN, OPT_OUT_OUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!vals[OPT_IN_IN].present && !vals[OPT_IN_OUT].present &&
        !vals[OPT_OUT_IN].present && !vals[OPT_OUT_OUT].present) {
        fprintf(stderr, "Error: At least one of --in-in, --in-out, --out-in, --out-out required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 2) {
        fprintf(stderr, "Usage: nmo behavior interface set-graph-io <id> <file> "
                "[--body <beh_id>] [--in-in ...] [--in-out ...] [--out-in ...] [--out-out ...] -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_object_t *beh_obj = NULL;
    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, &beh_obj);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    uint32_t body_id = idata->script.behavior_id;
    if (vals[OPT_BODY].present) {
        if (!nmo_tool_parse_u32(vals[OPT_BODY].val.str, &body_id)) {
            fprintf(stderr, "Error: Invalid --body ID '%s'\n", vals[OPT_BODY].val.str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        if (!iface_validate_behavior_id(&c, body_id)) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    nmo_interface_body_t *body = nmo_interface_find_body(idata, body_id);
    if (!body) {
        fprintf(stderr, "Error: Behavior %u has no body (not found or header-only)\n", body_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_arena_t *arena = nmo_object_get_storage_arena(beh_obj);

    /* Ensure graph_io exists */
    if (!body->has_graph_io || !body->graph_io) {
        nmo_interface_graph_io_t *gio = (nmo_interface_graph_io_t *)nmo_arena_alloc(
            arena, sizeof(*gio), _Alignof(nmo_interface_graph_io_t));
        memset(gio, 0, sizeof(*gio));
        body->graph_io = gio;
        body->has_graph_io = true;
    }

    nmo_interface_graph_io_t *gio = body->graph_io;
    int32_t tmp[64];
    size_t n;
    int arrays_set = 0;
    nmo_status_t st;

    if (vals[OPT_IN_IN].present) {
        n = parse_int32_list(vals[OPT_IN_IN].val.str, tmp, 64);
        st = nmo_interface_graph_io_set_array(&gio->inward_inputs, &gio->inward_input_count, arena, tmp, n);
        if (st != NMO_OK) { fprintf(stderr, "Error: %s\n", nmo_error_string(st)); return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR); }
        arrays_set++;
    }
    if (vals[OPT_IN_OUT].present) {
        n = parse_int32_list(vals[OPT_IN_OUT].val.str, tmp, 64);
        st = nmo_interface_graph_io_set_array(&gio->inward_outputs, &gio->inward_output_count, arena, tmp, n);
        if (st != NMO_OK) { fprintf(stderr, "Error: %s\n", nmo_error_string(st)); return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR); }
        arrays_set++;
    }
    if (vals[OPT_OUT_IN].present) {
        n = parse_int32_list(vals[OPT_OUT_IN].val.str, tmp, 64);
        st = nmo_interface_graph_io_set_array(&gio->outward_inputs, &gio->outward_input_count, arena, tmp, n);
        if (st != NMO_OK) { fprintf(stderr, "Error: %s\n", nmo_error_string(st)); return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR); }
        arrays_set++;
    }
    if (vals[OPT_OUT_OUT].present) {
        n = parse_int32_list(vals[OPT_OUT_OUT].val.str, tmp, 64);
        st = nmo_interface_graph_io_set_array(&gio->outward_outputs, &gio->outward_output_count, arena, tmp, n);
        if (st != NMO_OK) { fprintf(stderr, "Error: %s\n", nmo_error_string(st)); return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR); }
        arrays_set++;
    }

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Set %d graph IO array(s) in behavior %u\n", arrays_set, body_id);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
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
    };
    enum { OPT_OUTPUT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos_arr[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: -o/--output required\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (r.pos_count < 4) {
        fprintf(stderr, "Usage: nmo behavior interface translate <id> <dx> <dy> <file> -o <out>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char *endp;
    float dx = strtof(r.pos_args[1], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[1]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    float dy = strtof(r.pos_args[2], &endp);
    if (*endp != '\0') {
        fprintf(stderr, "Error: Invalid float '%s'\n", r.pos_args[2]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *file_path = r.pos_args[r.pos_count - 1];

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
    if (rc) return rc;

    nmo_interface_data_t *idata = iface_edit_get_data(&c, target_id, NULL);
    if (!idata) return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);

    /* Translate script position */
    idata->script.h_pos += dx;
    idata->script.v_pos += dy;

    /* Translate script body content */
    translate_body(&idata->script.body, dx, dy);

    /* Translate all sub-behaviors */
    for (size_t si = 0; si < idata->sub_count; si++) {
        nmo_interface_behavior_t *sub = &idata->subs[si];
        sub->h_pos += dx;
        sub->v_pos += dy;
        translate_body(&sub->body, dx, dy);
    }

    rc = iface_edit_save(&c, output_path);
    if (rc != NMO_CLI_EXIT_SUCCESS) return nmo_cmd_ctx_done(&c, rc);

    fprintf(c.out, "Translated all positions by (%.1f, %.1f)\n", (double)dx, (double)dy);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ================================================================
 * Interface show (read-only, the default sub-action)
 * ================================================================ */

int nmo_cmd_behavior_iface_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--brief", "-b", NMO_OPT_FLAG, "Brief summary output"},
        {"--json",  "-j", NMO_OPT_FLAG, "JSON output"},
    };
    enum { OPT_BRIEF, OPT_JSON, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool brief = vals[OPT_BRIEF].present && vals[OPT_BRIEF].val.flag;

    if (r.pos_count < 2) {
        fprintf(stderr, "Usage: nmo behavior interface [--brief] [--json] <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t target_id;
    if (!nmo_tool_parse_u32(r.pos_args[0], &target_id)) {
        fprintf(stderr, "Error: Invalid ID '%s'\n", r.pos_args[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    nmo_object_t *beh = nmo_object_repository_find_by_id(repo, target_id);
    if (!beh) {
        fprintf(stderr, "Error: Object %u not found\n", target_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if (!is_behavior_class(c.registry, nmo_object_get_class_id(beh))) {
        fprintf(stderr, "Error: Object %u is not a CKBehavior\n", target_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const nmo_behavior_state_t *bs = (const nmo_behavior_state_t *)nmo_object_get_state(beh);
    if (!bs) {
        fprintf(stderr, "Error: No state for behavior %u\n", target_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const nmo_interface_data_t *idata = bs->interface_data;
    if (!idata) {
        fprintf(stderr, "Error: Behavior %u has no interface data\n", target_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
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
        yyjson_mut_obj_add_bool(doc, data, "sectioned_layout", idata->sectioned_layout);
        yyjson_mut_obj_add_uint(doc, data, "sub_count", (uint64_t)idata->sub_count);

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
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    /* --- Brief output --- */
    if (brief) {
        char buf[128];
        char heading[256];
        snprintf(heading, sizeof(heading), "Interface: Behavior #%u %s",
                 target_id, (name && name[0]) ? name : "(unnamed)");
        nmo_cli_print_heading(c.out, heading, c.colorize);

        snprintf(buf, sizeof(buf), "0x%02X", idata->version);
        nmo_cli_print_kv(c.out, "Version", buf, 22, c.colorize);

        nmo_cli_print_kv(c.out, "Layout",
                         idata->sectioned_layout ? "sectioned" : "inline",
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

        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    /* --- Full text output --- */
    {
        char heading[256];
        snprintf(heading, sizeof(heading), "Interface: Behavior #%u %s",
                 target_id, (name && name[0]) ? name : "(unnamed)");
        nmo_cli_print_heading(c.out, heading, c.colorize);
    }

    /* Header */
    fprintf(c.out, "  version: 0x%02X  layout: %s\n",
            idata->version,
            idata->sectioned_layout ? "sectioned" : "inline");

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
    if (idata->sectioned_layout) {
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

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ================================================================
 * Sub-action table
 * ================================================================ */

const nmo_cli_action_t nmo_behavior_interface_sub_actions[] = {
    {"show",           NULL, "Show interface layout data",  nmo_cmd_behavior_iface_show,           NULL, NULL, 0, NULL},
    {"set-pos",        NULL, "Move behavior position",      nmo_cmd_behavior_iface_set_pos,        NULL, NULL, 0, NULL},
    {"fold",           NULL, "Fold behavior",               nmo_cmd_behavior_iface_fold,           NULL, NULL, 0, NULL},
    {"unfold",         NULL, "Unfold behavior",             nmo_cmd_behavior_iface_unfold,         NULL, NULL, 0, NULL},
    {"set-color",      NULL, "Set script color",            nmo_cmd_behavior_iface_set_color,      NULL, NULL, 0, NULL},
    {"add-comment",    NULL, "Add layout comment",          nmo_cmd_behavior_iface_add_comment,    NULL, NULL, 0, NULL},
    {"remove-comment",    NULL, "Remove layout comment",       nmo_cmd_behavior_iface_remove_comment,    NULL, NULL, 0, NULL},
    /* comment edits */
    {"set-comment-text",  NULL, "Set comment text",             nmo_cmd_behavior_iface_set_comment_text,  NULL, NULL, 0, NULL},
    {"move-comment",      NULL, "Move/resize comment",          nmo_cmd_behavior_iface_move_comment,      NULL, NULL, 0, NULL},
    {"set-comment-style", NULL, "Set comment style flags",      nmo_cmd_behavior_iface_set_comment_style, NULL, NULL, 0, NULL},
    /* link edits */
    {"add-point",         NULL, "Add link routing point",       nmo_cmd_behavior_iface_add_point,         NULL, NULL, 0, NULL},
    {"clear-points",      NULL, "Clear link routing points",    nmo_cmd_behavior_iface_clear_points,      NULL, NULL, 0, NULL},
    {"remove-point",      NULL, "Remove link routing point",    nmo_cmd_behavior_iface_remove_point,      NULL, NULL, 0, NULL},
    {"move-point",        NULL, "Move link routing point",      nmo_cmd_behavior_iface_move_point,        NULL, NULL, 0, NULL},
    {"set-link-highlight",NULL, "Toggle link highlight",        nmo_cmd_behavior_iface_set_link_highlight,NULL, NULL, 0, NULL},
    /* element edits */
    {"move-op",           NULL, "Move operation position",      nmo_cmd_behavior_iface_move_op,           NULL, NULL, 0, NULL},
    {"move-param",        NULL, "Move parameter grid position", nmo_cmd_behavior_iface_move_param,        NULL, NULL, 0, NULL},
    {"set-param-style",   NULL, "Set parameter style",          nmo_cmd_behavior_iface_set_param_style,   NULL, NULL, 0, NULL},
    /* structure edits */
    {"resize",            NULL, "Resize sub-behavior",          nmo_cmd_behavior_iface_resize,            NULL, NULL, 0, NULL},
    {"set-expand",        NULL, "Set expand size",              nmo_cmd_behavior_iface_set_expand,        NULL, NULL, 0, NULL},
    {"set-viewport",      NULL, "Set editor viewport",          nmo_cmd_behavior_iface_set_viewport,      NULL, NULL, 0, NULL},
    {"set-graph-io",      NULL, "Set graph IO port ordering",   nmo_cmd_behavior_iface_set_graph_io,      NULL, NULL, 0, NULL},
    /* bulk */
    {"translate",         NULL, "Translate all positions",       nmo_cmd_behavior_iface_translate,         NULL, NULL, 0, NULL},
};

_Static_assert(
    sizeof(nmo_behavior_interface_sub_actions) / sizeof(nmo_behavior_interface_sub_actions[0])
        == NMO_BEHAVIOR_INTERFACE_SUB_ACTION_COUNT,
    "sub-action table size must match NMO_BEHAVIOR_INTERFACE_SUB_ACTION_COUNT");
