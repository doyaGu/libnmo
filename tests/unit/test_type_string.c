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
#include "type/nmo_type_string.h"
#include "type/nmo_type_system.h"
#include "type/nmo_dynamic_types.h"
#include "type/nmo_type_guids.h"
#include "type/nmo_operations.h"
#include "type/nmo_reflection.h"
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
    nmo_status_t result = nmo_float_to_string(&value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("3.14159", buffer);
    
    teardown();
}

TEST(type_string, float_to_string_negative) {
    setup();
    
    char buffer[64];
    float value = -2.71828f;
    nmo_status_t result = nmo_float_to_string(&value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("-2.71828", buffer);
    
    teardown();
}

TEST(type_string, float_to_string_nan) {
    setup();
    
    char buffer[64];
    float value = NAN;
    nmo_status_t result = nmo_float_to_string(&value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("NaN", buffer);
    
    teardown();
}

TEST(type_string, float_to_string_infinity) {
    setup();
    
    char buffer[64];
    float value = INFINITY;
    nmo_status_t result = nmo_float_to_string(&value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("Infinity", buffer);
    
    teardown();
}

TEST(type_string, float_from_string_normal) {
    setup();
    
    float value = 0.0f;
    nmo_status_t result = nmo_float_from_string(&value, "3.14159");
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_FLOAT_EQ(3.14159f, value, 0.00001f);
    
    teardown();
}

TEST(type_string, float_from_string_scientific) {
    setup();
    
    float value = 0.0f;
    nmo_status_t result = nmo_float_from_string(&value, "2.5e-3");
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_FLOAT_EQ(0.0025f, value, 0.000001f);
    
    teardown();
}

TEST(type_string, float_from_string_nan) {
    setup();
    
    float value = 0.0f;
    nmo_status_t result = nmo_float_from_string(&value, "NaN");
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_TRUE(isnan(value));
    
    teardown();
}

TEST(type_string, float_roundtrip) {
    setup();
    
    float original = 123.456f;
    char buffer[64];
    float parsed = 0.0f;
    
    nmo_status_t r1 = nmo_float_to_string(&original, buffer, sizeof(buffer));
    ASSERT_EQ(NMO_OK, r1);
    
    nmo_status_t r2 = nmo_float_from_string(&parsed, buffer);
    ASSERT_EQ(NMO_OK, r2);
    
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
    nmo_status_t result = nmo_int_to_string(&value, buffer, sizeof(buffer), false);
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("42", buffer);
    
    teardown();
}

TEST(type_string, int_to_string_negative) {
    setup();
    
    char buffer[64];
    int32_t value = -100;
    nmo_status_t result = nmo_int_to_string(&value, buffer, sizeof(buffer), false);
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("-100", buffer);
    
    teardown();
}

TEST(type_string, int_to_string_hex) {
    setup();
    
    char buffer[64];
    int32_t value = 255;
    nmo_status_t result = nmo_int_to_string(&value, buffer, sizeof(buffer), true);
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("0xFF", buffer);
    
    teardown();
}

TEST(type_string, int_from_string_decimal) {
    setup();
    
    int32_t value = 0;
    nmo_status_t result = nmo_int_from_string(&value, "42");
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(42, value);
    
    teardown();
}

TEST(type_string, int_from_string_hex) {
    setup();
    
    int32_t value = 0;
    nmo_status_t result = nmo_int_from_string(&value, "0x2A");
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(42, value);
    
    teardown();
}

TEST(type_string, int_from_string_negative) {
    setup();
    
    int32_t value = 0;
    nmo_status_t result = nmo_int_from_string(&value, "-100");
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(-100, value);
    
    teardown();
}

TEST(type_string, int_roundtrip_decimal) {
    setup();
    
    int32_t original = 12345;
    char buffer[64];
    int32_t parsed = 0;
    
    nmo_status_t r1 = nmo_int_to_string(&original, buffer, sizeof(buffer), false);
    ASSERT_EQ(NMO_OK, r1);
    
    nmo_status_t r2 = nmo_int_from_string(&parsed, buffer);
    ASSERT_EQ(NMO_OK, r2);
    
    ASSERT_EQ(original, parsed);
    
    teardown();
}

TEST(type_string, int_roundtrip_hex) {
    setup();
    
    int32_t original = 0xABCD;
    char buffer[64];
    int32_t parsed = 0;
    
    nmo_status_t r1 = nmo_int_to_string(&original, buffer, sizeof(buffer), true);
    ASSERT_EQ(NMO_OK, r1);
    
    nmo_status_t r2 = nmo_int_from_string(&parsed, buffer);
    ASSERT_EQ(NMO_OK, r2);
    
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
    nmo_status_t result = nmo_bool_to_string(&value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("true", buffer);
    
    teardown();
}

TEST(type_string, bool_to_string_false) {
    setup();
    
    char buffer[64];
    bool value = false;
    nmo_status_t result = nmo_bool_to_string(&value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("false", buffer);
    
    teardown();
}

TEST(type_string, bool_from_string_true) {
    setup();
    
    bool value = false;
    nmo_status_t result = nmo_bool_from_string(&value, "true");
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_TRUE(value);
    
    teardown();
}

TEST(type_string, bool_from_string_false) {
    setup();
    
    bool value = true;
    nmo_status_t result = nmo_bool_from_string(&value, "false");
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_FALSE(value);
    
    teardown();
}

TEST(type_string, bool_from_string_one) {
    setup();
    
    bool value = false;
    nmo_status_t result = nmo_bool_from_string(&value, "1");
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_TRUE(value);
    
    teardown();
}

TEST(type_string, bool_from_string_zero) {
    setup();
    
    bool value = true;
    nmo_status_t result = nmo_bool_from_string(&value, "0");
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_FALSE(value);
    
    teardown();
}

TEST(type_string, bool_roundtrip) {
    setup();
    
    bool original = true;
    char buffer[64];
    bool parsed = false;
    
    nmo_status_t r1 = nmo_bool_to_string(&original, buffer, sizeof(buffer));
    ASSERT_EQ(NMO_OK, r1);
    
    nmo_status_t r2 = nmo_bool_from_string(&parsed, buffer);
    ASSERT_EQ(NMO_OK, r2);
    
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
    nmo_status_t result = nmo_vector_to_string(value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("(1, 2, 3)", buffer);
    
    teardown();
}

TEST(type_string, vector_from_string) {
    setup();
    
    float value[3] = {0.0f, 0.0f, 0.0f};
    nmo_status_t result = nmo_vector_from_string(value, "(1.5, 2.5, 3.5)");
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_FLOAT_EQ(1.5f, value[0], 0.001f);
    ASSERT_FLOAT_EQ(2.5f, value[1], 0.001f);
    ASSERT_FLOAT_EQ(3.5f, value[2], 0.001f);
    
    teardown();
}

TEST(type_string, vector_from_string_spaces) {
    setup();
    
    float value[3] = {0.0f, 0.0f, 0.0f};
    nmo_status_t result = nmo_vector_from_string(value, "( 10 , 20 , 30 )");
    
    ASSERT_EQ(NMO_OK, result);
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
    
    nmo_status_t r1 = nmo_vector_to_string(original, buffer, sizeof(buffer));
    ASSERT_EQ(NMO_OK, r1);
    
    nmo_status_t r2 = nmo_vector_from_string(parsed, buffer);
    ASSERT_EQ(NMO_OK, r2);
    
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
    nmo_status_t result = nmo_quaternion_to_string(value, buffer, sizeof(buffer));
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("(0.707, 0, 0.707, 0)", buffer);
    
    teardown();
}

TEST(type_string, quaternion_from_string) {
    setup();
    
    float value[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    nmo_status_t result = nmo_quaternion_from_string(value, "(0.5, 0.5, 0.5, 0.5)");
    
    ASSERT_EQ(NMO_OK, result);
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
    
    nmo_status_t r1 = nmo_quaternion_to_string(original, buffer, sizeof(buffer));
    ASSERT_EQ(NMO_OK, r1);
    
    nmo_status_t r2 = nmo_quaternion_from_string(parsed, buffer);
    ASSERT_EQ(NMO_OK, r2);
    
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
    nmo_status_t reg_result = nmo_type_registry_register_enum(
        registry, enum_guid, "Color", "RED=1,GREEN=2,BLUE=3");
    ASSERT_EQ(NMO_OK, reg_result);
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, enum_guid);
    ASSERT_NE(NULL, type);
    
    char buffer[64];
    int32_t value = 2;  // GREEN
    nmo_status_t result = nmo_enum_to_string(&value, type, buffer, sizeof(buffer), true);
    
    ASSERT_EQ(NMO_OK, result);
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
    nmo_status_t result = nmo_enum_to_string(&value, type, buffer, sizeof(buffer), false);
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("2", buffer);
    
    teardown();
}

TEST(type_string, enum_from_string_by_name) {
    setup();
    
    nmo_guid_t enum_guid = {0x12345678, 0x00000001};
    nmo_type_registry_register_enum(registry, enum_guid, "Color", "RED=1,GREEN=2,BLUE=3");
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, enum_guid);
    
    int32_t value = 0;
    nmo_status_t result = nmo_enum_from_string(&value, type, "BLUE");
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(3, value);
    
    teardown();
}

TEST(type_string, enum_from_string_by_value) {
    setup();
    
    nmo_guid_t enum_guid = {0x12345678, 0x00000001};
    nmo_type_registry_register_enum(registry, enum_guid, "Color", "RED=1,GREEN=2,BLUE=3");
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, enum_guid);
    
    int32_t value = 0;
    nmo_status_t result = nmo_enum_from_string(&value, type, "2");
    
    ASSERT_EQ(NMO_OK, result);
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
    
    nmo_status_t r1 = nmo_enum_to_string(&original, type, buffer, sizeof(buffer), true);
    ASSERT_EQ(NMO_OK, r1);
    
    nmo_status_t r2 = nmo_enum_from_string(&parsed, type, buffer);
    ASSERT_EQ(NMO_OK, r2);
    
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
    nmo_status_t reg_result = nmo_type_registry_register_flags(
        registry, flags_guid, "FileMode", "READ=1,WRITE=2,EXECUTE=4");
    ASSERT_EQ(NMO_OK, reg_result);
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, flags_guid);
    ASSERT_NE(NULL, type);
    
    char buffer[128];
    uint32_t value = 3;  // READ | WRITE
    nmo_status_t result = nmo_flags_to_string(&value, type, buffer, sizeof(buffer), true);
    
    ASSERT_EQ(NMO_OK, result);
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
    nmo_status_t result = nmo_flags_to_string(&value, type, buffer, sizeof(buffer), false);
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("0x7", buffer);
    
    teardown();
}

TEST(type_string, flags_from_string_by_names) {
    setup();
    
    nmo_guid_t flags_guid = {0x12345678, 0x00000002};
    nmo_type_registry_register_flags(registry, flags_guid, "FileMode", "READ=1,WRITE=2,EXECUTE=4");
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, flags_guid);
    
    uint32_t value = 0;
    nmo_status_t result = nmo_flags_from_string(&value, type, "READ|EXECUTE");
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(5u, value);  // 1 | 4
    
    teardown();
}

TEST(type_string, flags_from_string_by_hex) {
    setup();
    
    nmo_guid_t flags_guid = {0x12345678, 0x00000002};
    nmo_type_registry_register_flags(registry, flags_guid, "FileMode", "READ=1,WRITE=2,EXECUTE=4");
    
    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, flags_guid);
    
    uint32_t value = 0;
    nmo_status_t result = nmo_flags_from_string(&value, type, "0x3");
    
    ASSERT_EQ(NMO_OK, result);
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
    
    nmo_status_t r1 = nmo_flags_to_string(&original, type, buffer, sizeof(buffer), true);
    ASSERT_EQ(NMO_OK, r1);
    
    nmo_status_t r2 = nmo_flags_from_string(&parsed, type, buffer);
    ASSERT_EQ(NMO_OK, r2);
    
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
    nmo_object_id_t value = 12345;
    nmo_status_t result = nmo_object_id_to_string(&value, buffer, sizeof(buffer), NULL);
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("#12345", buffer);
    
    teardown();
}

TEST(type_string, object_id_from_string) {
    setup();
    
    nmo_object_id_t value = 0;
    nmo_status_t result = nmo_object_id_from_string(&value, "#12345", NULL);
    
    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(12345u, value);
    
    teardown();
}

TEST(type_string, object_id_roundtrip) {
    setup();
    
    nmo_object_id_t original = 99999;
    char buffer[64];
    nmo_object_id_t parsed = 0;
    
    nmo_status_t r1 = nmo_object_id_to_string(&original, buffer, sizeof(buffer), NULL);
    ASSERT_EQ(NMO_OK, r1);
    
    nmo_status_t r2 = nmo_object_id_from_string(&parsed, buffer, NULL);
    ASSERT_EQ(NMO_OK, r2);
    
    ASSERT_EQ(original, parsed);
    
    teardown();
}

TEST(type_string, type_value_to_string_object_id) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, CKPGUID_ID);
    ASSERT_NE(NULL, type);

    nmo_object_id_t value = 42;
    char buffer[64];
    nmo_status_t result = nmo_type_value_to_string(&value, type, registry, buffer, sizeof(buffer));

    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("#42", buffer);

    teardown();
}

