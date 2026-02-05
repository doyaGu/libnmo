/**
 * @file nmo_cmd_object.c
 * @brief CLI object command group implementation
 */

#include "nmo_cmd_object.h"

#include "../nmo_cli_common.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "../nmo_tool_session.h"
#include "../nmo_tool_common.h"
#include "../summaries/nmo_object_summary.h"

#include "nmo.h"
#include "app/nmo_context.h"
#include "core/nmo_arena.h"
#include "session/nmo_object_repository.h"
#include "session/nmo_ref_graph.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* For strcasecmp */
#ifdef _WIN32
#include <string.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

/**
 * Find file path (last non-option argument)
 */
static const char *find_file_arg_last(int argc, char **argv) {
    const char *last_non_opt = NULL;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            last_non_opt = argv[i];
        }
    }
    return last_non_opt;
}



/**
 * Parse --class filter
 */
static const char *parse_class_filter(int argc, char **argv) {
    for (int i = 1; i < argc - 1; ++i) {
        if (strcmp(argv[i], "--class") == 0 || strcmp(argv[i], "-c") == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

/* ============================================================================
 * object list
 * ============================================================================ */

int nmo_cmd_object_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo object list [--class <name>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *class_filter = parse_class_filter(argc, argv);

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Get objects */
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to get objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Resolve class filter if specified */
    nmo_class_id_t filter_class_id = 0;
    if (class_filter) {
        filter_class_id = nmo_cli_class_id_from_name(ctx, class_filter);
        if (!filter_class_id) {
            nmo_tool_close_session(ctx, session);
            fprintf(stderr, "Error: Unknown class '%s'\n", class_filter);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *objs = yyjson_mut_arr(doc);
        size_t filtered_count = 0;

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);

            /* Apply class filter */
            if (filter_class_id && !nmo_cli_class_is_derived_from(ctx, class_id, filter_class_id)) {
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

            yyjson_mut_arr_add_val(objs, item);
            filtered_count++;
        }

        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)filtered_count);
        yyjson_mut_obj_add_val(doc, data, "objects", objs);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "object.list", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        /* Table output */
        static const nmo_cli_table_col_t columns[] = {
            {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 30},
            {"Name", NMO_CLI_ALIGN_LEFT, 20, 50},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        size_t filtered_count = 0;
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);

            /* Apply class filter */
            if (filter_class_id && !nmo_cli_class_is_derived_from(ctx, class_id, filter_class_id)) {
                continue;
            }

            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
            const char *name = nmo_object_get_name(obj);

            const char *cells[] = {
                id_buf,
                class_name ? class_name : "-",
                (name && name[0]) ? name : "-"
            };
            nmo_cli_table_add_row(&table, cells, 3);
            filtered_count++;
        }

        fprintf(out, "Objects: %zu", filtered_count);
        if (class_filter) {
            fprintf(out, " (filtered by class: %s)", class_filter);
        }
        fprintf(out, "\n\n");

        nmo_cli_table_print(&table, out, colorize);
        nmo_cli_table_free(&table);
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * object tree
 * ============================================================================ */

/**
 * Build tree node recursively
 */
static nmo_cli_tree_node_t *build_object_tree_node(
    nmo_context_t *ctx,
    nmo_object_t *obj,
    nmo_arena_t *arena)
{
    nmo_cli_tree_node_t *node = nmo_arena_alloc(arena, sizeof(*node), _Alignof(nmo_cli_tree_node_t));
    if (!node) return NULL;

    /* Build label: "ID: Name [ClassName]" */
    const char *name = nmo_object_get_name(obj);
    const char *class_name = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(obj));
    char *label = nmo_arena_alloc(arena, 256, 1);
    if (label) {
        snprintf(label, 256, "%u: %s [%s]",
                 nmo_object_get_id(obj),
                 (name && name[0]) ? name : "(unnamed)",
                 class_name ? class_name : "?");
    }
    node->label = label ? label : "(alloc failed)";
    node->user_data = obj;
    node->first_child = NULL;
    node->next_sibling = NULL;

    /* Build children */
    size_t child_count = nmo_object_get_child_count(obj);
    nmo_cli_tree_node_t *prev_child = NULL;
    for (size_t i = 0; i < child_count; ++i) {
        nmo_object_t *child = nmo_object_get_child(obj, i);
        if (!child) continue;

        nmo_cli_tree_node_t *child_node = build_object_tree_node(ctx, child, arena);
        if (!child_node) continue;

        if (!node->first_child) {
            node->first_child = child_node;
        } else if (prev_child) {
            prev_child->next_sibling = child_node;
        }
        prev_child = child_node;
    }

    return node;
}

static yyjson_mut_val *build_object_json_tree(yyjson_mut_doc *doc, nmo_context_t *ctx, nmo_object_t *obj) {
    if (!doc || !obj) {
        return yyjson_mut_null(doc);
    }

    yyjson_mut_val *node = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, node, "id", nmo_object_get_id(obj));
    yyjson_mut_obj_add_uint(doc, node, "class_id", nmo_object_get_class_id(obj));

    const char *class_name = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(obj));
    if (class_name) {
        yyjson_mut_obj_add_str(doc, node, "class_name", class_name);
    }

    const char *name = nmo_object_get_name(obj);
    if (name && name[0]) {
        nmo_cli_json_add_str_safe(doc, node, "name", name);
    }

    size_t child_count = nmo_object_get_child_count(obj);
    yyjson_mut_obj_add_uint(doc, node, "child_count", (uint64_t)child_count);

    if (child_count > 0) {
        yyjson_mut_val *children = yyjson_mut_arr(doc);
        for (size_t i = 0; i < child_count; ++i) {
            nmo_object_t *child = nmo_object_get_child(obj, i);
            if (!child) {
                continue;
            }
            yyjson_mut_val *child_node = build_object_json_tree(doc, ctx, child);
            yyjson_mut_arr_add_val(children, child_node);
        }
        yyjson_mut_obj_add_val(doc, node, "children", children);
    }

    return node;
}

