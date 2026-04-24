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
#include "session/nmo_session.h"
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

static void dot_write_label(FILE *out, const char *label) {
    if (!out) {
        return;
    }
    if (!label) {
        return;
    }
    for (const unsigned char *p = (const unsigned char *)label; *p; ++p) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') {
            fputc('\\', out);
            fputc((char)c, out);
        } else if (c == '\n' || c == '\r') {
            fputs("\\n", out);
        } else if (c == '\t') {
            fputs("\\t", out);
        } else if (isprint(c)) {
            fputc((char)c, out);
        } else {
            fputc('?', out);
        }
    }
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

static void guid_to_string(nmo_guid_t guid, char *buf, size_t size) {
    if (!buf || size == 0) {
        return;
    }
    snprintf(buf, size, "%08X-%08X", guid.d1, guid.d2);
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

static const char *graph_node_display_name(
    nmo_object_repository_t *repo,
    const nmo_type_registry_t *reg,
    const nmo_behavior_registry_t *bb_reg,
    const nmo_cli_graph_node_t *node,
    char *buf,
    size_t size)
{
    if (!node) {
        return "";
    }

    if (node->kind && strcmp(node->kind, "operation") == 0) {
        const nmo_parameteroperation_state_t *op_state =
            get_operation_state_for_id(repo, node->id);
        if (op_state) {
            const char *op_name =
                nmo_type_registry_guid_to_name(reg, op_state->operation_guid);
            if (op_name && op_name[0]) {
                return op_name;
            }
            if (buf && size > 0) {
                guid_to_string(op_state->operation_guid, buf, size);
                return buf;
            }
        }
    }

    if (node->kind && strcmp(node->kind, "behavior") == 0) {
        const nmo_behavior_state_t *bs = get_behavior_state_for_id(repo, node->id);
        if (bs && (bs->flags & CKBEHAVIOR_BUILDINGBLOCK) &&
            !nmo_guid_is_null(bs->block_guid)) {
            const char *proto = nmo_behavior_registry_get_name(bb_reg, bs->block_guid);
            if (proto && proto[0]) {
                return proto;
            }
        }
    }

    if (node->name && node->name[0]) {
        return node->name;
    }
    if (node->class_name && node->class_name[0]) {
        return node->class_name;
    }
    return node->kind ? node->kind : "";
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

    if (nmo_session_ensure_behavior_acceleration(c.session) != NMO_OK) {
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
        char detail[256];
        size_t detail_len = nmo_last_error_message_copy(detail, sizeof(detail));
        nmo_error_code_t code = nmo_last_error_code();
        if (detail_len > 0) {
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
    size_t broken_links = graph.broken_links;
    size_t missing_nodes = graph.missing_nodes;

    size_t node_behavior = 0;
    size_t node_parameter = 0;
    size_t node_operation = 0;
    size_t node_io = 0;
    size_t node_unknown = 0;

    for (size_t i = 0; i < node_count; ++i) {
        if (!nodes[i].kind) {
            node_unknown++;
        } else if (strcmp(nodes[i].kind, "behavior") == 0) {
            node_behavior++;
        } else if (strcmp(nodes[i].kind, "parameter") == 0) {
            node_parameter++;
        } else if (strcmp(nodes[i].kind, "operation") == 0) {
            node_operation++;
        } else if (strcmp(nodes[i].kind, "io") == 0) {
            node_io++;
        } else {
            node_unknown++;
        }
    }

    size_t edge_behavior_link = 0;
    size_t edge_io_link = 0;
    size_t edge_param_in = 0;
    size_t edge_param_out = 0;
    size_t edge_param_local = 0;
    size_t edge_param_dest = 0;
    size_t edge_param_source = 0;
    size_t edge_op_in1 = 0;
    size_t edge_op_in2 = 0;
    size_t edge_op_out = 0;

    for (size_t i = 0; i < edge_count; ++i) {
        const char *kind = edges[i].kind ? edges[i].kind : "";
        if (strcmp(kind, "behavior_link") == 0) {
            edge_behavior_link++;
        } else if (strcmp(kind, "io_link") == 0) {
            edge_io_link++;
        } else if (strcmp(kind, "param_in") == 0) {
            edge_param_in++;
        } else if (strcmp(kind, "param_out") == 0) {
            edge_param_out++;
        } else if (strcmp(kind, "param_local") == 0) {
            edge_param_local++;
        } else if (strcmp(kind, "param_dest") == 0) {
            edge_param_dest++;
        } else if (strcmp(kind, "param_source") == 0) {
            edge_param_source++;
        } else if (strcmp(kind, "op_in1") == 0) {
            edge_op_in1++;
        } else if (strcmp(kind, "op_in2") == 0) {
            edge_op_in2++;
        } else if (strcmp(kind, "op_out") == 0) {
            edge_op_out++;
        }
    }

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
    size_t nodes_dropped = node_count - emit_node_count;
    size_t edges_dropped = edge_count - emit_edge_count;
    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    const nmo_behavior_registry_t *bb_reg = nmo_context_get_bb_registry(c.ctx);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "behavior_id", behavior_id);
        nmo_cmd_behavior_add_interface_diagnostics_json(doc, data, c.session);

        const char *behavior_name = graph.behavior_name;
        if (behavior_name && behavior_name[0]) {
            nmo_cli_json_add_str_safe(doc, data, "behavior_name", behavior_name);
        }
        nmo_class_id_t behavior_class_id = graph.behavior_class_id;
        const char *behavior_class = graph.behavior_class_name;
        if (behavior_class_id != 0) {
            yyjson_mut_obj_add_uint(doc, data, "behavior_class_id", (uint64_t)behavior_class_id);
        }
        if (behavior_class) {
            nmo_cli_json_add_str_safe(doc, data, "behavior_class", behavior_class);
        }

        yyjson_mut_val *counts = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, counts, "nodes_total", (uint64_t)node_count);
        yyjson_mut_obj_add_uint(doc, counts, "edges_total", (uint64_t)edge_count);
        yyjson_mut_obj_add_uint(doc, counts, "broken_links", (uint64_t)broken_links);
        yyjson_mut_obj_add_uint(doc, counts, "missing_nodes", (uint64_t)missing_nodes);

        yyjson_mut_val *nodes_by_kind = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, nodes_by_kind, "behavior", (uint64_t)node_behavior);
        yyjson_mut_obj_add_uint(doc, nodes_by_kind, "parameter", (uint64_t)node_parameter);
        yyjson_mut_obj_add_uint(doc, nodes_by_kind, "operation", (uint64_t)node_operation);
        yyjson_mut_obj_add_uint(doc, nodes_by_kind, "io", (uint64_t)node_io);
        yyjson_mut_obj_add_uint(doc, nodes_by_kind, "unknown", (uint64_t)node_unknown);
        yyjson_mut_obj_add_val(doc, counts, "nodes_by_kind", nodes_by_kind);

        yyjson_mut_val *edges_by_kind = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "behavior_link", (uint64_t)edge_behavior_link);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "io_link", (uint64_t)edge_io_link);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "param_in", (uint64_t)edge_param_in);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "param_out", (uint64_t)edge_param_out);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "param_local", (uint64_t)edge_param_local);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "param_dest", (uint64_t)edge_param_dest);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "param_source", (uint64_t)edge_param_source);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "op_in1", (uint64_t)edge_op_in1);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "op_in2", (uint64_t)edge_op_in2);
        yyjson_mut_obj_add_uint(doc, edges_by_kind, "op_out", (uint64_t)edge_op_out);
        yyjson_mut_obj_add_val(doc, counts, "edges_by_kind", edges_by_kind);

        yyjson_mut_obj_add_val(doc, data, "counts", counts);

        yyjson_mut_val *graph_val = yyjson_mut_obj(doc);
        yyjson_mut_val *nodes_arr = yyjson_mut_arr(doc);
        yyjson_mut_val *edges_arr = yyjson_mut_arr(doc);

        for (size_t i = 0; i < emit_node_count; ++i) {
            yyjson_mut_val *node = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, node, "id", nodes[i].id);
            if (nodes[i].kind) {
                nmo_cli_json_add_str_safe(doc, node, "kind", nodes[i].kind);
            }
            if (nodes[i].name && nodes[i].name[0]) {
                nmo_cli_json_add_str_safe(doc, node, "name", nodes[i].name);
            }
            char display_buf[64];
            const char *display_name = graph_node_display_name(
                repo, c.registry, bb_reg, &nodes[i],
                display_buf, sizeof(display_buf));
            if (display_name && display_name[0]) {
                nmo_cli_json_add_str_safe(doc, node, "display_name", display_name);
            }
            if (nodes[i].class_id != 0) {
                yyjson_mut_obj_add_uint(doc, node, "class_id", (uint64_t)nodes[i].class_id);
            }
            if (nodes[i].class_name && nodes[i].class_name[0]) {
                nmo_cli_json_add_str_safe(doc, node, "class_name", nodes[i].class_name);
            }
            yyjson_mut_obj_add_uint(doc, node, "depth", (uint64_t)nodes[i].depth);
            if (nodes[i].parent_id != 0) {
                yyjson_mut_obj_add_uint(doc, node, "parent_id", (uint64_t)nodes[i].parent_id);
            }
            if (nodes[i].kind && strcmp(nodes[i].kind, "behavior") == 0) {
                const nmo_behavior_state_t *bs =
                    get_behavior_state_for_id(repo, nodes[i].id);
                nmo_cli_json_add_str_safe(doc, node, "behavior_type",
                                          behavior_type_name(bs));
                if (bs && (bs->flags & CKBEHAVIOR_BUILDINGBLOCK)) {
                    char guid_buf[24];
                    guid_to_string(bs->block_guid, guid_buf, sizeof(guid_buf));
                    nmo_cli_json_add_str_safe(doc, node, "bb_guid", guid_buf);
                    yyjson_mut_obj_add_uint(doc, node, "bb_version",
                                            (uint64_t)bs->block_version);
                    const char *proto =
                        nmo_behavior_registry_get_name(bb_reg, bs->block_guid);
                    if (proto && proto[0]) {
                        nmo_cli_json_add_str_safe(doc, node,
                                                  "bb_proto_name", proto);
                    }
                }
            } else if (nodes[i].kind && strcmp(nodes[i].kind, "operation") == 0) {
                const nmo_parameteroperation_state_t *op_state =
                    get_operation_state_for_id(repo, nodes[i].id);
                if (op_state) {
                    char guid_buf[24];
                    guid_to_string(op_state->operation_guid, guid_buf,
                                   sizeof(guid_buf));
                    nmo_cli_json_add_str_safe(doc, node, "operation_guid",
                                              guid_buf);
                    const char *op_name = nmo_type_registry_guid_to_name(
                        c.registry, op_state->operation_guid);
                    nmo_cli_json_add_str_safe(doc, node, "operation_name",
                                              (op_name && op_name[0]) ? op_name : guid_buf);
                    yyjson_mut_obj_add_uint(doc, node, "in1_id",
                                            op_state->has_in1 ? op_state->in1_id : 0);
                    yyjson_mut_obj_add_uint(doc, node, "in2_id",
                                            op_state->has_in2 ? op_state->in2_id : 0);
                    yyjson_mut_obj_add_uint(doc, node, "out_id",
                                            op_state->has_out ? op_state->out_id : 0);
                }
            }
            yyjson_mut_arr_add_val(nodes_arr, node);
        }

        for (size_t i = 0; i < emit_edge_count; ++i) {
            size_t edge_index = emit_edge_indices ? emit_edge_indices[i] : i;
            const nmo_cli_graph_edge_t edge_ref = edges[edge_index];
            yyjson_mut_val *edge = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, edge, "from", edge_ref.from_id);
            yyjson_mut_obj_add_uint(doc, edge, "to", edge_ref.to_id);
            const nmo_cli_graph_node_t *from_node =
                find_graph_node(nodes, node_count, edge_ref.from_id);
            const nmo_cli_graph_node_t *to_node =
                find_graph_node(nodes, node_count, edge_ref.to_id);
            char from_name_buf[64];
            char to_name_buf[64];
            const char *from_name = graph_node_display_name(
                repo, c.registry, bb_reg, from_node,
                from_name_buf, sizeof(from_name_buf));
            const char *to_name = graph_node_display_name(
                repo, c.registry, bb_reg, to_node,
                to_name_buf, sizeof(to_name_buf));
            if (from_name && from_name[0]) {
                nmo_cli_json_add_str_safe(doc, edge, "from_name", from_name);
            }
            if (to_name && to_name[0]) {
                nmo_cli_json_add_str_safe(doc, edge, "to_name", to_name);
            }
            if (edge_ref.kind) {
                nmo_cli_json_add_str_safe(doc, edge, "kind", edge_ref.kind);
            }
            if (edge_ref.field_path) {
                nmo_cli_json_add_str_safe(doc, edge, "field_path", edge_ref.field_path);
            }
            if (edge_ref.link_id != 0) {
                yyjson_mut_obj_add_uint(doc, edge, "link_id", edge_ref.link_id);
            }
            if (edge_ref.in_io_id != 0) {
                yyjson_mut_obj_add_uint(doc, edge, "in_io_id", edge_ref.in_io_id);
            }
            if (edge_ref.out_io_id != 0) {
                yyjson_mut_obj_add_uint(doc, edge, "out_io_id", edge_ref.out_io_id);
            }
            if (edge_ref.kind && strcmp(edge_ref.kind, "behavior_link") == 0) {
                yyjson_mut_obj_add_int(doc, edge, "activation_delay", edge_ref.activation_delay);
                yyjson_mut_obj_add_int(doc, edge, "initial_activation_delay", edge_ref.initial_activation_delay);
                nmo_cli_json_add_str_safe(doc, edge, "source_io_name",
                                          resolve_name(repo, edge_ref.in_io_id));
                nmo_cli_json_add_str_safe(doc, edge, "target_io_name",
                                          resolve_name(repo, edge_ref.out_io_id));
                yyjson_mut_obj_add_uint(doc, edge, "source_owner_id",
                                        edge_ref.from_id);
                yyjson_mut_obj_add_uint(doc, edge, "target_owner_id",
                                        edge_ref.to_id);
                if (from_name && from_name[0]) {
                    nmo_cli_json_add_str_safe(doc, edge, "source_owner_name",
                                              from_name);
                }
                if (to_name && to_name[0]) {
                    nmo_cli_json_add_str_safe(doc, edge, "target_owner_name",
                                              to_name);
                }
            }
            if (graph_edge_is_parameter_kind(edge_ref.kind)) {
                nmo_object_id_t param_id = graph_edge_parameter_id(&edge_ref);
                yyjson_mut_obj_add_uint(doc, edge, "parameter_id", param_id);
                nmo_cli_json_add_str_safe(doc, edge, "parameter_name",
                                          resolve_name(repo, param_id));
                nmo_object_t *param_obj =
                    nmo_object_repository_find_by_id(repo, param_id);
                nmo_guid_t type_guid = get_param_type_guid(param_obj);
                char guid_buf[24];
                guid_to_string(type_guid, guid_buf, sizeof(guid_buf));
                nmo_cli_json_add_str_safe(doc, edge, "type_guid", guid_buf);
                nmo_cli_json_add_str_safe(doc, edge, "type_name",
                                          resolve_type(c.registry, type_guid));
            }
            if (edge_ref.is_shared) {
                yyjson_mut_obj_add_bool(doc, edge, "is_shared", true);
            }
            yyjson_mut_arr_add_val(edges_arr, edge);
        }

        yyjson_mut_obj_add_val(doc, graph_val, "nodes", nodes_arr);
        yyjson_mut_obj_add_val(doc, graph_val, "edges", edges_arr);
        if (nodes_truncated || edges_truncated) {
            yyjson_mut_val *truncated = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_bool(doc, truncated, "nodes", nodes_truncated);
            yyjson_mut_obj_add_bool(doc, truncated, "edges", edges_truncated);
            yyjson_mut_obj_add_uint(doc, truncated, "nodes_emitted",
                                    (uint64_t)emit_node_count);
            yyjson_mut_obj_add_uint(doc, truncated, "edges_emitted",
                                    (uint64_t)emit_edge_count);
            yyjson_mut_obj_add_uint(doc, truncated, "nodes_dropped",
                                    (uint64_t)nodes_dropped);
            yyjson_mut_obj_add_uint(doc, truncated, "edges_dropped",
                                    (uint64_t)edges_dropped);
            yyjson_mut_obj_add_val(doc, graph_val, "truncated", truncated);
        }
        yyjson_mut_obj_add_val(doc, data, "graph", graph_val);

        nmo_cmd_ctx_json_end(&c, doc, data, "behavior.graph");
    } else {
        nmo_cli_print_heading(c.out, "Behavior Graph", c.colorize);
        fprintf(c.out, "\n");
        const char *behavior_name = graph.behavior_name;
        const char *behavior_class = graph.behavior_class_name;
        fprintf(c.out, "Behavior %u: %s [%s]\n\n",
                behavior_id,
                (behavior_name && behavior_name[0]) ? behavior_name : "(unnamed)",
                behavior_class ? behavior_class : "?");

        fprintf(c.out, "Nodes: %zu (behavior %zu, parameter %zu, operation %zu, io %zu, unknown %zu)\n",
                node_count, node_behavior, node_parameter, node_operation, node_io, node_unknown);
        fprintf(c.out, "Edges: %zu (behavior links %zu, io links %zu, param %zu, op %zu)\n",
                edge_count,
                edge_behavior_link,
                edge_io_link,
                (edge_param_in + edge_param_out + edge_param_local + edge_param_dest + edge_param_source),
                (edge_op_in1 + edge_op_in2 + edge_op_out));
        if (nodes_truncated) {
            fprintf(c.out, "Note: Nodes truncated to %zu (use --max-nodes 0 to disable)\n", emit_node_count);
        }
        if (edges_truncated) {
            fprintf(c.out, "Note: Edges truncated to %zu (use --max-edges 0 to disable)\n", emit_edge_count);
        }
        if (broken_links > 0) {
            fprintf(c.out, "Broken links: %zu\n", broken_links);
        }
        if (missing_nodes > 0) {
            fprintf(c.out, "Missing objects: %zu\n", missing_nodes);
        }
        fprintf(c.out, "\n");

        static const nmo_cli_table_col_t node_columns[] = {
            {"ID", NMO_CLI_ALIGN_RIGHT, 6, 0},
            {"D", NMO_CLI_ALIGN_RIGHT, 2, 0},
            {"Kind", NMO_CLI_ALIGN_LEFT, 12, 16},
            {"Name", NMO_CLI_ALIGN_LEFT, 22, 50},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 40},
        };
        nmo_cli_table_t node_table;
        nmo_cli_table_init(&node_table, node_columns, sizeof(node_columns) / sizeof(node_columns[0]));

        for (size_t i = 0; i < emit_node_count; ++i) {
            char id_buf[16];
            char depth_buf[8];
            snprintf(id_buf, sizeof(id_buf), "%u", nodes[i].id);
            snprintf(depth_buf, sizeof(depth_buf), "%u", nodes[i].depth);

            /* For operation nodes, resolve the operation name */
            char op_name_buf[80];
            const char *display_name = (nodes[i].name && nodes[i].name[0]) ? nodes[i].name : "-";
            if (nodes[i].kind && strcmp(nodes[i].kind, "operation") == 0) {
                nmo_object_t *op_obj = nmo_object_repository_find_by_id(repo, nodes[i].id);
                if (op_obj && op_obj->state) {
                    const nmo_parameteroperation_state_t *op_state =
                        (const nmo_parameteroperation_state_t *)op_obj->state;
                    const char *op_type = nmo_type_registry_guid_to_name(
                        c.registry, op_state->operation_guid);
                    if (op_type) {
                        if (display_name && strcmp(display_name, "-") != 0) {
                            snprintf(op_name_buf, sizeof(op_name_buf), "%s (%s)",
                                     display_name, op_type);
                        } else {
                            snprintf(op_name_buf, sizeof(op_name_buf), "%s", op_type);
                        }
                        display_name = op_name_buf;
                    }
                }
            }

            const char *cells[] = {
                id_buf,
                depth_buf,
                nodes[i].kind ? nodes[i].kind : "-",
                display_name,
                (nodes[i].class_name && nodes[i].class_name[0]) ? nodes[i].class_name : "-",
            };
            nmo_cli_table_add_row(&node_table, cells, 5);
        }

        nmo_cli_table_print(&node_table, c.out, c.colorize);
        nmo_cli_table_free(&node_table);
        fprintf(c.out, "\n");

        static const nmo_cli_table_col_t edge_columns[] = {
            {"From", NMO_CLI_ALIGN_LEFT, 18, 32},
            {"To", NMO_CLI_ALIGN_LEFT, 18, 32},
            {"Kind", NMO_CLI_ALIGN_LEFT, 14, 18},
            {"Field", NMO_CLI_ALIGN_LEFT, 18, 24},
            {"Link", NMO_CLI_ALIGN_RIGHT, 6, 0},
            {"Meta", NMO_CLI_ALIGN_LEFT, 16, 32},
        };
        nmo_cli_table_t edge_table;
        nmo_cli_table_init(&edge_table, edge_columns, sizeof(edge_columns) / sizeof(edge_columns[0]));

        for (size_t i = 0; i < emit_edge_count; ++i) {
            size_t edge_index = emit_edge_indices ? emit_edge_indices[i] : i;
            const nmo_cli_graph_edge_t edge_ref = edges[edge_index];
            const nmo_cli_graph_node_t *from_node =
                find_graph_node(nodes, node_count, edge_ref.from_id);
            const nmo_cli_graph_node_t *to_node =
                find_graph_node(nodes, node_count, edge_ref.to_id);

            char from_buf[64];
            char to_buf[64];
            char link_buf[16];
            char meta_buf[64];

            if (from_node && from_node->name && from_node->name[0]) {
                snprintf(from_buf, sizeof(from_buf), "%u:%s", from_node->id, from_node->name);
            } else {
                snprintf(from_buf, sizeof(from_buf), "%u", edge_ref.from_id);
            }

            if (to_node && to_node->name && to_node->name[0]) {
                snprintf(to_buf, sizeof(to_buf), "%u:%s", to_node->id, to_node->name);
            } else {
                snprintf(to_buf, sizeof(to_buf), "%u", edge_ref.to_id);
            }

            if (edge_ref.link_id != 0) {
                snprintf(link_buf, sizeof(link_buf), "%u", edge_ref.link_id);
            } else {
                snprintf(link_buf, sizeof(link_buf), "-");
            }

            /* in_io = source, out_io = target (Virtools SDK naming) */
            if (edge_ref.kind && strcmp(edge_ref.kind, "behavior_link") == 0) {
                const char *src_io = resolve_name(repo, edge_ref.in_io_id);
                const char *tgt_io = resolve_name(repo, edge_ref.out_io_id);
                if (edge_ref.activation_delay != 0 || edge_ref.initial_activation_delay != 0) {
                    snprintf(meta_buf, sizeof(meta_buf), "%s->%s %d/%d",
                             src_io, tgt_io,
                             edge_ref.activation_delay,
                             edge_ref.initial_activation_delay);
                } else {
                    snprintf(meta_buf, sizeof(meta_buf), "%s->%s",
                             src_io, tgt_io);
                }
            } else if (edge_ref.kind && strcmp(edge_ref.kind, "io_link") == 0) {
                const char *src_io = resolve_name(repo, edge_ref.in_io_id);
                const char *tgt_io = resolve_name(repo, edge_ref.out_io_id);
                snprintf(meta_buf, sizeof(meta_buf), "%s->%s",
                         src_io, tgt_io);
            } else if (edge_ref.kind && (strcmp(edge_ref.kind, "param_local") == 0 ||
                        strcmp(edge_ref.kind, "param_in") == 0 ||
                        strcmp(edge_ref.kind, "param_out") == 0 ||
                        strcmp(edge_ref.kind, "param_source") == 0 ||
                        strcmp(edge_ref.kind, "param_dest") == 0 ||
                        strcmp(edge_ref.kind, "op_in1") == 0 ||
                        strcmp(edge_ref.kind, "op_in2") == 0 ||
                        strcmp(edge_ref.kind, "op_out") == 0)) {
                /* Look up the parameter node to get its type name */
                nmo_object_id_t param_id = edge_ref.from_id;
                if (strcmp(edge_ref.kind, "param_out") == 0 ||
                    strcmp(edge_ref.kind, "op_out") == 0) {
                    param_id = edge_ref.to_id;
                }
                nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, param_id);
                nmo_guid_t tg = get_param_type_guid(param_obj);
                const char *tname = resolve_type(c.registry, tg);
                if (edge_ref.is_shared) {
                    snprintf(meta_buf, sizeof(meta_buf), "%s (shared)", tname);
                } else {
                    snprintf(meta_buf, sizeof(meta_buf), "%s", tname);
                }
            } else {
                snprintf(meta_buf, sizeof(meta_buf), "-");
            }

            const char *cells[] = {
                from_buf,
                to_buf,
                edge_ref.kind ? edge_ref.kind : "-",
                edge_ref.field_path ? edge_ref.field_path : "-",
                link_buf,
                meta_buf,
            };
            nmo_cli_table_add_row(&edge_table, cells, 6);
        }

        nmo_cli_table_print(&edge_table, c.out, c.colorize);
        nmo_cli_table_free(&edge_table);

        if (emit_dot) {
            fprintf(c.out, "\n");
            nmo_cli_print_heading(c.out, "DOT Graph", c.colorize);
            fprintf(c.out, "\n");
            /* Look up interface data for the root behavior */
            const nmo_interface_data_t *idata = NULL;
            {
                nmo_object_t *root_beh = nmo_object_repository_find_by_id(repo, behavior_id);
                if (root_beh) {
                    const nmo_behavior_state_t *root_bs =
                        (const nmo_behavior_state_t *)nmo_object_get_state(root_beh);
                    if (root_bs)
                        idata = root_bs->interface_data;
                }
            }

            fprintf(c.out, "digraph behavior_graph {\n");
            if (idata)
                fprintf(c.out, "  graph [layout=neato, overlap=false];\n");
            fprintf(c.out, "  node [shape=box, fontname=\"Courier\", style=filled];\n");
            for (size_t i = 0; i < emit_node_count; ++i) {
                const char *label = (nodes[i].name && nodes[i].name[0]) ? nodes[i].name :
                    (nodes[i].class_name && nodes[i].class_name[0]) ? nodes[i].class_name :
                    (nodes[i].kind ? nodes[i].kind : "node");

                /* Resolve operation type for operation nodes */
                char dot_op_buf[80];
                if (nodes[i].kind && strcmp(nodes[i].kind, "operation") == 0) {
                    nmo_object_t *op_obj = nmo_object_repository_find_by_id(repo, nodes[i].id);
                    if (op_obj && op_obj->state) {
                        const nmo_parameteroperation_state_t *op_state =
                            (const nmo_parameteroperation_state_t *)op_obj->state;
                        const char *op_type = nmo_type_registry_guid_to_name(
                            c.registry, op_state->operation_guid);
                        if (op_type) {
                            snprintf(dot_op_buf, sizeof(dot_op_buf), "%s", op_type);
                            label = dot_op_buf;
                        }
                    }
                }

                /* Color by kind */
                const char *fillcolor = "white";
                if (nodes[i].kind) {
                    if (strcmp(nodes[i].kind, "behavior") == 0) {
                        nmo_object_t *bobj = nmo_object_repository_find_by_id(repo, nodes[i].id);
                        if (bobj && bobj->state) {
                            const nmo_behavior_state_t *bst =
                                (const nmo_behavior_state_t *)bobj->state;
                            if (bst->flags & CKBEHAVIOR_SCRIPT)
                                fillcolor = "lightgreen";
                            else if (bst->flags & CKBEHAVIOR_BUILDINGBLOCK)
                                fillcolor = "lightblue";
                            else
                                fillcolor = "lightyellow";
                        } else {
                            fillcolor = "lightyellow";
                        }
                    } else if (strcmp(nodes[i].kind, "parameter") == 0) {
                        fillcolor = "lemonchiffon";
                    } else if (strcmp(nodes[i].kind, "operation") == 0) {
                        fillcolor = "lightsalmon";
                    } else if (strcmp(nodes[i].kind, "io") == 0) {
                        fillcolor = "lightgray";
                    }
                }

                /* Override color for script root from interface data */
                char color_hex_buf[8];
                if (idata && idata->script.color != 0 && idata->script.behavior_id == nodes[i].id)
                    fillcolor = interface_color_to_hex(idata->script.color, color_hex_buf, sizeof(color_hex_buf));

                /* Position from interface data */
                float px, py;
                bool has_pos = false;
                if (nodes[i].kind && strcmp(nodes[i].kind, "operation") == 0)
                    has_pos = find_operation_position(idata, nodes[i].id, &px, &py);
                else
                    has_pos = find_interface_position(idata, nodes[i].id, &px, &py);

                /* Write node with optional position */
                fprintf(c.out, "  n%u [label=\"", nodes[i].id);
                dot_write_label(c.out, label);
                fprintf(c.out, "\", fillcolor=\"%s\"", fillcolor);
                if (has_pos)
                    fprintf(c.out, ", pos=\"%.0f,%.0f!\"", px, -py);
                fprintf(c.out, "];\n");
            }
            for (size_t i = 0; i < emit_edge_count; ++i) {
                size_t edge_index = emit_edge_indices ? emit_edge_indices[i] : i;
                const nmo_cli_graph_edge_t edge_ref = edges[edge_index];
                char dot_edge_label[128];

                if (edge_ref.kind && strcmp(edge_ref.kind, "behavior_link") == 0) {
                    const char *src_io = resolve_name(repo, edge_ref.in_io_id);
                    const char *tgt_io = resolve_name(repo, edge_ref.out_io_id);
                    snprintf(dot_edge_label, sizeof(dot_edge_label),
                             "%s->%s delay=%d/%d",
                             src_io, tgt_io,
                             edge_ref.activation_delay,
                             edge_ref.initial_activation_delay);
                } else if (edge_ref.kind && (strcmp(edge_ref.kind, "param_local") == 0 ||
                            strcmp(edge_ref.kind, "param_in") == 0 ||
                            strcmp(edge_ref.kind, "param_out") == 0 ||
                            strcmp(edge_ref.kind, "param_source") == 0 ||
                            strcmp(edge_ref.kind, "param_dest") == 0 ||
                            strcmp(edge_ref.kind, "op_in1") == 0 ||
                            strcmp(edge_ref.kind, "op_in2") == 0 ||
                            strcmp(edge_ref.kind, "op_out") == 0)) {
                    nmo_object_id_t pid = edge_ref.from_id;
                    if (strcmp(edge_ref.kind, "param_out") == 0 ||
                        strcmp(edge_ref.kind, "op_out") == 0) {
                        pid = edge_ref.to_id;
                    }
                    nmo_object_t *pobj = nmo_object_repository_find_by_id(repo, pid);
                    nmo_guid_t ptg = get_param_type_guid(pobj);
                    const char *ptn = resolve_type(c.registry, ptg);
                    snprintf(dot_edge_label, sizeof(dot_edge_label), "%s",
                             ptn);
                } else {
                    snprintf(dot_edge_label, sizeof(dot_edge_label), "%s",
                             edge_ref.kind ? edge_ref.kind : "link");
                }

                const nmo_interface_link_t *ilink = find_interface_link(idata, edge_ref.link_id);

                fprintf(c.out, "  n%u -> n%u [label=\"", edge_ref.from_id, edge_ref.to_id);
                dot_write_label(c.out, dot_edge_label);
                fprintf(c.out, "\"");
                if (ilink && ilink->highlight)
                    fprintf(c.out, ", style=bold, color=red");
                fprintf(c.out, "];\n");
            }
            fprintf(c.out, "}\n");
        }
    }

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

