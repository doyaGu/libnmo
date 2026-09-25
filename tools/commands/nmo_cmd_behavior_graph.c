/**
 * @file nmo_cmd_behavior_graph.c
 * @brief CLI behavior graph and dump command implementations
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
#include "behavior/nmo_behavior_analyze.h"
#include "behavior/nmo_behavior_view.h"
#include "runtime/nmo_context.h"
#include "format/nmo_interface_chunk.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameterlocal_schemas.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_system.h"
#include "behavior/nmo_behavior_registry.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef nmo_behavior_graph_node_t nmo_cli_graph_node_t;
typedef nmo_behavior_graph_edge_t nmo_cli_graph_edge_t;

static bool parse_behavior_graph_args(int argc, char **argv,
                                      bool expect_file_operand,
                                      nmo_core_object_selector_t *out_selector,
                                      const char **out_file,
                                      bool *out_dot,
                                      size_t *out_max_nodes,
                                      size_t *out_max_edges,
                                      uint32_t *out_depth)
{
    static const nmo_opt_def_t opts[] = {
        {"--dot",       NULL, NMO_OPT_FLAG, "Emit DOT graph output"},
        {"--max-nodes", NULL, NMO_OPT_UINT, "Max nodes to display"},
        {"--max-edges", NULL, NMO_OPT_UINT, "Max edges to display"},
        {"--depth",     "-d", NMO_OPT_UINT, "Recursion depth (default: unlimited)"},
        {"--json",      "-j", NMO_OPT_FLAG, "JSON output"},
        {"--id",        "-i", NMO_OPT_UINT, "Behavior object ID"},
        {"--name",      "-n", NMO_OPT_STRING, "Behavior object name"},
    };
    enum { OPT_DOT, OPT_MAX_NODES, OPT_MAX_EDGES, OPT_DEPTH, OPT_JSON,
           OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return false;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = NULL;
    const char *file_path = NULL;
    if (expect_file_operand) {
        if ((has_selector_opt && r.pos_count < 1) || (!has_selector_opt && r.pos_count < 2)) {
            return false;
        }
        positional_id = has_selector_opt ? NULL : r.pos_args[0];
        file_path = r.pos_args[r.pos_count - 1];
    } else if (has_selector_opt) {
        if (r.pos_count != 0) {
            return false;
        }
    } else {
        if (r.pos_count != 1) {
            return false;
        }
        positional_id = r.pos_args[0];
    }

    if (out_selector) {
        *out_selector = (nmo_core_object_selector_t){
            .has_id = vals[OPT_ID].present,
            .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
            .positional_id = positional_id,
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .required_base_class = NMO_CID_BEHAVIOR,
            .selector_label = "Behavior",
            .type_label = "CKBehavior",
        };
    }
    if (out_file) *out_file = file_path;
    if (out_dot) *out_dot = vals[OPT_DOT].val.flag;
    if (out_max_nodes) *out_max_nodes = vals[OPT_MAX_NODES].present ? (size_t)vals[OPT_MAX_NODES].val.u : 0;
    if (out_max_edges) *out_max_edges = vals[OPT_MAX_EDGES].present ? (size_t)vals[OPT_MAX_EDGES].val.u : 0;
    if (out_depth) *out_depth = vals[OPT_DEPTH].present ? vals[OPT_DEPTH].val.u : UINT32_MAX;
    return true;
}

static bool node_id_in_set(const nmo_object_id_t *ids, size_t count, nmo_object_id_t id) {
    if (!ids || id == 0) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

static const nmo_cli_graph_node_t *find_graph_node(
    const nmo_cli_graph_node_t *nodes,
    size_t count,
    nmo_object_id_t id)
{
    if (!nodes || id == 0) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        if (nodes[i].id == id) {
            return &nodes[i];
        }
    }
    return NULL;
}

static const char *behavior_type_name(const nmo_behavior_state_t *bs) {
    if (!bs) {
        return "Unknown";
    }
    if (bs->flags & CKBEHAVIOR_SCRIPT) {
        return "Script";
    }
    if (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) {
        return "BB";
    }
    return "Graph";
}

static const nmo_behavior_state_t *get_behavior_state_for_id(
    nmo_object_repository_t *repo,
    nmo_object_id_t id)
{
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, id);
    if (!obj) {
        return NULL;
    }
    return (const nmo_behavior_state_t *)nmo_object_get_state(obj);
}

static const nmo_parameteroperation_state_t *get_operation_state_for_id(
    nmo_object_repository_t *repo,
    nmo_object_id_t id)
{
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, id);
    if (!obj) {
        return NULL;
    }
    return (const nmo_parameteroperation_state_t *)nmo_object_get_state(obj);
}

/* malloc'd display name for a graph node ("" when nothing is known); free() it. */
static char *graph_node_display_name_dup(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    const nmo_behavior_registry_t *bb_reg,
    const nmo_cli_graph_node_t *node)
{
    if (!node) {
        return nmo_tool_strdup("");
    }

    if (node->kind && strcmp(node->kind, "operation") == 0) {
        const nmo_parameteroperation_state_t *op_state =
            get_operation_state_for_id(repo, node->id);
        if (op_state) {
            const char *op_name =
                nmo_type_registry_guid_to_name(reg, op_state->operation_guid);
            if (op_name && op_name[0]) {
                return nmo_tool_strdup(op_name);
            }
            return nmo_tool_strdup_fmt("%08X-%08X",
                                       op_state->operation_guid.d1,
                                       op_state->operation_guid.d2);
        }
    }

    if (node->kind && strcmp(node->kind, "behavior") == 0) {
        const nmo_behavior_state_t *bs = get_behavior_state_for_id(repo, node->id);
        if (bs && (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) &&
            !nmo_guid_is_null(bs->block_guid)) {
            const char *proto = nmo_behavior_registry_get_name(bb_reg, bs->block_guid);
            if (proto && proto[0]) {
                return nmo_tool_strdup(proto);
            }
        }
    }

    if (node->name && node->name[0]) {
        return nmo_tool_strdup(node->name);
    }
    if (node->class_name && node->class_name[0]) {
        return nmo_tool_strdup(node->class_name);
    }
    return nmo_tool_strdup(node->kind ? node->kind : "");
}

static bool graph_edge_is_parameter_kind(const char *kind) {
    return kind &&
        (strcmp(kind, "param_local") == 0 ||
         strcmp(kind, "param_in") == 0 ||
         strcmp(kind, "param_out") == 0 ||
         strcmp(kind, "param_source") == 0 ||
         strcmp(kind, "param_dest") == 0 ||
         strcmp(kind, "op_in1") == 0 ||
         strcmp(kind, "op_in2") == 0 ||
         strcmp(kind, "op_out") == 0);
}

static nmo_object_id_t graph_edge_parameter_id(const nmo_cli_graph_edge_t *edge) {
    if (!edge || !edge->kind) {
        return 0;
    }
    if (strcmp(edge->kind, "param_out") == 0 ||
        strcmp(edge->kind, "param_in") == 0 ||
        strcmp(edge->kind, "param_local") == 0 ||
        strcmp(edge->kind, "op_out") == 0 ||
        strcmp(edge->kind, "param_source") == 0 ||
        strcmp(edge->kind, "param_dest") == 0) {
        return edge->to_id;
    }
    return edge->from_id;
}

/* Node and edge tallies by kind. */
typedef struct behavior_graph_counts {
    size_t node_behavior;
    size_t node_parameter;
    size_t node_operation;
    size_t node_io;
    size_t node_unknown;
    size_t edge_behavior_link;
    size_t edge_io_link;
    size_t edge_param_in;
    size_t edge_param_out;
    size_t edge_param_local;
    size_t edge_param_dest;
    size_t edge_param_source;
    size_t edge_op_in1;
    size_t edge_op_in2;
    size_t edge_op_out;
} behavior_graph_counts_t;

static void behavior_graph_count(const nmo_behavior_graph_t *graph,
                                 behavior_graph_counts_t *counts)
{
    *counts = (behavior_graph_counts_t){0};
    for (size_t i = 0; i < graph->node_count; ++i) {
        const char *kind = graph->nodes[i].kind;
        if (!kind) {
            counts->node_unknown++;
        } else if (strcmp(kind, "behavior") == 0) {
            counts->node_behavior++;
        } else if (strcmp(kind, "parameter") == 0) {
            counts->node_parameter++;
        } else if (strcmp(kind, "operation") == 0) {
            counts->node_operation++;
        } else if (strcmp(kind, "io") == 0) {
            counts->node_io++;
        } else {
            counts->node_unknown++;
        }
    }

    for (size_t i = 0; i < graph->edge_count; ++i) {
        const char *kind = graph->edges[i].kind ? graph->edges[i].kind : "";
        if (strcmp(kind, "behavior_link") == 0) {
            counts->edge_behavior_link++;
        } else if (strcmp(kind, "io_link") == 0) {
            counts->edge_io_link++;
        } else if (strcmp(kind, "param_in") == 0) {
            counts->edge_param_in++;
        } else if (strcmp(kind, "param_out") == 0) {
            counts->edge_param_out++;
        } else if (strcmp(kind, "param_local") == 0) {
            counts->edge_param_local++;
        } else if (strcmp(kind, "param_dest") == 0) {
            counts->edge_param_dest++;
        } else if (strcmp(kind, "param_source") == 0) {
            counts->edge_param_source++;
        } else if (strcmp(kind, "op_in1") == 0) {
            counts->edge_op_in1++;
        } else if (strcmp(kind, "op_in2") == 0) {
            counts->edge_op_in2++;
        } else if (strcmp(kind, "op_out") == 0) {
            counts->edge_op_out++;
        }
    }
}

