/**
 * @file schema_registry.c
 * @brief Schema registry implementation
 */

#include "schema/nmo_schema_registry.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 32

/**
 * Schema registry entry
 */
typedef struct {
    const nmo_schema_descriptor* schema;
    int occupied;
} schema_entry;

/**
 * Schema registry structure
 */
struct nmo_schema_registry {
    schema_entry* entries;
    size_t capacity;
    size_t count;
};

/**
 * Hash function for class IDs
 */
static inline size_t hash_class_id(nmo_class_id class_id, size_t capacity) {
    return (class_id * 2654435761U) % capacity;
}

/**
 * Find entry slot (linear probing)
 */
static int find_slot(const nmo_schema_registry* registry, nmo_class_id class_id, int* found) {
    size_t index = hash_class_id(class_id, registry->capacity);
    size_t start_index = index;

    do {
        if (!registry->entries[index].occupied) {
            *found = 0;
            return (int)index;
        }

        if (registry->entries[index].schema->class_id == class_id) {
            *found = 1;
            return (int)index;
        }

        index = (index + 1) % registry->capacity;
    } while (index != start_index);

    *found = 0;
    return -1;  // Table full
}

/**
 * Resize registry
 */
static int resize_registry(nmo_schema_registry* registry, size_t new_capacity) {
    schema_entry* new_entries = (schema_entry*)calloc(new_capacity, sizeof(schema_entry));
    if (new_entries == NULL) {
        return NMO_ERR_NOMEM;
    }

    // Rehash existing entries
    schema_entry* old_entries = registry->entries;
    size_t old_capacity = registry->capacity;

    registry->entries = new_entries;
    registry->capacity = new_capacity;
    registry->count = 0;

    for (size_t i = 0; i < old_capacity; i++) {
        if (old_entries[i].occupied) {
            nmo_schema_registry_add(registry, old_entries[i].schema);
        }
    }

    free(old_entries);
    return NMO_OK;
}

/**
 * Create schema registry
 */
nmo_schema_registry* nmo_schema_registry_create(void) {
    nmo_schema_registry* registry = (nmo_schema_registry*)malloc(sizeof(nmo_schema_registry));
    if (registry == NULL) {
        return NULL;
    }

    registry->entries = (schema_entry*)calloc(INITIAL_CAPACITY, sizeof(schema_entry));
    if (registry->entries == NULL) {
        free(registry);
        return NULL;
    }

    registry->capacity = INITIAL_CAPACITY;
    registry->count = 0;

    return registry;
}

/**
 * Destroy schema registry
 */
void nmo_schema_registry_destroy(nmo_schema_registry* registry) {
    if (registry != NULL) {
        free(registry->entries);
        free(registry);
    }
}

/**
 * Register schema
 */
int nmo_schema_registry_add(nmo_schema_registry* registry,
                             const nmo_schema_descriptor* schema) {
    if (registry == NULL || schema == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Validate schema
    int validation_result = nmo_schema_descriptor_validate(schema);
    if (validation_result != NMO_OK) {
        return validation_result;
    }

    // Check if we need to resize (load factor > 0.7)
    if ((registry->count + 1) * 10 > registry->capacity * 7) {
        int result = resize_registry(registry, registry->capacity * 2);
        if (result != NMO_OK) {
            return result;
        }
    }

    // Find slot
    int found;
    int slot = find_slot(registry, schema->class_id, &found);
    if (slot < 0) {
        return NMO_ERR_NOMEM;  // Table full (shouldn't happen after resize)
    }

    if (found) {
        // Update existing schema
        registry->entries[slot].schema = schema;
    } else {
        // Add new schema
        registry->entries[slot].schema = schema;
        registry->entries[slot].occupied = 1;
        registry->count++;
    }

    return NMO_OK;
}

/**
 * Find schema by class ID
 */
const nmo_schema_descriptor* nmo_schema_registry_find_by_id(
    const nmo_schema_registry* registry, nmo_class_id class_id) {

    if (registry == NULL) {
        return NULL;
    }

    int found;
    int slot = find_slot(registry, class_id, &found);
    if (slot < 0 || !found) {
        return NULL;
    }

    return registry->entries[slot].schema;
}

/**
 * Find schema by class name
 */
const nmo_schema_descriptor* nmo_schema_registry_find_by_name(
    const nmo_schema_registry* registry, const char* class_name) {

    if (registry == NULL || class_name == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < registry->capacity; i++) {
        if (registry->entries[i].occupied) {
            const nmo_schema_descriptor* schema = registry->entries[i].schema;
            if (strcmp(schema->class_name, class_name) == 0) {
                return schema;
            }
        }
    }

    return NULL;
}

/**
 * Get number of registered schemas
 */
size_t nmo_schema_registry_get_count(const nmo_schema_registry* registry) {
    if (registry == NULL) {
        return 0;
    }

    return registry->count;
}

/**
 * Verify schema consistency
 */
int nmo_verify_schema_consistency(nmo_schema_registry* registry) {
    if (registry == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Check each schema
    for (size_t i = 0; i < registry->capacity; i++) {
        if (registry->entries[i].occupied) {
            const nmo_schema_descriptor* schema = registry->entries[i].schema;

            // Validate schema
            int result = nmo_schema_descriptor_validate(schema);
            if (result != NMO_OK) {
                return result;
            }

            // If schema has a parent, verify parent exists
            if (schema->parent_class_id != 0) {
                const nmo_schema_descriptor* parent =
                    nmo_schema_registry_find_by_id(registry, schema->parent_class_id);
                if (parent == NULL) {
                    return NMO_ERR_INVALID_ARGUMENT;  // Parent not found
                }
            }
        }
    }

    return NMO_OK;
}

/**
 * Clear all schemas from registry
 */
int nmo_schema_registry_clear(nmo_schema_registry* registry) {
    if (registry == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    memset(registry->entries, 0, registry->capacity * sizeof(schema_entry));
    registry->count = 0;

    return NMO_OK;
}

/**
 * Forward declaration for built-in schemas
 */
extern int nmo_builtin_schemas_register(nmo_schema_registry* registry);

/**
 * Register all built-in schemas
 */
int nmo_schema_registry_add_builtin(nmo_schema_registry* registry) {
    if (registry == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    return nmo_builtin_schemas_register(registry);
}
