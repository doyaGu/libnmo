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

    // Step 2: Create an empty document
    printf("Creating document...\n");
    nmo_document_t *document = nmo_document_create(ctx);
    if (document == NULL) {
        fprintf(stderr, "Error: Failed to create document\n");
        nmo_context_release(ctx);
        return 1;
    }
    printf("Document created successfully\n\n");

    // Step 3: Add an object through a workspace edit (an empty document
    // cannot be saved)
    printf("Setting up objects...\n");
    nmo_workspace_t *workspace = NULL;
    nmo_workspace_edit_t *edit = NULL;
    nmo_object_id_t camera_id = 0;
    nmo_status_t result = nmo_workspace_create(ctx, document, &workspace);
    if (result == NMO_OK) {
        result = nmo_workspace_edit_begin(workspace, "create camera", &edit);
    }
    if (result == NMO_OK) {
        nmo_object_create_desc_t desc = {
            .class_id = NMO_CID_CAMERA,
            .name = "Camera",
            .type_guid = NMO_GUID_NULL,
        };
        result = nmo_object_edit_create(edit, &desc, &camera_id);
        if (result == NMO_OK) {
            result = nmo_workspace_edit_commit(edit);
        } else {
            nmo_workspace_edit_rollback(edit);
        }
    }
    if (result != NMO_OK) {
        fprintf(stderr, "Error: Failed to create an object (%s)\n",
                nmo_error_string(result));
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return 1;
    }
    printf("Created camera (ID: %u)\n\n", camera_id);

    // Step 4: Save the file
    printf("Saving file: %s\n", output_file);
    result = nmo_document_save_file(document, output_file, NULL);

    if (result != NMO_OK) {
        fprintf(stderr, "Error: Failed to save file (%s)\n",
                nmo_error_string(result));
        nmo_workspace_destroy(workspace);
        nmo_document_destroy(document);
        nmo_context_release(ctx);
        return 1;
    }
    printf("File saved successfully!\n\n");

    // Step 5: Clean up
    printf("Cleaning up...\n");
    nmo_workspace_destroy(workspace);
    nmo_document_destroy(document);
    nmo_context_release(ctx);
    printf("Done.\n");

    return 0;
}
