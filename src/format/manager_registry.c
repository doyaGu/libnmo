/**
 * @file manager_registry.c
 * @brief Manager registry implementation with hash table
 */

#include "format/nmo_manager_registry.h"
#include "format/nmo_manager.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 16
#define LOAD_FACTOR 0.7
#define HASH_MULTIPLIER 2654435761U  /* Knuth's multiplicative hash */

/**
 * @brief Manager registry entry
 */
typedef struct {
    uint32_t manager_id;      /**< Manager ID */
    nmo_manager* manager;     /**< Manager instance */
    int occupied;             /**< Entry is occupied */
} manager_entry;

/**
 * @brief Manager registry structure
 */
struct nmo_manager_registry {
    manager_entry* entries;   /**< Hash table entries */
    size_t capacity;          /**< Table capacity */
    size_t count;             /**< Number of managers */

    /* Dense array for iteration */
    uint32_t* manager_ids;    /**< Array of manager IDs */
    size_t ids_capacity;      /**< IDs array capacity */
};

/**
 * @brief Hash function for manager ID
 */
static inline size_t hash_manager_id(uint32_t manager_id, size_t capacity) {
    return (manager_id * HASH_MULTIPLIER) % capacity;
}

/**
 * @brief Resize hash table
 */
static int resize_table(nmo_manager_registry_t* registry, size_t new_capacity) {
    manager_entry* new_entries = (manager_entry*)calloc(new_capacity, sizeof(manager_entry));
    if (new_entries == NULL) {
        return NMO_ERR_NOMEM;
    }

    /* Rehash existing entries */
    for (size_t i = 0; i < registry->capacity; i++) {
        if (registry->entries[i].occupied) {
            uint32_t manager_id = registry->entries[i].manager_id;
            nmo_manager* manager = registry->entries[i].manager;

            size_t index = hash_manager_id(manager_id, new_capacity);

            /* Linear probing */
            while (new_entries[index].occupied) {
                index = (index + 1) % new_capacity;
            }

            new_entries[index].manager_id = manager_id;
            new_entries[index].manager = manager;
            new_entries[index].occupied = 1;
        }
    }

    free(registry->entries);
    registry->entries = new_entries;
    registry->capacity = new_capacity;

    return NMO_OK;
}

/**
 * Create manager registry
 */
nmo_manager_registry_t* nmo_manager_registry_create(void) {
    nmo_manager_registry_t* registry = (nmo_manager_registry_t*)malloc(sizeof(nmo_manager_registry_t));
    if (registry == NULL) {
        return NULL;
    }

    registry->entries = (manager_entry*)calloc(INITIAL_CAPACITY, sizeof(manager_entry));
    if (registry->entries == NULL) {
        free(registry);
        return NULL;
    }

    registry->manager_ids = (uint32_t*)malloc(INITIAL_CAPACITY * sizeof(uint32_t));
    if (registry->manager_ids == NULL) {
        free(registry->entries);
        free(registry);
        return NULL;
    }

    registry->capacity = INITIAL_CAPACITY;
    registry->count = 0;
    registry->ids_capacity = INITIAL_CAPACITY;

    return registry;
}

/**
 * Destroy manager registry
 */
void nmo_manager_registry_destroy(nmo_manager_registry_t* registry) {
    if (registry != NULL) {
        /* Destroy all registered managers */
        for (size_t i = 0; i < registry->capacity; i++) {
            if (registry->entries[i].occupied && registry->entries[i].manager != NULL) {
                nmo_manager_destroy(registry->entries[i].manager);
            }
        }

        free(registry->entries);
        free(registry->manager_ids);
        free(registry);
    }
}

/**
 * Register manager
 */
