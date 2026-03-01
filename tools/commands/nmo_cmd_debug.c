/**
 * @file nmo_cmd_debug.c
 * @brief CLI debug command group implementation (non-interactive diagnostics)
 */

#include "nmo_cmd_debug.h"

#include "../nmo_cli_common.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_json.h"
#include "../nmo_cli_hex.h"
#include "../nmo_tool_session.h"
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
    const char *file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo debug load-phases <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Get finish loading stats */
    nmo_runtime_load_stats_t stats;
    bool has_stats = (nmo_session_get_runtime_load_stats(session, &stats) == NMO_OK);

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file", file_path);
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

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "debug.load-phases", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        nmo_cli_print_heading(out, "Load Phases", colorize);
        nmo_cli_print_kv(out, "File", file_path, 16, colorize);

        if (!has_stats) {
            fprintf(out, "\nLoad statistics unavailable\n");
        } else {
            char buf[64];
            fprintf(out, "\n");

            snprintf(buf, sizeof(buf), "%zu", stats.total_objects);
            nmo_cli_print_kv(out, "Total Objects", buf, 16, colorize);

            fprintf(out, "\nReferences:\n");
            snprintf(buf, sizeof(buf), "%u", stats.references.total);
            nmo_cli_print_kv(out, "  Total", buf, 14, colorize);
            snprintf(buf, sizeof(buf), "%u", stats.references.resolved);
            nmo_cli_print_kv(out, "  Resolved", buf, 14, colorize);
            snprintf(buf, sizeof(buf), "%u", stats.references.unresolved);
            nmo_cli_print_kv(out, "  Unresolved", buf, 14, colorize);
            snprintf(buf, sizeof(buf), "%u", stats.references.ambiguous);
            nmo_cli_print_kv(out, "  Ambiguous", buf, 14, colorize);

            fprintf(out, "\nIndexes:\n");
            snprintf(buf, sizeof(buf), "%zu", stats.indexes.class_entries);
            nmo_cli_print_kv(out, "  Classes", buf, 14, colorize);
            snprintf(buf, sizeof(buf), "%zu", stats.indexes.name_entries);
            nmo_cli_print_kv(out, "  Names", buf, 14, colorize);
            snprintf(buf, sizeof(buf), "%zu", stats.indexes.guid_entries);
            nmo_cli_print_kv(out, "  GUIDs", buf, 14, colorize);
            snprintf(buf, sizeof(buf), "%zu bytes", stats.indexes.memory_usage);
            nmo_cli_print_kv(out, "  Memory", buf, 14, colorize);

            fprintf(out, "\n");
            snprintf(buf, sizeof(buf), "%u", stats.manager_errors);
            nmo_cli_print_kv(out, "Manager Errors", buf, 16, colorize);
        }
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * debug chunks - Iterate objects to list chunk debug info
 * ============================================================================ */

int nmo_cmd_debug_chunks(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo debug chunks <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to get objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Count chunks */
    size_t chunk_count = 0;
    for (size_t i = 0; i < object_count; ++i) {
        if (nmo_object_get_chunk(objects[i])) {
            chunk_count++;
        }
    }

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)object_count);
        yyjson_mut_obj_add_uint(doc, data, "chunk_count", (uint64_t)chunk_count);

        /* Detailed chunk info */
        yyjson_mut_val *chunks = yyjson_mut_arr(doc);
        for (size_t i = 0; i < object_count; ++i) {
            nmo_object_t *obj = objects[i];
            nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
            if (!chunk) continue;

            yyjson_mut_val *c = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, c, "object_id", nmo_object_get_id(obj));
            yyjson_mut_obj_add_uint(doc, c, "class_id", chunk->class_id);
            yyjson_mut_obj_add_uint(doc, c, "data_size", (uint64_t)nmo_chunk_get_data_size(chunk));
            yyjson_mut_obj_add_uint(doc, c, "compressed_size", (uint64_t)chunk->compressed_size);
            yyjson_mut_obj_add_uint(doc, c, "options", chunk->chunk_options);

            const char *class_name = nmo_cli_class_name_from_id(ctx, chunk->class_id);
            if (class_name) {
                yyjson_mut_obj_add_str(doc, c, "class_name", class_name);
            }

            const char *name = nmo_object_get_name(obj);
            if (name && name[0]) {
                nmo_cli_json_add_str_safe(doc, c, "object_name", name);
            }

            yyjson_mut_arr_add_val(chunks, c);
        }
        yyjson_mut_obj_add_val(doc, data, "chunks", chunks);

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "debug.chunks", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        nmo_cli_print_heading(out, "Chunk Debug Info", colorize);
        fprintf(out, "Chunks: %zu (from %zu objects)\n\n", chunk_count, object_count);

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

            const char *class_name = nmo_cli_class_name_from_id(ctx, chunk->class_id);

            const char *cells[] = {oid, cid, class_name ? class_name : "-", dsz, csz, opt_cell};
            nmo_cli_table_add_row(&table, cells, 6);
        }

        nmo_cli_table_print(&table, out, colorize);
        nmo_cli_table_free(&table);
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * debug objects
 * ============================================================================ */