/* Everything the behavior graph report reads. */
typedef struct behavior_graph_report {
    nmo_cmd_ctx_t *c;
    nmo_object_repository_t *repo;
    const nmo_behavior_registry_t *bb_reg;
    const nmo_behavior_graph_t *graph;
    nmo_object_id_t behavior_id;
    behavior_graph_counts_t counts;
    size_t emit_node_count;
    const size_t *emit_edge_indices;
    size_t emit_edge_count;
    bool nodes_truncated;
    bool edges_truncated;
} behavior_graph_report_t;

static const nmo_cli_graph_edge_t *behavior_graph_emit_edge(
    const behavior_graph_report_t *r,
    size_t i)
{
    return &r->graph->edges[r->emit_edge_indices ? r->emit_edge_indices[i] : i];
}

/* The parameter whose type labels a parameter edge in text and DOT. */
static nmo_object_id_t graph_edge_text_parameter_id(const nmo_cli_graph_edge_t *edge) {
    if (strcmp(edge->kind, "param_out") == 0 ||
        strcmp(edge->kind, "op_out") == 0) {
        return edge->to_id;
    }
    return edge->from_id;
}

/* Registry name of an operation node's operation, or NULL. */
static const char *graph_node_operation_type(const behavior_graph_report_t *r,
                                             const nmo_cli_graph_node_t *node)
{
    if (!node->kind || strcmp(node->kind, "operation") != 0) {
        return NULL;
    }
    nmo_object_t *op_obj = nmo_object_repository_find_by_id(r->repo, node->id);
    if (!op_obj || !op_obj->state) {
        return NULL;
    }
    const nmo_parameteroperation_state_t *op_state =
        (const nmo_parameteroperation_state_t *)op_obj->state;
    return nmo_type_registry_guid_to_name(r->c->registry, op_state->operation_guid);
}

/* Append `item` to `arr`, freeing it when building it failed. */
static bool behavior_graph_add_item(nmo_cli_record_array_t *arr,
                                    nmo_cli_record_t *item,
                                    bool ok)
{
    if (!ok) {
        nmo_cli_record_free(item);
        return false;
    }
    return nmo_cli_record_array_add(arr, item);
}

/* Identity and node/edge tallies. */
static bool behavior_graph_add_summary(nmo_cli_record_t *rec,
                                       const behavior_graph_report_t *r)
{
    const nmo_behavior_graph_t *graph = r->graph;
    const behavior_graph_counts_t *n = &r->counts;
    const char *behavior_name = graph->behavior_name;
    const char *behavior_class = graph->behavior_class_name;

    bool ok = nmo_cli_record_title(rec, "Behavior Graph") &&
              nmo_cli_record_raw_fmt(rec, "\nBehavior %u: %s [%s]\n\n",
                                     r->behavior_id,
                                     (behavior_name && behavior_name[0]) ? behavior_name : "(unnamed)",
                                     behavior_class ? behavior_class : "?") &&
              nmo_cli_record_uint(rec, "behavior_id", NULL, r->behavior_id) &&
              nmo_cmd_behavior_add_interface_diagnostics(rec, r->c->workspace, false);
    if (behavior_name && behavior_name[0]) {
        ok = ok && nmo_cli_record_str(rec, "behavior_name", NULL, behavior_name);
    }
    if (graph->behavior_class_id != 0) {
        ok = ok && nmo_cli_record_uint(rec, "behavior_class_id", NULL,
                                       (uint64_t)graph->behavior_class_id);
    }
    if (behavior_class) {
        ok = ok && nmo_cli_record_str(rec, "behavior_class", NULL, behavior_class);
    }

    nmo_cli_record_t *counts = ok ? nmo_cli_record_object(rec, "counts") : NULL;
    ok = counts != NULL &&
         nmo_cli_record_uint(counts, "nodes_total", NULL, (uint64_t)graph->node_count) &&
         nmo_cli_record_uint(counts, "edges_total", NULL, (uint64_t)graph->edge_count) &&
         nmo_cli_record_uint(counts, "broken_links", NULL, (uint64_t)graph->broken_links) &&
         nmo_cli_record_uint(counts, "missing_nodes", NULL, (uint64_t)graph->missing_nodes) &&
         nmo_cli_record_uint(counts, "cycles", NULL, (uint64_t)graph->cycle_count);

    nmo_cli_record_t *nodes_by_kind = ok ? nmo_cli_record_object(counts, "nodes_by_kind") : NULL;
    ok = nodes_by_kind != NULL &&
         nmo_cli_record_uint(nodes_by_kind, "behavior", NULL, (uint64_t)n->node_behavior) &&
         nmo_cli_record_uint(nodes_by_kind, "parameter", NULL, (uint64_t)n->node_parameter) &&
         nmo_cli_record_uint(nodes_by_kind, "operation", NULL, (uint64_t)n->node_operation) &&
         nmo_cli_record_uint(nodes_by_kind, "io", NULL, (uint64_t)n->node_io) &&
         nmo_cli_record_uint(nodes_by_kind, "unknown", NULL, (uint64_t)n->node_unknown);

    nmo_cli_record_t *edges_by_kind = ok ? nmo_cli_record_object(counts, "edges_by_kind") : NULL;
    ok = edges_by_kind != NULL &&
         nmo_cli_record_uint(edges_by_kind, "behavior_link", NULL, (uint64_t)n->edge_behavior_link) &&
         nmo_cli_record_uint(edges_by_kind, "io_link", NULL, (uint64_t)n->edge_io_link) &&
         nmo_cli_record_uint(edges_by_kind, "param_in", NULL, (uint64_t)n->edge_param_in) &&
         nmo_cli_record_uint(edges_by_kind, "param_out", NULL, (uint64_t)n->edge_param_out) &&
         nmo_cli_record_uint(edges_by_kind, "param_local", NULL, (uint64_t)n->edge_param_local) &&
         nmo_cli_record_uint(edges_by_kind, "param_dest", NULL, (uint64_t)n->edge_param_dest) &&
         nmo_cli_record_uint(edges_by_kind, "param_source", NULL, (uint64_t)n->edge_param_source) &&
         nmo_cli_record_uint(edges_by_kind, "op_in1", NULL, (uint64_t)n->edge_op_in1) &&
         nmo_cli_record_uint(edges_by_kind, "op_in2", NULL, (uint64_t)n->edge_op_in2) &&
         nmo_cli_record_uint(edges_by_kind, "op_out", NULL, (uint64_t)n->edge_op_out);

    ok = ok &&
         nmo_cli_record_raw_fmt(rec, "Nodes: %zu (behavior %zu, parameter %zu, operation %zu, io %zu, unknown %zu)\n",
                                graph->node_count, n->node_behavior, n->node_parameter,
                                n->node_operation, n->node_io, n->node_unknown) &&
         nmo_cli_record_raw_fmt(rec, "Edges: %zu (behavior links %zu, io links %zu, param %zu, op %zu)\n",
                                graph->edge_count,
                                n->edge_behavior_link,
                                n->edge_io_link,
                                (n->edge_param_in + n->edge_param_out + n->edge_param_local +
                                 n->edge_param_dest + n->edge_param_source),
                                (n->edge_op_in1 + n->edge_op_in2 + n->edge_op_out));
    if (r->nodes_truncated) {
        ok = ok && nmo_cli_record_raw_fmt(rec, "Note: Nodes truncated to %zu (use --max-nodes 0 to disable)\n",
                                          r->emit_node_count);
    }
    if (r->edges_truncated) {
        ok = ok && nmo_cli_record_raw_fmt(rec, "Note: Edges truncated to %zu (use --max-edges 0 to disable)\n",
                                          r->emit_edge_count);
    }
    if (graph->broken_links > 0) {
        ok = ok && nmo_cli_record_raw_fmt(rec, "Broken links: %zu\n", graph->broken_links);
    }
    if (graph->missing_nodes > 0) {
        ok = ok && nmo_cli_record_raw_fmt(rec, "Missing objects: %zu\n", graph->missing_nodes);
    }
    if (graph->cycle_count > 0) {
        ok = ok && nmo_cli_record_raw_fmt(rec, "Behavior cycles: %zu\n", graph->cycle_count);
    }
    return ok && nmo_cli_record_raw(rec, "\n");
}

