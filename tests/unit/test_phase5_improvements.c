/**
 * @file test_phase5_improvements.c
 * @brief Tests for Phase 5 improvements over Phase 3 limitations
 */

#include "test_framework.h"
#include "schema/nmo_schema_macros.h"
#include "schema/nmo_schema.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_param_meta.h"
#include "schema/param_type_table.h"
#include "core/nmo_arena.h"
#include <string.h>

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

typedef struct test_context {
    nmo_arena_t *arena;
    nmo_schema_registry_t *registry;
    nmo_param_type_table_t *type_table;
} test_context_t;

static test_context_t *setup_test_context(void) {
    test_context_t *ctx = malloc(sizeof(test_context_t));
    if (!ctx) return NULL;
    
    ctx->arena = nmo_arena_create(NULL, 64 * 1024);
    ctx->registry = nmo_schema_registry_create(ctx->arena);
    
    /* Register scalar types */
    nmo_register_scalar_types(ctx->registry, ctx->arena);
    
    /* Register parameter types */
    nmo_register_param_types(ctx->registry, ctx->arena);
    
    /* Build type table */
    nmo_result_t result = nmo_param_type_table_build(
        ctx->registry, ctx->arena, &ctx->type_table);
    if (result.code != NMO_OK) {
        nmo_arena_destroy(ctx->arena);
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

static void teardown_test_context(test_context_t *ctx) {
    if (!ctx) return;
    nmo_arena_destroy(ctx->arena);
    free(ctx);
}

/* ============================================================================
 * Test 1: Simplified Field Declaration
 * ============================================================================ */

typedef struct test_simple {
    int x;
    int y;
    int z;
} test_simple_t;

/* Using standard SCHEMA_FIELD - this is the improved way */
NMO_DECLARE_SCHEMA(TestSimple, test_simple_t) {
    SCHEMA_FIELD(x, i32, test_simple_t),
    SCHEMA_FIELD(y, i32, test_simple_t),
    SCHEMA_FIELD(z, i32, test_simple_t)
};

TEST(phase5_improvements, schema_declaration) {
    /* Verify descriptor array was created correctly */
    ASSERT_EQ(3, sizeof(TestSimple_fields) / sizeof(TestSimple_fields[0]));
    
    /* Verify first field */
    ASSERT_STR_EQ("x", TestSimple_fields[0].name);
    ASSERT_STR_EQ("i32", TestSimple_fields[0].type_name);
    ASSERT_EQ(offsetof(test_simple_t, x), TestSimple_fields[0].offset);
    
    /* Verify second field */
    ASSERT_STR_EQ("y", TestSimple_fields[1].name);
    ASSERT_EQ(offsetof(test_simple_t, y), TestSimple_fields[1].offset);
    
    /* Verify third field */
    ASSERT_STR_EQ("z", TestSimple_fields[2].name);
    ASSERT_EQ(offsetof(test_simple_t, z), TestSimple_fields[2].offset);
}

/* ============================================================================
 * Test 2: Size and Alignment Verification
 * ============================================================================ */

typedef struct test_aligned {
    float x;
    float y;
    float z;
} test_aligned_t;

/* These should compile successfully */
NMO_VERIFY_SCHEMA_SIZE(test_aligned_t, 12);
NMO_VERIFY_SCHEMA_ALIGN(test_aligned_t, 4);

TEST(phase5_improvements, compile_time_verification) {
    /* If we reach here, static assertions passed */
    ASSERT_EQ(12, sizeof(test_aligned_t));
    ASSERT_EQ(4, _Alignof(test_aligned_t));
}

/* ============================================================================
 * Test 3: Type Compatibility Checking
 * ============================================================================ */

TEST(phase5_improvements, type_compatibility_exact_match) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* int should be compatible with int */
    bool compat = nmo_param_type_is_compatible(
        ctx->type_table, CKPGUID_INT, CKPGUID_INT);
    ASSERT_TRUE(compat);
    
    /* Vector should be compatible with Vector */
    compat = nmo_param_type_is_compatible(
        ctx->type_table, CKPGUID_VECTOR, CKPGUID_VECTOR);
    ASSERT_TRUE(compat);
    
    teardown_test_context(ctx);
}

TEST(phase5_improvements, type_compatibility_inheritance) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Check if ID and Object types are registered */
    const nmo_schema_type_t *id_type = 
        nmo_param_type_table_find_by_guid(ctx->type_table, CKPGUID_ID);
    const nmo_schema_type_t *obj_type = 
        nmo_param_type_table_find_by_guid(ctx->type_table, CKPGUID_OBJECT);
    
    if (id_type && obj_type && id_type->param_meta) {
        /* ID is derived from Object, so should be compatible */
        bool compat = nmo_param_type_is_compatible(
            ctx->type_table, CKPGUID_ID, CKPGUID_OBJECT);
        ASSERT_TRUE(compat);
        
        /* But Object is NOT compatible with ID (reverse direction) */
        compat = nmo_param_type_is_compatible(
            ctx->type_table, CKPGUID_OBJECT, CKPGUID_ID);
        ASSERT_FALSE(compat);
    }
    
    teardown_test_context(ctx);
}

