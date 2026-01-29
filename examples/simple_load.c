/**
 * @file simple_load.c
 * @brief Simple example demonstrating how to load an NMO file
 */

#include "nmo.h"
#include "app/nmo_parser.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.nmo>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];

    printf("=== Simple NMO File Loader ===\n\n");

    // Step 1: Create context with default allocator and stderr logger
    printf("Creating context...\n");
    nmo_logger_t logger = nmo_logger_stderr();
    nmo_context_desc_t ctx_desc = {
        .allocator = NULL,  // Use default allocator
        .logger = &logger,
        .thread_pool_size = 4,
    };

    nmo_context_t *ctx = nmo_context_create(&ctx_desc);
    if (ctx == NULL) {
        fprintf(stderr, "Error: Failed to create context\n");
        return 1;
    }
    printf("Context created successfully\n\n");

    // Step 2: Create a session
    printf("Creating session...\n");
    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) {
        fprintf(stderr, "Error: Failed to create session\n");
        nmo_context_release(ctx);
        return 1;
    }
    printf("Session created successfully\n\n");

    // Step 3: Load the NMO file
    printf("Loading file: %s\n", filename);
    int result = nmo_load_file(session, filename, NMO_LOAD_DEFAULT);

    if (result != NMO_OK) {
        fprintf(stderr, "Error: Failed to load file (%s)\n",
                nmo_error_string(result));
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 1;
    }
    printf("File loaded successfully!\n\n");

    // Step 4: Access loaded data
    printf("File contents:\n");
    nmo_object_repository_t *repo =
        nmo_session_get_repository(session);
    if (repo != NULL) {
        size_t count = nmo_object_repository_get_count(repo);
        printf("  Total objects: %zu\n", count);
    }
    printf("\n");

    // Step 5: Clean up
    printf("Cleaning up...\n");
    nmo_session_destroy(session);
    nmo_context_release(ctx);
    printf("Done.\n");

    return 0;
}
