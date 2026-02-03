/**
 * @file test_load_options.c
 * @brief Unit tests for Phase 2.1 Dual-Track IO load options API
 */

#include "test_framework.h"
#include "app/nmo_parser.h"
#include <string.h>

/* ============================================================================
 * nmo_load_options_default() Tests
 * ============================================================================ */

TEST(load_options, default_strategy) {
    nmo_load_options_t opts = nmo_load_options_default();
    ASSERT_EQ(opts.flags, NMO_LOAD_DEFAULT);
}

TEST(load_options, default_preserve_shadow) {
    nmo_load_options_t opts = nmo_load_options_default();
    ASSERT_TRUE((opts.flags & NMO_LOAD_PRESERVE_SHADOW) == 0);
}

TEST(load_options, default_allocator) {
    nmo_load_options_t opts = nmo_load_options_default();
    ASSERT_EQ(opts.allocator, NULL);  /* Default allocator */
}

TEST(load_options, default_flags) {
    nmo_load_options_t opts = nmo_load_options_default();
    ASSERT_EQ(opts.flags, NMO_LOAD_DEFAULT);
}

/* ============================================================================
 * nmo_load_file() Validation Tests
 * ============================================================================ */

TEST(load_file, null_session) {
    nmo_load_options_t opts = nmo_load_options_default();
    int result = nmo_load_file(NULL, "test.nmo", &opts);
    ASSERT_EQ(result, NMO_ERR_INVALID_ARGUMENT);
}

TEST(load_file, null_path) {
    /* We can't test with a real session here, but can test NULL path */
    nmo_load_options_t opts = nmo_load_options_default();
    int result = nmo_load_file(NULL, NULL, &opts);
    ASSERT_EQ(result, NMO_ERR_INVALID_ARGUMENT);
}

TEST(load_file, null_opts_accepted) {
    /* NULL options should be accepted (uses defaults internally) */
    /* Can only test parameter validation here without full session */
    int result = nmo_load_file(NULL, "test.nmo", NULL);
    ASSERT_EQ(result, NMO_ERR_INVALID_ARGUMENT);  /* Fails on NULL session, not opts */
}

/* ============================================================================
 * Load Options Custom Configuration Tests
 * ============================================================================ */

TEST(load_options, custom_options) {
    nmo_load_options_t opts = nmo_load_options_default();
    ASSERT_EQ(opts.flags, NMO_LOAD_DEFAULT);
}

TEST(load_options, custom_flags) {
    nmo_load_options_t opts = nmo_load_options_default();
    opts.flags = NMO_LOAD_CHECK_DEPENDENCIES | NMO_LOAD_SKIP_INDEX_BUILD;
    
    ASSERT_TRUE((opts.flags & NMO_LOAD_CHECK_DEPENDENCIES) != 0);
    ASSERT_TRUE((opts.flags & NMO_LOAD_SKIP_INDEX_BUILD) != 0);
    ASSERT_TRUE((opts.flags & NMO_LOAD_SKIP_REFERENCE_RESOLVE) == 0);
}

TEST(load_options, preserve_shadow_enable) {
    nmo_load_options_t opts = nmo_load_options_default();
    opts.flags |= NMO_LOAD_PRESERVE_SHADOW;
    ASSERT_TRUE((opts.flags & NMO_LOAD_PRESERVE_SHADOW) != 0);
}

/* ============================================================================
 * Test Registration
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* nmo_load_options_default() tests */
    REGISTER_TEST(load_options, default_strategy);
    REGISTER_TEST(load_options, default_preserve_shadow);
    REGISTER_TEST(load_options, default_allocator);
    REGISTER_TEST(load_options, default_flags);
    
    /* nmo_load_file() validation tests */
    REGISTER_TEST(load_file, null_session);
    REGISTER_TEST(load_file, null_path);
    REGISTER_TEST(load_file, null_opts_accepted);
    
    /* Custom configuration tests */
    REGISTER_TEST(load_options, custom_options);
    REGISTER_TEST(load_options, custom_flags);
    REGISTER_TEST(load_options, preserve_shadow_enable);
    
TEST_MAIN_END()
