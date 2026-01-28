/**
 * @file test_advanced_struct.c
 * @brief Tests for advanced struct features: incremental building, arrays, nested structs
 */

#include "test_framework.h"
#include "type/dynamic_types.h"
#include "type/type_system.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include <stdalign.h>
#include <string.h>

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

static nmo_arena_t *test_arena = NULL;
static nmo_type_registry_t *test_registry = NULL;

static void setup(void) {
    test_arena = nmo_arena_create(NULL, 2 * 1024 * 1024); /* 2MB */
    ASSERT_NE(NULL, test_arena);
    
    test_registry = nmo_type_registry_create(test_arena);
    ASSERT_NE(NULL, test_registry);
    
    /* Register basic types */
    nmo_type_descriptor_t *int_type = (nmo_type_descriptor_t*)
        nmo_arena_alloc(test_arena, sizeof(nmo_type_descriptor_t), alignof(nmo_type_descriptor_t));
    memset(int_type, 0, sizeof(nmo_type_descriptor_t));
    int_type->guid = (nmo_guid_t){0x6FED1D00, 0x00000001};
    int_type->name = "int";
    int_type->size = 4;
    int_type->alignment = 4;
    int_type->category = NMO_TYPE_CATEGORY_SCALAR;
    int_type->flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_POD;
    int_type->valid = true;
    nmo_type_registry_register(test_registry, int_type);
    
    nmo_type_descriptor_t *float_type = (nmo_type_descriptor_t*)
        nmo_arena_alloc(test_arena, sizeof(nmo_type_descriptor_t), alignof(nmo_type_descriptor_t));
    memset(float_type, 0, sizeof(nmo_type_descriptor_t));
    float_type->guid = (nmo_guid_t){0x6FED1D00, 0x00000002};
    float_type->name = "float";
    float_type->size = 4;
    float_type->alignment = 4;
    float_type->category = NMO_TYPE_CATEGORY_SCALAR;
    float_type->flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_POD;
    float_type->valid = true;
    nmo_type_registry_register(test_registry, float_type);
    
    nmo_type_descriptor_t *byte_type = (nmo_type_descriptor_t*)
        nmo_arena_alloc(test_arena, sizeof(nmo_type_descriptor_t), alignof(nmo_type_descriptor_t));
    memset(byte_type, 0, sizeof(nmo_type_descriptor_t));
    byte_type->guid = (nmo_guid_t){0x6FED1D00, 0x00000020};
    byte_type->name = "CKBYTE";
    byte_type->size = 1;
    byte_type->alignment = 1;
    byte_type->category = NMO_TYPE_CATEGORY_SCALAR;
    byte_type->flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_POD;
    byte_type->valid = true;
    nmo_type_registry_register(test_registry, byte_type);
}

static void teardown(void) {
    if (test_registry) {
        nmo_type_registry_destroy(test_registry);
        test_registry = NULL;
    }
    if (test_arena) {
        nmo_arena_destroy(test_arena);
        test_arena = NULL;
    }
}

/* ============================================================================
 * Incremental Struct Building Tests
 * ============================================================================ */

TEST(advanced_struct, incremental_build_basic) {
    setup();
    
    /* Begin struct */
    nmo_type_id_t type_id;
    nmo_result_t result = nmo_type_registry_begin_struct(
        test_registry, "IncrementalStruct", NMO_NULL_GUID, &type_id);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Add fields */
    result = nmo_type_registry_add_field(test_registry, type_id, "x", "int");
    ASSERT_EQ(NMO_OK, result.code);
    
    result = nmo_type_registry_add_field(test_registry, type_id, "y", "int");
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Finalize */
    result = nmo_type_registry_finalize_struct(test_registry, type_id);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify */
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_get_by_id(test_registry, type_id);
    ASSERT_NE(NULL, type_desc);
    ASSERT_STR_EQ("IncrementalStruct", type_desc->name);
    ASSERT_EQ(8, type_desc->size);  /* 2 ints */
    ASSERT(type_desc->valid);
    
    teardown();
}

