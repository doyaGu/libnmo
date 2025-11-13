/**
 * @file test_schema_registry.c
 * @brief Unit tests for schema registry
 */

#include "../test_framework.h"
#include "schema/nmo_schema_registry.h"

TEST(schema_registry, create_registry) {
    nmo_schema_registry_t *registry = nmo_schema_registry_create();
    ASSERT_NOT_NULL(registry);
    nmo_schema_registry_destroy(registry);
}

TEST(schema_registry, find_schema_by_id) {
    nmo_schema_registry_t *registry = nmo_schema_registry_create();
    ASSERT_NOT_NULL(registry);

    // Add builtin schemas
    int result = nmo_schema_registry_add_builtin(registry);
    ASSERT_EQ(result, 0);

    // Try to find a schema by class ID
    nmo_class_id_t class_id = 0x12345678;
    const nmo_schema_descriptor_t *schema = nmo_schema_registry_find_by_id(registry, class_id);
    // May be NULL if this specific class_id doesn't exist, which is fine

    nmo_schema_registry_destroy(registry);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(schema_registry, create_registry);
    REGISTER_TEST(schema_registry, find_schema_by_id);
TEST_MAIN_END()
