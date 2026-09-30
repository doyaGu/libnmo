/**
 * @file test_plugin_dependency_corpus.c
 * @brief Corpus-level plugin dependency regression gate.
 */

#include "../test_framework.h"

#include "document/nmo_document_load.h"
#include "core/nmo_guid.h"
#include "runtime/nmo_context.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct plugin_corpus_stats {
    size_t files_seen;
    size_t files_loaded;
    size_t files_with_missing;
    size_t files_with_null_guid;
    size_t missing_entries;
    size_t load_errors;
    char first_error[1024];
} plugin_corpus_stats_t;

static void record_first_error(plugin_corpus_stats_t *stats, const char *fmt, ...)
{
    if (stats->first_error[0] != '\0') {
        return;
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(stats->first_error, sizeof(stats->first_error), fmt, args);
    va_end(args);
}

static void scan_plugin_dependencies_for_file(
    nmo_context_t *ctx,
    const char *path,
    plugin_corpus_stats_t *stats)
{
    stats->files_seen++;

    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) {
        stats->load_errors++;
        record_first_error(stats, "failed to create session for %s", path);
        return;
    }

    nmo_load_options_t opts = nmo_load_options_default();
    opts.profile = NMO_LOAD_PROFILE_METADATA;

    nmo_status_t st = nmo_load_file(session, path, &opts);
    if (st != NMO_OK) {
        stats->load_errors++;
        record_first_error(stats, "metadata load failed for %s with status %d", path, (int)st);
        nmo_session_destroy(session);
        return;
    }

    stats->files_loaded++;

    const nmo_session_plugin_diagnostics_t *diag =
        nmo_session_get_plugin_diagnostics(session);
    if (diag == NULL) {
        stats->load_errors++;
        record_first_error(stats, "plugin diagnostics unavailable for %s", path);
        nmo_session_destroy(session);
        return;
    }

    if (diag->missing_count > 0) {
        stats->files_with_missing++;
        stats->missing_entries += diag->missing_count;
        record_first_error(stats, "%s has %zu missing plugin dependencies",
                           path, diag->missing_count);
    }

    for (size_t i = 0; i < diag->entry_count; i++) {
        const nmo_session_plugin_dependency_status_t *entry = &diag->entries[i];
        if (nmo_guid_is_null(entry->guid)) {
            stats->files_with_null_guid++;
            record_first_error(stats, "%s exposes null GUID plugin dependency", path);
            break;
        }
    }

    nmo_session_destroy(session);
}

typedef struct plugin_corpus_scan {
    nmo_context_t *ctx;
    plugin_corpus_stats_t *stats;
} plugin_corpus_scan_t;

static void scan_corpus_file(const char *path, void *user)
{
    plugin_corpus_scan_t *scan = (plugin_corpus_scan_t *)user;
    scan_plugin_dependencies_for_file(scan->ctx, path, scan->stats);
}

TEST(plugin_dependency_corpus, all_reference_files_resolve_plugin_dependencies)
{
    /* Corpus scan: skip when the gitignored Virtools sample set is absent. */
    TEST_REQUIRE_FIXTURE("Ballance/base.cmo");
    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;

    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    plugin_corpus_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    plugin_corpus_scan_t scan = { ctx, &stats };
    if (test_corpus_walk(NMO_TEST_DATA_DIR, scan_corpus_file, &scan) != 0) {
        record_first_error(&stats, "failed to read a directory under %s", NMO_TEST_DATA_DIR);
        stats.load_errors++;
    }

    printf("  Plugin dependency corpus: seen=%zu loaded=%zu missing_files=%zu "
           "missing_entries=%zu null_guid_files=%zu load_errors=%zu\n",
           stats.files_seen, stats.files_loaded, stats.files_with_missing,
           stats.missing_entries, stats.files_with_null_guid, stats.load_errors);
    if (stats.first_error[0] != '\0') {
        printf("  First plugin dependency corpus error: %s\n", stats.first_error);
    }

    nmo_context_release(ctx);

    /* The full reference corpus has several hundred files; smaller local
     * sample sets are still checked, but say so. */
    ASSERT_GE(stats.files_seen, 1u);
    if (stats.files_seen < 500u) {
        printf("  Note: only %zu files scanned; the full corpus has 500+\n",
               stats.files_seen);
    }
    ASSERT_EQ(stats.files_seen, stats.files_loaded);
    ASSERT_EQ(0u, stats.load_errors);
    ASSERT_EQ(0u, stats.files_with_missing);
    ASSERT_EQ(0u, stats.missing_entries);
    ASSERT_EQ(0u, stats.files_with_null_guid);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(plugin_dependency_corpus, all_reference_files_resolve_plugin_dependencies);
TEST_MAIN_END()

