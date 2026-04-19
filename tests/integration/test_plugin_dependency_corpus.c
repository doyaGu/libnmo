/**
 * @file test_plugin_dependency_corpus.c
 * @brief Corpus-level plugin dependency regression gate.
 */

#include "../test_framework.h"

#include "app/nmo_load.h"
#include "core/nmo_guid.h"
#include "session/nmo_context.h"
#include "session/nmo_deserializer.h"
#include "session/nmo_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

typedef struct plugin_corpus_stats {
    size_t files_seen;
    size_t files_loaded;
    size_t files_with_missing;
    size_t files_with_null_guid;
    size_t missing_entries;
    size_t load_errors;
    char first_error[1024];
} plugin_corpus_stats_t;

static int has_corpus_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        return 0;
    }
#ifdef _WIN32
    return _stricmp(dot, ".nmo") == 0 ||
           _stricmp(dot, ".cmo") == 0 ||
           _stricmp(dot, ".vmo") == 0;
#else
    return strcmp(dot, ".nmo") == 0 ||
           strcmp(dot, ".cmo") == 0 ||
           strcmp(dot, ".vmo") == 0;
#endif
}

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

#ifdef _WIN32
static void scan_plugin_dependency_directory(
    nmo_context_t *ctx,
    const char *dir,
    plugin_corpus_stats_t *stats)
{
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        record_first_error(stats, "failed to open directory %s", dir);
        return;
    }

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_plugin_dependency_directory(ctx, path, stats);
        } else if (has_corpus_extension(fd.cFileName)) {
            scan_plugin_dependencies_for_file(ctx, path, stats);
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}
#else
static void scan_plugin_dependency_directory(
    nmo_context_t *ctx,
    const char *dir,
    plugin_corpus_stats_t *stats)
{
    DIR *d = opendir(dir);
    if (d == NULL) {
        record_first_error(stats, "failed to open directory %s", dir);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            scan_plugin_dependency_directory(ctx, path, stats);
        } else if (has_corpus_extension(entry->d_name)) {
            scan_plugin_dependencies_for_file(ctx, path, stats);
        }
    }

    closedir(d);
}
#endif

TEST(plugin_dependency_corpus, all_reference_files_resolve_plugin_dependencies)
{
    nmo_context_desc_t desc = {0};
    desc.data_dir = NMO_TEST_DATA_DIR;

    nmo_context_t *ctx = nmo_context_create(&desc);
    ASSERT_NOT_NULL(ctx);

    plugin_corpus_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    scan_plugin_dependency_directory(ctx, NMO_TEST_DATA_DIR, &stats);

    printf("  Plugin dependency corpus: seen=%zu loaded=%zu missing_files=%zu "
           "missing_entries=%zu null_guid_files=%zu load_errors=%zu\n",
           stats.files_seen, stats.files_loaded, stats.files_with_missing,
           stats.missing_entries, stats.files_with_null_guid, stats.load_errors);
    if (stats.first_error[0] != '\0') {
        printf("  First plugin dependency corpus error: %s\n", stats.first_error);
    }

    nmo_context_release(ctx);

    ASSERT_GE(stats.files_seen, 500u);
    ASSERT_EQ(stats.files_seen, stats.files_loaded);
    ASSERT_EQ(0u, stats.load_errors);
    ASSERT_EQ(0u, stats.files_with_missing);
    ASSERT_EQ(0u, stats.missing_entries);
    ASSERT_EQ(0u, stats.files_with_null_guid);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(plugin_dependency_corpus, all_reference_files_resolve_plugin_dependencies);
TEST_MAIN_END()
