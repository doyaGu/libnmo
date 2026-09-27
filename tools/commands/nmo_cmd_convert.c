/**
 * @file nmo_cmd_convert.c
 * @brief CLI convert command group implementation
 */

#include "nmo_cmd_convert.h"
#include "../nmo_cmd_ctx.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cli_write.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"
#include "nmo.h"
#include "document/nmo_document_save.h"
#include "runtime/nmo_context.h"
#include "core/nmo_arena.h"
#include "core/nmo_parse.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_ref_graph.h"
#include "format/nmo_object.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * Helper functions
 * ============================================================================ */

typedef struct nmo_convert_strip_collect {
    const nmo_object_query_t *class_filter;
    const nmo_object_query_t *name_filter;
    nmo_object_id_t *ids;
    nmo_object_t **objects;
    size_t count;
    size_t capacity;
    bool oom;
} nmo_convert_strip_collect_t;

static int convert_strip_collect_object(size_t index,
                                        nmo_object_t *obj,
                                        const nmo_cmd_ctx_t *c,
                                        void *user)
{
    (void)index;

    nmo_convert_strip_collect_t *collect =
        (nmo_convert_strip_collect_t *)user;
    if (!collect || !obj) {
        return 0;
    }

    bool matches = false;
    if (collect->class_filter != NULL &&
        nmo_core_query_matches_object(c, collect->class_filter, obj)) {
        matches = true;
    }
    if (collect->name_filter != NULL &&
        nmo_core_query_matches_object(c, collect->name_filter, obj)) {
        matches = true;
    }
    if (!matches) {
        return 0;
    }

    if (collect->count == collect->capacity) {
        size_t new_capacity = collect->capacity ? collect->capacity * 2 : 32;
        nmo_object_id_t *new_ids = (nmo_object_id_t *)realloc(
            collect->ids, new_capacity * sizeof(*new_ids));
        if (!new_ids) {
            collect->oom = true;
            return 1;
        }
        collect->ids = new_ids;

        nmo_object_t **new_objects = (nmo_object_t **)realloc(
            collect->objects, new_capacity * sizeof(*new_objects));
        if (!new_objects) {
            collect->oom = true;
            return 1;
        }
        collect->objects = new_objects;
        collect->capacity = new_capacity;
    }

    collect->ids[collect->count] = nmo_object_get_id(obj);
    collect->objects[collect->count] = obj;
    collect->count++;
    return 0;
}

typedef struct nmo_convert_id_collect {
    nmo_object_id_t *ids;
    size_t count;
    size_t capacity;
    bool oom;
} nmo_convert_id_collect_t;

static int convert_collect_id(size_t index,
                              nmo_object_t *obj,
                              const nmo_cmd_ctx_t *c,
                              void *user)
{
    (void)index;
    (void)c;

    nmo_convert_id_collect_t *collect = (nmo_convert_id_collect_t *)user;
    if (!collect || !obj) {
        return 0;
    }

    if (collect->count == collect->capacity) {
        size_t new_capacity = collect->capacity ? collect->capacity * 2 : 64;
        nmo_object_id_t *new_ids = (nmo_object_id_t *)realloc(
            collect->ids, new_capacity * sizeof(*new_ids));
        if (!new_ids) {
            collect->oom = true;
            return 1;
        }
        collect->ids = new_ids;
        collect->capacity = new_capacity;
    }

    collect->ids[collect->count++] = nmo_object_get_id(obj);
    return 0;
}

/**
 * @brief Parse compression level from string
 * @return true on success, false on error
 */
static bool parse_compression_level(const char *str, int *out_level)
{
    if (!str || !out_level) {
        return false;
    }

    int32_t val = 0;
    if (nmo_parse_i32_range(str, 0, 9, &val) != NMO_OK) {
        return false;
    }

    *out_level = (int)val;
    return true;
}

static const char *convert_save_durability_name(nmo_save_durability_t durability)
{
    switch (durability) {
        case NMO_SAVE_DURABILITY_FAST:
            return "fast";
        case NMO_SAVE_DURABILITY_FSYNC:
            return "fsync";
        case NMO_SAVE_DURABILITY_DEFAULT:
        default:
            return "default";
    }
}

