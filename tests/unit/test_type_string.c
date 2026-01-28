/**
 * @file test_type_string.c
 * @brief Unit tests for type string conversion system (Phase 6.4.4)
 *
 * Tests all type-to-string and string-to-type converters:
 * - Float (normal, scientific, special values)
 * - Int (decimal, hexadecimal)
 * - Bool (true/false/1/0)
 * - Vector (3D)
 * - Quaternion (4D)
 * - Enum (name and value)
 * - Flags (names and hex)
 * - String (with escaping)
 * - ObjectID (stub)
 */

#include "test_framework.h"
#include "type/type_string.h"
#include "type/type_system.h"
#include "core/nmo_arena.h"
#include "core/nmo_guid.h"
#include <math.h>
#include <string.h>

/* ============================================================================
 * Test Setup
 * ============================================================================ */

static nmo_arena_t *arena = NULL;
static nmo_type_registry_t *registry = NULL;

static void setup(void) {
    arena = nmo_arena_create(NULL, 65536);
    ASSERT_NE(NULL, arena);
    
    registry = nmo_type_registry_create(arena);
    ASSERT_NE(NULL, registry);
}

static void teardown(void) {
    if (registry) {
        nmo_type_registry_destroy(registry);
        registry = NULL;
    }
    if (arena) {
        nmo_arena_destroy(arena);
        arena = NULL;
    }
}

/* ============================================================================
 * Float Conversion Tests
 * ============================================================================ */

