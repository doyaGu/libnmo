/**
 * @file object_inspector.c
 * @brief Example demonstrating object inspection and traversal
 *
 * This example shows how to:
 * 1. Load a file
 * 2. Traverse the object repository
 * 3. Inspect individual objects
 */

#include "nmo.h"
#include <stdio.h>
#include <stdlib.h>

void print_object_info(nmo_object *obj, int index) {
    if (obj == NULL) {
        return;
    }

    printf("  [%d] Object:\n", index);
    uint32_t id = nmo_object_get_id(obj);
    printf("      ID: %u\n", id);

    const char *name = nmo_object_get_name(obj);
    if (name != NULL) {
        printf("      Name: %s\n", name);
    }

    // Additional object information can be printed here
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.nmo>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];

    printf("=== Object Inspector ===\n\n");

    // Create context
    printf("Creating context...\n");
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

    // Create session
    nmo_session *session = nmo_session_create(ctx);
    if (session == NULL) {
        fprintf(stderr, "Error: Failed to create session\n");
        nmo_context_release(ctx);
        return 1;
    }

    // Load file
    printf("Loading file: %s\n\n", filename);
    nmo_result result = nmo_load_file(session, filename, NMO_LOAD_DEFAULT);

    if (result.code != NMO_OK) {
        if (result.error != NULL) {
            fprintf(stderr, "Error: %s\n",
                    nmo_error_message(result.error));
            nmo_error_destroy(result.error);
        } else {
            fprintf(stderr, "Error: Failed to load file (code %d)\n",
                    result.code);
        }
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 1;
    }

    // Get object repository
    nmo_object_repository *repo =
        nmo_session_get_object_repository(session);

    if (repo == NULL) {
        fprintf(stderr, "Error: No object repository\n");
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 1;
    }

    // Count objects
    uint32_t object_count = nmo_object_repository_count(repo);
    printf("Objects in file: %u\n\n", object_count);

    // Iterate through objects
    printf("Object details:\n");
    for (uint32_t i = 0; i < object_count; i++) {
        nmo_object *obj = nmo_object_repository_get_by_index(repo, i);
        if (obj != NULL) {
            print_object_info(obj, i);
        }
    }
    printf("\n");

    // Clean up
    printf("Cleaning up...\n");
    nmo_session_destroy(session);
    nmo_context_release(ctx);

    printf("Done.\n");
    return 0;
}
