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
#include "runtime/nmo_context.h"
#include "document/nmo_document_load.h"
#include "session/nmo_session.h"
#include "core/nmo_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

static int g_files_tested = 0;
static int g_truncations_tested = 0;

static int append_file_path(char ***files, size_t *count, const char *path) {
    if (files == NULL || count == NULL || path == NULL) return 0;

    size_t path_size = strlen(path) + 1u;
    char *path_copy = (char *)malloc(path_size);
    if (path_copy == NULL) return 0;
    memcpy(path_copy, path, path_size);

    char **new_files =
        (char **)realloc(*files, (*count + 1u) * sizeof(char *));
    if (new_files == NULL) {
        free(path_copy);
        return 0;
    }
    new_files[*count] = path_copy;
    *files = new_files;
    (*count)++;
    return 1;
}

static int has_composition_extension(const char *name) {
    size_t len = name ? strlen(name) : 0;
    if (len < 4 || name[len - 4] != '.') return 0;
    char a = (char)tolower((unsigned char)name[len - 3]);
    char b = (char)tolower((unsigned char)name[len - 2]);
    char c = (char)tolower((unsigned char)name[len - 1]);
    return (a == 'n' || a == 'c' || a == 'v') && b == 'm' && c == 'o';
}

static void try_load_truncated(const char *path, size_t trunc_size) {
    /* Read original file */
    FILE *f = fopen(path, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size <= 0) {
        fclose(f);
        return;
    }

    size_t actual_trunc = trunc_size;
    if (actual_trunc > (size_t)file_size) {
        actual_trunc = (size_t)file_size;
    }
    if (actual_trunc == 0) {
        fclose(f);
        return;
    }

    fseek(f, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc((size_t)file_size);
    if (!buf) {
        fclose(f);
        return;
    }
    size_t nread = fread(buf, 1, (size_t)file_size, f);
    fclose(f);
    if (nread < actual_trunc) {
        free(buf);
        return;
    }

    /* Write truncated copy to temp file */
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "fuzz_trunc_%zu.tmp", actual_trunc);

    f = fopen(temp_path, "wb");
    if (!f) {
        free(buf);
        return;
    }
    fwrite(buf, 1, actual_trunc, f);
    fclose(f);
    free(buf);

    /* Attempt to load -- error is expected, crash is not */
    nmo_context_t *ctx = nmo_context_create(NULL);
    if (!ctx) {
        remove(temp_path);
        return;
    }

    nmo_session_t *session = nmo_session_create(ctx);
    if (!session) {
        nmo_context_release(ctx);
        remove(temp_path);
        return;
    }

    /* Suppress logging for expected errors */
    int load_result = nmo_load_file(session, temp_path, NULL);
    if (load_result == NMO_OK) {
        const char *saved_path = "fuzz_trunc_roundtrip.tmp";
        remove(saved_path);
        ASSERT_EQ(NMO_OK, nmo_session_save_file(session, saved_path, NULL, NULL));

        nmo_session_t *reload = nmo_session_create(ctx);
        ASSERT_NOT_NULL(reload);
        ASSERT_EQ(NMO_OK, nmo_load_file(reload, saved_path, NULL));
        nmo_session_destroy(reload);
        remove(saved_path);
    }

    nmo_session_destroy(session);
    nmo_context_release(ctx);
    remove(temp_path);

    g_truncations_tested++;
}

static int fuzz_one_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fclose(f);

    if (file_size <= 0) return 0;

    g_files_tested++;

    /* Cover header boundaries, proportional cuts, and a one-byte-short file. */
    size_t points[] = {
        1u,
        4u,
        8u,
        16u,
        (size_t)(file_size * 25 / 100),
        (size_t)(file_size * 50 / 100),
        (size_t)(file_size * 75 / 100),
        (size_t)(file_size * 95 / 100),
        (size_t)file_size - 1u,
    };

    for (size_t i = 0; i < sizeof(points) / sizeof(points[0]); i++) {
        int duplicate = 0;
        for (size_t j = 0; j < i; ++j) {
            if (points[j] == points[i]) duplicate = 1;
        }
        if (!duplicate && points[i] > 0 && points[i] < (size_t)file_size) {
            try_load_truncated(path, points[i]);
        }
    }

    return 1;
}

#ifdef _WIN32
static void collect_nmo_files(const char *dir, char ***out_files, size_t *out_count) {
    /* Collect all supported composition files in this directory. */
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (!has_composition_extension(fd.cFileName)) continue;

            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir, fd.cFileName);

            (void)append_file_path(out_files, out_count, full_path);
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

        if (!has_composition_extension(entry->d_name)) continue;

        (void)append_file_path(out_files, out_count, full_path);
    }
    closedir(d);
}
#endif

TEST(truncation_fuzz, fuzz_all_nmo_files) {
    const char *data_dir = NMO_TEST_DATA_DIR;

    char **files = NULL;
    size_t file_count = 0;
    collect_nmo_files(data_dir, &files, &file_count);

    printf("  Found %zu .nmo/.cmo/.vmo files in %s\n", file_count, data_dir);
    ASSERT_TRUE(file_count >= 20);

    for (size_t i = 0; i < file_count; i++) {
        fuzz_one_file(files[i]);
        free(files[i]);
    }
    free(files);

    printf("  Fuzzed %d files, %d truncation attempts, no crashes\n",
           g_files_tested, g_truncations_tested);
    ASSERT_TRUE(g_files_tested >= 20);
    ASSERT_TRUE(g_truncations_tested >= 160);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST_WITH_TIMEOUT(truncation_fuzz, fuzz_all_nmo_files, 180.0);
TEST_MAIN_END()

