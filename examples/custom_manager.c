/**
 * @file custom_manager.c
 * @brief Example demonstrating custom manager creation
 *
 * This example shows how to:
 * 1. Create a custom manager
 * 2. Register it with the context
 * 3. Use it in a session
 */

#include "nmo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    printf("=== Custom Manager Example ===\n\n");

    // Step 1: Create context
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
    printf("Context created\n\n");

    // Step 2: Get manager registry
    printf("Accessing manager registry...\n");
    nmo_manager_registry *manager_registry =
        nmo_context_get_manager_registry(ctx);
    if (manager_registry == NULL) {
        fprintf(stderr, "Error: Failed to get manager registry\n");
        nmo_context_release(ctx);
        return 1;
    }
    printf("Manager registry accessed\n\n");

    // Step 3: Create a custom manager
    printf("Creating custom manager...\n");
    nmo_guid manager_guid = nmo_guid_create();

    nmo_manager_desc manager_desc = {
        .type = NMO_MANAGER_TYPE_DEFAULT,
        .guid = manager_guid,
    };

    nmo_manager *custom_manager =
        nmo_manager_create(nmo_context_get_allocator(ctx), &manager_desc);

    if (custom_manager == NULL) {
        fprintf(stderr, "Error: Failed to create custom manager\n");
        nmo_context_release(ctx);
        return 1;
    }
    printf("Custom manager created with GUID: ");

    char guid_str[37];
    nmo_guid_to_string(&manager_guid, guid_str, sizeof(guid_str));
    printf("%s\n\n", guid_str);

    // Step 4: Register the manager
    printf("Registering manager with registry...\n");
    nmo_error *reg_error =
        nmo_manager_registry_add_manager(manager_registry, custom_manager);

    if (reg_error != NULL) {
        fprintf(stderr, "Warning: Failed to register manager: %s\n",
                nmo_error_message(reg_error));
        nmo_error_destroy(reg_error);
    } else {
        printf("Manager registered successfully\n\n");
    }

    // Step 5: Create a session and use the manager
    printf("Creating session...\n");
    nmo_session *session = nmo_session_create(ctx);
    if (session == NULL) {
        fprintf(stderr, "Error: Failed to create session\n");
        nmo_manager_destroy(custom_manager);
        nmo_context_release(ctx);
        return 1;
    }
    printf("Session created\n\n");

    // Step 6: Create objects with the custom manager
    printf("Creating objects with custom manager...\n");
    nmo_object_desc obj_desc = {
        .type = NMO_OBJECT_TYPE_DEFAULT,
        .id = 1,
        .name = "CustomObject",
    };

    nmo_object *obj = nmo_object_create(nmo_context_get_allocator(ctx),
                                        &obj_desc);

    if (obj == NULL) {
        fprintf(stderr, "Error: Failed to create object\n");
    } else {
        printf("Object created successfully (ID: %u)\n\n", obj.id);
    }

    // Clean up
    printf("Cleaning up...\n");
    if (obj != NULL) {
        nmo_object_destroy(obj);
    }
    nmo_session_destroy(session);
    nmo_manager_destroy(custom_manager);
    nmo_context_release(ctx);

    printf("Done.\n");
    return 0;
}