static void convert_add_save_phase_stats(nmo_cli_record_t *rec,
                                         const nmo_save_perf_stats_t *stats)
{
    nmo_cli_record_t *phase_stats = nmo_cli_record_object(rec, "save_phase_stats");
    nmo_cli_record_uint(phase_stats, "planned_chunk_bytes", NULL, (uint64_t)stats->planned_chunk_bytes);
    nmo_cli_record_uint(phase_stats, "header1_unpacked_bytes", NULL, (uint64_t)stats->header1_unpacked_bytes);
    nmo_cli_record_uint(phase_stats, "data_unpacked_bytes", NULL, (uint64_t)stats->data_unpacked_bytes);
    nmo_cli_record_uint(phase_stats, "header1_packed_bytes", NULL, (uint64_t)stats->header1_packed_bytes);
    nmo_cli_record_uint(phase_stats, "data_packed_bytes", NULL, (uint64_t)stats->data_packed_bytes);

    nmo_cli_record_t *phases = nmo_cli_record_object(phase_stats, "phases");
    nmo_cli_record_raw(rec, "\nSave Phase Timings:\n");
    nmo_cli_record_raw_fmt(rec, "  %-28s %8s %12s\n", "phase", "calls", "ms");
    for (int i = 0; i < NMO_SAVE_PERF_PHASE_COUNT; i++) {
        const nmo_phase_time_t *phase = &stats->phases[i];
        const char *phase_name = nmo_save_perf_phase_name((nmo_save_perf_phase_t)i);
        nmo_cli_record_t *entry = nmo_cli_record_object(phases, phase_name);
        nmo_cli_record_uint(entry, "calls", NULL, phase->calls);
        nmo_cli_record_real(entry, "milliseconds", NULL, phase->milliseconds, NULL);
        nmo_cli_record_raw_fmt(rec, "  %-28s %8llu %12.3f\n",
                               phase_name,
                               (unsigned long long)phase->calls,
                               phase->milliseconds);
    }

    nmo_cli_record_raw(rec, "\nSave Section Bytes:\n");
    nmo_cli_record_raw_fmt(rec, "  Header1: packed=%zu unpacked=%zu\n",
                           stats->header1_packed_bytes,
                           stats->header1_unpacked_bytes);
    nmo_cli_record_raw_fmt(rec, "  Data:    packed=%zu unpacked=%zu\n",
                           stats->data_packed_bytes,
                           stats->data_unpacked_bytes);
}

static void convert_add_saved_text(nmo_cli_record_t *rec, const char *output_path,
                                   nmo_save_durability_t durability)
{
    nmo_cli_record_raw_fmt(rec, "Saved to %s\n", output_path);
    nmo_cli_record_raw_fmt(rec, "Durability: %s\n", convert_save_durability_name(durability));
}

static void convert_add_filters(nmo_cli_record_t *rec, const char *class_name,
                                const char *name_pattern)
{
    if (class_name) {
        nmo_cli_record_str(rec, "filter_class", NULL, class_name);
    }
    if (name_pattern) {
        nmo_cli_record_str(rec, "filter_name", NULL, name_pattern);
    }
}

/* ============================================================================
 * nmo convert copy - Round-trip copy with save options
 * ============================================================================ */

int nmo_cmd_convert_copy(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        {"--compress",        NULL, NMO_OPT_STRING, "Compression level (0-9)"},
        {"--sequential-ids",  NULL, NMO_OPT_FLAG,   "Renumber object IDs sequentially"},
        {"--no-managers",     NULL, NMO_OPT_FLAG,   "Strip manager data"},
        {"--strip-resources", NULL, NMO_OPT_FLAG,   "Strip embedded resources"},
        {"--validate",        NULL, NMO_OPT_FLAG,   "Validate after copy"},
        NMO_OPT_DEF_FAST_SAVE,
    };
    enum { OPT_OUTPUT, OPT_COMPRESS, OPT_SEQIDS, OPT_NOMGR, OPT_STRIPRES, OPT_VALIDATE, OPT_FAST_SAVE, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path   = nmo_opt_str(&vals[OPT_OUTPUT]);
    const char *compress_str  = nmo_opt_str(&vals[OPT_COMPRESS]);
    bool sequential_ids       = nmo_opt_flag(&vals[OPT_SEQIDS]);
    bool no_managers          = nmo_opt_flag(&vals[OPT_NOMGR]);
    bool strip_resources      = nmo_opt_flag(&vals[OPT_STRIPRES]);
    bool validate             = nmo_opt_flag(&vals[OPT_VALIDATE]);
    bool fast_save            = nmo_opt_flag(&vals[OPT_FAST_SAVE]);

    if (!output_path) {
        fprintf(stderr, "Error: Output file not specified (use -o or --output)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Build save options */
    nmo_save_options_t save_opts = nmo_tool_owner_save_options_default();
    nmo_save_perf_stats_t save_phase_stats;
    nmo_save_perf_stats_reset(&save_phase_stats);
    save_opts.collect_perf_stats = true;
    save_opts.perf_stats = &save_phase_stats;

    if (compress_str) {
        int level = 0;
        if (!parse_compression_level(compress_str, &level)) {
            fprintf(stderr, "Error: Invalid compression level '%s' (must be 0-9)\n", compress_str);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        save_opts.compression_level = level;
        save_opts.flags |= NMO_SAVE_COMPRESSED;
    }

    if (sequential_ids) {
        save_opts.flags |= NMO_SAVE_SEQUENTIAL_IDS;
    }

    if (!no_managers) {
        save_opts.flags |= NMO_SAVE_INCLUDE_MANAGERS;
    }

    if (strip_resources) {
        save_opts.flags |= NMO_SAVE_STRIP_INCLUDED_FILES;
    }

    if (validate) {
        save_opts.flags |= NMO_SAVE_VALIDATE_BEFORE;
    }

    if (fast_save) {
        save_opts.durability = NMO_SAVE_DURABILITY_FAST;
    }

    /* Save file */
    int result = nmo_cli_save_document(c.document, output_path, &save_opts);
    if (result != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, result);
    }

    /* Output results */
    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (rec) {
        nmo_cli_record_str(rec, "input_file", NULL, c.file_path);
        nmo_cli_record_str(rec, "output_file", NULL, output_path);
        nmo_cli_record_uint(rec, "flags", NULL, save_opts.flags);
        nmo_cli_record_int(rec, "compression_level", NULL, save_opts.compression_level);
        nmo_cli_record_str(rec, "save_durability", NULL,
                           convert_save_durability_name(save_opts.durability));
        convert_add_saved_text(rec, output_path, save_opts.durability);
        convert_add_save_phase_stats(rec, &save_phase_stats);
    }

    return nmo_cmd_ctx_done(&c, nmo_cmd_ctx_emit_record(&c, rec, "convert.copy", 0, false));
}

/* ============================================================================
 * nmo convert version - Show/modify file version metadata
 * ============================================================================ */

static void convert_add_version_field(nmo_cli_record_t *rec, const char *key,
                                      const char *label, uint32_t value)
{
    nmo_cli_record_uint(rec, key, NULL, value);
    nmo_cli_record_raw_fmt(rec, "%-17s%u\n", label, value);
}

int nmo_cmd_convert_version(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        NMO_OPT_DEF_FAST_SAVE,
    };
    enum { OPT_VERSION_OUTPUT, OPT_VERSION_FAST_SAVE, OPT_VERSION_COUNT };
    nmo_opt_val_t vals[OPT_VERSION_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_VERSION_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_VERSION_OUTPUT]);
    bool fast_save = nmo_opt_flag(&vals[OPT_VERSION_FAST_SAVE]);

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Get file info */
    nmo_file_info_t info = nmo_document_get_file_info(c.document);

    /* If no output, just show version info */
    if (!output_path) {
        nmo_cli_record_t *rec = nmo_cli_record_new();
        if (rec) {
            convert_add_version_field(rec, "file_version", "File version:", info.file_version);
            convert_add_version_field(rec, "file_version2", "File version2:", info.file_version2);
            convert_add_version_field(rec, "ck_version", "CK version:", info.ck_version);
            convert_add_version_field(rec, "product_version", "Product version:", info.product_version);
            convert_add_version_field(rec, "product_build", "Product build:", info.product_build);
            convert_add_version_field(rec, "object_count", "Object count:", info.object_count);
            convert_add_version_field(rec, "manager_count", "Manager count:", info.manager_count);
        }

        return nmo_cmd_ctx_done(&c, nmo_cmd_ctx_emit_record(&c, rec, "convert.version", 0, false));
    }

    /* If output specified, save the file (equivalent to copy) */
    nmo_save_options_t save_opts = nmo_tool_owner_save_options_default();
    if (fast_save) {
        save_opts.durability = NMO_SAVE_DURABILITY_FAST;
    }
    int result = nmo_cli_save_document(c.document, output_path, &save_opts);
    if (result != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, result);
    }

    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (rec) {
        nmo_cli_record_str(rec, "input_file", NULL, c.file_path);
        nmo_cli_record_str(rec, "output_file", NULL, output_path);
        nmo_cli_record_str(rec, "save_durability", NULL,
                           convert_save_durability_name(save_opts.durability));
        convert_add_saved_text(rec, output_path, save_opts.durability);
    }

    return nmo_cmd_ctx_done(&c, nmo_cmd_ctx_emit_record(&c, rec, "convert.version", 0, false));
}

