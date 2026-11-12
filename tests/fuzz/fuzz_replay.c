/**
 * @file fuzz_replay.c
 * @brief main() for builds without libFuzzer: run a harness once over each file.
 *
 * Usage: <harness> <file-or-directory>...
 *
 * A directory stands for the regular files directly inside it. A path that does
 * not exist is skipped, so CTest can name directories that only some checkouts
 * have. Exit status: 0 when at least one input ran, 77 (CTest's skip) when none
 * did, and a crash or abort from the harness when an input breaks it.
 */

#include "fuzz_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

static size_t g_inputs = 0;

static char *join_path(const char *dir, const char *name) {
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    char *path = (char *)malloc(dir_len + 1u + name_len + 1u);
    if (path == NULL) {
        return NULL;
    }
    memcpy(path, dir, dir_len);
    path[dir_len] = '/';
    memcpy(path + dir_len + 1u, name, name_len + 1u);
    return path;
}

static void run_file(const char *path) {
    size_t size = 0;
    uint8_t *data = fuzz_read_file(path, &size);
    if (data == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(1);
    }
    printf("%s (%zu bytes)\n", path, size);
    fflush(stdout);
    LLVMFuzzerTestOneInput(data, size);
    free(data);
    g_inputs++;
}

#ifdef _WIN32

static int is_directory(const char *path) {
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static int path_exists(const char *path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void run_directory(const char *dir) {
    char *pattern = join_path(dir, "*");
    if (pattern == NULL) {
        exit(1);
    }
    WIN32_FIND_DATAA entry;
    HANDLE find = FindFirstFileA(pattern, &entry);
    free(pattern);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        char *path = join_path(dir, entry.cFileName);
        if (path == NULL) {
            exit(1);
        }
        run_file(path);
        free(path);
    } while (FindNextFileA(find, &entry));
    FindClose(find);
}

#else

static int is_directory(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static int path_exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0;
}

static int compare_names(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void run_directory(const char *dir) {
    DIR *handle = opendir(dir);
    if (handle == NULL) {
        return;
    }
    char **paths = NULL;
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(handle)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char *path = join_path(dir, entry->d_name);
        if (path == NULL) {
            exit(1);
        }
        struct stat info;
        if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
            free(path);
            continue;
        }
        char **grown = (char **)realloc(paths, (count + 1u) * sizeof(*paths));
        if (grown == NULL) {
            exit(1);
        }
        paths = grown;
        paths[count++] = path;
    }
    closedir(handle);
    /* readdir order differs between file systems; keep runs comparable. */
    if (count > 1) {
        qsort(paths, count, sizeof(*paths), compare_names);
    }
    for (size_t i = 0; i < count; i++) {
        run_file(paths[i]);
        free(paths[i]);
    }
    free(paths);
}

#endif

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!path_exists(argv[i])) {
            printf("%s: not present, skipped\n", argv[i]);
            continue;
        }
        if (is_directory(argv[i])) {
            run_directory(argv[i]);
        } else {
            run_file(argv[i]);
        }
    }
    printf("%zu inputs\n", g_inputs);
    return g_inputs > 0 ? 0 : 77;
}
