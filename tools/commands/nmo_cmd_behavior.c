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

static const char *find_file_arg_last(int argc, char **argv) {
    const char *last_non_opt = NULL;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            last_non_opt = argv[i];
        }
    }
    return last_non_opt;
}

static int is_behavior_class(nmo_type_registry_t *registry, nmo_class_id_t class_id) {
    if (!registry) {
        return 0;
    }
    return nmo_type_registry_is_class_derived_from(
        registry, (uint32_t)class_id, (uint32_t)NMO_CID_BEHAVIOR) ? 1 : 0;
}

typedef struct {
    nmo_object_id_t io_id;
    nmo_object_id_t behavior_id;
    const char *behavior_name;
    bool is_output;
} nmo_cli_io_owner_t;

typedef struct {
    nmo_object_id_t id;
    const char *kind;
    const char *name;
    bool owns_name;
    nmo_class_id_t class_id;
    const char *class_name;
} nmo_cli_graph_node_t;

typedef struct {
    nmo_object_id_t link_id;
    nmo_object_id_t from_id;
    nmo_object_id_t to_id;
    const char *kind;
    const char *field_path;
    nmo_object_id_t in_io_id;
    nmo_object_id_t out_io_id;
    int32_t activation_delay;
    int32_t initial_activation_delay;
    bool is_shared;
} nmo_cli_graph_edge_t;

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

static const nmo_type_descriptor_t *get_type_by_guid(const nmo_type_registry_t *registry, nmo_guid_t guid) {
    return registry ? nmo_type_registry_find_by_guid(registry, guid) : NULL;
}

static const nmo_type_descriptor_t *get_type_by_class_id(const nmo_type_registry_t *registry, nmo_class_id_t class_id) {
    return registry ? nmo_type_registry_find_by_class_id(registry, class_id) : NULL;
}

static bool object_is_derived_from_guid(
    nmo_type_registry_t *registry,
    nmo_object_t *obj,
    nmo_guid_t base_guid)
{
    const nmo_type_descriptor_t *base = get_type_by_guid(registry, base_guid);
    const nmo_type_descriptor_t *derived = get_type_by_class_id(registry, nmo_object_get_class_id(obj));
    if (!base || !derived) {
        return false;
    }
    return nmo_type_is_derived_from(registry, derived->id, base->id);
}

static void *get_ancestor_state_by_guid(
    const nmo_type_registry_t *registry,
    nmo_object_t *obj,
    nmo_guid_t base_guid)
{
    const nmo_type_descriptor_t *base = get_type_by_guid(registry, base_guid);
    const nmo_type_descriptor_t *derived = get_type_by_class_id(registry, nmo_object_get_class_id(obj));
    if (!base || !derived) {
        return NULL;
    }
    return nmo_object_get_ancestor_state(obj, base, derived);
}

static bool add_unique_id(nmo_object_id_t **ids, size_t *count, size_t *cap, nmo_object_id_t id) {
    if (id == 0 || !ids || !count || !cap) {
        return true;
    }
    for (size_t i = 0; i < *count; ++i) {
        if ((*ids)[i] == id) {
            return true;
        }
    }
    if (*count == *cap) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap * 2);
        nmo_object_id_t *new_ids = (nmo_object_id_t *)realloc(*ids, new_cap * sizeof(*new_ids));
        if (!new_ids) {
            return false;
        }
        *ids = new_ids;
        *cap = new_cap;
    }
    (*ids)[(*count)++] = id;
    return true;
}

static bool add_io_owner(
    nmo_cli_io_owner_t **owners,
    size_t *count,
    size_t *cap,
    nmo_object_id_t io_id,
    nmo_object_t *behavior,
    bool is_output)
{
    if (io_id == 0 || !owners || !count || !cap || !behavior) {
        return true;
    }
    for (size_t i = 0; i < *count; ++i) {
        if ((*owners)[i].io_id == io_id) {
            return true;
        }
    }
    if (*count == *cap) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap * 2);
        nmo_cli_io_owner_t *new_arr = (nmo_cli_io_owner_t *)realloc(*owners, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return false;
        }
        *owners = new_arr;
        *cap = new_cap;
    }
    (*owners)[*count] = (nmo_cli_io_owner_t){
        .io_id = io_id,
        .behavior_id = nmo_object_get_id(behavior),
        .behavior_name = nmo_object_get_name(behavior),
        .is_output = is_output,
    };
    (*count)++;
    return true;
}

