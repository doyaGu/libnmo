/**
 * @file nmo_cmd_file.c
 * @brief CLI file command group implementation
 */

#include "nmo_cmd_file.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_cli_sort.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_stats.h"
#include "format/nmo_header.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int nmo_cmd_file_info_in_session(nmo_cmd_ctx_t *c, int argc, char **argv);
static int nmo_cmd_file_header_in_session(nmo_cmd_ctx_t *c, int argc, char **argv);
static int nmo_cmd_file_stats_in_session(nmo_cmd_ctx_t *c, int argc, char **argv);
static int nmo_cmd_file_classes_in_session(nmo_cmd_ctx_t *c, int argc, char **argv);
static int nmo_cmd_file_plugins_in_session(nmo_cmd_ctx_t *c, int argc, char **argv);
static int nmo_cmd_file_space_in_session(nmo_cmd_ctx_t *c, int argc, char **argv);

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

/*
 * File summary. JSON: object_count, manager_count, ck_version, and "file"
 * when a path is given; text: File (when given), Objects, Managers,
 * CK Version.
 */
static bool file_info_build_record(const nmo_file_info_t *info,
                                   const char *file_path,
                                   nmo_cli_record_t *rec)
{
    bool ok = true;
    if (file_path) {
        ok = nmo_cli_record_text(rec, "File", file_path);
    }
    ok = ok && nmo_cli_record_uint(rec, "object_count", "Objects", info->object_count) &&
         nmo_cli_record_uint(rec, "manager_count", "Managers", info->manager_count) &&
         nmo_cli_record_uint(rec, "ck_version", "CK Version", info->ck_version) &&
         nmo_cli_record_set_text_fmt(rec, "0x%08X", info->ck_version);
    if (ok && file_path) {
        ok = nmo_cli_record_str(rec, "file", NULL, file_path);
    }
    return ok;
}

