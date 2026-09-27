/**
 * @file nmo_cmd_object.c
 * @brief CLI object command group implementation
 */

#include "nmo_cmd_object.h"
#include "nmo_cmd_object_internal.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_cli_sort.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"
#include "object/nmo_object_summary.h"

#include "nmo.h"
#include "runtime/nmo_context.h"
#include "object/nmo_object_hierarchy.h"
#include "core/nmo_arena.h"
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

static const nmo_cli_table_col_t object_list_columns[] = {
    {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
    {"CLASS", NMO_CLI_ALIGN_LEFT, 20, 30},
    {"SIZE", NMO_CLI_ALIGN_RIGHT, 10, 0},
    {"NAME", NMO_CLI_ALIGN_LEFT, 20, 50},
};

static const nmo_cli_table_col_t object_find_columns[] = {
    {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
    {"Class", NMO_CLI_ALIGN_LEFT, 20, 30},
    {"Name", NMO_CLI_ALIGN_LEFT, 20, 50},
};

/* Identity fields shared by list and find rows. JSON: id, class_id,
 * class_name?, name?. Text: ID, <class_label>, and optionally <name_label>
 * right away (the list row appends SIZE before NAME instead). */
static bool object_row_identity(const nmo_cmd_ctx_t *c, nmo_object_t *obj,
                                nmo_cli_record_t *rec, const char *class_label,
                                const char *name_label, bool name_in_text_now)
{
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const char *class_name = nmo_core_class_name(c, class_id);
    const char *name = nmo_object_get_name(obj);
    bool ok = nmo_cli_record_uint(rec, "id", "ID", nmo_object_get_id(obj));
    ok = ok && nmo_cli_record_uint(rec, "class_id", NULL, class_id);
    ok = ok && nmo_cli_record_str_opt(rec, "class_name", NULL, class_name, NULL);
    ok = ok && nmo_cli_record_text(rec, class_label, class_name ? class_name : "-");
    ok = ok && nmo_cli_record_str_opt(rec, "name", NULL, name, NULL);
    if (name_in_text_now) {
        ok = ok && nmo_cli_record_text(rec, name_label,
                                       (name && name[0]) ? name : "-");
    }
    return ok;
}

static nmo_cli_record_t *object_list_row_new(const nmo_cmd_ctx_t *c,
                                             nmo_object_t *obj)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec) return NULL;
    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    const char *name = nmo_object_get_name(obj);
    bool ok = object_row_identity(c, obj, rec, "CLASS", "NAME", false);
    ok = ok && nmo_cli_record_uint(rec, "size", "SIZE",
                                   chunk ? (uint64_t)nmo_chunk_get_data_size(chunk) : 0u);
    ok = ok && nmo_cli_record_text(rec, "NAME", (name && name[0]) ? name : "-");
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

static nmo_cli_record_t *object_find_row_new(const nmo_cmd_ctx_t *c,
                                             nmo_object_t *obj)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec) return NULL;
    if (!object_row_identity(c, obj, rec, "Class", "Name", true)) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

typedef nmo_cli_record_t *(*object_row_fn)(const nmo_cmd_ctx_t *c,
                                           nmo_object_t *obj);

/* `count_key` = json_count, then `key` as a table of the rows of
 * objects[0, text_count); rows from json_count on are text only. */
static void object_add_rows(const nmo_cmd_ctx_t *c, nmo_cli_record_t *rec,
                            const char *count_key, const char *key,
                            const nmo_cli_table_col_t *columns,
                            size_t column_count, nmo_object_t **objects,
                            size_t json_count, size_t text_count,
                            object_row_fn row_new)
{
    nmo_cli_record_uint(rec, count_key, NULL, (uint64_t)json_count);
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, key, NULL);
    nmo_cli_record_array_set_table(arr, columns, column_count);
    for (size_t i = 0; i < text_count; ++i) {
        nmo_cli_record_t *row = row_new(c, objects[i]);
        if (!row) continue;
        if (i >= json_count) {
            nmo_cli_record_omit_json(row);
        }
        if (!nmo_cli_record_array_add(arr, row)) {
            nmo_cli_record_free(row);
        }
    }
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
    const char *sort_key_str;
    bool reverse;
    uint32_t top_n;
} object_list_opts_t;