/**
 * Custom render function for object tree nodes
 */
static void object_tree_render(FILE *out, const nmo_cli_tree_node_t *node, bool colorize) {
    if (colorize) {
        fprintf(out, "%s%s%s", NMO_CLI_COLOR_CYAN, node->label, NMO_CLI_COLOR_RESET);
    } else {
        fprintf(out, "%s", node->label);
    }
}

int nmo_cmd_object_tree(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo object tree <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Get objects */
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

    /* Find root objects (those without parents) */
    size_t root_count = 0;
    for (size_t i = 0; i < object_count; ++i) {
        if (objects[i]->parent == NULL) {
            root_count++;
        }
    }

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "total_objects", (uint64_t)object_count);
        yyjson_mut_obj_add_uint(doc, data, "root_objects", (uint64_t)root_count);

        yyjson_mut_val *roots = yyjson_mut_arr(doc);
        for (size_t i = 0; i < object_count; ++i) {
            if (objects[i]->parent == NULL) {
                yyjson_mut_val *obj_node = build_object_json_tree(doc, ctx, objects[i]);
                yyjson_mut_arr_add_val(roots, obj_node);
            }
        }
        yyjson_mut_obj_add_val(doc, data, "roots", roots);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "object.tree", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        fprintf(out, "Object Tree: %zu objects (%zu roots)\n\n", object_count, root_count);

        /* Build and print tree for each root */
        nmo_arena_t *tree_arena = nmo_session_get_arena(session);
        for (size_t i = 0; i < object_count; ++i) {
            if (objects[i]->parent == NULL) {
                nmo_cli_tree_node_t *tree = build_object_tree_node(ctx, objects[i], tree_arena);
                if (tree) {
                    nmo_cli_print_tree(tree, out, colorize, object_tree_render);
                    fprintf(out, "\n");
                }
            }
        }
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * object show
 * ============================================================================ */

