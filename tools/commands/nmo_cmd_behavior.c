/**
 * @file nmo_cmd_behavior.c
 * @brief CLI behavior command group implementation
 */

#include "nmo_cmd_behavior.h"

#include "nmo_cmd_object.h"

#include "../nmo_cli_common.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "../nmo_tool_session.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "app/nmo_behavior_graph.h"
#include "app/nmo_script_walker.h"
#include "app/nmo_context.h"
#include "core/nmo_array.h"
#include "format/nmo_object.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "object/builtin/nmo_behaviorlink_schemas.h"
#include "object/builtin/nmo_behaviorio_schemas.h"
#include "object/builtin/nmo_parameterin_schemas.h"
#include "object/builtin/nmo_parameterout_schemas.h"
#include "object/builtin/nmo_parameteroperation_schemas.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_types.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int is_behavior_class(nmo_type_registry_t *registry, nmo_class_id_t class_id) {
    if (!registry) {
        return 0;
    }
    return nmo_type_registry_is_class_derived_from(
        registry, (uint32_t)class_id, (uint32_t)NMO_CID_BEHAVIOR) ? 1 : 0;
}

typedef nmo_behavior_graph_node_t nmo_cli_graph_node_t;
typedef nmo_behavior_graph_edge_t nmo_cli_graph_edge_t;

static bool parse_behavior_graph_args(int argc, char **argv,
                                      nmo_object_id_t *out_id,
                                      const char **out_file,
                                      bool *out_dot,
                                      size_t *out_max_nodes,
                                      size_t *out_max_edges)
{
    const char *id_str = NULL;
    const char *file_path = NULL;
    bool dot = false;
    size_t max_nodes = 0;
    size_t max_edges = 0;

    bool *consumed = (bool *)calloc((size_t)argc, sizeof(bool));
    if (!consumed) {
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--dot") == 0) {
            dot = true;
            consumed[i] = true;
            continue;
        }
        if (strcmp(argv[i], "--max-nodes") == 0) {
            if ((i + 1) >= argc) {
                free(consumed);
                return false;
            }
            uint32_t tmp = 0;
            if (!nmo_tool_parse_u32(argv[i + 1], &tmp)) {
                free(consumed);
                return false;
            }
            max_nodes = (size_t)tmp;
            consumed[i] = true;
            consumed[i + 1] = true;
            ++i;
            continue;
        }
        if (strcmp(argv[i], "--max-edges") == 0) {
            if ((i + 1) >= argc) {
                free(consumed);
                return false;
            }
            uint32_t tmp = 0;
            if (!nmo_tool_parse_u32(argv[i + 1], &tmp)) {
                free(consumed);
                return false;
            }
            max_edges = (size_t)tmp;
            consumed[i] = true;
            consumed[i + 1] = true;
            ++i;
            continue;
        }
    }

    int non_opt_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (consumed[i] || argv[i][0] == '-') {
            continue;
        }
        non_opt_count++;
        if (non_opt_count == 1) {
            id_str = argv[i];
        } else if (non_opt_count == 2) {
            file_path = argv[i];
            break;
        }
    }

    free(consumed);

    if (!id_str || !file_path) {
        return false;
    }

    uint32_t id = 0;
    if (!nmo_tool_parse_u32(id_str, &id) || id == 0) {
        return false;
    }

    if (out_id) {
        *out_id = (nmo_object_id_t)id;
    }
    if (out_file) {
        *out_file = file_path;
    }
    if (out_dot) {
        *out_dot = dot;
    }
    if (out_max_nodes) {
        *out_max_nodes = max_nodes;
    }
    if (out_max_edges) {
        *out_max_edges = max_edges;
    }
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

