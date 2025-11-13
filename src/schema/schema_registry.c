/**
 * @file schema_registry.c
 * @brief Schema registry implementation
 */

#include "schema/nmo_schema_registry.h"
#include "core/nmo_hash_table.h"
#include "core/nmo_error.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 32

/**
 * Schema registry structure
 */
typedef struct nmo_schema_registry {
    nmo_hash_table_t *schemas;  /* class_id -> schema* */
} nmo_schema_registry_t;

/**
 * Create schema registry
 */
nmo_schema_registry_t *nmo_schema_registry_create(void) {
    nmo_schema_registry_t *registry = (nmo_schema_registry_t *) malloc(sizeof(nmo_schema_registry_t));
    if (registry == NULL) {
        return NULL;
    }

    registry->schemas = nmo_hash_table_create(
        sizeof(nmo_class_id_t),                    /* key: class_id */
        sizeof(const nmo_schema_descriptor_t *),   /* value: schema pointer */
        INITIAL_CAPACITY,
        nmo_hash_uint32,                           /* hash function for uint32_t */
        NULL                                        /* use default memcmp */
    );

    if (registry->schemas == NULL) {
        free(registry);
        return NULL;
    }

    return registry;
}

/**
 * Destroy schema registry
 */
void nmo_schema_registry_destroy(nmo_schema_registry_t *registry) {
    if (registry != NULL) {
        nmo_hash_table_destroy(registry->schemas);
        free(registry);
    }
}

/**
 * Register schema
 */
int nmo_schema_registry_add(nmo_schema_registry_t *registry,
                            const nmo_schema_descriptor_t *schema) {
    if (registry == NULL || schema == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    // Validate schema
    int validation_result = nmo_schema_descriptor_validate(schema);
    if (validation_result != NMO_OK) {
        return validation_result;
    }

    // Add or update schema in hash table
    return nmo_hash_table_insert(registry->schemas, &schema->class_id, &schema);
}

/**
 * Find schema by class ID
 */
const nmo_schema_descriptor_t *nmo_schema_registry_find_by_id(
    const nmo_schema_registry_t *registry, nmo_class_id_t class_id) {
    if (registry == NULL) {
        return NULL;
    }

    const nmo_schema_descriptor_t *schema = NULL;
    if (nmo_hash_table_get(registry->schemas, &class_id, &schema)) {
        return schema;
    }

    return NULL;
}

/**
 * Iterator context for name search
 */
typedef struct {
    const char *target_name;
    const nmo_schema_descriptor_t *found_schema;
} name_search_context_t;

/**
 * Iterator function to find schema by name
 */
static int find_by_name_iterator(const void *key, void *value, void *user_data) {
    (void)key;
    name_search_context_t *context = (name_search_context_t *)user_data;
    const nmo_schema_descriptor_t *schema = *(const nmo_schema_descriptor_t **)value;

    if (strcmp(schema->class_name, context->target_name) == 0) {
        context->found_schema = schema;
        return 0; /* Stop iteration */
    }

    return 1; /* Continue iteration */
}

/**
 * Find schema by class name
 */
const nmo_schema_descriptor_t *nmo_schema_registry_find_by_name(
    const nmo_schema_registry_t *registry, const char *class_name) {
    if (registry == NULL || class_name == NULL) {
        return NULL;
    }

    name_search_context_t context = {
        .target_name = class_name,
        .found_schema = NULL
    };

    nmo_hash_table_iterate(registry->schemas, find_by_name_iterator, &context);

    return context.found_schema;
}

/**
 * Get number of registered schemas
 */
size_t nmo_schema_registry_get_count(const nmo_schema_registry_t *registry) {
    if (registry == NULL) {
        return 0;
    }

    return nmo_hash_table_get_count(registry->schemas);
}

/**
 * Iterator context for schema verification
 */
typedef struct {
    nmo_schema_registry_t *registry;
    int error_code;
} verify_context_t;

/**
 * Iterator function to verify each schema
 */
static int verify_schema_iterator(const void *key, void *value, void *user_data) {
    (void)key;
    verify_context_t *context = (verify_context_t *)user_data;
    const nmo_schema_descriptor_t *schema = *(const nmo_schema_descriptor_t **)value;

    // Validate schema
    int result = nmo_schema_descriptor_validate(schema);
    if (result != NMO_OK) {
        context->error_code = result;
        return 0; /* Stop iteration */
    }

    // If schema has a parent, verify parent exists
    if (schema->parent_class_id != 0) {
        const nmo_schema_descriptor_t *parent =
            nmo_schema_registry_find_by_id(context->registry, schema->parent_class_id);
        if (parent == NULL) {
            context->error_code = NMO_ERR_INVALID_ARGUMENT; /* Parent not found */
            return 0; /* Stop iteration */
        }
    }

    return 1; /* Continue iteration */
}

/**
 * Verify schema consistency
 */
int nmo_verify_schema_consistency(nmo_schema_registry_t *registry) {
    if (registry == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    verify_context_t context = {
        .registry = registry,
        .error_code = NMO_OK
    };

    nmo_hash_table_iterate(registry->schemas, verify_schema_iterator, &context);

    return context.error_code;
}

/**
 * Clear all schemas from registry
 */
int nmo_schema_registry_clear(nmo_schema_registry_t *registry) {
    if (registry == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    nmo_hash_table_clear(registry->schemas);

    return NMO_OK;
}

/**
 * Forward declaration for built-in schemas
 */
extern int nmo_builtin_schemas_register(nmo_schema_registry_t *registry);

/**
 * Register all built-in schemas
 */
int nmo_schema_registry_add_builtin(nmo_schema_registry_t *registry) {
    if (registry == NULL) {
        return NMO_ERR_INVALID_ARGUMENT;
    }

    return nmo_builtin_schemas_register(registry);
}
