/**
 * @file test_hex.c
 * @brief Unit tests for hex encoding helpers.
 */

#include "test_framework.h"
#include "core/nmo_hex.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

TEST(hex, write_byte_lowercase) {
    char out[2];
    nmo_hex_write_byte(out, 0xAB, false);
    ASSERT_EQ((int)'a', (int)out[0]);
    ASSERT_EQ((int)'b', (int)out[1]);

    nmo_hex_write_byte(out, 0x00, false);
    ASSERT_EQ((int)'0', (int)out[0]);
    ASSERT_EQ((int)'0', (int)out[1]);

    nmo_hex_write_byte(out, 0xFF, false);
    ASSERT_EQ((int)'f', (int)out[0]);
    ASSERT_EQ((int)'f', (int)out[1]);
}

TEST(hex, write_byte_uppercase) {
    char out[2];
    nmo_hex_write_byte(out, 0xAB, true);
    ASSERT_EQ((int)'A', (int)out[0]);
    ASSERT_EQ((int)'B', (int)out[1]);

    nmo_hex_write_byte(out, 0xCD, true);
    ASSERT_EQ((int)'C', (int)out[0]);
    ASSERT_EQ((int)'D', (int)out[1]);

    nmo_hex_write_byte(out, 0xFF, true);
    ASSERT_EQ((int)'F', (int)out[0]);
    ASSERT_EQ((int)'F', (int)out[1]);
}

TEST(hex, bytes_to_string_basic) {
    uint8_t bytes[] = {0xDE, 0xAD, 0xBE, 0xEF};
    char *result = nmo_hex_bytes_to_string(bytes, 4, false);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ("deadbeef", result);
    free(result);
}

TEST(hex, bytes_to_string_empty) {
    char *result = nmo_hex_bytes_to_string(NULL, 0, false);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ("", result);
    free(result);
}

TEST(hex, bytes_to_string_null_with_nonzero_len) {
    char *result = nmo_hex_bytes_to_string(NULL, 10, false);
    ASSERT_NULL(result);
}

TEST(hex, bytes_to_string_overflow_returns_null) {
    /* SIZE_MAX/2 + 1 would overflow when computing len * 2 + 1 */
    size_t overflow_len = (SIZE_MAX - 1) / 2 + 1;
    char *result = nmo_hex_bytes_to_string((const uint8_t *)"dummy", overflow_len, false);
    ASSERT_NULL(result);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(hex, write_byte_lowercase);
    REGISTER_TEST(hex, write_byte_uppercase);
    REGISTER_TEST(hex, bytes_to_string_basic);
    REGISTER_TEST(hex, bytes_to_string_empty);
    REGISTER_TEST(hex, bytes_to_string_null_with_nonzero_len);
    REGISTER_TEST(hex, bytes_to_string_overflow_returns_null);
TEST_MAIN_END()
