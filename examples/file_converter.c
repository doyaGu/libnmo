/**
 * @file file_converter.c
 * @brief Example demonstrating file format conversion
 *
 * This example shows how to:
 * 1. Load a file
 * 2. Apply transformations
 * 3. Save in a different format or with different compression
 */

#include "nmo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int compress;
    int validate;
    int verbose;
} converter_options;

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr,
                "Usage: %s <input.nmo> <output.nmo> [--compress] [--validate]\n",
                argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    const char *output_file = argv[2];

    converter_options opts = {
        .compress = 0,
        .validate = 0,
        .verbose = 1,
    };

    // Parse options
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--compress") == 0) {
            opts.compress = 1;
        } else if (strcmp(argv[i], "--validate") == 0) {
            opts.validate = 1;
        }
    }

    printf("=== NMO File Converter ===\n\n");
    printf("Input:  %s\n", input_file);
    printf("Output: %s\n", output_file);
    if (opts.compress) printf("Options: compression enabled\n");
    if (opts.validate) printf("Options: validation enabled\n");
    printf("\n");

    // Create context
    printf("Creating context...\n");
    nmo_logger_t logger = opts.verbose ? nmo_logger_stderr() : nmo_logger_null();
    nmo_context_desc_t ctx_desc = {
        .allocator = NULL,
        .logger = &logger,
        .thread_pool_size = 4,
    };

    nmo_context_t *ctx = nmo_context_create(&ctx_desc);
    if (ctx == NULL) {
        fprintf(stderr, "Error: Failed to create context\n");
        return 1;
    }

    // Create session
    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) {
        fprintf(stderr, "Error: Failed to create session\n");
        nmo_context_release(ctx);
        return 1;
    }

    // Load input file
    printf("Loading input file...\n");
    nmo_load_options_t load_opts = nmo_load_options_default();
    if (opts.validate) {
        load_opts.flags |= NMO_LOAD_CHECK_DEPENDENCIES;
    }

    int load_result = nmo_load_file(session, input_file, &load_opts);

    if (load_result != NMO_OK) {
        fprintf(stderr, "Error loading file (%s)\n",
                nmo_error_string(load_result));
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 1;
    }
    printf("Input file loaded\n");

    // Apply transformations (if any)
    printf("Applying transformations...\n");
    // In production, transformations would be applied here
    printf("Transformations complete\n");

    // Save output file
    printf("Saving output file...\n");
    nmo_save_options_t save_opts = nmo_save_options_default();
    if (opts.compress) {
        save_opts.flags = NMO_SAVE_COMPRESSED;
    }
    save_opts.validate_before_write = opts.validate;

    int save_result = nmo_save_file(session, output_file, &save_opts);

    if (save_result != NMO_OK) {
        fprintf(stderr, "Error saving file (%s)\n",
                nmo_error_string(save_result));
        nmo_session_destroy(session);
        nmo_context_release(ctx);
        return 1;
    }

    printf("Output file saved successfully\n\n");

    // Clean up
    nmo_session_destroy(session);
    nmo_context_release(ctx);

    printf("Conversion complete.\n");
    return 0;
}
