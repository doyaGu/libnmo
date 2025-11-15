/**
 * @file test_string.c
 * @brief Unit tests for nmo_string (XString equivalent).
 */

#include "nmo.h"
#include "test_framework.h"

static void assert_ok(nmo_result_t result) {
    ASSERT_EQ(NMO_OK, result.code);
}

TEST(nmo_string, init_and_assign) {
    nmo_string_t str = NMO_STRING_INITIALIZER;
    assert_ok(nmo_string_init(&str, NULL));
    ASSERT_TRUE(nmo_string_empty(&str));
    ASSERT_STR_EQ("", nmo_string_c_str(&str));

    assert_ok(nmo_string_assign(&str, "Virtools"));
    ASSERT_EQ(8u, nmo_string_length(&str));
    ASSERT_STR_EQ("Virtools", nmo_string_c_str(&str));

    assert_ok(nmo_string_assign_len(&str, "libnmo", 3));
    ASSERT_EQ(3u, nmo_string_length(&str));
    ASSERT_STR_EQ("lib", nmo_string_c_str(&str));

    nmo_string_dispose(&str);
}

TEST(nmo_string, append_insert_erase) {
    nmo_string_t str;
    assert_ok(nmo_string_init(&str, NULL));

    assert_ok(nmo_string_append(&str, "Chunk"));
    assert_ok(nmo_string_append_char(&str, ' '));
    assert_ok(nmo_string_append(&str, "Parser"));
    ASSERT_STR_EQ("Chunk Parser", nmo_string_c_str(&str));

    assert_ok(nmo_string_insert(&str, 5, "-State", 6));
    ASSERT_STR_EQ("Chunk-State Parser", nmo_string_c_str(&str));

    assert_ok(nmo_string_erase(&str, 5, 6));
    ASSERT_STR_EQ("Chunk Parser", nmo_string_c_str(&str));

    nmo_string_dispose(&str);
}

TEST(nmo_string, replace_trim_case) {
    nmo_string_t str;
    assert_ok(nmo_string_init_cstr(&str, "  guid_guid_guid  ", NULL));

    size_t replaced = 0;
    assert_ok(nmo_string_replace_all(&str,
                                     NMO_STRING_VIEW_LITERAL("guid"),
                                     NMO_STRING_VIEW_LITERAL("GUID"),
                                     &replaced));
    ASSERT_EQ(3u, replaced);
    ASSERT_STR_EQ("  GUID_GUID_GUID  ", nmo_string_c_str(&str));

    nmo_string_trim(&str);
    ASSERT_STR_EQ("GUID_GUID_GUID", nmo_string_c_str(&str));

    nmo_string_to_lower(&str);
    ASSERT_STR_EQ("guid_guid_guid", nmo_string_c_str(&str));

    nmo_string_to_upper(&str);
    ASSERT_STR_EQ("GUID_GUID_GUID", nmo_string_c_str(&str));

    nmo_string_dispose(&str);
}

TEST(nmo_string, search_and_compare) {
    nmo_string_t str;
    assert_ok(nmo_string_init_cstr(&str, "Header1Chunk", NULL));

    ASSERT_EQ(0u, nmo_string_find(&str, NMO_STRING_VIEW_LITERAL("Head"), 0));
    ASSERT_EQ(7u, nmo_string_find_char(&str, 'C', 0));
    ASSERT_EQ(10u, nmo_string_rfind_char(&str, 'n', SIZE_MAX));

    ASSERT_TRUE(nmo_string_contains(&str, NMO_STRING_VIEW_LITERAL("Chunk")));
    ASSERT_TRUE(nmo_string_starts_with(&str, NMO_STRING_VIEW_LITERAL("Head")));
    ASSERT_TRUE(nmo_string_istarts_with(&str, NMO_STRING_VIEW_LITERAL("header")));
    ASSERT_TRUE(nmo_string_ends_with(&str, NMO_STRING_VIEW_LITERAL("Chunk")));
    ASSERT_TRUE(nmo_string_iends_with(&str, NMO_STRING_VIEW_LITERAL("chunk")));

    nmo_string_t other;
    assert_ok(nmo_string_init_cstr(&other, "header1chunk", NULL));
    ASSERT_LT(nmo_string_compare(&str, &other), 0);
    ASSERT_EQ(0, nmo_string_icompare_view(&str, nmo_string_view_from_cstr("header1chunk")));

    nmo_string_dispose(&str);
    nmo_string_dispose(&other);
}

TEST(nmo_string, format_and_numeric) {
    nmo_string_t str;
    assert_ok(nmo_string_init(&str, NULL));

    assert_ok(nmo_string_format(&str, "%s-%d", "manager", 32));
    ASSERT_STR_EQ("manager-32", nmo_string_c_str(&str));

    assert_ok(nmo_string_append_format(&str, "_%0.2f", 3.5));
    ASSERT_STR_EQ("manager-32_3.50", nmo_string_c_str(&str));

    int int_val = 0;
    ASSERT_TRUE(nmo_string_to_int(&str, &int_val) == 0);

    assert_ok(nmo_string_assign(&str, "4096"));
    ASSERT_TRUE(nmo_string_to_int(&str, &int_val));
    ASSERT_EQ(4096, int_val);

    uint32_t u32 = 0;
    assert_ok(nmo_string_assign(&str, "65535"));
    ASSERT_TRUE(nmo_string_to_uint32(&str, &u32));
    ASSERT_EQ(65535u, u32);

    double dbl = 0.0;
    assert_ok(nmo_string_assign(&str, "3.14159"));
    ASSERT_TRUE(nmo_string_to_double(&str, &dbl));
    ASSERT_FLOAT_EQ(3.14159, dbl, 1e-6);

    assert_ok(nmo_string_from_int(&str, -42));
    ASSERT_STR_EQ("-42", nmo_string_c_str(&str));

    assert_ok(nmo_string_from_float(&str, 1.25f));
    ASSERT_STR_EQ("1.25", nmo_string_c_str(&str));

    nmo_string_dispose(&str);
}

