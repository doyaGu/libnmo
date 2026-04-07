/**
 * @file nmo_cmd_type.c
 * @brief CLI type command group implementation
 */

#include "nmo_cmd_type.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "app/nmo_context.h"
#include "type/nmo_type_system.h"

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

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *classes = yyjson_mut_arr(doc);

        for (size_t i = 0; i < class_count; ++i) {
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "id", entries[i].class_id);
            yyjson_mut_obj_add_str(doc, item, "name", entries[i].name);

            if (entries[i].parent_id) {
                yyjson_mut_obj_add_uint(doc, item, "parent_id", entries[i].parent_id);
                if (entries[i].parent_name) {
                    yyjson_mut_obj_add_str(doc, item, "parent_name", entries[i].parent_name);
                }
            }

            yyjson_mut_arr_add_val(classes, item);
        }

        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)class_count);
        yyjson_mut_obj_add_val(doc, data, "classes", classes);

        nmo_cmd_ctx_json_end(&c, doc, data, "type.list");
    } else {
        nmo_cli_print_heading(c.out, "Registered Classes", c.colorize);
        fprintf(c.out, "\n");

        static const nmo_cli_table_col_t columns[] = {
            {"ID", NMO_CLI_ALIGN_RIGHT, 4, 0},
            {"Class Name", NMO_CLI_ALIGN_LEFT, 20, 30},
            {"Parent", NMO_CLI_ALIGN_LEFT, 20, 30},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        for (size_t i = 0; i < class_count; ++i) {
            char id_buf[16];
            snprintf(id_buf, sizeof(id_buf), "%u", entries[i].class_id);

            const char *parent_name = entries[i].parent_name ? entries[i].parent_name : "-";
            const char *cells[] = {id_buf, entries[i].name, parent_name};
            nmo_cli_table_add_row(&table, cells, 3);
        }

        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    free(entries);
    nmo_context_release(ctx);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
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

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "id", class_id);
        yyjson_mut_obj_add_str(doc, data, "name", class_name);

        if (parent_id) {
            yyjson_mut_obj_add_uint(doc, data, "parent_id", parent_id);
            if (parent_name) {
                yyjson_mut_obj_add_str(doc, data, "parent_name", parent_name);
            }
        }

        /* Build inheritance chain */
        yyjson_mut_val *chain = yyjson_mut_arr(doc);
        nmo_class_id_t cid = class_id;
        while (cid) {
            const char *n = nmo_cli_class_name_from_id(ctx, cid);
            if (n) {
                yyjson_mut_arr_add_str(doc, chain, n);
            }
            cid = nmo_cli_class_get_parent(ctx, cid);
        }
        yyjson_mut_obj_add_val(doc, data, "inheritance_chain", chain);

        nmo_cmd_ctx_json_end(&c, doc, data, "type.show");
    } else {
        nmo_cli_print_heading(c.out, "Class Details", c.colorize);

        char buf[32];
        snprintf(buf, sizeof(buf), "%u", class_id);
        nmo_cli_print_kv(c.out, "ID", buf, 12, c.colorize);
        nmo_cli_print_kv(c.out, "Name", class_name, 12, c.colorize);

        if (parent_id) {
            snprintf(buf, sizeof(buf), "%u", parent_id);
            nmo_cli_print_kv(c.out, "Parent ID", buf, 12, c.colorize);
            nmo_cli_print_kv(c.out, "Parent Name", parent_name ? parent_name : "-", 12, c.colorize);
        }

        /* Show inheritance chain */
        fprintf(c.out, "\nInheritance Chain:\n  ");
        nmo_class_id_t cid = class_id;
        bool first = true;
        while (cid) {
            const char *n = nmo_cli_class_name_from_id(ctx, cid);
            if (n) {
                if (!first) fprintf(c.out, " -> ");
                fprintf(c.out, "%s", n);
                first = false;
            }
            cid = nmo_cli_class_get_parent(ctx, cid);
        }
        fprintf(c.out, "\n");
    }

    nmo_context_release(ctx);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * type class-tree
 * ============================================================================ */

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

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, data, "count", (uint64_t)class_count);

        yyjson_mut_val *roots = yyjson_mut_arr(doc);

        for (size_t i = 0; i < class_count; ++i) {
            bool has_parent = false;
            if (entries[i].parent_id) {
                for (size_t j = 0; j < class_count; ++j) {
                    if (entries[j].class_id == entries[i].parent_id) {
                        has_parent = true;
                        break;
                    }
                }
            }

            if (!has_parent) {
                /* Build JSON subtree */
                yyjson_mut_val *node = build_class_tree_node(doc, entries, class_count, entries[i].class_id);
                if (node) {
                    yyjson_mut_arr_add_val(roots, node);
                }
            }
        }

        yyjson_mut_obj_add_val(doc, data, "roots", roots);
        nmo_cmd_ctx_json_end(&c, doc, data, "type.class-tree");
    } else {
        fprintf(c.out, "Class Tree: %zu classes\n\n", class_count);

        typedef struct {
            nmo_cli_tree_node_t node;
            char *label;
        } class_node_t;

        class_node_t *nodes = (class_node_t *)calloc(class_count, sizeof(*nodes));
        if (!nodes) {
            free(entries);
            nmo_context_release(ctx);
            fprintf(stderr, "Error: Out of memory\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        for (size_t i = 0; i < class_count; ++i) {
            char label_buf[128];
            snprintf(label_buf, sizeof(label_buf), "%s (%u)", entries[i].name, entries[i].class_id);
            nodes[i].label = nmo_tool_strdup(label_buf);
            nodes[i].node.label = nodes[i].label ? nodes[i].label : "(alloc failed)";
            nodes[i].node.user_data = (void *)&entries[i];
            nodes[i].node.first_child = NULL;
            nodes[i].node.next_sibling = NULL;
        }

        /* Link children */
        for (size_t i = 0; i < class_count; ++i) {
            if (!entries[i].parent_id) {
                continue;
            }
            for (size_t j = 0; j < class_count; ++j) {
                if (entries[j].class_id == entries[i].parent_id) {
                    nmo_cli_tree_node_t *parent = &nodes[j].node;
                    nmo_cli_tree_node_t *child = &nodes[i].node;
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

        for (size_t i = 0; i < class_count; ++i) {
            bool has_parent = false;
            if (entries[i].parent_id) {
                for (size_t j = 0; j < class_count; ++j) {
                    if (entries[j].class_id == entries[i].parent_id) {
                        has_parent = true;
                        break;
                    }
                }
            }
            if (!has_parent) {
                nmo_cli_print_tree(&nodes[i].node, c.out, c.colorize, NULL);
                fprintf(c.out, "\n");
            }
        }

        for (size_t i = 0; i < class_count; ++i) {
            free(nodes[i].label);
        }
        free(nodes);
    }

    free(entries);
    nmo_context_release(ctx);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

