/**
 * @file nmo_cmd_debug.c
 * @brief CLI debug command group implementation (non-interactive diagnostics)
 */

#include "nmo_cmd_debug.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "app/nmo_stats.h"
#include "app/nmo_context.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * debug load-phases
 * ============================================================================ */

int nmo_cmd_debug_load_phases(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Get finish loading stats */
    nmo_runtime_load_stats_t stats;
    bool has_stats = (nmo_session_get_runtime_load_stats(c.session, &stats) == NMO_OK);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file", c.file_path);
        yyjson_mut_obj_add_bool(doc, data, "stats_available", has_stats);

        if (has_stats) {
            yyjson_mut_obj_add_uint(doc, data, "total_objects", (uint64_t)stats.total_objects);

            yyjson_mut_val *refs = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, refs, "total", stats.references.total);
            yyjson_mut_obj_add_uint(doc, refs, "resolved", stats.references.resolved);
            yyjson_mut_obj_add_uint(doc, refs, "unresolved", stats.references.unresolved);
            yyjson_mut_obj_add_uint(doc, refs, "ambiguous", stats.references.ambiguous);
            yyjson_mut_obj_add_val(doc, data, "references", refs);

            yyjson_mut_val *idx = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, idx, "class_entries", (uint64_t)stats.indexes.class_entries);
            yyjson_mut_obj_add_uint(doc, idx, "name_entries", (uint64_t)stats.indexes.name_entries);
            yyjson_mut_obj_add_uint(doc, idx, "guid_entries", (uint64_t)stats.indexes.guid_entries);
            yyjson_mut_obj_add_uint(doc, idx, "memory_usage", (uint64_t)stats.indexes.memory_usage);
            yyjson_mut_obj_add_val(doc, data, "indexes", idx);

            yyjson_mut_obj_add_uint(doc, data, "manager_errors", stats.manager_errors);
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "debug.load-phases");
    } else {
        nmo_cli_print_heading(c.out, "Load Phases", c.colorize);
        nmo_cli_print_kv(c.out, "File", c.file_path, 16, c.colorize);

        if (!has_stats) {
            fprintf(c.out, "\nLoad statistics unavailable\n");
        } else {
            char buf[64];
            fprintf(c.out, "\n");

            snprintf(buf, sizeof(buf), "%zu", stats.total_objects);
            nmo_cli_print_kv(c.out, "Total Objects", buf, 16, c.colorize);

            fprintf(c.out, "\nReferences:\n");
            snprintf(buf, sizeof(buf), "%u", stats.references.total);
            nmo_cli_print_kv(c.out, "  Total", buf, 14, c.colorize);
            snprintf(buf, sizeof(buf), "%u", stats.references.resolved);
            nmo_cli_print_kv(c.out, "  Resolved", buf, 14, c.colorize);
            snprintf(buf, sizeof(buf), "%u", stats.references.unresolved);
            nmo_cli_print_kv(c.out, "  Unresolved", buf, 14, c.colorize);
            snprintf(buf, sizeof(buf), "%u", stats.references.ambiguous);
            nmo_cli_print_kv(c.out, "  Ambiguous", buf, 14, c.colorize);

            fprintf(c.out, "\nIndexes:\n");
            snprintf(buf, sizeof(buf), "%zu", stats.indexes.class_entries);
            nmo_cli_print_kv(c.out, "  Classes", buf, 14, c.colorize);
            snprintf(buf, sizeof(buf), "%zu", stats.indexes.name_entries);
            nmo_cli_print_kv(c.out, "  Names", buf, 14, c.colorize);
            snprintf(buf, sizeof(buf), "%zu", stats.indexes.guid_entries);
            nmo_cli_print_kv(c.out, "  GUIDs", buf, 14, c.colorize);
            snprintf(buf, sizeof(buf), "%zu bytes", stats.indexes.memory_usage);
            nmo_cli_print_kv(c.out, "  Memory", buf, 14, c.colorize);

            fprintf(c.out, "\n");
            snprintf(buf, sizeof(buf), "%u", stats.manager_errors);
            nmo_cli_print_kv(c.out, "Manager Errors", buf, 16, c.colorize);
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * debug chunks - Iterate objects to list chunk debug info
 * ============================================================================ */

int nmo_cmd_debug_chunks(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    /* Count chunks */
    size_t chunk_count = 0;
    for (size_t i = 0; i < object_count; ++i) {
        if (nmo_object_get_chunk(objects[i])) {
            chunk_count++;
        }
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)object_count);
        yyjson_mut_obj_add_uint(doc, data, "chunk_count", (uint64_t)chunk_count);

        /* Detailed chunk info */
        yyjson_mut_val *chunks = yyjson_mut_arr(doc);
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
            if (!chunk) continue;

            yyjson_mut_val *cv = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, cv, "object_id", nmo_object_get_id(obj));
            yyjson_mut_obj_add_uint(doc, cv, "class_id", chunk->class_id);
            yyjson_mut_obj_add_uint(doc, cv, "data_size", (uint64_t)nmo_chunk_get_data_size(chunk));
            yyjson_mut_obj_add_uint(doc, cv, "compressed_size", (uint64_t)chunk->compressed_size);
            yyjson_mut_obj_add_uint(doc, cv, "options", chunk->chunk_options);

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, chunk->class_id);
            if (class_name) {
                yyjson_mut_obj_add_str(doc, cv, "class_name", class_name);
            }

            const char *name = nmo_object_get_name(obj);
            if (name && name[0]) {
                nmo_cli_json_add_str_safe(doc, cv, "object_name", name);
            }

            yyjson_mut_arr_add_val(chunks, cv);
        }
        yyjson_mut_obj_add_val(doc, data, "chunks", chunks);

        nmo_cmd_ctx_json_end(&c, doc, data, "debug.chunks");
    } else {
        nmo_cli_print_heading(c.out, "Chunk Debug Info", c.colorize);
        fprintf(c.out, "Chunks: %zu (from %zu objects)\n\n", chunk_count, object_count);

        static const nmo_cli_table_col_t columns[] = {
            {"ObjectID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"ClassID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 15, 25},
            {"DataSize", NMO_CLI_ALIGN_RIGHT, 8, 0},
            {"PackSize", NMO_CLI_ALIGN_RIGHT, 8, 0},
            {"Options", NMO_CLI_ALIGN_LEFT, 8, 32},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
            if (!chunk) continue;

            char oid[16], cid[16], dsz[16], csz[16];
            char opt_buf[64];
            char opt_cell[96];
            snprintf(oid, sizeof(oid), "%u", nmo_object_get_id(obj));
            snprintf(cid, sizeof(cid), "%u", chunk->class_id);
            snprintf(dsz, sizeof(dsz), "%zu", nmo_chunk_get_data_size(chunk));
            snprintf(csz, sizeof(csz), "%zu", chunk->compressed_size);

            const char *opt = nmo_cli_chunk_options_to_string(chunk->chunk_options,
                opt_buf, sizeof(opt_buf));
            if (chunk->chunk_options == 0) {
                snprintf(opt_cell, sizeof(opt_cell), "-");
            } else {
                snprintf(opt_cell, sizeof(opt_cell), "%s (0x%04X)", opt, chunk->chunk_options);
            }

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, chunk->class_id);

            const char *cells[] = {oid, cid, class_name ? class_name : "-", dsz, csz, opt_cell};
            nmo_cli_table_add_row(&table, cells, 6);
        }

        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * debug objects
 * ============================================================================ */

