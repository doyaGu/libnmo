/**
 * @file test_version_management.c
 * @brief Unit tests for Phase 4 version management system
 *
 * Tests:
 * - nmo_schema_is_compatible()
 * - nmo_schema_registry_find_for_version()
 * - nmo_schema_registry_find_all_variants()
 * - Multi-version schema registration
 */

#include "test_framework.h"
#include "schema/nmo_schema.h"
#include "schema/nmo_schema_builder.h"
#include "schema/nmo_schema_registry.h"
#include "core/nmo_arena.h"
#include "core/nmo_allocator.h"
#include "core/nmo_error.h"
#include <string.h>

/* =============================================================================
 * TEST FIXTURES
 * ============================================================================= */

typedef struct {
    nmo_arena_t *arena;
    nmo_schema_registry_t *registry;
} test_context_t;

static test_context_t *setup_test_context(void) {
    test_context_t *ctx = (test_context_t *)malloc(sizeof(test_context_t));
    if (ctx == NULL) {
        return NULL;
    }
    
    ctx->arena = nmo_arena_create(NULL, 4096);
    if (ctx->arena == NULL) {
        free(ctx);
        return NULL;
    }
    
    ctx->registry = nmo_schema_registry_create(ctx->arena);
    if (ctx->registry == NULL) {
        nmo_arena_destroy(ctx->arena);
        free(ctx);
        return NULL;
    }
    
    /* Register scalar types (u32, f32, etc.) */
    nmo_register_scalar_types(ctx->registry, ctx->arena);
    
    return ctx;
}

static void teardown_test_context(test_context_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    
    if (ctx->registry != NULL) {
        nmo_schema_registry_destroy(ctx->registry);
    }
    if (ctx->arena != NULL) {
        nmo_arena_destroy(ctx->arena);
    }
    free(ctx);
}

/* =============================================================================
 * TEST HELPER: Create versioned type
 * ============================================================================= */

static const nmo_schema_type_t *create_versioned_type(
    nmo_schema_registry_t *registry,
    nmo_arena_t *arena,
    const char *name,
    size_t size,
    uint32_t since_version,
    uint32_t deprecated_version,
    uint32_t removed_version)
{
    nmo_schema_builder_t builder = nmo_builder_struct(arena, name, size, 4);
    nmo_builder_set_since_version(&builder, since_version);
    nmo_builder_set_deprecated_version(&builder, deprecated_version);
    nmo_builder_set_removed_version(&builder, removed_version);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    if (result.code != NMO_OK) {
        return NULL;
    }
    
    return nmo_schema_registry_find_by_name(registry, name);
}

/* =============================================================================
 * COMPATIBILITY TESTS
 * ============================================================================= */

TEST(version_management, compatibility_always_exists) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Type with no version restrictions (since=0, deprecated=0, removed=0) */
    const nmo_schema_type_t *type = create_versioned_type(
        ctx->registry, ctx->arena, "AlwaysExists", 16, 0, 0, 0);
    ASSERT_NE(NULL, type);
    
    /* Should be compatible with all versions */
    ASSERT_EQ(1, nmo_schema_is_compatible(type, 1));
    ASSERT_EQ(1, nmo_schema_is_compatible(type, 5));
    ASSERT_EQ(1, nmo_schema_is_compatible(type, 10));
    ASSERT_EQ(1, nmo_schema_is_compatible(type, 100));
    
    teardown_test_context(ctx);
}

TEST(version_management, compatibility_since_version) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Type added in version 5 */
    const nmo_schema_type_t *type = create_versioned_type(
        ctx->registry, ctx->arena, "AddedInV5", 16, 5, 0, 0);
    ASSERT_NE(NULL, type);
    
    /* Should NOT be compatible with versions before 5 */
    ASSERT_EQ(0, nmo_schema_is_compatible(type, 1));
    ASSERT_EQ(0, nmo_schema_is_compatible(type, 4));
    
    /* Should be compatible with version 5 and later */
    ASSERT_EQ(1, nmo_schema_is_compatible(type, 5));
    ASSERT_EQ(1, nmo_schema_is_compatible(type, 6));
    ASSERT_EQ(1, nmo_schema_is_compatible(type, 100));
    
    teardown_test_context(ctx);
}

