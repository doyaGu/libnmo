/**
 * @file nmo_cmd_object.c
 * @brief CLI object command group implementation
 */

#include "nmo_cmd_object.h"
#include "nmo_cmd_object_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_sort.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"
#include "app/nmo_object_summary.h"

#include "nmo.h"
#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "app/nmo_object_hierarchy.h"
#include "core/nmo_arena.h"
#include "dsl/nmo_dsl.h"
#include "object/nmo_class_ids.h"
#include "object/nmo_object_repository.h"
#include "type/nmo_type_string.h"
#include "type/nmo_reflection.h"

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
    if (class_name) nmo_cli_json_add_str_safe(d->doc, item, "class_name", class_name);
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
 * object list (single-file core + batch support)
 * ============================================================================ */

/** User data forwarded through batch handler for object list */
typedef struct {
    const char *class_filter_str;
    const char *filter_expr;
    const char *sort_key_str;
    bool reverse;
    uint32_t top_n;
} object_list_opts_t;

static int object_list_single(const char *file_path,
                              const nmo_cli_global_opts_t *global,
                              void *user_data,
                              yyjson_mut_doc *doc,
                              yyjson_mut_val *data)
{
    /* In text mode the framework wraps user_data in nmo_tool_text_output_ctx_t;
       in JSON mode user_data is the raw pointer we passed to batch_run. */
    const nmo_tool_text_output_ctx_t *text_ctx = NULL;
    const object_list_opts_t *opts = NULL;
    if (doc && data) {
        opts = (const object_list_opts_t *)user_data;
    } else {
        text_ctx = (const nmo_tool_text_output_ctx_t *)user_data;
        opts = text_ctx ? (const object_list_opts_t *)text_ctx->user_data : NULL;
    }
    const char *class_filter_str = opts ? opts->class_filter_str : NULL;
    const char *filter_expr      = opts ? opts->filter_expr : NULL;
    const char *sort_key_str     = opts ? opts->sort_key_str : NULL;
    bool reverse                 = opts ? opts->reverse : false;
    uint32_t top_n               = opts ? opts->top_n : 0;

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Build a lightweight cmd_ctx for core helpers */
    nmo_cmd_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.global = global;
    c.ctx = ctx;
    c.session = session;
    c.registry = nmo_context_get_type_registry(ctx);
    c.is_json = (doc != NULL);
    c.file_path = file_path;
    c.out = (text_ctx && text_ctx->out) ? text_ctx->out : stdout;
    c.colorize = text_ctx ? text_ctx->colorize : false;
    int rc = NMO_CLI_EXIT_SUCCESS;

    /* Build query */
    nmo_object_query_t query = {0};
    nmo_core_query_dsl_t query_dsl = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = class_filter_str,
        .filter_expr = filter_expr,
        .include_derived_classes = true,
        .print_dsl_context = true,
    };
    rc = nmo_core_query_build(&c, &query, &query_dsl, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        nmo_tool_close_session(ctx, session);
        return rc;
    }

    nmo_cli_sort_key_t sort_key = nmo_cli_parse_sort_key(sort_key_str);
    bool needs_collect = (sort_key != NMO_CLI_SORT_NONE) || (top_n > 0);
    nmo_core_iter_result_t result = {0};

    if (needs_collect) {
        obj_collect_t col = {0};
        nmo_core_object_query_run(&c, &query, obj_collect_visitor, &col, &result);

        if (sort_key != NMO_CLI_SORT_NONE && col.count > 1) {
            s_sort_ctx = &c;
            s_sort_reverse = reverse;
            obj_compare_fn cmp = obj_sort_comparator(sort_key);
            if (cmp) {
                qsort(col.objects, col.count, sizeof(nmo_object_t *), cmp);
            }
        }

        size_t output_count = col.count;
        if (top_n > 0 && (size_t)top_n < output_count) {
            output_count = (size_t)top_n;
        }

        if (doc && data) {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            for (size_t i = 0; i < output_count; ++i) {
                list_json_data_t jd = { .doc = doc, .arr = arr, .ctx = &c };
                list_json_visitor(i, col.objects[i], &c, &jd);
            }
            yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)output_count);
            yyjson_mut_obj_add_val(doc, data, "objects", arr);
        } else {
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
            if (class_filter_str) fprintf(c.out, " (filtered by class: %s)", class_filter_str);
            if (filter_expr) fprintf(c.out, " (filtered by: %s)", filter_expr);
            if (top_n > 0) fprintf(c.out, " (showing top %u)", top_n);
            fprintf(c.out, "\n\n");
            nmo_cli_table_print(&table, c.out, c.colorize);
            nmo_cli_table_free(&table);
        }
        free(col.objects);
    } else {
        if (doc && data) {
            yyjson_mut_val *arr = yyjson_mut_arr(doc);
            list_json_data_t jd = { .doc = doc, .arr = arr, .ctx = &c };
            nmo_core_object_query_run(&c, &query, list_json_visitor, &jd, &result);
            yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)result.matched);
            yyjson_mut_obj_add_val(doc, data, "objects", arr);
        } else {
            static const nmo_cli_table_col_t columns[] = {
                {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
                {"CLASS", NMO_CLI_ALIGN_LEFT, 20, 30},
                {"SIZE", NMO_CLI_ALIGN_RIGHT, 10, 0},
                {"NAME", NMO_CLI_ALIGN_LEFT, 20, 50},
            };
            nmo_cli_table_t table;
            nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));
            list_table_data_t td = { .table = &table, .ctx = &c };
            nmo_core_object_query_run(&c, &query, list_table_visitor, &td, &result);
            fprintf(c.out, "Objects: %zu", result.matched);
            if (class_filter_str) fprintf(c.out, " (filtered by class: %s)", class_filter_str);
            if (filter_expr) fprintf(c.out, " (filtered by: %s)", filter_expr);
            fprintf(c.out, "\n\n");
            nmo_cli_table_print(&table, c.out, c.colorize);
            nmo_cli_table_free(&table);
        }
    }

    nmo_core_query_dsl_destroy(&query_dsl);
    nmo_tool_close_session(ctx, session);
    return rc;
}

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

    /* Batch mode */
    if (global->batch_mode) {
        static const char *const value_opts[] = {
            "--class", "-c", "--filter", "-f", "--sort", "-s", "--top",
        };
        const char *paths[256];
        size_t count = nmo_tool_find_file_args_ex(
            argc, argv, paths, 256, value_opts,
            sizeof(value_opts) / sizeof(value_opts[0]));

        if (count == 0) {
            fprintf(stderr, "Error: No files specified\n");
            fprintf(stderr, "Usage: nmo --batch object list [options] <file1> <file2> ...\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }

        object_list_opts_t opts = {
            .class_filter_str = class_filter_str,
            .filter_expr = filter_expr,
            .sort_key_str = sort_key_str,
            .reverse = reverse,
            .top_n = top_n,
        };
        return nmo_tool_batch_run(paths, count, global, "object.list",
                                  object_list_single, &opts);
    }

    /* Single file mode */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Build query */
    nmo_object_query_t query = {0};
    nmo_core_query_dsl_t query_dsl = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = class_filter_str,
        .filter_expr = filter_expr,
        .include_derived_classes = true,
        .print_dsl_context = true,
    };
    rc = nmo_core_query_build(&c, &query, &query_dsl, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, rc);
    }

    bool needs_collect = (sort_key != NMO_CLI_SORT_NONE) || (top_n > 0);
    nmo_core_iter_result_t result = {0};

    if (needs_collect) {
        /* Path A: collect -> sort -> truncate -> output */
        obj_collect_t col = {0};
        nmo_core_object_query_run(&c, &query, obj_collect_visitor, &col, &result);

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
            nmo_core_object_query_run(&c, &query, list_json_visitor, &jd, &result);

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
            nmo_core_object_query_run(&c, &query, list_table_visitor, &td, &result);

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

    nmo_core_query_dsl_destroy(&query_dsl);
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

    obj_collect_t col = {0};
    nmo_core_iter_result_t iter_result = {0};
    rc = nmo_core_object_query_run(&c, NULL, obj_collect_visitor, &col, &iter_result);
    if (rc != NMO_CLI_EXIT_SUCCESS || col.count < iter_result.matched) {
        free(col.objects);
        fprintf(stderr, "Error: Failed to collect objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    nmo_object_t **objects = col.objects;
    size_t object_count = col.count;

    nmo_object_hierarchy_t hierarchy;
    if (!nmo_object_hierarchy_build(c.ctx, c.session, &hierarchy)) {
        free(col.objects);
        fprintf(stderr, "Error: Failed to build object hierarchy\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    size_t map_size = hierarchy.map_size;
    nmo_object_id_t *parent_of = hierarchy.parent_of;

    nmo_cli_tree_node_t **node_map = (nmo_cli_tree_node_t **)calloc(map_size, sizeof(nmo_cli_tree_node_t *));
    if (!node_map) {
        nmo_object_hierarchy_free(&hierarchy);
        free(col.objects);
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
    free(col.objects);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object show
 * ============================================================================ */

typedef struct object_show_args {
    const char *select_paths[64];
    size_t select_path_count;
    const char *exprs[64];
    size_t expr_count;
    int depth;
    bool full_mode;
    bool has_id;
    uint32_t id;
    const char *positional_id;
    const char *name;
    uint32_t required_base_class;
    const char *type_label;
} object_show_args_t;

static int object_show_parse(int argc, char **argv, bool expect_file_operand,
                             object_show_args_t *args, const char *usage)
{
    memset(args, 0, sizeof(*args));
    args->depth = -1;

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
                fprintf(stderr, "Usage: %s\n", usage);
                free(clean_argv);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            if (args->select_path_count < (sizeof(args->select_paths) / sizeof(args->select_paths[0]))) {
                args->select_paths[args->select_path_count++] = argv[i + 1];
            } else {
                fprintf(stderr, "Warning: --select limit reached (64 max), extra paths ignored\n");
            }
            i++; /* skip value */
            continue;
        }

        if ((strcmp(argv[i], "--expr") == 0 || strcmp(argv[i], "-e") == 0)) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: Missing argument for %s\n", argv[i]);
                fprintf(stderr, "Usage: %s\n", usage);
                free(clean_argv);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            if (args->expr_count < (sizeof(args->exprs) / sizeof(args->exprs[0]))) {
                args->exprs[args->expr_count++] = argv[i + 1];
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
        {"--id",    "-i", NMO_OPT_UINT, "Object ID"},
        {"--name",  "-n", NMO_OPT_STRING, "Object name"},
    };
    enum { OPT_DEPTH, OPT_FULL, OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(clean_argc, clean_argv, opts, OPT_COUNT, &r) < 0) {
        free(clean_argv);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    free(clean_argv);

    args->depth = vals[OPT_DEPTH].present ? (int)vals[OPT_DEPTH].val.u : -1;
    args->full_mode = vals[OPT_FULL].present && vals[OPT_FULL].val.flag;
    args->has_id = vals[OPT_ID].present;
    args->id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0;
    args->name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL;
    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    if (expect_file_operand) {
        args->positional_id = (!has_selector_opt && r.pos_count >= 2) ? r.pos_args[0] : NULL;
        if (!has_selector_opt && args->positional_id == NULL) {
            fprintf(stderr, "Error: Missing arguments\n");
            fprintf(stderr, "Usage: %s\n", usage);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else if (has_selector_opt) {
        if (r.pos_count != 0) {
            fprintf(stderr, "Error: Unexpected argument '%s'\n", r.pos_args[0]);
            fprintf(stderr, "Usage: %s\n", usage);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else {
        if (r.pos_count != 1) {
            fprintf(stderr, "Error: Missing arguments\n");
            fprintf(stderr, "Usage: %s\n", usage);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args->positional_id = r.pos_args[0];
    }

    return NMO_CLI_EXIT_SUCCESS;
}

static int object_show_run(nmo_cmd_ctx_t *ctx, const object_show_args_t *args,
                           bool close_ctx, const char *usage)
{
    nmo_cmd_ctx_t c = *ctx;

    nmo_core_object_selector_t selector = {
        .has_id = args->has_id,
        .id = args->id,
        .positional_id = args->positional_id,
        .name = args->name,
        .required_base_class = (nmo_class_id_t)args->required_base_class,
        .selector_label = "Object",
        .type_label = args->type_label ? args->type_label : "object",
    };
    nmo_object_t *target = NULL;
    nmo_object_id_t object_id = 0;
    int rc = nmo_core_resolve_one_object(&c, &selector, &target, &object_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: %s\n", usage);
        return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
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
            if (args->full_mode) {
                cfg.max_depth = 8;
                cfg.array_preview_max = 64;
                cfg.text_preview_max = 64;
            }
            if (args->depth >= 0) {
                cfg.max_depth = (uint32_t)args->depth;
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
            if (args->select_path_count > 0) {
                ok |= nmo_object_summary_select_with_config(target, &sum_out, &cfg,
                                                            args->select_paths,
                                                            args->select_path_count);
            }
            if (args->expr_count > 0) {
                ok |= nmo_object_summary_expr_with_config(target, &sum_out, &cfg,
                                                          args->exprs, args->expr_count);
            }
            if (args->select_path_count == 0 && args->expr_count == 0) {
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
            if (args->full_mode) {
                cfg.max_depth = 8;
                cfg.array_preview_max = 64;
                cfg.text_preview_max = 32;
            }
            if (args->depth >= 0) {
                cfg.max_depth = (uint32_t)args->depth;
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
            if (args->select_path_count > 0) {
                (void)nmo_object_summary_select_with_config(target, &sum_out, &cfg,
                                                            args->select_paths,
                                                            args->select_path_count);
            }
            if (args->expr_count > 0) {
                (void)nmo_object_summary_expr_with_config(target, &sum_out, &cfg,
                                                          args->exprs, args->expr_count);
            }
            if (args->select_path_count == 0 && args->expr_count == 0) {
                (void)nmo_object_summary_with_config(target, &sum_out, &cfg);
            }
        }
    }

    return close_ctx ? nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS) : NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_object_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    object_show_args_t args;
    const char *usage =
        "nmo object show [--select <path>]... [--expr <expr>]... "
        "[--id <id> | --name <name> | <id>] <file>";
    int rc = object_show_parse(argc, argv, true, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    return object_show_run(&c, &args, true, usage);
}

int nmo_cmd_object_show_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv) {
    object_show_args_t args;
    const char *usage =
        "object show [--select <path>]... [--expr <expr>]... "
        "[--id <id> | --name <name> | <id>]";
    int rc = object_show_parse(argc, argv, false, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    return object_show_run(ctx, &args, false, usage);
}

int nmo_cmd_object_show_class_in_session(nmo_cmd_ctx_t *ctx,
                                         int argc,
                                         char **argv,
                                         uint32_t required_base_class,
                                         const char *type_label) {
    object_show_args_t args;
    const char *usage =
        "show [--select <path>]... [--expr <expr>]... "
        "[--id <id> | --name <name> | <id>]";
    int rc = object_show_parse(argc, argv, false, &args, usage);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    args.required_base_class = required_base_class;
    args.type_label = type_label;
    return object_show_run(ctx, &args, false, usage);
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

    /* Build query */
    nmo_object_query_t query = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = class_filter_str,
        .name_wildcard = name_filter,
        .include_derived_classes = true,
    };
    rc = nmo_core_query_build(&c, &query, NULL, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, rc);
    }

    nmo_core_iter_result_t result = {0};
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        /* Record query */
        yyjson_mut_val *query_json = yyjson_mut_obj(doc);
        if (class_filter_str) {
            yyjson_mut_obj_add_str(doc, query_json, "class_name", class_filter_str);
        }
        if (name_filter) {
            yyjson_mut_obj_add_str(doc, query_json, "name_pattern", name_filter);
        }
        yyjson_mut_obj_add_val(doc, data, "query", query_json);

        yyjson_mut_val *matches = yyjson_mut_arr(doc);

        find_json_data_t jd = { .doc = doc, .arr = matches, .ctx = &c };
        nmo_core_object_query_run(&c, &query, find_json_visitor, &jd, &result);

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
        nmo_core_object_query_run(&c, &query, find_table_visitor, &td, &result);

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
 * object export - Importable semantic snapshot export
 * ============================================================================ */

int nmo_cmd_object_export(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--class",  "-c", NMO_OPT_STRING, "Filter by class name"},
        {"--name",   "-n", NMO_OPT_STRING, "Filter by name pattern"},
        {"--filter", "-f", NMO_OPT_STRING, "Filter by DSL expression"},
        {"--depth",  "-d", NMO_OPT_UINT,   "Recursion depth (default: 4)"},
        {"--full",   NULL, NMO_OPT_FLAG,   "Full detail mode for text output (depth 8)"},
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

    /* Build query */
    nmo_object_query_t query = {0};
    nmo_core_query_dsl_t query_dsl = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = class_filter_str,
        .name_wildcard = name_pattern,
        .filter_expr = filter_expr,
        .include_derived_classes = true,
        .has_object_id = id_filter != 0,
        .object_id = id_filter,
        .print_dsl_context = true,
    };
    rc = nmo_core_query_build(&c, &query, &query_dsl, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, rc);
    }

    /* Collect matching objects */
    obj_collect_t col = {0};
    nmo_core_iter_result_t iter_result = {0};
    nmo_core_object_query_run(&c, &query, obj_collect_visitor, &col, &iter_result);

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
            nmo_core_query_dsl_destroy(&query_dsl);
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
            yyjson_mut_val *fields_holder = yyjson_mut_obj(doc);
            nmo_summary_output_t sum_out = {
                .stream = c.out,
                .json_doc = doc,
                .json_data = fields_holder,
                .is_json = true,
                .colorize = false,
                .ctx = c.ctx,
                .session = c.session,
            };
            if (nmo_object_summary_with_config(obj, &sum_out, &cfg)) {
                yyjson_mut_val *fields = yyjson_mut_obj_get(fields_holder, "fields");
                yyjson_mut_obj_add_val(doc, obj_json, "fields",
                                       fields ? fields : yyjson_mut_arr(doc));
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
    nmo_core_query_dsl_destroy(&query_dsl);

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object list-fields - List all typed fields of an object
 *
 *   nmo object list-fields <id> <file>
 * ============================================================================ */

static int object_list_fields_report(nmo_cmd_ctx_t *c,
                                     nmo_object_t *obj,
                                     nmo_object_id_t object_id)
{
    void *state = nmo_object_get_state(obj);
    const nmo_type_descriptor_t *type =
        nmo_type_registry_find_by_class_id_inherited(
            (nmo_type_registry_t *)c->registry, nmo_object_get_class_id(obj));

    if (!type || !state) {
        fprintf(stderr, "Error: No typed state for object #%u\n", object_id);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, data, "id", object_id);
        yyjson_mut_obj_add_uint(doc, data, "class_id", nmo_object_get_class_id(obj));
        nmo_cli_json_add_str_safe(doc, data, "class_name",
                                  type->name ? type->name : "<unnamed>");
        yyjson_mut_obj_add_uint(doc, data, "field_count", type->field_count);

        yyjson_mut_val *fields = yyjson_mut_arr(doc);
        for (size_t i = 0; i < type->field_count; i++) {
            const nmo_type_field_t *field = &type->fields[i];
            const nmo_type_descriptor_t *ftype =
                nmo_type_registry_find_by_guid(
                    (nmo_type_registry_t *)c->registry, field->type_guid);

            char val_buf[256];
            val_buf[0] = '\0';
            if (state && ftype) {
                const void *fptr = nmo_field_get_ptr_const(state, field);
                if (fptr) {
                    nmo_type_value_to_string(fptr, ftype,
                        (nmo_type_registry_t *)c->registry, val_buf, sizeof(val_buf));
                }
            }

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "index", i);
            nmo_cli_json_add_str_safe(doc, item, "name",
                                      field->name ? field->name : "<unnamed>");
            nmo_cli_json_add_str_safe(doc, item, "type",
                                      ftype && ftype->name ? ftype->name : "???");
            nmo_cli_json_add_str_safe(doc, item, "value",
                                      val_buf[0] ? val_buf : "(empty)");
            yyjson_mut_arr_add_val(fields, item);
        }
        yyjson_mut_obj_add_val(doc, data, "fields", fields);

        return nmo_cmd_ctx_json_end(c, doc, data, "object.list-fields");
    }

    fprintf(c->out, "Object #%u (%s) -- %zu fields:\n",
            object_id, type->name ? type->name : "<unnamed>",
            type->field_count);

    for (size_t i = 0; i < type->field_count; i++) {
        const nmo_type_field_t *field = &type->fields[i];
        const nmo_type_descriptor_t *ftype =
            nmo_type_registry_find_by_guid(
                (nmo_type_registry_t *)c->registry, field->type_guid);

        char val_buf[256];
        val_buf[0] = '\0';
        if (state && ftype) {
            const void *fptr = nmo_field_get_ptr_const(state, field);
            if (fptr) {
                nmo_type_value_to_string(fptr, ftype,
                    (nmo_type_registry_t *)c->registry, val_buf, sizeof(val_buf));
            }
        }

        fprintf(c->out, "  %-30s %-20s = %s\n",
                field->name ? field->name : "<unnamed>",
                ftype && ftype->name ? ftype->name : "???",
                val_buf[0] ? val_buf : "(empty)");
    }

    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_object_list_fields(int argc, char **argv,
                               const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--id",   "-i", NMO_OPT_UINT,   "Object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Object name"},
    };
    enum { OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0)
        return NMO_CLI_EXIT_ARG_ERROR;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = NULL;
    if (!has_selector_opt) {
        positional_id = r.pos_count >= 2 ? r.pos_args[0] : NULL;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
        .positional_id = positional_id,
        .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
        .selector_label = "Object",
        .type_label = "object",
    };
    nmo_object_t *obj = NULL;
    nmo_object_id_t object_id = 0;
    rc = nmo_core_resolve_one_object(&c, &selector, &obj, &object_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        fprintf(stderr, "Usage: nmo object list-fields [--id <id> | --name <name> | <id>] <file>\n");
        return nmo_cmd_ctx_done(&c, rc);
    }

    rc = object_list_fields_report(&c, obj, object_id);
    return nmo_cmd_ctx_done(&c, rc);
}

static int object_list_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    static const nmo_opt_def_t opts[] = {
        {"--class",   "-c", NMO_OPT_STRING, "Filter by class name"},
        {"--filter",  "-f", NMO_OPT_STRING, "Filter by DSL expression"},
        {"--sort",    "-s", NMO_OPT_STRING, "Sort by: id, name, class, size"},
        {"--reverse", "-r", NMO_OPT_FLAG,   "Reverse sort direction"},
        {"--top",     NULL, NMO_OPT_UINT,   "Show only first N results"},
    };
    nmo_opt_val_t vals[5];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 5, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *class_filter_str = vals[0].present ? vals[0].val.str : NULL;
    const char *filter_expr      = vals[1].present ? vals[1].val.str : NULL;
    uint32_t top_n               = vals[4].present ? vals[4].val.u : 0;

    nmo_object_query_t query = {0};
    nmo_core_query_dsl_t query_dsl = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = class_filter_str,
        .filter_expr = filter_expr,
        .include_derived_classes = true,
        .print_dsl_context = true,
    };
    int rc = nmo_core_query_build(ctx, &query, &query_dsl, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_core_iter_result_t result = {0};
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        obj_collect_t col = {0};
        nmo_core_object_query_run(ctx, &query, obj_collect_visitor, &col, &result);
        size_t output_count = col.count;
        if (top_n > 0 && (size_t)top_n < output_count) output_count = (size_t)top_n;
        for (size_t i = 0; i < output_count; i++) {
            list_json_data_t jd = { .doc = doc, .arr = arr, .ctx = ctx };
            list_json_visitor(i, col.objects[i], ctx, &jd);
        }
        free(col.objects);
        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)output_count);
        yyjson_mut_obj_add_val(doc, data, "objects", arr);
        rc = nmo_cmd_ctx_json_end(ctx, doc, data, "object.list");
    } else {
        static const nmo_cli_table_col_t columns[] = {
            {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"CLASS", NMO_CLI_ALIGN_LEFT, 20, 30},
            {"SIZE", NMO_CLI_ALIGN_RIGHT, 10, 0},
            {"NAME", NMO_CLI_ALIGN_LEFT, 20, 50},
        };
        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));
        list_table_data_t td = { .table = &table, .ctx = ctx };
        nmo_core_object_query_run(ctx, &query, list_table_visitor, &td, &result);
        fprintf(ctx->out, "Objects: %zu", result.matched);
        if (top_n > 0) fprintf(ctx->out, " (showing top %u)", top_n);
        fprintf(ctx->out, "\n\n");
        nmo_cli_table_print(&table, ctx->out, ctx->colorize);
        nmo_cli_table_free(&table);
    }

    nmo_core_query_dsl_destroy(&query_dsl);
    return rc;
}

int nmo_cmd_object_list_class_in_session(nmo_cmd_ctx_t *ctx,
                                         int argc,
                                         char **argv,
                                         const char *class_name)
{
    if (!ctx || argc < 1 || !argv || !argv[0] || !class_name) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char **merged = (char **)malloc(((size_t)argc + 2u) * sizeof(char *));
    if (!merged) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    merged[0] = argv[0];
    merged[1] = "--class";
    merged[2] = (char *)class_name;
    for (int i = 1; i < argc; i++) {
        merged[i + 2] = argv[i];
    }

    int rc = object_list_in_session(ctx, argc + 2, merged);
    free(merged);
    return rc;
}

static int object_find_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
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
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_object_query_t query = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = class_filter_str,
        .name_wildcard = name_filter,
        .include_derived_classes = true,
    };
    int rc = nmo_core_query_build(ctx, &query, NULL, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_core_iter_result_t result = {0};
    if (ctx->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(ctx);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *matches = yyjson_mut_arr(doc);
        find_json_data_t jd = { .doc = doc, .arr = matches, .ctx = ctx };
        nmo_core_object_query_run(ctx, &query, find_json_visitor, &jd, &result);
        yyjson_mut_obj_add_uint(doc, data, "match_count", (uint64_t)result.matched);
        yyjson_mut_obj_add_val(doc, data, "matches", matches);
        return nmo_cmd_ctx_json_end(ctx, doc, data, "object.find");
    }

    static const nmo_cli_table_col_t columns[] = {
        {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
        {"Class", NMO_CLI_ALIGN_LEFT, 20, 30},
        {"Name", NMO_CLI_ALIGN_LEFT, 20, 50},
    };
    nmo_cli_table_t table;
    nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));
    find_table_data_t td = { .table = &table, .ctx = ctx };
    nmo_core_object_query_run(ctx, &query, find_table_visitor, &td, &result);
    fprintf(ctx->out, "Found: %zu objects\n\n", result.matched);
    nmo_cli_table_print(&table, ctx->out, ctx->colorize);
    nmo_cli_table_free(&table);
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_object_find_class_in_session(nmo_cmd_ctx_t *ctx,
                                         int argc,
                                         char **argv,
                                         const char *class_name)
{
    if (!ctx || argc < 1 || !argv || !argv[0] || !class_name) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char **merged = (char **)malloc(((size_t)argc + 2u) * sizeof(char *));
    if (!merged) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    merged[0] = argv[0];
    merged[1] = "--class";
    merged[2] = (char *)class_name;
    for (int i = 1; i < argc; i++) {
        merged[i + 2] = argv[i];
    }

    int rc = object_find_in_session(ctx, argc + 2, merged);
    free(merged);
    return rc;
}

static int object_selector_only_in_session(nmo_cmd_ctx_t *ctx,
                                           int argc,
                                           char **argv,
                                           const char *label,
                                           nmo_object_t **out_obj,
                                           nmo_object_id_t *out_object_id)
{
    static const nmo_opt_def_t opts[] = {
        {"--id",   "-i", NMO_OPT_UINT,   "Object ID"},
        {"--name", "-n", NMO_OPT_STRING, "Object name"},
    };
    enum { OPT_ID, OPT_NAME, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    bool has_selector_opt = vals[OPT_ID].present || vals[OPT_NAME].present;
    const char *positional_id = (!has_selector_opt && r.pos_count == 1) ? r.pos_args[0] : NULL;
    nmo_core_object_selector_t selector = {
        .has_id = vals[OPT_ID].present,
        .id = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0,
        .positional_id = positional_id,
        .name = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL,
        .selector_label = label,
        .type_label = "object",
    };
    nmo_object_t *obj = NULL;
    nmo_object_id_t object_id = 0;
    int rc = nmo_core_resolve_one_object(ctx, &selector, &obj, &object_id);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;
    if (out_obj) *out_obj = obj;
    if (out_object_id) *out_object_id = object_id;
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_object_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: object list|tree|show|find|refs|export|impact|orphans|cycles|graph|list-fields ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "show") == 0 || strcmp(argv[0], "s") == 0) {
        return nmo_cmd_object_show_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "refs") == 0 || strcmp(argv[0], "r") == 0) {
        return nmo_cmd_object_refs_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "ls") == 0) {
        return object_list_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "find") == 0 || strcmp(argv[0], "f") == 0) {
        return object_find_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "tree") == 0 || strcmp(argv[0], "t") == 0) {
        return object_list_in_session(ctx, 1, argv);
    }
    if (strcmp(argv[0], "impact") == 0 || strcmp(argv[0], "imp") == 0) {
        return nmo_cmd_object_refgraph_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "orphans") == 0 || strcmp(argv[0], "orp") == 0) {
        return nmo_cmd_object_refgraph_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "cycles") == 0 || strcmp(argv[0], "cyc") == 0) {
        return nmo_cmd_object_refgraph_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "graph") == 0 || strcmp(argv[0], "gr") == 0) {
        return nmo_cmd_object_refgraph_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "export") == 0 || strcmp(argv[0], "x") == 0) {
        return object_find_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "list-fields") == 0 || strcmp(argv[0], "lf") == 0) {
        nmo_object_t *obj = NULL;
        nmo_object_id_t object_id = 0;
        int rc = object_selector_only_in_session(ctx, argc, argv, "Object",
                                                 &obj, &object_id);
        if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

        return object_list_fields_report(ctx, obj, object_id);
    }

    fprintf(stderr, "Unsupported object read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}
