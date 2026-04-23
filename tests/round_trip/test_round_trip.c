/**
 * @file test_round_trip.c
 * @brief Round-trip test framework (load -> save -> load -> compare)
 */

#include "../test_framework.h"
#include "session/nmo_context.h"
#include "core/nmo_logger.h"
#include "document/nmo_document_load.h"
#include "document/nmo_document_save.h"
#include "session/nmo_session.h"
#include "document/nmo_document_compare.h"
#include "core/nmo_error.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#endif

typedef struct sample_file_list {
    char **items;
    size_t count;
    size_t capacity;
} sample_file_list_t;

static void print_last_error_chain(void) {
    char buffer[2048];
    size_t needed = nmo_last_error_chain_copy(buffer, sizeof(buffer));
    if (needed > 0) {
        buffer[sizeof(buffer) - 1] = '\0';
        printf("  Error chain: %s\n", buffer);
    }
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

static int collect_runtime_load_stats(const nmo_session_t *session,
                                      const char *path,
                                      nmo_runtime_load_stats_t *out_stats) {
    if (session == NULL || out_stats == NULL) {
        return 1;
    }

    memset(out_stats, 0, sizeof(*out_stats));
    int stats_result = nmo_session_get_runtime_load_stats(session, out_stats);
    if (stats_result != NMO_OK) {
        printf("  FAILED: runtime load stats unavailable for %s (error %d)\n",
               path, stats_result);
        return 1;
    }

    return 0;
}

static int validate_runtime_load_no_regression(
    const nmo_runtime_load_stats_t *baseline,
    const nmo_runtime_load_stats_t *current,
    const char *baseline_path,
    const char *current_path) {
    if (baseline == NULL || current == NULL) {
        return 1;
    }

    int failed = 0;
    if (current->references.unresolved > baseline->references.unresolved) {
        failed = 1;
    }
    if (current->object_postload.errors > baseline->object_postload.errors) {
        failed = 1;
    }
    if (current->manager_errors > baseline->manager_errors) {
        failed = 1;
    }

    if (failed) {
        printf("  FAILED: runtime load regression after round-trip\n");
        printf("    baseline=%s\n", baseline_path);
        printf("      unresolved_refs=%u object_errors=%u manager_errors=%u\n",
               baseline->references.unresolved,
               baseline->object_postload.errors,
               baseline->manager_errors);
        printf("    current=%s\n", current_path);
        printf("      unresolved_refs=%u object_errors=%u manager_errors=%u\n",
               current->references.unresolved,
               current->object_postload.errors,
               current->manager_errors);
        return 1;
    }

    return 0;
}

static int run_round_trip(const char *input_path) {
    if (!file_exists(input_path)) {
        printf("  SKIP: %s not found\n", input_path);
        return 0;
    }

    const char *slash = strrchr(input_path, '/');
    const char *backslash = strrchr(input_path, '\\');
    const char *basename = slash;
    if (backslash != NULL && (basename == NULL || backslash > basename)) {
        basename = backslash;
    }
    if (basename == NULL) {
        basename = input_path;
    } else {
        basename++;
    }

    char safe_basename[256];
    size_t safe_len = 0;
    for (const char *p = basename; *p != '\0' && safe_len + 1 < sizeof(safe_basename); ++p) {
        char c = *p;
        if (c == '/' || c == '\\' || c == ':') {
            c = '_';
        }
        safe_basename[safe_len++] = c;
    }
    safe_basename[safe_len] = '\0';

    uint32_t path_hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *) input_path; *p != '\0'; ++p) {
        path_hash ^= (uint32_t) *p;
        path_hash *= 16777619u;
    }

    char temp_file[512];
    snprintf(temp_file, sizeof(temp_file), "roundtrip_%08x_%s", path_hash, safe_basename);

    nmo_logger_t stderr_logger = nmo_logger_stderr();
    nmo_context_desc_t ctx_desc = {
        .allocator = NULL,
        .logger = &stderr_logger,
        .thread_pool_size = 1,
    };

    nmo_context_t *ctx = nmo_context_create(&ctx_desc);
    if (ctx == NULL) {
        printf("  FAILED: Could not create context\n");
        return 1;
    }

    nmo_session_t *load1 = nmo_session_create(ctx);
    if (load1 == NULL) {
        printf("  FAILED: Could not create load1 session\n");
        nmo_context_release(ctx);
        return 1;
    }

    int result = nmo_load_file(load1, input_path, NULL);
    if (result != NMO_OK) {
        printf("  FAILED: Load failed for %s (error %d)\n", input_path, result);
        print_last_error_chain();
        nmo_session_destroy(load1);
        nmo_context_release(ctx);
        return 1;
    }
    nmo_runtime_load_stats_t baseline_stats;
    if (collect_runtime_load_stats(load1, input_path, &baseline_stats) != 0) {
        nmo_session_destroy(load1);
        nmo_context_release(ctx);
        return 1;
    }

    nmo_save_options_t save_opts = nmo_save_options_default();
    save_opts.flags |= NMO_SAVE_REQUIRE_SCHEMA;
    result = nmo_save_file(load1, temp_file, &save_opts);
    if (result != NMO_OK) {
        printf("  FAILED: Save failed for %s (error %d)\n", temp_file, result);
        print_last_error_chain();
        nmo_session_destroy(load1);
        nmo_context_release(ctx);
        remove(temp_file);
        return 1;
    }

    nmo_session_t *load2 = nmo_session_create(ctx);
    if (load2 == NULL) {
        printf("  FAILED: Could not create load2 session\n");
        nmo_session_destroy(load1);
        nmo_context_release(ctx);
        remove(temp_file);
        return 1;
    }

    result = nmo_load_file(load2, temp_file, NULL);
    if (result != NMO_OK) {
        printf("  FAILED: Reload failed for %s (error %d)\n", temp_file, result);
        print_last_error_chain();
        nmo_session_destroy(load2);
        nmo_session_destroy(load1);
        nmo_context_release(ctx);
        remove(temp_file);
        return 1;
    }
    nmo_runtime_load_stats_t roundtrip_stats;
    if (collect_runtime_load_stats(load2, temp_file, &roundtrip_stats) != 0) {
        nmo_session_destroy(load2);
        nmo_session_destroy(load1);
        nmo_context_release(ctx);
        remove(temp_file);
        return 1;
    }
    if (validate_runtime_load_no_regression(
            &baseline_stats, &roundtrip_stats, input_path, temp_file) != 0) {
        nmo_session_destroy(load2);
        nmo_session_destroy(load1);
        nmo_context_release(ctx);
        remove(temp_file);
        return 1;
    }

    nmo_comparison_result_t compare_result;
    nmo_comparison_result_init(&compare_result);
    int compare_err = nmo_session_compare(
        load1,
        load2,
        NMO_COMPARE_STRICT | NMO_COMPARE_VERBOSE,
        &compare_result);

    int passed = (compare_err == NMO_OK) && compare_result.match;
    if (!passed && compare_err == NMO_OK) {
        /* Strict mode: any diff is a failure */
    }
    if (!passed) {
        if (compare_err != NMO_OK) {
            printf("  FAILED: Comparison error %d\n", compare_err);
        } else {
            printf("  FAILED: Comparison mismatch\n");
            printf("%s", compare_result.report);
        }
    }

    nmo_session_destroy(load2);
    nmo_session_destroy(load1);
    nmo_context_release(ctx);

    if (passed) {
        remove(temp_file);
    } else {
        printf("  (Temp file preserved at: %s)\n", temp_file);
    }

    return passed ? 0 : 1;
}

