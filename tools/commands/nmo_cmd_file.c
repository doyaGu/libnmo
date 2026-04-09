/**
 * @file nmo_cmd_file.c
 * @brief CLI file command group implementation
 */

#include "nmo_cmd_file.h"

#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_output.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"

#include "nmo.h"
#include "app/nmo_stats.h"
#include "format/nmo_header.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

    if (!nmo_tool_open_session(file_path, &ctx, &session, errbuf, sizeof(errbuf))) {
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

    /* Single file mode - uses nmo_cmd_ctx_init_no_file because
     * file_info_single opens its own session internally */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (rc) return rc;

    const char *file_path = nmo_tool_find_file_arg(argc, argv);
    if (!file_path) {
        fprintf(stderr, "Error: No file specified\n");
        fprintf(stderr, "Usage: nmo file info <file>\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = NULL;
        yyjson_mut_val *data = NULL;
        if (!nmo_cli_json_create_data_doc(&doc, &data)) {
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        rc = file_info_single(file_path, global, NULL, doc, data);

        yyjson_mut_obj_add_str(doc, data, "file", file_path);
        nmo_cli_json_write_enveloped_and_free(doc, data, "file.info", file_path,
                                              c.out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
        return nmo_cmd_ctx_done(&c, rc);
    }

    /* Text mode */
    nmo_tool_text_output_ctx_t text_ctx = {
        .out = c.out,
        .colorize = c.colorize,
        .user_data = NULL
    };
    nmo_cli_print_heading(c.out, "File Info", c.colorize);
    nmo_cli_print_kv(c.out, "File", file_path, 14, c.colorize);

    rc = file_info_single(file_path, global, &text_ctx, NULL, NULL);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * file header
 * ============================================================================ */

int nmo_cmd_file_header(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Get header - cast from opaque nmo_header_t to public nmo_file_header_t */
    const nmo_file_header_t *header = (const nmo_file_header_t *)nmo_session_get_header(c.session);
    if (!header) {
        fprintf(stderr, "Error: Failed to get file header\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        char sig_buf[9];
        memcpy(sig_buf, header->signature, 8);
        sig_buf[8] = '\0';
        yyjson_mut_obj_add_str(doc, data, "signature", sig_buf);
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

        nmo_cmd_ctx_json_end(&c, doc, data, "file.header");
    } else {
        nmo_cli_print_heading(c.out, "File Header", c.colorize);

        char sig_buf[9];
        memcpy(sig_buf, header->signature, 8);
        sig_buf[8] = '\0';
        nmo_cli_print_kv(c.out, "Signature", sig_buf, 18, c.colorize);

        char buf[64];
        snprintf(buf, sizeof(buf), "%u (secondary %u)", header->file_version, header->file_version2);
        nmo_cli_print_kv(c.out, "File Version", buf, 18, c.colorize);

        snprintf(buf, sizeof(buf), "0x%08X", header->ck_version);
        nmo_cli_print_kv(c.out, "CK Version", buf, 18, c.colorize);

        snprintf(buf, sizeof(buf), "0x%08X", header->crc);
        nmo_cli_print_kv(c.out, "CRC", buf, 18, c.colorize);

        snprintf(buf, sizeof(buf), "0x%X", header->file_write_mode);
        nmo_cli_print_kv(c.out, "Write Mode", buf, 18, c.colorize);

        snprintf(buf, sizeof(buf), "%u bytes", header->hdr1_pack_size);
        nmo_cli_print_kv(c.out, "Header1 Packed", buf, 18, c.colorize);

        if (header->file_version >= 5) {
            snprintf(buf, sizeof(buf), "%u bytes", header->data_pack_size);
            nmo_cli_print_kv(c.out, "Data Packed", buf, 18, c.colorize);

            snprintf(buf, sizeof(buf), "%u bytes", header->data_unpack_size);
            nmo_cli_print_kv(c.out, "Data Unpacked", buf, 18, c.colorize);

            snprintf(buf, sizeof(buf), "%u", header->object_count);
            nmo_cli_print_kv(c.out, "Objects", buf, 18, c.colorize);

            snprintf(buf, sizeof(buf), "%u", header->manager_count);
            nmo_cli_print_kv(c.out, "Managers", buf, 18, c.colorize);

            snprintf(buf, sizeof(buf), "%u", header->max_id_saved);
            nmo_cli_print_kv(c.out, "Max ID Saved", buf, 18, c.colorize);

            snprintf(buf, sizeof(buf), "%u / %u", header->product_version, header->product_build);
            nmo_cli_print_kv(c.out, "Product Ver/Build", buf, 18, c.colorize);
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * file stats
 * ============================================================================ */

int nmo_cmd_file_stats(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Collect stats */
    nmo_file_stats_t stats;
    if (nmo_stats_collect(c.session, &stats) != 0) {
        fprintf(stderr, "Error: Failed to collect statistics\n");
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
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

        nmo_cmd_ctx_json_end(&c, doc, data, "file.stats");
    } else {
        nmo_cli_print_heading(c.out, "File Statistics", c.colorize);
        fprintf(c.out, "\n");

        nmo_cli_print_heading(c.out, "Objects", c.colorize);
        char buf[64];
        snprintf(buf, sizeof(buf), "%zu", stats.objects.total_count);
        nmo_cli_print_kv(c.out, "Total", buf, 20, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.objects.unique_classes);
        nmo_cli_print_kv(c.out, "Unique Classes", buf, 20, c.colorize);
        fprintf(c.out, "\n");

        nmo_cli_print_heading(c.out, "Chunks", c.colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.chunks.total_chunks);
        nmo_cli_print_kv(c.out, "Total", buf, 20, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.chunks.compressed_chunks);
        nmo_cli_print_kv(c.out, "Compressed", buf, 20, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.chunks.max_chunk_size);
        nmo_cli_print_kv(c.out, "Max Size", buf, 20, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.chunks.avg_chunk_size);
        nmo_cli_print_kv(c.out, "Avg Size", buf, 20, c.colorize);
        fprintf(c.out, "\n");

        nmo_cli_print_heading(c.out, "Memory", c.colorize);
        snprintf(buf, sizeof(buf), "%zu bytes", stats.memory.total_size);
        nmo_cli_print_kv(c.out, "Total Size", buf, 20, c.colorize);
        snprintf(buf, sizeof(buf), "%zu bytes", stats.memory.header_size);
        nmo_cli_print_kv(c.out, "Header Size", buf, 20, c.colorize);
        snprintf(buf, sizeof(buf), "%zu bytes", stats.memory.data_size);
        nmo_cli_print_kv(c.out, "Data Size", buf, 20, c.colorize);
        snprintf(buf, sizeof(buf), "%zu bytes", stats.memory.chunk_data_size);
        nmo_cli_print_kv(c.out, "Chunk Data", buf, 20, c.colorize);
        snprintf(buf, sizeof(buf), "%zu%%", stats.memory.compression_ratio);
        nmo_cli_print_kv(c.out, "Compression", buf, 20, c.colorize);
        fprintf(c.out, "\n");

        nmo_cli_print_heading(c.out, "References", c.colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.references.total_references);
        nmo_cli_print_kv(c.out, "Total", buf, 20, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.references.resolved);
        nmo_cli_print_kv(c.out, "Resolved", buf, 20, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", stats.references.unresolved);
        nmo_cli_print_kv(c.out, "Unresolved", buf, 20, c.colorize);

        if (c.global->verbosity > 0) {
            fprintf(c.out, "\n");
            nmo_cli_print_heading(c.out, "Performance", c.colorize);
            snprintf(buf, sizeof(buf), "%.2f ms", stats.performance.load_time_ms);
            nmo_cli_print_kv(c.out, "Load Time", buf, 20, c.colorize);
            snprintf(buf, sizeof(buf), "%.2f ms", stats.performance.parse_time_ms);
            nmo_cli_print_kv(c.out, "Parse Time", buf, 20, c.colorize);
            snprintf(buf, sizeof(buf), "%.2f ms", stats.performance.remap_time_ms);
            nmo_cli_print_kv(c.out, "Remap Time", buf, 20, c.colorize);
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * file classes
 * ============================================================================ */

typedef struct nmo_class_count_entry {
    uint32_t class_id;
    size_t count;
    size_t total_size;
} nmo_class_count_entry_t;

/* Sort key enum and parser */
typedef enum {
    CLASS_SORT_ID = 0,
    CLASS_SORT_SIZE,
    CLASS_SORT_COUNT,
    CLASS_SORT_NAME,
} class_sort_key_t;

static class_sort_key_t parse_class_sort_key(const char *s) {
    if (!s) return CLASS_SORT_ID;
    if (strcmp(s, "id")    == 0) return CLASS_SORT_ID;
    if (strcmp(s, "size")  == 0) return CLASS_SORT_SIZE;
    if (strcmp(s, "count") == 0) return CLASS_SORT_COUNT;
    if (strcmp(s, "name")  == 0) return CLASS_SORT_NAME;
    return CLASS_SORT_ID;
}

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

static class_compare_fn class_sort_comparator(class_sort_key_t key) {
    switch (key) {
        case CLASS_SORT_ID:    return compare_class_by_id;
        case CLASS_SORT_SIZE:  return compare_class_by_size;
        case CLASS_SORT_COUNT: return compare_class_by_count;
        case CLASS_SORT_NAME:  return compare_class_by_name;
        default:               return compare_class_by_id;
    }
}

int nmo_cmd_file_classes(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--sort", NULL, NMO_OPT_STRING, "Sort by: id (default), size, count, name"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[16];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 16 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *sort_key_str = vals[0].present ? vals[0].val.str : NULL;

    /* Validate sort key early */
    class_sort_key_t sort_key = parse_class_sort_key(sort_key_str);
    if (sort_key_str &&
        strcmp(sort_key_str, "id") != 0 &&
        strcmp(sort_key_str, "size") != 0 &&
        strcmp(sort_key_str, "count") != 0 &&
        strcmp(sort_key_str, "name") != 0) {
        fprintf(stderr, "Error: Invalid sort key '%s' (use: id, size, count, name)\n", sort_key_str);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    size_t object_count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &object_count);

    nmo_class_count_entry_t *entries = NULL;
    size_t entry_count = 0;
    size_t entry_capacity = 0;
    size_t grand_total_size = 0;

    for (size_t i = 0; i < object_count; i++) {
        nmo_object_t *obj = objects[i];
        if (obj == NULL) {
            continue;
        }

        uint32_t class_id = nmo_object_get_class_id(obj);
        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        size_t obj_size = chunk ? nmo_chunk_get_data_size(chunk) : 0;
        grand_total_size += obj_size;

        size_t found = (size_t)-1;
        for (size_t j = 0; j < entry_count; j++) {
            if (entries[j].class_id == class_id) {
                found = j;
                break;
            }
        }

        if (found == (size_t)-1) {
            if (entry_count == entry_capacity) {
                size_t new_capacity = (entry_capacity == 0) ? 16 : entry_capacity * 2;
                nmo_class_count_entry_t *new_entries =
                    (nmo_class_count_entry_t *)realloc(entries, new_capacity * sizeof(nmo_class_count_entry_t));
                if (new_entries == NULL) {
                    free(entries);
                    fprintf(stderr, "Error: Out of memory\n");
                    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
                }
                entries = new_entries;
                entry_capacity = new_capacity;
            }
            entries[entry_count].class_id = class_id;
            entries[entry_count].count = 1;
            entries[entry_count].total_size = obj_size;
            entry_count++;
        } else {
            entries[found].count++;
            entries[found].total_size += obj_size;
        }
    }

    /* Sort entries */
    if (entry_count > 1) {
        s_class_sort_registry = c.registry;
        class_compare_fn cmp = class_sort_comparator(sort_key);
        qsort(entries, entry_count, sizeof(nmo_class_count_entry_t), cmp);
    }

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);
        yyjson_mut_val *classes = yyjson_mut_arr(doc);

        for (size_t i = 0; i < entry_count; i++) {
            const nmo_class_count_entry_t *entry = &entries[i];
            const nmo_type_descriptor_t *type_desc =
                (c.registry != NULL)
                    ? nmo_type_registry_find_by_class_id(c.registry, entry->class_id)
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
        nmo_cmd_ctx_json_end(&c, doc, data, "file.classes");
    } else {
        nmo_cli_print_heading(c.out, "File Class IDs", c.colorize);
        nmo_cli_print_kv(c.out, "File", c.file_path, 8, c.colorize);
        fprintf(c.out, "\n");

        fprintf(c.out, "%-12s %-8s %-12s %-10s %-6s %s\n",
                "CLASS ID", "COUNT", "TOTAL SIZE", "AVG SIZE", "%", "NAME");
        fprintf(c.out, "--------------------------------------------------------------\n");
        for (size_t i = 0; i < entry_count; i++) {
            const nmo_class_count_entry_t *entry = &entries[i];
            const nmo_type_descriptor_t *type_desc =
                (c.registry != NULL)
                    ? nmo_type_registry_find_by_class_id(c.registry, entry->class_id)
                    : NULL;
            const char *name = (type_desc != NULL && type_desc->name != NULL) ? type_desc->name : "";
            size_t avg = (entry->count > 0) ? entry->total_size / entry->count : 0;
            double pct = (grand_total_size > 0)
                ? (double)entry->total_size * 100.0 / (double)grand_total_size
                : 0.0;
            fprintf(c.out, "0x%08X %-8zu %-12zu %-10zu %5.1f%% %s\n",
                    entry->class_id, entry->count, entry->total_size, avg, pct, name);
        }
        fprintf(c.out, "\nTotal data size: %zu bytes\n", grand_total_size);
    }

    free(entries);
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * file plugins
 * ============================================================================ */

int nmo_cmd_file_plugins(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Get plugin diagnostics */
    const nmo_session_plugin_diagnostics_t *diag = nmo_session_get_plugin_diagnostics(c.session);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
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
                yyjson_mut_obj_add_str(doc, entry, "guid", guid_buf);
                if (e->resolved_name) {
                    yyjson_mut_obj_add_str(doc, entry, "name", e->resolved_name);
                }
                yyjson_mut_obj_add_uint(doc, entry, "status_flags", e->status_flags);

                yyjson_mut_arr_add_val(entries, entry);
            }
        }
        yyjson_mut_obj_add_val(doc, data, "entries", entries);

        nmo_cmd_ctx_json_end(&c, doc, data, "file.plugins");
    } else {
        nmo_cli_print_heading(c.out, "Plugin Dependencies", c.colorize);

        if (!diag) {
            fprintf(c.out, "Plugin diagnostics unavailable\n");
        } else {
            char buf[64];
            nmo_cli_print_kv(c.out, "Registry Available",
                            diag->extension_registry_available ? "yes" : "no", 18, c.colorize);
            snprintf(buf, sizeof(buf), "%zu", diag->missing_count);
            nmo_cli_print_kv(c.out, "Missing", buf, 18, c.colorize);
            snprintf(buf, sizeof(buf), "%zu", diag->outdated_count);
            nmo_cli_print_kv(c.out, "Outdated", buf, 18, c.colorize);
            snprintf(buf, sizeof(buf), "%zu", diag->entry_count);
            nmo_cli_print_kv(c.out, "Total Entries", buf, 18, c.colorize);

            if (diag->entries && diag->entry_count > 0) {
                fprintf(c.out, "\nEntries:\n");
                for (size_t i = 0; i < diag->entry_count; ++i) {
                    const nmo_session_plugin_dependency_status_t *e = &diag->entries[i];
                    char guid_buf[64];
                    nmo_guid_format(e->guid, guid_buf, sizeof(guid_buf));
                    fprintf(c.out, "  %s", guid_buf);
                    if (e->resolved_name) {
                        fprintf(c.out, " (%s)", e->resolved_name);
                    }
                    if (e->status_flags) {
                        fprintf(c.out, " [flags=0x%X]", e->status_flags);
                    }
                    fprintf(c.out, "\n");
                }
            }
        }
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
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

int nmo_cmd_file_space(int argc, char **argv, const nmo_cli_global_opts_t *global) {
    static const nmo_opt_def_t opts[] = {
        {"--top", NULL, NMO_OPT_UINT, "Show top N objects by size (default: 15)"},
    };
    enum { OPT_TOP, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    uint32_t top_n = vals[OPT_TOP].present ? vals[OPT_TOP].val.u : 15;

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_file_info_t info = nmo_session_get_file_info(c.session);

    /* Collect all objects */
    nmo_object_repository_t *repo = nmo_session_get_repository(c.session);
    size_t obj_count = 0;
    nmo_object_t **objects = nmo_object_repository_get_all(repo, &obj_count);

    /* Per-class aggregation */
    space_class_entry_t classes[256];
    size_t class_count = 0;

    /* Per-object data for top-N */
    typedef struct { nmo_object_t *obj; uint64_t data_sz; uint64_t pack_sz; } obj_entry_t;
    obj_entry_t *obj_entries = NULL;
    if (obj_count > 0)
        obj_entries = (obj_entry_t *)malloc(obj_count * sizeof(obj_entry_t));

    uint64_t total_data = 0, total_pack = 0;
    uint64_t compressed_count = 0;

    for (size_t i = 0; i < obj_count; i++) {
        nmo_object_t *obj = objects[i];
        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        uint64_t data_sz = chunk ? (uint64_t)nmo_chunk_get_data_size(chunk) : 0;
        uint64_t pack_sz = chunk ? (uint64_t)chunk->compressed_size : 0;
        if (chunk && chunk->is_compressed) compressed_count++;

        total_data += data_sz;
        total_pack += pack_sz;

        if (obj_entries)
            obj_entries[i] = (obj_entry_t){ .obj = obj, .data_sz = data_sz, .pack_sz = pack_sz };

        /* Accumulate into class bucket */
        nmo_class_id_t cid = nmo_object_get_class_id(obj);
        size_t ci;
        for (ci = 0; ci < class_count; ci++) {
            if (classes[ci].class_id == cid) break;
        }
        if (ci == class_count && class_count < 256) {
            classes[class_count].class_id = cid;
            classes[class_count].class_name = nmo_core_class_name(&c, cid);
            classes[class_count].count = 0;
            classes[class_count].data_size = 0;
            classes[class_count].pack_size = 0;
            class_count++;
        }
        if (ci < 256) {
            classes[ci].count++;
            classes[ci].data_size += data_sz;
            classes[ci].pack_size += pack_sz;
        }
    }

    /* Sort classes by data_size descending */
    qsort(classes, class_count, sizeof(space_class_entry_t), space_class_cmp_size);

    if (c.is_json) {
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
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
                nmo_cli_json_add_str_safe(doc, e, "class", classes[i].class_name);
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

        /* Top objects */
        if (obj_entries && obj_count > 0) {
            for (size_t i = 0; i < obj_count - 1 && i < top_n; i++) {
                for (size_t j = i + 1; j < obj_count; j++) {
                    if (obj_entries[j].data_sz > obj_entries[i].data_sz) {
                        obj_entry_t tmp = obj_entries[i];
                        obj_entries[i] = obj_entries[j];
                        obj_entries[j] = tmp;
                    }
                }
            }
            size_t show_n = obj_count < top_n ? obj_count : top_n;
            yyjson_mut_val *top_arr = yyjson_mut_arr(doc);
            for (size_t i = 0; i < show_n; i++) {
                yyjson_mut_val *e = yyjson_mut_obj(doc);
                nmo_cli_json_add_uint_safe(doc, e, "id",
                    (uint64_t)nmo_object_get_id(obj_entries[i].obj));
                const char *cn = nmo_core_class_name(&c,
                    nmo_object_get_class_id(obj_entries[i].obj));
                if (cn) nmo_cli_json_add_str_safe(doc, e, "class", cn);
                const char *nm = nmo_object_get_name(obj_entries[i].obj);
                if (nm && nm[0]) nmo_cli_json_add_str_safe(doc, e, "name", nm);
                nmo_cli_json_add_uint_safe(doc, e, "data_size", obj_entries[i].data_sz);
                nmo_cli_json_add_uint_safe(doc, e, "pack_size", obj_entries[i].pack_sz);
                yyjson_mut_arr_add_val(top_arr, e);
            }
            yyjson_mut_obj_add_val(doc, data, "top_objects", top_arr);
        }

        nmo_cmd_ctx_json_end(&c, doc, data, "file.space");
    } else {
        nmo_cli_print_heading(c.out, "Space Analysis", c.colorize);

        char buf[128];
        snprintf(buf, sizeof(buf), "%zu bytes", info.file_size);
        nmo_cli_print_kv(c.out, "File Size", buf, 20, c.colorize);
        snprintf(buf, sizeof(buf), "%zu", obj_count);
        nmo_cli_print_kv(c.out, "Objects", buf, 20, c.colorize);
        snprintf(buf, sizeof(buf), "%" PRIu64 " bytes", total_data);
        nmo_cli_print_kv(c.out, "Total Data", buf, 20, c.colorize);
        snprintf(buf, sizeof(buf), "%" PRIu64 " bytes", total_pack);
        nmo_cli_print_kv(c.out, "Total Packed", buf, 20, c.colorize);
        if (total_data > 0) {
            snprintf(buf, sizeof(buf), "%.1f%%",
                     (double)total_pack / (double)total_data * 100.0);
            nmo_cli_print_kv(c.out, "Compression", buf, 20, c.colorize);
        }
        snprintf(buf, sizeof(buf), "%" PRIu64 " / %zu",
                 compressed_count, obj_count);
        nmo_cli_print_kv(c.out, "Compressed", buf, 20, c.colorize);

        /* Per-class breakdown with cumulative % and ASCII bar */
        fprintf(c.out, "\n");
        nmo_cli_print_heading(c.out, "Space by Class", c.colorize);
        fprintf(c.out, "%-20s  %5s  %10s  %10s  %6s  %6s  %s\n",
                "CLASS", "COUNT", "DATA", "PACKED", "%", "CUM%", "BAR");
        fprintf(c.out, "%-20s  %5s  %10s  %10s  %6s  %6s  %s\n",
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

            fprintf(c.out, "%-20s  %5u  %10" PRIu64 "  %10" PRIu64 "  %5.1f%%  %5.1f%%  %s\n",
                    classes[i].class_name ? classes[i].class_name : "?",
                    classes[i].count,
                    classes[i].data_size,
                    classes[i].pack_size,
                    pct, cum_pct, bar);
        }

        /* Top N objects */
        if (obj_entries && obj_count > 0) {
            for (size_t i = 0; i < obj_count - 1 && i < top_n; i++) {
                for (size_t j = i + 1; j < obj_count; j++) {
                    if (obj_entries[j].data_sz > obj_entries[i].data_sz) {
                        obj_entry_t tmp = obj_entries[i];
                        obj_entries[i] = obj_entries[j];
                        obj_entries[j] = tmp;
                    }
                }
            }

            size_t show_n = obj_count < top_n ? obj_count : top_n;
            fprintf(c.out, "\n");
            snprintf(buf, sizeof(buf), "Top %zu Objects by Size", show_n);
            nmo_cli_print_heading(c.out, buf, c.colorize);
            fprintf(c.out, "%5s  %-20s  %10s  %10s  %6s  %-s\n",
                    "ID", "CLASS", "DATA", "PACKED", "RATIO", "NAME");
            fprintf(c.out, "%5s  %-20s  %10s  %10s  %6s  %-s\n",
                    "-----", "--------------------", "----------", "----------",
                    "------", "--------------------");

            for (size_t i = 0; i < show_n; i++) {
                nmo_object_t *obj = obj_entries[i].obj;
                const char *cn = nmo_core_class_name(&c, nmo_object_get_class_id(obj));
                const char *nm = nmo_object_get_name(obj);
                double ratio = obj_entries[i].data_sz > 0
                    ? (double)obj_entries[i].pack_sz / (double)obj_entries[i].data_sz * 100.0
                    : 0.0;

                fprintf(c.out, "%5u  %-20s  %10" PRIu64 "  %10" PRIu64 "  %5.1f%%  %s\n",
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
    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