/* One node: JSON details and an ID/D/Kind/Name/Class table row. */
static bool behavior_graph_add_node(nmo_cli_record_array_t *arr,
                                    const behavior_graph_report_t *r,
                                    const nmo_cli_graph_node_t *node)
{
    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item != NULL &&
        nmo_cli_record_uint(item, "id", NULL, node->id) &&
        nmo_cli_record_text_fmt(item, "ID", "%u", node->id) &&
        nmo_cli_record_text_fmt(item, "D", "%u", node->depth) &&
        nmo_cli_record_text(item, "Kind", node->kind ? node->kind : "-");
    if (node->kind) {
        ok = ok && nmo_cli_record_str(item, "kind", NULL, node->kind);
    }
    if (node->name && node->name[0]) {
        ok = ok && nmo_cli_record_str(item, "name", NULL, node->name);
    }

    /* For operation nodes, resolve the operation name */
    const char *text_name = (node->name && node->name[0]) ? node->name : "-";
    const char *op_type = graph_node_operation_type(r, node);
    if (op_type && strcmp(text_name, "-") != 0) {
        ok = ok && nmo_cli_record_text_fmt(item, "Name", "%s (%s)", text_name, op_type);
    } else {
        ok = ok && nmo_cli_record_text(item, "Name", op_type ? op_type : text_name);
    }
    ok = ok && nmo_cli_record_text(item, "Class",
                                   (node->class_name && node->class_name[0]) ? node->class_name : "-");

    char *display_name = ok ? graph_node_display_name_dup(
        r->repo, r->c->registry, r->bb_reg, node) : NULL;
    if (display_name && display_name[0]) {
        ok = ok && nmo_cli_record_str(item, "display_name", NULL, display_name);
    }
    free(display_name);
    if (node->class_id != 0) {
        ok = ok && nmo_cli_record_uint(item, "class_id", NULL, (uint64_t)node->class_id);
    }
    if (node->class_name && node->class_name[0]) {
        ok = ok && nmo_cli_record_str(item, "class_name", NULL, node->class_name);
    }
    ok = ok && nmo_cli_record_uint(item, "depth", NULL, (uint64_t)node->depth);
    if (node->parent_id != 0) {
        ok = ok && nmo_cli_record_uint(item, "parent_id", NULL, (uint64_t)node->parent_id);
    }

    if (node->kind && strcmp(node->kind, "behavior") == 0) {
        const nmo_behavior_state_t *bs = get_behavior_state_for_id(r->repo, node->id);
        ok = ok && nmo_cli_record_str(item, "behavior_type", NULL, behavior_type_name(bs));
        if (bs && (bs->flags & CKBEHAVIOR_BUILDINGBLOCK)) {
            const char *proto = nmo_behavior_registry_get_name(r->bb_reg, bs->block_guid);
            ok = ok &&
                nmo_cli_record_str_fmt(item, "bb_guid", NULL, "%08X-%08X",
                                       bs->block_guid.d1, bs->block_guid.d2) &&
                nmo_cli_record_uint(item, "bb_version", NULL, (uint64_t)bs->block_version) &&
                nmo_cli_record_str_opt(item, "bb_proto_name", NULL, proto, NULL);
        }
    } else if (node->kind && strcmp(node->kind, "operation") == 0) {
        const nmo_parameteroperation_state_t *op_state =
            get_operation_state_for_id(r->repo, node->id);
        if (op_state) {
            const nmo_guid_t op_guid = op_state->operation_guid;
            const char *op_name = nmo_type_registry_guid_to_name(r->c->registry, op_guid);
            ok = ok && nmo_cli_record_str_fmt(item, "operation_guid", NULL, "%08X-%08X",
                                              op_guid.d1, op_guid.d2);
            if (op_name && op_name[0]) {
                ok = ok && nmo_cli_record_str(item, "operation_name", NULL, op_name);
            } else {
                ok = ok && nmo_cli_record_str_fmt(item, "operation_name", NULL, "%08X-%08X",
                                                  op_guid.d1, op_guid.d2);
            }
            ok = ok &&
                nmo_cli_record_uint(item, "in1_id", NULL,
                                    op_state->has_in1 ? nmo_parameteroperation_in1_id(op_state) : 0) &&
                nmo_cli_record_uint(item, "in2_id", NULL,
                                    op_state->has_in2 ? nmo_parameteroperation_in2_id(op_state) : 0) &&
                nmo_cli_record_uint(item, "out_id", NULL,
                                    op_state->has_out ? nmo_parameteroperation_out_id(op_state) : 0);
        }
    }
    return behavior_graph_add_item(arr, item, ok);
}

/* Text: the Meta column of an edge row. */
static bool behavior_graph_add_edge_meta(nmo_cli_record_t *item,
                                         const behavior_graph_report_t *r,
                                         const nmo_cli_graph_edge_t *edge)
{
    /* in_io = source, out_io = target (Virtools SDK naming) */
    if (edge->kind && strcmp(edge->kind, "behavior_link") == 0) {
        const char *src_io = resolve_name(r->repo, edge->in_io_id);
        const char *tgt_io = resolve_name(r->repo, edge->out_io_id);
        if (edge->activation_delay != 0 || edge->initial_activation_delay != 0) {
            return nmo_cli_record_text_fmt(item, "Meta", "%s->%s %d/%d",
                                           src_io, tgt_io,
                                           edge->activation_delay,
                                           edge->initial_activation_delay);
        }
        return nmo_cli_record_text_fmt(item, "Meta", "%s->%s", src_io, tgt_io);
    }
    if (edge->kind && strcmp(edge->kind, "io_link") == 0) {
        return nmo_cli_record_text_fmt(item, "Meta", "%s->%s",
                                       resolve_name(r->repo, edge->in_io_id),
                                       resolve_name(r->repo, edge->out_io_id));
    }
    if (graph_edge_is_parameter_kind(edge->kind)) {
        /* Look up the parameter node to get its type name */
        nmo_object_t *param_obj = nmo_object_repository_find_by_id(
            r->repo, graph_edge_text_parameter_id(edge));
        const char *tname = resolve_type(r->c->registry, get_param_type_guid(param_obj));
        if (edge->is_shared) {
            return nmo_cli_record_text_fmt(item, "Meta", "%s (shared)", tname);
        }
        return nmo_cli_record_text(item, "Meta", tname);
    }
    return nmo_cli_record_text(item, "Meta", "-");
}

/* Text: a From/To cell, "<id>:<name>" for a named node. */
static bool behavior_graph_add_endpoint_cell(nmo_cli_record_t *item,
                                             const char *label,
                                             const nmo_cli_graph_node_t *node,
                                             nmo_object_id_t id)
{
    if (node && node->name && node->name[0]) {
        return nmo_cli_record_text_fmt(item, label, "%u:%s", node->id, node->name);
    }
    return nmo_cli_record_text_fmt(item, label, "%u", id);
}

/* One edge: JSON details and a From/To/Kind/Field/Link/Meta table row. */
static bool behavior_graph_add_edge(nmo_cli_record_array_t *arr,
                                    const behavior_graph_report_t *r,
                                    const nmo_cli_graph_edge_t *edge)
{
    const nmo_behavior_graph_t *graph = r->graph;
    const nmo_cli_graph_node_t *from_node =
        find_graph_node(graph->nodes, graph->node_count, edge->from_id);
    const nmo_cli_graph_node_t *to_node =
        find_graph_node(graph->nodes, graph->node_count, edge->to_id);
    char *from_name = graph_node_display_name_dup(r->repo, r->c->registry, r->bb_reg, from_node);
    char *to_name = graph_node_display_name_dup(r->repo, r->c->registry, r->bb_reg, to_node);

    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item != NULL &&
        nmo_cli_record_uint(item, "from", NULL, edge->from_id) &&
        nmo_cli_record_uint(item, "to", NULL, edge->to_id) &&
        behavior_graph_add_endpoint_cell(item, "From", from_node, edge->from_id) &&
        behavior_graph_add_endpoint_cell(item, "To", to_node, edge->to_id) &&
        nmo_cli_record_text(item, "Kind", edge->kind ? edge->kind : "-") &&
        nmo_cli_record_text(item, "Field", edge->field_path ? edge->field_path : "-");
    if (edge->link_id != 0) {
        ok = ok && nmo_cli_record_text_fmt(item, "Link", "%u", edge->link_id);
    } else {
        ok = ok && nmo_cli_record_text(item, "Link", "-");
    }
    ok = ok && behavior_graph_add_edge_meta(item, r, edge);

    ok = ok &&
        nmo_cli_record_str_opt(item, "from_name", NULL, from_name, NULL) &&
        nmo_cli_record_str_opt(item, "to_name", NULL, to_name, NULL);
    if (edge->kind) {
        ok = ok && nmo_cli_record_str(item, "kind", NULL, edge->kind);
    }
    if (edge->field_path) {
        ok = ok && nmo_cli_record_str(item, "field_path", NULL, edge->field_path);
    }
    if (edge->link_id != 0) {
        ok = ok && nmo_cli_record_uint(item, "link_id", NULL, edge->link_id);
    }
    if (edge->in_io_id != 0) {
        ok = ok && nmo_cli_record_uint(item, "in_io_id", NULL, edge->in_io_id);
    }
    if (edge->out_io_id != 0) {
        ok = ok && nmo_cli_record_uint(item, "out_io_id", NULL, edge->out_io_id);
    }
    if (edge->kind && strcmp(edge->kind, "behavior_link") == 0) {
        ok = ok &&
            nmo_cli_record_int(item, "activation_delay", NULL, edge->activation_delay) &&
            nmo_cli_record_int(item, "initial_activation_delay", NULL,
                               edge->initial_activation_delay) &&
            nmo_cli_record_str(item, "source_io_name", NULL, resolve_name(r->repo, edge->in_io_id)) &&
            nmo_cli_record_str(item, "target_io_name", NULL, resolve_name(r->repo, edge->out_io_id)) &&
            nmo_cli_record_uint(item, "source_owner_id", NULL, edge->from_id) &&
            nmo_cli_record_uint(item, "target_owner_id", NULL, edge->to_id) &&
            nmo_cli_record_str_opt(item, "source_owner_name", NULL, from_name, NULL) &&
            nmo_cli_record_str_opt(item, "target_owner_name", NULL, to_name, NULL);
    }
    if (graph_edge_is_parameter_kind(edge->kind)) {
        nmo_object_id_t param_id = graph_edge_parameter_id(edge);
        nmo_object_t *param_obj = nmo_object_repository_find_by_id(r->repo, param_id);
        nmo_guid_t type_guid = get_param_type_guid(param_obj);
        ok = ok &&
            nmo_cli_record_uint(item, "parameter_id", NULL, param_id) &&
            nmo_cli_record_str(item, "parameter_name", NULL, resolve_name(r->repo, param_id)) &&
            nmo_cli_record_str_fmt(item, "type_guid", NULL, "%08X-%08X",
                                   type_guid.d1, type_guid.d2) &&
            nmo_cli_record_str(item, "type_name", NULL, resolve_type(r->c->registry, type_guid));
    }
    if (edge->is_shared) {
        ok = ok && nmo_cli_record_bool(item, "is_shared", NULL, true);
    }
    free(from_name);
    free(to_name);
    return behavior_graph_add_item(arr, item, ok);
}

