/**
 * @file test_schema_macros.c
 * @brief Unit tests for declarative schema registration macros
 * 
 * Tests all aspects of the macro system:
 * - Field declaration macros
 * - Enum declaration macros
 * - Vtable generation macros
 * - Registration macros
 * - Descriptor-to-schema conversion
 */

#include "test_framework.h"
#include "schema/nmo_schema_macros.h"
#include "schema/nmo_schema_registry.h"
#include "schema/nmo_schema_builder.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include <string.h>

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

typedef struct test_context {
    nmo_arena_t *arena;
    nmo_schema_registry_t *registry;
} test_context_t;

static test_context_t *setup_test_context(void) {
    test_context_t *ctx = malloc(sizeof(test_context_t));
    if (!ctx) return NULL;
    
    ctx->arena = nmo_arena_create(NULL, 65536);
    if (!ctx->arena) {
        free(ctx);
        return NULL;
    }
    
    ctx->registry = nmo_schema_registry_create(ctx->arena);
    if (!ctx->registry) {
        nmo_arena_destroy(ctx->arena);
        free(ctx);
        return NULL;
    }
    
    /* Register scalar types (u8, u16, u32, i8, i16, i32, f32, f64, bool, string, etc.) */
    nmo_result_t result = nmo_register_scalar_types(ctx->registry, ctx->arena);
    if (result.code != NMO_OK) {
        nmo_arena_destroy(ctx->arena);
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

static void teardown_test_context(test_context_t *ctx) {
    if (ctx) {
        if (ctx->arena) {
            nmo_arena_destroy(ctx->arena);
        }
        free(ctx);
    }
}

/* ============================================================================
 * Test Types
 * ============================================================================ */

/* Simple struct with 3 fields */
typedef struct test_vector {
    float x;
    float y;
    float z;
} test_vector_t;

/* Struct with annotations */
typedef struct test_color {
    float r;
    float g;
    float b;
    float a;
} test_color_t;

/* Nested struct */
typedef struct test_box {
    test_vector_t min;
    test_vector_t max;
} test_box_t;

/* Struct with versioned fields */
typedef struct test_versioned {
    uint32_t id;
    uint32_t flags;
    float scale;          /* Added in v5 */
    float deprecated_val; /* Added in v3, deprecated in v7 */
} test_versioned_t;

/* Enum type */
typedef enum test_blend_mode {
    TEST_BLEND_ZERO = 0,
    TEST_BLEND_ONE = 1,
    TEST_BLEND_SRC_COLOR = 2,
    TEST_BLEND_INV_SRC_COLOR = 3,
} test_blend_mode_t;

/* ============================================================================
 * Schema Declarations
 * ============================================================================ */

NMO_DECLARE_SCHEMA(TestVector, test_vector_t) {
    SCHEMA_FIELD(x, f32, test_vector_t),
    SCHEMA_FIELD(y, f32, test_vector_t),
    SCHEMA_FIELD(z, f32, test_vector_t)
};

NMO_DECLARE_SCHEMA(TestColor, test_color_t) {
    SCHEMA_FIELD_EX(r, f32, test_color_t, NMO_ANNOTATION_COLOR),
    SCHEMA_FIELD_EX(g, f32, test_color_t, NMO_ANNOTATION_COLOR),
    SCHEMA_FIELD_EX(b, f32, test_color_t, NMO_ANNOTATION_COLOR),
    SCHEMA_FIELD_EX(a, f32, test_color_t, NMO_ANNOTATION_COLOR)
};

NMO_DECLARE_SCHEMA(TestBox, test_box_t) {
    SCHEMA_FIELD(min, TestVector, test_box_t),
    SCHEMA_FIELD(max, TestVector, test_box_t)
};

NMO_DECLARE_SCHEMA(TestVersioned, test_versioned_t) {
    SCHEMA_FIELD(id, u32, test_versioned_t),
    SCHEMA_FIELD(flags, u32, test_versioned_t),
    SCHEMA_FIELD_VERSIONED(scale, f32, test_versioned_t, 5, 0),
    SCHEMA_FIELD_FULL(deprecated_val, f32, test_versioned_t, 0, 3, 7, 0)
};

NMO_DECLARE_ENUM(TestBlendMode) {
    SCHEMA_ENUM_VALUE(ZERO, 0),
    SCHEMA_ENUM_VALUE(ONE, 1),
    SCHEMA_ENUM_VALUE(SRC_COLOR, 2),
    SCHEMA_ENUM_VALUE(INV_SRC_COLOR, 3)
};

/* ============================================================================
 * Test Cases: Field Declaration Macros
 * ============================================================================ */

TEST(schema_macros, field_descriptor_basic) {
    /* Test basic field descriptor */
    const nmo_schema_field_descriptor_t *field = &TestVector_fields[0];
    
    ASSERT_STR_EQ("x", field->name);
    ASSERT_STR_EQ("f32", field->type_name);
    ASSERT_EQ(offsetof(test_vector_t, x), field->offset);
    ASSERT_EQ(0U, field->annotations);
    ASSERT_EQ(0U, field->since_version);
    ASSERT_EQ(0U, field->deprecated_version);
    ASSERT_EQ(0U, field->removed_version);
}

TEST(schema_macros, field_descriptor_with_annotations) {
    /* Test annotated field descriptor */
    const nmo_schema_field_descriptor_t *field = &TestColor_fields[0];
    
    ASSERT_STR_EQ("r", field->name);
    ASSERT_STR_EQ("f32", field->type_name);
    ASSERT_EQ(offsetof(test_color_t, r), field->offset);
    ASSERT_EQ(NMO_ANNOTATION_COLOR, field->annotations);
}

TEST(schema_macros, field_descriptor_versioned) {
    /* Test versioned field descriptor */
    const nmo_schema_field_descriptor_t *field = &TestVersioned_fields[2]; /* scale */
    
    ASSERT_STR_EQ("scale", field->name);
    ASSERT_EQ(5U, field->since_version);
    ASSERT_EQ(0U, field->deprecated_version);
    
    /* Test deprecated field */
    field = &TestVersioned_fields[3]; /* deprecated_val */
    ASSERT_STR_EQ("deprecated_val", field->name);
    ASSERT_EQ(3U, field->since_version);
    ASSERT_EQ(7U, field->deprecated_version);
}

TEST(schema_macros, field_count) {
    /* Test array sizes match expected field counts */
    size_t vector_count = sizeof(TestVector_fields) / sizeof(TestVector_fields[0]);
    ASSERT_EQ(3U, vector_count);
    
    size_t color_count = sizeof(TestColor_fields) / sizeof(TestColor_fields[0]);
    ASSERT_EQ(4U, color_count);
    
    size_t box_count = sizeof(TestBox_fields) / sizeof(TestBox_fields[0]);
    ASSERT_EQ(2U, box_count);
    
    size_t versioned_count = sizeof(TestVersioned_fields) / sizeof(TestVersioned_fields[0]);
    ASSERT_EQ(4U, versioned_count);
}

/* ============================================================================
 * Test Cases: Enum Declaration Macros
 * ============================================================================ */

TEST(schema_macros, enum_descriptor_basic) {
    /* Test enum value descriptors */
    const nmo_schema_enum_descriptor_t *value = &TestBlendMode_values[0];
    
    ASSERT_STR_EQ("ZERO", value->name);
    ASSERT_EQ(0, value->value);
    
    value = &TestBlendMode_values[2];
    ASSERT_STR_EQ("SRC_COLOR", value->name);
    ASSERT_EQ(2, value->value);
}

TEST(schema_macros, enum_value_count) {
    size_t count = sizeof(TestBlendMode_values) / sizeof(TestBlendMode_values[0]);
    ASSERT_EQ(4U, count);
}

/* ============================================================================
 * Test Cases: Registration Functions
 * ============================================================================ */

TEST(schema_macros, register_simple_struct) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register simple struct */
    nmo_result_t result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, TestVector, test_vector_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify registered type */
    const nmo_schema_type_t *type = 
        nmo_schema_registry_find_by_name(ctx->registry, "TestVector");
    ASSERT_NE(NULL, type);
    ASSERT_STR_EQ("TestVector", type->name);
    ASSERT_EQ(sizeof(test_vector_t), type->size);
    ASSERT_EQ(_Alignof(test_vector_t), type->align);
    ASSERT_EQ(3U, type->field_count);
    ASSERT_EQ(NULL, type->vtable);
    
    teardown_test_context(ctx);
}

TEST(schema_macros, register_annotated_struct) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register annotated struct */
    nmo_result_t result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, TestColor, test_color_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify annotations preserved */
    const nmo_schema_type_t *type = 
        nmo_schema_registry_find_by_name(ctx->registry, "TestColor");
    ASSERT_NE(NULL, type);
    ASSERT_EQ(4U, type->field_count);
    
    /* Check first field has annotation */
    const nmo_schema_field_t *field = &type->fields[0];
    ASSERT_STR_EQ("r", field->name);
    ASSERT_EQ(NMO_ANNOTATION_COLOR, field->annotations);
    
    teardown_test_context(ctx);
}