TEST(advanced_struct, incremental_build_many_fields) {
    setup();
    
    nmo_type_id_t type_id;
    nmo_type_registry_begin_struct(test_registry, "ManyFields", NMO_NULL_GUID, &type_id);
    
    /* Add 15 fields to test array growth */
    for (int i = 0; i < 15; i++) {
        char field_name[32];
        snprintf(field_name, sizeof(field_name), "field%d", i);
        nmo_result_t result = nmo_type_registry_add_field(
            test_registry, type_id, field_name, "int");
        ASSERT_EQ(NMO_OK, result.code);
    }
    
    nmo_result_t result = nmo_type_registry_finalize_struct(test_registry, type_id);
    ASSERT_EQ(NMO_OK, result.code);
    
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_get_by_id(test_registry, type_id);
    ASSERT_EQ(60, type_desc->size);  /* 15 * 4 */
    
    teardown();
}

TEST(advanced_struct, incremental_build_null_params) {
    setup();
    
    nmo_type_id_t type_id;
    
    /* NULL registry */
    nmo_result_t result = nmo_type_registry_begin_struct(
        NULL, "Test", NMO_NULL_GUID, &type_id);
    ASSERT_NE(NMO_OK, result.code);
    
    /* Empty name */
    result = nmo_type_registry_begin_struct(
        test_registry, "", NMO_NULL_GUID, &type_id);
    ASSERT_NE(NMO_OK, result.code);
    
    teardown();
}

TEST(advanced_struct, incremental_finalize_without_fields) {
    setup();
    
    nmo_type_id_t type_id;
    nmo_type_registry_begin_struct(test_registry, "Empty", NMO_NULL_GUID, &type_id);
    
    /* Try to finalize without adding fields */
    nmo_result_t result = nmo_type_registry_finalize_struct(test_registry, type_id);
    ASSERT_NE(NMO_OK, result.code);
    
    teardown();
}

TEST(advanced_struct, incremental_add_after_finalize) {
    setup();
    
    nmo_type_id_t type_id;
    nmo_type_registry_begin_struct(test_registry, "Test", NMO_NULL_GUID, &type_id);
    nmo_type_registry_add_field(test_registry, type_id, "x", "int");
    nmo_type_registry_finalize_struct(test_registry, type_id);
    
    /* Try to add field after finalization */
    nmo_result_t result = nmo_type_registry_add_field(test_registry, type_id, "y", "int");
    ASSERT_NE(NMO_OK, result.code);
    
    teardown();
}

/* ============================================================================
 * Array Field Tests
 * ============================================================================ */

