/**
 * @file nmo_cmd_object.c
 * @brief CLI object command group implementation
 */

#include "nmo_cmd_object.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_sort.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"
#include "app/nmo_object_summary.h"

#include "nmo.h"
#include "session/nmo_context.h"
#include "app/nmo_object_hierarchy.h"
#include "app/nmo_save.h"
#include "core/nmo_arena.h"
#include "dsl/nmo_dsl.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"

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
    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    uint64_t size = chunk ? (uint64_t)nmo_chunk_get_data_size(chunk) : 0;
    yyjson_mut_obj_add_uint(d->doc, item, "size", size);
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
    char size_buf[32];
    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    snprintf(size_buf, sizeof(size_buf), "%zu", chunk ? nmo_chunk_get_data_size(chunk) : (size_t)0);
    const char *cells[] = { id_buf, class_name ? class_name : "-", size_buf, (name && name[0]) ? name : "-" };
    nmo_cli_table_add_row(d->table, cells, 4);
    return 0;
}

/* ============================================================================
 * object list - sort infrastructure
 * ============================================================================ */

/** Dynamic array for collecting objects */
typedef struct {
    nmo_object_t **objects;
    size_t count;
    size_t capacity;
} obj_collect_t;

static int obj_collect_visitor(size_t index, nmo_object_t *obj,
                               const nmo_cmd_ctx_t *c, void *user) {
    (void)index;
    (void)c;
    obj_collect_t *col = (obj_collect_t *)user;
    if (col->count >= col->capacity) {
        size_t new_cap = col->capacity ? col->capacity * 2 : 64;
        nmo_object_t **tmp = (nmo_object_t **)realloc(col->objects, new_cap * sizeof(*tmp));
        if (!tmp) return -1;
        col->objects = tmp;
        col->capacity = new_cap;
    }
    col->objects[col->count++] = obj;
    return 0;
}

/** File-static sort context for qsort comparators */
static const nmo_cmd_ctx_t *s_sort_ctx;
static bool s_sort_reverse;

static size_t obj_chunk_size(nmo_object_t *obj) {
    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    return chunk ? nmo_chunk_get_data_size(chunk) : 0;
}

typedef int (*obj_compare_fn)(const void *, const void *);

static int compare_obj_id(const void *a, const void *b) {
    nmo_object_t *oa = *(nmo_object_t *const *)a;
    nmo_object_t *ob = *(nmo_object_t *const *)b;
    uint32_t ia = nmo_object_get_id(oa);
    uint32_t ib = nmo_object_get_id(ob);
    int cmp = (ia > ib) - (ia < ib);
    return s_sort_reverse ? -cmp : cmp;
}

static int compare_obj_name(const void *a, const void *b) {
    nmo_object_t *oa = *(nmo_object_t *const *)a;
    nmo_object_t *ob = *(nmo_object_t *const *)b;
    const char *na = nmo_object_get_name(oa);
    const char *nb = nmo_object_get_name(ob);
    if (!na) na = "";
    if (!nb) nb = "";
    int cmp = strcmp(na, nb);
    return s_sort_reverse ? -cmp : cmp;
}

static int compare_obj_class(const void *a, const void *b) {
    nmo_object_t *oa = *(nmo_object_t *const *)a;
    nmo_object_t *ob = *(nmo_object_t *const *)b;
    const char *ca = nmo_core_class_name(s_sort_ctx, nmo_object_get_class_id(oa));
    const char *cb = nmo_core_class_name(s_sort_ctx, nmo_object_get_class_id(ob));
    if (!ca) ca = "";
    if (!cb) cb = "";
    int cmp = strcmp(ca, cb);
    return s_sort_reverse ? -cmp : cmp;
}

static int compare_obj_size(const void *a, const void *b) {
    nmo_object_t *oa = *(nmo_object_t *const *)a;
    nmo_object_t *ob = *(nmo_object_t *const *)b;
    size_t sa = obj_chunk_size(oa);
    size_t sb = obj_chunk_size(ob);
    int cmp = (sa > sb) - (sa < sb);
    return s_sort_reverse ? -cmp : cmp;
}

