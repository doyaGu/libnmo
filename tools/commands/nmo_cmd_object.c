/**
 * @file nmo_cmd_object.c
 * @brief CLI object command group implementation
 */

#include "nmo_cmd_object.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"
#include "app/nmo_object_summary.h"

#include "nmo.h"
#include "app/nmo_context.h"
#include "app/nmo_object_hierarchy.h"
#include "core/nmo_arena.h"
#include "dsl/nmo_dsl.h"
#include "object/nmo_object_repository.h"
#include "session/nmo_ref_graph.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * object list - visitor callbacks
 * ============================================================================ */

typedef struct {
    yyjson_mut_doc *doc;
    yyjson_mut_val *arr;
    const nmo_cmd_ctx_t *ctx;
} list_json_data_t;

static int list_json_visitor(size_t index, nmo_object_t *obj, const nmo_cmd_ctx_t *c, void *user) {
    (void)index;
    list_json_data_t *d = (list_json_data_t *)user;
    yyjson_mut_val *item = yyjson_mut_obj(d->doc);
    yyjson_mut_obj_add_uint(d->doc, item, "id", nmo_object_get_id(obj));
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    yyjson_mut_obj_add_uint(d->doc, item, "class_id", class_id);
    const char *class_name = nmo_core_class_name(c, class_id);
    if (class_name) yyjson_mut_obj_add_str(d->doc, item, "class_name", class_name);
    const char *name = nmo_object_get_name(obj);
    if (name && name[0]) nmo_cli_json_add_str_safe(d->doc, item, "name", name);
    yyjson_mut_arr_add_val(d->arr, item);
    return 0;
}

typedef struct {
    nmo_cli_table_t *table;
    const nmo_cmd_ctx_t *ctx;
} list_table_data_t;

static int list_table_visitor(size_t index, nmo_object_t *obj, const nmo_cmd_ctx_t *c, void *user) {
    (void)index;
    list_table_data_t *d = (list_table_data_t *)user;
    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));
    const char *class_name = nmo_core_class_name(c, nmo_object_get_class_id(obj));
    const char *name = nmo_object_get_name(obj);
    const char *cells[] = { id_buf, class_name ? class_name : "-", (name && name[0]) ? name : "-" };
    nmo_cli_table_add_row(d->table, cells, 3);
    return 0;
}

/* ============================================================================
 * object list
 * ============================================================================ */

