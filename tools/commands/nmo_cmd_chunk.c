/**
 * @file nmo_cmd_chunk.c
 * @brief CLI chunk command group implementation
 */

#include "nmo_cmd_chunk.h"

#include "../nmo_cmd_core.h"
#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

#include "nmo.h"
#include "chunk/nmo_chunk_index.h"
#include "object/nmo_context.h"

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

/*
 * One chunk tree node. JSON: the chunk fields plus a "children" array when the
 * chunk has sub-chunks. Text: one tree line ("<prefix>|-- <label>", or the bare
 * label for a root, whose `prefix` is NULL), then the children's lines.
 */
static bool chunk_tree_build_node(const nmo_cmd_ctx_t *c,
                                  nmo_chunk_t *chunk,
                                  const nmo_cli_chunk_ptr_index_t *index_map,
                                  size_t index_map_count,
                                  const char *prefix,
                                  bool is_last,
                                  nmo_cli_record_t *node)
{
    const char *class_name = nmo_cli_class_name_from_id(c->ctx, chunk->class_id);
    uint32_t sub_count = nmo_chunk_get_sub_chunk_count(chunk);
    const char *color = c->colorize ? NMO_CLI_COLOR_CYAN : "";
    const char *reset = c->colorize ? NMO_CLI_COLOR_RESET : "";

    char *opt = nmo_cli_chunk_options_dup(chunk->chunk_options);
    bool ok = opt != NULL &&
              nmo_cli_record_raw_fmt(node, "%s%s%s%s (cid=%u size=%zu opt=%s sub=%u)%s\n",
                                     prefix ? prefix : "",
                                     prefix ? (is_last ? "`-- " : "|-- ") : "",
                                     color,
                                     class_name ? class_name : "(unknown)",
                                     chunk->class_id,
                                     nmo_chunk_get_data_size(chunk),
                                     opt,
                                     sub_count,
                                     reset);
    free(opt);

    ok = ok && nmo_cli_record_uint(node, "class_id", NULL, chunk->class_id);
    if (class_name) {
        ok = ok && nmo_cli_record_str(node, "class_name", NULL, class_name);
    }
    ok = ok && nmo_cli_record_uint(node, "data_version", NULL, chunk->data_version) &&
         nmo_cli_record_uint(node, "chunk_version", NULL, chunk->chunk_version) &&
         nmo_cli_record_uint(node, "options", NULL, chunk->chunk_options) &&
         nmo_cli_record_uint(node, "data_size", NULL,
                             (uint64_t)nmo_chunk_get_data_size(chunk)) &&
         nmo_cli_record_uint(node, "compressed_size", NULL,
                             (uint64_t)chunk->compressed_size) &&
         nmo_cli_record_uint(node, "uncompressed_size", NULL,
                             (uint64_t)chunk->uncompressed_size);

    uint32_t flat_index = 0;
    if (index_map && lookup_chunk_index(index_map, index_map_count, chunk, &flat_index)) {
        ok = ok && nmo_cli_record_uint(node, "flat_index", NULL, flat_index);
    }
    ok = ok && nmo_cli_record_uint(node, "subchunk_count", NULL, (uint64_t)sub_count);
    if (!ok || sub_count == 0) {
        return ok;
    }

    nmo_cli_record_array_t *children = nmo_cli_record_array(node, "children", NULL);
    if (!children) {
        return false;
    }
    nmo_cli_record_array_omit_heading(children);
    nmo_cli_record_array_inline_items(children);

    /* The last present sub-chunk takes the closing connector. */
    uint32_t last = 0;
    for (uint32_t i = 0; i < sub_count; ++i) {
        if (nmo_chunk_get_sub_chunk(chunk, i)) {
            last = i;
        }
    }
    char *child_prefix = nmo_tool_strdup_fmt("%s%s", prefix ? prefix : "",
                                             prefix ? (is_last ? "    " : "|   ") : "");
    ok = child_prefix != NULL;
    for (uint32_t i = 0; ok && i < sub_count; ++i) {
        nmo_chunk_t *sub = nmo_chunk_get_sub_chunk(chunk, i);
        if (!sub) {
            continue;
        }
        nmo_cli_record_t *child = nmo_cli_record_new();
        ok = child != NULL &&
             chunk_tree_build_node(c, sub, index_map, index_map_count,
                                   child_prefix, i == last, child);
        if (!ok) {
            nmo_cli_record_free(child);
        } else {
            ok = nmo_cli_record_array_add(children, child);
        }
    }
    free(child_prefix);
    return ok;
}