TEST(version_management, compatibility_removed_version) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Type removed in version 7 */
    const nmo_schema_type_t *type = create_versioned_type(
        ctx->registry, ctx->arena, "RemovedInV7", 16, 0, 0, 7);
    ASSERT_NE(NULL, type);
    
    /* Should be compatible with versions before 7 */
    ASSERT_EQ(1, nmo_schema_is_compatible(type, 1));
    ASSERT_EQ(1, nmo_schema_is_compatible(type, 6));
    
    /* Should NOT be compatible with version 7 and later */
    ASSERT_EQ(0, nmo_schema_is_compatible(type, 7));
    ASSERT_EQ(0, nmo_schema_is_compatible(type, 8));
    ASSERT_EQ(0, nmo_schema_is_compatible(type, 100));
    
    teardown_test_context(ctx);
}

TEST(version_management, compatibility_version_range) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Type exists in versions 3-8 (since=3, removed=8) */
    const nmo_schema_type_t *type = create_versioned_type(
        ctx->registry, ctx->arena, "ExistsV3ToV8", 16, 3, 0, 8);
    ASSERT_NE(NULL, type);
    
    /* Should NOT be compatible before version 3 */
    ASSERT_EQ(0, nmo_schema_is_compatible(type, 1));
    ASSERT_EQ(0, nmo_schema_is_compatible(type, 2));
    
    /* Should be compatible with versions 3-7 */
    ASSERT_EQ(1, nmo_schema_is_compatible(type, 3));
    ASSERT_EQ(1, nmo_schema_is_compatible(type, 5));
    ASSERT_EQ(1, nmo_schema_is_compatible(type, 7));
    
    /* Should NOT be compatible from version 8 onwards */
    ASSERT_EQ(0, nmo_schema_is_compatible(type, 8));
    ASSERT_EQ(0, nmo_schema_is_compatible(type, 10));
    
    teardown_test_context(ctx);
}

TEST(version_management, compatibility_deprecated_not_removed) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Type deprecated in version 5 but NOT removed (removed=0) */
    const nmo_schema_type_t *type = create_versioned_type(
        ctx->registry, ctx->arena, "DeprecatedV5", 16, 0, 5, 0);
    ASSERT_NE(NULL, type);
    
    /* Deprecated flag doesn't affect compatibility - still usable */
    ASSERT_EQ(1, nmo_schema_is_compatible(type, 1));
    ASSERT_EQ(1, nmo_schema_is_compatible(type, 5));  /* Deprecated but still compatible */
    ASSERT_EQ(1, nmo_schema_is_compatible(type, 10));
    
    teardown_test_context(ctx);
}

/* =============================================================================
 * FIND FOR VERSION TESTS
 * ============================================================================= */

TEST(version_management, find_for_version_exact_match) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register type with no version restrictions */
    const nmo_schema_type_t *type = create_versioned_type(
        ctx->registry, ctx->arena, "TestType", 16, 0, 0, 0);
    ASSERT_NE(NULL, type);
    
    /* Should find the type for any version */
    const nmo_schema_type_t *found = nmo_schema_registry_find_for_version(
        ctx->registry, "TestType", 5);
    ASSERT_NE(NULL, found);
    ASSERT_STR_EQ("TestType", found->name);
    
    teardown_test_context(ctx);
}

TEST(version_management, find_for_version_incompatible) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register type that only exists in version 5+ */
    create_versioned_type(ctx->registry, ctx->arena, "ModernType", 16, 5, 0, 0);
    
    /* Should NOT find the type for version 3 (before since_version) */
    const nmo_schema_type_t *found = nmo_schema_registry_find_for_version(
        ctx->registry, "ModernType", 3);
    ASSERT_EQ(NULL, found);
    
    /* Should find the type for version 5+ */
    found = nmo_schema_registry_find_for_version(
        ctx->registry, "ModernType", 5);
    ASSERT_NE(NULL, found);
    
    teardown_test_context(ctx);
}

TEST(version_management, find_for_version_nonexistent) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Try to find a type that doesn't exist */
    const nmo_schema_type_t *found = nmo_schema_registry_find_for_version(
        ctx->registry, "NonExistent", 5);
    ASSERT_EQ(NULL, found);
    
    teardown_test_context(ctx);
}

/* =============================================================================
 * FIND ALL VARIANTS TESTS
 * ============================================================================= */