static int file_info_single(const char *file_path,
                             const nmo_cli_global_opts_t *global,
                             void *user_data,
                             yyjson_mut_doc *doc,
                             yyjson_mut_val *data)
{
    const nmo_tool_text_output_ctx_t *text_ctx =
        (const nmo_tool_text_output_ctx_t *)user_data;

    nmo_context_t *ctx = NULL;
    nmo_document_t *document = NULL;
    nmo_workspace_t *workspace = NULL;
    char *open_error = NULL;

    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = NMO_LOAD_PROFILE_METADATA;
    if (!nmo_tool_open_document_opts(file_path, &opts, &ctx, &document, &workspace,
                                     &open_error)) {
        fprintf(stderr, "Error: %s\n", open_error ? open_error : "Failed to open file");
        free(open_error);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    nmo_file_info_t info = nmo_document_get_file_info(document);

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec || !file_info_build_record(&info, NULL, rec)) {
        nmo_cli_record_free(rec);
        nmo_tool_close_document(ctx, document, workspace);
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    if (doc && data) {
        nmo_cli_record_to_json(rec, doc, data);
    } else {
        FILE *out = (text_ctx && text_ctx->out) ? text_ctx->out : stdout;
        bool colorize = (text_ctx != NULL) ? text_ctx->colorize : nmo_cli_should_colorize(global, out);
        nmo_cli_record_print_kv(rec, out, 14, colorize);
    }
    nmo_cli_record_free(rec);

    nmo_tool_close_document(ctx, document, workspace);
    return NMO_CLI_EXIT_SUCCESS;
}

static int nmo_cmd_file_info_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
    (void)argc;
    (void)argv;

    nmo_file_info_t info = nmo_document_get_file_info(c->document);
    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (!rec || !nmo_cli_record_title(rec, "File Info") ||
        !file_info_build_record(&info, c->file_path, rec)) {
        nmo_cli_record_free(rec);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return nmo_cmd_ctx_emit_record(c, rec, "file.info", 14, c->colorize);
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

/*
 * Raw header fields. The text side folds the secondary version into "File
 * Version" and product version/build into one line; hdr1_unpack_size is
 * JSON-only.
 */
static bool file_header_build_record(const nmo_file_header_t *header,
                                     nmo_cli_record_t *rec)
{
    /* The signature is eight bytes without a terminator. */
    bool ok = nmo_cli_record_str_fmt(rec, "signature", "Signature", "%.8s", header->signature) &&
              nmo_cli_record_uint(rec, "file_version", "File Version", header->file_version) &&
              nmo_cli_record_set_text_fmt(rec, "%u (secondary %u)",
                                          header->file_version, header->file_version2) &&
              nmo_cli_record_uint(rec, "file_version2", NULL, header->file_version2);
    /* JSON keeps these numeric; the text side shows them in hex. */
    ok = ok && nmo_cli_record_uint(rec, "ck_version", "CK Version", header->ck_version) &&
         nmo_cli_record_set_text_fmt(rec, "0x%08X", header->ck_version);
    ok = ok && nmo_cli_record_uint(rec, "crc", "CRC", header->crc) &&
         nmo_cli_record_set_text_fmt(rec, "0x%08X", header->crc);
    ok = ok && nmo_cli_record_uint(rec, "file_write_mode", "Write Mode", header->file_write_mode) &&
         nmo_cli_record_set_text_fmt(rec, "0x%X", header->file_write_mode);
    ok = ok && nmo_cli_record_uint(rec, "hdr1_pack_size", "Header1 Packed", header->hdr1_pack_size) &&
         nmo_cli_record_set_text_fmt(rec, "%u bytes", header->hdr1_pack_size);
    if (!ok || header->file_version < 5) {
        return ok;
    }

    ok = nmo_cli_record_uint(rec, "data_pack_size", "Data Packed", header->data_pack_size) &&
         nmo_cli_record_set_text_fmt(rec, "%u bytes", header->data_pack_size);
    ok = ok && nmo_cli_record_uint(rec, "data_unpack_size", "Data Unpacked", header->data_unpack_size) &&
         nmo_cli_record_set_text_fmt(rec, "%u bytes", header->data_unpack_size);
    ok = ok && nmo_cli_record_uint(rec, "object_count", "Objects", header->object_count) &&
         nmo_cli_record_uint(rec, "manager_count", "Managers", header->manager_count) &&
         nmo_cli_record_uint(rec, "max_id_saved", "Max ID Saved", header->max_id_saved);
    return ok &&
           nmo_cli_record_uint(rec, "product_version", "Product Ver/Build", header->product_version) &&
           nmo_cli_record_set_text_fmt(rec, "%u / %u", header->product_version, header->product_build) &&
           nmo_cli_record_uint(rec, "product_build", NULL, header->product_build) &&
           nmo_cli_record_uint(rec, "hdr1_unpack_size", NULL, header->hdr1_unpack_size);
}

static int nmo_cmd_file_header_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Get header - cast from opaque nmo_header_t to public nmo_file_header_t */
    const nmo_file_header_t *header =
        (const nmo_file_header_t *)nmo_document_get_header(c->document);
    if (!header) {
        fprintf(stderr, "Error: Failed to get file header\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_title(rec, "File Header");
    if (!ok || !file_header_build_record(header, rec)) {
        nmo_cli_record_free(rec);
        fprintf(stderr, "Error: Out of memory\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    return nmo_cmd_ctx_emit_record(c, rec, "file.header", 18, c->colorize);
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

/* Emit a finished record, or report INTERNAL_ERROR when building it failed. */
static int file_emit(nmo_cmd_ctx_t *c,
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

/* ============================================================================
 * file stats
 * ============================================================================ */

static int nmo_cmd_file_stats_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
    (void)argc;
    (void)argv;
    /* Collect stats */
    nmo_file_stats_t stats;
    if (nmo_tool_owner_stats_collect(c->workspace, &stats) != NMO_OK) {
        fprintf(stderr, "Error: Failed to collect statistics\n");
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Performance timings are always in JSON, in text only when verbose. */
    bool verbose = c->global && c->global->verbosity > 0;

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_title(rec, "File Statistics");

    nmo_cli_record_t *objects = ok ? nmo_cli_record_object(rec, "objects") : NULL;
    ok = objects != NULL;
    ok = ok && nmo_cli_record_heading(objects, "Objects");
    ok = ok && nmo_cli_record_uint(objects, "total", "Total",
                                   stats.objects.total_count);
    ok = ok && nmo_cli_record_uint(objects, "unique_classes", "Unique Classes",
                                   stats.objects.unique_classes);

    nmo_cli_record_t *chunks = ok ? nmo_cli_record_object(rec, "chunks") : NULL;
    ok = chunks != NULL;
    ok = ok && nmo_cli_record_heading(chunks, "Chunks");
    ok = ok && nmo_cli_record_uint(chunks, "total", "Total",
                                   stats.chunks.total_chunks);
    ok = ok && nmo_cli_record_uint(chunks, "compressed", "Compressed",
                                   stats.chunks.compressed_chunks);
    ok = ok && nmo_cli_record_uint(chunks, "max_size", "Max Size",
                                   stats.chunks.max_chunk_size);
    ok = ok && nmo_cli_record_text_fmt(chunks, "Avg Size", "%zu",
                                       stats.chunks.avg_chunk_size);

    nmo_cli_record_t *memory = ok ? nmo_cli_record_object(rec, "memory") : NULL;
    ok = memory != NULL;
    ok = ok && nmo_cli_record_heading(memory, "Memory");
    ok = ok && nmo_cli_record_uint(memory, "total_size", "Total Size",
                                   stats.memory.total_size);
    ok = ok && nmo_cli_record_set_text_fmt(memory, "%zu bytes",
                                           stats.memory.total_size);
    ok = ok && nmo_cli_record_uint(memory, "header_size", "Header Size",
                                   stats.memory.header_size);
    ok = ok && nmo_cli_record_set_text_fmt(memory, "%zu bytes",
                                           stats.memory.header_size);
    ok = ok && nmo_cli_record_uint(memory, "data_size", "Data Size",
                                   stats.memory.data_size);
    ok = ok && nmo_cli_record_set_text_fmt(memory, "%zu bytes",
                                           stats.memory.data_size);
    ok = ok && nmo_cli_record_uint(memory, "chunk_data_size", "Chunk Data",
                                   stats.memory.chunk_data_size);
    ok = ok && nmo_cli_record_set_text_fmt(memory, "%zu bytes",
                                           stats.memory.chunk_data_size);
    ok = ok && nmo_cli_record_uint(memory, "chunk_overhead", NULL,
                                   stats.memory.chunk_overhead);
    ok = ok && nmo_cli_record_uint(memory, "compression_ratio", "Compression",
                                   stats.memory.compression_ratio);
    ok = ok && nmo_cli_record_set_text_fmt(memory, "%zu%%",
                                           stats.memory.compression_ratio);

    nmo_cli_record_t *refs = ok ? nmo_cli_record_object(rec, "references") : NULL;
    ok = refs != NULL;
    ok = ok && nmo_cli_record_heading(refs, "References");
    ok = ok && nmo_cli_record_uint(refs, "total", "Total",
                                   stats.references.total_references);
    ok = ok && nmo_cli_record_uint(refs, "resolved", "Resolved",
                                   stats.references.resolved);
    ok = ok && nmo_cli_record_uint(refs, "unresolved", "Unresolved",
                                   stats.references.unresolved);

    nmo_cli_record_t *perf = ok ? nmo_cli_record_object(rec, "performance") : NULL;
    ok = perf != NULL;
    if (verbose) {
        ok = ok && nmo_cli_record_heading(perf, "Performance");
    }
    ok = ok && nmo_cli_record_real(perf, "load_time_ms",
                                   verbose ? "Load Time" : NULL,
                                   stats.performance.load_time_ms, "%.2f ms");
    ok = ok && nmo_cli_record_real(perf, "parse_time_ms",
                                   verbose ? "Parse Time" : NULL,
                                   stats.performance.parse_time_ms, "%.2f ms");
    ok = ok && nmo_cli_record_real(perf, "remap_time_ms",
                                   verbose ? "Remap Time" : NULL,
                                   stats.performance.remap_time_ms, "%.2f ms");

    return file_emit(c, rec, ok, "file.stats", 20);
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

static int nmo_cmd_file_classes_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
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

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_title(rec, "File Class IDs");
    ok = ok && nmo_cli_record_text(rec, "File", c->file_path);
    ok = ok && nmo_cli_record_raw_fmt(
        rec, "\n%-12s %-8s %-12s %-10s %-6s %s\n"
             "--------------------------------------------------------------\n",
        "CLASS ID", "COUNT", "TOTAL SIZE", "AVG SIZE", "%", "NAME");

    nmo_cli_record_array_t *classes =
        ok ? nmo_cli_record_array(rec, "classes", NULL) : NULL;
    ok = classes != NULL;
    if (ok) {
        nmo_cli_record_array_omit_heading(classes);
    }
    for (size_t i = 0; ok && i < entry_count; i++) {
        const nmo_class_count_entry_t *entry = &entries[i];
        const nmo_type_descriptor_t *type_desc =
            (c->registry != NULL)
                ? nmo_type_registry_find_by_class_id(c->registry, entry->class_id)
                : NULL;
        const char *name = (type_desc != NULL) ? type_desc->name : NULL;
        size_t avg = (entry->count > 0) ? entry->total_size / entry->count : 0;
        double pct = (grand_total_size > 0)
            ? (double)entry->total_size * 100.0 / (double)grand_total_size
            : 0.0;

        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL;
        ok = ok && nmo_cli_record_uint(item, "class_id", NULL, entry->class_id);
        ok = ok && nmo_cli_record_uint(item, "count", NULL, entry->count);
        ok = ok && nmo_cli_record_uint(item, "total_size", NULL, entry->total_size);
        ok = ok && nmo_cli_record_uint(item, "avg_size", NULL, avg);
        ok = ok && nmo_cli_record_real(item, "percentage", NULL, pct, NULL);
        if (name != NULL) {
            ok = ok && nmo_cli_record_str(item, "name", NULL, name);
        }
        ok = ok && nmo_cli_record_set_summary_fmt(
            item, "0x%08X %-8zu %-12zu %-10zu %5.1f%% %s",
            entry->class_id, entry->count, entry->total_size, avg, pct,
            name != NULL ? name : "");
        if (!ok) {
            nmo_cli_record_free(item);
        } else {
            ok = nmo_cli_record_array_add(classes, item);
        }
    }
    ok = ok && nmo_cli_record_uint(rec, "grand_total_size", NULL, grand_total_size);
    ok = ok && nmo_cli_record_raw_fmt(rec, "\nTotal data size: %zu bytes\n",
                                      grand_total_size);

    free(entries);
    return file_emit(c, rec, ok, "file.classes", 8);
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

static int nmo_cmd_file_plugins_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Get plugin diagnostics */
    const nmo_tool_plugin_diagnostics_t *diag =
        nmo_document_get_plugin_diagnostics(c->document);

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_title(rec, "Plugin Dependencies");
    if (!diag) {
        ok = ok && nmo_cli_record_bool(rec, "extension_registry_available", NULL, false);
        ok = ok && nmo_cli_record_uint(rec, "missing_count", NULL, 0);
        ok = ok && nmo_cli_record_uint(rec, "outdated_count", NULL, 0);
        ok = ok && nmo_cli_record_uint(rec, "entry_count", NULL, 0);
        ok = ok && nmo_cli_record_raw(rec, "Plugin diagnostics unavailable\n");
    } else {
        ok = ok && nmo_cli_record_bool(rec, "extension_registry_available",
                                       "Registry Available",
                                       diag->extension_registry_available);
        ok = ok && nmo_cli_record_set_text(
            rec, diag->extension_registry_available ? "yes" : "no");
        ok = ok && nmo_cli_record_uint(rec, "missing_count", "Missing",
                                       diag->missing_count);
        ok = ok && nmo_cli_record_uint(rec, "outdated_count", "Outdated",
                                       diag->outdated_count);
        ok = ok && nmo_cli_record_uint(rec, "entry_count", "Total Entries",
                                       diag->entry_count);
    }

    nmo_cli_record_array_t *entries =
        ok ? nmo_cli_record_array(rec, "entries", NULL) : NULL;
    ok = entries != NULL;
    size_t entry_count = (diag && diag->entries) ? diag->entry_count : 0;
    if (ok && entry_count > 0) {
        ok = nmo_cli_record_array_set_heading(entries, "Entries:");
    } else if (ok) {
        nmo_cli_record_array_omit_heading(entries);
    }
    for (size_t i = 0; ok && i < entry_count; ++i) {
        const nmo_tool_plugin_dependency_status_t *e = &diag->entries[i];
        const char *category_name = file_plugin_category_name(e->category);
        char guid_buf[NMO_GUID_STRING_SIZE];
        nmo_guid_format(e->guid, guid_buf, sizeof(guid_buf));

        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL;
        ok = ok && nmo_cli_record_guid(item, "guid", NULL, e->guid);
        ok = ok && nmo_cli_record_uint(item, "category", NULL, (uint32_t)e->category);
        ok = ok && nmo_cli_record_str(item, "category_name", NULL, category_name);
        ok = ok && nmo_cli_record_uint(item, "required_version", NULL,
                                       e->required_version);
        ok = ok && nmo_cli_record_uint(item, "resolved_version", NULL,
                                       e->resolved_version);
        if (e->resolved_name) {
            ok = ok && nmo_cli_record_str(item, "name", NULL, e->resolved_name);
        }
        ok = ok && nmo_cli_record_uint(item, "status_flags", NULL, e->status_flags);

        const char *name_open = e->resolved_name ? " (" : "";
        const char *name = e->resolved_name ? e->resolved_name : "";
        const char *name_close = e->resolved_name ? ")" : "";
        if (e->status_flags) {
            ok = ok && nmo_cli_record_set_summary_fmt(
                item, "  %s [%s req=%u resolved=%u]%s%s%s [flags=0x%X]",
                guid_buf, category_name, e->required_version,
                e->resolved_version, name_open, name, name_close,
                e->status_flags);
        } else {
            ok = ok && nmo_cli_record_set_summary_fmt(
                item, "  %s [%s req=%u resolved=%u]%s%s%s",
                guid_buf, category_name, e->required_version,
                e->resolved_version, name_open, name, name_close);
        }
        if (!ok) {
            nmo_cli_record_free(item);
        } else {
            ok = nmo_cli_record_array_add(entries, item);
        }
    }

    return file_emit(c, rec, ok, "file.plugins", 18);
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

static int nmo_cmd_file_space_in_session(nmo_cmd_ctx_t *c, int argc, char **argv) {
    static const nmo_opt_def_t opts[] = {
        {"--top", "-t", NMO_OPT_UINT, "Show top N objects by size (default: 15)"},
    };
    enum { OPT_TOP, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    uint32_t top_n = vals[OPT_TOP].present ? vals[OPT_TOP].val.u : 15;

    nmo_file_info_t info = nmo_document_get_file_info(c->document);

    file_space_collect_t collect = {0};
    const nmo_file_header_t *header =
        (const nmo_file_header_t *)nmo_document_get_header(c->document);
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

    static const char bar_fill[] = "####################";

    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_title(rec, "Space Analysis");
    ok = ok && nmo_cli_record_uint(rec, "file_size", "File Size", info.file_size);
    ok = ok && nmo_cli_record_set_text_fmt(rec, "%zu bytes", info.file_size);
    ok = ok && nmo_cli_record_uint(rec, "object_count", "Objects", obj_count);
    ok = ok && nmo_cli_record_uint(rec, "total_data_size", "Total Data", total_data);
    ok = ok && nmo_cli_record_set_text_fmt(rec, "%" PRIu64 " bytes", total_data);
    ok = ok && nmo_cli_record_uint(rec, "total_pack_size", "Total Packed", total_pack);
    ok = ok && nmo_cli_record_set_text_fmt(rec, "%" PRIu64 " bytes", total_pack);
    if (total_data > 0) {
        ok = ok && nmo_cli_record_text_fmt(
            rec, "Compression", "%.1f%%",
            (double)total_pack / (double)total_data * 100.0);
    }
    ok = ok && nmo_cli_record_uint(rec, "compressed_objects", "Compressed",
                                   compressed_count);
    ok = ok && nmo_cli_record_set_text_fmt(rec, "%" PRIu64 " / %zu",
                                           compressed_count, obj_count);

    /* Per-class breakdown with cumulative % and ASCII bar */
    ok = ok && nmo_cli_record_heading(rec, "Space by Class");
    ok = ok && nmo_cli_record_raw_fmt(
        rec, "%-20s  %5s  %10s  %10s  %6s  %6s  %s\n"
             "%-20s  %5s  %10s  %10s  %6s  %6s  %s\n",
        "CLASS", "COUNT", "DATA", "PACKED", "%", "CUM%", "BAR",
        "--------------------", "-----", "----------", "----------",
        "------", "------", "--------------------");

    nmo_cli_record_array_t *cls_arr =
        ok ? nmo_cli_record_array(rec, "classes", NULL) : NULL;
    ok = cls_arr != NULL;
    if (ok) {
        nmo_cli_record_array_omit_heading(cls_arr);
    }
    uint64_t cumul = 0;
    for (size_t i = 0; ok && i < class_count; i++) {
        cumul += classes[i].data_size;
        double pct = total_data > 0
            ? (double)classes[i].data_size / (double)total_data * 100.0 : 0.0;
        double cum_pct = total_data > 0
            ? (double)cumul / (double)total_data * 100.0 : 0.0;
        int bar_len = (int)(pct / 5.0 + 0.5);
        if (bar_len > 20) bar_len = 20;

        nmo_cli_record_t *item = nmo_cli_record_new();
        ok = item != NULL;
        if (classes[i].class_name) {
            ok = ok && nmo_cli_record_str(item, "class_name", NULL,
                                          classes[i].class_name);
        }
        ok = ok && nmo_cli_record_uint(item, "count", NULL, classes[i].count);
        ok = ok && nmo_cli_record_uint(item, "data_size", NULL, classes[i].data_size);
        ok = ok && nmo_cli_record_uint(item, "pack_size", NULL, classes[i].pack_size);
        if (total_data > 0) {
            ok = ok && nmo_cli_record_real(item, "percent", NULL, pct, NULL);
            ok = ok && nmo_cli_record_real(item, "cumulative_percent", NULL,
                                           cum_pct, NULL);
        }
        ok = ok && nmo_cli_record_set_summary_fmt(
            item, "%-20s  %5u  %10" PRIu64 "  %10" PRIu64 "  %5.1f%%  %5.1f%%  %.*s",
            classes[i].class_name ? classes[i].class_name : "?",
            classes[i].count,
            classes[i].data_size,
            classes[i].pack_size,
            pct, cum_pct, bar_len, bar_fill);
        if (!ok) {
            nmo_cli_record_free(item);
        } else {
            ok = nmo_cli_record_array_add(cls_arr, item);
        }
    }

    /* Top N objects (already sorted above) */
    if (obj_entries && obj_count > 0) {
        size_t show_n = obj_count < top_n ? obj_count : top_n;
        ok = ok && nmo_cli_record_raw(rec, "\n");
        ok = ok && nmo_cli_record_title_fmt(rec, "Top %zu Objects by Size", show_n);
        ok = ok && nmo_cli_record_raw_fmt(
            rec, "%5s  %-20s  %10s  %10s  %6s  %-s\n"
                 "%5s  %-20s  %10s  %10s  %6s  %-s\n",
            "ID", "CLASS", "DATA", "PACKED", "RATIO", "NAME",
            "-----", "--------------------", "----------", "----------",
            "------", "--------------------");

        nmo_cli_record_array_t *top_arr =
            ok ? nmo_cli_record_array(rec, "top_objects", NULL) : NULL;
        ok = top_arr != NULL;
        if (ok) {
            nmo_cli_record_array_omit_heading(top_arr);
        }
        for (size_t i = 0; ok && i < show_n; i++) {
            nmo_object_t *obj = obj_entries[i].obj;
            const char *cn = nmo_core_class_name(c, nmo_object_get_class_id(obj));
            const char *nm = nmo_object_get_name(obj);
            double ratio = obj_entries[i].data_sz > 0
                ? (double)obj_entries[i].pack_sz / (double)obj_entries[i].data_sz * 100.0
                : 0.0;

            nmo_cli_record_t *item = nmo_cli_record_new();
            ok = item != NULL;
            ok = ok && nmo_cli_record_uint(item, "id", NULL, nmo_object_get_id(obj));
            if (cn) {
                ok = ok && nmo_cli_record_str(item, "class_name", NULL, cn);
            }
            ok = ok && nmo_cli_record_str_opt(item, "name", NULL, nm, NULL);
            ok = ok && nmo_cli_record_uint(item, "data_size", NULL,
                                           obj_entries[i].data_sz);
            ok = ok && nmo_cli_record_uint(item, "pack_size", NULL,
                                           obj_entries[i].pack_sz);
            ok = ok && nmo_cli_record_set_summary_fmt(
                item, "%5u  %-20s  %10" PRIu64 "  %10" PRIu64 "  %5.1f%%  %s",
                nmo_object_get_id(obj),
                cn ? cn : "?",
                obj_entries[i].data_sz,
                obj_entries[i].pack_sz,
                ratio,
                (nm && nm[0]) ? nm : "(unnamed)");
            if (!ok) {
                nmo_cli_record_free(item);
            } else {
                ok = nmo_cli_record_array_add(top_arr, item);
            }
        }
    }

    free(obj_entries);
    return file_emit(c, rec, ok, "file.space", 20);
}

int nmo_cmd_file_space(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;
    rc = nmo_cmd_file_space_in_session(&c, argc, argv);
    return nmo_cmd_ctx_done(&c, rc);
}

