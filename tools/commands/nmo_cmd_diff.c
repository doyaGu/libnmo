/**
 * @file nmo_cmd_diff.c
 * @brief CLI diff command group implementation
 */

#include "nmo_cmd_diff.h"
#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_output.h"
#include "../nmo_tool_common.h"
#include "nmo.h"
#include "app/nmo_session.h"
#include "app/nmo_comparison.h"
#include "app/nmo_context.h"
#include "app/nmo_inspector.h"
#include "app/nmo_object_diff.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/* Per-class object count entry for comparison breakdown */
typedef struct {
    nmo_class_id_t class_id;
    uint32_t count1;    /* count in file 1 */
    uint32_t count2;    /* count in file 2 */
} class_count_entry_t;

#define MAX_CLASS_ENTRIES 256

typedef struct {
    class_count_entry_t entries[MAX_CLASS_ENTRIES];
    size_t count;
} class_histogram_t;

static bool parse_float_01(const char *text, float *out) {
    if (!text || !out) return false;
    errno = 0;
    char *end = NULL;
    float v = strtof(text, &end);
    if (errno != 0 || end == text || (end && *end != '\0')) return false;
    if (v < 0.0f || v > 1.0f) return false;
    *out = v;
    return true;
}

static void class_histogram_init(class_histogram_t *h) {
    memset(h, 0, sizeof(*h));
}

static class_count_entry_t *class_histogram_find(class_histogram_t *h,
                                                  nmo_class_id_t class_id)
{
    for (size_t i = 0; i < h->count; i++) {
        if (h->entries[i].class_id == class_id) {
            return &h->entries[i];
        }
    }
    return NULL;
}

static class_count_entry_t *class_histogram_add(class_histogram_t *h,
                                                 nmo_class_id_t class_id)
{
    class_count_entry_t *e = class_histogram_find(h, class_id);
    if (e) return e;
    if (h->count >= MAX_CLASS_ENTRIES) return NULL;
    e = &h->entries[h->count++];
    e->class_id = class_id;
    e->count1 = 0;
    e->count2 = 0;
    return e;
}

/**
 * @brief Build a merged class histogram from two object repositories
 */
static void build_class_histogram(nmo_object_repository_t *repo1,
                                   nmo_object_repository_t *repo2,
                                   class_histogram_t *hist)
{
    class_histogram_init(hist);

    size_t n1 = 0, n2 = 0;
    nmo_object_t **objs1 = nmo_object_repository_get_all(repo1, &n1);
    nmo_object_t **objs2 = nmo_object_repository_get_all(repo2, &n2);

    for (size_t i = 0; i < n1; i++) {
        nmo_class_id_t cid = nmo_object_get_class_id(objs1[i]);
        class_count_entry_t *e = class_histogram_add(hist, cid);
        if (e) e->count1++;
    }
    for (size_t i = 0; i < n2; i++) {
        nmo_class_id_t cid = nmo_object_get_class_id(objs2[i]);
        class_count_entry_t *e = class_histogram_add(hist, cid);
        if (e) e->count2++;
    }
}

/* Sort comparison: entries with differences first, then by class_id */
static int class_entry_cmp(const void *a, const void *b) {
    const class_count_entry_t *ea = (const class_count_entry_t *)a;
    const class_count_entry_t *eb = (const class_count_entry_t *)b;
    bool da = (ea->count1 != ea->count2);
    bool db = (eb->count1 != eb->count2);
    if (da != db) return da ? -1 : 1; /* differences first */
    if (ea->count1 + ea->count2 != eb->count1 + eb->count2) {
        return (ea->count1 + ea->count2 > eb->count1 + eb->count2) ? -1 : 1;
    }
    return (int)ea->class_id - (int)eb->class_id;
}

/**
 * @brief Open two sessions for comparison
 * @return 0 on success, NMO_CLI_EXIT_IO_ERROR on failure
 */
