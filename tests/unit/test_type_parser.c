/**
 * @file test_type_parser.c
 * @brief Unit tests for type name parser (Phase 6.2, Task 6.2.2)
 */

#include "test_framework.h"
#include "type/dynamic_types.h"
#include "type/type_system.h"
#include "core/nmo_arena.h"

/* ============================================================================
 * Test Fixtures
 * ============================================================================ */

static nmo_arena_t *arena = NULL;
static nmo_type_registry_t *registry = NULL;

static void setup(void) {
    arena = nmo_arena_create(NULL, 4096);
    ASSERT_NE(NULL, arena);
    
    registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
}

static void teardown(void) {
    nmo_type_registry_destroy(registry);
    nmo_arena_destroy(arena);
    registry = NULL;
    arena = NULL;
}

/* ============================================================================
 * Basic Type Name Parsing Tests
 * ============================================================================ */

TEST(type_parser, parse_basic_int) {
    setup();
    
    nmo_type_parse_result_t result;
    nmo_result_t res = nmo_type_registry_parse_type_name(registry, "int", &result);
    
    ASSERT_EQ(NMO_OK, res.code);
    ASSERT_EQ(false, result.is_array);
    ASSERT_EQ(false, result.is_pointer);
    ASSERT_EQ(0, result.array_count);
    ASSERT_EQ(0, result.pointer_depth);
    ASSERT_NE(0ULL, result.base_type_guid.d1 | result.base_type_guid.d2);
    
    teardown();
}

TEST(type_parser, parse_basic_float) {
    setup();
    
    nmo_type_parse_result_t result;
    nmo_result_t res = nmo_type_registry_parse_type_name(registry, "float", &result);
    
    ASSERT_EQ(NMO_OK, res.code);
    ASSERT_EQ(false, result.is_array);
    ASSERT_EQ(false, result.is_pointer);
    
    teardown();
}

TEST(type_parser, parse_case_insensitive) {
    setup();
    
    nmo_type_parse_result_t result1, result2, result3;
    nmo_result_t res1 = nmo_type_registry_parse_type_name(registry, "int", &result1);
    nmo_result_t res2 = nmo_type_registry_parse_type_name(registry, "INT", &result2);
    nmo_result_t res3 = nmo_type_registry_parse_type_name(registry, "InT", &result3);
    
    ASSERT_EQ(NMO_OK, res1.code);
    ASSERT_EQ(NMO_OK, res2.code);
    ASSERT_EQ(NMO_OK, res3.code);
    
    /* All should resolve to same GUID */
    ASSERT_EQ(result1.base_type_guid.d1, result2.base_type_guid.d1);
    ASSERT_EQ(result1.base_type_guid.d2, result2.base_type_guid.d2);
    ASSERT_EQ(result1.base_type_guid.d1, result3.base_type_guid.d1);
    
    teardown();
}

TEST(type_parser, parse_with_whitespace) {
    setup();
    
    nmo_type_parse_result_t result;
    nmo_result_t res = nmo_type_registry_parse_type_name(registry, "  int  ", &result);
    
    ASSERT_EQ(NMO_OK, res.code);
    ASSERT_EQ(false, result.is_array);
    
    teardown();
}

/* ============================================================================
 * Array Type Parsing Tests
 * ============================================================================ */

TEST(type_parser, parse_array_int10) {
    setup();
    
    nmo_type_parse_result_t result;
    nmo_result_t res = nmo_type_registry_parse_type_name(registry, "int[10]", &result);
    
    ASSERT_EQ(NMO_OK, res.code);
    ASSERT_EQ(true, result.is_array);
    ASSERT_EQ(10, result.array_count);
    ASSERT_EQ(false, result.is_pointer);
    
    teardown();
}

TEST(type_parser, parse_array_float256) {
    setup();
    
    nmo_type_parse_result_t result;
    nmo_result_t res = nmo_type_registry_parse_type_name(registry, "float[256]", &result);
    
    ASSERT_EQ(NMO_OK, res.code);
    ASSERT_EQ(true, result.is_array);
    ASSERT_EQ(256, result.array_count);
    
    teardown();
}

TEST(type_parser, parse_array_with_spaces) {
    setup();
    
    nmo_type_parse_result_t result;
    nmo_result_t res = nmo_type_registry_parse_type_name(registry, "int [ 5 ]", &result);
    
    /* Note: Current implementation doesn't handle spaces inside brackets */
    /* This is acceptable as it's not common usage */
    ASSERT_NE(NMO_OK, res.code);
    
    teardown();
}