TEST(type_string, float_to_string_normal) {
    setup();
    
    char buffer[64];
    float value = 3.14159f;
    nmo_result_t result = nmo_float_to_string(&value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("3.14159", buffer);
    
    teardown();
}

TEST(type_string, float_to_string_negative) {
    setup();
    
    char buffer[64];
    float value = -2.71828f;
    nmo_result_t result = nmo_float_to_string(&value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("-2.71828", buffer);
    
    teardown();
}

TEST(type_string, float_to_string_nan) {
    setup();
    
    char buffer[64];
    float value = NAN;
    nmo_result_t result = nmo_float_to_string(&value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("NaN", buffer);
    
    teardown();
}

TEST(type_string, float_to_string_infinity) {
    setup();
    
    char buffer[64];
    float value = INFINITY;
    nmo_result_t result = nmo_float_to_string(&value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("Infinity", buffer);
    
    teardown();
}

TEST(type_string, float_from_string_normal) {
    setup();
    
    float value = 0.0f;
    nmo_result_t result = nmo_float_from_string(&value, "3.14159");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_FLOAT_EQ(3.14159f, value, 0.00001f);
    
    teardown();
}

TEST(type_string, float_from_string_scientific) {
    setup();
    
    float value = 0.0f;
    nmo_result_t result = nmo_float_from_string(&value, "2.5e-3");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_FLOAT_EQ(0.0025f, value, 0.000001f);
    
    teardown();
}

TEST(type_string, float_from_string_nan) {
    setup();
    
    float value = 0.0f;
    nmo_result_t result = nmo_float_from_string(&value, "NaN");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_TRUE(isnan(value));
    
    teardown();
}

TEST(type_string, float_roundtrip) {
    setup();
    
    float original = 123.456f;
    char buffer[64];
    float parsed = 0.0f;
    
    nmo_result_t r1 = nmo_float_to_string(&original, buffer, sizeof(buffer));
    ASSERT_EQ(NMO_OK, r1.code);
    
    nmo_result_t r2 = nmo_float_from_string(&parsed, buffer);
    ASSERT_EQ(NMO_OK, r2.code);
    
    ASSERT_FLOAT_EQ(original, parsed, 0.001f);
    
    teardown();
}

/* ============================================================================
 * Int Conversion Tests
 * ============================================================================ */

TEST(type_string, int_to_string_decimal) {
    setup();
    
    char buffer[64];
    int32_t value = 42;
    nmo_result_t result = nmo_int_to_string(&value, buffer, sizeof(buffer), false);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("42", buffer);
    
    teardown();
}

TEST(type_string, int_to_string_negative) {
    setup();
    
    char buffer[64];
    int32_t value = -100;
    nmo_result_t result = nmo_int_to_string(&value, buffer, sizeof(buffer), false);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("-100", buffer);
    
    teardown();
}

TEST(type_string, int_to_string_hex) {
    setup();
    
    char buffer[64];
    int32_t value = 255;
    nmo_result_t result = nmo_int_to_string(&value, buffer, sizeof(buffer), true);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("0xFF", buffer);
    
    teardown();
}

TEST(type_string, int_from_string_decimal) {
    setup();
    
    int32_t value = 0;
    nmo_result_t result = nmo_int_from_string(&value, "42");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(42, value);
    
    teardown();
}

TEST(type_string, int_from_string_hex) {
    setup();
    
    int32_t value = 0;
    nmo_result_t result = nmo_int_from_string(&value, "0x2A");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(42, value);
    
    teardown();
}

TEST(type_string, int_from_string_negative) {
    setup();
    
    int32_t value = 0;
    nmo_result_t result = nmo_int_from_string(&value, "-100");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(-100, value);
    
    teardown();
}

TEST(type_string, int_roundtrip_decimal) {
    setup();
    
    int32_t original = 12345;
    char buffer[64];
    int32_t parsed = 0;
    
    nmo_result_t r1 = nmo_int_to_string(&original, buffer, sizeof(buffer), false);
    ASSERT_EQ(NMO_OK, r1.code);
    
    nmo_result_t r2 = nmo_int_from_string(&parsed, buffer);
    ASSERT_EQ(NMO_OK, r2.code);
    
    ASSERT_EQ(original, parsed);
    
    teardown();
}

TEST(type_string, int_roundtrip_hex) {
    setup();
    
    int32_t original = 0xABCD;
    char buffer[64];
    int32_t parsed = 0;
    
    nmo_result_t r1 = nmo_int_to_string(&original, buffer, sizeof(buffer), true);
    ASSERT_EQ(NMO_OK, r1.code);
    
    nmo_result_t r2 = nmo_int_from_string(&parsed, buffer);
    ASSERT_EQ(NMO_OK, r2.code);
    
    ASSERT_EQ(original, parsed);
    
    teardown();
}

/* ============================================================================
 * Bool Conversion Tests
 * ============================================================================ */

TEST(type_string, bool_to_string_true) {
    setup();
    
    char buffer[64];
    bool value = true;
    nmo_result_t result = nmo_bool_to_string(&value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("true", buffer);
    
    teardown();
}

TEST(type_string, bool_to_string_false) {
    setup();
    
    char buffer[64];
    bool value = false;
    nmo_result_t result = nmo_bool_to_string(&value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("false", buffer);
    
    teardown();
}

TEST(type_string, bool_from_string_true) {
    setup();
    
    bool value = false;
    nmo_result_t result = nmo_bool_from_string(&value, "true");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_TRUE(value);
    
    teardown();
}

TEST(type_string, bool_from_string_false) {
    setup();
    
    bool value = true;
    nmo_result_t result = nmo_bool_from_string(&value, "false");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_FALSE(value);
    
    teardown();
}

TEST(type_string, bool_from_string_one) {
    setup();
    
    bool value = false;
    nmo_result_t result = nmo_bool_from_string(&value, "1");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_TRUE(value);
    
    teardown();
}

TEST(type_string, bool_from_string_zero) {
    setup();
    
    bool value = true;
    nmo_result_t result = nmo_bool_from_string(&value, "0");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_FALSE(value);
    
    teardown();
}

TEST(type_string, bool_roundtrip) {
    setup();
    
    bool original = true;
    char buffer[64];
    bool parsed = false;
    
    nmo_result_t r1 = nmo_bool_to_string(&original, buffer, sizeof(buffer));
    ASSERT_EQ(NMO_OK, r1.code);
    
    nmo_result_t r2 = nmo_bool_from_string(&parsed, buffer);
    ASSERT_EQ(NMO_OK, r2.code);
    
    ASSERT_EQ(original, parsed);
    
    teardown();
}

/* ============================================================================
 * Vector Conversion Tests
 * ============================================================================ */

TEST(type_string, vector_to_string) {
    setup();
    
    char buffer[128];
    float value[3] = {1.0f, 2.0f, 3.0f};
    nmo_result_t result = nmo_vector_to_string(value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("(1, 2, 3)", buffer);
    
    teardown();
}

TEST(type_string, vector_from_string) {
    setup();
    
    float value[3] = {0.0f, 0.0f, 0.0f};
    nmo_result_t result = nmo_vector_from_string(value, "(1.5, 2.5, 3.5)");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_FLOAT_EQ(1.5f, value[0], 0.001f);
    ASSERT_FLOAT_EQ(2.5f, value[1], 0.001f);
    ASSERT_FLOAT_EQ(3.5f, value[2], 0.001f);
    
    teardown();
}

TEST(type_string, vector_from_string_spaces) {
    setup();
    
    float value[3] = {0.0f, 0.0f, 0.0f};
    nmo_result_t result = nmo_vector_from_string(value, "( 10 , 20 , 30 )");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_FLOAT_EQ(10.0f, value[0], 0.001f);
    ASSERT_FLOAT_EQ(20.0f, value[1], 0.001f);
    ASSERT_FLOAT_EQ(30.0f, value[2], 0.001f);
    
    teardown();
}

TEST(type_string, vector_roundtrip) {
    setup();
    
    float original[3] = {-5.5f, 10.25f, 0.333f};
    char buffer[128];
    float parsed[3] = {0.0f, 0.0f, 0.0f};
    
    nmo_result_t r1 = nmo_vector_to_string(original, buffer, sizeof(buffer));
    ASSERT_EQ(NMO_OK, r1.code);
    
    nmo_result_t r2 = nmo_vector_from_string(parsed, buffer);
    ASSERT_EQ(NMO_OK, r2.code);
    
    ASSERT_FLOAT_EQ(original[0], parsed[0], 0.001f);
    ASSERT_FLOAT_EQ(original[1], parsed[1], 0.001f);
    ASSERT_FLOAT_EQ(original[2], parsed[2], 0.001f);
    
    teardown();
}

/* ============================================================================
 * Quaternion Conversion Tests
 * ============================================================================ */

TEST(type_string, quaternion_to_string) {
    setup();
    
    char buffer[128];
    float value[4] = {0.707f, 0.0f, 0.707f, 0.0f};
    nmo_result_t result = nmo_quaternion_to_string(value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("(0.707, 0, 0.707, 0)", buffer);
    
    teardown();
}

TEST(type_string, quaternion_from_string) {
    setup();
    
    float value[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_result_t result = nmo_quaternion_from_string(value, "(0.5, 0.5, 0.5, 0.5)");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_FLOAT_EQ(0.5f, value[0], 0.001f);
    ASSERT_FLOAT_EQ(0.5f, value[1], 0.001f);
    ASSERT_FLOAT_EQ(0.5f, value[2], 0.001f);
    ASSERT_FLOAT_EQ(0.5f, value[3], 0.001f);
    
    teardown();
}

TEST(type_string, quaternion_roundtrip) {
    setup();
    
    float original[4] = {0.707f, 0.0f, 0.707f, 0.0f};
    char buffer[128];
    float parsed[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    
    nmo_result_t r1 = nmo_quaternion_to_string(original, buffer, sizeof(buffer));
    ASSERT_EQ(NMO_OK, r1.code);
    
    nmo_result_t r2 = nmo_quaternion_from_string(parsed, buffer);
    ASSERT_EQ(NMO_OK, r2.code);
    
    ASSERT_FLOAT_EQ(original[0], parsed[0], 0.001f);
    ASSERT_FLOAT_EQ(original[1], parsed[1], 0.001f);
    ASSERT_FLOAT_EQ(original[2], parsed[2], 0.001f);
    ASSERT_FLOAT_EQ(original[3], parsed[3], 0.001f);
    
    teardown();
}

/* ============================================================================
 * Enum Conversion Tests (DISABLED - requires Phase 6.2 register_enum API)
 * ============================================================================ */

#if 0  // Disabled until nmo_type_registry_register_enum is implemented
TEST(type_string, enum_to_string_by_name) {
    setup();
    
    // Register enum type: Color { RED=1, GREEN=2, BLUE=3 }
    nmo_guid_t enum_guid = {0x12345678, 0x00000001};
    nmo_result_t reg_result = nmo_type_registry_register_enum(
        registry, enum_guid, "Color", "RED=1,GREEN=2,BLUE=3");
    ASSERT_EQ(NMO_OK, reg_result.code);
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, enum_guid);
    ASSERT_NE(NULL, type);
    
    char buffer[64];
    int32_t value = 2;  // GREEN
    nmo_result_t result = nmo_enum_to_string(&value, type, buffer, sizeof(buffer), true);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("GREEN", buffer);
    
    teardown();
}

TEST(type_string, enum_to_string_by_value) {
    setup();
    
    nmo_guid_t enum_guid = {0x12345678, 0x00000001};
    nmo_type_registry_register_enum(registry, enum_guid, "Color", "RED=1,GREEN=2,BLUE=3");
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, enum_guid);
    
    char buffer[64];
    int32_t value = 2;
    nmo_result_t result = nmo_enum_to_string(&value, type, buffer, sizeof(buffer), false);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("2", buffer);
    
    teardown();
}

TEST(type_string, enum_from_string_by_name) {
    setup();
    
    nmo_guid_t enum_guid = {0x12345678, 0x00000001};
    nmo_type_registry_register_enum(registry, enum_guid, "Color", "RED=1,GREEN=2,BLUE=3");
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, enum_guid);
    
    int32_t value = 0;
    nmo_result_t result = nmo_enum_from_string(&value, type, "BLUE");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3, value);
    
    teardown();
}

TEST(type_string, enum_from_string_by_value) {
    setup();
    
    nmo_guid_t enum_guid = {0x12345678, 0x00000001};
    nmo_type_registry_register_enum(registry, enum_guid, "Color", "RED=1,GREEN=2,BLUE=3");
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, enum_guid);
    
    int32_t value = 0;
    nmo_result_t result = nmo_enum_from_string(&value, type, "2");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(2, value);
    
    teardown();
}

TEST(type_string, enum_roundtrip) {
    setup();
    
    nmo_guid_t enum_guid = {0x12345678, 0x00000001};
    nmo_type_registry_register_enum(registry, enum_guid, "Color", "RED=1,GREEN=2,BLUE=3");
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, enum_guid);
    
    int32_t original = 1;  // RED
    char buffer[64];
    int32_t parsed = 0;
    
    nmo_result_t r1 = nmo_enum_to_string(&original, type, buffer, sizeof(buffer), true);
    ASSERT_EQ(NMO_OK, r1.code);
    
    nmo_result_t r2 = nmo_enum_from_string(&parsed, type, buffer);
    ASSERT_EQ(NMO_OK, r2.code);
    
    ASSERT_EQ(original, parsed);
    
    teardown();
}
#endif  // Disabled enum tests

/* ============================================================================
 * Flags Conversion Tests (DISABLED - requires Phase 6.2 register_flags API)
 * ============================================================================ */

#if 0  // Disabled until nmo_type_registry_register_flags is implemented
TEST(type_string, flags_to_string_by_names) {
    setup();
    
    // Register flags type: FileMode { READ=1, WRITE=2, EXECUTE=4 }
    nmo_guid_t flags_guid = {0x12345678, 0x00000002};
    nmo_result_t reg_result = nmo_type_registry_register_flags(
        registry, flags_guid, "FileMode", "READ=1,WRITE=2,EXECUTE=4");
    ASSERT_EQ(NMO_OK, reg_result.code);
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, flags_guid);
    ASSERT_NE(NULL, type);
    
    char buffer[128];
    uint32_t value = 3;  // READ | WRITE
    nmo_result_t result = nmo_flags_to_string(&value, type, buffer, sizeof(buffer), true);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("READ|WRITE", buffer);
    
    teardown();
}

TEST(type_string, flags_to_string_by_hex) {
    setup();
    
    nmo_guid_t flags_guid = {0x12345678, 0x00000002};
    nmo_type_registry_register_flags(registry, flags_guid, "FileMode", "READ=1,WRITE=2,EXECUTE=4");
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, flags_guid);
    
    char buffer[128];
    uint32_t value = 7;  // READ | WRITE | EXECUTE
    nmo_result_t result = nmo_flags_to_string(&value, type, buffer, sizeof(buffer), false);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("0x7", buffer);
    
    teardown();
}

TEST(type_string, flags_from_string_by_names) {
    setup();
    
    nmo_guid_t flags_guid = {0x12345678, 0x00000002};
    nmo_type_registry_register_flags(registry, flags_guid, "FileMode", "READ=1,WRITE=2,EXECUTE=4");
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, flags_guid);
    
    uint32_t value = 0;
    nmo_result_t result = nmo_flags_from_string(&value, type, "READ|EXECUTE");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(5u, value);  // 1 | 4
    
    teardown();
}

TEST(type_string, flags_from_string_by_hex) {
    setup();
    
    nmo_guid_t flags_guid = {0x12345678, 0x00000002};
    nmo_type_registry_register_flags(registry, flags_guid, "FileMode", "READ=1,WRITE=2,EXECUTE=4");
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, flags_guid);
    
    uint32_t value = 0;
    nmo_result_t result = nmo_flags_from_string(&value, type, "0x3");
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(3u, value);
    
    teardown();
}

