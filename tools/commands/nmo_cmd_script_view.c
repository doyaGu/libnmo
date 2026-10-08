/**
 * @file nmo_cmd_script_view.c
 * @brief nmo script view and nmo script xref: readable graphs and cross references
 */

#include "nmo_cmd_script.h"
#include "nmo_cmd_behavior_internal.h"

#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "behavior/nmo_script_index.h"
#include "behavior/nmo_script_model.h"
#include "object/nmo_class_ids.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Shared text
 * ============================================================================ */

/* "Root#id / Graph#id / ..." from the root down to `graph_id`. Heap string. */
static char *view_path_dup(const nmo_cmd_behavior_flow_ctx_t *f, nmo_object_id_t graph_id)
{
    nmo_object_id_t chain[64];
    size_t depth = 0;
    for (nmo_object_id_t cur = graph_id; cur != 0 && depth < 64;
         cur = nmo_cmd_behavior_parent_id(f, cur)) {
        chain[depth++] = cur;
    }
    char *path = nmo_tool_strdup_fmt("%s", "");
    for (size_t i = depth; path && i > 0; i--) {
        char *label = nmo_cmd_behavior_node_label_dup(f, chain[i - 1]);
        char *next = label ? nmo_tool_strdup_fmt("%s%s%s", path, i == depth ? "" : " / ", label)
                           : NULL;
        free(label);
        free(path);
        path = next;
    }
    return path;
}

static const char *view_value_kind_name(nmo_script_value_kind_t kind)
{
    switch (kind) {
    case NMO_SCRIPT_VALUE_SAVED:    return "saved";
    case NMO_SCRIPT_VALUE_COMPUTED: return "computed";
    case NMO_SCRIPT_VALUE_WRITTEN:  return "written";
    default:                        return "none";
    }
}

/* Why a use's key is unknown. */
static const char *view_unknown_key_text(const nmo_script_use_t *use)
{
    switch (use->key_value) {
    case NMO_SCRIPT_VALUE_COMPUTED: return "computed at run time";
    case NMO_SCRIPT_VALUE_WRITTEN:  return "written at run time";
    case NMO_SCRIPT_VALUE_SAVED:    return "saved empty";
    default:                        return "not connected";
    }
}

/*
 * What a use does: 'sends "Msg" to Dest#id', 'reads CurrentLevel#10703
 * [Points]', 'activates Script#id'. Heap string.
 */
static char *view_use_text_dup(const nmo_cmd_behavior_flow_ctx_t *f, const nmo_script_use_t *use)
{
    static const char *const verbs[] = {
        [NMO_SCRIPT_USE_MESSAGE_SEND] = "sends",
        [NMO_SCRIPT_USE_MESSAGE_WAIT] = "waits for",
        [NMO_SCRIPT_USE_MESSAGE_OTHER] = "uses message",
        [NMO_SCRIPT_USE_ARRAY_READ] = "reads",
        [NMO_SCRIPT_USE_ARRAY_WRITE] = "writes",
        [NMO_SCRIPT_USE_SCRIPT_ACTIVATE] = "activates",
        [NMO_SCRIPT_USE_SCRIPT_DEACTIVATE] = "deactivates",
    };
    const char *verb = verbs[use->kind];
    if (nmo_script_use_kind_is_message(use->kind)) {
        char *dest = use->dest_object_id ? nmo_cmd_behavior_node_label_dup(f, use->dest_object_id)
                                         : NULL;
        char *text = use->message
            ? nmo_tool_strdup_fmt("%s \"%s\"%s%s", verb, use->message,
                                  dest ? " to " : "", dest ? dest : "")
            : nmo_tool_strdup_fmt("%s a message %s", verb, view_unknown_key_text(use));
        free(dest);
        return text;
    }
    if (use->object_id == 0 && use->object_name) {
        return use->column_name || use->column < 0
            ? nmo_tool_strdup_fmt("%s \"%s\"%s%s%s (looked up by name)", verb, use->object_name,
                                  use->column_name ? " [" : "",
                                  use->column_name ? use->column_name : "",
                                  use->column_name ? "]" : "")
            : nmo_tool_strdup_fmt("%s \"%s\" [column %d] (looked up by name)", verb,
                                  use->object_name, (int)use->column);
    }
    if (use->object_id == 0) {
        return nmo_tool_strdup_fmt("%s %s %s", verb,
                                   nmo_script_use_kind_is_array(use->kind) ? "an array"
                                                                           : "a script",
                                   view_unknown_key_text(use));
    }
    char *object = nmo_cmd_behavior_node_label_dup(f, use->object_id);
    char *text = NULL;
    if (object && use->column_name) {
        text = nmo_tool_strdup_fmt("%s %s [%s]", verb, object, use->column_name);
    } else if (object && use->column >= 0) {
        text = nmo_tool_strdup_fmt("%s %s [column %d]", verb, object, (int)use->column);
    } else if (object) {
        text = nmo_tool_strdup_fmt("%s %s", verb, object);
    }
    free(object);
    return text;
}