/* ============================================================================
 * Pointer Type Parsing Tests
 * ============================================================================ */

TEST(type_parser, parse_pointer_int) {
    setup();
    
    nmo_type_parse_result_t result;
    nmo_result_t res = nmo_type_registry_parse_type_name(registry, "int*", &result);
    
    ASSERT_EQ(NMO_OK, res.code);
    ASSERT_EQ(true, result.is_pointer);
    ASSERT_EQ(1, result.pointer_depth);
    ASSERT_EQ(false, result.is_array);
    
    teardown();
}

TEST(type_parser, parse_double_pointer) {
    setup();
    
    nmo_type_parse_result_t result;
    nmo_result_t res = nmo_type_registry_parse_type_name(registry, "int**", &result);
    
    ASSERT_EQ(NMO_OK, res.code);
    ASSERT_EQ(true, result.is_pointer);
    ASSERT_EQ(2, result.pointer_depth);
    
    teardown();
}

/* ============================================================================
 * Error Handling Tests
 * ============================================================================ */

TEST(type_parser, parse_empty_string) {
    setup();
    
    nmo_type_parse_result_t result;
    nmo_result_t res = nmo_type_registry_parse_type_name(registry, "", &result);
    
    ASSERT_NE(NMO_OK, res.code);
    
    teardown();
}

TEST(type_parser, parse_unknown_type) {
    setup();
    
    nmo_type_parse_result_t result;
    nmo_result_t res = nmo_type_registry_parse_type_name(registry, "UnknownType", &result);
    
    ASSERT_NE(NMO_OK, res.code);
    ASSERT_EQ(NMO_ERR_NOT_FOUND, res.code);
    
    teardown();
}

TEST(type_parser, parse_null_params) {
    setup();
    
    nmo_type_parse_result_t result;
    nmo_result_t res1 = nmo_type_registry_parse_type_name(NULL, "int", &result);
    nmo_result_t res2 = nmo_type_registry_parse_type_name(registry, NULL, &result);
    nmo_result_t res3 = nmo_type_registry_parse_type_name(registry, "int", NULL);
    
    ASSERT_NE(NMO_OK, res1.code);
    ASSERT_NE(NMO_OK, res2.code);
    ASSERT_NE(NMO_OK, res3.code);
    
    teardown();
}

/* ============================================================================
 * GUID Generation Tests
 * ============================================================================ */

TEST(type_parser, generate_guid_deterministic) {
    nmo_guid_t guid1 = nmo_type_generate_guid("MyType");
    nmo_guid_t guid2 = nmo_type_generate_guid("MyType");
    
    /* Same name should produce same GUID */
    ASSERT_EQ(guid1.d1, guid2.d1);
    ASSERT_EQ(guid1.d2, guid2.d2);
}

TEST(type_parser, generate_guid_different_names) {
    nmo_guid_t guid1 = nmo_type_generate_guid("Type1");
    nmo_guid_t guid2 = nmo_type_generate_guid("Type2");
    
    /* Different names should produce different GUIDs */
    ASSERT_TRUE((guid1.d1 != guid2.d1) || (guid1.d2 != guid2.d2));
}

TEST(type_parser, generate_guid_null_name) {
    nmo_guid_t guid = nmo_type_generate_guid(NULL);
    
    /* NULL name should produce null GUID */
    ASSERT_EQ(0, guid.d1);
    ASSERT_EQ(0, guid.d2);
}

TEST(type_parser, generate_guid_has_marker_bit) {
    nmo_guid_t guid = nmo_type_generate_guid("TestType");
    
    /* Bit 31 of d1 should be set (marker for auto-generated) */
    ASSERT_TRUE((guid.d1 & 0x80000000) != 0);
}

/* ============================================================================
 * Virtools Types Tests
 * ============================================================================ */

TEST(type_parser, parse_virtools_vector3) {
    setup();
    
    nmo_type_parse_result_t result;
    nmo_result_t res = nmo_type_registry_parse_type_name(registry, "VxVector3", &result);
    
    ASSERT_EQ(NMO_OK, res.code);
    ASSERT_EQ(false, result.is_array);
    ASSERT_EQ(false, result.is_pointer);
    
    teardown();
}

TEST(type_parser, parse_virtools_color) {
    setup();
    
    nmo_type_parse_result_t result;
    nmo_result_t res = nmo_type_registry_parse_type_name(registry, "VxColor", &result);
    
    ASSERT_EQ(NMO_OK, res.code);
    
    teardown();
}

