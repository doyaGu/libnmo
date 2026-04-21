/**
 * @file nmo_cmd_script.c
 * @brief CLI script graph command implementation.
 */

#include "nmo_cmd_script.h"

#include "../nmo_cli_json.h"
#include "../nmo_cli_write.h"
#include "../nmo_cmd_core.h"
#include "../nmo_opt.h"

#include "behavior/nmo_param_value.h"
#include "behavior/nmo_script_edit.h"
#include "behavior/nmo_script_edit_graph.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "object/nmo_object_repository.h"
#include "object/builtin/nmo_parameter_schemas.h"
#include "session/nmo_context.h"
#include "type/nmo_operation_system.h"
#include "type/nmo_type_system.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Task 2 freezes the future script write option spellings in
 * tests/fixtures/script_edit_reports.md. Keep CLI long options aligned with
 * Lua option-table fields through the direct kebab-case -> snake_case mapping.
 */
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

typedef struct script_node_add_args {
    uint32_t parent_id;
    nmo_guid_t bb_guid;
    const char *name;
    nmo_object_id_t node_id;
} script_node_add_args_t;

typedef struct script_node_remove_args {
    uint32_t parent_id;
    uint32_t node_id;
} script_node_remove_args_t;

typedef struct script_io_add_args {
    uint32_t behavior_id;
    nmo_script_edit_io_kind_t kind;
    const char *name;
    nmo_object_id_t io_id;
} script_io_add_args_t;

typedef struct script_io_rename_args {
    uint32_t io_id;
    const char *name;
} script_io_rename_args_t;

typedef struct script_io_remove_args {
    uint32_t io_id;
} script_io_remove_args_t;

typedef struct script_link_add_args {
    uint32_t parent_id;
    uint32_t from_id;
    uint32_t to_id;
    uint32_t delay;
    nmo_object_id_t link_id;
} script_link_add_args_t;

typedef struct script_link_rewire_args {
    uint32_t link_id;
    uint32_t from_id;
    uint32_t to_id;
} script_link_rewire_args_t;

typedef struct script_link_set_delay_args {
    uint32_t link_id;
    uint32_t delay;
} script_link_set_delay_args_t;

typedef struct script_link_remove_args {
    uint32_t parent_id;
    uint32_t link_id;
} script_link_remove_args_t;

typedef struct script_param_add_args {
    uint32_t owner_id;
    const char *kind;
    const char *type_name;
    const char *name;
    nmo_object_id_t param_id;
} script_param_add_args_t;

typedef struct script_param_set_args {
    uint32_t param_id;
    const char *value_str;
    char *old_value;
    char *new_value;
} script_param_set_args_t;

typedef struct script_param_connect_args {
    uint32_t source_id;
    uint32_t target_id;
} script_param_connect_args_t;

typedef struct script_param_disconnect_args {
    uint32_t target_id;
} script_param_disconnect_args_t;

typedef struct script_param_remove_args {
    uint32_t param_id;
    bool detach;
} script_param_remove_args_t;

typedef struct script_op_add_args {
    uint32_t parent_id;
    nmo_guid_t op_guid;
    uint32_t in1_id;
    uint32_t in2_id;
    uint32_t out_id;
    nmo_object_id_t op_id;
} script_op_add_args_t;

typedef struct script_op_rewire_args {
    uint32_t op_id;
    uint32_t slot_flags;
    uint32_t in1_id;
    uint32_t in2_id;
    uint32_t out_id;
} script_op_rewire_args_t;

typedef struct script_op_remove_args {
    uint32_t op_id;
} script_op_remove_args_t;

static char *script_format_parameter_value(nmo_cmd_ctx_t *ctx,
                                           nmo_object_id_t param_id)
{
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *object = NULL;
    const nmo_parameter_state_t *state = NULL;
    size_t buffer_size = 512u;
    char *buffer = NULL;
    nmo_status_t rc = NMO_OK;

    if (!ctx || !ctx->session || !ctx->registry || param_id == 0u) {
        return NULL;
    }

    repo = nmo_session_get_repository(ctx->session);
    object = repo ? nmo_object_repository_find_by_id(repo, param_id) : NULL;
    state = object ? nmo_parameter_get_state(object) : NULL;
    if (!state) {
        return NULL;
    }

    buffer = (char *)malloc(buffer_size);
    if (!buffer) {
        return NULL;
    }

    rc = nmo_param_value_to_string(state, ctx->registry, ctx->session, buffer,
                                   buffer_size);
    if (rc != NMO_OK) {
        free(buffer);
        return NULL;
    }
    return buffer;
}