/* ============================================================================
 * nmo convert strip - Remove objects by class/name pattern
 * ============================================================================ */

static const nmo_cli_table_col_t convert_strip_columns[] = {
    {"ID", NMO_CLI_ALIGN_RIGHT, 5, 0},
    {"CLASS", NMO_CLI_ALIGN_LEFT, 20, 30},
    {"SIZE", NMO_CLI_ALIGN_RIGHT, 10, 0},
    {"NAME", NMO_CLI_ALIGN_LEFT, 20, 50},
};

/*
 * Add `objects` as the `key` array, shown as a table in text when non-empty.
 * Returns the summed chunk data size.
 */
static uint64_t convert_strip_add_objects(const nmo_cmd_ctx_t *c,
                                          nmo_cli_record_t *rec,
                                          const char *key,
                                          nmo_object_t *const *objects,
                                          size_t count,
                                          bool with_class_id)
{
    nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, key, NULL);
    if (arr && count > 0) {
        nmo_cli_record_array_set_table(
            arr, convert_strip_columns,
            sizeof(convert_strip_columns) / sizeof(convert_strip_columns[0]));
    }

    uint64_t total_size = 0;
    for (size_t i = 0; i < count; ++i) {
        nmo_object_t *obj = objects[i];
        nmo_class_id_t cid = nmo_object_get_class_id(obj);
        const char *cn = nmo_cli_class_name_from_id(c->ctx, cid);
        nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
        uint64_t sz = chunk ? (uint64_t)nmo_chunk_get_data_size(chunk) : 0;
        total_size += sz;

        nmo_cli_record_t *item = nmo_cli_record_new();
        if (!item) {
            continue;
        }
        nmo_cli_record_uint(item, "id", "ID", (uint64_t)nmo_object_get_id(obj));
        if (with_class_id) {
            nmo_cli_record_uint(item, "class_id", NULL, (uint64_t)cid);
        }
        if (cn) {
            nmo_cli_record_str(item, "class_name", "CLASS", cn);
        } else {
            nmo_cli_record_text(item, "CLASS", "-");
        }
        nmo_cli_record_uint(item, "size", "SIZE", sz);
        nmo_cli_record_str_opt(item, "name", "NAME", nmo_object_get_name(obj), "-");
        if (!nmo_cli_record_array_add(arr, item)) {
            nmo_cli_record_free(item);
        }
    }
    return total_size;
}