static const nmo_cli_io_owner_t *find_io_owner(
    const nmo_cli_io_owner_t *owners,
    size_t count,
    nmo_object_id_t io_id)
{
    if (!owners || io_id == 0) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        if (owners[i].io_id == io_id) {
            return &owners[i];
        }
    }
    return NULL;
}

static bool collect_behavior_ids(
    const nmo_behavior_state_t *state,
    nmo_object_id_t root_id,
    nmo_object_id_t **out_ids,
    size_t *out_count)
{
    if (!state || !out_ids || !out_count) {
        return false;
    }

    size_t cap = 0;
    size_t count = 0;
    nmo_object_id_t *ids = NULL;

    if (!add_unique_id(&ids, &count, &cap, root_id)) {
        free(ids);
        return false;
    }

    const nmo_object_id_t *sub_ids = NMO_ARRAY_DATA(nmo_object_id_t, &state->sub_behaviors);
    for (size_t i = 0; i < state->sub_behaviors.count; ++i) {
        if (!add_unique_id(&ids, &count, &cap, sub_ids[i])) {
            free(ids);
            return false;
        }
    }

    *out_ids = ids;
    *out_count = count;
    return true;
}

static bool collect_behavior_io_owners(
    nmo_type_registry_t *registry,
    nmo_object_repository_t *repo,
    const nmo_object_id_t *behavior_ids,
    size_t behavior_count,
    nmo_cli_io_owner_t **out_owners,
    size_t *out_owner_count)
{
    if (!registry || !repo || !out_owners || !out_owner_count) {
        return false;
    }

    nmo_cli_io_owner_t *owners = NULL;
    size_t count = 0;
    size_t cap = 0;

    for (size_t i = 0; i < behavior_count; ++i) {
        nmo_object_t *behavior = nmo_object_repository_find_by_id(repo, behavior_ids[i]);
        if (!behavior) {
            continue;
        }
        if (!object_is_derived_from_guid(registry, behavior, CKPGUID_BEHAVIOR)) {
            continue;
        }

        const nmo_behavior_state_t *state =
            (const nmo_behavior_state_t *)get_ancestor_state_by_guid(registry, behavior, CKPGUID_BEHAVIOR);
        if (!state) {
            continue;
        }

        const nmo_object_id_t *inputs = NMO_ARRAY_DATA(nmo_object_id_t, &state->inputs);
        const nmo_object_id_t *outputs = NMO_ARRAY_DATA(nmo_object_id_t, &state->outputs);

        for (size_t j = 0; j < state->inputs.count; ++j) {
            if (!add_io_owner(&owners, &count, &cap, inputs[j], behavior, false)) {
                free(owners);
                return false;
            }
        }

        for (size_t j = 0; j < state->outputs.count; ++j) {
            if (!add_io_owner(&owners, &count, &cap, outputs[j], behavior, true)) {
                free(owners);
                return false;
            }
        }
    }

    *out_owners = owners;
    *out_owner_count = count;
    return true;
}

static bool add_graph_node(
    nmo_cli_graph_node_t **nodes,
    size_t *count,
    size_t *cap,
    nmo_object_id_t id,
    const char *kind,
    const char *name,
    bool owns_name,
    nmo_class_id_t class_id,
    const char *class_name)
{
    if (!nodes || !count || !cap || id == 0) {
        return true;
    }
    for (size_t i = 0; i < *count; ++i) {
        if ((*nodes)[i].id == id) {
            if (kind && (*nodes)[i].kind && strcmp((*nodes)[i].kind, "unknown") == 0) {
                (*nodes)[i].kind = kind;
            }
            if (class_id != 0 && (*nodes)[i].class_id == 0) {
                (*nodes)[i].class_id = class_id;
                (*nodes)[i].class_name = class_name;
            }
            if (name && name[0] && (!(*nodes)[i].name || !(*nodes)[i].name[0])) {
                (*nodes)[i].name = name;
                (*nodes)[i].owns_name = owns_name;
            } else if (owns_name && name && name[0] && (*nodes)[i].owns_name) {
                free((void *)((*nodes)[i].name));
                (*nodes)[i].name = name;
                (*nodes)[i].owns_name = owns_name;
            }
            return true;
        }
    }
    if (*count == *cap) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap * 2);
        nmo_cli_graph_node_t *new_nodes = (nmo_cli_graph_node_t *)realloc(*nodes, new_cap * sizeof(*new_nodes));
        if (!new_nodes) {
            return false;
        }
        *nodes = new_nodes;
        *cap = new_cap;
    }
    (*nodes)[*count] = (nmo_cli_graph_node_t){
        .id = id,
        .kind = kind,
        .name = name,
        .owns_name = owns_name,
        .class_id = class_id,
        .class_name = class_name,
    };
    (*count)++;
    return true;
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

