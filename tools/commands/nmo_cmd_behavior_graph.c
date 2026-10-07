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
#include "object/nmo_context.h"
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
#include "extension/nmo_behavior_registry.h"

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
        NMO_OPT_DEF_DEPTH,
        NMO_OPT_DEF_JSON,
        {"--id",        "-i", NMO_OPT_UINT, "Behavior object ID"},
        {"--name",      "-n", NMO_OPT_STRING, "Behavior object name"},
    };
    enum { OPT_DOT, OPT_MAX_NODES, OPT_MAX_EDGES, OPT_DEPTH, OPT_JSON,
           OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
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
            .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
            .positional_id = positional_id,
            .name = nmo_opt_str(&vals[OPT_NAME]),
            .required_base_class = NMO_CID_BEHAVIOR,
            .selector_label = "Behavior",
            .type_label = "CKBehavior",
        };
    }
    if (out_file) *out_file = file_path;
    if (out_dot) *out_dot = vals[OPT_DOT].val.flag;
    if (out_max_nodes) *out_max_nodes = vals[OPT_MAX_NODES].present ? (size_t)vals[OPT_MAX_NODES].val.u : 0;
    if (out_max_edges) *out_max_edges = vals[OPT_MAX_EDGES].present ? (size_t)vals[OPT_MAX_EDGES].val.u : 0;
    if (out_depth) *out_depth = nmo_opt_uint_or(&vals[OPT_DEPTH], UINT32_MAX);
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

/* Everything the behavior dump tree walk reads and writes. */
typedef struct behavior_dump {
    nmo_object_repository_t *repo;
    const nmo_type_registry_t *reg;
    const nmo_behavior_registry_t *bb_reg;
    nmo_workspace_t *workspace;
    nmo_cli_record_t *text;       /* text-only tree lines */
    nmo_cli_record_array_t *tree; /* JSON-only "tree" nodes */
    nmo_cmd_behavior_flow_ctx_t flow;
    bool include_values;
    bool include_flows;
    nmo_object_id_t *graphs;      /* graphs (not BBs) in tree order, for the flows */
    size_t graph_count;
    size_t graph_capacity;
    size_t printed;
    bool ok;
} behavior_dump_t;

static const char *const dump_branch_bar = "\xe2\x94\x82   ";

/* Text: the prefix of a continuation line under a tree node. */
static bool dump_add_text_prefix(nmo_cli_record_t *text, int depth, bool last_child,
                                 uint32_t branch_mask)
{
    bool ok = true;
    for (int d = 0; ok && d + 1 < depth; d++) {
        ok = nmo_cli_record_raw(text, (branch_mask & (1u << (unsigned)(d + 1))) ?
                                          dump_branch_bar : "    ");
    }
    if (depth > 0) {
        ok = ok && nmo_cli_record_raw(text, last_child ? "    " : dump_branch_bar);
    }
    return ok;
}

