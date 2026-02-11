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
#include "object/nmo_class_ids.h"
#include "object/nmo_object_guids.h"
#include "object/nmo_object_types.h"
#include "session/nmo_object_repository.h"
#include "type/nmo_type_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find_file_arg_last(int argc, char **argv) {
    const char *last_non_opt = NULL;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            last_non_opt = argv[i];
        }
    }
    return last_non_opt;
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
} nmo_cli_graph_node_t;

typedef struct {
    nmo_object_id_t link_id;
    nmo_object_id_t from_id;
    nmo_object_id_t to_id;
    const char *kind;
    const char *field_path;
} nmo_cli_graph_edge_t;

static bool parse_behavior_id_file(int argc, char **argv, nmo_object_id_t *out_id, const char **out_file) {
    const char *id_str = NULL;
    const char *file_path = NULL;
    int non_opt_count = 0;

    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
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
    return true;
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
    bool owns_name)
{
    if (!nodes || !count || !cap || id == 0) {
        return true;
    }
    for (size_t i = 0; i < *count; ++i) {
        if ((*nodes)[i].id == id) {
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
    nmo_object_id_t to_id)
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
        .kind = "link",
        .field_path = "sub_behavior_links",
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

            if (!nmo_object_class_is_behavior(registry, class_id)) {
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
            if (!nmo_object_class_is_behavior(registry, class_id)) {
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
        if (!nmo_object_class_is_behavior(registry, class_id)) {
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

int nmo_cmd_behavior_links(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_object_id_t behavior_id = 0;
    const char *file_path = NULL;
    if (!parse_behavior_id_file(argc, argv, &behavior_id, &file_path)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: nmo behavior links <id> <file>\n");
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

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *behavior = nmo_object_repository_find_by_id(repo, behavior_id);
    if (!behavior) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Behavior %u not found\n", behavior_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!nmo_object_class_is_behavior(registry, nmo_object_get_class_id(behavior))) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Object %u is not a behavior\n", behavior_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const nmo_behavior_state_t *behavior_state =
        (const nmo_behavior_state_t *)get_ancestor_state_by_guid(registry, behavior, CKPGUID_BEHAVIOR);
    if (!behavior_state) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Behavior state unavailable\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_id_t *behavior_ids = NULL;
    size_t behavior_count = 0;
    if (!collect_behavior_ids(behavior_state, behavior_id, &behavior_ids, &behavior_count)) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to collect behavior IDs\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_io_owner_t *io_owners = NULL;
    size_t io_owner_count = 0;
    if (!collect_behavior_io_owners(registry, repo, behavior_ids, behavior_count, &io_owners, &io_owner_count)) {
        free(behavior_ids);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to collect behavior I/O owners\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    const nmo_object_id_t *link_ids = NMO_ARRAY_DATA(nmo_object_id_t, &behavior_state->sub_behavior_links);
    size_t link_count = behavior_state->sub_behavior_links.count;

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        free(io_owners);
        free(behavior_ids);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "behavior_id", behavior_id);
        yyjson_mut_obj_add_uint(doc, data, "link_count", (uint64_t)link_count);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < link_count; ++i) {
            nmo_object_id_t link_id = link_ids[i];
            if (link_id == 0) {
                continue;
            }

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "link_id", link_id);

            nmo_object_t *link_obj = nmo_object_repository_find_by_id(repo, link_id);
            const nmo_behaviorlink_state_t *link_state = NULL;
            if (link_obj && object_is_derived_from_guid(registry, link_obj, CKPGUID_BEHAVIORLINK)) {
                link_state = (const nmo_behaviorlink_state_t *)get_ancestor_state_by_guid(
                    registry, link_obj, CKPGUID_BEHAVIORLINK);
            }

            if (!link_state) {
                yyjson_mut_obj_add_bool(doc, item, "broken", true);
                yyjson_mut_arr_add_val(arr, item);
                continue;
            }

            yyjson_mut_obj_add_int(doc, item, "activation_delay", link_state->activation_delay);
            yyjson_mut_obj_add_int(doc, item, "initial_activation_delay", link_state->initial_activation_delay);
            yyjson_mut_obj_add_uint(doc, item, "in_io_id", link_state->in_io_id);
            yyjson_mut_obj_add_uint(doc, item, "out_io_id", link_state->out_io_id);

            const nmo_cli_io_owner_t *in_owner = find_io_owner(io_owners, io_owner_count, link_state->in_io_id);
            const nmo_cli_io_owner_t *out_owner = find_io_owner(io_owners, io_owner_count, link_state->out_io_id);

            if (in_owner) {
                yyjson_mut_obj_add_uint(doc, item, "in_behavior_id", in_owner->behavior_id);
                if (in_owner->behavior_name && in_owner->behavior_name[0]) {
                    nmo_cli_json_add_str_safe(doc, item, "in_behavior_name", in_owner->behavior_name);
                }
            }
            if (out_owner) {
                yyjson_mut_obj_add_uint(doc, item, "out_behavior_id", out_owner->behavior_id);
                if (out_owner->behavior_name && out_owner->behavior_name[0]) {
                    nmo_cli_json_add_str_safe(doc, item, "out_behavior_name", out_owner->behavior_name);
                }
            }

            nmo_object_t *in_io = nmo_object_repository_find_by_id(repo, link_state->in_io_id);
            if (in_io) {
                const char *name = nmo_object_get_name(in_io);
                if (name && name[0]) {
                    nmo_cli_json_add_str_safe(doc, item, "in_io_name", name);
                }
            }

            nmo_object_t *out_io = nmo_object_repository_find_by_id(repo, link_state->out_io_id);
            if (out_io) {
                const char *name = nmo_object_get_name(out_io);
                if (name && name[0]) {
                    nmo_cli_json_add_str_safe(doc, item, "out_io_name", name);
                }
            }

            yyjson_mut_arr_add_val(arr, item);
        }

        yyjson_mut_obj_add_val(doc, data, "links", arr);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "behavior.links", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        nmo_cli_print_heading(out, "Behavior Links", colorize);
        fprintf(out, "\n");

        const char *behavior_name = nmo_object_get_name(behavior);
        const char *behavior_class = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(behavior));
        fprintf(out, "Behavior %u: %s [%s]\n\n",
                behavior_id,
                (behavior_name && behavior_name[0]) ? behavior_name : "(unnamed)",
                behavior_class ? behavior_class : "?");

        if (link_count == 0) {
            fprintf(out, "(no sub-behavior links)\n");
        } else {
            static const nmo_cli_table_col_t columns[] = {
                {"Link", NMO_CLI_ALIGN_RIGHT, 6, 0},
                {"In IO", NMO_CLI_ALIGN_RIGHT, 6, 0},
                {"Out IO", NMO_CLI_ALIGN_RIGHT, 7, 0},
                {"In Behavior", NMO_CLI_ALIGN_LEFT, 18, 32},
                {"Out Behavior", NMO_CLI_ALIGN_LEFT, 18, 32},
                {"Delay", NMO_CLI_ALIGN_RIGHT, 7, 0},
            };

            nmo_cli_table_t table;
            nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

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

                char link_buf[16];
                char in_io_buf[16];
                char out_io_buf[16];
                char in_beh_buf[64];
                char out_beh_buf[64];
                char delay_buf[32];

                snprintf(link_buf, sizeof(link_buf), "%u", link_id);

                if (!link_state) {
                    const char *cells[] = {link_buf, "-", "-", "(broken)", "-", "-"};
                    nmo_cli_table_add_row(&table, cells, 6);
                    continue;
                }

                snprintf(in_io_buf, sizeof(in_io_buf), "%u", link_state->in_io_id);
                snprintf(out_io_buf, sizeof(out_io_buf), "%u", link_state->out_io_id);
                snprintf(delay_buf, sizeof(delay_buf), "%d/%d",
                         link_state->activation_delay,
                         link_state->initial_activation_delay);

                const nmo_cli_io_owner_t *in_owner =
                    find_io_owner(io_owners, io_owner_count, link_state->in_io_id);
                const nmo_cli_io_owner_t *out_owner =
                    find_io_owner(io_owners, io_owner_count, link_state->out_io_id);

                if (in_owner) {
                    if (in_owner->behavior_name && in_owner->behavior_name[0]) {
                        snprintf(in_beh_buf, sizeof(in_beh_buf), "%u:%s",
                                 in_owner->behavior_id, in_owner->behavior_name);
                    } else {
                        snprintf(in_beh_buf, sizeof(in_beh_buf), "%u", in_owner->behavior_id);
                    }
                } else {
                    snprintf(in_beh_buf, sizeof(in_beh_buf), "-");
                }

                if (out_owner) {
                    if (out_owner->behavior_name && out_owner->behavior_name[0]) {
                        snprintf(out_beh_buf, sizeof(out_beh_buf), "%u:%s",
                                 out_owner->behavior_id, out_owner->behavior_name);
                    } else {
                        snprintf(out_beh_buf, sizeof(out_beh_buf), "%u", out_owner->behavior_id);
                    }
                } else {
                    snprintf(out_beh_buf, sizeof(out_beh_buf), "-");
                }

                const char *cells[] = {
                    link_buf,
                    in_io_buf,
                    out_io_buf,
                    in_beh_buf,
                    out_beh_buf,
                    delay_buf,
                };
                nmo_cli_table_add_row(&table, cells, 6);
            }

            nmo_cli_table_print(&table, out, colorize);
            nmo_cli_table_free(&table);
        }
    }

    free(io_owners);
    free(behavior_ids);
    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_behavior_graph(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_object_id_t behavior_id = 0;
    const char *file_path = NULL;
    if (!parse_behavior_id_file(argc, argv, &behavior_id, &file_path)) {
        fprintf(stderr, "Error: Missing or invalid arguments\n");
        fprintf(stderr, "Usage: nmo behavior graph <id> <file>\n");
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

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *behavior = nmo_object_repository_find_by_id(repo, behavior_id);
    if (!behavior) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Behavior %u not found\n", behavior_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!nmo_object_class_is_behavior(registry, nmo_object_get_class_id(behavior))) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Object %u is not a behavior\n", behavior_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const nmo_behavior_state_t *behavior_state =
        (const nmo_behavior_state_t *)get_ancestor_state_by_guid(registry, behavior, CKPGUID_BEHAVIOR);
    if (!behavior_state) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Behavior state unavailable\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_object_id_t *behavior_ids = NULL;
    size_t behavior_count = 0;
    if (!collect_behavior_ids(behavior_state, behavior_id, &behavior_ids, &behavior_count)) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to collect behavior IDs\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_io_owner_t *io_owners = NULL;
    size_t io_owner_count = 0;
    if (!collect_behavior_io_owners(registry, repo, behavior_ids, behavior_count, &io_owners, &io_owner_count)) {
        free(behavior_ids);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to collect behavior I/O owners\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_graph_node_t *nodes = NULL;
    size_t node_count = 0;
    size_t node_cap = 0;

    for (size_t i = 0; i < behavior_count; ++i) {
        nmo_object_t *obj = nmo_object_repository_find_by_id(repo, behavior_ids[i]);
        if (!obj) {
            continue;
        }
        const char *name = nmo_object_get_name(obj);
        const char *class_name = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(obj));
        const char *label = (name && name[0]) ? name : class_name;
        if (!add_graph_node(&nodes, &node_count, &node_cap, nmo_object_get_id(obj), "behavior", label, false)) {
            free(io_owners);
            free(behavior_ids);
            free_graph_nodes(nodes, node_count);
            nmo_tool_close_session(ctx, session);
            fprintf(stderr, "Error: Out of memory\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    }

    nmo_cli_graph_edge_t *behavior_edges = NULL;
    size_t behavior_edge_count = 0;
    size_t behavior_edge_cap = 0;

    nmo_cli_graph_edge_t *io_edges = NULL;
    size_t io_edge_count = 0;
    size_t io_edge_cap = 0;

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
            if (!add_graph_edge(&behavior_edges, &behavior_edge_count, &behavior_edge_cap,
                                link_id, out_owner->behavior_id, in_owner->behavior_id)) {
                free(io_owners);
                free(behavior_ids);
                free_graph_nodes(nodes, node_count);
                free(behavior_edges);
                free(io_edges);
                nmo_tool_close_session(ctx, session);
                fprintf(stderr, "Error: Out of memory\n");
                return NMO_CLI_EXIT_INTERNAL_ERROR;
            }
        } else if (link_state->in_io_id != 0 && link_state->out_io_id != 0) {
            char io_label[32];
            if (!find_graph_node(nodes, node_count, link_state->in_io_id)) {
                snprintf(io_label, sizeof(io_label), "IO %u", link_state->in_io_id);
                char *name_copy = nmo_tool_strdup(io_label);
                if (!add_graph_node(&nodes, &node_count, &node_cap,
                                    link_state->in_io_id, "unknown", name_copy, true)) {
                    free(name_copy);
                    free(io_owners);
                    free(behavior_ids);
                    free_graph_nodes(nodes, node_count);
                    free(behavior_edges);
                    free(io_edges);
                    nmo_tool_close_session(ctx, session);
                    fprintf(stderr, "Error: Out of memory\n");
                    return NMO_CLI_EXIT_INTERNAL_ERROR;
                }
            }
            if (!find_graph_node(nodes, node_count, link_state->out_io_id)) {
                snprintf(io_label, sizeof(io_label), "IO %u", link_state->out_io_id);
                char *name_copy = nmo_tool_strdup(io_label);
                if (!add_graph_node(&nodes, &node_count, &node_cap,
                                    link_state->out_io_id, "unknown", name_copy, true)) {
                    free(name_copy);
                    free(io_owners);
                    free(behavior_ids);
                    free_graph_nodes(nodes, node_count);
                    free(behavior_edges);
                    free(io_edges);
                    nmo_tool_close_session(ctx, session);
                    fprintf(stderr, "Error: Out of memory\n");
                    return NMO_CLI_EXIT_INTERNAL_ERROR;
                }
            }
            if (!add_graph_edge(&io_edges, &io_edge_count, &io_edge_cap,
                                link_id, link_state->out_io_id, link_state->in_io_id)) {
                free(io_owners);
                free(behavior_ids);
                free_graph_nodes(nodes, node_count);
                free(behavior_edges);
                free(io_edges);
                nmo_tool_close_session(ctx, session);
                fprintf(stderr, "Error: Out of memory\n");
                return NMO_CLI_EXIT_INTERNAL_ERROR;
            }
        }
    }

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        free(io_owners);
        free(behavior_ids);
        free_graph_nodes(nodes, node_count);
        free(behavior_edges);
        free(io_edges);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "behavior_id", behavior_id);
        yyjson_mut_obj_add_uint(doc, data, "node_count", (uint64_t)node_count);
        yyjson_mut_obj_add_uint(doc, data, "edge_count", (uint64_t)(behavior_edge_count + io_edge_count));
        yyjson_mut_obj_add_uint(doc, data, "link_count", (uint64_t)link_count);
        if (broken_links > 0) {
            yyjson_mut_obj_add_uint(doc, data, "broken_links", (uint64_t)broken_links);
        }

        yyjson_mut_val *graph = yyjson_mut_obj(doc);
        yyjson_mut_val *nodes_arr = yyjson_mut_arr(doc);
        yyjson_mut_val *edges_arr = yyjson_mut_arr(doc);

        for (size_t i = 0; i < node_count; ++i) {
            yyjson_mut_val *node = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, node, "id", nodes[i].id);
            if (nodes[i].kind) {
                yyjson_mut_obj_add_str(doc, node, "kind", nodes[i].kind);
            }
            if (nodes[i].name && nodes[i].name[0]) {
                nmo_cli_json_add_str_safe(doc, node, "name", nodes[i].name);
            }
            yyjson_mut_arr_add_val(nodes_arr, node);
        }

        for (size_t i = 0; i < behavior_edge_count; ++i) {
            yyjson_mut_val *edge = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, edge, "from", behavior_edges[i].from_id);
            yyjson_mut_obj_add_uint(doc, edge, "to", behavior_edges[i].to_id);
            if (behavior_edges[i].kind) {
                yyjson_mut_obj_add_str(doc, edge, "kind", behavior_edges[i].kind);
            }
            if (behavior_edges[i].field_path) {
                yyjson_mut_obj_add_str(doc, edge, "field_path", behavior_edges[i].field_path);
            }
            yyjson_mut_obj_add_uint(doc, edge, "link_id", behavior_edges[i].link_id);
            yyjson_mut_arr_add_val(edges_arr, edge);
        }

        for (size_t i = 0; i < io_edge_count; ++i) {
            yyjson_mut_val *edge = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, edge, "from", io_edges[i].from_id);
            yyjson_mut_obj_add_uint(doc, edge, "to", io_edges[i].to_id);
            if (io_edges[i].kind) {
                yyjson_mut_obj_add_str(doc, edge, "kind", io_edges[i].kind);
            }
            if (io_edges[i].field_path) {
                yyjson_mut_obj_add_str(doc, edge, "field_path", io_edges[i].field_path);
            }
            yyjson_mut_obj_add_uint(doc, edge, "link_id", io_edges[i].link_id);
            yyjson_mut_arr_add_val(edges_arr, edge);
        }

        yyjson_mut_obj_add_val(doc, graph, "nodes", nodes_arr);
        yyjson_mut_obj_add_val(doc, graph, "edges", edges_arr);
        yyjson_mut_obj_add_val(doc, data, "graph", graph);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "behavior.graph", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        nmo_cli_print_heading(out, "Behavior Graph", colorize);
        fprintf(out, "\n");
        fprintf(out, "Behaviors: %zu\n", behavior_count);
        fprintf(out, "Links: %zu (resolved %zu, unresolved %zu)\n",
                link_count, behavior_edge_count, io_edge_count);
        if (broken_links > 0) {
            fprintf(out, "Broken links: %zu\n", broken_links);
        }
        fprintf(out, "\n");

        if (behavior_edge_count > 0) {
            static const nmo_cli_table_col_t columns[] = {
                {"From", NMO_CLI_ALIGN_LEFT, 18, 32},
                {"To", NMO_CLI_ALIGN_LEFT, 18, 32},
                {"Link", NMO_CLI_ALIGN_RIGHT, 6, 0},
            };
            nmo_cli_table_t table;
            nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

            for (size_t i = 0; i < behavior_edge_count; ++i) {
                const nmo_cli_graph_node_t *from_node =
                    find_graph_node(nodes, node_count, behavior_edges[i].from_id);
                const nmo_cli_graph_node_t *to_node =
                    find_graph_node(nodes, node_count, behavior_edges[i].to_id);

                char from_buf[64];
                char to_buf[64];
                char link_buf[16];

                if (from_node && from_node->name && from_node->name[0]) {
                    snprintf(from_buf, sizeof(from_buf), "%u:%s", from_node->id, from_node->name);
                } else {
                    snprintf(from_buf, sizeof(from_buf), "%u", behavior_edges[i].from_id);
                }

                if (to_node && to_node->name && to_node->name[0]) {
                    snprintf(to_buf, sizeof(to_buf), "%u:%s", to_node->id, to_node->name);
                } else {
                    snprintf(to_buf, sizeof(to_buf), "%u", behavior_edges[i].to_id);
                }

                snprintf(link_buf, sizeof(link_buf), "%u", behavior_edges[i].link_id);

                const char *cells[] = {from_buf, to_buf, link_buf};
                nmo_cli_table_add_row(&table, cells, 3);
            }

            nmo_cli_table_print(&table, out, colorize);
            nmo_cli_table_free(&table);
            fprintf(out, "\n");
        }

        if (io_edge_count > 0) {
            fprintf(out, "Unresolved I/O links (%zu):\n", io_edge_count);
            static const nmo_cli_table_col_t columns[] = {
                {"From IO", NMO_CLI_ALIGN_RIGHT, 7, 0},
                {"To IO", NMO_CLI_ALIGN_RIGHT, 5, 0},
                {"Link", NMO_CLI_ALIGN_RIGHT, 6, 0},
            };
            nmo_cli_table_t table;
            nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

            for (size_t i = 0; i < io_edge_count; ++i) {
                char from_buf[16];
                char to_buf[16];
                char link_buf[16];
                snprintf(from_buf, sizeof(from_buf), "%u", io_edges[i].from_id);
                snprintf(to_buf, sizeof(to_buf), "%u", io_edges[i].to_id);
                snprintf(link_buf, sizeof(link_buf), "%u", io_edges[i].link_id);

                const char *cells[] = {from_buf, to_buf, link_buf};
                nmo_cli_table_add_row(&table, cells, 3);
            }

            nmo_cli_table_print(&table, out, colorize);
            nmo_cli_table_free(&table);
        }
    }

    free(io_owners);
    free(behavior_ids);
    free_graph_nodes(nodes, node_count);
    free(behavior_edges);
    free(io_edges);
    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}