static int open_two_sessions(const char *path1, const char *path2,
                             nmo_context_t **ctx1, nmo_session_t **ses1,
                             nmo_context_t **ctx2, nmo_session_t **ses2)
{
    char errbuf[256];

    /* Open first file */
    if (!nmo_tool_open_session(path1, ctx1, ses1, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error opening '%s': %s\n", path1, errbuf);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Open second file */
    if (!nmo_tool_open_session(path2, ctx2, ses2, errbuf, sizeof(errbuf))) {
        fprintf(stderr, "Error opening '%s': %s\n", path2, errbuf);
        nmo_tool_close_session(*ctx1, *ses1);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    return 0;
}

/**
 * @brief Get diff type name as string
 */
static const char *diff_type_name(nmo_diff_type_t type)
{
    switch (type) {
        case NMO_DIFF_NONE: return "none";
        case NMO_DIFF_OBJECT_COUNT: return "object_count";
        case NMO_DIFF_MANAGER_COUNT: return "manager_count";
        case NMO_DIFF_OBJECT_MISSING: return "object_missing";
        case NMO_DIFF_OBJECT_ORDER: return "object_order";
        case NMO_DIFF_OBJECT_ID: return "object_id";
        case NMO_DIFF_OBJECT_NAME: return "object_name";
        case NMO_DIFF_OBJECT_CLASS_ID: return "object_class_id";
        case NMO_DIFF_OBJECT_REFERENCE_FLAG: return "object_reference_flag";
        case NMO_DIFF_OBJECT_CHUNK_SIZE: return "object_chunk_size";
        case NMO_DIFF_OBJECT_CHUNK_DATA: return "object_chunk_data";
        case NMO_DIFF_MANAGER_MISSING: return "manager_missing";
        case NMO_DIFF_MANAGER_GUID: return "manager_guid";
        case NMO_DIFF_MANAGER_CHUNK_SIZE: return "manager_chunk_size";
        case NMO_DIFF_MANAGER_CHUNK_DATA: return "manager_chunk_data";
        case NMO_DIFF_FILE_VERSION: return "file_version";
        case NMO_DIFF_CK_VERSION: return "ck_version";
        case NMO_DIFF_SHADOW_DATA: return "shadow_data";
        default: return "unknown";
    }
}

/* ============================================================================
 * diff summary
 * ============================================================================ */

int nmo_cmd_diff_summary(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Parse file arguments */
    const char *paths[2];
    size_t path_count = nmo_tool_find_file_args(argc, argv, paths, 2);
    if (path_count < 2) {
        fprintf(stderr, "Error: Need two files to compare\n");
        fprintf(stderr, "Usage: nmo diff summary [options] <file1> <file2>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Parse flags */
    bool ignore_order = nmo_tool_has_flag(argc, argv, "--ignore-order", NULL);
    bool verbose = nmo_tool_has_flag(argc, argv, "--verbose", "-v");
    bool strict = nmo_tool_has_flag(argc, argv, "--strict", NULL);

    /* Open both sessions */
    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_session_t *ses1 = NULL, *ses2 = NULL;
    int open_result = open_two_sessions(paths[0], paths[1], &ctx1, &ses1, &ctx2, &ses2);
    if (open_result != 0) {
        return open_result;
    }

    /* Compare sessions */
    nmo_compare_flags_t flags = NMO_COMPARE_STRUCTURE | NMO_COMPARE_FILE_INFO;
    if (ignore_order) {
        flags |= NMO_COMPARE_IGNORE_ORDER;
    }
    if (verbose) {
        flags |= NMO_COMPARE_VERBOSE;
    }

    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    nmo_status_t cmp_result = nmo_session_compare(ses1, ses2, flags, &result);

    if (cmp_result != NMO_OK) {
        fprintf(stderr, "Error: Comparison failed with code %d\n", cmp_result);
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Get file info from both sessions */
    nmo_file_info_t info1 = nmo_session_get_file_info(ses1);
    nmo_file_info_t info2 = nmo_session_get_file_info(ses2);

    /* Build per-class breakdown */
    nmo_object_repository_t *repo1 = nmo_session_get_repository(ses1);
    nmo_object_repository_t *repo2 = nmo_session_get_repository(ses2);
    class_histogram_t hist;
    build_class_histogram(repo1, repo2, &hist);
    qsort(hist.entries, hist.count, sizeof(class_count_entry_t), class_entry_cmp);

    /* Output */
    nmo_cmd_ctx_t c;
    int out_rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (out_rc) {
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        return out_rc;
    }

    if (c.is_json) {
        /* JSON output */
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file1", paths[0]);
        yyjson_mut_obj_add_str(doc, data, "file2", paths[1]);
        yyjson_mut_obj_add_bool(doc, data, "identical", result.match != 0);
        yyjson_mut_obj_add_int(doc, data, "diff_count", result.diff_count);
        yyjson_mut_obj_add_uint(doc, data, "objects_compared", result.objects_compared);
        yyjson_mut_obj_add_uint(doc, data, "objects_matched", result.objects_matched);

        /* File info */
        yyjson_mut_val *file1_info = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, file1_info, "object_count", info1.object_count);
        yyjson_mut_obj_add_uint(doc, file1_info, "manager_count", info1.manager_count);
        yyjson_mut_obj_add_uint(doc, file1_info, "file_version", info1.file_version);
        yyjson_mut_obj_add_uint(doc, file1_info, "ck_version", info1.ck_version);
        yyjson_mut_obj_add_val(doc, data, "file1_info", file1_info);

        yyjson_mut_val *file2_info = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, file2_info, "object_count", info2.object_count);
        yyjson_mut_obj_add_uint(doc, file2_info, "manager_count", info2.manager_count);
        yyjson_mut_obj_add_uint(doc, file2_info, "file_version", info2.file_version);
        yyjson_mut_obj_add_uint(doc, file2_info, "ck_version", info2.ck_version);
        yyjson_mut_obj_add_val(doc, data, "file2_info", file2_info);

        /* Per-class breakdown */
        if (hist.count > 0) {
            yyjson_mut_val *classes = yyjson_mut_arr(doc);
            for (size_t i = 0; i < hist.count; i++) {
                class_count_entry_t *e = &hist.entries[i];
                yyjson_mut_val *cls = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, cls, "class_id", e->class_id);
                const char *cname = nmo_cli_class_name_from_id(ctx1, e->class_id);
                if (!cname) cname = nmo_cli_class_name_from_id(ctx2, e->class_id);
                nmo_cli_json_add_str_safe(doc, cls, "class_name", cname);
                yyjson_mut_obj_add_uint(doc, cls, "count1", e->count1);
                yyjson_mut_obj_add_uint(doc, cls, "count2", e->count2);
                if (e->count1 != e->count2) {
                    yyjson_mut_obj_add_sint(doc, cls, "delta",
                        (int64_t)e->count2 - (int64_t)e->count1);
                }
                yyjson_mut_arr_append(classes, cls);
            }
            yyjson_mut_obj_add_val(doc, data, "class_breakdown", classes);
        }

        /* Differences */
        if (result.diff_count > 0) {
            yyjson_mut_val *diffs = yyjson_mut_arr(doc);
            for (int i = 0; i < result.diff_count; i++) {
                yyjson_mut_val *diff = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_str(doc, diff, "type", diff_type_name(result.diffs[i].type));
                yyjson_mut_obj_add_uint(doc, diff, "object_id", result.diffs[i].object_id);
                nmo_cli_json_add_str_safe(doc, diff, "context", result.diffs[i].context);
                yyjson_mut_arr_append(diffs, diff);
            }
            yyjson_mut_obj_add_val(doc, data, "diffs", diffs);
        }

        nmo_cli_json_write_enveloped_and_free(doc, data, "diff.summary", paths[0], c.out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        /* Text output */
        nmo_cli_print_heading(c.out, "Diff Summary", c.colorize);

        char buf[128];
        snprintf(buf, sizeof(buf), "%s", paths[0]);
        nmo_cli_print_kv(c.out, "File 1", buf, 18, c.colorize);
        snprintf(buf, sizeof(buf), "%s", paths[1]);
        nmo_cli_print_kv(c.out, "File 2", buf, 18, c.colorize);

        if (result.match) {
            fprintf(c.out, "\n%sFiles are identical%s\n",
                    c.colorize ? NMO_CLI_COLOR_GREEN : "",
                    c.colorize ? NMO_CLI_COLOR_RESET : "");
        } else {
            fprintf(c.out, "\n%sDifferences found: %d%s\n",
                    c.colorize ? NMO_CLI_COLOR_YELLOW : "",
                    result.diff_count,
                    c.colorize ? NMO_CLI_COLOR_RESET : "");
        }

        fprintf(c.out, "\n");
        snprintf(buf, sizeof(buf), "%u / %u", info1.object_count, info2.object_count);
        nmo_cli_print_kv(c.out, "Object count", buf, 18, c.colorize);
        snprintf(buf, sizeof(buf), "%u / %u", info1.manager_count, info2.manager_count);
        nmo_cli_print_kv(c.out, "Manager count", buf, 18, c.colorize);
        snprintf(buf, sizeof(buf), "0x%08X / 0x%08X", info1.ck_version, info2.ck_version);
        nmo_cli_print_kv(c.out, "CK version", buf, 18, c.colorize);

        /* Per-class breakdown */
        if (hist.count > 0) {
            fprintf(c.out, "\n");
            nmo_cli_print_heading(c.out, "Class Breakdown", c.colorize);
            fprintf(c.out, "  %-28s %6s  %6s  %s\n", "Class", "File 1", "File 2", "Delta");
            fprintf(c.out, "  %-28s %6s  %6s  %s\n",
                    "----------------------------", "------", "------", "-----");
            for (size_t i = 0; i < hist.count; i++) {
                class_count_entry_t *e = &hist.entries[i];
                const char *cname = nmo_cli_class_name_from_id(ctx1, e->class_id);
                if (!cname) cname = nmo_cli_class_name_from_id(ctx2, e->class_id);
                char name_buf[32];
                if (!cname) {
                    snprintf(name_buf, sizeof(name_buf), "class#%u", e->class_id);
                    cname = name_buf;
                }
                int delta = (int)e->count2 - (int)e->count1;
                if (delta == 0) {
                    fprintf(c.out, "  %-28s %6u  %6u\n",
                            cname, e->count1, e->count2);
                } else {
                    fprintf(c.out, "  %-28s %6u  %6u  %s%+d%s\n",
                            cname, e->count1, e->count2,
                            c.colorize ? (delta > 0 ? NMO_CLI_COLOR_GREEN
                                                  : NMO_CLI_COLOR_RED) : "",
                            delta,
                            c.colorize ? NMO_CLI_COLOR_RESET : "");
                }
            }
        }

        if (verbose && result.diff_count > 0) {
            fprintf(c.out, "\n");
            nmo_cli_print_heading(c.out, "Differences", c.colorize);
            for (int i = 0; i < result.diff_count; i++) {
                fprintf(c.out, "  [%s] %s\n",
                        diff_type_name(result.diffs[i].type),
                        result.diffs[i].context);
            }
        }
    }

    nmo_tool_close_session(ctx1, ses1);
    nmo_tool_close_session(ctx2, ses2);

    /* Return strict failure if requested and diffs found */
    if (strict && !result.match) {
        return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_STRICT_FAILURE);
    }

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * diff objects �?thin CLI wrapper over nmo_diff_objects() library API
 * ============================================================================ */

int nmo_cmd_diff_objects(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Parse file arguments */
    const char *paths[2] = {0};
    size_t path_count = 0;
    for (int i = 1; i < argc && path_count < 2; ++i) {
        const char *arg = argv[i];
        if (arg[0] == '-') {
            if (strcmp(arg, "--max-objects") == 0 ||
                strcmp(arg, "--max-fields") == 0 ||
                strcmp(arg, "--min-similarity") == 0 ||
                strcmp(arg, "--rename-similarity") == 0 ||
                strcmp(arg, "--format") == 0 ||
                strcmp(arg, "-f") == 0) {
                ++i; /* skip option value */
            }
            continue;
        }
        paths[path_count++] = arg;
    }
    if (path_count < 2) {
        fprintf(stderr, "Error: Need two files to compare\n");
        fprintf(stderr, "Usage: nmo diff objects [options] <file1> <file2>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Parse options */
    uint32_t max_objects = 0; /* 0 = unlimited */
    uint32_t max_fields = 0;  /* 0 = unlimited */
    float min_similarity = -1.0f;
    float rename_similarity = -1.0f;
    const char *max_objects_opt = nmo_tool_find_opt_value(argc, argv, "--max-objects", NULL);
    const char *max_fields_opt = nmo_tool_find_opt_value(argc, argv, "--max-fields", NULL);
    const char *min_similarity_opt = nmo_tool_find_opt_value(argc, argv, "--min-similarity", NULL);
    const char *rename_similarity_opt = nmo_tool_find_opt_value(argc, argv, "--rename-similarity", NULL);
    if (max_objects_opt && !nmo_tool_parse_u32(max_objects_opt, &max_objects)) {
        fprintf(stderr, "Error: Invalid --max-objects value: %s\n", max_objects_opt);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (max_fields_opt && !nmo_tool_parse_u32(max_fields_opt, &max_fields)) {
        fprintf(stderr, "Error: Invalid --max-fields value: %s\n", max_fields_opt);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (min_similarity_opt && !parse_float_01(min_similarity_opt, &min_similarity)) {
        fprintf(stderr, "Error: Invalid --min-similarity value: %s (expect 0..1)\n", min_similarity_opt);
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (rename_similarity_opt && !parse_float_01(rename_similarity_opt, &rename_similarity)) {
        fprintf(stderr, "Error: Invalid --rename-similarity value: %s (expect 0..1)\n", rename_similarity_opt);
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Open both sessions */
    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_session_t *ses1 = NULL, *ses2 = NULL;
    int open_result = open_two_sessions(paths[0], paths[1], &ctx1, &ses1, &ctx2, &ses2);
    if (open_result != 0) return open_result;

    /* Run library-level diff engine */
    nmo_diff_config_t cfg = nmo_diff_config_default();
    cfg.max_objects = max_objects;
    cfg.max_fields = max_fields;
    if (min_similarity >= 0.0f) cfg.min_similarity = min_similarity;
    if (rename_similarity >= 0.0f) cfg.rename_similarity = rename_similarity;

    nmo_diff_result_t diff;
    nmo_status_t st = nmo_diff_objects(ctx1, ses1, ctx2, ses2, &cfg, &diff);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Diff engine failed with code %d\n", st);
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* ---- Output ---- */
    nmo_cmd_ctx_t c;
    int out_rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (out_rc) {
        nmo_diff_result_destroy(&diff);
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        return out_rc;
    }

    if (c.is_json) {
        /* ---- JSON output ---- */
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file1", paths[0]);
        yyjson_mut_obj_add_str(doc, data, "file2", paths[1]);
        yyjson_mut_obj_add_uint(doc, data, "objects_file1", (uint64_t)diff.total_objects1);
        yyjson_mut_obj_add_uint(doc, data, "objects_file2", (uint64_t)diff.total_objects2);
        yyjson_mut_obj_add_real(doc, data, "min_similarity", cfg.min_similarity);
        yyjson_mut_obj_add_real(doc, data, "rename_similarity", cfg.rename_similarity);
        yyjson_mut_obj_add_uint(doc, data, "identical_count", (uint64_t)diff.identical_count);

        /* Changed objects */
        yyjson_mut_val *changed_arr = yyjson_mut_arr(doc);
        size_t emitted = 0;
        for (size_t i = 0; i < diff.changed_count; i++) {
            if (max_objects > 0 && emitted >= max_objects) break;
            const nmo_object_diff_t *od = &diff.changed[i];
            char path_buf[256];
            nmo_object_format_path(path_buf, sizeof(path_buf), ctx1, od->obj1);

            yyjson_mut_val *obj_val = yyjson_mut_obj(doc);
            nmo_cli_json_add_str_safe(doc, obj_val, "path", path_buf);
            yyjson_mut_obj_add_uint(doc, obj_val, "changed_fields",
                                    (uint64_t)od->field_diff_total);

            yyjson_mut_val *field_arr = yyjson_mut_arr(doc);
            for (size_t fi = 0; fi < od->field_diff_count; fi++) {
                const nmo_field_diff_t *fd = &od->field_diffs[fi];
                yyjson_mut_val *field_obj = yyjson_mut_obj(doc);
                nmo_cli_json_add_str_safe(doc, field_obj, "field", fd->field_name);
                nmo_cli_json_add_str_safe(doc, field_obj, "before", fd->before);
                nmo_cli_json_add_str_safe(doc, field_obj, "after", fd->after);
                yyjson_mut_arr_add_val(field_arr, field_obj);
            }
            yyjson_mut_obj_add_val(doc, obj_val, "fields", field_arr);
            yyjson_mut_arr_add_val(changed_arr, obj_val);
            emitted++;
        }
        yyjson_mut_obj_add_val(doc, data, "changed", changed_arr);
        yyjson_mut_obj_add_uint(doc, data, "changed_count", (uint64_t)diff.changed_count);

        /* Renamed objects */
        yyjson_mut_val *renamed_arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < diff.renamed_count; i++) {
            const nmo_rename_diff_t *rd = &diff.renamed[i];
            char before_path[256];
            char after_path[256];
            nmo_object_format_path(before_path, sizeof(before_path), ctx1, rd->obj1);
            nmo_object_format_path(after_path, sizeof(after_path), ctx2, rd->obj2);
            yyjson_mut_val *v = yyjson_mut_obj(doc);
            nmo_cli_json_add_str_safe(doc, v, "before", before_path);
            nmo_cli_json_add_str_safe(doc, v, "after", after_path);
            nmo_cli_json_add_str_safe(doc, v, "before_name", rd->before_name);
            nmo_cli_json_add_str_safe(doc, v, "after_name", rd->after_name);
            yyjson_mut_obj_add_real(doc, v, "similarity", rd->similarity);
            yyjson_mut_arr_add_val(renamed_arr, v);
        }
        yyjson_mut_obj_add_val(doc, data, "renamed", renamed_arr);
        yyjson_mut_obj_add_uint(doc, data, "renamed_count", (uint64_t)diff.renamed_count);

        /* Removed objects */
        yyjson_mut_val *removed_arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < diff.removed_count; i++) {
            char path_buf[256];
            nmo_object_format_path(path_buf, sizeof(path_buf), ctx1, diff.removed[i]);
            yyjson_mut_val *v = yyjson_mut_strcpy(doc, path_buf);
            if (v) yyjson_mut_arr_append(removed_arr, v);
        }
        yyjson_mut_obj_add_val(doc, data, "removed", removed_arr);
        yyjson_mut_obj_add_uint(doc, data, "removed_count", (uint64_t)diff.removed_count);

        /* Added objects */
        yyjson_mut_val *added_arr = yyjson_mut_arr(doc);
        for (size_t i = 0; i < diff.added_count; i++) {
            char path_buf[256];
            nmo_object_format_path(path_buf, sizeof(path_buf), ctx2, diff.added[i]);
            yyjson_mut_val *v = yyjson_mut_strcpy(doc, path_buf);
            if (v) yyjson_mut_arr_append(added_arr, v);
        }
        yyjson_mut_obj_add_val(doc, data, "added", added_arr);
        yyjson_mut_obj_add_uint(doc, data, "added_count", (uint64_t)diff.added_count);

        nmo_cli_json_write_enveloped_and_free(doc, data, "diff.objects", paths[0], c.out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        /* ---- Text output: Git-style unified diff ---- */
        fprintf(c.out, "%sdiff a/%s b/%s%s\n",
                c.colorize ? "\x1b[1m" : "",  /* bold */
                paths[0], paths[1],
                c.colorize ? "\x1b[0m" : "");

        size_t emitted = 0;
        for (size_t i = 0; i < diff.changed_count; i++) {
            if (max_objects > 0 && emitted >= max_objects) break;
            const nmo_object_diff_t *od = &diff.changed[i];

            char path_a[256], path_b[256];
            nmo_object_format_path(path_a, sizeof(path_a), ctx1, od->obj1);
            nmo_object_format_path(path_b, sizeof(path_b), ctx2, od->obj2);

            /* Emit unified diff header for this object */
            fprintf(c.out, "\n%s--- a/%s%s\n",
                    c.colorize ? NMO_CLI_COLOR_RED : "",
                    path_a,
                    c.colorize ? NMO_CLI_COLOR_RESET : "");
            fprintf(c.out, "%s+++ b/%s%s\n",
                    c.colorize ? NMO_CLI_COLOR_GREEN : "",
                    path_b,
                    c.colorize ? NMO_CLI_COLOR_RESET : "");

            for (size_t fi = 0; fi < od->field_diff_count; fi++) {
                const nmo_field_diff_t *fd = &od->field_diffs[fi];
                fprintf(c.out, "%s-  %-24s : %s%s\n",
                        c.colorize ? NMO_CLI_COLOR_RED : "",
                        fd->field_name, fd->before,
                        c.colorize ? NMO_CLI_COLOR_RESET : "");
                fprintf(c.out, "%s+  %-24s : %s%s\n",
                        c.colorize ? NMO_CLI_COLOR_GREEN : "",
                        fd->field_name, fd->after,
                        c.colorize ? NMO_CLI_COLOR_RESET : "");
            }
            /* Truncation notice */
            if (od->field_diff_total > od->field_diff_count) {
                fprintf(c.out, "%s   ... and %zu more field(s)%s\n",
                        c.colorize ? NMO_CLI_COLOR_YELLOW : "",
                        od->field_diff_total - od->field_diff_count,
                        c.colorize ? NMO_CLI_COLOR_RESET : "");
            }
            emitted++;
        }

        /* List renamed objects */
        if (diff.renamed_count > 0) {
            fprintf(c.out, "\n%sRenamed (%zu):%s\n",
                    c.colorize ? NMO_CLI_COLOR_CYAN : "",
                    diff.renamed_count,
                    c.colorize ? NMO_CLI_COLOR_RESET : "");
            for (size_t i = 0; i < diff.renamed_count; i++) {
                char before_path[256];
                char after_path[256];
                const nmo_rename_diff_t *rd = &diff.renamed[i];
                nmo_object_format_path(before_path, sizeof(before_path), ctx1, rd->obj1);
                nmo_object_format_path(after_path, sizeof(after_path), ctx2, rd->obj2);
                fprintf(c.out, "  %s -> %s (sim=%.3f)\n",
                        before_path, after_path, rd->similarity);
            }
        }

        /* List removed objects */
        if (diff.removed_count > 0) {
            fprintf(c.out, "\n%sRemoved (%zu):%s\n",
                    c.colorize ? NMO_CLI_COLOR_RED : "",
                    diff.removed_count,
                    c.colorize ? NMO_CLI_COLOR_RESET : "");
            for (size_t i = 0; i < diff.removed_count; i++) {
                char path_buf[256];
                nmo_object_format_path(path_buf, sizeof(path_buf), ctx1, diff.removed[i]);
                fprintf(c.out, "%s  - %s%s\n",
                        c.colorize ? NMO_CLI_COLOR_RED : "",
                        path_buf,
                        c.colorize ? NMO_CLI_COLOR_RESET : "");
            }
        }

        /* List added objects */
        if (diff.added_count > 0) {
            fprintf(c.out, "\n%sAdded (%zu):%s\n",
                    c.colorize ? NMO_CLI_COLOR_GREEN : "",
                    diff.added_count,
                    c.colorize ? NMO_CLI_COLOR_RESET : "");
            for (size_t i = 0; i < diff.added_count; i++) {
                char path_buf[256];
                nmo_object_format_path(path_buf, sizeof(path_buf), ctx2, diff.added[i]);
                fprintf(c.out, "%s  + %s%s\n",
                        c.colorize ? NMO_CLI_COLOR_GREEN : "",
                        path_buf,
                        c.colorize ? NMO_CLI_COLOR_RESET : "");
            }
        }

        /* Summary */
        if (diff.changed_count == 0 && diff.renamed_count == 0 &&
            diff.removed_count == 0 && diff.added_count == 0) {
            fprintf(c.out, "\n%sFiles are identical%s\n",
                    c.colorize ? NMO_CLI_COLOR_GREEN : "",
                    c.colorize ? NMO_CLI_COLOR_RESET : "");
        } else {
            fprintf(c.out, "\n%zu object(s) changed, %zu renamed, %zu removed, %zu added"
                    " (%zu identical)\n",
                    diff.changed_count, diff.renamed_count,
                    diff.removed_count, diff.added_count, diff.identical_count);
            if (max_objects > 0 && diff.changed_count > max_objects) {
                fprintf(c.out, "%s(showing first %u of %zu changed)%s\n",
                        c.colorize ? NMO_CLI_COLOR_YELLOW : "",
                        max_objects, diff.changed_count,
                        c.colorize ? NMO_CLI_COLOR_RESET : "");
            }
        }
    }

    /* Cleanup */
    nmo_diff_result_destroy(&diff);
    nmo_tool_close_session(ctx1, ses1);
    nmo_tool_close_session(ctx2, ses2);

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * diff chunks
 * ============================================================================ */

int nmo_cmd_diff_chunks(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Parse file arguments */
    const char *paths[2];
    size_t path_count = nmo_tool_find_file_args(argc, argv, paths, 2);
    if (path_count < 2) {
        fprintf(stderr, "Error: Need two files to compare\n");
        fprintf(stderr, "Usage: nmo diff chunks [options] <file1> <file2>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Parse options */
    const char *object_id_str = nmo_tool_find_opt_value(argc, argv, "--object", "-o");
    uint32_t object_id = 0;
    bool specific_object = false;

    if (object_id_str) {
        if (!nmo_tool_parse_u32(object_id_str, &object_id)) {
            fprintf(stderr, "Error: Invalid object ID: %s\n", object_id_str);
            return NMO_CLI_EXIT_ARG_ERROR;
        }
        specific_object = true;
    }

    /* Open both sessions */
    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_session_t *ses1 = NULL, *ses2 = NULL;
    int open_result = open_two_sessions(paths[0], paths[1], &ctx1, &ses1, &ctx2, &ses2);
    if (open_result != 0) {
        return open_result;
    }

    /* Compare chunks */
    nmo_compare_flags_t flags = NMO_COMPARE_CHUNKS | NMO_COMPARE_IDS;
    if (specific_object) {
        /* When comparing a specific object, we'll use the general comparison
         * but filter results in output */
    }

    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    nmo_status_t cmp_result = nmo_session_compare(ses1, ses2, flags, &result);

    if (cmp_result != NMO_OK) {
        fprintf(stderr, "Error: Comparison failed with code %d\n", cmp_result);
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Output */
    nmo_cmd_ctx_t c;
    int out_rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (out_rc) {
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        return out_rc;
    }

    if (c.is_json) {
        /* JSON output */
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file1", paths[0]);
        yyjson_mut_obj_add_str(doc, data, "file2", paths[1]);
        if (specific_object) {
            yyjson_mut_obj_add_uint(doc, data, "object_id", object_id);
        }
        yyjson_mut_obj_add_bool(doc, data, "identical", result.match != 0);
        yyjson_mut_obj_add_int(doc, data, "diff_count", result.diff_count);

        /* Chunk differences (filter by object_id if specified) */
        if (result.diff_count > 0) {
            yyjson_mut_val *diffs = yyjson_mut_arr(doc);
            for (int i = 0; i < result.diff_count; i++) {
                if (specific_object && result.diffs[i].object_id != object_id) {
                    continue;
                }

                /* Only include chunk-related diffs */
                nmo_diff_type_t type = result.diffs[i].type;
                if (type != NMO_DIFF_OBJECT_CHUNK_SIZE &&
                    type != NMO_DIFF_OBJECT_CHUNK_DATA) {
                    continue;
                }

                yyjson_mut_val *diff = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_str(doc, diff, "type", diff_type_name(type));
                yyjson_mut_obj_add_uint(doc, diff, "object_id", result.diffs[i].object_id);
                nmo_cli_json_add_str_safe(doc, diff, "context", result.diffs[i].context);
                yyjson_mut_arr_append(diffs, diff);
            }
            yyjson_mut_obj_add_val(doc, data, "diffs", diffs);
        }

        nmo_cli_json_write_enveloped_and_free(doc, data, "diff.chunks", paths[0], c.out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        /* Text output */
        nmo_cli_print_heading(c.out, "Chunk Comparison", c.colorize);

        char buf[128];
        snprintf(buf, sizeof(buf), "%s", paths[0]);
        nmo_cli_print_kv(c.out, "File 1", buf, 18, c.colorize);
        snprintf(buf, sizeof(buf), "%s", paths[1]);
        nmo_cli_print_kv(c.out, "File 2", buf, 18, c.colorize);

        if (specific_object) {
            snprintf(buf, sizeof(buf), "%u", object_id);
            nmo_cli_print_kv(c.out, "Object ID", buf, 18, c.colorize);
        }

        /* Count chunk-specific diffs */
        int chunk_diff_count = 0;
        for (int i = 0; i < result.diff_count; i++) {
            if (specific_object && result.diffs[i].object_id != object_id) {
                continue;
            }
            nmo_diff_type_t type = result.diffs[i].type;
            if (type == NMO_DIFF_OBJECT_CHUNK_SIZE ||
                type == NMO_DIFF_OBJECT_CHUNK_DATA) {
                chunk_diff_count++;
            }
        }

        if (chunk_diff_count == 0) {
            fprintf(c.out, "\n%sChunks are identical%s\n",
                    c.colorize ? NMO_CLI_COLOR_GREEN : "",
                    c.colorize ? NMO_CLI_COLOR_RESET : "");
        } else {
            fprintf(c.out, "\n%sChunk differences: %d%s\n",
                    c.colorize ? NMO_CLI_COLOR_YELLOW : "",
                    chunk_diff_count,
                    c.colorize ? NMO_CLI_COLOR_RESET : "");

            fprintf(c.out, "\n");
            nmo_cli_print_heading(c.out, "Differences", c.colorize);
            for (int i = 0; i < result.diff_count; i++) {
                if (specific_object && result.diffs[i].object_id != object_id) {
                    continue;
                }
                nmo_diff_type_t type = result.diffs[i].type;
                if (type == NMO_DIFF_OBJECT_CHUNK_SIZE ||
                    type == NMO_DIFF_OBJECT_CHUNK_DATA) {
                    fprintf(c.out, "  [%s] %s\n",
                            diff_type_name(type),
                            result.diffs[i].context);
                }
            }
        }
    }

    nmo_tool_close_session(ctx1, ses1);
    nmo_tool_close_session(ctx2, ses2);

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

/* ============================================================================
 * diff full
 * ============================================================================ */

int nmo_cmd_diff_full(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    /* Parse file arguments */
    const char *paths[2];
    size_t path_count = nmo_tool_find_file_args(argc, argv, paths, 2);
    if (path_count < 2) {
        fprintf(stderr, "Error: Need two files to compare\n");
        fprintf(stderr, "Usage: nmo diff full [options] <file1> <file2>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    /* Parse flags */
    bool ignore_order = nmo_tool_has_flag(argc, argv, "--ignore-order", NULL);

    /* Open both sessions */
    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_session_t *ses1 = NULL, *ses2 = NULL;
    int open_result = open_two_sessions(paths[0], paths[1], &ctx1, &ses1, &ctx2, &ses2);
    if (open_result != 0) {
        return open_result;
    }

    /* Full comparison with all flags */
    nmo_compare_flags_t flags = NMO_COMPARE_STRUCTURE | NMO_COMPARE_IDS |
                                NMO_COMPARE_NAMES | NMO_COMPARE_CLASS_IDS |
                                NMO_COMPARE_CHUNKS | NMO_COMPARE_SHADOW |
                                NMO_COMPARE_MANAGERS | NMO_COMPARE_FILE_INFO |
                                NMO_COMPARE_VERBOSE;
    if (ignore_order) {
        flags |= NMO_COMPARE_IGNORE_ORDER;
    }

    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    nmo_status_t cmp_result = nmo_session_compare(ses1, ses2, flags, &result);

    if (cmp_result != NMO_OK) {
        fprintf(stderr, "Error: Comparison failed with code %d\n", cmp_result);
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Generate detailed report */
    nmo_comparison_result_format_report(&result);

    /* Output */
    nmo_cmd_ctx_t c;
    int out_rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (out_rc) {
        nmo_tool_close_session(ctx1, ses1);
        nmo_tool_close_session(ctx2, ses2);
        return out_rc;
    }

    if (c.is_json) {
        /* JSON output */
        yyjson_mut_doc *doc = nmo_cmd_ctx_json_begin(&c);
        yyjson_mut_val *data = yyjson_mut_obj(doc);

        yyjson_mut_obj_add_str(doc, data, "file1", paths[0]);
        yyjson_mut_obj_add_str(doc, data, "file2", paths[1]);
        yyjson_mut_obj_add_bool(doc, data, "identical", result.match != 0);
        yyjson_mut_obj_add_int(doc, data, "diff_count", result.diff_count);
        yyjson_mut_obj_add_bool(doc, data, "diff_overflow", result.diff_overflow != 0);
        yyjson_mut_obj_add_uint(doc, data, "objects_compared", result.objects_compared);
        yyjson_mut_obj_add_uint(doc, data, "objects_matched", result.objects_matched);
        yyjson_mut_obj_add_uint(doc, data, "managers_compared", result.managers_compared);
        yyjson_mut_obj_add_uint(doc, data, "managers_matched", result.managers_matched);

        /* Full report */
        if (result.report[0] != '\0') {
            nmo_cli_json_add_str_safe(doc, data, "report", result.report);
        }

        /* All differences */
        if (result.diff_count > 0) {
            yyjson_mut_val *diffs = yyjson_mut_arr(doc);
            for (int i = 0; i < result.diff_count; i++) {
                yyjson_mut_val *diff = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_str(doc, diff, "type", diff_type_name(result.diffs[i].type));
                yyjson_mut_obj_add_uint(doc, diff, "object_id", result.diffs[i].object_id);
                nmo_cli_json_add_str_safe(doc, diff, "context", result.diffs[i].context);
                yyjson_mut_arr_append(diffs, diff);
            }
            yyjson_mut_obj_add_val(doc, data, "diffs", diffs);
        }

        nmo_cli_json_write_enveloped_and_free(doc, data, "diff.full", paths[0], c.out, global->format == NMO_CLI_FORMAT_JSON_PRETTY);
    } else {
        /* Text output - print the formatted report */
        nmo_cli_print_heading(c.out, "Full Comparison Report", c.colorize);

        char buf[128];
        snprintf(buf, sizeof(buf), "%s", paths[0]);
        nmo_cli_print_kv(c.out, "File 1", buf, 18, c.colorize);
        snprintf(buf, sizeof(buf), "%s", paths[1]);
        nmo_cli_print_kv(c.out, "File 2", buf, 18, c.colorize);

        fprintf(c.out, "\n");
        snprintf(buf, sizeof(buf), "%u", result.objects_compared);
        nmo_cli_print_kv(c.out, "Objects compared", buf, 18, c.colorize);
        snprintf(buf, sizeof(buf), "%u", result.objects_matched);
        nmo_cli_print_kv(c.out, "Objects matched", buf, 18, c.colorize);
        snprintf(buf, sizeof(buf), "%u", result.managers_compared);
        nmo_cli_print_kv(c.out, "Managers compared", buf, 18, c.colorize);
        snprintf(buf, sizeof(buf), "%u", result.managers_matched);
        nmo_cli_print_kv(c.out, "Managers matched", buf, 18, c.colorize);

        if (result.match) {
            fprintf(c.out, "\n%sFiles are identical%s\n",
                    c.colorize ? NMO_CLI_COLOR_GREEN : "",
                    c.colorize ? NMO_CLI_COLOR_RESET : "");
        } else {
            fprintf(c.out, "\n%sDifferences found: %d%s",
                    c.colorize ? NMO_CLI_COLOR_YELLOW : "",
                    result.diff_count,
                    c.colorize ? NMO_CLI_COLOR_RESET : "");
            if (result.diff_overflow) {
                fprintf(c.out, " %s(overflow, only first %d shown)%s",
                        c.colorize ? NMO_CLI_COLOR_RED : "",
                        NMO_MAX_DIFFS,
                        c.colorize ? NMO_CLI_COLOR_RESET : "");
            }
            fprintf(c.out, "\n");
        }

        /* Print the detailed report */
        if (result.report[0] != '\0') {
            fprintf(c.out, "\n");
            nmo_cli_print_heading(c.out, "Detailed Report", c.colorize);
            fprintf(c.out, "%s", result.report);
        }
    }

    nmo_tool_close_session(ctx1, ses1);
    nmo_tool_close_session(ctx2, ses2);

    return nmo_cmd_ctx_done(&c, NMO_CLI_EXIT_SUCCESS);
}