TEST(advanced_struct, array_field_basic) {
    setup();
    
    /* Register struct with array field */
    nmo_struct_field_def_t fields[] = {
        { .name = "id", .type_name = "int" },
        { .name = "values", .type_name = "float[10]" }  /* Array field */
    };
    
    nmo_struct_type_def_t struct_def = {
        .name = "ArrayStruct",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 2
    };
    
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_struct(
        test_registry, &struct_def, &guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify size: int(4) + float[10](40) = 44 */
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(test_registry, guid);
    ASSERT_NE(NULL, type_desc);
    ASSERT_EQ(44, type_desc->size);
    
    teardown();
}

TEST(advanced_struct, array_field_multidimensional) {
    setup();
    
    /* Matrix-like structure */
    nmo_struct_field_def_t fields[] = {
        { .name = "matrix", .type_name = "float[16]" }  /* 4x4 matrix as flat array */
    };
    
    nmo_struct_type_def_t struct_def = {
        .name = "Matrix4x4",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 1
    };
    
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_struct(
        test_registry, &struct_def, &guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(test_registry, guid);
    ASSERT_EQ(64, type_desc->size);  /* 16 * 4 */
    
    teardown();
}

TEST(advanced_struct, array_field_mixed_types) {
    setup();
    
    nmo_struct_field_def_t fields[] = {
        { .name = "count", .type_name = "int" },
        { .name = "positions", .type_name = "float[3]" },
        { .name = "colors", .type_name = "int[4]" }
    };
    
    nmo_struct_type_def_t struct_def = {
        .name = "Vertex",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 3
    };
    
    nmo_guid_t guid;
    nmo_result_t result = nmo_type_registry_register_struct(
        test_registry, &struct_def, &guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Size: int(4) + float[3](12) + int[4](16) = 32 */
    const nmo_type_descriptor_t *type_desc = nmo_type_registry_find_by_guid(test_registry, guid);
    ASSERT_EQ(32, type_desc->size);
    
    teardown();
}

/* ============================================================================
 * Nested Struct Tests
 * ============================================================================ */

TEST(advanced_struct, nested_struct_basic) {
    setup();
    
    /* Register inner struct first */
    nmo_struct_field_def_t vec2_fields[] = {
        { .name = "x", .type_name = "float" },
        { .name = "y", .type_name = "float" }
    };
    
    nmo_struct_type_def_t vec2_def = {
        .name = "Vector2",
        .guid = NMO_NULL_GUID,
        .fields = vec2_fields,
        .field_count = 2
    };
    
    nmo_guid_t vec2_guid;
    nmo_result_t result = nmo_type_registry_register_struct(
        test_registry, &vec2_def, &vec2_guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Register outer struct with nested Vector2 */
    nmo_struct_field_def_t rect_fields[] = {
        { .name = "position", .type_name = "Vector2" },  /* Nested struct */
        { .name = "size", .type_name = "Vector2" }
    };
    
    nmo_struct_type_def_t rect_def = {
        .name = "Rectangle",
        .guid = NMO_NULL_GUID,
        .fields = rect_fields,
        .field_count = 2
    };
    
    nmo_guid_t rect_guid;
    result = nmo_type_registry_register_struct(
        test_registry, &rect_def, &rect_guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify size: Vector2(8) + Vector2(8) = 16 */
    const nmo_type_descriptor_t *rect_type = nmo_type_registry_find_by_guid(test_registry, rect_guid);
    ASSERT_NE(NULL, rect_type);
    ASSERT_EQ(16, rect_type->size);
    
    teardown();
}

TEST(advanced_struct, nested_struct_deep) {
    setup();
    
    /* Level 1: Point */
    nmo_struct_field_def_t point_fields[] = {
        { .name = "x", .type_name = "float" },
        { .name = "y", .type_name = "float" }
    };
    nmo_struct_type_def_t point_def = {
        .name = "Point", .guid = NMO_NULL_GUID,
        .fields = point_fields, .field_count = 2
    };
    nmo_guid_t point_guid;
    nmo_type_registry_register_struct(test_registry, &point_def, &point_guid);
    
    /* Level 2: Line (contains 2 Points) */
    nmo_struct_field_def_t line_fields[] = {
        { .name = "start", .type_name = "Point" },
        { .name = "end", .type_name = "Point" }
    };
    nmo_struct_type_def_t line_def = {
        .name = "Line", .guid = NMO_NULL_GUID,
        .fields = line_fields, .field_count = 2
    };
    nmo_guid_t line_guid;
    nmo_type_registry_register_struct(test_registry, &line_def, &line_guid);
    
    /* Level 3: Polygon (contains multiple Lines) */
    nmo_struct_field_def_t poly_fields[] = {
        { .name = "edge1", .type_name = "Line" },
        { .name = "edge2", .type_name = "Line" },
        { .name = "edge3", .type_name = "Line" }
    };
    nmo_struct_type_def_t poly_def = {
        .name = "Triangle", .guid = NMO_NULL_GUID,
        .fields = poly_fields, .field_count = 3
    };
    nmo_guid_t poly_guid;
    nmo_result_t result = nmo_type_registry_register_struct(
        test_registry, &poly_def, &poly_guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Size: Point(8) -> Line(16) -> Triangle(48) */
    const nmo_type_descriptor_t *poly_type = nmo_type_registry_find_by_guid(test_registry, poly_guid);
    ASSERT_EQ(48, poly_type->size);
    
    teardown();
}

TEST(advanced_struct, nested_struct_with_arrays) {
    setup();
    
    /* Inner struct */
    nmo_struct_field_def_t vec3_fields[] = {
        { .name = "x", .type_name = "float" },
        { .name = "y", .type_name = "float" },
        { .name = "z", .type_name = "float" }
    };
    nmo_struct_type_def_t vec3_def = {
        .name = "Vector3", .guid = NMO_NULL_GUID,
        .fields = vec3_fields, .field_count = 3
    };
    nmo_guid_t vec3_guid;
    nmo_type_registry_register_struct(test_registry, &vec3_def, &vec3_guid);
    
    /* Outer struct with array of nested structs */
    nmo_struct_field_def_t path_fields[] = {
        { .name = "waypoints", .type_name = "Vector3[5]" }  /* Array of structs */
    };
    nmo_struct_type_def_t path_def = {
        .name = "Path", .guid = NMO_NULL_GUID,
        .fields = path_fields, .field_count = 1
    };
    nmo_guid_t path_guid;
    nmo_result_t result = nmo_type_registry_register_struct(
        test_registry, &path_def, &path_guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Size: Vector3[5] = 12 * 5 = 60 */
    const nmo_type_descriptor_t *path_type = nmo_type_registry_find_by_guid(test_registry, path_guid);
    ASSERT_EQ(60, path_type->size);
    
    teardown();
}

TEST(advanced_struct, nested_struct_incomplete_error) {
    setup();
    
    /* Begin an incomplete struct */
    nmo_type_id_t incomplete_id;
    nmo_type_registry_begin_struct(test_registry, "Incomplete", NMO_NULL_GUID, &incomplete_id);
    nmo_type_registry_add_field(test_registry, incomplete_id, "x", "int");
    /* Don't finalize */
    
    /* Try to use incomplete struct as field */
    nmo_struct_field_def_t outer_fields[] = {
        { .name = "inner", .type_name = "Incomplete" }
    };
    nmo_struct_type_def_t outer_def = {
        .name = "Outer", .guid = NMO_NULL_GUID,
        .fields = outer_fields, .field_count = 1
    };
    nmo_guid_t outer_guid;
    nmo_result_t result = nmo_type_registry_register_struct(
        test_registry, &outer_def, &outer_guid);
    
    /* Should fail because inner struct is incomplete */
    ASSERT_NE(NMO_OK, result.code);
    
    teardown();
}

/* ============================================================================
 * Complex Combination Tests
 * ============================================================================ */

TEST(advanced_struct, complex_game_entity) {
    setup();
    
    /* Vector3 */
    nmo_struct_field_def_t vec3_fields[] = {
        { .name = "x", .type_name = "float" },
        { .name = "y", .type_name = "float" },
        { .name = "z", .type_name = "float" }
    };
    nmo_struct_type_def_t vec3_def = {
        .name = "Vector3", .guid = NMO_NULL_GUID,
        .fields = vec3_fields, .field_count = 3
    };
    nmo_guid_t vec3_guid;
    nmo_type_registry_register_struct(test_registry, &vec3_def, &vec3_guid);
    
    /* Transform (nested struct) */
    nmo_struct_field_def_t transform_fields[] = {
        { .name = "position", .type_name = "Vector3" },
        { .name = "rotation", .type_name = "Vector3" },
        { .name = "scale", .type_name = "Vector3" }
    };
    nmo_struct_type_def_t transform_def = {
        .name = "Transform", .guid = NMO_NULL_GUID,
        .fields = transform_fields, .field_count = 3
    };
    nmo_guid_t transform_guid;
    nmo_type_registry_register_struct(test_registry, &transform_def, &transform_guid);
    
    /* Entity (nested + arrays) */
    nmo_struct_field_def_t entity_fields[] = {
        { .name = "id", .type_name = "int" },
        { .name = "transform", .type_name = "Transform" },
        { .name = "velocities", .type_name = "float[3]" },
        { .name = "health", .type_name = "float" }
    };
    nmo_struct_type_def_t entity_def = {
        .name = "GameEntity", .guid = NMO_NULL_GUID,
        .fields = entity_fields, .field_count = 4
    };
    nmo_guid_t entity_guid;
    nmo_result_t result = nmo_type_registry_register_struct(
        test_registry, &entity_def, &entity_guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Size: int(4) + Transform(36) + float[3](12) + float(4) = 56 */
    const nmo_type_descriptor_t *entity_type = nmo_type_registry_find_by_guid(test_registry, entity_guid);
    ASSERT_EQ(56, entity_type->size);
    
    teardown();
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* Incremental building */
    REGISTER_TEST(advanced_struct, incremental_build_basic);
    REGISTER_TEST(advanced_struct, incremental_build_many_fields);
    REGISTER_TEST(advanced_struct, incremental_build_null_params);
    REGISTER_TEST(advanced_struct, incremental_finalize_without_fields);
    REGISTER_TEST(advanced_struct, incremental_add_after_finalize);
    
    /* Array fields */
    REGISTER_TEST(advanced_struct, array_field_basic);
    REGISTER_TEST(advanced_struct, array_field_multidimensional);
    REGISTER_TEST(advanced_struct, array_field_mixed_types);
    
    /* Nested structs */
    REGISTER_TEST(advanced_struct, nested_struct_basic);
    REGISTER_TEST(advanced_struct, nested_struct_deep);
    REGISTER_TEST(advanced_struct, nested_struct_with_arrays);
    REGISTER_TEST(advanced_struct, nested_struct_incomplete_error);
    
    /* Complex combinations */
    REGISTER_TEST(advanced_struct, complex_game_entity);
TEST_MAIN_END()