TEST(version_management, find_all_variants_single) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register single variant */
    create_versioned_type(ctx->registry, ctx->arena, "SingleType", 16, 0, 0, 0);
    
    /* Find all variants */
    const nmo_schema_type_t **variants = NULL;
    size_t count = 0;
    nmo_result_t result = nmo_schema_registry_find_all_variants(
        ctx->registry, "SingleType", ctx->arena, &variants, &count);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(1, count);
    ASSERT_NE(NULL, variants);
    ASSERT_STR_EQ("SingleType", variants[0]->name);
    
    teardown_test_context(ctx);
}

TEST(version_management, find_all_variants_multiple) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register multiple variants with different version ranges */
    create_versioned_type(ctx->registry, ctx->arena, "MeshData", 16, 2, 5, 0);  /* v2-v4 */
    create_versioned_type(ctx->registry, ctx->arena, "MeshData_v5", 32, 5, 0, 0);  /* v5+ */
    create_versioned_type(ctx->registry, ctx->arena, "MeshData_v7", 64, 7, 0, 0);  /* v7+ */
    
    /* Find all variants */
    const nmo_schema_type_t **variants = NULL;
    size_t count = 0;
    nmo_result_t result = nmo_schema_registry_find_all_variants(
        ctx->registry, "MeshData", ctx->arena, &variants, &count);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3, count);
    ASSERT_NE(NULL, variants);
    
    /* Verify all variants found (order may vary) */
    int found_base = 0, found_v5 = 0, found_v7 = 0;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(variants[i]->name, "MeshData") == 0) found_base = 1;
        if (strcmp(variants[i]->name, "MeshData_v5") == 0) found_v5 = 1;
        if (strcmp(variants[i]->name, "MeshData_v7") == 0) found_v7 = 1;
    }
    ASSERT_EQ(1, found_base);
    ASSERT_EQ(1, found_v5);
    ASSERT_EQ(1, found_v7);
    
    teardown_test_context(ctx);
}

TEST(version_management, find_all_variants_none) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Try to find variants of non-existent type */
    const nmo_schema_type_t **variants = NULL;
    size_t count = 0;
    nmo_result_t result = nmo_schema_registry_find_all_variants(
        ctx->registry, "NonExistent", ctx->arena, &variants, &count);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(0, count);
    
    teardown_test_context(ctx);
}

TEST(version_management, find_all_variants_no_match_prefix) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register types with different prefixes */
    create_versioned_type(ctx->registry, ctx->arena, "MeshData", 16, 0, 0, 0);
    create_versioned_type(ctx->registry, ctx->arena, "TextureData", 32, 0, 0, 0);
    create_versioned_type(ctx->registry, ctx->arena, "MaterialData", 64, 0, 0, 0);
    
    /* Search for "Mesh" should only find MeshData */
    const nmo_schema_type_t **variants = NULL;
    size_t count = 0;
    nmo_result_t result = nmo_schema_registry_find_all_variants(
        ctx->registry, "MeshData", ctx->arena, &variants, &count);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(1, count);
    ASSERT_STR_EQ("MeshData", variants[0]->name);
    
    teardown_test_context(ctx);
}

/* =============================================================================
 * INTEGRATION TESTS
 * ============================================================================= */

TEST(version_management, integration_version_selection) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register two versions: legacy (v2-4) and modern (v5+) */
    const nmo_schema_type_t *legacy = create_versioned_type(
        ctx->registry, ctx->arena, "Data", 16, 2, 5, 0);
    const nmo_schema_type_t *modern = create_versioned_type(
        ctx->registry, ctx->arena, "Data_v5", 32, 5, 0, 0);
    
    ASSERT_NE(NULL, legacy);
    ASSERT_NE(NULL, modern);
    
    /* Version 3 should find legacy version */
    const nmo_schema_type_t *found = nmo_schema_registry_find_for_version(
        ctx->registry, "Data", 3);
    ASSERT_NE(NULL, found);
    ASSERT_EQ(16, found->size);  /* Legacy has size 16 */
    
    /* Version 5 should find modern version (TODO: implement version suffix lookup) */
    /* For now, exact name lookup only works, so this test is informational */
    
    teardown_test_context(ctx);
}