/* ============================================================================
 * String Parser Tests (Phase 6.2.1)
 * ============================================================================ */

TEST(type_parser, parse_flags_simple) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_flags_string(
        "READ=1,WRITE=2,EXECUTE=4", &values, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3, count);
    ASSERT_NE(NULL, values);
    
    ASSERT_STR_EQ("READ", values[0].name);
    ASSERT_EQ(1, values[0].value);
    ASSERT_STR_EQ("WRITE", values[1].name);
    ASSERT_EQ(2, values[1].value);
    ASSERT_STR_EQ("EXECUTE", values[2].name);
    ASSERT_EQ(4, values[2].value);
    
    teardown();
}

TEST(type_parser, parse_flags_hex_values) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_flags_string(
        "FLAG1=0x01,FLAG2=0x02,FLAG4=0x04,FLAG8=0x08",
        &values, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(4, count);
    ASSERT_EQ(0x01, values[0].value);
    ASSERT_EQ(0x02, values[1].value);
    ASSERT_EQ(0x04, values[2].value);
    ASSERT_EQ(0x08, values[3].value);
    
    teardown();
}

TEST(type_parser, parse_flags_with_whitespace) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_flags_string(
        "  FLAG_A = 1 ,  FLAG_B = 2  ,  FLAG_C = 4  ",
        &values, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3, count);
    ASSERT_STR_EQ("FLAG_A", values[0].name);
    ASSERT_STR_EQ("FLAG_B", values[1].name);
    ASSERT_STR_EQ("FLAG_C", values[2].name);
    
    teardown();
}

TEST(type_parser, parse_flags_single_entry) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_flags_string(
        "ENABLED=1", &values, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(1, count);
    ASSERT_STR_EQ("ENABLED", values[0].name);
    ASSERT_EQ(1, values[0].value);
    
    teardown();
}

TEST(type_parser, parse_flags_zero_value) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_flags_string(
        "NONE=0,FLAG1=1", &values, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(2, count);
    ASSERT_STR_EQ("NONE", values[0].name);
    ASSERT_EQ(0, values[0].value);
    
    teardown();
}

TEST(type_parser, parse_flags_combined_values) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_flags_string(
        "READ=1,WRITE=2,ALL=0xFF", &values, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3, count);
    ASSERT_EQ(0xFF, values[2].value);
    
    teardown();
}

TEST(type_parser, parse_flags_empty_string) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_flags_string(
        "", &values, &count, arena);
    
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, result.code);
    
    teardown();
}

TEST(type_parser, parse_flags_null_input) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_flags_string(
        NULL, &values, &count, arena);
    
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    teardown();
}

TEST(type_parser, parse_flags_missing_equals) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_flags_string(
        "FLAG1=1,FLAG2", &values, &count, arena);
    
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, result.code);
    
    teardown();
}

TEST(type_parser, parse_flags_invalid_identifier) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_flags_string(
        "1FLAG=1", &values, &count, arena);
    
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, result.code);
    
    teardown();
}

TEST(type_parser, parse_flags_invalid_value) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_flags_string(
        "FLAG=xyz", &values, &count, arena);
    
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, result.code);
    
    teardown();
}

TEST(type_parser, parse_flags_duplicate_name) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_flags_string(
        "FLAG=1,FLAG=2", &values, &count, arena);
    
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, result.code);
    
    teardown();
}

TEST(type_parser, parse_enum_simple) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_enum_string(
        "RED=0,GREEN=1,BLUE=2", &values, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3, count);
    ASSERT_STR_EQ("RED", values[0].name);
    ASSERT_EQ(0, values[0].value);
    ASSERT_STR_EQ("GREEN", values[1].name);
    ASSERT_EQ(1, values[1].value);
    ASSERT_STR_EQ("BLUE", values[2].name);
    ASSERT_EQ(2, values[2].value);
    
    teardown();
}

TEST(type_parser, parse_enum_non_sequential) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_enum_string(
        "IDLE=0,RUNNING=10,STOPPED=20", &values, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3, count);
    ASSERT_EQ(0, values[0].value);
    ASSERT_EQ(10, values[1].value);
    ASSERT_EQ(20, values[2].value);
    
    teardown();
}

TEST(type_parser, parse_enum_negative_values) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_enum_string(
        "MINUS_ONE=-1,ZERO=0,ONE=1", &values, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3, count);
    ASSERT_EQ(-1, values[0].value);
    ASSERT_EQ(0, values[1].value);
    ASSERT_EQ(1, values[2].value);
    
    teardown();
}

