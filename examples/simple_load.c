/**
 * @file simple_load.c
 * @brief Simple example demonstrating how to load an NMO file
 */

#include "nmo.h"
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
    nmo_context_desc ctx_desc = {
        .allocator = NULL,  // Use default allocator
        .logger = nmo_logger_stderr(),
        .thread_pool_size = 4,
    };

    nmo_context *ctx = nmo_context_create(&ctx_desc);
    if (ctx == NULL) {
        fprintf(stderr, "Error: Failed to create context\n");
        return 1;
    }
    printf("Context created successfully\n\n");

    // Step 2: Get schema registry and register built-in schemas
    printf("Registering built-in schemas...\n");
    nmo_schema_registry *registry = nmo_context_get_schema_registry(ctx);
    if (registry == NULL) {
        fprintf(stderr, "Error: Failed to get schema registry\n");
        nmo_context_release(ctx);
        return 1;
    }
    // In production, register schemas here
    printf("Schema registry ready\n\n");

    // Step 3: Create a session
    printf("Creating session...\n");
    nmo_session *session = nmo_session_create(ctx);
    if (session == NULL) {
        fprintf(stderr, "Error: Failed to create session\n");
        nmo_context_release(ctx);
        return 1;
    }
    printf("Session created successfully\n\n");

    // Step 4: Load the NMO file
    printf("Loading file: %s\n", filename);
    nmo_result result =
        nmo_load_file(session, filename, NMO_LOAD_DEFAULT);

    if (result.code != NMO_OK) {
        if (result.error != NULL) {
            fprintf(stderr, "Error: %s\n",
                    nmo_error_message(result.error));
            nmo_error_destroy(result.error);
        } else {
            fprintf(stderr, "Error: Failed to load file (error code %d)\n",
                    result.code);
        }
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 1;
    }
    printf("File loaded successfully!\n\n");

    // Step 5: Access loaded data
    printf("File contents:\n");
    nmo_object_repository *repo =
        nmo_session_get_object_repository(session);
    if (repo != NULL) {
        uint32_t count = nmo_object_repository_count(repo);
        printf("  Total objects: %u\n", count);
    }
    printf("\n");

    // Step 6: Clean up
    printf("Cleaning up...\n");
    nmo_session_destroy(session);
    nmo_context_release(ctx);
    printf("Done.\n");

    return 0;
}