/* "graph": the emitted nodes and edges, shown in text as two tables. */
static bool behavior_graph_add_tables(nmo_cli_record_t *rec,
                                      const behavior_graph_report_t *r)
{
    static const nmo_cli_table_col_t node_columns[] = {
        {"ID", NMO_CLI_ALIGN_RIGHT, 6, 0},
        {"D", NMO_CLI_ALIGN_RIGHT, 2, 0},
        {"Kind", NMO_CLI_ALIGN_LEFT, 12, 16},
        {"Name", NMO_CLI_ALIGN_LEFT, 22, 50},
        {"Class", NMO_CLI_ALIGN_LEFT, 20, 40},
    };
    static const nmo_cli_table_col_t edge_columns[] = {
        {"From", NMO_CLI_ALIGN_LEFT, 18, 32},
        {"To", NMO_CLI_ALIGN_LEFT, 18, 32},
        {"Kind", NMO_CLI_ALIGN_LEFT, 14, 18},
        {"Field", NMO_CLI_ALIGN_LEFT, 18, 24},
        {"Link", NMO_CLI_ALIGN_RIGHT, 6, 0},
        {"Meta", NMO_CLI_ALIGN_LEFT, 16, 32},
    };

    nmo_cli_record_t *graph_rec = nmo_cli_record_object(rec, "graph");
    nmo_cli_record_array_t *nodes = graph_rec ? nmo_cli_record_array(graph_rec, "nodes", NULL) : NULL;
    bool ok = nodes != NULL &&
              nmo_cli_record_array_set_table(nodes, node_columns,
                                             sizeof(node_columns) / sizeof(node_columns[0]));
    for (size_t i = 0; ok && i < r->emit_node_count; ++i) {
        ok = behavior_graph_add_node(nodes, r, &r->graph->nodes[i]);
    }

    ok = ok && nmo_cli_record_raw(graph_rec, "\n");
    nmo_cli_record_array_t *edges = ok ? nmo_cli_record_array(graph_rec, "edges", NULL) : NULL;
    ok = edges != NULL &&
         nmo_cli_record_array_set_table(edges, edge_columns,
                                        sizeof(edge_columns) / sizeof(edge_columns[0]));
    for (size_t i = 0; ok && i < r->emit_edge_count; ++i) {
        ok = behavior_graph_add_edge(edges, r, behavior_graph_emit_edge(r, i));
    }

    if (ok && (r->nodes_truncated || r->edges_truncated)) {
        nmo_cli_record_t *truncated = nmo_cli_record_object(graph_rec, "truncated");
        ok = truncated != NULL &&
             nmo_cli_record_bool(truncated, "nodes", NULL, r->nodes_truncated) &&
             nmo_cli_record_bool(truncated, "edges", NULL, r->edges_truncated) &&
             nmo_cli_record_uint(truncated, "nodes_emitted", NULL, (uint64_t)r->emit_node_count) &&
             nmo_cli_record_uint(truncated, "edges_emitted", NULL, (uint64_t)r->emit_edge_count) &&
             nmo_cli_record_uint(truncated, "nodes_dropped", NULL,
                                 (uint64_t)(r->graph->node_count - r->emit_node_count)) &&
             nmo_cli_record_uint(truncated, "edges_dropped", NULL,
                                 (uint64_t)(r->graph->edge_count - r->emit_edge_count));
    }
    return ok;
}

/* DOT label text, escaped for a double-quoted DOT string; free() it. */
static char *dot_label_dup(const char *label) {
    size_t len = label ? strlen(label) : 0;
    char *out = (char *)malloc(len * 2u + 1u);
    if (!out) {
        return NULL;
    }
    char *w = out;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)label[i];
        if (c == '"' || c == '\\') {
            *w++ = '\\';
            *w++ = (char)c;
        } else if (c == '\n' || c == '\r') {
            *w++ = '\\';
            *w++ = 'n';
        } else if (c == '\t') {
            *w++ = '\\';
            *w++ = 't';
        } else if (isprint(c)) {
            *w++ = (char)c;
        } else {
            *w++ = '?';
        }
    }
    *w = '\0';
    return out;
}

/* Fill color of a DOT node, by kind. */
static const char *behavior_graph_dot_fillcolor(const behavior_graph_report_t *r,
                                                const nmo_cli_graph_node_t *node)
{
    if (!node->kind) {
        return "white";
    }
    if (strcmp(node->kind, "behavior") == 0) {
        nmo_object_t *bobj = nmo_object_repository_find_by_id(r->repo, node->id);
        if (!bobj || !bobj->state) {
            return "lightyellow";
        }
        const nmo_behavior_state_t *bst = (const nmo_behavior_state_t *)bobj->state;
        if (bst->flags & CKBEHAVIOR_SCRIPT)
            return "lightgreen";
        if (bst->flags & CKBEHAVIOR_BUILDINGBLOCK)
            return "lightblue";
        return "lightyellow";
    }
    if (strcmp(node->kind, "parameter") == 0) {
        return "lemonchiffon";
    }
    if (strcmp(node->kind, "operation") == 0) {
        return "lightsalmon";
    }
    if (strcmp(node->kind, "io") == 0) {
        return "lightgray";
    }
    return "white";
}

static bool behavior_graph_add_dot_node(nmo_cli_record_t *rec,
                                        const behavior_graph_report_t *r,
                                        const nmo_interface_data_t *idata,
                                        const nmo_cli_graph_node_t *node)
{
    const char *label = (node->name && node->name[0]) ? node->name :
        (node->class_name && node->class_name[0]) ? node->class_name :
        (node->kind ? node->kind : "node");

    /* Resolve operation type for operation nodes */
    const char *op_type = graph_node_operation_type(r, node);
    if (op_type) {
        label = op_type;
    }

    char *escaped = dot_label_dup(label);
    bool ok = escaped != NULL &&
              nmo_cli_record_raw_fmt(rec, "  n%u [label=\"%s", node->id, escaped);
    free(escaped);

    /* Override color for script root from interface data */
    if (idata && idata->script.color != 0 && idata->script.behavior_id == node->id) {
        const uint32_t color = idata->script.color;
        ok = ok && nmo_cli_record_raw_fmt(rec, "\", fillcolor=\"#%02X%02X%02X\"",
                                          (unsigned)((color >> 16) & 0xFFu),
                                          (unsigned)((color >> 8) & 0xFFu),
                                          (unsigned)(color & 0xFFu));
    } else {
        ok = ok && nmo_cli_record_raw_fmt(rec, "\", fillcolor=\"%s\"",
                                          behavior_graph_dot_fillcolor(r, node));
    }

    /* Position from interface data */
    float px, py;
    bool has_pos = false;
    if (node->kind && strcmp(node->kind, "operation") == 0)
        has_pos = find_operation_position(idata, node->id, &px, &py);
    else
        has_pos = find_interface_position(idata, node->id, &px, &py);
    if (has_pos) {
        ok = ok && nmo_cli_record_raw_fmt(rec, ", pos=\"%.0f,%.0f!\"", px, -py);
    }
    return ok && nmo_cli_record_raw(rec, "];\n");
}

static bool behavior_graph_add_dot_edge(nmo_cli_record_t *rec,
                                        const behavior_graph_report_t *r,
                                        const nmo_interface_data_t *idata,
                                        const nmo_cli_graph_edge_t *edge)
{
    char *owned_label = NULL;
    const char *dot_edge_label;
    if (edge->kind && strcmp(edge->kind, "behavior_link") == 0) {
        owned_label = nmo_tool_strdup_fmt("%s->%s delay=%d/%d",
                                          resolve_name(r->repo, edge->in_io_id),
                                          resolve_name(r->repo, edge->out_io_id),
                                          edge->activation_delay,
                                          edge->initial_activation_delay);
        dot_edge_label = owned_label ? owned_label : "";
    } else if (graph_edge_is_parameter_kind(edge->kind)) {
        nmo_object_t *pobj = nmo_object_repository_find_by_id(
            r->repo, graph_edge_text_parameter_id(edge));
        dot_edge_label = resolve_type(r->c->registry, get_param_type_guid(pobj));
    } else {
        dot_edge_label = edge->kind ? edge->kind : "link";
    }

    const nmo_interface_link_t *ilink = find_interface_link(idata, edge->link_id);
    char *escaped = dot_label_dup(dot_edge_label);
    free(owned_label);
    bool ok = escaped != NULL &&
              nmo_cli_record_raw_fmt(rec, "  n%u -> n%u [label=\"%s\"%s];\n",
                                     edge->from_id, edge->to_id, escaped,
                                     (ilink && ilink->highlight) ? ", style=bold, color=red" : "");
    free(escaped);
    return ok;
}

/* Text: "DOT Graph", the emitted nodes and edges as a Graphviz digraph. */
static bool behavior_graph_add_dot(nmo_cli_record_t *rec, const behavior_graph_report_t *r)
{
    /* Look up interface data for the root behavior */
    const nmo_behavior_state_t *root_bs = get_behavior_state_for_id(r->repo, r->behavior_id);
    const nmo_interface_data_t *idata = root_bs ? root_bs->interface_data : NULL;

    bool ok = nmo_cli_record_heading(rec, "DOT Graph") &&
              nmo_cli_record_raw(rec, "\ndigraph behavior_graph {\n");
    if (idata) {
        ok = ok && nmo_cli_record_raw(rec, "  graph [layout=neato, overlap=false];\n");
    }
    ok = ok && nmo_cli_record_raw(rec, "  node [shape=box, fontname=\"Courier\", style=filled];\n");
    for (size_t i = 0; ok && i < r->emit_node_count; ++i) {
        ok = behavior_graph_add_dot_node(rec, r, idata, &r->graph->nodes[i]);
    }
    for (size_t i = 0; ok && i < r->emit_edge_count; ++i) {
        ok = behavior_graph_add_dot_edge(rec, r, idata, behavior_graph_emit_edge(r, i));
    }
    return ok && nmo_cli_record_raw(rec, "}\n");
}

