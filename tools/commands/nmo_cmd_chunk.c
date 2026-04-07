/**
 * @file nmo_cmd_chunk.c
 * @brief CLI chunk command group implementation
 */

#include "nmo_cmd_chunk.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "app/nmo_chunk_index.h"
#include "app/nmo_context.h"
#include "app/nmo_hexdump.h"

#include "format/nmo_chunk_api.h"

#include <stdio.h>
#include <string.h>

#include <stdlib.h>


typedef nmo_chunk_index_entry_t nmo_cli_chunk_entry_t;
typedef nmo_chunk_ptr_index_t nmo_cli_chunk_ptr_index_t;

static bool build_chunk_index_map(const nmo_cli_chunk_entry_t *entries,
                                  size_t entry_count,
                                  nmo_cli_chunk_ptr_index_t **out_map,
                                  size_t *out_map_count)
{
    return nmo_chunk_index_build_map(entries, entry_count, out_map, out_map_count);
}

static bool lookup_chunk_index(const nmo_cli_chunk_ptr_index_t *map,
                               size_t map_count,
                               const nmo_chunk_t *chunk,
                               uint32_t *out_index)
{
    return nmo_chunk_index_lookup(map, map_count, chunk, out_index);
}

static nmo_cli_tree_node_t *build_chunk_tree_node(nmo_context_t *ctx,
                                                  nmo_chunk_t *chunk,
                                                  nmo_arena_t *arena)
{
    if (!chunk || !arena) {
        return NULL;
    }

    nmo_cli_tree_node_t *node = nmo_arena_alloc(arena, sizeof(*node), _Alignof(nmo_cli_tree_node_t));
    if (!node) {
        return NULL;
    }

    const char *class_name = nmo_cli_class_name_from_id(ctx, chunk->class_id);
    uint32_t sub_count = nmo_chunk_get_sub_chunk_count(chunk);

    char *label = nmo_arena_alloc(arena, 256, 1);
    if (label) {
        char opt_buf[96];
        const char *opt = nmo_cli_chunk_options_to_string(chunk->chunk_options,
                                                          opt_buf,
                                                          sizeof(opt_buf));
        snprintf(label, 256, "%s (cid=%u size=%zu opt=%s sub=%u)",
                 class_name ? class_name : "(unknown)",
                 chunk->class_id,
                 nmo_chunk_get_data_size(chunk),
                 opt,
                 sub_count);
    }

    node->label = label ? label : "(alloc failed)";
    node->user_data = chunk;
    node->first_child = NULL;
    node->next_sibling = NULL;

    nmo_cli_tree_node_t *prev_child = NULL;
    for (uint32_t i = 0; i < sub_count; ++i) {
        nmo_chunk_t *sub = nmo_chunk_get_sub_chunk(chunk, i);
        if (!sub) {
            continue;
        }

        nmo_cli_tree_node_t *child = build_chunk_tree_node(ctx, sub, arena);
        if (!child) {
            continue;
        }

        if (!node->first_child) {
            node->first_child = child;
        } else if (prev_child) {
            prev_child->next_sibling = child;
        }
        prev_child = child;
    }

    return node;
}

static yyjson_mut_val *build_chunk_json_tree(yyjson_mut_doc *doc,
                                             nmo_context_t *ctx,
                                             nmo_chunk_t *chunk,
                                             const nmo_cli_chunk_ptr_index_t *index_map,
                                             size_t index_map_count)
{
    if (!doc || !chunk) {
        return yyjson_mut_null(doc);
    }

    yyjson_mut_val *node = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, node, "class_id", chunk->class_id);
    const char *class_name = nmo_cli_class_name_from_id(ctx, chunk->class_id);
    if (class_name) {
        yyjson_mut_obj_add_str(doc, node, "class_name", class_name);
    }

    yyjson_mut_obj_add_uint(doc, node, "data_version", chunk->data_version);
    yyjson_mut_obj_add_uint(doc, node, "chunk_version", chunk->chunk_version);
    yyjson_mut_obj_add_uint(doc, node, "options", chunk->chunk_options);
    yyjson_mut_obj_add_uint(doc, node, "data_size", (uint64_t)nmo_chunk_get_data_size(chunk));
    yyjson_mut_obj_add_uint(doc, node, "compressed_size", (uint64_t)chunk->compressed_size);
    yyjson_mut_obj_add_uint(doc, node, "uncompressed_size", (uint64_t)chunk->uncompressed_size);

    if (index_map) {
        uint32_t flat_index = 0;
        if (lookup_chunk_index(index_map, index_map_count, chunk, &flat_index)) {
            yyjson_mut_obj_add_uint(doc, node, "flat_index", flat_index);
        }
    }

    uint32_t sub_count = nmo_chunk_get_sub_chunk_count(chunk);
    yyjson_mut_obj_add_uint(doc, node, "subchunk_count", (uint64_t)sub_count);

    if (sub_count > 0) {
        yyjson_mut_val *children = yyjson_mut_arr(doc);
        for (uint32_t i = 0; i < sub_count; ++i) {
            nmo_chunk_t *sub = nmo_chunk_get_sub_chunk(chunk, i);
            if (!sub) {
                continue;
            }
            yyjson_mut_arr_add_val(children,
                                   build_chunk_json_tree(doc, ctx, sub, index_map, index_map_count));
        }
        yyjson_mut_obj_add_val(doc, node, "children", children);
    }

    return node;
}

