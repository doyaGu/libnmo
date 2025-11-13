/**
 * @file test_validator.c
 * @brief Unit tests for schema validator
 */

#include "../test_framework.h"
#include "schema/nmo_validator.h"
#include "schema/nmo_schema_registry.h"

TEST(validator, create_destroy) {
    nmo_schema_registry_t *registry = nmo_schema_registry_create();
    ASSERT_NOT_NULL(registry);
    
    nmo_validation_t *validation = nmo_validation_create(registry, NMO_VALIDATION_STRICT);
    ASSERT_NOT_NULL(validation);
    
    nmo_validation_destroy(validation);
    nmo_schema_registry_destroy(registry);
}

TEST(validator, create_null_registry) {
    nmo_validation_t *validation = nmo_validation_create(NULL, NMO_VALIDATION_STRICT);
    ASSERT_NULL(validation);
}

TEST(validator, destroy_null) {
    // Should not crash
    nmo_validation_destroy(NULL);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(validator, create_destroy);
    REGISTER_TEST(validator, create_null_registry);
    REGISTER_TEST(validator, destroy_null);
TEST_MAIN_END()