int nmo_cmd_debug_objects(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)object_count);

        yyjson_mut_val *objs = yyjson_mut_arr(doc);
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];

            yyjson_mut_val *o = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, o, "index", (uint64_t)i);
            yyjson_mut_obj_add_uint(doc, o, "id", nmo_object_get_id(obj));
            yyjson_mut_obj_add_uint(doc, o, "class_id", nmo_object_get_class_id(obj));
            yyjson_mut_obj_add_uint(doc, o, "flags", nmo_object_get_flags(obj));

            const char *name = nmo_object_get_name(obj);
            if (name && name[0]) {
                nmo_cli_json_add_str_safe(doc, o, "name", name);
            }

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, nmo_object_get_class_id(obj));
            if (class_name) {
                yyjson_mut_obj_add_str(doc, o, "class_name", class_name);
            }

            nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
            yyjson_mut_obj_add_bool(doc, o, "has_chunk", chunk != NULL);
            if (chunk) {
                yyjson_mut_obj_add_uint(doc, o, "chunk_size", (uint64_t)nmo_chunk_get_data_size(chunk));
            }

            yyjson_mut_arr_add_val(objs, o);
        }
        yyjson_mut_obj_add_val(doc, data, "objects", objs);

        nmo_cmd_ctx_json_end(&c, doc, data, "debug.objects");
    } else {
        nmo_cli_print_heading(c.out, "Object Debug Info", c.colorize);
        fprintf(c.out, "Objects: %zu\n\n", object_count);

        static const nmo_cli_table_col_t columns[] = {
            {"Idx", NMO_CLI_ALIGN_RIGHT, 4, 0},
            {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
            {"Flags", NMO_CLI_ALIGN_RIGHT, 10, 0},
            {"Class", NMO_CLI_ALIGN_LEFT, 15, 25},
            {"Name", NMO_CLI_ALIGN_LEFT, 20, 40},
            {"Chunk", NMO_CLI_ALIGN_RIGHT, 8, 0},
        };

        nmo_cli_table_t table;
        nmo_cli_table_init(&table, columns, sizeof(columns) / sizeof(columns[0]));

        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];

            char idx[24], id[16], flags[16], chunk_sz[24];
            snprintf(idx, sizeof(idx), "%zu", i);
            snprintf(id, sizeof(id), "%u", nmo_object_get_id(obj));
            snprintf(flags, sizeof(flags), "0x%08X", nmo_object_get_flags(obj));

            nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
            if (chunk) {
                snprintf(chunk_sz, sizeof(chunk_sz), "%zu", nmo_chunk_get_data_size(chunk));
            } else {
                snprintf(chunk_sz, sizeof(chunk_sz), "-");
            }

            const char *class_name = nmo_cli_class_name_from_id(c.ctx, nmo_object_get_class_id(obj));
            const char *name = nmo_object_get_name(obj);

            const char *cells[] = {
                idx, id, flags,
                class_name ? class_name : "-",
                (name && name[0]) ? name : "-",
                chunk_sz
            };
            nmo_cli_table_add_row(&table, cells, 6);
        }

        nmo_cli_table_print(&table, c.out, c.colorize);
        nmo_cli_table_free(&table);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * debug export
 * ============================================================================ */