TEST(type_string, type_value_to_string_struct_with_object_id_field) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    typedef struct test_material_group_t {
        nmo_object_id_t material_id;
    } test_material_group_t;

    static const nmo_struct_field_def_t fields[] = {
        {
            .name = "material_id",
            .type_name = NULL,
            .type_guid = CKPGUID_ID_INIT,
            .description = NULL,
            .flags = NMO_FIELD_REFERENCE,
            .default_value = NULL
        }
    };

    const nmo_struct_type_def_t def = {
        .name = "TestMaterialGroup",
        .description = NULL,
        .guid = NMO_NULL_GUID,
        .fields = fields,
        .field_count = 1,
        .alignment = 0,
        .packed = false
    };

    nmo_status_t reg_res = nmo_type_registry_register_struct(registry, &def, NULL);
    ASSERT_EQ(NMO_OK, reg_res);

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_name(registry, "TestMaterialGroup");
    ASSERT_NE(NULL, type);

    test_material_group_t value = { .material_id = 7 };
    char buffer[128];
    nmo_status_t result = nmo_type_value_to_string(&value, type, registry, buffer, sizeof(buffer));

    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("{material_id=#7}", buffer);

    teardown();
}

TEST(type_string, type_value_to_string_object_ref_with_fields) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    typedef struct test_objref_t {
        nmo_object_id_t material_id;
    } test_objref_t;

    static const nmo_type_field_t fields[] = {
        NMO_FIELD(test_objref_t, material_id, CKPGUID_ID)
    };

    nmo_type_descriptor_t desc = {
        .guid = NMO_GUID(0xDEADBEEFu, 0x00000001u),
        .id = NMO_TYPE_ID_INVALID,
        .class_id = 0,
        .category = NMO_TYPE_CATEGORY_OBJECT_REF,
        .flags = 0,
        .name = "TestObjRef",
        .description = NULL,
        .base_type = NMO_NULL_GUID,
        .base_type_id = NMO_TYPE_ID_INVALID,
        .size = (uint32_t)sizeof(test_objref_t),
        .alignment = (uint32_t)alignof(test_objref_t),
        .fields = fields,
        .field_count = sizeof(fields) / sizeof(fields[0]),
        .vtable = NULL,
        .creator_plugin_guid = NMO_NULL_GUID,
        .saver_manager = 0,
        .specialized_index = NMO_SPECIALIZED_INDEX_INVALID,
        .valid = true,
        .version = 0,
        .min_compatible_version = 0,
        .ext = NULL
    };

    ASSERT_EQ(NMO_OK, nmo_type_registry_register(registry, &desc));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, desc.guid);
    ASSERT_NE(NULL, type);

    test_objref_t value = { .material_id = 7 };
    char buffer[128];
    nmo_status_t result = nmo_type_value_to_string(&value, type, registry, buffer, sizeof(buffer));
    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("{material_id=#7}", buffer);

    teardown();
}

