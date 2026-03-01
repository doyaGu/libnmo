/**
 * @file manager.c
 * @brief Manager plugin interface implementation
 */

#include "format/nmo_manager.h"
#include "core/nmo_allocator.h"
#include <stdlib.h>
#include <string.h>

/**
 * Create manager
 */
nmo_manager_t *nmo_manager_create(nmo_guid_t guid, const char *name, nmo_plugin_category_t category) {
    nmo_allocator_t alloc = nmo_allocator_default();
    
    nmo_manager_t *manager = (nmo_manager_t *) nmo_alloc(&alloc, sizeof(nmo_manager_t), _Alignof(nmo_manager_t));
    if (manager == NULL) {
        return NULL;
    }

    memset(manager, 0, sizeof(nmo_manager_t));
    manager->guid = guid;
    manager->category = category;

    // Copy name if provided
    if (name != NULL) {
        size_t name_len = strlen(name);
        char *name_copy = (char *) nmo_alloc(&alloc, name_len + 1, 1);
        if (name_copy == NULL) {
            nmo_free(&alloc, manager);
            return NULL;
        }
        memcpy(name_copy, name, name_len + 1);
        manager->name = name_copy;
    }

    return manager;
}

/**
 * Destroy manager
 */
void nmo_manager_destroy(nmo_manager_t *manager) {
    if (manager != NULL) {
        nmo_allocator_t alloc = nmo_allocator_default();
        if (manager->name != NULL) {
            nmo_free(&alloc, (void *) manager->name);
        }
        nmo_free(&alloc, manager);
    }
}

/**
 * Set manager user data
 */
int nmo_manager_set_user_data(nmo_manager_t *manager, void *user_data) {
    if (manager == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    manager->user_data = user_data;
    return NMO_OK;
}

/**
 * Set event hook
 */
int nmo_manager_set_on_event_hook(nmo_manager_t *manager, nmo_manager_on_event_fn hook) {
    if (manager == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    manager->on_event = hook;
    return NMO_OK;
}

/**
 * Dispatch event
 */
int nmo_manager_invoke_event(
    nmo_manager_t *manager,
    void *session,
    const nmo_runtime_event_ctx_t *ctx) {
    if (manager == NULL || ctx == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (manager->on_event == NULL) {
        return NMO_OK; // No hook registered, success
    }

    return manager->on_event(session, ctx, manager->user_data);
}

/**
 * Get manager GUID
 */
nmo_guid_t nmo_manager_get_guid(const nmo_manager_t *manager) {
    nmo_guid_t zero_guid = {0, 0};
    if (manager == NULL) {
        return zero_guid;
    }

    return manager->guid;
}

/**
 * Get manager name
 */
const char *nmo_manager_get_name(const nmo_manager_t *manager) {
    if (manager == NULL) {
        return NULL;
    }

    return manager->name;
}

/**
 * Get manager category
 */
nmo_plugin_category_t nmo_manager_get_category(const nmo_manager_t *manager) {
    if (manager == NULL) {
        return NMO_PLUGIN_MANAGER_DLL; // Default
    }

    return manager->category;
}
