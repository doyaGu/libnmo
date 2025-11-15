/**
 * @file manager_registry.c
 * @brief Manager registry implementation with indexed map
 */

#include "format/nmo_manager_registry.h"
#include "format/nmo_manager.h"
#include "core/nmo_indexed_map.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include "core/nmo_arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>

#define INITIAL_CAPACITY 16

/**
 * @brief Manager registry structure
 */
typedef struct nmo_manager_registry {
    nmo_arena_t *arena;
    nmo_indexed_map_t *managers_by_id;   /* manager_id -> nmo_manager_t* */
    nmo_indexed_map_t *managers_by_guid; /* nmo_guid_t -> nmo_manager_t* */
} nmo_manager_registry_t;

static size_t nmo_map_hash_guid(const void *key, size_t key_size) {
    (void)key_size;
    const nmo_guid_t *guid = (const nmo_guid_t *)key;
    return (size_t)nmo_guid_hash(*guid);
}

/**
 * Create manager registry
 */
nmo_manager_registry_t *nmo_manager_registry_create(nmo_arena_t *arena) {
    if (arena == NULL) {
        return NULL;
    }

    nmo_manager_registry_t *registry =
        (nmo_manager_registry_t *)nmo_arena_alloc(arena, sizeof(nmo_manager_registry_t), alignof(nmo_manager_registry_t));
    if (registry == NULL) {
        return NULL;
    }

    memset(registry, 0, sizeof(*registry));
    registry->arena = arena;

    registry->managers_by_id = nmo_indexed_map_create(
        arena,
        NULL,
        sizeof(uint32_t),
        sizeof(nmo_manager_t *),
        INITIAL_CAPACITY,
        nmo_map_hash_uint32,
        NULL
    );

    if (registry->managers_by_id == NULL) {
        return NULL;
    }

    registry->managers_by_guid = nmo_indexed_map_create(
        arena,
        NULL,
        sizeof(nmo_guid_t),
        sizeof(nmo_manager_t *),
        INITIAL_CAPACITY,
        nmo_map_hash_guid,
        NULL
    );

    if (registry->managers_by_guid == NULL) {
        return NULL;
    }

    return registry;
}

/**
 * Destroy manager registry
 */
void nmo_manager_registry_destroy(nmo_manager_registry_t *registry) {
    if (registry != NULL) {
        /* Destroy all registered managers */
        size_t count = nmo_indexed_map_get_count(registry->managers_by_id);
        for (size_t i = 0; i < count; i++) {
            nmo_manager_t *manager = NULL;
            if (nmo_indexed_map_get_value_at(registry->managers_by_id, i, &manager) && manager != NULL) {
                nmo_manager_destroy(manager);
            }
        }

        nmo_indexed_map_destroy(registry->managers_by_id);
        nmo_indexed_map_destroy(registry->managers_by_guid);
    }
}

/**
 * Register manager
 */
nmo_result_t nmo_manager_registry_register(
    nmo_manager_registry_t *registry, uint32_t manager_id, void *manager) {
    nmo_result_t result;

    if (registry == NULL || manager == NULL) {
        result.code = NMO_ERR_INVALID_ARGUMENT;
        result.error = NULL;
        return result;
    }

    /* Check if manager already registered */
    if (nmo_indexed_map_contains(registry->managers_by_id, &manager_id)) {
        result.code = NMO_ERR_INVALID_ARGUMENT;
        result.error = NULL;
        return result;
    }

    nmo_manager_t *mgr = (nmo_manager_t *)manager;

    if (nmo_indexed_map_contains(registry->managers_by_guid, &mgr->guid)) {
        result.code = NMO_ERR_INVALID_ARGUMENT;
        result.error = NULL;
        return result;
    }

    /* Insert manager */
    int insert_result = nmo_indexed_map_insert(registry->managers_by_id, &manager_id, &mgr);
    if (insert_result != NMO_OK) {
        result.code = insert_result;
        result.error = NULL;
        return result;
    }

    insert_result = nmo_indexed_map_insert(registry->managers_by_guid, &mgr->guid, &mgr);
    if (insert_result != NMO_OK) {
        /* Roll back ID map insert */
        nmo_indexed_map_remove(registry->managers_by_id, &manager_id);
        result.code = insert_result;
        result.error = NULL;
        return result;
    }

    return nmo_result_ok();
}