static bool collect_all_chunk_entries(nmo_workspace_t *workspace,
                                      nmo_cli_chunk_entry_t **out_entries,
                                      size_t *out_count,
                                      size_t *out_object_count)
{
    return nmo_tool_owner_chunk_entries(workspace, out_entries, out_count, out_object_count);
}

/* Emit a finished record, or report INTERNAL_ERROR when building it failed. */
static int chunk_emit(nmo_cmd_ctx_t *c,
                      nmo_cli_record_t *rec,
                      bool ok,
                      const char *cmd_name,
                      int key_width)
{
    if (!ok) {
        nmo_cli_record_free(rec);
        rec = NULL;
    }
    return nmo_cmd_ctx_emit_record(c, rec, cmd_name, key_width, c->colorize);
}

typedef struct nmo_cli_object_collect {
    nmo_object_t **objects;
    size_t count;
    size_t capacity;
    bool allocation_failed;
} nmo_cli_object_collect_t;

static int chunk_collect_object(size_t index, nmo_object_t *obj,
                                const nmo_cmd_ctx_t *c, void *user)
{
    (void)index;
    (void)c;

    nmo_cli_object_collect_t *collect = (nmo_cli_object_collect_t *)user;
    if (!collect || !obj) {
        return 0;
    }

    if (collect->count == collect->capacity) {
        size_t new_capacity = collect->capacity ? collect->capacity * 2 : 64;
        if (new_capacity <= collect->capacity) {
            collect->allocation_failed = true;
            return 1;
        }
        nmo_object_t **new_objects = (nmo_object_t **)realloc(
            collect->objects, new_capacity * sizeof(*new_objects));
        if (!new_objects) {
            collect->allocation_failed = true;
            return 1;
        }
        collect->objects = new_objects;
        collect->capacity = new_capacity;
    }

    collect->objects[collect->count++] = obj;
    return 0;
}

static bool chunk_collect_objects(const nmo_cmd_ctx_t *c,
                                  nmo_object_t ***out_objects,
                                  size_t *out_count)
{
    if (!c || !out_objects || !out_count) {
        return false;
    }

    *out_objects = NULL;
    *out_count = 0;

    nmo_cli_object_collect_t collect = {0};
    nmo_core_iter_result_t result = {0};
    int rc = nmo_core_object_query_run(c, NULL, chunk_collect_object,
                                       &collect, &result);
    if (rc != NMO_CLI_EXIT_SUCCESS || collect.allocation_failed ||
        collect.count != result.visited) {
        free(collect.objects);
        return false;
    }

    *out_objects = collect.objects;
    *out_count = collect.count;
    return true;
}

typedef struct nmo_cli_chunk_find_data {
    nmo_class_id_t filter_class_id;
    nmo_cli_object_collect_t matches;
} nmo_cli_chunk_find_data_t;

/* One match: JSON id/class_name/name/data_size, text Object ID/Class/Object
 * Name/Size. */
static bool chunk_find_build_record(const nmo_cmd_ctx_t *c, nmo_object_t *obj,
                                    nmo_chunk_t *chunk, nmo_cli_record_t *rec)
{
    const char *class_name = nmo_cli_class_name_from_id(c->ctx, chunk->class_id);
    const char *name = nmo_object_get_name(obj);

    bool ok = nmo_cli_record_uint(rec, "id", "Object ID", nmo_object_get_id(obj));
    if (class_name) {
        ok = ok && nmo_cli_record_str(rec, "class_name", "Class", class_name);
    } else {
        ok = ok && nmo_cli_record_text(rec, "Class", "-");
    }
    return ok && nmo_cli_record_str_opt(rec, "name", "Object Name", name, "-") &&
           nmo_cli_record_uint(rec, "data_size", "Size",
                               (uint64_t)nmo_chunk_get_data_size(chunk));
}