static void script_param_set_args_cleanup(script_param_set_args_t *args)
{
    if (!args) {
        return;
    }
    free(args->old_value);
    free(args->new_value);
    args->old_value = NULL;
    args->new_value = NULL;
}

static bool script_parse_parameter_kind(
    const char *text,
    nmo_script_edit_parameter_kind_t *out_kind)
{
    if (!text || !out_kind) {
        return false;
    }
    if (strcmp(text, "in") == 0 || strcmp(text, "input") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_IN;
        return true;
    }
    if (strcmp(text, "out") == 0 || strcmp(text, "output") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_OUT;
        return true;
    }
    if (strcmp(text, "local") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_LOCAL;
        return true;
    }
    if (strcmp(text, "shared") == 0) {
        *out_kind = NMO_SCRIPT_EDIT_PARAM_SHARED;
        return true;
    }
    return false;
}

static bool script_try_resolve_parameter_type_name(
    const nmo_type_registry_t *registry,
    const char *type_name,
    nmo_guid_t *out_guid)
{
    char alias_buf[128];
    const char *lookup_name = type_name;
    size_t alias_len = 0;

    if (!registry || !type_name || !out_guid) {
        return false;
    }

    if (nmo_type_registry_name_to_guid(registry, lookup_name, out_guid) == NMO_OK) {
        return true;
    }

    if (strncmp(type_name, "CKPGUID_", 8) == 0) {
        lookup_name = type_name + 8;
        alias_len = strlen(lookup_name);
        if (alias_len > 0 && alias_len < sizeof(alias_buf)) {
            for (size_t i = 0; i < alias_len; i++) {
                alias_buf[i] = (char)tolower((unsigned char)lookup_name[i]);
            }
            alias_buf[alias_len] = '\0';
            if (nmo_type_registry_name_to_guid(registry, alias_buf, out_guid) == NMO_OK ||
                nmo_type_registry_name_to_guid(registry, lookup_name, out_guid) == NMO_OK) {
                return true;
            }
        }
    }

    *out_guid = nmo_guid_parse(type_name);
    return !nmo_guid_is_null(*out_guid);
}

static int script_edit_finalize_tx_impl(nmo_script_edit_tx_t *tx,
                                        bool dry_run,
                                        bool validate_references,
                                        bool validate_behavior_index)
{
    nmo_status_t rc = NMO_OK;

    rc = nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_ROUNDTRIP_READY);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Script edit roundtrip validation failed: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (validate_references) {
        rc = nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_REFERENCES);
        if (rc != NMO_OK) {
            fprintf(stderr, "Error: Script edit reference validation failed: %s\n",
                    nmo_error_string(rc));
            nmo_script_edit_rollback(tx);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    }

    if (validate_behavior_index) {
        rc = nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_BEHAVIOR_INDEX);
        if (rc != NMO_OK) {
            fprintf(stderr, "Error: Script edit behavior-index validation failed: %s\n",
                    nmo_error_string(rc));
            nmo_script_edit_rollback(tx);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    }

    rc = nmo_script_edit_validate(tx, NMO_SCRIPT_EDIT_VALIDATE_INTERFACE);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Script edit interface validation failed: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (dry_run) {
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_SUCCESS;
    }

    rc = nmo_script_edit_commit(tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Script edit commit failed: %s\n",
                nmo_error_string(rc));
    }
    return rc == NMO_OK ? NMO_CLI_EXIT_SUCCESS : NMO_CLI_EXIT_INTERNAL_ERROR;
}

static int script_edit_finalize_tx(nmo_script_edit_tx_t *tx, bool dry_run)
{
    return script_edit_finalize_tx_impl(tx, dry_run, true, true);
}

static int script_node_add_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_node_add_args_t *args = (script_node_add_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script node add", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script node add: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_add_node(tx, args->parent_id, args->bb_guid,
                                  args->name, &args->node_id);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to add script node: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return script_edit_finalize_tx(tx, dry_run);
}

