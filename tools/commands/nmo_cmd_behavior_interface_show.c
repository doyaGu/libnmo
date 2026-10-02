/**
 * @file nmo_cmd_behavior_interface_show.c
 * @brief nmo behavior interface show: the editor layout of a script as records.
 */

#include "nmo_cmd_behavior_interface_internal.h"

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
