/**
 * @file nmo_cmd_script.c
 * @brief CLI script graph command implementation.
 */

#include "nmo_cmd_script.h"

#include "../nmo_cli_json.h"
#include "../nmo_cmd_core.h"
#include "../nmo_opt.h"

#include "behavior/nmo_script_edit_graph.h"
#include "core/nmo_error.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool parse_script_graph_args(int argc,
                                    char **argv,
                                    bool expect_file_operand,
                                    nmo_core_object_selector_t *out_selector,
                                    const char **out_file,
                                    bool *out_dot,
                                    uint32_t *out_depth)
{
    static const nmo_opt_def_t opts[] = {
        {"--dot", NULL, NMO_OPT_FLAG, "Emit DOT graph output"},
        {"--depth", "-d", NMO_OPT_UINT, "Recursion depth (default: unlimited)"},
        {"--id", "-i", NMO_OPT_UINT, "Script root behavior object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Script root behavior name"},
    };
    enum { OPT_DOT, OPT_DEPTH, OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t result = {
        .vals = vals,
        .pos_args = pos,
        .pos_capacity = 16
    };
    bool has_selector_opt = false;
    const char *positional_id = NULL;
    const char *file_path = NULL;

    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &result) < 0) {
        return false;
    }

    has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    if (expect_file_operand) {
        if ((has_selector_opt && result.pos_count < 1) ||
            (!has_selector_opt && result.pos_count < 2)) {
            return false;
        }
        positional_id = has_selector_opt ? NULL : result.pos_args[0];
        file_path = result.pos_args[result.pos_count - 1];
    } else if (has_selector_opt) {
        if (result.pos_count != 0) {
            return false;
        }
    } else {
        if (result.pos_count != 1) {
            return false;
        }
        positional_id = result.pos_args[0];
    }

    if (out_selector) {
        *out_selector = (nmo_core_object_selector_t){
            .has_id = vals[OPT_ID].present,
            .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
            .positional_id = positional_id,
            .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
            .required_base_class = NMO_CID_BEHAVIOR,
            .selector_label = "Script root",
            .type_label = "CKBehavior"
        };
    }
    if (out_file) {
        *out_file = file_path;
    }
    if (out_dot) {
        *out_dot = vals[OPT_DOT].val.flag;
    }
    if (out_depth) {
        *out_depth = vals[OPT_DEPTH].present ? vals[OPT_DEPTH].val.u : UINT32_MAX;
    }

    return true;
}

static const char *node_kind_name(nmo_script_edit_node_kind_t kind)
{
    switch (kind) {
    case NMO_SCRIPT_EDIT_NODE_BEHAVIOR:
        return "behavior";
    case NMO_SCRIPT_EDIT_NODE_IO:
        return "io";
    case NMO_SCRIPT_EDIT_NODE_PARAMETER:
        return "parameter";
    case NMO_SCRIPT_EDIT_NODE_OPERATION:
        return "operation";
    case NMO_SCRIPT_EDIT_NODE_LINK:
        return "link";
    default:
        return "unknown";
    }
}

static void guid_to_string(nmo_guid_t guid, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0u) {
        return;
    }
    snprintf(buffer, buffer_size, "%08X-%08X", guid.d1, guid.d2);
}

static void dot_write_label(FILE *out, const char *label)
{
    const unsigned char *p = NULL;

    if (!out || !label) {
        return;
    }

    for (p = (const unsigned char *)label; *p; ++p) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', out);
            fputc((char)*p, out);
        } else if (*p == '\n' || *p == '\r') {
            fputs("\\n", out);
        } else if (*p == '\t') {
            fputs("\\t", out);
        } else if (isprint(*p)) {
            fputc((char)*p, out);
        } else {
            fputc('?', out);
        }
    }
}

static void add_endpoint_json(yyjson_mut_doc *doc,
                              yyjson_mut_val *parent,
                              const char *key,
                              const nmo_script_edit_endpoint_t *endpoint)
{
    yyjson_mut_val *value = yyjson_mut_obj(doc);

    yyjson_mut_obj_add_uint(doc, value, "object_id", endpoint->object_id);
    yyjson_mut_obj_add_uint(doc, value, "owner_behavior_id",
                            endpoint->owner_behavior_id);
    yyjson_mut_obj_add_int(doc, value, "owner_index", endpoint->owner_index);
    yyjson_mut_obj_add_uint(doc, value, "kind", endpoint->kind);
    yyjson_mut_obj_add_val(doc, parent, key, value);
}

