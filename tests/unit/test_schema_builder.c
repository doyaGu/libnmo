/**
 * @file test_schema_builder.c
 * @brief Tests for fluent schema builder API
 */

#include "../../tests/test_framework.h"
#include "schema/nmo_schema_builder.h"
#include "schema/nmo_schema_registry.h"
#include "core/nmo_arena.h"
#include <string.h>
#include <stdalign.h>

/**
 * @brief Test scalar type builder
 */
TEST(schema_builder, scalar_types) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);
    
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Build u32 type */
    nmo_schema_builder_t builder = nmo_builder_scalar(arena, "u32", NMO_TYPE_U32, 4);
    ASSERT_NE(NULL, builder.type);
    ASSERT_STR_EQ("u32", builder.type->name);
    ASSERT_EQ(NMO_TYPE_U32, builder.type->kind);
    ASSERT_EQ(4, builder.type->size);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify lookup */
    const nmo_schema_type_t *found = nmo_schema_registry_find_by_name(registry, "u32");
    ASSERT_NE(NULL, found);
    ASSERT_STR_EQ("u32", found->name);
    
    nmo_arena_destroy(arena);
}

/**
 * @brief Test struct type builder with fields
 */
TEST(schema_builder, struct_with_fields) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);
    
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register scalar first */
    nmo_schema_builder_t f32_builder = nmo_builder_scalar(arena, "f32", NMO_TYPE_F32, 4);
    nmo_builder_build(&f32_builder, registry);
    
    const nmo_schema_type_t *f32_type = nmo_schema_registry_find_by_name(registry, "f32");
    ASSERT_NE(NULL, f32_type);
    
    /* Build Vec3 type */
    typedef struct { float x, y, z; } vec3_t;
    
    nmo_schema_builder_t vec3 = nmo_builder_struct(arena, "Vec3", sizeof(vec3_t), alignof(vec3_t));
    nmo_builder_add_field(&vec3, "x", f32_type, offsetof(vec3_t, x));
    nmo_builder_add_field(&vec3, "y", f32_type, offsetof(vec3_t, y));
    nmo_builder_add_field(&vec3, "z", f32_type, offsetof(vec3_t, z));
    
    nmo_result_t result = nmo_builder_build(&vec3, registry);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify */
    const nmo_schema_type_t *found = nmo_schema_registry_find_by_name(registry, "Vec3");
    ASSERT_NE(NULL, found);
    ASSERT_EQ(NMO_TYPE_STRUCT, found->kind);
    ASSERT_EQ(3, found->field_count);
    ASSERT_STR_EQ("x", found->fields[0].name);
    ASSERT_STR_EQ("y", found->fields[1].name);
    ASSERT_STR_EQ("z", found->fields[2].name);
    
    nmo_arena_destroy(arena);
}

/**
 * @brief Test enum type builder
 */
TEST(schema_builder, enum_type) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);
    
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Build enum */
    nmo_schema_builder_t builder = nmo_builder_enum(arena, "ColorMode", NMO_TYPE_U32);
    nmo_builder_add_enum_value(&builder, "RGB", 0);
    nmo_builder_add_enum_value(&builder, "HSV", 1);
    nmo_builder_add_enum_value(&builder, "RGBA", 2);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify */
    const nmo_schema_type_t *found = nmo_schema_registry_find_by_name(registry, "ColorMode");
    ASSERT_NE(NULL, found);
    ASSERT_EQ(NMO_TYPE_ENUM, found->kind);
    ASSERT_EQ(3, found->enum_value_count);
    ASSERT_STR_EQ("RGB", found->enum_values[0].name);
    ASSERT_EQ(0, found->enum_values[0].value);
    ASSERT_STR_EQ("HSV", found->enum_values[1].name);
    ASSERT_EQ(1, found->enum_values[1].value);
    
    nmo_arena_destroy(arena);
}

/**
 * @brief Test batch scalar registration
 */
TEST(schema_builder, batch_scalar_registration) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);
    
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register all scalars at once */
    nmo_result_t result = nmo_register_scalar_types(registry, arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify some types */
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "u8"));
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "u32"));
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "i64"));
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "f32"));
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "bool"));
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "string"));
    
    nmo_arena_destroy(arena);
}

/**
 * @brief Test batch math type registration
 */
TEST(schema_builder, batch_math_registration) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);
    
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Scalars first, then math */
    nmo_register_scalar_types(registry, arena);
    nmo_result_t result = nmo_register_math_types(registry, arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify math types */
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "Vector2"));
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "Vector3"));
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "Vector4"));
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "Quaternion"));
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "Matrix"));
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "Color"));
    
    nmo_arena_destroy(arena);
}

/**
 * @brief Test complete builtin registration
 */
TEST(schema_builder, builtin_types_complete) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);
    
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Register everything */
    nmo_result_t result = nmo_register_builtin_types(registry, arena);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify we have scalars, math, and virtools types */
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "u32"));
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "Vector3"));
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "GUID"));
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "ObjectID"));
    ASSERT_NE(NULL, nmo_schema_registry_find_by_name(registry, "FileVersion"));
    
    nmo_arena_destroy(arena);
}

/**
 * @brief Test field annotations
 */
TEST(schema_builder, field_annotations) {
    nmo_arena_t *arena = nmo_arena_create(NULL, 64 * 1024);
    ASSERT_NE(NULL, arena);
    
    nmo_schema_registry_t *registry = nmo_schema_registry_create(arena);
    ASSERT_NE(NULL, registry);
    
    /* Setup */
    nmo_register_scalar_types(registry, arena);
    const nmo_schema_type_t *f32_type = nmo_schema_registry_find_by_name(registry, "f32");
    
    typedef struct { float x, y, z; } pos_t;
    
    /* Build with annotations */
    nmo_schema_builder_t builder = nmo_builder_struct(arena, "Position", sizeof(pos_t), alignof(pos_t));
    nmo_builder_add_field_ex(&builder, "x", f32_type, offsetof(pos_t, x), NMO_ANNOTATION_POSITION);
    nmo_builder_add_field_ex(&builder, "y", f32_type, offsetof(pos_t, y), NMO_ANNOTATION_POSITION);
    nmo_builder_add_field_ex(&builder, "z", f32_type, offsetof(pos_t, z), NMO_ANNOTATION_POSITION);
    
    nmo_result_t result = nmo_builder_build(&builder, registry);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify annotations */
    const nmo_schema_type_t *found = nmo_schema_registry_find_by_name(registry, "Position");
    ASSERT_NE(NULL, found);
    ASSERT_EQ(NMO_ANNOTATION_POSITION, found->fields[0].annotations);
    ASSERT_EQ(NMO_ANNOTATION_POSITION, found->fields[1].annotations);
    ASSERT_EQ(NMO_ANNOTATION_POSITION, found->fields[2].annotations);
    
    nmo_arena_destroy(arena);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(schema_builder, scalar_types);
    REGISTER_TEST(schema_builder, struct_with_fields);
    REGISTER_TEST(schema_builder, enum_type);
    REGISTER_TEST(schema_builder, batch_scalar_registration);
    REGISTER_TEST(schema_builder, batch_math_registration);
    REGISTER_TEST(schema_builder, builtin_types_complete);
    REGISTER_TEST(schema_builder, field_annotations);
TEST_MAIN_END()