/* Text: the connectors in front of a tree node's label. */
static bool dump_add_text_connector(nmo_cli_record_t *text, int depth, bool last_child,
                                    uint32_t branch_mask)
{
    bool ok = true;
    for (int d = 0; ok && d < depth; d++) {
        if (d == depth - 1) {
            ok = nmo_cli_record_raw(text, last_child ?
                "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 " : "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 ");
        } else {
            ok = nmo_cli_record_raw(text, (branch_mask & (1u << (unsigned)(d + 1))) ?
                                              dump_branch_bar : "    ");
        }
    }
    return ok;
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

static bool dump_has_decoded_value(const behavior_dump_t *d, const nmo_array_t *ids)
{
    for (size_t i = 0; i < ids->count; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(ids, i);
        nmo_object_t *obj = nmo_object_repository_find_by_id(d->repo, id);
        char *probe = dump_param_decoded_value_dup(obj, d->reg, d->workspace);
        free(probe);
        if (probe) {
            return true;
        }
    }
    return false;
}

/*
 * Every decodable parameter of `ids`. JSON: one "decoded_values" item each,
 * tagged `json_kind`. Text: "<text_kind> N: name [type] = value" lines.
 */
static bool dump_add_decoded_value_group(
    const behavior_dump_t *d,
    nmo_cli_record_array_t *arr,
    const nmo_array_t *ids,
    const char *json_kind,
    const char *text_kind,
    int depth,
    bool last_child,
    uint32_t branch_mask)
{
    if (!ids->data) {
        return true;
    }

    bool ok = true;
    for (size_t i = 0; ok && i < ids->count; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(ids, i);
        nmo_object_t *param_obj = nmo_object_repository_find_by_id(d->repo, id);
        char *value = dump_param_decoded_value_dup(param_obj, d->reg, d->workspace);
        if (!value) {
            continue;
        }

        nmo_guid_t type_guid = get_param_type_guid(param_obj);
        const char *type_name = resolve_type(d->reg, type_guid);
        const char *name = param_obj ? nmo_object_get_name(param_obj) : NULL;
        nmo_cli_record_t *item = nmo_cli_record_new();
        bool item_ok = item != NULL &&
            nmo_cli_record_str(item, "kind", NULL, json_kind) &&
            nmo_cli_record_uint(item, "index", NULL, (uint64_t)i) &&
            nmo_cli_record_uint(item, "id", NULL, id) &&
            nmo_cli_record_str(item, "name", NULL, (name && name[0]) ? name : "") &&
            nmo_cli_record_str_fmt(item, "type_guid", NULL, "%08X-%08X",
                                   type_guid.d1, type_guid.d2) &&
            nmo_cli_record_str(item, "type_name", NULL, type_name) &&
            nmo_cli_record_str(item, "decoded_value", NULL, value);
        if (item_ok) {
            item_ok = nmo_cli_record_array_add(arr, item);
        } else {
            nmo_cli_record_free(item);
        }
        ok = item_ok &&
             dump_add_text_prefix(d->text, depth, last_child, branch_mask) &&
             nmo_cli_record_raw_fmt(d->text, "    %s %zu: %s [%s] = %s\n",
                                    text_kind, i, (name && name[0]) ? name : "(unnamed)",
                                    type_name, value);
        free(value);
    }
    return ok;
}

/* JSON: "decoded_values". Text: a "Decoded Values:" block when there is one. */
static bool dump_add_decoded_values(
    const behavior_dump_t *d,
    nmo_cli_record_t *node,
    const nmo_behavior_state_t *bs,
    int depth,
    bool last_child,
    uint32_t branch_mask)
{
    nmo_cli_record_array_t *arr = nmo_cli_record_array(node, "decoded_values", NULL);
    bool ok = arr != NULL;
    if (ok && (dump_has_decoded_value(d, &bs->local_parameters) ||
               dump_has_decoded_value(d, &bs->out_parameters))) {
        ok = dump_add_text_prefix(d->text, depth, last_child, branch_mask) &&
             nmo_cli_record_raw(d->text, "  Decoded Values:\n");
    }
    return ok &&
           dump_add_decoded_value_group(d, arr, &bs->local_parameters, "local", "local",
                                        depth, last_child, branch_mask) &&
           dump_add_decoded_value_group(d, arr, &bs->out_parameters, "output", "pOut",
                                        depth, last_child, branch_mask);
}

/* Text: a BB's input parameter signatures, on one line. */
static bool dump_add_input_signature(const behavior_dump_t *d,
                                     const nmo_behavior_state_t *bs,
                                     int depth,
                                     bool last_child,
                                     uint32_t branch_mask)
{
    bool ok = dump_add_text_prefix(d->text, depth, last_child, branch_mask) &&
              nmo_cli_record_raw(d->text, "  pIn: ");
    for (size_t i = 0; ok && i < bs->in_parameters.count && i < 6; i++) {
        nmo_object_id_t id = nmo_behavior_ref_array_get_id(&bs->in_parameters, i);
        nmo_object_t *p = nmo_object_repository_find_by_id(d->repo, id);
        const char *pn = p ? nmo_object_get_name(p) : "?";
        const char *tn = resolve_type(d->reg, get_param_type_guid(p));
        ok = nmo_cli_record_raw_fmt(d->text, "%s%s[%s]", i > 0 ? ", " : "",
                                    (pn && pn[0]) ? pn : "?", tn);
    }
    if (bs->in_parameters.count > 6) {
        ok = ok && nmo_cli_record_raw_fmt(d->text, " ...(+%zu)", bs->in_parameters.count - 6);
    }
    return ok && nmo_cli_record_raw(d->text, "\n");
}

/* Remember a graph of the tree, to dump its flows after the tree. */
static bool dump_note_graph(behavior_dump_t *d, nmo_object_id_t graph_id)
{
    if (d->graph_count == d->graph_capacity) {
        size_t capacity = d->graph_capacity ? d->graph_capacity * 2u : 32u;
        nmo_object_id_t *graphs = (nmo_object_id_t *)realloc(d->graphs,
                                                             capacity * sizeof(*graphs));
        if (!graphs) {
            return false;
        }
        d->graphs = graphs;
        d->graph_capacity = capacity;
    }
    d->graphs[d->graph_count++] = graph_id;
    return true;
}

/*
 * One behavior and, recursively, its sub-behaviors. JSON: a flat "tree" node
 * each. Text: a tree of labels with box-drawing connectors.
 */
static bool dump_add_behavior_tree(behavior_dump_t *d,
                                   nmo_object_id_t beh_id,
                                   int depth,
                                   bool last_child,
                                   uint32_t branch_mask)
{
    if (depth > 16) return true;

    nmo_object_t *obj = nmo_object_repository_find_by_id(d->repo, beh_id);
    if (!obj) return true;

    const nmo_behavior_state_t *bs = (const nmo_behavior_state_t *)nmo_object_get_state(obj);
    if (!bs) return true;

    const char *name = nmo_object_get_name(obj);
    bool is_bb = (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) != 0;
    bool is_script = (bs->flags & CKBEHAVIOR_SCRIPT) != 0;
    const char *type = is_script ? "Script" : is_bb ? "BB" : "Graph";

    nmo_cli_record_t *node = nmo_cli_record_new();
    bool ok = node != NULL &&
        nmo_cli_record_uint(node, "id", NULL, beh_id) &&
        nmo_cli_record_int(node, "depth", NULL, depth) &&
        nmo_cli_record_str(node, "name", NULL, (name && name[0]) ? name : "") &&
        nmo_cli_record_str(node, "type", NULL, type) &&
        nmo_cli_record_uint(node, "input_count", NULL, (uint64_t)bs->inputs.count) &&
        nmo_cli_record_uint(node, "output_count", NULL, (uint64_t)bs->outputs.count) &&
        nmo_cli_record_uint(node, "in_param_count", NULL, (uint64_t)bs->in_parameters.count) &&
        nmo_cli_record_uint(node, "out_param_count", NULL, (uint64_t)bs->out_parameters.count) &&
        nmo_cli_record_uint(node, "sub_count", NULL, (uint64_t)bs->sub_behaviors.count);
    const char *proto = NULL;
    if (is_bb && !nmo_guid_is_null(bs->block_guid)) {
        proto = nmo_behavior_registry_get_name(d->bb_reg, bs->block_guid);
        ok = ok && nmo_cli_record_str_fmt(node, "bb_guid", NULL, "%08X-%08X",
                                          bs->block_guid.d1, bs->block_guid.d2);
        if (proto) {
            ok = ok && nmo_cli_record_str(node, "proto_name", NULL, proto);
        }
    }
    const char *op_name = is_bb ? nmo_cmd_behavior_op_block_name(&d->flow, beh_id) : NULL;
    if (op_name) {
        ok = ok && nmo_cli_record_str(node, "operation_name", NULL, op_name);
    }
    if (!is_bb && d->include_flows) {
        ok = ok && dump_note_graph(d, beh_id);
    }

    /* Node label (with the prototype of a renamed BB) and compact IO summary */
    bool renamed = proto && name && name[0] && strcmp(name, proto) != 0;
    ok = ok && dump_add_text_connector(d->text, depth, last_child, branch_mask) &&
         nmo_cli_record_raw_fmt(d->text, "%s [#%u] (%s%s%s)",
                                (name && name[0]) ? name : "(unnamed)", beh_id, type,
                                renamed ? ": " : "", renamed ? proto : "");
    if (op_name) {
        ok = ok && nmo_cli_record_raw_fmt(d->text, " = %s", op_name);
    }
    if (bs->inputs.count > 0 || bs->outputs.count > 0) {
        ok = ok && nmo_cli_record_raw_fmt(d->text, "  io:%zu/%zu",
                                          bs->inputs.count, bs->outputs.count);
    }
    if (bs->in_parameters.count > 0) {
        ok = ok && nmo_cli_record_raw_fmt(d->text, "  pIn:%zu", bs->in_parameters.count);
    }
    if (bs->out_parameters.count > 0) {
        ok = ok && nmo_cli_record_raw_fmt(d->text, "  pOut:%zu", bs->out_parameters.count);
    }
    ok = ok && nmo_cli_record_raw(d->text, "\n");

    if (is_bb && bs->in_parameters.count > 0 && depth < 8) {
        ok = ok && dump_add_input_signature(d, bs, depth, last_child, branch_mask);
    }
    if (d->include_values) {
        ok = ok && dump_add_decoded_values(d, node, bs, depth, last_child, branch_mask);
    }
    if (!ok) {
        nmo_cli_record_free(node);
        return false;
    }
    if (!nmo_cli_record_array_add(d->tree, node)) {
        return false;
    }

    /* Recurse into sub-behaviors */
    uint32_t next_mask = branch_mask;
    if (depth > 0 && !last_child) {
        next_mask |= (1u << (unsigned)depth);
    }
    for (size_t i = 0; ok && i < bs->sub_behaviors.count; i++) {
        bool is_last = (i == bs->sub_behaviors.count - 1);
        nmo_object_id_t sub_id = nmo_behavior_ref_array_get_id(&bs->sub_behaviors, i);
        ok = dump_add_behavior_tree(d, sub_id, depth + 1, is_last, next_mask);
    }
    return ok;
}

/*
 * A flow section: `key` in JSON, "\n<heading>\n" and `empty_text` when it has
 * no items in text.
 */
static nmo_cli_record_array_t *dump_flow_array(nmo_cli_record_t *rec,
                                               const char *key,
                                               const char *heading,
                                               const char *empty_text)
{
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, key, NULL);
    if (!arr) {
        return NULL;
    }
    if (!nmo_cli_record_array_set_heading(arr, heading) ||
        !nmo_cli_record_array_set_empty_text(arr, empty_text)) {
        return NULL;
    }
    return arr;
}

