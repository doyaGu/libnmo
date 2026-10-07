/**
 * @file nmo_cmd_behavior_search.c
 * @brief CLI behavior find and trace command implementations
 */

#include "nmo_cmd_behavior.h"
#include "nmo_cmd_behavior_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

#include "nmo.h"
#include "behavior/nmo_behavior_analyze.h"
#include "object/nmo_context.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_system.h"
#include "extension/nmo_behavior_registry.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * behavior find -- search behaviors by name/GUID/parameter type
 * ============================================================================ */

static bool guid_str_match(nmo_guid_t guid, const char *pattern) {
    char *text = nmo_tool_strdup_fmt("%08X-%08X", guid.d1, guid.d2);
    if (!text) {
        return false;
    }
    bool found = false;
    for (const char *p = text; *p && !found; p++) {
        const char *a = p, *b = pattern;
        while (*a && *b) {
            char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 32) : *a;
            char cb = (*b >= 'a' && *b <= 'z') ? (char)(*b - 32) : *b;
            if (ca != cb) break;
            a++; b++;
        }
        found = (*b == '\0');
    }
    free(text);
    return found;
}

static bool behavior_has_param_type(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    const nmo_behavior_state_t *bs,
    const char *type_pattern)
{
    if (bs->in_parameters.data) {
        for (size_t i = 0; i < bs->in_parameters.count; i++) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &bs->in_parameters, i);
            nmo_object_t *p = nmo_object_repository_find_by_id(repo, id);
            if (!p) continue;
            nmo_guid_t tg = get_param_type_guid(p);
            const char *tn = resolve_type(reg, tg);
            if (tn && nmo_tool_match_wildcard_ci(type_pattern, tn)) return true;
        }
    }
    if (bs->out_parameters.data) {
        for (size_t i = 0; i < bs->out_parameters.count; i++) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &bs->out_parameters, i);
            nmo_object_t *p = nmo_object_repository_find_by_id(repo, id);
            if (!p) continue;
            nmo_guid_t tg = get_param_type_guid(p);
            const char *tn = resolve_type(reg, tg);
            if (tn && nmo_tool_match_wildcard_ci(type_pattern, tn)) return true;
        }
    }
    return false;
}

static bool behavior_has_op_type(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    const nmo_behavior_state_t *bs,
    const char *op_pattern)
{
    if (!bs->operations.data) return false;
    for (size_t i = 0; i < bs->operations.count; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(&bs->operations, i);
        nmo_object_t *op = nmo_object_repository_find_by_id(repo, id);
        if (!op || !op->state) continue;
        const nmo_parameteroperation_state_t *os =
            (const nmo_parameteroperation_state_t *)op->state;
        const char *on = nmo_type_registry_guid_to_name(reg, os->operation_guid);
        if (on && nmo_tool_match_wildcard_ci(op_pattern, on)) return true;
    }
    return false;
}

typedef struct behavior_find_data {
    nmo_object_repository_t *repo;
    const char *name_pat;
    const char *guid_pat;
    const char *ptype_pat;
    const char *optype_pat;
    bool only_scripts;
    bool only_bbs;
    nmo_object_t **matches;
    size_t match_count;
    size_t match_capacity;
    bool ok;
} behavior_find_data_t;