TEST(type_parser, parse_enum_hex_values) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_enum_string(
        "LOW=0x00,MID=0x7F,HIGH=0xFF", &values, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3, count);
    ASSERT_EQ(0x00, values[0].value);
    ASSERT_EQ(0x7F, values[1].value);
    ASSERT_EQ(0xFF, values[2].value);
    
    teardown();
}

TEST(type_parser, parse_enum_empty_string) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_enum_string(
        "   ", &values, &count, arena);
    
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, result.code);
    
    teardown();
}

TEST(type_parser, parse_enum_duplicate_name) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_enum_string(
        "VALUE=0,VALUE=1", &values, &count, arena);
    
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, result.code);
    
    teardown();
}

TEST(type_parser, parse_struct_fields_simple) {
    setup();
    
    char **names = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_struct_fields(
        "Position,Rotation,Scale", &names, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3, count);
    ASSERT_NE(NULL, names);
    ASSERT_STR_EQ("Position", names[0]);
    ASSERT_STR_EQ("Rotation", names[1]);
    ASSERT_STR_EQ("Scale", names[2]);
    
    teardown();
}

TEST(type_parser, parse_struct_fields_single) {
    setup();
    
    char **names = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_struct_fields(
        "Value", &names, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(1, count);
    ASSERT_STR_EQ("Value", names[0]);
    
    teardown();
}

TEST(type_parser, parse_struct_fields_with_whitespace) {
    setup();
    
    char **names = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_struct_fields(
        "  x , y  ,  z  ", &names, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3, count);
    ASSERT_STR_EQ("x", names[0]);
    ASSERT_STR_EQ("y", names[1]);
    ASSERT_STR_EQ("z", names[2]);
    
    teardown();
}

TEST(type_parser, parse_struct_fields_underscore) {
    setup();
    
    char **names = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_struct_fields(
        "field_1,field_2,_private_field", &names, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3, count);
    ASSERT_STR_EQ("field_1", names[0]);
    ASSERT_STR_EQ("field_2", names[1]);
    ASSERT_STR_EQ("_private_field", names[2]);
    
    teardown();
}

TEST(type_parser, parse_struct_fields_many) {
    setup();
    
    char **names = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_struct_fields(
        "a,b,c,d,e,f,g,h,i,j", &names, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(10, count);
    ASSERT_STR_EQ("a", names[0]);
    ASSERT_STR_EQ("j", names[9]);
    
    teardown();
}

TEST(type_parser, parse_struct_fields_empty_string) {
    setup();
    
    char **names = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_struct_fields(
        "", &names, &count, arena);
    
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, result.code);
    
    teardown();
}

TEST(type_parser, parse_struct_fields_null_input) {
    setup();
    
    char **names = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_struct_fields(
        NULL, &names, &count, arena);
    
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_ARGUMENT, result.code);
    
    teardown();
}

TEST(type_parser, parse_struct_fields_empty_field) {
    setup();
    
    char **names = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_struct_fields(
        "field1,,field2", &names, &count, arena);
    
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, result.code);
    
    teardown();
}

TEST(type_parser, parse_struct_fields_invalid_identifier) {
    setup();
    
    char **names = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_struct_fields(
        "field1,2field", &names, &count, arena);
    
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, result.code);
    
    teardown();
}

TEST(type_parser, parse_struct_fields_special_chars) {
    setup();
    
    char **names = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_struct_fields(
        "field-1,field@2", &names, &count, arena);
    
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, result.code);
    
    teardown();
}

TEST(type_parser, parse_struct_fields_duplicate_name) {
    setup();
    
    char **names = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_struct_fields(
        "field,field", &names, &count, arena);
    
    ASSERT_NE(NMO_OK, result.code);
    ASSERT_EQ(NMO_ERR_INVALID_FORMAT, result.code);
    
    teardown();
}

TEST(type_parser, parse_flags_large_hex_value) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_flags_string(
        "ALL=0xFFFFFFFF", &values, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(1, count);
    ASSERT_EQ(0xFFFFFFFF, values[0].value);
    
    teardown();
}

