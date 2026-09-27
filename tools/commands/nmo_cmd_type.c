/**
 * @file nmo_cmd_type.c
 * @brief CLI type command group implementation
 */

#include "nmo_cmd_type.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "runtime/nmo_context.h"
#include "type/nmo_type_system.h"
#include "nmo_tool_common.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct {
    nmo_class_id_t class_id;
    const char *name;
    nmo_class_id_t parent_id;
    const char *parent_name;
} nmo_cli_class_entry_t;

static int compare_class_entry(const void *a, const void *b) {
    const nmo_cli_class_entry_t *ea = (const nmo_cli_class_entry_t *)a;
    const nmo_cli_class_entry_t *eb = (const nmo_cli_class_entry_t *)b;
    if (ea->class_id < eb->class_id) {
        return -1;
    }
    if (ea->class_id > eb->class_id) {
        return 1;
    }
    if (!ea->name && !eb->name) {
        return 0;
    }
    if (!ea->name) {
        return -1;
    }
    if (!eb->name) {
        return 1;
    }
    return nmo_tool_stricmp(ea->name, eb->name);
}

static nmo_cli_class_entry_t *collect_class_entries(nmo_context_t *ctx, size_t *out_count) {
    if (out_count) {
        *out_count = 0;
    }
    if (!ctx || !out_count) {
        return NULL;
    }

    nmo_type_registry_t *registry = nmo_context_get_type_registry(ctx);
    if (!registry) {
        return NULL;
    }

    size_t type_count = nmo_type_registry_get_type_count(registry);
    if (type_count == 0) {
        return NULL;
    }

    nmo_cli_class_entry_t *entries = (nmo_cli_class_entry_t *)malloc(type_count * sizeof(*entries));
    if (!entries) {
        return NULL;
    }

    size_t count = 0;
    for (nmo_type_id_t id = 0; id < (nmo_type_id_t)type_count; ++id) {
        const nmo_type_descriptor_t *desc = nmo_type_registry_get_by_id(registry, id);
        if (!desc || !desc->valid || desc->class_id == 0 || !desc->name) {
            continue;
        }

        nmo_class_id_t class_id = (nmo_class_id_t)desc->class_id;
        entries[count].class_id = class_id;
        entries[count].name = desc->name;
        entries[count].parent_id = nmo_cli_class_get_parent(ctx, class_id);
        entries[count].parent_name = entries[count].parent_id
            ? nmo_cli_class_name_from_id(ctx, entries[count].parent_id)
            : NULL;
        count++;
    }

    if (count == 0) {
        free(entries);
        return NULL;
    }

    qsort(entries, count, sizeof(*entries), compare_class_entry);
    *out_count = count;
    return entries;
}

static yyjson_mut_val *build_class_tree_node(yyjson_mut_doc *doc,
                                             const nmo_cli_class_entry_t *list,
                                             size_t count,
                                             nmo_class_id_t class_id) {
    const nmo_cli_class_entry_t *entry = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (list[i].class_id == class_id) {
            entry = &list[i];
            break;
        }
    }
    if (!entry) {
        return NULL;
    }

    yyjson_mut_val *node = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, node, "id", entry->class_id);
    yyjson_mut_obj_add_str(doc, node, "name", entry->name);
    if (entry->parent_id) {
        yyjson_mut_obj_add_uint(doc, node, "parent_id", entry->parent_id);
        if (entry->parent_name) {
            yyjson_mut_obj_add_str(doc, node, "parent_name", entry->parent_name);
        }
    }

    yyjson_mut_val *children = yyjson_mut_arr(doc);
    bool has_children = false;
    for (size_t i = 0; i < count; ++i) {
        if (list[i].parent_id == class_id) {
            yyjson_mut_val *child = build_class_tree_node(doc, list, count, list[i].class_id);
            if (child) {
                yyjson_mut_arr_add_val(children, child);
                has_children = true;
            }
        }
    }
    if (has_children) {
        yyjson_mut_obj_add_val(doc, node, "children", children);
    }

    return node;
}

/* ============================================================================
 * type list
 * ============================================================================ */

/* One registered class for type list. */
static bool type_class_build_record(const nmo_cli_class_entry_t *entry,
                                    nmo_cli_record_t *rec)
{
    bool ok = nmo_cli_record_uint(rec, "id", "ID", entry->class_id) &&
              nmo_cli_record_str(rec, "name", "Class Name", entry->name);
    if (ok && entry->parent_id) {
        ok = nmo_cli_record_uint(rec, "parent_id", NULL, entry->parent_id);
        if (ok && entry->parent_name) {
            return nmo_cli_record_str(rec, "parent_name", "Parent", entry->parent_name);
        }
    }
    return ok && nmo_cli_record_text(rec, "Parent", "-");
}

/* JSON: id, name, parent_id, parent_name; text: ID, Class Name, Parent. */
static const nmo_cli_table_col_t type_list_columns[] = {
    {"ID", NMO_CLI_ALIGN_RIGHT, 4, 0},
    {"Class Name", NMO_CLI_ALIGN_LEFT, 20, 30},
    {"Parent", NMO_CLI_ALIGN_LEFT, 20, 30},
};