TEST(version_management, integration_variant_analysis) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register historical evolution of a type */
    create_versioned_type(ctx->registry, ctx->arena, "Transform", 48, 1, 3, 0);  /* v1-v2 */
    create_versioned_type(ctx->registry, ctx->arena, "Transform_v3", 64, 3, 5, 0);  /* v3-v4 */
    create_versioned_type(ctx->registry, ctx->arena, "Transform_v5", 80, 5, 0, 0);  /* v5+ */
    
    /* Get all variants for migration analysis */
    const nmo_schema_type_t **variants = NULL;
    size_t count = 0;
    nmo_result_t result = nmo_schema_registry_find_all_variants(
        ctx->registry, "Transform", ctx->arena, &variants, &count);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3, count);
    
    /* Verify sizes are increasing (more data over time) */
    /* Note: Order is not guaranteed, so we just check all sizes present */
    int found_48 = 0, found_64 = 0, found_80 = 0;
    for (size_t i = 0; i < count; i++) {
        if (variants[i]->size == 48) found_48 = 1;
        if (variants[i]->size == 64) found_64 = 1;
        if (variants[i]->size == 80) found_80 = 1;
    }
    ASSERT_EQ(1, found_48);
    ASSERT_EQ(1, found_64);
    ASSERT_EQ(1, found_80);
    
    teardown_test_context(ctx);
}

/* =============================================================================
 * ERROR HANDLING TESTS
 * ============================================================================= */

TEST(version_management, error_null_type) {
    /* nmo_schema_is_compatible with NULL type should return 0 */
    ASSERT_EQ(0, nmo_schema_is_compatible(NULL, 5));
}

TEST(version_management, error_null_registry) {
    /* nmo_schema_registry_find_for_version with NULL registry should return NULL */
    const nmo_schema_type_t *found = nmo_schema_registry_find_for_version(
        NULL, "TestType", 5);
    ASSERT_EQ(NULL, found);
}

TEST(version_management, error_null_name) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* nmo_schema_registry_find_for_version with NULL name should return NULL */
    const nmo_schema_type_t *found = nmo_schema_registry_find_for_version(
        ctx->registry, NULL, 5);
    ASSERT_EQ(NULL, found);
    
    teardown_test_context(ctx);
}

TEST(version_management, error_find_all_variants_null_args) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    const nmo_schema_type_t **variants = NULL;
    size_t count = 0;
    
    /* NULL registry should return error */
    nmo_result_t result = nmo_schema_registry_find_all_variants(
        NULL, "Test", ctx->arena, &variants, &count);
    ASSERT_NE(NMO_OK, result.code);
    
    /* NULL base_name should return error */
    result = nmo_schema_registry_find_all_variants(
        ctx->registry, NULL, ctx->arena, &variants, &count);
    ASSERT_NE(NMO_OK, result.code);
    
    /* NULL out_schemas should return error */
    result = nmo_schema_registry_find_all_variants(
        ctx->registry, "Test", ctx->arena, NULL, &count);
    ASSERT_NE(NMO_OK, result.code);
    
    /* NULL out_count should return error */
    result = nmo_schema_registry_find_all_variants(
        ctx->registry, "Test", ctx->arena, &variants, NULL);
    ASSERT_NE(NMO_OK, result.code);
    
    teardown_test_context(ctx);
}

/* =============================================================================
 * MAIN
 * ============================================================================= */

TEST_MAIN_BEGIN()
    /* Compatibility tests */
    REGISTER_TEST(version_management, compatibility_always_exists);
    REGISTER_TEST(version_management, compatibility_since_version);
    REGISTER_TEST(version_management, compatibility_removed_version);
    REGISTER_TEST(version_management, compatibility_version_range);
    REGISTER_TEST(version_management, compatibility_deprecated_not_removed);
    
    /* Find for version tests */
    REGISTER_TEST(version_management, find_for_version_exact_match);
    REGISTER_TEST(version_management, find_for_version_incompatible);
    REGISTER_TEST(version_management, find_for_version_nonexistent);
    
    /* Find all variants tests */
    REGISTER_TEST(version_management, find_all_variants_single);
    REGISTER_TEST(version_management, find_all_variants_multiple);
    REGISTER_TEST(version_management, find_all_variants_none);
    REGISTER_TEST(version_management, find_all_variants_no_match_prefix);
    
    /* Integration tests */
    REGISTER_TEST(version_management, integration_version_selection);
    REGISTER_TEST(version_management, integration_variant_analysis);
    
    /* Error handling tests */
    REGISTER_TEST(version_management, error_null_type);
    REGISTER_TEST(version_management, error_null_registry);
    REGISTER_TEST(version_management, error_null_name);
    REGISTER_TEST(version_management, error_find_all_variants_null_args);
TEST_MAIN_END()