static void chunk_tree_render(FILE *out, const nmo_cli_tree_node_t *node, bool colorize) {
    if (colorize) {
        fprintf(out, "%s%s%s", NMO_CLI_COLOR_CYAN, node->label, NMO_CLI_COLOR_RESET);
    } else {
        fprintf(out, "%s", node->label);
    }
}

static bool collect_all_chunk_entries(nmo_session_t *session,
                                      nmo_cli_chunk_entry_t **out_entries,
                                      size_t *out_count,
                                      size_t *out_object_count)
{
    return nmo_chunk_index_collect_entries(session, out_entries, out_count, out_object_count);
}

/* ============================================================================
 * chunk list - List all chunks by iterating over objects
 * ============================================================================ */

int nmo_cmd_chunk_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Collect all chunks (including sub-chunks) */
    nmo_cli_chunk_entry_t *entries = NULL;
    size_t entry_count = 0;
    size_t object_count = 0;
    if (!collect_all_chunk_entries(c.session, &entries, &entry_count, &object_count)) {
        fprintf(stderr, "Error: Failed to collect chunks\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, data, "total_objects", (uint64_t)object_count);
        yyjson_mut_obj_add_uint(doc, data, "total_chunks", (uint64_t)entry_count);

        yyjson_mut_val *chunks = yyjson_mut_arr(doc);
        for (size_t i = 0; i < entry_count; ++i) {
            const nmo_cli_chunk_entry_t *e = &entries[i];
            nmo_chunk_t *chunk = e->chunk;
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "index", (uint64_t)i);
            if (e->parent_index >= 0) {
                yyjson_mut_obj_add_uint(doc, item, "parent_index", (uint64_t)e->parent_index);
            }
            yyjson_mut_obj_add_uint(doc, item, "depth", (uint64_t)e->depth);
            yyjson_mut_obj_add_uint(doc, item, "owner_object_id", e->owner_object_id);
            if (e->owner_object_name) {
                nmo_cli_json_add_str_safe(doc, item, "owner_object_name", e->owner_object_name);
            }

            yyjson_mut_obj_add_uint(doc, item, "owner_class_id", e->owner_class_id);
            {
                const char *owner_class_name = nmo_cli_class_name_from_id(c.ctx, e->owner_class_id);
                if (owner_class_name) {
                    yyjson_mut_obj_add_str(doc, item, "owner_class_name", owner_class_name);
                }
            }

            yyjson_mut_obj_add_uint(doc, item, "class_id", chunk->class_id);

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, chunk->class_id);
            if (class_name) {
                yyjson_mut_obj_add_str(doc, item, "class_name", class_name);
            }

            yyjson_mut_obj_add_uint(doc, item, "data_size", (uint64_t)nmo_chunk_get_data_size(chunk));
            yyjson_mut_obj_add_uint(doc, item, "options", chunk->chunk_options);
            yyjson_mut_obj_add_uint(doc, item, "subchunk_count",
                                    (uint64_t)nmo_chunk_get_sub_chunk_count(chunk));

            yyjson_mut_obj_add_uint(doc, item, "data_version", chunk->data_version);
            yyjson_mut_obj_add_uint(doc, item, "chunk_version", chunk->chunk_version);

            yyjson_mut_arr_add_val(chunks, item);
        }
        yyjson_mut_obj_add_val(doc, data, "chunks", chunks);

        nmo_cmd_ctx_json_end(&c, doc, data, "chunk.list");
    } else {
        fprintf(c.out, "Chunks: %zu (including sub-chunks; from %zu objects)\n\n", entry_count, object_count);

        /* Table output */
        static const nmo_cli_table_col_t columns[] = {
            {"Idx", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Parent", NMO_CLI_ALIGN_RIGHT, 6, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 40},
            {"Owner", NMO_CLI_ALIGN_LEFT, 24, 60},
            {"Opt", NMO_CLI_ALIGN_LEFT, 14, 26},
            {"Size", NMO_CLI_ALIGN_RIGHT, 10, 0},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        for (size_t i = 0; i < entry_count; ++i) {
            const nmo_cli_chunk_entry_t *e = &entries[i];
            nmo_chunk_t *chunk = e->chunk;

            char idx_buf[32], parent_buf[32], size_buf[32];
            snprintf(idx_buf, sizeof(idx_buf), "%zu", i);
            if (e->parent_index >= 0) {
                snprintf(parent_buf, sizeof(parent_buf), "%lld", (long long)e->parent_index);
            } else {
                snprintf(parent_buf, sizeof(parent_buf), "-");
            }
            snprintf(size_buf, sizeof(size_buf), "%zu", nmo_chunk_get_data_size(chunk));

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, chunk->class_id);
            const char *owner_name = e->owner_object_name;

            char owner_buf[128];
            if (owner_name && owner_name[0]) {
                snprintf(owner_buf, sizeof(owner_buf), "%u %s", e->owner_object_id, owner_name);
            } else {
                snprintf(owner_buf, sizeof(owner_buf), "%u", e->owner_object_id);
            }

            char opt_buf[128];
            (void)nmo_cli_chunk_options_to_string(chunk->chunk_options, opt_buf, sizeof(opt_buf));

            char class_buf[128];
            if (e->depth > 0) {
                snprintf(class_buf, sizeof(class_buf), "%*s%s",
                         (int)(e->depth * 2), "", class_name ? class_name : "-");
            } else {
                snprintf(class_buf, sizeof(class_buf), "%s", class_name ? class_name : "-");
            }

            const char *cells[] = {
                idx_buf,
                parent_buf,
                class_buf,
                owner_buf,
                opt_buf,
                size_buf
            };
            nmo_cli_table_add_row(&table, cells, 6);
        }

        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    nmo_chunk_index_free_entries(entries);

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * chunk tree - Chunks don't have hierarchy (use object tree instead)
 * ============================================================================ */

int nmo_cmd_chunk_tree(int argc, char **argv, const nmo_cli_global_opts_t *global) {
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

    /* Pre-collect flat index map to attach stable indices in JSON */
    nmo_cli_chunk_entry_t *flat_entries = NULL;
    size_t flat_count = 0;
    (void)collect_all_chunk_entries(c.session, &flat_entries, &flat_count, NULL);
    nmo_cli_chunk_ptr_index_t *index_map = NULL;
    size_t index_map_count = 0;
    if (flat_entries) {
        (void)build_chunk_index_map(flat_entries, flat_count, &index_map, &index_map_count);
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "total_objects", (uint64_t)object_count);
        yyjson_mut_obj_add_uint(doc, data, "flat_chunk_count", (uint64_t)flat_count);

        yyjson_mut_val *roots = yyjson_mut_arr(doc);

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
            if (!chunk) {
                continue;
            }

            /* Root wrapper: owner + chunk tree */
            yyjson_mut_val *root = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, root, "owner_object_id", nmo_object_get_id(obj));
            const char *owner_name = nmo_object_get_name(obj);
            if (owner_name && owner_name[0]) {
                nmo_cli_json_add_str_safe(doc, root, "owner_object_name", owner_name);
            }

            const char *owner_class = nmo_cli_class_name_from_id(c.ctx, nmo_object_get_class_id(obj));
            if (owner_class) {
                yyjson_mut_obj_add_str(doc, root, "owner_class_name", owner_class);
            }
            yyjson_mut_obj_add_uint(doc, root, "owner_class_id", nmo_object_get_class_id(obj));

            yyjson_mut_obj_add_val(doc, root, "chunk",
                                   build_chunk_json_tree(doc, c.ctx, chunk, index_map, index_map_count));
            yyjson_mut_arr_add_val(roots, root);
        }

        yyjson_mut_obj_add_val(doc, data, "roots", roots);

        nmo_cmd_ctx_json_end(&c, doc, data, "chunk.tree");
    } else {
        fprintf(c.out, "Chunk Tree (sub-chunks): %zu objects\n\n", object_count);

        nmo_arena_t *arena = nmo_session_get_arena(c.session);
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
            if (!chunk) {
                continue;
            }

            const char *owner_name = nmo_object_get_name(obj);
            const char *owner_class = nmo_cli_class_name_from_id(c.ctx, nmo_object_get_class_id(obj));
            fprintf(c.out, "Object %u: %s [%s]\n",
                    nmo_object_get_id(obj),
                    (owner_name && owner_name[0]) ? owner_name : "(unnamed)",
                    owner_class ? owner_class : "?");

            nmo_cli_tree_node_t *tree = build_chunk_tree_node(c.ctx, chunk, arena);
            if (!tree) {
                fprintf(c.out, "  (alloc failed)\n\n");
                continue;
            }

            nmo_cli_print_tree(tree, c.out, c.colorize, chunk_tree_render);
            fprintf(c.out, "\n");
        }
    }

    nmo_chunk_index_free_map(index_map);
    nmo_chunk_index_free_entries(flat_entries);

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * chunk show - Show chunk for a specific object ID
 * ============================================================================ */

