/**
 * @file nmo-inspect.c
 * @brief CLI tool to inspect NMO file contents
 *
 * Usage: nmo-inspect <file.nmo>
 */

#include "nmo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.nmo>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];

    // Create context
    nmo_context_desc ctx_desc = {
        .allocator = NULL,
        .logger = nmo_logger_stderr(),
        .thread_pool_size = 4,
    };

    nmo_context *ctx = nmo_context_create(&ctx_desc);
    if (ctx == NULL) {
        fprintf(stderr, "Error: Failed to create context\n");
        return 1;
    }

    // Register built-in schemas
    nmo_schema_registry *registry = nmo_context_get_schema_registry(ctx);
    if (registry == NULL) {
        fprintf(stderr, "Error: Failed to get schema registry\n");
        nmo_context_release(ctx);
        return 1;
    }

    // Create session
    nmo_session *session = nmo_session_create(ctx);
    if (session == NULL) {
        fprintf(stderr, "Error: Failed to create session\n");
        nmo_context_release(ctx);
        return 1;
    }

    // Load file
    printf("Inspecting file: %s\n", filename);
    nmo_result result = nmo_load_file(session, filename, NMO_LOAD_DEFAULT);

    if (result.code != NMO_OK) {
        if (result.error != NULL) {
            fprintf(stderr, "Error: %s\n", nmo_error_message(result.error));
            nmo_error_destroy(result.error);
        } else {
            fprintf(stderr, "Error: Failed to load file (error code %d)\n",
                    result.code);
        }
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 1;
    }

    printf("File loaded successfully\n");

    // Get object repository
    nmo_object_repository *repo = nmo_session_get_object_repository(session);
    if (repo != NULL) {
        uint32_t object_count = nmo_object_repository_count(repo);
        printf("Objects: %u\n", object_count);
    }

    // Clean up
    nmo_session_destroy(session);
    nmo_context_release(ctx);

    printf("Done.\n");
    return 0;
}