TEST(schema_macros, register_nested_struct) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register dependency first */
    nmo_result_t result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, TestVector, test_vector_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Register nested struct */
    result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, TestBox, test_box_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify nested type references */
    const nmo_schema_type_t *box_type = 
        nmo_schema_registry_find_by_name(ctx->registry, "TestBox");
    ASSERT_NE(NULL, box_type);
    ASSERT_EQ(2U, box_type->field_count);
    
    const nmo_schema_field_t *min_field = &box_type->fields[0];
    ASSERT_STR_EQ("min", min_field->name);
    ASSERT_NE(NULL, min_field->type);
    ASSERT_STR_EQ("TestVector", min_field->type->name);
    
    teardown_test_context(ctx);
}

TEST(schema_macros, register_versioned_struct) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register versioned struct */
    nmo_result_t result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, TestVersioned, test_versioned_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify version metadata */
    const nmo_schema_type_t *type = 
        nmo_schema_registry_find_by_name(ctx->registry, "TestVersioned");
    ASSERT_NE(NULL, type);
    ASSERT_EQ(4U, type->field_count);
    
    /* Check versioned field */
    const nmo_schema_field_t *scale_field = &type->fields[2];
    ASSERT_STR_EQ("scale", scale_field->name);
    ASSERT_EQ(5U, scale_field->since_version);
    ASSERT_EQ(0U, scale_field->deprecated_version);
    
    /* Check deprecated field */
    const nmo_schema_field_t *depr_field = &type->fields[3];
    ASSERT_STR_EQ("deprecated_val", depr_field->name);
    ASSERT_EQ(3U, depr_field->since_version);
    ASSERT_EQ(7U, depr_field->deprecated_version);
    
    teardown_test_context(ctx);
}