static obj_compare_fn obj_sort_comparator(nmo_cli_sort_key_t key) {
    switch (key) {
        case NMO_CLI_SORT_ID:    return compare_obj_id;
        case NMO_CLI_SORT_NAME:  return compare_obj_name;
        case NMO_CLI_SORT_CLASS: return compare_obj_class;
        case NMO_CLI_SORT_SIZE:  return compare_obj_size;
        default:                 return NULL;
    }
}

/* ============================================================================
 * object list
 * ============================================================================ */

int nmo_cmd_object_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--class",   "-c", NMO_OPT_STRING, "Filter by class name"},
        {"--filter",  "-f", NMO_OPT_STRING, "Filter by DSL expression"},
        {"--sort",    "-s",  NMO_OPT_STRING, "Sort by: id, name, class, size"},
        {"--reverse", "-r",  NMO_OPT_FLAG,   "Reverse sort direction"},
        {"--top",     NULL,  NMO_OPT_UINT,   "Show only first N results"},
    };
    nmo_opt_val_t vals[5];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 5, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *class_filter_str = vals[0].present ? vals[0].val.str : NULL;
    const char *filter_expr      = vals[1].present ? vals[1].val.str : NULL;
    const char *sort_key_str     = vals[2].present ? vals[2].val.str : NULL;
    bool reverse                 = vals[3].present && vals[3].val.flag;
    uint32_t top_n               = vals[4].present ? vals[4].val.u : 0;

    /* Validate sort key early */
    nmo_cli_sort_key_t sort_key = nmo_cli_parse_sort_key(sort_key_str);
    if (sort_key_str && sort_key == NMO_CLI_SORT_NONE) {
        fprintf(stderr, "Error: Invalid sort key '%s' (use: id, name, class, size)\n", sort_key_str);
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
    if (filter_expr) {
        nmo_dsl_compile_options_t compile_opts = { .mode = NMO_DSL_MODE_EXPRESSION };
        nmo_status_t st = nmo_dsl_compile(c.registry, NULL, filter_expr, &compile_opts, &filter.dsl_filter);
        if (st != NMO_OK) {
            fprintf(stderr, "Error: Failed to compile filter expression: %s\n", filter_expr);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    bool needs_collect = (sort_key != NMO_CLI_SORT_NONE) || (top_n > 0);
    nmo_core_iter_result_t result;

    if (needs_collect) {
        /* Path A: collect → sort → truncate → output */
        obj_collect_t col = {0};
        nmo_core_iter_objects(&c, &filter, obj_collect_visitor, &col, &result);

        /* Sort if requested */
        if (sort_key != NMO_CLI_SORT_NONE && col.count > 1) {
            s_sort_ctx = &c;
            s_sort_reverse = reverse;
            obj_compare_fn cmp = obj_sort_comparator(sort_key);
            if (cmp) {
                qsort(col.objects, col.count, sizeof(nmo_object_t *), cmp);
            }
        }

        /* Compute output count */
        size_t output_count = col.count;
        if (top_n > 0 && (size_t)top_n < output_count) {
            output_count = (size_t)top_n;
        }

        if (c.is_json) {
            yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
            yyjson_mut_val *data = yyjson_mut_obj(doc);
            yyjson_mut_val *arr = yyjson_mut_arr(doc);

            for (size_t i = 0; i < output_count; ++i) {
                list_json_data_t jd = { .doc = doc, .arr = arr, .ctx = &c };
                list_json_visitor(i, col.objects[i], &c, &jd);
            }

            yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)output_count);
            yyjson_mut_obj_add_val(doc, data, "objects", arr);

            nmo_cmd_ctx_json_end(&c, doc, data, "object.list");
        } else {
            /* Table output */
            static const nmo_cli_table_col_t columns[] = {
                {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
                {"CLASS", NMO_CLI_ALIGN_LEFT, 20, 30},
                {"SIZE", NMO_CLI_ALIGN_RIGHT, 10, 0},
                {"NAME", NMO_CLI_ALIGN_LEFT, 20, 50},
            };

            nmo_cli_table_t table;
            nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

            for (size_t i = 0; i < output_count; ++i) {
                list_table_data_t td = { .table = &table, .ctx = &c };
                list_table_visitor(i, col.objects[i], &c, &td);
            }

            fprintf(c.out, "Objects: %zu", result.matched);
            if (class_filter_str) {
                fprintf(c.out, " (filtered by class: %s)", class_filter_str);
            }
            if (filter_expr) {
                fprintf(c.out, " (filtered by: %s)", filter_expr);
            }
            if (top_n > 0) {
                fprintf(c.out, " (showing top %u)", top_n);
            }
            fprintf(c.out, "\n\n");

            nmo_cli_table_print(&table, c.out, c.colorize);
            nmo_cli_table_free(&table);
        }

        free(col.objects);
    } else {
        /* Path B: streaming (no sort, no top) */
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
                {"CLASS", NMO_CLI_ALIGN_LEFT, 20, 30},
                {"SIZE", NMO_CLI_ALIGN_RIGHT, 10, 0},
                {"NAME", NMO_CLI_ALIGN_LEFT, 20, 50},
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
    /* Parse: nmo object show [--select <path>]... [--expr <expr>]... [--depth N] [--full] <id> <file>
     *
     * Two-pass approach: first collect repeatable --select/--expr and build
     * a cleaned argv, then use nmo_opt for --depth and --full.
     */
    const char *select_paths[64];
    size_t select_path_count = 0;

    const char *exprs[64];
    size_t expr_count = 0;

    /* Pass 1: collect --select/--expr, build cleaned argv */
    char **clean_argv = (char **)malloc((size_t)argc * sizeof(char *));
    if (!clean_argv) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    int clean_argc = 0;
    clean_argv[clean_argc++] = argv[0]; /* action name */

    for (int i = 1; i < argc; ++i) {
        if ((strcmp(argv[i], "--select") == 0 || strcmp(argv[i], "-s") == 0)) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: Missing argument for %s\n", argv[i]);
                fprintf(stderr,
                        "Usage: nmo object show [--select <path>]... [--expr <expr>]... <id> <file>\n");
                free(clean_argv);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            if (select_path_count < (sizeof(select_paths) / sizeof(select_paths[0]))) {
                select_paths[select_path_count++] = argv[i + 1];
            } else {
                fprintf(stderr, "Warning: --select limit reached (64 max), extra paths ignored\n");
            }
            i++; /* skip value */
            continue;
        }

        if ((strcmp(argv[i], "--expr") == 0 || strcmp(argv[i], "-e") == 0)) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: Missing argument for %s\n", argv[i]);
                fprintf(stderr,
                        "Usage: nmo object show [--select <path>]... [--expr <expr>]... <id> <file>\n");
                free(clean_argv);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            if (expr_count < (sizeof(exprs) / sizeof(exprs[0]))) {
                exprs[expr_count++] = argv[i + 1];
            } else {
                fprintf(stderr, "Warning: --expr limit reached (64 max), extra expressions ignored\n");
            }
            i++; /* skip value */
            continue;
        }

        clean_argv[clean_argc++] = argv[i];
    }

    /* Pass 2: nmo_opt for --depth and --full on cleaned argv */
    static const nmo_opt_def_t opts[] = {
        {"--depth", "-d", NMO_OPT_UINT, "Recursion depth (default: unlimited)"},
        {"--full",  NULL, NMO_OPT_FLAG, "Full detail mode"},
    };
    enum { OPT_DEPTH, OPT_FULL, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(clean_argc, clean_argv, opts, OPT_COUNT, &r) < 0) {
        free(clean_argv);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    free(clean_argv);

    int depth = vals[OPT_DEPTH].present ? (int)vals[OPT_DEPTH].val.u : -1;
    bool full_mode = vals[OPT_FULL].present && vals[OPT_FULL].val.flag;

    const char *id_str = r.pos_count > 0 ? r.pos_args[0] : NULL;
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
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
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
            yyjson_mut_obj_add_str(doc, query, "class_name", class_filter_str);
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
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
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

/* ============================================================================
 * object rename - Rename an object and save to new file
 *
 * Two modes:
 *   Single:  nmo object rename <id> <new_name> <file> -o <output>
 *   Batch:   nmo object rename --name <pattern> --to <template> [opts] <file> -o <output>
 * ============================================================================ */

/** Rename entry used by both batch text and JSON output */
typedef struct {
    nmo_object_id_t id;
    nmo_class_id_t class_id;
    char old_name[256];
    char new_name[256];
    bool collision;
} rename_entry_t;

/**
 * Open a manual session context (shared by single and batch paths).
 * Returns 0 on success, NMO_CLI_EXIT_* on error.
 */
static int rename_open_ctx(nmo_cmd_ctx_t *c, const char *file_path,
                           const nmo_cli_global_opts_t *global)
{
    memset(c, 0, sizeof(*c));
    c->global = global;
    c->is_json = (global->format == NMO_CLI_FORMAT_JSON ||
                  global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    c->file_path = file_path;

    char errbuf[256];
    if (!nmo_tool_open_session(file_path, &c->ctx, &c->session,
                               errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    c->registry = nmo_context_get_type_registry(c->ctx);

    char out_err[128];
    c->out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!c->out) {
        nmo_tool_close_session(c->ctx, c->session);
        c->ctx = NULL;
        c->session = NULL;
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    c->colorize = nmo_cli_should_colorize(global, c->out);
    return 0;
}

/**
 * Batch rename mode.
 *
 * Options already parsed by caller:
 *   name_pattern, to_template, use_regex, class_filter, dry_run, output_path
 *   file_path (last positional arg)
 */
static int nmo_cmd_object_rename_batch(
    const char *name_pattern,
    const char *to_template,
    bool use_regex,
    const char *class_filter,
    bool dry_run,
    const char *output_path,
    const char *file_path,
    const nmo_cli_global_opts_t *global)
{
    /* Validate required args */
    if (!to_template) {
        fprintf(stderr, "Error: --to is required for batch rename\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (!file_path) {
        fprintf(stderr, "Error: No input file specified\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_cmd_ctx_t c;
    int rc = rename_open_ctx(&c, file_path, global);
    if (rc) return rc;

    /* Resolve class filter */
    nmo_class_id_t filter_cid = 0;
    if (class_filter) {
        filter_cid = nmo_core_class_id(&c, class_filter);
        if (!filter_cid) {
            fprintf(stderr, "Error: Unknown class '%s'\n", class_filter);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    /* Get all objects */
    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    size_t obj_count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &obj_count);

    /* Collect rename entries */
    rename_entry_t *entries = NULL;
    size_t entry_count = 0;
    size_t entry_cap = 0;
    size_t collision_count = 0;

    for (size_t i = 0; i < obj_count; i++) {
        nmo_object_t *obj = objects[i];
        const char *name = nmo_object_get_name(obj);
        if (!name || !name[0]) continue;

        /* Class filter */
        if (filter_cid) {
            nmo_class_id_t ocid = nmo_object_get_class_id(obj);
            if (ocid != filter_cid &&
                !nmo_core_class_derives(&c, ocid, filter_cid)) {
                continue;
            }
        }

        /* Pattern match + template application */
        char new_name_buf[256];
        if (use_regex) {
            /* Regex mode: match via lightweight regex, full name as {0} */
            if (!nmo_core_regex_match(name, name_pattern, true))
                continue;

            char no_captures[1][256];
            if (nmo_tool_apply_rename_template(to_template, name,
                                               no_captures, 0,
                                               new_name_buf,
                                               sizeof(new_name_buf)) < 0) {
                fprintf(stderr, "Warning: Template expansion failed for '%s'\n",
                        name);
                continue;
            }
        } else {
            /* Glob mode */
            char captures[16][256];
            size_t cap_count = 0;
            if (!nmo_tool_wildcard_capture_ci(name_pattern, name,
                                              captures, 16, &cap_count)) {
                continue;
            }
            if (nmo_tool_apply_rename_template(to_template, name,
                                               captures, cap_count,
                                               new_name_buf,
                                               sizeof(new_name_buf)) < 0) {
                fprintf(stderr, "Warning: Template expansion failed for '%s'\n",
                        name);
                continue;
            }
        }

        /* Skip if name unchanged */
        if (strcmp(name, new_name_buf) == 0) continue;

        /* Grow entries array */
        if (entry_count >= entry_cap) {
            size_t new_cap = entry_cap ? entry_cap * 2 : 32;
            rename_entry_t *tmp = (rename_entry_t *)realloc(
                entries, new_cap * sizeof(rename_entry_t));
            if (!tmp) {
                fprintf(stderr, "Error: Out of memory\n");
                free(entries);
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
            }
            entries = tmp;
            entry_cap = new_cap;
        }

        rename_entry_t *e = &entries[entry_count++];
        e->id = nmo_object_get_id(obj);
        e->class_id = nmo_object_get_class_id(obj);
        snprintf(e->old_name, sizeof(e->old_name), "%s", name);
        snprintf(e->new_name, sizeof(e->new_name), "%s", new_name_buf);

        /* Collision check */
        nmo_object_t *existing = nmo_object_repository_find_by_name(
            repo, new_name_buf);
        e->collision = (existing &&
                        nmo_object_get_id(existing) != e->id);
        if (e->collision) collision_count++;
    }

    /* Perform renames (unless dry-run) */
    size_t rename_errors = 0;
    if (!dry_run) {
        for (size_t i = 0; i < entry_count; i++) {
            int rrc = nmo_object_repository_rename(
                repo, entries[i].id, entries[i].new_name);
            if (rrc != NMO_OK) {
                fprintf(stderr, "Error: Failed to rename object %u: %s\n",
                        entries[i].id, nmo_error_string(rrc));
                rename_errors++;
            }
        }

        /* Save file (only if at least one rename succeeded) */
        size_t succeeded = entry_count - rename_errors;
        if (succeeded > 0) {
            nmo_save_options_t save_opts = nmo_save_options_default();
            int save_rc = nmo_save_file(c.session, output_path, &save_opts);
            if (save_rc != NMO_OK) {
                fprintf(stderr, "Error saving file: %s\n",
                        nmo_error_string(save_rc));
                free(entries);
                return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
            }
        }

        if (rename_errors > 0) {
            fprintf(stderr, "Warning: %zu rename(s) failed\n", rename_errors);
        }
    }

    /* Output results */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc) {
            free(entries);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_bool_safe(doc, data, "dry_run", dry_run);
        nmo_cli_json_add_uint_safe(doc, data, "match_count",
                                   (uint64_t)entry_count);
        nmo_cli_json_add_uint_safe(doc, data, "collision_count",
                                   (uint64_t)collision_count);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < entry_count; i++) {
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            nmo_cli_json_add_uint_safe(doc, item, "id",
                                       (uint64_t)entries[i].id);
            const char *cls = nmo_core_class_name(&c, entries[i].class_id);
            nmo_cli_json_add_str_safe(doc, item, "class_name",
                                      cls ? cls : "?");
            nmo_cli_json_add_str_safe(doc, item, "old_name",
                                      entries[i].old_name);
            nmo_cli_json_add_str_safe(doc, item, "new_name",
                                      entries[i].new_name);
            if (entries[i].collision) {
                nmo_cli_json_add_bool_safe(doc, item, "collision", true);
            }
            yyjson_mut_arr_add_val(arr, item);
        }
        yyjson_mut_obj_add_val(doc, data, "renames", arr);

        if (!dry_run && output_path) {
            nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "object.rename");
    } else {
        if (dry_run) {
            fprintf(c.out, "=== Dry Run: Batch Rename ===\n\n");

            if (entry_count > 0) {
                static const nmo_cli_table_col_t cols[] = {
                    {"ID",       NMO_CLI_ALIGN_RIGHT, 5,  0},
                    {"CLASS",    NMO_CLI_ALIGN_LEFT,  15, 25},
                    {"OLD NAME", NMO_CLI_ALIGN_LEFT,  20, 40},
                    {"NEW NAME", NMO_CLI_ALIGN_LEFT,  20, 40},
                };
                nmo_cli_table_t table;
                nmo_cli_table_init(&table, cols, 4);

                for (size_t i = 0; i < entry_count; i++) {
                    char id_buf[16];
                    snprintf(id_buf, sizeof(id_buf), "%u", entries[i].id);
                    const char *cls = nmo_core_class_name(&c,
                                                          entries[i].class_id);
                    const char *cells[] = {
                        id_buf,
                        cls ? cls : "?",
                        entries[i].old_name,
                        entries[i].new_name
                    };
                    nmo_cli_table_add_row(&table, cells, 4);
                }

                nmo_cli_table_print(&table, c.out, c.colorize);
                nmo_cli_table_free(&table);
                fprintf(c.out, "\n");
            }

            fprintf(c.out, "%zu object(s) would be renamed, %zu collisions\n",
                    entry_count, collision_count);
        } else {
            fprintf(c.out, "%zu object(s) renamed, %zu collisions\n",
                    entry_count, collision_count);
            if (entry_count > 0 && output_path) {
                fprintf(c.out, "Saved to: %s\n", output_path);
            }
        }

        /* Print collision warnings */
        for (size_t i = 0; i < entry_count; i++) {
            if (entries[i].collision) {
                fprintf(stderr, "Warning: Name '%s' collides with existing object\n",
                        entries[i].new_name);
            }
        }
    }

    free(entries);
    int exit_code = (rename_errors > 0) ? NMO_CLI_EXIT_INTERNAL_ERROR : NMO_CLI_EXIT_SUCCESS;
    return nmo_cmd_ctx_done(&c, exit_code);
}

/* ============================================================================
 * object export - Semantic JSON export
 * ============================================================================ */

int nmo_cmd_object_export(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--class",  "-c", NMO_OPT_STRING, "Filter by class name"},
        {"--name",   "-n", NMO_OPT_STRING, "Filter by name pattern"},
        {"--filter", "-f", NMO_OPT_STRING, "Filter by DSL expression"},
        {"--depth",  "-d", NMO_OPT_UINT,   "Recursion depth (default: 4)"},
        {"--full",   NULL, NMO_OPT_FLAG,   "Full detail mode (depth 8, more array elements)"},
        {"--id",     NULL, NMO_OPT_UINT,   "Export specific object by ID"},
    };
    enum { OPT_CLASS, OPT_NAME, OPT_FILTER, OPT_DEPTH, OPT_FULL, OPT_ID, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *class_filter_str = vals[OPT_CLASS].present ? vals[OPT_CLASS].val.str : NULL;
    const char *name_pattern     = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL;
    const char *filter_expr      = vals[OPT_FILTER].present ? vals[OPT_FILTER].val.str : NULL;
    int depth                    = vals[OPT_DEPTH].present ? (int)vals[OPT_DEPTH].val.u : -1;
    bool full_mode               = vals[OPT_FULL].present && vals[OPT_FULL].val.flag;
    uint32_t id_filter           = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0;

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
    if (name_pattern) {
        filter.name_pattern = name_pattern;
    }
    if (filter_expr) {
        nmo_dsl_compile_options_t compile_opts = { .mode = NMO_DSL_MODE_EXPRESSION };
        nmo_status_t st = nmo_dsl_compile(c.registry, NULL, filter_expr, &compile_opts, &filter.dsl_filter);
        if (st != NMO_OK) {
            fprintf(stderr, "Error: Failed to compile filter expression: %s\n", filter_expr);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }
    if (id_filter) {
        filter.object_id = id_filter;
    }

    /* Collect matching objects */
    obj_collect_t col = {0};
    nmo_core_iter_result_t iter_result;
    nmo_core_iter_objects(&c, &filter, obj_collect_visitor, &col, &iter_result);

    /* Summary config */
    nmo_summary_config_t cfg = nmo_summary_config_default();
    cfg.resolve_object_refs = true;
    cfg.format_enum_names = true;
    cfg.format_flags_names = true;
    if (full_mode) {
        cfg.max_depth = 8;
        cfg.array_preview_max = 64;
        cfg.text_preview_max = 64;
    }
    if (depth >= 0) {
        cfg.max_depth = (uint32_t)depth;
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc) {
            free(col.objects);
            if (filter.dsl_filter) nmo_dsl_program_destroy(filter.dsl_filter);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *objects_arr = yyjson_mut_arr(doc);

        for (size_t i = 0; i < col.count; i++) {
            nmo_object_t *obj = col.objects[i];
            yyjson_mut_val *obj_json = yyjson_mut_obj(doc);

            /* Basic metadata */
            yyjson_mut_obj_add_uint(doc, obj_json, "id", nmo_object_get_id(obj));
            nmo_class_id_t cid = nmo_object_get_class_id(obj);
            yyjson_mut_obj_add_uint(doc, obj_json, "class_id", cid);
            const char *cn = nmo_core_class_name(&c, cid);
            if (cn) yyjson_mut_obj_add_str(doc, obj_json, "class_name", cn);
            const char *oname = nmo_object_get_name(obj);
            if (oname && oname[0]) nmo_cli_json_add_str_safe(doc, obj_json, "name", oname);

            /* Semantic summary via reflection */
            yyjson_mut_val *fields = yyjson_mut_obj(doc);
            nmo_summary_output_t sum_out = {
                .stream = c.out,
                .json_doc = doc,
                .json_data = fields,
                .is_json = true,
                .colorize = false,
                .ctx = c.ctx,
                .session = c.session,
            };
            if (nmo_object_summary_with_config(obj, &sum_out, &cfg)) {
                yyjson_mut_obj_add_val(doc, obj_json, "fields", fields);
            }

            yyjson_mut_arr_add_val(objects_arr, obj_json);
        }

        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)col.count);
        yyjson_mut_obj_add_val(doc, data, "objects", objects_arr);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.export");
    } else {
        /* Text output: structured summary for each object */
        for (size_t i = 0; i < col.count; i++) {
            nmo_object_t *obj = col.objects[i];
            nmo_object_id_t oid = nmo_object_get_id(obj);
            nmo_class_id_t cid = nmo_object_get_class_id(obj);
            const char *cn = nmo_core_class_name(&c, cid);
            const char *oname = nmo_object_get_name(obj);

            if (i > 0) fprintf(c.out, "\n");

            char heading[256];
            snprintf(heading, sizeof(heading), "[%zu/%zu] #%u %s (%s)",
                     i + 1, col.count, oid,
                     (oname && oname[0]) ? oname : "(unnamed)",
                     cn ? cn : "?");
            nmo_cli_print_heading(c.out, heading, c.colorize);

            nmo_summary_output_t sum_out = {
                .stream = c.out,
                .json_doc = NULL,
                .json_data = NULL,
                .is_json = false,
                .colorize = c.colorize,
                .ctx = c.ctx,
                .session = c.session,
            };
            (void)nmo_object_summary_with_config(obj, &sum_out, &cfg);
        }

        if (col.count == 0) {
            fprintf(c.out, "No objects matched.\n");
        } else {
            fprintf(c.out, "\n%zu object(s) exported.\n", col.count);
        }
    }

    free(col.objects);
    if (filter.dsl_filter) {
        nmo_dsl_program_destroy(filter.dsl_filter);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object rename - Rename an object and save to new file
 *
 * Two modes:
 *   Single:  nmo object rename <id> <new_name> <file> -o <output>
 *   Batch:   nmo object rename --name <pattern> --to <template> [opts] <file> -o <output>
 * ============================================================================ */

int nmo_cmd_object_rename(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Unified option parsing for both single and batch modes */
    static const nmo_opt_def_t opts[] = {
        {"--name",    "-n", NMO_OPT_STRING, "Glob/regex pattern to match object names (batch mode)"},
        {"--to",      NULL, NMO_OPT_STRING, "Rename template with {0},{1}..{N} placeholders"},
        {"--regex",   NULL, NMO_OPT_FLAG,   "Treat --name as POSIX regex instead of glob"},
        {"--class",   "-c", NMO_OPT_STRING, "Restrict to objects of this class"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview renames without saving"},
        {"--output",  "-o", NMO_OPT_STRING, "Output file path"},
    };
    enum { OPT_NAME, OPT_TO, OPT_REGEX, OPT_CLASS, OPT_DRY_RUN, OPT_OUTPUT, OPT_COUNT };

    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    /* Batch mode: --name is present */
    if (vals[OPT_NAME].present) {
        const char *file_path = r.pos_count > 0
            ? r.pos_args[r.pos_count - 1] : NULL;
        return nmo_cmd_object_rename_batch(
            vals[OPT_NAME].val.str,
            vals[OPT_TO].present ? vals[OPT_TO].val.str : NULL,
            vals[OPT_REGEX].present && vals[OPT_REGEX].val.flag,
            vals[OPT_CLASS].present ? vals[OPT_CLASS].val.str : NULL,
            vals[OPT_DRY_RUN].present && vals[OPT_DRY_RUN].val.flag,
            vals[OPT_OUTPUT].present ? vals[OPT_OUTPUT].val.str : NULL,
            file_path,
            global);
    }

    /* Single mode: <id> <new_name> <file> -o <output> */
    const char *output_path = vals[OPT_OUTPUT].present
        ? vals[OPT_OUTPUT].val.str : NULL;
    if (!output_path) {
        fprintf(stderr, "Error: Output file not specified (use -o or --output)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (r.pos_count < 3) {
        fprintf(stderr, "Error: Expected <id> <new_name> <file>\n");
        fprintf(stderr, "Usage: nmo object rename <id> <new_name> <file> -o <output>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *id_str = r.pos_args[0];
    const char *new_name = r.pos_args[1];
    const char *file_path = r.pos_args[r.pos_count - 1];

    /* Parse object ID */
    uint32_t object_id;
    if (!nmo_tool_parse_u32(id_str, &object_id)) {
        fprintf(stderr, "Error: Invalid object ID '%s'\n", id_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session manually (nmo_cmd_ctx_init would pick -o value as file) */
    nmo_cmd_ctx_t c;
    int rc = rename_open_ctx(&c, file_path, global);
    if (rc) return rc;

    /* Get repository */
    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);

    /* Find object by ID */
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, object_id);
    if (!obj) {
        fprintf(stderr, "Error: Object %u not found\n", object_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Save old name */
    const char *old_name = nmo_object_get_name(obj);
    char old_name_buf[256];
    if (old_name && old_name[0]) {
        snprintf(old_name_buf, sizeof(old_name_buf), "%s", old_name);
    } else {
        old_name_buf[0] = '\0';
    }

    /* Check for name collision */
    bool name_collision = false;
    nmo_object_t *existing = nmo_object_repository_find_by_name(repo, new_name);
    if (existing && nmo_object_get_id(existing) != object_id) {
        name_collision = true;
        if (!c.is_json) {
            fprintf(stderr, "Warning: Name '%s' already used by object %u\n",
                    new_name, nmo_object_get_id(existing));
        }
    }

    /* Perform rename */
    int rename_rc = nmo_object_repository_rename(repo, object_id, new_name);
    if (rename_rc != NMO_OK) {
        fprintf(stderr, "Error: Failed to rename object %u: %s\n",
                object_id, nmo_error_string(rename_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Save file */
    nmo_save_options_t save_opts = nmo_save_options_default();
    int save_rc = nmo_save_file(c.session, output_path, &save_opts);
    if (save_rc != NMO_OK) {
        fprintf(stderr, "Error saving file: %s\n", nmo_error_string(save_rc));
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    /* Output results */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        if (!doc) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        yyjson_mut_val *data = yyjson_mut_obj(doc);
        nmo_cli_json_add_uint_safe(doc, data, "id", (uint64_t)object_id);
        nmo_cli_json_add_str_safe(doc, data, "old_name",
                                  old_name_buf[0] ? old_name_buf : "");
        nmo_cli_json_add_str_safe(doc, data, "new_name", new_name);
        nmo_cli_json_add_str_safe(doc, data, "output", output_path);
        nmo_cli_json_add_bool_safe(doc, data, "name_collision", name_collision);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.rename");
    } else {
        fprintf(c.out, "Renamed: %s -> %s (ID %u)\n",
                old_name_buf[0] ? old_name_buf : "(unnamed)", new_name, object_id);
        fprintf(c.out, "Saved to: %s\n", output_path);
        if (name_collision) {
            fprintf(c.out, "Warning: Name collision with existing object\n");
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

