/**
 * @file nmo_cmd_file.c
 * @brief CLI file command group implementation
 */

#include "nmo_cmd_file.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_sort.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "app/nmo_stats.h"
#include "format/nmo_header.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int nmo_cmd_file_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: file info|header|stats|classes|plugins|space ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (strcmp(argv[0], "info") == 0 || strcmp(argv[0], "i") == 0) {
        return nmo_cmd_file_info_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "header") == 0 || strcmp(argv[0], "hdr") == 0) {
        return nmo_cmd_file_header_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "stats") == 0 || strcmp(argv[0], "st") == 0) {
        return nmo_cmd_file_stats_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "classes") == 0 || strcmp(argv[0], "cls") == 0) {
        return nmo_cmd_file_classes_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "plugins") == 0 || strcmp(argv[0], "pl") == 0) {
        return nmo_cmd_file_plugins_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "space") == 0 || strcmp(argv[0], "sp") == 0) {
        return nmo_cmd_file_space_in_session(ctx, argc, argv);
    }

    fprintf(stderr, "Unsupported file read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

/* ============================================================================
 * file info (single-file core + batch support)
 * ============================================================================ */

static int file_info_single(const char *file_path,
                             const nmo_cli_global_opts_t *global,
                             void *user_data,
                             yyjson_mut_doc *doc,
                             yyjson_mut_val *data)
{
    const nmo_tool_text_output_ctx_t *text_ctx =
        (const nmo_tool_text_output_ctx_t *)user_data;

    nmo_context_t *ctx = NULL;
    nmo_session_t *session = NULL;
    char errbuf[256];

    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = NMO_LOAD_PROFILE_METADATA;
    if (!nmo_tool_open_session_opts(file_path, &opts, &ctx, &session, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_file_info_t info = nmo_session_get_file_info(session);

    if (doc && data) {
        yyjson_mut_obj_add_uint(doc, data, "object_count", info.object_count);
        yyjson_mut_obj_add_uint(doc, data, "manager_count", info.manager_count);
        yyjson_mut_obj_add_uint(doc, data, "ck_version", info.ck_version);
    } else {
        FILE *out = (text_ctx && text_ctx->out) ? text_ctx->out : stdout;
        bool colorize = (text_ctx != NULL) ? text_ctx->colorize : nmo_cli_should_colorize(global, out);
        char buf[64];
        snprintf(buf, sizeof(buf), "%u", info.object_count);
        nmo_cli_print_kv(out, "Objects", buf, 14, colorize);
        snprintf(buf, sizeof(buf), "%u", info.manager_count);
        nmo_cli_print_kv(out, "Managers", buf, 14, colorize);
        snprintf(buf, sizeof(buf), "0x%08X", info.ck_version);
        nmo_cli_print_kv(out, "CK Version", buf, 14, colorize);
    }

    nmo_tool_close_session(ctx, session);
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_file_info_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
    (void)argc;
    (void)argv;

    nmo_file_info_t info = nmo_session_get_file_info(c->session);
    if (c->is_json) {
        yyjson_mut_doc *doc = NULL;
        yyjson_mut_val *data = NULL;
        if (!nmo_cli_json_create_data_doc(&doc, &data)) {
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_obj_add_uint(doc, data, "object_count", info.object_count);
        yyjson_mut_obj_add_uint(doc, data, "manager_count", info.manager_count);
        yyjson_mut_obj_add_uint(doc, data, "ck_version", info.ck_version);
        yyjson_mut_obj_add_str(doc, data, "file", c->file_path);
        nmo_cli_json_write_enveloped_and_free(
            doc, data, "file.info", c->file_path, c->out,
            c->global && c->global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        return NMO_CLI_EXIT_SUCCESS;
    }

    char buf[64];
    nmo_cli_print_heading(c->out, "File Info", c->colorize);
    nmo_cli_print_kv(c->out, "File", c->file_path, 14, c->colorize);
    snprintf(buf, sizeof(buf), "%u", info.object_count);
    nmo_cli_print_kv(c->out, "Objects", buf, 14, c->colorize);
    snprintf(buf, sizeof(buf), "%u", info.manager_count);
    nmo_cli_print_kv(c->out, "Managers", buf, 14, c->colorize);
    snprintf(buf, sizeof(buf), "0x%08X", info.ck_version);
    nmo_cli_print_kv(c->out, "CK Version", buf, 14, c->colorize);
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_file_info(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    /* Batch mode */
    if (global->batch_mode) {
        /* Count positional args first */
        size_t initial_capacity = 64;
        const char **paths = (const char **)malloc(initial_capacity * sizeof(const char *));
        if (!paths) {
            fprintf(stderr, "Error: Out of memory\n");
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }

        size_t count = 0;
        for (int i = 1; i < argc; ++i) {
            if (argv[i][0] != '-') {
                if (count >= initial_capacity) {
                    size_t new_capacity = initial_capacity * 2;
                    const char **new_paths = (const char **)realloc(paths, new_capacity * sizeof(const char *));
                    if (!new_paths) {
                        free(paths);
                        fprintf(stderr, "Error: Out of memory\n");
                        return NMO_CLI_EXIT_INTERNAL_ERROR;
                    }
                    paths = new_paths;
                    initial_capacity = new_capacity;
                }
                paths[count++] = argv[i];
            }
        }

        if (count == 0) {
            free(paths);
            fprintf(stderr, "Error: No files specified\n");
            fprintf(stderr, "Usage: nmo --batch file info <file1> <file2> ...\n");
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        int result = nmo_tool_batch_run(paths, count, global, "file.info",
                                        file_info_single, NULL);
        free(paths);
        return result;
    }

    const char *file_path = nmo_tool_find_file_arg(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo file info <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = NMO_LOAD_PROFILE_METADATA;
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_load_options(&c, argc, argv, global, &opts);
    if (rc) return rc;
    rc = nmo_cmd_file_info_in_session(&c, argc, argv);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * file header
 * ============================================================================ */

int nmo_cmd_file_header_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Get header - cast from opaque nmo_header_t to public nmo_file_header_t */
    const nmo_file_header_t *header = (const nmo_file_header_t *)nmo_session_get_header(c->session);
    if (!header) {
        fprintf(stderr, "Error: Failed to get file header\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        char sig_buf[9];
        memcpy(sig_buf, header->signature, 8);
        sig_buf[8] = '\0';
        yyjson_mut_obj_add_strcpy(doc, data, "signature", sig_buf);
        yyjson_mut_obj_add_uint(doc, data, "file_version", header->file_version);
        yyjson_mut_obj_add_uint(doc, data, "file_version2", header->file_version2);
        yyjson_mut_obj_add_uint(doc, data, "ck_version", header->ck_version);
        yyjson_mut_obj_add_uint(doc, data, "crc", header->crc);
        yyjson_mut_obj_add_uint(doc, data, "file_write_mode", header->file_write_mode);
        yyjson_mut_obj_add_uint(doc, data, "hdr1_pack_size", header->hdr1_pack_size);
        if (header->file_version >= 5) {
            yyjson_mut_obj_add_uint(doc, data, "data_pack_size", header->data_pack_size);
            yyjson_mut_obj_add_uint(doc, data, "data_unpack_size", header->data_unpack_size);
            yyjson_mut_obj_add_uint(doc, data, "object_count", header->object_count);
            yyjson_mut_obj_add_uint(doc, data, "manager_count", header->manager_count);
            yyjson_mut_obj_add_uint(doc, data, "max_id_saved", header->max_id_saved);
            yyjson_mut_obj_add_uint(doc, data, "product_version", header->product_version);
            yyjson_mut_obj_add_uint(doc, data, "product_build", header->product_build);
            yyjson_mut_obj_add_uint(doc, data, "hdr1_unpack_size", header->hdr1_unpack_size);
        }

        nmo_cmd_ctx_json_end(c, doc, data, "file.header");
    } else {
        nmo_cli_print_heading(c->out, "File Header", c->colorize);

        char sig_buf[9];
        memcpy(sig_buf, header->signature, 8);
        sig_buf[8] = '\0';
        nmo_cli_print_kv(c->out, "Signature", sig_buf, 18, c->colorize);

        char buf[64];
        snprintf(buf, sizeof(buf), "%u (secondary %u)", header->file_version, header->file_version2);
        nmo_cli_print_kv(c->out, "File Version", buf, 18, c->colorize);

        snprintf(buf, sizeof(buf), "0x%08X", header->ck_version);
        nmo_cli_print_kv(c->out, "CK Version", buf, 18, c->colorize);

        snprintf(buf, sizeof(buf), "0x%08X", header->crc);
        nmo_cli_print_kv(c->out, "CRC", buf, 18, c->colorize);

        snprintf(buf, sizeof(buf), "0x%X", header->file_write_mode);
        nmo_cli_print_kv(c->out, "Write Mode", buf, 18, c->colorize);

        snprintf(buf, sizeof(buf), "%u bytes", header->hdr1_pack_size);
        nmo_cli_print_kv(c->out, "Header1 Packed", buf, 18, c->colorize);

        if (header->file_version >= 5) {
            snprintf(buf, sizeof(buf), "%u bytes", header->data_pack_size);
            nmo_cli_print_kv(c->out, "Data Packed", buf, 18, c->colorize);

            snprintf(buf, sizeof(buf), "%u bytes", header->data_unpack_size);
            nmo_cli_print_kv(c->out, "Data Unpacked", buf, 18, c->colorize);

            snprintf(buf, sizeof(buf), "%u", header->object_count);
            nmo_cli_print_kv(c->out, "Objects", buf, 18, c->colorize);

            snprintf(buf, sizeof(buf), "%u", header->manager_count);
            nmo_cli_print_kv(c->out, "Managers", buf, 18, c->colorize);

            snprintf(buf, sizeof(buf), "%u", header->max_id_saved);
            nmo_cli_print_kv(c->out, "Max ID Saved", buf, 18, c->colorize);

            snprintf(buf, sizeof(buf), "%u / %u", header->product_version, header->product_build);
            nmo_cli_print_kv(c->out, "Product Ver/Build", buf, 18, c->colorize);
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_file_header(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = NMO_LOAD_PROFILE_HEADER_ONLY;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_load_options(&c, argc, argv, global, &opts);
    if (rc) return rc;
    rc = nmo_cmd_file_header_in_session(&c, argc, argv);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * file stats
 * ============================================================================ */

int nmo_cmd_file_stats_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
    (void)argc;
    (void)argv;
    /* Collect stats */
    nmo_file_stats_t stats;
    if (nmo_stats_collect(c->session, &stats) != 0) {
        fprintf(stderr, "Error: Failed to collect statistics\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        /* Objects stats */
        yyjson_mut_val *obj_stats = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, obj_stats, "total", stats.objects.total_count);
        yyjson_mut_obj_add_uint(doc, obj_stats, "unique_classes", (uint64_t)stats.objects.unique_classes);
        yyjson_mut_obj_add_val(doc, data, "objects", obj_stats);

        /* Chunks stats */
        yyjson_mut_val *chunk_stats = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, chunk_stats, "total", stats.chunks.total_chunks);
        yyjson_mut_obj_add_uint(doc, chunk_stats, "compressed", stats.chunks.compressed_chunks);
        yyjson_mut_obj_add_uint(doc, chunk_stats, "max_size", stats.chunks.max_chunk_size);
        yyjson_mut_obj_add_val(doc, data, "chunks", chunk_stats);

        /* Memory stats */
        yyjson_mut_val *mem_stats = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, mem_stats, "total_size", stats.memory.total_size);
        yyjson_mut_obj_add_uint(doc, mem_stats, "header_size", stats.memory.header_size);
        yyjson_mut_obj_add_uint(doc, mem_stats, "data_size", stats.memory.data_size);
        yyjson_mut_obj_add_uint(doc, mem_stats, "chunk_data_size", stats.memory.chunk_data_size);
        yyjson_mut_obj_add_uint(doc, mem_stats, "chunk_overhead", stats.memory.chunk_overhead);
        yyjson_mut_obj_add_uint(doc, mem_stats, "compression_ratio", stats.memory.compression_ratio);
        yyjson_mut_obj_add_val(doc, data, "memory", mem_stats);

        /* Reference stats */
        yyjson_mut_val *ref_stats = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, ref_stats, "total", stats.references.total_references);
        yyjson_mut_obj_add_uint(doc, ref_stats, "resolved", stats.references.resolved);
        yyjson_mut_obj_add_uint(doc, ref_stats, "unresolved", stats.references.unresolved);
        yyjson_mut_obj_add_val(doc, data, "references", ref_stats);

        /* Performance stats */
        yyjson_mut_val *perf_stats = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_real(doc, perf_stats, "load_time_ms", stats.performance.load_time_ms);
        yyjson_mut_obj_add_real(doc, perf_stats, "parse_time_ms", stats.performance.parse_time_ms);
        yyjson_mut_obj_add_real(doc, perf_stats, "remap_time_ms", stats.performance.remap_time_ms);
        yyjson_mut_obj_add_val(doc, data, "performance", perf_stats);

        nmo_cmd_ctx_json_end(c, doc, data, "file.stats");
    } else {
        nmo_cli_print_heading(c->out, "File Statistics", c->colorize);
        fprintf(c->out, "\n");

        nmo_cli_print_heading(c->out, "Objects", c->colorize);
        char buf[64];
        snprintf(buf, sizeof(buf), "%zu", stats.objects.total_count);
        nmo_cli_print_kv(c->out, "Total", buf, 20, c->colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.objects.unique_classes);
        nmo_cli_print_kv(c->out, "Unique Classes", buf, 20, c->colorize);
        fprintf(c->out, "\n");

        nmo_cli_print_heading(c->out, "Chunks", c->colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.chunks.total_chunks);
        nmo_cli_print_kv(c->out, "Total", buf, 20, c->colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.chunks.compressed_chunks);
        nmo_cli_print_kv(c->out, "Compressed", buf, 20, c->colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.chunks.max_chunk_size);
        nmo_cli_print_kv(c->out, "Max Size", buf, 20, c->colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.chunks.avg_chunk_size);
        nmo_cli_print_kv(c->out, "Avg Size", buf, 20, c->colorize);
        fprintf(c->out, "\n");

        nmo_cli_print_heading(c->out, "Memory", c->colorize);
        snprintf(buf, sizeof(buf), "%zu bytes", stats.memory.total_size);
        nmo_cli_print_kv(c->out, "Total Size", buf, 20, c->colorize);
        snprintf(buf, sizeof(buf), "%zu bytes", stats.memory.header_size);
        nmo_cli_print_kv(c->out, "Header Size", buf, 20, c->colorize);
        snprintf(buf, sizeof(buf), "%zu bytes", stats.memory.data_size);
        nmo_cli_print_kv(c->out, "Data Size", buf, 20, c->colorize);
        snprintf(buf, sizeof(buf), "%zu bytes", stats.memory.chunk_data_size);
        nmo_cli_print_kv(c->out, "Chunk Data", buf, 20, c->colorize);
        snprintf(buf, sizeof(buf), "%zu%%", stats.memory.compression_ratio);
        nmo_cli_print_kv(c->out, "Compression", buf, 20, c->colorize);
        fprintf(c->out, "\n");

        nmo_cli_print_heading(c->out, "References", c->colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.references.total_references);
        nmo_cli_print_kv(c->out, "Total", buf, 20, c->colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.references.resolved);
        nmo_cli_print_kv(c->out, "Resolved", buf, 20, c->colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.references.unresolved);
        nmo_cli_print_kv(c->out, "Unresolved", buf, 20, c->colorize);

        if (c->global && c->global->verbosity > 0) {
            fprintf(c->out, "\n");
            nmo_cli_print_heading(c->out, "Performance", c->colorize);
            snprintf(buf, sizeof(buf), "%.2f ms", stats.performance.load_time_ms);
            nmo_cli_print_kv(c->out, "Load Time", buf, 20, c->colorize);
            snprintf(buf, sizeof(buf), "%.2f ms", stats.performance.parse_time_ms);
            nmo_cli_print_kv(c->out, "Parse Time", buf, 20, c->colorize);
            snprintf(buf, sizeof(buf), "%.2f ms", stats.performance.remap_time_ms);
            nmo_cli_print_kv(c->out, "Remap Time", buf, 20, c->colorize);
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_file_stats(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    rc = nmo_cmd_file_stats_in_session(&c, argc, argv);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * file classes
 * ============================================================================ */

typedef struct nmo_class_count_entry {
    uint32_t class_id;
    size_t count;
    size_t total_size;
} nmo_class_count_entry_t;

/* File-static registry pointer for name comparator (qsort can't take context) */
static const nmo_type_registry_t *s_class_sort_registry;

static int compare_class_by_id(const void *a, const void *b) {
    const nmo_class_count_entry_t *ea = (const nmo_class_count_entry_t *)a;
    const nmo_class_count_entry_t *eb = (const nmo_class_count_entry_t *)b;
    if (ea->class_id < eb->class_id) return -1;
    if (ea->class_id > eb->class_id) return 1;
    return 0;
}

static int compare_class_by_size(const void *a, const void *b) {
    const nmo_class_count_entry_t *ea = (const nmo_class_count_entry_t *)a;
    const nmo_class_count_entry_t *eb = (const nmo_class_count_entry_t *)b;
    /* Descending */
    if (ea->total_size > eb->total_size) return -1;
    if (ea->total_size < eb->total_size) return 1;
    return 0;
}

static int compare_class_by_count(const void *a, const void *b) {
    const nmo_class_count_entry_t *ea = (const nmo_class_count_entry_t *)a;
    const nmo_class_count_entry_t *eb = (const nmo_class_count_entry_t *)b;
    /* Descending */
    if (ea->count > eb->count) return -1;
    if (ea->count < eb->count) return 1;
    return 0;
}

static int compare_class_by_name(const void *a, const void *b) {
    const nmo_class_count_entry_t *ea = (const nmo_class_count_entry_t *)a;
    const nmo_class_count_entry_t *eb = (const nmo_class_count_entry_t *)b;
    const char *na = NULL;
    const char *nb = NULL;
    if (s_class_sort_registry) {
        const nmo_type_descriptor_t *da =
            nmo_type_registry_find_by_class_id(s_class_sort_registry, ea->class_id);
        const nmo_type_descriptor_t *db =
            nmo_type_registry_find_by_class_id(s_class_sort_registry, eb->class_id);
        if (da) na = da->name;
        if (db) nb = db->name;
    }
    if (!na) na = "";
    if (!nb) nb = "";
    return strcmp(na, nb);
}

typedef int (*class_compare_fn)(const void *, const void *);

static class_compare_fn class_sort_comparator(nmo_cli_sort_key_t key) {
    switch (key) {
        case NMO_CLI_SORT_ID:    return compare_class_by_id;
        case NMO_CLI_SORT_SIZE:  return compare_class_by_size;
        case NMO_CLI_SORT_COUNT: return compare_class_by_count;
        case NMO_CLI_SORT_NAME:  return compare_class_by_name;
        default:                 return compare_class_by_id;
    }
}

typedef struct file_class_collect {
    nmo_class_count_entry_t *entries;
    size_t count;
    size_t capacity;
    size_t grand_total_size;
    bool oom;
} file_class_collect_t;

static int file_classes_object(size_t index,
                               nmo_object_t *obj,
                               const nmo_cmd_ctx_t *c,
                               void *user)
{
    (void)index;
    (void)c;

    file_class_collect_t *collect = (file_class_collect_t *)user;
    if (!collect || !obj) {
        return 0;
    }

    uint32_t class_id = nmo_object_get_class_id(obj);
    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    size_t obj_size = chunk ? nmo_chunk_get_data_size(chunk) : 0;
    collect->grand_total_size += obj_size;

    size_t found = (size_t)-1;
    for (size_t j = 0; j < collect->count; j++) {
        if (collect->entries[j].class_id == class_id) {
            found = j;
            break;
        }
    }

    if (found == (size_t)-1) {
        if (collect->count == collect->capacity) {
            size_t new_capacity = collect->capacity ? collect->capacity * 2 : 16;
            nmo_class_count_entry_t *new_entries =
                (nmo_class_count_entry_t *)realloc(
                    collect->entries,
                    new_capacity * sizeof(*new_entries));
            if (!new_entries) {
                collect->oom = true;
                return 1;
            }
            collect->entries = new_entries;
            collect->capacity = new_capacity;
        }
        found = collect->count++;
        collect->entries[found].class_id = class_id;
        collect->entries[found].count = 0;
        collect->entries[found].total_size = 0;
    }

    collect->entries[found].count++;
    collect->entries[found].total_size += obj_size;
    return 0;
}

int nmo_cmd_file_classes_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
    static const nmo_opt_def_t opts[] = {
        {"--sort", "-s", NMO_OPT_STRING, "Sort by: id (default), size, count, name"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *sort_key_str = vals[0].present ? vals[0].val.str : NULL;

    /* Validate sort key early */
    nmo_cli_sort_key_t sort_key = nmo_cli_parse_sort_key(sort_key_str);
    if (sort_key_str && sort_key == NMO_CLI_SORT_NONE) {
        fprintf(stderr, "Error: Invalid sort key '%s' (use: id, size, count, name)\n", sort_key_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    /* Default to sort by id when no key specified */
    if (!sort_key_str) sort_key = NMO_CLI_SORT_ID;

    file_class_collect_t collect = {0};
    int rc = nmo_core_object_query_run(c, NULL, file_classes_object,
                                       &collect, NULL);
    if (rc != NMO_CLI_EXIT_SUCCESS || collect.oom) {
        free(collect.entries);
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    nmo_class_count_entry_t *entries = collect.entries;
    size_t entry_count = collect.count;
    size_t grand_total_size = collect.grand_total_size;

    /* Sort entries */
    if (entry_count > 1) {
        s_class_sort_registry = c->registry;
        class_compare_fn cmp = class_sort_comparator(sort_key);
        qsort(entries, entry_count, sizeof(nmo_class_count_entry_t), cmp);
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *classes = yyjson_mut_arr(doc);

        for (size_t i = 0; i < entry_count; i++) {
            const nmo_class_count_entry_t *entry = &entries[i];
            const nmo_type_descriptor_t *type_desc =
                (c->registry != NULL)
                    ? nmo_type_registry_find_by_class_id(c->registry, entry->class_id)
                    : NULL;
            yyjson_mut_val *item = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_uint(doc, item, "class_id", entry->class_id);
            yyjson_mut_obj_add_uint(doc, item, "count", (uint64_t)entry->count);
            yyjson_mut_obj_add_uint(doc, item, "total_size", (uint64_t)entry->total_size);
            size_t avg = (entry->count > 0) ? entry->total_size / entry->count : 0;
            yyjson_mut_obj_add_uint(doc, item, "avg_size", (uint64_t)avg);
            double pct = (grand_total_size > 0)
                ? (double)entry->total_size * 100.0 / (double)grand_total_size
                : 0.0;
            yyjson_mut_obj_add_real(doc, item, "percentage", pct);
            if (type_desc != NULL && type_desc->name != NULL) {
                yyjson_mut_obj_add_str(doc, item, "name", type_desc->name);
            }
            yyjson_mut_arr_append(classes, item);
        }

        yyjson_mut_obj_add_val(doc, data, "classes", classes);
        yyjson_mut_obj_add_uint(doc, data, "grand_total_size", (uint64_t)grand_total_size);
        nmo_cmd_ctx_json_end(c, doc, data, "file.classes");
    } else {
        nmo_cli_print_heading(c->out, "File Class IDs", c->colorize);
        nmo_cli_print_kv(c->out, "File", c->file_path, 8, c->colorize);
        fprintf(c->out, "\n");

        fprintf(c->out, "%-12s %-8s %-12s %-10s %-6s %s\n",
                "CLASS ID", "COUNT", "TOTAL SIZE", "AVG SIZE", "%", "NAME");
        fprintf(c->out, "--------------------------------------------------------------\n");
        for (size_t i = 0; i < entry_count; i++) {
            const nmo_class_count_entry_t *entry = &entries[i];
            const nmo_type_descriptor_t *type_desc =
                (c->registry != NULL)
                    ? nmo_type_registry_find_by_class_id(c->registry, entry->class_id)
                    : NULL;
            const char *name = (type_desc != NULL && type_desc->name != NULL) ? type_desc->name : "";
            size_t avg = (entry->count > 0) ? entry->total_size / entry->count : 0;
            double pct = (grand_total_size > 0)
                ? (double)entry->total_size * 100.0 / (double)grand_total_size
                : 0.0;
            fprintf(c->out, "0x%08X %-8zu %-12zu %-10zu %5.1f%% %s\n",
                    entry->class_id, entry->count, entry->total_size, avg, pct, name);
        }
        fprintf(c->out, "\nTotal data size: %zu bytes\n", grand_total_size);
    }

    free(entries);
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_file_classes(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    rc = nmo_cmd_file_classes_in_session(&c, argc, argv);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * file plugins
 * ============================================================================ */

static const char *file_plugin_category_name(nmo_plugin_category_t category) {
    switch (category) {
        case NMO_PLUGIN_BITMAP_READER:     return "bitmap_reader";
        case NMO_PLUGIN_SOUND_READER:      return "sound_reader";
        case NMO_PLUGIN_MODEL_READER:      return "model_reader";
        case NMO_PLUGIN_MANAGER_DLL:       return "manager";
        case NMO_PLUGIN_BEHAVIOR_DLL:      return "behavior";
        case NMO_PLUGIN_RENDER_DLL:        return "render";
        case NMO_PLUGIN_MOVIE_READER:      return "movie_reader";
        case NMO_PLUGIN_EXTENSION_DLL:     return "extension";
        case NMO_PLUGIN_CUSTOM_DLL:        return "custom";
        default:                           return "unknown";
    }
}

int nmo_cmd_file_plugins_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Get plugin diagnostics */
    const nmo_session_plugin_diagnostics_t *diag = nmo_session_get_plugin_diagnostics(c->session);

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_bool(doc, data, "extension_registry_available",
                                diag ? diag->extension_registry_available : false);
        yyjson_mut_obj_add_uint(doc, data, "missing_count", diag ? diag->missing_count : 0);
        yyjson_mut_obj_add_uint(doc, data, "outdated_count", diag ? diag->outdated_count : 0);
        yyjson_mut_obj_add_uint(doc, data, "entry_count", diag ? diag->entry_count : 0);

        /* Plugin entries */
        yyjson_mut_val *entries = yyjson_mut_arr(doc);
        if (diag && diag->entries) {
            for (size_t i = 0; i < diag->entry_count; ++i) {
                const nmo_session_plugin_dependency_status_t *e = &diag->entries[i];
                yyjson_mut_val *entry = yyjson_mut_obj(doc);

                char guid_buf[64];
                nmo_guid_format(e->guid, guid_buf, sizeof(guid_buf));
                yyjson_mut_obj_add_strcpy(doc, entry, "guid", guid_buf);
                yyjson_mut_obj_add_uint(doc, entry, "category", (uint32_t)e->category);
                yyjson_mut_obj_add_str(doc, entry, "category_name",
                                       file_plugin_category_name(e->category));
                yyjson_mut_obj_add_uint(doc, entry, "required_version", e->required_version);
                yyjson_mut_obj_add_uint(doc, entry, "resolved_version", e->resolved_version);
                if (e->resolved_name) {
                    yyjson_mut_obj_add_str(doc, entry, "name", e->resolved_name);
                }
                yyjson_mut_obj_add_uint(doc, entry, "status_flags", e->status_flags);

                yyjson_mut_arr_add_val(entries, entry);
            }
        }
        yyjson_mut_obj_add_val(doc, data, "entries", entries);

        nmo_cmd_ctx_json_end(c, doc, data, "file.plugins");
    } else {
        nmo_cli_print_heading(c->out, "Plugin Dependencies", c->colorize);

        if (!diag) {
            fprintf(c->out, "Plugin diagnostics unavailable\n");
        } else {
            char buf[64];
            nmo_cli_print_kv(c->out, "Registry Available",
                            diag->extension_registry_available ? "yes" : "no", 18, c->colorize);
            snprintf(buf, sizeof(buf), "%zu", diag->missing_count);
            nmo_cli_print_kv(c->out, "Missing", buf, 18, c->colorize);
            snprintf(buf, sizeof(buf), "%zu", diag->outdated_count);
            nmo_cli_print_kv(c->out, "Outdated", buf, 18, c->colorize);
            snprintf(buf, sizeof(buf), "%zu", diag->entry_count);
            nmo_cli_print_kv(c->out, "Total Entries", buf, 18, c->colorize);

            if (diag->entries && diag->entry_count > 0) {
                fprintf(c->out, "\nEntries:\n");
                for (size_t i = 0; i < diag->entry_count; ++i) {
                    const nmo_session_plugin_dependency_status_t *e = &diag->entries[i];
                    char guid_buf[64];
                    nmo_guid_format(e->guid, guid_buf, sizeof(guid_buf));
                    fprintf(c->out, "  %s [%s req=%u resolved=%u]",
                            guid_buf,
                            file_plugin_category_name(e->category),
                            e->required_version,
                            e->resolved_version);
                    if (e->resolved_name) {
                        fprintf(c->out, " (%s)", e->resolved_name);
                    }
                    if (e->status_flags) {
                        fprintf(c->out, " [flags=0x%X]", e->status_flags);
                    }
                    fprintf(c->out, "\n");
                }
            }
        }
    }

    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_file_plugins(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = NMO_LOAD_PROFILE_METADATA;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_with_load_options(&c, argc, argv, global, &opts);
    if (rc) return rc;
    rc = nmo_cmd_file_plugins_in_session(&c, argc, argv);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * file space - byte-level space analysis
 * ============================================================================ */

typedef struct {
    nmo_class_id_t class_id;
    const char *class_name;
    uint32_t count;
    uint64_t data_size;
    uint64_t pack_size;
} space_class_entry_t;

static int space_class_cmp_size(const void *a, const void *b) {
    const space_class_entry_t *ea = (const space_class_entry_t *)a;
    const space_class_entry_t *eb = (const space_class_entry_t *)b;
    if (ea->data_size > eb->data_size) return -1;
    if (ea->data_size < eb->data_size) return 1;
    return 0;
}

typedef struct {
    nmo_object_t *obj;
    uint64_t data_sz;
    uint64_t pack_sz;
} file_space_obj_entry_t;

typedef struct file_space_collect {
    space_class_entry_t classes[256];
    size_t class_count;
    file_space_obj_entry_t *objects;
    size_t object_count;
    size_t object_capacity;
    uint64_t total_data;
    uint64_t total_pack;
    uint64_t compressed_count;
    uint64_t pack_scale_num;
    uint64_t pack_scale_den;
    bool global_data_compressed;
    bool oom;
} file_space_collect_t;

static uint64_t file_space_estimate_packed_size(uint64_t data_size,
                                                const file_space_collect_t *collect)
{
    if (data_size == 0) {
        return 0;
    }
    if (collect == NULL ||
        collect->pack_scale_num == 0 ||
        collect->pack_scale_den == 0 ||
        collect->pack_scale_num >= collect->pack_scale_den) {
        return data_size;
    }

    long double scaled = (long double)data_size *
                         (long double)collect->pack_scale_num /
                         (long double)collect->pack_scale_den;
    uint64_t packed = (uint64_t)(scaled + 0.5L);
    return packed > 0 ? packed : 1;
}

static int file_space_object(size_t index,
                             nmo_object_t *obj,
                             const nmo_cmd_ctx_t *c,
                             void *user)
{
    (void)index;

    file_space_collect_t *collect = (file_space_collect_t *)user;
    if (!collect || !obj) {
        return 0;
    }

    nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
    uint64_t data_sz = 0;
    uint64_t pack_sz = 0;
    if (chunk) {
        data_sz = chunk->uncompressed_size > 0
            ? (uint64_t)chunk->uncompressed_size
            : (uint64_t)nmo_chunk_get_data_size(chunk);
        pack_sz = chunk->compressed_size > 0
            ? (uint64_t)chunk->compressed_size
            : file_space_estimate_packed_size(data_sz, collect);
    }
    if (chunk && (chunk->is_compressed || collect->global_data_compressed)) {
        collect->compressed_count++;
    }

    collect->total_data += data_sz;
    collect->total_pack += pack_sz;

    if (collect->object_count == collect->object_capacity) {
        size_t new_capacity = collect->object_capacity ? collect->object_capacity * 2 : 64;
        file_space_obj_entry_t *new_objects =
            (file_space_obj_entry_t *)realloc(
                collect->objects, new_capacity * sizeof(*new_objects));
        if (!new_objects) {
            collect->oom = true;
            return 1;
        }
        collect->objects = new_objects;
        collect->object_capacity = new_capacity;
    }
    collect->objects[collect->object_count++] =
        (file_space_obj_entry_t){ .obj = obj, .data_sz = data_sz, .pack_sz = pack_sz };

    nmo_class_id_t cid = nmo_object_get_class_id(obj);
    size_t ci;
    for (ci = 0; ci < collect->class_count; ci++) {
        if (collect->classes[ci].class_id == cid) break;
    }
    if (ci == collect->class_count && collect->class_count < 256) {
        collect->classes[collect->class_count].class_id = cid;
        collect->classes[collect->class_count].class_name = nmo_core_class_name(c, cid);
        collect->classes[collect->class_count].count = 0;
        collect->classes[collect->class_count].data_size = 0;
        collect->classes[collect->class_count].pack_size = 0;
        collect->class_count++;
    }
    if (ci < 256) {
        collect->classes[ci].count++;
        collect->classes[ci].data_size += data_sz;
        collect->classes[ci].pack_size += pack_sz;
    }
    return 0;
}

int nmo_cmd_file_space_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
    static const nmo_opt_def_t opts[] = {
        {"--top", "-t", NMO_OPT_UINT, "Show top N objects by size (default: 15)"},
    };
    enum { OPT_TOP, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    uint32_t top_n = vals[OPT_TOP].present ? vals[OPT_TOP].val.u : 15;

    nmo_file_info_t info = nmo_session_get_file_info(c->session);

    file_space_collect_t collect = {0};
    const nmo_file_header_t *header = (const nmo_file_header_t *)nmo_session_get_header(c->session);
    if (header != NULL && header->data_pack_size > 0 && header->data_unpack_size > 0) {
        collect.pack_scale_num = header->data_pack_size;
        collect.pack_scale_den = header->data_unpack_size;
        collect.global_data_compressed = header->data_pack_size < header->data_unpack_size;
    }
    int rc = nmo_core_object_query_run(c, NULL, file_space_object,
                                       &collect, NULL);
    if (rc != NMO_CLI_EXIT_SUCCESS || collect.oom) {
        free(collect.objects);
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    space_class_entry_t *classes = collect.classes;
    size_t class_count = collect.class_count;
    file_space_obj_entry_t *obj_entries = collect.objects;
    size_t obj_count = collect.object_count;
    uint64_t total_data = collect.total_data;
    uint64_t total_pack = collect.total_pack;
    uint64_t compressed_count = collect.compressed_count;

    /* Sort classes by data_size descending */
    qsort(classes, class_count, sizeof(space_class_entry_t), space_class_cmp_size);

    /* Partial sort for top-N objects (done once before output branches) */
    if (obj_entries && obj_count > 1) {
        size_t sort_limit = obj_count < top_n ? obj_count : top_n;
        for (size_t i = 0; i < sort_limit; i++) {
            for (size_t j = i + 1; j < obj_count; j++) {
                if (obj_entries[j].data_sz > obj_entries[i].data_sz) {
                    file_space_obj_entry_t tmp = obj_entries[i];
                    obj_entries[i] = obj_entries[j];
                    obj_entries[j] = tmp;
                }
            }
        }
    }

    if (c->is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(c);
        if (!doc) {
            free(obj_entries);
            return NMO_CLI_EXIT_INTERNAL_ERROR;
        }
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        nmo_cli_json_add_uint_safe(doc, data, "file_size", (uint64_t)info.file_size);
        nmo_cli_json_add_uint_safe(doc, data, "object_count", (uint64_t)obj_count);
        nmo_cli_json_add_uint_safe(doc, data, "total_data_size", total_data);
        nmo_cli_json_add_uint_safe(doc, data, "total_pack_size", total_pack);
        nmo_cli_json_add_uint_safe(doc, data, "compressed_objects", compressed_count);

        yyjson_mut_val *cls_arr = yyjson_mut_arr(doc);
        uint64_t cumul = 0;
        for (size_t i = 0; i < class_count; i++) {
            cumul += classes[i].data_size;
            yyjson_mut_val *e = yyjson_mut_obj(doc);
            if (classes[i].class_name)
                nmo_cli_json_add_str_safe(doc, e, "class_name", classes[i].class_name);
            nmo_cli_json_add_uint_safe(doc, e, "count", classes[i].count);
            nmo_cli_json_add_uint_safe(doc, e, "data_size", classes[i].data_size);
            nmo_cli_json_add_uint_safe(doc, e, "pack_size", classes[i].pack_size);
            if (total_data > 0) {
                double pct = (double)classes[i].data_size / (double)total_data * 100.0;
                double cum_pct = (double)cumul / (double)total_data * 100.0;
                yyjson_mut_obj_add_real(doc, e, "percent", pct);
                yyjson_mut_obj_add_real(doc, e, "cumulative_percent", cum_pct);
            }
            yyjson_mut_arr_add_val(cls_arr, e);
        }
        yyjson_mut_obj_add_val(doc, data, "classes", cls_arr);

        /* Top objects (already sorted above) */
        if (obj_entries && obj_count > 0) {
            size_t show_n = obj_count < top_n ? obj_count : top_n;
            yyjson_mut_val *top_arr = yyjson_mut_arr(doc);
            for (size_t i = 0; i < show_n; i++) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                nmo_cli_json_add_uint_safe(doc, e, "id",
                    (uint64_t)nmo_object_get_id(obj_entries[i].obj));
                const char *cn = nmo_core_class_name(c,
                    nmo_object_get_class_id(obj_entries[i].obj));
                if (cn) nmo_cli_json_add_str_safe(doc, e, "class_name", cn);
                const char *nm = nmo_object_get_name(obj_entries[i].obj);
                if (nm && nm[0]) nmo_cli_json_add_str_safe(doc, e, "name", nm);
                nmo_cli_json_add_uint_safe(doc, e, "data_size", obj_entries[i].data_sz);
                nmo_cli_json_add_uint_safe(doc, e, "pack_size", obj_entries[i].pack_sz);
                yyjson_mut_arr_add_val(top_arr, e);
            }
            yyjson_mut_obj_add_val(doc, data, "top_objects", top_arr);
        }

        nmo_cmd_ctx_json_end(c, doc, data, "file.space");
    } else {
        nmo_cli_print_heading(c->out, "Space Analysis", c->colorize);

        char buf[128];
        snprintf(buf, sizeof(buf), "%zu bytes", info.file_size);
        nmo_cli_print_kv(c->out, "File Size", buf, 20, c->colorize);
        snprintf(buf, sizeof(buf), "%zu", obj_count);
        nmo_cli_print_kv(c->out, "Objects", buf, 20, c->colorize);
        snprintf(buf, sizeof(buf), "%" PRIu64 " bytes", total_data);
        nmo_cli_print_kv(c->out, "Total Data", buf, 20, c->colorize);
        snprintf(buf, sizeof(buf), "%" PRIu64 " bytes", total_pack);
        nmo_cli_print_kv(c->out, "Total Packed", buf, 20, c->colorize);
        if (total_data > 0) {
            snprintf(buf, sizeof(buf), "%.1f%%",
                     (double)total_pack / (double)total_data * 100.0);
            nmo_cli_print_kv(c->out, "Compression", buf, 20, c->colorize);
        }
        snprintf(buf, sizeof(buf), "%" PRIu64 " / %zu",
                 compressed_count, obj_count);
        nmo_cli_print_kv(c->out, "Compressed", buf, 20, c->colorize);

        /* Per-class breakdown with cumulative % and ASCII bar */
        fprintf(c->out, "\n");
        nmo_cli_print_heading(c->out, "Space by Class", c->colorize);
        fprintf(c->out, "%-20s  %5s  %10s  %10s  %6s  %6s  %s\n",
                "CLASS", "COUNT", "DATA", "PACKED", "%", "CUM%", "BAR");
        fprintf(c->out, "%-20s  %5s  %10s  %10s  %6s  %6s  %s\n",
                "--------------------", "-----", "----------", "----------",
                "------", "------", "--------------------");

        uint64_t cumul = 0;
        for (size_t i = 0; i < class_count; i++) {
            cumul += classes[i].data_size;
            double pct = total_data > 0
                ? (double)classes[i].data_size / (double)total_data * 100.0 : 0.0;
            double cum_pct = total_data > 0
                ? (double)cumul / (double)total_data * 100.0 : 0.0;

            int bar_len = (int)(pct / 5.0 + 0.5);
            if (bar_len > 20) bar_len = 20;
            char bar[21];
            for (int b = 0; b < bar_len; b++) bar[b] = '#';
            bar[bar_len] = '\0';

            fprintf(c->out, "%-20s  %5u  %10" PRIu64 "  %10" PRIu64 "  %5.1f%%  %5.1f%%  %s\n",
                    classes[i].class_name ? classes[i].class_name : "?",
                    classes[i].count,
                    classes[i].data_size,
                    classes[i].pack_size,
                    pct, cum_pct, bar);
        }

        /* Top N objects (already sorted above) */
        if (obj_entries && obj_count > 0) {
            size_t show_n = obj_count < top_n ? obj_count : top_n;
            fprintf(c->out, "\n");
            snprintf(buf, sizeof(buf), "Top %zu Objects by Size", show_n);
            nmo_cli_print_heading(c->out, buf, c->colorize);
            fprintf(c->out, "%5s  %-20s  %10s  %10s  %6s  %-s\n",
                    "ID", "CLASS", "DATA", "PACKED", "RATIO", "NAME");
            fprintf(c->out, "%5s  %-20s  %10s  %10s  %6s  %-s\n",
                    "-----", "--------------------", "----------", "----------",
                    "------", "--------------------");

            for (size_t i = 0; i < show_n; i++) {
                nmo_object_t *obj = obj_entries[i].obj;
                const char *cn = nmo_core_class_name(c, nmo_object_get_class_id(obj));
                const char *nm = nmo_object_get_name(obj);
                double ratio = obj_entries[i].data_sz > 0
                    ? (double)obj_entries[i].pack_sz / (double)obj_entries[i].data_sz * 100.0
                    : 0.0;

                fprintf(c->out, "%5u  %-20s  %10" PRIu64 "  %10" PRIu64 "  %5.1f%%  %s\n",
                        nmo_object_get_id(obj),
                        cn ? cn : "?",
                        obj_entries[i].data_sz,
                        obj_entries[i].pack_sz,
                        ratio,
                        (nm && nm[0]) ? nm : "(unnamed)");
            }
        }
    }

    free(obj_entries);
    return NMO_CLI_EXIT_SUCCESS;
}

int nmo_cmd_file_space(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    rc = nmo_cmd_file_space_in_session(&c, argc, argv);
    return nmo_cmd_ctx_done(&c, rc);
}