static int chunk_find_object(size_t index, nmo_object_t *obj,
                             const nmo_cmd_ctx_t *c, void *user)
{
    nmo_cli_chunk_find_data_t *data = (nmo_cli_chunk_find_data_t *)user;
    if (!data || !obj) {
        return 0;
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (!chunk) {
        return 0;
    }

    if (!nmo_cli_class_is_derived_from(c->ctx, chunk->class_id,
                                       data->filter_class_id)) {
        return 0;
    }

    return chunk_collect_object(index, obj, c, &data->matches);
}

/* ============================================================================
 * chunk list - List all chunks by iterating over objects
 * ============================================================================ */

/*
 * One flat chunk entry. Text columns are Idx, Parent, Class (indented by
 * depth), Owner ("id name"), Opt, Size; the JSON keys keep their historical
 * order, so the class, owner, options, and size values appear on both sides
 * as separate fields.
 */
static bool chunk_list_build_record(const nmo_cmd_ctx_t *c, size_t index,
                                    const nmo_cli_chunk_entry_t *e,
                                    nmo_cli_record_t *rec)
{
    nmo_chunk_t *chunk = e->chunk;
    const char *class_name = nmo_cli_class_name_from_id(c->ctx, chunk->class_id);
    const char *owner_class_name = nmo_cli_class_name_from_id(c->ctx, e->owner_class_id);
    const char *owner_name = e->owner_object_name;
    size_t data_size = nmo_chunk_get_data_size(chunk);

    char *opt_text = nmo_cli_chunk_options_dup(chunk->chunk_options);

    bool ok = nmo_cli_record_uint(rec, "index", "Idx", (uint64_t)index);
    if (e->parent_index >= 0) {
        ok = ok && nmo_cli_record_uint(rec, "parent_index", "Parent",
                                       (uint64_t)e->parent_index);
    } else {
        ok = ok && nmo_cli_record_text(rec, "Parent", "-");
    }
    ok = ok && nmo_cli_record_uint(rec, "depth", NULL, (uint64_t)e->depth) &&
         nmo_cli_record_text_fmt(rec, "Class", "%*s%s", (int)(e->depth * 2), "",
                                 class_name ? class_name : "-");
    if (ok && owner_name && owner_name[0]) {
        ok = nmo_cli_record_text_fmt(rec, "Owner", "%u %s", e->owner_object_id, owner_name);
    } else if (ok) {
        ok = nmo_cli_record_text_fmt(rec, "Owner", "%u", e->owner_object_id);
    }
    ok = ok && opt_text != NULL && nmo_cli_record_text(rec, "Opt", opt_text);
    free(opt_text);
    ok = ok && nmo_cli_record_text_fmt(rec, "Size", "%zu", data_size) &&
         nmo_cli_record_uint(rec, "owner_object_id", NULL, e->owner_object_id);
    if (ok && owner_name) {
        ok = nmo_cli_record_str(rec, "owner_object_name", NULL, owner_name);
    }
    ok = ok && nmo_cli_record_uint(rec, "owner_class_id", NULL, e->owner_class_id);
    if (ok && owner_class_name) {
        ok = nmo_cli_record_str(rec, "owner_class_name", NULL, owner_class_name);
    }
    ok = ok && nmo_cli_record_uint(rec, "class_id", NULL, chunk->class_id);
    if (ok && class_name) {
        ok = nmo_cli_record_str(rec, "class_name", NULL, class_name);
    }
    return ok &&
           nmo_cli_record_uint(rec, "data_size", NULL, (uint64_t)data_size) &&
           nmo_cli_record_uint(rec, "options", NULL, chunk->chunk_options) &&
           nmo_cli_record_uint(rec, "subchunk_count", NULL,
                               (uint64_t)nmo_chunk_get_sub_chunk_count(chunk)) &&
           nmo_cli_record_uint(rec, "data_version", NULL, chunk->data_version) &&
           nmo_cli_record_uint(rec, "chunk_version", NULL, chunk->chunk_version);
}

static int chunk_list_run(nmo_cmd_ctx_t *ctx, uint32_t top_n)
{
    nmo_cmd_ctx_t c = *ctx;

    /* Collect all chunks (including sub-chunks) */
    nmo_cli_chunk_entry_t *entries = NULL;
    size_t entry_count = 0;
    size_t object_count = 0;
    if (!collect_all_chunk_entries(c.workspace, &entries, &entry_count, &object_count)) {
        fprintf(stderr, "Error: Failed to collect chunks\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Determine how many entries to emit */
    size_t emit_count = entry_count;
    if (top_n > 0 && (size_t)top_n < emit_count) {
        emit_count = (size_t)top_n;
    }

    static const nmo_cli_table_col_t columns[] = {
        {"Idx", NMO_CLI_ALIGN_RIGHT, 5, 0},
        {"Parent", NMO_CLI_ALIGN_RIGHT, 6, 0},
        {"Class", NMO_CLI_ALIGN_LEFT, 20, 40},
        {"Owner", NMO_CLI_ALIGN_LEFT, 24, 60},
        {"Opt", NMO_CLI_ALIGN_LEFT, 14, 26},
        {"Size", NMO_CLI_ALIGN_RIGHT, 10, 0},
    };

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_raw_fmt(rec, "Chunks: %zu (including sub-chunks; from %zu objects)\n\n",
                                      entry_count, object_count) &&
         nmo_cli_record_uint(rec, "total_objects", NULL, (uint64_t)object_count) &&
         nmo_cli_record_uint(rec, "total_chunks", NULL, (uint64_t)entry_count);

    nmo_cli_record_array_t *chunks = ok ? nmo_cli_record_array(rec, "chunks", NULL) : NULL;
    ok = chunks != NULL &&
         nmo_cli_record_array_set_table(chunks, columns,
                                        sizeof(columns) / sizeof(columns[0]));
    for (size_t i = 0; ok && i < emit_count; ++i) {
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL && chunk_list_build_record(&c, i, &entries[i], item);
        if (!ok) {
            nmo_cli_record_free(item);
        } else {
            ok = nmo_cli_record_array_add(chunks, item);
        }
    }

    nmo_chunk_index_free_entries(entries);

    return chunk_emit(&c, rec, ok, "chunk.list", 0);
}

int nmo_cmd_chunk_list(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--top", NULL, NMO_OPT_UINT, "Limit output to first N entries"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    uint32_t top_n = nmo_opt_uint_or(&vals[0], 0);

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    rc = chunk_list_run(&c, top_n);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * chunk tree - Chunks don't have hierarchy (use object tree instead)
 * ============================================================================ */

static int chunk_tree_run(nmo_cmd_ctx_t *ctx)
{
    nmo_cmd_ctx_t c = *ctx;

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (!chunk_collect_objects(&c, &objects, &object_count)) {
        fprintf(stderr, "Error: Failed to get objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Pre-collect flat index map to attach stable indices in JSON */
    nmo_cli_chunk_entry_t *flat_entries = NULL;
    size_t flat_count = 0;
    (void)collect_all_chunk_entries(c.workspace, &flat_entries, &flat_count, NULL);
    nmo_cli_chunk_ptr_index_t *index_map = NULL;
    size_t index_map_count = 0;
    if (flat_entries) {
        (void)build_chunk_index_map(flat_entries, flat_count, &index_map, &index_map_count);
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_raw_fmt(rec, "Chunk Tree (sub-chunks): %zu objects\n\n",
                                      object_count) &&
         nmo_cli_record_uint(rec, "total_objects", NULL, (uint64_t)object_count) &&
         nmo_cli_record_uint(rec, "flat_chunk_count", NULL, (uint64_t)flat_count);

    nmo_cli_record_array_t *roots = ok ? nmo_cli_record_array(rec, "roots", NULL) : NULL;
    ok = roots != NULL;
    if (ok) {
        nmo_cli_record_array_omit_heading(roots);
        nmo_cli_record_array_inline_items(roots);
    }
    for (size_t i = 0; ok && i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        if (!chunk) {
            continue;
        }

        /* Root wrapper: owner + chunk tree */
        const char *owner_name = nmo_object_get_name(obj);
        nmo_class_id_t owner_class_id = nmo_object_get_class_id(obj);
        const char *owner_class = nmo_cli_class_name_from_id(c.ctx, owner_class_id);
        nmo_cli_record_t *root = nmo_cli_record_new();
        ok = root != NULL &&
             nmo_cli_record_raw_fmt(root, "Object %u: %s [%s]\n",
                                    nmo_object_get_id(obj),
                                    (owner_name && owner_name[0]) ? owner_name : "(unnamed)",
                                    owner_class ? owner_class : "?") &&
             nmo_cli_record_uint(root, "owner_object_id", NULL, nmo_object_get_id(obj)) &&
             nmo_cli_record_str_opt(root, "owner_object_name", NULL, owner_name, NULL);
        if (owner_class) {
            ok = ok && nmo_cli_record_str(root, "owner_class_name", NULL, owner_class);
        }
        ok = ok && nmo_cli_record_uint(root, "owner_class_id", NULL, owner_class_id);

        nmo_cli_record_t *node = ok ? nmo_cli_record_object(root, "chunk") : NULL;
        ok = node != NULL &&
             chunk_tree_build_node(&c, chunk, index_map, index_map_count,
                                   NULL, true, node) &&
             nmo_cli_record_raw(root, "\n");
        if (!ok) {
            nmo_cli_record_free(root);
        } else {
            ok = nmo_cli_record_array_add(roots, root);
        }
    }

    nmo_chunk_index_free_map(index_map);
    nmo_chunk_index_free_entries(flat_entries);
    free(objects);

    return chunk_emit(&c, rec, ok, "chunk.tree", 0);
}

int nmo_cmd_chunk_tree(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    rc = chunk_tree_run(&c);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * chunk show - Show chunk for a specific object ID
 * ============================================================================ */

typedef struct chunk_show_args {
    uint32_t object_id;
    uint32_t chunk_index;
    bool use_index;
    bool include_hexdump;
    size_t max_bytes;
} chunk_show_args_t;

static int chunk_show_parse(int argc, char **argv, bool in_session, chunk_show_args_t *args)
{
    if (!args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    memset(args, 0, sizeof(*args));
    args->max_bytes = 256;

    static const nmo_opt_def_t opts[] = {
        {"--index",     "-i", NMO_OPT_STRING, "Chunk index"},
        {"--hexdump",   NULL, NMO_OPT_FLAG,   "Include hex dump"},
        {"--max-bytes", "-m", NMO_OPT_UINT,   "Max bytes for hexdump (default: 256)"},
    };
    nmo_opt_val_t vals[3];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, 3, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *index_str = nmo_opt_str(&vals[0]);
    args->include_hexdump = vals[1].val.flag;
    args->max_bytes = vals[2].present ? (size_t)vals[2].val.u : 256;

    /* Positional args:
       CLI: [object-id] <file>; in-session: [object-id]. */
    const char *obj_id_str = NULL;
    if (in_session) {
        if (index_str) {
            if (r.pos_count != 0) {
                fprintf(stderr, "Usage: chunk show --index <n>\n");
                return NMO_CLI_EXIT_ARG_ERROR;
            }
        } else if (r.pos_count == 1) {
            obj_id_str = r.pos_args[0];
        } else {
            fprintf(stderr, "Error: Invalid object ID\n");
            fprintf(stderr, "Usage: chunk show <object-id>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    } else {
        const char *file_path = NULL;
        if (r.pos_count >= 2) {
            obj_id_str = r.pos_args[0];
            file_path = r.pos_args[r.pos_count - 1];
        } else if (r.pos_count == 1) {
            file_path = r.pos_args[0];
        }

        if (!file_path) {
            fprintf(stderr, "Error: No file specified\n");
            fprintf(stderr, "Usage: nmo chunk show --index <n> <file>\n");
            fprintf(stderr, "       nmo chunk show <object-id> <file>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    if (index_str) {
        if (!nmo_tool_parse_u32(index_str, &args->chunk_index)) {
            fprintf(stderr, "Error: Invalid chunk index '%s'\n", index_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        args->use_index = true;
    } else {
        if (!obj_id_str || !nmo_tool_parse_u32(obj_id_str, &args->object_id)) {
            fprintf(stderr, "Error: Invalid object ID\n");
            if (in_session) {
                fprintf(stderr, "Usage: chunk show <object-id>\n");
            } else {
                fprintf(stderr, "Usage: nmo chunk show <object-id> <file>\n");
            }
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

/*
 * Chunk details in four text sections (identity, Format, Size, References,
 * plus Hexdump on request). JSON is one flat object in the same order.
 */
static bool chunk_show_build_record(const nmo_cmd_ctx_t *c,
                                    const chunk_show_args_t *args,
                                    nmo_chunk_t *chunk,
                                    nmo_object_t *target,
                                    uint32_t object_id,
                                    bool flat_index_known,
                                    uint32_t flat_index,
                                    int64_t parent_index,
                                    uint32_t depth,
                                    nmo_cli_record_t *rec)
{
    bool ok = true;

    if (flat_index_known) {
        ok = nmo_cli_record_uint(rec, "flat_index", "Flat Index", flat_index);
    } else {
        ok = nmo_cli_record_text(rec, "Flat Index", "-");
    }
    if (ok && parent_index >= 0) {
        ok = nmo_cli_record_uint(rec, "parent_index", "Parent Index",
                                 (uint64_t)parent_index);
    }
    ok = ok && nmo_cli_record_uint(rec, "depth", "Depth", depth);

    /* JSON: id (+ name when known); text: one "Object" line. */
    const char *obj_name = target ? nmo_object_get_name(target) : NULL;
    ok = ok && nmo_cli_record_uint(rec, "id", NULL, object_id);
    if (ok && target) {
        ok = nmo_cli_record_text_fmt(rec, "Object", "%u  %s", object_id,
                                     (obj_name && obj_name[0]) ? obj_name : "(unnamed)");
    } else if (ok) {
        ok = nmo_cli_record_text_fmt(rec, "Object", "%u", object_id);
    }
    if (ok && obj_name && obj_name[0]) {
        ok = nmo_cli_record_str(rec, "name", NULL, obj_name);
    }

    ok = ok && nmo_cli_record_uint(rec, "class_id", "Class ID", chunk->class_id);
    const char *class_name = nmo_cli_class_name_from_id(c->ctx, chunk->class_id);
    if (class_name) {
        ok = ok && nmo_cli_record_str(rec, "class_name", "Class Name", class_name);
    } else {
        ok = ok && nmo_cli_record_text(rec, "Class Name", "-");
    }

    ok = ok && nmo_cli_record_heading(rec, "Format") &&
         nmo_cli_record_uint(rec, "data_version", "Data Version", chunk->data_version) &&
         nmo_cli_record_uint(rec, "chunk_version", "Chunk Version", chunk->chunk_version);
    if (ok) {
        char *opt = nmo_cli_chunk_options_dup(chunk->chunk_options);
        ok = opt != NULL &&
             nmo_cli_record_uint(rec, "options", "Options", chunk->chunk_options) &&
             nmo_cli_record_set_text_fmt(rec, "%s (0x%04X)", opt,
                                         (unsigned int)chunk->chunk_options);
        free(opt);
    }

    size_t data_size = nmo_chunk_get_data_size(chunk);
    ok = ok && nmo_cli_record_heading(rec, "Size");
    ok = ok && nmo_cli_record_uint(rec, "data_size", "Data Size", (uint64_t)data_size) &&
         nmo_cli_record_set_text_fmt(rec, "%zu bytes", data_size);
    ok = ok && nmo_cli_record_uint(rec, "compressed_size", "Compressed Size",
                                   (uint64_t)chunk->compressed_size) &&
         nmo_cli_record_set_text_fmt(rec, "%zu bytes", chunk->compressed_size);
    ok = ok && nmo_cli_record_uint(rec, "uncompressed_size", "Uncompressed Size",
                                   (uint64_t)chunk->uncompressed_size) &&
         nmo_cli_record_set_text_fmt(rec, "%zu bytes", chunk->uncompressed_size);

    if (ok && args->include_hexdump) {
        size_t raw_size = 0;
        const uint8_t *raw = (const uint8_t *)nmo_chunk_get_data(chunk, &raw_size);
        ok = nmo_cli_record_heading(rec, "Hexdump") &&
             nmo_cli_record_hex_bytes(rec, "Data", raw, raw_size, args->max_bytes);
    }

    return ok && nmo_cli_record_heading(rec, "References") &&
           nmo_cli_record_uint(rec, "id_count", "Object IDs", (uint64_t)chunk->ids.count) &&
           nmo_cli_record_uint(rec, "subchunk_count", "Sub-chunks",
                               (uint64_t)nmo_chunk_get_sub_chunk_count(chunk)) &&
           nmo_cli_record_uint(rec, "manager_count", "Manager Refs",
                               (uint64_t)chunk->managers.count);
}

static int chunk_show_run(nmo_cmd_ctx_t *ctx, const chunk_show_args_t *args)
{
    if (!ctx || !args) {
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cmd_ctx_t c = *ctx;
    uint32_t object_id = args->object_id;
    bool flat_index_known = false;
    uint32_t flat_index = 0;

    int64_t parent_index = -1;
    uint32_t depth = 0;

    nmo_object_t *target = NULL;
    nmo_chunk_t *chunk = NULL;

    if (args->use_index) {
        nmo_cli_chunk_entry_t *entries = NULL;
        size_t entry_count = 0;
        if (!collect_all_chunk_entries(c.workspace, &entries, &entry_count, NULL)) {
            fprintf(stderr, "Error: Failed to collect chunks\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        if ((size_t)args->chunk_index >= entry_count) {
            nmo_chunk_index_free_entries(entries);
            fprintf(stderr, "Error: Chunk index %u out of range (0..%zu)\n",
                    args->chunk_index, entry_count ? (entry_count - 1) : 0);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        const nmo_cli_chunk_entry_t selected = entries[args->chunk_index];
        chunk = selected.chunk;
        object_id = selected.owner_object_id;
        parent_index = selected.parent_index;
        depth = selected.depth;
        nmo_chunk_index_free_entries(entries);

        flat_index_known = true;
        flat_index = args->chunk_index;

        /* Best-effort resolve owner object for name */
        target = nmo_core_find_by_id(&c, object_id);
    } else {
        target = nmo_core_find_by_id(&c, object_id);
        if (!target) {
            fprintf(stderr, "Error: Object %u not found\n", object_id);
            return NMO_CLI_EXIT_ARG_ERROR;
        }

        chunk = nmo_object_get_chunk(target);
        if (!chunk) {
            fprintf(stderr, "Error: Object %u has no chunk data\n", object_id);
            return NMO_CLI_EXIT_ARG_ERROR;
        }

        /* Provide stable index info when possible */
        nmo_cli_chunk_entry_t *entries = NULL;
        size_t entry_count = 0;
        if (collect_all_chunk_entries(c.workspace, &entries, &entry_count, NULL) && entries) {
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

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL &&
              nmo_cli_record_title(rec, "Chunk Details") &&
              chunk_show_build_record(&c, args, chunk, target, object_id,
                                      flat_index_known, flat_index,
                                      parent_index, depth, rec);
    if (!ok) {
        nmo_cli_record_free(rec);
        fprintf(stderr, "Error: Out of memory while describing chunk\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return nmo_cmd_ctx_emit_record(&c, rec, "chunk.show", 18, c.colorize);
}

int nmo_cmd_chunk_show(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Modes:
       - nmo chunk show --index <n> <file> (supports sub-chunks)
       - nmo chunk show <object-id> <file> (root chunk for object)
    */
    chunk_show_args_t args;
    int rc = chunk_show_parse(argc, argv, false, &args);
    if (rc) return rc;

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    rc = chunk_show_run(&c, &args);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * chunk find - Find chunks by class
 * ============================================================================ */

static int chunk_find_run(nmo_cmd_ctx_t *ctx, const char *class_filter)
{
    nmo_cmd_ctx_t c = *ctx;

    /* Resolve class filter */
    nmo_class_id_t filter_class_id = nmo_cli_class_id_from_name(c.ctx, class_filter);
    if (!filter_class_id) {
        fprintf(stderr, "Error: Unknown class '%s'\n", class_filter);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cli_chunk_find_data_t find_data = {
        .filter_class_id = filter_class_id,
    };
    int rc = nmo_core_object_query_run(&c, NULL, chunk_find_object,
                                       &find_data, NULL);
    if (rc != NMO_CLI_EXIT_SUCCESS || find_data.matches.allocation_failed) {
        free(find_data.matches.objects);
        fprintf(stderr, "Error: Failed to query objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    size_t match_count = find_data.matches.count;

    static const nmo_cli_table_col_t columns[] = {
        {"Object ID", NMO_CLI_ALIGN_RIGHT, 6, 0},
        {"Class", NMO_CLI_ALIGN_LEFT, 20, 40},
        {"Object Name", NMO_CLI_ALIGN_LEFT, 20, 40},
        {"Size", NMO_CLI_ALIGN_RIGHT, 10, 0},
    };

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_str(rec, "class_filter", NULL, class_filter) &&
         nmo_cli_record_uint(rec, "match_count", NULL, (uint64_t)match_count) &&
         nmo_cli_record_raw_fmt(rec, "Found: %zu chunks (class: %s)\n\n",
                                match_count, class_filter);

    nmo_cli_record_array_t *matches = ok ? nmo_cli_record_array(rec, "matches", NULL) : NULL;
    ok = matches != NULL &&
         nmo_cli_record_array_set_table(matches, columns,
                                        sizeof(columns) / sizeof(columns[0]));
    for (size_t i = 0; ok && i < match_count; ++i) {
        nmo_object_t *obj = find_data.matches.objects[i];
        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL &&
             chunk_find_build_record(&c, obj, nmo_object_get_chunk(obj), item);
        if (!ok) {
            nmo_cli_record_free(item);
        } else {
            ok = nmo_cli_record_array_add(matches, item);
        }
    }
    free(find_data.matches.objects);

    return chunk_emit(&c, rec, ok, "chunk.find", 0);
}

int nmo_cmd_chunk_find(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--class", "-c", NMO_OPT_STRING, "Class name filter"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *class_filter = nmo_opt_str(&vals[0]);
    if (!class_filter) {
        fprintf(stderr, "Error: --class filter required\n");
        fprintf(stderr, "Usage: nmo chunk find --class <name> <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    rc = chunk_find_run(&c, class_filter);
    return nmo_cmd_ctx_done(&c, rc);
}

int nmo_cmd_chunk_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: chunk list|tree|show|find ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "list") == 0 || strcmp(argv[0], "ls") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--top", NULL, NMO_OPT_UINT, "Limit output to first N entries"},
        };
        nmo_opt_val_t vals[1];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
        if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        uint32_t top_n = nmo_opt_uint_or(&vals[0], 0);
        return chunk_list_run(ctx, top_n);
    }
    if (strcmp(argv[0], "tree") == 0 || strcmp(argv[0], "t") == 0) {
        return chunk_tree_run(ctx);
    }
    if (strcmp(argv[0], "show") == 0 || strcmp(argv[0], "s") == 0) {
        chunk_show_args_t args;
        int rc = chunk_show_parse(argc, argv, true, &args);
        if (rc) {
            return rc;
        }
        return chunk_show_run(ctx, &args);
    }
    if (strcmp(argv[0], "find") == 0 || strcmp(argv[0], "f") == 0) {
        static const nmo_opt_def_t opts[] = {
            {"--class", "-c", NMO_OPT_STRING, "Class name filter"},
        };
        nmo_opt_val_t vals[1];
        const char *pos[16];
        nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
        if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) {
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        const char *class_filter = nmo_opt_str(&vals[0]);
        if (!class_filter) {
            fprintf(stderr, "Error: --class filter required\n");
            fprintf(stderr, "Usage: chunk find --class <name>\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        return chunk_find_run(ctx, class_filter);
    }

    fprintf(stderr, "Unsupported chunk read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