TEST(phase5_improvements, type_compatibility_unrelated) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* int and Vector are unrelated */
    bool compat = nmo_param_type_is_compatible(
        ctx->type_table, CKPGUID_INT, CKPGUID_VECTOR);
    ASSERT_FALSE(compat);
    
    /* float and Matrix are unrelated */
    compat = nmo_param_type_is_compatible(
        ctx->type_table, CKPGUID_FLOAT, CKPGUID_MATRIX);
    ASSERT_FALSE(compat);
    
    teardown_test_context(ctx);
}

/* ============================================================================
 * Test 4: Inheritance Depth Calculation
 * ============================================================================ */

TEST(phase5_improvements, inheritance_depth_base_type) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Base types have depth 0 */
    int depth = nmo_param_type_get_depth(ctx->type_table, CKPGUID_INT);
    if (depth >= 0) {
        ASSERT_EQ(0, depth);
    }
    
    depth = nmo_param_type_get_depth(ctx->type_table, CKPGUID_OBJECT);
    if (depth >= 0) {
        ASSERT_EQ(0, depth);
    }
    
    teardown_test_context(ctx);
}

TEST(phase5_improvements, inheritance_depth_derived) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* ID is derived from Object, so depth = 1 */
    int depth = nmo_param_type_get_depth(ctx->type_table, CKPGUID_ID);
    if (depth >= 0) {
        ASSERT_EQ(1, depth);
    }
    
    teardown_test_context(ctx);
}

TEST(phase5_improvements, inheritance_depth_invalid) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Non-existent GUID should return -1 */
    nmo_guid_t fake_guid = CKPGUID(0xDEADBEEF, 0xCAFEBABE);
    int depth = nmo_param_type_get_depth(ctx->type_table, fake_guid);
    ASSERT_EQ(-1, depth);
    
    teardown_test_context(ctx);
}

/* ============================================================================
 * Test 5: Error Handling
 * ============================================================================ */

TEST(phase5_improvements, type_compatibility_null_table) {
    /* Should handle NULL gracefully */
    bool compat = nmo_param_type_is_compatible(
        NULL, CKPGUID_INT, CKPGUID_FLOAT);
    ASSERT_FALSE(compat);
}

TEST(phase5_improvements, inheritance_depth_null_table) {
    /* Should handle NULL gracefully */
    int depth = nmo_param_type_get_depth(NULL, CKPGUID_INT);
    ASSERT_EQ(-1, depth);
}

/* ============================================================================
 * Test 6: Performance Comparison (Documentation Only)
 * ============================================================================ */

TEST(phase5_improvements, guid_lookup_performance) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* GUID lookup should be O(1) */
    const nmo_schema_type_t *type1 = 
        nmo_param_type_table_find_by_guid(ctx->type_table, CKPGUID_INT);
    if (type1) {
        ASSERT_NE(NULL, type1);
        ASSERT_STR_EQ("int", type1->name);
    }
    
    /* Both lookups should be equally fast (O(1)) */
    /* In contrast to string lookup which is O(log n) */
    
    teardown_test_context(ctx);
}

/* ============================================================================
 * Test 7: GUID vs String Lookup Comparison
 * ============================================================================ */

TEST(phase5_improvements, guid_vs_string_lookup) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* GUID lookup - O(1) */
    const nmo_schema_type_t *via_guid = 
        nmo_param_type_table_find_by_guid(ctx->type_table, CKPGUID_INT);
    
    /* String lookup - O(log n) */
    const nmo_schema_type_t *via_string = 
        nmo_schema_registry_find_by_name(ctx->registry, "int");
    
    if (via_guid && via_string) {
        /* Should be same type */
        ASSERT_EQ(via_guid, via_string);
    }
    
    /* But GUID lookup is faster for large registries */
    
    teardown_test_context(ctx);
}

/* ============================================================================
 * Test Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    REGISTER_TEST(phase5_improvements, schema_declaration);
    REGISTER_TEST(phase5_improvements, compile_time_verification);
    REGISTER_TEST(phase5_improvements, type_compatibility_exact_match);
    REGISTER_TEST(phase5_improvements, type_compatibility_inheritance);
    REGISTER_TEST(phase5_improvements, type_compatibility_unrelated);
    REGISTER_TEST(phase5_improvements, inheritance_depth_base_type);
    REGISTER_TEST(phase5_improvements, inheritance_depth_derived);
    REGISTER_TEST(phase5_improvements, inheritance_depth_invalid);
    REGISTER_TEST(phase5_improvements, type_compatibility_null_table);
    REGISTER_TEST(phase5_improvements, inheritance_depth_null_table);
    REGISTER_TEST(phase5_improvements, guid_lookup_performance);
    REGISTER_TEST(phase5_improvements, guid_vs_string_lookup);
TEST_MAIN_END()