int nmo_cmd_object_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Parse: nmo object show <id> <file> */
    const char *id_str = NULL;
    const char *file_path = NULL;

    int non_opt_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            non_opt_count++;
            if (non_opt_count == 1) {
                id_str = argv[i];
            } else if (non_opt_count == 2) {
                file_path = argv[i];
            }
        }
    }

    if (!id_str || !file_path) {
        fprintf(stderr, "Error: Missing arguments\n");
        fprintf(stderr, "Usage: nmo object show <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t object_id;
    if (!nmo_tool_parse_u32(id_str, &object_id)) {
        fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Get objects */
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to get objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Find object by ID */
    nmo_object_t *target = NULL;
    for (size_t i = 0; i < object_count; ++i) {
        if (nmo_object_get_id(objects[i]) == object_id) {
            target = objects[i];
            break;
        }
    }

    if (!target) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Object %u not found\n", object_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    nmo_class_id_t class_id = nmo_object_get_class_id(target);
    const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
    const char *name = nmo_object_get_name(target);
    uint32_t flags = target->flags;

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", object_id);
        yyjson_mut_obj_add_uint(doc, data, "class_id", class_id);
        if (class_name) {
            yyjson_mut_obj_add_str(doc, data, "class_name", class_name);
        }
        if (name && name[0]) {
            nmo_cli_json_add_str_safe(doc, data, "name", name);
        }
        yyjson_mut_obj_add_uint(doc, data, "flags", flags);

        /* Chunk info */
        nmo_chunk_t *chunk = nmo_object_get_chunk(target);
        if (chunk) {
            yyjson_mut_val *chunk_obj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, chunk_obj, "data_size", (uint64_t)nmo_chunk_get_data_size(chunk));
            yyjson_mut_obj_add_uint(doc, chunk_obj, "pack_size", (uint64_t)chunk->compressed_size);
            yyjson_mut_obj_add_val(doc, data, "chunk", chunk_obj);
        }

        /* Semantic + reflection summary (JSON) */
        {
            yyjson_mut_val *summary = yyjson_mut_obj(doc);
            nmo_summary_output_t sum_out = {
                .stream = out,
                .json_doc = doc,
                .json_data = summary,
                .is_json = true,
                .colorize = false,
                .ctx = ctx,
                .session = session,
            };
            if (nmo_object_summary(target, &sum_out)) {
                yyjson_mut_obj_add_val(doc, data, "summary", summary);
            }
        }

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "object.show", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        nmo_cli_print_heading(out, "Object Details", colorize);

        char buf[128];
        snprintf(buf, sizeof(buf), "#%u (%s)", object_id,
                 (name && name[0]) ? name : "(unnamed)");
        nmo_cli_print_kv(out, "ID / Name", buf, 14, colorize);

        snprintf(buf, sizeof(buf), "#%u (%s)", class_id, class_name ? class_name : "-");
        nmo_cli_print_kv(out, "Class", buf, 14, colorize);

        snprintf(buf, sizeof(buf), "0x%08X", flags);
        nmo_cli_print_kv(out, "Flags", buf, 14, colorize);

        /* Chunk info */
        nmo_chunk_t *chunk = nmo_object_get_chunk(target);
        if (chunk) {
            fprintf(out, "\n");
            nmo_cli_print_heading(out, "Chunk", colorize);
            snprintf(buf, sizeof(buf), "%zu bytes", nmo_chunk_get_data_size(chunk));
            nmo_cli_print_kv(out, "Data Size", buf, 14, colorize);
            snprintf(buf, sizeof(buf), "%zu bytes", chunk->compressed_size);
            nmo_cli_print_kv(out, "Pack Size", buf, 14, colorize);
        }

        /* Semantic + reflection summary (text) */
        {
            nmo_summary_output_t sum_out = {
                .stream = out,
                .json_doc = NULL,
                .json_data = NULL,
                .is_json = false,
                .colorize = colorize,
                .ctx = ctx,
                .session = session,
            };
            (void)nmo_object_summary(target, &sum_out);
        }
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * object find
 * ============================================================================ */

/**
 * Parse --name filter
 */
static const char *parse_name_filter(int argc, char **argv) {
    for (int i = 1; i < argc - 1; ++i) {
        if (strcmp(argv[i], "--name") == 0 || strcmp(argv[i], "-n") == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

/**
 * Simple wildcard match (* at start/end only)
 */
static bool simple_pattern_match(const char *pattern, const char *str) {
    if (!pattern || !str) return false;

    size_t plen = strlen(pattern);
    size_t slen = strlen(str);

    /* "*" matches everything */
    if (plen == 1 && pattern[0] == '*') return true;

    /* "*suffix" */
    if (pattern[0] == '*') {
        const char *suffix = pattern + 1;
        size_t suffix_len = plen - 1;
        if (slen >= suffix_len) {
            return strcasecmp(str + slen - suffix_len, suffix) == 0;
        }
        return false;
    }

    /* "prefix*" */
    if (pattern[plen - 1] == '*') {
        size_t prefix_len = plen - 1;
        if (slen >= prefix_len) {
            return strncasecmp(str, pattern, prefix_len) == 0;
        }
        return false;
    }

    /* Exact match (case-insensitive) */
    return strcasecmp(pattern, str) == 0;
}

int nmo_cmd_object_find(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo object find [--name <pattern>] [--class <name>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *class_filter = parse_class_filter(argc, argv);
    const char *name_filter = parse_name_filter(argc, argv);

    if (!class_filter && !name_filter) {
        fprintf(stderr, "Error: At least one filter required (--name or --class)\n");
        fprintf(stderr, "Usage: nmo object find [--name <pattern>] [--class <name>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Get objects */
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to get objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Resolve class filter if specified */
    nmo_class_id_t filter_class_id = 0;
    if (class_filter) {
        filter_class_id = nmo_cli_class_id_from_name(ctx, class_filter);
        if (!filter_class_id) {
            nmo_tool_close_session(ctx, session);
            fprintf(stderr, "Error: Unknown class '%s'\n", class_filter);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        /* Record query */
        yyjson_mut_val *query = yyjson_mut_obj(doc);
        if (class_filter) {
            yyjson_mut_obj_add_str(doc, query, "class", class_filter);
        }
        if (name_filter) {
            yyjson_mut_obj_add_str(doc, query, "name_pattern", name_filter);
        }
        yyjson_mut_obj_add_val(doc, data, "query", query);

        yyjson_mut_val *matches = yyjson_mut_arr(doc);
        size_t match_count = 0;

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);

            /* Apply class filter */
            if (filter_class_id && !nmo_cli_class_is_derived_from(ctx, class_id, filter_class_id)) {
                continue;
            }

            /* Apply name filter */
            const char *name = nmo_object_get_name(obj);
            if (name_filter && !simple_pattern_match(name_filter, name ? name : "")) {
                continue;
            }

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "id", nmo_object_get_id(obj));
            yyjson_mut_obj_add_uint(doc, item, "class_id", class_id);

            const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
            if (class_name) {
                yyjson_mut_obj_add_str(doc, item, "class_name", class_name);
            }

            if (name && name[0]) {
                nmo_cli_json_add_str_safe(doc, item, "name", name);
            }

            yyjson_mut_arr_add_val(matches, item);
            match_count++;
        }

        yyjson_mut_obj_add_uint(doc, data, "match_count", (uint64_t)match_count);
        yyjson_mut_obj_add_val(doc, data, "matches", matches);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "object.find", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        /* Table output */
        static const nmo_cli_table_col_t columns[] = {
            {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 30},
            {"Name", NMO_CLI_ALIGN_LEFT, 20, 50},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        size_t match_count = 0;
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);

            /* Apply class filter */
            if (filter_class_id && !nmo_cli_class_is_derived_from(ctx, class_id, filter_class_id)) {
                continue;
            }

            /* Apply name filter */
            const char *name = nmo_object_get_name(obj);
            if (name_filter && !simple_pattern_match(name_filter, name ? name : "")) {
                continue;
            }

            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);

            const char *cells[] = {
                id_buf,
                class_name ? class_name : "-",
                (name && name[0]) ? name : "-"
            };
            nmo_cli_table_add_row(&table, cells, 3);
            match_count++;
        }

        fprintf(out, "Found: %zu objects", match_count);
        if (class_filter) {
            fprintf(out, " (class: %s)", class_filter);
        }
        if (name_filter) {
            fprintf(out, " (name: %s)", name_filter);
        }
        fprintf(out, "\n\n");

        nmo_cli_table_print(&table, out, colorize);
        nmo_cli_table_free(&table);
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

/**
 * Parse object ID from command line
 */
static nmo_object_id_t parse_object_id(int argc, char **argv) {
    /* Find first numeric argument (skip command, subcommand) */
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            /* Try to parse as object ID */
            char *endptr = NULL;
            unsigned long id = strtoul(argv[i], &endptr, 10);
            if (endptr && *endptr == '\0' && id > 0 && id <= UINT32_MAX) {
                return (nmo_object_id_t)id;
            }
        }
    }
    return 0;
}

/* ============================================================================
 * object refs
 * ============================================================================ */

int nmo_cmd_object_refs(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo object refs <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_id_t object_id = parse_object_id(argc, argv);
    if (object_id == 0) {
        fprintf(stderr, "Error: No valid object ID specified\n");
        fprintf(stderr, "Usage: nmo object refs <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Find the object */
    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, object_id);
    if (!obj) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Object %u not found\n", object_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Create reference graph */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to create arena\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_ref_graph_t *graph = nmo_ref_graph_create(session, arena);
    if (!graph) {
        nmo_arena_destroy(arena);
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Get outgoing edges */
    nmo_ref_edge_t *out_edges = NULL;
    size_t out_count = 0;
    nmo_ref_graph_get_object_edges(graph, object_id, NMO_REF_DIR_OUTGOING, &out_edges, &out_count);

    /* Get incoming edges */
    nmo_ref_edge_t *in_edges = NULL;
    size_t in_count = 0;
    nmo_ref_graph_get_object_edges(graph, object_id, NMO_REF_DIR_INCOMING, &in_edges, &in_count);

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        /* Object info */
        yyjson_mut_obj_add_uint(doc, data, "id", object_id);
        const char *class_name = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(obj));
        if (class_name) {
            yyjson_mut_obj_add_str(doc, data, "class_name", class_name);
        }
        const char *name = nmo_object_get_name(obj);
        if (name && name[0]) {
            nmo_cli_json_add_str_safe(doc, data, "name", name);
        }

        /* Outgoing references */
        yyjson_mut_val *outgoing = yyjson_mut_arr(doc);
        for (size_t i = 0; i < out_count; ++i) {
            yyjson_mut_val *edge = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, edge, "target_id", out_edges[i].to);
            yyjson_mut_obj_add_str(doc, edge, "kind", nmo_ref_kind_name(out_edges[i].kind));
            yyjson_mut_obj_add_str(doc, edge, "field", out_edges[i].field_path);
            if (out_edges[i].index > 0) {
                yyjson_mut_obj_add_uint(doc, edge, "index", out_edges[i].index);
            }

            /* Add target object info if available */
            nmo_object_t *target = nmo_object_repository_find_by_id(repo, out_edges[i].to);
            if (target) {
                const char *target_class = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(target));
                if (target_class) {
                    yyjson_mut_obj_add_str(doc, edge, "target_class", target_class);
                }
                const char *target_name = nmo_object_get_name(target);
                if (target_name && target_name[0]) {
                    nmo_cli_json_add_str_safe(doc, edge, "target_name", target_name);
                }
            } else {
                yyjson_mut_obj_add_bool(doc, edge, "broken", true);
            }

            yyjson_mut_arr_add_val(outgoing, edge);
        }
        yyjson_mut_obj_add_val(doc, data, "outgoing", outgoing);
        yyjson_mut_obj_add_uint(doc, data, "outgoing_count", (uint64_t)out_count);

        /* Incoming references */
        yyjson_mut_val *incoming = yyjson_mut_arr(doc);
        for (size_t i = 0; i < in_count; ++i) {
            yyjson_mut_val *edge = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, edge, "source_id", in_edges[i].from);
            yyjson_mut_obj_add_str(doc, edge, "kind", nmo_ref_kind_name(in_edges[i].kind));
            yyjson_mut_obj_add_str(doc, edge, "field", in_edges[i].field_path);
            if (in_edges[i].index > 0) {
                yyjson_mut_obj_add_uint(doc, edge, "index", in_edges[i].index);
            }

            /* Add source object info */
            nmo_object_t *source = nmo_object_repository_find_by_id(repo, in_edges[i].from);
            if (source) {
                const char *source_class = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(source));
                if (source_class) {
                    yyjson_mut_obj_add_str(doc, edge, "source_class", source_class);
                }
                const char *source_name = nmo_object_get_name(source);
                if (source_name && source_name[0]) {
                    nmo_cli_json_add_str_safe(doc, edge, "source_name", source_name);
                }
            }

            yyjson_mut_arr_add_val(incoming, edge);
        }
        yyjson_mut_obj_add_val(doc, data, "incoming", incoming);
        yyjson_mut_obj_add_uint(doc, data, "incoming_count", (uint64_t)in_count);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "object.refs", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        /* Text output */
        const char *obj_name = nmo_object_get_name(obj);
        const char *obj_class = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(obj));
        fprintf(out, "References for object %u: %s [%s]\n\n",
                object_id,
                (obj_name && obj_name[0]) ? obj_name : "(unnamed)",
                obj_class ? obj_class : "?");

        /* Outgoing references */
        fprintf(out, "Outgoing references (%zu):\n", out_count);
        if (out_count == 0) {
            fprintf(out, "  (none)\n");
        } else {
            static const nmo_cli_table_col_t out_cols[] = {
                {"Target", NMO_CLI_ALIGN_RIGHT, 8, 0},
                {"Kind", NMO_CLI_ALIGN_LEFT, 15, 0},
                {"Field", NMO_CLI_ALIGN_LEFT, 20, 0},
                {"Target Class", NMO_CLI_ALIGN_LEFT, 18, 0},
                {"Target Name", NMO_CLI_ALIGN_LEFT, 25, 0},
            };

            nmo_cli_table_t table;
            nmo_cli_table_init(&table, out_cols, sizeof(out_cols) / sizeof(out_cols[0]));

            for (size_t i = 0; i < out_count; ++i) {
                char id_buf[16];
                snprintf(id_buf, sizeof(id_buf), "%u", out_edges[i].to);

                char field_buf[32];
                if (out_edges[i].index > 0) {
                    snprintf(field_buf, sizeof(field_buf), "%s[%u]",
                             out_edges[i].field_path, out_edges[i].index);
                } else {
                    snprintf(field_buf, sizeof(field_buf), "%s", out_edges[i].field_path);
                }

                nmo_object_t *target = nmo_object_repository_find_by_id(repo, out_edges[i].to);
                const char *target_class = "-";
                const char *target_name = "-";

                if (target) {
                    const char *tc = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(target));
                    if (tc) target_class = tc;
                    const char *tn = nmo_object_get_name(target);
                    if (tn && tn[0]) target_name = tn;
                } else {
                    target_name = "(BROKEN)";
                }

                const char *cells[] = {
                    id_buf,
                    nmo_ref_kind_name(out_edges[i].kind),
                    field_buf,
                    target_class,
                    target_name
                };
                nmo_cli_table_add_row(&table, cells, 5);
            }

            nmo_cli_table_print(&table, out, colorize);
            nmo_cli_table_free(&table);
        }

        fprintf(out, "\n");

        /* Incoming references */
        fprintf(out, "Incoming references (%zu):\n", in_count);
        if (in_count == 0) {
            fprintf(out, "  (none)\n");
        } else {
            static const nmo_cli_table_col_t in_cols[] = {
                {"Source", NMO_CLI_ALIGN_RIGHT, 8, 0},
                {"Kind", NMO_CLI_ALIGN_LEFT, 15, 0},
                {"Field", NMO_CLI_ALIGN_LEFT, 20, 0},
                {"Source Class", NMO_CLI_ALIGN_LEFT, 18, 0},
                {"Source Name", NMO_CLI_ALIGN_LEFT, 25, 0},
            };

            nmo_cli_table_t table;
            nmo_cli_table_init(&table, in_cols, sizeof(in_cols) / sizeof(in_cols[0]));

            for (size_t i = 0; i < in_count; ++i) {
                char id_buf[16];
                snprintf(id_buf, sizeof(id_buf), "%u", in_edges[i].from);

                char field_buf[32];
                if (in_edges[i].index > 0) {
                    snprintf(field_buf, sizeof(field_buf), "%s[%u]",
                             in_edges[i].field_path, in_edges[i].index);
                } else {
                    snprintf(field_buf, sizeof(field_buf), "%s", in_edges[i].field_path);
                }

                nmo_object_t *source = nmo_object_repository_find_by_id(repo, in_edges[i].from);
                const char *source_class = "-";
                const char *source_name = "-";

                if (source) {
                    const char *sc = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(source));
                    if (sc) source_class = sc;
                    const char *sn = nmo_object_get_name(source);
                    if (sn && sn[0]) source_name = sn;
                }

                const char *cells[] = {
                    id_buf,
                    nmo_ref_kind_name(in_edges[i].kind),
                    field_buf,
                    source_class,
                    source_name
                };
                nmo_cli_table_add_row(&table, cells, 5);
            }

            nmo_cli_table_print(&table, out, colorize);
            nmo_cli_table_free(&table);
        }
    }

    nmo_arena_destroy(arena);
    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}
