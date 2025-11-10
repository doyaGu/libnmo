/**
 * @file nmo-convert.c
 * @brief CLI tool to convert between NMO file formats
 *
 * Usage: nmo-convert <input.nmo> <output.nmo> [--compress]
 */

#include "nmo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.nmo> <output.nmo> [--compress]\n",
                argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    const char *output_file = argv[2];
    int use_compression = 0;

    // Check for --compress flag
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--compress") == 0) {
            use_compression = 1;
        }
    }

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

    // Load input file
    printf("Loading: %s\n", input_file);
    nmo_result load_result = nmo_load_file(session, input_file, NMO_LOAD_DEFAULT);

    if (load_result.code != NMO_OK) {
        if (load_result.error != NULL) {
            fprintf(stderr, "Error loading file: %s\n",
                    nmo_error_message(load_result.error));
            nmo_error_destroy(load_result.error);
        } else {
            fprintf(stderr, "Error loading file (error code %d)\n",
                    load_result.code);
        }
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 1;
    }

    // Save output file
    printf("Converting to: %s\n", output_file);
    int save_flags = NMO_SAVE_DEFAULT;
    if (use_compression) {
        save_flags |= NMO_SAVE_COMPRESS;
        printf("Using compression\n");
    }

    nmo_result save_result = nmo_save_file(session, output_file, save_flags);

    if (save_result.code != NMO_OK) {
        if (save_result.error != NULL) {
            fprintf(stderr, "Error saving file: %s\n",
                    nmo_error_message(save_result.error));
            nmo_error_destroy(save_result.error);
        } else {
            fprintf(stderr, "Error saving file (error code %d)\n",
                    save_result.code);
        }
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 1;
    }

    printf("Conversion complete!\n");

    // Clean up
    nmo_session_destroy(session);
    nmo_context_release(ctx);

    return 0;
}