static int has_supported_extension(const char *name) {
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        return 0;
    }
#ifdef _WIN32
    return (_stricmp(dot, ".nmo") == 0) || (_stricmp(dot, ".cmo") == 0) ||
           (_stricmp(dot, ".nms") == 0);
#else
    return (strcasecmp(dot, ".nmo") == 0) || (strcasecmp(dot, ".cmo") == 0) ||
           (strcasecmp(dot, ".nms") == 0);
#endif
}

static int should_skip_sample(const char *name) {
    (void)name;
    return 0;
}

static int sample_file_list_push(sample_file_list_t *list, const char *path) {
    if (list == NULL || path == NULL) {
        return -1;
    }

    if (list->count == list->capacity) {
        size_t new_capacity = (list->capacity == 0) ? 16 : list->capacity * 2;
        char **new_items = (char **) realloc(list->items, new_capacity * sizeof(char *));
        if (new_items == NULL) {
            return -1;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }

    size_t len = strlen(path);
    char *copy = (char *) malloc(len + 1);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, path, len + 1);
    list->items[list->count++] = copy;
    return 0;
}

static void sample_file_list_free(sample_file_list_t *list) {
    if (list == NULL) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int compare_paths(const void *a, const void *b) {
    const char *pa = *(const char * const *) a;
    const char *pb = *(const char * const *) b;
#ifdef _WIN32
    return _stricmp(pa, pb);
#else
    return strcasecmp(pa, pb);
#endif
}

static int collect_sample_files_recursive(sample_file_list_t *list, const char *dir_path) {
    if (list == NULL || dir_path == NULL) {
        return -1;
    }

#ifdef _WIN32
    char pattern[768];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir_path);

    WIN32_FIND_DATAA find_data;
    HANDLE handle = FindFirstFileA(pattern, &find_data);
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }

    do {
        if (strcmp(find_data.cFileName, ".") == 0 ||
            strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s\\%s", dir_path, find_data.cFileName);

        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (collect_sample_files_recursive(list, full_path) != 0) {
                FindClose(handle);
                return -1;
            }
            continue;
        }

        if (!has_supported_extension(find_data.cFileName) ||
            should_skip_sample(find_data.cFileName)) {
            continue;
        }

        if (sample_file_list_push(list, full_path) != 0) {
            FindClose(handle);
            return -1;
        }
    } while (FindNextFileA(handle, &find_data) != 0);

    FindClose(handle);