TEST(type_string, type_value_to_string_uint16) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, CKPGUID_UINT16);
    ASSERT_NE(NULL, type);

    uint16_t value = 65535u;
    char buffer[64];
    nmo_status_t result = nmo_type_value_to_string(&value, type, registry, buffer, sizeof(buffer));

    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("65535", buffer);

    teardown();
}

TEST(type_string, type_value_to_string_guid) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, CKPGUID_GUID);
    ASSERT_NE(NULL, type);

    nmo_guid_t value = NMO_GUID(0x12345678u, 0x9ABCDEF0u);
    char buffer[64];
    nmo_status_t result = nmo_type_value_to_string(&value, type, registry, buffer, sizeof(buffer));

    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("{12345678-9ABCDEF0}", buffer);

    teardown();
}

TEST(type_string, type_value_to_string_string_quotes) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, CKPGUID_STRING);
    ASSERT_NE(NULL, type);

    const char *value = "hello\nworld";
    char buffer[128];
    nmo_status_t result = nmo_type_value_to_string(&value, type, registry, buffer, sizeof(buffer));

    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("\"hello\\nworld\"", buffer);

    teardown();
}

TEST(type_string, type_value_from_string_guid) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, CKPGUID_GUID);
    ASSERT_NE(NULL, type);

    nmo_guid_t value = NMO_NULL_GUID;
    nmo_status_t result = nmo_type_value_from_string(&value, type, registry, "{12345678-9ABCDEF0}");

    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(0x12345678u, value.d1);
    ASSERT_EQ(0x9ABCDEF0u, value.d2);

    teardown();
}

