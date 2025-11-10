/**
 * @file simple_save.c
 * @brief Simple example demonstrating how to save an NMO file
 */

#include "nmo.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <output.nmo>\n", argv[0]);
        return 1;
    }

    const char *output_file = argv[1];

    printf("=== Simple NMO File Saver ===\n\n");

    // Step 1: Create context
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

    // Step 2: Create a session
    printf("Creating session...\n");
    nmo_session *session = nmo_session_create(ctx);
    if (session == NULL) {
        fprintf(stderr, "Error: Failed to create session\n");
        nmo_context_release(ctx);
        return 1;
    }
    printf("Session created successfully\n\n");

    // Step 3: (In a real application) Add objects to the session
    printf("Setting up objects...\n");
    // In production, you would add objects to the session here
    printf("Objects ready\n\n");

    // Step 4: Save the file
    printf("Saving file: %s\n", output_file);
    nmo_result result =
        nmo_save_file(session, output_file, NMO_SAVE_DEFAULT);

    if (result.code != NMO_OK) {
        if (result.error != NULL) {
            fprintf(stderr, "Error: %s\n",
                    nmo_error_message(result.error));
            nmo_error_destroy(result.error);
        } else {
            fprintf(stderr, "Error: Failed to save file (error code %d)\n",
                    result.code);
        }
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 1;
    }
    printf("File saved successfully!\n\n");

    // Step 5: Clean up
    printf("Cleaning up...\n");
    nmo_session_destroy(session);
    nmo_context_release(ctx);
    printf("Done.\n");

    return 0;
}