/* The object list report of what `query` matches, sorted and cut to --top
 * when requested. In a session --sort and the class filter note are left
 * out, and the text table keeps every match. */
static nmo_cli_record_t *object_list_record_new(const nmo_cmd_ctx_t *c,
                                                const nmo_object_query_t *query,
                                                const object_list_opts_t *opts,
                                                bool in_session)
{
    obj_collect_t col = {0};
    nmo_core_iter_result_t result = {0};
    nmo_core_object_query_run(c, query, obj_collect_visitor, &col, &result);

    nmo_cli_sort_key_t sort_key = in_session
        ? NMO_CLI_SORT_NONE
        : nmo_cli_parse_sort_key(opts->sort_key_str);
    if (sort_key != NMO_CLI_SORT_NONE && col.count > 1) {
        s_sort_ctx = c;
        s_sort_reverse = opts->reverse;
        obj_compare_fn cmp = obj_sort_comparator(sort_key);
        if (cmp) {
            qsort(col.objects, col.count, sizeof(nmo_object_t *), cmp);
        }
    }

    size_t output_count = col.count;
    if (opts->top_n > 0 && (size_t)opts->top_n < output_count) {
        output_count = (size_t)opts->top_n;
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    nmo_cli_record_raw_fmt(rec, "Objects: %zu", result.matched);
    if (!in_session && opts->class_filter_str) {
        nmo_cli_record_raw_fmt(rec, " (filtered by class: %s)", opts->class_filter_str);
    }
    if (opts->top_n > 0) {
        nmo_cli_record_raw_fmt(rec, " (showing top %u)", opts->top_n);
    }
    nmo_cli_record_raw(rec, "\n\n");
    object_add_rows(c, rec, "count", "objects", object_list_columns,
                    sizeof(object_list_columns) / sizeof(object_list_columns[0]),
                    col.objects, output_count,
                    in_session ? col.count : output_count, object_list_row_new);
    free(col.objects);
    return rec;
}

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
    object_list_opts_t list_opts = {0};
    if (opts) {
        list_opts = *opts;
    }

    nmo_context_t *ctx = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    char *open_error = NULL;

    if (!nmo_tool_open_document(file_path, &ctx, &document, &workspace, &open_error)) {
        fprintf(stderr, "Error: %s\n", open_error ? open_error : "Failed to open file");
        free(open_error);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Build a lightweight cmd_ctx for core helpers */
    nmo_cmd_ctx_t c;
    nmo_cmd_ctx_init_from_repl_document(&c, ctx, document, workspace,
                                        text_ctx ? text_ctx->colorize : false);
    c.global = global;
    c.is_json = (doc != NULL);
    c.file_path = file_path;
    c.out = (text_ctx && text_ctx->out) ? text_ctx->out : stdout;
    int rc = NMO_CLI_EXIT_SUCCESS;

    /* Build query */
    nmo_object_query_t query = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = list_opts.class_filter_str,
        .include_derived_classes = true,
    };
    rc = nmo_core_query_build(&c, &query, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        nmo_tool_close_document(ctx, document, workspace);
        return rc;
    }

    nmo_cli_record_t *rec = object_list_record_new(&c, &query, &list_opts, false);
    if (!rec) {
        rc = NMO_CLI_EXIT_INTERNAL_ERROR;
    } else if (doc && data) {
        if (!nmo_cli_record_to_json(rec, doc, data)) {
            rc = NMO_CLI_EXIT_INTERNAL_ERROR;
        }
    } else {
        nmo_cli_record_print_kv(rec, c.out, 0, c.colorize);
    }
    nmo_cli_record_free(rec);

    nmo_tool_close_document(ctx, document, workspace);
    return rc;
}

