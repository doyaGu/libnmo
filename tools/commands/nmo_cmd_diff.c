/**
 * @file nmo_cmd_diff.c
 * @brief CLI diff command group implementation
 */

#include "nmo_cmd_diff.h"
#include "../nmo_cmd_core.h"
#include "../nmo_cmd_ctx.h"
#include "../nmo_cli_output.h"
#include "../nmo_cli_record.h"
#include "../nmo_opt.h"
#include "../nmo_tool_common.h"
#include "nmo.h"
#include "document/nmo_document_compare.h"
#include "runtime/nmo_context.h"
#include "chunk/nmo_chunk_inspect.h"
#include "object/nmo_object_diff.h"
#include "format/nmo_object.h"
#include "object/nmo_object_repository.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

    size_t n1 = nmo_object_repository_get_count(repo1);
    size_t n2 = nmo_object_repository_get_count(repo2);

    for (size_t i = 0; i < n1; i++) {
        nmo_object_t *obj = nmo_object_repository_get_by_index(repo1, i);
        if (!obj) {
            continue;
        }
        nmo_class_id_t cid = nmo_object_get_class_id(obj);
        class_count_entry_t *e = class_histogram_add(hist, cid);
        if (e) e->count1++;
    }
    for (size_t i = 0; i < n2; i++) {
        nmo_object_t *obj = nmo_object_repository_get_by_index(repo2, i);
        if (!obj) {
            continue;
        }
        nmo_class_id_t cid = nmo_object_get_class_id(obj);
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
 * @brief Open two documents for comparison
 * @return 0 on success, NMO_CLI_EXIT_IO_ERROR on failure
 */
static int open_two_documents(const char *path1, const char *path2,
                              nmo_context_t **ctx1,
                              nmo_document_t **doc1,
                              nmo_workspace_t **ws1,
                              bool *owns1,
                              nmo_context_t **ctx2,
                              nmo_document_t **doc2,
                              nmo_workspace_t **ws2,
                              bool *owns2)
{
    char *open_error = NULL;

    *owns1 = true;
    *owns2 = true;

    if (!nmo_tool_open_document(path1, ctx1, doc1, ws1, &open_error)) {
        fprintf(stderr, "Error opening '%s': %s\n", path1,
                open_error ? open_error : "Failed to open file");
        free(open_error);
        return NMO_CLI_EXIT_IO_ERROR;
    }

    /* Open second file */
    if (!nmo_tool_open_document(path2, ctx2, doc2, ws2, &open_error)) {
        fprintf(stderr, "Error opening '%s': %s\n", path2,
                open_error ? open_error : "Failed to open file");
        free(open_error);
        if (*owns1) {
            nmo_tool_close_document(*ctx1, *doc1, *ws1);
        }
        return NMO_CLI_EXIT_IO_ERROR;
    }

    return 0;
}

static void close_two_documents(nmo_context_t *ctx1,
                                nmo_document_t *doc1,
                                nmo_workspace_t *ws1,
                                bool owns1,
                                nmo_context_t *ctx2,
                                nmo_document_t *doc2,
                                nmo_workspace_t *ws2,
                                bool owns2)
{
    if (owns1) {
        nmo_tool_close_document(ctx1, doc1, ws1);
    }
    if (owns2) {
        nmo_tool_close_document(ctx2, doc2, ws2);
    }
}

static int open_current_left_document(nmo_cmd_ctx_t *left, const char *right_path,
                                      nmo_context_t **ctx1,
                                      nmo_document_t **doc1,
                                      nmo_workspace_t **ws1,
                                      bool *owns1,
                                      nmo_context_t **ctx2,
                                      nmo_document_t **doc2,
                                      nmo_workspace_t **ws2,
                                      bool *owns2)
{
    if (!left || !left->ctx || !left->document || !left->workspace || !right_path) {
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    char *open_error = NULL;
    *ctx1 = left->ctx;
    *doc1 = left->document;
    *ws1 = left->workspace;
    *owns1 = false;
    *owns2 = true;

    if (!nmo_tool_open_document(right_path, ctx2, doc2, ws2, &open_error)) {
        fprintf(stderr, "Error opening '%s': %s\n", right_path,
                open_error ? open_error : "Failed to open file");
        free(open_error);
        return NMO_CLI_EXIT_IO_ERROR;
    }
    return NMO_CLI_EXIT_SUCCESS;
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
        case NMO_DIFF_OBJECT_CHUNK_METADATA: return "object_chunk_metadata";
        case NMO_DIFF_PLUGIN_DEPENDENCIES: return "plugin_dependencies";
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
 * Report records
 *
 * Every builder serves a CLI command and its in-session variant. The
 * in-session reports have always been shorter; `details` selects the parts
 * only the CLI commands print.
 * ============================================================================ */

typedef struct {
    bool chunks_only;
    bool by_object;
    uint32_t object_id;
} diff_entry_filter_t;

static bool diff_entry_selected(const nmo_diff_entry_t *entry,
                                const diff_entry_filter_t *filter)
{
    if (!filter) {
        return true;
    }
    if (filter->by_object && entry->object_id != filter->object_id) {
        return false;
    }
    return !filter->chunks_only ||
           entry->type == NMO_DIFF_OBJECT_CHUNK_SIZE ||
           entry->type == NMO_DIFF_OBJECT_CHUNK_DATA ||
           entry->type == NMO_DIFF_OBJECT_CHUNK_METADATA;
}

static int diff_selected_count(const nmo_comparison_result_t *result,
                               const diff_entry_filter_t *filter)
{
    int count = 0;
    for (int i = 0; i < result->diff_count; i++) {
        if (diff_entry_selected(&result->diffs[i], filter)) {
            count++;
        }
    }
    return count;
}

/* String field that stays JSON null when `value` is NULL. */
static bool diff_record_str_or_null(nmo_cli_record_t *rec, const char *key,
                                    const char *value)
{
    return value ? nmo_cli_record_str(rec, key, NULL, value)
                 : nmo_cli_record_null(rec, key, NULL, NULL);
}

/* Title and the "File 1" / "File 2" lines shared by the keyed reports. */
static nmo_cli_record_t *diff_report_new(const char *title,
                                         const char *const paths[2])
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_title(rec, title);
    ok = ok && nmo_cli_record_str(rec, "file1", "File 1", paths[0]);
    ok = ok && nmo_cli_record_str(rec, "file2", "File 2", paths[1]);
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

static bool diff_add_status(nmo_cli_record_t *rec,
                            const nmo_comparison_result_t *result,
                            bool note_overflow, bool colorize)
{
    if (result->match) {
        return nmo_cli_record_raw_fmt(rec, "\n%sFiles are identical%s\n",
                                      colorize ? NMO_CLI_COLOR_GREEN : "",
                                      colorize ? NMO_CLI_COLOR_RESET : "");
    }
    bool ok = nmo_cli_record_raw_fmt(rec, "\n%sDifferences found: %d%s",
                                     colorize ? NMO_CLI_COLOR_YELLOW : "",
                                     result->diff_count,
                                     colorize ? NMO_CLI_COLOR_RESET : "");
    if (ok && note_overflow && result->diff_overflow) {
        ok = nmo_cli_record_raw_fmt(rec, " %s(overflow, only first %d shown)%s",
                                    colorize ? NMO_CLI_COLOR_RED : "",
                                    NMO_MAX_DIFFS,
                                    colorize ? NMO_CLI_COLOR_RESET : "");
    }
    return ok && nmo_cli_record_raw(rec, "\n");
}

static nmo_cli_record_t *diff_entry_record(const nmo_diff_entry_t *entry)
{
    const char *type = diff_type_name(entry->type);
    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item != NULL;
    ok = ok && nmo_cli_record_str(item, "type", NULL, type);
    ok = ok && nmo_cli_record_uint(item, "id", NULL, entry->object_id);
    ok = ok && nmo_cli_record_str(item, "context", NULL, entry->context);
    ok = ok && nmo_cli_record_set_summary_fmt(item, "  [%s] %s", type,
                                              entry->context);
    if (!ok) {
        nmo_cli_record_free(item);
        return NULL;
    }
    return item;
}

/*
 * "diffs" array of the selected entries, present whenever the comparison
 * found any difference. Text lists them under a "Differences" heading only
 * when `show_text` is set.
 */
static bool diff_add_entries(nmo_cli_record_t *rec,
                             const nmo_comparison_result_t *result,
                             const diff_entry_filter_t *filter,
                             bool show_text)
{
    if (result->diff_count <= 0) {
        return true;
    }
    if (show_text && !nmo_cli_record_heading(rec, "Differences")) {
        return false;
    }
    nmo_cli_record_array_t *diffs = nmo_cli_record_array(rec, "diffs", NULL);
    if (!diffs) {
        return false;
    }
    if (show_text) {
        nmo_cli_record_array_omit_heading(diffs);
    }
    bool ok = true;
    for (int i = 0; ok && i < result->diff_count; i++) {
        if (diff_entry_selected(&result->diffs[i], filter)) {
            ok = nmo_cli_record_array_add(diffs,
                                          diff_entry_record(&result->diffs[i]));
        }
    }
    return ok;
}

static bool diff_add_file_info(nmo_cli_record_t *rec, const char *key,
                               const nmo_file_info_t *info)
{
    nmo_cli_record_t *obj = nmo_cli_record_object(rec, key);
    return obj &&
           nmo_cli_record_uint(obj, "object_count", NULL, info->object_count) &&
           nmo_cli_record_uint(obj, "manager_count", NULL, info->manager_count) &&
           nmo_cli_record_uint(obj, "file_version", NULL, info->file_version) &&
           nmo_cli_record_uint(obj, "ck_version", NULL, info->ck_version);
}

/* One class breakdown entry; its summary is the text table row. */
static nmo_cli_record_t *diff_class_record(const class_count_entry_t *e,
                                           nmo_context_t *ctx1,
                                           nmo_context_t *ctx2,
                                           bool colorize)
{
    const char *cname = nmo_cli_class_name_from_id(ctx1, e->class_id);
    if (!cname) cname = nmo_cli_class_name_from_id(ctx2, e->class_id);

    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item != NULL;
    ok = ok && nmo_cli_record_uint(item, "class_id", NULL, e->class_id);
    ok = ok && diff_record_str_or_null(item, "class_name", cname);
    ok = ok && nmo_cli_record_uint(item, "count1", NULL, e->count1);
    ok = ok && nmo_cli_record_uint(item, "count2", NULL, e->count2);
    if (ok && e->count1 != e->count2) {
        ok = nmo_cli_record_int(item, "delta", NULL,
                                (int64_t)e->count2 - (int64_t)e->count1);
    }

    char *cname_owned = NULL;
    if (!cname) {
        cname_owned = nmo_tool_strdup_fmt("class#%u", e->class_id);
        cname = cname_owned ? cname_owned : "class#?";
    }
    int delta = (int)e->count2 - (int)e->count1;
    if (delta == 0) {
        ok = ok && nmo_cli_record_set_summary_fmt(item, "  %-28s %6u  %6u",
                                                  cname, e->count1, e->count2);
    } else {
        ok = ok && nmo_cli_record_set_summary_fmt(
            item, "  %-28s %6u  %6u  %s%+d%s",
            cname, e->count1, e->count2,
            colorize ? (delta > 0 ? NMO_CLI_COLOR_GREEN : NMO_CLI_COLOR_RED) : "",
            delta,
            colorize ? NMO_CLI_COLOR_RESET : "");
    }
    free(cname_owned);
    if (!ok) {
        nmo_cli_record_free(item);
        return NULL;
    }
    return item;
}

static nmo_cli_record_t *diff_summary_record(
    const char *const paths[2],
    const nmo_comparison_result_t *result,
    const nmo_file_info_t *info1,
    const nmo_file_info_t *info2,
    const class_histogram_t *hist,
    nmo_context_t *ctx1,
    nmo_context_t *ctx2,
    bool details,
    bool verbose,
    bool colorize)
{
    nmo_cli_record_t *rec = diff_report_new("Diff Summary", paths);
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_bool(rec, "identical", NULL, result->match != 0);
    ok = ok && nmo_cli_record_int(rec, "diff_count", NULL, result->diff_count);
    ok = ok && nmo_cli_record_uint(rec, "objects_compared", NULL,
                                   result->objects_compared);
    ok = ok && nmo_cli_record_uint(rec, "objects_matched", NULL,
                                   result->objects_matched);
    ok = ok && diff_add_status(rec, result, false, colorize);
    ok = ok && nmo_cli_record_raw(rec, "\n");
    ok = ok && diff_add_file_info(rec, "file1_info", info1);
    ok = ok && diff_add_file_info(rec, "file2_info", info2);
    ok = ok && nmo_cli_record_text_fmt(rec, "Object count", "%u / %u",
                                       info1->object_count, info2->object_count);
    ok = ok && nmo_cli_record_text_fmt(rec, "Manager count", "%u / %u",
                                       info1->manager_count, info2->manager_count);
    ok = ok && nmo_cli_record_text_fmt(rec, "CK version", "0x%08X / 0x%08X",
                                       info1->ck_version, info2->ck_version);

    if (ok && hist->count > 0) {
        if (details) {
            ok = nmo_cli_record_heading(rec, "Class Breakdown") &&
                 nmo_cli_record_raw_fmt(rec, "  %-28s %6s  %6s  %s\n",
                                        "Class", "File 1", "File 2", "Delta") &&
                 nmo_cli_record_raw_fmt(rec, "  %-28s %6s  %6s  %s\n",
                                        "----------------------------",
                                        "------", "------", "-----");
        }
        nmo_cli_record_array_t *classes =
            ok ? nmo_cli_record_array(rec, "class_breakdown", NULL) : NULL;
        ok = classes != NULL;
        if (ok && details) {
            nmo_cli_record_array_omit_heading(classes);
        }
        for (size_t i = 0; ok && i < hist->count; i++) {
            ok = nmo_cli_record_array_add(
                classes, diff_class_record(&hist->entries[i], ctx1, ctx2, colorize));
        }
    }

    if (ok && details) {
        ok = diff_add_entries(rec, result, NULL, verbose);
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

/* JSON file names and the bold text "diff a/... b/..." line. */
static bool diff_add_unified_header(nmo_cli_record_t *rec,
                                    const char *const paths[2],
                                    bool colorize)
{
    return nmo_cli_record_str(rec, "file1", NULL, paths[0]) &&
           nmo_cli_record_str(rec, "file2", NULL, paths[1]) &&
           nmo_cli_record_raw_fmt(rec, "%sdiff a/%s b/%s%s\n",
                                  colorize ? "\x1b[1m" : "",
                                  paths[0], paths[1],
                                  colorize ? "\x1b[0m" : "");
}

static bool diff_add_object_counts_line(nmo_cli_record_t *rec,
                                        const nmo_diff_result_t *diff)
{
    return nmo_cli_record_raw_fmt(
        rec, "\n%zu object(s) changed, %zu renamed, %zu removed, %zu added"
             " (%zu identical)\n",
        diff->changed_count, diff->renamed_count,
        diff->removed_count, diff->added_count, diff->identical_count);
}

static nmo_cli_record_t *diff_field_record(const nmo_field_diff_t *fd,
                                           bool colorize)
{
    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item != NULL;
    ok = ok && diff_record_str_or_null(item, "field", fd->field_name);
    ok = ok && nmo_cli_record_str(item, "before", NULL, fd->before);
    ok = ok && nmo_cli_record_str(item, "after", NULL, fd->after);
    ok = ok && nmo_cli_record_set_summary_fmt(
        item, "%s-  %-24s : %s%s\n%s+  %-24s : %s%s",
        colorize ? NMO_CLI_COLOR_RED : "",
        fd->field_name, fd->before,
        colorize ? NMO_CLI_COLOR_RESET : "",
        colorize ? NMO_CLI_COLOR_GREEN : "",
        fd->field_name, fd->after,
        colorize ? NMO_CLI_COLOR_RESET : "");
    if (!ok) {
        nmo_cli_record_free(item);
        return NULL;
    }
    return item;
}

/* One changed object; its text is a unified diff hunk. */
static nmo_cli_record_t *diff_changed_record(const nmo_object_diff_t *od,
                                             nmo_context_t *ctx1,
                                             nmo_context_t *ctx2,
                                             bool colorize)
{
    char *path_a = nmo_core_object_path_dup(ctx1, od->obj1);
    char *path_b = nmo_core_object_path_dup(ctx2, od->obj2);

    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item != NULL;
    ok = ok && nmo_cli_record_raw_fmt(item, "\n%s--- a/%s%s\n%s+++ b/%s%s\n",
                                      colorize ? NMO_CLI_COLOR_RED : "",
                                      path_a ? path_a : "",
                                      colorize ? NMO_CLI_COLOR_RESET : "",
                                      colorize ? NMO_CLI_COLOR_GREEN : "",
                                      path_b ? path_b : "",
                                      colorize ? NMO_CLI_COLOR_RESET : "");
    ok = ok && nmo_cli_record_str(item, "path", NULL, path_a ? path_a : "");
    ok = ok && nmo_cli_record_uint(item, "changed_fields", NULL,
                                   (uint64_t)od->field_diff_total);
    free(path_a);
    free(path_b);

    nmo_cli_record_array_t *fields =
        ok ? nmo_cli_record_array(item, "fields", NULL) : NULL;
    ok = fields != NULL;
    if (ok) {
        nmo_cli_record_array_omit_heading(fields);
    }
    for (size_t fi = 0; ok && fi < od->field_diff_count; fi++) {
        ok = nmo_cli_record_array_add(
            fields, diff_field_record(&od->field_diffs[fi], colorize));
    }
    if (ok && od->field_diff_total > od->field_diff_count) {
        ok = nmo_cli_record_raw_fmt(item, "%s   ... and %zu more field(s)%s\n",
                                    colorize ? NMO_CLI_COLOR_YELLOW : "",
                                    od->field_diff_total - od->field_diff_count,
                                    colorize ? NMO_CLI_COLOR_RESET : "");
    }
    if (!ok) {
        nmo_cli_record_free(item);
        return NULL;
    }
    return item;
}

static nmo_cli_record_t *diff_rename_record(const nmo_rename_diff_t *rd,
                                            nmo_context_t *ctx1,
                                            nmo_context_t *ctx2)
{
    char *before_path = nmo_core_object_path_dup(ctx1, rd->obj1);
    char *after_path = nmo_core_object_path_dup(ctx2, rd->obj2);

    nmo_cli_record_t *item = nmo_cli_record_new();
    bool ok = item != NULL;
    ok = ok && nmo_cli_record_str(item, "before", NULL,
                                  before_path ? before_path : "");
    ok = ok && nmo_cli_record_str(item, "after", NULL,
                                  after_path ? after_path : "");
    ok = ok && diff_record_str_or_null(item, "before_name", rd->before_name);
    ok = ok && diff_record_str_or_null(item, "after_name", rd->after_name);
    ok = ok && nmo_cli_record_real(item, "similarity", NULL, rd->similarity, NULL);
    ok = ok && nmo_cli_record_set_summary_fmt(item, "  %s -> %s (sim=%.3f)",
                                              before_path ? before_path : "",
                                              after_path ? after_path : "",
                                              rd->similarity);
    free(before_path);
    free(after_path);
    if (!ok) {
        nmo_cli_record_free(item);
        return NULL;
    }
    return item;
}

/*
 * Object path list: `key` holds the paths in JSON, `count_key` their number.
 * Text lists them, one "<sign> path" line each, under a "<title> (N):"
 * heading, and prints nothing when the list is empty.
 */
static bool diff_add_path_list(nmo_cli_record_t *rec,
                               const char *key,
                               const char *count_key,
                               nmo_context_t *ctx,
                               const nmo_object_t *const *objects,
                               size_t count,
                               const char *title,
                               char sign,
                               const char *color,
                               bool colorize)
{
    char **owned = count ? (char **)calloc(count, sizeof(*owned)) : NULL;
    const char **values = count ? (const char **)calloc(count, sizeof(*values)) : NULL;
    bool ok = count == 0 || (owned && values);
    if (ok && count > 0) {
        ok = nmo_cli_record_raw_fmt(rec, "\n%s%s (%zu):%s\n",
                                    colorize ? color : "", title, count,
                                    colorize ? NMO_CLI_COLOR_RESET : "");
    }
    for (size_t i = 0; ok && i < count; i++) {
        owned[i] = nmo_core_object_path_dup(ctx, objects[i]);
        values[i] = owned[i] ? owned[i] : "";
        ok = nmo_cli_record_raw_fmt(rec, "%s  %c %s%s\n",
                                    colorize ? color : "", sign, values[i],
                                    colorize ? NMO_CLI_COLOR_RESET : "");
    }
    ok = ok && nmo_cli_record_str_list(rec, key, NULL, values, count, NULL);
    ok = ok && nmo_cli_record_uint(rec, count_key, NULL, (uint64_t)count);
    for (size_t i = 0; owned && i < count; i++) {
        free(owned[i]);
    }
    free(owned);
    free((void *)values);
    return ok;
}

static nmo_cli_record_t *diff_objects_record(const char *const paths[2],
                                             const nmo_diff_result_t *diff,
                                             const nmo_diff_config_t *cfg,
                                             nmo_context_t *ctx1,
                                             nmo_context_t *ctx2,
                                             uint32_t max_objects,
                                             bool colorize)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && diff_add_unified_header(rec, paths, colorize);
    ok = ok && nmo_cli_record_uint(rec, "objects_file1", NULL,
                                   (uint64_t)diff->total_objects1);
    ok = ok && nmo_cli_record_uint(rec, "objects_file2", NULL,
                                   (uint64_t)diff->total_objects2);
    ok = ok && nmo_cli_record_real(rec, "min_similarity", NULL,
                                   cfg->min_similarity, NULL);
    ok = ok && nmo_cli_record_real(rec, "rename_similarity", NULL,
                                   cfg->rename_similarity, NULL);
    ok = ok && nmo_cli_record_uint(rec, "identical_count", NULL,
                                   (uint64_t)diff->identical_count);

    nmo_cli_record_array_t *changed =
        ok ? nmo_cli_record_array(rec, "changed", NULL) : NULL;
    ok = changed != NULL;
    if (ok) {
        nmo_cli_record_array_omit_heading(changed);
        nmo_cli_record_array_inline_items(changed);
    }
    for (size_t i = 0; ok && i < diff->changed_count; i++) {
        if (max_objects > 0 && i >= max_objects) break;
        ok = nmo_cli_record_array_add(
            changed, diff_changed_record(&diff->changed[i], ctx1, ctx2, colorize));
    }
    ok = ok && nmo_cli_record_uint(rec, "changed_count", NULL,
                                   (uint64_t)diff->changed_count);

    nmo_cli_record_array_t *renamed =
        ok ? nmo_cli_record_array(rec, "renamed", NULL) : NULL;
    ok = renamed != NULL;
    if (ok && diff->renamed_count > 0) {
        char *heading = nmo_tool_strdup_fmt("%sRenamed (%zu):%s",
                                            colorize ? NMO_CLI_COLOR_CYAN : "",
                                            diff->renamed_count,
                                            colorize ? NMO_CLI_COLOR_RESET : "");
        ok = heading && nmo_cli_record_array_set_heading(renamed, heading);
        free(heading);
    }
    for (size_t i = 0; ok && i < diff->renamed_count; i++) {
        ok = nmo_cli_record_array_add(
            renamed, diff_rename_record(&diff->renamed[i], ctx1, ctx2));
    }
    ok = ok && nmo_cli_record_uint(rec, "renamed_count", NULL,
                                   (uint64_t)diff->renamed_count);

    ok = ok && diff_add_path_list(rec, "removed", "removed_count", ctx1,
                                  diff->removed, diff->removed_count,
                                  "Removed", '-', NMO_CLI_COLOR_RED, colorize);
    ok = ok && diff_add_path_list(rec, "added", "added_count", ctx2,
                                  diff->added, diff->added_count,
                                  "Added", '+', NMO_CLI_COLOR_GREEN, colorize);

    if (ok && diff->changed_count == 0 && diff->renamed_count == 0 &&
        diff->removed_count == 0 && diff->added_count == 0) {
        ok = nmo_cli_record_raw_fmt(rec, "\n%sFiles are identical%s\n",
                                    colorize ? NMO_CLI_COLOR_GREEN : "",
                                    colorize ? NMO_CLI_COLOR_RESET : "");
    } else if (ok) {
        ok = diff_add_object_counts_line(rec, diff);
        if (ok && max_objects > 0 && diff->changed_count > max_objects) {
            ok = nmo_cli_record_raw_fmt(rec, "%s(showing first %u of %zu changed)%s\n",
                                        colorize ? NMO_CLI_COLOR_YELLOW : "",
                                        max_objects, diff->changed_count,
                                        colorize ? NMO_CLI_COLOR_RESET : "");
        }
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

/* In-session objects report: counts only. */
static nmo_cli_record_t *diff_object_counts_record(const char *const paths[2],
                                                   const nmo_diff_result_t *diff,
                                                   bool colorize)
{
    nmo_cli_record_t *rec = nmo_cli_record_new();
    bool ok = rec != NULL;
    ok = ok && diff_add_unified_header(rec, paths, colorize);
    ok = ok && nmo_cli_record_uint(rec, "objects_file1", NULL,
                                   (uint64_t)diff->total_objects1);
    ok = ok && nmo_cli_record_uint(rec, "objects_file2", NULL,
                                   (uint64_t)diff->total_objects2);
    ok = ok && nmo_cli_record_uint(rec, "changed_count", NULL,
                                   (uint64_t)diff->changed_count);
    ok = ok && nmo_cli_record_uint(rec, "renamed_count", NULL,
                                   (uint64_t)diff->renamed_count);
    ok = ok && nmo_cli_record_uint(rec, "removed_count", NULL,
                                   (uint64_t)diff->removed_count);
    ok = ok && nmo_cli_record_uint(rec, "added_count", NULL,
                                   (uint64_t)diff->added_count);
    ok = ok && nmo_cli_record_uint(rec, "identical_count", NULL,
                                   (uint64_t)diff->identical_count);
    ok = ok && diff_add_object_counts_line(rec, diff);
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

/*
 * The CLI report keys "identical" and "diff_count" to the whole comparison
 * and lists the chunk differences; the in-session one keys them to the chunk
 * differences alone.
 */
static nmo_cli_record_t *diff_chunks_record(const char *const paths[2],
                                            const nmo_comparison_result_t *result,
                                            const diff_entry_filter_t *filter,
                                            bool details,
                                            bool colorize)
{
    int chunk_diff_count = diff_selected_count(result, filter);

    nmo_cli_record_t *rec = diff_report_new("Chunk Comparison", paths);
    bool ok = rec != NULL;
    if (ok && filter->by_object) {
        ok = nmo_cli_record_uint(rec, "id", details ? "Object ID" : NULL,
                                 filter->object_id);
    }
    ok = ok && nmo_cli_record_bool(rec, "identical", NULL,
                                   details ? result->match != 0
                                           : chunk_diff_count == 0);
    ok = ok && nmo_cli_record_int(rec, "diff_count", NULL,
                                  details ? result->diff_count : chunk_diff_count);
    if (ok && details && chunk_diff_count == 0) {
        ok = nmo_cli_record_raw_fmt(rec, "\n%sChunks are identical%s\n",
                                    colorize ? NMO_CLI_COLOR_GREEN : "",
                                    colorize ? NMO_CLI_COLOR_RESET : "");
    } else if (ok) {
        bool highlight = colorize && chunk_diff_count > 0;
        ok = nmo_cli_record_raw_fmt(rec, "\n%sChunk differences: %d%s\n",
                                    highlight ? NMO_CLI_COLOR_YELLOW : "",
                                    chunk_diff_count,
                                    highlight ? NMO_CLI_COLOR_RESET : "");
    }
    if (ok && details) {
        ok = diff_add_entries(rec, result, filter, chunk_diff_count > 0);
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

static nmo_cli_record_t *diff_full_record(const char *const paths[2],
                                          const nmo_comparison_result_t *result,
                                          bool details,
                                          bool colorize)
{
    nmo_cli_record_t *rec = diff_report_new("Full Comparison Report", paths);
    bool ok = rec != NULL;
    ok = ok && nmo_cli_record_bool(rec, "identical", NULL, result->match != 0);
    ok = ok && nmo_cli_record_int(rec, "diff_count", NULL, result->diff_count);
    ok = ok && nmo_cli_record_bool(rec, "diff_overflow", NULL,
                                   result->diff_overflow != 0);
    ok = ok && nmo_cli_record_raw(rec, "\n");
    ok = ok && nmo_cli_record_uint(rec, "objects_compared", "Objects compared",
                                   result->objects_compared);
    ok = ok && nmo_cli_record_uint(rec, "objects_matched", "Objects matched",
                                   result->objects_matched);
    if (ok && details) {
        ok = nmo_cli_record_uint(rec, "managers_compared", "Managers compared",
                                 result->managers_compared) &&
             nmo_cli_record_uint(rec, "managers_matched", "Managers matched",
                                 result->managers_matched) &&
             diff_add_status(rec, result, true, colorize);
        if (ok && result->report[0] != '\0') {
            ok = nmo_cli_record_str(rec, "report", NULL, result->report) &&
                 nmo_cli_record_heading(rec, "Detailed Report") &&
                 nmo_cli_record_raw(rec, result->report);
        }
        ok = ok && diff_add_entries(rec, result, NULL, false);
    }
    if (!ok) {
        nmo_cli_record_free(rec);
        return NULL;
    }
    return rec;
}

/*
 * Diff reports name the left file as the JSON input file, which the output
 * context of the two-file commands does not carry.
 */
static int diff_emit(const nmo_cmd_ctx_t *c, nmo_cli_record_t *rec,
                     const char *command, const char *input_file)
{
    nmo_cmd_ctx_t out = *c;
    out.file_path = input_file;
    return nmo_cmd_ctx_emit_record(&out, rec, command, 18, c->colorize);
}

/* ============================================================================
 * diff summary
 * ============================================================================ */

int nmo_cmd_diff_summary(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--ignore-order", NULL, NMO_OPT_FLAG, "Ignore object order"},
        {"--verbose",      "-v", NMO_OPT_FLAG, "Show detailed differences"},
        {"--strict",       NULL, NMO_OPT_FLAG, "Return failure exit code if differences found"},
    };
    enum { OPT_IGNORD, OPT_VERBOSE, OPT_STRICT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    if (r.pos_count < 2) {
        fprintf(stderr, "Error: Need two files to compare\n");
        fprintf(stderr, "Usage: nmo diff summary [options] <file1> <file2>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *paths[2] = {
        r.pos_args[0],
        r.pos_args[1]
    };
    bool ignore_order = vals[OPT_IGNORD].present && vals[OPT_IGNORD].val.flag;
    bool verbose      = vals[OPT_VERBOSE].present && vals[OPT_VERBOSE].val.flag;
    bool strict       = vals[OPT_STRICT].present && vals[OPT_STRICT].val.flag;

    /* Open both sessions */
    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_workspace_t *ws1 = NULL, *ws2 = NULL;
    nmo_document_t *doc1 = NULL, *doc2 = NULL;
    bool owns1 = true, owns2 = true;
    int open_result = open_two_documents(paths[0], paths[1],
                                        &ctx1, &doc1, &ws1, &owns1,
                                        &ctx2, &doc2, &ws2, &owns2);
    if (open_result != 0) {
        return open_result;
    }
    /* Compare sessions */
    nmo_compare_flags_t flags = NMO_COMPARE_STRUCTURE | NMO_COMPARE_FILE_INFO | NMO_COMPARE_CHUNKS;
    if (ignore_order) {
        flags |= NMO_COMPARE_IGNORE_ORDER;
    }
    if (verbose) {
        flags |= NMO_COMPARE_VERBOSE;
    }

    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    nmo_status_t cmp_result = nmo_document_compare(doc1, doc2, flags, &result);

    if (cmp_result != NMO_OK) {
        fprintf(stderr, "Error: Comparison failed with code %d\n", cmp_result);
        close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Get file info from both sessions */
    nmo_file_info_t info1 = nmo_document_get_file_info(doc1);
    nmo_file_info_t info2 = nmo_document_get_file_info(doc2);

    /* Build per-class breakdown */
    nmo_object_repository_t *repo1 = nmo_tool_owner_repository(ws1);
    nmo_object_repository_t *repo2 = nmo_tool_owner_repository(ws2);
    class_histogram_t hist;
    build_class_histogram(repo1, repo2, &hist);
    qsort(hist.entries, hist.count, sizeof(class_count_entry_t), class_entry_cmp);

    /* Output */
    nmo_cmd_ctx_t c;
    int out_rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (out_rc) {
        close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
        return out_rc;
    }

    int rc = diff_emit(&c, diff_summary_record(paths, &result, &info1, &info2,
                                              &hist, ctx1, ctx2, true,
                                              verbose, c.colorize),
                       "diff.summary", paths[0]);

    close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);

    /* Return strict failure if requested and diffs found */
    if (rc == NMO_CLI_EXIT_SUCCESS && strict && !result.match) {
        rc = NMO_CLI_EXIT_STRICT_FAILURE;
    }

    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * diff objects -- thin CLI wrapper over nmo_diff_objects() library API
 * ============================================================================ */

int nmo_cmd_diff_objects(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--max-objects",       NULL, NMO_OPT_UINT,  "Maximum objects to show"},
        {"--max-fields",        NULL, NMO_OPT_UINT,  "Maximum fields per object"},
        {"--min-similarity",    NULL, NMO_OPT_FLOAT, "Minimum similarity threshold (0..1)"},
        {"--rename-similarity", NULL, NMO_OPT_FLOAT, "Rename detection threshold (0..1)"},
        {"--format",            "-f", NMO_OPT_STRING, "Output format"},
    };
    enum { OPT_MAXOBJ, OPT_MAXFLD, OPT_MINSIM, OPT_RENSIM, OPT_FMT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    if (r.pos_count < 2) {
        fprintf(stderr, "Error: Need two files to compare\n");
        fprintf(stderr, "Usage: nmo diff objects [options] <file1> <file2>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *paths[2] = {
        r.pos_args[0],
        r.pos_args[1]
    };
    uint32_t max_objects = vals[OPT_MAXOBJ].present ? vals[OPT_MAXOBJ].val.u : 0;
    uint32_t max_fields  = vals[OPT_MAXFLD].present ? vals[OPT_MAXFLD].val.u : 0;
    float min_similarity = vals[OPT_MINSIM].present ? vals[OPT_MINSIM].val.f : -1.0f;
    float rename_similarity = vals[OPT_RENSIM].present ? vals[OPT_RENSIM].val.f : -1.0f;

    /* Open both sessions */
    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_workspace_t *ws1 = NULL, *ws2 = NULL;
    nmo_document_t *doc1 = NULL, *doc2 = NULL;
    bool owns1 = true, owns2 = true;
    int open_result = open_two_documents(paths[0], paths[1],
                                        &ctx1, &doc1, &ws1, &owns1,
                                        &ctx2, &doc2, &ws2, &owns2);
    if (open_result != 0) return open_result;
    /* Run library-level diff engine */
    nmo_diff_config_t cfg = nmo_diff_config_default();
    cfg.max_objects = max_objects;
    cfg.max_fields = max_fields;
    if (min_similarity >= 0.0f) cfg.min_similarity = min_similarity;
    if (rename_similarity >= 0.0f) cfg.rename_similarity = rename_similarity;

    nmo_diff_result_t diff;
    nmo_status_t st = nmo_diff_objects(doc1, doc2, &cfg, &diff);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Diff engine failed with code %d\n", st);
        close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* ---- Output ---- */
    nmo_cmd_ctx_t c;
    int out_rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (out_rc) {
        nmo_diff_result_destroy(&diff);
        close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
        return out_rc;
    }

    int rc = diff_emit(&c, diff_objects_record(paths, &diff, &cfg, ctx1, ctx2,
                                              max_objects, c.colorize),
                       "diff.objects", paths[0]);

    /* Cleanup */
    nmo_diff_result_destroy(&diff);
    close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);

    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * diff chunks
 * ============================================================================ */

int nmo_cmd_diff_chunks(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--object", "-o", NMO_OPT_UINT, "Filter by object ID"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    if (r.pos_count < 2) {
        fprintf(stderr, "Error: Need two files to compare\n");
        fprintf(stderr, "Usage: nmo diff chunks [options] <file1> <file2>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *paths[2] = {
        r.pos_args[0],
        r.pos_args[1]
    };
    uint32_t object_id = vals[0].present ? vals[0].val.u : 0;
    bool specific_object = vals[0].present;

    /* Open both sessions */
    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_workspace_t *ws1 = NULL, *ws2 = NULL;
    nmo_document_t *doc1 = NULL, *doc2 = NULL;
    bool owns1 = true, owns2 = true;
    int open_result = open_two_documents(paths[0], paths[1],
                                        &ctx1, &doc1, &ws1, &owns1,
                                        &ctx2, &doc2, &ws2, &owns2);
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
    nmo_status_t cmp_result = nmo_document_compare(doc1, doc2, flags, &result);

    if (cmp_result != NMO_OK) {
        fprintf(stderr, "Error: Comparison failed with code %d\n", cmp_result);
        close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Output */
    nmo_cmd_ctx_t c;
    int out_rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (out_rc) {
        close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
        return out_rc;
    }

    diff_entry_filter_t filter = {
        .chunks_only = true,
        .by_object = specific_object,
        .object_id = object_id,
    };
    int rc = diff_emit(&c, diff_chunks_record(paths, &result, &filter, true,
                                             c.colorize),
                       "diff.chunks", paths[0]);

    close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);

    return nmo_cmd_ctx_done(&c, rc);
}

/* ============================================================================
 * diff full
 * ============================================================================ */

int nmo_cmd_diff_full(int argc, char **argv, const nmo_cli_global_opts_t *global)
{
    static const nmo_opt_def_t opts[] = {
        {"--ignore-order", NULL, NMO_OPT_FLAG, "Ignore object order"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;

    if (r.pos_count < 2) {
        fprintf(stderr, "Error: Need two files to compare\n");
        fprintf(stderr, "Usage: nmo diff full [options] <file1> <file2>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *paths[2] = {
        r.pos_args[0],
        r.pos_args[1]
    };
    bool ignore_order = vals[0].present && vals[0].val.flag;

    /* Open both sessions */
    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_workspace_t *ws1 = NULL, *ws2 = NULL;
    nmo_document_t *doc1 = NULL, *doc2 = NULL;
    bool owns1 = true, owns2 = true;
    int open_result = open_two_documents(paths[0], paths[1],
                                        &ctx1, &doc1, &ws1, &owns1,
                                        &ctx2, &doc2, &ws2, &owns2);
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
    nmo_status_t cmp_result = nmo_document_compare(doc1, doc2, flags, &result);

    if (cmp_result != NMO_OK) {
        fprintf(stderr, "Error: Comparison failed with code %d\n", cmp_result);
        close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    /* Generate detailed report */
    nmo_comparison_result_format_report(&result);

    /* Output */
    nmo_cmd_ctx_t c;
    int out_rc = nmo_cmd_ctx_init_no_file(&c, global);
    if (out_rc) {
        close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
        return out_rc;
    }

    int rc = diff_emit(&c, diff_full_record(paths, &result, true, c.colorize),
                       "diff.full", paths[0]);

    close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);

    return nmo_cmd_ctx_done(&c, rc);
}

static const char *diff_current_session_label(const nmo_cmd_ctx_t *ctx)
{
    return (ctx && ctx->file_path && ctx->file_path[0])
        ? ctx->file_path
        : "(current session)";
}

static int nmo_cmd_diff_summary_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    static const nmo_opt_def_t opts[] = {
        {"--ignore-order", NULL, NMO_OPT_FLAG, "Ignore object order"},
        {"--verbose",      "-v", NMO_OPT_FLAG, "Show detailed differences"},
        {"--strict",       NULL, NMO_OPT_FLAG, "Return failure exit code if differences found"},
    };
    enum { OPT_IGNORD, OPT_VERBOSE, OPT_STRICT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;
    if (r.pos_count < 1) {
        fprintf(stderr, "Error: Need comparison file\n");
        fprintf(stderr, "Usage: diff summary [options] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *paths[2] = { diff_current_session_label(ctx), r.pos_args[0] };
    bool ignore_order = vals[OPT_IGNORD].present && vals[OPT_IGNORD].val.flag;
    bool verbose      = vals[OPT_VERBOSE].present && vals[OPT_VERBOSE].val.flag;
    bool strict       = vals[OPT_STRICT].present && vals[OPT_STRICT].val.flag;

    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_workspace_t *ws1 = NULL, *ws2 = NULL;
    nmo_document_t *doc1 = NULL, *doc2 = NULL;
    bool owns1 = false, owns2 = true;
    int open_result = open_current_left_document(ctx, paths[1],
                                                &ctx1, &doc1, &ws1, &owns1,
                                        &ctx2, &doc2, &ws2, &owns2);
    if (open_result != 0) return open_result;
    nmo_compare_flags_t flags = NMO_COMPARE_STRUCTURE | NMO_COMPARE_FILE_INFO | NMO_COMPARE_CHUNKS;
    if (ignore_order) flags |= NMO_COMPARE_IGNORE_ORDER;
    if (verbose) flags |= NMO_COMPARE_VERBOSE;

    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    nmo_status_t cmp_result = nmo_document_compare(doc1, doc2, flags, &result);
    if (cmp_result != NMO_OK) {
        fprintf(stderr, "Error: Comparison failed with code %d\n", cmp_result);
        close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_file_info_t info1 = nmo_document_get_file_info(doc1);
    nmo_file_info_t info2 = nmo_document_get_file_info(doc2);

    nmo_object_repository_t *repo1 = nmo_tool_owner_repository(ws1);
    nmo_object_repository_t *repo2 = nmo_tool_owner_repository(ws2);
    class_histogram_t hist;
    build_class_histogram(repo1, repo2, &hist);
    qsort(hist.entries, hist.count, sizeof(class_count_entry_t), class_entry_cmp);

    nmo_cmd_ctx_t c = *ctx;
    int rc = diff_emit(&c, diff_summary_record(paths, &result, &info1, &info2,
                                              &hist, ctx1, ctx2, false,
                                              verbose, c.colorize),
                       "diff.summary", paths[0]);

    close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
    if (rc == NMO_CLI_EXIT_SUCCESS && strict && !result.match) {
        rc = NMO_CLI_EXIT_STRICT_FAILURE;
    }
    return rc;
}

static int nmo_cmd_diff_objects_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    static const nmo_opt_def_t opts[] = {
        {"--max-objects",       NULL, NMO_OPT_UINT,  "Maximum objects to show"},
        {"--max-fields",        NULL, NMO_OPT_UINT,  "Maximum fields per object"},
        {"--min-similarity",    NULL, NMO_OPT_FLOAT, "Minimum similarity threshold (0..1)"},
        {"--rename-similarity", NULL, NMO_OPT_FLOAT, "Rename detection threshold (0..1)"},
        {"--format",            "-f", NMO_OPT_STRING, "Output format"},
    };
    enum { OPT_MAXOBJ, OPT_MAXFLD, OPT_MINSIM, OPT_RENSIM, OPT_FMT, OPT_COUNT };
    nmo_opt_val_t vals[OPT_COUNT];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, OPT_COUNT, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;
    if (r.pos_count < 1) {
        fprintf(stderr, "Error: Need comparison file\n");
        fprintf(stderr, "Usage: diff objects [options] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *paths[2] = { diff_current_session_label(ctx), r.pos_args[0] };
    uint32_t max_objects = vals[OPT_MAXOBJ].present ? vals[OPT_MAXOBJ].val.u : 0;
    uint32_t max_fields  = vals[OPT_MAXFLD].present ? vals[OPT_MAXFLD].val.u : 0;
    float min_similarity = vals[OPT_MINSIM].present ? vals[OPT_MINSIM].val.f : -1.0f;
    float rename_similarity = vals[OPT_RENSIM].present ? vals[OPT_RENSIM].val.f : -1.0f;

    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_workspace_t *ws1 = NULL, *ws2 = NULL;
    nmo_document_t *doc1 = NULL, *doc2 = NULL;
    bool owns1 = false, owns2 = true;
    int open_result = open_current_left_document(ctx, paths[1],
                                                &ctx1, &doc1, &ws1, &owns1,
                                        &ctx2, &doc2, &ws2, &owns2);
    if (open_result != 0) return open_result;
    nmo_diff_config_t cfg = nmo_diff_config_default();
    cfg.max_objects = max_objects;
    cfg.max_fields = max_fields;
    if (min_similarity >= 0.0f) cfg.min_similarity = min_similarity;
    if (rename_similarity >= 0.0f) cfg.rename_similarity = rename_similarity;

    nmo_diff_result_t diff;
    nmo_status_t st = nmo_diff_objects(doc1, doc2, &cfg, &diff);
    if (st != NMO_OK) {
        fprintf(stderr, "Error: Diff engine failed with code %d\n", st);
        close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    nmo_cmd_ctx_t c = *ctx;
    int rc = diff_emit(&c, diff_object_counts_record(paths, &diff, c.colorize),
                       "diff.objects", paths[0]);

    nmo_diff_result_destroy(&diff);
    close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
    return rc;
}

static int nmo_cmd_diff_chunks_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    static const nmo_opt_def_t opts[] = {
        {"--object", "-o", NMO_OPT_UINT, "Filter by object ID"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;
    if (r.pos_count < 1) {
        fprintf(stderr, "Error: Need comparison file\n");
        fprintf(stderr, "Usage: diff chunks [options] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *paths[2] = { diff_current_session_label(ctx), r.pos_args[0] };
    uint32_t object_id = vals[0].present ? vals[0].val.u : 0;
    bool specific_object = vals[0].present;

    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_workspace_t *ws1 = NULL, *ws2 = NULL;
    nmo_document_t *doc1 = NULL, *doc2 = NULL;
    bool owns1 = false, owns2 = true;
    int open_result = open_current_left_document(ctx, paths[1],
                                                &ctx1, &doc1, &ws1, &owns1,
                                        &ctx2, &doc2, &ws2, &owns2);
    if (open_result != 0) return open_result;
    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    nmo_status_t cmp_result = nmo_document_compare(
        doc1, doc2, NMO_COMPARE_CHUNKS | NMO_COMPARE_IDS, &result);
    if (cmp_result != NMO_OK) {
        fprintf(stderr, "Error: Comparison failed with code %d\n", cmp_result);
        close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }

    diff_entry_filter_t filter = {
        .chunks_only = true,
        .by_object = specific_object,
        .object_id = object_id,
    };

    nmo_cmd_ctx_t c = *ctx;
    int rc = diff_emit(&c, diff_chunks_record(paths, &result, &filter, false,
                                             c.colorize),
                       "diff.chunks", paths[0]);
    close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
    return rc;
}

static int nmo_cmd_diff_full_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    static const nmo_opt_def_t opts[] = {
        {"--ignore-order", NULL, NMO_OPT_FLAG, "Ignore object order"},
    };
    nmo_opt_val_t vals[1];
    const char *pos[8];
    nmo_opt_result_t r = { .vals = vals, .pos_args = pos, .pos_capacity = 8 };
    if (nmo_opt_parse(argc, argv, opts, 1, &r) < 0) return NMO_CLI_EXIT_ARG_ERROR;
    if (r.pos_count < 1) {
        fprintf(stderr, "Error: Need comparison file\n");
        fprintf(stderr, "Usage: diff full [options] <file>\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }

    const char *paths[2] = { diff_current_session_label(ctx), r.pos_args[0] };
    bool ignore_order = vals[0].present && vals[0].val.flag;

    nmo_context_t *ctx1 = NULL, *ctx2 = NULL;
    nmo_workspace_t *ws1 = NULL, *ws2 = NULL;
    nmo_document_t *doc1 = NULL, *doc2 = NULL;
    bool owns1 = false, owns2 = true;
    int open_result = open_current_left_document(ctx, paths[1],
                                                &ctx1, &doc1, &ws1, &owns1,
                                        &ctx2, &doc2, &ws2, &owns2);
    if (open_result != 0) return open_result;
    nmo_compare_flags_t flags = NMO_COMPARE_STRUCTURE | NMO_COMPARE_IDS |
                                NMO_COMPARE_NAMES | NMO_COMPARE_CLASS_IDS |
                                NMO_COMPARE_CHUNKS | NMO_COMPARE_SHADOW |
                                NMO_COMPARE_MANAGERS | NMO_COMPARE_FILE_INFO |
                                NMO_COMPARE_VERBOSE;
    if (ignore_order) flags |= NMO_COMPARE_IGNORE_ORDER;

    nmo_comparison_result_t result;
    nmo_comparison_result_init(&result);
    nmo_status_t cmp_result = nmo_document_compare(doc1, doc2, flags, &result);
    if (cmp_result != NMO_OK) {
        fprintf(stderr, "Error: Comparison failed with code %d\n", cmp_result);
        close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
        return NMO_CLI_EXIT_INTERNAL_ERROR;
    }
    nmo_comparison_result_format_report(&result);

    nmo_cmd_ctx_t c = *ctx;
    int rc = diff_emit(&c, diff_full_record(paths, &result, false, c.colorize),
                       "diff.full", paths[0]);
    close_two_documents(ctx1, doc1, ws1, owns1, ctx2, doc2, ws2, owns2);
    return rc;
}

int nmo_cmd_diff_in_session(nmo_cmd_ctx_t *ctx, int argc, char **argv)
{
    if (!ctx || argc < 1 || !argv || !argv[0]) {
        fprintf(stderr, "Usage: diff summary|objects|chunks|full <other-file> ...\n");
        return NMO_CLI_EXIT_ARG_ERROR;
    }
    if (strcmp(argv[0], "summary") == 0 || strcmp(argv[0], "s") == 0) {
        return nmo_cmd_diff_summary_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "objects") == 0 || strcmp(argv[0], "obj") == 0) {
        return nmo_cmd_diff_objects_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "chunks") == 0 || strcmp(argv[0], "ch") == 0) {
        return nmo_cmd_diff_chunks_in_session(ctx, argc, argv);
    }
    if (strcmp(argv[0], "full") == 0 || strcmp(argv[0], "f") == 0) {
        return nmo_cmd_diff_full_in_session(ctx, argc, argv);
    }
    fprintf(stderr, "Unsupported diff read action in session: %s\n", argv[0]);
    return NMO_CLI_EXIT_ARG_ERROR;
}

