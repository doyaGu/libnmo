/**
 * @file nmo_cmd_object.c
 * @brief CLI object command group implementation
 */

#include "nmo_cmd_object.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_output.h"
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

/* For strcasecmp */
#ifdef _WIN32
#include <string.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

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

/**
 * Parse --filter expression
 */
static const char *parse_filter_expr(int argc, char **argv) {
    for (int i = 1; i < argc - 1; ++i) {
        if (strcmp(argv[i], "--filter") == 0 || strcmp(argv[i], "-f") == 0) {
            return argv[i + 1];
        }
    }
    return NULL;
}

/**
 * Evaluate a DSL value as truthy/falsy.
 */
static bool dsl_value_is_truthy(const nmo_dsl_value_t *val) {
    switch (val->kind) {
        case NMO_DSL_VALUE_BOOL:   return val->as.b;
        case NMO_DSL_VALUE_INT:    return val->as.i != 0;
        case NMO_DSL_VALUE_UINT:   return val->as.u != 0;
        case NMO_DSL_VALUE_REAL:   return val->as.r != 0.0;
        case NMO_DSL_VALUE_STRING: return val->as.s != NULL && val->as.s[0] != '\0';
        case NMO_DSL_VALUE_NULL:   return false;
        default:                   return true;
    }
}

/**
 * Evaluate a DSL filter expression against a single object.
 * Returns true if the object matches (expression is truthy).
 */
static bool object_matches_filter(
    nmo_object_t *obj,
    const nmo_type_registry_t *registry,
    nmo_dsl_program_t *program)
{
    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (!chunk) {
        return false;
    }

    nmo_type_id_t type_id = nmo_type_registry_class_id_to_type_id(
        registry, (uint32_t)nmo_object_get_class_id(obj));
    const nmo_type_descriptor_t *type_desc = NULL;
    if (type_id != NMO_TYPE_ID_INVALID) {
        type_desc = nmo_type_registry_get_by_id(registry, type_id);
    }

    size_t data_size = 0;
    const void *instance = nmo_chunk_get_data(chunk, &data_size);

    nmo_dsl_eval_context_t eval_ctx;
    memset(&eval_ctx, 0, sizeof(eval_ctx));
    eval_ctx.registry = registry;
    eval_ctx.root_type = type_desc;
    eval_ctx.root_instance = (void *)instance;
    eval_ctx.current_type = type_desc;
    eval_ctx.current_instance = instance;

    nmo_dsl_value_t result = {0};
    nmo_status_t st = nmo_dsl_eval_expr(program, &eval_ctx, &result);

    bool match = (st == NMO_OK) && dsl_value_is_truthy(&result);
    nmo_dsl_value_destroy(&result);
    return match;
}

/* ============================================================================
 * object list
 * ============================================================================ */