/* "Execution Flow": each behavior link of `graph_id` as "Owner#id.port -> Owner#id.port". */
static bool dump_add_execution_flow(nmo_cli_record_t *rec,
                                    const behavior_dump_t *d,
                                    nmo_object_id_t graph_id,
                                    const char *graph_label)
{
    nmo_object_t *graph = nmo_object_repository_find_by_id(d->repo, graph_id);
    const nmo_behavior_state_t *bs =
        graph ? (const nmo_behavior_state_t *)nmo_object_get_state(graph) : NULL;
    char *heading = nmo_tool_strdup_fmt("Execution Flow: %s", graph_label);
    nmo_cli_record_array_t *arr = heading ? dump_flow_array(rec, "execution_flow", heading,
                                                            "  (no execution links)") : NULL;
    free(heading);
    bool ok = arr != NULL;
    for (size_t i = 0; ok && bs && i < bs->sub_behavior_links.count; i++) {
        nmo_object_id_t link_id = nmo_behavior_ref_array_get_id(&bs->sub_behavior_links, i);
        nmo_object_t *link_obj = nmo_object_repository_find_by_id(d->repo, link_id);
        if (!link_obj || !link_obj->state) {
            continue;
        }
        const nmo_behaviorlink_state_t *link =
            (const nmo_behaviorlink_state_t *)link_obj->state;
        /* in_io_id = source (SDK naming is backwards), out_io_id = target */
        const nmo_object_id_t in_io_id = nmo_behaviorlink_in_io_id(link);
        const nmo_object_id_t out_io_id = nmo_behaviorlink_out_io_id(link);
        const nmo_port_owner_t *sp =
            d->flow.index ? nmo_behavior_index_find(d->flow.index, in_io_id) : NULL;
        const nmo_port_owner_t *tp =
            d->flow.index ? nmo_behavior_index_find(d->flow.index, out_io_id) : NULL;
        nmo_object_id_t src_owner = sp ? sp->owner_id : graph_id;
        nmo_object_id_t tgt_owner = tp ? tp->owner_id : graph_id;
        char *src = nmo_cmd_behavior_io_label_dup(&d->flow, in_io_id);
        char *tgt = nmo_cmd_behavior_io_label_dup(&d->flow, out_io_id);

        nmo_cli_record_t *item = nmo_cli_record_new();
        bool item_ok = item != NULL && src != NULL && tgt != NULL &&
            nmo_cli_record_uint(item, "link_id", NULL, link_id) &&
            nmo_cli_record_uint(item, "source_io_id", NULL, in_io_id) &&
            nmo_cli_record_str(item, "source_io_name", NULL, resolve_name(d->repo, in_io_id)) &&
            nmo_cli_record_uint(item, "source_owner_id", NULL, src_owner) &&
            nmo_cli_record_str(item, "source_owner_name", NULL,
                               resolve_name(d->repo, src_owner)) &&
            nmo_cli_record_uint(item, "target_io_id", NULL, out_io_id) &&
            nmo_cli_record_str(item, "target_io_name", NULL, resolve_name(d->repo, out_io_id)) &&
            nmo_cli_record_uint(item, "target_owner_id", NULL, tgt_owner) &&
            nmo_cli_record_str(item, "target_owner_name", NULL,
                               resolve_name(d->repo, tgt_owner)) &&
            nmo_cli_record_int(item, "activation_delay", NULL, link->activation_delay);
        if (item_ok && link->activation_delay != 0) {
            item_ok = nmo_cli_record_set_summary_fmt(item, "  %s -> %s  (delay: %d)",
                                                     src, tgt, link->activation_delay);
        } else if (item_ok) {
            item_ok = nmo_cli_record_set_summary_fmt(item, "  %s -> %s", src, tgt);
        }
        free(src);
        free(tgt);
        if (item_ok) {
            ok = nmo_cli_record_array_add(arr, item);
        } else {
            nmo_cli_record_free(item);
            ok = false;
        }
    }
    return ok;
}