static int script_node_add_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_node_add_args_t *args = (script_node_add_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "parent_id", args->parent_id);
        yyjson_mut_obj_add_uint(doc, data, "node_id", args->node_id);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.node.add");
    }

    fprintf(ctx->out, "Created script node #%u in behavior #%u\n",
            args->node_id, args->parent_id);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int script_node_remove_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_node_remove_args_t *args = (script_node_remove_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script node remove", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script node remove: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_remove_node(tx, args->parent_id, args->node_id, 0u);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to remove script node: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return script_edit_finalize_tx(tx, dry_run);
}

static int script_node_remove_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_node_remove_args_t *args = (script_node_remove_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "parent_id", args->parent_id);
        yyjson_mut_obj_add_uint(doc, data, "node_id", args->node_id);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.node.remove");
    }

    fprintf(ctx->out, "Removed script node #%u from behavior #%u\n",
            args->node_id, args->parent_id);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int script_io_add_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_io_add_args_t *args = (script_io_add_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script io add", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script io add: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_add_io(tx, args->behavior_id, args->kind, args->name,
                                &args->io_id);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to add script io: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return script_edit_finalize_tx(tx, dry_run);
}

static int script_io_add_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_io_add_args_t *args = (script_io_add_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "behavior_id", args->behavior_id);
        yyjson_mut_obj_add_uint(doc, data, "io_id", args->io_id);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.io.add");
    }
    fprintf(ctx->out, "Created IO #%u on behavior #%u\n",
            args->io_id, args->behavior_id);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int script_io_rename_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_io_rename_args_t *args = (script_io_rename_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script io rename", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script io rename: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_rename_io(tx, args->io_id, args->name);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to rename script io: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return script_edit_finalize_tx(tx, dry_run);
}

static int script_io_rename_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_io_rename_args_t *args = (script_io_rename_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "io_id", args->io_id);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.io.rename");
    }
    fprintf(ctx->out, "Renamed IO #%u\n", args->io_id);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int script_io_remove_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_io_remove_args_t *args = (script_io_remove_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script io remove", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script io remove: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_remove_io(tx, args->io_id, false);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to remove script io: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return script_edit_finalize_tx(tx, dry_run);
}

static int script_io_remove_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_io_remove_args_t *args = (script_io_remove_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "io_id", args->io_id);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.io.remove");
    }
    fprintf(ctx->out, "Removed IO #%u\n", args->io_id);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int script_link_add_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_link_add_args_t *args = (script_link_add_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script link add", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script link add: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_add_behavior_link(tx, args->parent_id, args->from_id,
                                           args->to_id, args->delay,
                                           &args->link_id);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to add script link: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return script_edit_finalize_tx(tx, dry_run);
}

static int script_link_add_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_link_add_args_t *args = (script_link_add_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "parent_id", args->parent_id);
        yyjson_mut_obj_add_uint(doc, data, "link_id", args->link_id);
        yyjson_mut_obj_add_uint(doc, data, "from_id", args->from_id);
        yyjson_mut_obj_add_uint(doc, data, "to_id", args->to_id);
        yyjson_mut_obj_add_uint(doc, data, "delay", args->delay);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.link.add");
    }
    fprintf(ctx->out, "Created link #%u: #%u -> #%u in behavior #%u\n",
            args->link_id, args->from_id, args->to_id, args->parent_id);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int script_link_rewire_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_link_rewire_args_t *args = (script_link_rewire_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script link rewire", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script link rewire: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_rewire_behavior_link(tx, args->link_id, args->from_id,
                                              args->to_id);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to rewire script link: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return script_edit_finalize_tx(tx, dry_run);
}

static int script_link_rewire_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_link_rewire_args_t *args = (script_link_rewire_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "link_id", args->link_id);
        if (args->from_id != 0u) {
            yyjson_mut_obj_add_uint(doc, data, "from_id", args->from_id);
        }
        if (args->to_id != 0u) {
            yyjson_mut_obj_add_uint(doc, data, "to_id", args->to_id);
        }
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.link.rewire");
    }
    fprintf(ctx->out, "Rewired link #%u\n", args->link_id);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int script_link_set_delay_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_link_set_delay_args_t *args =
        (script_link_set_delay_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script link set-delay",
                               &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script link set-delay: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_set_behavior_link_delay(tx, args->link_id, args->delay);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to set script link delay: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return script_edit_finalize_tx(tx, dry_run);
}

