/**
 * @file nmo-diff.c
 * @brief CLI tool to compare two NMO files
 *
 * Usage: nmo-diff <file1.nmo> <file2.nmo>
 */

#include "nmo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    nmo_context *ctx;
    nmo_session *session;
    const char *filename;
} file_context;

static int load_file(const char *filename, file_context *fctx) {
    // Create context
    nmo_context_desc ctx_desc = {
        .allocator = NULL,
        .logger = nmo_logger_null(),
        .thread_pool_size = 4,
    };

    fctx->ctx = nmo_context_create(&ctx_desc);
    if (fctx->ctx == NULL) {
        fprintf(stderr, "Error: Failed to create context for %s\n", filename);
        return 0;
    }

    // Create session
    fctx->session = nmo_session_create(fctx->ctx);
    if (fctx->session == NULL) {
        fprintf(stderr, "Error: Failed to create session for %s\n", filename);
        nmo_context_release(fctx->ctx);
        return 0;
    }

    // Load file
    nmo_result result = nmo_load_file(fctx->session, filename, NMO_LOAD_DEFAULT);
    fctx->filename = filename;

    if (result.code != NMO_OK) {
        if (result.error != NULL) {
            fprintf(stderr, "Error loading %s: %s\n", filename,
                    nmo_error_message(result.error));
            nmo_error_destroy(result.error);
        } else {
            fprintf(stderr, "Error loading %s (error code %d)\n", filename,
                    result.code);
        }
        nmo_session_destroy(fctx->session);
        nmo_context_release(fctx->ctx);
        return 0;
    }

    return 1;
}

static void cleanup_context(file_context *fctx) {
    if (fctx->session != NULL) {
        nmo_session_destroy(fctx->session);
    }
    if (fctx->ctx != NULL) {
        nmo_context_release(fctx->ctx);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <file1.nmo> <file2.nmo>\n", argv[0]);
        return 1;
    }

    const char *file1 = argv[1];
    const char *file2 = argv[2];

    printf("Comparing files:\n");
    printf("  File 1: %s\n", file1);
    printf("  File 2: %s\n", file2);

    // Load both files
    file_context fctx1 = {0};
    file_context fctx2 = {0};

    if (!load_file(file1, &fctx1)) {
        return 1;
    }

    if (!load_file(file2, &fctx2)) {
        cleanup_context(&fctx1);
        return 1;
    }

    // Compare object repositories
    nmo_object_repository *repo1 = nmo_session_get_object_repository(fctx1.session);
    nmo_object_repository *repo2 = nmo_session_get_object_repository(fctx2.session);

    uint32_t count1 = 0;
    uint32_t count2 = 0;

    if (repo1 != NULL) {
        count1 = nmo_object_repository_count(repo1);
    }
    if (repo2 != NULL) {
        count2 = nmo_object_repository_count(repo2);
    }

    printf("\nObject counts:\n");
    printf("  File 1: %u objects\n", count1);
    printf("  File 2: %u objects\n", count2);

    if (count1 != count2) {
        printf("\nDifference: Files have different object counts\n");
    } else {
        printf("\nSame: Files have the same number of objects\n");
    }

    // Clean up
    cleanup_context(&fctx1);
    cleanup_context(&fctx2);

    return 0;
}