TEST(type_string, type_value_from_string_angle_fallback) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, CKPGUID_ANGLE);
    ASSERT_NE(NULL, type);

    float value = 0.0f;
    nmo_status_t result = nmo_type_value_from_string(&value, type, registry, "90.0");

    ASSERT_EQ(NMO_OK, result);
    ASSERT_TRUE(fabs(value - 90.0f) < 0.00001f);

    teardown();
}

TEST(type_string, type_value_roundtrip_rect) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, CKPGUID_RECT);
    ASSERT_NE(NULL, type);

    nmo_rect_t r = { .left = 1.0f, .top = 2.0f, .right = 3.0f, .bottom = 4.0f };
    char buffer[128];
    ASSERT_EQ(NMO_OK, nmo_type_value_to_string(&r, type, registry, buffer, sizeof(buffer)));
    ASSERT_STR_EQ("(1, 2, 3, 4)", buffer);

    nmo_rect_t parsed = {0};
    ASSERT_EQ(NMO_OK, nmo_type_value_from_string(&parsed, type, registry, buffer));
    ASSERT_TRUE(fabs(parsed.left - 1.0f) < 0.00001f);
    ASSERT_TRUE(fabs(parsed.top - 2.0f) < 0.00001f);
    ASSERT_TRUE(fabs(parsed.right - 3.0f) < 0.00001f);
    ASSERT_TRUE(fabs(parsed.bottom - 4.0f) < 0.00001f);

    teardown();
}

