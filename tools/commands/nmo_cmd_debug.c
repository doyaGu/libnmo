/**
 * @file nmo_cmd_debug.c
 * @brief CLI debug command group implementation (non-interactive diagnostics)
 */

#include "nmo_cmd_debug.h"

#include "../nmo_cmd_core.h"
#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"
#include "../nmo_opt.h"

#include "nmo.h"
#include "app/nmo_stats.h"
#include "session/nmo_context.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct nmo_debug_chunks_data {
    yyjson_mut_doc *doc;
    yyjson_mut_val *chunks;
    nmo_cli_table_t *table;
    size_t chunk_count;
} nmo_debug_chunks_data_t;

static void debug_add_load_phase_stats_json(yyjson_mut_doc *doc,
                                            yyjson_mut_val *data,
                                            const nmo_load_perf_stats_t *stats) {
    if (doc == NULL || data == NULL || stats == NULL) {
        return;
    }

    yyjson_mut_val *phase_stats = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, phase_stats, "packed_header1_bytes", (uint64_t)stats->packed_header1_bytes);
    yyjson_mut_obj_add_uint(doc, phase_stats, "unpacked_header1_bytes", (uint64_t)stats->unpacked_header1_bytes);
    yyjson_mut_obj_add_uint(doc, phase_stats, "packed_data_bytes", (uint64_t)stats->packed_data_bytes);
    yyjson_mut_obj_add_uint(doc, phase_stats, "unpacked_data_bytes", (uint64_t)stats->unpacked_data_bytes);

    yyjson_mut_val *phases = yyjson_mut_obj(doc);
    for (int i = 0; i < NMO_LOAD_PERF_PHASE_COUNT; i++) {
        const nmo_phase_time_t *phase = &stats->phases[i];
        yyjson_mut_val *entry = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, entry, "calls", phase->calls);
        yyjson_mut_obj_add_real(doc, entry, "milliseconds", phase->milliseconds);
        yyjson_mut_obj_add_val(doc, phases, nmo_load_perf_phase_name((nmo_load_perf_phase_t)i), entry);
    }
    yyjson_mut_obj_add_val(doc, phase_stats, "phases", phases);
    yyjson_mut_obj_add_val(doc, data, "phase_stats", phase_stats);
}

static void debug_print_load_phase_stats(FILE *out, const nmo_load_perf_stats_t *stats) {
    if (out == NULL || stats == NULL) {
        return;
    }

    fprintf(out, "\nPhase Timings:\n");
    fprintf(out, "  %-28s %8s %12s\n", "phase", "calls", "ms");
    for (int i = 0; i < NMO_LOAD_PERF_PHASE_COUNT; i++) {
        const nmo_phase_time_t *phase = &stats->phases[i];
        fprintf(out, "  %-28s %8llu %12.3f\n",
                nmo_load_perf_phase_name((nmo_load_perf_phase_t)i),
                (unsigned long long)phase->calls,
                phase->milliseconds);
    }

    fprintf(out, "\nSection Bytes:\n");
    fprintf(out, "  Header1: packed=%zu unpacked=%zu\n",
            stats->packed_header1_bytes,
            stats->unpacked_header1_bytes);
    fprintf(out, "  Data:    packed=%zu unpacked=%zu\n",
            stats->packed_data_bytes,
            stats->unpacked_data_bytes);
}

static bool debug_load_profile_from_arg(const char *arg, nmo_load_profile_t *out_profile) {
    if (arg == NULL || out_profile == NULL) {
        return false;
    }
    if (strcmp(arg, "full") == 0) {
        *out_profile = NMO_LOAD_PROFILE_FULL;
        return true;
    }
    if (strcmp(arg, "metadata") == 0) {
        *out_profile = NMO_LOAD_PROFILE_METADATA;
        return true;
    }
    if (strcmp(arg, "header") == 0 || strcmp(arg, "header-only") == 0) {
        *out_profile = NMO_LOAD_PROFILE_HEADER_ONLY;
        return true;
    }
    return false;
}