#else
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return -1;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) {
            closedir(dir);
            return -1;
        }

        if (S_ISDIR(st.st_mode)) {
            if (collect_sample_files_recursive(list, full_path) != 0) {
                closedir(dir);
                return -1;
            }
            continue;
        }

        if (!has_supported_extension(entry->d_name) ||
            should_skip_sample(entry->d_name)) {
            continue;
        }

        if (sample_file_list_push(list, full_path) != 0) {
            closedir(dir);
            return -1;
        }
    }
    closedir(dir);
#endif

    return 0;
}

static int collect_sample_files(sample_file_list_t *list) {
    if (collect_sample_files_recursive(list, NMO_TEST_DATA_DIR) != 0) {
        return -1;
    }

    if (list->count > 1) {
        qsort(list->items, list->count, sizeof(char *), compare_paths);
    }

    return 0;
}

static int copy_file_bytes(const char *src_path, const char *dst_path) {
    if (src_path == NULL || dst_path == NULL) {
        return -1;
    }

    FILE *src = fopen(src_path, "rb");
    if (src == NULL) {
        return -1;
    }

    FILE *dst = fopen(dst_path, "wb");
    if (dst == NULL) {
        fclose(src);
        return -1;
    }

    char buffer[4096];
    while (!feof(src)) {
        size_t read_bytes = fread(buffer, 1, sizeof(buffer), src);
        if (read_bytes > 0) {
            size_t written = fwrite(buffer, 1, read_bytes, dst);
            if (written != read_bytes) {
                fclose(dst);
                fclose(src);
                return -1;
            }
        }
        if (ferror(src)) {
            fclose(dst);
            fclose(src);
            return -1;
        }
    }

    fclose(dst);
    fclose(src);
    return 0;
}

static int append_synthetic_samples(sample_file_list_t *list,
                                    size_t source_count,
                                    size_t count) {
    if (list == NULL || source_count == 0 || source_count > list->count) {
        return -1;
    }

    for (size_t i = 0; i < count; ++i) {
        const char *source = list->items[i % source_count];
        const char *dot = strrchr(source, '.');
        const char *ext = (dot != NULL) ? dot : ".nmo";

        char path[128];
        snprintf(path, sizeof(path), "synthetic_roundtrip_%02zu%s", i + 1, ext);
        remove(path);

        if (copy_file_bytes(source, path) != 0) {
            return -1;
        }

        if (sample_file_list_push(list, path) != 0) {
            remove(path);
            return -1;
        }
    }

    return 0;
}

static size_t get_min_expected_samples(void) {
    const char *env_value = getenv("NMO_ROUNDTRIP_MIN_SAMPLES");
    if (env_value == NULL || env_value[0] == '\0') {
        return 50u;
    }

    char *end = NULL;
    unsigned long value = strtoul(env_value, &end, 10);
    if (end == env_value || *end != '\0') {
        return 50u;
    }
    return (size_t) value;
}

static size_t get_env_size_value(const char *name, size_t fallback) {
    const char *env_value = getenv(name);
    if (env_value == NULL || env_value[0] == '\0') {
        return fallback;
    }

    char *end = NULL;
    unsigned long value = strtoul(env_value, &end, 10);
    if (end == env_value || *end != '\0') {
        return fallback;
    }
    return (size_t) value;
}

TEST(round_trip, sample_files) {
    sample_file_list_t files = {0};
    ASSERT_EQ(collect_sample_files(&files), 0);
    size_t discovered_count = files.count;
    ASSERT_GT(discovered_count, 0u);

    ASSERT_EQ(append_synthetic_samples(&files, discovered_count, 10u), 0);

    size_t min_expected = get_min_expected_samples();
    ASSERT_GE(files.count, min_expected);

    size_t start_index = get_env_size_value("NMO_ROUNDTRIP_START", 0u);
    size_t max_count = get_env_size_value("NMO_ROUNDTRIP_COUNT", 0u);
    if (start_index > files.count) {
        start_index = files.count;
    }
    size_t end_index = files.count;
    if (max_count > 0u && start_index + max_count < end_index) {
        end_index = start_index + max_count;
    }
    size_t run_count = (end_index > start_index) ? (end_index - start_index) : 0u;

    int failures = 0;

    printf("Round-trip sample count: %zu (real: %zu, synthetic: %zu, min expected: %zu)\n",
           files.count, discovered_count, files.count - discovered_count, min_expected);
    if (start_index > 0u || max_count > 0u) {
        printf("Round-trip range: [%zu, %zu) of %zu\n", start_index, end_index, files.count);
    }
    printf("Round-trip run count: %zu\n", run_count);

    for (size_t i = start_index; i < end_index; i++) {
        printf("\nRound-trip: %s\n", files.items[i]);
        if (run_round_trip(files.items[i]) != 0) {
            failures++;
        }
    }

    for (size_t i = discovered_count; i < files.count; ++i) {
        remove(files.items[i]);
    }
    sample_file_list_free(&files);
    ASSERT_EQ(failures, 0);
}

TEST_MAIN_BEGIN()
    /* Full-corpus round-trip runs can exceed the default 30s unit timeout. */
    REGISTER_TEST_WITH_TIMEOUT(round_trip, sample_files, 180.0);
TEST_MAIN_END()