int nmo_cmd_behavior_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo behavior list <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    if (!registry) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Type registry unavailable\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to get objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    size_t behavior_count = 0;

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);

            if (!is_behavior_class(registry, class_id)) {
                continue;
            }

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "id", nmo_object_get_id(obj));
            yyjson_mut_obj_add_uint(doc, item, "class_id", class_id);

            const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
            if (class_name) {
                yyjson_mut_obj_add_str(doc, item, "class_name", class_name);
            }

            const char *name = nmo_object_get_name(obj);
            if (name && name[0]) {
                nmo_cli_json_add_str_safe(doc, item, "name", name);
            }

            yyjson_mut_arr_add_val(arr, item);
            behavior_count++;
        }

        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)behavior_count);
        yyjson_mut_obj_add_val(doc, data, "objects", arr);

        nmo_cli_json_write_enveloped_and_free(doc, data, "behavior.list", file_path, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        static const nmo_cli_table_col_t columns[] = {
            {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 30},
            {"Name", NMO_CLI_ALIGN_LEFT, 20, 50},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);
            if (!is_behavior_class(registry, class_id)) {
                continue;
            }

            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
            const char *name = nmo_object_get_name(obj);

            const char *cells[] = {
                id_buf,
                class_name ? class_name : "-",
                (name && name[0]) ? name : "-",
            };
            nmo_cli_table_add_row(&table, cells, 3);
            behavior_count++;
        }

        fprintf(out, "Behaviors: %zu\n\n", behavior_count);
        nmo_cli_table_print(&table, out, colorize);
        nmo_cli_table_free(&table);
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_behavior_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Reuse object show output contract. */
    return nmo_cmd_object_show(argc, argv, global);
}

typedef struct {
    nmo_class_id_t class_id;
    size_t count;
} nmo_cli_class_count_t;

static int class_count_cmp_desc(const void *a, const void *b) {
    const nmo_cli_class_count_t *aa = (const nmo_cli_class_count_t *)a;
    const nmo_cli_class_count_t *bb = (const nmo_cli_class_count_t *)b;
    if (aa->count != bb->count) {
        return (aa->count < bb->count) ? 1 : -1;
    }
    if (aa->class_id != bb->class_id) {
        return (aa->class_id < bb->class_id) ? -1 : 1;
    }
    return 0;
}