int nmo_cmd_object_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--class",  "-c", NMO_OPT_STRING, "Filter by class name"},
        {"--filter", "-f", NMO_OPT_STRING, "Filter by DSL expression"},
    };
    nmo_opt_val_t vals[2];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos };
    if (nmo_opt_parse(argc, argv, opts, 2, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *class_filter_str = vals[0].present ? vals[0].val.str : NULL;
    const char *filter_expr      = vals[1].present ? vals[1].val.str : NULL;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Build filter */
    nmo_core_object_filter_t filter = {0};
    if (class_filter_str) {
        filter.class_id = nmo_core_class_id(&c, class_filter_str);
        if (!filter.class_id) {
            fprintf(stderr, "Error: Unknown class '%s'\n", class_filter_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        filter.class_derived = true;
    }
    if (filter_expr) {
        nmo_dsl_compile_options_t compile_opts = { .mode = NMO_DSL_MODE_EXPRESSION };
        nmo_status_t st = nmo_dsl_compile(c.registry, NULL, filter_expr, &compile_opts, &filter.dsl_filter);
        if (st != NMO_OK) {
            fprintf(stderr, "Error: Failed to compile filter expression: %s\n", filter_expr);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    nmo_core_iter_result_t result;
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *arr = yyjson_mut_arr(doc);

        list_json_data_t jd = { .doc = doc, .arr = arr, .ctx = &c };
        nmo_core_iter_objects(&c, &filter, list_json_visitor, &jd, &result);

        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)result.matched);
        yyjson_mut_obj_add_val(doc, data, "objects", arr);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.list");
    } else {
        /* Table output */
        static const nmo_cli_table_col_t columns[] = {
            {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 30},
            {"Name", NMO_CLI_ALIGN_LEFT, 20, 50},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        list_table_data_t td = { .table = &table, .ctx = &c };
        nmo_core_iter_objects(&c, &filter, list_table_visitor, &td, &result);

        fprintf(c.out, "Objects: %zu", result.matched);
        if (class_filter_str) {
            fprintf(c.out, " (filtered by class: %s)", class_filter_str);
        }
        if (filter_expr) {
            fprintf(c.out, " (filtered by: %s)", filter_expr);
        }
        fprintf(c.out, "\n\n");

        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    if (filter.dsl_filter) {
        nmo_dsl_program_destroy(filter.dsl_filter);
    }
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object tree - Ownership-based hierarchy
 *
 * Builds a tree by analyzing reference fields to determine object ownership:
 *   - Forward ownership: CKBeObject.script_ids, CKBehavior.sub_behaviors,
 *     inputs, outputs, in_parameters, out_parameters, local_parameters, etc.
 *   - Reverse ownership: CK3dEntity.parent_id (child points to parent)
 *   - All other references (mesh_ids, material, texture, etc.) are not ownership.
 * ============================================================================ */

/** Create a tree node for an object (label + user_data, no children yet) */
static nmo_cli_tree_node_t *create_tree_label(
    nmo_context_t *ctx,
    nmo_object_t *obj,
    nmo_arena_t *arena)
{
    nmo_cli_tree_node_t *node = nmo_arena_alloc(arena, sizeof(*node), _Alignof(nmo_cli_tree_node_t));
    if (!node) return NULL;

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
    return node;
}

/** Append a child node to a parent (at end of sibling list) */
static void tree_node_add_child(nmo_cli_tree_node_t *parent, nmo_cli_tree_node_t *child) {
    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        nmo_cli_tree_node_t *last = parent->first_child;
        while (last->next_sibling) last = last->next_sibling;
        last->next_sibling = child;
    }
}

/** Build JSON tree recursively from tree nodes */
static yyjson_mut_val *build_json_from_tree(
    yyjson_mut_doc *doc,
    nmo_context_t *ctx,
    const nmo_cli_tree_node_t *node)
{
    if (!doc || !node) return yyjson_mut_null(doc);

    nmo_object_t *obj = (nmo_object_t *)node->user_data;
    yyjson_mut_val *jnode = yyjson_mut_obj(doc);

    yyjson_mut_obj_add_uint(doc, jnode, "id", nmo_object_get_id(obj));
    yyjson_mut_obj_add_uint(doc, jnode, "class_id", nmo_object_get_class_id(obj));

    const char *class_name = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(obj));
    if (class_name) {
        yyjson_mut_obj_add_str(doc, jnode, "class_name", class_name);
    }

    const char *name = nmo_object_get_name(obj);
    if (name && name[0]) {
        nmo_cli_json_add_str_safe(doc, jnode, "name", name);
    }

    /* Count children */
    size_t child_count = 0;
    for (const nmo_cli_tree_node_t *c = node->first_child; c; c = c->next_sibling) {
        child_count++;
    }
    yyjson_mut_obj_add_uint(doc, jnode, "child_count", (uint64_t)child_count);

    if (child_count > 0) {
        yyjson_mut_val *children = yyjson_mut_arr(doc);
        for (const nmo_cli_tree_node_t *c = node->first_child; c; c = c->next_sibling) {
            yyjson_mut_val *cjson = build_json_from_tree(doc, ctx, c);
            yyjson_mut_arr_add_val(children, cjson);
        }
        yyjson_mut_obj_add_val(doc, jnode, "children", children);
    }

    return jnode;
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
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Get objects */
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_hierarchy_t hierarchy;
    if (!nmo_object_hierarchy_build(c.ctx, c.session, &hierarchy)) {
        fprintf(stderr, "Error: Failed to build object hierarchy\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    size_t map_size = hierarchy.map_size;
    nmo_object_id_t *parent_of = hierarchy.parent_of;

    nmo_cli_tree_node_t **node_map = (nmo_cli_tree_node_t **)calloc(map_size, sizeof(nmo_cli_tree_node_t *));
    if (!node_map) {
        nmo_object_hierarchy_free(&hierarchy);
        fprintf(stderr, "Error: Out of memory\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* ---- Phase 2: Create tree nodes ---- */
    nmo_arena_t *tree_arena = nmo_session_get_arena(c.session);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_id_t id = nmo_object_get_id(objects[i]);
        if (id > 0 && id < map_size) {
            node_map[id] = create_tree_label(c.ctx, objects[i], tree_arena);
        }
    }

    /* ---- Phase 3: Link children to parents ---- */
    size_t root_count = 0;
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_id_t id = nmo_object_get_id(objects[i]);
        if (id == 0 || id >= map_size || !node_map[id]) continue;

        nmo_object_id_t pid = parent_of[id];
        if (pid > 0 && pid < map_size && node_map[pid]) {
            tree_node_add_child(node_map[pid], node_map[id]);
        } else {
            root_count++;
        }
    }

    /* ---- Phase 4: Output ---- */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "total_objects", (uint64_t)object_count);
        yyjson_mut_obj_add_uint(doc, data, "root_objects", (uint64_t)root_count);

        yyjson_mut_val *roots = yyjson_mut_arr(doc);
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_id_t id = nmo_object_get_id(objects[i]);
            if (id == 0 || id >= map_size || !node_map[id]) continue;
            if (parent_of[id] > 0 && parent_of[id] < map_size && node_map[parent_of[id]]) continue;

            yyjson_mut_val *obj_node = build_json_from_tree(doc, c.ctx, node_map[id]);
            yyjson_mut_arr_add_val(roots, obj_node);
        }
        yyjson_mut_obj_add_val(doc, data, "roots", roots);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.tree");
    } else {
        fprintf(c.out, "Object Tree: %zu objects (%zu roots)\n\n", object_count, root_count);

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_id_t id = nmo_object_get_id(objects[i]);
            if (id == 0 || id >= map_size || !node_map[id]) continue;
            if (parent_of[id] > 0 && parent_of[id] < map_size && node_map[parent_of[id]]) continue;

            nmo_cli_print_tree(node_map[id], c.out, c.colorize, object_tree_render);
            fprintf(c.out, "\n");
        }
    }

    nmo_object_hierarchy_free(&hierarchy);
    free(node_map);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object show
 * ============================================================================ */

int nmo_cmd_object_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Parse: nmo object show [--select <path>]... [--expr <expr>]... [--depth N] [--full] <id> <file> */
    const char *id_str = NULL;

    const char *select_paths[64];
    size_t select_path_count = 0;

    const char *exprs[64];
    size_t expr_count = 0;

    int depth = -1; /* -1 = use default */
    bool full_mode = false;

    int non_opt_count = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--depth") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: Missing argument for --depth\n");
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            uint32_t d = 0;
            if (!nmo_tool_parse_u32_dec(argv[++i], &d)) {
                fprintf(stderr, "Error: Invalid depth value '%s'\n", argv[i]);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            depth = (int)d;
            continue;
        }

        if (strcmp(argv[i], "--full") == 0) {
            full_mode = true;
            continue;
        }

        if (strcmp(argv[i], "--select") == 0 || strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: Missing argument for %s\n", argv[i]);
                fprintf(stderr,
                        "Usage: nmo object show [--select <path>]... [--expr <expr>]... <id> <file>\n");
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            if (select_path_count < (sizeof(select_paths) / sizeof(select_paths[0]))) {
                select_paths[select_path_count++] = argv[i + 1];
            } else {
                fprintf(stderr, "Warning: --select limit reached (64 max), extra paths ignored\n");
            }
            i++;
            continue;
        }

        if (strcmp(argv[i], "--expr") == 0 || strcmp(argv[i], "-e") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: Missing argument for %s\n", argv[i]);
                fprintf(stderr,
                        "Usage: nmo object show [--select <path>]... [--expr <expr>]... <id> <file>\n");
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            if (expr_count < (sizeof(exprs) / sizeof(exprs[0]))) {
                exprs[expr_count++] = argv[i + 1];
            } else {
                fprintf(stderr, "Warning: --expr limit reached (64 max), extra expressions ignored\n");
            }
            i++;
            continue;
        }

        if (argv[i][0] != '-') {
            non_opt_count++;
            if (non_opt_count == 1) {
                id_str = argv[i];
            }
        }
    }

    if (!id_str) {
        fprintf(stderr, "Error: Missing arguments\n");
        fprintf(stderr, "Usage: nmo object show [--select <path>]... [--expr <expr>]... <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t object_id;
    if (!nmo_tool_parse_u32(id_str, &object_id)) {
        fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Get objects */
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
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
        fprintf(stderr, "Error: Object %u not found\n", object_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_class_id_t class_id = nmo_object_get_class_id(target);
    const char *class_name = nmo_cli_class_name_from_id(c.ctx, class_id);
    const char *name = nmo_object_get_name(target);
    uint32_t flags = nmo_object_get_flags(target);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
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
            nmo_summary_config_t cfg = nmo_summary_config_default();
            if (full_mode) {
                cfg.max_depth = 8;
                cfg.array_preview_max = 64;
                cfg.text_preview_max = 64;
            }
            if (depth >= 0) {
                cfg.max_depth = (uint32_t)depth;
            }

            yyjson_mut_val *summary = yyjson_mut_obj(doc);
            nmo_summary_output_t sum_out = {
                .stream = c.out,
                .json_doc = doc,
                .json_data = summary,
                .is_json = true,
                .colorize = false,
                .ctx = c.ctx,
                .session = c.session,
            };
            bool ok = false;
            if (select_path_count > 0) {
                ok |= nmo_object_summary_select_with_config(target, &sum_out, &cfg, select_paths, select_path_count);
            }
            if (expr_count > 0) {
                ok |= nmo_object_summary_expr_with_config(target, &sum_out, &cfg, exprs, expr_count);
            }
            if (select_path_count == 0 && expr_count == 0) {
                ok |= nmo_object_summary_with_config(target, &sum_out, &cfg);
            }
            if (ok) {
                yyjson_mut_obj_add_val(doc, data, "summary", summary);
            }
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "object.show");
    } else {
        nmo_cli_print_heading(c.out, "Object Details", c.colorize);

        char buf[128];
        snprintf(buf, sizeof(buf), "#%u (%s)", object_id,
                 (name && name[0]) ? name : "(unnamed)");
        nmo_cli_print_kv(c.out, "ID / Name", buf, 14, c.colorize);

        snprintf(buf, sizeof(buf), "#%u (%s)", class_id, class_name ? class_name : "-");
        nmo_cli_print_kv(c.out, "Class", buf, 14, c.colorize);

        snprintf(buf, sizeof(buf), "0x%08X", flags);
        nmo_cli_print_kv(c.out, "Flags", buf, 14, c.colorize);

        /* Chunk info */
        nmo_chunk_t *chunk = nmo_object_get_chunk(target);
        if (chunk) {
            fprintf(c.out, "\n");
            nmo_cli_print_heading(c.out, "Chunk", c.colorize);
            snprintf(buf, sizeof(buf), "%zu bytes", nmo_chunk_get_data_size(chunk));
            nmo_cli_print_kv(c.out, "Data Size", buf, 14, c.colorize);
            snprintf(buf, sizeof(buf), "%zu bytes", chunk->compressed_size);
            nmo_cli_print_kv(c.out, "Pack Size", buf, 14, c.colorize);
        }

        /* Semantic + reflection summary (text) */
        {
            nmo_summary_config_t cfg = nmo_summary_config_default();
            if (full_mode) {
                cfg.max_depth = 8;
                cfg.array_preview_max = 64;
                cfg.text_preview_max = 32;
            }
            if (depth >= 0) {
                cfg.max_depth = (uint32_t)depth;
            }

            nmo_summary_output_t sum_out = {
                .stream = c.out,
                .json_doc = NULL,
                .json_data = NULL,
                .is_json = false,
                .colorize = c.colorize,
                .ctx = c.ctx,
                .session = c.session,
            };
            if (select_path_count > 0) {
                (void)nmo_object_summary_select_with_config(target, &sum_out, &cfg, select_paths, select_path_count);
            }
            if (expr_count > 0) {
                (void)nmo_object_summary_expr_with_config(target, &sum_out, &cfg, exprs, expr_count);
            }
            if (select_path_count == 0 && expr_count == 0) {
                (void)nmo_object_summary_with_config(target, &sum_out, &cfg);
            }
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object find - visitor callbacks
 * ============================================================================ */

typedef struct {
    yyjson_mut_doc *doc;
    yyjson_mut_val *arr;
    const nmo_cmd_ctx_t *ctx;
} find_json_data_t;

static int find_json_visitor(size_t index, nmo_object_t *obj, const nmo_cmd_ctx_t *c, void *user) {
    (void)index;
    find_json_data_t *d = (find_json_data_t *)user;
    yyjson_mut_val *item = yyjson_mut_obj(d->doc);
    yyjson_mut_obj_add_uint(d->doc, item, "id", nmo_object_get_id(obj));
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    yyjson_mut_obj_add_uint(d->doc, item, "class_id", class_id);
    const char *class_name = nmo_core_class_name(c, class_id);
    if (class_name) yyjson_mut_obj_add_str(d->doc, item, "class_name", class_name);
    const char *name = nmo_object_get_name(obj);
    if (name && name[0]) nmo_cli_json_add_str_safe(d->doc, item, "name", name);
    yyjson_mut_arr_add_val(d->arr, item);
    return 0;
}

typedef struct {
    nmo_cli_table_t *table;
    const nmo_cmd_ctx_t *ctx;
} find_table_data_t;

static int find_table_visitor(size_t index, nmo_object_t *obj, const nmo_cmd_ctx_t *c, void *user) {
    (void)index;
    find_table_data_t *d = (find_table_data_t *)user;
    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));
    const char *class_name = nmo_core_class_name(c, nmo_object_get_class_id(obj));
    const char *name = nmo_object_get_name(obj);
    const char *cells[] = { id_buf, class_name ? class_name : "-", (name && name[0]) ? name : "-" };
    nmo_cli_table_add_row(d->table, cells, 3);
    return 0;
}

int nmo_cmd_object_find(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--class", "-c", NMO_OPT_STRING, "Filter by class name"},
        {"--name",  "-n", NMO_OPT_STRING, "Filter by name pattern"},
    };
    nmo_opt_val_t vals[2];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos };
    if (nmo_opt_parse(argc, argv, opts, 2, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *class_filter_str = vals[0].present ? vals[0].val.str : NULL;
    const char *name_filter      = vals[1].present ? vals[1].val.str : NULL;

    if (!class_filter_str && !name_filter) {
        fprintf(stderr, "Error: At least one filter required (--name or --class)\n");
        fprintf(stderr, "Usage: nmo object find [--name <pattern>] [--class <name>] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Build filter */
    nmo_core_object_filter_t filter = {0};
    if (class_filter_str) {
        filter.class_id = nmo_core_class_id(&c, class_filter_str);
        if (!filter.class_id) {
            fprintf(stderr, "Error: Unknown class '%s'\n", class_filter_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        filter.class_derived = true;
    }
    if (name_filter) {
        filter.name_pattern = name_filter;
    }

    nmo_core_iter_result_t result;
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        /* Record query */
        yyjson_mut_val *query = yyjson_mut_obj(doc);
        if (class_filter_str) {
            yyjson_mut_obj_add_str(doc, query, "class", class_filter_str);
        }
        if (name_filter) {
            yyjson_mut_obj_add_str(doc, query, "name_pattern", name_filter);
        }
        yyjson_mut_obj_add_val(doc, data, "query", query);

        yyjson_mut_val *matches = yyjson_mut_arr(doc);

        find_json_data_t jd = { .doc = doc, .arr = matches, .ctx = &c };
        nmo_core_iter_objects(&c, &filter, find_json_visitor, &jd, &result);

        yyjson_mut_obj_add_uint(doc, data, "match_count", (uint64_t)result.matched);
        yyjson_mut_obj_add_val(doc, data, "matches", matches);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.find");
    } else {
        /* Table output */
        static const nmo_cli_table_col_t columns[] = {
            {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 30},
            {"Name", NMO_CLI_ALIGN_LEFT, 20, 50},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        find_table_data_t td = { .table = &table, .ctx = &c };
        nmo_core_iter_objects(&c, &filter, find_table_visitor, &td, &result);

        fprintf(c.out, "Found: %zu objects", result.matched);
        if (class_filter_str) {
            fprintf(c.out, " (class: %s)", class_filter_str);
        }
        if (name_filter) {
            fprintf(c.out, " (name: %s)", name_filter);
        }
        fprintf(c.out, "\n\n");

        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object refs - visitor callbacks for nmo_core_iter_refs
 * ============================================================================ */

/** Visitor data for JSON ref output */
typedef struct {
    yyjson_mut_doc *doc;
    yyjson_mut_val *outgoing;
    yyjson_mut_val *incoming;
} cli_refs_json_data_t;

static int cli_refs_json_visitor(const nmo_core_ref_info_t *info,
                                 const nmo_cmd_ctx_t *c, void *user) {
    (void)c;
    cli_refs_json_data_t *d = (cli_refs_json_data_t *)user;
    yyjson_mut_doc *doc = d->doc;

    yyjson_mut_val *edge = yyjson_mut_obj(doc);

    if (info->is_incoming) {
        yyjson_mut_obj_add_uint(doc, edge, "source_id", info->edge->from);
    } else {
        yyjson_mut_obj_add_uint(doc, edge, "target_id", info->edge->to);
    }

    yyjson_mut_obj_add_str(doc, edge, "kind",
                           nmo_ref_kind_name(info->edge->kind));
    yyjson_mut_obj_add_str(doc, edge, "field",
                           info->edge->field_path ? info->edge->field_path : "unknown");
    if (info->edge->index > 0) {
        yyjson_mut_obj_add_uint(doc, edge, "index", info->edge->index);
    }

    if (info->peer) {
        if (info->peer_class_name) {
            yyjson_mut_obj_add_str(doc, edge,
                info->is_incoming ? "source_class" : "target_class",
                info->peer_class_name);
        }
        if (info->peer_name && info->peer_name[0]) {
            nmo_cli_json_add_str_safe(doc, edge,
                info->is_incoming ? "source_name" : "target_name",
                info->peer_name);
        }
    } else if (!info->is_incoming) {
        yyjson_mut_obj_add_bool(doc, edge, "broken", true);
    }

    yyjson_mut_arr_add_val(
        info->is_incoming ? d->incoming : d->outgoing, edge);
    return 0;
}

/** Visitor data for text ref output */
typedef struct {
    nmo_cli_table_t *out_table;
    nmo_cli_table_t *in_table;
} cli_refs_text_data_t;

static int cli_refs_text_visitor(const nmo_core_ref_info_t *info,
                                 const nmo_cmd_ctx_t *c, void *user) {
    (void)c;
    cli_refs_text_data_t *d = (cli_refs_text_data_t *)user;

    nmo_object_id_t peer_id = info->is_incoming ? info->edge->from : info->edge->to;
    char id_buf[16];
    snprintf(id_buf, sizeof(id_buf), "%u", peer_id);

    char field_buf[32];
    const char *field_name = info->edge->field_path ? info->edge->field_path : "unknown";
    if (info->edge->index > 0) {
        snprintf(field_buf, sizeof(field_buf), "%s[%u]",
                 field_name, info->edge->index);
    } else {
        snprintf(field_buf, sizeof(field_buf), "%s", field_name);
    }

    const char *peer_class = "-";
    const char *peer_name = "-";

    if (info->peer) {
        if (info->peer_class_name) peer_class = info->peer_class_name;
        if (info->peer_name && info->peer_name[0]) peer_name = info->peer_name;
    } else if (!info->is_incoming) {
        peer_name = "(BROKEN)";
    }

    const char *cells[] = {
        id_buf,
        nmo_ref_kind_name(info->edge->kind),
        field_buf,
        peer_class,
        peer_name
    };

    nmo_cli_table_t *table = info->is_incoming ? d->in_table : d->out_table;
    nmo_cli_table_add_row(table, cells, 5);
    return 0;
}

int nmo_cmd_object_refs(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_opt_val_t vals[1]; /* no named options currently */
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos };
    /* Parse with empty option table to collect positional args */
    if (nmo_opt_parse(argc, argv, NULL, 0, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    /* Find the object ID among positional args (first numeric value) */
    nmo_object_id_t object_id = 0;
    for (size_t i = 0; i < r.pos_count; ++i) {
        char *endptr = NULL;
        unsigned long id = strtoul(r.pos_args[i], &endptr, 10);
        if (endptr && *endptr == '\0' && id > 0 && id <= UINT32_MAX) {
            object_id = (nmo_object_id_t)id;
            break;
        }
    }
    if (object_id == 0) {
        fprintf(stderr, "Error: No valid object ID specified\n");
        fprintf(stderr, "Usage: nmo object refs <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Find the object */
    nmo_object_t *obj = nmo_core_find_by_id(&c, object_id);
    if (!obj) {
        fprintf(stderr, "Error: Object %u not found\n", object_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        /* Object info */
        yyjson_mut_obj_add_uint(doc, data, "id", object_id);
        const char *class_name = nmo_core_class_name(&c, nmo_object_get_class_id(obj));
        if (class_name) {
            yyjson_mut_obj_add_str(doc, data, "class_name", class_name);
        }
        const char *name = nmo_object_get_name(obj);
        if (name && name[0]) {
            nmo_cli_json_add_str_safe(doc, data, "name", name);
        }

        cli_refs_json_data_t jd = {
            .doc = doc,
            .outgoing = yyjson_mut_arr(doc),
            .incoming = yyjson_mut_arr(doc),
        };

        nmo_core_ref_result_t ref_result = {0};
        nmo_core_iter_refs(&c, object_id, NMO_CORE_REFS_BOTH,
                           cli_refs_json_visitor, &jd, &ref_result);

        yyjson_mut_obj_add_val(doc, data, "outgoing", jd.outgoing);
        yyjson_mut_obj_add_uint(doc, data, "outgoing_count",
                                (uint64_t)ref_result.outgoing);
        yyjson_mut_obj_add_val(doc, data, "incoming", jd.incoming);
        yyjson_mut_obj_add_uint(doc, data, "incoming_count",
                                (uint64_t)ref_result.incoming);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.refs");
    } else {
        /* Text output */
        const char *obj_name = nmo_object_get_name(obj);
        const char *obj_class = nmo_core_class_name(&c, nmo_object_get_class_id(obj));
        fprintf(c.out, "References for object %u: %s [%s]\n\n",
                object_id,
                (obj_name && obj_name[0]) ? obj_name : "(unnamed)",
                obj_class ? obj_class : "?");

        static const nmo_cli_table_col_t out_cols[] = {
            {"Target", NMO_CLI_ALIGN_RIGHT, 8, 0},
            {"Kind", NMO_CLI_ALIGN_LEFT, 15, 0},
            {"Field", NMO_CLI_ALIGN_LEFT, 20, 0},
            {"Target Class", NMO_CLI_ALIGN_LEFT, 18, 0},
            {"Target Name", NMO_CLI_ALIGN_LEFT, 25, 0},
        };
        static const nmo_cli_table_col_t in_cols[] = {
            {"Source", NMO_CLI_ALIGN_RIGHT, 8, 0},
            {"Kind", NMO_CLI_ALIGN_LEFT, 15, 0},
            {"Field", NMO_CLI_ALIGN_LEFT, 20, 0},
            {"Source Class", NMO_CLI_ALIGN_LEFT, 18, 0},
            {"Source Name", NMO_CLI_ALIGN_LEFT, 25, 0},
        };

        nmo_cli_table_t out_table;
        nmo_cli_table_init(&out_table, out_cols,
                           sizeof(out_cols) / sizeof(out_cols[0]));
        nmo_cli_table_t in_table;
        nmo_cli_table_init(&in_table, in_cols,
                           sizeof(in_cols) / sizeof(in_cols[0]));

        cli_refs_text_data_t td = {
            .out_table = &out_table,
            .in_table = &in_table,
        };

        nmo_core_ref_result_t ref_result = {0};
        nmo_core_iter_refs(&c, object_id, NMO_CORE_REFS_BOTH,
                           cli_refs_text_visitor, &td, &ref_result);

        /* Outgoing references */
        fprintf(c.out, "Outgoing references (%zu):\n", ref_result.outgoing);
        if (ref_result.outgoing == 0) {
            fprintf(c.out, "  (none)\n");
        } else {
            nmo_cli_table_print(&out_table, c.out, c.colorize);
        }
        nmo_cli_table_free(&out_table);

        fprintf(c.out, "\n");

        /* Incoming references */
        fprintf(c.out, "Incoming references (%zu):\n", ref_result.incoming);
        if (ref_result.incoming == 0) {
            fprintf(c.out, "  (none)\n");
        } else {
            nmo_cli_table_print(&in_table, c.out, c.colorize);
        }
        nmo_cli_table_free(&in_table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

