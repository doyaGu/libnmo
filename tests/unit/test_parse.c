/**
 * @file test_parse.c
 * @brief Unit tests for shared parse helpers.
 */

#include "test_framework.h"
#include "core/nmo_parse.h"

#include <stdint.h>

TEST(parse, object_id_accepts_plain_and_hash_prefixed_decimal) {
    nmo_object_id_t id = 0;

    ASSERT_EQ(NMO_OK, nmo_parse_object_id("42", &id));
    ASSERT_EQ(42u, id);

    ASSERT_EQ(NMO_OK, nmo_parse_object_id("#123", &id));
    ASSERT_EQ(123u, id);
}

TEST(parse, object_id_rejects_bad_inputs) {
    nmo_object_id_t id = 7;

    ASSERT_NE(NMO_OK, nmo_parse_object_id("", &id));
    ASSERT_NE(NMO_OK, nmo_parse_object_id("#", &id));
    ASSERT_NE(NMO_OK, nmo_parse_object_id("12x", &id));
    ASSERT_NE(NMO_OK, nmo_parse_object_id("-1", &id));
}

TEST(parse, signed_integer_range) {
    int32_t value = 0;

    ASSERT_EQ(NMO_OK, nmo_parse_i32_range("-5", -10, 10, &value));
    ASSERT_EQ(-5, value);
    ASSERT_EQ(NMO_OK, nmo_parse_i32_range("10", -10, 10, &value));
    ASSERT_EQ(10, value);
    ASSERT_NE(NMO_OK, nmo_parse_i32_range("11", -10, 10, &value));
    ASSERT_NE(NMO_OK, nmo_parse_i32_range("1.5", -10, 10, &value));
}

TEST(parse, unsigned_integer_range) {
    uint32_t value = 0;

    ASSERT_EQ(NMO_OK, nmo_parse_u32_range("0", 0, 255, &value));
    ASSERT_EQ(0u, value);
    ASSERT_EQ(NMO_OK, nmo_parse_u32_range("255", 0, 255, &value));
    ASSERT_EQ(255u, value);
    ASSERT_NE(NMO_OK, nmo_parse_u32_range("256", 0, 255, &value));
    ASSERT_NE(NMO_OK, nmo_parse_u32_range("-1", 0, 255, &value));
}

TEST(parse, base_aware_integer_ranges) {
    int32_t signed_value = 0;
    uint32_t unsigned_value = 0;
    size_t size_value = 0;
    int64_t signed64_value = 0;
    uint64_t unsigned64_value = 0;

    ASSERT_EQ(NMO_OK, nmo_parse_i32_range_base("0x10", 0, -32, 32, &signed_value));
    ASSERT_EQ(16, signed_value);
    ASSERT_EQ(NMO_OK, nmo_parse_i32_range_base("-010", 0, -32, 32, &signed_value));
    ASSERT_EQ(-8, signed_value);
    ASSERT_NE(NMO_OK, nmo_parse_i32_range_base("0x100", 0, -32, 32, &signed_value));

    ASSERT_EQ(NMO_OK, nmo_parse_u32_range_base("0xFF", 0, 0, 255, &unsigned_value));
    ASSERT_EQ(255u, unsigned_value);
    ASSERT_EQ(NMO_OK, nmo_parse_u32_range_base("077", 0, 0, 255, &unsigned_value));
    ASSERT_EQ(63u, unsigned_value);
    ASSERT_NE(NMO_OK, nmo_parse_u32_range_base("-1", 0, 0, 255, &unsigned_value));

    ASSERT_EQ(NMO_OK, nmo_parse_size_range_base("0x100", 0, 0, SIZE_MAX, &size_value));
    ASSERT_EQ((size_t)256, size_value);
    ASSERT_NE(NMO_OK, nmo_parse_size_range_base("bad", 0, 0, SIZE_MAX, &size_value));

    ASSERT_EQ(NMO_OK, nmo_parse_i64_range_base("-0x20", 0, INT64_MIN, INT64_MAX, &signed64_value));
    ASSERT_EQ((int64_t)-32, signed64_value);
    ASSERT_EQ(NMO_OK, nmo_parse_u64_range_base("0xFFFFFFFFFFFFFFFF", 0, 0, UINT64_MAX, &unsigned64_value));
    ASSERT_EQ(UINT64_MAX, unsigned64_value);
    ASSERT_NE(NMO_OK, nmo_parse_u64_range_base("-1", 0, 0, UINT64_MAX, &unsigned64_value));
}