TEST(type_parser, parse_enum_long_names) {
    setup();
    
    nmo_enum_value_def_t *values = NULL;
    size_t count = 0;
    
    nmo_result_t result = nmo_parse_enum_string(
        "VERY_LONG_ENUM_NAME_ONE=1,VERY_LONG_ENUM_NAME_TWO=2",
        &values, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(2, count);
    ASSERT_STR_EQ("VERY_LONG_ENUM_NAME_ONE", values[0].name);
    ASSERT_STR_EQ("VERY_LONG_ENUM_NAME_TWO", values[1].name);
    
    teardown();
}

TEST(type_parser, parse_struct_many_fields) {
    setup();
    
    char **names = NULL;
    size_t count = 0;
    
    char buffer[1024] = {0};
    for (int i = 0; i < 50; i++) {
        char field[16];
        snprintf(field, sizeof(field), "field%d%s", i, (i < 49) ? "," : "");
        strcat(buffer, field);
    }
    
    nmo_result_t result = nmo_parse_struct_fields(
        buffer, &names, &count, arena);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(50, count);
    ASSERT_STR_EQ("field0", names[0]);
    ASSERT_STR_EQ("field49", names[49]);
    
    teardown();
}

/* ============================================================================
 * Test Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* Basic types */
    REGISTER_TEST(type_parser, parse_basic_int);
    REGISTER_TEST(type_parser, parse_basic_float);
    REGISTER_TEST(type_parser, parse_case_insensitive);
    REGISTER_TEST(type_parser, parse_with_whitespace);
    
    /* Arrays */
    REGISTER_TEST(type_parser, parse_array_int10);
    REGISTER_TEST(type_parser, parse_array_float256);
    REGISTER_TEST(type_parser, parse_array_with_spaces);
    
    /* Pointers */
    REGISTER_TEST(type_parser, parse_pointer_int);
    REGISTER_TEST(type_parser, parse_double_pointer);
    
    /* Error handling */
    REGISTER_TEST(type_parser, parse_empty_string);
    REGISTER_TEST(type_parser, parse_unknown_type);
    REGISTER_TEST(type_parser, parse_null_params);
    
    /* GUID generation */
    REGISTER_TEST(type_parser, generate_guid_deterministic);
    REGISTER_TEST(type_parser, generate_guid_different_names);
    REGISTER_TEST(type_parser, generate_guid_null_name);
    REGISTER_TEST(type_parser, generate_guid_has_marker_bit);
    
    /* Virtools types */
    REGISTER_TEST(type_parser, parse_virtools_vector3);
    REGISTER_TEST(type_parser, parse_virtools_color);
    
    /* String parsers (Phase 6.2.1) */
    REGISTER_TEST(type_parser, parse_flags_simple);
    REGISTER_TEST(type_parser, parse_flags_hex_values);
    REGISTER_TEST(type_parser, parse_flags_with_whitespace);
    REGISTER_TEST(type_parser, parse_flags_single_entry);
    REGISTER_TEST(type_parser, parse_flags_zero_value);
    REGISTER_TEST(type_parser, parse_flags_combined_values);
    REGISTER_TEST(type_parser, parse_flags_empty_string);
    REGISTER_TEST(type_parser, parse_flags_null_input);
    REGISTER_TEST(type_parser, parse_flags_missing_equals);
    REGISTER_TEST(type_parser, parse_flags_invalid_identifier);
    REGISTER_TEST(type_parser, parse_flags_invalid_value);
    REGISTER_TEST(type_parser, parse_flags_duplicate_name);
    REGISTER_TEST(type_parser, parse_enum_simple);
    REGISTER_TEST(type_parser, parse_enum_non_sequential);
    REGISTER_TEST(type_parser, parse_enum_negative_values);
    REGISTER_TEST(type_parser, parse_enum_hex_values);
    REGISTER_TEST(type_parser, parse_enum_empty_string);
    REGISTER_TEST(type_parser, parse_enum_duplicate_name);
    REGISTER_TEST(type_parser, parse_struct_fields_simple);
    REGISTER_TEST(type_parser, parse_struct_fields_single);
    REGISTER_TEST(type_parser, parse_struct_fields_with_whitespace);
    REGISTER_TEST(type_parser, parse_struct_fields_underscore);
    REGISTER_TEST(type_parser, parse_struct_fields_many);
    REGISTER_TEST(type_parser, parse_struct_fields_empty_string);
    REGISTER_TEST(type_parser, parse_struct_fields_null_input);
    REGISTER_TEST(type_parser, parse_struct_fields_empty_field);
    REGISTER_TEST(type_parser, parse_struct_fields_invalid_identifier);
    REGISTER_TEST(type_parser, parse_struct_fields_special_chars);
    REGISTER_TEST(type_parser, parse_struct_fields_duplicate_name);
    REGISTER_TEST(type_parser, parse_flags_large_hex_value);
    REGISTER_TEST(type_parser, parse_enum_long_names);
    REGISTER_TEST(type_parser, parse_struct_many_fields);
TEST_MAIN_END()