/* "Data Flow": the parameter reads and writes inside `graph_id`. */
static bool dump_add_data_flow(nmo_cli_record_t *rec,
                               const behavior_dump_t *d,
                               nmo_object_id_t graph_id,
                               const char *graph_label)
{
    char *heading = nmo_tool_strdup_fmt("Data Flow: %s", graph_label);
    nmo_cli_record_array_t *arr = heading ? dump_flow_array(rec, "data_flow", heading,
                                                            "  (no parameter connections)")
                                          : NULL;
    free(heading);
    return arr != NULL && nmo_cmd_behavior_add_data_flow_items(arr, &d->flow, graph_id, true);
}

/* Both flow sections of `graph_id` into `rec`. */
static bool dump_add_graph_flows(nmo_cli_record_t *rec,
                                 const behavior_dump_t *d,
                                 nmo_object_id_t graph_id)
{
    char *label = nmo_cmd_behavior_node_label_dup(&d->flow, graph_id);
    bool ok = label != NULL &&
              dump_add_execution_flow(rec, d, graph_id, label) &&
              dump_add_data_flow(rec, d, graph_id, label);
    free(label);
    return ok;
}

/*
 * JSON: "graph_flows", one {graph_id, graph_name, execution_flow, data_flow}
 * per graph of the dumped trees but `skip_id`. Text: their flow sections.
 */