static const char *debug_load_profile_name(nmo_load_profile_t profile) {
    switch (profile) {
        case NMO_LOAD_PROFILE_FULL:
            return "full";
        case NMO_LOAD_PROFILE_METADATA:
            return "metadata";
        case NMO_LOAD_PROFILE_HEADER_ONLY:
            return "header-only";
        default:
            return "unknown";
    }
}

static int debug_parse_load_profile(int argc, char **argv,
                                    nmo_load_profile_t *profile)
{
    *profile = NMO_LOAD_PROFILE_FULL;
    for (int i = 0; i < argc; i++) {
        const char *value = NULL;
        if (strncmp(argv[i], "--profile=", 10) == 0) {
            value = argv[i] + 10;
        } else if (strncmp(argv[i], "--load-profile=", 15) == 0) {
            value = argv[i] + 15;
        }

        if (value != NULL && !debug_load_profile_from_arg(value, profile)) {
            fprintf(stderr, "Error: Invalid load profile '%s'\n", value);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }
    return NMO_CLI_EXIT_SUCCESS;
}

static int debug_chunks_object(size_t index, nmo_object_t *obj,
                               const nmo_cmd_ctx_t *c, void *user)
{
    (void)index;

    nmo_debug_chunks_data_t *data = (nmo_debug_chunks_data_t *)user;
    if (!data || !obj) {
        return 0;
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (!chunk) {
        return 0;
    }

    if (data->doc && data->chunks) {
        yyjson_mut_val *cv = yyjson_mut_obj(data->doc);
        yyjson_mut_obj_add_uint(data->doc, cv, "id", nmo_object_get_id(obj));
        yyjson_mut_obj_add_uint(data->doc, cv, "class_id", chunk->class_id);
        yyjson_mut_obj_add_uint(data->doc, cv, "data_size",
                                (uint64_t)nmo_chunk_get_data_size(chunk));
        yyjson_mut_obj_add_uint(data->doc, cv, "compressed_size",
                                (uint64_t)chunk->compressed_size);
        yyjson_mut_obj_add_uint(data->doc, cv, "options", chunk->chunk_options);

        const char *class_name = nmo_cli_class_name_from_id(c->ctx, chunk->class_id);
        if (class_name) {
            yyjson_mut_obj_add_str(data->doc, cv, "class_name", class_name);
        }

        const char *name = nmo_object_get_name(obj);
        if (name && name[0]) {
            nmo_cli_json_add_str_safe(data->doc, cv, "name", name);
        }

        yyjson_mut_arr_add_val(data->chunks, cv);
    } else if (data->table) {
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

        const char *class_name = nmo_cli_class_name_from_id(c->ctx, chunk->class_id);

        const char *cells[] = {oid, cid, class_name ? class_name : "-", dsz, csz, opt_cell};
        (void)nmo_cli_table_add_row(data->table, cells, 6);
    }

    data->chunk_count++;
    return 0;
}

typedef struct nmo_debug_objects_data {
    yyjson_mut_doc *doc;
    yyjson_mut_val *objects;
    nmo_cli_table_t *table;
} nmo_debug_objects_data_t;

static int debug_objects_object(size_t index, nmo_object_t *obj,
                                const nmo_cmd_ctx_t *c, void *user)
{
    nmo_debug_objects_data_t *data = (nmo_debug_objects_data_t *)user;
    if (!data || !obj) {
        return 0;
    }

    if (data->doc && data->objects) {
        yyjson_mut_val *o = yyjson_mut_obj(data->doc);
        yyjson_mut_obj_add_uint(data->doc, o, "index", (uint64_t)index);
        yyjson_mut_obj_add_uint(data->doc, o, "id", nmo_object_get_id(obj));
        yyjson_mut_obj_add_uint(data->doc, o, "class_id", nmo_object_get_class_id(obj));
        yyjson_mut_obj_add_uint(data->doc, o, "flags", nmo_object_get_flags(obj));

        const char *name = nmo_object_get_name(obj);
        if (name && name[0]) {
            nmo_cli_json_add_str_safe(data->doc, o, "name", name);
        }

        const char *class_name = nmo_cli_class_name_from_id(c->ctx, nmo_object_get_class_id(obj));
        if (class_name) {
            yyjson_mut_obj_add_str(data->doc, o, "class_name", class_name);
        }

        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        yyjson_mut_obj_add_bool(data->doc, o, "has_chunk", chunk != NULL);
        if (chunk) {
            yyjson_mut_obj_add_uint(data->doc, o, "chunk_size",
                                    (uint64_t)nmo_chunk_get_data_size(chunk));
        }

        yyjson_mut_arr_add_val(data->objects, o);
    } else if (data->table) {
        char idx[24], id[16], flags[16], chunk_sz[24];
        snprintf(idx, sizeof(idx), "%zu", index);
        snprintf(id, sizeof(id), "%u", nmo_object_get_id(obj));
        snprintf(flags, sizeof(flags), "0x%08X", nmo_object_get_flags(obj));

        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        if (chunk) {
            snprintf(chunk_sz, sizeof(chunk_sz), "%zu", nmo_chunk_get_data_size(chunk));
        } else {
            snprintf(chunk_sz, sizeof(chunk_sz), "-");
        }

        const char *class_name = nmo_cli_class_name_from_id(c->ctx, nmo_object_get_class_id(obj));
        const char *name = nmo_object_get_name(obj);

        const char *cells[] = {
            idx, id, flags,
            class_name ? class_name : "-",
            (name && name[0]) ? name : "-",
            chunk_sz
        };
        (void)nmo_cli_table_add_row(data->table, cells, 6);
    }

    return 0;
}

typedef struct nmo_debug_export_data {
    yyjson_mut_doc *doc;
    yyjson_mut_val *objects;
    bool include_data;
    size_t max_bytes;
} nmo_debug_export_data_t;

static int debug_export_object(size_t index, nmo_object_t *obj,
                               const nmo_cmd_ctx_t *c, void *user)
{
    nmo_debug_export_data_t *data = (nmo_debug_export_data_t *)user;
    if (!data || !data->doc || !data->objects || !obj) {
        return 0;
    }

    yyjson_mut_val *o = yyjson_mut_obj(data->doc);
    yyjson_mut_obj_add_uint(data->doc, o, "index", (uint64_t)index);
    yyjson_mut_obj_add_uint(data->doc, o, "id", nmo_object_get_id(obj));
    yyjson_mut_obj_add_uint(data->doc, o, "class_id", nmo_object_get_class_id(obj));
    yyjson_mut_obj_add_uint(data->doc, o, "flags", nmo_object_get_flags(obj));

    const char *name = nmo_object_get_name(obj);
    if (name && name[0]) {
        nmo_cli_json_add_str_safe(data->doc, o, "name", name);
    }

    const char *class_name = nmo_cli_class_name_from_id(c->ctx, nmo_object_get_class_id(obj));
    if (class_name) {
        yyjson_mut_obj_add_str(data->doc, o, "class_name", class_name);
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    if (chunk) {
        yyjson_mut_val *cv = yyjson_mut_obj(data->doc);
        yyjson_mut_obj_add_uint(data->doc, cv, "class_id", chunk->class_id);
        yyjson_mut_obj_add_uint(data->doc, cv, "data_size",
                                (uint64_t)nmo_chunk_get_data_size(chunk));
        yyjson_mut_obj_add_uint(data->doc, cv, "compressed_size",
                                (uint64_t)chunk->compressed_size);
        yyjson_mut_obj_add_uint(data->doc, cv, "uncompressed_size",
                                (uint64_t)chunk->uncompressed_size);
        yyjson_mut_obj_add_uint(data->doc, cv, "options", (uint64_t)chunk->chunk_options);
        yyjson_mut_obj_add_uint(data->doc, cv, "id_count",
                                (uint64_t)nmo_chunk_get_id_count(chunk));
        yyjson_mut_obj_add_uint(data->doc, cv, "subchunk_count",
                                (uint64_t)nmo_chunk_get_sub_chunk_count(chunk));

        if (data->include_data) {
            size_t data_size = 0;
            const uint8_t *chunk_data = (const uint8_t *)nmo_chunk_get_data(chunk, &data_size);
            (void)nmo_cli_json_add_data_hex(data->doc, cv, chunk_data,
                                            data_size, data->max_bytes, false);
        }

        yyjson_mut_obj_add_val(data->doc, o, "chunk", cv);
    }

    yyjson_mut_arr_add_val(data->objects, o);
    return 0;
}

/* ============================================================================
 * debug load-phases
 * ============================================================================ */

static int debug_load_phases_run_in_ctx(nmo_cmd_ctx_t *c,
                                        nmo_load_profile_t profile,
                                        const nmo_load_perf_stats_t *phase_stats,
                                        bool close_ctx)
{
    nmo_load_perf_stats_t empty_phase_stats;
    if (!phase_stats) {
        nmo_load_perf_stats_reset(&empty_phase_stats);
        phase_stats = &empty_phase_stats;
    }

    /* Get finish loading stats */
    nmo_runtime_load_stats_t stats;
    bool has_stats = (nmo_session_get_runtime_load_stats(c->session, &stats) == NMO_OK);

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file", c->file_path);
        yyjson_mut_obj_add_str(doc, data, "profile", debug_load_profile_name(profile));
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
        debug_add_load_phase_stats_json(doc, data, phase_stats);

        nmo_cmd_ctx_json_end(c, doc, data, "debug.load-phases");
    } else {
        nmo_cli_print_heading(c->out, "Load Phases", c->colorize);
        nmo_cli_print_kv(c->out, "File", c->file_path, 16, c->colorize);
        nmo_cli_print_kv(c->out, "Profile", debug_load_profile_name(profile), 16, c->colorize);

        if (!has_stats) {
            fprintf(c->out, "\nLoad statistics unavailable\n");
        } else {
            char buf[64];
            fprintf(c->out, "\n");

            snprintf(buf, sizeof(buf), "%zu", stats.total_objects);
            nmo_cli_print_kv(c->out, "Total Objects", buf, 16, c->colorize);

            fprintf(c->out, "\nReferences:\n");
            snprintf(buf, sizeof(buf), "%u", stats.references.total);
            nmo_cli_print_kv(c->out, "  Total", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%u", stats.references.resolved);
            nmo_cli_print_kv(c->out, "  Resolved", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%u", stats.references.unresolved);
            nmo_cli_print_kv(c->out, "  Unresolved", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%u", stats.references.ambiguous);
            nmo_cli_print_kv(c->out, "  Ambiguous", buf, 14, c->colorize);

            fprintf(c->out, "\nIndexes:\n");
            snprintf(buf, sizeof(buf), "%zu", stats.indexes.class_entries);
            nmo_cli_print_kv(c->out, "  Classes", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%zu", stats.indexes.name_entries);
            nmo_cli_print_kv(c->out, "  Names", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%zu", stats.indexes.guid_entries);
            nmo_cli_print_kv(c->out, "  GUIDs", buf, 14, c->colorize);
            snprintf(buf, sizeof(buf), "%zu bytes", stats.indexes.memory_usage);
            nmo_cli_print_kv(c->out, "  Memory", buf, 14, c->colorize);

            fprintf(c->out, "\n");
            snprintf(buf, sizeof(buf), "%u", stats.manager_errors);
            nmo_cli_print_kv(c->out, "Manager Errors", buf, 16, c->colorize);
        }
        debug_print_load_phase_stats(c->out, phase_stats);
    }

    return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_debug_load_phases(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (rc) return rc;

    nmo_load_profile_t profile;
    rc = debug_parse_load_profile(argc, argv, &profile);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, rc);
    }

    c.file_path = nmo_tool_find_file_arg_last(argc, argv);
    if (!c.file_path) {
        fprintf(stderr, "Error: No file specified\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    nmo_load_perf_stats_t phase_stats;
    nmo_load_perf_stats_reset(&phase_stats);

    nmo_load_options_t load_opts = nmo_load_options_default();
    load_opts.profile = profile;
    load_opts.collect_perf_stats = true;
    load_opts.perf_stats = &phase_stats;

    char errbuf[256];
    if (!nmo_tool_open_session_opts(c.file_path, &load_opts,
                                    &c.ctx, &c.session,
                                    errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }
    c.owns_session = true;
    c.registry = nmo_context_get_type_registry(c.ctx);
    return debug_load_phases_run_in_ctx(&c, profile, &phase_stats, true);
}

static int nmo_cmd_debug_load_phases_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    nmo_load_profile_t profile;
    int rc = debug_parse_load_profile(argc, argv, &profile);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    return debug_load_phases_run_in_ctx(ctx, profile, NULL, false);
}

/* ============================================================================
 * debug chunks - Iterate objects to list chunk debug info
 * ============================================================================ */

static int debug_chunks_run_in_ctx(nmo_cmd_ctx_t *c, bool close_ctx)
{
    int rc = NMO_CLI_EXIT_SUCCESS;
    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *chunks = yyjson_mut_arr(doc);
        nmo_debug_chunks_data_t chunks_data = {
            .doc = doc,
            .chunks = chunks,
        };
        nmo_core_iter_result_t result = {0};
        rc = nmo_core_object_query_run(c, NULL, debug_chunks_object,
                                       &chunks_data, &result);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            yyjson_mut_doc_free(doc);
            fprintf(stderr, "Error: Failed to query objects\n");
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)result.matched);
        yyjson_mut_obj_add_uint(doc, data, "chunk_count",
                                (uint64_t)chunks_data.chunk_count);
        yyjson_mut_obj_add_val(doc, data, "chunks", chunks);

        nmo_cmd_ctx_json_end(c, doc, data, "debug.chunks");
    } else {
        nmo_cli_print_heading(c->out, "Chunk Debug Info", c->colorize);

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

        nmo_debug_chunks_data_t chunks_data = {
            .table = &table,
        };
        nmo_core_iter_result_t result = {0};
        rc = nmo_core_object_query_run(c, NULL, debug_chunks_object,
                                       &chunks_data, &result);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_table_free(&table);
            fprintf(stderr, "Error: Failed to query objects\n");
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        fprintf(c->out, "Chunks: %zu (from %zu objects)\n\n",
                chunks_data.chunk_count, result.matched);
        nmo_cli_table_print(&table, c->out, c->colorize);
        nmo_cli_table_free(&table);
    }

    return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_debug_chunks(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    return debug_chunks_run_in_ctx(&c, true);
}

static int nmo_cmd_debug_chunks_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!ctx) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return debug_chunks_run_in_ctx(ctx, false);
}

/* ============================================================================
 * debug objects
 * ============================================================================ */

static int debug_objects_run_in_ctx(nmo_cmd_ctx_t *c, bool close_ctx)
{
    int rc = NMO_CLI_EXIT_SUCCESS;
    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_val *objs = yyjson_mut_arr(doc);
        nmo_debug_objects_data_t objects_data = {
            .doc = doc,
            .objects = objs,
        };
        nmo_core_iter_result_t result = {0};
        rc = nmo_core_object_query_run(c, NULL, debug_objects_object,
                                       &objects_data, &result);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            yyjson_mut_doc_free(doc);
            fprintf(stderr, "Error: Failed to query objects\n");
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)result.matched);
        yyjson_mut_obj_add_val(doc, data, "objects", objs);

        nmo_cmd_ctx_json_end(c, doc, data, "debug.objects");
    } else {
        nmo_cli_print_heading(c->out, "Object Debug Info", c->colorize);

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

        nmo_debug_objects_data_t objects_data = {
            .table = &table,
        };
        nmo_core_iter_result_t result = {0};
        rc = nmo_core_object_query_run(c, NULL, debug_objects_object,
                                       &objects_data, &result);
        if (rc != NMO_CLI_EXIT_SUCCESS) {
            nmo_cli_table_free(&table);
            fprintf(stderr, "Error: Failed to query objects\n");
            return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                             : NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        fprintf(c->out, "Objects: %zu\n\n", result.matched);
        nmo_cli_table_print(&table, c->out, c->colorize);
        nmo_cli_table_free(&table);
    }

    return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_debug_objects(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    return debug_objects_run_in_ctx(&c, true);
}

static int nmo_cmd_debug_objects_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!ctx) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    return debug_objects_run_in_ctx(ctx, false);
}

/* ============================================================================
 * debug export
 * ============================================================================ */

static int debug_export_parse(int argc, char **argv,
                              bool *include_data,
                              size_t *max_bytes)
{
    static const nmo_opt_def_t opts[] = {
        {"--data",      "--include-data", NMO_OPT_FLAG, "Include chunk data"},
        {"--max-bytes", NULL,             NMO_OPT_UINT, "Max bytes for data dump (default: 4096)"},
    };
    nmo_opt_val_t vals[2];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 2, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    *include_data = vals[0].val.flag;
    *max_bytes = vals[1].present ? (size_t)vals[1].val.u : 4096;
    return NMO_CLI_EXIT_SUCCESS;
}

static int debug_export_run_in_ctx(nmo_cmd_ctx_t *c,
                                   bool include_data,
                                   size_t max_bytes,
                                   bool close_ctx)
{
    yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
    yyjson_mut_val *data = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, data, "file", c->file_path);
    yyjson_mut_obj_add_bool(doc, data, "include_data", include_data);
    yyjson_mut_obj_add_uint(doc, data, "max_bytes", (uint64_t)max_bytes);

    yyjson_mut_val *objs = yyjson_mut_arr(doc);
    nmo_debug_export_data_t export_data = {
        .doc = doc,
        .objects = objs,
        .include_data = include_data,
        .max_bytes = max_bytes,
    };
    nmo_core_iter_result_t result = {0};
    int rc = nmo_core_object_query_run(c, NULL, debug_export_object,
                                       &export_data, &result);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        yyjson_mut_doc_free(doc);
        fprintf(stderr, "Error: Failed to query objects\n");
        return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_INTERNAL_ERROR)
                         : NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    yyjson_mut_obj_add_uint(doc, data, "object_count", (uint64_t)result.matched);
    yyjson_mut_obj_add_val(doc, data, "objects", objs);
    nmo_cmd_ctx_json_end(c, doc, data, "debug.export");

    if (!c->is_json && c->global && c->global->output_path) {
        fprintf(stdout, "Exported %zu objects to %s\n", result.matched, c->global->output_path);
    }

    return close_ctx ? nmo_cmd_ctx_done(c, NMO_CLI_EXIT_SUCCESS)
                     : NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_debug_export(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    bool include_data = false;
    size_t max_bytes = 4096;
    int rc = debug_export_parse(argc, argv, &include_data, &max_bytes);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }

    nmo_cmd_ctx_t c;
    rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    return debug_export_run_in_ctx(&c, include_data, max_bytes, true);
}

static int nmo_cmd_debug_export_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    bool include_data = false;
    size_t max_bytes = 4096;
    int rc = debug_export_parse(argc, argv, &include_data, &max_bytes);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return rc;
    }
    return debug_export_run_in_ctx(ctx, include_data, max_bytes, false);
}

int nmo_cmd_debug_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: debug load-phases|chunks|objects|export ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "load-phases") == 0 || strcmp(argv[0], "lp") == 0) {
        return nmo_cmd_debug_load_phases_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "chunks") == 0 || strcmp(argv[0], "ch") == 0) {
        return nmo_cmd_debug_chunks_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "objects") == 0 || strcmp(argv[0], "obj") == 0) {
        return nmo_cmd_debug_objects_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "export") == 0 || strcmp(argv[0], "x") == 0) {
        return nmo_cmd_debug_export_in_session(ctx, argc, argv);
    }

    fprintf(stderr, "Unsupported debug read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}
