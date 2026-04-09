/**
 * @file test_batch_rename.c
 * @brief Tests for wildcard capture and template substitution helpers
 */

#include "test_framework.h"
#include "nmo_tool_common.h"

#include <string.h>

/* ============================================================================
 * Wildcard capture tests
 * ============================================================================ */

TEST(wildcard_capture, single_star) {
    char captures[8][256];
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("Ball_*", "Ball_Red",
                                           captures, 8, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("Red", captures[0]);
}

TEST(wildcard_capture, two_stars) {
    char captures[8][256];
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("*_*", "Ball_Red",
                                           captures, 8, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(2, (int)count);
    ASSERT_STR_EQ("Ball", captures[0]);
    ASSERT_STR_EQ("Red", captures[1]);
}

TEST(wildcard_capture, no_match) {
    char captures[8][256];
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("Sphere_*", "Ball_Red",
                                           captures, 8, &count);
    ASSERT_FALSE(ok);
}

TEST(wildcard_capture, star_only) {
    char captures[8][256];
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("*", "anything",
                                           captures, 8, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("anything", captures[0]);
}

TEST(wildcard_capture, case_insensitive) {
    char captures[8][256];
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("BALL_*", "ball_Red",
                                           captures, 8, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("Red", captures[0]);
}

TEST(wildcard_capture, question_mark_not_captured) {
    char captures[8][256];
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("?all_*", "Ball_Red",
                                           captures, 8, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("Red", captures[0]);
}

TEST(wildcard_capture, empty_star_match) {
    char captures[8][256];
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("Ball*", "Ball",
                                           captures, 8, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("", captures[0]);
}

TEST(wildcard_capture, adjacent_stars) {
    char captures[8][256];
    size_t count = 0;
    /* "**" should behave like two captures; first matches empty, second matches all */
    bool ok = nmo_tool_wildcard_capture_ci("**", "abc",
                                           captures, 8, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(2, (int)count);
    /* first star can be empty, second gets everything */
    ASSERT_STR_EQ("", captures[0]);
    ASSERT_STR_EQ("abc", captures[1]);
}

TEST(wildcard_capture, null_inputs) {
    char captures[8][256];
    size_t count = 0;
    /* NULL pattern matches everything (same as existing wildcard) */
    bool ok = nmo_tool_wildcard_capture_ci(NULL, "anything",
                                           captures, 8, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(0, (int)count);
}

TEST(wildcard_capture, max_captures_exceeded) {
    char captures[1][256];
    size_t count = 0;
    /* Pattern has two stars but max_captures is 1 */
    bool ok = nmo_tool_wildcard_capture_ci("*_*", "Ball_Red",
                                           captures, 1, &count);
    ASSERT_FALSE(ok);
}

/* ============================================================================
 * Template substitution tests
 * ============================================================================ */

TEST(template, simple_capture) {
    char captures[8][256];
    strncpy(captures[0], "Red", 256);
    char out[256];
    int rc = nmo_tool_apply_rename_template("Sphere_{1}", "Ball_Red",
                                            captures, 1, out, sizeof(out));
    ASSERT_EQ(0, rc);
    ASSERT_STR_EQ("Sphere_Red", out);
}

TEST(template, full_match_ref) {
    char captures[8][256];
    char out[256];
    int rc = nmo_tool_apply_rename_template("prefix_{0}", "OldName",
                                            captures, 0, out, sizeof(out));
    ASSERT_EQ(0, rc);
    ASSERT_STR_EQ("prefix_OldName", out);
}

TEST(template, two_captures_swap) {
    char captures[8][256];
    strncpy(captures[0], "Ball", 256);
    strncpy(captures[1], "Red", 256);
    char out[256];
    int rc = nmo_tool_apply_rename_template("{2}_{1}", "Ball_Red",
                                            captures, 2, out, sizeof(out));
    ASSERT_EQ(0, rc);
    ASSERT_STR_EQ("Red_Ball", out);
}

TEST(template, literal_braces) {
    char captures[8][256];
    char out[256];
    int rc = nmo_tool_apply_rename_template("a{{b}}", "x",
                                            captures, 0, out, sizeof(out));
    ASSERT_EQ(0, rc);
    ASSERT_STR_EQ("a{b}", out);
}

TEST(template, invalid_capture_ref) {
    char captures[8][256];
    strncpy(captures[0], "Red", 256);
    char out[256];
    /* {3} is out of range with only 1 capture */
    int rc = nmo_tool_apply_rename_template("{3}", "x",
                                            captures, 1, out, sizeof(out));
    ASSERT_EQ(-1, rc);
}

TEST(template, buffer_overflow) {
    char captures[8][256];
    strncpy(captures[0], "LongString", 256);
    char out[4]; /* too small */
    int rc = nmo_tool_apply_rename_template("prefix_{1}", "x",
                                            captures, 1, out, sizeof(out));
    ASSERT_EQ(-1, rc);
}

TEST(template, no_placeholders) {
    char captures[8][256];
    char out[256];
    int rc = nmo_tool_apply_rename_template("literal_name", "x",
                                            captures, 0, out, sizeof(out));
    ASSERT_EQ(0, rc);
    ASSERT_STR_EQ("literal_name", out);
}

TEST(template, null_template) {
    char captures[8][256];
    char out[256];
    int rc = nmo_tool_apply_rename_template(NULL, "x",
                                            captures, 0, out, sizeof(out));
    ASSERT_EQ(-1, rc);
}

/* ============================================================================
 * Main
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* Wildcard capture tests */
    REGISTER_TEST(wildcard_capture, single_star);
    REGISTER_TEST(wildcard_capture, two_stars);
    REGISTER_TEST(wildcard_capture, no_match);
    REGISTER_TEST(wildcard_capture, star_only);
    REGISTER_TEST(wildcard_capture, case_insensitive);
    REGISTER_TEST(wildcard_capture, question_mark_not_captured);
    REGISTER_TEST(wildcard_capture, empty_star_match);
    REGISTER_TEST(wildcard_capture, adjacent_stars);
    REGISTER_TEST(wildcard_capture, null_inputs);
    REGISTER_TEST(wildcard_capture, max_captures_exceeded);

    /* Template substitution tests */
    REGISTER_TEST(template, simple_capture);
    REGISTER_TEST(template, full_match_ref);
    REGISTER_TEST(template, two_captures_swap);
    REGISTER_TEST(template, literal_braces);
    REGISTER_TEST(template, invalid_capture_ref);
    REGISTER_TEST(template, buffer_overflow);
    REGISTER_TEST(template, no_placeholders);
    REGISTER_TEST(template, null_template);
TEST_MAIN_END()