static int script_link_set_delay_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_link_set_delay_args_t *args =
        (script_link_set_delay_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "link_id", args->link_id);
        yyjson_mut_obj_add_uint(doc, data, "delay", args->delay);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.link.set-delay");
    }
    fprintf(ctx->out, "Set link #%u delay to %u\n", args->link_id, args->delay);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int script_link_remove_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_link_remove_args_t *args = (script_link_remove_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script link remove", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script link remove: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_remove_behavior_link(tx, args->parent_id, args->link_id);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to remove script link: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return script_edit_finalize_tx(tx, dry_run);
}

static int script_link_remove_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_link_remove_args_t *args = (script_link_remove_args_t *)user_data;
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "parent_id", args->parent_id);
        yyjson_mut_obj_add_uint(doc, data, "link_id", args->link_id);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.link.remove");
    }
    fprintf(ctx->out, "Removed link #%u from behavior #%u\n",
            args->link_id, args->parent_id);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_script_node(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_cli_write_spec_t spec = {
        .command_name = "script.node",
        .output_required_unless_dry_run = true,
    };
    if (argc < 2 || !argv || !argv[1]) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[1], "add") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--parent", NULL, NMO_OPT_UINT, "Parent behavior ID"},
            {"--bb-guid", NULL, NMO_OPT_STRING, "Building block GUID"},
            {"--name", NULL, NMO_OPT_STRING, "Behavior name"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_PARENT, OPT_BB_GUID, OPT_NAME, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_node_add_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARENT].present || !vals[OPT_BB_GUID].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.parent_id = vals[OPT_PARENT].val.u;
        args.bb_guid = nmo_guid_parse(vals[OPT_BB_GUID].val.str);
        args.name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL;
        if (nmo_guid_is_null(args.bb_guid)) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_node_add_mutate,
            script_node_add_report,
            &args);
    }

    if (strcmp(argv[1], "remove") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--parent", NULL, NMO_OPT_UINT, "Parent behavior ID"},
            {"--node", NULL, NMO_OPT_UINT, "Node ID"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_PARENT, OPT_NODE, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_node_remove_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARENT].present || !vals[OPT_NODE].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.parent_id = vals[OPT_PARENT].val.u;
        args.node_id = vals[OPT_NODE].val.u;
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_node_remove_mutate,
            script_node_remove_report,
            &args);
    }

    return NMO_CLI_EXIT_ARG_ERROR;
}

int nmo_cmd_script_io(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_cli_write_spec_t spec = {
        .command_name = "script.io",
        .output_required_unless_dry_run = true,
    };
    if (argc < 2 || !argv || !argv[1]) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[1], "add") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--behavior", NULL, NMO_OPT_UINT, "Owner behavior ID"},
            {"--kind", NULL, NMO_OPT_STRING, "input|output"},
            {"--name", NULL, NMO_OPT_STRING, "IO name"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_BEHAVIOR, OPT_KIND, OPT_NAME, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_io_add_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_BEHAVIOR].present || !vals[OPT_KIND].present ||
            !vals[OPT_NAME].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.behavior_id = vals[OPT_BEHAVIOR].val.u;
        args.kind = strcmp(vals[OPT_KIND].val.str, "output") == 0
            ? NMO_SCRIPT_EDIT_IO_OUTPUT
            : NMO_SCRIPT_EDIT_IO_INPUT;
        args.name = vals[OPT_NAME].val.str;
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_io_add_mutate,
            script_io_add_report,
            &args);
    }

    if (strcmp(argv[1], "rename") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--io", NULL, NMO_OPT_UINT, "IO ID"},
            {"--name", NULL, NMO_OPT_STRING, "New IO name"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_IO, OPT_NAME, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_io_rename_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_IO].present || !vals[OPT_NAME].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.io_id = vals[OPT_IO].val.u;
        args.name = vals[OPT_NAME].val.str;
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_io_rename_mutate,
            script_io_rename_report,
            &args);
    }

    if (strcmp(argv[1], "remove") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--io", NULL, NMO_OPT_UINT, "IO ID"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_IO, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_io_remove_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_IO].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.io_id = vals[OPT_IO].val.u;
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_io_remove_mutate,
            script_io_remove_report,
            &args);
    }

    return NMO_CLI_EXIT_ARG_ERROR;
}