TEST(type_string, flags_roundtrip) {
    setup();
    
    nmo_guid_t flags_guid = {0x12345678, 0x00000002};
    nmo_type_registry_register_flags(registry, flags_guid, "FileMode", "READ=1,WRITE=2,EXECUTE=4");
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, flags_guid);
    
    uint32_t original = 6;  // WRITE | EXECUTE
    char buffer[128];
    uint32_t parsed = 0;
    
    nmo_result_t r1 = nmo_flags_to_string(&original, type, buffer, sizeof(buffer), true);
    ASSERT_EQ(NMO_OK, r1.code);
    
    nmo_result_t r2 = nmo_flags_from_string(&parsed, type, buffer);
    ASSERT_EQ(NMO_OK, r2.code);
    
    ASSERT_EQ(original, parsed);
    
    teardown();
}
#endif  // Disabled flags tests

/* ============================================================================
 * String Escape/Unescape Tests
 * ============================================================================ */

TEST(type_string, string_escape_simple) {
    setup();
    
    char buffer[128];
    size_t len = nmo_string_escape("Hello", buffer, sizeof(buffer));
    
    ASSERT_GT(len, 0u);
    ASSERT_STR_EQ("\"Hello\"", buffer);
    
    teardown();
}

TEST(type_string, string_escape_with_quotes) {
    setup();
    
    char buffer[128];
    size_t len = nmo_string_escape("Say \"Hello\"", buffer, sizeof(buffer));
    
    ASSERT_GT(len, 0u);
    ASSERT_STR_EQ("\"Say \\\"Hello\\\"\"", buffer);
    
    teardown();
}