static int behavior_find_object(size_t index, nmo_object_t *obj,
                                const nmo_cmd_ctx_t *c, void *user)
{
    (void)index;

    behavior_find_data_t *data = (behavior_find_data_t *)user;
    if (!data || !obj || !data->ok) {
        return 0;
    }

    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    if (!is_behavior_class(c->registry, cid)) {
        return 0;
    }

    const nmo_behavior_state_t *bs =
        (const nmo_behavior_state_t *)nmo_object_get_state(obj);
    if (!bs) {
        return 0;
    }

    bool is_script = (bs->flags & CKBEHAVIOR_SCRIPT) != 0;
    bool is_bb = (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;

    if (data->only_scripts && !is_script) return 0;
    if (data->only_bbs && !is_bb) return 0;

    const char *name = nmo_object_get_name(obj);
    if (data->name_pat &&
        (!name || !nmo_tool_match_wildcard_ci(data->name_pat, name))) {
        return 0;
    }

    if (data->guid_pat) {
        if (nmo_guid_is_null(bs->block_guid) ||
            !guid_str_match(bs->block_guid, data->guid_pat)) {
            return 0;
        }
    }

    if (data->ptype_pat &&
        !behavior_has_param_type(data->repo, c->registry, bs, data->ptype_pat)) {
        return 0;
    }
    if (data->optype_pat &&
        !behavior_has_op_type(data->repo, c->registry, bs, data->optype_pat)) {
        return 0;
    }

    if (data->match_count == data->match_capacity) {
        size_t new_capacity = data->match_capacity ? data->match_capacity * 2u : 64u;
        nmo_object_t **new_matches = (nmo_object_t **)realloc(
            data->matches, new_capacity * sizeof(*new_matches));
        if (!new_matches) {
            data->ok = false;
            return 0;
        }
        data->matches = new_matches;
        data->match_capacity = new_capacity;
    }
    data->matches[data->match_count++] = obj;
    return 0;
}

/* One "results" item; in text, a table row. */
static bool behavior_find_add_result(nmo_cli_record_array_t *arr,
                                     const nmo_cmd_ctx_t *c,
                                     nmo_object_t *obj)
{
    const nmo_behavior_state_t *bs =
        (const nmo_behavior_state_t *)nmo_object_get_state(obj);
    bool is_script = (bs->flags & CKBEHAVIOR_SCRIPT) != 0;
    bool is_bb = (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
    const char *name = nmo_object_get_name(obj);

    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item != NULL &&
              nmo_cli_record_uint(item, "id", "ID", nmo_object_get_id(obj)) &&
              nmo_cli_record_str(item, "name", NULL, (name && name[0]) ? name : "") &&
              nmo_cli_record_str(item, "type", "TYPE",
                                 is_script ? "Script" : is_bb ? "BB" : "Graph");
    if (is_bb && !nmo_guid_is_null(bs->block_guid)) {
        const char *proto_name = nmo_behavior_registry_get_name(
            nmo_context_get_bb_registry(c->ctx), bs->block_guid);
        ok = ok && nmo_cli_record_str_fmt(item, "bb_guid", NULL, "%08X-%08X",
                                          bs->block_guid.d1, bs->block_guid.d2);
        if (proto_name) {
            ok = ok && nmo_cli_record_str(item, "proto_name", NULL, proto_name) &&
                 nmo_cli_record_text(item, "PROTOTYPE", proto_name);
        } else {
            ok = ok && nmo_cli_record_text_fmt(item, "PROTOTYPE", "{%08X-%08X}",
                                               bs->block_guid.d1, bs->block_guid.d2);
        }
    } else {
        ok = ok && nmo_cli_record_text(item, "PROTOTYPE", "-");
    }
    ok = ok && nmo_cli_record_text(item, "NAME", (name && name[0]) ? name : "-");
    if (!ok) {
        nmo_cli_record_free(item);
        return false;
    }
    return nmo_cli_record_array_add(arr, item);
}

int nmo_cmd_behavior_find(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--name",       "-n", NMO_OPT_STRING, "Filter by name pattern"},
        {"--guid",       "-g", NMO_OPT_STRING, "Filter by BB GUID (substring)"},
        {"--param-type", "-t", NMO_OPT_STRING, "Filter by parameter type name"},
        {"--op-type",    "-o", NMO_OPT_STRING, "Filter by operation type name"},
        {"--scripts",    NULL, NMO_OPT_FLAG,   "Show only scripts"},
        {"--bbs",        NULL, NMO_OPT_FLAG,   "Show only building blocks"},
        NMO_OPT_DEF_JSON,
    };
    enum { OPT_NAME, OPT_GUID, OPT_PTYPE, OPT_OPTYPE, OPT_SCRIPTS, OPT_BBS, OPT_JSON, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *name_pat  = nmo_opt_str(&vals[OPT_NAME]);
    /* "find <pattern> <file>": the pattern is a name pattern */
    if (r.pos_count >= 2) {
        if (name_pat) {
            fprintf(stderr, "Error: give the name pattern either as --name or as an argument\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        name_pat = r.pos_args[0];
    }
    const char *guid_pat  = nmo_opt_str(&vals[OPT_GUID]);
    const char *ptype_pat = nmo_opt_str(&vals[OPT_PTYPE]);
    const char *optype_pat = nmo_opt_str(&vals[OPT_OPTYPE]);
    bool only_scripts     = nmo_opt_flag(&vals[OPT_SCRIPTS]);
    bool only_bbs         = nmo_opt_flag(&vals[OPT_BBS]);

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);

    behavior_find_data_t find_data = {
        .repo = repo,
        .name_pat = name_pat,
        .guid_pat = guid_pat,
        .ptype_pat = ptype_pat,
        .optype_pat = optype_pat,
        .only_scripts = only_scripts,
        .only_bbs = only_bbs,
        .ok = true,
    };

    rc = nmo_core_object_query_run(&c, NULL, behavior_find_object,
                                   &find_data, NULL);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        free(find_data.matches);
        fprintf(stderr, "Error: Failed to query objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    static const nmo_cli_table_col_t columns[] = {
        {"ID",   NMO_CLI_ALIGN_RIGHT, 5, 0},
        {"TYPE", NMO_CLI_ALIGN_LEFT,  6, 0},
        {"PROTOTYPE", NMO_CLI_ALIGN_LEFT, 28, 0},
        {"NAME", NMO_CLI_ALIGN_LEFT, 28, 50},
    };
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL && find_data.ok &&
              nmo_cli_record_uint(rec, "match_count", NULL,
                                  (uint64_t)find_data.match_count) &&
              nmo_cli_record_raw_fmt(rec, "Found: %zu behavior(s)\n\n",
                                     find_data.match_count);
    nmo_cli_record_array_t *results =
        ok ? nmo_cli_record_array(rec, "results", NULL) : NULL;
    ok = results != NULL &&
         nmo_cli_record_array_set_table(results, columns,
                                        sizeof(columns) / sizeof(columns[0]));
    for (size_t i = 0; ok && i < find_data.match_count; i++) {
        ok = behavior_find_add_result(results, &c, find_data.matches[i]);
    }
    free(find_data.matches);

    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    rc = nmo_cmd_ctx_emit_record(&c, rec, "behavior.find", 0, c.colorize);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * behavior trace -- execution path tracing from IO (recursive into sub-graphs)
 * ============================================================================ */

/* The behavior links of `node_id` and the graphs below it. */
static size_t trace_count_links(const nmo_script_model_t *model, nmo_object_id_t node_id,
                                uint32_t depth)
{
    const nmo_script_node_t *node = nmo_script_model_find_node(model, node_id);
    if (!node || depth > 256) {
        return 0;
    }
    size_t child_total = 0;
    const nmo_object_id_t *children = nmo_script_model_children(model, &child_total);
    size_t count = node->link_count;
    for (size_t i = 0; i < node->child_count; i++) {
        count += trace_count_links(model, children[node->first_child + i], depth + 1);
    }
    return count;
}

static const char *trace_behavior_type_name(const nmo_script_node_t *node)
{
    return node ? nmo_script_node_kind_name(node->kind) : "Unknown";
}

/* The prototype of a building block, or its name when the prototype is unknown. */
static const char *trace_bb_proto_name(const nmo_script_node_t *node)
{
    if (!node || node->kind != NMO_SCRIPT_NODE_BUILDING_BLOCK) {
        return NULL;
    }
    if (node->prototype_name) {
        return node->prototype_name;
    }
    return node->name[0] ? node->name : NULL;
}

static const char *trace_transition_name(
    nmo_object_id_t root_behavior_id,
    nmo_object_id_t source_owner,
    nmo_object_id_t target_owner,
    const nmo_script_node_t *target)
{
    if (source_owner == 0 || target_owner == 0) {
        return "unknown";
    }
    if (target_owner == root_behavior_id && source_owner != root_behavior_id) {
        return "exit_to_parent";
    }
    if (target_owner != source_owner && target &&
        target->kind != NMO_SCRIPT_NODE_BUILDING_BLOCK) {
        return "enter_subgraph";
    }
    return "same_graph";
}

typedef struct {
    uint32_t depth;
    nmo_object_id_t source_io;
    nmo_object_id_t source_owner;
    nmo_object_id_t target_io;
    const char *target_io_name;
    nmo_object_id_t target_owner;
    const nmo_script_node_t *target;
    const char *transition;
    int32_t delay;
    const char *truncated_reason;
    bool loop_detected;
    nmo_object_id_t loop_io;
} trace_step_t;

/* Top-level fields shared by the empty and the full trace report. */
static nmo_cli_record_t *trace_record_new(nmo_object_id_t beh_id,
                                          const char *beh_name,
                                          size_t entry_count,
                                          size_t link_count)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "behavior_id", NULL, beh_id) &&
              nmo_cli_record_str(rec, "behavior_name", NULL,
                                 (beh_name && beh_name[0]) ? beh_name : "") &&
              nmo_cli_record_uint(rec, "entry_count", NULL,
                                  (uint64_t)entry_count) &&
              nmo_cli_record_uint(rec, "link_count", NULL,
                                  (uint64_t)link_count);
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

/* Text: "  -> Label#id [Prototype].port [Kind]  (transition: ...)". */
static bool trace_add_step_text(nmo_cli_record_t *item,
                                const nmo_cmd_behavior_flow_ctx_t *f,
                                const trace_step_t *step)
{
    const nmo_script_node_t *target = step->target;
    const char *type_label = "";
    const char *proto_name = NULL;
    bool entering_subgraph = false;
    if (target) {
        type_label = target->kind == NMO_SCRIPT_NODE_SCRIPT ? " [Script]"
                   : target->kind == NMO_SCRIPT_NODE_GRAPH ? " [Graph]" : " [BB]";
        entering_subgraph = target->kind == NMO_SCRIPT_NODE_GRAPH;
        /* The prototype of a renamed building block */
        if (target->prototype_name && strcmp(target->prototype_name, target->name) != 0) {
            proto_name = target->prototype_name;
        }
    }

    char *label = step->target_owner ? nmo_cmd_behavior_node_label_dup(f, step->target_owner)
                                     : nmo_tool_strdup_fmt("?");
    bool ok = label != NULL &&
              nmo_cli_record_raw_fmt(item, "%*s%s%s",
                                     (int)(2u * (step->depth + 1u)), "",
                                     entering_subgraph ? "\xe2\x96\xb6 " : "\xe2\x86\x92 ",
                                     label);
    free(label);
    if (proto_name) {
        ok = ok && nmo_cli_record_raw_fmt(item, " [%s]", proto_name);
    }
    ok = ok && nmo_cli_record_raw_fmt(item, ".%s%s  (transition: %s)",
                                      step->target_io_name ? step->target_io_name : "?",
                                      type_label, step->transition);
    if (step->delay != 0) {
        ok = ok && nmo_cli_record_raw_fmt(item, "  (delay: %d)", (int)step->delay);
    }
    if (step->truncated_reason) {
        ok = ok && nmo_cli_record_raw_fmt(item, "  (truncated: %s)",
                                          step->truncated_reason);
    }
    if (step->loop_detected) {
        ok = ok && nmo_cli_record_raw_fmt(item, "  (loop path: #%u -> #%u",
                                          step->source_io, step->target_io);
        if (step->loop_io != 0) {
            ok = ok && nmo_cli_record_raw_fmt(item, " -> #%u", step->loop_io);
        }
        ok = ok && nmo_cli_record_raw(item, ")");
    }
    return ok && nmo_cli_record_raw(item, "\n");
}

/* One "steps" item; in text, one indented trace line. */
static bool trace_add_step(nmo_cli_record_array_t *steps,
                           const nmo_cmd_behavior_flow_ctx_t *f,
                           const trace_step_t *step)
{
    const char *target_proto = trace_bb_proto_name(step->target);
    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item != NULL &&
              nmo_cli_record_uint(item, "depth", NULL, step->depth) &&
              nmo_cli_record_uint(item, "source_io_id", NULL, step->source_io);
    if (step->source_owner != 0) {
        ok = ok &&
             nmo_cli_record_uint(item, "source_owner_id", NULL, step->source_owner) &&
             nmo_cli_record_str(item, "source_owner_name", NULL,
                                resolve_name(f->repo, step->source_owner));
    }
    ok = ok &&
         nmo_cli_record_uint(item, "target_io_id", NULL, step->target_io) &&
         nmo_cli_record_str(item, "target_io_name", NULL,
                            step->target_io_name ? step->target_io_name : "");
    if (step->target_owner != 0) {
        ok = ok &&
             nmo_cli_record_uint(item, "target_owner_id", NULL, step->target_owner) &&
             nmo_cli_record_str(item, "target_owner_name", NULL,
                                resolve_name(f->repo, step->target_owner));
    }
    ok = ok && nmo_cli_record_str(item, "target_behavior_type", NULL,
                                  trace_behavior_type_name(step->target));
    if (target_proto) {
        ok = ok && nmo_cli_record_str(item, "target_bb_proto_name", NULL, target_proto);
    }
    ok = ok && nmo_cli_record_str(item, "transition", NULL, step->transition);
    if (step->delay != 0) {
        ok = ok && nmo_cli_record_int(item, "delay", NULL, step->delay);
    }
    if (step->truncated_reason) {
        ok = ok && nmo_cli_record_str(item, "truncated_reason", NULL,
                                      step->truncated_reason);
    }
    if (step->loop_detected) {
        uint64_t loop_path[3] = {step->source_io, step->target_io, step->loop_io};
        ok = ok &&
             nmo_cli_record_bool(item, "loop_detected", NULL, true) &&
             nmo_cli_record_uint_list(item, "loop_path_io_ids", NULL, loop_path,
                                      step->loop_io != 0 ? 3u : 2u, NULL);
    }
    ok = ok && trace_add_step_text(item, f, step);
    if (!ok) {
        nmo_cli_record_free(item);
        return false;
    }
    return nmo_cli_record_array_add(steps, item);
}

/* Array whose items print their own text lines, without a heading. */
static nmo_cli_record_array_t *trace_inline_array(nmo_cli_record_t *rec,
                                                  const char *key)
{
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, key, NULL);
    if (arr) {
        nmo_cli_record_array_omit_heading(arr);
        nmo_cli_record_array_inline_items(arr);
    }
    return arr;
}

/* The IOs a trace has reached, and the IOs still to follow. */
typedef struct trace_walk {
    nmo_object_id_t *visited;
    size_t visited_count;
    size_t visited_cap;
    struct trace_pending { nmo_object_id_t io; uint32_t depth; } *stack;
    size_t stack_count;
    size_t stack_cap;
} trace_walk_t;

static bool trace_walk_seen(const trace_walk_t *w, nmo_object_id_t io)
{
    for (size_t i = 0; i < w->visited_count; i++) {
        if (w->visited[i] == io) {
            return true;
        }
    }
    return false;
}

/* Mark `io` reached and queue it at `depth`. */
static bool trace_walk_push(trace_walk_t *w, nmo_object_id_t io, uint32_t depth)
{
    if (w->visited_count == w->visited_cap) {
        size_t cap = w->visited_cap ? w->visited_cap * 2u : 256u;
        nmo_object_id_t *v = (nmo_object_id_t *)realloc(w->visited, cap * sizeof(*v));
        if (!v) return false;
        w->visited = v;
        w->visited_cap = cap;
    }
    if (w->stack_count == w->stack_cap) {
        size_t cap = w->stack_cap ? w->stack_cap * 2u : 256u;
        struct trace_pending *st =
            (struct trace_pending *)realloc(w->stack, cap * sizeof(*st));
        if (!st) return false;
        w->stack = st;
        w->stack_cap = cap;
    }
    w->visited[w->visited_count++] = io;
    w->stack[w->stack_count].io = io;
    w->stack[w->stack_count].depth = depth;
    w->stack_count++;
    return true;
}

/* The steps from entry IO `entry` of the traced behavior `root_id`. */
static bool trace_add_entry_steps(nmo_cli_record_array_t *steps,
                                  const nmo_cmd_behavior_flow_ctx_t *f,
                                  nmo_object_id_t root_id,
                                  nmo_object_id_t entry,
                                  uint32_t max_depth,
                                  trace_walk_t *w)
{
    size_t io_total = 0;
    const nmo_script_io_t *ios = nmo_script_model_ios(f->model, &io_total);
    w->visited_count = 0;
    w->stack_count = 0;
    bool ok = trace_walk_push(w, entry, 0);
    while (ok && w->stack_count > 0) {
        struct trace_pending cur = w->stack[--w->stack_count];
        if (cur.depth > max_depth) continue;

        size_t link_count = 0;
        const nmo_script_link_t *const *links =
            nmo_script_model_links_from_io(f->model, cur.io, &link_count);
        for (size_t li = 0; ok && li < link_count; li++) {
            const nmo_script_link_t *link = links[li];
            /* Only the links of the traced behavior and the graphs below it */
            if (link->graph_id != root_id &&
                !nmo_script_model_is_ancestor(f->model, root_id, link->graph_id)) {
                continue;
            }
            const nmo_script_node_t *target =
                nmo_script_model_find_node(f->model, link->target_node_id);
            const nmo_script_io_t *target_io =
                nmo_script_model_find_io(f->model, link->target_io_id);

            /* Whether the target's outputs lead somewhere new */
            nmo_object_id_t loop_io = 0;
            bool loop_detected = false;
            bool has_unseen_continuation = false;
            for (size_t oi = 0; target && oi < target->output_count; oi++) {
                nmo_object_id_t output_id = ios[target->first_io + target->input_count + oi].id;
                if (trace_walk_seen(w, output_id)) {
                    loop_detected = true;
                    if (loop_io == 0) loop_io = output_id;
                } else {
                    has_unseen_continuation = true;
                }
            }
            const char *truncated_reason = NULL;
            if (cur.depth >= max_depth && has_unseen_continuation) {
                truncated_reason = "max_depth";
            } else if (loop_detected && !has_unseen_continuation) {
                truncated_reason = "loop";
            }

            trace_step_t step = {
                .depth = cur.depth,
                .source_io = cur.io,
                .source_owner = link->source_node_id,
                .target_io = link->target_io_id,
                .target_io_name = target_io ? target_io->name
                                            : resolve_name(f->repo, link->target_io_id),
                .target_owner = link->target_node_id,
                .target = target,
                .transition = trace_transition_name(root_id, link->source_node_id,
                                                    link->target_node_id, target),
                .delay = link->activation_delay,
                .truncated_reason = truncated_reason,
                .loop_detected = loop_detected,
                .loop_io = loop_io,
            };
            ok = trace_add_step(steps, f, &step);

            /* Continue through the target's outputs */
            for (size_t oi = 0; ok && target && cur.depth < max_depth &&
                                oi < target->output_count; oi++) {
                nmo_object_id_t output_id = ios[target->first_io + target->input_count + oi].id;
                if (!trace_walk_seen(w, output_id)) {
                    ok = trace_walk_push(w, output_id, cur.depth + 1);
                }
            }
        }
    }
    return ok;
}

int nmo_cmd_behavior_trace(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--from",  NULL, NMO_OPT_STRING, "Start IO name (default: first bIn)"},
        {"--depth", "-d", NMO_OPT_UINT,   "Max trace depth (default: unlimited)"},
        NMO_OPT_DEF_JSON,
        {"--id",    "-i", NMO_OPT_UINT,   "Behavior object ID"},
        {"--name",  "-n", NMO_OPT_STRING, "Behavior object name"},
    };
    enum { OPT_FROM, OPT_DEPTH, OPT_JSON, OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *from_name = nmo_opt_str(&vals[OPT_FROM]);
    uint32_t max_trace_depth = nmo_opt_uint_or(&vals[OPT_DEPTH], 64);

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = has_selector_opt ? NULL : (r.pos_count >= 2 ? r.pos_args[0] : NULL);
    if ((has_selector_opt && r.pos_count < 1) || (!has_selector_opt && positional_id == NULL)) {
        fprintf(stderr, "Usage: nmo behavior trace [--from <io>] [--depth N] [--id <id> | --name <name> | <id>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
        .positional_id = positional_id,
        .name = nmo_opt_str(&vals[OPT_NAME]),
        .required_base_class = NMO_CID_BEHAVIOR,
        .selector_label = "Behavior",
        .type_label = "CKBehavior",
    };
    nmo_object_t *beh = NULL;
    nmo_object_id_t beh_id = 0;
    rc = nmo_core_resolve_one_object(&c, &selector, &beh, &beh_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo behavior trace [--from <io>] [--depth N] [--id <id> | --name <name> | <id>] <file>\n");
        return nmo_cmd_ctx_done(&c, rc);
    }

    nmo_cmd_behavior_flow_ctx_t flow;
    if (!nmo_cmd_behavior_flow_init(&flow, &c)) {
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    const nmo_script_node_t *root = nmo_script_model_find_node(flow.model, beh_id);
    if (!root) {
        fprintf(stderr, "Error: Behavior %u has no state\n", beh_id);
        nmo_cmd_behavior_flow_dispose(&flow);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }
    const char *beh_name = root->name;
    size_t link_count = trace_count_links(flow.model, beh_id, 0);

    if (link_count == 0) {
        nmo_cli_record_t *rec = trace_record_new(beh_id, beh_name, 0, 0);
        bool ok = rec != NULL &&
                  nmo_cli_record_raw(rec, "No behavior links to trace.\n") &&
                  trace_inline_array(rec, "entries") != NULL;
        if (!ok) {
            nmo_cli_record_free(rec);
            rec = NULL;
        }
        rc = nmo_cmd_ctx_emit_record(&c, rec, "behavior.trace", 0, false);
        nmo_cmd_behavior_flow_dispose(&flow);
        return nmo_cmd_ctx_done(&c, rc);
    }

    /* Entry points: the behavior's inputs with links, matching --from */
    size_t io_total = 0;
    const nmo_script_io_t *ios = nmo_script_model_ios(flow.model, &io_total);
    nmo_object_id_t *entry_ios = (nmo_object_id_t *)malloc(
        (root->input_count ? root->input_count : 1u) * sizeof(*entry_ios));
    size_t entry_count = 0;
    bool from_matched = false;
    for (size_t i = 0; entry_ios && i < root->input_count; i++) {
        const nmo_script_io_t *io = &ios[root->first_io + i];
        if (from_name && !nmo_tool_match_wildcard_ci(from_name, io->name)) {
            continue;
        }
        from_matched = true;
        size_t out_count = 0;
        nmo_script_model_links_from_io(flow.model, io->id, &out_count);
        if (out_count > 0) {
            entry_ios[entry_count++] = io->id;
        }
    }
    int entry_rc = NMO_CLI_EXIT_SUCCESS;
    if (!entry_ios) {
        entry_rc = NMO_CLI_EXIT_INTERNAL_ERROR;
    } else if (root->input_count == 0) {
        fprintf(stderr, "Error: Behavior has no input IOs\n");
        entry_rc = NMO_CLI_EXIT_ARG_ERROR;
    } else if (from_name && !from_matched) {
        fprintf(stderr, "Error: IO '%s' not found\n", from_name);
        entry_rc = NMO_CLI_EXIT_ARG_ERROR;
    } else if (from_name && entry_count == 0) {
        fprintf(stderr, "Error: No entry IO matching '%s'\n", from_name);
        entry_rc = NMO_CLI_EXIT_ARG_ERROR;
    }
    if (entry_rc != NMO_CLI_EXIT_SUCCESS) {
        free(entry_ios);
        nmo_cmd_behavior_flow_dispose(&flow);
        return nmo_cmd_ctx_done(&c, entry_rc);
    }

    nmo_cli_record_t *rec = trace_record_new(beh_id, beh_name, entry_count, link_count);
    const char *root_name = beh_name[0] ? beh_name : "(unnamed)";
    bool ok = rec != NULL &&
              nmo_cli_record_raw_fmt(rec,
                                     "Execution Trace: %s [%s] [#%u]\n"
                                     "Current graph: %s [#%u]\n"
                                     "Entry points: %zu, Links: %zu\n\n",
                                     root_name, nmo_script_node_kind_name(root->kind), beh_id,
                                     root_name, beh_id,
                                     entry_count, link_count);
    nmo_cli_record_array_t *entries = ok ? trace_inline_array(rec, "entries") : NULL;
    ok = entries != NULL;

    trace_walk_t walk = {0};
    char *root_label = nmo_cmd_behavior_node_label_dup(&flow, beh_id);
    ok = ok && root_label != NULL;
    for (size_t ei = 0; ok && ei < entry_count; ei++) {
        nmo_object_id_t entry = entry_ios[ei];
        const char *entry_name = nmo_script_model_find_io(flow.model, entry)->name;
        nmo_cli_record_t *entry_item = nmo_cli_record_new();
        ok = entry_item != NULL &&
             nmo_cli_record_uint(entry_item, "entry_io_id", NULL, entry) &&
             nmo_cli_record_str(entry_item, "entry_io_name", NULL, entry_name) &&
             nmo_cli_record_uint(entry_item, "entry_owner_id", NULL, beh_id) &&
             nmo_cli_record_str(entry_item, "entry_owner_name", NULL, beh_name) &&
             nmo_cli_record_raw_fmt(entry_item, "%s.%s\n", root_label, entry_name);
        nmo_cli_record_array_t *steps = ok ? trace_inline_array(entry_item, "steps") : NULL;
        ok = steps != NULL &&
             trace_add_entry_steps(steps, &flow, beh_id, entry, max_trace_depth, &walk) &&
             nmo_cli_record_raw(entry_item, "\n");
        if (!ok) {
            nmo_cli_record_free(entry_item);
        } else {
            ok = nmo_cli_record_array_add(entries, entry_item);
        }
    }
    free(root_label);
    free(walk.visited);
    free(walk.stack);
    free(entry_ios);
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    rc = nmo_cmd_ctx_emit_record(&c, rec, "behavior.trace", 0, false);
    nmo_cmd_behavior_flow_dispose(&flow);
    return nmo_cmd_ctx_done(&c, rc);
}