int nmo_cmd_script_link(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_cli_write_spec_t spec = {
        .command_name = "script.link",
        .output_required_unless_dry_run = true,
    };
    if (argc < 2 || !argv || !argv[1]) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[1], "add") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--parent", NULL, NMO_OPT_UINT, "Parent behavior ID"},
            {"--from", NULL, NMO_OPT_UINT, "Source IO ID"},
            {"--to", NULL, NMO_OPT_UINT, "Target IO ID"},
            {"--delay", NULL, NMO_OPT_UINT, "Activation delay"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum {
            OPT_PARENT, OPT_FROM, OPT_TO, OPT_DELAY, OPT_OUTPUT, OPT_DRY_RUN,
            OPT_COUNT
        };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_link_add_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARENT].present || !vals[OPT_FROM].present ||
            !vals[OPT_TO].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.parent_id = vals[OPT_PARENT].val.u;
        args.from_id = vals[OPT_FROM].val.u;
        args.to_id = vals[OPT_TO].val.u;
        args.delay = vals[OPT_DELAY].present ? vals[OPT_DELAY].val.u : 1u;
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_link_add_mutate,
            script_link_add_report,
            &args);
    }

    if (strcmp(argv[1], "rewire") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--link", NULL, NMO_OPT_UINT, "Link ID"},
            {"--from", NULL, NMO_OPT_UINT, "Source IO ID"},
            {"--to", NULL, NMO_OPT_UINT, "Target IO ID"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_LINK, OPT_FROM, OPT_TO, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_link_rewire_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_LINK].present ||
            (!vals[OPT_FROM].present && !vals[OPT_TO].present) ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.link_id = vals[OPT_LINK].val.u;
        args.from_id = vals[OPT_FROM].present ? vals[OPT_FROM].val.u : 0u;
        args.to_id = vals[OPT_TO].present ? vals[OPT_TO].val.u : 0u;
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_link_rewire_mutate,
            script_link_rewire_report,
            &args);
    }

    if (strcmp(argv[1], "set-delay") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--link", NULL, NMO_OPT_UINT, "Link ID"},
            {"--delay", NULL, NMO_OPT_UINT, "Activation delay"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_LINK, OPT_DELAY, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_link_set_delay_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_LINK].present || !vals[OPT_DELAY].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.link_id = vals[OPT_LINK].val.u;
        args.delay = vals[OPT_DELAY].val.u;
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_link_set_delay_mutate,
            script_link_set_delay_report,
            &args);
    }

    if (strcmp(argv[1], "remove") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--parent", NULL, NMO_OPT_UINT, "Parent behavior ID"},
            {"--link", NULL, NMO_OPT_UINT, "Link ID"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_PARENT, OPT_LINK, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_link_remove_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARENT].present || !vals[OPT_LINK].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.parent_id = vals[OPT_PARENT].val.u;
        args.link_id = vals[OPT_LINK].val.u;
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_link_remove_mutate,
            script_link_remove_report,
            &args);
    }

    return NMO_CLI_EXIT_ARG_ERROR;
}

static int script_param_add_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_param_add_args_t *args = (script_param_add_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_script_edit_parameter_kind_t kind = NMO_SCRIPT_EDIT_PARAM_IN;
    nmo_guid_t type_guid = NMO_GUID_NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!ctx || !args || !ctx->registry ||
        !script_parse_parameter_kind(args->kind, &kind)) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!script_try_resolve_parameter_type_name(ctx->registry, args->type_name,
                                                &type_guid)) {
        fprintf(stderr, "Error: Unknown parameter type '%s'\n", args->type_name);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script param add", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script param add: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_add_parameter(tx, args->owner_id, kind, type_guid,
                                       args->name, &args->param_id);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to add script parameter: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    return script_edit_finalize_tx(tx, dry_run);
}

static int script_param_add_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_param_add_args_t *args = (script_param_add_args_t *)user_data;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "owner_id", args->owner_id);
        yyjson_mut_obj_add_uint(doc, data, "param_id", args->param_id);
        nmo_cli_json_add_str_safe(doc, data, "kind", args->kind);
        nmo_cli_json_add_str_safe(doc, data, "type", args->type_name);
        nmo_cli_json_add_str_safe(doc, data, "name", args->name);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.param.add");
    }
    fprintf(ctx->out, "Created script parameter #%u in behavior #%u\n",
            args->param_id, args->owner_id);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int script_param_set_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_param_set_args_t *args = (script_param_set_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    args->old_value = script_format_parameter_value(ctx, args->param_id);
    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script param set", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script param set: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_set_parameter_value(tx, args->param_id, args->value_str);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to set script parameter: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    args->new_value = script_format_parameter_value(ctx, args->param_id);
    return script_edit_finalize_tx(tx, dry_run);
}

