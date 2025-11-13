/**
 * @file manager.c
 * @brief Manager plugin interface implementation
 */

#include "format/nmo_manager.h"
#include <stdlib.h>
#include <string.h>

/**
 * Create manager
 */
nmo_manager_t *nmo_manager_create(nmo_guid_t guid, const char *name, nmo_plugin_category_t category) {
    nmo_manager_t *manager = (nmo_manager_t *) malloc(sizeof(nmo_manager_t));
    if (manager == NULL) {
        return NULL;
    }

    memset(manager, 0, sizeof(nmo_manager_t));
    manager->guid = guid;
    manager->category = category;

    // Copy name if provided
    if (name != NULL) {
        size_t name_len = strlen(name);
        char *name_copy = (char *) malloc(name_len + 1);
        if (name_copy == NULL) {
            free(manager);
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
        if (manager->name != NULL) {
            free((void *) manager->name);
        }
        free(manager);
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
 * Set pre-load hook
 */
int nmo_manager_set_pre_load_hook(nmo_manager_t *manager, int (*hook)(void *, void *)) {
    if (manager == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    manager->pre_load = hook;
    return NMO_OK;
}

/**
 * Set post-load hook
 */
int nmo_manager_set_post_load_hook(nmo_manager_t *manager, int (*hook)(void *, void *)) {
    if (manager == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    manager->post_load = hook;
    return NMO_OK;
}

/**
 * Set load-data hook
 */
int nmo_manager_set_load_data_hook(nmo_manager_t *manager, int (*hook)(void *, const nmo_chunk_t *, void *)) {
    if (manager == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    manager->load_data = hook;
    return NMO_OK;
}

/**
 * Set save-data hook
 */
int nmo_manager_set_save_data_hook(nmo_manager_t *manager, nmo_chunk_t * (*hook)(void *, void *)) {
    if (manager == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    manager->save_data = hook;
    return NMO_OK;
}

/**
 * Set pre-save hook
 */
int nmo_manager_set_pre_save_hook(nmo_manager_t *manager, int (*hook)(void *, void *)) {
    if (manager == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    manager->pre_save = hook;
    return NMO_OK;
}

/**
 * Set post-save hook
 */
int nmo_manager_set_post_save_hook(nmo_manager_t *manager, int (*hook)(void *, void *)) {
    if (manager == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    manager->post_save = hook;
    return NMO_OK;
}

/**
 * Invoke pre-load hook
 */
int nmo_manager_invoke_pre_load(nmo_manager_t *manager, void *session) {
    if (manager == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (manager->pre_load == NULL) {
        return NMO_OK; // No hook registered, success
    }

    return manager->pre_load(session, manager->user_data);
}

/**
 * Invoke post-load hook
 */
int nmo_manager_invoke_post_load(nmo_manager_t *manager, void *session) {
    if (manager == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (manager->post_load == NULL) {
        return NMO_OK; // No hook registered, success
    }

    return manager->post_load(session, manager->user_data);
}

/**
 * Invoke load-data hook
 */
int nmo_manager_invoke_load_data(nmo_manager_t *manager, void *session, const nmo_chunk_t *chunk) {
    if (manager == NULL || chunk == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (manager->load_data == NULL) {
        return NMO_OK; // No hook registered, success
    }

    return manager->load_data(session, chunk, manager->user_data);
}

/**
 * Invoke save-data hook
 */
nmo_chunk_t *nmo_manager_invoke_save_data(nmo_manager_t *manager, void *session) {
    if (manager == NULL) {
        return NULL;
    }

    if (manager->save_data == NULL) {
        return NULL; // No hook registered
    }

    return manager->save_data(session, manager->user_data);
}

/**
 * Invoke pre-save hook
 */
int nmo_manager_invoke_pre_save(nmo_manager_t *manager, void *session) {
    if (manager == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (manager->pre_save == NULL) {
        return NMO_OK; // No hook registered, success
    }

    return manager->pre_save(session, manager->user_data);
}

/**
 * Invoke post-save hook
 */
int nmo_manager_invoke_post_save(nmo_manager_t *manager, void *session) {
    if (manager == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    if (manager->post_save == NULL) {
        return NMO_OK; // No hook registered, success
    }

    return manager->post_save(session, manager->user_data);
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
