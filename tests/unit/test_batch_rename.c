/**
 * @file test_batch_rename.c
 * @brief Tests for wildcard capture and template substitution helpers
 */

#include "test_framework.h"
#include "nmo_tool_common.h"

#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Wildcard capture tests
 * ============================================================================ */

TEST(wildcard_capture, single_star) {
    char **captures = NULL;
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("Ball_*", "Ball_Red", &captures, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("Red", captures[0]);
    nmo_tool_captures_free(captures, count);
}

TEST(wildcard_capture, two_stars) {
    char **captures = NULL;
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("*_*", "Ball_Red", &captures, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(2, (int)count);
    ASSERT_STR_EQ("Ball", captures[0]);
    ASSERT_STR_EQ("Red", captures[1]);
    nmo_tool_captures_free(captures, count);
}

TEST(wildcard_capture, no_match) {
    char **captures = NULL;
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("Sphere_*", "Ball_Red", &captures, &count);
    ASSERT_FALSE(ok);
    ASSERT_NULL(captures);
    ASSERT_EQ(0, (int)count);
}

TEST(wildcard_capture, star_only) {
    char **captures = NULL;
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("*", "anything", &captures, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("anything", captures[0]);
    nmo_tool_captures_free(captures, count);
}

TEST(wildcard_capture, case_insensitive) {
    char **captures = NULL;
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("BALL_*", "ball_Red", &captures, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("Red", captures[0]);
    nmo_tool_captures_free(captures, count);
}

TEST(wildcard_capture, question_mark_not_captured) {
    char **captures = NULL;
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("?all_*", "Ball_Red", &captures, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("Red", captures[0]);
    nmo_tool_captures_free(captures, count);
}

TEST(wildcard_capture, empty_star_match) {
    char **captures = NULL;
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("Ball*", "Ball", &captures, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1, (int)count);
    ASSERT_STR_EQ("", captures[0]);
    nmo_tool_captures_free(captures, count);
}

TEST(wildcard_capture, adjacent_stars) {
    char **captures = NULL;
    size_t count = 0;
    /* "**" should behave like two captures; first matches empty, second matches all */
    bool ok = nmo_tool_wildcard_capture_ci("**", "abc", &captures, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(2, (int)count);
    /* first star can be empty, second gets everything */
    ASSERT_STR_EQ("", captures[0]);
    ASSERT_STR_EQ("abc", captures[1]);
    nmo_tool_captures_free(captures, count);
}

TEST(wildcard_capture, null_inputs) {
    char **captures = NULL;
    size_t count = 0;
    /* NULL pattern matches everything (same as existing wildcard) */
    bool ok = nmo_tool_wildcard_capture_ci(NULL, "anything", &captures, &count);
    ASSERT_TRUE(ok);
    ASSERT_NULL(captures);
    ASSERT_EQ(0, (int)count);
}

TEST(wildcard_capture, no_star_match_has_no_captures) {
    char **captures = NULL;
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("Ball_?ed", "Ball_Red", &captures, &count);
    ASSERT_TRUE(ok);
    ASSERT_NULL(captures);
    ASSERT_EQ(0, (int)count);
}

TEST(wildcard_capture, long_capture_is_not_truncated) {
    char long_name[1024];
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    char **captures = NULL;
    size_t count = 0;
    bool ok = nmo_tool_wildcard_capture_ci("*", long_name, &captures, &count);
    ASSERT_TRUE(ok);
    ASSERT_EQ(1, (int)count);
    ASSERT_EQ(sizeof(long_name) - 1, strlen(captures[0]));
    nmo_tool_captures_free(captures, count);
}

/* ============================================================================
 * Template substitution tests
 * ============================================================================ */

TEST(template, simple_capture) {
    const char *captures[] = {"Red"};
    char *out = nmo_tool_apply_rename_template("Sphere_{1}", "Ball_Red", captures, 1);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ("Sphere_Red", out);
    free(out);
}

TEST(template, full_match_ref) {
    char *out = nmo_tool_apply_rename_template("prefix_{0}", "OldName", NULL, 0);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ("prefix_OldName", out);
    free(out);
}

TEST(template, two_captures_swap) {
    const char *captures[] = {"Ball", "Red"};
    char *out = nmo_tool_apply_rename_template("{2}_{1}", "Ball_Red", captures, 2);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ("Red_Ball", out);
    free(out);
}

TEST(template, literal_braces) {
    char *out = nmo_tool_apply_rename_template("a{{b}}", "x", NULL, 0);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ("a{b}", out);
    free(out);
}

TEST(template, invalid_capture_ref) {
    const char *captures[] = {"Red"};
    /* {3} is out of range with only 1 capture */
    char *out = nmo_tool_apply_rename_template("{3}", "x", captures, 1);
    ASSERT_NULL(out);
}

TEST(template, long_expansion) {
    char long_capture[2048];
    memset(long_capture, 'y', sizeof(long_capture) - 1);
    long_capture[sizeof(long_capture) - 1] = '\0';
    const char *captures[] = {long_capture};
    char *out = nmo_tool_apply_rename_template("prefix_{1}_{1}", "x", captures, 1);
    ASSERT_NOT_NULL(out);
    ASSERT_EQ(strlen("prefix__") + 2u * (sizeof(long_capture) - 1), strlen(out));
    free(out);
}

TEST(template, no_placeholders) {
    char *out = nmo_tool_apply_rename_template("literal_name", "x", NULL, 0);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ("literal_name", out);
    free(out);
}

TEST(template, empty_template) {
    char *out = nmo_tool_apply_rename_template("", "x", NULL, 0);
    ASSERT_NOT_NULL(out);
    ASSERT_STR_EQ("", out);
    free(out);
}

TEST(template, null_template) {
    char *out = nmo_tool_apply_rename_template(NULL, "x", NULL, 0);
    ASSERT_NULL(out);
}

TEST(template, stray_closing_brace) {
    char *out = nmo_tool_apply_rename_template("a}b", "x", NULL, 0);
    ASSERT_NULL(out);
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
    REGISTER_TEST(wildcard_capture, no_star_match_has_no_captures);
    REGISTER_TEST(wildcard_capture, long_capture_is_not_truncated);

    /* Template substitution tests */
    REGISTER_TEST(template, simple_capture);
    REGISTER_TEST(template, full_match_ref);
    REGISTER_TEST(template, two_captures_swap);
    REGISTER_TEST(template, literal_braces);
    REGISTER_TEST(template, invalid_capture_ref);
    REGISTER_TEST(template, long_expansion);
    REGISTER_TEST(template, no_placeholders);
    REGISTER_TEST(template, empty_template);
    REGISTER_TEST(template, null_template);
    REGISTER_TEST(template, stray_closing_brace);
TEST_MAIN_END()