static int script_param_set_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_param_set_args_t *args = (script_param_set_args_t *)user_data;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "param_id", args->param_id);
        if (args->old_value) {
            nmo_cli_json_add_str_safe(doc, data, "old_value", args->old_value);
        }
        if (args->new_value) {
            nmo_cli_json_add_str_safe(doc, data, "new_value", args->new_value);
        }
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.param.set");
    }
    fprintf(ctx->out, "Updated script parameter #%u\n", args->param_id);
    if (args->old_value) {
        fprintf(ctx->out, "  Old: %s\n", args->old_value);
    }
    if (args->new_value) {
        fprintf(ctx->out, "  New: %s\n", args->new_value);
    }
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int script_param_connect_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_param_connect_args_t *args = (script_param_connect_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script param connect", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script param connect: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_connect_parameter(tx, args->source_id, args->target_id);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to connect script parameters: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return script_edit_finalize_tx(tx, dry_run);
}

static int script_param_connect_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_param_connect_args_t *args = (script_param_connect_args_t *)user_data;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "source_id", args->source_id);
        yyjson_mut_obj_add_uint(doc, data, "target_id", args->target_id);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.param.connect");
    }
    fprintf(ctx->out, "Connected parameter #%u -> #%u\n",
            args->source_id, args->target_id);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int script_param_disconnect_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_param_disconnect_args_t *args =
        (script_param_disconnect_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script param disconnect", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script param disconnect: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_disconnect_parameter(tx, args->target_id);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to disconnect script parameter: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return script_edit_finalize_tx(tx, dry_run);
}

static int script_param_disconnect_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_param_disconnect_args_t *args =
        (script_param_disconnect_args_t *)user_data;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "target_id", args->target_id);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.param.disconnect");
    }
    fprintf(ctx->out, "Disconnected parameter #%u\n", args->target_id);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int script_param_remove_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_param_remove_args_t *args = (script_param_remove_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script param remove", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script param remove: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_remove_parameter(tx, args->param_id, args->detach);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to remove script parameter: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return script_edit_finalize_tx(tx, dry_run);
}

static int script_param_remove_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_param_remove_args_t *args = (script_param_remove_args_t *)user_data;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "param_id", args->param_id);
        yyjson_mut_obj_add_bool(doc, data, "detach", args->detach);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.param.remove");
    }
    fprintf(ctx->out, "Removed script parameter #%u\n", args->param_id);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int script_op_add_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_op_add_args_t *args = (script_op_add_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script op add", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script op add: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_add_operation(tx, args->parent_id, args->op_guid,
                                       args->in1_id, args->in2_id, args->out_id,
                                       &args->op_id);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to add script operation: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return script_edit_finalize_tx(tx, dry_run);
}

static int script_op_add_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_op_add_args_t *args = (script_op_add_args_t *)user_data;
    char guid_buf[24];
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    guid_to_string(args->op_guid, guid_buf, sizeof(guid_buf));
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "parent_id", args->parent_id);
        yyjson_mut_obj_add_uint(doc, data, "op_id", args->op_id);
        nmo_cli_json_add_str_safe(doc, data, "operation_guid", guid_buf);
        if (args->in1_id != 0u) yyjson_mut_obj_add_uint(doc, data, "in1_id", args->in1_id);
        if (args->in2_id != 0u) yyjson_mut_obj_add_uint(doc, data, "in2_id", args->in2_id);
        if (args->out_id != 0u) yyjson_mut_obj_add_uint(doc, data, "out_id", args->out_id);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.op.add");
    }
    fprintf(ctx->out, "Created script operation #%u in behavior #%u\n",
            args->op_id, args->parent_id);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int script_op_rewire_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_op_rewire_args_t *args = (script_op_rewire_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script op rewire", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script op rewire: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_rewire_operation(tx, args->op_id, args->slot_flags,
                                          args->in1_id, args->in2_id, args->out_id);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to rewire script operation: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return script_edit_finalize_tx(tx, dry_run);
}