static int behavior_graph_run(nmo_cmd_ctx_t *ctx,
                              const nmo_core_object_selector_t *selector,
                              bool emit_dot,
                              size_t max_nodes,
                              size_t max_edges,
                              uint32_t depth,
                              bool close_ctx,
                              const char *usage) {
    nmo_cmd_ctx_t c = *ctx;
    nmo_object_id_t behavior_id = 0;
    int exit_code = NMO_CLI_EXIT_SUCCESS;

    nmo_behavior_graph_t graph = {0};

    nmo_object_id_t *emit_node_ids = NULL;
    size_t *emit_edge_indices = NULL;

    if (nmo_tool_owner_ensure_behavior_acceleration(c.workspace) != NMO_OK) {
        fprintf(stderr, "Error: Failed to build behavior acceleration\n");
        exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
        goto cleanup;
    }

    nmo_object_t *behavior = NULL;
    int rc = nmo_core_resolve_one_object(&c, selector, &behavior, &behavior_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: %s\n", usage);
        exit_code = rc;
        goto cleanup;
    }

    if (!nmo_behavior_graph_build(c.workspace, behavior_id, depth, &graph)) {
        const char *detail = nmo_last_error_message();
        nmo_error_code_t code = nmo_last_error_code();
        if (detail && detail[0]) {
            fprintf(stderr, "Error: %s\n", detail);
        } else {
            fprintf(stderr, "Error: Failed to build behavior graph\n");
        }
        if (code == NMO_ERR_INVALID_ARGUMENT || code == NMO_ERR_NOT_FOUND) {
            exit_code = NMO_CLI_EXIT_ARG_ERROR;
        } else {
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        goto cleanup;
    }

    const nmo_cli_graph_node_t *nodes = graph.nodes;
    size_t node_count = graph.node_count;
    const nmo_cli_graph_edge_t *edges = graph.edges;
    size_t edge_count = graph.edge_count;

    size_t emit_node_count = node_count;
    bool nodes_truncated = false;
    if (max_nodes > 0 && node_count > max_nodes) {
        emit_node_count = max_nodes;
        nodes_truncated = true;
    }

    if (emit_node_count > 0) {
        emit_node_ids = (nmo_object_id_t *)malloc(emit_node_count * sizeof(*emit_node_ids));
        if (!emit_node_ids) {
            fprintf(stderr, "Error: Out of memory\n");
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
            goto cleanup;
        }
        for (size_t i = 0; i < emit_node_count; ++i) {
            emit_node_ids[i] = nodes[i].id;
        }
    }

    size_t emit_edge_count = 0;
    size_t emit_edge_cap = edge_count;
    if (max_edges > 0 && max_edges < emit_edge_cap) {
        emit_edge_cap = max_edges;
    }
    if (emit_edge_cap > 0) {
        emit_edge_indices = (size_t *)malloc(emit_edge_cap * sizeof(*emit_edge_indices));
        if (!emit_edge_indices) {
            fprintf(stderr, "Error: Out of memory\n");
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
            goto cleanup;
        }
    }

    bool edges_limited = false;
    for (size_t i = 0; i < edge_count; ++i) {
        if (!node_id_in_set(emit_node_ids, emit_node_count, edges[i].from_id) ||
            !node_id_in_set(emit_node_ids, emit_node_count, edges[i].to_id)) {
            continue;
        }
        if (max_edges > 0 && emit_edge_count >= max_edges) {
            edges_limited = true;
            break;
        }
        if (emit_edge_indices) {
            emit_edge_indices[emit_edge_count] = i;
        }
        emit_edge_count++;
    }

    bool edges_truncated = edges_limited || nodes_truncated;
    behavior_graph_report_t report = {
        .c = &c,
        .repo = nmo_tool_owner_repository(c.workspace),
        .bb_reg = nmo_context_get_bb_registry(c.ctx),
        .graph = &graph,
        .behavior_id = behavior_id,
        .emit_node_count = emit_node_count,
        .emit_edge_indices = emit_edge_indices,
        .emit_edge_count = emit_edge_count,
        .nodes_truncated = nodes_truncated,
        .edges_truncated = edges_truncated,
    };
    behavior_graph_count(&graph, &report.counts);

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              behavior_graph_add_summary(rec, &report) &&
              behavior_graph_add_tables(rec, &report) &&
              (!emit_dot || behavior_graph_add_dot(rec, &report));
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    exit_code = nmo_cmd_ctx_emit_record(&c, rec, "behavior.graph", 0, c.colorize);

cleanup:
    free(emit_edge_indices);
    free(emit_node_ids);
    nmo_behavior_graph_free(&graph);
    return close_ctx ? nmo_cmd_ctx_done(&c, exit_code) : exit_code;
}

int nmo_cmd_behavior_graph(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_core_object_selector_t selector = {0};
    const char *file_path = NULL;
    bool emit_dot = false;
    size_t max_nodes = 0;
    size_t max_edges = 0;
    uint32_t depth = UINT32_MAX;
    const char *usage = "nmo behavior graph [--depth N] [--dot] [--id <id> | --name <name> | <id>] <file>";

    if (!parse_behavior_graph_args(argc, argv, true, &selector, &file_path,
                                   &emit_dot, &max_nodes, &max_edges, &depth)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    return behavior_graph_run(&c, &selector, emit_dot, max_nodes, max_edges,
                              depth, true, usage);
}

int nmo_cmd_behavior_graph_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv) {
    nmo_core_object_selector_t selector = {0};
    const char *file_path = NULL;
    bool emit_dot = false;
    size_t max_nodes = 0;
    size_t max_edges = 0;
    uint32_t depth = UINT32_MAX;
    const char *usage = "behavior graph [--depth N] [--dot] [--id <id> | --name <name> | <id>]";

    if (!parse_behavior_graph_args(argc, argv, false, &selector, &file_path,
                                   &emit_dot, &max_nodes, &max_edges, &depth)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    return behavior_graph_run(ctx, &selector, emit_dot, max_nodes, max_edges,
                              depth, false, usage);
}

/* ============================================================================
 * behavior dump -- hierarchical tree view
 * ============================================================================ */

static void dump_text_prefix(FILE *out, int depth, bool last_child,
                             uint32_t branch_mask) {
    for (int d = 0; d < depth; d++) {
        fprintf(out, "%s",
                (branch_mask & (1u << (unsigned)d)) ?
                    "\xe2\x94\x82   " : "    ");
    }
    fprintf(out, "%s",
            (depth > 0) ? (last_child ? "    " : "\xe2\x94\x82   ") : "");
}

/*
 * malloc'd decoded value text of a parameter, or NULL when the object is not a
 * decodable parameter or the value formats to nothing.
 */
static char *dump_param_decoded_value_dup(
    nmo_object_t *param_obj,
    const nmo_type_registry_t *reg,
    const nmo_workspace_t *workspace)
{
    if (!param_obj) {
        return NULL;
    }

    nmo_class_id_t cid = nmo_object_get_class_id(param_obj);
    if (cid != NMO_CID_PARAMETERLOCAL &&
        cid != NMO_CID_PARAMETEROUT &&
        cid != NMO_CID_PARAMETER) {
        return NULL;
    }

    const nmo_parameter_state_t *param =
        (const nmo_parameter_state_t *)nmo_object_get_state(param_obj);
    if (!param || !param->has_state) {
        return NULL;
    }

    char *text = nmo_core_param_value_dup(param, reg, workspace);
    if (text && text[0] == '\0') {
        free(text);
        return NULL;
    }
    return text;
}

static size_t dump_print_decoded_value_group(
    FILE *out,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    const nmo_workspace_t *workspace,
    const nmo_array_t *ids,
    const char *kind,
    int depth,
    bool last_child,
    uint32_t branch_mask)
{
    if (!ids || !ids->data) {
        return 0;
    }

    size_t printed = 0;
    for (size_t i = 0; i < ids->count; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(ids, i);
        nmo_object_t *param_obj =
            nmo_object_repository_find_by_id(repo, id);
        char *value = dump_param_decoded_value_dup(param_obj, reg, workspace);
        if (!value) {
            continue;
        }

        nmo_guid_t type_guid = get_param_type_guid(param_obj);
        const char *name = param_obj ? nmo_object_get_name(param_obj) : NULL;
        dump_text_prefix(out, depth, last_child, branch_mask);
        fprintf(out, "    %s %zu: %s [%s] = %s\n",
                kind, i, (name && name[0]) ? name : "(unnamed)",
                resolve_type(reg, type_guid), value);
        free(value);
        printed++;
    }
    return printed;
}

static void dump_print_decoded_values(
    FILE *out,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    const nmo_workspace_t *workspace,
    const nmo_behavior_state_t *bs,
    int depth,
    bool last_child,
    uint32_t branch_mask)
{
    if (!out || !repo || !bs) {
        return;
    }

    bool has_any = false;
    for (size_t i = 0; i < bs->local_parameters.count && !has_any; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(
            &bs->local_parameters, i);
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, id);
        char *probe = dump_param_decoded_value_dup(obj, reg, workspace);
        has_any = probe != NULL;
        free(probe);
    }
    for (size_t i = 0; i < bs->out_parameters.count && !has_any; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(
            &bs->out_parameters, i);
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, id);
        char *probe = dump_param_decoded_value_dup(obj, reg, workspace);
        has_any = probe != NULL;
        free(probe);
    }
    if (!has_any) {
        return;
    }

    dump_text_prefix(out, depth, last_child, branch_mask);
    fprintf(out, "  Decoded Values:\n");
    dump_print_decoded_value_group(out, repo, reg, workspace,
                                   &bs->local_parameters, "local",
                                   depth, last_child, branch_mask);
    dump_print_decoded_value_group(out, repo, reg, workspace,
                                   &bs->out_parameters, "pOut",
                                   depth, last_child, branch_mask);
}

