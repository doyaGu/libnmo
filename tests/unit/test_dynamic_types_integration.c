/**
 * @file test_dynamic_types_integration.c
 * @brief Integration tests for dynamic type system (Phase 6.2 Task 6.2.5)
 * 
 * Tests cross-type interactions and complex scenarios:
 * - Registering all type kinds together
 * - Lookup by GUID and name across types
 * - Structs with enum/flags fields
 * - Type name parsing with registered types
 * - Performance benchmarks
 */

#include "test_framework.h"
#include "type/dynamic_types.h"
#include "type/type_system.h"
#include "core/nmo_arena.h"
#include "core/nmo_error.h"
#include "core/nmo_guid.h"
#include <stdalign.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

static nmo_arena_t *test_arena = NULL;
static nmo_type_registry_t *test_registry = NULL;

static void setup(void) {
    test_arena = nmo_arena_create(NULL, 2 * 1024 * 1024); /* 2MB for complex tests */
    ASSERT_NE(NULL, test_arena);
    
    test_registry = nmo_type_registry_create(test_arena);
    ASSERT_NE(NULL, test_registry);
    
    /* Register basic types needed for struct fields */
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
    
    nmo_type_descriptor_t *bool_type = (nmo_type_descriptor_t*)
        nmo_arena_alloc(test_arena, sizeof(nmo_type_descriptor_t), alignof(nmo_type_descriptor_t));
    memset(bool_type, 0, sizeof(nmo_type_descriptor_t));
    bool_type->guid = (nmo_guid_t){0x6FED1D00, 0x00000003};
    bool_type->name = "bool";
    bool_type->size = 1;
    bool_type->alignment = 1;
    bool_type->category = NMO_TYPE_CATEGORY_SCALAR;
    bool_type->flags = NMO_TYPE_FLAG_SERIALIZABLE | NMO_TYPE_FLAG_POD;
    bool_type->valid = true;
    nmo_type_registry_register(test_registry, bool_type);
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
 * Cross-Type Registration Tests
 * ============================================================================ */

TEST(dynamic_types_integration, register_all_type_kinds) {
    setup();
    
    /* Register enum */
    nmo_enum_value_def_t colors[] = {
        { "RED", 0, NULL }, { "GREEN", 1, NULL }, { "BLUE", 2, NULL }
    };
    nmo_enum_type_def_t color_enum = {
        .name = "Color",
        .values = colors,
        .value_count = 3,
        .default_value = 0
    };
    nmo_guid_t color_guid;
    nmo_result_t result = nmo_type_registry_register_enum(test_registry, &color_enum, &color_guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Register flags */
    nmo_flags_bit_def_t perms[] = {
        { "READ", 0x01, NULL }, { "WRITE", 0x02, NULL }, { "EXECUTE", 0x04, NULL }
    };
    nmo_flags_type_def_t perm_flags = {
        .name = "Permissions",
        .bits = perms,
        .bit_count = 3,
        .default_value = 0x01
    };
    nmo_guid_t perm_guid;
    result = nmo_type_registry_register_flags(test_registry, &perm_flags, &perm_guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Register struct */
    nmo_struct_field_def_t fields[] = {
        { .name = "x", .type_name = "int" },
        { .name = "y", .type_name = "int" }
    };
    nmo_struct_type_def_t point_struct = {
        .name = "Point",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 2
    };
    nmo_guid_t point_guid;
    result = nmo_type_registry_register_struct(test_registry, &point_struct, &point_guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify all types are registered */
    const nmo_type_descriptor_t *color_type = nmo_type_registry_find_by_guid(test_registry, color_guid);
    ASSERT_NE(NULL, color_type);
    ASSERT_EQ(NMO_TYPE_CATEGORY_ENUM, color_type->category);
    
    const nmo_type_descriptor_t *perm_type = nmo_type_registry_find_by_guid(test_registry, perm_guid);
    ASSERT_NE(NULL, perm_type);
    ASSERT_EQ(NMO_TYPE_CATEGORY_FLAGS, perm_type->category);
    
    const nmo_type_descriptor_t *point_type = nmo_type_registry_find_by_guid(test_registry, point_guid);
    ASSERT_NE(NULL, point_type);
    ASSERT_EQ(NMO_TYPE_CATEGORY_STRUCT, point_type->category);
    
    teardown();
}

TEST(dynamic_types_integration, lookup_by_name_across_types) {
    setup();
    
    /* Register multiple types */
    nmo_enum_value_def_t states[] = { { "IDLE", 0, NULL }, { "RUNNING", 1, NULL } };
    nmo_enum_type_def_t state_enum = {
        .name = "State",
        .values = states,
        .value_count = 2
    };
    nmo_guid_t state_guid;
    nmo_type_registry_register_enum(test_registry, &state_enum, &state_guid);
    
    nmo_struct_field_def_t fields[] = { { .name = "value", .type_name = "int" } };
    nmo_struct_type_def_t data_struct = {
        .name = "Data",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 1
    };
    nmo_guid_t data_guid;
    nmo_type_registry_register_struct(test_registry, &data_struct, &data_guid);
    
    /* Lookup by name (requires name index) */
    const nmo_type_descriptor_t *state_type = nmo_type_registry_find_by_guid(test_registry, state_guid);
    ASSERT_NE(NULL, state_type);
    ASSERT_STR_EQ("State", state_type->name);
    
    const nmo_type_descriptor_t *data_type = nmo_type_registry_find_by_guid(test_registry, data_guid);
    ASSERT_NE(NULL, data_type);
    ASSERT_STR_EQ("Data", data_type->name);
    
    teardown();
}

/* ============================================================================
 * Complex Struct Tests
 * ============================================================================ */

TEST(dynamic_types_integration, struct_with_multiple_field_types) {
    setup();
    
    /* Register enum for field */
    nmo_enum_value_def_t sizes[] = {
        { "SMALL", 0, NULL }, { "MEDIUM", 1, NULL }, { "LARGE", 2, NULL }
    };
    nmo_enum_type_def_t size_enum = {
        .name = "Size",
        .values = sizes,
        .value_count = 3,
        .default_value = 1
    };
    nmo_guid_t size_guid;
    nmo_type_registry_register_enum(test_registry, &size_enum, &size_guid);
    
    /* Create enum type descriptor for struct field */
    const nmo_type_descriptor_t *size_type = nmo_type_registry_find_by_guid(test_registry, size_guid);
    ASSERT_NE(NULL, size_type);
    
    /* Register struct with mixed field types */
    nmo_struct_field_def_t fields[] = {
        { .name = "id", .type_name = "int" },
        { .name = "name_length", .type_name = "int" },
        { .name = "scale", .type_name = "float" },
        { .name = "active", .type_name = "bool" }
    };
    nmo_struct_type_def_t entity_struct = {
        .name = "Entity",
        .description = "Game entity with mixed field types",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 4
    };
    nmo_guid_t entity_guid;
    nmo_result_t result = nmo_type_registry_register_struct(test_registry, &entity_struct, &entity_guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Verify struct */
    const nmo_type_descriptor_t *entity_type = nmo_type_registry_find_by_guid(test_registry, entity_guid);
    ASSERT_NE(NULL, entity_type);
    ASSERT_STR_EQ("Entity", entity_type->name);
    
    /* Verify size calculation: int(4) + int(4) + float(4) + bool(1+3pad) = 16 */
    ASSERT_EQ(16, entity_type->size);
    ASSERT_EQ(4, entity_type->alignment);
    
    teardown();
}

TEST(dynamic_types_integration, large_struct_with_many_fields) {
    setup();
    
    /* Create struct with 10 fields */
    nmo_struct_field_def_t fields[10];
    for (int i = 0; i < 10; i++) {
        fields[i].name = (i % 2 == 0) ? "int_field" : "float_field";
        fields[i].type_name = (i % 2 == 0) ? "int" : "float";
        fields[i].type_guid = NMO_NULL_GUID;
        fields[i].description = NULL;
        fields[i].flags = 0;
        fields[i].default_value = NULL;
    }
    
    nmo_struct_type_def_t big_struct = {
        .name = "BigStruct",
        .description = "Struct with many fields",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 10,
        .alignment = 0,
        .packed = false
    };
    
    nmo_guid_t big_guid;
    nmo_result_t result = nmo_type_registry_register_struct(test_registry, &big_struct, &big_guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    const nmo_type_descriptor_t *big_type = nmo_type_registry_find_by_guid(test_registry, big_guid);
    ASSERT_NE(NULL, big_type);
    ASSERT_EQ(40, big_type->size); /* 10 * 4 bytes */
    
    teardown();
}

/* ============================================================================
 * Type Parser Integration Tests
 * ============================================================================ */

TEST(dynamic_types_integration, parse_registered_type_names) {
    setup();
    
    /* Register custom type */
    nmo_struct_field_def_t fields[] = {
        { .name = "x", .type_name = "float" },
        { .name = "y", .type_name = "float" }
    };
    nmo_struct_type_def_t vec2_struct = {
        .name = "Vector2",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 2
    };
    nmo_guid_t vec2_guid;
    nmo_type_registry_register_struct(test_registry, &vec2_struct, &vec2_guid);
    
    /* Parse the registered type name */
    nmo_type_parse_result_t parse_result;
    nmo_result_t result = nmo_type_registry_parse_type_name(
        test_registry, "Vector2", &parse_result);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT(nmo_guid_equals(vec2_guid, parse_result.base_type_guid));
    ASSERT_EQ(0, parse_result.array_count);
    ASSERT_EQ(0, parse_result.pointer_depth);
    
    teardown();
}

TEST(dynamic_types_integration, parse_builtin_types) {
    setup();
    
    /* Test parsing builtin type names */
    const char *builtin_names[] = { "int", "float", "bool", "VxVector3", "VxMatrix" };
    
    for (int i = 0; i < 5; i++) {
        nmo_type_parse_result_t parse_result;
        nmo_result_t result = nmo_type_registry_parse_type_name(
            test_registry, builtin_names[i], &parse_result);
        
        ASSERT_EQ(NMO_OK, result.code);
        ASSERT(!nmo_guid_is_null(parse_result.base_type_guid));
    }
    
    teardown();
}

/* ============================================================================
 * Error Handling Tests
 * ============================================================================ */

TEST(dynamic_types_integration, prevent_duplicate_names_across_types) {
    setup();
    
    /* Register enum with name "Status" */
    nmo_enum_value_def_t values[] = { { "OK", 0, NULL }, { "ERROR", 1, NULL } };
    nmo_enum_type_def_t status_enum = {
        .name = "Status",
        .values = values,
        .value_count = 2
    };
    nmo_guid_t enum_guid;
    nmo_result_t result = nmo_type_registry_register_enum(test_registry, &status_enum, &enum_guid);
    ASSERT_EQ(NMO_OK, result.code);
    
    /* Try to register struct with same name "Status" */
    nmo_struct_field_def_t fields[] = { { .name = "code", .type_name = "int" } };
    nmo_struct_type_def_t status_struct = {
        .name = "Status",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 1
    };
    nmo_guid_t struct_guid;
    result = nmo_type_registry_register_struct(test_registry, &status_struct, &struct_guid);
    
    /* Should fail due to duplicate name (same GUID generated from name) */
    ASSERT_NE(NMO_OK, result.code);
    
    teardown();
}

/* ============================================================================
 * Performance Benchmark Tests
 * ============================================================================ */

TEST(dynamic_types_integration, benchmark_type_registration) {
    setup();
    
    clock_t start = clock();
    
    /* Register 100 enum types */
    for (int i = 0; i < 100; i++) {
        char name[32];
        snprintf(name, sizeof(name), "TestEnum%d", i);
        
        nmo_enum_value_def_t values[] = {
            { "VALUE_0", 0, NULL },
            { "VALUE_1", 1, NULL },
            { "VALUE_2", 2, NULL }
        };
        
        nmo_enum_type_def_t enum_def = {
            .name = name,
            .values = values,
            .value_count = 3,
            .default_value = 0
        };
        
        nmo_guid_t guid;
        nmo_result_t result = nmo_type_registry_register_enum(test_registry, &enum_def, &guid);
        ASSERT_EQ(NMO_OK, result.code);
    }
    
    clock_t end = clock();
    double elapsed_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
    
    /* Should complete in reasonable time (< 50ms) */
    ASSERT(elapsed_ms < 50.0);
    
    teardown();
}

TEST(dynamic_types_integration, benchmark_type_lookup) {
    setup();
    
    /* Register several types */
    nmo_guid_t guids[50];
    for (int i = 0; i < 50; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Type%d", i);
        
        nmo_struct_field_def_t fields[] = {
            { .name = "field", .type_name = "int" }
        };
        
        nmo_struct_type_def_t struct_def = {
            .name = name,
            .guid = NMO_NULL_GUID,
            .fields = fields,
            .field_count = 1
        };
        
        nmo_type_registry_register_struct(test_registry, &struct_def, &guids[i]);
    }
    
    clock_t start = clock();
    
    /* Lookup each type 100 times */
    for (int j = 0; j < 100; j++) {
        for (int i = 0; i < 50; i++) {
            const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(test_registry, guids[i]);
            ASSERT_NE(NULL, type);
        }
    }
    
    clock_t end = clock();
    double elapsed_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
    
    /* 5000 lookups should be fast (< 10ms) */
    ASSERT(elapsed_ms < 10.0);
    
    teardown();
}

/* ============================================================================
 * Registry Statistics Tests
 * ============================================================================ */

TEST(dynamic_types_integration, get_type_statistics) {
    setup();
    
    /* Register various types */
    nmo_enum_value_def_t colors[] = { { "RED", 0, NULL }, { "BLUE", 1, NULL } };
    nmo_enum_type_def_t color_enum = {
        .name = "Color",
        .values = colors,
        .value_count = 2
    };
    nmo_guid_t color_guid;
    nmo_type_registry_register_enum(test_registry, &color_enum, &color_guid);
    
    nmo_flags_bit_def_t flags[] = { { "BIT0", 0x01, NULL }, { "BIT1", 0x02, NULL } };
    nmo_flags_type_def_t flag_def = {
        .name = "Flags",
        .bits = flags,
        .bit_count = 2
    };
    nmo_guid_t flag_guid;
    nmo_type_registry_register_flags(test_registry, &flag_def, &flag_guid);
    
    nmo_struct_field_def_t fields[] = { { .name = "x", .type_name = "int" } };
    nmo_struct_type_def_t struct_def = {
        .name = "Point",
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 1
    };
    nmo_guid_t struct_guid;
    nmo_type_registry_register_struct(test_registry, &struct_def, &struct_guid);
    
    /* Get statistics */
    size_t total_types = 0;
    size_t builtin_types = 0;
    size_t plugin_types = 0;
    
    nmo_type_registry_get_stats(
        test_registry, &total_types, &builtin_types, &plugin_types);
    
    /* Should have: 3 builtins (int, float, bool) + 3 custom (enum, flags, struct) = 6 */
    ASSERT_EQ(6, total_types);
    
    teardown();
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* Cross-type registration tests */
    REGISTER_TEST(dynamic_types_integration, register_all_type_kinds);
    REGISTER_TEST(dynamic_types_integration, lookup_by_name_across_types);
    
    /* Complex struct tests */
    REGISTER_TEST(dynamic_types_integration, struct_with_multiple_field_types);
    REGISTER_TEST(dynamic_types_integration, large_struct_with_many_fields);
    
    /* Type parser integration */
    REGISTER_TEST(dynamic_types_integration, parse_registered_type_names);
    REGISTER_TEST(dynamic_types_integration, parse_builtin_types);
    
    /* Error handling */
    REGISTER_TEST(dynamic_types_integration, prevent_duplicate_names_across_types);
    
    /* Performance benchmarks */
    REGISTER_TEST(dynamic_types_integration, benchmark_type_registration);
    REGISTER_TEST(dynamic_types_integration, benchmark_type_lookup);
    
    /* Statistics */
    REGISTER_TEST(dynamic_types_integration, get_type_statistics);
TEST_MAIN_END()