static int script_op_rewire_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_op_rewire_args_t *args = (script_op_rewire_args_t *)user_data;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "op_id", args->op_id);
        if ((args->slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN1) != 0u) {
            yyjson_mut_obj_add_uint(doc, data, "in1_id", args->in1_id);
        }
        if ((args->slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_IN2) != 0u) {
            yyjson_mut_obj_add_uint(doc, data, "in2_id", args->in2_id);
        }
        if ((args->slot_flags & NMO_SCRIPT_EDIT_OP_SLOT_OUT) != 0u) {
            yyjson_mut_obj_add_uint(doc, data, "out_id", args->out_id);
        }
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.op.rewire");
    }
    fprintf(ctx->out, "Rewired script operation #%u\n", args->op_id);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int script_op_remove_mutate(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_op_remove_args_t *args = (script_op_remove_args_t *)user_data;
    nmo_script_edit_tx_t *tx = NULL;
    nmo_status_t rc = NMO_OK;

    (void)output_path;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    rc = nmo_script_edit_begin(ctx->ctx, ctx->session, "script op remove", &tx);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to begin script op remove: %s\n",
                nmo_error_string(rc));
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    rc = nmo_script_edit_remove_operation(tx, args->op_id);
    if (rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to remove script operation: %s\n",
                nmo_error_string(rc));
        nmo_script_edit_rollback(tx);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return script_edit_finalize_tx(tx, dry_run);
}

static int script_op_remove_report(
    nmo_cmd_ctx_t *ctx,
    bool dry_run,
    const char *output_path,
    void *user_data)
{
    script_op_remove_args_t *args = (script_op_remove_args_t *)user_data;
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, data, "dry_run", dry_run);
        yyjson_mut_obj_add_uint(doc, data, "op_id", args->op_id);
        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }
        return nmo_cmd_ctx_json_end(ctx, doc, data, "script.op.remove");
    }
    fprintf(ctx->out, "Removed script operation #%u\n", args->op_id);
    if (!dry_run && output_path) {
        fprintf(ctx->out, "Saved to: %s\n", output_path);
    }
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_script_param(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_cli_write_spec_t spec = {
        .command_name = "script.param",
        .output_required_unless_dry_run = true,
    };
    if (argc < 2 || !argv || !argv[1]) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[1], "add") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--owner", NULL, NMO_OPT_UINT, "Owner behavior ID"},
            {"--kind", NULL, NMO_OPT_STRING, "in|out|local|shared"},
            {"--type", NULL, NMO_OPT_STRING, "Parameter type name or GUID"},
            {"--name", NULL, NMO_OPT_STRING, "Parameter name"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_OWNER, OPT_KIND, OPT_TYPE, OPT_NAME, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_param_add_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_OWNER].present || !vals[OPT_KIND].present ||
            !vals[OPT_TYPE].present || !vals[OPT_NAME].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.owner_id = vals[OPT_OWNER].val.u;
        args.kind = vals[OPT_KIND].val.str;
        args.type_name = vals[OPT_TYPE].val.str;
        args.name = vals[OPT_NAME].val.str;
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_param_add_mutate,
            script_param_add_report,
            &args);
    }

    if (strcmp(argv[1], "set") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--param", NULL, NMO_OPT_UINT, "Parameter ID"},
            {"--value", NULL, NMO_OPT_STRING, "Typed parameter value"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_PARAM, OPT_VALUE, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_param_set_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARAM].present || !vals[OPT_VALUE].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.param_id = vals[OPT_PARAM].val.u;
        args.value_str = vals[OPT_VALUE].val.str;
        {
            int rc = nmo_cli_run_write_command(
                r.pos_args[0],
                vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
                vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
                global,
                &spec,
                script_param_set_mutate,
                script_param_set_report,
                &args);
            script_param_set_args_cleanup(&args);
            return rc;
        }
    }

    if (strcmp(argv[1], "connect") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--from", NULL, NMO_OPT_UINT, "Source parameter ID"},
            {"--to", NULL, NMO_OPT_UINT, "Target ParameterIn ID"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_FROM, OPT_TO, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_param_connect_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_FROM].present || !vals[OPT_TO].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.source_id = vals[OPT_FROM].val.u;
        args.target_id = vals[OPT_TO].val.u;
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_param_connect_mutate,
            script_param_connect_report,
            &args);
    }

    if (strcmp(argv[1], "disconnect") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--to", NULL, NMO_OPT_UINT, "Target ParameterIn ID"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_TO, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_param_disconnect_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_TO].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.target_id = vals[OPT_TO].val.u;
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_param_disconnect_mutate,
            script_param_disconnect_report,
            &args);
    }

    if (strcmp(argv[1], "remove") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--param", NULL, NMO_OPT_UINT, "Parameter ID"},
            {"--detach", NULL, NMO_OPT_FLAG, "Detach data-flow references first"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_PARAM, OPT_DETACH, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_param_remove_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARAM].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.param_id = vals[OPT_PARAM].val.u;
        args.detach = vals[OPT_DETACH].present && vals[OPT_DETACH].val.flag;
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_param_remove_mutate,
            script_param_remove_report,
            &args);
    }

    return NMO_CLI_EXIT_ARG_ERROR;
}