int nmo_cmd_chunk_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Modes:
       - nmo chunk show --index <n> <file> (supports sub-chunks)
       - nmo chunk show <object-id> <file> (root chunk for object)
    */
    const char *file_path = NULL;
    const char *index_str = NULL;
    const char *obj_id_str = NULL;
    bool include_hexdump = false;
    size_t max_bytes = 256;

    bool *consumed = (bool *)calloc((size_t)argc, sizeof(bool));
    if (!consumed) {
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--index") == 0 || strcmp(argv[i], "-i") == 0) {
            if ((i + 1) >= argc) {
                continue;
            }
            index_str = argv[i + 1];
            consumed[i] = true;
            consumed[i + 1] = true;
            ++i;
            continue;
        }
        if (strcmp(argv[i], "--hexdump") == 0) {
            include_hexdump = true;
            consumed[i] = true;
            continue;
        }
        if (strcmp(argv[i], "--max-bytes") == 0 || strcmp(argv[i], "-m") == 0) {
            if ((i + 1) >= argc) {
                continue;
            }
            uint32_t tmp = 0;
            if (!nmo_tool_parse_u32(argv[i + 1], &tmp)) {
                fprintf(stderr, "Error: Invalid --max-bytes '%s'\n", argv[i + 1]);
                free(consumed);
                return NMO_CLI_EXIT_ARG_ERROR;
            }
            max_bytes = (size_t)tmp;
            consumed[i] = true;
            consumed[i + 1] = true;
            ++i;
            continue;
        }
    }

    /* File is last positional (non-option) argument that is not an option value. */
    for (int i = argc - 1; i >= 1; --i) {
        if (!consumed[i] && argv[i][0] != '-') {
            file_path = argv[i];
            consumed[i] = true;
            break;
        }
    }

    /* Object-id form: first remaining positional argument. */
    if (!index_str) {
        for (int i = 1; i < argc; ++i) {
            if (!consumed[i] && argv[i][0] != '-') {
                obj_id_str = argv[i];
                consumed[i] = true;
                break;
            }
        }
    }

    free(consumed);

    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo chunk show --index <n> <file>\n");
        fprintf(stderr, "       nmo chunk show <object-id> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    uint32_t object_id = 0;
    uint32_t chunk_index = 0;
    bool use_index = false;

    bool flat_index_known = false;
    uint32_t flat_index = 0;

    int64_t parent_index = -1;
    uint32_t depth = 0;

    if (index_str) {
        if (!nmo_tool_parse_u32(index_str, &chunk_index)) {
            fprintf(stderr, "Error: Invalid chunk index '%s'\n", index_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        use_index = true;
    } else {
        if (!obj_id_str || !nmo_tool_parse_u32(obj_id_str, &object_id)) {
            fprintf(stderr, "Error: Invalid object ID\n");
            fprintf(stderr, "Usage: nmo chunk show <object-id> <file>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_t *target = NULL;
    nmo_chunk_t *chunk = NULL;

    if (use_index) {
        nmo_cli_chunk_entry_t *entries = NULL;
        size_t entry_count = 0;
        if (!collect_all_chunk_entries(c.session, &entries, &entry_count, NULL)) {
            fprintf(stderr, "Error: Failed to collect chunks\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        if ((size_t)chunk_index >= entry_count) {
            nmo_chunk_index_free_entries(entries);
            fprintf(stderr, "Error: Chunk index %u out of range (0..%zu)\n",
                    chunk_index, entry_count ? (entry_count - 1) : 0);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        const nmo_cli_chunk_entry_t selected = entries[chunk_index];
        chunk = selected.chunk;
        object_id = selected.owner_object_id;
        parent_index = selected.parent_index;
        depth = selected.depth;
        nmo_chunk_index_free_entries(entries);

        flat_index_known = true;
        flat_index = chunk_index;

        /* Best-effort resolve owner object for name */
        nmo_object_t **objects = NULL;
        size_t object_count = 0;
        if (nmo_session_get_objects(c.session, &objects, &object_count) == NMO_OK) {
            for (size_t i = 0; i < object_count; ++i) {
                if (nmo_object_get_id(objects[i]) == object_id) {
                    target = objects[i];
                    break;
                }
            }
        }
    } else {
        /* Get objects and find the target */
        nmo_object_t **objects = NULL;
        size_t object_count = 0;
        if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
            fprintf(stderr, "Error: Failed to get objects\n");
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

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

        chunk = nmo_object_get_chunk(target);
        if (!chunk) {
            fprintf(stderr, "Error: Object %u has no chunk data\n", object_id);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }

        /* Provide stable index info when possible */
        nmo_cli_chunk_entry_t *entries = NULL;
        size_t entry_count = 0;
        if (collect_all_chunk_entries(c.session, &entries, &entry_count, NULL) && entries) {
            nmo_cli_chunk_ptr_index_t *map = NULL;
            size_t map_count = 0;
            if (build_chunk_index_map(entries, entry_count, &map, &map_count) && map) {
                uint32_t idx = 0;
                if (lookup_chunk_index(map, map_count, chunk, &idx)) {
                    parent_index = -1;
                    depth = 0;

                    flat_index_known = true;
                    flat_index = idx;
                }
                free(map);
            }
            nmo_chunk_index_free_entries(entries);
        }
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        if (flat_index_known) {
            yyjson_mut_obj_add_uint(doc, data, "flat_index", flat_index);
        }
        if (parent_index >= 0) {
            yyjson_mut_obj_add_uint(doc, data, "parent_index", (uint64_t)parent_index);
        }
        yyjson_mut_obj_add_uint(doc, data, "depth", (uint64_t)depth);

        yyjson_mut_obj_add_uint(doc, data, "object_id", object_id);

        if (target) {
            const char *obj_name = nmo_object_get_name(target);
            if (obj_name && obj_name[0]) {
                nmo_cli_json_add_str_safe(doc, data, "object_name", obj_name);
            }
        }

        yyjson_mut_obj_add_uint(doc, data, "class_id", chunk->class_id);
        const char *class_name = nmo_cli_class_name_from_id(c.ctx, chunk->class_id);
        if (class_name) {
            yyjson_mut_obj_add_str(doc, data, "class_name", class_name);
        }

        yyjson_mut_obj_add_uint(doc, data, "data_version", chunk->data_version);
        yyjson_mut_obj_add_uint(doc, data, "chunk_version", chunk->chunk_version);
        yyjson_mut_obj_add_uint(doc, data, "options", chunk->chunk_options);
        yyjson_mut_obj_add_uint(doc, data, "data_size", (uint64_t)nmo_chunk_get_data_size(chunk));
        yyjson_mut_obj_add_uint(doc, data, "compressed_size", (uint64_t)chunk->compressed_size);
        yyjson_mut_obj_add_uint(doc, data, "uncompressed_size", (uint64_t)chunk->uncompressed_size);

        if (include_hexdump) {
            size_t data_size = 0;
            const uint8_t *raw = (const uint8_t *)nmo_chunk_get_data(chunk, &data_size);
            (void)nmo_cli_json_add_data_hex(doc, data, raw, data_size, max_bytes, false);
        }

        /* Count sub-chunks and IDs */
        yyjson_mut_obj_add_uint(doc, data, "id_count", (uint64_t)chunk->ids.count);
        yyjson_mut_obj_add_uint(doc, data, "subchunk_count", (uint64_t)nmo_chunk_get_sub_chunk_count(chunk));
        yyjson_mut_obj_add_uint(doc, data, "manager_count", (uint64_t)chunk->managers.count);

        nmo_cmd_ctx_json_end(&c, doc, data, "chunk.show");
    } else {
        nmo_cli_print_heading(c.out, "Chunk Details", c.colorize);

        char buf[64];
        if (flat_index_known) {
            snprintf(buf, sizeof(buf), "%u", flat_index);
        } else {
            snprintf(buf, sizeof(buf), "-");
        }
        nmo_cli_print_kv(c.out, "Flat Index", buf, 18, c.colorize);
        if (parent_index >= 0) {
            snprintf(buf, sizeof(buf), "%lld", (long long)parent_index);
            nmo_cli_print_kv(c.out, "Parent Index", buf, 18, c.colorize);
        }
        snprintf(buf, sizeof(buf), "%u", depth);
        nmo_cli_print_kv(c.out, "Depth", buf, 18, c.colorize);

        if (target) {
            const char *obj_name = nmo_object_get_name(target);
            snprintf(buf, sizeof(buf), "%u  %s", object_id,
                     (obj_name && obj_name[0]) ? obj_name : "(unnamed)");
        } else {
            snprintf(buf, sizeof(buf), "%u", object_id);
        }
        nmo_cli_print_kv(c.out, "Object", buf, 18, c.colorize);

        snprintf(buf, sizeof(buf), "%u", chunk->class_id);
        nmo_cli_print_kv(c.out, "Class ID", buf, 18, c.colorize);

        const char *class_name = nmo_cli_class_name_from_id(c.ctx, chunk->class_id);
        nmo_cli_print_kv(c.out, "Class Name", class_name ? class_name : "-", 18, c.colorize);

        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Format", c.colorize);

        snprintf(buf, sizeof(buf), "%u", chunk->data_version);
        nmo_cli_print_kv(c.out, "Data Version", buf, 18, c.colorize);

        snprintf(buf, sizeof(buf), "%u", chunk->chunk_version);
        nmo_cli_print_kv(c.out, "Chunk Version", buf, 18, c.colorize);

        {
            char opt_flags[128];
            const char *opt = nmo_cli_chunk_options_to_string(chunk->chunk_options,
                                                              opt_flags,
                                                              sizeof(opt_flags));
            snprintf(buf, sizeof(buf), "%s (0x%04X)", opt, (unsigned int)chunk->chunk_options);
            nmo_cli_print_kv(c.out, "Options", buf, 18, c.colorize);
        }

        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Size", c.colorize);

        snprintf(buf, sizeof(buf), "%zu bytes", nmo_chunk_get_data_size(chunk));
        nmo_cli_print_kv(c.out, "Data Size", buf, 18, c.colorize);

        snprintf(buf, sizeof(buf), "%zu bytes", chunk->compressed_size);
        nmo_cli_print_kv(c.out, "Compressed Size", buf, 18, c.colorize);

        snprintf(buf, sizeof(buf), "%zu bytes", chunk->uncompressed_size);
        nmo_cli_print_kv(c.out, "Uncompressed Size", buf, 18, c.colorize);

        if (include_hexdump) {
            size_t data_size = 0;
            const uint8_t *raw = (const uint8_t *)nmo_chunk_get_data(chunk, &data_size);
            fprintf(c.out, "\n");
            nmo_cli_print_heading(c.out, "Hexdump", c.colorize);
            if (!raw || data_size == 0) {
                nmo_cli_print_kv(c.out, "Data", "(empty)", 18, c.colorize);
            } else {
                size_t emit_size = (max_bytes > 0 && data_size > max_bytes) ? max_bytes : data_size;
                if (emit_size < data_size) {
                    snprintf(buf, sizeof(buf), "showing %zu/%zu bytes", emit_size, data_size);
                    nmo_cli_print_kv(c.out, "Data", buf, 18, c.colorize);
                }

                nmo_hexdump_options_t hd;
                nmo_hexdump_init_options(&hd);
                hd.colorize = c.colorize;
                hd.ansi.offset = NMO_CLI_COLOR_DIM;
                hd.ansi.hex = NMO_CLI_COLOR_CYAN;
                hd.ansi.ascii = NMO_CLI_COLOR_GREEN;
                hd.ansi.delim = NMO_CLI_COLOR_DIM;
                hd.ansi.reset = NMO_CLI_COLOR_RESET;
                nmo_hexdump_canonical(c.out, raw, emit_size, &hd);
            }
        }

        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "References", c.colorize);

        snprintf(buf, sizeof(buf), "%zu", chunk->ids.count);
        nmo_cli_print_kv(c.out, "Object IDs", buf, 18, c.colorize);

        snprintf(buf, sizeof(buf), "%u", nmo_chunk_get_sub_chunk_count(chunk));
        nmo_cli_print_kv(c.out, "Sub-chunks", buf, 18, c.colorize);

        snprintf(buf, sizeof(buf), "%zu", chunk->managers.count);
        nmo_cli_print_kv(c.out, "Manager Refs", buf, 18, c.colorize);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * chunk find - Find chunks by class
 * ============================================================================ */

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

int nmo_cmd_chunk_find(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *class_filter = parse_class_filter(argc, argv);
    if (!class_filter) {
        fprintf(stderr, "Error: --class filter required\n");
        fprintf(stderr, "Usage: nmo chunk find --class <name> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Resolve class filter */
    nmo_class_id_t filter_class_id = nmo_cli_class_id_from_name(c.ctx, class_filter);
    if (!filter_class_id) {
        fprintf(stderr, "Error: Unknown class '%s'\n", class_filter);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    /* Get objects */
    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "class_filter", class_filter);

        yyjson_mut_val *matches = yyjson_mut_arr(doc);
        size_t match_count = 0;

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
            if (!chunk) continue;

            /* Check if chunk class matches filter (including derived classes) */
            if (!nmo_cli_class_is_derived_from(c.ctx, chunk->class_id, filter_class_id)) {
                continue;
            }

            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "object_id", nmo_object_get_id(obj));

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, chunk->class_id);
            if (class_name) {
                yyjson_mut_obj_add_str(doc, item, "class_name", class_name);
            }

            const char *name = nmo_object_get_name(obj);
            if (name && name[0]) {
                nmo_cli_json_add_str_safe(doc, item, "object_name", name);
            }

            yyjson_mut_obj_add_uint(doc, item, "data_size", (uint64_t)nmo_chunk_get_data_size(chunk));

            yyjson_mut_arr_add_val(matches, item);
            match_count++;
        }

        yyjson_mut_obj_add_uint(doc, data, "match_count", (uint64_t)match_count);
        yyjson_mut_obj_add_val(doc, data, "matches", matches);

        nmo_cmd_ctx_json_end(&c, doc, data, "chunk.find");
    } else {
        /* Table output */
        static const nmo_cli_table_col_t columns[] = {
            {"Object ID", NMO_CLI_ALIGN_RIGHT, 6, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 20, 40},
            {"Object Name", NMO_CLI_ALIGN_LEFT, 20, 40},
            {"Size", NMO_CLI_ALIGN_RIGHT, 10, 0},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        size_t match_count = 0;
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
            if (!chunk) continue;

            if (!nmo_cli_class_is_derived_from(c.ctx, chunk->class_id, filter_class_id)) {
                continue;
            }

            char oid_buf[16], size_buf[16];
            snprintf(oid_buf, sizeof(oid_buf), "%u", nmo_object_get_id(obj));
            snprintf(size_buf, sizeof(size_buf), "%zu", nmo_chunk_get_data_size(chunk));

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, chunk->class_id);
            const char *name = nmo_object_get_name(obj);

            const char *cells[] = {
                oid_buf,
                class_name ? class_name : "-",
                (name && name[0]) ? name : "-",
                size_buf
            };
            nmo_cli_table_add_row(&table, cells, 4);
            match_count++;
        }

        fprintf(c.out, "Found: %zu chunks (class: %s)\n\n", match_count, class_filter);
        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

