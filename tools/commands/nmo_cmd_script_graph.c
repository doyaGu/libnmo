/**
 * @file nmo_cmd_script_graph.c
 * @brief nmo script graph: the behavior graph of a script as records or DOT.
 */

#include "nmo_cmd_script_internal.h"

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
        NMO_OPT_DEF_DEPTH,
        {"--id", "-i", NMO_OPT_UINT, "Script root behavior object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Script root behavior name"},
    };
    enum { OPT_DOT, OPT_DEPTH, OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t result = NMO_OPT_RESULT(vals, pos);
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
            .id = nmo_opt_uint_or(&vals[OPT_ID], 0),
            .positional_id = positional_id,
            .name = nmo_opt_str(&vals[OPT_NAME]),
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
        *out_depth = nmo_opt_uint_or(&vals[OPT_DEPTH], UINT32_MAX);
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

/* DOT label text, escaped for a double-quoted DOT string; free() it. */
static char *dot_label_dup(const char *label)
{
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

static bool script_graph_add_item(nmo_cli_record_array_t *arr,
                                  nmo_cli_record_t *item,
                                  bool ok)
{
    if (!ok) {
        nmo_cli_record_free(item);
        return false;
    }
    return nmo_cli_record_array_add(arr, item);
}

static bool script_graph_add_endpoint(nmo_cli_record_t *parent,
                                      const char *key,
                                      const nmo_script_edit_endpoint_t *endpoint)
{
    nmo_cli_record_t *value = nmo_cli_record_object(parent, key);
    return value != NULL &&
           nmo_cli_record_uint(value, "object_id", NULL, endpoint->object_id) &&
           nmo_cli_record_uint(value, "owner_behavior_id", NULL,
                               endpoint->owner_behavior_id) &&
           nmo_cli_record_int(value, "owner_index", NULL, endpoint->owner_index) &&
           nmo_cli_record_uint(value, "kind", NULL, endpoint->kind);
}

/* JSON: "nodes", "control_edges" and "data_edges". Absent from text. */
static bool script_graph_add_elements(nmo_cli_record_t *rec,
                                      const nmo_script_edit_graph_t *graph)
{
    size_t node_count = 0;
    size_t control_edge_count = 0;
    size_t data_edge_count = 0;
    const nmo_script_edit_node_t *nodes =
        nmo_script_edit_graph_nodes(graph, &node_count);
    const nmo_script_edit_control_edge_t *control_edges =
        nmo_script_edit_graph_control_edges(graph, &control_edge_count);
    const nmo_script_edit_data_edge_t *data_edges =
        nmo_script_edit_graph_data_edges(graph, &data_edge_count);

    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, "nodes", NULL);
    bool ok = arr != NULL;
    for (size_t i = 0; ok && i < node_count; ++i) {
        const nmo_script_edit_node_t *node = &nodes[i];
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = script_graph_add_item(arr, item, item != NULL &&
            nmo_cli_record_uint(item, "object_id", NULL, node->object_id) &&
            nmo_cli_record_str(item, "kind", NULL, node_kind_name(node->kind)) &&
            nmo_cli_record_str_opt(item, "name", NULL, node->name, NULL) &&
            nmo_cli_record_str_opt(item, "class_name", NULL, node->class_name, NULL) &&
            nmo_cli_record_uint(item, "class_id", NULL, node->class_id) &&
            nmo_cli_record_uint(item, "depth", NULL, node->depth) &&
            nmo_cli_record_uint(item, "parent_behavior_id", NULL,
                                node->parent_behavior_id) &&
            nmo_cli_record_uint(item, "owner_behavior_id", NULL,
                                node->owner_behavior_id) &&
            nmo_cli_record_int(item, "owner_slot_index", NULL,
                               node->owner_slot_index) &&
            nmo_cli_record_uint(item, "owner_slot_kind", NULL,
                                node->owner_slot_kind));
    }

    arr = ok ? nmo_cli_record_array(rec, "control_edges", NULL) : NULL;
    ok = arr != NULL;
    for (size_t i = 0; ok && i < control_edge_count; ++i) {
        const nmo_script_edit_control_edge_t *edge = &control_edges[i];
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = script_graph_add_item(arr, item, item != NULL &&
            nmo_cli_record_uint(item, "link_id", NULL, edge->link_id) &&
            script_graph_add_endpoint(item, "source", &edge->source) &&
            script_graph_add_endpoint(item, "target", &edge->target) &&
            nmo_cli_record_int(item, "activation_delay", NULL,
                               edge->activation_delay) &&
            nmo_cli_record_int(item, "initial_activation_delay", NULL,
                               edge->initial_activation_delay));
    }

    arr = ok ? nmo_cli_record_array(rec, "data_edges", NULL) : NULL;
    ok = arr != NULL;
    for (size_t i = 0; ok && i < data_edge_count; ++i) {
        const nmo_script_edit_data_edge_t *edge = &data_edges[i];
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = script_graph_add_item(arr, item, item != NULL &&
            nmo_cli_record_uint(item, "source_parameter_id", NULL,
                                edge->source_parameter_id) &&
            nmo_cli_record_uint(item, "target_parameter_id", NULL,
                                edge->target_parameter_id) &&
            nmo_cli_record_uint(item, "source_owner_id", NULL, edge->source_owner_id) &&
            nmo_cli_record_uint(item, "target_owner_id", NULL, edge->target_owner_id) &&
            nmo_cli_record_str_fmt(item, "type_guid", NULL, "%08X-%08X",
                                   edge->type_guid.d1, edge->type_guid.d2) &&
            nmo_cli_record_bool(item, "shared", NULL, edge->shared));
    }
    return ok;
}

/* The graph summary: counts and reference validation. */
static bool script_graph_add_summary(nmo_cli_record_t *rec,
                                     const nmo_script_edit_graph_t *graph,
                                     nmo_object_id_t behavior_id)
{
    size_t control_edge_count = 0;
    size_t data_edge_count = 0;
    size_t broken_ref_count = 0;
    bool edit_ready = nmo_script_edit_graph_edit_ready(graph);
    bool owner_index = nmo_script_edit_graph_owner_index_available(graph);
    nmo_script_edit_graph_control_edges(graph, &control_edge_count);
    nmo_script_edit_graph_data_edges(graph, &data_edge_count);
    nmo_status_t ref_status = nmo_script_edit_graph_reference_validation_status(
        graph, &broken_ref_count);

    bool ok = nmo_cli_record_uint(rec, "root_behavior_id", NULL,
                                  nmo_script_edit_graph_root_behavior_id(graph)) &&
              nmo_cli_record_text_fmt(rec, "Script Graph", "%u", behavior_id) &&
              nmo_cli_record_bool(rec, "edit_ready", "Edit ready", edit_ready) &&
              nmo_cli_record_set_text(rec, edit_ready ? "yes" : "no") &&
              nmo_cli_record_bool(rec, "owner_index_available", "Owner index",
                                  owner_index) &&
              nmo_cli_record_set_text(rec, owner_index ? "available" : "missing") &&
              nmo_cli_record_uint(rec, "node_count", "Nodes",
                                  (uint64_t)nmo_script_edit_graph_node_count(graph)) &&
              nmo_cli_record_text_fmt(rec, "Control edges", "%zu", control_edge_count) &&
              nmo_cli_record_text_fmt(rec, "Data edges", "%zu", data_edge_count);

    nmo_cli_record_t *validation =
        ok ? nmo_cli_record_object(rec, "reference_validation") : NULL;
    return validation != NULL &&
           nmo_cli_record_int(validation, "status", NULL, ref_status) &&
           nmo_cli_record_str(validation, "status_name", NULL,
                              nmo_error_string(ref_status)) &&
           nmo_cli_record_uint(validation, "broken_count", NULL,
                               (uint64_t)broken_ref_count) &&
           nmo_cli_record_text_fmt(validation, "Reference validation", "%s (%zu broken)",
                                   nmo_error_string(ref_status), broken_ref_count);
}

/* DOT digraph of the graph: its nodes, then control and data edges. */
static bool script_graph_add_dot(nmo_cli_record_t *rec,
                                 const nmo_script_edit_graph_t *graph,
                                 nmo_object_id_t behavior_id)
{
    size_t node_count = 0;
    size_t control_edge_count = 0;
    size_t data_edge_count = 0;
    const nmo_script_edit_node_t *nodes =
        nmo_script_edit_graph_nodes(graph, &node_count);
    const nmo_script_edit_control_edge_t *control_edges =
        nmo_script_edit_graph_control_edges(graph, &control_edge_count);
    const nmo_script_edit_data_edge_t *data_edges =
        nmo_script_edit_graph_data_edges(graph, &data_edge_count);

    bool ok = nmo_cli_record_raw_fmt(rec, "digraph script_%u {\n", behavior_id);
    for (size_t i = 0; ok && i < node_count; ++i) {
        const nmo_script_edit_node_t *node = &nodes[i];
        char *label = NULL;
        if (node->name && node->name[0] != '\0') {
            label = dot_label_dup(node->name);
        } else {
            char *fallback = nmo_tool_strdup_fmt("%s #%u", node_kind_name(node->kind),
                                                 node->object_id);
            label = fallback ? dot_label_dup(fallback) : NULL;
            free(fallback);
        }
        ok = label != NULL &&
             nmo_cli_record_raw_fmt(rec, "  n%u [label=\"%s\"];\n",
                                    node->object_id, label);
        free(label);
    }
    for (size_t i = 0; ok && i < control_edge_count; ++i) {
        ok = nmo_cli_record_raw_fmt(rec, "  n%u -> n%u [label=\"ctrl:%u\"];\n",
                                    control_edges[i].source.object_id,
                                    control_edges[i].target.object_id,
                                    control_edges[i].link_id);
    }
    for (size_t i = 0; ok && i < data_edge_count; ++i) {
        ok = nmo_cli_record_raw_fmt(rec, "  n%u -> n%u [style=dashed,label=\"data\"];\n",
                                    data_edges[i].source_parameter_id,
                                    data_edges[i].target_parameter_id);
    }
    return ok && nmo_cli_record_raw(rec, "}\n");
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
    int exit_code = NMO_CLI_EXIT_SUCCESS;
    int rc = 0;

    rc = nmo_core_resolve_one_object(&c, selector, &behavior, &behavior_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: %s\n", usage);
        exit_code = rc;
        goto cleanup;
    }

    rc = (int)nmo_script_edit_graph_build(c.workspace, behavior_id,
                                          depth, &graph);
    if (rc != NMO_OK) {
        const char *detail = nmo_last_error_message();
        if (detail && detail[0] != '\0') {
            fprintf(stderr, "Error: %s\n", detail);
        } else {
            fprintf(stderr, "Error: Failed to build script edit graph\n");
        }
        exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
        goto cleanup;
    }
    (void)behavior;

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (emit_dot) {
        /* DOT is written as is, in every output format */
        if (rec && script_graph_add_dot(rec, graph, behavior_id)) {
            nmo_cli_record_print_kv(rec, c.out, 0, false);
        }
        nmo_cli_record_free(rec);
        goto cleanup;
    }

    bool ok = rec != NULL &&
              script_graph_add_summary(rec, graph, behavior_id) &&
              script_graph_add_elements(rec, graph);
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    exit_code = nmo_cmd_ctx_emit_record(&c, rec, "script.graph", 0, false);

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

    if (strcmp(argv[0], "view") == 0 || strcmp(argv[0], "v") == 0) {
        return nmo_cmd_script_view_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "xref") == 0 || strcmp(argv[0], "x") == 0) {
        return nmo_cmd_script_xref_in_session(ctx, argc, argv);
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