/* JSON: one use. Text: "  <kind>  <label>  in <path>  <details>". */
static nmo_cli_record_t *view_use_record(const nmo_cmd_behavior_flow_ctx_t *f,
                                         const nmo_script_use_t *use)
{
    char *label = nmo_cmd_behavior_node_label_dup(f, use->node_id);
    char *path = view_path_dup(f, use->graph_id);
    char *what = view_use_text_dup(f, use);
    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item && label && path && what &&
        nmo_cli_record_str(item, "kind", NULL, nmo_script_use_kind_name(use->kind)) &&
        nmo_cli_record_uint(item, "node_id", NULL, use->node_id) &&
        nmo_cli_record_str(item, "node_label", NULL, label) &&
        nmo_cli_record_uint(item, "graph_id", NULL, use->graph_id) &&
        nmo_cli_record_uint(item, "root_id", NULL, use->root_id) &&
        nmo_cli_record_str(item, "path", NULL, path) &&
        nmo_cli_record_str(item, "key_value", NULL, view_value_kind_name(use->key_value)) &&
        nmo_cli_record_str(item, "summary", NULL, what);
    if (ok && use->message) {
        ok = nmo_cli_record_str(item, "message", NULL, use->message);
    }
    if (ok && use->object_id) {
        ok = nmo_cli_record_uint(item, "object_id", NULL, use->object_id);
    }
    if (ok && (use->object_id || use->object_name)) {
        ok = nmo_cli_record_str(item, "object_name", NULL,
                                use->object_id ? resolve_name(f->repo, use->object_id)
                                               : use->object_name);
    }
    if (ok && use->object_name) {
        ok = nmo_cli_record_bool(item, "by_name", NULL, true);
    }
    if (ok && use->column >= 0) {
        ok = nmo_cli_record_int(item, "column", NULL, use->column) &&
             nmo_cli_record_str_opt(item, "column_name", NULL, use->column_name, NULL);
    }
    if (ok && use->dest_object_id) {
        ok = nmo_cli_record_uint(item, "dest_object_id", NULL, use->dest_object_id) &&
             nmo_cli_record_str(item, "dest_name", NULL,
                                resolve_name(f->repo, use->dest_object_id));
    }
    if (ok) {
        ok = nmo_cli_record_set_summary_fmt(item, "  %-10s %s  in %s  (%s)",
                                            nmo_script_use_kind_name(use->kind), label, path,
                                            what);
    }
    free(label);
    free(path);
    free(what);
    if (!ok) {
        nmo_cli_record_free(item);
        return NULL;
    }
    return item;
}

/* ============================================================================
 * nmo script xref
 * ============================================================================ */

typedef enum xref_section {
    XREF_MESSAGES = 0,
    XREF_ARRAYS,
    XREF_SCRIPTS,
    XREF_SECTION_COUNT,
} xref_section_t;

typedef struct xref_entry {
    const nmo_script_use_t *use;
    const char *key;     /* the message name, or the object name */
    nmo_object_id_t key_id;
    size_t order;
} xref_entry_t;

static int xref_entry_cmp(const void *a, const void *b)
{
    const xref_entry_t *x = (const xref_entry_t *)a;
    const xref_entry_t *y = (const xref_entry_t *)b;
    int c = strcmp(x->key, y->key);
    if (c != 0) return c;
    if (x->key_id != y->key_id) return x->key_id < y->key_id ? -1 : 1;
    if (x->use->kind != y->use->kind) return x->use->kind < y->use->kind ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order ? 1 : 0;
}

static bool xref_use_in_section(const nmo_script_use_t *use, xref_section_t section)
{
    switch (section) {
    case XREF_MESSAGES: return nmo_script_use_kind_is_message(use->kind);
    case XREF_ARRAYS:   return nmo_script_use_kind_is_array(use->kind);
    case XREF_SCRIPTS:  return nmo_script_use_kind_is_script(use->kind);
    default:            return false;
    }
}

static bool xref_use_known(const nmo_script_use_t *use)
{
    return nmo_script_use_kind_is_message(use->kind) ? use->message != NULL
                                                     : use->object_id != 0 || use->object_name;
}

