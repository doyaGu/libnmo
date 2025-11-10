/**
 * @file test_schema.c
 * @brief Unit tests for schema system
 */

#include "../test_framework.h"
#include "schema/nmo_schema.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_validator.h"
#include "schema/nmo_migrator.h"
#include "format/nmo_object.h"
#include "core/nmo_arena.h"
#include <stdio.h>
#include <string.h>

// Test: Field type helpers
static void test_field_type_helpers(void) {
    // Test field type names
    if (strcmp(nmo_field_type_name(NMO_FIELD_INT32), "int32") != 0) {
        printf("FAIL: test_field_type_helpers - field type name mismatch\n");
        return;
    }

    // Test field type sizes
    if (nmo_field_type_size(NMO_FIELD_INT32) != 4) {
        printf("FAIL: test_field_type_helpers - INT32 size incorrect\n");
        return;
    }

    if (nmo_field_type_size(NMO_FIELD_GUID) != 8) {
        printf("FAIL: test_field_type_helpers - GUID size incorrect\n");
        return;
    }

    if (nmo_field_type_size(NMO_FIELD_STRING) != 0) {
        printf("FAIL: test_field_type_helpers - STRING size should be 0 (variable)\n");
        return;
    }

    printf("PASS: test_field_type_helpers\n");
}

// Test: Schema registry
static void test_schema_registry(void) {
    nmo_schema_registry* registry = nmo_schema_registry_create();
    if (registry == NULL) {
        printf("FAIL: test_schema_registry - registry creation failed\n");
        return;
    }

    // Add built-in schemas
    if (nmo_schema_registry_add_builtin(registry) != NMO_OK) {
        printf("FAIL: test_schema_registry - add_builtin failed\n");
        nmo_schema_registry_destroy(registry);
        return;
    }

    // Check schema count
    size_t count = nmo_schema_registry_get_count(registry);
    if (count != 6) {  // We registered 6 schemas
        printf("FAIL: test_schema_registry - expected 6 schemas, got %zu\n", count);
        nmo_schema_registry_destroy(registry);
        return;
    }

    // Find schema by ID
    const nmo_schema_descriptor* schema = nmo_schema_registry_find_by_id(registry, 0x00000001);
    if (schema == NULL) {
        printf("FAIL: test_schema_registry - CKObject schema not found\n");
        nmo_schema_registry_destroy(registry);
        return;
    }

    if (strcmp(schema->class_name, "CKObject") != 0) {
        printf("FAIL: test_schema_registry - schema name mismatch\n");
        nmo_schema_registry_destroy(registry);
        return;
    }

    // Find schema by name
    schema = nmo_schema_registry_find_by_name(registry, "CKBeObject");
    if (schema == NULL) {
        printf("FAIL: test_schema_registry - CKBeObject schema not found by name\n");
        nmo_schema_registry_destroy(registry);
        return;
    }

    if (schema->class_id != 0x00000002) {
        printf("FAIL: test_schema_registry - CKBeObject class ID mismatch\n");
        nmo_schema_registry_destroy(registry);
        return;
    }

    // Verify schema consistency
    if (nmo_verify_schema_consistency(registry) != NMO_OK) {
        printf("FAIL: test_schema_registry - schema consistency check failed\n");
        nmo_schema_registry_destroy(registry);
        return;
    }

    nmo_schema_registry_destroy(registry);
    printf("PASS: test_schema_registry\n");
}

// Test: Validator
static void test_validator(void) {
    nmo_schema_registry* registry = nmo_schema_registry_create();
    if (registry == NULL) {
        printf("FAIL: test_validator - registry creation failed\n");
        return;
    }

    nmo_schema_registry_add_builtin(registry);

    nmo_validation* validation = nmo_validation_create(registry, NMO_VALIDATION_STRICT);
    if (validation == NULL) {
        printf("FAIL: test_validator - validator creation failed\n");
        nmo_schema_registry_destroy(registry);
        return;
    }

    // Create test object
    nmo_arena* arena = nmo_arena_create(NULL, 8192);
    nmo_object* obj = nmo_object_create(arena, 100, 0x00000001);  // CKObject

    // Validate object
    nmo_validation_result result = nmo_validate_object(validation, obj);
    if (result != NMO_VALID) {
        printf("FAIL: test_validator - validation failed for valid object\n");
        nmo_validation_destroy(validation);
        nmo_schema_registry_destroy(registry);
        nmo_arena_destroy(arena);
        return;
    }

    nmo_validation_destroy(validation);
    nmo_schema_registry_destroy(registry);
    nmo_arena_destroy(arena);
    printf("PASS: test_validator\n");
}

// Test: Migrator
static void test_migrator(void) {
    nmo_schema_registry* registry = nmo_schema_registry_create();
    if (registry == NULL) {
        printf("FAIL: test_migrator - registry creation failed\n");
        return;
    }

    nmo_schema_registry_add_builtin(registry);

    nmo_migrator* migrator = nmo_migrator_create(registry);
    if (migrator == NULL) {
        printf("FAIL: test_migrator - migrator creation failed\n");
        nmo_schema_registry_destroy(registry);
        return;
    }

    // Check if migration is supported
    if (!nmo_migrator_can_migrate(migrator, 6, 7)) {
        printf("FAIL: test_migrator - migration should be supported\n");
        nmo_migrator_destroy(migrator);
        nmo_schema_registry_destroy(registry);
        return;
    }

    nmo_migrator_destroy(migrator);
    nmo_schema_registry_destroy(registry);
    printf("PASS: test_migrator\n");
}

int main(void) {
    printf("Running schema system tests...\n\n");

    test_field_type_helpers();
    test_schema_registry();
    test_validator();
    test_migrator();

    printf("\nAll schema system tests completed!\n");
    return 0;
}