static size_t dump_add_decoded_value_group_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *arr,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    const nmo_workspace_t *workspace,
    const nmo_array_t *ids,
    const char *kind)
{
    if (!doc || !arr || !ids || !ids->data) {
        return 0;
    }

    size_t added = 0;
    for (size_t i = 0; i < ids->count; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(ids, i);
        nmo_object_t *param_obj =
            nmo_object_repository_find_by_id(repo, id);
        char *value = dump_param_decoded_value_dup(param_obj, reg, workspace);
        if (!value) {
            continue;
        }

        yyjson_mut_val *item = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(doc, item, "kind", kind);
        yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
        yyjson_mut_obj_add_uint(doc, item, "id", id);
        const char *name = param_obj ? nmo_object_get_name(param_obj) : NULL;
        nmo_cli_json_add_str_safe(doc, item, "name",
                                  (name && name[0]) ? name : "");
        nmo_guid_t type_guid = get_param_type_guid(param_obj);
        nmo_cli_json_add_str_fmt_safe(doc, item, "type_guid", "%08X-%08X",
                                      type_guid.d1, type_guid.d2);
        nmo_cli_json_add_str_safe(doc, item, "type_name",
                                  resolve_type(reg, type_guid));
        nmo_cli_json_add_str_safe(doc, item, "decoded_value", value);
        free(value);
        yyjson_mut_arr_add_val(arr, item);
        added++;
    }
    return added;
}

static void dump_add_decoded_values_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *node,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    const nmo_workspace_t *workspace,
    const nmo_behavior_state_t *bs)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    dump_add_decoded_value_group_json(doc, arr, repo, reg, workspace,
                                      &bs->local_parameters, "local");
    dump_add_decoded_value_group_json(doc, arr, repo, reg, workspace,
                                      &bs->out_parameters, "output");
    yyjson_mut_obj_add_val(doc, node, "decoded_values", arr);
}

static nmo_object_id_t dump_io_owner(
    const nmo_behavior_index_t *bidx,
    nmo_object_id_t io_id,
    nmo_object_id_t fallback)
{
    if (bidx) {
        const nmo_port_owner_t *po = nmo_behavior_index_find(bidx, io_id);
        if (po && po->owner_id != 0) {
            return po->owner_id;
        }
    }
    return fallback;
}

static const char *dump_owner_display_name(
    nmo_object_repository_t *repo,
    nmo_object_id_t root_id,
    const char *root_name,
    nmo_object_id_t owner_id)
{
    if (owner_id == root_id) {
        return (root_name && root_name[0]) ? root_name : "(root)";
    }
    return resolve_name(repo, owner_id);
}

static void dump_print_execution_flow(
    FILE *out,
    nmo_object_repository_t *repo,
    const nmo_behavior_index_t *bidx,
    const nmo_behavior_state_t *bs,
    nmo_object_id_t root_id,
    const char *root_name)
{
    if (!out || !repo || !bs) {
        return;
    }

    fprintf(out, "\nExecution Flow\n");
    size_t printed = 0;
    if (bs->sub_behavior_links.data) {
        for (size_t i = 0; i < bs->sub_behavior_links.count; i++) {
            nmo_object_id_t link_id = nmo_behavior_ref_array_get_id(
                &bs->sub_behavior_links, i);
            nmo_object_t *link_obj =
                nmo_object_repository_find_by_id(repo, link_id);
            if (!link_obj || !link_obj->state) {
                continue;
            }
            const nmo_behaviorlink_state_t *link =
                (const nmo_behaviorlink_state_t *)link_obj->state;
            nmo_object_id_t src_owner =
                dump_io_owner(
                    bidx, nmo_behaviorlink_in_io_id(link), root_id);
            nmo_object_id_t tgt_owner =
                dump_io_owner(
                    bidx, nmo_behaviorlink_out_io_id(link), root_id);
            fprintf(out, "  %s.%s -> %s.%s",
                    dump_owner_display_name(repo, root_id, root_name, src_owner),
                    resolve_name(repo, nmo_behaviorlink_in_io_id(link)),
                    dump_owner_display_name(repo, root_id, root_name, tgt_owner),
                    resolve_name(repo, nmo_behaviorlink_out_io_id(link)));
            if (link->activation_delay != 0) {
                fprintf(out, "  (delay: %d)", link->activation_delay);
            }
            fprintf(out, "\n");
            printed++;
        }
    }
    if (printed == 0) {
        fprintf(out, "  (no execution links)\n");
    }
}

static void dump_add_execution_flow_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *data,
    nmo_object_repository_t *repo,
    const nmo_behavior_index_t *bidx,
    const nmo_behavior_state_t *bs,
    nmo_object_id_t root_id,
    const char *root_name)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    if (bs && bs->sub_behavior_links.data) {
        for (size_t i = 0; i < bs->sub_behavior_links.count; i++) {
            nmo_object_id_t link_id = nmo_behavior_ref_array_get_id(
                &bs->sub_behavior_links, i);
            nmo_object_t *link_obj =
                nmo_object_repository_find_by_id(repo, link_id);
            if (!link_obj || !link_obj->state) {
                continue;
            }
            const nmo_behaviorlink_state_t *link =
                (const nmo_behaviorlink_state_t *)link_obj->state;
            nmo_object_id_t src_owner =
                dump_io_owner(
                    bidx, nmo_behaviorlink_in_io_id(link), root_id);
            nmo_object_id_t tgt_owner =
                dump_io_owner(
                    bidx, nmo_behaviorlink_out_io_id(link), root_id);

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "link_id", link_id);
            yyjson_mut_obj_add_uint(
                doc, item, "source_io_id", nmo_behaviorlink_in_io_id(link));
            nmo_cli_json_add_str_safe(doc, item, "source_io_name",
                                      resolve_name(
                                          repo,
                                          nmo_behaviorlink_in_io_id(link)));
            yyjson_mut_obj_add_uint(doc, item, "source_owner_id", src_owner);
            nmo_cli_json_add_str_safe(
                doc, item, "source_owner_name",
                dump_owner_display_name(repo, root_id, root_name, src_owner));
            yyjson_mut_obj_add_uint(
                doc, item, "target_io_id", nmo_behaviorlink_out_io_id(link));
            nmo_cli_json_add_str_safe(doc, item, "target_io_name",
                                      resolve_name(
                                          repo,
                                          nmo_behaviorlink_out_io_id(link)));
            yyjson_mut_obj_add_uint(doc, item, "target_owner_id", tgt_owner);
            nmo_cli_json_add_str_safe(
                doc, item, "target_owner_name",
                dump_owner_display_name(repo, root_id, root_name, tgt_owner));
            yyjson_mut_obj_add_int(doc, item, "activation_delay",
                                   link->activation_delay);
            yyjson_mut_arr_add_val(arr, item);
        }
    }
    yyjson_mut_obj_add_val(doc, data, "execution_flow", arr);
}

typedef struct dump_flow_source {
    nmo_object_id_t param_id;
    nmo_object_id_t owner_id;
    const char *owner_name;
    const char *param_name;
} dump_flow_source_t;

static size_t dump_collect_data_flow_sources(
    nmo_object_repository_t *repo,
    const nmo_behavior_state_t *bs,
    nmo_object_id_t root_id,
    const char *root_name,
    dump_flow_source_t *sources,
    size_t source_cap)
{
    size_t source_count = 0;
    if (bs->local_parameters.data) {
        for (size_t i = 0; i < bs->local_parameters.count &&
                           source_count < source_cap; i++) {
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &bs->local_parameters, i);
            if (id == 0) continue;
            sources[source_count].param_id = id;
            sources[source_count].owner_id = root_id;
            sources[source_count].owner_name =
                (root_name && root_name[0]) ? root_name : "(root)";
            sources[source_count].param_name = resolve_name(repo, id);
            source_count++;
        }
    }

    if (bs->sub_behaviors.data) {
        for (size_t si = 0; si < bs->sub_behaviors.count; si++) {
            nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(
                &bs->sub_behaviors, si);
            if (sub_id == 0) continue;
            nmo_object_t *sub =
                nmo_object_repository_find_by_id(repo, sub_id);
            if (!sub || !sub->state) {
                continue;
            }
            const nmo_behavior_state_t *sub_bs =
                (const nmo_behavior_state_t *)sub->state;
            const char *sub_name = nmo_object_get_name(sub);
            if (!sub_name || !sub_name[0]) {
                sub_name = "(unnamed)";
            }
            for (size_t pi = 0; pi < sub_bs->out_parameters.count &&
                               source_count < source_cap; pi++) {
                nmo_object_id_t param_id = nmo_behavior_ref_array_get_id(
                    &sub_bs->out_parameters, pi);
                if (param_id == 0) continue;
                sources[source_count].param_id = param_id;
                sources[source_count].owner_id = sub_id;
                sources[source_count].owner_name = sub_name;
                sources[source_count].param_name = resolve_name(repo, param_id);
                source_count++;
            }
        }
    }
    return source_count;
}