int nmo_cmd_debug_objects(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo debug objects <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to get objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    char out_err[128];
    FILE *out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    bool colorize = nmo_cli_should_colorize(global, out);

    if (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY) {
        yyjson_mut_doc *doc = nmo_cli_json_create_doc();
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

            const char *class_name = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(obj));
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

        yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "debug.objects", file_path);
        yyjson_mut_doc_set_root(doc, root);
        nmo_cli_json_write(doc, out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        nmo_cli_json_free_doc(doc);
    } else {
        nmo_cli_print_heading(out, "Object Debug Info", colorize);
        fprintf(out, "Objects: %zu\n\n", object_count);

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

            const char *class_name = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(obj));
            const char *name = nmo_object_get_name(obj);

            const char *cells[] = {
                idx, id, flags,
                class_name ? class_name : "-",
                (name && name[0]) ? name : "-",
                chunk_sz
            };
            nmo_cli_table_add_row(&table, cells, 6);
        }

        nmo_cli_table_print(&table, out, colorize);
        nmo_cli_table_free(&table);
    }

    nmo_tool_close_session(ctx, session);
    nmo_cli_close_output_stream(global, out);
    return NMO_CLI_EXIT_SUCCESS;
}

/* ============================================================================
 * debug export
 * ============================================================================ */

int nmo_cmd_debug_export(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    const char *file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo debug export <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

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

    /* Open session */
    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_object_t **objects = NULL;
    size_t object_count = 0;
    if (nmo_session_get_objects(session, &objects, &object_count) != NMO_OK) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: Failed to get objects\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    FILE *json_out = stdout;
    char out_err[128];
    json_out = nmo_cli_get_output_stream(global, out_err, sizeof(out_err));
    if (!json_out) {
        nmo_tool_close_session(ctx, session);
        fprintf(stderr, "Error: %s\n", out_err);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    bool is_json = (global->format == NMO_CLI_FORMAT_JSON || global->format == NMO_CLI_FORMAT_JSON_PRETTY);

    yyjson_mut_doc *doc = nmo_cli_json_create_doc();
    yyjson_mut_val *data = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, data, "file", file_path);
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

        const char *class_name = nmo_cli_class_name_from_id(ctx, nmo_object_get_class_id(obj));
        if (class_name) {
            yyjson_mut_obj_add_str(doc, o, "class_name", class_name);
        }

        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        if (chunk) {
            yyjson_mut_val *c = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, c, "class_id", chunk->class_id);
            yyjson_mut_obj_add_uint(doc, c, "data_size", (uint64_t)nmo_chunk_get_data_size(chunk));
            yyjson_mut_obj_add_uint(doc, c, "compressed_size", (uint64_t)chunk->compressed_size);
            yyjson_mut_obj_add_uint(doc, c, "uncompressed_size", (uint64_t)chunk->uncompressed_size);
            yyjson_mut_obj_add_uint(doc, c, "options", (uint64_t)chunk->chunk_options);
            yyjson_mut_obj_add_uint(doc, c, "id_count", (uint64_t)nmo_chunk_get_id_count(chunk));
            yyjson_mut_obj_add_uint(doc, c, "subchunk_count", (uint64_t)nmo_chunk_get_sub_chunk_count(chunk));

            if (include_data) {
                size_t data_size = 0;
                const uint8_t *data = (const uint8_t *)nmo_chunk_get_data(chunk, &data_size);
                (void)nmo_cli_json_add_data_hex(doc, c, data, data_size, max_bytes, false);
            }

            yyjson_mut_obj_add_val(doc, o, "chunk", c);
        }

        yyjson_mut_arr_add_val(objs, o);
    }

    yyjson_mut_obj_add_val(doc, data, "objects", objs);
    yyjson_mut_val *root = nmo_cli_json_add_envelope(doc, data, "debug.export", file_path);
    yyjson_mut_doc_set_root(doc, root);
    nmo_cli_json_write(doc, json_out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    nmo_cli_json_free_doc(doc);

    if (!is_json && global->output_path) {
        fprintf(stdout, "Exported %zu objects to %s\n", object_count, global->output_path);
    }

    nmo_cli_close_output_stream(global, json_out);
    nmo_tool_close_session(ctx, session);
    return NMO_CLI_EXIT_SUCCESS;
}