/**
 * Unregister manager
 */
nmo_result_t nmo_manager_registry_unregister(nmo_manager_registry_t *registry, uint32_t manager_id) {
    nmo_result_t result;

    if (registry == NULL) {
        result.code = NMO_ERR_INVALID_ARGUMENT;
        result.error = NULL;
        return result;
    }

    /* Get manager before removing */
    nmo_manager_t *manager = NULL;
    if (nmo_indexed_map_get(registry->managers_by_id, &manager_id, &manager) && manager != NULL) {
        nmo_manager_destroy(manager);
        nmo_indexed_map_remove(registry->managers_by_id, &manager_id);
        nmo_indexed_map_remove(registry->managers_by_guid, &manager->guid);
        return nmo_result_ok();
    }

    result.code = NMO_ERR_INVALID_ARGUMENT;
    result.error = NULL;
    return result;
}

/**
 * Get manager by ID
 */
void *nmo_manager_registry_get(const nmo_manager_registry_t *registry, uint32_t manager_id) {
    if (registry == NULL) {
        return NULL;
    }

    nmo_manager_t *manager = NULL;
    if (nmo_indexed_map_get(registry->managers_by_id, &manager_id, &manager)) {
        return manager;
    }

    return NULL;
}

/**
 * Check if manager is registered
 */
int nmo_manager_registry_contains(const nmo_manager_registry_t *registry, uint32_t manager_id) {
    if (registry == NULL) {
        return 0;
    }
    return nmo_indexed_map_contains(registry->managers_by_id, &manager_id);
}

/**
 * Get registered manager count
 */
uint32_t nmo_manager_registry_get_count(const nmo_manager_registry_t *registry) {
    if (registry == NULL) {
        return 0;
    }

    return (uint32_t)nmo_indexed_map_get_count(registry->managers_by_id);
}

/**
 * Get manager ID at index
 */
uint32_t nmo_manager_registry_get_id_at(const nmo_manager_registry_t *registry, uint32_t index) {
    if (registry == NULL) {
        return 0;
    }

    uint32_t manager_id = 0;
    if (nmo_indexed_map_get_key_at(registry->managers_by_id, index, &manager_id)) {
        return manager_id;
    }

    return 0;
}

/**
 * Clear all managers
 */
nmo_result_t nmo_manager_registry_clear(nmo_manager_registry_t *registry) {
    nmo_result_t result;

    if (registry == NULL) {
        result.code = NMO_ERR_INVALID_ARGUMENT;
        result.error = NULL;
        return result;
    }

    /* Destroy all managers */
    size_t count = nmo_indexed_map_get_count(registry->managers_by_id);
    for (size_t i = 0; i < count; i++) {
        nmo_manager_t *manager = NULL;
        if (nmo_indexed_map_get_value_at(registry->managers_by_id, i, &manager) && manager != NULL) {
            nmo_manager_destroy(manager);
        }
    }

    nmo_indexed_map_clear(registry->managers_by_id);
    nmo_indexed_map_clear(registry->managers_by_guid);

    return nmo_result_ok();
}

void *nmo_manager_registry_find_by_guid(
    const nmo_manager_registry_t *registry,
    nmo_guid_t guid) {
    if (registry == NULL) {
        return NULL;
    }

    nmo_manager_t *manager = NULL;
    if (nmo_indexed_map_get(registry->managers_by_guid, &guid, &manager)) {
        return manager;
    }

    return NULL;
}