int nmo_cmd_behavior_stats(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo behavior stats <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    if (!registry) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Type registry unavailable\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to get objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_class_count_t *by_class = NULL;
    size_t by_class_count = 0;
    size_t by_class_cap = 0;
    size_t total = 0;

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        nmo_class_id_t class_id = nmo_object_get_class_id(obj);
        if (!is_behavior_class(registry, class_id)) {
            continue;
        }
        total++;

        bool found = false;
        for (size_t j = 0; j < by_class_count; ++j) {
            if (by_class[j].class_id == class_id) {
                by_class[j].count++;
                found = true;
                break;
            }
        }
        if (found) {
            continue;
        }

        if (by_class_count == by_class_cap) {
            size_t new_cap = (by_class_cap == 0) ? 8 : (by_class_cap * 2);
            nmo_cli_class_count_t *new_arr = (nmo_cli_class_count_t *)realloc(by_class, new_cap * sizeof(*by_class));
            if (!new_arr) {
                free(by_class);
                nmo_tool_close_session(ctx, session);
                fprintf(stderr, "Error: Out of memory\n");
                return NMO_CLI_EXIT_INTERNAL_ERROR;
            }
            by_class = new_arr;
            by_class_cap = new_cap;
        }
        by_class[by_class_count++] = (nmo_cli_class_count_t){.class_id = class_id, .count = 1};
    }

    qsort(by_class, by_class_count, sizeof(*by_class), class_count_cmp_desc);

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        free(by_class);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "total", (uint64_t)total);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < by_class_count; ++i) {
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "class_id", (uint64_t)by_class[i].class_id);
            yyjson_mut_obj_add_uint(doc, item, "count", (uint64_t)by_class[i].count);

            const char *class_name = nmo_cli_class_name_from_id(ctx, by_class[i].class_id);
            if (class_name) {
                yyjson_mut_obj_add_str(doc, item, "class_name", class_name);
            }

            yyjson_mut_arr_add_val(arr, item);
        }
        yyjson_mut_obj_add_val(doc, data, "by_class", arr);

        nmo_cli_json_write_enveloped_and_free(doc, data, "behavior.stats", file_path, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        nmo_cli_print_heading(out, "Behavior Statistics", colorize);
        fprintf(out, "\n");

        char buf[64];
        snprintf(buf, sizeof(buf), "%zu", total);
        nmo_cli_print_kv(out, "Total", buf, 16, colorize);
        fprintf(out, "\n");

        static const nmo_cli_table_col_t columns[] = {
            {"Class ID", NMO_CLI_ALIGN_RIGHT, 8, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 32},
            {"Count", NMO_CLI_ALIGN_RIGHT, 5, 0},
        };
        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        for (size_t i = 0; i < by_class_count; ++i) {
            char class_id_buf[16];
            char count_buf[16];
            snprintf(class_id_buf, sizeof(class_id_buf), "%u", (unsigned)by_class[i].class_id);
            snprintf(count_buf, sizeof(count_buf), "%zu", by_class[i].count);

            const char *class_name = nmo_cli_class_name_from_id(ctx, by_class[i].class_id);
            const char *cells[] = {
                class_id_buf,
                class_name ? class_name : "-",
                count_buf,
            };
            nmo_cli_table_add_row(&table, cells, 3);
        }

        nmo_cli_table_print(&table, out, colorize);
        nmo_cli_table_free(&table);
    }

    free(by_class);
    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_behavior_graph(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_object_id_t behavior_id = 0;
    const char *file_path = NULL;
    bool emit_dot = false;
    size_t max_nodes = 0;
    size_t max_edges = 0;
    int exit_code = NMO_CLI_EXIT_SUCCESS;

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    nmo_behavior_graph_t graph = {0};

    nmo_object_id_t *emit_node_ids = NULL;
    size_t *emit_edge_indices = NULL;

    FILE *out = NULL;
    if (!parse_behavior_graph_args(argc, argv, &behavior_id, &file_path,
                                   &emit_dot, &max_nodes, &max_edges)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: nmo behavior graph <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        exit_code = NMO_CLI_EXIT_IO_ERROR;
        goto cleanup;
    }

    if (!nmo_behavior_graph_build(ctx, session, behavior_id, &graph)) {
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

    char out_err[128];
    out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        fprintf(stderr, "Error: %s\n", out_err);
        exit_code = NMO_CLI_EXIT_IO_ERROR;
        goto cleanup;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

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

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "behavior_id", behavior_id);

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
            yyjson_mut_obj_add_str(doc, data, "behavior_class", behavior_class);
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

        yyjson_mut_val *graph = yyjson_mut_obj(doc);
        yyjson_mut_val *nodes_arr = yyjson_mut_arr(doc);
        yyjson_mut_val *edges_arr = yyjson_mut_arr(doc);

        for (size_t i = 0; i < emit_node_count; ++i) {
            yyjson_mut_val *node = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, node, "id", nodes[i].id);
            if (nodes[i].kind) {
                yyjson_mut_obj_add_str(doc, node, "kind", nodes[i].kind);
            }
            if (nodes[i].name && nodes[i].name[0]) {
                nmo_cli_json_add_str_safe(doc, node, "name", nodes[i].name);
            }
            if (nodes[i].class_id != 0) {
                yyjson_mut_obj_add_uint(doc, node, "class_id", (uint64_t)nodes[i].class_id);
            }
            if (nodes[i].class_name && nodes[i].class_name[0]) {
                yyjson_mut_obj_add_str(doc, node, "class_name", nodes[i].class_name);
            }
            yyjson_mut_arr_add_val(nodes_arr, node);
        }

        for (size_t i = 0; i < emit_edge_count; ++i) {
            size_t edge_index = emit_edge_indices ? emit_edge_indices[i] : i;
            const nmo_cli_graph_edge_t edge_ref = edges[edge_index];
            yyjson_mut_val *edge = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, edge, "from", edge_ref.from_id);
            yyjson_mut_obj_add_uint(doc, edge, "to", edge_ref.to_id);
            if (edge_ref.kind) {
                yyjson_mut_obj_add_str(doc, edge, "kind", edge_ref.kind);
            }
            if (edge_ref.field_path) {
                yyjson_mut_obj_add_str(doc, edge, "field_path", edge_ref.field_path);
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
            }
            if (edge_ref.is_shared) {
                yyjson_mut_obj_add_bool(doc, edge, "is_shared", true);
            }
            yyjson_mut_arr_add_val(edges_arr, edge);
        }

        yyjson_mut_obj_add_val(doc, graph, "nodes", nodes_arr);
        yyjson_mut_obj_add_val(doc, graph, "edges", edges_arr);
        if (nodes_truncated || edges_truncated) {
            yyjson_mut_val *truncated = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_bool(doc, truncated, "nodes", nodes_truncated);
            yyjson_mut_obj_add_bool(doc, truncated, "edges", edges_truncated);
            yyjson_mut_obj_add_val(doc, graph, "truncated", truncated);
        }
        yyjson_mut_obj_add_val(doc, data, "graph", graph);

        nmo_cli_json_write_enveloped_and_free(doc, data, "behavior.graph", file_path, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        nmo_cli_print_heading(out, "Behavior Graph", colorize);
        fprintf(out, "\n");
        const char *behavior_name = graph.behavior_name;
        const char *behavior_class = graph.behavior_class_name;
        fprintf(out, "Behavior %u: %s [%s]\n\n",
                behavior_id,
                (behavior_name && behavior_name[0]) ? behavior_name : "(unnamed)",
                behavior_class ? behavior_class : "?");

        fprintf(out, "Nodes: %zu (behavior %zu, parameter %zu, operation %zu, io %zu, unknown %zu)\n",
                node_count, node_behavior, node_parameter, node_operation, node_io, node_unknown);
        fprintf(out, "Edges: %zu (behavior links %zu, io links %zu, param %zu, op %zu)\n",
                edge_count,
                edge_behavior_link,
                edge_io_link,
                (edge_param_in + edge_param_out + edge_param_local + edge_param_dest + edge_param_source),
                (edge_op_in1 + edge_op_in2 + edge_op_out));
        if (nodes_truncated) {
            fprintf(out, "Note: Nodes truncated to %zu (use --max-nodes 0 to disable)\n", emit_node_count);
        }
        if (edges_truncated) {
            fprintf(out, "Note: Edges truncated to %zu (use --max-edges 0 to disable)\n", emit_edge_count);
        }
        if (broken_links > 0) {
            fprintf(out, "Broken links: %zu\n", broken_links);
        }
        if (missing_nodes > 0) {
            fprintf(out, "Missing objects: %zu\n", missing_nodes);
        }
        fprintf(out, "\n");

        static const nmo_cli_table_col_t node_columns[] = {
            {"ID", NMO_CLI_ALIGN_RIGHT, 6, 0},
            {"Kind", NMO_CLI_ALIGN_LEFT, 12, 16},
            {"Name", NMO_CLI_ALIGN_LEFT, 22, 50},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 40},
        };
        nmo_cli_table_t node_table;
        nmo_cli_table_init(&node_table, node_columns, sizeof(node_columns) / sizeof(node_columns[0]));

        for (size_t i = 0; i < emit_node_count; ++i) {
            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nodes[i].id);
            const char *cells[] = {
                id_buf,
                nodes[i].kind ? nodes[i].kind : "-",
                (nodes[i].name && nodes[i].name[0]) ? nodes[i].name : "-",
                (nodes[i].class_name && nodes[i].class_name[0]) ? nodes[i].class_name : "-",
            };
            nmo_cli_table_add_row(&node_table, cells, 4);
        }

        nmo_cli_table_print(&node_table, out, colorize);
        nmo_cli_table_free(&node_table);
        fprintf(out, "\n");

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

            if (edge_ref.kind && strcmp(edge_ref.kind, "behavior_link") == 0) {
                snprintf(meta_buf, sizeof(meta_buf), "io %u->%u %d/%d",
                         edge_ref.out_io_id,
                         edge_ref.in_io_id,
                         edge_ref.activation_delay,
                         edge_ref.initial_activation_delay);
            } else if (edge_ref.kind && strcmp(edge_ref.kind, "io_link") == 0) {
                snprintf(meta_buf, sizeof(meta_buf), "io %u->%u",
                         edge_ref.out_io_id,
                         edge_ref.in_io_id);
            } else if (edge_ref.kind && strcmp(edge_ref.kind, "param_source") == 0 && edge_ref.is_shared) {
                snprintf(meta_buf, sizeof(meta_buf), "shared");
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

        nmo_cli_table_print(&edge_table, out, colorize);
        nmo_cli_table_free(&edge_table);

        if (emit_dot) {
            fprintf(out, "\n");
            nmo_cli_print_heading(out, "DOT Graph", colorize);
            fprintf(out, "\n");
            fprintf(out, "digraph behavior_graph {\n");
            fprintf(out, "  node [shape=box, fontname=\"Courier\"];\n");
            for (size_t i = 0; i < emit_node_count; ++i) {
                const char *label = (nodes[i].name && nodes[i].name[0]) ? nodes[i].name :
                    (nodes[i].class_name && nodes[i].class_name[0]) ? nodes[i].class_name :
                    (nodes[i].kind ? nodes[i].kind : "node");
                fprintf(out, "  n%u [label=\"", nodes[i].id);
                dot_write_label(out, label);
                fprintf(out, "\"];\n");
            }
            for (size_t i = 0; i < emit_edge_count; ++i) {
                size_t edge_index = emit_edge_indices ? emit_edge_indices[i] : i;
                const nmo_cli_graph_edge_t edge_ref = edges[edge_index];
                const char *edge_label = edge_ref.kind ? edge_ref.kind : "link";
                fprintf(out, "  n%u -> n%u [label=\"", edge_ref.from_id, edge_ref.to_id);
                dot_write_label(out, edge_label);
                fprintf(out, "\"];\n");
            }
            fprintf(out, "}\n");
        }
    }

