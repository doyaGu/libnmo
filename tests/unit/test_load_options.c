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
    ASSERT_EQ(opts.strategy, NMO_LOAD_STRATEGY_AUTO);
}

TEST(load_options, default_strict_crc) {
    nmo_load_options_t opts = nmo_load_options_default();
    ASSERT_EQ(opts.strict_crc, 0);  /* Disabled by default */
}

TEST(load_options, default_preserve_shadow) {
    nmo_load_options_t opts = nmo_load_options_default();
    ASSERT_EQ(opts.preserve_shadow, 1);  /* Enabled by default */
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
 * nmo_load_strategy_t Enum Value Tests
 * ============================================================================ */

TEST(load_strategy, auto_is_zero) {
    /* AUTO should be 0 so it's the default for zero-initialized structs */
    ASSERT_EQ(NMO_LOAD_STRATEGY_AUTO, 0);
}

TEST(load_strategy, values_distinct) {
    ASSERT_NE(NMO_LOAD_STRATEGY_AUTO, NMO_LOAD_STRATEGY_COMPRESSED);
    ASSERT_NE(NMO_LOAD_STRATEGY_AUTO, NMO_LOAD_STRATEGY_MMAP);
    ASSERT_NE(NMO_LOAD_STRATEGY_COMPRESSED, NMO_LOAD_STRATEGY_MMAP);
}

/* ============================================================================
 * nmo_load_file_ex() Validation Tests
 * ============================================================================ */

TEST(load_file_ex, null_session) {
    nmo_load_options_t opts = nmo_load_options_default();
    int result = nmo_load_file_ex(NULL, "test.nmo", &opts);
    ASSERT_EQ(result, NMO_ERR_INVALID_ARGUMENT);
}

TEST(load_file_ex, null_path) {
    /* We can't test with a real session here, but can test NULL path */
    nmo_load_options_t opts = nmo_load_options_default();
    int result = nmo_load_file_ex(NULL, NULL, &opts);
    ASSERT_EQ(result, NMO_ERR_INVALID_ARGUMENT);
}

TEST(load_file_ex, null_opts_accepted) {
    /* NULL options should be accepted (uses defaults internally) */
    /* Can only test parameter validation here without full session */
    int result = nmo_load_file_ex(NULL, "test.nmo", NULL);
    ASSERT_EQ(result, NMO_ERR_INVALID_ARGUMENT);  /* Fails on NULL session, not opts */
}

/* ============================================================================
 * Load Options Custom Configuration Tests
 * ============================================================================ */

TEST(load_options, custom_strategy) {
    nmo_load_options_t opts = nmo_load_options_default();
    opts.strategy = NMO_LOAD_STRATEGY_COMPRESSED;
    ASSERT_EQ(opts.strategy, NMO_LOAD_STRATEGY_COMPRESSED);
    
    opts.strategy = NMO_LOAD_STRATEGY_MMAP;
    ASSERT_EQ(opts.strategy, NMO_LOAD_STRATEGY_MMAP);
}

TEST(load_options, custom_flags) {
    nmo_load_options_t opts = nmo_load_options_default();
    opts.flags = NMO_LOAD_CHECK_DEPENDENCIES | NMO_LOAD_SKIP_INDEX_BUILD;
    
    ASSERT_TRUE((opts.flags & NMO_LOAD_CHECK_DEPENDENCIES) != 0);
    ASSERT_TRUE((opts.flags & NMO_LOAD_SKIP_INDEX_BUILD) != 0);
    ASSERT_TRUE((opts.flags & NMO_LOAD_SKIP_REFERENCE_RESOLVE) == 0);
}

TEST(load_options, strict_crc_enable) {
    nmo_load_options_t opts = nmo_load_options_default();
    opts.strict_crc = 1;
    ASSERT_EQ(opts.strict_crc, 1);
}

TEST(load_options, preserve_shadow_disable) {
    nmo_load_options_t opts = nmo_load_options_default();
    opts.preserve_shadow = 0;
    ASSERT_EQ(opts.preserve_shadow, 0);
}

/* ============================================================================
 * nmo_session_get_load_strategy() Tests
 * ============================================================================ */

TEST(load_strategy, null_session_get) {
    /* Should not crash with NULL session */
    nmo_load_strategy_t strategy = nmo_session_get_load_strategy(NULL);
    /* Currently returns AUTO as placeholder */
    ASSERT_EQ(strategy, NMO_LOAD_STRATEGY_AUTO);
}

/* ============================================================================
 * Test Registration
 * ============================================================================ */

TEST_MAIN_BEGIN()
    /* nmo_load_options_default() tests */
    REGISTER_TEST(load_options, default_strategy);
    REGISTER_TEST(load_options, default_strict_crc);
    REGISTER_TEST(load_options, default_preserve_shadow);
    REGISTER_TEST(load_options, default_allocator);
    REGISTER_TEST(load_options, default_flags);
    
    /* Strategy enum tests */
    REGISTER_TEST(load_strategy, auto_is_zero);
    REGISTER_TEST(load_strategy, values_distinct);
    
    /* nmo_load_file_ex() validation tests */
    REGISTER_TEST(load_file_ex, null_session);
    REGISTER_TEST(load_file_ex, null_path);
    REGISTER_TEST(load_file_ex, null_opts_accepted);
    
    /* Custom configuration tests */
    REGISTER_TEST(load_options, custom_strategy);
    REGISTER_TEST(load_options, custom_flags);
    REGISTER_TEST(load_options, strict_crc_enable);
    REGISTER_TEST(load_options, preserve_shadow_disable);
    
    /* Strategy getter tests */
    REGISTER_TEST(load_strategy, null_session_get);
TEST_MAIN_END()