nmo_result_t nmo_manager_registry_register(
    nmo_manager_registry_t* registry, uint32_t manager_id, void* manager) {
    nmo_result_t result;

    if (registry == NULL || manager == NULL) {
        result.code = NMO_ERR_INVALID_ARGUMENT;
        result.error = NULL;
        return result;
    }

    /* Check load factor and resize if needed */
    if ((double)registry->count / (double)registry->capacity >= LOAD_FACTOR) {
        int resize_result = resize_table(registry, registry->capacity * 2);
        if (resize_result != NMO_OK) {
            result.code = resize_result;
            result.error = NULL;
            return result;
        }
    }

    /* Find slot using linear probing */
    size_t index = hash_manager_id(manager_id, registry->capacity);
    size_t start_index = index;

    while (registry->entries[index].occupied) {
        /* Check if manager already registered */
        if (registry->entries[index].manager_id == manager_id) {
            result.code = NMO_ERR_INVALID_ARGUMENT;
            result.error = NULL;
            return result;
        }

        index = (index + 1) % registry->capacity;

        /* Table is full (shouldn't happen with load factor) */
        if (index == start_index) {
            result.code = NMO_ERR_NOMEM;
            result.error = NULL;
            return result;
        }
    }

    /* Insert manager */
    registry->entries[index].manager_id = manager_id;
    registry->entries[index].manager = (nmo_manager*)manager;
    registry->entries[index].occupied = 1;

    /* Add to dense array for iteration */
    if (registry->count >= registry->ids_capacity) {
        size_t new_capacity = registry->ids_capacity * 2;
        uint32_t* new_ids = (uint32_t*)realloc(registry->manager_ids, new_capacity * sizeof(uint32_t));
        if (new_ids == NULL) {
            /* Rollback insertion */
            registry->entries[index].occupied = 0;
            result.code = NMO_ERR_NOMEM;
            result.error = NULL;
            return result;
        }
        registry->manager_ids = new_ids;
        registry->ids_capacity = new_capacity;
    }

    registry->manager_ids[registry->count] = manager_id;
    registry->count++;

    return nmo_result_ok();
}

/**
 * Unregister manager
 */
nmo_result_t nmo_manager_registry_unregister(nmo_manager_registry_t* registry, uint32_t manager_id) {
    nmo_result_t result;

    if (registry == NULL) {
        result.code = NMO_ERR_INVALID_ARGUMENT;
        result.error = NULL;
        return result;
    }

    /* Find manager */
    size_t index = hash_manager_id(manager_id, registry->capacity);
    size_t start_index = index;

    while (registry->entries[index].occupied) {
        if (registry->entries[index].manager_id == manager_id) {
            /* Destroy manager */
            if (registry->entries[index].manager != NULL) {
                nmo_manager_destroy(registry->entries[index].manager);
            }

            /* Mark as unoccupied */
            registry->entries[index].occupied = 0;
            registry->entries[index].manager = NULL;

            /* Remove from dense array */
            for (size_t i = 0; i < registry->count; i++) {
                if (registry->manager_ids[i] == manager_id) {
                    /* Swap with last element */
                    registry->manager_ids[i] = registry->manager_ids[registry->count - 1];
                    break;
                }
            }

            registry->count--;
            return nmo_result_ok();
        }

        index = (index + 1) % registry->capacity;

        if (index == start_index) {
            break;
        }
    }

    result.code = NMO_ERR_INVALID_ARGUMENT;
    result.error = NULL;
    return result;
}

/**
 * Get manager by ID
 */
void* nmo_manager_registry_get(const nmo_manager_registry_t* registry, uint32_t manager_id) {
    if (registry == NULL) {
        return NULL;
    }

    size_t index = hash_manager_id(manager_id, registry->capacity);
    size_t start_index = index;

    while (registry->entries[index].occupied) {
        if (registry->entries[index].manager_id == manager_id) {
            return registry->entries[index].manager;
        }

        index = (index + 1) % registry->capacity;

        if (index == start_index) {
            break;
        }
    }

    return NULL;
}

/**
 * Check if manager is registered
 */
int nmo_manager_registry_contains(const nmo_manager_registry_t* registry, uint32_t manager_id) {
    return nmo_manager_registry_get(registry, manager_id) != NULL;
}

/**
 * Get registered manager count
 */
uint32_t nmo_manager_registry_get_count(const nmo_manager_registry_t* registry) {
    if (registry == NULL) {
        return 0;
    }

    return (uint32_t)registry->count;
}

/**
 * Get manager ID at index
 */
uint32_t nmo_manager_registry_get_id_at(const nmo_manager_registry_t* registry, uint32_t index) {
    if (registry == NULL || index >= registry->count) {
        return 0;
    }

    return registry->manager_ids[index];
}

/**
 * Clear all managers
 */
nmo_result_t nmo_manager_registry_clear(nmo_manager_registry_t* registry) {
    nmo_result_t result;

    if (registry == NULL) {
        result.code = NMO_ERR_INVALID_ARGUMENT;
        result.error = NULL;
        return result;
    }

    /* Destroy all managers */
    for (size_t i = 0; i < registry->capacity; i++) {
        if (registry->entries[i].occupied && registry->entries[i].manager != NULL) {
            nmo_manager_destroy(registry->entries[i].manager);
        }
        registry->entries[i].occupied = 0;
        registry->entries[i].manager = NULL;
    }

    registry->count = 0;

    return nmo_result_ok();
}