TEST(type_string, type_value_roundtrip_box) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, CKPGUID_BOX);
    ASSERT_NE(NULL, type);

    nmo_box_t b = { .min = {1.0f, 2.0f, 3.0f}, .max = {4.0f, 5.0f, 6.0f} };
    char buffer[128];
    ASSERT_EQ(NMO_OK, nmo_type_value_to_string(&b, type, registry, buffer, sizeof(buffer)));
    ASSERT_STR_EQ("((1, 2, 3), (4, 5, 6))", buffer);

    nmo_box_t parsed = {0};
    ASSERT_EQ(NMO_OK, nmo_type_value_from_string(&parsed, type, registry, buffer));
    ASSERT_TRUE(fabs(parsed.min.x - 1.0f) < 0.00001f);
    ASSERT_TRUE(fabs(parsed.min.y - 2.0f) < 0.00001f);
    ASSERT_TRUE(fabs(parsed.min.z - 3.0f) < 0.00001f);
    ASSERT_TRUE(fabs(parsed.max.x - 4.0f) < 0.00001f);
    ASSERT_TRUE(fabs(parsed.max.y - 5.0f) < 0.00001f);
    ASSERT_TRUE(fabs(parsed.max.z - 6.0f) < 0.00001f);

    teardown();
}

TEST(type_string, type_value_roundtrip_eulerangles) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, CKPGUID_EULERANGLES);
    ASSERT_NE(NULL, type);

    nmo_eulerangles_t e = { .x = 10.0f, .y = 20.0f, .z = 30.0f };
    char buffer[128];
    ASSERT_EQ(NMO_OK, nmo_type_value_to_string(&e, type, registry, buffer, sizeof(buffer)));
    ASSERT_STR_EQ("(10, 20, 30)", buffer);

    nmo_eulerangles_t parsed = {0};
    ASSERT_EQ(NMO_OK, nmo_type_value_from_string(&parsed, type, registry, buffer));
    ASSERT_TRUE(fabs(parsed.x - 10.0f) < 0.00001f);
    ASSERT_TRUE(fabs(parsed.y - 20.0f) < 0.00001f);
    ASSERT_TRUE(fabs(parsed.z - 30.0f) < 0.00001f);

    teardown();
}