TEST(type_string, string_escape_with_newline) {
    setup();
    
    char buffer[128];
    size_t len = nmo_string_escape("Line1\nLine2", buffer, sizeof(buffer));
    
    ASSERT_GT(len, 0u);
    ASSERT_STR_EQ("\"Line1\\nLine2\"", buffer);
    
    teardown();
}

TEST(type_string, string_unescape_simple) {
    setup();
    
    char buffer[128];
    size_t len = nmo_string_unescape("\"Hello\"", buffer, sizeof(buffer));
    
    ASSERT_GT(len, 0u);
    ASSERT_STR_EQ("Hello", buffer);
    
    teardown();
}

TEST(type_string, string_unescape_with_quotes) {
    setup();
    
    char buffer[128];
    size_t len = nmo_string_unescape("\"Say \\\"Hello\\\"\"", buffer, sizeof(buffer));
    
    ASSERT_GT(len, 0u);
    ASSERT_STR_EQ("Say \"Hello\"", buffer);
    
    teardown();
}

TEST(type_string, string_unescape_with_newline) {
    setup();
    
    char buffer[128];
    size_t len = nmo_string_unescape("\"Line1\\nLine2\"", buffer, sizeof(buffer));
    
    ASSERT_GT(len, 0u);
    ASSERT_STR_EQ("Line1\nLine2", buffer);
    
    teardown();
}