static void dump_guid_to_string(nmo_guid_t guid, char *buf, size_t size) {
    if (!buf || size == 0) {
        return;
    }
    snprintf(buf, size, "%08X-%08X", guid.d1, guid.d2);
}

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

static bool dump_param_decoded_value(
    nmo_object_t *param_obj,
    const nmo_type_registry_t *reg,
    const nmo_workspace_t *workspace,
    char *buf,
    size_t size)
{
    if (!param_obj || !buf || size == 0) {
        return false;
    }

    nmo_class_id_t cid = nmo_object_get_class_id(param_obj);
    if (cid != NMO_CID_PARAMETERLOCAL &&
        cid != NMO_CID_PARAMETEROUT &&
        cid != NMO_CID_PARAMETER) {
        return false;
    }

    const nmo_parameter_state_t *param =
        (const nmo_parameter_state_t *)nmo_object_get_state(param_obj);
    if (!param || !param->has_state) {
        return false;
    }

    return nmo_behavior_param_value_to_string(param, reg, workspace, buf, size) == NMO_OK &&
           buf[0] != '\0';
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
    const nmo_object_id_t *param_ids = (const nmo_object_id_t *)ids->data;
    for (size_t i = 0; i < ids->count; i++) {
        nmo_object_t *param_obj =
            nmo_object_repository_find_by_id(repo, param_ids[i]);
        char value_buf[256];
        if (!dump_param_decoded_value(param_obj, reg, workspace,
                                      value_buf, sizeof(value_buf))) {
            continue;
        }

        nmo_guid_t type_guid = get_param_type_guid(param_obj);
        const char *name = param_obj ? nmo_object_get_name(param_obj) : NULL;
        dump_text_prefix(out, depth, last_child, branch_mask);
        fprintf(out, "    %s %zu: %s [%s] = %s\n",
                kind, i, (name && name[0]) ? name : "(unnamed)",
                resolve_type(reg, type_guid), value_buf);
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

    char scratch[256];
    bool has_any = false;
    const nmo_object_id_t *local_ids =
        (const nmo_object_id_t *)bs->local_parameters.data;
    for (size_t i = 0; i < bs->local_parameters.count && !has_any; i++) {
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, local_ids[i]);
        has_any = dump_param_decoded_value(obj, reg, workspace,
                                           scratch, sizeof(scratch));
    }
    const nmo_object_id_t *out_ids =
        (const nmo_object_id_t *)bs->out_parameters.data;
    for (size_t i = 0; i < bs->out_parameters.count && !has_any; i++) {
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, out_ids[i]);
        has_any = dump_param_decoded_value(obj, reg, workspace,
                                           scratch, sizeof(scratch));
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
    const nmo_object_id_t *param_ids = (const nmo_object_id_t *)ids->data;
    for (size_t i = 0; i < ids->count; i++) {
        nmo_object_t *param_obj =
            nmo_object_repository_find_by_id(repo, param_ids[i]);
        char value_buf[256];
        if (!dump_param_decoded_value(param_obj, reg, workspace,
                                      value_buf, sizeof(value_buf))) {
            continue;
        }

        yyjson_mut_val *item = yyjson_mut_obj(doc);
        nmo_cli_json_add_str_safe(doc, item, "kind", kind);
        yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
        yyjson_mut_obj_add_uint(doc, item, "id", param_ids[i]);
        const char *name = param_obj ? nmo_object_get_name(param_obj) : NULL;
        nmo_cli_json_add_str_safe(doc, item, "name",
                                  (name && name[0]) ? name : "");
        nmo_guid_t type_guid = get_param_type_guid(param_obj);
        char guid_buf[24];
        dump_guid_to_string(type_guid, guid_buf, sizeof(guid_buf));
        nmo_cli_json_add_str_safe(doc, item, "type_guid", guid_buf);
        nmo_cli_json_add_str_safe(doc, item, "type_name",
                                  resolve_type(reg, type_guid));
        nmo_cli_json_add_str_safe(doc, item, "decoded_value", value_buf);
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
        const nmo_object_id_t *link_ids =
            (const nmo_object_id_t *)bs->sub_behavior_links.data;
        for (size_t i = 0; i < bs->sub_behavior_links.count; i++) {
            nmo_object_t *link_obj =
                nmo_object_repository_find_by_id(repo, link_ids[i]);
            if (!link_obj || !link_obj->state) {
                continue;
            }
            const nmo_behaviorlink_state_t *link =
                (const nmo_behaviorlink_state_t *)link_obj->state;
            nmo_object_id_t src_owner =
                dump_io_owner(bidx, link->in_io_id, root_id);
            nmo_object_id_t tgt_owner =
                dump_io_owner(bidx, link->out_io_id, root_id);
            fprintf(out, "  %s.%s -> %s.%s",
                    dump_owner_display_name(repo, root_id, root_name, src_owner),
                    resolve_name(repo, link->in_io_id),
                    dump_owner_display_name(repo, root_id, root_name, tgt_owner),
                    resolve_name(repo, link->out_io_id));
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
        const nmo_object_id_t *link_ids =
            (const nmo_object_id_t *)bs->sub_behavior_links.data;
        for (size_t i = 0; i < bs->sub_behavior_links.count; i++) {
            nmo_object_t *link_obj =
                nmo_object_repository_find_by_id(repo, link_ids[i]);
            if (!link_obj || !link_obj->state) {
                continue;
            }
            const nmo_behaviorlink_state_t *link =
                (const nmo_behaviorlink_state_t *)link_obj->state;
            nmo_object_id_t src_owner =
                dump_io_owner(bidx, link->in_io_id, root_id);
            nmo_object_id_t tgt_owner =
                dump_io_owner(bidx, link->out_io_id, root_id);

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "link_id", link_ids[i]);
            yyjson_mut_obj_add_uint(doc, item, "source_io_id", link->in_io_id);
            nmo_cli_json_add_str_safe(doc, item, "source_io_name",
                                      resolve_name(repo, link->in_io_id));
            yyjson_mut_obj_add_uint(doc, item, "source_owner_id", src_owner);
            nmo_cli_json_add_str_safe(
                doc, item, "source_owner_name",
                dump_owner_display_name(repo, root_id, root_name, src_owner));
            yyjson_mut_obj_add_uint(doc, item, "target_io_id", link->out_io_id);
            nmo_cli_json_add_str_safe(doc, item, "target_io_name",
                                      resolve_name(repo, link->out_io_id));
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
        const nmo_object_id_t *ids =
            (const nmo_object_id_t *)bs->local_parameters.data;
        for (size_t i = 0; i < bs->local_parameters.count &&
                           source_count < source_cap; i++) {
            sources[source_count].param_id = ids[i];
            sources[source_count].owner_id = root_id;
            sources[source_count].owner_name =
                (root_name && root_name[0]) ? root_name : "(root)";
            sources[source_count].param_name = resolve_name(repo, ids[i]);
            source_count++;
        }
    }

    if (bs->sub_behaviors.data) {
        const nmo_object_id_t *sub_ids =
            (const nmo_object_id_t *)bs->sub_behaviors.data;
        for (size_t si = 0; si < bs->sub_behaviors.count; si++) {
            nmo_object_t *sub =
                nmo_object_repository_find_by_id(repo, sub_ids[si]);
            if (!sub || !sub->state) {
                continue;
            }
            const nmo_behavior_state_t *sub_bs =
                (const nmo_behavior_state_t *)sub->state;
            const char *sub_name = nmo_object_get_name(sub);
            if (!sub_name || !sub_name[0]) {
                sub_name = "(unnamed)";
            }
            const nmo_object_id_t *pids =
                (const nmo_object_id_t *)sub_bs->out_parameters.data;
            for (size_t pi = 0; pi < sub_bs->out_parameters.count &&
                               source_count < source_cap; pi++) {
                sources[source_count].param_id = pids[pi];
                sources[source_count].owner_id = sub_ids[si];
                sources[source_count].owner_name = sub_name;
                sources[source_count].param_name = resolve_name(repo, pids[pi]);
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
    const nmo_object_id_t *sub_ids =
        (const nmo_object_id_t *)bs->sub_behaviors.data;
    for (size_t si = 0; si < bs->sub_behaviors.count; si++) {
        nmo_object_t *sub = nmo_object_repository_find_by_id(repo, sub_ids[si]);
        if (!sub || !sub->state) {
            continue;
        }
        const nmo_behavior_state_t *sub_bs =
            (const nmo_behavior_state_t *)sub->state;
        const char *sub_name = nmo_object_get_name(sub);
        if (!sub_name || !sub_name[0]) {
            sub_name = "(unnamed)";
        }
        const nmo_object_id_t *pids =
            (const nmo_object_id_t *)sub_bs->in_parameters.data;
        for (size_t pi = 0; pi < sub_bs->in_parameters.count; pi++) {
            nmo_object_t *pin_obj =
                nmo_object_repository_find_by_id(repo, pids[pi]);
            if (!pin_obj || !pin_obj->state) {
                continue;
            }
            const nmo_parameterin_state_t *pin =
                (const nmo_parameterin_state_t *)pin_obj->state;
            if (!pin || pin->source_id == 0) {
                continue;
            }

            const dump_flow_source_t *src =
                dump_find_flow_source(sources, source_count, pin->source_id);
            const char *src_owner = src ? src->owner_name : "(external)";
            const char *src_name =
                src ? src->param_name : resolve_name(repo, pin->source_id);
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

        const nmo_object_id_t *sub_ids =
            (const nmo_object_id_t *)bs->sub_behaviors.data;
        for (size_t si = 0; si < bs->sub_behaviors.count; si++) {
            nmo_object_t *sub =
                nmo_object_repository_find_by_id(repo, sub_ids[si]);
            if (!sub || !sub->state) {
                continue;
            }
            const nmo_behavior_state_t *sub_bs =
                (const nmo_behavior_state_t *)sub->state;
            const char *sub_name = nmo_object_get_name(sub);
            if (!sub_name || !sub_name[0]) {
                sub_name = "(unnamed)";
            }
            const nmo_object_id_t *pids =
                (const nmo_object_id_t *)sub_bs->in_parameters.data;
            for (size_t pi = 0; pi < sub_bs->in_parameters.count; pi++) {
                nmo_object_t *pin_obj =
                    nmo_object_repository_find_by_id(repo, pids[pi]);
                if (!pin_obj || !pin_obj->state) {
                    continue;
                }
                const nmo_parameterin_state_t *pin =
                    (const nmo_parameterin_state_t *)pin_obj->state;
                if (!pin || pin->source_id == 0) {
                    continue;
                }

                const dump_flow_source_t *src =
                    dump_find_flow_source(sources, source_count,
                                          pin->source_id);
                const char *src_owner = src ? src->owner_name : "(external)";
                const char *src_name =
                    src ? src->param_name : resolve_name(repo, pin->source_id);
                nmo_guid_t type_guid = get_param_type_guid(pin_obj);
                char guid_buf[24];
                dump_guid_to_string(type_guid, guid_buf, sizeof(guid_buf));

                yyjson_mut_val *item = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, item, "source_id",
                                        pin->source_id);
                nmo_cli_json_add_str_safe(doc, item, "source_name",
                                          src_name ? src_name : "");
                yyjson_mut_obj_add_uint(doc, item, "source_owner_id",
                                        src ? src->owner_id : 0);
                nmo_cli_json_add_str_safe(doc, item, "source_owner_name",
                                          src_owner);
                yyjson_mut_obj_add_uint(doc, item, "target_id", pids[pi]);
                nmo_cli_json_add_str_safe(doc, item, "target_name",
                                          resolve_name(repo, pids[pi]));
                yyjson_mut_obj_add_uint(doc, item, "target_owner_id",
                                        sub_ids[si]);
                nmo_cli_json_add_str_safe(doc, item, "target_owner_name",
                                          sub_name);
                nmo_cli_json_add_str_safe(doc, item, "type_guid", guid_buf);
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

        const nmo_object_id_t *pids = (const nmo_object_id_t *)bs->in_parameters.data;
        for (size_t i = 0; i < bs->in_parameters.count && i < 6; i++) {
            if (i > 0) fprintf(out, ", ");
            nmo_object_t *p = nmo_object_repository_find_by_id(repo, pids[i]);
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
        const nmo_object_id_t *sub_ids = (const nmo_object_id_t *)bs->sub_behaviors.data;
        uint32_t next_mask = branch_mask;
        if (depth > 0 && !last_child) {
            next_mask |= (1u << (unsigned)depth);
        }
        for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
            bool is_last = (i == bs->sub_behaviors.count - 1);
            dump_behavior_tree(out, repo, reg, workspace, sub_ids[i],
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
        char guid_buf[24];
        snprintf(guid_buf, sizeof(guid_buf), "%08X-%08X",
                 bs->block_guid.d1, bs->block_guid.d2);
        nmo_cli_json_add_str_safe(doc, node, "bb_guid", guid_buf);
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
        const nmo_object_id_t *sub_ids =
            (const nmo_object_id_t *)bs->sub_behaviors.data;
        for (size_t i = 0; i < bs->sub_behaviors.count; i++) {
            dump_behavior_tree_json(doc, arr, repo, reg, bb_reg,
                                    workspace, sub_ids[i], depth + 1,
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

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    const nmo_behavior_index_t *bidx = NULL;
    if (include_flows) {
        if (nmo_session_ensure_behavior_acceleration(c.session) != NMO_OK) {
            fprintf(stderr, "Error: Failed to build behavior acceleration\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        bidx = nmo_session_get_behavior_index(c.session);
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