TEST(type_string, type_value_from_string_uint32) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, CKPGUID_UINT32);
    ASSERT_NE(NULL, type);

    uint32_t value = 0;
    nmo_status_t result = nmo_type_value_from_string(&value, type, registry, "42");

    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(42u, value);

    teardown();
}

TEST(type_string, type_value_from_string_uint64) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, CKPGUID_UINT64);
    ASSERT_NE(NULL, type);

    uint64_t value = 0;
    nmo_status_t result = nmo_type_value_from_string(&value, type, registry, "18446744073709551615");

    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(UINT64_MAX, value);

    teardown();
}

TEST(type_string, type_value_from_string_double) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, CKPGUID_DOUBLE);
    ASSERT_NE(NULL, type);

    double value = 0.0;
    nmo_status_t result = nmo_type_value_from_string(&value, type, registry, "3.5");

    ASSERT_EQ(NMO_OK, result);
    ASSERT_TRUE(fabs(value - 3.5) < 0.000001);

    teardown();
}

TEST(type_string, type_value_from_string_string) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, CKPGUID_STRING);
    ASSERT_NE(NULL, type);

    const char *value = NULL;
    nmo_status_t result = nmo_type_value_from_string(&value, type, registry, "\"hello\"");

    ASSERT_EQ(NMO_OK, result);
    ASSERT_NE(NULL, value);
    ASSERT_STR_EQ("hello", value);

    teardown();
}

TEST(type_string, type_value_from_string_uint8_overflow) {
    setup();

    ASSERT_EQ(NMO_OK, nmo_register_builtin_types(registry));

    const nmo_type_descriptor_t *type = nmo_type_registry_find_by_guid(registry, CKPGUID_UINT8);
    ASSERT_NE(NULL, type);

    uint8_t value = 0;
    nmo_status_t result = nmo_type_value_from_string(&value, type, registry, "300");

    ASSERT_NE(NMO_OK, result);

    teardown();
}

typedef struct test_object_name_session {
    int unused;
} test_object_name_session_t;

static nmo_status_t test_object_id_to_name(const void *session, nmo_object_id_t id, const char **out_name)
{
    (void)session;
    if (!out_name) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "out_name is NULL");
    }

    if (id == 42) {
        *out_name = "Ball_01";
        NMO_RETURN_OK();
    }

    *out_name = NULL;
    NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND,
        NMO_SEVERITY_ERROR, "Object not found");
}

static nmo_status_t test_object_name_to_id(const void *session, const char *name, nmo_object_id_t *out_id)
{
    (void)session;
    if (!name || !out_id) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "Invalid arguments");
    }

    if (strcmp(name, "Ball_01") == 0) {
        *out_id = 42;
        NMO_RETURN_OK();
    }

    NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND,
        NMO_SEVERITY_ERROR, "Object name not found");
}