int nmo_cmd_type_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    (void)argc;
    (void)argv;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (rc) return rc;

    /* For type list, we don't need a file - we use a temporary context */
    nmo_context_t *ctx = nmo_context_create(NULL);
    if (!ctx) {
        fprintf(stderr, "Error: Failed to create context\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    size_t class_count = 0;
    nmo_cli_class_entry_t *entries = collect_class_entries(ctx, &class_count);

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_title(rec, "Registered Classes") &&
              nmo_cli_record_raw(rec, "\n") &&
              nmo_cli_record_uint(rec, "count", NULL, class_count);
    nmo_cli_record_array_t *classes =
        ok ? nmo_cli_record_array(rec, "classes", NULL) : NULL;
    ok = ok && classes != NULL &&
         nmo_cli_record_array_set_table(
             classes, type_list_columns,
             sizeof(type_list_columns) / sizeof(type_list_columns[0]));
    for (size_t i = 0; ok && i < class_count; ++i) {
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL && type_class_build_record(&entries[i], item);
        if (!ok) {
            nmo_cli_record_free(item);
        } else {
            ok = nmo_cli_record_array_add(classes, item);
        }
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    rc = nmo_cmd_ctx_emit_record(&c, rec, "type.list", 0, c.colorize);

    free(entries);
    nmo_context_release(ctx);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * type show
 * ============================================================================ */

int nmo_cmd_type_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Find class name or ID */
    const char *type_arg = NULL;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            type_arg = argv[i];
            break;
        }
    }

    if (!type_arg) {
        fprintf(stderr, "Error: No type specified\n");
        fprintf(stderr, "Usage: nmo type show <class-name-or-id>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Create context for type lookups */
    nmo_context_t *ctx = nmo_context_create(NULL);
    if (!ctx) {
        fprintf(stderr, "Error: Failed to create context\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Try to find class by name first, then by ID */
    nmo_class_id_t class_id = nmo_cli_class_id_from_name(ctx, type_arg);
    if (!class_id) {
        /* Try parsing as ID */
        uint32_t id;
        if (nmo_tool_parse_u32(type_arg, &id)) {
            class_id = (nmo_class_id_t)id;
        }
    }

    if (!class_id) {
        nmo_context_release(ctx);
        fprintf(stderr, "Error: Unknown class '%s'\n", type_arg);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *class_name = nmo_cli_class_name_from_id(ctx, class_id);
    if (!class_name) {
        nmo_context_release(ctx);
        fprintf(stderr, "Error: Class ID %u not found\n", class_id);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (rc) {
        nmo_context_release(ctx);
        return rc;
    }

    nmo_class_id_t parent_id = nmo_cli_class_get_parent(ctx, class_id);
    const char *parent_name = parent_id ? nmo_cli_class_name_from_id(ctx, parent_id) : NULL;

    /* Inheritance chain, root last: a JSON string list and one text line. */
    size_t chain_len = 0;
    size_t chain_text_len = 0;
    for (nmo_class_id_t cid = class_id; cid; cid = nmo_cli_class_get_parent(ctx, cid)) {
        const char *n = nmo_cli_class_name_from_id(ctx, cid);
        if (n) {
            chain_text_len += strlen(n) + 4u; /* " -> " */
            chain_len++;
        }
    }
    const char **chain = (const char **)calloc(chain_len ? chain_len : 1u, sizeof(*chain));
    char *chain_text = (char *)malloc(chain_text_len + 1u);
    bool ok = chain != NULL && chain_text != NULL;
    if (ok) {
        size_t k = 0;
        size_t pos = 0;
        for (nmo_class_id_t cid = class_id; cid; cid = nmo_cli_class_get_parent(ctx, cid)) {
            const char *n = nmo_cli_class_name_from_id(ctx, cid);
            if (!n) {
                continue;
            }
            chain[k++] = n;
            if (pos > 0) {
                memcpy(chain_text + pos, " -> ", 4u);
                pos += 4u;
            }
            size_t len = strlen(n);
            memcpy(chain_text + pos, n, len);
            pos += len;
        }
        chain_text[pos] = '\0';
    }

    nmo_cli_record_t *rec = ok ? nmo_cli_record_new() : NULL;
    ok = rec != NULL &&
         nmo_cli_record_title(rec, "Class Details") &&
         nmo_cli_record_uint(rec, "id", "ID", class_id) &&
         nmo_cli_record_str(rec, "name", "Name", class_name);
    if (ok && parent_id) {
        ok = nmo_cli_record_uint(rec, "parent_id", "Parent ID", parent_id);
        if (ok && parent_name) {
            ok = nmo_cli_record_str(rec, "parent_name", "Parent Name", parent_name);
        } else if (ok) {
            ok = nmo_cli_record_text(rec, "Parent Name", "-");
        }
    }
    ok = ok && nmo_cli_record_str_list(rec, "inheritance_chain", NULL, chain, chain_len, NULL) &&
         nmo_cli_record_raw(rec, "\nInheritance Chain:\n  ") &&
         nmo_cli_record_raw(rec, chain_text) &&
         nmo_cli_record_raw(rec, "\n");
    free(chain);
    free(chain_text);
    if (!ok) {
        nmo_cli_record_free(rec);
        nmo_context_release(ctx);
        fprintf(stderr, "Error: Out of memory while describing class\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    rc = nmo_cmd_ctx_emit_record(&c, rec, "type.show", 12, c.colorize);

    nmo_context_release(ctx);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * type class-tree
 * ============================================================================ */

typedef struct {
    nmo_cli_tree_node_t node;
    char *label;
} class_node_t;

typedef struct {
    const nmo_cli_class_entry_t *entries;
    size_t count;
    class_node_t *nodes;
} class_tree_t;

/* A root is a class whose parent is not registered. */
static bool class_tree_is_root(const class_tree_t *tree, size_t i)
{
    if (!tree->entries[i].parent_id) {
        return true;
    }
    for (size_t j = 0; j < tree->count; ++j) {
        if (tree->entries[j].class_id == tree->entries[i].parent_id) {
            return false;
        }
    }
    return true;
}

static void class_tree_link(class_tree_t *tree)
{
    for (size_t i = 0; i < tree->count; ++i) {
        const nmo_cli_class_entry_t *entry = &tree->entries[i];
        tree->nodes[i].label = nmo_tool_strdup_fmt("%s (%u)", entry->name, entry->class_id);
        tree->nodes[i].node.label = tree->nodes[i].label ? tree->nodes[i].label : "(alloc failed)";
        tree->nodes[i].node.user_data = (void *)entry;
    }

    for (size_t i = 0; i < tree->count; ++i) {
        if (!tree->entries[i].parent_id) {
            continue;
        }
        for (size_t j = 0; j < tree->count; ++j) {
            if (tree->entries[j].class_id == tree->entries[i].parent_id) {
                nmo_cli_tree_node_t *parent = &tree->nodes[j].node;
                nmo_cli_tree_node_t *child = &tree->nodes[i].node;
                if (!parent->first_child) {
                    parent->first_child = child;
                } else {
                    nmo_cli_tree_node_t *cursor = parent->first_child;
                    while (cursor->next_sibling) {
                        cursor = cursor->next_sibling;
                    }
                    cursor->next_sibling = child;
                }
                break;
            }
        }
    }
}

static bool class_tree_roots_json(yyjson_mut_doc *doc, yyjson_mut_val *obj,
                                  const void *data)
{
    const class_tree_t *tree = (const class_tree_t *)data;
    yyjson_mut_val *roots = yyjson_mut_arr(doc);
    if (!roots) {
        return false;
    }
    for (size_t i = 0; i < tree->count; ++i) {
        if (class_tree_is_root(tree, i)) {
            yyjson_mut_val *node = build_class_tree_node(doc, tree->entries, tree->count,
                                                         tree->entries[i].class_id);
            if (node) {
                yyjson_mut_arr_add_val(roots, node);
            }
        }
    }
    return yyjson_mut_obj_add_val(doc, obj, "roots", roots);
}

static void class_tree_print(FILE *out, bool colorize, const void *data)
{
    const class_tree_t *tree = (const class_tree_t *)data;
    for (size_t i = 0; i < tree->count; ++i) {
        if (class_tree_is_root(tree, i)) {
            nmo_cli_print_tree(&tree->nodes[i].node, out, colorize, NULL);
            fprintf(out, "\n");
        }
    }
}

int nmo_cmd_type_class_tree(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    (void)argc;
    (void)argv;

    nmo_context_t *ctx = nmo_context_create(NULL);
    if (!ctx) {
        fprintf(stderr, "Error: Failed to create context\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    size_t class_count = 0;
    nmo_cli_class_entry_t *entries = collect_class_entries(ctx, &class_count);
    if (!entries || class_count == 0) {
        nmo_context_release(ctx);
        fprintf(stderr, "Error: No class metadata available\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (rc) {
        free(entries);
        nmo_context_release(ctx);
        return rc;
    }

    class_tree_t tree = {
        .entries = entries,
        .count = class_count,
        .nodes = (class_node_t *)calloc(class_count, sizeof(*tree.nodes)),
    };
    if (!tree.nodes) {
        free(entries);
        nmo_context_release(ctx);
        fprintf(stderr, "Error: Out of memory\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    class_tree_link(&tree);

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_uint(rec, "count", NULL, class_count) &&
              nmo_cli_record_json(rec, class_tree_roots_json, &tree) &&
              nmo_cli_record_raw_fmt(rec, "Class Tree: %zu classes\n\n", class_count) &&
              nmo_cli_record_text_splice(rec, class_tree_print, &tree);
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    rc = nmo_cmd_ctx_emit_record(&c, rec, "type.class-tree", 0, c.colorize);

    for (size_t i = 0; i < class_count; ++i) {
        free(tree.nodes[i].label);
    }
    free(tree.nodes);
    free(entries);
    nmo_context_release(ctx);
    return nmo_cmd_ctx_done(&c, rc);
}