TEST(parse, f32_requires_full_string) {
    float value = 0.0f;

    ASSERT_EQ(NMO_OK, nmo_parse_f32("1.25", &value));
    ASSERT_FLOAT_EQ(1.25, value, 0.0001);
    ASSERT_EQ(NMO_OK, nmo_parse_f32("-2", &value));
    ASSERT_FLOAT_EQ(-2.0, value, 0.0001);
    ASSERT_NE(NMO_OK, nmo_parse_f32("1.25px", &value));
}

TEST(parse, f32_tuple_exact_count) {
    float values[4] = {0};

    ASSERT_EQ(NMO_OK, nmo_parse_f32_tuple("1,2.5,-3", values, 3));
    ASSERT_FLOAT_EQ(1.0, values[0], 0.0001);
    ASSERT_FLOAT_EQ(2.5, values[1], 0.0001);
    ASSERT_FLOAT_EQ(-3.0, values[2], 0.0001);
    ASSERT_NE(NMO_OK, nmo_parse_f32_tuple("1,2", values, 3));
    ASSERT_NE(NMO_OK, nmo_parse_f32_tuple("1,2,3,4", values, 3));
}

TEST(parse, f64_requires_full_string) {
    double value = 0.0;

    ASSERT_EQ(NMO_OK, nmo_parse_f64("3.5", &value));
    ASSERT_TRUE(value > 3.499 && value < 3.501);
    ASSERT_NE(NMO_OK, nmo_parse_f64("3.5 trailing", &value));
}

TEST(parse, f32_parenthesized_tuple_accepts_selected_separators) {
    float values[4] = {0};

    ASSERT_EQ(NMO_OK, nmo_parse_f32_parenthesized_tuple("(1, 2; 3, 4)", ",;", values, 4));
    ASSERT_FLOAT_EQ(1.0, values[0], 0.0001);
    ASSERT_FLOAT_EQ(2.0, values[1], 0.0001);
    ASSERT_FLOAT_EQ(3.0, values[2], 0.0001);
    ASSERT_FLOAT_EQ(4.0, values[3], 0.0001);
    ASSERT_NE(NMO_OK, nmo_parse_f32_parenthesized_tuple("(1; 2; 3)", ",", values, 3));
    ASSERT_NE(NMO_OK, nmo_parse_f32_parenthesized_tuple("(1, 2) trailing", ",", values, 2));
}

TEST(parse, color_hex_rgb_and_argb) {
    uint32_t color = 0;

    ASSERT_EQ(NMO_OK, nmo_parse_hex_color("112233", &color));
    ASSERT_EQ(0x112233u, color);
    ASSERT_EQ(NMO_OK, nmo_parse_hex_color("0x112233", &color));
    ASSERT_EQ(0x112233u, color);
    ASSERT_EQ(NMO_OK, nmo_parse_hex_color("AA112233", &color));
    ASSERT_EQ(0xAA112233u, color);
    ASSERT_NE(NMO_OK, nmo_parse_hex_color("12345", &color));
    ASSERT_NE(NMO_OK, nmo_parse_hex_color("GG1122", &color));
}

TEST(parse, hex_bytes_allow_spaces_and_require_capacity) {
    uint8_t bytes[4] = {0};
    size_t count = 0;

    ASSERT_EQ(NMO_OK, nmo_parse_hex_bytes("01 02ff", bytes, sizeof(bytes), &count));
    ASSERT_EQ(3u, count);
    ASSERT_EQ(0x01u, bytes[0]);
    ASSERT_EQ(0x02u, bytes[1]);
    ASSERT_EQ(0xFFu, bytes[2]);

    ASSERT_NE(NMO_OK, nmo_parse_hex_bytes("0", bytes, sizeof(bytes), &count));
    ASSERT_NE(NMO_OK, nmo_parse_hex_bytes("0102030405", bytes, sizeof(bytes), &count));
    ASSERT_NE(NMO_OK, nmo_parse_hex_bytes("01zz", bytes, sizeof(bytes), &count));
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(parse, object_id_accepts_plain_and_hash_prefixed_decimal);
    REGISTER_TEST(parse, object_id_rejects_bad_inputs);
    REGISTER_TEST(parse, signed_integer_range);
    REGISTER_TEST(parse, unsigned_integer_range);
    REGISTER_TEST(parse, base_aware_integer_ranges);
    REGISTER_TEST(parse, f32_requires_full_string);
    REGISTER_TEST(parse, f32_tuple_exact_count);
    REGISTER_TEST(parse, f64_requires_full_string);
    REGISTER_TEST(parse, f32_parenthesized_tuple_accepts_selected_separators);
    REGISTER_TEST(parse, color_hex_rgb_and_argb);
    REGISTER_TEST(parse, hex_bytes_allow_spaces_and_require_capacity);
TEST_MAIN_END()
