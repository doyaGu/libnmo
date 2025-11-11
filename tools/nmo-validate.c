/**
 * @file nmo-validate.c
 * @brief CLI tool to validate NMO file integrity
 *
 * Usage: nmo-validate <file.nmo>
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

    // Create session
    nmo_session *session = nmo_session_create(ctx);
    if (session == NULL) {
        fprintf(stderr, "Error: Failed to create session\n");
        nmo_context_release(ctx);
        return 1;
    }

    // Load file with validation
    printf("Validating file: %s\n", filename);
    nmo_result result = nmo_load_file(session, filename,
                                       NMO_LOAD_DEFAULT | NMO_LOAD_VALIDATE);

    if (result.code != NMO_OK) {
        if (result.error != NULL) {
            fprintf(stderr, "Validation failed: %s\n",
                    nmo_error_message(result.error));
            nmo_error_destroy(result.error);
        } else {
            fprintf(stderr, "Validation failed (error code %d)\n", result.code);
        }
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 1;
    }

    printf("File validation passed!\n");

    // Clean up
    nmo_session_destroy(session);
    nmo_context_release(ctx);

    return 0;
}
