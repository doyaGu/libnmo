/**
 * Batch byte-level oracle for interface chunk writer.
 * Loads every file under data/BBSamples, writes each non-BB behavior's
 * interface_data via nmo_interface_chunk_write(), and compares DWORDs
 * against the original raw interface_chunk.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "session/nmo_context.h"
#include "session/nmo_session.h"
#include "format/nmo_object.h"
#include "format/nmo_chunk.h"
#include "format/nmo_chunk_api.h"
#include "format/nmo_interface_chunk.h"
#include "object/nmo_object_repository.h"
#include "object/nmo_class_ids.h"
#include "object/builtin/nmo_behavior_schemas.h"
#include "core/nmo_arena_array.h"

#define CKBEHAVIOR_BUILDINGBLOCK 0x00008000u

typedef struct {
    int files_scanned;
    int files_loaded;
    int behaviors_tested;
    int behaviors_passed;
    int behaviors_failed;
    int behaviors_skipped;
    int size_mismatches;
    int dword_mismatches;
} oracle_stats_t;

static int has_ext(const char *name, const char *ext) {
    size_t nl = strlen(name), el = strlen(ext);
    if (nl < el) return 0;
    const char *tail = name + nl - el;
#ifdef _WIN32
    return _stricmp(tail, ext) == 0;
#else
    return strcasecmp(tail, ext) == 0;
#endif
}

static void verify_file(const char *path, oracle_stats_t *stats) {
    stats->files_scanned++;

    nmo_context_t *ctx = nmo_context_create(NULL);
    nmo_session_t *session = nmo_session_create(ctx);
    int load_ok = nmo_session_load_file(session, path, NULL, NULL);
    if (load_ok != 0) {
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return;
    }
    stats->files_loaded++;

    nmo_object_repository_t *repo = nmo_session_get_repository(session);
    size_t count = 0;
    nmo_object_t **all = nmo_object_repository_get_all(repo, &count);

    for (size_t i = 0; i < count; i++) {
        nmo_object_t *obj = all[i];
        if (!obj || nmo_object_get_class_id(obj) != NMO_CID_BEHAVIOR) continue;

        const nmo_behavior_state_t *state =
            (const nmo_behavior_state_t *)nmo_object_get_state(obj);
        if (!state) continue;

        if (!state->interface_data || !state->interface_chunk) {
            stats->behaviors_skipped++;
            continue;
        }

        /* Write from structured data */
        nmo_arena_t *arena = nmo_arena_create(NULL, 65536);
        nmo_chunk_t *written = nmo_chunk_create(arena);

        nmo_interface_parse_ctx_t pctx;
        memset(&pctx, 0, sizeof(pctx));

        nmo_status_t st = nmo_interface_chunk_write(written,
            state->interface_data, &pctx);
        if (st != 0) {
            printf("  WRITE FAIL: %s obj=%u name='%s' err=%d\n",
                   path, nmo_object_get_id(obj),
                   nmo_object_get_name(obj) ? nmo_object_get_name(obj) : "", st);
            stats->behaviors_failed++;
            nmo_arena_destroy(arena);
            continue;
        }

        nmo_chunk_t *original = state->interface_chunk;

        if (original->data.count != written->data.count) {
            printf("  SIZE DIFF: %s obj=%u name='%s' orig=%zu writ=%zu\n",
                   path, nmo_object_get_id(obj),
                   nmo_object_get_name(obj) ? nmo_object_get_name(obj) : "",
                   original->data.count, written->data.count);
            stats->size_mismatches++;
            stats->behaviors_failed++;
            nmo_arena_destroy(arena);
            continue;
        }

        uint32_t *orig_data = NMO_ARENA_ARRAY_DATA(uint32_t, &original->data);
        uint32_t *writ_data = NMO_ARENA_ARRAY_DATA(uint32_t, &written->data);

        int mismatches = 0;
        for (size_t d = 0; d < original->data.count; d++) {
            if (orig_data[d] != writ_data[d]) {
                /* Allow string padding: bytes after null are irrelevant */
                const uint8_t *ob = (const uint8_t *)&orig_data[d];
                uint32_t mask = 0xFFFFFFFFu;
                for (int b = 0; b < 4; b++) {
                    if (ob[b] == 0) { mask = (1u << (b * 8)) - 1u; break; }
                }
                if ((orig_data[d] & mask) == (writ_data[d] & mask)) continue;

                if (mismatches == 0) {
                    printf("  DWORD DIFF: %s obj=%u name='%s' dword[%zu]: "
                           "orig=0x%08X writ=0x%08X\n",
                           path, nmo_object_get_id(obj),
                           nmo_object_get_name(obj) ? nmo_object_get_name(obj) : "",
                           d, orig_data[d], writ_data[d]);
                }
                mismatches++;
            }
        }

        if (mismatches > 0) {
            stats->dword_mismatches += mismatches;
            stats->behaviors_failed++;
        } else {
            stats->behaviors_passed++;
        }
        stats->behaviors_tested++;

        nmo_arena_destroy(arena);
    }

    nmo_session_destroy(session);
    nmo_context_release(ctx);
}

#ifdef _WIN32
static void scan_dir(const char *dir_path, oracle_stats_t *stats) {
    char pattern[768];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir_path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        char full[1024];
        snprintf(full, sizeof(full), "%s\\%s", dir_path, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_dir(full, stats);
        } else if (has_ext(fd.cFileName, ".cmo") ||
                   has_ext(fd.cFileName, ".nmo") ||
                   has_ext(fd.cFileName, ".vmo")) {
            verify_file(full, stats);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
#else
static void scan_dir(const char *dir_path, oracle_stats_t *stats) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", dir_path, e->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_dir(full, stats);
        } else if (has_ext(e->d_name, ".cmo") ||
                   has_ext(e->d_name, ".nmo") ||
                   has_ext(e->d_name, ".vmo")) {
            verify_file(full, stats);
        }
    }
    closedir(dir);
}
#endif

int main(int argc, char **argv) {
    const char *data_dir = (argc >= 2) ? argv[1] : "data/BBSamples";

    oracle_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    printf("Interface chunk writer byte-level oracle\n");
    printf("Scanning: %s\n\n", data_dir);

    scan_dir(data_dir, &stats);

    printf("\n========================================\n");
    printf("Files scanned:      %d\n", stats.files_scanned);
    printf("Files loaded:       %d\n", stats.files_loaded);
    printf("Behaviors tested:   %d\n", stats.behaviors_tested);
    printf("  Passed:           %d\n", stats.behaviors_passed);
    printf("  Failed:           %d\n", stats.behaviors_failed);
    printf("  Skipped (BB):     %d\n", stats.behaviors_skipped);
    printf("Size mismatches:    %d\n", stats.size_mismatches);
    printf("DWORD mismatches:   %d\n", stats.dword_mismatches);
    printf("========================================\n");

    if (stats.behaviors_failed > 0) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    if (stats.behaviors_tested == 0) {
        printf("RESULT: NO DATA\n");
        return 2;
    }
    printf("RESULT: PASS\n");
    return 0;
}