int nmo_cmd_convert_strip(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        NMO_OPT_DEF_OUTPUT,
        NMO_OPT_DEF_CLASS_FILTER,
        {"--name",    "-n", NMO_OPT_STRING, "Filter by name pattern"},
        {"--dry-run", NULL, NMO_OPT_FLAG,   "Preview without modifying"},
        NMO_OPT_DEF_FAST_SAVE,
    };
    enum { OPT_OUTPUT, OPT_CLASS, OPT_NAME, OPT_DRYRUN, OPT_FAST_SAVE, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path = nmo_opt_str(&vals[OPT_OUTPUT]);
    const char *class_name  = nmo_opt_str(&vals[OPT_CLASS]);
    const char *name_pattern = nmo_opt_str(&vals[OPT_NAME]);
    bool dry_run            = nmo_opt_flag(&vals[OPT_DRYRUN]);
    bool fast_save          = nmo_opt_flag(&vals[OPT_FAST_SAVE]);

    if (!output_path && !dry_run) {
        fprintf(stderr, "Error: Output file not specified (use -o or --output)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!class_name && !name_pattern) {
        fprintf(stderr, "Error: Must specify --class or --name filter\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    nmo_object_query_t class_query = {0};
    const nmo_object_query_t *class_filter = NULL;
    if (class_name) {
        nmo_status_t st =
            nmo_core_query_set_class_name(&c, &class_query, class_name, true);
        if (st != NMO_OK) {
            fprintf(stderr, "Warning: Unknown class '%s'\n", class_name);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_ARG_ERROR);
        }
        class_filter = &class_query;
    }

    nmo_object_query_t name_query = {0};
    const nmo_object_query_t *name_filter = NULL;
    if (name_pattern) {
        nmo_core_query_set_name_wildcard(&name_query, name_pattern);
        name_filter = &name_query;
    }

    nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);
    nmo_convert_strip_collect_t collect = {
        .class_filter = class_filter,
        .name_filter = name_filter,
    };
    rc = nmo_core_object_query_run(&c, NULL, convert_strip_collect_object,
                                   &collect, NULL);
    if (rc != NMO_CLI_EXIT_SUCCESS || collect.oom) {
        fprintf(stderr, "Error: Failed to collect removal list\n");
        free(collect.ids);
        free(collect.objects);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    nmo_object_id_t *ids_to_remove = collect.ids;
    nmo_object_t **matched_objects = collect.objects;
    size_t remove_count = collect.count;

    /* Dry-run: output preview and exit */
    if (dry_run) {
        /* Compute cascade impact via preview API */
        nmo_arena_t *preview_arena = nmo_arena_create(NULL, 0);
        nmo_object_id_t *expanded_ids = NULL;
        size_t expanded_count = 0;
        bool have_cascade = false;

        if (preview_arena && remove_count > 0) {
            int prev_rc = nmo_tool_owner_preview_destroy(
                c.workspace, ids_to_remove, remove_count,
                NMO_RUNTIME_REQUEST_CASCADE, preview_arena,
                &expanded_ids, &expanded_count);
            have_cascade = (prev_rc == NMO_OK && expanded_ids != NULL);
            if (prev_rc != NMO_OK) {
                fprintf(stderr, "Warning: Could not compute cascade impact\n");
            }
        }

        /* Partition expanded set into cascade-only IDs */
        nmo_object_t **cascade_objects = NULL;
        size_t cascade_count = 0;

        if (have_cascade && expanded_count > remove_count) {
            cascade_objects = (nmo_object_t **)malloc(
                expanded_count * sizeof(nmo_object_t *));
            if (!cascade_objects) {
                fprintf(stderr, "Warning: Could not allocate cascade display buffer\n");
            }
            if (cascade_objects) {
                for (size_t ei = 0; ei < expanded_count; ++ei) {
                    nmo_object_id_t eid = expanded_ids[ei];
                    /* Check if this ID was directly matched */
                    bool is_direct = false;
                    for (size_t di = 0; di < remove_count; ++di) {
                        if (ids_to_remove[di] == eid) {
                            is_direct = true;
                            break;
                        }
                    }
                    if (!is_direct) {
                        nmo_object_t *cobj =
                            nmo_object_repository_find_by_id(repo, eid);
                        if (cobj) {
                            cascade_objects[cascade_count++] = cobj;
                        }
                    }
                }
            }
        }

        nmo_cli_record_t *rec = nmo_cli_record_new();
        if (rec) {
            nmo_cli_record_bool(rec, "dry_run", NULL, true);
            nmo_cli_record_uint(rec, "match_count", NULL, (uint64_t)remove_count);
            nmo_cli_record_raw(rec, "=== Dry Run: Strip Preview ===\n\n");
            if (remove_count == 0) {
                nmo_cli_record_raw(rec, "No objects matched the filter.\n");
            } else {
                nmo_cli_record_raw_fmt(rec, "Matched %zu object(s):\n\n", remove_count);
            }

            uint64_t match_size = convert_strip_add_objects(
                &c, rec, "matches", matched_objects, remove_count, true);
            nmo_cli_record_uint(rec, "total_match_size", NULL, match_size);

            nmo_cli_record_uint(rec, "cascade_count", NULL, (uint64_t)cascade_count);
            if (cascade_count > 0) {
                nmo_cli_record_raw_fmt(rec, "\nCascade impact: %zu additional object(s):\n",
                                       cascade_count);
            }
            uint64_t cascade_size = convert_strip_add_objects(
                &c, rec, "cascade_objects", cascade_objects, cascade_count, false);
            nmo_cli_record_uint(rec, "cascade_size", NULL, cascade_size);
            nmo_cli_record_uint(rec, "total_size", NULL, match_size + cascade_size);
            if (remove_count > 0) {
                nmo_cli_record_raw_fmt(rec, "\nTotal: %zu objects, %" PRIu64 " bytes\n",
                                       remove_count + cascade_count,
                                       match_size + cascade_size);
            }
            convert_add_filters(rec, class_name, name_pattern);
        }
        rc = nmo_cmd_ctx_emit_record(&c, rec, "convert.strip", 0, c.colorize);

        free(cascade_objects);
        nmo_arena_destroy(preview_arena);
        free(ids_to_remove);
        free(matched_objects);
        return nmo_cmd_ctx_done(&c, rc);
    }

    free(matched_objects);

    /* Destroy matched objects */
    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));

    if (remove_count > 0) {
        int result = nmo_tool_owner_destroy_objects(c.workspace, ids_to_remove, remove_count,
                                                 NMO_RUNTIME_REQUEST_CASCADE, &report);
        if (result != NMO_OK) {
            fprintf(stderr, "Error destroying objects: %s\n", nmo_error_string(result));
            free(ids_to_remove);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
        }
    }

    free(ids_to_remove);

    /* Save file */
    nmo_save_options_t save_opts = nmo_tool_owner_save_options_default();
    if (fast_save) {
        save_opts.durability = NMO_SAVE_DURABILITY_FAST;
    }
    int result = nmo_cli_save_document(c.document, output_path, &save_opts);
    if (result != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, result);
    }

    /* Output results */
    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (rec) {
        nmo_cli_record_str(rec, "input_file", NULL, c.file_path);
        nmo_cli_record_str(rec, "output_file", NULL, output_path);
        nmo_cli_record_str(rec, "save_durability", NULL,
                           convert_save_durability_name(save_opts.durability));
        nmo_cli_record_uint(rec, "objects_removed", NULL, (uint64_t)report.deleted_objects);
        convert_add_filters(rec, class_name, name_pattern);
        nmo_cli_record_raw_fmt(rec, "Removed %zu object(s)\n", report.deleted_objects);
        convert_add_saved_text(rec, output_path, save_opts.durability);
    }

    return nmo_cmd_ctx_done(&c, nmo_cmd_ctx_emit_record(&c, rec, "convert.strip", 0, false));
}

