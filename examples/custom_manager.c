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
    (void)argc;
    (void)argv;
    printf("=== Custom Manager Example ===\n\n");

    // Step 1: Create context
    printf("Creating context...\n");
    nmo_logger_t logger = nmo_logger_stderr();
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
    printf("Context created\n\n");

    // Step 2: Get manager registry
    printf("Accessing manager registry...\n");
    nmo_manager_registry_t *manager_registry =
        nmo_context_get_manager_registry(ctx);
    if (manager_registry == NULL) {
        fprintf(stderr, "Error: Failed to get manager registry\n");
        nmo_context_release(ctx);
        return 1;
    }
    printf("Manager registry accessed\n\n");

    // Step 3: Create a custom manager
    printf("Creating custom manager...\n");
    nmo_guid_t manager_guid = NMO_GUID(0x12345678, 0x9ABCDEF0);

    nmo_manager_t *custom_manager =
        nmo_manager_create(manager_guid, "CustomManager", NMO_PLUGIN_CUSTOM_DLL);

    if (custom_manager == NULL) {
        fprintf(stderr, "Error: Failed to create custom manager\n");
        nmo_context_release(ctx);
        return 1;
    }
    printf("Custom manager created with GUID: ");

    char guid_str[37];
    nmo_guid_format(manager_guid, guid_str, sizeof(guid_str));
    printf("%s\n\n", guid_str);

    // Step 4: Register the manager
    printf("Registering manager with registry...\n");
    nmo_manager_id_t manager_id = 1;
    nmo_status_t reg_result =
        nmo_manager_registry_register(manager_registry, manager_id, custom_manager);

    if (reg_result != NMO_OK) {
        fprintf(stderr, "Warning: Failed to register manager (%s)\n",
                nmo_error_string(reg_result));
    } else {
        printf("Manager registered successfully\n\n");
    }

    // Step 5: Create a session and use the manager
    printf("Creating session...\n");
    nmo_session_t *session = nmo_session_create(ctx);
    if (session == NULL) {
        fprintf(stderr, "Error: Failed to create session\n");
        nmo_manager_destroy(custom_manager);
        nmo_context_release(ctx);
        return 1;
    }
    printf("Session created\n\n");

    // Step 6: Create objects with the custom manager
    printf("Creating objects with custom manager...\n");
    nmo_object_id_t object_id = 1;
    nmo_class_id_t class_id = 1; /* CKObject */
    const nmo_allocator_t *allocator = nmo_context_get_allocator(nmo_session_get_context(session));
    nmo_object_t *obj = nmo_object_create(allocator, object_id, class_id);
    int obj_added_to_repo = 0;

    if (obj == NULL) {
        fprintf(stderr, "Error: Failed to create object\n");
    } else {
        nmo_object_set_name(obj, "CustomObject");
        nmo_object_repository_t *repo = nmo_session_get_repository(session);
        if (repo != NULL) {
            obj_added_to_repo = (nmo_object_repository_add(repo, obj) == NMO_OK);
        }
        printf("Object created successfully (ID: %u)\n\n",
               nmo_object_get_id(obj));
    }

    // Clean up
    printf("Cleaning up...\n");
    if (obj != NULL && !obj_added_to_repo) {
        nmo_object_destroy(obj);
    }
    nmo_session_destroy(session);
    nmo_manager_destroy(custom_manager);
    nmo_context_release(ctx);

    printf("Done.\n");
    return 0;
}