static nmo_status_t test_object_id_to_unsafe_name(const void *session, nmo_object_id_t id, const char **out_name)
{
    (void)session;
    if (!out_name) {
        NMO_RETURN_ERROR(NMO_ERR_INVALID_ARGUMENT,
            NMO_SEVERITY_ERROR, "out_name is NULL");
    }

    if (id == 42) {
        *out_name = "Ball 01"; // unsafe (space)
        NMO_RETURN_OK();
    }

    *out_name = NULL;
    NMO_RETURN_ERROR(NMO_ERR_NOT_FOUND,
        NMO_SEVERITY_ERROR, "Object not found");
}

TEST(type_string, object_id_to_string_uses_name_resolver) {
    setup();

    test_object_name_session_t session = {0};

    nmo_type_string_set_object_resolvers(test_object_id_to_name, test_object_name_to_id);

    char buffer[64];
    nmo_object_id_t value = 42;
    nmo_status_t result = nmo_object_id_to_string(&value, buffer, sizeof(buffer), (struct nmo_session*)&session);

    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("Ball_01", buffer);

    nmo_type_string_set_object_resolvers(NULL, NULL);
    teardown();
}

TEST(type_string, object_id_from_string_uses_name_resolver) {
    setup();

    test_object_name_session_t session = {0};

    nmo_type_string_set_object_resolvers(test_object_id_to_name, test_object_name_to_id);

    nmo_object_id_t value = 0;
    nmo_status_t result = nmo_object_id_from_string(&value, "Ball_01", (struct nmo_session*)&session);

    ASSERT_EQ(NMO_OK, result);
    ASSERT_EQ(42u, value);

    nmo_type_string_set_object_resolvers(NULL, NULL);
    teardown();
}

TEST(type_string, object_id_to_string_falls_back_on_unsafe_name) {
    setup();

    test_object_name_session_t session = {0};

    nmo_type_string_set_object_resolvers(test_object_id_to_unsafe_name, test_object_name_to_id);

    char buffer[64];
    nmo_object_id_t value = 42;
    nmo_status_t result = nmo_object_id_to_string(&value, buffer, sizeof(buffer), (struct nmo_session*)&session);

    ASSERT_EQ(NMO_OK, result);
    ASSERT_STR_EQ("#42", buffer);

    nmo_type_string_set_object_resolvers(NULL, NULL);
    teardown();
}

TEST(type_string, object_id_from_string_name_not_found) {
    setup();

    test_object_name_session_t session = {0};

    nmo_type_string_set_object_resolvers(test_object_id_to_name, test_object_name_to_id);

    nmo_object_id_t value = 0;
    nmo_status_t result = nmo_object_id_from_string(&value, "DoesNotExist", (struct nmo_session*)&session);

    ASSERT_EQ(NMO_ERR_NOT_FOUND, result);

    nmo_type_string_set_object_resolvers(NULL, NULL);
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
    REGISTER_TEST(type_string, type_value_to_string_object_id);
    REGISTER_TEST(type_string, type_value_to_string_struct_with_object_id_field);
    REGISTER_TEST(type_string, type_value_to_string_object_ref_with_fields);
    REGISTER_TEST(type_string, type_value_to_string_uint16);
    REGISTER_TEST(type_string, type_value_to_string_guid);
    REGISTER_TEST(type_string, type_value_to_string_string_quotes);
    REGISTER_TEST(type_string, type_value_from_string_uint32);
    REGISTER_TEST(type_string, type_value_from_string_uint64);
    REGISTER_TEST(type_string, type_value_from_string_double);
    REGISTER_TEST(type_string, type_value_from_string_string);
    REGISTER_TEST(type_string, type_value_from_string_uint8_overflow);
    REGISTER_TEST(type_string, type_value_from_string_guid);
    REGISTER_TEST(type_string, type_value_from_string_angle_fallback);
    REGISTER_TEST(type_string, type_value_roundtrip_rect);
    REGISTER_TEST(type_string, type_value_roundtrip_box);
    REGISTER_TEST(type_string, type_value_roundtrip_eulerangles);
    REGISTER_TEST(type_string, object_id_to_string_uses_name_resolver);
    REGISTER_TEST(type_string, object_id_from_string_uses_name_resolver);
    REGISTER_TEST(type_string, object_id_to_string_falls_back_on_unsafe_name);
    REGISTER_TEST(type_string, object_id_from_string_name_not_found);
TEST_MAIN_END()