static bool dump_add_nested_graph_flows(nmo_cli_record_t *rec,
                                        const behavior_dump_t *d,
                                        nmo_object_id_t skip_id)
{
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, "graph_flows", NULL);
    if (!arr) {
        return false;
    }
    nmo_cli_record_array_omit_heading(arr);
    nmo_cli_record_array_inline_items(arr);
    bool ok = true;
    for (size_t i = 0; ok && i < d->graph_count; i++) {
        nmo_object_id_t graph_id = d->graphs[i];
        if (graph_id == skip_id) {
            continue;
        }
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL &&
             nmo_cli_record_uint(item, "graph_id", NULL, graph_id) &&
             nmo_cli_record_str(item, "graph_name", NULL, resolve_name(d->repo, graph_id)) &&
             dump_add_graph_flows(item, d, graph_id);
        if (ok) {
            ok = nmo_cli_record_array_add(arr, item);
        } else {
            nmo_cli_record_free(item);
        }
    }
    return ok;
}

static int behavior_dump_all_object(size_t index, nmo_object_t *obj,
                                    const nmo_cmd_ctx_t *c, void *user)
{
    (void)index;
    (void)c;

    behavior_dump_t *d = (behavior_dump_t *)user;
    if (!d || !obj || !d->ok) {
        return 0;
    }

    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    if (!is_behavior_class(d->reg, cid)) {
        return 0;
    }

    const nmo_behavior_state_t *bs =
        (const nmo_behavior_state_t *)nmo_object_get_state(obj);
    if (!bs || !(bs->flags & CKBEHAVIOR_SCRIPT)) {
        return 0;
    }

    if (d->printed > 0) {
        d->ok = nmo_cli_record_raw(d->text, "\n");
    }
    d->ok = d->ok && dump_add_behavior_tree(d, nmo_object_get_id(obj), 0, true, 0);
    d->printed++;
    return 0;
}