cleanup:
    free(emit_edge_indices);
    free(emit_node_ids);
    nmo_behavior_graph_free(&graph);
    if (ctx || session) {
        nmo_tool_close_session(ctx, session);
    }
    if (out) {
        nmo_cli_close_output_stream(global, out);
    }
    return exit_code;
}

/* ============================================================================
 * behavior dump — dump behavior tree with decoded parameter values
 * ============================================================================ */

int nmo_cmd_behavior_dump(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *id_str = NULL;
    const char *file_path = NULL;
    bool dump_all = false;

    int non_opt = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--all") == 0 || strcmp(argv[i], "-a") == 0) {
            dump_all = true;
            continue;
        }
        if (argv[i][0] != '-') {
            non_opt++;
            if (non_opt == 1) id_str = argv[i];
            if (non_opt == 2) file_path = argv[i];
        }
    }

    if (dump_all) {
        /* --all mode: id_str is actually the file path */
        if (!id_str) {
            fprintf(stderr, "Usage: nmo behavior dump --all <file>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        file_path = id_str;
        id_str = NULL;
    } else if (!id_str || !file_path) {
        fprintf(stderr, "Usage: nmo behavior dump [--all] [<id>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    if (dump_all) {
        /* Find and dump all scripts */
        nmo_array_t scripts;
        nmo_array_init(&scripts, sizeof(nmo_script_entry_t), 16, NULL);

        nmo_script_walker_find_scripts(ctx, session, &scripts);

        size_t count = scripts.count;
        fprintf(out, "Scripts found: %zu\n\n", count);

        for (size_t i = 0; i < count; ++i) {
            const nmo_script_entry_t *entry =
                (const nmo_script_entry_t *)nmo_array_get(&scripts, i);
            fprintf(out, "=== Script #%u", entry->script_id);
            if (entry->script_name && entry->script_name[0]) {
                fprintf(out, " \"%s\"", entry->script_name);
            }
            fprintf(out, " (owner: #%u", entry->owner_id);
            if (entry->owner_name && entry->owner_name[0]) {
                fprintf(out, " \"%s\"", entry->owner_name);
            }
            fprintf(out, ") ===\n");

            nmo_script_walker_dump_text(ctx, session, entry->script_id, out);
            fprintf(out, "\n");
        }

        nmo_array_dispose(&scripts);
    } else {
        uint32_t object_id;
        if (!nmo_tool_parse_u32(id_str, &object_id)) {
            fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str);
            nmo_tool_close_session(ctx, session);
            nmo_cli_close_output_stream(global, out);
            return NMO_CLI_EXIT_ARG_ERROR;
        }

        nmo_script_walker_dump_text(ctx, session, object_id, out);
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