static bool add_graph_edge(
    nmo_cli_graph_edge_t **edges,
    size_t *count,
    size_t *cap,
    nmo_object_id_t link_id,
    nmo_object_id_t from_id,
    nmo_object_id_t to_id,
    const char *kind,
    const char *field_path,
    nmo_object_id_t in_io_id,
    nmo_object_id_t out_io_id,
    int32_t activation_delay,
    int32_t initial_activation_delay,
    bool is_shared)
{
    if (!edges || !count || !cap || from_id == 0 || to_id == 0) {
        return true;
    }
    if (*count == *cap) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap * 2);
        nmo_cli_graph_edge_t *new_edges = (nmo_cli_graph_edge_t *)realloc(*edges, new_cap * sizeof(*new_edges));
        if (!new_edges) {
            return false;
        }
        *edges = new_edges;
        *cap = new_cap;
    }
    (*edges)[*count] = (nmo_cli_graph_edge_t){
        .link_id = link_id,
        .from_id = from_id,
        .to_id = to_id,
        .kind = kind,
        .field_path = field_path,
        .in_io_id = in_io_id,
        .out_io_id = out_io_id,
        .activation_delay = activation_delay,
        .initial_activation_delay = initial_activation_delay,
        .is_shared = is_shared,
    };
    (*count)++;
    return true;
}

static void free_graph_nodes(nmo_cli_graph_node_t *nodes, size_t count) {
    if (!nodes) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        if (nodes[i].owns_name) {
            free((void *)nodes[i].name);
        }
    }
    free(nodes);
}

static bool add_graph_node_from_object(
    nmo_cli_graph_node_t **nodes,
    size_t *node_count,
    size_t *node_cap,
    nmo_object_repository_t *repo,
    nmo_context_t *ctx,
    nmo_object_id_t id,
    const char *kind,
    const char *missing_prefix,
    size_t *missing_count)
{
    if (!repo || !ctx || id == 0) {
        return true;
    }

    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, id);
    if (!obj) {
        if (missing_count) {
            (*missing_count)++;
        }
        char label_buf[64];
        snprintf(label_buf, sizeof(label_buf), "%s %u", missing_prefix, id);
        char *label_copy = nmo_tool_strdup(label_buf);
        if (!label_copy) {
            return false;
        }
        if (!add_graph_node(nodes, node_count, node_cap, id, kind, label_copy, true, 0, NULL)) {
            free(label_copy);
            return false;
        }
        return true;
    }

    const char *name = nmo_object_get_name(obj);
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
    const char *label = (name && name[0]) ? name : class_name;
    return add_graph_node(nodes, node_count, node_cap, id, kind, label, false, class_id, class_name);
}

static bool add_parameter_edge(
    nmo_object_id_t **param_ids,
    size_t *param_count,
    size_t *param_cap,
    nmo_cli_graph_node_t **nodes,
    size_t *node_count,
    size_t *node_cap,
    nmo_cli_graph_edge_t **edges,
    size_t *edge_count,
    size_t *edge_cap,
    nmo_object_repository_t *repo,
    nmo_context_t *ctx,
    nmo_object_id_t param_id,
    nmo_object_id_t from_id,
    nmo_object_id_t to_id,
    const char *edge_kind,
    const char *field_path,
    size_t *missing_nodes,
    bool is_shared)
{
    if (!add_unique_id(param_ids, param_count, param_cap, param_id)) {
        return false;
    }
    if (!add_graph_node_from_object(nodes, node_count, node_cap,
                                    repo, ctx, param_id, "parameter",
                                    "Param", missing_nodes)) {
        return false;
    }
    if (!add_graph_edge(edges, edge_count, edge_cap,
                        0,
                        from_id,
                        to_id,
                        edge_kind,
                        field_path,
                        0,
                        0,
                        0,
                        0,
                        is_shared)) {
        return false;
    }
    return true;
}

