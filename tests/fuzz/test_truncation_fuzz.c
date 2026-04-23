/**
 * @file test_truncation_fuzz.c
 * @brief Deterministic fuzz test: truncate real .nmo files at various points
 *        and verify no crashes.
 *
 * For each .nmo file in the data/ directory, creates truncated copies at
 * 25%, 50%, 75%, and 95% of the original size, then attempts to load them.
 * The test passes if no crash or ASan violation occurs; error returns are
 * expected and acceptable.
 */

#include "../test_framework.h"
#include "session/nmo_context.h"
#include "document/nmo_document_load.h"
#include "session/nmo_session.h"
#include "core/nmo_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

static int g_files_tested = 0;
static int g_truncations_tested = 0;

static int try_load_truncated(const char *path, size_t trunc_size) {
    /* Read original file */
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size <= 0) {
        fclose(f);
        return 0;
    }

    size_t actual_trunc = trunc_size;
    if (actual_trunc > (size_t)file_size) {
        actual_trunc = (size_t)file_size;
    }
    if (actual_trunc == 0) {
        fclose(f);
        return 0;
    }

    fseek(f, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc((size_t)file_size);
    if (!buf) {
        fclose(f);
        return 0;
    }
    size_t nread = fread(buf, 1, (size_t)file_size, f);
    fclose(f);
    if (nread < actual_trunc) {
        free(buf);
        return 0;
    }

    /* Write truncated copy to temp file */
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "fuzz_trunc_%zu.tmp", actual_trunc);

    f = fopen(temp_path, "wb");
    if (!f) {
        free(buf);
        return 0;
    }
    fwrite(buf, 1, actual_trunc, f);
    fclose(f);
    free(buf);

    /* Attempt to load -- error is expected, crash is not */
    nmo_context_t *ctx = nmo_context_create(NULL);
    if (!ctx) {
        remove(temp_path);
        return 0;
    }

    nmo_session_t *session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        remove(temp_path);
        return 0;
    }

    /* Suppress logging for expected errors */
    (void)nmo_load_file(session, temp_path, NULL);
    /* We don't check the return value -- errors are expected */

    nmo_session_destroy(session);
    nmo_context_release(ctx);
    remove(temp_path);

    g_truncations_tested++;
    return 1;
}

static int fuzz_one_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fclose(f);

    if (file_size <= 0) return 0;

    g_files_tested++;

    /* Truncation points: 25%, 50%, 75%, 95% */
    size_t points[] = {
        (size_t)(file_size * 25 / 100),
        (size_t)(file_size * 50 / 100),
        (size_t)(file_size * 75 / 100),
        (size_t)(file_size * 95 / 100),
    };

    for (size_t i = 0; i < 4; i++) {
        if (points[i] > 0) {
            try_load_truncated(path, points[i]);
        }
    }

    return 1;
}

#ifdef _WIN32
static void collect_nmo_files(const char *dir, char ***out_files, size_t *out_count) {
    /* Collect .nmo files in this directory */
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*.nmo", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir, fd.cFileName);

            *out_files = (char **)realloc(*out_files, (*out_count + 1) * sizeof(char *));
            (*out_files)[*out_count] = strdup(full_path);
            (*out_count)++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    /* Recurse into subdirectories */
    char dir_pattern[512];
    snprintf(dir_pattern, sizeof(dir_pattern), "%s\\*", dir);

    h = FindFirstFileA(dir_pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        char subdir[512];
        snprintf(subdir, sizeof(subdir), "%s/%s", dir, fd.cFileName);
        collect_nmo_files(subdir, out_files, out_count);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
#else
static void collect_nmo_files(const char *dir, char ***out_files, size_t *out_count) {
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            collect_nmo_files(full_path, out_files, out_count);
            continue;
        }

        size_t len = strlen(entry->d_name);
        if (len < 4) continue;
        if (strcmp(entry->d_name + len - 4, ".nmo") != 0) continue;

        *out_files = (char **)realloc(*out_files, (*out_count + 1) * sizeof(char *));
        (*out_files)[*out_count] = strdup(full_path);
        (*out_count)++;
    }
    closedir(d);
}
#endif

TEST(truncation_fuzz, fuzz_all_nmo_files) {
    const char *data_dir = NMO_TEST_DATA_DIR;

    char **files = NULL;
    size_t file_count = 0;
    collect_nmo_files(data_dir, &files, &file_count);

    printf("  Found %zu .nmo files in %s\n", file_count, data_dir);
    ASSERT_TRUE(file_count >= 20);

    for (size_t i = 0; i < file_count; i++) {
        fuzz_one_file(files[i]);
        free(files[i]);
    }
    free(files);

    printf("  Fuzzed %d files, %d truncation attempts, no crashes\n",
           g_files_tested, g_truncations_tested);
    ASSERT_TRUE(g_files_tested >= 20);
    ASSERT_TRUE(g_truncations_tested >= 80); /* 20 files * 4 truncation points */
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(truncation_fuzz, fuzz_all_nmo_files);
TEST_MAIN_END()