TEST(schema_macros, register_enum) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register enum */
    nmo_result_t result = NMO_REGISTER_ENUM(
        ctx->registry, ctx->arena, TestBlendMode);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify enum type */
    const nmo_schema_type_t *type = 
        nmo_schema_registry_find_by_name(ctx->registry, "TestBlendMode");
    ASSERT_NE(NULL, type);
    ASSERT_EQ(NMO_TYPE_ENUM, type->kind);
    ASSERT_EQ(4U, type->enum_value_count);
    
    /* Check enum values */
    const nmo_enum_value_t *value = &type->enum_values[0];
    ASSERT_STR_EQ("ZERO", value->name);
    ASSERT_EQ(0, value->value);
    
    value = &type->enum_values[2];
    ASSERT_STR_EQ("SRC_COLOR", value->name);
    ASSERT_EQ(2, value->value);
    
    teardown_test_context(ctx);
}

/* ============================================================================
 * Test Cases: Error Handling
 * ============================================================================ */

TEST(schema_macros, register_missing_dependency) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Try to register nested struct without dependency */
    nmo_result_t result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, TestBox, test_box_t);
    
    /* Should fail because TestVector not registered */
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_NE(NULL, result.error);
    
    teardown_test_context(ctx);
}

TEST(schema_macros, register_duplicate_type) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    /* Register once */
    nmo_result_t result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, TestVector, test_vector_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Try to register again */
    result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, TestVector, test_vector_t);
    
    /* Should fail (duplicate registration) */
    ASSERT_NE(NMO_OK, result.code);
    
    teardown_test_context(ctx);
}