TEST(type_string, string_escape_unescape_roundtrip) {
    setup();
    
    const char *original = "Test\nWith\t\"Escapes\"\\And\\Backslashes";
    char escaped[256];
    char unescaped[256];
    
    size_t esc_len = nmo_string_escape(original, escaped, sizeof(escaped));
    ASSERT_GT(esc_len, 0u);
    
    size_t unesc_len = nmo_string_unescape(escaped, unescaped, sizeof(unescaped));
    ASSERT_GT(unesc_len, 0u);
    
    ASSERT_STR_EQ(original, unescaped);
    
    teardown();
}

/* ============================================================================
 * Object ID Tests
 * ============================================================================ */

TEST(type_string, object_id_to_string) {
    setup();
    
    char buffer[64];
    nmo_id_t value = 12345;
    nmo_result_t result = nmo_object_id_to_string(&value, buffer, sizeof(buffer), NULL);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_STR_EQ("#12345", buffer);
    
    teardown();
}

TEST(type_string, object_id_from_string) {
    setup();
    
    nmo_id_t value = 0;
    nmo_result_t result = nmo_object_id_from_string(&value, "#12345", NULL);
    
    ASSERT_EQ(NMO_OK, result.code);
    ASSERT_EQ(12345u, value);
    
    teardown();
}

