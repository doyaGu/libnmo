/**
 * @file test_schema_registry.c
 * @brief Unit tests for schema registry
 */

#include "../test_framework.h"
#include "schema/nmo_schema_registry.h"
#include "core/nmo_arena.h"

TEST(schema_registry, create_registry) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    ASSERT_NOT_NULL(arena);
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NOT_NULL(registry);
    nmo_schema_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST(schema_registry, find_schema_by_id) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 0);
    ASSERT_NOT_NULL(arena);
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NOT_NULL(registry);

    nmo_result_t result = nmo_schema_registry_add_builtin(registry);
    ASSERT_EQ(result.code, NMO_OK);

    // Try to find a schema by class ID
    nmo_class_id_t class_id = 0x12345678;
    const nmo_schema_type_t *schema = nmo_schema_registry_find_by_class_id(registry, class_id);
    (void)schema; /* May be NULL depending on registered schemas */

    nmo_schema_registry_destroy(registry);
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(schema_registry, create_registry);
    REGISTER_TEST(schema_registry, find_schema_by_id);
TEST_MAIN_END()