static const dump_flow_source_t *dump_find_flow_source(
    const dump_flow_source_t *sources,
    size_t source_count,
    nmo_object_id_t param_id)
{
    for (size_t i = 0; i < source_count; i++) {
        if (sources[i].param_id == param_id) {
            return &sources[i];
        }
    }
    return NULL;
}

static void dump_print_data_flow(
    FILE *out,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    const nmo_behavior_state_t *bs,
    nmo_object_id_t root_id,
    const char *root_name)
{
    if (!out || !repo || !bs) {
        return;
    }

    dump_flow_source_t sources[512];
    size_t source_count = dump_collect_data_flow_sources(
        repo, bs, root_id, root_name, sources,
        sizeof(sources) / sizeof(sources[0]));

    fprintf(out, "\nData Flow\n");
    size_t printed = 0;
    for (size_t si = 0; si < bs->sub_behaviors.count; si++) {
        nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(
            &bs->sub_behaviors, si);
        nmo_object_t *sub = nmo_object_repository_find_by_id(repo, sub_id);
        if (!sub || !sub->state) {
            continue;
        }
        const nmo_behavior_state_t *sub_bs =
            (const nmo_behavior_state_t *)sub->state;
        const char *sub_name = nmo_object_get_name(sub);
        if (!sub_name || !sub_name[0]) {
            sub_name = "(unnamed)";
        }
        for (size_t pi = 0; pi < sub_bs->in_parameters.count; pi++) {
            nmo_object_id_t param_id = nmo_behavior_ref_array_get_id(
                &sub_bs->in_parameters, pi);
            nmo_object_t *pin_obj =
                nmo_object_repository_find_by_id(repo, param_id);
            if (!pin_obj || !pin_obj->state) {
                continue;
            }
            const nmo_parameterin_state_t *pin =
                (const nmo_parameterin_state_t *)pin_obj->state;
            const nmo_object_id_t source_id =
                nmo_parameterin_source_id(pin);
            if (source_id == 0) {
                continue;
            }

            const dump_flow_source_t *src =
                dump_find_flow_source(sources, source_count, source_id);
            const char *src_owner = src ? src->owner_name : "(external)";
            const char *src_name =
                src ? src->param_name : resolve_name(repo, source_id);
            const char *pin_name = nmo_object_get_name(pin_obj);
            nmo_guid_t type_guid = get_param_type_guid(pin_obj);
            fprintf(out, "  %s.%s -> %s.%s  [%s]%s\n",
                    src_owner,
                    (src_name && src_name[0]) ? src_name : "?",
                    sub_name,
                    (pin_name && pin_name[0]) ? pin_name : "?",
                    resolve_type(reg, type_guid),
                    pin->is_shared ? " (shared)" : "");
            printed++;
        }
    }
    if (printed == 0) {
        fprintf(out, "  (no parameter connections)\n");
    }
}

static void dump_add_data_flow_json(
    yyjson_mut_doc *doc,
    yyjson_mut_val *data,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    const nmo_behavior_state_t *bs,
    nmo_object_id_t root_id,
    const char *root_name)
{
    yyjson_mut_val *arr = yyjson_mut_arr(doc);
    if (bs) {
        dump_flow_source_t sources[512];
        size_t source_count = dump_collect_data_flow_sources(
            repo, bs, root_id, root_name, sources,
            sizeof(sources) / sizeof(sources[0]));

        for (size_t si = 0; si < bs->sub_behaviors.count; si++) {
            nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(
                &bs->sub_behaviors, si);
            nmo_object_t *sub =
                nmo_object_repository_find_by_id(repo, sub_id);
            if (!sub || !sub->state) {
                continue;
            }
            const nmo_behavior_state_t *sub_bs =
                (const nmo_behavior_state_t *)sub->state;
            const char *sub_name = nmo_object_get_name(sub);
            if (!sub_name || !sub_name[0]) {
                sub_name = "(unnamed)";
            }
            for (size_t pi = 0; pi < sub_bs->in_parameters.count; pi++) {
                nmo_object_id_t param_id = nmo_behavior_ref_array_get_id(
                    &sub_bs->in_parameters, pi);
                nmo_object_t *pin_obj =
                    nmo_object_repository_find_by_id(repo, param_id);
                if (!pin_obj || !pin_obj->state) {
                    continue;
                }
                const nmo_parameterin_state_t *pin =
                    (const nmo_parameterin_state_t *)pin_obj->state;
                const nmo_object_id_t source_id =
                    nmo_parameterin_source_id(pin);
                if (source_id == 0) {
                    continue;
                }

                const dump_flow_source_t *src =
                    dump_find_flow_source(sources, source_count,
                                          source_id);
                const char *src_owner = src ? src->owner_name : "(external)";
                const char *src_name =
                    src ? src->param_name : resolve_name(repo, source_id);
                nmo_guid_t type_guid = get_param_type_guid(pin_obj);

                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "source_id",
                                        source_id);
                nmo_cli_json_add_str_safe(doc, item, "source_name",
                                          src_name ? src_name : "");
                yyjson_mut_obj_add_uint(doc, item, "source_owner_id",
                                        src ? src->owner_id : 0);
                nmo_cli_json_add_str_safe(doc, item, "source_owner_name",
                                          src_owner);
                yyjson_mut_obj_add_uint(doc, item, "target_id", param_id);
                nmo_cli_json_add_str_safe(doc, item, "target_name",
                                          resolve_name(repo, param_id));
                yyjson_mut_obj_add_uint(doc, item, "target_owner_id",
                                        sub_id);
                nmo_cli_json_add_str_safe(doc, item, "target_owner_name",
                                          sub_name);
                nmo_cli_json_add_str_fmt_safe(doc, item, "type_guid", "%08X-%08X",
                                              type_guid.d1, type_guid.d2);
                nmo_cli_json_add_str_safe(doc, item, "type_name",
                                          resolve_type(reg, type_guid));
                yyjson_mut_obj_add_bool(doc, item, "is_shared",
                                        pin->is_shared != 0);
                yyjson_mut_arr_add_val(arr, item);
            }
        }
    }
    yyjson_mut_obj_add_val(doc, data, "data_flow", arr);
}

static void dump_behavior_tree(
    FILE *out, nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    const nmo_workspace_t *workspace,
    nmo_object_id_t beh_id, int depth, bool last_child, uint32_t branch_mask,
    bool include_values)
{
    if (depth > 16) return;

    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, beh_id);
    if (!obj) return;

    const nmo_behavior_state_t *bs = (const nmo_behavior_state_t *)nmo_object_get_state(obj);
    if (!bs) return;

    const char *name = nmo_object_get_name(obj);
    bool is_bb = (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
    bool is_script = (bs->flags & CKBEHAVIOR_SCRIPT) != 0;

    /* Draw tree connectors */
    for (int d = 0; d < depth; d++) {
        if (d == depth - 1) {
            fprintf(out, "%s", last_child ? "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 " : "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 ");
        } else {
            fprintf(out, "%s", (branch_mask & (1u << (unsigned)d)) ? "\xe2\x94\x82   " : "    ");
        }
    }

    /* Node label */
    fprintf(out, "%s [#%u] (%s)",
            (name && name[0]) ? name : "(unnamed)",
            beh_id,
            is_script ? "Script" : is_bb ? "BB" : "Graph");

    /* Compact IO summary */
    if (bs->inputs.count > 0 || bs->outputs.count > 0) {
        fprintf(out, "  io:%zu/%zu", bs->inputs.count, bs->outputs.count);
    }
    if (bs->in_parameters.count > 0) {
        fprintf(out, "  pIn:%zu", bs->in_parameters.count);
    }
    if (bs->out_parameters.count > 0) {
        fprintf(out, "  pOut:%zu", bs->out_parameters.count);
    }
    fprintf(out, "\n");

    /* Show input parameter signatures for BBs (compact, one line) */
    if (is_bb && bs->in_parameters.count > 0 && depth < 8) {
        /* Print tree prefix for continuation line */
        for (int d = 0; d < depth; d++) {
            fprintf(out, "%s", (branch_mask & (1u << (unsigned)d)) ? "\xe2\x94\x82   " : "    ");
        }
        fprintf(out, "%s", (depth > 0) ? (last_child ? "    " : "\xe2\x94\x82   ") : "");
        fprintf(out, "  pIn: ");

        for (size_t i = 0; i < bs->in_parameters.count && i < 6; i++) {
            if (i > 0) fprintf(out, ", ");
            nmo_object_id_t id = nmo_behavior_ref_array_get_id(
                &bs->in_parameters, i);
            nmo_object_t *p = nmo_object_repository_find_by_id(repo, id);
            const char *pn = p ? nmo_object_get_name(p) : "?";
            nmo_guid_t tg = get_param_type_guid(p);
            const char *tn = resolve_type(reg, tg);
            fprintf(out, "%s[%s]", (pn && pn[0]) ? pn : "?", tn);
        }
        if (bs->in_parameters.count > 6) {
            fprintf(out, " ...(+%zu)", bs->in_parameters.count - 6);
        }
        fprintf(out, "\n");
    }

    if (include_values) {
        dump_print_decoded_values(out, repo, reg, workspace, bs,
                                  depth, last_child, branch_mask);
    }

    /* Recurse into sub-behaviors */
    if (bs->sub_behaviors.count > 0) {
        uint32_t next_mask = branch_mask;
        if (depth > 0 && !last_child) {
            next_mask |= (1u << (unsigned)depth);
        }
        for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
            bool is_last = (i == bs->sub_behaviors.count - 1);
            nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(
                &bs->sub_behaviors, i);
            dump_behavior_tree(out, repo, reg, workspace, sub_id,
                               depth + 1, is_last, next_mask,
                               include_values);
        }
    }
}

