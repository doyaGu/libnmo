/**
 * @file test_migrator.c
 * @brief Unit tests for schema migrator
 */

#include "../test_framework.h"
#include "schema/nmo_migrator.h"
#include "schema/nmo_schema_registry.h"

TEST(migrator, create_destroy) {
    nmo_schema_registry_t *registry = nmo_schema_registry_create();
    ASSERT_NOT_NULL(registry);

    nmo_migrator_t *migrator = nmo_migrator_create(registry);
    ASSERT_NOT_NULL(migrator);

    nmo_migrator_destroy(migrator);
    nmo_schema_registry_destroy(registry);
}

TEST(migrator, create_null_registry) {
    nmo_migrator_t *migrator = nmo_migrator_create(NULL);
    ASSERT_NULL(migrator);
}

TEST(migrator, can_migrate_same_version) {
    nmo_schema_registry_t *registry = nmo_schema_registry_create();
    ASSERT_NOT_NULL(registry);

    nmo_migrator_t *migrator = nmo_migrator_create(registry);
    ASSERT_NOT_NULL(migrator);

    // Same version should be supported (no migration needed)
    int result = nmo_migrator_can_migrate(migrator, 1, 1);
    ASSERT_TRUE(result);

    nmo_migrator_destroy(migrator);
    nmo_schema_registry_destroy(registry);
}

TEST(migrator, destroy_null) {
    // Should not crash
    nmo_migrator_destroy(NULL);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(migrator, create_destroy);
    REGISTER_TEST(migrator, create_null_registry);
    REGISTER_TEST(migrator, can_migrate_same_version);
    REGISTER_TEST(migrator, destroy_null);
TEST_MAIN_END()
