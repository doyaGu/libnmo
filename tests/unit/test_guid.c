/**
 * @file test_guid.c
 * @brief Unit tests for GUID handling
 */

#include "../test_framework.h"
#include "core/nmo_guid.h"

TEST(guid, create_guid)
{
    nmo_guid_t guid = nmo_guid_create(0x12345678, 0x9ABCDEF0);
    ASSERT_FALSE(nmo_guid_is_null(guid));
}

TEST(guid, guid_string_conversion)
{
    nmo_guid_t guid = nmo_guid_create(0x12345678, 0x9ABCDEF0);
    char buffer[40];
    int len = nmo_guid_format(guid, buffer, sizeof(buffer));
    ASSERT_TRUE(len > 0);
}

TEST(guid, parse_guid_string)
{
    const char *guid_str = "{12345678-9ABCDEF0}";
    nmo_guid_t guid = nmo_guid_parse(guid_str);
    ASSERT_FALSE(nmo_guid_is_null(guid));
}

TEST(guid, null_guid)
{
    nmo_guid_t guid = nmo_guid_create(0, 0);
    ASSERT_TRUE(nmo_guid_is_null(guid));
}

/* Error condition tests */

TEST(guid, format_null_buffer)
{
    nmo_guid_t guid = nmo_guid_create(0x12345678, 0x9ABCDEF0);
    int len = nmo_guid_format(guid, NULL, 40);
    ASSERT_EQ(len, -1);  // Should return error for NULL buffer
}

TEST(guid, format_buffer_too_small)
{
    nmo_guid_t guid = nmo_guid_create(0x12345678, 0x9ABCDEF0);
    char buffer[10];  // Too small for GUID format (needs at least 21 bytes)
    int len = nmo_guid_format(guid, buffer, sizeof(buffer));
    ASSERT_EQ(len, -1);  // Should return error for buffer too small
}

TEST(guid, format_zero_size_buffer)
{
    nmo_guid_t guid = nmo_guid_create(0x12345678, 0x9ABCDEF0);
    char buffer[40];
    int len = nmo_guid_format(guid, buffer, 0);
    ASSERT_EQ(len, -1);  // Should return error for zero size buffer
}

TEST(guid, parse_null_string)
{
    nmo_guid_t guid = nmo_guid_parse(NULL);
    ASSERT_TRUE(nmo_guid_is_null(guid));  // Should return null GUID for NULL input
}

TEST(guid, parse_empty_string)
{
    nmo_guid_t guid = nmo_guid_parse("");
    ASSERT_TRUE(nmo_guid_is_null(guid));  // Should return null GUID for empty string
}

TEST(guid, parse_invalid_format_no_braces)
{
    nmo_guid_t guid = nmo_guid_parse("12345678-9ABCDEF0");  // Missing braces
    ASSERT_TRUE(nmo_guid_is_null(guid));  // Should return null GUID for invalid format
}

TEST(guid, parse_invalid_format_wrong_length)
{
    nmo_guid_t guid = nmo_guid_parse("{12345678-9ABC}");  // Wrong length
    ASSERT_TRUE(nmo_guid_is_null(guid));  // Should return null GUID for invalid format
}

TEST(guid, parse_invalid_characters)
{
    nmo_guid_t guid = nmo_guid_parse("{ZZZZZZZZ-XXXXXXXX}");  // Invalid hex chars
    ASSERT_TRUE(nmo_guid_is_null(guid));  // Should return null GUID for invalid characters
}

TEST(guid, parse_malformed_braces)
{
    nmo_guid_t guid = nmo_guid_parse("12345678-9ABCDEF0}");  // Missing opening brace
    ASSERT_TRUE(nmo_guid_is_null(guid));  // Should return null GUID for malformed input
}

TEST(guid, parse_malformed_hyphen)
{
    nmo_guid_t guid = nmo_guid_parse("{123456789ABCDEF0}");  // Missing hyphen
    ASSERT_TRUE(nmo_guid_is_null(guid));  // Should return null GUID for malformed input
}

TEST(guid, guid_equals_different_values)
{
    nmo_guid_t guid1 = nmo_guid_create(0x12345678, 0x9ABCDEF0);
    nmo_guid_t guid2 = nmo_guid_create(0x87654321, 0x0FEDCBA9);
    ASSERT_FALSE(nmo_guid_equals(guid1, guid2));  // Different GUIDs should not be equal
}

TEST(guid, guid_equals_same_values)
{
    nmo_guid_t guid1 = nmo_guid_create(0x12345678, 0x9ABCDEF0);
    nmo_guid_t guid2 = nmo_guid_create(0x12345678, 0x9ABCDEF0);
    ASSERT_TRUE(nmo_guid_equals(guid1, guid2));  // Same GUIDs should be equal
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(guid, create_guid);
    REGISTER_TEST(guid, guid_string_conversion);
    REGISTER_TEST(guid, parse_guid_string);
    REGISTER_TEST(guid, null_guid);
    REGISTER_TEST(guid, format_null_buffer);
    REGISTER_TEST(guid, format_buffer_too_small);
    REGISTER_TEST(guid, format_zero_size_buffer);
    REGISTER_TEST(guid, parse_null_string);
    REGISTER_TEST(guid, parse_empty_string);
    REGISTER_TEST(guid, parse_invalid_format_no_braces);
    REGISTER_TEST(guid, parse_invalid_format_wrong_length);
    REGISTER_TEST(guid, parse_invalid_characters);
    REGISTER_TEST(guid, parse_malformed_braces);
    REGISTER_TEST(guid, parse_malformed_hyphen);
    REGISTER_TEST(guid, guid_equals_different_values);
    REGISTER_TEST(guid, guid_equals_same_values);
TEST_MAIN_END()