int nmo_cmd_object_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--class",   "-c", NMO_OPT_STRING, "Filter by class name"},
        {"--sort",    "-s",  NMO_OPT_STRING, "Sort by: id, name, class, size"},
        {"--reverse", "-r",  NMO_OPT_FLAG,   "Reverse sort direction"},
        {"--top",     NULL,  NMO_OPT_UINT,   "Show only first N results"},
    };
    nmo_opt_val_t vals[4];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 4, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *class_filter_str = vals[0].present ? vals[0].val.str : NULL;
    const char *sort_key_str     = vals[1].present ? vals[1].val.str : NULL;
    bool reverse                 = vals[2].present && vals[2].val.flag;
    uint32_t top_n               = vals[3].present ? vals[3].val.u : 0;

    /* Validate sort key early */
    nmo_cli_sort_key_t sort_key = nmo_cli_parse_sort_key(sort_key_str);
    if (sort_key_str && sort_key == NMO_CLI_SORT_NONE) {
        fprintf(stderr, "Error: Invalid sort key '%s' (use: id, name, class, size)\n", sort_key_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    object_list_opts_t list_opts = {
        .class_filter_str = class_filter_str,
        .sort_key_str = sort_key_str,
        .reverse = reverse,
        .top_n = top_n,
    };

    /* Batch mode */
    if (global->batch_mode) {
        static const char *const value_opts[] = {
            "--class", "-c", "--sort", "-s", "--top",
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

        return nmo_tool_batch_run(paths, count, global, "object.list",
                                  object_list_single, &list_opts);
    }

    /* Single file mode */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Build query */
    nmo_object_query_t query = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = class_filter_str,
        .include_derived_classes = true,
    };
    rc = nmo_core_query_build(&c, &query, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, rc);
    }

    nmo_cli_record_t *rec = object_list_record_new(&c, &query, &list_opts, false);
    rc = nmo_cmd_ctx_emit_record(&c, rec, "object.list", 0, c.colorize);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * object tree - Ownership-based hierarchy
 *
 * Builds a tree by analyzing reference fields to determine object ownership:
 *   - Forward ownership: CKBeObject.scripts, CKBehavior.sub_behaviors,
 *     inputs, outputs, in_parameters, out_parameters, local_parameters, etc.
 *   - Reverse ownership: CK3dEntity.parent (child points to parent)
 *   - All other references (mesh_ids, material, texture, etc.) are not ownership.
 * ============================================================================ */

/** Create a tree node for an object (user_data only, no children yet) */
static nmo_cli_tree_node_t *create_tree_node(nmo_object_t *obj, nmo_arena_t *arena)
{
    nmo_cli_tree_node_t *node = nmo_arena_alloc(arena, sizeof(*node), _Alignof(nmo_cli_tree_node_t));
    if (!node) return NULL;
    node->label = NULL;
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

/*
 * One tree node and its subtree. JSON: id, class_id, class_name?, name?,
 * child_count, and children when there are any. Text: the node's line in the
 * nmo_cli_print_tree layout, under `prefix` (NULL: a root, printed without a
 * connector), then the lines of its children.
 */
static nmo_cli_record_t *object_tree_node_new(nmo_context_t *ctx,
                                              const nmo_cli_tree_node_t *node,
                                              const char *prefix, bool is_last,
                                              bool colorize)
{
    nmo_object_t *obj = (nmo_object_t *)node->user_data;
    nmo_object_id_t id = nmo_object_get_id(obj);
    nmo_class_id_t class_id = nmo_object_get_class_id(obj);
    const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
    const char *name = nmo_object_get_name(obj);

    size_t child_count = 0;
    for (const nmo_cli_tree_node_t *c = node->first_child; c; c = c->next_sibling) {
        child_count++;
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec) return NULL;
    nmo_cli_record_uint(rec, "id", NULL, id);
    nmo_cli_record_uint(rec, "class_id", NULL, class_id);
    nmo_cli_record_str_opt(rec, "class_name", NULL, class_name, NULL);
    nmo_cli_record_str_opt(rec, "name", NULL, name, NULL);
    nmo_cli_record_uint(rec, "child_count", NULL, (uint64_t)child_count);
    nmo_cli_record_raw_fmt(rec, "%s%s%s%u: %s [%s]%s\n",
                           prefix ? prefix : "",
                           prefix ? (is_last ? "`-- " : "|-- ") : "",
                           colorize ? NMO_CLI_COLOR_CYAN : "",
                           id, (name && name[0]) ? name : "(unnamed)",
                           class_name ? class_name : "?",
                           colorize ? NMO_CLI_COLOR_RESET : "");

    nmo_cli_record_array_t *children = nmo_cli_record_array(rec, "children", NULL);
    nmo_cli_record_array_omit_empty(children);
    nmo_cli_record_array_omit_heading(children);
    nmo_cli_record_array_inline_items(children);
    if (child_count == 0) {
        return rec;
    }

    /* Children of a root sit at column 0; deeper ones extend the prefix. */
    char *child_prefix = NULL;
    if (prefix) {
        size_t prefix_len = strlen(prefix);
        child_prefix = (char *)malloc(prefix_len + 5u);
        if (!child_prefix) {
            nmo_cli_record_free(rec);
            return NULL;
        }
        memcpy(child_prefix, prefix, prefix_len);
        memcpy(child_prefix + prefix_len, is_last ? "    " : "|   ", 5u);
    }
    for (const nmo_cli_tree_node_t *c = node->first_child; c; c = c->next_sibling) {
        nmo_cli_record_t *child = object_tree_node_new(
            ctx, c, child_prefix ? child_prefix : "", c->next_sibling == NULL,
            colorize);
        if (child && !nmo_cli_record_array_add(children, child)) {
            nmo_cli_record_free(child);
        }
    }
    free(child_prefix);
    return rec;
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
    if (!nmo_tool_owner_build_hierarchy(c.ctx, c.workspace, &hierarchy)) {
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
    nmo_arena_t *tree_arena = nmo_tool_owner_arena(c.workspace);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_id_t id = nmo_object_get_id(objects[i]);
        if (id > 0 && id < map_size) {
            node_map[id] = create_tree_node(objects[i], tree_arena);
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
    nmo_cli_record_t *rec = nmo_cli_record_new();
    nmo_cli_record_uint(rec, "total_objects", NULL, (uint64_t)object_count);
    nmo_cli_record_uint(rec, "root_objects", NULL, (uint64_t)root_count);
    nmo_cli_record_raw_fmt(rec, "Object Tree: %zu objects (%zu roots)\n\n",
                           object_count, root_count);
    nmo_cli_record_array_t *roots = nmo_cli_record_array(rec, "roots", NULL);
    nmo_cli_record_array_omit_heading(roots);
    nmo_cli_record_array_inline_items(roots);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_id_t id = nmo_object_get_id(objects[i]);
        if (id == 0 || id >= map_size || !node_map[id]) continue;
        if (parent_of[id] > 0 && parent_of[id] < map_size && node_map[parent_of[id]]) continue;

        nmo_cli_record_t *root = object_tree_node_new(c.ctx, node_map[id], NULL,
                                                      true, c.colorize);
        nmo_cli_record_raw(root, "\n");
        if (root && !nmo_cli_record_array_add(roots, root)) {
            nmo_cli_record_free(root);
        }
    }

    nmo_object_hierarchy_free(&hierarchy);
    free(node_map);
    free(col.objects);
    rc = nmo_cmd_ctx_emit_record(&c, rec, "object.tree", 0, false);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * object show
 * ============================================================================ */

typedef struct object_show_args {
    const char *select_paths[64];
    size_t select_path_count;
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

    /* Pass 1: collect --select, build cleaned argv */
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

/* The semantic + reflection summary of `target`, spliced into its report. */
typedef struct {
    const nmo_cmd_ctx_t *c;
    nmo_object_t *target;
    const object_show_args_t *args;
} object_show_summary_t;

static nmo_summary_config_t object_show_summary_config(const object_show_args_t *args,
                                                       uint32_t text_preview_max)
{
    nmo_summary_config_t cfg = nmo_summary_config_default();
    if (args->full_mode) {
        cfg.max_depth = 8;
        cfg.array_preview_max = 64;
        cfg.text_preview_max = text_preview_max;
    }
    if (args->depth >= 0) {
        cfg.max_depth = (uint32_t)args->depth;
    }
    return cfg;
}

static bool object_show_summary_json(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                     const void *data)
{
    const object_show_summary_t *sum = (const object_show_summary_t *)data;
    const object_show_args_t *args = sum->args;
    nmo_summary_config_t cfg = object_show_summary_config(args, 64);

    yyjson_mut_val *summary = yyjson_mut_obj(doc);
    nmo_summary_output_t sum_out = {
        .stream = sum->c->out,
        .json_doc = doc,
        .json_data = summary,
        .is_json = true,
        .colorize = false,
        .ctx = sum->c->ctx,
    };
    nmo_tool_owner_summary_output_bind(&sum_out, sum->c->workspace);
    bool ok = false;
    if (args->select_path_count > 0) {
        ok |= nmo_object_summary_select_with_config(sum->target, &sum_out, &cfg,
                                                    args->select_paths,
                                                    args->select_path_count);
    }
    if (args->select_path_count == 0) {
        ok |= nmo_object_summary_with_config(sum->target, &sum_out, &cfg);
    }
    if (ok) {
        yyjson_mut_obj_add_val(doc, obj, "summary", summary);
    }
    return true;
}

static void object_show_summary_text(FILE *out, bool colorize, const void *data)
{
    const object_show_summary_t *sum = (const object_show_summary_t *)data;
    const object_show_args_t *args = sum->args;
    nmo_summary_config_t cfg = object_show_summary_config(args, 32);

    nmo_summary_output_t sum_out = {
        .stream = out,
        .json_doc = NULL,
        .json_data = NULL,
        .is_json = false,
        .colorize = colorize,
        .ctx = sum->c->ctx,
    };
    nmo_tool_owner_summary_output_bind(&sum_out, sum->c->workspace);
    if (args->select_path_count > 0) {
        (void)nmo_object_summary_select_with_config(sum->target, &sum_out, &cfg,
                                                    args->select_paths,
                                                    args->select_path_count);
    }
    if (args->select_path_count == 0) {
        (void)nmo_object_summary_with_config(sum->target, &sum_out, &cfg);
    }
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

    nmo_cli_record_t *rec = nmo_cli_record_new();
    nmo_cli_record_title(rec, "Object Details");
    nmo_cli_record_uint(rec, "id", "ID / Name", object_id);
    nmo_cli_record_set_text_fmt(rec, "#%u (%s)", object_id,
                                (name && name[0]) ? name : "(unnamed)");
    nmo_cli_record_uint(rec, "class_id", "Class", class_id);
    nmo_cli_record_set_text_fmt(rec, "#%u (%s)", class_id,
                                class_name ? class_name : "-");
    nmo_cli_record_str_opt(rec, "class_name", NULL, class_name, NULL);
    nmo_cli_record_str_opt(rec, "name", NULL, name, NULL);
    nmo_cli_record_uint(rec, "flags", "Flags", flags);
    nmo_cli_record_set_text_fmt(rec, "0x%08X", flags);

    /* Chunk info */
    nmo_chunk_t *chunk = nmo_object_get_chunk(target);
    if (chunk) {
        nmo_cli_record_heading(rec, "Chunk");
        nmo_cli_record_t *chunk_rec = nmo_cli_record_object(rec, "chunk");
        size_t data_size = nmo_chunk_get_data_size(chunk);
        nmo_cli_record_uint(chunk_rec, "data_size", "Data Size", (uint64_t)data_size);
        nmo_cli_record_set_text_fmt(chunk_rec, "%zu bytes", data_size);
        nmo_cli_record_uint(chunk_rec, "pack_size", "Pack Size",
                            (uint64_t)chunk->compressed_size);
        nmo_cli_record_set_text_fmt(chunk_rec, "%zu bytes", chunk->compressed_size);
    }

    object_show_summary_t summary = { .c = &c, .target = target, .args = args };
    nmo_cli_record_json(rec, object_show_summary_json, &summary);
    nmo_cli_record_text_splice(rec, object_show_summary_text, &summary);

    rc = nmo_cmd_ctx_emit_record(&c, rec, "object.show", 14, c.colorize);
    return close_ctx ? nmo_cmd_ctx_done(&c, rc) : rc;
}

int nmo_cmd_object_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    object_show_args_t args;
    const char *usage =
        "nmo object show [--select <path>]... "
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
        "object show [--select <path>]... "
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
        "show [--select <path>]... "
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

/* The object find report of what `query` matches. In a session the query
 * object and the filter notes are left out. */
static nmo_cli_record_t *object_find_record_new(const nmo_cmd_ctx_t *c,
                                                const nmo_object_query_t *query,
                                                const char *class_filter,
                                                const char *name_filter,
                                                bool in_session)
{
    obj_collect_t col = {0};
    nmo_core_iter_result_t result = {0};
    nmo_core_object_query_run(c, query, obj_collect_visitor, &col, &result);

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!in_session) {
        nmo_cli_record_t *query_rec = nmo_cli_record_object(rec, "query");
        if (class_filter) {
            nmo_cli_record_str(query_rec, "class_name", NULL, class_filter);
        }
        if (name_filter) {
            nmo_cli_record_str(query_rec, "name_pattern", NULL, name_filter);
        }
    }
    nmo_cli_record_raw_fmt(rec, "Found: %zu objects", result.matched);
    if (!in_session && class_filter) {
        nmo_cli_record_raw_fmt(rec, " (class: %s)", class_filter);
    }
    if (!in_session && name_filter) {
        nmo_cli_record_raw_fmt(rec, " (name: %s)", name_filter);
    }
    nmo_cli_record_raw(rec, "\n\n");
    object_add_rows(c, rec, "match_count", "matches", object_find_columns,
                    sizeof(object_find_columns) / sizeof(object_find_columns[0]),
                    col.objects, col.count, col.count, object_find_row_new);
    free(col.objects);
    return rec;
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
    rc = nmo_core_query_build(&c, &query, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, rc);
    }

    nmo_cli_record_t *rec = object_find_record_new(&c, &query, class_filter_str,
                                                   name_filter, false);
    rc = nmo_cmd_ctx_emit_record(&c, rec, "object.find", 0, c.colorize);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * object export - Importable semantic snapshot export
 * ============================================================================ */

/* One exported object's summary, spliced into its report item. */
typedef struct {
    const nmo_cmd_ctx_t *c;
    nmo_object_t *obj;
    const nmo_summary_config_t *cfg;
} object_export_item_t;

static bool object_export_fields_json(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                      const void *data)
{
    const object_export_item_t *item = (const object_export_item_t *)data;
    yyjson_mut_val *fields_holder = yyjson_mut_obj(doc);
    nmo_summary_output_t sum_out = {
        .stream = item->c->out,
        .json_doc = doc,
        .json_data = fields_holder,
        .is_json = true,
        .colorize = false,
        .ctx = item->c->ctx,
    };
    nmo_tool_owner_summary_output_bind(&sum_out, item->c->workspace);
    if (nmo_object_summary_with_config(item->obj, &sum_out, item->cfg)) {
        yyjson_mut_val *fields = yyjson_mut_obj_get(fields_holder, "fields");
        yyjson_mut_obj_add_val(doc, obj, "fields",
                               fields ? fields : yyjson_mut_arr(doc));
    }
    return true;
}

static void object_export_summary_text(FILE *out, bool colorize, const void *data)
{
    const object_export_item_t *item = (const object_export_item_t *)data;
    nmo_summary_output_t sum_out = {
        .stream = out,
        .json_doc = NULL,
        .json_data = NULL,
        .is_json = false,
        .colorize = colorize,
        .ctx = item->c->ctx,
    };
    nmo_tool_owner_summary_output_bind(&sum_out, item->c->workspace);
    (void)nmo_object_summary_with_config(item->obj, &sum_out, item->cfg);
}

int nmo_cmd_object_export(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--class",  "-c", NMO_OPT_STRING, "Filter by class name"},
        {"--name",   "-n", NMO_OPT_STRING, "Filter by name pattern"},
        {"--depth",  "-d", NMO_OPT_UINT,   "Recursion depth (default: 4)"},
        {"--full",   NULL, NMO_OPT_FLAG,   "Full detail mode for text output (depth 8)"},
        {"--id",     NULL, NMO_OPT_UINT,   "Export specific object by ID"},
    };
    enum { OPT_CLASS, OPT_NAME, OPT_DEPTH, OPT_FULL, OPT_ID, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *class_filter_str = vals[OPT_CLASS].present ? vals[OPT_CLASS].val.str : NULL;
    const char *name_pattern     = vals[OPT_NAME].present ? vals[OPT_NAME].val.str : NULL;
    int depth                    = vals[OPT_DEPTH].present ? (int)vals[OPT_DEPTH].val.u : -1;
    bool full_mode               = vals[OPT_FULL].present && vals[OPT_FULL].val.flag;
    uint32_t id_filter           = vals[OPT_ID].present ? vals[OPT_ID].val.u : 0;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Build query */
    nmo_object_query_t query = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = class_filter_str,
        .name_wildcard = name_pattern,
        .include_derived_classes = true,
        .has_object_id = id_filter != 0,
        .object_id = id_filter,
    };
    rc = nmo_core_query_build(&c, &query, &query_opts);
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

    object_export_item_t *items =
        (object_export_item_t *)calloc(col.count ? col.count : 1u, sizeof(*items));
    if (!items) {
        free(col.objects);
        fprintf(stderr, "Error: Out of memory\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    nmo_cli_record_uint(rec, "count", NULL, (uint64_t)col.count);
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, "objects", NULL);
    nmo_cli_record_array_omit_heading(arr);
    nmo_cli_record_array_inline_items(arr);
    for (size_t i = 0; i < col.count; i++) {
        nmo_object_t *obj = col.objects[i];
        nmo_object_id_t oid = nmo_object_get_id(obj);
        nmo_class_id_t cid = nmo_object_get_class_id(obj);
        const char *cn = nmo_core_class_name(&c, cid);
        const char *oname = nmo_object_get_name(obj);
        items[i] = (object_export_item_t){ .c = &c, .obj = obj, .cfg = &cfg };

        nmo_cli_record_t *item = nmo_cli_record_new();
        if (i > 0) nmo_cli_record_raw(item, "\n");
        nmo_cli_record_title_fmt(item, "[%zu/%zu] #%u %s (%s)",
                                 i + 1, col.count, oid,
                                 (oname && oname[0]) ? oname : "(unnamed)",
                                 cn ? cn : "?");
        nmo_cli_record_uint(item, "id", NULL, oid);
        nmo_cli_record_uint(item, "class_id", NULL, cid);
        nmo_cli_record_str_opt(item, "class_name", NULL, cn, NULL);
        nmo_cli_record_str_opt(item, "name", NULL, oname, NULL);
        nmo_cli_record_json(item, object_export_fields_json, &items[i]);
        nmo_cli_record_text_splice(item, object_export_summary_text, &items[i]);
        if (item && !nmo_cli_record_array_add(arr, item)) {
            nmo_cli_record_free(item);
        }
    }

    if (col.count == 0) {
        nmo_cli_record_raw(rec, "No objects matched.\n");
    } else {
        nmo_cli_record_raw_fmt(rec, "\n%zu object(s) exported.\n", col.count);
    }

    rc = nmo_cmd_ctx_emit_record(&c, rec, "object.export", 0, false);
    free(items);
    free(col.objects);
    return nmo_cmd_ctx_done(&c, rc);
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

    const char *type_name = type->name ? type->name : "<unnamed>";
    nmo_cli_record_t *rec = nmo_cli_record_new();
    nmo_cli_record_uint(rec, "id", NULL, object_id);
    nmo_cli_record_uint(rec, "class_id", NULL, nmo_object_get_class_id(obj));
    nmo_cli_record_str(rec, "class_name", NULL, type_name);
    nmo_cli_record_uint(rec, "field_count", NULL, type->field_count);
    nmo_cli_record_raw_fmt(rec, "Object #%u (%s) -- %zu fields:\n",
                           object_id, type_name, type->field_count);

    nmo_cli_record_array_t *fields = nmo_cli_record_array(rec, "fields", NULL);
    nmo_cli_record_array_omit_heading(fields);
    for (size_t i = 0; i < type->field_count; i++) {
        const nmo_type_field_t *field = &type->fields[i];
        const nmo_type_descriptor_t *ftype =
            nmo_type_registry_find_by_guid(
                (nmo_type_registry_t *)c->registry, field->type_guid);

        char *value = ftype ? nmo_core_field_dup(state, type, field, c->registry) : NULL;
        const char *field_name = field->name ? field->name : "<unnamed>";
        const char *ftype_name = ftype && ftype->name ? ftype->name : "???";
        const char *shown = (value && value[0]) ? value : "(empty)";

        nmo_cli_record_t *item = nmo_cli_record_new();
        nmo_cli_record_uint(item, "index", NULL, i);
        nmo_cli_record_str(item, "name", NULL, field_name);
        nmo_cli_record_str(item, "type", NULL, ftype_name);
        nmo_cli_record_str(item, "value", NULL, shown);
        nmo_cli_record_set_summary_fmt(item, "  %-30s %-20s = %s",
                                       field_name, ftype_name, shown);
        free(value);
        if (item && !nmo_cli_record_array_add(fields, item)) {
            nmo_cli_record_free(item);
        }
    }

    return nmo_cmd_ctx_emit_record(c, rec, "object.list-fields", 0, false);
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
        {"--sort",    "-s", NMO_OPT_STRING, "Sort by: id, name, class, size"},
        {"--reverse", "-r", NMO_OPT_FLAG,   "Reverse sort direction"},
        {"--top",     NULL, NMO_OPT_UINT,   "Show only first N results"},
    };
    nmo_opt_val_t vals[4];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 4, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    object_list_opts_t list_opts = {
        .class_filter_str = vals[0].present ? vals[0].val.str : NULL,
        .top_n = vals[3].present ? vals[3].val.u : 0,
    };

    nmo_object_query_t query = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = list_opts.class_filter_str,
        .include_derived_classes = true,
    };
    int rc = nmo_core_query_build(ctx, &query, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_cli_record_t *rec = object_list_record_new(ctx, &query, &list_opts, true);
    return nmo_cmd_ctx_emit_record(ctx, rec, "object.list", 0, ctx->colorize);
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
    for (int i = 1; i < argc; i++) {
        merged[i] = argv[i];
    }
    merged[argc] = "--class";
    merged[argc + 1] = (char *)class_name;

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
    int rc = nmo_core_query_build(ctx, &query, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) return rc;

    nmo_cli_record_t *rec = object_find_record_new(ctx, &query, class_filter_str,
                                                   name_filter, true);
    return nmo_cmd_ctx_emit_record(ctx, rec, "object.find", 0, ctx->colorize);
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
    for (int i = 1; i < argc; i++) {
        merged[i] = argv[i];
    }
    merged[argc] = "--class";
    merged[argc + 1] = (char *)class_name;

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