/* ============================================================================
 * nmo convert merge - Merge objects from source into target
 * ============================================================================ */

int nmo_cmd_convert_merge(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Find input files (need two positional args) */
    const char *file_args[2] = {NULL, NULL};
    const char *const value_opts[] = {"-o", "--output"};
    size_t file_count = nmo_tool_find_file_args_ex(
        argc, argv, file_args, 2, value_opts, 2);

    if (file_count < 2) {
        fprintf(stderr, "Error: Need two input files (source and target)\n");
        fprintf(stderr, "Usage: nmo convert merge [options] -o <output> <source> <target>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *source_path = file_args[0];
    const char *target_path = file_args[1];

    /* Parse options */
    const char *output_path = nmo_tool_find_opt_value(argc, argv, "-o", "--output");
    if (!output_path) {
        fprintf(stderr, "Error: Output file not specified (use -o or --output)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    bool fast_save = nmo_tool_has_flag(argc, argv, "--fast-save", NULL);

    /* Use init_no_file since we manage two documents manually */
    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (rc) return rc;

    /* Load source file */
    nmo_context_t *src_ctx = NULL;
    nmo_document_t *src_document = NULL;
    nmo_workspace_t *src_workspace = NULL;
    char *open_error = NULL;
    if (!nmo_tool_open_document(source_path, &src_ctx, &src_document, &src_workspace,
                                &open_error)) {
        fprintf(stderr, "Error loading source file: %s\n",
                open_error ? open_error : "Failed to open file");
        free(open_error);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    /* Load target file */
    nmo_context_t *tgt_ctx = NULL;
    nmo_document_t *tgt_document = NULL;
    nmo_workspace_t *tgt_workspace = NULL;
    if (!nmo_tool_open_document(target_path, &tgt_ctx, &tgt_document, &tgt_workspace,
                                &open_error)) {
        fprintf(stderr, "Error loading target file: %s\n",
                open_error ? open_error : "Failed to open file");
        free(open_error);
        nmo_tool_close_document(src_ctx, src_document, src_workspace);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
    }

    /* Get all objects from source */
    nmo_cmd_ctx_t src_cmd;
    nmo_cmd_ctx_init_from_repl_document(
        &src_cmd, src_ctx, src_document, src_workspace, false);
    nmo_convert_id_collect_t src_collect = {0};
    if (nmo_core_object_query_run(&src_cmd, NULL, convert_collect_id,
                                  &src_collect, NULL) != NMO_CLI_EXIT_SUCCESS ||
        src_collect.oom) {
        fprintf(stderr, "Error: Failed to collect source object IDs\n");
        free(src_collect.ids);
        nmo_tool_close_document(src_ctx, src_document, src_workspace);
        nmo_tool_close_document(tgt_ctx, tgt_document, tgt_workspace);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    nmo_object_id_t *src_ids = src_collect.ids;
    size_t src_count = src_collect.count;

    /* Copy objects from source to target */
    nmo_runtime_report_t report;
    memset(&report, 0, sizeof(report));

    if (src_count > 0) {
        int result = nmo_tool_owner_copy_objects(
            tgt_workspace, src_ids, src_count, NMO_RUNTIME_REQUEST_DEFAULT, &report);
        if (result != NMO_OK) {
            fprintf(stderr, "Error copying objects: %s\n", nmo_error_string(result));
            free(src_ids);
            nmo_tool_close_document(src_ctx, src_document, src_workspace);
            nmo_tool_close_document(tgt_ctx, tgt_document, tgt_workspace);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_IO_ERROR);
        }
    }

    free(src_ids);

    /* Save target to output */
    nmo_save_options_t save_opts = nmo_tool_owner_save_options_default();
    if (fast_save) {
        save_opts.durability = NMO_SAVE_DURABILITY_FAST;
    }
    int result = nmo_cli_save_document(tgt_document, output_path, &save_opts);
    if (result != NMO_CLI_EXIT_SUCCESS) {
        nmo_tool_close_document(src_ctx, src_document, src_workspace);
        nmo_tool_close_document(tgt_ctx, tgt_document, tgt_workspace);
        return nmo_cmd_ctx_done(&c, result);
    }

    /* Output results */
    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (rec) {
        nmo_cli_record_str(rec, "source_file", NULL, source_path);
        nmo_cli_record_str(rec, "target_file", NULL, target_path);
        nmo_cli_record_str(rec, "output_file", NULL, output_path);
        nmo_cli_record_str(rec, "save_durability", NULL,
                           convert_save_durability_name(save_opts.durability));
        nmo_cli_record_uint(rec, "objects_copied", NULL, (uint64_t)report.copied_objects);
        nmo_cli_record_raw_fmt(rec, "Copied %zu object(s) from source to target\n",
                               report.copied_objects);
        convert_add_saved_text(rec, output_path, save_opts.durability);
    }

    /* The envelope names the source file, since no_file init leaves it unset */
    c.file_path = source_path;
    rc = nmo_cmd_ctx_emit_record(&c, rec, "convert.merge", 0, false);

    nmo_tool_close_document(src_ctx, src_document, src_workspace);
    nmo_tool_close_document(tgt_ctx, tgt_document, tgt_workspace);
    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * nmo convert export - Export selected objects to new NMO file
 * ============================================================================ */

/** Dynamic array for collecting objects (local to convert export) */
typedef struct {
    nmo_object_t **objects;
    size_t count;
    size_t capacity;
} convert_obj_collect_t;

static int convert_obj_collect_visitor(size_t index, nmo_object_t *obj,
                                       const nmo_cmd_ctx_t *c, void *user) {
    (void)index;
    (void)c;
    convert_obj_collect_t *col = (convert_obj_collect_t *)user;
    if (col->count >= col->capacity) {
        size_t new_cap = col->capacity ? col->capacity * 2 : 64;
        nmo_object_t **tmp = (nmo_object_t **)realloc(col->objects, new_cap * sizeof(*tmp));
        if (!tmp) return -1;
        col->objects = tmp;
        col->capacity = new_cap;
    }
    col->objects[col->count++] = obj;
    return 0;
}

static int id_cmp(const void *a, const void *b) {
    nmo_object_id_t ia = *(const nmo_object_id_t *)a;
    nmo_object_id_t ib = *(const nmo_object_id_t *)b;
    return (ia > ib) - (ia < ib);
}

static bool id_in_sorted(const nmo_object_id_t *arr, size_t count, nmo_object_id_t id) {
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] < id) lo = mid + 1;
        else if (arr[mid] > id) hi = mid;
        else return true;
    }
    return false;
}

int nmo_cmd_convert_export(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--output",   "-o", NMO_OPT_STRING, "Output file path (required)"},
        NMO_OPT_DEF_CLASS_FILTER,
        {"--name",     "-n", NMO_OPT_STRING, "Filter by name pattern"},
        {"--deps",     NULL, NMO_OPT_FLAG,   "Include transitive dependencies"},
        {"--all",      NULL, NMO_OPT_FLAG,   "Export all objects (no filter required)"},
        {"--dry-run",  NULL, NMO_OPT_FLAG,   "Preview matching objects without writing"},
        {"--compress", NULL, NMO_OPT_STRING, "Compression level (0-9)"},
        NMO_OPT_DEF_FAST_SAVE,
    };
    enum { OPT_OUTPUT, OPT_CLASS, OPT_NAME, OPT_DEPS,
           OPT_ALL, OPT_DRYRUN, OPT_COMPRESS, OPT_FAST_SAVE, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[16];
    nmo_opt_result_t r = NMO_OPT_RESULT(vals, pos);
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    const char *output_path      = nmo_opt_str(&vals[OPT_OUTPUT]);
    const char *class_filter_str = nmo_opt_str(&vals[OPT_CLASS]);
    const char *name_pattern     = nmo_opt_str(&vals[OPT_NAME]);
    bool include_deps            = nmo_opt_flag(&vals[OPT_DEPS]);
    bool export_all              = nmo_opt_flag(&vals[OPT_ALL]);
    bool dry_run                 = nmo_opt_flag(&vals[OPT_DRYRUN]);
    const char *compress_str     = nmo_opt_str(&vals[OPT_COMPRESS]);
    bool fast_save               = nmo_opt_flag(&vals[OPT_FAST_SAVE]);

    if (!dry_run && !output_path) {
        fprintf(stderr, "Error: -o/--output is required (or use --dry-run)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    if (!export_all && !class_filter_str && !name_pattern) {
        fprintf(stderr, "Error: At least one filter required (--class, --name, or --all)\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Validate compression level early (before any destructive session ops) */
    int compress_level = 0;
    if (compress_str) {
        if (!parse_compression_level(compress_str, &compress_level)) {
            fprintf(stderr, "Error: Invalid compression level '%s' (must be 0-9)\n", compress_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
    }

    nmo_cmd_ctx_t c;
    int rc = nmo_cmd_ctx_init(&c, argc, argv, global);
    if (rc) return rc;

    /* Build query */
    nmo_object_query_t query = {0};
    nmo_core_query_build_options_t query_opts = {
        .class_name = class_filter_str,
        .name_wildcard = name_pattern,
        .include_derived_classes = true,
    };
    rc = nmo_core_query_build(&c, &query, &query_opts);
    if (rc != NMO_CLI_EXIT_SUCCESS) {
        return nmo_cmd_ctx_done(&c, rc);
    }

    /* Collect matching objects */
    convert_obj_collect_t col = {0};
    nmo_core_iter_result_t iter_result = {0};
    nmo_core_object_query_run(&c, &query, convert_obj_collect_visitor, &col, &iter_result);

    if (col.count == 0) {
        fprintf(stderr, "No objects matched the filter.\n");
        free(col.objects);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
    }

    size_t seed_count = col.count;

    /* Resolve transitive dependencies if requested */
    nmo_object_t **final_objects = NULL;
    size_t final_count = 0;
    size_t dep_count = 0;

    if (include_deps) {
        nmo_object_repository_t *repo = nmo_tool_owner_repository(c.workspace);

        /* Extract and sort seed IDs for membership testing */
        nmo_object_id_t *seed_ids = (nmo_object_id_t *)malloc(seed_count * sizeof(nmo_object_id_t));
        if (!seed_ids) {
            fprintf(stderr, "Error: Out of memory\n");
            free(col.objects);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }
        for (size_t i = 0; i < seed_count; i++)
            seed_ids[i] = nmo_object_get_id(col.objects[i]);
        qsort(seed_ids, seed_count, sizeof(nmo_object_id_t), id_cmp);

        /* Get reference graph from session cache and compute transitive closure */
        nmo_ref_graph_t *graph = nmo_tool_owner_ref_graph(c.workspace);
        if (!graph) {
            fprintf(stderr, "Error: Failed to build reference graph\n");
            free(seed_ids);
            free(col.objects);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        /* Arena for mark_reachable results */
        nmo_arena_t *deps_arena = nmo_arena_create(NULL, 0);
        if (!deps_arena) {
            free(seed_ids);
            free(col.objects);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        nmo_object_id_t *reachable_ids = NULL;
        size_t reachable_count = 0;
        nmo_status_t ms = nmo_ref_graph_mark_reachable(
            graph, seed_ids, seed_count, deps_arena,
            &reachable_ids, &reachable_count);

        if (ms != NMO_OK) {
            fprintf(stderr, "Error: Failed to resolve dependencies\n");
            nmo_arena_destroy(deps_arena);
            free(seed_ids);
            free(col.objects);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        /* Build final array: dependencies first, then seeds */
        final_objects = (nmo_object_t **)malloc(reachable_count * sizeof(nmo_object_t *));
        if (!final_objects) {
            nmo_arena_destroy(deps_arena);
            free(seed_ids);
            free(col.objects);
            return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
        }

        /* Pass 1: dependency-only objects (reachable but not in seed set) */
        final_count = 0;
        for (size_t i = 0; i < reachable_count; i++) {
            if (!id_in_sorted(seed_ids, seed_count, reachable_ids[i])) {
                nmo_object_t *obj = nmo_object_repository_find_by_id(repo, reachable_ids[i]);
                if (obj)
                    final_objects[final_count++] = obj;
            }
        }
        dep_count = final_count;

        /* Pass 2: seed objects (preserving original match order) */
        for (size_t i = 0; i < seed_count; i++)
            final_objects[final_count++] = col.objects[i];

        nmo_arena_destroy(deps_arena);
        free(seed_ids);
    } else {
        final_objects = col.objects;
        final_count = col.count;
        col.objects = NULL; /* prevent double-free */
    }

    /* Dry-run: just list what would be exported */
    if (dry_run) {
        nmo_cli_record_t *rec = nmo_cli_record_new();
        if (rec) {
            nmo_cli_record_str(rec, "input_file", NULL, c.file_path);
            nmo_cli_record_bool(rec, "dry_run", NULL, true);
            nmo_cli_record_uint(rec, "matched", NULL, (uint64_t)seed_count);
            if (include_deps) {
                nmo_cli_record_bool(rec, "deps", NULL, true);
                nmo_cli_record_uint(rec, "deps_resolved", NULL, (uint64_t)dep_count);
            }
            nmo_cli_record_uint(rec, "total", NULL, (uint64_t)final_count);

            nmo_cli_record_raw_fmt(rec, "Dry run: would export %zu object(s)\n", final_count);
            if (include_deps && dep_count > 0)
                nmo_cli_record_raw_fmt(rec, "  Matched: %zu, Dependencies: %zu\n",
                                       seed_count, dep_count);
            nmo_cli_record_raw(rec, "\n");
            nmo_cli_record_raw_fmt(rec, "   %5s  %-20s  %10s  %-4s  %s\n",
                                   "ID", "CLASS", "SIZE", "DEP", "NAME");
            nmo_cli_record_raw_fmt(rec, "   %5s  %-20s  %10s  %-4s  %s\n",
                                   "-----", "--------------------", "----------", "----",
                                   "--------------------");

            nmo_cli_record_array_t *arr = nmo_cli_record_array(rec, "objects", NULL);
            nmo_cli_record_array_omit_heading(arr);
            for (size_t i = 0; i < final_count; i++) {
                nmo_object_t *obj = final_objects[i];
                nmo_cli_record_t *item = nmo_cli_record_new();
                if (!item) continue;
                nmo_chunk_t *chunk = nmo_object_get_chunk(obj);
                size_t sz = chunk ? nmo_chunk_get_data_size(chunk) : 0;
                const char *cn = nmo_core_class_name(&c, nmo_object_get_class_id(obj));
                const char *nm = nmo_object_get_name(obj);
                nmo_cli_record_uint(item, "id", NULL, (uint64_t)nmo_object_get_id(obj));
                if (cn) nmo_cli_record_str(item, "class_name", NULL, cn);
                nmo_cli_record_str_opt(item, "name", NULL, nm, NULL);
                nmo_cli_record_bool(item, "is_dep", NULL, i < dep_count);
                nmo_cli_record_set_summary_fmt(item, "   %5u  %-20s  %10zu  %-4s  %s",
                                               nmo_object_get_id(obj),
                                               cn ? cn : "?",
                                               sz,
                                               i < dep_count ? "yes" : "",
                                               nm ? nm : "");
                if (!nmo_cli_record_array_add(arr, item))
                    nmo_cli_record_free(item);
            }
        }
        rc = nmo_cmd_ctx_emit_record(&c, rec, "convert.export", 0, false);
        free(final_objects);
        free(col.objects);
        return nmo_cmd_ctx_done(&c, rc);
    }

    /* Build include list for saver filter (no repository mutation needed) */
    nmo_object_id_t *include_ids = (nmo_object_id_t *)malloc(final_count * sizeof(nmo_object_id_t));
    if (!include_ids) {
        free(final_objects);
        free(col.objects);
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_INTERNAL_ERROR);
    }
    for (size_t i = 0; i < final_count; i++)
        include_ids[i] = nmo_object_get_id(final_objects[i]);

    /* Save via saver pipeline with object filter */
    nmo_save_options_t save_opts = nmo_tool_owner_save_options_default();
    save_opts.include_ids = include_ids;
    save_opts.include_count = final_count;
    if (fast_save) {
        save_opts.durability = NMO_SAVE_DURABILITY_FAST;
    }
    if (compress_str) {
        save_opts.compression_level = compress_level;
        save_opts.flags |= NMO_SAVE_COMPRESSED;
    }

    int save_result = nmo_cli_save_document(c.document, output_path, &save_opts);
    if (save_result != NMO_CLI_EXIT_SUCCESS) {
        free(include_ids);
        free(final_objects);
        free(col.objects);
        return nmo_cmd_ctx_done(&c, save_result);
    }

    /* Output results */
    nmo_cli_record_t *rec = nmo_cli_record_new();
    if (rec) {
        nmo_cli_record_str(rec, "input_file", NULL, c.file_path);
        nmo_cli_record_str(rec, "output_file", NULL, output_path);
        nmo_cli_record_str(rec, "save_durability", NULL,
                           convert_save_durability_name(save_opts.durability));
        nmo_cli_record_uint(rec, "matched", NULL, (uint64_t)seed_count);
        if (include_deps) {
            nmo_cli_record_bool(rec, "deps", NULL, true);
            nmo_cli_record_uint(rec, "deps_resolved", NULL, (uint64_t)dep_count);
        }
        nmo_cli_record_uint(rec, "exported", NULL, (uint64_t)final_count);
        convert_add_filters(rec, class_filter_str, name_pattern);
        if (include_deps && dep_count > 0) {
            nmo_cli_record_raw_fmt(rec, "Matched %zu, resolved %zu dep(s), exported %zu object(s) to %s\n",
                                   seed_count, dep_count, final_count, output_path);
        } else {
            nmo_cli_record_raw_fmt(rec, "Exported %zu object(s) to %s\n", final_count, output_path);
        }
        nmo_cli_record_raw_fmt(rec, "Durability: %s\n",
                               convert_save_durability_name(save_opts.durability));
    }
    rc = nmo_cmd_ctx_emit_record(&c, rec, "convert.export", 0, false);

    free(include_ids);
    free(final_objects);
    free(col.objects);
    return nmo_cmd_ctx_done(&c, rc);
}

