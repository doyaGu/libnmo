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
#include "session/nmo_session.h"
#include "session/nmo_runtime_delete.h"
#include "session/nmo_runtime_kernel.h"
#include "app/nmo_object_hierarchy.h"
#include "app/nmo_save.h"
#include "core/nmo_arena.h"
#include "dsl/nmo_dsl.h"
#include "object/nmo_class_ids.h"
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

    /* Build filter */
    nmo_core_object_filter_t filter = {0};
    if (class_filter_str) {
        filter.class_id = nmo_core_class_id(&c, class_filter_str);
        if (!filter.class_id) {
            fprintf(stderr, "Error: Unknown class '%s'\n", class_filter_str);
            nmo_tool_close_session(ctx, session);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        filter.class_derived = true;
    }
    if (filter_expr) {
        nmo_dsl_compile_options_t compile_opts = { .mode = NMO_DSL_MODE_EXPRESSION };
        nmo_status_t st = nmo_dsl_compile(c.registry, NULL, filter_expr, &compile_opts, &filter.dsl_filter);
        if (st != NMO_OK) {
            nmo_core_dsl_print_error(stderr, filter_expr, "Error: Failed to compile filter expression");
            nmo_tool_close_session(ctx, session);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    nmo_cli_sort_key_t sort_key = nmo_cli_parse_sort_key(sort_key_str);
    bool needs_collect = (sort_key != NMO_CLI_SORT_NONE) || (top_n > 0);
    nmo_core_iter_result_t result;
    int rc = NMO_CLI_EXIT_SUCCESS;

    if (needs_collect) {
        obj_collect_t col = {0};
        nmo_core_iter_objects(&c, &filter, obj_collect_visitor, &col, &result);

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
            nmo_core_iter_objects(&c, &filter, list_json_visitor, &jd, &result);
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
            nmo_core_iter_objects(&c, &filter, list_table_visitor, &td, &result);
            fprintf(c.out, "Objects: %zu", result.matched);
            if (class_filter_str) fprintf(c.out, " (filtered by class: %s)", class_filter_str);
            if (filter_expr) fprintf(c.out, " (filtered by: %s)", filter_expr);
            fprintf(c.out, "\n\n");
            nmo_cli_table_print(&table, c.out, c.colorize);
            nmo_cli_table_free(&table);
        }
    }

    if (filter.dsl_filter) {
        nmo_dsl_program_destroy(filter.dsl_filter);
    }
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
            nmo_core_dsl_print_error(stderr, filter_expr, "Error: Failed to compile filter expression");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    bool needs_collect = (sort_key != NMO_CLI_SORT_NONE) || (top_n > 0);
    nmo_core_iter_result_t result;

    if (needs_collect) {
        /* Path A: collect -> sort -> truncate -> output */
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
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
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
            nmo_core_dsl_print_error(stderr, filter_expr, "Error: Failed to compile filter expression");
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
    int rc = nmo_cmd_ctx_init_with_file(&c, file_path, global);
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

/* ============================================================================
 * object impact - Show deletion impact analysis
 * ============================================================================ */

int nmo_cmd_object_impact(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, NULL, 0, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    /* Find the object ID among positional args */
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
        fprintf(stderr, "Usage: nmo object impact <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Find the target object */
    nmo_object_t *obj = nmo_core_find_by_id(&c, object_id);
    if (!obj) {
        fprintf(stderr, "Error: Object %u not found\n", object_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    const char *obj_name = nmo_object_get_name(obj);
    nmo_class_id_t obj_cid = nmo_object_get_class_id(obj);
    char obj_cbuf[32];
    const char *obj_class = nmo_core_class_name_or(&c, obj_cid, obj_cbuf, sizeof(obj_cbuf));

    /* Build ref graph for direct dependents */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        fprintf(stderr, "Error: Failed to create arena\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    nmo_ref_graph_t *graph = nmo_ref_graph_create(repo, c.registry, arena);
    if (!graph) {
        nmo_arena_destroy(arena);
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Get direct dependents (incoming refs) */
    nmo_ref_edge_t *in_edges = NULL;
    size_t in_count = 0;
    nmo_ref_graph_get_object_edges(graph, object_id, NMO_REF_DIR_INCOMING,
                                   &in_edges, &in_count);

    /* Preview cascade deletion */
    const nmo_type_runtime_t *type_rt = nmo_context_get_type_runtime(c.ctx);
    nmo_object_id_t *cascade_ids = NULL;
    size_t cascade_count = 0;
    int preview_rc = nmo_runtime_preview_delete(
        repo, type_rt, arena,
        &object_id, 1,
        NMO_RUNTIME_REQUEST_CASCADE,
        &cascade_ids, &cascade_count);

    if (preview_rc != NMO_OK) {
        /* Fallback: just the target itself */
        cascade_ids = NULL;
        cascade_count = 0;
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        /* Target info */
        yyjson_mut_val *target = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, target, "id", object_id);
        if (obj_name && obj_name[0])
            nmo_cli_json_add_str_safe(doc, target, "name", obj_name);
        yyjson_mut_obj_add_str(doc, target, "class_name", obj_class);
        yyjson_mut_obj_add_val(doc, data, "target", target);

        /* Direct dependents */
        yyjson_mut_val *deps = yyjson_mut_arr(doc);
        for (size_t i = 0; i < in_count; ++i) {
            yyjson_mut_val *dep = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, dep, "id", in_edges[i].from);

            nmo_object_t *peer = nmo_core_find_by_id(&c, in_edges[i].from);
            if (peer) {
                const char *pname = nmo_object_get_name(peer);
                if (pname && pname[0])
                    nmo_cli_json_add_str_safe(doc, dep, "name", pname);
                char cbuf[32];
                const char *pcls = nmo_core_class_name_or(
                    &c, nmo_object_get_class_id(peer), cbuf, sizeof(cbuf));
                yyjson_mut_obj_add_str(doc, dep, "class_name", pcls);
            }
            yyjson_mut_obj_add_str(doc, dep, "ref_kind",
                                   nmo_ref_kind_name(in_edges[i].kind));
            yyjson_mut_arr_add_val(deps, dep);
        }
        yyjson_mut_obj_add_val(doc, data, "direct_dependents", deps);

        /* Cascade set */
        yyjson_mut_val *cas = yyjson_mut_arr(doc);
        for (size_t i = 0; i < cascade_count; ++i) {
            yyjson_mut_val *entry = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, entry, "id", cascade_ids[i]);

            nmo_object_t *cobj = nmo_core_find_by_id(&c, cascade_ids[i]);
            if (cobj) {
                const char *cname = nmo_object_get_name(cobj);
                if (cname && cname[0])
                    nmo_cli_json_add_str_safe(doc, entry, "name", cname);
                char cbuf[32];
                const char *ccls = nmo_core_class_name_or(
                    &c, nmo_object_get_class_id(cobj), cbuf, sizeof(cbuf));
                yyjson_mut_obj_add_str(doc, entry, "class_name", ccls);
            }
            yyjson_mut_arr_add_val(cas, entry);
        }
        yyjson_mut_obj_add_val(doc, data, "cascade_set", cas);
        yyjson_mut_obj_add_uint(doc, data, "cascade_count",
                                (uint64_t)cascade_count);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.impact");
    } else {
        /* Text output */
        fprintf(c.out, "Impact Analysis: Object #%u", object_id);
        if (obj_name && obj_name[0])
            fprintf(c.out, " \"%s\"", obj_name);
        fprintf(c.out, " (%s)\n\n", obj_class);

        /* Direct dependents */
        fprintf(c.out, "Direct dependents (%zu):\n", in_count);
        if (in_count == 0) {
            fprintf(c.out, "  (none)\n");
        } else {
            static const nmo_cli_table_col_t dep_cols[] = {
                {"ID",    NMO_CLI_ALIGN_RIGHT, 6, 0},
                {"Class", NMO_CLI_ALIGN_LEFT, 18, 0},
                {"Name",  NMO_CLI_ALIGN_LEFT, 24, 0},
                {"Kind",  NMO_CLI_ALIGN_LEFT, 15, 0},
            };
            nmo_cli_table_t dep_table;
            nmo_cli_table_init(&dep_table, dep_cols,
                               sizeof(dep_cols) / sizeof(dep_cols[0]));

            for (size_t i = 0; i < in_count; ++i) {
                char id_buf[16];
                snprintf(id_buf, sizeof(id_buf), "%u", in_edges[i].from);
                const char *pcls = "-";
                const char *pname = "-";
                nmo_object_t *peer = nmo_core_find_by_id(&c, in_edges[i].from);
                char cbuf[32];
                if (peer) {
                    pcls = nmo_core_class_name_or(
                        &c, nmo_object_get_class_id(peer), cbuf, sizeof(cbuf));
                    const char *n = nmo_object_get_name(peer);
                    if (n && n[0]) pname = n;
                }
                const char *cells[] = {
                    id_buf, pcls, pname,
                    nmo_ref_kind_name(in_edges[i].kind)
                };
                nmo_cli_table_add_row(&dep_table, cells, 4);
            }
            nmo_cli_table_print(&dep_table, c.out, c.colorize);
            nmo_cli_table_free(&dep_table);
        }

        /* Cascade set */
        fprintf(c.out, "\nCascade deletion would remove %zu object(s):\n",
                cascade_count);
        if (cascade_count == 0) {
            fprintf(c.out, "  (none)\n");
        } else {
            static const nmo_cli_table_col_t cas_cols[] = {
                {"ID",    NMO_CLI_ALIGN_RIGHT, 6, 0},
                {"Class", NMO_CLI_ALIGN_LEFT, 18, 0},
                {"Name",  NMO_CLI_ALIGN_LEFT, 24, 0},
            };
            nmo_cli_table_t cas_table;
            nmo_cli_table_init(&cas_table, cas_cols,
                               sizeof(cas_cols) / sizeof(cas_cols[0]));

            for (size_t i = 0; i < cascade_count; ++i) {
                char id_buf[16];
                snprintf(id_buf, sizeof(id_buf), "%u", cascade_ids[i]);
                const char *ccls = "-";
                const char *cname_str = "-";
                nmo_object_t *cobj = nmo_core_find_by_id(&c, cascade_ids[i]);
                char cbuf[32];
                if (cobj) {
                    ccls = nmo_core_class_name_or(
                        &c, nmo_object_get_class_id(cobj), cbuf, sizeof(cbuf));
                    const char *n = nmo_object_get_name(cobj);
                    if (n && n[0]) cname_str = n;
                }
                const char *cells[] = { id_buf, ccls, cname_str };
                nmo_cli_table_add_row(&cas_table, cells, 3);
            }
            nmo_cli_table_print(&cas_table, c.out, c.colorize);
            nmo_cli_table_free(&cas_table);
        }
    }

    nmo_ref_graph_destroy(graph);
    nmo_arena_destroy(arena);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object orphans - Find unreachable objects
 * ============================================================================ */

/** Binary search in sorted ID array */
static bool orphan_id_in_set(const nmo_object_id_t *arr, size_t count,
                             nmo_object_id_t id) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] < id) lo = mid + 1;
        else if (arr[mid] > id) hi = mid;
        else return true;
    }
    return false;
}

int nmo_cmd_object_orphans(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--class", "-c", NMO_OPT_STRING, "Filter by class name"},
    };
    enum { OPT_CLASS };
    nmo_opt_val_t vals[1];
    const char *pos_arr[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos_arr, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *class_filter_str = vals[OPT_CLASS].present ? vals[OPT_CLASS].val.str : NULL;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Resolve optional class filter */
    nmo_class_id_t filter_cid = 0;
    if (class_filter_str) {
        filter_cid = nmo_core_class_id(&c, class_filter_str);
        if (!filter_cid) {
            fprintf(stderr, "Error: Unknown class '%s'\n", class_filter_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    /* Get objects */
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Build reference graph */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        fprintf(stderr, "Error: Failed to create arena\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    nmo_ref_graph_t *graph = nmo_ref_graph_create(repo, c.registry, arena);
    if (!graph) {
        nmo_arena_destroy(arena);
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Collect root IDs using tiered strategy (same as validate orphans) */
    nmo_object_id_t *root_ids = NULL;
    size_t root_count = 0;
    if (object_count > 0) {
        root_ids = (nmo_object_id_t *)nmo_arena_alloc(arena,
            object_count * sizeof(nmo_object_id_t),
            _Alignof(nmo_object_id_t));
        if (!root_ids) {
            nmo_arena_destroy(arena);
            fprintf(stderr, "Error: Allocation failed\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        /* Tier 1: CKLevel / CKScene */
        for (size_t i = 0; i < object_count; ++i) {
            nmo_class_id_t cid = nmo_object_get_class_id(objects[i]);
            if (cid == NMO_CID_LEVEL || cid == NMO_CID_SCENE ||
                nmo_type_registry_is_class_derived_from(c.registry, cid, NMO_CID_LEVEL) ||
                nmo_type_registry_is_class_derived_from(c.registry, cid, NMO_CID_SCENE)) {
                root_ids[root_count++] = nmo_object_get_id(objects[i]);
            }
        }

        /* Tier 2: CKGroup */
        if (root_count == 0) {
            for (size_t i = 0; i < object_count; ++i) {
                nmo_class_id_t cid = nmo_object_get_class_id(objects[i]);
                if (cid == NMO_CID_GROUP ||
                    nmo_type_registry_is_class_derived_from(c.registry, cid, NMO_CID_GROUP)) {
                    root_ids[root_count++] = nmo_object_get_id(objects[i]);
                }
            }
        }

        /* Tier 3: CK3dEntity / CK3dObject */
        if (root_count == 0) {
            for (size_t i = 0; i < object_count; ++i) {
                nmo_class_id_t cid = nmo_object_get_class_id(objects[i]);
                if (cid == NMO_CID_3DENTITY || cid == NMO_CID_3DOBJECT ||
                    nmo_type_registry_is_class_derived_from(c.registry, cid, NMO_CID_3DENTITY)) {
                    root_ids[root_count++] = nmo_object_get_id(objects[i]);
                }
            }
        }

        /* Tier 4: all objects with zero incoming references */
        if (root_count == 0) {
            for (size_t i = 0; i < object_count; ++i) {
                nmo_object_id_t oid = nmo_object_get_id(objects[i]);
                nmo_ref_edge_t *edges = NULL;
                size_t ecount = 0;
                nmo_ref_graph_get_object_edges(graph, oid, NMO_REF_DIR_INCOMING,
                                               &edges, &ecount);
                if (ecount == 0) {
                    root_ids[root_count++] = oid;
                }
            }
        }
    }

    /* Mark reachable set */
    nmo_object_id_t *reachable_ids = NULL;
    size_t reachable_count = 0;
    {
        nmo_status_t ms = nmo_ref_graph_mark_reachable(
            graph, root_ids, root_count, arena,
            &reachable_ids, &reachable_count);
        if (ms != NMO_OK) {
            nmo_ref_graph_destroy(graph);
            nmo_arena_destroy(arena);
            fprintf(stderr, "Error: mark_reachable failed\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
    }

    /* Collect orphans */
    typedef struct {
        nmo_object_t *obj;
    } orphan_entry_t;

    orphan_entry_t *orphan_list = NULL;
    size_t orphan_count = 0;
    if (object_count > 0) {
        orphan_list = (orphan_entry_t *)nmo_arena_alloc(arena,
            object_count * sizeof(orphan_entry_t),
            _Alignof(orphan_entry_t));
        if (!orphan_list) {
            nmo_ref_graph_destroy(graph);
            nmo_arena_destroy(arena);
            fprintf(stderr, "Error: Allocation failed\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
    }

    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *o = objects[i];
        nmo_object_id_t oid = nmo_object_get_id(o);

        /* Skip reachable */
        if (orphan_id_in_set(reachable_ids, reachable_count, oid))
            continue;

        /* Apply class filter */
        if (filter_cid != 0) {
            nmo_class_id_t cid = nmo_object_get_class_id(o);
            if (cid != filter_cid &&
                !nmo_type_registry_is_class_derived_from(c.registry, cid, filter_cid))
                continue;
        }

        orphan_list[orphan_count].obj = o;
        orphan_count++;
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "total_objects",
                                (uint64_t)object_count);
        yyjson_mut_obj_add_uint(doc, data, "root_count",
                                (uint64_t)root_count);

        /* Roots */
        yyjson_mut_val *roots = yyjson_mut_arr(doc);
        for (size_t i = 0; i < root_count; ++i) {
            yyjson_mut_val *rentry = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, rentry, "id", root_ids[i]);
            nmo_object_t *robj = nmo_core_find_by_id(&c, root_ids[i]);
            if (robj) {
                char cbuf[32];
                const char *rcls = nmo_core_class_name_or(
                    &c, nmo_object_get_class_id(robj), cbuf, sizeof(cbuf));
                yyjson_mut_obj_add_str(doc, rentry, "class_name", rcls);
            }
            yyjson_mut_arr_add_val(roots, rentry);
        }
        yyjson_mut_obj_add_val(doc, data, "roots", roots);

        yyjson_mut_obj_add_uint(doc, data, "orphan_count",
                                (uint64_t)orphan_count);

        /* Orphan list */
        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < orphan_count; ++i) {
            nmo_object_t *o = orphan_list[i].obj;
            yyjson_mut_val *entry = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, entry, "id",
                                    (uint64_t)nmo_object_get_id(o));
            nmo_class_id_t cid = nmo_object_get_class_id(o);
            yyjson_mut_obj_add_uint(doc, entry, "class_id", (uint64_t)cid);
            char cbuf[32];
            const char *cname = nmo_core_class_name_or(&c, cid, cbuf, sizeof(cbuf));
            yyjson_mut_obj_add_str(doc, entry, "class_name", cname);
            const char *name = nmo_object_get_name(o);
            if (name && name[0])
                nmo_cli_json_add_str_safe(doc, entry, "name", name);
            yyjson_mut_arr_add_val(arr, entry);
        }
        yyjson_mut_obj_add_val(doc, data, "orphans", arr);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.orphans");
    } else {
        /* Text output */
        fprintf(c.out, "Orphan Analysis: %zu unreachable object(s) (of %zu total)\n\n",
                orphan_count, object_count);

        if (orphan_count > 0) {
            static const nmo_cli_table_col_t cols[] = {
                {"ID",    NMO_CLI_ALIGN_RIGHT, 6, 0},
                {"Class", NMO_CLI_ALIGN_LEFT, 18, 0},
                {"Name",  NMO_CLI_ALIGN_LEFT, 24, 0},
            };
            nmo_cli_table_t table;
            nmo_cli_table_init(&table, cols, sizeof(cols) / sizeof(cols[0]));

            for (size_t i = 0; i < orphan_count; ++i) {
                nmo_object_t *o = orphan_list[i].obj;
                char id_buf[16];
                snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(o));
                char cbuf[32];
                const char *cname = nmo_core_class_name_or(
                    &c, nmo_object_get_class_id(o), cbuf, sizeof(cbuf));
                const char *name = nmo_object_get_name(o);
                const char *name_str = (name && name[0]) ? name : "-";
                const char *cells[] = { id_buf, cname, name_str };
                nmo_cli_table_add_row(&table, cells, 3);
            }
            nmo_cli_table_print(&table, c.out, c.colorize);
            nmo_cli_table_free(&table);
        }

        /* Root summary */
        fprintf(c.out, "\nRoot objects used: %zu", root_count);
        if (root_count > 0 && root_count <= 10) {
            fprintf(c.out, " (");
            for (size_t i = 0; i < root_count; ++i) {
                if (i > 0) fprintf(c.out, ", ");
                nmo_object_t *robj = nmo_core_find_by_id(&c, root_ids[i]);
                char cbuf[32];
                const char *rcls = robj ? nmo_core_class_name_or(
                    &c, nmo_object_get_class_id(robj), cbuf, sizeof(cbuf)) : "?";
                fprintf(c.out, "%s #%u", rcls, root_ids[i]);
            }
            fprintf(c.out, ")");
        }
        fprintf(c.out, "\n");
    }

    nmo_ref_graph_destroy(graph);
    nmo_arena_destroy(arena);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * object cycles - Detect circular references
 * ============================================================================ */

/** Cycle record: array of object IDs forming the cycle */
typedef struct {
    nmo_object_id_t *ids;
    nmo_ref_kind_t *kinds;    /**< ref kinds along edges (length == count) */
    size_t count;
} cycle_record_t;

/** DFS state for cycle detection */
typedef struct {
    nmo_cmd_ctx_t *c;
    nmo_ref_graph_t *graph;
    uint8_t *color;           /**< 0=WHITE, 1=GRAY, 2=BLACK */
    nmo_object_id_t *stack;   /**< DFS path stack */
    nmo_ref_kind_t *stack_kinds; /**< ref kind for each stack entry */
    size_t stack_size;

    cycle_record_t *cycles;
    size_t cycle_count;
    size_t cycle_cap;

    nmo_arena_t *arena;
    nmo_object_id_t max_id;
} cycle_dfs_state_t;

static void cycle_dfs_record(cycle_dfs_state_t *st, nmo_object_id_t back_target,
                             nmo_ref_kind_t back_kind) {
    /* Find back_target in the stack to extract the cycle */
    size_t start = 0;
    bool found = false;
    for (size_t i = 0; i < st->stack_size; ++i) {
        if (st->stack[i] == back_target) {
            start = i;
            found = true;
            break;
        }
    }
    if (!found) return;

    size_t len = st->stack_size - start;
    /* Normalize: rotate so minimum ID is first (for dedup across rotations) */
    size_t min_pos = 0;
    for (size_t j = 1; j < len; ++j) {
        if (st->stack[start + j] < st->stack[start + min_pos])
            min_pos = j;
    }

    /* Deduplicate: check if we already have this cycle (rotation-normalized) */
    for (size_t ci = 0; ci < st->cycle_count; ++ci) {
        if (st->cycles[ci].count == len) {
            bool same = true;
            for (size_t j = 0; j < len; ++j) {
                if (st->cycles[ci].ids[j] != st->stack[start + ((min_pos + j) % len)]) {
                    same = false;
                    break;
                }
            }
            if (same) return; /* already recorded */
        }
    }

    /* Grow cycle array if needed */
    if (st->cycle_count >= st->cycle_cap) {
        size_t new_cap = st->cycle_cap ? st->cycle_cap * 2 : 16;
        cycle_record_t *tmp = (cycle_record_t *)realloc(
            st->cycles, new_cap * sizeof(cycle_record_t));
        if (!tmp) return;
        st->cycles = tmp;
        st->cycle_cap = new_cap;
    }

    nmo_object_id_t *ids = (nmo_object_id_t *)nmo_arena_alloc(
        st->arena, len * sizeof(nmo_object_id_t),
        _Alignof(nmo_object_id_t));
    nmo_ref_kind_t *kinds = (nmo_ref_kind_t *)nmo_arena_alloc(
        st->arena, len * sizeof(nmo_ref_kind_t),
        _Alignof(nmo_ref_kind_t));
    if (!ids || !kinds) return;

    for (size_t j = 0; j < len; ++j) {
        size_t src_j = (min_pos + j) % len;
        size_t next_j = (min_pos + j + 1) % len;
        ids[j] = st->stack[start + src_j];
        kinds[j] = (j + 1 < len) ? st->stack_kinds[start + next_j] : back_kind;
    }

    cycle_record_t *rec = &st->cycles[st->cycle_count++];
    rec->ids = ids;
    rec->kinds = kinds;
    rec->count = len;
}

/* Iterative DFS frame — avoids C stack overflow on deep graphs */
typedef struct {
    nmo_object_id_t id;
    nmo_ref_kind_t entry_kind;
    nmo_ref_edge_t *edges;
    size_t ecount;
    size_t edge_idx;  /* next edge to process */
} dfs_frame_t;

static void cycle_dfs_visit(cycle_dfs_state_t *st, nmo_object_id_t start_id,
                            nmo_ref_kind_t start_kind) {
    size_t frame_cap = st->max_id < 4096 ? 4096 : (size_t)(st->max_id + 1);
    dfs_frame_t *frames = (dfs_frame_t *)malloc(frame_cap * sizeof(dfs_frame_t));
    if (!frames) return;
    size_t frame_top = 0;

    /* Push initial frame */
    if (start_id > st->max_id) { free(frames); return; }
    st->color[start_id] = 1; /* GRAY */
    st->stack[st->stack_size] = start_id;
    st->stack_kinds[st->stack_size] = start_kind;
    st->stack_size++;

    nmo_ref_edge_t *edges = NULL;
    size_t ecount = 0;
    nmo_ref_graph_get_object_edges(st->graph, start_id, NMO_REF_DIR_OUTGOING,
                                   &edges, &ecount);
    frames[frame_top].id = start_id;
    frames[frame_top].entry_kind = start_kind;
    frames[frame_top].edges = edges;
    frames[frame_top].ecount = ecount;
    frames[frame_top].edge_idx = 0;
    frame_top++;

    while (frame_top > 0) {
        dfs_frame_t *f = &frames[frame_top - 1];

        if (f->edge_idx >= f->ecount) {
            /* All edges processed — pop frame, mark BLACK */
            st->stack_size--;
            st->color[f->id] = 2; /* BLACK */
            frame_top--;
            continue;
        }

        nmo_ref_edge_t *edge = &f->edges[f->edge_idx++];
        nmo_object_id_t target = edge->to;
        if (target > st->max_id) continue;

        if (st->color[target] == 1) {
            /* Back edge → cycle */
            cycle_dfs_record(st, target, edge->kind);
        } else if (st->color[target] == 0) {
            /* Unvisited → push new frame */
            if (frame_top >= frame_cap || st->stack_size >= frame_cap) continue;

            st->color[target] = 1; /* GRAY */
            st->stack[st->stack_size] = target;
            st->stack_kinds[st->stack_size] = edge->kind;
            st->stack_size++;

            nmo_ref_edge_t *tedges = NULL;
            size_t tecount = 0;
            nmo_ref_graph_get_object_edges(st->graph, target, NMO_REF_DIR_OUTGOING,
                                           &tedges, &tecount);
            frames[frame_top].id = target;
            frames[frame_top].entry_kind = edge->kind;
            frames[frame_top].edges = tedges;
            frames[frame_top].ecount = tecount;
            frames[frame_top].edge_idx = 0;
            frame_top++;
        }
    }

    free(frames);
}

int nmo_cmd_object_cycles(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, NULL, 0, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Get objects to find max_id */
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_id_t max_id = 0;
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_id_t oid = nmo_object_get_id(objects[i]);
        if (oid > max_id) max_id = oid;
    }

    /* Build reference graph */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        fprintf(stderr, "Error: Failed to create arena\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    nmo_ref_graph_t *graph = nmo_ref_graph_create(repo, c.registry, arena);
    if (!graph) {
        nmo_arena_destroy(arena);
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Allocate DFS state */
    size_t color_size = (size_t)(max_id + 1);
    uint8_t *color = (uint8_t *)calloc(color_size, sizeof(uint8_t));
    nmo_object_id_t *stack = (nmo_object_id_t *)malloc(
        object_count * sizeof(nmo_object_id_t));
    nmo_ref_kind_t *stack_kinds = (nmo_ref_kind_t *)malloc(
        object_count * sizeof(nmo_ref_kind_t));

    if (!color || !stack || !stack_kinds) {
        free(color);
        free(stack);
        free(stack_kinds);
        nmo_ref_graph_destroy(graph);
        nmo_arena_destroy(arena);
        fprintf(stderr, "Error: Allocation failed\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    cycle_dfs_state_t st;
    memset(&st, 0, sizeof(st));
    st.c = &c;
    st.graph = graph;
    st.color = color;
    st.stack = stack;
    st.stack_kinds = stack_kinds;
    st.stack_size = 0;
    st.cycles = NULL;
    st.cycle_count = 0;
    st.cycle_cap = 0;
    st.arena = arena;
    st.max_id = max_id;

    /* Run DFS from each unvisited object */
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_id_t oid = nmo_object_get_id(objects[i]);
        if (oid <= max_id && color[oid] == 0) {
            cycle_dfs_visit(&st, oid, NMO_REF_KIND_UNKNOWN);
        }
    }

    /* Output results */
    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "cycle_count",
                                (uint64_t)st.cycle_count);

        yyjson_mut_val *arr = yyjson_mut_arr(doc);
        for (size_t ci = 0; ci < st.cycle_count; ++ci) {
            cycle_record_t *rec = &st.cycles[ci];
            yyjson_mut_val *cyc = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, cyc, "length", (uint64_t)rec->count);

            yyjson_mut_val *objs = yyjson_mut_arr(doc);
            for (size_t j = 0; j < rec->count; ++j) {
                yyjson_mut_val *entry = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, entry, "id", rec->ids[j]);
                nmo_object_t *o = nmo_core_find_by_id(&c, rec->ids[j]);
                if (o) {
                    const char *n = nmo_object_get_name(o);
                    if (n && n[0])
                        nmo_cli_json_add_str_safe(doc, entry, "name", n);
                    char cbuf[32];
                    const char *cls = nmo_core_class_name_or(
                        &c, nmo_object_get_class_id(o), cbuf, sizeof(cbuf));
                    yyjson_mut_obj_add_str(doc, entry, "class_name", cls);
                }
                yyjson_mut_arr_add_val(objs, entry);
            }
            yyjson_mut_obj_add_val(doc, cyc, "objects", objs);

            yyjson_mut_val *kinds = yyjson_mut_arr(doc);
            for (size_t j = 0; j < rec->count; ++j) {
                yyjson_mut_arr_add_str(doc, kinds,
                                       nmo_ref_kind_name(rec->kinds[j]));
            }
            yyjson_mut_obj_add_val(doc, cyc, "ref_kinds", kinds);

            yyjson_mut_arr_add_val(arr, cyc);
        }
        yyjson_mut_obj_add_val(doc, data, "cycles", arr);

        nmo_cmd_ctx_json_end(&c, doc, data, "object.cycles");
    } else {
        if (st.cycle_count == 0) {
            fprintf(c.out, "No circular references detected.\n");
        } else {
            fprintf(c.out, "Cycle Detection: %zu cycle(s) found\n",
                    st.cycle_count);

            for (size_t ci = 0; ci < st.cycle_count; ++ci) {
                cycle_record_t *rec = &st.cycles[ci];
                fprintf(c.out, "\nCycle %zu (%zu object%s):\n",
                        ci + 1, rec->count,
                        rec->count == 1 ? "" : "s");

                /* Print chain: #A -> #B -> ... -> #A */
                fprintf(c.out, "  ");
                for (size_t j = 0; j < rec->count; ++j) {
                    if (j > 0) fprintf(c.out, " -> ");
                    nmo_object_t *o = nmo_core_find_by_id(&c, rec->ids[j]);
                    const char *n = o ? nmo_object_get_name(o) : NULL;
                    char cbuf[32];
                    const char *cls = o ? nmo_core_class_name_or(
                        &c, nmo_object_get_class_id(o), cbuf, sizeof(cbuf)) : "?";
                    fprintf(c.out, "#%u %s", rec->ids[j], cls);
                    if (n && n[0]) fprintf(c.out, " \"%s\"", n);
                }
                fprintf(c.out, " -> #%u\n", rec->ids[0]);

                /* Print reference kinds */
                fprintf(c.out, "  Reference kinds: ");
                for (size_t j = 0; j < rec->count; ++j) {
                    if (j > 0) fprintf(c.out, " -> ");
                    fprintf(c.out, "%s", nmo_ref_kind_name(rec->kinds[j]));
                }
                fprintf(c.out, "\n");
            }
        }
    }

    free(color);
    free(stack);
    free(stack_kinds);
    free(st.cycles);
    nmo_ref_graph_destroy(graph);
    nmo_arena_destroy(arena);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