static int script_graph_run(nmo_cmd_ctx_t *ctx,
                            const nmo_core_object_selector_t *selector,
                            bool emit_dot,
                            uint32_t depth,
                            bool close_ctx,
                            const char *usage)
{
    nmo_cmd_ctx_t c = *ctx;
    nmo_object_t *behavior = NULL;
    nmo_object_id_t behavior_id = 0;
    nmo_script_edit_graph_t *graph = NULL;
    size_t node_count = 0;
    size_t control_edge_count = 0;
    size_t data_edge_count = 0;
    size_t broken_ref_count = 0;
    nmo_status_t ref_status = NMO_OK;
    int exit_code = NMO_CLI_EXIT_SUCCESS;
    int rc = 0;

    rc = nmo_core_resolve_one_object(&c, selector, &behavior, &behavior_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: %s\n", usage);
        exit_code = rc;
        goto cleanup;
    }

    rc = (int)nmo_script_edit_graph_build(c.ctx, c.session, behavior_id,
                                          depth, &graph);
    if (rc != NMO_OK) {
        char detail[256] = {0};
        if (nmo_last_error_message_copy(detail, sizeof(detail)) > 0u) {
            fprintf(stderr, "Error: %s\n", detail);
        } else {
            fprintf(stderr, "Error: Failed to build script edit graph\n");
        }
        exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
        goto cleanup;
    }

    node_count = nmo_script_edit_graph_node_count(graph);
    nmo_script_edit_graph_control_edges(graph, &control_edge_count);
    nmo_script_edit_graph_data_edges(graph, &data_edge_count);
    ref_status = nmo_script_edit_graph_reference_validation_status(
        graph, &broken_ref_count);
    (void)behavior;

    if (emit_dot) {
        const nmo_script_edit_node_t *nodes = NULL;
        const nmo_script_edit_control_edge_t *control_edges = NULL;
        const nmo_script_edit_data_edge_t *data_edges = NULL;
        size_t i = 0;

        nodes = nmo_script_edit_graph_nodes(graph, &node_count);
        control_edges = nmo_script_edit_graph_control_edges(graph,
                                                            &control_edge_count);
        data_edges = nmo_script_edit_graph_data_edges(graph, &data_edge_count);

        fprintf(c.out, "digraph script_%u {\n", behavior_id);
        for (i = 0; i < node_count; ++i) {
            const nmo_script_edit_node_t *node = &nodes[i];
            fprintf(c.out, "  n%u [label=\"", node->object_id);
            if (node->name && node->name[0] != '\0') {
                dot_write_label(c.out, node->name);
            } else {
                char fallback[64];
                snprintf(fallback, sizeof(fallback), "%s #%u",
                         node_kind_name(node->kind), node->object_id);
                dot_write_label(c.out, fallback);
            }
            fprintf(c.out, "\"];\n");
        }
        for (i = 0; i < control_edge_count; ++i) {
            fprintf(c.out,
                    "  n%u -> n%u [label=\"ctrl:%u\"];\n",
                    control_edges[i].source.object_id,
                    control_edges[i].target.object_id,
                    control_edges[i].link_id);
        }
        for (i = 0; i < data_edge_count; ++i) {
            fprintf(c.out,
                    "  n%u -> n%u [style=dashed,label=\"data\"];\n",
                    data_edges[i].source_parameter_id,
                    data_edges[i].target_parameter_id);
        }
        fprintf(c.out, "}\n");
        goto cleanup;
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *reference_validation = yyjson_mut_obj(doc);
        yyjson_mut_val *nodes_json = yyjson_mut_arr(doc);
        yyjson_mut_val *control_edges_json = yyjson_mut_arr(doc);
        yyjson_mut_val *data_edges_json = yyjson_mut_arr(doc);
        const nmo_script_edit_node_t *nodes = NULL;
        const nmo_script_edit_control_edge_t *control_edges = NULL;
        const nmo_script_edit_data_edge_t *data_edges = NULL;
        size_t i = 0;

        nodes = nmo_script_edit_graph_nodes(graph, &node_count);
        control_edges = nmo_script_edit_graph_control_edges(graph,
                                                            &control_edge_count);
        data_edges = nmo_script_edit_graph_data_edges(graph, &data_edge_count);

        yyjson_mut_obj_add_uint(doc, data, "root_behavior_id",
                                nmo_script_edit_graph_root_behavior_id(graph));
        yyjson_mut_obj_add_bool(doc, data, "edit_ready",
                                nmo_script_edit_graph_edit_ready(graph));
        yyjson_mut_obj_add_bool(doc, data, "owner_index_available",
                                nmo_script_edit_graph_owner_index_available(graph));
        yyjson_mut_obj_add_uint(doc, data, "node_count", (uint64_t)node_count);

        yyjson_mut_obj_add_int(doc, reference_validation, "status", ref_status);
        nmo_cli_json_add_str_safe(doc, reference_validation, "status_name",
                                  nmo_error_string(ref_status));
        yyjson_mut_obj_add_uint(doc, reference_validation, "broken_count",
                                (uint64_t)broken_ref_count);
        yyjson_mut_obj_add_val(doc, data, "reference_validation",
                               reference_validation);

        for (i = 0; i < node_count; ++i) {
            yyjson_mut_val *node = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, node, "object_id", nodes[i].object_id);
            nmo_cli_json_add_str_safe(doc, node, "kind",
                                      node_kind_name(nodes[i].kind));
            if (nodes[i].name && nodes[i].name[0] != '\0') {
                nmo_cli_json_add_str_safe(doc, node, "name", nodes[i].name);
            }
            if (nodes[i].class_name && nodes[i].class_name[0] != '\0') {
                nmo_cli_json_add_str_safe(doc, node, "class_name",
                                          nodes[i].class_name);
            }
            yyjson_mut_obj_add_uint(doc, node, "class_id", nodes[i].class_id);
            yyjson_mut_obj_add_uint(doc, node, "depth", nodes[i].depth);
            yyjson_mut_obj_add_uint(doc, node, "parent_behavior_id",
                                    nodes[i].parent_behavior_id);
            yyjson_mut_obj_add_uint(doc, node, "owner_behavior_id",
                                    nodes[i].owner_behavior_id);
            yyjson_mut_obj_add_int(doc, node, "owner_slot_index",
                                   nodes[i].owner_slot_index);
            yyjson_mut_obj_add_uint(doc, node, "owner_slot_kind",
                                    nodes[i].owner_slot_kind);
            yyjson_mut_arr_add_val(nodes_json, node);
        }
        yyjson_mut_obj_add_val(doc, data, "nodes", nodes_json);

        for (i = 0; i < control_edge_count; ++i) {
            yyjson_mut_val *edge = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, edge, "link_id", control_edges[i].link_id);
            add_endpoint_json(doc, edge, "source", &control_edges[i].source);
            add_endpoint_json(doc, edge, "target", &control_edges[i].target);
            yyjson_mut_obj_add_int(doc, edge, "activation_delay",
                                   control_edges[i].activation_delay);
            yyjson_mut_obj_add_int(doc, edge, "initial_activation_delay",
                                   control_edges[i].initial_activation_delay);
            yyjson_mut_arr_add_val(control_edges_json, edge);
        }
        yyjson_mut_obj_add_val(doc, data, "control_edges", control_edges_json);

        for (i = 0; i < data_edge_count; ++i) {
            yyjson_mut_val *edge = yyjson_mut_obj(doc);
            char guid_buffer[24];
            yyjson_mut_obj_add_uint(doc, edge, "source_parameter_id",
                                    data_edges[i].source_parameter_id);
            yyjson_mut_obj_add_uint(doc, edge, "target_parameter_id",
                                    data_edges[i].target_parameter_id);
            yyjson_mut_obj_add_uint(doc, edge, "source_owner_id",
                                    data_edges[i].source_owner_id);
            yyjson_mut_obj_add_uint(doc, edge, "target_owner_id",
                                    data_edges[i].target_owner_id);
            guid_to_string(data_edges[i].type_guid, guid_buffer,
                           sizeof(guid_buffer));
            nmo_cli_json_add_str_safe(doc, edge, "type_guid", guid_buffer);
            yyjson_mut_obj_add_bool(doc, edge, "shared", data_edges[i].shared);
            yyjson_mut_arr_add_val(data_edges_json, edge);
        }
        yyjson_mut_obj_add_val(doc, data, "data_edges", data_edges_json);

        exit_code = nmo_cmd_ctx_json_end(&c, doc, data, "script.graph");
        goto cleanup;
    }

    fprintf(c.out, "Script Graph: %u\n", behavior_id);
    fprintf(c.out, "Edit ready: %s\n",
            nmo_script_edit_graph_edit_ready(graph) ? "yes" : "no");
    fprintf(c.out, "Owner index: %s\n",
            nmo_script_edit_graph_owner_index_available(graph) ? "available" : "missing");
    fprintf(c.out, "Nodes: %zu\n", node_count);
    fprintf(c.out, "Control edges: %zu\n", control_edge_count);
    fprintf(c.out, "Data edges: %zu\n", data_edge_count);
    fprintf(c.out, "Reference validation: %s (%zu broken)\n",
            nmo_error_string(ref_status), broken_ref_count);