static void dump_behavior_tree_json(
    yyjson_mut_doc *doc, yyjson_mut_val *arr,
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    const nmo_behavior_registry_t *bb_reg,
    const nmo_workspace_t *workspace,
    nmo_object_id_t beh_id, int depth,
    bool include_values)
{
    if (depth > 16) return;

    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, beh_id);
    if (!obj) return;

    const nmo_behavior_state_t *bs =
        (const nmo_behavior_state_t *)nmo_object_get_state(obj);
    if (!bs) return;

    const char *name = nmo_object_get_name(obj);
    bool is_bb = (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
    bool is_script = (bs->flags & CKBEHAVIOR_SCRIPT) != 0;

    yyjson_mut_val *node = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, node, "id", beh_id);
    yyjson_mut_obj_add_int(doc, node, "depth", depth);
    nmo_cli_json_add_str_safe(doc, node, "name",
        (name && name[0]) ? name : "");
    nmo_cli_json_add_str_safe(doc, node, "type",
        is_script ? "Script" : is_bb ? "BB" : "Graph");
    yyjson_mut_obj_add_uint(doc, node, "input_count",
                            (uint64_t)bs->inputs.count);
    yyjson_mut_obj_add_uint(doc, node, "output_count",
                            (uint64_t)bs->outputs.count);
    yyjson_mut_obj_add_uint(doc, node, "in_param_count",
                            (uint64_t)bs->in_parameters.count);
    yyjson_mut_obj_add_uint(doc, node, "out_param_count",
                            (uint64_t)bs->out_parameters.count);
    yyjson_mut_obj_add_uint(doc, node, "sub_count",
                            (uint64_t)bs->sub_behaviors.count);

    if (is_bb && !nmo_guid_is_null(bs->block_guid)) {
        nmo_cli_json_add_str_fmt_safe(doc, node, "bb_guid", "%08X-%08X",
                                      bs->block_guid.d1, bs->block_guid.d2);
        const char *proto = nmo_behavior_registry_get_name(bb_reg, bs->block_guid);
        if (proto) {
            nmo_cli_json_add_str_safe(doc, node, "proto_name", proto);
        }
    }

    if (include_values) {
        dump_add_decoded_values_json(doc, node, repo, reg, workspace, bs);
    }

    yyjson_mut_arr_add_val(arr, node);

    /* Recurse into sub-behaviors */
    if (bs->sub_behaviors.count > 0) {
        for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
            nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(
                &bs->sub_behaviors, i);
            dump_behavior_tree_json(doc, arr, repo, reg, bb_reg,
                                    workspace, sub_id, depth + 1,
                                    include_values);
        }
    }
}

typedef struct behavior_dump_all_data {
    nmo_object_repository_t *repo;
    const nmo_behavior_registry_t *bb_reg;
    const nmo_workspace_t *workspace;
    yyjson_mut_doc *doc;
    yyjson_mut_val *tree;
    FILE *out;
    size_t printed;
    bool include_values;
} behavior_dump_all_data_t;

static int behavior_dump_all_object(size_t index, nmo_object_t *obj,
                                    const nmo_cmd_ctx_t *c, void *user)
{
    (void)index;

    behavior_dump_all_data_t *data = (behavior_dump_all_data_t *)user;
    if (!data || !obj) {
        return 0;
    }

    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    if (!is_behavior_class(c->registry, cid)) {
        return 0;
    }

    const nmo_behavior_state_t *bs =
        (const nmo_behavior_state_t *)nmo_object_get_state(obj);
    if (!bs || !(bs->flags & CKBEHAVIOR_SCRIPT)) {
        return 0;
    }

    nmo_object_id_t id = nmo_object_get_id(obj);
    if (data->doc && data->tree) {
        dump_behavior_tree_json(data->doc, data->tree, data->repo,
                                c->registry, data->bb_reg, data->workspace,
                                id, 0, data->include_values);
    } else if (data->out) {
        if (data->printed > 0) {
            fprintf(data->out, "\n");
        }
        dump_behavior_tree(data->out, data->repo, c->registry,
                           data->workspace, id, 0, true, 0,
                           data->include_values);
    }
    data->printed++;
    return 0;
}

int nmo_cmd_behavior_dump(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--all",    "-a", NMO_OPT_FLAG, "Dump all script behaviors as trees"},
        {"--flows",  NULL, NMO_OPT_FLAG, "Include execution/data flow summaries for one behavior"},
        {"--values", NULL, NMO_OPT_FLAG, "Include decoded local/output values"},
        {"--json",   "-j", NMO_OPT_FLAG, "JSON output"},
        {"--id",     "-i", NMO_OPT_UINT, "Behavior object ID"},
        {"--name",   "-n", NMO_OPT_STRING, "Behavior object name"},
    };
    enum { OPT_ALL, OPT_FLOWS, OPT_VALUES, OPT_JSON, OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool dump_all = vals[OPT_ALL].present && vals[OPT_ALL].val.flag;
    bool include_flows = vals[OPT_FLOWS].present && vals[OPT_FLOWS].val.flag;
    bool include_values = vals[OPT_VALUES].present && vals[OPT_VALUES].val.flag;

    if (dump_all && include_flows) {
        fprintf(stderr, "Error: --flows cannot be used with --all; specify one behavior id.\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_core_object_selector_t selector = {0};
    if (!dump_all) {
        bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
        if ((has_selector_opt && r.pos_count < 1) || (!has_selector_opt && r.pos_count < 2)) {
            fprintf(stderr, "Usage: nmo behavior dump [--all | --id <id> | --name <name> | <id>] <file>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        selector = (nmo_core_object_selector_t){
            .has_id = vals[OPT_ID].present,
            .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
            .positional_id = has_selector_opt ? NULL : r.pos_args[0],
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .required_base_class = NMO_CID_BEHAVIOR,
            .selector_label = "Behavior",
            .type_label = "CKBehavior",
        };
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);
    const nmo_behavior_index_t *bidx = NULL;
    if (include_flows) {
        if (nmo_tool_owner_ensure_behavior_acceleration(c.workspace) != NMO_OK) {
            fprintf(stderr, "Error: Failed to build behavior acceleration\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        bidx = nmo_tool_owner_behavior_index(c.workspace);
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *tree = yyjson_mut_arr(doc);
        const nmo_behavior_registry_t *bb_reg =
            nmo_context_get_bb_registry(c.ctx);

        if (dump_all) {
            behavior_dump_all_data_t dump_data = {
                .repo = repo,
                .bb_reg = bb_reg,
                .workspace = c.workspace,
                .doc = doc,
                .tree = tree,
                .include_values = include_values,
            };
            rc = nmo_core_object_query_run(&c, NULL, behavior_dump_all_object,
                                           &dump_data, NULL);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                yyjson_mut_doc_free(doc);
                fprintf(stderr, "Error: Failed to query objects\n");
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
            }
        } else {
            nmo_object_t *selected = NULL;
            nmo_object_id_t object_id = 0;
            rc = nmo_core_resolve_one_object(&c, &selector, &selected, &object_id);
            if (rc != NMO_CLI_EXIT_SUCCESS) {
                fprintf(stderr, "Usage: nmo behavior dump [--all | --id <id> | --name <name> | <id>] <file>\n");
                yyjson_mut_doc_free(doc);
                return nmo_cmd_ctx_done(&c, rc);
            }
            dump_behavior_tree_json(doc, tree, repo, c.registry, bb_reg,
                                    c.workspace, object_id, 0, include_values);
            if (include_flows) {
                nmo_object_t *obj =
                    nmo_object_repository_find_by_id(repo, object_id);
                const nmo_behavior_state_t *bs =
                    obj ? (const nmo_behavior_state_t *)nmo_object_get_state(obj) : NULL;
                const char *name = obj ? nmo_object_get_name(obj) : NULL;
                dump_add_execution_flow_json(doc, data, repo, bidx, bs,
                                             object_id, name);
                dump_add_data_flow_json(doc, data, repo, c.registry, bs,
                                        object_id, name);
            }
        }

        yyjson_mut_obj_add_val(doc, data, "tree", tree);
        nmo_cmd_ctx_json_end(&c, doc, data, "behavior.dump");
    } else if (dump_all) {
        behavior_dump_all_data_t dump_data = {
            .repo = repo,
            .workspace = c.workspace,
            .out = c.out,
            .include_values = include_values,
        };
        rc = nmo_core_object_query_run(&c, NULL, behavior_dump_all_object,
                                       &dump_data, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            fprintf(stderr, "Error: Failed to query objects\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        if (dump_data.printed == 0) {
            fprintf(c.out, "No script behaviors found.\n");
        }
    } else {
        nmo_object_t *selected = NULL;
        nmo_object_id_t object_id = 0;
        rc = nmo_core_resolve_one_object(&c, &selector, &selected, &object_id);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            fprintf(stderr, "Usage: nmo behavior dump [--all | --id <id> | --name <name> | <id>] <file>\n");
            return nmo_cmd_ctx_done(&c, rc);
        }

        dump_behavior_tree(c.out, repo, c.registry, c.workspace,
                           object_id, 0, true, 0, include_values);
        if (include_flows) {
            nmo_object_t *obj = nmo_object_repository_find_by_id(repo, object_id);
            const nmo_behavior_state_t *bs =
                obj ? (const nmo_behavior_state_t *)nmo_object_get_state(obj) : NULL;
            const char *name = obj ? nmo_object_get_name(obj) : NULL;
            dump_print_execution_flow(c.out, repo, bidx, bs, object_id, name);
            dump_print_data_flow(c.out, repo, c.registry, bs, object_id, name);
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