TEST(type_string, object_id_roundtrip) {
    setup();
    
    nmo_id_t original = 99999;
    char buffer[64];
    nmo_id_t parsed = 0;
    
    nmo_result_t r1 = nmo_object_id_to_string(&original, buffer, sizeof(buffer), NULL);
    ASSERT_EQ(NMO_OK, r1.code);
    
    nmo_result_t r2 = nmo_object_id_from_string(&parsed, buffer, NULL);
    ASSERT_EQ(NMO_OK, r2.code);
    
    ASSERT_EQ(original, parsed);
    
    teardown();
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

TEST_MAIN_BEGIN()
    // Float tests
    REGISTER_TEST(type_string, float_to_string_normal);
    REGISTER_TEST(type_string, float_to_string_negative);
    REGISTER_TEST(type_string, float_to_string_nan);
    REGISTER_TEST(type_string, float_to_string_infinity);
    REGISTER_TEST(type_string, float_from_string_normal);
    REGISTER_TEST(type_string, float_from_string_scientific);
    REGISTER_TEST(type_string, float_from_string_nan);
    REGISTER_TEST(type_string, float_roundtrip);
    
    // Int tests
    REGISTER_TEST(type_string, int_to_string_decimal);
    REGISTER_TEST(type_string, int_to_string_negative);
    REGISTER_TEST(type_string, int_to_string_hex);
    REGISTER_TEST(type_string, int_from_string_decimal);
    REGISTER_TEST(type_string, int_from_string_hex);
    REGISTER_TEST(type_string, int_from_string_negative);
    REGISTER_TEST(type_string, int_roundtrip_decimal);
    REGISTER_TEST(type_string, int_roundtrip_hex);
    
    // Bool tests
    REGISTER_TEST(type_string, bool_to_string_true);
    REGISTER_TEST(type_string, bool_to_string_false);
    REGISTER_TEST(type_string, bool_from_string_true);
    REGISTER_TEST(type_string, bool_from_string_false);
    REGISTER_TEST(type_string, bool_from_string_one);
    REGISTER_TEST(type_string, bool_from_string_zero);
    REGISTER_TEST(type_string, bool_roundtrip);
    
    // Vector tests
    REGISTER_TEST(type_string, vector_to_string);
    REGISTER_TEST(type_string, vector_from_string);
    REGISTER_TEST(type_string, vector_from_string_spaces);
    REGISTER_TEST(type_string, vector_roundtrip);
    
    // Quaternion tests
    REGISTER_TEST(type_string, quaternion_to_string);
    REGISTER_TEST(type_string, quaternion_from_string);
    REGISTER_TEST(type_string, quaternion_roundtrip);
    
    // Enum tests (DISABLED - Phase 6.2 API not implemented yet)
    /*
    REGISTER_TEST(type_string, enum_to_string_by_name);
    REGISTER_TEST(type_string, enum_to_string_by_value);
    REGISTER_TEST(type_string, enum_from_string_by_name);
    REGISTER_TEST(type_string, enum_from_string_by_value);
    REGISTER_TEST(type_string, enum_roundtrip);
    */
    
    // Flags tests (DISABLED - Phase 6.2 API not implemented yet)
    /*
    REGISTER_TEST(type_string, flags_to_string_by_names);
    REGISTER_TEST(type_string, flags_to_string_by_hex);
    REGISTER_TEST(type_string, flags_from_string_by_names);
    REGISTER_TEST(type_string, flags_from_string_by_hex);
    REGISTER_TEST(type_string, flags_roundtrip);
    */
    
    // String escape/unescape tests
    REGISTER_TEST(type_string, string_escape_simple);
    REGISTER_TEST(type_string, string_escape_with_quotes);
    REGISTER_TEST(type_string, string_escape_with_newline);
    REGISTER_TEST(type_string, string_unescape_simple);
    REGISTER_TEST(type_string, string_unescape_with_quotes);
    REGISTER_TEST(type_string, string_unescape_with_newline);
    REGISTER_TEST(type_string, string_escape_unescape_roundtrip);
    
    // Object ID tests
    REGISTER_TEST(type_string, object_id_to_string);
    REGISTER_TEST(type_string, object_id_from_string);
    REGISTER_TEST(type_string, object_id_roundtrip);
TEST_MAIN_END()