/* One section: its keys, each with its uses; JSON: `key` -> [{name, [id], uses}]. */
static bool xref_add_section(nmo_cli_record_t *rec,
                             const nmo_cmd_behavior_flow_ctx_t *f,
                             const nmo_script_use_t *uses,
                             size_t use_count,
                             xref_section_t section,
                             const char *pattern,
                             size_t *unresolved)
{
    static const char *const keys[] = {"messages", "arrays", "scripts"};
    static const char *const titles[] = {"Messages", "Data Arrays", "Scripts"};
    xref_entry_t *entries = (xref_entry_t *)malloc((use_count ? use_count : 1u) *
                                                    sizeof(*entries));
    if (!entries) return false;
    size_t count = 0;
    for (size_t i = 0; i < use_count; i++) {
        const nmo_script_use_t *use = &uses[i];
        if (!xref_use_in_section(use, section)) continue;
        if (!xref_use_known(use)) {
            (*unresolved)++;
            continue;
        }
        const char *key = section == XREF_MESSAGES ? use->message
                        : use->object_id ? resolve_name(f->repo, use->object_id)
                        : use->object_name;
        if (pattern && !nmo_tool_match_wildcard_ci(pattern, key)) continue;
        entries[count++] = (xref_entry_t){use, key, section == XREF_MESSAGES ? 0 : use->object_id,
                                          i};
    }
    qsort(entries, count, sizeof(*entries), xref_entry_cmp);

    size_t key_count = 0;
    for (size_t i = 0; i < count; i++) {
        key_count += i == 0 || xref_entry_cmp(&(xref_entry_t){entries[i].use, entries[i].key,
                                                               entries[i].key_id, 0},
                                              &(xref_entry_t){entries[i].use, entries[i - 1].key,
                                                               entries[i - 1].key_id, 0}) != 0;
    }
    bool ok = nmo_cli_record_heading(rec, titles[section]) &&
              nmo_cli_record_raw_fmt(rec, "  %zu key(s), %zu use(s)\n", key_count, count);
    nmo_cli_record_array_t *arr = ok ? nmo_cli_record_array(rec, keys[section], NULL) : NULL;
    ok = arr != NULL;
    if (ok) {
        nmo_cli_record_array_omit_heading(arr);
        nmo_cli_record_array_inline_items(arr);
    }
    size_t i = 0;
    while (ok && i < count) {
        size_t end = i + 1;
        while (end < count && strcmp(entries[end].key, entries[i].key) == 0 &&
               entries[end].key_id == entries[i].key_id) {
            end++;
        }
        nmo_cli_record_t *item = nmo_cli_record_new();
        char *title = section == XREF_MESSAGES
            ? nmo_tool_strdup_fmt("\"%s\"", entries[i].key)
            : entries[i].key_id
            ? nmo_cmd_behavior_node_label_dup(f, entries[i].key_id)
            : nmo_tool_strdup_fmt("\"%s\" (looked up by name; not in this file)", entries[i].key);
        ok = item && title &&
             nmo_cli_record_str(item, "name", NULL, entries[i].key) &&
             (section == XREF_MESSAGES || entries[i].key_id == 0 ||
              nmo_cli_record_uint(item, "id", NULL, entries[i].key_id)) &&
             nmo_cli_record_raw_fmt(item, "\n%s\n", title);
        free(title);
        nmo_cli_record_array_t *item_uses = ok ? nmo_cli_record_array(item, "uses", NULL) : NULL;
        ok = item_uses != NULL;
        if (ok) nmo_cli_record_array_omit_heading(item_uses);
        for (size_t u = i; ok && u < end; u++) {
            nmo_cli_record_t *use = view_use_record(f, entries[u].use);
            ok = use != NULL && nmo_cli_record_array_add(item_uses, use);
        }
        if (ok) {
            ok = nmo_cli_record_array_add(arr, item);
        } else {
            nmo_cli_record_free(item);
        }
        i = end;
    }
    free(entries);
    return ok;
}

/* The uses whose key the file does not hold (computed or unconnected). */
static bool xref_add_unresolved(nmo_cli_record_t *rec,
                                const nmo_cmd_behavior_flow_ctx_t *f,
                                const nmo_script_use_t *uses,
                                size_t use_count,
                                const bool *sections,
                                size_t unresolved)
{
    bool ok = nmo_cli_record_heading(rec, "Unresolved") &&
              nmo_cli_record_raw_fmt(rec, "  %zu use(s) whose key is not saved in the file\n",
                                     unresolved);
    nmo_cli_record_array_t *arr = ok ? nmo_cli_record_array(rec, "unresolved", NULL) : NULL;
    ok = arr != NULL;
    if (ok) nmo_cli_record_array_omit_heading(arr);
    for (size_t i = 0; ok && i < use_count; i++) {
        const nmo_script_use_t *use = &uses[i];
        bool shown = false;
        for (int s = 0; s < XREF_SECTION_COUNT; s++) {
            shown = shown || (sections[s] && xref_use_in_section(use, (xref_section_t)s));
        }
        if (!shown || xref_use_known(use)) continue;
        nmo_cli_record_t *item = view_use_record(f, use);
        ok = item != NULL && nmo_cli_record_array_add(arr, item);
    }
    return ok;
}

typedef struct xref_opts {
    bool sections[XREF_SECTION_COUNT];
    const char *pattern;
} xref_opts_t;