int nmo_cmd_script_op(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_cli_write_spec_t spec = {
        .command_name = "script.op",
        .output_required_unless_dry_run = true,
    };
    if (argc < 2 || !argv || !argv[1]) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[1], "add") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--parent", NULL, NMO_OPT_UINT, "Owner behavior ID"},
            {"--op-guid", NULL, NMO_OPT_STRING, "Operation GUID"},
            {"--in1", NULL, NMO_OPT_UINT, "Input 1 parameter ID"},
            {"--in2", NULL, NMO_OPT_UINT, "Input 2 parameter ID"},
            {"--out", NULL, NMO_OPT_UINT, "Output parameter ID"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_PARENT, OPT_OP_GUID, OPT_IN1, OPT_IN2, OPT_OUT, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_op_add_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_PARENT].present || !vals[OPT_OP_GUID].present ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.parent_id = vals[OPT_PARENT].val.u;
        args.op_guid = nmo_guid_parse(vals[OPT_OP_GUID].val.str);
        if (nmo_guid_is_null(args.op_guid)) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.in1_id = vals[OPT_IN1].present ? vals[OPT_IN1].val.u : 0u;
        args.in2_id = vals[OPT_IN2].present ? vals[OPT_IN2].val.u : 0u;
        args.out_id = vals[OPT_OUT].present ? vals[OPT_OUT].val.u : 0u;
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_op_add_mutate,
            script_op_add_report,
            &args);
    }

    if (strcmp(argv[1], "rewire") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--op", NULL, NMO_OPT_UINT, "Operation ID"},
            {"--in1", NULL, NMO_OPT_UINT, "Input 1 parameter ID (0 clears)"},
            {"--in2", NULL, NMO_OPT_UINT, "Input 2 parameter ID (0 clears)"},
            {"--out", NULL, NMO_OPT_UINT, "Output parameter ID (0 clears)"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_OP, OPT_IN1, OPT_IN2, OPT_OUT, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_op_rewire_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_OP].present ||
            (!vals[OPT_IN1].present && !vals[OPT_IN2].present && !vals[OPT_OUT].present) ||
            r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.op_id = vals[OPT_OP].val.u;
        if (vals[OPT_IN1].present) {
            args.slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_IN1;
            args.in1_id = vals[OPT_IN1].val.u;
        }
        if (vals[OPT_IN2].present) {
            args.slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_IN2;
            args.in2_id = vals[OPT_IN2].val.u;
        }
        if (vals[OPT_OUT].present) {
            args.slot_flags |= NMO_SCRIPT_EDIT_OP_SLOT_OUT;
            args.out_id = vals[OPT_OUT].val.u;
        }
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_op_rewire_mutate,
            script_op_rewire_report,
            &args);
    }

    if (strcmp(argv[1], "remove") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--op", NULL, NMO_OPT_UINT, "Operation ID"},
            {"--output", "-o", NMO_OPT_STRING, "Output file"},
            {"--dry-run", NULL, NMO_OPT_FLAG, "Preview only"},
        };
        enum { OPT_OP, OPT_OUTPUT, OPT_DRY_RUN, OPT_COUNT };
        nmo_opt_val_t vals[OPT_COUNT];
        const char *pos[16];
        nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
        script_op_remove_args_t args = {0};
        if (nmo_opt_parse(argc - 1, argv + 1, opts, OPT_COUNT, &r) < 0 ||
            !vals[OPT_OP].present || r.pos_count != 1) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args.op_id = vals[OPT_OP].val.u;
        return nmo_cli_run_write_command(
            r.pos_args[0],
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            global,
            &spec,
            script_op_remove_mutate,
            script_op_remove_report,
            &args);
    }

    return NMO_CLI_EXIT_ARG_ERROR;
}