int nmo_cmd_behavior_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = find_file_arg_last(argc, argv);
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

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "behavior.list", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
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
    const char *file_path = find_file_arg_last(argc, argv);
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

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "behavior.stats", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
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
    nmo_type_registry_t *registry = NULL;
    nmo_object_repository_t *repo = NULL;
    nmo_object_t *behavior = NULL;

    nmo_object_id_t *behavior_ids = NULL;
    size_t behavior_count = 0;

    nmo_cli_io_owner_t *io_owners = NULL;
    size_t io_owner_count = 0;

    nmo_cli_graph_node_t *nodes = NULL;
    size_t node_count = 0;
    size_t node_cap = 0;
    size_t missing_nodes = 0;

    nmo_cli_graph_edge_t *edges = NULL;
    size_t edge_count = 0;
    size_t edge_cap = 0;

    nmo_object_id_t *parameter_ids = NULL;
    size_t parameter_count = 0;
    size_t parameter_cap = 0;

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

    registry = nmo_context_get_type_registry(ctx);
    if (!registry) {
        fprintf(stderr, "Error: Type registry unavailable\n");
        exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
        goto cleanup;
    }

    repo = nmo_session_get_repository(session);
    behavior = nmo_object_repository_find_by_id(repo, behavior_id);
    if (!behavior) {
        fprintf(stderr, "Error: Behavior %u not found\n", behavior_id);
        exit_code = NMO_CLI_EXIT_ARG_ERROR;
        goto cleanup;
    }

    if (!is_behavior_class(registry, nmo_object_get_class_id(behavior))) {
        fprintf(stderr, "Error: Object %u is not a behavior\n", behavior_id);
        exit_code = NMO_CLI_EXIT_ARG_ERROR;
        goto cleanup;
    }

    const nmo_behavior_state_t *behavior_state =
        (const nmo_behavior_state_t *)get_ancestor_state_by_guid(registry, behavior, CKPGUID_BEHAVIOR);
    if (!behavior_state) {
        fprintf(stderr, "Error: Behavior state unavailable\n");
        exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
        goto cleanup;
    }

    if (!collect_behavior_ids(behavior_state, behavior_id, &behavior_ids, &behavior_count)) {
        fprintf(stderr, "Error: Failed to collect behavior IDs\n");
        exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
        goto cleanup;
    }

    if (!collect_behavior_io_owners(registry, repo, behavior_ids, behavior_count, &io_owners, &io_owner_count)) {
        fprintf(stderr, "Error: Failed to collect behavior I/O owners\n");
        exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
        goto cleanup;
    }

    for (size_t i = 0; i < behavior_count; ++i) {
        if (!add_graph_node_from_object(&nodes, &node_count, &node_cap,
                                        repo, ctx, behavior_ids[i], "behavior",
                                        "Behavior", &missing_nodes)) {
            fprintf(stderr, "Error: Out of memory\n");
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
            goto cleanup;
        }
    }

    size_t broken_links = 0;

    const nmo_object_id_t *link_ids = NMO_ARRAY_DATA(nmo_object_id_t, &behavior_state->sub_behavior_links);
    size_t link_count = behavior_state->sub_behavior_links.count;

    for (size_t i = 0; i < link_count; ++i) {
        nmo_object_id_t link_id = link_ids[i];
        if (link_id == 0) {
            continue;
        }

        nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id);
        const nmo_behaviorlink_state_t *link_state = NULL;
        if (link_obj && object_is_derived_from_guid(registry, link_obj, CKPGUID_BEHAVIORLINK)) {
            link_state = (const nmo_behaviorlink_state_t *)get_ancestor_state_by_guid(
                registry, link_obj, CKPGUID_BEHAVIORLINK);
        }

        if (!link_state) {
            broken_links++;
            continue;
        }

        const nmo_cli_io_owner_t *in_owner =
            find_io_owner(io_owners, io_owner_count, link_state->in_io_id);
        const nmo_cli_io_owner_t *out_owner =
            find_io_owner(io_owners, io_owner_count, link_state->out_io_id);

        if (in_owner && out_owner) {
            if (!add_graph_edge(&edges, &edge_count, &edge_cap,
                                link_id,
                                out_owner->behavior_id,
                                in_owner->behavior_id,
                                "behavior_link",
                                "sub_behavior_links",
                                link_state->in_io_id,
                                link_state->out_io_id,
                                link_state->activation_delay,
                                link_state->initial_activation_delay,
                                false)) {
                fprintf(stderr, "Error: Out of memory\n");
                exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
                goto cleanup;
            }
        } else if (link_state->in_io_id != 0 && link_state->out_io_id != 0) {
            if (!add_graph_node_from_object(&nodes, &node_count, &node_cap,
                                            repo, ctx, link_state->in_io_id, "io",
                                            "IO", &missing_nodes)) {
                fprintf(stderr, "Error: Out of memory\n");
                exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
                goto cleanup;
            }
            if (!add_graph_node_from_object(&nodes, &node_count, &node_cap,
                                            repo, ctx, link_state->out_io_id, "io",
                                            "IO", &missing_nodes)) {
                fprintf(stderr, "Error: Out of memory\n");
                exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
                goto cleanup;
            }
            if (!add_graph_edge(&edges, &edge_count, &edge_cap,
                                link_id,
                                link_state->out_io_id,
                                link_state->in_io_id,
                                "io_link",
                                "sub_behavior_links",
                                link_state->in_io_id,
                                link_state->out_io_id,
                                0,
                                0,
                                false)) {
                fprintf(stderr, "Error: Out of memory\n");
                exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
                goto cleanup;
            }
        }
    }

    const nmo_object_id_t *in_params = NMO_ARRAY_DATA(nmo_object_id_t, &behavior_state->in_parameters);
    for (size_t i = 0; i < behavior_state->in_parameters.count; ++i) {
        if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                &nodes, &node_count, &node_cap,
                                &edges, &edge_count, &edge_cap,
                                repo, ctx,
                                in_params[i],
                                behavior_id,
                                in_params[i],
                                "param_in",
                                "in_parameters",
                                &missing_nodes,
                                false)) {
            fprintf(stderr, "Error: Out of memory\n");
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
            goto cleanup;
        }
    }

    const nmo_object_id_t *out_params = NMO_ARRAY_DATA(nmo_object_id_t, &behavior_state->out_parameters);
    for (size_t i = 0; i < behavior_state->out_parameters.count; ++i) {
        if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                &nodes, &node_count, &node_cap,
                                &edges, &edge_count, &edge_cap,
                                repo, ctx,
                                out_params[i],
                                behavior_id,
                                out_params[i],
                                "param_out",
                                "out_parameters",
                                &missing_nodes,
                                false)) {
            fprintf(stderr, "Error: Out of memory\n");
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
            goto cleanup;
        }
    }

    const nmo_object_id_t *local_params = NMO_ARRAY_DATA(nmo_object_id_t, &behavior_state->local_parameters);
    for (size_t i = 0; i < behavior_state->local_parameters.count; ++i) {
        if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                &nodes, &node_count, &node_cap,
                                &edges, &edge_count, &edge_cap,
                                repo, ctx,
                                local_params[i],
                                behavior_id,
                                local_params[i],
                                "param_local",
                                "local_parameters",
                                &missing_nodes,
                                false)) {
            fprintf(stderr, "Error: Out of memory\n");
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
            goto cleanup;
        }
    }

    const nmo_object_id_t *ops = NMO_ARRAY_DATA(nmo_object_id_t, &behavior_state->operations);
    for (size_t i = 0; i < behavior_state->operations.count; ++i) {
        nmo_object_id_t op_id = ops[i];
        if (!add_graph_node_from_object(&nodes, &node_count, &node_cap,
                                        repo, ctx, op_id, "operation",
                                        "Operation", &missing_nodes)) {
            fprintf(stderr, "Error: Out of memory\n");
            exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
            goto cleanup;
        }

        nmo_object_t *op_obj = nmo_object_repository_find_by_id(repo, op_id);
        if (!op_obj) {
            continue;
        }
        if (!object_is_derived_from_guid(registry, op_obj, CKPGUID_PARAMETEROPERATION)) {
            continue;
        }

        const nmo_parameteroperation_state_t *op_state =
            (const nmo_parameteroperation_state_t *)get_ancestor_state_by_guid(
                registry, op_obj, CKPGUID_PARAMETEROPERATION);
        if (!op_state) {
            continue;
        }

        if (op_state->has_in1 && op_state->in1_id != 0) {
            if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                    &nodes, &node_count, &node_cap,
                                    &edges, &edge_count, &edge_cap,
                                    repo, ctx,
                                    op_state->in1_id,
                                    op_state->in1_id,
                                    op_id,
                                    "op_in1",
                                    "in1_id",
                                    &missing_nodes,
                                    false)) {
                fprintf(stderr, "Error: Out of memory\n");
                exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
                goto cleanup;
            }
        }

        if (op_state->has_in2 && op_state->in2_id != 0) {
            if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                    &nodes, &node_count, &node_cap,
                                    &edges, &edge_count, &edge_cap,
                                    repo, ctx,
                                    op_state->in2_id,
                                    op_state->in2_id,
                                    op_id,
                                    "op_in2",
                                    "in2_id",
                                    &missing_nodes,
                                    false)) {
                fprintf(stderr, "Error: Out of memory\n");
                exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
                goto cleanup;
            }
        }

        if (op_state->has_out && op_state->out_id != 0) {
            if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                    &nodes, &node_count, &node_cap,
                                    &edges, &edge_count, &edge_cap,
                                    repo, ctx,
                                    op_state->out_id,
                                    op_id,
                                    op_state->out_id,
                                    "op_out",
                                    "out_id",
                                    &missing_nodes,
                                    false)) {
                fprintf(stderr, "Error: Out of memory\n");
                exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
                goto cleanup;
            }
        }
    }

    for (size_t i = 0; i < parameter_count; ++i) {
        nmo_object_id_t param_id = parameter_ids[i];
        nmo_object_t *param_obj = nmo_object_repository_find_by_id(repo, param_id);
        if (!param_obj) {
            continue;
        }

        if (object_is_derived_from_guid(registry, param_obj, CKPGUID_PARAMETEROUT)) {
            const nmo_parameterout_state_t *out_state =
                (const nmo_parameterout_state_t *)get_ancestor_state_by_guid(
                    registry, param_obj, CKPGUID_PARAMETEROUT);
            if (out_state && out_state->destination_ids && out_state->destination_count > 0) {
                for (uint32_t d = 0; d < out_state->destination_count; ++d) {
                    nmo_object_id_t dest_id = out_state->destination_ids[d];
                    if (dest_id == 0) {
                        continue;
                    }
                    if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                            &nodes, &node_count, &node_cap,
                                            &edges, &edge_count, &edge_cap,
                                            repo, ctx,
                                            dest_id,
                                            param_id,
                                            dest_id,
                                            "param_dest",
                                            "destination_ids",
                                            &missing_nodes,
                                            false)) {
                        fprintf(stderr, "Error: Out of memory\n");
                        exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
                        goto cleanup;
                    }
                }
            }
        }

        if (object_is_derived_from_guid(registry, param_obj, CKPGUID_PARAMETERIN)) {
            const nmo_parameterin_state_t *in_state =
                (const nmo_parameterin_state_t *)get_ancestor_state_by_guid(
                    registry, param_obj, CKPGUID_PARAMETERIN);
            if (in_state && in_state->source_id != 0) {
                if (!add_parameter_edge(&parameter_ids, &parameter_count, &parameter_cap,
                                        &nodes, &node_count, &node_cap,
                                        &edges, &edge_count, &edge_cap,
                                        repo, ctx,
                                        in_state->source_id,
                                        in_state->source_id,
                                        param_id,
                                        "param_source",
                                        "source_id",
                                        &missing_nodes,
                                        in_state->is_shared != 0)) {
                    fprintf(stderr, "Error: Out of memory\n");
                    exit_code = NMO_CLI_EXIT_INTERNAL_ERROR;
                    goto cleanup;
                }
            }
        }
    }

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

        const char *behavior_name = nmo_object_get_name(behavior);
        if (behavior_name && behavior_name[0]) {
            nmo_cli_json_add_str_safe(doc, data, "behavior_name", behavior_name);
        }
        nmo_class_id_t behavior_class_id = nmo_object_get_class_id(behavior);
        const char *behavior_class = nmo_cli_class_name_from_id(ctx, behavior_class_id);
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

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "behavior.graph", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        nmo_cli_print_heading(out, "Behavior Graph", colorize);
        fprintf(out, "\n");
        const char *behavior_name = nmo_object_get_name(behavior);
        const char *behavior_class = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(behavior));
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
    free(io_owners);
    free(behavior_ids);
    free_graph_nodes(nodes, node_count);
    free(edges);
    free(parameter_ids);
    if (ctx || session) {
        nmo_tool_close_session(ctx, session);
    }
    if (out) {
        nmo_cli_close_output_stream(global, out);
    }
    return exit_code;
}
