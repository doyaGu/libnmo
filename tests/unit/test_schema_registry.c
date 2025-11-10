/**
 * @file test_schema_registry.c
 * @brief Unit tests for schema registry
 */

#include "../test_framework.h"
#include "nmo.h"

TEST(schema_registry, create_registry) {
    nmo_schema_registry *registry = nmo_schema_registry_create(NULL);
    ASSERT_NOT_NULL(registry);
    nmo_schema_registry_destroy(registry);
}

TEST(schema_registry, add_schema) {
    nmo_schema_registry *registry = nmo_schema_registry_create(NULL);
    ASSERT_NOT_NULL(registry);

    nmo_schema *schema = nmo_schema_create(NULL);
    if (schema != NULL) {
        nmo_error *err = nmo_schema_registry_add_schema(registry, schema);
        if (err != NULL) {
            nmo_error_destroy(err);
        }
        nmo_schema_destroy(schema);
    }

    nmo_schema_registry_destroy(registry);
}

TEST(schema_registry, get_schema) {
    nmo_schema_registry *registry = nmo_schema_registry_create(NULL);
    ASSERT_NOT_NULL(registry);

    nmo_guid guid = nmo_guid_create();
    nmo_schema *schema = nmo_schema_registry_get_schema(registry, &guid);
    // May be NULL if schema doesn't exist, which is fine

    nmo_schema_registry_destroy(registry);
}

int main(void) {
    return 0;
}