int nmo_cmd_debug_export(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    bool include_data = false;
    size_t max_bytes = 4096;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--data") == 0 || strcmp(argv[i], "--include-data") == 0) {
            include_data = true;
        } else if (strcmp(argv[i], "--max-bytes") == 0 && (i + 1) < argc) {
            size_t value = 0;
            if (nmo_tool_parse_size(argv[i + 1], &value) && value > 0) {
                max_bytes = value;
            }
            i++;
        }
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(c.session, &objects, &object_count) != NMO_OK) {
        fprintf(stderr, "Error: Failed to get objects\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
    yyjson_mut_val *data = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, data, "file", c.file_path);
    yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)object_count);
    yyjson_mut_obj_add_bool(doc, data, "include_data", include_data);
    yyjson_mut_obj_add_uint(doc, data, "max_bytes", (uint64_t)max_bytes);

    yyjson_mut_val *objs = yyjson_mut_arr(doc);
    for (size_t i = 0; i < object_count; ++i) {
        nmo_object_t *obj = objects[i];
        yyjson_mut_val *o = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, o, "index", (uint64_t)i);
        yyjson_mut_obj_add_uint(doc, o, "id", nmo_object_get_id(obj));
        yyjson_mut_obj_add_uint(doc, o, "class_id", nmo_object_get_class_id(obj));
        yyjson_mut_obj_add_uint(doc, o, "flags", nmo_object_get_flags(obj));

        const char *name = nmo_object_get_name(obj);
        if (name && name[0]) {
            nmo_cli_json_add_str_safe(doc, o, "name", name);
        }

        const char *class_name = nmo_cli_class_name_from_id(c.ctx, nmo_object_get_class_id(obj));
        if (class_name) {
            yyjson_mut_obj_add_str(doc, o, "class_name", class_name);
        }

        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        if (chunk) {
            yyjson_mut_val *cv = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, cv, "class_id", chunk->class_id);
            yyjson_mut_obj_add_uint(doc, cv, "data_size", (uint64_t)nmo_chunk_get_data_size(chunk));
            yyjson_mut_obj_add_uint(doc, cv, "compressed_size", (uint64_t)chunk->compressed_size);
            yyjson_mut_obj_add_uint(doc, cv, "uncompressed_size", (uint64_t)chunk->uncompressed_size);
            yyjson_mut_obj_add_uint(doc, cv, "options", (uint64_t)chunk->chunk_options);
            yyjson_mut_obj_add_uint(doc, cv, "id_count", (uint64_t)nmo_chunk_get_id_count(chunk));
            yyjson_mut_obj_add_uint(doc, cv, "subchunk_count", (uint64_t)nmo_chunk_get_sub_chunk_count(chunk));

            if (include_data) {
                size_t data_size = 0;
                const uint8_t *chunk_data = (const uint8_t *)nmo_chunk_get_data(chunk, &data_size);
                (void)nmo_cli_json_add_data_hex(doc, cv, chunk_data, data_size, max_bytes, false);
            }

            yyjson_mut_obj_add_val(doc, o, "chunk", cv);
        }

        yyjson_mut_arr_add_val(objs, o);
    }

    yyjson_mut_obj_add_val(doc, data, "objects", objs);
    nmo_cmd_ctx_json_end(&c, doc, data, "debug.export");

    if (!c.is_json && global->output_path) {
        fprintf(stdout, "Exported %zu objects to %s\n", object_count, global->output_path);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