TEST(nmo_string, substring_helpers) {
    nmo_string_t original;
    assert_ok(nmo_string_init_cstr(&original, "Manager/Parser", NULL));

    nmo_string_view_t head;
    ASSERT_TRUE(nmo_string_slice_view(&original, 0, 7, &head));
    ASSERT_EQ(7u, head.length);
    ASSERT_MEM_EQ(head.data, "Manager", 7);

    nmo_string_view_t tail;
    ASSERT_TRUE(nmo_string_slice_view(&original, 8, SIZE_MAX, &tail));
    ASSERT_EQ(6u, tail.length);
    ASSERT_MEM_EQ(tail.data, "Parser", 6);

    nmo_string_view_t entire = nmo_string_view_from_string(&original);
    ASSERT_EQ(nmo_string_length(&original), entire.length);
    ASSERT_EQ(original.data, entire.data);

    nmo_string_t copy;
    assert_ok(nmo_string_init(&copy, NULL));
    assert_ok(nmo_string_substr(&copy, &original, 8, 16));
    ASSERT_STR_EQ("Parser", nmo_string_c_str(&copy));

    ASSERT_FALSE(nmo_string_slice_view(&original, 32, 4, &head));

    nmo_string_dispose(&original);
    nmo_string_dispose(&copy);
}

TEST(nmo_string, equals_helpers) {
    nmo_string_t upper, lower;
    assert_ok(nmo_string_init_cstr(&upper, "Header", NULL));
    assert_ok(nmo_string_init_cstr(&lower, "header", NULL));

    nmo_string_t clone;
    assert_ok(nmo_string_init(&clone, NULL));
    assert_ok(nmo_string_copy(&clone, &upper));

    ASSERT_TRUE(nmo_string_equals(&upper, &clone));
    ASSERT_FALSE(nmo_string_equals(&upper, &lower));

    ASSERT_TRUE(nmo_string_equals_view(&upper, NMO_STRING_VIEW_LITERAL("Header")));
    ASSERT_FALSE(nmo_string_equals_view(&upper, NMO_STRING_VIEW_LITERAL("header")));

    ASSERT_TRUE(nmo_string_iequals_view(&upper, NMO_STRING_VIEW_LITERAL("header")));
    ASSERT_TRUE(nmo_string_iequals_view(&lower, nmo_string_view_from_string(&upper)));

    nmo_string_dispose(&upper);
    nmo_string_dispose(&lower);
    nmo_string_dispose(&clone);
}

TEST(nmo_string, pop_back_and_capacity) {
    nmo_string_t str;
    assert_ok(nmo_string_init_cstr(&str, "Chunk!", NULL));

    char last = '\0';
    assert_ok(nmo_string_pop_back(&str, &last));
    ASSERT_EQ('!', last);
    ASSERT_STR_EQ("Chunk", nmo_string_c_str(&str));

    assert_ok(nmo_string_pop_back(&str, NULL));
    ASSERT_STR_EQ("Chun", nmo_string_c_str(&str));

    size_t reserved = nmo_string_capacity(&str);
    assert_ok(nmo_string_reserve(&str, reserved + 16));
    ASSERT_TRUE(nmo_string_capacity(&str) >= reserved + 16);

    assert_ok(nmo_string_shrink_to_fit(&str));
    ASSERT_EQ(nmo_string_length(&str), nmo_string_capacity(&str));

    nmo_string_clear(&str);
    nmo_result_t pop_empty = nmo_string_pop_back(&str, &last);
    ASSERT_EQ(NMO_ERR_INVALID_STATE, pop_empty.code);

    nmo_string_dispose(&str);
}

TEST(nmo_string, numeric_failures) {
    nmo_string_t str;
    assert_ok(nmo_string_init(&str, NULL));

    assert_ok(nmo_string_assign(&str, "12abc"));
    int value = 0;
    ASSERT_FALSE(nmo_string_to_int(&str, &value));

    assert_ok(nmo_string_assign(&str, "4294967297"));
    uint32_t u32 = 0;
    ASSERT_FALSE(nmo_string_to_uint32(&str, &u32));

    assert_ok(nmo_string_assign(&str, "not-a-float"));
    double dbl = 0.0;
    ASSERT_FALSE(nmo_string_to_double(&str, &dbl));

    nmo_string_dispose(&str);
}

TEST_MAIN_BEGIN()
    REGISTER_TEST(nmo_string, init_and_assign);
    REGISTER_TEST(nmo_string, append_insert_erase);
    REGISTER_TEST(nmo_string, replace_trim_case);
    REGISTER_TEST(nmo_string, search_and_compare);
    REGISTER_TEST(nmo_string, format_and_numeric);
    REGISTER_TEST(nmo_string, substring_helpers);
    REGISTER_TEST(nmo_string, equals_helpers);
    REGISTER_TEST(nmo_string, pop_back_and_capacity);
    REGISTER_TEST(nmo_string, numeric_failures);
TEST_MAIN_END()