cleanup:
    nmo_script_edit_graph_destroy(graph);
    return close_ctx ? nmo_cmd_ctx_done(&c, exit_code) : exit_code;
}

int nmo_cmd_script_graph(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    nmo_core_object_selector_t selector = {0};
    const char *file_path = NULL;
    bool emit_dot = false;
    uint32_t depth = UINT32_MAX;
    const char *usage =
        "nmo script graph [--depth N] [--dot] [--id <id> | --name <name> | <id>] <file>";
    nmo_cmd_ctx_t ctx;
    int rc = 0;

    if (!parse_script_graph_args(argc, argv, true, &selector, &file_path,
                                 &emit_dot, &depth)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    (void)file_path;

    rc = nmo_cmd_ctx_init(&ctx, argc, argv, global);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    return script_graph_run(&ctx, &selector, emit_dot, depth, true, usage);
}

int nmo_cmd_script_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    nmo_core_object_selector_t selector = {0};
    const char *file_path = NULL;
    bool emit_dot = false;
    uint32_t depth = UINT32_MAX;
    const char *usage =
        "script graph [--depth N] [--dot] [--id <id> | --name <name> | <id>]";

    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: script graph ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "graph") != 0 && strcmp(argv[0], "g") != 0) {
        fprintf(stderr, "Unsupported script read action in session: %s\n", argv[0]);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!parse_script_graph_args(argc - 1, argv + 1, false, &selector, &file_path,
                                 &emit_dot, &depth)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: %s\n", usage);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    (void)file_path;

    return script_graph_run(ctx, &selector, emit_dot, depth, false, usage);
}