int nmo_cmd_object_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *class_filter = parse_class_filter(argc, argv);
    const char *filter_expr = parse_filter_expr(argc, argv);

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

    /* Resolve class filter if specified */
    nmo_class_id_t filter_class_id = 0;
    if (class_filter) {
        filter_class_id = nmo_cli_class_id_from_name(c.ctx, class_filter);
        if (!filter_class_id) {
            fprintf(stderr, "Error: Unknown class '%s'\n", class_filter);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    /* Compile DSL filter if specified */
    nmo_dsl_program_t *filter_program = NULL;
    if (filter_expr) {
        nmo_dsl_compile_options_t opts = { .mode = NMO_DSL_MODE_EXPRESSION };
        nmo_status_t st = nmo_dsl_compile(c.registry, NULL, filter_expr, &opts, &filter_program);
        if (st != NMO_OK) {
            fprintf(stderr, "Error: Failed to compile filter expression: %s\n", filter_expr);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *objs = yyjson_mut_arr(doc);
        size_t filtered_count = 0;

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);

            /* Apply class filter */
            if (filter_class_id && !nmo_cli_class_is_derived_from(c.ctx, class_id, filter_class_id)) {
                continue;
            }

            /* Apply DSL filter */
            if (filter_program && !object_matches_filter(obj, c.registry, filter_program)) {
                continue;
            }

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "id", nmo_object_get_id(obj));
            yyjson_mut_obj_add_uint(doc, item, "class_id", class_id);

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, class_id);
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

        size_t filtered_count = 0;
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);

            /* Apply class filter */
            if (filter_class_id && !nmo_cli_class_is_derived_from(c.ctx, class_id, filter_class_id)) {
                continue;
            }

            /* Apply DSL filter */
            if (filter_program && !object_matches_filter(obj, c.registry, filter_program)) {
                continue;
            }

            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, class_id);
            const char *name = nmo_object_get_name(obj);

            const char *cells[] = {
                id_buf,
                class_name ? class_name : "-",
                (name && name[0]) ? name : "-"
            };
            nmo_cli_table_add_row(&table, cells, 3);
            filtered_count++;
        }

        fprintf(c.out, "Objects: %zu", filtered_count);
        if (class_filter) {
            fprintf(c.out, " (filtered by class: %s)", class_filter);
        }
        if (filter_expr) {
            fprintf(c.out, " (filtered by: %s)", filter_expr);
        }
        fprintf(c.out, "\n\n");

        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    if (filter_program) {
        nmo_dsl_program_destroy(filter_program);
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
    const char *class_filter = parse_class_filter(argc, argv);
    const char *name_filter = parse_name_filter(argc, argv);

    if (!class_filter && !name_filter) {
        fprintf(stderr, "Error: At least one filter required (--name or --class)\n");
        fprintf(stderr, "Usage: nmo object find [--name <pattern>] [--class <name>] <file>\n");
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

    /* Resolve class filter if specified */
    nmo_class_id_t filter_class_id = 0;
    if (class_filter) {
        filter_class_id = nmo_cli_class_id_from_name(c.ctx, class_filter);
        if (!filter_class_id) {
            fprintf(stderr, "Error: Unknown class '%s'\n", class_filter);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
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
            if (filter_class_id && !nmo_cli_class_is_derived_from(c.ctx, class_id, filter_class_id)) {
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

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, class_id);
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

        size_t match_count = 0;
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_class_id_t class_id = nmo_object_get_class_id(obj);

            /* Apply class filter */
            if (filter_class_id && !nmo_cli_class_is_derived_from(c.ctx, class_id, filter_class_id)) {
                continue;
            }

            /* Apply name filter */
            const char *name = nmo_object_get_name(obj);
            if (name_filter && !simple_pattern_match(name_filter, name ? name : "")) {
                continue;
            }

            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", nmo_object_get_id(obj));

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, class_id);

            const char *cells[] = {
                id_buf,
                class_name ? class_name : "-",
                (name && name[0]) ? name : "-"
            };
            nmo_cli_table_add_row(&table, cells, 3);
            match_count++;
        }

        fprintf(c.out, "Found: %zu objects", match_count);
        if (class_filter) {
            fprintf(c.out, " (class: %s)", class_filter);
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
    nmo_object_id_t object_id = parse_object_id(argc, argv);
    if (object_id == 0) {
        fprintf(stderr, "Error: No valid object ID specified\n");
        fprintf(stderr, "Usage: nmo object refs <id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Find the object */
    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    nmo_object_t *obj = nmo_object_repository_find_by_id(repo, object_id);
    if (!obj) {
        fprintf(stderr, "Error: Object %u not found\n", object_id);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Create reference graph */
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    if (!arena) {
        fprintf(stderr, "Error: Failed to create arena\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    nmo_ref_graph_t *graph = nmo_ref_graph_create(repo, c.registry, arena);
    if (!graph) {
        nmo_arena_destroy(arena);
        fprintf(stderr, "Error: Failed to create reference graph\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Get outgoing edges */
    nmo_ref_edge_t *out_edges = NULL;
    size_t out_count = 0;
    nmo_ref_graph_get_object_edges(graph, object_id, NMO_REF_DIR_OUTGOING, &out_edges, &out_count);

    /* Get incoming edges */
    nmo_ref_edge_t *in_edges = NULL;
    size_t in_count = 0;
    nmo_ref_graph_get_object_edges(graph, object_id, NMO_REF_DIR_INCOMING, &in_edges, &in_count);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        /* Object info */
        yyjson_mut_obj_add_uint(doc, data, "id", object_id);
        const char *class_name = nmo_cli_class_name_from_id(c.ctx, nmo_object_get_class_id(obj));
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
            yyjson_mut_obj_add_str(doc, edge, "field",
                                   out_edges[i].field_path ? out_edges[i].field_path : "unknown");
            if (out_edges[i].index > 0) {
                yyjson_mut_obj_add_uint(doc, edge, "index", out_edges[i].index);
            }

            /* Add target object info if available */
            nmo_object_t *target = nmo_object_repository_find_by_id(repo, out_edges[i].to);
            if (target) {
                const char *target_class = nmo_cli_class_name_from_id(c.ctx, nmo_object_get_class_id(target));
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
            yyjson_mut_obj_add_str(doc, edge, "field",
                                   in_edges[i].field_path ? in_edges[i].field_path : "unknown");
            if (in_edges[i].index > 0) {
                yyjson_mut_obj_add_uint(doc, edge, "index", in_edges[i].index);
            }

            /* Add source object info */
            nmo_object_t *source = nmo_object_repository_find_by_id(repo, in_edges[i].from);
            if (source) {
                const char *source_class = nmo_cli_class_name_from_id(c.ctx, nmo_object_get_class_id(source));
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

        nmo_cmd_ctx_json_end(&c, doc, data, "object.refs");
    } else {
        /* Text output */
        const char *obj_name = nmo_object_get_name(obj);
        const char *obj_class = nmo_cli_class_name_from_id(c.ctx, nmo_object_get_class_id(obj));
        fprintf(c.out, "References for object %u: %s [%s]\n\n",
                object_id,
                (obj_name && obj_name[0]) ? obj_name : "(unnamed)",
                obj_class ? obj_class : "?");

        /* Outgoing references */
        fprintf(c.out, "Outgoing references (%zu):\n", out_count);
        if (out_count == 0) {
            fprintf(c.out, "  (none)\n");
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
                const char *field_name = out_edges[i].field_path ? out_edges[i].field_path : "unknown";
                if (out_edges[i].index > 0) {
                    snprintf(field_buf, sizeof(field_buf), "%s[%u]",
                             field_name, out_edges[i].index);
                } else {
                    snprintf(field_buf, sizeof(field_buf), "%s", field_name);
                }

                nmo_object_t *target = nmo_object_repository_find_by_id(repo, out_edges[i].to);
                const char *target_class = "-";
                const char *target_name = "-";

                if (target) {
                    const char *tc = nmo_cli_class_name_from_id(c.ctx, nmo_object_get_class_id(target));
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

            nmo_cli_table_print(&table, c.out, c.colorize);
            nmo_cli_table_free(&table);
        }

        fprintf(c.out, "\n");

        /* Incoming references */
        fprintf(c.out, "Incoming references (%zu):\n", in_count);
        if (in_count == 0) {
            fprintf(c.out, "  (none)\n");
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
                const char *field_name = in_edges[i].field_path ? in_edges[i].field_path : "unknown";
                if (in_edges[i].index > 0) {
                    snprintf(field_buf, sizeof(field_buf), "%s[%u]",
                             field_name, in_edges[i].index);
                } else {
                    snprintf(field_buf, sizeof(field_buf), "%s", field_name);
                }

                nmo_object_t *source = nmo_object_repository_find_by_id(repo, in_edges[i].from);
                const char *source_class = "-";
                const char *source_name = "-";

                if (source) {
                    const char *sc = nmo_cli_class_name_from_id(c.ctx, nmo_object_get_class_id(source));
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

            nmo_cli_table_print(&table, c.out, c.colorize);
            nmo_cli_table_free(&table);
        }
    }

    nmo_arena_destroy(arena);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