/* ============================================================================
 * Test Cases: Field Offset Verification
 * ============================================================================ */

TEST(schema_macros, field_offsets_correct) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    nmo_result_t result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, TestVector, test_vector_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    const nmo_schema_type_t *type = 
        nmo_schema_registry_find_by_name(ctx->registry, "TestVector");
    ASSERT_NE(NULL, type);
    
    /* Verify offsets match actual struct layout */
    test_vector_t dummy;
    
    const nmo_schema_field_t *x_field = &type->fields[0];
    ASSERT_EQ((size_t)((char*)&dummy.x - (char*)&dummy), x_field->offset);
    
    const nmo_schema_field_t *y_field = &type->fields[1];
    ASSERT_EQ((size_t)((char*)&dummy.y - (char*)&dummy), y_field->offset);
    
    const nmo_schema_field_t *z_field = &type->fields[2];
    ASSERT_EQ((size_t)((char*)&dummy.z - (char*)&dummy), z_field->offset);
    
    teardown_test_context(ctx);
}

/* ============================================================================
 * Test Cases: Type Size and Alignment
 * ============================================================================ */

TEST(schema_macros, type_size_alignment) {
    test_context_t *ctx = setup_test_context();
    ASSERT_NE(NULL, ctx);
    
    nmo_result_t result = NMO_REGISTER_SIMPLE_SCHEMA(
        ctx->registry, ctx->arena, TestVector, test_vector_t);
    ASSERT_EQ(NMO_OK, result.code);
    
    const nmo_schema_type_t *type = 
        nmo_schema_registry_find_by_name(ctx->registry, "TestVector");
    ASSERT_NE(NULL, type);
    
    /* Verify size and alignment */
    ASSERT_EQ(sizeof(test_vector_t), type->size);
    ASSERT_EQ(_Alignof(test_vector_t), type->align);
    
    teardown_test_context(ctx);
}

/* ============================================================================
 * Test Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* Field declaration tests */
    REGISTER_TEST(schema_macros, field_descriptor_basic);
    REGISTER_TEST(schema_macros, field_descriptor_with_annotations);
    REGISTER_TEST(schema_macros, field_descriptor_versioned);
    REGISTER_TEST(schema_macros, field_count);
    
    /* Enum declaration tests */
    REGISTER_TEST(schema_macros, enum_descriptor_basic);
    REGISTER_TEST(schema_macros, enum_value_count);
    
    /* Registration tests */
    REGISTER_TEST(schema_macros, register_simple_struct);
    REGISTER_TEST(schema_macros, register_annotated_struct);
    REGISTER_TEST(schema_macros, register_nested_struct);
    REGISTER_TEST(schema_macros, register_versioned_struct);
    REGISTER_TEST(schema_macros, register_enum);
    
    /* Error handling tests */
    REGISTER_TEST(schema_macros, register_missing_dependency);
    REGISTER_TEST(schema_macros, register_duplicate_type);
    
    /* Verification tests */
    REGISTER_TEST(schema_macros, field_offsets_correct);
    REGISTER_TEST(schema_macros, type_size_alignment);
TEST_MAIN_END()