int nmo_cmd_behavior_dump(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--all",    "-a", NMO_OPT_FLAG, "Dump all script behaviors as trees"},
        {"--flows",  NULL, NMO_OPT_FLAG, "Include execution/data flows of every dumped graph"},
        {"--values", NULL, NMO_OPT_FLAG, "Include decoded local/output values"},
        NMO_OPT_DEF_JSON,
        {"--id",     "-i", NMO_OPT_UINT, "Behavior object ID"},
        {"--name",   "-n", NMO_OPT_STRING, "Behavior object name"},
    };
    enum { OPT_ALL, OPT_FLOWS, OPT_VALUES, OPT_JSON, OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool dump_all = nmo_opt_flag(&vals[OPT_ALL]);
    bool include_flows = nmo_opt_flag(&vals[OPT_FLOWS]);
    bool include_values = nmo_opt_flag(&vals[OPT_VALUES]);

    nmo_core_object_selector_t selector = {0};
    if (!dump_all) {
        bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
        if ((has_selector_opt && r.pos_count < 1) || (!has_selector_opt && r.pos_count < 2)) {
            fprintf(stderr, "Usage: nmo behavior dump [--all | --id <id> | --name <name> | <id>] <file>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        selector = (nmo_core_object_selector_t){
            .has_id = vals[OPT_ID].present,
            .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
            .positional_id = has_selector_opt ? NULL : r.pos_args[0],
            .name = nmo_opt_str(&vals[OPT_NAME]),
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

    behavior_dump_t dump = {
        .repo = repo,
        .reg = c.registry,
        .bb_reg = nmo_context_get_bb_registry(c.ctx),
        .workspace = c.workspace,
        .flow = {
            .repo = repo,
            .registry = c.registry,
            .workspace = c.workspace,
            .index = bidx,
        },
        .include_values = include_values,
        .include_flows = include_flows,
        .ok = true,
    };
    /* The tree text comes first, the flows after it */
    nmo_cli_record_t *rec = nmo_cli_record_new();
    dump.text = rec ? nmo_cli_record_object(rec, NULL) : NULL;
    dump.ok = dump.text != NULL;

    if (dump_all) {
        dump.tree = dump.ok ? nmo_cli_record_array(rec, "tree", NULL) : NULL;
        dump.ok = dump.tree != NULL;
        rc = nmo_core_object_query_run(&c, NULL, behavior_dump_all_object, &dump, NULL);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_record_free(rec);
            fprintf(stderr, "Error: Failed to query objects\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        if (dump.printed == 0) {
            dump.ok = dump.ok && nmo_cli_record_raw(dump.text, "No script behaviors found.\n");
        }
        if (include_flows) {
            dump.ok = dump.ok && dump_add_nested_graph_flows(rec, &dump, 0);
        }
    } else {
        nmo_object_t *selected = NULL;
        nmo_object_id_t object_id = 0;
        rc = nmo_core_resolve_one_object(&c, &selector, &selected, &object_id);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            fprintf(stderr, "Usage: nmo behavior dump [--all | --id <id> | --name <name> | <id>] <file>\n");
            nmo_cli_record_free(rec);
            return nmo_cmd_ctx_done(&c, rc);
        }

        dump.tree = dump.ok ? nmo_cli_record_array(rec, "tree", NULL) : NULL;
        dump.ok = dump.tree != NULL &&
                  dump_add_behavior_tree(&dump, object_id, 0, true, 0);
        /* The selected behavior's flows at the top level, nested graphs' after */
        if (include_flows) {
            dump.ok = dump.ok &&
                      dump_add_graph_flows(rec, &dump, object_id) &&
                      dump_add_nested_graph_flows(rec, &dump, object_id);
        }
    }
    free(dump.graphs);

    if (!dump.ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    rc = nmo_cmd_ctx_emit_record(&c, rec, "behavior.dump", 0, c.colorize);
    return nmo_cmd_ctx_done(&c, rc);
}