/* Parse the options; `file_operand` when the last argument is the file. */
static int xref_parse(int argc, char **argv, bool file_operand, xref_opts_t *out)
{
    static const nmo_opt_def_t opts[] = {
        {"--messages", NULL, NMO_OPT_FLAG,   "Only messages"},
        {"--arrays",   NULL, NMO_OPT_FLAG,   "Only data arrays"},
        {"--scripts",  NULL, NMO_OPT_FLAG,   "Only script activation"},
        {"--name",     "-n", NMO_OPT_STRING, "Only keys whose name matches the pattern"},
        NMO_OPT_DEF_JSON,
    };
    enum { OPT_MESSAGES, OPT_ARRAYS, OPT_SCRIPTS, OPT_NAME, OPT_JSON, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;
    if (r.pos_count != (file_operand ? 1u : 0u)) {
        fprintf(stderr, "Usage: nmo script xref [--messages] [--arrays] [--scripts] "
                        "[--name <pattern>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    memset(out, 0, sizeof(*out));
    out->sections[XREF_MESSAGES] = nmo_opt_flag(&vals[OPT_MESSAGES]);
    out->sections[XREF_ARRAYS] = nmo_opt_flag(&vals[OPT_ARRAYS]);
    out->sections[XREF_SCRIPTS] = nmo_opt_flag(&vals[OPT_SCRIPTS]);
    if (!out->sections[0] && !out->sections[1] && !out->sections[2]) {
        out->sections[0] = out->sections[1] = out->sections[2] = true;
    }
    out->pattern = nmo_opt_str(&vals[OPT_NAME]);
    return NMO_CLI_EXIT_SUCCESS;
}

static int xref_run(nmo_cmd_ctx_t *c, const xref_opts_t *o)
{
    nmo_cmd_behavior_flow_ctx_t flow;
    if (!nmo_cmd_behavior_flow_init(&flow, c)) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    nmo_script_index_t *index = NULL;
    if (nmo_script_index_build(c->workspace, flow.model, &index) != NMO_OK) {
        fprintf(stderr, "Error: Failed to build the script index\n");
        nmo_cmd_behavior_flow_dispose(&flow);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    size_t use_count = 0;
    const nmo_script_use_t *uses = nmo_script_index_uses(index, &use_count);

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    size_t unresolved = 0;
    for (int s = 0; ok && s < XREF_SECTION_COUNT; s++) {
        if (o->sections[s]) {
            ok = xref_add_section(rec, &flow, uses, use_count, (xref_section_t)s, o->pattern,
                                  &unresolved);
        }
    }
    if (ok && !o->pattern) {
        ok = xref_add_unresolved(rec, &flow, uses, use_count, o->sections, unresolved);
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    int rc = nmo_cmd_ctx_emit_record(c, rec, "script.xref", 0, c->colorize);
    nmo_script_index_destroy(index);
    nmo_cmd_behavior_flow_dispose(&flow);
    return rc;
}

int nmo_cmd_script_xref(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    xref_opts_t o;
    int rc = xref_parse(argc, argv, true, &o);
    if (rc) return rc;
    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    return nmo_cmd_ctx_done(&c, xref_run(&c, &o));
}

int nmo_cmd_script_xref_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    xref_opts_t o;
    int rc = xref_parse(argc, argv, false, &o);
    return rc ? rc : xref_run(ctx, &o);
}

/* ============================================================================
 * nmo script view
 * ============================================================================ */

typedef struct view_ctx {
    nmo_cmd_behavior_flow_ctx_t flow;
    nmo_script_index_t *index;
    const nmo_script_node_t *nodes;
    const nmo_object_id_t *children;
    const nmo_script_io_t *ios;
    const nmo_script_param_t *params;
    const nmo_script_link_t *links;
    const nmo_script_data_edge_t *edges;
    const nmo_script_use_t *uses;
    size_t use_count;
    const nmo_script_operation_t *operations;
} view_ctx_t;

/* "Label#id" with " [Prototype]" for a renamed building block and " [Graph]" for a graph. */
static char *view_node_title_dup(const view_ctx_t *v, const nmo_script_node_t *node)
{
    char *label = nmo_cmd_behavior_node_label_dup(&v->flow, node->id);
    if (!label) return NULL;
    const char *tag = NULL;
    if (node->kind != NMO_SCRIPT_NODE_BUILDING_BLOCK) {
        tag = nmo_script_node_kind_name(node->kind);
    } else if (node->prototype_name && strcmp(node->prototype_name, node->name) != 0) {
        tag = node->prototype_name;
    }
    char *title = tag ? nmo_tool_strdup_fmt("%s [%s]", label, tag) : nmo_tool_strdup_fmt("%s", label);
    free(label);
    return title;
}

/* Text: "Target#id.io, ..." for the links of `io` in `graph_id`, then a newline. */
static bool view_add_io_links(nmo_cli_record_t *item, const view_ctx_t *v,
                              const nmo_script_io_t *io, nmo_object_id_t graph_id,
                              nmo_cli_record_array_t *json_links)
{
    size_t count = 0;
    const nmo_script_link_t *const *links =
        nmo_script_model_links_from_io(v->flow.model, io->id, &count);
    bool ok = true;
    bool any = false;
    for (size_t l = 0; ok && l < count; l++) {
        if (links[l]->graph_id != graph_id) continue;
        char *target = nmo_cmd_behavior_io_label_dup(&v->flow, links[l]->target_io_id);
        ok = target != NULL && nmo_cli_record_raw_fmt(item, "%s%s", any ? ", " : "", target);
        if (ok && links[l]->activation_delay != 0) {
            ok = nmo_cli_record_raw_fmt(item, " (delay %d)", (int)links[l]->activation_delay);
        }
        nmo_cli_record_t *link = ok ? nmo_cli_record_new() : NULL;
        ok = link != NULL &&
             nmo_cli_record_str(link, "from", NULL, io->name) &&
             nmo_cli_record_str(link, "to", NULL, target) &&
             nmo_cli_record_int(link, "delay", NULL, links[l]->activation_delay) &&
             nmo_cli_record_array_add(json_links, link);
        free(target);
        any = true;
    }
    return ok && nmo_cli_record_raw(item, "\n");
}

/* The outgoing links of `node`'s outputs, one text line per output. */
static bool view_add_output_links(nmo_cli_record_t *item, const view_ctx_t *v,
                                  const nmo_script_node_t *node, nmo_object_id_t graph_id,
                                  bool node_is_graph_itself)
{
    nmo_cli_record_array_t *json_links = nmo_cli_record_array(item, "links", NULL);
    if (!json_links) return false;
    nmo_cli_record_array_omit_heading(json_links);
    /* A graph's own inputs start its flow; a sub-behavior's outputs continue it */
    size_t first = node->first_io + (node_is_graph_itself ? 0 : node->input_count);
    size_t count = node_is_graph_itself ? node->input_count : node->output_count;
    bool ok = true;
    for (size_t i = 0; ok && i < count; i++) {
        const nmo_script_io_t *io = &v->ios[first + i];
        size_t link_count = 0;
        const nmo_script_link_t *const *links =
            nmo_script_model_links_from_io(v->flow.model, io->id, &link_count);
        size_t shown = 0;
        for (size_t l = 0; l < link_count; l++) {
            shown += links[l]->graph_id == graph_id;
        }
        if (shown == 0) continue;
        ok = nmo_cli_record_raw_fmt(item, "    %s -> ", io->name) &&
             view_add_io_links(item, v, io, graph_id, json_links);
    }
    return ok;
}

/* The semantic lines of `node`: what it sends, waits for, reads, writes, or activates. */
static bool view_add_semantics(nmo_cli_record_t *item, const view_ctx_t *v,
                               const nmo_script_node_t *node)
{
    nmo_cli_record_array_t *arr = nmo_cli_record_array(item, "uses", NULL);
    if (!arr) return false;
    nmo_cli_record_array_omit_heading(arr);
    bool ok = true;
    if (node->operation_name) {
        ok = nmo_cli_record_raw_fmt(item, "    = %s\n", node->operation_name);
    }
    for (size_t i = 0; ok && i < v->use_count; i++) {
        if (v->uses[i].node_id != node->id) continue;
        char *what = view_use_text_dup(&v->flow, &v->uses[i]);
        nmo_cli_record_t *use = what ? nmo_cli_record_new() : NULL;
        ok = use != NULL &&
             nmo_cli_record_str(use, "kind", NULL, nmo_script_use_kind_name(v->uses[i].kind)) &&
             nmo_cli_record_str(use, "summary", NULL, what) &&
             nmo_cli_record_array_add(arr, use) &&
             nmo_cli_record_raw_fmt(item, "    = %s\n", what);
        free(what);
    }
    return ok;
}

/* "Owner#id.pOut, ..." for the outputs writing the parameter input `param_id` reads,
 * when its value is written at run time; NULL otherwise. Heap string. */
static char *view_writers_text_dup(const view_ctx_t *v, nmo_object_id_t param_id)
{
    nmo_object_id_t holder = 0;
    if (nmo_script_model_value_source(v->flow.model, param_id, &holder) !=
        NMO_SCRIPT_VALUE_WRITTEN) {
        return NULL;
    }
    size_t count = 0;
    const nmo_script_data_edge_t *const *writers =
        nmo_script_model_param_writers(v->flow.model, holder, &count);
    char *text = NULL;
    for (size_t i = 0; i < count && i < 3; i++) {
        char owner[256];
        (void)nmo_script_model_label(v->flow.model, writers[i]->source_owner_id, owner,
                                     sizeof(owner));
        char *next = nmo_tool_strdup_fmt("%s%s%s.%s", text ? text : "", text ? ", " : "",
                                         owner, resolve_name(v->flow.repo, writers[i]->source_id));
        free(text);
        text = next;
    }
    if (text != NULL && count > 3) {
        char *next = nmo_tool_strdup_fmt("%s and %zu more", text, count - 3);
        free(text);
        text = next;
    }
    return text;
}

/* "    Name <- <source>" for each target and input of `node` read in `graph_id`. */
static bool view_add_inputs(nmo_cli_record_t *item, const view_ctx_t *v,
                            const nmo_script_node_t *node, nmo_object_id_t graph_id)
{
    nmo_cli_record_array_t *arr = nmo_cli_record_array(item, "inputs", NULL);
    if (!arr) return false;
    nmo_cli_record_array_omit_heading(arr);
    bool ok = true;
    for (size_t i = 0; ok && i < node->param_count; i++) {
        const nmo_script_param_t *p = &v->params[node->first_param + i];
        if (p->role != NMO_SCRIPT_PARAM_INPUT && p->role != NMO_SCRIPT_PARAM_TARGET) continue;
        const char *name = p->role == NMO_SCRIPT_PARAM_TARGET ? "(target)"
                         : p->name[0] ? p->name : "(unnamed)";
        nmo_cmd_behavior_param_ref_t src = {0};
        bool has_source = p->source_id != 0;
        ok = !has_source || nmo_cmd_behavior_resolve_param(&v->flow, graph_id, p->source_id, &src);
        const char *text = has_source ? src.text : "(not connected)";
        char *writers = ok ? view_writers_text_dup(v, p->id) : NULL;
        nmo_cli_record_t *in = ok ? nmo_cli_record_new() : NULL;
        ok = in != NULL &&
             nmo_cli_record_str(in, "name", NULL, name) &&
             nmo_cli_record_str(in, "type", NULL, p->type_name) &&
             nmo_cli_record_str(in, "source", NULL, text) &&
             (writers == NULL || nmo_cli_record_str(in, "written_by", NULL, writers)) &&
             nmo_cli_record_array_add(arr, in) &&
             nmo_cli_record_raw_fmt(item, "    %s <- %s%s%s%s\n", name, text,
                                    p->is_shared ? " (shared)" : "",
                                    writers ? ", written by " : "", writers ? writers : "");
        free(writers);
        nmo_cmd_behavior_param_ref_dispose(&src);
    }
    /* Building block settings */
    for (size_t i = 0; ok && node->kind == NMO_SCRIPT_NODE_BUILDING_BLOCK &&
                       i < node->param_count; i++) {
        const nmo_script_param_t *p = &v->params[node->first_param + i];
        char value[256];
        if (p->role != NMO_SCRIPT_PARAM_LOCAL || !p->is_setting ||
            nmo_script_model_param_value(v->flow.model, p->id, value, sizeof(value)) != NMO_OK) {
            continue;
        }
        ok = nmo_cli_record_raw_fmt(item, "    setting %s = %s\n",
                                    p->name[0] ? p->name : "(unnamed)", value);
    }
    return ok;
}

/* "    pOut => Target (kind)" for each write of `node`'s outputs inside `graph_id`. */
static bool view_add_writes(nmo_cli_record_t *item, const view_ctx_t *v,
                            const nmo_script_node_t *node, const nmo_script_node_t *graph)
{
    bool ok = true;
    for (size_t i = 0; ok && i < graph->data_edge_count; i++) {
        const nmo_script_data_edge_t *e = &v->edges[graph->first_data_edge + i];
        if (e->kind != NMO_SCRIPT_DATA_WRITE || e->source_owner_id != node->id) continue;
        nmo_cmd_behavior_param_ref_t target = {0};
        ok = nmo_cmd_behavior_resolve_param(&v->flow, graph->id, e->target_id, &target) &&
             nmo_cli_record_raw_fmt(item, "    %s => %s\n",
                                    resolve_name(v->flow.repo, e->source_id), target.text);
        nmo_cmd_behavior_param_ref_dispose(&target);
    }
    return ok;
}

static bool view_order_has(const nmo_object_id_t *order, size_t count, nmo_object_id_t id)
{
    for (size_t k = 0; k < count; k++) {
        if (order[k] == id) return true;
    }
    return false;
}

/*
 * The order to show a graph's sub-behaviors in `order` (room for `cap`):
 * order[0] is the graph, then the sub-behaviors as its links reach them
 * breadth first from its inputs, then the ones no link reaches.
 */
static size_t view_node_order(const view_ctx_t *v, const nmo_script_node_t *graph,
                              nmo_object_id_t *order, size_t cap)
{
    size_t count = 0;
    size_t head = 0;
    order[count++] = graph->id;
    while (head < count) {
        const nmo_script_node_t *n = nmo_script_model_find_node(v->flow.model, order[head++]);
        bool is_graph = n && n->id == graph->id;
        size_t first = n ? n->first_io + (is_graph ? 0 : n->input_count) : 0;
        size_t io_count = n ? (is_graph ? n->input_count : n->output_count) : 0;
        for (size_t i = 0; i < io_count; i++) {
            size_t link_count = 0;
            const nmo_script_link_t *const *links =
                nmo_script_model_links_from_io(v->flow.model, v->ios[first + i].id, &link_count);
            for (size_t l = 0; l < link_count && count < cap; l++) {
                nmo_object_id_t target = links[l]->target_node_id;
                if (links[l]->graph_id == graph->id && target != 0 &&
                    !view_order_has(order, count, target)) {
                    order[count++] = target;
                }
            }
        }
    }
    for (size_t i = 0; i < graph->child_count && count < cap; i++) {
        nmo_object_id_t child = v->children[graph->first_child + i];
        if (!view_order_has(order, count, child)) {
            order[count++] = child;
        }
    }
    return count;
}

static bool view_add_graph(nmo_cli_record_array_t *graphs, const view_ctx_t *v,
                           const nmo_script_node_t *graph, bool recursive);

static bool view_add_operations(nmo_cli_record_t *rec, const view_ctx_t *v,
                                const nmo_script_node_t *graph)
{
    bool ok = true;
    for (size_t i = 0; ok && i < graph->operation_count; i++) {
        const nmo_script_operation_t *op = &v->operations[graph->first_operation + i];
        nmo_cmd_behavior_param_ref_t in1 = {0}, in2 = {0};
        const nmo_script_param_t *p1 = nmo_script_model_find_param(v->flow.model, op->input1_id);
        const nmo_script_param_t *p2 = nmo_script_model_find_param(v->flow.model, op->input2_id);
        bool has1 = p1 && p1->source_id, has2 = p2 && p2->source_id;
        ok = (!has1 || nmo_cmd_behavior_resolve_param(&v->flow, graph->id, p1->source_id, &in1)) &&
             (!has2 || nmo_cmd_behavior_resolve_param(&v->flow, graph->id, p2->source_id, &in2)) &&
             nmo_cli_record_raw_fmt(rec, "\n  %s#%u: %s = %s(%s%s%s)\n", op->operation_name,
                                    (unsigned)op->id, resolve_name(v->flow.repo, op->output_id),
                                    op->operation_name, has1 ? in1.text : "-",
                                    op->input2_id ? ", " : "",
                                    op->input2_id ? (has2 ? in2.text : "-") : "");
        nmo_cmd_behavior_param_ref_dispose(&in1);
        nmo_cmd_behavior_param_ref_dispose(&in2);
    }
    return ok;
}

static bool view_add_graph(nmo_cli_record_array_t *graphs, const view_ctx_t *v,
                           const nmo_script_node_t *graph, bool recursive)
{
    size_t order_cap = graph->child_count + 1u;
    nmo_object_id_t *order = (nmo_object_id_t *)malloc(order_cap * sizeof(*order));
    char *title = view_node_title_dup(v, graph);
    char *path = graph->parent_id ? view_path_dup(&v->flow, graph->parent_id) : NULL;
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = order && title && rec &&
              nmo_cli_record_uint(rec, "id", NULL, graph->id) &&
              nmo_cli_record_str(rec, "title", NULL, title) &&
              nmo_cli_record_uint(rec, "parent_id", NULL, graph->parent_id) &&
              nmo_cli_record_raw_fmt(rec, "%s%s%s\n", title, path ? "  in " : "",
                                     path ? path : "");
    free(title);
    free(path);

    /* The graph's own inputs and where its parameters come from */
    ok = ok && view_add_inputs(rec, v, graph, graph->parent_id) &&
         view_add_output_links(rec, v, graph, graph->id, true);
    ok = ok && view_add_operations(rec, v, graph);

    nmo_cli_record_array_t *nodes = ok ? nmo_cli_record_array(rec, "nodes", NULL) : NULL;
    ok = nodes != NULL;
    if (ok) {
        nmo_cli_record_array_omit_heading(nodes);
        nmo_cli_record_array_inline_items(nodes);
    }
    size_t count = ok ? view_node_order(v, graph, order, order_cap) : 0;
    for (size_t i = 1; ok && i < count; i++) {
        const nmo_script_node_t *node = nmo_script_model_find_node(v->flow.model, order[i]);
        if (!node) continue;
        char *node_title = view_node_title_dup(v, node);
        nmo_cli_record_t *item = node_title ? nmo_cli_record_new() : NULL;
        ok = item != NULL &&
             nmo_cli_record_uint(item, "id", NULL, node->id) &&
             nmo_cli_record_str(item, "title", NULL, node_title) &&
             nmo_cli_record_str_opt(item, "prototype", NULL, node->prototype_name, NULL) &&
             nmo_cli_record_raw_fmt(item, "\n  %s\n", node_title) &&
             view_add_semantics(item, v, node) &&
             view_add_inputs(item, v, node, graph->id) &&
             view_add_output_links(item, v, node, graph->id, false) &&
             view_add_writes(item, v, node, graph);
        free(node_title);
        if (ok) {
            ok = nmo_cli_record_array_add(nodes, item);
        } else {
            nmo_cli_record_free(item);
        }
    }
    ok = ok && nmo_cli_record_raw(rec, "\n");
    if (ok) {
        ok = nmo_cli_record_array_add(graphs, rec);
    } else {
        nmo_cli_record_free(rec);
    }
    for (size_t i = 0; ok && recursive && i < graph->child_count; i++) {
        const nmo_script_node_t *child =
            nmo_script_model_find_node(v->flow.model, v->children[graph->first_child + i]);
        if (child && child->kind != NMO_SCRIPT_NODE_BUILDING_BLOCK && child->parent_id == graph->id) {
            ok = view_add_graph(graphs, v, child, true);
        }
    }
    free(order);
    return ok;
}

typedef struct view_opts {
    bool all;
    bool recursive;
    nmo_core_object_selector_t selector;
} view_opts_t;

/* Parse the options; `file_operand` when the last argument is the file. */
static int view_parse(int argc, char **argv, bool file_operand, view_opts_t *out)
{
    static const nmo_opt_def_t opts[] = {
        {"--recursive", "-r", NMO_OPT_FLAG,   "Also show the graphs inside it"},
        {"--all",       "-a", NMO_OPT_FLAG,   "Show every root and the graphs inside them"},
        NMO_OPT_DEF_JSON,
        {"--id",        "-i", NMO_OPT_UINT,   "Behavior object ID"},
        {"--name",      "-n", NMO_OPT_STRING, "Behavior object name"},
    };
    enum { OPT_RECURSIVE, OPT_ALL, OPT_JSON, OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    memset(out, 0, sizeof(*out));
    out->all = nmo_opt_flag(&vals[OPT_ALL]);
    out->recursive = out->all || nmo_opt_flag(&vals[OPT_RECURSIVE]);
    size_t files = file_operand ? 1u : 0u;
    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    bool has_positional = r.pos_count == files + 1u;
    if (r.pos_count < files || r.pos_count > files + 1u ||
        (!out->all && !has_selector_opt && !has_positional) ||
        ((out->all || has_selector_opt) && has_positional)) {
        fprintf(stderr, "Usage: nmo script view [--recursive] [--all | --id <id> | --name <name> "
                        "| <id>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    out->selector = (nmo_core_object_selector_t){
        .has_id = vals[OPT_ID].present,
        .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
        .positional_id = has_positional ? r.pos_args[0] : NULL,
        .name = nmo_opt_str(&vals[OPT_NAME]),
        .required_base_class = NMO_CID_BEHAVIOR,
        .selector_label = "Behavior",
        .type_label = "CKBehavior",
    };
    return NMO_CLI_EXIT_SUCCESS;
}

static int view_run(nmo_cmd_ctx_t *c, const view_opts_t *o)
{
    nmo_object_id_t target_id = 0;
    if (!o->all) {
        nmo_object_t *selected = NULL;
        int rc = nmo_core_resolve_one_object(c, &o->selector, &selected, &target_id);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            return rc;
        }
    }

    view_ctx_t v;
    memset(&v, 0, sizeof(v));
    if (!nmo_cmd_behavior_flow_init(&v.flow, c)) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (nmo_script_index_build(c->workspace, v.flow.model, &v.index) != NMO_OK) {
        fprintf(stderr, "Error: Failed to build the script index\n");
        nmo_cmd_behavior_flow_dispose(&v.flow);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    v.nodes = nmo_script_model_nodes(v.flow.model, NULL);
    v.children = nmo_script_model_children(v.flow.model, NULL);
    v.ios = nmo_script_model_ios(v.flow.model, NULL);
    v.params = nmo_script_model_params(v.flow.model, NULL);
    v.links = nmo_script_model_links(v.flow.model, NULL);
    v.edges = nmo_script_model_data_edges(v.flow.model, NULL);
    v.operations = nmo_script_model_operations(v.flow.model, NULL);
    v.uses = nmo_script_index_uses(v.index, &v.use_count);

    nmo_cli_record_t *rec = nmo_cli_record_new();
    nmo_cli_record_array_t *graphs = rec ? nmo_cli_record_array(rec, "graphs", NULL) : NULL;
    bool ok = graphs != NULL;
    if (ok) {
        nmo_cli_record_array_omit_heading(graphs);
        nmo_cli_record_array_inline_items(graphs);
    }
    if (ok && o->all) {
        size_t root_count = 0;
        const nmo_object_id_t *roots = nmo_script_model_roots(v.flow.model, &root_count);
        for (size_t i = 0; ok && i < root_count; i++) {
            const nmo_script_node_t *root = nmo_script_model_find_node(v.flow.model, roots[i]);
            if (root && root->kind != NMO_SCRIPT_NODE_BUILDING_BLOCK) {
                ok = view_add_graph(graphs, &v, root, true);
            }
        }
    } else if (ok) {
        const nmo_script_node_t *node = nmo_script_model_find_node(v.flow.model, target_id);
        ok = node != NULL && view_add_graph(graphs, &v, node, o->recursive);
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    int rc = nmo_cmd_ctx_emit_record(c, rec, "script.view", 0, c->colorize);
    nmo_script_index_destroy(v.index);
    nmo_cmd_behavior_flow_dispose(&v.flow);
    return rc;
}

int nmo_cmd_script_view(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    view_opts_t o;
    int rc = view_parse(argc, argv, true, &o);
    if (rc) return rc;
    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    return nmo_cmd_ctx_done(&c, view_run(&c, &o));
}

int nmo_cmd_script_view_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    view_opts_t o;
    int rc = view_parse(argc, argv, false, &o);
    return rc ? rc : view_run(ctx, &o);
}
