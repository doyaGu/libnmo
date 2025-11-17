/**
 * @file test_param_meta.c
 * @brief Unit tests for Parameter metadata system
 *
 * Tests parameter type registration, metadata query, and type table building.
 */

#include "test_framework.h"
#include "schema/nmo_param_meta.h"
#include "schema/param_type_table.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_schema_builder.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include <string.h>

/* =============================================================================
 * TEST CONTEXT
 * ============================================================================= */

typedef struct test_context {
    nmo_arena_t *arena;
    nmo_schema_registry_t *registry;
} test_context_t;

static test_context_t *setup_test_context(void)
{
    test_context_t *ctx = malloc(sizeof(test_context_t));
    ctx->arena = nmo_arena_create(NULL, 4096);
    ctx->registry = nmo_schema_registry_create(ctx->arena);
    return ctx;
}

static void teardown_test_context(test_context_t *ctx)
{
    if (ctx) {
        nmo_arena_destroy(ctx->arena);
        free(ctx);
    }
}

/* =============================================================================
 * PARAM TYPES REGISTRATION TESTS
 * ============================================================================= */

TEST(param_meta, register_param_types)
{
    test_context_t *ctx = setup_test_context();
    
    /* Register all parameter types */
    nmo_result_t result = nmo_register_param_types(ctx->registry, ctx->arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify scalar types exist */
    const nmo_schema_type_t *int_type = nmo_schema_registry_find_by_name(ctx->registry, "int");
    ASSERT_NE(NULL, int_type);
    ASSERT_NE(NULL, int_type->param_meta);
    ASSERT_EQ(NMO_PARAM_SCALAR, int_type->param_meta->kind);
    ASSERT_EQ(4, int_type->param_meta->default_size);
    
    const nmo_schema_type_t *float_type = nmo_schema_registry_find_by_name(ctx->registry, "float");
    ASSERT_NE(NULL, float_type);
    ASSERT_NE(NULL, float_type->param_meta);
    ASSERT_EQ(NMO_PARAM_SCALAR, float_type->param_meta->kind);
    
    const nmo_schema_type_t *bool_type = nmo_schema_registry_find_by_name(ctx->registry, "bool");
    ASSERT_NE(NULL, bool_type);
    ASSERT_NE(NULL, bool_type->param_meta);
    
    const nmo_schema_type_t *string_type = nmo_schema_registry_find_by_name(ctx->registry, "string");
    ASSERT_NE(NULL, string_type);
    ASSERT_NE(NULL, string_type->param_meta);
    ASSERT_EQ(0, string_type->param_meta->default_size); /* Variable size */
    
    teardown_test_context(ctx);
}

TEST(param_meta, verify_math_types)
{
    test_context_t *ctx = setup_test_context();
    nmo_register_param_types(ctx->registry, ctx->arena);
    
    /* Vector (12 bytes) */
    const nmo_schema_type_t *vec_type = nmo_schema_registry_find_by_name(ctx->registry, "Vector");
    ASSERT_NE(NULL, vec_type);
    ASSERT_NE(NULL, vec_type->param_meta);
    ASSERT_EQ(NMO_PARAM_STRUCT, vec_type->param_meta->kind);
    ASSERT_EQ(12, vec_type->param_meta->default_size);
    
    /* 2DVector (8 bytes) */
    const nmo_schema_type_t *vec2d_type = nmo_schema_registry_find_by_name(ctx->registry, "2DVector");
    ASSERT_NE(NULL, vec2d_type);
    ASSERT_EQ(8, vec2d_type->param_meta->default_size);
    
    /* Quaternion (16 bytes) */
    const nmo_schema_type_t *quat_type = nmo_schema_registry_find_by_name(ctx->registry, "Quaternion");
    ASSERT_NE(NULL, quat_type);
    ASSERT_EQ(16, quat_type->param_meta->default_size);
    
    /* Matrix (64 bytes) */
    const nmo_schema_type_t *mat_type = nmo_schema_registry_find_by_name(ctx->registry, "Matrix");
    ASSERT_NE(NULL, mat_type);
    ASSERT_EQ(64, mat_type->param_meta->default_size);
    
    /* Color (16 bytes) */
    const nmo_schema_type_t *color_type = nmo_schema_registry_find_by_name(ctx->registry, "Color");
    ASSERT_NE(NULL, color_type);
    ASSERT_EQ(16, color_type->param_meta->default_size);
    
    teardown_test_context(ctx);
}

TEST(param_meta, verify_object_ref_types)
{
    test_context_t *ctx = setup_test_context();
    nmo_register_param_types(ctx->registry, ctx->arena);
    
    /* Object type */
    const nmo_schema_type_t *obj_type = nmo_schema_registry_find_by_name(ctx->registry, "Object");
    ASSERT_NE(NULL, obj_type);
    ASSERT_NE(NULL, obj_type->param_meta);
    ASSERT_EQ(NMO_PARAM_OBJECT_REF, obj_type->param_meta->kind);
    ASSERT_EQ(4, obj_type->param_meta->default_size);
    
    /* ID type (derived from Object) */
    const nmo_schema_type_t *id_type = nmo_schema_registry_find_by_name(ctx->registry, "ID");
    ASSERT_NE(NULL, id_type);
    ASSERT_NE(NULL, id_type->param_meta);
    ASSERT_EQ(NMO_PARAM_OBJECT_REF, id_type->param_meta->kind);
    
    /* Verify ID is derived from Object */
    ASSERT_TRUE(nmo_guid_equals(id_type->param_meta->derived_from, obj_type->param_meta->guid));
    ASSERT_TRUE(id_type->param_meta->flags & NMO_PARAM_FLAG_DERIVED);
    
    teardown_test_context(ctx);
}

/* =============================================================================
 * PARAM GUID TESTS
 * ============================================================================= */

TEST(param_meta, verify_guids_unique)
{
    test_context_t *ctx = setup_test_context();
    nmo_register_param_types(ctx->registry, ctx->arena);
    
    /* Get multiple types and verify GUIDs are unique */
    const nmo_schema_type_t *int_type = nmo_schema_registry_find_by_name(ctx->registry, "int");
    const nmo_schema_type_t *float_type = nmo_schema_registry_find_by_name(ctx->registry, "float");
    const nmo_schema_type_t *vec_type = nmo_schema_registry_find_by_name(ctx->registry, "Vector");
    
    ASSERT_NE(NULL, int_type);
    ASSERT_NE(NULL, float_type);
    ASSERT_NE(NULL, vec_type);
    
    /* All GUIDs should be different */
    ASSERT_FALSE(nmo_guid_equals(int_type->param_meta->guid, float_type->param_meta->guid));
    ASSERT_FALSE(nmo_guid_equals(int_type->param_meta->guid, vec_type->param_meta->guid));
    ASSERT_FALSE(nmo_guid_equals(float_type->param_meta->guid, vec_type->param_meta->guid));
    
    /* No GUID should be null */
    ASSERT_FALSE(nmo_guid_is_null(int_type->param_meta->guid));
    ASSERT_FALSE(nmo_guid_is_null(float_type->param_meta->guid));
    ASSERT_FALSE(nmo_guid_is_null(vec_type->param_meta->guid));
    
    teardown_test_context(ctx);
}

TEST(param_meta, verify_standard_guids)
{
    test_context_t *ctx = setup_test_context();
    nmo_register_param_types(ctx->registry, ctx->arena);
    
    /* Verify standard GUID values match constants */
    const nmo_schema_type_t *int_type = nmo_schema_registry_find_by_name(ctx->registry, "int");
    ASSERT_TRUE(nmo_guid_equals(int_type->param_meta->guid, CKPGUID_INT));
    
    const nmo_schema_type_t *float_type = nmo_schema_registry_find_by_name(ctx->registry, "float");
    ASSERT_TRUE(nmo_guid_equals(float_type->param_meta->guid, CKPGUID_FLOAT));
    
    const nmo_schema_type_t *vec_type = nmo_schema_registry_find_by_name(ctx->registry, "Vector");
    ASSERT_TRUE(nmo_guid_equals(vec_type->param_meta->guid, CKPGUID_VECTOR));
    
    const nmo_schema_type_t *mat_type = nmo_schema_registry_find_by_name(ctx->registry, "Matrix");
    ASSERT_TRUE(nmo_guid_equals(mat_type->param_meta->guid, CKPGUID_MATRIX));
    
    teardown_test_context(ctx);
}

/* =============================================================================
 * PARAM TYPE TABLE TESTS
 * ============================================================================= */

TEST(param_meta, build_type_table)
{
    test_context_t *ctx = setup_test_context();
    nmo_register_param_types(ctx->registry, ctx->arena);
    
    /* Build parameter type table */
    nmo_param_type_table_t *table = NULL;
    nmo_result_t result = nmo_param_type_table_build(ctx->registry, ctx->arena, &table);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_NE(NULL, table);
    ASSERT_GT(table->type_count, 0);
    
    teardown_test_context(ctx);
}

TEST(param_meta, type_table_lookup_by_guid)
{
    test_context_t *ctx = setup_test_context();
    nmo_register_param_types(ctx->registry, ctx->arena);
    
    nmo_param_type_table_t *table = NULL;
    nmo_param_type_table_build(ctx->registry, ctx->arena, &table);
    
    /* Lookup int type by GUID */
    const nmo_schema_type_t *found = nmo_param_type_table_find_by_guid(table, CKPGUID_INT);
    ASSERT_NE(NULL, found);
    ASSERT_STR_EQ("int", found->name);
    
    /* Lookup Vector by GUID */
    found = nmo_param_type_table_find_by_guid(table, CKPGUID_VECTOR);
    ASSERT_NE(NULL, found);
    ASSERT_STR_EQ("Vector", found->name);
    
    /* Lookup Matrix by GUID */
    found = nmo_param_type_table_find_by_guid(table, CKPGUID_MATRIX);
    ASSERT_NE(NULL, found);
    ASSERT_STR_EQ("Matrix", found->name);
    
    teardown_test_context(ctx);
}

TEST(param_meta, type_table_lookup_nonexistent)
{
    test_context_t *ctx = setup_test_context();
    nmo_register_param_types(ctx->registry, ctx->arena);
    
    nmo_param_type_table_t *table = NULL;
    nmo_param_type_table_build(ctx->registry, ctx->arena, &table);
    
    /* Lookup non-existent GUID */
    nmo_guid_t fake_guid = CKPGUID(0xDEADBEEF, 0xCAFEBABE);
    const nmo_schema_type_t *found = nmo_param_type_table_find_by_guid(table, fake_guid);
    ASSERT_EQ(NULL, found);
    
    teardown_test_context(ctx);
}

TEST(param_meta, type_table_statistics)
{
    test_context_t *ctx = setup_test_context();
    nmo_register_param_types(ctx->registry, ctx->arena);
    
    nmo_param_type_table_t *table = NULL;
    nmo_param_type_table_build(ctx->registry, ctx->arena, &table);
    
    /* Check that table was built */
    ASSERT_NE(NULL, table);
    ASSERT_EQ(14, table->type_count);  /* Should be 14 types registered */
    
    nmo_param_type_table_stats_t stats = {0};
    nmo_param_type_table_get_stats(table, &stats);
    
    /* Verify total types */
    ASSERT_EQ(14, stats.total_types);
    
    /* Verify counts add up */
    size_t sum = stats.scalar_count + stats.struct_count + stats.enum_count + stats.object_ref_count;
    ASSERT_EQ(14, sum);  /* All types should be categorized */
    
    /* At least some of each category should exist (5 scalars, 7 structs, 2 refs expected) */
    ASSERT_GT(stats.scalar_count, 0);
    ASSERT_GT(stats.struct_count, 0);
    ASSERT_GT(stats.object_ref_count, 0);
    
    teardown_test_context(ctx);
}

/* =============================================================================
 * PARAM FLAGS TESTS
 * ============================================================================= */

TEST(param_meta, verify_flags)
{
    test_context_t *ctx = setup_test_context();
    nmo_register_param_types(ctx->registry, ctx->arena);
    
    /* int should be serializable and animatable */
    const nmo_schema_type_t *int_type = nmo_schema_registry_find_by_name(ctx->registry, "int");
    ASSERT_TRUE(int_type->param_meta->flags & NMO_PARAM_FLAG_SERIALIZABLE);
    ASSERT_TRUE(int_type->param_meta->flags & NMO_PARAM_FLAG_ANIMATABLE);
    
    /* Vector should be serializable and animatable */
    const nmo_schema_type_t *vec_type = nmo_schema_registry_find_by_name(ctx->registry, "Vector");
    ASSERT_TRUE(vec_type->param_meta->flags & NMO_PARAM_FLAG_SERIALIZABLE);
    ASSERT_TRUE(vec_type->param_meta->flags & NMO_PARAM_FLAG_ANIMATABLE);
    
    /* ID should be derived */
    const nmo_schema_type_t *id_type = nmo_schema_registry_find_by_name(ctx->registry, "ID");
    ASSERT_TRUE(id_type->param_meta->flags & NMO_PARAM_FLAG_DERIVED);
    
    teardown_test_context(ctx);
}

/* =============================================================================
 * ERROR HANDLING TESTS
 * ============================================================================= */

TEST(param_meta, type_table_null_registry)
{
    nmo_arena_t *arena = nmo_arena_create(NULL, 4096);
    nmo_param_type_table_t *table = NULL;
    
    nmo_result_t result = nmo_param_type_table_build(NULL, arena, &table);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    ASSERT_EQ(NULL, table);
    
    nmo_arena_destroy(arena);
}

TEST(param_meta, type_table_null_output)
{
    test_context_t *ctx = setup_test_context();
    nmo_register_param_types(ctx->registry, ctx->arena);
    
    nmo_result_t result = nmo_param_type_table_build(ctx->registry, ctx->arena, NULL);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    teardown_test_context(ctx);
}

TEST(param_meta, find_by_guid_null_table)
{
    const nmo_schema_type_t *found = nmo_param_type_table_find_by_guid(NULL, CKPGUID_INT);
    ASSERT_EQ(NULL, found);
}

/* =============================================================================
 * INTEGRATION TEST
 * ============================================================================= */

TEST(param_meta, full_workflow)
{
    test_context_t *ctx = setup_test_context();
    
    /* 1. Register parameter types */
    nmo_result_t result = nmo_register_param_types(ctx->registry, ctx->arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* 2. Build type table */
    nmo_param_type_table_t *table = NULL;
    result = nmo_param_type_table_build(ctx->registry, ctx->arena, &table);
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_NE(NULL, table);
    
    /* 3. Lookup types by name */
    const nmo_schema_type_t *int_type = nmo_schema_registry_find_by_name(ctx->registry, "int");
    const nmo_schema_type_t *vec_type = nmo_schema_registry_find_by_name(ctx->registry, "Vector");
    ASSERT_NE(NULL, int_type);
    ASSERT_NE(NULL, vec_type);
    
    /* 4. Lookup types by GUID */
    const nmo_schema_type_t *int_by_guid = nmo_param_type_table_find_by_guid(table, int_type->param_meta->guid);
    const nmo_schema_type_t *vec_by_guid = nmo_param_type_table_find_by_guid(table, vec_type->param_meta->guid);
    ASSERT_EQ(int_type, int_by_guid);
    ASSERT_EQ(vec_type, vec_by_guid);
    
    /* 5. Verify statistics */
    nmo_param_type_table_stats_t stats;
    nmo_param_type_table_get_stats(table, &stats);
    ASSERT_EQ(14, stats.total_types);
    
    teardown_test_context(ctx);
}

/* =============================================================================
 * TEST REGISTRATION
 * ============================================================================= */

TEST_MAIN_BEGIN()
    /* Registration tests */
    REGISTER_TEST(param_meta, register_param_types);
    REGISTER_TEST(param_meta, verify_math_types);
    REGISTER_TEST(param_meta, verify_object_ref_types);
    
    /* GUID tests */
    REGISTER_TEST(param_meta, verify_guids_unique);
    REGISTER_TEST(param_meta, verify_standard_guids);
    
    /* Type table tests */
    REGISTER_TEST(param_meta, build_type_table);
    REGISTER_TEST(param_meta, type_table_lookup_by_guid);
    REGISTER_TEST(param_meta, type_table_lookup_nonexistent);
    REGISTER_TEST(param_meta, type_table_statistics);
    
    /* Flags tests */
    REGISTER_TEST(param_meta, verify_flags);
    
    /* Error handling tests */
    REGISTER_TEST(param_meta, type_table_null_registry);
    REGISTER_TEST(param_meta, type_table_null_output);
    REGISTER_TEST(param_meta, find_by_guid_null_table);
    
    /* Integration test */
    REGISTER_TEST(param_meta, full_workflow);
TEST_MAIN_END()
